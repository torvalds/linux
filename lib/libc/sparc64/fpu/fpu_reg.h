/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LIBC_SPARC64_FPU_FPU_REG_H_
#define _LIBC_SPARC64_FPU_FPU_REG_H_

/*
 * These are not really of type char[]. They are arrays of functions defined
 * in fpu_reg.S; each array member loads/stores a certain fpu register of the
 * given size.
 */
extern char __fpu_ld32[];
extern char __fpu_st32[];
extern char __fpu_ld64[];
extern char __fpu_st64[];

/* Size of the functions in the arrays. */
#define	FPU_LD32_SZ	8
#define	FPU_ST32_SZ	8
#define	FPU_LD64_SZ	8
#define	FPU_ST64_SZ	8

/* Typedefs for convenient casts in the functions below. */
typedef void (fp_ldst32_fn)(u_int32_t *);
typedef void (fp_ldst64_fn)(u_int64_t *);

/*
 * These are the functions that are actually used in the fpu emulation code to
 * access the fp registers. They are usually not used more than once, so
 * caching needs not be done here.
 */
static __inline u_int32_t
__fpu_getreg(int r)
{
	u_int32_t rv;

	((fp_ldst32_fn *)&__fpu_st32[r * FPU_ST32_SZ])(&rv);
	return (rv);
}

static __inline u_int64_t
__fpu_getreg64(int r)
{
	u_int64_t rv;

	((fp_ldst64_fn *)&__fpu_st64[(r >> 1) * FPU_ST64_SZ])(&rv);
	return (rv);
}

static __inline void
__fpu_setreg(int r, u_int32_t v)
{

	((fp_ldst32_fn *)&__fpu_ld32[r * FPU_LD32_SZ])(&v);
}

static __inline void
__fpu_setreg64(int r, u_int64_t v)
{

	((fp_ldst64_fn *)&__fpu_ld64[(r >> 1) * FPU_LD64_SZ])(&v);
}

#endif /* _LIBC_SPARC64_FPU_FPU_REG_H_ */
