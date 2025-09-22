/*	$OpenBSD: namespace.h,v 1.3 2018/03/12 06:19:19 guenther Exp $	*/

#ifndef _LIBM_NAMESPACE_H_
#define _LIBM_NAMESPACE_H_

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

/*
 * The goal: calls from inside libc to other libc functions should be via
 * identifiers that are of hidden visibility and--to avoid confusion--are
 * in the reserved namespace.  By doing this these calls are protected
 * from overriding by applications and on many platforms can avoid creation
 * or use of GOT or PLT entries.  I've chosen a prefix of "_libm_" for this.
 * These will not be declared directly; instead, the gcc "asm labels"
 * extension will be used rename the function.
 *
 * In order to actually set up the desired asm labels, we use these in
 * the internal .h files:
 *   PROTO_NORMAL(x)		Symbols used both internally and externally
 *	This makes gcc convert use of x to use _libm_x instead.  Use
 *	DEF_STD(x) or DEF_NONSTD(x) to create the external alias.
 *	ex: PROTO_NORMAL(ceil)
 *
 *   PROTO_STD_DEPRECATED(x)	Standard C symbols that are not used internally
 * 	This just marks the symbol as deprecated, with no renaming.
 *	Do not use DEF_*(x) with this.
 *	ex: PROTO_STD_DEPRECATED(tgammal)
 *
 *   PROTO_DEPRECATED(x)	Symbols not in C that are not used internally
 * 	This marks the symbol as deprecated and, in the static lib, weak.
 *	No renaming is done.  Do not use DEF_*(x) with this.
 *	ex: PROTO_DEPRECATED(creat)
 *
 * Finally, to create the expected aliases, we use these in the .c files
 * where the definitions are:
 *   DEF_STD(x)		Symbols reserved to or specified by ISO C
 *	This defines x as a strong alias for _libm_x; this must only
 *	be used for symbols that are reserved by the C standard
 *	(or reserved in the external identifier namespace).
 *	Matches with PROTO_NORMAL()
 *	ex: DEF_STD(fopen)
 *
 *   DEF_NONSTD(x)		Symbols used internally and not in ISO C
 *	This defines x as a alias for _libm_x, weak in the static version
 *	Matches with PROTO_NORMAL()
 *	ex: DEF_NONSTD(lseek)
 *
 *   LDBL_CLONE(x)		long double aliases that are used
 *	This defines xl and _libm_xl as aliases for _libm_x.
 *	Matches with LDBL_PROTO_NORMAL()
 *
 *   LDBL_UNUSED_CLONE(x)	long double aliases that are unused
 *	This defines xl as an alias for _libm_x.
 *	Matches with LDBL_PROTO_STD_DEPRECATED()
 *
 *   LDBL_MAYBE_CLONE(x)
 *   LDBL_MAYBE_UNUSED_CLONE(x)
 *	Like LDBL_CLONE() and LDBL_UNUSED_CLONE(), except they do nothing
 *	if LDBL_MANT_DIG != DBL_MANT_DIG
 *
 *   MAKE_UNUSED_CLONE(dst, src)	Unused symbols that are exact clones
 *					of other symbols
 *	This declares dst as being the same type as dst, and makes
 *	_libm_dst a strong, hidden alias for _libm_src.  You still need to
 *	DEF_STD(dst) or DEF_NONSTD(dst) to alias dst itself
 *	ex: MAKE_UNUSED_CLONE(nexttoward, nextafter);
 */

#include <sys/cdefs.h>	/* for __dso_hidden and __{weak,strong}_alias */

#ifndef PIC
# define WEAK_IN_STATIC_ALIAS(x,y)	__weak_alias(x,y)
# define WEAK_IN_STATIC			__attribute__((weak))
#else
# define WEAK_IN_STATIC_ALIAS(x,y)	__strong_alias(x,y)
# define WEAK_IN_STATIC			/* nothing */
#endif

#define	HIDDEN(x)		_libm_##x
#define	HIDDEN_STRING(x)	"_libm_" __STRING(x)

#define	PROTO_NORMAL(x)		__dso_hidden typeof(x) HIDDEN(x), x asm(HIDDEN_STRING(x))
#define	PROTO_STD_DEPRECATED(x)	typeof(x) HIDDEN(x), x __attribute__((deprecated))
#define PROTO_DEPRECATED(x)	PROTO_STD_DEPRECATED(x) WEAK_IN_STATIC

#define	DEF_STD(x)		__strong_alias(x, HIDDEN(x))
#define DEF_NONSTD(x)		WEAK_IN_STATIC_ALIAS(x, HIDDEN(x))

#define	MAKE_UNUSED_CLONE(dst, src)	__strong_alias(dst, src)
#define LDBL_UNUSED_CLONE(x)		__strong_alias(x##l, HIDDEN(x))
#define LDBL_NONSTD_UNUSED_CLONE(x)	WEAK_IN_STATIC_ALIAS(x##l, HIDDEN(x))
#define LDBL_CLONE(x)		LDBL_UNUSED_CLONE(x); \
				__dso_hidden typeof(HIDDEN(x##l)) HIDDEN(x##l) \
				__attribute__((alias (HIDDEN_STRING(x))))
#define LDBL_NONSTD_CLONE(x)	LDBL_NONSTD_UNUSED_CLONE(x); \
				__dso_hidden typeof(HIDDEN(x##l)) HIDDEN(x##l) \
				__attribute__((alias (HIDDEN_STRING(x))))

#if __LDBL_MANT_DIG__ == __DBL_MANT_DIG__
# define LDBL_PROTO_NORMAL(x)		typeof(x) HIDDEN(x)
# define LDBL_PROTO_STD_DEPRECATED(x)	typeof(x) HIDDEN(x)
# define LDBL_MAYBE_CLONE(x)		LDBL_CLONE(x)
# define LDBL_MAYBE_UNUSED_CLONE(x)	LDBL_UNUSED_CLONE(x)
# define LDBL_MAYBE_NONSTD_UNUSED_CLONE(x)	LDBL_NONSTD_UNUSED_CLONE(x)
# define LDBL_MAYBE_NONSTD_CLONE(x)	LDBL_NONSTD_CLONE(x)
#else
# define LDBL_PROTO_NORMAL(x)		PROTO_NORMAL(x)
# define LDBL_PROTO_STD_DEPRECATED(x)	PROTO_STD_DEPRECATED(x)
# define LDBL_MAYBE_CLONE(x)		__asm("")
# define LDBL_MAYBE_UNUSED_CLONE(x)	__asm("")
# define LDBL_MAYBE_NONSTD_UNUSED_CLONE(x)	__asm("")
# define LDBL_MAYBE_NONSTD_CLONE(x)	__asm("")
#endif

#endif	/* _LIBM_NAMESPACE_H_ */
