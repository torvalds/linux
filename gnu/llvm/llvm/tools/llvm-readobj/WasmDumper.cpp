//===-- WasmDumper.cpp - Wasm-specific object file dumper -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Wasm-specific dumper for llvm-readobj.
//
//===----------------------------------------------------------------------===//

#include "ObjDumper.h"
#include "llvm-readobj.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace object;

namespace {

const EnumEntry<unsigned> WasmSymbolTypes[] = {
#define ENUM_ENTRY(X)                                                          \
  { #X, wasm::WASM_SYMBOL_TYPE_##X }
    ENUM_ENTRY(FUNCTION), ENUM_ENTRY(DATA), ENUM_ENTRY(GLOBAL),
    ENUM_ENTRY(SECTION),  ENUM_ENTRY(TAG),  ENUM_ENTRY(TABLE),
#undef ENUM_ENTRY
};

const EnumEntry<uint32_t> WasmSectionTypes[] = {
#define ENUM_ENTRY(X)                                                          \
  { #X, wasm::WASM_SEC_##X }
    ENUM_ENTRY(CUSTOM),   ENUM_ENTRY(TYPE),      ENUM_ENTRY(IMPORT),
    ENUM_ENTRY(FUNCTION), ENUM_ENTRY(TABLE),     ENUM_ENTRY(MEMORY),
    ENUM_ENTRY(GLOBAL),   ENUM_ENTRY(TAG),       ENUM_ENTRY(EXPORT),
    ENUM_ENTRY(START),    ENUM_ENTRY(ELEM),      ENUM_ENTRY(CODE),
    ENUM_ENTRY(DATA),     ENUM_ENTRY(DATACOUNT),
#undef ENUM_ENTRY
};

const EnumEntry<unsigned> WasmSymbolFlags[] = {
#define ENUM_ENTRY(X)                                                          \
  { #X, wasm::WASM_SYMBOL_##X }
  ENUM_ENTRY(BINDING_GLOBAL),
  ENUM_ENTRY(BINDING_WEAK),
  ENUM_ENTRY(BINDING_LOCAL),
  ENUM_ENTRY(VISIBILITY_DEFAULT),
  ENUM_ENTRY(VISIBILITY_HIDDEN),
  ENUM_ENTRY(UNDEFINED),
  ENUM_ENTRY(EXPORTED),
  ENUM_ENTRY(EXPLICIT_NAME),
  ENUM_ENTRY(NO_STRIP),
#undef ENUM_ENTRY
};

class WasmDumper : public ObjDumper {
public:
  WasmDumper(const WasmObjectFile *Obj, ScopedPrinter &Writer)
      : ObjDumper(Writer, Obj->getFileName()), Obj(Obj) {}

  void printFileHeaders() override;
  void printSectionHeaders() override;
  void printRelocations() override;
  void printUnwindInfo() override { llvm_unreachable("unimplemented"); }
  void printStackMap() const override { llvm_unreachable("unimplemented"); }

protected:
  void printSymbol(const SymbolRef &Sym);
  void printRelocation(const SectionRef &Section, const RelocationRef &Reloc);

private:
  void printSymbols(bool ExtraSymInfo) override;
  void printDynamicSymbols() override { llvm_unreachable("unimplemented"); }

  const WasmObjectFile *Obj;
};

void WasmDumper::printFileHeaders() {
  W.printHex("Version", Obj->getHeader().Version);
}

void WasmDumper::printRelocation(const SectionRef &Section,
                                 const RelocationRef &Reloc) {
  SmallString<64> RelocTypeName;
  uint64_t RelocType = Reloc.getType();
  Reloc.getTypeName(RelocTypeName);
  const wasm::WasmRelocation &WasmReloc = Obj->getWasmRelocation(Reloc);

  StringRef SymName;
  symbol_iterator SI = Reloc.getSymbol();
  if (SI != Obj->symbol_end())
    SymName = unwrapOrError(Obj->getFileName(), SI->getName());

  bool HasAddend = wasm::relocTypeHasAddend(static_cast<uint32_t>(RelocType));

  if (opts::ExpandRelocs) {
    DictScope Group(W, "Relocation");
    W.printNumber("Type", RelocTypeName, RelocType);
    W.printHex("Offset", Reloc.getOffset());
    if (!SymName.empty())
      W.printString("Symbol", SymName);
    else
      W.printHex("Index", WasmReloc.Index);
    if (HasAddend)
      W.printNumber("Addend", WasmReloc.Addend);
  } else {
    raw_ostream &OS = W.startLine();
    OS << W.hex(Reloc.getOffset()) << " " << RelocTypeName << " ";
    if (!SymName.empty())
      OS << SymName;
    else
      OS << WasmReloc.Index;
    if (HasAddend)
      OS << " " << WasmReloc.Addend;
    OS << "\n";
  }
}

void WasmDumper::printRelocations() {
  ListScope D(W, "Relocations");

  int SectionNumber = 0;
  for (const SectionRef &Section : Obj->sections()) {
    bool PrintedGroup = false;
    StringRef Name = unwrapOrError(Obj->getFileName(), Section.getName());

    ++SectionNumber;

    for (const RelocationRef &Reloc : Section.relocations()) {
      if (!PrintedGroup) {
        W.startLine() << "Section (" << SectionNumber << ") " << Name << " {\n";
        W.indent();
        PrintedGroup = true;
      }

      printRelocation(Section, Reloc);
    }

    if (PrintedGroup) {
      W.unindent();
      W.startLine() << "}\n";
    }
  }
}

void WasmDumper::printSymbols(bool /*ExtraSymInfo*/) {
  ListScope Group(W, "Symbols");

  for (const SymbolRef &Symbol : Obj->symbols())
    printSymbol(Symbol);
}

void WasmDumper::printSectionHeaders() {
  ListScope Group(W, "Sections");
  for (const SectionRef &Section : Obj->sections()) {
    const WasmSection &WasmSec = Obj->getWasmSection(Section);
    DictScope SectionD(W, "Section");
    W.printEnum("Type", WasmSec.Type, ArrayRef(WasmSectionTypes));
    W.printNumber("Size", static_cast<uint64_t>(WasmSec.Content.size()));
    W.printNumber("Offset", WasmSec.Offset);
    switch (WasmSec.Type) {
    case wasm::WASM_SEC_CUSTOM:
      W.printString("Name", WasmSec.Name);
      if (WasmSec.Name == "linking") {
        const wasm::WasmLinkingData &LinkingData = Obj->linkingData();
        if (!LinkingData.InitFunctions.empty()) {
          ListScope Group(W, "InitFunctions");
          for (const wasm::WasmInitFunc &F : LinkingData.InitFunctions)
            W.startLine() << F.Symbol << " (priority=" << F.Priority << ")\n";
        }
      }
      break;
    case wasm::WASM_SEC_DATA: {
      ListScope Group(W, "Segments");
      for (const WasmSegment &Segment : Obj->dataSegments()) {
        const wasm::WasmDataSegment &Seg = Segment.Data;
        DictScope Group(W, "Segment");
        if (!Seg.Name.empty())
          W.printString("Name", Seg.Name);
        W.printNumber("Size", static_cast<uint64_t>(Seg.Content.size()));
        if (Seg.Offset.Extended)
          llvm_unreachable("extended const exprs not supported");
        else if (Seg.Offset.Inst.Opcode == wasm::WASM_OPCODE_I32_CONST)
          W.printNumber("Offset", Seg.Offset.Inst.Value.Int32);
        else if (Seg.Offset.Inst.Opcode == wasm::WASM_OPCODE_I64_CONST)
          W.printNumber("Offset", Seg.Offset.Inst.Value.Int64);
        else if (Seg.Offset.Inst.Opcode == wasm::WASM_OPCODE_GLOBAL_GET) {
          ListScope Group(W, "Offset");
          W.printNumber("Global", Seg.Offset.Inst.Value.Global);
        } else
          llvm_unreachable("unknown init expr opcode");
      }
      break;
    }
    case wasm::WASM_SEC_MEMORY:
      ListScope Group(W, "Memories");
      for (const wasm::WasmLimits &Memory : Obj->memories()) {
        DictScope Group(W, "Memory");
        W.printNumber("MinPages", Memory.Minimum);
        if (Memory.Flags & wasm::WASM_LIMITS_FLAG_HAS_MAX) {
          W.printNumber("MaxPages", WasmSec.Offset);
        }
      }
      break;
    }

    if (opts::SectionRelocations) {
      ListScope D(W, "Relocations");
      for (const RelocationRef &Reloc : Section.relocations())
        printRelocation(Section, Reloc);
    }

    if (opts::SectionData) {
      W.printBinaryBlock("SectionData", WasmSec.Content);
    }
  }
}

void WasmDumper::printSymbol(const SymbolRef &Sym) {
  DictScope D(W, "Symbol");
  WasmSymbol Symbol = Obj->getWasmSymbol(Sym.getRawDataRefImpl());
  W.printString("Name", Symbol.Info.Name);
  W.printEnum("Type", Symbol.Info.Kind, ArrayRef(WasmSymbolTypes));
  W.printFlags("Flags", Symbol.Info.Flags, ArrayRef(WasmSymbolFlags));

  if (Symbol.Info.Flags & wasm::WASM_SYMBOL_UNDEFINED) {
    if (Symbol.Info.ImportName) {
      W.printString("ImportName", *Symbol.Info.ImportName);
    }
    if (Symbol.Info.ImportModule) {
      W.printString("ImportModule", *Symbol.Info.ImportModule);
    }
  }
  if (Symbol.Info.Kind != wasm::WASM_SYMBOL_TYPE_DATA) {
    W.printHex("ElementIndex", Symbol.Info.ElementIndex);
  } else if (!(Symbol.Info.Flags & wasm::WASM_SYMBOL_UNDEFINED)) {
    W.printHex("Offset", Symbol.Info.DataRef.Offset);
    W.printHex("Segment", Symbol.Info.DataRef.Segment);
    W.printHex("Size", Symbol.Info.DataRef.Size);
  }
}

} // namespace

namespace llvm {

std::unique_ptr<ObjDumper> createWasmDumper(const object::WasmObjectFile &Obj,
                                            ScopedPrinter &Writer) {
  return std::make_unique<WasmDumper>(&Obj, Writer);
}

} // namespace llvm
