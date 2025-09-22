//===--- AnalysisConsumer.cpp - ASTConsumer for running Analyses ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// "Meta" ASTConsumer for running different source analyses.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Frontend/AnalysisConsumer.h"
#include "ModelInjector.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/Analysis/CodeInjector.h"
#include "clang/Analysis/MacroExpansionContext.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CrossTU/CrossTranslationUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <queue>
#include <utility>

using namespace clang;
using namespace ento;

#define DEBUG_TYPE "AnalysisConsumer"

STATISTIC(NumFunctionTopLevel, "The # of functions at top level.");
STATISTIC(NumFunctionsAnalyzed,
                      "The # of functions and blocks analyzed (as top level "
                      "with inlining turned on).");
STATISTIC(NumBlocksInAnalyzedFunctions,
                      "The # of basic blocks in the analyzed functions.");
STATISTIC(NumVisitedBlocksInAnalyzedFunctions,
          "The # of visited basic blocks in the analyzed functions.");
STATISTIC(PercentReachableBlocks, "The % of reachable basic blocks.");
STATISTIC(MaxCFGSize, "The maximum number of basic blocks in a function.");

//===----------------------------------------------------------------------===//
// AnalysisConsumer declaration.
//===----------------------------------------------------------------------===//

namespace {

class AnalysisConsumer : public AnalysisASTConsumer,
                         public RecursiveASTVisitor<AnalysisConsumer> {
  enum {
    AM_None = 0,
    AM_Syntax = 0x1,
    AM_Path = 0x2
  };
  typedef unsigned AnalysisMode;

  /// Mode of the analyzes while recursively visiting Decls.
  AnalysisMode RecVisitorMode;
  /// Bug Reporter to use while recursively visiting Decls.
  BugReporter *RecVisitorBR;

  std::vector<std::function<void(CheckerRegistry &)>> CheckerRegistrationFns;

public:
  ASTContext *Ctx;
  Preprocessor &PP;
  const std::string OutDir;
  AnalyzerOptions &Opts;
  ArrayRef<std::string> Plugins;
  CodeInjector *Injector;
  cross_tu::CrossTranslationUnitContext CTU;

  /// Stores the declarations from the local translation unit.
  /// Note, we pre-compute the local declarations at parse time as an
  /// optimization to make sure we do not deserialize everything from disk.
  /// The local declaration to all declarations ratio might be very small when
  /// working with a PCH file.
  SetOfDecls LocalTUDecls;

  MacroExpansionContext MacroExpansions;

  // Set of PathDiagnosticConsumers.  Owned by AnalysisManager.
  PathDiagnosticConsumers PathConsumers;

  StoreManagerCreator CreateStoreMgr;
  ConstraintManagerCreator CreateConstraintMgr;

  std::unique_ptr<CheckerManager> checkerMgr;
  std::unique_ptr<AnalysisManager> Mgr;

  /// Time the analyzes time of each translation unit.
  std::unique_ptr<llvm::TimerGroup> AnalyzerTimers;
  std::unique_ptr<llvm::Timer> SyntaxCheckTimer;
  std::unique_ptr<llvm::Timer> ExprEngineTimer;
  std::unique_ptr<llvm::Timer> BugReporterTimer;

  /// The information about analyzed functions shared throughout the
  /// translation unit.
  FunctionSummariesTy FunctionSummaries;

  AnalysisConsumer(CompilerInstance &CI, const std::string &outdir,
                   AnalyzerOptions &opts, ArrayRef<std::string> plugins,
                   CodeInjector *injector)
      : RecVisitorMode(0), RecVisitorBR(nullptr), Ctx(nullptr),
        PP(CI.getPreprocessor()), OutDir(outdir), Opts(opts),
        Plugins(plugins), Injector(injector), CTU(CI),
        MacroExpansions(CI.getLangOpts()) {
    DigestAnalyzerOptions();
    if (Opts.AnalyzerDisplayProgress || Opts.PrintStats ||
        Opts.ShouldSerializeStats) {
      AnalyzerTimers = std::make_unique<llvm::TimerGroup>(
          "analyzer", "Analyzer timers");
      SyntaxCheckTimer = std::make_unique<llvm::Timer>(
          "syntaxchecks", "Syntax-based analysis time", *AnalyzerTimers);
      ExprEngineTimer = std::make_unique<llvm::Timer>(
          "exprengine", "Path exploration time", *AnalyzerTimers);
      BugReporterTimer = std::make_unique<llvm::Timer>(
          "bugreporter", "Path-sensitive report post-processing time",
          *AnalyzerTimers);
    }

    if (Opts.PrintStats || Opts.ShouldSerializeStats) {
      llvm::EnableStatistics(/* DoPrintOnExit= */ false);
    }

    if (Opts.ShouldDisplayMacroExpansions)
      MacroExpansions.registerForPreprocessor(PP);
  }

  ~AnalysisConsumer() override {
    if (Opts.PrintStats) {
      llvm::PrintStatistics();
    }
  }

  void DigestAnalyzerOptions() {
    switch (Opts.AnalysisDiagOpt) {
    case PD_NONE:
      break;
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATEFN)                    \
  case PD_##NAME:                                                              \
    CREATEFN(Opts.getDiagOpts(), PathConsumers, OutDir, PP, CTU,              \
             MacroExpansions);                                                 \
    break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    default:
      llvm_unreachable("Unknown analyzer output type!");
    }

    // Create the analyzer component creators.
    CreateStoreMgr = &CreateRegionStoreManager;

    switch (Opts.AnalysisConstraintsOpt) {
    default:
      llvm_unreachable("Unknown constraint manager.");
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATEFN)     \
      case NAME##Model: CreateConstraintMgr = CREATEFN; break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    }
  }

  void DisplayTime(llvm::TimeRecord &Time) {
    if (!Opts.AnalyzerDisplayProgress) {
      return;
    }
    llvm::errs() << " : " << llvm::format("%1.1f", Time.getWallTime() * 1000)
                 << " ms\n";
  }

  void DisplayFunction(const Decl *D, AnalysisMode Mode,
                       ExprEngine::InliningModes IMode) {
    if (!Opts.AnalyzerDisplayProgress)
      return;

    SourceManager &SM = Mgr->getASTContext().getSourceManager();
    PresumedLoc Loc = SM.getPresumedLoc(D->getLocation());
    if (Loc.isValid()) {
      llvm::errs() << "ANALYZE";

      if (Mode == AM_Syntax)
        llvm::errs() << " (Syntax)";
      else if (Mode == AM_Path) {
        llvm::errs() << " (Path, ";
        switch (IMode) {
        case ExprEngine::Inline_Minimal:
          llvm::errs() << " Inline_Minimal";
          break;
        case ExprEngine::Inline_Regular:
          llvm::errs() << " Inline_Regular";
          break;
        }
        llvm::errs() << ")";
      } else
        assert(Mode == (AM_Syntax | AM_Path) && "Unexpected mode!");

      llvm::errs() << ": " << Loc.getFilename() << ' '
                   << AnalysisDeclContext::getFunctionName(D);
    }
  }

  void Initialize(ASTContext &Context) override {
    Ctx = &Context;
    checkerMgr = std::make_unique<CheckerManager>(*Ctx, Opts, PP, Plugins,
                                                  CheckerRegistrationFns);

    Mgr = std::make_unique<AnalysisManager>(*Ctx, PP, PathConsumers,
                                            CreateStoreMgr, CreateConstraintMgr,
                                            checkerMgr.get(), Opts, Injector);
  }

  /// Store the top level decls in the set to be processed later on.
  /// (Doing this pre-processing avoids deserialization of data from PCH.)
  bool HandleTopLevelDecl(DeclGroupRef D) override;
  void HandleTopLevelDeclInObjCContainer(DeclGroupRef D) override;

  void HandleTranslationUnit(ASTContext &C) override;

  /// Determine which inlining mode should be used when this function is
  /// analyzed. This allows to redefine the default inlining policies when
  /// analyzing a given function.
  ExprEngine::InliningModes
    getInliningModeForFunction(const Decl *D, const SetOfConstDecls &Visited);

  /// Build the call graph for all the top level decls of this TU and
  /// use it to define the order in which the functions should be visited.
  void HandleDeclsCallGraph(const unsigned LocalTUDeclsSize);

  /// Run analyzes(syntax or path sensitive) on the given function.
  /// \param Mode - determines if we are requesting syntax only or path
  /// sensitive only analysis.
  /// \param VisitedCallees - The output parameter, which is populated with the
  /// set of functions which should be considered analyzed after analyzing the
  /// given root function.
  void HandleCode(Decl *D, AnalysisMode Mode,
                  ExprEngine::InliningModes IMode = ExprEngine::Inline_Minimal,
                  SetOfConstDecls *VisitedCallees = nullptr);

  void RunPathSensitiveChecks(Decl *D,
                              ExprEngine::InliningModes IMode,
                              SetOfConstDecls *VisitedCallees);

  /// Visitors for the RecursiveASTVisitor.
  bool shouldWalkTypesOfTypeLocs() const { return false; }

  /// Handle callbacks for arbitrary Decls.
  bool VisitDecl(Decl *D) {
    AnalysisMode Mode = getModeForDecl(D, RecVisitorMode);
    if (Mode & AM_Syntax) {
      if (SyntaxCheckTimer)
        SyntaxCheckTimer->startTimer();
      checkerMgr->runCheckersOnASTDecl(D, *Mgr, *RecVisitorBR);
      if (SyntaxCheckTimer)
        SyntaxCheckTimer->stopTimer();
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *VD) {
    if (!Opts.IsNaiveCTUEnabled)
      return true;

    if (VD->hasExternalStorage() || VD->isStaticDataMember()) {
      if (!cross_tu::shouldImport(VD, *Ctx))
        return true;
    } else {
      // Cannot be initialized in another TU.
      return true;
    }

    if (VD->getAnyInitializer())
      return true;

    llvm::Expected<const VarDecl *> CTUDeclOrError =
      CTU.getCrossTUDefinition(VD, Opts.CTUDir, Opts.CTUIndexName,
                               Opts.DisplayCTUProgress);

    if (!CTUDeclOrError) {
      handleAllErrors(CTUDeclOrError.takeError(),
                      [&](const cross_tu::IndexError &IE) {
                        CTU.emitCrossTUDiagnostics(IE);
                      });
    }

    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *FD) {
    IdentifierInfo *II = FD->getIdentifier();
    if (II && II->getName().starts_with("__inline"))
      return true;

    // We skip function template definitions, as their semantics is
    // only determined when they are instantiated.
    if (FD->isThisDeclarationADefinition() &&
        !FD->isDependentContext()) {
      assert(RecVisitorMode == AM_Syntax || Mgr->shouldInlineCall() == false);
      HandleCode(FD, RecVisitorMode);
    }
    return true;
  }

  bool VisitObjCMethodDecl(ObjCMethodDecl *MD) {
    if (MD->isThisDeclarationADefinition()) {
      assert(RecVisitorMode == AM_Syntax || Mgr->shouldInlineCall() == false);
      HandleCode(MD, RecVisitorMode);
    }
    return true;
  }

  bool VisitBlockDecl(BlockDecl *BD) {
    if (BD->hasBody()) {
      assert(RecVisitorMode == AM_Syntax || Mgr->shouldInlineCall() == false);
      // Since we skip function template definitions, we should skip blocks
      // declared in those functions as well.
      if (!BD->isDependentContext()) {
        HandleCode(BD, RecVisitorMode);
      }
    }
    return true;
  }

  void AddDiagnosticConsumer(PathDiagnosticConsumer *Consumer) override {
    PathConsumers.push_back(Consumer);
  }

  void AddCheckerRegistrationFn(std::function<void(CheckerRegistry&)> Fn) override {
    CheckerRegistrationFns.push_back(std::move(Fn));
  }

private:
  void storeTopLevelDecls(DeclGroupRef DG);

  /// Check if we should skip (not analyze) the given function.
  AnalysisMode getModeForDecl(Decl *D, AnalysisMode Mode);
  void runAnalysisOnTranslationUnit(ASTContext &C);

  /// Print \p S to stderr if \c Opts.AnalyzerDisplayProgress is set.
  void reportAnalyzerProgress(StringRef S);
}; // namespace
} // end anonymous namespace


//===----------------------------------------------------------------------===//
// AnalysisConsumer implementation.
//===----------------------------------------------------------------------===//
bool AnalysisConsumer::HandleTopLevelDecl(DeclGroupRef DG) {
  storeTopLevelDecls(DG);
  return true;
}

void AnalysisConsumer::HandleTopLevelDeclInObjCContainer(DeclGroupRef DG) {
  storeTopLevelDecls(DG);
}

void AnalysisConsumer::storeTopLevelDecls(DeclGroupRef DG) {
  for (auto &I : DG) {

    // Skip ObjCMethodDecl, wait for the objc container to avoid
    // analyzing twice.
    if (isa<ObjCMethodDecl>(I))
      continue;

    LocalTUDecls.push_back(I);
  }
}

static bool shouldSkipFunction(const Decl *D,
                               const SetOfConstDecls &Visited,
                               const SetOfConstDecls &VisitedAsTopLevel) {
  if (VisitedAsTopLevel.count(D))
    return true;

  // Skip analysis of inheriting constructors as top-level functions. These
  // constructors don't even have a body written down in the code, so even if
  // we find a bug, we won't be able to display it.
  if (const auto *CD = dyn_cast<CXXConstructorDecl>(D))
    if (CD->isInheritingConstructor())
      return true;

  // We want to re-analyse the functions as top level in the following cases:
  // - The 'init' methods should be reanalyzed because
  //   ObjCNonNilReturnValueChecker assumes that '[super init]' never returns
  //   'nil' and unless we analyze the 'init' functions as top level, we will
  //   not catch errors within defensive code.
  // - We want to reanalyze all ObjC methods as top level to report Retain
  //   Count naming convention errors more aggressively.
  if (isa<ObjCMethodDecl>(D))
    return false;
  // We also want to reanalyze all C++ copy and move assignment operators to
  // separately check the two cases where 'this' aliases with the parameter and
  // where it may not. (cplusplus.SelfAssignmentChecker)
  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator())
      return false;
  }

  // Otherwise, if we visited the function before, do not reanalyze it.
  return Visited.count(D);
}

ExprEngine::InliningModes
AnalysisConsumer::getInliningModeForFunction(const Decl *D,
                                             const SetOfConstDecls &Visited) {
  // We want to reanalyze all ObjC methods as top level to report Retain
  // Count naming convention errors more aggressively. But we should tune down
  // inlining when reanalyzing an already inlined function.
  if (Visited.count(D) && isa<ObjCMethodDecl>(D)) {
    const ObjCMethodDecl *ObjCM = cast<ObjCMethodDecl>(D);
    if (ObjCM->getMethodFamily() != OMF_init)
      return ExprEngine::Inline_Minimal;
  }

  return ExprEngine::Inline_Regular;
}

void AnalysisConsumer::HandleDeclsCallGraph(const unsigned LocalTUDeclsSize) {
  // Build the Call Graph by adding all the top level declarations to the graph.
  // Note: CallGraph can trigger deserialization of more items from a pch
  // (though HandleInterestingDecl); triggering additions to LocalTUDecls.
  // We rely on random access to add the initially processed Decls to CG.
  CallGraph CG;
  for (unsigned i = 0 ; i < LocalTUDeclsSize ; ++i) {
    CG.addToCallGraph(LocalTUDecls[i]);
  }

  // Walk over all of the call graph nodes in topological order, so that we
  // analyze parents before the children. Skip the functions inlined into
  // the previously processed functions. Use external Visited set to identify
  // inlined functions. The topological order allows the "do not reanalyze
  // previously inlined function" performance heuristic to be triggered more
  // often.
  SetOfConstDecls Visited;
  SetOfConstDecls VisitedAsTopLevel;
  llvm::ReversePostOrderTraversal<clang::CallGraph*> RPOT(&CG);
  for (auto &N : RPOT) {
    NumFunctionTopLevel++;

    Decl *D = N->getDecl();

    // Skip the abstract root node.
    if (!D)
      continue;

    // Skip the functions which have been processed already or previously
    // inlined.
    if (shouldSkipFunction(D, Visited, VisitedAsTopLevel))
      continue;

    // The CallGraph might have declarations as callees. However, during CTU
    // the declaration might form a declaration chain with the newly imported
    // definition from another TU. In this case we don't want to analyze the
    // function definition as toplevel.
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      // Calling 'hasBody' replaces 'FD' in place with the FunctionDecl
      // that has the body.
      FD->hasBody(FD);
      if (CTU.isImportedAsNew(FD))
        continue;
    }

    // Analyze the function.
    SetOfConstDecls VisitedCallees;

    HandleCode(D, AM_Path, getInliningModeForFunction(D, Visited),
               (Mgr->options.InliningMode == All ? nullptr : &VisitedCallees));

    // Add the visited callees to the global visited set.
    for (const Decl *Callee : VisitedCallees)
      // Decls from CallGraph are already canonical. But Decls coming from
      // CallExprs may be not. We should canonicalize them manually.
      Visited.insert(isa<ObjCMethodDecl>(Callee) ? Callee
                                                 : Callee->getCanonicalDecl());
    VisitedAsTopLevel.insert(D);
  }
}

static bool fileContainsString(StringRef Substring, ASTContext &C) {
  const SourceManager &SM = C.getSourceManager();
  FileID FID = SM.getMainFileID();
  StringRef Buffer = SM.getBufferOrFake(FID).getBuffer();
  return Buffer.contains(Substring);
}

static void reportAnalyzerFunctionMisuse(const AnalyzerOptions &Opts,
                                         const ASTContext &Ctx) {
  llvm::errs() << "Every top-level function was skipped.\n";

  if (!Opts.AnalyzerDisplayProgress)
    llvm::errs() << "Pass the -analyzer-display-progress for tracking which "
                    "functions are analyzed.\n";

  bool HasBrackets =
      Opts.AnalyzeSpecificFunction.find("(") != std::string::npos;

  if (Ctx.getLangOpts().CPlusPlus && !HasBrackets) {
    llvm::errs()
        << "For analyzing C++ code you need to pass the function parameter "
           "list: -analyze-function=\"foobar(int, _Bool)\"\n";
  } else if (!Ctx.getLangOpts().CPlusPlus && HasBrackets) {
    llvm::errs() << "For analyzing C code you shouldn't pass the function "
                    "parameter list, only the name of the function: "
                    "-analyze-function=foobar\n";
  }
}

void AnalysisConsumer::runAnalysisOnTranslationUnit(ASTContext &C) {
  BugReporter BR(*Mgr);
  const TranslationUnitDecl *TU = C.getTranslationUnitDecl();
  BR.setAnalysisEntryPoint(TU);
  if (SyntaxCheckTimer)
    SyntaxCheckTimer->startTimer();
  checkerMgr->runCheckersOnASTDecl(TU, *Mgr, BR);
  if (SyntaxCheckTimer)
    SyntaxCheckTimer->stopTimer();

  // Run the AST-only checks using the order in which functions are defined.
  // If inlining is not turned on, use the simplest function order for path
  // sensitive analyzes as well.
  RecVisitorMode = AM_Syntax;
  if (!Mgr->shouldInlineCall())
    RecVisitorMode |= AM_Path;
  RecVisitorBR = &BR;

  // Process all the top level declarations.
  //
  // Note: TraverseDecl may modify LocalTUDecls, but only by appending more
  // entries.  Thus we don't use an iterator, but rely on LocalTUDecls
  // random access.  By doing so, we automatically compensate for iterators
  // possibly being invalidated, although this is a bit slower.
  const unsigned LocalTUDeclsSize = LocalTUDecls.size();
  for (unsigned i = 0 ; i < LocalTUDeclsSize ; ++i) {
    TraverseDecl(LocalTUDecls[i]);
  }

  if (Mgr->shouldInlineCall())
    HandleDeclsCallGraph(LocalTUDeclsSize);

  // After all decls handled, run checkers on the entire TranslationUnit.
  checkerMgr->runCheckersOnEndOfTranslationUnit(TU, *Mgr, BR);

  BR.FlushReports();
  RecVisitorBR = nullptr;

  // If the user wanted to analyze a specific function and the number of basic
  // blocks analyzed is zero, than the user might not specified the function
  // name correctly.
  // FIXME: The user might have analyzed the requested function in Syntax mode,
  // but we are unaware of that.
  if (!Opts.AnalyzeSpecificFunction.empty() && NumFunctionsAnalyzed == 0)
    reportAnalyzerFunctionMisuse(Opts, *Ctx);
}

void AnalysisConsumer::reportAnalyzerProgress(StringRef S) {
  if (Opts.AnalyzerDisplayProgress)
    llvm::errs() << S;
}

void AnalysisConsumer::HandleTranslationUnit(ASTContext &C) {
  // Don't run the actions if an error has occurred with parsing the file.
  DiagnosticsEngine &Diags = PP.getDiagnostics();
  if (Diags.hasErrorOccurred() || Diags.hasFatalErrorOccurred())
    return;

  // Explicitly destroy the PathDiagnosticConsumer.  This will flush its output.
  // FIXME: This should be replaced with something that doesn't rely on
  // side-effects in PathDiagnosticConsumer's destructor. This is required when
  // used with option -disable-free.
  const auto DiagFlusherScopeExit =
      llvm::make_scope_exit([this] { Mgr.reset(); });

  if (Opts.ShouldIgnoreBisonGeneratedFiles &&
      fileContainsString("/* A Bison parser, made by", C)) {
    reportAnalyzerProgress("Skipping bison-generated file\n");
    return;
  }

  if (Opts.ShouldIgnoreFlexGeneratedFiles &&
      fileContainsString("/* A lexical scanner generated by flex", C)) {
    reportAnalyzerProgress("Skipping flex-generated file\n");
    return;
  }

  // Don't analyze if the user explicitly asked for no checks to be performed
  // on this file.
  if (Opts.DisableAllCheckers) {
    reportAnalyzerProgress("All checks are disabled using a supplied option\n");
    return;
  }

  // Otherwise, just run the analysis.
  runAnalysisOnTranslationUnit(C);

  // Count how many basic blocks we have not covered.
  NumBlocksInAnalyzedFunctions = FunctionSummaries.getTotalNumBasicBlocks();
  NumVisitedBlocksInAnalyzedFunctions =
      FunctionSummaries.getTotalNumVisitedBasicBlocks();
  if (NumBlocksInAnalyzedFunctions > 0)
    PercentReachableBlocks =
        (FunctionSummaries.getTotalNumVisitedBasicBlocks() * 100) /
        NumBlocksInAnalyzedFunctions;
}

AnalysisConsumer::AnalysisMode
AnalysisConsumer::getModeForDecl(Decl *D, AnalysisMode Mode) {
  if (!Opts.AnalyzeSpecificFunction.empty() &&
      AnalysisDeclContext::getFunctionName(D) != Opts.AnalyzeSpecificFunction)
    return AM_None;

  // Unless -analyze-all is specified, treat decls differently depending on
  // where they came from:
  // - Main source file: run both path-sensitive and non-path-sensitive checks.
  // - Header files: run non-path-sensitive checks only.
  // - System headers: don't run any checks.
  if (Opts.AnalyzeAll)
    return Mode;

  const SourceManager &SM = Ctx->getSourceManager();

  const SourceLocation Loc = [&SM](Decl *D) -> SourceLocation {
    const Stmt *Body = D->getBody();
    SourceLocation SL = Body ? Body->getBeginLoc() : D->getLocation();
    return SM.getExpansionLoc(SL);
  }(D);

  // Ignore system headers.
  if (Loc.isInvalid() || SM.isInSystemHeader(Loc))
    return AM_None;

  // Disable path sensitive analysis in user-headers.
  if (!Mgr->isInCodeFile(Loc))
    return Mode & ~AM_Path;

  return Mode;
}

void AnalysisConsumer::HandleCode(Decl *D, AnalysisMode Mode,
                                  ExprEngine::InliningModes IMode,
                                  SetOfConstDecls *VisitedCallees) {
  if (!D->hasBody())
    return;
  Mode = getModeForDecl(D, Mode);
  if (Mode == AM_None)
    return;

  // Clear the AnalysisManager of old AnalysisDeclContexts.
  Mgr->ClearContexts();
  // Ignore autosynthesized code.
  if (Mgr->getAnalysisDeclContext(D)->isBodyAutosynthesized())
    return;

  CFG *DeclCFG = Mgr->getCFG(D);
  if (DeclCFG)
    MaxCFGSize.updateMax(DeclCFG->size());

  DisplayFunction(D, Mode, IMode);
  BugReporter BR(*Mgr);
  BR.setAnalysisEntryPoint(D);

  if (Mode & AM_Syntax) {
    llvm::TimeRecord CheckerStartTime;
    if (SyntaxCheckTimer) {
      CheckerStartTime = SyntaxCheckTimer->getTotalTime();
      SyntaxCheckTimer->startTimer();
    }
    checkerMgr->runCheckersOnASTBody(D, *Mgr, BR);
    if (SyntaxCheckTimer) {
      SyntaxCheckTimer->stopTimer();
      llvm::TimeRecord CheckerEndTime = SyntaxCheckTimer->getTotalTime();
      CheckerEndTime -= CheckerStartTime;
      DisplayTime(CheckerEndTime);
    }
  }

  BR.FlushReports();

  if ((Mode & AM_Path) && checkerMgr->hasPathSensitiveCheckers()) {
    RunPathSensitiveChecks(D, IMode, VisitedCallees);
    if (IMode != ExprEngine::Inline_Minimal)
      NumFunctionsAnalyzed++;
  }
}

//===----------------------------------------------------------------------===//
// Path-sensitive checking.
//===----------------------------------------------------------------------===//

void AnalysisConsumer::RunPathSensitiveChecks(Decl *D,
                                              ExprEngine::InliningModes IMode,
                                              SetOfConstDecls *VisitedCallees) {
  // Construct the analysis engine.  First check if the CFG is valid.
  // FIXME: Inter-procedural analysis will need to handle invalid CFGs.
  if (!Mgr->getCFG(D))
    return;

  // See if the LiveVariables analysis scales.
  if (!Mgr->getAnalysisDeclContext(D)->getAnalysis<RelaxedLiveVariables>())
    return;

  ExprEngine Eng(CTU, *Mgr, VisitedCallees, &FunctionSummaries, IMode);

  // Execute the worklist algorithm.
  llvm::TimeRecord ExprEngineStartTime;
  if (ExprEngineTimer) {
    ExprEngineStartTime = ExprEngineTimer->getTotalTime();
    ExprEngineTimer->startTimer();
  }
  Eng.ExecuteWorkList(Mgr->getAnalysisDeclContextManager().getStackFrame(D),
                      Mgr->options.MaxNodesPerTopLevelFunction);
  if (ExprEngineTimer) {
    ExprEngineTimer->stopTimer();
    llvm::TimeRecord ExprEngineEndTime = ExprEngineTimer->getTotalTime();
    ExprEngineEndTime -= ExprEngineStartTime;
    DisplayTime(ExprEngineEndTime);
  }

  if (!Mgr->options.DumpExplodedGraphTo.empty())
    Eng.DumpGraph(Mgr->options.TrimGraph, Mgr->options.DumpExplodedGraphTo);

  // Visualize the exploded graph.
  if (Mgr->options.visualizeExplodedGraphWithGraphViz)
    Eng.ViewGraph(Mgr->options.TrimGraph);

  // Display warnings.
  if (BugReporterTimer)
    BugReporterTimer->startTimer();
  Eng.getBugReporter().FlushReports();
  if (BugReporterTimer)
    BugReporterTimer->stopTimer();
}

//===----------------------------------------------------------------------===//
// AnalysisConsumer creation.
//===----------------------------------------------------------------------===//

std::unique_ptr<AnalysisASTConsumer>
ento::CreateAnalysisConsumer(CompilerInstance &CI) {
  // Disable the effects of '-Werror' when using the AnalysisConsumer.
  CI.getPreprocessor().getDiagnostics().setWarningsAsErrors(false);

  AnalyzerOptions &analyzerOpts = CI.getAnalyzerOpts();
  bool hasModelPath = analyzerOpts.Config.count("model-path") > 0;

  return std::make_unique<AnalysisConsumer>(
      CI, CI.getFrontendOpts().OutputFile, analyzerOpts,
      CI.getFrontendOpts().Plugins,
      hasModelPath ? new ModelInjector(CI) : nullptr);
}
