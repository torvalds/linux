/* $OpenBSD: tlsexttest.c,v 1.94 2025/05/03 08:37:28 tb Exp $ */
/*
 * Copyright (c) 2017 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2017 Doug Hogan <doug@openbsd.org>
 * Copyright (c) 2019 Bob Beck <beck@openbsd.org>
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <openssl/tls1.h>

#include "ssl_local.h"

#include "bytestring.h"
#include "ssl_tlsext.h"

struct tls_extension_funcs {
	int (*needs)(SSL *s, uint16_t msg_type);
	int (*build)(SSL *s, uint16_t msg_type, CBB *cbb);
	int (*process)(SSL *s, uint16_t msg_type, CBS *cbs, int *alert);
};

uint16_t tls_extension_type(const struct tls_extension *);
const struct tls_extension *tls_extension_find(uint16_t, size_t *);
const struct tls_extension_funcs *tlsext_funcs(const struct tls_extension *,
    int);
int tlsext_linearize_build_order(SSL *);

static int
tls_extension_funcs(int type, const struct tls_extension_funcs **client_funcs,
    const struct tls_extension_funcs **server_funcs)
{
	const struct tls_extension *ext;
	size_t idx;

	if ((ext = tls_extension_find(type, &idx)) == NULL)
		return 0;

	if ((*client_funcs = tlsext_funcs(ext, 0)) == NULL)
		return 0;

	if ((*server_funcs = tlsext_funcs(ext, 1)) == NULL)
		return 0;

	return 1;
}

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static void
hexdump2(const uint16_t *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len / 2; i++)
		fprintf(stderr, " 0x%04hx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static void
compare_data(const uint8_t *recv, size_t recv_len, const uint8_t *expect,
    size_t expect_len)
{
	fprintf(stderr, "received:\n");
	hexdump(recv, recv_len);

	fprintf(stderr, "test data:\n");
	hexdump(expect, expect_len);
}

static void
compare_data2(const uint16_t *recv, size_t recv_len, const uint16_t *expect,
    size_t expect_len)
{
	fprintf(stderr, "received:\n");
	hexdump2(recv, recv_len);

	fprintf(stderr, "test data:\n");
	hexdump2(expect, expect_len);
}

#define FAIL(msg, ...)						\
do {								\
	fprintf(stderr, "[%s:%d] FAIL: ", __FILE__, __LINE__);	\
	fprintf(stderr, msg, ##__VA_ARGS__);			\
} while(0)

/*
 * Supported Application-Layer Protocol Negotiation - RFC 7301
 *
 * There are already extensive unit tests for this so this just
 * tests the state info.
 */

const uint8_t tlsext_alpn_multiple_protos_val[] = {
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
	/* opaque ProtocolName<1..2^8-1> -- 'stun.nat' */
	0x09, /* len */
	0x73, 0x74, 0x75, 0x6e, 0x2e, 0x74, 0x75, 0x72, 0x6e
};

const uint8_t tlsext_alpn_multiple_protos[] = {
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x13, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
	/* opaque ProtocolName<1..2^8-1> -- 'stun.nat' */
	0x09, /* len */
	0x73, 0x74, 0x75, 0x6e, 0x2e, 0x74, 0x75, 0x72, 0x6e
};

const uint8_t tlsext_alpn_single_proto_val[] = {
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31
};

const uint8_t tlsext_alpn_single_proto_name[] = {
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31 /* 'http/1.1' */
};

const uint8_t tlsext_alpn_single_proto[] = {
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x09, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31
};

#define TLSEXT_TYPE_alpn TLSEXT_TYPE_application_layer_protocol_negotiation

static int
test_tlsext_alpn_client(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	uint8_t *data = NULL;
	CBB cbb;
	CBS cbs;
	int failure, alert;
	size_t dlen;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_alpn, &client_funcs, &server_funcs))
		errx(1, "failed to fetch ALPN funcs");

	/* By default, we don't need this */
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need ALPN by default\n");
		goto err;
	}

	/*
	 * Prereqs:
	 * 1) Set s->alpn_client_proto_list
	 *    - Using SSL_set_alpn_protos()
	 * 2) We have not finished or renegotiated.
	 *    - s->s3->tmp.finish_md_len == 0
	 */
	if (SSL_set_alpn_protos(ssl, tlsext_alpn_single_proto_val,
	    sizeof(tlsext_alpn_single_proto_val)) != 0) {
		FAIL("should be able to set ALPN to http/1.1\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need ALPN by default\n");
		goto err;
	}

	/* Make sure we can build the client with a single proto. */

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build ALPN\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_alpn_single_proto)) {
		FAIL("got client ALPN with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_alpn_single_proto));
		compare_data(data, dlen, tlsext_alpn_single_proto,
		    sizeof(tlsext_alpn_single_proto));
		goto err;
	}
	if (memcmp(data, tlsext_alpn_single_proto, dlen) != 0) {
		FAIL("client ALPN differs:\n");
		compare_data(data, dlen, tlsext_alpn_single_proto,
		    sizeof(tlsext_alpn_single_proto));
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	/* Make sure we can parse the single proto. */

	CBS_init(&cbs, tlsext_alpn_single_proto,
	    sizeof(tlsext_alpn_single_proto));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse ALPN\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->alpn_client_proto_list_len !=
	    sizeof(tlsext_alpn_single_proto_val)) {
		FAIL("got client ALPN with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_alpn_single_proto_val));
		compare_data(ssl->alpn_client_proto_list,
		    ssl->alpn_client_proto_list_len,
		    tlsext_alpn_single_proto_val,
		    sizeof(tlsext_alpn_single_proto_val));
		goto err;
	}
	if (memcmp(ssl->alpn_client_proto_list,
	    tlsext_alpn_single_proto_val,
	    sizeof(tlsext_alpn_single_proto_val)) != 0) {
		FAIL("client ALPN differs:\n");
		compare_data(data, dlen, tlsext_alpn_single_proto_val,
		    sizeof(tlsext_alpn_single_proto_val));
		goto err;
	}

	/* Make sure we can build the clienthello with multiple entries. */

	if (SSL_set_alpn_protos(ssl, tlsext_alpn_multiple_protos_val,
	    sizeof(tlsext_alpn_multiple_protos_val)) != 0) {
		FAIL("should be able to set ALPN to http/1.1\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need ALPN by now\n");
		goto err;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build ALPN\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_alpn_multiple_protos)) {
		FAIL("got client ALPN with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_alpn_multiple_protos));
		compare_data(data, dlen, tlsext_alpn_multiple_protos,
		    sizeof(tlsext_alpn_multiple_protos));
		goto err;
	}
	if (memcmp(data, tlsext_alpn_multiple_protos, dlen) != 0) {
		FAIL("client ALPN differs:\n");
		compare_data(data, dlen, tlsext_alpn_multiple_protos,
		    sizeof(tlsext_alpn_multiple_protos));
		goto err;
	}

	/* Make sure we can parse multiple protos */

	CBS_init(&cbs, tlsext_alpn_multiple_protos,
	    sizeof(tlsext_alpn_multiple_protos));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse ALPN\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->alpn_client_proto_list_len !=
	    sizeof(tlsext_alpn_multiple_protos_val)) {
		FAIL("got client ALPN with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_alpn_multiple_protos_val));
		compare_data(ssl->alpn_client_proto_list,
		    ssl->alpn_client_proto_list_len,
		    tlsext_alpn_multiple_protos_val,
		    sizeof(tlsext_alpn_multiple_protos_val));
		goto err;
	}
	if (memcmp(ssl->alpn_client_proto_list,
	    tlsext_alpn_multiple_protos_val,
	    sizeof(tlsext_alpn_multiple_protos_val)) != 0) {
		FAIL("client ALPN differs:\n");
		compare_data(data, dlen, tlsext_alpn_multiple_protos_val,
		    sizeof(tlsext_alpn_multiple_protos_val));
		goto err;
	}

	/* Make sure we can remove the list and avoid ALPN */

	free(ssl->alpn_client_proto_list);
	ssl->alpn_client_proto_list = NULL;
	ssl->alpn_client_proto_list_len = 0;

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need ALPN by default\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_alpn_server(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	uint8_t *data = NULL;
	CBB cbb;
	CBS cbs;
	int failure, alert;
	size_t dlen;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_alpn, &client_funcs, &server_funcs))
		errx(1, "failed to fetch ALPN funcs");

	/* By default, ALPN isn't needed. */
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need ALPN by default\n");
		goto err;
	}

	/*
	 * The server has a single ALPN selection which is set by
	 * SSL_CTX_set_alpn_select_cb() and calls SSL_select_next_proto().
	 *
	 * This will be a plain name and separate length.
	 */
	if ((ssl->s3->alpn_selected = malloc(sizeof(tlsext_alpn_single_proto_name))) == NULL) {
		errx(1, "failed to malloc");
	}
	memcpy(ssl->s3->alpn_selected, tlsext_alpn_single_proto_name,
	    sizeof(tlsext_alpn_single_proto_name));
	ssl->s3->alpn_selected_len = sizeof(tlsext_alpn_single_proto_name);

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need ALPN after a protocol is selected\n");
		goto err;
	}

	/* Make sure we can build a server with one protocol */

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server should be able to build a response\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_alpn_single_proto)) {
		FAIL("got client ALPN with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_alpn_single_proto));
		compare_data(data, dlen, tlsext_alpn_single_proto,
		    sizeof(tlsext_alpn_single_proto));
		goto err;
	}
	if (memcmp(data, tlsext_alpn_single_proto, dlen) != 0) {
		FAIL("client ALPN differs:\n");
		compare_data(data, dlen, tlsext_alpn_single_proto,
		    sizeof(tlsext_alpn_single_proto));
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	/* Make sure we can parse the single proto. */

	CBS_init(&cbs, tlsext_alpn_single_proto,
	    sizeof(tlsext_alpn_single_proto));

	/* Shouldn't be able to parse without requesting */
	if (client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("Should only parse server if we requested it\n");
		goto err;
	}

	/* Should be able to parse once requested. */
	if (SSL_set_alpn_protos(ssl, tlsext_alpn_single_proto_val,
	    sizeof(tlsext_alpn_single_proto_val)) != 0) {
		FAIL("should be able to set ALPN to http/1.1\n");
		goto err;
	}
	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("Should be able to parse server when we request it\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->s3->alpn_selected_len !=
	    sizeof(tlsext_alpn_single_proto_name)) {
		FAIL("got server ALPN with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_alpn_single_proto_name));
		compare_data(ssl->s3->alpn_selected,
		    ssl->s3->alpn_selected_len,
		    tlsext_alpn_single_proto_name,
		    sizeof(tlsext_alpn_single_proto_name));
		goto err;
	}
	if (memcmp(ssl->s3->alpn_selected,
	    tlsext_alpn_single_proto_name,
	    sizeof(tlsext_alpn_single_proto_name)) != 0) {
		FAIL("server ALPN differs:\n");
		compare_data(ssl->s3->alpn_selected,
		    ssl->s3->alpn_selected_len,
		    tlsext_alpn_single_proto_name,
		    sizeof(tlsext_alpn_single_proto_name));
		goto err;
	}

	/*
	 * We should NOT be able to build a server with multiple
	 * protocol names.  However, the existing code did not check for this
	 * case because it is passed in as an encoded value.
	 */

	/* Make sure we can remove the list and avoid ALPN */

	free(ssl->s3->alpn_selected);
	ssl->s3->alpn_selected = NULL;
	ssl->s3->alpn_selected_len = 0;

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need ALPN by default\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);

}

/*
 * Supported Elliptic Curves - RFC 4492 section 5.1.1.
 *
 * This extension is only used by the client.
 */

static const uint8_t tlsext_supportedgroups_client_default[] = {
	0x00, 0x08,
	0x00, 0x1d,  /* X25519 (29) */
	0x00, 0x17,  /* secp256r1 (23) */
	0x00, 0x18,  /* secp384r1 (24) */
	0x00, 0x19,  /* secp521r1 (25) */
};

static const uint16_t tlsext_supportedgroups_client_secp384r1_val[] = {
	0x0018   /* tls1_ec_nid2group_id(NID_secp384r1) */
};
static const uint8_t tlsext_supportedgroups_client_secp384r1[] = {
	0x00, 0x02,
	0x00, 0x18  /* secp384r1 (24) */
};

/* Example from RFC 4492 section 5.1.1 */
static const uint16_t tlsext_supportedgroups_client_nistp192and224_val[] = {
	0x0013,  /* tls1_ec_nid2group_id(NID_X9_62_prime192v1) */
	0x0015   /* tls1_ec_nid2group_id(NID_secp224r1) */
};
static const uint8_t tlsext_supportedgroups_client_nistp192and224[] = {
	0x00, 0x04,
	0x00, 0x13, /* secp192r1 aka NIST P-192 */
	0x00, 0x15  /* secp224r1 aka NIST P-224 */
};

static int
test_tlsext_supportedgroups_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	size_t dlen;
	int failure, alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_supported_groups, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch supported groups funcs");

	/*
	 * Default ciphers include EC so we need it by default.
	 */
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need Ellipticcurves for default "
		    "ciphers\n");
		goto err;
	}

	/*
	 * Exclude cipher suites so we can test not including it.
	 */
	if (!SSL_set_cipher_list(ssl, "TLSv1.2:!ECDHE:!ECDSA")) {
		FAIL("client should be able to set cipher list\n");
		goto err;
	}
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need Ellipticcurves\n");
		goto err;
	}

	/*
	 * Use libtls default for the rest of the testing
	 */
	if (!SSL_set_cipher_list(ssl, "TLSv1.2+AEAD+ECDHE")) {
		FAIL("client should be able to set cipher list\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need Ellipticcurves\n");
		goto err;
	}

	/*
	 * Test with a session secp384r1.  The default is used instead.
	 */
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	if ((ssl->session->tlsext_supportedgroups = malloc(sizeof(uint16_t)))
	    == NULL) {
		FAIL("client could not malloc\n");
		goto err;
	}
	if (!tls1_ec_nid2group_id(NID_secp384r1,
	    &ssl->session->tlsext_supportedgroups[0]))
		goto err;
	ssl->session->tlsext_supportedgroups_length = 1;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need Ellipticcurves\n");
		goto err;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build Ellipticcurves\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_supportedgroups_client_default)) {
		FAIL("got client Ellipticcurves with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_supportedgroups_client_default));
		compare_data(data, dlen, tlsext_supportedgroups_client_default,
		    sizeof(tlsext_supportedgroups_client_default));
		goto err;
	}

	if (memcmp(data, tlsext_supportedgroups_client_default, dlen) != 0) {
		FAIL("client Ellipticcurves differs:\n");
		compare_data(data, dlen, tlsext_supportedgroups_client_default,
		    sizeof(tlsext_supportedgroups_client_default));
		goto err;
	}

	/*
	 * Test parsing secp384r1
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	CBS_init(&cbs, tlsext_supportedgroups_client_secp384r1,
	    sizeof(tlsext_supportedgroups_client_secp384r1));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client Ellipticcurves\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->session->tlsext_supportedgroups_length !=
	    sizeof(tlsext_supportedgroups_client_secp384r1_val) / sizeof(uint16_t)) {
		FAIL("no tlsext_ellipticcurves from client "
		    "Ellipticcurves\n");
		goto err;
	}

	if (memcmp(ssl->session->tlsext_supportedgroups,
	    tlsext_supportedgroups_client_secp384r1_val,
	    sizeof(tlsext_supportedgroups_client_secp384r1_val)) != 0) {
		FAIL("client had an incorrect Ellipticcurves "
		    "entry\n");
		compare_data2(ssl->session->tlsext_supportedgroups,
		    ssl->session->tlsext_supportedgroups_length * 2,
		    tlsext_supportedgroups_client_secp384r1_val,
		    sizeof(tlsext_supportedgroups_client_secp384r1_val));
		goto err;
	}

	/*
	 * Use a custom order.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	if ((ssl->tlsext_supportedgroups = malloc(sizeof(uint16_t) * 2)) == NULL) {
		FAIL("client could not malloc\n");
		goto err;
	}
	if (!tls1_ec_nid2group_id(NID_X9_62_prime192v1,
	    &ssl->tlsext_supportedgroups[0]))
		goto err;
	if (!tls1_ec_nid2group_id(NID_secp224r1,
	    &ssl->tlsext_supportedgroups[1]))
		goto err;
	ssl->tlsext_supportedgroups_length = 2;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need Ellipticcurves\n");
		goto err;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build Ellipticcurves\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_supportedgroups_client_nistp192and224)) {
		FAIL("got client Ellipticcurves with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_supportedgroups_client_nistp192and224));
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_supportedgroups_client_nistp192and224,
		    sizeof(tlsext_supportedgroups_client_nistp192and224));
		goto err;
	}

	if (memcmp(data, tlsext_supportedgroups_client_nistp192and224, dlen) != 0) {
		FAIL("client Ellipticcurves differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_supportedgroups_client_nistp192and224,
		    sizeof(tlsext_supportedgroups_client_nistp192and224));
		goto err;
	}

	/*
	 * Parse non-default curves to session.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	/* Reset back to the default list. */
	free(ssl->tlsext_supportedgroups);
	ssl->tlsext_supportedgroups = NULL;
	ssl->tlsext_supportedgroups_length = 0;

	CBS_init(&cbs, tlsext_supportedgroups_client_nistp192and224,
	    sizeof(tlsext_supportedgroups_client_nistp192and224));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client Ellipticcurves\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->session->tlsext_supportedgroups_length !=
	    sizeof(tlsext_supportedgroups_client_nistp192and224_val) / sizeof(uint16_t)) {
		FAIL("no tlsext_ellipticcurves from client Ellipticcurves\n");
		goto err;
	}

	if (memcmp(ssl->session->tlsext_supportedgroups,
	    tlsext_supportedgroups_client_nistp192and224_val,
	    sizeof(tlsext_supportedgroups_client_nistp192and224_val)) != 0) {
		FAIL("client had an incorrect Ellipticcurves entry\n");
		compare_data2(ssl->session->tlsext_supportedgroups,
		    ssl->session->tlsext_supportedgroups_length * 2,
		    tlsext_supportedgroups_client_nistp192and224_val,
		    sizeof(tlsext_supportedgroups_client_nistp192and224_val));
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}


/* elliptic_curves is only used by the client so this doesn't test much. */
static int
test_tlsext_supportedgroups_server(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;

	failure = 1;

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_supported_groups, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch supported groups funcs");

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need elliptic_curves\n");
		goto err;
	}

	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need elliptic_curves\n");
		goto err;
	}

	failure = 0;

 err:
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	return (failure);

}

/*
 * Supported Point Formats - RFC 4492 section 5.1.2.
 *
 * Examples are from the RFC.  Both client and server have the same build and
 * parse but the needs differ.
 */

static const uint8_t tlsext_ecpf_hello_uncompressed_val[] = {
	TLSEXT_ECPOINTFORMAT_uncompressed
};
static const uint8_t tlsext_ecpf_hello_uncompressed[] = {
	0x01,
	0x00 /* TLSEXT_ECPOINTFORMAT_uncompressed */
};

static const uint8_t tlsext_ecpf_hello_prime[] = {
	0x01,
	0x01 /* TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime */
};

static const uint8_t tlsext_ecpf_hello_prefer_order_val[] = {
	TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime,
	TLSEXT_ECPOINTFORMAT_uncompressed,
	TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2
};
static const uint8_t tlsext_ecpf_hello_prefer_order[] = {
	0x03,
	0x01, /* TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime */
	0x00, /* TLSEXT_ECPOINTFORMAT_uncompressed */
	0x02  /* TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2 */
};

static int
test_tlsext_ecpf_client(void)
{
	uint8_t *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	size_t dlen;
	int failure, alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_ec_point_formats, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch ecpf funcs");

	/*
	 * Default ciphers include EC so we need it by default.
	 */
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need ECPointFormats for default "
		    "ciphers\n");
		goto err;
	}

	/*
	 * Exclude EC cipher suites so we can test not including it.
	 */
	if (!SSL_set_cipher_list(ssl, "ALL:!ECDHE:!ECDH")) {
		FAIL("client should be able to set cipher list\n");
		goto err;
	}
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need ECPointFormats\n");
		goto err;
	}

	/*
	 * Use libtls default for the rest of the testing
	 */
	if (!SSL_set_cipher_list(ssl, "TLSv1.2+AEAD+ECDHE")) {
		FAIL("client should be able to set cipher list\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need ECPointFormats\n");
		goto err;
	}

	/*
	 * The default ECPointFormats should only have uncompressed
	 */
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build ECPointFormats\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_ecpf_hello_uncompressed)) {
		FAIL("got client ECPointFormats with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_ecpf_hello_uncompressed));
		compare_data(data, dlen, tlsext_ecpf_hello_uncompressed,
		    sizeof(tlsext_ecpf_hello_uncompressed));
		goto err;
	}

	if (memcmp(data, tlsext_ecpf_hello_uncompressed, dlen) != 0) {
		FAIL("client ECPointFormats differs:\n");
		compare_data(data, dlen, tlsext_ecpf_hello_uncompressed,
		    sizeof(tlsext_ecpf_hello_uncompressed));
		goto err;
	}

	/*
	 * Make sure we can parse the default.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	CBS_init(&cbs, tlsext_ecpf_hello_uncompressed,
	    sizeof(tlsext_ecpf_hello_uncompressed));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client ECPointFormats\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->session->tlsext_ecpointformatlist_length !=
	    sizeof(tlsext_ecpf_hello_uncompressed_val)) {
		FAIL("no tlsext_ecpointformats from client "
		    "ECPointFormats\n");
		goto err;
	}

	if (memcmp(ssl->session->tlsext_ecpointformatlist,
	    tlsext_ecpf_hello_uncompressed_val,
	    sizeof(tlsext_ecpf_hello_uncompressed_val)) != 0) {
		FAIL("client had an incorrect ECPointFormats entry\n");
		goto err;
	}

	/*
	 * Test with a custom order.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	if ((ssl->tlsext_ecpointformatlist = malloc(sizeof(uint8_t) * 3)) == NULL) {
		FAIL("client could not malloc\n");
		goto err;
	}
	ssl->tlsext_ecpointformatlist[0] = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime;
	ssl->tlsext_ecpointformatlist[1] = TLSEXT_ECPOINTFORMAT_uncompressed;
	ssl->tlsext_ecpointformatlist[2] = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2;
	ssl->tlsext_ecpointformatlist_length = 3;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need ECPointFormats with a custom "
		    "format\n");
		goto err;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build ECPointFormats\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_ecpf_hello_prefer_order)) {
		FAIL("got client ECPointFormats with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_ecpf_hello_prefer_order));
		compare_data(data, dlen, tlsext_ecpf_hello_prefer_order,
		    sizeof(tlsext_ecpf_hello_prefer_order));
		goto err;
	}

	if (memcmp(data, tlsext_ecpf_hello_prefer_order, dlen) != 0) {
		FAIL("client ECPointFormats differs:\n");
		compare_data(data, dlen, tlsext_ecpf_hello_prefer_order,
		    sizeof(tlsext_ecpf_hello_prefer_order));
		goto err;
	}

	/*
	 * Make sure that we can parse this custom order.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	/* Reset the custom list so we go back to the default uncompressed. */
	free(ssl->tlsext_ecpointformatlist);
	ssl->tlsext_ecpointformatlist = NULL;
	ssl->tlsext_ecpointformatlist_length = 0;

	CBS_init(&cbs, tlsext_ecpf_hello_prefer_order,
	    sizeof(tlsext_ecpf_hello_prefer_order));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client ECPointFormats\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->session->tlsext_ecpointformatlist_length !=
	    sizeof(tlsext_ecpf_hello_prefer_order_val)) {
		FAIL("no tlsext_ecpointformats from client "
		    "ECPointFormats\n");
		goto err;
	}

	if (memcmp(ssl->session->tlsext_ecpointformatlist,
	    tlsext_ecpf_hello_prefer_order_val,
	    sizeof(tlsext_ecpf_hello_prefer_order_val)) != 0) {
		FAIL("client had an incorrect ECPointFormats entry\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_ecpf_server(void)
{
	uint8_t *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	size_t dlen;
	int failure, alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_ec_point_formats, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch ecpf funcs");

	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	/* Setup the state so we can call needs. */
	if ((ssl->s3->hs.cipher = ssl3_get_cipher_by_value(0xcca9)) == NULL) {
		FAIL("server cannot find cipher\n");
		goto err;
	}
	if ((ssl->session->tlsext_ecpointformatlist = malloc(sizeof(uint8_t)))
	    == NULL) {
		FAIL("server could not malloc\n");
		goto err;
	}
	ssl->session->tlsext_ecpointformatlist[0] = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime;
	ssl->session->tlsext_ecpointformatlist_length = 1;

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need ECPointFormats now\n");
		goto err;
	}

	/*
	 * The server will ignore the session list and use either a custom
	 * list or the default (uncompressed).
	 */
	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server failed to build ECPointFormats\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_ecpf_hello_uncompressed)) {
		FAIL("got server ECPointFormats with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_ecpf_hello_uncompressed));
		compare_data(data, dlen, tlsext_ecpf_hello_uncompressed,
		    sizeof(tlsext_ecpf_hello_uncompressed));
		goto err;
	}

	if (memcmp(data, tlsext_ecpf_hello_uncompressed, dlen) != 0) {
		FAIL("server ECPointFormats differs:\n");
		compare_data(data, dlen, tlsext_ecpf_hello_uncompressed,
		    sizeof(tlsext_ecpf_hello_uncompressed));
		goto err;
	}

	/*
	 * Cannot parse a non-default list without at least uncompressed.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	CBS_init(&cbs, tlsext_ecpf_hello_prime,
	    sizeof(tlsext_ecpf_hello_prime));
	if (client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("must include uncompressed in server ECPointFormats\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	/*
	 * Test with a custom order that replaces the default uncompressed.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	/* Add a session list even though it will be ignored. */
	if ((ssl->session->tlsext_ecpointformatlist = malloc(sizeof(uint8_t)))
	    == NULL) {
		FAIL("server could not malloc\n");
		goto err;
	}
	ssl->session->tlsext_ecpointformatlist[0] = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2;
	ssl->session->tlsext_ecpointformatlist_length = 1;

	/* Replace the default list with a custom one. */
	if ((ssl->tlsext_ecpointformatlist = malloc(sizeof(uint8_t) * 3)) == NULL) {
		FAIL("server could not malloc\n");
		goto err;
	}
	ssl->tlsext_ecpointformatlist[0] = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime;
	ssl->tlsext_ecpointformatlist[1] = TLSEXT_ECPOINTFORMAT_uncompressed;
	ssl->tlsext_ecpointformatlist[2] = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2;
	ssl->tlsext_ecpointformatlist_length = 3;

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need ECPointFormats\n");
		goto err;
	}

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server failed to build ECPointFormats\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_ecpf_hello_prefer_order)) {
		FAIL("got server ECPointFormats with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_ecpf_hello_prefer_order));
		compare_data(data, dlen, tlsext_ecpf_hello_prefer_order,
		    sizeof(tlsext_ecpf_hello_prefer_order));
		goto err;
	}

	if (memcmp(data, tlsext_ecpf_hello_prefer_order, dlen) != 0) {
		FAIL("server ECPointFormats differs:\n");
		compare_data(data, dlen, tlsext_ecpf_hello_prefer_order,
		    sizeof(tlsext_ecpf_hello_prefer_order));
		goto err;
	}

	/*
	 * Should be able to parse the custom list into a session list.
	 */
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	/* Reset back to the default (uncompressed) */
	free(ssl->tlsext_ecpointformatlist);
	ssl->tlsext_ecpointformatlist = NULL;
	ssl->tlsext_ecpointformatlist_length = 0;

	CBS_init(&cbs, tlsext_ecpf_hello_prefer_order,
	    sizeof(tlsext_ecpf_hello_prefer_order));
	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("failed to parse server ECPointFormats\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->session->tlsext_ecpointformatlist_length !=
	    sizeof(tlsext_ecpf_hello_prefer_order_val)) {
		FAIL("no tlsext_ecpointformats from server "
		    "ECPointFormats\n");
		goto err;
	}

	if (memcmp(ssl->session->tlsext_ecpointformatlist,
	    tlsext_ecpf_hello_prefer_order_val,
	    sizeof(tlsext_ecpf_hello_prefer_order_val)) != 0) {
		FAIL("server had an incorrect ECPointFormats entry\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

/*
 * Renegotiation Indication - RFC 5746.
 */

static const unsigned char tlsext_ri_prev_client[] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
};

static const unsigned char tlsext_ri_prev_server[] = {
	0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
	0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
};

static const unsigned char tlsext_ri_client[] = {
	0x10,
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
};

static const unsigned char tlsext_ri_server[] = {
	0x20,
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
	0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
};

static int
test_tlsext_ri_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLSv1_2_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_renegotiate, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch ri funcs");

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need RI\n");
		goto err;
	}

	if (!SSL_renegotiate(ssl)) {
		FAIL("client failed to set renegotiate\n");
		goto err;
	}

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need RI\n");
		goto err;
	}

	memcpy(ssl->s3->previous_client_finished, tlsext_ri_prev_client,
	    sizeof(tlsext_ri_prev_client));
	ssl->s3->previous_client_finished_len = sizeof(tlsext_ri_prev_client);

	ssl->s3->renegotiate_seen = 0;

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build RI\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_ri_client)) {
		FAIL("got client RI with length %zu, "
		    "want length %zu\n", dlen, sizeof(tlsext_ri_client));
		goto err;
	}

	if (memcmp(data, tlsext_ri_client, dlen) != 0) {
		FAIL("client RI differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_ri_client, sizeof(tlsext_ri_client));
		goto err;
	}

	CBS_init(&cbs, tlsext_ri_client, sizeof(tlsext_ri_client));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client RI\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->s3->renegotiate_seen != 1) {
		FAIL("renegotiate seen not set\n");
		goto err;
	}
	if (ssl->s3->send_connection_binding != 1) {
		FAIL("send connection binding not set\n");
		goto err;
	}

	memset(ssl->s3->previous_client_finished, 0,
	    sizeof(ssl->s3->previous_client_finished));

	ssl->s3->renegotiate_seen = 0;

	CBS_init(&cbs, tlsext_ri_client, sizeof(tlsext_ri_client));
	if (server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("parsed invalid client RI\n");
		goto err;
	}

	if (ssl->s3->renegotiate_seen == 1) {
		FAIL("renegotiate seen set\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_ri_server(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_renegotiate, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch ri funcs");

	ssl->version = TLS1_2_VERSION;
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need RI\n");
		goto err;
	}

	ssl->s3->send_connection_binding = 1;

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need RI\n");
		goto err;
	}

	memcpy(ssl->s3->previous_client_finished, tlsext_ri_prev_client,
	    sizeof(tlsext_ri_prev_client));
	ssl->s3->previous_client_finished_len = sizeof(tlsext_ri_prev_client);

	memcpy(ssl->s3->previous_server_finished, tlsext_ri_prev_server,
	    sizeof(tlsext_ri_prev_server));
	ssl->s3->previous_server_finished_len = sizeof(tlsext_ri_prev_server);

	ssl->s3->renegotiate_seen = 0;

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server failed to build RI\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_ri_server)) {
		FAIL("got server RI with length %zu, "
		    "want length %zu\n", dlen, sizeof(tlsext_ri_server));
		goto err;
	}

	if (memcmp(data, tlsext_ri_server, dlen) != 0) {
		FAIL("server RI differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_ri_server, sizeof(tlsext_ri_server));
		goto err;
	}

	CBS_init(&cbs, tlsext_ri_server, sizeof(tlsext_ri_server));
	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("failed to parse server RI\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->s3->renegotiate_seen != 1) {
		FAIL("renegotiate seen not set\n");
		goto err;
	}
	if (ssl->s3->send_connection_binding != 1) {
		FAIL("send connection binding not set\n");
		goto err;
	}

	memset(ssl->s3->previous_client_finished, 0,
	    sizeof(ssl->s3->previous_client_finished));
	memset(ssl->s3->previous_server_finished, 0,
	    sizeof(ssl->s3->previous_server_finished));

	ssl->s3->renegotiate_seen = 0;

	CBS_init(&cbs, tlsext_ri_server, sizeof(tlsext_ri_server));
	if (client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("parsed invalid server RI\n");
		goto err;
	}

	if (ssl->s3->renegotiate_seen == 1) {
		FAIL("renegotiate seen set\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

/*
 * Signature Algorithms - RFC 5246 section 7.4.1.4.1.
 */

static const unsigned char tlsext_sigalgs_client[] = {
	0x00, 0x16, 0x08, 0x06, 0x06, 0x01, 0x06, 0x03,
	0x08, 0x05, 0x05, 0x01, 0x05, 0x03, 0x08, 0x04,
	0x04, 0x01, 0x04, 0x03, 0x02, 0x01, 0x02, 0x03,
};

static int
test_tlsext_sigalgs_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_signature_algorithms,
	    &client_funcs, &server_funcs))
		errx(1, "failed to fetch sigalgs funcs");

	ssl->s3->hs.our_max_tls_version = TLS1_1_VERSION;

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need sigalgs\n");
		goto done;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_2_VERSION;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need sigalgs\n");
		goto done;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build sigalgs\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_sigalgs_client)) {
		FAIL("got client sigalgs length %zu, "
		    "want length %zu\n", dlen, sizeof(tlsext_sigalgs_client));
		goto done;
	}

	if (memcmp(data, tlsext_sigalgs_client, dlen) != 0) {
		FAIL("client SNI differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_sigalgs_client, sizeof(tlsext_sigalgs_client));
		goto done;
	}

	CBS_init(&cbs, tlsext_sigalgs_client, sizeof(tlsext_sigalgs_client));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client SNI\n");
		goto done;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}

	failure = 0;

 done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

#if 0
static int
test_tlsext_sigalgs_server(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_server_name, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch sigalgs funcs");

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need sigalgs\n");
		goto done;
	}

	if (server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server should not build sigalgs\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	CBS_init(&cbs, tlsext_sigalgs_client, sizeof(tlsext_sigalgs_client));
	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("server should not parse sigalgs\n");
		goto done;
	}

	failure = 0;

 done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}
#endif

/*
 * Server Name Indication - RFC 6066 section 3.
 */

#define TEST_SNI_SERVERNAME "www.libressl.org"

static const unsigned char tlsext_sni_client[] = {
	0x00, 0x13, 0x00, 0x00, 0x10, 0x77, 0x77, 0x77,
	0x2e, 0x6c, 0x69, 0x62, 0x72, 0x65, 0x73, 0x73,
	0x6c, 0x2e, 0x6f, 0x72, 0x67,
};

/* An empty array is an incomplete type and sizeof() is undefined. */
static const unsigned char tlsext_sni_server[] = {
	0x00,
};
static size_t tlsext_sni_server_len = 0;

static int
test_tlsext_sni_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_server_name, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch sni funcs");

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need SNI\n");
		goto err;
	}

	if (!SSL_set_tlsext_host_name(ssl, TEST_SNI_SERVERNAME)) {
		FAIL("client failed to set server name\n");
		goto err;
	}

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need SNI\n");
		goto err;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build SNI\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB");
		goto err;
	}

	if (dlen != sizeof(tlsext_sni_client)) {
		FAIL("got client SNI with length %zu, "
		    "want length %zu\n", dlen, sizeof(tlsext_sni_client));
		goto err;
	}

	if (memcmp(data, tlsext_sni_client, dlen) != 0) {
		FAIL("client SNI differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_sni_client, sizeof(tlsext_sni_client));
		goto err;
	}

	/*
	 * SSL_set_tlsext_host_name() may be called with a NULL host name to
	 * disable SNI.
	 */
	if (!SSL_set_tlsext_host_name(ssl, NULL)) {
		FAIL("cannot set host name to NULL");
		goto err;
	}

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need SNI\n");
		goto err;
	}

	if ((ssl->session = SSL_SESSION_new()) == NULL) {
		FAIL("failed to create session");
		goto err;
	}

	ssl->hit = 0;

	CBS_init(&cbs, tlsext_sni_client, sizeof(tlsext_sni_client));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client SNI\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->session->tlsext_hostname == NULL) {
		FAIL("no tlsext_hostname from client SNI\n");
		goto err;
	}

	if (strlen(ssl->session->tlsext_hostname) != strlen(TEST_SNI_SERVERNAME) ||
	    strncmp(ssl->session->tlsext_hostname, TEST_SNI_SERVERNAME,
		strlen(TEST_SNI_SERVERNAME)) != 0) {
		FAIL("got tlsext_hostname `%s', want `%s'\n",
		    ssl->session->tlsext_hostname, TEST_SNI_SERVERNAME);
		goto err;
	}

	ssl->hit = 1;

	free(ssl->session->tlsext_hostname);
	if ((ssl->session->tlsext_hostname = strdup("notthesame.libressl.org")) ==
	    NULL) {
		FAIL("failed to strdup tlsext_hostname");
		goto err;
	}

	CBS_init(&cbs, tlsext_sni_client, sizeof(tlsext_sni_client));
	if (server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("parsed client with mismatched SNI\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_sni_server(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_server_name, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch sni funcs");

	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need SNI\n");
		goto err;
	}

	if (!SSL_set_tlsext_host_name(ssl, TEST_SNI_SERVERNAME)) {
		FAIL("client failed to set server name\n");
		goto err;
	}

	if ((ssl->session->tlsext_hostname = strdup(TEST_SNI_SERVERNAME)) ==
	    NULL)
		errx(1, "failed to strdup tlsext_hostname");

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need SNI\n");
		goto err;
	}

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server failed to build SNI\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != tlsext_sni_server_len) {
		FAIL("got server SNI with length %zu, "
		    "want length %zu\n", dlen, tlsext_sni_server_len);
		goto err;
	}

	if (memcmp(data, tlsext_sni_server, dlen) != 0) {
		FAIL("server SNI differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_sni_server, tlsext_sni_server_len);
		goto err;
	}

	free(ssl->session->tlsext_hostname);
	ssl->session->tlsext_hostname = NULL;

	CBS_init(&cbs, tlsext_sni_server, tlsext_sni_server_len);
	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("failed to parse server SNI\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->session->tlsext_hostname == NULL) {
		FAIL("no tlsext_hostname after server SNI\n");
		goto err;
	}

	if (strlen(ssl->session->tlsext_hostname) != strlen(TEST_SNI_SERVERNAME) ||
	    strncmp(ssl->session->tlsext_hostname, TEST_SNI_SERVERNAME,
		strlen(TEST_SNI_SERVERNAME)) != 0) {
		FAIL("got tlsext_hostname `%s', want `%s'\n",
		    ssl->session->tlsext_hostname, TEST_SNI_SERVERNAME);
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}


/*
 * QUIC transport parameters extension - RFC 90210 :)
 */

static const unsigned char tlsext_quic_transport_data[] = {
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
};

static int
test_tlsext_quic_transport_parameters_client(void)
{
	const SSL_QUIC_METHOD quic_method = {0};
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	CBB cbb;
	CBS cbs;
	int alert;
	const uint8_t *out_bytes;
	size_t out_bytes_len;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_quic_transport_parameters,
	    &client_funcs, &server_funcs))
		errx(1, "failed to fetch quic transport parameter funcs");

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need QUIC\n");
		goto err;
	}

	if (!SSL_set_quic_transport_params(ssl,
	    tlsext_quic_transport_data, sizeof(tlsext_quic_transport_data))) {
		FAIL("client failed to set QUIC parameters\n");
		goto err;
	}

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need QUIC\n");
		goto err;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;
	ssl->s3->hs.negotiated_tls_version = TLS1_3_VERSION;

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need QUIC\n");
		goto err;
	}

	ssl->quic_method = &quic_method;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need QUIC\n");
		goto err;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build QUIC\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB");
		goto err;
	}

	if (dlen != sizeof(tlsext_quic_transport_data)) {
		FAIL("got client QUIC with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_quic_transport_data));
		goto err;
	}

	if (memcmp(data, tlsext_quic_transport_data, dlen) != 0) {
		FAIL("client QUIC differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_quic_transport_data,
		    sizeof(tlsext_quic_transport_data));
		goto err;
	}

	CBS_init(&cbs, tlsext_quic_transport_data,
	    sizeof(tlsext_quic_transport_data));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("server_parse of QUIC from server failed\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	SSL_get_peer_quic_transport_params(ssl, &out_bytes, &out_bytes_len);

	if (out_bytes_len != sizeof(tlsext_quic_transport_data)) {
		FAIL("server_parse QUIC length differs, got %zu want %zu\n",
		    out_bytes_len,
		    sizeof(tlsext_quic_transport_data));
		goto err;
	}

	if (memcmp(out_bytes, tlsext_quic_transport_data,
	    out_bytes_len) != 0) {
		FAIL("server_parse QUIC differs from sent:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_quic_transport_data,
		    sizeof(tlsext_quic_transport_data));
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_quic_transport_parameters_server(void)
{
	const SSL_QUIC_METHOD quic_method = {0};
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;
	const uint8_t *out_bytes;
	size_t out_bytes_len;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_quic_transport_parameters,
	    &client_funcs, &server_funcs))
		errx(1, "failed to fetch quic transport parameter funcs");

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need QUIC\n");
		goto err;
	}

	if (!SSL_set_quic_transport_params(ssl,
	    tlsext_quic_transport_data, sizeof(tlsext_quic_transport_data))) {
		FAIL("server failed to set QUIC parametes\n");
		goto err;
	}

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_EE)) {
		FAIL("server should not need QUIC\n");
		goto err;
	}

	ssl->quic_method = &quic_method;

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_EE)) {
		FAIL("server should need QUIC\n");
		goto err;
	}

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_EE, &cbb)) {
		FAIL("server failed to build QUIC\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_quic_transport_data)) {
		FAIL("got server QUIC with length %zu, want length %zu\n",
		    dlen, sizeof(tlsext_quic_transport_data));
		goto err;
	}

	if (memcmp(data, tlsext_quic_transport_data, dlen) != 0) {
		FAIL("saved server QUIC differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_quic_transport_data,
		    sizeof(tlsext_quic_transport_data));
		goto err;
	}

	CBS_init(&cbs, tlsext_quic_transport_data,
	    sizeof(tlsext_quic_transport_data));

	ssl->quic_method = NULL;

	if (client_funcs->process(ssl, SSL_TLSEXT_MSG_EE, &cbs, &alert)) {
		FAIL("QUIC parse should have failed!\n");
		goto err;
	}

	ssl->quic_method = &quic_method;

	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("client_parse of QUIC from server failed\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	SSL_get_peer_quic_transport_params(ssl, &out_bytes, &out_bytes_len);

	if (out_bytes_len != sizeof(tlsext_quic_transport_data)) {
		FAIL("client QUIC length differs, got %zu want %zu\n",
		    out_bytes_len,
		    sizeof(tlsext_quic_transport_data));
		goto err;
	}

	if (memcmp(out_bytes, tlsext_quic_transport_data, out_bytes_len) != 0) {
		FAIL("client QUIC differs from sent:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tlsext_quic_transport_data,
		    sizeof(tlsext_quic_transport_data));
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static const unsigned char tls_ocsp_client_default[] = {
	0x01, 0x00, 0x00, 0x00, 0x00
};

static int
test_tlsext_ocsp_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	size_t dlen;
	int failure;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_status_request, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch ocsp funcs");

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need TLSEXT_TYPE_status_request\n");
		goto err;
	}
	SSL_set_tlsext_status_type(ssl, TLSEXT_STATUSTYPE_ocsp);

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need TLSEXT_TYPE_status_request\n");
		goto err;
	}
	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build SNI\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tls_ocsp_client_default)) {
		FAIL("got TLSEXT_TYPE_status_request client with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tls_ocsp_client_default));
		goto err;
	}
	if (memcmp(data, tls_ocsp_client_default, dlen) != 0) {
		FAIL("TLSEXT_TYPE_status_request client differs:\n");
		fprintf(stderr, "received:\n");
		hexdump(data, dlen);
		fprintf(stderr, "test data:\n");
		hexdump(tls_ocsp_client_default,
		    sizeof(tls_ocsp_client_default));
		goto err;
	}
	CBS_init(&cbs, tls_ocsp_client_default,
	    sizeof(tls_ocsp_client_default));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse TLSEXT_TYPE_status_request client\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_ocsp_server(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	size_t dlen;
	int failure;
	CBB cbb;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_status_request, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch ocsp funcs");

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need TLSEXT_TYPE_status_request\n");
		goto err;
	}

	ssl->tlsext_status_expected = 1;

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need TLSEXT_TYPE_status_request\n");
		goto err;
	}
	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server failed to build TLSEXT_TYPE_status_request\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

/*
 * Session ticket - RFC 5077 since no known implementations use 4507.
 *
 * Session tickets can be length 0 (special case) to 2^16-1.
 *
 * The state is encrypted by the server so it is opaque to the client.
 */
static uint8_t tlsext_sessionticket_hello_min[1];
static uint8_t tlsext_sessionticket_hello_max[65535];

static int
test_tlsext_sessionticket_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	CBB cbb;
	size_t dlen;
	uint8_t dummy[1234];

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	/* Create fake session tickets with random data. */
	arc4random_buf(tlsext_sessionticket_hello_min,
	    sizeof(tlsext_sessionticket_hello_min));
	arc4random_buf(tlsext_sessionticket_hello_max,
	    sizeof(tlsext_sessionticket_hello_max));

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_session_ticket, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch session ticket funcs");

	/* Should need a ticket by default. */
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need Sessionticket for default "
		    "ciphers\n");
		goto err;
	}

	/* Test disabling tickets. */
	if ((SSL_set_options(ssl, SSL_OP_NO_TICKET) & SSL_OP_NO_TICKET) == 0) {
		FAIL("Cannot disable tickets in the TLS connection\n");
		goto err;
	}
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need SessionTicket if it was disabled\n");
		goto err;
	}

	/* Test re-enabling tickets. */
	if ((SSL_clear_options(ssl, SSL_OP_NO_TICKET) & SSL_OP_NO_TICKET) != 0) {
		FAIL("Cannot re-enable tickets in the TLS connection\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need SessionTicket if it was disabled\n");
		goto err;
	}

	/* Since we don't have a session, we should build an empty ticket. */
	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("Cannot build a ticket\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("Cannot finish CBB\n");
		goto err;
	}
	if (dlen != 0) {
		FAIL("Expected 0 length but found %zu\n", dlen);
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	/* With a new session (but no ticket), we should still have 0 length */
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("Should still want a session ticket with a new session\n");
		goto err;
	}
	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("Cannot build a ticket\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("Cannot finish CBB\n");
		goto err;
	}
	if (dlen != 0) {
		FAIL("Expected 0 length but found %zu\n", dlen);
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	/* With a new session (and ticket), we should use that ticket */
	SSL_SESSION_free(ssl->session);
	if ((ssl->session = SSL_SESSION_new()) == NULL)
		errx(1, "failed to create session");

	arc4random_buf(&dummy, sizeof(dummy));
	if ((ssl->session->tlsext_tick = malloc(sizeof(dummy))) == NULL) {
		errx(1, "failed to malloc");
	}
	memcpy(ssl->session->tlsext_tick, dummy, sizeof(dummy));
	ssl->session->tlsext_ticklen = sizeof(dummy);

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("Should still want a session ticket with a new session\n");
		goto err;
	}
	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("Cannot build a ticket\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("Cannot finish CBB\n");
		goto err;
	}
	if (dlen != sizeof(dummy)) {
		FAIL("Expected %zu length but found %zu\n", sizeof(dummy), dlen);
		goto err;
	}
	if (memcmp(data, dummy, dlen) != 0) {
		FAIL("server SNI differs:\n");
		compare_data(data, dlen,
		    dummy, sizeof(dummy));
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;
	free(ssl->session->tlsext_tick);
	ssl->session->tlsext_tick = NULL;
	ssl->session->tlsext_ticklen = 0;

	/*
	 * Send in NULL to disable session tickets at runtime without going
	 * through SSL_set_options().
	 */
	if (!SSL_set_session_ticket_ext(ssl, NULL, 0)) {
		FAIL("Could not set a NULL custom ticket\n");
		goto err;
	}
	/* Should not need a ticket in this case */
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("Should not want to use session tickets with a NULL custom\n");
		goto err;
	}

	/*
	 * If you want to remove the tlsext_session_ticket behavior, you have
	 * to do it manually.
	 */
	free(ssl->tlsext_session_ticket);
	ssl->tlsext_session_ticket = NULL;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("Should need a session ticket again when the custom one is removed\n");
		goto err;
	}

	/* Test a custom session ticket (not recommended in practice) */
	if (!SSL_set_session_ticket_ext(ssl, tlsext_sessionticket_hello_max,
	    sizeof(tlsext_sessionticket_hello_max))) {
		FAIL("Should be able to set a custom ticket\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("Should need a session ticket again when the custom one is not empty\n");
		goto err;
	}
	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("Cannot build a ticket with a max length random payload\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("Cannot finish CBB\n");
		goto err;
	}
	if (dlen != sizeof(tlsext_sessionticket_hello_max)) {
		FAIL("Expected %zu length but found %zu\n",
		    sizeof(tlsext_sessionticket_hello_max), dlen);
		goto err;
	}
	if (memcmp(data, tlsext_sessionticket_hello_max,
	    sizeof(tlsext_sessionticket_hello_max)) != 0) {
		FAIL("Expected to get what we passed in\n");
		compare_data(data, dlen,
		    tlsext_sessionticket_hello_max,
		    sizeof(tlsext_sessionticket_hello_max));
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}


static int
test_tlsext_sessionticket_server(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	uint8_t *data = NULL;
	size_t dlen;
	CBB cbb;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_session_ticket, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch session ticket funcs");

	/*
	 * By default, should not need a session ticket since the ticket
	 * is not yet expected.
	 */
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need SessionTicket by default\n");
		goto err;
	}

	/* Test disabling tickets. */
	if ((SSL_set_options(ssl, SSL_OP_NO_TICKET) & SSL_OP_NO_TICKET) == 0) {
		FAIL("Cannot disable tickets in the TLS connection\n");
		goto err;
	}
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need SessionTicket if it was disabled\n");
		goto err;
	}

	/* Test re-enabling tickets. */
	if ((SSL_clear_options(ssl, SSL_OP_NO_TICKET) & SSL_OP_NO_TICKET) != 0) {
		FAIL("Cannot re-enable tickets in the TLS connection\n");
		goto err;
	}
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need SessionTicket yet\n");
		goto err;
	}

	/* Set expected to require it. */
	ssl->tlsext_ticket_expected = 1;
	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should now be required for SessionTicket\n");
		goto err;
	}

	/* server hello's session ticket should always be 0 length payload. */
	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("Cannot build a ticket with a max length random payload\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("Cannot finish CBB\n");
		goto err;
	}
	if (dlen != 0) {
		FAIL("Expected 0 length but found %zu\n", dlen);
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

#ifndef OPENSSL_NO_SRTP
/*
 * Supported Secure Real-time Transport Protocol (RFC 5764 section 4.1.1)
 */

/* Colon separated string values */
const char *tlsext_srtp_single_profile = "SRTP_AES128_CM_SHA1_80";
const char *tlsext_srtp_multiple_profiles = "SRTP_AES128_CM_SHA1_80:SRTP_AES128_CM_SHA1_32";

const char *tlsext_srtp_aes128cmsha80 = "SRTP_AES128_CM_SHA1_80";
const char *tlsext_srtp_aes128cmsha32 = "SRTP_AES128_CM_SHA1_32";

const uint8_t tlsext_srtp_single[] = {
	/* SRTPProtectionProfile SRTPProtectionProfiles<2..2^16-1> */
	0x00, 0x02, /* len */
	0x00, 0x01, /* SRTP_AES128_CM_SHA1_80 */
	0x00        /* opaque srtp_mki<0..255> */
};

const uint8_t tlsext_srtp_multiple[] = {
	/* SRTPProtectionProfile SRTPProtectionProfiles<2..2^16-1> */
	0x00, 0x04, /* len */
	0x00, 0x01, /* SRTP_AES128_CM_SHA1_80 */
	0x00, 0x02, /* SRTP_AES128_CM_SHA1_32 */
	0x00	/* opaque srtp_mki<0..255> */
};

const uint8_t tlsext_srtp_multiple_invalid[] = {
	/* SRTPProtectionProfile SRTPProtectionProfiles<2..2^16-1> */
	0x00, 0x04, /* len */
	0x00, 0x08, /* arbitrary value not found in known profiles */
	0x00, 0x09, /* arbitrary value not found in known profiles */
	0x00	/* opaque srtp_mki<0..255> */
};

const uint8_t tlsext_srtp_single_invalid[] = {
	/* SRTPProtectionProfile SRTPProtectionProfiles<2..2^16-1> */
	0x00, 0x02, /* len */
	0x00, 0x08, /* arbitrary value not found in known profiles */
	0x00	/* opaque srtp_mki<0..255> */
};

const uint8_t tlsext_srtp_multiple_one_valid[] = {
	/* SRTPProtectionProfile SRTPProtectionProfiles<2..2^16-1> */
	0x00, 0x04, /* len */
	0x00, 0x08, /* arbitrary value not found in known profiles */
	0x00, 0x02, /* SRTP_AES128_CM_SHA1_32 */
	0x00	    /* opaque srtp_mki<0..255> */
};

static int
test_tlsext_srtp_client(void)
{
	SRTP_PROTECTION_PROFILE *prof;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	uint8_t *data = NULL;
	CBB cbb;
	CBS cbs;
	int failure, alert;
	size_t dlen;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	/* SRTP is for DTLS */
	if ((ssl_ctx = SSL_CTX_new(DTLSv1_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_use_srtp, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch srtp funcs");

	/* By default, we don't need this */
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need SRTP by default\n");
		goto err;
	}

	if (SSL_set_tlsext_use_srtp(ssl, tlsext_srtp_single_profile) != 0) {
		FAIL("should be able to set a single SRTP\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need SRTP\n");
		goto err;
	}

	/* Make sure we can build the client with a single profile. */

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build SRTP\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_srtp_single)) {
		FAIL("got client SRTP with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_srtp_single));
		compare_data(data, dlen, tlsext_srtp_single,
		    sizeof(tlsext_srtp_single));
		goto err;
	}
	if (memcmp(data, tlsext_srtp_single, dlen) != 0) {
		FAIL("client SRTP differs:\n");
		compare_data(data, dlen, tlsext_srtp_single,
		    sizeof(tlsext_srtp_single));
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	/* Make sure we can parse the single profile. */

	if (SSL_get_selected_srtp_profile(ssl) != NULL) {
		FAIL("SRTP profile should not be set yet\n");
		goto err;
	}

	CBS_init(&cbs, tlsext_srtp_single, sizeof(tlsext_srtp_single));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse SRTP\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if ((prof = SSL_get_selected_srtp_profile(ssl)) == NULL) {
		FAIL("SRTP profile should be set now\n");
		goto err;
	}
	if (strcmp(prof->name, tlsext_srtp_aes128cmsha80) != 0) {
		FAIL("SRTP profile was not set properly\n");
		goto err;
	}

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("should send server extension when profile selected\n");
		goto err;
	}

	/* Make sure we can build the clienthello with multiple entries. */

	if (SSL_set_tlsext_use_srtp(ssl, tlsext_srtp_multiple_profiles) != 0) {
		FAIL("should be able to set SRTP to multiple profiles\n");
		goto err;
	}
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need SRTP by now\n");
		goto err;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build SRTP\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_srtp_multiple)) {
		FAIL("got client SRTP with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_srtp_multiple));
		compare_data(data, dlen, tlsext_srtp_multiple,
		    sizeof(tlsext_srtp_multiple));
		goto err;
	}
	if (memcmp(data, tlsext_srtp_multiple, dlen) != 0) {
		FAIL("client SRTP differs:\n");
		compare_data(data, dlen, tlsext_srtp_multiple,
		    sizeof(tlsext_srtp_multiple));
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	/* Make sure we can parse multiple profiles (selects server preferred) */

	ssl->srtp_profile = NULL;

	CBS_init(&cbs, tlsext_srtp_multiple,
	    sizeof(tlsext_srtp_multiple));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse SRTP\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if ((prof = SSL_get_selected_srtp_profile(ssl)) == NULL) {
		FAIL("SRTP profile should be set now\n");
		goto err;
	}
	if (strcmp(prof->name, tlsext_srtp_aes128cmsha80) != 0) {
		FAIL("SRTP profile was not set properly\n");
		goto err;
	}

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("should send server extension when profile selected\n");
		goto err;
	}

	/*
	 * Make sure we can parse the clienthello with multiple entries
	 * where one is unknown.
	 */
	ssl->srtp_profile = NULL;

	CBS_init(&cbs, tlsext_srtp_multiple_one_valid,
	    sizeof(tlsext_srtp_multiple_one_valid));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse SRTP\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if ((prof = SSL_get_selected_srtp_profile(ssl)) == NULL) {
		FAIL("SRTP profile should be set now\n");
		goto err;
	}
	if (strcmp(prof->name, tlsext_srtp_aes128cmsha32) != 0) {
		FAIL("SRTP profile was not set properly\n");
		goto err;
	}

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("should send server extension when profile selected\n");
		goto err;
	}

	/* Make sure we fall back to negotiated when none work. */

	ssl->srtp_profile = NULL;

	CBS_init(&cbs, tlsext_srtp_multiple_invalid,
	    sizeof(tlsext_srtp_multiple_invalid));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("should be able to fall back to negotiated\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	/* If we fallback, the server should NOT send the extension. */
	if (SSL_get_selected_srtp_profile(ssl) != NULL) {
		FAIL("should not have selected a profile when none found\n");
		goto err;
	}
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("should not send server tlsext when no profile found\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_srtp_server(void)
{
	const SRTP_PROTECTION_PROFILE *prof;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	uint8_t *data = NULL;
	CBB cbb;
	CBS cbs;
	int failure, alert;
	size_t dlen;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	/* SRTP is for DTLS */
	if ((ssl_ctx = SSL_CTX_new(DTLSv1_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_use_srtp, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch srtp funcs");

	/* By default, we don't need this */
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need SRTP by default\n");
		goto err;
	}

	if (srtp_find_profile_by_name(tlsext_srtp_aes128cmsha80, &prof,
	    strlen(tlsext_srtp_aes128cmsha80))) {
		FAIL("should be able to find the given profile\n");
		goto err;
	}
	ssl->srtp_profile = prof;
	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need SRTP by now\n");
		goto err;
	}

	/* Make sure we can build the server with a single profile. */

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server failed to build SRTP\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish CBB");

	if (dlen != sizeof(tlsext_srtp_single)) {
		FAIL("got server SRTP with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_srtp_single));
		compare_data(data, dlen, tlsext_srtp_single,
		    sizeof(tlsext_srtp_single));
		goto err;
	}
	if (memcmp(data, tlsext_srtp_single, dlen) != 0) {
		FAIL("server SRTP differs:\n");
		compare_data(data, dlen, tlsext_srtp_single,
		    sizeof(tlsext_srtp_single));
		goto err;
	}

	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");
	free(data);
	data = NULL;

	/* Make sure we can parse the single profile. */
	ssl->srtp_profile = NULL;

	if (SSL_get_selected_srtp_profile(ssl) != NULL) {
		FAIL("SRTP profile should not be set yet\n");
		goto err;
	}

	/* Setup the environment as if a client sent a list of profiles. */
	if (SSL_set_tlsext_use_srtp(ssl, tlsext_srtp_multiple_profiles) != 0) {
		FAIL("should be able to set multiple profiles in SRTP\n");
		goto err;
	}

	CBS_init(&cbs, tlsext_srtp_single, sizeof(tlsext_srtp_single));
	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("failed to parse SRTP\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if ((prof = SSL_get_selected_srtp_profile(ssl)) == NULL) {
		FAIL("SRTP profile should be set now\n");
		goto err;
	}
	if (strcmp(prof->name, tlsext_srtp_aes128cmsha80) != 0) {
		FAIL("SRTP profile was not set properly\n");
		goto err;
	}

	/* Make sure we cannot parse multiple profiles */
	ssl->srtp_profile = NULL;

	CBS_init(&cbs, tlsext_srtp_multiple,
	    sizeof(tlsext_srtp_multiple));
	if (client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("should not find multiple entries from the server\n");
		goto err;
	}

	/* Make sure we cannot parse a server with unknown profile */
	ssl->srtp_profile = NULL;

	CBS_init(&cbs, tlsext_srtp_single_invalid,
	    sizeof(tlsext_srtp_single_invalid));
	if (client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("should not be able to parse this\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}
#endif /* OPENSSL_NO_SRTP */

static const unsigned char tlsext_clienthello_default[] = {
	0x00, 0x34, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0x08,
	0x00, 0x1d, 0x00, 0x17, 0x00, 0x18, 0x00, 0x19,
	0x00, 0x0b, 0x00, 0x02, 0x01, 0x00, 0x00, 0x23,
	0x00, 0x00, 0x00, 0x0d, 0x00, 0x18, 0x00, 0x16,
	0x08, 0x06, 0x06, 0x01, 0x06, 0x03, 0x08, 0x05,
	0x05, 0x01, 0x05, 0x03, 0x08, 0x04, 0x04, 0x01,
	0x04, 0x03, 0x02, 0x01, 0x02, 0x03,
};

/* An empty array is an incomplete type and sizeof() is undefined. */
static const unsigned char tlsext_clienthello_disabled[] = {
	0x00,
};
static size_t tlsext_clienthello_disabled_len = 0;

static int
test_tlsext_clienthello_build(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	size_t dlen;
	int failure;
	CBB cbb;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL) {
		FAIL("failed to create SSL_CTX");
		goto err;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL) {
		FAIL("failed to create SSL");
		goto err;
	}

	if (!tlsext_linearize_build_order(ssl)) {
		FAIL("failed to linearize build order");
		goto err;
	}

	if (!tls_extension_funcs(TLSEXT_TYPE_supported_versions, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch supported versions funcs");

	ssl->s3->hs.our_min_tls_version = TLS1_VERSION;
	ssl->s3->hs.our_max_tls_version = TLS1_2_VERSION;

	if (!tlsext_client_build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("failed to build clienthello extensions\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB");
		goto err;
	}

	if (dlen != sizeof(tlsext_clienthello_default)) {
		FAIL("got clienthello extensions with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_clienthello_default));
		compare_data(data, dlen, tlsext_clienthello_default,
		    sizeof(tlsext_clienthello_default));
		goto err;
	}
	if (memcmp(data, tlsext_clienthello_default, dlen) != 0) {
		FAIL("clienthello extensions differs:\n");
		compare_data(data, dlen, tlsext_clienthello_default,
		    sizeof(tlsext_clienthello_default));
		goto err;
	}

	free(data);
	data = NULL;
	CBB_cleanup(&cbb);
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	/* Switch to TLSv1.1, disable EC ciphers and session tickets. */
	ssl->s3->hs.our_max_tls_version = TLS1_1_VERSION;
	if (!SSL_set_cipher_list(ssl, "TLSv1.2:!ECDHE:!ECDSA")) {
		FAIL("failed to set cipher list\n");
		goto err;
	}
	if ((SSL_set_options(ssl, SSL_OP_NO_TICKET) & SSL_OP_NO_TICKET) == 0) {
		FAIL("failed to disable session tickets\n");
		goto err;
	}

	if (!tlsext_client_build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("failed to build clienthello extensions\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB");
		goto err;
	}

	if (dlen != tlsext_clienthello_disabled_len) {
		FAIL("got clienthello extensions with length %zu, "
		    "want length %zu\n", dlen,
		    tlsext_clienthello_disabled_len);
		compare_data(data, dlen, tlsext_clienthello_disabled,
		    tlsext_clienthello_disabled_len);
		goto err;
	}
	if (memcmp(data, tlsext_clienthello_disabled, dlen) != 0) {
		FAIL("clienthello extensions differs:\n");
		compare_data(data, dlen, tlsext_clienthello_disabled,
		    tlsext_clienthello_disabled_len);
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

unsigned char tlsext_serverhello_default[] = {
	0x00, 0x06, 0x00, 0x2b, 0x00, 0x02, 0x03, 0x04,
};

unsigned char tlsext_serverhello_enabled[] = {
	0x00, 0x10, 0x00, 0x2b, 0x00, 0x02, 0x03, 0x04,
	0x00, 0x0b, 0x00, 0x02, 0x01, 0x00, 0x00, 0x23,
	0x00, 0x00,
};

static int
test_tlsext_serverhello_build(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	size_t dlen;
	int failure;
	CBB cbb;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL) {
		FAIL("failed to create SSL_CTX");
		goto err;
	}
	if ((ssl = SSL_new(ssl_ctx)) == NULL) {
		FAIL("failed to create SSL");
		goto err;
	}
	if (!tlsext_linearize_build_order(ssl)) {
		FAIL("failed to linearize build order");
		goto err;
	}
	if ((ssl->session = SSL_SESSION_new()) == NULL) {
		FAIL("failed to create session");
		goto err;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;
	ssl->s3->hs.negotiated_tls_version = TLS1_3_VERSION;
	ssl->s3->hs.cipher = ssl3_get_cipher_by_value(0x003c);

	if (!tlsext_server_build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("failed to build serverhello extensions\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB");
		goto err;
	}

	if (dlen != sizeof(tlsext_serverhello_default)) {
		FAIL("got serverhello extensions with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_serverhello_default));
		compare_data(data, dlen, tlsext_serverhello_default,
		    sizeof(tlsext_serverhello_default));
		goto err;
	}
	if (memcmp(data, tlsext_serverhello_default, dlen) != 0) {
		FAIL("serverhello extensions differs:\n");
		compare_data(data, dlen, tlsext_serverhello_default,
		    sizeof(tlsext_serverhello_default));
		goto err;
	}

	CBB_cleanup(&cbb);
	free(data);
	data = NULL;
	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	/* Turn a few things on so we get extensions... */
	ssl->s3->send_connection_binding = 1;
	ssl->s3->hs.cipher = ssl3_get_cipher_by_value(0xc027);
	ssl->tlsext_status_expected = 1;
	ssl->tlsext_ticket_expected = 1;
	if ((ssl->session->tlsext_ecpointformatlist = malloc(1)) == NULL) {
		FAIL("malloc failed");
		goto err;
	}
	ssl->session->tlsext_ecpointformatlist_length = 1;
	ssl->session->tlsext_ecpointformatlist[0] =
	    TLSEXT_ECPOINTFORMAT_uncompressed;

	if (!tlsext_server_build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("failed to build serverhello extensions\n");
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB");
		goto err;
	}

	if (dlen != sizeof(tlsext_serverhello_enabled)) {
		FAIL("got serverhello extensions with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_serverhello_enabled));
		compare_data(data, dlen, tlsext_serverhello_enabled,
		    sizeof(tlsext_serverhello_enabled));
		goto err;
	}
	if (memcmp(data, tlsext_serverhello_enabled, dlen) != 0) {
		FAIL("serverhello extensions differs:\n");
		compare_data(data, dlen, tlsext_serverhello_enabled,
		    sizeof(tlsext_serverhello_enabled));
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

const unsigned char tlsext_versions_client[] = {
	0x08, 0x03, 0x04, 0x03, 0x03, 0x03,
	0x02, 0x03, 0x01,
};

const unsigned char tlsext_versions_server[] = {
	0x03, 0x04,
};

static int
test_tlsext_versions_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_supported_versions, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch supported versions funcs");

	ssl->s3->hs.our_max_tls_version = TLS1_1_VERSION;

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need versions\n");
		goto done;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_2_VERSION;

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need versions\n");
		goto done;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need versions\n");
		goto done;
	}

	ssl->s3->hs.our_min_tls_version = TLS1_VERSION;
	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client should have built versions\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB\n");
		goto done;
	}

	if (dlen != sizeof(tlsext_versions_client)) {
		FAIL("got versions with length %zu, "
		    "want length %zu\n", dlen, sizeof(tlsext_versions_client));
		goto done;
	}

	CBS_init(&cbs, data, dlen);
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client versions\n");
		goto done;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}

	failure = 0;

 done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_versions_server(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_supported_versions, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch supported versions funcs");

	ssl->s3->hs.negotiated_tls_version = TLS1_2_VERSION;

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need versions\n");
		goto done;
	}

	ssl->s3->hs.negotiated_tls_version = TLS1_3_VERSION;

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need versions\n");
		goto done;
	}

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server should have built versions\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB\n");
		goto done;
	}

	if (dlen != sizeof(tlsext_versions_server)) {
		FAIL("got versions with length %zu, "
		    "want length %zu\n", dlen, sizeof(tlsext_versions_server));
		goto done;
	}

	CBS_init(&cbs, data, dlen);
	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("failed to parse client versions\n");
		goto done;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}

	failure = 0;

 done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

const unsigned char tlsext_keyshare_client[] = {
	0x00, 0x24, 0x00, 0x1d, 0x00, 0x20, 0xba, 0x83,
	0x2e, 0x4a, 0x18, 0xbe, 0x96, 0xd2, 0x71, 0x70,
	0x18, 0x04, 0xf9, 0x9d, 0x76, 0x98, 0xef, 0xe8,
	0x4f, 0x8b, 0x85, 0x41, 0xa4, 0xd9, 0x61, 0x57,
	0xad, 0x5b, 0xa4, 0xe9, 0x8b, 0x6b,
};

const unsigned char tlsext_keyshare_server[] = {
	0x00, 0x1d, 0x00, 0x20, 0xe5, 0xe8, 0x5a, 0xb9,
	0x7e, 0x12, 0x62, 0xe3, 0xd8, 0x7f, 0x6e, 0x3c,
	0xec, 0xa6, 0x8b, 0x99, 0x45, 0x77, 0x8e, 0x11,
	0xb3, 0xb9, 0x12, 0xb6, 0xbe, 0x35, 0xca, 0x51,
	0x76, 0x1e, 0xe8, 0x22
};

static int
test_tlsext_keyshare_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	size_t idx;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_key_share, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch keyshare funcs");

	if ((ssl->s3->hs.key_share =
	    tls_key_share_new_nid(NID_X25519)) == NULL)
		errx(1, "failed to create key share");
	if (!tls_key_share_generate(ssl->s3->hs.key_share))
		errx(1, "failed to generate key share");

	ssl->s3->hs.our_max_tls_version = TLS1_2_VERSION;
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need keyshare\n");
		goto done;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;
	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need keyshare\n");
		goto done;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;
	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client should have built keyshare\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB\n");
		goto done;
	}

	if (dlen != sizeof(tlsext_keyshare_client)) {
		FAIL("got client keyshare with length %zu, "
		    "want length %zu\n", dlen, (size_t) sizeof(tlsext_keyshare_client));
		goto done;
	}

	ssl->version = TLS1_3_VERSION;

	/* Fake up the ssl enough so the key share can process */
	tls_key_share_free(ssl->s3->hs.key_share);
	ssl->session = SSL_SESSION_new();
	if (ssl->session == NULL) {
		FAIL("malloc");
		goto done;
	}
	memset(ssl->s3, 0, sizeof(*ssl->s3));
	ssl->session->tlsext_supportedgroups = calloc(4,
	    sizeof(unsigned short));
	if (ssl->session->tlsext_supportedgroups == NULL) {
		FAIL("malloc");
		goto done;
	}
	ssl->session->tlsext_supportedgroups[0] = 29;
	ssl->session->tlsext_supportedgroups[1] = 23;
	ssl->session->tlsext_supportedgroups[2] = 24;
	ssl->session->tlsext_supportedgroups[3] = 25;
	ssl->session->tlsext_supportedgroups_length = 4;
	tls_extension_find(TLSEXT_TYPE_supported_groups, &idx);
	ssl->s3->hs.extensions_processed |= (1 << idx);
	ssl->s3->hs.extensions_seen |= (1 << idx);
	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;

	/*
	 * We should select the key share for group 29, when group 29
	 * is the most preferred group
	 */
	CBS_init(&cbs, data, dlen);
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to process client keyshare\n");
		goto done;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}
	if (ssl->s3->hs.key_share == NULL) {
		FAIL("Did not select a key share");
		goto done;
	}
	if (tls_key_share_group(ssl->s3->hs.key_share) != 29) {
		FAIL("wrong key share group: got %d, expected 29\n",
		     tls_key_share_group(ssl->s3->hs.key_share));
		goto done;
	}

	/*
	 * Pretend the client did not send the supported groups extension. We
	 * should fail to process.
	 */
	ssl->s3->hs.extensions_seen = 0;
	tls_key_share_free(ssl->s3->hs.key_share);
	ssl->s3->hs.key_share = NULL;
	CBS_init(&cbs, data, dlen);
	if (server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("Processed key share when supported groups not provided");
		goto done;
	}
	ssl->s3->hs.extensions_seen |= (1 << idx);

	/*
	 * Pretend supported groups did not get processed. We should fail to
	 * process
	 */
	ssl->s3->hs.extensions_processed = 0;
	ssl->s3->hs.key_share = NULL;
	CBS_init(&cbs, data, dlen);
	if (server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("Processed key share when supported groups unprocesed");
		goto done;
	}
	ssl->s3->hs.extensions_processed |= (1 << idx);

	/*
	 * Remove group 29 by making it 0xbeef, meaning 29 has not been sent in
	 * supported groups. This should fail to process.
	 */
	ssl->session->tlsext_supportedgroups[0] = 0xbeef;
	ssl->s3->hs.key_share = NULL;
	CBS_init(&cbs, data, dlen);
	if (server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("Processed key share with invalid group!");
		goto done;
	}

	/*
	 * Make 29 least preferred, while server supports both 29 and 25.
	 * Client key share is for 29 but it prefers 25. We should successfully
	 * process, but should not select this key share.
	 */
	ssl->session->tlsext_supportedgroups[0] = 25;
	ssl->session->tlsext_supportedgroups[3] = 29;
	ssl->s3->hs.key_share = NULL;
	CBS_init(&cbs, data, dlen);
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to process client keyshare\n");
		goto done;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}
	if (ssl->s3->hs.key_share != NULL) {
		FAIL("Selected a key share when I should not have!");
		goto done;
	}
	ssl->session->tlsext_supportedgroups[0] = 29;
	ssl->session->tlsext_supportedgroups[3] = 25;

	failure = 0;

 done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static const uint8_t bogokey[] = {
	0xe5, 0xe8, 0x5a, 0xb9,	0x7e, 0x12, 0x62, 0xe3,
	0xd8, 0x7f, 0x6e, 0x3c,	0xec, 0xa6, 0x8b, 0x99,
	0x45, 0x77, 0x8e, 0x11,	0xb3, 0xb9, 0x12, 0xb6,
	0xbe, 0x35, 0xca, 0x51,	0x76, 0x1e, 0xe8, 0x22,
};

static int
test_tlsext_keyshare_server(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int decode_error;
	int failure;
	size_t dlen, idx;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_key_share, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch keyshare funcs");

	ssl->s3->hs.negotiated_tls_version = TLS1_2_VERSION;
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need keyshare\n");
		goto done;
	}

	ssl->s3->hs.negotiated_tls_version = TLS1_3_VERSION;
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("client should not need keyshare\n");
		goto done;
	}

	if (tls_extension_find(TLSEXT_TYPE_key_share, &idx) == NULL) {
		FAIL("failed to find keyshare extension\n");
		goto done;
	}
	ssl->s3->hs.extensions_seen |= (1 << idx);

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should need keyshare\n");
		goto done;
	}

	if (server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server should not have built a keyshare response\n");
		goto done;
	}

	if ((ssl->s3->hs.key_share =
		tls_key_share_new_nid(NID_X25519)) == NULL) {
		FAIL("failed to create key share");
		goto done;
	}

	if (!tls_key_share_generate(ssl->s3->hs.key_share)) {
		FAIL("failed to generate key share");
		goto done;
	}

	CBS_init(&cbs, bogokey, sizeof(bogokey));

	if (!tls_key_share_peer_public(ssl->s3->hs.key_share, &cbs,
	    &decode_error, NULL)) {
		FAIL("failed to load peer public key\n");
		goto done;
	}

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_SH, &cbb)) {
		FAIL("server should be able to build a keyshare response\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB\n");
		goto done;
	}

	if (dlen != sizeof(tlsext_keyshare_server)) {
		FAIL("got server keyshare with length %zu, "
		    "want length %zu\n", dlen, sizeof(tlsext_keyshare_server));
		goto done;
	}

	tls_key_share_free(ssl->s3->hs.key_share);

	if ((ssl->s3->hs.key_share =
	    tls_key_share_new_nid(NID_X25519)) == NULL) {
		FAIL("failed to create key share");
		goto done;
	}
	if (!tls_key_share_generate(ssl->s3->hs.key_share)) {
		FAIL("failed to generate key share");
		goto done;
	}

	CBS_init(&cbs, data, dlen);

	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("failed to parse server keyshare\n");
		goto done;
	}

	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}

	failure = 0;

done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

/* One day I hope to be the only Muppet in this codebase */
const uint8_t cookie[] = "\n"
    "        (o)(o)        \n"
    "      m'      'm      \n"
    "     M  -****-  M     \n"
    "      'm      m'      \n"
    "     m''''''''''m     \n"
    "    M            M BB \n";

static int
test_tlsext_cookie_client(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_cookie, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch cookie funcs");

	ssl->s3->hs.our_max_tls_version = TLS1_2_VERSION;
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need cookie\n");
		goto done;
	}


	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need cookie\n");
		goto done;
	}

	/* Normally would be set by receiving a server cookie in an HRR */
	ssl->s3->hs.tls13.cookie = strdup(cookie);
	ssl->s3->hs.tls13.cookie_len = strlen(cookie);

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need cookie\n");
		goto done;
	}

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client should have built a cookie response\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB\n");
		goto done;
	}

	if (dlen != strlen(cookie) + sizeof(uint16_t)) {
		FAIL("got cookie with length %zu, "
		    "want length %zu\n", dlen, strlen(cookie) +
		    sizeof(uint16_t));
		goto done;
	}

	CBS_init(&cbs, data, dlen);

	/* Checks cookie against what's in the hs.tls13 */
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse client cookie\n");
		goto done;
	}

	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}

	failure = 0;

 done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

static int
test_tlsext_cookie_server(void)
{
	unsigned char *data = NULL;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	size_t dlen;
	int alert;
	CBB cbb;
	CBS cbs;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_cookie, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch cookie funcs");

	ssl->s3->hs.our_max_tls_version = TLS1_2_VERSION;
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need cookie\n");
		goto done;
	}

	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;
	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need cookie\n");
		goto done;
	}

	/* Normally would be set by server before sending HRR */
	ssl->s3->hs.tls13.cookie = strdup(cookie);
	ssl->s3->hs.tls13.cookie_len = strlen(cookie);

	if (!server_funcs->needs(ssl, SSL_TLSEXT_MSG_HRR)) {
		FAIL("server should need cookie\n");
		goto done;
	}

	if (!server_funcs->build(ssl, SSL_TLSEXT_MSG_HRR, &cbb)) {
		FAIL("server should have built a cookie response\n");
		goto done;
	}

	if (!CBB_finish(&cbb, &data, &dlen)) {
		FAIL("failed to finish CBB\n");
		goto done;
	}

	if (dlen != strlen(cookie) + sizeof(uint16_t)) {
		FAIL("got cookie with length %zu, "
		    "want length %zu\n", dlen, strlen(cookie) +
		    sizeof(uint16_t));
		goto done;
	}

	CBS_init(&cbs, data, dlen);

	if (client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("client should not have parsed server cookie\n");
		goto done;
	}

	freezero(ssl->s3->hs.tls13.cookie, ssl->s3->hs.tls13.cookie_len);
	ssl->s3->hs.tls13.cookie = NULL;
	ssl->s3->hs.tls13.cookie_len = 0;

	if (!client_funcs->process(ssl, SSL_TLSEXT_MSG_SH, &cbs, &alert)) {
		FAIL("failed to parse server cookie\n");
		goto done;
	}

	if (memcmp(cookie, ssl->s3->hs.tls13.cookie,
		ssl->s3->hs.tls13.cookie_len) != 0) {
		FAIL("parsed server cookie does not match sent cookie\n");
		goto done;
	}

	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto done;
	}

	failure = 0;

done:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return (failure);
}

const uint8_t tlsext_default_psk_modes[] = {
	0x01, 0x01,
};

const uint8_t tlsext_psk_only_mode[] = {
	0x01, 0x00,
};

const uint8_t tlsext_psk_both_modes[] = {
	0x02, 0x00, 0x01,
};

static int
test_tlsext_psk_modes_client(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;
	uint8_t *data = NULL;
	size_t dlen;
	CBB cbb;
	CBS cbs;
	int alert;

	failure = 1;

	if (!CBB_init(&cbb, 0))
		errx(1, "Failed to create CBB");

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_psk_kex_modes, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch psk funcs");

	/* Disabled by default. */
	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need psk kex modes by default\n");
		goto err;
	}

	/*
	 * Prerequisites: use_psk_dhe_ke flag is set and
	 * our_max_tls_version >= TLSv1.3.
	 */

	ssl->s3->hs.tls13.use_psk_dhe_ke = 1;
	ssl->s3->hs.our_max_tls_version = TLS1_2_VERSION;

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need psk kex modes with TLSv1.2\n");
		goto err;
	}

	ssl->s3->hs.tls13.use_psk_dhe_ke = 0;
	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;

	if (client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should not need psk kex modes without "
		    "use_psk_dhe_ke\n");
		goto err;
	}

	ssl->s3->hs.tls13.use_psk_dhe_ke = 1;
	ssl->s3->hs.our_max_tls_version = TLS1_3_VERSION;

	if (!client_funcs->needs(ssl, SSL_TLSEXT_MSG_CH)) {
		FAIL("client should need psk kex modes with TLSv1.3\n");
		goto err;
	}

	/* Make sure we can build psk modes with DHE key establishment. */

	if (!client_funcs->build(ssl, SSL_TLSEXT_MSG_CH, &cbb)) {
		FAIL("client failed to build psk kex modes\n");
		goto err;
	}

	if (!CBB_finish(&cbb, &data, &dlen))
		errx(1, "failed to finish psk kex CBB");

	if (dlen != sizeof(tlsext_default_psk_modes)) {
		FAIL("got client psk kex modes with length %zu, "
		    "want length %zu\n", dlen,
		    sizeof(tlsext_default_psk_modes));
		compare_data(data, dlen, tlsext_default_psk_modes,
		    sizeof(tlsext_default_psk_modes));
		goto err;
	}
	if (memcmp(data, tlsext_default_psk_modes, dlen) != 0) {
		FAIL("client psk kex modes differ:\n");
		compare_data(data, dlen, tlsext_default_psk_modes,
		    sizeof(tlsext_default_psk_modes));
		goto err;
	}

	CBB_cleanup(&cbb);
	free(data);
	data = NULL;

	/*
	 * Make sure we can parse the default psk modes and that use_psk_dhe_ke
	 * is set after parsing.
	 */

	ssl->s3->hs.tls13.use_psk_dhe_ke = 0;

	CBS_init(&cbs, tlsext_default_psk_modes,
	    sizeof(tlsext_default_psk_modes));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse psk kex modes\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->s3->hs.tls13.use_psk_dhe_ke != 1) {
		FAIL("should have set use_psk_dhe_ke\n");
		goto err;
	}

	/*
	 * Make sure we can parse the psk-only mode and that use_psk_dhe_ke
	 * is still not set after parsing.
	 */

	ssl->s3->hs.tls13.use_psk_dhe_ke = 0;

	CBS_init(&cbs, tlsext_psk_only_mode, sizeof(tlsext_psk_only_mode));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse psk kex modes\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->s3->hs.tls13.use_psk_dhe_ke != 0) {
		FAIL("should not have set use_psk_dhe_ke\n");
		goto err;
	}

	/*
	 * Make sure we can parse the extension indicating both modes and that
	 * use_psk_dhe_ke is set after parsing.
	 */

	ssl->s3->hs.tls13.use_psk_dhe_ke = 0;

	CBS_init(&cbs, tlsext_psk_both_modes, sizeof(tlsext_psk_both_modes));
	if (!server_funcs->process(ssl, SSL_TLSEXT_MSG_CH, &cbs, &alert)) {
		FAIL("failed to parse psk kex modes\n");
		goto err;
	}
	if (CBS_len(&cbs) != 0) {
		FAIL("extension data remaining\n");
		goto err;
	}

	if (ssl->s3->hs.tls13.use_psk_dhe_ke != 1) {
		FAIL("should have set use_psk_dhe_ke\n");
		goto err;
	}

	failure = 0;

 err:
	CBB_cleanup(&cbb);
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);
	free(data);

	return failure;
}

static int
test_tlsext_psk_modes_server(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	const struct tls_extension_funcs *client_funcs;
	const struct tls_extension_funcs *server_funcs;
	int failure;

	failure = 1;

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	if (!tls_extension_funcs(TLSEXT_TYPE_psk_kex_modes, &client_funcs,
	    &server_funcs))
		errx(1, "failed to fetch psk funcs");

	if (server_funcs->needs(ssl, SSL_TLSEXT_MSG_SH)) {
		FAIL("server should not need psk kex modes\n");
		goto err;
	}

	failure = 0;

 err:
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	return failure;
}

struct tls_sni_test {
	const char *hostname;
	int is_ip;
	int valid;
};

static const struct tls_sni_test tls_sni_tests[] = {
	{
		.hostname = "openbsd.org",
		.valid = 1,
	},
	{
		.hostname = "op3nbsd.org",
		.valid = 1,
	},
	{
		.hostname = "org",
		.valid = 1,
	},
	{
		.hostname = "3openbsd.com",
		.valid = 1,
	},
	{
		.hostname = "3-0penb-d.c-m",
		.valid = 1,
	},
	{
		.hostname = "a",
		.valid = 1,
	},
	{
		.hostname =
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
		.valid = 1,
	},
	{
		.hostname =
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		.valid = 1,
	},
	{
		.hostname = "openbsd.org.",
		.valid = 0,
	},
	{
		.hostname = "openbsd..org",
		.valid = 0,
	},
	{
		.hostname = "openbsd.org-",
		.valid = 0,
	},
	{
		.hostname =
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
		.valid = 0,
	},
	{
		.hostname =
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.a",
		.valid = 0,
	},
	{
		.hostname = "-p3nbsd.org",
		.valid = 0,
	},
	{
		.hostname = "openbs-.org",
		.valid = 0,
	},
	{
		.hostname = "openbsd\n.org",
		.valid = 0,
	},
	{
		.hostname = "open_bsd.org",
		.valid = 0,
	},
	{
		.hostname = "open\177bsd.org",
		.valid = 0,
	},
	{
		.hostname = "open\255bsd.org",
		.valid = 0,
	},
	{
		.hostname = "dead::beef",
		.is_ip = 1,
		.valid = 0,
	},
	{
		.hostname = "192.168.0.1",
		.is_ip = 1,
		.valid = 0,
	},
};

#define N_TLS_SNI_TESTS (sizeof(tls_sni_tests) / sizeof(*tls_sni_tests))

static int
test_tlsext_is_valid_hostname(const struct tls_sni_test *tst)
{
	int failure;
	int is_ip;
	CBS cbs;

	failure = 1;

	CBS_init(&cbs, tst->hostname, strlen(tst->hostname));
	if (tlsext_sni_is_valid_hostname(&cbs, &is_ip) != tst->valid) {
		if (tst->valid) {
			FAIL("Valid hostname '%s' rejected\n",
			    tst->hostname);
		} else {
			FAIL("Invalid hostname '%s' accepted\n",
			    tst->hostname);
		}
		goto done;
	}
	if (tst->is_ip != is_ip) {
		if (tst->is_ip) {
			FAIL("Hostname '%s' is an IP literal but not "
			    "identified as one\n", tst->hostname);
		} else {
			FAIL("Hostname '%s' is not an IP literal but is "
			    "identified as one\n", tst->hostname);
		}
		goto done;
	}

	if (tst->valid) {
		CBS_init(&cbs, tst->hostname,
		    strlen(tst->hostname) + 1);
		if (tlsext_sni_is_valid_hostname(&cbs, &is_ip)) {
			FAIL("hostname with NUL byte accepted\n");
			goto done;
		}
	}

	failure = 0;

 done:

	return failure;
}

static int
test_tlsext_valid_hostnames(void)
{
	const struct tls_sni_test *tst;
	int failure = 0;
	size_t i;

	for (i = 0; i < N_TLS_SNI_TESTS; i++) {
		tst = &tls_sni_tests[i];
		failure |= test_tlsext_is_valid_hostname(tst);
	}

	return failure;
}

#define N_TLSEXT_RANDOMIZATION_TESTS 1000

static int
test_tlsext_check_psk_is_last_extension(SSL *ssl)
{
	const struct tls_extension *ext;
	uint16_t type;

	if (ssl->tlsext_build_order_len == 0) {
		FAIL("Unexpected zero build order length");
		return 1;
	}

	ext = ssl->tlsext_build_order[ssl->tlsext_build_order_len - 1];
	if ((type = tls_extension_type(ext)) != TLSEXT_TYPE_psk) {
		FAIL("last extension is %u, want %u\n", type, TLSEXT_TYPE_psk);
		return 1;
	}

	return 0;
}

static int
test_tlsext_randomized_extensions(SSL *ssl)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_TLSEXT_RANDOMIZATION_TESTS; i++) {
		if (!tlsext_randomize_build_order(ssl))
			errx(1, "failed to randomize extensions");
		failed |= test_tlsext_check_psk_is_last_extension(ssl);
	}

	return failed;
}

static int
test_tlsext_extension_order(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	int failure;

	failure = 0;

	if ((ssl_ctx = SSL_CTX_new(TLS_client_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	failure |= test_tlsext_randomized_extensions(ssl);

	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	if ((ssl_ctx = SSL_CTX_new(TLS_server_method())) == NULL)
		errx(1, "failed to create SSL_CTX");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL");

	failure |= test_tlsext_randomized_extensions(ssl);

	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	return failure;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	SSL_library_init();
	SSL_load_error_strings();

	failed |= test_tlsext_alpn_client();
	failed |= test_tlsext_alpn_server();

	failed |= test_tlsext_supportedgroups_client();
	failed |= test_tlsext_supportedgroups_server();

	failed |= test_tlsext_ecpf_client();
	failed |= test_tlsext_ecpf_server();

	failed |= test_tlsext_ri_client();
	failed |= test_tlsext_ri_server();

	failed |= test_tlsext_sigalgs_client();

	failed |= test_tlsext_sni_client();
	failed |= test_tlsext_sni_server();

	failed |= test_tlsext_ocsp_client();
	failed |= test_tlsext_ocsp_server();

	failed |= test_tlsext_sessionticket_client();
	failed |= test_tlsext_sessionticket_server();

	failed |= test_tlsext_versions_client();
	failed |= test_tlsext_versions_server();

	failed |= test_tlsext_keyshare_client();
	failed |= test_tlsext_keyshare_server();

	failed |= test_tlsext_cookie_client();
	failed |= test_tlsext_cookie_server();

#ifndef OPENSSL_NO_SRTP
	failed |= test_tlsext_srtp_client();
	failed |= test_tlsext_srtp_server();
#else
	fprintf(stderr, "Skipping SRTP tests due to OPENSSL_NO_SRTP\n");
#endif

	failed |= test_tlsext_psk_modes_client();
	failed |= test_tlsext_psk_modes_server();

	failed |= test_tlsext_clienthello_build();
	failed |= test_tlsext_serverhello_build();

	failed |= test_tlsext_valid_hostnames();

	failed |= test_tlsext_quic_transport_parameters_client();
	failed |= test_tlsext_quic_transport_parameters_server();

	failed |= test_tlsext_extension_order();

	return (failed);
}
