//===- GenericSSAContext.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the little GenericSSAContext<X> template class
/// that can be used to implement IR analyses as templates.
/// Specializing these templates allows the analyses to be used over
/// both LLVM IR and Machine IR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICSSACONTEXT_H
#define LLVM_ADT_GENERICSSACONTEXT_H

#include "llvm/Support/Printable.h"

namespace llvm {

template <typename, bool> class DominatorTreeBase;
template <typename> class SmallVectorImpl;

namespace Intrinsic {
typedef unsigned ID;
}

// Specializations of this template should provide the types used by the
// template GenericSSAContext below.
template <typename _FunctionT> struct GenericSSATraits;

// Ideally this should have been a stateless traits class. But the print methods
// for Machine IR need access to the owning function. So we track that state in
// the template itself.
//
// We use FunctionT as a template argument and not GenericSSATraits to allow
// forward declarations using well-known typenames.
template <typename _FunctionT> class GenericSSAContext {
  using SSATraits = GenericSSATraits<_FunctionT>;
  const typename SSATraits::FunctionT *F;

public:
  // The smallest unit of the IR is a ValueT. The SSA context uses a ValueRefT,
  // which is a pointer to a ValueT, since Machine IR does not have the
  // equivalent of a ValueT.
  using ValueRefT = typename SSATraits::ValueRefT;

  // The ConstValueRefT is needed to work with "const Value *", where const
  // needs to bind to the pointee and not the pointer.
  using ConstValueRefT = typename SSATraits::ConstValueRefT;

  // The null value for ValueRefT. For LLVM IR and MIR, this is simply the
  // default constructed value.
  static constexpr ValueRefT *ValueRefNull = {};

  // An InstructionT usually defines one or more ValueT objects.
  using InstructionT = typename SSATraits::InstructionT;

  // A UseT represents a data-edge from the defining instruction to the using
  // instruction.
  using UseT = typename SSATraits::UseT;

  // A BlockT is a sequence of InstructionT, and forms a node of the CFG. It
  // has global methods predecessors() and successors() that return
  // the list of incoming CFG edges and outgoing CFG edges
  // respectively.
  using BlockT = typename SSATraits::BlockT;

  // A FunctionT represents a CFG along with arguments and return values. It is
  // the smallest complete unit of code in a Module.
  using FunctionT = typename SSATraits::FunctionT;

  // A dominator tree provides the dominance relation between basic blocks in
  // a given funciton.
  using DominatorTreeT = DominatorTreeBase<BlockT, false>;

  GenericSSAContext() = default;
  GenericSSAContext(const FunctionT *F) : F(F) {}

  const FunctionT *getFunction() const { return F; }

  static Intrinsic::ID getIntrinsicID(const InstructionT &I);

  static void appendBlockDefs(SmallVectorImpl<ValueRefT> &defs, BlockT &block);
  static void appendBlockDefs(SmallVectorImpl<ConstValueRefT> &defs,
                              const BlockT &block);

  static void appendBlockTerms(SmallVectorImpl<InstructionT *> &terms,
                               BlockT &block);
  static void appendBlockTerms(SmallVectorImpl<const InstructionT *> &terms,
                               const BlockT &block);

  static bool isConstantOrUndefValuePhi(const InstructionT &Instr);
  const BlockT *getDefBlock(ConstValueRefT value) const;

  Printable print(const BlockT *block) const;
  Printable printAsOperand(const BlockT *BB) const;
  Printable print(const InstructionT *inst) const;
  Printable print(ConstValueRefT value) const;
};
} // namespace llvm

#endif // LLVM_ADT_GENERICSSACONTEXT_H
