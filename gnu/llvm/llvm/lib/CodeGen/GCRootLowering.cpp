//===-- GCRootLowering.cpp - Garbage collection infrastructure ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the lowering for the gc.root mechanism.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCContext.h"

using namespace llvm;

/// Lower barriers out of existence (if the associated GCStrategy hasn't
/// already done so...), and insert initializing stores to roots as a defensive
/// measure.  Given we're going to report all roots live at all safepoints, we
/// need to be able to ensure each root has been initialized by the point the
/// first safepoint is reached.  This really should have been done by the
/// frontend, but the old API made this non-obvious, so we do a potentially
/// redundant store just in case.
static bool DoLowering(Function &F, GCStrategy &S);

namespace {

/// LowerIntrinsics - This pass rewrites calls to the llvm.gcread or
/// llvm.gcwrite intrinsics, replacing them with simple loads and stores as
/// directed by the GCStrategy. It also performs automatic root initialization
/// and custom intrinsic lowering.
class LowerIntrinsics : public FunctionPass {
public:
  static char ID;

  LowerIntrinsics();
  StringRef getPassName() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;
};

/// GCMachineCodeAnalysis - This is a target-independent pass over the machine
/// function representation to identify safe points for the garbage collector
/// in the machine code. It inserts labels at safe points and populates a
/// GCMetadata record for each function.
class GCMachineCodeAnalysis : public MachineFunctionPass {
  GCFunctionInfo *FI = nullptr;
  const TargetInstrInfo *TII = nullptr;

  void FindSafePoints(MachineFunction &MF);
  void VisitCallPoint(MachineBasicBlock::iterator CI);
  MCSymbol *InsertLabel(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                        const DebugLoc &DL) const;

  void FindStackOffsets(MachineFunction &MF);

public:
  static char ID;

  GCMachineCodeAnalysis();
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &MF) override;
};
}

PreservedAnalyses GCLoweringPass::run(Function &F,
                                      FunctionAnalysisManager &FAM) {
  if (!F.hasGC())
    return PreservedAnalyses::all();

  auto &Info = FAM.getResult<GCFunctionAnalysis>(F);

  bool Changed = DoLowering(F, Info.getStrategy());

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

// -----------------------------------------------------------------------------

INITIALIZE_PASS_BEGIN(LowerIntrinsics, "gc-lowering", "GC Lowering", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(GCModuleInfo)
INITIALIZE_PASS_END(LowerIntrinsics, "gc-lowering", "GC Lowering", false, false)

FunctionPass *llvm::createGCLoweringPass() { return new LowerIntrinsics(); }

char LowerIntrinsics::ID = 0;
char &llvm::GCLoweringID = LowerIntrinsics::ID;

LowerIntrinsics::LowerIntrinsics() : FunctionPass(ID) {
  initializeLowerIntrinsicsPass(*PassRegistry::getPassRegistry());
}

StringRef LowerIntrinsics::getPassName() const {
  return "Lower Garbage Collection Instructions";
}

void LowerIntrinsics::getAnalysisUsage(AnalysisUsage &AU) const {
  FunctionPass::getAnalysisUsage(AU);
  AU.addRequired<GCModuleInfo>();
  AU.addPreserved<DominatorTreeWrapperPass>();
}

/// doInitialization - If this module uses the GC intrinsics, find them now.
bool LowerIntrinsics::doInitialization(Module &M) {
  GCModuleInfo *MI = getAnalysisIfAvailable<GCModuleInfo>();
  assert(MI && "LowerIntrinsics didn't require GCModuleInfo!?");
  for (Function &F : M)
    if (!F.isDeclaration() && F.hasGC())
      MI->getFunctionInfo(F); // Instantiate the GC strategy.

  return false;
}

/// CouldBecomeSafePoint - Predicate to conservatively determine whether the
/// instruction could introduce a safe point.
static bool CouldBecomeSafePoint(Instruction *I) {
  // The natural definition of instructions which could introduce safe points
  // are:
  //
  //   - call, invoke (AfterCall, BeforeCall)
  //   - phis (Loops)
  //   - invoke, ret, unwind (Exit)
  //
  // However, instructions as seemingly inoccuous as arithmetic can become
  // libcalls upon lowering (e.g., div i64 on a 32-bit platform), so instead
  // it is necessary to take a conservative approach.

  if (isa<AllocaInst>(I) || isa<GetElementPtrInst>(I) || isa<StoreInst>(I) ||
      isa<LoadInst>(I))
    return false;

  // llvm.gcroot is safe because it doesn't do anything at runtime.
  if (CallInst *CI = dyn_cast<CallInst>(I))
    if (Function *F = CI->getCalledFunction())
      if (Intrinsic::ID IID = F->getIntrinsicID())
        if (IID == Intrinsic::gcroot)
          return false;

  return true;
}

static bool InsertRootInitializers(Function &F, ArrayRef<AllocaInst *> Roots) {
  // Scroll past alloca instructions.
  BasicBlock::iterator IP = F.getEntryBlock().begin();
  while (isa<AllocaInst>(IP))
    ++IP;

  // Search for initializers in the initial BB.
  SmallPtrSet<AllocaInst *, 16> InitedRoots;
  for (; !CouldBecomeSafePoint(&*IP); ++IP)
    if (StoreInst *SI = dyn_cast<StoreInst>(IP))
      if (AllocaInst *AI =
              dyn_cast<AllocaInst>(SI->getOperand(1)->stripPointerCasts()))
        InitedRoots.insert(AI);

  // Add root initializers.
  bool MadeChange = false;

  for (AllocaInst *Root : Roots)
    if (!InitedRoots.count(Root)) {
      new StoreInst(
          ConstantPointerNull::get(cast<PointerType>(Root->getAllocatedType())),
          Root, std::next(Root->getIterator()));
      MadeChange = true;
    }

  return MadeChange;
}

/// runOnFunction - Replace gcread/gcwrite intrinsics with loads and stores.
/// Leave gcroot intrinsics; the code generator needs to see those.
bool LowerIntrinsics::runOnFunction(Function &F) {
  // Quick exit for functions that do not use GC.
  if (!F.hasGC())
    return false;

  GCFunctionInfo &FI = getAnalysis<GCModuleInfo>().getFunctionInfo(F);
  GCStrategy &S = FI.getStrategy();

  return DoLowering(F, S);
}

bool DoLowering(Function &F, GCStrategy &S) {
  SmallVector<AllocaInst *, 32> Roots;

  bool MadeChange = false;
  for (BasicBlock &BB : F)
    for (Instruction &I : llvm::make_early_inc_range(BB)) {
      IntrinsicInst *CI = dyn_cast<IntrinsicInst>(&I);
      if (!CI)
        continue;

      Function *F = CI->getCalledFunction();
      switch (F->getIntrinsicID()) {
      default: break;
      case Intrinsic::gcwrite: {
        // Replace a write barrier with a simple store.
        Value *St = new StoreInst(CI->getArgOperand(0), CI->getArgOperand(2),
                                  CI->getIterator());
        CI->replaceAllUsesWith(St);
        CI->eraseFromParent();
        MadeChange = true;
        break;
      }
      case Intrinsic::gcread: {
        // Replace a read barrier with a simple load.
        Value *Ld = new LoadInst(CI->getType(), CI->getArgOperand(1), "",
                                 CI->getIterator());
        Ld->takeName(CI);
        CI->replaceAllUsesWith(Ld);
        CI->eraseFromParent();
        MadeChange = true;
        break;
      }
      case Intrinsic::gcroot: {
        // Initialize the GC root, but do not delete the intrinsic. The
        // backend needs the intrinsic to flag the stack slot.
        Roots.push_back(
            cast<AllocaInst>(CI->getArgOperand(0)->stripPointerCasts()));
        break;
      }
      }
    }

  if (Roots.size())
    MadeChange |= InsertRootInitializers(F, Roots);

  return MadeChange;
}

// -----------------------------------------------------------------------------

char GCMachineCodeAnalysis::ID = 0;
char &llvm::GCMachineCodeAnalysisID = GCMachineCodeAnalysis::ID;

INITIALIZE_PASS(GCMachineCodeAnalysis, "gc-analysis",
                "Analyze Machine Code For Garbage Collection", false, false)

GCMachineCodeAnalysis::GCMachineCodeAnalysis() : MachineFunctionPass(ID) {}

void GCMachineCodeAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
  AU.setPreservesAll();
  AU.addRequired<GCModuleInfo>();
}

MCSymbol *GCMachineCodeAnalysis::InsertLabel(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator MI,
                                             const DebugLoc &DL) const {
  MCSymbol *Label = MBB.getParent()->getContext().createTempSymbol();
  BuildMI(MBB, MI, DL, TII->get(TargetOpcode::GC_LABEL)).addSym(Label);
  return Label;
}

void GCMachineCodeAnalysis::VisitCallPoint(MachineBasicBlock::iterator CI) {
  // Find the return address (next instruction), since that's what will be on
  // the stack when the call is suspended and we need to inspect the stack.
  MachineBasicBlock::iterator RAI = CI;
  ++RAI;

  MCSymbol *Label = InsertLabel(*CI->getParent(), RAI, CI->getDebugLoc());
  FI->addSafePoint(Label, CI->getDebugLoc());
}

void GCMachineCodeAnalysis::FindSafePoints(MachineFunction &MF) {
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.isCall()) {
        // Do not treat tail or sibling call sites as safe points.  This is
        // legal since any arguments passed to the callee which live in the
        // remnants of the callers frame will be owned and updated by the
        // callee if required.
        if (MI.isTerminator())
          continue;
        VisitCallPoint(&MI);
      }
}

void GCMachineCodeAnalysis::FindStackOffsets(MachineFunction &MF) {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  assert(TFI && "TargetRegisterInfo not available!");

  for (GCFunctionInfo::roots_iterator RI = FI->roots_begin();
       RI != FI->roots_end();) {
    // If the root references a dead object, no need to keep it.
    if (MF.getFrameInfo().isDeadObjectIndex(RI->Num)) {
      RI = FI->removeStackRoot(RI);
    } else {
      Register FrameReg; // FIXME: surely GCRoot ought to store the
                         // register that the offset is from?
      auto FrameOffset = TFI->getFrameIndexReference(MF, RI->Num, FrameReg);
      assert(!FrameOffset.getScalable() &&
             "Frame offsets with a scalable component are not supported");
      RI->StackOffset = FrameOffset.getFixed();
      ++RI;
    }
  }
}

bool GCMachineCodeAnalysis::runOnMachineFunction(MachineFunction &MF) {
  // Quick exit for functions that do not use GC.
  if (!MF.getFunction().hasGC())
    return false;

  FI = &getAnalysis<GCModuleInfo>().getFunctionInfo(MF.getFunction());
  TII = MF.getSubtarget().getInstrInfo();

  // Find the size of the stack frame.  There may be no correct static frame
  // size, we use UINT64_MAX to represent this.
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  const bool DynamicFrameSize =
      MFI.hasVarSizedObjects() || RegInfo->hasStackRealignment(MF);
  FI->setFrameSize(DynamicFrameSize ? UINT64_MAX : MFI.getStackSize());

  // Find all safe points.
  if (FI->getStrategy().needsSafePoints())
    FindSafePoints(MF);

  // Find the concrete stack offsets for all roots (stack slots)
  FindStackOffsets(MF);

  return false;
}
