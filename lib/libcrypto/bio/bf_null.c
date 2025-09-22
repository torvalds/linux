/* $OpenBSD: bf_null.c,v 1.15 2023/07/05 21:23:37 beck Exp $ */
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

#include <openssl/bio.h>

#include "bio_local.h"

/* BIO_put and BIO_get both add to the digest,
 * BIO_gets returns the digest */

static int nullf_write(BIO *h, const char *buf, int num);
static int nullf_read(BIO *h, char *buf, int size);
static int nullf_puts(BIO *h, const char *str);
static int nullf_gets(BIO *h, char *str, int size);
static long nullf_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int nullf_new(BIO *h);
static int nullf_free(BIO *data);
static long nullf_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);

static const BIO_METHOD methods_nullf = {
	.type = BIO_TYPE_NULL_FILTER,
	.name = "NULL filter",
	.bwrite = nullf_write,
	.bread = nullf_read,
	.bputs = nullf_puts,
	.bgets = nullf_gets,
	.ctrl = nullf_ctrl,
	.create = nullf_new,
	.destroy = nullf_free,
	.callback_ctrl = nullf_callback_ctrl
};

const BIO_METHOD *
BIO_f_null(void)
{
	return (&methods_nullf);
}
LCRYPTO_ALIAS(BIO_f_null);

static int
nullf_new(BIO *bi)
{
	bi->init = 1;
	bi->ptr = NULL;
	bi->flags = 0;
	return (1);
}

static int
nullf_free(BIO *a)
{
	if (a == NULL)
		return (0);
/*	a->ptr=NULL;
	a->init=0;
	a->flags=0;*/
	return (1);
}

static int
nullf_read(BIO *b, char *out, int outl)
{
	int ret = 0;

	if (out == NULL)
		return (0);
	if (b->next_bio == NULL)
		return (0);
	ret = BIO_read(b->next_bio, out, outl);
	BIO_clear_retry_flags(b);
	BIO_copy_next_retry(b);
	return (ret);
}

static int
nullf_write(BIO *b, const char *in, int inl)
{
	int ret = 0;

	if ((in == NULL) || (inl <= 0))
		return (0);
	if (b->next_bio == NULL)
		return (0);
	ret = BIO_write(b->next_bio, in, inl);
	BIO_clear_retry_flags(b);
	BIO_copy_next_retry(b);
	return (ret);
}

static long
nullf_ctrl(BIO *b, int cmd, long num, void *ptr)
{
	long ret;

	if (b->next_bio == NULL)
		return (0);
	switch (cmd) {
	case BIO_C_DO_STATE_MACHINE:
		BIO_clear_retry_flags(b);
		ret = BIO_ctrl(b->next_bio, cmd, num, ptr);
		BIO_copy_next_retry(b);
		break;
	case BIO_CTRL_DUP:
		ret = 0L;
		break;
	default:
		ret = BIO_ctrl(b->next_bio, cmd, num, ptr);
	}
	return (ret);
}

static long
nullf_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
	long ret = 1;

	if (b->next_bio == NULL)
		return (0);
	switch (cmd) {
	default:
		ret = BIO_callback_ctrl(b->next_bio, cmd, fp);
		break;
	}
	return (ret);
}

static int
nullf_gets(BIO *bp, char *buf, int size)
{
	if (bp->next_bio == NULL)
		return (0);
	return (BIO_gets(bp->next_bio, buf, size));
}

static int
nullf_puts(BIO *bp, const char *str)
{
	if (bp->next_bio == NULL)
		return (0);
	return (BIO_puts(bp->next_bio, str));
}
