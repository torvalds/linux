//===-- llvm-bcanalyzer.cpp - Bitcode Analyzer --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tool may be invoked in the following manner:
//  llvm-bcanalyzer [options]      - Read LLVM bitcode from stdin
//  llvm-bcanalyzer [options] x.bc - Read LLVM bitcode from the x.bc file
//
//  Options:
//      --help            - Output information about command line switches
//      --dump            - Dump low-level bitcode structure in readable format
//      --dump-blockinfo  - Dump the BLOCKINFO_BLOCK, when used with --dump
//
// This tool provides analytical information about a bitcode file. It is
// intended as an aid to developers of bitcode reading and writing software. It
// produces on std::out a summary of the bitcode file that shows various
// statistics about the contents of the file. By default this information is
// detailed and contains information about individual bitcode blocks and the
// functions in the module.
// The tool is also able to print a bitcode file in a straight forward text
// format that shows the containment and relationships of the information in
// the bitcode file (-dump option).
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeAnalyzer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>
using namespace llvm;

static cl::OptionCategory BCAnalyzerCategory("BC Analyzer Options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode>"),
                                          cl::init("-"),
                                          cl::cat(BCAnalyzerCategory));

static cl::opt<bool> Dump("dump", cl::desc("Dump low level bitcode trace"),
                          cl::cat(BCAnalyzerCategory));

static cl::opt<bool> DumpBlockinfo("dump-blockinfo",
                                   cl::desc("Include BLOCKINFO details in low"
                                            " level dump"),
                                   cl::cat(BCAnalyzerCategory));

//===----------------------------------------------------------------------===//
// Bitcode specific analysis.
//===----------------------------------------------------------------------===//

static cl::opt<bool> NoHistogram("disable-histogram",
                                 cl::desc("Do not print per-code histogram"),
                                 cl::cat(BCAnalyzerCategory));

static cl::opt<bool> NonSymbolic("non-symbolic",
                                 cl::desc("Emit numeric info in dump even if"
                                          " symbolic info is available"),
                                 cl::cat(BCAnalyzerCategory));

static cl::opt<std::string>
    BlockInfoFilename("block-info",
                      cl::desc("Use the BLOCK_INFO from the given file"),
                      cl::cat(BCAnalyzerCategory));

static cl::opt<bool>
    ShowBinaryBlobs("show-binary-blobs",
                    cl::desc("Print binary blobs using hex escapes"),
                    cl::cat(BCAnalyzerCategory));

static cl::opt<std::string> CheckHash(
    "check-hash",
    cl::desc("Check module hash using the argument as a string table"),
    cl::cat(BCAnalyzerCategory));

static Error reportError(StringRef Message) {
  return createStringError(std::errc::illegal_byte_sequence, Message.data());
}

static Expected<std::unique_ptr<MemoryBuffer>> openBitcodeFile(StringRef Path) {
  // Read the input file.
  Expected<std::unique_ptr<MemoryBuffer>> MemBufOrErr =
      errorOrToExpected(MemoryBuffer::getFileOrSTDIN(Path));
  if (Error E = MemBufOrErr.takeError())
    return std::move(E);

  std::unique_ptr<MemoryBuffer> MemBuf = std::move(*MemBufOrErr);

  if (MemBuf->getBufferSize() & 3)
    return reportError(
        "Bitcode stream should be a multiple of 4 bytes in length");
  return std::move(MemBuf);
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::HideUnrelatedOptions({&BCAnalyzerCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "llvm-bcanalyzer file analyzer\n");
  ExitOnError ExitOnErr("llvm-bcanalyzer: ");

  std::unique_ptr<MemoryBuffer> MB = ExitOnErr(openBitcodeFile(InputFilename));
  std::unique_ptr<MemoryBuffer> BlockInfoMB = nullptr;
  if (!BlockInfoFilename.empty())
    BlockInfoMB = ExitOnErr(openBitcodeFile(BlockInfoFilename));

  BitcodeAnalyzer BA(MB->getBuffer(),
                     BlockInfoMB
                         ? std::optional<StringRef>(BlockInfoMB->getBuffer())
                         : std::nullopt);

  BCDumpOptions O(outs());
  O.Histogram = !NoHistogram;
  O.Symbolic = !NonSymbolic;
  O.ShowBinaryBlobs = ShowBinaryBlobs;
  O.DumpBlockinfo = DumpBlockinfo;

  ExitOnErr(BA.analyze(
      Dump ? std::optional<BCDumpOptions>(O) : std::optional<BCDumpOptions>(),
      CheckHash.empty() ? std::nullopt : std::optional<StringRef>(CheckHash)));

  if (Dump)
    outs() << "\n\n";

  BA.printStats(O, StringRef(InputFilename.getValue()));
  return 0;
}
