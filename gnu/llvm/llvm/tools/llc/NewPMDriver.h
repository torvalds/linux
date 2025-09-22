//===- NewPMDriver.h - Function to drive llc with the new PM ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A single function which is called to drive the llc behavior for the new
/// PassManager.
///
/// This is only in a separate TU with a header to avoid including all of the
/// old pass manager headers and the new pass manager headers into the same
/// file. Eventually all of the routines here will get folded back into
/// llc.cpp.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLC_NEWPMDRIVER_H
#define LLVM_TOOLS_LLC_NEWPMDRIVER_H

#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/Support/CodeGen.h"
#include <memory>
#include <vector>

namespace llvm {
class Module;
class TargetLibraryInfoImpl;
class TargetMachine;
class ToolOutputFile;
class LLVMContext;
class MIRParser;

struct LLCDiagnosticHandler : public DiagnosticHandler {
  bool handleDiagnostics(const DiagnosticInfo &DI) override;
};

int compileModuleWithNewPM(StringRef Arg0, std::unique_ptr<Module> M,
                           std::unique_ptr<MIRParser> MIR,
                           std::unique_ptr<TargetMachine> Target,
                           std::unique_ptr<ToolOutputFile> Out,
                           std::unique_ptr<ToolOutputFile> DwoOut,
                           LLVMContext &Context,
                           const TargetLibraryInfoImpl &TLII, bool NoVerify,
                           StringRef PassPipeline, CodeGenFileType FileType);
} // namespace llvm

#endif
