/*	$OpenBSD: cipher_list.c,v 1.14 2022/12/17 16:05:28 jsing Exp $	*/
/*
 * Copyright (c) 2015 Doug Hogan <doug@openbsd.org>
 * Copyright (c) 2015 Joel Sing <jsing@openbsd.org>
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

/*
 * Test TLS ssl bytes (aka cipher suites) to cipher list and back.
 *
 * TLSv1.0 - RFC 2246 section 7.4.1.2 (ClientHello struct)
 * TLSv1.1 - RFC 4346 section 7.4.1.2 (ClientHello struct)
 * TLSv1.2 - RFC 5246 section 7.4.1.2 (ClientHello struct)
 *
 * In all of these standards, the relevant structures are:
 *
 * uint8 CipherSuite[2];
 *
 * struct {
 *    ...
 *    CipherSuite cipher_suites<2..2^16-2>
 *    ...
 * } ClientHello;
 */

#include <openssl/ssl.h>

#include <stdio.h>
#include <string.h>

#include "ssl_local.h"

#include "tests.h"

static uint8_t cipher_bytes[] = {
	0xcc, 0xa8,	/* ECDHE-ECDSA-CHACHA20-POLY1305 */
	0xcc, 0xa9,	/* ECDHE-RSA-CHACHA20-POLY1305 */
	0xcc, 0xaa,	/* DHE-RSA-CHACHA20-POLY1305 */
	0x00, 0x9c,	/* AES128-GCM-SHA256 */
	0x00, 0x3d,	/* AES256-SHA256 */
};

static uint8_t cipher_bytes_seclevel3[] = {
	0xcc, 0xa8,	/* ECDHE-ECDSA-CHACHA20-POLY1305 */
	0xcc, 0xa9,	/* ECDHE-RSA-CHACHA20-POLY1305 */
	0xcc, 0xaa,	/* DHE-RSA-CHACHA20-POLY1305 */
};

static uint16_t cipher_values[] = {
	0xcca8,		/* ECDHE-ECDSA-CHACHA20-POLY1305 */
	0xcca9,		/* ECDHE-RSA-CHACHA20-POLY1305 */
	0xccaa,		/* DHE-RSA-CHACHA20-POLY1305 */
	0x009c,		/* AES128-GCM-SHA256 */
	0x003d,		/* AES256-SHA256 */
};

#define N_CIPHERS (sizeof(cipher_bytes) / 2)

static int
ssl_bytes_to_list_alloc(SSL *s, STACK_OF(SSL_CIPHER) **ciphers)
{
	SSL_CIPHER *cipher;
	uint16_t value;
	CBS cbs;
	int i;

	CBS_init(&cbs, cipher_bytes, sizeof(cipher_bytes));

	*ciphers = ssl_bytes_to_cipher_list(s, &cbs);
	CHECK(*ciphers != NULL);
	CHECK(sk_SSL_CIPHER_num(*ciphers) == N_CIPHERS);
	for (i = 0; i < sk_SSL_CIPHER_num(*ciphers); i++) {
		cipher = sk_SSL_CIPHER_value(*ciphers, i);
		CHECK(cipher != NULL);
		value = SSL_CIPHER_get_value(cipher);
		CHECK(value == cipher_values[i]);
	}

	return 1;
}

static int
ssl_list_to_bytes_scsv(SSL *s, STACK_OF(SSL_CIPHER) **ciphers,
    const uint8_t *cb, size_t cb_len)
{
	CBB cbb;
	unsigned char *buf = NULL;
	size_t buflen, outlen;
	int ret = 0;

	/* Space for cipher bytes, plus reneg SCSV and two spare bytes. */
	CHECK(sk_SSL_CIPHER_num(*ciphers) == N_CIPHERS);
	buflen = cb_len + 2 + 2;
	CHECK((buf = calloc(1, buflen)) != NULL);

	/* Clear renegotiate so it adds SCSV */
	s->renegotiate = 0;

	CHECK_GOTO(CBB_init_fixed(&cbb, buf, buflen));
	CHECK_GOTO(ssl_cipher_list_to_bytes(s, *ciphers, &cbb));
	CHECK_GOTO(CBB_finish(&cbb, NULL, &outlen));

	CHECK_GOTO(outlen > 0 && outlen == cb_len + 2);
	CHECK_GOTO(memcmp(buf, cb, cb_len) == 0);
	CHECK_GOTO(buf[buflen - 4] == 0x00 && buf[buflen - 3] == 0xff);
	CHECK_GOTO(buf[buflen - 2] == 0x00 && buf[buflen - 1] == 0x00);

	ret = 1;

 err:
	free(buf);
	return ret;
}

static int
ssl_list_to_bytes_no_scsv(SSL *s, STACK_OF(SSL_CIPHER) **ciphers,
    const uint8_t *cb, size_t cb_len)
{
	CBB cbb;
	unsigned char *buf = NULL;
	size_t buflen, outlen;
	int ret = 0;

	/* Space for cipher bytes and two spare bytes */
	CHECK(sk_SSL_CIPHER_num(*ciphers) == N_CIPHERS);
	buflen = cb_len + 2;
	CHECK((buf = calloc(1, buflen)) != NULL);
	buf[buflen - 2] = 0xfe;
	buf[buflen - 1] = 0xab;

	/* Set renegotiate so it doesn't add SCSV */
	s->renegotiate = 1;

	CHECK_GOTO(CBB_init_fixed(&cbb, buf, buflen));
	CHECK_GOTO(ssl_cipher_list_to_bytes(s, *ciphers, &cbb));
	CHECK_GOTO(CBB_finish(&cbb, NULL, &outlen));

	CHECK_GOTO(outlen > 0 && outlen == cb_len);
	CHECK_GOTO(memcmp(buf, cb, cb_len) == 0);
	CHECK_GOTO(buf[buflen - 2] == 0xfe && buf[buflen - 1] == 0xab);

	ret = 1;

 err:
	free(buf);
	return ret;
}

static int
ssl_bytes_to_list_invalid(SSL *s, STACK_OF(SSL_CIPHER) **ciphers)
{
	uint8_t empty_cipher_bytes[] = {0};
	CBS cbs;

	sk_SSL_CIPHER_free(*ciphers);

	/* Invalid length: CipherSuite is 2 bytes so it must be even */
	CBS_init(&cbs, cipher_bytes, sizeof(cipher_bytes) - 1);
	*ciphers = ssl_bytes_to_cipher_list(s, &cbs);
	CHECK(*ciphers == NULL);

	/* Invalid length: cipher_suites must be at least 2 */
	CBS_init(&cbs, empty_cipher_bytes, sizeof(empty_cipher_bytes));
	*ciphers = ssl_bytes_to_cipher_list(s, &cbs);
	CHECK(*ciphers == NULL);

	return 1;
}

int
main(void)
{
	STACK_OF(SSL_CIPHER) *ciphers = NULL;
	SSL_CTX *ctx = NULL;
	SSL *s = NULL;
	int rv = 1;

	SSL_library_init();

	/* Use TLSv1.2 client to get all ciphers. */
	CHECK_GOTO((ctx = SSL_CTX_new(TLSv1_2_client_method())) != NULL);
	CHECK_GOTO((s = SSL_new(ctx)) != NULL);
	SSL_set_security_level(s, 2);

	if (!ssl_bytes_to_list_alloc(s, &ciphers))
		goto err;
	if (!ssl_list_to_bytes_scsv(s, &ciphers, cipher_bytes,
	    sizeof(cipher_bytes)))
		goto err;
	if (!ssl_list_to_bytes_no_scsv(s, &ciphers, cipher_bytes,
	    sizeof(cipher_bytes)))
		goto err;
	if (!ssl_bytes_to_list_invalid(s, &ciphers))
		goto err;

	sk_SSL_CIPHER_free(ciphers);
	ciphers = NULL;

	SSL_set_security_level(s, 3);
	if (!ssl_bytes_to_list_alloc(s, &ciphers))
		goto err;
	if (!ssl_list_to_bytes_scsv(s, &ciphers, cipher_bytes_seclevel3,
	    sizeof(cipher_bytes_seclevel3)))
		goto err;
	if (!ssl_list_to_bytes_no_scsv(s, &ciphers, cipher_bytes_seclevel3,
	    sizeof(cipher_bytes_seclevel3)))
		goto err;

	rv = 0;

 err:
	sk_SSL_CIPHER_free(ciphers);
	SSL_CTX_free(ctx);
	SSL_free(s);

	if (!rv)
		printf("PASS %s\n", __FILE__);

	return rv;
}
