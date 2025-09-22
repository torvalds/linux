/*	$OpenBSD: profile.h,v 1.4 2012/08/22 17:19:35 pascal Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)profile.h	8.1 (Berkeley) 6/11/93
 */

#ifdef __PIC__
/* Inline expansion of PICCY_SET() (see <machine/asm.h>). */
#define MCOUNT \
	__asm__(".global _mcount");\
	__asm__("_mcount:");\
	__asm__("add %o7, 8, %o1");\
	__asm__("1: rd %pc, %o2");\
	__asm__("add %o2, __mcount-1b, %o2");\
	__asm__("ld [%o2], %o2");\
	__asm__("jmpl %o2, %g0");\
	__asm__("add %i7, 8, %o0");
#else
#define MCOUNT \
	__asm__(".global _mcount");\
	__asm__("_mcount:");\
	__asm__("add %i7, 8, %o0");\
	__asm__("sethi %hi(__mcount), %o2");\
	__asm__("jmpl %o2 + %lo(__mcount), %g0");\
	__asm__("add %o7, 8, %o1");
#endif

#define	_MCOUNT_DECL	static void __mcount

#ifdef _KERNEL
/*
 * Block interrupts during mcount so that those interrupts can also be
 * counted (as soon as we get done with the current counting).  On the
 * SPARC, we just splhigh/splx as those do not recursively invoke mcount.
 */
#define	MCOUNT_ENTER	s = splhigh()
#define	MCOUNT_EXIT	splx(s)
#endif /* _KERNEL */
