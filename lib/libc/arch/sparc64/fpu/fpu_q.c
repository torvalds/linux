/*	$OpenBSD: fpu_q.c,v 1.2 2004/02/03 17:18:13 jason Exp $	*/

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

#include <sys/types.h>
#include "fpu_q.h"

long double
_Q_mul(long double a, long double b)
{
	long double c;

	_Qp_mul(&c, &a, &b);
	return (c);
}

long double
_Q_div(long double a, long double b)
{
	long double c;

	_Qp_div(&c, &a, &b);
	return (c);
}

long double
_Q_sub(long double a, long double b)
{
	long double c;

	_Qp_sub(&c, &a, &b);
	return (c);
}

long double
_Q_add(long double a, long double b)
{
	long double c;

	_Qp_add(&c, &a, &b);
	return (c);
}

long double
_Q_neg(long double a)
{
	long double z = 0, r;

	_Qp_sub(&r, &z, &a);
	return (r);
}

long double
_Q_sqrt(long double a)
{
	long double v;

	_Qp_sqrt(&v, &a);
	return (v);
}

long double
_Q_dtoq(double a)
{
	long double v;

	_Qp_dtoq(&v, a);
	return (v);
}

long double
_Q_stoq(float a)
{
	long double v;

	_Qp_stoq(&v, a);
	return (v);
}

long double
_Q_itoq(int a)
{
	long double v;

	_Qp_itoq(&v, a);
	return (v);
}

long double
_Q_utoq(unsigned int a)
{
	long double v;

	_Qp_uitoq(&v, a);
	return (v);
}

double
_Q_qtod(long double a)
{
	double v;

	v = _Qp_qtod(&a);
	return (v);
}

int
_Q_qtoi(long double a)
{
	int v;

	v = _Qp_qtoi(&a);
	return (v);
}

float
_Q_qtos(long double a)
{
	float v;

	v = _Qp_qtos(&a);
	return (v);
}

unsigned int
_Q_qtou(long double a)
{
	unsigned int v;

	v = _Qp_qtoui(&a);
	return (v);
}

int
_Q_feq(long double a, long double b)
{
	return (_Qp_feq(&a, &b));
}

int
_Q_fne(long double a, long double b)
{
	return (_Qp_fne(&a, &b));
}

int
_Q_fge(long double a, long double b)
{
	return (_Qp_fge(&a, &b));
}

int
_Q_fgt(long double a, long double b)
{
	return (_Qp_fgt(&a, &b));
}

int
_Q_flt(long double a, long double b)
{
	return (_Qp_flt(&a, &b));
}

int
_Q_fle(long double a, long double b)
{
	return (_Qp_fle(&a, &b));
}
