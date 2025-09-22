//===- llvm/CodeGen/PBQPRAConstraint.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PBQPBuilder interface, for classes which build PBQP
// instances to represent register allocation problems, and the RegAllocPBQP
// interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQPRACONSTRAINT_H
#define LLVM_CODEGEN_PBQPRACONSTRAINT_H

#include <algorithm>
#include <memory>
#include <vector>

namespace llvm {

namespace PBQP {
namespace RegAlloc {

// Forward declare PBQP graph class.
class PBQPRAGraph;

} // end namespace RegAlloc
} // end namespace PBQP

using PBQPRAGraph = PBQP::RegAlloc::PBQPRAGraph;

/// Abstract base for classes implementing PBQP register allocation
///        constraints (e.g. Spill-costs, interference, coalescing).
class PBQPRAConstraint {
public:
  virtual ~PBQPRAConstraint() = 0;
  virtual void apply(PBQPRAGraph &G) = 0;

private:
  virtual void anchor();
};

/// PBQP register allocation constraint composer.
///
///   Constraints added to this list will be applied, in the order that they are
/// added, to the PBQP graph.
class PBQPRAConstraintList : public PBQPRAConstraint {
public:
  void apply(PBQPRAGraph &G) override {
    for (auto &C : Constraints)
      C->apply(G);
  }

  void addConstraint(std::unique_ptr<PBQPRAConstraint> C) {
    if (C)
      Constraints.push_back(std::move(C));
  }

private:
  std::vector<std::unique_ptr<PBQPRAConstraint>> Constraints;

  void anchor() override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PBQPRACONSTRAINT_H
