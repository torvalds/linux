/*-
 * Copyright (c) 2002, 2003 David Schultz <das@FreeBSD.ORG>
 * Copyright (c) 2014 The FreeBSD Foundation
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

union IEEEl2bits {
	long double	e;
	struct {
		unsigned long	manl	:64;
		unsigned long	manh	:48;
		unsigned int	exp	:15;
		unsigned int	sign	:1;
	} bits;
	struct {
		unsigned long	manl	:64;
		unsigned long	manh	:48;
		unsigned int	expsign	:16;
	} xbits;
};

#define	LDBL_NBIT	0
#define	LDBL_IMPLICIT_NBIT
#define	mask_nbit_l(u)	((void)0)

#define	LDBL_MANH_SIZE	20
#define	LDBL_MANL_SIZE	32

#define	LDBL_TO_ARRAY32(u, a) do {			\
	(a)[0] = (uint32_t)(u).bits.manl;		\
	(a)[1] = (uint32_t)(u).bits.manh;		\
} while(0)

/*
 * TODO: Due to compiler problem we are temporary using
 * LDBL_PREC == 53. Use code below for LDBL_PREC == 113
 */
#if 0
#define	LDBL_MANH_SIZE	48
#define	LDBL_MANL_SIZE	64

#define	LDBL_TO_ARRAY32(u, a) do {			\
	(a)[0] = (uint32_t)(u).bits.manl;		\
	(a)[1] = (uint32_t)((u).bits.manl >> 32);	\
	(a)[2] = (uint32_t)(u).bits.manh;		\
	(a)[3] = (uint32_t)((u).bits.manh >> 32);	\
} while(0)
#endif
