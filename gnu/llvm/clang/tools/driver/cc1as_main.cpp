//===-- cc1as_main.cpp - Clang Assembler  ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the clang -cc1as functionality, which implements
// the direct interface to the LLVM MC based assembler.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <optional>
#include <system_error>
using namespace clang;
using namespace clang::driver;
using namespace clang::driver::options;
using namespace llvm;
using namespace llvm::opt;

namespace {

/// Helper class for representing a single invocation of the assembler.
struct AssemblerInvocation {
  /// @name Target Options
  /// @{

  /// The name of the target triple to assemble for.
  std::string Triple;

  /// If given, the name of the target CPU to determine which instructions
  /// are legal.
  std::string CPU;

  /// The list of target specific features to enable or disable -- this should
  /// be a list of strings starting with '+' or '-'.
  std::vector<std::string> Features;

  /// The list of symbol definitions.
  std::vector<std::string> SymbolDefs;

  /// @}
  /// @name Language Options
  /// @{

  std::vector<std::string> IncludePaths;
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoInitialTextSection : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SaveTemporaryLabels : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned GenDwarfForAssembly : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned RelaxELFRelocations : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SSE2AVX : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Dwarf64 : 1;
  unsigned DwarfVersion;
  std::string DwarfDebugFlags;
  std::string DwarfDebugProducer;
  std::string DebugCompilationDir;
  llvm::SmallVector<std::pair<std::string, std::string>, 0> DebugPrefixMap;
  llvm::DebugCompressionType CompressDebugSections =
      llvm::DebugCompressionType::None;
  std::string MainFileName;
  std::string SplitDwarfOutput;

  /// @}
  /// @name Frontend Options
  /// @{

  std::string InputFile;
  std::vector<std::string> LLVMArgs;
  std::string OutputPath;
  enum FileType {
    FT_Asm,  ///< Assembly (.s) output, transliterate mode.
    FT_Null, ///< No output, for timing purposes.
    FT_Obj   ///< Object file output.
  };
  FileType OutputType;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowHelp : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowVersion : 1;

  /// @}
  /// @name Transliterate Options
  /// @{

  unsigned OutputAsmVariant;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowEncoding : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowInst : 1;

  /// @}
  /// @name Assembler Options
  /// @{

  LLVM_PREFERRED_TYPE(bool)
  unsigned RelaxAll : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoExecStack : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned FatalWarnings : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoWarn : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoTypeCheck : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IncrementalLinkerCompatible : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned EmbedBitcode : 1;

  /// Whether to emit DWARF unwind info.
  EmitDwarfUnwindType EmitDwarfUnwind;

  // Whether to emit compact-unwind for non-canonical entries.
  // Note: maybe overriden by other constraints.
  LLVM_PREFERRED_TYPE(bool)
  unsigned EmitCompactUnwindNonCanonical : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Crel : 1;

  /// The name of the relocation model to use.
  std::string RelocationModel;

  /// The ABI targeted by the backend. Specified using -target-abi. Empty
  /// otherwise.
  std::string TargetABI;

  /// Darwin target variant triple, the variant of the deployment target
  /// for which the code is being compiled.
  std::optional<llvm::Triple> DarwinTargetVariantTriple;

  /// The version of the darwin target variant SDK which was used during the
  /// compilation
  llvm::VersionTuple DarwinTargetVariantSDKVersion;

  /// The name of a file to use with \c .secure_log_unique directives.
  std::string AsSecureLogFile;
  /// @}

public:
  AssemblerInvocation() {
    Triple = "";
    NoInitialTextSection = 0;
    InputFile = "-";
    OutputPath = "-";
    OutputType = FT_Asm;
    OutputAsmVariant = 0;
    ShowInst = 0;
    ShowEncoding = 0;
    RelaxAll = 0;
    SSE2AVX = 0;
    NoExecStack = 0;
    FatalWarnings = 0;
    NoWarn = 0;
    NoTypeCheck = 0;
    IncrementalLinkerCompatible = 0;
    Dwarf64 = 0;
    DwarfVersion = 0;
    EmbedBitcode = 0;
    EmitDwarfUnwind = EmitDwarfUnwindType::Default;
    EmitCompactUnwindNonCanonical = false;
    Crel = false;
  }

  static bool CreateFromArgs(AssemblerInvocation &Res,
                             ArrayRef<const char *> Argv,
                             DiagnosticsEngine &Diags);
};

}

bool AssemblerInvocation::CreateFromArgs(AssemblerInvocation &Opts,
                                         ArrayRef<const char *> Argv,
                                         DiagnosticsEngine &Diags) {
  bool Success = true;

  // Parse the arguments.
  const OptTable &OptTbl = getDriverOptTable();

  llvm::opt::Visibility VisibilityMask(options::CC1AsOption);
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args =
      OptTbl.ParseArgs(Argv, MissingArgIndex, MissingArgCount, VisibilityMask);

  // Check for missing argument error.
  if (MissingArgCount) {
    Diags.Report(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;
    Success = false;
  }

  // Issue errors on unknown arguments.
  for (const Arg *A : Args.filtered(OPT_UNKNOWN)) {
    auto ArgString = A->getAsString(Args);
    std::string Nearest;
    if (OptTbl.findNearest(ArgString, Nearest, VisibilityMask) > 1)
      Diags.Report(diag::err_drv_unknown_argument) << ArgString;
    else
      Diags.Report(diag::err_drv_unknown_argument_with_suggestion)
          << ArgString << Nearest;
    Success = false;
  }

  // Construct the invocation.

  // Target Options
  Opts.Triple = llvm::Triple::normalize(Args.getLastArgValue(OPT_triple));
  if (Arg *A = Args.getLastArg(options::OPT_darwin_target_variant_triple))
    Opts.DarwinTargetVariantTriple = llvm::Triple(A->getValue());
  if (Arg *A = Args.getLastArg(OPT_darwin_target_variant_sdk_version_EQ)) {
    VersionTuple Version;
    if (Version.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.DarwinTargetVariantSDKVersion = Version;
  }

  Opts.CPU = std::string(Args.getLastArgValue(OPT_target_cpu));
  Opts.Features = Args.getAllArgValues(OPT_target_feature);

  // Use the default target triple if unspecified.
  if (Opts.Triple.empty())
    Opts.Triple = llvm::sys::getDefaultTargetTriple();

  // Language Options
  Opts.IncludePaths = Args.getAllArgValues(OPT_I);
  Opts.NoInitialTextSection = Args.hasArg(OPT_n);
  Opts.SaveTemporaryLabels = Args.hasArg(OPT_msave_temp_labels);
  // Any DebugInfoKind implies GenDwarfForAssembly.
  Opts.GenDwarfForAssembly = Args.hasArg(OPT_debug_info_kind_EQ);

  if (const Arg *A = Args.getLastArg(OPT_compress_debug_sections_EQ)) {
    Opts.CompressDebugSections =
        llvm::StringSwitch<llvm::DebugCompressionType>(A->getValue())
            .Case("none", llvm::DebugCompressionType::None)
            .Case("zlib", llvm::DebugCompressionType::Zlib)
            .Case("zstd", llvm::DebugCompressionType::Zstd)
            .Default(llvm::DebugCompressionType::None);
  }

  Opts.RelaxELFRelocations = !Args.hasArg(OPT_mrelax_relocations_no);
  Opts.SSE2AVX = Args.hasArg(OPT_msse2avx);
  if (auto *DwarfFormatArg = Args.getLastArg(OPT_gdwarf64, OPT_gdwarf32))
    Opts.Dwarf64 = DwarfFormatArg->getOption().matches(OPT_gdwarf64);
  Opts.DwarfVersion = getLastArgIntValue(Args, OPT_dwarf_version_EQ, 2, Diags);
  Opts.DwarfDebugFlags =
      std::string(Args.getLastArgValue(OPT_dwarf_debug_flags));
  Opts.DwarfDebugProducer =
      std::string(Args.getLastArgValue(OPT_dwarf_debug_producer));
  if (const Arg *A = Args.getLastArg(options::OPT_ffile_compilation_dir_EQ,
                                     options::OPT_fdebug_compilation_dir_EQ))
    Opts.DebugCompilationDir = A->getValue();
  Opts.MainFileName = std::string(Args.getLastArgValue(OPT_main_file_name));

  for (const auto &Arg : Args.getAllArgValues(OPT_fdebug_prefix_map_EQ)) {
    auto Split = StringRef(Arg).split('=');
    Opts.DebugPrefixMap.emplace_back(Split.first, Split.second);
  }

  // Frontend Options
  if (Args.hasArg(OPT_INPUT)) {
    bool First = true;
    for (const Arg *A : Args.filtered(OPT_INPUT)) {
      if (First) {
        Opts.InputFile = A->getValue();
        First = false;
      } else {
        Diags.Report(diag::err_drv_unknown_argument) << A->getAsString(Args);
        Success = false;
      }
    }
  }
  Opts.LLVMArgs = Args.getAllArgValues(OPT_mllvm);
  Opts.OutputPath = std::string(Args.getLastArgValue(OPT_o));
  Opts.SplitDwarfOutput =
      std::string(Args.getLastArgValue(OPT_split_dwarf_output));
  if (Arg *A = Args.getLastArg(OPT_filetype)) {
    StringRef Name = A->getValue();
    unsigned OutputType = StringSwitch<unsigned>(Name)
      .Case("asm", FT_Asm)
      .Case("null", FT_Null)
      .Case("obj", FT_Obj)
      .Default(~0U);
    if (OutputType == ~0U) {
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
      Success = false;
    } else
      Opts.OutputType = FileType(OutputType);
  }
  Opts.ShowHelp = Args.hasArg(OPT_help);
  Opts.ShowVersion = Args.hasArg(OPT_version);

  // Transliterate Options
  Opts.OutputAsmVariant =
      getLastArgIntValue(Args, OPT_output_asm_variant, 0, Diags);
  Opts.ShowEncoding = Args.hasArg(OPT_show_encoding);
  Opts.ShowInst = Args.hasArg(OPT_show_inst);

  // Assemble Options
  Opts.RelaxAll = Args.hasArg(OPT_mrelax_all);
  Opts.NoExecStack = Args.hasArg(OPT_mno_exec_stack);
  Opts.FatalWarnings = Args.hasArg(OPT_massembler_fatal_warnings);
  Opts.NoWarn = Args.hasArg(OPT_massembler_no_warn);
  Opts.NoTypeCheck = Args.hasArg(OPT_mno_type_check);
  Opts.RelocationModel =
      std::string(Args.getLastArgValue(OPT_mrelocation_model, "pic"));
  Opts.TargetABI = std::string(Args.getLastArgValue(OPT_target_abi));
  Opts.IncrementalLinkerCompatible =
      Args.hasArg(OPT_mincremental_linker_compatible);
  Opts.SymbolDefs = Args.getAllArgValues(OPT_defsym);

  // EmbedBitcode Option. If -fembed-bitcode is enabled, set the flag.
  // EmbedBitcode behaves the same for all embed options for assembly files.
  if (auto *A = Args.getLastArg(OPT_fembed_bitcode_EQ)) {
    Opts.EmbedBitcode = llvm::StringSwitch<unsigned>(A->getValue())
                            .Case("all", 1)
                            .Case("bitcode", 1)
                            .Case("marker", 1)
                            .Default(0);
  }

  if (auto *A = Args.getLastArg(OPT_femit_dwarf_unwind_EQ)) {
    Opts.EmitDwarfUnwind =
        llvm::StringSwitch<EmitDwarfUnwindType>(A->getValue())
            .Case("always", EmitDwarfUnwindType::Always)
            .Case("no-compact-unwind", EmitDwarfUnwindType::NoCompactUnwind)
            .Case("default", EmitDwarfUnwindType::Default);
  }

  Opts.EmitCompactUnwindNonCanonical =
      Args.hasArg(OPT_femit_compact_unwind_non_canonical);
  Opts.Crel = Args.hasArg(OPT_crel);

  Opts.AsSecureLogFile = Args.getLastArgValue(OPT_as_secure_log_file);

  return Success;
}

static std::unique_ptr<raw_fd_ostream>
getOutputStream(StringRef Path, DiagnosticsEngine &Diags, bool Binary) {
  // Make sure that the Out file gets unlinked from the disk if we get a
  // SIGINT.
  if (Path != "-")
    sys::RemoveFileOnSignal(Path);

  std::error_code EC;
  auto Out = std::make_unique<raw_fd_ostream>(
      Path, EC, (Binary ? sys::fs::OF_None : sys::fs::OF_TextWithCRLF));
  if (EC) {
    Diags.Report(diag::err_fe_unable_to_open_output) << Path << EC.message();
    return nullptr;
  }

  return Out;
}

static bool ExecuteAssemblerImpl(AssemblerInvocation &Opts,
                                 DiagnosticsEngine &Diags) {
  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(Opts.Triple, Error);
  if (!TheTarget)
    return Diags.Report(diag::err_target_unknown_triple) << Opts.Triple;

  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      MemoryBuffer::getFileOrSTDIN(Opts.InputFile, /*IsText=*/true);

  if (std::error_code EC = Buffer.getError()) {
    return Diags.Report(diag::err_fe_error_reading)
           << Opts.InputFile << EC.message();
  }

  SourceMgr SrcMgr;

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  unsigned BufferIndex = SrcMgr.AddNewSourceBuffer(std::move(*Buffer), SMLoc());

  // Record the location of the include directories so that the lexer can find
  // it later.
  SrcMgr.setIncludeDirs(Opts.IncludePaths);

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(Opts.Triple));
  assert(MRI && "Unable to create target register info!");

  MCTargetOptions MCOptions;
  MCOptions.MCRelaxAll = Opts.RelaxAll;
  MCOptions.EmitDwarfUnwind = Opts.EmitDwarfUnwind;
  MCOptions.EmitCompactUnwindNonCanonical = Opts.EmitCompactUnwindNonCanonical;
  MCOptions.MCSaveTempLabels = Opts.SaveTemporaryLabels;
  MCOptions.Crel = Opts.Crel;
  MCOptions.X86RelaxRelocations = Opts.RelaxELFRelocations;
  MCOptions.X86Sse2Avx = Opts.SSE2AVX;
  MCOptions.CompressDebugSections = Opts.CompressDebugSections;
  MCOptions.AsSecureLogFile = Opts.AsSecureLogFile;

  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, Opts.Triple, MCOptions));
  assert(MAI && "Unable to create target asm info!");

  // Ensure MCAsmInfo initialization occurs before any use, otherwise sections
  // may be created with a combination of default and explicit settings.


  bool IsBinary = Opts.OutputType == AssemblerInvocation::FT_Obj;
  if (Opts.OutputPath.empty())
    Opts.OutputPath = "-";
  std::unique_ptr<raw_fd_ostream> FDOS =
      getOutputStream(Opts.OutputPath, Diags, IsBinary);
  if (!FDOS)
    return true;
  std::unique_ptr<raw_fd_ostream> DwoOS;
  if (!Opts.SplitDwarfOutput.empty())
    DwoOS = getOutputStream(Opts.SplitDwarfOutput, Diags, IsBinary);

  // Build up the feature string from the target feature list.
  std::string FS = llvm::join(Opts.Features, ",");

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(Opts.Triple, Opts.CPU, FS));
  assert(STI && "Unable to create subtarget info!");

  MCContext Ctx(Triple(Opts.Triple), MAI.get(), MRI.get(), STI.get(), &SrcMgr,
                &MCOptions);

  bool PIC = false;
  if (Opts.RelocationModel == "static") {
    PIC = false;
  } else if (Opts.RelocationModel == "pic") {
    PIC = true;
  } else {
    assert(Opts.RelocationModel == "dynamic-no-pic" &&
           "Invalid PIC model!");
    PIC = false;
  }

  // FIXME: This is not pretty. MCContext has a ptr to MCObjectFileInfo and
  // MCObjectFileInfo needs a MCContext reference in order to initialize itself.
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, PIC));
  if (Opts.DarwinTargetVariantTriple)
    MOFI->setDarwinTargetVariantTriple(*Opts.DarwinTargetVariantTriple);
  if (!Opts.DarwinTargetVariantSDKVersion.empty())
    MOFI->setDarwinTargetVariantSDKVersion(Opts.DarwinTargetVariantSDKVersion);
  Ctx.setObjectFileInfo(MOFI.get());

  if (Opts.GenDwarfForAssembly)
    Ctx.setGenDwarfForAssembly(true);
  if (!Opts.DwarfDebugFlags.empty())
    Ctx.setDwarfDebugFlags(StringRef(Opts.DwarfDebugFlags));
  if (!Opts.DwarfDebugProducer.empty())
    Ctx.setDwarfDebugProducer(StringRef(Opts.DwarfDebugProducer));
  if (!Opts.DebugCompilationDir.empty())
    Ctx.setCompilationDir(Opts.DebugCompilationDir);
  else {
    // If no compilation dir is set, try to use the current directory.
    SmallString<128> CWD;
    if (!sys::fs::current_path(CWD))
      Ctx.setCompilationDir(CWD);
  }
  if (!Opts.DebugPrefixMap.empty())
    for (const auto &KV : Opts.DebugPrefixMap)
      Ctx.addDebugPrefixMapEntry(KV.first, KV.second);
  if (!Opts.MainFileName.empty())
    Ctx.setMainFileName(StringRef(Opts.MainFileName));
  Ctx.setDwarfFormat(Opts.Dwarf64 ? dwarf::DWARF64 : dwarf::DWARF32);
  Ctx.setDwarfVersion(Opts.DwarfVersion);
  if (Opts.GenDwarfForAssembly)
    Ctx.setGenDwarfRootFile(Opts.InputFile,
                            SrcMgr.getMemoryBuffer(BufferIndex)->getBuffer());

  std::unique_ptr<MCStreamer> Str;

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  assert(MCII && "Unable to create instruction info!");

  raw_pwrite_stream *Out = FDOS.get();
  std::unique_ptr<buffer_ostream> BOS;

  MCOptions.MCNoWarn = Opts.NoWarn;
  MCOptions.MCFatalWarnings = Opts.FatalWarnings;
  MCOptions.MCNoTypeCheck = Opts.NoTypeCheck;
  MCOptions.ShowMCInst = Opts.ShowInst;
  MCOptions.AsmVerbose = true;
  MCOptions.MCUseDwarfDirectory = MCTargetOptions::EnableDwarfDirectory;
  MCOptions.ABIName = Opts.TargetABI;

  // FIXME: There is a bit of code duplication with addPassesToEmitFile.
  if (Opts.OutputType == AssemblerInvocation::FT_Asm) {
    MCInstPrinter *IP = TheTarget->createMCInstPrinter(
        llvm::Triple(Opts.Triple), Opts.OutputAsmVariant, *MAI, *MCII, *MRI);

    std::unique_ptr<MCCodeEmitter> CE;
    if (Opts.ShowEncoding)
      CE.reset(TheTarget->createMCCodeEmitter(*MCII, Ctx));
    std::unique_ptr<MCAsmBackend> MAB(
        TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));

    auto FOut = std::make_unique<formatted_raw_ostream>(*Out);
    Str.reset(TheTarget->createAsmStreamer(Ctx, std::move(FOut), IP,
                                           std::move(CE), std::move(MAB)));
  } else if (Opts.OutputType == AssemblerInvocation::FT_Null) {
    Str.reset(createNullStreamer(Ctx));
  } else {
    assert(Opts.OutputType == AssemblerInvocation::FT_Obj &&
           "Invalid file type!");
    if (!FDOS->supportsSeeking()) {
      BOS = std::make_unique<buffer_ostream>(*FDOS);
      Out = BOS.get();
    }

    std::unique_ptr<MCCodeEmitter> CE(
        TheTarget->createMCCodeEmitter(*MCII, Ctx));
    std::unique_ptr<MCAsmBackend> MAB(
        TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
    assert(MAB && "Unable to create asm backend!");

    std::unique_ptr<MCObjectWriter> OW =
        DwoOS ? MAB->createDwoObjectWriter(*Out, *DwoOS)
              : MAB->createObjectWriter(*Out);

    Triple T(Opts.Triple);
    Str.reset(TheTarget->createMCObjectStreamer(
        T, Ctx, std::move(MAB), std::move(OW), std::move(CE), *STI));
    Str.get()->initSections(Opts.NoExecStack, *STI);
  }

  // When -fembed-bitcode is passed to clang_as, a 1-byte marker
  // is emitted in __LLVM,__asm section if the object file is MachO format.
  if (Opts.EmbedBitcode && Ctx.getObjectFileType() == MCContext::IsMachO) {
    MCSection *AsmLabel = Ctx.getMachOSection(
        "__LLVM", "__asm", MachO::S_REGULAR, 4, SectionKind::getReadOnly());
    Str.get()->switchSection(AsmLabel);
    Str.get()->emitZeros(1);
  }

  bool Failed = false;

  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, Ctx, *Str.get(), *MAI));

  // FIXME: init MCTargetOptions from sanitizer flags here.
  std::unique_ptr<MCTargetAsmParser> TAP(
      TheTarget->createMCAsmParser(*STI, *Parser, *MCII, MCOptions));
  if (!TAP)
    Failed = Diags.Report(diag::err_target_unknown_triple) << Opts.Triple;

  // Set values for symbols, if any.
  for (auto &S : Opts.SymbolDefs) {
    auto Pair = StringRef(S).split('=');
    auto Sym = Pair.first;
    auto Val = Pair.second;
    int64_t Value;
    // We have already error checked this in the driver.
    Val.getAsInteger(0, Value);
    Ctx.setSymbolValue(Parser->getStreamer(), Sym, Value);
  }

  if (!Failed) {
    Parser->setTargetParser(*TAP.get());
    Failed = Parser->Run(Opts.NoInitialTextSection);
  }

  return Failed;
}

static bool ExecuteAssembler(AssemblerInvocation &Opts,
                             DiagnosticsEngine &Diags) {
  bool Failed = ExecuteAssemblerImpl(Opts, Diags);

  // Delete output file if there were errors.
  if (Failed) {
    if (Opts.OutputPath != "-")
      sys::fs::remove(Opts.OutputPath);
    if (!Opts.SplitDwarfOutput.empty() && Opts.SplitDwarfOutput != "-")
      sys::fs::remove(Opts.SplitDwarfOutput);
  }

  return Failed;
}

static void LLVMErrorHandler(void *UserData, const char *Message,
                             bool GenCrashDiag) {
  DiagnosticsEngine &Diags = *static_cast<DiagnosticsEngine*>(UserData);

  Diags.Report(diag::err_fe_error_backend) << Message;

  // We cannot recover from llvm errors.
  sys::Process::Exit(1);
}

int cc1as_main(ArrayRef<const char *> Argv, const char *Argv0, void *MainAddr) {
  // Initialize targets and assembly printers/parsers.
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();

  // Construct our diagnostic client.
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  TextDiagnosticPrinter *DiagClient
    = new TextDiagnosticPrinter(errs(), &*DiagOpts);
  DiagClient->setPrefix("clang -cc1as");
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagClient);

  // Set an error handler, so that any LLVM backend diagnostics go through our
  // error handler.
  ScopedFatalErrorHandler FatalErrorHandler
    (LLVMErrorHandler, static_cast<void*>(&Diags));

  // Parse the arguments.
  AssemblerInvocation Asm;
  if (!AssemblerInvocation::CreateFromArgs(Asm, Argv, Diags))
    return 1;

  if (Asm.ShowHelp) {
    getDriverOptTable().printHelp(
        llvm::outs(), "clang -cc1as [options] file...",
        "Clang Integrated Assembler", /*ShowHidden=*/false,
        /*ShowAllAliases=*/false,
        llvm::opt::Visibility(driver::options::CC1AsOption));

    return 0;
  }

  // Honor -version.
  //
  // FIXME: Use a better -version message?
  if (Asm.ShowVersion) {
    llvm::cl::PrintVersionMessage();
    return 0;
  }

  // Honor -mllvm.
  //
  // FIXME: Remove this, one day.
  if (!Asm.LLVMArgs.empty()) {
    unsigned NumArgs = Asm.LLVMArgs.size();
    auto Args = std::make_unique<const char*[]>(NumArgs + 2);
    Args[0] = "clang (LLVM option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = Asm.LLVMArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    llvm::cl::ParseCommandLineOptions(NumArgs + 1, Args.get());
  }

  // Execute the invocation, unless there were parsing errors.
  bool Failed = Diags.hasErrorOccurred() || ExecuteAssembler(Asm, Diags);

  // If any timers were active but haven't been destroyed yet, print their
  // results now.
  TimerGroup::printAll(errs());
  TimerGroup::clearAll();

  return !!Failed;
}
