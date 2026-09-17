#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include <errno.h>
#include "paperbak.h"

int       pb_resx, pb_resy;
t_printdata pb_printdata;
int       pb_orientation;
t_procdata pb_procdata;
t_fproc   pb_fproc[NFILE];
char      pb_infile[MAXPATH];
char      pb_outbmp[MAXPATH];
char      pb_inbmp[MAXPATH];
char      pb_outfile[MAXPATH];
int       pb_dpi;
int       pb_dotpercent;
int       pb_redundancy;
int       pb_printheader;
int       pb_printborder;
int       pb_autosave;
int       pb_bestquality;
int       pb_marginunits;
int       pb_marginleft;
int       pb_marginright;
int       pb_margintop;
int       pb_marginbottom;
int pb_errors, pb_force;
char pb_expect[SHA256_HEXLEN+1];
double pb_paperwidth=210, pb_paperheight=297;
double pb_margins[4]={10,10,10,10};
static int number(const char *s,int low,int high) {
  char *end; errno=0; long n=strtol(s,&end,10);
  return errno || end==s || *end || n<low || n>high ? -1 : (int)n;
}
static double dimension(const char *s) {
  char *end; double n=strtod(s,&end);
  if(end==s || !isfinite(n) || n<0) return -1;
  if(!strcmp(end,"in")) n*=25.4;
  else if(*end && strcmp(end,"mm")) return -1;
  return n;
}
static void help(void) {
 puts("Usage: paperback-cli --encode -i FILE -o PAGE.bmp [options]\n"
 "       paperback-cli --decode -i SCAN.bmp [-i SCAN2.bmp ...] -o FILE\n"
 "       paperback-cli --decode -o FILE SCAN1.bmp SCAN2.bmp ...\n"
 "  -p, --pages N         Read base_0001.bmp through base_NNNN.bmp\n"
 "  --expect HEX          Check the restored file against a SHA-256 digest\n"
 "  -f, --force           Accept damaged pages; save to -o with zero-filled gaps\n"
 "  --paper NAME          A3, A4 (default), A5, A6, Letter, Legal, Tabloid\n"
 "  --paper-size WxHmm    Custom sheet size (also WxHin)\n"
 "  --landscape           Swap sheet dimensions\n"
 "  --margin SIZE         All margins, default 10mm; mm or in\n"
 "  --margin-left SIZE    Also --margin-right, --margin-top, --margin-bottom\n"
 "  --image-dpi N         Bitmap resolution, 80..2400; default 3 times --dpi\n"
 "  -d, --dpi N           Code dot density, 40..600; default 150\n"
 "  -s, --dotsize N       Dot width percent, 50..100; default 70\n"
 "  -r, --redundancy N    One recovery block per N data blocks, 2..10; default 5\n"
 "  -b, --border          Black outer border\n"
 "  --header              Print a text header and footer; costs grid rows\n"
 "  -h, --help            Help\n"
 "  -v, --version         Version\n"
 "Output uses the exact -o path; OUTPUT.map records gaps and integrity.\n"
 "Exit: 0 complete, 1 error, 2 damaged output saved. Print at actual size (100%).");
}
int main(int argc,char **argv) {
 enum { PAPER=256,SIZE,LANDSCAPE,MARGIN,LEFT,RIGHT,TOP,BOTTOM,IMAGE_DPI,HEADER,EXPECT };
 struct option options[]={
 {"encode",0,0,'e'},{"decode",0,0,'D'},{"input",1,0,'i'},{"output",1,0,'o'},
 {"pages",1,0,'p'},{"force",0,0,'f'},{"dpi",1,0,'d'},{"dotsize",1,0,'s'},
 {"redundancy",1,0,'r'},{"border",0,0,'b'},{"no-header",0,0,'n'},
 {"help",0,0,'h'},{"version",0,0,'v'},{"paper",1,0,PAPER},
 {"paper-size",1,0,SIZE},{"landscape",0,0,LANDSCAPE},{"margin",1,0,MARGIN},
 {"margin-left",1,0,LEFT},{"margin-right",1,0,RIGHT},{"margin-top",1,0,TOP},
 {"margin-bottom",1,0,BOTTOM},{"image-dpi",1,0,IMAGE_DPI},
 {"header",0,0,HEADER},{"expect",1,0,EXPECT},{0,0,0,0}};
 const char **inputs=calloc(argc,sizeof(*inputs));
 int count=0,mode=0,pages=0,landscape=0,c,status=0;
 if(!inputs) return 1;
 pb_dpi=150; pb_dotpercent=70; pb_redundancy=5; pb_autosave=0;
 while((c=getopt_long(argc,argv,"i:o:p:fd:s:r:nbvh",options,NULL))!=-1) {
  switch(c) {
   case 'e': case 'D': if(mode && mode!=c) goto invalid; mode=c; break;
   case 'i': inputs[count++]=optarg; break;
   case 'o': if(strlen(optarg)>=MAXPATH-32) goto invalid;
    strcpy(pb_outfile,optarg); strcpy(pb_outbmp,optarg); break;
   case 'p': pages=number(optarg,1,9999); if(pages<0) goto invalid; break;
   case 'f': pb_force=1; break;
   case 'd': pb_dpi=number(optarg,40,600); if(pb_dpi<0) goto invalid; break;
   case 's': pb_dotpercent=number(optarg,50,100); if(pb_dotpercent<0) goto invalid; break;
   case 'r': pb_redundancy=number(optarg,2,10); if(pb_redundancy<0) goto invalid; break;
   case 'b': pb_printborder=1; break;
   case 'n': break;
   case 'h': help(); free(inputs); return 0;
   case 'v': puts("PaperBack CLI 1.3 (GPL); PaperBack by Oleh Yuschuk"); free(inputs); return 0;
   case IMAGE_DPI: pb_resx=pb_resy=number(optarg,80,2400); if(pb_resx<0) goto invalid; break;
   case HEADER: pb_printheader=1; break;
   case EXPECT: {
    size_t n=strlen(optarg); if(n!=SHA256_HEXLEN) goto invalid;
    for(size_t i=0;i<n;i++) {
     int ch=tolower((unsigned char)optarg[i]);
     if(!isxdigit(ch)) goto invalid;
     pb_expect[i]=(char)ch; }
    pb_expect[n]='\0'; break;
   }
   case LANDSCAPE: landscape=1; break;
   case PAPER: {
    const char *names[]={"A3","A4","A5","A6","Letter","Legal","Tabloid"};
    double widths[]={297,210,148,105,215.9,215.9,279.4};
    double heights[]={420,297,210,148,279.4,355.6,431.8};
    int i; for(i=0;i<7;i++) if(!strnicmp(optarg,names[i],32)) break;
    if(i==7) goto invalid;
    pb_paperwidth=widths[i]; pb_paperheight=heights[i]; break;
   }
   case SIZE: {
    char *end; double w=strtod(optarg,&end);
    if(end==optarg || (*end!='x' && *end!='X') || !isfinite(w) || w<=0) goto invalid;
    double h=dimension(end+1); size_t len=strlen(optarg);
    if(len>=2 && !strcmp(optarg+len-2,"in")) w*=25.4;
    if(h<=0) goto invalid;
    pb_paperwidth=w; pb_paperheight=h; break;
   }
   case MARGIN: case LEFT: case RIGHT: case TOP: case BOTTOM: {
    double n=dimension(optarg); if(n<0) goto invalid;
    if(c==MARGIN) for(int i=0;i<4;i++) pb_margins[i]=n;
    else pb_margins[c-LEFT]=n;
    break;
   }
   default: goto invalid;
  }
 }
 while(optind<argc) inputs[count++]=argv[optind++];
 if(!mode || !count || !pb_outfile[0] || (pages && count!=1)) goto invalid;
 for(int i=0;i<count;i++) if(strlen(inputs[i])>=MAXPATH-32) goto invalid;
 if(mode=='e') {
  if(count!=1 || pages || pb_force || pb_expect[0]) goto invalid;
  if(landscape) {double t=pb_paperwidth;pb_paperwidth=pb_paperheight;pb_paperheight=t;}
  if(pb_paperwidth<=pb_margins[0]+pb_margins[1] || pb_paperheight<=pb_margins[2]+pb_margins[3]) goto invalid;
  if(!pb_resx) pb_resx=pb_resy=pb_dpi*3;
  if(pb_resx<2*pb_dpi) goto invalid;
  Printfile(inputs[0],pb_outbmp);
  while(pb_printdata.step) Nextdataprintingstep(&pb_printdata);
  status=pb_errors?1:0;
 } else {
  for(int i=0;i<(pages?pages:count);i++) {
   char path[MAXPATH];
   if(pages) {
    char drv[MAXDRIVE],dir[MAXDIR],name[MAXFILE],ext[MAXEXT];
    fnsplit(inputs[0],drv,dir,name,ext);
    snprintf(path,sizeof(path),"%s%s%s_%04d%s",drv,dir,name,i+1,ext);
   } else strcpy(path,inputs[i]);
   printf("Decoding %s\n",path);
   int errors=pb_errors;
   if(Decodebitmap(path)==0) while(pb_procdata.step) Nextdataprocessingstep(&pb_procdata);
   if(pb_errors!=errors && !pb_force) {status=1;break;}
  }
  Freeprocdata(&pb_procdata);
  int found=0,slot=-1;
  for(int i=0;i<NFILE;i++) if(pb_fproc[i].busy) {found++;slot=i;}
  if(found!=1) {Reporterror(found?"Scans contain different files; refusing to mix outputs":"No readable file header found");status=1;}
  else if(!status) {
   t_fproc *pf=&pb_fproc[slot];
   printf("Recovered %d/%d blocks; %d missing; %d repaired using redundancy\n",pf->ndata,pf->nblock,pf->nblock-pf->ndata,pf->recoveredblocks);
   if(pf->ndata!=pf->nblock && pf->pagesize) {
    fputs("Pages to rescan:",stdout);
    for(int page=0;page<pf->npages;page++) {
     int start=page*(pf->pagesize/NDATA),end=start+pf->pagesize/NDATA;
     for(int j=start;j<end && j<pf->nblock;j++) if(pf->datavalid[j]!=1) {printf(" %d",page+1);break;}
    }
    putchar('\n');
   }
   if(pf->ndata!=pf->nblock && !pb_force) {
    Reporterror("Incomplete file: scan again or use --force to accept damaged pages");status=1;
   } else {int saved=Saverestoredfile(slot,pb_force);status=saved<0?1:saved;}
  }
  for(int i=0;i<NFILE;i++) Closefproc(i);
 }
 free(inputs);return status;
invalid:
 fputs("Invalid arguments. Use --help.\n",stderr);free(inputs);return 1;
}
