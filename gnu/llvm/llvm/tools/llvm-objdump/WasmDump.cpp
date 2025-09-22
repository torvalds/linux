//===-- WasmDump.cpp - wasm-specific dumper ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the wasm-specific dumper for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "WasmDump.h"

#include "llvm-objdump.h"
#include "llvm/Object/Wasm.h"

using namespace llvm;
using namespace llvm::object;

namespace {
class WasmDumper : public objdump::Dumper {
  const WasmObjectFile &Obj;

public:
  WasmDumper(const WasmObjectFile &O) : Dumper(O), Obj(O) {}
  void printPrivateHeaders() override;
};
} // namespace

std::unique_ptr<objdump::Dumper>
objdump::createWasmDumper(const object::WasmObjectFile &Obj) {
  return std::make_unique<WasmDumper>(Obj);
}

void WasmDumper::printPrivateHeaders() {
  outs() << "Program Header:\n";
  outs() << "Version: 0x";
  outs().write_hex(Obj.getHeader().Version);
  outs() << "\n";
}

Error objdump::getWasmRelocationValueString(const WasmObjectFile *Obj,
                                            const RelocationRef &RelRef,
                                            SmallVectorImpl<char> &Result) {
  const wasm::WasmRelocation &Rel = Obj->getWasmRelocation(RelRef);
  symbol_iterator SI = RelRef.getSymbol();
  std::string FmtBuf;
  raw_string_ostream Fmt(FmtBuf);
  if (SI == Obj->symbol_end()) {
    // Not all wasm relocations have symbols associated with them.
    // In particular R_WASM_TYPE_INDEX_LEB.
    Fmt << Rel.Index;
  } else {
    Expected<StringRef> SymNameOrErr = SI->getName();
    if (!SymNameOrErr)
      return SymNameOrErr.takeError();
    StringRef SymName = *SymNameOrErr;
    Result.append(SymName.begin(), SymName.end());
  }
  Fmt << (Rel.Addend < 0 ? "" : "+") << Rel.Addend;
  Fmt.flush();
  Result.append(FmtBuf.begin(), FmtBuf.end());
  return Error::success();
}
