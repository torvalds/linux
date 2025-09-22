//===- NewPMDriver.cpp - Driver for llc using new PM ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file is just a split of the code that logically belongs in llc.cpp but
/// that includes the new pass manager headers.
///
//===----------------------------------------------------------------------===//

#include "NewPMDriver.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/CodeGenPassBuilder.h" // TODO: Include pass headers properly.
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Target/CGPassBuilderOption.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace llvm {
extern cl::opt<bool> PrintPipelinePasses;
} // namespace llvm

using namespace llvm;

static cl::opt<std::string>
    RegAlloc("regalloc-npm",
             cl::desc("Register allocator to use for new pass manager"),
             cl::Hidden, cl::init("default"));

static cl::opt<bool>
    DebugPM("debug-pass-manager", cl::Hidden,
            cl::desc("Print pass management debugging information"));

bool LLCDiagnosticHandler::handleDiagnostics(const DiagnosticInfo &DI) {
  DiagnosticHandler::handleDiagnostics(DI);
  if (DI.getKind() == llvm::DK_SrcMgr) {
    const auto &DISM = cast<DiagnosticInfoSrcMgr>(DI);
    const SMDiagnostic &SMD = DISM.getSMDiag();

    SMD.print(nullptr, errs());

    // For testing purposes, we print the LocCookie here.
    if (DISM.isInlineAsmDiag() && DISM.getLocCookie())
      WithColor::note() << "!srcloc = " << DISM.getLocCookie() << "\n";

    return true;
  }

  if (auto *Remark = dyn_cast<DiagnosticInfoOptimizationBase>(&DI))
    if (!Remark->isEnabled())
      return true;

  DiagnosticPrinterRawOStream DP(errs());
  errs() << LLVMContext::getDiagnosticMessagePrefix(DI.getSeverity()) << ": ";
  DI.print(DP);
  errs() << "\n";
  return true;
}

static llvm::ExitOnError ExitOnErr;

int llvm::compileModuleWithNewPM(
    StringRef Arg0, std::unique_ptr<Module> M, std::unique_ptr<MIRParser> MIR,
    std::unique_ptr<TargetMachine> Target, std::unique_ptr<ToolOutputFile> Out,
    std::unique_ptr<ToolOutputFile> DwoOut, LLVMContext &Context,
    const TargetLibraryInfoImpl &TLII, bool NoVerify, StringRef PassPipeline,
    CodeGenFileType FileType) {

  if (!PassPipeline.empty() && TargetPassConfig::hasLimitedCodeGenPipeline()) {
    WithColor::warning(errs(), Arg0)
        << "--passes cannot be used with "
        << TargetPassConfig::getLimitedCodeGenPipelineReason() << ".\n";
    return 1;
  }

  LLVMTargetMachine &LLVMTM = static_cast<LLVMTargetMachine &>(*Target);

  raw_pwrite_stream *OS = &Out->os();

  // Fetch options from TargetPassConfig
  CGPassBuilderOption Opt = getCGPassBuilderOption();
  Opt.DisableVerify = NoVerify;
  Opt.DebugPM = DebugPM;
  Opt.RegAlloc = RegAlloc;

  MachineModuleInfo MMI(&LLVMTM);

  PassInstrumentationCallbacks PIC;
  StandardInstrumentations SI(Context, Opt.DebugPM, !NoVerify);
  registerCodeGenCallback(PIC, LLVMTM);

  MachineFunctionAnalysisManager MFAM;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassBuilder PB(Target.get(), PipelineTuningOptions(), std::nullopt, &PIC);
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.registerMachineFunctionAnalyses(MFAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM, &MFAM);
  SI.registerCallbacks(PIC, &MAM);

  FAM.registerPass([&] { return TargetLibraryAnalysis(TLII); });
  MAM.registerPass([&] { return MachineModuleAnalysis(MMI); });

  ModulePassManager MPM;
  FunctionPassManager FPM;

  if (!PassPipeline.empty()) {
    // Construct a custom pass pipeline that starts after instruction
    // selection.

    if (!MIR) {
      WithColor::warning(errs(), Arg0) << "-passes is for .mir file only.\n";
      return 1;
    }

    // FIXME: verify that there are no IR passes.
    ExitOnErr(PB.parsePassPipeline(MPM, PassPipeline));
    MPM.addPass(PrintMIRPreparePass(*OS));
    MachineFunctionPassManager MFPM;
    MFPM.addPass(PrintMIRPass(*OS));
    FPM.addPass(createFunctionToMachineFunctionPassAdaptor(std::move(MFPM)));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

    if (MIR->parseMachineFunctions(*M, MAM))
      return 1;
  } else {
    ExitOnErr(LLVMTM.buildCodeGenPipeline(
        MPM, *OS, DwoOut ? &DwoOut->os() : nullptr, FileType, Opt, &PIC));
  }

  if (PrintPipelinePasses) {
    std::string PipelineStr;
    raw_string_ostream OS(PipelineStr);
    MPM.printPipeline(OS, [&PIC](StringRef ClassName) {
      auto PassName = PIC.getPassNameForClassName(ClassName);
      return PassName.empty() ? ClassName : PassName;
    });
    outs() << PipelineStr << '\n';
    return 0;
  }

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  MPM.run(*M, MAM);

  if (Context.getDiagHandlerPtr()->HasErrors)
    exit(1);

  // Declare success.
  Out->keep();
  if (DwoOut)
    DwoOut->keep();

  return 0;
}
