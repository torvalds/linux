/* $OpenBSD: des_local.h,v 1.6 2025/04/23 10:08:20 jsing Exp $ */
/* Copyright (C) 1995-1997 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#ifndef HEADER_DES_LOCL_H
#define HEADER_DES_LOCL_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/opensslconf.h>

#include <openssl/des.h>

__BEGIN_HIDDEN_DECLS

#define ITERATIONS 16

#define c2l(c,l)	(l =((DES_LONG)(*((c)++)))    ,			\
			 l|=((DES_LONG)(*((c)++)))<< 8L,		\
			 l|=((DES_LONG)(*((c)++)))<<16L,		\
			 l|=((DES_LONG)(*((c)++)))<<24L)

/* NOTE - c is not incremented as per c2l */
#define c2ln(c,l1,l2,n)	{						\
			c+=n;						\
			l1=l2=0;					\
			switch (n) {					\
			case 8: l2 =((DES_LONG)(*(--(c))))<<24L;	\
			case 7: l2|=((DES_LONG)(*(--(c))))<<16L;	\
			case 6: l2|=((DES_LONG)(*(--(c))))<< 8L;	\
			case 5: l2|=((DES_LONG)(*(--(c))));		\
			case 4: l1 =((DES_LONG)(*(--(c))))<<24L;	\
			case 3: l1|=((DES_LONG)(*(--(c))))<<16L;	\
			case 2: l1|=((DES_LONG)(*(--(c))))<< 8L;	\
			case 1: l1|=((DES_LONG)(*(--(c))));		\
				}					\
			}

#define l2c(l,c)	(*((c)++)=(unsigned char)(((l)     )&0xff),	\
			 *((c)++)=(unsigned char)(((l)>> 8L)&0xff),	\
			 *((c)++)=(unsigned char)(((l)>>16L)&0xff),	\
			 *((c)++)=(unsigned char)(((l)>>24L)&0xff))

/* NOTE - c is not incremented as per l2c */
#define l2cn(l1,l2,c,n)	{						\
			c+=n;						\
			switch (n) {					\
			case 8: *(--(c))=(unsigned char)(((l2)>>24L)&0xff);\
			case 7: *(--(c))=(unsigned char)(((l2)>>16L)&0xff);\
			case 6: *(--(c))=(unsigned char)(((l2)>> 8L)&0xff);\
			case 5: *(--(c))=(unsigned char)(((l2)     )&0xff);\
			case 4: *(--(c))=(unsigned char)(((l1)>>24L)&0xff);\
			case 3: *(--(c))=(unsigned char)(((l1)>>16L)&0xff);\
			case 2: *(--(c))=(unsigned char)(((l1)>> 8L)&0xff);\
			case 1: *(--(c))=(unsigned char)(((l1)     )&0xff);\
				}					\
			}

static inline uint32_t
ROTATE(uint32_t a, uint32_t n)
{
	return (a >> n) + (a << (32 - n));
}

/* Don't worry about the LOAD_DATA() stuff, that is used by
 * fcrypt() to add it's little bit to the front */

#ifdef DES_FCRYPT

#define LOAD_DATA_tmp(R,S,u,t,E0,E1) \
	{ DES_LONG tmp; LOAD_DATA(R,S,u,t,E0,E1,tmp); }

#define LOAD_DATA(R,S,u,t,E0,E1,tmp)					\
	t=R^(R>>16L);							\
	u=t&E0; t&=E1;							\
	tmp=(u<<16); u^=R^s[S  ]; u^=tmp;				\
	tmp=(t<<16); t^=R^s[S+1]; t^=tmp
#else
#define LOAD_DATA_tmp(a,b,c,d,e,f) LOAD_DATA(a,b,c,d,e,f,g)
#define LOAD_DATA(R,S,u,t,E0,E1,tmp)					\
	u=R^s[S  ];							\
	t=R^s[S+1]
#endif

#define D_ENCRYPT(LL,R,S) {						\
	LOAD_DATA_tmp(R,S,u,t,E0,E1);					\
	t=ROTATE(t,4);							\
	LL^=								\
		DES_SPtrans[0][(u>> 2L)&0x3f]^				\
		DES_SPtrans[2][(u>>10L)&0x3f]^				\
		DES_SPtrans[4][(u>>18L)&0x3f]^				\
		DES_SPtrans[6][(u>>26L)&0x3f]^				\
		DES_SPtrans[1][(t>> 2L)&0x3f]^				\
		DES_SPtrans[3][(t>>10L)&0x3f]^				\
		DES_SPtrans[5][(t>>18L)&0x3f]^				\
		DES_SPtrans[7][(t>>26L)&0x3f]; }

	/* IP and FP
	 * The problem is more of a geometric problem that random bit fiddling.
	 0  1  2  3  4  5  6  7      62 54 46 38 30 22 14  6
	 8  9 10 11 12 13 14 15      60 52 44 36 28 20 12  4
	16 17 18 19 20 21 22 23      58 50 42 34 26 18 10  2
	24 25 26 27 28 29 30 31  to  56 48 40 32 24 16  8  0

	32 33 34 35 36 37 38 39      63 55 47 39 31 23 15  7
	40 41 42 43 44 45 46 47      61 53 45 37 29 21 13  5
	48 49 50 51 52 53 54 55      59 51 43 35 27 19 11  3
	56 57 58 59 60 61 62 63      57 49 41 33 25 17  9  1

	The output has been subject to swaps of the form
	0 1 -> 3 1 but the odd and even bits have been put into
	2 3    2 0
	different words.  The main trick is to remember that
	t=((l>>size)^r)&(mask);
	r^=t;
	l^=(t<<size);
	can be used to swap and move bits between words.

	So l =  0  1  2  3  r = 16 17 18 19
	        4  5  6  7      20 21 22 23
	        8  9 10 11      24 25 26 27
	       12 13 14 15      28 29 30 31
	becomes (for size == 2 and mask == 0x3333)
	   t =   2^16  3^17 -- --   l =  0  1 16 17  r =  2  3 18 19
		 6^20  7^21 -- --        4  5 20 21       6  7 22 23
		10^24 11^25 -- --        8  9 24 25      10 11 24 25
		14^28 15^29 -- --       12 13 28 29      14 15 28 29

	Thanks for hints from Richard Outerbridge - he told me IP&FP
	could be done in 15 xor, 10 shifts and 5 ands.
	When I finally started to think of the problem in 2D
	I first got ~42 operations without xors.  When I remembered
	how to use xors :-) I got it to its final state.
	*/
#define PERM_OP(a,b,t,n,m) ((t)=((((a)>>(n))^(b))&(m)),			\
	(b)^=(t),							\
	(a)^=((t)<<(n)))

#define IP(l,r)								\
	{								\
	DES_LONG tt;							\
	PERM_OP(r,l,tt, 4,0x0f0f0f0fL);					\
	PERM_OP(l,r,tt,16,0x0000ffffL);					\
	PERM_OP(r,l,tt, 2,0x33333333L);					\
	PERM_OP(l,r,tt, 8,0x00ff00ffL);					\
	PERM_OP(r,l,tt, 1,0x55555555L);					\
	}

#define FP(l,r)								\
	{								\
	DES_LONG tt;							\
	PERM_OP(l,r,tt, 1,0x55555555L);					\
	PERM_OP(r,l,tt, 8,0x00ff00ffL);					\
	PERM_OP(l,r,tt, 2,0x33333333L);					\
	PERM_OP(r,l,tt,16,0x0000ffffL);					\
	PERM_OP(l,r,tt, 4,0x0f0f0f0fL);					\
	}

extern const DES_LONG DES_SPtrans[8][64];

__END_HIDDEN_DECLS

#endif
