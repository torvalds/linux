//===-- llvm-mc-assemble-fuzzer.cpp - Fuzzer for the MC layer -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Target.h"
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
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"

using namespace llvm;

static mc::RegisterMCTargetOptionsFlags MOF;

static cl::opt<std::string>
    TripleName("triple", cl::desc("Target triple to assemble for, "
                                  "see -version for available targets"));

static cl::opt<std::string>
    MCPU("mcpu",
         cl::desc("Target a specific cpu type (-mcpu=help for details)"),
         cl::value_desc("cpu-name"), cl::init(""));

// This is useful for variable-length instruction sets.
static cl::opt<unsigned> InsnLimit(
    "insn-limit",
    cl::desc("Limit the number of instructions to process (0 for no limit)"),
    cl::value_desc("count"), cl::init(0));

static cl::list<std::string>
    MAttrs("mattr", cl::CommaSeparated,
           cl::desc("Target specific attributes (-mattr=help for details)"),
           cl::value_desc("a1,+a2,-a3,..."));
// The feature string derived from -mattr's values.
std::string FeaturesStr;

static cl::list<std::string>
    FuzzerArgs("fuzzer-args", cl::Positional,
               cl::desc("Options to pass to the fuzzer"),
               cl::PositionalEatsArgs);
static std::vector<char *> ModifiedArgv;

enum OutputFileType {
  OFT_Null,
  OFT_AssemblyFile,
  OFT_ObjectFile
};
static cl::opt<OutputFileType>
FileType("filetype", cl::init(OFT_AssemblyFile),
  cl::desc("Choose an output file type:"),
  cl::values(
       clEnumValN(OFT_AssemblyFile, "asm",
                  "Emit an assembly ('.s') file"),
       clEnumValN(OFT_Null, "null",
                  "Don't emit anything (for timing purposes)"),
       clEnumValN(OFT_ObjectFile, "obj",
                  "Emit a native object ('.o') file")));


class LLVMFuzzerInputBuffer : public MemoryBuffer
{
  public:
    LLVMFuzzerInputBuffer(const uint8_t *data_, size_t size_)
      : Data(reinterpret_cast<const char *>(data_)),
        Size(size_) {
        init(Data, Data+Size, false);
      }


    virtual BufferKind getBufferKind() const {
      return MemoryBuffer_Malloc; // it's not disk-backed so I think that's
                                  // the intent ... though AFAIK it
                                  // probably came from an mmap or sbrk
    }

  private:
    const char *Data;
    size_t Size;
};

static int AssembleInput(const char *ProgName, const Target *TheTarget,
                         SourceMgr &SrcMgr, MCContext &Ctx, MCStreamer &Str,
                         MCAsmInfo &MAI, MCSubtargetInfo &STI,
                         MCInstrInfo &MCII, MCTargetOptions &MCOptions) {
  static const bool NoInitialTextSection = false;

  std::unique_ptr<MCAsmParser> Parser(
    createMCAsmParser(SrcMgr, Ctx, Str, MAI));

  std::unique_ptr<MCTargetAsmParser> TAP(
    TheTarget->createMCAsmParser(STI, *Parser, MCII, MCOptions));

  if (!TAP) {
    errs() << ProgName
           << ": error: this target '" << TripleName
           << "', does not support assembly parsing.\n";
    abort();
  }

  Parser->setTargetParser(*TAP);

  return Parser->Run(NoInitialTextSection);
}


int AssembleOneInput(const uint8_t *Data, size_t Size) {
  Triple TheTriple(Triple::normalize(TripleName));

  SourceMgr SrcMgr;

  std::unique_ptr<MemoryBuffer> BufferPtr(new LLVMFuzzerInputBuffer(Data, Size));

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(BufferPtr), SMLoc());

  static const std::vector<std::string> NoIncludeDirs;
  SrcMgr.setIncludeDirs(NoIncludeDirs);

  static std::string ArchName;
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(ArchName, TheTriple,
      Error);
  if (!TheTarget) {
    errs() << "error: this target '" << TheTriple.normalize()
      << "/" << ArchName << "', was not found: '" << Error << "'\n";

    abort();
  }

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  if (!MRI) {
    errs() << "Unable to create target register info!";
    abort();
  }

  MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags();
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  if (!MAI) {
    errs() << "Unable to create target asm info!";
    abort();
  }

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, MCPU, FeaturesStr));

  MCContext Ctx(TheTriple, MAI.get(), MRI.get(), STI.get(), &SrcMgr);
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, /*PIC=*/false));
  Ctx.setObjectFileInfo(MOFI.get());

  const unsigned OutputAsmVariant = 0;
  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  MCInstPrinter *IP = TheTarget->createMCInstPrinter(Triple(TripleName), OutputAsmVariant,
      *MAI, *MCII, *MRI);
  if (!IP) {
    errs()
      << "error: unable to create instruction printer for target triple '"
      << TheTriple.normalize() << "' with assembly variant "
      << OutputAsmVariant << ".\n";

    abort();
  }

  const char *ProgName = "llvm-mc-fuzzer";
  std::unique_ptr<MCCodeEmitter> CE = nullptr;
  std::unique_ptr<MCAsmBackend> MAB = nullptr;

  std::string OutputString;
  raw_string_ostream Out(OutputString);
  auto FOut = std::make_unique<formatted_raw_ostream>(Out);

  std::unique_ptr<MCStreamer> Str;

  if (FileType == OFT_AssemblyFile) {
    Str.reset(TheTarget->createAsmStreamer(Ctx, std::move(FOut), IP,
                                           std::move(CE), std::move(MAB)));
  } else {
    assert(FileType == OFT_ObjectFile && "Invalid file type!");

    std::error_code EC;
    const std::string OutputFilename = "-";
    auto Out =
        std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::OF_None);
    if (EC) {
      errs() << EC.message() << '\n';
      abort();
    }

    // Don't waste memory on names of temp labels.
    Ctx.setUseNamesOnTempLabels(false);

    std::unique_ptr<buffer_ostream> BOS;
    raw_pwrite_stream *OS = &Out->os();
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
  }
  const int Res = AssembleInput(ProgName, TheTarget, SrcMgr, Ctx, *Str, *MAI, *STI,
      *MCII, MCOptions);

  (void) Res;

  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  return AssembleOneInput(Data, Size);
}

extern "C" LLVM_ATTRIBUTE_USED int LLVMFuzzerInitialize(int *argc,
                                                        char ***argv) {
  // The command line is unusual compared to other fuzzers due to the need to
  // specify the target. Options like -triple, -mcpu, and -mattr work like
  // their counterparts in llvm-mc, while -fuzzer-args collects options for the
  // fuzzer itself.
  //
  // Examples:
  //
  // Fuzz the big-endian MIPS32R6 disassembler using 100,000 inputs of up to
  // 4-bytes each and use the contents of ./corpus as the test corpus:
  //   llvm-mc-fuzzer -triple mips-linux-gnu -mcpu=mips32r6 -disassemble \
  //       -fuzzer-args -max_len=4 -runs=100000 ./corpus
  //
  // Infinitely fuzz the little-endian MIPS64R2 disassembler with the MSA
  // feature enabled using up to 64-byte inputs:
  //   llvm-mc-fuzzer -triple mipsel-linux-gnu -mcpu=mips64r2 -mattr=msa \
  //       -disassemble -fuzzer-args ./corpus
  //
  // If your aim is to find instructions that are not tested, then it is
  // advisable to constrain the maximum input size to a single instruction
  // using -max_len as in the first example. This results in a test corpus of
  // individual instructions that test unique paths. Without this constraint,
  // there will be considerable redundancy in the corpus.

  char **OriginalArgv = *argv;

  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllAsmParsers();

  cl::ParseCommandLineOptions(*argc, OriginalArgv);

  // Rebuild the argv without the arguments llvm-mc-fuzzer consumed so that
  // the driver can parse its arguments.
  //
  // FuzzerArgs cannot provide the non-const pointer that OriginalArgv needs.
  // Re-use the strings from OriginalArgv instead of copying FuzzerArg to a
  // non-const buffer to avoid the need to clean up when the fuzzer terminates.
  ModifiedArgv.push_back(OriginalArgv[0]);
  for (const auto &FuzzerArg : FuzzerArgs) {
    for (int i = 1; i < *argc; ++i) {
      if (FuzzerArg == OriginalArgv[i])
        ModifiedArgv.push_back(OriginalArgv[i]);
    }
  }
  *argc = ModifiedArgv.size();
  *argv = ModifiedArgv.data();

  // Package up features to be passed to target/subtarget
  // We have to pass it via a global since the callback doesn't
  // permit any user data.
  if (MAttrs.size()) {
    SubtargetFeatures Features;
    for (unsigned i = 0; i != MAttrs.size(); ++i)
      Features.AddFeature(MAttrs[i]);
    FeaturesStr = Features.getString();
  }

  if (TripleName.empty())
    TripleName = sys::getDefaultTargetTriple();

  return 0;
}
