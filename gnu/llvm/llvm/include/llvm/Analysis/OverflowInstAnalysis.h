//===-- OverflowInstAnalysis.h - Utils to fold overflow insts ----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file holds routines to help analyse overflow instructions
// and fold them into constants or other overflow instructions
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OVERFLOWINSTANALYSIS_H
#define LLVM_ANALYSIS_OVERFLOWINSTANALYSIS_H

namespace llvm {
class Use;
class Value;

/// Match one of the patterns up to the select/logic op:
///   %Op0 = icmp ne i4 %X, 0
///   %Agg = call { i4, i1 } @llvm.[us]mul.with.overflow.i4(i4 %X, i4 %Y)
///   %Op1 = extractvalue { i4, i1 } %Agg, 1
///   %ret = select i1 %Op0, i1 %Op1, i1 false / %ret = and i1 %Op0, %Op1
///
///   %Op0 = icmp eq i4 %X, 0
///   %Agg = call { i4, i1 } @llvm.[us]mul.with.overflow.i4(i4 %X, i4 %Y)
///   %NotOp1 = extractvalue { i4, i1 } %Agg, 1
///   %Op1 = xor i1 %NotOp1, true
///   %ret = select i1 %Op0, i1 true, i1 %Op1 / %ret = or i1 %Op0, %Op1
///
/// Callers are expected to align that with the operands of the select/logic.
/// IsAnd is set to true if the Op0 and Op1 are used as the first pattern.
/// If Op0 and Op1 match one of the patterns above, return true and fill Y's
/// use.

bool isCheckForZeroAndMulWithOverflow(Value *Op0, Value *Op1, bool IsAnd,
                                      Use *&Y);
bool isCheckForZeroAndMulWithOverflow(Value *Op0, Value *Op1, bool IsAnd);
} // end namespace llvm

#endif
