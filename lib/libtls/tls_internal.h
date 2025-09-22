/* $OpenBSD: tls_internal.h,v 1.86 2024/12/10 08:40:30 tb Exp $ */
/*
 * Copyright (c) 2014 Jeremie Courreges-Anglas <jca@openbsd.org>
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
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

#ifndef HEADER_TLS_INTERNAL_H
#define HEADER_TLS_INTERNAL_H

#include <pthread.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <openssl/ssl.h>

__BEGIN_HIDDEN_DECLS

#ifndef TLS_DEFAULT_CA_FILE
#define TLS_DEFAULT_CA_FILE	"/etc/ssl/cert.pem"
#endif

#define TLS_CIPHERS_DEFAULT	"TLSv1.3:TLSv1.2+AEAD+ECDHE:TLSv1.2+AEAD+DHE"
#define TLS_CIPHERS_COMPAT	"HIGH:!aNULL"
#define TLS_CIPHERS_LEGACY	"HIGH:MEDIUM:!aNULL"
#define TLS_CIPHERS_ALL		"ALL:!aNULL:!eNULL"

#define TLS_ECDHE_CURVES	"X25519,P-256,P-384"

union tls_addr {
	struct in_addr ip4;
	struct in6_addr ip6;
};

struct tls_error {
	char *msg;
	int code;
	int errno_value;
	int tls;
};

struct tls_keypair {
	struct tls_keypair *next;

	char *cert_mem;
	size_t cert_len;
	char *key_mem;
	size_t key_len;
	char *ocsp_staple;
	size_t ocsp_staple_len;
	char *pubkey_hash;
};

#define TLS_MIN_SESSION_TIMEOUT (4)
#define TLS_MAX_SESSION_TIMEOUT (24 * 60 * 60)

#define TLS_NUM_TICKETS				4
#define TLS_TICKET_NAME_SIZE			16
#define TLS_TICKET_AES_SIZE			32
#define TLS_TICKET_HMAC_SIZE			16

struct tls_ticket_key {
	/* The key_name must be 16 bytes according to -lssl */
	unsigned char	key_name[TLS_TICKET_NAME_SIZE];
	unsigned char	aes_key[TLS_TICKET_AES_SIZE];
	unsigned char	hmac_key[TLS_TICKET_HMAC_SIZE];
	time_t		time;
};

typedef int (*tls_sign_cb)(void *_cb_arg, const char *_pubkey_hash,
    const uint8_t *_input, size_t _input_len, int _padding_type,
    uint8_t **_out_signature, size_t *_out_signature_len);

struct tls_config {
	struct tls_error error;

	pthread_mutex_t mutex;
	int refcount;

	char *alpn;
	size_t alpn_len;
	const char *ca_path;
	char *ca_mem;
	size_t ca_len;
	const char *ciphers;
	int ciphers_server;
	char *crl_mem;
	size_t crl_len;
	int dheparams;
	int *ecdhecurves;
	size_t ecdhecurves_len;
	struct tls_keypair *keypair;
	int ocsp_require_stapling;
	uint32_t protocols;
	unsigned char session_id[TLS_MAX_SESSION_ID_LENGTH];
	int session_fd;
	int session_lifetime;
	struct tls_ticket_key ticket_keys[TLS_NUM_TICKETS];
	uint32_t ticket_keyrev;
	int ticket_autorekey;
	int verify_cert;
	int verify_client;
	int verify_depth;
	int verify_name;
	int verify_time;
	int skip_private_key_check;
	int use_fake_private_key;
	tls_sign_cb sign_cb;
	void *sign_cb_arg;
};

struct tls_conninfo {
	char *alpn;
	char *cipher;
	int cipher_strength;
	char *servername;
	int session_resumed;
	char *version;

	char *common_name;
	char *hash;
	char *issuer;
	char *subject;

	uint8_t *peer_cert;
	size_t peer_cert_len;

	time_t notbefore;
	time_t notafter;
};

#define TLS_CLIENT		(1 << 0)
#define TLS_SERVER		(1 << 1)
#define TLS_SERVER_CONN		(1 << 2)

#define TLS_EOF_NO_CLOSE_NOTIFY	(1 << 0)
#define TLS_CONNECTED		(1 << 1)
#define TLS_HANDSHAKE_COMPLETE	(1 << 2)
#define TLS_SSL_NEEDS_SHUTDOWN	(1 << 3)

struct tls_ocsp_result {
	const char *result_msg;
	int response_status;
	int cert_status;
	int crl_reason;
	time_t this_update;
	time_t next_update;
	time_t revocation_time;
};

struct tls_ocsp {
	/* responder location */
	char *ocsp_url;

	/* cert data, this struct does not own these */
	X509 *main_cert;
	STACK_OF(X509) *extra_certs;

	struct tls_ocsp_result *ocsp_result;
};

struct tls_sni_ctx {
	struct tls_sni_ctx *next;

	struct tls_keypair *keypair;

	SSL_CTX *ssl_ctx;
	X509 *ssl_cert;
};

struct tls {
	struct tls_config *config;
	struct tls_keypair *keypair;

	struct tls_error error;

	uint32_t flags;
	uint32_t state;

	char *servername;
	int socket;

	SSL *ssl_conn;
	SSL_CTX *ssl_ctx;

	struct tls_sni_ctx *sni_ctx;

	X509 *ssl_peer_cert;
	STACK_OF(X509) *ssl_peer_chain;

	struct tls_conninfo *conninfo;

	struct tls_ocsp *ocsp;

	tls_read_cb read_cb;
	tls_write_cb write_cb;
	void *cb_arg;
};

int tls_set_mem(char **_dest, size_t *_destlen, const void *_src,
    size_t _srclen);
int tls_set_string(const char **_dest, const char *_src);

struct tls_keypair *tls_keypair_new(void);
void tls_keypair_clear_key(struct tls_keypair *_keypair);
void tls_keypair_free(struct tls_keypair *_keypair);
int tls_keypair_set_cert_file(struct tls_keypair *_keypair,
    struct tls_error *_error, const char *_cert_file);
int tls_keypair_set_cert_mem(struct tls_keypair *_keypair,
    struct tls_error *_error, const uint8_t *_cert, size_t _len);
int tls_keypair_set_key_file(struct tls_keypair *_keypair,
    struct tls_error *_error, const char *_key_file);
int tls_keypair_set_key_mem(struct tls_keypair *_keypair,
    struct tls_error *_error, const uint8_t *_key, size_t _len);
int tls_keypair_set_ocsp_staple_file(struct tls_keypair *_keypair,
    struct tls_error *_error, const char *_ocsp_file);
int tls_keypair_set_ocsp_staple_mem(struct tls_keypair *_keypair,
    struct tls_error *_error, const uint8_t *_staple, size_t _len);
int tls_keypair_load_cert(struct tls_keypair *_keypair,
    struct tls_error *_error, X509 **_cert);

struct tls_sni_ctx *tls_sni_ctx_new(void);
void tls_sni_ctx_free(struct tls_sni_ctx *sni_ctx);

struct tls_config *tls_config_new_internal(void);

struct tls *tls_new(void);
struct tls *tls_server_conn(struct tls *ctx);

int tls_get_common_name(struct tls *_ctx, X509 *_cert, const char *_in_name,
    char **_out_common_name);
int tls_check_name(struct tls *ctx, X509 *cert, const char *servername,
    int *match);
int tls_configure_server(struct tls *ctx);

int tls_configure_ssl(struct tls *ctx, SSL_CTX *ssl_ctx);
int tls_configure_ssl_keypair(struct tls *ctx, SSL_CTX *ssl_ctx,
    struct tls_keypair *keypair, int required);
int tls_configure_ssl_verify(struct tls *ctx, SSL_CTX *ssl_ctx, int verify);

int tls_handshake_client(struct tls *ctx);
int tls_handshake_server(struct tls *ctx);

int tls_config_load_file(struct tls_error *error, const char *filetype,
    const char *filename, char **buf, size_t *len);
int tls_config_ticket_autorekey(struct tls_config *config);
int tls_host_port(const char *hostport, char **host, char **port);

int tls_set_cbs(struct tls *ctx,
    tls_read_cb read_cb, tls_write_cb write_cb, void *cb_arg);

void tls_error_clear(struct tls_error *error);
int tls_error_set(struct tls_error *error, int code, const char *fmt, ...)
    __attribute__((__format__ (printf, 3, 4)))
    __attribute__((__nonnull__ (3)));
int tls_error_setx(struct tls_error *error, int code, const char *fmt, ...)
    __attribute__((__format__ (printf, 3, 4)))
    __attribute__((__nonnull__ (3)));
int tls_config_set_error(struct tls_config *cfg, int code, const char *fmt, ...)
    __attribute__((__format__ (printf, 3, 4)))
    __attribute__((__nonnull__ (3)));
int tls_config_set_errorx(struct tls_config *cfg, int code, const char *fmt, ...)
    __attribute__((__format__ (printf, 3, 4)))
    __attribute__((__nonnull__ (3)));
int tls_set_error(struct tls *ctx, int code, const char *fmt, ...)
    __attribute__((__format__ (printf, 3, 4)))
    __attribute__((__nonnull__ (3)));
int tls_set_errorx(struct tls *ctx, int code, const char *fmt, ...)
    __attribute__((__format__ (printf, 3, 4)))
    __attribute__((__nonnull__ (3)));
int tls_set_ssl_errorx(struct tls *ctx, int code, const char *fmt, ...)
    __attribute__((__format__ (printf, 3, 4)))
    __attribute__((__nonnull__ (3)));

int tls_ssl_error(struct tls *ctx, SSL *ssl_conn, int ssl_ret,
    const char *prefix);

int tls_conninfo_populate(struct tls *ctx);
void tls_conninfo_free(struct tls_conninfo *conninfo);

int tls_ocsp_verify_cb(SSL *ssl, void *arg);
int tls_ocsp_stapling_cb(SSL *ssl, void *arg);
void tls_ocsp_free(struct tls_ocsp *ctx);
struct tls_ocsp *tls_ocsp_setup_from_peer(struct tls *ctx);
int tls_hex_string(const unsigned char *_in, size_t _inlen, char **_out,
    size_t *_outlen);
int tls_cert_hash(X509 *_cert, char **_hash);
int tls_cert_pubkey_hash(X509 *_cert, char **_hash);

int tls_password_cb(char *_buf, int _size, int _rwflag, void *_u);

RSA_METHOD *tls_signer_rsa_method(void);
EC_KEY_METHOD *tls_signer_ecdsa_method(void);

#define TLS_PADDING_NONE			0
#define TLS_PADDING_RSA_PKCS1			1

int tls_config_set_sign_cb(struct tls_config *_config, tls_sign_cb _cb,
    void *_cb_arg);

struct tls_signer* tls_signer_new(void);
void tls_signer_free(struct tls_signer * _signer);
const char *tls_signer_error(struct tls_signer * _signer);
int tls_signer_add_keypair_file(struct tls_signer *_signer,
    const char *_cert_file, const char *_key_file);
int tls_signer_add_keypair_mem(struct tls_signer *_signer, const uint8_t *_cert,
    size_t _cert_len, const uint8_t *_key, size_t _key_len);
int tls_signer_sign(struct tls_signer *_signer, const char *_pubkey_hash,
    const uint8_t *_input, size_t _input_len, int _padding_type,
    uint8_t **_out_signature, size_t *_out_signature_len);

__END_HIDDEN_DECLS

/* XXX this function is not fully hidden so relayd can use it */
void tls_config_skip_private_key_check(struct tls_config *config);
void tls_config_use_fake_private_key(struct tls_config *config);

#endif /* HEADER_TLS_INTERNAL_H */
