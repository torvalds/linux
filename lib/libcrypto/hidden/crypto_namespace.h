/*	$OpenBSD: crypto_namespace.h,v 1.5 2025/08/18 16:00:05 tb Exp $	*/
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

#ifndef _LIBCRYPTO_CRYPTO_NAMESPACE_H_
#define _LIBCRYPTO_CRYPTO_NAMESPACE_H_

/*
 * If marked as 'used', then internal calls use the name with prefix "_lcry_"
 * and we alias that to the normal name *and* the name with prefix "_libre_";
 * external calls use the latter name.
 */

#ifdef LIBRESSL_NAMESPACE
#ifdef LIBRESSL_CRYPTO_NAMESPACE
#  define LCRYPTO_UNUSED(x)	__attribute__((deprecated))		\
				__attribute__((visibility("hidden")))	\
				typeof(x) x asm("_lcry_"#x)
#  define LCRYPTO_USED(x)	__attribute__((visibility("hidden")))	\
				typeof(x) x asm("_lcry_"#x)
#  if defined(__hppa__)
#  define LCRYPTO_ALIAS1(pre,x)	asm("! .global "#pre#x" ! .set "#pre#x", _lcry_"#x)
#else
#  define LCRYPTO_ALIAS1(pre,x)	asm(".global "#pre#x"; "#pre#x" = _lcry_"#x)
#endif
#  define LCRYPTO_ALIAS(x)	LCRYPTO_ALIAS1(,x); LCRYPTO_ALIAS1(_libre_,x)
#else
#  define LCRYPTO_UNUSED(x)	typeof(x) x __attribute__((deprecated))
#  define LCRYPTO_USED(x)	typeof(x) x asm("_libre_"#x)
#endif
#else
# define LCRYPTO_UNUSED(x)
# define LCRYPTO_USED(x)
# define LCRYPTO_ALIAS1(pre,x)
#ifdef _MSC_VER
# define LCRYPTO_ALIAS(x)
#else
# define LCRYPTO_ALIAS(x)	asm("")
#endif /* _MSC_VER */
#endif

#endif	/* _LIBCRYPTO_CRYPTO_NAMESPACE_H_ */
