//===- SampleProfWriter.cpp - Write LLVM sample profile data --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the class that writes LLVM sample profiles. It
// supports two file formats: text and binary. The textual representation
// is useful for debugging and testing purposes. The binary representation
// is more compact, resulting in smaller file sizes. However, they can
// both be used interchangeably.
//
// See lib/ProfileData/SampleProfReader.cpp for documentation on each of the
// supported formats.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/SampleProfWriter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#define DEBUG_TYPE "llvm-profdata"

using namespace llvm;
using namespace sampleprof;

namespace llvm {
namespace support {
namespace endian {
namespace {

// Adapter class to llvm::support::endian::Writer for pwrite().
struct SeekableWriter {
  raw_pwrite_stream &OS;
  endianness Endian;
  SeekableWriter(raw_pwrite_stream &OS, endianness Endian)
      : OS(OS), Endian(Endian) {}

  template <typename ValueType>
  void pwrite(ValueType Val, size_t Offset) {
    std::string StringBuf;
    raw_string_ostream SStream(StringBuf);
    Writer(SStream, Endian).write(Val);
    OS.pwrite(StringBuf.data(), StringBuf.size(), Offset);
  }
};

} // namespace
} // namespace endian
} // namespace support
} // namespace llvm

DefaultFunctionPruningStrategy::DefaultFunctionPruningStrategy(
    SampleProfileMap &ProfileMap, size_t OutputSizeLimit)
    : FunctionPruningStrategy(ProfileMap, OutputSizeLimit) {
  sortFuncProfiles(ProfileMap, SortedFunctions);
}

void DefaultFunctionPruningStrategy::Erase(size_t CurrentOutputSize) {
  double D = (double)OutputSizeLimit / CurrentOutputSize;
  size_t NewSize = (size_t)round(ProfileMap.size() * D * D);
  size_t NumToRemove = ProfileMap.size() - NewSize;
  if (NumToRemove < 1)
    NumToRemove = 1;

  assert(NumToRemove <= SortedFunctions.size());
  for (const NameFunctionSamples &E :
       llvm::drop_begin(SortedFunctions, SortedFunctions.size() - NumToRemove))
    ProfileMap.erase(E.first);
  SortedFunctions.resize(SortedFunctions.size() - NumToRemove);
}

std::error_code SampleProfileWriter::writeWithSizeLimitInternal(
    SampleProfileMap &ProfileMap, size_t OutputSizeLimit,
    FunctionPruningStrategy *Strategy) {
  if (OutputSizeLimit == 0)
    return write(ProfileMap);

  size_t OriginalFunctionCount = ProfileMap.size();

  std::unique_ptr<raw_ostream> OriginalOutputStream;
  OutputStream.swap(OriginalOutputStream);

  size_t IterationCount = 0;
  size_t TotalSize;

  SmallVector<char> StringBuffer;
  do {
    StringBuffer.clear();
    OutputStream.reset(new raw_svector_ostream(StringBuffer));
    if (std::error_code EC = write(ProfileMap))
      return EC;

    TotalSize = StringBuffer.size();
    // On Windows every "\n" is actually written as "\r\n" to disk but not to
    // memory buffer, this difference should be added when considering the total
    // output size.
#ifdef _WIN32
    if (Format == SPF_Text)
      TotalSize += LineCount;
#endif
    if (TotalSize <= OutputSizeLimit)
      break;

    Strategy->Erase(TotalSize);
    IterationCount++;
  } while (ProfileMap.size() != 0);

  if (ProfileMap.size() == 0)
    return sampleprof_error::too_large;

  OutputStream.swap(OriginalOutputStream);
  OutputStream->write(StringBuffer.data(), StringBuffer.size());
  LLVM_DEBUG(dbgs() << "Profile originally has " << OriginalFunctionCount
                    << " functions, reduced to " << ProfileMap.size() << " in "
                    << IterationCount << " iterations\n");
  // Silence warning on Release build.
  (void)OriginalFunctionCount;
  (void)IterationCount;
  return sampleprof_error::success;
}

std::error_code
SampleProfileWriter::writeFuncProfiles(const SampleProfileMap &ProfileMap) {
  std::vector<NameFunctionSamples> V;
  sortFuncProfiles(ProfileMap, V);
  for (const auto &I : V) {
    if (std::error_code EC = writeSample(*I.second))
      return EC;
  }
  return sampleprof_error::success;
}

std::error_code SampleProfileWriter::write(const SampleProfileMap &ProfileMap) {
  if (std::error_code EC = writeHeader(ProfileMap))
    return EC;

  if (std::error_code EC = writeFuncProfiles(ProfileMap))
    return EC;

  return sampleprof_error::success;
}

/// Return the current position and prepare to use it as the start
/// position of a section given the section type \p Type and its position
/// \p LayoutIdx in SectionHdrLayout.
uint64_t
SampleProfileWriterExtBinaryBase::markSectionStart(SecType Type,
                                                   uint32_t LayoutIdx) {
  uint64_t SectionStart = OutputStream->tell();
  assert(LayoutIdx < SectionHdrLayout.size() && "LayoutIdx out of range");
  const auto &Entry = SectionHdrLayout[LayoutIdx];
  assert(Entry.Type == Type && "Unexpected section type");
  // Use LocalBuf as a temporary output for writting data.
  if (hasSecFlag(Entry, SecCommonFlags::SecFlagCompress))
    LocalBufStream.swap(OutputStream);
  return SectionStart;
}

std::error_code SampleProfileWriterExtBinaryBase::compressAndOutput() {
  if (!llvm::compression::zlib::isAvailable())
    return sampleprof_error::zlib_unavailable;
  std::string &UncompressedStrings =
      static_cast<raw_string_ostream *>(LocalBufStream.get())->str();
  if (UncompressedStrings.size() == 0)
    return sampleprof_error::success;
  auto &OS = *OutputStream;
  SmallVector<uint8_t, 128> CompressedStrings;
  compression::zlib::compress(arrayRefFromStringRef(UncompressedStrings),
                              CompressedStrings,
                              compression::zlib::BestSizeCompression);
  encodeULEB128(UncompressedStrings.size(), OS);
  encodeULEB128(CompressedStrings.size(), OS);
  OS << toStringRef(CompressedStrings);
  UncompressedStrings.clear();
  return sampleprof_error::success;
}

/// Add a new section into section header table given the section type
/// \p Type, its position \p LayoutIdx in SectionHdrLayout and the
/// location \p SectionStart where the section should be written to.
std::error_code SampleProfileWriterExtBinaryBase::addNewSection(
    SecType Type, uint32_t LayoutIdx, uint64_t SectionStart) {
  assert(LayoutIdx < SectionHdrLayout.size() && "LayoutIdx out of range");
  const auto &Entry = SectionHdrLayout[LayoutIdx];
  assert(Entry.Type == Type && "Unexpected section type");
  if (hasSecFlag(Entry, SecCommonFlags::SecFlagCompress)) {
    LocalBufStream.swap(OutputStream);
    if (std::error_code EC = compressAndOutput())
      return EC;
  }
  SecHdrTable.push_back({Type, Entry.Flags, SectionStart - FileStart,
                         OutputStream->tell() - SectionStart, LayoutIdx});
  return sampleprof_error::success;
}

std::error_code
SampleProfileWriterExtBinaryBase::write(const SampleProfileMap &ProfileMap) {
  // When calling write on a different profile map, existing states should be
  // cleared.
  NameTable.clear();
  CSNameTable.clear();
  SecHdrTable.clear();

  if (std::error_code EC = writeHeader(ProfileMap))
    return EC;

  std::string LocalBuf;
  LocalBufStream = std::make_unique<raw_string_ostream>(LocalBuf);
  if (std::error_code EC = writeSections(ProfileMap))
    return EC;

  if (std::error_code EC = writeSecHdrTable())
    return EC;

  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeContextIdx(
    const SampleContext &Context) {
  if (Context.hasContext())
    return writeCSNameIdx(Context);
  else
    return SampleProfileWriterBinary::writeNameIdx(Context.getFunction());
}

std::error_code
SampleProfileWriterExtBinaryBase::writeCSNameIdx(const SampleContext &Context) {
  const auto &Ret = CSNameTable.find(Context);
  if (Ret == CSNameTable.end())
    return sampleprof_error::truncated_name_table;
  encodeULEB128(Ret->second, *OutputStream);
  return sampleprof_error::success;
}

std::error_code
SampleProfileWriterExtBinaryBase::writeSample(const FunctionSamples &S) {
  uint64_t Offset = OutputStream->tell();
  auto &Context = S.getContext();
  FuncOffsetTable[Context] = Offset - SecLBRProfileStart;
  encodeULEB128(S.getHeadSamples(), *OutputStream);
  return writeBody(S);
}

std::error_code SampleProfileWriterExtBinaryBase::writeFuncOffsetTable() {
  auto &OS = *OutputStream;

  // Write out the table size.
  encodeULEB128(FuncOffsetTable.size(), OS);

  // Write out FuncOffsetTable.
  auto WriteItem = [&](const SampleContext &Context, uint64_t Offset) {
    if (std::error_code EC = writeContextIdx(Context))
      return EC;
    encodeULEB128(Offset, OS);
    return (std::error_code)sampleprof_error::success;
  };

  if (FunctionSamples::ProfileIsCS) {
    // Sort the contexts before writing them out. This is to help fast load all
    // context profiles for a function as well as their callee contexts which
    // can help profile-guided importing for ThinLTO.
    std::map<SampleContext, uint64_t> OrderedFuncOffsetTable(
        FuncOffsetTable.begin(), FuncOffsetTable.end());
    for (const auto &Entry : OrderedFuncOffsetTable) {
      if (std::error_code EC = WriteItem(Entry.first, Entry.second))
        return EC;
    }
    addSectionFlag(SecFuncOffsetTable, SecFuncOffsetFlags::SecFlagOrdered);
  } else {
    for (const auto &Entry : FuncOffsetTable) {
      if (std::error_code EC = WriteItem(Entry.first, Entry.second))
        return EC;
    }
  }

  FuncOffsetTable.clear();
  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeFuncMetadata(
    const FunctionSamples &FunctionProfile) {
  auto &OS = *OutputStream;
  if (std::error_code EC = writeContextIdx(FunctionProfile.getContext()))
    return EC;

  if (FunctionSamples::ProfileIsProbeBased)
    encodeULEB128(FunctionProfile.getFunctionHash(), OS);
  if (FunctionSamples::ProfileIsCS || FunctionSamples::ProfileIsPreInlined) {
    encodeULEB128(FunctionProfile.getContext().getAllAttributes(), OS);
  }

  if (!FunctionSamples::ProfileIsCS) {
    // Recursively emit attributes for all callee samples.
    uint64_t NumCallsites = 0;
    for (const auto &J : FunctionProfile.getCallsiteSamples())
      NumCallsites += J.second.size();
    encodeULEB128(NumCallsites, OS);
    for (const auto &J : FunctionProfile.getCallsiteSamples()) {
      for (const auto &FS : J.second) {
        LineLocation Loc = J.first;
        encodeULEB128(Loc.LineOffset, OS);
        encodeULEB128(Loc.Discriminator, OS);
        if (std::error_code EC = writeFuncMetadata(FS.second))
          return EC;
      }
    }
  }

  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeFuncMetadata(
    const SampleProfileMap &Profiles) {
  if (!FunctionSamples::ProfileIsProbeBased && !FunctionSamples::ProfileIsCS &&
      !FunctionSamples::ProfileIsPreInlined)
    return sampleprof_error::success;
  for (const auto &Entry : Profiles) {
    if (std::error_code EC = writeFuncMetadata(Entry.second))
      return EC;
  }
  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeNameTable() {
  if (!UseMD5)
    return SampleProfileWriterBinary::writeNameTable();

  auto &OS = *OutputStream;
  std::set<FunctionId> V;
  stablizeNameTable(NameTable, V);

  // Write out the MD5 name table. We wrote unencoded MD5 so reader can
  // retrieve the name using the name index without having to read the
  // whole name table.
  encodeULEB128(NameTable.size(), OS);
  support::endian::Writer Writer(OS, llvm::endianness::little);
  for (auto N : V)
    Writer.write(N.getHashCode());
  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeNameTableSection(
    const SampleProfileMap &ProfileMap) {
  for (const auto &I : ProfileMap) {
    addContext(I.second.getContext());
    addNames(I.second);
  }

  // If NameTable contains ".__uniq." suffix, set SecFlagUniqSuffix flag
  // so compiler won't strip the suffix during profile matching after
  // seeing the flag in the profile.
  // Original names are unavailable if using MD5, so this option has no use.
  if (!UseMD5) {
    for (const auto &I : NameTable) {
      if (I.first.stringRef().contains(FunctionSamples::UniqSuffix)) {
        addSectionFlag(SecNameTable, SecNameTableFlags::SecFlagUniqSuffix);
        break;
      }
    }
  }

  if (auto EC = writeNameTable())
    return EC;
  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeCSNameTableSection() {
  // Sort the names to make CSNameTable deterministic.
  std::set<SampleContext> OrderedContexts;
  for (const auto &I : CSNameTable)
    OrderedContexts.insert(I.first);
  assert(OrderedContexts.size() == CSNameTable.size() &&
         "Unmatched ordered and unordered contexts");
  uint64_t I = 0;
  for (auto &Context : OrderedContexts)
    CSNameTable[Context] = I++;

  auto &OS = *OutputStream;
  encodeULEB128(OrderedContexts.size(), OS);
  support::endian::Writer Writer(OS, llvm::endianness::little);
  for (auto Context : OrderedContexts) {
    auto Frames = Context.getContextFrames();
    encodeULEB128(Frames.size(), OS);
    for (auto &Callsite : Frames) {
      if (std::error_code EC = writeNameIdx(Callsite.Func))
        return EC;
      encodeULEB128(Callsite.Location.LineOffset, OS);
      encodeULEB128(Callsite.Location.Discriminator, OS);
    }
  }

  return sampleprof_error::success;
}

std::error_code
SampleProfileWriterExtBinaryBase::writeProfileSymbolListSection() {
  if (ProfSymList && ProfSymList->size() > 0)
    if (std::error_code EC = ProfSymList->write(*OutputStream))
      return EC;

  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeOneSection(
    SecType Type, uint32_t LayoutIdx, const SampleProfileMap &ProfileMap) {
  // The setting of SecFlagCompress should happen before markSectionStart.
  if (Type == SecProfileSymbolList && ProfSymList && ProfSymList->toCompress())
    setToCompressSection(SecProfileSymbolList);
  if (Type == SecFuncMetadata && FunctionSamples::ProfileIsProbeBased)
    addSectionFlag(SecFuncMetadata, SecFuncMetadataFlags::SecFlagIsProbeBased);
  if (Type == SecFuncMetadata &&
      (FunctionSamples::ProfileIsCS || FunctionSamples::ProfileIsPreInlined))
    addSectionFlag(SecFuncMetadata, SecFuncMetadataFlags::SecFlagHasAttribute);
  if (Type == SecProfSummary && FunctionSamples::ProfileIsCS)
    addSectionFlag(SecProfSummary, SecProfSummaryFlags::SecFlagFullContext);
  if (Type == SecProfSummary && FunctionSamples::ProfileIsPreInlined)
    addSectionFlag(SecProfSummary, SecProfSummaryFlags::SecFlagIsPreInlined);
  if (Type == SecProfSummary && FunctionSamples::ProfileIsFS)
    addSectionFlag(SecProfSummary, SecProfSummaryFlags::SecFlagFSDiscriminator);

  uint64_t SectionStart = markSectionStart(Type, LayoutIdx);
  switch (Type) {
  case SecProfSummary:
    computeSummary(ProfileMap);
    if (auto EC = writeSummary())
      return EC;
    break;
  case SecNameTable:
    if (auto EC = writeNameTableSection(ProfileMap))
      return EC;
    break;
  case SecCSNameTable:
    if (auto EC = writeCSNameTableSection())
      return EC;
    break;
  case SecLBRProfile:
    SecLBRProfileStart = OutputStream->tell();
    if (std::error_code EC = writeFuncProfiles(ProfileMap))
      return EC;
    break;
  case SecFuncOffsetTable:
    if (auto EC = writeFuncOffsetTable())
      return EC;
    break;
  case SecFuncMetadata:
    if (std::error_code EC = writeFuncMetadata(ProfileMap))
      return EC;
    break;
  case SecProfileSymbolList:
    if (auto EC = writeProfileSymbolListSection())
      return EC;
    break;
  default:
    if (auto EC = writeCustomSection(Type))
      return EC;
    break;
  }
  if (std::error_code EC = addNewSection(Type, LayoutIdx, SectionStart))
    return EC;
  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinary::writeDefaultLayout(
    const SampleProfileMap &ProfileMap) {
  // The const indices passed to writeOneSection below are specifying the
  // positions of the sections in SectionHdrLayout. Look at
  // initSectionHdrLayout to find out where each section is located in
  // SectionHdrLayout.
  if (auto EC = writeOneSection(SecProfSummary, 0, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecNameTable, 1, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecCSNameTable, 2, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecLBRProfile, 4, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecProfileSymbolList, 5, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecFuncOffsetTable, 3, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecFuncMetadata, 6, ProfileMap))
    return EC;
  return sampleprof_error::success;
}

static void splitProfileMapToTwo(const SampleProfileMap &ProfileMap,
                                 SampleProfileMap &ContextProfileMap,
                                 SampleProfileMap &NoContextProfileMap) {
  for (const auto &I : ProfileMap) {
    if (I.second.getCallsiteSamples().size())
      ContextProfileMap.insert({I.first, I.second});
    else
      NoContextProfileMap.insert({I.first, I.second});
  }
}

std::error_code SampleProfileWriterExtBinary::writeCtxSplitLayout(
    const SampleProfileMap &ProfileMap) {
  SampleProfileMap ContextProfileMap, NoContextProfileMap;
  splitProfileMapToTwo(ProfileMap, ContextProfileMap, NoContextProfileMap);

  if (auto EC = writeOneSection(SecProfSummary, 0, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecNameTable, 1, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecLBRProfile, 3, ContextProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecFuncOffsetTable, 2, ContextProfileMap))
    return EC;
  // Mark the section to have no context. Note section flag needs to be set
  // before writing the section.
  addSectionFlag(5, SecCommonFlags::SecFlagFlat);
  if (auto EC = writeOneSection(SecLBRProfile, 5, NoContextProfileMap))
    return EC;
  // Mark the section to have no context. Note section flag needs to be set
  // before writing the section.
  addSectionFlag(4, SecCommonFlags::SecFlagFlat);
  if (auto EC = writeOneSection(SecFuncOffsetTable, 4, NoContextProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecProfileSymbolList, 6, ProfileMap))
    return EC;
  if (auto EC = writeOneSection(SecFuncMetadata, 7, ProfileMap))
    return EC;

  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinary::writeSections(
    const SampleProfileMap &ProfileMap) {
  std::error_code EC;
  if (SecLayout == DefaultLayout)
    EC = writeDefaultLayout(ProfileMap);
  else if (SecLayout == CtxSplitLayout)
    EC = writeCtxSplitLayout(ProfileMap);
  else
    llvm_unreachable("Unsupported layout");
  return EC;
}

/// Write samples to a text file.
///
/// Note: it may be tempting to implement this in terms of
/// FunctionSamples::print().  Please don't.  The dump functionality is intended
/// for debugging and has no specified form.
///
/// The format used here is more structured and deliberate because
/// it needs to be parsed by the SampleProfileReaderText class.
std::error_code SampleProfileWriterText::writeSample(const FunctionSamples &S) {
  auto &OS = *OutputStream;
  if (FunctionSamples::ProfileIsCS)
    OS << "[" << S.getContext().toString() << "]:" << S.getTotalSamples();
  else
    OS << S.getFunction() << ":" << S.getTotalSamples();

  if (Indent == 0)
    OS << ":" << S.getHeadSamples();
  OS << "\n";
  LineCount++;

  SampleSorter<LineLocation, SampleRecord> SortedSamples(S.getBodySamples());
  for (const auto &I : SortedSamples.get()) {
    LineLocation Loc = I->first;
    const SampleRecord &Sample = I->second;
    OS.indent(Indent + 1);
    if (Loc.Discriminator == 0)
      OS << Loc.LineOffset << ": ";
    else
      OS << Loc.LineOffset << "." << Loc.Discriminator << ": ";

    OS << Sample.getSamples();

    for (const auto &J : Sample.getSortedCallTargets())
      OS << " " << J.first << ":" << J.second;
    OS << "\n";
    LineCount++;
  }

  SampleSorter<LineLocation, FunctionSamplesMap> SortedCallsiteSamples(
      S.getCallsiteSamples());
  Indent += 1;
  for (const auto &I : SortedCallsiteSamples.get())
    for (const auto &FS : I->second) {
      LineLocation Loc = I->first;
      const FunctionSamples &CalleeSamples = FS.second;
      OS.indent(Indent);
      if (Loc.Discriminator == 0)
        OS << Loc.LineOffset << ": ";
      else
        OS << Loc.LineOffset << "." << Loc.Discriminator << ": ";
      if (std::error_code EC = writeSample(CalleeSamples))
        return EC;
    }
  Indent -= 1;

  if (FunctionSamples::ProfileIsProbeBased) {
    OS.indent(Indent + 1);
    OS << "!CFGChecksum: " << S.getFunctionHash() << "\n";
    LineCount++;
  }

  if (S.getContext().getAllAttributes()) {
    OS.indent(Indent + 1);
    OS << "!Attributes: " << S.getContext().getAllAttributes() << "\n";
    LineCount++;
  }

  return sampleprof_error::success;
}

std::error_code
SampleProfileWriterBinary::writeContextIdx(const SampleContext &Context) {
  assert(!Context.hasContext() && "cs profile is not supported");
  return writeNameIdx(Context.getFunction());
}

std::error_code SampleProfileWriterBinary::writeNameIdx(FunctionId FName) {
  auto &NTable = getNameTable();
  const auto &Ret = NTable.find(FName);
  if (Ret == NTable.end())
    return sampleprof_error::truncated_name_table;
  encodeULEB128(Ret->second, *OutputStream);
  return sampleprof_error::success;
}

void SampleProfileWriterBinary::addName(FunctionId FName) {
  auto &NTable = getNameTable();
  NTable.insert(std::make_pair(FName, 0));
}

void SampleProfileWriterBinary::addContext(const SampleContext &Context) {
  addName(Context.getFunction());
}

void SampleProfileWriterBinary::addNames(const FunctionSamples &S) {
  // Add all the names in indirect call targets.
  for (const auto &I : S.getBodySamples()) {
    const SampleRecord &Sample = I.second;
    for (const auto &J : Sample.getCallTargets())
      addName(J.first);
  }

  // Recursively add all the names for inlined callsites.
  for (const auto &J : S.getCallsiteSamples())
    for (const auto &FS : J.second) {
      const FunctionSamples &CalleeSamples = FS.second;
      addName(CalleeSamples.getFunction());
      addNames(CalleeSamples);
    }
}

void SampleProfileWriterExtBinaryBase::addContext(
    const SampleContext &Context) {
  if (Context.hasContext()) {
    for (auto &Callsite : Context.getContextFrames())
      SampleProfileWriterBinary::addName(Callsite.Func);
    CSNameTable.insert(std::make_pair(Context, 0));
  } else {
    SampleProfileWriterBinary::addName(Context.getFunction());
  }
}

void SampleProfileWriterBinary::stablizeNameTable(
    MapVector<FunctionId, uint32_t> &NameTable, std::set<FunctionId> &V) {
  // Sort the names to make NameTable deterministic.
  for (const auto &I : NameTable)
    V.insert(I.first);
  int i = 0;
  for (const FunctionId &N : V)
    NameTable[N] = i++;
}

std::error_code SampleProfileWriterBinary::writeNameTable() {
  auto &OS = *OutputStream;
  std::set<FunctionId> V;
  stablizeNameTable(NameTable, V);

  // Write out the name table.
  encodeULEB128(NameTable.size(), OS);
  for (auto N : V) {
    OS << N;
    encodeULEB128(0, OS);
  }
  return sampleprof_error::success;
}

std::error_code
SampleProfileWriterBinary::writeMagicIdent(SampleProfileFormat Format) {
  auto &OS = *OutputStream;
  // Write file magic identifier.
  encodeULEB128(SPMagic(Format), OS);
  encodeULEB128(SPVersion(), OS);
  return sampleprof_error::success;
}

std::error_code
SampleProfileWriterBinary::writeHeader(const SampleProfileMap &ProfileMap) {
  // When calling write on a different profile map, existing names should be
  // cleared.
  NameTable.clear();

  writeMagicIdent(Format);

  computeSummary(ProfileMap);
  if (auto EC = writeSummary())
    return EC;

  // Generate the name table for all the functions referenced in the profile.
  for (const auto &I : ProfileMap) {
    addContext(I.second.getContext());
    addNames(I.second);
  }

  writeNameTable();
  return sampleprof_error::success;
}

void SampleProfileWriterExtBinaryBase::setToCompressAllSections() {
  for (auto &Entry : SectionHdrLayout)
    addSecFlag(Entry, SecCommonFlags::SecFlagCompress);
}

void SampleProfileWriterExtBinaryBase::setToCompressSection(SecType Type) {
  addSectionFlag(Type, SecCommonFlags::SecFlagCompress);
}

void SampleProfileWriterExtBinaryBase::allocSecHdrTable() {
  support::endian::Writer Writer(*OutputStream, llvm::endianness::little);

  Writer.write(static_cast<uint64_t>(SectionHdrLayout.size()));
  SecHdrTableOffset = OutputStream->tell();
  for (uint32_t i = 0; i < SectionHdrLayout.size(); i++) {
    Writer.write(static_cast<uint64_t>(-1));
    Writer.write(static_cast<uint64_t>(-1));
    Writer.write(static_cast<uint64_t>(-1));
    Writer.write(static_cast<uint64_t>(-1));
  }
}

std::error_code SampleProfileWriterExtBinaryBase::writeSecHdrTable() {
  assert(SecHdrTable.size() == SectionHdrLayout.size() &&
         "SecHdrTable entries doesn't match SectionHdrLayout");
  SmallVector<uint32_t, 16> IndexMap(SecHdrTable.size(), -1);
  for (uint32_t TableIdx = 0; TableIdx < SecHdrTable.size(); TableIdx++) {
    IndexMap[SecHdrTable[TableIdx].LayoutIndex] = TableIdx;
  }

  // Write the section header table in the order specified in
  // SectionHdrLayout. SectionHdrLayout specifies the sections
  // order in which profile reader expect to read, so the section
  // header table should be written in the order in SectionHdrLayout.
  // Note that the section order in SecHdrTable may be different
  // from the order in SectionHdrLayout, for example, SecFuncOffsetTable
  // needs to be computed after SecLBRProfile (the order in SecHdrTable),
  // but it needs to be read before SecLBRProfile (the order in
  // SectionHdrLayout). So we use IndexMap above to switch the order.
  support::endian::SeekableWriter Writer(
      static_cast<raw_pwrite_stream &>(*OutputStream),
      llvm::endianness::little);
  for (uint32_t LayoutIdx = 0; LayoutIdx < SectionHdrLayout.size();
       LayoutIdx++) {
    assert(IndexMap[LayoutIdx] < SecHdrTable.size() &&
           "Incorrect LayoutIdx in SecHdrTable");
    auto Entry = SecHdrTable[IndexMap[LayoutIdx]];
    Writer.pwrite(static_cast<uint64_t>(Entry.Type),
                  SecHdrTableOffset + 4 * LayoutIdx * sizeof(uint64_t));
    Writer.pwrite(static_cast<uint64_t>(Entry.Flags),
                  SecHdrTableOffset + (4 * LayoutIdx + 1) * sizeof(uint64_t));
    Writer.pwrite(static_cast<uint64_t>(Entry.Offset),
                  SecHdrTableOffset + (4 * LayoutIdx + 2) * sizeof(uint64_t));
    Writer.pwrite(static_cast<uint64_t>(Entry.Size),
                  SecHdrTableOffset + (4 * LayoutIdx + 3) * sizeof(uint64_t));
  }

  return sampleprof_error::success;
}

std::error_code SampleProfileWriterExtBinaryBase::writeHeader(
    const SampleProfileMap &ProfileMap) {
  auto &OS = *OutputStream;
  FileStart = OS.tell();
  writeMagicIdent(Format);

  allocSecHdrTable();
  return sampleprof_error::success;
}

std::error_code SampleProfileWriterBinary::writeSummary() {
  auto &OS = *OutputStream;
  encodeULEB128(Summary->getTotalCount(), OS);
  encodeULEB128(Summary->getMaxCount(), OS);
  encodeULEB128(Summary->getMaxFunctionCount(), OS);
  encodeULEB128(Summary->getNumCounts(), OS);
  encodeULEB128(Summary->getNumFunctions(), OS);
  ArrayRef<ProfileSummaryEntry> Entries = Summary->getDetailedSummary();
  encodeULEB128(Entries.size(), OS);
  for (auto Entry : Entries) {
    encodeULEB128(Entry.Cutoff, OS);
    encodeULEB128(Entry.MinCount, OS);
    encodeULEB128(Entry.NumCounts, OS);
  }
  return sampleprof_error::success;
}
std::error_code SampleProfileWriterBinary::writeBody(const FunctionSamples &S) {
  auto &OS = *OutputStream;
  if (std::error_code EC = writeContextIdx(S.getContext()))
    return EC;

  encodeULEB128(S.getTotalSamples(), OS);

  // Emit all the body samples.
  encodeULEB128(S.getBodySamples().size(), OS);
  for (const auto &I : S.getBodySamples()) {
    LineLocation Loc = I.first;
    const SampleRecord &Sample = I.second;
    encodeULEB128(Loc.LineOffset, OS);
    encodeULEB128(Loc.Discriminator, OS);
    encodeULEB128(Sample.getSamples(), OS);
    encodeULEB128(Sample.getCallTargets().size(), OS);
    for (const auto &J : Sample.getSortedCallTargets()) {
      FunctionId Callee = J.first;
      uint64_t CalleeSamples = J.second;
      if (std::error_code EC = writeNameIdx(Callee))
        return EC;
      encodeULEB128(CalleeSamples, OS);
    }
  }

  // Recursively emit all the callsite samples.
  uint64_t NumCallsites = 0;
  for (const auto &J : S.getCallsiteSamples())
    NumCallsites += J.second.size();
  encodeULEB128(NumCallsites, OS);
  for (const auto &J : S.getCallsiteSamples())
    for (const auto &FS : J.second) {
      LineLocation Loc = J.first;
      const FunctionSamples &CalleeSamples = FS.second;
      encodeULEB128(Loc.LineOffset, OS);
      encodeULEB128(Loc.Discriminator, OS);
      if (std::error_code EC = writeBody(CalleeSamples))
        return EC;
    }

  return sampleprof_error::success;
}

/// Write samples of a top-level function to a binary file.
///
/// \returns true if the samples were written successfully, false otherwise.
std::error_code
SampleProfileWriterBinary::writeSample(const FunctionSamples &S) {
  encodeULEB128(S.getHeadSamples(), *OutputStream);
  return writeBody(S);
}

/// Create a sample profile file writer based on the specified format.
///
/// \param Filename The file to create.
///
/// \param Format Encoding format for the profile file.
///
/// \returns an error code indicating the status of the created writer.
ErrorOr<std::unique_ptr<SampleProfileWriter>>
SampleProfileWriter::create(StringRef Filename, SampleProfileFormat Format) {
  std::error_code EC;
  std::unique_ptr<raw_ostream> OS;
  if (Format == SPF_Binary || Format == SPF_Ext_Binary)
    OS.reset(new raw_fd_ostream(Filename, EC, sys::fs::OF_None));
  else
    OS.reset(new raw_fd_ostream(Filename, EC, sys::fs::OF_TextWithCRLF));
  if (EC)
    return EC;

  return create(OS, Format);
}

/// Create a sample profile stream writer based on the specified format.
///
/// \param OS The output stream to store the profile data to.
///
/// \param Format Encoding format for the profile file.
///
/// \returns an error code indicating the status of the created writer.
ErrorOr<std::unique_ptr<SampleProfileWriter>>
SampleProfileWriter::create(std::unique_ptr<raw_ostream> &OS,
                            SampleProfileFormat Format) {
  std::error_code EC;
  std::unique_ptr<SampleProfileWriter> Writer;

  // Currently only Text and Extended Binary format are supported for CSSPGO.
  if ((FunctionSamples::ProfileIsCS || FunctionSamples::ProfileIsProbeBased) &&
      Format == SPF_Binary)
    return sampleprof_error::unsupported_writing_format;

  if (Format == SPF_Binary)
    Writer.reset(new SampleProfileWriterRawBinary(OS));
  else if (Format == SPF_Ext_Binary)
    Writer.reset(new SampleProfileWriterExtBinary(OS));
  else if (Format == SPF_Text)
    Writer.reset(new SampleProfileWriterText(OS));
  else if (Format == SPF_GCC)
    EC = sampleprof_error::unsupported_writing_format;
  else
    EC = sampleprof_error::unrecognized_format;

  if (EC)
    return EC;

  Writer->Format = Format;
  return std::move(Writer);
}

void SampleProfileWriter::computeSummary(const SampleProfileMap &ProfileMap) {
  SampleProfileSummaryBuilder Builder(ProfileSummaryBuilder::DefaultCutoffs);
  Summary = Builder.computeSummaryForProfiles(ProfileMap);
}
