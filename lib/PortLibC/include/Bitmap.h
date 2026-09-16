/*
 * =====================================================================================
 *
 *       Filename:  Bitmap.h
 *
 *    Description:  Bitmap structs and defines
 *
 *        Version:  2.0
 *        Created:  07/26/2017 09:38:18 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  suhrke@teknik.io
 *
 * =====================================================================================
 */

#ifndef PORTLIBC_BITMAP_H
#define PORTLIBC_BITMAP_H

#include <stdint.h>

#define CHAR_BM 19778




// Only include if on systems other than Windows
#if ! ( defined(_WIN32) || defined(__CYGWIN__))


#define BI_RGB 0 //bitmap value for uncompressed color data



typedef struct __attribute__ ((packed)) BITMAPFILEHEADER {
  uint16_t bfType;          //filetype, must be 'BM'
  uint32_t bfSize;          //size in bytes of bitmap file 
  uint16_t bfReserved1;     //should be 0
  uint16_t bfReserved2;     //should be 0
  uint32_t bfOffBits;       //offset in bytes from beginning of 
                            // BITMAPFILEHEADER to the bitmap bits
} BITMAPFILEHEADER;



typedef struct __attribute__ ((packed)) BITMAPINFOHEADER {
  uint32_t biSize;         //# of bytes in struct
  int32_t biWidth;         //width of bitmap in pixels
  int32_t biHeight;        //height of bitmap in pixels
  uint16_t biPlanes;       //# of planes for target device 
  uint16_t biBitCount;     //bitmap, bits per pixel
  uint32_t biCompression;  //bitmap, type of compression
  uint32_t biSizeImage;    //size of image, in bytes
                           //   0 if BI_RGB
  int32_t biXPelsPerMeter; //horizontal resolution of target device
  int32_t biYPelsPerMeter; //vertical resolution of target device
  uint32_t biClrUsed;      //# of color indices in color table
                           //  if 0, bitmap uses max # of colors that
                           //  correspond to biBitCount
  uint32_t biClrImportant; //# of color indices required for displaying bitmap
} BITMAPINFOHEADER; 



typedef struct  __attribute__ ((packed)) RGBQUAD {
  unsigned char rgbBlue;
  unsigned char rgbGreen;
  unsigned char rgbRed;
  unsigned char rgbReserved; //should be 0
} RGBQUAD;



typedef struct  __attribute__ ((packed)) BITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD          bmiColors[1];
} BITMAPINFO;
#endif // not on windows/cygwin/msys2


#endif //PORTLIBC_BITMAP_H

