/* $OpenBSD: tls_content.h,v 1.2 2022/11/11 17:15:27 jsing Exp $ */
/*
 * Copyright (c) 2020 Joel Sing <jsing@openbsd.org>
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

#ifndef HEADER_TLS_CONTENT_H
#define HEADER_TLS_CONTENT_H

#include "bytestring.h"

__BEGIN_HIDDEN_DECLS

struct tls_content;

struct tls_content *tls_content_new(void);
void tls_content_clear(struct tls_content *content);
void tls_content_free(struct tls_content *content);

CBS *tls_content_cbs(struct tls_content *content);
int tls_content_equal(struct tls_content *content, const uint8_t *buf, size_t n);
size_t tls_content_remaining(struct tls_content *content);
uint8_t tls_content_type(struct tls_content *content);
uint16_t tls_content_epoch(struct tls_content *content);

int tls_content_dup_data(struct tls_content *content, uint8_t type,
    const uint8_t *data, size_t data_len);
void tls_content_set_data(struct tls_content *content, uint8_t type,
    const uint8_t *data, size_t data_len);
int tls_content_set_bounds(struct tls_content *content, size_t offset,
    size_t len);
void tls_content_set_epoch(struct tls_content *content, uint16_t epoch);

ssize_t tls_content_peek(struct tls_content *content, uint8_t *buf, size_t n);
ssize_t tls_content_read(struct tls_content *content, uint8_t *buf, size_t n);

__END_HIDDEN_DECLS

#endif
