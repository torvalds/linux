/*	$OpenBSD: fpu.h,v 1.2 2014/07/01 20:26:09 miod Exp $	*/

/*
 * Copyright (c) 2007, 2014, Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_M88K_FPU_H_
#define	_M88K_FPU_H_

/* FPECR bits */
#define	FPECR_FIOV	0x80
#define	FPECR_FUNIMP	0x40
#define	FPECR_FPRV	0x20
#define	FPECR_FROP	0x10
#define	FPECR_FDVZ	0x08
#define	FPECR_FUNF	0x04
#define	FPECR_FOVF	0x02
#define	FPECR_FINX	0x01

/* FPSR and FPCR exception bits */
#define	FPSR_EFINV	0x10
#define	FPSR_EFDVZ	0x08
#define	FPSR_EFUNF	0x04
#define	FPSR_EFOVF	0x02
#define	FPSR_EFINX	0x01

/* FPIT bits */
#define	FPIT_DBL	0x00000400

/* FPRH bits and masks */
#define	FPRH_SIGN	0x80000000
#define	FPRH_ADDONE	0x02000000
#define	FPRH_MANTH_MASK	0x001fffff
#define	FPRH_MANTH_BITS	21

#endif	/* _M88K_FPU_H_ */
