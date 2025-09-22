/* $OpenBSD: icdb.h,v 1.4 2016/09/04 14:51:39 nicm Exp $ */
/*
 * Copyright (c) 2015 Ted Unangst <tedu@openbsd.org>
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

#ifndef _ICDB_H_
#define _ICDB_H_

#include <sys/types.h>

__BEGIN_DECLS

struct icdb;

struct icdb *icdb_new(uint32_t version, uint32_t nentries, uint32_t entrysize,
    uint32_t nkeys, const uint32_t *keysizes, const uint32_t *keyoffsets);

struct icdb *icdb_open(const char *name, int flags, uint32_t version);
int icdb_get(struct icdb *db, void *entry, uint32_t idx);
int icdb_lookup(struct icdb *db, int keynum, const void *key, void *entry,
    uint32_t *idxp);
int icdb_nentries(struct icdb *db);
const void *icdb_entries(struct icdb *db);
int icdb_update(struct icdb *db, const void *entry, int offset);
int icdb_add(struct icdb *db, const void *entry);
int icdb_rehash(struct icdb *db);
int icdb_save(struct icdb *db, int fd);
int icdb_close(struct icdb *db);

__END_DECLS

#endif /* !_ICDB_H_ */
