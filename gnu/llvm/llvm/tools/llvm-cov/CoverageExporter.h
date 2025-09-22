//===- CoverageExporter.h - Code coverage exporter ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class defines a code coverage exporter interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_COV_COVERAGEEXPORTER_H
#define LLVM_COV_COVERAGEEXPORTER_H

#include "CoverageFilters.h"
#include "CoverageSummaryInfo.h"
#include "CoverageViewOptions.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"

namespace llvm {

/// Exports the code coverage information.
class CoverageExporter {
protected:
  /// The full CoverageMapping object to export.
  const coverage::CoverageMapping &Coverage;

  /// The options passed to the tool.
  const CoverageViewOptions &Options;

  /// Output stream to print to.
  raw_ostream &OS;

  CoverageExporter(const coverage::CoverageMapping &CoverageMapping,
                   const CoverageViewOptions &Options, raw_ostream &OS)
      : Coverage(CoverageMapping), Options(Options), OS(OS) {}

public:
  virtual ~CoverageExporter(){};

  /// Render the CoverageMapping object.
  virtual void renderRoot(const CoverageFilters &IgnoreFilters) = 0;

  /// Render the CoverageMapping object for specified source files.
  virtual void renderRoot(ArrayRef<std::string> SourceFiles) = 0;
};

} // end namespace llvm

#endif // LLVM_COV_COVERAGEEXPORTER_H
