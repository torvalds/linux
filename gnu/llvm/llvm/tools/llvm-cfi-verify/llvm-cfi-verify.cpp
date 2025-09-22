//===-- llvm-cfi-verify.cpp - CFI Verification tool for LLVM --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tool verifies Control Flow Integrity (CFI) instrumentation by static
// binary analysis. See the design document in /docs/CFIVerify.rst for more
// information.
//
// This tool is currently incomplete. It currently only does disassembly for
// object files, and searches through the code for indirect control flow
// instructions, printing them once found.
//
//===----------------------------------------------------------------------===//

#include "lib/FileAnalysis.h"
#include "lib/GraphBuilder.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"

#include <cstdlib>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::cfi_verify;

static cl::OptionCategory CFIVerifyCategory("CFI Verify Options");

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input file>"),
                                   cl::Required, cl::cat(CFIVerifyCategory));
cl::opt<std::string> IgnorelistFilename(cl::Positional,
                                        cl::desc("[ignorelist file]"),
                                        cl::init("-"),
                                        cl::cat(CFIVerifyCategory));
cl::opt<bool> PrintGraphs(
    "print-graphs",
    cl::desc("Print graphs around indirect CF instructions in DOT format."),
    cl::init(false), cl::cat(CFIVerifyCategory));
cl::opt<unsigned> PrintBlameContext(
    "blame-context",
    cl::desc("Print the blame context (if possible) for BAD instructions. This "
             "specifies the number of lines of context to include, where zero "
             "disables this feature."),
    cl::init(0), cl::cat(CFIVerifyCategory));
cl::opt<unsigned> PrintBlameContextAll(
    "blame-context-all",
    cl::desc("Prints the blame context (if possible) for ALL instructions. "
             "This specifies the number of lines of context for non-BAD "
             "instructions (see --blame-context). If --blame-context is "
             "unspecified, it prints this number of contextual lines for BAD "
             "instructions as well."),
    cl::init(0), cl::cat(CFIVerifyCategory));
cl::opt<bool> Summarize("summarize", cl::desc("Print the summary only."),
                        cl::init(false), cl::cat(CFIVerifyCategory));

ExitOnError ExitOnErr;

static void printBlameContext(const DILineInfo &LineInfo, unsigned Context) {
  auto FileOrErr = MemoryBuffer::getFile(LineInfo.FileName);
  if (!FileOrErr) {
    errs() << "Could not open file: " << LineInfo.FileName << "\n";
    return;
  }

  std::unique_ptr<MemoryBuffer> File = std::move(FileOrErr.get());
  SmallVector<StringRef, 100> Lines;
  File->getBuffer().split(Lines, '\n');

  for (unsigned i = std::max<size_t>(1, LineInfo.Line - Context);
       i <
       std::min<size_t>(Lines.size() + 1, LineInfo.Line + Context + 1);
       ++i) {
    if (i == LineInfo.Line)
      outs() << ">";
    else
      outs() << " ";

    outs() << i << ": " << Lines[i - 1] << "\n";
  }
}

static void printInstructionInformation(const FileAnalysis &Analysis,
                                        const Instr &InstrMeta,
                                        const GraphResult &Graph,
                                        CFIProtectionStatus ProtectionStatus) {
  outs() << "Instruction: " << format_hex(InstrMeta.VMAddress, 2) << " ("
         << stringCFIProtectionStatus(ProtectionStatus) << "): ";
  Analysis.printInstruction(InstrMeta, outs());
  outs() << " \n";

  if (PrintGraphs)
    Graph.printToDOT(Analysis, outs());
}

static void printInstructionStatus(unsigned BlameLine, bool CFIProtected,
                                   const DILineInfo &LineInfo) {
  if (BlameLine) {
    outs() << "Ignorelist Match: " << IgnorelistFilename << ":" << BlameLine
           << "\n";
    if (CFIProtected)
      outs() << "====> Unexpected Protected\n";
    else
      outs() << "====> Expected Unprotected\n";

    if (PrintBlameContextAll)
      printBlameContext(LineInfo, PrintBlameContextAll);
  } else {
    if (CFIProtected) {
      outs() << "====> Expected Protected\n";
      if (PrintBlameContextAll)
        printBlameContext(LineInfo, PrintBlameContextAll);
    } else {
      outs() << "====> Unexpected Unprotected (BAD)\n";
      if (PrintBlameContext)
        printBlameContext(LineInfo, PrintBlameContext);
    }
  }
}

static void
printIndirectCFInstructions(FileAnalysis &Analysis,
                            const SpecialCaseList *SpecialCaseList) {
  uint64_t ExpectedProtected = 0;
  uint64_t UnexpectedProtected = 0;
  uint64_t ExpectedUnprotected = 0;
  uint64_t UnexpectedUnprotected = 0;

  std::map<unsigned, uint64_t> BlameCounter;

  for (object::SectionedAddress Address : Analysis.getIndirectInstructions()) {
    const auto &InstrMeta = Analysis.getInstructionOrDie(Address.Address);
    GraphResult Graph = GraphBuilder::buildFlowGraph(Analysis, Address);

    CFIProtectionStatus ProtectionStatus =
        Analysis.validateCFIProtection(Graph);
    bool CFIProtected = (ProtectionStatus == CFIProtectionStatus::PROTECTED);

    if (!Summarize) {
      outs() << "-----------------------------------------------------\n";
      printInstructionInformation(Analysis, InstrMeta, Graph, ProtectionStatus);
    }

    if (IgnoreDWARFFlag) {
      if (CFIProtected)
        ExpectedProtected++;
      else
        UnexpectedUnprotected++;
      continue;
    }

    auto InliningInfo = Analysis.symbolizeInlinedCode(Address);
    if (!InliningInfo || InliningInfo->getNumberOfFrames() == 0) {
      errs() << "Failed to symbolise " << format_hex(Address.Address, 2)
             << " with line tables from " << InputFilename << "\n";
      exit(EXIT_FAILURE);
    }

    const auto &LineInfo = InliningInfo->getFrame(0);

    // Print the inlining symbolisation of this instruction.
    if (!Summarize) {
      for (uint32_t i = 0; i < InliningInfo->getNumberOfFrames(); ++i) {
        const auto &Line = InliningInfo->getFrame(i);
        outs() << "  " << format_hex(Address.Address, 2) << " = "
               << Line.FileName << ":" << Line.Line << ":" << Line.Column
               << " (" << Line.FunctionName << ")\n";
      }
    }

    if (!SpecialCaseList) {
      if (CFIProtected) {
        if (PrintBlameContextAll && !Summarize)
          printBlameContext(LineInfo, PrintBlameContextAll);
        ExpectedProtected++;
      } else {
        if (PrintBlameContext && !Summarize)
          printBlameContext(LineInfo, PrintBlameContext);
        UnexpectedUnprotected++;
      }
      continue;
    }

    unsigned BlameLine = 0;
    for (auto &K : {"cfi-icall", "cfi-vcall"}) {
      if (!BlameLine)
        BlameLine =
            SpecialCaseList->inSectionBlame(K, "src", LineInfo.FileName);
      if (!BlameLine)
        BlameLine =
            SpecialCaseList->inSectionBlame(K, "fun", LineInfo.FunctionName);
    }

    if (BlameLine) {
      BlameCounter[BlameLine]++;
      if (CFIProtected)
        UnexpectedProtected++;
      else
        ExpectedUnprotected++;
    } else {
      if (CFIProtected)
        ExpectedProtected++;
      else
        UnexpectedUnprotected++;
    }

    if (!Summarize)
      printInstructionStatus(BlameLine, CFIProtected, LineInfo);
  }

  uint64_t IndirectCFInstructions = ExpectedProtected + UnexpectedProtected +
                                    ExpectedUnprotected + UnexpectedUnprotected;

  if (IndirectCFInstructions == 0) {
    outs() << "No indirect CF instructions found.\n";
    return;
  }

  outs() << formatv("\nTotal Indirect CF Instructions: {0}\n"
                    "Expected Protected: {1} ({2:P})\n"
                    "Unexpected Protected: {3} ({4:P})\n"
                    "Expected Unprotected: {5} ({6:P})\n"
                    "Unexpected Unprotected (BAD): {7} ({8:P})\n",
                    IndirectCFInstructions, ExpectedProtected,
                    ((double)ExpectedProtected) / IndirectCFInstructions,
                    UnexpectedProtected,
                    ((double)UnexpectedProtected) / IndirectCFInstructions,
                    ExpectedUnprotected,
                    ((double)ExpectedUnprotected) / IndirectCFInstructions,
                    UnexpectedUnprotected,
                    ((double)UnexpectedUnprotected) / IndirectCFInstructions);

  if (!SpecialCaseList)
    return;

  outs() << "\nIgnorelist Results:\n";
  for (const auto &KV : BlameCounter) {
    outs() << "  " << IgnorelistFilename << ":" << KV.first << " affects "
           << KV.second << " indirect CF instructions.\n";
  }
}

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions({&CFIVerifyCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(
      argc, argv,
      "Identifies whether Control Flow Integrity protects all indirect control "
      "flow instructions in the provided object file, DSO or binary.\nNote: "
      "Anything statically linked into the provided file *must* be compiled "
      "with '-g'. This can be relaxed through the '--ignore-dwarf' flag.");

  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllDisassemblers();

  if (PrintBlameContextAll && !PrintBlameContext)
    PrintBlameContext.setValue(PrintBlameContextAll);

  std::unique_ptr<SpecialCaseList> SpecialCaseList;
  if (IgnorelistFilename != "-") {
    std::string Error;
    SpecialCaseList = SpecialCaseList::create({IgnorelistFilename},
                                              *vfs::getRealFileSystem(), Error);
    if (!SpecialCaseList) {
      errs() << "Failed to get ignorelist: " << Error << "\n";
      exit(EXIT_FAILURE);
    }
  }

  FileAnalysis Analysis = ExitOnErr(FileAnalysis::Create(InputFilename));
  printIndirectCFInstructions(Analysis, SpecialCaseList.get());

  return EXIT_SUCCESS;
}
