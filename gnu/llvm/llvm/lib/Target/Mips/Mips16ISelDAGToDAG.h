//===---- Mips16ISelDAGToDAG.h - A Dag to Dag Inst Selector for Mips ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Subclass of MipsDAGToDAGISel specialized for mips16.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPS16ISELDAGTODAG_H
#define LLVM_LIB_TARGET_MIPS_MIPS16ISELDAGTODAG_H

#include "MipsISelDAGToDAG.h"

namespace llvm {

class Mips16DAGToDAGISel : public MipsDAGToDAGISel {
public:
  explicit Mips16DAGToDAGISel(MipsTargetMachine &TM, CodeGenOptLevel OL)
      : MipsDAGToDAGISel(TM, OL) {}

private:
  std::pair<SDNode *, SDNode *> selectMULT(SDNode *N, unsigned Opc,
                                           const SDLoc &DL, EVT Ty, bool HasLo,
                                           bool HasHi);

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool selectAddr(bool SPAllowed, SDValue Addr, SDValue &Base,
                  SDValue &Offset);
  bool selectAddr16(SDValue Addr, SDValue &Base,
                    SDValue &Offset) override;
  bool selectAddr16SP(SDValue Addr, SDValue &Base,
                      SDValue &Offset) override;

  bool trySelect(SDNode *Node) override;

  void processFunctionAfterISel(MachineFunction &MF) override;

  // Insert instructions to initialize the global base register in the
  // first MBB of the function.
  void initGlobalBaseReg(MachineFunction &MF);

  void initMips16SPAliasReg(MachineFunction &MF);
};

class Mips16DAGToDAGISelLegacy : public MipsDAGToDAGISelLegacy {
public:
  explicit Mips16DAGToDAGISelLegacy(MipsTargetMachine &TM, CodeGenOptLevel OL);
};

FunctionPass *createMips16ISelDag(MipsTargetMachine &TM,
                                  CodeGenOptLevel OptLevel);
}

#endif
