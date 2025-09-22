//===-- sanitizer_leb128.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_LEB128_H
#define SANITIZER_LEB128_H

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"

namespace __sanitizer {

template <typename T, typename It>
It EncodeSLEB128(T value, It begin, It end) {
  bool more;
  do {
    u8 byte = value & 0x7f;
    // NOTE: this assumes that this signed shift is an arithmetic right shift.
    value >>= 7;
    more = !((((value == 0) && ((byte & 0x40) == 0)) ||
              ((value == -1) && ((byte & 0x40) != 0))));
    if (more)
      byte |= 0x80;
    if (UNLIKELY(begin == end))
      break;
    *(begin++) = byte;
  } while (more);
  return begin;
}

template <typename T, typename It>
It DecodeSLEB128(It begin, It end, T* v) {
  T value = 0;
  unsigned shift = 0;
  u8 byte;
  do {
    if (UNLIKELY(begin == end))
      return begin;
    byte = *(begin++);
    T slice = byte & 0x7f;
    value |= slice << shift;
    shift += 7;
  } while (byte >= 128);
  if (shift < 64 && (byte & 0x40))
    value |= (-1ULL) << shift;
  *v = value;
  return begin;
}

template <typename T, typename It>
It EncodeULEB128(T value, It begin, It end) {
  do {
    u8 byte = value & 0x7f;
    value >>= 7;
    if (value)
      byte |= 0x80;
    if (UNLIKELY(begin == end))
      break;
    *(begin++) = byte;
  } while (value);
  return begin;
}

template <typename T, typename It>
It DecodeULEB128(It begin, It end, T* v) {
  T value = 0;
  unsigned shift = 0;
  u8 byte;
  do {
    if (UNLIKELY(begin == end))
      return begin;
    byte = *(begin++);
    T slice = byte & 0x7f;
    value += slice << shift;
    shift += 7;
  } while (byte >= 128);
  *v = value;
  return begin;
}

}  // namespace __sanitizer

#endif  // SANITIZER_LEB128_H
