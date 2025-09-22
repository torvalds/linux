/* $OpenBSD: rc2_local.h,v 1.3 2024/03/29 05:03:48 jsing Exp $ */
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

#undef c2l
#define c2l(c,l)	(l =((unsigned long)(*((c)++)))    ,		\
			 l|=((unsigned long)(*((c)++)))<< 8L,		\
			 l|=((unsigned long)(*((c)++)))<<16L,		\
			 l|=((unsigned long)(*((c)++)))<<24L)

/* NOTE - c is not incremented as per c2l */
#undef c2ln
#define c2ln(c,l1,l2,n)	{						\
			c+=n;						\
			l1=l2=0;					\
			switch (n) {					\
			case 8: l2 =((unsigned long)(*(--(c))))<<24L;	\
			case 7: l2|=((unsigned long)(*(--(c))))<<16L;	\
			case 6: l2|=((unsigned long)(*(--(c))))<< 8L;	\
			case 5: l2|=((unsigned long)(*(--(c))));	\
			case 4: l1 =((unsigned long)(*(--(c))))<<24L;	\
			case 3: l1|=((unsigned long)(*(--(c))))<<16L;	\
			case 2: l1|=((unsigned long)(*(--(c))))<< 8L;	\
			case 1: l1|=((unsigned long)(*(--(c))));	\
				}					\
			}

#undef l2c
#define l2c(l,c)	(*((c)++)=(unsigned char)(((l)     )&0xff),	\
			 *((c)++)=(unsigned char)(((l)>> 8L)&0xff),	\
			 *((c)++)=(unsigned char)(((l)>>16L)&0xff),	\
			 *((c)++)=(unsigned char)(((l)>>24L)&0xff))

/* NOTE - c is not incremented as per l2c */
#undef l2cn
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

#define C_RC2(n)							\
	t=(x0+(x1& ~x3)+(x2&x3)+ *(p0++))&0xffff;			\
	x0=(t<<1)|(t>>15);						\
	t=(x1+(x2& ~x0)+(x3&x0)+ *(p0++))&0xffff;			\
	x1=(t<<2)|(t>>14);						\
	t=(x2+(x3& ~x1)+(x0&x1)+ *(p0++))&0xffff;			\
	x2=(t<<3)|(t>>13);						\
	t=(x3+(x0& ~x2)+(x1&x2)+ *(p0++))&0xffff;			\
	x3=(t<<5)|(t>>11);
