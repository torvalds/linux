//===--- tools/clang-repl/ClangRepl.cpp - clang-repl - the Clang REPL -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a REPL tool on top of clang.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Interpreter/CodeCompletion.h"
#include "clang/Interpreter/Interpreter.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/LineEditor/LineEditor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h" // llvm_shutdown
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include <optional>

// Disable LSan for this test.
// FIXME: Re-enable once we can assume GCC 13.2 or higher.
// https://llvm.org/github.com/llvm/llvm-project/issues/67586.
#if LLVM_ADDRESS_SANITIZER_BUILD || LLVM_HWADDRESS_SANITIZER_BUILD
#include <sanitizer/lsan_interface.h>
LLVM_ATTRIBUTE_USED int __lsan_is_turned_off() { return 1; }
#endif

static llvm::cl::opt<bool> CudaEnabled("cuda", llvm::cl::Hidden);
static llvm::cl::opt<std::string> CudaPath("cuda-path", llvm::cl::Hidden);
static llvm::cl::opt<std::string> OffloadArch("offload-arch", llvm::cl::Hidden);

static llvm::cl::list<std::string>
    ClangArgs("Xcc",
              llvm::cl::desc("Argument to pass to the CompilerInvocation"),
              llvm::cl::CommaSeparated);
static llvm::cl::opt<bool> OptHostSupportsJit("host-supports-jit",
                                              llvm::cl::Hidden);
static llvm::cl::list<std::string> OptInputs(llvm::cl::Positional,
                                             llvm::cl::desc("[code to run]"));

static void LLVMErrorHandler(void *UserData, const char *Message,
                             bool GenCrashDiag) {
  auto &Diags = *static_cast<clang::DiagnosticsEngine *>(UserData);

  Diags.Report(clang::diag::err_fe_error_backend) << Message;

  // Run the interrupt handlers to make sure any special cleanups get done, in
  // particular that we remove files registered with RemoveFileOnSignal.
  llvm::sys::RunInterruptHandlers();

  // We cannot recover from llvm errors.  When reporting a fatal error, exit
  // with status 70 to generate crash diagnostics.  For BSD systems this is
  // defined as an internal software error. Otherwise, exit with status 1.

  exit(GenCrashDiag ? 70 : 1);
}

// If we are running with -verify a reported has to be returned as unsuccess.
// This is relevant especially for the test suite.
static int checkDiagErrors(const clang::CompilerInstance *CI, bool HasError) {
  unsigned Errs = CI->getDiagnostics().getClient()->getNumErrors();
  if (CI->getDiagnosticOpts().VerifyDiagnostics) {
    // If there was an error that came from the verifier we must return 1 as
    // an exit code for the process. This will make the test fail as expected.
    clang::DiagnosticConsumer *Client = CI->getDiagnostics().getClient();
    Client->EndSourceFile();
    Errs = Client->getNumErrors();

    // The interpreter expects BeginSourceFile/EndSourceFiles to be balanced.
    Client->BeginSourceFile(CI->getLangOpts(), &CI->getPreprocessor());
  }
  return (Errs || HasError) ? EXIT_FAILURE : EXIT_SUCCESS;
}

struct ReplListCompleter {
  clang::IncrementalCompilerBuilder &CB;
  clang::Interpreter &MainInterp;
  ReplListCompleter(clang::IncrementalCompilerBuilder &CB,
                    clang::Interpreter &Interp)
      : CB(CB), MainInterp(Interp){};

  std::vector<llvm::LineEditor::Completion> operator()(llvm::StringRef Buffer,
                                                       size_t Pos) const;
  std::vector<llvm::LineEditor::Completion>
  operator()(llvm::StringRef Buffer, size_t Pos, llvm::Error &ErrRes) const;
};

std::vector<llvm::LineEditor::Completion>
ReplListCompleter::operator()(llvm::StringRef Buffer, size_t Pos) const {
  auto Err = llvm::Error::success();
  auto res = (*this)(Buffer, Pos, Err);
  if (Err)
    llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
  return res;
}

std::vector<llvm::LineEditor::Completion>
ReplListCompleter::operator()(llvm::StringRef Buffer, size_t Pos,
                              llvm::Error &ErrRes) const {
  std::vector<llvm::LineEditor::Completion> Comps;
  std::vector<std::string> Results;

  auto CI = CB.CreateCpp();
  if (auto Err = CI.takeError()) {
    ErrRes = std::move(Err);
    return {};
  }

  size_t Lines =
      std::count(Buffer.begin(), std::next(Buffer.begin(), Pos), '\n') + 1;
  auto Interp = clang::Interpreter::create(std::move(*CI));

  if (auto Err = Interp.takeError()) {
    // log the error and returns an empty vector;
    ErrRes = std::move(Err);

    return {};
  }
  auto *MainCI = (*Interp)->getCompilerInstance();
  auto CC = clang::ReplCodeCompleter();
  CC.codeComplete(MainCI, Buffer, Lines, Pos + 1,
                  MainInterp.getCompilerInstance(), Results);
  for (auto c : Results) {
    if (c.find(CC.Prefix) == 0)
      Comps.push_back(
          llvm::LineEditor::Completion(c.substr(CC.Prefix.size()), c));
  }
  return Comps;
}

llvm::ExitOnError ExitOnErr;
int main(int argc, const char **argv) {
  ExitOnErr.setBanner("clang-repl: ");
  llvm::cl::ParseCommandLineOptions(argc, argv);

  llvm::llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  std::vector<const char *> ClangArgv(ClangArgs.size());
  std::transform(ClangArgs.begin(), ClangArgs.end(), ClangArgv.begin(),
                 [](const std::string &s) -> const char * { return s.data(); });
  // Initialize all targets (required for device offloading)
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  if (OptHostSupportsJit) {
    auto J = llvm::orc::LLJITBuilder().create();
    if (J)
      llvm::outs() << "true\n";
    else {
      llvm::consumeError(J.takeError());
      llvm::outs() << "false\n";
    }
    return 0;
  }

  clang::IncrementalCompilerBuilder CB;
  CB.SetCompilerArgs(ClangArgv);

  std::unique_ptr<clang::CompilerInstance> DeviceCI;
  if (CudaEnabled) {
    if (!CudaPath.empty())
      CB.SetCudaSDK(CudaPath);

    if (OffloadArch.empty()) {
      OffloadArch = "sm_35";
    }
    CB.SetOffloadArch(OffloadArch);

    DeviceCI = ExitOnErr(CB.CreateCudaDevice());
  }

  // FIXME: Investigate if we could use runToolOnCodeWithArgs from tooling. It
  // can replace the boilerplate code for creation of the compiler instance.
  std::unique_ptr<clang::CompilerInstance> CI;
  if (CudaEnabled) {
    CI = ExitOnErr(CB.CreateCudaHost());
  } else {
    CI = ExitOnErr(CB.CreateCpp());
  }

  // Set an error handler, so that any LLVM backend diagnostics go through our
  // error handler.
  llvm::install_fatal_error_handler(LLVMErrorHandler,
                                    static_cast<void *>(&CI->getDiagnostics()));

  // Load any requested plugins.
  CI->LoadRequestedPlugins();
  if (CudaEnabled)
    DeviceCI->LoadRequestedPlugins();

  std::unique_ptr<clang::Interpreter> Interp;

  if (CudaEnabled) {
    Interp = ExitOnErr(
        clang::Interpreter::createWithCUDA(std::move(CI), std::move(DeviceCI)));

    if (CudaPath.empty()) {
      ExitOnErr(Interp->LoadDynamicLibrary("libcudart.so"));
    } else {
      auto CudaRuntimeLibPath = CudaPath + "/lib/libcudart.so";
      ExitOnErr(Interp->LoadDynamicLibrary(CudaRuntimeLibPath.c_str()));
    }
  } else
    Interp = ExitOnErr(clang::Interpreter::create(std::move(CI)));

  bool HasError = false;

  for (const std::string &input : OptInputs) {
    if (auto Err = Interp->ParseAndExecute(input)) {
      llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      HasError = true;
    }
  }

  if (OptInputs.empty()) {
    llvm::LineEditor LE("clang-repl");
    std::string Input;
    LE.setListCompleter(ReplListCompleter(CB, *Interp));
    while (std::optional<std::string> Line = LE.readLine()) {
      llvm::StringRef L = *Line;
      L = L.trim();
      if (L.ends_with("\\")) {
        // FIXME: Support #ifdef X \ ...
        Input += L.drop_back(1);
        LE.setPrompt("clang-repl...   ");
        continue;
      }

      Input += L;
      if (Input == R"(%quit)") {
        break;
      }
      if (Input == R"(%undo)") {
        if (auto Err = Interp->Undo())
          llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      } else if (Input.rfind("%lib ", 0) == 0) {
        if (auto Err = Interp->LoadDynamicLibrary(Input.data() + 5))
          llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      } else if (auto Err = Interp->ParseAndExecute(Input)) {
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      }

      Input = "";
      LE.setPrompt("clang-repl> ");
    }
  }

  // Our error handler depends on the Diagnostics object, which we're
  // potentially about to delete. Uninstall the handler now so that any
  // later errors use the default handling behavior instead.
  llvm::remove_fatal_error_handler();

  return checkDiagErrors(Interp->getCompilerInstance(), HasError);
}
