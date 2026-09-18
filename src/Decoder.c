////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// PaperBack -- high density backups on the plain paper                       //
//                                                                            //
// Copyright (c) 2007 Oleh Yuschuk                                            //
// ollydbg at t-online de (set Subject to 'paperback' or be filtered out!)    //
//                                                                            //
//                                                                            //
// This file is part of PaperBack.                                            //
//                                                                            //
// Paperback is free software; you can redistribute it and/or modify it under //
// the terms of the GNU General Public License as published by the Free       //
// Software Foundation; either version 3 of the License, or (at your option)  //
// any later version.                                                         //
//                                                                            //
// PaperBack is distributed in the hope that it will be useful, but WITHOUT   //
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      //
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for   //
// more details.                                                              //
//                                                                            //
// You should have received a copy of the GNU General Public License along    //
// with this program. If not, see <http://www.gnu.org/licenses/>.             //
//                                                                            //
//                                                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#include <windows.h>
#endif
#include <stdlib.h>
#include <math.h>

#include "paperbak.h"
#include "Resource.h"

#define NHYST          1024            // Number of points in histogramm
#define NPEAK          32              // Maximal number of peaks
#define SUBDX          8               // X size of subblock, pixels
#define SUBDY          8               // Y size of subblock, pixels
#define NRULER         49              // Profile bins, cell centre to centre
#define NRULERDOT      400             // Isolated dots the ruler averages

// Given hystogramm h of length n points, locates black peaks and determines
// phase and step of the grid.
static float Findpeaks(int *h,int n,float *bestpeak,float *beststep) {
  int i,j,k,ampl,amin,amax,d,l[NHYST],limit,sum;
  int npeak,dist,bestdist,bestcount,height[NPEAK];
  float area,moment,peak[NPEAK],weight[NPEAK];
  float x0,step,sn,sx,sy,sxx,syy,sxy;
  // I expect at least 16 and at most NHYST points in the histogramm.
  if (n<16) return 0.0;
  if (n>NHYST) n=NHYST;
  // Get absolute minimum and maximum.
  amin=amax=h[0];
  for (i=1; i<n; i++) {
    if (h[i]<amin) amin=h[i];
    if (h[i]>amax) amax=h[i]; };
  // Remove gradients by shadowing over 32 pixels. May create small artefacts
  // in the vicinity of the main peak.
  d=(amax-amin+16)/32;
  ampl=h[0];
  for (i=0; i<n; i++) {
    l[i]=ampl=max(ampl-d,h[i]); };
  amax=0;
  for (i=n-1; i>=0; i--) {
    ampl=max(ampl-d,l[i]);
    l[i]=ampl-h[i];
    amax=max(amax,l[i]); };

// TRY TO COMPARE WITH SECOND LARGE PEAK?

  // I set peak limit to 3/4 of the amplitude of the highest peak. This
  // solution at least works in 90% of all cases.
  limit=amax*3/4;
  if (limit==0) limit=1;
  // Start search and skip incomplete first peak.
  i=0; npeak=0;
  while (i<n && l[i]>limit) i++;
  // Find peaks.
  while (i<n && npeak<NPEAK) {
    // Find next peak.
    while (i<n && l[i]<=limit) i++;
    // Calculate peak parameters.
    area=0.0; moment=0.0; amax=0;
    while (i<n && l[i]>limit) {
      ampl=l[i]-limit;
      area+=ampl;
      moment+=ampl*i;
      amax=max(amax,l[i]);
      i++; };
    // Don't process incomplete peaks.
    if (i>=n) break;
    // Add peak to the list, removing weak artefacts.
    if (npeak>0) {
      if (amax*8<height[npeak-1]) continue;
      if (amax>height[npeak-1]*8) npeak--; };
    peak[npeak]=moment/area;
    weight[npeak]=area;
    height[npeak]=amax;
    npeak++;
  };
  // At least two peaks are necessary to detect the step.
  if (npeak<2) return 0.0;
  // Calculate all possible distances between the found peaks.
  for (i=0; i<n; i++) l[i]=0;
  for (i=0; i<npeak-1; i++) {
    for (j=i+1; j<npeak; j++) {
      l[(int)(peak[j]-peak[i])]++;
    };
  };
  // Find group with the maximal number of peaks. I allow for approximately 3%
  // dispersion. Distances under 16 pixels are too short to be real. Caveat:
  // this method can't distinguish direct sequence from interleaved.
  bestdist=0; bestcount=0;
  for (i=16; i<n; i++) {
    if (l[i]==0) continue;
    sum=0;
    for (j=i; j<=i+i/33+1 && j<n; j++) sum+=l[j];
    if (sum>bestcount) {               // Shorter is better
      bestdist=i;
      bestcount=sum;
    };
  };
  if (bestdist==0) return 0.0;
  // Now determine the parameters of the sequence. The method I use is not very
  // good but usually sufficient.
  sn=sx=sy=sxx=syy=sxy=0.0;
  moment=0.0;
  for (i=0; i<npeak-1; i++) {
    for (j=i+1; j<npeak; j++) {
      dist=peak[j]-peak[i];
      if (dist<bestdist || dist>=bestdist+bestdist/33+1) continue;
      if (sn==0.0)                     // First link
        k=0;
      else {
        x0=(sx*sxy-sxx*sy)/(sx*sx-sn*sxx);
        step=(sx*sy-sn*sxy)/(sx*sx-sn*sxx);
        k=(peak[i]-x0+step/2.0)/step; };
      sn+=2.0;
      sx+=k*2+1;
      sy+=peak[i]+peak[j];
      sxx+=k*k+(k+1)*(k+1);
      syy+=peak[i]*peak[i]+peak[j]*peak[j];
      sxy+=peak[i]*k+peak[j]*(k+1);
      moment+=height[i]+height[j];
    };
  };
  *bestpeak=(sx*sxy-sxx*sy)/(sx*sx-sn*sxx);
  *beststep=(sx*sy-sn*sxy)/(sx*sx-sn*sxx);
  return moment/sn;
};

// Confidence of a byte that contains no dot yet.
#define MAXMARGIN 0x7FFFFFFF

// The sheet carries its own ruler. A dot is printed pb_dotpercent of the cell
// wide, and ink on paper only ever spreads: through a printer, a sheet of paper
// and a pane of glass a dot comes back wider, never narrower. So a dot that
// measures narrower than it was printed says the scan squared off its edges --
// sharpening, the scanner's own contrast, or a white point -- and the pixels of
// partial coverage it threw away are the ones the decoder reads. That damage is
// applied before the file is written and leaves no other trace in it. After the
// stroke-width check of the paper-sound project; experiments/NOTES.md 9 carries
// the numbers and what the ruler does and does not catch.
//
// The profile runs from the centre of one cell to the centre of the next, in
// steps of a twentyfourth of a cell, and takes both cuts through every isolated
// dot found. It is read off the UNSHARP buffer: the decoder's own unsharp mask
// is exactly the kind of edge steepening being looked for.
static double rulerprof[NRULER];       // Sum of samples per profile bin
static int    rulerdots;               // Isolated dots measured so far

// Bitmap value at a fractional position, linear between the four pixels around
// it, or -1 outside. The profile is sampled AT fixed fractions of the cell
// rather than binned by where whole pixels happen to fall: a page printed and
// scanned at the same dpi has a grid step of very nearly a whole number of
// pixels, so its dots never walk off their phase, and binning leaves most of a
// profile this fine empty. Interpolating feeds every bin from every dot.
static double Atxy(uchar *buf,int dx,int dy,double x,double y) {
  int i,j;
  double fx,fy;
  uchar *p;
  i=(int)x; j=(int)y;
  if (i<0 || j<0 || i>=dx-1 || j>=dy-1)
    return -1.0;
  fx=x-i; fy=y-j;
  p=buf+j*dx+i;
  return (p[0]+(p[1]-p[0])*fx)*(1.0-fy)+(p[dx]+(p[dx+1]-p[dx])*fx)*fy;
};

// Accumulates the profile of every dot that stands alone in its cross, given
// the block's own grid: peaks and steps are of the data dots, in unsharp.
static void Measuredot(t_procdata *pdata,float xpeak,float xstep,
  float ypeak,float ystep) {
  int i,j,b,dx,dy,margin;
  double cx,cy,u,h,v,one[NRULER];
  uchar *buf;
  buf=pdata->unsharp;
  dx=pdata->bufdx;
  dy=pdata->bufdy;
  // A dot is dark against its four neighbours by this much or it is not alone.
  // A level rather than a threshold on the dot itself: at two pixels per dot
  // blur keeps an isolated dot well away from cmin, and what matters here is
  // only that the cells beside it are bare paper.
  margin=(pdata->cmax-pdata->cmin)/4;
  for (j=1; j<NDOT-1 && rulerdots<NRULERDOT; j++) {
    cy=ypeak+ystep*j;
    for (i=1; i<NDOT-1 && rulerdots<NRULERDOT; i++) {
      cx=xpeak+xstep*i;
      v=Atxy(buf,dx,dy,cx,cy);
      if (v<0.0) continue;
      if (v+margin>Atxy(buf,dx,dy,cx-xstep,cy) ||
        v+margin>Atxy(buf,dx,dy,cx+xstep,cy) ||
        v+margin>Atxy(buf,dx,dy,cx,cy-ystep) ||
        v+margin>Atxy(buf,dx,dy,cx,cy+ystep)) continue;
      // Both cuts through the same dot go into the same profile: one number is
      // what the question needs, and a printer that spreads its two axes
      // differently spreads them both. Gathered for the dot as a whole and kept
      // only if the whole of it fit, so that a dot at the edge of the buffer
      // cannot tilt the profile with the side of it that did.
      for (b=0; b<NRULER; b++) {
        u=2.0*b/(NRULER-1.0)-1.0;
        h=Atxy(buf,dx,dy,cx+u*xstep,cy);
        v=Atxy(buf,dx,dy,cx,cy+u*ystep);
        if (h<0.0 || v<0.0) break;
        one[b]=h+v; };
      if (b<NRULER) continue;
      for (b=0; b<NRULER; b++)
        rulerprof[b]+=one[b];
      rulerdots++;
    };
  };
};

// Dot width at half of its own depth, in percent of the cell, or 0 if the
// profile is not complete enough to be believed.
static float Dotwidth(void) {
  int b,bmin;
  double p[NRULER],ink,paper,half,lo,hi;
  if (rulerdots<NRULERDOT/8)
    return 0.0;                        // Too few dots stood alone
  for (b=0; b<NRULER; b++)
    p[b]=rulerprof[b]/(2*rulerdots);   // Two cuts through each dot
  for (bmin=0,b=1; b<NRULER; b++)
    if (p[b]<p[bmin]) bmin=b;
  for (b=0,paper=p[0]; b<NRULER; b++)
    if (p[b]>paper) paper=p[b];
  ink=p[bmin];
  if (paper-ink<8.0)
    return 0.0;                        // Nothing here is a dot
  half=(ink+paper)/2.0;
  // Walk out of the dot to where the profile crosses its own half depth, and
  // interpolate into the bin outside. Clamped at the window edges, where the
  // crossing is the window's and the answer is thrown away below.
  for (b=bmin; b>0 && p[b]<half; b--);
  if (b==bmin) return 0.0;
  lo=b+(p[b]-half)/(p[b]-p[b+1]);
  for (b=bmin; b<NRULER-1 && p[b]<half; b++);
  if (b==bmin) return 0.0;
  hi=b-(p[b]-half)/(p[b]-p[b-1]);
  if (lo<=0.0 || hi>=NRULER-1)
    return 0.0;                        // Dot wider than the window
  return (float)((hi-lo)*2.0/(NRULER-1)*100.0);
};

// Reports the ruler: what it measured, and what it means if the dot came back
// narrower than the printer drew it.
static void Printdotwidth(void) {
  float width;
  width=Dotwidth();
  if (width<=0.0) {
    if (pb_qualitymap)
      printf("Dot width not measurable on this page\n");
    return; };
  if (pb_qualitymap)
    printf("Dot measures %.0f%% of the cell against %d%% printed "
      "(-s, which the printer rounds to whole pixels); %d dots\n",
      width,pb_dotpercent,rulerdots);
  if (width<0.9*pb_dotpercent)
    printf("Dot measures %.0f%% of the cell but -s printed it %d%%, and ink on "
      "paper only spreads: a dot comes back wider, never narrower. Something "
      "in the scan is squaring off the edges - sharpening, the scanner's own "
      "contrast, or a white point - and the partial pixels it throws away are "
      "the ones read here. Rescan with every adjustment off, or pass the -s "
      "the page was printed with\n",width,pb_dotpercent);
};

// Given grid of recognized dots, extracts saved information. Returns number of
// corrected erorrs (0..16) on success and 17 if information is not readable.
static int Recognizebits(t_data *result,uchar grid[NDOT][NDOT],
  t_procdata *pdata,int erasures) {
  int i,j,k,q,r,factor,lcorr,c,cmin,cmax,limit;
  int grid1[NDOT][NDOT],answer,bestanswer;
  int m,n,e,best,margin[sizeof(t_data)],eras[ECC_SIZE];
  uint32_t bitrow[NDOT];
  t_data raw;
  static int lastgood;
  ushort crc;
  t_data uncorrected,bestresult;
  cmin=pdata->cmin;
  cmax=pdata->cmax;
  bestanswer=17;
  // If orientation is not yet known, try all possible orientations + mirroring.
  for (r=0; r<8; r++) {
    if (pdata->orientation>=0 && r!=pdata->orientation) continue;
    // Try 3 different point overlapping factors, combined with 3 different
    // thresholds. Usually all cells are alike, so I remember the last known
    // good combination and start with it.
    for (k=0; k<9; k++) {
      q=(k+lastgood)%9;
      switch (q) {
        case 0: factor=1000; lcorr=0; break;
        case 1: factor=32; lcorr=0; break;
        case 2: factor=16; lcorr=0; break;
        case 3: factor=1000; lcorr=(cmin-cmax)/16; break;
        case 4: factor=32; lcorr=(cmin-cmax)/16; break;
        case 5: factor=16; lcorr=(cmin-cmax)/16; break;
        case 6: factor=1000; lcorr=(cmax-cmin)/16; break;
        case 7: factor=32; lcorr=(cmax-cmin)/16; break;
        case 8: factor=16; lcorr=(cmax-cmin)/16; break;
        default: factor=1000; lcorr=0; lastgood=0; break; };
      // Correct grid for overlapping dots and calculate limit between black
      // and white. I take into account only adjacent dots; the influence of
      // diagonals is significantly lower.
      limit=0;
      for (j=0; j<NDOT; j++) {
        for (i=0; i<NDOT; i++) {
          c=grid[j][i]*factor;
          if (i>0) c-=grid[j][i-1]; else c-=cmax;
          if (i<31) c-=grid[j][i+1]; else c-=cmax;
          if (j>0) c-=grid[j-1][i]; else c-=cmax;
          if (j<31) c-=grid[j+1][i]; else c-=cmax;
          grid1[j][i]=c;
          limit+=c;
        };
      };
      limit=limit/1024+lcorr*factor;
      // Extract data according to the selected orientation. Distance of the
      // dot from the threshold is the confidence; the weakest dot decides the
      // confidence of the byte it belongs to.
      // t_data is packed, so the dot rows are built in an aligned local array
      // and copied over the result rather than written through a uint32_t *.
      memset(bitrow,0,sizeof(bitrow));
      for (n=0; n<(int)sizeof(t_data); n++) margin[n]=MAXMARGIN;
      for (j=0; j<NDOT; j++) {
        for (i=0; i<NDOT; i++) {
          switch (r) {
            case 0: c=grid1[j][i]; break;
            case 1: c=grid1[i][NDOT-1-j]; break;
            case 2: c=grid1[NDOT-1-j][NDOT-1-i]; break;
            case 3: c=grid1[NDOT-1-i][j]; break;
            case 4: c=grid1[i][j]; break;
            case 5: c=grid1[j][NDOT-1-i]; break;
            case 6: c=grid1[NDOT-1-i][NDOT-1-j]; break;
            case 7: c=grid1[NDOT-1-j][i]; break;
          };
          if (c<limit) {
            bitrow[j]|=1u<<i;
          };
          m=c-limit; if (m<0) m=-m;
          n=j*sizeof(uint32_t)+i/8;    // Byte of the block holding this dot
          if (m<margin[n]) margin[n]=m;
        };
      };
      // XOR with grid that corrects mean brightness.
      for (j=0; j<NDOT; j++) {
        bitrow[j]^=(j & 1?0xAAAAAAAA:0x55555555); };
      memcpy(result,bitrow,sizeof(bitrow));
      // Apply ECC to restore invalid data.
      if (pdata->mode & M_BEST)
        memcpy(&uncorrected,result,sizeof(t_data));
      else
        memcpy(&pdata->uncorrected,result,sizeof(t_data));
      raw=*result;
      answer=Decode8((uchar *)result,NULL,0,127);
      if (erasures && (answer<0 ||
        (ushort)(Crc16((uchar *)result,NDATA+4)^0x55AA)!=result->crc)) {
        // Decoding failed. Reed-Solomon corrects 32 bytes of known position
        // against only 16 of unknown position, so retry with the 32 least
        // reliable bytes declared erasures. Their contents are ignored, and
        // the CRC below still decides whether the result is accepted.
        for (e=0; e<ECC_SIZE; e++) {
          best=0;
          for (n=1; n<(int)sizeof(t_data); n++) {
            if (margin[n]<margin[best]) best=n; };
          eras[e]=best+127;            // Decode8 counts in codeword positions
          margin[best]=MAXMARGIN; };
        *result=raw;
        answer=Decode8((uchar *)result,eras,ECC_SIZE,127);
        if (answer>16) answer=16;      // Caller reads 17 and above as failure
      };
      if (answer<0) answer=17;
      // Verify data for correctness by calculating CRC.
      if (answer<=16) {
        crc=(ushort)(Crc16((uchar *)result,NDATA+4)^0x55AA);
        if (crc==result->crc) {
          // Data recognized correctly, save orientation of actually processed
          // page and factoring.
          pdata->orientation=r;
          // Report success.
          if ((pdata->mode & M_BEST)==0) {
            lastgood=q;
            return answer; }
          else if (answer<bestanswer) {
            bestanswer=answer;
            bestresult=*result;
            memcpy(&pdata->uncorrected,&uncorrected,sizeof(t_data));
          };
        };
      };
    };
  };
  if (pdata->mode & M_BEST)
    *result=bestresult;
  return bestanswer;
};

// Determines rough grid position.
static void Getgridposition(t_procdata *pdata) {
  int i,j,nx,ny,stepx,stepy,sizex,sizey;
  int c,cmin,cmax,distrx[256],distry[256],limit;
  uchar *data,*pd;
  // Get frequently used variables.
  sizex=pdata->sizex;
  sizey=pdata->sizey;
  data=pdata->data;
  // Check overall bitmap size.
  if (sizex<=3*NDOT || sizey<=3*NDOT) {
    Reporterror("Bitmap is too small to process");
    pdata->step=0; return; };
  // Select horizontal and vertical lines (at most 256 in each direction) to
  // check for grid location.
  stepx=sizex/256+1; nx=(sizex-2)/stepx; if (nx>256) nx=256;
  stepy=sizey/256+1; ny=(sizey-2)/stepy; if (ny>256) ny=256;
  // The main problem in determining the grid location are the black and/or
  // white borders around the grid. To distinguish between borders with more or
  // less constant intensity and quickly changing raster, I take into account
  // only the fast intensity changes over the short distance (2 pixels).
  // Caveat: this approach may fail for artificially created bitmaps.
  memset(distrx,0,nx*sizeof(int));
  memset(distry,0,ny*sizeof(int));
  for (j=0; j<ny; j++) {
    pd=data+j*stepy*sizex;
    for (i=0; i<nx; i++,pd+=stepx) {
      c=pd[0];         cmin=c;           cmax=c;
      c=pd[2];         cmin=min(cmin,c); cmax=max(cmax,c);
      c=pd[sizex+1];   cmin=min(cmin,c); cmax=max(cmax,c);
      c=pd[2*sizex];   cmin=min(cmin,c); cmax=max(cmax,c);
      c=pd[2*sizex+2]; cmin=min(cmin,c); cmax=max(cmax,c);
      distrx[i]+=cmax-cmin;
      distry[j]+=cmax-cmin;
    };
  };
  // Get rough bitmap limits in horizontal direction (at the level 50% of
  // maximum).
  limit=0;
  for (i=0; i<nx; i++) {
    if (distrx[i]>limit) limit=distrx[i]; };
  limit/=2;
  for (i=0; i<nx-1; i++) {
    if (distrx[i]>=limit) break; };
  pdata->gridxmin=i*stepx;
  for (i=nx-1; i>0; i--) {
    if (distrx[i]>=limit) break; };
  pdata->gridxmax=i*stepx;
  // Get rough bitmap limits in vertical direction.
  limit=0;
  for (j=0; j<ny; j++) {
    if (distry[j]>limit) limit=distry[j]; };
  limit/=2;
  for (j=0; j<ny-1; j++) {
    if (distry[j]>=limit) break; };
  pdata->gridymin=j*stepy;
  for (j=ny-1; j>0; j--) {
    if (distry[j]>=limit) break; };
  pdata->gridymax=j*stepy;
  // Step finished.
  pdata->step++;
};

// Ink and paper levels over one area of the bitmap: the level not reached by 3%
// of its pixels and the level exceeded by 3% of them. Rows are taken every
// `step`, so a whole sheet costs no more to measure than a window of it.
static void Getlevels(t_procdata *pdata,int x0,int x1,int y0,int y1,int step,
  int *cmin,int *cmax) {
  int i,j,n,sum,limit,distr[256];
  uchar *pd;
  memset(distr,0,sizeof(distr));
  for (j=y0,n=0,sum=0; j<y1; j+=step) {
    pd=pdata->data+j*pdata->sizex+x0;
    for (i=x0; i<x1; i++,pd++) {
      distr[*pd]++; n++;
    };
  };
  if (n<=0) {
    *cmin=*cmax=0;
    return; };
  limit=n/33;                          // 3% of the total number of pixels
  for (i=0,sum=0; i<255; i++) {
    sum+=distr[i];
    if (sum>=limit) break; };
  *cmin=i;
  for (i=255,sum=0; i>0; i--) {
    sum+=distr[i];
    if (sum>=limit) break; };
  *cmax=i;
};

// Selects search range, determines grid intensity and estimates sharpness.
static void Getgridintensity(t_procdata *pdata) {
  int i,j,sizex,sizey,centerx,centery,dx,dy,nd,x0,x1,y0,y1,step;
  int searchx0,searchy0,searchx1,searchy1;
  int distrd[256],cmin,cmax,rmin,rmax,limit,sum,contrast;
  uchar *data,*pd;
  // Get frequently used variables.
  sizex=pdata->sizex;
  sizey=pdata->sizey;
  data=pdata->data;
  // Select X and Y ranges to search for the grid. As I use affine transforms
  // instead of more CPU-intensive rotations, these ranges are determined for
  // Y=0 (searchx0,searchx1) and for X=0 (searchy0,searchy1).
  centerx=(pdata->gridxmin+pdata->gridxmax)/2;
  centery=(pdata->gridymin+pdata->gridymax)/2;
  searchx0=centerx-NHYST/2; if (searchx0<0) searchx0=0;
  searchx1=searchx0+NHYST; if (searchx1>sizex) searchx1=sizex;
  searchy0=centery-NHYST/2; if (searchy0<0) searchy0=0;
  searchy1=searchy0+NHYST; if (searchy1>sizey) searchy1=sizey;
  dx=searchx1-searchx0;
  dy=searchy1-searchy0;
  // Sharpness: the 5% level of the difference between adjacent pixels, over the
  // window. It stays on the window because the correction applied to it further
  // down was tuned against exactly this sample of the page (NOTES.md 11.5).
  memset(distrd,0,sizeof(distrd));
  for (j=0,nd=0; j<dy-1; j++) {
    pd=data+(searchy0+j)*sizex+searchx0;
    for (i=0; i<dx-1; i++,pd++,nd++) {
      distrd[abs(pd[1]-pd[0])]++;
      distrd[abs(pd[sizex]-pd[0])]++;
    };
  };
  // Levels of ink and paper, from the window as 1.20 took them. As a minimum I
  // take the level not reached by 3% of all pixels, as a maximum - the level
  // exceeded by 3% of them.
  Getlevels(pdata,searchx0,searchx1,searchy0,searchy1,1,&cmin,&cmax);
  // But a sheet can carry a sticker, a coffee ring or a white crease exactly
  // where that window falls. 1.20 then measures cmin==cmax inside it and throws
  // the whole page away with "No image" although the other 95% of it is clean.
  // When the window holds far less contrast than the sheet does, it is not a
  // sample of the sheet, and the sheet is measured instead. Compared against
  // the sheet rather than against a constant, so a healthy page - where the two
  // agree - keeps the levels 1.20 gave it, and with them the sharpness
  // correction that was fitted to them.
  //
  // ponytail: switched, not blended. The fold-shadow page of NOTES.md 11 gains
  // 30 blocks from the wider levels although its own window is good, and
  // taking them always costs blocks on a blurred scan; a blend would need the
  // sharpness correction refitted, which is a bigger job than this one.
  x0=pdata->gridxmin; x1=pdata->gridxmax;
  y0=pdata->gridymin; y1=pdata->gridymax;
  step=(y1-y0)/NHYST+1;
  if (x1-x0>=dx && y1-y0>=dy) {
    Getlevels(pdata,x0,x1,y0,y1,step,&rmin,&rmax);
    if (rmax-rmin>2*(cmax-cmin)) {
      cmin=rmin; cmax=rmax; }; };
  if (cmax-cmin<1) {
    Reporterror("No image");
    pdata->step=0;
    return; };
  // Estimate image sharpness. The factor is rather empirical. Later, when
  // dot size is known, this value will be corrected.
  limit=nd/10;                         // 5% (each point is counted twice)
  for (contrast=255,sum=0; contrast>1; contrast--) {
    sum+=distrd[contrast];
    if (sum>=limit) break; };
  pdata->sharpfactor=(cmax-cmin)/(2.0*contrast)-1.0;
  // Save results.
  pdata->searchx0=searchx0;
  pdata->searchx1=searchx1;
  pdata->searchy0=searchy0;
  pdata->searchy1=searchy1;
  pdata->cmin=cmin;
  pdata->cmax=cmax;
  // Step finished.
  pdata->step++;
};

// Find angle and step of vertical grid lines.
static void Getxangle(t_procdata *pdata) {
  int i,j,a,x,y,x0,y0,dx,dy,sizex;
  int h[NHYST],nh[NHYST],ystep;
  uchar *data,*pd;
  float weight,xpeak,xstep;
  float maxweight,bestxpeak,bestxangle,bestxstep;
  // Get frequently used variables.
  sizex=pdata->sizex;
  data=pdata->data;
  x0=pdata->searchx0;
  dx=pdata->searchx1-x0;
  // The profile is 1024 columns wide because h[] is, but nothing says the rows
  // feeding it must come from the same 1024-pixel square. They are taken from
  // the whole height of the raster, sheared by the angle under test: index
  // arithmetic, no pixel resampled, and the same 256 sampled lines as before,
  // only spread down the sheet instead of a patch in the middle of it. A wrong
  // angle then smears the comb over the whole page rather than over a thousand
  // rows, and a patch of blank paper - a sticker, a crease - dilutes the comb
  // instead of being all there is of it. The same profile the paper-sound
  // project sums along its own lean; experiments/NOTES.md 11.
  y0=pdata->gridymin;
  dy=pdata->gridymax-y0;
  if (dy<NHYST) { y0=pdata->searchy0; dy=pdata->searchy1-y0; };
  // Calculate vertical step. 256 lines are sufficient. Warning: danger of
  // moire, especially on synthetic bitmaps!
  ystep=dy/256; if (ystep<1) ystep=1;
  maxweight=0.0;
  xstep=bestxstep=0.0;
  // Determine rough angle, step and base for the vertical grid lines. Due to
  // the oversimplified conversion, cases a=+-1 are almost identical to a=0.
  // Maximal allowed angle is approx. +/-5 degrees (1/10 radian).
  for (a=-(NHYST/20)*2; a<=(NHYST/20)*2; a+=2) {
    // Clear histogramm.
    memset(h,0,dx*sizeof(int));
    memset(nh,0,dx*sizeof(int));
    // Gather histogramm.
    for (j=0; j<dy; j+=ystep) {
      y=y0+j;
      x=x0+(y0+j)*a/NHYST;             // Affine transformation
      pd=data+y*sizex+x;
      for (i=0; i<dx; i++,x++,pd++) {
        if (x<0) continue;
        if (x>=sizex) break;
        h[i]+=*pd; nh[i]++;
      };
    };
    // Normalize histogramm.
    for (i=0; i<dx; i++) {
      if (nh[i]>0) h[i]/=nh[i]; };
    // Find peaks. On small synthetic bitmaps (height less than NHYST/2
    // pixels) weights for a=0 and +/-2 are the same and routine would select
    // -2 as a best angle. To solve this problem, I add small correction that
    // preferes zero angle.
    weight=Findpeaks(h,dx,&xpeak,&xstep)+1.0/(abs(a)+10.0);
    if (weight>maxweight) {
      bestxpeak=xpeak+x0;
      bestxangle=(float)a/NHYST;
      bestxstep=xstep;
      maxweight=weight;
    };
  };
  // Analyse and save results.
  if (maxweight==0.0 || bestxstep<NDOT) {
    Reporterror("No grid");
    pdata->step=0;
    return; };
  pdata->xpeak=bestxpeak;
  pdata->xstep=bestxstep;
  pdata->xangle=bestxangle;
  // Step finished.
  pdata->step++;
};

// Find angle and step of horizontal grid lines. Very similar to Getxangle().
static void Getyangle(t_procdata *pdata) {
  int i,j,a,x,y,x0,y0,dx,dy,sizex,sizey;
  int h[NHYST],nh[NHYST],xstep;
  uchar *data,*pd;
  float weight,ypeak,ystep;
  float maxweight,bestypeak,bestyangle,bestystep;
  // Get frequently used variables.
  sizex=pdata->sizex;
  sizey=pdata->sizey;
  data=pdata->data;
  y0=pdata->searchy0;
  dy=pdata->searchy1-y0;
  x0=pdata->gridxmin;                  // Whole width of the raster, as above
  dx=pdata->gridxmax-x0;
  if (dx<NHYST) { x0=pdata->searchx0; dx=pdata->searchx1-x0; };
  // Calculate vertical step. 256 lines are sufficient. Warning: danger of
  // moire, especially on synthetic bitmaps!
  xstep=dx/256; if (xstep<1) xstep=1;
  maxweight=0.0;
  ystep=bestystep=0.0;
  // Determine rough angle, step and base for the vertical grid lines. I do not
  // take into account the changes of angle caused by the X transformation.
  for (a=-(NHYST/20)*2; a<=(NHYST/20)*2; a+=2) {
    // Clear histogramm.
    memset(h,0,dy*sizeof(int));
    memset(nh,0,dy*sizeof(int));
    for (i=0; i<dx; i+=xstep) {
      x=x0+i;
      y=y0+(x0+i)*a/NHYST;             // Affine transformation
      pd=data+y*sizex+x;
      for (j=0; j<dy; j++,y++,pd+=sizex) {
        if (y<0) continue;
        if (y>=sizey) break;
        h[j]+=*pd; nh[j]++;
      };
    };
    // Normalize histogramm.
    for (j=0; j<dy; j++) {
      if (nh[j]>0) h[j]/=nh[j]; };
    // Find peaks.
    weight=Findpeaks(h,dy,&ypeak,&ystep)+1.0/(abs(a)+10.0);
    if (weight>maxweight) {
      bestypeak=ypeak+y0;
      bestyangle=(float)a/NHYST;
      bestystep=ystep;
      maxweight=weight;
    };
  };
  // Analyse and save results.
  if (maxweight==0.0 || bestystep<NDOT ||
    bestystep<pdata->xstep*0.40 ||
    bestystep>pdata->xstep*2.50
  ) {
    Reporterror("No grid");
    pdata->step=0;
    return; };
  pdata->ypeak=bestypeak;
  pdata->ystep=bestystep;
  pdata->yangle=bestyangle;
  // Step finished.
  pdata->step++;
};

// Prepare data and allocate memory for data decoding.
static void Preparefordecoding(t_procdata *pdata) {
  int sizex,sizey,dx,dy;
  float xstep,ystep,border,sharpfactor,shift,maxxshift,maxyshift,dotsize;
  // Get frequently used variables.
  sizex=pdata->sizex;
  sizey=pdata->sizey;
  xstep=pdata->xstep;
  ystep=pdata->ystep;
  border=pdata->blockborder;
  sharpfactor=pdata->sharpfactor;
  // Empirical formula: the larger the angle, the more imprecise is the
  // expected position of the block.
  if (border<=0.0) {
    border=max(fabs(pdata->xangle),fabs(pdata->yangle))*5.0+0.4;
    pdata->blockborder=border; };
  // Correct sharpness for known dot size. This correction is empirical.
  dotsize=max(xstep,ystep)/(NDOT+3.0);
  sharpfactor+=1.3/dotsize-0.1;
  if (sharpfactor<0.0) sharpfactor=0.0;
  else if (sharpfactor>2.0) sharpfactor=2.0;
  pdata->sharpfactor=sharpfactor;
  // Calculate start coordinates and number of block that fit onto the page
  // in X direction.
  maxxshift=fabs(pdata->xangle*sizey);
  if (pdata->xangle<0.0)
    shift=0.0;
  else
    shift=maxxshift;
  while (pdata->xpeak-xstep>-shift-xstep*border)
    pdata->xpeak-=xstep;
  pdata->nposx=(int)((sizex+maxxshift)/xstep);
  // The same in Y direction.
  maxyshift=fabs(pdata->yangle*sizex);
  if (pdata->yangle<0.0)
    shift=0.0;
  else
    shift=maxyshift;
  while (pdata->ypeak-ystep>-shift-ystep*border)
    pdata->ypeak-=ystep;
  pdata->nposy=(int)((sizey+maxyshift)/ystep);
  // Start new quality map. Note that this call doesn't force map to be
  // displayed.
  //Initqualitymap(pdata->nposx,pdata->nposy);
  // Allocate block buffers.
  dx=xstep*(2.0*border+1.0)+1.0;
  dy=ystep*(2.0*border+1.0)+1.0;
  pdata->buf1=(uchar *)malloc(dx*dy);
  pdata->buf2=(uchar *)malloc(dx*dy);
  pdata->bufx=(int *)malloc(dx*sizeof(int));
  pdata->bufy=(int *)malloc(dy*sizeof(int));
  pdata->blocklist=(t_block *)
    malloc(pdata->nposx*pdata->nposy*sizeof(t_block));
  // Per-position decoding result and measured block position. The results
  // drive the second decoding pass and the quality map, the positions let
  // each block be searched for relative to its decoded neighbours.
  pdata->qmap=(signed char *)malloc(pdata->nposx*pdata->nposy);
  pdata->orgx=(float *)malloc(pdata->nposx*pdata->nposy*sizeof(float));
  pdata->orgy=(float *)malloc(pdata->nposx*pdata->nposy*sizeof(float));
  // Check that we have enough memory.
  if (pdata->buf1==NULL || pdata->buf2==NULL ||
    pdata->bufx==NULL || pdata->bufy==NULL || pdata->blocklist==NULL ||
    pdata->qmap==NULL || pdata->orgx==NULL || pdata->orgy==NULL
  ) {
    if (pdata->buf1!=NULL) free(pdata->buf1);
    if (pdata->buf2!=NULL) free(pdata->buf2);
    if (pdata->bufx!=NULL) free(pdata->bufx);
    if (pdata->bufy!=NULL) free(pdata->bufy);
    if (pdata->blocklist!=NULL) free(pdata->blocklist);
    if (pdata->qmap!=NULL) free(pdata->qmap);
    if (pdata->orgx!=NULL) free(pdata->orgx);
    if (pdata->orgy!=NULL) free(pdata->orgy);
    pdata->buf1=pdata->buf2=NULL; pdata->bufx=pdata->bufy=NULL;
    pdata->blocklist=NULL; pdata->qmap=NULL; pdata->orgx=pdata->orgy=NULL;
    Reporterror("Low memory");
    pdata->step=0;
    return; };
  // Determine maximal size of the dot on the bitmap.
  if (xstep<2*(NDOT+3) || ystep<2*(NDOT+3))
    pdata->maxdotsize=1;
  else if (xstep<3*(NDOT+3) || ystep<3*(NDOT+3))
    pdata->maxdotsize=2;
  else if (xstep<4*(NDOT+3) || ystep<4*(NDOT+3))
    pdata->maxdotsize=3;
  else
    pdata->maxdotsize=4;
  // Prepare superblock.
  memset(&pdata->superblock,0,sizeof(t_superblock));
  // Initialize remaining items.
  pdata->bufdx=dx;
  pdata->bufdy=dy;
  pdata->orientation=-1;               // As yet, unknown page orientation
  pdata->ngood=0;
  pdata->nbad=0;
  pdata->nsuper=0;
  pdata->nrestored=0;
  pdata->posx=pdata->posy=0;           // First block to scan
  pdata->pass=0;                       // First decoding pass
  memset(pdata->qmap,-2,pdata->nposx*pdata->nposy);
  // Step finished.
  pdata->step++;
};

// Predicts the search window origin for block (posx,posy) from the measured
// position of a neighbour that has already been decoded. Only neighbours whose
// CRC checked out are trusted: a block that was located but stayed unreadable
// is often unreadable precisely because it was located in the wrong place.
// Returns the number of such neighbours, so that the caller can tell a block
// surrounded by good data from one sitting off the edge of the raster, and 0
// when there is none and the global grid is all there is to go on.
static int Predictorigin(t_procdata *pdata,int posx,int posy,
  float *x0,float *y0
) {
  static const int dx[4]={-1,0,1,0},dy[4]={0,-1,0,1};
  int k,nx,ny,i,n=0;
  if (pdata->qmap==NULL)
    return 0;
  for (k=0; k<4; k++) {
    nx=posx+dx[k]; ny=posy+dy[k];
    if (nx<0 || nx>=pdata->nposx || ny<0 || ny>=pdata->nposy)
      continue;
    i=ny*pdata->nposx+nx;
    if (pdata->qmap[i]<0 || pdata->qmap[i]>=17)
      continue;
    // Carry the measured block boundary over whole grid steps. Only the phase
    // comes from the neighbour, the step stays global: a step measured on a
    // single block is too noisy to integrate over a page.
    if (n==0) {
      *x0=pdata->orgx[i]+pdata->xstep*(posx-nx)-pdata->xstep*pdata->blockborder;
      *y0=pdata->orgy[i]-pdata->ystep*(posy-ny)-pdata->ystep*pdata->blockborder; };
    n++;
  };
  return n;
};

// The most important routine, converts scanned blocks into data. Used both by
// data decoder and by block display. Returns -1 if block cannot be located,
// 0 to 16 if block is correctly decoded and 17 if block is unrecoverable.
int Decodeblock(t_procdata *pdata,int posx,int posy,t_data *result) {
  int i,j,x,y,x0,y0,dx,dy,sizex,sizey,*bufx,*bufy;
  int c,cmin,cmax,dotsize,shift,shiftmax,sum,answer,bestanswer;
  float xangle,yangle,xbmp,ybmp,xres,yres,sharpfactor;
  float xpeak,xstep,ypeak,ystep,halfdot,predx,predy;
  float sy,syy,disp,dispmin,dispmax;
  uchar *psrc,*pdest,*data,g[9][NDOT][NDOT],grid[NDOT][NDOT];
  static const int shiftorder[9]={4,1,3,5,7,0,2,6,8};
  t_data uncorrected,bestresult;
  // Get frequently used variables.
  sizex=pdata->sizex;
  sizey=pdata->sizey;
  xangle=pdata->xangle;
  yangle=pdata->yangle;
  data=pdata->data;
  cmin=pdata->cmin;
  cmax=pdata->cmax;
  sharpfactor=pdata->sharpfactor;
  bufx=pdata->bufx;
  bufy=pdata->bufy;
  // Get block coordinates in the bitmap. Note that bitmap in memory is placed
  // upside down. The global grid stays in charge: averaged over the whole page
  // it is less noisy than a phase carried over from a single neighbour. Only
  // when the caller has already failed with it is the neighbour asked instead.
  if (!pdata->usepred || !Predictorigin(pdata,posx,posy,&predx,&predy)) {
    predx=pdata->xpeak+pdata->xstep*(posx-pdata->blockborder);
    predy=pdata->ypeak+pdata->ystep*(pdata->nposy-posy-1-pdata->blockborder); };
  x0=predx;
  y0=predy;
  dx=pdata->bufdx;
  dy=pdata->bufdy;
  // Rotate selected block to 'unsharp' buffer using bilinear interpolation.
  // Fast discrete shifts are also thinkable but deliver significantly higher
  // error rate.
  if (sharpfactor>0.0)
    pdest=pdata->buf2;                 // Sharping necessary
  else
    pdest=pdata->buf1;
  pdata->unsharp=pdest;
  for (j=0; j<dy; j++) {
    xbmp=x0+(y0+j)*xangle;
    if (xbmp>=0.0) x=xbmp;             // Integer and fractional parts
    else x=xbmp-1.0;
    xres=xbmp-x;
    for (i=0; i<dx; i++,pdest++,x++) {
      ybmp=y0+j+(x0+i)*yangle;
      if (ybmp>0.0) y=ybmp;
      else y=ybmp-1.0;
      yres=ybmp-y;
      if (x<0 || x>=sizex-1 || y<0 || y>=sizey-1)
        *pdest=(uchar)cmax;            // Fill areas outside the page white
      else {
        psrc=data+y*sizex+x;
        *pdest=(psrc[0]+(psrc[1]-psrc[0])*xres)*(1.0-yres)+
        (psrc[sizex]+(psrc[sizex+1]-psrc[sizex])*xres)*yres;
      };
    };
  };
  // Sharpen rotated block, if necessary.
  if (sharpfactor>0.0) {
    psrc=pdata->buf2;
    pdest=pdata->buf1;
    for (j=0; j<dy; j++) {
      for (i=0; i<dx; i++,psrc++,pdest++) {
        if (i==0 || i==dx-1 || j==0 || j==dy-1)
          *pdest=*psrc;
        else {
          *pdest=(uchar)max(cmin,min((int)(psrc[0]*(1.0+4.0*sharpfactor)-
          (psrc[-dx]+psrc[-1]+psrc[1]+psrc[dx])*sharpfactor),cmax));
        };
      };
    };
  };
  pdata->sharp=pdata->buf1;
  // Find grid lines for the whole block. This works perfectly for laser
  // printers. For bidirectional jet printers, splitting left and right
  // borders into several pieces may give better results.
  memset(bufx,0,dx*sizeof(int));
  memset(bufy,0,dy*sizeof(int));
  psrc=pdata->buf1;
  for (j=0; j<dy; j++) {
    for (i=0; i<dx; i++,psrc++) {
      bufx[i]+=*psrc;
      bufy[j]+=*psrc;
    };
  };
  if (Findpeaks(bufx,dx,&xpeak,&xstep)<=0.0)
    return -1;                         // No X grid
  if (fabs(xstep-pdata->xstep)>pdata->xstep/16.0)
    return -1;                         // Invalid grid step
  if (Findpeaks(bufy,dy,&ypeak,&ystep)<=0.0)
    return -1;                         // No Y grid
  if (fabs(ystep-pdata->ystep)>pdata->ystep/16.0)
    return -1;                         // Invalid grid step
  // Save block position for displaying purposes.
  pdata->blockxpeak=xpeak;
  pdata->blockxstep=xstep;
  pdata->blockypeak=ypeak;
  pdata->blockystep=ystep;
  // Remember where this block really sits, so that its neighbours can be
  // searched for relative to it instead of relative to the page centre.
  if (pdata->orgx!=NULL &&
    posx>=0 && posx<pdata->nposx && posy>=0 && posy<pdata->nposy
  ) {
    pdata->orgx[posy*pdata->nposx+posx]=x0+xpeak;
    pdata->orgy[posy*pdata->nposx+posx]=y0+ypeak; };
  // Calculate dot step and correct peaks so that they point to first dot.
  xstep=xstep/(NDOT+3.0);
  xpeak+=2.0*xstep;
  ystep=ystep/(NDOT+3.0);
  ypeak+=2.0*ystep;
  // Read the ruler off the first blocks that locate, while the buffer holds
  // this block's own unsharpened pixels and its own grid.
  if (rulerdots<NRULERDOT)
    Measuredot(pdata,xpeak,xstep,ypeak,ystep);
  // In search-for-the-best-quality mode, I look for the best possible
  // decoding. Helps to estimate the overall quality of the picture.
  bestanswer=17;
  // Try different dot sizes, starting from 1x1 pixel. If scanner resolution
  // is sufficient, 2x2 dot usually gives best results.
  for (dotsize=1; dotsize<=pdata->maxdotsize; dotsize++) {
    halfdot=dotsize/2.0-1.0;
    for (j=0; j<NDOT; j++) {
      y=ypeak+ystep*j-halfdot;
      for (i=0; i<NDOT; i++) {
        x=xpeak+xstep*i-halfdot;
        // For each dot size I try +/- 1 pixel shifts in all possible
        // directions.
        for (shift=0; shift<9; shift++) {
          switch (shift) {
            case 0: psrc=pdata->buf1+(y-1)*dx+(x-1); break;
            case 1: psrc=pdata->buf1+(y-1)*dx+(x+0); break;
            case 2: psrc=pdata->buf1+(y-1)*dx+(x+1); break;
            case 3: psrc=pdata->buf1+(y+0)*dx+(x-1); break;
            case 4: psrc=pdata->buf1+(y+0)*dx+(x+0); break;
            case 5: psrc=pdata->buf1+(y+0)*dx+(x+1); break;
            case 6: psrc=pdata->buf1+(y+1)*dx+(x-1); break;
            case 7: psrc=pdata->buf1+(y+1)*dx+(x+0); break;
            case 8: psrc=pdata->buf1+(y+1)*dx+(x+1); break; };
          switch (dotsize) {
            case 4:                    // Rounded 4x4 dot (rarely works)
              sum=(psrc[1]+psrc[2]+psrc[dx]+psrc[dx+1]+psrc[dx+2]+psrc[dx+3]+
                psrc[2*dx]+psrc[2*dx+1]+psrc[2*dx+2]+psrc[2*dx+3]+
                psrc[3*dx+1]+psrc[3*dx+2])/12;
              break;
            case 3:                    // 3x3 pixel
              sum=(psrc[0]+psrc[1]+psrc[2]+psrc[dx]+psrc[dx+1]+psrc[dx+2]+
                psrc[2*dx]+psrc[2*dx+1]+psrc[2*dx+2])/9;
              break;
            case 2:                    // 2x2 pixel (usually the best)
              sum=(psrc[0]+psrc[1]+psrc[dx]+psrc[dx+1])/4;
              break;
            default:                   // 1x1 pixel dot (or internal error)
              sum=psrc[0];
            break; };
          g[shift][j][i]=(uchar)sum;
        };
      };
    };
    // We have gathered 9 grids with 1-pixel shifts. Non-shifted grid is the
    // most probable good candidate, try it first, then the four along the axes
    // and last the diagonals. 1.20 tried only the non-shifted one and kept the
    // rest for the recombined grid below, but each of them is also the whole
    // block read one pixel over, which is what a page with few pixels per dot
    // needs when the grid's phase lands between two of them: at 2 px per dot
    // and the PSF measured on paper scaled 1.5x, one page read 6 blocks of 445
    // this way and all 445 that way (experiments/NOTES.md 8).
    for (i=0; i<9; i++) {
      // Erasures only on the unshifted grid, where 1.20 had them. Declaring
      // the 32 least reliable bytes erasures spends ALL the Reed-Solomon
      // redundancy: whatever the other 95 bytes say is then a valid codeword,
      // and only the 16-bit CRC stands between a fabricated block and the
      // file. One in 65536 attempts gets through, so the number of attempts
      // is the exposure - and trying the shifted grids that way as well
      // multiplied it ninefold. On the three sheets of NOTES.md 12 that was
      // enough to accept one wrong block in 910.
      answer=Recognizebits(result,g[shiftorder[i]],pdata,shiftorder[i]==4);
      // Don't stop if in search-for-the-best-quality mode.
      if ((pdata->mode & M_BEST)!=0 && answer<bestanswer) {
        bestanswer=answer;
        bestresult=*result;
        uncorrected=pdata->uncorrected;
        if (answer!=0) answer=17; };
      if (answer!=17) break; };
    // If data recognition fails, combine grid from subblocks SUBDX*SUBDY dots
    // with maximal dispersion. This compensates for small distortions, even
    // nonlinear, and partially for bidirectional print.
    if (answer==17) {
      for (j=0; j<NDOT; j+=SUBDY) {
        for (i=0; i<NDOT; i+=SUBDX) {
          dispmin=1.0e99; dispmax=-1.0e99;
          for (shift=0; shift<9; shift++) {
            sy=0.0; syy=0.0;
            for (y=j; y<j+SUBDY; y++) {
              for (x=i; x<i+SUBDX; x++) {
                c=g[shift][y][x];
                sy+=c; syy+=c*c;
              };
            };
            // Dispersion in the mathematical sense is a bit different beast
            // (includes Division, Square Roots and Other Incomprehensible
            // Things), but we are interested only in the shift corresponding
            // to the maximum.
            disp=syy*SUBDX*SUBDY-sy*sy;
            if (disp<dispmin) dispmin=disp;
            if (disp>dispmax) {
              dispmax=disp;
              shiftmax=shift;
            };
          };
          // If difference between minimal and maximal dispersion is low (the
          // case of mostly black/mostly white dots), I set shift to zero. 20%
          // for disp equals to roughly 10% in strict mathematical sense.
          if (dispmax-dispmin<dispmax/5.0)
            shiftmax=4;
          // Copy subblock with maximal dispersion to main grid.
          for (y=j; y<j+SUBDY; y++) {
            for (x=i; x<i+SUBDX; x++) {
              grid[y][x]=g[shiftmax][y][x];
            };
          };
        };
      };
      // Try to recognize data in the combined grid.
      answer=Recognizebits(result,grid,pdata,1);
      // Again, don't stop if in search-for-the-best-quality mode.
      if ((pdata->mode & M_BEST)!=0 && answer<bestanswer) {
        bestanswer=answer;
        bestresult=*result;
        uncorrected=pdata->uncorrected;
        if (answer!=0) answer=17;
      };
    };
    // If data is restored, we don't need different dot size.
    if (answer<17) break;
  };
  if (pdata->mode & M_BEST) {
    answer=bestanswer;
    *result=bestresult;
    pdata->uncorrected=uncorrected; };
  return answer;
};

static void Decodenextblock(t_procdata *pdata) {
  int answer,second,ngroup,percent,index,old;
  float predx,predy;
  char s[TEXTLEN];
  t_data result,retry;

  // Display percent of executed data and, if known, data name in progress bar.
  //if (pdata->superblock.name[0]=='\0')
  //  sprintf(s,"Processing image");
  //else {
  //  sprintf(s,"%.64s (page %i)",
  //      pdata->superblock.name,pdata->superblock.page);
  //}
  //percent=(pdata->posy*pdata->nposx+pdata->posx)*100/
  //  (pdata->nposx*pdata->nposy);
  //  Message(s,percent);
  
  index=pdata->posy*pdata->nposx+pdata->posx;
  old=pdata->qmap[index];
  // On the second pass only the blocks that failed are worth another try:
  // their neighbours are decoded by now, so the search window lands right.
  if (pdata->pass>0 && old>=0 && old<17)
    goto finish;
  // Decode block.
  answer=Decodeblock(pdata,pdata->posx,pdata->posy,&result);
  // A retry that locates nothing is no reason to discard the first attempt.
  if (answer<0 && old>=0)
    goto finish;
  // A block the global grid could not deliver gets a second attempt at the
  // position its decoded neighbours predict - one grid step away instead of
  // half a page, so local paper warp and feed drift do not accumulate.
  // Two decoded neighbours or more: this position is surrounded by data and
  // worth the extra work. One or none, and it is most likely simply off the
  // edge of the raster, where a retry would only cost time.
  if ((answer<0 || answer>=17) &&
    Predictorigin(pdata,pdata->posx,pdata->posy,&predx,&predy)>=2
  ) {
    pdata->usepred=1;
    second=Decodeblock(pdata,pdata->posx,pdata->posy,&retry);
    pdata->usepred=0;
    if (second>=0 && second<17) {
      answer=second;
      result=retry; }
    else if (answer<0)
      answer=second; };
  pdata->qmap[index]=(signed char)(answer<0?-1:answer);
  // If we are unable to locate block, probably we are outside the raster.
  if (answer<0)
    goto finish;
  if (pdata->pass>0 && old>=17)
    pdata->nbad--;                     // The earlier failure is superseded
  // If this is the very first block located on the page, show it in the block
  // display window.
  //if (pdata->ngood==0 && pdata->nbad==0 && pdata->nsuper==0)
  //  Displayblockimage(pdata,pdata->posx,pdata->posy,answer,&result);
  // Analyze answer.
  if (answer>=17) {
    // Error, block is unreadable.
    pdata->nbad++; }
  else if (result.addr==SUPERBLOCK) {
    // Superblock.
    pdata->superblock.addr=SUPERBLOCK;
    pdata->superblock.datasize=((t_superdata *)&result)->datasize;
    pdata->superblock.pagesize=((t_superdata *)&result)->pagesize;
    pdata->superblock.origsize=((t_superdata *)&result)->origsize;
    pdata->superblock.mode=((t_superdata *)&result)->mode;
    pdata->superblock.page=((t_superdata *)&result)->page;
    pdata->superblock.modified=((t_superdata *)&result)->modified;
    pdata->superblock.attributes=((t_superdata *)&result)->attributes;
    pdata->superblock.filecrc=((t_superdata *)&result)->filecrc;
    memcpy(pdata->superblock.name,((t_superdata *)&result)->name,64);
    pdata->nsuper++;
    pdata->nrestored+=answer; }
  else if (pdata->ngood<pdata->nposx*pdata->nposy) {
    // Success, place data block into the intermediate buffer.
    pdata->blocklist[pdata->ngood].addr=result.addr & 0x0FFFFFFF;
    ngroup=(result.addr>>28) & 0x0000000F;
    if (ngroup>0) {                    // Recovery block
      pdata->blocklist[pdata->ngood].recsize=ngroup*NDATA;
      pdata->superblock.ngroup=ngroup; }
    else                               // Data block
      pdata->blocklist[pdata->ngood].recsize=0;
    memcpy(pdata->blocklist[pdata->ngood].data,result.data,NDATA);
    pdata->ngood++;
    // Number of bytes corrected by ECC may be misleading (block is so good
    // it can be read with wrong settings), but I have no better indicator
    // of quality.
    pdata->nrestored+=answer; };
  // Add block to quality map.
  //Addblocktomap(pdata->posx,pdata->posy,answer);
  // Block processed, set new coordinates.
finish:
  pdata->posx++;
  if (pdata->posx>=pdata->nposx) {
    pdata->posx=0;
    pdata->posy++;
    if (pdata->posy>=pdata->nposy) {
      if (pdata->pass==0 && pdata->nbad>0) {
        pdata->pass=1;                 // Retry the failed blocks
        pdata->posx=pdata->posy=0; }
      else
        pdata->step++;                 // Page processed
    };
  };
};

// Prints one character per block: a digit for a decoded block (how many bytes
// its ECC had to repair), '+' for ten or more repairs, '#' for a block that
// was located but stayed unreadable, '.' for one the grid search never found.
// The pattern names the cause: dots in clusters mean the raster was lost,
// hashes scattered over the page mean the dots themselves are too poor.
static void Printqualitymap(t_procdata *pdata) {
  int x,y,q;
  if (pdata->qmap==NULL)
    return;
  printf("Block map %dx%d "
    "(digit: ECC repairs, +: 10 or more, #: unreadable, .: not located)\n",
    pdata->nposx,pdata->nposy);
  for (y=0; y<pdata->nposy; y++) {
    for (x=0; x<pdata->nposx; x++) {
      q=pdata->qmap[y*pdata->nposx+x];
      if (q<0)        putchar('.');
      else if (q>=17) putchar('#');
      else if (q>9)   putchar('+');
      else            putchar('0'+q);
    };
    putchar('\n');
  };
};

// Passes gathered data to file processor and frees resources allocated by call
// to Preparefordecoding().
static void Finishdecoding(t_procdata *pdata) {
  int i,fileindex;
  if (pb_qualitymap)
    Printqualitymap(pdata);
  Printdotwidth();
  // Pass gathered data to file processor.
  if (pdata->superblock.addr==0)
    Reporterror("Page label is not readable");
  else {
    fileindex=Startnextpage(&pdata->superblock);
    if (fileindex>=0) {
      for (i=0; i<pdata->ngood; i++)
        Addblock(pdata->blocklist+i,fileindex);
      Finishpage(fileindex,
        pdata->ngood+pdata->nsuper,pdata->nbad,pdata->nrestored);
      ;
    };
  };
  // Page processed.
  pdata->step=0;
};

// Extracts data from the bitmap in small slices. To start decoding, pass
// bitmap to Startbitmapdecoding().
void Nextdataprocessingstep(t_procdata *pdata) {
  if (pdata==NULL)
    return;                            // Invalid data descriptor
  switch (pdata->step) {
    case 0:                            // Idle data
      return;
    case 1:                            // Remove previous images
      //SetWindowPos(hwmain,HWND_TOP,0,0,0,0,
      //  SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
      //Initqualitymap(0,0);
      //Displayblockimage(NULL,0,0,0,NULL);
      pdata->step++;
      break;
    case 2:                            // Determine grid size
      Message("Searching for raster...", 0);
      Getgridposition(pdata);
      break;
    case 3:                            // Determine min and max intensity
      Getgridintensity(pdata);
      break;
    case 4:                            // Determine step and angle in X
      Message("Searching for grid lines...", 0);
      Getxangle(pdata);
      break;
    case 5:                            // Determine step and angle in Y
      Getyangle(pdata);
      break;
    case 6:                            // Prepare for data decoding
      Message("Decoding", 0);
      Preparefordecoding(pdata);
      break;
    case 7:                            // Decode next block of data
      Decodenextblock(pdata);
      break;
    case 8:                            // Finish data decoding
      Finishdecoding(pdata);
      break;
    default: break;                    // Internal error
  };
  //if (pdata->step==0) Updatebuttons(); // Right or wrong, decoding finished
};

// Frees resources allocated by pdata.
void Freeprocdata(t_procdata *pdata) {
  // Free data.
  if (pdata->data!=NULL) {
    free(pdata->data);
    pdata->data=NULL; };
  // Free allocated buffers.
  if (pdata->buf1!=NULL) {
    free(pdata->buf1);
    pdata->buf1=NULL; };
  if (pdata->buf2!=NULL) {
    free(pdata->buf2);
    pdata->buf2=NULL; };
  if (pdata->bufx!=NULL) {
    free(pdata->bufx);
    pdata->bufx=NULL; };
  if (pdata->bufy!=NULL) {
    free(pdata->bufy);
    pdata->bufy=NULL; };
  if (pdata->blocklist!=NULL) {
    free(pdata->blocklist);
    pdata->blocklist=NULL; };
  if (pdata->qmap!=NULL) {
    free(pdata->qmap);
    pdata->qmap=NULL; };
  if (pdata->orgx!=NULL) {
    free(pdata->orgx);
    pdata->orgx=NULL; };
  if (pdata->orgy!=NULL) {
    free(pdata->orgy);
    pdata->orgy=NULL;
  };
};

// Starts decoding of the new bitmap. If previous decoding is still running,
// it will be stopped and all intermediate results will be discarded.
void Startbitmapdecoding(t_procdata *pdata,uchar *data,int sizex,int sizey) {
  // Free resources allocated for the previous bitmap. User may want to
  // browse bitmap while and after it is processed.
  Freeprocdata(pdata);
  memset(pdata,0,sizeof(t_procdata));
  pdata->data=data;
  pdata->sizex=sizex;
  pdata->sizey=sizey;
  pdata->blockborder=0.0;              // Autoselect
  pdata->step=1;
  memset(rulerprof,0,sizeof(rulerprof));
  rulerdots=0;
  if (pb_bestquality)
    pdata->mode|=M_BEST;
  //Updatebuttons();
};

// Stops bitmap decoding. Data decoded so far is discarded, but resources
// (especially, bitmap) remain in memory.
void Stopbitmapdecoding(t_procdata *pdata) {
  if (pdata->step!=0) {
    pdata->step=0;
  };
};

