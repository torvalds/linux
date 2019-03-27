/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991-1997 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <signal.h>
#include <sys/fbio.h>
#include "vgl.h"

static byte VGLSavePaletteRed[256];
static byte VGLSavePaletteGreen[256];
static byte VGLSavePaletteBlue[256];

#define ABS(a)		(((a)<0) ? -(a) : (a))
#define SGN(a)		(((a)<0) ? -1 : 1)
#define min(x, y)	(((x) < (y)) ? (x) : (y))
#define max(x, y)	(((x) > (y)) ? (x) : (y))

static void
color2mem(u_long color, byte *b, int len)
{
  switch (len) {
  case 4:
    b[3] = (color >> 24) & 0xff;
    /* fallthrough */
  case 3:
    b[2] = (color >> 16) & 0xff;
    /* fallthrough */
  case 2:
    b[1] = (color >> 8) & 0xff;
    /* fallthrough */
  case 1:
  default:
    b[0] = color & 0xff;
    break;
  }

  return;
}

static u_long
mem2color(byte *b, int len)
{
  u_long color = 0;

  switch (len) {
  case 4:
    color |= (b[3] & 0xff) << 24;
    /* fallthrough */
  case 3:
    color |= (b[2] & 0xff) << 16;
    /* fallthrough */
  case 2:
    color |= (b[1] & 0xff) << 8;
    /* fallthrough */
  case 1:
  default:
    color |= (b[0] & 0xff);
    break;
  }

  return color;
}

void
VGLSetXY(VGLBitmap *object, int x, int y, u_long color)
{
  int offset;
  byte b[4];

  VGLCheckSwitch();
  if (x>=0 && x<object->VXsize && y>=0 && y<object->VYsize) {
    if (!VGLMouseFreeze(x, y, 1, 1, color)) {
      switch (object->Type) {
      case MEMBUF:
      case VIDBUF8:
        object->Bitmap[y*object->VXsize+x]=((byte)color);
        break;
      case VIDBUF8S:
	object->Bitmap[VGLSetSegment(y*object->VXsize+x)]=((byte)color);
	break;
      case VIDBUF16:
      case VIDBUF24:
      case VIDBUF32:
	color2mem(color, b, object->PixelBytes);
        bcopy(b, &object->Bitmap[(y*object->VXsize+x) * object->PixelBytes],
		object->PixelBytes);
        break;
      case VIDBUF16S:
      case VIDBUF24S:
      case VIDBUF32S:
	color2mem(color, b, object->PixelBytes);
	offset = VGLSetSegment((y*object->VXsize+x) * object->PixelBytes);
	bcopy(b, &object->Bitmap[offset], object->PixelBytes);
	break;
      case VIDBUF8X:
        outb(0x3c4, 0x02);
        outb(0x3c5, 0x01 << (x&0x3));
	object->Bitmap[(unsigned)(VGLAdpInfo.va_line_width*y)+(x/4)] = ((byte)color);
	break;
      case VIDBUF4S:
	offset = VGLSetSegment(y*VGLAdpInfo.va_line_width + x/8);
	goto set_planar;
      case VIDBUF4:
	offset = y*VGLAdpInfo.va_line_width + x/8;
set_planar:
        outb(0x3c4, 0x02); outb(0x3c5, 0x0f);
        outb(0x3ce, 0x00); outb(0x3cf, (byte)color & 0x0f);	/* set/reset */
        outb(0x3ce, 0x01); outb(0x3cf, 0x0f);		/* set/reset enable */
        outb(0x3ce, 0x08); outb(0x3cf, 0x80 >> (x%8));	/* bit mask */
	object->Bitmap[offset] |= (byte)color;
      }
    }
    VGLMouseUnFreeze();
  }
}

u_long
VGLGetXY(VGLBitmap *object, int x, int y)
{
  int offset;
  byte b[4];
  int i;
  u_long color;
  byte mask;

  VGLCheckSwitch();
  if (x<0 || x>=object->VXsize || y<0 || y>=object->VYsize)
    return 0;
  switch (object->Type) {
    case MEMBUF:
    case VIDBUF8:
      return object->Bitmap[((y*object->VXsize)+x)];
    case VIDBUF8S:
      return object->Bitmap[VGLSetSegment(y*object->VXsize+x)];
    case VIDBUF16:
    case VIDBUF24:
    case VIDBUF32:
      bcopy(&object->Bitmap[(y*object->VXsize+x) * object->PixelBytes],
		b, object->PixelBytes);
      return (mem2color(b, object->PixelBytes));
    case VIDBUF16S:
    case VIDBUF24S:
    case VIDBUF32S:
	offset = VGLSetSegment((y*object->VXsize+x) * object->PixelBytes);
	bcopy(&object->Bitmap[offset], b, object->PixelBytes);

      return (mem2color(b, object->PixelBytes));
    case VIDBUF8X:
      outb(0x3ce, 0x04); outb(0x3cf, x & 0x3);
      return object->Bitmap[(unsigned)(VGLAdpInfo.va_line_width*y)+(x/4)];
    case VIDBUF4S:
      offset = VGLSetSegment(y*VGLAdpInfo.va_line_width + x/8);
      goto get_planar;
    case VIDBUF4:
      offset = y*VGLAdpInfo.va_line_width + x/8;
get_planar:
      color = 0;
      mask = 0x80 >> (x%8);
      for (i = 0; i < VGLModeInfo.vi_planes; i++) {
	outb(0x3ce, 0x04); outb(0x3cf, i);
	color |= (((volatile VGLBitmap *)object)->Bitmap[offset] & mask) ?
		 (1 << i) : 0;
      }
      return color;
  }
  return 0;		/* XXX black? */
}

 /*
  * Symmetric Double Step Line Algorithm by Brian Wyvill from
  * "Graphics Gems", Academic Press, 1990.
  */

#define SL_SWAP(a,b)           {a^=b; b^=a; a^=b;}
#define SL_ABSOLUTE(i,j,k)     ( (i-j)*(k = ( (i-j)<0 ? -1 : 1)))

void
plot(VGLBitmap * object, int x, int y, int flag, byte color)
{
  /* non-zero flag indicates the pixels need swapping back. */
  if (flag)
    VGLSetXY(object, y, x, color);
  else
    VGLSetXY(object, x, y, color);
}


void
VGLLine(VGLBitmap *object, int x1, int y1, int x2, int y2, u_long color)
{
  int dx, dy, incr1, incr2, D, x, y, xend, c, pixels_left;
  int sign_x, sign_y, step, reverse, i;

  dx = SL_ABSOLUTE(x2, x1, sign_x);
  dy = SL_ABSOLUTE(y2, y1, sign_y);
  /* decide increment sign by the slope sign */
  if (sign_x == sign_y)
    step = 1;
  else
    step = -1;

  if (dy > dx) {	/* chooses axis of greatest movement (make dx) */
    SL_SWAP(x1, y1);
    SL_SWAP(x2, y2);
    SL_SWAP(dx, dy);
    reverse = 1;
  } else
    reverse = 0;
  /* note error check for dx==0 should be included here */
  if (x1 > x2) {      /* start from the smaller coordinate */
    x = x2;
    y = y2;
/*  x1 = x1;
    y1 = y1; */
  } else {
    x = x1;
    y = y1;
    x1 = x2;
    y1 = y2;
  }


  /* Note dx=n implies 0 - n or (dx+1) pixels to be set */
  /* Go round loop dx/4 times then plot last 0,1,2 or 3 pixels */
  /* In fact (dx-1)/4 as 2 pixels are already plotted */
  xend = (dx - 1) / 4;
  pixels_left = (dx - 1) % 4;  /* number of pixels left over at the
           * end */
  plot(object, x, y, reverse, color);
  if (pixels_left < 0)
    return;      /* plot only one pixel for zero length
           * vectors */
  plot(object, x1, y1, reverse, color);  /* plot first two points */
  incr2 = 4 * dy - 2 * dx;
  if (incr2 < 0) {    /* slope less than 1/2 */
    c = 2 * dy;
    incr1 = 2 * c;
    D = incr1 - dx;

    for (i = 0; i < xend; i++) {  /* plotting loop */
      ++x;
      --x1;
      if (D < 0) {
        /* pattern 1 forwards */
        plot(object, x, y, reverse, color);
        plot(object, ++x, y, reverse, color);
        /* pattern 1 backwards */
        plot(object, x1, y1, reverse, color);
        plot(object, --x1, y1, reverse, color);
        D += incr1;
      } else {
        if (D < c) {
          /* pattern 2 forwards */
          plot(object, x, y, reverse, color);
          plot(object, ++x, y += step, reverse,
              color);
          /* pattern 2 backwards */
          plot(object, x1, y1, reverse, color);
          plot(object, --x1, y1 -= step, reverse,
              color);
        } else {
          /* pattern 3 forwards */
          plot(object, x, y += step, reverse, color);
          plot(object, ++x, y, reverse, color);
          /* pattern 3 backwards */
          plot(object, x1, y1 -= step, reverse,
              color);
          plot(object, --x1, y1, reverse, color);
        }
        D += incr2;
      }
    }      /* end for */

    /* plot last pattern */
    if (pixels_left) {
      if (D < 0) {
        plot(object, ++x, y, reverse, color);  /* pattern 1 */
        if (pixels_left > 1)
          plot(object, ++x, y, reverse, color);
        if (pixels_left > 2)
          plot(object, --x1, y1, reverse, color);
      } else {
        if (D < c) {
          plot(object, ++x, y, reverse, color);  /* pattern 2  */
          if (pixels_left > 1)
            plot(object, ++x, y += step, reverse, color);
          if (pixels_left > 2)
            plot(object, --x1, y1, reverse, color);
        } else {
          /* pattern 3 */
          plot(object, ++x, y += step, reverse, color);
          if (pixels_left > 1)
            plot(object, ++x, y, reverse, color);
          if (pixels_left > 2)
            plot(object, --x1, y1 -= step, reverse, color);
        }
      }
    }      /* end if pixels_left */
  }
  /* end slope < 1/2 */
  else {        /* slope greater than 1/2 */
    c = 2 * (dy - dx);
    incr1 = 2 * c;
    D = incr1 + dx;
    for (i = 0; i < xend; i++) {
      ++x;
      --x1;
      if (D > 0) {
        /* pattern 4 forwards */
        plot(object, x, y += step, reverse, color);
        plot(object, ++x, y += step, reverse, color);
        /* pattern 4 backwards */
        plot(object, x1, y1 -= step, reverse, color);
        plot(object, --x1, y1 -= step, reverse, color);
        D += incr1;
      } else {
        if (D < c) {
          /* pattern 2 forwards */
          plot(object, x, y, reverse, color);
          plot(object, ++x, y += step, reverse,
              color);

          /* pattern 2 backwards */
          plot(object, x1, y1, reverse, color);
          plot(object, --x1, y1 -= step, reverse,
              color);
        } else {
          /* pattern 3 forwards */
          plot(object, x, y += step, reverse, color);
          plot(object, ++x, y, reverse, color);
          /* pattern 3 backwards */
          plot(object, x1, y1 -= step, reverse, color);
          plot(object, --x1, y1, reverse, color);
        }
        D += incr2;
      }
    }      /* end for */
    /* plot last pattern */
    if (pixels_left) {
      if (D > 0) {
        plot(object, ++x, y += step, reverse, color);  /* pattern 4 */
        if (pixels_left > 1)
          plot(object, ++x, y += step, reverse,
              color);
        if (pixels_left > 2)
          plot(object, --x1, y1 -= step, reverse,
              color);
      } else {
        if (D < c) {
          plot(object, ++x, y, reverse, color);  /* pattern 2  */
          if (pixels_left > 1)
            plot(object, ++x, y += step, reverse, color);
          if (pixels_left > 2)
            plot(object, --x1, y1, reverse, color);
        } else {
          /* pattern 3 */
          plot(object, ++x, y += step, reverse, color);
          if (pixels_left > 1)
            plot(object, ++x, y, reverse, color);
          if (pixels_left > 2) {
            if (D > c)  /* step 3 */
              plot(object, --x1, y1 -= step, reverse, color);
            else  /* step 2 */
              plot(object, --x1, y1, reverse, color);
          }
        }
      }
    }
  }
}

void
VGLBox(VGLBitmap *object, int x1, int y1, int x2, int y2, u_long color)
{
  VGLLine(object, x1, y1, x2, y1, color);
  VGLLine(object, x2, y1, x2, y2, color);
  VGLLine(object, x2, y2, x1, y2, color);
  VGLLine(object, x1, y2, x1, y1, color);
}

void
VGLFilledBox(VGLBitmap *object, int x1, int y1, int x2, int y2, u_long color)
{
  int y;

  for (y=y1; y<=y2; y++) VGLLine(object, x1, y, x2, y, color);
}

static inline void
set4pixels(VGLBitmap *object, int x, int y, int xc, int yc, u_long color)
{
  if (x!=0) { 
    VGLSetXY(object, xc+x, yc+y, color); 
    VGLSetXY(object, xc-x, yc+y, color); 
    if (y!=0) { 
      VGLSetXY(object, xc+x, yc-y, color); 
      VGLSetXY(object, xc-x, yc-y, color); 
    } 
  } 
  else { 
    VGLSetXY(object, xc, yc+y, color); 
    if (y!=0) 
      VGLSetXY(object, xc, yc-y, color); 
  } 
}

void
VGLEllipse(VGLBitmap *object, int xc, int yc, int a, int b, u_long color)
{
  int x = 0, y = b, asq = a*a, asq2 = a*a*2, bsq = b*b;
  int bsq2 = b*b*2, d = bsq-asq*b+asq/4, dx = 0, dy = asq2*b;

  while (dx<dy) {
    set4pixels(object, x, y, xc, yc, color);
    if (d>0) {
      y--; dy-=asq2; d-=dy;
    }
    x++; dx+=bsq2; d+=bsq+dx;
  }
  d+=(3*(asq-bsq)/2-(dx+dy))/2;
  while (y>=0) {
    set4pixels(object, x, y, xc, yc, color);
    if (d<0) {
      x++; dx+=bsq2; d+=dx;
    }
    y--; dy-=asq2; d+=asq-dy;
  }
}

static inline void
set2lines(VGLBitmap *object, int x, int y, int xc, int yc, u_long color)
{
  if (x!=0) { 
    VGLLine(object, xc+x, yc+y, xc-x, yc+y, color); 
    if (y!=0) 
      VGLLine(object, xc+x, yc-y, xc-x, yc-y, color); 
  } 
  else { 
    VGLLine(object, xc, yc+y, xc, yc-y, color); 
  } 
}

void
VGLFilledEllipse(VGLBitmap *object, int xc, int yc, int a, int b, u_long color)
{
  int x = 0, y = b, asq = a*a, asq2 = a*a*2, bsq = b*b;
  int bsq2 = b*b*2, d = bsq-asq*b+asq/4, dx = 0, dy = asq2*b;

  while (dx<dy) {
    set2lines(object, x, y, xc, yc, color);
    if (d>0) {
      y--; dy-=asq2; d-=dy;
    }
    x++; dx+=bsq2; d+=bsq+dx;
  }
  d+=(3*(asq-bsq)/2-(dx+dy))/2;
  while (y>=0) {
    set2lines(object, x, y, xc, yc, color);
    if (d<0) {
      x++; dx+=bsq2; d+=dx;
    }
    y--; dy-=asq2; d+=asq-dy;
  }
}

void
VGLClear(VGLBitmap *object, u_long color)
{
  int offset;
  int len;
  int i, total = 0;
  byte b[4];

  VGLCheckSwitch();
  VGLMouseFreeze(0, 0, object->Xsize, object->Ysize, color); /* XXX */
  switch (object->Type) {
  case MEMBUF:
  case VIDBUF8:
    memset(object->Bitmap, (byte)color, object->VXsize*object->VYsize);
    break;

  case VIDBUF8S:
    for (offset = 0; offset < object->VXsize*object->VYsize; ) {
      VGLSetSegment(offset);
      len = min(object->VXsize*object->VYsize - offset,
		VGLAdpInfo.va_window_size);
      memset(object->Bitmap, (byte)color, len);
      offset += len;
    }
    break;
  case VIDBUF16:
  case VIDBUF24:
  case VIDBUF32:
    color2mem(color, b, object->PixelBytes);
    total = object->VXsize*object->VYsize*object->PixelBytes;
    for (i = 0; i < total; i += object->PixelBytes)
      bcopy(b, object->Bitmap + i, object->PixelBytes);
    break;

  case VIDBUF16S:
  case VIDBUF24S:
  case VIDBUF32S:
    color2mem(color, b, object->PixelBytes);
    total = object->VXsize*object->VYsize*object->PixelBytes;
    for (offset = 0; offset < total; ) {
      VGLSetSegment(offset);
      len = min(total - offset, VGLAdpInfo.va_window_size);
      for (i = 0; i < len; i += object->PixelBytes)
	bcopy(object->Bitmap + (offset + i) % VGLAdpInfo.va_window_size, b,
	      object->PixelBytes);
      offset += len;
    }
    break;

  case VIDBUF8X:
    /* XXX works only for Xsize % 4 = 0 */
    outb(0x3c6, 0xff);
    outb(0x3c4, 0x02); outb(0x3c5, 0x0f);
    memset(object->Bitmap, (byte)color, VGLAdpInfo.va_line_width*object->VYsize);
    break;

  case VIDBUF4:
  case VIDBUF4S:
    /* XXX works only for Xsize % 8 = 0 */
    outb(0x3c4, 0x02); outb(0x3c5, 0x0f);
    outb(0x3ce, 0x05); outb(0x3cf, 0x02);		/* mode 2 */
    outb(0x3ce, 0x01); outb(0x3cf, 0x00);		/* set/reset enable */
    outb(0x3ce, 0x08); outb(0x3cf, 0xff);		/* bit mask */
    for (offset = 0; offset < VGLAdpInfo.va_line_width*object->VYsize; ) {
      VGLSetSegment(offset);
      len = min(object->VXsize*object->VYsize - offset,
		VGLAdpInfo.va_window_size);
      memset(object->Bitmap, (byte)color, len);
      offset += len;
    }
    outb(0x3ce, 0x05); outb(0x3cf, 0x00);
    break;
  }
  VGLMouseUnFreeze();
}

void
VGLRestorePalette()
{
  int i;

  outb(0x3C6, 0xFF);
  inb(0x3DA); 
  outb(0x3C8, 0x00);
  for (i=0; i<256; i++) {
    outb(0x3C9, VGLSavePaletteRed[i]);
    inb(0x84);
    outb(0x3C9, VGLSavePaletteGreen[i]);
    inb(0x84);
    outb(0x3C9, VGLSavePaletteBlue[i]);
    inb(0x84);
  }
  inb(0x3DA);
  outb(0x3C0, 0x20);
}

void
VGLSavePalette()
{
  int i;

  outb(0x3C6, 0xFF);
  inb(0x3DA);
  outb(0x3C7, 0x00);
  for (i=0; i<256; i++) {
    VGLSavePaletteRed[i] = inb(0x3C9);
    inb(0x84);
    VGLSavePaletteGreen[i] = inb(0x3C9);
    inb(0x84);
    VGLSavePaletteBlue[i] = inb(0x3C9);
    inb(0x84);
  }
  inb(0x3DA);
  outb(0x3C0, 0x20);
}

void
VGLSetPalette(byte *red, byte *green, byte *blue)
{
  int i;
  
  for (i=0; i<256; i++) {
    VGLSavePaletteRed[i] = red[i];
    VGLSavePaletteGreen[i] = green[i];
    VGLSavePaletteBlue[i] = blue[i];
  }
  VGLCheckSwitch();
  outb(0x3C6, 0xFF);
  inb(0x3DA); 
  outb(0x3C8, 0x00);
  for (i=0; i<256; i++) {
    outb(0x3C9, VGLSavePaletteRed[i]);
    inb(0x84);
    outb(0x3C9, VGLSavePaletteGreen[i]);
    inb(0x84);
    outb(0x3C9, VGLSavePaletteBlue[i]);
    inb(0x84);
  }
  inb(0x3DA);
  outb(0x3C0, 0x20);
}

void
VGLSetPaletteIndex(byte color, byte red, byte green, byte blue)
{
  VGLSavePaletteRed[color] = red;
  VGLSavePaletteGreen[color] = green;
  VGLSavePaletteBlue[color] = blue;
  VGLCheckSwitch();
  outb(0x3C6, 0xFF);
  inb(0x3DA);
  outb(0x3C8, color); 
  outb(0x3C9, red); outb(0x3C9, green); outb(0x3C9, blue);
  inb(0x3DA);
  outb(0x3C0, 0x20);
}

void
VGLSetBorder(byte color)
{
  VGLCheckSwitch();
  inb(0x3DA);
  outb(0x3C0,0x11); outb(0x3C0, color); 
  inb(0x3DA);
  outb(0x3C0, 0x20);
}

void
VGLBlankDisplay(int blank)
{
  byte val;

  VGLCheckSwitch();
  outb(0x3C4, 0x01); val = inb(0x3C5); outb(0x3C4, 0x01);
  outb(0x3C5, ((blank) ? (val |= 0x20) : (val &= 0xDF)));
}
