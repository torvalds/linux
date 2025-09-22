//=- SystemZTargetStreamer.h - SystemZ Target Streamer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZTARGETSTREAMER_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZTARGETSTREAMER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include <map>
#include <utility>

namespace llvm {

class SystemZTargetStreamer : public MCTargetStreamer {
public:
  SystemZTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

  typedef std::pair<MCInst, const MCSubtargetInfo *> MCInstSTIPair;
  struct CmpMCInst {
    bool operator()(const MCInstSTIPair &MCI_STI_A,
                    const MCInstSTIPair &MCI_STI_B) const {
      if (MCI_STI_A.second != MCI_STI_B.second)
        return uintptr_t(MCI_STI_A.second) < uintptr_t(MCI_STI_B.second);
      const MCInst &A = MCI_STI_A.first;
      const MCInst &B = MCI_STI_B.first;
      assert(A.getNumOperands() == B.getNumOperands() &&
             A.getNumOperands() == 5 && A.getOperand(2).getImm() == 1 &&
             B.getOperand(2).getImm() == 1 && "Unexpected EXRL target MCInst");
      if (A.getOpcode() != B.getOpcode())
        return A.getOpcode() < B.getOpcode();
      if (A.getOperand(0).getReg() != B.getOperand(0).getReg())
        return A.getOperand(0).getReg() < B.getOperand(0).getReg();
      if (A.getOperand(1).getImm() != B.getOperand(1).getImm())
        return A.getOperand(1).getImm() < B.getOperand(1).getImm();
      if (A.getOperand(3).getReg() != B.getOperand(3).getReg())
        return A.getOperand(3).getReg() < B.getOperand(3).getReg();
      if (A.getOperand(4).getImm() != B.getOperand(4).getImm())
        return A.getOperand(4).getImm() < B.getOperand(4).getImm();
      return false;
    }
  };
  typedef std::map<MCInstSTIPair, MCSymbol *, CmpMCInst> EXRLT2SymMap;
  EXRLT2SymMap EXRLTargets2Sym;

  void emitConstantPools() override;

  virtual void emitMachine(StringRef CPU) {};
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZTARGETSTREAMER_H
