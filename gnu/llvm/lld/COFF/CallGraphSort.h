//===- CallGraphSort.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_CALL_GRAPH_SORT_H
#define LLD_COFF_CALL_GRAPH_SORT_H

#include "llvm/ADT/DenseMap.h"

namespace lld::coff {
class SectionChunk;
class COFFLinkerContext;

llvm::DenseMap<const SectionChunk *, int>
computeCallGraphProfileOrder(const COFFLinkerContext &ctx);
} // namespace lld::coff

#endif
