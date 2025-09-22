//===- llvm-profgen.cpp - LLVM SPGO profile generation tool -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-profgen generates SPGO profiles from perf script ouput.
//
//===----------------------------------------------------------------------===//

#include "ErrorHandling.h"
#include "PerfReader.h"
#include "ProfileGenerator.h"
#include "ProfiledBinary.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"

static cl::OptionCategory ProfGenCategory("ProfGen Options");

static cl::opt<std::string> PerfScriptFilename(
    "perfscript", cl::value_desc("perfscript"),
    cl::desc("Path of perf-script trace created by Linux perf tool with "
             "`script` command(the raw perf.data should be profiled with -b)"),
    cl::cat(ProfGenCategory));
static cl::alias PSA("ps", cl::desc("Alias for --perfscript"),
                     cl::aliasopt(PerfScriptFilename));

static cl::opt<std::string> PerfDataFilename(
    "perfdata", cl::value_desc("perfdata"),
    cl::desc("Path of raw perf data created by Linux perf tool (it should be "
             "profiled with -b)"),
    cl::cat(ProfGenCategory));
static cl::alias PDA("pd", cl::desc("Alias for --perfdata"),
                     cl::aliasopt(PerfDataFilename));

static cl::opt<std::string> UnsymbolizedProfFilename(
    "unsymbolized-profile", cl::value_desc("unsymbolized profile"),
    cl::desc("Path of the unsymbolized profile created by "
             "`llvm-profgen` with `--skip-symbolization`"),
    cl::cat(ProfGenCategory));
static cl::alias UPA("up", cl::desc("Alias for --unsymbolized-profile"),
                     cl::aliasopt(UnsymbolizedProfFilename));

static cl::opt<std::string> SampleProfFilename(
    "llvm-sample-profile", cl::value_desc("llvm sample profile"),
    cl::desc("Path of the LLVM sample profile"), cl::cat(ProfGenCategory));

static cl::opt<std::string>
    BinaryPath("binary", cl::value_desc("binary"), cl::Required,
               cl::desc("Path of profiled executable binary."),
               cl::cat(ProfGenCategory));

static cl::opt<uint32_t>
    ProcessId("pid", cl::value_desc("process Id"), cl::init(0),
              cl::desc("Process Id for the profiled executable binary."),
              cl::cat(ProfGenCategory));

static cl::opt<std::string> DebugBinPath(
    "debug-binary", cl::value_desc("debug-binary"),
    cl::desc("Path of debug info binary, llvm-profgen will load the DWARF info "
             "from it instead of the executable binary."),
    cl::cat(ProfGenCategory));

extern cl::opt<bool> ShowDisassemblyOnly;
extern cl::opt<bool> ShowSourceLocations;
extern cl::opt<bool> SkipSymbolization;

using namespace llvm;
using namespace sampleprof;

// Validate the command line input.
static void validateCommandLine() {
  // Allow the missing perfscript if we only use to show binary disassembly.
  if (!ShowDisassemblyOnly) {
    // Validate input profile is provided only once
    bool HasPerfData = PerfDataFilename.getNumOccurrences() > 0;
    bool HasPerfScript = PerfScriptFilename.getNumOccurrences() > 0;
    bool HasUnsymbolizedProfile =
        UnsymbolizedProfFilename.getNumOccurrences() > 0;
    bool HasSampleProfile = SampleProfFilename.getNumOccurrences() > 0;
    uint16_t S =
        HasPerfData + HasPerfScript + HasUnsymbolizedProfile + HasSampleProfile;
    if (S != 1) {
      std::string Msg =
          S > 1
              ? "`--perfscript`, `--perfdata` and `--unsymbolized-profile` "
                "cannot be used together."
              : "Perf input file is missing, please use one of `--perfscript`, "
                "`--perfdata` and `--unsymbolized-profile` for the input.";
      exitWithError(Msg);
    }

    auto CheckFileExists = [](bool H, StringRef File) {
      if (H && !llvm::sys::fs::exists(File)) {
        std::string Msg = "Input perf file(" + File.str() + ") doesn't exist.";
        exitWithError(Msg);
      }
    };

    CheckFileExists(HasPerfData, PerfDataFilename);
    CheckFileExists(HasPerfScript, PerfScriptFilename);
    CheckFileExists(HasUnsymbolizedProfile, UnsymbolizedProfFilename);
    CheckFileExists(HasSampleProfile, SampleProfFilename);
  }

  if (!llvm::sys::fs::exists(BinaryPath)) {
    std::string Msg = "Input binary(" + BinaryPath + ") doesn't exist.";
    exitWithError(Msg);
  }

  if (CSProfileGenerator::MaxCompressionSize < -1) {
    exitWithError("Value of --compress-recursion should >= -1");
  }
  if (ShowSourceLocations && !ShowDisassemblyOnly) {
    exitWithError("--show-source-locations should work together with "
                  "--show-disassembly-only!");
  }
}

static PerfInputFile getPerfInputFile() {
  PerfInputFile File;
  if (PerfDataFilename.getNumOccurrences()) {
    File.InputFile = PerfDataFilename;
    File.Format = PerfFormat::PerfData;
  } else if (PerfScriptFilename.getNumOccurrences()) {
    File.InputFile = PerfScriptFilename;
    File.Format = PerfFormat::PerfScript;
  } else if (UnsymbolizedProfFilename.getNumOccurrences()) {
    File.InputFile = UnsymbolizedProfFilename;
    File.Format = PerfFormat::UnsymbolizedProfile;
  }
  return File;
}

int main(int argc, const char *argv[]) {
  InitLLVM X(argc, argv);

  // Initialize targets and assembly printers/parsers.
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllDisassemblers();

  cl::HideUnrelatedOptions({&ProfGenCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "llvm SPGO profile generator\n");
  validateCommandLine();

  // Load symbols and disassemble the code of a given binary.
  std::unique_ptr<ProfiledBinary> Binary =
      std::make_unique<ProfiledBinary>(BinaryPath, DebugBinPath);
  if (ShowDisassemblyOnly)
    return EXIT_SUCCESS;

  if (SampleProfFilename.getNumOccurrences()) {
    LLVMContext Context;
    auto FS = vfs::getRealFileSystem();
    auto ReaderOrErr =
        SampleProfileReader::create(SampleProfFilename, Context, *FS);
    std::unique_ptr<sampleprof::SampleProfileReader> Reader =
        std::move(ReaderOrErr.get());
    Reader->read();
    std::unique_ptr<ProfileGeneratorBase> Generator =
        ProfileGeneratorBase::create(Binary.get(), Reader->getProfiles(),
                                     Reader->profileIsCS());
    Generator->generateProfile();
    Generator->write();
  } else {
    std::optional<uint32_t> PIDFilter;
    if (ProcessId.getNumOccurrences())
      PIDFilter = ProcessId;
    PerfInputFile PerfFile = getPerfInputFile();
    std::unique_ptr<PerfReaderBase> Reader =
        PerfReaderBase::create(Binary.get(), PerfFile, PIDFilter);
    // Parse perf events and samples
    Reader->parsePerfTraces();

    if (SkipSymbolization)
      return EXIT_SUCCESS;

    std::unique_ptr<ProfileGeneratorBase> Generator =
        ProfileGeneratorBase::create(Binary.get(), &Reader->getSampleCounters(),
                                     Reader->profileIsCS());
    Generator->generateProfile();
    Generator->write();
  }

  return EXIT_SUCCESS;
}
