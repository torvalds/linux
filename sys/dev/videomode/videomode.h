/*	$NetBSD: videomode.h,v 1.2 2010/05/04 21:17:10 macallan Exp $	*/
/*	$FreeBSD$	*/

/*
 * Copyright (c) 2001, 2002 Bang Jun-Young
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#ifndef _DEV_VIDEOMODE_H
#define _DEV_VIDEOMODE_H

struct videomode {
	int dot_clock;		/* Dot clock frequency in kHz. */
	int hdisplay;
	int hsync_start;
	int hsync_end;
	int htotal;
	int vdisplay;
	int vsync_start;
	int vsync_end;
	int vtotal;
	int flags;		/* Video mode flags; see below. */
	const char *name;
	int hskew;
};

/*
 * Video mode flags.
 */

#define VID_PHSYNC	0x0001
#define VID_NHSYNC	0x0002
#define VID_PVSYNC	0x0004
#define VID_NVSYNC	0x0008
#define VID_INTERLACE	0x0010
#define VID_DBLSCAN	0x0020
#define VID_CSYNC	0x0040
#define VID_PCSYNC	0x0080
#define VID_NCSYNC	0x0100
#define VID_HSKEW	0x0200
#define VID_BCAST	0x0400
#define VID_PIXMUX	0x1000
#define VID_DBLCLK	0x2000
#define VID_CLKDIV2	0x4000

extern const struct videomode videomode_list[];
extern const int videomode_count;

const struct videomode *pick_mode_by_dotclock(int, int, int);
const struct videomode *pick_mode_by_ref(int, int, int);
void sort_modes(struct videomode *, struct videomode **, int);

#endif /* _DEV_VIDEOMODE_H */
