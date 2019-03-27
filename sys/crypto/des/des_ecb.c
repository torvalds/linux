/*	$KAME: des_ecb.c,v 1.6 2001/09/10 04:03:58 itojun Exp $	*/

/* crypto/des/ecb_enc.c */

/* Copyright (C) 1995-1998 Eric Young (eay@mincom.oz.au)
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <crypto/des/des_locl.h>
#include <crypto/des/spr.h>

/* char *libdes_version="libdes v 3.24 - 20-Apr-1996 - eay"; */ /* wrong */
/* char *DES_version="DES part of SSLeay 0.6.4 30-Aug-1996"; */

char *des_options(void)
        {
        static int init=1;
        static char buf[32];

        if (init)
                {
                const char *ptr,*unroll,*risc,*size;

#ifdef DES_PTR
                ptr="ptr";
#else
                ptr="idx";
#endif
#if defined(DES_RISC1) || defined(DES_RISC2)
#ifdef DES_RISC1
                risc="risc1";
#endif
#ifdef DES_RISC2
                risc="risc2";
#endif
#else
                risc="cisc";
#endif
#ifdef DES_UNROLL
                unroll="16";
#else
                unroll="4";
#endif
                if (sizeof(DES_LONG) != sizeof(long))
                        size="int";
                else
                        size="long";
                sprintf(buf,"des(%s,%s,%s,%s)",ptr,risc,unroll,size);
                init=0;
                }
        return(buf);
}
void des_ecb_encrypt(des_cblock *input, des_cblock *output, 
		     des_key_schedule ks, int enc)
{
	register DES_LONG l;
	DES_LONG ll[2];
	const unsigned char *in=&(*input)[0];
	unsigned char *out = &(*output)[0];

	c2l(in,l); ll[0]=l;
	c2l(in,l); ll[1]=l;
	des_encrypt1(ll,ks,enc);
	l=ll[0]; l2c(l,out);
	l=ll[1]; l2c(l,out);
	l=ll[0]=ll[1]=0;
}

void des_ecb3_encrypt(des_cblock *input, des_cblock *output,
             des_key_schedule ks1, des_key_schedule ks2, des_key_schedule ks3,
             int enc)
{
	register DES_LONG l0,l1;
	DES_LONG ll[2];
	const unsigned char *in = &(*input)[0];
	unsigned char *out = &(*output)[0];
 
	c2l(in,l0); 
	c2l(in,l1);
	ll[0]=l0; 
	ll[1]=l1;

	if (enc)
		des_encrypt3(ll,ks1,ks2,ks3);
	else
		des_decrypt3(ll,ks1,ks2,ks3);

	l0=ll[0];
	l1=ll[1];
	l2c(l0,out);
	l2c(l1,out);
}
