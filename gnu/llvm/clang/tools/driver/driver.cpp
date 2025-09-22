//===-- driver.cpp - Clang GCC-Compatible Driver --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the clang driver; it is a thin wrapper
// for functionality in the Driver clang library.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Driver.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/HeaderInclude.h"
#include "clang/Basic/Stack.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Frontend/ChainedDiagnosticConsumer.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/SerializedDiagnosticPrinter.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <memory>
#include <optional>
#include <set>
#include <system_error>
using namespace clang;
using namespace clang::driver;
using namespace llvm::opt;

std::string GetExecutablePath(const char *Argv0, bool CanonicalPrefixes) {
  if (!CanonicalPrefixes) {
    SmallString<128> ExecutablePath(Argv0);
    // Do a PATH lookup if Argv0 isn't a valid path.
    if (!llvm::sys::fs::exists(ExecutablePath))
      if (llvm::ErrorOr<std::string> P =
              llvm::sys::findProgramByName(ExecutablePath))
        ExecutablePath = *P;
    return std::string(ExecutablePath);
  }

  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *P = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, P);
}

static const char *GetStableCStr(llvm::StringSet<> &SavedStrings, StringRef S) {
  return SavedStrings.insert(S).first->getKeyData();
}

extern int cc1_main(ArrayRef<const char *> Argv, const char *Argv0,
                    void *MainAddr);
extern int cc1as_main(ArrayRef<const char *> Argv, const char *Argv0,
                      void *MainAddr);
extern int cc1gen_reproducer_main(ArrayRef<const char *> Argv,
                                  const char *Argv0, void *MainAddr,
                                  const llvm::ToolContext &);

static void insertTargetAndModeArgs(const ParsedClangName &NameParts,
                                    SmallVectorImpl<const char *> &ArgVector,
                                    llvm::StringSet<> &SavedStrings) {
  // Put target and mode arguments at the start of argument list so that
  // arguments specified in command line could override them. Avoid putting
  // them at index 0, as an option like '-cc1' must remain the first.
  int InsertionPoint = 0;
  if (ArgVector.size() > 0)
    ++InsertionPoint;

  if (NameParts.DriverMode) {
    // Add the mode flag to the arguments.
    ArgVector.insert(ArgVector.begin() + InsertionPoint,
                     GetStableCStr(SavedStrings, NameParts.DriverMode));
  }

  if (NameParts.TargetIsValid) {
    const char *arr[] = {"-target", GetStableCStr(SavedStrings,
                                                  NameParts.TargetPrefix)};
    ArgVector.insert(ArgVector.begin() + InsertionPoint,
                     std::begin(arr), std::end(arr));
  }
}

static void getCLEnvVarOptions(std::string &EnvValue, llvm::StringSaver &Saver,
                               SmallVectorImpl<const char *> &Opts) {
  llvm::cl::TokenizeWindowsCommandLine(EnvValue, Saver, Opts);
  // The first instance of '#' should be replaced with '=' in each option.
  for (const char *Opt : Opts)
    if (char *NumberSignPtr = const_cast<char *>(::strchr(Opt, '#')))
      *NumberSignPtr = '=';
}

template <class T>
static T checkEnvVar(const char *EnvOptSet, const char *EnvOptFile,
                     std::string &OptFile) {
  const char *Str = ::getenv(EnvOptSet);
  if (!Str)
    return T{};

  T OptVal = Str;
  if (const char *Var = ::getenv(EnvOptFile))
    OptFile = Var;
  return OptVal;
}

static bool SetBackdoorDriverOutputsFromEnvVars(Driver &TheDriver) {
  TheDriver.CCPrintOptions =
      checkEnvVar<bool>("CC_PRINT_OPTIONS", "CC_PRINT_OPTIONS_FILE",
                        TheDriver.CCPrintOptionsFilename);
  if (checkEnvVar<bool>("CC_PRINT_HEADERS", "CC_PRINT_HEADERS_FILE",
                        TheDriver.CCPrintHeadersFilename)) {
    TheDriver.CCPrintHeadersFormat = HIFMT_Textual;
    TheDriver.CCPrintHeadersFiltering = HIFIL_None;
  } else {
    std::string EnvVar = checkEnvVar<std::string>(
        "CC_PRINT_HEADERS_FORMAT", "CC_PRINT_HEADERS_FILE",
        TheDriver.CCPrintHeadersFilename);
    if (!EnvVar.empty()) {
      TheDriver.CCPrintHeadersFormat =
          stringToHeaderIncludeFormatKind(EnvVar.c_str());
      if (!TheDriver.CCPrintHeadersFormat) {
        TheDriver.Diag(clang::diag::err_drv_print_header_env_var)
            << 0 << EnvVar;
        return false;
      }

      const char *FilteringStr = ::getenv("CC_PRINT_HEADERS_FILTERING");
      HeaderIncludeFilteringKind Filtering;
      if (!stringToHeaderIncludeFiltering(FilteringStr, Filtering)) {
        TheDriver.Diag(clang::diag::err_drv_print_header_env_var)
            << 1 << FilteringStr;
        return false;
      }

      if ((TheDriver.CCPrintHeadersFormat == HIFMT_Textual &&
           Filtering != HIFIL_None) ||
          (TheDriver.CCPrintHeadersFormat == HIFMT_JSON &&
           Filtering != HIFIL_Only_Direct_System)) {
        TheDriver.Diag(clang::diag::err_drv_print_header_env_var_combination)
            << EnvVar << FilteringStr;
        return false;
      }
      TheDriver.CCPrintHeadersFiltering = Filtering;
    }
  }

  TheDriver.CCLogDiagnostics =
      checkEnvVar<bool>("CC_LOG_DIAGNOSTICS", "CC_LOG_DIAGNOSTICS_FILE",
                        TheDriver.CCLogDiagnosticsFilename);
  TheDriver.CCPrintProcessStats =
      checkEnvVar<bool>("CC_PRINT_PROC_STAT", "CC_PRINT_PROC_STAT_FILE",
                        TheDriver.CCPrintStatReportFilename);
  TheDriver.CCPrintInternalStats =
      checkEnvVar<bool>("CC_PRINT_INTERNAL_STAT", "CC_PRINT_INTERNAL_STAT_FILE",
                        TheDriver.CCPrintInternalStatReportFilename);

  return true;
}

static void FixupDiagPrefixExeName(TextDiagnosticPrinter *DiagClient,
                                   const std::string &Path) {
  // If the clang binary happens to be named cl.exe for compatibility reasons,
  // use clang-cl.exe as the prefix to avoid confusion between clang and MSVC.
  StringRef ExeBasename(llvm::sys::path::stem(Path));
  if (ExeBasename.equals_insensitive("cl"))
    ExeBasename = "clang-cl";
  DiagClient->setPrefix(std::string(ExeBasename));
}

static int ExecuteCC1Tool(SmallVectorImpl<const char *> &ArgV,
                          const llvm::ToolContext &ToolContext) {
  // If we call the cc1 tool from the clangDriver library (through
  // Driver::CC1Main), we need to clean up the options usage count. The options
  // are currently global, and they might have been used previously by the
  // driver.
  llvm::cl::ResetAllOptionOccurrences();

  llvm::BumpPtrAllocator A;
  llvm::cl::ExpansionContext ECtx(A, llvm::cl::TokenizeGNUCommandLine);
  if (llvm::Error Err = ECtx.expandResponseFiles(ArgV)) {
    llvm::errs() << toString(std::move(Err)) << '\n';
    return 1;
  }
  StringRef Tool = ArgV[1];
  void *GetExecutablePathVP = (void *)(intptr_t)GetExecutablePath;
  if (Tool == "-cc1")
    return cc1_main(ArrayRef(ArgV).slice(1), ArgV[0], GetExecutablePathVP);
  if (Tool == "-cc1as")
    return cc1as_main(ArrayRef(ArgV).slice(2), ArgV[0], GetExecutablePathVP);
  if (Tool == "-cc1gen-reproducer")
    return cc1gen_reproducer_main(ArrayRef(ArgV).slice(2), ArgV[0],
                                  GetExecutablePathVP, ToolContext);
  // Reject unknown tools.
  llvm::errs()
      << "error: unknown integrated tool '" << Tool << "'. "
      << "Valid tools include '-cc1', '-cc1as' and '-cc1gen-reproducer'.\n";
  return 1;
}

int clang_main(int Argc, char **Argv, const llvm::ToolContext &ToolContext) {
  noteBottomOfStack();
  llvm::setBugReportMsg("PLEASE submit a bug report to " BUG_REPORT_URL
                        " and include the crash backtrace, preprocessed "
                        "source, and associated run script.\n");
  SmallVector<const char *, 256> Args(Argv, Argv + Argc);

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  llvm::InitializeAllTargets();

  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver(A);

  const char *ProgName =
      ToolContext.NeedsPrependArg ? ToolContext.PrependArg : ToolContext.Path;

  bool ClangCLMode =
      IsClangCL(getDriverMode(ProgName, llvm::ArrayRef(Args).slice(1)));

  if (llvm::Error Err = expandResponseFiles(Args, ClangCLMode, A)) {
    llvm::errs() << toString(std::move(Err)) << '\n';
    return 1;
  }

  // Handle -cc1 integrated tools.
  if (Args.size() >= 2 && StringRef(Args[1]).starts_with("-cc1"))
    return ExecuteCC1Tool(Args, ToolContext);

  // Handle options that need handling before the real command line parsing in
  // Driver::BuildCompilation()
  bool CanonicalPrefixes = true;
  for (int i = 1, size = Args.size(); i < size; ++i) {
    // Skip end-of-line response file markers
    if (Args[i] == nullptr)
      continue;
    if (StringRef(Args[i]) == "-canonical-prefixes")
      CanonicalPrefixes = true;
    else if (StringRef(Args[i]) == "-no-canonical-prefixes")
      CanonicalPrefixes = false;
  }

  // Handle CL and _CL_ which permits additional command line options to be
  // prepended or appended.
  if (ClangCLMode) {
    // Arguments in "CL" are prepended.
    std::optional<std::string> OptCL = llvm::sys::Process::GetEnv("CL");
    if (OptCL) {
      SmallVector<const char *, 8> PrependedOpts;
      getCLEnvVarOptions(*OptCL, Saver, PrependedOpts);

      // Insert right after the program name to prepend to the argument list.
      Args.insert(Args.begin() + 1, PrependedOpts.begin(), PrependedOpts.end());
    }
    // Arguments in "_CL_" are appended.
    std::optional<std::string> Opt_CL_ = llvm::sys::Process::GetEnv("_CL_");
    if (Opt_CL_) {
      SmallVector<const char *, 8> AppendedOpts;
      getCLEnvVarOptions(*Opt_CL_, Saver, AppendedOpts);

      // Insert at the end of the argument list to append.
      Args.append(AppendedOpts.begin(), AppendedOpts.end());
    }
  }

  llvm::StringSet<> SavedStrings;
  // Handle CCC_OVERRIDE_OPTIONS, used for editing a command line behind the
  // scenes.
  if (const char *OverrideStr = ::getenv("CCC_OVERRIDE_OPTIONS")) {
    // FIXME: Driver shouldn't take extra initial argument.
    driver::applyOverrideOptions(Args, OverrideStr, SavedStrings,
                                 &llvm::errs());
  }

  std::string Path = GetExecutablePath(ToolContext.Path, CanonicalPrefixes);

  // Whether the cc1 tool should be called inside the current process, or if we
  // should spawn a new clang subprocess (old behavior).
  // Not having an additional process saves some execution time of Windows,
  // and makes debugging and profiling easier.
  bool UseNewCC1Process = CLANG_SPAWN_CC1;
  for (const char *Arg : Args)
    UseNewCC1Process = llvm::StringSwitch<bool>(Arg)
                           .Case("-fno-integrated-cc1", true)
                           .Case("-fintegrated-cc1", false)
                           .Default(UseNewCC1Process);

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
      CreateAndPopulateDiagOpts(Args);

  TextDiagnosticPrinter *DiagClient
    = new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  FixupDiagPrefixExeName(DiagClient, ProgName);

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());

  DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagClient);

  if (!DiagOpts->DiagnosticSerializationFile.empty()) {
    auto SerializedConsumer =
        clang::serialized_diags::create(DiagOpts->DiagnosticSerializationFile,
                                        &*DiagOpts, /*MergeChildRecords=*/true);
    Diags.setClient(new ChainedDiagnosticConsumer(
        Diags.takeClient(), std::move(SerializedConsumer)));
  }

  ProcessWarningOptions(Diags, *DiagOpts, /*ReportDiags=*/false);

  Driver TheDriver(Path, llvm::sys::getDefaultTargetTriple(), Diags);
  auto TargetAndMode = ToolChain::getTargetAndModeFromProgramName(ProgName);
  TheDriver.setTargetAndMode(TargetAndMode);
  // If -canonical-prefixes is set, GetExecutablePath will have resolved Path
  // to the llvm driver binary, not clang. In this case, we need to use
  // PrependArg which should be clang-*. Checking just CanonicalPrefixes is
  // safe even in the normal case because PrependArg will be null so
  // setPrependArg will be a no-op.
  if (ToolContext.NeedsPrependArg || CanonicalPrefixes)
    TheDriver.setPrependArg(ToolContext.PrependArg);

  insertTargetAndModeArgs(TargetAndMode, Args, SavedStrings);

  if (!SetBackdoorDriverOutputsFromEnvVars(TheDriver))
    return 1;

  if (!UseNewCC1Process) {
    TheDriver.CC1Main = [ToolContext](SmallVectorImpl<const char *> &ArgV) {
      return ExecuteCC1Tool(ArgV, ToolContext);
    };
    // Ensure the CC1Command actually catches cc1 crashes
    llvm::CrashRecoveryContext::Enable();
  }

  std::unique_ptr<Compilation> C(TheDriver.BuildCompilation(Args));

  Driver::ReproLevel ReproLevel = Driver::ReproLevel::OnCrash;
  if (Arg *A = C->getArgs().getLastArg(options::OPT_gen_reproducer_eq)) {
    auto Level =
        llvm::StringSwitch<std::optional<Driver::ReproLevel>>(A->getValue())
            .Case("off", Driver::ReproLevel::Off)
            .Case("crash", Driver::ReproLevel::OnCrash)
            .Case("error", Driver::ReproLevel::OnError)
            .Case("always", Driver::ReproLevel::Always)
            .Default(std::nullopt);
    if (!Level) {
      llvm::errs() << "Unknown value for " << A->getSpelling() << ": '"
                   << A->getValue() << "'\n";
      return 1;
    }
    ReproLevel = *Level;
  }
  if (!!::getenv("FORCE_CLANG_DIAGNOSTICS_CRASH"))
    ReproLevel = Driver::ReproLevel::Always;

  int Res = 1;
  bool IsCrash = false;
  Driver::CommandStatus CommandStatus = Driver::CommandStatus::Ok;
  // Pretend the first command failed if ReproStatus is Always.
  const Command *FailingCommand = nullptr;
  if (!C->getJobs().empty())
    FailingCommand = &*C->getJobs().begin();
  if (C && !C->containsError()) {
    SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
    Res = TheDriver.ExecuteCompilation(*C, FailingCommands);

    for (const auto &P : FailingCommands) {
      int CommandRes = P.first;
      FailingCommand = P.second;
      if (!Res)
        Res = CommandRes;

      // If result status is < 0, then the driver command signalled an error.
      // If result status is 70, then the driver command reported a fatal error.
      // On Windows, abort will return an exit code of 3.  In these cases,
      // generate additional diagnostic information if possible.
      IsCrash = CommandRes < 0 || CommandRes == 70;
#ifdef _WIN32
      IsCrash |= CommandRes == 3;
#endif
#if LLVM_ON_UNIX
      // When running in integrated-cc1 mode, the CrashRecoveryContext returns
      // the same codes as if the program crashed. See section "Exit Status for
      // Commands":
      // https://pubs.opengroup.org/onlinepubs/9699919799/xrat/V4_xcu_chap02.html
      IsCrash |= CommandRes > 128;
#endif
      CommandStatus =
          IsCrash ? Driver::CommandStatus::Crash : Driver::CommandStatus::Error;
      if (IsCrash)
        break;
    }
  }

  // Print the bug report message that would be printed if we did actually
  // crash, but only if we're crashing due to FORCE_CLANG_DIAGNOSTICS_CRASH.
  if (::getenv("FORCE_CLANG_DIAGNOSTICS_CRASH"))
    llvm::dbgs() << llvm::getBugReportMsg();
  if (FailingCommand != nullptr &&
    TheDriver.maybeGenerateCompilationDiagnostics(CommandStatus, ReproLevel,
                                                  *C, *FailingCommand))
    Res = 1;

  Diags.getClient()->finish();

  if (!UseNewCC1Process && IsCrash) {
    // When crashing in -fintegrated-cc1 mode, bury the timer pointers, because
    // the internal linked list might point to already released stack frames.
    llvm::BuryPointer(llvm::TimerGroup::aquireDefaultGroup());
  } else {
    // If any timers were active but haven't been destroyed yet, print their
    // results now.  This happens in -disable-free mode.
    llvm::TimerGroup::printAll(llvm::errs());
    llvm::TimerGroup::clearAll();
  }

#ifdef _WIN32
  // Exit status should not be negative on Win32, unless abnormal termination.
  // Once abnormal termination was caught, negative status should not be
  // propagated.
  if (Res < 0)
    Res = 1;
#endif

  // If we have multiple failing commands, we return the result of the first
  // failing command.
  return Res;
}
