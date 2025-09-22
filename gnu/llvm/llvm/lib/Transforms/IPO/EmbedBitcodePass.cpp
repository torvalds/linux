//===- EmbedBitcodePass.cpp - Pass that embeds the bitcode into a global---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/EmbedBitcodePass.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <memory>
#include <string>

using namespace llvm;

PreservedAnalyses EmbedBitcodePass::run(Module &M, ModuleAnalysisManager &AM) {
  if (M.getGlobalVariable("llvm.embedded.module", /*AllowInternal=*/true))
    report_fatal_error("Can only embed the module once",
                       /*gen_crash_diag=*/false);

  Triple T(M.getTargetTriple());
  if (T.getObjectFormat() != Triple::ELF)
    report_fatal_error(
        "EmbedBitcode pass currently only supports ELF object format",
        /*gen_crash_diag=*/false);

  std::string Data;
  raw_string_ostream OS(Data);
  if (IsThinLTO)
    ThinLTOBitcodeWriterPass(OS, /*ThinLinkOS=*/nullptr).run(M, AM);
  else
    BitcodeWriterPass(OS, /*ShouldPreserveUseListOrder=*/false, EmitLTOSummary)
        .run(M, AM);

  embedBufferInModule(M, MemoryBufferRef(Data, "ModuleData"), ".llvm.lto");

  return PreservedAnalyses::all();
}
