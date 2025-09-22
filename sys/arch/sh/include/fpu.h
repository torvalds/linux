#ifndef _SH_FPU_H_
/*	$OpenBSD: fpu.h,v 1.1 2006/11/05 18:57:20 miod Exp $	*/
/*
 * Copyright (c) 2006, Miodrag Vallat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define	_SH_FPU_H_

/*
 * SH4{,a} FPU definitions
 */

/* FPSCR bits */

#define	FPSCR_RB		0x00200000	/* register bank */
#define	FPSCR_SZ		0x00100000	/* transfer size mode */
#define	FPSCR_PR		0x00080000	/* precision mode */
#define	FPSCR_DN		0x00040000	/* denormalization mode */
#define	FPSCR_CAUSE_MASK	0x0003f000	/* exception cause mask */
#define	FPSCR_CAUSE_SHIFT	12
#define	FPSCR_ENABLE_MASK	0x00000f80	/* exception enable mask */
#define	FPSCR_ENABLE_SHIFT	7
#define	FPSCR_FLAG_MASK		0x0000007c	/* exception sticky mask */
#define	FPSCR_FLAG_SHIFT	2
#define	FPSCR_ROUNDING_MASK	0x00000003	/* rounding mask */

/* FPSCR exception bits */

#define	FPEXC_E			0x20	/* FPU Error */
#define	FPEXC_V			0x10	/* invalid operation */
#define	FPEXC_Z			0x08	/* divide by zero */
#define	FPEXC_O			0x04	/* overflow */
#define	FPEXC_U			0x02	/* underflow */
#define	FPEXC_I			0x01	/* inexact */

#endif	/* _SH_FPU_H_ */
