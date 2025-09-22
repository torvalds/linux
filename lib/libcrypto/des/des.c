/* $OpenBSD: des.c,v 1.9 2024/08/31 15:56:09 jsing Exp $ */
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

#include <endian.h>

#include <openssl/opensslconf.h>

#include "des_local.h"

void
DES_cbc_encrypt(const unsigned char *in, unsigned char *out, long length,
    DES_key_schedule *_schedule, DES_cblock *ivec, int enc)
{
	DES_LONG tin0, tin1;
	DES_LONG tout0, tout1, xor0, xor1;
	long l = length;
	DES_LONG tin[2];
	unsigned char *iv;

	iv = &(*ivec)[0];

	if (enc) {
		c2l(iv, tout0);
		c2l(iv, tout1);
		for (l -= 8; l >= 0; l -= 8) {
			c2l(in, tin0);
			c2l(in, tin1);
			tin0 ^= tout0;
			tin[0] = tin0;
			tin1 ^= tout1;
			tin[1] = tin1;
			DES_encrypt1((DES_LONG *)tin, _schedule, DES_ENCRYPT);
			tout0 = tin[0];
			l2c(tout0, out);
			tout1 = tin[1];
			l2c(tout1, out);
		}
		if (l != -8) {
			c2ln(in, tin0, tin1, l + 8);
			tin0 ^= tout0;
			tin[0] = tin0;
			tin1 ^= tout1;
			tin[1] = tin1;
			DES_encrypt1((DES_LONG *)tin, _schedule, DES_ENCRYPT);
			tout0 = tin[0];
			l2c(tout0, out);
			tout1 = tin[1];
			l2c(tout1, out);
		}
	} else {
		c2l(iv, xor0);
		c2l(iv, xor1);
		for (l -= 8; l >= 0; l -= 8) {
			c2l(in, tin0);
			tin[0] = tin0;
			c2l(in, tin1);
			tin[1] = tin1;
			DES_encrypt1((DES_LONG *)tin, _schedule, DES_DECRYPT);
			tout0 = tin[0] ^ xor0;
			tout1 = tin[1] ^ xor1;
			l2c(tout0, out);
			l2c(tout1, out);
			xor0 = tin0;
			xor1 = tin1;
		}
		if (l != -8) {
			c2l(in, tin0);
			tin[0] = tin0;
			c2l(in, tin1);
			tin[1] = tin1;
			DES_encrypt1((DES_LONG *)tin, _schedule, DES_DECRYPT);
			tout0 = tin[0] ^ xor0;
			tout1 = tin[1] ^ xor1;
			l2cn(tout0, tout1, out, l + 8);
		}
	}
	tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
	tin[0] = tin[1] = 0;
}
LCRYPTO_ALIAS(DES_cbc_encrypt);

/* The input and output encrypted as though 64bit cfb mode is being
 * used.  The extra state information to record how much of the
 * 64bit block we have used is contained in *num;
 */

void
DES_ede3_cfb64_encrypt(const unsigned char *in, unsigned char *out,
    long length, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3,
    DES_cblock *ivec, int *num, int enc)
{
	DES_LONG v0, v1;
	long l = length;
	int n = *num;
	DES_LONG ti[2];
	unsigned char *iv, c, cc;

	iv = &(*ivec)[0];
	if (enc) {
		while (l--) {
			if (n == 0) {
				c2l(iv, v0);
				c2l(iv, v1);

				ti[0] = v0;
				ti[1] = v1;
				DES_encrypt3(ti, ks1, ks2, ks3);
				v0 = ti[0];
				v1 = ti[1];

				iv = &(*ivec)[0];
				l2c(v0, iv);
				l2c(v1, iv);
				iv = &(*ivec)[0];
			}
			c = *(in++) ^ iv[n];
			*(out++) = c;
			iv[n] = c;
			n = (n + 1) & 0x07;
		}
	} else {
		while (l--) {
			if (n == 0) {
				c2l(iv, v0);
				c2l(iv, v1);

				ti[0] = v0;
				ti[1] = v1;
				DES_encrypt3(ti, ks1, ks2, ks3);
				v0 = ti[0];
				v1 = ti[1];

				iv = &(*ivec)[0];
				l2c(v0, iv);
				l2c(v1, iv);
				iv = &(*ivec)[0];
			}
			cc = *(in++);
			c = iv[n];
			iv[n] = cc;
			*(out++) = c ^ cc;
			n = (n + 1) & 0x07;
		}
	}
	v0 = v1 = ti[0] = ti[1] = c = cc = 0;
	*num = n;
}
LCRYPTO_ALIAS(DES_ede3_cfb64_encrypt);

/* This is compatible with the single key CFB-r for DES, even thought that's
 * not what EVP needs.
 */

void
DES_ede3_cfb_encrypt(const unsigned char *in, unsigned char *out,
    int numbits, long length, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3,
    DES_cblock *ivec, int enc)
{
	DES_LONG d0, d1, v0, v1;
	unsigned long l = length, n = ((unsigned int)numbits + 7)/8;
	int num = numbits, i;
	DES_LONG ti[2];
	unsigned char *iv;
	unsigned char ovec[16];

	if (num > 64)
		return;
	iv = &(*ivec)[0];
	c2l(iv, v0);
	c2l(iv, v1);
	if (enc) {
		while (l >= n) {
			l -= n;
			ti[0] = v0;
			ti[1] = v1;
			DES_encrypt3(ti, ks1, ks2, ks3);
			c2ln(in, d0, d1, n);
			in += n;
			d0 ^= ti[0];
			d1 ^= ti[1];
			l2cn(d0, d1, out, n);
			out += n;
			/* 30-08-94 - eay - changed because l>>32 and
			 * l<<32 are bad under gcc :-( */
			if (num == 32) {
				v0 = v1;
				v1 = d0;
			} else if (num == 64) {
				v0 = d0;
				v1 = d1;
			} else {
				iv = &ovec[0];
				l2c(v0, iv);
				l2c(v1, iv);
				l2c(d0, iv);
				l2c(d1, iv);
				/* shift ovec left most of the bits... */
				memmove(ovec, ovec + num/8,
				    8 + (num % 8 ? 1 : 0));
				/* now the remaining bits */
				if (num % 8 != 0) {
					for (i = 0; i < 8; ++i) {
						ovec[i] <<= num % 8;
						ovec[i] |= ovec[i + 1] >>
						    (8 - num % 8);
					}
				}
				iv = &ovec[0];
				c2l(iv, v0);
				c2l(iv, v1);
			}
		}
	} else {
		while (l >= n) {
			l -= n;
			ti[0] = v0;
			ti[1] = v1;
			DES_encrypt3(ti, ks1, ks2, ks3);
			c2ln(in, d0, d1, n);
			in += n;
			/* 30-08-94 - eay - changed because l>>32 and
			 * l<<32 are bad under gcc :-( */
			if (num == 32) {
				v0 = v1;
				v1 = d0;
			} else if (num == 64) {
				v0 = d0;
				v1 = d1;
			} else {
				iv = &ovec[0];
				l2c(v0, iv);
				l2c(v1, iv);
				l2c(d0, iv);
				l2c(d1, iv);
				/* shift ovec left most of the bits... */
				memmove(ovec, ovec + num/8,
				    8 + (num % 8 ? 1 : 0));
				/* now the remaining bits */
				if (num % 8 != 0) {
					for (i = 0; i < 8; ++i) {
						ovec[i] <<= num % 8;
						ovec[i] |= ovec[i + 1] >>
						    (8 - num % 8);
					}
				}
				iv = &ovec[0];
				c2l(iv, v0);
				c2l(iv, v1);
			}
			d0 ^= ti[0];
			d1 ^= ti[1];
			l2cn(d0, d1, out, n);
			out += n;
		}
	}
	iv = &(*ivec)[0];
	l2c(v0, iv);
	l2c(v1, iv);
	v0 = v1 = d0 = d1 = ti[0] = ti[1] = 0;
}
LCRYPTO_ALIAS(DES_ede3_cfb_encrypt);

/* The input and output encrypted as though 64bit cfb mode is being
 * used.  The extra state information to record how much of the
 * 64bit block we have used is contained in *num;
 */

void
DES_cfb64_encrypt(const unsigned char *in, unsigned char *out,
    long length, DES_key_schedule *schedule,
    DES_cblock *ivec, int *num, int enc)
{
	DES_LONG v0, v1;
	long l = length;
	int n = *num;
	DES_LONG ti[2];
	unsigned char *iv, c, cc;

	iv = &(*ivec)[0];
	if (enc) {
		while (l--) {
			if (n == 0) {
				c2l(iv, v0);
				ti[0] = v0;
				c2l(iv, v1);
				ti[1] = v1;
				DES_encrypt1(ti, schedule, DES_ENCRYPT);
				iv = &(*ivec)[0];
				v0 = ti[0];
				l2c(v0, iv);
				v0 = ti[1];
				l2c(v0, iv);
				iv = &(*ivec)[0];
			}
			c = *(in++) ^ iv[n];
			*(out++) = c;
			iv[n] = c;
			n = (n + 1) & 0x07;
		}
	} else {
		while (l--) {
			if (n == 0) {
				c2l(iv, v0);
				ti[0] = v0;
				c2l(iv, v1);
				ti[1] = v1;
				DES_encrypt1(ti, schedule, DES_ENCRYPT);
				iv = &(*ivec)[0];
				v0 = ti[0];
				l2c(v0, iv);
				v0 = ti[1];
				l2c(v0, iv);
				iv = &(*ivec)[0];
			}
			cc = *(in++);
			c = iv[n];
			iv[n] = cc;
			*(out++) = c ^ cc;
			n = (n + 1) & 0x07;
		}
	}
	v0 = v1 = ti[0] = ti[1] = c = cc = 0;
	*num = n;
}
LCRYPTO_ALIAS(DES_cfb64_encrypt);

/* The input and output are loaded in multiples of 8 bits.
 * What this means is that if you hame numbits=12 and length=2
 * the first 12 bits will be retrieved from the first byte and half
 * the second.  The second 12 bits will come from the 3rd and half the 4th
 * byte.
 */
/* Until Aug 1 2003 this function did not correctly implement CFB-r, so it
 * will not be compatible with any encryption prior to that date. Ben. */
void
DES_cfb_encrypt(const unsigned char *in, unsigned char *out, int numbits,
    long length, DES_key_schedule *schedule, DES_cblock *ivec,
    int enc)
{
	DES_LONG d0, d1, v0, v1;
	unsigned long l = length;
	int num = numbits/8, n = (numbits + 7)/8, i, rem = numbits % 8;
	DES_LONG ti[2];
	unsigned char *iv;
#if BYTE_ORDER != LITTLE_ENDIAN
	unsigned char ovec[16];
#else
	unsigned int sh[4];
	unsigned char *ovec = (unsigned char *)sh;
#endif

	if (numbits <= 0 || numbits > 64)
		return;
	iv = &(*ivec)[0];
	c2l(iv, v0);
	c2l(iv, v1);
	if (enc) {
		while (l >= (unsigned long)n) {
			l -= n;
			ti[0] = v0;
			ti[1] = v1;
			DES_encrypt1((DES_LONG *)ti, schedule, DES_ENCRYPT);
			c2ln(in, d0, d1, n);
			in += n;
			d0 ^= ti[0];
			d1 ^= ti[1];
			l2cn(d0, d1, out, n);
			out += n;
			/* 30-08-94 - eay - changed because l>>32 and
			 * l<<32 are bad under gcc :-( */
			if (numbits == 32) {
				v0 = v1;
				v1 = d0;
			} else if (numbits == 64) {
				v0 = d0;
				v1 = d1;
			} else {
#if BYTE_ORDER != LITTLE_ENDIAN
				iv = &ovec[0];
				l2c(v0, iv);
				l2c(v1, iv);
				l2c(d0, iv);
				l2c(d1, iv);
#else
				sh[0] = v0, sh[1] = v1, sh[2] = d0, sh[3] = d1;
#endif
				if (rem == 0)
					memmove(ovec, ovec + num, 8);
				else
					for (i = 0; i < 8; ++i)
						ovec[i] = ovec[i + num] << rem |
						    ovec[i + num + 1] >> (8 -
						    rem);
#if BYTE_ORDER == LITTLE_ENDIAN
				v0 = sh[0], v1 = sh[1];
#else
				iv = &ovec[0];
				c2l(iv, v0);
				c2l(iv, v1);
#endif
			}
		}
	} else {
		while (l >= (unsigned long)n) {
			l -= n;
			ti[0] = v0;
			ti[1] = v1;
			DES_encrypt1((DES_LONG *)ti, schedule, DES_ENCRYPT);
			c2ln(in, d0, d1, n);
			in += n;
			/* 30-08-94 - eay - changed because l>>32 and
			 * l<<32 are bad under gcc :-( */
			if (numbits == 32) {
				v0 = v1;
				v1 = d0;
			} else if (numbits == 64) {
				v0 = d0;
				v1 = d1;
			} else {
#if BYTE_ORDER != LITTLE_ENDIAN
				iv = &ovec[0];
				l2c(v0, iv);
				l2c(v1, iv);
				l2c(d0, iv);
				l2c(d1, iv);
#else
				sh[0] = v0, sh[1] = v1, sh[2] = d0, sh[3] = d1;
#endif
				if (rem == 0)
					memmove(ovec, ovec + num, 8);
				else
					for (i = 0; i < 8; ++i)
						ovec[i] = ovec[i + num] << rem |
						    ovec[i + num + 1] >> (8 -
						    rem);
#if BYTE_ORDER == LITTLE_ENDIAN
				v0 = sh[0], v1 = sh[1];
#else
				iv = &ovec[0];
				c2l(iv, v0);
				c2l(iv, v1);
#endif
			}
			d0 ^= ti[0];
			d1 ^= ti[1];
			l2cn(d0, d1, out, n);
			out += n;
		}
	}
	iv = &(*ivec)[0];
	l2c(v0, iv);
	l2c(v1, iv);
	v0 = v1 = d0 = d1 = ti[0] = ti[1] = 0;
}
LCRYPTO_ALIAS(DES_cfb_encrypt);

void
DES_ecb3_encrypt(const_DES_cblock *input, DES_cblock *output,
    DES_key_schedule *ks1, DES_key_schedule *ks2,
    DES_key_schedule *ks3,
    int enc)
{
	DES_LONG l0, l1;
	DES_LONG ll[2];
	const unsigned char *in = &(*input)[0];
	unsigned char *out = &(*output)[0];

	c2l(in, l0);
	c2l(in, l1);
	ll[0] = l0;
	ll[1] = l1;
	if (enc)
		DES_encrypt3(ll, ks1, ks2, ks3);
	else
		DES_decrypt3(ll, ks1, ks2, ks3);
	l0 = ll[0];
	l1 = ll[1];
	l2c(l0, out);
	l2c(l1, out);
}
LCRYPTO_ALIAS(DES_ecb3_encrypt);

void
DES_ecb_encrypt(const_DES_cblock *input, DES_cblock *output,
    DES_key_schedule *ks, int enc)
{
	DES_LONG l;
	DES_LONG ll[2];
	const unsigned char *in = &(*input)[0];
	unsigned char *out = &(*output)[0];

	c2l(in, l);
	ll[0] = l;
	c2l(in, l);
	ll[1] = l;
	DES_encrypt1(ll, ks, enc);
	l = ll[0];
	l2c(l, out);
	l = ll[1];
	l2c(l, out);
	l = ll[0] = ll[1] = 0;
}
LCRYPTO_ALIAS(DES_ecb_encrypt);

/*

This is an implementation of Triple DES Cipher Block Chaining with Output
Feedback Masking, by Coppersmith, Johnson and Matyas, (IBM and Certicom).

Note that there is a known attack on this by Biham and Knudsen but it takes
a lot of work:

http://www.cs.technion.ac.il/users/wwwb/cgi-bin/tr-get.cgi/1998/CS/CS0928.ps.gz

*/

#ifndef OPENSSL_NO_DESCBCM
void
DES_ede3_cbcm_encrypt(const unsigned char *in, unsigned char *out,
    long length, DES_key_schedule *ks1, DES_key_schedule *ks2,
    DES_key_schedule *ks3, DES_cblock *ivec1, DES_cblock *ivec2,
    int enc)
{
	DES_LONG tin0, tin1;
	DES_LONG tout0, tout1, xor0, xor1, m0, m1;
	long l = length;
	DES_LONG tin[2];
	unsigned char *iv1, *iv2;

	iv1 = &(*ivec1)[0];
	iv2 = &(*ivec2)[0];

	if (enc) {
		c2l(iv1, m0);
		c2l(iv1, m1);
		c2l(iv2, tout0);
		c2l(iv2, tout1);
		for (l -= 8; l >= -7; l -= 8) {
			tin[0] = m0;
			tin[1] = m1;
			DES_encrypt1(tin, ks3, 1);
			m0 = tin[0];
			m1 = tin[1];

			if (l < 0) {
				c2ln(in, tin0, tin1, l + 8);
			} else {
				c2l(in, tin0);
				c2l(in, tin1);
			}
			tin0 ^= tout0;
			tin1 ^= tout1;

			tin[0] = tin0;
			tin[1] = tin1;
			DES_encrypt1(tin, ks1, 1);
			tin[0] ^= m0;
			tin[1] ^= m1;
			DES_encrypt1(tin, ks2, 0);
			tin[0] ^= m0;
			tin[1] ^= m1;
			DES_encrypt1(tin, ks1, 1);
			tout0 = tin[0];
			tout1 = tin[1];

			l2c(tout0, out);
			l2c(tout1, out);
		}
		iv1 = &(*ivec1)[0];
		l2c(m0, iv1);
		l2c(m1, iv1);

		iv2 = &(*ivec2)[0];
		l2c(tout0, iv2);
		l2c(tout1, iv2);
	} else {
		DES_LONG t0, t1;

		c2l(iv1, m0);
		c2l(iv1, m1);
		c2l(iv2, xor0);
		c2l(iv2, xor1);
		for (l -= 8; l >= -7; l -= 8) {
			tin[0] = m0;
			tin[1] = m1;
			DES_encrypt1(tin, ks3, 1);
			m0 = tin[0];
			m1 = tin[1];

			c2l(in, tin0);
			c2l(in, tin1);

			t0 = tin0;
			t1 = tin1;

			tin[0] = tin0;
			tin[1] = tin1;
			DES_encrypt1(tin, ks1, 0);
			tin[0] ^= m0;
			tin[1] ^= m1;
			DES_encrypt1(tin, ks2, 1);
			tin[0] ^= m0;
			tin[1] ^= m1;
			DES_encrypt1(tin, ks1, 0);
			tout0 = tin[0];
			tout1 = tin[1];

			tout0 ^= xor0;
			tout1 ^= xor1;
			if (l < 0) {
				l2cn(tout0, tout1, out, l + 8);
			} else {
				l2c(tout0, out);
				l2c(tout1, out);
			}
			xor0 = t0;
			xor1 = t1;
		}

		iv1 = &(*ivec1)[0];
		l2c(m0, iv1);
		l2c(m1, iv1);

		iv2 = &(*ivec2)[0];
		l2c(xor0, iv2);
		l2c(xor1, iv2);
	}
	tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
	tin[0] = tin[1] = 0;
}
LCRYPTO_ALIAS(DES_ede3_cbcm_encrypt);
#endif

/* The input and output encrypted as though 64bit ofb mode is being
 * used.  The extra state information to record how much of the
 * 64bit block we have used is contained in *num;
 */
void
DES_ede3_ofb64_encrypt(const unsigned char *in,
    unsigned char *out, long length,
    DES_key_schedule *k1, DES_key_schedule *k2,
    DES_key_schedule *k3, DES_cblock *ivec,
    int *num)
{
	DES_LONG v0, v1;
	int n = *num;
	long l = length;
	DES_cblock d;
	char *dp;
	DES_LONG ti[2];
	unsigned char *iv;
	int save = 0;

	iv = &(*ivec)[0];
	c2l(iv, v0);
	c2l(iv, v1);
	ti[0] = v0;
	ti[1] = v1;
	dp = (char *)d;
	l2c(v0, dp);
	l2c(v1, dp);
	while (l--) {
		if (n == 0) {
			/* ti[0]=v0; */
			/* ti[1]=v1; */
			DES_encrypt3(ti, k1, k2, k3);
			v0 = ti[0];
			v1 = ti[1];

			dp = (char *)d;
			l2c(v0, dp);
			l2c(v1, dp);
			save++;
		}
		*(out++) = *(in++) ^ d[n];
		n = (n + 1) & 0x07;
	}
	if (save) {
		iv = &(*ivec)[0];
		l2c(v0, iv);
		l2c(v1, iv);
	}
	v0 = v1 = ti[0] = ti[1] = 0;
	*num = n;
}
LCRYPTO_ALIAS(DES_ede3_ofb64_encrypt);

/* The input and output encrypted as though 64bit ofb mode is being
 * used.  The extra state information to record how much of the
 * 64bit block we have used is contained in *num;
 */
void
DES_ofb64_encrypt(const unsigned char *in,
    unsigned char *out, long length,
    DES_key_schedule *schedule, DES_cblock *ivec, int *num)
{
	DES_LONG v0, v1, t;
	int n = *num;
	long l = length;
	DES_cblock d;
	unsigned char *dp;
	DES_LONG ti[2];
	unsigned char *iv;
	int save = 0;

	iv = &(*ivec)[0];
	c2l(iv, v0);
	c2l(iv, v1);
	ti[0] = v0;
	ti[1] = v1;
	dp = d;
	l2c(v0, dp);
	l2c(v1, dp);
	while (l--) {
		if (n == 0) {
			DES_encrypt1(ti, schedule, DES_ENCRYPT);
			dp = d;
			t = ti[0];
			l2c(t, dp);
			t = ti[1];
			l2c(t, dp);
			save++;
		}
		*(out++) = *(in++) ^ d[n];
		n = (n + 1) & 0x07;
	}
	if (save) {
		v0 = ti[0];
		v1 = ti[1];
		iv = &(*ivec)[0];
		l2c(v0, iv);
		l2c(v1, iv);
	}
	t = v0 = v1 = ti[0] = ti[1] = 0;
	*num = n;
}
LCRYPTO_ALIAS(DES_ofb64_encrypt);

/* The input and output are loaded in multiples of 8 bits.
 * What this means is that if you hame numbits=12 and length=2
 * the first 12 bits will be retrieved from the first byte and half
 * the second.  The second 12 bits will come from the 3rd and half the 4th
 * byte.
 */
void
DES_ofb_encrypt(const unsigned char *in, unsigned char *out, int numbits,
    long length, DES_key_schedule *schedule,
    DES_cblock *ivec)
{
	DES_LONG d0, d1, vv0, vv1, v0, v1, n = (numbits + 7)/8;
	DES_LONG mask0, mask1;
	long l = length;
	int num = numbits;
	DES_LONG ti[2];
	unsigned char *iv;

	if (num > 64)
		return;
	if (num > 32) {
		mask0 = 0xffffffffL;
		if (num >= 64)
			mask1 = mask0;
		else
			mask1 = (1L << (num - 32)) - 1;
	} else {
		if (num == 32)
			mask0 = 0xffffffffL;
		else
			mask0 = (1L << num) - 1;
		mask1 = 0x00000000L;
	}

	iv = &(*ivec)[0];
	c2l(iv, v0);
	c2l(iv, v1);
	ti[0] = v0;
	ti[1] = v1;
	while (l-- > 0) {
		ti[0] = v0;
		ti[1] = v1;
		DES_encrypt1((DES_LONG *)ti, schedule, DES_ENCRYPT);
		vv0 = ti[0];
		vv1 = ti[1];
		c2ln(in, d0, d1, n);
		in += n;
		d0 = (d0 ^ vv0) & mask0;
		d1 = (d1 ^ vv1) & mask1;
		l2cn(d0, d1, out, n);
		out += n;

		if (num == 32) {
			v0 = v1;
			v1 = vv0;
		} else if (num == 64) {
			v0 = vv0;
			v1 = vv1;
		} else if (num > 32) { /* && num != 64 */
			v0 = ((v1 >> (num - 32))|(vv0 << (64 - num))) &
			    0xffffffffL;
			v1 = ((vv0 >> (num - 32))|(vv1 << (64 - num))) &
			    0xffffffffL;
		} else /* num < 32 */ {
			v0 = ((v0 >> num)|(v1 << (32 - num))) & 0xffffffffL;
			v1 = ((v1 >> num)|(vv0 << (32 - num))) & 0xffffffffL;
		}
	}
	iv = &(*ivec)[0];
	l2c(v0, iv);
	l2c(v1, iv);
	v0 = v1 = d0 = d1 = ti[0] = ti[1] = vv0 = vv1 = 0;
}
LCRYPTO_ALIAS(DES_ofb_encrypt);

void
DES_pcbc_encrypt(const unsigned char *input, unsigned char *output,
    long length, DES_key_schedule *schedule,
    DES_cblock *ivec, int enc)
{
	DES_LONG sin0, sin1, xor0, xor1, tout0, tout1;
	DES_LONG tin[2];
	const unsigned char *in;
	unsigned char *out, *iv;

	in = input;
	out = output;
	iv = &(*ivec)[0];

	if (enc) {
		c2l(iv, xor0);
		c2l(iv, xor1);
		for (; length > 0; length -= 8) {
			if (length >= 8) {
				c2l(in, sin0);
				c2l(in, sin1);
			} else
				c2ln(in, sin0, sin1, length);
			tin[0] = sin0 ^ xor0;
			tin[1] = sin1 ^ xor1;
			DES_encrypt1((DES_LONG *)tin, schedule, DES_ENCRYPT);
			tout0 = tin[0];
			tout1 = tin[1];
			xor0 = sin0 ^ tout0;
			xor1 = sin1 ^ tout1;
			l2c(tout0, out);
			l2c(tout1, out);
		}
	} else {
		c2l(iv, xor0);
		c2l(iv, xor1);
		for (; length > 0; length -= 8) {
			c2l(in, sin0);
			c2l(in, sin1);
			tin[0] = sin0;
			tin[1] = sin1;
			DES_encrypt1((DES_LONG *)tin, schedule, DES_DECRYPT);
			tout0 = tin[0] ^ xor0;
			tout1 = tin[1] ^ xor1;
			if (length >= 8) {
				l2c(tout0, out);
				l2c(tout1, out);
			} else
				l2cn(tout0, tout1, out, length);
			xor0 = tout0 ^ sin0;
			xor1 = tout1 ^ sin1;
		}
	}
	tin[0] = tin[1] = 0;
	sin0 = sin1 = xor0 = xor1 = tout0 = tout1 = 0;
}
LCRYPTO_ALIAS(DES_pcbc_encrypt);

/* RSA's DESX */

void
DES_xcbc_encrypt(const unsigned char *in, unsigned char *out,
    long length, DES_key_schedule *schedule,
    DES_cblock *ivec, const_DES_cblock *inw,
    const_DES_cblock *outw, int enc)
{
	DES_LONG tin0, tin1;
	DES_LONG tout0, tout1, xor0, xor1;
	DES_LONG inW0, inW1, outW0, outW1;
	const unsigned char *in2;
	long l = length;
	DES_LONG tin[2];
	unsigned char *iv;

	in2 = &(*inw)[0];
	c2l(in2, inW0);
	c2l(in2, inW1);
	in2 = &(*outw)[0];
	c2l(in2, outW0);
	c2l(in2, outW1);

	iv = &(*ivec)[0];

	if (enc) {
		c2l(iv, tout0);
		c2l(iv, tout1);
		for (l -= 8; l >= 0; l -= 8) {
			c2l(in, tin0);
			c2l(in, tin1);
			tin0 ^= tout0 ^ inW0;
			tin[0] = tin0;
			tin1 ^= tout1 ^ inW1;
			tin[1] = tin1;
			DES_encrypt1(tin, schedule, DES_ENCRYPT);
			tout0 = tin[0] ^ outW0;
			l2c(tout0, out);
			tout1 = tin[1] ^ outW1;
			l2c(tout1, out);
		}
		if (l != -8) {
			c2ln(in, tin0, tin1, l + 8);
			tin0 ^= tout0 ^ inW0;
			tin[0] = tin0;
			tin1 ^= tout1 ^ inW1;
			tin[1] = tin1;
			DES_encrypt1(tin, schedule, DES_ENCRYPT);
			tout0 = tin[0] ^ outW0;
			l2c(tout0, out);
			tout1 = tin[1] ^ outW1;
			l2c(tout1, out);
		}
		iv = &(*ivec)[0];
		l2c(tout0, iv);
		l2c(tout1, iv);
	} else {
		c2l(iv, xor0);
		c2l(iv, xor1);
		for (l -= 8; l > 0; l -= 8) {
			c2l(in, tin0);
			tin[0] = tin0 ^ outW0;
			c2l(in, tin1);
			tin[1] = tin1 ^ outW1;
			DES_encrypt1(tin, schedule, DES_DECRYPT);
			tout0 = tin[0] ^ xor0 ^ inW0;
			tout1 = tin[1] ^ xor1 ^ inW1;
			l2c(tout0, out);
			l2c(tout1, out);
			xor0 = tin0;
			xor1 = tin1;
		}
		if (l != -8) {
			c2l(in, tin0);
			tin[0] = tin0 ^ outW0;
			c2l(in, tin1);
			tin[1] = tin1 ^ outW1;
			DES_encrypt1(tin, schedule, DES_DECRYPT);
			tout0 = tin[0] ^ xor0 ^ inW0;
			tout1 = tin[1] ^ xor1 ^ inW1;
			l2cn(tout0, tout1, out, l + 8);
			xor0 = tin0;
			xor1 = tin1;
		}

		iv = &(*ivec)[0];
		l2c(xor0, iv);
		l2c(xor1, iv);
	}
	tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
	inW0 = inW1 = outW0 = outW1 = 0;
	tin[0] = tin[1] = 0;
}
LCRYPTO_ALIAS(DES_xcbc_encrypt);
