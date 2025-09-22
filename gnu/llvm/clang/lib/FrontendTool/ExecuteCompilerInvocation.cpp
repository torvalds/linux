//===--- ExecuteCompilerInvocation.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file holds ExecuteCompilerInvocation(). It is split into its own file to
// minimize the impact of pulling in essentially everything else in Clang.
//
//===----------------------------------------------------------------------===//

#include "clang/ARCMigrate/ARCMTActions.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Config/config.h"
#include "clang/Driver/Options.h"
#include "clang/ExtractAPI/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/StaticAnalyzer/Frontend/AnalyzerHelpFlags.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;
using namespace llvm::opt;

namespace clang {

static std::unique_ptr<FrontendAction>
CreateFrontendBaseAction(CompilerInstance &CI) {
  using namespace clang::frontend;
  StringRef Action("unknown");
  (void)Action;

  switch (CI.getFrontendOpts().ProgramAction) {
  case ASTDeclList:            return std::make_unique<ASTDeclListAction>();
  case ASTDump:                return std::make_unique<ASTDumpAction>();
  case ASTPrint:               return std::make_unique<ASTPrintAction>();
  case ASTView:                return std::make_unique<ASTViewAction>();
  case DumpCompilerOptions:
    return std::make_unique<DumpCompilerOptionsAction>();
  case DumpRawTokens:          return std::make_unique<DumpRawTokensAction>();
  case DumpTokens:             return std::make_unique<DumpTokensAction>();
  case EmitAssembly:           return std::make_unique<EmitAssemblyAction>();
  case EmitBC:                 return std::make_unique<EmitBCAction>();
  case EmitCIR:
    llvm_unreachable("CIR suppport not built into clang");
  case EmitHTML:               return std::make_unique<HTMLPrintAction>();
  case EmitLLVM:               return std::make_unique<EmitLLVMAction>();
  case EmitLLVMOnly:           return std::make_unique<EmitLLVMOnlyAction>();
  case EmitCodeGenOnly:        return std::make_unique<EmitCodeGenOnlyAction>();
  case EmitObj:                return std::make_unique<EmitObjAction>();
  case ExtractAPI:
    return std::make_unique<ExtractAPIAction>();
  case FixIt:                  return std::make_unique<FixItAction>();
  case GenerateModule:
    return std::make_unique<GenerateModuleFromModuleMapAction>();
  case GenerateModuleInterface:
    return std::make_unique<GenerateModuleInterfaceAction>();
  case GenerateReducedModuleInterface:
    return std::make_unique<GenerateReducedModuleInterfaceAction>();
  case GenerateHeaderUnit:
    return std::make_unique<GenerateHeaderUnitAction>();
  case GeneratePCH:            return std::make_unique<GeneratePCHAction>();
  case GenerateInterfaceStubs:
    return std::make_unique<GenerateInterfaceStubsAction>();
  case InitOnly:               return std::make_unique<InitOnlyAction>();
  case ParseSyntaxOnly:        return std::make_unique<SyntaxOnlyAction>();
  case ModuleFileInfo:         return std::make_unique<DumpModuleInfoAction>();
  case VerifyPCH:              return std::make_unique<VerifyPCHAction>();
  case TemplightDump:          return std::make_unique<TemplightDumpAction>();

  case PluginAction: {
    for (const FrontendPluginRegistry::entry &Plugin :
         FrontendPluginRegistry::entries()) {
      if (Plugin.getName() == CI.getFrontendOpts().ActionName) {
        std::unique_ptr<PluginASTAction> P(Plugin.instantiate());
        if ((P->getActionType() != PluginASTAction::ReplaceAction &&
             P->getActionType() != PluginASTAction::CmdlineAfterMainAction) ||
            !P->ParseArgs(
                CI,
                CI.getFrontendOpts().PluginArgs[std::string(Plugin.getName())]))
          return nullptr;
        return std::move(P);
      }
    }

    CI.getDiagnostics().Report(diag::err_fe_invalid_plugin_name)
      << CI.getFrontendOpts().ActionName;
    return nullptr;
  }

  case PrintPreamble:          return std::make_unique<PrintPreambleAction>();
  case PrintPreprocessedInput: {
    if (CI.getPreprocessorOutputOpts().RewriteIncludes ||
        CI.getPreprocessorOutputOpts().RewriteImports)
      return std::make_unique<RewriteIncludesAction>();
    return std::make_unique<PrintPreprocessedAction>();
  }

  case RewriteMacros:          return std::make_unique<RewriteMacrosAction>();
  case RewriteTest:            return std::make_unique<RewriteTestAction>();
#if CLANG_ENABLE_OBJC_REWRITER
  case RewriteObjC:            return std::make_unique<RewriteObjCAction>();
#else
  case RewriteObjC:            Action = "RewriteObjC"; break;
#endif
#if CLANG_ENABLE_ARCMT
  case MigrateSource:
    return std::make_unique<arcmt::MigrateSourceAction>();
#else
  case MigrateSource:          Action = "MigrateSource"; break;
#endif
#if CLANG_ENABLE_STATIC_ANALYZER
  case RunAnalysis:            return std::make_unique<ento::AnalysisAction>();
#else
  case RunAnalysis:            Action = "RunAnalysis"; break;
#endif
  case RunPreprocessorOnly:    return std::make_unique<PreprocessOnlyAction>();
  case PrintDependencyDirectivesSourceMinimizerOutput:
    return std::make_unique<PrintDependencyDirectivesSourceMinimizerAction>();
  }

#if !CLANG_ENABLE_ARCMT || !CLANG_ENABLE_STATIC_ANALYZER \
  || !CLANG_ENABLE_OBJC_REWRITER
  CI.getDiagnostics().Report(diag::err_fe_action_not_available) << Action;
  return 0;
#else
  llvm_unreachable("Invalid program action!");
#endif
}

std::unique_ptr<FrontendAction>
CreateFrontendAction(CompilerInstance &CI) {
  // Create the underlying action.
  std::unique_ptr<FrontendAction> Act = CreateFrontendBaseAction(CI);
  if (!Act)
    return nullptr;

  const FrontendOptions &FEOpts = CI.getFrontendOpts();

  if (FEOpts.FixAndRecompile) {
    Act = std::make_unique<FixItRecompile>(std::move(Act));
  }

#if CLANG_ENABLE_ARCMT
  if (CI.getFrontendOpts().ProgramAction != frontend::MigrateSource &&
      CI.getFrontendOpts().ProgramAction != frontend::GeneratePCH) {
    // Potentially wrap the base FE action in an ARC Migrate Tool action.
    switch (FEOpts.ARCMTAction) {
    case FrontendOptions::ARCMT_None:
      break;
    case FrontendOptions::ARCMT_Check:
      Act = std::make_unique<arcmt::CheckAction>(std::move(Act));
      break;
    case FrontendOptions::ARCMT_Modify:
      Act = std::make_unique<arcmt::ModifyAction>(std::move(Act));
      break;
    case FrontendOptions::ARCMT_Migrate:
      Act = std::make_unique<arcmt::MigrateAction>(std::move(Act),
                                     FEOpts.MTMigrateDir,
                                     FEOpts.ARCMTMigrateReportOut,
                                     FEOpts.ARCMTMigrateEmitARCErrors);
      break;
    }

    if (FEOpts.ObjCMTAction != FrontendOptions::ObjCMT_None) {
      Act = std::make_unique<arcmt::ObjCMigrateAction>(std::move(Act),
                                                        FEOpts.MTMigrateDir,
                                                        FEOpts.ObjCMTAction);
    }
  }
#endif

  // Wrap the base FE action in an extract api action to generate
  // symbol graph as a biproduct of compilation (enabled with
  // --emit-symbol-graph option)
  if (FEOpts.EmitSymbolGraph) {
    if (FEOpts.SymbolGraphOutputDir.empty()) {
      CI.getDiagnostics().Report(diag::warn_missing_symbol_graph_dir);
      CI.getFrontendOpts().SymbolGraphOutputDir = ".";
    }
    CI.getCodeGenOpts().ClearASTBeforeBackend = false;
    Act = std::make_unique<WrappingExtractAPIAction>(std::move(Act));
  }

  // If there are any AST files to merge, create a frontend action
  // adaptor to perform the merge.
  if (!FEOpts.ASTMergeFiles.empty())
    Act = std::make_unique<ASTMergeAction>(std::move(Act),
                                            FEOpts.ASTMergeFiles);

  return Act;
}

bool ExecuteCompilerInvocation(CompilerInstance *Clang) {
  // Honor -help.
  if (Clang->getFrontendOpts().ShowHelp) {
    driver::getDriverOptTable().printHelp(
        llvm::outs(), "clang -cc1 [options] file...",
        "LLVM 'Clang' Compiler: http://clang.llvm.org",
        /*ShowHidden=*/false, /*ShowAllAliases=*/false,
        llvm::opt::Visibility(driver::options::CC1Option));
    return true;
  }

  // Honor -version.
  //
  // FIXME: Use a better -version message?
  if (Clang->getFrontendOpts().ShowVersion) {
    llvm::cl::PrintVersionMessage();
    return true;
  }

  Clang->LoadRequestedPlugins();

  // Honor -mllvm.
  //
  // FIXME: Remove this, one day.
  // This should happen AFTER plugins have been loaded!
  if (!Clang->getFrontendOpts().LLVMArgs.empty()) {
    unsigned NumArgs = Clang->getFrontendOpts().LLVMArgs.size();
    auto Args = std::make_unique<const char*[]>(NumArgs + 2);
    Args[0] = "clang (LLVM option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = Clang->getFrontendOpts().LLVMArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    llvm::cl::ParseCommandLineOptions(NumArgs + 1, Args.get());
  }

#if CLANG_ENABLE_STATIC_ANALYZER
  // These should happen AFTER plugins have been loaded!

  AnalyzerOptions &AnOpts = Clang->getAnalyzerOpts();

  // Honor -analyzer-checker-help and -analyzer-checker-help-hidden.
  if (AnOpts.ShowCheckerHelp || AnOpts.ShowCheckerHelpAlpha ||
      AnOpts.ShowCheckerHelpDeveloper) {
    ento::printCheckerHelp(llvm::outs(), *Clang);
    return true;
  }

  // Honor -analyzer-checker-option-help.
  if (AnOpts.ShowCheckerOptionList || AnOpts.ShowCheckerOptionAlphaList ||
      AnOpts.ShowCheckerOptionDeveloperList) {
    ento::printCheckerConfigList(llvm::outs(), *Clang);
    return true;
  }

  // Honor -analyzer-list-enabled-checkers.
  if (AnOpts.ShowEnabledCheckerList) {
    ento::printEnabledCheckerList(llvm::outs(), *Clang);
    return true;
  }

  // Honor -analyzer-config-help.
  if (AnOpts.ShowConfigOptionsList) {
    ento::printAnalyzerConfigList(llvm::outs());
    return true;
  }
#endif

  // If there were errors in processing arguments, don't do anything else.
  if (Clang->getDiagnostics().hasErrorOccurred())
    return false;
  // Create and execute the frontend action.
  std::unique_ptr<FrontendAction> Act(CreateFrontendAction(*Clang));
  if (!Act)
    return false;
  bool Success = Clang->ExecuteAction(*Act);
  if (Clang->getFrontendOpts().DisableFree)
    llvm::BuryPointer(std::move(Act));
  return Success;
}

} // namespace clang
