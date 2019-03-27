/*	$FreeBSD$	*/
/*	$KAME: des_locl.h,v 1.7 2001/09/10 04:03:58 itojun Exp $	*/

/* crypto/des/des_locl.h */
/* Copyright (C) 1995-1997 Eric Young (eay@mincom.oz.au)
 * All rights reserved.
 *
 * This file is part of an SSL implementation written
 * by Eric Young (eay@mincom.oz.au).
 * The implementation was written so as to conform with Netscapes SSL
 * specification.  This library and applications are
 * FREE FOR COMMERCIAL AND NON-COMMERCIAL USE
 * as long as the following conditions are aheared to.
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.  If this code is used in a product,
 * Eric Young should be given attribution as the author of the parts used.
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
 *    This product includes software developed by Eric Young (eay@mincom.oz.au)
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

#include <crypto/des/des.h>

#undef DES_PTR

#ifdef __STDC__
#undef NOPROTO
#endif

#define ITERATIONS 16
#define HALF_ITERATIONS 8

/* used in des_read and des_write */
#define MAXWRITE	(1024*16)
#define BSIZE		(MAXWRITE+4)

#define c2l(c,l)	(l =((DES_LONG)(*((c)++)))    , \
			 l|=((DES_LONG)(*((c)++)))<< 8L, \
			 l|=((DES_LONG)(*((c)++)))<<16L, \
			 l|=((DES_LONG)(*((c)++)))<<24L)

/* NOTE - c is not incremented as per c2l */
#define c2ln(c,l1,l2,n)	{ \
			c+=n; \
			l1=l2=0; \
			switch (n) { \
			case 8: l2 =((DES_LONG)(*(--(c))))<<24L; \
			case 7: l2|=((DES_LONG)(*(--(c))))<<16L; \
			case 6: l2|=((DES_LONG)(*(--(c))))<< 8L; \
			case 5: l2|=((DES_LONG)(*(--(c))));     \
			case 4: l1 =((DES_LONG)(*(--(c))))<<24L; \
			case 3: l1|=((DES_LONG)(*(--(c))))<<16L; \
			case 2: l1|=((DES_LONG)(*(--(c))))<< 8L; \
			case 1: l1|=((DES_LONG)(*(--(c))));     \
				} \
			}

#define l2c(l,c)	(*((c)++)=(unsigned char)(((l)     )&0xff), \
			 *((c)++)=(unsigned char)(((l)>> 8L)&0xff), \
			 *((c)++)=(unsigned char)(((l)>>16L)&0xff), \
			 *((c)++)=(unsigned char)(((l)>>24L)&0xff))

/* replacements for htonl and ntohl since I have no idea what to do
 * when faced with machines with 8 byte longs. */
#define HDRSIZE 4

#define n2l(c,l)	(l =((DES_LONG)(*((c)++)))<<24L, \
			 l|=((DES_LONG)(*((c)++)))<<16L, \
			 l|=((DES_LONG)(*((c)++)))<< 8L, \
			 l|=((DES_LONG)(*((c)++))))

#define l2n(l,c)	(*((c)++)=(unsigned char)(((l)>>24L)&0xff), \
			 *((c)++)=(unsigned char)(((l)>>16L)&0xff), \
			 *((c)++)=(unsigned char)(((l)>> 8L)&0xff), \
			 *((c)++)=(unsigned char)(((l)     )&0xff))

/* NOTE - c is not incremented as per l2c */
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

#define	ROTATE(a,n)	(((a)>>(n))+((a)<<(32-(n))))

#define LOAD_DATA_tmp(a,b,c,d,e,f) LOAD_DATA(a,b,c,d,e,f,g)
#define LOAD_DATA(R,S,u,t,E0,E1,tmp) \
	u=R^s[S  ]; \
	t=R^s[S+1]

/* The changes to this macro may help or hinder, depending on the
 * compiler and the achitecture.  gcc2 always seems to do well :-).
 * Inspired by Dana How <how@isl.stanford.edu>
 * DO NOT use the alternative version on machines with 8 byte longs.
 * It does not seem to work on the Alpha, even when DES_LONG is 4
 * bytes, probably an issue of accessing non-word aligned objects :-( */
#ifdef DES_PTR

/* It recently occurred to me that 0^0^0^0^0^0^0 == 0, so there
 * is no reason to not xor all the sub items together.  This potentially
 * saves a register since things can be xored directly into L */

#if defined(DES_RISC1) || defined(DES_RISC2)
#ifdef DES_RISC1
#define D_ENCRYPT(LL,R,S) { \
        unsigned int u1,u2,u3; \
        LOAD_DATA(R,S,u,t,E0,E1,u1); \
        u2=(int)u>>8L; \
        u1=(int)u&0xfc; \
        u2&=0xfc; \
        t=ROTATE(t,4); \
        u>>=16L; \
        LL^= *(const DES_LONG *)(des_SP      +u1); \
        LL^= *(const DES_LONG *)(des_SP+0x200+u2); \
        u3=(int)(u>>8L); \
        u1=(int)u&0xfc; \
        u3&=0xfc; \
        LL^= *(const DES_LONG *)(des_SP+0x400+u1); \
        LL^= *(const DES_LONG *)(des_SP+0x600+u3); \
        u2=(int)t>>8L; \
        u1=(int)t&0xfc; \
        u2&=0xfc; \
        t>>=16L; \
        LL^= *(const DES_LONG *)(des_SP+0x100+u1); \
        LL^= *(const DES_LONG *)(des_SP+0x300+u2); \
        u3=(int)t>>8L; \
        u1=(int)t&0xfc; \
        u3&=0xfc; \
        LL^= *(const DES_LONG *)(des_SP+0x500+u1); \
        LL^= *(const DES_LONG *)(des_SP+0x700+u3); }
#endif /* DES_RISC1 */
#ifdef DES_RISC2
#define D_ENCRYPT(LL,R,S) { \
        unsigned int u1,u2,s1,s2; \
        LOAD_DATA(R,S,u,t,E0,E1,u1); \
        u2=(int)u>>8L; \
        u1=(int)u&0xfc; \
        u2&=0xfc; \
        t=ROTATE(t,4); \
        LL^= *(const DES_LONG *)(des_SP      +u1); \
        LL^= *(const DES_LONG *)(des_SP+0x200+u2); \
        s1=(int)(u>>16L); \
        s2=(int)(u>>24L); \
        s1&=0xfc; \
        s2&=0xfc; \
        LL^= *(const DES_LONG *)(des_SP+0x400+s1); \
        LL^= *(const DES_LONG *)(des_SP+0x600+s2); \
        u2=(int)t>>8L; \
        u1=(int)t&0xfc; \
        u2&=0xfc; \
        LL^= *(const DES_LONG *)(des_SP+0x100+u1); \
        LL^= *(const DES_LONG *)(des_SP+0x300+u2); \
        s1=(int)(t>>16L); \
        s2=(int)(t>>24L); \
        s1&=0xfc; \
        s2&=0xfc; \
        LL^= *(const DES_LONG *)(des_SP+0x400+s1); \
        LL^= *(const DES_LONG *)(des_SP+0x600+s2); \
        u2=(int)t>>8L; \
        u1=(int)t&0xfc; \
        u2&=0xfc; \
        LL^= *(const DES_LONG *)(des_SP+0x100+u1); \
        LL^= *(const DES_LONG *)(des_SP+0x300+u2); \
        s1=(int)(t>>16L); \
        s2=(int)(t>>24L); \
        s1&=0xfc; \
        s2&=0xfc; \
        LL^= *(const DES_LONG *)(des_SP+0x500+s1); \
        LL^= *(const DES_LONG *)(des_SP+0x700+s2); }
#endif /* DES_RISC2 */
#else  /* DES_RISC1 || DES_RISC2 */
#define D_ENCRYPT(LL,R,S) { \
	LOAD_DATA_tmp(R,S,u,t,E0,E1); \
	t=ROTATE(t,4); \
	LL^= \
	*(const DES_LONG *)(des_SP      +((u     )&0xfc))^ \
	*(const DES_LONG *)(des_SP+0x200+((u>> 8L)&0xfc))^ \
	*(const DES_LONG *)(des_SP+0x400+((u>>16L)&0xfc))^ \
	*(const DES_LONG *)(des_SP+0x600+((u>>24L)&0xfc))^ \
	*(const DES_LONG *)(des_SP+0x100+((t     )&0xfc))^ \
	*(const DES_LONG *)(des_SP+0x300+((t>> 8L)&0xfc))^ \
	*(const DES_LONG *)(des_SP+0x500+((t>>16L)&0xfc))^ \
	*(const DES_LONG *)(des_SP+0x700+((t>>24L)&0xfc)); }
#endif /* DES_RISC1 || DES_RISC2 */
#else /* original version */

#if defined(DES_RISC1) || defined(DES_RISC2)
#ifdef DES_RISC1
#define D_ENCRYPT(LL,R,S) {\
	unsigned int u1,u2,u3; \
	LOAD_DATA(R,S,u,t,E0,E1,u1); \
	u>>=2L; \
	t=ROTATE(t,6); \
	u2=(int)u>>8L; \
	u1=(int)u&0x3f; \
	u2&=0x3f; \
	u>>=16L; \
	LL^=des_SPtrans[0][u1]; \
	LL^=des_SPtrans[2][u2]; \
	u3=(int)u>>8L; \
	u1=(int)u&0x3f; \
	u3&=0x3f; \
	LL^=des_SPtrans[4][u1]; \
	LL^=des_SPtrans[6][u3]; \
	u2=(int)t>>8L; \
	u1=(int)t&0x3f; \
	u2&=0x3f; \
	t>>=16L; \
	LL^=des_SPtrans[1][u1]; \
	LL^=des_SPtrans[3][u2]; \
	u3=(int)t>>8L; \
	u1=(int)t&0x3f; \
	u3&=0x3f; \
	LL^=des_SPtrans[5][u1]; \
	LL^=des_SPtrans[7][u3]; }
#endif /* DES_RISC1 */
#ifdef DES_RISC2
#define D_ENCRYPT(LL,R,S) {\
	unsigned int u1,u2,s1,s2; \
	LOAD_DATA(R,S,u,t,E0,E1,u1); \
	u>>=2L; \
	t=ROTATE(t,6); \
	u2=(int)u>>8L; \
	u1=(int)u&0x3f; \
	u2&=0x3f; \
	LL^=des_SPtrans[0][u1]; \
	LL^=des_SPtrans[2][u2]; \
	s1=(int)u>>16L; \
	s2=(int)u>>24L; \
	s1&=0x3f; \
	s2&=0x3f; \
	LL^=des_SPtrans[4][s1]; \
	LL^=des_SPtrans[6][s2]; \
	u2=(int)t>>8L; \
	u1=(int)t&0x3f; \
	u2&=0x3f; \
	LL^=des_SPtrans[1][u1]; \
	LL^=des_SPtrans[3][u2]; \
	s1=(int)t>>16; \
	s2=(int)t>>24L; \
	s1&=0x3f; \
	s2&=0x3f; \
	LL^=des_SPtrans[5][s1]; \
	LL^=des_SPtrans[7][s2]; }
#endif /* DES_RISC2 */

#else /* DES_RISC1 || DES_RISC2 */

#define D_ENCRYPT(LL,R,S) {\
	LOAD_DATA_tmp(R,S,u,t,E0,E1); \
	t=ROTATE(t,4); \
	LL^=\
        	des_SPtrans[0][(u>> 2L)&0x3f]^ \
        	des_SPtrans[2][(u>>10L)&0x3f]^ \
        	des_SPtrans[4][(u>>18L)&0x3f]^ \
        	des_SPtrans[6][(u>>26L)&0x3f]^ \
        	des_SPtrans[1][(t>> 2L)&0x3f]^ \
        	des_SPtrans[3][(t>>10L)&0x3f]^ \
        	des_SPtrans[5][(t>>18L)&0x3f]^ \
        	des_SPtrans[7][(t>>26L)&0x3f]; }
#endif /* DES_RISC1 || DES_RISC2 */
#endif /* DES_PTR */

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
#define PERM_OP(a,b,t,n,m) ((t)=((((a)>>(n))^(b))&(m)),\
	(b)^=(t),\
	(a)^=((t)<<(n)))

#define IP(l,r) \
	{ \
	register DES_LONG tt; \
	PERM_OP(r,l,tt, 4,0x0f0f0f0fL); \
	PERM_OP(l,r,tt,16,0x0000ffffL); \
	PERM_OP(r,l,tt, 2,0x33333333L); \
	PERM_OP(l,r,tt, 8,0x00ff00ffL); \
	PERM_OP(r,l,tt, 1,0x55555555L); \
	}

#define FP(l,r) \
	{ \
	register DES_LONG tt; \
	PERM_OP(l,r,tt, 1,0x55555555L); \
	PERM_OP(r,l,tt, 8,0x00ff00ffL); \
	PERM_OP(l,r,tt, 2,0x33333333L); \
	PERM_OP(r,l,tt,16,0x0000ffffL); \
	PERM_OP(l,r,tt, 4,0x0f0f0f0fL); \
	}
#endif
