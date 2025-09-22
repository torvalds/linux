//===- AsmCond.h - Assembly file conditional assembly  ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_ASMCOND_H
#define LLVM_MC_MCPARSER_ASMCOND_H

namespace llvm {

/// AsmCond - Class to support conditional assembly
///
/// The conditional assembly feature (.if, .else, .elseif and .endif) is
/// implemented with AsmCond that tells us what we are in the middle of
/// processing.  Ignore can be either true or false.  When true we are ignoring
/// the block of code in the middle of a conditional.

class AsmCond {
public:
  enum ConditionalAssemblyType {
    NoCond,     // no conditional is being processed
    IfCond,     // inside if conditional
    ElseIfCond, // inside elseif conditional
    ElseCond    // inside else conditional
  };

  ConditionalAssemblyType TheCond = NoCond;
  bool CondMet = false;
  bool Ignore = false;
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_ASMCOND_H
