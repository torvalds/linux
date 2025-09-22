/*	$OpenBSD: err.h,v 1.2 2015/09/10 18:13:46 guenther Exp $	*/
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

#ifndef _LIBC_ERR_H_
#define	_LIBC_ERR_H_

#include_next <err.h>

PROTO_NORMAL(err);
PROTO_NORMAL(errc);
PROTO_NORMAL(errx);
PROTO_NORMAL(verr);
PROTO_NORMAL(verrc);
PROTO_NORMAL(verrx);
PROTO_NORMAL(vwarn);
PROTO_NORMAL(vwarnc);
PROTO_NORMAL(vwarnx);
PROTO_NORMAL(warn);
PROTO_NORMAL(warnc);
PROTO_NORMAL(warnx);

#endif /* !_LIBC_ERR_H_ */
