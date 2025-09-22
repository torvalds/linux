//==- AArch64PBQPRegAlloc.h - AArch64 specific PBQP constraints --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64PBQPREGALOC_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64PBQPREGALOC_H

#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/PBQPRAConstraint.h"

namespace llvm {

class TargetRegisterInfo;

/// Add the accumulator chaining constraint to a PBQP graph
class A57ChainingConstraint : public PBQPRAConstraint {
public:
  // Add A57 specific constraints to the PBQP graph.
  void apply(PBQPRAGraph &G) override;

private:
  SmallSetVector<unsigned, 32> Chains;
  const TargetRegisterInfo *TRI;

  // Add the accumulator chaining constraint, inside the chain, i.e. so that
  // parity(Rd) == parity(Ra).
  // \return true if a constraint was added
  bool addIntraChainConstraint(PBQPRAGraph &G, unsigned Rd, unsigned Ra);

  // Add constraints between existing chains
  void addInterChainConstraint(PBQPRAGraph &G, unsigned Rd, unsigned Ra);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AARCH64_AARCH64PBQPREGALOC_H
