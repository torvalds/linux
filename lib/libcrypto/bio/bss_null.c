/* $OpenBSD: bss_null.c,v 1.13 2023/07/05 21:23:37 beck Exp $ */
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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bio.h>

#include "bio_local.h"

static int null_write(BIO *h, const char *buf, int num);
static int null_read(BIO *h, char *buf, int size);
static int null_puts(BIO *h, const char *str);
static int null_gets(BIO *h, char *str, int size);
static long null_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int null_new(BIO *h);
static int null_free(BIO *data);

static const BIO_METHOD null_method = {
	.type = BIO_TYPE_NULL,
	.name = "NULL",
	.bwrite = null_write,
	.bread = null_read,
	.bputs = null_puts,
	.bgets = null_gets,
	.ctrl = null_ctrl,
	.create = null_new,
	.destroy = null_free
};

const BIO_METHOD *
BIO_s_null(void)
{
	return (&null_method);
}
LCRYPTO_ALIAS(BIO_s_null);

static int
null_new(BIO *bi)
{
	bi->init = 1;
	bi->num = 0;
	bi->ptr = (NULL);
	return (1);
}

static int
null_free(BIO *a)
{
	if (a == NULL)
		return (0);
	return (1);
}

static int
null_read(BIO *b, char *out, int outl)
{
	return (0);
}

static int
null_write(BIO *b, const char *in, int inl)
{
	return (inl);
}

static long
null_ctrl(BIO *b, int cmd, long num, void *ptr)
{
	long ret = 1;

	switch (cmd) {
	case BIO_CTRL_RESET:
	case BIO_CTRL_EOF:
	case BIO_CTRL_SET:
	case BIO_CTRL_SET_CLOSE:
	case BIO_CTRL_FLUSH:
	case BIO_CTRL_DUP:
		ret = 1;
		break;
	case BIO_CTRL_GET_CLOSE:
	case BIO_CTRL_INFO:
	case BIO_CTRL_GET:
	case BIO_CTRL_PENDING:
	case BIO_CTRL_WPENDING:
	default:
		ret = 0;
		break;
	}
	return (ret);
}

static int
null_gets(BIO *bp, char *buf, int size)
{
	return (0);
}

static int
null_puts(BIO *bp, const char *str)
{
	if (str == NULL)
		return (0);
	return (strlen(str));
}
