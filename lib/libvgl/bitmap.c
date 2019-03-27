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
 *    notice, this list of conditions and the following disclaimer,
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/types.h>
#include <signal.h>
#include <sys/fbio.h>
#include "vgl.h"

#define min(x, y)	(((x) < (y)) ? (x) : (y))

static byte mask[8] = {0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01};
static int color2bit[16] = {0x00000000, 0x00000001, 0x00000100, 0x00000101,
			    0x00010000, 0x00010001, 0x00010100, 0x00010101,
			    0x01000000, 0x01000001, 0x01000100, 0x01000101,
			    0x01010000, 0x01010001, 0x01010100, 0x01010101};

static void
WriteVerticalLine(VGLBitmap *dst, int x, int y, int width, byte *line)
{
  int i, pos, last, planepos, start_offset, end_offset, offset;
  int len;
  unsigned int word = 0;
  byte *address;
  byte *VGLPlane[4];

  switch (dst->Type) {
  case VIDBUF4:
  case VIDBUF4S:
    start_offset = (x & 0x07);
    end_offset = (x + width) & 0x07;
    i = (width + start_offset) / 8;
    if (end_offset)
	i++;
    VGLPlane[0] = VGLBuf;
    VGLPlane[1] = VGLPlane[0] + i;
    VGLPlane[2] = VGLPlane[1] + i;
    VGLPlane[3] = VGLPlane[2] + i;
    pos = 0;
    planepos = 0;
    last = 8 - start_offset;
    while (pos < width) {
      word = 0;
      while (pos < last && pos < width)
	word = (word<<1) | color2bit[line[pos++]&0x0f];
      VGLPlane[0][planepos] = word;
      VGLPlane[1][planepos] = word>>8;
      VGLPlane[2][planepos] = word>>16;
      VGLPlane[3][planepos] = word>>24;
      planepos++;
      last += 8;
    }
    planepos--;
    if (end_offset) {
      word <<= (8 - end_offset);
      VGLPlane[0][planepos] = word;
      VGLPlane[1][planepos] = word>>8;
      VGLPlane[2][planepos] = word>>16;
      VGLPlane[3][planepos] = word>>24;
    }
    if (start_offset || end_offset)
      width+=8;
    width /= 8;
    outb(0x3ce, 0x01); outb(0x3cf, 0x00);		/* set/reset enable */
    outb(0x3ce, 0x08); outb(0x3cf, 0xff);		/* bit mask */
    for (i=0; i<4; i++) {
      outb(0x3c4, 0x02);
      outb(0x3c5, 0x01<<i);
      outb(0x3ce, 0x04);
      outb(0x3cf, i);
      pos = VGLAdpInfo.va_line_width*y + x/8;
      if (dst->Type == VIDBUF4) {
	if (end_offset)
	  VGLPlane[i][planepos] |= dst->Bitmap[pos+planepos] & mask[end_offset];
	if (start_offset)
	  VGLPlane[i][0] |= dst->Bitmap[pos] & ~mask[start_offset];
	bcopy(&VGLPlane[i][0], dst->Bitmap + pos, width);
      } else {	/* VIDBUF4S */
	if (end_offset) {
	  offset = VGLSetSegment(pos + planepos);
	  VGLPlane[i][planepos] |= dst->Bitmap[offset] & mask[end_offset];
	}
	offset = VGLSetSegment(pos);
	if (start_offset)
	  VGLPlane[i][0] |= dst->Bitmap[offset] & ~mask[start_offset];
	for (last = width; ; ) { 
	  len = min(VGLAdpInfo.va_window_size - offset, last);
	  bcopy(&VGLPlane[i][width - last], dst->Bitmap + offset, len);
	  pos += len;
	  last -= len;
	  if (last <= 0)
	    break;
	  offset = VGLSetSegment(pos);
	}
      }
    }
    break;
  case VIDBUF8X:
    address = dst->Bitmap + VGLAdpInfo.va_line_width * y + x/4;
    for (i=0; i<4; i++) {
      outb(0x3c4, 0x02);
      outb(0x3c5, 0x01 << ((x + i)%4));
      for (planepos=0, pos=i; pos<width; planepos++, pos+=4)
        address[planepos] = line[pos];
      if ((x + i)%4 == 3)
	++address;
    }
    break;
  case VIDBUF8S:
    pos = dst->VXsize * y + x;
    while (width > 0) {
      offset = VGLSetSegment(pos);
      i = min(VGLAdpInfo.va_window_size - offset, width);
      bcopy(line, dst->Bitmap + offset, i);
      line += i;
      pos += i;
      width -= i;
    }
    break;
  case VIDBUF16S:
  case VIDBUF24S:
  case VIDBUF32S:
    width = width * dst->PixelBytes;
    pos = (dst->VXsize * y + x) * dst->PixelBytes;
    while (width > 0) {
      offset = VGLSetSegment(pos);
      i = min(VGLAdpInfo.va_window_size - offset, width);
      bcopy(line, dst->Bitmap + offset, i);
      line += i;
      pos += i;
      width -= i;
    }
    break;
  case VIDBUF8:
  case MEMBUF:
    address = dst->Bitmap + dst->VXsize * y + x;
    bcopy(line, address, width);
    break;
  case VIDBUF16:
  case VIDBUF24:
  case VIDBUF32:
    address = dst->Bitmap + (dst->VXsize * y + x) * dst->PixelBytes;
    bcopy(line, address, width * dst->PixelBytes);
    break;
  default:
    ;
  }
}

static void
ReadVerticalLine(VGLBitmap *src, int x, int y, int width, byte *line)
{
  int i, bit, pos, count, planepos, start_offset, end_offset, offset;
  int width2, len;
  byte *address;
  byte *VGLPlane[4];

  switch (src->Type) {
  case VIDBUF4S:
    start_offset = (x & 0x07);
    end_offset = (x + width) & 0x07;
    count = (width + start_offset) / 8;
    if (end_offset)
      count++;
    VGLPlane[0] = VGLBuf;
    VGLPlane[1] = VGLPlane[0] + count;
    VGLPlane[2] = VGLPlane[1] + count;
    VGLPlane[3] = VGLPlane[2] + count;
    for (i=0; i<4; i++) {
      outb(0x3ce, 0x04);
      outb(0x3cf, i);
      pos = VGLAdpInfo.va_line_width*y + x/8;
      for (width2 = count; width2 > 0; ) {
	offset = VGLSetSegment(pos);
	len = min(VGLAdpInfo.va_window_size - offset, width2);
	bcopy(src->Bitmap + offset, &VGLPlane[i][count - width2], len);
	pos += len;
	width2 -= len;
      }
    }
    goto read_planar;
  case VIDBUF4:
    address = src->Bitmap + VGLAdpInfo.va_line_width * y + x/8;
    start_offset = (x & 0x07);
    end_offset = (x + width) & 0x07;
    count = (width + start_offset) / 8;
    if (end_offset)
      count++;
    VGLPlane[0] = VGLBuf;
    VGLPlane[1] = VGLPlane[0] + count;
    VGLPlane[2] = VGLPlane[1] + count;
    VGLPlane[3] = VGLPlane[2] + count;
    for (i=0; i<4; i++) {
      outb(0x3ce, 0x04);
      outb(0x3cf, i);
      bcopy(address, &VGLPlane[i][0], count);
    }
read_planar:
    pos = 0;
    planepos = 0;
    bit = 7 - start_offset;
    while (pos < width) {
      for (; bit >= 0 && pos < width; bit--, pos++) {
        line[pos] = (VGLPlane[0][planepos] & (1<<bit) ? 1 : 0) |
                    ((VGLPlane[1][planepos] & (1<<bit) ? 1 : 0) << 1) |
                    ((VGLPlane[2][planepos] & (1<<bit) ? 1 : 0) << 2) |
                    ((VGLPlane[3][planepos] & (1<<bit) ? 1 : 0) << 3);
      }
      planepos++;
      bit = 7;
    }
    break;
  case VIDBUF8X:
    address = src->Bitmap + VGLAdpInfo.va_line_width * y + x/4;
    for (i=0; i<4; i++) {
      outb(0x3ce, 0x04);
      outb(0x3cf, (x + i)%4);
      for (planepos=0, pos=i; pos<width; planepos++, pos+=4)
        line[pos] = address[planepos];
      if ((x + i)%4 == 3)
	++address;
    }
    break;
  case VIDBUF8S:
    pos = src->VXsize * y + x;
    while (width > 0) {
      offset = VGLSetSegment(pos);
      i = min(VGLAdpInfo.va_window_size - offset, width);
      bcopy(src->Bitmap + offset, line, i);
      line += i;
      pos += i;
      width -= i;
    }
    break;
  case VIDBUF16S:
  case VIDBUF24S:
  case VIDBUF32S:
    width = width * src->PixelBytes;
    pos = (src->VXsize * y + x) * src->PixelBytes;
    while (width > 0) {
      offset = VGLSetSegment(pos);
      i = min(VGLAdpInfo.va_window_size - offset, width);
      bcopy(src->Bitmap + offset, line, i);
      line += i;
      pos += i;
      width -= i;
    }
    break;
  case VIDBUF8:
  case MEMBUF:
    address = src->Bitmap + src->VXsize * y + x;
    bcopy(address, line, width);
    break;
  case VIDBUF16:
  case VIDBUF24:
  case VIDBUF32:
    address = src->Bitmap + (src->VXsize * y + x) * src->PixelBytes;
    bcopy(address, line, width * src->PixelBytes);
    break;
  default:
    ;
  }
}

int
__VGLBitmapCopy(VGLBitmap *src, int srcx, int srcy,
	      VGLBitmap *dst, int dstx, int dsty, int width, int hight)
{
  int srcline, dstline;

  if (srcx>src->VXsize || srcy>src->VYsize
	|| dstx>dst->VXsize || dsty>dst->VYsize)
    return -1;  
  if (srcx < 0) {
    width=width+srcx; dstx-=srcx; srcx=0;    
  }
  if (srcy < 0) {
    hight=hight+srcy; dsty-=srcy; srcy=0; 
  }
  if (dstx < 0) {    
    width=width+dstx; srcx-=dstx; dstx=0;
  }
  if (dsty < 0) {
    hight=hight+dsty; srcy-=dsty; dsty=0;
  }
  if (srcx+width > src->VXsize)
     width=src->VXsize-srcx;
  if (srcy+hight > src->VYsize)
     hight=src->VYsize-srcy;
  if (dstx+width > dst->VXsize)
     width=dst->VXsize-dstx;
  if (dsty+hight > dst->VYsize)
     hight=dst->VYsize-dsty;
  if (width < 0 || hight < 0)
     return -1;
  if (src->Type == MEMBUF) {
    for (srcline=srcy, dstline=dsty; srcline<srcy+hight; srcline++, dstline++) {
      WriteVerticalLine(dst, dstx, dstline, width, 
	(src->Bitmap+(srcline*src->VXsize)+srcx));
    }
  }
  else if (dst->Type == MEMBUF) {
    for (srcline=srcy, dstline=dsty; srcline<srcy+hight; srcline++, dstline++) {
      ReadVerticalLine(src, srcx, srcline, width,
	 (dst->Bitmap+(dstline*dst->VXsize)+dstx));
    }
  }
  else {
    byte buffer[2048];	/* XXX */
    byte *p;

    if (width * src->PixelBytes > sizeof(buffer)) {
      p = malloc(width * src->PixelBytes);
      if (p == NULL)
	return 1;
    } else {
      p = buffer;
    }
    for (srcline=srcy, dstline=dsty; srcline<srcy+hight; srcline++, dstline++) {
      ReadVerticalLine(src, srcx, srcline, width, p);
      WriteVerticalLine(dst, dstx, dstline, width, p);
    }
    if (width * src->PixelBytes > sizeof(buffer))
      free(p);
  }
  return 0;
}

int
VGLBitmapCopy(VGLBitmap *src, int srcx, int srcy,
	      VGLBitmap *dst, int dstx, int dsty, int width, int hight)
{
  int error;

  VGLMouseFreeze(dstx, dsty, width, hight, 0);
  error = __VGLBitmapCopy(src, srcx, srcy, dst, dstx, dsty, width, hight);
  VGLMouseUnFreeze();
  return error;
}

VGLBitmap
*VGLBitmapCreate(int type, int xsize, int ysize, byte *bits)
{
  VGLBitmap *object;

  if (type != MEMBUF)
    return NULL;
  if (xsize < 0 || ysize < 0)
    return NULL;
  object = (VGLBitmap *)malloc(sizeof(*object));
  if (object == NULL)
    return NULL;
  object->Type = type;
  object->Xsize = xsize;
  object->Ysize = ysize;
  object->VXsize = xsize;
  object->VYsize = ysize;
  object->Xorigin = 0;
  object->Yorigin = 0;
  object->Bitmap = bits;
  object->PixelBytes = VGLDisplay->PixelBytes;
  return object;
}

void
VGLBitmapDestroy(VGLBitmap *object)
{
  if (object->Bitmap)
    free(object->Bitmap);
  free(object);
}

int
VGLBitmapAllocateBits(VGLBitmap *object)
{
  object->Bitmap = malloc(object->VXsize*object->VYsize*object->PixelBytes);
  if (object->Bitmap == NULL)
    return -1;
  return 0;
}
