//=- llvm/LTO/SummaryBasedOptimizations.h -Link time optimizations-*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_SUMMARYBASEDOPTIMIZATIONS_H
#define LLVM_LTO_SUMMARYBASEDOPTIMIZATIONS_H
namespace llvm {
class ModuleSummaryIndex;

/// Compute synthetic function entry counts.
void computeSyntheticCounts(ModuleSummaryIndex &Index);

} // namespace llvm
#endif
