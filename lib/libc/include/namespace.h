/*	$OpenBSD: namespace.h,v 1.16 2023/10/29 14:26:13 millert Exp $	*/

#ifndef _LIBC_NAMESPACE_H_
#define _LIBC_NAMESPACE_H_

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
 * ISO C11 or higher is required to build libc.
 * This must come _before_ sys/cdefs.h is included.
 */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112
#define _ISOC11_SOURCE 1
#endif

/*
 * For explanations of how to use these, see README
 * For explanations of why we use them and how they work, see DETAILS
 */

#include <sys/cdefs.h>	/* for __dso_hidden and __{weak,strong}_alias */

#define	__dso_protected		__attribute__((__visibility__("protected")))

#define	HIDDEN(x)		_libc_##x
#define	CANCEL(x)		_libc_##x##_cancel
#define	WRAP(x)			_libc_##x##_wrap
#define	HIDDEN_STRING(x)	"_libc_" __STRING(x)
#define	CANCEL_STRING(x)	"_libc_" __STRING(x) "_cancel"
#define	WRAP_STRING(x)		"_libc_" __STRING(x) "_wrap"

#define	PROTO_NORMAL(x)		__dso_hidden typeof(x) x asm(HIDDEN_STRING(x))
#define	PROTO_STD_DEPRECATED(x)	typeof(x) x __attribute__((deprecated))
#define	PROTO_DEPRECATED(x)	typeof(x) x __attribute__((deprecated, weak))
#define	PROTO_CANCEL(x)		__dso_hidden typeof(x) HIDDEN(x), \
					x asm(CANCEL_STRING(x))
#define	PROTO_WRAP(x)		PROTO_NORMAL(x), WRAP(x)
#define	PROTO_PROTECTED(x)	__dso_protected typeof(x) x

#define	DEF_STRONG(x)		__strong_alias(x, HIDDEN(x))
#define	DEF_WEAK(x)		__weak_alias(x, HIDDEN(x))
#define	DEF_CANCEL(x)		__weak_alias(x, CANCEL(x))
#define	DEF_WRAP(x)		__weak_alias(x, WRAP(x))
#define	DEF_SYS(x)		__strong_alias(_thread_sys_##x, HIDDEN(x))

#if !defined(__clang__) && __GNUC__ != 3
/* our gcc 4.2 handles redirecting builtins via PROTO_NORMAL()'s asm() label */
#define	DEF_BUILTIN(x)		DEF_STRONG(x)
#define	BUILTIN
#else
/*
 * clang and gcc can't redirect builtins via asm() labels, so mark
 * them protected instead.
 */
#define	DEF_BUILTIN(x)		__asm("")
#define	BUILTIN			__dso_protected
#endif

#define	MAKE_CLONE(dst, src)	__dso_hidden typeof(dst) HIDDEN(dst) \
				__attribute__((alias (HIDDEN_STRING(src))))

#define	__relro			__attribute__((section(".data.rel.ro")))


/*
 * gcc and clang will generate calls to the functions below.
 * Declare and redirect them here so we always go
 * directly to our hidden aliases.
 */
#include <sys/_types.h>
BUILTIN void	*memmove(void *, const void *, __size_t);
BUILTIN void	*memcpy(void *__restrict, const void *__restrict, __size_t);
BUILTIN void	*memset(void *, int, __size_t);
BUILTIN void	__stack_smash_handler(const char [], int __unused);
#if !defined(__clang__) && __GNUC__ != 3
PROTO_NORMAL(memmove);
PROTO_NORMAL(memcpy);
PROTO_NORMAL(memset);
PROTO_NORMAL(__stack_smash_handler);
#endif
#undef	BUILTIN

#endif  /* _LIBC_NAMESPACE_H_ */

