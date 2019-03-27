/*	$FreeBSD$	*/
/*	$KAME: bf_locl.h,v 1.6 2001/09/10 04:03:56 itojun Exp $	*/

/* crypto/bf/bf_local.h */
/* Copyright (C) 1995-1997 Eric Young (eay@mincom.oz.au)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@mincom.oz.au).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@mincom.oz.au).
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
 *     Eric Young (eay@mincom.oz.au)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@mincom.oz.au)"
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
/* WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 * Always modify bf_locl.org since bf_locl.h is automatically generated from
 * it during SSLeay configuration.
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 */

#undef c2l
#define c2l(c,l)	(l =((BF_LONG)(*((c)++)))    , \
			 l|=((BF_LONG)(*((c)++)))<< 8L, \
			 l|=((BF_LONG)(*((c)++)))<<16L, \
			 l|=((BF_LONG)(*((c)++)))<<24L)

/* NOTE - c is not incremented as per c2l */
#undef c2ln
#define c2ln(c,l1,l2,n)	{ \
			c+=n; \
			l1=l2=0; \
			switch (n) { \
			case 8: l2 =((BF_LONG)(*(--(c))))<<24L; \
			case 7: l2|=((BF_LONG)(*(--(c))))<<16L; \
			case 6: l2|=((BF_LONG)(*(--(c))))<< 8L; \
			case 5: l2|=((BF_LONG)(*(--(c))));     \
			case 4: l1 =((BF_LONG)(*(--(c))))<<24L; \
			case 3: l1|=((BF_LONG)(*(--(c))))<<16L; \
			case 2: l1|=((BF_LONG)(*(--(c))))<< 8L; \
			case 1: l1|=((BF_LONG)(*(--(c))));     \
				} \
			}

#undef l2c
#define l2c(l,c)	(*((c)++)=(unsigned char)(((l)     )&0xff), \
			 *((c)++)=(unsigned char)(((l)>> 8L)&0xff), \
			 *((c)++)=(unsigned char)(((l)>>16L)&0xff), \
			 *((c)++)=(unsigned char)(((l)>>24L)&0xff))

/* NOTE - c is not incremented as per l2c */
#undef l2cn
#define l2cn(l1,l2,c,n)	{ \
			c+=n; \
			switch (n) { \
			case 8: *(--(c))=(unsigned char)(((l2)>>24L)&0xff); \
			case 7: *(--(c))=(unsigned char)(((l2)>>16L)&0xff); \
			case 6: *(--(c))=(unsigned char)(((l2)>> 8L)&0xff); \
			case 5: *(--(c))=(unsigned char)(((l2)     )&0xff); \
			case 4: *(--(c))=(unsigned char)(((l1)>>24L)&0xff); \
			case 3: *(--(c))=(unsigned char)(((l1)>>16L)&0xff); \
			case 2: *(--(c))=(unsigned char)(((l1)>> 8L)&0xff); \
			case 1: *(--(c))=(unsigned char)(((l1)     )&0xff); \
				} \
			}

/* NOTE - c is not incremented as per n2l */
#define n2ln(c,l1,l2,n)	{ \
			c+=n; \
			l1=l2=0; \
			switch (n) { \
			case 8: l2 =((BF_LONG)(*(--(c))))    ; \
			case 7: l2|=((BF_LONG)(*(--(c))))<< 8; \
			case 6: l2|=((BF_LONG)(*(--(c))))<<16; \
			case 5: l2|=((BF_LONG)(*(--(c))))<<24; \
			case 4: l1 =((BF_LONG)(*(--(c))))    ; \
			case 3: l1|=((BF_LONG)(*(--(c))))<< 8; \
			case 2: l1|=((BF_LONG)(*(--(c))))<<16; \
			case 1: l1|=((BF_LONG)(*(--(c))))<<24; \
				} \
			}

/* NOTE - c is not incremented as per l2n */
#define l2nn(l1,l2,c,n)	{ \
			c+=n; \
			switch (n) { \
			case 8: *(--(c))=(unsigned char)(((l2)    )&0xff); \
			case 7: *(--(c))=(unsigned char)(((l2)>> 8)&0xff); \
			case 6: *(--(c))=(unsigned char)(((l2)>>16)&0xff); \
			case 5: *(--(c))=(unsigned char)(((l2)>>24)&0xff); \
			case 4: *(--(c))=(unsigned char)(((l1)    )&0xff); \
			case 3: *(--(c))=(unsigned char)(((l1)>> 8)&0xff); \
			case 2: *(--(c))=(unsigned char)(((l1)>>16)&0xff); \
			case 1: *(--(c))=(unsigned char)(((l1)>>24)&0xff); \
				} \
			}

#undef n2l
#define n2l(c,l)        (l =((BF_LONG)(*((c)++)))<<24L, \
                         l|=((BF_LONG)(*((c)++)))<<16L, \
                         l|=((BF_LONG)(*((c)++)))<< 8L, \
                         l|=((BF_LONG)(*((c)++))))

#undef l2n
#define l2n(l,c)        (*((c)++)=(unsigned char)(((l)>>24L)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>16L)&0xff), \
                         *((c)++)=(unsigned char)(((l)>> 8L)&0xff), \
                         *((c)++)=(unsigned char)(((l)     )&0xff))

/* This is actually a big endian algorithm, the most significate byte
 * is used to lookup array 0 */

/* use BF_PTR2 for intel boxes,
 * BF_PTR for sparc and MIPS/SGI
 * use nothing for Alpha and HP.
 */
#undef	BF_PTR
#undef	BF_PTR2
#ifdef __i386__
#define	BF_PTR2
#else
#ifdef __mips__
#define	BF_PTR
#endif
#endif

#define BF_M	0x3fc
#define BF_0	22L
#define BF_1	14L
#define BF_2	 6L
#define BF_3	 2L /* left shift */

#if defined(BF_PTR2)

/* This is basically a special pentium verson */
#define BF_ENC(LL,R,S,P) \
	{ \
	BF_LONG t,u,v; \
	u=R>>BF_0; \
	v=R>>BF_1; \
	u&=BF_M; \
	v&=BF_M; \
	t=  *(BF_LONG *)((unsigned char *)&(S[  0])+u); \
	u=R>>BF_2; \
	t+= *(BF_LONG *)((unsigned char *)&(S[256])+v); \
	v=R<<BF_3; \
	u&=BF_M; \
	v&=BF_M; \
	t^= *(BF_LONG *)((unsigned char *)&(S[512])+u); \
	LL^=P; \
	t+= *(BF_LONG *)((unsigned char *)&(S[768])+v); \
	LL^=t; \
	}

#elif defined(BF_PTR)

/* This is normally very good */

#define BF_ENC(LL,R,S,P) \
	LL^=P; \
	LL^= (((*(BF_LONG *)((unsigned char *)&(S[  0])+((R>>BF_0)&BF_M))+ \
		*(BF_LONG *)((unsigned char *)&(S[256])+((R>>BF_1)&BF_M)))^ \
		*(BF_LONG *)((unsigned char *)&(S[512])+((R>>BF_2)&BF_M)))+ \
		*(BF_LONG *)((unsigned char *)&(S[768])+((R<<BF_3)&BF_M)));
#else

/* This will always work, even on 64 bit machines and strangly enough,
 * on the Alpha it is faster than the pointer versions (both 32 and 64
 * versions of BF_LONG) */

#define BF_ENC(LL,R,S,P) \
	LL^=P; \
	LL^=(((	S[        (R>>24L)      ] + \
		S[0x0100+((R>>16L)&0xff)])^ \
		S[0x0200+((R>> 8L)&0xff)])+ \
		S[0x0300+((R     )&0xff)])&0xffffffff;
#endif
