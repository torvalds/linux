//===-- llvm/CodeGen/MachineModuleInfo.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::dwarf;

static cl::opt<bool>
    DisableDebugInfoPrinting("disable-debug-info-print", cl::Hidden,
                             cl::desc("Disable debug info printing"));

// Out of line virtual method.
MachineModuleInfoImpl::~MachineModuleInfoImpl() = default;

void MachineModuleInfo::initialize() {
  ObjFileMMI = nullptr;
  CurCallSite = 0;
  NextFnNum = 0;
  UsesMSVCFloatingPoint = false;
  DbgInfoAvailable = false;
}

void MachineModuleInfo::finalize() {
  Context.reset();
  // We don't clear the ExternalContext.

  delete ObjFileMMI;
  ObjFileMMI = nullptr;
}

MachineModuleInfo::MachineModuleInfo(MachineModuleInfo &&MMI)
    : TM(std::move(MMI.TM)),
      Context(TM.getTargetTriple(), TM.getMCAsmInfo(), TM.getMCRegisterInfo(),
              TM.getMCSubtargetInfo(), nullptr, &TM.Options.MCOptions, false),
      MachineFunctions(std::move(MMI.MachineFunctions)) {
  Context.setObjectFileInfo(TM.getObjFileLowering());
  ObjFileMMI = MMI.ObjFileMMI;
  CurCallSite = MMI.CurCallSite;
  ExternalContext = MMI.ExternalContext;
  TheModule = MMI.TheModule;
}

MachineModuleInfo::MachineModuleInfo(const LLVMTargetMachine *TM)
    : TM(*TM), Context(TM->getTargetTriple(), TM->getMCAsmInfo(),
                       TM->getMCRegisterInfo(), TM->getMCSubtargetInfo(),
                       nullptr, &TM->Options.MCOptions, false) {
  Context.setObjectFileInfo(TM->getObjFileLowering());
  initialize();
}

MachineModuleInfo::MachineModuleInfo(const LLVMTargetMachine *TM,
                                     MCContext *ExtContext)
    : TM(*TM), Context(TM->getTargetTriple(), TM->getMCAsmInfo(),
                       TM->getMCRegisterInfo(), TM->getMCSubtargetInfo(),
                       nullptr, &TM->Options.MCOptions, false),
      ExternalContext(ExtContext) {
  Context.setObjectFileInfo(TM->getObjFileLowering());
  initialize();
}

MachineModuleInfo::~MachineModuleInfo() { finalize(); }

MachineFunction *
MachineModuleInfo::getMachineFunction(const Function &F) const {
  auto I = MachineFunctions.find(&F);
  return I != MachineFunctions.end() ? I->second.get() : nullptr;
}

MachineFunction &MachineModuleInfo::getOrCreateMachineFunction(Function &F) {
  // Shortcut for the common case where a sequence of MachineFunctionPasses
  // all query for the same Function.
  if (LastRequest == &F)
    return *LastResult;

  auto I = MachineFunctions.insert(
      std::make_pair(&F, std::unique_ptr<MachineFunction>()));
  MachineFunction *MF;
  if (I.second) {
    // No pre-existing machine function, create a new one.
    const TargetSubtargetInfo &STI = *TM.getSubtargetImpl(F);
    MF = new MachineFunction(F, TM, STI, NextFnNum++, *this);
    MF->initTargetMachineFunctionInfo(STI);

    // MRI callback for target specific initializations.
    TM.registerMachineRegisterInfoCallback(*MF);

    // Update the set entry.
    I.first->second.reset(MF);
  } else {
    MF = I.first->second.get();
  }

  LastRequest = &F;
  LastResult = MF;
  return *MF;
}

void MachineModuleInfo::deleteMachineFunctionFor(Function &F) {
  MachineFunctions.erase(&F);
  LastRequest = nullptr;
  LastResult = nullptr;
}

void MachineModuleInfo::insertFunction(const Function &F,
                                       std::unique_ptr<MachineFunction> &&MF) {
  auto I = MachineFunctions.insert(std::make_pair(&F, std::move(MF)));
  assert(I.second && "machine function already mapped");
  (void)I;
}

namespace {

/// This pass frees the MachineFunction object associated with a Function.
class FreeMachineFunction : public FunctionPass {
public:
  static char ID;

  FreeMachineFunction() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
  }

  bool runOnFunction(Function &F) override {
    MachineModuleInfo &MMI =
        getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
    MMI.deleteMachineFunctionFor(F);
    return true;
  }

  StringRef getPassName() const override {
    return "Free MachineFunction";
  }
};

} // end anonymous namespace

char FreeMachineFunction::ID;

FunctionPass *llvm::createFreeMachineFunctionPass() {
  return new FreeMachineFunction();
}

MachineModuleInfoWrapperPass::MachineModuleInfoWrapperPass(
    const LLVMTargetMachine *TM)
    : ImmutablePass(ID), MMI(TM) {
  initializeMachineModuleInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

MachineModuleInfoWrapperPass::MachineModuleInfoWrapperPass(
    const LLVMTargetMachine *TM, MCContext *ExtContext)
    : ImmutablePass(ID), MMI(TM, ExtContext) {
  initializeMachineModuleInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

// Handle the Pass registration stuff necessary to use DataLayout's.
INITIALIZE_PASS(MachineModuleInfoWrapperPass, "machinemoduleinfo",
                "Machine Module Information", false, false)
char MachineModuleInfoWrapperPass::ID = 0;

static uint64_t getLocCookie(const SMDiagnostic &SMD, const SourceMgr &SrcMgr,
                             std::vector<const MDNode *> &LocInfos) {
  // Look up a LocInfo for the buffer this diagnostic is coming from.
  unsigned BufNum = SrcMgr.FindBufferContainingLoc(SMD.getLoc());
  const MDNode *LocInfo = nullptr;
  if (BufNum > 0 && BufNum <= LocInfos.size())
    LocInfo = LocInfos[BufNum - 1];

  // If the inline asm had metadata associated with it, pull out a location
  // cookie corresponding to which line the error occurred on.
  uint64_t LocCookie = 0;
  if (LocInfo) {
    unsigned ErrorLine = SMD.getLineNo() - 1;
    if (ErrorLine >= LocInfo->getNumOperands())
      ErrorLine = 0;

    if (LocInfo->getNumOperands() != 0)
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(LocInfo->getOperand(ErrorLine)))
        LocCookie = CI->getZExtValue();
  }

  return LocCookie;
}

bool MachineModuleInfoWrapperPass::doInitialization(Module &M) {
  MMI.initialize();
  MMI.TheModule = &M;
  LLVMContext &Ctx = M.getContext();
  MMI.getContext().setDiagnosticHandler(
      [&Ctx, &M](const SMDiagnostic &SMD, bool IsInlineAsm,
                 const SourceMgr &SrcMgr,
                 std::vector<const MDNode *> &LocInfos) {
        uint64_t LocCookie = 0;
        if (IsInlineAsm)
          LocCookie = getLocCookie(SMD, SrcMgr, LocInfos);
        Ctx.diagnose(
            DiagnosticInfoSrcMgr(SMD, M.getName(), IsInlineAsm, LocCookie));
      });
  MMI.DbgInfoAvailable = !DisableDebugInfoPrinting &&
                         !M.debug_compile_units().empty();
  return false;
}

bool MachineModuleInfoWrapperPass::doFinalization(Module &M) {
  MMI.finalize();
  return false;
}

AnalysisKey MachineModuleAnalysis::Key;

MachineModuleAnalysis::Result
MachineModuleAnalysis::run(Module &M, ModuleAnalysisManager &) {
  MMI.TheModule = &M;
  LLVMContext &Ctx = M.getContext();
  MMI.getContext().setDiagnosticHandler(
      [&Ctx, &M](const SMDiagnostic &SMD, bool IsInlineAsm,
                 const SourceMgr &SrcMgr,
                 std::vector<const MDNode *> &LocInfos) {
        unsigned LocCookie = 0;
        if (IsInlineAsm)
          LocCookie = getLocCookie(SMD, SrcMgr, LocInfos);
        Ctx.diagnose(
            DiagnosticInfoSrcMgr(SMD, M.getName(), IsInlineAsm, LocCookie));
      });
  MMI.DbgInfoAvailable =
      !DisableDebugInfoPrinting && !M.debug_compile_units().empty();
  return Result(MMI);
}
