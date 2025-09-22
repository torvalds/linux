/* $OpenBSD: icdb.h,v 1.1 2015/11/25 15:49:50 guenther Exp $ */
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#ifndef _LIBC_ICDB_H_
#define	_LIBC_ICDB_H_

#include_next <icdb.h>

PROTO_NORMAL(icdb_add);
PROTO_NORMAL(icdb_close);
PROTO_NORMAL(icdb_entries);
PROTO_NORMAL(icdb_get);
PROTO_NORMAL(icdb_lookup);
PROTO_NORMAL(icdb_nentries);
PROTO_NORMAL(icdb_new);
PROTO_NORMAL(icdb_open);
PROTO_NORMAL(icdb_rehash);
PROTO_NORMAL(icdb_save);
PROTO_NORMAL(icdb_update);

#endif /* !_LIBC_ICDB_H_ */
