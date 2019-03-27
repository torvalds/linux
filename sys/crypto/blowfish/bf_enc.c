/*	$KAME: bf_enc.c,v 1.7 2002/02/27 01:33:59 itojun Exp $	*/

/* crypto/bf/bf_enc.c */

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
#include <crypto/blowfish/blowfish.h>
#include <crypto/blowfish/bf_locl.h>

/* Blowfish as implemented from 'Blowfish: Springer-Verlag paper'
 * (From LECTURE NOTES IN COIMPUTER SCIENCE 809, FAST SOFTWARE ENCRYPTION,
 * CAMBRIDGE SECURITY WORKSHOP, CAMBRIDGE, U.K., DECEMBER 9-11, 1993)
 */

#if (BF_ROUNDS != 16) && (BF_ROUNDS != 20)
If you set BF_ROUNDS to some value other than 16 or 20, you will have
to modify the code.
#endif

/* XXX "data" is host endian */
void
BF_encrypt(data, key)
	BF_LONG *data;
	BF_KEY *key;
{
	register BF_LONG l, r, *p, *s;

	p = key->P;
	s= &key->S[0];
	l = data[0];
	r = data[1];

	l^=p[0];
	BF_ENC(r, l, s, p[ 1]);
	BF_ENC(l, r, s, p[ 2]);
	BF_ENC(r, l, s, p[ 3]);
	BF_ENC(l, r, s, p[ 4]);
	BF_ENC(r, l, s, p[ 5]);
	BF_ENC(l, r, s, p[ 6]);
	BF_ENC(r, l, s, p[ 7]);
	BF_ENC(l, r, s, p[ 8]);
	BF_ENC(r, l, s, p[ 9]);
	BF_ENC(l, r, s, p[10]);
	BF_ENC(r, l, s, p[11]);
	BF_ENC(l, r, s, p[12]);
	BF_ENC(r, l, s, p[13]);
	BF_ENC(l, r, s, p[14]);
	BF_ENC(r, l, s, p[15]);
	BF_ENC(l, r, s, p[16]);
#if BF_ROUNDS == 20
	BF_ENC(r, l, s, p[17]);
	BF_ENC(l, r, s, p[18]);
	BF_ENC(r, l, s, p[19]);
	BF_ENC(l, r, s, p[20]);
#endif
	r ^= p[BF_ROUNDS + 1];

	data[1] = l & 0xffffffff;
	data[0] = r & 0xffffffff;
}

/* XXX "data" is host endian */
void
BF_decrypt(data, key)
	BF_LONG *data;
	BF_KEY *key;
{
	register BF_LONG l, r, *p, *s;

	p = key->P;
	s= &key->S[0];
	l = data[0];
	r = data[1];

	l ^= p[BF_ROUNDS + 1];
#if BF_ROUNDS == 20
	BF_ENC(r, l, s, p[20]);
	BF_ENC(l, r, s, p[19]);
	BF_ENC(r, l, s, p[18]);
	BF_ENC(l, r, s, p[17]);
#endif
	BF_ENC(r, l, s, p[16]);
	BF_ENC(l, r, s, p[15]);
	BF_ENC(r, l, s, p[14]);
	BF_ENC(l, r, s, p[13]);
	BF_ENC(r, l, s, p[12]);
	BF_ENC(l, r, s, p[11]);
	BF_ENC(r, l, s, p[10]);
	BF_ENC(l, r, s, p[ 9]);
	BF_ENC(r, l, s, p[ 8]);
	BF_ENC(l, r, s, p[ 7]);
	BF_ENC(r, l, s, p[ 6]);
	BF_ENC(l, r, s, p[ 5]);
	BF_ENC(r, l, s, p[ 4]);
	BF_ENC(l, r, s, p[ 3]);
	BF_ENC(r, l, s, p[ 2]);
	BF_ENC(l, r, s, p[ 1]);
	r ^= p[0];

	data[1] = l & 0xffffffff;
	data[0] = r & 0xffffffff;
}
