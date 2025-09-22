/* $OpenBSD: des_enc.c,v 1.21 2025/07/27 13:26:24 jsing Exp $ */
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

#include "des_local.h"

const DES_LONG DES_SPtrans[8][64] = {
	{
/* nibble 0 */
		0x02080800L, 0x00080000L, 0x02000002L, 0x02080802L,
		0x02000000L, 0x00080802L, 0x00080002L, 0x02000002L,
		0x00080802L, 0x02080800L, 0x02080000L, 0x00000802L,
		0x02000802L, 0x02000000L, 0x00000000L, 0x00080002L,
		0x00080000L, 0x00000002L, 0x02000800L, 0x00080800L,
		0x02080802L, 0x02080000L, 0x00000802L, 0x02000800L,
		0x00000002L, 0x00000800L, 0x00080800L, 0x02080002L,
		0x00000800L, 0x02000802L, 0x02080002L, 0x00000000L,
		0x00000000L, 0x02080802L, 0x02000800L, 0x00080002L,
		0x02080800L, 0x00080000L, 0x00000802L, 0x02000800L,
		0x02080002L, 0x00000800L, 0x00080800L, 0x02000002L,
		0x00080802L, 0x00000002L, 0x02000002L, 0x02080000L,
		0x02080802L, 0x00080800L, 0x02080000L, 0x02000802L,
		0x02000000L, 0x00000802L, 0x00080002L, 0x00000000L,
		0x00080000L, 0x02000000L, 0x02000802L, 0x02080800L,
		0x00000002L, 0x02080002L, 0x00000800L, 0x00080802L,
	}, {
/* nibble 1 */
		0x40108010L, 0x00000000L, 0x00108000L, 0x40100000L,
		0x40000010L, 0x00008010L, 0x40008000L, 0x00108000L,
		0x00008000L, 0x40100010L, 0x00000010L, 0x40008000L,
		0x00100010L, 0x40108000L, 0x40100000L, 0x00000010L,
		0x00100000L, 0x40008010L, 0x40100010L, 0x00008000L,
		0x00108010L, 0x40000000L, 0x00000000L, 0x00100010L,
		0x40008010L, 0x00108010L, 0x40108000L, 0x40000010L,
		0x40000000L, 0x00100000L, 0x00008010L, 0x40108010L,
		0x00100010L, 0x40108000L, 0x40008000L, 0x00108010L,
		0x40108010L, 0x00100010L, 0x40000010L, 0x00000000L,
		0x40000000L, 0x00008010L, 0x00100000L, 0x40100010L,
		0x00008000L, 0x40000000L, 0x00108010L, 0x40008010L,
		0x40108000L, 0x00008000L, 0x00000000L, 0x40000010L,
		0x00000010L, 0x40108010L, 0x00108000L, 0x40100000L,
		0x40100010L, 0x00100000L, 0x00008010L, 0x40008000L,
		0x40008010L, 0x00000010L, 0x40100000L, 0x00108000L,
	}, {
/* nibble 2 */
		0x04000001L, 0x04040100L, 0x00000100L, 0x04000101L,
		0x00040001L, 0x04000000L, 0x04000101L, 0x00040100L,
		0x04000100L, 0x00040000L, 0x04040000L, 0x00000001L,
		0x04040101L, 0x00000101L, 0x00000001L, 0x04040001L,
		0x00000000L, 0x00040001L, 0x04040100L, 0x00000100L,
		0x00000101L, 0x04040101L, 0x00040000L, 0x04000001L,
		0x04040001L, 0x04000100L, 0x00040101L, 0x04040000L,
		0x00040100L, 0x00000000L, 0x04000000L, 0x00040101L,
		0x04040100L, 0x00000100L, 0x00000001L, 0x00040000L,
		0x00000101L, 0x00040001L, 0x04040000L, 0x04000101L,
		0x00000000L, 0x04040100L, 0x00040100L, 0x04040001L,
		0x00040001L, 0x04000000L, 0x04040101L, 0x00000001L,
		0x00040101L, 0x04000001L, 0x04000000L, 0x04040101L,
		0x00040000L, 0x04000100L, 0x04000101L, 0x00040100L,
		0x04000100L, 0x00000000L, 0x04040001L, 0x00000101L,
		0x04000001L, 0x00040101L, 0x00000100L, 0x04040000L,
	}, {
/* nibble 3 */
		0x00401008L, 0x10001000L, 0x00000008L, 0x10401008L,
		0x00000000L, 0x10400000L, 0x10001008L, 0x00400008L,
		0x10401000L, 0x10000008L, 0x10000000L, 0x00001008L,
		0x10000008L, 0x00401008L, 0x00400000L, 0x10000000L,
		0x10400008L, 0x00401000L, 0x00001000L, 0x00000008L,
		0x00401000L, 0x10001008L, 0x10400000L, 0x00001000L,
		0x00001008L, 0x00000000L, 0x00400008L, 0x10401000L,
		0x10001000L, 0x10400008L, 0x10401008L, 0x00400000L,
		0x10400008L, 0x00001008L, 0x00400000L, 0x10000008L,
		0x00401000L, 0x10001000L, 0x00000008L, 0x10400000L,
		0x10001008L, 0x00000000L, 0x00001000L, 0x00400008L,
		0x00000000L, 0x10400008L, 0x10401000L, 0x00001000L,
		0x10000000L, 0x10401008L, 0x00401008L, 0x00400000L,
		0x10401008L, 0x00000008L, 0x10001000L, 0x00401008L,
		0x00400008L, 0x00401000L, 0x10400000L, 0x10001008L,
		0x00001008L, 0x10000000L, 0x10000008L, 0x10401000L,
	}, {
/* nibble 4 */
		0x08000000L, 0x00010000L, 0x00000400L, 0x08010420L,
		0x08010020L, 0x08000400L, 0x00010420L, 0x08010000L,
		0x00010000L, 0x00000020L, 0x08000020L, 0x00010400L,
		0x08000420L, 0x08010020L, 0x08010400L, 0x00000000L,
		0x00010400L, 0x08000000L, 0x00010020L, 0x00000420L,
		0x08000400L, 0x00010420L, 0x00000000L, 0x08000020L,
		0x00000020L, 0x08000420L, 0x08010420L, 0x00010020L,
		0x08010000L, 0x00000400L, 0x00000420L, 0x08010400L,
		0x08010400L, 0x08000420L, 0x00010020L, 0x08010000L,
		0x00010000L, 0x00000020L, 0x08000020L, 0x08000400L,
		0x08000000L, 0x00010400L, 0x08010420L, 0x00000000L,
		0x00010420L, 0x08000000L, 0x00000400L, 0x00010020L,
		0x08000420L, 0x00000400L, 0x00000000L, 0x08010420L,
		0x08010020L, 0x08010400L, 0x00000420L, 0x00010000L,
		0x00010400L, 0x08010020L, 0x08000400L, 0x00000420L,
		0x00000020L, 0x00010420L, 0x08010000L, 0x08000020L,
	}, {
/* nibble 5 */
		0x80000040L, 0x00200040L, 0x00000000L, 0x80202000L,
		0x00200040L, 0x00002000L, 0x80002040L, 0x00200000L,
		0x00002040L, 0x80202040L, 0x00202000L, 0x80000000L,
		0x80002000L, 0x80000040L, 0x80200000L, 0x00202040L,
		0x00200000L, 0x80002040L, 0x80200040L, 0x00000000L,
		0x00002000L, 0x00000040L, 0x80202000L, 0x80200040L,
		0x80202040L, 0x80200000L, 0x80000000L, 0x00002040L,
		0x00000040L, 0x00202000L, 0x00202040L, 0x80002000L,
		0x00002040L, 0x80000000L, 0x80002000L, 0x00202040L,
		0x80202000L, 0x00200040L, 0x00000000L, 0x80002000L,
		0x80000000L, 0x00002000L, 0x80200040L, 0x00200000L,
		0x00200040L, 0x80202040L, 0x00202000L, 0x00000040L,
		0x80202040L, 0x00202000L, 0x00200000L, 0x80002040L,
		0x80000040L, 0x80200000L, 0x00202040L, 0x00000000L,
		0x00002000L, 0x80000040L, 0x80002040L, 0x80202000L,
		0x80200000L, 0x00002040L, 0x00000040L, 0x80200040L,
	}, {
/* nibble 6 */
		0x00004000L, 0x00000200L, 0x01000200L, 0x01000004L,
		0x01004204L, 0x00004004L, 0x00004200L, 0x00000000L,
		0x01000000L, 0x01000204L, 0x00000204L, 0x01004000L,
		0x00000004L, 0x01004200L, 0x01004000L, 0x00000204L,
		0x01000204L, 0x00004000L, 0x00004004L, 0x01004204L,
		0x00000000L, 0x01000200L, 0x01000004L, 0x00004200L,
		0x01004004L, 0x00004204L, 0x01004200L, 0x00000004L,
		0x00004204L, 0x01004004L, 0x00000200L, 0x01000000L,
		0x00004204L, 0x01004000L, 0x01004004L, 0x00000204L,
		0x00004000L, 0x00000200L, 0x01000000L, 0x01004004L,
		0x01000204L, 0x00004204L, 0x00004200L, 0x00000000L,
		0x00000200L, 0x01000004L, 0x00000004L, 0x01000200L,
		0x00000000L, 0x01000204L, 0x01000200L, 0x00004200L,
		0x00000204L, 0x00004000L, 0x01004204L, 0x01000000L,
		0x01004200L, 0x00000004L, 0x00004004L, 0x01004204L,
		0x01000004L, 0x01004200L, 0x01004000L, 0x00004004L,
	}, {
/* nibble 7 */
		0x20800080L, 0x20820000L, 0x00020080L, 0x00000000L,
		0x20020000L, 0x00800080L, 0x20800000L, 0x20820080L,
		0x00000080L, 0x20000000L, 0x00820000L, 0x00020080L,
		0x00820080L, 0x20020080L, 0x20000080L, 0x20800000L,
		0x00020000L, 0x00820080L, 0x00800080L, 0x20020000L,
		0x20820080L, 0x20000080L, 0x00000000L, 0x00820000L,
		0x20000000L, 0x00800000L, 0x20020080L, 0x20800080L,
		0x00800000L, 0x00020000L, 0x20820000L, 0x00000080L,
		0x00800000L, 0x00020000L, 0x20000080L, 0x20820080L,
		0x00020080L, 0x20000000L, 0x00000000L, 0x00820000L,
		0x20800080L, 0x20020080L, 0x20020000L, 0x00800080L,
		0x20820000L, 0x00000080L, 0x00800080L, 0x20020000L,
		0x20820080L, 0x00800000L, 0x20800000L, 0x20000080L,
		0x00820000L, 0x00020080L, 0x20020080L, 0x20800000L,
		0x00000080L, 0x20820000L, 0x00820080L, 0x00000000L,
		0x20000000L, 0x20800080L, 0x00020000L, 0x00820080L,
	},
};

void
DES_encrypt1(DES_LONG *data, DES_key_schedule *ks, int enc)
{
	DES_LONG l, r, t, u;
	DES_LONG *s;
	int i;

	r = data[0];
	l = data[1];

	IP(r, l);
	/* Things have been modified so that the initial rotate is
	 * done outside the loop.  This required the
	 * DES_SPtrans values in sp.h to be rotated 1 bit to the right.
	 * One perl script later and things have a 5% speed up on a sparc2.
	 * Thanks to Richard Outerbridge <71755.204@CompuServe.COM>
	 * for pointing this out. */
	/* clear the top bits on machines with 8byte longs */
	/* shift left by 2 */
	r = ROTATE(r, 29) & 0xffffffffL;
	l = ROTATE(l, 29) & 0xffffffffL;

	s = ks->ks->deslong;

	if (enc) {
		for (i = 0; i < 32; i += 8) {
			D_ENCRYPT(l, r, i + 0);
			D_ENCRYPT(r, l, i + 2);
			D_ENCRYPT(l, r, i + 4);
			D_ENCRYPT(r, l, i + 6);
		}
	} else {
		for (i = 32; i > 0; i -= 8) {
			D_ENCRYPT(l, r, i - 2);
			D_ENCRYPT(r, l, i - 4);
			D_ENCRYPT(l, r, i - 6);
			D_ENCRYPT(r, l, i - 8);
		}
	}

	/* rotate and clear the top bits on machines with 8byte longs */
	l = ROTATE(l, 3) & 0xffffffffL;
	r = ROTATE(r, 3) & 0xffffffffL;

	FP(r, l);
	data[0] = l;
	data[1] = r;
	l = r = t = u = 0;
}
LCRYPTO_ALIAS(DES_encrypt1);

void
DES_encrypt2(DES_LONG *data, DES_key_schedule *ks, int enc)
{
	DES_LONG l, r, t, u;
	DES_LONG *s;
	int i;

	r = data[0];
	l = data[1];

	/* Things have been modified so that the initial rotate is
	 * done outside the loop.  This required the
	 * DES_SPtrans values in sp.h to be rotated 1 bit to the right.
	 * One perl script later and things have a 5% speed up on a sparc2.
	 * Thanks to Richard Outerbridge <71755.204@CompuServe.COM>
	 * for pointing this out. */
	/* clear the top bits on machines with 8byte longs */
	r = ROTATE(r, 29) & 0xffffffffL;
	l = ROTATE(l, 29) & 0xffffffffL;

	s = ks->ks->deslong;
	/* I don't know if it is worth the effort of loop unrolling the
	 * inner loop */
	if (enc) {
		for (i = 0; i < 32; i += 8) {
			D_ENCRYPT(l, r, i + 0);
			D_ENCRYPT(r, l, i + 2);
			D_ENCRYPT(l, r, i + 4);
			D_ENCRYPT(r, l, i + 6);
		}
	} else {
		for (i = 32; i > 0; i -= 8) {
			D_ENCRYPT(l, r, i - 2);
			D_ENCRYPT(r, l, i - 4);
			D_ENCRYPT(l, r, i - 6);
			D_ENCRYPT(r, l, i - 8);
		}
	}
	/* rotate and clear the top bits on machines with 8byte longs */
	data[0] = ROTATE(l, 3) & 0xffffffffL;
	data[1] = ROTATE(r, 3) & 0xffffffffL;
	l = r = t = u = 0;
}
LCRYPTO_ALIAS(DES_encrypt2);

void
DES_encrypt3(DES_LONG *data, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3)
{
	DES_LONG l, r;

	l = data[0];
	r = data[1];
	IP(l, r);
	data[0] = l;
	data[1] = r;
	DES_encrypt2((DES_LONG *)data, ks1, DES_ENCRYPT);
	DES_encrypt2((DES_LONG *)data, ks2, DES_DECRYPT);
	DES_encrypt2((DES_LONG *)data, ks3, DES_ENCRYPT);
	l = data[0];
	r = data[1];
	FP(r, l);
	data[0] = l;
	data[1] = r;
}
LCRYPTO_ALIAS(DES_encrypt3);

void
DES_decrypt3(DES_LONG *data, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3)
{
	DES_LONG l, r;

	l = data[0];
	r = data[1];
	IP(l, r);
	data[0] = l;
	data[1] = r;
	DES_encrypt2((DES_LONG *)data, ks3, DES_DECRYPT);
	DES_encrypt2((DES_LONG *)data, ks2, DES_ENCRYPT);
	DES_encrypt2((DES_LONG *)data, ks1, DES_DECRYPT);
	l = data[0];
	r = data[1];
	FP(r, l);
	data[0] = l;
	data[1] = r;
}
LCRYPTO_ALIAS(DES_decrypt3);

#ifndef DES_DEFAULT_OPTIONS

void
DES_ncbc_encrypt(const unsigned char *in, unsigned char *out, long length,
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
		iv = &(*ivec)[0];
		l2c(tout0, iv);
		l2c(tout1, iv);
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
			xor0 = tin0;
			xor1 = tin1;
		}
		iv = &(*ivec)[0];
		l2c(xor0, iv);
		l2c(xor1, iv);
	}
	tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
	tin[0] = tin[1] = 0;
}
LCRYPTO_ALIAS(DES_ncbc_encrypt);

void
DES_ede3_cbc_encrypt(const unsigned char *input, unsigned char *output,
    long length, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3,
    DES_cblock *ivec, int enc)
{
	DES_LONG tin0, tin1;
	DES_LONG tout0, tout1, xor0, xor1;
	const unsigned char *in;
	unsigned char *out;
	long l = length;
	DES_LONG tin[2];
	unsigned char *iv;

	in = input;
	out = output;
	iv = &(*ivec)[0];

	if (enc) {
		c2l(iv, tout0);
		c2l(iv, tout1);
		for (l -= 8; l >= 0; l -= 8) {
			c2l(in, tin0);
			c2l(in, tin1);
			tin0 ^= tout0;
			tin1 ^= tout1;

			tin[0] = tin0;
			tin[1] = tin1;
			DES_encrypt3((DES_LONG *)tin, ks1, ks2, ks3);
			tout0 = tin[0];
			tout1 = tin[1];

			l2c(tout0, out);
			l2c(tout1, out);
		}
		if (l != -8) {
			c2ln(in, tin0, tin1, l + 8);
			tin0 ^= tout0;
			tin1 ^= tout1;

			tin[0] = tin0;
			tin[1] = tin1;
			DES_encrypt3((DES_LONG *)tin, ks1, ks2, ks3);
			tout0 = tin[0];
			tout1 = tin[1];

			l2c(tout0, out);
			l2c(tout1, out);
		}
		iv = &(*ivec)[0];
		l2c(tout0, iv);
		l2c(tout1, iv);
	} else {
		DES_LONG t0, t1;

		c2l(iv, xor0);
		c2l(iv, xor1);
		for (l -= 8; l >= 0; l -= 8) {
			c2l(in, tin0);
			c2l(in, tin1);

			t0 = tin0;
			t1 = tin1;

			tin[0] = tin0;
			tin[1] = tin1;
			DES_decrypt3((DES_LONG *)tin, ks1, ks2, ks3);
			tout0 = tin[0];
			tout1 = tin[1];

			tout0 ^= xor0;
			tout1 ^= xor1;
			l2c(tout0, out);
			l2c(tout1, out);
			xor0 = t0;
			xor1 = t1;
		}
		if (l != -8) {
			c2l(in, tin0);
			c2l(in, tin1);

			t0 = tin0;
			t1 = tin1;

			tin[0] = tin0;
			tin[1] = tin1;
			DES_decrypt3((DES_LONG *)tin, ks1, ks2, ks3);
			tout0 = tin[0];
			tout1 = tin[1];

			tout0 ^= xor0;
			tout1 ^= xor1;
			l2cn(tout0, tout1, out, l + 8);
			xor0 = t0;
			xor1 = t1;
		}

		iv = &(*ivec)[0];
		l2c(xor0, iv);
		l2c(xor1, iv);
	}
	tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
	tin[0] = tin[1] = 0;
}
LCRYPTO_ALIAS(DES_ede3_cbc_encrypt);

#endif /* DES_DEFAULT_OPTIONS */
