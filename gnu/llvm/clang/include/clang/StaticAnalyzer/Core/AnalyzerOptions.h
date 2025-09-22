//===- AnalyzerOptions.h - Analysis Engine Options --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines various options for the static analyzer that are set
// by the frontend and are consulted throughout the analyzer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_ANALYZEROPTIONS_H
#define LLVM_CLANG_STATICANALYZER_CORE_ANALYZEROPTIONS_H

#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <utility>
#include <vector>

namespace clang {

namespace ento {

class CheckerBase;

} // namespace ento

/// AnalysisConstraints - Set of available constraint models.
enum AnalysisConstraints {
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATFN) NAME##Model,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumConstraints
};

/// AnalysisDiagClients - Set of available diagnostic clients for rendering
///  analysis results.
enum AnalysisDiagClients {
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATFN) PD_##NAME,
#include "clang/StaticAnalyzer/Core/Analyses.def"
PD_NONE,
NUM_ANALYSIS_DIAG_CLIENTS
};

/// AnalysisPurgeModes - Set of available strategies for dead symbol removal.
enum AnalysisPurgeMode {
#define ANALYSIS_PURGE(NAME, CMDFLAG, DESC) NAME,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumPurgeModes
};

/// AnalysisInlineFunctionSelection - Set of inlining function selection heuristics.
enum AnalysisInliningMode {
#define ANALYSIS_INLINING_MODE(NAME, CMDFLAG, DESC) NAME,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumInliningModes
};

/// Describes the different kinds of C++ member functions which can be
/// considered for inlining by the analyzer.
///
/// These options are cumulative; enabling one kind of member function will
/// enable all kinds with lower enum values.
enum CXXInlineableMemberKind {
  // Uninitialized = 0,

  /// A dummy mode in which no C++ inlining is enabled.
  CIMK_None,

  /// Refers to regular member function and operator calls.
  CIMK_MemberFunctions,

  /// Refers to constructors (implicit or explicit).
  ///
  /// Note that a constructor will not be inlined if the corresponding
  /// destructor is non-trivial.
  CIMK_Constructors,

  /// Refers to destructors (implicit or explicit).
  CIMK_Destructors
};

/// Describes the different modes of inter-procedural analysis.
enum IPAKind {
  /// Perform only intra-procedural analysis.
  IPAK_None = 1,

  /// Inline C functions and blocks when their definitions are available.
  IPAK_BasicInlining = 2,

  /// Inline callees(C, C++, ObjC) when their definitions are available.
  IPAK_Inlining = 3,

  /// Enable inlining of dynamically dispatched methods.
  IPAK_DynamicDispatch = 4,

  /// Enable inlining of dynamically dispatched methods, bifurcate paths when
  /// exact type info is unavailable.
  IPAK_DynamicDispatchBifurcate = 5
};

enum class ExplorationStrategyKind {
  DFS,
  BFS,
  UnexploredFirst,
  UnexploredFirstQueue,
  UnexploredFirstLocationQueue,
  BFSBlockDFSContents,
};

/// Describes the kinds for high-level analyzer mode.
enum UserModeKind {
  /// Perform shallow but fast analyzes.
  UMK_Shallow = 1,

  /// Perform deep analyzes.
  UMK_Deep = 2
};

enum class CTUPhase1InliningKind { None, Small, All };

/// Stores options for the analyzer from the command line.
///
/// Some options are frontend flags (e.g.: -analyzer-output), but some are
/// analyzer configuration options, which are preceded by -analyzer-config
/// (e.g.: -analyzer-config notes-as-events=true).
///
/// If you'd like to add a new frontend flag, add it to
/// include/clang/Driver/CC1Options.td, add a new field to store the value of
/// that flag in this class, and initialize it in
/// lib/Frontend/CompilerInvocation.cpp.
///
/// If you'd like to add a new non-checker configuration, register it in
/// include/clang/StaticAnalyzer/Core/AnalyzerOptions.def, and refer to the
/// top of the file for documentation.
///
/// If you'd like to add a new checker option, call getChecker*Option()
/// whenever.
///
/// Some of the options are controlled by raw frontend flags for no good reason,
/// and should be eventually converted into -analyzer-config flags. New analyzer
/// options should not be implemented as frontend flags. Frontend flags still
/// make sense for things that do not affect the actual analysis.
class AnalyzerOptions : public RefCountedBase<AnalyzerOptions> {
public:
  using ConfigTable = llvm::StringMap<std::string>;

  /// Retrieves the list of checkers generated from Checkers.td. This doesn't
  /// contain statically linked but non-generated checkers and plugin checkers!
  static std::vector<StringRef>
  getRegisteredCheckers(bool IncludeExperimental = false);

  /// Retrieves the list of packages generated from Checkers.td. This doesn't
  /// contain statically linked but non-generated packages and plugin packages!
  static std::vector<StringRef>
  getRegisteredPackages(bool IncludeExperimental = false);

  /// Convenience function for printing options or checkers and their
  /// description in a formatted manner. If \p MinLineWidth is set to 0, no line
  /// breaks are introduced for the description.
  ///
  /// Format, depending whether the option name's length is less than
  /// \p EntryWidth:
  ///
  ///   <padding>EntryName<padding>Description
  ///   <---------padding--------->Description
  ///   <---------padding--------->Description
  ///
  ///   <padding>VeryVeryLongEntryName
  ///   <---------padding--------->Description
  ///   <---------padding--------->Description
  ///   ^~~~~~~~~InitialPad
  ///            ^~~~~~~~~~~~~~~~~~EntryWidth
  ///   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~MinLineWidth
  static void printFormattedEntry(llvm::raw_ostream &Out,
                                  std::pair<StringRef, StringRef> EntryDescPair,
                                  size_t InitialPad, size_t EntryWidth,
                                  size_t MinLineWidth = 0);

  /// Pairs of checker/package name and enable/disable.
  std::vector<std::pair<std::string, bool>> CheckersAndPackages;

  /// Vector of checker/package names which will not emit warnings.
  std::vector<std::string> SilencedCheckersAndPackages;

  /// A key-value table of use-specified configuration values.
  // TODO: This shouldn't be public.
  ConfigTable Config;
  AnalysisConstraints AnalysisConstraintsOpt = RangeConstraintsModel;
  AnalysisDiagClients AnalysisDiagOpt = PD_HTML;
  AnalysisPurgeMode AnalysisPurgeOpt = PurgeStmt;

  std::string AnalyzeSpecificFunction;

  /// File path to which the exploded graph should be dumped.
  std::string DumpExplodedGraphTo;

  /// Store full compiler invocation for reproducible instructions in the
  /// generated report.
  std::string FullCompilerInvocation;

  /// The maximum number of times the analyzer visits a block.
  unsigned maxBlockVisitOnPath;

  /// Disable all analyzer checkers.
  ///
  /// This flag allows one to disable analyzer checkers on the code processed by
  /// the given analysis consumer. Note, the code will get parsed and the
  /// command-line options will get checked.
  unsigned DisableAllCheckers : 1;

  unsigned ShowCheckerHelp : 1;
  unsigned ShowCheckerHelpAlpha : 1;
  unsigned ShowCheckerHelpDeveloper : 1;

  unsigned ShowCheckerOptionList : 1;
  unsigned ShowCheckerOptionAlphaList : 1;
  unsigned ShowCheckerOptionDeveloperList : 1;

  unsigned ShowEnabledCheckerList : 1;
  unsigned ShowConfigOptionsList : 1;
  unsigned ShouldEmitErrorsOnInvalidConfigValue : 1;
  unsigned AnalyzeAll : 1;
  unsigned AnalyzerDisplayProgress : 1;
  unsigned AnalyzerNoteAnalysisEntryPoints : 1;

  unsigned eagerlyAssumeBinOpBifurcation : 1;

  unsigned TrimGraph : 1;
  unsigned visualizeExplodedGraphWithGraphViz : 1;
  unsigned UnoptimizedCFG : 1;
  unsigned PrintStats : 1;

  /// Do not re-analyze paths leading to exhausted nodes with a different
  /// strategy. We get better code coverage when retry is enabled.
  unsigned NoRetryExhausted : 1;

  /// Emit analyzer warnings as errors.
  bool AnalyzerWerror : 1;

  /// The inlining stack depth limit.
  unsigned InlineMaxStackDepth;

  /// The mode of function selection used during inlining.
  AnalysisInliningMode InliningMode = NoRedundancy;

  // Create a field for each -analyzer-config option.
#define ANALYZER_OPTION_DEPENDS_ON_USER_MODE(TYPE, NAME, CMDFLAG, DESC,        \
                                             SHALLOW_VAL, DEEP_VAL)            \
  ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, SHALLOW_VAL)

#define ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, DEFAULT_VAL)                \
  TYPE NAME;

#include "clang/StaticAnalyzer/Core/AnalyzerOptions.def"
#undef ANALYZER_OPTION
#undef ANALYZER_OPTION_DEPENDS_ON_USER_MODE

  bool isUnknownAnalyzerConfig(llvm::StringRef Name) {
    static std::vector<llvm::StringLiteral> AnalyzerConfigCmdFlags = []() {
      // Create an array of all -analyzer-config command line options.
      std::vector<llvm::StringLiteral> AnalyzerConfigCmdFlags = {
#define ANALYZER_OPTION_DEPENDS_ON_USER_MODE(TYPE, NAME, CMDFLAG, DESC,        \
                                             SHALLOW_VAL, DEEP_VAL)            \
  ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, SHALLOW_VAL)

#define ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, DEFAULT_VAL)                \
  llvm::StringLiteral(CMDFLAG),

#include "clang/StaticAnalyzer/Core/AnalyzerOptions.def"
#undef ANALYZER_OPTION
#undef ANALYZER_OPTION_DEPENDS_ON_USER_MODE
      };
      // FIXME: Sort this at compile-time when we get constexpr sort (C++20).
      llvm::sort(AnalyzerConfigCmdFlags);
      return AnalyzerConfigCmdFlags;
    }();

    return !std::binary_search(AnalyzerConfigCmdFlags.begin(),
                               AnalyzerConfigCmdFlags.end(), Name);
  }

  AnalyzerOptions()
      : DisableAllCheckers(false), ShowCheckerHelp(false),
        ShowCheckerHelpAlpha(false), ShowCheckerHelpDeveloper(false),
        ShowCheckerOptionList(false), ShowCheckerOptionAlphaList(false),
        ShowCheckerOptionDeveloperList(false), ShowEnabledCheckerList(false),
        ShowConfigOptionsList(false),
        ShouldEmitErrorsOnInvalidConfigValue(false), AnalyzeAll(false),
        AnalyzerDisplayProgress(false), AnalyzerNoteAnalysisEntryPoints(false),
        eagerlyAssumeBinOpBifurcation(false), TrimGraph(false),
        visualizeExplodedGraphWithGraphViz(false), UnoptimizedCFG(false),
        PrintStats(false), NoRetryExhausted(false), AnalyzerWerror(false) {}

  /// Interprets an option's string value as a boolean. The "true" string is
  /// interpreted as true and the "false" string is interpreted as false.
  ///
  /// If an option value is not provided, returns the given \p DefaultVal.
  /// @param [in] CheckerName The *full name* of the checker. One may retrieve
  /// this from the checker object's field \c Name, or through \c
  /// CheckerManager::getCurrentCheckerName within the checker's registry
  /// function.
  /// Checker options are retrieved in the following format:
  /// `-analyzer-config CheckerName:OptionName=Value.
  /// @param [in] OptionName Name for option to retrieve.
  /// @param [in] SearchInParents If set to true and the searched option was not
  /// specified for the given checker the options for the parent packages will
  /// be searched as well. The inner packages take precedence over the outer
  /// ones.
  bool getCheckerBooleanOption(StringRef CheckerName, StringRef OptionName,
                               bool SearchInParents = false) const;

  bool getCheckerBooleanOption(const ento::CheckerBase *C, StringRef OptionName,
                               bool SearchInParents = false) const;

  /// Interprets an option's string value as an integer value.
  ///
  /// If an option value is not provided, returns the given \p DefaultVal.
  /// @param [in] CheckerName The *full name* of the checker. One may retrieve
  /// this from the checker object's field \c Name, or through \c
  /// CheckerManager::getCurrentCheckerName within the checker's registry
  /// function.
  /// Checker options are retrieved in the following format:
  /// `-analyzer-config CheckerName:OptionName=Value.
  /// @param [in] OptionName Name for option to retrieve.
  /// @param [in] SearchInParents If set to true and the searched option was not
  /// specified for the given checker the options for the parent packages will
  /// be searched as well. The inner packages take precedence over the outer
  /// ones.
  int getCheckerIntegerOption(StringRef CheckerName, StringRef OptionName,
                              bool SearchInParents = false) const;

  int getCheckerIntegerOption(const ento::CheckerBase *C, StringRef OptionName,
                              bool SearchInParents = false) const;

  /// Query an option's string value.
  ///
  /// If an option value is not provided, returns the given \p DefaultVal.
  /// @param [in] CheckerName The *full name* of the checker. One may retrieve
  /// this from the checker object's field \c Name, or through \c
  /// CheckerManager::getCurrentCheckerName within the checker's registry
  /// function.
  /// Checker options are retrieved in the following format:
  /// `-analyzer-config CheckerName:OptionName=Value.
  /// @param [in] OptionName Name for option to retrieve.
  /// @param [in] SearchInParents If set to true and the searched option was not
  /// specified for the given checker the options for the parent packages will
  /// be searched as well. The inner packages take precedence over the outer
  /// ones.
  StringRef getCheckerStringOption(StringRef CheckerName, StringRef OptionName,
                                   bool SearchInParents = false) const;

  StringRef getCheckerStringOption(const ento::CheckerBase *C,
                                   StringRef OptionName,
                                   bool SearchInParents = false) const;

  ExplorationStrategyKind getExplorationStrategy() const;
  CTUPhase1InliningKind getCTUPhase1Inlining() const;

  /// Returns the inter-procedural analysis mode.
  IPAKind getIPAMode() const;

  /// Returns the option controlling which C++ member functions will be
  /// considered for inlining.
  ///
  /// This is controlled by the 'c++-inlining' config option.
  ///
  /// \sa CXXMemberInliningMode
  bool mayInlineCXXMemberFunction(CXXInlineableMemberKind K) const;

  ento::PathDiagnosticConsumerOptions getDiagOpts() const {
    return {FullCompilerInvocation,
            ShouldDisplayMacroExpansions,
            ShouldSerializeStats,
            // The stable report filename option is deprecated because
            // file names are now always stable. Now the old option acts as
            // an alias to the new verbose filename option because this
            // closely mimics the behavior under the old option.
            ShouldWriteStableReportFilename || ShouldWriteVerboseReportFilename,
            AnalyzerWerror,
            ShouldApplyFixIts,
            ShouldDisplayCheckerNameForText};
  }
};

using AnalyzerOptionsRef = IntrusiveRefCntPtr<AnalyzerOptions>;

//===----------------------------------------------------------------------===//
// We'll use AnalyzerOptions in the frontend, but we can't link the frontend
// with clangStaticAnalyzerCore, because clangStaticAnalyzerCore depends on
// clangFrontend.
//
// For this reason, implement some methods in this header file.
//===----------------------------------------------------------------------===//

inline std::vector<StringRef>
AnalyzerOptions::getRegisteredCheckers(bool IncludeExperimental) {
  static constexpr llvm::StringLiteral StaticAnalyzerCheckerNames[] = {
#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI, IS_HIDDEN)                 \
  llvm::StringLiteral(FULLNAME),
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS
  };
  std::vector<StringRef> Checkers;
  for (StringRef CheckerName : StaticAnalyzerCheckerNames) {
    if (!CheckerName.starts_with("debug.") &&
        (IncludeExperimental || !CheckerName.starts_with("alpha.")))
      Checkers.push_back(CheckerName);
  }
  return Checkers;
}

inline std::vector<StringRef>
AnalyzerOptions::getRegisteredPackages(bool IncludeExperimental) {
  static constexpr llvm::StringLiteral StaticAnalyzerPackageNames[] = {
#define GET_PACKAGES
#define PACKAGE(FULLNAME) llvm::StringLiteral(FULLNAME),
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef PACKAGE
#undef GET_PACKAGES
  };
  std::vector<StringRef> Packages;
  for (StringRef PackageName : StaticAnalyzerPackageNames) {
    if (PackageName != "debug" &&
        (IncludeExperimental || PackageName != "alpha"))
      Packages.push_back(PackageName);
  }
  return Packages;
}

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_ANALYZEROPTIONS_H
