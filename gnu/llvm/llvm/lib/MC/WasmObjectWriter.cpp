//===- lib/MC/WasmObjectWriter.cpp - Wasm File Writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Wasm object file writer information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/BinaryFormat/WasmTraits.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWasmObjectWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "mc"

namespace {

// When we create the indirect function table we start at 1, so that there is
// and empty slot at 0 and therefore calling a null function pointer will trap.
static const uint32_t InitialTableOffset = 1;

// For patching purposes, we need to remember where each section starts, both
// for patching up the section size field, and for patching up references to
// locations within the section.
struct SectionBookkeeping {
  // Where the size of the section is written.
  uint64_t SizeOffset;
  // Where the section header ends (without custom section name).
  uint64_t PayloadOffset;
  // Where the contents of the section starts.
  uint64_t ContentsOffset;
  uint32_t Index;
};

// A wasm data segment.  A wasm binary contains only a single data section
// but that can contain many segments, each with their own virtual location
// in memory.  Each MCSection data created by llvm is modeled as its own
// wasm data segment.
struct WasmDataSegment {
  MCSectionWasm *Section;
  StringRef Name;
  uint32_t InitFlags;
  uint64_t Offset;
  uint32_t Alignment;
  uint32_t LinkingFlags;
  SmallVector<char, 4> Data;
};

// A wasm function to be written into the function section.
struct WasmFunction {
  uint32_t SigIndex;
  MCSection *Section;
};

// A wasm global to be written into the global section.
struct WasmGlobal {
  wasm::WasmGlobalType Type;
  uint64_t InitialValue;
};

// Information about a single item which is part of a COMDAT.  For each data
// segment or function which is in the COMDAT, there is a corresponding
// WasmComdatEntry.
struct WasmComdatEntry {
  unsigned Kind;
  uint32_t Index;
};

// Information about a single relocation.
struct WasmRelocationEntry {
  uint64_t Offset;                   // Where is the relocation.
  const MCSymbolWasm *Symbol;        // The symbol to relocate with.
  int64_t Addend;                    // A value to add to the symbol.
  unsigned Type;                     // The type of the relocation.
  const MCSectionWasm *FixupSection; // The section the relocation is targeting.

  WasmRelocationEntry(uint64_t Offset, const MCSymbolWasm *Symbol,
                      int64_t Addend, unsigned Type,
                      const MCSectionWasm *FixupSection)
      : Offset(Offset), Symbol(Symbol), Addend(Addend), Type(Type),
        FixupSection(FixupSection) {}

  bool hasAddend() const { return wasm::relocTypeHasAddend(Type); }

  void print(raw_ostream &Out) const {
    Out << wasm::relocTypetoString(Type) << " Off=" << Offset
        << ", Sym=" << *Symbol << ", Addend=" << Addend
        << ", FixupSection=" << FixupSection->getName();
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const { print(dbgs()); }
#endif
};

static const uint32_t InvalidIndex = -1;

struct WasmCustomSection {

  StringRef Name;
  MCSectionWasm *Section;

  uint32_t OutputContentsOffset = 0;
  uint32_t OutputIndex = InvalidIndex;

  WasmCustomSection(StringRef Name, MCSectionWasm *Section)
      : Name(Name), Section(Section) {}
};

#if !defined(NDEBUG)
raw_ostream &operator<<(raw_ostream &OS, const WasmRelocationEntry &Rel) {
  Rel.print(OS);
  return OS;
}
#endif

// Write Value as an (unsigned) LEB value at offset Offset in Stream, padded
// to allow patching.
template <typename T, int W>
void writePatchableULEB(raw_pwrite_stream &Stream, T Value, uint64_t Offset) {
  uint8_t Buffer[W];
  unsigned SizeLen = encodeULEB128(Value, Buffer, W);
  assert(SizeLen == W);
  Stream.pwrite((char *)Buffer, SizeLen, Offset);
}

// Write Value as an signed LEB value at offset Offset in Stream, padded
// to allow patching.
template <typename T, int W>
void writePatchableSLEB(raw_pwrite_stream &Stream, T Value, uint64_t Offset) {
  uint8_t Buffer[W];
  unsigned SizeLen = encodeSLEB128(Value, Buffer, W);
  assert(SizeLen == W);
  Stream.pwrite((char *)Buffer, SizeLen, Offset);
}

static void writePatchableU32(raw_pwrite_stream &Stream, uint32_t Value,
                              uint64_t Offset) {
  writePatchableULEB<uint32_t, 5>(Stream, Value, Offset);
}

static void writePatchableS32(raw_pwrite_stream &Stream, int32_t Value,
                              uint64_t Offset) {
  writePatchableSLEB<int32_t, 5>(Stream, Value, Offset);
}

static void writePatchableU64(raw_pwrite_stream &Stream, uint64_t Value,
                              uint64_t Offset) {
  writePatchableSLEB<uint64_t, 10>(Stream, Value, Offset);
}

static void writePatchableS64(raw_pwrite_stream &Stream, int64_t Value,
                              uint64_t Offset) {
  writePatchableSLEB<int64_t, 10>(Stream, Value, Offset);
}

// Write Value as a plain integer value at offset Offset in Stream.
static void patchI32(raw_pwrite_stream &Stream, uint32_t Value,
                     uint64_t Offset) {
  uint8_t Buffer[4];
  support::endian::write32le(Buffer, Value);
  Stream.pwrite((char *)Buffer, sizeof(Buffer), Offset);
}

static void patchI64(raw_pwrite_stream &Stream, uint64_t Value,
                     uint64_t Offset) {
  uint8_t Buffer[8];
  support::endian::write64le(Buffer, Value);
  Stream.pwrite((char *)Buffer, sizeof(Buffer), Offset);
}

bool isDwoSection(const MCSection &Sec) {
  return Sec.getName().ends_with(".dwo");
}

class WasmObjectWriter : public MCObjectWriter {
  support::endian::Writer *W = nullptr;

  /// The target specific Wasm writer instance.
  std::unique_ptr<MCWasmObjectTargetWriter> TargetObjectWriter;

  // Relocations for fixing up references in the code section.
  std::vector<WasmRelocationEntry> CodeRelocations;
  // Relocations for fixing up references in the data section.
  std::vector<WasmRelocationEntry> DataRelocations;

  // Index values to use for fixing up call_indirect type indices.
  // Maps function symbols to the index of the type of the function
  DenseMap<const MCSymbolWasm *, uint32_t> TypeIndices;
  // Maps function symbols to the table element index space. Used
  // for TABLE_INDEX relocation types (i.e. address taken functions).
  DenseMap<const MCSymbolWasm *, uint32_t> TableIndices;
  // Maps function/global/table symbols to the
  // function/global/table/tag/section index space.
  DenseMap<const MCSymbolWasm *, uint32_t> WasmIndices;
  DenseMap<const MCSymbolWasm *, uint32_t> GOTIndices;
  // Maps data symbols to the Wasm segment and offset/size with the segment.
  DenseMap<const MCSymbolWasm *, wasm::WasmDataReference> DataLocations;

  // Stores output data (index, relocations, content offset) for custom
  // section.
  std::vector<WasmCustomSection> CustomSections;
  std::unique_ptr<WasmCustomSection> ProducersSection;
  std::unique_ptr<WasmCustomSection> TargetFeaturesSection;
  // Relocations for fixing up references in the custom sections.
  DenseMap<const MCSectionWasm *, std::vector<WasmRelocationEntry>>
      CustomSectionsRelocations;

  // Map from section to defining function symbol.
  DenseMap<const MCSection *, const MCSymbol *> SectionFunctions;

  DenseMap<wasm::WasmSignature, uint32_t> SignatureIndices;
  SmallVector<wasm::WasmSignature, 4> Signatures;
  SmallVector<WasmDataSegment, 4> DataSegments;
  unsigned NumFunctionImports = 0;
  unsigned NumGlobalImports = 0;
  unsigned NumTableImports = 0;
  unsigned NumTagImports = 0;
  uint32_t SectionCount = 0;

  enum class DwoMode {
    AllSections,
    NonDwoOnly,
    DwoOnly,
  };
  bool IsSplitDwarf = false;
  raw_pwrite_stream *OS = nullptr;
  raw_pwrite_stream *DwoOS = nullptr;

  // TargetObjectWriter wranppers.
  bool is64Bit() const { return TargetObjectWriter->is64Bit(); }
  bool isEmscripten() const { return TargetObjectWriter->isEmscripten(); }

  void startSection(SectionBookkeeping &Section, unsigned SectionId);
  void startCustomSection(SectionBookkeeping &Section, StringRef Name);
  void endSection(SectionBookkeeping &Section);

public:
  WasmObjectWriter(std::unique_ptr<MCWasmObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS_)
      : TargetObjectWriter(std::move(MOTW)), OS(&OS_) {}

  WasmObjectWriter(std::unique_ptr<MCWasmObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS_, raw_pwrite_stream &DwoOS_)
      : TargetObjectWriter(std::move(MOTW)), IsSplitDwarf(true), OS(&OS_),
        DwoOS(&DwoOS_) {}

private:
  void reset() override {
    CodeRelocations.clear();
    DataRelocations.clear();
    TypeIndices.clear();
    WasmIndices.clear();
    GOTIndices.clear();
    TableIndices.clear();
    DataLocations.clear();
    CustomSections.clear();
    ProducersSection.reset();
    TargetFeaturesSection.reset();
    CustomSectionsRelocations.clear();
    SignatureIndices.clear();
    Signatures.clear();
    DataSegments.clear();
    SectionFunctions.clear();
    NumFunctionImports = 0;
    NumGlobalImports = 0;
    NumTableImports = 0;
    MCObjectWriter::reset();
  }

  void writeHeader(const MCAssembler &Asm);

  void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override;

  void executePostLayoutBinding(MCAssembler &Asm) override;
  void prepareImports(SmallVectorImpl<wasm::WasmImport> &Imports,
                      MCAssembler &Asm);
  uint64_t writeObject(MCAssembler &Asm) override;

  uint64_t writeOneObject(MCAssembler &Asm, DwoMode Mode);

  void writeString(const StringRef Str) {
    encodeULEB128(Str.size(), W->OS);
    W->OS << Str;
  }

  void writeStringWithAlignment(const StringRef Str, unsigned Alignment);

  void writeI32(int32_t val) {
    char Buffer[4];
    support::endian::write32le(Buffer, val);
    W->OS.write(Buffer, sizeof(Buffer));
  }

  void writeI64(int64_t val) {
    char Buffer[8];
    support::endian::write64le(Buffer, val);
    W->OS.write(Buffer, sizeof(Buffer));
  }

  void writeValueType(wasm::ValType Ty) { W->OS << static_cast<char>(Ty); }

  void writeTypeSection(ArrayRef<wasm::WasmSignature> Signatures);
  void writeImportSection(ArrayRef<wasm::WasmImport> Imports, uint64_t DataSize,
                          uint32_t NumElements);
  void writeFunctionSection(ArrayRef<WasmFunction> Functions);
  void writeExportSection(ArrayRef<wasm::WasmExport> Exports);
  void writeElemSection(const MCSymbolWasm *IndirectFunctionTable,
                        ArrayRef<uint32_t> TableElems);
  void writeDataCountSection();
  uint32_t writeCodeSection(const MCAssembler &Asm,
                            ArrayRef<WasmFunction> Functions);
  uint32_t writeDataSection(const MCAssembler &Asm);
  void writeTagSection(ArrayRef<uint32_t> TagTypes);
  void writeGlobalSection(ArrayRef<wasm::WasmGlobal> Globals);
  void writeTableSection(ArrayRef<wasm::WasmTable> Tables);
  void writeRelocSection(uint32_t SectionIndex, StringRef Name,
                         std::vector<WasmRelocationEntry> &Relocations);
  void writeLinkingMetaDataSection(
      ArrayRef<wasm::WasmSymbolInfo> SymbolInfos,
      ArrayRef<std::pair<uint16_t, uint32_t>> InitFuncs,
      const std::map<StringRef, std::vector<WasmComdatEntry>> &Comdats);
  void writeCustomSection(WasmCustomSection &CustomSection,
                          const MCAssembler &Asm);
  void writeCustomRelocSections();

  uint64_t getProvisionalValue(const MCAssembler &Asm,
                               const WasmRelocationEntry &RelEntry);
  void applyRelocations(ArrayRef<WasmRelocationEntry> Relocations,
                        uint64_t ContentsOffset, const MCAssembler &Asm);

  uint32_t getRelocationIndexValue(const WasmRelocationEntry &RelEntry);
  uint32_t getFunctionType(const MCSymbolWasm &Symbol);
  uint32_t getTagType(const MCSymbolWasm &Symbol);
  void registerFunctionType(const MCSymbolWasm &Symbol);
  void registerTagType(const MCSymbolWasm &Symbol);
};

} // end anonymous namespace

// Write out a section header and a patchable section size field.
void WasmObjectWriter::startSection(SectionBookkeeping &Section,
                                    unsigned SectionId) {
  LLVM_DEBUG(dbgs() << "startSection " << SectionId << "\n");
  W->OS << char(SectionId);

  Section.SizeOffset = W->OS.tell();

  // The section size. We don't know the size yet, so reserve enough space
  // for any 32-bit value; we'll patch it later.
  encodeULEB128(0, W->OS, 5);

  // The position where the section starts, for measuring its size.
  Section.ContentsOffset = W->OS.tell();
  Section.PayloadOffset = W->OS.tell();
  Section.Index = SectionCount++;
}

// Write a string with extra paddings for trailing alignment
// TODO: support alignment at asm and llvm level?
void WasmObjectWriter::writeStringWithAlignment(const StringRef Str,
                                                unsigned Alignment) {

  // Calculate the encoded size of str length and add pads based on it and
  // alignment.
  raw_null_ostream NullOS;
  uint64_t StrSizeLength = encodeULEB128(Str.size(), NullOS);
  uint64_t Offset = W->OS.tell() + StrSizeLength + Str.size();
  uint64_t Paddings = offsetToAlignment(Offset, Align(Alignment));
  Offset += Paddings;

  // LEB128 greater than 5 bytes is invalid
  assert((StrSizeLength + Paddings) <= 5 && "too long string to align");

  encodeSLEB128(Str.size(), W->OS, StrSizeLength + Paddings);
  W->OS << Str;

  assert(W->OS.tell() == Offset && "invalid padding");
}

void WasmObjectWriter::startCustomSection(SectionBookkeeping &Section,
                                          StringRef Name) {
  LLVM_DEBUG(dbgs() << "startCustomSection " << Name << "\n");
  startSection(Section, wasm::WASM_SEC_CUSTOM);

  // The position where the section header ends, for measuring its size.
  Section.PayloadOffset = W->OS.tell();

  // Custom sections in wasm also have a string identifier.
  if (Name != "__clangast") {
    writeString(Name);
  } else {
    // The on-disk hashtable in clangast needs to be aligned by 4 bytes.
    writeStringWithAlignment(Name, 4);
  }

  // The position where the custom section starts.
  Section.ContentsOffset = W->OS.tell();
}

// Now that the section is complete and we know how big it is, patch up the
// section size field at the start of the section.
void WasmObjectWriter::endSection(SectionBookkeeping &Section) {
  uint64_t Size = W->OS.tell();
  // /dev/null doesn't support seek/tell and can report offset of 0.
  // Simply skip this patching in that case.
  if (!Size)
    return;

  Size -= Section.PayloadOffset;
  if (uint32_t(Size) != Size)
    report_fatal_error("section size does not fit in a uint32_t");

  LLVM_DEBUG(dbgs() << "endSection size=" << Size << "\n");

  // Write the final section size to the payload_len field, which follows
  // the section id byte.
  writePatchableU32(static_cast<raw_pwrite_stream &>(W->OS), Size,
                    Section.SizeOffset);
}

// Emit the Wasm header.
void WasmObjectWriter::writeHeader(const MCAssembler &Asm) {
  W->OS.write(wasm::WasmMagic, sizeof(wasm::WasmMagic));
  W->write<uint32_t>(wasm::WasmVersion);
}

void WasmObjectWriter::executePostLayoutBinding(MCAssembler &Asm) {
  // Some compilation units require the indirect function table to be present
  // but don't explicitly reference it.  This is the case for call_indirect
  // without the reference-types feature, and also function bitcasts in all
  // cases.  In those cases the __indirect_function_table has the
  // WASM_SYMBOL_NO_STRIP attribute.  Here we make sure this symbol makes it to
  // the assembler, if needed.
  if (auto *Sym = Asm.getContext().lookupSymbol("__indirect_function_table")) {
    const auto *WasmSym = static_cast<const MCSymbolWasm *>(Sym);
    if (WasmSym->isNoStrip())
      Asm.registerSymbol(*Sym);
  }

  // Build a map of sections to the function that defines them, for use
  // in recordRelocation.
  for (const MCSymbol &S : Asm.symbols()) {
    const auto &WS = static_cast<const MCSymbolWasm &>(S);
    if (WS.isDefined() && WS.isFunction() && !WS.isVariable()) {
      const auto &Sec = static_cast<const MCSectionWasm &>(S.getSection());
      auto Pair = SectionFunctions.insert(std::make_pair(&Sec, &S));
      if (!Pair.second)
        report_fatal_error("section already has a defining function: " +
                           Sec.getName());
    }
  }
}

void WasmObjectWriter::recordRelocation(MCAssembler &Asm,
                                        const MCFragment *Fragment,
                                        const MCFixup &Fixup, MCValue Target,
                                        uint64_t &FixedValue) {
  // The WebAssembly backend should never generate FKF_IsPCRel fixups
  assert(!(Asm.getBackend().getFixupKindInfo(Fixup.getKind()).Flags &
           MCFixupKindInfo::FKF_IsPCRel));

  const auto &FixupSection = cast<MCSectionWasm>(*Fragment->getParent());
  uint64_t C = Target.getConstant();
  uint64_t FixupOffset = Asm.getFragmentOffset(*Fragment) + Fixup.getOffset();
  MCContext &Ctx = Asm.getContext();
  bool IsLocRel = false;

  if (const MCSymbolRefExpr *RefB = Target.getSymB()) {

    const auto &SymB = cast<MCSymbolWasm>(RefB->getSymbol());

    if (FixupSection.isText()) {
      Ctx.reportError(Fixup.getLoc(),
                      Twine("symbol '") + SymB.getName() +
                          "' unsupported subtraction expression used in "
                          "relocation in code section.");
      return;
    }

    if (SymB.isUndefined()) {
      Ctx.reportError(Fixup.getLoc(),
                      Twine("symbol '") + SymB.getName() +
                          "' can not be undefined in a subtraction expression");
      return;
    }
    const MCSection &SecB = SymB.getSection();
    if (&SecB != &FixupSection) {
      Ctx.reportError(Fixup.getLoc(),
                      Twine("symbol '") + SymB.getName() +
                          "' can not be placed in a different section");
      return;
    }
    IsLocRel = true;
    C += FixupOffset - Asm.getSymbolOffset(SymB);
  }

  // We either rejected the fixup or folded B into C at this point.
  const MCSymbolRefExpr *RefA = Target.getSymA();
  const auto *SymA = cast<MCSymbolWasm>(&RefA->getSymbol());

  // The .init_array isn't translated as data, so don't do relocations in it.
  if (FixupSection.getName().starts_with(".init_array")) {
    SymA->setUsedInInitArray();
    return;
  }

  if (SymA->isVariable()) {
    const MCExpr *Expr = SymA->getVariableValue();
    if (const auto *Inner = dyn_cast<MCSymbolRefExpr>(Expr))
      if (Inner->getKind() == MCSymbolRefExpr::VK_WEAKREF)
        llvm_unreachable("weakref used in reloc not yet implemented");
  }

  // Put any constant offset in an addend. Offsets can be negative, and
  // LLVM expects wrapping, in contrast to wasm's immediates which can't
  // be negative and don't wrap.
  FixedValue = 0;

  unsigned Type =
      TargetObjectWriter->getRelocType(Target, Fixup, FixupSection, IsLocRel);

  // Absolute offset within a section or a function.
  // Currently only supported for metadata sections.
  // See: test/MC/WebAssembly/blockaddress.ll
  if ((Type == wasm::R_WASM_FUNCTION_OFFSET_I32 ||
       Type == wasm::R_WASM_FUNCTION_OFFSET_I64 ||
       Type == wasm::R_WASM_SECTION_OFFSET_I32) &&
      SymA->isDefined()) {
    // SymA can be a temp data symbol that represents a function (in which case
    // it needs to be replaced by the section symbol), [XXX and it apparently
    // later gets changed again to a func symbol?] or it can be a real
    // function symbol, in which case it can be left as-is.

    if (!FixupSection.isMetadata())
      report_fatal_error("relocations for function or section offsets are "
                         "only supported in metadata sections");

    const MCSymbol *SectionSymbol = nullptr;
    const MCSection &SecA = SymA->getSection();
    if (SecA.isText()) {
      auto SecSymIt = SectionFunctions.find(&SecA);
      if (SecSymIt == SectionFunctions.end())
        report_fatal_error("section doesn\'t have defining symbol");
      SectionSymbol = SecSymIt->second;
    } else {
      SectionSymbol = SecA.getBeginSymbol();
    }
    if (!SectionSymbol)
      report_fatal_error("section symbol is required for relocation");

    C += Asm.getSymbolOffset(*SymA);
    SymA = cast<MCSymbolWasm>(SectionSymbol);
  }

  if (Type == wasm::R_WASM_TABLE_INDEX_REL_SLEB ||
      Type == wasm::R_WASM_TABLE_INDEX_REL_SLEB64 ||
      Type == wasm::R_WASM_TABLE_INDEX_SLEB ||
      Type == wasm::R_WASM_TABLE_INDEX_SLEB64 ||
      Type == wasm::R_WASM_TABLE_INDEX_I32 ||
      Type == wasm::R_WASM_TABLE_INDEX_I64) {
    // TABLE_INDEX relocs implicitly use the default indirect function table.
    // We require the function table to have already been defined.
    auto TableName = "__indirect_function_table";
    MCSymbolWasm *Sym = cast_or_null<MCSymbolWasm>(Ctx.lookupSymbol(TableName));
    if (!Sym) {
      report_fatal_error("missing indirect function table symbol");
    } else {
      if (!Sym->isFunctionTable())
        report_fatal_error("__indirect_function_table symbol has wrong type");
      // Ensure that __indirect_function_table reaches the output.
      Sym->setNoStrip();
      Asm.registerSymbol(*Sym);
    }
  }

  // Relocation other than R_WASM_TYPE_INDEX_LEB are required to be
  // against a named symbol.
  if (Type != wasm::R_WASM_TYPE_INDEX_LEB) {
    if (SymA->getName().empty())
      report_fatal_error("relocations against un-named temporaries are not yet "
                         "supported by wasm");

    SymA->setUsedInReloc();
  }

  switch (RefA->getKind()) {
  case MCSymbolRefExpr::VK_GOT:
  case MCSymbolRefExpr::VK_WASM_GOT_TLS:
    SymA->setUsedInGOT();
    break;
  default:
    break;
  }

  WasmRelocationEntry Rec(FixupOffset, SymA, C, Type, &FixupSection);
  LLVM_DEBUG(dbgs() << "WasmReloc: " << Rec << "\n");

  if (FixupSection.isWasmData()) {
    DataRelocations.push_back(Rec);
  } else if (FixupSection.isText()) {
    CodeRelocations.push_back(Rec);
  } else if (FixupSection.isMetadata()) {
    CustomSectionsRelocations[&FixupSection].push_back(Rec);
  } else {
    llvm_unreachable("unexpected section type");
  }
}

// Compute a value to write into the code at the location covered
// by RelEntry. This value isn't used by the static linker; it just serves
// to make the object format more readable and more likely to be directly
// useable.
uint64_t
WasmObjectWriter::getProvisionalValue(const MCAssembler &Asm,
                                      const WasmRelocationEntry &RelEntry) {
  if ((RelEntry.Type == wasm::R_WASM_GLOBAL_INDEX_LEB ||
       RelEntry.Type == wasm::R_WASM_GLOBAL_INDEX_I32) &&
      !RelEntry.Symbol->isGlobal()) {
    assert(GOTIndices.count(RelEntry.Symbol) > 0 && "symbol not found in GOT index space");
    return GOTIndices[RelEntry.Symbol];
  }

  switch (RelEntry.Type) {
  case wasm::R_WASM_TABLE_INDEX_REL_SLEB:
  case wasm::R_WASM_TABLE_INDEX_REL_SLEB64:
  case wasm::R_WASM_TABLE_INDEX_SLEB:
  case wasm::R_WASM_TABLE_INDEX_SLEB64:
  case wasm::R_WASM_TABLE_INDEX_I32:
  case wasm::R_WASM_TABLE_INDEX_I64: {
    // Provisional value is table address of the resolved symbol itself
    const MCSymbolWasm *Base =
        cast<MCSymbolWasm>(Asm.getBaseSymbol(*RelEntry.Symbol));
    assert(Base->isFunction());
    if (RelEntry.Type == wasm::R_WASM_TABLE_INDEX_REL_SLEB ||
        RelEntry.Type == wasm::R_WASM_TABLE_INDEX_REL_SLEB64)
      return TableIndices[Base] - InitialTableOffset;
    else
      return TableIndices[Base];
  }
  case wasm::R_WASM_TYPE_INDEX_LEB:
    // Provisional value is same as the index
    return getRelocationIndexValue(RelEntry);
  case wasm::R_WASM_FUNCTION_INDEX_LEB:
  case wasm::R_WASM_FUNCTION_INDEX_I32:
  case wasm::R_WASM_GLOBAL_INDEX_LEB:
  case wasm::R_WASM_GLOBAL_INDEX_I32:
  case wasm::R_WASM_TAG_INDEX_LEB:
  case wasm::R_WASM_TABLE_NUMBER_LEB:
    // Provisional value is function/global/tag Wasm index
    assert(WasmIndices.count(RelEntry.Symbol) > 0 && "symbol not found in wasm index space");
    return WasmIndices[RelEntry.Symbol];
  case wasm::R_WASM_FUNCTION_OFFSET_I32:
  case wasm::R_WASM_FUNCTION_OFFSET_I64:
  case wasm::R_WASM_SECTION_OFFSET_I32: {
    if (!RelEntry.Symbol->isDefined())
      return 0;
    const auto &Section =
        static_cast<const MCSectionWasm &>(RelEntry.Symbol->getSection());
    return Section.getSectionOffset() + RelEntry.Addend;
  }
  case wasm::R_WASM_MEMORY_ADDR_LEB:
  case wasm::R_WASM_MEMORY_ADDR_LEB64:
  case wasm::R_WASM_MEMORY_ADDR_SLEB:
  case wasm::R_WASM_MEMORY_ADDR_SLEB64:
  case wasm::R_WASM_MEMORY_ADDR_REL_SLEB:
  case wasm::R_WASM_MEMORY_ADDR_REL_SLEB64:
  case wasm::R_WASM_MEMORY_ADDR_I32:
  case wasm::R_WASM_MEMORY_ADDR_I64:
  case wasm::R_WASM_MEMORY_ADDR_TLS_SLEB:
  case wasm::R_WASM_MEMORY_ADDR_TLS_SLEB64:
  case wasm::R_WASM_MEMORY_ADDR_LOCREL_I32: {
    // Provisional value is address of the global plus the offset
    // For undefined symbols, use zero
    if (!RelEntry.Symbol->isDefined())
      return 0;
    const wasm::WasmDataReference &SymRef = DataLocations[RelEntry.Symbol];
    const WasmDataSegment &Segment = DataSegments[SymRef.Segment];
    // Ignore overflow. LLVM allows address arithmetic to silently wrap.
    return Segment.Offset + SymRef.Offset + RelEntry.Addend;
  }
  default:
    llvm_unreachable("invalid relocation type");
  }
}

static void addData(SmallVectorImpl<char> &DataBytes,
                    MCSectionWasm &DataSection) {
  LLVM_DEBUG(errs() << "addData: " << DataSection.getName() << "\n");

  DataBytes.resize(alignTo(DataBytes.size(), DataSection.getAlign()));

  for (const MCFragment &Frag : DataSection) {
    if (Frag.hasInstructions())
      report_fatal_error("only data supported in data sections");

    if (auto *Align = dyn_cast<MCAlignFragment>(&Frag)) {
      if (Align->getValueSize() != 1)
        report_fatal_error("only byte values supported for alignment");
      // If nops are requested, use zeros, as this is the data section.
      uint8_t Value = Align->hasEmitNops() ? 0 : Align->getValue();
      uint64_t Size =
          std::min<uint64_t>(alignTo(DataBytes.size(), Align->getAlignment()),
                             DataBytes.size() + Align->getMaxBytesToEmit());
      DataBytes.resize(Size, Value);
    } else if (auto *Fill = dyn_cast<MCFillFragment>(&Frag)) {
      int64_t NumValues;
      if (!Fill->getNumValues().evaluateAsAbsolute(NumValues))
        llvm_unreachable("The fill should be an assembler constant");
      DataBytes.insert(DataBytes.end(), Fill->getValueSize() * NumValues,
                       Fill->getValue());
    } else if (auto *LEB = dyn_cast<MCLEBFragment>(&Frag)) {
      const SmallVectorImpl<char> &Contents = LEB->getContents();
      llvm::append_range(DataBytes, Contents);
    } else {
      const auto &DataFrag = cast<MCDataFragment>(Frag);
      const SmallVectorImpl<char> &Contents = DataFrag.getContents();
      llvm::append_range(DataBytes, Contents);
    }
  }

  LLVM_DEBUG(dbgs() << "addData -> " << DataBytes.size() << "\n");
}

uint32_t
WasmObjectWriter::getRelocationIndexValue(const WasmRelocationEntry &RelEntry) {
  if (RelEntry.Type == wasm::R_WASM_TYPE_INDEX_LEB) {
    if (!TypeIndices.count(RelEntry.Symbol))
      report_fatal_error("symbol not found in type index space: " +
                         RelEntry.Symbol->getName());
    return TypeIndices[RelEntry.Symbol];
  }

  return RelEntry.Symbol->getIndex();
}

// Apply the portions of the relocation records that we can handle ourselves
// directly.
void WasmObjectWriter::applyRelocations(
    ArrayRef<WasmRelocationEntry> Relocations, uint64_t ContentsOffset,
    const MCAssembler &Asm) {
  auto &Stream = static_cast<raw_pwrite_stream &>(W->OS);
  for (const WasmRelocationEntry &RelEntry : Relocations) {
    uint64_t Offset = ContentsOffset +
                      RelEntry.FixupSection->getSectionOffset() +
                      RelEntry.Offset;

    LLVM_DEBUG(dbgs() << "applyRelocation: " << RelEntry << "\n");
    uint64_t Value = getProvisionalValue(Asm, RelEntry);

    switch (RelEntry.Type) {
    case wasm::R_WASM_FUNCTION_INDEX_LEB:
    case wasm::R_WASM_TYPE_INDEX_LEB:
    case wasm::R_WASM_GLOBAL_INDEX_LEB:
    case wasm::R_WASM_MEMORY_ADDR_LEB:
    case wasm::R_WASM_TAG_INDEX_LEB:
    case wasm::R_WASM_TABLE_NUMBER_LEB:
      writePatchableU32(Stream, Value, Offset);
      break;
    case wasm::R_WASM_MEMORY_ADDR_LEB64:
      writePatchableU64(Stream, Value, Offset);
      break;
    case wasm::R_WASM_TABLE_INDEX_I32:
    case wasm::R_WASM_MEMORY_ADDR_I32:
    case wasm::R_WASM_FUNCTION_OFFSET_I32:
    case wasm::R_WASM_FUNCTION_INDEX_I32:
    case wasm::R_WASM_SECTION_OFFSET_I32:
    case wasm::R_WASM_GLOBAL_INDEX_I32:
    case wasm::R_WASM_MEMORY_ADDR_LOCREL_I32:
      patchI32(Stream, Value, Offset);
      break;
    case wasm::R_WASM_TABLE_INDEX_I64:
    case wasm::R_WASM_MEMORY_ADDR_I64:
    case wasm::R_WASM_FUNCTION_OFFSET_I64:
      patchI64(Stream, Value, Offset);
      break;
    case wasm::R_WASM_TABLE_INDEX_SLEB:
    case wasm::R_WASM_TABLE_INDEX_REL_SLEB:
    case wasm::R_WASM_MEMORY_ADDR_SLEB:
    case wasm::R_WASM_MEMORY_ADDR_REL_SLEB:
    case wasm::R_WASM_MEMORY_ADDR_TLS_SLEB:
      writePatchableS32(Stream, Value, Offset);
      break;
    case wasm::R_WASM_TABLE_INDEX_SLEB64:
    case wasm::R_WASM_TABLE_INDEX_REL_SLEB64:
    case wasm::R_WASM_MEMORY_ADDR_SLEB64:
    case wasm::R_WASM_MEMORY_ADDR_REL_SLEB64:
    case wasm::R_WASM_MEMORY_ADDR_TLS_SLEB64:
      writePatchableS64(Stream, Value, Offset);
      break;
    default:
      llvm_unreachable("invalid relocation type");
    }
  }
}

void WasmObjectWriter::writeTypeSection(
    ArrayRef<wasm::WasmSignature> Signatures) {
  if (Signatures.empty())
    return;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_TYPE);

  encodeULEB128(Signatures.size(), W->OS);

  for (const wasm::WasmSignature &Sig : Signatures) {
    W->OS << char(wasm::WASM_TYPE_FUNC);
    encodeULEB128(Sig.Params.size(), W->OS);
    for (wasm::ValType Ty : Sig.Params)
      writeValueType(Ty);
    encodeULEB128(Sig.Returns.size(), W->OS);
    for (wasm::ValType Ty : Sig.Returns)
      writeValueType(Ty);
  }

  endSection(Section);
}

void WasmObjectWriter::writeImportSection(ArrayRef<wasm::WasmImport> Imports,
                                          uint64_t DataSize,
                                          uint32_t NumElements) {
  if (Imports.empty())
    return;

  uint64_t NumPages = (DataSize + wasm::WasmPageSize - 1) / wasm::WasmPageSize;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_IMPORT);

  encodeULEB128(Imports.size(), W->OS);
  for (const wasm::WasmImport &Import : Imports) {
    writeString(Import.Module);
    writeString(Import.Field);
    W->OS << char(Import.Kind);

    switch (Import.Kind) {
    case wasm::WASM_EXTERNAL_FUNCTION:
      encodeULEB128(Import.SigIndex, W->OS);
      break;
    case wasm::WASM_EXTERNAL_GLOBAL:
      W->OS << char(Import.Global.Type);
      W->OS << char(Import.Global.Mutable ? 1 : 0);
      break;
    case wasm::WASM_EXTERNAL_MEMORY:
      encodeULEB128(Import.Memory.Flags, W->OS);
      encodeULEB128(NumPages, W->OS); // initial
      break;
    case wasm::WASM_EXTERNAL_TABLE:
      W->OS << char(Import.Table.ElemType);
      encodeULEB128(Import.Table.Limits.Flags, W->OS);
      encodeULEB128(NumElements, W->OS); // initial
      break;
    case wasm::WASM_EXTERNAL_TAG:
      W->OS << char(0); // Reserved 'attribute' field
      encodeULEB128(Import.SigIndex, W->OS);
      break;
    default:
      llvm_unreachable("unsupported import kind");
    }
  }

  endSection(Section);
}

void WasmObjectWriter::writeFunctionSection(ArrayRef<WasmFunction> Functions) {
  if (Functions.empty())
    return;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_FUNCTION);

  encodeULEB128(Functions.size(), W->OS);
  for (const WasmFunction &Func : Functions)
    encodeULEB128(Func.SigIndex, W->OS);

  endSection(Section);
}

void WasmObjectWriter::writeTagSection(ArrayRef<uint32_t> TagTypes) {
  if (TagTypes.empty())
    return;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_TAG);

  encodeULEB128(TagTypes.size(), W->OS);
  for (uint32_t Index : TagTypes) {
    W->OS << char(0); // Reserved 'attribute' field
    encodeULEB128(Index, W->OS);
  }

  endSection(Section);
}

void WasmObjectWriter::writeGlobalSection(ArrayRef<wasm::WasmGlobal> Globals) {
  if (Globals.empty())
    return;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_GLOBAL);

  encodeULEB128(Globals.size(), W->OS);
  for (const wasm::WasmGlobal &Global : Globals) {
    encodeULEB128(Global.Type.Type, W->OS);
    W->OS << char(Global.Type.Mutable);
    if (Global.InitExpr.Extended) {
      llvm_unreachable("extected init expressions not supported");
    } else {
      W->OS << char(Global.InitExpr.Inst.Opcode);
      switch (Global.Type.Type) {
      case wasm::WASM_TYPE_I32:
        encodeSLEB128(0, W->OS);
        break;
      case wasm::WASM_TYPE_I64:
        encodeSLEB128(0, W->OS);
        break;
      case wasm::WASM_TYPE_F32:
        writeI32(0);
        break;
      case wasm::WASM_TYPE_F64:
        writeI64(0);
        break;
      case wasm::WASM_TYPE_EXTERNREF:
        writeValueType(wasm::ValType::EXTERNREF);
        break;
      default:
        llvm_unreachable("unexpected type");
      }
    }
    W->OS << char(wasm::WASM_OPCODE_END);
  }

  endSection(Section);
}

void WasmObjectWriter::writeTableSection(ArrayRef<wasm::WasmTable> Tables) {
  if (Tables.empty())
    return;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_TABLE);

  encodeULEB128(Tables.size(), W->OS);
  for (const wasm::WasmTable &Table : Tables) {
    assert(Table.Type.ElemType != wasm::ValType::OTHERREF &&
           "Cannot encode general ref-typed tables");
    encodeULEB128((uint32_t)Table.Type.ElemType, W->OS);
    encodeULEB128(Table.Type.Limits.Flags, W->OS);
    encodeULEB128(Table.Type.Limits.Minimum, W->OS);
    if (Table.Type.Limits.Flags & wasm::WASM_LIMITS_FLAG_HAS_MAX)
      encodeULEB128(Table.Type.Limits.Maximum, W->OS);
  }
  endSection(Section);
}

void WasmObjectWriter::writeExportSection(ArrayRef<wasm::WasmExport> Exports) {
  if (Exports.empty())
    return;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_EXPORT);

  encodeULEB128(Exports.size(), W->OS);
  for (const wasm::WasmExport &Export : Exports) {
    writeString(Export.Name);
    W->OS << char(Export.Kind);
    encodeULEB128(Export.Index, W->OS);
  }

  endSection(Section);
}

void WasmObjectWriter::writeElemSection(
    const MCSymbolWasm *IndirectFunctionTable, ArrayRef<uint32_t> TableElems) {
  if (TableElems.empty())
    return;

  assert(IndirectFunctionTable);

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_ELEM);

  encodeULEB128(1, W->OS); // number of "segments"

  assert(WasmIndices.count(IndirectFunctionTable));
  uint32_t TableNumber = WasmIndices.find(IndirectFunctionTable)->second;
  uint32_t Flags = 0;
  if (TableNumber)
    Flags |= wasm::WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER;
  encodeULEB128(Flags, W->OS);
  if (Flags & wasm::WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER)
    encodeULEB128(TableNumber, W->OS); // the table number

  // init expr for starting offset
  W->OS << char(is64Bit() ? wasm::WASM_OPCODE_I64_CONST
                          : wasm::WASM_OPCODE_I32_CONST);
  encodeSLEB128(InitialTableOffset, W->OS);
  W->OS << char(wasm::WASM_OPCODE_END);

  if (Flags & wasm::WASM_ELEM_SEGMENT_MASK_HAS_ELEM_KIND) {
    // We only write active function table initializers, for which the elem kind
    // is specified to be written as 0x00 and interpreted to mean "funcref".
    const uint8_t ElemKind = 0;
    W->OS << ElemKind;
  }

  encodeULEB128(TableElems.size(), W->OS);
  for (uint32_t Elem : TableElems)
    encodeULEB128(Elem, W->OS);

  endSection(Section);
}

void WasmObjectWriter::writeDataCountSection() {
  if (DataSegments.empty())
    return;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_DATACOUNT);
  encodeULEB128(DataSegments.size(), W->OS);
  endSection(Section);
}

uint32_t WasmObjectWriter::writeCodeSection(const MCAssembler &Asm,
                                            ArrayRef<WasmFunction> Functions) {
  if (Functions.empty())
    return 0;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_CODE);

  encodeULEB128(Functions.size(), W->OS);

  for (const WasmFunction &Func : Functions) {
    auto *FuncSection = static_cast<MCSectionWasm *>(Func.Section);

    int64_t Size = Asm.getSectionAddressSize(*FuncSection);
    encodeULEB128(Size, W->OS);
    FuncSection->setSectionOffset(W->OS.tell() - Section.ContentsOffset);
    Asm.writeSectionData(W->OS, FuncSection);
  }

  // Apply fixups.
  applyRelocations(CodeRelocations, Section.ContentsOffset, Asm);

  endSection(Section);
  return Section.Index;
}

uint32_t WasmObjectWriter::writeDataSection(const MCAssembler &Asm) {
  if (DataSegments.empty())
    return 0;

  SectionBookkeeping Section;
  startSection(Section, wasm::WASM_SEC_DATA);

  encodeULEB128(DataSegments.size(), W->OS); // count

  for (const WasmDataSegment &Segment : DataSegments) {
    encodeULEB128(Segment.InitFlags, W->OS); // flags
    if (Segment.InitFlags & wasm::WASM_DATA_SEGMENT_HAS_MEMINDEX)
      encodeULEB128(0, W->OS); // memory index
    if ((Segment.InitFlags & wasm::WASM_DATA_SEGMENT_IS_PASSIVE) == 0) {
      W->OS << char(is64Bit() ? wasm::WASM_OPCODE_I64_CONST
                              : wasm::WASM_OPCODE_I32_CONST);
      encodeSLEB128(Segment.Offset, W->OS); // offset
      W->OS << char(wasm::WASM_OPCODE_END);
    }
    encodeULEB128(Segment.Data.size(), W->OS); // size
    Segment.Section->setSectionOffset(W->OS.tell() - Section.ContentsOffset);
    W->OS << Segment.Data; // data
  }

  // Apply fixups.
  applyRelocations(DataRelocations, Section.ContentsOffset, Asm);

  endSection(Section);
  return Section.Index;
}

void WasmObjectWriter::writeRelocSection(
    uint32_t SectionIndex, StringRef Name,
    std::vector<WasmRelocationEntry> &Relocs) {
  // See: https://github.com/WebAssembly/tool-conventions/blob/main/Linking.md
  // for descriptions of the reloc sections.

  if (Relocs.empty())
    return;

  // First, ensure the relocations are sorted in offset order.  In general they
  // should already be sorted since `recordRelocation` is called in offset
  // order, but for the code section we combine many MC sections into single
  // wasm section, and this order is determined by the order of Asm.Symbols()
  // not the sections order.
  llvm::stable_sort(
      Relocs, [](const WasmRelocationEntry &A, const WasmRelocationEntry &B) {
        return (A.Offset + A.FixupSection->getSectionOffset()) <
               (B.Offset + B.FixupSection->getSectionOffset());
      });

  SectionBookkeeping Section;
  startCustomSection(Section, std::string("reloc.") + Name.str());

  encodeULEB128(SectionIndex, W->OS);
  encodeULEB128(Relocs.size(), W->OS);
  for (const WasmRelocationEntry &RelEntry : Relocs) {
    uint64_t Offset =
        RelEntry.Offset + RelEntry.FixupSection->getSectionOffset();
    uint32_t Index = getRelocationIndexValue(RelEntry);

    W->OS << char(RelEntry.Type);
    encodeULEB128(Offset, W->OS);
    encodeULEB128(Index, W->OS);
    if (RelEntry.hasAddend())
      encodeSLEB128(RelEntry.Addend, W->OS);
  }

  endSection(Section);
}

void WasmObjectWriter::writeCustomRelocSections() {
  for (const auto &Sec : CustomSections) {
    auto &Relocations = CustomSectionsRelocations[Sec.Section];
    writeRelocSection(Sec.OutputIndex, Sec.Name, Relocations);
  }
}

void WasmObjectWriter::writeLinkingMetaDataSection(
    ArrayRef<wasm::WasmSymbolInfo> SymbolInfos,
    ArrayRef<std::pair<uint16_t, uint32_t>> InitFuncs,
    const std::map<StringRef, std::vector<WasmComdatEntry>> &Comdats) {
  SectionBookkeeping Section;
  startCustomSection(Section, "linking");
  encodeULEB128(wasm::WasmMetadataVersion, W->OS);

  SectionBookkeeping SubSection;
  if (SymbolInfos.size() != 0) {
    startSection(SubSection, wasm::WASM_SYMBOL_TABLE);
    encodeULEB128(SymbolInfos.size(), W->OS);
    for (const wasm::WasmSymbolInfo &Sym : SymbolInfos) {
      encodeULEB128(Sym.Kind, W->OS);
      encodeULEB128(Sym.Flags, W->OS);
      switch (Sym.Kind) {
      case wasm::WASM_SYMBOL_TYPE_FUNCTION:
      case wasm::WASM_SYMBOL_TYPE_GLOBAL:
      case wasm::WASM_SYMBOL_TYPE_TAG:
      case wasm::WASM_SYMBOL_TYPE_TABLE:
        encodeULEB128(Sym.ElementIndex, W->OS);
        if ((Sym.Flags & wasm::WASM_SYMBOL_UNDEFINED) == 0 ||
            (Sym.Flags & wasm::WASM_SYMBOL_EXPLICIT_NAME) != 0)
          writeString(Sym.Name);
        break;
      case wasm::WASM_SYMBOL_TYPE_DATA:
        writeString(Sym.Name);
        if ((Sym.Flags & wasm::WASM_SYMBOL_UNDEFINED) == 0) {
          encodeULEB128(Sym.DataRef.Segment, W->OS);
          encodeULEB128(Sym.DataRef.Offset, W->OS);
          encodeULEB128(Sym.DataRef.Size, W->OS);
        }
        break;
      case wasm::WASM_SYMBOL_TYPE_SECTION: {
        const uint32_t SectionIndex =
            CustomSections[Sym.ElementIndex].OutputIndex;
        encodeULEB128(SectionIndex, W->OS);
        break;
      }
      default:
        llvm_unreachable("unexpected kind");
      }
    }
    endSection(SubSection);
  }

  if (DataSegments.size()) {
    startSection(SubSection, wasm::WASM_SEGMENT_INFO);
    encodeULEB128(DataSegments.size(), W->OS);
    for (const WasmDataSegment &Segment : DataSegments) {
      writeString(Segment.Name);
      encodeULEB128(Segment.Alignment, W->OS);
      encodeULEB128(Segment.LinkingFlags, W->OS);
    }
    endSection(SubSection);
  }

  if (!InitFuncs.empty()) {
    startSection(SubSection, wasm::WASM_INIT_FUNCS);
    encodeULEB128(InitFuncs.size(), W->OS);
    for (auto &StartFunc : InitFuncs) {
      encodeULEB128(StartFunc.first, W->OS);  // priority
      encodeULEB128(StartFunc.second, W->OS); // function index
    }
    endSection(SubSection);
  }

  if (Comdats.size()) {
    startSection(SubSection, wasm::WASM_COMDAT_INFO);
    encodeULEB128(Comdats.size(), W->OS);
    for (const auto &C : Comdats) {
      writeString(C.first);
      encodeULEB128(0, W->OS); // flags for future use
      encodeULEB128(C.second.size(), W->OS);
      for (const WasmComdatEntry &Entry : C.second) {
        encodeULEB128(Entry.Kind, W->OS);
        encodeULEB128(Entry.Index, W->OS);
      }
    }
    endSection(SubSection);
  }

  endSection(Section);
}

void WasmObjectWriter::writeCustomSection(WasmCustomSection &CustomSection,
                                          const MCAssembler &Asm) {
  SectionBookkeeping Section;
  auto *Sec = CustomSection.Section;
  startCustomSection(Section, CustomSection.Name);

  Sec->setSectionOffset(W->OS.tell() - Section.ContentsOffset);
  Asm.writeSectionData(W->OS, Sec);

  CustomSection.OutputContentsOffset = Section.ContentsOffset;
  CustomSection.OutputIndex = Section.Index;

  endSection(Section);

  // Apply fixups.
  auto &Relocations = CustomSectionsRelocations[CustomSection.Section];
  applyRelocations(Relocations, CustomSection.OutputContentsOffset, Asm);
}

uint32_t WasmObjectWriter::getFunctionType(const MCSymbolWasm &Symbol) {
  assert(Symbol.isFunction());
  assert(TypeIndices.count(&Symbol));
  return TypeIndices[&Symbol];
}

uint32_t WasmObjectWriter::getTagType(const MCSymbolWasm &Symbol) {
  assert(Symbol.isTag());
  assert(TypeIndices.count(&Symbol));
  return TypeIndices[&Symbol];
}

void WasmObjectWriter::registerFunctionType(const MCSymbolWasm &Symbol) {
  assert(Symbol.isFunction());

  wasm::WasmSignature S;

  if (auto *Sig = Symbol.getSignature()) {
    S.Returns = Sig->Returns;
    S.Params = Sig->Params;
  }

  auto Pair = SignatureIndices.insert(std::make_pair(S, Signatures.size()));
  if (Pair.second)
    Signatures.push_back(S);
  TypeIndices[&Symbol] = Pair.first->second;

  LLVM_DEBUG(dbgs() << "registerFunctionType: " << Symbol
                    << " new:" << Pair.second << "\n");
  LLVM_DEBUG(dbgs() << "  -> type index: " << Pair.first->second << "\n");
}

void WasmObjectWriter::registerTagType(const MCSymbolWasm &Symbol) {
  assert(Symbol.isTag());

  // TODO Currently we don't generate imported exceptions, but if we do, we
  // should have a way of infering types of imported exceptions.
  wasm::WasmSignature S;
  if (auto *Sig = Symbol.getSignature()) {
    S.Returns = Sig->Returns;
    S.Params = Sig->Params;
  }

  auto Pair = SignatureIndices.insert(std::make_pair(S, Signatures.size()));
  if (Pair.second)
    Signatures.push_back(S);
  TypeIndices[&Symbol] = Pair.first->second;

  LLVM_DEBUG(dbgs() << "registerTagType: " << Symbol << " new:" << Pair.second
                    << "\n");
  LLVM_DEBUG(dbgs() << "  -> type index: " << Pair.first->second << "\n");
}

static bool isInSymtab(const MCSymbolWasm &Sym) {
  if (Sym.isUsedInReloc() || Sym.isUsedInInitArray())
    return true;

  if (Sym.isComdat() && !Sym.isDefined())
    return false;

  if (Sym.isTemporary())
    return false;

  if (Sym.isSection())
    return false;

  if (Sym.omitFromLinkingSection())
    return false;

  return true;
}

static bool isSectionReferenced(MCAssembler &Asm, MCSectionWasm &Section) {
  StringRef SectionName = Section.getName();

  for (const MCSymbol &S : Asm.symbols()) {
    const auto &WS = static_cast<const MCSymbolWasm &>(S);
    if (WS.isData() && WS.isInSection()) {
      auto &RefSection = static_cast<MCSectionWasm &>(WS.getSection());
      if (RefSection.getName() == SectionName) {
        return true;
      }
    }
  }

  return false;
}

void WasmObjectWriter::prepareImports(
    SmallVectorImpl<wasm::WasmImport> &Imports, MCAssembler &Asm) {
  // For now, always emit the memory import, since loads and stores are not
  // valid without it. In the future, we could perhaps be more clever and omit
  // it if there are no loads or stores.
  wasm::WasmImport MemImport;
  MemImport.Module = "env";
  MemImport.Field = "__linear_memory";
  MemImport.Kind = wasm::WASM_EXTERNAL_MEMORY;
  MemImport.Memory.Flags = is64Bit() ? wasm::WASM_LIMITS_FLAG_IS_64
                                     : wasm::WASM_LIMITS_FLAG_NONE;
  Imports.push_back(MemImport);

  // Populate SignatureIndices, and Imports and WasmIndices for undefined
  // symbols.  This must be done before populating WasmIndices for defined
  // symbols.
  for (const MCSymbol &S : Asm.symbols()) {
    const auto &WS = static_cast<const MCSymbolWasm &>(S);

    // Register types for all functions, including those with private linkage
    // (because wasm always needs a type signature).
    if (WS.isFunction()) {
      const auto *BS = Asm.getBaseSymbol(S);
      if (!BS)
        report_fatal_error(Twine(S.getName()) +
                           ": absolute addressing not supported!");
      registerFunctionType(*cast<MCSymbolWasm>(BS));
    }

    if (WS.isTag())
      registerTagType(WS);

    if (WS.isTemporary())
      continue;

    // If the symbol is not defined in this translation unit, import it.
    if (!WS.isDefined() && !WS.isComdat()) {
      if (WS.isFunction()) {
        wasm::WasmImport Import;
        Import.Module = WS.getImportModule();
        Import.Field = WS.getImportName();
        Import.Kind = wasm::WASM_EXTERNAL_FUNCTION;
        Import.SigIndex = getFunctionType(WS);
        Imports.push_back(Import);
        assert(WasmIndices.count(&WS) == 0);
        WasmIndices[&WS] = NumFunctionImports++;
      } else if (WS.isGlobal()) {
        if (WS.isWeak())
          report_fatal_error("undefined global symbol cannot be weak");

        wasm::WasmImport Import;
        Import.Field = WS.getImportName();
        Import.Kind = wasm::WASM_EXTERNAL_GLOBAL;
        Import.Module = WS.getImportModule();
        Import.Global = WS.getGlobalType();
        Imports.push_back(Import);
        assert(WasmIndices.count(&WS) == 0);
        WasmIndices[&WS] = NumGlobalImports++;
      } else if (WS.isTag()) {
        if (WS.isWeak())
          report_fatal_error("undefined tag symbol cannot be weak");

        wasm::WasmImport Import;
        Import.Module = WS.getImportModule();
        Import.Field = WS.getImportName();
        Import.Kind = wasm::WASM_EXTERNAL_TAG;
        Import.SigIndex = getTagType(WS);
        Imports.push_back(Import);
        assert(WasmIndices.count(&WS) == 0);
        WasmIndices[&WS] = NumTagImports++;
      } else if (WS.isTable()) {
        if (WS.isWeak())
          report_fatal_error("undefined table symbol cannot be weak");

        wasm::WasmImport Import;
        Import.Module = WS.getImportModule();
        Import.Field = WS.getImportName();
        Import.Kind = wasm::WASM_EXTERNAL_TABLE;
        Import.Table = WS.getTableType();
        Imports.push_back(Import);
        assert(WasmIndices.count(&WS) == 0);
        WasmIndices[&WS] = NumTableImports++;
      }
    }
  }

  // Add imports for GOT globals
  for (const MCSymbol &S : Asm.symbols()) {
    const auto &WS = static_cast<const MCSymbolWasm &>(S);
    if (WS.isUsedInGOT()) {
      wasm::WasmImport Import;
      if (WS.isFunction())
        Import.Module = "GOT.func";
      else
        Import.Module = "GOT.mem";
      Import.Field = WS.getName();
      Import.Kind = wasm::WASM_EXTERNAL_GLOBAL;
      Import.Global = {wasm::WASM_TYPE_I32, true};
      Imports.push_back(Import);
      assert(GOTIndices.count(&WS) == 0);
      GOTIndices[&WS] = NumGlobalImports++;
    }
  }
}

uint64_t WasmObjectWriter::writeObject(MCAssembler &Asm) {
  support::endian::Writer MainWriter(*OS, llvm::endianness::little);
  W = &MainWriter;
  if (IsSplitDwarf) {
    uint64_t TotalSize = writeOneObject(Asm, DwoMode::NonDwoOnly);
    assert(DwoOS);
    support::endian::Writer DwoWriter(*DwoOS, llvm::endianness::little);
    W = &DwoWriter;
    return TotalSize + writeOneObject(Asm, DwoMode::DwoOnly);
  } else {
    return writeOneObject(Asm, DwoMode::AllSections);
  }
}

uint64_t WasmObjectWriter::writeOneObject(MCAssembler &Asm,
                                          DwoMode Mode) {
  uint64_t StartOffset = W->OS.tell();
  SectionCount = 0;
  CustomSections.clear();

  LLVM_DEBUG(dbgs() << "WasmObjectWriter::writeObject\n");

  // Collect information from the available symbols.
  SmallVector<WasmFunction, 4> Functions;
  SmallVector<uint32_t, 4> TableElems;
  SmallVector<wasm::WasmImport, 4> Imports;
  SmallVector<wasm::WasmExport, 4> Exports;
  SmallVector<uint32_t, 2> TagTypes;
  SmallVector<wasm::WasmGlobal, 1> Globals;
  SmallVector<wasm::WasmTable, 1> Tables;
  SmallVector<wasm::WasmSymbolInfo, 4> SymbolInfos;
  SmallVector<std::pair<uint16_t, uint32_t>, 2> InitFuncs;
  std::map<StringRef, std::vector<WasmComdatEntry>> Comdats;
  uint64_t DataSize = 0;
  if (Mode != DwoMode::DwoOnly)
    prepareImports(Imports, Asm);

  // Populate DataSegments and CustomSections, which must be done before
  // populating DataLocations.
  for (MCSection &Sec : Asm) {
    auto &Section = static_cast<MCSectionWasm &>(Sec);
    StringRef SectionName = Section.getName();

    if (Mode == DwoMode::NonDwoOnly && isDwoSection(Sec))
      continue;
    if (Mode == DwoMode::DwoOnly && !isDwoSection(Sec))
      continue;

    LLVM_DEBUG(dbgs() << "Processing Section " << SectionName << "  group "
                      << Section.getGroup() << "\n";);

    // .init_array sections are handled specially elsewhere, include them in
    // data segments if and only if referenced by a symbol.
    if (SectionName.starts_with(".init_array") &&
        !isSectionReferenced(Asm, Section))
      continue;

    // Code is handled separately
    if (Section.isText())
      continue;

    if (Section.isWasmData()) {
      uint32_t SegmentIndex = DataSegments.size();
      DataSize = alignTo(DataSize, Section.getAlign());
      DataSegments.emplace_back();
      WasmDataSegment &Segment = DataSegments.back();
      Segment.Name = SectionName;
      Segment.InitFlags = Section.getPassive()
                              ? (uint32_t)wasm::WASM_DATA_SEGMENT_IS_PASSIVE
                              : 0;
      Segment.Offset = DataSize;
      Segment.Section = &Section;
      addData(Segment.Data, Section);
      Segment.Alignment = Log2(Section.getAlign());
      Segment.LinkingFlags = Section.getSegmentFlags();
      DataSize += Segment.Data.size();
      Section.setSegmentIndex(SegmentIndex);

      if (const MCSymbolWasm *C = Section.getGroup()) {
        Comdats[C->getName()].emplace_back(
            WasmComdatEntry{wasm::WASM_COMDAT_DATA, SegmentIndex});
      }
    } else {
      // Create custom sections
      assert(Section.isMetadata());

      StringRef Name = SectionName;

      // For user-defined custom sections, strip the prefix
      Name.consume_front(".custom_section.");

      MCSymbol *Begin = Sec.getBeginSymbol();
      if (Begin) {
        assert(WasmIndices.count(cast<MCSymbolWasm>(Begin)) == 0);
        WasmIndices[cast<MCSymbolWasm>(Begin)] = CustomSections.size();
      }

      // Separate out the producers and target features sections
      if (Name == "producers") {
        ProducersSection = std::make_unique<WasmCustomSection>(Name, &Section);
        continue;
      }
      if (Name == "target_features") {
        TargetFeaturesSection =
            std::make_unique<WasmCustomSection>(Name, &Section);
        continue;
      }

      // Custom sections can also belong to COMDAT groups. In this case the
      // decriptor's "index" field is the section index (in the final object
      // file), but that is not known until after layout, so it must be fixed up
      // later
      if (const MCSymbolWasm *C = Section.getGroup()) {
        Comdats[C->getName()].emplace_back(
            WasmComdatEntry{wasm::WASM_COMDAT_SECTION,
                            static_cast<uint32_t>(CustomSections.size())});
      }

      CustomSections.emplace_back(Name, &Section);
    }
  }

  if (Mode != DwoMode::DwoOnly) {
    // Populate WasmIndices and DataLocations for defined symbols.
    for (const MCSymbol &S : Asm.symbols()) {
      // Ignore unnamed temporary symbols, which aren't ever exported, imported,
      // or used in relocations.
      if (S.isTemporary() && S.getName().empty())
        continue;

      const auto &WS = static_cast<const MCSymbolWasm &>(S);
      LLVM_DEBUG(
          dbgs() << "MCSymbol: "
                 << toString(WS.getType().value_or(wasm::WASM_SYMBOL_TYPE_DATA))
                 << " '" << S << "'"
                 << " isDefined=" << S.isDefined() << " isExternal="
                 << S.isExternal() << " isTemporary=" << S.isTemporary()
                 << " isWeak=" << WS.isWeak() << " isHidden=" << WS.isHidden()
                 << " isVariable=" << WS.isVariable() << "\n");

      if (WS.isVariable())
        continue;
      if (WS.isComdat() && !WS.isDefined())
        continue;

      if (WS.isFunction()) {
        unsigned Index;
        if (WS.isDefined()) {
          if (WS.getOffset() != 0)
            report_fatal_error(
                "function sections must contain one function each");

          // A definition. Write out the function body.
          Index = NumFunctionImports + Functions.size();
          WasmFunction Func;
          Func.SigIndex = getFunctionType(WS);
          Func.Section = &WS.getSection();
          assert(WasmIndices.count(&WS) == 0);
          WasmIndices[&WS] = Index;
          Functions.push_back(Func);

          auto &Section = static_cast<MCSectionWasm &>(WS.getSection());
          if (const MCSymbolWasm *C = Section.getGroup()) {
            Comdats[C->getName()].emplace_back(
                WasmComdatEntry{wasm::WASM_COMDAT_FUNCTION, Index});
          }

          if (WS.hasExportName()) {
            wasm::WasmExport Export;
            Export.Name = WS.getExportName();
            Export.Kind = wasm::WASM_EXTERNAL_FUNCTION;
            Export.Index = Index;
            Exports.push_back(Export);
          }
        } else {
          // An import; the index was assigned above.
          Index = WasmIndices.find(&WS)->second;
        }

        LLVM_DEBUG(dbgs() << "  -> function index: " << Index << "\n");

      } else if (WS.isData()) {
        if (!isInSymtab(WS))
          continue;

        if (!WS.isDefined()) {
          LLVM_DEBUG(dbgs() << "  -> segment index: -1"
                            << "\n");
          continue;
        }

        if (!WS.getSize())
          report_fatal_error("data symbols must have a size set with .size: " +
                             WS.getName());

        int64_t Size = 0;
        if (!WS.getSize()->evaluateAsAbsolute(Size, Asm))
          report_fatal_error(".size expression must be evaluatable");

        auto &DataSection = static_cast<MCSectionWasm &>(WS.getSection());
        if (!DataSection.isWasmData())
          report_fatal_error("data symbols must live in a data section: " +
                             WS.getName());

        // For each data symbol, export it in the symtab as a reference to the
        // corresponding Wasm data segment.
        wasm::WasmDataReference Ref = wasm::WasmDataReference{
            DataSection.getSegmentIndex(), Asm.getSymbolOffset(WS),
            static_cast<uint64_t>(Size)};
        assert(DataLocations.count(&WS) == 0);
        DataLocations[&WS] = Ref;
        LLVM_DEBUG(dbgs() << "  -> segment index: " << Ref.Segment << "\n");

      } else if (WS.isGlobal()) {
        // A "true" Wasm global (currently just __stack_pointer)
        if (WS.isDefined()) {
          wasm::WasmGlobal Global;
          Global.Type = WS.getGlobalType();
          Global.Index = NumGlobalImports + Globals.size();
          Global.InitExpr.Extended = false;
          switch (Global.Type.Type) {
          case wasm::WASM_TYPE_I32:
            Global.InitExpr.Inst.Opcode = wasm::WASM_OPCODE_I32_CONST;
            break;
          case wasm::WASM_TYPE_I64:
            Global.InitExpr.Inst.Opcode = wasm::WASM_OPCODE_I64_CONST;
            break;
          case wasm::WASM_TYPE_F32:
            Global.InitExpr.Inst.Opcode = wasm::WASM_OPCODE_F32_CONST;
            break;
          case wasm::WASM_TYPE_F64:
            Global.InitExpr.Inst.Opcode = wasm::WASM_OPCODE_F64_CONST;
            break;
          case wasm::WASM_TYPE_EXTERNREF:
            Global.InitExpr.Inst.Opcode = wasm::WASM_OPCODE_REF_NULL;
            break;
          default:
            llvm_unreachable("unexpected type");
          }
          assert(WasmIndices.count(&WS) == 0);
          WasmIndices[&WS] = Global.Index;
          Globals.push_back(Global);
        } else {
          // An import; the index was assigned above
          LLVM_DEBUG(dbgs() << "  -> global index: "
                            << WasmIndices.find(&WS)->second << "\n");
        }
      } else if (WS.isTable()) {
        if (WS.isDefined()) {
          wasm::WasmTable Table;
          Table.Index = NumTableImports + Tables.size();
          Table.Type = WS.getTableType();
          assert(WasmIndices.count(&WS) == 0);
          WasmIndices[&WS] = Table.Index;
          Tables.push_back(Table);
        }
        LLVM_DEBUG(dbgs() << " -> table index: "
                          << WasmIndices.find(&WS)->second << "\n");
      } else if (WS.isTag()) {
        // C++ exception symbol (__cpp_exception) or longjmp symbol
        // (__c_longjmp)
        unsigned Index;
        if (WS.isDefined()) {
          Index = NumTagImports + TagTypes.size();
          uint32_t SigIndex = getTagType(WS);
          assert(WasmIndices.count(&WS) == 0);
          WasmIndices[&WS] = Index;
          TagTypes.push_back(SigIndex);
        } else {
          // An import; the index was assigned above.
          assert(WasmIndices.count(&WS) > 0);
        }
        LLVM_DEBUG(dbgs() << "  -> tag index: " << WasmIndices.find(&WS)->second
                          << "\n");

      } else {
        assert(WS.isSection());
      }
    }

    // Populate WasmIndices and DataLocations for aliased symbols.  We need to
    // process these in a separate pass because we need to have processed the
    // target of the alias before the alias itself and the symbols are not
    // necessarily ordered in this way.
    for (const MCSymbol &S : Asm.symbols()) {
      if (!S.isVariable())
        continue;

      assert(S.isDefined());

      const auto *BS = Asm.getBaseSymbol(S);
      if (!BS)
        report_fatal_error(Twine(S.getName()) +
                           ": absolute addressing not supported!");
      const MCSymbolWasm *Base = cast<MCSymbolWasm>(BS);

      // Find the target symbol of this weak alias and export that index
      const auto &WS = static_cast<const MCSymbolWasm &>(S);
      LLVM_DEBUG(dbgs() << WS.getName() << ": weak alias of '" << *Base
                        << "'\n");

      if (Base->isFunction()) {
        assert(WasmIndices.count(Base) > 0);
        uint32_t WasmIndex = WasmIndices.find(Base)->second;
        assert(WasmIndices.count(&WS) == 0);
        WasmIndices[&WS] = WasmIndex;
        LLVM_DEBUG(dbgs() << "  -> index:" << WasmIndex << "\n");
      } else if (Base->isData()) {
        auto &DataSection = static_cast<MCSectionWasm &>(WS.getSection());
        uint64_t Offset = Asm.getSymbolOffset(S);
        int64_t Size = 0;
        // For data symbol alias we use the size of the base symbol as the
        // size of the alias.  When an offset from the base is involved this
        // can result in a offset + size goes past the end of the data section
        // which out object format doesn't support.  So we must clamp it.
        if (!Base->getSize()->evaluateAsAbsolute(Size, Asm))
          report_fatal_error(".size expression must be evaluatable");
        const WasmDataSegment &Segment =
            DataSegments[DataSection.getSegmentIndex()];
        Size =
            std::min(static_cast<uint64_t>(Size), Segment.Data.size() - Offset);
        wasm::WasmDataReference Ref = wasm::WasmDataReference{
            DataSection.getSegmentIndex(),
            static_cast<uint32_t>(Asm.getSymbolOffset(S)),
            static_cast<uint32_t>(Size)};
        DataLocations[&WS] = Ref;
        LLVM_DEBUG(dbgs() << "  -> index:" << Ref.Segment << "\n");
      } else {
        report_fatal_error("don't yet support global/tag aliases");
      }
    }
  }

  // Finally, populate the symbol table itself, in its "natural" order.
  for (const MCSymbol &S : Asm.symbols()) {
    const auto &WS = static_cast<const MCSymbolWasm &>(S);
    if (!isInSymtab(WS)) {
      WS.setIndex(InvalidIndex);
      continue;
    }
    LLVM_DEBUG(dbgs() << "adding to symtab: " << WS << "\n");

    uint32_t Flags = 0;
    if (WS.isWeak())
      Flags |= wasm::WASM_SYMBOL_BINDING_WEAK;
    if (WS.isHidden())
      Flags |= wasm::WASM_SYMBOL_VISIBILITY_HIDDEN;
    if (!WS.isExternal() && WS.isDefined())
      Flags |= wasm::WASM_SYMBOL_BINDING_LOCAL;
    if (WS.isUndefined())
      Flags |= wasm::WASM_SYMBOL_UNDEFINED;
    if (WS.isNoStrip()) {
      Flags |= wasm::WASM_SYMBOL_NO_STRIP;
      if (isEmscripten()) {
        Flags |= wasm::WASM_SYMBOL_EXPORTED;
      }
    }
    if (WS.hasImportName())
      Flags |= wasm::WASM_SYMBOL_EXPLICIT_NAME;
    if (WS.hasExportName())
      Flags |= wasm::WASM_SYMBOL_EXPORTED;
    if (WS.isTLS())
      Flags |= wasm::WASM_SYMBOL_TLS;

    wasm::WasmSymbolInfo Info;
    Info.Name = WS.getName();
    Info.Kind = WS.getType().value_or(wasm::WASM_SYMBOL_TYPE_DATA);
    Info.Flags = Flags;
    if (!WS.isData()) {
      assert(WasmIndices.count(&WS) > 0);
      Info.ElementIndex = WasmIndices.find(&WS)->second;
    } else if (WS.isDefined()) {
      assert(DataLocations.count(&WS) > 0);
      Info.DataRef = DataLocations.find(&WS)->second;
    }
    WS.setIndex(SymbolInfos.size());
    SymbolInfos.emplace_back(Info);
  }

  {
    auto HandleReloc = [&](const WasmRelocationEntry &Rel) {
      // Functions referenced by a relocation need to put in the table.  This is
      // purely to make the object file's provisional values readable, and is
      // ignored by the linker, which re-calculates the relocations itself.
      if (Rel.Type != wasm::R_WASM_TABLE_INDEX_I32 &&
          Rel.Type != wasm::R_WASM_TABLE_INDEX_I64 &&
          Rel.Type != wasm::R_WASM_TABLE_INDEX_SLEB &&
          Rel.Type != wasm::R_WASM_TABLE_INDEX_SLEB64 &&
          Rel.Type != wasm::R_WASM_TABLE_INDEX_REL_SLEB &&
          Rel.Type != wasm::R_WASM_TABLE_INDEX_REL_SLEB64)
        return;
      assert(Rel.Symbol->isFunction());
      const MCSymbolWasm *Base =
          cast<MCSymbolWasm>(Asm.getBaseSymbol(*Rel.Symbol));
      uint32_t FunctionIndex = WasmIndices.find(Base)->second;
      uint32_t TableIndex = TableElems.size() + InitialTableOffset;
      if (TableIndices.try_emplace(Base, TableIndex).second) {
        LLVM_DEBUG(dbgs() << "  -> adding " << Base->getName()
                          << " to table: " << TableIndex << "\n");
        TableElems.push_back(FunctionIndex);
        registerFunctionType(*Base);
      }
    };

    for (const WasmRelocationEntry &RelEntry : CodeRelocations)
      HandleReloc(RelEntry);
    for (const WasmRelocationEntry &RelEntry : DataRelocations)
      HandleReloc(RelEntry);
  }

  // Translate .init_array section contents into start functions.
  for (const MCSection &S : Asm) {
    const auto &WS = static_cast<const MCSectionWasm &>(S);
    if (WS.getName().starts_with(".fini_array"))
      report_fatal_error(".fini_array sections are unsupported");
    if (!WS.getName().starts_with(".init_array"))
      continue;
    auto IT = WS.begin();
    if (IT == WS.end())
      continue;
    const MCFragment &EmptyFrag = *IT;
    if (EmptyFrag.getKind() != MCFragment::FT_Data)
      report_fatal_error(".init_array section should be aligned");

    const MCFragment *nextFrag = EmptyFrag.getNext();
    while (nextFrag != nullptr) {
      const MCFragment &AlignFrag = *nextFrag;
      if (AlignFrag.getKind() != MCFragment::FT_Align)
        report_fatal_error(".init_array section should be aligned");
      if (cast<MCAlignFragment>(AlignFrag).getAlignment() !=
          Align(is64Bit() ? 8 : 4))
        report_fatal_error(
            ".init_array section should be aligned for pointers");

      const MCFragment &Frag = *AlignFrag.getNext();
      nextFrag = Frag.getNext();
      if (Frag.hasInstructions() || Frag.getKind() != MCFragment::FT_Data)
        report_fatal_error("only data supported in .init_array section");

      uint16_t Priority = UINT16_MAX;
      unsigned PrefixLength = strlen(".init_array");
      if (WS.getName().size() > PrefixLength) {
        if (WS.getName()[PrefixLength] != '.')
          report_fatal_error(
              ".init_array section priority should start with '.'");
        if (WS.getName().substr(PrefixLength + 1).getAsInteger(10, Priority))
          report_fatal_error("invalid .init_array section priority");
      }
      const auto &DataFrag = cast<MCDataFragment>(Frag);
      const SmallVectorImpl<char> &Contents = DataFrag.getContents();
      for (const uint8_t *
               P = (const uint8_t *)Contents.data(),
              *End = (const uint8_t *)Contents.data() + Contents.size();
           P != End; ++P) {
        if (*P != 0)
          report_fatal_error("non-symbolic data in .init_array section");
      }
      for (const MCFixup &Fixup : DataFrag.getFixups()) {
        assert(Fixup.getKind() ==
               MCFixup::getKindForSize(is64Bit() ? 8 : 4, false));
        const MCExpr *Expr = Fixup.getValue();
        auto *SymRef = dyn_cast<MCSymbolRefExpr>(Expr);
        if (!SymRef)
          report_fatal_error(
              "fixups in .init_array should be symbol references");
        const auto &TargetSym = cast<const MCSymbolWasm>(SymRef->getSymbol());
        if (TargetSym.getIndex() == InvalidIndex)
          report_fatal_error("symbols in .init_array should exist in symtab");
        if (!TargetSym.isFunction())
          report_fatal_error("symbols in .init_array should be for functions");
        InitFuncs.push_back(std::make_pair(Priority, TargetSym.getIndex()));
      }
    }
  }

  // Write out the Wasm header.
  writeHeader(Asm);

  uint32_t CodeSectionIndex, DataSectionIndex;
  if (Mode != DwoMode::DwoOnly) {
    writeTypeSection(Signatures);
    writeImportSection(Imports, DataSize, TableElems.size());
    writeFunctionSection(Functions);
    writeTableSection(Tables);
    // Skip the "memory" section; we import the memory instead.
    writeTagSection(TagTypes);
    writeGlobalSection(Globals);
    writeExportSection(Exports);
    const MCSymbol *IndirectFunctionTable =
        Asm.getContext().lookupSymbol("__indirect_function_table");
    writeElemSection(cast_or_null<const MCSymbolWasm>(IndirectFunctionTable),
                     TableElems);
    writeDataCountSection();

    CodeSectionIndex = writeCodeSection(Asm, Functions);
    DataSectionIndex = writeDataSection(Asm);
  }

  // The Sections in the COMDAT list have placeholder indices (their index among
  // custom sections, rather than among all sections). Fix them up here.
  for (auto &Group : Comdats) {
    for (auto &Entry : Group.second) {
      if (Entry.Kind == wasm::WASM_COMDAT_SECTION) {
        Entry.Index += SectionCount;
      }
    }
  }
  for (auto &CustomSection : CustomSections)
    writeCustomSection(CustomSection, Asm);

  if (Mode != DwoMode::DwoOnly) {
    writeLinkingMetaDataSection(SymbolInfos, InitFuncs, Comdats);

    writeRelocSection(CodeSectionIndex, "CODE", CodeRelocations);
    writeRelocSection(DataSectionIndex, "DATA", DataRelocations);
  }
  writeCustomRelocSections();
  if (ProducersSection)
    writeCustomSection(*ProducersSection, Asm);
  if (TargetFeaturesSection)
    writeCustomSection(*TargetFeaturesSection, Asm);

  // TODO: Translate the .comment section to the output.
  return W->OS.tell() - StartOffset;
}

std::unique_ptr<MCObjectWriter>
llvm::createWasmObjectWriter(std::unique_ptr<MCWasmObjectTargetWriter> MOTW,
                             raw_pwrite_stream &OS) {
  return std::make_unique<WasmObjectWriter>(std::move(MOTW), OS);
}

std::unique_ptr<MCObjectWriter>
llvm::createWasmDwoObjectWriter(std::unique_ptr<MCWasmObjectTargetWriter> MOTW,
                                raw_pwrite_stream &OS,
                                raw_pwrite_stream &DwoOS) {
  return std::make_unique<WasmObjectWriter>(std::move(MOTW), OS, DwoOS);
}
