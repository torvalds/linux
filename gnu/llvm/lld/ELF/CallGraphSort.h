//===- CallGraphSort.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_CALL_GRAPH_SORT_H
#define LLD_ELF_CALL_GRAPH_SORT_H

#include "llvm/ADT/DenseMap.h"

namespace lld::elf {
class InputSectionBase;

llvm::DenseMap<const InputSectionBase *, int> computeCacheDirectedSortOrder();

llvm::DenseMap<const InputSectionBase *, int> computeCallGraphProfileOrder();
} // namespace lld::elf

#endif
