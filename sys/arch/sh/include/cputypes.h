/*	$OpenBSD: cputypes.h,v 1.2 2008/06/26 05:42:12 ray Exp $	*/
/*	$NetBSD: cputypes.h,v 1.10 2006/01/21 00:40:36 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SH_CPUTYPES_H_
#define	_SH_CPUTYPES_H_

#ifdef _KERNEL

#define	CPU_ARCH_SH3		3
#define	CPU_ARCH_SH4		4

/* SH3 series */
#define	CPU_PRODUCT_7708	1
#define	CPU_PRODUCT_7708S	2
#define	CPU_PRODUCT_7708R	3
#define	CPU_PRODUCT_7709	4
#define	CPU_PRODUCT_7709A	5

/* SH4 series */
#define	CPU_PRODUCT_7750	6
#define	CPU_PRODUCT_7750S	7
#define	CPU_PRODUCT_7750R	8
#define	CPU_PRODUCT_7751	9
#define	CPU_PRODUCT_7751R	10


#ifndef _LOCORE
extern int cpu_arch;
extern int cpu_product;
#if defined(SH3) && defined(SH4)
#define	CPU_IS_SH3		(cpu_arch == CPU_ARCH_SH3)
#define	CPU_IS_SH4		(cpu_arch == CPU_ARCH_SH4)
#elif defined(SH3)
#define	CPU_IS_SH3		(/* CONSTCOND */1)
#define	CPU_IS_SH4		(/* CONSTCOND */0)
#elif defined(SH4)
#define	CPU_IS_SH3		(/* CONSTCOND */0)
#define	CPU_IS_SH4		(/* CONSTCOND */1)
#else
#error "define SH3 and/or SH4"
#endif
#endif /* !_LOCORE */

#endif /* _KERNEL */

#endif /* !_SH_CPUTYPES_H_ */
