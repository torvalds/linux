/*	$OpenBSD: psl.h,v 1.4 2010/04/25 18:11:36 miod Exp $	*/
/*	$NetBSD: psl.h,v 1.8 2005/12/11 12:18:58 christos Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	@(#)psl.h	5.2 (Berkeley) 1/18/91
 */

#ifndef _SH_PSL_H_
#define	_SH_PSL_H_

/*
 * SuperH Processor Status Register.
 */
#define	PSL_TBIT	0x00000001	/* T bit */
#define	PSL_SBIT	0x00000002	/* S bit */
#define	PSL_IMASK	0x000000f0	/* Interrupt Mask bit */
#define	PSL_QBIT	0x00000100	/* Q bit */
#define	PSL_MBIT	0x00000200	/* M bit */
#define	PSL_FD		0x00008000	/* FPU Disable bit */
#define	PSL_BL		0x10000000	/* Exception Block bit */
#define	PSL_RB		0x20000000	/* Register Bank bit */
#define	PSL_MD		0x40000000	/* Processor Mode bit */

#define	PSL_MBO		0x00000000	/* must be one bits */
#define	PSL_MBZ		0x8fff7c0c	/* must be zero bits */

#define	PSL_USERSET	0
#define	PSL_USERSTATIC	(PSL_BL|PSL_RB|PSL_MD|PSL_IMASK|PSL_FD|PSL_MBO|PSL_MBZ)

#define	KERNELMODE(sr)		((sr) & PSL_MD)

#ifdef _KERNEL
#ifndef _LOCORE
/* SR.IMASK */
int _cpu_intr_raise(int);
int _cpu_intr_suspend(void);
int _cpu_intr_resume(int);
/* SR.BL */
int _cpu_exception_suspend(void);
void _cpu_exception_resume(int);
#endif /* !_LOCORE */
#endif /* _KERNEL */

#endif /* !_SH_PSL_H_ */
