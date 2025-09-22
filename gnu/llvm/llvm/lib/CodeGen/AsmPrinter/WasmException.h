//===-- WasmException.h - Wasm Exception Framework -------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing WebAssembly exception info into asm
// files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_WASMEXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_WASMEXCEPTION_H

#include "EHStreamer.h"

namespace llvm {
class AsmPrinter;
class MachineFunction;
struct LandingPadInfo;
template <typename T> class SmallVectorImpl;

class LLVM_LIBRARY_VISIBILITY WasmException : public EHStreamer {
public:
  WasmException(AsmPrinter *A) : EHStreamer(A) {}

  void endModule() override;
  void beginFunction(const MachineFunction *MF) override {}
  void endFunction(const MachineFunction *MF) override;

protected:
  // Compute the call site table for wasm EH.
  void computeCallSiteTable(
      SmallVectorImpl<CallSiteEntry> &CallSites,
      SmallVectorImpl<CallSiteRange> &CallSiteRanges,
      const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
      const SmallVectorImpl<unsigned> &FirstActions) override;
};

} // End of namespace llvm

#endif
