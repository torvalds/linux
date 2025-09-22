// WebAssemblyAsmPrinter.h - WebAssembly implementation of AsmPrinter-*- C++ -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYASMPRINTER_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYASMPRINTER_H

#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class WebAssemblyTargetStreamer;

class LLVM_LIBRARY_VISIBILITY WebAssemblyAsmPrinter final : public AsmPrinter {
  const WebAssemblySubtarget *Subtarget;
  const MachineRegisterInfo *MRI;
  WebAssemblyFunctionInfo *MFI;
  bool signaturesEmitted = false;

public:
  explicit WebAssemblyAsmPrinter(TargetMachine &TM,
                                 std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), Subtarget(nullptr), MRI(nullptr),
        MFI(nullptr) {}

  StringRef getPassName() const override {
    return "WebAssembly Assembly Printer";
  }

  const WebAssemblySubtarget &getSubtarget() const { return *Subtarget; }

  //===------------------------------------------------------------------===//
  // MachineFunctionPass Implementation.
  //===------------------------------------------------------------------===//

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<WebAssemblySubtarget>();
    MRI = &MF.getRegInfo();
    MFI = MF.getInfo<WebAssemblyFunctionInfo>();
    return AsmPrinter::runOnMachineFunction(MF);
  }

  //===------------------------------------------------------------------===//
  // AsmPrinter Implementation.
  //===------------------------------------------------------------------===//

  void emitEndOfAsmFile(Module &M) override;
  void EmitProducerInfo(Module &M);
  void EmitTargetFeatures(Module &M);
  void EmitFunctionAttributes(Module &M);
  void emitSymbolType(const MCSymbolWasm *Sym);
  void emitGlobalVariable(const GlobalVariable *GV) override;
  void emitJumpTableInfo() override;
  void emitConstantPool() override;
  void emitFunctionBodyStart() override;
  void emitInstruction(const MachineInstr *MI) override;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;

  MVT getRegType(unsigned RegNo) const;
  std::string regToString(const MachineOperand &MO);
  WebAssemblyTargetStreamer *getTargetStreamer();
  MCSymbolWasm *getMCSymbolForFunction(const Function *F, bool EnableEmEH,
                                       wasm::WasmSignature *Sig,
                                       bool &InvokeDetected);
  MCSymbol *getOrCreateWasmSymbol(StringRef Name);
  void emitDecls(const Module &M);
};

} // end namespace llvm

#endif
