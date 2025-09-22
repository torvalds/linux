//===-- InstrProfCorrelator.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/InstrProfCorrelator.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFLocationExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/WithColor.h"
#include <optional>

#define DEBUG_TYPE "correlator"

using namespace llvm;

/// Get profile section.
Expected<object::SectionRef> getInstrProfSection(const object::ObjectFile &Obj,
                                                 InstrProfSectKind IPSK) {
  // On COFF, the getInstrProfSectionName returns the section names may followed
  // by "$M". The linker removes the dollar and everything after it in the final
  // binary. Do the same to match.
  Triple::ObjectFormatType ObjFormat = Obj.getTripleObjectFormat();
  auto StripSuffix = [ObjFormat](StringRef N) {
    return ObjFormat == Triple::COFF ? N.split('$').first : N;
  };
  std::string ExpectedSectionName =
      getInstrProfSectionName(IPSK, ObjFormat,
                              /*AddSegmentInfo=*/false);
  ExpectedSectionName = StripSuffix(ExpectedSectionName);
  for (auto &Section : Obj.sections()) {
    if (auto SectionName = Section.getName())
      if (*SectionName == ExpectedSectionName)
        return Section;
  }
  return make_error<InstrProfError>(
      instrprof_error::unable_to_correlate_profile,
      "could not find section (" + Twine(ExpectedSectionName) + ")");
}

const char *InstrProfCorrelator::FunctionNameAttributeName = "Function Name";
const char *InstrProfCorrelator::CFGHashAttributeName = "CFG Hash";
const char *InstrProfCorrelator::NumCountersAttributeName = "Num Counters";

llvm::Expected<std::unique_ptr<InstrProfCorrelator::Context>>
InstrProfCorrelator::Context::get(std::unique_ptr<MemoryBuffer> Buffer,
                                  const object::ObjectFile &Obj,
                                  ProfCorrelatorKind FileKind) {
  auto C = std::make_unique<Context>();
  auto CountersSection = getInstrProfSection(Obj, IPSK_cnts);
  if (auto Err = CountersSection.takeError())
    return std::move(Err);
  if (FileKind == InstrProfCorrelator::BINARY) {
    auto DataSection = getInstrProfSection(Obj, IPSK_covdata);
    if (auto Err = DataSection.takeError())
      return std::move(Err);
    auto DataOrErr = DataSection->getContents();
    if (!DataOrErr)
      return DataOrErr.takeError();
    auto NameSection = getInstrProfSection(Obj, IPSK_covname);
    if (auto Err = NameSection.takeError())
      return std::move(Err);
    auto NameOrErr = NameSection->getContents();
    if (!NameOrErr)
      return NameOrErr.takeError();
    C->DataStart = DataOrErr->data();
    C->DataEnd = DataOrErr->data() + DataOrErr->size();
    C->NameStart = NameOrErr->data();
    C->NameSize = NameOrErr->size();
  }
  C->Buffer = std::move(Buffer);
  C->CountersSectionStart = CountersSection->getAddress();
  C->CountersSectionEnd = C->CountersSectionStart + CountersSection->getSize();
  // In COFF object file, there's a null byte at the beginning of the counter
  // section which doesn't exist in raw profile.
  if (Obj.getTripleObjectFormat() == Triple::COFF)
    ++C->CountersSectionStart;

  C->ShouldSwapBytes = Obj.isLittleEndian() != sys::IsLittleEndianHost;
  return Expected<std::unique_ptr<Context>>(std::move(C));
}

llvm::Expected<std::unique_ptr<InstrProfCorrelator>>
InstrProfCorrelator::get(StringRef Filename, ProfCorrelatorKind FileKind) {
  if (FileKind == DEBUG_INFO) {
    auto DsymObjectsOrErr =
        object::MachOObjectFile::findDsymObjectMembers(Filename);
    if (auto Err = DsymObjectsOrErr.takeError())
      return std::move(Err);
    if (!DsymObjectsOrErr->empty()) {
      // TODO: Enable profile correlation when there are multiple objects in a
      // dSYM bundle.
      if (DsymObjectsOrErr->size() > 1)
        return make_error<InstrProfError>(
            instrprof_error::unable_to_correlate_profile,
            "using multiple objects is not yet supported");
      Filename = *DsymObjectsOrErr->begin();
    }
    auto BufferOrErr = errorOrToExpected(MemoryBuffer::getFile(Filename));
    if (auto Err = BufferOrErr.takeError())
      return std::move(Err);

    return get(std::move(*BufferOrErr), FileKind);
  }
  if (FileKind == BINARY) {
    auto BufferOrErr = errorOrToExpected(MemoryBuffer::getFile(Filename));
    if (auto Err = BufferOrErr.takeError())
      return std::move(Err);

    return get(std::move(*BufferOrErr), FileKind);
  }
  return make_error<InstrProfError>(
      instrprof_error::unable_to_correlate_profile,
      "unsupported correlation kind (only DWARF debug info and Binary format "
      "(ELF/COFF) are supported)");
}

llvm::Expected<std::unique_ptr<InstrProfCorrelator>>
InstrProfCorrelator::get(std::unique_ptr<MemoryBuffer> Buffer,
                         ProfCorrelatorKind FileKind) {
  auto BinOrErr = object::createBinary(*Buffer);
  if (auto Err = BinOrErr.takeError())
    return std::move(Err);

  if (auto *Obj = dyn_cast<object::ObjectFile>(BinOrErr->get())) {
    auto CtxOrErr = Context::get(std::move(Buffer), *Obj, FileKind);
    if (auto Err = CtxOrErr.takeError())
      return std::move(Err);
    auto T = Obj->makeTriple();
    if (T.isArch64Bit())
      return InstrProfCorrelatorImpl<uint64_t>::get(std::move(*CtxOrErr), *Obj,
                                                    FileKind);
    if (T.isArch32Bit())
      return InstrProfCorrelatorImpl<uint32_t>::get(std::move(*CtxOrErr), *Obj,
                                                    FileKind);
  }
  return make_error<InstrProfError>(
      instrprof_error::unable_to_correlate_profile, "not an object file");
}

std::optional<size_t> InstrProfCorrelator::getDataSize() const {
  if (auto *C = dyn_cast<InstrProfCorrelatorImpl<uint32_t>>(this)) {
    return C->getDataSize();
  } else if (auto *C = dyn_cast<InstrProfCorrelatorImpl<uint64_t>>(this)) {
    return C->getDataSize();
  }
  return {};
}

namespace llvm {

template <>
InstrProfCorrelatorImpl<uint32_t>::InstrProfCorrelatorImpl(
    std::unique_ptr<InstrProfCorrelator::Context> Ctx)
    : InstrProfCorrelatorImpl(InstrProfCorrelatorKind::CK_32Bit,
                              std::move(Ctx)) {}
template <>
InstrProfCorrelatorImpl<uint64_t>::InstrProfCorrelatorImpl(
    std::unique_ptr<InstrProfCorrelator::Context> Ctx)
    : InstrProfCorrelatorImpl(InstrProfCorrelatorKind::CK_64Bit,
                              std::move(Ctx)) {}
template <>
bool InstrProfCorrelatorImpl<uint32_t>::classof(const InstrProfCorrelator *C) {
  return C->getKind() == InstrProfCorrelatorKind::CK_32Bit;
}
template <>
bool InstrProfCorrelatorImpl<uint64_t>::classof(const InstrProfCorrelator *C) {
  return C->getKind() == InstrProfCorrelatorKind::CK_64Bit;
}

} // end namespace llvm

template <class IntPtrT>
llvm::Expected<std::unique_ptr<InstrProfCorrelatorImpl<IntPtrT>>>
InstrProfCorrelatorImpl<IntPtrT>::get(
    std::unique_ptr<InstrProfCorrelator::Context> Ctx,
    const object::ObjectFile &Obj, ProfCorrelatorKind FileKind) {
  if (FileKind == DEBUG_INFO) {
    if (Obj.isELF() || Obj.isMachO()) {
      auto DICtx = DWARFContext::create(Obj);
      return std::make_unique<DwarfInstrProfCorrelator<IntPtrT>>(
          std::move(DICtx), std::move(Ctx));
    }
    return make_error<InstrProfError>(
        instrprof_error::unable_to_correlate_profile,
        "unsupported debug info format (only DWARF is supported)");
  }
  if (Obj.isELF() || Obj.isCOFF())
    return std::make_unique<BinaryInstrProfCorrelator<IntPtrT>>(std::move(Ctx));
  return make_error<InstrProfError>(
      instrprof_error::unable_to_correlate_profile,
      "unsupported binary format (only ELF and COFF are supported)");
}

template <class IntPtrT>
Error InstrProfCorrelatorImpl<IntPtrT>::correlateProfileData(int MaxWarnings) {
  assert(Data.empty() && Names.empty() && NamesVec.empty());
  correlateProfileDataImpl(MaxWarnings);
  if (this->Data.empty())
    return make_error<InstrProfError>(
        instrprof_error::unable_to_correlate_profile,
        "could not find any profile data metadata in correlated file");
  Error Result = correlateProfileNameImpl();
  this->CounterOffsets.clear();
  this->NamesVec.clear();
  return Result;
}

template <> struct yaml::MappingTraits<InstrProfCorrelator::CorrelationData> {
  static void mapping(yaml::IO &io,
                      InstrProfCorrelator::CorrelationData &Data) {
    io.mapRequired("Probes", Data.Probes);
  }
};

template <> struct yaml::MappingTraits<InstrProfCorrelator::Probe> {
  static void mapping(yaml::IO &io, InstrProfCorrelator::Probe &P) {
    io.mapRequired("Function Name", P.FunctionName);
    io.mapOptional("Linkage Name", P.LinkageName);
    io.mapRequired("CFG Hash", P.CFGHash);
    io.mapRequired("Counter Offset", P.CounterOffset);
    io.mapRequired("Num Counters", P.NumCounters);
    io.mapOptional("File", P.FilePath);
    io.mapOptional("Line", P.LineNumber);
  }
};

template <> struct yaml::SequenceElementTraits<InstrProfCorrelator::Probe> {
  static const bool flow = false;
};

template <class IntPtrT>
Error InstrProfCorrelatorImpl<IntPtrT>::dumpYaml(int MaxWarnings,
                                                 raw_ostream &OS) {
  InstrProfCorrelator::CorrelationData Data;
  correlateProfileDataImpl(MaxWarnings, &Data);
  if (Data.Probes.empty())
    return make_error<InstrProfError>(
        instrprof_error::unable_to_correlate_profile,
        "could not find any profile data metadata in debug info");
  yaml::Output YamlOS(OS);
  YamlOS << Data;
  return Error::success();
}

template <class IntPtrT>
void InstrProfCorrelatorImpl<IntPtrT>::addDataProbe(uint64_t NameRef,
                                                    uint64_t CFGHash,
                                                    IntPtrT CounterOffset,
                                                    IntPtrT FunctionPtr,
                                                    uint32_t NumCounters) {
  // Check if a probe was already added for this counter offset.
  if (!CounterOffsets.insert(CounterOffset).second)
    return;
  Data.push_back({
      maybeSwap<uint64_t>(NameRef),
      maybeSwap<uint64_t>(CFGHash),
      // In this mode, CounterPtr actually stores the section relative address
      // of the counter.
      maybeSwap<IntPtrT>(CounterOffset),
      // TODO: MC/DC is not yet supported.
      /*BitmapOffset=*/maybeSwap<IntPtrT>(0),
      maybeSwap<IntPtrT>(FunctionPtr),
      // TODO: Value profiling is not yet supported.
      /*ValuesPtr=*/maybeSwap<IntPtrT>(0),
      maybeSwap<uint32_t>(NumCounters),
      /*NumValueSites=*/{maybeSwap<uint16_t>(0), maybeSwap<uint16_t>(0)},
      // TODO: MC/DC is not yet supported.
      /*NumBitmapBytes=*/maybeSwap<uint32_t>(0),
  });
}

template <class IntPtrT>
std::optional<uint64_t>
DwarfInstrProfCorrelator<IntPtrT>::getLocation(const DWARFDie &Die) const {
  auto Locations = Die.getLocations(dwarf::DW_AT_location);
  if (!Locations) {
    consumeError(Locations.takeError());
    return {};
  }
  auto &DU = *Die.getDwarfUnit();
  auto AddressSize = DU.getAddressByteSize();
  for (auto &Location : *Locations) {
    DataExtractor Data(Location.Expr, DICtx->isLittleEndian(), AddressSize);
    DWARFExpression Expr(Data, AddressSize);
    for (auto &Op : Expr) {
      if (Op.getCode() == dwarf::DW_OP_addr) {
        return Op.getRawOperand(0);
      } else if (Op.getCode() == dwarf::DW_OP_addrx) {
        uint64_t Index = Op.getRawOperand(0);
        if (auto SA = DU.getAddrOffsetSectionItem(Index))
          return SA->Address;
      }
    }
  }
  return {};
}

template <class IntPtrT>
bool DwarfInstrProfCorrelator<IntPtrT>::isDIEOfProbe(const DWARFDie &Die) {
  const auto &ParentDie = Die.getParent();
  if (!Die.isValid() || !ParentDie.isValid() || Die.isNULL())
    return false;
  if (Die.getTag() != dwarf::DW_TAG_variable)
    return false;
  if (!ParentDie.isSubprogramDIE())
    return false;
  if (!Die.hasChildren())
    return false;
  if (const char *Name = Die.getName(DINameKind::ShortName))
    return StringRef(Name).starts_with(getInstrProfCountersVarPrefix());
  return false;
}

template <class IntPtrT>
void DwarfInstrProfCorrelator<IntPtrT>::correlateProfileDataImpl(
    int MaxWarnings, InstrProfCorrelator::CorrelationData *Data) {
  bool UnlimitedWarnings = (MaxWarnings == 0);
  // -N suppressed warnings means we can emit up to N (unsuppressed) warnings
  int NumSuppressedWarnings = -MaxWarnings;
  auto maybeAddProbe = [&](DWARFDie Die) {
    if (!isDIEOfProbe(Die))
      return;
    std::optional<const char *> FunctionName;
    std::optional<uint64_t> CFGHash;
    std::optional<uint64_t> CounterPtr = getLocation(Die);
    auto FnDie = Die.getParent();
    auto FunctionPtr = dwarf::toAddress(FnDie.find(dwarf::DW_AT_low_pc));
    std::optional<uint64_t> NumCounters;
    for (const DWARFDie &Child : Die.children()) {
      if (Child.getTag() != dwarf::DW_TAG_LLVM_annotation)
        continue;
      auto AnnotationFormName = Child.find(dwarf::DW_AT_name);
      auto AnnotationFormValue = Child.find(dwarf::DW_AT_const_value);
      if (!AnnotationFormName || !AnnotationFormValue)
        continue;
      auto AnnotationNameOrErr = AnnotationFormName->getAsCString();
      if (auto Err = AnnotationNameOrErr.takeError()) {
        consumeError(std::move(Err));
        continue;
      }
      StringRef AnnotationName = *AnnotationNameOrErr;
      if (AnnotationName == InstrProfCorrelator::FunctionNameAttributeName) {
        if (auto EC =
                AnnotationFormValue->getAsCString().moveInto(FunctionName))
          consumeError(std::move(EC));
      } else if (AnnotationName == InstrProfCorrelator::CFGHashAttributeName) {
        CFGHash = AnnotationFormValue->getAsUnsignedConstant();
      } else if (AnnotationName ==
                 InstrProfCorrelator::NumCountersAttributeName) {
        NumCounters = AnnotationFormValue->getAsUnsignedConstant();
      }
    }
    if (!FunctionName || !CFGHash || !CounterPtr || !NumCounters) {
      if (UnlimitedWarnings || ++NumSuppressedWarnings < 1) {
        WithColor::warning()
            << "Incomplete DIE for function " << FunctionName
            << ": CFGHash=" << CFGHash << "  CounterPtr=" << CounterPtr
            << "  NumCounters=" << NumCounters << "\n";
        LLVM_DEBUG(Die.dump(dbgs()));
      }
      return;
    }
    uint64_t CountersStart = this->Ctx->CountersSectionStart;
    uint64_t CountersEnd = this->Ctx->CountersSectionEnd;
    if (*CounterPtr < CountersStart || *CounterPtr >= CountersEnd) {
      if (UnlimitedWarnings || ++NumSuppressedWarnings < 1) {
        WithColor::warning()
            << format("CounterPtr out of range for function %s: Actual=0x%x "
                      "Expected=[0x%x, 0x%x)\n",
                      *FunctionName, *CounterPtr, CountersStart, CountersEnd);
        LLVM_DEBUG(Die.dump(dbgs()));
      }
      return;
    }
    if (!FunctionPtr && (UnlimitedWarnings || ++NumSuppressedWarnings < 1)) {
      WithColor::warning() << format("Could not find address of function %s\n",
                                     *FunctionName);
      LLVM_DEBUG(Die.dump(dbgs()));
    }
    // In debug info correlation mode, the CounterPtr is an absolute address of
    // the counter, but it's expected to be relative later when iterating Data.
    IntPtrT CounterOffset = *CounterPtr - CountersStart;
    if (Data) {
      InstrProfCorrelator::Probe P;
      P.FunctionName = *FunctionName;
      if (auto Name = FnDie.getName(DINameKind::LinkageName))
        P.LinkageName = Name;
      P.CFGHash = *CFGHash;
      P.CounterOffset = CounterOffset;
      P.NumCounters = *NumCounters;
      auto FilePath = FnDie.getDeclFile(
          DILineInfoSpecifier::FileLineInfoKind::RelativeFilePath);
      if (!FilePath.empty())
        P.FilePath = FilePath;
      if (auto LineNumber = FnDie.getDeclLine())
        P.LineNumber = LineNumber;
      Data->Probes.push_back(P);
    } else {
      this->addDataProbe(IndexedInstrProf::ComputeHash(*FunctionName), *CFGHash,
                         CounterOffset, FunctionPtr.value_or(0), *NumCounters);
      this->NamesVec.push_back(*FunctionName);
    }
  };
  for (auto &CU : DICtx->normal_units())
    for (const auto &Entry : CU->dies())
      maybeAddProbe(DWARFDie(CU.get(), &Entry));
  for (auto &CU : DICtx->dwo_units())
    for (const auto &Entry : CU->dies())
      maybeAddProbe(DWARFDie(CU.get(), &Entry));

  if (!UnlimitedWarnings && NumSuppressedWarnings > 0)
    WithColor::warning() << format("Suppressed %d additional warnings\n",
                                   NumSuppressedWarnings);
}

template <class IntPtrT>
Error DwarfInstrProfCorrelator<IntPtrT>::correlateProfileNameImpl() {
  if (this->NamesVec.empty()) {
    return make_error<InstrProfError>(
        instrprof_error::unable_to_correlate_profile,
        "could not find any profile name metadata in debug info");
  }
  auto Result =
      collectGlobalObjectNameStrings(this->NamesVec,
                                     /*doCompression=*/false, this->Names);
  return Result;
}

template <class IntPtrT>
void BinaryInstrProfCorrelator<IntPtrT>::correlateProfileDataImpl(
    int MaxWarnings, InstrProfCorrelator::CorrelationData *CorrelateData) {
  using RawProfData = RawInstrProf::ProfileData<IntPtrT>;
  bool UnlimitedWarnings = (MaxWarnings == 0);
  // -N suppressed warnings means we can emit up to N (unsuppressed) warnings
  int NumSuppressedWarnings = -MaxWarnings;

  const RawProfData *DataStart = (const RawProfData *)this->Ctx->DataStart;
  const RawProfData *DataEnd = (const RawProfData *)this->Ctx->DataEnd;
  // We need to use < here because the last data record may have no padding.
  for (const RawProfData *I = DataStart; I < DataEnd; ++I) {
    uint64_t CounterPtr = this->template maybeSwap<IntPtrT>(I->CounterPtr);
    uint64_t CountersStart = this->Ctx->CountersSectionStart;
    uint64_t CountersEnd = this->Ctx->CountersSectionEnd;
    if (CounterPtr < CountersStart || CounterPtr >= CountersEnd) {
      if (UnlimitedWarnings || ++NumSuppressedWarnings < 1) {
        WithColor::warning()
            << format("CounterPtr out of range for function: Actual=0x%x "
                      "Expected=[0x%x, 0x%x) at data offset=0x%x\n",
                      CounterPtr, CountersStart, CountersEnd,
                      (I - DataStart) * sizeof(RawProfData));
      }
    }
    // In binary correlation mode, the CounterPtr is an absolute address of the
    // counter, but it's expected to be relative later when iterating Data.
    IntPtrT CounterOffset = CounterPtr - CountersStart;
    this->addDataProbe(I->NameRef, I->FuncHash, CounterOffset,
                       I->FunctionPointer, I->NumCounters);
  }
}

template <class IntPtrT>
Error BinaryInstrProfCorrelator<IntPtrT>::correlateProfileNameImpl() {
  if (this->Ctx->NameSize == 0) {
    return make_error<InstrProfError>(
        instrprof_error::unable_to_correlate_profile,
        "could not find any profile data metadata in object file");
  }
  this->Names.append(this->Ctx->NameStart, this->Ctx->NameSize);
  return Error::success();
}
