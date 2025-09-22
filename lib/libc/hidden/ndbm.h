/*	$OpenBSD: ndbm.h,v 1.1 2015/09/12 15:20:52 guenther Exp $	*/
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

#ifndef _LIBC_NDBM_H_
#define	_LIBC_NDBM_H_

#include_next <ndbm.h>

PROTO_DEPRECATED(dbm_clearerr);
PROTO_NORMAL(dbm_close);
PROTO_NORMAL(dbm_delete);
PROTO_DEPRECATED(dbm_dirfno);
PROTO_DEPRECATED(dbm_error);
PROTO_NORMAL(dbm_fetch);
PROTO_NORMAL(dbm_firstkey);
PROTO_NORMAL(dbm_nextkey);
PROTO_DEPRECATED(dbm_open);
PROTO_NORMAL(dbm_rdonly);
PROTO_NORMAL(dbm_store);

#endif /* !_LIBC_NDBM_H_ */
