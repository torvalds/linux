/* $OpenBSD: x509_verify.h,v 1.2 2021/11/04 23:52:34 beck Exp $ */
/*
 * Copyright (c) 2020 Bob Beck <beck@openbsd.org>
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
#ifndef HEADER_X509_VERIFY_H
#define HEADER_X509_VERIFY_H

#ifdef LIBRESSL_INTERNAL
struct x509_verify_ctx;
struct x509_verify_cert_info;
typedef struct x509_verify_ctx X509_VERIFY_CTX;

X509_VERIFY_CTX *x509_verify_ctx_new(STACK_OF(X509) *roots);
void x509_verify_ctx_free(struct x509_verify_ctx *ctx);

int x509_verify_ctx_set_max_depth(X509_VERIFY_CTX *ctx, size_t max);
int x509_verify_ctx_set_max_chains(X509_VERIFY_CTX *ctx, size_t max);
int x509_verify_ctx_set_max_signatures(X509_VERIFY_CTX *ctx, size_t max);
int x509_verify_ctx_set_purpose(X509_VERIFY_CTX *ctx, int purpose_id);
int x509_verify_ctx_set_intermediates(X509_VERIFY_CTX *ctx,
    STACK_OF(X509) *intermediates);

const char *x509_verify_ctx_error_string(X509_VERIFY_CTX *ctx);
size_t x509_verify_ctx_error_depth(X509_VERIFY_CTX *ctx);

STACK_OF(X509) *x509_verify_ctx_chain(X509_VERIFY_CTX *ctx, size_t chain);

size_t x509_verify(X509_VERIFY_CTX *ctx, X509 *leaf, char *name);
#endif

#endif
