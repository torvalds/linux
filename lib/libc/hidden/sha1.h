/*	$OpenBSD: sha1.h,v 1.1 2015/09/11 09:18:27 guenther Exp $	*/
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
/*	$OpenBSD: sha1.h,v 1.1 2015/09/11 09:18:27 guenther Exp $	*/

#ifndef _LIBC_SHA1_H
#define _LIBC_SHA1_H

#include_next <sha1.h>

PROTO_NORMAL(SHA1Data);
PROTO_NORMAL(SHA1End);
PROTO_NORMAL(SHA1File);
PROTO_NORMAL(SHA1FileChunk);
PROTO_NORMAL(SHA1Final);
PROTO_NORMAL(SHA1Init);
PROTO_NORMAL(SHA1Pad);
PROTO_NORMAL(SHA1Transform);
PROTO_NORMAL(SHA1Update);

#endif /* _LIBC_SHA1_H */
