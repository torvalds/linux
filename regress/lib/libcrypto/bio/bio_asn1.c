/*	$OpenBSD: bio_asn1.c,v 1.5 2023/07/21 20:22:47 tb Exp $ */

/*
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pkcs7.h>

#include "asn1_local.h"

/*
 * Minimal reproducer for the BIO_new_NDEF() write after free fixed in
 * bio_ndef.c r1.13.
 */

static int
waf_cb(int op, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	return 0;
}

static const ASN1_AUX WAF_aux = {
	.asn1_cb = waf_cb,
};

static const ASN1_ITEM WAF_it = {
	.funcs = &WAF_aux,
};

static int
test_bio_new_ndef_waf(void)
{
	BIO *out = NULL;
	int failed = 1;

	if ((out = BIO_new(BIO_s_mem())) == NULL)
		goto err;

	/*
	 * BIO_new_NDEF() pushes out onto asn_bio. The waf_cb() call fails.
	 * Prior to bio_ndef.c r1.13, asn_bio was freed and out->prev_bio
	 * still pointed to it.
	 */

	if (BIO_new_NDEF(out, NULL, &WAF_it) != NULL) {
		fprintf(stderr, "%s: BIO_new_NDEF succeeded\n", __func__);
		goto err;
	}

	/*
	 * If out->prev_bio != NULL, this writes to out->prev_bio->next_bio.
	 * After bio_ndef.c r1.13, out is an isolated BIO, so this is a noop.
	 */

	BIO_pop(out);

	failed = 0;

 err:
	BIO_free(out);

	return failed;
}

/*
 * test_prefix_leak() leaks before asn/bio_asn1.c r1.19.
 */

static long
read_leak_cb(BIO *bio, int cmd, const char *argp, int argi, long argl, long ret)
{
	int read_return = BIO_CB_READ | BIO_CB_RETURN;
	char *set_me;

	if ((cmd & read_return) != read_return)
		return ret;

	set_me = BIO_get_callback_arg(bio);
	*set_me = 1;

	return 0;
}

static int
test_prefix_leak(void)
{
	BIO *bio_in = NULL, *bio_out = NULL;
	PKCS7 *pkcs7 = NULL;
	char set_me = 0;
	int failed = 1;

	if ((bio_in = BIO_new_mem_buf("some data\n", -1)) == NULL)
		goto err;

	BIO_set_callback(bio_in, read_leak_cb);
	BIO_set_callback_arg(bio_in, &set_me);

	if ((pkcs7 = PKCS7_new()) == NULL)
		goto err;
	if (!PKCS7_set_type(pkcs7, NID_pkcs7_data))
		goto err;

	if ((bio_out = BIO_new(BIO_s_mem())) == NULL)
		goto err;

	if (!i2d_PKCS7_bio_stream(bio_out, pkcs7, bio_in,
	    SMIME_STREAM | SMIME_BINARY))
		goto err;

	if (set_me != 1) {
		fprintf(stderr, "%s: read_leak_cb didn't set set_me", __func__);
		goto err;
	}

	failed = 0;

 err:
	BIO_free(bio_in);
	BIO_free(bio_out);
	PKCS7_free(pkcs7);

	return failed;
}

/*
 * test_infinite_loop() would hang before asn/bio_asn1.c r1.18.
 */

#define SENTINEL (-57)

static long
inf_loop_cb(BIO *bio, int cmd, const char *argp, int argi, long argl, long ret)
{
	int write_return = BIO_CB_WRITE | BIO_CB_RETURN;
	char *set_me;

	if ((cmd & write_return) != write_return)
		return ret;

	set_me = BIO_get_callback_arg(bio);

	/* First time around: ASN1_STATE_HEADER_COPY - succeed. */
	if (*set_me == 0) {
		*set_me = 1;
		return ret;
	}

	/* Second time around: ASN1_STATE_DATA_COPY - return sentinel value. */
	if (*set_me == 1) {
		*set_me = 2;
		return SENTINEL;
	}

	/* Everything else is unexpected: return EOF. */
	*set_me = 3;

	return 0;

}

static int
test_infinite_loop(void)
{
	BIO *asn_bio = NULL, *bio = NULL;
	char set_me = 0;
	int failed = 1;
	int write_ret;

	if ((asn_bio = BIO_new(BIO_f_asn1())) == NULL)
		goto err;

	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		goto err;

	BIO_set_callback(bio, inf_loop_cb);
	BIO_set_callback_arg(bio, &set_me);

	if (BIO_push(asn_bio, bio) == NULL) {
		BIO_free(bio);
		goto err;
	}

	if ((write_ret = BIO_write(asn_bio, "foo", 3)) != SENTINEL) {
		fprintf(stderr, "%s: BIO_write: want %d, got %d", __func__,
		    SENTINEL, write_ret);
		goto err;
	}

	if (set_me != 2) {
		fprintf(stderr, "%s: set_me: %d != 2", __func__, set_me);
		goto err;
	}

	failed = 0;
 err:
	BIO_free_all(asn_bio);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_bio_new_ndef_waf();
	failed |= test_prefix_leak();
	failed |= test_infinite_loop();

	return failed;
}
