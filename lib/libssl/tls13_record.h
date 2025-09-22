/* $OpenBSD: tls13_record.h,v 1.5 2021/10/23 13:12:14 jsing Exp $ */
/*
 * Copyright (c) 2019 Joel Sing <jsing@openbsd.org>
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

#ifndef HEADER_TLS13_RECORD_H
#define HEADER_TLS13_RECORD_H

#include "bytestring.h"

__BEGIN_HIDDEN_DECLS

/*
 * TLSv1.3 Record Protocol - RFC 8446 section 5.
 *
 * The maximum plaintext is 2^14, however for inner plaintext an additional
 * byte is allowed for the content type. A maximum AEAD overhead of 255-bytes
 * is permitted, along with a 5-byte header, giving a maximum size of
 * 5 + 2^14 + 1 + 255 = 16,645-bytes.
 */
#define TLS13_RECORD_HEADER_LEN			5
#define TLS13_RECORD_MAX_AEAD_OVERHEAD		255
#define TLS13_RECORD_MAX_PLAINTEXT_LEN		16384
#define TLS13_RECORD_MAX_INNER_PLAINTEXT_LEN \
	(TLS13_RECORD_MAX_PLAINTEXT_LEN + 1)
#define TLS13_RECORD_MAX_CIPHERTEXT_LEN \
	(TLS13_RECORD_MAX_INNER_PLAINTEXT_LEN + TLS13_RECORD_MAX_AEAD_OVERHEAD)
#define TLS13_RECORD_MAX_LEN \
	(TLS13_RECORD_HEADER_LEN + TLS13_RECORD_MAX_CIPHERTEXT_LEN)

/*
 * TLSv1.3 Per-Record Nonces and Sequence Numbers - RFC 8446 section 5.3.
 */
#define TLS13_RECORD_SEQ_NUM_LEN 8

struct tls13_record;

struct tls13_record *tls13_record_new(void);
void tls13_record_free(struct tls13_record *_rec);
uint16_t tls13_record_version(struct tls13_record *_rec);
uint8_t tls13_record_content_type(struct tls13_record *_rec);
int tls13_record_header(struct tls13_record *_rec, CBS *_cbs);
int tls13_record_content(struct tls13_record *_rec, CBS *_cbs);
void tls13_record_data(struct tls13_record *_rec, CBS *_cbs);
int tls13_record_set_data(struct tls13_record *_rec, uint8_t *_data,
    size_t _data_len);
ssize_t tls13_record_recv(struct tls13_record *_rec, tls_read_cb _wire_read,
    void *_wire_arg);
ssize_t tls13_record_send(struct tls13_record *_rec, tls_write_cb _wire_write,
    void *_wire_arg);

__END_HIDDEN_DECLS

#endif
