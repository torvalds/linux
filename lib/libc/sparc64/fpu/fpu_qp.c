/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Jake Burkholder.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <machine/fsr.h>

#include "fpu_emu.h"
#include "fpu_extern.h"

#define	_QP_OP(op) \
void _Qp_ ## op(u_int *c, u_int *a, u_int *b); \
void \
_Qp_ ## op(u_int *c, u_int *a, u_int *b) \
{ \
	struct fpemu fe; \
	struct fpn *r; \
	__asm __volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	fe.fe_cx = 0; \
	fe.fe_f1.fp_sign = a[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, a[0], a[1], a[2], a[3]); \
	fe.fe_f2.fp_sign = b[0] >> 31; \
	fe.fe_f2.fp_sticky = 0; \
	fe.fe_f2.fp_class = __fpu_qtof(&fe.fe_f2, b[0], b[1], b[2], b[3]); \
	r = __fpu_ ## op(&fe); \
	c[0] = __fpu_ftoq(&fe, r, c); \
	fe.fe_fsr |= fe.fe_cx << FSR_AEXC_SHIFT; \
	__asm __volatile("ldx %0, %%fsr" : : "m" (fe.fe_fsr)); \
}

#define	_QP_TTOQ(qname, fname, ntype, signpos, atype, ...) \
void _Qp_ ## qname ## toq(u_int *c, ntype n); \
void \
_Qp_ ## qname ## toq(u_int *c, ntype n) \
{ \
	struct fpemu fe; \
	union { atype a[2]; ntype n; } u = { .n = n }; \
	__asm __volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	fe.fe_cx = 0; \
	fe.fe_f1.fp_sign = (signpos >= 0) ? u.a[0] >> signpos : 0; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_ ## fname ## tof(&fe.fe_f1, __VA_ARGS__); \
	c[0] = __fpu_ftoq(&fe, &fe.fe_f1, c); \
	fe.fe_fsr |= fe.fe_cx << FSR_AEXC_SHIFT; \
	__asm __volatile("ldx %0, %%fsr" : : "m" (fe.fe_fsr)); \
}

#define	_QP_QTOT(qname, fname, type, ...) \
type _Qp_qto ## qname(u_int *c); \
type \
_Qp_qto ## qname(u_int *c) \
{ \
	struct fpemu fe; \
	union { u_int a; type n; } u; \
	__asm __volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	fe.fe_cx = 0; \
	fe.fe_f1.fp_sign = c[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, c[0], c[1], c[2], c[3]); \
	u.a = __fpu_fto ## fname(&fe, &fe.fe_f1, ## __VA_ARGS__); \
	fe.fe_fsr |= fe.fe_cx << FSR_AEXC_SHIFT; \
	__asm __volatile("ldx %0, %%fsr" : : "m" (fe.fe_fsr)); \
	return (u.n); \
}

#define	FCC_EQ(fcc)	((fcc) == FSR_CC_EQ)
#define	FCC_GE(fcc)	((fcc) == FSR_CC_EQ || (fcc) == FSR_CC_GT)
#define	FCC_GT(fcc)	((fcc) == FSR_CC_GT)
#define	FCC_LE(fcc)	((fcc) == FSR_CC_EQ || (fcc) == FSR_CC_LT)
#define	FCC_LT(fcc)	((fcc) == FSR_CC_LT)
#define	FCC_NE(fcc)	((fcc) != FSR_CC_EQ)
#define	FCC_ID(fcc)	(fcc)

#define	_QP_CMP(name, cmpe, test) \
int _Qp_ ## name(u_int *a, u_int *b) ; \
int \
_Qp_ ## name(u_int *a, u_int *b) \
{ \
	struct fpemu fe; \
	__asm __volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	fe.fe_cx = 0; \
	fe.fe_f1.fp_sign = a[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, a[0], a[1], a[2], a[3]); \
	fe.fe_f2.fp_sign = b[0] >> 31; \
	fe.fe_f2.fp_sticky = 0; \
	fe.fe_f2.fp_class = __fpu_qtof(&fe.fe_f2, b[0], b[1], b[2], b[3]); \
	__fpu_compare(&fe, cmpe, 0); \
	fe.fe_fsr |= fe.fe_cx << FSR_AEXC_SHIFT; \
	__asm __volatile("ldx %0, %%fsr" : : "m" (fe.fe_fsr)); \
	return (test(FSR_GET_FCC0(fe.fe_fsr))); \
}

void _Qp_sqrt(u_int *c, u_int *a);
void
_Qp_sqrt(u_int *c, u_int *a)
{
	struct fpemu fe;
	struct fpn *r;
	__asm __volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :);
	fe.fe_cx = 0;
	fe.fe_f1.fp_sign = a[0] >> 31;
	fe.fe_f1.fp_sticky = 0;
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, a[0], a[1], a[2], a[3]);
	r = __fpu_sqrt(&fe);
	c[0] = __fpu_ftoq(&fe, r, c);
	fe.fe_fsr |= fe.fe_cx << FSR_AEXC_SHIFT;
	__asm __volatile("ldx %0, %%fsr" : : "m" (fe.fe_fsr));
}

_QP_OP(add)
_QP_OP(div)
_QP_OP(mul)
_QP_OP(sub)

_QP_TTOQ(d,	d,	double,	31, u_int,  u.a[0], u.a[1])
_QP_TTOQ(i,	i,	int,	31, u_int,  u.a[0])
_QP_TTOQ(s,	s,	float,	31, u_int,  u.a[0])
_QP_TTOQ(x,	x,	long,	63, u_long, u.a[0])
_QP_TTOQ(ui,	i,	u_int,	-1, u_int,  u.a[0])
_QP_TTOQ(ux,	x,	u_long,	-1, u_long, u.a[0])

_QP_QTOT(d,	d,	double,	&u.a)
_QP_QTOT(i,	i,	int)
_QP_QTOT(s,	s,	float)
_QP_QTOT(x,	x,	long,	&u.a)
_QP_QTOT(ui,	i,	u_int)
_QP_QTOT(ux,	x,	u_long,	&u.a)

_QP_CMP(feq,	0,	FCC_EQ)
_QP_CMP(fge,	1,	FCC_GE)
_QP_CMP(fgt,	1,	FCC_GT)
_QP_CMP(fle,	1,	FCC_LE)
_QP_CMP(flt,	1,	FCC_LT)
_QP_CMP(fne,	0, 	FCC_NE)
_QP_CMP(cmp,	0, 	FCC_ID)
_QP_CMP(cmpe,	1, 	FCC_ID)
