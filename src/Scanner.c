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
#include "Bitmap.h"

#include "paperbak.h"
#include "Resource.h"





// Validate all offsets before converting to bottom-up grayscale. With --force,
// missing pixel bytes in a truncated BMP are white, never uninitialized memory.
int Decodebitmap(char *path) {
  FILE *f=fopen(path,"rb");
  BITMAPFILEHEADER file;
  BITMAPINFOHEADER info;
  uchar palette[256], *data=NULL, *row=NULL;
  int result=-1;
  if(!f) {Reporterror("Unable to open bitmap");return -1;}
  if(fread(&file,1,sizeof(file),f)!=sizeof(file) || fread(&info,1,sizeof(info),f)!=sizeof(info)) goto invalid;
  if(file.bfType!=CHAR_BM || info.biSize!=sizeof(info) || info.biPlanes!=1 ||
     (info.biBitCount!=8 && info.biBitCount!=24) || info.biCompression!=BI_RGB ||
     info.biWidth<128 || info.biWidth>32768 || info.biHeight==0 ||
     info.biHeight>32768 || info.biHeight< -32768 ||
     (info.biBitCount==24 && info.biClrUsed!=0)) goto invalid;
  int width=info.biWidth,height=abs(info.biHeight);
  size_t stride=((size_t)width*(info.biBitCount/8)+3)&~(size_t)3;
  if((size_t)width*height>268435456) goto invalid;
  unsigned colors=info.biBitCount==8?(info.biClrUsed?info.biClrUsed:256):0;
  if(colors>256 || file.bfOffBits<sizeof(file)+sizeof(info)+colors*sizeof(RGBQUAD)) goto invalid;
  memset(palette,255,sizeof(palette));
  for(unsigned i=0;i<colors;i++) {
    RGBQUAD color;
    if(fread(&color,1,sizeof(color),f)!=sizeof(color)) goto invalid;
    palette[i]=(color.rgbRed+color.rgbGreen+color.rgbBlue)/3;
  }
  if(fseek(f,0,SEEK_END)!=0) goto invalid;
  long length=ftell(f);
  if(length<0 || (size_t)length<file.bfOffBits) goto invalid;
  int truncated=(size_t)length-file.bfOffBits<stride*height;
  if(truncated && !pb_force) {Reporterror("Truncated bitmap; use --force to accept the damaged page");goto done;}
  if(truncated) fprintf(stderr,"Warning: truncated bitmap; missing pixels treated as white\n");
  if(fseek(f,file.bfOffBits,SEEK_SET)!=0) goto invalid;
  data=malloc((size_t)width*height); row=malloc(stride);
  if(!data || !row) {Reporterror("Low memory");goto done;}
  int badindex=0;
  for(int y=0;y<height;y++) {
    size_t got=fread(row,1,stride,f);
    uchar *dst=data+(size_t)(info.biHeight>0?y:height-1-y)*width;
    for(int x=0;x<width;x++) {
      size_t pos=(size_t)x*(info.biBitCount/8);
      if(pos+info.biBitCount/8>got) dst[x]=255;
      else if(info.biBitCount==24) dst[x]=(row[pos]+row[pos+1]+row[pos+2])/3;
      else if(row[pos]<colors) dst[x]=palette[row[pos]];
      else {dst[x]=255;badindex=1;}
    }
  }
  if(ferror(f)) {Reporterror("Bitmap read error");goto done;}
  if(badindex && !pb_force) goto invalid;
  if(badindex) fprintf(stderr,"Warning: invalid palette indices treated as white\n");
  Startbitmapdecoding(&pb_procdata,data,width,height); data=NULL; result=0;
  goto done;
invalid:
  Reporterror("Invalid or unsupported BMP (expected uncompressed 8-bit or 24-bit)");
done:
  free(row);free(data);fclose(f);return result;
}
