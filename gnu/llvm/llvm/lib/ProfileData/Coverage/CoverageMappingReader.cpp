//===- CoverageMappingReader.cpp - Code coverage mapping reader -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading coverage mapping data for
// instrumentation based coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/Coverage/CoverageMappingReader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <vector>

using namespace llvm;
using namespace coverage;
using namespace object;

#define DEBUG_TYPE "coverage-mapping"

STATISTIC(CovMapNumRecords, "The # of coverage function records");
STATISTIC(CovMapNumUsedRecords, "The # of used coverage function records");

void CoverageMappingIterator::increment() {
  if (ReadErr != coveragemap_error::success)
    return;

  // Check if all the records were read or if an error occurred while reading
  // the next record.
  if (auto E = Reader->readNextRecord(Record))
    handleAllErrors(std::move(E), [&](const CoverageMapError &CME) {
      if (CME.get() == coveragemap_error::eof)
        *this = CoverageMappingIterator();
      else
        ReadErr = CME.get();
    });
}

Error RawCoverageReader::readULEB128(uint64_t &Result) {
  if (Data.empty())
    return make_error<CoverageMapError>(coveragemap_error::truncated);
  unsigned N = 0;
  Result = decodeULEB128(Data.bytes_begin(), &N);
  if (N > Data.size())
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "the size of ULEB128 is too big");
  Data = Data.substr(N);
  return Error::success();
}

Error RawCoverageReader::readIntMax(uint64_t &Result, uint64_t MaxPlus1) {
  if (auto Err = readULEB128(Result))
    return Err;
  if (Result >= MaxPlus1)
    return make_error<CoverageMapError>(
        coveragemap_error::malformed,
        "the value of ULEB128 is greater than or equal to MaxPlus1");
  return Error::success();
}

Error RawCoverageReader::readSize(uint64_t &Result) {
  if (auto Err = readULEB128(Result))
    return Err;
  if (Result > Data.size())
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "the value of ULEB128 is too big");
  return Error::success();
}

Error RawCoverageReader::readString(StringRef &Result) {
  uint64_t Length;
  if (auto Err = readSize(Length))
    return Err;
  Result = Data.substr(0, Length);
  Data = Data.substr(Length);
  return Error::success();
}

Error RawCoverageFilenamesReader::read(CovMapVersion Version) {
  uint64_t NumFilenames;
  if (auto Err = readSize(NumFilenames))
    return Err;
  if (!NumFilenames)
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "number of filenames is zero");

  if (Version < CovMapVersion::Version4)
    return readUncompressed(Version, NumFilenames);

  // The uncompressed length may exceed the size of the encoded filenames.
  // Skip size validation.
  uint64_t UncompressedLen;
  if (auto Err = readULEB128(UncompressedLen))
    return Err;

  uint64_t CompressedLen;
  if (auto Err = readSize(CompressedLen))
    return Err;

  if (CompressedLen > 0) {
    if (!compression::zlib::isAvailable())
      return make_error<CoverageMapError>(
          coveragemap_error::decompression_failed);

    // Allocate memory for the decompressed filenames.
    SmallVector<uint8_t, 0> StorageBuf;

    // Read compressed filenames.
    StringRef CompressedFilenames = Data.substr(0, CompressedLen);
    Data = Data.substr(CompressedLen);
    auto Err = compression::zlib::decompress(
        arrayRefFromStringRef(CompressedFilenames), StorageBuf,
        UncompressedLen);
    if (Err) {
      consumeError(std::move(Err));
      return make_error<CoverageMapError>(
          coveragemap_error::decompression_failed);
    }

    RawCoverageFilenamesReader Delegate(toStringRef(StorageBuf), Filenames,
                                        CompilationDir);
    return Delegate.readUncompressed(Version, NumFilenames);
  }

  return readUncompressed(Version, NumFilenames);
}

Error RawCoverageFilenamesReader::readUncompressed(CovMapVersion Version,
                                                   uint64_t NumFilenames) {
  // Read uncompressed filenames.
  if (Version < CovMapVersion::Version6) {
    for (size_t I = 0; I < NumFilenames; ++I) {
      StringRef Filename;
      if (auto Err = readString(Filename))
        return Err;
      Filenames.push_back(Filename.str());
    }
  } else {
    StringRef CWD;
    if (auto Err = readString(CWD))
      return Err;
    Filenames.push_back(CWD.str());

    for (size_t I = 1; I < NumFilenames; ++I) {
      StringRef Filename;
      if (auto Err = readString(Filename))
        return Err;
      if (sys::path::is_absolute(Filename)) {
        Filenames.push_back(Filename.str());
      } else {
        SmallString<256> P;
        if (!CompilationDir.empty())
          P.assign(CompilationDir);
        else
          P.assign(CWD);
        llvm::sys::path::append(P, Filename);
        sys::path::remove_dots(P, /*remove_dot_dot=*/true);
        Filenames.push_back(static_cast<std::string>(P.str()));
      }
    }
  }
  return Error::success();
}

Error RawCoverageMappingReader::decodeCounter(unsigned Value, Counter &C) {
  auto Tag = Value & Counter::EncodingTagMask;
  switch (Tag) {
  case Counter::Zero:
    C = Counter::getZero();
    return Error::success();
  case Counter::CounterValueReference:
    C = Counter::getCounter(Value >> Counter::EncodingTagBits);
    return Error::success();
  default:
    break;
  }
  Tag -= Counter::Expression;
  switch (Tag) {
  case CounterExpression::Subtract:
  case CounterExpression::Add: {
    auto ID = Value >> Counter::EncodingTagBits;
    if (ID >= Expressions.size())
      return make_error<CoverageMapError>(coveragemap_error::malformed,
                                          "counter expression is invalid");
    Expressions[ID].Kind = CounterExpression::ExprKind(Tag);
    C = Counter::getExpression(ID);
    break;
  }
  default:
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "counter expression kind is invalid");
  }
  return Error::success();
}

Error RawCoverageMappingReader::readCounter(Counter &C) {
  uint64_t EncodedCounter;
  if (auto Err =
          readIntMax(EncodedCounter, std::numeric_limits<unsigned>::max()))
    return Err;
  if (auto Err = decodeCounter(EncodedCounter, C))
    return Err;
  return Error::success();
}

static const unsigned EncodingExpansionRegionBit = 1
                                                   << Counter::EncodingTagBits;

/// Read the sub-array of regions for the given inferred file id.
/// \param NumFileIDs the number of file ids that are defined for this
/// function.
Error RawCoverageMappingReader::readMappingRegionsSubArray(
    std::vector<CounterMappingRegion> &MappingRegions, unsigned InferredFileID,
    size_t NumFileIDs) {
  uint64_t NumRegions;
  if (auto Err = readSize(NumRegions))
    return Err;
  unsigned LineStart = 0;
  for (size_t I = 0; I < NumRegions; ++I) {
    Counter C, C2;
    uint64_t BIDX, NC;
    // They are stored as internal values plus 1 (min is -1)
    uint64_t ID1, TID1, FID1;
    mcdc::Parameters Params;
    CounterMappingRegion::RegionKind Kind = CounterMappingRegion::CodeRegion;

    // Read the combined counter + region kind.
    uint64_t EncodedCounterAndRegion;
    if (auto Err = readIntMax(EncodedCounterAndRegion,
                              std::numeric_limits<unsigned>::max()))
      return Err;
    unsigned Tag = EncodedCounterAndRegion & Counter::EncodingTagMask;
    uint64_t ExpandedFileID = 0;

    // If Tag does not represent a ZeroCounter, then it is understood to refer
    // to a counter or counter expression with region kind assumed to be
    // "CodeRegion". In that case, EncodedCounterAndRegion actually encodes the
    // referenced counter or counter expression (and nothing else).
    //
    // If Tag represents a ZeroCounter and EncodingExpansionRegionBit is set,
    // then EncodedCounterAndRegion is interpreted to represent an
    // ExpansionRegion. In all other cases, EncodedCounterAndRegion is
    // interpreted to refer to a specific region kind, after which additional
    // fields may be read (e.g. BranchRegions have two encoded counters that
    // follow an encoded region kind value).
    if (Tag != Counter::Zero) {
      if (auto Err = decodeCounter(EncodedCounterAndRegion, C))
        return Err;
    } else {
      // Is it an expansion region?
      if (EncodedCounterAndRegion & EncodingExpansionRegionBit) {
        Kind = CounterMappingRegion::ExpansionRegion;
        ExpandedFileID = EncodedCounterAndRegion >>
                         Counter::EncodingCounterTagAndExpansionRegionTagBits;
        if (ExpandedFileID >= NumFileIDs)
          return make_error<CoverageMapError>(coveragemap_error::malformed,
                                              "ExpandedFileID is invalid");
      } else {
        switch (EncodedCounterAndRegion >>
                Counter::EncodingCounterTagAndExpansionRegionTagBits) {
        case CounterMappingRegion::CodeRegion:
          // Don't do anything when we have a code region with a zero counter.
          break;
        case CounterMappingRegion::SkippedRegion:
          Kind = CounterMappingRegion::SkippedRegion;
          break;
        case CounterMappingRegion::BranchRegion:
          // For a Branch Region, read two successive counters.
          Kind = CounterMappingRegion::BranchRegion;
          if (auto Err = readCounter(C))
            return Err;
          if (auto Err = readCounter(C2))
            return Err;
          break;
        case CounterMappingRegion::MCDCBranchRegion:
          // For a MCDC Branch Region, read two successive counters and 3 IDs.
          Kind = CounterMappingRegion::MCDCBranchRegion;
          if (auto Err = readCounter(C))
            return Err;
          if (auto Err = readCounter(C2))
            return Err;
          if (auto Err = readIntMax(ID1, std::numeric_limits<int16_t>::max()))
            return Err;
          if (auto Err = readIntMax(TID1, std::numeric_limits<int16_t>::max()))
            return Err;
          if (auto Err = readIntMax(FID1, std::numeric_limits<int16_t>::max()))
            return Err;
          if (ID1 == 0)
            return make_error<CoverageMapError>(
                coveragemap_error::malformed,
                "MCDCConditionID shouldn't be zero");
          Params = mcdc::BranchParameters{
              static_cast<int16_t>(static_cast<int16_t>(ID1) - 1),
              {static_cast<int16_t>(static_cast<int16_t>(FID1) - 1),
               static_cast<int16_t>(static_cast<int16_t>(TID1) - 1)}};
          break;
        case CounterMappingRegion::MCDCDecisionRegion:
          Kind = CounterMappingRegion::MCDCDecisionRegion;
          if (auto Err = readIntMax(BIDX, std::numeric_limits<unsigned>::max()))
            return Err;
          if (auto Err = readIntMax(NC, std::numeric_limits<int16_t>::max()))
            return Err;
          Params = mcdc::DecisionParameters{static_cast<unsigned>(BIDX),
                                            static_cast<uint16_t>(NC)};
          break;
        default:
          return make_error<CoverageMapError>(coveragemap_error::malformed,
                                              "region kind is incorrect");
        }
      }
    }

    // Read the source range.
    uint64_t LineStartDelta, ColumnStart, NumLines, ColumnEnd;
    if (auto Err =
            readIntMax(LineStartDelta, std::numeric_limits<unsigned>::max()))
      return Err;
    if (auto Err = readULEB128(ColumnStart))
      return Err;
    if (ColumnStart > std::numeric_limits<unsigned>::max())
      return make_error<CoverageMapError>(coveragemap_error::malformed,
                                          "start column is too big");
    if (auto Err = readIntMax(NumLines, std::numeric_limits<unsigned>::max()))
      return Err;
    if (auto Err = readIntMax(ColumnEnd, std::numeric_limits<unsigned>::max()))
      return Err;
    LineStart += LineStartDelta;

    // If the high bit of ColumnEnd is set, this is a gap region.
    if (ColumnEnd & (1U << 31)) {
      Kind = CounterMappingRegion::GapRegion;
      ColumnEnd &= ~(1U << 31);
    }

    // Adjust the column locations for the empty regions that are supposed to
    // cover whole lines. Those regions should be encoded with the
    // column range (1 -> std::numeric_limits<unsigned>::max()), but because
    // the encoded std::numeric_limits<unsigned>::max() is several bytes long,
    // we set the column range to (0 -> 0) to ensure that the column start and
    // column end take up one byte each.
    // The std::numeric_limits<unsigned>::max() is used to represent a column
    // position at the end of the line without knowing the length of that line.
    if (ColumnStart == 0 && ColumnEnd == 0) {
      ColumnStart = 1;
      ColumnEnd = std::numeric_limits<unsigned>::max();
    }

    LLVM_DEBUG({
      dbgs() << "Counter in file " << InferredFileID << " " << LineStart << ":"
             << ColumnStart << " -> " << (LineStart + NumLines) << ":"
             << ColumnEnd << ", ";
      if (Kind == CounterMappingRegion::ExpansionRegion)
        dbgs() << "Expands to file " << ExpandedFileID;
      else
        CounterMappingContext(Expressions).dump(C, dbgs());
      dbgs() << "\n";
    });

    auto CMR = CounterMappingRegion(
        C, C2, InferredFileID, ExpandedFileID, LineStart, ColumnStart,
        LineStart + NumLines, ColumnEnd, Kind, Params);
    if (CMR.startLoc() > CMR.endLoc())
      return make_error<CoverageMapError>(
          coveragemap_error::malformed,
          "counter mapping region locations are incorrect");
    MappingRegions.push_back(CMR);
  }
  return Error::success();
}

Error RawCoverageMappingReader::read() {
  // Read the virtual file mapping.
  SmallVector<unsigned, 8> VirtualFileMapping;
  uint64_t NumFileMappings;
  if (auto Err = readSize(NumFileMappings))
    return Err;
  for (size_t I = 0; I < NumFileMappings; ++I) {
    uint64_t FilenameIndex;
    if (auto Err = readIntMax(FilenameIndex, TranslationUnitFilenames.size()))
      return Err;
    VirtualFileMapping.push_back(FilenameIndex);
  }

  // Construct the files using unique filenames and virtual file mapping.
  for (auto I : VirtualFileMapping) {
    Filenames.push_back(TranslationUnitFilenames[I]);
  }

  // Read the expressions.
  uint64_t NumExpressions;
  if (auto Err = readSize(NumExpressions))
    return Err;
  // Create an array of dummy expressions that get the proper counters
  // when the expressions are read, and the proper kinds when the counters
  // are decoded.
  Expressions.resize(
      NumExpressions,
      CounterExpression(CounterExpression::Subtract, Counter(), Counter()));
  for (size_t I = 0; I < NumExpressions; ++I) {
    if (auto Err = readCounter(Expressions[I].LHS))
      return Err;
    if (auto Err = readCounter(Expressions[I].RHS))
      return Err;
  }

  // Read the mapping regions sub-arrays.
  for (unsigned InferredFileID = 0, S = VirtualFileMapping.size();
       InferredFileID < S; ++InferredFileID) {
    if (auto Err = readMappingRegionsSubArray(MappingRegions, InferredFileID,
                                              VirtualFileMapping.size()))
      return Err;
  }

  // Set the counters for the expansion regions.
  // i.e. Counter of expansion region = counter of the first region
  // from the expanded file.
  // Perform multiple passes to correctly propagate the counters through
  // all the nested expansion regions.
  SmallVector<CounterMappingRegion *, 8> FileIDExpansionRegionMapping;
  FileIDExpansionRegionMapping.resize(VirtualFileMapping.size(), nullptr);
  for (unsigned Pass = 1, S = VirtualFileMapping.size(); Pass < S; ++Pass) {
    for (auto &R : MappingRegions) {
      if (R.Kind != CounterMappingRegion::ExpansionRegion)
        continue;
      assert(!FileIDExpansionRegionMapping[R.ExpandedFileID]);
      FileIDExpansionRegionMapping[R.ExpandedFileID] = &R;
    }
    for (auto &R : MappingRegions) {
      if (FileIDExpansionRegionMapping[R.FileID]) {
        FileIDExpansionRegionMapping[R.FileID]->Count = R.Count;
        FileIDExpansionRegionMapping[R.FileID] = nullptr;
      }
    }
  }

  return Error::success();
}

Expected<bool> RawCoverageMappingDummyChecker::isDummy() {
  // A dummy coverage mapping data consists of just one region with zero count.
  uint64_t NumFileMappings;
  if (Error Err = readSize(NumFileMappings))
    return std::move(Err);
  if (NumFileMappings != 1)
    return false;
  // We don't expect any specific value for the filename index, just skip it.
  uint64_t FilenameIndex;
  if (Error Err =
          readIntMax(FilenameIndex, std::numeric_limits<unsigned>::max()))
    return std::move(Err);
  uint64_t NumExpressions;
  if (Error Err = readSize(NumExpressions))
    return std::move(Err);
  if (NumExpressions != 0)
    return false;
  uint64_t NumRegions;
  if (Error Err = readSize(NumRegions))
    return std::move(Err);
  if (NumRegions != 1)
    return false;
  uint64_t EncodedCounterAndRegion;
  if (Error Err = readIntMax(EncodedCounterAndRegion,
                             std::numeric_limits<unsigned>::max()))
    return std::move(Err);
  unsigned Tag = EncodedCounterAndRegion & Counter::EncodingTagMask;
  return Tag == Counter::Zero;
}

Error InstrProfSymtab::create(SectionRef &Section) {
  Expected<StringRef> DataOrErr = Section.getContents();
  if (!DataOrErr)
    return DataOrErr.takeError();
  Data = *DataOrErr;
  Address = Section.getAddress();

  // If this is a linked PE/COFF file, then we have to skip over the null byte
  // that is allocated in the .lprfn$A section in the LLVM profiling runtime.
  // If the name section is .lprfcovnames, it doesn't have the null byte at the
  // beginning.
  const ObjectFile *Obj = Section.getObject();
  if (isa<COFFObjectFile>(Obj) && !Obj->isRelocatableObject())
    if (Expected<StringRef> NameOrErr = Section.getName())
      if (*NameOrErr != getInstrProfSectionName(IPSK_covname, Triple::COFF))
        Data = Data.drop_front(1);

  return Error::success();
}

StringRef InstrProfSymtab::getFuncName(uint64_t Pointer, size_t Size) {
  if (Pointer < Address)
    return StringRef();
  auto Offset = Pointer - Address;
  if (Offset + Size > Data.size())
    return StringRef();
  return Data.substr(Pointer - Address, Size);
}

// Check if the mapping data is a dummy, i.e. is emitted for an unused function.
static Expected<bool> isCoverageMappingDummy(uint64_t Hash, StringRef Mapping) {
  // The hash value of dummy mapping records is always zero.
  if (Hash)
    return false;
  return RawCoverageMappingDummyChecker(Mapping).isDummy();
}

/// A range of filename indices. Used to specify the location of a batch of
/// filenames in a vector-like container.
struct FilenameRange {
  unsigned StartingIndex;
  unsigned Length;

  FilenameRange(unsigned StartingIndex, unsigned Length)
      : StartingIndex(StartingIndex), Length(Length) {}

  void markInvalid() { Length = 0; }
  bool isInvalid() const { return Length == 0; }
};

namespace {

/// The interface to read coverage mapping function records for a module.
struct CovMapFuncRecordReader {
  virtual ~CovMapFuncRecordReader() = default;

  // Read a coverage header.
  //
  // \p CovBuf points to the buffer containing the \c CovHeader of the coverage
  // mapping data associated with the module.
  //
  // Returns a pointer to the next \c CovHeader if it exists, or to an address
  // greater than \p CovEnd if not.
  virtual Expected<const char *> readCoverageHeader(const char *CovBuf,
                                                    const char *CovBufEnd) = 0;

  // Read function records.
  //
  // \p FuncRecBuf points to the buffer containing a batch of function records.
  // \p FuncRecBufEnd points past the end of the batch of records.
  //
  // Prior to Version4, \p OutOfLineFileRange points to a sequence of filenames
  // associated with the function records. It is unused in Version4.
  //
  // Prior to Version4, \p OutOfLineMappingBuf points to a sequence of coverage
  // mappings associated with the function records. It is unused in Version4.
  virtual Error
  readFunctionRecords(const char *FuncRecBuf, const char *FuncRecBufEnd,
                      std::optional<FilenameRange> OutOfLineFileRange,
                      const char *OutOfLineMappingBuf,
                      const char *OutOfLineMappingBufEnd) = 0;

  template <class IntPtrT, llvm::endianness Endian>
  static Expected<std::unique_ptr<CovMapFuncRecordReader>>
  get(CovMapVersion Version, InstrProfSymtab &P,
      std::vector<BinaryCoverageReader::ProfileMappingRecord> &R, StringRef D,
      std::vector<std::string> &F);
};

// A class for reading coverage mapping function records for a module.
template <CovMapVersion Version, class IntPtrT, llvm::endianness Endian>
class VersionedCovMapFuncRecordReader : public CovMapFuncRecordReader {
  using FuncRecordType =
      typename CovMapTraits<Version, IntPtrT>::CovMapFuncRecordType;
  using NameRefType = typename CovMapTraits<Version, IntPtrT>::NameRefType;

  // Maps function's name references to the indexes of their records
  // in \c Records.
  DenseMap<NameRefType, size_t> FunctionRecords;
  InstrProfSymtab &ProfileNames;
  StringRef CompilationDir;
  std::vector<std::string> &Filenames;
  std::vector<BinaryCoverageReader::ProfileMappingRecord> &Records;

  // Maps a hash of the filenames in a TU to a \c FileRange. The range
  // specifies the location of the hashed filenames in \c Filenames.
  DenseMap<uint64_t, FilenameRange> FileRangeMap;

  // Add the record to the collection if we don't already have a record that
  // points to the same function name. This is useful to ignore the redundant
  // records for the functions with ODR linkage.
  // In addition, prefer records with real coverage mapping data to dummy
  // records, which were emitted for inline functions which were seen but
  // not used in the corresponding translation unit.
  Error insertFunctionRecordIfNeeded(const FuncRecordType *CFR,
                                     StringRef Mapping,
                                     FilenameRange FileRange) {
    ++CovMapNumRecords;
    uint64_t FuncHash = CFR->template getFuncHash<Endian>();
    NameRefType NameRef = CFR->template getFuncNameRef<Endian>();
    auto InsertResult =
        FunctionRecords.insert(std::make_pair(NameRef, Records.size()));
    if (InsertResult.second) {
      StringRef FuncName;
      if (Error Err = CFR->template getFuncName<Endian>(ProfileNames, FuncName))
        return Err;
      if (FuncName.empty())
        return make_error<InstrProfError>(instrprof_error::malformed,
                                          "function name is empty");
      ++CovMapNumUsedRecords;
      Records.emplace_back(Version, FuncName, FuncHash, Mapping,
                           FileRange.StartingIndex, FileRange.Length);
      return Error::success();
    }
    // Update the existing record if it's a dummy and the new record is real.
    size_t OldRecordIndex = InsertResult.first->second;
    BinaryCoverageReader::ProfileMappingRecord &OldRecord =
        Records[OldRecordIndex];
    Expected<bool> OldIsDummyExpected = isCoverageMappingDummy(
        OldRecord.FunctionHash, OldRecord.CoverageMapping);
    if (Error Err = OldIsDummyExpected.takeError())
      return Err;
    if (!*OldIsDummyExpected)
      return Error::success();
    Expected<bool> NewIsDummyExpected =
        isCoverageMappingDummy(FuncHash, Mapping);
    if (Error Err = NewIsDummyExpected.takeError())
      return Err;
    if (*NewIsDummyExpected)
      return Error::success();
    ++CovMapNumUsedRecords;
    OldRecord.FunctionHash = FuncHash;
    OldRecord.CoverageMapping = Mapping;
    OldRecord.FilenamesBegin = FileRange.StartingIndex;
    OldRecord.FilenamesSize = FileRange.Length;
    return Error::success();
  }

public:
  VersionedCovMapFuncRecordReader(
      InstrProfSymtab &P,
      std::vector<BinaryCoverageReader::ProfileMappingRecord> &R, StringRef D,
      std::vector<std::string> &F)
      : ProfileNames(P), CompilationDir(D), Filenames(F), Records(R) {}

  ~VersionedCovMapFuncRecordReader() override = default;

  Expected<const char *> readCoverageHeader(const char *CovBuf,
                                            const char *CovBufEnd) override {
    using namespace support;

    if (CovBuf + sizeof(CovMapHeader) > CovBufEnd)
      return make_error<CoverageMapError>(
          coveragemap_error::malformed,
          "coverage mapping header section is larger than buffer size");
    auto CovHeader = reinterpret_cast<const CovMapHeader *>(CovBuf);
    uint32_t NRecords = CovHeader->getNRecords<Endian>();
    uint32_t FilenamesSize = CovHeader->getFilenamesSize<Endian>();
    uint32_t CoverageSize = CovHeader->getCoverageSize<Endian>();
    assert((CovMapVersion)CovHeader->getVersion<Endian>() == Version);
    CovBuf = reinterpret_cast<const char *>(CovHeader + 1);

    // Skip past the function records, saving the start and end for later.
    // This is a no-op in Version4 (function records are read after all headers
    // are read).
    const char *FuncRecBuf = nullptr;
    const char *FuncRecBufEnd = nullptr;
    if (Version < CovMapVersion::Version4)
      FuncRecBuf = CovBuf;
    CovBuf += NRecords * sizeof(FuncRecordType);
    if (Version < CovMapVersion::Version4)
      FuncRecBufEnd = CovBuf;

    // Get the filenames.
    if (CovBuf + FilenamesSize > CovBufEnd)
      return make_error<CoverageMapError>(
          coveragemap_error::malformed,
          "filenames section is larger than buffer size");
    size_t FilenamesBegin = Filenames.size();
    StringRef FilenameRegion(CovBuf, FilenamesSize);
    RawCoverageFilenamesReader Reader(FilenameRegion, Filenames,
                                      CompilationDir);
    if (auto Err = Reader.read(Version))
      return std::move(Err);
    CovBuf += FilenamesSize;
    FilenameRange FileRange(FilenamesBegin, Filenames.size() - FilenamesBegin);

    if (Version >= CovMapVersion::Version4) {
      // Map a hash of the filenames region to the filename range associated
      // with this coverage header.
      int64_t FilenamesRef =
          llvm::IndexedInstrProf::ComputeHash(FilenameRegion);
      auto Insert =
          FileRangeMap.insert(std::make_pair(FilenamesRef, FileRange));
      if (!Insert.second) {
        // The same filenames ref was encountered twice. It's possible that
        // the associated filenames are the same.
        auto It = Filenames.begin();
        FilenameRange &OrigRange = Insert.first->getSecond();
        if (std::equal(It + OrigRange.StartingIndex,
                       It + OrigRange.StartingIndex + OrigRange.Length,
                       It + FileRange.StartingIndex,
                       It + FileRange.StartingIndex + FileRange.Length))
          // Map the new range to the original one.
          FileRange = OrigRange;
        else
          // This is a hash collision. Mark the filenames ref invalid.
          OrigRange.markInvalid();
      }
    }

    // We'll read the coverage mapping records in the loop below.
    // This is a no-op in Version4 (coverage mappings are not affixed to the
    // coverage header).
    const char *MappingBuf = CovBuf;
    if (Version >= CovMapVersion::Version4 && CoverageSize != 0)
      return make_error<CoverageMapError>(coveragemap_error::malformed,
                                          "coverage mapping size is not zero");
    CovBuf += CoverageSize;
    const char *MappingEnd = CovBuf;

    if (CovBuf > CovBufEnd)
      return make_error<CoverageMapError>(
          coveragemap_error::malformed,
          "function records section is larger than buffer size");

    if (Version < CovMapVersion::Version4) {
      // Read each function record.
      if (Error E = readFunctionRecords(FuncRecBuf, FuncRecBufEnd, FileRange,
                                        MappingBuf, MappingEnd))
        return std::move(E);
    }

    // Each coverage map has an alignment of 8, so we need to adjust alignment
    // before reading the next map.
    CovBuf += offsetToAlignedAddr(CovBuf, Align(8));

    return CovBuf;
  }

  Error readFunctionRecords(const char *FuncRecBuf, const char *FuncRecBufEnd,
                            std::optional<FilenameRange> OutOfLineFileRange,
                            const char *OutOfLineMappingBuf,
                            const char *OutOfLineMappingBufEnd) override {
    auto CFR = reinterpret_cast<const FuncRecordType *>(FuncRecBuf);
    while ((const char *)CFR < FuncRecBufEnd) {
      // Validate the length of the coverage mapping for this function.
      const char *NextMappingBuf;
      const FuncRecordType *NextCFR;
      std::tie(NextMappingBuf, NextCFR) =
          CFR->template advanceByOne<Endian>(OutOfLineMappingBuf);
      if (Version < CovMapVersion::Version4)
        if (NextMappingBuf > OutOfLineMappingBufEnd)
          return make_error<CoverageMapError>(
              coveragemap_error::malformed,
              "next mapping buffer is larger than buffer size");

      // Look up the set of filenames associated with this function record.
      std::optional<FilenameRange> FileRange;
      if (Version < CovMapVersion::Version4) {
        FileRange = OutOfLineFileRange;
      } else {
        uint64_t FilenamesRef = CFR->template getFilenamesRef<Endian>();
        auto It = FileRangeMap.find(FilenamesRef);
        if (It == FileRangeMap.end())
          return make_error<CoverageMapError>(
              coveragemap_error::malformed,
              "no filename found for function with hash=0x" +
                  Twine::utohexstr(FilenamesRef));
        else
          FileRange = It->getSecond();
      }

      // Now, read the coverage data.
      if (FileRange && !FileRange->isInvalid()) {
        StringRef Mapping =
            CFR->template getCoverageMapping<Endian>(OutOfLineMappingBuf);
        if (Version >= CovMapVersion::Version4 &&
            Mapping.data() + Mapping.size() > FuncRecBufEnd)
          return make_error<CoverageMapError>(
              coveragemap_error::malformed,
              "coverage mapping data is larger than buffer size");
        if (Error Err = insertFunctionRecordIfNeeded(CFR, Mapping, *FileRange))
          return Err;
      }

      std::tie(OutOfLineMappingBuf, CFR) = std::tie(NextMappingBuf, NextCFR);
    }
    return Error::success();
  }
};

} // end anonymous namespace

template <class IntPtrT, llvm::endianness Endian>
Expected<std::unique_ptr<CovMapFuncRecordReader>> CovMapFuncRecordReader::get(
    CovMapVersion Version, InstrProfSymtab &P,
    std::vector<BinaryCoverageReader::ProfileMappingRecord> &R, StringRef D,
    std::vector<std::string> &F) {
  using namespace coverage;

  switch (Version) {
  case CovMapVersion::Version1:
    return std::make_unique<VersionedCovMapFuncRecordReader<
        CovMapVersion::Version1, IntPtrT, Endian>>(P, R, D, F);
  case CovMapVersion::Version2:
  case CovMapVersion::Version3:
  case CovMapVersion::Version4:
  case CovMapVersion::Version5:
  case CovMapVersion::Version6:
  case CovMapVersion::Version7:
    // Decompress the name data.
    if (Error E = P.create(P.getNameData()))
      return std::move(E);
    if (Version == CovMapVersion::Version2)
      return std::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version2, IntPtrT, Endian>>(P, R, D, F);
    else if (Version == CovMapVersion::Version3)
      return std::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version3, IntPtrT, Endian>>(P, R, D, F);
    else if (Version == CovMapVersion::Version4)
      return std::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version4, IntPtrT, Endian>>(P, R, D, F);
    else if (Version == CovMapVersion::Version5)
      return std::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version5, IntPtrT, Endian>>(P, R, D, F);
    else if (Version == CovMapVersion::Version6)
      return std::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version6, IntPtrT, Endian>>(P, R, D, F);
    else if (Version == CovMapVersion::Version7)
      return std::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version7, IntPtrT, Endian>>(P, R, D, F);
  }
  llvm_unreachable("Unsupported version");
}

template <typename T, llvm::endianness Endian>
static Error readCoverageMappingData(
    InstrProfSymtab &ProfileNames, StringRef CovMap, StringRef FuncRecords,
    std::vector<BinaryCoverageReader::ProfileMappingRecord> &Records,
    StringRef CompilationDir, std::vector<std::string> &Filenames) {
  using namespace coverage;

  // Read the records in the coverage data section.
  auto CovHeader =
      reinterpret_cast<const CovMapHeader *>(CovMap.data());
  CovMapVersion Version = (CovMapVersion)CovHeader->getVersion<Endian>();
  if (Version > CovMapVersion::CurrentVersion)
    return make_error<CoverageMapError>(coveragemap_error::unsupported_version);
  Expected<std::unique_ptr<CovMapFuncRecordReader>> ReaderExpected =
      CovMapFuncRecordReader::get<T, Endian>(Version, ProfileNames, Records,
                                             CompilationDir, Filenames);
  if (Error E = ReaderExpected.takeError())
    return E;
  auto Reader = std::move(ReaderExpected.get());
  const char *CovBuf = CovMap.data();
  const char *CovBufEnd = CovBuf + CovMap.size();
  const char *FuncRecBuf = FuncRecords.data();
  const char *FuncRecBufEnd = FuncRecords.data() + FuncRecords.size();
  while (CovBuf < CovBufEnd) {
    // Read the current coverage header & filename data.
    //
    // Prior to Version4, this also reads all function records affixed to the
    // header.
    //
    // Return a pointer to the next coverage header.
    auto NextOrErr = Reader->readCoverageHeader(CovBuf, CovBufEnd);
    if (auto E = NextOrErr.takeError())
      return E;
    CovBuf = NextOrErr.get();
  }
  // In Version4, function records are not affixed to coverage headers. Read
  // the records from their dedicated section.
  if (Version >= CovMapVersion::Version4)
    return Reader->readFunctionRecords(FuncRecBuf, FuncRecBufEnd, std::nullopt,
                                       nullptr, nullptr);
  return Error::success();
}

Expected<std::unique_ptr<BinaryCoverageReader>>
BinaryCoverageReader::createCoverageReaderFromBuffer(
    StringRef Coverage, FuncRecordsStorage &&FuncRecords,
    std::unique_ptr<InstrProfSymtab> ProfileNamesPtr, uint8_t BytesInAddress,
    llvm::endianness Endian, StringRef CompilationDir) {
  if (ProfileNamesPtr == nullptr)
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "Caller must provide ProfileNames");
  std::unique_ptr<BinaryCoverageReader> Reader(new BinaryCoverageReader(
      std::move(ProfileNamesPtr), std::move(FuncRecords)));
  InstrProfSymtab &ProfileNames = *Reader->ProfileNames;
  StringRef FuncRecordsRef = Reader->FuncRecords->getBuffer();
  if (BytesInAddress == 4 && Endian == llvm::endianness::little) {
    if (Error E = readCoverageMappingData<uint32_t, llvm::endianness::little>(
            ProfileNames, Coverage, FuncRecordsRef, Reader->MappingRecords,
            CompilationDir, Reader->Filenames))
      return std::move(E);
  } else if (BytesInAddress == 4 && Endian == llvm::endianness::big) {
    if (Error E = readCoverageMappingData<uint32_t, llvm::endianness::big>(
            ProfileNames, Coverage, FuncRecordsRef, Reader->MappingRecords,
            CompilationDir, Reader->Filenames))
      return std::move(E);
  } else if (BytesInAddress == 8 && Endian == llvm::endianness::little) {
    if (Error E = readCoverageMappingData<uint64_t, llvm::endianness::little>(
            ProfileNames, Coverage, FuncRecordsRef, Reader->MappingRecords,
            CompilationDir, Reader->Filenames))
      return std::move(E);
  } else if (BytesInAddress == 8 && Endian == llvm::endianness::big) {
    if (Error E = readCoverageMappingData<uint64_t, llvm::endianness::big>(
            ProfileNames, Coverage, FuncRecordsRef, Reader->MappingRecords,
            CompilationDir, Reader->Filenames))
      return std::move(E);
  } else
    return make_error<CoverageMapError>(
        coveragemap_error::malformed,
        "not supported endianness or bytes in address");
  return std::move(Reader);
}

static Expected<std::unique_ptr<BinaryCoverageReader>>
loadTestingFormat(StringRef Data, StringRef CompilationDir) {
  uint8_t BytesInAddress = 8;
  llvm::endianness Endian = llvm::endianness::little;

  // Read the magic and version.
  Data = Data.substr(sizeof(TestingFormatMagic));
  if (Data.size() < sizeof(uint64_t))
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "the size of data is too small");
  auto TestingVersion =
      support::endian::byte_swap<uint64_t, llvm::endianness::little>(
          *reinterpret_cast<const uint64_t *>(Data.data()));
  Data = Data.substr(sizeof(uint64_t));

  // Read the ProfileNames data.
  if (Data.empty())
    return make_error<CoverageMapError>(coveragemap_error::truncated);
  unsigned N = 0;
  uint64_t ProfileNamesSize = decodeULEB128(Data.bytes_begin(), &N);
  if (N > Data.size())
    return make_error<CoverageMapError>(
        coveragemap_error::malformed,
        "the size of TestingFormatMagic is too big");
  Data = Data.substr(N);
  if (Data.empty())
    return make_error<CoverageMapError>(coveragemap_error::truncated);
  N = 0;
  uint64_t Address = decodeULEB128(Data.bytes_begin(), &N);
  if (N > Data.size())
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "the size of ULEB128 is too big");
  Data = Data.substr(N);
  if (Data.size() < ProfileNamesSize)
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "the size of ProfileNames is too big");
  auto ProfileNames = std::make_unique<InstrProfSymtab>();
  if (Error E = ProfileNames->create(Data.substr(0, ProfileNamesSize), Address))
    return std::move(E);
  Data = Data.substr(ProfileNamesSize);

  // In Version2, the size of CoverageMapping is stored directly.
  uint64_t CoverageMappingSize;
  if (TestingVersion == uint64_t(TestingFormatVersion::Version2)) {
    N = 0;
    CoverageMappingSize = decodeULEB128(Data.bytes_begin(), &N);
    if (N > Data.size())
      return make_error<CoverageMapError>(coveragemap_error::malformed,
                                          "the size of ULEB128 is too big");
    Data = Data.substr(N);
    if (CoverageMappingSize < sizeof(CovMapHeader))
      return make_error<CoverageMapError>(
          coveragemap_error::malformed,
          "the size of CoverageMapping is teoo small");
  } else if (TestingVersion != uint64_t(TestingFormatVersion::Version1)) {
    return make_error<CoverageMapError>(coveragemap_error::unsupported_version);
  }

  // Skip the padding bytes because coverage map data has an alignment of 8.
  auto Pad = offsetToAlignedAddr(Data.data(), Align(8));
  if (Data.size() < Pad)
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "insufficient padding");
  Data = Data.substr(Pad);
  if (Data.size() < sizeof(CovMapHeader))
    return make_error<CoverageMapError>(
        coveragemap_error::malformed,
        "coverage mapping header section is larger than data size");
  auto const *CovHeader = reinterpret_cast<const CovMapHeader *>(
      Data.substr(0, sizeof(CovMapHeader)).data());
  auto Version =
      CovMapVersion(CovHeader->getVersion<llvm::endianness::little>());

  // In Version1, the size of CoverageMapping is calculated.
  if (TestingVersion == uint64_t(TestingFormatVersion::Version1)) {
    if (Version < CovMapVersion::Version4) {
      CoverageMappingSize = Data.size();
    } else {
      auto FilenamesSize =
          CovHeader->getFilenamesSize<llvm::endianness::little>();
      CoverageMappingSize = sizeof(CovMapHeader) + FilenamesSize;
    }
  }

  auto CoverageMapping = Data.substr(0, CoverageMappingSize);
  Data = Data.substr(CoverageMappingSize);

  // Read the CoverageRecords data.
  if (Version < CovMapVersion::Version4) {
    if (!Data.empty())
      return make_error<CoverageMapError>(coveragemap_error::malformed,
                                          "data is not empty");
  } else {
    // Skip the padding bytes because coverage records data has an alignment
    // of 8.
    Pad = offsetToAlignedAddr(Data.data(), Align(8));
    if (Data.size() < Pad)
      return make_error<CoverageMapError>(coveragemap_error::malformed,
                                          "insufficient padding");
    Data = Data.substr(Pad);
  }
  BinaryCoverageReader::FuncRecordsStorage CoverageRecords =
      MemoryBuffer::getMemBuffer(Data);

  return BinaryCoverageReader::createCoverageReaderFromBuffer(
      CoverageMapping, std::move(CoverageRecords), std::move(ProfileNames),
      BytesInAddress, Endian, CompilationDir);
}

/// Find all sections that match \p IPSK name. There may be more than one if
/// comdats are in use, e.g. for the __llvm_covfun section on ELF.
static Expected<std::vector<SectionRef>>
lookupSections(ObjectFile &OF, InstrProfSectKind IPSK) {
  auto ObjFormat = OF.getTripleObjectFormat();
  auto Name =
      getInstrProfSectionName(IPSK, ObjFormat, /*AddSegmentInfo=*/false);
  // On COFF, the object file section name may end in "$M". This tells the
  // linker to sort these sections between "$A" and "$Z". The linker removes the
  // dollar and everything after it in the final binary. Do the same to match.
  bool IsCOFF = isa<COFFObjectFile>(OF);
  auto stripSuffix = [IsCOFF](StringRef N) {
    return IsCOFF ? N.split('$').first : N;
  };
  Name = stripSuffix(Name);

  std::vector<SectionRef> Sections;
  for (const auto &Section : OF.sections()) {
    Expected<StringRef> NameOrErr = Section.getName();
    if (!NameOrErr)
      return NameOrErr.takeError();
    if (stripSuffix(*NameOrErr) == Name) {
      // COFF profile name section contains two null bytes indicating the
      // start/end of the section. If its size is 2 bytes, it's empty.
      if (IsCOFF && IPSK == IPSK_name && Section.getSize() == 2)
        continue;
      Sections.push_back(Section);
    }
  }
  if (Sections.empty())
    return make_error<CoverageMapError>(coveragemap_error::no_data_found);
  return Sections;
}

static Expected<std::unique_ptr<BinaryCoverageReader>>
loadBinaryFormat(std::unique_ptr<Binary> Bin, StringRef Arch,
                 StringRef CompilationDir = "",
                 object::BuildIDRef *BinaryID = nullptr) {
  std::unique_ptr<ObjectFile> OF;
  if (auto *Universal = dyn_cast<MachOUniversalBinary>(Bin.get())) {
    // If we have a universal binary, try to look up the object for the
    // appropriate architecture.
    auto ObjectFileOrErr = Universal->getMachOObjectForArch(Arch);
    if (!ObjectFileOrErr)
      return ObjectFileOrErr.takeError();
    OF = std::move(ObjectFileOrErr.get());
  } else if (isa<ObjectFile>(Bin.get())) {
    // For any other object file, upcast and take ownership.
    OF.reset(cast<ObjectFile>(Bin.release()));
    // If we've asked for a particular arch, make sure they match.
    if (!Arch.empty() && OF->getArch() != Triple(Arch).getArch())
      return errorCodeToError(object_error::arch_not_found);
  } else
    // We can only handle object files.
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "binary is not an object file");

  // The coverage uses native pointer sizes for the object it's written in.
  uint8_t BytesInAddress = OF->getBytesInAddress();
  llvm::endianness Endian =
      OF->isLittleEndian() ? llvm::endianness::little : llvm::endianness::big;

  // Look for the sections that we are interested in.
  auto ProfileNames = std::make_unique<InstrProfSymtab>();
  std::vector<SectionRef> NamesSectionRefs;
  // If IPSK_name is not found, fallback to search for IPK_covname, which is
  // used when binary correlation is enabled.
  auto NamesSection = lookupSections(*OF, IPSK_name);
  if (auto E = NamesSection.takeError()) {
    consumeError(std::move(E));
    NamesSection = lookupSections(*OF, IPSK_covname);
    if (auto E = NamesSection.takeError())
      return std::move(E);
  }
  NamesSectionRefs = *NamesSection;

  if (NamesSectionRefs.size() != 1)
    return make_error<CoverageMapError>(
        coveragemap_error::malformed,
        "the size of coverage mapping section is not one");
  if (Error E = ProfileNames->create(NamesSectionRefs.back()))
    return std::move(E);

  auto CoverageSection = lookupSections(*OF, IPSK_covmap);
  if (auto E = CoverageSection.takeError())
    return std::move(E);
  std::vector<SectionRef> CoverageSectionRefs = *CoverageSection;
  if (CoverageSectionRefs.size() != 1)
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "the size of name section is not one");
  auto CoverageMappingOrErr = CoverageSectionRefs.back().getContents();
  if (!CoverageMappingOrErr)
    return CoverageMappingOrErr.takeError();
  StringRef CoverageMapping = CoverageMappingOrErr.get();

  // Look for the coverage records section (Version4 only).
  auto CoverageRecordsSections = lookupSections(*OF, IPSK_covfun);

  BinaryCoverageReader::FuncRecordsStorage FuncRecords;
  if (auto E = CoverageRecordsSections.takeError()) {
    consumeError(std::move(E));
    FuncRecords = MemoryBuffer::getMemBuffer("");
  } else {
    // Compute the FuncRecordsBuffer of the buffer, taking into account the
    // padding between each record, and making sure the first block is aligned
    // in memory to maintain consistency between buffer address and size
    // alignment.
    const Align RecordAlignment(8);
    uint64_t FuncRecordsSize = 0;
    for (SectionRef Section : *CoverageRecordsSections) {
      auto CoverageRecordsOrErr = Section.getContents();
      if (!CoverageRecordsOrErr)
        return CoverageRecordsOrErr.takeError();
      FuncRecordsSize += alignTo(CoverageRecordsOrErr->size(), RecordAlignment);
    }
    auto WritableBuffer =
        WritableMemoryBuffer::getNewUninitMemBuffer(FuncRecordsSize);
    char *FuncRecordsBuffer = WritableBuffer->getBufferStart();
    assert(isAddrAligned(RecordAlignment, FuncRecordsBuffer) &&
           "Allocated memory is correctly aligned");

    for (SectionRef Section : *CoverageRecordsSections) {
      auto CoverageRecordsOrErr = Section.getContents();
      if (!CoverageRecordsOrErr)
        return CoverageRecordsOrErr.takeError();
      const auto &CoverageRecords = CoverageRecordsOrErr.get();
      FuncRecordsBuffer = std::copy(CoverageRecords.begin(),
                                    CoverageRecords.end(), FuncRecordsBuffer);
      FuncRecordsBuffer =
          std::fill_n(FuncRecordsBuffer,
                      alignAddr(FuncRecordsBuffer, RecordAlignment) -
                          (uintptr_t)FuncRecordsBuffer,
                      '\0');
    }
    assert(FuncRecordsBuffer == WritableBuffer->getBufferEnd() &&
           "consistent init");
    FuncRecords = std::move(WritableBuffer);
  }

  if (BinaryID)
    *BinaryID = getBuildID(OF.get());

  return BinaryCoverageReader::createCoverageReaderFromBuffer(
      CoverageMapping, std::move(FuncRecords), std::move(ProfileNames),
      BytesInAddress, Endian, CompilationDir);
}

/// Determine whether \p Arch is invalid or empty, given \p Bin.
static bool isArchSpecifierInvalidOrMissing(Binary *Bin, StringRef Arch) {
  // If we have a universal binary and Arch doesn't identify any of its slices,
  // it's user error.
  if (auto *Universal = dyn_cast<MachOUniversalBinary>(Bin)) {
    for (auto &ObjForArch : Universal->objects())
      if (Arch == ObjForArch.getArchFlagName())
        return false;
    return true;
  }
  return false;
}

Expected<std::vector<std::unique_ptr<BinaryCoverageReader>>>
BinaryCoverageReader::create(
    MemoryBufferRef ObjectBuffer, StringRef Arch,
    SmallVectorImpl<std::unique_ptr<MemoryBuffer>> &ObjectFileBuffers,
    StringRef CompilationDir, SmallVectorImpl<object::BuildIDRef> *BinaryIDs) {
  std::vector<std::unique_ptr<BinaryCoverageReader>> Readers;

  if (ObjectBuffer.getBuffer().size() > sizeof(TestingFormatMagic)) {
    uint64_t Magic =
        support::endian::byte_swap<uint64_t, llvm::endianness::little>(
            *reinterpret_cast<const uint64_t *>(ObjectBuffer.getBufferStart()));
    if (Magic == TestingFormatMagic) {
      // This is a special format used for testing.
      auto ReaderOrErr =
          loadTestingFormat(ObjectBuffer.getBuffer(), CompilationDir);
      if (!ReaderOrErr)
        return ReaderOrErr.takeError();
      Readers.push_back(std::move(ReaderOrErr.get()));
      return std::move(Readers);
    }
  }

  auto BinOrErr = createBinary(ObjectBuffer);
  if (!BinOrErr)
    return BinOrErr.takeError();
  std::unique_ptr<Binary> Bin = std::move(BinOrErr.get());

  if (isArchSpecifierInvalidOrMissing(Bin.get(), Arch))
    return make_error<CoverageMapError>(
        coveragemap_error::invalid_or_missing_arch_specifier);

  // MachO universal binaries which contain archives need to be treated as
  // archives, not as regular binaries.
  if (auto *Universal = dyn_cast<MachOUniversalBinary>(Bin.get())) {
    for (auto &ObjForArch : Universal->objects()) {
      // Skip slices within the universal binary which target the wrong arch.
      std::string ObjArch = ObjForArch.getArchFlagName();
      if (Arch != ObjArch)
        continue;

      auto ArchiveOrErr = ObjForArch.getAsArchive();
      if (!ArchiveOrErr) {
        // If this is not an archive, try treating it as a regular object.
        consumeError(ArchiveOrErr.takeError());
        break;
      }

      return BinaryCoverageReader::create(
          ArchiveOrErr.get()->getMemoryBufferRef(), Arch, ObjectFileBuffers,
          CompilationDir, BinaryIDs);
    }
  }

  // Load coverage out of archive members.
  if (auto *Ar = dyn_cast<Archive>(Bin.get())) {
    Error Err = Error::success();
    for (auto &Child : Ar->children(Err)) {
      Expected<MemoryBufferRef> ChildBufOrErr = Child.getMemoryBufferRef();
      if (!ChildBufOrErr)
        return ChildBufOrErr.takeError();

      auto ChildReadersOrErr = BinaryCoverageReader::create(
          ChildBufOrErr.get(), Arch, ObjectFileBuffers, CompilationDir,
          BinaryIDs);
      if (!ChildReadersOrErr)
        return ChildReadersOrErr.takeError();
      for (auto &Reader : ChildReadersOrErr.get())
        Readers.push_back(std::move(Reader));
    }
    if (Err)
      return std::move(Err);

    // Thin archives reference object files outside of the archive file, i.e.
    // files which reside in memory not owned by the caller. Transfer ownership
    // to the caller.
    if (Ar->isThin())
      for (auto &Buffer : Ar->takeThinBuffers())
        ObjectFileBuffers.push_back(std::move(Buffer));

    return std::move(Readers);
  }

  object::BuildIDRef BinaryID;
  auto ReaderOrErr = loadBinaryFormat(std::move(Bin), Arch, CompilationDir,
                                      BinaryIDs ? &BinaryID : nullptr);
  if (!ReaderOrErr)
    return ReaderOrErr.takeError();
  Readers.push_back(std::move(ReaderOrErr.get()));
  if (!BinaryID.empty())
    BinaryIDs->push_back(BinaryID);
  return std::move(Readers);
}

Error BinaryCoverageReader::readNextRecord(CoverageMappingRecord &Record) {
  if (CurrentRecord >= MappingRecords.size())
    return make_error<CoverageMapError>(coveragemap_error::eof);

  FunctionsFilenames.clear();
  Expressions.clear();
  MappingRegions.clear();
  auto &R = MappingRecords[CurrentRecord];
  auto F = ArrayRef(Filenames).slice(R.FilenamesBegin, R.FilenamesSize);
  RawCoverageMappingReader Reader(R.CoverageMapping, F, FunctionsFilenames,
                                  Expressions, MappingRegions);
  if (auto Err = Reader.read())
    return Err;

  Record.FunctionName = R.FunctionName;
  Record.FunctionHash = R.FunctionHash;
  Record.Filenames = FunctionsFilenames;
  Record.Expressions = Expressions;
  Record.MappingRegions = MappingRegions;

  ++CurrentRecord;
  return Error::success();
}
