//===- llvm/CodeGen/MachineInstrBundle.h - MI bundle utilities --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provide utility functions to manipulate machine instruction
// bundles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTRBUNDLE_H
#define LLVM_CODEGEN_MACHINEINSTRBUNDLE_H

#include "llvm/CodeGen/MachineBasicBlock.h"

namespace llvm {

/// finalizeBundle - Finalize a machine instruction bundle which includes
/// a sequence of instructions starting from FirstMI to LastMI (exclusive).
/// This routine adds a BUNDLE instruction to represent the bundle, it adds
/// IsInternalRead markers to MachineOperands which are defined inside the
/// bundle, and it copies externally visible defs and uses to the BUNDLE
/// instruction.
void finalizeBundle(MachineBasicBlock &MBB,
                    MachineBasicBlock::instr_iterator FirstMI,
                    MachineBasicBlock::instr_iterator LastMI);

/// finalizeBundle - Same functionality as the previous finalizeBundle except
/// the last instruction in the bundle is not provided as an input. This is
/// used in cases where bundles are pre-determined by marking instructions
/// with 'InsideBundle' marker. It returns the MBB instruction iterator that
/// points to the end of the bundle.
MachineBasicBlock::instr_iterator finalizeBundle(MachineBasicBlock &MBB,
                    MachineBasicBlock::instr_iterator FirstMI);

/// finalizeBundles - Finalize instruction bundles in the specified
/// MachineFunction. Return true if any bundles are finalized.
bool finalizeBundles(MachineFunction &MF);

/// Returns an iterator to the first instruction in the bundle containing \p I.
inline MachineBasicBlock::instr_iterator getBundleStart(
    MachineBasicBlock::instr_iterator I) {
  while (I->isBundledWithPred())
    --I;
  return I;
}

/// Returns an iterator to the first instruction in the bundle containing \p I.
inline MachineBasicBlock::const_instr_iterator getBundleStart(
    MachineBasicBlock::const_instr_iterator I) {
  while (I->isBundledWithPred())
    --I;
  return I;
}

/// Returns an iterator pointing beyond the bundle containing \p I.
inline MachineBasicBlock::instr_iterator getBundleEnd(
    MachineBasicBlock::instr_iterator I) {
  while (I->isBundledWithSucc())
    ++I;
  ++I;
  return I;
}

/// Returns an iterator pointing beyond the bundle containing \p I.
inline MachineBasicBlock::const_instr_iterator getBundleEnd(
    MachineBasicBlock::const_instr_iterator I) {
  while (I->isBundledWithSucc())
    ++I;
  ++I;
  return I;
}

//===----------------------------------------------------------------------===//
// MachineBundleOperand iterator
//

/// MIBundleOperandIteratorBase - Iterator that visits all operands in a bundle
/// of MachineInstrs. This class is not intended to be used directly, use one
/// of the sub-classes instead.
///
/// Intended use:
///
///   for (MIBundleOperands MIO(MI); MIO.isValid(); ++MIO) {
///     if (!MIO->isReg())
///       continue;
///     ...
///   }
///
template <typename ValueT>
class MIBundleOperandIteratorBase
    : public iterator_facade_base<MIBundleOperandIteratorBase<ValueT>,
                                  std::forward_iterator_tag, ValueT> {
  MachineBasicBlock::instr_iterator InstrI, InstrE;
  MachineInstr::mop_iterator OpI, OpE;

  // If the operands on InstrI are exhausted, advance InstrI to the next
  // bundled instruction with operands.
  void advance() {
    while (OpI == OpE) {
      // Don't advance off the basic block, or into a new bundle.
      if (++InstrI == InstrE || !InstrI->isInsideBundle()) {
        InstrI = InstrE;
        break;
      }
      OpI = InstrI->operands_begin();
      OpE = InstrI->operands_end();
    }
  }

protected:
  /// MIBundleOperandIteratorBase - Create an iterator that visits all operands
  /// on MI, or all operands on every instruction in the bundle containing MI.
  ///
  /// @param MI The instruction to examine.
  ///
  explicit MIBundleOperandIteratorBase(MachineInstr &MI) {
    InstrI = getBundleStart(MI.getIterator());
    InstrE = MI.getParent()->instr_end();
    OpI = InstrI->operands_begin();
    OpE = InstrI->operands_end();
    advance();
  }

  /// Constructor for an iterator past the last iteration: both instruction
  /// iterators point to the end of the BB and OpI == OpE.
  explicit MIBundleOperandIteratorBase(MachineBasicBlock::instr_iterator InstrE,
                                       MachineInstr::mop_iterator OpE)
      : InstrI(InstrE), InstrE(InstrE), OpI(OpE), OpE(OpE) {}

public:
  /// isValid - Returns true until all the operands have been visited.
  bool isValid() const { return OpI != OpE; }

  /// Preincrement.  Move to the next operand.
  void operator++() {
    assert(isValid() && "Cannot advance MIOperands beyond the last operand");
    ++OpI;
    advance();
  }

  ValueT &operator*() const { return *OpI; }
  ValueT *operator->() const { return &*OpI; }

  bool operator==(const MIBundleOperandIteratorBase &Arg) const {
    // Iterators are equal, if InstrI matches and either OpIs match or OpI ==
    // OpE match for both. The second condition allows us to construct an 'end'
    // iterator, without finding the last instruction in a bundle up-front.
    return InstrI == Arg.InstrI &&
           (OpI == Arg.OpI || (OpI == OpE && Arg.OpI == Arg.OpE));
  }
  /// getOperandNo - Returns the number of the current operand relative to its
  /// instruction.
  ///
  unsigned getOperandNo() const {
    return OpI - InstrI->operands_begin();
  }
};

/// MIBundleOperands - Iterate over all operands in a bundle of machine
/// instructions.
///
class MIBundleOperands : public MIBundleOperandIteratorBase<MachineOperand> {
  /// Constructor for an iterator past the last iteration.
  MIBundleOperands(MachineBasicBlock::instr_iterator InstrE,
                   MachineInstr::mop_iterator OpE)
      : MIBundleOperandIteratorBase(InstrE, OpE) {}

public:
  MIBundleOperands(MachineInstr &MI) : MIBundleOperandIteratorBase(MI) {}

  /// Returns an iterator past the last iteration.
  static MIBundleOperands end(const MachineBasicBlock &MBB) {
    return {const_cast<MachineBasicBlock &>(MBB).instr_end(),
            const_cast<MachineBasicBlock &>(MBB).instr_begin()->operands_end()};
  }
};

/// ConstMIBundleOperands - Iterate over all operands in a const bundle of
/// machine instructions.
///
class ConstMIBundleOperands
    : public MIBundleOperandIteratorBase<const MachineOperand> {

  /// Constructor for an iterator past the last iteration.
  ConstMIBundleOperands(MachineBasicBlock::instr_iterator InstrE,
                        MachineInstr::mop_iterator OpE)
      : MIBundleOperandIteratorBase(InstrE, OpE) {}

public:
  ConstMIBundleOperands(const MachineInstr &MI)
      : MIBundleOperandIteratorBase(const_cast<MachineInstr &>(MI)) {}

  /// Returns an iterator past the last iteration.
  static ConstMIBundleOperands end(const MachineBasicBlock &MBB) {
    return {const_cast<MachineBasicBlock &>(MBB).instr_end(),
            const_cast<MachineBasicBlock &>(MBB).instr_begin()->operands_end()};
  }
};

inline iterator_range<ConstMIBundleOperands>
const_mi_bundle_ops(const MachineInstr &MI) {
  return make_range(ConstMIBundleOperands(MI),
                    ConstMIBundleOperands::end(*MI.getParent()));
}

inline iterator_range<MIBundleOperands> mi_bundle_ops(MachineInstr &MI) {
  return make_range(MIBundleOperands(MI),
                    MIBundleOperands::end(*MI.getParent()));
}

/// VirtRegInfo - Information about a virtual register used by a set of
/// operands.
///
struct VirtRegInfo {
  /// Reads - One of the operands read the virtual register.  This does not
  /// include undef or internal use operands, see MO::readsReg().
  bool Reads;

  /// Writes - One of the operands writes the virtual register.
  bool Writes;

  /// Tied - Uses and defs must use the same register. This can be because of
  /// a two-address constraint, or there may be a partial redefinition of a
  /// sub-register.
  bool Tied;
};

/// AnalyzeVirtRegInBundle - Analyze how the current instruction or bundle uses
/// a virtual register.  This function should not be called after operator++(),
/// it expects a fresh iterator.
///
/// @param Reg The virtual register to analyze.
/// @param Ops When set, this vector will receive an (MI, OpNum) entry for
///            each operand referring to Reg.
/// @returns A filled-in RegInfo struct.
VirtRegInfo AnalyzeVirtRegInBundle(
    MachineInstr &MI, Register Reg,
    SmallVectorImpl<std::pair<MachineInstr *, unsigned>> *Ops = nullptr);

/// Return a pair of lane masks (reads, writes) indicating which lanes this
/// instruction uses with Reg.
std::pair<LaneBitmask, LaneBitmask>
AnalyzeVirtRegLanesInBundle(const MachineInstr &MI, Register Reg,
                            const MachineRegisterInfo &MRI,
                            const TargetRegisterInfo &TRI);

/// Information about how a physical register Reg is used by a set of
/// operands.
struct PhysRegInfo {
  /// There is a regmask operand indicating Reg is clobbered.
  /// \see MachineOperand::CreateRegMask().
  bool Clobbered;

  /// Reg or one of its aliases is defined. The definition may only cover
  /// parts of the register.
  bool Defined;
  /// Reg or a super-register is defined. The definition covers the full
  /// register.
  bool FullyDefined;

  /// Reg or one of its aliases is read. The register may only be read
  /// partially.
  bool Read;
  /// Reg or a super-register is read. The full register is read.
  bool FullyRead;

  /// Either:
  /// - Reg is FullyDefined and all defs of reg or an overlapping
  ///   register are dead, or
  /// - Reg is completely dead because "defined" by a clobber.
  bool DeadDef;

  /// Reg is Defined and all defs of reg or an overlapping register are
  /// dead.
  bool PartialDeadDef;

  /// There is a use operand of reg or a super-register with kill flag set.
  bool Killed;
};

/// AnalyzePhysRegInBundle - Analyze how the current instruction or bundle uses
/// a physical register.  This function should not be called after operator++(),
/// it expects a fresh iterator.
///
/// @param Reg The physical register to analyze.
/// @returns A filled-in PhysRegInfo struct.
PhysRegInfo AnalyzePhysRegInBundle(const MachineInstr &MI, Register Reg,
                                   const TargetRegisterInfo *TRI);

} // End llvm namespace

#endif
