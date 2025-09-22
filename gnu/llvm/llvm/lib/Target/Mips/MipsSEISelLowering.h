//===- MipsSEISelLowering.h - MipsSE DAG Lowering Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Subclass of MipsTargetLowering specialized for mips32/64.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSSEISELLOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPSSEISELLOWERING_H

#include "MipsISelLowering.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"

namespace llvm {

class MachineBasicBlock;
class MachineInstr;
class MipsSubtarget;
class MipsTargetMachine;
class SelectionDAG;
class TargetRegisterClass;

  class MipsSETargetLowering : public MipsTargetLowering  {
  public:
    explicit MipsSETargetLowering(const MipsTargetMachine &TM,
                                  const MipsSubtarget &STI);

    /// Enable MSA support for the given integer type and Register
    /// class.
    void addMSAIntType(MVT::SimpleValueType Ty, const TargetRegisterClass *RC);

    /// Enable MSA support for the given floating-point type and
    /// Register class.
    void addMSAFloatType(MVT::SimpleValueType Ty,
                         const TargetRegisterClass *RC);

    bool allowsMisalignedMemoryAccesses(
        EVT VT, unsigned AS = 0, Align Alignment = Align(1),
        MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
        unsigned *Fast = nullptr) const override;

    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

    MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr &MI,
                                MachineBasicBlock *MBB) const override;

    bool isShuffleMaskLegal(ArrayRef<int> Mask, EVT VT) const override {
      return false;
    }

    const TargetRegisterClass *getRepRegClassFor(MVT VT) const override;

  private:
    bool isEligibleForTailCallOptimization(
        const CCState &CCInfo, unsigned NextStackOffset,
        const MipsFunctionInfo &FI) const override;

    void
    getOpndList(SmallVectorImpl<SDValue> &Ops,
                std::deque<std::pair<unsigned, SDValue>> &RegsToPass,
                bool IsPICCall, bool GlobalOrExternal, bool InternalLinkage,
                bool IsCallReloc, CallLoweringInfo &CLI, SDValue Callee,
                SDValue Chain) const override;

    SDValue lowerLOAD(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerSTORE(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerBITCAST(SDValue Op, SelectionDAG &DAG) const;

    SDValue lowerMulDiv(SDValue Op, unsigned NewOpc, bool HasLo, bool HasHi,
                        SelectionDAG &DAG) const;

    SDValue lowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerINTRINSIC_W_CHAIN(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerINTRINSIC_VOID(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
    /// Lower VECTOR_SHUFFLE into one of a number of instructions
    /// depending on the indices in the shuffle.
    SDValue lowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerSELECT(SDValue Op, SelectionDAG &DAG) const;

    MachineBasicBlock *emitBPOSGE32(MachineInstr &MI,
                                    MachineBasicBlock *BB) const;
    MachineBasicBlock *emitMSACBranchPseudo(MachineInstr &MI,
                                            MachineBasicBlock *BB,
                                            unsigned BranchOp) const;
    /// Emit the COPY_FW pseudo instruction
    MachineBasicBlock *emitCOPY_FW(MachineInstr &MI,
                                   MachineBasicBlock *BB) const;
    /// Emit the COPY_FD pseudo instruction
    MachineBasicBlock *emitCOPY_FD(MachineInstr &MI,
                                   MachineBasicBlock *BB) const;
    /// Emit the INSERT_FW pseudo instruction
    MachineBasicBlock *emitINSERT_FW(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
    /// Emit the INSERT_FD pseudo instruction
    MachineBasicBlock *emitINSERT_FD(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
    /// Emit the INSERT_([BHWD]|F[WD])_VIDX pseudo instruction
    MachineBasicBlock *emitINSERT_DF_VIDX(MachineInstr &MI,
                                          MachineBasicBlock *BB,
                                          unsigned EltSizeInBytes,
                                          bool IsFP) const;
    /// Emit the FILL_FW pseudo instruction
    MachineBasicBlock *emitFILL_FW(MachineInstr &MI,
                                   MachineBasicBlock *BB) const;
    /// Emit the FILL_FD pseudo instruction
    MachineBasicBlock *emitFILL_FD(MachineInstr &MI,
                                   MachineBasicBlock *BB) const;
    /// Emit the FEXP2_W_1 pseudo instructions.
    MachineBasicBlock *emitFEXP2_W_1(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
    /// Emit the FEXP2_D_1 pseudo instructions.
    MachineBasicBlock *emitFEXP2_D_1(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
    /// Emit the FILL_FW pseudo instruction
    MachineBasicBlock *emitLD_F16_PSEUDO(MachineInstr &MI,
                                   MachineBasicBlock *BB) const;
    /// Emit the FILL_FD pseudo instruction
    MachineBasicBlock *emitST_F16_PSEUDO(MachineInstr &MI,
                                   MachineBasicBlock *BB) const;
    /// Emit the FEXP2_W_1 pseudo instructions.
    MachineBasicBlock *emitFPEXTEND_PSEUDO(MachineInstr &MI,
                                           MachineBasicBlock *BB,
                                           bool IsFGR64) const;
    /// Emit the FEXP2_D_1 pseudo instructions.
    MachineBasicBlock *emitFPROUND_PSEUDO(MachineInstr &MI,
                                          MachineBasicBlock *BBi,
                                          bool IsFGR64) const;
  };

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSSEISELLOWERING_H
