//===- CoverageReport.cpp - Code coverage report -------------------------===//
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

#include "CoverageReport.h"
#include "RenderingSupport.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include <numeric>

using namespace llvm;

namespace {

/// Helper struct which prints trimmed and aligned columns.
struct Column {
  enum TrimKind { NoTrim, WidthTrim, RightTrim };

  enum AlignmentKind { LeftAlignment, RightAlignment };

  StringRef Str;
  unsigned Width;
  TrimKind Trim;
  AlignmentKind Alignment;

  Column(StringRef Str, unsigned Width)
      : Str(Str), Width(Width), Trim(WidthTrim), Alignment(LeftAlignment) {}

  Column &set(TrimKind Value) {
    Trim = Value;
    return *this;
  }

  Column &set(AlignmentKind Value) {
    Alignment = Value;
    return *this;
  }

  void render(raw_ostream &OS) const {
    if (Str.size() <= Width) {
      if (Alignment == RightAlignment) {
        OS.indent(Width - Str.size());
        OS << Str;
        return;
      }
      OS << Str;
      OS.indent(Width - Str.size());
      return;
    }

    switch (Trim) {
    case NoTrim:
      OS << Str;
      break;
    case WidthTrim:
      OS << Str.substr(0, Width);
      break;
    case RightTrim:
      OS << Str.substr(0, Width - 3) << "...";
      break;
    }
  }
};

raw_ostream &operator<<(raw_ostream &OS, const Column &Value) {
  Value.render(OS);
  return OS;
}

Column column(StringRef Str, unsigned Width) { return Column(Str, Width); }

template <typename T>
Column column(StringRef Str, unsigned Width, const T &Value) {
  return Column(Str, Width).set(Value);
}

// Specify the default column widths.
size_t FileReportColumns[] = {25, 12, 18, 10, 12, 18, 10, 16, 16, 10,
                              12, 18, 10, 12, 18, 10, 20, 21, 10};
size_t FunctionReportColumns[] = {25, 10, 8, 8, 10, 8, 8, 10, 8, 8, 20, 8, 8};

/// Adjust column widths to fit long file paths and function names.
void adjustColumnWidths(ArrayRef<StringRef> Files,
                        ArrayRef<StringRef> Functions) {
  for (StringRef Filename : Files)
    FileReportColumns[0] = std::max(FileReportColumns[0], Filename.size());
  for (StringRef Funcname : Functions)
    FunctionReportColumns[0] =
        std::max(FunctionReportColumns[0], Funcname.size());
}

/// Prints a horizontal divider long enough to cover the given column
/// widths.
void renderDivider(raw_ostream &OS, const CoverageViewOptions &Options, bool isFileReport) {
  size_t Length;
  if (isFileReport) {
    Length = std::accumulate(std::begin(FileReportColumns), std::end(FileReportColumns), 0);
    if (!Options.ShowRegionSummary)
      Length -= (FileReportColumns[1] + FileReportColumns[2] + FileReportColumns[3]);
    if (!Options.ShowInstantiationSummary)
      Length -= (FileReportColumns[7] + FileReportColumns[8] + FileReportColumns[9]);
    if (!Options.ShowBranchSummary)
      Length -= (FileReportColumns[13] + FileReportColumns[14] + FileReportColumns[15]);
    if (!Options.ShowMCDCSummary)
      Length -= (FileReportColumns[16] + FileReportColumns[17] + FileReportColumns[18]);
  } else {
    Length = std::accumulate(std::begin(FunctionReportColumns), std::end(FunctionReportColumns), 0);
    if (!Options.ShowBranchSummary)
      Length -= (FunctionReportColumns[7] + FunctionReportColumns[8] + FunctionReportColumns[9]);
    if (!Options.ShowMCDCSummary)
      Length -= (FunctionReportColumns[10] + FunctionReportColumns[11] + FunctionReportColumns[12]);
  }
  for (size_t I = 0; I < Length; ++I)
    OS << '-';
}

/// Return the color which correponds to the coverage percentage of a
/// certain metric.
template <typename T>
raw_ostream::Colors determineCoveragePercentageColor(const T &Info) {
  if (Info.isFullyCovered())
    return raw_ostream::GREEN;
  return Info.getPercentCovered() >= 80.0 ? raw_ostream::YELLOW
                                          : raw_ostream::RED;
}

/// Get the number of redundant path components in each path in \p Paths.
unsigned getNumRedundantPathComponents(ArrayRef<std::string> Paths) {
  // To start, set the number of redundant path components to the maximum
  // possible value.
  SmallVector<StringRef, 8> FirstPathComponents{sys::path::begin(Paths[0]),
                                                sys::path::end(Paths[0])};
  unsigned NumRedundant = FirstPathComponents.size();

  for (unsigned I = 1, E = Paths.size(); NumRedundant > 0 && I < E; ++I) {
    StringRef Path = Paths[I];
    for (const auto &Component :
         enumerate(make_range(sys::path::begin(Path), sys::path::end(Path)))) {
      // Do not increase the number of redundant components: that would remove
      // useful parts of already-visited paths.
      if (Component.index() >= NumRedundant)
        break;

      // Lower the number of redundant components when there's a mismatch
      // between the first path, and the path under consideration.
      if (FirstPathComponents[Component.index()] != Component.value()) {
        NumRedundant = Component.index();
        break;
      }
    }
  }

  return NumRedundant;
}

/// Determine the length of the longest redundant prefix of the paths in
/// \p Paths.
unsigned getRedundantPrefixLen(ArrayRef<std::string> Paths) {
  // If there's at most one path, no path components are redundant.
  if (Paths.size() <= 1)
    return 0;

  unsigned PrefixLen = 0;
  unsigned NumRedundant = getNumRedundantPathComponents(Paths);
  auto Component = sys::path::begin(Paths[0]);
  for (unsigned I = 0; I < NumRedundant; ++I) {
    auto LastComponent = Component;
    ++Component;
    PrefixLen += Component - LastComponent;
  }
  return PrefixLen;
}

/// Determine the length of the longest redundant prefix of the substrs starts
/// from \p LCP in \p Paths. \p Paths can't be empty. If there's only one
/// element in \p Paths, the length of the substr is returned. Note this is
/// differnet from the behavior of the function above.
unsigned getRedundantPrefixLen(ArrayRef<StringRef> Paths, unsigned LCP) {
  assert(!Paths.empty() && "Paths must have at least one element");

  auto Iter = Paths.begin();
  auto IterE = Paths.end();
  auto Prefix = Iter->substr(LCP);
  while (++Iter != IterE) {
    auto Other = Iter->substr(LCP);
    auto Len = std::min(Prefix.size(), Other.size());
    for (std::size_t I = 0; I < Len; ++I) {
      if (Prefix[I] != Other[I]) {
        Prefix = Prefix.substr(0, I);
        break;
      }
    }
  }

  for (auto I = Prefix.size(); --I != SIZE_MAX;) {
    if (Prefix[I] == '/' || Prefix[I] == '\\')
      return I + 1;
  }

  return Prefix.size();
}

} // end anonymous namespace

namespace llvm {

void CoverageReport::render(const FileCoverageSummary &File,
                            raw_ostream &OS) const {
  auto FileCoverageColor =
      determineCoveragePercentageColor(File.RegionCoverage);
  auto FuncCoverageColor =
      determineCoveragePercentageColor(File.FunctionCoverage);
  auto InstantiationCoverageColor =
      determineCoveragePercentageColor(File.InstantiationCoverage);
  auto LineCoverageColor = determineCoveragePercentageColor(File.LineCoverage);
  SmallString<256> FileName = File.Name;
  sys::path::native(FileName);

  // remove_dots will remove trailing slash, so we need to check before it.
  auto IsDir = FileName.ends_with(sys::path::get_separator());
  sys::path::remove_dots(FileName, /*remove_dot_dot=*/true);
  if (IsDir)
    FileName += sys::path::get_separator();

  OS << column(FileName, FileReportColumns[0], Column::NoTrim);

  if (Options.ShowRegionSummary) {
    OS << format("%*u", FileReportColumns[1],
                 (unsigned)File.RegionCoverage.getNumRegions());
    Options.colored_ostream(OS, FileCoverageColor)
        << format("%*u", FileReportColumns[2],
                  (unsigned)(File.RegionCoverage.getNumRegions() -
                             File.RegionCoverage.getCovered()));
    if (File.RegionCoverage.getNumRegions())
      Options.colored_ostream(OS, FileCoverageColor)
          << format("%*.2f", FileReportColumns[3] - 1,
                    File.RegionCoverage.getPercentCovered())
          << '%';
    else
      OS << column("-", FileReportColumns[3], Column::RightAlignment);
  }

  OS << format("%*u", FileReportColumns[4],
               (unsigned)File.FunctionCoverage.getNumFunctions());
  OS << format("%*u", FileReportColumns[5],
               (unsigned)(File.FunctionCoverage.getNumFunctions() -
                          File.FunctionCoverage.getExecuted()));
  if (File.FunctionCoverage.getNumFunctions())
    Options.colored_ostream(OS, FuncCoverageColor)
        << format("%*.2f", FileReportColumns[6] - 1,
                  File.FunctionCoverage.getPercentCovered())
        << '%';
  else
    OS << column("-", FileReportColumns[6], Column::RightAlignment);

  if (Options.ShowInstantiationSummary) {
    OS << format("%*u", FileReportColumns[7],
                 (unsigned)File.InstantiationCoverage.getNumFunctions());
    OS << format("%*u", FileReportColumns[8],
                 (unsigned)(File.InstantiationCoverage.getNumFunctions() -
                            File.InstantiationCoverage.getExecuted()));
    if (File.InstantiationCoverage.getNumFunctions())
      Options.colored_ostream(OS, InstantiationCoverageColor)
          << format("%*.2f", FileReportColumns[9] - 1,
                    File.InstantiationCoverage.getPercentCovered())
          << '%';
    else
      OS << column("-", FileReportColumns[9], Column::RightAlignment);
  }

  OS << format("%*u", FileReportColumns[10],
               (unsigned)File.LineCoverage.getNumLines());
  Options.colored_ostream(OS, LineCoverageColor) << format(
      "%*u", FileReportColumns[11], (unsigned)(File.LineCoverage.getNumLines() -
                                               File.LineCoverage.getCovered()));
  if (File.LineCoverage.getNumLines())
    Options.colored_ostream(OS, LineCoverageColor)
        << format("%*.2f", FileReportColumns[12] - 1,
                  File.LineCoverage.getPercentCovered())
        << '%';
  else
    OS << column("-", FileReportColumns[12], Column::RightAlignment);

  if (Options.ShowBranchSummary) {
    OS << format("%*u", FileReportColumns[13],
                 (unsigned)File.BranchCoverage.getNumBranches());
    Options.colored_ostream(OS, LineCoverageColor)
        << format("%*u", FileReportColumns[14],
                  (unsigned)(File.BranchCoverage.getNumBranches() -
                             File.BranchCoverage.getCovered()));
    if (File.BranchCoverage.getNumBranches())
      Options.colored_ostream(OS, LineCoverageColor)
          << format("%*.2f", FileReportColumns[15] - 1,
                    File.BranchCoverage.getPercentCovered())
          << '%';
    else
      OS << column("-", FileReportColumns[15], Column::RightAlignment);
  }

  if (Options.ShowMCDCSummary) {
    OS << format("%*u", FileReportColumns[16],
                 (unsigned)File.MCDCCoverage.getNumPairs());
    Options.colored_ostream(OS, LineCoverageColor)
        << format("%*u", FileReportColumns[17],
                  (unsigned)(File.MCDCCoverage.getNumPairs() -
                             File.MCDCCoverage.getCoveredPairs()));
    if (File.MCDCCoverage.getNumPairs())
      Options.colored_ostream(OS, LineCoverageColor)
          << format("%*.2f", FileReportColumns[18] - 1,
                    File.MCDCCoverage.getPercentCovered())
          << '%';
    else
      OS << column("-", FileReportColumns[18], Column::RightAlignment);
  }

  OS << "\n";
}

void CoverageReport::render(const FunctionCoverageSummary &Function,
                            const DemangleCache &DC,
                            raw_ostream &OS) const {
  auto FuncCoverageColor =
      determineCoveragePercentageColor(Function.RegionCoverage);
  auto LineCoverageColor =
      determineCoveragePercentageColor(Function.LineCoverage);
  OS << column(DC.demangle(Function.Name), FunctionReportColumns[0],
               Column::RightTrim)
     << format("%*u", FunctionReportColumns[1],
               (unsigned)Function.RegionCoverage.getNumRegions());
  Options.colored_ostream(OS, FuncCoverageColor)
      << format("%*u", FunctionReportColumns[2],
                (unsigned)(Function.RegionCoverage.getNumRegions() -
                           Function.RegionCoverage.getCovered()));
  Options.colored_ostream(
      OS, determineCoveragePercentageColor(Function.RegionCoverage))
      << format("%*.2f", FunctionReportColumns[3] - 1,
                Function.RegionCoverage.getPercentCovered())
      << '%';
  OS << format("%*u", FunctionReportColumns[4],
               (unsigned)Function.LineCoverage.getNumLines());
  Options.colored_ostream(OS, LineCoverageColor)
      << format("%*u", FunctionReportColumns[5],
                (unsigned)(Function.LineCoverage.getNumLines() -
                           Function.LineCoverage.getCovered()));
  Options.colored_ostream(
      OS, determineCoveragePercentageColor(Function.LineCoverage))
      << format("%*.2f", FunctionReportColumns[6] - 1,
                Function.LineCoverage.getPercentCovered())
      << '%';
  if (Options.ShowBranchSummary) {
    OS << format("%*u", FunctionReportColumns[7],
                 (unsigned)Function.BranchCoverage.getNumBranches());
    Options.colored_ostream(OS, LineCoverageColor)
        << format("%*u", FunctionReportColumns[8],
                  (unsigned)(Function.BranchCoverage.getNumBranches() -
                             Function.BranchCoverage.getCovered()));
    Options.colored_ostream(
        OS, determineCoveragePercentageColor(Function.BranchCoverage))
        << format("%*.2f", FunctionReportColumns[9] - 1,
                  Function.BranchCoverage.getPercentCovered())
        << '%';
  }
  if (Options.ShowMCDCSummary) {
    OS << format("%*u", FunctionReportColumns[10],
                 (unsigned)Function.MCDCCoverage.getNumPairs());
    Options.colored_ostream(OS, LineCoverageColor)
        << format("%*u", FunctionReportColumns[11],
                  (unsigned)(Function.MCDCCoverage.getNumPairs() -
                             Function.MCDCCoverage.getCoveredPairs()));
    Options.colored_ostream(
        OS, determineCoveragePercentageColor(Function.MCDCCoverage))
        << format("%*.2f", FunctionReportColumns[12] - 1,
                  Function.MCDCCoverage.getPercentCovered())
        << '%';
  }
  OS << "\n";
}

void CoverageReport::renderFunctionReports(ArrayRef<std::string> Files,
                                           const DemangleCache &DC,
                                           raw_ostream &OS) {
  bool isFirst = true;
  for (StringRef Filename : Files) {
    auto Functions = Coverage.getCoveredFunctions(Filename);

    if (isFirst)
      isFirst = false;
    else
      OS << "\n";

    std::vector<StringRef> Funcnames;
    for (const auto &F : Functions)
      Funcnames.emplace_back(DC.demangle(F.Name));
    adjustColumnWidths({}, Funcnames);

    OS << "File '" << Filename << "':\n";
    OS << column("Name", FunctionReportColumns[0])
       << column("Regions", FunctionReportColumns[1], Column::RightAlignment)
       << column("Miss", FunctionReportColumns[2], Column::RightAlignment)
       << column("Cover", FunctionReportColumns[3], Column::RightAlignment)
       << column("Lines", FunctionReportColumns[4], Column::RightAlignment)
       << column("Miss", FunctionReportColumns[5], Column::RightAlignment)
       << column("Cover", FunctionReportColumns[6], Column::RightAlignment);
    if (Options.ShowBranchSummary)
      OS << column("Branches", FunctionReportColumns[7], Column::RightAlignment)
         << column("Miss", FunctionReportColumns[8], Column::RightAlignment)
         << column("Cover", FunctionReportColumns[9], Column::RightAlignment);
    if (Options.ShowMCDCSummary)
      OS << column("MC/DC Conditions", FunctionReportColumns[10],
                   Column::RightAlignment)
         << column("Miss", FunctionReportColumns[11], Column::RightAlignment)
         << column("Cover", FunctionReportColumns[12], Column::RightAlignment);
    OS << "\n";
    renderDivider(OS, Options, false);
    OS << "\n";
    FunctionCoverageSummary Totals("TOTAL");
    for (const auto &F : Functions) {
      auto Function = FunctionCoverageSummary::get(Coverage, F);
      ++Totals.ExecutionCount;
      Totals.RegionCoverage += Function.RegionCoverage;
      Totals.LineCoverage += Function.LineCoverage;
      Totals.BranchCoverage += Function.BranchCoverage;
      Totals.MCDCCoverage += Function.MCDCCoverage;
      render(Function, DC, OS);
    }
    if (Totals.ExecutionCount) {
      renderDivider(OS, Options, false);
      OS << "\n";
      render(Totals, DC, OS);
    }
  }
}

void CoverageReport::prepareSingleFileReport(const StringRef Filename,
    const coverage::CoverageMapping *Coverage,
    const CoverageViewOptions &Options, const unsigned LCP,
    FileCoverageSummary *FileReport, const CoverageFilter *Filters) {
  for (const auto &Group : Coverage->getInstantiationGroups(Filename)) {
    std::vector<FunctionCoverageSummary> InstantiationSummaries;
    for (const coverage::FunctionRecord *F : Group.getInstantiations()) {
      if (!Filters->matches(*Coverage, *F))
        continue;
      auto InstantiationSummary = FunctionCoverageSummary::get(*Coverage, *F);
      FileReport->addInstantiation(InstantiationSummary);
      InstantiationSummaries.push_back(InstantiationSummary);
    }
    if (InstantiationSummaries.empty())
      continue;

    auto GroupSummary =
        FunctionCoverageSummary::get(Group, InstantiationSummaries);

    if (Options.Debug)
      outs() << "InstantiationGroup: " << GroupSummary.Name << " with "
             << "size = " << Group.size() << "\n";

    FileReport->addFunction(GroupSummary);
  }
}

std::vector<FileCoverageSummary> CoverageReport::prepareFileReports(
    const coverage::CoverageMapping &Coverage, FileCoverageSummary &Totals,
    ArrayRef<std::string> Files, const CoverageViewOptions &Options,
    const CoverageFilter &Filters) {
  unsigned LCP = getRedundantPrefixLen(Files);

  ThreadPoolStrategy S = hardware_concurrency(Options.NumThreads);
  if (Options.NumThreads == 0) {
    // If NumThreads is not specified, create one thread for each input, up to
    // the number of hardware cores.
    S = heavyweight_hardware_concurrency(Files.size());
    S.Limit = true;
  }
  DefaultThreadPool Pool(S);

  std::vector<FileCoverageSummary> FileReports;
  FileReports.reserve(Files.size());

  for (StringRef Filename : Files) {
    FileReports.emplace_back(Filename.drop_front(LCP));
    Pool.async(&CoverageReport::prepareSingleFileReport, Filename,
               &Coverage, Options, LCP, &FileReports.back(), &Filters);
  }
  Pool.wait();

  for (const auto &FileReport : FileReports)
    Totals += FileReport;

  return FileReports;
}

void CoverageReport::renderFileReports(
    raw_ostream &OS, const CoverageFilters &IgnoreFilenameFilters) const {
  std::vector<std::string> UniqueSourceFiles;
  for (StringRef SF : Coverage.getUniqueSourceFiles()) {
    // Apply ignore source files filters.
    if (!IgnoreFilenameFilters.matchesFilename(SF))
      UniqueSourceFiles.emplace_back(SF.str());
  }
  renderFileReports(OS, UniqueSourceFiles);
}

void CoverageReport::renderFileReports(
    raw_ostream &OS, ArrayRef<std::string> Files) const {
  renderFileReports(OS, Files, CoverageFiltersMatchAll());
}

void CoverageReport::renderFileReports(
    raw_ostream &OS, ArrayRef<std::string> Files,
    const CoverageFiltersMatchAll &Filters) const {
  FileCoverageSummary Totals("TOTAL");
  auto FileReports =
      prepareFileReports(Coverage, Totals, Files, Options, Filters);
  renderFileReports(OS, FileReports, Totals, Filters.empty());
}

void CoverageReport::renderFileReports(
    raw_ostream &OS, const std::vector<FileCoverageSummary> &FileReports,
    const FileCoverageSummary &Totals, bool ShowEmptyFiles) const {
  std::vector<StringRef> Filenames;
  Filenames.reserve(FileReports.size());
  for (const FileCoverageSummary &FCS : FileReports)
    Filenames.emplace_back(FCS.Name);
  adjustColumnWidths(Filenames, {});

  OS << column("Filename", FileReportColumns[0]);
  if (Options.ShowRegionSummary)
    OS << column("Regions", FileReportColumns[1], Column::RightAlignment)
       << column("Missed Regions", FileReportColumns[2], Column::RightAlignment)
       << column("Cover", FileReportColumns[3], Column::RightAlignment);
  OS << column("Functions", FileReportColumns[4], Column::RightAlignment)
     << column("Missed Functions", FileReportColumns[5], Column::RightAlignment)
     << column("Executed", FileReportColumns[6], Column::RightAlignment);
  if (Options.ShowInstantiationSummary)
    OS << column("Instantiations", FileReportColumns[7], Column::RightAlignment)
       << column("Missed Insts.", FileReportColumns[8], Column::RightAlignment)
       << column("Executed", FileReportColumns[9], Column::RightAlignment);
  OS << column("Lines", FileReportColumns[10], Column::RightAlignment)
     << column("Missed Lines", FileReportColumns[11], Column::RightAlignment)
     << column("Cover", FileReportColumns[12], Column::RightAlignment);
  if (Options.ShowBranchSummary)
    OS << column("Branches", FileReportColumns[13], Column::RightAlignment)
       << column("Missed Branches", FileReportColumns[14],
                 Column::RightAlignment)
       << column("Cover", FileReportColumns[15], Column::RightAlignment);
  if (Options.ShowMCDCSummary)
    OS << column("MC/DC Conditions", FileReportColumns[16],
                 Column::RightAlignment)
       << column("Missed Conditions", FileReportColumns[17],
                 Column::RightAlignment)
       << column("Cover", FileReportColumns[18], Column::RightAlignment);
  OS << "\n";
  renderDivider(OS, Options, true);
  OS << "\n";

  std::vector<const FileCoverageSummary *> EmptyFiles;
  for (const FileCoverageSummary &FCS : FileReports) {
    if (FCS.FunctionCoverage.getNumFunctions())
      render(FCS, OS);
    else
      EmptyFiles.push_back(&FCS);
  }

  if (!EmptyFiles.empty() && ShowEmptyFiles) {
    OS << "\n"
       << "Files which contain no functions:\n";

    for (auto FCS : EmptyFiles)
      render(*FCS, OS);
  }

  renderDivider(OS, Options, true);
  OS << "\n";
  render(Totals, OS);
}

Expected<FileCoverageSummary> DirectoryCoverageReport::prepareDirectoryReports(
    ArrayRef<std::string> SourceFiles) {
  std::vector<StringRef> Files(SourceFiles.begin(), SourceFiles.end());

  unsigned RootLCP = getRedundantPrefixLen(Files, 0);
  auto LCPath = Files.front().substr(0, RootLCP);

  ThreadPoolStrategy PoolS = hardware_concurrency(Options.NumThreads);
  if (Options.NumThreads == 0) {
    PoolS = heavyweight_hardware_concurrency(Files.size());
    PoolS.Limit = true;
  }
  DefaultThreadPool Pool(PoolS);

  TPool = &Pool;
  LCPStack = {RootLCP};
  FileCoverageSummary RootTotals(LCPath);
  if (auto E = prepareSubDirectoryReports(Files, &RootTotals))
    return {std::move(E)};
  return {std::move(RootTotals)};
}

/// Filter out files in LCPStack.back(), group others by subdirectory name
/// and recurse on them. After returning from all subdirectories, call
/// generateSubDirectoryReport(). \p Files must be non-empty. The
/// FileCoverageSummary of this directory will be added to \p Totals.
Error DirectoryCoverageReport::prepareSubDirectoryReports(
    const ArrayRef<StringRef> &Files, FileCoverageSummary *Totals) {
  assert(!Files.empty() && "Files must have at least one element");

  auto LCP = LCPStack.back();
  auto LCPath = Files.front().substr(0, LCP).str();

  // Use ordered map to keep entries in order.
  SubFileReports SubFiles;
  SubDirReports SubDirs;
  for (auto &&File : Files) {
    auto SubPath = File.substr(LCPath.size());
    SmallVector<char, 128> NativeSubPath;
    sys::path::native(SubPath, NativeSubPath);
    StringRef NativeSubPathRef(NativeSubPath.data(), NativeSubPath.size());

    auto I = sys::path::begin(NativeSubPathRef);
    auto E = sys::path::end(NativeSubPathRef);
    assert(I != E && "Such case should have been filtered out in the caller");

    auto Name = SubPath.substr(0, I->size());
    if (++I == E) {
      auto Iter = SubFiles.insert_or_assign(Name, SubPath).first;
      // Makes files reporting overlap with subdir reporting.
      TPool->async(&CoverageReport::prepareSingleFileReport, File, &Coverage,
                   Options, LCP, &Iter->second, &Filters);
    } else {
      SubDirs[Name].second.push_back(File);
    }
  }

  // Call recursively on subdirectories.
  for (auto &&KV : SubDirs) {
    auto &V = KV.second;
    if (V.second.size() == 1) {
      // If there's only one file in that subdirectory, we don't bother to
      // recurse on it further.
      V.first.Name = V.second.front().substr(LCP);
      TPool->async(&CoverageReport::prepareSingleFileReport, V.second.front(),
                   &Coverage, Options, LCP, &V.first, &Filters);
    } else {
      auto SubDirLCP = getRedundantPrefixLen(V.second, LCP);
      V.first.Name = V.second.front().substr(LCP, SubDirLCP);
      LCPStack.push_back(LCP + SubDirLCP);
      if (auto E = prepareSubDirectoryReports(V.second, &V.first))
        return E;
    }
  }

  TPool->wait();

  FileCoverageSummary CurrentTotals(LCPath);
  for (auto &&KV : SubFiles)
    CurrentTotals += KV.second;
  for (auto &&KV : SubDirs)
    CurrentTotals += KV.second.first;
  *Totals += CurrentTotals;

  if (auto E = generateSubDirectoryReport(
          std::move(SubFiles), std::move(SubDirs), std::move(CurrentTotals)))
    return E;

  LCPStack.pop_back();
  return Error::success();
}

} // end namespace llvm
