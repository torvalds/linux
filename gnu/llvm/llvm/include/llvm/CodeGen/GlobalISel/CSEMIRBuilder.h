//===-- llvm/CodeGen/GlobalISel/CSEMIRBuilder.h  --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements a version of MachineIRBuilder which CSEs insts within
/// a MachineBasicBlock.
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_GLOBALISEL_CSEMIRBUILDER_H
#define LLVM_CODEGEN_GLOBALISEL_CSEMIRBUILDER_H

#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"

namespace llvm {

class GISelInstProfileBuilder;
/// Defines a builder that does CSE of MachineInstructions using GISelCSEInfo.
/// Eg usage.
///
///
/// GISelCSEInfo *Info =
/// &getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEInfo(); CSEMIRBuilder
/// CB(Builder.getState()); CB.setCSEInfo(Info); auto A = CB.buildConstant(s32,
/// 42); auto B = CB.buildConstant(s32, 42); assert(A == B); unsigned CReg =
/// MRI.createGenericVirtualRegister(s32); auto C = CB.buildConstant(CReg, 42);
/// assert(C->getOpcode() == TargetOpcode::COPY);
/// Explicitly passing in a register would materialize a copy if possible.
/// CSEMIRBuilder also does trivial constant folding for binary ops.
class CSEMIRBuilder : public MachineIRBuilder {

  /// Returns true if A dominates B (within the same basic block).
  /// Both iterators must be in the same basic block.
  //
  // TODO: Another approach for checking dominance is having two iterators and
  // making them go towards each other until they meet or reach begin/end. Which
  // approach is better? Should this even change dynamically? For G_CONSTANTS
  // most of which will be at the top of the BB, the top down approach would be
  // a better choice. Does IRTranslator placing constants at the beginning still
  // make sense? Should this change based on Opcode?
  bool dominates(MachineBasicBlock::const_iterator A,
                 MachineBasicBlock::const_iterator B) const;

  /// For given ID, find a machineinstr in the CSE Map. If found, check if it
  /// dominates the current insertion point and if not, move it just before the
  /// current insertion point and return it. If not found, return Null
  /// MachineInstrBuilder.
  MachineInstrBuilder getDominatingInstrForID(FoldingSetNodeID &ID,
                                              void *&NodeInsertPos);
  /// Simple check if we can CSE (we have the CSEInfo) or if this Opcode is
  /// safe to CSE.
  bool canPerformCSEForOpc(unsigned Opc) const;

  void profileDstOp(const DstOp &Op, GISelInstProfileBuilder &B) const;

  void profileDstOps(ArrayRef<DstOp> Ops, GISelInstProfileBuilder &B) const {
    for (const DstOp &Op : Ops)
      profileDstOp(Op, B);
  }

  void profileSrcOp(const SrcOp &Op, GISelInstProfileBuilder &B) const;

  void profileSrcOps(ArrayRef<SrcOp> Ops, GISelInstProfileBuilder &B) const {
    for (const SrcOp &Op : Ops)
      profileSrcOp(Op, B);
  }

  void profileMBBOpcode(GISelInstProfileBuilder &B, unsigned Opc) const;

  void profileEverything(unsigned Opc, ArrayRef<DstOp> DstOps,
                         ArrayRef<SrcOp> SrcOps, std::optional<unsigned> Flags,
                         GISelInstProfileBuilder &B) const;

  // Takes a MachineInstrBuilder and inserts it into the CSEMap using the
  // NodeInsertPos.
  MachineInstrBuilder memoizeMI(MachineInstrBuilder MIB, void *NodeInsertPos);

  // If we have can CSE an instruction, but still need to materialize to a VReg,
  // we emit a copy from the CSE'd inst to the VReg.
  MachineInstrBuilder generateCopiesIfRequired(ArrayRef<DstOp> DstOps,
                                               MachineInstrBuilder &MIB);

  // If we have can CSE an instruction, but still need to materialize to a VReg,
  // check if we can generate copies. It's not possible to return a single MIB,
  // while emitting copies to multiple vregs.
  bool checkCopyToDefsPossible(ArrayRef<DstOp> DstOps);

public:
  // Pull in base class constructors.
  using MachineIRBuilder::MachineIRBuilder;
  // Unhide buildInstr
  MachineInstrBuilder
  buildInstr(unsigned Opc, ArrayRef<DstOp> DstOps, ArrayRef<SrcOp> SrcOps,
             std::optional<unsigned> Flag = std::nullopt) override;
  // Bring in the other overload from the base class.
  using MachineIRBuilder::buildConstant;

  MachineInstrBuilder buildConstant(const DstOp &Res,
                                    const ConstantInt &Val) override;

  // Bring in the other overload from the base class.
  using MachineIRBuilder::buildFConstant;
  MachineInstrBuilder buildFConstant(const DstOp &Res,
                                     const ConstantFP &Val) override;
};
} // namespace llvm
#endif
