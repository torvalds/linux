//===- HTMLDiagnostics.cpp - HTML Diagnostics for Paths -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the HTMLDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/IssueHash.h"
#include "clang/Analysis/MacroExpansionContext.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "clang/Rewrite/Core/HTMLRewrite.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Boilerplate.
//===----------------------------------------------------------------------===//

namespace {

class ArrowMap;

class HTMLDiagnostics : public PathDiagnosticConsumer {
  PathDiagnosticConsumerOptions DiagOpts;
  std::string Directory;
  bool createdDir = false;
  bool noDir = false;
  const Preprocessor &PP;
  const bool SupportsCrossFileDiagnostics;
  llvm::StringSet<> EmittedHashes;
  html::RelexRewriteCacheRef RewriterCache =
      html::instantiateRelexRewriteCache();

public:
  HTMLDiagnostics(PathDiagnosticConsumerOptions DiagOpts,
                  const std::string &OutputDir, const Preprocessor &pp,
                  bool supportsMultipleFiles)
      : DiagOpts(std::move(DiagOpts)), Directory(OutputDir), PP(pp),
        SupportsCrossFileDiagnostics(supportsMultipleFiles) {}

  ~HTMLDiagnostics() override { FlushDiagnostics(nullptr); }

  void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                            FilesMade *filesMade) override;

  StringRef getName() const override { return "HTMLDiagnostics"; }

  bool supportsCrossFileDiagnostics() const override {
    return SupportsCrossFileDiagnostics;
  }

  unsigned ProcessMacroPiece(raw_ostream &os, const PathDiagnosticMacroPiece &P,
                             unsigned num);

  unsigned ProcessControlFlowPiece(Rewriter &R, FileID BugFileID,
                                   const PathDiagnosticControlFlowPiece &P,
                                   unsigned Number);

  void HandlePiece(Rewriter &R, FileID BugFileID, const PathDiagnosticPiece &P,
                   const std::vector<SourceRange> &PopUpRanges, unsigned num,
                   unsigned max);

  void HighlightRange(Rewriter &R, FileID BugFileID, SourceRange Range,
                      const char *HighlightStart = "<span class=\"mrange\">",
                      const char *HighlightEnd = "</span>");

  void ReportDiag(const PathDiagnostic &D, FilesMade *filesMade);

  // Generate the full HTML report
  std::string GenerateHTML(const PathDiagnostic &D, Rewriter &R,
                           const SourceManager &SMgr, const PathPieces &path,
                           const char *declName);

  // Add HTML header/footers to file specified by FID
  void FinalizeHTML(const PathDiagnostic &D, Rewriter &R,
                    const SourceManager &SMgr, const PathPieces &path,
                    FileID FID, FileEntryRef Entry, const char *declName);

  // Rewrite the file specified by FID with HTML formatting.
  void RewriteFile(Rewriter &R, const PathPieces &path, FileID FID);

  PathGenerationScheme getGenerationScheme() const override {
    return Everything;
  }

private:
  void addArrowSVGs(Rewriter &R, FileID BugFileID,
                    const ArrowMap &ArrowIndices);

  /// \return Javascript for displaying shortcuts help;
  StringRef showHelpJavascript();

  /// \return Javascript for navigating the HTML report using j/k keys.
  StringRef generateKeyboardNavigationJavascript();

  /// \return Javascript for drawing control-flow arrows.
  StringRef generateArrowDrawingJavascript();

  /// \return JavaScript for an option to only show relevant lines.
  std::string showRelevantLinesJavascript(const PathDiagnostic &D,
                                          const PathPieces &path);

  /// Write executed lines from \p D in JSON format into \p os.
  void dumpCoverageData(const PathDiagnostic &D, const PathPieces &path,
                        llvm::raw_string_ostream &os);
};

bool isArrowPiece(const PathDiagnosticPiece &P) {
  return isa<PathDiagnosticControlFlowPiece>(P) && P.getString().empty();
}

unsigned getPathSizeWithoutArrows(const PathPieces &Path) {
  unsigned TotalPieces = Path.size();
  unsigned TotalArrowPieces = llvm::count_if(
      Path, [](const PathDiagnosticPieceRef &P) { return isArrowPiece(*P); });
  return TotalPieces - TotalArrowPieces;
}

class ArrowMap : public std::vector<unsigned> {
  using Base = std::vector<unsigned>;

public:
  ArrowMap(unsigned Size) : Base(Size, 0) {}
  unsigned getTotalNumberOfArrows() const { return at(0); }
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const ArrowMap &Indices) {
  OS << "[ ";
  llvm::interleave(Indices, OS, ",");
  return OS << " ]";
}

} // namespace

void ento::createHTMLDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &OutputDir, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {

  // FIXME: HTML is currently our default output type, but if the output
  // directory isn't specified, it acts like if it was in the minimal text
  // output mode. This doesn't make much sense, we should have the minimal text
  // as our default. In the case of backward compatibility concerns, this could
  // be preserved with -analyzer-config-compatibility-mode=true.
  createTextMinimalPathDiagnosticConsumer(DiagOpts, C, OutputDir, PP, CTU,
                                          MacroExpansions);

  // TODO: Emit an error here.
  if (OutputDir.empty())
    return;

  C.push_back(new HTMLDiagnostics(std::move(DiagOpts), OutputDir, PP, true));
}

void ento::createHTMLSingleFileDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &OutputDir, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const clang::MacroExpansionContext &MacroExpansions) {
  createTextMinimalPathDiagnosticConsumer(DiagOpts, C, OutputDir, PP, CTU,
                                          MacroExpansions);

  // TODO: Emit an error here.
  if (OutputDir.empty())
    return;

  C.push_back(new HTMLDiagnostics(std::move(DiagOpts), OutputDir, PP, false));
}

void ento::createPlistHTMLDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &prefix, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {
  createHTMLDiagnosticConsumer(
      DiagOpts, C, std::string(llvm::sys::path::parent_path(prefix)), PP, CTU,
      MacroExpansions);
  createPlistMultiFileDiagnosticConsumer(DiagOpts, C, prefix, PP, CTU,
                                         MacroExpansions);
  createTextMinimalPathDiagnosticConsumer(std::move(DiagOpts), C, prefix, PP,
                                          CTU, MacroExpansions);
}

void ento::createSarifHTMLDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &sarif_file, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {
  createHTMLDiagnosticConsumer(
      DiagOpts, C, std::string(llvm::sys::path::parent_path(sarif_file)), PP,
      CTU, MacroExpansions);
  createSarifDiagnosticConsumer(DiagOpts, C, sarif_file, PP, CTU,
                                MacroExpansions);
  createTextMinimalPathDiagnosticConsumer(std::move(DiagOpts), C, sarif_file,
                                          PP, CTU, MacroExpansions);
}

//===----------------------------------------------------------------------===//
// Report processing.
//===----------------------------------------------------------------------===//

void HTMLDiagnostics::FlushDiagnosticsImpl(
  std::vector<const PathDiagnostic *> &Diags,
  FilesMade *filesMade) {
  for (const auto Diag : Diags)
    ReportDiag(*Diag, filesMade);
}

static llvm::SmallString<32> getIssueHash(const PathDiagnostic &D,
                                          const Preprocessor &PP) {
  SourceManager &SMgr = PP.getSourceManager();
  PathDiagnosticLocation UPDLoc = D.getUniqueingLoc();
  FullSourceLoc L(SMgr.getExpansionLoc(UPDLoc.isValid()
                                           ? UPDLoc.asLocation()
                                           : D.getLocation().asLocation()),
                  SMgr);
  return getIssueHash(L, D.getCheckerName(), D.getBugType(),
                      D.getDeclWithIssue(), PP.getLangOpts());
}

void HTMLDiagnostics::ReportDiag(const PathDiagnostic& D,
                                 FilesMade *filesMade) {
  // Create the HTML directory if it is missing.
  if (!createdDir) {
    createdDir = true;
    if (std::error_code ec = llvm::sys::fs::create_directories(Directory)) {
      llvm::errs() << "warning: could not create directory '"
                   << Directory << "': " << ec.message() << '\n';
      noDir = true;
      return;
    }
  }

  if (noDir)
    return;

  // First flatten out the entire path to make it easier to use.
  PathPieces path = D.path.flatten(/*ShouldFlattenMacros=*/false);

  // The path as already been prechecked that the path is non-empty.
  assert(!path.empty());
  const SourceManager &SMgr = path.front()->getLocation().getManager();

  // Create a new rewriter to generate HTML.
  Rewriter R(const_cast<SourceManager&>(SMgr), PP.getLangOpts());

  // Get the function/method name
  SmallString<128> declName("unknown");
  int offsetDecl = 0;
  if (const Decl *DeclWithIssue = D.getDeclWithIssue()) {
      if (const auto *ND = dyn_cast<NamedDecl>(DeclWithIssue))
          declName = ND->getDeclName().getAsString();

      if (const Stmt *Body = DeclWithIssue->getBody()) {
          // Retrieve the relative position of the declaration which will be used
          // for the file name
          FullSourceLoc L(
              SMgr.getExpansionLoc(path.back()->getLocation().asLocation()),
              SMgr);
          FullSourceLoc FunL(SMgr.getExpansionLoc(Body->getBeginLoc()), SMgr);
          offsetDecl = L.getExpansionLineNumber() - FunL.getExpansionLineNumber();
      }
  }

  SmallString<32> IssueHash = getIssueHash(D, PP);
  auto [It, IsNew] = EmittedHashes.insert(IssueHash);
  if (!IsNew) {
    // We've already emitted a duplicate issue. It'll get overwritten anyway.
    return;
  }

  std::string report = GenerateHTML(D, R, SMgr, path, declName.c_str());
  if (report.empty()) {
    llvm::errs() << "warning: no diagnostics generated for main file.\n";
    return;
  }

  // Create a path for the target HTML file.
  int FD;

  SmallString<128> FileNameStr;
  llvm::raw_svector_ostream FileName(FileNameStr);
  FileName << "report-";

  // Historically, neither the stable report filename nor the unstable report
  // filename were actually stable. That said, the stable report filename
  // was more stable because it was mostly composed of information
  // about the bug report instead of being completely random.
  // Now both stable and unstable report filenames are in fact stable
  // but the stable report filename is still more verbose.
  if (DiagOpts.ShouldWriteVerboseReportFilename) {
    // FIXME: This code relies on knowing what constitutes the issue hash.
    // Otherwise deduplication won't work correctly.
    FileID ReportFile =
        path.back()->getLocation().asLocation().getExpansionLoc().getFileID();

    OptionalFileEntryRef Entry = SMgr.getFileEntryRefForID(ReportFile);

    FileName << llvm::sys::path::filename(Entry->getName()).str() << "-"
             << declName.c_str() << "-" << offsetDecl << "-";
  }

  FileName << StringRef(IssueHash).substr(0, 6).str() << ".html";

  SmallString<128> ResultPath;
  llvm::sys::path::append(ResultPath, Directory, FileName.str());
  if (std::error_code EC = llvm::sys::fs::make_absolute(ResultPath)) {
    llvm::errs() << "warning: could not make '" << ResultPath
                 << "' absolute: " << EC.message() << '\n';
    return;
  }

  if (std::error_code EC = llvm::sys::fs::openFileForReadWrite(
          ResultPath, FD, llvm::sys::fs::CD_CreateNew,
          llvm::sys::fs::OF_Text)) {
    // Existence of the file corresponds to the situation where a different
    // Clang instance has emitted a bug report with the same issue hash.
    // This is an entirely normal situation that does not deserve a warning,
    // as apart from hash collisions this can happen because the reports
    // are in fact similar enough to be considered duplicates of each other.
    if (EC != llvm::errc::file_exists) {
      llvm::errs() << "warning: could not create file in '" << Directory
                   << "': " << EC.message() << '\n';
    }
    return;
  }

  llvm::raw_fd_ostream os(FD, true);

  if (filesMade)
    filesMade->addDiagnostic(D, getName(),
                             llvm::sys::path::filename(ResultPath));

  // Emit the HTML to disk.
  os << report;
}

std::string HTMLDiagnostics::GenerateHTML(const PathDiagnostic& D, Rewriter &R,
    const SourceManager& SMgr, const PathPieces& path, const char *declName) {
  // Rewrite source files as HTML for every new file the path crosses
  std::vector<FileID> FileIDs;
  for (auto I : path) {
    FileID FID = I->getLocation().asLocation().getExpansionLoc().getFileID();
    if (llvm::is_contained(FileIDs, FID))
      continue;

    FileIDs.push_back(FID);
    RewriteFile(R, path, FID);
  }

  if (SupportsCrossFileDiagnostics && FileIDs.size() > 1) {
    // Prefix file names, anchor tags, and nav cursors to every file
    for (auto I = FileIDs.begin(), E = FileIDs.end(); I != E; I++) {
      std::string s;
      llvm::raw_string_ostream os(s);

      if (I != FileIDs.begin())
        os << "<hr class=divider>\n";

      os << "<div id=File" << I->getHashValue() << ">\n";

      // Left nav arrow
      if (I != FileIDs.begin())
        os << "<div class=FileNav><a href=\"#File" << (I - 1)->getHashValue()
           << "\">&#x2190;</a></div>";

      os << "<h4 class=FileName>" << SMgr.getFileEntryRefForID(*I)->getName()
         << "</h4>\n";

      // Right nav arrow
      if (I + 1 != E)
        os << "<div class=FileNav><a href=\"#File" << (I + 1)->getHashValue()
           << "\">&#x2192;</a></div>";

      os << "</div>\n";

      R.InsertTextBefore(SMgr.getLocForStartOfFile(*I), os.str());
    }

    // Append files to the main report file in the order they appear in the path
    for (auto I : llvm::drop_begin(FileIDs)) {
      std::string s;
      llvm::raw_string_ostream os(s);

      const RewriteBuffer *Buf = R.getRewriteBufferFor(I);
      for (auto BI : *Buf)
        os << BI;

      R.InsertTextAfter(SMgr.getLocForEndOfFile(FileIDs[0]), os.str());
    }
  }

  const RewriteBuffer *Buf = R.getRewriteBufferFor(FileIDs[0]);
  if (!Buf)
    return {};

  // Add CSS, header, and footer.
  FileID FID =
      path.back()->getLocation().asLocation().getExpansionLoc().getFileID();
  OptionalFileEntryRef Entry = SMgr.getFileEntryRefForID(FID);
  FinalizeHTML(D, R, SMgr, path, FileIDs[0], *Entry, declName);

  std::string file;
  llvm::raw_string_ostream os(file);
  for (auto BI : *Buf)
    os << BI;

  return file;
}

void HTMLDiagnostics::dumpCoverageData(
    const PathDiagnostic &D,
    const PathPieces &path,
    llvm::raw_string_ostream &os) {

  const FilesToLineNumsMap &ExecutedLines = D.getExecutedLines();

  os << "var relevant_lines = {";
  for (auto I = ExecutedLines.begin(),
            E = ExecutedLines.end(); I != E; ++I) {
    if (I != ExecutedLines.begin())
      os << ", ";

    os << "\"" << I->first.getHashValue() << "\": {";
    for (unsigned LineNo : I->second) {
      if (LineNo != *(I->second.begin()))
        os << ", ";

      os << "\"" << LineNo << "\": 1";
    }
    os << "}";
  }

  os << "};";
}

std::string HTMLDiagnostics::showRelevantLinesJavascript(
      const PathDiagnostic &D, const PathPieces &path) {
  std::string s;
  llvm::raw_string_ostream os(s);
  os << "<script type='text/javascript'>\n";
  dumpCoverageData(D, path, os);
  os << R"<<<(

var filterCounterexample = function (hide) {
  var tables = document.getElementsByClassName("code");
  for (var t=0; t<tables.length; t++) {
    var table = tables[t];
    var file_id = table.getAttribute("data-fileid");
    var lines_in_fid = relevant_lines[file_id];
    if (!lines_in_fid) {
      lines_in_fid = {};
    }
    var lines = table.getElementsByClassName("codeline");
    for (var i=0; i<lines.length; i++) {
        var el = lines[i];
        var lineNo = el.getAttribute("data-linenumber");
        if (!lines_in_fid[lineNo]) {
          if (hide) {
            el.setAttribute("hidden", "");
          } else {
            el.removeAttribute("hidden");
          }
        }
    }
  }
}

window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  // SHIFT + S
  if (event.shiftKey && event.keyCode == 83) {
    var checked = document.getElementsByName("showCounterexample")[0].checked;
    filterCounterexample(!checked);
    document.getElementsByName("showCounterexample")[0].click();
  } else {
    return;
  }
  event.preventDefault();
}, true);

document.addEventListener("DOMContentLoaded", function() {
    document.querySelector('input[name="showCounterexample"]').onchange=
        function (event) {
      filterCounterexample(this.checked);
    };
});
</script>

<form>
    <input type="checkbox" name="showCounterexample" id="showCounterexample" />
    <label for="showCounterexample">
       Show only relevant lines
    </label>
    <input type="checkbox" name="showArrows"
           id="showArrows" style="margin-left: 10px" />
    <label for="showArrows">
       Show control flow arrows
    </label>
</form>
)<<<";

  return s;
}

void HTMLDiagnostics::FinalizeHTML(const PathDiagnostic &D, Rewriter &R,
                                   const SourceManager &SMgr,
                                   const PathPieces &path, FileID FID,
                                   FileEntryRef Entry, const char *declName) {
  // This is a cludge; basically we want to append either the full
  // working directory if we have no directory information.  This is
  // a work in progress.

  llvm::SmallString<0> DirName;

  if (llvm::sys::path::is_relative(Entry.getName())) {
    llvm::sys::fs::current_path(DirName);
    DirName += '/';
  }

  int LineNumber = path.back()->getLocation().asLocation().getExpansionLineNumber();
  int ColumnNumber = path.back()->getLocation().asLocation().getExpansionColumnNumber();

  R.InsertTextBefore(SMgr.getLocForStartOfFile(FID), showHelpJavascript());

  R.InsertTextBefore(SMgr.getLocForStartOfFile(FID),
                     generateKeyboardNavigationJavascript());

  R.InsertTextBefore(SMgr.getLocForStartOfFile(FID),
                     generateArrowDrawingJavascript());

  // Checkbox and javascript for filtering the output to the counterexample.
  R.InsertTextBefore(SMgr.getLocForStartOfFile(FID),
                     showRelevantLinesJavascript(D, path));

  // Add the name of the file as an <h1> tag.
  {
    std::string s;
    llvm::raw_string_ostream os(s);

    os << "<!-- REPORTHEADER -->\n"
       << "<h3>Bug Summary</h3>\n<table class=\"simpletable\">\n"
          "<tr><td class=\"rowname\">File:</td><td>"
       << html::EscapeText(DirName)
       << html::EscapeText(Entry.getName())
       << "</td></tr>\n<tr><td class=\"rowname\">Warning:</td><td>"
          "<a href=\"#EndPath\">line "
       << LineNumber
       << ", column "
       << ColumnNumber
       << "</a><br />"
       << D.getVerboseDescription() << "</td></tr>\n";

    // The navigation across the extra notes pieces.
    unsigned NumExtraPieces = 0;
    for (const auto &Piece : path) {
      if (const auto *P = dyn_cast<PathDiagnosticNotePiece>(Piece.get())) {
        int LineNumber =
            P->getLocation().asLocation().getExpansionLineNumber();
        int ColumnNumber =
            P->getLocation().asLocation().getExpansionColumnNumber();
        ++NumExtraPieces;
        os << "<tr><td class=\"rowname\">Note:</td><td>"
           << "<a href=\"#Note" << NumExtraPieces << "\">line "
           << LineNumber << ", column " << ColumnNumber << "</a><br />"
           << P->getString() << "</td></tr>";
      }
    }

    // Output any other meta data.

    for (const std::string &Metadata :
         llvm::make_range(D.meta_begin(), D.meta_end())) {
      os << "<tr><td></td><td>" << html::EscapeText(Metadata) << "</td></tr>\n";
    }

    os << R"<<<(
</table>
<!-- REPORTSUMMARYEXTRA -->
<h3>Annotated Source Code</h3>
<p>Press <a href="#" onclick="toggleHelp(); return false;">'?'</a>
   to see keyboard shortcuts</p>
<input type="checkbox" class="spoilerhider" id="showinvocation" />
<label for="showinvocation" >Show analyzer invocation</label>
<div class="spoiler">clang -cc1 )<<<";
    os << html::EscapeText(DiagOpts.ToolInvocation);
    os << R"<<<(
</div>
<div id='tooltiphint' hidden="true">
  <p>Keyboard shortcuts: </p>
  <ul>
    <li>Use 'j/k' keys for keyboard navigation</li>
    <li>Use 'Shift+S' to show/hide relevant lines</li>
    <li>Use '?' to toggle this window</li>
  </ul>
  <a href="#" onclick="toggleHelp(); return false;">Close</a>
</div>
)<<<";

    R.InsertTextBefore(SMgr.getLocForStartOfFile(FID), os.str());
  }

  // Embed meta-data tags.
  {
    std::string s;
    llvm::raw_string_ostream os(s);

    StringRef BugDesc = D.getVerboseDescription();
    if (!BugDesc.empty())
      os << "\n<!-- BUGDESC " << BugDesc << " -->\n";

    StringRef BugType = D.getBugType();
    if (!BugType.empty())
      os << "\n<!-- BUGTYPE " << BugType << " -->\n";

    PathDiagnosticLocation UPDLoc = D.getUniqueingLoc();
    FullSourceLoc L(SMgr.getExpansionLoc(UPDLoc.isValid()
                                             ? UPDLoc.asLocation()
                                             : D.getLocation().asLocation()),
                    SMgr);

    StringRef BugCategory = D.getCategory();
    if (!BugCategory.empty())
      os << "\n<!-- BUGCATEGORY " << BugCategory << " -->\n";

    os << "\n<!-- BUGFILE " << DirName << Entry.getName() << " -->\n";

    os << "\n<!-- FILENAME " << llvm::sys::path::filename(Entry.getName()) << " -->\n";

    os  << "\n<!-- FUNCTIONNAME " <<  declName << " -->\n";

    os << "\n<!-- ISSUEHASHCONTENTOFLINEINCONTEXT " << getIssueHash(D, PP)
       << " -->\n";

    os << "\n<!-- BUGLINE "
       << LineNumber
       << " -->\n";

    os << "\n<!-- BUGCOLUMN "
      << ColumnNumber
      << " -->\n";

    os << "\n<!-- BUGPATHLENGTH " << getPathSizeWithoutArrows(path) << " -->\n";

    // Mark the end of the tags.
    os << "\n<!-- BUGMETAEND -->\n";

    // Insert the text.
    R.InsertTextBefore(SMgr.getLocForStartOfFile(FID), os.str());
  }

  html::AddHeaderFooterInternalBuiltinCSS(R, FID, Entry.getName());
}

StringRef HTMLDiagnostics::showHelpJavascript() {
  return R"<<<(
<script type='text/javascript'>

var toggleHelp = function() {
    var hint = document.querySelector("#tooltiphint");
    var attributeName = "hidden";
    if (hint.hasAttribute(attributeName)) {
      hint.removeAttribute(attributeName);
    } else {
      hint.setAttribute("hidden", "true");
    }
};
window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  if (event.key == "?") {
    toggleHelp();
  } else {
    return;
  }
  event.preventDefault();
});
</script>
)<<<";
}

static bool shouldDisplayPopUpRange(const SourceRange &Range) {
  return !(Range.getBegin().isMacroID() || Range.getEnd().isMacroID());
}

static void
HandlePopUpPieceStartTag(Rewriter &R,
                         const std::vector<SourceRange> &PopUpRanges) {
  for (const auto &Range : PopUpRanges) {
    if (!shouldDisplayPopUpRange(Range))
      continue;

    html::HighlightRange(R, Range.getBegin(), Range.getEnd(), "",
                         "<table class='variable_popup'><tbody>",
                         /*IsTokenRange=*/true);
  }
}

static void HandlePopUpPieceEndTag(Rewriter &R,
                                   const PathDiagnosticPopUpPiece &Piece,
                                   std::vector<SourceRange> &PopUpRanges,
                                   unsigned int LastReportedPieceIndex,
                                   unsigned int PopUpPieceIndex) {
  SmallString<256> Buf;
  llvm::raw_svector_ostream Out(Buf);

  SourceRange Range(Piece.getLocation().asRange());
  if (!shouldDisplayPopUpRange(Range))
    return;

  // Write out the path indices with a right arrow and the message as a row.
  Out << "<tr><td valign='top'><div class='PathIndex PathIndexPopUp'>"
      << LastReportedPieceIndex;

  // Also annotate the state transition with extra indices.
  Out << '.' << PopUpPieceIndex;

  Out << "</div></td><td>" << Piece.getString() << "</td></tr>";

  // If no report made at this range mark the variable and add the end tags.
  if (!llvm::is_contained(PopUpRanges, Range)) {
    // Store that we create a report at this range.
    PopUpRanges.push_back(Range);

    Out << "</tbody></table></span>";
    html::HighlightRange(R, Range.getBegin(), Range.getEnd(),
                         "<span class='variable'>", Buf.c_str(),
                         /*IsTokenRange=*/true);
  } else {
    // Otherwise inject just the new row at the end of the range.
    html::HighlightRange(R, Range.getBegin(), Range.getEnd(), "", Buf.c_str(),
                         /*IsTokenRange=*/true);
  }
}

void HTMLDiagnostics::RewriteFile(Rewriter &R, const PathPieces &path,
                                  FileID FID) {

  // Process the path.
  // Maintain the counts of extra note pieces separately.
  unsigned TotalPieces = getPathSizeWithoutArrows(path);
  unsigned TotalNotePieces =
      llvm::count_if(path, [](const PathDiagnosticPieceRef &p) {
        return isa<PathDiagnosticNotePiece>(*p);
      });
  unsigned PopUpPieceCount =
      llvm::count_if(path, [](const PathDiagnosticPieceRef &p) {
        return isa<PathDiagnosticPopUpPiece>(*p);
      });

  unsigned TotalRegularPieces = TotalPieces - TotalNotePieces - PopUpPieceCount;
  unsigned NumRegularPieces = TotalRegularPieces;
  unsigned NumNotePieces = TotalNotePieces;
  unsigned NumberOfArrows = 0;
  // Stores the count of the regular piece indices.
  std::map<int, int> IndexMap;
  ArrowMap ArrowIndices(TotalRegularPieces + 1);

  // Stores the different ranges where we have reported something.
  std::vector<SourceRange> PopUpRanges;
  for (const PathDiagnosticPieceRef &I : llvm::reverse(path)) {
    const auto &Piece = *I.get();

    if (isa<PathDiagnosticPopUpPiece>(Piece)) {
      ++IndexMap[NumRegularPieces];
    } else if (isa<PathDiagnosticNotePiece>(Piece)) {
      // This adds diagnostic bubbles, but not navigation.
      // Navigation through note pieces would be added later,
      // as a separate pass through the piece list.
      HandlePiece(R, FID, Piece, PopUpRanges, NumNotePieces, TotalNotePieces);
      --NumNotePieces;

    } else if (isArrowPiece(Piece)) {
      NumberOfArrows = ProcessControlFlowPiece(
          R, FID, cast<PathDiagnosticControlFlowPiece>(Piece), NumberOfArrows);
      ArrowIndices[NumRegularPieces] = NumberOfArrows;

    } else {
      HandlePiece(R, FID, Piece, PopUpRanges, NumRegularPieces,
                  TotalRegularPieces);
      --NumRegularPieces;
      ArrowIndices[NumRegularPieces] = ArrowIndices[NumRegularPieces + 1];
    }
  }
  ArrowIndices[0] = NumberOfArrows;

  // At this point ArrowIndices represent the following data structure:
  //   [a_0, a_1, ..., a_N]
  // where N is the number of events in the path.
  //
  // Then for every event with index i \in [0, N - 1], we can say that
  // arrows with indices \in [a_(i+1), a_i) correspond to that event.
  // We can say that because arrows with these indices appeared in the
  // path in between the i-th and the (i+1)-th events.
  assert(ArrowIndices.back() == 0 &&
         "No arrows should be after the last event");
  // This assertion also guarantees that all indices in are <= NumberOfArrows.
  assert(llvm::is_sorted(ArrowIndices, std::greater<unsigned>()) &&
         "Incorrect arrow indices map");

  // Secondary indexing if we are having multiple pop-ups between two notes.
  // (e.g. [(13) 'a' is 'true'];  [(13.1) 'b' is 'false'];  [(13.2) 'c' is...)
  NumRegularPieces = TotalRegularPieces;
  for (const PathDiagnosticPieceRef &I : llvm::reverse(path)) {
    const auto &Piece = *I.get();

    if (const auto *PopUpP = dyn_cast<PathDiagnosticPopUpPiece>(&Piece)) {
      int PopUpPieceIndex = IndexMap[NumRegularPieces];

      // Pop-up pieces needs the index of the last reported piece and its count
      // how many times we report to handle multiple reports on the same range.
      // This marks the variable, adds the </table> end tag and the message
      // (list element) as a row. The <table> start tag will be added after the
      // rows has been written out. Note: It stores every different range.
      HandlePopUpPieceEndTag(R, *PopUpP, PopUpRanges, NumRegularPieces,
                             PopUpPieceIndex);

      if (PopUpPieceIndex > 0)
        --IndexMap[NumRegularPieces];

    } else if (!isa<PathDiagnosticNotePiece>(Piece) && !isArrowPiece(Piece)) {
      --NumRegularPieces;
    }
  }

  // Add the <table> start tag of pop-up pieces based on the stored ranges.
  HandlePopUpPieceStartTag(R, PopUpRanges);

  // Add line numbers, header, footer, etc.
  html::EscapeText(R, FID);
  html::AddLineNumbers(R, FID);

  addArrowSVGs(R, FID, ArrowIndices);

  // If we have a preprocessor, relex the file and syntax highlight.
  // We might not have a preprocessor if we come from a deserialized AST file,
  // for example.
  html::SyntaxHighlight(R, FID, PP, RewriterCache);
  html::HighlightMacros(R, FID, PP, RewriterCache);
}

void HTMLDiagnostics::HandlePiece(Rewriter &R, FileID BugFileID,
                                  const PathDiagnosticPiece &P,
                                  const std::vector<SourceRange> &PopUpRanges,
                                  unsigned num, unsigned max) {
  // For now, just draw a box above the line in question, and emit the
  // warning.
  FullSourceLoc Pos = P.getLocation().asLocation();

  if (!Pos.isValid())
    return;

  SourceManager &SM = R.getSourceMgr();
  assert(&Pos.getManager() == &SM && "SourceManagers are different!");
  std::pair<FileID, unsigned> LPosInfo = SM.getDecomposedExpansionLoc(Pos);

  if (LPosInfo.first != BugFileID)
    return;

  llvm::MemoryBufferRef Buf = SM.getBufferOrFake(LPosInfo.first);
  const char *FileStart = Buf.getBufferStart();

  // Compute the column number.  Rewind from the current position to the start
  // of the line.
  unsigned ColNo = SM.getColumnNumber(LPosInfo.first, LPosInfo.second);
  const char *TokInstantiationPtr =Pos.getExpansionLoc().getCharacterData();
  const char *LineStart = TokInstantiationPtr-ColNo;

  // Compute LineEnd.
  const char *LineEnd = TokInstantiationPtr;
  const char *FileEnd = Buf.getBufferEnd();
  while (*LineEnd != '\n' && LineEnd != FileEnd)
    ++LineEnd;

  // Compute the margin offset by counting tabs and non-tabs.
  unsigned PosNo = 0;
  for (const char* c = LineStart; c != TokInstantiationPtr; ++c)
    PosNo += *c == '\t' ? 8 : 1;

  // Create the html for the message.

  const char *Kind = nullptr;
  bool IsNote = false;
  bool SuppressIndex = (max == 1);
  switch (P.getKind()) {
  case PathDiagnosticPiece::Event: Kind = "Event"; break;
  case PathDiagnosticPiece::ControlFlow: Kind = "Control"; break;
    // Setting Kind to "Control" is intentional.
  case PathDiagnosticPiece::Macro: Kind = "Control"; break;
  case PathDiagnosticPiece::Note:
    Kind = "Note";
    IsNote = true;
    SuppressIndex = true;
    break;
  case PathDiagnosticPiece::Call:
  case PathDiagnosticPiece::PopUp:
    llvm_unreachable("Calls and extra notes should already be handled");
  }

  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);

  os << "\n<tr><td class=\"num\"></td><td class=\"line\"><div id=\"";

  if (IsNote)
    os << "Note" << num;
  else if (num == max)
    os << "EndPath";
  else
    os << "Path" << num;

  os << "\" class=\"msg";
  if (Kind)
    os << " msg" << Kind;
  os << "\" style=\"margin-left:" << PosNo << "ex";

  // Output a maximum size.
  if (!isa<PathDiagnosticMacroPiece>(P)) {
    // Get the string and determining its maximum substring.
    const auto &Msg = P.getString();
    unsigned max_token = 0;
    unsigned cnt = 0;
    unsigned len = Msg.size();

    for (char C : Msg)
      switch (C) {
      default:
        ++cnt;
        continue;
      case ' ':
      case '\t':
      case '\n':
        if (cnt > max_token) max_token = cnt;
        cnt = 0;
      }

    if (cnt > max_token)
      max_token = cnt;

    // Determine the approximate size of the message bubble in em.
    unsigned em;
    const unsigned max_line = 120;

    if (max_token >= max_line)
      em = max_token / 2;
    else {
      unsigned characters = max_line;
      unsigned lines = len / max_line;

      if (lines > 0) {
        for (; characters > max_token; --characters)
          if (len / characters > lines) {
            ++characters;
            break;
          }
      }

      em = characters / 2;
    }

    if (em < max_line/2)
      os << "; max-width:" << em << "em";
  }
  else
    os << "; max-width:100em";

  os << "\">";

  if (!SuppressIndex) {
    os << "<table class=\"msgT\"><tr><td valign=\"top\">";
    os << "<div class=\"PathIndex";
    if (Kind) os << " PathIndex" << Kind;
    os << "\">" << num << "</div>";

    if (num > 1) {
      os << "</td><td><div class=\"PathNav\"><a href=\"#Path"
         << (num - 1)
         << "\" title=\"Previous event ("
         << (num - 1)
         << ")\">&#x2190;</a></div>";
    }

    os << "</td><td>";
  }

  if (const auto *MP = dyn_cast<PathDiagnosticMacroPiece>(&P)) {
    os << "Within the expansion of the macro '";

    // Get the name of the macro by relexing it.
    {
      FullSourceLoc L = MP->getLocation().asLocation().getExpansionLoc();
      assert(L.isFileID());
      StringRef BufferInfo = L.getBufferData();
      std::pair<FileID, unsigned> LocInfo = L.getDecomposedLoc();
      const char* MacroName = LocInfo.second + BufferInfo.data();
      Lexer rawLexer(SM.getLocForStartOfFile(LocInfo.first), PP.getLangOpts(),
                     BufferInfo.begin(), MacroName, BufferInfo.end());

      Token TheTok;
      rawLexer.LexFromRawLexer(TheTok);
      for (unsigned i = 0, n = TheTok.getLength(); i < n; ++i)
        os << MacroName[i];
    }

    os << "':\n";

    if (!SuppressIndex) {
      os << "</td>";
      if (num < max) {
        os << "<td><div class=\"PathNav\"><a href=\"#";
        if (num == max - 1)
          os << "EndPath";
        else
          os << "Path" << (num + 1);
        os << "\" title=\"Next event ("
        << (num + 1)
        << ")\">&#x2192;</a></div></td>";
      }

      os << "</tr></table>";
    }

    // Within a macro piece.  Write out each event.
    ProcessMacroPiece(os, *MP, 0);
  }
  else {
    os << html::EscapeText(P.getString());

    if (!SuppressIndex) {
      os << "</td>";
      if (num < max) {
        os << "<td><div class=\"PathNav\"><a href=\"#";
        if (num == max - 1)
          os << "EndPath";
        else
          os << "Path" << (num + 1);
        os << "\" title=\"Next event ("
           << (num + 1)
           << ")\">&#x2192;</a></div></td>";
      }

      os << "</tr></table>";
    }
  }

  os << "</div></td></tr>";

  // Insert the new html.
  unsigned DisplayPos = LineEnd - FileStart;
  SourceLocation Loc =
    SM.getLocForStartOfFile(LPosInfo.first).getLocWithOffset(DisplayPos);

  R.InsertTextBefore(Loc, os.str());

  // Now highlight the ranges.
  ArrayRef<SourceRange> Ranges = P.getRanges();
  for (const auto &Range : Ranges) {
    // If we have already highlighted the range as a pop-up there is no work.
    if (llvm::is_contained(PopUpRanges, Range))
      continue;

    HighlightRange(R, LPosInfo.first, Range);
  }
}

static void EmitAlphaCounter(raw_ostream &os, unsigned n) {
  unsigned x = n % ('z' - 'a');
  n /= 'z' - 'a';

  if (n > 0)
    EmitAlphaCounter(os, n);

  os << char('a' + x);
}

unsigned HTMLDiagnostics::ProcessMacroPiece(raw_ostream &os,
                                            const PathDiagnosticMacroPiece& P,
                                            unsigned num) {
  for (const auto &subPiece : P.subPieces) {
    if (const auto *MP = dyn_cast<PathDiagnosticMacroPiece>(subPiece.get())) {
      num = ProcessMacroPiece(os, *MP, num);
      continue;
    }

    if (const auto *EP = dyn_cast<PathDiagnosticEventPiece>(subPiece.get())) {
      os << "<div class=\"msg msgEvent\" style=\"width:94%; "
            "margin-left:5px\">"
            "<table class=\"msgT\"><tr>"
            "<td valign=\"top\"><div class=\"PathIndex PathIndexEvent\">";
      EmitAlphaCounter(os, num++);
      os << "</div></td><td valign=\"top\">"
         << html::EscapeText(EP->getString())
         << "</td></tr></table></div>\n";
    }
  }

  return num;
}

void HTMLDiagnostics::addArrowSVGs(Rewriter &R, FileID BugFileID,
                                   const ArrowMap &ArrowIndices) {
  std::string S;
  llvm::raw_string_ostream OS(S);

  OS << R"<<<(
<style type="text/css">
  svg {
      position:absolute;
      top:0;
      left:0;
      height:100%;
      width:100%;
      pointer-events: none;
      overflow: visible
  }
  .arrow {
      stroke-opacity: 0.2;
      stroke-width: 1;
      marker-end: url(#arrowhead);
  }

  .arrow.selected {
      stroke-opacity: 0.6;
      stroke-width: 2;
      marker-end: url(#arrowheadSelected);
  }

  .arrowhead {
      orient: auto;
      stroke: none;
      opacity: 0.6;
      fill: blue;
  }
</style>
<svg xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="arrowheadSelected" class="arrowhead" opacity="0.6"
            viewBox="0 0 10 10" refX="3" refY="5"
            markerWidth="4" markerHeight="4">
      <path d="M 0 0 L 10 5 L 0 10 z" />
    </marker>
    <marker id="arrowhead" class="arrowhead" opacity="0.2"
            viewBox="0 0 10 10" refX="3" refY="5"
            markerWidth="4" markerHeight="4">
      <path d="M 0 0 L 10 5 L 0 10 z" />
    </marker>
  </defs>
  <g id="arrows" fill="none" stroke="blue" visibility="hidden">
)<<<";

  for (unsigned Index : llvm::seq(0u, ArrowIndices.getTotalNumberOfArrows())) {
    OS << "    <path class=\"arrow\" id=\"arrow" << Index << "\"/>\n";
  }

  OS << R"<<<(
  </g>
</svg>
<script type='text/javascript'>
const arrowIndices = )<<<";

  OS << ArrowIndices << "\n</script>\n";

  R.InsertTextBefore(R.getSourceMgr().getLocForStartOfFile(BugFileID),
                     OS.str());
}

std::string getSpanBeginForControl(const char *ClassName, unsigned Index) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  OS << "<span id=\"" << ClassName << Index << "\">";
  return Result;
}

std::string getSpanBeginForControlStart(unsigned Index) {
  return getSpanBeginForControl("start", Index);
}

std::string getSpanBeginForControlEnd(unsigned Index) {
  return getSpanBeginForControl("end", Index);
}

unsigned HTMLDiagnostics::ProcessControlFlowPiece(
    Rewriter &R, FileID BugFileID, const PathDiagnosticControlFlowPiece &P,
    unsigned Number) {
  for (const PathDiagnosticLocationPair &LPair : P) {
    std::string Start = getSpanBeginForControlStart(Number),
                End = getSpanBeginForControlEnd(Number++);

    HighlightRange(R, BugFileID, LPair.getStart().asRange().getBegin(),
                   Start.c_str());
    HighlightRange(R, BugFileID, LPair.getEnd().asRange().getBegin(),
                   End.c_str());
  }

  return Number;
}

void HTMLDiagnostics::HighlightRange(Rewriter& R, FileID BugFileID,
                                     SourceRange Range,
                                     const char *HighlightStart,
                                     const char *HighlightEnd) {
  SourceManager &SM = R.getSourceMgr();
  const LangOptions &LangOpts = R.getLangOpts();

  SourceLocation InstantiationStart = SM.getExpansionLoc(Range.getBegin());
  unsigned StartLineNo = SM.getExpansionLineNumber(InstantiationStart);

  SourceLocation InstantiationEnd = SM.getExpansionLoc(Range.getEnd());
  unsigned EndLineNo = SM.getExpansionLineNumber(InstantiationEnd);

  if (EndLineNo < StartLineNo)
    return;

  if (SM.getFileID(InstantiationStart) != BugFileID ||
      SM.getFileID(InstantiationEnd) != BugFileID)
    return;

  // Compute the column number of the end.
  unsigned EndColNo = SM.getExpansionColumnNumber(InstantiationEnd);
  unsigned OldEndColNo = EndColNo;

  if (EndColNo) {
    // Add in the length of the token, so that we cover multi-char tokens.
    EndColNo += Lexer::MeasureTokenLength(Range.getEnd(), SM, LangOpts)-1;
  }

  // Highlight the range.  Make the span tag the outermost tag for the
  // selected range.

  SourceLocation E =
    InstantiationEnd.getLocWithOffset(EndColNo - OldEndColNo);

  html::HighlightRange(R, InstantiationStart, E, HighlightStart, HighlightEnd);
}

StringRef HTMLDiagnostics::generateKeyboardNavigationJavascript() {
  return R"<<<(
<script type='text/javascript'>
var digitMatcher = new RegExp("[0-9]+");

var querySelectorAllArray = function(selector) {
  return Array.prototype.slice.call(
    document.querySelectorAll(selector));
}

document.addEventListener("DOMContentLoaded", function() {
    querySelectorAllArray(".PathNav > a").forEach(
        function(currentValue, currentIndex) {
            var hrefValue = currentValue.getAttribute("href");
            currentValue.onclick = function() {
                scrollTo(document.querySelector(hrefValue));
                return false;
            };
        });
});

var findNum = function() {
    var s = document.querySelector(".msg.selected");
    if (!s || s.id == "EndPath") {
        return 0;
    }
    var out = parseInt(digitMatcher.exec(s.id)[0]);
    return out;
};

var classListAdd = function(el, theClass) {
  if(!el.className.baseVal)
    el.className += " " + theClass;
  else
    el.className.baseVal += " " + theClass;
};

var classListRemove = function(el, theClass) {
  var className = (!el.className.baseVal) ?
      el.className : el.className.baseVal;
    className = className.replace(" " + theClass, "");
  if(!el.className.baseVal)
    el.className = className;
  else
    el.className.baseVal = className;
};

var scrollTo = function(el) {
    querySelectorAllArray(".selected").forEach(function(s) {
      classListRemove(s, "selected");
    });
    classListAdd(el, "selected");
    window.scrollBy(0, el.getBoundingClientRect().top -
        (window.innerHeight / 2));
    highlightArrowsForSelectedEvent();
};

var move = function(num, up, numItems) {
  if (num == 1 && up || num == numItems - 1 && !up) {
    return 0;
  } else if (num == 0 && up) {
    return numItems - 1;
  } else if (num == 0 && !up) {
    return 1 % numItems;
  }
  return up ? num - 1 : num + 1;
}

var numToId = function(num) {
  if (num == 0) {
    return document.getElementById("EndPath")
  }
  return document.getElementById("Path" + num);
};

var navigateTo = function(up) {
  var numItems = document.querySelectorAll(
      ".line > .msgEvent, .line > .msgControl").length;
  var currentSelected = findNum();
  var newSelected = move(currentSelected, up, numItems);
  var newEl = numToId(newSelected, numItems);

  // Scroll element into center.
  scrollTo(newEl);
};

window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  // key 'j'
  if (event.keyCode == 74) {
    navigateTo(/*up=*/false);
  // key 'k'
  } else if (event.keyCode == 75) {
    navigateTo(/*up=*/true);
  } else {
    return;
  }
  event.preventDefault();
}, true);
</script>
  )<<<";
}

StringRef HTMLDiagnostics::generateArrowDrawingJavascript() {
  return R"<<<(
<script type='text/javascript'>
// Return range of numbers from a range [lower, upper).
function range(lower, upper) {
  var array = [];
  for (var i = lower; i <= upper; ++i) {
      array.push(i);
  }
  return array;
}

var getRelatedArrowIndices = function(pathId) {
  // HTML numeration of events is a bit different than it is in the path.
  // Everything is rotated one step to the right, so the last element
  // (error diagnostic) has index 0.
  if (pathId == 0) {
    // arrowIndices has at least 2 elements
    pathId = arrowIndices.length - 1;
  }

  return range(arrowIndices[pathId], arrowIndices[pathId - 1]);
}

var highlightArrowsForSelectedEvent = function() {
  const selectedNum = findNum();
  const arrowIndicesToHighlight = getRelatedArrowIndices(selectedNum);
  arrowIndicesToHighlight.forEach((index) => {
    var arrow = document.querySelector("#arrow" + index);
    if(arrow) {
      classListAdd(arrow, "selected")
    }
  });
}

var getAbsoluteBoundingRect = function(element) {
  const relative = element.getBoundingClientRect();
  return {
    left: relative.left + window.pageXOffset,
    right: relative.right + window.pageXOffset,
    top: relative.top + window.pageYOffset,
    bottom: relative.bottom + window.pageYOffset,
    height: relative.height,
    width: relative.width
  };
}

var drawArrow = function(index) {
  // This function is based on the great answer from SO:
  //   https://stackoverflow.com/a/39575674/11582326
  var start = document.querySelector("#start" + index);
  var end   = document.querySelector("#end" + index);
  var arrow = document.querySelector("#arrow" + index);

  var startRect = getAbsoluteBoundingRect(start);
  var endRect   = getAbsoluteBoundingRect(end);

  // It is an arrow from a token to itself, no need to visualize it.
  if (startRect.top == endRect.top &&
      startRect.left == endRect.left)
    return;

  // Each arrow is a very simple Bézier curve, with two nodes and
  // two handles.  So, we need to calculate four points in the window:
  //   * start node
  var posStart    = { x: 0, y: 0 };
  //   * end node
  var posEnd      = { x: 0, y: 0 };
  //   * handle for the start node
  var startHandle = { x: 0, y: 0 };
  //   * handle for the end node
  var endHandle   = { x: 0, y: 0 };
  // One can visualize it as follows:
  //
  //         start handle
  //        /
  //       X"""_.-""""X
  //         .'        \
  //        /           start node
  //       |
  //       |
  //       |      end node
  //        \    /
  //         `->X
  //        X-'
  //         \
  //          end handle
  //
  // NOTE: (0, 0) is the top left corner of the window.

  // We have 3 similar, but still different scenarios to cover:
  //
  //   1. Two tokens on different lines.
  //             -xxx
  //           /
  //           \
  //             -> xxx
  //      In this situation, we draw arrow on the left curving to the left.
  //   2. Two tokens on the same line, and the destination is on the right.
  //             ____
  //            /    \
  //           /      V
  //        xxx        xxx
  //      In this situation, we draw arrow above curving upwards.
  //   3. Two tokens on the same line, and the destination is on the left.
  //        xxx        xxx
  //           ^      /
  //            \____/
  //      In this situation, we draw arrow below curving downwards.
  const onDifferentLines = startRect.top <= endRect.top - 5 ||
    startRect.top >= endRect.top + 5;
  const leftToRight = startRect.left < endRect.left;

  // NOTE: various magic constants are chosen empirically for
  //       better positioning and look
  if (onDifferentLines) {
    // Case #1
    const topToBottom = startRect.top < endRect.top;
    posStart.x = startRect.left - 1;
    // We don't want to start it at the top left corner of the token,
    // it doesn't feel like this is where the arrow comes from.
    // For this reason, we start it in the middle of the left side
    // of the token.
    posStart.y = startRect.top + startRect.height / 2;

    // End node has arrow head and we give it a bit more space.
    posEnd.x = endRect.left - 4;
    posEnd.y = endRect.top;

    // Utility object with x and y offsets for handles.
    var curvature = {
      // We want bottom-to-top arrow to curve a bit more, so it doesn't
      // overlap much with top-to-bottom curves (much more frequent).
      x: topToBottom ? 15 : 25,
      y: Math.min((posEnd.y - posStart.y) / 3, 10)
    }

    // When destination is on the different line, we can make a
    // curvier arrow because we have space for it.
    // So, instead of using
    //
    //   startHandle.x = posStart.x - curvature.x
    //   endHandle.x   = posEnd.x - curvature.x
    //
    // We use the leftmost of these two values for both handles.
    startHandle.x = Math.min(posStart.x, posEnd.x) - curvature.x;
    endHandle.x = startHandle.x;

    // Curving downwards from the start node...
    startHandle.y = posStart.y + curvature.y;
    // ... and upwards from the end node.
    endHandle.y = posEnd.y - curvature.y;

  } else if (leftToRight) {
    // Case #2
    // Starting from the top right corner...
    posStart.x = startRect.right - 1;
    posStart.y = startRect.top;

    // ...and ending at the top left corner of the end token.
    posEnd.x = endRect.left + 1;
    posEnd.y = endRect.top - 1;

    // Utility object with x and y offsets for handles.
    var curvature = {
      x: Math.min((posEnd.x - posStart.x) / 3, 15),
      y: 5
    }

    // Curving to the right...
    startHandle.x = posStart.x + curvature.x;
    // ... and upwards from the start node.
    startHandle.y = posStart.y - curvature.y;

    // And to the left...
    endHandle.x = posEnd.x - curvature.x;
    // ... and upwards from the end node.
    endHandle.y = posEnd.y - curvature.y;

  } else {
    // Case #3
    // Starting from the bottom right corner...
    posStart.x = startRect.right;
    posStart.y = startRect.bottom;

    // ...and ending also at the bottom right corner, but of the end token.
    posEnd.x = endRect.right - 1;
    posEnd.y = endRect.bottom + 1;

    // Utility object with x and y offsets for handles.
    var curvature = {
      x: Math.min((posStart.x - posEnd.x) / 3, 15),
      y: 5
    }

    // Curving to the left...
    startHandle.x = posStart.x - curvature.x;
    // ... and downwards from the start node.
    startHandle.y = posStart.y + curvature.y;

    // And to the right...
    endHandle.x = posEnd.x + curvature.x;
    // ... and downwards from the end node.
    endHandle.y = posEnd.y + curvature.y;
  }

  // Put it all together into a path.
  // More information on the format:
  //   https://developer.mozilla.org/en-US/docs/Web/SVG/Tutorial/Paths
  var pathStr = "M" + posStart.x + "," + posStart.y + " " +
    "C" + startHandle.x + "," + startHandle.y + " " +
    endHandle.x + "," + endHandle.y + " " +
    posEnd.x + "," + posEnd.y;

  arrow.setAttribute("d", pathStr);
};

var drawArrows = function() {
  const numOfArrows = document.querySelectorAll("path[id^=arrow]").length;
  for (var i = 0; i < numOfArrows; ++i) {
    drawArrow(i);
  }
}

var toggleArrows = function(event) {
  const arrows = document.querySelector("#arrows");
  if (event.target.checked) {
    arrows.setAttribute("visibility", "visible");
  } else {
    arrows.setAttribute("visibility", "hidden");
  }
}

window.addEventListener("resize", drawArrows);
document.addEventListener("DOMContentLoaded", function() {
  // Whenever we show invocation, locations change, i.e. we
  // need to redraw arrows.
  document
    .querySelector('input[id="showinvocation"]')
    .addEventListener("click", drawArrows);
  // Hiding irrelevant lines also should cause arrow rerender.
  document
    .querySelector('input[name="showCounterexample"]')
    .addEventListener("change", drawArrows);
  document
    .querySelector('input[name="showArrows"]')
    .addEventListener("change", toggleArrows);
  drawArrows();
  // Default highlighting for the last event.
  highlightArrowsForSelectedEvent();
});
</script>
  )<<<";
}
