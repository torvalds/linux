//===-- WebAssemblyWasmObjectWriter.cpp - WebAssembly Wasm Writer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file handles Wasm-specific object emission, converting LLVM's
/// internal fixups into the appropriate relocations.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyFixupKinds.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWasmObjectWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class WebAssemblyWasmObjectWriter final : public MCWasmObjectTargetWriter {
public:
  explicit WebAssemblyWasmObjectWriter(bool Is64Bit, bool IsEmscripten);

private:
  unsigned getRelocType(const MCValue &Target, const MCFixup &Fixup,
                        const MCSectionWasm &FixupSection,
                        bool IsLocRel) const override;
};
} // end anonymous namespace

WebAssemblyWasmObjectWriter::WebAssemblyWasmObjectWriter(bool Is64Bit,
                                                         bool IsEmscripten)
    : MCWasmObjectTargetWriter(Is64Bit, IsEmscripten) {}

static const MCSection *getTargetSection(const MCExpr *Expr) {
  if (auto SyExp = dyn_cast<MCSymbolRefExpr>(Expr)) {
    if (SyExp->getSymbol().isInSection())
      return &SyExp->getSymbol().getSection();
    return nullptr;
  }

  if (auto BinOp = dyn_cast<MCBinaryExpr>(Expr)) {
    auto SectionLHS = getTargetSection(BinOp->getLHS());
    auto SectionRHS = getTargetSection(BinOp->getRHS());
    return SectionLHS == SectionRHS ? nullptr : SectionLHS;
  }

  if (auto UnOp = dyn_cast<MCUnaryExpr>(Expr))
    return getTargetSection(UnOp->getSubExpr());

  return nullptr;
}

unsigned WebAssemblyWasmObjectWriter::getRelocType(
    const MCValue &Target, const MCFixup &Fixup,
    const MCSectionWasm &FixupSection, bool IsLocRel) const {
  const MCSymbolRefExpr *RefA = Target.getSymA();
  assert(RefA);
  auto& SymA = cast<MCSymbolWasm>(RefA->getSymbol());

  MCSymbolRefExpr::VariantKind Modifier = Target.getAccessVariant();

  switch (Modifier) {
    case MCSymbolRefExpr::VK_GOT:
    case MCSymbolRefExpr::VK_WASM_GOT_TLS:
      return wasm::R_WASM_GLOBAL_INDEX_LEB;
    case MCSymbolRefExpr::VK_WASM_TBREL:
      assert(SymA.isFunction());
      return is64Bit() ? wasm::R_WASM_TABLE_INDEX_REL_SLEB64
                       : wasm::R_WASM_TABLE_INDEX_REL_SLEB;
    case MCSymbolRefExpr::VK_WASM_TLSREL:
      return is64Bit() ? wasm::R_WASM_MEMORY_ADDR_TLS_SLEB64
                       : wasm::R_WASM_MEMORY_ADDR_TLS_SLEB;
    case MCSymbolRefExpr::VK_WASM_MBREL:
      assert(SymA.isData());
      return is64Bit() ? wasm::R_WASM_MEMORY_ADDR_REL_SLEB64
                       : wasm::R_WASM_MEMORY_ADDR_REL_SLEB;
    case MCSymbolRefExpr::VK_WASM_TYPEINDEX:
      return wasm::R_WASM_TYPE_INDEX_LEB;
    case MCSymbolRefExpr::VK_None:
      break;
    case MCSymbolRefExpr::VK_WASM_FUNCINDEX:
      return wasm::R_WASM_FUNCTION_INDEX_I32;
    default:
      report_fatal_error("unknown VariantKind");
      break;
  }

  switch (unsigned(Fixup.getKind())) {
  case WebAssembly::fixup_sleb128_i32:
    if (SymA.isFunction())
      return wasm::R_WASM_TABLE_INDEX_SLEB;
    return wasm::R_WASM_MEMORY_ADDR_SLEB;
  case WebAssembly::fixup_sleb128_i64:
    if (SymA.isFunction())
      return wasm::R_WASM_TABLE_INDEX_SLEB64;
    return wasm::R_WASM_MEMORY_ADDR_SLEB64;
  case WebAssembly::fixup_uleb128_i32:
    if (SymA.isGlobal())
      return wasm::R_WASM_GLOBAL_INDEX_LEB;
    if (SymA.isFunction())
      return wasm::R_WASM_FUNCTION_INDEX_LEB;
    if (SymA.isTag())
      return wasm::R_WASM_TAG_INDEX_LEB;
    if (SymA.isTable())
      return wasm::R_WASM_TABLE_NUMBER_LEB;
    return wasm::R_WASM_MEMORY_ADDR_LEB;
  case WebAssembly::fixup_uleb128_i64:
    assert(SymA.isData());
    return wasm::R_WASM_MEMORY_ADDR_LEB64;
  case FK_Data_4:
    if (SymA.isFunction()) {
      if (FixupSection.isMetadata())
        return wasm::R_WASM_FUNCTION_OFFSET_I32;
      assert(FixupSection.isWasmData());
      return wasm::R_WASM_TABLE_INDEX_I32;
    }
    if (SymA.isGlobal())
      return wasm::R_WASM_GLOBAL_INDEX_I32;
    if (auto Section = static_cast<const MCSectionWasm *>(
            getTargetSection(Fixup.getValue()))) {
      if (Section->isText())
        return wasm::R_WASM_FUNCTION_OFFSET_I32;
      else if (!Section->isWasmData())
        return wasm::R_WASM_SECTION_OFFSET_I32;
    }
    return IsLocRel ? wasm::R_WASM_MEMORY_ADDR_LOCREL_I32
                    : wasm::R_WASM_MEMORY_ADDR_I32;
  case FK_Data_8:
    if (SymA.isFunction()) {
      if (FixupSection.isMetadata())
        return wasm::R_WASM_FUNCTION_OFFSET_I64;
      return wasm::R_WASM_TABLE_INDEX_I64;
    }
    if (SymA.isGlobal())
      llvm_unreachable("unimplemented R_WASM_GLOBAL_INDEX_I64");
    if (auto Section = static_cast<const MCSectionWasm *>(
            getTargetSection(Fixup.getValue()))) {
      if (Section->isText())
        return wasm::R_WASM_FUNCTION_OFFSET_I64;
      else if (!Section->isWasmData())
        llvm_unreachable("unimplemented R_WASM_SECTION_OFFSET_I64");
    }
    assert(SymA.isData());
    return wasm::R_WASM_MEMORY_ADDR_I64;
  default:
    llvm_unreachable("unimplemented fixup kind");
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createWebAssemblyWasmObjectWriter(bool Is64Bit, bool IsEmscripten) {
  return std::make_unique<WebAssemblyWasmObjectWriter>(Is64Bit, IsEmscripten);
}
