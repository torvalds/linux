//===-- llvm-mc.cpp - Machine Code Hacking Driver ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility is a simple driver that allows command line hacking on machine
// code.
//
//===----------------------------------------------------------------------===//

#include "Disassembler.h"
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
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;

static mc::RegisterMCTargetOptionsFlags MOF;

static cl::OptionCategory MCCategory("MC Options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::init("-"), cl::cat(MCCategory));

static cl::list<std::string>
    DisassemblerOptions("M", cl::desc("Disassembler options"),
                        cl::cat(MCCategory));

static cl::opt<std::string> OutputFilename("o", cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("-"), cl::cat(MCCategory));

static cl::opt<std::string> SplitDwarfFile("split-dwarf-file",
                                           cl::desc("DWO output filename"),
                                           cl::value_desc("filename"),
                                           cl::cat(MCCategory));

static cl::opt<bool> ShowEncoding("show-encoding",
                                  cl::desc("Show instruction encodings"),
                                  cl::cat(MCCategory));

static cl::opt<DebugCompressionType> CompressDebugSections(
    "compress-debug-sections", cl::ValueOptional,
    cl::init(DebugCompressionType::None),
    cl::desc("Choose DWARF debug sections compression:"),
    cl::values(clEnumValN(DebugCompressionType::None, "none", "No compression"),
               clEnumValN(DebugCompressionType::Zlib, "zlib", "Use zlib"),
               clEnumValN(DebugCompressionType::Zstd, "zstd", "Use zstd")),
    cl::cat(MCCategory));

static cl::opt<bool>
    ShowInst("show-inst", cl::desc("Show internal instruction representation"),
             cl::cat(MCCategory));

static cl::opt<bool>
    ShowInstOperands("show-inst-operands",
                     cl::desc("Show instructions operands as parsed"),
                     cl::cat(MCCategory));

static cl::opt<unsigned>
    OutputAsmVariant("output-asm-variant",
                     cl::desc("Syntax variant to use for output printing"),
                     cl::cat(MCCategory));

static cl::opt<bool>
    PrintImmHex("print-imm-hex", cl::init(false),
                cl::desc("Prefer hex format for immediate values"),
                cl::cat(MCCategory));

static cl::list<std::string>
    DefineSymbol("defsym",
                 cl::desc("Defines a symbol to be an integer constant"),
                 cl::cat(MCCategory));

static cl::opt<bool>
    PreserveComments("preserve-comments",
                     cl::desc("Preserve Comments in outputted assembly"),
                     cl::cat(MCCategory));

enum OutputFileType {
  OFT_Null,
  OFT_AssemblyFile,
  OFT_ObjectFile
};
static cl::opt<OutputFileType>
    FileType("filetype", cl::init(OFT_AssemblyFile),
             cl::desc("Choose an output file type:"),
             cl::values(clEnumValN(OFT_AssemblyFile, "asm",
                                   "Emit an assembly ('.s') file"),
                        clEnumValN(OFT_Null, "null",
                                   "Don't emit anything (for timing purposes)"),
                        clEnumValN(OFT_ObjectFile, "obj",
                                   "Emit a native object ('.o') file")),
             cl::cat(MCCategory));

static cl::list<std::string> IncludeDirs("I",
                                         cl::desc("Directory of include files"),
                                         cl::value_desc("directory"),
                                         cl::Prefix, cl::cat(MCCategory));

static cl::opt<std::string>
    ArchName("arch",
             cl::desc("Target arch to assemble for, "
                      "see -version for available targets"),
             cl::cat(MCCategory));

static cl::opt<std::string>
    TripleName("triple",
               cl::desc("Target triple to assemble for, "
                        "see -version for available targets"),
               cl::cat(MCCategory));

static cl::opt<std::string>
    MCPU("mcpu",
         cl::desc("Target a specific cpu type (-mcpu=help for details)"),
         cl::value_desc("cpu-name"), cl::init(""), cl::cat(MCCategory));

static cl::list<std::string>
    MAttrs("mattr", cl::CommaSeparated,
           cl::desc("Target specific attributes (-mattr=help for details)"),
           cl::value_desc("a1,+a2,-a3,..."), cl::cat(MCCategory));

static cl::opt<bool> PIC("position-independent",
                         cl::desc("Position independent"), cl::init(false),
                         cl::cat(MCCategory));

static cl::opt<bool>
    LargeCodeModel("large-code-model",
                   cl::desc("Create cfi directives that assume the code might "
                            "be more than 2gb away"),
                   cl::cat(MCCategory));

static cl::opt<bool>
    NoInitialTextSection("n",
                         cl::desc("Don't assume assembly file starts "
                                  "in the text section"),
                         cl::cat(MCCategory));

static cl::opt<bool>
    GenDwarfForAssembly("g",
                        cl::desc("Generate dwarf debugging info for assembly "
                                 "source files"),
                        cl::cat(MCCategory));

static cl::opt<std::string>
    DebugCompilationDir("fdebug-compilation-dir",
                        cl::desc("Specifies the debug info's compilation dir"),
                        cl::cat(MCCategory));

static cl::list<std::string> DebugPrefixMap(
    "fdebug-prefix-map", cl::desc("Map file source paths in debug info"),
    cl::value_desc("= separated key-value pairs"), cl::cat(MCCategory));

static cl::opt<std::string> MainFileName(
    "main-file-name",
    cl::desc("Specifies the name we should consider the input file"),
    cl::cat(MCCategory));

static cl::opt<bool> LexMasmIntegers(
    "masm-integers",
    cl::desc("Enable binary and hex masm integers (0b110 and 0ABCh)"),
    cl::cat(MCCategory));

static cl::opt<bool> LexMasmHexFloats(
    "masm-hexfloats",
    cl::desc("Enable MASM-style hex float initializers (3F800000r)"),
    cl::cat(MCCategory));

static cl::opt<bool> LexMotorolaIntegers(
    "motorola-integers",
    cl::desc("Enable binary and hex Motorola integers (%110 and $ABC)"),
    cl::cat(MCCategory));

static cl::opt<bool> NoExecStack("no-exec-stack",
                                 cl::desc("File doesn't need an exec stack"),
                                 cl::cat(MCCategory));

enum ActionType {
  AC_AsLex,
  AC_Assemble,
  AC_Disassemble,
  AC_MDisassemble,
  AC_CDisassemble,
};

static cl::opt<ActionType> Action(
    cl::desc("Action to perform:"), cl::init(AC_Assemble),
    cl::values(clEnumValN(AC_AsLex, "as-lex", "Lex tokens from a .s file"),
               clEnumValN(AC_Assemble, "assemble",
                          "Assemble a .s file (default)"),
               clEnumValN(AC_Disassemble, "disassemble",
                          "Disassemble strings of hex bytes"),
               clEnumValN(AC_MDisassemble, "mdis",
                          "Marked up disassembly of strings of hex bytes"),
               clEnumValN(AC_CDisassemble, "cdis",
                          "Colored disassembly of strings of hex bytes")),
    cl::cat(MCCategory));

static const Target *GetTarget(const char *ProgName) {
  // Figure out the target triple.
  if (TripleName.empty())
    TripleName = sys::getDefaultTargetTriple();
  Triple TheTriple(Triple::normalize(TripleName));

  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(ArchName, TheTriple,
                                                         Error);
  if (!TheTarget) {
    WithColor::error(errs(), ProgName) << Error;
    return nullptr;
  }

  // Update the triple name and return the found target.
  TripleName = TheTriple.getTriple();
  return TheTarget;
}

static std::unique_ptr<ToolOutputFile> GetOutputStream(StringRef Path,
    sys::fs::OpenFlags Flags) {
  std::error_code EC;
  auto Out = std::make_unique<ToolOutputFile>(Path, EC, Flags);
  if (EC) {
    WithColor::error() << EC.message() << '\n';
    return nullptr;
  }

  return Out;
}

static std::string DwarfDebugFlags;
static void setDwarfDebugFlags(int argc, char **argv) {
  if (!getenv("RC_DEBUG_OPTIONS"))
    return;
  for (int i = 0; i < argc; i++) {
    DwarfDebugFlags += argv[i];
    if (i + 1 < argc)
      DwarfDebugFlags += " ";
  }
}

static std::string DwarfDebugProducer;
static void setDwarfDebugProducer() {
  if(!getenv("DEBUG_PRODUCER"))
    return;
  DwarfDebugProducer += getenv("DEBUG_PRODUCER");
}

static int AsLexInput(SourceMgr &SrcMgr, MCAsmInfo &MAI,
                      raw_ostream &OS) {

  AsmLexer Lexer(MAI);
  Lexer.setBuffer(SrcMgr.getMemoryBuffer(SrcMgr.getMainFileID())->getBuffer());

  bool Error = false;
  while (Lexer.Lex().isNot(AsmToken::Eof)) {
    Lexer.getTok().dump(OS);
    OS << "\n";
    if (Lexer.getTok().getKind() == AsmToken::Error)
      Error = true;
  }

  return Error;
}

static int fillCommandLineSymbols(MCAsmParser &Parser) {
  for (auto &I: DefineSymbol) {
    auto Pair = StringRef(I).split('=');
    auto Sym = Pair.first;
    auto Val = Pair.second;

    if (Sym.empty() || Val.empty()) {
      WithColor::error() << "defsym must be of the form: sym=value: " << I
                         << "\n";
      return 1;
    }
    int64_t Value;
    if (Val.getAsInteger(0, Value)) {
      WithColor::error() << "value is not an integer: " << Val << "\n";
      return 1;
    }
    Parser.getContext().setSymbolValue(Parser.getStreamer(), Sym, Value);
  }
  return 0;
}

static int AssembleInput(const char *ProgName, const Target *TheTarget,
                         SourceMgr &SrcMgr, MCContext &Ctx, MCStreamer &Str,
                         MCAsmInfo &MAI, MCSubtargetInfo &STI,
                         MCInstrInfo &MCII, MCTargetOptions const &MCOptions) {
  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, Ctx, Str, MAI));
  std::unique_ptr<MCTargetAsmParser> TAP(
      TheTarget->createMCAsmParser(STI, *Parser, MCII, MCOptions));

  if (!TAP) {
    WithColor::error(errs(), ProgName)
        << "this target does not support assembly parsing.\n";
    return 1;
  }

  int SymbolResult = fillCommandLineSymbols(*Parser);
  if(SymbolResult)
    return SymbolResult;
  Parser->setShowParsedOperands(ShowInstOperands);
  Parser->setTargetParser(*TAP);
  Parser->getLexer().setLexMasmIntegers(LexMasmIntegers);
  Parser->getLexer().setLexMasmHexFloats(LexMasmHexFloats);
  Parser->getLexer().setLexMotorolaIntegers(LexMotorolaIntegers);

  int Res = Parser->Run(NoInitialTextSection);

  return Res;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Initialize targets and assembly printers/parsers.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();

  // Register the target printer for --version.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  cl::HideUnrelatedOptions({&MCCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "llvm machine code playground\n");
  MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags();
  MCOptions.CompressDebugSections = CompressDebugSections.getValue();
  MCOptions.ShowMCInst = ShowInst;
  MCOptions.AsmVerbose = true;
  MCOptions.MCUseDwarfDirectory = MCTargetOptions::EnableDwarfDirectory;

  setDwarfDebugFlags(argc, argv);
  setDwarfDebugProducer();

  const char *ProgName = argv[0];
  const Target *TheTarget = GetTarget(ProgName);
  if (!TheTarget)
    return 1;
  // Now that GetTarget() has (potentially) replaced TripleName, it's safe to
  // construct the Triple object.
  Triple TheTriple(TripleName);

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferPtr =
      MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (std::error_code EC = BufferPtr.getError()) {
    WithColor::error(errs(), ProgName)
        << InputFilename << ": " << EC.message() << '\n';
    return 1;
  }
  MemoryBuffer *Buffer = BufferPtr->get();

  SourceMgr SrcMgr;

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(*BufferPtr), SMLoc());

  // Record the location of the include directories so that the lexer can find
  // it later.
  SrcMgr.setIncludeDirs(IncludeDirs);

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  assert(MRI && "Unable to create target register info!");

  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  assert(MAI && "Unable to create target asm info!");

  if (CompressDebugSections != DebugCompressionType::None) {
    if (const char *Reason = compression::getReasonIfUnsupported(
            compression::formatFor(CompressDebugSections))) {
      WithColor::error(errs(), ProgName)
          << "--compress-debug-sections: " << Reason;
      return 1;
    }
  }
  MAI->setPreserveAsmComments(PreserveComments);

  // Package up features to be passed to target/subtarget
  std::string FeaturesStr;
  if (MAttrs.size()) {
    SubtargetFeatures Features;
    for (unsigned i = 0; i != MAttrs.size(); ++i)
      Features.AddFeature(MAttrs[i]);
    FeaturesStr = Features.getString();
  }

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, MCPU, FeaturesStr));
  assert(STI && "Unable to create subtarget info!");

  // FIXME: This is not pretty. MCContext has a ptr to MCObjectFileInfo and
  // MCObjectFileInfo needs a MCContext reference in order to initialize itself.
  MCContext Ctx(TheTriple, MAI.get(), MRI.get(), STI.get(), &SrcMgr,
                &MCOptions);
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, PIC, LargeCodeModel));
  Ctx.setObjectFileInfo(MOFI.get());

  Ctx.setGenDwarfForAssembly(GenDwarfForAssembly);
  // Default to 4 for dwarf version.
  unsigned DwarfVersion = MCOptions.DwarfVersion ? MCOptions.DwarfVersion : 4;
  if (DwarfVersion < 2 || DwarfVersion > 5) {
    errs() << ProgName << ": Dwarf version " << DwarfVersion
           << " is not supported." << '\n';
    return 1;
  }
  Ctx.setDwarfVersion(DwarfVersion);
  if (MCOptions.Dwarf64) {
    // The 64-bit DWARF format was introduced in DWARFv3.
    if (DwarfVersion < 3) {
      errs() << ProgName
             << ": the 64-bit DWARF format is not supported for DWARF versions "
                "prior to 3\n";
      return 1;
    }
    // 32-bit targets don't support DWARF64, which requires 64-bit relocations.
    if (MAI->getCodePointerSize() < 8) {
      errs() << ProgName
             << ": the 64-bit DWARF format is only supported for 64-bit "
                "targets\n";
      return 1;
    }
    // If needsDwarfSectionOffsetDirective is true, we would eventually call
    // MCStreamer::emitSymbolValue() with IsSectionRelative = true, but that
    // is supported only for 4-byte long references.
    if (MAI->needsDwarfSectionOffsetDirective()) {
      errs() << ProgName << ": the 64-bit DWARF format is not supported for "
             << TheTriple.normalize() << "\n";
      return 1;
    }
    Ctx.setDwarfFormat(dwarf::DWARF64);
  }
  if (!DwarfDebugFlags.empty())
    Ctx.setDwarfDebugFlags(StringRef(DwarfDebugFlags));
  if (!DwarfDebugProducer.empty())
    Ctx.setDwarfDebugProducer(StringRef(DwarfDebugProducer));
  if (!DebugCompilationDir.empty())
    Ctx.setCompilationDir(DebugCompilationDir);
  else {
    // If no compilation dir is set, try to use the current directory.
    SmallString<128> CWD;
    if (!sys::fs::current_path(CWD))
      Ctx.setCompilationDir(CWD);
  }
  for (const auto &Arg : DebugPrefixMap) {
    const auto &KV = StringRef(Arg).split('=');
    Ctx.addDebugPrefixMapEntry(std::string(KV.first), std::string(KV.second));
  }
  if (!MainFileName.empty())
    Ctx.setMainFileName(MainFileName);
  if (GenDwarfForAssembly)
    Ctx.setGenDwarfRootFile(InputFilename, Buffer->getBuffer());

  sys::fs::OpenFlags Flags = (FileType == OFT_AssemblyFile)
                                 ? sys::fs::OF_TextWithCRLF
                                 : sys::fs::OF_None;
  std::unique_ptr<ToolOutputFile> Out = GetOutputStream(OutputFilename, Flags);
  if (!Out)
    return 1;

  std::unique_ptr<ToolOutputFile> DwoOut;
  if (!SplitDwarfFile.empty()) {
    if (FileType != OFT_ObjectFile) {
      WithColor::error() << "dwo output only supported with object files\n";
      return 1;
    }
    DwoOut = GetOutputStream(SplitDwarfFile, sys::fs::OF_None);
    if (!DwoOut)
      return 1;
  }

  std::unique_ptr<buffer_ostream> BOS;
  raw_pwrite_stream *OS = &Out->os();
  std::unique_ptr<MCStreamer> Str;

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  assert(MCII && "Unable to create instruction info!");

  MCInstPrinter *IP = nullptr;
  if (FileType == OFT_AssemblyFile) {
    IP = TheTarget->createMCInstPrinter(Triple(TripleName), OutputAsmVariant,
                                        *MAI, *MCII, *MRI);

    if (!IP) {
      WithColor::error()
          << "unable to create instruction printer for target triple '"
          << TheTriple.normalize() << "' with assembly variant "
          << OutputAsmVariant << ".\n";
      return 1;
    }

    for (StringRef Opt : DisassemblerOptions)
      if (!IP->applyTargetSpecificCLOption(Opt)) {
        WithColor::error() << "invalid disassembler option '" << Opt << "'\n";
        return 1;
      }

    // Set the display preference for hex vs. decimal immediates.
    IP->setPrintImmHex(PrintImmHex);

    // Set up the AsmStreamer.
    std::unique_ptr<MCCodeEmitter> CE;
    if (ShowEncoding)
      CE.reset(TheTarget->createMCCodeEmitter(*MCII, Ctx));

    std::unique_ptr<MCAsmBackend> MAB(
        TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
    auto FOut = std::make_unique<formatted_raw_ostream>(*OS);
    Str.reset(TheTarget->createAsmStreamer(Ctx, std::move(FOut), IP,
                                           std::move(CE), std::move(MAB)));

  } else if (FileType == OFT_Null) {
    Str.reset(TheTarget->createNullStreamer(Ctx));
  } else {
    assert(FileType == OFT_ObjectFile && "Invalid file type!");

    if (!Out->os().supportsSeeking()) {
      BOS = std::make_unique<buffer_ostream>(Out->os());
      OS = BOS.get();
    }

    MCCodeEmitter *CE = TheTarget->createMCCodeEmitter(*MCII, Ctx);
    MCAsmBackend *MAB = TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions);
    Str.reset(TheTarget->createMCObjectStreamer(
        TheTriple, Ctx, std::unique_ptr<MCAsmBackend>(MAB),
        DwoOut ? MAB->createDwoObjectWriter(*OS, DwoOut->os())
               : MAB->createObjectWriter(*OS),
        std::unique_ptr<MCCodeEmitter>(CE), *STI));
    if (NoExecStack)
      Str->initSections(true, *STI);
  }

  int Res = 1;
  bool disassemble = false;
  switch (Action) {
  case AC_AsLex:
    Res = AsLexInput(SrcMgr, *MAI, Out->os());
    break;
  case AC_Assemble:
    Res = AssembleInput(ProgName, TheTarget, SrcMgr, Ctx, *Str, *MAI, *STI,
                        *MCII, MCOptions);
    break;
  case AC_MDisassemble:
    IP->setUseMarkup(true);
    disassemble = true;
    break;
  case AC_CDisassemble:
    IP->setUseColor(true);
    disassemble = true;
    break;
  case AC_Disassemble:
    disassemble = true;
    break;
  }
  if (disassemble)
    Res = Disassembler::disassemble(*TheTarget, TripleName, *STI, *Str, *Buffer,
                                    SrcMgr, Ctx, MCOptions);

  // Keep output if no errors.
  if (Res == 0) {
    Out->keep();
    if (DwoOut)
      DwoOut->keep();
  }
  return Res;
}
