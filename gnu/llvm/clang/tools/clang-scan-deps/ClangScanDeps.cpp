//===- ClangScanDeps.cpp - Implementation of clang-scan-deps --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningTool.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningWorker.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/Timer.h"
#include "llvm/TargetParser/Host.h"
#include <mutex>
#include <optional>
#include <thread>

#include "Opts.inc"

using namespace clang;
using namespace tooling::dependencies;

namespace {

using namespace llvm::opt;
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  constexpr llvm::StringLiteral NAME##_init[] = VALUE;                         \
  constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                          \
      NAME##_init, std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

const llvm::opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class ScanDepsOptTable : public llvm::opt::GenericOptTable {
public:
  ScanDepsOptTable() : GenericOptTable(InfoTable) {
    setGroupedShortOptions(true);
  }
};

enum ResourceDirRecipeKind {
  RDRK_ModifyCompilerPath,
  RDRK_InvokeCompiler,
};

static std::string OutputFileName = "-";
static ScanningMode ScanMode = ScanningMode::DependencyDirectivesScan;
static ScanningOutputFormat Format = ScanningOutputFormat::Make;
static ScanningOptimizations OptimizeArgs;
static std::string ModuleFilesDir;
static bool EagerLoadModules;
static unsigned NumThreads = 0;
static std::string CompilationDB;
static std::string ModuleName;
static std::vector<std::string> ModuleDepTargets;
static bool DeprecatedDriverCommand;
static ResourceDirRecipeKind ResourceDirRecipe;
static bool Verbose;
static bool PrintTiming;
static llvm::BumpPtrAllocator Alloc;
static llvm::StringSaver Saver{Alloc};
static std::vector<const char *> CommandLine;

#ifndef NDEBUG
static constexpr bool DoRoundTripDefault = true;
#else
static constexpr bool DoRoundTripDefault = false;
#endif

static bool RoundTripArgs = DoRoundTripDefault;

static void ParseArgs(int argc, char **argv) {
  ScanDepsOptTable Tbl;
  llvm::StringRef ToolName = argv[0];
  llvm::opt::InputArgList Args =
      Tbl.parseArgs(argc, argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) {
        llvm::errs() << Msg << '\n';
        std::exit(1);
      });

  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(llvm::outs(), "clang-scan-deps [options]", "clang-scan-deps");
    std::exit(0);
  }
  if (Args.hasArg(OPT_version)) {
    llvm::outs() << ToolName << '\n';
    llvm::cl::PrintVersionMessage();
    std::exit(0);
  }
  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_mode_EQ)) {
    auto ModeType =
        llvm::StringSwitch<std::optional<ScanningMode>>(A->getValue())
            .Case("preprocess-dependency-directives",
                  ScanningMode::DependencyDirectivesScan)
            .Case("preprocess", ScanningMode::CanonicalPreprocessing)
            .Default(std::nullopt);
    if (!ModeType) {
      llvm::errs() << ToolName
                   << ": for the --mode option: Cannot find option named '"
                   << A->getValue() << "'\n";
      std::exit(1);
    }
    ScanMode = *ModeType;
  }

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_format_EQ)) {
    auto FormatType =
        llvm::StringSwitch<std::optional<ScanningOutputFormat>>(A->getValue())
            .Case("make", ScanningOutputFormat::Make)
            .Case("p1689", ScanningOutputFormat::P1689)
            .Case("experimental-full", ScanningOutputFormat::Full)
            .Default(std::nullopt);
    if (!FormatType) {
      llvm::errs() << ToolName
                   << ": for the --format option: Cannot find option named '"
                   << A->getValue() << "'\n";
      std::exit(1);
    }
    Format = *FormatType;
  }

  std::vector<std::string> OptimizationFlags =
      Args.getAllArgValues(OPT_optimize_args_EQ);
  OptimizeArgs = ScanningOptimizations::None;
  for (const auto &Arg : OptimizationFlags) {
    auto Optimization =
        llvm::StringSwitch<std::optional<ScanningOptimizations>>(Arg)
            .Case("none", ScanningOptimizations::None)
            .Case("header-search", ScanningOptimizations::HeaderSearch)
            .Case("system-warnings", ScanningOptimizations::SystemWarnings)
            .Case("vfs", ScanningOptimizations::VFS)
            .Case("canonicalize-macros", ScanningOptimizations::Macros)
            .Case("all", ScanningOptimizations::All)
            .Default(std::nullopt);
    if (!Optimization) {
      llvm::errs()
          << ToolName
          << ": for the --optimize-args option: Cannot find option named '"
          << Arg << "'\n";
      std::exit(1);
    }
    OptimizeArgs |= *Optimization;
  }
  if (OptimizationFlags.empty())
    OptimizeArgs = ScanningOptimizations::Default;

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_module_files_dir_EQ))
    ModuleFilesDir = A->getValue();

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_o))
    OutputFileName = A->getValue();

  EagerLoadModules = Args.hasArg(OPT_eager_load_pcm);

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_j)) {
    StringRef S{A->getValue()};
    if (!llvm::to_integer(S, NumThreads, 0)) {
      llvm::errs() << ToolName << ": for the -j option: '" << S
                   << "' value invalid for uint argument!\n";
      std::exit(1);
    }
  }

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_compilation_database_EQ))
    CompilationDB = A->getValue();

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_module_name_EQ))
    ModuleName = A->getValue();

  for (const llvm::opt::Arg *A : Args.filtered(OPT_dependency_target_EQ))
    ModuleDepTargets.emplace_back(A->getValue());

  DeprecatedDriverCommand = Args.hasArg(OPT_deprecated_driver_command);

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_resource_dir_recipe_EQ)) {
    auto Kind =
        llvm::StringSwitch<std::optional<ResourceDirRecipeKind>>(A->getValue())
            .Case("modify-compiler-path", RDRK_ModifyCompilerPath)
            .Case("invoke-compiler", RDRK_InvokeCompiler)
            .Default(std::nullopt);
    if (!Kind) {
      llvm::errs() << ToolName
                   << ": for the --resource-dir-recipe option: Cannot find "
                      "option named '"
                   << A->getValue() << "'\n";
      std::exit(1);
    }
    ResourceDirRecipe = *Kind;
  }

  PrintTiming = Args.hasArg(OPT_print_timing);

  Verbose = Args.hasArg(OPT_verbose);

  RoundTripArgs = Args.hasArg(OPT_round_trip_args);

  if (const llvm::opt::Arg *A = Args.getLastArgNoClaim(OPT_DASH_DASH))
    CommandLine.assign(A->getValues().begin(), A->getValues().end());
}

class SharedStream {
public:
  SharedStream(raw_ostream &OS) : OS(OS) {}
  void applyLocked(llvm::function_ref<void(raw_ostream &OS)> Fn) {
    std::unique_lock<std::mutex> LockGuard(Lock);
    Fn(OS);
    OS.flush();
  }

private:
  std::mutex Lock;
  raw_ostream &OS;
};

class ResourceDirectoryCache {
public:
  /// findResourceDir finds the resource directory relative to the clang
  /// compiler being used in Args, by running it with "-print-resource-dir"
  /// option and cache the results for reuse. \returns resource directory path
  /// associated with the given invocation command or empty string if the
  /// compiler path is NOT an absolute path.
  StringRef findResourceDir(const tooling::CommandLineArguments &Args,
                            bool ClangCLMode) {
    if (Args.size() < 1)
      return "";

    const std::string &ClangBinaryPath = Args[0];
    if (!llvm::sys::path::is_absolute(ClangBinaryPath))
      return "";

    const std::string &ClangBinaryName =
        std::string(llvm::sys::path::filename(ClangBinaryPath));

    std::unique_lock<std::mutex> LockGuard(CacheLock);
    const auto &CachedResourceDir = Cache.find(ClangBinaryPath);
    if (CachedResourceDir != Cache.end())
      return CachedResourceDir->second;

    std::vector<StringRef> PrintResourceDirArgs{ClangBinaryName};
    if (ClangCLMode)
      PrintResourceDirArgs.push_back("/clang:-print-resource-dir");
    else
      PrintResourceDirArgs.push_back("-print-resource-dir");

    llvm::SmallString<64> OutputFile, ErrorFile;
    llvm::sys::fs::createTemporaryFile("print-resource-dir-output",
                                       "" /*no-suffix*/, OutputFile);
    llvm::sys::fs::createTemporaryFile("print-resource-dir-error",
                                       "" /*no-suffix*/, ErrorFile);
    llvm::FileRemover OutputRemover(OutputFile.c_str());
    llvm::FileRemover ErrorRemover(ErrorFile.c_str());
    std::optional<StringRef> Redirects[] = {
        {""}, // Stdin
        OutputFile.str(),
        ErrorFile.str(),
    };
    if (llvm::sys::ExecuteAndWait(ClangBinaryPath, PrintResourceDirArgs, {},
                                  Redirects)) {
      auto ErrorBuf = llvm::MemoryBuffer::getFile(ErrorFile.c_str());
      llvm::errs() << ErrorBuf.get()->getBuffer();
      return "";
    }

    auto OutputBuf = llvm::MemoryBuffer::getFile(OutputFile.c_str());
    if (!OutputBuf)
      return "";
    StringRef Output = OutputBuf.get()->getBuffer().rtrim('\n');

    Cache[ClangBinaryPath] = Output.str();
    return Cache[ClangBinaryPath];
  }

private:
  std::map<std::string, std::string> Cache;
  std::mutex CacheLock;
};

} // end anonymous namespace

/// Takes the result of a dependency scan and prints error / dependency files
/// based on the result.
///
/// \returns True on error.
static bool
handleMakeDependencyToolResult(const std::string &Input,
                               llvm::Expected<std::string> &MaybeFile,
                               SharedStream &OS, SharedStream &Errs) {
  if (!MaybeFile) {
    llvm::handleAllErrors(
        MaybeFile.takeError(), [&Input, &Errs](llvm::StringError &Err) {
          Errs.applyLocked([&](raw_ostream &OS) {
            OS << "Error while scanning dependencies for " << Input << ":\n";
            OS << Err.getMessage();
          });
        });
    return true;
  }
  OS.applyLocked([&](raw_ostream &OS) { OS << *MaybeFile; });
  return false;
}

static llvm::json::Array toJSONSorted(const llvm::StringSet<> &Set) {
  std::vector<llvm::StringRef> Strings;
  for (auto &&I : Set)
    Strings.push_back(I.getKey());
  llvm::sort(Strings);
  return llvm::json::Array(Strings);
}

// Technically, we don't need to sort the dependency list to get determinism.
// Leaving these be will simply preserve the import order.
static llvm::json::Array toJSONSorted(std::vector<ModuleID> V) {
  llvm::sort(V);

  llvm::json::Array Ret;
  for (const ModuleID &MID : V)
    Ret.push_back(llvm::json::Object(
        {{"module-name", MID.ModuleName}, {"context-hash", MID.ContextHash}}));
  return Ret;
}

static llvm::json::Array
toJSONSorted(llvm::SmallVector<Module::LinkLibrary, 2> &LinkLibs) {
  llvm::sort(LinkLibs, [](const Module::LinkLibrary &lhs,
                          const Module::LinkLibrary &rhs) {
    return lhs.Library < rhs.Library;
  });

  llvm::json::Array Ret;
  for (const Module::LinkLibrary &LL : LinkLibs)
    Ret.push_back(llvm::json::Object(
        {{"link-name", LL.Library}, {"isFramework", LL.IsFramework}}));
  return Ret;
}

// Thread safe.
class FullDeps {
public:
  FullDeps(size_t NumInputs) : Inputs(NumInputs) {}

  void mergeDeps(StringRef Input, TranslationUnitDeps TUDeps,
                 size_t InputIndex) {
    mergeDeps(std::move(TUDeps.ModuleGraph), InputIndex);

    InputDeps ID;
    ID.FileName = std::string(Input);
    ID.ContextHash = std::move(TUDeps.ID.ContextHash);
    ID.FileDeps = std::move(TUDeps.FileDeps);
    ID.ModuleDeps = std::move(TUDeps.ClangModuleDeps);
    ID.DriverCommandLine = std::move(TUDeps.DriverCommandLine);
    ID.Commands = std::move(TUDeps.Commands);

    assert(InputIndex < Inputs.size() && "Input index out of bounds");
    assert(Inputs[InputIndex].FileName.empty() && "Result already populated");
    Inputs[InputIndex] = std::move(ID);
  }

  void mergeDeps(ModuleDepsGraph Graph, size_t InputIndex) {
    std::vector<ModuleDeps *> NewMDs;
    {
      std::unique_lock<std::mutex> ul(Lock);
      for (const ModuleDeps &MD : Graph) {
        auto I = Modules.find({MD.ID, 0});
        if (I != Modules.end()) {
          I->first.InputIndex = std::min(I->first.InputIndex, InputIndex);
          continue;
        }
        auto Res = Modules.insert(I, {{MD.ID, InputIndex}, std::move(MD)});
        NewMDs.push_back(&Res->second);
      }
      // First call to \c getBuildArguments is somewhat expensive. Let's call it
      // on the current thread (instead of the main one), and outside the
      // critical section.
      for (ModuleDeps *MD : NewMDs)
        (void)MD->getBuildArguments();
    }
  }

  bool roundTripCommand(ArrayRef<std::string> ArgStrs,
                        DiagnosticsEngine &Diags) {
    if (ArgStrs.empty() || ArgStrs[0] != "-cc1")
      return false;
    SmallVector<const char *> Args;
    for (const std::string &Arg : ArgStrs)
      Args.push_back(Arg.c_str());
    return !CompilerInvocation::checkCC1RoundTrip(Args, Diags);
  }

  // Returns \c true if any command lines fail to round-trip. We expect
  // commands already be canonical when output by the scanner.
  bool roundTripCommands(raw_ostream &ErrOS) {
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions{};
    TextDiagnosticPrinter DiagConsumer(ErrOS, &*DiagOpts);
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
        CompilerInstance::createDiagnostics(&*DiagOpts, &DiagConsumer,
                                            /*ShouldOwnClient=*/false);

    for (auto &&M : Modules)
      if (roundTripCommand(M.second.getBuildArguments(), *Diags))
        return true;

    for (auto &&I : Inputs)
      for (const auto &Cmd : I.Commands)
        if (roundTripCommand(Cmd.Arguments, *Diags))
          return true;

    return false;
  }

  void printFullOutput(raw_ostream &OS) {
    // Skip sorting modules and constructing the JSON object if the output
    // cannot be observed anyway. This makes timings less noisy.
    if (&OS == &llvm::nulls())
      return;

    // Sort the modules by name to get a deterministic order.
    std::vector<IndexedModuleID> ModuleIDs;
    for (auto &&M : Modules)
      ModuleIDs.push_back(M.first);
    llvm::sort(ModuleIDs);

    using namespace llvm::json;

    Array OutModules;
    for (auto &&ModID : ModuleIDs) {
      auto &MD = Modules[ModID];
      Object O{{"name", MD.ID.ModuleName},
               {"context-hash", MD.ID.ContextHash},
               {"file-deps", toJSONSorted(MD.FileDeps)},
               {"clang-module-deps", toJSONSorted(MD.ClangModuleDeps)},
               {"clang-modulemap-file", MD.ClangModuleMapFile},
               {"command-line", MD.getBuildArguments()},
               {"link-libraries", toJSONSorted(MD.LinkLibraries)}};
      OutModules.push_back(std::move(O));
    }

    Array TUs;
    for (auto &&I : Inputs) {
      Array Commands;
      if (I.DriverCommandLine.empty()) {
        for (const auto &Cmd : I.Commands) {
          Object O{
              {"input-file", I.FileName},
              {"clang-context-hash", I.ContextHash},
              {"file-deps", I.FileDeps},
              {"clang-module-deps", toJSONSorted(I.ModuleDeps)},
              {"executable", Cmd.Executable},
              {"command-line", Cmd.Arguments},
          };
          Commands.push_back(std::move(O));
        }
      } else {
        Object O{
            {"input-file", I.FileName},
            {"clang-context-hash", I.ContextHash},
            {"file-deps", I.FileDeps},
            {"clang-module-deps", toJSONSorted(I.ModuleDeps)},
            {"executable", "clang"},
            {"command-line", I.DriverCommandLine},
        };
        Commands.push_back(std::move(O));
      }
      TUs.push_back(Object{
          {"commands", std::move(Commands)},
      });
    }

    Object Output{
        {"modules", std::move(OutModules)},
        {"translation-units", std::move(TUs)},
    };

    OS << llvm::formatv("{0:2}\n", Value(std::move(Output)));
  }

private:
  struct IndexedModuleID {
    ModuleID ID;

    // FIXME: This is mutable so that it can still be updated after insertion
    //  into an unordered associative container. This is "fine", since this
    //  field doesn't contribute to the hash, but it's a brittle hack.
    mutable size_t InputIndex;

    bool operator==(const IndexedModuleID &Other) const {
      return ID == Other.ID;
    }

    bool operator<(const IndexedModuleID &Other) const {
      /// We need the output of clang-scan-deps to be deterministic. However,
      /// the dependency graph may contain two modules with the same name. How
      /// do we decide which one to print first? If we made that decision based
      /// on the context hash, the ordering would be deterministic, but
      /// different across machines. This can happen for example when the inputs
      /// or the SDKs (which both contribute to the "context" hash) live in
      /// different absolute locations. We solve that by tracking the index of
      /// the first input TU that (transitively) imports the dependency, which
      /// is always the same for the same input, resulting in deterministic
      /// sorting that's also reproducible across machines.
      return std::tie(ID.ModuleName, InputIndex) <
             std::tie(Other.ID.ModuleName, Other.InputIndex);
    }

    struct Hasher {
      std::size_t operator()(const IndexedModuleID &IMID) const {
        return llvm::hash_value(IMID.ID);
      }
    };
  };

  struct InputDeps {
    std::string FileName;
    std::string ContextHash;
    std::vector<std::string> FileDeps;
    std::vector<ModuleID> ModuleDeps;
    std::vector<std::string> DriverCommandLine;
    std::vector<Command> Commands;
  };

  std::mutex Lock;
  std::unordered_map<IndexedModuleID, ModuleDeps, IndexedModuleID::Hasher>
      Modules;
  std::vector<InputDeps> Inputs;
};

static bool handleTranslationUnitResult(
    StringRef Input, llvm::Expected<TranslationUnitDeps> &MaybeTUDeps,
    FullDeps &FD, size_t InputIndex, SharedStream &OS, SharedStream &Errs) {
  if (!MaybeTUDeps) {
    llvm::handleAllErrors(
        MaybeTUDeps.takeError(), [&Input, &Errs](llvm::StringError &Err) {
          Errs.applyLocked([&](raw_ostream &OS) {
            OS << "Error while scanning dependencies for " << Input << ":\n";
            OS << Err.getMessage();
          });
        });
    return true;
  }
  FD.mergeDeps(Input, std::move(*MaybeTUDeps), InputIndex);
  return false;
}

static bool handleModuleResult(
    StringRef ModuleName, llvm::Expected<ModuleDepsGraph> &MaybeModuleGraph,
    FullDeps &FD, size_t InputIndex, SharedStream &OS, SharedStream &Errs) {
  if (!MaybeModuleGraph) {
    llvm::handleAllErrors(MaybeModuleGraph.takeError(),
                          [&ModuleName, &Errs](llvm::StringError &Err) {
                            Errs.applyLocked([&](raw_ostream &OS) {
                              OS << "Error while scanning dependencies for "
                                 << ModuleName << ":\n";
                              OS << Err.getMessage();
                            });
                          });
    return true;
  }
  FD.mergeDeps(std::move(*MaybeModuleGraph), InputIndex);
  return false;
}

class P1689Deps {
public:
  void printDependencies(raw_ostream &OS) {
    addSourcePathsToRequires();
    // Sort the modules by name to get a deterministic order.
    llvm::sort(Rules, [](const P1689Rule &A, const P1689Rule &B) {
      return A.PrimaryOutput < B.PrimaryOutput;
    });

    using namespace llvm::json;
    Array OutputRules;
    for (const P1689Rule &R : Rules) {
      Object O{{"primary-output", R.PrimaryOutput}};

      if (R.Provides) {
        Array Provides;
        Object Provided{{"logical-name", R.Provides->ModuleName},
                        {"source-path", R.Provides->SourcePath},
                        {"is-interface", R.Provides->IsStdCXXModuleInterface}};
        Provides.push_back(std::move(Provided));
        O.insert({"provides", std::move(Provides)});
      }

      Array Requires;
      for (const P1689ModuleInfo &Info : R.Requires) {
        Object RequiredInfo{{"logical-name", Info.ModuleName}};
        if (!Info.SourcePath.empty())
          RequiredInfo.insert({"source-path", Info.SourcePath});
        Requires.push_back(std::move(RequiredInfo));
      }

      if (!Requires.empty())
        O.insert({"requires", std::move(Requires)});

      OutputRules.push_back(std::move(O));
    }

    Object Output{
        {"version", 1}, {"revision", 0}, {"rules", std::move(OutputRules)}};

    OS << llvm::formatv("{0:2}\n", Value(std::move(Output)));
  }

  void addRules(P1689Rule &Rule) {
    std::unique_lock<std::mutex> LockGuard(Lock);
    Rules.push_back(Rule);
  }

private:
  void addSourcePathsToRequires() {
    llvm::DenseMap<StringRef, StringRef> ModuleSourceMapper;
    for (const P1689Rule &R : Rules)
      if (R.Provides && !R.Provides->SourcePath.empty())
        ModuleSourceMapper[R.Provides->ModuleName] = R.Provides->SourcePath;

    for (P1689Rule &R : Rules) {
      for (P1689ModuleInfo &Info : R.Requires) {
        auto Iter = ModuleSourceMapper.find(Info.ModuleName);
        if (Iter != ModuleSourceMapper.end())
          Info.SourcePath = Iter->second;
      }
    }
  }

  std::mutex Lock;
  std::vector<P1689Rule> Rules;
};

static bool
handleP1689DependencyToolResult(const std::string &Input,
                                llvm::Expected<P1689Rule> &MaybeRule,
                                P1689Deps &PD, SharedStream &Errs) {
  if (!MaybeRule) {
    llvm::handleAllErrors(
        MaybeRule.takeError(), [&Input, &Errs](llvm::StringError &Err) {
          Errs.applyLocked([&](raw_ostream &OS) {
            OS << "Error while scanning dependencies for " << Input << ":\n";
            OS << Err.getMessage();
          });
        });
    return true;
  }
  PD.addRules(*MaybeRule);
  return false;
}

/// Construct a path for the explicitly built PCM.
static std::string constructPCMPath(ModuleID MID, StringRef OutputDir) {
  SmallString<256> ExplicitPCMPath(OutputDir);
  llvm::sys::path::append(ExplicitPCMPath, MID.ContextHash,
                          MID.ModuleName + "-" + MID.ContextHash + ".pcm");
  return std::string(ExplicitPCMPath);
}

static std::string lookupModuleOutput(const ModuleID &MID, ModuleOutputKind MOK,
                                      StringRef OutputDir) {
  std::string PCMPath = constructPCMPath(MID, OutputDir);
  switch (MOK) {
  case ModuleOutputKind::ModuleFile:
    return PCMPath;
  case ModuleOutputKind::DependencyFile:
    return PCMPath + ".d";
  case ModuleOutputKind::DependencyTargets:
    // Null-separate the list of targets.
    return join(ModuleDepTargets, StringRef("\0", 1));
  case ModuleOutputKind::DiagnosticSerializationFile:
    return PCMPath + ".diag";
  }
  llvm_unreachable("Fully covered switch above!");
}

static std::string getModuleCachePath(ArrayRef<std::string> Args) {
  for (StringRef Arg : llvm::reverse(Args)) {
    Arg.consume_front("/clang:");
    if (Arg.consume_front("-fmodules-cache-path="))
      return std::string(Arg);
  }
  SmallString<128> Path;
  driver::Driver::getDefaultModuleCachePath(Path);
  return std::string(Path);
}

/// Attempts to construct the compilation database from '-compilation-database'
/// or from the arguments following the positional '--'.
static std::unique_ptr<tooling::CompilationDatabase>
getCompilationDatabase(int argc, char **argv, std::string &ErrorMessage) {
  ParseArgs(argc, argv);

  if (!(CommandLine.empty() ^ CompilationDB.empty())) {
    llvm::errs() << "The compilation command line must be provided either via "
                    "'-compilation-database' or after '--'.";
    return nullptr;
  }

  if (!CompilationDB.empty())
    return tooling::JSONCompilationDatabase::loadFromFile(
        CompilationDB, ErrorMessage,
        tooling::JSONCommandLineSyntax::AutoDetect);

  llvm::IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
      CompilerInstance::createDiagnostics(new DiagnosticOptions);
  driver::Driver TheDriver(CommandLine[0], llvm::sys::getDefaultTargetTriple(),
                           *Diags);
  TheDriver.setCheckInputsExist(false);
  std::unique_ptr<driver::Compilation> C(
      TheDriver.BuildCompilation(CommandLine));
  if (!C || C->getJobs().empty())
    return nullptr;

  auto Cmd = C->getJobs().begin();
  auto CI = std::make_unique<CompilerInvocation>();
  CompilerInvocation::CreateFromArgs(*CI, Cmd->getArguments(), *Diags,
                                     CommandLine[0]);
  if (!CI)
    return nullptr;

  FrontendOptions &FEOpts = CI->getFrontendOpts();
  if (FEOpts.Inputs.size() != 1) {
    llvm::errs()
        << "Exactly one input file is required in the per-file mode ('--').\n";
    return nullptr;
  }

  // There might be multiple jobs for a compilation. Extract the specified
  // output filename from the last job.
  auto LastCmd = C->getJobs().end();
  LastCmd--;
  if (LastCmd->getOutputFilenames().size() != 1) {
    llvm::errs()
        << "Exactly one output file is required in the per-file mode ('--').\n";
    return nullptr;
  }
  StringRef OutputFile = LastCmd->getOutputFilenames().front();

  class InplaceCompilationDatabase : public tooling::CompilationDatabase {
  public:
    InplaceCompilationDatabase(StringRef InputFile, StringRef OutputFile,
                               ArrayRef<const char *> CommandLine)
        : Command(".", InputFile, {}, OutputFile) {
      for (auto *C : CommandLine)
        Command.CommandLine.push_back(C);
    }

    std::vector<tooling::CompileCommand>
    getCompileCommands(StringRef FilePath) const override {
      if (FilePath != Command.Filename)
        return {};
      return {Command};
    }

    std::vector<std::string> getAllFiles() const override {
      return {Command.Filename};
    }

    std::vector<tooling::CompileCommand>
    getAllCompileCommands() const override {
      return {Command};
    }

  private:
    tooling::CompileCommand Command;
  };

  return std::make_unique<InplaceCompilationDatabase>(
      FEOpts.Inputs[0].getFile(), OutputFile, CommandLine);
}

int clang_scan_deps_main(int argc, char **argv, const llvm::ToolContext &) {
  llvm::InitializeAllTargetInfos();
  std::string ErrorMessage;
  std::unique_ptr<tooling::CompilationDatabase> Compilations =
      getCompilationDatabase(argc, argv, ErrorMessage);
  if (!Compilations) {
    llvm::errs() << ErrorMessage << "\n";
    return 1;
  }

  llvm::cl::PrintOptionValues();

  // Expand response files in advance, so that we can "see" all the arguments
  // when adjusting below.
  Compilations = expandResponseFiles(std::move(Compilations),
                                     llvm::vfs::getRealFileSystem());

  Compilations = inferTargetAndDriverMode(std::move(Compilations));

  Compilations = inferToolLocation(std::move(Compilations));

  // The command options are rewritten to run Clang in preprocessor only mode.
  auto AdjustingCompilations =
      std::make_unique<tooling::ArgumentsAdjustingCompilations>(
          std::move(Compilations));
  ResourceDirectoryCache ResourceDirCache;

  AdjustingCompilations->appendArgumentsAdjuster(
      [&ResourceDirCache](const tooling::CommandLineArguments &Args,
                          StringRef FileName) {
        std::string LastO;
        bool HasResourceDir = false;
        bool ClangCLMode = false;
        auto FlagsEnd = llvm::find(Args, "--");
        if (FlagsEnd != Args.begin()) {
          ClangCLMode =
              llvm::sys::path::stem(Args[0]).contains_insensitive("clang-cl") ||
              llvm::is_contained(Args, "--driver-mode=cl");

          // Reverse scan, starting at the end or at the element before "--".
          auto R = std::make_reverse_iterator(FlagsEnd);
          auto E = Args.rend();
          // Don't include Args[0] in the iteration; that's the executable, not
          // an option.
          if (E != R)
            E--;
          for (auto I = R; I != E; ++I) {
            StringRef Arg = *I;
            if (ClangCLMode) {
              // Ignore arguments that are preceded by "-Xclang".
              if ((I + 1) != E && I[1] == "-Xclang")
                continue;
              if (LastO.empty()) {
                // With clang-cl, the output obj file can be specified with
                // "/opath", "/o path", "/Fopath", and the dash counterparts.
                // Also, clang-cl adds ".obj" extension if none is found.
                if ((Arg == "-o" || Arg == "/o") && I != R)
                  LastO = I[-1]; // Next argument (reverse iterator)
                else if (Arg.starts_with("/Fo") || Arg.starts_with("-Fo"))
                  LastO = Arg.drop_front(3).str();
                else if (Arg.starts_with("/o") || Arg.starts_with("-o"))
                  LastO = Arg.drop_front(2).str();

                if (!LastO.empty() && !llvm::sys::path::has_extension(LastO))
                  LastO.append(".obj");
              }
            }
            if (Arg == "-resource-dir")
              HasResourceDir = true;
          }
        }
        tooling::CommandLineArguments AdjustedArgs(Args.begin(), FlagsEnd);
        // The clang-cl driver passes "-o -" to the frontend. Inject the real
        // file here to ensure "-MT" can be deduced if need be.
        if (ClangCLMode && !LastO.empty()) {
          AdjustedArgs.push_back("/clang:-o");
          AdjustedArgs.push_back("/clang:" + LastO);
        }

        if (!HasResourceDir && ResourceDirRecipe == RDRK_InvokeCompiler) {
          StringRef ResourceDir =
              ResourceDirCache.findResourceDir(Args, ClangCLMode);
          if (!ResourceDir.empty()) {
            AdjustedArgs.push_back("-resource-dir");
            AdjustedArgs.push_back(std::string(ResourceDir));
          }
        }
        AdjustedArgs.insert(AdjustedArgs.end(), FlagsEnd, Args.end());
        return AdjustedArgs;
      });

  SharedStream Errs(llvm::errs());

  std::optional<llvm::raw_fd_ostream> FileOS;
  llvm::raw_ostream &ThreadUnsafeDependencyOS = [&]() -> llvm::raw_ostream & {
    if (OutputFileName == "-")
      return llvm::outs();

    if (OutputFileName == "/dev/null")
      return llvm::nulls();

    std::error_code EC;
    FileOS.emplace(OutputFileName, EC);
    if (EC) {
      llvm::errs() << "Failed to open output file '" << OutputFileName
                   << "': " << llvm::errorCodeToError(EC) << '\n';
      std::exit(1);
    }
    return *FileOS;
  }();
  SharedStream DependencyOS(ThreadUnsafeDependencyOS);

  std::vector<tooling::CompileCommand> Inputs =
      AdjustingCompilations->getAllCompileCommands();

  std::atomic<bool> HadErrors(false);
  std::optional<FullDeps> FD;
  P1689Deps PD;

  std::mutex Lock;
  size_t Index = 0;
  auto GetNextInputIndex = [&]() -> std::optional<size_t> {
    std::unique_lock<std::mutex> LockGuard(Lock);
    if (Index < Inputs.size())
      return Index++;
    return {};
  };

  if (Format == ScanningOutputFormat::Full)
    FD.emplace(ModuleName.empty() ? Inputs.size() : 0);

  auto ScanningTask = [&](DependencyScanningService &Service) {
    DependencyScanningTool WorkerTool(Service);

    llvm::DenseSet<ModuleID> AlreadySeenModules;
    while (auto MaybeInputIndex = GetNextInputIndex()) {
      size_t LocalIndex = *MaybeInputIndex;
      const tooling::CompileCommand *Input = &Inputs[LocalIndex];
      std::string Filename = std::move(Input->Filename);
      std::string CWD = std::move(Input->Directory);

      std::optional<StringRef> MaybeModuleName;
      if (!ModuleName.empty())
        MaybeModuleName = ModuleName;

      std::string OutputDir(ModuleFilesDir);
      if (OutputDir.empty())
        OutputDir = getModuleCachePath(Input->CommandLine);
      auto LookupOutput = [&](const ModuleID &MID, ModuleOutputKind MOK) {
        return ::lookupModuleOutput(MID, MOK, OutputDir);
      };

      // Run the tool on it.
      if (Format == ScanningOutputFormat::Make) {
        auto MaybeFile = WorkerTool.getDependencyFile(Input->CommandLine, CWD);
        if (handleMakeDependencyToolResult(Filename, MaybeFile, DependencyOS,
                                           Errs))
          HadErrors = true;
      } else if (Format == ScanningOutputFormat::P1689) {
        // It is useful to generate the make-format dependency output during
        // the scanning for P1689. Otherwise the users need to scan again for
        // it. We will generate the make-format dependency output if we find
        // `-MF` in the command lines.
        std::string MakeformatOutputPath;
        std::string MakeformatOutput;

        auto MaybeRule = WorkerTool.getP1689ModuleDependencyFile(
            *Input, CWD, MakeformatOutput, MakeformatOutputPath);

        if (handleP1689DependencyToolResult(Filename, MaybeRule, PD, Errs))
          HadErrors = true;

        if (!MakeformatOutputPath.empty() && !MakeformatOutput.empty() &&
            !HadErrors) {
          static std::mutex Lock;
          // With compilation database, we may open different files
          // concurrently or we may write the same file concurrently. So we
          // use a map here to allow multiple compile commands to write to the
          // same file. Also we need a lock here to avoid data race.
          static llvm::StringMap<llvm::raw_fd_ostream> OSs;
          std::unique_lock<std::mutex> LockGuard(Lock);

          auto OSIter = OSs.find(MakeformatOutputPath);
          if (OSIter == OSs.end()) {
            std::error_code EC;
            OSIter =
                OSs.try_emplace(MakeformatOutputPath, MakeformatOutputPath, EC)
                    .first;
            if (EC)
              llvm::errs() << "Failed to open P1689 make format output file \""
                           << MakeformatOutputPath << "\" for " << EC.message()
                           << "\n";
          }

          SharedStream MakeformatOS(OSIter->second);
          llvm::Expected<std::string> MaybeOutput(MakeformatOutput);
          if (handleMakeDependencyToolResult(Filename, MaybeOutput,
                                             MakeformatOS, Errs))
            HadErrors = true;
        }
      } else if (MaybeModuleName) {
        auto MaybeModuleDepsGraph = WorkerTool.getModuleDependencies(
            *MaybeModuleName, Input->CommandLine, CWD, AlreadySeenModules,
            LookupOutput);
        if (handleModuleResult(*MaybeModuleName, MaybeModuleDepsGraph, *FD,
                               LocalIndex, DependencyOS, Errs))
          HadErrors = true;
      } else {
        auto MaybeTUDeps = WorkerTool.getTranslationUnitDependencies(
            Input->CommandLine, CWD, AlreadySeenModules, LookupOutput);
        if (handleTranslationUnitResult(Filename, MaybeTUDeps, *FD, LocalIndex,
                                        DependencyOS, Errs))
          HadErrors = true;
      }
    }
  };

  DependencyScanningService Service(ScanMode, Format, OptimizeArgs,
                                    EagerLoadModules);

  llvm::Timer T;
  T.startTimer();

  if (Inputs.size() == 1) {
    ScanningTask(Service);
  } else {
    llvm::DefaultThreadPool Pool(llvm::hardware_concurrency(NumThreads));

    if (Verbose) {
      llvm::outs() << "Running clang-scan-deps on " << Inputs.size()
                   << " files using " << Pool.getMaxConcurrency()
                   << " workers\n";
    }

    for (unsigned I = 0; I < Pool.getMaxConcurrency(); ++I)
      Pool.async([ScanningTask, &Service]() { ScanningTask(Service); });

    Pool.wait();
  }

  T.stopTimer();
  if (PrintTiming)
    llvm::errs() << llvm::format(
        "clang-scan-deps timing: %0.2fs wall, %0.2fs process\n",
        T.getTotalTime().getWallTime(), T.getTotalTime().getProcessTime());

  if (RoundTripArgs)
    if (FD && FD->roundTripCommands(llvm::errs()))
      HadErrors = true;

  if (Format == ScanningOutputFormat::Full)
    FD->printFullOutput(ThreadUnsafeDependencyOS);
  else if (Format == ScanningOutputFormat::P1689)
    PD.printDependencies(ThreadUnsafeDependencyOS);

  return HadErrors;
}
