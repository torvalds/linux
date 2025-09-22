/* $OpenBSD: tls13_internal.h,v 1.105 2025/03/09 15:12:18 tb Exp $ */
/*
 * Copyright (c) 2018 Bob Beck <beck@openbsd.org>
 * Copyright (c) 2018 Theo Buehler <tb@openbsd.org>
 * Copyright (c) 2018, 2019 Joel Sing <jsing@openbsd.org>
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

#ifndef HEADER_TLS13_INTERNAL_H
#define HEADER_TLS13_INTERNAL_H

#include <openssl/evp.h>
#include <openssl/ssl.h>

#include "bytestring.h"
#include "tls_internal.h"

__BEGIN_HIDDEN_DECLS

#define TLS13_HS_CLIENT			1
#define TLS13_HS_SERVER			2

#define TLS13_IO_SUCCESS		 1
#define TLS13_IO_EOF			 0
#define TLS13_IO_FAILURE		-1
#define TLS13_IO_ALERT			-2
#define TLS13_IO_WANT_POLLIN		-3
#define TLS13_IO_WANT_POLLOUT		-4
#define TLS13_IO_WANT_RETRY		-5 /* Retry the previous call immediately. */
#define TLS13_IO_USE_LEGACY		-6
#define TLS13_IO_RECORD_VERSION		-7
#define TLS13_IO_RECORD_OVERFLOW	-8

#define TLS13_ERR_VERIFY_FAILED		16
#define TLS13_ERR_HRR_FAILED		17
#define TLS13_ERR_TRAILING_DATA		18
#define TLS13_ERR_NO_SHARED_CIPHER	19
#define TLS13_ERR_NO_CERTIFICATE	20
#define TLS13_ERR_NO_PEER_CERTIFICATE	21

#define TLS13_ALERT_LEVEL_WARNING			1
#define TLS13_ALERT_LEVEL_FATAL				2

#define TLS13_ALERT_CLOSE_NOTIFY			0
#define TLS13_ALERT_UNEXPECTED_MESSAGE			10
#define TLS13_ALERT_BAD_RECORD_MAC			20
#define TLS13_ALERT_RECORD_OVERFLOW			22
#define TLS13_ALERT_HANDSHAKE_FAILURE			40
#define TLS13_ALERT_BAD_CERTIFICATE			42
#define TLS13_ALERT_UNSUPPORTED_CERTIFICATE		43
#define TLS13_ALERT_CERTIFICATE_REVOKED			44
#define TLS13_ALERT_CERTIFICATE_EXPIRED			45
#define TLS13_ALERT_CERTIFICATE_UNKNOWN			46
#define TLS13_ALERT_ILLEGAL_PARAMETER			47
#define TLS13_ALERT_UNKNOWN_CA				48
#define TLS13_ALERT_ACCESS_DENIED			49
#define TLS13_ALERT_DECODE_ERROR			50
#define TLS13_ALERT_DECRYPT_ERROR			51
#define TLS13_ALERT_PROTOCOL_VERSION			70
#define TLS13_ALERT_INSUFFICIENT_SECURITY		71
#define TLS13_ALERT_INTERNAL_ERROR			80
#define TLS13_ALERT_INAPPROPRIATE_FALLBACK		86
#define TLS13_ALERT_USER_CANCELED			90
#define TLS13_ALERT_MISSING_EXTENSION			109
#define TLS13_ALERT_UNSUPPORTED_EXTENSION		110
#define TLS13_ALERT_UNRECOGNIZED_NAME			112
#define TLS13_ALERT_BAD_CERTIFICATE_STATUS_RESPONSE	113
#define TLS13_ALERT_UNKNOWN_PSK_IDENTITY		115
#define TLS13_ALERT_CERTIFICATE_REQUIRED		116
#define TLS13_ALERT_NO_APPLICATION_PROTOCOL		120

#define TLS13_INFO_HANDSHAKE_STARTED			SSL_CB_HANDSHAKE_START
#define TLS13_INFO_HANDSHAKE_COMPLETED			SSL_CB_HANDSHAKE_DONE
#define TLS13_INFO_ACCEPT_LOOP				SSL_CB_ACCEPT_LOOP
#define TLS13_INFO_CONNECT_LOOP				SSL_CB_CONNECT_LOOP
#define TLS13_INFO_ACCEPT_EXIT				SSL_CB_ACCEPT_EXIT
#define TLS13_INFO_CONNECT_EXIT				SSL_CB_CONNECT_EXIT

typedef void (*tls13_alert_cb)(uint8_t _alert_level, uint8_t _alert_desc,
    void *_cb_arg);
typedef ssize_t (*tls13_phh_recv_cb)(void *_cb_arg);
typedef void (*tls13_phh_sent_cb)(void *_cb_arg);
typedef void (*tls13_handshake_message_cb)(void *_cb_arg);
typedef void (*tls13_info_cb)(void *_cb_arg, int _state, int _ret);
typedef int (*tls13_ocsp_status_cb)(void *_cb_arg);

/*
 * PSK support.
 */

/*
 * Known PskKeyExchangeMode values.
 * https://www.iana.org/assignments/tls-parameters/#tls-pskkeyexchangemode
 */
#define TLS13_PSK_KE					0
#define TLS13_PSK_DHE_KE				1

/*
 * Secrets.
 */
struct tls13_secret {
	uint8_t *data;
	size_t len;
};

/* RFC 8446 Section 7.1  Page 92 */
struct tls13_secrets {
	const EVP_MD *digest;
	int resumption;
	int init_done;
	int early_done;
	int handshake_done;
	int schedule_done;
	int insecure; /* Set by tests */
	struct tls13_secret zeros;
	struct tls13_secret empty_hash;
	struct tls13_secret extracted_early;
	struct tls13_secret binder_key;
	struct tls13_secret client_early_traffic;
	struct tls13_secret early_exporter_master;
	struct tls13_secret derived_early;
	struct tls13_secret extracted_handshake;
	struct tls13_secret client_handshake_traffic;
	struct tls13_secret server_handshake_traffic;
	struct tls13_secret derived_handshake;
	struct tls13_secret extracted_master;
	struct tls13_secret client_application_traffic;
	struct tls13_secret server_application_traffic;
	struct tls13_secret exporter_master;
	struct tls13_secret resumption_master;
};

int tls13_secret_init(struct tls13_secret *secret, size_t len);
void tls13_secret_cleanup(struct tls13_secret *secret);
struct tls13_secrets *tls13_secrets_create(const EVP_MD *digest,
    int resumption);
void tls13_secrets_destroy(struct tls13_secrets *secrets);

int tls13_hkdf_expand_label(struct tls13_secret *out, const EVP_MD *digest,
    const struct tls13_secret *secret, const char *label,
    const struct tls13_secret *context);
int tls13_hkdf_expand_label_with_length(struct tls13_secret *out,
    const EVP_MD *digest, const struct tls13_secret *secret,
    const uint8_t *label, size_t label_len, const struct tls13_secret *context);

int tls13_derive_secret(struct tls13_secret *out, const EVP_MD *digest,
    const struct tls13_secret *secret, const char *label,
    const struct tls13_secret *context);
int tls13_derive_secret_with_label_length(struct tls13_secret *out,
    const EVP_MD *digest, const struct tls13_secret *secret,
    const uint8_t *label, size_t label_len, const struct tls13_secret *context);

int tls13_derive_early_secrets(struct tls13_secrets *secrets, uint8_t *psk,
    size_t psk_len, const struct tls13_secret *context);
int tls13_derive_handshake_secrets(struct tls13_secrets *secrets,
    const uint8_t *ecdhe, size_t ecdhe_len, const struct tls13_secret *context);
int tls13_derive_application_secrets(struct tls13_secrets *secrets,
    const struct tls13_secret *context);
int tls13_update_client_traffic_secret(struct tls13_secrets *secrets);
int tls13_update_server_traffic_secret(struct tls13_secrets *secrets);

/*
 * Record Layer.
 */
struct tls13_record_layer;

struct tls13_record_layer_callbacks {
	/* Wire callbacks. */
	tls_read_cb wire_read;
	tls_write_cb wire_write;
	tls_flush_cb wire_flush;

	/* Interceptors. */
	tls_handshake_read_cb handshake_read;
	tls_handshake_write_cb handshake_write;
	tls_traffic_key_cb set_read_traffic_key;
	tls_traffic_key_cb set_write_traffic_key;
	tls_alert_send_cb alert_send;

	/* Notification callbacks. */
	tls13_alert_cb alert_recv;
	tls13_alert_cb alert_sent;
	tls13_phh_recv_cb phh_recv;
	tls13_phh_sent_cb phh_sent;
};

struct tls13_record_layer *tls13_record_layer_new(
    const struct tls13_record_layer_callbacks *callbacks, void *cb_arg);
void tls13_record_layer_free(struct tls13_record_layer *rl);
void tls13_record_layer_set_callbacks(struct tls13_record_layer *rl,
    const struct tls13_record_layer_callbacks *callbacks, void *cb_arg);
void tls13_record_layer_allow_ccs(struct tls13_record_layer *rl, int allow);
void tls13_record_layer_allow_legacy_alerts(struct tls13_record_layer *rl, int allow);
void tls13_record_layer_rcontent(struct tls13_record_layer *rl, CBS *cbs);
void tls13_record_layer_set_aead(struct tls13_record_layer *rl,
    const EVP_AEAD *aead);
void tls13_record_layer_set_hash(struct tls13_record_layer *rl,
    const EVP_MD *hash);
void tls13_record_layer_set_legacy_version(struct tls13_record_layer *rl,
    uint16_t version);
void tls13_record_layer_set_retry_after_phh(struct tls13_record_layer *rl, int retry);
void tls13_record_layer_alert_sent(struct tls13_record_layer *rl,
    uint8_t alert_level, uint8_t alert_desc);
void tls13_record_layer_handshake_completed(struct tls13_record_layer *rl);
int tls13_record_layer_set_read_traffic_key(struct tls13_record_layer *rl,
    struct tls13_secret *read_key, enum ssl_encryption_level_t read_level);
int tls13_record_layer_set_write_traffic_key(struct tls13_record_layer *rl,
    struct tls13_secret *write_key, enum ssl_encryption_level_t write_level);
ssize_t tls13_record_layer_send_pending(struct tls13_record_layer *rl);
ssize_t tls13_record_layer_phh(struct tls13_record_layer *rl, CBS *cbs);
ssize_t tls13_record_layer_flush(struct tls13_record_layer *rl);

ssize_t tls13_read_handshake_data(struct tls13_record_layer *rl, uint8_t *buf, size_t n);
ssize_t tls13_write_handshake_data(struct tls13_record_layer *rl, const uint8_t *buf,
    size_t n);
ssize_t tls13_pending_application_data(struct tls13_record_layer *rl);
ssize_t tls13_peek_application_data(struct tls13_record_layer *rl, uint8_t *buf, size_t n);
ssize_t tls13_read_application_data(struct tls13_record_layer *rl, uint8_t *buf, size_t n);
ssize_t tls13_write_application_data(struct tls13_record_layer *rl, const uint8_t *buf,
    size_t n);

ssize_t tls13_send_alert(struct tls13_record_layer *rl, uint8_t alert_desc);
ssize_t tls13_send_dummy_ccs(struct tls13_record_layer *rl);

/*
 * Handshake Messages.
 */
struct tls13_handshake_msg;

struct tls13_handshake_msg *tls13_handshake_msg_new(void);
void tls13_handshake_msg_free(struct tls13_handshake_msg *msg);
void tls13_handshake_msg_data(struct tls13_handshake_msg *msg, CBS *cbs);
uint8_t tls13_handshake_msg_type(struct tls13_handshake_msg *msg);
int tls13_handshake_msg_content(struct tls13_handshake_msg *msg, CBS *cbs);
int tls13_handshake_msg_start(struct tls13_handshake_msg *msg, CBB *body,
    uint8_t msg_type);
int tls13_handshake_msg_finish(struct tls13_handshake_msg *msg);
int tls13_handshake_msg_recv(struct tls13_handshake_msg *msg,
    struct tls13_record_layer *rl);
int tls13_handshake_msg_send(struct tls13_handshake_msg *msg,
    struct tls13_record_layer *rl);

struct tls13_handshake_stage {
	uint8_t	hs_type;
	uint8_t	message_number;
};

struct ssl_handshake_tls13_st;

struct tls13_error {
	int code;
	int subcode;
	int errnum;
	const char *file;
	int line;
	char *msg;
};

struct tls13_ctx {
	struct tls13_error error;

	SSL *ssl;
	struct ssl_handshake_st *hs;
	uint8_t	mode;
	struct tls13_handshake_stage handshake_stage;
	int handshake_started;
	int handshake_completed;
	int need_flush;
	int middlebox_compat;
	int send_dummy_ccs;
	int send_dummy_ccs_after;

	int close_notify_sent;
	int close_notify_recv;

	const EVP_AEAD *aead;
	const EVP_MD *hash;

	struct tls13_record_layer *rl;
	struct tls13_handshake_msg *hs_msg;
	uint8_t key_update_request;
	uint8_t alert;
	int phh_count;
	time_t phh_last_seen;

	tls13_alert_cb alert_sent_cb;
	tls13_alert_cb alert_recv_cb;
	tls13_handshake_message_cb handshake_message_sent_cb;
	tls13_handshake_message_cb handshake_message_recv_cb;
	tls13_info_cb info_cb;
	tls13_ocsp_status_cb ocsp_status_recv_cb;
};
#ifndef TLS13_PHH_LIMIT_TIME
#define TLS13_PHH_LIMIT_TIME 3600
#endif
#ifndef TLS13_PHH_LIMIT
#define TLS13_PHH_LIMIT 100
#endif

struct tls13_ctx *tls13_ctx_new(int mode, SSL *ssl);
void tls13_ctx_free(struct tls13_ctx *ctx);

const EVP_AEAD *tls13_cipher_aead(const SSL_CIPHER *cipher);
const EVP_MD *tls13_cipher_hash(const SSL_CIPHER *cipher);

void tls13_alert_received_cb(uint8_t alert_level, uint8_t alert_desc, void *arg);
void tls13_alert_sent_cb(uint8_t alert_level, uint8_t alert_desc, void *arg);
ssize_t tls13_phh_received_cb(void *cb_arg);
void tls13_phh_done_cb(void *cb_arg);

int tls13_quic_init(struct tls13_ctx *ctx);

/*
 * Legacy interfaces.
 */
int tls13_use_legacy_client(struct tls13_ctx *ctx);
int tls13_use_legacy_server(struct tls13_ctx *ctx);
int tls13_legacy_accept(SSL *ssl);
int tls13_legacy_connect(SSL *ssl);
ssize_t tls13_legacy_wire_read_cb(void *buf, size_t n, void *arg);
ssize_t tls13_legacy_wire_write_cb(const void *buf, size_t n, void *arg);
ssize_t tls13_legacy_wire_flush_cb(void *arg);
int tls13_legacy_pending(const SSL *ssl);
int tls13_legacy_read_bytes(SSL *ssl, int type, unsigned char *buf, int len,
    int peek);
int tls13_legacy_write_bytes(SSL *ssl, int type, const void *buf, int len);
int tls13_legacy_shutdown(SSL *ssl);
int tls13_legacy_servername_process(struct tls13_ctx *ctx, uint8_t *alert);

/*
 * Message Types - RFC 8446, Section B.3.
 *
 * Values listed as "_RESERVED" were used in previous versions of TLS and are
 * listed here for completeness.  TLS 1.3 implementations MUST NOT send them but
 * might receive them from older TLS implementations.
 */
#define	TLS13_MT_HELLO_REQUEST_RESERVED		0
#define	TLS13_MT_CLIENT_HELLO			1
#define	TLS13_MT_SERVER_HELLO			2
#define	TLS13_MT_HELLO_VERIFY_REQUEST_RESERVED	3
#define	TLS13_MT_NEW_SESSION_TICKET		4
#define	TLS13_MT_END_OF_EARLY_DATA		5
#define	TLS13_MT_HELLO_RETRY_REQUEST_RESERVED	6
#define	TLS13_MT_ENCRYPTED_EXTENSIONS		8
#define	TLS13_MT_CERTIFICATE			11
#define	TLS13_MT_SERVER_KEY_EXCHANGE_RESERVED	12
#define	TLS13_MT_CERTIFICATE_REQUEST		13
#define	TLS13_MT_SERVER_HELLO_DONE_RESERVED	14
#define	TLS13_MT_CERTIFICATE_VERIFY		15
#define	TLS13_MT_CLIENT_KEY_EXCHANGE_RESERVED	16
#define	TLS13_MT_FINISHED			20
#define	TLS13_MT_CERTIFICATE_URL_RESERVED	21
#define	TLS13_MT_CERTIFICATE_STATUS_RESERVED	22
#define	TLS13_MT_SUPPLEMENTAL_DATA_RESERVED	23
#define	TLS13_MT_KEY_UPDATE			24
#define	TLS13_MT_MESSAGE_HASH			254

int tls13_handshake_msg_record(struct tls13_ctx *ctx);
int tls13_handshake_perform(struct tls13_ctx *ctx);

int tls13_client_init(struct tls13_ctx *ctx);
int tls13_server_init(struct tls13_ctx *ctx);
int tls13_client_connect(struct tls13_ctx *ctx);
int tls13_server_accept(struct tls13_ctx *ctx);

int tls13_client_hello_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_client_hello_sent(struct tls13_ctx *ctx);
int tls13_client_hello_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_client_hello_retry_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_client_hello_retry_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_client_end_of_early_data_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_client_end_of_early_data_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_client_certificate_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_client_certificate_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_client_certificate_verify_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_client_certificate_verify_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_client_finished_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_client_finished_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_client_finished_sent(struct tls13_ctx *ctx);
int tls13_server_hello_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_server_hello_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_server_hello_sent(struct tls13_ctx *ctx);
int tls13_server_hello_retry_request_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_server_hello_retry_request_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_server_hello_retry_request_sent(struct tls13_ctx *ctx);
int tls13_server_encrypted_extensions_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_server_encrypted_extensions_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_server_certificate_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_server_certificate_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_server_certificate_request_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_server_certificate_request_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_server_certificate_verify_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_server_certificate_verify_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_server_finished_recv(struct tls13_ctx *ctx, CBS *cbs);
int tls13_server_finished_send(struct tls13_ctx *ctx, CBB *cbb);
int tls13_server_finished_sent(struct tls13_ctx *ctx);

void tls13_error_clear(struct tls13_error *error);
int tls13_cert_add(struct tls13_ctx *ctx, CBB *cbb, X509 *cert,
    int(*build_extensions)(SSL *s, uint16_t msg_type, CBB *cbb));

int tls13_synthetic_handshake_message(struct tls13_ctx *ctx);
int tls13_clienthello_hash_init(struct tls13_ctx *ctx);
void tls13_clienthello_hash_clear(struct ssl_handshake_tls13_st *hs);
int tls13_clienthello_hash_update_bytes(struct tls13_ctx *ctx, void *data,
    size_t len);
int tls13_clienthello_hash_update(struct tls13_ctx *ctx, CBS *cbs);
int tls13_clienthello_hash_finalize(struct tls13_ctx *ctx);
int tls13_clienthello_hash_validate(struct tls13_ctx *ctx);

int tls13_error_set(struct tls13_error *error, int code, int subcode,
    const char *file, int line, const char *fmt, ...);
int tls13_error_setx(struct tls13_error *error, int code, int subcode,
    const char *file, int line, const char *fmt, ...);

#define tls13_set_error(ctx, code, subcode, fmt, ...) \
	tls13_error_set(&(ctx)->error, (code), (subcode), OPENSSL_FILE, OPENSSL_LINE, \
	    (fmt), __VA_ARGS__)
#define tls13_set_errorx(ctx, code, subcode, fmt, ...) \
	tls13_error_setx(&(ctx)->error, (code), (subcode), OPENSSL_FILE, OPENSSL_LINE, \
	    (fmt), __VA_ARGS__)

int tls13_exporter(struct tls13_ctx *ctx, const uint8_t *label, size_t label_len,
    const uint8_t *context_value, size_t context_value_len, uint8_t *out,
    size_t out_len);

extern const uint8_t tls13_downgrade_12[8];
extern const uint8_t tls13_downgrade_11[8];
extern const uint8_t tls13_hello_retry_request_hash[32];
extern const uint8_t tls13_cert_verify_pad[64];
extern const uint8_t tls13_cert_client_verify_context[];
extern const uint8_t tls13_cert_server_verify_context[];

__END_HIDDEN_DECLS

#endif
