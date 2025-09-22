/*	$OpenBSD: maskbits.h,v 1.3 2017/11/03 06:54:06 aoyama Exp $	*/
/*	$NetBSD: maskbits.h,v 1.3 1997/03/31 07:37:28 scottr Exp $	*/

/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)maskbits.h	8.2 (Berkeley) 3/21/94
 */

#include <luna88k/dev/omrasops.h>	/* R(), W(), RR_* defines */

/* This file is derived from OpenBSD:/usr/src/sys/arch/hp300/dev/maskbits.h */

/*
 * Derived from X11R4
 */

/* the following notes use the following conventions:
SCREEN LEFT				SCREEN RIGHT
in this file and maskbits.c, left and right refer to screen coordinates,
NOT bit numbering in registers.

rasops_lmask[n]
	bits[0,n-1] = 0	bits[n,31] = 1
rasops_rmask[n] =
	bits[0,n-1] = 1	bits[n,31] = 0

maskbits(x, w, startmask, endmask, nlw)
	for a span of width w starting at position x, returns
a mask for ragged bits at start, mask for ragged bits at end,
and the number of whole longwords between the ends.

*/

#define maskbits(x, w, startmask, endmask, nlw)				\
do {									\
	startmask = rasops_lmask[(x) & 0x1f];				\
	endmask = rasops_rmask[((x) + (w)) & 0x1f];			\
	if (startmask)							\
		nlw = (((w) - (32 - ((x) & 0x1f))) >> 5);		\
	else								\
		nlw = (w) >> 5;						\
} while (0)

/*
 * On LUNA's frame buffer, MSB(bit 31) is displayed at most left hand of
 * the screen.  This is different from rasops_masks.{c,h} assumption.
 * So we use our own MBL(Move Bit Left)/MBR(Move Bit Right) macros to
 * handle display memory images.
 */
 
#define OMFB_MBL(x,y)	((y) > 31 ? 0 : (x) << (y))
#define OMFB_MBR(x,y)	((y) > 31 ? 0 : (x) >> (y))

/*
 * And, our private version of GETBITS/PUTBITS.
 * XXX: We consider only 1 bpp for now.
 */

/* Get a number of bits ( <= 32 ) from *sp and store in dw */
#define OMFB_GETBITS(sp, x, w, dw)					\
do {									\
	dw = OMFB_MBL(*(sp), (x));					\
	if (((x) + (w)) > 32)						\
		dw |= (OMFB_MBR(*(sp + 1), 32 - (x)));	 		\
} while(0);

/* Put a number of bits ( <= 32 ) from sw to *dp */
#define OMFB_PUTBITS(sw, x, w, dp)					\
do {									\
	int n = (x) + (w) - 32;						\
									\
	if (n <= 0) {							\
		n = rasops_pmask[x & 31][w & 31];			\
		*(dp) = (*(dp) & ~n) | (OMFB_MBR(sw, x) & n);		\
	} else {							\
		*(dp) = (*(dp) & rasops_rmask[x])			\
			| (OMFB_MBR(sw, x));				\
		*(dp + 1) = (*(dp + 1) & rasops_lmask[n])		\
			| (OMFB_MBL(sw, 32-(x)) & rasops_rmask[n]);	\
	}								\
} while(0);

#define getandputrop(psrc, srcbit, dstbit, width, pdst, rop)		\
do {									\
	u_int32_t _tmpdst; 						\
	if (rop == RR_CLEAR)						\
		_tmpdst = 0;						\
	else								\
		OMFB_GETBITS(psrc, srcbit, width, _tmpdst);		\
	OMFB_PUTBITS(_tmpdst, dstbit, width, pdst); 			\
} while (0)

#define getunalignedword(psrc, x, dst)					\
do {									\
	u_int32_t _tmp; 						\
	OMFB_GETBITS(psrc, x, 32, _tmp);				\
	dst = _tmp;							\
} while (0)
