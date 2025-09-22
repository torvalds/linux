//===-- llvm-modextract.cpp - LLVM module extractor utility ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is for testing features that rely on multi-module bitcode files.
// It takes a multi-module bitcode file, extracts one of the modules and writes
// it to the output file.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;

static cl::OptionCategory ModextractCategory("Modextract Options");

static cl::opt<bool>
    BinaryExtract("b", cl::desc("Whether to perform binary extraction"),
                  cl::cat(ModextractCategory));

static cl::opt<std::string> OutputFilename("o", cl::Required,
                                           cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::cat(ModextractCategory));

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode>"),
                                          cl::init("-"),
                                          cl::cat(ModextractCategory));

static cl::opt<unsigned> ModuleIndex("n", cl::Required,
                                     cl::desc("Index of module to extract"),
                                     cl::value_desc("index"),
                                     cl::cat(ModextractCategory));

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions({&ModextractCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "Module extractor");

  ExitOnError ExitOnErr("llvm-modextract: error: ");

  std::unique_ptr<MemoryBuffer> MB =
      ExitOnErr(errorOrToExpected(MemoryBuffer::getFileOrSTDIN(InputFilename)));
  std::vector<BitcodeModule> Ms = ExitOnErr(getBitcodeModuleList(*MB));

  LLVMContext Context;
  if (ModuleIndex >= Ms.size()) {
    errs() << "llvm-modextract: error: module index out of range; bitcode file "
              "contains "
           << Ms.size() << " module(s)\n";
    return 1;
  }

  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(OutputFilename, EC, sys::fs::OF_None));
  ExitOnErr(errorCodeToError(EC));

  if (BinaryExtract) {
    SmallVector<char, 0> Result;
    BitcodeWriter Writer(Result);
    Result.append(Ms[ModuleIndex].getBuffer().begin(),
                  Ms[ModuleIndex].getBuffer().end());
    Writer.copyStrtab(Ms[ModuleIndex].getStrtab());
    Out->os() << Result;
    Out->keep();
    return 0;
  }

  std::unique_ptr<Module> M = ExitOnErr(Ms[ModuleIndex].parseModule(Context));
  WriteBitcodeToFile(*M, Out->os());

  Out->keep();
  return 0;
}
