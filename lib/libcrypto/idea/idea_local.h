/* $OpenBSD: idea_local.h,v 1.2 2023/07/07 12:51:58 beck Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
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

/* The new form of this macro (check if the a*b == 0) was suggested by
 * Colin Plumb <colin@nyx10.cs.du.edu> */
/* Removal of the inner if from from Wei Dai 24/4/96 */
#define idea_mul(r,a,b,ul)						\
ul=(unsigned long)a*b;							\
if (ul != 0)								\
	{								\
	r=(ul&0xffff)-(ul>>16);						\
	r-=((r)>>16);							\
	}								\
else									\
	r=(-(int)a-b+1); /* assuming a or b is 0 and in range */ 

/*  7/12/95 - Many thanks to Rhys Weatherley <rweather@us.oracle.com>
 * for pointing out that I was assuming little endian
 * byte order for all quantities what idea
 * actually used bigendian.  No where in the spec does it mention
 * this, it is all in terms of 16 bit numbers and even the example
 * does not use byte streams for the input example :-(.
 * If you byte swap each pair of input, keys and iv, the functions
 * would produce the output as the old version :-(.
 */

/* NOTE - c is not incremented as per n2l */
#define n2ln(c,l1,l2,n)	{						\
			c+=n;						\
			l1=l2=0;					\
			switch (n) {					\
			case 8: l2 =((unsigned long)(*(--(c))))    ;	\
			case 7: l2|=((unsigned long)(*(--(c))))<< 8;	\
			case 6: l2|=((unsigned long)(*(--(c))))<<16;	\
			case 5: l2|=((unsigned long)(*(--(c))))<<24;	\
			case 4: l1 =((unsigned long)(*(--(c))))    ;	\
			case 3: l1|=((unsigned long)(*(--(c))))<< 8;	\
			case 2: l1|=((unsigned long)(*(--(c))))<<16;	\
			case 1: l1|=((unsigned long)(*(--(c))))<<24;	\
				}					\
			}

/* NOTE - c is not incremented as per l2n */
#define l2nn(l1,l2,c,n)	{						\
			c+=n;						\
			switch (n) {					\
			case 8: *(--(c))=(unsigned char)(((l2)    )&0xff);\
			case 7: *(--(c))=(unsigned char)(((l2)>> 8)&0xff);\
			case 6: *(--(c))=(unsigned char)(((l2)>>16)&0xff);\
			case 5: *(--(c))=(unsigned char)(((l2)>>24)&0xff);\
			case 4: *(--(c))=(unsigned char)(((l1)    )&0xff);\
			case 3: *(--(c))=(unsigned char)(((l1)>> 8)&0xff);\
			case 2: *(--(c))=(unsigned char)(((l1)>>16)&0xff);\
			case 1: *(--(c))=(unsigned char)(((l1)>>24)&0xff);\
				}					\
			}

#undef n2l
#define n2l(c,l)        (l =((unsigned long)(*((c)++)))<<24L,		\
                         l|=((unsigned long)(*((c)++)))<<16L,		\
                         l|=((unsigned long)(*((c)++)))<< 8L,		\
                         l|=((unsigned long)(*((c)++))))

#undef l2n
#define l2n(l,c)        (*((c)++)=(unsigned char)(((l)>>24L)&0xff),	\
                         *((c)++)=(unsigned char)(((l)>>16L)&0xff),	\
                         *((c)++)=(unsigned char)(((l)>> 8L)&0xff),	\
                         *((c)++)=(unsigned char)(((l)     )&0xff))

#undef s2n
#define s2n(l,c)	(*((c)++)=(unsigned char)(((l)     )&0xff), \
			 *((c)++)=(unsigned char)(((l)>> 8L)&0xff))

#undef n2s
#define n2s(c,l)	(l =((IDEA_INT)(*((c)++)))<< 8L, \
			 l|=((IDEA_INT)(*((c)++)))      )

#define E_IDEA(num)							\
	x1&=0xffff;							\
	idea_mul(x1,x1,*p,ul); p++;					\
	x2+= *(p++);							\
	x3+= *(p++);							\
	x4&=0xffff;							\
	idea_mul(x4,x4,*p,ul); p++;					\
	t0=(x1^x3)&0xffff;						\
	idea_mul(t0,t0,*p,ul); p++;					\
	t1=(t0+(x2^x4))&0xffff;						\
	idea_mul(t1,t1,*p,ul); p++;					\
	t0+=t1;								\
	x1^=t1;								\
	x4^=t0;								\
	ul=x2^t0; /* do the swap to x3 */				\
	x2=x3^t1;							\
	x3=ul;
