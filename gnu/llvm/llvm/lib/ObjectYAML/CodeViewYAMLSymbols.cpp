//===- CodeViewYAMLSymbols.cpp - CodeView YAMLIO Symbol implementation ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/CodeViewYAMLSymbols.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/YAMLTraits.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::CodeViewYAML;
using namespace llvm::CodeViewYAML::detail;
using namespace llvm::yaml;

LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(TypeIndex)
LLVM_YAML_IS_SEQUENCE_VECTOR(LocalVariableAddrGap)

// We only need to declare these, the definitions are in CodeViewYAMLTypes.cpp
LLVM_YAML_DECLARE_SCALAR_TRAITS(APSInt, QuotingType::None)
LLVM_YAML_DECLARE_SCALAR_TRAITS(TypeIndex, QuotingType::None)

LLVM_YAML_DECLARE_ENUM_TRAITS(SymbolKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(FrameCookieKind)

LLVM_YAML_DECLARE_BITSET_TRAITS(CompileSym2Flags)
LLVM_YAML_DECLARE_BITSET_TRAITS(CompileSym3Flags)
LLVM_YAML_DECLARE_BITSET_TRAITS(ExportFlags)
LLVM_YAML_DECLARE_BITSET_TRAITS(PublicSymFlags)
LLVM_YAML_DECLARE_BITSET_TRAITS(LocalSymFlags)
LLVM_YAML_DECLARE_BITSET_TRAITS(ProcSymFlags)
LLVM_YAML_DECLARE_BITSET_TRAITS(FrameProcedureOptions)
LLVM_YAML_DECLARE_ENUM_TRAITS(CPUType)
LLVM_YAML_DECLARE_ENUM_TRAITS(RegisterId)
LLVM_YAML_DECLARE_ENUM_TRAITS(TrampolineType)
LLVM_YAML_DECLARE_ENUM_TRAITS(ThunkOrdinal)
LLVM_YAML_DECLARE_ENUM_TRAITS(JumpTableEntrySize)

LLVM_YAML_STRONG_TYPEDEF(StringRef, TypeName)

LLVM_YAML_DECLARE_SCALAR_TRAITS(TypeName, QuotingType::Single)

StringRef ScalarTraits<TypeName>::input(StringRef S, void *V, TypeName &T) {
  return ScalarTraits<StringRef>::input(S, V, T.value);
}

void ScalarTraits<TypeName>::output(const TypeName &T, void *V,
                                    raw_ostream &R) {
  ScalarTraits<StringRef>::output(T.value, V, R);
}

void ScalarEnumerationTraits<SymbolKind>::enumeration(IO &io,
                                                      SymbolKind &Value) {
  auto SymbolNames = getSymbolTypeNames();
  for (const auto &E : SymbolNames)
    io.enumCase(Value, E.Name.str().c_str(), E.Value);
}

void ScalarBitSetTraits<CompileSym2Flags>::bitset(IO &io,
                                                  CompileSym2Flags &Flags) {
  auto FlagNames = getCompileSym2FlagNames();
  for (const auto &E : FlagNames) {
    io.bitSetCase(Flags, E.Name.str().c_str(),
                  static_cast<CompileSym2Flags>(E.Value));
  }
}

void ScalarBitSetTraits<CompileSym3Flags>::bitset(IO &io,
                                                  CompileSym3Flags &Flags) {
  auto FlagNames = getCompileSym3FlagNames();
  for (const auto &E : FlagNames) {
    io.bitSetCase(Flags, E.Name.str().c_str(),
                  static_cast<CompileSym3Flags>(E.Value));
  }
}

void ScalarBitSetTraits<ExportFlags>::bitset(IO &io, ExportFlags &Flags) {
  auto FlagNames = getExportSymFlagNames();
  for (const auto &E : FlagNames) {
    io.bitSetCase(Flags, E.Name.str().c_str(),
                  static_cast<ExportFlags>(E.Value));
  }
}

void ScalarBitSetTraits<PublicSymFlags>::bitset(IO &io, PublicSymFlags &Flags) {
  auto FlagNames = getPublicSymFlagNames();
  for (const auto &E : FlagNames) {
    io.bitSetCase(Flags, E.Name.str().c_str(),
                  static_cast<PublicSymFlags>(E.Value));
  }
}

void ScalarBitSetTraits<LocalSymFlags>::bitset(IO &io, LocalSymFlags &Flags) {
  auto FlagNames = getLocalFlagNames();
  for (const auto &E : FlagNames) {
    io.bitSetCase(Flags, E.Name.str().c_str(),
                  static_cast<LocalSymFlags>(E.Value));
  }
}

void ScalarBitSetTraits<ProcSymFlags>::bitset(IO &io, ProcSymFlags &Flags) {
  auto FlagNames = getProcSymFlagNames();
  for (const auto &E : FlagNames) {
    io.bitSetCase(Flags, E.Name.str().c_str(),
                  static_cast<ProcSymFlags>(E.Value));
  }
}

void ScalarBitSetTraits<FrameProcedureOptions>::bitset(
    IO &io, FrameProcedureOptions &Flags) {
  auto FlagNames = getFrameProcSymFlagNames();
  for (const auto &E : FlagNames) {
    io.bitSetCase(Flags, E.Name.str().c_str(),
                  static_cast<FrameProcedureOptions>(E.Value));
  }
}

void ScalarEnumerationTraits<CPUType>::enumeration(IO &io, CPUType &Cpu) {
  auto CpuNames = getCPUTypeNames();
  for (const auto &E : CpuNames) {
    io.enumCase(Cpu, E.Name.str().c_str(), static_cast<CPUType>(E.Value));
  }
}

void ScalarEnumerationTraits<RegisterId>::enumeration(IO &io, RegisterId &Reg) {
  const auto *Header = static_cast<COFF::header *>(io.getContext());
  assert(Header && "The IO context is not initialized");

  std::optional<CPUType> CpuType;
  ArrayRef<EnumEntry<uint16_t>> RegNames;

  switch (Header->Machine) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    CpuType = CPUType::Pentium3;
    break;
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    CpuType = CPUType::X64;
    break;
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    CpuType = CPUType::ARMNT;
    break;
  case COFF::IMAGE_FILE_MACHINE_ARM64:
  case COFF::IMAGE_FILE_MACHINE_ARM64EC:
  case COFF::IMAGE_FILE_MACHINE_ARM64X:
    CpuType = CPUType::ARM64;
    break;
  }

  if (CpuType)
    RegNames = getRegisterNames(*CpuType);

  for (const auto &E : RegNames) {
    io.enumCase(Reg, E.Name.str().c_str(), static_cast<RegisterId>(E.Value));
  }
  io.enumFallback<Hex16>(Reg);
}

void ScalarEnumerationTraits<TrampolineType>::enumeration(
    IO &io, TrampolineType &Tramp) {
  auto TrampNames = getTrampolineNames();
  for (const auto &E : TrampNames) {
    io.enumCase(Tramp, E.Name.str().c_str(),
                static_cast<TrampolineType>(E.Value));
  }
}

void ScalarEnumerationTraits<ThunkOrdinal>::enumeration(IO &io,
                                                        ThunkOrdinal &Ord) {
  auto ThunkNames = getThunkOrdinalNames();
  for (const auto &E : ThunkNames) {
    io.enumCase(Ord, E.Name.str().c_str(), static_cast<ThunkOrdinal>(E.Value));
  }
}

void ScalarEnumerationTraits<FrameCookieKind>::enumeration(
    IO &io, FrameCookieKind &FC) {
  auto ThunkNames = getFrameCookieKindNames();
  for (const auto &E : ThunkNames) {
    io.enumCase(FC, E.Name.str().c_str(),
                static_cast<FrameCookieKind>(E.Value));
  }
}

void ScalarEnumerationTraits<JumpTableEntrySize>::enumeration(
    IO &io, JumpTableEntrySize &FC) {
  auto ThunkNames = getJumpTableEntrySizeNames();
  for (const auto &E : ThunkNames) {
    io.enumCase(FC, E.Name.str().c_str(),
                static_cast<JumpTableEntrySize>(E.Value));
  }
}

namespace llvm {
namespace yaml {
template <> struct MappingTraits<LocalVariableAddrRange> {
  static void mapping(IO &io, LocalVariableAddrRange &Range) {
    io.mapRequired("OffsetStart", Range.OffsetStart);
    io.mapRequired("ISectStart", Range.ISectStart);
    io.mapRequired("Range", Range.Range);
  }
};
template <> struct MappingTraits<LocalVariableAddrGap> {
  static void mapping(IO &io, LocalVariableAddrGap &Gap) {
    io.mapRequired("GapStartOffset", Gap.GapStartOffset);
    io.mapRequired("Range", Gap.Range);
  }
};
} // namespace yaml
} // namespace llvm

namespace llvm {
namespace CodeViewYAML {
namespace detail {

struct SymbolRecordBase {
  codeview::SymbolKind Kind;

  explicit SymbolRecordBase(codeview::SymbolKind K) : Kind(K) {}
  virtual ~SymbolRecordBase() = default;

  virtual void map(yaml::IO &io) = 0;
  virtual codeview::CVSymbol
  toCodeViewSymbol(BumpPtrAllocator &Allocator,
                   CodeViewContainer Container) const = 0;
  virtual Error fromCodeViewSymbol(codeview::CVSymbol Type) = 0;
};

template <typename T> struct SymbolRecordImpl : public SymbolRecordBase {
  explicit SymbolRecordImpl(codeview::SymbolKind K)
      : SymbolRecordBase(K), Symbol(static_cast<SymbolRecordKind>(K)) {}

  void map(yaml::IO &io) override;

  codeview::CVSymbol
  toCodeViewSymbol(BumpPtrAllocator &Allocator,
                   CodeViewContainer Container) const override {
    return SymbolSerializer::writeOneSymbol(Symbol, Allocator, Container);
  }

  Error fromCodeViewSymbol(codeview::CVSymbol CVS) override {
    return SymbolDeserializer::deserializeAs<T>(CVS, Symbol);
  }

  mutable T Symbol;
};

struct UnknownSymbolRecord : public SymbolRecordBase {
  explicit UnknownSymbolRecord(codeview::SymbolKind K) : SymbolRecordBase(K) {}

  void map(yaml::IO &io) override;

  CVSymbol toCodeViewSymbol(BumpPtrAllocator &Allocator,
                            CodeViewContainer Container) const override {
    RecordPrefix Prefix;
    uint32_t TotalLen = sizeof(RecordPrefix) + Data.size();
    Prefix.RecordKind = Kind;
    Prefix.RecordLen = TotalLen - 2;
    uint8_t *Buffer = Allocator.Allocate<uint8_t>(TotalLen);
    ::memcpy(Buffer, &Prefix, sizeof(RecordPrefix));
    ::memcpy(Buffer + sizeof(RecordPrefix), Data.data(), Data.size());
    return CVSymbol(ArrayRef<uint8_t>(Buffer, TotalLen));
  }

  Error fromCodeViewSymbol(CVSymbol CVS) override {
    this->Kind = CVS.kind();
    Data = CVS.RecordData.drop_front(sizeof(RecordPrefix));
    return Error::success();
  }

  std::vector<uint8_t> Data;
};

template <> void SymbolRecordImpl<ScopeEndSym>::map(IO &IO) {}

void UnknownSymbolRecord::map(yaml::IO &io) {
  yaml::BinaryRef Binary;
  if (io.outputting())
    Binary = yaml::BinaryRef(Data);
  io.mapRequired("Data", Binary);
  if (!io.outputting()) {
    std::string Str;
    raw_string_ostream OS(Str);
    Binary.writeAsBinary(OS);
    OS.flush();
    Data.assign(Str.begin(), Str.end());
  }
}

template <> void SymbolRecordImpl<Thunk32Sym>::map(IO &IO) {
  IO.mapRequired("Parent", Symbol.Parent);
  IO.mapRequired("End", Symbol.End);
  IO.mapRequired("Next", Symbol.Next);
  IO.mapRequired("Off", Symbol.Offset);
  IO.mapRequired("Seg", Symbol.Segment);
  IO.mapRequired("Len", Symbol.Length);
  IO.mapRequired("Ordinal", Symbol.Thunk);
}

template <> void SymbolRecordImpl<TrampolineSym>::map(IO &IO) {
  IO.mapRequired("Type", Symbol.Type);
  IO.mapRequired("Size", Symbol.Size);
  IO.mapRequired("ThunkOff", Symbol.ThunkOffset);
  IO.mapRequired("TargetOff", Symbol.TargetOffset);
  IO.mapRequired("ThunkSection", Symbol.ThunkSection);
  IO.mapRequired("TargetSection", Symbol.TargetSection);
}

template <> void SymbolRecordImpl<SectionSym>::map(IO &IO) {
  IO.mapRequired("SectionNumber", Symbol.SectionNumber);
  IO.mapRequired("Alignment", Symbol.Alignment);
  IO.mapRequired("Rva", Symbol.Rva);
  IO.mapRequired("Length", Symbol.Length);
  IO.mapRequired("Characteristics", Symbol.Characteristics);
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<CoffGroupSym>::map(IO &IO) {
  IO.mapRequired("Size", Symbol.Size);
  IO.mapRequired("Characteristics", Symbol.Characteristics);
  IO.mapRequired("Offset", Symbol.Offset);
  IO.mapRequired("Segment", Symbol.Segment);
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<ExportSym>::map(IO &IO) {
  IO.mapRequired("Ordinal", Symbol.Ordinal);
  IO.mapRequired("Flags", Symbol.Flags);
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<ProcSym>::map(IO &IO) {
  IO.mapOptional("PtrParent", Symbol.Parent, 0U);
  IO.mapOptional("PtrEnd", Symbol.End, 0U);
  IO.mapOptional("PtrNext", Symbol.Next, 0U);
  IO.mapRequired("CodeSize", Symbol.CodeSize);
  IO.mapRequired("DbgStart", Symbol.DbgStart);
  IO.mapRequired("DbgEnd", Symbol.DbgEnd);
  IO.mapRequired("FunctionType", Symbol.FunctionType);
  IO.mapOptional("Offset", Symbol.CodeOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("Flags", Symbol.Flags);
  IO.mapRequired("DisplayName", Symbol.Name);
}

template <> void SymbolRecordImpl<RegisterSym>::map(IO &IO) {
  IO.mapRequired("Type", Symbol.Index);
  IO.mapRequired("Seg", Symbol.Register);
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<PublicSym32>::map(IO &IO) {
  IO.mapRequired("Flags", Symbol.Flags);
  IO.mapOptional("Offset", Symbol.Offset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<ProcRefSym>::map(IO &IO) {
  IO.mapRequired("SumName", Symbol.SumName);
  IO.mapRequired("SymOffset", Symbol.SymOffset);
  IO.mapRequired("Mod", Symbol.Module);
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<EnvBlockSym>::map(IO &IO) {
  IO.mapRequired("Entries", Symbol.Fields);
}

template <> void SymbolRecordImpl<InlineSiteSym>::map(IO &IO) {
  IO.mapOptional("PtrParent", Symbol.Parent, 0U);
  IO.mapOptional("PtrEnd", Symbol.End, 0U);
  IO.mapRequired("Inlinee", Symbol.Inlinee);
  // TODO: The binary annotations
}

template <> void SymbolRecordImpl<LocalSym>::map(IO &IO) {
  IO.mapRequired("Type", Symbol.Type);
  IO.mapRequired("Flags", Symbol.Flags);

  IO.mapRequired("VarName", Symbol.Name);
}

template <> void SymbolRecordImpl<DefRangeSym>::map(IO &IO) {
  IO.mapRequired("Program", Symbol.Program);
  IO.mapRequired("Range", Symbol.Range);
  IO.mapRequired("Gaps", Symbol.Gaps);
}

template <> void SymbolRecordImpl<DefRangeSubfieldSym>::map(IO &IO) {
  IO.mapRequired("Program", Symbol.Program);
  IO.mapRequired("OffsetInParent", Symbol.OffsetInParent);
  IO.mapRequired("Range", Symbol.Range);
  IO.mapRequired("Gaps", Symbol.Gaps);
}

template <> void SymbolRecordImpl<DefRangeRegisterSym>::map(IO &IO) {
  IO.mapRequired("Register", Symbol.Hdr.Register);
  IO.mapRequired("MayHaveNoName", Symbol.Hdr.MayHaveNoName);
  IO.mapRequired("Range", Symbol.Range);
  IO.mapRequired("Gaps", Symbol.Gaps);
}

template <> void SymbolRecordImpl<DefRangeFramePointerRelSym>::map(IO &IO) {
  IO.mapRequired("Offset", Symbol.Hdr.Offset);
  IO.mapRequired("Range", Symbol.Range);
  IO.mapRequired("Gaps", Symbol.Gaps);
}

template <> void SymbolRecordImpl<DefRangeSubfieldRegisterSym>::map(IO &IO) {
  IO.mapRequired("Register", Symbol.Hdr.Register);
  IO.mapRequired("MayHaveNoName", Symbol.Hdr.MayHaveNoName);
  IO.mapRequired("OffsetInParent", Symbol.Hdr.OffsetInParent);
  IO.mapRequired("Range", Symbol.Range);
  IO.mapRequired("Gaps", Symbol.Gaps);
}

template <>
void SymbolRecordImpl<DefRangeFramePointerRelFullScopeSym>::map(IO &IO) {
  IO.mapRequired("Register", Symbol.Offset);
}

template <> void SymbolRecordImpl<DefRangeRegisterRelSym>::map(IO &IO) {
  IO.mapRequired("Register", Symbol.Hdr.Register);
  IO.mapRequired("Flags", Symbol.Hdr.Flags);
  IO.mapRequired("BasePointerOffset", Symbol.Hdr.BasePointerOffset);
  IO.mapRequired("Range", Symbol.Range);
  IO.mapRequired("Gaps", Symbol.Gaps);
}

template <> void SymbolRecordImpl<BlockSym>::map(IO &IO) {
  IO.mapOptional("PtrParent", Symbol.Parent, 0U);
  IO.mapOptional("PtrEnd", Symbol.End, 0U);
  IO.mapRequired("CodeSize", Symbol.CodeSize);
  IO.mapOptional("Offset", Symbol.CodeOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("BlockName", Symbol.Name);
}

template <> void SymbolRecordImpl<LabelSym>::map(IO &IO) {
  IO.mapOptional("Offset", Symbol.CodeOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("Flags", Symbol.Flags);
  IO.mapRequired("DisplayName", Symbol.Name);
}

template <> void SymbolRecordImpl<ObjNameSym>::map(IO &IO) {
  IO.mapRequired("Signature", Symbol.Signature);
  IO.mapRequired("ObjectName", Symbol.Name);
}

template <> void SymbolRecordImpl<Compile2Sym>::map(IO &IO) {
  IO.mapRequired("Flags", Symbol.Flags);
  IO.mapRequired("Machine", Symbol.Machine);
  IO.mapRequired("FrontendMajor", Symbol.VersionFrontendMajor);
  IO.mapRequired("FrontendMinor", Symbol.VersionFrontendMinor);
  IO.mapRequired("FrontendBuild", Symbol.VersionFrontendBuild);
  IO.mapRequired("BackendMajor", Symbol.VersionBackendMajor);
  IO.mapRequired("BackendMinor", Symbol.VersionBackendMinor);
  IO.mapRequired("BackendBuild", Symbol.VersionBackendBuild);
  IO.mapRequired("Version", Symbol.Version);
}

template <> void SymbolRecordImpl<Compile3Sym>::map(IO &IO) {
  IO.mapRequired("Flags", Symbol.Flags);
  IO.mapRequired("Machine", Symbol.Machine);
  IO.mapRequired("FrontendMajor", Symbol.VersionFrontendMajor);
  IO.mapRequired("FrontendMinor", Symbol.VersionFrontendMinor);
  IO.mapRequired("FrontendBuild", Symbol.VersionFrontendBuild);
  IO.mapRequired("FrontendQFE", Symbol.VersionFrontendQFE);
  IO.mapRequired("BackendMajor", Symbol.VersionBackendMajor);
  IO.mapRequired("BackendMinor", Symbol.VersionBackendMinor);
  IO.mapRequired("BackendBuild", Symbol.VersionBackendBuild);
  IO.mapRequired("BackendQFE", Symbol.VersionBackendQFE);
  IO.mapRequired("Version", Symbol.Version);
}

template <> void SymbolRecordImpl<FrameProcSym>::map(IO &IO) {
  IO.mapRequired("TotalFrameBytes", Symbol.TotalFrameBytes);
  IO.mapRequired("PaddingFrameBytes", Symbol.PaddingFrameBytes);
  IO.mapRequired("OffsetToPadding", Symbol.OffsetToPadding);
  IO.mapRequired("BytesOfCalleeSavedRegisters",
                 Symbol.BytesOfCalleeSavedRegisters);
  IO.mapRequired("OffsetOfExceptionHandler", Symbol.OffsetOfExceptionHandler);
  IO.mapRequired("SectionIdOfExceptionHandler",
                 Symbol.SectionIdOfExceptionHandler);
  IO.mapRequired("Flags", Symbol.Flags);
}

template <> void SymbolRecordImpl<CallSiteInfoSym>::map(IO &IO) {
  IO.mapOptional("Offset", Symbol.CodeOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("Type", Symbol.Type);
}

template <> void SymbolRecordImpl<FileStaticSym>::map(IO &IO) {
  IO.mapRequired("Index", Symbol.Index);
  IO.mapRequired("ModFilenameOffset", Symbol.ModFilenameOffset);
  IO.mapRequired("Flags", Symbol.Flags);
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<HeapAllocationSiteSym>::map(IO &IO) {
  IO.mapOptional("Offset", Symbol.CodeOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("CallInstructionSize", Symbol.CallInstructionSize);
  IO.mapRequired("Type", Symbol.Type);
}

template <> void SymbolRecordImpl<FrameCookieSym>::map(IO &IO) {
  IO.mapRequired("Register", Symbol.Register);
  IO.mapRequired("CookieKind", Symbol.CookieKind);
  IO.mapRequired("Flags", Symbol.Flags);
}

template <> void SymbolRecordImpl<CallerSym>::map(IO &IO) {
  IO.mapRequired("FuncID", Symbol.Indices);
}

template <> void SymbolRecordImpl<UDTSym>::map(IO &IO) {
  IO.mapRequired("Type", Symbol.Type);
  IO.mapRequired("UDTName", Symbol.Name);
}

template <> void SymbolRecordImpl<BuildInfoSym>::map(IO &IO) {
  IO.mapRequired("BuildId", Symbol.BuildId);
}

template <> void SymbolRecordImpl<BPRelativeSym>::map(IO &IO) {
  IO.mapRequired("Offset", Symbol.Offset);
  IO.mapRequired("Type", Symbol.Type);
  IO.mapRequired("VarName", Symbol.Name);
}

template <> void SymbolRecordImpl<RegRelativeSym>::map(IO &IO) {
  IO.mapRequired("Offset", Symbol.Offset);
  IO.mapRequired("Type", Symbol.Type);
  IO.mapRequired("Register", Symbol.Register);
  IO.mapRequired("VarName", Symbol.Name);
}

template <> void SymbolRecordImpl<ConstantSym>::map(IO &IO) {
  IO.mapRequired("Type", Symbol.Type);
  IO.mapRequired("Value", Symbol.Value);
  IO.mapRequired("Name", Symbol.Name);
}

template <> void SymbolRecordImpl<DataSym>::map(IO &IO) {
  IO.mapRequired("Type", Symbol.Type);
  IO.mapOptional("Offset", Symbol.DataOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("DisplayName", Symbol.Name);
}

template <> void SymbolRecordImpl<ThreadLocalDataSym>::map(IO &IO) {
  IO.mapRequired("Type", Symbol.Type);
  IO.mapOptional("Offset", Symbol.DataOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("DisplayName", Symbol.Name);
}

template <> void SymbolRecordImpl<UsingNamespaceSym>::map(IO &IO) {
  IO.mapRequired("Namespace", Symbol.Name);
}

template <> void SymbolRecordImpl<AnnotationSym>::map(IO &IO) {
  IO.mapOptional("Offset", Symbol.CodeOffset, 0U);
  IO.mapOptional("Segment", Symbol.Segment, uint16_t(0));
  IO.mapRequired("Strings", Symbol.Strings);
}

template <> void SymbolRecordImpl<JumpTableSym>::map(IO &IO) {
  IO.mapRequired("BaseOffset", Symbol.BaseOffset);
  IO.mapRequired("BaseSegment", Symbol.BaseSegment);
  IO.mapRequired("SwitchType", Symbol.SwitchType);
  IO.mapRequired("BranchOffset", Symbol.BranchOffset);
  IO.mapRequired("TableOffset", Symbol.TableOffset);
  IO.mapRequired("BranchSegment", Symbol.BranchSegment);
  IO.mapRequired("TableSegment", Symbol.TableSegment);
  IO.mapRequired("EntriesCount", Symbol.EntriesCount);
}

} // end namespace detail
} // end namespace CodeViewYAML
} // end namespace llvm

CVSymbol CodeViewYAML::SymbolRecord::toCodeViewSymbol(
    BumpPtrAllocator &Allocator, CodeViewContainer Container) const {
  return Symbol->toCodeViewSymbol(Allocator, Container);
}

namespace llvm {
namespace yaml {

template <> struct MappingTraits<SymbolRecordBase> {
  static void mapping(IO &io, SymbolRecordBase &Record) { Record.map(io); }
};

} // end namespace yaml
} // end namespace llvm

template <typename SymbolType>
static inline Expected<CodeViewYAML::SymbolRecord>
fromCodeViewSymbolImpl(CVSymbol Symbol) {
  CodeViewYAML::SymbolRecord Result;

  auto Impl = std::make_shared<SymbolType>(Symbol.kind());
  if (auto EC = Impl->fromCodeViewSymbol(Symbol))
    return std::move(EC);
  Result.Symbol = Impl;
  return Result;
}

Expected<CodeViewYAML::SymbolRecord>
CodeViewYAML::SymbolRecord::fromCodeViewSymbol(CVSymbol Symbol) {
#define SYMBOL_RECORD(EnumName, EnumVal, ClassName)                            \
  case EnumName:                                                               \
    return fromCodeViewSymbolImpl<SymbolRecordImpl<ClassName>>(Symbol);
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)           \
  SYMBOL_RECORD(EnumName, EnumVal, ClassName)
  switch (Symbol.kind()) {
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
  default:
    return fromCodeViewSymbolImpl<UnknownSymbolRecord>(Symbol);
  }
  return make_error<CodeViewError>(cv_error_code::corrupt_record);
}

template <typename ConcreteType>
static void mapSymbolRecordImpl(IO &IO, const char *Class, SymbolKind Kind,
                                CodeViewYAML::SymbolRecord &Obj) {
  if (!IO.outputting())
    Obj.Symbol = std::make_shared<ConcreteType>(Kind);

  IO.mapRequired(Class, *Obj.Symbol);
}

void MappingTraits<CodeViewYAML::SymbolRecord>::mapping(
    IO &IO, CodeViewYAML::SymbolRecord &Obj) {
  SymbolKind Kind;
  if (IO.outputting())
    Kind = Obj.Symbol->Kind;
  IO.mapRequired("Kind", Kind);

#define SYMBOL_RECORD(EnumName, EnumVal, ClassName)                            \
  case EnumName:                                                               \
    mapSymbolRecordImpl<SymbolRecordImpl<ClassName>>(IO, #ClassName, Kind,     \
                                                     Obj);                     \
    break;
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)           \
  SYMBOL_RECORD(EnumName, EnumVal, ClassName)
  switch (Kind) {
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
  default:
    mapSymbolRecordImpl<UnknownSymbolRecord>(IO, "UnknownSym", Kind, Obj);
  }
}
