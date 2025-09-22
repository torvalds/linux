/*	$OpenBSD: namespace.h,v 1.1 2016/04/02 19:56:53 guenther Exp $	*/

#ifndef _LIBPTHREAD_NAMESPACE_H_
#define _LIBPTHREAD_NAMESPACE_H_

/*
 * Copyright (c) 2016 Philip Guenther <guenther@openbsd.org>
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

#include <sys/cdefs.h>	/* for __dso_hidden and __strong_alias */

#ifndef PIC
# define WEAK_IN_STATIC_ALIAS(x,y)      __weak_alias(x,y)
# define WEAK_IN_STATIC                 __attribute__((weak))
#else
# define WEAK_IN_STATIC_ALIAS(x,y)      __strong_alias(x,y)
# define WEAK_IN_STATIC                 /* nothing */
#endif

#define	HIDDEN(x)		_libpthread_##x
#define	HIDDEN_STRING(x)	"_libpthread_" __STRING(x)

#define	PROTO_NORMAL(x)		__dso_hidden typeof(x) x asm(HIDDEN_STRING(x))
#define	PROTO_STD_DEPRECATED(x)	typeof(x) x __attribute__((deprecated))
#define	PROTO_DEPRECATED(x)	PROTO_STD_DEPRECATED(x) WEAK_IN_STATIC

#define	DEF_STD(x)		__strong_alias(x, HIDDEN(x))
#define	DEF_NONSTD(x)		WEAK_IN_STATIC_ALIAS(x, HIDDEN(x))

#endif  /* _LIBPTHREAD_NAMESPACE_H_ */

