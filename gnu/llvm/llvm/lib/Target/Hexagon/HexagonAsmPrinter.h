//===- HexagonAsmPrinter.h - Print machine code -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hexagon Assembly printer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONASMPRINTER_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONASMPRINTER_H

#include "HexagonSubtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/MC/MCStreamer.h"
#include <utility>

namespace llvm {

class MachineInstr;
class MCInst;
class raw_ostream;
class TargetMachine;

  class HexagonAsmPrinter : public AsmPrinter {
    const HexagonSubtarget *Subtarget = nullptr;

    void emitAttributes();

  public:
    explicit HexagonAsmPrinter(TargetMachine &TM,
                               std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

    bool runOnMachineFunction(MachineFunction &Fn) override {
      Subtarget = &Fn.getSubtarget<HexagonSubtarget>();
      const bool Modified = AsmPrinter::runOnMachineFunction(Fn);
      // Emit the XRay table for this function.
      emitXRayTable();

      return Modified;
    }

    StringRef getPassName() const override {
      return "Hexagon Assembly Printer";
    }

    bool isBlockOnlyReachableByFallthrough(const MachineBasicBlock *MBB)
          const override;

    void emitInstruction(const MachineInstr *MI) override;

    //===------------------------------------------------------------------===//
    // XRay implementation
    //===------------------------------------------------------------------===//
    // XRay-specific lowering for Hexagon.
    void LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI);
    void LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI);
    void LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI);
    void EmitSled(const MachineInstr &MI, SledKind Kind);

    void HexagonProcessInstruction(MCInst &Inst, const MachineInstr &MBB);

    void printOperand(const MachineInstr *MI, unsigned OpNo, raw_ostream &O);
    bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                         const char *ExtraCode, raw_ostream &OS) override;
    bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                               const char *ExtraCode, raw_ostream &OS) override;
    void emitStartOfAsmFile(Module &M) override;
    void emitEndOfAsmFile(Module &M) override;
  };

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONASMPRINTER_H
