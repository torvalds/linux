/*	$OpenBSD: ssl_namespace.h,v 1.4 2025/08/18 16:00:53 tb Exp $	*/
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

#ifndef _LIBSSL_SSL_NAMESPACE_H_
#define _LIBSSL_SSL_NAMESPACE_H_

/*
 * If marked as 'used', then internal calls use the name with prefix "_lssl_"
 * and we alias that to the normal name.
 */

#ifdef LIBRESSL_NAMESPACE
#define LSSL_UNUSED(x)		typeof(x) x __attribute__((deprecated))
#define LSSL_USED(x)		__attribute__((visibility("hidden")))	\
				typeof(x) x asm("_lssl_"#x)
#if defined(__hppa__)
#define LSSL_ALIAS(x)		asm("! .global "#x" ! .set "#x", _lssl_"#x)
#else
#define LSSL_ALIAS(x)		asm(".global "#x"; "#x" = _lssl_"#x)
#endif
#else
#define LSSL_UNUSED(x)
#define LSSL_USED(x)
#ifdef _MSC_VER
#define LSSL_ALIAS(x)
#else
#define LSSL_ALIAS(x)		asm("")
#endif /* _MSC_VER */
#endif

#endif	/* _LIBSSL_SSL_NAMESPACE_H_ */
