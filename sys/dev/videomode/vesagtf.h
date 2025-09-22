/*	$NetBSD: vesagtf.h,v 1.1 2006/05/11 01:49:53 gdamore Exp $	*/

/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Written by Garrett D'Amore for Itronix Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

#ifndef _DEV_VIDEOMODE_VESAGTF_H
#define _DEV_VIDEOMODE_VESAGTF_H

/*
 * Use VESA GTF formula to generate a monitor mode, given resolution and
 * refresh rates.
 */

struct vesagtf_params {
	unsigned	margin_ppt;	/* vertical margin size, percent * 10
					 * think parts-per-thousand */
	unsigned	min_porch;	/* minimum front porch */
	unsigned	vsync_rqd;	/* width of vsync in lines */
	unsigned	hsync_pct;	/* hsync as % of total width */
	unsigned	min_vsbp;	/* minimum vsync + back porch (usec) */
	unsigned	M;		/* blanking formula gradient */
	unsigned	C;		/* blanking formula offset */
	unsigned	K;		/* blanking formula scaling factor */
	unsigned	J;		/* blanking formula scaling factor */
};

/*
 * Default values to use for params.
 */
#define	VESAGTF_MARGIN_PPT	18	/* 1.8% */
#define	VESAGTF_MIN_PORCH	1	/* minimum front porch */
#define	VESAGTF_VSYNC_RQD	3	/* vsync width in lines */
#define	VESAGTF_HSYNC_PCT	8	/* width of hsync % of total line */
#define	VESAGTF_MIN_VSBP	550	/* min vsync + back porch (usec) */
#define	VESAGTF_M		600	/* blanking formula gradient */
#define	VESAGTF_C		40	/* blanking formula offset */
#define	VESAGTF_K		128	/* blanking formula scaling factor */
#define	VESAGTF_J		20	/* blanking formula scaling factor */

/*
 * Use VESA GTF formula to generate monitor timings.  Assumes default
 * GTF parameters, non-interlaced, and no margins.
 */
void vesagtf_mode(unsigned x, unsigned y, unsigned refresh,
    struct videomode *);

/*
 * A more complete version, in case we ever want to use alternate GTF
 * parameters.  EDID 1.3 allows for "secondary GTF parameters".
 */
void vesagtf_mode_params(unsigned x, unsigned y, unsigned refresh,
    struct vesagtf_params *, int flags, struct videomode *);

#define	VESAGTF_FLAG_ILACE	0x0001		/* use interlace */
#define	VESAGTF_FLAG_MARGINS	0x0002		/* use margins */

#endif /* _DEV_VIDEOMODE_VESAGTF_H */
