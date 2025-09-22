/* $OpenBSD: bio_cb.c,v 1.20 2025/05/10 05:54:38 tb Exp $ */
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
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>

#include "bio_local.h"

long
BIO_debug_callback(BIO *bio, int cmd, const char *argp, int argi, long argl,
    long ret)
{
	BIO *b;
	char buf[256];
	char *p;
	int nbuf;
	long r = 1;
	size_t p_maxlen;

	if (BIO_CB_RETURN & cmd)
		r = ret;

	nbuf = snprintf(buf, sizeof(buf), "BIO[%p]: ", bio);
	if (nbuf < 0)
		nbuf = 0;	/* Ignore error; continue printing. */
	if (nbuf >= sizeof(buf))
		goto out;

	p = buf + nbuf;
	p_maxlen = sizeof(buf) - nbuf;

	switch (cmd) {
	case BIO_CB_FREE:
		snprintf(p, p_maxlen, "Free - %s\n", bio->method->name);
		break;
	case BIO_CB_READ:
		if (bio->method->type & BIO_TYPE_DESCRIPTOR)
			snprintf(p, p_maxlen,
			    "read(%d,%lu) - %s fd=%d\n",
			    bio->num, (unsigned long)argi,
			    bio->method->name, bio->num);
		else
			snprintf(p, p_maxlen, "read(%d,%lu) - %s\n",
			    bio->num, (unsigned long)argi, bio->method->name);
		break;
	case BIO_CB_WRITE:
		if (bio->method->type & BIO_TYPE_DESCRIPTOR)
			snprintf(p, p_maxlen,
			    "write(%d,%lu) - %s fd=%d\n",
			    bio->num, (unsigned long)argi,
			    bio->method->name, bio->num);
		else
			snprintf(p, p_maxlen, "write(%d,%lu) - %s\n",
			    bio->num, (unsigned long)argi, bio->method->name);
		break;
	case BIO_CB_PUTS:
		snprintf(p, p_maxlen,
		    "puts() - %s\n", bio->method->name);
		break;
	case BIO_CB_GETS:
		snprintf(p, p_maxlen, "gets(%lu) - %s\n",
		    (unsigned long)argi, bio->method->name);
		break;
	case BIO_CB_CTRL:
		snprintf(p, p_maxlen, "ctrl(%lu) - %s\n",
		    (unsigned long)argi, bio->method->name);
		break;
	case BIO_CB_RETURN|BIO_CB_READ:
		snprintf(p, p_maxlen, "read return %ld\n", ret);
		break;
	case BIO_CB_RETURN|BIO_CB_WRITE:
		snprintf(p, p_maxlen, "write return %ld\n", ret);
		break;
	case BIO_CB_RETURN|BIO_CB_GETS:
		snprintf(p, p_maxlen, "gets return %ld\n", ret);
		break;
	case BIO_CB_RETURN|BIO_CB_PUTS:
		snprintf(p, p_maxlen, "puts return %ld\n", ret);
		break;
	case BIO_CB_RETURN|BIO_CB_CTRL:
		snprintf(p, p_maxlen, "ctrl return %ld\n", ret);
		break;
	default:
		snprintf(p, p_maxlen,
		    "bio callback - unknown type (%d)\n", cmd);
		break;
	}

 out:
	b = (BIO *)bio->cb_arg;
	if (b != NULL)
		BIO_write(b, buf, strlen(buf));
	else
		fputs(buf, stderr);
	return (r);
}
LCRYPTO_ALIAS(BIO_debug_callback);
