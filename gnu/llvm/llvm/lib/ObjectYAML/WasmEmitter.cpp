//===- yaml2wasm - Convert YAML to a Wasm object file --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The Wasm component of yaml2obj.
///
//===----------------------------------------------------------------------===//
//

#include "llvm/Object/Wasm.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"

using namespace llvm;

namespace {
/// This parses a yaml stream that represents a Wasm object file.
/// See docs/yaml2obj for the yaml scheema.
class WasmWriter {
public:
  WasmWriter(WasmYAML::Object &Obj, yaml::ErrorHandler EH)
      : Obj(Obj), ErrHandler(EH) {}
  bool writeWasm(raw_ostream &OS);

private:
  void writeRelocSection(raw_ostream &OS, WasmYAML::Section &Sec,
                         uint32_t SectionIndex);

  void writeInitExpr(raw_ostream &OS, const WasmYAML::InitExpr &InitExpr);

  void writeSectionContent(raw_ostream &OS, WasmYAML::CustomSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::TypeSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::ImportSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::FunctionSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::TableSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::MemorySection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::TagSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::GlobalSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::ExportSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::StartSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::ElemSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::CodeSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::DataSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::DataCountSection &Section);

  // Custom section types
  void writeSectionContent(raw_ostream &OS, WasmYAML::DylinkSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::NameSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::LinkingSection &Section);
  void writeSectionContent(raw_ostream &OS, WasmYAML::ProducersSection &Section);
  void writeSectionContent(raw_ostream &OS,
                          WasmYAML::TargetFeaturesSection &Section);
  WasmYAML::Object &Obj;
  uint32_t NumImportedFunctions = 0;
  uint32_t NumImportedGlobals = 0;
  uint32_t NumImportedTables = 0;
  uint32_t NumImportedTags = 0;

  bool HasError = false;
  yaml::ErrorHandler ErrHandler;
  void reportError(const Twine &Msg);
};

class SubSectionWriter {
  raw_ostream &OS;
  std::string OutString;
  raw_string_ostream StringStream;

public:
  SubSectionWriter(raw_ostream &OS) : OS(OS), StringStream(OutString) {}

  void done() {
    StringStream.flush();
    encodeULEB128(OutString.size(), OS);
    OS << OutString;
    OutString.clear();
  }

  raw_ostream &getStream() { return StringStream; }
};

} // end anonymous namespace

static int writeUint64(raw_ostream &OS, uint64_t Value) {
  char Data[sizeof(Value)];
  support::endian::write64le(Data, Value);
  OS.write(Data, sizeof(Data));
  return 0;
}

static int writeUint32(raw_ostream &OS, uint32_t Value) {
  char Data[sizeof(Value)];
  support::endian::write32le(Data, Value);
  OS.write(Data, sizeof(Data));
  return 0;
}

static int writeUint8(raw_ostream &OS, uint8_t Value) {
  char Data[sizeof(Value)];
  memcpy(Data, &Value, sizeof(Data));
  OS.write(Data, sizeof(Data));
  return 0;
}

static int writeStringRef(const StringRef &Str, raw_ostream &OS) {
  encodeULEB128(Str.size(), OS);
  OS << Str;
  return 0;
}

static int writeLimits(const WasmYAML::Limits &Lim, raw_ostream &OS) {
  writeUint8(OS, Lim.Flags);
  encodeULEB128(Lim.Minimum, OS);
  if (Lim.Flags & wasm::WASM_LIMITS_FLAG_HAS_MAX)
    encodeULEB128(Lim.Maximum, OS);
  return 0;
}

void WasmWriter::reportError(const Twine &Msg) {
  ErrHandler(Msg);
  HasError = true;
}

void WasmWriter::writeInitExpr(raw_ostream &OS,
                               const WasmYAML::InitExpr &InitExpr) {
  if (InitExpr.Extended) {
    InitExpr.Body.writeAsBinary(OS);
  } else {
    writeUint8(OS, InitExpr.Inst.Opcode);
    switch (InitExpr.Inst.Opcode) {
    case wasm::WASM_OPCODE_I32_CONST:
      encodeSLEB128(InitExpr.Inst.Value.Int32, OS);
      break;
    case wasm::WASM_OPCODE_I64_CONST:
      encodeSLEB128(InitExpr.Inst.Value.Int64, OS);
      break;
    case wasm::WASM_OPCODE_F32_CONST:
      writeUint32(OS, InitExpr.Inst.Value.Float32);
      break;
    case wasm::WASM_OPCODE_F64_CONST:
      writeUint64(OS, InitExpr.Inst.Value.Float64);
      break;
    case wasm::WASM_OPCODE_GLOBAL_GET:
      encodeULEB128(InitExpr.Inst.Value.Global, OS);
      break;
    default:
      reportError("unknown opcode in init_expr: " +
                  Twine(InitExpr.Inst.Opcode));
      return;
    }
    writeUint8(OS, wasm::WASM_OPCODE_END);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::DylinkSection &Section) {
  writeStringRef(Section.Name, OS);

  writeUint8(OS, wasm::WASM_DYLINK_MEM_INFO);
  SubSectionWriter SubSection(OS);
  raw_ostream &SubOS = SubSection.getStream();
  encodeULEB128(Section.MemorySize, SubOS);
  encodeULEB128(Section.MemoryAlignment, SubOS);
  encodeULEB128(Section.TableSize, SubOS);
  encodeULEB128(Section.TableAlignment, SubOS);
  SubSection.done();

  if (Section.Needed.size()) {
    writeUint8(OS, wasm::WASM_DYLINK_NEEDED);
    raw_ostream &SubOS = SubSection.getStream();
    encodeULEB128(Section.Needed.size(), SubOS);
    for (StringRef Needed : Section.Needed)
      writeStringRef(Needed, SubOS);
    SubSection.done();
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::LinkingSection &Section) {
  writeStringRef(Section.Name, OS);
  encodeULEB128(Section.Version, OS);

  SubSectionWriter SubSection(OS);

  // SYMBOL_TABLE subsection
  if (Section.SymbolTable.size()) {
    writeUint8(OS, wasm::WASM_SYMBOL_TABLE);
    encodeULEB128(Section.SymbolTable.size(), SubSection.getStream());
    for (auto Sym : llvm::enumerate(Section.SymbolTable)) {
      const WasmYAML::SymbolInfo &Info = Sym.value();
      assert(Info.Index == Sym.index());
      writeUint8(SubSection.getStream(), Info.Kind);
      encodeULEB128(Info.Flags, SubSection.getStream());
      switch (Info.Kind) {
      case wasm::WASM_SYMBOL_TYPE_FUNCTION:
      case wasm::WASM_SYMBOL_TYPE_GLOBAL:
      case wasm::WASM_SYMBOL_TYPE_TABLE:
      case wasm::WASM_SYMBOL_TYPE_TAG:
        encodeULEB128(Info.ElementIndex, SubSection.getStream());
        if ((Info.Flags & wasm::WASM_SYMBOL_UNDEFINED) == 0 ||
            (Info.Flags & wasm::WASM_SYMBOL_EXPLICIT_NAME) != 0)
          writeStringRef(Info.Name, SubSection.getStream());
        break;
      case wasm::WASM_SYMBOL_TYPE_DATA:
        writeStringRef(Info.Name, SubSection.getStream());
        if ((Info.Flags & wasm::WASM_SYMBOL_UNDEFINED) == 0) {
          encodeULEB128(Info.DataRef.Segment, SubSection.getStream());
          encodeULEB128(Info.DataRef.Offset, SubSection.getStream());
          encodeULEB128(Info.DataRef.Size, SubSection.getStream());
        }
        break;
      case wasm::WASM_SYMBOL_TYPE_SECTION:
        encodeULEB128(Info.ElementIndex, SubSection.getStream());
        break;
      default:
        llvm_unreachable("unexpected kind");
      }
    }

    SubSection.done();
  }

  // SEGMENT_NAMES subsection
  if (Section.SegmentInfos.size()) {
    writeUint8(OS, wasm::WASM_SEGMENT_INFO);
    encodeULEB128(Section.SegmentInfos.size(), SubSection.getStream());
    for (const WasmYAML::SegmentInfo &SegmentInfo : Section.SegmentInfos) {
      writeStringRef(SegmentInfo.Name, SubSection.getStream());
      encodeULEB128(SegmentInfo.Alignment, SubSection.getStream());
      encodeULEB128(SegmentInfo.Flags, SubSection.getStream());
    }
    SubSection.done();
  }

  // INIT_FUNCS subsection
  if (Section.InitFunctions.size()) {
    writeUint8(OS, wasm::WASM_INIT_FUNCS);
    encodeULEB128(Section.InitFunctions.size(), SubSection.getStream());
    for (const WasmYAML::InitFunction &Func : Section.InitFunctions) {
      encodeULEB128(Func.Priority, SubSection.getStream());
      encodeULEB128(Func.Symbol, SubSection.getStream());
    }
    SubSection.done();
  }

  // COMDAT_INFO subsection
  if (Section.Comdats.size()) {
    writeUint8(OS, wasm::WASM_COMDAT_INFO);
    encodeULEB128(Section.Comdats.size(), SubSection.getStream());
    for (const auto &C : Section.Comdats) {
      writeStringRef(C.Name, SubSection.getStream());
      encodeULEB128(0, SubSection.getStream()); // flags for future use
      encodeULEB128(C.Entries.size(), SubSection.getStream());
      for (const WasmYAML::ComdatEntry &Entry : C.Entries) {
        writeUint8(SubSection.getStream(), Entry.Kind);
        encodeULEB128(Entry.Index, SubSection.getStream());
      }
    }
    SubSection.done();
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::NameSection &Section) {
  writeStringRef(Section.Name, OS);
  if (Section.FunctionNames.size()) {
    writeUint8(OS, wasm::WASM_NAMES_FUNCTION);

    SubSectionWriter SubSection(OS);

    encodeULEB128(Section.FunctionNames.size(), SubSection.getStream());
    for (const WasmYAML::NameEntry &NameEntry : Section.FunctionNames) {
      encodeULEB128(NameEntry.Index, SubSection.getStream());
      writeStringRef(NameEntry.Name, SubSection.getStream());
    }

    SubSection.done();
  }
  if (Section.GlobalNames.size()) {
    writeUint8(OS, wasm::WASM_NAMES_GLOBAL);

    SubSectionWriter SubSection(OS);

    encodeULEB128(Section.GlobalNames.size(), SubSection.getStream());
    for (const WasmYAML::NameEntry &NameEntry : Section.GlobalNames) {
      encodeULEB128(NameEntry.Index, SubSection.getStream());
      writeStringRef(NameEntry.Name, SubSection.getStream());
    }

    SubSection.done();
  }
  if (Section.DataSegmentNames.size()) {
    writeUint8(OS, wasm::WASM_NAMES_DATA_SEGMENT);

    SubSectionWriter SubSection(OS);

    encodeULEB128(Section.DataSegmentNames.size(), SubSection.getStream());
    for (const WasmYAML::NameEntry &NameEntry : Section.DataSegmentNames) {
      encodeULEB128(NameEntry.Index, SubSection.getStream());
      writeStringRef(NameEntry.Name, SubSection.getStream());
    }

    SubSection.done();
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::ProducersSection &Section) {
  writeStringRef(Section.Name, OS);
  int Fields = int(!Section.Languages.empty()) + int(!Section.Tools.empty()) +
               int(!Section.SDKs.empty());
  if (Fields == 0)
    return;
  encodeULEB128(Fields, OS);
  for (auto &Field : {std::make_pair(StringRef("language"), &Section.Languages),
                      std::make_pair(StringRef("processed-by"), &Section.Tools),
                      std::make_pair(StringRef("sdk"), &Section.SDKs)}) {
    if (Field.second->empty())
      continue;
    writeStringRef(Field.first, OS);
    encodeULEB128(Field.second->size(), OS);
    for (auto &Entry : *Field.second) {
      writeStringRef(Entry.Name, OS);
      writeStringRef(Entry.Version, OS);
    }
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::TargetFeaturesSection &Section) {
  writeStringRef(Section.Name, OS);
  encodeULEB128(Section.Features.size(), OS);
  for (auto &E : Section.Features) {
    writeUint8(OS, E.Prefix);
    writeStringRef(E.Name, OS);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::CustomSection &Section) {
  if (auto S = dyn_cast<WasmYAML::DylinkSection>(&Section)) {
    writeSectionContent(OS, *S);
  } else if (auto S = dyn_cast<WasmYAML::NameSection>(&Section)) {
    writeSectionContent(OS, *S);
  } else if (auto S = dyn_cast<WasmYAML::LinkingSection>(&Section)) {
    writeSectionContent(OS, *S);
  } else if (auto S = dyn_cast<WasmYAML::ProducersSection>(&Section)) {
    writeSectionContent(OS, *S);
  } else if (auto S = dyn_cast<WasmYAML::TargetFeaturesSection>(&Section)) {
    writeSectionContent(OS, *S);
  } else {
    writeStringRef(Section.Name, OS);
    Section.Payload.writeAsBinary(OS);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                    WasmYAML::TypeSection &Section) {
  encodeULEB128(Section.Signatures.size(), OS);
  uint32_t ExpectedIndex = 0;
  for (const WasmYAML::Signature &Sig : Section.Signatures) {
    if (Sig.Index != ExpectedIndex) {
      reportError("unexpected type index: " + Twine(Sig.Index));
      return;
    }
    ++ExpectedIndex;
    writeUint8(OS, Sig.Form);
    encodeULEB128(Sig.ParamTypes.size(), OS);
    for (auto ParamType : Sig.ParamTypes)
      writeUint8(OS, ParamType);
    encodeULEB128(Sig.ReturnTypes.size(), OS);
    for (auto ReturnType : Sig.ReturnTypes)
      writeUint8(OS, ReturnType);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                    WasmYAML::ImportSection &Section) {
  encodeULEB128(Section.Imports.size(), OS);
  for (const WasmYAML::Import &Import : Section.Imports) {
    writeStringRef(Import.Module, OS);
    writeStringRef(Import.Field, OS);
    writeUint8(OS, Import.Kind);
    switch (Import.Kind) {
    case wasm::WASM_EXTERNAL_FUNCTION:
      encodeULEB128(Import.SigIndex, OS);
      NumImportedFunctions++;
      break;
    case wasm::WASM_EXTERNAL_GLOBAL:
      writeUint8(OS, Import.GlobalImport.Type);
      writeUint8(OS, Import.GlobalImport.Mutable);
      NumImportedGlobals++;
      break;
    case wasm::WASM_EXTERNAL_TAG:
      writeUint8(OS, 0); // Reserved 'attribute' field
      encodeULEB128(Import.SigIndex, OS);
      NumImportedTags++;
      break;
    case wasm::WASM_EXTERNAL_MEMORY:
      writeLimits(Import.Memory, OS);
      break;
    case wasm::WASM_EXTERNAL_TABLE:
      writeUint8(OS, Import.TableImport.ElemType);
      writeLimits(Import.TableImport.TableLimits, OS);
      NumImportedTables++;
      break;
    default:
      reportError("unknown import type: " +Twine(Import.Kind));
      return;
    }
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::FunctionSection &Section) {
  encodeULEB128(Section.FunctionTypes.size(), OS);
  for (uint32_t FuncType : Section.FunctionTypes)
    encodeULEB128(FuncType, OS);
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                    WasmYAML::ExportSection &Section) {
  encodeULEB128(Section.Exports.size(), OS);
  for (const WasmYAML::Export &Export : Section.Exports) {
    writeStringRef(Export.Name, OS);
    writeUint8(OS, Export.Kind);
    encodeULEB128(Export.Index, OS);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::StartSection &Section) {
  encodeULEB128(Section.StartFunction, OS);
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::TableSection &Section) {
  encodeULEB128(Section.Tables.size(), OS);
  uint32_t ExpectedIndex = NumImportedTables;
  for (auto &Table : Section.Tables) {
    if (Table.Index != ExpectedIndex) {
      reportError("unexpected table index: " + Twine(Table.Index));
      return;
    }
    ++ExpectedIndex;
    writeUint8(OS, Table.ElemType);
    writeLimits(Table.TableLimits, OS);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::MemorySection &Section) {
  encodeULEB128(Section.Memories.size(), OS);
  for (const WasmYAML::Limits &Mem : Section.Memories)
    writeLimits(Mem, OS);
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::TagSection &Section) {
  encodeULEB128(Section.TagTypes.size(), OS);
  for (uint32_t TagType : Section.TagTypes) {
    writeUint8(OS, 0); // Reserved 'attribute' field
    encodeULEB128(TagType, OS);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::GlobalSection &Section) {
  encodeULEB128(Section.Globals.size(), OS);
  uint32_t ExpectedIndex = NumImportedGlobals;
  for (auto &Global : Section.Globals) {
    if (Global.Index != ExpectedIndex) {
      reportError("unexpected global index: " + Twine(Global.Index));
      return;
    }
    ++ExpectedIndex;
    writeUint8(OS, Global.Type);
    writeUint8(OS, Global.Mutable);
    writeInitExpr(OS, Global.Init);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::ElemSection &Section) {
  encodeULEB128(Section.Segments.size(), OS);
  for (auto &Segment : Section.Segments) {
    encodeULEB128(Segment.Flags, OS);
    if (Segment.Flags & wasm::WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER)
      encodeULEB128(Segment.TableNumber, OS);

    writeInitExpr(OS, Segment.Offset);

    if (Segment.Flags & wasm::WASM_ELEM_SEGMENT_MASK_HAS_ELEM_KIND) {
      // We only support active function table initializers, for which the elem
      // kind is specified to be written as 0x00 and interpreted to mean
      // "funcref".
      if (Segment.ElemKind != uint32_t(wasm::ValType::FUNCREF)) {
        reportError("unexpected elemkind: " + Twine(Segment.ElemKind));
        return;
      }
      const uint8_t ElemKind = 0;
      writeUint8(OS, ElemKind);
    }

    encodeULEB128(Segment.Functions.size(), OS);
    for (auto &Function : Segment.Functions)
      encodeULEB128(Function, OS);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                    WasmYAML::CodeSection &Section) {
  encodeULEB128(Section.Functions.size(), OS);
  uint32_t ExpectedIndex = NumImportedFunctions;
  for (auto &Func : Section.Functions) {
    std::string OutString;
    raw_string_ostream StringStream(OutString);
    if (Func.Index != ExpectedIndex) {
      reportError("unexpected function index: " + Twine(Func.Index));
      return;
    }
    ++ExpectedIndex;

    encodeULEB128(Func.Locals.size(), StringStream);
    for (auto &LocalDecl : Func.Locals) {
      encodeULEB128(LocalDecl.Count, StringStream);
      writeUint8(StringStream, LocalDecl.Type);
    }

    Func.Body.writeAsBinary(StringStream);

    // Write the section size followed by the content
    StringStream.flush();
    encodeULEB128(OutString.size(), OS);
    OS << OutString;
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::DataSection &Section) {
  encodeULEB128(Section.Segments.size(), OS);
  for (auto &Segment : Section.Segments) {
    encodeULEB128(Segment.InitFlags, OS);
    if (Segment.InitFlags & wasm::WASM_DATA_SEGMENT_HAS_MEMINDEX)
      encodeULEB128(Segment.MemoryIndex, OS);
    if ((Segment.InitFlags & wasm::WASM_DATA_SEGMENT_IS_PASSIVE) == 0)
      writeInitExpr(OS, Segment.Offset);
    encodeULEB128(Segment.Content.binary_size(), OS);
    Segment.Content.writeAsBinary(OS);
  }
}

void WasmWriter::writeSectionContent(raw_ostream &OS,
                                     WasmYAML::DataCountSection &Section) {
  encodeULEB128(Section.Count, OS);
}

void WasmWriter::writeRelocSection(raw_ostream &OS, WasmYAML::Section &Sec,
                                  uint32_t SectionIndex) {
  switch (Sec.Type) {
  case wasm::WASM_SEC_CODE:
    writeStringRef("reloc.CODE", OS);
    break;
  case wasm::WASM_SEC_DATA:
    writeStringRef("reloc.DATA", OS);
    break;
  case wasm::WASM_SEC_CUSTOM: {
    auto *CustomSection = cast<WasmYAML::CustomSection>(&Sec);
    writeStringRef(("reloc." + CustomSection->Name).str(), OS);
    break;
  }
  default:
    llvm_unreachable("not yet implemented");
  }

  encodeULEB128(SectionIndex, OS);
  encodeULEB128(Sec.Relocations.size(), OS);

  for (auto Reloc : Sec.Relocations) {
    writeUint8(OS, Reloc.Type);
    encodeULEB128(Reloc.Offset, OS);
    encodeULEB128(Reloc.Index, OS);
    if (wasm::relocTypeHasAddend(Reloc.Type))
      encodeSLEB128(Reloc.Addend, OS);
  }
}

bool WasmWriter::writeWasm(raw_ostream &OS) {
  // Write headers
  OS.write(wasm::WasmMagic, sizeof(wasm::WasmMagic));
  writeUint32(OS, Obj.Header.Version);

  // Write each section
  llvm::object::WasmSectionOrderChecker Checker;
  for (const std::unique_ptr<WasmYAML::Section> &Sec : Obj.Sections) {
    StringRef SecName = "";
    if (auto S = dyn_cast<WasmYAML::CustomSection>(Sec.get()))
      SecName = S->Name;
    if (!Checker.isValidSectionOrder(Sec->Type, SecName)) {
      reportError("out of order section type: " + Twine(Sec->Type));
      return false;
    }
    encodeULEB128(Sec->Type, OS);
    std::string OutString;
    raw_string_ostream StringStream(OutString);
    if (auto S = dyn_cast<WasmYAML::CustomSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::TypeSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::ImportSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::FunctionSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::TableSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::MemorySection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::TagSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::GlobalSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::ExportSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::StartSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::ElemSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::CodeSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::DataSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else if (auto S = dyn_cast<WasmYAML::DataCountSection>(Sec.get()))
      writeSectionContent(StringStream, *S);
    else
      reportError("unknown section type: " + Twine(Sec->Type));

    if (HasError)
      return false;

    StringStream.flush();

    unsigned HeaderSecSizeEncodingLen =
        Sec->HeaderSecSizeEncodingLen ? *Sec->HeaderSecSizeEncodingLen : 5;
    unsigned RequiredLen = getULEB128Size(OutString.size());
    // Wasm spec does not allow LEBs larger than 5 bytes
    assert(RequiredLen <= 5);
    if (HeaderSecSizeEncodingLen < RequiredLen) {
      reportError("section header length can't be encoded in a LEB of size " +
                  Twine(HeaderSecSizeEncodingLen));
      return false;
    }
    // Write the section size followed by the content
    encodeULEB128(OutString.size(), OS, HeaderSecSizeEncodingLen);
    OS << OutString;
  }

  // write reloc sections for any section that have relocations
  uint32_t SectionIndex = 0;
  for (const std::unique_ptr<WasmYAML::Section> &Sec : Obj.Sections) {
    if (Sec->Relocations.empty()) {
      SectionIndex++;
      continue;
    }

    writeUint8(OS, wasm::WASM_SEC_CUSTOM);
    std::string OutString;
    raw_string_ostream StringStream(OutString);
    writeRelocSection(StringStream, *Sec, SectionIndex++);
    StringStream.flush();

    encodeULEB128(OutString.size(), OS);
    OS << OutString;
  }

  return true;
}

namespace llvm {
namespace yaml {

bool yaml2wasm(WasmYAML::Object &Doc, raw_ostream &Out, ErrorHandler EH) {
  WasmWriter Writer(Doc, EH);
  return Writer.writeWasm(Out);
}

} // namespace yaml
} // namespace llvm
