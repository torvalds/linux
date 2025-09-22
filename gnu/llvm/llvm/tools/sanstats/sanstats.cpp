//===- sanstats.cpp - Sanitizer statistics dumper -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tool dumps statistics information from files in the format produced
// by clang's -fsanitize-stats feature.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Transforms/Utils/SanitizerStats.h"
#include <stdint.h>

using namespace llvm;

static cl::OptionCategory Cat("sanstats Options");

static cl::opt<std::string> ClInputFile(cl::Positional, cl::Required,
                                        cl::desc("<filename>"));

static cl::opt<bool> ClDemangle("demangle", cl::init(false),
                                cl::desc("Print demangled function name"),
                                cl::cat(Cat));

inline uint64_t KindFromData(uint64_t Data, char SizeofPtr) {
  return Data >> (SizeofPtr * 8 - kSanitizerStatKindBits);
}

inline uint64_t CountFromData(uint64_t Data, char SizeofPtr) {
  return Data & ((1ull << (SizeofPtr * 8 - kSanitizerStatKindBits)) - 1);
}

static uint64_t ReadLE(char Size, const char *Begin, const char *End) {
  uint64_t Result = 0;
  char Pos = 0;
  while (Begin < End && Pos != Size) {
    Result |= uint64_t(uint8_t(*Begin)) << (Pos * 8);
    ++Begin;
    ++Pos;
  }
  return Result;
}

static const char *ReadModule(char SizeofPtr, const char *Begin,
                              const char *End) {
  const char *FilenameBegin = Begin;
  while (Begin != End && *Begin)
    ++Begin;
  if (Begin == End)
    return nullptr;
  std::string Filename(FilenameBegin, Begin - FilenameBegin);

  if (!llvm::sys::fs::exists(Filename))
    Filename = std::string(llvm::sys::path::parent_path(ClInputFile)) +
               std::string(llvm::sys::path::filename(Filename));

  ++Begin;
  if (Begin == End)
    return nullptr;

  symbolize::LLVMSymbolizer::Options SymbolizerOptions;
  SymbolizerOptions.Demangle = ClDemangle;
  SymbolizerOptions.UseSymbolTable = true;
  symbolize::LLVMSymbolizer Symbolizer(SymbolizerOptions);

  while (true) {
    uint64_t Addr = ReadLE(SizeofPtr, Begin, End);
    Begin += SizeofPtr;
    uint64_t Data = ReadLE(SizeofPtr, Begin, End);
    Begin += SizeofPtr;

    if (Begin > End)
      return nullptr;
    if (Addr == 0 && Data == 0)
      return Begin;
    if (Begin == End)
      return nullptr;

    // As the instrumentation tracks the return address and not
    // the address of the call to `__sanitizer_stat_report` we
    // remove one from the address to get the correct DI.
    // TODO: it would be neccessary to set proper section index here.
    // object::SectionedAddress::UndefSection works for only absolute addresses.
    if (Expected<DILineInfo> LineInfo = Symbolizer.symbolizeCode(
            Filename, {Addr - 1, object::SectionedAddress::UndefSection})) {
      llvm::outs() << format_hex(Addr - 1, 18) << ' ' << LineInfo->FileName
                   << ':' << LineInfo->Line << ' ' << LineInfo->FunctionName
                   << ' ';
    } else {
      logAllUnhandledErrors(LineInfo.takeError(), llvm::outs(), "<error> ");
    }

    switch (KindFromData(Data, SizeofPtr)) {
    case SanStat_CFI_VCall:
      llvm::outs() << "cfi-vcall";
      break;
    case SanStat_CFI_NVCall:
      llvm::outs() << "cfi-nvcall";
      break;
    case SanStat_CFI_DerivedCast:
      llvm::outs() << "cfi-derived-cast";
      break;
    case SanStat_CFI_UnrelatedCast:
      llvm::outs() << "cfi-unrelated-cast";
      break;
    case SanStat_CFI_ICall:
      llvm::outs() << "cfi-icall";
      break;
    default:
      llvm::outs() << "<unknown>";
      break;
    }

    llvm::outs() << " " << CountFromData(Data, SizeofPtr) << '\n';
  }
}

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions(Cat);
  cl::ParseCommandLineOptions(argc, argv,
                              "Sanitizer Statistics Processing Tool");

  ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr = MemoryBuffer::getFile(
      ClInputFile, /*IsText=*/false, /*RequiresNullTerminator=*/false);
  if (!MBOrErr) {
    errs() << argv[0] << ": " << ClInputFile << ": "
           << MBOrErr.getError().message() << '\n';
    return 1;
  }
  std::unique_ptr<MemoryBuffer> MB = std::move(MBOrErr.get());
  const char *Begin = MB->getBufferStart(), *End = MB->getBufferEnd();
  if (Begin == End) {
    errs() << argv[0] << ": " << ClInputFile << ": short read\n";
    return 1;
  }
  char SizeofPtr = *Begin++;
  while (Begin != End) {
    Begin = ReadModule(SizeofPtr, Begin, End);
    if (Begin == nullptr) {
      errs() << argv[0] << ": " << ClInputFile << ": short read\n";
      return 1;
    }
    assert(Begin <= End);
  }
}
