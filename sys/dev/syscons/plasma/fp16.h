/*-
 * Copyright (c) 2015 Dag-Erling Smørgrav
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
 *
 * $FreeBSD$
 */

#ifndef FP16_H_INCLUDED
#define FP16_H_INCLUDED

typedef signed long long fp16_t;

#define ItoFP16(n)	((signed long long)(n) << 16)
#define FP16toI(n)	((signed long long)(n) >> 16)

#ifndef _KERNEL
#define FP16toF(n)	((n) / 65536.0)
#endif

/* add a and b */
static inline fp16_t
fp16_add(fp16_t a, fp16_t b)
{

	return (a + b);
}

/* subtract b from a */
static inline fp16_t
fp16_sub(fp16_t a, fp16_t b)
{

	return (a - b);
}

/* multiply a by b */
static inline fp16_t
fp16_mul(fp16_t a, fp16_t b)
{

	return (a * b >> 16);
}

/* divide a by b */
static inline fp16_t
fp16_div(fp16_t a, fp16_t b)
{

	return ((a << 16) / b);
}

/* square root */
fp16_t fp16_sqrt(fp16_t);

#define FP16_2PI	 411774
#define FP16_PI		 205887
#define FP16_PI_2	 102943
#define FP16_PI_4	  51471

/* sine and cosine */
fp16_t fp16_sin(fp16_t);
fp16_t fp16_cos(fp16_t);

#endif
