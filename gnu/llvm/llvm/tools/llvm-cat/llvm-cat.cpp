//===- llvm-cat.cpp - LLVM module concatenation utility -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is for testing features that rely on multi-module bitcode files.
// It takes a list of input modules and uses them to create a multi-module
// bitcode file.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;

cl::OptionCategory CatCategory("llvm-cat Options");

static cl::opt<bool>
    BinaryCat("b", cl::desc("Whether to perform binary concatenation"),
              cl::cat(CatCategory));

static cl::opt<std::string> OutputFilename("o", cl::Required,
                                           cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::cat(CatCategory));

static cl::list<std::string> InputFilenames(cl::Positional,
                                            cl::desc("<input  files>"),
                                            cl::cat(CatCategory));

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions(CatCategory);
  cl::ParseCommandLineOptions(argc, argv, "Module concatenation");

  ExitOnError ExitOnErr("llvm-cat: ");
  LLVMContext Context;

  SmallVector<char, 0> Buffer;
  BitcodeWriter Writer(Buffer);
  if (BinaryCat) {
    for (const auto &InputFilename : InputFilenames) {
      std::unique_ptr<MemoryBuffer> MB = ExitOnErr(
          errorOrToExpected(MemoryBuffer::getFileOrSTDIN(InputFilename)));
      std::vector<BitcodeModule> Mods = ExitOnErr(getBitcodeModuleList(*MB));
      for (auto &BitcodeMod : Mods) {
        llvm::append_range(Buffer, BitcodeMod.getBuffer());
        Writer.copyStrtab(BitcodeMod.getStrtab());
      }
    }
  } else {
    // The string table does not own strings added to it, some of which are
    // owned by the modules; keep them alive until we write the string table.
    std::vector<std::unique_ptr<Module>> OwnedMods;
    for (const auto &InputFilename : InputFilenames) {
      SMDiagnostic Err;
      std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
      if (!M) {
        Err.print(argv[0], errs());
        return 1;
      }
      Writer.writeModule(*M);
      OwnedMods.push_back(std::move(M));
    }
    Writer.writeStrtab();
  }

  std::error_code EC;
  raw_fd_ostream OS(OutputFilename, EC, sys::fs::OpenFlags::OF_None);
  if (EC) {
    errs() << argv[0] << ": cannot open " << OutputFilename << " for writing: "
           << EC.message();
    return 1;
  }

  OS.write(Buffer.data(), Buffer.size());
  return 0;
}
