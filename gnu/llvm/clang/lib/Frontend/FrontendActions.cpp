//===--- FrontendActions.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/FrontendActions.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/DependencyDirectivesScanner.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "clang/Serialization/ModuleFile.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>
#include <system_error>

using namespace clang;

namespace {
CodeCompleteConsumer *GetCodeCompletionConsumer(CompilerInstance &CI) {
  return CI.hasCodeCompletionConsumer() ? &CI.getCodeCompletionConsumer()
                                        : nullptr;
}

void EnsureSemaIsCreated(CompilerInstance &CI, FrontendAction &Action) {
  if (Action.hasCodeCompletionSupport() &&
      !CI.getFrontendOpts().CodeCompletionAt.FileName.empty())
    CI.createCodeCompletionConsumer();

  if (!CI.hasSema())
    CI.createSema(Action.getTranslationUnitKind(),
                  GetCodeCompletionConsumer(CI));
}
} // namespace

//===----------------------------------------------------------------------===//
// Custom Actions
//===----------------------------------------------------------------------===//

std::unique_ptr<ASTConsumer>
InitOnlyAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}

void InitOnlyAction::ExecuteAction() {
}

// Basically PreprocessOnlyAction::ExecuteAction.
void ReadPCHAndPreprocessAction::ExecuteAction() {
  Preprocessor &PP = getCompilerInstance().getPreprocessor();

  // Ignore unknown pragmas.
  PP.IgnorePragmas();

  Token Tok;
  // Start parsing the specified input file.
  PP.EnterMainSourceFile();
  do {
    PP.Lex(Tok);
  } while (Tok.isNot(tok::eof));
}

std::unique_ptr<ASTConsumer>
ReadPCHAndPreprocessAction::CreateASTConsumer(CompilerInstance &CI,
                                              StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

std::unique_ptr<ASTConsumer>
ASTPrintAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  if (std::unique_ptr<raw_ostream> OS =
          CI.createDefaultOutputFile(false, InFile))
    return CreateASTPrinter(std::move(OS), CI.getFrontendOpts().ASTDumpFilter);
  return nullptr;
}

std::unique_ptr<ASTConsumer>
ASTDumpAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  const FrontendOptions &Opts = CI.getFrontendOpts();
  return CreateASTDumper(nullptr /*Dump to stdout.*/, Opts.ASTDumpFilter,
                         Opts.ASTDumpDecls, Opts.ASTDumpAll,
                         Opts.ASTDumpLookups, Opts.ASTDumpDeclTypes,
                         Opts.ASTDumpFormat);
}

std::unique_ptr<ASTConsumer>
ASTDeclListAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return CreateASTDeclNodeLister();
}

std::unique_ptr<ASTConsumer>
ASTViewAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return CreateASTViewer();
}

std::unique_ptr<ASTConsumer>
GeneratePCHAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  std::string Sysroot;
  if (!ComputeASTConsumerArguments(CI, /*ref*/ Sysroot))
    return nullptr;

  std::string OutputFile;
  std::unique_ptr<raw_pwrite_stream> OS =
      CreateOutputFile(CI, InFile, /*ref*/ OutputFile);
  if (!OS)
    return nullptr;

  if (!CI.getFrontendOpts().RelocatablePCH)
    Sysroot.clear();

  const auto &FrontendOpts = CI.getFrontendOpts();
  auto Buffer = std::make_shared<PCHBuffer>();
  std::vector<std::unique_ptr<ASTConsumer>> Consumers;
  Consumers.push_back(std::make_unique<PCHGenerator>(
      CI.getPreprocessor(), CI.getModuleCache(), OutputFile, Sysroot, Buffer,
      FrontendOpts.ModuleFileExtensions,
      CI.getPreprocessorOpts().AllowPCHWithCompilerErrors,
      FrontendOpts.IncludeTimestamps, FrontendOpts.BuildingImplicitModule,
      +CI.getLangOpts().CacheGeneratedPCH));
  Consumers.push_back(CI.getPCHContainerWriter().CreatePCHContainerGenerator(
      CI, std::string(InFile), OutputFile, std::move(OS), Buffer));

  return std::make_unique<MultiplexConsumer>(std::move(Consumers));
}

bool GeneratePCHAction::ComputeASTConsumerArguments(CompilerInstance &CI,
                                                    std::string &Sysroot) {
  Sysroot = CI.getHeaderSearchOpts().Sysroot;
  if (CI.getFrontendOpts().RelocatablePCH && Sysroot.empty()) {
    CI.getDiagnostics().Report(diag::err_relocatable_without_isysroot);
    return false;
  }

  return true;
}

std::unique_ptr<llvm::raw_pwrite_stream>
GeneratePCHAction::CreateOutputFile(CompilerInstance &CI, StringRef InFile,
                                    std::string &OutputFile) {
  // Because this is exposed via libclang we must disable RemoveFileOnSignal.
  std::unique_ptr<raw_pwrite_stream> OS = CI.createDefaultOutputFile(
      /*Binary=*/true, InFile, /*Extension=*/"", /*RemoveFileOnSignal=*/false);
  if (!OS)
    return nullptr;

  OutputFile = CI.getFrontendOpts().OutputFile;
  return OS;
}

bool GeneratePCHAction::shouldEraseOutputFiles() {
  if (getCompilerInstance().getPreprocessorOpts().AllowPCHWithCompilerErrors)
    return false;
  return ASTFrontendAction::shouldEraseOutputFiles();
}

bool GeneratePCHAction::BeginSourceFileAction(CompilerInstance &CI) {
  CI.getLangOpts().CompilingPCH = true;
  return true;
}

std::vector<std::unique_ptr<ASTConsumer>>
GenerateModuleAction::CreateMultiplexConsumer(CompilerInstance &CI,
                                              StringRef InFile) {
  std::unique_ptr<raw_pwrite_stream> OS = CreateOutputFile(CI, InFile);
  if (!OS)
    return {};

  std::string OutputFile = CI.getFrontendOpts().OutputFile;
  std::string Sysroot;

  auto Buffer = std::make_shared<PCHBuffer>();
  std::vector<std::unique_ptr<ASTConsumer>> Consumers;

  Consumers.push_back(std::make_unique<PCHGenerator>(
      CI.getPreprocessor(), CI.getModuleCache(), OutputFile, Sysroot, Buffer,
      CI.getFrontendOpts().ModuleFileExtensions,
      /*AllowASTWithErrors=*/
      +CI.getFrontendOpts().AllowPCMWithCompilerErrors,
      /*IncludeTimestamps=*/
      +CI.getFrontendOpts().BuildingImplicitModule &&
          +CI.getFrontendOpts().IncludeTimestamps,
      /*BuildingImplicitModule=*/+CI.getFrontendOpts().BuildingImplicitModule,
      /*ShouldCacheASTInMemory=*/
      +CI.getFrontendOpts().BuildingImplicitModule));
  Consumers.push_back(CI.getPCHContainerWriter().CreatePCHContainerGenerator(
      CI, std::string(InFile), OutputFile, std::move(OS), Buffer));
  return Consumers;
}

std::unique_ptr<ASTConsumer>
GenerateModuleAction::CreateASTConsumer(CompilerInstance &CI,
                                        StringRef InFile) {
  std::vector<std::unique_ptr<ASTConsumer>> Consumers =
      CreateMultiplexConsumer(CI, InFile);
  if (Consumers.empty())
    return nullptr;

  return std::make_unique<MultiplexConsumer>(std::move(Consumers));
}

bool GenerateModuleAction::shouldEraseOutputFiles() {
  return !getCompilerInstance().getFrontendOpts().AllowPCMWithCompilerErrors &&
         ASTFrontendAction::shouldEraseOutputFiles();
}

bool GenerateModuleFromModuleMapAction::BeginSourceFileAction(
    CompilerInstance &CI) {
  if (!CI.getLangOpts().Modules) {
    CI.getDiagnostics().Report(diag::err_module_build_requires_fmodules);
    return false;
  }

  return GenerateModuleAction::BeginSourceFileAction(CI);
}

std::unique_ptr<raw_pwrite_stream>
GenerateModuleFromModuleMapAction::CreateOutputFile(CompilerInstance &CI,
                                                    StringRef InFile) {
  // If no output file was provided, figure out where this module would go
  // in the module cache.
  if (CI.getFrontendOpts().OutputFile.empty()) {
    StringRef ModuleMapFile = CI.getFrontendOpts().OriginalModuleMap;
    if (ModuleMapFile.empty())
      ModuleMapFile = InFile;

    HeaderSearch &HS = CI.getPreprocessor().getHeaderSearchInfo();
    CI.getFrontendOpts().OutputFile =
        HS.getCachedModuleFileName(CI.getLangOpts().CurrentModule,
                                   ModuleMapFile);
  }

  // Because this is exposed via libclang we must disable RemoveFileOnSignal.
  return CI.createDefaultOutputFile(/*Binary=*/true, InFile, /*Extension=*/"",
                                    /*RemoveFileOnSignal=*/false,
                                    /*CreateMissingDirectories=*/true,
                                    /*ForceUseTemporary=*/true);
}

bool GenerateModuleInterfaceAction::BeginSourceFileAction(
    CompilerInstance &CI) {
  CI.getLangOpts().setCompilingModule(LangOptions::CMK_ModuleInterface);

  return GenerateModuleAction::BeginSourceFileAction(CI);
}

std::unique_ptr<ASTConsumer>
GenerateModuleInterfaceAction::CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) {
  std::vector<std::unique_ptr<ASTConsumer>> Consumers;

  if (CI.getFrontendOpts().GenReducedBMI &&
      !CI.getFrontendOpts().ModuleOutputPath.empty()) {
    Consumers.push_back(std::make_unique<ReducedBMIGenerator>(
        CI.getPreprocessor(), CI.getModuleCache(),
        CI.getFrontendOpts().ModuleOutputPath));
  }

  Consumers.push_back(std::make_unique<CXX20ModulesGenerator>(
      CI.getPreprocessor(), CI.getModuleCache(),
      CI.getFrontendOpts().OutputFile));

  return std::make_unique<MultiplexConsumer>(std::move(Consumers));
}

std::unique_ptr<raw_pwrite_stream>
GenerateModuleInterfaceAction::CreateOutputFile(CompilerInstance &CI,
                                                StringRef InFile) {
  return CI.createDefaultOutputFile(/*Binary=*/true, InFile, "pcm");
}

std::unique_ptr<ASTConsumer>
GenerateReducedModuleInterfaceAction::CreateASTConsumer(CompilerInstance &CI,
                                                        StringRef InFile) {
  return std::make_unique<ReducedBMIGenerator>(CI.getPreprocessor(),
                                               CI.getModuleCache(),
                                               CI.getFrontendOpts().OutputFile);
}

bool GenerateHeaderUnitAction::BeginSourceFileAction(CompilerInstance &CI) {
  if (!CI.getLangOpts().CPlusPlusModules) {
    CI.getDiagnostics().Report(diag::err_module_interface_requires_cpp_modules);
    return false;
  }
  CI.getLangOpts().setCompilingModule(LangOptions::CMK_HeaderUnit);
  return GenerateModuleAction::BeginSourceFileAction(CI);
}

std::unique_ptr<raw_pwrite_stream>
GenerateHeaderUnitAction::CreateOutputFile(CompilerInstance &CI,
                                           StringRef InFile) {
  return CI.createDefaultOutputFile(/*Binary=*/true, InFile, "pcm");
}

SyntaxOnlyAction::~SyntaxOnlyAction() {
}

std::unique_ptr<ASTConsumer>
SyntaxOnlyAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}

std::unique_ptr<ASTConsumer>
DumpModuleInfoAction::CreateASTConsumer(CompilerInstance &CI,
                                        StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}

std::unique_ptr<ASTConsumer>
VerifyPCHAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}

void VerifyPCHAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  bool Preamble = CI.getPreprocessorOpts().PrecompiledPreambleBytes.first != 0;
  const std::string &Sysroot = CI.getHeaderSearchOpts().Sysroot;
  std::unique_ptr<ASTReader> Reader(new ASTReader(
      CI.getPreprocessor(), CI.getModuleCache(), &CI.getASTContext(),
      CI.getPCHContainerReader(), CI.getFrontendOpts().ModuleFileExtensions,
      Sysroot.empty() ? "" : Sysroot.c_str(),
      DisableValidationForModuleKind::None,
      /*AllowASTWithCompilerErrors*/ false,
      /*AllowConfigurationMismatch*/ true,
      /*ValidateSystemInputs*/ true));

  Reader->ReadAST(getCurrentFile(),
                  Preamble ? serialization::MK_Preamble
                           : serialization::MK_PCH,
                  SourceLocation(),
                  ASTReader::ARR_ConfigurationMismatch);
}

namespace {
struct TemplightEntry {
  std::string Name;
  std::string Kind;
  std::string Event;
  std::string DefinitionLocation;
  std::string PointOfInstantiation;
};
} // namespace

namespace llvm {
namespace yaml {
template <> struct MappingTraits<TemplightEntry> {
  static void mapping(IO &io, TemplightEntry &fields) {
    io.mapRequired("name", fields.Name);
    io.mapRequired("kind", fields.Kind);
    io.mapRequired("event", fields.Event);
    io.mapRequired("orig", fields.DefinitionLocation);
    io.mapRequired("poi", fields.PointOfInstantiation);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
class DefaultTemplateInstCallback : public TemplateInstantiationCallback {
  using CodeSynthesisContext = Sema::CodeSynthesisContext;

public:
  void initialize(const Sema &) override {}

  void finalize(const Sema &) override {}

  void atTemplateBegin(const Sema &TheSema,
                       const CodeSynthesisContext &Inst) override {
    displayTemplightEntry<true>(llvm::outs(), TheSema, Inst);
  }

  void atTemplateEnd(const Sema &TheSema,
                     const CodeSynthesisContext &Inst) override {
    displayTemplightEntry<false>(llvm::outs(), TheSema, Inst);
  }

private:
  static std::string toString(CodeSynthesisContext::SynthesisKind Kind) {
    switch (Kind) {
    case CodeSynthesisContext::TemplateInstantiation:
      return "TemplateInstantiation";
    case CodeSynthesisContext::DefaultTemplateArgumentInstantiation:
      return "DefaultTemplateArgumentInstantiation";
    case CodeSynthesisContext::DefaultFunctionArgumentInstantiation:
      return "DefaultFunctionArgumentInstantiation";
    case CodeSynthesisContext::ExplicitTemplateArgumentSubstitution:
      return "ExplicitTemplateArgumentSubstitution";
    case CodeSynthesisContext::DeducedTemplateArgumentSubstitution:
      return "DeducedTemplateArgumentSubstitution";
    case CodeSynthesisContext::LambdaExpressionSubstitution:
      return "LambdaExpressionSubstitution";
    case CodeSynthesisContext::PriorTemplateArgumentSubstitution:
      return "PriorTemplateArgumentSubstitution";
    case CodeSynthesisContext::DefaultTemplateArgumentChecking:
      return "DefaultTemplateArgumentChecking";
    case CodeSynthesisContext::ExceptionSpecEvaluation:
      return "ExceptionSpecEvaluation";
    case CodeSynthesisContext::ExceptionSpecInstantiation:
      return "ExceptionSpecInstantiation";
    case CodeSynthesisContext::DeclaringSpecialMember:
      return "DeclaringSpecialMember";
    case CodeSynthesisContext::DeclaringImplicitEqualityComparison:
      return "DeclaringImplicitEqualityComparison";
    case CodeSynthesisContext::DefiningSynthesizedFunction:
      return "DefiningSynthesizedFunction";
    case CodeSynthesisContext::RewritingOperatorAsSpaceship:
      return "RewritingOperatorAsSpaceship";
    case CodeSynthesisContext::Memoization:
      return "Memoization";
    case CodeSynthesisContext::ConstraintsCheck:
      return "ConstraintsCheck";
    case CodeSynthesisContext::ConstraintSubstitution:
      return "ConstraintSubstitution";
    case CodeSynthesisContext::ConstraintNormalization:
      return "ConstraintNormalization";
    case CodeSynthesisContext::RequirementParameterInstantiation:
      return "RequirementParameterInstantiation";
    case CodeSynthesisContext::ParameterMappingSubstitution:
      return "ParameterMappingSubstitution";
    case CodeSynthesisContext::RequirementInstantiation:
      return "RequirementInstantiation";
    case CodeSynthesisContext::NestedRequirementConstraintsCheck:
      return "NestedRequirementConstraintsCheck";
    case CodeSynthesisContext::InitializingStructuredBinding:
      return "InitializingStructuredBinding";
    case CodeSynthesisContext::MarkingClassDllexported:
      return "MarkingClassDllexported";
    case CodeSynthesisContext::BuildingBuiltinDumpStructCall:
      return "BuildingBuiltinDumpStructCall";
    case CodeSynthesisContext::BuildingDeductionGuides:
      return "BuildingDeductionGuides";
    case CodeSynthesisContext::TypeAliasTemplateInstantiation:
      return "TypeAliasTemplateInstantiation";
    }
    return "";
  }

  template <bool BeginInstantiation>
  static void displayTemplightEntry(llvm::raw_ostream &Out, const Sema &TheSema,
                                    const CodeSynthesisContext &Inst) {
    std::string YAML;
    {
      llvm::raw_string_ostream OS(YAML);
      llvm::yaml::Output YO(OS);
      TemplightEntry Entry =
          getTemplightEntry<BeginInstantiation>(TheSema, Inst);
      llvm::yaml::EmptyContext Context;
      llvm::yaml::yamlize(YO, Entry, true, Context);
    }
    Out << "---" << YAML << "\n";
  }

  static void printEntryName(const Sema &TheSema, const Decl *Entity,
                             llvm::raw_string_ostream &OS) {
    auto *NamedTemplate = cast<NamedDecl>(Entity);

    PrintingPolicy Policy = TheSema.Context.getPrintingPolicy();
    // FIXME: Also ask for FullyQualifiedNames?
    Policy.SuppressDefaultTemplateArgs = false;
    NamedTemplate->getNameForDiagnostic(OS, Policy, true);

    if (!OS.str().empty())
      return;

    Decl *Ctx = Decl::castFromDeclContext(NamedTemplate->getDeclContext());
    NamedDecl *NamedCtx = dyn_cast_or_null<NamedDecl>(Ctx);

    if (const auto *Decl = dyn_cast<TagDecl>(NamedTemplate)) {
      if (const auto *R = dyn_cast<RecordDecl>(Decl)) {
        if (R->isLambda()) {
          OS << "lambda at ";
          Decl->getLocation().print(OS, TheSema.getSourceManager());
          return;
        }
      }
      OS << "unnamed " << Decl->getKindName();
      return;
    }

    assert(NamedCtx && "NamedCtx cannot be null");

    if (const auto *Decl = dyn_cast<ParmVarDecl>(NamedTemplate)) {
      OS << "unnamed function parameter " << Decl->getFunctionScopeIndex()
         << " ";
      if (Decl->getFunctionScopeDepth() > 0)
        OS << "(at depth " << Decl->getFunctionScopeDepth() << ") ";
      OS << "of ";
      NamedCtx->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
      return;
    }

    if (const auto *Decl = dyn_cast<TemplateTypeParmDecl>(NamedTemplate)) {
      if (const Type *Ty = Decl->getTypeForDecl()) {
        if (const auto *TTPT = dyn_cast_or_null<TemplateTypeParmType>(Ty)) {
          OS << "unnamed template type parameter " << TTPT->getIndex() << " ";
          if (TTPT->getDepth() > 0)
            OS << "(at depth " << TTPT->getDepth() << ") ";
          OS << "of ";
          NamedCtx->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
          return;
        }
      }
    }

    if (const auto *Decl = dyn_cast<NonTypeTemplateParmDecl>(NamedTemplate)) {
      OS << "unnamed template non-type parameter " << Decl->getIndex() << " ";
      if (Decl->getDepth() > 0)
        OS << "(at depth " << Decl->getDepth() << ") ";
      OS << "of ";
      NamedCtx->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
      return;
    }

    if (const auto *Decl = dyn_cast<TemplateTemplateParmDecl>(NamedTemplate)) {
      OS << "unnamed template template parameter " << Decl->getIndex() << " ";
      if (Decl->getDepth() > 0)
        OS << "(at depth " << Decl->getDepth() << ") ";
      OS << "of ";
      NamedCtx->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
      return;
    }

    llvm_unreachable("Failed to retrieve a name for this entry!");
    OS << "unnamed identifier";
  }

  template <bool BeginInstantiation>
  static TemplightEntry getTemplightEntry(const Sema &TheSema,
                                          const CodeSynthesisContext &Inst) {
    TemplightEntry Entry;
    Entry.Kind = toString(Inst.Kind);
    Entry.Event = BeginInstantiation ? "Begin" : "End";
    llvm::raw_string_ostream OS(Entry.Name);
    printEntryName(TheSema, Inst.Entity, OS);
    const PresumedLoc DefLoc =
        TheSema.getSourceManager().getPresumedLoc(Inst.Entity->getLocation());
    if (!DefLoc.isInvalid())
      Entry.DefinitionLocation = std::string(DefLoc.getFilename()) + ":" +
                                 std::to_string(DefLoc.getLine()) + ":" +
                                 std::to_string(DefLoc.getColumn());
    const PresumedLoc PoiLoc =
        TheSema.getSourceManager().getPresumedLoc(Inst.PointOfInstantiation);
    if (!PoiLoc.isInvalid()) {
      Entry.PointOfInstantiation = std::string(PoiLoc.getFilename()) + ":" +
                                   std::to_string(PoiLoc.getLine()) + ":" +
                                   std::to_string(PoiLoc.getColumn());
    }
    return Entry;
  }
};
} // namespace

std::unique_ptr<ASTConsumer>
TemplightDumpAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}

void TemplightDumpAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();

  // This part is normally done by ASTFrontEndAction, but needs to happen
  // before Templight observers can be created
  // FIXME: Move the truncation aspect of this into Sema, we delayed this till
  // here so the source manager would be initialized.
  EnsureSemaIsCreated(CI, *this);

  CI.getSema().TemplateInstCallbacks.push_back(
      std::make_unique<DefaultTemplateInstCallback>());
  ASTFrontendAction::ExecuteAction();
}

namespace {
  /// AST reader listener that dumps module information for a module
  /// file.
  class DumpModuleInfoListener : public ASTReaderListener {
    llvm::raw_ostream &Out;

  public:
    DumpModuleInfoListener(llvm::raw_ostream &Out) : Out(Out) { }

#define DUMP_BOOLEAN(Value, Text)                       \
    Out.indent(4) << Text << ": " << (Value? "Yes" : "No") << "\n"

    bool ReadFullVersionInformation(StringRef FullVersion) override {
      Out.indent(2)
        << "Generated by "
        << (FullVersion == getClangFullRepositoryVersion()? "this"
                                                          : "a different")
        << " Clang: " << FullVersion << "\n";
      return ASTReaderListener::ReadFullVersionInformation(FullVersion);
    }

    void ReadModuleName(StringRef ModuleName) override {
      Out.indent(2) << "Module name: " << ModuleName << "\n";
    }
    void ReadModuleMapFile(StringRef ModuleMapPath) override {
      Out.indent(2) << "Module map file: " << ModuleMapPath << "\n";
    }

    bool ReadLanguageOptions(const LangOptions &LangOpts, bool Complain,
                             bool AllowCompatibleDifferences) override {
      Out.indent(2) << "Language options:\n";
#define LANGOPT(Name, Bits, Default, Description) \
      DUMP_BOOLEAN(LangOpts.Name, Description);
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
      Out.indent(4) << Description << ": "                   \
                    << static_cast<unsigned>(LangOpts.get##Name()) << "\n";
#define VALUE_LANGOPT(Name, Bits, Default, Description) \
      Out.indent(4) << Description << ": " << LangOpts.Name << "\n";
#define BENIGN_LANGOPT(Name, Bits, Default, Description)
#define BENIGN_ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#include "clang/Basic/LangOptions.def"

      if (!LangOpts.ModuleFeatures.empty()) {
        Out.indent(4) << "Module features:\n";
        for (StringRef Feature : LangOpts.ModuleFeatures)
          Out.indent(6) << Feature << "\n";
      }

      return false;
    }

    bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                           bool AllowCompatibleDifferences) override {
      Out.indent(2) << "Target options:\n";
      Out.indent(4) << "  Triple: " << TargetOpts.Triple << "\n";
      Out.indent(4) << "  CPU: " << TargetOpts.CPU << "\n";
      Out.indent(4) << "  TuneCPU: " << TargetOpts.TuneCPU << "\n";
      Out.indent(4) << "  ABI: " << TargetOpts.ABI << "\n";

      if (!TargetOpts.FeaturesAsWritten.empty()) {
        Out.indent(4) << "Target features:\n";
        for (unsigned I = 0, N = TargetOpts.FeaturesAsWritten.size();
             I != N; ++I) {
          Out.indent(6) << TargetOpts.FeaturesAsWritten[I] << "\n";
        }
      }

      return false;
    }

    bool ReadDiagnosticOptions(IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
                               bool Complain) override {
      Out.indent(2) << "Diagnostic options:\n";
#define DIAGOPT(Name, Bits, Default) DUMP_BOOLEAN(DiagOpts->Name, #Name);
#define ENUM_DIAGOPT(Name, Type, Bits, Default) \
      Out.indent(4) << #Name << ": " << DiagOpts->get##Name() << "\n";
#define VALUE_DIAGOPT(Name, Bits, Default) \
      Out.indent(4) << #Name << ": " << DiagOpts->Name << "\n";
#include "clang/Basic/DiagnosticOptions.def"

      Out.indent(4) << "Diagnostic flags:\n";
      for (const std::string &Warning : DiagOpts->Warnings)
        Out.indent(6) << "-W" << Warning << "\n";
      for (const std::string &Remark : DiagOpts->Remarks)
        Out.indent(6) << "-R" << Remark << "\n";

      return false;
    }

    bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                 StringRef SpecificModuleCachePath,
                                 bool Complain) override {
      Out.indent(2) << "Header search options:\n";
      Out.indent(4) << "System root [-isysroot=]: '" << HSOpts.Sysroot << "'\n";
      Out.indent(4) << "Resource dir [ -resource-dir=]: '" << HSOpts.ResourceDir << "'\n";
      Out.indent(4) << "Module Cache: '" << SpecificModuleCachePath << "'\n";
      DUMP_BOOLEAN(HSOpts.UseBuiltinIncludes,
                   "Use builtin include directories [-nobuiltininc]");
      DUMP_BOOLEAN(HSOpts.UseStandardSystemIncludes,
                   "Use standard system include directories [-nostdinc]");
      DUMP_BOOLEAN(HSOpts.UseStandardCXXIncludes,
                   "Use standard C++ include directories [-nostdinc++]");
      DUMP_BOOLEAN(HSOpts.UseLibcxx,
                   "Use libc++ (rather than libstdc++) [-stdlib=]");
      return false;
    }

    bool ReadHeaderSearchPaths(const HeaderSearchOptions &HSOpts,
                               bool Complain) override {
      Out.indent(2) << "Header search paths:\n";
      Out.indent(4) << "User entries:\n";
      for (const auto &Entry : HSOpts.UserEntries)
        Out.indent(6) << Entry.Path << "\n";
      Out.indent(4) << "System header prefixes:\n";
      for (const auto &Prefix : HSOpts.SystemHeaderPrefixes)
        Out.indent(6) << Prefix.Prefix << "\n";
      Out.indent(4) << "VFS overlay files:\n";
      for (const auto &Overlay : HSOpts.VFSOverlayFiles)
        Out.indent(6) << Overlay << "\n";
      return false;
    }

    bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                 bool ReadMacros, bool Complain,
                                 std::string &SuggestedPredefines) override {
      Out.indent(2) << "Preprocessor options:\n";
      DUMP_BOOLEAN(PPOpts.UsePredefines,
                   "Uses compiler/target-specific predefines [-undef]");
      DUMP_BOOLEAN(PPOpts.DetailedRecord,
                   "Uses detailed preprocessing record (for indexing)");

      if (ReadMacros) {
        Out.indent(4) << "Predefined macros:\n";
      }

      for (std::vector<std::pair<std::string, bool/*isUndef*/> >::const_iterator
             I = PPOpts.Macros.begin(), IEnd = PPOpts.Macros.end();
           I != IEnd; ++I) {
        Out.indent(6);
        if (I->second)
          Out << "-U";
        else
          Out << "-D";
        Out << I->first << "\n";
      }
      return false;
    }

    /// Indicates that a particular module file extension has been read.
    void readModuleFileExtension(
           const ModuleFileExtensionMetadata &Metadata) override {
      Out.indent(2) << "Module file extension '"
                    << Metadata.BlockName << "' " << Metadata.MajorVersion
                    << "." << Metadata.MinorVersion;
      if (!Metadata.UserInfo.empty()) {
        Out << ": ";
        Out.write_escaped(Metadata.UserInfo);
      }

      Out << "\n";
    }

    /// Tells the \c ASTReaderListener that we want to receive the
    /// input files of the AST file via \c visitInputFile.
    bool needsInputFileVisitation() override { return true; }

    /// Tells the \c ASTReaderListener that we want to receive the
    /// input files of the AST file via \c visitInputFile.
    bool needsSystemInputFileVisitation() override { return true; }

    /// Indicates that the AST file contains particular input file.
    ///
    /// \returns true to continue receiving the next input file, false to stop.
    bool visitInputFile(StringRef Filename, bool isSystem,
                        bool isOverridden, bool isExplicitModule) override {

      Out.indent(2) << "Input file: " << Filename;

      if (isSystem || isOverridden || isExplicitModule) {
        Out << " [";
        if (isSystem) {
          Out << "System";
          if (isOverridden || isExplicitModule)
            Out << ", ";
        }
        if (isOverridden) {
          Out << "Overridden";
          if (isExplicitModule)
            Out << ", ";
        }
        if (isExplicitModule)
          Out << "ExplicitModule";

        Out << "]";
      }

      Out << "\n";

      return true;
    }

    /// Returns true if this \c ASTReaderListener wants to receive the
    /// imports of the AST file via \c visitImport, false otherwise.
    bool needsImportVisitation() const override { return true; }

    /// If needsImportVisitation returns \c true, this is called for each
    /// AST file imported by this AST file.
    void visitImport(StringRef ModuleName, StringRef Filename) override {
      Out.indent(2) << "Imports module '" << ModuleName
                    << "': " << Filename.str() << "\n";
    }
#undef DUMP_BOOLEAN
  };
}

bool DumpModuleInfoAction::BeginInvocation(CompilerInstance &CI) {
  // The Object file reader also supports raw ast files and there is no point in
  // being strict about the module file format in -module-file-info mode.
  CI.getHeaderSearchOpts().ModuleFormat = "obj";
  return true;
}

static StringRef ModuleKindName(Module::ModuleKind MK) {
  switch (MK) {
  case Module::ModuleMapModule:
    return "Module Map Module";
  case Module::ModuleInterfaceUnit:
    return "Interface Unit";
  case Module::ModuleImplementationUnit:
    return "Implementation Unit";
  case Module::ModulePartitionInterface:
    return "Partition Interface";
  case Module::ModulePartitionImplementation:
    return "Partition Implementation";
  case Module::ModuleHeaderUnit:
    return "Header Unit";
  case Module::ExplicitGlobalModuleFragment:
    return "Global Module Fragment";
  case Module::ImplicitGlobalModuleFragment:
    return "Implicit Module Fragment";
  case Module::PrivateModuleFragment:
    return "Private Module Fragment";
  }
  llvm_unreachable("unknown module kind!");
}

void DumpModuleInfoAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();

  // Don't process files of type other than module to avoid crash
  if (!isCurrentFileAST()) {
    CI.getDiagnostics().Report(diag::err_file_is_not_module)
        << getCurrentFile();
    return;
  }

  // Set up the output file.
  StringRef OutputFileName = CI.getFrontendOpts().OutputFile;
  if (!OutputFileName.empty() && OutputFileName != "-") {
    std::error_code EC;
    OutputStream.reset(new llvm::raw_fd_ostream(
        OutputFileName.str(), EC, llvm::sys::fs::OF_TextWithCRLF));
  }
  llvm::raw_ostream &Out = OutputStream ? *OutputStream : llvm::outs();

  Out << "Information for module file '" << getCurrentFile() << "':\n";
  auto &FileMgr = CI.getFileManager();
  auto Buffer = FileMgr.getBufferForFile(getCurrentFile());
  StringRef Magic = (*Buffer)->getMemBufferRef().getBuffer();
  bool IsRaw = Magic.starts_with("CPCH");
  Out << "  Module format: " << (IsRaw ? "raw" : "obj") << "\n";

  Preprocessor &PP = CI.getPreprocessor();
  DumpModuleInfoListener Listener(Out);
  HeaderSearchOptions &HSOpts = PP.getHeaderSearchInfo().getHeaderSearchOpts();

  // The FrontendAction::BeginSourceFile () method loads the AST so that much
  // of the information is already available and modules should have been
  // loaded.

  const LangOptions &LO = getCurrentASTUnit().getLangOpts();
  if (LO.CPlusPlusModules && !LO.CurrentModule.empty()) {
    ASTReader *R = getCurrentASTUnit().getASTReader().get();
    unsigned SubModuleCount = R->getTotalNumSubmodules();
    serialization::ModuleFile &MF = R->getModuleManager().getPrimaryModule();
    Out << "  ====== C++20 Module structure ======\n";

    if (MF.ModuleName != LO.CurrentModule)
      Out << "  Mismatched module names : " << MF.ModuleName << " and "
          << LO.CurrentModule << "\n";

    struct SubModInfo {
      unsigned Idx;
      Module *Mod;
      Module::ModuleKind Kind;
      std::string &Name;
      bool Seen;
    };
    std::map<std::string, SubModInfo> SubModMap;
    auto PrintSubMapEntry = [&](std::string Name, Module::ModuleKind Kind) {
      Out << "    " << ModuleKindName(Kind) << " '" << Name << "'";
      auto I = SubModMap.find(Name);
      if (I == SubModMap.end())
        Out << " was not found in the sub modules!\n";
      else {
        I->second.Seen = true;
        Out << " is at index #" << I->second.Idx << "\n";
      }
    };
    Module *Primary = nullptr;
    for (unsigned Idx = 0; Idx <= SubModuleCount; ++Idx) {
      Module *M = R->getModule(Idx);
      if (!M)
        continue;
      if (M->Name == LO.CurrentModule) {
        Primary = M;
        Out << "  " << ModuleKindName(M->Kind) << " '" << LO.CurrentModule
            << "' is the Primary Module at index #" << Idx << "\n";
        SubModMap.insert({M->Name, {Idx, M, M->Kind, M->Name, true}});
      } else
        SubModMap.insert({M->Name, {Idx, M, M->Kind, M->Name, false}});
    }
    if (Primary) {
      if (!Primary->submodules().empty())
        Out << "   Sub Modules:\n";
      for (auto *MI : Primary->submodules()) {
        PrintSubMapEntry(MI->Name, MI->Kind);
      }
      if (!Primary->Imports.empty())
        Out << "   Imports:\n";
      for (auto *IMP : Primary->Imports) {
        PrintSubMapEntry(IMP->Name, IMP->Kind);
      }
      if (!Primary->Exports.empty())
        Out << "   Exports:\n";
      for (unsigned MN = 0, N = Primary->Exports.size(); MN != N; ++MN) {
        if (Module *M = Primary->Exports[MN].getPointer()) {
          PrintSubMapEntry(M->Name, M->Kind);
        }
      }
    }

    // Emit the macro definitions in the module file so that we can know how
    // much definitions in the module file quickly.
    // TODO: Emit the macro definition bodies completely.
    if (auto FilteredMacros = llvm::make_filter_range(
            R->getPreprocessor().macros(),
            [](const auto &Macro) { return Macro.first->isFromAST(); });
        !FilteredMacros.empty()) {
      Out << "   Macro Definitions:\n";
      for (/*<IdentifierInfo *, MacroState> pair*/ const auto &Macro :
           FilteredMacros)
        Out << "     " << Macro.first->getName() << "\n";
    }

    // Now let's print out any modules we did not see as part of the Primary.
    for (const auto &SM : SubModMap) {
      if (!SM.second.Seen && SM.second.Mod) {
        Out << "  " << ModuleKindName(SM.second.Kind) << " '" << SM.first
            << "' at index #" << SM.second.Idx
            << " has no direct reference in the Primary\n";
      }
    }
    Out << "  ====== ======\n";
  }

  // The reminder of the output is produced from the listener as the AST
  // FileCcontrolBlock is (re-)parsed.
  ASTReader::readASTFileControlBlock(
      getCurrentFile(), FileMgr, CI.getModuleCache(),
      CI.getPCHContainerReader(),
      /*FindModuleFileExtensions=*/true, Listener,
      HSOpts.ModulesValidateDiagnosticOptions);
}

//===----------------------------------------------------------------------===//
// Preprocessor Actions
//===----------------------------------------------------------------------===//

void DumpRawTokensAction::ExecuteAction() {
  Preprocessor &PP = getCompilerInstance().getPreprocessor();
  SourceManager &SM = PP.getSourceManager();

  // Start lexing the specified input file.
  llvm::MemoryBufferRef FromFile = SM.getBufferOrFake(SM.getMainFileID());
  Lexer RawLex(SM.getMainFileID(), FromFile, SM, PP.getLangOpts());
  RawLex.SetKeepWhitespaceMode(true);

  Token RawTok;
  RawLex.LexFromRawLexer(RawTok);
  while (RawTok.isNot(tok::eof)) {
    PP.DumpToken(RawTok, true);
    llvm::errs() << "\n";
    RawLex.LexFromRawLexer(RawTok);
  }
}

void DumpTokensAction::ExecuteAction() {
  Preprocessor &PP = getCompilerInstance().getPreprocessor();
  // Start preprocessing the specified input file.
  Token Tok;
  PP.EnterMainSourceFile();
  do {
    PP.Lex(Tok);
    PP.DumpToken(Tok, true);
    llvm::errs() << "\n";
  } while (Tok.isNot(tok::eof));
}

void PreprocessOnlyAction::ExecuteAction() {
  Preprocessor &PP = getCompilerInstance().getPreprocessor();

  // Ignore unknown pragmas.
  PP.IgnorePragmas();

  Token Tok;
  // Start parsing the specified input file.
  PP.EnterMainSourceFile();
  do {
    PP.Lex(Tok);
  } while (Tok.isNot(tok::eof));
}

void PrintPreprocessedAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  // Output file may need to be set to 'Binary', to avoid converting Unix style
  // line feeds (<LF>) to Microsoft style line feeds (<CR><LF>) on Windows.
  //
  // Look to see what type of line endings the file uses. If there's a
  // CRLF, then we won't open the file up in binary mode. If there is
  // just an LF or CR, then we will open the file up in binary mode.
  // In this fashion, the output format should match the input format, unless
  // the input format has inconsistent line endings.
  //
  // This should be a relatively fast operation since most files won't have
  // all of their source code on a single line. However, that is still a
  // concern, so if we scan for too long, we'll just assume the file should
  // be opened in binary mode.

  bool BinaryMode = false;
  if (llvm::Triple(LLVM_HOST_TRIPLE).isOSWindows()) {
    BinaryMode = true;
    const SourceManager &SM = CI.getSourceManager();
    if (std::optional<llvm::MemoryBufferRef> Buffer =
            SM.getBufferOrNone(SM.getMainFileID())) {
      const char *cur = Buffer->getBufferStart();
      const char *end = Buffer->getBufferEnd();
      const char *next = (cur != end) ? cur + 1 : end;

      // Limit ourselves to only scanning 256 characters into the source
      // file.  This is mostly a check in case the file has no
      // newlines whatsoever.
      if (end - cur > 256)
        end = cur + 256;

      while (next < end) {
        if (*cur == 0x0D) {  // CR
          if (*next == 0x0A) // CRLF
            BinaryMode = false;

          break;
        } else if (*cur == 0x0A) // LF
          break;

        ++cur;
        ++next;
      }
    }
  }

  std::unique_ptr<raw_ostream> OS =
      CI.createDefaultOutputFile(BinaryMode, getCurrentFileOrBufferName());
  if (!OS) return;

  // If we're preprocessing a module map, start by dumping the contents of the
  // module itself before switching to the input buffer.
  auto &Input = getCurrentInput();
  if (Input.getKind().getFormat() == InputKind::ModuleMap) {
    if (Input.isFile()) {
      (*OS) << "# 1 \"";
      OS->write_escaped(Input.getFile());
      (*OS) << "\"\n";
    }
    getCurrentModule()->print(*OS);
    (*OS) << "#pragma clang module contents\n";
  }

  DoPrintPreprocessedInput(CI.getPreprocessor(), OS.get(),
                           CI.getPreprocessorOutputOpts());
}

void PrintPreambleAction::ExecuteAction() {
  switch (getCurrentFileKind().getLanguage()) {
  case Language::C:
  case Language::CXX:
  case Language::ObjC:
  case Language::ObjCXX:
  case Language::OpenCL:
  case Language::OpenCLCXX:
  case Language::CUDA:
  case Language::HIP:
  case Language::HLSL:
  case Language::CIR:
    break;

  case Language::Unknown:
  case Language::Asm:
  case Language::LLVM_IR:
  case Language::RenderScript:
    // We can't do anything with these.
    return;
  }

  // We don't expect to find any #include directives in a preprocessed input.
  if (getCurrentFileKind().isPreprocessed())
    return;

  CompilerInstance &CI = getCompilerInstance();
  auto Buffer = CI.getFileManager().getBufferForFile(getCurrentFile());
  if (Buffer) {
    unsigned Preamble =
        Lexer::ComputePreamble((*Buffer)->getBuffer(), CI.getLangOpts()).Size;
    llvm::outs().write((*Buffer)->getBufferStart(), Preamble);
  }
}

void DumpCompilerOptionsAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  std::unique_ptr<raw_ostream> OSP =
      CI.createDefaultOutputFile(false, getCurrentFile());
  if (!OSP)
    return;

  raw_ostream &OS = *OSP;
  const Preprocessor &PP = CI.getPreprocessor();
  const LangOptions &LangOpts = PP.getLangOpts();

  // FIXME: Rather than manually format the JSON (which is awkward due to
  // needing to remove trailing commas), this should make use of a JSON library.
  // FIXME: Instead of printing enums as an integral value and specifying the
  // type as a separate field, use introspection to print the enumerator.

  OS << "{\n";
  OS << "\n\"features\" : [\n";
  {
    llvm::SmallString<128> Str;
#define FEATURE(Name, Predicate)                                               \
  ("\t{\"" #Name "\" : " + llvm::Twine(Predicate ? "true" : "false") + "},\n") \
      .toVector(Str);
#include "clang/Basic/Features.def"
#undef FEATURE
    // Remove the newline and comma from the last entry to ensure this remains
    // valid JSON.
    OS << Str.substr(0, Str.size() - 2);
  }
  OS << "\n],\n";

  OS << "\n\"extensions\" : [\n";
  {
    llvm::SmallString<128> Str;
#define EXTENSION(Name, Predicate)                                             \
  ("\t{\"" #Name "\" : " + llvm::Twine(Predicate ? "true" : "false") + "},\n") \
      .toVector(Str);
#include "clang/Basic/Features.def"
#undef EXTENSION
    // Remove the newline and comma from the last entry to ensure this remains
    // valid JSON.
    OS << Str.substr(0, Str.size() - 2);
  }
  OS << "\n]\n";

  OS << "}";
}

void PrintDependencyDirectivesSourceMinimizerAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  SourceManager &SM = CI.getPreprocessor().getSourceManager();
  llvm::MemoryBufferRef FromFile = SM.getBufferOrFake(SM.getMainFileID());

  llvm::SmallVector<dependency_directives_scan::Token, 16> Tokens;
  llvm::SmallVector<dependency_directives_scan::Directive, 32> Directives;
  if (scanSourceForDependencyDirectives(
          FromFile.getBuffer(), Tokens, Directives, &CI.getDiagnostics(),
          SM.getLocForStartOfFile(SM.getMainFileID()))) {
    assert(CI.getDiagnostics().hasErrorOccurred() &&
           "no errors reported for failure");

    // Preprocess the source when verifying the diagnostics to capture the
    // 'expected' comments.
    if (CI.getDiagnosticOpts().VerifyDiagnostics) {
      // Make sure we don't emit new diagnostics!
      CI.getDiagnostics().setSuppressAllDiagnostics(true);
      Preprocessor &PP = getCompilerInstance().getPreprocessor();
      PP.EnterMainSourceFile();
      Token Tok;
      do {
        PP.Lex(Tok);
      } while (Tok.isNot(tok::eof));
    }
    return;
  }
  printDependencyDirectivesAsSource(FromFile.getBuffer(), Directives,
                                    llvm::outs());
}

void GetDependenciesByModuleNameAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  Preprocessor &PP = CI.getPreprocessor();
  SourceManager &SM = PP.getSourceManager();
  FileID MainFileID = SM.getMainFileID();
  SourceLocation FileStart = SM.getLocForStartOfFile(MainFileID);
  SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Path;
  IdentifierInfo *ModuleID = PP.getIdentifierInfo(ModuleName);
  Path.push_back(std::make_pair(ModuleID, FileStart));
  auto ModResult = CI.loadModule(FileStart, Path, Module::Hidden, false);
  PPCallbacks *CB = PP.getPPCallbacks();
  CB->moduleImport(SourceLocation(), Path, ModResult);
}
