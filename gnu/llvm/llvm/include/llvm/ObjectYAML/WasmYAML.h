//===- WasmYAML.h - Wasm YAMLIO implementation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of wasm binaries.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_WASMYAML_H
#define LLVM_OBJECTYAML_WASMYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Casting.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
namespace WasmYAML {

LLVM_YAML_STRONG_TYPEDEF(uint32_t, SectionType)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ValueType)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, TableType)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, SignatureForm)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ExportKind)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, Opcode)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, RelocType)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, SymbolFlags)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, SymbolKind)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, SegmentFlags)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, LimitFlags)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ComdatKind)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, FeaturePolicyPrefix)

struct FileHeader {
  yaml::Hex32 Version;
};

struct Limits {
  LimitFlags Flags;
  yaml::Hex32 Minimum;
  yaml::Hex32 Maximum;
};

struct Table {
  TableType ElemType;
  Limits TableLimits;
  uint32_t Index;
};

struct Export {
  StringRef Name;
  ExportKind Kind;
  uint32_t Index;
};

struct InitExpr {
  InitExpr() {}
  bool Extended;
  union {
    wasm::WasmInitExprMVP Inst;
    yaml::BinaryRef Body;
  };
};

struct ElemSegment {
  uint32_t Flags;
  uint32_t TableNumber;
  ValueType ElemKind;
  InitExpr Offset;
  std::vector<uint32_t> Functions;
};

struct Global {
  uint32_t Index;
  ValueType Type;
  bool Mutable;
  InitExpr Init;
};

struct Import {
  Import() {}
  StringRef Module;
  StringRef Field;
  ExportKind Kind;
  union {
    uint32_t SigIndex;
    Table TableImport;
    Limits Memory;
    uint32_t TagIndex;
    Global GlobalImport;
  };
};

struct LocalDecl {
  ValueType Type;
  uint32_t Count;
};

struct Function {
  uint32_t Index;
  std::vector<LocalDecl> Locals;
  yaml::BinaryRef Body;
};

struct Relocation {
  RelocType Type;
  uint32_t Index;
  // TODO(wvo): this would strictly be better as Hex64, but that will change
  // all existing obj2yaml output.
  yaml::Hex32 Offset;
  int64_t Addend;
};

struct DataSegment {
  uint32_t SectionOffset;
  uint32_t InitFlags;
  uint32_t MemoryIndex;
  InitExpr Offset;
  yaml::BinaryRef Content;
};

struct NameEntry {
  uint32_t Index;
  StringRef Name;
};

struct ProducerEntry {
  std::string Name;
  std::string Version;
};

struct FeatureEntry {
  FeaturePolicyPrefix Prefix;
  std::string Name;
};

struct SegmentInfo {
  uint32_t Index;
  StringRef Name;
  uint32_t Alignment;
  SegmentFlags Flags;
};

struct Signature {
  uint32_t Index;
  SignatureForm Form = wasm::WASM_TYPE_FUNC;
  std::vector<ValueType> ParamTypes;
  std::vector<ValueType> ReturnTypes;
};

struct SymbolInfo {
  uint32_t Index;
  StringRef Name;
  SymbolKind Kind;
  SymbolFlags Flags;
  union {
    uint32_t ElementIndex;
    wasm::WasmDataReference DataRef;
  };
};

struct InitFunction {
  uint32_t Priority;
  uint32_t Symbol;
};

struct ComdatEntry {
  ComdatKind Kind;
  uint32_t Index;
};

struct Comdat {
  StringRef Name;
  std::vector<ComdatEntry> Entries;
};

struct Section {
  explicit Section(SectionType SecType) : Type(SecType) {}
  virtual ~Section();

  SectionType Type;
  std::vector<Relocation> Relocations;
  std::optional<uint8_t> HeaderSecSizeEncodingLen;
};

struct CustomSection : Section {
  explicit CustomSection(StringRef Name)
      : Section(wasm::WASM_SEC_CUSTOM), Name(Name) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_CUSTOM;
  }

  StringRef Name;
  yaml::BinaryRef Payload;
};

struct DylinkImportInfo {
  StringRef Module;
  StringRef Field;
  SymbolFlags Flags;
};

struct DylinkExportInfo {
  StringRef Name;
  SymbolFlags Flags;
};

struct DylinkSection : CustomSection {
  DylinkSection() : CustomSection("dylink.0") {}

  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "dylink.0";
  }

  uint32_t MemorySize;
  uint32_t MemoryAlignment;
  uint32_t TableSize;
  uint32_t TableAlignment;
  std::vector<StringRef> Needed;
  std::vector<DylinkImportInfo> ImportInfo;
  std::vector<DylinkExportInfo> ExportInfo;
};

struct NameSection : CustomSection {
  NameSection() : CustomSection("name") {}

  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "name";
  }

  std::vector<NameEntry> FunctionNames;
  std::vector<NameEntry> GlobalNames;
  std::vector<NameEntry> DataSegmentNames;
};

struct LinkingSection : CustomSection {
  LinkingSection() : CustomSection("linking") {}

  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "linking";
  }

  uint32_t Version;
  std::vector<SymbolInfo> SymbolTable;
  std::vector<SegmentInfo> SegmentInfos;
  std::vector<InitFunction> InitFunctions;
  std::vector<Comdat> Comdats;
};

struct ProducersSection : CustomSection {
  ProducersSection() : CustomSection("producers") {}

  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "producers";
  }

  std::vector<ProducerEntry> Languages;
  std::vector<ProducerEntry> Tools;
  std::vector<ProducerEntry> SDKs;
};

struct TargetFeaturesSection : CustomSection {
  TargetFeaturesSection() : CustomSection("target_features") {}

  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "target_features";
  }

  std::vector<FeatureEntry> Features;
};

struct TypeSection : Section {
  TypeSection() : Section(wasm::WASM_SEC_TYPE) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_TYPE;
  }

  std::vector<Signature> Signatures;
};

struct ImportSection : Section {
  ImportSection() : Section(wasm::WASM_SEC_IMPORT) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_IMPORT;
  }

  std::vector<Import> Imports;
};

struct FunctionSection : Section {
  FunctionSection() : Section(wasm::WASM_SEC_FUNCTION) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_FUNCTION;
  }

  std::vector<uint32_t> FunctionTypes;
};

struct TableSection : Section {
  TableSection() : Section(wasm::WASM_SEC_TABLE) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_TABLE;
  }

  std::vector<Table> Tables;
};

struct MemorySection : Section {
  MemorySection() : Section(wasm::WASM_SEC_MEMORY) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_MEMORY;
  }

  std::vector<Limits> Memories;
};

struct TagSection : Section {
  TagSection() : Section(wasm::WASM_SEC_TAG) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_TAG;
  }

  std::vector<uint32_t> TagTypes;
};

struct GlobalSection : Section {
  GlobalSection() : Section(wasm::WASM_SEC_GLOBAL) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_GLOBAL;
  }

  std::vector<Global> Globals;
};

struct ExportSection : Section {
  ExportSection() : Section(wasm::WASM_SEC_EXPORT) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_EXPORT;
  }

  std::vector<Export> Exports;
};

struct StartSection : Section {
  StartSection() : Section(wasm::WASM_SEC_START) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_START;
  }

  uint32_t StartFunction;
};

struct ElemSection : Section {
  ElemSection() : Section(wasm::WASM_SEC_ELEM) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_ELEM;
  }

  std::vector<ElemSegment> Segments;
};

struct CodeSection : Section {
  CodeSection() : Section(wasm::WASM_SEC_CODE) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_CODE;
  }

  std::vector<Function> Functions;
};

struct DataSection : Section {
  DataSection() : Section(wasm::WASM_SEC_DATA) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_DATA;
  }

  std::vector<DataSegment> Segments;
};

struct DataCountSection : Section {
  DataCountSection() : Section(wasm::WASM_SEC_DATACOUNT) {}

  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_DATACOUNT;
  }

  uint32_t Count;
};

struct Object {
  FileHeader Header;
  std::vector<std::unique_ptr<Section>> Sections;
};

} // end namespace WasmYAML
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(std::unique_ptr<llvm::WasmYAML::Section>)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Signature)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::ValueType)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Table)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Import)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Export)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::ElemSegment)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Limits)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::DataSegment)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Global)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Function)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::LocalDecl)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Relocation)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::NameEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::ProducerEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::FeatureEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::SegmentInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::SymbolInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::InitFunction)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::ComdatEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::Comdat)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::DylinkImportInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::WasmYAML::DylinkExportInfo)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<WasmYAML::FileHeader> {
  static void mapping(IO &IO, WasmYAML::FileHeader &FileHdr);
};

template <> struct MappingTraits<std::unique_ptr<WasmYAML::Section>> {
  static void mapping(IO &IO, std::unique_ptr<WasmYAML::Section> &Section);
};

template <> struct MappingTraits<WasmYAML::Object> {
  static void mapping(IO &IO, WasmYAML::Object &Object);
};

template <> struct MappingTraits<WasmYAML::Import> {
  static void mapping(IO &IO, WasmYAML::Import &Import);
};

template <> struct MappingTraits<WasmYAML::Export> {
  static void mapping(IO &IO, WasmYAML::Export &Export);
};

template <> struct MappingTraits<WasmYAML::Global> {
  static void mapping(IO &IO, WasmYAML::Global &Global);
};

template <> struct ScalarBitSetTraits<WasmYAML::LimitFlags> {
  static void bitset(IO &IO, WasmYAML::LimitFlags &Value);
};

template <> struct ScalarBitSetTraits<WasmYAML::SymbolFlags> {
  static void bitset(IO &IO, WasmYAML::SymbolFlags &Value);
};

template <> struct ScalarEnumerationTraits<WasmYAML::SymbolKind> {
  static void enumeration(IO &IO, WasmYAML::SymbolKind &Kind);
};

template <> struct ScalarBitSetTraits<WasmYAML::SegmentFlags> {
  static void bitset(IO &IO, WasmYAML::SegmentFlags &Value);
};

template <> struct ScalarEnumerationTraits<WasmYAML::SectionType> {
  static void enumeration(IO &IO, WasmYAML::SectionType &Type);
};

template <> struct MappingTraits<WasmYAML::Signature> {
  static void mapping(IO &IO, WasmYAML::Signature &Signature);
};

template <> struct MappingTraits<WasmYAML::Table> {
  static void mapping(IO &IO, WasmYAML::Table &Table);
};

template <> struct MappingTraits<WasmYAML::Limits> {
  static void mapping(IO &IO, WasmYAML::Limits &Limits);
};

template <> struct MappingTraits<WasmYAML::Function> {
  static void mapping(IO &IO, WasmYAML::Function &Function);
};

template <> struct MappingTraits<WasmYAML::Relocation> {
  static void mapping(IO &IO, WasmYAML::Relocation &Relocation);
};

template <> struct MappingTraits<WasmYAML::NameEntry> {
  static void mapping(IO &IO, WasmYAML::NameEntry &NameEntry);
};

template <> struct MappingTraits<WasmYAML::ProducerEntry> {
  static void mapping(IO &IO, WasmYAML::ProducerEntry &ProducerEntry);
};

template <> struct ScalarEnumerationTraits<WasmYAML::FeaturePolicyPrefix> {
  static void enumeration(IO &IO, WasmYAML::FeaturePolicyPrefix &Prefix);
};

template <> struct MappingTraits<WasmYAML::FeatureEntry> {
  static void mapping(IO &IO, WasmYAML::FeatureEntry &FeatureEntry);
};

template <> struct MappingTraits<WasmYAML::SegmentInfo> {
  static void mapping(IO &IO, WasmYAML::SegmentInfo &SegmentInfo);
};

template <> struct MappingTraits<WasmYAML::LocalDecl> {
  static void mapping(IO &IO, WasmYAML::LocalDecl &LocalDecl);
};

template <> struct MappingTraits<WasmYAML::InitExpr> {
  static void mapping(IO &IO, WasmYAML::InitExpr &Expr);
};

template <> struct MappingTraits<WasmYAML::DataSegment> {
  static void mapping(IO &IO, WasmYAML::DataSegment &Segment);
};

template <> struct MappingTraits<WasmYAML::ElemSegment> {
  static void mapping(IO &IO, WasmYAML::ElemSegment &Segment);
};

template <> struct MappingTraits<WasmYAML::SymbolInfo> {
  static void mapping(IO &IO, WasmYAML::SymbolInfo &Info);
};

template <> struct MappingTraits<WasmYAML::InitFunction> {
  static void mapping(IO &IO, WasmYAML::InitFunction &Init);
};

template <> struct ScalarEnumerationTraits<WasmYAML::ComdatKind> {
  static void enumeration(IO &IO, WasmYAML::ComdatKind &Kind);
};

template <> struct MappingTraits<WasmYAML::ComdatEntry> {
  static void mapping(IO &IO, WasmYAML::ComdatEntry &ComdatEntry);
};

template <> struct MappingTraits<WasmYAML::Comdat> {
  static void mapping(IO &IO, WasmYAML::Comdat &Comdat);
};

template <> struct ScalarEnumerationTraits<WasmYAML::ValueType> {
  static void enumeration(IO &IO, WasmYAML::ValueType &Type);
};

template <> struct ScalarEnumerationTraits<WasmYAML::ExportKind> {
  static void enumeration(IO &IO, WasmYAML::ExportKind &Kind);
};

template <> struct ScalarEnumerationTraits<WasmYAML::TableType> {
  static void enumeration(IO &IO, WasmYAML::TableType &Type);
};

template <> struct ScalarEnumerationTraits<WasmYAML::Opcode> {
  static void enumeration(IO &IO, WasmYAML::Opcode &Opcode);
};

template <> struct ScalarEnumerationTraits<WasmYAML::RelocType> {
  static void enumeration(IO &IO, WasmYAML::RelocType &Kind);
};

template <> struct MappingTraits<WasmYAML::DylinkImportInfo> {
  static void mapping(IO &IO, WasmYAML::DylinkImportInfo &Info);
};

template <> struct MappingTraits<WasmYAML::DylinkExportInfo> {
  static void mapping(IO &IO, WasmYAML::DylinkExportInfo &Info);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_WASMYAML_H
