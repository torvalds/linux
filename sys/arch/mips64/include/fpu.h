/*	$OpenBSD: fpu.h,v 1.2 2012/12/04 05:00:40 deraadt Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Layout of the floating-point control/status register (FCR31)
 */

/* flush denormalized results to zero instead of causing FPCSR_C_E */
#define	FPCSR_FS	0x01000000

/* compare condition bits: one bit for MIPS I/II/III, eight bits for MIPS IV */
#define	FPCSR_CONDBIT(c)	((c) == 0 ? 23 : 24 + (c))
#define	FPCSR_CONDVAL(c)	(1U << FPCSR_CONDBIT(c))

/* cause bits */
#define	FPCSR_C_E	0x00020000	/* unimplemented operation */
#define	FPCSR_C_V	0x00010000	/* invalid operation */
#define	FPCSR_C_Z	0x00008000	/* division by zero */
#define	FPCSR_C_O	0x00004000	/* overflow */
#define	FPCSR_C_U	0x00002000	/* underflow */
#define	FPCSR_C_I	0x00001000	/* inexact */

/* enable bits */
#define	FPCSR_E_V	0x00000800	/* invalid operation */
#define	FPCSR_E_Z	0x00000400	/* division by zero */
#define	FPCSR_E_O	0x00000200	/* overflow */
#define	FPCSR_E_U	0x00000100	/* underflow */
#define	FPCSR_E_I	0x00000080	/* inexact */

/* flags bits */
#define	FPCSR_F_V	0x00000040	/* invalid operation */
#define	FPCSR_F_Z	0x00000020	/* division by zero */
#define	FPCSR_F_O	0x00000010	/* overflow */
#define	FPCSR_F_U	0x00000008	/* underflow */
#define	FPCSR_F_I	0x00000004	/* inexact */

#define	FPCSR_C_MASK	0x0003f000
#define	FPCSR_C_SHIFT		12
#define	FPCSR_E_MASK	0x00000f80
#define	FPCSR_E_SHIFT		7
#define	FPCSR_F_MASK	0x0000007c
#define	FPCSR_F_SHIFT		2
#define	FPCSR_RM_MASK	0x00000003	/* rounding mode */

#ifndef _KERNEL

/*
 * IRIX-compatible interfaces allowing userland to control the state
 * of the floating-point control/status register.  These are intended
 * to let userland control the state of the FS bit.
 */
#include <sys/cdefs.h>

__BEGIN_DECLS
int	get_fpc_csr(void);
int	set_fpc_csr(int);
__END_DECLS
#endif	/* _KERNEL */
