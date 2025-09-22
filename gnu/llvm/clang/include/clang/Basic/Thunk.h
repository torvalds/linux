//===----- Thunk.h - Declarations related to VTable Thunks ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Enums/classes describing THUNK related information about constructors,
/// destructors and thunks.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_THUNK_H
#define LLVM_CLANG_BASIC_THUNK_H

#include <cstdint>
#include <cstring>

namespace clang {

class CXXMethodDecl;
class Type;

/// A return adjustment.
struct ReturnAdjustment {
  /// The non-virtual adjustment from the derived object to its
  /// nearest virtual base.
  int64_t NonVirtual = 0;

  /// Holds the ABI-specific information about the virtual return
  /// adjustment, if needed.
  union VirtualAdjustment {
    // Itanium ABI
    struct {
      /// The offset (in bytes), relative to the address point
      /// of the virtual base class offset.
      int64_t VBaseOffsetOffset;
    } Itanium;

    // Microsoft ABI
    struct {
      /// The offset (in bytes) of the vbptr, relative to the beginning
      /// of the derived class.
      uint32_t VBPtrOffset;

      /// Index of the virtual base in the vbtable.
      uint32_t VBIndex;
    } Microsoft;

    VirtualAdjustment() { memset(this, 0, sizeof(*this)); }

    bool Equals(const VirtualAdjustment &Other) const {
      return memcmp(this, &Other, sizeof(Other)) == 0;
    }

    bool isEmpty() const {
      VirtualAdjustment Zero;
      return Equals(Zero);
    }

    bool Less(const VirtualAdjustment &RHS) const {
      return memcmp(this, &RHS, sizeof(RHS)) < 0;
    }
  } Virtual;

  ReturnAdjustment() = default;

  bool isEmpty() const { return !NonVirtual && Virtual.isEmpty(); }

  friend bool operator==(const ReturnAdjustment &LHS,
                         const ReturnAdjustment &RHS) {
    return LHS.NonVirtual == RHS.NonVirtual && LHS.Virtual.Equals(RHS.Virtual);
  }

  friend bool operator!=(const ReturnAdjustment &LHS,
                         const ReturnAdjustment &RHS) {
    return !(LHS == RHS);
  }

  friend bool operator<(const ReturnAdjustment &LHS,
                        const ReturnAdjustment &RHS) {
    if (LHS.NonVirtual < RHS.NonVirtual)
      return true;

    return LHS.NonVirtual == RHS.NonVirtual && LHS.Virtual.Less(RHS.Virtual);
  }
};

/// A \c this pointer adjustment.
struct ThisAdjustment {
  /// The non-virtual adjustment from the derived object to its
  /// nearest virtual base.
  int64_t NonVirtual = 0;

  /// Holds the ABI-specific information about the virtual this
  /// adjustment, if needed.
  union VirtualAdjustment {
    // Itanium ABI
    struct {
      /// The offset (in bytes), relative to the address point,
      /// of the virtual call offset.
      int64_t VCallOffsetOffset;
    } Itanium;

    struct {
      /// The offset of the vtordisp (in bytes), relative to the ECX.
      int32_t VtordispOffset;

      /// The offset of the vbptr of the derived class (in bytes),
      /// relative to the ECX after vtordisp adjustment.
      int32_t VBPtrOffset;

      /// The offset (in bytes) of the vbase offset in the vbtable.
      int32_t VBOffsetOffset;
    } Microsoft;

    VirtualAdjustment() { memset(this, 0, sizeof(*this)); }

    bool Equals(const VirtualAdjustment &Other) const {
      return memcmp(this, &Other, sizeof(Other)) == 0;
    }

    bool isEmpty() const {
      VirtualAdjustment Zero;
      return Equals(Zero);
    }

    bool Less(const VirtualAdjustment &RHS) const {
      return memcmp(this, &RHS, sizeof(RHS)) < 0;
    }
  } Virtual;

  ThisAdjustment() = default;

  bool isEmpty() const { return !NonVirtual && Virtual.isEmpty(); }

  friend bool operator==(const ThisAdjustment &LHS, const ThisAdjustment &RHS) {
    return LHS.NonVirtual == RHS.NonVirtual && LHS.Virtual.Equals(RHS.Virtual);
  }

  friend bool operator!=(const ThisAdjustment &LHS, const ThisAdjustment &RHS) {
    return !(LHS == RHS);
  }

  friend bool operator<(const ThisAdjustment &LHS, const ThisAdjustment &RHS) {
    if (LHS.NonVirtual < RHS.NonVirtual)
      return true;

    return LHS.NonVirtual == RHS.NonVirtual && LHS.Virtual.Less(RHS.Virtual);
  }
};

/// The \c this pointer adjustment as well as an optional return
/// adjustment for a thunk.
struct ThunkInfo {
  /// The \c this pointer adjustment.
  ThisAdjustment This;

  /// The return adjustment.
  ReturnAdjustment Return;

  /// Holds a pointer to the overridden method this thunk is for,
  /// if needed by the ABI to distinguish different thunks with equal
  /// adjustments.
  /// In the Itanium ABI, this field can hold the method that created the
  /// vtable entry for this thunk.
  /// Otherwise, null.
  /// CAUTION: In the unlikely event you need to sort ThunkInfos, consider using
  /// an ABI-specific comparator.
  const CXXMethodDecl *Method;
  const Type *ThisType;

  ThunkInfo() : Method(nullptr), ThisType(nullptr) {}

  ThunkInfo(const ThisAdjustment &This, const ReturnAdjustment &Return,
            const Type *ThisT, const CXXMethodDecl *Method = nullptr)
      : This(This), Return(Return), Method(Method), ThisType(ThisT) {}

  friend bool operator==(const ThunkInfo &LHS, const ThunkInfo &RHS) {
    return LHS.This == RHS.This && LHS.Return == RHS.Return &&
           LHS.Method == RHS.Method && LHS.ThisType == RHS.ThisType;
  }

  bool isEmpty() const {
    return This.isEmpty() && Return.isEmpty() && Method == nullptr;
  }
};

} // end namespace clang

#endif
