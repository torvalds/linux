//===- StackProtector.cpp - Stack Protector Insertion ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass inserts stack protectors into functions which need them. A variable
// with a random value in it is stored onto the stack before the local variables
// are allocated. Upon exiting the block, the stored value is checked. If it's
// changed, then there was some sort of violation and the program aborts.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/StackProtector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "stack-protector"

STATISTIC(NumFunProtected, "Number of functions protected");
STATISTIC(NumAddrTaken, "Number of local variables that have their address"
                        " taken.");

static cl::opt<bool> EnableSelectionDAGSP("enable-selectiondag-sp",
                                          cl::init(true), cl::Hidden);
static cl::opt<bool> DisableCheckNoReturn("disable-check-noreturn-call",
                                          cl::init(false), cl::Hidden);

/// InsertStackProtectors - Insert code into the prologue and epilogue of the
/// function.
///
///  - The prologue code loads and stores the stack guard onto the stack.
///  - The epilogue checks the value stored in the prologue against the original
///    value. It calls __stack_chk_fail if they differ.
static bool InsertStackProtectors(const TargetMachine *TM, Function *F,
                                  DomTreeUpdater *DTU, bool &HasPrologue,
                                  bool &HasIRCheck);

/// CreateFailBB - Create a basic block to jump to when the stack protector
/// check fails.
static BasicBlock *CreateFailBB(Function *F, const Triple &Trip);

bool SSPLayoutInfo::shouldEmitSDCheck(const BasicBlock &BB) const {
  return HasPrologue && !HasIRCheck && isa<ReturnInst>(BB.getTerminator());
}

void SSPLayoutInfo::copyToMachineFrameInfo(MachineFrameInfo &MFI) const {
  if (Layout.empty())
    return;

  for (int I = 0, E = MFI.getObjectIndexEnd(); I != E; ++I) {
    if (MFI.isDeadObjectIndex(I))
      continue;

    const AllocaInst *AI = MFI.getObjectAllocation(I);
    if (!AI)
      continue;

    SSPLayoutMap::const_iterator LI = Layout.find(AI);
    if (LI == Layout.end())
      continue;

    MFI.setObjectSSPLayout(I, LI->second);
  }
}

SSPLayoutInfo SSPLayoutAnalysis::run(Function &F,
                                     FunctionAnalysisManager &FAM) {

  SSPLayoutInfo Info;
  Info.RequireStackProtector =
      SSPLayoutAnalysis::requiresStackProtector(&F, &Info.Layout);
  Info.SSPBufferSize = F.getFnAttributeAsParsedInteger(
      "stack-protector-buffer-size", SSPLayoutInfo::DefaultSSPBufferSize);
  return Info;
}

AnalysisKey SSPLayoutAnalysis::Key;

PreservedAnalyses StackProtectorPass::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  auto &Info = FAM.getResult<SSPLayoutAnalysis>(F);
  auto *DT = FAM.getCachedResult<DominatorTreeAnalysis>(F);
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);

  if (!Info.RequireStackProtector)
    return PreservedAnalyses::all();

  // TODO(etienneb): Functions with funclets are not correctly supported now.
  // Do nothing if this is funclet-based personality.
  if (F.hasPersonalityFn()) {
    EHPersonality Personality = classifyEHPersonality(F.getPersonalityFn());
    if (isFuncletEHPersonality(Personality))
      return PreservedAnalyses::all();
  }

  ++NumFunProtected;
  bool Changed = InsertStackProtectors(TM, &F, DT ? &DTU : nullptr,
                                       Info.HasPrologue, Info.HasIRCheck);
#ifdef EXPENSIVE_CHECKS
  assert((!DT || DT->verify(DominatorTree::VerificationLevel::Full)) &&
         "Failed to maintain validity of domtree!");
#endif

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<SSPLayoutAnalysis>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

char StackProtector::ID = 0;

StackProtector::StackProtector() : FunctionPass(ID) {
  initializeStackProtectorPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(StackProtector, DEBUG_TYPE,
                      "Insert stack protectors", false, true)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(StackProtector, DEBUG_TYPE,
                    "Insert stack protectors", false, true)

FunctionPass *llvm::createStackProtectorPass() { return new StackProtector(); }

void StackProtector::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.addPreserved<DominatorTreeWrapperPass>();
}

bool StackProtector::runOnFunction(Function &Fn) {
  F = &Fn;
  M = F->getParent();
  if (auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>())
    DTU.emplace(DTWP->getDomTree(), DomTreeUpdater::UpdateStrategy::Lazy);
  TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
  LayoutInfo.HasPrologue = false;
  LayoutInfo.HasIRCheck = false;

  LayoutInfo.SSPBufferSize = Fn.getFnAttributeAsParsedInteger(
      "stack-protector-buffer-size", SSPLayoutInfo::DefaultSSPBufferSize);
  if (!requiresStackProtector(F, &LayoutInfo.Layout))
    return false;

  // TODO(etienneb): Functions with funclets are not correctly supported now.
  // Do nothing if this is funclet-based personality.
  if (Fn.hasPersonalityFn()) {
    EHPersonality Personality = classifyEHPersonality(Fn.getPersonalityFn());
    if (isFuncletEHPersonality(Personality))
      return false;
  }

  ++NumFunProtected;
  bool Changed =
      InsertStackProtectors(TM, F, DTU ? &*DTU : nullptr,
                            LayoutInfo.HasPrologue, LayoutInfo.HasIRCheck);
#ifdef EXPENSIVE_CHECKS
  assert((!DTU ||
          DTU->getDomTree().verify(DominatorTree::VerificationLevel::Full)) &&
         "Failed to maintain validity of domtree!");
#endif
  DTU.reset();
  return Changed;
}

/// \param [out] IsLarge is set to true if a protectable array is found and
/// it is "large" ( >= ssp-buffer-size).  In the case of a structure with
/// multiple arrays, this gets set if any of them is large.
static bool ContainsProtectableArray(Type *Ty, Module *M, unsigned SSPBufferSize,
                                     bool &IsLarge, bool Strong,
                                     bool InStruct) {
  if (!Ty)
    return false;
  if (ArrayType *AT = dyn_cast<ArrayType>(Ty)) {
    if (!AT->getElementType()->isIntegerTy(8)) {
      // If we're on a non-Darwin platform or we're inside of a structure, don't
      // add stack protectors unless the array is a character array.
      // However, in strong mode any array, regardless of type and size,
      // triggers a protector.
      if (!Strong && (InStruct || !Triple(M->getTargetTriple()).isOSDarwin()))
        return false;
    }

    // If an array has more than SSPBufferSize bytes of allocated space, then we
    // emit stack protectors.
    if (SSPBufferSize <= M->getDataLayout().getTypeAllocSize(AT)) {
      IsLarge = true;
      return true;
    }

    if (Strong)
      // Require a protector for all arrays in strong mode
      return true;
  }

  const StructType *ST = dyn_cast<StructType>(Ty);
  if (!ST)
    return false;

  bool NeedsProtector = false;
  for (Type *ET : ST->elements())
    if (ContainsProtectableArray(ET, M, SSPBufferSize, IsLarge, Strong, true)) {
      // If the element is a protectable array and is large (>= SSPBufferSize)
      // then we are done.  If the protectable array is not large, then
      // keep looking in case a subsequent element is a large array.
      if (IsLarge)
        return true;
      NeedsProtector = true;
    }

  return NeedsProtector;
}

/// Check whether a stack allocation has its address taken.
static bool HasAddressTaken(const Instruction *AI, TypeSize AllocSize,
                            Module *M,
                            SmallPtrSet<const PHINode *, 16> &VisitedPHIs) {
  const DataLayout &DL = M->getDataLayout();
  for (const User *U : AI->users()) {
    const auto *I = cast<Instruction>(U);
    // If this instruction accesses memory make sure it doesn't access beyond
    // the bounds of the allocated object.
    std::optional<MemoryLocation> MemLoc = MemoryLocation::getOrNone(I);
    if (MemLoc && MemLoc->Size.hasValue() &&
        !TypeSize::isKnownGE(AllocSize, MemLoc->Size.getValue()))
      return true;
    switch (I->getOpcode()) {
    case Instruction::Store:
      if (AI == cast<StoreInst>(I)->getValueOperand())
        return true;
      break;
    case Instruction::AtomicCmpXchg:
      // cmpxchg conceptually includes both a load and store from the same
      // location. So, like store, the value being stored is what matters.
      if (AI == cast<AtomicCmpXchgInst>(I)->getNewValOperand())
        return true;
      break;
    case Instruction::PtrToInt:
      if (AI == cast<PtrToIntInst>(I)->getOperand(0))
        return true;
      break;
    case Instruction::Call: {
      // Ignore intrinsics that do not become real instructions.
      // TODO: Narrow this to intrinsics that have store-like effects.
      const auto *CI = cast<CallInst>(I);
      if (!CI->isDebugOrPseudoInst() && !CI->isLifetimeStartOrEnd())
        return true;
      break;
    }
    case Instruction::Invoke:
      return true;
    case Instruction::GetElementPtr: {
      // If the GEP offset is out-of-bounds, or is non-constant and so has to be
      // assumed to be potentially out-of-bounds, then any memory access that
      // would use it could also be out-of-bounds meaning stack protection is
      // required.
      const GetElementPtrInst *GEP = cast<GetElementPtrInst>(I);
      unsigned IndexSize = DL.getIndexTypeSizeInBits(I->getType());
      APInt Offset(IndexSize, 0);
      if (!GEP->accumulateConstantOffset(DL, Offset))
        return true;
      TypeSize OffsetSize = TypeSize::getFixed(Offset.getLimitedValue());
      if (!TypeSize::isKnownGT(AllocSize, OffsetSize))
        return true;
      // Adjust AllocSize to be the space remaining after this offset.
      // We can't subtract a fixed size from a scalable one, so in that case
      // assume the scalable value is of minimum size.
      TypeSize NewAllocSize =
          TypeSize::getFixed(AllocSize.getKnownMinValue()) - OffsetSize;
      if (HasAddressTaken(I, NewAllocSize, M, VisitedPHIs))
        return true;
      break;
    }
    case Instruction::BitCast:
    case Instruction::Select:
    case Instruction::AddrSpaceCast:
      if (HasAddressTaken(I, AllocSize, M, VisitedPHIs))
        return true;
      break;
    case Instruction::PHI: {
      // Keep track of what PHI nodes we have already visited to ensure
      // they are only visited once.
      const auto *PN = cast<PHINode>(I);
      if (VisitedPHIs.insert(PN).second)
        if (HasAddressTaken(PN, AllocSize, M, VisitedPHIs))
          return true;
      break;
    }
    case Instruction::Load:
    case Instruction::AtomicRMW:
    case Instruction::Ret:
      // These instructions take an address operand, but have load-like or
      // other innocuous behavior that should not trigger a stack protector.
      // atomicrmw conceptually has both load and store semantics, but the
      // value being stored must be integer; so if a pointer is being stored,
      // we'll catch it in the PtrToInt case above.
      break;
    default:
      // Conservatively return true for any instruction that takes an address
      // operand, but is not handled above.
      return true;
    }
  }
  return false;
}

/// Search for the first call to the llvm.stackprotector intrinsic and return it
/// if present.
static const CallInst *findStackProtectorIntrinsic(Function &F) {
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      if (const auto *II = dyn_cast<IntrinsicInst>(&I))
        if (II->getIntrinsicID() == Intrinsic::stackprotector)
          return II;
  return nullptr;
}

/// Check whether or not this function needs a stack protector based
/// upon the stack protector level.
///
/// We use two heuristics: a standard (ssp) and strong (sspstrong).
/// The standard heuristic which will add a guard variable to functions that
/// call alloca with a either a variable size or a size >= SSPBufferSize,
/// functions with character buffers larger than SSPBufferSize, and functions
/// with aggregates containing character buffers larger than SSPBufferSize. The
/// strong heuristic will add a guard variables to functions that call alloca
/// regardless of size, functions with any buffer regardless of type and size,
/// functions with aggregates that contain any buffer regardless of type and
/// size, and functions that contain stack-based variables that have had their
/// address taken.
bool SSPLayoutAnalysis::requiresStackProtector(Function *F,
                                               SSPLayoutMap *Layout) {
  Module *M = F->getParent();
  bool Strong = false;
  bool NeedsProtector = false;

  // The set of PHI nodes visited when determining if a variable's reference has
  // been taken.  This set is maintained to ensure we don't visit the same PHI
  // node multiple times.
  SmallPtrSet<const PHINode *, 16> VisitedPHIs;

  unsigned SSPBufferSize = F->getFnAttributeAsParsedInteger(
      "stack-protector-buffer-size", SSPLayoutInfo::DefaultSSPBufferSize);

  if (F->hasFnAttribute(Attribute::SafeStack))
    return false;

  // We are constructing the OptimizationRemarkEmitter on the fly rather than
  // using the analysis pass to avoid building DominatorTree and LoopInfo which
  // are not available this late in the IR pipeline.
  OptimizationRemarkEmitter ORE(F);

  if (F->hasFnAttribute(Attribute::StackProtectReq)) {
    if (!Layout)
      return true;
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "StackProtectorRequested", F)
             << "Stack protection applied to function "
             << ore::NV("Function", F)
             << " due to a function attribute or command-line switch";
    });
    NeedsProtector = true;
    Strong = true; // Use the same heuristic as strong to determine SSPLayout
  } else if (F->hasFnAttribute(Attribute::StackProtectStrong))
    Strong = true;
  else if (!F->hasFnAttribute(Attribute::StackProtect))
    return false;

  for (const BasicBlock &BB : *F) {
    for (const Instruction &I : BB) {
      if (const AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
        if (AI->isArrayAllocation()) {
          auto RemarkBuilder = [&]() {
            return OptimizationRemark(DEBUG_TYPE, "StackProtectorAllocaOrArray",
                                      &I)
                   << "Stack protection applied to function "
                   << ore::NV("Function", F)
                   << " due to a call to alloca or use of a variable length "
                      "array";
          };
          if (const auto *CI = dyn_cast<ConstantInt>(AI->getArraySize())) {
            if (CI->getLimitedValue(SSPBufferSize) >= SSPBufferSize) {
              // A call to alloca with size >= SSPBufferSize requires
              // stack protectors.
              if (!Layout)
                return true;
              Layout->insert(
                  std::make_pair(AI, MachineFrameInfo::SSPLK_LargeArray));
              ORE.emit(RemarkBuilder);
              NeedsProtector = true;
            } else if (Strong) {
              // Require protectors for all alloca calls in strong mode.
              if (!Layout)
                return true;
              Layout->insert(
                  std::make_pair(AI, MachineFrameInfo::SSPLK_SmallArray));
              ORE.emit(RemarkBuilder);
              NeedsProtector = true;
            }
          } else {
            // A call to alloca with a variable size requires protectors.
            if (!Layout)
              return true;
            Layout->insert(
                std::make_pair(AI, MachineFrameInfo::SSPLK_LargeArray));
            ORE.emit(RemarkBuilder);
            NeedsProtector = true;
          }
          continue;
        }

        bool IsLarge = false;
        if (ContainsProtectableArray(AI->getAllocatedType(), M, SSPBufferSize,
                                     IsLarge, Strong, false)) {
          if (!Layout)
            return true;
          Layout->insert(std::make_pair(
              AI, IsLarge ? MachineFrameInfo::SSPLK_LargeArray
                          : MachineFrameInfo::SSPLK_SmallArray));
          ORE.emit([&]() {
            return OptimizationRemark(DEBUG_TYPE, "StackProtectorBuffer", &I)
                   << "Stack protection applied to function "
                   << ore::NV("Function", F)
                   << " due to a stack allocated buffer or struct containing a "
                      "buffer";
          });
          NeedsProtector = true;
          continue;
        }

        if (Strong &&
            HasAddressTaken(
                AI, M->getDataLayout().getTypeAllocSize(AI->getAllocatedType()),
                M, VisitedPHIs)) {
          ++NumAddrTaken;
          if (!Layout)
            return true;
          Layout->insert(std::make_pair(AI, MachineFrameInfo::SSPLK_AddrOf));
          ORE.emit([&]() {
            return OptimizationRemark(DEBUG_TYPE, "StackProtectorAddressTaken",
                                      &I)
                   << "Stack protection applied to function "
                   << ore::NV("Function", F)
                   << " due to the address of a local variable being taken";
          });
          NeedsProtector = true;
        }
        // Clear any PHIs that we visited, to make sure we examine all uses of
        // any subsequent allocas that we look at.
        VisitedPHIs.clear();
      }
    }
  }

  return NeedsProtector;
}

/// Create a stack guard loading and populate whether SelectionDAG SSP is
/// supported.
static Value *getStackGuard(const TargetLoweringBase *TLI, Module *M,
                            IRBuilder<> &B,
                            bool *SupportsSelectionDAGSP = nullptr) {
  Value *Guard = TLI->getIRStackGuard(B);
  StringRef GuardMode = M->getStackProtectorGuard();
  if ((GuardMode == "tls" || GuardMode.empty()) && Guard)
    return B.CreateLoad(B.getPtrTy(), Guard, true, "StackGuard");

  // Use SelectionDAG SSP handling, since there isn't an IR guard.
  //
  // This is more or less weird, since we optionally output whether we
  // should perform a SelectionDAG SP here. The reason is that it's strictly
  // defined as !TLI->getIRStackGuard(B), where getIRStackGuard is also
  // mutating. There is no way to get this bit without mutating the IR, so
  // getting this bit has to happen in this right time.
  //
  // We could have define a new function TLI::supportsSelectionDAGSP(), but that
  // will put more burden on the backends' overriding work, especially when it
  // actually conveys the same information getIRStackGuard() already gives.
  if (SupportsSelectionDAGSP)
    *SupportsSelectionDAGSP = true;
  TLI->insertSSPDeclarations(*M);
  return B.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::stackguard));
}

/// Insert code into the entry block that stores the stack guard
/// variable onto the stack:
///
///   entry:
///     StackGuardSlot = alloca i8*
///     StackGuard = <stack guard>
///     call void @llvm.stackprotector(StackGuard, StackGuardSlot)
///
/// Returns true if the platform/triple supports the stackprotectorcreate pseudo
/// node.
static bool CreatePrologue(Function *F, Module *M, Instruction *CheckLoc,
                           const TargetLoweringBase *TLI, AllocaInst *&AI) {
  bool SupportsSelectionDAGSP = false;
  IRBuilder<> B(&F->getEntryBlock().front());
  PointerType *PtrTy = PointerType::getUnqual(CheckLoc->getContext());
  AI = B.CreateAlloca(PtrTy, nullptr, "StackGuardSlot");

  Value *GuardSlot = getStackGuard(TLI, M, B, &SupportsSelectionDAGSP);
  B.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::stackprotector),
               {GuardSlot, AI});
  return SupportsSelectionDAGSP;
}

bool InsertStackProtectors(const TargetMachine *TM, Function *F,
                           DomTreeUpdater *DTU, bool &HasPrologue,
                           bool &HasIRCheck) {
  auto *M = F->getParent();
  auto *TLI = TM->getSubtargetImpl(*F)->getTargetLowering();

  // If the target wants to XOR the frame pointer into the guard value, it's
  // impossible to emit the check in IR, so the target *must* support stack
  // protection in SDAG.
  bool SupportsSelectionDAGSP =
      TLI->useStackGuardXorFP() ||
      (EnableSelectionDAGSP && !TM->Options.EnableFastISel);
  AllocaInst *AI = nullptr; // Place on stack that stores the stack guard.
  BasicBlock *FailBB = nullptr;

  for (BasicBlock &BB : llvm::make_early_inc_range(*F)) {
    // This is stack protector auto generated check BB, skip it.
    if (&BB == FailBB)
      continue;
    Instruction *CheckLoc = dyn_cast<ReturnInst>(BB.getTerminator());
    if (!CheckLoc && !DisableCheckNoReturn)
      for (auto &Inst : BB)
        if (auto *CB = dyn_cast<CallBase>(&Inst))
          // Do stack check before noreturn calls that aren't nounwind (e.g:
          // __cxa_throw).
          if (CB->doesNotReturn() && !CB->doesNotThrow()) {
            CheckLoc = CB;
            break;
          }

    if (!CheckLoc)
      continue;

    // Generate prologue instrumentation if not already generated.
    if (!HasPrologue) {
      HasPrologue = true;
      SupportsSelectionDAGSP &= CreatePrologue(F, M, CheckLoc, TLI, AI);
    }

    // SelectionDAG based code generation. Nothing else needs to be done here.
    // The epilogue instrumentation is postponed to SelectionDAG.
    if (SupportsSelectionDAGSP)
      break;

    // Find the stack guard slot if the prologue was not created by this pass
    // itself via a previous call to CreatePrologue().
    if (!AI) {
      const CallInst *SPCall = findStackProtectorIntrinsic(*F);
      assert(SPCall && "Call to llvm.stackprotector is missing");
      AI = cast<AllocaInst>(SPCall->getArgOperand(1));
    }

    // Set HasIRCheck to true, so that SelectionDAG will not generate its own
    // version. SelectionDAG called 'shouldEmitSDCheck' to check whether
    // instrumentation has already been generated.
    HasIRCheck = true;

    // If we're instrumenting a block with a tail call, the check has to be
    // inserted before the call rather than between it and the return. The
    // verifier guarantees that a tail call is either directly before the
    // return or with a single correct bitcast of the return value in between so
    // we don't need to worry about many situations here.
    Instruction *Prev = CheckLoc->getPrevNonDebugInstruction();
    if (Prev && isa<CallInst>(Prev) && cast<CallInst>(Prev)->isTailCall())
      CheckLoc = Prev;
    else if (Prev) {
      Prev = Prev->getPrevNonDebugInstruction();
      if (Prev && isa<CallInst>(Prev) && cast<CallInst>(Prev)->isTailCall())
        CheckLoc = Prev;
    }

    // Generate epilogue instrumentation. The epilogue intrumentation can be
    // function-based or inlined depending on which mechanism the target is
    // providing.
    if (Function *GuardCheck = TLI->getSSPStackGuardCheck(*M)) {
      // Generate the function-based epilogue instrumentation.
      // The target provides a guard check function, generate a call to it.
      IRBuilder<> B(CheckLoc);
      LoadInst *Guard = B.CreateLoad(B.getPtrTy(), AI, true, "Guard");
      CallInst *Call = B.CreateCall(GuardCheck, {Guard});
      Call->setAttributes(GuardCheck->getAttributes());
      Call->setCallingConv(GuardCheck->getCallingConv());
    } else {
      // Generate the epilogue with inline instrumentation.
      // If we do not support SelectionDAG based calls, generate IR level
      // calls.
      //
      // For each block with a return instruction, convert this:
      //
      //   return:
      //     ...
      //     ret ...
      //
      // into this:
      //
      //   return:
      //     ...
      //     %1 = <stack guard>
      //     %2 = load StackGuardSlot
      //     %3 = icmp ne i1 %1, %2
      //     br i1 %3, label %CallStackCheckFailBlk, label %SP_return
      //
      //   SP_return:
      //     ret ...
      //
      //   CallStackCheckFailBlk:
      //     call void @__stack_chk_fail()
      //     unreachable

      // Create the FailBB. We duplicate the BB every time since the MI tail
      // merge pass will merge together all of the various BB into one including
      // fail BB generated by the stack protector pseudo instruction.
      if (!FailBB)
        FailBB = CreateFailBB(F, TM->getTargetTriple());

      IRBuilder<> B(CheckLoc);
      Value *Guard = getStackGuard(TLI, M, B);
      LoadInst *LI2 = B.CreateLoad(B.getPtrTy(), AI, true);
      auto *Cmp = cast<ICmpInst>(B.CreateICmpNE(Guard, LI2));
      auto SuccessProb =
          BranchProbabilityInfo::getBranchProbStackProtector(true);
      auto FailureProb =
          BranchProbabilityInfo::getBranchProbStackProtector(false);
      MDNode *Weights = MDBuilder(F->getContext())
                            .createBranchWeights(FailureProb.getNumerator(),
                                                 SuccessProb.getNumerator());

      SplitBlockAndInsertIfThen(Cmp, CheckLoc,
                                /*Unreachable=*/false, Weights, DTU,
                                /*LI=*/nullptr, /*ThenBlock=*/FailBB);

      auto *BI = cast<BranchInst>(Cmp->getParent()->getTerminator());
      BasicBlock *NewBB = BI->getSuccessor(1);
      NewBB->setName("SP_return");
      NewBB->moveAfter(&BB);

      Cmp->setPredicate(Cmp->getInversePredicate());
      BI->swapSuccessors();
    }
  }

  // Return if we didn't modify any basic blocks. i.e., there are no return
  // statements in the function.
  return HasPrologue;
}

BasicBlock *CreateFailBB(Function *F, const Triple &Trip) {
  auto *M = F->getParent();
  LLVMContext &Context = F->getContext();
  BasicBlock *FailBB = BasicBlock::Create(Context, "CallStackCheckFailBlk", F);
  IRBuilder<> B(FailBB);
  if (F->getSubprogram())
    B.SetCurrentDebugLocation(
        DILocation::get(Context, 0, 0, F->getSubprogram()));
  FunctionCallee StackChkFail;
  SmallVector<Value *, 1> Args;
  if (Trip.isOSOpenBSD()) {
    StackChkFail = M->getOrInsertFunction("__stack_smash_handler",
                                          Type::getVoidTy(Context),
                                          PointerType::getUnqual(Context));
    Args.push_back(B.CreateGlobalStringPtr(F->getName(), "SSH"));
  } else {
    StackChkFail =
        M->getOrInsertFunction("__stack_chk_fail", Type::getVoidTy(Context));
  }
  cast<Function>(StackChkFail.getCallee())->addFnAttr(Attribute::NoReturn);
  B.CreateCall(StackChkFail, Args);
  B.CreateUnreachable();
  return FailBB;
}
