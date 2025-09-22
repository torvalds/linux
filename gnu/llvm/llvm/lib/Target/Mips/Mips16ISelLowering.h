//===-- Mips16ISelLowering.h - Mips16 DAG Lowering Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Subclass of MipsTargetLowering specialized for mips16.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPS16ISELLOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPS16ISELLOWERING_H

#include "MipsISelLowering.h"

namespace llvm {
  class Mips16TargetLowering : public MipsTargetLowering  {
  public:
    explicit Mips16TargetLowering(const MipsTargetMachine &TM,
                                  const MipsSubtarget &STI);

    bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AddrSpace,
                                        Align Alignment,
                                        MachineMemOperand::Flags Flags,
                                        unsigned *Fast) const override;

    MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr &MI,
                                MachineBasicBlock *MBB) const override;

  private:
    bool isEligibleForTailCallOptimization(
        const CCState &CCInfo, unsigned NextStackOffset,
        const MipsFunctionInfo &FI) const override;

    void setMips16HardFloatLibCalls();

    unsigned int
      getMips16HelperFunctionStubNumber(ArgListTy &Args) const;

    const char *getMips16HelperFunction
      (Type* RetTy, ArgListTy &Args, bool &needHelper) const;

    void
    getOpndList(SmallVectorImpl<SDValue> &Ops,
                std::deque< std::pair<unsigned, SDValue> > &RegsToPass,
                bool IsPICCall, bool GlobalOrExternal, bool InternalLinkage,
                bool IsCallReloc, CallLoweringInfo &CLI, SDValue Callee,
                SDValue Chain) const override;

    MachineBasicBlock *emitSel16(unsigned Opc, MachineInstr &MI,
                                 MachineBasicBlock *BB) const;

    MachineBasicBlock *emitSeliT16(unsigned Opc1, unsigned Opc2,
                                   MachineInstr &MI,
                                   MachineBasicBlock *BB) const;

    MachineBasicBlock *emitSelT16(unsigned Opc1, unsigned Opc2,
                                  MachineInstr &MI,
                                  MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_T8I816_ins(unsigned BtOpc, unsigned CmpOpc,
                                           MachineInstr &MI,
                                           MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_T8I8I16_ins(unsigned BtOpc, unsigned CmpiOpc,
                                            unsigned CmpiXOpc, bool ImmSigned,
                                            MachineInstr &MI,
                                            MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_CCRX16_ins(unsigned SltOpc, MachineInstr &MI,
                                           MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_CCRXI16_ins(unsigned SltiOpc, unsigned SltiXOpc,
                                            MachineInstr &MI,
                                            MachineBasicBlock *BB) const;
  };
}

#endif
