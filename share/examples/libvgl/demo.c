/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/fbio.h>
#include <sys/kbio.h>
#include <sys/consio.h>
#include <vgl.h>

int
main(int argc, char **argv)
{
  int y, xsize, ysize, i,j;
  VGLBitmap *tmp;

  // set graphics mode, here 320x240 256 colors
  // supported modes are (from <sys/consio.h>):
  // SW_VGA_CG320:      std VGA 320x200 256 colors
  // SW_VGA_MODEX:      Modex VGA 320x240 256 colors
  // SW_VGA_VG640:      std VGA 640x480 16 colors
  VGLInit(SW_VGA_MODEX);

  // initialize mouse and show pointer
  VGLMouseInit(VGL_MOUSESHOW);

  // VGLDisplay is a ptr to a struct Bitmap defined and initialized by
  // libvgl. The Bitmap points directly to screen memory etc.
  xsize=VGLDisplay->Xsize;
  ysize=VGLDisplay->Ysize;

  // alloc a new bitmap
  tmp = VGLBitmapCreate(MEMBUF, 256, 256, NULL);
  VGLBitmapAllocateBits(tmp);
  VGLClear(tmp, 0);

  // fill the screen with colored lines
  for (y=0; y<ysize; y++)
    VGLLine(VGLDisplay, 0, y, xsize-1, y, y/2 % 256);

  // draw some lines and circles just to show off
  VGLLine(VGLDisplay, 0, 0, xsize-1, ysize-1, 63);
  VGLLine(VGLDisplay, 0, ysize-1, xsize-1, 0, 63);
  VGLLine(VGLDisplay, 0, 0, 0, ysize-1, 63);
  VGLLine(VGLDisplay, xsize-1, 0, xsize-1, ysize-1, 63);
  VGLEllipse(VGLDisplay, 256, 0, 256, 256, 63);
  VGLEllipse(VGLDisplay, 0, 256, 256, 256, 0);

  // some text is also useful
  VGLBitmapString(VGLDisplay, 100,100,
    "This is text", 63, 0, 0, VGL_DIR_RIGHT);
  sleep(2);
  VGLBitmapString(VGLDisplay, 100,100,
    "This is text", 63, 0, 0, VGL_DIR_UP);
  sleep(2);
  VGLBitmapString(VGLDisplay, 100,100,
    "This is text", 63, 0, 0, VGL_DIR_LEFT);
  sleep(2);
  VGLBitmapString(VGLDisplay, 100,100,
    "This is text", 63, 0, 0, VGL_DIR_DOWN);
  sleep(2);

  // now show some simple bitblit
  for (i=0; i<256; i++)
    for (j=0; j<256; j++)
      tmp->Bitmap[i+256*j] = i%16;
  VGLBitmapCopy(tmp, 0, 0, VGLDisplay, 0, 0, 128, 128);
  for (i=0; i<256; i++)
    for (j=0; j<256; j++)
      tmp->Bitmap[i+256*j] = j%16;
  VGLBitmapCopy(tmp, 0, 0, VGLDisplay, 3, 128, 128, 128);
  sleep(2);
  VGLBitmapCopy(VGLDisplay, 237, 311, tmp, 64, 64, 128, 128);
  VGLBitmapCopy(tmp, 32, 32, VGLDisplay, 400, 128, 128, 128);
  sleep(2);
  VGLBitmapCopy(VGLDisplay, 300, 300, VGLDisplay, 500, 128, 128, 128);
  sleep(5);
  i=0;

  // loop around drawing and copying
  while (++i) {
    VGLBitmapCopy(VGLDisplay, rand()%xsize, rand()%ysize,
                  VGLDisplay, rand()%xsize, rand()%ysize,
                  rand()%xsize, rand()%ysize);
    VGLLine(VGLDisplay,  rand()%xsize, rand()%ysize, 
            rand()%xsize, rand()%ysize, rand()%256);
    VGLEllipse(VGLDisplay, rand()%xsize, rand()%ysize,
               rand()%xsize/2, rand()%ysize/2, rand()%256);
    rand();
    if (i > 1000) break;
  }

  // restore screen to its original mode
  VGLEnd();
  return 0;
}
