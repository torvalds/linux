//===- CoverageReport.h - Code coverage report ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements rendering of a code coverage report.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_COV_COVERAGEREPORT_H
#define LLVM_COV_COVERAGEREPORT_H

#include "CoverageFilters.h"
#include "CoverageSummaryInfo.h"
#include "CoverageViewOptions.h"
#include <map>

namespace llvm {

class ThreadPoolInterface;

/// Displays the code coverage report.
class CoverageReport {
  const CoverageViewOptions &Options;
  const coverage::CoverageMapping &Coverage;

  void render(const FileCoverageSummary &File, raw_ostream &OS) const;
  void render(const FunctionCoverageSummary &Function, const DemangleCache &DC,
              raw_ostream &OS) const;

public:
  CoverageReport(const CoverageViewOptions &Options,
                 const coverage::CoverageMapping &Coverage)
      : Options(Options), Coverage(Coverage) {}

  void renderFunctionReports(ArrayRef<std::string> Files,
                             const DemangleCache &DC, raw_ostream &OS);

  /// Prepare file reports for the files specified in \p Files.
  static std::vector<FileCoverageSummary>
  prepareFileReports(const coverage::CoverageMapping &Coverage,
                     FileCoverageSummary &Totals, ArrayRef<std::string> Files,
                     const CoverageViewOptions &Options,
                     const CoverageFilter &Filters = CoverageFiltersMatchAll());

  static void
  prepareSingleFileReport(const StringRef Filename,
                          const coverage::CoverageMapping *Coverage,
                          const CoverageViewOptions &Options,
                          const unsigned LCP,
                          FileCoverageSummary *FileReport,
                          const CoverageFilter *Filters);

  /// Render file reports for every unique file in the coverage mapping.
  void renderFileReports(raw_ostream &OS,
                         const CoverageFilters &IgnoreFilenameFilters) const;

  /// Render file reports for the files specified in \p Files.
  void renderFileReports(raw_ostream &OS, ArrayRef<std::string> Files) const;

  /// Render file reports for the files specified in \p Files and the functions
  /// in \p Filters.
  void renderFileReports(raw_ostream &OS, ArrayRef<std::string> Files,
                         const CoverageFiltersMatchAll &Filters) const;

  /// Render file reports with given data.
  void renderFileReports(raw_ostream &OS,
                         const std::vector<FileCoverageSummary> &FileReports,
                         const FileCoverageSummary &Totals,
                         bool ShowEmptyFiles) const;
};

/// Prepare reports for every non-trivial directories (which have more than 1
/// source files) of the source files. This class uses template method pattern.
class DirectoryCoverageReport {
public:
  DirectoryCoverageReport(
      const CoverageViewOptions &Options,
      const coverage::CoverageMapping &Coverage,
      const CoverageFiltersMatchAll &Filters = CoverageFiltersMatchAll())
      : Options(Options), Coverage(Coverage), Filters(Filters) {}

  virtual ~DirectoryCoverageReport() = default;

  /// Prepare file reports for each directory in \p SourceFiles. The total
  /// report for all files is returned and its Name is set to the LCP of all
  /// files. The size of \p SourceFiles must be greater than 1 or else the
  /// behavior is undefined, in which case you should use
  /// CoverageReport::prepareSingleFileReport instead. If an error occurs,
  /// the recursion will stop immediately.
  Expected<FileCoverageSummary>
  prepareDirectoryReports(ArrayRef<std::string> SourceFiles);

protected:
  // These member variables below are used for avoiding being passed
  // repeatedly in recursion.
  const CoverageViewOptions &Options;
  const coverage::CoverageMapping &Coverage;
  const CoverageFiltersMatchAll &Filters;

  /// For calling CoverageReport::prepareSingleFileReport asynchronously
  /// in prepareSubDirectoryReports(). It's not intended to be modified by
  /// generateSubDirectoryReport().
  ThreadPoolInterface *TPool;

  /// One report level may correspond to multiple directory levels as we omit
  /// directories which have only one subentry. So we use this Stack to track
  /// each report level's corresponding drectory level.
  /// Each value in the stack is the LCP prefix length length of that report
  /// level. LCPStack.front() is the root LCP. Current LCP is LCPStack.back().
  SmallVector<unsigned, 32> LCPStack;

  // Use std::map to sort table rows in order.
  using SubFileReports = std::map<StringRef, FileCoverageSummary>;
  using SubDirReports =
      std::map<StringRef,
               std::pair<FileCoverageSummary, SmallVector<StringRef, 0>>>;

  /// This method is called when a report level is prepared during the
  /// recursion. \p SubFiles are the reports for those files directly in the
  /// current directory. \p SubDirs are the reports for subdirectories in
  /// current directory. \p SubTotals is the sum of all, and its name is the
  /// current LCP. Note that this method won't be called for trivial
  /// directories.
  virtual Error generateSubDirectoryReport(SubFileReports &&SubFiles,
                                           SubDirReports &&SubDirs,
                                           FileCoverageSummary &&SubTotals) = 0;

private:
  Error prepareSubDirectoryReports(const ArrayRef<StringRef> &Files,
                                   FileCoverageSummary *Totals);
};

} // end namespace llvm

#endif // LLVM_COV_COVERAGEREPORT_H
