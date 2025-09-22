/*
 * endian.h -- byte order abstractions
 *
 * Copyright (c) 2023, NLnet Labs.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef ENDIAN_H
#define ENDIAN_H

#include "config.h"

// https://www.austingroupbugs.net/view.php?id=162#c665

#if _WIN32
#include <stdlib.h>

#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321
#define BYTE_ORDER LITTLE_ENDIAN

#if BYTE_ORDER == LITTLE_ENDIAN
#define htobe16(x) _byteswap_ushort(x)
#define htobe32(x) _byteswap_ulong(x)
#define htobe64(x) _byteswap_uint64(x)
#define htole16(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)

#define be16toh(x) _byteswap_ushort(x)
#define be32toh(x) _byteswap_ulong(x)
#define be64toh(x) _byteswap_uint64(x)
#define le16toh(x) (x)
#define le32toh(x) (x)
#define le64toh(x) (x)
#else
#define htobe16(x) (x)
#define htobe32(x) (x)
#define htobe64(x) (x)
#define htole16(x) _byteswap_ushort(x)
#define htole32(x) _byteswap_ulong(x)
#define htole64(x) _byteswap_uint64(x)

#define be16toh(x) (x)
#define be32toh(x) (x)
#define be64toh(x) (x)
#define le16toh(x) _byteswap_ushort(x)
#define le32toh(x) _byteswap_ulong(x)
#define le64toh(x) _byteswap_uint64(x)
#endif

#elif __APPLE__
#include <libkern/OSByteOrder.h>

#if !defined BYTE_ORDER
# define BYTE_ORDER __BYTE_ORDER__
#endif
#if !defined LITTLE_ENDIAN
# define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif
#if !defined BIG_ENDIAN
# define BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define htole64(x) OSSwapHostToLittleInt64(x)

#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#else
#if HAVE_ENDIAN_H
#include <endian.h>
#elif defined(__OpenBSD__)
// endian.h was added in OpenBSD 5.6. machine/endian.h exports optimized
// bswap routines for use in sys/endian.h, which it includes.
#include <machine/endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/endian.h>
#endif

#if defined(__NetBSD__)
/* Bring bswap{16,32,64} into scope: */
#include <sys/types.h>
#include <machine/bswap.h>
#endif

#if !defined(LITTLE_ENDIAN)
# if defined(__ORDER_LITTLE_ENDIAN__)
#   define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
# else
#   define LITTLE_ENDIAN 1234
# endif
#endif

#if !defined(BIG_ENDIAN)
# if defined(__ORDER_BIG_ENDIAN__)
#   define BIG_ENDIAN __ORDER_BIG_ENDIAN__
# else
#   define BIG_ENDIAN 4321
# endif
#endif

#if !defined(BYTE_ORDER)
# if defined(__BYTE_ORDER__)
#   define BYTE_ORDER __BYTE_ORDER__
# elif defined(__BYTE_ORDER)
#   define BYTE_ORDER __BYTE_ORDER
# elif defined(i386) || defined(__i386__) || defined(__i486__) || \
       defined(__i586__) || defined(__i686__) || \
       defined(__x86) || defined(__x86_64) || defined(__x86_64__) || \
       defined(__amd64) || defined(__amd64__)
#   define BYTE_ORDER LITTLE_ENDIAN
# elif defined(sparc) || defined(__sparc) || defined(__sparc__) || \
       defined(POWERPC) || defined(mc68000) || defined(sel)
#   define BYTE_ORDER BIG_ENDIAN
# else
#   error "missing definition of BYTE_ORDER"
# endif
#endif

#if !defined(__NetBSD__)

#if !HAVE_DECL_BSWAP16
static really_inline uint16_t bswap16(uint16_t x)
{
  // Copied from src/common/lib/libc/gen/bswap16.c in NetBSD
  // Written by Manuel Bouyer <bouyer@NetBSD.org>.
  // Public domain.
  return ((x << 8) & 0xff00) | ((x >> 8) & 0x00ff);
}
#endif

#if !HAVE_DECL_BSWAP32
static really_inline uint32_t bswap32(uint32_t x)
{
  // Copied from src/common/lib/libc/gen/bswap32.c in NetBSD
  // Written by Manuel Bouyer <bouyer@NetBSD.org>.
  // Public domain.
  return ( (x << 24) & 0xff000000 ) |
         ( (x <<  8) & 0x00ff0000 ) |
         ( (x >>  8) & 0x0000ff00 ) |
         ( (x >> 24) & 0x000000ff );
}
#endif

#if !HAVE_DECL_BSWAP64
static really_inline uint64_t bswap64(uint64_t x)
{
  // Copied from src/common/lib/libc/gen/bswap64.c in NetBSD
  // Written by Manuel Bouyer <bouyer@NetBSD.org>.
  // Public domain.
  return ( (x << 56) & 0xff00000000000000ull ) |
         ( (x << 40) & 0x00ff000000000000ull ) |
         ( (x << 24) & 0x0000ff0000000000ull ) |
         ( (x <<  8) & 0x000000ff00000000ull ) |
         ( (x >>  8) & 0x00000000ff000000ull ) |
         ( (x >> 24) & 0x0000000000ff0000ull ) |
         ( (x >> 40) & 0x000000000000ff00ull ) |
         ( (x >> 56) & 0x00000000000000ffull );
}
#endif

#endif /* !defined(__NetBSD__) */

# if BYTE_ORDER == LITTLE_ENDIAN
#   define htobe(bits, x) bswap ## bits((x))
#   define htole(bits, x) (x)
#   define betoh(bits, x) bswap ## bits((x))
#   define letoh(bits, x) (x)
# else
#   define htobe(bits, x) (x)
#   define htole(bits, x) bswap ## bits((x))
#   define betoh(bits, x) (x)
#   define letoh(bits, x) bswap ## bits((x))
# endif

# if !defined htobe16
#   define htobe16(x) htobe(16,(x))
# endif
# if !defined htobe32
#   define htobe32(x) htobe(32,(x))
# endif
# if !defined htobe64
#   define htobe64(x) htobe(64,(x))
# endif
# if !defined htole16
#   define htole16(x) htole(16,(x))
# endif
# if !defined htole32
#   define htole32(x) htole(32,(x))
# endif
# if !defined htole64
#   define htole64(x) htole(64,(x))
# endif

# if !defined be16toh
#   define be16toh(x) betoh(16,(x))
# endif
# if !defined be32toh
#   define be32toh(x) betoh(32,(x))
# endif
# if !defined be64toh
#   define be64toh(x) betoh(64,(x))
# endif
# if !defined le16toh
#   define le16toh(x) letoh(16,(x))
# endif
# if !defined le32toh
#   define le32toh(x) letoh(32,(x))
# endif
# if !defined le64toh
#   define le64toh(x) letoh(64,(x))
# endif
#endif

#endif // ENDIAN_H
