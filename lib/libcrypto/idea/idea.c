/* $OpenBSD: idea.c,v 1.1 2024/03/29 05:23:50 jsing Exp $ */
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
 * OUT OF THE USE OF THIS  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <openssl/idea.h>
#include "idea_local.h"

void
idea_cbc_encrypt(const unsigned char *in, unsigned char *out, long length,
    IDEA_KEY_SCHEDULE *ks, unsigned char *iv, int encrypt)
{
	unsigned long tin0, tin1;
	unsigned long tout0, tout1, xor0, xor1;
	long l = length;
	unsigned long tin[2];

	if (encrypt) {
		n2l(iv, tout0);
		n2l(iv, tout1);
		iv -= 8;
		for (l -= 8; l >= 0; l -= 8)
		{
			n2l(in, tin0);
			n2l(in, tin1);
			tin0 ^= tout0;
			tin1 ^= tout1;
			tin[0] = tin0;
			tin[1] = tin1;
			idea_encrypt(tin, ks);
			tout0 = tin[0];
			l2n(tout0, out);
			tout1 = tin[1];
			l2n(tout1, out);
		}
		if (l != -8) {
			n2ln(in, tin0, tin1, l + 8);
			tin0 ^= tout0;
			tin1 ^= tout1;
			tin[0] = tin0;
			tin[1] = tin1;
			idea_encrypt(tin, ks);
			tout0 = tin[0];
			l2n(tout0, out);
			tout1 = tin[1];
			l2n(tout1, out);
		}
		l2n(tout0, iv);
		l2n(tout1, iv);
	} else {
		n2l(iv, xor0);
		n2l(iv, xor1);
		iv -= 8;
		for (l -= 8; l >= 0; l -= 8)
		{
			n2l(in, tin0);
			tin[0] = tin0;
			n2l(in, tin1);
			tin[1] = tin1;
			idea_encrypt(tin, ks);
			tout0 = tin[0] ^ xor0;
			tout1 = tin[1] ^ xor1;
			l2n(tout0, out);
			l2n(tout1, out);
			xor0 = tin0;
			xor1 = tin1;
		}
		if (l != -8) {
			n2l(in, tin0);
			tin[0] = tin0;
			n2l(in, tin1);
			tin[1] = tin1;
			idea_encrypt(tin, ks);
			tout0 = tin[0] ^ xor0;
			tout1 = tin[1] ^ xor1;
			l2nn(tout0, tout1, out, l + 8);
			xor0 = tin0;
			xor1 = tin1;
		}
		l2n(xor0, iv);
		l2n(xor1, iv);
	}
	tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
	tin[0] = tin[1] = 0;
}
LCRYPTO_ALIAS(idea_cbc_encrypt);

void
idea_encrypt(unsigned long *d, IDEA_KEY_SCHEDULE *key)
{
	IDEA_INT *p;
	unsigned long x1, x2, x3, x4, t0, t1, ul;

	x2 = d[0];
	x1 = (x2 >> 16);
	x4 = d[1];
	x3 = (x4 >> 16);

	p = &(key->data[0][0]);

	E_IDEA(0);
	E_IDEA(1);
	E_IDEA(2);
	E_IDEA(3);
	E_IDEA(4);
	E_IDEA(5);
	E_IDEA(6);
	E_IDEA(7);

	x1 &= 0xffff;
	idea_mul(x1, x1, *p, ul);
	p++;

	t0 = x3 + *(p++);
	t1 = x2 + *(p++);

	x4 &= 0xffff;
	idea_mul(x4, x4, *p, ul);

	d[0] = (t0 & 0xffff)|((x1 & 0xffff) << 16);
	d[1] = (x4 & 0xffff)|((t1 & 0xffff) << 16);
}
LCRYPTO_ALIAS(idea_encrypt);

/* The input and output encrypted as though 64bit cfb mode is being
 * used.  The extra state information to record how much of the
 * 64bit block we have used is contained in *num;
 */

void
idea_cfb64_encrypt(const unsigned char *in, unsigned char *out,
    long length, IDEA_KEY_SCHEDULE *schedule,
    unsigned char *ivec, int *num, int encrypt)
{
	unsigned long v0, v1, t;
	int n = *num;
	long l = length;
	unsigned long ti[2];
	unsigned char *iv, c, cc;

	iv = (unsigned char *)ivec;
	if (encrypt) {
		while (l--) {
			if (n == 0) {
				n2l(iv, v0);
				ti[0] = v0;
				n2l(iv, v1);
				ti[1] = v1;
				idea_encrypt((unsigned long *)ti, schedule);
				iv = (unsigned char *)ivec;
				t = ti[0];
				l2n(t, iv);
				t = ti[1];
				l2n(t, iv);
				iv = (unsigned char *)ivec;
			}
			c = *(in++) ^ iv[n];
			*(out++) = c;
			iv[n] = c;
			n = (n + 1) & 0x07;
		}
	} else {
		while (l--) {
			if (n == 0) {
				n2l(iv, v0);
				ti[0] = v0;
				n2l(iv, v1);
				ti[1] = v1;
				idea_encrypt((unsigned long *)ti, schedule);
				iv = (unsigned char *)ivec;
				t = ti[0];
				l2n(t, iv);
				t = ti[1];
				l2n(t, iv);
				iv = (unsigned char *)ivec;
			}
			cc = *(in++);
			c = iv[n];
			iv[n] = cc;
			*(out++) = c ^ cc;
			n = (n + 1) & 0x07;
		}
	}
	v0 = v1 = ti[0] = ti[1] = t = c = cc = 0;
	*num = n;
}
LCRYPTO_ALIAS(idea_cfb64_encrypt);

void
idea_ecb_encrypt(const unsigned char *in, unsigned char *out,
    IDEA_KEY_SCHEDULE *ks)
{
	unsigned long l0, l1, d[2];

	n2l(in, l0);
	d[0] = l0;
	n2l(in, l1);
	d[1] = l1;
	idea_encrypt(d, ks);
	l0 = d[0];
	l2n(l0, out);
	l1 = d[1];
	l2n(l1, out);
	l0 = l1 = d[0] = d[1] = 0;
}
LCRYPTO_ALIAS(idea_ecb_encrypt);

/*
 * The input and output encrypted as though 64bit ofb mode is being
 * used.  The extra state information to record how much of the
 * 64bit block we have used is contained in *num;
 */
void
idea_ofb64_encrypt(const unsigned char *in, unsigned char *out,
    long length, IDEA_KEY_SCHEDULE *schedule,
    unsigned char *ivec, int *num)
{
	unsigned long v0, v1, t;
	int n = *num;
	long l = length;
	unsigned char d[8];
	char *dp;
	unsigned long ti[2];
	unsigned char *iv;
	int save = 0;

	iv = (unsigned char *)ivec;
	n2l(iv, v0);
	n2l(iv, v1);
	ti[0] = v0;
	ti[1] = v1;
	dp = (char *)d;
	l2n(v0, dp);
	l2n(v1, dp);
	while (l--) {
		if (n == 0) {
			idea_encrypt((unsigned long *)ti, schedule);
			dp = (char *)d;
			t = ti[0];
			l2n(t, dp);
			t = ti[1];
			l2n(t, dp);
			save++;
		}
		*(out++) = *(in++) ^ d[n];
		n = (n + 1) & 0x07;
	}
	if (save) {
		v0 = ti[0];
		v1 = ti[1];
		iv = (unsigned char *)ivec;
		l2n(v0, iv);
		l2n(v1, iv);
	}
	t = v0 = v1 = ti[0] = ti[1] = 0;
	*num = n;
}
LCRYPTO_ALIAS(idea_ofb64_encrypt);

/* taken directly from the 'paper' I'll have a look at it later */
static IDEA_INT
inverse(unsigned int xin)
{
	long n1, n2, q, r, b1, b2, t;

	if (xin == 0)
		b2 = 0;
	else {
		n1 = 0x10001;
		n2 = xin;
		b2 = 1;
		b1 = 0;

		do {
			r = (n1 % n2);
			q = (n1 - r)/n2;
			if (r == 0) {
				if (b2 < 0)
					b2 = 0x10001 + b2;
			} else {
				n1 = n2;
				n2 = r;
				t = b2;
				b2 = b1 - q*b2;
				b1 = t;
			}
		} while (r != 0);
	}
	return ((IDEA_INT)b2);
}

void
idea_set_encrypt_key(const unsigned char *key, IDEA_KEY_SCHEDULE *ks)
{
	int i;
	IDEA_INT *kt, *kf, r0, r1, r2;

	kt = &(ks->data[0][0]);
	n2s(key, kt[0]);
	n2s(key, kt[1]);
	n2s(key, kt[2]);
	n2s(key, kt[3]);
	n2s(key, kt[4]);
	n2s(key, kt[5]);
	n2s(key, kt[6]);
	n2s(key, kt[7]);

	kf = kt;
	kt += 8;
	for (i = 0; i < 6; i++)
	{
		r2 = kf[1];
		r1 = kf[2];
		*(kt++) = ((r2 << 9) | (r1 >> 7)) & 0xffff;
		r0 = kf[3];
		*(kt++) = ((r1 << 9) | (r0 >> 7)) & 0xffff;
		r1 = kf[4];
		*(kt++) = ((r0 << 9) | (r1 >> 7)) & 0xffff;
		r0 = kf[5];
		*(kt++) = ((r1 << 9) | (r0 >> 7)) & 0xffff;
		r1 = kf[6];
		*(kt++) = ((r0 << 9) | (r1 >> 7)) & 0xffff;
		r0 = kf[7];
		*(kt++) = ((r1 << 9) | (r0 >> 7)) & 0xffff;
		r1 = kf[0];
		if (i >= 5)
			break;
		*(kt++) = ((r0 << 9) | (r1 >> 7)) & 0xffff;
		*(kt++) = ((r1 << 9) | (r2 >> 7)) & 0xffff;
		kf += 8;
	}
}
LCRYPTO_ALIAS(idea_set_encrypt_key);

void
idea_set_decrypt_key(IDEA_KEY_SCHEDULE *ek, IDEA_KEY_SCHEDULE *dk)
{
	int r;
	IDEA_INT *fp, *tp, t;

	tp = &(dk->data[0][0]);
	fp = &(ek->data[8][0]);
	for (r = 0; r < 9; r++)
	{
		*(tp++) = inverse(fp[0]);
		*(tp++) = ((int)(0x10000L - fp[2]) & 0xffff);
		*(tp++) = ((int)(0x10000L - fp[1]) & 0xffff);
		*(tp++) = inverse(fp[3]);
		if (r == 8)
			break;
		fp -= 6;
		*(tp++) = fp[4];
		*(tp++) = fp[5];
	}

	tp = &(dk->data[0][0]);
	t = tp[1];
	tp[1] = tp[2];
	tp[2] = t;

	t = tp[49];
	tp[49] = tp[50];
	tp[50] = t;
}
LCRYPTO_ALIAS(idea_set_decrypt_key);
