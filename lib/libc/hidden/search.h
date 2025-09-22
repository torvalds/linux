/*	$OpenBSD: search.h,v 1.2 2021/12/08 22:06:28 cheloha Exp $	*/
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

#ifndef _LIBC_SEARCH_H_
#define _LIBC_SEARCH_H_

#include_next <search.h>

PROTO_DEPRECATED(hcreate);
PROTO_DEPRECATED(hdestroy);
PROTO_DEPRECATED(hsearch);
PROTO_DEPRECATED(insque);
PROTO_NORMAL(lfind);
PROTO_DEPRECATED(lsearch);
PROTO_DEPRECATED(remque);
PROTO_DEPRECATED(tdelete);
PROTO_DEPRECATED(tfind);
PROTO_DEPRECATED(tsearch);
PROTO_DEPRECATED(twalk);

#endif /* !_LIBC_SEARCH_H_ */
