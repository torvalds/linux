//===-- llvm-ml.cpp - masm-compatible assembler -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A simple driver around MasmParser; based on llvm-mc.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Host.h"
#include <ctime>
#include <optional>

using namespace llvm;
using namespace llvm::opt;

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

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class MLOptTable : public opt::GenericOptTable {
public:
  MLOptTable() : opt::GenericOptTable(InfoTable, /*IgnoreCase=*/false) {}
};
} // namespace

static Triple GetTriple(StringRef ProgName, opt::InputArgList &Args) {
  // Figure out the target triple.
  StringRef DefaultBitness = "32";
  SmallString<255> Program = ProgName;
  sys::path::replace_extension(Program, "");
  if (Program.ends_with("ml64"))
    DefaultBitness = "64";

  StringRef TripleName =
      StringSwitch<StringRef>(Args.getLastArgValue(OPT_bitness, DefaultBitness))
          .Case("32", "i386-pc-windows")
          .Case("64", "x86_64-pc-windows")
          .Default("");
  return Triple(Triple::normalize(TripleName));
}

static std::unique_ptr<ToolOutputFile> GetOutputStream(StringRef Path) {
  std::error_code EC;
  auto Out = std::make_unique<ToolOutputFile>(Path, EC, sys::fs::OF_None);
  if (EC) {
    WithColor::error() << EC.message() << '\n';
    return nullptr;
  }

  return Out;
}

static int AsLexInput(SourceMgr &SrcMgr, MCAsmInfo &MAI, raw_ostream &OS) {
  AsmLexer Lexer(MAI);
  Lexer.setBuffer(SrcMgr.getMemoryBuffer(SrcMgr.getMainFileID())->getBuffer());
  Lexer.setLexMasmIntegers(true);
  Lexer.useMasmDefaultRadix(true);
  Lexer.setLexMasmHexFloats(true);
  Lexer.setLexMasmStrings(true);

  bool Error = false;
  while (Lexer.Lex().isNot(AsmToken::Eof)) {
    Lexer.getTok().dump(OS);
    OS << "\n";
    if (Lexer.getTok().getKind() == AsmToken::Error)
      Error = true;
  }

  return Error;
}

static int AssembleInput(StringRef ProgName, const Target *TheTarget,
                         SourceMgr &SrcMgr, MCContext &Ctx, MCStreamer &Str,
                         MCAsmInfo &MAI, MCSubtargetInfo &STI,
                         MCInstrInfo &MCII, MCTargetOptions &MCOptions,
                         const opt::ArgList &InputArgs) {
  struct tm TM;
  time_t Timestamp;
  if (InputArgs.hasArg(OPT_timestamp)) {
    StringRef TimestampStr = InputArgs.getLastArgValue(OPT_timestamp);
    int64_t IntTimestamp;
    if (TimestampStr.getAsInteger(10, IntTimestamp)) {
      WithColor::error(errs(), ProgName)
          << "invalid timestamp '" << TimestampStr
          << "'; must be expressed in seconds since the UNIX epoch.\n";
      return 1;
    }
    Timestamp = IntTimestamp;
  } else {
    Timestamp = time(nullptr);
  }
  if (InputArgs.hasArg(OPT_utc)) {
    // Not thread-safe.
    TM = *gmtime(&Timestamp);
  } else {
    // Not thread-safe.
    TM = *localtime(&Timestamp);
  }

  std::unique_ptr<MCAsmParser> Parser(
      createMCMasmParser(SrcMgr, Ctx, Str, MAI, TM, 0));
  std::unique_ptr<MCTargetAsmParser> TAP(
      TheTarget->createMCAsmParser(STI, *Parser, MCII, MCOptions));

  if (!TAP) {
    WithColor::error(errs(), ProgName)
        << "this target does not support assembly parsing.\n";
    return 1;
  }

  Parser->setShowParsedOperands(InputArgs.hasArg(OPT_show_inst_operands));
  Parser->setTargetParser(*TAP);
  Parser->getLexer().setLexMasmIntegers(true);
  Parser->getLexer().useMasmDefaultRadix(true);
  Parser->getLexer().setLexMasmHexFloats(true);
  Parser->getLexer().setLexMasmStrings(true);

  auto Defines = InputArgs.getAllArgValues(OPT_define);
  for (StringRef Define : Defines) {
    const auto NameValue = Define.split('=');
    StringRef Name = NameValue.first, Value = NameValue.second;
    if (Parser->defineMacro(Name, Value)) {
      WithColor::error(errs(), ProgName)
          << "can't define macro '" << Name << "' = '" << Value << "'\n";
      return 1;
    }
  }

  int Res = Parser->Run(/*NoInitialTextSection=*/true);

  return Res;
}

int llvm_ml_main(int Argc, char **Argv, const llvm::ToolContext &) {
  StringRef ProgName = sys::path::filename(Argv[0]);

  // Initialize targets and assembly printers/parsers.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();

  MLOptTable T;
  unsigned MissingArgIndex, MissingArgCount;
  ArrayRef<const char *> ArgsArr = ArrayRef(Argv + 1, Argc - 1);
  opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgIndex, MissingArgCount);

  std::string InputFilename;
  for (auto *Arg : InputArgs.filtered(OPT_INPUT)) {
    std::string ArgString = Arg->getAsString(InputArgs);
    bool IsFile = false;
    std::error_code IsFileEC =
        llvm::sys::fs::is_regular_file(ArgString, IsFile);
    if (ArgString == "-" || IsFile) {
      if (!InputFilename.empty()) {
        WithColor::warning(errs(), ProgName)
            << "does not support multiple assembly files in one command; "
            << "ignoring '" << InputFilename << "'\n";
      }
      InputFilename = ArgString;
    } else {
      std::string Diag;
      raw_string_ostream OS(Diag);
      OS << ArgString << ": " << IsFileEC.message();

      std::string Nearest;
      if (T.findNearest(ArgString, Nearest) < 2)
        OS << ", did you mean '" << Nearest << "'?";

      WithColor::error(errs(), ProgName) << OS.str() << '\n';
      exit(1);
    }
  }
  for (auto *Arg : InputArgs.filtered(OPT_assembly_file)) {
    if (!InputFilename.empty()) {
      WithColor::warning(errs(), ProgName)
          << "does not support multiple assembly files in one command; "
          << "ignoring '" << InputFilename << "'\n";
    }
    InputFilename = Arg->getValue();
  }

  for (auto *Arg : InputArgs.filtered(OPT_unsupported_Group)) {
    WithColor::warning(errs(), ProgName)
        << "ignoring unsupported '" << Arg->getOption().getName()
        << "' option\n";
  }

  if (InputArgs.hasArg(OPT_debug)) {
    DebugFlag = true;
  }
  for (auto *Arg : InputArgs.filtered(OPT_debug_only)) {
    setCurrentDebugTypes(Arg->getValues().data(), Arg->getNumValues());
  }

  if (InputArgs.hasArg(OPT_help)) {
    std::string Usage = llvm::formatv("{0} [ /options ] file", ProgName).str();
    T.printHelp(outs(), Usage.c_str(), "LLVM MASM Assembler",
                /*ShowHidden=*/false);
    return 0;
  } else if (InputFilename.empty()) {
    outs() << "USAGE: " << ProgName << " [ /options ] file\n"
           << "Run \"" << ProgName << " /?\" or \"" << ProgName
           << " /help\" for more info.\n";
    return 0;
  }

  MCTargetOptions MCOptions;
  MCOptions.AssemblyLanguage = "masm";
  MCOptions.MCFatalWarnings = InputArgs.hasArg(OPT_fatal_warnings);
  MCOptions.MCSaveTempLabels = InputArgs.hasArg(OPT_save_temp_labels);
  MCOptions.ShowMCInst = InputArgs.hasArg(OPT_show_inst);
  MCOptions.AsmVerbose = true;

  Triple TheTriple = GetTriple(ProgName, InputArgs);
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget("", TheTriple, Error);
  if (!TheTarget) {
    WithColor::error(errs(), ProgName) << Error;
    return 1;
  }
  const std::string &TripleName = TheTriple.getTriple();

  bool SafeSEH = InputArgs.hasArg(OPT_safeseh);
  if (SafeSEH && !(TheTriple.isArch32Bit() && TheTriple.isX86())) {
    WithColor::warning()
        << "/safeseh applies only to 32-bit X86 platforms; ignoring.\n";
    SafeSEH = false;
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferPtr =
      MemoryBuffer::getFileOrSTDIN(InputFilename);
  if (std::error_code EC = BufferPtr.getError()) {
    WithColor::error(errs(), ProgName)
        << InputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  SourceMgr SrcMgr;

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(*BufferPtr), SMLoc());

  // Record the location of the include directories so that the lexer can find
  // included files later.
  std::vector<std::string> IncludeDirs =
      InputArgs.getAllArgValues(OPT_include_path);
  if (!InputArgs.hasArg(OPT_ignore_include_envvar)) {
    if (std::optional<std::string> IncludeEnvVar =
            llvm::sys::Process::GetEnv("INCLUDE")) {
      SmallVector<StringRef, 8> Dirs;
      StringRef(*IncludeEnvVar)
          .split(Dirs, ";", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
      IncludeDirs.reserve(IncludeDirs.size() + Dirs.size());
      for (StringRef Dir : Dirs)
        IncludeDirs.push_back(Dir.str());
    }
  }
  SrcMgr.setIncludeDirs(IncludeDirs);

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  assert(MRI && "Unable to create target register info!");

  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  assert(MAI && "Unable to create target asm info!");

  MAI->setPreserveAsmComments(InputArgs.hasArg(OPT_preserve_comments));

  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TripleName, /*CPU=*/"", /*Features=*/""));
  assert(STI && "Unable to create subtarget info!");

  // FIXME: This is not pretty. MCContext has a ptr to MCObjectFileInfo and
  // MCObjectFileInfo needs a MCContext reference in order to initialize itself.
  MCContext Ctx(TheTriple, MAI.get(), MRI.get(), STI.get(), &SrcMgr);
  std::unique_ptr<MCObjectFileInfo> MOFI(TheTarget->createMCObjectFileInfo(
      Ctx, /*PIC=*/false, /*LargeCodeModel=*/true));
  Ctx.setObjectFileInfo(MOFI.get());

  // Set compilation information.
  SmallString<128> CWD;
  if (!sys::fs::current_path(CWD))
    Ctx.setCompilationDir(CWD);
  Ctx.setMainFileName(InputFilename);

  StringRef FileType = InputArgs.getLastArgValue(OPT_filetype, "obj");
  SmallString<255> DefaultOutputFilename;
  if (InputArgs.hasArg(OPT_as_lex)) {
    DefaultOutputFilename = "-";
  } else {
    DefaultOutputFilename = InputFilename;
    sys::path::replace_extension(DefaultOutputFilename, FileType);
  }
  const StringRef OutputFilename =
      InputArgs.getLastArgValue(OPT_output_file, DefaultOutputFilename);
  std::unique_ptr<ToolOutputFile> Out = GetOutputStream(OutputFilename);
  if (!Out)
    return 1;

  std::unique_ptr<buffer_ostream> BOS;
  raw_pwrite_stream *OS = &Out->os();
  std::unique_ptr<MCStreamer> Str;

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  assert(MCII && "Unable to create instruction info!");

  MCInstPrinter *IP = nullptr;
  if (FileType == "s") {
    const bool OutputATTAsm = InputArgs.hasArg(OPT_output_att_asm);
    const unsigned OutputAsmVariant = OutputATTAsm ? 0U   // ATT dialect
                                                   : 1U;  // Intel dialect
    IP = TheTarget->createMCInstPrinter(TheTriple, OutputAsmVariant, *MAI,
                                        *MCII, *MRI);

    if (!IP) {
      WithColor::error()
          << "unable to create instruction printer for target triple '"
          << TheTriple.normalize() << "' with "
          << (OutputATTAsm ? "ATT" : "Intel") << " assembly variant.\n";
      return 1;
    }

    // Set the display preference for hex vs. decimal immediates.
    IP->setPrintImmHex(InputArgs.hasArg(OPT_print_imm_hex));

    // Set up the AsmStreamer.
    std::unique_ptr<MCCodeEmitter> CE;
    if (InputArgs.hasArg(OPT_show_encoding))
      CE.reset(TheTarget->createMCCodeEmitter(*MCII, Ctx));

    std::unique_ptr<MCAsmBackend> MAB(
        TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
    auto FOut = std::make_unique<formatted_raw_ostream>(*OS);
    Str.reset(TheTarget->createAsmStreamer(Ctx, std::move(FOut), IP,
                                           std::move(CE), std::move(MAB)));

  } else if (FileType == "null") {
    Str.reset(TheTarget->createNullStreamer(Ctx));
  } else if (FileType == "obj") {
    if (!Out->os().supportsSeeking()) {
      BOS = std::make_unique<buffer_ostream>(Out->os());
      OS = BOS.get();
    }

    MCCodeEmitter *CE = TheTarget->createMCCodeEmitter(*MCII, Ctx);
    MCAsmBackend *MAB = TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions);
    Str.reset(TheTarget->createMCObjectStreamer(
        TheTriple, Ctx, std::unique_ptr<MCAsmBackend>(MAB),
        MAB->createObjectWriter(*OS), std::unique_ptr<MCCodeEmitter>(CE),
        *STI));
  } else {
    llvm_unreachable("Invalid file type!");
  }

  if (TheTriple.isOSBinFormatCOFF()) {
    // Emit an absolute @feat.00 symbol. This is a features bitfield read by
    // link.exe.
    int64_t Feat00Flags = 0x2;
    if (SafeSEH) {
      // According to the PE-COFF spec, the LSB of this value marks the object
      // for "registered SEH".  This means that all SEH handler entry points
      // must be registered in .sxdata.  Use of any unregistered handlers will
      // cause the process to terminate immediately.
      Feat00Flags |= 0x1;
    }
    MCSymbol *Feat00Sym = Ctx.getOrCreateSymbol("@feat.00");
    Feat00Sym->setRedefinable(true);
    Str->emitSymbolAttribute(Feat00Sym, MCSA_Global);
    Str->emitAssignment(Feat00Sym, MCConstantExpr::create(Feat00Flags, Ctx));
  }

  int Res = 1;
  if (InputArgs.hasArg(OPT_as_lex)) {
    // -as-lex; Lex only, and output a stream of tokens
    Res = AsLexInput(SrcMgr, *MAI, Out->os());
  } else {
    Res = AssembleInput(ProgName, TheTarget, SrcMgr, Ctx, *Str, *MAI, *STI,
                        *MCII, MCOptions, InputArgs);
  }

  // Keep output if no errors.
  if (Res == 0)
    Out->keep();
  return Res;
}
