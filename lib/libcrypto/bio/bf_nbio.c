/* $OpenBSD: bf_nbio.c,v 1.23 2023/07/05 21:23:37 beck Exp $ */
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
#include <stdlib.h>

#include <openssl/bio.h>

#include "bio_local.h"

/* BIO_put and BIO_get both add to the digest,
 * BIO_gets returns the digest */

static int nbiof_write(BIO *h, const char *buf, int num);
static int nbiof_read(BIO *h, char *buf, int size);
static int nbiof_puts(BIO *h, const char *str);
static int nbiof_gets(BIO *h, char *str, int size);
static long nbiof_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int nbiof_new(BIO *h);
static int nbiof_free(BIO *data);
static long nbiof_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);

typedef struct nbio_test_st {
	/* only set if we sent a 'should retry' error */
	int lrn;
	int lwn;
} NBIO_TEST;

static const BIO_METHOD methods_nbiof = {
	.type = BIO_TYPE_NBIO_TEST,
	.name = "non-blocking IO test filter",
	.bwrite = nbiof_write,
	.bread = nbiof_read,
	.bputs = nbiof_puts,
	.bgets = nbiof_gets,
	.ctrl = nbiof_ctrl,
	.create = nbiof_new,
	.destroy = nbiof_free,
	.callback_ctrl = nbiof_callback_ctrl
};

const BIO_METHOD *
BIO_f_nbio_test(void)
{
	return (&methods_nbiof);
}
LCRYPTO_ALIAS(BIO_f_nbio_test);

static int
nbiof_new(BIO *bi)
{
	NBIO_TEST *nt;

	if (!(nt = malloc(sizeof(NBIO_TEST))))
		return (0);
	nt->lrn = -1;
	nt->lwn = -1;
	bi->ptr = (char *)nt;
	bi->init = 1;
	bi->flags = 0;
	return (1);
}

static int
nbiof_free(BIO *a)
{
	if (a == NULL)
		return (0);
	free(a->ptr);
	a->ptr = NULL;
	a->init = 0;
	a->flags = 0;
	return (1);
}

static int
nbiof_read(BIO *b, char *out, int outl)
{
	int ret = 0;
	int num;
	unsigned char n;

	if (out == NULL)
		return (0);
	if (b->next_bio == NULL)
		return (0);

	BIO_clear_retry_flags(b);

	arc4random_buf(&n, 1);
	num = (n & 0x07);

	if (outl > num)
		outl = num;

	if (num == 0) {
		ret = -1;
		BIO_set_retry_read(b);
	} else {
		ret = BIO_read(b->next_bio, out, outl);
		if (ret < 0)
			BIO_copy_next_retry(b);
	}
	return (ret);
}

static int
nbiof_write(BIO *b, const char *in, int inl)
{
	NBIO_TEST *nt;
	int ret = 0;
	int num;
	unsigned char n;

	if ((in == NULL) || (inl <= 0))
		return (0);
	if (b->next_bio == NULL)
		return (0);
	nt = (NBIO_TEST *)b->ptr;

	BIO_clear_retry_flags(b);

	if (nt->lwn > 0) {
		num = nt->lwn;
		nt->lwn = 0;
	} else {
		arc4random_buf(&n, 1);
		num = (n&7);
	}

	if (inl > num)
		inl = num;

	if (num == 0) {
		ret = -1;
		BIO_set_retry_write(b);
	} else {
		ret = BIO_write(b->next_bio, in, inl);
		if (ret < 0) {
			BIO_copy_next_retry(b);
			nt->lwn = inl;
		}
	}
	return (ret);
}

static long
nbiof_ctrl(BIO *b, int cmd, long num, void *ptr)
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
		break;
	}
	return (ret);
}

static long
nbiof_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
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
nbiof_gets(BIO *bp, char *buf, int size)
{
	if (bp->next_bio == NULL)
		return (0);
	return (BIO_gets(bp->next_bio, buf, size));
}

static int
nbiof_puts(BIO *bp, const char *str)
{
	if (bp->next_bio == NULL)
		return (0);
	return (BIO_puts(bp->next_bio, str));
}
