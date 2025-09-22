/* $OpenBSD: des_fcrypt.c,v 1.5 2025/07/27 13:26:24 jsing Exp $ */
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

#include <stdio.h>

/* This version of crypt has been developed from my MIT compatible
 * DES library.
 * Eric Young (eay@cryptsoft.com)
 */

/* Modification by Jens Kupferschmidt (Cu)
 * I have included directive PARA for shared memory computers.
 * I have included a directive LONGCRYPT to using this routine to cipher
 * passwords with more than 8 bytes like HP-UX 10.x it used. The MAXPLEN
 * definition is the maximum of length of password and can changed. I have
 * defined 24.
 */

#define DES_FCRYPT
#include "des_local.h"
#undef DES_FCRYPT

#undef PERM_OP
#define PERM_OP(a,b,t,n,m) ((t)=((((a)>>(n))^(b))&(m)),			\
	(b)^=(t),							\
	(a)^=((t)<<(n)))

#undef HPERM_OP
#define HPERM_OP(a,t,n,m) ((t)=((((a)<<(16-(n)))^(a))&(m)),		\
	(a)=(a)^(t)^(t>>(16-(n))))					\

static void
fcrypt_body(DES_LONG *out, DES_key_schedule *ks, DES_LONG Eswap0,
    DES_LONG Eswap1)
{
	DES_LONG l, r, t, u;
	DES_LONG *s;
	DES_LONG E0, E1;
	int i, j;

	l = 0;
	r = 0;

	s = (DES_LONG *)ks;
	E0 = Eswap0;
	E1 = Eswap1;

	for (j = 0; j < 25; j++) {
		for (i = 0; i < 32; i += 8) {
			D_ENCRYPT(l, r, i + 0);
			D_ENCRYPT(r, l, i + 2);
			D_ENCRYPT(l, r, i + 4);
			D_ENCRYPT(r, l, i + 6);
		}
		t = l;
		l = r;
		r = t;
	}
	l = ROTATE(l, 3) & 0xffffffffL;
	r = ROTATE(r, 3) & 0xffffffffL;

	PERM_OP(l, r, t, 1, 0x55555555L);
	PERM_OP(r, l, t, 8, 0x00ff00ffL);
	PERM_OP(l, r, t, 2, 0x33333333L);
	PERM_OP(r, l, t, 16, 0x0000ffffL);
	PERM_OP(l, r, t, 4, 0x0f0f0f0fL);

	out[0] = r;
	out[1] = l;
}

/* Added more values to handle illegal salt values the way normal
 * crypt() implementations do.  The patch was sent by
 * Bjorn Gronvall <bg@sics.se>
 */
static unsigned const char con_salt[128] = {
	0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
	0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1,
	0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
	0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
	0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
	0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF, 0x00, 0x01,
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0A, 0x0B, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
	0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
	0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
	0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,
	0x23, 0x24, 0x25, 0x20, 0x21, 0x22, 0x23, 0x24,
	0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
	0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34,
	0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
	0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
};

static unsigned const char cov_2char[64] = {
	0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
	0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C,
	0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x61, 0x62,
	0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
	0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72,
	0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A
};

char *
DES_crypt(const char *buf, const char *salt)
{
	static char buff[14];

	return (DES_fcrypt(buf, salt, buff));
}
LCRYPTO_ALIAS(DES_crypt);

char *
DES_fcrypt(const char *buf, const char *salt, char *ret)
{
	unsigned int i, j, x, y;
	DES_LONG Eswap0, Eswap1;
	DES_LONG out[2], ll;
	DES_cblock key;
	DES_key_schedule ks;
	unsigned char bb[9];
	unsigned char *b = bb;
	unsigned char c, u;

	/* eay 25/08/92
	 * If you call crypt("pwd","*") as often happens when you
	 * have * as the pwd field in /etc/passwd, the function
	 * returns *\0xxxxxxxxx
	 * The \0 makes the string look like * so the pwd "*" would
	 * crypt to "*".  This was found when replacing the crypt in
	 * our shared libraries.  People found that the disabled
	 * accounts effectively had no passwd :-(. */
	x = ret[0] = ((salt[0] == '\0') ? 'A' : salt[0]);
	Eswap0 = con_salt[x] << 2;
	x = ret[1] = ((salt[1] == '\0') ? 'A' : salt[1]);
	Eswap1 = con_salt[x] << 6;
/* EAY
r=strlen(buf);
r=(r+7)/8;
*/
	for (i = 0; i < 8; i++) {
		c = *(buf++);
		if (!c)
			break;
		key[i] = (c << 1);
	}
	for (; i < 8; i++)
		key[i] = 0;

	DES_set_key_unchecked(&key, &ks);
	fcrypt_body(&(out[0]), &ks, Eswap0, Eswap1);

	ll = out[0];
	l2c(ll, b);
	ll = out[1];
	l2c(ll, b);
	y = 0;
	u = 0x80;
	bb[8] = 0;
	for (i = 2; i < 13; i++) {
		c = 0;
		for (j = 0; j < 6; j++) {
			c <<= 1;
			if (bb[y] & u)
				c |= 1;
			u >>= 1;
			if (!u) {
				y++;
				u = 0x80;
			}
		}
		ret[i] = cov_2char[c];
	}
	ret[13] = '\0';
	return (ret);
}
LCRYPTO_ALIAS(DES_fcrypt);
