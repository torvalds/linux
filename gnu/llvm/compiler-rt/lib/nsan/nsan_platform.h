//===------------------------ nsan_platform.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Platform specific information for NSan.
//
//===----------------------------------------------------------------------===//

#ifndef NSAN_PLATFORM_H
#define NSAN_PLATFORM_H

namespace __nsan {

// NSan uses two regions of memory to store information:
// - 'shadow memory' stores the shadow copies of numerical values stored in
//   application memory.
// - 'shadow types' is used to determine which value type each byte of memory
//   belongs to. This makes sure that we always know whether a shadow value is
//   valid. Shadow values may be tampered with using access through other
//   pointer types (type punning). Each byte stores:
//     - bit 1-0: whether the corresponding value is of unknown (00),
//       float (01), double (10), or long double (11) type.
//     - bit 5-2: the index of this byte in the value, or 0000 if type is
//       unknown.
//       This allows handling unaligned loat load/stores by checking that a load
//       with a given alignment corresponds to the alignment of the store.
//       Any store of a non-floating point type invalidates the corresponding
//       bytes, so that subsequent overlapping loads (aligned or not) know that
//       the corresponding shadow value is no longer valid.

// On Linux/x86_64, memory is laid out as follows:
//
// +--------------------+ 0x800000000000 (top of memory)
// | application memory |
// +--------------------+ 0x700000008000 (kAppAddr)
// |                    |
// |       unused       |
// |                    |
// +--------------------+ 0x400000000000 (kUnusedAddr)
// |   shadow memory    |
// +--------------------+ 0x200000000000 (kShadowAddr)
// |   shadow types     |
// +--------------------+ 0x100000000000 (kTypesAddr)
// | reserved by kernel |
// +--------------------+ 0x000000000000
//
//
// To derive a shadow memory address from an application memory address,
// bits 44-46 are cleared to bring the address into the range
// [0x000000000000,0x100000000000).  We scale to account for the fact that a
// shadow value takes twice as much space as the original value.
// Then we add kShadowAddr to put the shadow relative offset into the shadow
// memory. See getShadowAddrFor().
// The process is similar for the shadow types.

// The ratio of app to shadow memory.
enum { kShadowScale = 2 };

// The original value type of a byte in app memory. Uses LLVM terminology:
// https://llvm.org/docs/LangRef.html#floating-point-types
// FIXME: support half and bfloat.
enum ValueType {
  kUnknownValueType = 0,
  kFloatValueType = 1,  // LLVM float, shadow type double.
  kDoubleValueType = 2, // LLVM double, shadow type fp128.
  kFp80ValueType = 3,   // LLVM x86_fp80, shadow type fp128.
};

// The size of ValueType encoding, in bits.
enum {
  kValueSizeSizeBits = 2,
};

#if defined(__x86_64__)
struct Mapping {
  // FIXME: kAppAddr == 0x700000000000 ?
  static const uptr kAppAddr = 0x700000008000;
  static const uptr kUnusedAddr = 0x400000000000;
  static const uptr kShadowAddr = 0x200000000000;
  static const uptr kTypesAddr = 0x100000000000;
  static const uptr kShadowMask = ~0x700000000000;
};
#else
#error "NSan not supported for this platform!"
#endif

enum MappingType {
  MAPPING_APP_ADDR,
  MAPPING_UNUSED_ADDR,
  MAPPING_SHADOW_ADDR,
  MAPPING_TYPES_ADDR,
  MAPPING_SHADOW_MASK
};

template <typename Mapping, int Type> uptr MappingImpl() {
  switch (Type) {
  case MAPPING_APP_ADDR:
    return Mapping::kAppAddr;
  case MAPPING_UNUSED_ADDR:
    return Mapping::kUnusedAddr;
  case MAPPING_SHADOW_ADDR:
    return Mapping::kShadowAddr;
  case MAPPING_TYPES_ADDR:
    return Mapping::kTypesAddr;
  case MAPPING_SHADOW_MASK:
    return Mapping::kShadowMask;
  }
}

template <int Type> uptr MappingArchImpl() {
  return MappingImpl<Mapping, Type>();
}

ALWAYS_INLINE
uptr AppAddr() { return MappingArchImpl<MAPPING_APP_ADDR>(); }

ALWAYS_INLINE
uptr UnusedAddr() { return MappingArchImpl<MAPPING_UNUSED_ADDR>(); }

ALWAYS_INLINE
uptr ShadowAddr() { return MappingArchImpl<MAPPING_SHADOW_ADDR>(); }

ALWAYS_INLINE
uptr TypesAddr() { return MappingArchImpl<MAPPING_TYPES_ADDR>(); }

ALWAYS_INLINE
uptr ShadowMask() { return MappingArchImpl<MAPPING_SHADOW_MASK>(); }

} // end namespace __nsan

#endif
