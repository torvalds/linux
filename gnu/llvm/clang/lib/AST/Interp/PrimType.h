//===--- PrimType.h - Types for the constexpr VM ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the VM types and helpers operating on types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_TYPE_H
#define LLVM_CLANG_AST_INTERP_TYPE_H

#include "llvm/Support/raw_ostream.h"
#include <climits>
#include <cstddef>
#include <cstdint>

namespace clang {
namespace interp {

class Pointer;
class Boolean;
class Floating;
class FunctionPointer;
class MemberPointer;
template <bool Signed> class IntegralAP;
template <unsigned Bits, bool Signed> class Integral;

/// Enumeration of the primitive types of the VM.
enum PrimType : unsigned {
  PT_Sint8 = 0,
  PT_Uint8 = 1,
  PT_Sint16 = 2,
  PT_Uint16 = 3,
  PT_Sint32 = 4,
  PT_Uint32 = 5,
  PT_Sint64 = 6,
  PT_Uint64 = 7,
  PT_IntAP = 8,
  PT_IntAPS = 9,
  PT_Bool = 10,
  PT_Float = 11,
  PT_Ptr = 12,
  PT_FnPtr = 13,
  PT_MemberPtr = 14,
};

inline constexpr bool isPtrType(PrimType T) {
  return T == PT_Ptr || T == PT_FnPtr || T == PT_MemberPtr;
}

enum class CastKind : uint8_t {
  Reinterpret,
  Atomic,
};
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     interp::CastKind CK) {
  switch (CK) {
  case interp::CastKind::Reinterpret:
    OS << "reinterpret_cast";
    break;
  case interp::CastKind::Atomic:
    OS << "atomic";
    break;
  }
  return OS;
}

constexpr bool isIntegralType(PrimType T) { return T <= PT_Bool; }

/// Mapping from primitive types to their representation.
template <PrimType T> struct PrimConv;
template <> struct PrimConv<PT_Sint8> { using T = Integral<8, true>; };
template <> struct PrimConv<PT_Uint8> { using T = Integral<8, false>; };
template <> struct PrimConv<PT_Sint16> { using T = Integral<16, true>; };
template <> struct PrimConv<PT_Uint16> { using T = Integral<16, false>; };
template <> struct PrimConv<PT_Sint32> { using T = Integral<32, true>; };
template <> struct PrimConv<PT_Uint32> { using T = Integral<32, false>; };
template <> struct PrimConv<PT_Sint64> { using T = Integral<64, true>; };
template <> struct PrimConv<PT_Uint64> { using T = Integral<64, false>; };
template <> struct PrimConv<PT_IntAP> {
  using T = IntegralAP<false>;
};
template <> struct PrimConv<PT_IntAPS> {
  using T = IntegralAP<true>;
};
template <> struct PrimConv<PT_Float> { using T = Floating; };
template <> struct PrimConv<PT_Bool> { using T = Boolean; };
template <> struct PrimConv<PT_Ptr> { using T = Pointer; };
template <> struct PrimConv<PT_FnPtr> {
  using T = FunctionPointer;
};
template <> struct PrimConv<PT_MemberPtr> {
  using T = MemberPointer;
};

/// Returns the size of a primitive type in bytes.
size_t primSize(PrimType Type);

/// Aligns a size to the pointer alignment.
constexpr size_t align(size_t Size) {
  return ((Size + alignof(void *) - 1) / alignof(void *)) * alignof(void *);
}

constexpr bool aligned(uintptr_t Value) { return Value == align(Value); }
static_assert(aligned(sizeof(void *)));

static inline bool aligned(const void *P) {
  return aligned(reinterpret_cast<uintptr_t>(P));
}

} // namespace interp
} // namespace clang

/// Helper macro to simplify type switches.
/// The macro implicitly exposes a type T in the scope of the inner block.
#define TYPE_SWITCH_CASE(Name, B) \
  case Name: { using T = PrimConv<Name>::T; B; break; }
#define TYPE_SWITCH(Expr, B)                                                   \
  do {                                                                         \
    switch (Expr) {                                                            \
      TYPE_SWITCH_CASE(PT_Sint8, B)                                            \
      TYPE_SWITCH_CASE(PT_Uint8, B)                                            \
      TYPE_SWITCH_CASE(PT_Sint16, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint16, B)                                           \
      TYPE_SWITCH_CASE(PT_Sint32, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint32, B)                                           \
      TYPE_SWITCH_CASE(PT_Sint64, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint64, B)                                           \
      TYPE_SWITCH_CASE(PT_IntAP, B)                                            \
      TYPE_SWITCH_CASE(PT_IntAPS, B)                                           \
      TYPE_SWITCH_CASE(PT_Float, B)                                            \
      TYPE_SWITCH_CASE(PT_Bool, B)                                             \
      TYPE_SWITCH_CASE(PT_Ptr, B)                                              \
      TYPE_SWITCH_CASE(PT_FnPtr, B)                                            \
      TYPE_SWITCH_CASE(PT_MemberPtr, B)                                        \
    }                                                                          \
  } while (0)

#define INT_TYPE_SWITCH(Expr, B)                                               \
  do {                                                                         \
    switch (Expr) {                                                            \
      TYPE_SWITCH_CASE(PT_Sint8, B)                                            \
      TYPE_SWITCH_CASE(PT_Uint8, B)                                            \
      TYPE_SWITCH_CASE(PT_Sint16, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint16, B)                                           \
      TYPE_SWITCH_CASE(PT_Sint32, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint32, B)                                           \
      TYPE_SWITCH_CASE(PT_Sint64, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint64, B)                                           \
      TYPE_SWITCH_CASE(PT_IntAP, B)                                            \
      TYPE_SWITCH_CASE(PT_IntAPS, B)                                           \
      TYPE_SWITCH_CASE(PT_Bool, B)                                             \
    default:                                                                   \
      llvm_unreachable("Not an integer value");                                \
    }                                                                          \
  } while (0)

#define INT_TYPE_SWITCH_NO_BOOL(Expr, B)                                       \
  do {                                                                         \
    switch (Expr) {                                                            \
      TYPE_SWITCH_CASE(PT_Sint8, B)                                            \
      TYPE_SWITCH_CASE(PT_Uint8, B)                                            \
      TYPE_SWITCH_CASE(PT_Sint16, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint16, B)                                           \
      TYPE_SWITCH_CASE(PT_Sint32, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint32, B)                                           \
      TYPE_SWITCH_CASE(PT_Sint64, B)                                           \
      TYPE_SWITCH_CASE(PT_Uint64, B)                                           \
      TYPE_SWITCH_CASE(PT_IntAP, B)                                            \
      TYPE_SWITCH_CASE(PT_IntAPS, B)                                           \
    default:                                                                   \
      llvm_unreachable("Not an integer value");                                \
    }                                                                          \
  } while (0)

#define COMPOSITE_TYPE_SWITCH(Expr, B, D)                                      \
  do {                                                                         \
    switch (Expr) {                                                            \
      TYPE_SWITCH_CASE(PT_Ptr, B)                                              \
      default: { D; break; }                                                   \
    }                                                                          \
  } while (0)
#endif
