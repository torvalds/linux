/*	$OpenBSD: dlfcn.h,v 1.4 2020/10/09 16:31:03 otto Exp $	*/
/*
 * Copyright (c) 2019 Philip Guenther <guenther@openbsd.org>
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

#ifndef	_LIBC_DLFCN_H_
#define	_LIBC_DLFCN_H_

#include_next <dlfcn.h>

PROTO_NORMAL(dladdr);
PROTO_DEPRECATED(dlclose);
PROTO_DEPRECATED(dlerror);
PROTO_DEPRECATED(dlopen);
PROTO_DEPRECATED(dlsym);
PROTO_NORMAL(dlctl);

#endif /* _LIBC_DLFCN_H_ */
