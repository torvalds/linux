/*	$OpenBSD: hash.h,v 1.3 2017/05/30 17:22:00 yasuoka Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 */
/* $Id: hash.h,v 1.3 2017/05/30 17:22:00 yasuoka Exp $ */
#ifndef HASH_H
#define HASH_H

#ifndef	HASH_SIZE
#define	HASH_SIZE	127
#endif

typedef struct HASH_LINK {
	const void *key;
	struct HASH_LINK *next;
	void *item;
} hash_link;

typedef struct {
	int (*cmp) (const void *, const void *);
	uint32_t (*hash) (const void *, int);
	hash_link **bucket;
	size_t size;
	int cur;
	hash_link *bucket_cur;
} hash_table;

#ifdef __cplusplus
extern "C" {
#endif
hash_table  *hash_create(int (*)(const void *, const void *), uint32_t (*) (const void *, int), int);
hash_link   *hash_first(hash_table *);
hash_link   *hash_next(hash_table *);
hash_link   *hash_lookup(hash_table *, const void *);
int         hash_insert(hash_table *, const void *, void *);
int         hash_delete(hash_table *, const void *, int);
void        hash_delete_all(hash_table *, int);
void        hash_free(hash_table *);

#ifdef __cplusplus
}
#endif

#endif /* HASH_H */
