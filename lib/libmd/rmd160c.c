/* crypto/ripemd/rmd_dgst.c */
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

#include "rmd_locl.h"

/*
 * The assembly-language code is not position-independent, so don't
 * try to use it in a shared library.
 */
#ifdef PIC
#undef RMD160_ASM
#endif

char *RMD160_version="RIPEMD160 part of SSLeay 0.9.0b 11-Oct-1998";

#ifdef RMD160_ASM
void ripemd160_block_x86(RIPEMD160_CTX *c, const u_int32_t *p,int num);
#define ripemd160_block ripemd160_block_x86
#else
void ripemd160_block(RIPEMD160_CTX *c, const u_int32_t *p,int num);
#endif

void RIPEMD160_Init(c)
RIPEMD160_CTX *c;
	{
	c->A=RIPEMD160_A;
	c->B=RIPEMD160_B;
	c->C=RIPEMD160_C;
	c->D=RIPEMD160_D;
	c->E=RIPEMD160_E;
	c->Nl=0;
	c->Nh=0;
	c->num=0;
	}

void RIPEMD160_Update(c, in, len)
RIPEMD160_CTX *c;
const void *in;
size_t len;
	{
	u_int32_t *p;
	int sw,sc;
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

		if ((c->num+len) >= RIPEMD160_CBLOCK)
			{
			l= p[sw];
			p_c2l(data,l,sc);
			p[sw++]=l;
			for (; sw<RIPEMD160_LBLOCK; sw++)
				{
				c2l(data,l);
				p[sw]=l;
				}
			len-=(RIPEMD160_CBLOCK-c->num);

			ripemd160_block(c,p,64);
			c->num=0;
			/* drop through and do the rest */
			}
		else
			{
			int ew,ec;

			c->num+=(int)len;
			if ((sc+len) < 4) /* ugly, add char's to a word */
				{
				l= p[sw];
				p_c2l_p(data,l,sc,len);
				p[sw]=l;
				}
			else
				{
				ew=(c->num>>2);
				ec=(c->num&0x03);
				l= p[sw];
				p_c2l(data,l,sc);
				p[sw++]=l;
				for (; sw < ew; sw++)
					{ c2l(data,l); p[sw]=l; }
				if (ec)
					{
					c2l_p(data,l,ec);
					p[sw]=l;
					}
				}
			return;
			}
		}
	/* we now can process the input data in blocks of RIPEMD160_CBLOCK
	 * chars and save the leftovers to c->data. */
#if BYTE_ORDER == LITTLE_ENDIAN
	if ((((unsigned long)data)%sizeof(u_int32_t)) == 0)
		{
		sw=(int)len/RIPEMD160_CBLOCK;
		if (sw > 0)
			{
			sw*=RIPEMD160_CBLOCK;
			ripemd160_block(c,(u_int32_t *)data,sw);
			data+=sw;
			len-=sw;
			}
		}
#endif
	p=c->data;
	while (len >= RIPEMD160_CBLOCK)
		{
#if BYTE_ORDER == LITTLE_ENDIAN || BYTE_ORDER == BIG_ENDIAN
		if (p != (u_int32_t *)data)
			memcpy(p,data,RIPEMD160_CBLOCK);
		data+=RIPEMD160_CBLOCK;
#if BYTE_ORDER == BIG_ENDIAN
		for (sw=(RIPEMD160_LBLOCK/4); sw; sw--)
			{
			Endian_Reverse32(p[0]);
			Endian_Reverse32(p[1]);
			Endian_Reverse32(p[2]);
			Endian_Reverse32(p[3]);
			p+=4;
			}
#endif
#else
		for (sw=(RIPEMD160_LBLOCK/4); sw; sw--)
			{
			c2l(data,l); *(p++)=l;
			c2l(data,l); *(p++)=l;
			c2l(data,l); *(p++)=l;
			c2l(data,l); *(p++)=l; 
			} 
#endif
		p=c->data;
		ripemd160_block(c,p,64);
		len-=RIPEMD160_CBLOCK;
		}
	sc=(int)len;
	c->num=sc;
	if (sc)
		{
		sw=sc>>2;	/* words to copy */
#if BYTE_ORDER == LITTLE_ENDIAN
		p[sw]=0;
		memcpy(p,data,sc);
#else
		sc&=0x03;
		for ( ; sw; sw--)
			{ c2l(data,l); *(p++)=l; }
		c2l_p(data,l,sc);
		*p=l;
#endif
		}
	}

void RIPEMD160_Transform(c,b)
RIPEMD160_CTX *c;
unsigned char *b;
	{
	u_int32_t p[16];
#if BYTE_ORDER != LITTLE_ENDIAN
	u_int32_t *q;
	int i;
#endif

#if BYTE_ORDER == BIG_ENDIAN || BYTE_ORDER == LITTLE_ENDIAN 
	memcpy(p,b,64);
#if BYTE_ORDER == BIG_ENDIAN
	q=p;
	for (i=(RIPEMD160_LBLOCK/4); i; i--)
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
	for (i=(RIPEMD160_LBLOCK/4); i; i--)
		{
		u_int32_t l;
		c2l(b,l); *(q++)=l;
		c2l(b,l); *(q++)=l;
		c2l(b,l); *(q++)=l;
		c2l(b,l); *(q++)=l; 
		} 
#endif
	ripemd160_block(c,p,64);
	}

#ifndef RMD160_ASM

void ripemd160_block(ctx, X, num)
RIPEMD160_CTX *ctx;
const u_int32_t *X;
int num;
	{
	u_int32_t A,B,C,D,E;
	u_int32_t a,b,c,d,e;

	for (;;)
		{
		A=ctx->A; B=ctx->B; C=ctx->C; D=ctx->D; E=ctx->E;

	RIP1(A,B,C,D,E,WL00,SL00);
	RIP1(E,A,B,C,D,WL01,SL01);
	RIP1(D,E,A,B,C,WL02,SL02);
	RIP1(C,D,E,A,B,WL03,SL03);
	RIP1(B,C,D,E,A,WL04,SL04);
	RIP1(A,B,C,D,E,WL05,SL05);
	RIP1(E,A,B,C,D,WL06,SL06);
	RIP1(D,E,A,B,C,WL07,SL07);
	RIP1(C,D,E,A,B,WL08,SL08);
	RIP1(B,C,D,E,A,WL09,SL09);
	RIP1(A,B,C,D,E,WL10,SL10);
	RIP1(E,A,B,C,D,WL11,SL11);
	RIP1(D,E,A,B,C,WL12,SL12);
	RIP1(C,D,E,A,B,WL13,SL13);
	RIP1(B,C,D,E,A,WL14,SL14);
	RIP1(A,B,C,D,E,WL15,SL15);

	RIP2(E,A,B,C,D,WL16,SL16,KL1);
	RIP2(D,E,A,B,C,WL17,SL17,KL1);
	RIP2(C,D,E,A,B,WL18,SL18,KL1);
	RIP2(B,C,D,E,A,WL19,SL19,KL1);
	RIP2(A,B,C,D,E,WL20,SL20,KL1);
	RIP2(E,A,B,C,D,WL21,SL21,KL1);
	RIP2(D,E,A,B,C,WL22,SL22,KL1);
	RIP2(C,D,E,A,B,WL23,SL23,KL1);
	RIP2(B,C,D,E,A,WL24,SL24,KL1);
	RIP2(A,B,C,D,E,WL25,SL25,KL1);
	RIP2(E,A,B,C,D,WL26,SL26,KL1);
	RIP2(D,E,A,B,C,WL27,SL27,KL1);
	RIP2(C,D,E,A,B,WL28,SL28,KL1);
	RIP2(B,C,D,E,A,WL29,SL29,KL1);
	RIP2(A,B,C,D,E,WL30,SL30,KL1);
	RIP2(E,A,B,C,D,WL31,SL31,KL1);

	RIP3(D,E,A,B,C,WL32,SL32,KL2);
	RIP3(C,D,E,A,B,WL33,SL33,KL2);
	RIP3(B,C,D,E,A,WL34,SL34,KL2);
	RIP3(A,B,C,D,E,WL35,SL35,KL2);
	RIP3(E,A,B,C,D,WL36,SL36,KL2);
	RIP3(D,E,A,B,C,WL37,SL37,KL2);
	RIP3(C,D,E,A,B,WL38,SL38,KL2);
	RIP3(B,C,D,E,A,WL39,SL39,KL2);
	RIP3(A,B,C,D,E,WL40,SL40,KL2);
	RIP3(E,A,B,C,D,WL41,SL41,KL2);
	RIP3(D,E,A,B,C,WL42,SL42,KL2);
	RIP3(C,D,E,A,B,WL43,SL43,KL2);
	RIP3(B,C,D,E,A,WL44,SL44,KL2);
	RIP3(A,B,C,D,E,WL45,SL45,KL2);
	RIP3(E,A,B,C,D,WL46,SL46,KL2);
	RIP3(D,E,A,B,C,WL47,SL47,KL2);

	RIP4(C,D,E,A,B,WL48,SL48,KL3);
	RIP4(B,C,D,E,A,WL49,SL49,KL3);
	RIP4(A,B,C,D,E,WL50,SL50,KL3);
	RIP4(E,A,B,C,D,WL51,SL51,KL3);
	RIP4(D,E,A,B,C,WL52,SL52,KL3);
	RIP4(C,D,E,A,B,WL53,SL53,KL3);
	RIP4(B,C,D,E,A,WL54,SL54,KL3);
	RIP4(A,B,C,D,E,WL55,SL55,KL3);
	RIP4(E,A,B,C,D,WL56,SL56,KL3);
	RIP4(D,E,A,B,C,WL57,SL57,KL3);
	RIP4(C,D,E,A,B,WL58,SL58,KL3);
	RIP4(B,C,D,E,A,WL59,SL59,KL3);
	RIP4(A,B,C,D,E,WL60,SL60,KL3);
	RIP4(E,A,B,C,D,WL61,SL61,KL3);
	RIP4(D,E,A,B,C,WL62,SL62,KL3);
	RIP4(C,D,E,A,B,WL63,SL63,KL3);

	RIP5(B,C,D,E,A,WL64,SL64,KL4);
	RIP5(A,B,C,D,E,WL65,SL65,KL4);
	RIP5(E,A,B,C,D,WL66,SL66,KL4);
	RIP5(D,E,A,B,C,WL67,SL67,KL4);
	RIP5(C,D,E,A,B,WL68,SL68,KL4);
	RIP5(B,C,D,E,A,WL69,SL69,KL4);
	RIP5(A,B,C,D,E,WL70,SL70,KL4);
	RIP5(E,A,B,C,D,WL71,SL71,KL4);
	RIP5(D,E,A,B,C,WL72,SL72,KL4);
	RIP5(C,D,E,A,B,WL73,SL73,KL4);
	RIP5(B,C,D,E,A,WL74,SL74,KL4);
	RIP5(A,B,C,D,E,WL75,SL75,KL4);
	RIP5(E,A,B,C,D,WL76,SL76,KL4);
	RIP5(D,E,A,B,C,WL77,SL77,KL4);
	RIP5(C,D,E,A,B,WL78,SL78,KL4);
	RIP5(B,C,D,E,A,WL79,SL79,KL4);

	a=A; b=B; c=C; d=D; e=E;
	/* Do other half */
	A=ctx->A; B=ctx->B; C=ctx->C; D=ctx->D; E=ctx->E;

	RIP5(A,B,C,D,E,WR00,SR00,KR0);
	RIP5(E,A,B,C,D,WR01,SR01,KR0);
	RIP5(D,E,A,B,C,WR02,SR02,KR0);
	RIP5(C,D,E,A,B,WR03,SR03,KR0);
	RIP5(B,C,D,E,A,WR04,SR04,KR0);
	RIP5(A,B,C,D,E,WR05,SR05,KR0);
	RIP5(E,A,B,C,D,WR06,SR06,KR0);
	RIP5(D,E,A,B,C,WR07,SR07,KR0);
	RIP5(C,D,E,A,B,WR08,SR08,KR0);
	RIP5(B,C,D,E,A,WR09,SR09,KR0);
	RIP5(A,B,C,D,E,WR10,SR10,KR0);
	RIP5(E,A,B,C,D,WR11,SR11,KR0);
	RIP5(D,E,A,B,C,WR12,SR12,KR0);
	RIP5(C,D,E,A,B,WR13,SR13,KR0);
	RIP5(B,C,D,E,A,WR14,SR14,KR0);
	RIP5(A,B,C,D,E,WR15,SR15,KR0);

	RIP4(E,A,B,C,D,WR16,SR16,KR1);
	RIP4(D,E,A,B,C,WR17,SR17,KR1);
	RIP4(C,D,E,A,B,WR18,SR18,KR1);
	RIP4(B,C,D,E,A,WR19,SR19,KR1);
	RIP4(A,B,C,D,E,WR20,SR20,KR1);
	RIP4(E,A,B,C,D,WR21,SR21,KR1);
	RIP4(D,E,A,B,C,WR22,SR22,KR1);
	RIP4(C,D,E,A,B,WR23,SR23,KR1);
	RIP4(B,C,D,E,A,WR24,SR24,KR1);
	RIP4(A,B,C,D,E,WR25,SR25,KR1);
	RIP4(E,A,B,C,D,WR26,SR26,KR1);
	RIP4(D,E,A,B,C,WR27,SR27,KR1);
	RIP4(C,D,E,A,B,WR28,SR28,KR1);
	RIP4(B,C,D,E,A,WR29,SR29,KR1);
	RIP4(A,B,C,D,E,WR30,SR30,KR1);
	RIP4(E,A,B,C,D,WR31,SR31,KR1);

	RIP3(D,E,A,B,C,WR32,SR32,KR2);
	RIP3(C,D,E,A,B,WR33,SR33,KR2);
	RIP3(B,C,D,E,A,WR34,SR34,KR2);
	RIP3(A,B,C,D,E,WR35,SR35,KR2);
	RIP3(E,A,B,C,D,WR36,SR36,KR2);
	RIP3(D,E,A,B,C,WR37,SR37,KR2);
	RIP3(C,D,E,A,B,WR38,SR38,KR2);
	RIP3(B,C,D,E,A,WR39,SR39,KR2);
	RIP3(A,B,C,D,E,WR40,SR40,KR2);
	RIP3(E,A,B,C,D,WR41,SR41,KR2);
	RIP3(D,E,A,B,C,WR42,SR42,KR2);
	RIP3(C,D,E,A,B,WR43,SR43,KR2);
	RIP3(B,C,D,E,A,WR44,SR44,KR2);
	RIP3(A,B,C,D,E,WR45,SR45,KR2);
	RIP3(E,A,B,C,D,WR46,SR46,KR2);
	RIP3(D,E,A,B,C,WR47,SR47,KR2);

	RIP2(C,D,E,A,B,WR48,SR48,KR3);
	RIP2(B,C,D,E,A,WR49,SR49,KR3);
	RIP2(A,B,C,D,E,WR50,SR50,KR3);
	RIP2(E,A,B,C,D,WR51,SR51,KR3);
	RIP2(D,E,A,B,C,WR52,SR52,KR3);
	RIP2(C,D,E,A,B,WR53,SR53,KR3);
	RIP2(B,C,D,E,A,WR54,SR54,KR3);
	RIP2(A,B,C,D,E,WR55,SR55,KR3);
	RIP2(E,A,B,C,D,WR56,SR56,KR3);
	RIP2(D,E,A,B,C,WR57,SR57,KR3);
	RIP2(C,D,E,A,B,WR58,SR58,KR3);
	RIP2(B,C,D,E,A,WR59,SR59,KR3);
	RIP2(A,B,C,D,E,WR60,SR60,KR3);
	RIP2(E,A,B,C,D,WR61,SR61,KR3);
	RIP2(D,E,A,B,C,WR62,SR62,KR3);
	RIP2(C,D,E,A,B,WR63,SR63,KR3);

	RIP1(B,C,D,E,A,WR64,SR64);
	RIP1(A,B,C,D,E,WR65,SR65);
	RIP1(E,A,B,C,D,WR66,SR66);
	RIP1(D,E,A,B,C,WR67,SR67);
	RIP1(C,D,E,A,B,WR68,SR68);
	RIP1(B,C,D,E,A,WR69,SR69);
	RIP1(A,B,C,D,E,WR70,SR70);
	RIP1(E,A,B,C,D,WR71,SR71);
	RIP1(D,E,A,B,C,WR72,SR72);
	RIP1(C,D,E,A,B,WR73,SR73);
	RIP1(B,C,D,E,A,WR74,SR74);
	RIP1(A,B,C,D,E,WR75,SR75);
	RIP1(E,A,B,C,D,WR76,SR76);
	RIP1(D,E,A,B,C,WR77,SR77);
	RIP1(C,D,E,A,B,WR78,SR78);
	RIP1(B,C,D,E,A,WR79,SR79);

	D     =ctx->B+c+D;
	ctx->B=ctx->C+d+E;
	ctx->C=ctx->D+e+A;
	ctx->D=ctx->E+a+B;
	ctx->E=ctx->A+b+C;
	ctx->A=D;

	X+=16;
	num-=64;
	if (num <= 0) break;
		}
	}
#endif

void RIPEMD160_Final(md, c)
unsigned char *md;
RIPEMD160_CTX *c;
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

	/* purify often complains about the following line as an
	 * Uninitialized Memory Read.  While this can be true, the
	 * following p_c2l macro will reset l when that case is true.
	 * This is because j&0x03 contains the number of 'valid' bytes
	 * already in p[i].  If and only if j&0x03 == 0, the UMR will
	 * occur but this is also the only time p_c2l will do
	 * l= *(cp++) instead of l|= *(cp++)
	 * Many thanks to Alex Tang <altitude@cic.net> for pickup this
	 * 'potential bug' */
#ifdef PURIFY
	if ((j&0x03) == 0) p[i]=0;
#endif
	l=p[i];
	p_c2l(cp,l,j&0x03);
	p[i]=l;
	i++;
	/* i is the next 'undefined word' */
	if (c->num >= RIPEMD160_LAST_BLOCK)
		{
		for (; i<RIPEMD160_LBLOCK; i++)
			p[i]=0;
		ripemd160_block(c,p,64);
		i=0;
		}
	for (; i<(RIPEMD160_LBLOCK-2); i++)
		p[i]=0;
	p[RIPEMD160_LBLOCK-2]=c->Nl;
	p[RIPEMD160_LBLOCK-1]=c->Nh;
	ripemd160_block(c,p,64);
	cp=md;
	l=c->A; l2c(l,cp);
	l=c->B; l2c(l,cp);
	l=c->C; l2c(l,cp);
	l=c->D; l2c(l,cp);
	l=c->E; l2c(l,cp);

	/* Clear the context state */
	explicit_bzero(&c, sizeof(c));
	}

#ifdef undef
int printit(l)
unsigned long *l;
	{
	int i,ii;

	for (i=0; i<2; i++)
		{
		for (ii=0; ii<8; ii++)
			{
			fprintf(stderr,"%08lx ",l[i*8+ii]);
			}
		fprintf(stderr,"\n");
		}
	}
#endif

#ifdef WEAK_REFS
/* When building libmd, provide weak references. Note: this is not
   activated in the context of compiling these sources for internal
   use in libcrypt.
 */
#undef RIPEMD160_Init
__weak_reference(_libmd_RIPEMD160_Init, RIPEMD160_Init);
#undef RIPEMD160_Update
__weak_reference(_libmd_RIPEMD160_Update, RIPEMD160_Update);
#undef RIPEMD160_Final
__weak_reference(_libmd_RIPEMD160_Final, RIPEMD160_Final);
#undef RIPEMD160_Transform
__weak_reference(_libmd_RIPEMD160_Transform, RIPEMD160_Transform);
#undef RMD160_version
__weak_reference(_libmd_RMD160_version, RMD160_version);
#undef ripemd160_block
__weak_reference(_libmd_ripemd160_block, ripemd160_block);
#endif
