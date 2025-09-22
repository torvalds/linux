/*	$OpenBSD: sysarch.h,v 1.9 2012/12/05 23:20:09 deraadt Exp $	*/
/*	$NetBSD: sysarch.h,v 1.8 2001/04/26 03:10:46 ross Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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

#ifndef _MACHINE_SYSARCH_H_
#define _MACHINE_SYSARCH_H_

#include <machine/ieeefp.h>

/*
 * Architecture specific syscalls (ALPHA)
 */

#define	ALPHA_FPGETMASK			0
#define	ALPHA_FPSETMASK			1
#define	ALPHA_FPSETSTICKY		2
#define	ALPHA_FPGETSTICKY		6
#define	ALPHA_GET_FP_C			7
#define	ALPHA_SET_FP_C			8

struct alpha_fp_except_args {
	fp_except mask;
};

struct alpha_fp_c_args {
	uint64_t fp_c;
};

#ifndef _KERNEL

#include <sys/cdefs.h>

__BEGIN_DECLS
int	sysarch(int, void *);
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_MACHINE_SYSARCH_H_ */
