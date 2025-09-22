//===- Local.h - Functions to perform local transformations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform various local transformations to the
// program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_UTILS_LOCAL_H
#define LLVM_ANALYSIS_UTILS_LOCAL_H

namespace llvm {

class DataLayout;
class IRBuilderBase;
class User;
class Value;

/// Given a getelementptr instruction/constantexpr, emit the code necessary to
/// compute the offset from the base pointer (without adding in the base
/// pointer). Return the result as a signed integer of intptr size.
/// When NoAssumptions is true, no assumptions about index computation not
/// overflowing is made.
Value *emitGEPOffset(IRBuilderBase *Builder, const DataLayout &DL, User *GEP,
                     bool NoAssumptions = false);

} // namespace llvm

#endif // LLVM_ANALYSIS_UTILS_LOCAL_H
