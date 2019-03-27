/* crypto/sha/sha_dgst.c */
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <stdio.h>
#include <string.h>

#if 0
#include <machine/ansi.h>	/* we use the __ variants of bit-sized types */
#endif
#include <machine/endian.h>

#define  SHA_0
#undef SHA_1
#include "sha.h"
#include "sha_locl.h"

char *SHA_version="SHA part of SSLeay 0.9.0b 11-Oct-1998";

/* Implemented from SHA-0 document - The Secure Hash Algorithm
 */

#define INIT_DATA_h0 (unsigned long)0x67452301L
#define INIT_DATA_h1 (unsigned long)0xefcdab89L
#define INIT_DATA_h2 (unsigned long)0x98badcfeL
#define INIT_DATA_h3 (unsigned long)0x10325476L
#define INIT_DATA_h4 (unsigned long)0xc3d2e1f0L

#define K_00_19	0x5a827999L
#define K_20_39 0x6ed9eba1L
#define K_40_59 0x8f1bbcdcL
#define K_60_79 0xca62c1d6L

#ifndef NOPROTO
   void sha_block(SHA_CTX *c, const u_int32_t *p, int num);
#else
   void sha_block();
#endif

#define	M_c2nl 		c2nl
#define	M_p_c2nl	p_c2nl
#define	M_c2nl_p	c2nl_p
#define	M_p_c2nl_p	p_c2nl_p
#define	M_nl2c		nl2c

void SHA_Init(c)
SHA_CTX *c;
	{
	c->h0=INIT_DATA_h0;
	c->h1=INIT_DATA_h1;
	c->h2=INIT_DATA_h2;
	c->h3=INIT_DATA_h3;
	c->h4=INIT_DATA_h4;
	c->Nl=0;
	c->Nh=0;
	c->num=0;
	}

void SHA_Update(c, in, len)
SHA_CTX *c;
const void *in;
size_t len;
	{
	u_int32_t *p;
	int ew,ec,sw,sc;
	u_int32_t l;
	const unsigned char *data = in;

	if (len == 0) return;

	l=(c->Nl+(len<<3))&0xffffffffL;
	if (l < c->Nl) /* overflow */
		c->Nh++;
	c->Nh+=(len>>29);
	c->Nl=l;

	if (c->num != 0)
		{
		p=c->data;
		sw=c->num>>2;
		sc=c->num&0x03;

		if ((c->num+len) >= SHA_CBLOCK)
			{
			l= p[sw];
			M_p_c2nl(data,l,sc);
			p[sw++]=l;
			for (; sw<SHA_LBLOCK; sw++)
				{
				M_c2nl(data,l);
				p[sw]=l;
				}
			len-=(SHA_CBLOCK-c->num);

			sha_block(c,p,64);
			c->num=0;
			/* drop through and do the rest */
			}
		else
			{
			c->num+=(int)len;
			if ((sc+len) < 4) /* ugly, add char's to a word */
				{
				l= p[sw];
				M_p_c2nl_p(data,l,sc,len);
				p[sw]=l;
				}
			else
				{
				ew=(c->num>>2);
				ec=(c->num&0x03);
				l= p[sw];
				M_p_c2nl(data,l,sc);
				p[sw++]=l;
				for (; sw < ew; sw++)
					{ M_c2nl(data,l); p[sw]=l; }
				if (ec)
					{
					M_c2nl_p(data,l,ec);
					p[sw]=l;
					}
				}
			return;
			}
		}
	/* We can only do the following code for assember, the reason
	 * being that the sha_block 'C' version changes the values
	 * in the 'data' array.  The assember code avoids this and
	 * copies it to a local array.  I should be able to do this for
	 * the C version as well....
	 */
#if 1
#if BYTE_ORDER == BIG_ENDIAN || defined(SHA_ASM)
	if ((((unsigned int)data)%sizeof(u_int32_t)) == 0)
		{
		sw=len/SHA_CBLOCK;
		if (sw)
			{
			sw*=SHA_CBLOCK;
			sha_block(c,(u_int32_t *)data,sw);
			data+=sw;
			len-=sw;
			}
		}
#endif
#endif
	/* we now can process the input data in blocks of SHA_CBLOCK
	 * chars and save the leftovers to c->data. */
	p=c->data;
	while (len >= SHA_CBLOCK)
		{
#if BYTE_ORDER == BIG_ENDIAN || BYTE_ORDER == LITTLE_ENDIAN
		if (p != (u_int32_t *)data)
			memcpy(p,data,SHA_CBLOCK);
		data+=SHA_CBLOCK;
#  if BYTE_ORDER == LITTLE_ENDIAN
#    ifndef SHA_ASM /* Will not happen */
		for (sw=(SHA_LBLOCK/4); sw; sw--)
			{
			Endian_Reverse32(p[0]);
			Endian_Reverse32(p[1]);
			Endian_Reverse32(p[2]);
			Endian_Reverse32(p[3]);
			p+=4;
			}
		p=c->data;
#    endif
#  endif
#else
		for (sw=(SHA_BLOCK/4); sw; sw--)
			{
			M_c2nl(data,l); *(p++)=l;
			M_c2nl(data,l); *(p++)=l;
			M_c2nl(data,l); *(p++)=l;
			M_c2nl(data,l); *(p++)=l;
			}
		p=c->data;
#endif
		sha_block(c,p,64);
		len-=SHA_CBLOCK;
		}
	ec=(int)len;
	c->num=ec;
	ew=(ec>>2);
	ec&=0x03;

	for (sw=0; sw < ew; sw++)
		{ M_c2nl(data,l); p[sw]=l; }
	M_c2nl_p(data,l,ec);
	p[sw]=l;
	}

void SHA_Transform(c,b)
SHA_CTX *c;
unsigned char *b;
	{
	u_int32_t p[16];
#if BYTE_ORDER == LITTLE_ENDIAN
	u_int32_t *q;
	int i;
#endif

#if BYTE_ORDER == BIG_ENDIAN || BYTE_ORDER == LITTLE_ENDIAN
	memcpy(p,b,64);
#if BYTE_ORDER == LITTLE_ENDIAN
	q=p;
	for (i=(SHA_LBLOCK/4); i; i--)
		{
		Endian_Reverse32(q[0]);
		Endian_Reverse32(q[1]);
		Endian_Reverse32(q[2]);
		Endian_Reverse32(q[3]);
		q+=4;
		}
#endif
#else
	q=p;
	for (i=(SHA_LBLOCK/4); i; i--)
		{
		u_int32_t l;
		c2nl(b,l); *(q++)=l;
		c2nl(b,l); *(q++)=l;
		c2nl(b,l); *(q++)=l;
		c2nl(b,l); *(q++)=l; 
		} 
#endif
	sha_block(c,p,64);
	}

void sha_block(c, W, num)
SHA_CTX *c;
const u_int32_t *W;
int num;
	{
	u_int32_t A,B,C,D,E,T;
	u_int32_t X[16];

	A=c->h0;
	B=c->h1;
	C=c->h2;
	D=c->h3;
	E=c->h4;

	for (;;)
		{
	BODY_00_15( 0,A,B,C,D,E,T,W);
	BODY_00_15( 1,T,A,B,C,D,E,W);
	BODY_00_15( 2,E,T,A,B,C,D,W);
	BODY_00_15( 3,D,E,T,A,B,C,W);
	BODY_00_15( 4,C,D,E,T,A,B,W);
	BODY_00_15( 5,B,C,D,E,T,A,W);
	BODY_00_15( 6,A,B,C,D,E,T,W);
	BODY_00_15( 7,T,A,B,C,D,E,W);
	BODY_00_15( 8,E,T,A,B,C,D,W);
	BODY_00_15( 9,D,E,T,A,B,C,W);
	BODY_00_15(10,C,D,E,T,A,B,W);
	BODY_00_15(11,B,C,D,E,T,A,W);
	BODY_00_15(12,A,B,C,D,E,T,W);
	BODY_00_15(13,T,A,B,C,D,E,W);
	BODY_00_15(14,E,T,A,B,C,D,W);
	BODY_00_15(15,D,E,T,A,B,C,W);
	BODY_16_19(16,C,D,E,T,A,B,W,W,W,W);
	BODY_16_19(17,B,C,D,E,T,A,W,W,W,W);
	BODY_16_19(18,A,B,C,D,E,T,W,W,W,W);
	BODY_16_19(19,T,A,B,C,D,E,W,W,W,X);

	BODY_20_31(20,E,T,A,B,C,D,W,W,W,X);
	BODY_20_31(21,D,E,T,A,B,C,W,W,W,X);
	BODY_20_31(22,C,D,E,T,A,B,W,W,W,X);
	BODY_20_31(23,B,C,D,E,T,A,W,W,W,X);
	BODY_20_31(24,A,B,C,D,E,T,W,W,X,X);
	BODY_20_31(25,T,A,B,C,D,E,W,W,X,X);
	BODY_20_31(26,E,T,A,B,C,D,W,W,X,X);
	BODY_20_31(27,D,E,T,A,B,C,W,W,X,X);
	BODY_20_31(28,C,D,E,T,A,B,W,W,X,X);
	BODY_20_31(29,B,C,D,E,T,A,W,W,X,X);
	BODY_20_31(30,A,B,C,D,E,T,W,X,X,X);
	BODY_20_31(31,T,A,B,C,D,E,W,X,X,X);
	BODY_32_39(32,E,T,A,B,C,D,X);
	BODY_32_39(33,D,E,T,A,B,C,X);
	BODY_32_39(34,C,D,E,T,A,B,X);
	BODY_32_39(35,B,C,D,E,T,A,X);
	BODY_32_39(36,A,B,C,D,E,T,X);
	BODY_32_39(37,T,A,B,C,D,E,X);
	BODY_32_39(38,E,T,A,B,C,D,X);
	BODY_32_39(39,D,E,T,A,B,C,X);

	BODY_40_59(40,C,D,E,T,A,B,X);
	BODY_40_59(41,B,C,D,E,T,A,X);
	BODY_40_59(42,A,B,C,D,E,T,X);
	BODY_40_59(43,T,A,B,C,D,E,X);
	BODY_40_59(44,E,T,A,B,C,D,X);
	BODY_40_59(45,D,E,T,A,B,C,X);
	BODY_40_59(46,C,D,E,T,A,B,X);
	BODY_40_59(47,B,C,D,E,T,A,X);
	BODY_40_59(48,A,B,C,D,E,T,X);
	BODY_40_59(49,T,A,B,C,D,E,X);
	BODY_40_59(50,E,T,A,B,C,D,X);
	BODY_40_59(51,D,E,T,A,B,C,X);
	BODY_40_59(52,C,D,E,T,A,B,X);
	BODY_40_59(53,B,C,D,E,T,A,X);
	BODY_40_59(54,A,B,C,D,E,T,X);
	BODY_40_59(55,T,A,B,C,D,E,X);
	BODY_40_59(56,E,T,A,B,C,D,X);
	BODY_40_59(57,D,E,T,A,B,C,X);
	BODY_40_59(58,C,D,E,T,A,B,X);
	BODY_40_59(59,B,C,D,E,T,A,X);

	BODY_60_79(60,A,B,C,D,E,T,X);
	BODY_60_79(61,T,A,B,C,D,E,X);
	BODY_60_79(62,E,T,A,B,C,D,X);
	BODY_60_79(63,D,E,T,A,B,C,X);
	BODY_60_79(64,C,D,E,T,A,B,X);
	BODY_60_79(65,B,C,D,E,T,A,X);
	BODY_60_79(66,A,B,C,D,E,T,X);
	BODY_60_79(67,T,A,B,C,D,E,X);
	BODY_60_79(68,E,T,A,B,C,D,X);
	BODY_60_79(69,D,E,T,A,B,C,X);
	BODY_60_79(70,C,D,E,T,A,B,X);
	BODY_60_79(71,B,C,D,E,T,A,X);
	BODY_60_79(72,A,B,C,D,E,T,X);
	BODY_60_79(73,T,A,B,C,D,E,X);
	BODY_60_79(74,E,T,A,B,C,D,X);
	BODY_60_79(75,D,E,T,A,B,C,X);
	BODY_60_79(76,C,D,E,T,A,B,X);
	BODY_60_79(77,B,C,D,E,T,A,X);
	BODY_60_79(78,A,B,C,D,E,T,X);
	BODY_60_79(79,T,A,B,C,D,E,X);
	
	c->h0=(c->h0+E)&0xffffffffL; 
	c->h1=(c->h1+T)&0xffffffffL;
	c->h2=(c->h2+A)&0xffffffffL;
	c->h3=(c->h3+B)&0xffffffffL;
	c->h4=(c->h4+C)&0xffffffffL;

	num-=64;
	if (num <= 0) break;

	A=c->h0;
	B=c->h1;
	C=c->h2;
	D=c->h3;
	E=c->h4;

	W+=16;
		}
	}

void SHA_Final(md, c)
unsigned char *md;
SHA_CTX *c;
	{
	int i,j;
	u_int32_t l;
	u_int32_t *p;
	static unsigned char end[4]={0x80,0x00,0x00,0x00};
	unsigned char *cp=end;

	/* c->num should definitly have room for at least one more byte. */
	p=c->data;
	j=c->num;
	i=j>>2;
#ifdef PURIFY
	if ((j&0x03) == 0) p[i]=0;
#endif
	l=p[i];
	M_p_c2nl(cp,l,j&0x03);
	p[i]=l;
	i++;
	/* i is the next 'undefined word' */
	if (c->num >= SHA_LAST_BLOCK)
		{
		for (; i<SHA_LBLOCK; i++)
			p[i]=0;
		sha_block(c,p,64);
		i=0;
		}
	for (; i<(SHA_LBLOCK-2); i++)
		p[i]=0;
	p[SHA_LBLOCK-2]=c->Nh;
	p[SHA_LBLOCK-1]=c->Nl;
	sha_block(c,p,64);
	cp=md;
	l=c->h0; nl2c(l,cp);
	l=c->h1; nl2c(l,cp);
	l=c->h2; nl2c(l,cp);
	l=c->h3; nl2c(l,cp);
	l=c->h4; nl2c(l,cp);

	/* Clear the context state */
	explicit_bzero(&c, sizeof(c));
	}

