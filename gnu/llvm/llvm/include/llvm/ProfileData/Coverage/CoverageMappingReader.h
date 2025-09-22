//===- CoverageMappingReader.h - Code coverage mapping reader ---*- C++ -*-===//
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

#ifndef LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H
#define LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

namespace llvm {
namespace coverage {

class CoverageMappingReader;

/// Coverage mapping information for a single function.
struct CoverageMappingRecord {
  StringRef FunctionName;
  uint64_t FunctionHash;
  ArrayRef<StringRef> Filenames;
  ArrayRef<CounterExpression> Expressions;
  ArrayRef<CounterMappingRegion> MappingRegions;
};

/// A file format agnostic iterator over coverage mapping data.
class CoverageMappingIterator {
  CoverageMappingReader *Reader;
  CoverageMappingRecord Record;
  coveragemap_error ReadErr;

  void increment();

public:
  using iterator_category = std::input_iterator_tag;
  using value_type = CoverageMappingRecord;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

  CoverageMappingIterator()
      : Reader(nullptr), ReadErr(coveragemap_error::success) {}

  CoverageMappingIterator(CoverageMappingReader *Reader)
      : Reader(Reader), ReadErr(coveragemap_error::success) {
    increment();
  }

  ~CoverageMappingIterator() {
    if (ReadErr != coveragemap_error::success)
      llvm_unreachable("Unexpected error in coverage mapping iterator");
  }

  CoverageMappingIterator &operator++() {
    increment();
    return *this;
  }
  bool operator==(const CoverageMappingIterator &RHS) const {
    return Reader == RHS.Reader;
  }
  bool operator!=(const CoverageMappingIterator &RHS) const {
    return Reader != RHS.Reader;
  }
  Expected<CoverageMappingRecord &> operator*() {
    if (ReadErr != coveragemap_error::success) {
      auto E = make_error<CoverageMapError>(ReadErr);
      ReadErr = coveragemap_error::success;
      return std::move(E);
    }
    return Record;
  }
  Expected<CoverageMappingRecord *> operator->() {
    if (ReadErr != coveragemap_error::success) {
      auto E = make_error<CoverageMapError>(ReadErr);
      ReadErr = coveragemap_error::success;
      return std::move(E);
    }
    return &Record;
  }
};

class CoverageMappingReader {
public:
  virtual ~CoverageMappingReader() = default;

  virtual Error readNextRecord(CoverageMappingRecord &Record) = 0;
  CoverageMappingIterator begin() { return CoverageMappingIterator(this); }
  CoverageMappingIterator end() { return CoverageMappingIterator(); }
};

/// Base class for the raw coverage mapping and filenames data readers.
class RawCoverageReader {
protected:
  StringRef Data;

  RawCoverageReader(StringRef Data) : Data(Data) {}

  Error readULEB128(uint64_t &Result);
  Error readIntMax(uint64_t &Result, uint64_t MaxPlus1);
  Error readSize(uint64_t &Result);
  Error readString(StringRef &Result);
};

/// Checks if the given coverage mapping data is exported for
/// an unused function.
class RawCoverageMappingDummyChecker : public RawCoverageReader {
public:
  RawCoverageMappingDummyChecker(StringRef MappingData)
      : RawCoverageReader(MappingData) {}

  Expected<bool> isDummy();
};

/// Reader for the raw coverage mapping data.
class RawCoverageMappingReader : public RawCoverageReader {
  ArrayRef<std::string> &TranslationUnitFilenames;
  std::vector<StringRef> &Filenames;
  std::vector<CounterExpression> &Expressions;
  std::vector<CounterMappingRegion> &MappingRegions;

public:
  RawCoverageMappingReader(StringRef MappingData,
                           ArrayRef<std::string> &TranslationUnitFilenames,
                           std::vector<StringRef> &Filenames,
                           std::vector<CounterExpression> &Expressions,
                           std::vector<CounterMappingRegion> &MappingRegions)
      : RawCoverageReader(MappingData),
        TranslationUnitFilenames(TranslationUnitFilenames),
        Filenames(Filenames), Expressions(Expressions),
        MappingRegions(MappingRegions) {}
  RawCoverageMappingReader(const RawCoverageMappingReader &) = delete;
  RawCoverageMappingReader &
  operator=(const RawCoverageMappingReader &) = delete;

  Error read();

private:
  Error decodeCounter(unsigned Value, Counter &C);
  Error readCounter(Counter &C);
  Error
  readMappingRegionsSubArray(std::vector<CounterMappingRegion> &MappingRegions,
                             unsigned InferredFileID, size_t NumFileIDs);
};

/// Reader for the coverage mapping data that is emitted by the
/// frontend and stored in an object file.
class BinaryCoverageReader : public CoverageMappingReader {
public:
  struct ProfileMappingRecord {
    CovMapVersion Version;
    StringRef FunctionName;
    uint64_t FunctionHash;
    StringRef CoverageMapping;
    size_t FilenamesBegin;
    size_t FilenamesSize;

    ProfileMappingRecord(CovMapVersion Version, StringRef FunctionName,
                         uint64_t FunctionHash, StringRef CoverageMapping,
                         size_t FilenamesBegin, size_t FilenamesSize)
        : Version(Version), FunctionName(FunctionName),
          FunctionHash(FunctionHash), CoverageMapping(CoverageMapping),
          FilenamesBegin(FilenamesBegin), FilenamesSize(FilenamesSize) {}
  };

  using FuncRecordsStorage = std::unique_ptr<MemoryBuffer>;

private:
  std::vector<std::string> Filenames;
  std::vector<ProfileMappingRecord> MappingRecords;
  std::unique_ptr<InstrProfSymtab> ProfileNames;
  size_t CurrentRecord = 0;
  std::vector<StringRef> FunctionsFilenames;
  std::vector<CounterExpression> Expressions;
  std::vector<CounterMappingRegion> MappingRegions;

  // Used to tie the lifetimes of coverage function records to the lifetime of
  // this BinaryCoverageReader instance. Needed to support the format change in
  // D69471, which can split up function records into multiple sections on ELF.
  FuncRecordsStorage FuncRecords;

  BinaryCoverageReader(std::unique_ptr<InstrProfSymtab> Symtab,
                       FuncRecordsStorage &&FuncRecords)
      : ProfileNames(std::move(Symtab)), FuncRecords(std::move(FuncRecords)) {}

public:
  BinaryCoverageReader(const BinaryCoverageReader &) = delete;
  BinaryCoverageReader &operator=(const BinaryCoverageReader &) = delete;

  static Expected<std::vector<std::unique_ptr<BinaryCoverageReader>>>
  create(MemoryBufferRef ObjectBuffer, StringRef Arch,
         SmallVectorImpl<std::unique_ptr<MemoryBuffer>> &ObjectFileBuffers,
         StringRef CompilationDir = "",
         SmallVectorImpl<object::BuildIDRef> *BinaryIDs = nullptr);

  static Expected<std::unique_ptr<BinaryCoverageReader>>
  createCoverageReaderFromBuffer(
      StringRef Coverage, FuncRecordsStorage &&FuncRecords,
      std::unique_ptr<InstrProfSymtab> ProfileNamesPtr, uint8_t BytesInAddress,
      llvm::endianness Endian, StringRef CompilationDir = "");

  Error readNextRecord(CoverageMappingRecord &Record) override;
};

/// Reader for the raw coverage filenames.
class RawCoverageFilenamesReader : public RawCoverageReader {
  std::vector<std::string> &Filenames;
  StringRef CompilationDir;

  // Read an uncompressed sequence of filenames.
  Error readUncompressed(CovMapVersion Version, uint64_t NumFilenames);

public:
  RawCoverageFilenamesReader(StringRef Data,
                             std::vector<std::string> &Filenames,
                             StringRef CompilationDir = "")
      : RawCoverageReader(Data), Filenames(Filenames),
        CompilationDir(CompilationDir) {}
  RawCoverageFilenamesReader(const RawCoverageFilenamesReader &) = delete;
  RawCoverageFilenamesReader &
  operator=(const RawCoverageFilenamesReader &) = delete;

  Error read(CovMapVersion Version);
};

} // end namespace coverage
} // end namespace llvm

#endif // LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H
