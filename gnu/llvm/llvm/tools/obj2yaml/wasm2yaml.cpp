//===------ utils/wasm2yaml.cpp - obj2yaml conversion tool ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/Object/COFF.h"
#include "llvm/ObjectYAML/WasmYAML.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;
using object::WasmSection;

namespace {

class WasmDumper {
  const object::WasmObjectFile &Obj;

public:
  WasmDumper(const object::WasmObjectFile &O) : Obj(O) {}

  ErrorOr<WasmYAML::Object *> dump();

  std::unique_ptr<WasmYAML::CustomSection>
  dumpCustomSection(const WasmSection &WasmSec);
};

} // namespace

static WasmYAML::Limits makeLimits(const wasm::WasmLimits &Limits) {
  WasmYAML::Limits L;
  L.Flags = Limits.Flags;
  L.Minimum = Limits.Minimum;
  L.Maximum = Limits.Maximum;
  return L;
}

static WasmYAML::Table makeTable(uint32_t Index,
                                 const wasm::WasmTableType &Type) {
  WasmYAML::Table T;
  T.Index = Index;
  T.ElemType = (uint32_t)Type.ElemType;
  T.TableLimits = makeLimits(Type.Limits);
  return T;
}

std::unique_ptr<WasmYAML::CustomSection>
WasmDumper::dumpCustomSection(const WasmSection &WasmSec) {
  std::unique_ptr<WasmYAML::CustomSection> CustomSec;
  if (WasmSec.Name == "dylink" || WasmSec.Name == "dylink.0") {
    std::unique_ptr<WasmYAML::DylinkSection> DylinkSec =
        std::make_unique<WasmYAML::DylinkSection>();
    const wasm::WasmDylinkInfo& Info = Obj.dylinkInfo();
    DylinkSec->MemorySize = Info.MemorySize;
    DylinkSec->MemoryAlignment = Info.MemoryAlignment;
    DylinkSec->TableSize = Info.TableSize;
    DylinkSec->TableAlignment = Info.TableAlignment;
    DylinkSec->Needed = Info.Needed;
    for (const auto &Imp : Info.ImportInfo)
      DylinkSec->ImportInfo.push_back({Imp.Module, Imp.Field, Imp.Flags});
    for (const auto &Exp : Info.ExportInfo)
      DylinkSec->ExportInfo.push_back({Exp.Name, Exp.Flags});
    CustomSec = std::move(DylinkSec);
  } else if (WasmSec.Name == "name") {
    std::unique_ptr<WasmYAML::NameSection> NameSec =
        std::make_unique<WasmYAML::NameSection>();
    for (const llvm::wasm::WasmDebugName &Name : Obj.debugNames()) {
      WasmYAML::NameEntry NameEntry;
      NameEntry.Name = Name.Name;
      NameEntry.Index = Name.Index;
      if (Name.Type == llvm::wasm::NameType::FUNCTION) {
        NameSec->FunctionNames.push_back(NameEntry);
      } else if (Name.Type == llvm::wasm::NameType::GLOBAL) {
        NameSec->GlobalNames.push_back(NameEntry);
      } else {
        assert(Name.Type == llvm::wasm::NameType::DATA_SEGMENT);
        NameSec->DataSegmentNames.push_back(NameEntry);
      }
    }
    CustomSec = std::move(NameSec);
  } else if (WasmSec.Name == "linking") {
    std::unique_ptr<WasmYAML::LinkingSection> LinkingSec =
        std::make_unique<WasmYAML::LinkingSection>();
    LinkingSec->Version = Obj.linkingData().Version;

    ArrayRef<StringRef> Comdats = Obj.linkingData().Comdats;
    for (StringRef ComdatName : Comdats)
      LinkingSec->Comdats.emplace_back(WasmYAML::Comdat{ComdatName, {}});
    for (auto &Func : Obj.functions()) {
      if (Func.Comdat != UINT32_MAX) {
        LinkingSec->Comdats[Func.Comdat].Entries.emplace_back(
            WasmYAML::ComdatEntry{wasm::WASM_COMDAT_FUNCTION, Func.Index});
      }
    }

    uint32_t SegmentIndex = 0;
    for (const object::WasmSegment &Segment : Obj.dataSegments()) {
      if (!Segment.Data.Name.empty()) {
        WasmYAML::SegmentInfo SegmentInfo;
        SegmentInfo.Name = Segment.Data.Name;
        SegmentInfo.Index = SegmentIndex;
        SegmentInfo.Alignment = Segment.Data.Alignment;
        SegmentInfo.Flags = Segment.Data.LinkingFlags;
        LinkingSec->SegmentInfos.push_back(SegmentInfo);
      }
      if (Segment.Data.Comdat != UINT32_MAX) {
        LinkingSec->Comdats[Segment.Data.Comdat].Entries.emplace_back(
            WasmYAML::ComdatEntry{wasm::WASM_COMDAT_DATA, SegmentIndex});
      }
      SegmentIndex++;
    }
    uint32_t SectionIndex = 0;
    for (const auto &Sec : Obj.sections()) {
      const WasmSection &WasmSec = Obj.getWasmSection(Sec);
      if (WasmSec.Comdat != UINT32_MAX)
        LinkingSec->Comdats[WasmSec.Comdat].Entries.emplace_back(
            WasmYAML::ComdatEntry{wasm::WASM_COMDAT_SECTION, SectionIndex});
      SectionIndex++;
    }

    uint32_t SymbolIndex = 0;
    for (const object::SymbolRef &Sym : Obj.symbols()) {
      const wasm::WasmSymbolInfo &Symbol = Obj.getWasmSymbol(Sym).Info;
      WasmYAML::SymbolInfo Info;
      Info.Index = SymbolIndex++;
      Info.Kind = static_cast<uint32_t>(Symbol.Kind);
      Info.Name = Symbol.Name;
      Info.Flags = Symbol.Flags;
      switch (Symbol.Kind) {
      case wasm::WASM_SYMBOL_TYPE_DATA:
        Info.DataRef = Symbol.DataRef;
        break;
      case wasm::WASM_SYMBOL_TYPE_FUNCTION:
      case wasm::WASM_SYMBOL_TYPE_GLOBAL:
      case wasm::WASM_SYMBOL_TYPE_TABLE:
      case wasm::WASM_SYMBOL_TYPE_TAG:
        Info.ElementIndex = Symbol.ElementIndex;
        break;
      case wasm::WASM_SYMBOL_TYPE_SECTION:
        Info.ElementIndex = Symbol.ElementIndex;
        break;
      }
      LinkingSec->SymbolTable.emplace_back(Info);
    }

    for (const wasm::WasmInitFunc &Func : Obj.linkingData().InitFunctions) {
      WasmYAML::InitFunction F{Func.Priority, Func.Symbol};
      LinkingSec->InitFunctions.emplace_back(F);
    }

    CustomSec = std::move(LinkingSec);
  } else if (WasmSec.Name == "producers") {
    std::unique_ptr<WasmYAML::ProducersSection> ProducersSec =
        std::make_unique<WasmYAML::ProducersSection>();
    const llvm::wasm::WasmProducerInfo &Info = Obj.getProducerInfo();
    for (auto &E : Info.Languages) {
      WasmYAML::ProducerEntry Producer;
      Producer.Name = E.first;
      Producer.Version = E.second;
      ProducersSec->Languages.push_back(Producer);
    }
    for (auto &E : Info.Tools) {
      WasmYAML::ProducerEntry Producer;
      Producer.Name = E.first;
      Producer.Version = E.second;
      ProducersSec->Tools.push_back(Producer);
    }
    for (auto &E : Info.SDKs) {
      WasmYAML::ProducerEntry Producer;
      Producer.Name = E.first;
      Producer.Version = E.second;
      ProducersSec->SDKs.push_back(Producer);
    }
    CustomSec = std::move(ProducersSec);
  } else if (WasmSec.Name == "target_features") {
    std::unique_ptr<WasmYAML::TargetFeaturesSection> TargetFeaturesSec =
        std::make_unique<WasmYAML::TargetFeaturesSection>();
    for (auto &E : Obj.getTargetFeatures()) {
      WasmYAML::FeatureEntry Feature;
      Feature.Prefix = E.Prefix;
      Feature.Name = E.Name;
      TargetFeaturesSec->Features.push_back(Feature);
    }
    CustomSec = std::move(TargetFeaturesSec);
  } else {
    CustomSec = std::make_unique<WasmYAML::CustomSection>(WasmSec.Name);
  }
  CustomSec->Payload = yaml::BinaryRef(WasmSec.Content);
  return CustomSec;
}

ErrorOr<WasmYAML::Object *> WasmDumper::dump() {
  auto Y = std::make_unique<WasmYAML::Object>();

  // Dump header
  Y->Header.Version = Obj.getHeader().Version;

  // Dump sections
  for (const auto &Sec : Obj.sections()) {
    const WasmSection &WasmSec = Obj.getWasmSection(Sec);
    std::unique_ptr<WasmYAML::Section> S;
    switch (WasmSec.Type) {
    case wasm::WASM_SEC_CUSTOM: {
      if (WasmSec.Name.starts_with("reloc.")) {
        // Relocations are attached the sections they apply to rather than
        // being represented as a custom section in the YAML output.
        continue;
      }
      S = dumpCustomSection(WasmSec);
      break;
    }
    case wasm::WASM_SEC_TYPE: {
      auto TypeSec = std::make_unique<WasmYAML::TypeSection>();
      uint32_t Index = 0;
      for (const auto &FunctionSig : Obj.types()) {
        WasmYAML::Signature Sig;
        Sig.Index = Index++;
        for (const auto &ParamType : FunctionSig.Params)
          Sig.ParamTypes.emplace_back(static_cast<uint32_t>(ParamType));
        for (const auto &ReturnType : FunctionSig.Returns)
          Sig.ReturnTypes.emplace_back(static_cast<uint32_t>(ReturnType));
        TypeSec->Signatures.push_back(Sig);
      }
      S = std::move(TypeSec);
      break;
    }
    case wasm::WASM_SEC_IMPORT: {
      auto ImportSec = std::make_unique<WasmYAML::ImportSection>();
      for (auto &Import : Obj.imports()) {
        WasmYAML::Import Im;
        Im.Module = Import.Module;
        Im.Field = Import.Field;
        Im.Kind = Import.Kind;
        switch (Im.Kind) {
        case wasm::WASM_EXTERNAL_FUNCTION:
          Im.SigIndex = Import.SigIndex;
          break;
        case wasm::WASM_EXTERNAL_GLOBAL:
          Im.GlobalImport.Type = Import.Global.Type;
          Im.GlobalImport.Mutable = Import.Global.Mutable;
          break;
        case wasm::WASM_EXTERNAL_TAG:
          Im.SigIndex = Import.SigIndex;
          break;
        case wasm::WASM_EXTERNAL_TABLE:
          // FIXME: Currently we always output an index of 0 for any imported
          // table.
          Im.TableImport = makeTable(0, Import.Table);
          break;
        case wasm::WASM_EXTERNAL_MEMORY:
          Im.Memory = makeLimits(Import.Memory);
          break;
        }
        ImportSec->Imports.push_back(Im);
      }
      S = std::move(ImportSec);
      break;
    }
    case wasm::WASM_SEC_FUNCTION: {
      auto FuncSec = std::make_unique<WasmYAML::FunctionSection>();
      for (const auto &Func : Obj.functions()) {
        FuncSec->FunctionTypes.push_back(Func.SigIndex);
      }
      S = std::move(FuncSec);
      break;
    }
    case wasm::WASM_SEC_TABLE: {
      auto TableSec = std::make_unique<WasmYAML::TableSection>();
      for (const wasm::WasmTable &Table : Obj.tables()) {
        TableSec->Tables.push_back(makeTable(Table.Index, Table.Type));
      }
      S = std::move(TableSec);
      break;
    }
    case wasm::WASM_SEC_MEMORY: {
      auto MemorySec = std::make_unique<WasmYAML::MemorySection>();
      for (const wasm::WasmLimits &Memory : Obj.memories()) {
        MemorySec->Memories.push_back(makeLimits(Memory));
      }
      S = std::move(MemorySec);
      break;
    }
    case wasm::WASM_SEC_TAG: {
      auto TagSec = std::make_unique<WasmYAML::TagSection>();
      for (auto &Tag : Obj.tags()) {
        TagSec->TagTypes.push_back(Tag.SigIndex);
      }
      S = std::move(TagSec);
      break;
    }
    case wasm::WASM_SEC_GLOBAL: {
      auto GlobalSec = std::make_unique<WasmYAML::GlobalSection>();
      for (auto &Global : Obj.globals()) {
        WasmYAML::Global G;
        G.Index = Global.Index;
        G.Type = Global.Type.Type;
        G.Mutable = Global.Type.Mutable;
        G.Init.Extended = Global.InitExpr.Extended;
        if (Global.InitExpr.Extended) {
          G.Init.Body = Global.InitExpr.Body;
        } else {
          G.Init.Inst = Global.InitExpr.Inst;
        }
        GlobalSec->Globals.push_back(G);
      }
      S = std::move(GlobalSec);
      break;
    }
    case wasm::WASM_SEC_START: {
      auto StartSec = std::make_unique<WasmYAML::StartSection>();
      StartSec->StartFunction = Obj.startFunction();
      S = std::move(StartSec);
      break;
    }
    case wasm::WASM_SEC_EXPORT: {
      auto ExportSec = std::make_unique<WasmYAML::ExportSection>();
      for (auto &Export : Obj.exports()) {
        WasmYAML::Export Ex;
        Ex.Name = Export.Name;
        Ex.Kind = Export.Kind;
        Ex.Index = Export.Index;
        ExportSec->Exports.push_back(Ex);
      }
      S = std::move(ExportSec);
      break;
    }
    case wasm::WASM_SEC_ELEM: {
      auto ElemSec = std::make_unique<WasmYAML::ElemSection>();
      for (auto &Segment : Obj.elements()) {
        WasmYAML::ElemSegment Seg;
        Seg.Flags = Segment.Flags;
        Seg.TableNumber = Segment.TableNumber;
        Seg.ElemKind = (uint32_t)Segment.ElemKind;
        Seg.Offset.Extended = Segment.Offset.Extended;
        if (Seg.Offset.Extended) {
          Seg.Offset.Body = yaml::BinaryRef(Segment.Offset.Body);
        } else {
          Seg.Offset.Inst = Segment.Offset.Inst;
        }
        append_range(Seg.Functions, Segment.Functions);
        ElemSec->Segments.push_back(Seg);
      }
      S = std::move(ElemSec);
      break;
    }
    case wasm::WASM_SEC_CODE: {
      auto CodeSec = std::make_unique<WasmYAML::CodeSection>();
      for (auto &Func : Obj.functions()) {
        WasmYAML::Function Function;
        Function.Index = Func.Index;
        for (auto &Local : Func.Locals) {
          WasmYAML::LocalDecl LocalDecl;
          LocalDecl.Type = Local.Type;
          LocalDecl.Count = Local.Count;
          Function.Locals.push_back(LocalDecl);
        }
        Function.Body = yaml::BinaryRef(Func.Body);
        CodeSec->Functions.push_back(Function);
      }
      S = std::move(CodeSec);
      break;
    }
    case wasm::WASM_SEC_DATA: {
      auto DataSec = std::make_unique<WasmYAML::DataSection>();
      for (const object::WasmSegment &Segment : Obj.dataSegments()) {
        WasmYAML::DataSegment Seg;
        Seg.SectionOffset = Segment.SectionOffset;
        Seg.InitFlags = Segment.Data.InitFlags;
        Seg.MemoryIndex = Segment.Data.MemoryIndex;
        Seg.Offset.Extended = Segment.Data.Offset.Extended;
        if (Seg.Offset.Extended) {
          Seg.Offset.Body = yaml::BinaryRef(Segment.Data.Offset.Body);
        } else {
          Seg.Offset.Inst = Segment.Data.Offset.Inst;
        }
        Seg.Content = yaml::BinaryRef(Segment.Data.Content);
        DataSec->Segments.push_back(Seg);
      }
      S = std::move(DataSec);
      break;
    }
    case wasm::WASM_SEC_DATACOUNT: {
      auto DataCountSec = std::make_unique<WasmYAML::DataCountSection>();
      DataCountSec->Count = Obj.dataSegments().size();
      S = std::move(DataCountSec);
      break;
    }
    default:
      llvm_unreachable("Unknown section type");
      break;
    }

    // Only propagate the section size encoding length if it's not the minimal
    // size or 5 (the default "padded" value). This is to avoid having every
    // YAML output polluted with this value when we usually don't care about it
    // (and avoid rewriting all the test expectations).
    if (WasmSec.HeaderSecSizeEncodingLen &&
        WasmSec.HeaderSecSizeEncodingLen !=
            getULEB128Size(WasmSec.Content.size()) &&
        WasmSec.HeaderSecSizeEncodingLen != 5)
      S->HeaderSecSizeEncodingLen = WasmSec.HeaderSecSizeEncodingLen;

    for (const wasm::WasmRelocation &Reloc : WasmSec.Relocations) {
      WasmYAML::Relocation R;
      R.Type = Reloc.Type;
      R.Index = Reloc.Index;
      R.Offset = Reloc.Offset;
      R.Addend = Reloc.Addend;
      S->Relocations.push_back(R);
    }
    Y->Sections.push_back(std::move(S));
  }

  return Y.release();
}

std::error_code wasm2yaml(raw_ostream &Out, const object::WasmObjectFile &Obj) {
  WasmDumper Dumper(Obj);
  ErrorOr<WasmYAML::Object *> YAMLOrErr = Dumper.dump();
  if (std::error_code EC = YAMLOrErr.getError())
    return EC;

  std::unique_ptr<WasmYAML::Object> YAML(YAMLOrErr.get());
  yaml::Output Yout(Out);
  Yout << *YAML;

  return std::error_code();
}
