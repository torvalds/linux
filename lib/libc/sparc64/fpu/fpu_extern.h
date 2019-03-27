/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1995 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 *
 *	$NetBSD: fpu_extern.h,v 1.4 2000/08/03 18:32:08 eeh Exp $
 * $FreeBSD$
 */

#ifndef _SPARC64_FPU_FPU_EXTERN_H_
#define _SPARC64_FPU_FPU_EXTERN_H_

struct utrapframe;
struct fpemu;
struct fpn;

/* fpu.c */
int __fpu_exception(struct utrapframe *tf);

/* fpu_add.c */
struct fpn *__fpu_add(struct fpemu *);

/* fpu_compare.c */
void __fpu_compare(struct fpemu *, int, int);

/* fpu_div.c */
struct fpn *__fpu_div(struct fpemu *);

/* fpu_explode.c */
int __fpu_itof(struct fpn *, u_int);
int __fpu_xtof(struct fpn *, u_int64_t);
int __fpu_stof(struct fpn *, u_int);
int __fpu_dtof(struct fpn *, u_int, u_int);
int __fpu_qtof(struct fpn *, u_int, u_int, u_int, u_int);
void __fpu_explode(struct fpemu *, struct fpn *, int, int);

/* fpu_implode.c */
u_int __fpu_ftoi(struct fpemu *, struct fpn *);
u_int __fpu_ftox(struct fpemu *, struct fpn *, u_int *);
u_int __fpu_ftos(struct fpemu *, struct fpn *);
u_int __fpu_ftod(struct fpemu *, struct fpn *, u_int *);
u_int __fpu_ftoq(struct fpemu *, struct fpn *, u_int *);
void __fpu_implode(struct fpemu *, struct fpn *, int, u_int *);

/* fpu_mul.c */
struct fpn *__fpu_mul(struct fpemu *);

/* fpu_sqrt.c */
struct fpn *__fpu_sqrt(struct fpemu *);

/* fpu_subr.c */
/*
 * Shift a number right some number of bits, taking care of round/sticky.
 * Note that the result is probably not a well-formed number (it will lack
 * the normal 1-bit mant[0]&FP_1).
 */
int __fpu_shr(register struct fpn *, register int);
void __fpu_norm(register struct fpn *);
/* Build a new Quiet NaN (sign=0, frac=all 1's). */
struct fpn *__fpu_newnan(register struct fpemu *);

#endif /* !_SPARC64_FPU_FPU_EXTERN_H_ */
