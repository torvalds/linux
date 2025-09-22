//===-- int_endianness.h - configuration header for compiler-rt -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a configuration header for compiler-rt.
// This file is not part of the interface of this library.
//
//===----------------------------------------------------------------------===//

#ifndef INT_ENDIANNESS_H
#define INT_ENDIANNESS_H

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) &&                \
    defined(__ORDER_LITTLE_ENDIAN__)

// Clang and GCC provide built-in endianness definitions.
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define _YUGA_LITTLE_ENDIAN 0
#define _YUGA_BIG_ENDIAN 1
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define _YUGA_LITTLE_ENDIAN 1
#define _YUGA_BIG_ENDIAN 0
#endif // __BYTE_ORDER__

#else // Compilers other than Clang or GCC.

#if defined(__SVR4) && defined(__sun)
#include <sys/byteorder.h>

#if defined(_BIG_ENDIAN)
#define _YUGA_LITTLE_ENDIAN 0
#define _YUGA_BIG_ENDIAN 1
#elif defined(_LITTLE_ENDIAN)
#define _YUGA_LITTLE_ENDIAN 1
#define _YUGA_BIG_ENDIAN 0
#else // !_LITTLE_ENDIAN
#error "unknown endianness"
#endif // !_LITTLE_ENDIAN

#endif // Solaris

// ..

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) ||   \
    defined(__minix)
#include <sys/endian.h>

#if _BYTE_ORDER == _BIG_ENDIAN
#define _YUGA_LITTLE_ENDIAN 0
#define _YUGA_BIG_ENDIAN 1
#elif _BYTE_ORDER == _LITTLE_ENDIAN
#define _YUGA_LITTLE_ENDIAN 1
#define _YUGA_BIG_ENDIAN 0
#endif // _BYTE_ORDER

#endif // *BSD

#if defined(__OpenBSD__)
#include <machine/endian.h>

#if _BYTE_ORDER == _BIG_ENDIAN
#define _YUGA_LITTLE_ENDIAN 0
#define _YUGA_BIG_ENDIAN 1
#elif _BYTE_ORDER == _LITTLE_ENDIAN
#define _YUGA_LITTLE_ENDIAN 1
#define _YUGA_BIG_ENDIAN 0
#endif // _BYTE_ORDER

#endif // OpenBSD

// ..

// Mac OSX has __BIG_ENDIAN__ or __LITTLE_ENDIAN__ automatically set by the
// compiler (at least with GCC)
#if defined(__APPLE__) || defined(__ellcc__)

#ifdef __BIG_ENDIAN__
#if __BIG_ENDIAN__
#define _YUGA_LITTLE_ENDIAN 0
#define _YUGA_BIG_ENDIAN 1
#endif
#endif // __BIG_ENDIAN__

#ifdef __LITTLE_ENDIAN__
#if __LITTLE_ENDIAN__
#define _YUGA_LITTLE_ENDIAN 1
#define _YUGA_BIG_ENDIAN 0
#endif
#endif // __LITTLE_ENDIAN__

#endif // Mac OSX

// ..

#if defined(_WIN32)

#define _YUGA_LITTLE_ENDIAN 1
#define _YUGA_BIG_ENDIAN 0

#endif // Windows

#endif // Clang or GCC.

// .

#if !defined(_YUGA_LITTLE_ENDIAN) || !defined(_YUGA_BIG_ENDIAN)
#error Unable to determine endian
#endif // Check we found an endianness correctly.

#endif // INT_ENDIANNESS_H
