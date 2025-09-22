//===--- AlignOf.h - Portable calculation of type alignment -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the AlignedCharArrayUnion class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ALIGNOF_H
#define LLVM_SUPPORT_ALIGNOF_H

#include <type_traits>

namespace llvm {

/// A suitably aligned and sized character array member which can hold elements
/// of any type.
///
/// This template is equivalent to std::aligned_union_t<1, ...>, but we cannot
/// use it due to a bug in the MSVC x86 compiler:
/// https://github.com/microsoft/STL/issues/1533
/// Using `alignas` here works around the bug.
template <typename T, typename... Ts> struct AlignedCharArrayUnion {
  using AlignedUnion = std::aligned_union_t<1, T, Ts...>;
  alignas(alignof(AlignedUnion)) char buffer[sizeof(AlignedUnion)];
};

} // end namespace llvm

#endif // LLVM_SUPPORT_ALIGNOF_H
