/*	$OpenBSD: fpu_qp.c,v 1.9 2019/10/27 21:07:13 guenther Exp $	*/

/*-
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

#include <sys/types.h>
#include <machine/fsr.h>

#include "fpu_emu.h"
#include "fpu_extern.h"

#define	_QP_OP(op) \
__dso_hidden void _Qp_ ## op(u_int *c, u_int *a, u_int *b); \
PROTO_NORMAL(_Qp_ ## op); \
void \
_Qp_ ## op(u_int *c, u_int *a, u_int *b) \
{ \
	struct fpemu fe; \
	struct fpn *r; \
	__asm volatile("stx %%fsr, [%0]" : : "r" (&fe.fe_fsr)); \
	fe.fe_f1.fp_sign = a[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, a[0], a[1], a[2], a[3]); \
	fe.fe_f2.fp_sign = b[0] >> 31; \
	fe.fe_f2.fp_sticky = 0; \
	fe.fe_f2.fp_class = __fpu_qtof(&fe.fe_f2, b[0], b[1], b[2], b[3]); \
	r = __fpu_ ## op(&fe); \
	c[0] = __fpu_ftoq(&fe, r, c); \
} \
DEF_STRONG(_Qp_ ## op); \
asm(".protected _Qp_"#op);

#define	_QP_TTOQ(qname, fname, ntype, signpos, atype, ...) \
void _Qp_ ## qname ## toq(u_int *c, ntype n); \
PROTO_NORMAL(_Qp_ ## qname ## toq); \
void \
_Qp_ ## qname ## toq(u_int *c, ntype n) \
{ \
	struct fpemu fe; \
	atype *a; \
	__asm volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	a = (atype *)&n; \
	fe.fe_f1.fp_sign = (signpos >= 0) ? a[0] >> signpos : 0; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_ ## fname ## tof(&fe.fe_f1, __VA_ARGS__); \
	c[0] = __fpu_ftoq(&fe, &fe.fe_f1, c); \
} \
DEF_STRONG(_Qp_ ## qname ## toq);

#define	_QP_QTOT4(qname, fname, type, x)		\
type _Qp_qto ## qname(u_int *c); \
PROTO_NORMAL(_Qp_qto ## qname); \
type \
_Qp_qto ## qname(u_int *c) \
{ \
	struct fpemu fe; \
	u_int *a; \
	type n; \
	__asm volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	a = (u_int *)&n; \
	fe.fe_f1.fp_sign = c[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, c[0], c[1], c[2], c[3]); \
	a[0] = __fpu_fto ## fname(&fe, &fe.fe_f1, x); \
	return (n); \
} \
DEF_STRONG(_Qp_qto ## qname);

#define	_QP_QTOT3(qname, fname, type)		\
type _Qp_qto ## qname(u_int *c); \
PROTO_NORMAL(_Qp_qto ## qname); \
type \
_Qp_qto ## qname(u_int *c) \
{ \
	struct fpemu fe; \
	u_int *a; \
	type n; \
	__asm volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	a = (u_int *)&n; \
	fe.fe_f1.fp_sign = c[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, c[0], c[1], c[2], c[3]); \
	a[0] = __fpu_fto ## fname(&fe, &fe.fe_f1); \
	return (n); \
} \
DEF_STRONG(_Qp_qto ## qname);

#define	_QP_QTOT(qname, fname, type, ...) \
type _Qp_qto ## qname(u_int *c); \
PROTO_NORMAL(_Qp_qto ## qname); \
type \
_Qp_qto ## qname(u_int *c) \
{ \
	struct fpemu fe; \
	u_int *a; \
	type n; \
	__asm volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	a = (u_int *)&n; \
	fe.fe_f1.fp_sign = c[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, c[0], c[1], c[2], c[3]); \
	a[0] = __fpu_fto ## fname(&fe, &fe.fe_f1, ## __VA_ARGS__); \
	return (n); \
} \
DEF_STRONG(_Qp_qto ## qname);

#define	FCC_EQ(fcc)	((fcc) == FSR_CC_EQ)
#define	FCC_GE(fcc)	((fcc) == FSR_CC_EQ || (fcc) == FSR_CC_GT)
#define	FCC_GT(fcc)	((fcc) == FSR_CC_GT)
#define	FCC_LE(fcc)	((fcc) == FSR_CC_EQ || (fcc) == FSR_CC_LT)
#define	FCC_LT(fcc)	((fcc) == FSR_CC_LT)
#define	FCC_NE(fcc)	((fcc) != FSR_CC_EQ)
#define	FCC_ID(fcc)	(fcc)

#define	FSR_GET_FCC0(fsr)	(((fsr) >> FSR_FCC_SHIFT) & FSR_FCC_MASK)

#define	_QP_CMP(name, cmpe, test) \
int _Qp_ ## name(u_int *a, u_int *b) ; \
PROTO_NORMAL(_Qp_ ## name); \
int \
_Qp_ ## name(u_int *a, u_int *b) \
{ \
	struct fpemu fe; \
	__asm volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :); \
	fe.fe_f1.fp_sign = a[0] >> 31; \
	fe.fe_f1.fp_sticky = 0; \
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, a[0], a[1], a[2], a[3]); \
	fe.fe_f2.fp_sign = b[0] >> 31; \
	fe.fe_f2.fp_sticky = 0; \
	fe.fe_f2.fp_class = __fpu_qtof(&fe.fe_f2, b[0], b[1], b[2], b[3]); \
	__fpu_compare(&fe, cmpe, 0); \
	return (test(FSR_GET_FCC0(fe.fe_fsr))); \
} \
DEF_STRONG(_Qp_ ## name);

void _Qp_sqrt(u_int *c, u_int *a);
PROTO_NORMAL(_Qp_sqrt);
void
_Qp_sqrt(u_int *c, u_int *a)
{
	struct fpemu fe;
	struct fpn *r;
	__asm volatile("stx %%fsr, %0" : "=m" (fe.fe_fsr) :);
	fe.fe_f1.fp_sign = a[0] >> 31;
	fe.fe_f1.fp_sticky = 0;
	fe.fe_f1.fp_class = __fpu_qtof(&fe.fe_f1, a[0], a[1], a[2], a[3]);
	r = __fpu_sqrt(&fe);
	c[0] = __fpu_ftoq(&fe, r, c);
}
DEF_STRONG(_Qp_sqrt);

_QP_OP(add)
_QP_OP(div)
_QP_OP(mul)
_QP_OP(sub)

_QP_TTOQ(d,	d,	double,	31,	u_int,	a[0], a[1])
_QP_TTOQ(i,	i,	int,	31,	u_int,	a[0])
_QP_TTOQ(s,	s,	float,	31,	u_int,	a[0])
_QP_TTOQ(x,	x,	long,	63,	u_long,	a[0])
_QP_TTOQ(ui,	ui,	u_int,	-1,	u_int,	a[0])
_QP_TTOQ(ux,	ux,	u_long,	-1,	u_long,	a[0])

_QP_QTOT4(d,	d,	double,	a)
_QP_QTOT3(i,	i,	int)
_QP_QTOT3(s,	s,	float)
_QP_QTOT4(x,	x,	long,	a)
_QP_QTOT3(ui,	i,	u_int)
_QP_QTOT4(ux,	x,	u_long,	a)

_QP_CMP(feq,	0,	FCC_EQ)
_QP_CMP(fge,	0,	FCC_GE)
_QP_CMP(fgt,	0,	FCC_GT)
_QP_CMP(fle,	0,	FCC_LE)
_QP_CMP(flt,	0,	FCC_LT)
_QP_CMP(fne,	0, 	FCC_NE)
_QP_CMP(cmp,	0, 	FCC_ID)
_QP_CMP(cmpe,	1, 	FCC_ID)
