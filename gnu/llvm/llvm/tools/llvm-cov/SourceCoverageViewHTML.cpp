//===- SourceCoverageViewHTML.cpp - A html code coverage view -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file implements the html coverage renderer.
///
//===----------------------------------------------------------------------===//

#include "SourceCoverageViewHTML.h"
#include "CoverageReport.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include <optional>

using namespace llvm;

namespace {

// Return a string with the special characters in \p Str escaped.
std::string escape(StringRef Str, const CoverageViewOptions &Opts) {
  std::string TabExpandedResult;
  unsigned ColNum = 0; // Record the column number.
  for (char C : Str) {
    if (C == '\t') {
      // Replace '\t' with up to TabSize spaces.
      unsigned NumSpaces = Opts.TabSize - (ColNum % Opts.TabSize);
      TabExpandedResult.append(NumSpaces, ' ');
      ColNum += NumSpaces;
    } else {
      TabExpandedResult += C;
      if (C == '\n' || C == '\r')
        ColNum = 0;
      else
        ++ColNum;
    }
  }
  std::string EscapedHTML;
  {
    raw_string_ostream OS{EscapedHTML};
    printHTMLEscaped(TabExpandedResult, OS);
  }
  return EscapedHTML;
}

// Create a \p Name tag around \p Str, and optionally set its \p ClassName.
std::string tag(StringRef Name, StringRef Str, StringRef ClassName = "") {
  std::string Tag = "<";
  Tag += Name;
  if (!ClassName.empty()) {
    Tag += " class='";
    Tag += ClassName;
    Tag += "'";
  }
  Tag += ">";
  Tag += Str;
  Tag += "</";
  Tag += Name;
  Tag += ">";
  return Tag;
}

// Create an anchor to \p Link with the label \p Str.
std::string a(StringRef Link, StringRef Str, StringRef TargetName = "") {
  std::string Tag;
  Tag += "<a ";
  if (!TargetName.empty()) {
    Tag += "name='";
    Tag += TargetName;
    Tag += "' ";
  }
  Tag += "href='";
  Tag += Link;
  Tag += "'>";
  Tag += Str;
  Tag += "</a>";
  return Tag;
}

const char *BeginHeader =
    "<head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'>";

const char *JSForCoverage =
    R"javascript(

function next_uncovered(selector, reverse, scroll_selector) {
  function visit_element(element) {
    element.classList.add("seen");
    element.classList.add("selected");
  
  if (!scroll_selector) {
    scroll_selector = "tr:has(.selected) td.line-number"
  }
  
  const scroll_to = document.querySelector(scroll_selector);
  if (scroll_to) {
    scroll_to.scrollIntoView({behavior: "smooth", block: "center", inline: "end"});
  }
  
  }
  
  function select_one() {
    if (!reverse) {
      const previously_selected = document.querySelector(".selected");
      
      if (previously_selected) {
        previously_selected.classList.remove("selected");
      }
      
      return document.querySelector(selector + ":not(.seen)");
    } else {      
      const previously_selected = document.querySelector(".selected");
      
      if (previously_selected) {
        previously_selected.classList.remove("selected");
        previously_selected.classList.remove("seen");
      }
      
      const nodes = document.querySelectorAll(selector + ".seen");
      if (nodes) {
        const last = nodes[nodes.length - 1]; // last
        return last;
      } else {
        return undefined;
      }
    }
  }
  
  function reset_all() {
    if (!reverse) {
      const all_seen = document.querySelectorAll(selector + ".seen");
  
      if (all_seen) {
        all_seen.forEach(e => e.classList.remove("seen"));
      }
    } else {
      const all_seen = document.querySelectorAll(selector + ":not(.seen)");
  
      if (all_seen) {
        all_seen.forEach(e => e.classList.add("seen"));
      }
    }
    
  }
  
  const uncovered = select_one();

  if (uncovered) {
    visit_element(uncovered);
  } else {
    reset_all();
    
    
    const uncovered = select_one();
    
    if (uncovered) {
      visit_element(uncovered);
    }
  }
}

function next_line(reverse) { 
  next_uncovered("td.uncovered-line", reverse)
}

function next_region(reverse) { 
  next_uncovered("span.red.region", reverse);
}

function next_branch(reverse) { 
  next_uncovered("span.red.branch", reverse);
}

document.addEventListener("keypress", function(event) {
  console.log(event);
  const reverse = event.shiftKey;
  if (event.code == "KeyL") {
    next_line(reverse);
  }
  if (event.code == "KeyB") {
    next_branch(reverse);
  }
  if (event.code == "KeyR") {
    next_region(reverse);
  }
  
});
)javascript";

const char *CSSForCoverage =
    R"(.red {
  background-color: #f004;
}
.cyan {
  background-color: cyan;
}
html {
  scroll-behavior: smooth;
}
body {
  font-family: -apple-system, sans-serif;
}
pre {
  margin-top: 0px !important;
  margin-bottom: 0px !important;
}
.source-name-title {
  padding: 5px 10px;
  border-bottom: 1px solid #8888;
  background-color: #0002;
  line-height: 35px;
}
.centered {
  display: table;
  margin-left: left;
  margin-right: auto;
  border: 1px solid #8888;
  border-radius: 3px;
}
.expansion-view {
  margin-left: 0px;
  margin-top: 5px;
  margin-right: 5px;
  margin-bottom: 5px;
  border: 1px solid #8888;
  border-radius: 3px;
}
table {
  border-collapse: collapse;
}
.light-row {
  border: 1px solid #8888;
  border-left: none;
  border-right: none;
}
.light-row-bold {
  border: 1px solid #8888;
  border-left: none;
  border-right: none;
  font-weight: bold;
}
.column-entry {
  text-align: left;
}
.column-entry-bold {
  font-weight: bold;
  text-align: left;
}
.column-entry-yellow {
  text-align: left;
  background-color: #ff06;
}
.column-entry-red {
  text-align: left;
  background-color: #f004;
}
.column-entry-gray {
  text-align: left;
  background-color: #fff4;
}
.column-entry-green {
  text-align: left;
  background-color: #0f04;
}
.line-number {
  text-align: right;
}
.covered-line {
  text-align: right;
  color: #06d;
}
.uncovered-line {
  text-align: right;
  color: #d00;
}
.uncovered-line.selected {
  color: #f00;
  font-weight: bold;
}
.region.red.selected {
  background-color: #f008;
  font-weight: bold;
}
.branch.red.selected {
  background-color: #f008;
  font-weight: bold;
}
.tooltip {
  position: relative;
  display: inline;
  background-color: #bef;
  text-decoration: none;
}
.tooltip span.tooltip-content {
  position: absolute;
  width: 100px;
  margin-left: -50px;
  color: #FFFFFF;
  background: #000000;
  height: 30px;
  line-height: 30px;
  text-align: center;
  visibility: hidden;
  border-radius: 6px;
}
.tooltip span.tooltip-content:after {
  content: '';
  position: absolute;
  top: 100%;
  left: 50%;
  margin-left: -8px;
  width: 0; height: 0;
  border-top: 8px solid #000000;
  border-right: 8px solid transparent;
  border-left: 8px solid transparent;
}
:hover.tooltip span.tooltip-content {
  visibility: visible;
  opacity: 0.8;
  bottom: 30px;
  left: 50%;
  z-index: 999;
}
th, td {
  vertical-align: top;
  padding: 2px 8px;
  border-collapse: collapse;
  border-right: 1px solid #8888;
  border-left: 1px solid #8888;
  text-align: left;
}
td pre {
  display: inline-block;
  text-decoration: inherit;
}
td:first-child {
  border-left: none;
}
td:last-child {
  border-right: none;
}
tr:hover {
  background-color: #eee;
}
tr:last-child {
  border-bottom: none;
}
tr:has(> td >a:target), tr:has(> td.uncovered-line.selected) {
  background-color: #8884;
}
a {
  color: inherit;
}
.control {
  position: fixed;
  top: 0em;
  right: 0em;
  padding: 1em;
  background: #FFF8;
}
@media (prefers-color-scheme: dark) {
  body {
    background-color: #222;
    color: whitesmoke;
  }
  tr:hover {
    background-color: #111;
  }
  .covered-line {
    color: #39f;
  }
  .uncovered-line {
    color: #f55;
  }
  .tooltip {
    background-color: #068;
  }
  .control {
    background: #2228;
  }
  tr:has(> td >a:target), tr:has(> td.uncovered-line.selected) {
    background-color: #8884;
  }
}
)";

const char *EndHeader = "</head>";

const char *BeginCenteredDiv = "<div class='centered'>";

const char *EndCenteredDiv = "</div>";

const char *BeginSourceNameDiv = "<div class='source-name-title'>";

const char *EndSourceNameDiv = "</div>";

const char *BeginCodeTD = "<td class='code'>";

const char *EndCodeTD = "</td>";

const char *BeginPre = "<pre>";

const char *EndPre = "</pre>";

const char *BeginExpansionDiv = "<div class='expansion-view'>";

const char *EndExpansionDiv = "</div>";

const char *BeginTable = "<table>";

const char *EndTable = "</table>";

const char *ProjectTitleTag = "h1";

const char *ReportTitleTag = "h2";

const char *CreatedTimeTag = "h4";

std::string getPathToStyle(StringRef ViewPath) {
  std::string PathToStyle;
  std::string PathSep = std::string(sys::path::get_separator());
  unsigned NumSeps = ViewPath.count(PathSep);
  for (unsigned I = 0, E = NumSeps; I < E; ++I)
    PathToStyle += ".." + PathSep;
  return PathToStyle + "style.css";
}

std::string getPathToJavaScript(StringRef ViewPath) {
  std::string PathToJavaScript;
  std::string PathSep = std::string(sys::path::get_separator());
  unsigned NumSeps = ViewPath.count(PathSep);
  for (unsigned I = 0, E = NumSeps; I < E; ++I)
    PathToJavaScript += ".." + PathSep;
  return PathToJavaScript + "control.js";
}

void emitPrelude(raw_ostream &OS, const CoverageViewOptions &Opts,
                 const std::string &PathToStyle = "",
                 const std::string &PathToJavaScript = "") {
  OS << "<!doctype html>"
        "<html>"
     << BeginHeader;

  // Link to a stylesheet if one is available. Otherwise, use the default style.
  if (PathToStyle.empty())
    OS << "<style>" << CSSForCoverage << "</style>";
  else
    OS << "<link rel='stylesheet' type='text/css' href='"
       << escape(PathToStyle, Opts) << "'>";

  // Link to a JavaScript if one is available
  if (PathToJavaScript.empty())
    OS << "<script>" << JSForCoverage << "</script>";
  else
    OS << "<script src='" << escape(PathToJavaScript, Opts) << "'></script>";

  OS << EndHeader << "<body>";
}

void emitTableRow(raw_ostream &OS, const CoverageViewOptions &Opts,
                  const std::string &FirstCol, const FileCoverageSummary &FCS,
                  bool IsTotals) {
  SmallVector<std::string, 8> Columns;

  // Format a coverage triple and add the result to the list of columns.
  auto AddCoverageTripleToColumn =
      [&Columns, &Opts](unsigned Hit, unsigned Total, float Pctg) {
        std::string S;
        {
          raw_string_ostream RSO{S};
          if (Total)
            RSO << format("%*.2f", 7, Pctg) << "% ";
          else
            RSO << "- ";
          RSO << '(' << Hit << '/' << Total << ')';
        }
        const char *CellClass = "column-entry-yellow";
        if (!Total)
          CellClass = "column-entry-gray";
        else if (Pctg >= Opts.HighCovWatermark)
          CellClass = "column-entry-green";
        else if (Pctg < Opts.LowCovWatermark)
          CellClass = "column-entry-red";
        Columns.emplace_back(tag("td", tag("pre", S), CellClass));
      };

  Columns.emplace_back(tag("td", tag("pre", FirstCol)));
  AddCoverageTripleToColumn(FCS.FunctionCoverage.getExecuted(),
                            FCS.FunctionCoverage.getNumFunctions(),
                            FCS.FunctionCoverage.getPercentCovered());
  if (Opts.ShowInstantiationSummary)
    AddCoverageTripleToColumn(FCS.InstantiationCoverage.getExecuted(),
                              FCS.InstantiationCoverage.getNumFunctions(),
                              FCS.InstantiationCoverage.getPercentCovered());
  AddCoverageTripleToColumn(FCS.LineCoverage.getCovered(),
                            FCS.LineCoverage.getNumLines(),
                            FCS.LineCoverage.getPercentCovered());
  if (Opts.ShowRegionSummary)
    AddCoverageTripleToColumn(FCS.RegionCoverage.getCovered(),
                              FCS.RegionCoverage.getNumRegions(),
                              FCS.RegionCoverage.getPercentCovered());
  if (Opts.ShowBranchSummary)
    AddCoverageTripleToColumn(FCS.BranchCoverage.getCovered(),
                              FCS.BranchCoverage.getNumBranches(),
                              FCS.BranchCoverage.getPercentCovered());
  if (Opts.ShowMCDCSummary)
    AddCoverageTripleToColumn(FCS.MCDCCoverage.getCoveredPairs(),
                              FCS.MCDCCoverage.getNumPairs(),
                              FCS.MCDCCoverage.getPercentCovered());

  if (IsTotals)
    OS << tag("tr", join(Columns.begin(), Columns.end(), ""), "light-row-bold");
  else
    OS << tag("tr", join(Columns.begin(), Columns.end(), ""), "light-row");
}

void emitEpilog(raw_ostream &OS) {
  OS << "</body>"
     << "</html>";
}

} // anonymous namespace

Expected<CoveragePrinter::OwnedStream>
CoveragePrinterHTML::createViewFile(StringRef Path, bool InToplevel) {
  auto OSOrErr = createOutputStream(Path, "html", InToplevel);
  if (!OSOrErr)
    return OSOrErr;

  OwnedStream OS = std::move(OSOrErr.get());

  if (!Opts.hasOutputDirectory()) {
    emitPrelude(*OS.get(), Opts);
  } else {
    std::string ViewPath = getOutputPath(Path, "html", InToplevel);
    emitPrelude(*OS.get(), Opts, getPathToStyle(ViewPath),
                getPathToJavaScript(ViewPath));
  }

  return std::move(OS);
}

void CoveragePrinterHTML::closeViewFile(OwnedStream OS) {
  emitEpilog(*OS.get());
}

/// Emit column labels for the table in the index.
static void emitColumnLabelsForIndex(raw_ostream &OS,
                                     const CoverageViewOptions &Opts) {
  SmallVector<std::string, 4> Columns;
  Columns.emplace_back(tag("td", "Filename", "column-entry-bold"));
  Columns.emplace_back(tag("td", "Function Coverage", "column-entry-bold"));
  if (Opts.ShowInstantiationSummary)
    Columns.emplace_back(
        tag("td", "Instantiation Coverage", "column-entry-bold"));
  Columns.emplace_back(tag("td", "Line Coverage", "column-entry-bold"));
  if (Opts.ShowRegionSummary)
    Columns.emplace_back(tag("td", "Region Coverage", "column-entry-bold"));
  if (Opts.ShowBranchSummary)
    Columns.emplace_back(tag("td", "Branch Coverage", "column-entry-bold"));
  if (Opts.ShowMCDCSummary)
    Columns.emplace_back(tag("td", "MC/DC", "column-entry-bold"));
  OS << tag("tr", join(Columns.begin(), Columns.end(), ""));
}

std::string
CoveragePrinterHTML::buildLinkToFile(StringRef SF,
                                     const FileCoverageSummary &FCS) const {
  SmallString<128> LinkTextStr(sys::path::relative_path(FCS.Name));
  sys::path::remove_dots(LinkTextStr, /*remove_dot_dot=*/true);
  sys::path::native(LinkTextStr);
  std::string LinkText = escape(LinkTextStr, Opts);
  std::string LinkTarget =
      escape(getOutputPath(SF, "html", /*InToplevel=*/false), Opts);
  return a(LinkTarget, LinkText);
}

Error CoveragePrinterHTML::emitStyleSheet() {
  auto CSSOrErr = createOutputStream("style", "css", /*InToplevel=*/true);
  if (Error E = CSSOrErr.takeError())
    return E;

  OwnedStream CSS = std::move(CSSOrErr.get());
  CSS->operator<<(CSSForCoverage);

  return Error::success();
}

Error CoveragePrinterHTML::emitJavaScript() {
  auto JSOrErr = createOutputStream("control", "js", /*InToplevel=*/true);
  if (Error E = JSOrErr.takeError())
    return E;

  OwnedStream JS = std::move(JSOrErr.get());
  JS->operator<<(JSForCoverage);

  return Error::success();
}

void CoveragePrinterHTML::emitReportHeader(raw_ostream &OSRef,
                                           const std::string &Title) {
  // Emit some basic information about the coverage report.
  if (Opts.hasProjectTitle())
    OSRef << tag(ProjectTitleTag, escape(Opts.ProjectTitle, Opts));
  OSRef << tag(ReportTitleTag, Title);
  if (Opts.hasCreatedTime())
    OSRef << tag(CreatedTimeTag, escape(Opts.CreatedTimeStr, Opts));

  // Emit a link to some documentation.
  OSRef << tag("p", "Click " +
                        a("http://clang.llvm.org/docs/"
                          "SourceBasedCodeCoverage.html#interpreting-reports",
                          "here") +
                        " for information about interpreting this report.");

  // Emit a table containing links to reports for each file in the covmapping.
  // Exclude files which don't contain any regions.
  OSRef << BeginCenteredDiv << BeginTable;
  emitColumnLabelsForIndex(OSRef, Opts);
}

/// Render a file coverage summary (\p FCS) in a table row. If \p IsTotals is
/// false, link the summary to \p SF.
void CoveragePrinterHTML::emitFileSummary(raw_ostream &OS, StringRef SF,
                                          const FileCoverageSummary &FCS,
                                          bool IsTotals) const {
  // Simplify the display file path, and wrap it in a link if requested.
  std::string Filename;
  if (IsTotals) {
    Filename = std::string(SF);
  } else {
    Filename = buildLinkToFile(SF, FCS);
  }

  emitTableRow(OS, Opts, Filename, FCS, IsTotals);
}

Error CoveragePrinterHTML::createIndexFile(
    ArrayRef<std::string> SourceFiles, const CoverageMapping &Coverage,
    const CoverageFiltersMatchAll &Filters) {
  // Emit the default stylesheet.
  if (Error E = emitStyleSheet())
    return E;

  // Emit the JavaScript UI implementation
  if (Error E = emitJavaScript())
    return E;

  // Emit a file index along with some coverage statistics.
  auto OSOrErr = createOutputStream("index", "html", /*InToplevel=*/true);
  if (Error E = OSOrErr.takeError())
    return E;
  auto OS = std::move(OSOrErr.get());
  raw_ostream &OSRef = *OS.get();

  assert(Opts.hasOutputDirectory() && "No output directory for index file");
  emitPrelude(OSRef, Opts, getPathToStyle(""), getPathToJavaScript(""));

  emitReportHeader(OSRef, "Coverage Report");

  FileCoverageSummary Totals("TOTALS");
  auto FileReports = CoverageReport::prepareFileReports(
      Coverage, Totals, SourceFiles, Opts, Filters);
  bool EmptyFiles = false;
  for (unsigned I = 0, E = FileReports.size(); I < E; ++I) {
    if (FileReports[I].FunctionCoverage.getNumFunctions())
      emitFileSummary(OSRef, SourceFiles[I], FileReports[I]);
    else
      EmptyFiles = true;
  }
  emitFileSummary(OSRef, "Totals", Totals, /*IsTotals=*/true);
  OSRef << EndTable << EndCenteredDiv;

  // Emit links to files which don't contain any functions. These are normally
  // not very useful, but could be relevant for code which abuses the
  // preprocessor.
  if (EmptyFiles && Filters.empty()) {
    OSRef << tag("p", "Files which contain no functions. (These "
                      "files contain code pulled into other files "
                      "by the preprocessor.)\n");
    OSRef << BeginCenteredDiv << BeginTable;
    for (unsigned I = 0, E = FileReports.size(); I < E; ++I)
      if (!FileReports[I].FunctionCoverage.getNumFunctions()) {
        std::string Link = buildLinkToFile(SourceFiles[I], FileReports[I]);
        OSRef << tag("tr", tag("td", tag("pre", Link)), "light-row") << '\n';
      }
    OSRef << EndTable << EndCenteredDiv;
  }

  OSRef << tag("h5", escape(Opts.getLLVMVersionString(), Opts));
  emitEpilog(OSRef);

  return Error::success();
}

struct CoveragePrinterHTMLDirectory::Reporter : public DirectoryCoverageReport {
  CoveragePrinterHTMLDirectory &Printer;

  Reporter(CoveragePrinterHTMLDirectory &Printer,
           const coverage::CoverageMapping &Coverage,
           const CoverageFiltersMatchAll &Filters)
      : DirectoryCoverageReport(Printer.Opts, Coverage, Filters),
        Printer(Printer) {}

  Error generateSubDirectoryReport(SubFileReports &&SubFiles,
                                   SubDirReports &&SubDirs,
                                   FileCoverageSummary &&SubTotals) override {
    auto &LCPath = SubTotals.Name;
    assert(Options.hasOutputDirectory() &&
           "No output directory for index file");

    SmallString<128> OSPath = LCPath;
    sys::path::append(OSPath, "index");
    auto OSOrErr = Printer.createOutputStream(OSPath, "html",
                                              /*InToplevel=*/false);
    if (auto E = OSOrErr.takeError())
      return E;
    auto OS = std::move(OSOrErr.get());
    raw_ostream &OSRef = *OS.get();

    auto IndexHtmlPath = Printer.getOutputPath((LCPath + "index").str(), "html",
                                               /*InToplevel=*/false);
    emitPrelude(OSRef, Options, getPathToStyle(IndexHtmlPath),
                getPathToJavaScript(IndexHtmlPath));

    auto NavLink = buildTitleLinks(LCPath);
    Printer.emitReportHeader(OSRef, "Coverage Report (" + NavLink + ")");

    std::vector<const FileCoverageSummary *> EmptyFiles;

    // Make directories at the top of the table.
    for (auto &&SubDir : SubDirs) {
      auto &Report = SubDir.second.first;
      if (!Report.FunctionCoverage.getNumFunctions())
        EmptyFiles.push_back(&Report);
      else
        emitTableRow(OSRef, Options, buildRelLinkToFile(Report.Name), Report,
                     /*IsTotals=*/false);
    }

    for (auto &&SubFile : SubFiles) {
      auto &Report = SubFile.second;
      if (!Report.FunctionCoverage.getNumFunctions())
        EmptyFiles.push_back(&Report);
      else
        emitTableRow(OSRef, Options, buildRelLinkToFile(Report.Name), Report,
                     /*IsTotals=*/false);
    }

    // Emit the totals row.
    emitTableRow(OSRef, Options, "Totals", SubTotals, /*IsTotals=*/false);
    OSRef << EndTable << EndCenteredDiv;

    // Emit links to files which don't contain any functions. These are normally
    // not very useful, but could be relevant for code which abuses the
    // preprocessor.
    if (!EmptyFiles.empty()) {
      OSRef << tag("p", "Files which contain no functions. (These "
                        "files contain code pulled into other files "
                        "by the preprocessor.)\n");
      OSRef << BeginCenteredDiv << BeginTable;
      for (auto FCS : EmptyFiles) {
        auto Link = buildRelLinkToFile(FCS->Name);
        OSRef << tag("tr", tag("td", tag("pre", Link)), "light-row") << '\n';
      }
      OSRef << EndTable << EndCenteredDiv;
    }

    // Emit epilog.
    OSRef << tag("h5", escape(Options.getLLVMVersionString(), Options));
    emitEpilog(OSRef);

    return Error::success();
  }

  /// Make a title with hyperlinks to the index.html files of each hierarchy
  /// of the report.
  std::string buildTitleLinks(StringRef LCPath) const {
    // For each report level in LCPStack, extract the path component and
    // calculate the number of "../" relative to current LCPath.
    SmallVector<std::pair<SmallString<128>, unsigned>, 16> Components;

    auto Iter = LCPStack.begin(), IterE = LCPStack.end();
    SmallString<128> RootPath;
    if (*Iter == 0) {
      // If llvm-cov works on relative coverage mapping data, the LCP of
      // all source file paths can be 0, which makes the title path empty.
      // As we like adding a slash at the back of the path to indicate a
      // directory, in this case, we use "." as the root path to make it
      // not be confused with the root path "/".
      RootPath = ".";
    } else {
      RootPath = LCPath.substr(0, *Iter);
      sys::path::native(RootPath);
      sys::path::remove_dots(RootPath, /*remove_dot_dot=*/true);
    }
    Components.emplace_back(std::move(RootPath), 0);

    for (auto Last = *Iter; ++Iter != IterE; Last = *Iter) {
      SmallString<128> SubPath = LCPath.substr(Last, *Iter - Last);
      sys::path::native(SubPath);
      sys::path::remove_dots(SubPath, /*remove_dot_dot=*/true);
      auto Level = unsigned(SubPath.count(sys::path::get_separator())) + 1;
      Components.back().second += Level;
      Components.emplace_back(std::move(SubPath), Level);
    }

    // Then we make the title accroding to Components.
    std::string S;
    for (auto I = Components.begin(), E = Components.end();;) {
      auto &Name = I->first;
      if (++I == E) {
        S += a("./index.html", Name);
        S += sys::path::get_separator();
        break;
      }

      SmallString<128> Link;
      for (unsigned J = I->second; J > 0; --J)
        Link += "../";
      Link += "index.html";
      S += a(Link, Name);
      S += sys::path::get_separator();
    }
    return S;
  }

  std::string buildRelLinkToFile(StringRef RelPath) const {
    SmallString<128> LinkTextStr(RelPath);
    sys::path::native(LinkTextStr);

    // remove_dots will remove trailing slash, so we need to check before it.
    auto IsDir = LinkTextStr.ends_with(sys::path::get_separator());
    sys::path::remove_dots(LinkTextStr, /*remove_dot_dot=*/true);

    SmallString<128> LinkTargetStr(LinkTextStr);
    if (IsDir) {
      LinkTextStr += sys::path::get_separator();
      sys::path::append(LinkTargetStr, "index.html");
    } else {
      LinkTargetStr += ".html";
    }

    auto LinkText = escape(LinkTextStr, Options);
    auto LinkTarget = escape(LinkTargetStr, Options);
    return a(LinkTarget, LinkText);
  }
};

Error CoveragePrinterHTMLDirectory::createIndexFile(
    ArrayRef<std::string> SourceFiles, const CoverageMapping &Coverage,
    const CoverageFiltersMatchAll &Filters) {
  // The createSubIndexFile function only works when SourceFiles is
  // more than one. So we fallback to CoveragePrinterHTML when it is.
  if (SourceFiles.size() <= 1)
    return CoveragePrinterHTML::createIndexFile(SourceFiles, Coverage, Filters);

  // Emit the default stylesheet.
  if (Error E = emitStyleSheet())
    return E;

  // Emit the JavaScript UI implementation
  if (Error E = emitJavaScript())
    return E;

  // Emit index files in every subdirectory.
  Reporter Report(*this, Coverage, Filters);
  auto TotalsOrErr = Report.prepareDirectoryReports(SourceFiles);
  if (auto E = TotalsOrErr.takeError())
    return E;
  auto &LCPath = TotalsOrErr->Name;

  // Emit the top level index file. Top level index file is just a redirection
  // to the index file in the LCP directory.
  auto OSOrErr = createOutputStream("index", "html", /*InToplevel=*/true);
  if (auto E = OSOrErr.takeError())
    return E;
  auto OS = std::move(OSOrErr.get());
  auto LCPIndexFilePath =
      getOutputPath((LCPath + "index").str(), "html", /*InToplevel=*/false);
  *OS.get() << R"(<!DOCTYPE html>
  <html>
    <head>
      <meta http-equiv="Refresh" content="0; url=')"
            << LCPIndexFilePath << R"('" />
    </head>
    <body></body>
  </html>
  )";

  return Error::success();
}

void SourceCoverageViewHTML::renderViewHeader(raw_ostream &OS) {
  OS << BeginCenteredDiv << BeginTable;
}

void SourceCoverageViewHTML::renderViewFooter(raw_ostream &OS) {
  OS << EndTable << EndCenteredDiv;
}

void SourceCoverageViewHTML::renderSourceName(raw_ostream &OS, bool WholeFile) {
  OS << BeginSourceNameDiv << tag("pre", escape(getSourceName(), getOptions()))
     << EndSourceNameDiv;
}

void SourceCoverageViewHTML::renderLinePrefix(raw_ostream &OS, unsigned) {
  OS << "<tr>";
}

void SourceCoverageViewHTML::renderLineSuffix(raw_ostream &OS, unsigned) {
  // If this view has sub-views, renderLine() cannot close the view's cell.
  // Take care of it here, after all sub-views have been rendered.
  if (hasSubViews())
    OS << EndCodeTD;
  OS << "</tr>";
}

void SourceCoverageViewHTML::renderViewDivider(raw_ostream &, unsigned) {
  // The table-based output makes view dividers unnecessary.
}

void SourceCoverageViewHTML::renderLine(raw_ostream &OS, LineRef L,
                                        const LineCoverageStats &LCS,
                                        unsigned ExpansionCol, unsigned) {
  StringRef Line = L.Line;
  unsigned LineNo = L.LineNo;

  // Steps for handling text-escaping, highlighting, and tooltip creation:
  //
  // 1. Split the line into N+1 snippets, where N = |Segments|. The first
  //    snippet starts from Col=1 and ends at the start of the first segment.
  //    The last snippet starts at the last mapped column in the line and ends
  //    at the end of the line. Both are required but may be empty.

  SmallVector<std::string, 8> Snippets;
  CoverageSegmentArray Segments = LCS.getLineSegments();

  unsigned LCol = 1;
  auto Snip = [&](unsigned Start, unsigned Len) {
    Snippets.push_back(std::string(Line.substr(Start, Len)));
    LCol += Len;
  };

  Snip(LCol - 1, Segments.empty() ? 0 : (Segments.front()->Col - 1));

  for (unsigned I = 1, E = Segments.size(); I < E; ++I)
    Snip(LCol - 1, Segments[I]->Col - LCol);

  // |Line| + 1 is needed to avoid underflow when, e.g |Line| = 0 and LCol = 1.
  Snip(LCol - 1, Line.size() + 1 - LCol);

  // 2. Escape all of the snippets.

  for (unsigned I = 0, E = Snippets.size(); I < E; ++I)
    Snippets[I] = escape(Snippets[I], getOptions());

  // 3. Use \p WrappedSegment to set the highlight for snippet 0. Use segment
  //    1 to set the highlight for snippet 2, segment 2 to set the highlight for
  //    snippet 3, and so on.

  std::optional<StringRef> Color;
  SmallVector<std::pair<unsigned, unsigned>, 2> HighlightedRanges;
  auto Highlight = [&](const std::string &Snippet, unsigned LC, unsigned RC) {
    if (getOptions().Debug)
      HighlightedRanges.emplace_back(LC, RC);
    if (Snippet.empty())
      return tag("span", Snippet, std::string(*Color));
    else
      return tag("span", Snippet, "region " + std::string(*Color));
  };

  auto CheckIfUncovered = [&](const CoverageSegment *S) {
    return S && (!S->IsGapRegion || (Color && *Color == "red")) &&
           S->HasCount && S->Count == 0;
  };

  if (CheckIfUncovered(LCS.getWrappedSegment())) {
    Color = "red";
    if (!Snippets[0].empty())
      Snippets[0] = Highlight(Snippets[0], 1, 1 + Snippets[0].size());
  }

  for (unsigned I = 0, E = Segments.size(); I < E; ++I) {
    const auto *CurSeg = Segments[I];
    if (CheckIfUncovered(CurSeg))
      Color = "red";
    else if (CurSeg->Col == ExpansionCol)
      Color = "cyan";
    else
      Color = std::nullopt;

    if (Color)
      Snippets[I + 1] = Highlight(Snippets[I + 1], CurSeg->Col,
                                  CurSeg->Col + Snippets[I + 1].size());
  }

  if (Color && Segments.empty())
    Snippets.back() = Highlight(Snippets.back(), 1, 1 + Snippets.back().size());

  if (getOptions().Debug) {
    for (const auto &Range : HighlightedRanges) {
      errs() << "Highlighted line " << LineNo << ", " << Range.first << " -> ";
      if (Range.second == 0)
        errs() << "?";
      else
        errs() << Range.second;
      errs() << "\n";
    }
  }

  // 4. Snippets[1:N+1] correspond to \p Segments[0:N]: use these to generate
  //    sub-line region count tooltips if needed.

  if (shouldRenderRegionMarkers(LCS)) {
    // Just consider the segments which start *and* end on this line.
    for (unsigned I = 0, E = Segments.size() - 1; I < E; ++I) {
      const auto *CurSeg = Segments[I];
      if (!CurSeg->IsRegionEntry)
        continue;
      if (CurSeg->Count == LCS.getExecutionCount())
        continue;

      Snippets[I + 1] =
          tag("div", Snippets[I + 1] + tag("span", formatCount(CurSeg->Count),
                                           "tooltip-content"),
              "tooltip");

      if (getOptions().Debug)
        errs() << "Marker at " << CurSeg->Line << ":" << CurSeg->Col << " = "
               << formatCount(CurSeg->Count) << "\n";
    }
  }

  OS << BeginCodeTD;
  OS << BeginPre;
  for (const auto &Snippet : Snippets)
    OS << Snippet;
  OS << EndPre;

  // If there are no sub-views left to attach to this cell, end the cell.
  // Otherwise, end it after the sub-views are rendered (renderLineSuffix()).
  if (!hasSubViews())
    OS << EndCodeTD;
}

void SourceCoverageViewHTML::renderLineCoverageColumn(
    raw_ostream &OS, const LineCoverageStats &Line) {
  std::string Count;
  if (Line.isMapped())
    Count = tag("pre", formatCount(Line.getExecutionCount()));
  std::string CoverageClass =
      (Line.getExecutionCount() > 0)
          ? "covered-line"
          : (Line.isMapped() ? "uncovered-line" : "skipped-line");
  OS << tag("td", Count, CoverageClass);
}

void SourceCoverageViewHTML::renderLineNumberColumn(raw_ostream &OS,
                                                    unsigned LineNo) {
  std::string LineNoStr = utostr(uint64_t(LineNo));
  std::string TargetName = "L" + LineNoStr;
  OS << tag("td", a("#" + TargetName, tag("pre", LineNoStr), TargetName),
            "line-number");
}

void SourceCoverageViewHTML::renderRegionMarkers(raw_ostream &,
                                                 const LineCoverageStats &Line,
                                                 unsigned) {
  // Region markers are rendered in-line using tooltips.
}

void SourceCoverageViewHTML::renderExpansionSite(raw_ostream &OS, LineRef L,
                                                 const LineCoverageStats &LCS,
                                                 unsigned ExpansionCol,
                                                 unsigned ViewDepth) {
  // Render the line containing the expansion site. No extra formatting needed.
  renderLine(OS, L, LCS, ExpansionCol, ViewDepth);
}

void SourceCoverageViewHTML::renderExpansionView(raw_ostream &OS,
                                                 ExpansionView &ESV,
                                                 unsigned ViewDepth) {
  OS << BeginExpansionDiv;
  ESV.View->print(OS, /*WholeFile=*/false, /*ShowSourceName=*/false,
                  /*ShowTitle=*/false, ViewDepth + 1);
  OS << EndExpansionDiv;
}

void SourceCoverageViewHTML::renderBranchView(raw_ostream &OS, BranchView &BRV,
                                              unsigned ViewDepth) {
  // Render the child subview.
  if (getOptions().Debug)
    errs() << "Branch at line " << BRV.getLine() << '\n';

  OS << BeginExpansionDiv;
  OS << BeginPre;
  for (const auto &R : BRV.Regions) {
    // Calculate TruePercent and False Percent.
    double TruePercent = 0.0;
    double FalsePercent = 0.0;
    // FIXME: It may overflow when the data is too large, but I have not
    // encountered it in actual use, and not sure whether to use __uint128_t.
    uint64_t Total = R.ExecutionCount + R.FalseExecutionCount;

    if (!getOptions().ShowBranchCounts && Total != 0) {
      TruePercent = ((double)(R.ExecutionCount) / (double)Total) * 100.0;
      FalsePercent = ((double)(R.FalseExecutionCount) / (double)Total) * 100.0;
    }

    // Display Line + Column.
    std::string LineNoStr = utostr(uint64_t(R.LineStart));
    std::string ColNoStr = utostr(uint64_t(R.ColumnStart));
    std::string TargetName = "L" + LineNoStr;

    OS << "  Branch (";
    OS << tag("span",
              a("#" + TargetName, tag("span", LineNoStr + ":" + ColNoStr),
                TargetName),
              "line-number") +
              "): [";

    if (R.Folded) {
      OS << "Folded - Ignored]\n";
      continue;
    }

    // Display TrueCount or TruePercent.
    std::string TrueColor = R.ExecutionCount ? "None" : "red branch";
    std::string TrueCovClass =
        (R.ExecutionCount > 0) ? "covered-line" : "uncovered-line";

    OS << tag("span", "True", TrueColor);
    OS << ": ";
    if (getOptions().ShowBranchCounts)
      OS << tag("span", formatCount(R.ExecutionCount), TrueCovClass) << ", ";
    else
      OS << format("%0.2f", TruePercent) << "%, ";

    // Display FalseCount or FalsePercent.
    std::string FalseColor = R.FalseExecutionCount ? "None" : "red branch";
    std::string FalseCovClass =
        (R.FalseExecutionCount > 0) ? "covered-line" : "uncovered-line";

    OS << tag("span", "False", FalseColor);
    OS << ": ";
    if (getOptions().ShowBranchCounts)
      OS << tag("span", formatCount(R.FalseExecutionCount), FalseCovClass);
    else
      OS << format("%0.2f", FalsePercent) << "%";

    OS << "]\n";
  }
  OS << EndPre;
  OS << EndExpansionDiv;
}

void SourceCoverageViewHTML::renderMCDCView(raw_ostream &OS, MCDCView &MRV,
                                            unsigned ViewDepth) {
  for (auto &Record : MRV.Records) {
    OS << BeginExpansionDiv;
    OS << BeginPre;
    OS << "  MC/DC Decision Region (";

    // Display Line + Column information.
    const CounterMappingRegion &DecisionRegion = Record.getDecisionRegion();
    std::string LineNoStr = Twine(DecisionRegion.LineStart).str();
    std::string ColNoStr = Twine(DecisionRegion.ColumnStart).str();
    std::string TargetName = "L" + LineNoStr;
    OS << tag("span",
              a("#" + TargetName, tag("span", LineNoStr + ":" + ColNoStr)),
              "line-number") +
              ") to (";
    LineNoStr = utostr(uint64_t(DecisionRegion.LineEnd));
    ColNoStr = utostr(uint64_t(DecisionRegion.ColumnEnd));
    OS << tag("span",
              a("#" + TargetName, tag("span", LineNoStr + ":" + ColNoStr)),
              "line-number") +
              ")\n\n";

    // Display MC/DC Information.
    OS << "  Number of Conditions: " << Record.getNumConditions() << "\n";
    for (unsigned i = 0; i < Record.getNumConditions(); i++) {
      OS << "     " << Record.getConditionHeaderString(i);
    }
    OS << "\n";
    OS << "  Executed MC/DC Test Vectors:\n\n     ";
    OS << Record.getTestVectorHeaderString();
    for (unsigned i = 0; i < Record.getNumTestVectors(); i++)
      OS << Record.getTestVectorString(i);
    OS << "\n";
    for (unsigned i = 0; i < Record.getNumConditions(); i++)
      OS << Record.getConditionCoverageString(i);
    OS << "  MC/DC Coverage for Expression: ";
    OS << format("%0.2f", Record.getPercentCovered()) << "%\n";
    OS << EndPre;
    OS << EndExpansionDiv;
  }
  return;
}

void SourceCoverageViewHTML::renderInstantiationView(raw_ostream &OS,
                                                     InstantiationView &ISV,
                                                     unsigned ViewDepth) {
  OS << BeginExpansionDiv;
  if (!ISV.View)
    OS << BeginSourceNameDiv
       << tag("pre",
              escape("Unexecuted instantiation: " + ISV.FunctionName.str(),
                     getOptions()))
       << EndSourceNameDiv;
  else
    ISV.View->print(OS, /*WholeFile=*/false, /*ShowSourceName=*/true,
                    /*ShowTitle=*/false, ViewDepth);
  OS << EndExpansionDiv;
}

void SourceCoverageViewHTML::renderTitle(raw_ostream &OS, StringRef Title) {
  if (getOptions().hasProjectTitle())
    OS << tag(ProjectTitleTag, escape(getOptions().ProjectTitle, getOptions()));
  OS << tag(ReportTitleTag, escape(Title, getOptions()));
  if (getOptions().hasCreatedTime())
    OS << tag(CreatedTimeTag,
              escape(getOptions().CreatedTimeStr, getOptions()));

  OS << tag("span",
            a("javascript:next_line()", "next uncovered line (L)") + ", " +
                a("javascript:next_region()", "next uncovered region (R)") +
                ", " +
                a("javascript:next_branch()", "next uncovered branch (B)"),
            "control");
}

void SourceCoverageViewHTML::renderTableHeader(raw_ostream &OS,
                                               unsigned ViewDepth) {
  std::string Links;

  renderLinePrefix(OS, ViewDepth);
  OS << tag("td", tag("pre", "Line")) << tag("td", tag("pre", "Count"));
  OS << tag("td", tag("pre", "Source" + Links));
  renderLineSuffix(OS, ViewDepth);
}
