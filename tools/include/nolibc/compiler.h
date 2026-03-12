/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * NOLIBC compiler support header
 * Copyright (C) 2023 Thomas Weißschuh <linux@weissschuh.net>
 */
#ifndef _NOLIBC_COMPILER_H
#define _NOLIBC_COMPILER_H

#if defined(__has_attribute)
#  define __nolibc_has_attribute(attr) __has_attribute(attr)
#else
#  define __nolibc_has_attribute(attr) 0
#endif

#if defined(__has_feature)
#  define __nolibc_has_feature(feature) __has_feature(feature)
#else
#  define __nolibc_has_feature(feature) 0
#endif

#define __nolibc_aligned(alignment) __attribute__((aligned(alignment)))
#define __nolibc_aligned_as(type) __nolibc_aligned(__alignof__(type))

#if __nolibc_has_attribute(naked)
#  define __nolibc_entrypoint __attribute__((naked))
#  define __nolibc_entrypoint_epilogue()
#else
#  define __nolibc_entrypoint __attribute__((optimize("Os", "omit-frame-pointer")))
#  define __nolibc_entrypoint_epilogue() __builtin_unreachable()
#endif /* __nolibc_has_attribute(naked) */

#if defined(__SSP__) || defined(__SSP_STRONG__) || defined(__SSP_ALL__) || defined(__SSP_EXPLICIT__)

#define _NOLIBC_STACKPROTECTOR

#endif /* defined(__SSP__) ... */

#if __nolibc_has_attribute(no_stack_protector)
#  define __no_stack_protector __attribute__((no_stack_protector))
#else
#  define __no_stack_protector __attribute__((__optimize__("-fno-stack-protector")))
#endif /* __nolibc_has_attribute(no_stack_protector) */

#if __nolibc_has_attribute(__fallthrough__)
#  define __nolibc_fallthrough do { } while (0); __attribute__((__fallthrough__))
#else
#  define __nolibc_fallthrough do { } while (0)
#endif /* __nolibc_has_attribute(fallthrough) */

#define __nolibc_version(_major, _minor, _patch) ((_major) * 10000 + (_minor) * 100 + (_patch))

#ifdef __GNUC__
#  define __nolibc_gnuc_version \
		__nolibc_version(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#else
#  define __nolibc_gnuc_version 0
#endif /* __GNUC__ */

#ifdef __clang__
#  define __nolibc_clang_version \
		__nolibc_version(__clang_major__, __clang_minor__, __clang_patchlevel__)
#else
#  define __nolibc_clang_version 0
#endif /* __clang__ */

#if __STDC_VERSION__ >= 201112L || \
	__nolibc_gnuc_version >= __nolibc_version(4, 6, 0) || \
	__nolibc_clang_version >= __nolibc_version(3, 0, 0)
#  define __nolibc_static_assert(_t) _Static_assert(_t, "")
#else
#  define __nolibc_static_assert(_t)
#endif

#endif /* _NOLIBC_COMPILER_H */
