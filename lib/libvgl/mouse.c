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

#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#include "vgl.h"

#define X 0xff
static byte StdAndMask[MOUSE_IMG_SIZE*MOUSE_IMG_SIZE] = {
	X,X,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	X,X,X,0,0,0,0,0,0,0,0,0,0,0,0,0,
	X,X,X,X,0,0,0,0,0,0,0,0,0,0,0,0,
	X,X,X,X,X,0,0,0,0,0,0,0,0,0,0,0,
	X,X,X,X,X,X,0,0,0,0,0,0,0,0,0,0,
	X,X,X,X,X,X,X,0,0,0,0,0,0,0,0,0,
	X,X,X,X,X,X,X,X,0,0,0,0,0,0,0,0,
	X,X,X,X,X,X,X,X,X,0,0,0,0,0,0,0,
	X,X,X,X,X,X,X,0,0,0,0,0,0,0,0,0,
	0,0,0,X,X,X,X,0,0,0,0,0,0,0,0,0,
	0,0,0,X,X,X,X,X,0,0,0,0,0,0,0,0,
	0,0,0,0,X,X,X,X,0,0,0,0,0,0,0,0,
	0,0,0,0,X,X,X,X,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static byte StdOrMask[MOUSE_IMG_SIZE*MOUSE_IMG_SIZE] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,X,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,X,X,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,X,X,X,0,0,0,0,0,0,0,0,0,0,0,0,
	0,X,X,X,X,0,0,0,0,0,0,0,0,0,0,0,
	0,X,X,X,X,X,0,0,0,0,0,0,0,0,0,0,
	0,X,X,X,X,X,X,0,0,0,0,0,0,0,0,0,
	0,X,X,0,X,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,X,X,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,X,X,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,X,X,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,X,X,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
#undef X
static VGLBitmap VGLMouseStdAndMask = 
    VGLBITMAP_INITIALIZER(MEMBUF, MOUSE_IMG_SIZE, MOUSE_IMG_SIZE, StdAndMask);
static VGLBitmap VGLMouseStdOrMask = 
    VGLBITMAP_INITIALIZER(MEMBUF, MOUSE_IMG_SIZE, MOUSE_IMG_SIZE, StdOrMask);
static VGLBitmap *VGLMouseAndMask, *VGLMouseOrMask;
static byte map[MOUSE_IMG_SIZE*MOUSE_IMG_SIZE*4];
static VGLBitmap VGLMouseSave = 
    VGLBITMAP_INITIALIZER(MEMBUF, MOUSE_IMG_SIZE, MOUSE_IMG_SIZE, map);
static int VGLMouseVisible = 0;
static int VGLMouseFrozen = 0;
static int VGLMouseShown = 0;
static int VGLMouseXpos = 0;
static int VGLMouseYpos = 0;
static int VGLMouseButtons = 0;

void
VGLMousePointerShow()
{
  byte buf[MOUSE_IMG_SIZE*MOUSE_IMG_SIZE*4];
  VGLBitmap buffer =
    VGLBITMAP_INITIALIZER(MEMBUF, MOUSE_IMG_SIZE, MOUSE_IMG_SIZE, buf);
  byte crtcidx, crtcval, gdcidx, gdcval;
  int pos;

  if (!VGLMouseVisible) {
    VGLMouseVisible = 1;
    crtcidx = inb(0x3c4);
    crtcval = inb(0x3c5);
    gdcidx = inb(0x3ce);
    gdcval = inb(0x3cf);
    __VGLBitmapCopy(VGLDisplay, VGLMouseXpos, VGLMouseYpos, 
		  &VGLMouseSave, 0, 0, MOUSE_IMG_SIZE, MOUSE_IMG_SIZE);
    bcopy(VGLMouseSave.Bitmap, buffer.Bitmap, MOUSE_IMG_SIZE*MOUSE_IMG_SIZE);
    for (pos = 0; pos <  MOUSE_IMG_SIZE*MOUSE_IMG_SIZE; pos++)
      buffer.Bitmap[pos]=(buffer.Bitmap[pos]&~(VGLMouseAndMask->Bitmap[pos])) |
			   VGLMouseOrMask->Bitmap[pos];
    __VGLBitmapCopy(&buffer, 0, 0, VGLDisplay, 
		  VGLMouseXpos, VGLMouseYpos, MOUSE_IMG_SIZE, MOUSE_IMG_SIZE);
    outb(0x3c4, crtcidx);
    outb(0x3c5, crtcval);
    outb(0x3ce, gdcidx);
    outb(0x3cf, gdcval);
  }
}

void
VGLMousePointerHide()
{
  byte crtcidx, crtcval, gdcidx, gdcval;

  if (VGLMouseVisible) {
    VGLMouseVisible = 0;
    crtcidx = inb(0x3c4);
    crtcval = inb(0x3c5);
    gdcidx = inb(0x3ce);
    gdcval = inb(0x3cf);
    __VGLBitmapCopy(&VGLMouseSave, 0, 0, VGLDisplay, 
		  VGLMouseXpos, VGLMouseYpos, MOUSE_IMG_SIZE, MOUSE_IMG_SIZE);
    outb(0x3c4, crtcidx);
    outb(0x3c5, crtcval);
    outb(0x3ce, gdcidx);
    outb(0x3cf, gdcval);
  }
}

void
VGLMouseMode(int mode)
{
  if (mode == VGL_MOUSESHOW) {
    if (VGLMouseShown == VGL_MOUSEHIDE) {
      VGLMousePointerShow();
      VGLMouseShown = VGL_MOUSESHOW;
    }
  }
  else {
    if (VGLMouseShown == VGL_MOUSESHOW) {
      VGLMousePointerHide();
      VGLMouseShown = VGL_MOUSEHIDE;
    }
  }
}

void
VGLMouseAction(int dummy)	
{
  struct mouse_info mouseinfo;

  if (VGLMouseFrozen) {
    VGLMouseFrozen++;
    return;
  }
  mouseinfo.operation = MOUSE_GETINFO;
  ioctl(0, CONS_MOUSECTL, &mouseinfo);
  if (VGLMouseShown == VGL_MOUSESHOW)
    VGLMousePointerHide();
  VGLMouseXpos = mouseinfo.u.data.x;
  VGLMouseYpos = mouseinfo.u.data.y;
  VGLMouseButtons = mouseinfo.u.data.buttons;
  if (VGLMouseShown == VGL_MOUSESHOW)
    VGLMousePointerShow();
}

void
VGLMouseSetImage(VGLBitmap *AndMask, VGLBitmap *OrMask)
{
  if (VGLMouseShown == VGL_MOUSESHOW)
    VGLMousePointerHide();
  VGLMouseAndMask = AndMask;
  VGLMouseOrMask = OrMask;
  if (VGLMouseShown == VGL_MOUSESHOW)
    VGLMousePointerShow();
}

void
VGLMouseSetStdImage()
{
  if (VGLMouseShown == VGL_MOUSESHOW)
    VGLMousePointerHide();
  VGLMouseAndMask = &VGLMouseStdAndMask;
  VGLMouseOrMask = &VGLMouseStdOrMask;
  if (VGLMouseShown == VGL_MOUSESHOW)
    VGLMousePointerShow();
}

int
VGLMouseInit(int mode)
{
  struct mouse_info mouseinfo;
  int error;

  VGLMouseSetStdImage();
  mouseinfo.operation = MOUSE_MODE;
  mouseinfo.u.mode.signal = SIGUSR2;
  if ((error = ioctl(0, CONS_MOUSECTL, &mouseinfo)))
    return error;
  signal(SIGUSR2, VGLMouseAction);
  mouseinfo.operation = MOUSE_GETINFO;
  ioctl(0, CONS_MOUSECTL, &mouseinfo);
  VGLMouseXpos = mouseinfo.u.data.x;
  VGLMouseYpos = mouseinfo.u.data.y;
  VGLMouseButtons = mouseinfo.u.data.buttons;
  VGLMouseMode(mode);
  return 0;
}

int
VGLMouseStatus(int *x, int *y, char *buttons)
{
  signal(SIGUSR2, SIG_IGN);
  *x =  VGLMouseXpos;
  *y =  VGLMouseYpos;
  *buttons =  VGLMouseButtons;
  signal(SIGUSR2, VGLMouseAction);
  return VGLMouseShown;
}

int
VGLMouseFreeze(int x, int y, int width, int hight, u_long color)
{
  if (!VGLMouseFrozen) {
    VGLMouseFrozen = 1;
    if (width > 1 || hight > 1) {		/* bitmap */
      if (VGLMouseShown == 1) {
        int overlap;

        if (x > VGLMouseXpos)
          overlap = (VGLMouseXpos + MOUSE_IMG_SIZE) - x;
        else
          overlap = (x + width) - VGLMouseXpos;
        if (overlap > 0) {
          if (y > VGLMouseYpos)
            overlap = (VGLMouseYpos + MOUSE_IMG_SIZE) - y;
          else
            overlap = (y + hight) - VGLMouseYpos;
          if (overlap > 0)
            VGLMousePointerHide();
        } 
      }
    }
    else {				/* bit */
      if (VGLMouseShown &&
          x >= VGLMouseXpos && x < VGLMouseXpos + MOUSE_IMG_SIZE &&
          y >= VGLMouseYpos && y < VGLMouseYpos + MOUSE_IMG_SIZE) {
        VGLMouseSave.Bitmap[(y-VGLMouseYpos)*MOUSE_IMG_SIZE+(x-VGLMouseXpos)] =
          (color);
        if (VGLMouseAndMask->Bitmap 
          [(y-VGLMouseYpos)*MOUSE_IMG_SIZE+(x-VGLMouseXpos)]) {
          return 1;
        }   
      }       
    }
  }
  return 0;
}

void
VGLMouseUnFreeze()
{
  if (VGLMouseFrozen > 1) {
    VGLMouseFrozen = 0;
    VGLMouseAction(0);
  }
  else {
    VGLMouseFrozen = 0;
    if (VGLMouseShown == VGL_MOUSESHOW && !VGLMouseVisible)
      VGLMousePointerShow();
  }
}
