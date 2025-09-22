/*	$OpenBSD: tls_ext_alpn.c,v 1.9 2022/11/26 16:08:57 tb Exp $	*/
/*
 * Copyright (c) 2015 Doug Hogan <doug@openbsd.org>
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
 * Test TLS extension Application-Layer Protocol Negotiation (RFC 7301).
 */
#include <stdio.h>
#include <openssl/ssl.h>

#include "ssl_local.h"
#include "ssl_tlsext.h"

#include "tests.h"

/*
 * In the ProtocolNameList, ProtocolNames must not include empty strings and
 * byte strings must not be truncated.
 *
 * This uses some of the IANA approved protocol names from:
 * http://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml
 */

/* Valid for client and server since it only has one name. */
static uint8_t proto_single[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0f, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x0b, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x09, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31
};

/* Valid for client, but NOT server.  Server must have exactly one name. */
static uint8_t proto_multiple1[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x19, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x15, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x13, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
	/* opaque ProtocolName<1..2^8-1> -- 'stun.nat' */
	0x09, /* len */
	0x73, 0x74, 0x75, 0x6e, 0x2e, 0x74, 0x75, 0x72, 0x6e
};

/* Valid for client, but NOT server.  Server must have exactly one name. */
static uint8_t proto_multiple2[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x1c, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x18, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x16, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
	/* opaque ProtocolName<1..2^8-1> -- 'h2' */
	0x02, /* len */
	0x68, 0x32,
	/* opaque ProtocolName<1..2^8-1> -- 'stun.nat' */
	0x09, /* len */
	0x73, 0x74, 0x75, 0x6e, 0x2e, 0x74, 0x75, 0x72, 0x6e
};

/* Valid for client, but NOT server.  Server must have exactly one name. */
static uint8_t proto_multiple3[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x20, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x1c, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x1a, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
	/* opaque ProtocolName<1..2^8-1> -- 'h2' */
	0x02, /* len */
	0x68, 0x32,
	/* opaque ProtocolName<1..2^8-1> -- 'stun.nat' */
	0x09, /* len */
	0x73, 0x74, 0x75, 0x6e, 0x2e, 0x74, 0x75, 0x72, 0x6e,
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x03, /* len */
	0x68, 0x32, 0x63
};

static uint8_t proto_empty[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions. */
	0x00, 0x00, /* none present. */
};

/* Invalid for both client and server.  Length is wrong. */
static uint8_t proto_invalid_len1[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x06, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x04, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x04, /* XXX len too large */
	0x68, 0x32, 0x63
};
static uint8_t proto_invalid_len2[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x06, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x04, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x02, /* XXX len too small */
	0x68, 0x32, 0x63
};
static uint8_t proto_invalid_len3[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x06, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x03, /* XXX len too small */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x03, /* len */
	0x68, 0x32, 0x63
};
static uint8_t proto_invalid_len4[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x06, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x06, /* XXX len too large */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x03, /* len */
	0x68, 0x32, 0x63
};
static uint8_t proto_invalid_len5[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x01, 0x08, /* XXX len too large */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x04, /* len */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x03, /* len */
	0x68, 0x32, 0x63
};
static uint8_t proto_invalid_len6[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x05, /* XXX len too small */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x04, /* len */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x03, /* len */
	0x68, 0x32, 0x63
};
static uint8_t proto_invalid_len7[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x06, /* XXX len too small */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x06, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x04, /* len */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x03, /* len */
	0x68, 0x32, 0x63
};
static uint8_t proto_invalid_len8[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0b, /* XXX len too large */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x06, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x04, /* len */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	0x03, /* len */
	0x68, 0x32, 0x63
};

/* Invalid for client and server since it is missing data. */
static uint8_t proto_invalid_missing1[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x06, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x04, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'h2c' */
	/* XXX missing */
};
static uint8_t proto_invalid_missing2[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x00, /* XXX missing name list */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
};
static uint8_t proto_invalid_missing3[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x02, /* XXX size is sufficient but missing data for name list */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
};
static uint8_t proto_invalid_missing4[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x0a, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	/* XXX missing */
};
static uint8_t proto_invalid_missing5[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x1c, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x18, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x16, /* len of all names */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x08, /* len */
	0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
	/* opaque ProtocolName<1..2^8-1> -- 'h2' */
	0x02, /* len */
	0x68, 0x32,
	/* XXX missing name */
};
static uint8_t proto_invalid_missing6[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x07, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x03, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x01, /* XXX len must be at least 2 */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x00, /* XXX len cannot be 0 */
};
static uint8_t proto_invalid_missing7[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x07, /* len */
	/* ExtensionType extension_type */
	0x00, 0x10, /* ALPN */
	/* opaque extension_data<0..2^16-1> */
	0x00, 0x03, /* len */
	/* ProtocolName protocol_name_list<2..2^16-1> -- ALPN names */
	0x00, 0x02, /* XXX len is at least 2 but not correct. */
	/* opaque ProtocolName<1..2^8-1> -- 'http/1.1' */
	0x00, /* XXX len cannot be 0 */
};
static uint8_t proto_invalid_missing8[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x00, 0x01, /* len */
	/* ExtensionType extension_type */
	0x00, /* XXX need a 2 byte type */
};
static uint8_t proto_invalid_missing9[] = {
	/* Extension extensions<0..2^16-1> -- All TLS extensions */
	0x0a, /* XXX need a 2 byte len */
};


#define CHECK_BOTH(c_val, s_val, proto) do {				\
	{								\
		CBS cbs;						\
		int al;							\
									\
		CBS_init(&cbs, proto, sizeof(proto));			\
		CHECK(c_val == tlsext_server_parse(s, SSL_TLSEXT_MSG_CH, &cbs, &al)); \
		CBS_init(&cbs, proto, sizeof(proto));			\
		CHECK(s_val == tlsext_client_parse(s, SSL_TLSEXT_MSG_SH, &cbs, &al)); \
	}								\
} while (0)

static int dummy_alpn_cb(SSL *ssl, const unsigned char **out,
    unsigned char *outlen, const unsigned char *in, unsigned int inlen,
    void *arg);

static int
check_valid_alpn(SSL *s)
{
	const uint8_t str[] = {
		0x08, /* len */
		0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31 /* http/1.1 */
	};

	/* Setup in order to test ALPN. */
	CHECK(! SSL_set_alpn_protos(s, str, 9));
	SSL_CTX_set_alpn_select_cb(s->ctx, dummy_alpn_cb, NULL);

	/* Prerequisites to test these. */
	CHECK(s->alpn_client_proto_list != NULL);
	CHECK(s->ctx->alpn_select_cb != NULL);
	//CHECK(s->s3->tmp.finish_md_len == 0);

	CHECK_BOTH(1, 1, proto_single);
	CHECK_BOTH(1, 1, proto_empty);

	/* Multiple protocol names are only valid for client */
	CHECK_BOTH(1, 0, proto_multiple1);
	CHECK_BOTH(1, 0, proto_multiple2);
	CHECK_BOTH(1, 0, proto_multiple3);

	return 1;
}

/*
 * Some of the IANA approved IDs from:
 * http://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml
 */
static int
check_invalid_alpn(SSL *s)
{
	const uint8_t str[] = {
		0x08, /* len */
		0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31 /* http/1.1 */
	};

	/* Setup in order to test ALPN. */
	CHECK(! SSL_set_alpn_protos(s, str, 9));
	SSL_CTX_set_alpn_select_cb(s->ctx, dummy_alpn_cb, NULL);

	/* Prerequisites to test these. */
	CHECK(s->alpn_client_proto_list != NULL);
	CHECK(s->ctx->alpn_select_cb != NULL);
	//CHECK(s->s3->tmp.finish_md_len == 0);

	/* None of these are valid for client or server */
	CHECK_BOTH(0, 0, proto_invalid_len1);
	CHECK_BOTH(0, 0, proto_invalid_len2);
	CHECK_BOTH(0, 0, proto_invalid_len3);
	CHECK_BOTH(0, 0, proto_invalid_len4);
	CHECK_BOTH(0, 0, proto_invalid_len5);
	CHECK_BOTH(0, 0, proto_invalid_len6);
	CHECK_BOTH(0, 0, proto_invalid_len7);
	CHECK_BOTH(0, 0, proto_invalid_len8);
	CHECK_BOTH(0, 0, proto_invalid_missing1);
	CHECK_BOTH(0, 0, proto_invalid_missing2);
	CHECK_BOTH(0, 0, proto_invalid_missing3);
	CHECK_BOTH(0, 0, proto_invalid_missing4);
	CHECK_BOTH(0, 0, proto_invalid_missing5);
	CHECK_BOTH(0, 0, proto_invalid_missing6);
	CHECK_BOTH(0, 0, proto_invalid_missing7);
	CHECK_BOTH(0, 0, proto_invalid_missing8);
	CHECK_BOTH(0, 0, proto_invalid_missing9);

	return 1;
}

int
dummy_alpn_cb(SSL *ssl __attribute__((unused)), const unsigned char **out,
    unsigned char *outlen, const unsigned char *in, unsigned int inlen,
    void *arg __attribute__((unused)))
{
	*out = in;
	*outlen = (unsigned char)inlen;

	return 0;
}

int
main(void)
{
	SSL_CTX *ctx = NULL;
	SSL *s = NULL;
	int rv = 1;

	SSL_library_init();

	CHECK_GOTO((ctx = SSL_CTX_new(TLSv1_2_client_method())) != NULL);
	CHECK_GOTO((s = SSL_new(ctx)) != NULL);

	if (!check_valid_alpn(s))
		goto err;
	if (!check_invalid_alpn(s))
		goto err;

	rv = 0;

err:
	SSL_CTX_free(ctx);
	SSL_free(s);

	if (!rv)
		printf("PASS %s\n", __FILE__);
	return rv;
}
