/* $OpenBSD: tls.h,v 1.68 2024/12/10 08:40:30 tb Exp $ */
/*
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

#ifndef HEADER_TLS_H
#define HEADER_TLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#include <stddef.h>
#include <stdint.h>

#define TLS_API	20200120

/*
 * Deprecated versions of TLS. Using these effectively selects
 * the minimum supported version.
 */
#define TLS_PROTOCOL_TLSv1_0	(1 << 1)
#define TLS_PROTOCOL_TLSv1_1	(1 << 2)
/* Supported versions of TLS */
#define TLS_PROTOCOL_TLSv1_2	(1 << 3)
#define TLS_PROTOCOL_TLSv1_3	(1 << 4)

#define TLS_PROTOCOL_TLSv1 \
	(TLS_PROTOCOL_TLSv1_2|TLS_PROTOCOL_TLSv1_3)

#define TLS_PROTOCOLS_ALL TLS_PROTOCOL_TLSv1
#define TLS_PROTOCOLS_DEFAULT (TLS_PROTOCOL_TLSv1_2|TLS_PROTOCOL_TLSv1_3)

#define TLS_WANT_POLLIN		-2
#define TLS_WANT_POLLOUT	-3

/* RFC 6960 Section 2.3 */
#define TLS_OCSP_RESPONSE_SUCCESSFUL		0
#define TLS_OCSP_RESPONSE_MALFORMED		1
#define TLS_OCSP_RESPONSE_INTERNALERROR		2
#define TLS_OCSP_RESPONSE_TRYLATER		3
#define TLS_OCSP_RESPONSE_SIGREQUIRED		4
#define TLS_OCSP_RESPONSE_UNAUTHORIZED		5

/* RFC 6960 Section 2.2 */
#define TLS_OCSP_CERT_GOOD			0
#define TLS_OCSP_CERT_REVOKED			1
#define TLS_OCSP_CERT_UNKNOWN			2

/* RFC 5280 Section 5.3.1 */
#define TLS_CRL_REASON_UNSPECIFIED		0
#define TLS_CRL_REASON_KEY_COMPROMISE		1
#define TLS_CRL_REASON_CA_COMPROMISE		2
#define TLS_CRL_REASON_AFFILIATION_CHANGED	3
#define TLS_CRL_REASON_SUPERSEDED		4
#define TLS_CRL_REASON_CESSATION_OF_OPERATION	5
#define TLS_CRL_REASON_CERTIFICATE_HOLD		6
#define TLS_CRL_REASON_REMOVE_FROM_CRL		8
#define TLS_CRL_REASON_PRIVILEGE_WITHDRAWN	9
#define TLS_CRL_REASON_AA_COMPROMISE		10

#define TLS_MAX_SESSION_ID_LENGTH		32
#define TLS_TICKET_KEY_SIZE			48

/* Error codes */
#if defined(LIBRESSL_NEXT_API) || defined(LIBRESSL_INTERNAL)
#define TLS_ERROR_UNKNOWN			0x0000
#define TLS_ERROR_OUT_OF_MEMORY			0x1000
#define TLS_ERROR_INVALID_CONTEXT		0x2000
#define TLS_ERROR_INVALID_ARGUMENT		0x2001
#endif

struct tls;
struct tls_config;

typedef ssize_t (*tls_read_cb)(struct tls *_ctx, void *_buf, size_t _buflen,
    void *_cb_arg);
typedef ssize_t (*tls_write_cb)(struct tls *_ctx, const void *_buf,
    size_t _buflen, void *_cb_arg);

int tls_init(void);

const char *tls_config_error(struct tls_config *_config);
const char *tls_error(struct tls *_ctx);
#if defined(LIBRESSL_NEXT_API) || defined(LIBRESSL_INTERNAL)
int tls_config_error_code(struct tls_config *_config);
int tls_error_code(struct tls *_ctx);
#endif

struct tls_config *tls_config_new(void);
void tls_config_free(struct tls_config *_config);

const char *tls_default_ca_cert_file(void);

int tls_config_add_keypair_file(struct tls_config *_config,
    const char *_cert_file, const char *_key_file);
int tls_config_add_keypair_mem(struct tls_config *_config, const uint8_t *_cert,
    size_t _cert_len, const uint8_t *_key, size_t _key_len);
int tls_config_add_keypair_ocsp_file(struct tls_config *_config,
    const char *_cert_file, const char *_key_file,
    const char *_ocsp_staple_file);
int tls_config_add_keypair_ocsp_mem(struct tls_config *_config, const uint8_t *_cert,
    size_t _cert_len, const uint8_t *_key, size_t _key_len,
    const uint8_t *_staple, size_t _staple_len);
int tls_config_set_alpn(struct tls_config *_config, const char *_alpn);
int tls_config_set_ca_file(struct tls_config *_config, const char *_ca_file);
int tls_config_set_ca_path(struct tls_config *_config, const char *_ca_path);
int tls_config_set_ca_mem(struct tls_config *_config, const uint8_t *_ca,
    size_t _len);
int tls_config_set_cert_file(struct tls_config *_config,
    const char *_cert_file);
int tls_config_set_cert_mem(struct tls_config *_config, const uint8_t *_cert,
    size_t _len);
int tls_config_set_ciphers(struct tls_config *_config, const char *_ciphers);
int tls_config_set_crl_file(struct tls_config *_config, const char *_crl_file);
int tls_config_set_crl_mem(struct tls_config *_config, const uint8_t *_crl,
    size_t _len);
int tls_config_set_dheparams(struct tls_config *_config, const char *_params);
int tls_config_set_ecdhecurve(struct tls_config *_config, const char *_curve);
int tls_config_set_ecdhecurves(struct tls_config *_config, const char *_curves);
int tls_config_set_key_file(struct tls_config *_config, const char *_key_file);
int tls_config_set_key_mem(struct tls_config *_config, const uint8_t *_key,
    size_t _len);
int tls_config_set_keypair_file(struct tls_config *_config,
    const char *_cert_file, const char *_key_file);
int tls_config_set_keypair_mem(struct tls_config *_config, const uint8_t *_cert,
    size_t _cert_len, const uint8_t *_key, size_t _key_len);
int tls_config_set_keypair_ocsp_file(struct tls_config *_config,
    const char *_cert_file, const char *_key_file, const char *_staple_file);
int tls_config_set_keypair_ocsp_mem(struct tls_config *_config, const uint8_t *_cert,
    size_t _cert_len, const uint8_t *_key, size_t _key_len,
    const uint8_t *_staple, size_t staple_len);
int tls_config_set_ocsp_staple_mem(struct tls_config *_config,
    const uint8_t *_staple, size_t _len);
int tls_config_set_ocsp_staple_file(struct tls_config *_config,
    const char *_staple_file);
int tls_config_set_protocols(struct tls_config *_config, uint32_t _protocols);
int tls_config_set_session_fd(struct tls_config *_config, int _session_fd);
int tls_config_set_verify_depth(struct tls_config *_config, int _verify_depth);

void tls_config_prefer_ciphers_client(struct tls_config *_config);
void tls_config_prefer_ciphers_server(struct tls_config *_config);

void tls_config_insecure_noverifycert(struct tls_config *_config);
void tls_config_insecure_noverifyname(struct tls_config *_config);
void tls_config_insecure_noverifytime(struct tls_config *_config);
void tls_config_verify(struct tls_config *_config);

void tls_config_ocsp_require_stapling(struct tls_config *_config);
void tls_config_verify_client(struct tls_config *_config);
void tls_config_verify_client_optional(struct tls_config *_config);

void tls_config_clear_keys(struct tls_config *_config);
int tls_config_parse_protocols(uint32_t *_protocols, const char *_protostr);

int tls_config_set_session_id(struct tls_config *_config,
    const unsigned char *_session_id, size_t _len);
int tls_config_set_session_lifetime(struct tls_config *_config, int _lifetime);
int tls_config_add_ticket_key(struct tls_config *_config, uint32_t _keyrev,
    unsigned char *_key, size_t _keylen);

struct tls *tls_client(void);
struct tls *tls_server(void);
int tls_configure(struct tls *_ctx, struct tls_config *_config);
void tls_reset(struct tls *_ctx);
void tls_free(struct tls *_ctx);

int tls_accept_fds(struct tls *_ctx, struct tls **_cctx, int _fd_read,
    int _fd_write);
int tls_accept_socket(struct tls *_ctx, struct tls **_cctx, int _socket);
int tls_accept_cbs(struct tls *_ctx, struct tls **_cctx,
    tls_read_cb _read_cb, tls_write_cb _write_cb, void *_cb_arg);
int tls_connect(struct tls *_ctx, const char *_host, const char *_port);
int tls_connect_fds(struct tls *_ctx, int _fd_read, int _fd_write,
    const char *_servername);
int tls_connect_servername(struct tls *_ctx, const char *_host,
    const char *_port, const char *_servername);
int tls_connect_socket(struct tls *_ctx, int _s, const char *_servername);
int tls_connect_cbs(struct tls *_ctx, tls_read_cb _read_cb,
    tls_write_cb _write_cb, void *_cb_arg, const char *_servername);
int tls_handshake(struct tls *_ctx);
ssize_t tls_read(struct tls *_ctx, void *_buf, size_t _buflen);
ssize_t tls_write(struct tls *_ctx, const void *_buf, size_t _buflen);
int tls_close(struct tls *_ctx);

int tls_peer_cert_provided(struct tls *_ctx);
int tls_peer_cert_contains_name(struct tls *_ctx, const char *_name);

const char *tls_peer_cert_common_name(struct tls *_ctx);
const char *tls_peer_cert_hash(struct tls *_ctx);
const char *tls_peer_cert_issuer(struct tls *_ctx);
const char *tls_peer_cert_subject(struct tls *_ctx);
time_t	tls_peer_cert_notbefore(struct tls *_ctx);
time_t	tls_peer_cert_notafter(struct tls *_ctx);
const uint8_t *tls_peer_cert_chain_pem(struct tls *_ctx, size_t *_len);

const char *tls_conn_alpn_selected(struct tls *_ctx);
const char *tls_conn_cipher(struct tls *_ctx);
int tls_conn_cipher_strength(struct tls *_ctx);
const char *tls_conn_servername(struct tls *_ctx);
int tls_conn_session_resumed(struct tls *_ctx);
const char *tls_conn_version(struct tls *_ctx);

uint8_t *tls_load_file(const char *_file, size_t *_len, char *_password);
void tls_unload_file(uint8_t *_buf, size_t len);

int tls_ocsp_process_response(struct tls *_ctx, const unsigned char *_response,
    size_t _size);
int tls_peer_ocsp_cert_status(struct tls *_ctx);
int tls_peer_ocsp_crl_reason(struct tls *_ctx);
time_t tls_peer_ocsp_next_update(struct tls *_ctx);
int tls_peer_ocsp_response_status(struct tls *_ctx);
const char *tls_peer_ocsp_result(struct tls *_ctx);
time_t tls_peer_ocsp_revocation_time(struct tls *_ctx);
time_t tls_peer_ocsp_this_update(struct tls *_ctx);
const char *tls_peer_ocsp_url(struct tls *_ctx);

#ifdef __cplusplus
}
#endif

#endif /* HEADER_TLS_H */
