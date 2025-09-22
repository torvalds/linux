//===- llvm/ADT/SmallVectorExtras.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines less commonly used SmallVector utilities.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLVECTOREXTRAS_H
#define LLVM_ADT_SMALLVECTOREXTRAS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

/// Map a range to a SmallVector with element types deduced from the mapping.
template <unsigned Size, class ContainerTy, class FuncTy>
auto map_to_vector(ContainerTy &&C, FuncTy &&F) {
  return to_vector<Size>(
      map_range(std::forward<ContainerTy>(C), std::forward<FuncTy>(F)));
}
template <class ContainerTy, class FuncTy>
auto map_to_vector(ContainerTy &&C, FuncTy &&F) {
  return to_vector(
      map_range(std::forward<ContainerTy>(C), std::forward<FuncTy>(F)));
}

} // namespace llvm

#endif // LLVM_ADT_SMALLVECTOREXTRAS_H