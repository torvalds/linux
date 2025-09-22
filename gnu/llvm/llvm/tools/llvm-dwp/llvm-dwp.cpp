//===-- llvm-dwp.cpp - Split DWARF merging tool for llvm ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A utility for merging DWARF 5 Split DWARF .dwo files into .dwp (DWARF
// package files).
//
//===----------------------------------------------------------------------===//
#include "llvm/DWP/DWP.h"
#include "llvm/DWP/DWPError.h"
#include "llvm/DWP/DWPStringPool.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include <optional>

using namespace llvm;
using namespace llvm::object;

static mc::RegisterMCTargetOptionsFlags MCTargetOptionsFlags;

// Command-line option boilerplate.
namespace {
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

using namespace llvm::opt;
static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class DwpOptTable : public opt::GenericOptTable {
public:
  DwpOptTable() : GenericOptTable(InfoTable) {}
};
} // end anonymous namespace

// Options
static std::vector<std::string> ExecFilenames;
static std::string OutputFilename;
static std::string ContinueOption;

static Expected<SmallVector<std::string, 16>>
getDWOFilenames(StringRef ExecFilename) {
  auto ErrOrObj = object::ObjectFile::createObjectFile(ExecFilename);
  if (!ErrOrObj)
    return ErrOrObj.takeError();

  const ObjectFile &Obj = *ErrOrObj.get().getBinary();
  std::unique_ptr<DWARFContext> DWARFCtx = DWARFContext::create(Obj);

  SmallVector<std::string, 16> DWOPaths;
  for (const auto &CU : DWARFCtx->compile_units()) {
    const DWARFDie &Die = CU->getUnitDIE();
    std::string DWOName = dwarf::toString(
        Die.find({dwarf::DW_AT_dwo_name, dwarf::DW_AT_GNU_dwo_name}), "");
    if (DWOName.empty())
      continue;
    std::string DWOCompDir =
        dwarf::toString(Die.find(dwarf::DW_AT_comp_dir), "");
    if (!DWOCompDir.empty()) {
      SmallString<16> DWOPath(DWOName);
      sys::fs::make_absolute(DWOCompDir, DWOPath);
      if (!sys::fs::exists(DWOPath) && sys::fs::exists(DWOName))
        DWOPaths.push_back(std::move(DWOName));
      else
        DWOPaths.emplace_back(DWOPath.data(), DWOPath.size());
    } else {
      DWOPaths.push_back(std::move(DWOName));
    }
  }
  return std::move(DWOPaths);
}

static int error(const Twine &Error, const Twine &Context) {
  errs() << Twine("while processing ") + Context + ":\n";
  errs() << Twine("error: ") + Error + "\n";
  return 1;
}

static Expected<Triple> readTargetTriple(StringRef FileName) {
  auto ErrOrObj = object::ObjectFile::createObjectFile(FileName);
  if (!ErrOrObj)
    return ErrOrObj.takeError();

  return ErrOrObj->getBinary()->makeTriple();
}

int llvm_dwp_main(int argc, char **argv, const llvm::ToolContext &) {
  DwpOptTable Tbl;
  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver{A};
  OnCuIndexOverflow OverflowOptValue = OnCuIndexOverflow::HardStop;
  opt::InputArgList Args =
      Tbl.parseArgs(argc, argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) {
        llvm::errs() << Msg << '\n';
        std::exit(1);
      });

  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(llvm::outs(), "llvm-dwp [options] <input files>",
                  "merge split dwarf (.dwo) files");
    std::exit(0);
  }

  if (Args.hasArg(OPT_version)) {
    llvm::cl::PrintVersionMessage();
    std::exit(0);
  }

  OutputFilename = Args.getLastArgValue(OPT_outputFileName, "");
  if (Arg *Arg = Args.getLastArg(OPT_continueOnCuIndexOverflow,
                                 OPT_continueOnCuIndexOverflow_EQ)) {
    if (Arg->getOption().matches(OPT_continueOnCuIndexOverflow)) {
      OverflowOptValue = OnCuIndexOverflow::Continue;
    } else {
      ContinueOption = Arg->getValue();
      if (ContinueOption == "soft-stop") {
        OverflowOptValue = OnCuIndexOverflow::SoftStop;
      } else if (ContinueOption == "continue") {
        OverflowOptValue = OnCuIndexOverflow::Continue;
      } else {
        llvm::errs() << "invalid value for --continue-on-cu-index-overflow"
                     << ContinueOption << '\n';
        exit(1);
      }
    }
  }

  for (const llvm::opt::Arg *A : Args.filtered(OPT_execFileNames))
    ExecFilenames.emplace_back(A->getValue());

  std::vector<std::string> DWOFilenames;
  for (const llvm::opt::Arg *A : Args.filtered(OPT_INPUT))
    DWOFilenames.emplace_back(A->getValue());

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllTargets();
  llvm::InitializeAllAsmPrinters();

  for (const auto &ExecFilename : ExecFilenames) {
    auto DWOs = getDWOFilenames(ExecFilename);
    if (!DWOs) {
      logAllUnhandledErrors(
          handleErrors(DWOs.takeError(),
                       [&](std::unique_ptr<ECError> EC) -> Error {
                         return createFileError(ExecFilename,
                                                Error(std::move(EC)));
                       }),
          WithColor::error());
      return 1;
    }
    DWOFilenames.insert(DWOFilenames.end(),
                        std::make_move_iterator(DWOs->begin()),
                        std::make_move_iterator(DWOs->end()));
  }

  if (DWOFilenames.empty()) {
    WithColor::defaultWarningHandler(make_error<DWPError>(
        "executable file does not contain any references to dwo files"));
    return 0;
  }

  std::string ErrorStr;
  StringRef Context = "dwarf streamer init";

  auto ErrOrTriple = readTargetTriple(DWOFilenames.front());
  if (!ErrOrTriple) {
    logAllUnhandledErrors(
        handleErrors(ErrOrTriple.takeError(),
                     [&](std::unique_ptr<ECError> EC) -> Error {
                       return createFileError(DWOFilenames.front(),
                                              Error(std::move(EC)));
                     }),
        WithColor::error());
    return 1;
  }

  // Get the target.
  const Target *TheTarget =
      TargetRegistry::lookupTarget("", *ErrOrTriple, ErrorStr);
  if (!TheTarget)
    return error(ErrorStr, Context);
  std::string TripleName = ErrOrTriple->getTriple();

  // Create all the MC Objects.
  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  if (!MRI)
    return error(Twine("no register info for target ") + TripleName, Context);

  MCTargetOptions MCOptions = llvm::mc::InitMCTargetOptionsFromFlags();
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  if (!MAI)
    return error("no asm info for target " + TripleName, Context);

  std::unique_ptr<MCSubtargetInfo> MSTI(
      TheTarget->createMCSubtargetInfo(TripleName, "", ""));
  if (!MSTI)
    return error("no subtarget info for target " + TripleName, Context);

  MCContext MC(*ErrOrTriple, MAI.get(), MRI.get(), MSTI.get());
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(MC, /*PIC=*/false));
  MC.setObjectFileInfo(MOFI.get());

  MCTargetOptions Options;
  auto MAB = TheTarget->createMCAsmBackend(*MSTI, *MRI, Options);
  if (!MAB)
    return error("no asm backend for target " + TripleName, Context);

  std::unique_ptr<MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  if (!MII)
    return error("no instr info info for target " + TripleName, Context);

  MCCodeEmitter *MCE = TheTarget->createMCCodeEmitter(*MII, MC);
  if (!MCE)
    return error("no code emitter for target " + TripleName, Context);

  // Create the output file.
  std::error_code EC;
  ToolOutputFile OutFile(OutputFilename, EC, sys::fs::OF_None);
  std::optional<buffer_ostream> BOS;
  raw_pwrite_stream *OS;
  if (EC)
    return error(Twine(OutputFilename) + ": " + EC.message(), Context);
  if (OutFile.os().supportsSeeking()) {
    OS = &OutFile.os();
  } else {
    BOS.emplace(OutFile.os());
    OS = &*BOS;
  }

  std::unique_ptr<MCStreamer> MS(TheTarget->createMCObjectStreamer(
      *ErrOrTriple, MC, std::unique_ptr<MCAsmBackend>(MAB),
      MAB->createObjectWriter(*OS), std::unique_ptr<MCCodeEmitter>(MCE),
      *MSTI));
  if (!MS)
    return error("no object streamer for target " + TripleName, Context);

  if (auto Err = write(*MS, DWOFilenames, OverflowOptValue)) {
    logAllUnhandledErrors(std::move(Err), WithColor::error());
    return 1;
  }

  MS->finish();
  OutFile.keep();
  return 0;
}
