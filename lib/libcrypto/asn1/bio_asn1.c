/* $OpenBSD: bio_asn1.c,v 1.23 2023/07/28 09:58:30 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

/* Experimental ASN1 BIO. When written through the data is converted
 * to an ASN1 string type: default is OCTET STRING. Additional functions
 * can be provided to add prefix and suffix data.
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/asn1.h>

#include "bio_local.h"

#define BIO_C_SET_PREFIX			149
#define BIO_C_SET_SUFFIX			151

/* Must be large enough for biggest tag+length */
#define DEFAULT_ASN1_BUF_SIZE 20

typedef enum {
	ASN1_STATE_START,
	ASN1_STATE_PRE_COPY,
	ASN1_STATE_HEADER,
	ASN1_STATE_HEADER_COPY,
	ASN1_STATE_DATA_COPY,
	ASN1_STATE_POST_COPY,
	ASN1_STATE_DONE
} asn1_bio_state_t;

typedef struct BIO_ASN1_EX_FUNCS_st {
	asn1_ps_func	*ex_func;
	asn1_ps_func	*ex_free_func;
} BIO_ASN1_EX_FUNCS;

typedef struct BIO_ASN1_BUF_CTX_t {
	/* Internal state */
	asn1_bio_state_t state;
	/* Internal buffer */
	unsigned char *buf;
	/* Size of buffer */
	int bufsize;
	/* Current position in buffer */
	int bufpos;
	/* Current buffer length */
	int buflen;
	/* Amount of data to copy */
	int copylen;
	/* Class and tag to use */
	int asn1_class, asn1_tag;
	asn1_ps_func *prefix, *prefix_free, *suffix, *suffix_free;
	/* Extra buffer for prefix and suffix data */
	unsigned char *ex_buf;
	int ex_len;
	int ex_pos;
	void *ex_arg;
} BIO_ASN1_BUF_CTX;


static int asn1_bio_write(BIO *h, const char *buf, int num);
static int asn1_bio_read(BIO *h, char *buf, int size);
static int asn1_bio_puts(BIO *h, const char *str);
static int asn1_bio_gets(BIO *h, char *str, int size);
static long asn1_bio_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int asn1_bio_new(BIO *h);
static int asn1_bio_free(BIO *data);
static long asn1_bio_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);

static int asn1_bio_flush_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx,
    asn1_ps_func *cleanup, asn1_bio_state_t next);
static int asn1_bio_setup_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx,
    asn1_ps_func *setup, asn1_bio_state_t ex_state,
    asn1_bio_state_t other_state);

static const BIO_METHOD methods_asn1 = {
	.type = BIO_TYPE_ASN1,
	.name = "asn1",
	.bwrite = asn1_bio_write,
	.bread = asn1_bio_read,
	.bputs = asn1_bio_puts,
	.bgets = asn1_bio_gets,
	.ctrl = asn1_bio_ctrl,
	.create = asn1_bio_new,
	.destroy = asn1_bio_free,
	.callback_ctrl = asn1_bio_callback_ctrl
};

const BIO_METHOD *
BIO_f_asn1(void)
{
	return (&methods_asn1);
}

static int
asn1_bio_new(BIO *b)
{
	BIO_ASN1_BUF_CTX *ctx;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return 0;

	if ((ctx->buf = malloc(DEFAULT_ASN1_BUF_SIZE)) == NULL) {
		free(ctx);
		return 0;
	}
	ctx->bufsize = DEFAULT_ASN1_BUF_SIZE;
	ctx->asn1_class = V_ASN1_UNIVERSAL;
	ctx->asn1_tag = V_ASN1_OCTET_STRING;
	ctx->state = ASN1_STATE_START;

	b->init = 1;
	b->ptr = ctx;
	b->flags = 0;

	return 1;
}

static int
asn1_bio_free(BIO *b)
{
	BIO_ASN1_BUF_CTX *ctx = b->ptr;

	if (ctx == NULL)
		return 0;

	if (ctx->prefix_free != NULL)
		ctx->prefix_free(b, &ctx->ex_buf, &ctx->ex_len, &ctx->ex_arg);
	if (ctx->suffix_free != NULL)
		ctx->suffix_free(b, &ctx->ex_buf, &ctx->ex_len, &ctx->ex_arg);

	free(ctx->buf);
	free(ctx);
	b->init = 0;
	b->ptr = NULL;
	b->flags = 0;
	return 1;
}

static int
asn1_bio_write(BIO *b, const char *in , int inl)
{
	BIO_ASN1_BUF_CTX *ctx;
	int wrmax, wrlen, ret, buflen;
	unsigned char *p;

	if (!in || (inl < 0) || (b->next_bio == NULL))
		return 0;

	if ((ctx = b->ptr) == NULL)
		return 0;

	wrlen = 0;
	ret = -1;

	for (;;) {
		switch (ctx->state) {

			/* Setup prefix data, call it */
		case ASN1_STATE_START:
			if (!asn1_bio_setup_ex(b, ctx, ctx->prefix,
				    ASN1_STATE_PRE_COPY, ASN1_STATE_HEADER))
				return 0;
			break;

			/* Copy any pre data first */
		case ASN1_STATE_PRE_COPY:
			ret = asn1_bio_flush_ex(b, ctx, ctx->prefix_free,
			    ASN1_STATE_HEADER);
			if (ret <= 0)
				goto done;
			break;

		case ASN1_STATE_HEADER:
			buflen = ASN1_object_size(0, inl, ctx->asn1_tag) - inl;
			if (buflen <= 0 || buflen > ctx->bufsize)
				return -1;
			ctx->buflen = buflen;
			p = ctx->buf;
			ASN1_put_object(&p, 0, inl,
			    ctx->asn1_tag, ctx->asn1_class);
			ctx->copylen = inl;
			ctx->state = ASN1_STATE_HEADER_COPY;
			break;

		case ASN1_STATE_HEADER_COPY:
			ret = BIO_write(b->next_bio,
			    ctx->buf + ctx->bufpos, ctx->buflen);
			if (ret <= 0)
				goto done;

			ctx->buflen -= ret;
			if (ctx->buflen)
				ctx->bufpos += ret;
			else {
				ctx->bufpos = 0;
				ctx->state = ASN1_STATE_DATA_COPY;
			}
			break;

		case ASN1_STATE_DATA_COPY:

			if (inl > ctx->copylen)
				wrmax = ctx->copylen;
			else
				wrmax = inl;
			ret = BIO_write(b->next_bio, in, wrmax);
			if (ret <= 0)
				goto done;
			wrlen += ret;
			ctx->copylen -= ret;
			in += ret;
			inl -= ret;

			if (ctx->copylen == 0)
				ctx->state = ASN1_STATE_HEADER;
			if (inl == 0)
				goto done;
			break;

		default:
			BIO_clear_retry_flags(b);
			return 0;
		}

	}

 done:
	BIO_clear_retry_flags(b);
	BIO_copy_next_retry(b);

	return (wrlen > 0) ? wrlen : ret;
}

static int
asn1_bio_flush_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx, asn1_ps_func *cleanup,
    asn1_bio_state_t next)
{
	int ret;

	if (ctx->ex_len <= 0)
		return 1;
	for (;;) {
		ret = BIO_write(b->next_bio, ctx->ex_buf + ctx->ex_pos,
		    ctx->ex_len);
		if (ret <= 0)
			break;
		ctx->ex_len -= ret;
		if (ctx->ex_len > 0)
			ctx->ex_pos += ret;
		else {
			if (cleanup)
				cleanup(b, &ctx->ex_buf, &ctx->ex_len,
				    &ctx->ex_arg);
			ctx->state = next;
			ctx->ex_pos = 0;
			break;
		}
	}
	return ret;
}

static int
asn1_bio_setup_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx, asn1_ps_func *setup,
    asn1_bio_state_t ex_state, asn1_bio_state_t other_state)
{
	if (setup && !setup(b, &ctx->ex_buf, &ctx->ex_len, &ctx->ex_arg)) {
		BIO_clear_retry_flags(b);
		return 0;
	}
	if (ctx->ex_len > 0)
		ctx->state = ex_state;
	else
		ctx->state = other_state;
	return 1;
}

static int
asn1_bio_read(BIO *b, char *in , int inl)
{
	if (!b->next_bio)
		return 0;
	return BIO_read(b->next_bio, in , inl);
}

static int
asn1_bio_puts(BIO *b, const char *str)
{
	return asn1_bio_write(b, str, strlen(str));
}

static int
asn1_bio_gets(BIO *b, char *str, int size)
{
	if (!b->next_bio)
		return 0;
	return BIO_gets(b->next_bio, str , size);
}

static long
asn1_bio_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
	if (b->next_bio == NULL)
		return (0);
	return BIO_callback_ctrl(b->next_bio, cmd, fp);
}

static long
asn1_bio_ctrl(BIO *b, int cmd, long arg1, void *arg2)
{
	BIO_ASN1_BUF_CTX *ctx;
	BIO_ASN1_EX_FUNCS *ex_func;
	long ret = 1;

	if ((ctx = b->ptr) == NULL)
		return 0;
	switch (cmd) {

	case BIO_C_SET_PREFIX:
		ex_func = arg2;
		ctx->prefix = ex_func->ex_func;
		ctx->prefix_free = ex_func->ex_free_func;
		break;

	case BIO_C_SET_SUFFIX:
		ex_func = arg2;
		ctx->suffix = ex_func->ex_func;
		ctx->suffix_free = ex_func->ex_free_func;
		break;

	case BIO_C_SET_EX_ARG:
		ctx->ex_arg = arg2;
		break;

	case BIO_C_GET_EX_ARG:
		*(void **)arg2 = ctx->ex_arg;
		break;

	case BIO_CTRL_FLUSH:
		if (!b->next_bio)
			return 0;

		/* Call post function if possible */
		if (ctx->state == ASN1_STATE_HEADER) {
			if (!asn1_bio_setup_ex(b, ctx, ctx->suffix,
			    ASN1_STATE_POST_COPY, ASN1_STATE_DONE))
				return 0;
		}

		if (ctx->state == ASN1_STATE_POST_COPY) {
			ret = asn1_bio_flush_ex(b, ctx, ctx->suffix_free,
			    ASN1_STATE_DONE);
			if (ret <= 0)
				return ret;
		}

		if (ctx->state == ASN1_STATE_DONE)
			return BIO_ctrl(b->next_bio, cmd, arg1, arg2);
		else {
			BIO_clear_retry_flags(b);
			return 0;
		}
		break;


	default:
		if (!b->next_bio)
			return 0;
		return BIO_ctrl(b->next_bio, cmd, arg1, arg2);

	}

	return ret;
}

static int
asn1_bio_set_ex(BIO *b, int cmd, asn1_ps_func *ex_func, asn1_ps_func
    *ex_free_func)
{
	BIO_ASN1_EX_FUNCS extmp;

	extmp.ex_func = ex_func;
	extmp.ex_free_func = ex_free_func;
	return BIO_ctrl(b, cmd, 0, &extmp);
}

int
BIO_asn1_set_prefix(BIO *b, asn1_ps_func *prefix, asn1_ps_func *prefix_free)
{
	return asn1_bio_set_ex(b, BIO_C_SET_PREFIX, prefix, prefix_free);
}

int
BIO_asn1_set_suffix(BIO *b, asn1_ps_func *suffix, asn1_ps_func *suffix_free)
{
	return asn1_bio_set_ex(b, BIO_C_SET_SUFFIX, suffix, suffix_free);
}
