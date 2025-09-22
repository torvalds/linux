//===-- MipsSEISelDAGToDAG.h - A Dag to Dag Inst Selector for MipsSE -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Subclass of MipsDAGToDAGISel specialized for mips32/64.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSSEISELDAGTODAG_H
#define LLVM_LIB_TARGET_MIPS_MIPSSEISELDAGTODAG_H

#include "MipsISelDAGToDAG.h"

namespace llvm {

class MipsSEDAGToDAGISel : public MipsDAGToDAGISel {

public:
  explicit MipsSEDAGToDAGISel(MipsTargetMachine &TM, CodeGenOptLevel OL)
      : MipsDAGToDAGISel(TM, OL) {}

private:

  bool runOnMachineFunction(MachineFunction &MF) override;

  void addDSPCtrlRegOperands(bool IsDef, MachineInstr &MI,
                             MachineFunction &MF);

  unsigned getMSACtrlReg(const SDValue RegIdx) const;

  bool replaceUsesWithZeroReg(MachineRegisterInfo *MRI, const MachineInstr&);

  std::pair<SDNode *, SDNode *> selectMULT(SDNode *N, unsigned Opc,
                                           const SDLoc &dl, EVT Ty, bool HasLo,
                                           bool HasHi);

  void selectAddE(SDNode *Node, const SDLoc &DL) const;

  bool selectAddrFrameIndex(SDValue Addr, SDValue &Base, SDValue &Offset) const;
  bool selectAddrFrameIndexOffset(SDValue Addr, SDValue &Base, SDValue &Offset,
                                  unsigned OffsetBits,
                                  unsigned ShiftAmount) const;

  bool selectAddrRegImm(SDValue Addr, SDValue &Base,
                        SDValue &Offset) const override;

  bool selectAddrDefault(SDValue Addr, SDValue &Base,
                         SDValue &Offset) const override;

  bool selectIntAddr(SDValue Addr, SDValue &Base,
                     SDValue &Offset) const override;

  bool selectAddrRegImm9(SDValue Addr, SDValue &Base,
                         SDValue &Offset) const;

  bool selectAddrRegImm11(SDValue Addr, SDValue &Base,
                          SDValue &Offset) const;

  bool selectAddrRegImm12(SDValue Addr, SDValue &Base,
                          SDValue &Offset) const;

  bool selectAddrRegImm16(SDValue Addr, SDValue &Base,
                          SDValue &Offset) const;

  bool selectIntAddr11MM(SDValue Addr, SDValue &Base,
                         SDValue &Offset) const override;

  bool selectIntAddr12MM(SDValue Addr, SDValue &Base,
                         SDValue &Offset) const override;

  bool selectIntAddr16MM(SDValue Addr, SDValue &Base,
                         SDValue &Offset) const override;

  bool selectIntAddrLSL2MM(SDValue Addr, SDValue &Base,
                           SDValue &Offset) const override;

  bool selectIntAddrSImm10(SDValue Addr, SDValue &Base,
                           SDValue &Offset) const override;

  bool selectIntAddrSImm10Lsl1(SDValue Addr, SDValue &Base,
                               SDValue &Offset) const override;

  bool selectIntAddrSImm10Lsl2(SDValue Addr, SDValue &Base,
                               SDValue &Offset) const override;

  bool selectIntAddrSImm10Lsl3(SDValue Addr, SDValue &Base,
                               SDValue &Offset) const override;

  /// Select constant vector splats.
  bool selectVSplat(SDNode *N, APInt &Imm,
                    unsigned MinSizeInBits) const override;
  /// Select constant vector splats whose value fits in a given integer.
  bool selectVSplatCommon(SDValue N, SDValue &Imm, bool Signed,
                                  unsigned ImmBitSize) const;
  /// Select constant vector splats whose value fits in a uimm1.
  bool selectVSplatUimm1(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value fits in a uimm2.
  bool selectVSplatUimm2(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value fits in a uimm3.
  bool selectVSplatUimm3(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value fits in a uimm4.
  bool selectVSplatUimm4(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value fits in a uimm5.
  bool selectVSplatUimm5(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value fits in a uimm6.
  bool selectVSplatUimm6(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value fits in a uimm8.
  bool selectVSplatUimm8(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value fits in a simm5.
  bool selectVSplatSimm5(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value is a power of 2.
  bool selectVSplatUimmPow2(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value is the inverse of a
  /// power of 2.
  bool selectVSplatUimmInvPow2(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value is a run of set bits
  /// ending at the most significant bit.
  bool selectVSplatMaskL(SDValue N, SDValue &Imm) const override;
  /// Select constant vector splats whose value is a run of set bits
  /// starting at bit zero.
  bool selectVSplatMaskR(SDValue N, SDValue &Imm) const override;

  /// Select constant vector splats whose value is 1.
  bool selectVSplatImmEq1(SDValue N) const override;

  bool trySelect(SDNode *Node) override;

  // Emits proper ABI for _mcount profiling calls.
  void emitMCountABI(MachineInstr &MI, MachineBasicBlock &MBB,
                     MachineFunction &MF);

  void processFunctionAfterISel(MachineFunction &MF) override;

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;
};

class MipsSEDAGToDAGISelLegacy : public MipsDAGToDAGISelLegacy {
public:
  explicit MipsSEDAGToDAGISelLegacy(MipsTargetMachine &TM, CodeGenOptLevel OL);
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

FunctionPass *createMipsSEISelDag(MipsTargetMachine &TM,
                                  CodeGenOptLevel OptLevel);
}

#endif
