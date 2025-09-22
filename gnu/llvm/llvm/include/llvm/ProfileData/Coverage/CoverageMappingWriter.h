//===- CoverageMappingWriter.h - Code coverage mapping writer ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing coverage mapping data for
// instrumentation based coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGWRITER_H
#define LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGWRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"

namespace llvm {

class raw_ostream;

namespace coverage {

/// Writer of the filenames section for the instrumentation
/// based code coverage.
class CoverageFilenamesSectionWriter {
  ArrayRef<std::string> Filenames;

public:
  CoverageFilenamesSectionWriter(ArrayRef<std::string> Filenames);

  /// Write encoded filenames to the given output stream. If \p Compress is
  /// true, attempt to compress the filenames.
  void write(raw_ostream &OS, bool Compress = true);
};

/// Writer for instrumentation based coverage mapping data.
class CoverageMappingWriter {
  ArrayRef<unsigned> VirtualFileMapping;
  ArrayRef<CounterExpression> Expressions;
  MutableArrayRef<CounterMappingRegion> MappingRegions;

public:
  CoverageMappingWriter(ArrayRef<unsigned> VirtualFileMapping,
                        ArrayRef<CounterExpression> Expressions,
                        MutableArrayRef<CounterMappingRegion> MappingRegions)
      : VirtualFileMapping(VirtualFileMapping), Expressions(Expressions),
        MappingRegions(MappingRegions) {}

  /// Write encoded coverage mapping data to the given output stream.
  void write(raw_ostream &OS);
};

/// Writer for the coverage mapping testing format.
class TestingFormatWriter {
  uint64_t ProfileNamesAddr;
  StringRef ProfileNamesData;
  StringRef CoverageMappingData;
  StringRef CoverageRecordsData;

public:
  TestingFormatWriter(uint64_t ProfileNamesAddr, StringRef ProfileNamesData,
                      StringRef CoverageMappingData,
                      StringRef CoverageRecordsData)
      : ProfileNamesAddr(ProfileNamesAddr), ProfileNamesData(ProfileNamesData),
        CoverageMappingData(CoverageMappingData),
        CoverageRecordsData(CoverageRecordsData) {}

  /// Encode to the given output stream.
  void
  write(raw_ostream &OS,
        TestingFormatVersion Version = TestingFormatVersion::CurrentVersion);
};

} // end namespace coverage

} // end namespace llvm

#endif // LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGWRITER_H
