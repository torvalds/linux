/*	$OpenBSD: fpu_q.h,v 1.3 2016/05/08 18:41:17 guenther Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

double _Q_qtod(long double);
int _Q_qtoi(long double);
float _Q_qtos(long double);
unsigned int _Q_qtou(long double);
long double _Q_neg(long double);
long double _Q_sqrt(long double);
long double _Q_dtoq(double);
long double _Q_stoq(float);
long double _Q_itoq(int);
long double _Q_utoq(unsigned int);
long double _Q_mul(long double, long double);
long double _Q_div(long double, long double);
long double _Q_sub(long double, long double);
long double _Q_add(long double, long double);
int _Q_feq(long double, long double);
int _Q_fne(long double, long double);
int _Q_fge(long double, long double);
int _Q_fgt(long double, long double);
int _Q_flt(long double, long double);
int _Q_fle(long double, long double);

void _Qp_add(long double *, long double *, long double *);
void _Qp_div(long double *, long double *, long double *);
void _Qp_mul(long double *, long double *, long double *);
void _Qp_sub(long double *, long double *, void *);
void _Qp_dtoq(long double *, double);
void _Qp_itoq(long double *, int);
void _Qp_stoq(long double *, float);
void _Qp_xtoq(long double *, long);
void _Qp_uitoq(long double *, u_int);
void _Qp_uxtoq(long double *, u_long);
double _Qp_qtod(long double *);
long _Qp_qtox(long double *);
u_long _Qp_qtoux(long double *);
u_int _Qp_qtoui(long double *);
int _Qp_qtoi(long double *);
float _Qp_qtos(long double *);
int _Qp_feq(long double *, long double *);
int _Qp_fge(long double *, long double *);
int _Qp_fle(long double *, long double *);
int _Qp_fgt(long double *, long double *);
int _Qp_flt(long double *, long double *);
int _Qp_fne(long double *, long double *);
void _Qp_sqrt(long double *, long double *);

PROTO_NORMAL(_Qp_add);
PROTO_NORMAL(_Qp_div);
PROTO_NORMAL(_Qp_dtoq);
PROTO_NORMAL(_Qp_feq);
PROTO_NORMAL(_Qp_fge);
PROTO_NORMAL(_Qp_fgt);
PROTO_NORMAL(_Qp_fle);
PROTO_NORMAL(_Qp_flt);
PROTO_NORMAL(_Qp_fne);
PROTO_NORMAL(_Qp_itoq);
PROTO_NORMAL(_Qp_mul);
PROTO_NORMAL(_Qp_qtod);
PROTO_NORMAL(_Qp_qtoi);
PROTO_NORMAL(_Qp_qtos);
PROTO_NORMAL(_Qp_qtoui);
PROTO_NORMAL(_Qp_sqrt);
PROTO_NORMAL(_Qp_stoq);
PROTO_NORMAL(_Qp_sub);
PROTO_NORMAL(_Qp_uitoq);
