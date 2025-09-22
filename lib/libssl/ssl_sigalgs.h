/* $OpenBSD: ssl_sigalgs.h,v 1.27 2024/02/03 15:58:34 beck Exp $ */
/*
 * Copyright (c) 2018-2019 Bob Beck <beck@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HEADER_SSL_SIGALGS_H
#define HEADER_SSL_SIGALGS_H

__BEGIN_HIDDEN_DECLS

#define SIGALG_NONE			0x0000

/*
 * RFC 8446 Section 4.2.3
 * RFC 5246 Section 7.4.1.4.1
 */
#define SIGALG_RSA_PKCS1_SHA224		0x0301
#define SIGALG_RSA_PKCS1_SHA256		0x0401
#define SIGALG_RSA_PKCS1_SHA384		0x0501
#define SIGALG_RSA_PKCS1_SHA512		0x0601
#define SIGALG_ECDSA_SECP224R1_SHA224	0x0303
#define SIGALG_ECDSA_SECP256R1_SHA256	0x0403
#define SIGALG_ECDSA_SECP384R1_SHA384	0x0503
#define SIGALG_ECDSA_SECP521R1_SHA512	0x0603
#define SIGALG_RSA_PSS_RSAE_SHA256	0x0804
#define SIGALG_RSA_PSS_RSAE_SHA384	0x0805
#define SIGALG_RSA_PSS_RSAE_SHA512	0x0806
#define SIGALG_ED25519			0x0807
#define SIGALG_ED448			0x0808
#define SIGALG_RSA_PSS_PSS_SHA256	0x0809
#define SIGALG_RSA_PSS_PSS_SHA384	0x080a
#define SIGALG_RSA_PSS_PSS_SHA512	0x080b
#define SIGALG_RSA_PKCS1_SHA1		0x0201
#define SIGALG_ECDSA_SHA1		0x0203
#define SIGALG_PRIVATE_START		0xFE00
#define SIGALG_PRIVATE_END		0xFFFF

/* Legacy sigalg for < TLSv1.2 same value as BoringSSL uses. */
#define SIGALG_RSA_PKCS1_MD5_SHA1	0xFF01

#define SIGALG_FLAG_RSA_PSS	0x00000001

struct ssl_sigalg {
	uint16_t value;
	int key_type;
	const EVP_MD *(*md)(void);
	int security_level;
	int group_nid;
	int flags;
};

int ssl_sigalgs_build(uint16_t tls_version, CBB *cbb, int security_level);
const struct ssl_sigalg *ssl_sigalg_select(SSL *s, EVP_PKEY *pkey);
const struct ssl_sigalg *ssl_sigalg_for_peer(SSL *s, EVP_PKEY *pkey,
    uint16_t sigalg_value);

__END_HIDDEN_DECLS

#endif
