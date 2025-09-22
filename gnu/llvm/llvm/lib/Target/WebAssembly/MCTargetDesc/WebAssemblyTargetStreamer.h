//==-- WebAssemblyTargetStreamer.h - WebAssembly Target Streamer -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares WebAssembly-specific target streamer classes.
/// These are for implementing support for target-specific assembly directives.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYTARGETSTREAMER_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYTARGETSTREAMER_H

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {

class MCSymbolWasm;
class formatted_raw_ostream;

/// WebAssembly-specific streamer interface, to implement support
/// WebAssembly-specific assembly directives.
class WebAssemblyTargetStreamer : public MCTargetStreamer {
public:
  explicit WebAssemblyTargetStreamer(MCStreamer &S);

  /// .local
  virtual void emitLocal(ArrayRef<wasm::ValType> Types) = 0;
  /// .functype
  virtual void emitFunctionType(const MCSymbolWasm *Sym) = 0;
  /// .indidx
  virtual void emitIndIdx(const MCExpr *Value) = 0;
  /// .globaltype
  virtual void emitGlobalType(const MCSymbolWasm *Sym) = 0;
  /// .tabletype
  virtual void emitTableType(const MCSymbolWasm *Sym) = 0;
  /// .tagtype
  virtual void emitTagType(const MCSymbolWasm *Sym) = 0;
  /// .import_module
  virtual void emitImportModule(const MCSymbolWasm *Sym,
                                StringRef ImportModule) = 0;
  /// .import_name
  virtual void emitImportName(const MCSymbolWasm *Sym,
                              StringRef ImportName) = 0;
  /// .export_name
  virtual void emitExportName(const MCSymbolWasm *Sym,
                              StringRef ExportName) = 0;

protected:
  void emitValueType(wasm::ValType Type);
};

/// This part is for ascii assembly output
class WebAssemblyTargetAsmStreamer final : public WebAssemblyTargetStreamer {
  formatted_raw_ostream &OS;

public:
  WebAssemblyTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitLocal(ArrayRef<wasm::ValType> Types) override;
  void emitFunctionType(const MCSymbolWasm *Sym) override;
  void emitIndIdx(const MCExpr *Value) override;
  void emitGlobalType(const MCSymbolWasm *Sym) override;
  void emitTableType(const MCSymbolWasm *Sym) override;
  void emitTagType(const MCSymbolWasm *Sym) override;
  void emitImportModule(const MCSymbolWasm *Sym, StringRef ImportModule) override;
  void emitImportName(const MCSymbolWasm *Sym, StringRef ImportName) override;
  void emitExportName(const MCSymbolWasm *Sym, StringRef ExportName) override;
};

/// This part is for Wasm object output
class WebAssemblyTargetWasmStreamer final : public WebAssemblyTargetStreamer {
public:
  explicit WebAssemblyTargetWasmStreamer(MCStreamer &S);

  void emitLocal(ArrayRef<wasm::ValType> Types) override;
  void emitFunctionType(const MCSymbolWasm *Sym) override {}
  void emitIndIdx(const MCExpr *Value) override;
  void emitGlobalType(const MCSymbolWasm *Sym) override {}
  void emitTableType(const MCSymbolWasm *Sym) override {}
  void emitTagType(const MCSymbolWasm *Sym) override {}
  void emitImportModule(const MCSymbolWasm *Sym,
                        StringRef ImportModule) override {}
  void emitImportName(const MCSymbolWasm *Sym,
                      StringRef ImportName) override {}
  void emitExportName(const MCSymbolWasm *Sym,
                      StringRef ExportName) override {}
};

/// This part is for null output
class WebAssemblyTargetNullStreamer final : public WebAssemblyTargetStreamer {
public:
  explicit WebAssemblyTargetNullStreamer(MCStreamer &S)
      : WebAssemblyTargetStreamer(S) {}

  void emitLocal(ArrayRef<wasm::ValType>) override {}
  void emitFunctionType(const MCSymbolWasm *) override {}
  void emitIndIdx(const MCExpr *) override {}
  void emitGlobalType(const MCSymbolWasm *) override {}
  void emitTableType(const MCSymbolWasm *) override {}
  void emitTagType(const MCSymbolWasm *) override {}
  void emitImportModule(const MCSymbolWasm *, StringRef) override {}
  void emitImportName(const MCSymbolWasm *, StringRef) override {}
  void emitExportName(const MCSymbolWasm *, StringRef) override {}
};

} // end namespace llvm

#endif
