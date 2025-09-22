/* $OpenBSD: tls_internal.h,v 1.10 2022/11/10 18:06:37 jsing Exp $ */
/*
 * Copyright (c) 2018, 2019, 2021 Joel Sing <jsing@openbsd.org>
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

#include <openssl/dh.h>
#include <openssl/evp.h>

#include "bytestring.h"

__BEGIN_HIDDEN_DECLS

#define TLS_IO_SUCCESS			 1
#define TLS_IO_EOF			 0
#define TLS_IO_FAILURE			-1
#define TLS_IO_ALERT			-2
#define TLS_IO_WANT_POLLIN		-3
#define TLS_IO_WANT_POLLOUT		-4
#define TLS_IO_WANT_RETRY		-5 /* Retry the previous call immediately. */

enum ssl_encryption_level_t;

struct tls13_secret;

/*
 * Callbacks.
 */
typedef ssize_t (*tls_read_cb)(void *_buf, size_t _buflen, void *_cb_arg);
typedef ssize_t (*tls_write_cb)(const void *_buf, size_t _buflen,
    void *_cb_arg);
typedef ssize_t (*tls_flush_cb)(void *_cb_arg);

typedef ssize_t (*tls_handshake_read_cb)(void *_buf, size_t _buflen,
    void *_cb_arg);
typedef ssize_t (*tls_handshake_write_cb)(const void *_buf, size_t _buflen,
    void *_cb_arg);
typedef int (*tls_traffic_key_cb)(struct tls13_secret *key,
    enum ssl_encryption_level_t level, void *_cb_arg);
typedef int (*tls_alert_send_cb)(int _alert_desc, void *_cb_arg);

/*
 * Buffers.
 */
struct tls_buffer;

struct tls_buffer *tls_buffer_new(size_t init_size);
void tls_buffer_clear(struct tls_buffer *buf);
void tls_buffer_free(struct tls_buffer *buf);
void tls_buffer_set_capacity_limit(struct tls_buffer *buf, size_t limit);
ssize_t tls_buffer_extend(struct tls_buffer *buf, size_t len,
    tls_read_cb read_cb, void *cb_arg);
size_t tls_buffer_remaining(struct tls_buffer *buf);
ssize_t tls_buffer_read(struct tls_buffer *buf, uint8_t *rbuf, size_t n);
ssize_t tls_buffer_write(struct tls_buffer *buf, const uint8_t *wbuf, size_t n);
int tls_buffer_append(struct tls_buffer *buf, const uint8_t *wbuf, size_t n);
int tls_buffer_data(struct tls_buffer *buf, CBS *cbs);
int tls_buffer_finish(struct tls_buffer *buf, uint8_t **out, size_t *out_len);

/*
 * Key shares.
 */
struct tls_key_share;

struct tls_key_share *tls_key_share_new(uint16_t group_id);
struct tls_key_share *tls_key_share_new_nid(int nid);
void tls_key_share_free(struct tls_key_share *ks);

uint16_t tls_key_share_group(struct tls_key_share *ks);
int tls_key_share_nid(struct tls_key_share *ks);
void tls_key_share_set_key_bits(struct tls_key_share *ks, size_t key_bits);
int tls_key_share_set_dh_params(struct tls_key_share *ks, DH *dh_params);
int tls_key_share_peer_pkey(struct tls_key_share *ks, EVP_PKEY *pkey);
int tls_key_share_generate(struct tls_key_share *ks);
int tls_key_share_params(struct tls_key_share *ks, CBB *cbb);
int tls_key_share_public(struct tls_key_share *ks, CBB *cbb);
int tls_key_share_peer_params(struct tls_key_share *ks, CBS *cbs,
    int *decode_error, int *invalid_params);
int tls_key_share_peer_public(struct tls_key_share *ks, CBS *cbs,
    int *decode_error, int *invalid_key);
int tls_key_share_derive(struct tls_key_share *ks, uint8_t **shared_key,
    size_t *shared_key_len);
int tls_key_share_peer_security(const SSL *ssl, struct tls_key_share *ks);

__END_HIDDEN_DECLS

#endif
