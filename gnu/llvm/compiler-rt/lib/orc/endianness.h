//===- endian.h - Endianness support ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares generic and optimized functions to swap the byte order of
// an integral type.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_ENDIAN_H
#define ORC_RT_ENDIAN_H

#include <cstddef>
#include <cstdint>
#include <type_traits>
#if defined(_MSC_VER) && !defined(_DEBUG)
#include <stdlib.h>
#endif

#if defined(__linux__) || defined(__GNU__) || defined(__HAIKU__) ||            \
    defined(__Fuchsia__) || defined(__EMSCRIPTEN__)
#include <endian.h>
#elif defined(_AIX)
#include <sys/machine.h>
#elif defined(__sun)
/* Solaris provides _BIG_ENDIAN/_LITTLE_ENDIAN selector in sys/types.h */
#include <sys/types.h>
#define BIG_ENDIAN 4321
#define LITTLE_ENDIAN 1234
#if defined(_BIG_ENDIAN)
#define BYTE_ORDER BIG_ENDIAN
#else
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#elif defined(__MVS__)
#define BIG_ENDIAN 4321
#define LITTLE_ENDIAN 1234
#define BYTE_ORDER BIG_ENDIAN
#else
#if !defined(BYTE_ORDER) && !defined(_WIN32)
#include <machine/endian.h>
#endif
#endif

namespace __orc_rt {

/// ByteSwap_16 - This function returns a byte-swapped representation of
/// the 16-bit argument.
inline uint16_t ByteSwap_16(uint16_t value) {
#if defined(_MSC_VER) && !defined(_DEBUG)
  // The DLL version of the runtime lacks these functions (bug!?), but in a
  // release build they're replaced with BSWAP instructions anyway.
  return _byteswap_ushort(value);
#else
  uint16_t Hi = value << 8;
  uint16_t Lo = value >> 8;
  return Hi | Lo;
#endif
}

/// This function returns a byte-swapped representation of the 32-bit argument.
inline uint32_t ByteSwap_32(uint32_t value) {
#if defined(__llvm__) || (defined(__GNUC__) && !defined(__ICC))
  return __builtin_bswap32(value);
#elif defined(_MSC_VER) && !defined(_DEBUG)
  return _byteswap_ulong(value);
#else
  uint32_t Byte0 = value & 0x000000FF;
  uint32_t Byte1 = value & 0x0000FF00;
  uint32_t Byte2 = value & 0x00FF0000;
  uint32_t Byte3 = value & 0xFF000000;
  return (Byte0 << 24) | (Byte1 << 8) | (Byte2 >> 8) | (Byte3 >> 24);
#endif
}

/// This function returns a byte-swapped representation of the 64-bit argument.
inline uint64_t ByteSwap_64(uint64_t value) {
#if defined(__llvm__) || (defined(__GNUC__) && !defined(__ICC))
  return __builtin_bswap64(value);
#elif defined(_MSC_VER) && !defined(_DEBUG)
  return _byteswap_uint64(value);
#else
  uint64_t Hi = ByteSwap_32(uint32_t(value));
  uint32_t Lo = ByteSwap_32(uint32_t(value >> 32));
  return (Hi << 32) | Lo;
#endif
}

#if defined(BYTE_ORDER) && defined(BIG_ENDIAN) && BYTE_ORDER == BIG_ENDIAN
constexpr bool IsBigEndianHost = true;
#else
constexpr bool IsBigEndianHost = false;
#endif

static const bool IsLittleEndianHost = !IsBigEndianHost;

inline unsigned char getSwappedBytes(unsigned char C) { return C; }
inline signed char getSwappedBytes(signed char C) { return C; }
inline char getSwappedBytes(char C) { return C; }

inline unsigned short getSwappedBytes(unsigned short C) {
  return ByteSwap_16(C);
}
inline signed short getSwappedBytes(signed short C) { return ByteSwap_16(C); }

inline unsigned int getSwappedBytes(unsigned int C) { return ByteSwap_32(C); }
inline signed int getSwappedBytes(signed int C) { return ByteSwap_32(C); }

inline unsigned long getSwappedBytes(unsigned long C) {
  // Handle LLP64 and LP64 platforms.
  return sizeof(long) == sizeof(int) ? ByteSwap_32((uint32_t)C)
                                     : ByteSwap_64((uint64_t)C);
}
inline signed long getSwappedBytes(signed long C) {
  // Handle LLP64 and LP64 platforms.
  return sizeof(long) == sizeof(int) ? ByteSwap_32((uint32_t)C)
                                     : ByteSwap_64((uint64_t)C);
}

inline unsigned long long getSwappedBytes(unsigned long long C) {
  return ByteSwap_64(C);
}
inline signed long long getSwappedBytes(signed long long C) {
  return ByteSwap_64(C);
}

template <typename T>
inline std::enable_if_t<std::is_enum<T>::value, T> getSwappedBytes(T C) {
  return static_cast<T>(
      getSwappedBytes(static_cast<std::underlying_type_t<T>>(C)));
}

template <typename T> inline void swapByteOrder(T &Value) {
  Value = getSwappedBytes(Value);
}

} // end namespace __orc_rt

#endif // ORC_RT_ENDIAN_H
