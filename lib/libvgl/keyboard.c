/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Søren Schmidt
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
#include <termios.h>
#include <sys/time.h>
#include <sys/fbio.h>
#include <sys/kbio.h>
#include "vgl.h"

static struct termios VGLKeyboardTty;
static int VGLKeyboardMode = -1;

int
VGLKeyboardInit(int mode)
{
  static struct termios term;

  ioctl(0, KDGKBMODE, &VGLKeyboardMode);
  tcgetattr(0, &VGLKeyboardTty);

  term = VGLKeyboardTty;
  cfmakeraw(&term);
  term.c_iflag = IGNPAR | IGNBRK;
  term.c_oflag = OPOST | ONLCR;
  term.c_cflag = CREAD | CS8;
  term.c_lflag &= ~(ICANON | ECHO | ISIG);
  term.c_cc[VTIME] = 0;
  term.c_cc[VMIN] = 0;
  cfsetispeed(&term, 9600);
  cfsetospeed(&term, 9600);
  tcsetattr(0, TCSANOW | TCSAFLUSH, &term);

  switch (mode) {
  case VGL_RAWKEYS:
    ioctl(0, KDSKBMODE, K_RAW);
    break;
  case VGL_CODEKEYS:
    ioctl(0, KDSKBMODE, K_CODE);
    break;
  case VGL_XLATEKEYS:
    ioctl(0, KDSKBMODE, K_XLATE);
    break;
  }
  return 0;
}

void
VGLKeyboardEnd()
{
  if (VGLKeyboardMode != -1) {
    ioctl(0, KDSKBMODE, VGLKeyboardMode);
    tcsetattr(0, TCSANOW | TCSAFLUSH, &VGLKeyboardTty);
  }
}

int
VGLKeyboardGetCh()
{
  unsigned char ch = 0;

  read (0, &ch, 1);
  return (int)ch;
}
