/* $OpenBSD: x509_issuer_cache.h,v 1.3 2023/12/30 18:06:59 tb Exp $ */
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

/* x509_issuer_cache */
#ifndef HEADER_X509_ISSUER_CACHE_H
#define HEADER_X509_ISSUER_CACHE_H

#include <sys/tree.h>
#include <sys/queue.h>

#include <openssl/x509.h>

__BEGIN_HIDDEN_DECLS

struct x509_issuer {
	RB_ENTRY(x509_issuer) entry;
	TAILQ_ENTRY(x509_issuer) queue;	/* LRU of entries */
	/* parent_md and child_md must point to EVP_MAX_MD_SIZE of memory */
	unsigned char *parent_md;
	unsigned char *child_md;
	int valid;			/* Result of signature validation. */
};

#define X509_ISSUER_CACHE_MAX 40000	/* Approx 7.5 MB, entries 200 bytes */

int x509_issuer_cache_set_max(size_t max);
int x509_issuer_cache_find(unsigned char *parent_md, unsigned char *child_md);
void x509_issuer_cache_add(unsigned char *parent_md, unsigned char *child_md,
    int valid);
void x509_issuer_cache_free(void);

__END_HIDDEN_DECLS

#endif
