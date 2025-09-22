//===-- llvm-dis.cpp - The low-level LLVM disassembler --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-dis [options]      - Read LLVM bitcode from stdin, write asm to stdout
//  llvm-dis [options] x.bc - Read LLVM bitcode from the x.bc file, write asm
//                            to the x.ll file.
//  Options:
//      --help   - Output information about command line switches
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include <system_error>
using namespace llvm;

static cl::OptionCategory DisCategory("Disassembler Options");

static cl::list<std::string> InputFilenames(cl::Positional,
                                            cl::desc("[input bitcode]..."),
                                            cl::cat(DisCategory));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"),
                                           cl::cat(DisCategory));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"),
                           cl::cat(DisCategory));

static cl::opt<bool> DontPrint("disable-output",
                               cl::desc("Don't output the .ll file"),
                               cl::Hidden, cl::cat(DisCategory));

static cl::opt<bool>
    SetImporting("set-importing",
                 cl::desc("Set lazy loading to pretend to import a module"),
                 cl::Hidden, cl::cat(DisCategory));

static cl::opt<bool>
    ShowAnnotations("show-annotations",
                    cl::desc("Add informational comments to the .ll file"),
                    cl::cat(DisCategory));

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden, cl::cat(DisCategory));

static cl::opt<bool>
    MaterializeMetadata("materialize-metadata",
                        cl::desc("Load module without materializing metadata, "
                                 "then materialize only the metadata"),
                        cl::cat(DisCategory));

static cl::opt<bool> PrintThinLTOIndexOnly(
    "print-thinlto-index-only",
    cl::desc("Only read thinlto index and print the index as LLVM assembly."),
    cl::init(false), cl::Hidden, cl::cat(DisCategory));

extern cl::opt<bool> WriteNewDbgInfoFormat;

extern cl::opt<cl::boolOrDefault> LoadBitcodeIntoNewDbgInfoFormat;

namespace {

static void printDebugLoc(const DebugLoc &DL, formatted_raw_ostream &OS) {
  OS << DL.getLine() << ":" << DL.getCol();
  if (DILocation *IDL = DL.getInlinedAt()) {
    OS << "@";
    printDebugLoc(IDL, OS);
  }
}
class CommentWriter : public AssemblyAnnotationWriter {
public:
  void emitFunctionAnnot(const Function *F,
                         formatted_raw_ostream &OS) override {
    OS << "; [#uses=" << F->getNumUses() << ']';  // Output # uses
    OS << '\n';
  }
  void printInfoComment(const Value &V, formatted_raw_ostream &OS) override {
    bool Padded = false;
    if (!V.getType()->isVoidTy()) {
      OS.PadToColumn(50);
      Padded = true;
      // Output # uses and type
      OS << "; [#uses=" << V.getNumUses() << " type=" << *V.getType() << "]";
    }
    if (const Instruction *I = dyn_cast<Instruction>(&V)) {
      if (const DebugLoc &DL = I->getDebugLoc()) {
        if (!Padded) {
          OS.PadToColumn(50);
          Padded = true;
          OS << ";";
        }
        OS << " [debug line = ";
        printDebugLoc(DL,OS);
        OS << "]";
      }
      if (const DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(I)) {
        if (!Padded) {
          OS.PadToColumn(50);
          OS << ";";
        }
        OS << " [debug variable = " << DDI->getVariable()->getName() << "]";
      }
      else if (const DbgValueInst *DVI = dyn_cast<DbgValueInst>(I)) {
        if (!Padded) {
          OS.PadToColumn(50);
          OS << ";";
        }
        OS << " [debug variable = " << DVI->getVariable()->getName() << "]";
      }
    }
  }
};

struct LLVMDisDiagnosticHandler : public DiagnosticHandler {
  char *Prefix;
  LLVMDisDiagnosticHandler(char *PrefixPtr) : Prefix(PrefixPtr) {}
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    raw_ostream &OS = errs();
    OS << Prefix << ": ";
    switch (DI.getSeverity()) {
      case DS_Error: WithColor::error(OS); break;
      case DS_Warning: WithColor::warning(OS); break;
      case DS_Remark: OS << "remark: "; break;
      case DS_Note: WithColor::note(OS); break;
    }

    DiagnosticPrinterRawOStream DP(OS);
    DI.print(DP);
    OS << '\n';

    if (DI.getSeverity() == DS_Error)
      exit(1);
    return true;
  }
};
} // end anon namespace

static ExitOnError ExitOnErr;

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  ExitOnErr.setBanner(std::string(argv[0]) + ": error: ");

  cl::HideUnrelatedOptions({&DisCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "llvm .bc -> .ll disassembler\n");

  // Load bitcode into the new debug info format by default.
  if (LoadBitcodeIntoNewDbgInfoFormat == cl::boolOrDefault::BOU_UNSET)
    LoadBitcodeIntoNewDbgInfoFormat = cl::boolOrDefault::BOU_TRUE;

  LLVMContext Context;
  Context.setDiagnosticHandler(
      std::make_unique<LLVMDisDiagnosticHandler>(argv[0]));

  if (InputFilenames.size() < 1) {
    InputFilenames.push_back("-");
  } else if (InputFilenames.size() > 1 && !OutputFilename.empty()) {
    errs()
        << "error: output file name cannot be set for multiple input files\n";
    return 1;
  }

  for (std::string InputFilename : InputFilenames) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getFileOrSTDIN(InputFilename);
    if (std::error_code EC = BufferOrErr.getError()) {
      WithColor::error() << InputFilename << ": " << EC.message() << '\n';
      return 1;
    }
    std::unique_ptr<MemoryBuffer> MB = std::move(BufferOrErr.get());

    BitcodeFileContents IF = ExitOnErr(llvm::getBitcodeFileContents(*MB));

    const size_t N = IF.Mods.size();

    if (OutputFilename == "-" && N > 1)
      errs() << "only single module bitcode files can be written to stdout\n";

    for (size_t I = 0; I < N; ++I) {
      BitcodeModule MB = IF.Mods[I];

      std::unique_ptr<Module> M;

      if (!PrintThinLTOIndexOnly) {
        M = ExitOnErr(
            MB.getLazyModule(Context, MaterializeMetadata, SetImporting));
        if (MaterializeMetadata)
          ExitOnErr(M->materializeMetadata());
        else
          ExitOnErr(M->materializeAll());
      }

      BitcodeLTOInfo LTOInfo = ExitOnErr(MB.getLTOInfo());
      std::unique_ptr<ModuleSummaryIndex> Index;
      if (LTOInfo.HasSummary)
        Index = ExitOnErr(MB.getSummary());

      std::string FinalFilename(OutputFilename);

      // Just use stdout.  We won't actually print anything on it.
      if (DontPrint)
        FinalFilename = "-";

      if (FinalFilename.empty()) { // Unspecified output, infer it.
        if (InputFilename == "-") {
          FinalFilename = "-";
        } else {
          StringRef IFN = InputFilename;
          FinalFilename = (IFN.ends_with(".bc") ? IFN.drop_back(3) : IFN).str();
          if (N > 1)
            FinalFilename += std::string(".") + std::to_string(I);
          FinalFilename += ".ll";
        }
      } else {
        if (N > 1)
          FinalFilename += std::string(".") + std::to_string(I);
      }

      std::error_code EC;
      std::unique_ptr<ToolOutputFile> Out(
          new ToolOutputFile(FinalFilename, EC, sys::fs::OF_TextWithCRLF));
      if (EC) {
        errs() << EC.message() << '\n';
        return 1;
      }

      std::unique_ptr<AssemblyAnnotationWriter> Annotator;
      if (ShowAnnotations)
        Annotator.reset(new CommentWriter());

      // All that llvm-dis does is write the assembly to a file.
      if (!DontPrint) {
        if (M) {
          M->setIsNewDbgInfoFormat(WriteNewDbgInfoFormat);
          if (WriteNewDbgInfoFormat)
            M->removeDebugIntrinsicDeclarations();
          M->print(Out->os(), Annotator.get(), PreserveAssemblyUseListOrder);
        }
        if (Index)
          Index->print(Out->os());
      }

      // Declare success.
      Out->keep();
    }
  }

  return 0;
}
