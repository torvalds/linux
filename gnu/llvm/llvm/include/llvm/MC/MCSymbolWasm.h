//===- MCSymbolWasm.h -  ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLWASM_H
#define LLVM_MC_MCSYMBOLWASM_H

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"

namespace llvm {

class MCSymbolWasm : public MCSymbol {
  std::optional<wasm::WasmSymbolType> Type;
  bool IsWeak = false;
  bool IsHidden = false;
  bool IsComdat = false;
  bool OmitFromLinkingSection = false;
  mutable bool IsUsedInInitArray = false;
  mutable bool IsUsedInGOT = false;
  std::optional<StringRef> ImportModule;
  std::optional<StringRef> ImportName;
  std::optional<StringRef> ExportName;
  wasm::WasmSignature *Signature = nullptr;
  std::optional<wasm::WasmGlobalType> GlobalType;
  std::optional<wasm::WasmTableType> TableType;

  /// An expression describing how to calculate the size of a symbol. If a
  /// symbol has no size this field will be NULL.
  const MCExpr *SymbolSize = nullptr;

public:
  MCSymbolWasm(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(SymbolKindWasm, Name, isTemporary) {}
  static bool classof(const MCSymbol *S) { return S->isWasm(); }

  const MCExpr *getSize() const { return SymbolSize; }
  void setSize(const MCExpr *SS) { SymbolSize = SS; }

  bool isFunction() const { return Type == wasm::WASM_SYMBOL_TYPE_FUNCTION; }
  // Data is the default value if not set.
  bool isData() const { return !Type || Type == wasm::WASM_SYMBOL_TYPE_DATA; }
  bool isGlobal() const { return Type == wasm::WASM_SYMBOL_TYPE_GLOBAL; }
  bool isTable() const { return Type == wasm::WASM_SYMBOL_TYPE_TABLE; }
  bool isSection() const { return Type == wasm::WASM_SYMBOL_TYPE_SECTION; }
  bool isTag() const { return Type == wasm::WASM_SYMBOL_TYPE_TAG; }

  std::optional<wasm::WasmSymbolType> getType() const { return Type; }

  void setType(wasm::WasmSymbolType type) { Type = type; }

  bool isExported() const {
    return getFlags() & wasm::WASM_SYMBOL_EXPORTED;
  }
  void setExported() const {
    modifyFlags(wasm::WASM_SYMBOL_EXPORTED, wasm::WASM_SYMBOL_EXPORTED);
  }

  bool isNoStrip() const {
    return getFlags() & wasm::WASM_SYMBOL_NO_STRIP;
  }
  void setNoStrip() const {
    modifyFlags(wasm::WASM_SYMBOL_NO_STRIP, wasm::WASM_SYMBOL_NO_STRIP);
  }

  bool isTLS() const { return getFlags() & wasm::WASM_SYMBOL_TLS; }
  void setTLS() const {
    modifyFlags(wasm::WASM_SYMBOL_TLS, wasm::WASM_SYMBOL_TLS);
  }

  bool isWeak() const { return IsWeak; }
  void setWeak(bool isWeak) { IsWeak = isWeak; }

  bool isHidden() const { return IsHidden; }
  void setHidden(bool isHidden) { IsHidden = isHidden; }

  bool isComdat() const { return IsComdat; }
  void setComdat(bool isComdat) { IsComdat = isComdat; }

  // wasm-ld understands a finite set of symbol types.  This flag allows the
  // compiler to avoid emitting symbol table entries that would confuse the
  // linker, unless the user specifically requests the feature.
  bool omitFromLinkingSection() const { return OmitFromLinkingSection; }
  void setOmitFromLinkingSection() { OmitFromLinkingSection = true; }

  bool hasImportModule() const { return ImportModule.has_value(); }
  StringRef getImportModule() const {
    if (ImportModule)
      return *ImportModule;
    // Use a default module name of "env" for now, for compatibility with
    // existing tools.
    // TODO(sbc): Find a way to specify a default value in the object format
    // without picking a hardcoded value like this.
    return "env";
  }
  void setImportModule(StringRef Name) { ImportModule = Name; }

  bool hasImportName() const { return ImportName.has_value(); }
  StringRef getImportName() const {
    if (ImportName)
      return *ImportName;
    return getName();
  }
  void setImportName(StringRef Name) { ImportName = Name; }

  bool hasExportName() const { return ExportName.has_value(); }
  StringRef getExportName() const { return *ExportName; }
  void setExportName(StringRef Name) { ExportName = Name; }

  bool isFunctionTable() const {
    return isTable() && hasTableType() &&
           getTableType().ElemType == wasm::ValType::FUNCREF;
  }
  void setFunctionTable(bool is64) {
    setType(wasm::WASM_SYMBOL_TYPE_TABLE);
    uint8_t flags =
        is64 ? wasm::WASM_LIMITS_FLAG_IS_64 : wasm::WASM_LIMITS_FLAG_NONE;
    setTableType(wasm::ValType::FUNCREF, flags);
  }

  void setUsedInGOT() const { IsUsedInGOT = true; }
  bool isUsedInGOT() const { return IsUsedInGOT; }

  void setUsedInInitArray() const { IsUsedInInitArray = true; }
  bool isUsedInInitArray() const { return IsUsedInInitArray; }

  const wasm::WasmSignature *getSignature() const { return Signature; }
  void setSignature(wasm::WasmSignature *Sig) { Signature = Sig; }

  const wasm::WasmGlobalType &getGlobalType() const {
    assert(GlobalType);
    return *GlobalType;
  }
  void setGlobalType(wasm::WasmGlobalType GT) { GlobalType = GT; }

  bool hasTableType() const { return TableType.has_value(); }
  const wasm::WasmTableType &getTableType() const {
    assert(hasTableType());
    return *TableType;
  }
  void setTableType(wasm::WasmTableType TT) { TableType = TT; }
  void setTableType(wasm::ValType VT,
                    uint8_t flags = wasm::WASM_LIMITS_FLAG_NONE) {
    // Declare a table with element type VT and no limits (min size 0, no max
    // size).
    wasm::WasmLimits Limits = {flags, 0, 0};
    setTableType({VT, Limits});
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLWASM_H
