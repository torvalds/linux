/* $OpenBSD: ssl_local.h,v 1.33 2025/05/10 06:04:36 tb Exp $ */
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
/* ====================================================================
 * Copyright (c) 1998-2007 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
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
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECC cipher suite support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */
/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

#ifndef HEADER_SSL_LOCL_H
#define HEADER_SSL_LOCL_H

#include <sys/types.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/opensslconf.h>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/stack.h>

#include "bytestring.h"
#include "tls_content.h"
#include "tls13_internal.h"

__BEGIN_HIDDEN_DECLS

#ifndef CTASSERT
#define CTASSERT(x)	extern char  _ctassert[(x) ? 1 : -1 ]   \
			    __attribute__((__unused__))
#endif

#ifndef LIBRESSL_HAS_DTLS1_2
#define LIBRESSL_HAS_DTLS1_2
#endif

/* LOCAL STUFF */

#define SSL_DECRYPT	0
#define SSL_ENCRYPT	1

/*
 * Define the Bitmasks for SSL_CIPHER.algorithms.
 * This bits are used packed as dense as possible. If new methods/ciphers
 * etc will be added, the bits a likely to change, so this information
 * is for internal library use only, even though SSL_CIPHER.algorithms
 * can be publicly accessed.
 * Use the according functions for cipher management instead.
 *
 * The bit mask handling in the selection and sorting scheme in
 * ssl_create_cipher_list() has only limited capabilities, reflecting
 * that the different entities within are mutually exclusive:
 * ONLY ONE BIT PER MASK CAN BE SET AT A TIME.
 */

/* Bits for algorithm_mkey (key exchange algorithm) */
#define SSL_kRSA		0x00000001L /* RSA key exchange */
#define SSL_kDHE		0x00000008L /* tmp DH key no DH cert */
#define SSL_kECDHE		0x00000080L /* ephemeral ECDH */
#define SSL_kTLS1_3		0x00000400L /* TLSv1.3 key exchange */

/* Bits for algorithm_auth (server authentication) */
#define SSL_aRSA		0x00000001L /* RSA auth */
#define SSL_aNULL		0x00000004L /* no auth (i.e. use ADH or AECDH) */
#define SSL_aECDSA		0x00000040L /* ECDSA auth*/
#define SSL_aTLS1_3		0x00000400L /* TLSv1.3 authentication */

/* Bits for algorithm_enc (symmetric encryption) */
#define SSL_3DES		0x00000002L
#define SSL_RC4			0x00000004L
#define SSL_eNULL		0x00000010L
#define SSL_AES128		0x00000020L
#define SSL_AES256		0x00000040L
#define SSL_CAMELLIA128		0x00000080L
#define SSL_CAMELLIA256		0x00000100L
#define SSL_AES128GCM		0x00000400L
#define SSL_AES256GCM		0x00000800L
#define SSL_CHACHA20POLY1305	0x00001000L

#define SSL_AES			(SSL_AES128|SSL_AES256|SSL_AES128GCM|SSL_AES256GCM)
#define SSL_CAMELLIA		(SSL_CAMELLIA128|SSL_CAMELLIA256)


/* Bits for algorithm_mac (symmetric authentication) */

#define SSL_MD5			0x00000001L
#define SSL_SHA1		0x00000002L
#define SSL_SHA256		0x00000010L
#define SSL_SHA384		0x00000020L
/* Not a real MAC, just an indication it is part of cipher */
#define SSL_AEAD		0x00000040L
#define SSL_STREEBOG256		0x00000080L

/* Bits for algorithm_ssl (protocol version) */
#define SSL_SSLV3		0x00000002L
#define SSL_TLSV1		SSL_SSLV3	/* for now */
#define SSL_TLSV1_2		0x00000004L
#define SSL_TLSV1_3		0x00000008L


/* Bits for algorithm2 (handshake digests and other extra flags) */

#define SSL_HANDSHAKE_MAC_MASK		0xff0
#define SSL_HANDSHAKE_MAC_SHA256	0x080
#define SSL_HANDSHAKE_MAC_SHA384	0x100

#define SSL3_CK_ID		0x03000000
#define SSL3_CK_VALUE_MASK	0x0000ffff

/*
 * Cipher strength information.
 */
#define SSL_STRONG_MASK		0x000001fcL
#define SSL_STRONG_NONE		0x00000004L
#define SSL_LOW			0x00000020L
#define SSL_MEDIUM		0x00000040L
#define SSL_HIGH		0x00000080L

/*
 * The keylength (measured in RSA key bits, I guess)  for temporary keys.
 * Cipher argument is so that this can be variable in the future.
 */
#define SSL_C_PKEYLENGTH(c)	1024

/* See if we use signature algorithms extension. */
#define SSL_USE_SIGALGS(s) \
	(s->method->enc_flags & SSL_ENC_FLAG_SIGALGS)

/* Allow TLS 1.2 ciphersuites: applies to DTLS 1.2 as well as TLS 1.2. */
#define SSL_USE_TLS1_2_CIPHERS(s) \
	(s->method->enc_flags & SSL_ENC_FLAG_TLS1_2_CIPHERS)

/* Allow TLS 1.3 ciphersuites only. */
#define SSL_USE_TLS1_3_CIPHERS(s) \
	(s->method->enc_flags & SSL_ENC_FLAG_TLS1_3_CIPHERS)

#define SSL_PKEY_RSA		0
#define SSL_PKEY_ECC		1
#define SSL_PKEY_NUM		2

#define SSL_MAX_EMPTY_RECORDS	32

/* SSL_kRSA <- RSA_ENC | (RSA_TMP & RSA_SIGN) |
 *	    <- (EXPORT & (RSA_ENC | RSA_TMP) & RSA_SIGN)
 * SSL_kDH  <- DH_ENC & (RSA_ENC | RSA_SIGN | DSA_SIGN)
 * SSL_kDHE <- RSA_ENC | RSA_SIGN | DSA_SIGN
 * SSL_aRSA <- RSA_ENC | RSA_SIGN
 * SSL_aDSS <- DSA_SIGN
 */

/* From RFC 4492, section 5.4. Only named curves are supported. */
#define NAMED_CURVE_TYPE	3

typedef struct ssl_cert_pkey_st {
	X509 *x509;
	EVP_PKEY *privatekey;
	STACK_OF(X509) *chain;
} SSL_CERT_PKEY;

typedef struct ssl_cert_st {
	/* Current active set */
	/* ALWAYS points to an element of the pkeys array
	 * Probably it would make more sense to store
	 * an index, not a pointer. */
	SSL_CERT_PKEY *key;

	SSL_CERT_PKEY pkeys[SSL_PKEY_NUM];

	/* The following masks are for the key and auth
	 * algorithms that are supported by the certs below */
	int valid;
	unsigned long mask_k;
	unsigned long mask_a;

	DH *dhe_params;
	DH *(*dhe_params_cb)(SSL *ssl, int is_export, int keysize);
	int dhe_params_auto;

	int (*security_cb)(const SSL *s, const SSL_CTX *ctx, int op, int bits,
	    int nid, void *other, void *ex_data); /* Not exposed in API. */
	int security_level;
	void *security_ex_data; /* Not exposed in API. */

	int references; /* >1 only if SSL_copy_session_id is used */
} SSL_CERT;

struct ssl_comp_st {
	int id;
	const char *name;
};

struct ssl_cipher_st {
	uint16_t value;			/* Cipher suite value. */

	const char *name;		/* text name */

	unsigned long algorithm_mkey;	/* key exchange algorithm */
	unsigned long algorithm_auth;	/* server authentication */
	unsigned long algorithm_enc;	/* symmetric encryption */
	unsigned long algorithm_mac;	/* symmetric authentication */
	unsigned long algorithm_ssl;	/* (major) protocol version */

	unsigned long algo_strength;	/* strength and export flags */
	unsigned long algorithm2;	/* Extra flags */
	int strength_bits;		/* Number of bits really used */
	int alg_bits;			/* Number of bits for algorithm */
};

struct ssl_method_st {
	int dtls;
	int server;
	int version;

	uint16_t min_tls_version;
	uint16_t max_tls_version;

	int (*ssl_new)(SSL *s);
	void (*ssl_clear)(SSL *s);
	void (*ssl_free)(SSL *s);

	int (*ssl_accept)(SSL *s);
	int (*ssl_connect)(SSL *s);
	int (*ssl_shutdown)(SSL *s);

	int (*ssl_renegotiate)(SSL *s);
	int (*ssl_renegotiate_check)(SSL *s);

	int (*ssl_pending)(const SSL *s);
	int (*ssl_read_bytes)(SSL *s, int type, unsigned char *buf, int len,
	    int peek);
	int (*ssl_write_bytes)(SSL *s, int type, const void *buf_, int len);

	unsigned int enc_flags;		/* SSL_ENC_FLAG_* */
};

/*
 * Let's make this into an ASN.1 type structure as follows
 * SSL_SESSION_ID ::= SEQUENCE {
 *	version			INTEGER,	-- structure version number
 *	SSLversion		INTEGER,	-- SSL version number
 *	Cipher			OCTET STRING,	-- the 2 byte cipher ID
 *	Session_ID		OCTET STRING,	-- the Session ID
 *	Master_key		OCTET STRING,	-- the master key
 *	KRB5_principal		OCTET STRING	-- optional Kerberos principal
 *	Time [ 1 ] EXPLICIT	INTEGER,	-- optional Start Time
 *	Timeout [ 2 ] EXPLICIT	INTEGER,	-- optional Timeout ins seconds
 *	Peer [ 3 ] EXPLICIT	X509,		-- optional Peer Certificate
 *	Session_ID_context [ 4 ] EXPLICIT OCTET STRING,   -- the Session ID context
 *	Verify_result [ 5 ] EXPLICIT INTEGER,   -- X509_V_... code for `Peer'
 *	HostName [ 6 ] EXPLICIT OCTET STRING,   -- optional HostName from servername TLS extension
 *	PSK_identity_hint [ 7 ] EXPLICIT OCTET STRING, -- optional PSK identity hint
 *	PSK_identity [ 8 ] EXPLICIT OCTET STRING,  -- optional PSK identity
 *	Ticket_lifetime_hint [9] EXPLICIT INTEGER, -- server's lifetime hint for session ticket
 *	Ticket [10]		EXPLICIT OCTET STRING, -- session ticket (clients only)
 *	Compression_meth [11]   EXPLICIT OCTET STRING, -- optional compression method
 *	SRP_username [ 12 ] EXPLICIT OCTET STRING -- optional SRP username
 * }
 * Look in ssl/ssl_asn1.c for more details
 * I'm using EXPLICIT tags so I can read the damn things using asn1parse :-).
 */
struct ssl_session_st {
	int ssl_version;	/* what ssl version session info is
				 * being kept in here? */

	size_t master_key_length;
	unsigned char master_key[SSL_MAX_MASTER_KEY_LENGTH];

	/* session_id - valid? */
	size_t session_id_length;
	unsigned char session_id[SSL_MAX_SSL_SESSION_ID_LENGTH];

	/* this is used to determine whether the session is being reused in
	 * the appropriate context. It is up to the application to set this,
	 * via SSL_new */
	size_t sid_ctx_length;
	unsigned char sid_ctx[SSL_MAX_SID_CTX_LENGTH];

	/* Peer provided leaf (end-entity) certificate. */
	X509 *peer_cert;
	int peer_cert_type;

	/* when app_verify_callback accepts a session where the peer's certificate
	 * is not ok, we must remember the error for session reuse: */
	long verify_result; /* only for servers */

	long timeout;
	time_t time;
	int references;

	uint16_t cipher_value;

	char *tlsext_hostname;

	/* Session resumption - RFC 5077 and RFC 8446. */
	unsigned char *tlsext_tick;		/* Session ticket */
	size_t tlsext_ticklen;			/* Session ticket length */
	uint32_t tlsext_tick_lifetime_hint;	/* Session lifetime hint in seconds */
	uint32_t tlsext_tick_age_add; /* TLSv1.3 ticket age obfuscation (in ms) */
	struct tls13_secret resumption_master_secret;

	CRYPTO_EX_DATA ex_data; /* application specific data */

	/* These are used to make removal of session-ids more
	 * efficient and to implement a maximum cache size. */
	struct ssl_session_st *prev, *next;

	/* Used to indicate that session resumption is not allowed.
	 * Applications can also set this bit for a new session via
	 * not_resumable_session_cb to disable session caching and tickets. */
	int not_resumable;

	size_t tlsext_ecpointformatlist_length;
	uint8_t *tlsext_ecpointformatlist; /* peer's list */
	size_t tlsext_supportedgroups_length;
	uint16_t *tlsext_supportedgroups; /* peer's list */
};

struct ssl_sigalg;

typedef struct ssl_handshake_tls12_st {
	/* Used when SSL_ST_FLUSH_DATA is entered. */
	int next_state;

	/* Handshake message type and size. */
	int message_type;
	unsigned long message_size;

	/* Reuse current handshake message. */
	int reuse_message;

	/* Client certificate requests. */
	int cert_request;
	STACK_OF(X509_NAME) *ca_names;

	/* Record-layer key block for TLS 1.2 and earlier. */
	struct tls12_key_block *key_block;

	/* Transcript hash prior to sending certificate verify message. */
	uint8_t cert_verify[EVP_MAX_MD_SIZE];
} SSL_HANDSHAKE_TLS12;

typedef struct ssl_handshake_tls13_st {
	int use_legacy;
	int hrr;

	/* Client indicates psk_dhe_ke support in PskKeyExchangeMode. */
	int use_psk_dhe_ke;

	/* Certificate selected for use (static pointer). */
	const SSL_CERT_PKEY *cpk;

	/* Version proposed by peer server. */
	uint16_t server_version;

	uint16_t server_group;
	struct tls13_secrets *secrets;

	uint8_t *cookie;
	size_t cookie_len;

	/* Preserved transcript hash. */
	uint8_t transcript_hash[EVP_MAX_MD_SIZE];
	size_t transcript_hash_len;

	/* Legacy session ID. */
	uint8_t legacy_session_id[SSL_MAX_SSL_SESSION_ID_LENGTH];
	size_t legacy_session_id_len;

	/* ClientHello hash, used to validate following HelloRetryRequest */
	EVP_MD_CTX *clienthello_md_ctx;
	unsigned char *clienthello_hash;
	unsigned int clienthello_hash_len;

	/* QUIC read buffer and read/write encryption levels. */
	struct tls_buffer *quic_read_buffer;
	enum ssl_encryption_level_t quic_read_level;
	enum ssl_encryption_level_t quic_write_level;
} SSL_HANDSHAKE_TLS13;

typedef struct ssl_handshake_st {
	/*
	 * Minimum and maximum versions supported for this handshake. These are
	 * initialised at the start of a handshake based on the method in use
	 * and the current protocol version configuration.
	 */
	uint16_t our_min_tls_version;
	uint16_t our_max_tls_version;

	/*
	 * Version negotiated for this session. For a client this is set once
	 * the server selected version is parsed from the ServerHello (either
	 * from the legacy version or supported versions extension). For a
	 * server this is set once we select the version we will use with the
	 * client.
	 */
	uint16_t negotiated_tls_version;

	/*
	 * Legacy version advertised by our peer. For a server this is the
	 * version specified by the client in the ClientHello message. For a
	 * client, this is the version provided in the ServerHello message.
	 */
	uint16_t peer_legacy_version;

	/*
	 * Current handshake state - contains one of the SSL3_ST_* values and
	 * is used by the TLSv1.2 state machine, as well as being updated by
	 * the TLSv1.3 stack due to it being exposed externally.
	 */
	int state;

	/* Cipher being negotiated in this handshake. */
	const SSL_CIPHER *cipher;

	/* Ciphers sent by the client. */
	STACK_OF(SSL_CIPHER) *client_ciphers;

	/* Extensions seen in this handshake. */
	uint32_t extensions_seen;

	/* Extensions processed in this handshake. */
	uint32_t extensions_processed;

	/* Signature algorithms selected for use (static pointers). */
	const struct ssl_sigalg *our_sigalg;
	const struct ssl_sigalg *peer_sigalg;

	/* sigalgs offered in this handshake in wire form */
	uint8_t *sigalgs;
	size_t sigalgs_len;

	/* Key share for ephemeral key exchange. */
	struct tls_key_share *key_share;

	/*
	 * Copies of the verify data sent in our finished message and the
	 * verify data received in the finished message sent by our peer.
	 */
	uint8_t finished[EVP_MAX_MD_SIZE];
	size_t finished_len;
	uint8_t peer_finished[EVP_MAX_MD_SIZE];
	size_t peer_finished_len;

	/* List of certificates received from our peer. */
	STACK_OF(X509) *peer_certs;
	STACK_OF(X509) *peer_certs_no_leaf;

	/* Certificate chain resulting from X.509 verification. */
	STACK_OF(X509) *verified_chain;

	SSL_HANDSHAKE_TLS12 tls12;
	SSL_HANDSHAKE_TLS13 tls13;
} SSL_HANDSHAKE;

typedef struct tls_session_ticket_ext_st TLS_SESSION_TICKET_EXT;

/* TLS Session Ticket extension struct. */
struct tls_session_ticket_ext_st {
	unsigned short length;
	void *data;
};

struct tls12_key_block;

struct tls12_key_block *tls12_key_block_new(void);
void tls12_key_block_free(struct tls12_key_block *kb);
void tls12_key_block_client_write(struct tls12_key_block *kb, CBS *mac_key,
    CBS *key, CBS *iv);
void tls12_key_block_server_write(struct tls12_key_block *kb, CBS *mac_key,
    CBS *key, CBS *iv);
int tls12_key_block_generate(struct tls12_key_block *kb, SSL *s,
    const EVP_AEAD *aead, const EVP_CIPHER *cipher, const EVP_MD *mac_hash);

struct tls12_record_layer;

struct tls12_record_layer *tls12_record_layer_new(void);
void tls12_record_layer_free(struct tls12_record_layer *rl);
void tls12_record_layer_alert(struct tls12_record_layer *rl,
    uint8_t *alert_desc);
int tls12_record_layer_write_overhead(struct tls12_record_layer *rl,
    size_t *overhead);
int tls12_record_layer_read_protected(struct tls12_record_layer *rl);
int tls12_record_layer_write_protected(struct tls12_record_layer *rl);
void tls12_record_layer_set_aead(struct tls12_record_layer *rl,
    const EVP_AEAD *aead);
void tls12_record_layer_set_cipher_hash(struct tls12_record_layer *rl,
    const EVP_CIPHER *cipher, const EVP_MD *handshake_hash,
    const EVP_MD *mac_hash);
void tls12_record_layer_set_version(struct tls12_record_layer *rl,
    uint16_t version);
void tls12_record_layer_set_initial_epoch(struct tls12_record_layer *rl,
    uint16_t epoch);
uint16_t tls12_record_layer_read_epoch(struct tls12_record_layer *rl);
uint16_t tls12_record_layer_write_epoch(struct tls12_record_layer *rl);
int tls12_record_layer_use_write_epoch(struct tls12_record_layer *rl,
    uint16_t epoch);
void tls12_record_layer_write_epoch_done(struct tls12_record_layer *rl,
    uint16_t epoch);
void tls12_record_layer_clear_read_state(struct tls12_record_layer *rl);
void tls12_record_layer_clear_write_state(struct tls12_record_layer *rl);
void tls12_record_layer_reflect_seq_num(struct tls12_record_layer *rl);
int tls12_record_layer_change_read_cipher_state(struct tls12_record_layer *rl,
    CBS *mac_key, CBS *key, CBS *iv);
int tls12_record_layer_change_write_cipher_state(struct tls12_record_layer *rl,
    CBS *mac_key, CBS *key, CBS *iv);
int tls12_record_layer_open_record(struct tls12_record_layer *rl,
    uint8_t *buf, size_t buf_len, struct tls_content *out);
int tls12_record_layer_seal_record(struct tls12_record_layer *rl,
    uint8_t content_type, const uint8_t *content, size_t content_len,
    CBB *out);

typedef void (ssl_info_callback_fn)(const SSL *s, int type, int val);
typedef void (ssl_msg_callback_fn)(int is_write, int version, int content_type,
    const void *buf, size_t len, SSL *ssl, void *arg);

struct ssl_ctx_st {
	const SSL_METHOD *method;
	const SSL_QUIC_METHOD *quic_method;

	STACK_OF(SSL_CIPHER) *cipher_list;

	struct x509_store_st /* X509_STORE */ *cert_store;

	/* If timeout is not 0, it is the default timeout value set
	 * when SSL_new() is called.  This has been put in to make
	 * life easier to set things up */
	long session_timeout;

	int references;

	/* Default values to use in SSL structures follow (these are copied by SSL_new) */

	STACK_OF(X509) *extra_certs;

	int verify_mode;
	size_t sid_ctx_length;
	unsigned char sid_ctx[SSL_MAX_SID_CTX_LENGTH];

	X509_VERIFY_PARAM *param;

	/*
	 * XXX
	 * default_passwd_cb used by python and openvpn, need to keep it until we
	 * add an accessor
	 */
	/* Default password callback. */
	pem_password_cb *default_passwd_callback;

	/* Default password callback user data. */
	void *default_passwd_callback_userdata;

	uint16_t min_tls_version;
	uint16_t max_tls_version;

	/*
	 * These may be zero to imply minimum or maximum version supported by
	 * the method.
	 */
	uint16_t min_proto_version;
	uint16_t max_proto_version;

	unsigned long options;
	unsigned long mode;

	/* If this callback is not null, it will be called each
	 * time a session id is added to the cache.  If this function
	 * returns 1, it means that the callback will do a
	 * SSL_SESSION_free() when it has finished using it.  Otherwise,
	 * on 0, it means the callback has finished with it.
	 * If remove_session_cb is not null, it will be called when
	 * a session-id is removed from the cache.  After the call,
	 * OpenSSL will SSL_SESSION_free() it. */
	int (*new_session_cb)(struct ssl_st *ssl, SSL_SESSION *sess);
	void (*remove_session_cb)(struct ssl_ctx_st *ctx, SSL_SESSION *sess);
	SSL_SESSION *(*get_session_cb)(struct ssl_st *ssl,
	    const unsigned char *data, int len, int *copy);

	/* if defined, these override the X509_verify_cert() calls */
	int (*app_verify_callback)(X509_STORE_CTX *, void *);
	    void *app_verify_arg;

	/* get client cert callback */
	int (*client_cert_cb)(SSL *ssl, X509 **x509, EVP_PKEY **pkey);

	/* cookie generate callback */
	int (*app_gen_cookie_cb)(SSL *ssl, unsigned char *cookie,
	    unsigned int *cookie_len);

	/* verify cookie callback */
	int (*app_verify_cookie_cb)(SSL *ssl, const unsigned char *cookie,
	    unsigned int cookie_len);

	ssl_info_callback_fn *info_callback;

	/* callback that allows applications to peek at protocol messages */
	ssl_msg_callback_fn *msg_callback;
	void *msg_callback_arg;

	int (*default_verify_callback)(int ok,X509_STORE_CTX *ctx); /* called 'verify_callback' in the SSL */

	/* Default generate session ID callback. */
	GEN_SESSION_CB generate_session_id;

	/* TLS extensions servername callback */
	int (*tlsext_servername_callback)(SSL*, int *, void *);
	void *tlsext_servername_arg;

	/* Callback to support customisation of ticket key setting */
	int (*tlsext_ticket_key_cb)(SSL *ssl, unsigned char *name,
	    unsigned char *iv, EVP_CIPHER_CTX *ectx, HMAC_CTX *hctx, int enc);

	/* certificate status request info */
	/* Callback for status request */
	int (*tlsext_status_cb)(SSL *ssl, void *arg);
	void *tlsext_status_arg;

	struct lhash_st_SSL_SESSION *sessions;

	/* Most session-ids that will be cached, default is
	 * SSL_SESSION_CACHE_MAX_SIZE_DEFAULT. 0 is unlimited. */
	unsigned long session_cache_size;
	struct ssl_session_st *session_cache_head;
	struct ssl_session_st *session_cache_tail;

	/* This can have one of 2 values, ored together,
	 * SSL_SESS_CACHE_CLIENT,
	 * SSL_SESS_CACHE_SERVER,
	 * Default is SSL_SESSION_CACHE_SERVER, which means only
	 * SSL_accept which cache SSL_SESSIONS. */
	int session_cache_mode;

	struct {
		int sess_connect;	/* SSL new conn - started */
		int sess_connect_renegotiate;/* SSL reneg - requested */
		int sess_connect_good;	/* SSL new conne/reneg - finished */
		int sess_accept;	/* SSL new accept - started */
		int sess_accept_renegotiate;/* SSL reneg - requested */
		int sess_accept_good;	/* SSL accept/reneg - finished */
		int sess_miss;		/* session lookup misses  */
		int sess_timeout;	/* reuse attempt on timeouted session */
		int sess_cache_full;	/* session removed due to full cache */
		int sess_hit;		/* session reuse actually done */
		int sess_cb_hit;	/* session-id that was not
					 * in the cache was
					 * passed back via the callback.  This
					 * indicates that the application is
					 * supplying session-id's from other
					 * processes - spooky :-) */
	} stats;

	CRYPTO_EX_DATA ex_data;

	STACK_OF(SSL_CIPHER) *cipher_list_tls13;

	SSL_CERT *cert;

	/* Default values used when no per-SSL value is defined follow */

	/* what we put in client cert requests */
	STACK_OF(X509_NAME) *client_CA;

	long max_cert_list;

	int read_ahead;

	int quiet_shutdown;

	/* Maximum amount of data to send in one fragment.
	 * actual record size can be more than this due to
	 * padding and MAC overheads.
	 */
	unsigned int max_send_fragment;

	/* RFC 4507 session ticket keys */
	unsigned char tlsext_tick_key_name[16];
	unsigned char tlsext_tick_hmac_key[16];
	unsigned char tlsext_tick_aes_key[16];

	/* SRTP profiles we are willing to do from RFC 5764 */
	STACK_OF(SRTP_PROTECTION_PROFILE) *srtp_profiles;

	/*
	 * ALPN information.
	 */

	/*
	 * Server callback function that allows the server to select the
	 * protocol for the connection.
	 *   out: on successful return, this must point to the raw protocol
	 *       name (without the length prefix).
	 *   outlen: on successful return, this contains the length of out.
	 *   in: points to the client's list of supported protocols in
	 *       wire-format.
	 *   inlen: the length of in.
	 */
	int (*alpn_select_cb)(SSL *s, const unsigned char **out,
	    unsigned char *outlen, const unsigned char *in, unsigned int inlen,
	    void *arg);
	void *alpn_select_cb_arg;

	/* Client list of supported protocols in wire format. */
	uint8_t *alpn_client_proto_list;
	size_t alpn_client_proto_list_len;

	size_t tlsext_ecpointformatlist_length;
	uint8_t *tlsext_ecpointformatlist; /* our list */
	size_t tlsext_supportedgroups_length;
	uint16_t *tlsext_supportedgroups; /* our list */
	SSL_CTX_keylog_cb_func keylog_callback; /* Unused. For OpenSSL compatibility. */
	size_t num_tickets; /* Unused, for OpenSSL compatibility */
};

struct ssl_st {
	/* protocol version
	 * (one of SSL2_VERSION, SSL3_VERSION, TLS1_VERSION, DTLS1_VERSION)
	 */
	int version;

	const SSL_METHOD *method;
	const SSL_QUIC_METHOD *quic_method;

	/* There are 2 BIO's even though they are normally both the
	 * same.  This is so data can be read and written to different
	 * handlers */

	BIO *rbio; /* used by SSL_read */
	BIO *wbio; /* used by SSL_write */
	BIO *bbio; /* used during session-id reuse to concatenate
		    * messages */
	int server;	/* are we the server side? - mostly used by SSL_clear*/

	struct ssl3_state_st *s3; /* SSLv3 variables */
	struct dtls1_state_st *d1; /* DTLSv1 variables */

	X509_VERIFY_PARAM *param;

	/* crypto */
	STACK_OF(SSL_CIPHER) *cipher_list;

	/* This is used to hold the server certificate used */
	SSL_CERT *cert;

	/* the session_id_context is used to ensure sessions are only reused
	 * in the appropriate context */
	size_t sid_ctx_length;
	unsigned char sid_ctx[SSL_MAX_SID_CTX_LENGTH];

	/* This can also be in the session once a session is established */
	SSL_SESSION *session;

	/* Used in SSL2 and SSL3 */
	int verify_mode;	/* 0 don't care about verify failure.
				 * 1 fail if verify fails */
	int error;		/* error bytes to be written */
	int error_code;		/* actual code */

	SSL_CTX *ctx;

	long verify_result;

	int references;

	int client_version;	/* what was passed, used for
				 * SSLv3/TLS rollback check */

	unsigned int max_send_fragment;

	const struct tls_extension **tlsext_build_order;
	size_t tlsext_build_order_len;

	char *tlsext_hostname;

	/* certificate status request info */
	/* Status type or -1 if no status type */
	int tlsext_status_type;

	SSL_CTX * initial_ctx; /* initial ctx, used to store sessions */
#define session_ctx initial_ctx

	struct tls13_ctx *tls13;

	uint16_t min_tls_version;
	uint16_t max_tls_version;

	/*
	 * These may be zero to imply minimum or maximum version supported by
	 * the method.
	 */
	uint16_t min_proto_version;
	uint16_t max_proto_version;

	unsigned long options; /* protocol behaviour */
	unsigned long mode; /* API behaviour */

	/* Client list of supported protocols in wire format. */
	uint8_t *alpn_client_proto_list;
	size_t alpn_client_proto_list_len;

	/* QUIC transport params we will send */
	uint8_t *quic_transport_params;
	size_t quic_transport_params_len;

	/* XXX Callbacks */

	/* true when we are actually in SSL_accept() or SSL_connect() */
	int in_handshake;
	int (*handshake_func)(SSL *);

	ssl_info_callback_fn *info_callback;

	/* callback that allows applications to peek at protocol messages */
	ssl_msg_callback_fn *msg_callback;
	void *msg_callback_arg;

	int (*verify_callback)(int ok,X509_STORE_CTX *ctx); /* fail if callback returns 0 */

	/* Default generate session ID callback. */
	GEN_SESSION_CB generate_session_id;

	/* TLS extension debug callback */
	void (*tlsext_debug_cb)(SSL *s, int client_server, int type,
	    unsigned char *data, int len, void *arg);
	void *tlsext_debug_arg;

	/* TLS Session Ticket extension callback */
	tls_session_ticket_ext_cb_fn tls_session_ticket_ext_cb;
	void *tls_session_ticket_ext_cb_arg;

	/* TLS pre-shared secret session resumption */
	tls_session_secret_cb_fn tls_session_secret_cb;
	void *tls_session_secret_cb_arg;

	/* XXX non-callback */

	/* This holds a variable that indicates what we were doing
	 * when a 0 or -1 is returned.  This is needed for
	 * non-blocking IO so we know what request needs re-doing when
	 * in SSL_accept or SSL_connect */
	int rwstate;

	/* Imagine that here's a boolean member "init" that is
	 * switched as soon as SSL_set_{accept/connect}_state
	 * is called for the first time, so that "state" and
	 * "handshake_func" are properly initialized.  But as
	 * handshake_func is == 0 until then, we use this
	 * test instead of an "init" member.
	 */

	int new_session;/* Generate a new session or reuse an old one.
			 * NB: For servers, the 'new' session may actually be a previously
			 * cached session or even the previous session unless
			 * SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION is set */
	int quiet_shutdown;/* don't send shutdown packets */
	int shutdown;	/* we have shut things down, 0x01 sent, 0x02
			 * for received */
	BUF_MEM *init_buf;	/* buffer used during init */
	void *init_msg;		/* pointer to handshake message body, set by ssl3_get_message() */
	int init_num;		/* amount read/written */
	int init_off;		/* amount read/written */

	/* used internally to point at a raw packet */
	unsigned char *packet;
	unsigned int packet_length;

	int read_ahead;		/* Read as many input bytes as possible
				 * (for non-blocking reads) */

	int hit;		/* reusing a previous session */

	STACK_OF(SSL_CIPHER) *cipher_list_tls13;

	struct tls12_record_layer *rl;

	/* session info */

	/* extra application data */
	CRYPTO_EX_DATA ex_data;

	/* client cert? */
	/* for server side, keep the list of CA_dn we can use */
	STACK_OF(X509_NAME) *client_CA;

	long max_cert_list;
	int first_packet;

	/* Expect OCSP CertificateStatus message */
	int tlsext_status_expected;
	/* OCSP status request only */
	STACK_OF(OCSP_RESPID) *tlsext_ocsp_ids;
	X509_EXTENSIONS *tlsext_ocsp_exts;

	/* OCSP response received or to be sent */
	unsigned char *tlsext_ocsp_resp;
	size_t tlsext_ocsp_resp_len;

	/* RFC4507 session ticket expected to be received or sent */
	int tlsext_ticket_expected;

	size_t tlsext_ecpointformatlist_length;
	uint8_t *tlsext_ecpointformatlist; /* our list */
	size_t tlsext_supportedgroups_length;
	uint16_t *tlsext_supportedgroups; /* our list */

	/* TLS Session Ticket extension override */
	TLS_SESSION_TICKET_EXT *tlsext_session_ticket;

	STACK_OF(SRTP_PROTECTION_PROFILE) *srtp_profiles;	/* What we'll do */
	const SRTP_PROTECTION_PROFILE *srtp_profile;		/* What's been chosen */

	int renegotiate;/* 1 if we are renegotiating.
			 * 2 if we are a server and are inside a handshake
			 * (i.e. not just sending a HelloRequest) */

	int rstate;	/* where we are when reading */

	int mac_packet;

	int empty_record_count;

	size_t num_tickets; /* Unused, for OpenSSL compatibility */
};

typedef struct ssl3_record_internal_st {
	int type;               /* type of record */
	unsigned int length;    /* How many bytes available */
	unsigned int padding_length; /* Number of padding bytes. */
	unsigned int off;       /* read/write offset into 'buf' */
	unsigned char *data;    /* pointer to the record data */
	unsigned char *input;   /* where the decode bytes are */
	uint16_t epoch;		/* epoch number, needed by DTLS1 */
	unsigned char seq_num[8]; /* sequence number, needed by DTLS1 */
} SSL3_RECORD_INTERNAL;

typedef struct ssl3_buffer_internal_st {
	unsigned char *buf;	/* at least SSL3_RT_MAX_PACKET_SIZE bytes,
				 * see ssl3_setup_buffers() */
	size_t len;		/* buffer size */
	int offset;		/* where to 'copy from' */
	int left;		/* how many bytes left */
} SSL3_BUFFER_INTERNAL;

typedef struct ssl3_state_st {
	long flags;

	unsigned char server_random[SSL3_RANDOM_SIZE];
	unsigned char client_random[SSL3_RANDOM_SIZE];

	SSL3_BUFFER_INTERNAL rbuf;	/* read IO goes into here */
	SSL3_BUFFER_INTERNAL wbuf;	/* write IO goes into here */

	SSL3_RECORD_INTERNAL rrec;	/* each decoded record goes in here */

	struct tls_content *rcontent;	/* Content from opened TLS records. */

	/* we allow one fatal and one warning alert to be outstanding,
	 * send close alert via the warning alert */
	int alert_dispatch;
	unsigned char send_alert[2];

	/* flags for countermeasure against known-IV weakness */
	int need_empty_fragments;
	int empty_fragment_done;

	/* Unprocessed Alert/Handshake protocol data. */
	struct tls_buffer *alert_fragment;
	struct tls_buffer *handshake_fragment;

	/* partial write - check the numbers match */
	unsigned int wnum;	/* number of bytes sent so far */
	int wpend_tot;		/* number bytes written */
	int wpend_type;
	int wpend_ret;		/* number of bytes submitted */
	const unsigned char *wpend_buf;

	/* Transcript of handshake messages that have been sent and received. */
	struct tls_buffer *handshake_transcript;

	/* Rolling hash of handshake messages. */
	EVP_MD_CTX *handshake_hash;

	/* this is set whenerver we see a change_cipher_spec message
	 * come in when we are not looking for one */
	int change_cipher_spec;

	int warn_alert;
	int fatal_alert;

	/* This flag is set when we should renegotiate ASAP, basically when
	 * there is no more data in the read or write buffers */
	int renegotiate;
	int total_renegotiations;
	int num_renegotiations;

	int in_read_app_data;

	SSL_HANDSHAKE hs;

	/* Connection binding to prevent renegotiation attacks */
	unsigned char previous_client_finished[EVP_MAX_MD_SIZE];
	unsigned char previous_client_finished_len;
	unsigned char previous_server_finished[EVP_MAX_MD_SIZE];
	unsigned char previous_server_finished_len;
	int send_connection_binding; /* TODOEKR */

	/* Set if we saw a Renegotiation Indication extension from our peer. */
	int renegotiate_seen;

	/*
	 * ALPN information.
	 *
	 * In a server these point to the selected ALPN protocol after the
	 * ClientHello has been processed. In a client these contain the
	 * protocol that the server selected once the ServerHello has been
	 * processed.
	 */
	uint8_t *alpn_selected;
	size_t alpn_selected_len;

	/* Contains the QUIC transport params received from our peer. */
	uint8_t *peer_quic_transport_params;
	size_t peer_quic_transport_params_len;
} SSL3_STATE;

/*
 * Flag values for enc_flags.
 */

/* Uses signature algorithms extension. */
#define SSL_ENC_FLAG_SIGALGS            (1 << 1)

/* Allow TLS 1.2 ciphersuites: applies to DTLS 1.2 as well as TLS 1.2. */
#define SSL_ENC_FLAG_TLS1_2_CIPHERS     (1 << 4)

/* Allow TLS 1.3 ciphersuites only. */
#define SSL_ENC_FLAG_TLS1_3_CIPHERS     (1 << 5)

#define TLSV1_ENC_FLAGS		0
#define TLSV1_1_ENC_FLAGS	0
#define TLSV1_2_ENC_FLAGS	(SSL_ENC_FLAG_SIGALGS		| \
				 SSL_ENC_FLAG_TLS1_2_CIPHERS)
#define TLSV1_3_ENC_FLAGS	(SSL_ENC_FLAG_SIGALGS		| \
				 SSL_ENC_FLAG_TLS1_3_CIPHERS)

extern const SSL_CIPHER ssl3_ciphers[];

const char *ssl_version_string(int ver);
int ssl_version_set_min(const SSL_METHOD *meth, uint16_t proto_ver,
    uint16_t max_tls_ver, uint16_t *out_tls_ver, uint16_t *out_proto_ver);
int ssl_version_set_max(const SSL_METHOD *meth, uint16_t proto_ver,
    uint16_t min_tls_ver, uint16_t *out_tls_ver, uint16_t *out_proto_ver);
int ssl_enabled_tls_version_range(SSL *s, uint16_t *min_ver, uint16_t *max_ver);
int ssl_supported_tls_version_range(SSL *s, uint16_t *min_ver, uint16_t *max_ver);
uint16_t ssl_tls_version(uint16_t version);
uint16_t ssl_effective_tls_version(SSL *s);
int ssl_max_supported_version(SSL *s, uint16_t *max_ver);
int ssl_max_legacy_version(SSL *s, uint16_t *max_ver);
int ssl_max_shared_version(SSL *s, uint16_t peer_ver, uint16_t *max_ver);
int ssl_check_version_from_server(SSL *s, uint16_t server_version);
int ssl_legacy_stack_version(SSL *s, uint16_t version);
int ssl_cipher_in_list(STACK_OF(SSL_CIPHER) *ciphers, const SSL_CIPHER *cipher);
int ssl_cipher_allowed_in_tls_version_range(const SSL_CIPHER *cipher,
    uint16_t min_ver, uint16_t max_ver);

const SSL_METHOD *tls_legacy_method(void);
const SSL_METHOD *ssl_get_method(uint16_t version);

void ssl_clear_cipher_state(SSL *s);
int ssl_clear_bad_session(SSL *s);

void ssl_info_callback(const SSL *s, int type, int value);
void ssl_msg_callback(SSL *s, int is_write, int content_type,
    const void *msg_buf, size_t msg_len);
void ssl_msg_callback_cbs(SSL *s, int is_write, int content_type, CBS *cbs);

SSL_CERT *ssl_cert_new(void);
SSL_CERT *ssl_cert_dup(SSL_CERT *cert);
void ssl_cert_free(SSL_CERT *c);
SSL_CERT *ssl_get0_cert(SSL_CTX *ctx, SSL *ssl);
int ssl_cert_set0_chain(SSL_CTX *ctx, SSL *ssl, STACK_OF(X509) *chain);
int ssl_cert_set1_chain(SSL_CTX *ctx, SSL *ssl, STACK_OF(X509) *chain);
int ssl_cert_add0_chain_cert(SSL_CTX *ctx, SSL *ssl, X509 *cert);
int ssl_cert_add1_chain_cert(SSL_CTX *ctx, SSL *ssl, X509 *cert);

int ssl_security_default_cb(const SSL *ssl, const SSL_CTX *ctx, int op,
    int bits, int nid, void *other, void *ex_data);

int ssl_security_cipher_check(const SSL *ssl, SSL_CIPHER *cipher);
int ssl_security_shared_cipher(const SSL *ssl, SSL_CIPHER *cipher);
int ssl_security_supported_cipher(const SSL *ssl, SSL_CIPHER *cipher);
int ssl_ctx_security_dh(const SSL_CTX *ctx, DH *dh);
int ssl_security_dh(const SSL *ssl, DH *dh);
int ssl_security_sigalg_check(const SSL *ssl, const EVP_PKEY *pkey);
int ssl_security_tickets(const SSL *ssl);
int ssl_security_version(const SSL *ssl, int version);
int ssl_security_cert(const SSL_CTX *ctx, const SSL *ssl, X509 *x509,
    int is_peer, int *out_error);
int ssl_security_cert_chain(const SSL *ssl, STACK_OF(X509) *sk,
    X509 *x509, int *out_error);
int ssl_security_shared_group(const SSL *ssl, uint16_t group_id);
int ssl_security_supported_group(const SSL *ssl, uint16_t group_id);

SSL_SESSION *ssl_session_dup(SSL_SESSION *src, int include_ticket);
int ssl_get_new_session(SSL *s, int session);
int ssl_get_prev_session(SSL *s, CBS *session_id, CBS *ext_block,
    int *alert);
int ssl_cipher_list_to_bytes(SSL *s, STACK_OF(SSL_CIPHER) *ciphers, CBB *cbb);
STACK_OF(SSL_CIPHER) *ssl_bytes_to_cipher_list(SSL *s, CBS *cbs);
STACK_OF(SSL_CIPHER) *ssl_create_cipher_list(const SSL_METHOD *meth,
    STACK_OF(SSL_CIPHER) **pref, STACK_OF(SSL_CIPHER) *tls13,
    const char *rule_str, SSL_CERT *cert);
int ssl_parse_ciphersuites(STACK_OF(SSL_CIPHER) **out_ciphers, const char *str);
int ssl_merge_cipherlists(STACK_OF(SSL_CIPHER) *cipherlist,
    STACK_OF(SSL_CIPHER) *cipherlist_tls13,
    STACK_OF(SSL_CIPHER) **out_cipherlist);
void ssl_update_cache(SSL *s, int mode);
int ssl_cipher_get_evp(SSL *s, const EVP_CIPHER **enc,
    const EVP_MD **md, int *mac_pkey_type, int *mac_secret_size);
int ssl_cipher_get_evp_aead(SSL *s, const EVP_AEAD **aead);
int ssl_get_handshake_evp_md(SSL *s, const EVP_MD **md);

int ssl_verify_cert_chain(SSL *s, STACK_OF(X509) *sk);
int ssl_undefined_function(SSL *s);
int ssl_undefined_void_function(void);
int ssl_undefined_const_function(const SSL *s);
SSL_CERT_PKEY *ssl_get_server_send_pkey(const SSL *s);
EVP_PKEY *ssl_get_sign_pkey(SSL *s, const SSL_CIPHER *c, const EVP_MD **pmd,
    const struct ssl_sigalg **sap);
size_t ssl_dhe_params_auto_key_bits(SSL *s);
int ssl_cert_type(EVP_PKEY *pkey);
void ssl_set_cert_masks(SSL_CERT *c, const SSL_CIPHER *cipher);
STACK_OF(SSL_CIPHER) *ssl_get_ciphers_by_id(SSL *s);
int ssl_has_ecc_ciphers(SSL *s);
int ssl_verify_alarm_type(long type);

int SSL_SESSION_ticket(SSL_SESSION *ss, unsigned char **out, size_t *out_len);

int ssl3_do_write(SSL *s, int type);
int ssl3_send_alert(SSL *s, int level, int desc);
int ssl3_get_req_cert_types(SSL *s, CBB *cbb);
int ssl3_get_message(SSL *s, int st1, int stn, int mt, long max);
int ssl3_num_ciphers(void);
const SSL_CIPHER *ssl3_get_cipher_by_index(int idx);
const SSL_CIPHER *ssl3_get_cipher_by_value(uint16_t value);
int ssl3_renegotiate(SSL *ssl);

int ssl3_renegotiate_check(SSL *ssl);

void ssl_force_want_read(SSL *s);

int ssl3_dispatch_alert(SSL *s);
int ssl3_read_alert(SSL *s);
int ssl3_read_change_cipher_spec(SSL *s);
int ssl3_read_bytes(SSL *s, int type, unsigned char *buf, int len, int peek);
int ssl3_write_bytes(SSL *s, int type, const void *buf, int len);
int ssl3_output_cert_chain(SSL *s, CBB *cbb, SSL_CERT_PKEY *cpk);
SSL_CIPHER *ssl3_choose_cipher(SSL *ssl, STACK_OF(SSL_CIPHER) *clnt,
    STACK_OF(SSL_CIPHER) *srvr);
int	ssl3_setup_buffers(SSL *s);
int	ssl3_setup_init_buffer(SSL *s);
void ssl3_release_init_buffer(SSL *s);
int	ssl3_setup_read_buffer(SSL *s);
int	ssl3_setup_write_buffer(SSL *s);
void ssl3_release_buffer(SSL3_BUFFER_INTERNAL *b);
void ssl3_release_read_buffer(SSL *s);
void ssl3_release_write_buffer(SSL *s);
int	ssl3_new(SSL *s);
void	ssl3_free(SSL *s);
int	ssl3_accept(SSL *s);
int	ssl3_connect(SSL *s);
int	ssl3_read(SSL *s, void *buf, int len);
int	ssl3_peek(SSL *s, void *buf, int len);
int	ssl3_write(SSL *s, const void *buf, int len);
int	ssl3_shutdown(SSL *s);
void	ssl3_clear(SSL *s);
long	ssl3_ctrl(SSL *s, int cmd, long larg, void *parg);
long	ssl3_ctx_ctrl(SSL_CTX *s, int cmd, long larg, void *parg);
long	ssl3_callback_ctrl(SSL *s, int cmd, void (*fp)(void));
long	ssl3_ctx_callback_ctrl(SSL_CTX *s, int cmd, void (*fp)(void));
int	ssl3_pending(const SSL *s);

int ssl3_handshake_msg_hdr_len(SSL *s);
int ssl3_handshake_msg_start(SSL *s, CBB *handshake, CBB *body,
    uint8_t msg_type);
int ssl3_handshake_msg_finish(SSL *s, CBB *handshake);
int ssl3_handshake_write(SSL *s);
int ssl3_record_write(SSL *s, int type);

int ssl3_do_change_cipher_spec(SSL *ssl);

int ssl3_packet_read(SSL *s, int plen);
int ssl3_packet_extend(SSL *s, int plen);
int ssl_server_legacy_first_packet(SSL *s);
int ssl3_write_pending(SSL *s, int type, const unsigned char *buf,
    unsigned int len);

int ssl_kex_generate_dhe(DH *dh, DH *dh_params);
int ssl_kex_generate_dhe_params_auto(DH *dh, size_t key_len);
int ssl_kex_params_dhe(DH *dh, CBB *cbb);
int ssl_kex_public_dhe(DH *dh, CBB *cbb);
int ssl_kex_peer_params_dhe(DH *dh, CBS *cbs, int *decode_error,
    int *invalid_params);
int ssl_kex_peer_public_dhe(DH *dh, CBS *cbs, int *decode_error,
    int *invalid_key);
int ssl_kex_derive_dhe(DH *dh, DH *dh_peer,
    uint8_t **shared_key, size_t *shared_key_len);

int ssl_kex_dummy_ecdhe_x25519(EVP_PKEY *pkey);
int ssl_kex_generate_ecdhe_ecp(EC_KEY *ecdh, int nid);
int ssl_kex_public_ecdhe_ecp(EC_KEY *ecdh, CBB *cbb);
int ssl_kex_peer_public_ecdhe_ecp(EC_KEY *ecdh, int nid, CBS *cbs);
int ssl_kex_derive_ecdhe_ecp(EC_KEY *ecdh, EC_KEY *ecdh_peer,
    uint8_t **shared_key, size_t *shared_key_len);

int tls1_new(SSL *s);
void tls1_free(SSL *s);
void tls1_clear(SSL *s);

int ssl_init_wbio_buffer(SSL *s, int push);
void ssl_free_wbio_buffer(SSL *s);

int tls1_transcript_hash_init(SSL *s);
int tls1_transcript_hash_update(SSL *s, const unsigned char *buf, size_t len);
int tls1_transcript_hash_value(SSL *s, unsigned char *out, size_t len,
    size_t *outlen);
void tls1_transcript_hash_free(SSL *s);

int tls1_transcript_init(SSL *s);
void tls1_transcript_free(SSL *s);
void tls1_transcript_reset(SSL *s);
int tls1_transcript_append(SSL *s, const unsigned char *buf, size_t len);
int tls1_transcript_data(SSL *s, const unsigned char **data, size_t *len);
void tls1_transcript_freeze(SSL *s);
void tls1_transcript_unfreeze(SSL *s);
int tls1_transcript_record(SSL *s, const unsigned char *buf, size_t len);

int tls1_PRF(SSL *s, const unsigned char *secret, size_t secret_len,
    const void *seed1, size_t seed1_len, const void *seed2, size_t seed2_len,
    const void *seed3, size_t seed3_len, const void *seed4, size_t seed4_len,
    const void *seed5, size_t seed5_len, unsigned char *out, size_t out_len);

void tls1_cleanup_key_block(SSL *s);
int tls1_change_read_cipher_state(SSL *s);
int tls1_change_write_cipher_state(SSL *s);
int tls1_setup_key_block(SSL *s);
int tls1_generate_key_block(SSL *s, uint8_t *key_block, size_t key_block_len);
int ssl_ok(SSL *s);

int tls12_derive_finished(SSL *s);
int tls12_derive_peer_finished(SSL *s);
int tls12_derive_master_secret(SSL *s, uint8_t *premaster_secret,
    size_t premaster_secret_len);

int ssl_using_ecc_cipher(SSL *s);
int ssl_check_srvr_ecc_cert_and_alg(SSL *s, X509 *x);

void tls1_get_formatlist(const SSL *s, int client_formats,
    const uint8_t **pformats, size_t *pformatslen);
void tls1_get_group_list(const SSL *s, int client_groups,
    const uint16_t **pgroups, size_t *pgroupslen);

int tls1_set_groups(uint16_t **out_group_ids, size_t *out_group_ids_len,
    const int *groups, size_t ngroups);
int tls1_set_group_list(uint16_t **out_group_ids, size_t *out_group_ids_len,
    const char *groups);

int tls1_ec_group_id2nid(uint16_t group_id, int *out_nid);
int tls1_ec_group_id2bits(uint16_t group_id, int *out_bits);
int tls1_ec_nid2group_id(int nid, uint16_t *out_group_id);
int tls1_check_group(SSL *s, uint16_t group_id);
int tls1_count_shared_groups(const SSL *ssl, size_t *out_count);
int tls1_get_shared_group_by_index(const SSL *ssl, size_t index, int *out_nid);
int tls1_get_supported_group(const SSL *s, int *out_nid);

int ssl_check_clienthello_tlsext_early(SSL *s);
int ssl_check_clienthello_tlsext_late(SSL *s);
int ssl_check_serverhello_tlsext(SSL *s);

#define TLS1_TICKET_FATAL_ERROR		-1
#define TLS1_TICKET_NONE		 0
#define TLS1_TICKET_EMPTY		 1
#define TLS1_TICKET_NOT_DECRYPTED	 2
#define TLS1_TICKET_DECRYPTED		 3

int tls1_process_ticket(SSL *s, CBS *ext_block, int *alert, SSL_SESSION **ret);

int tls1_check_ec_server_key(SSL *s);

/* s3_cbc.c */
void ssl3_cbc_copy_mac(unsigned char *out, const SSL3_RECORD_INTERNAL *rec,
    unsigned int md_size, unsigned int orig_len);
int ssl3_cbc_remove_padding(SSL3_RECORD_INTERNAL *rec, unsigned int eiv_len,
    unsigned int mac_size);
char ssl3_cbc_record_digest_supported(const EVP_MD_CTX *ctx);
int ssl3_cbc_digest_record(const EVP_MD_CTX *ctx, unsigned char *md_out,
    size_t *md_out_size, const unsigned char header[13],
    const unsigned char *data, size_t data_plus_mac_size,
    size_t data_plus_mac_plus_padding_size, const unsigned char *mac_secret,
    unsigned int mac_secret_length);
int SSL_state_func_code(int _state);

void SSL_error_internal(const SSL *s, int r, const char *f, int l);
#define SSLerror(s, r)	SSL_error_internal(s, r, OPENSSL_FILE, OPENSSL_LINE)
#define SSLerrorx(r)	ERR_PUT_error(ERR_LIB_SSL,(0xfff),(r),OPENSSL_FILE,OPENSSL_LINE)
#define SYSerror(r)	ERR_PUT_error(ERR_LIB_SYS,(0xfff),(r),OPENSSL_FILE,OPENSSL_LINE)

#ifndef OPENSSL_NO_SRTP

int srtp_find_profile_by_name(const char *profile_name,
    const SRTP_PROTECTION_PROFILE **pptr, unsigned int len);
int srtp_find_profile_by_num(unsigned int profile_num,
    const SRTP_PROTECTION_PROFILE **pptr);

#endif /* OPENSSL_NO_SRTP */

int tls_process_peer_certs(SSL *s, STACK_OF(X509) *peer_certs);

__END_HIDDEN_DECLS

#endif /* !HEADER_SSL_LOCL_H */
