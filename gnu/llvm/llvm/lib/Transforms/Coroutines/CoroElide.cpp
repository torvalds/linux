//===- CoroElide.cpp - Coroutine Frame Allocation Elision Pass ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Coroutines/CoroElide.h"
#include "CoroInternal.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "coro-elide"

STATISTIC(NumOfCoroElided, "The # of coroutine get elided.");

#ifndef NDEBUG
static cl::opt<std::string> CoroElideInfoOutputFilename(
    "coro-elide-info-output-file", cl::value_desc("filename"),
    cl::desc("File to record the coroutines got elided"), cl::Hidden);
#endif

namespace {
// Created on demand if the coro-elide pass has work to do.
class FunctionElideInfo {
public:
  FunctionElideInfo(Function *F) : ContainingFunction(F) {
    this->collectPostSplitCoroIds();
  }

  bool hasCoroIds() const { return !CoroIds.empty(); }

  const SmallVectorImpl<CoroIdInst *> &getCoroIds() const { return CoroIds; }

private:
  Function *ContainingFunction;
  SmallVector<CoroIdInst *, 4> CoroIds;
  // Used in canCoroBeginEscape to distinguish coro.suspend switchs.
  SmallPtrSet<const SwitchInst *, 4> CoroSuspendSwitches;

  void collectPostSplitCoroIds();
  friend class CoroIdElider;
};

class CoroIdElider {
public:
  CoroIdElider(CoroIdInst *CoroId, FunctionElideInfo &FEI, AAResults &AA,
               DominatorTree &DT, OptimizationRemarkEmitter &ORE);
  void elideHeapAllocations(uint64_t FrameSize, Align FrameAlign);
  bool lifetimeEligibleForElide() const;
  bool attemptElide();
  bool canCoroBeginEscape(const CoroBeginInst *,
                          const SmallPtrSetImpl<BasicBlock *> &) const;

private:
  CoroIdInst *CoroId;
  FunctionElideInfo &FEI;
  AAResults &AA;
  DominatorTree &DT;
  OptimizationRemarkEmitter &ORE;

  SmallVector<CoroBeginInst *, 1> CoroBegins;
  SmallVector<CoroAllocInst *, 1> CoroAllocs;
  SmallVector<CoroSubFnInst *, 4> ResumeAddr;
  DenseMap<CoroBeginInst *, SmallVector<CoroSubFnInst *, 4>> DestroyAddr;
};
} // end anonymous namespace

// Go through the list of coro.subfn.addr intrinsics and replace them with the
// provided constant.
static void replaceWithConstant(Constant *Value,
                                SmallVectorImpl<CoroSubFnInst *> &Users) {
  if (Users.empty())
    return;

  // See if we need to bitcast the constant to match the type of the intrinsic
  // being replaced. Note: All coro.subfn.addr intrinsics return the same type,
  // so we only need to examine the type of the first one in the list.
  Type *IntrTy = Users.front()->getType();
  Type *ValueTy = Value->getType();
  if (ValueTy != IntrTy) {
    // May need to tweak the function type to match the type expected at the
    // use site.
    assert(ValueTy->isPointerTy() && IntrTy->isPointerTy());
    Value = ConstantExpr::getBitCast(Value, IntrTy);
  }

  // Now the value type matches the type of the intrinsic. Replace them all!
  for (CoroSubFnInst *I : Users)
    replaceAndRecursivelySimplify(I, Value);
}

// See if any operand of the call instruction references the coroutine frame.
static bool operandReferences(CallInst *CI, AllocaInst *Frame, AAResults &AA) {
  for (Value *Op : CI->operand_values())
    if (!AA.isNoAlias(Op, Frame))
      return true;
  return false;
}

// Look for any tail calls referencing the coroutine frame and remove tail
// attribute from them, since now coroutine frame resides on the stack and tail
// call implies that the function does not references anything on the stack.
// However if it's a musttail call, we cannot remove the tailcall attribute.
// It's safe to keep it there as the musttail call is for symmetric transfer,
// and by that point the frame should have been destroyed and hence not
// interfering with operands.
static void removeTailCallAttribute(AllocaInst *Frame, AAResults &AA) {
  Function &F = *Frame->getFunction();
  for (Instruction &I : instructions(F))
    if (auto *Call = dyn_cast<CallInst>(&I))
      if (Call->isTailCall() && operandReferences(Call, Frame, AA) &&
          !Call->isMustTailCall())
        Call->setTailCall(false);
}

// Given a resume function @f.resume(%f.frame* %frame), returns the size
// and expected alignment of %f.frame type.
static std::optional<std::pair<uint64_t, Align>>
getFrameLayout(Function *Resume) {
  // Pull information from the function attributes.
  auto Size = Resume->getParamDereferenceableBytes(0);
  if (!Size)
    return std::nullopt;
  return std::make_pair(Size, Resume->getParamAlign(0).valueOrOne());
}

// Finds first non alloca instruction in the entry block of a function.
static Instruction *getFirstNonAllocaInTheEntryBlock(Function *F) {
  for (Instruction &I : F->getEntryBlock())
    if (!isa<AllocaInst>(&I))
      return &I;
  llvm_unreachable("no terminator in the entry block");
}

#ifndef NDEBUG
static std::unique_ptr<raw_fd_ostream> getOrCreateLogFile() {
  assert(!CoroElideInfoOutputFilename.empty() &&
         "coro-elide-info-output-file shouldn't be empty");
  std::error_code EC;
  auto Result = std::make_unique<raw_fd_ostream>(CoroElideInfoOutputFilename,
                                                 EC, sys::fs::OF_Append);
  if (!EC)
    return Result;
  llvm::errs() << "Error opening coro-elide-info-output-file '"
               << CoroElideInfoOutputFilename << " for appending!\n";
  return std::make_unique<raw_fd_ostream>(2, false); // stderr.
}
#endif

void FunctionElideInfo::collectPostSplitCoroIds() {
  for (auto &I : instructions(this->ContainingFunction)) {
    if (auto *CII = dyn_cast<CoroIdInst>(&I))
      if (CII->getInfo().isPostSplit())
        // If it is the coroutine itself, don't touch it.
        if (CII->getCoroutine() != CII->getFunction())
          CoroIds.push_back(CII);

    // Consider case like:
    // %0 = call i8 @llvm.coro.suspend(...)
    // switch i8 %0, label %suspend [i8 0, label %resume
    //                              i8 1, label %cleanup]
    // and collect the SwitchInsts which are used by escape analysis later.
    if (auto *CSI = dyn_cast<CoroSuspendInst>(&I))
      if (CSI->hasOneUse() && isa<SwitchInst>(CSI->use_begin()->getUser())) {
        SwitchInst *SWI = cast<SwitchInst>(CSI->use_begin()->getUser());
        if (SWI->getNumCases() == 2)
          CoroSuspendSwitches.insert(SWI);
      }
  }
}

CoroIdElider::CoroIdElider(CoroIdInst *CoroId, FunctionElideInfo &FEI,
                           AAResults &AA, DominatorTree &DT,
                           OptimizationRemarkEmitter &ORE)
    : CoroId(CoroId), FEI(FEI), AA(AA), DT(DT), ORE(ORE) {
  // Collect all coro.begin and coro.allocs associated with this coro.id.
  for (User *U : CoroId->users()) {
    if (auto *CB = dyn_cast<CoroBeginInst>(U))
      CoroBegins.push_back(CB);
    else if (auto *CA = dyn_cast<CoroAllocInst>(U))
      CoroAllocs.push_back(CA);
  }

  // Collect all coro.subfn.addrs associated with coro.begin.
  // Note, we only devirtualize the calls if their coro.subfn.addr refers to
  // coro.begin directly. If we run into cases where this check is too
  // conservative, we can consider relaxing the check.
  for (CoroBeginInst *CB : CoroBegins) {
    for (User *U : CB->users())
      if (auto *II = dyn_cast<CoroSubFnInst>(U))
        switch (II->getIndex()) {
        case CoroSubFnInst::ResumeIndex:
          ResumeAddr.push_back(II);
          break;
        case CoroSubFnInst::DestroyIndex:
          DestroyAddr[CB].push_back(II);
          break;
        default:
          llvm_unreachable("unexpected coro.subfn.addr constant");
        }
  }
}

// To elide heap allocations we need to suppress code blocks guarded by
// llvm.coro.alloc and llvm.coro.free instructions.
void CoroIdElider::elideHeapAllocations(uint64_t FrameSize, Align FrameAlign) {
  LLVMContext &C = FEI.ContainingFunction->getContext();
  BasicBlock::iterator InsertPt =
      getFirstNonAllocaInTheEntryBlock(FEI.ContainingFunction)->getIterator();

  // Replacing llvm.coro.alloc with false will suppress dynamic
  // allocation as it is expected for the frontend to generate the code that
  // looks like:
  //   id = coro.id(...)
  //   mem = coro.alloc(id) ? malloc(coro.size()) : 0;
  //   coro.begin(id, mem)
  auto *False = ConstantInt::getFalse(C);
  for (auto *CA : CoroAllocs) {
    CA->replaceAllUsesWith(False);
    CA->eraseFromParent();
  }

  // FIXME: Design how to transmit alignment information for every alloca that
  // is spilled into the coroutine frame and recreate the alignment information
  // here. Possibly we will need to do a mini SROA here and break the coroutine
  // frame into individual AllocaInst recreating the original alignment.
  const DataLayout &DL = FEI.ContainingFunction->getDataLayout();
  auto FrameTy = ArrayType::get(Type::getInt8Ty(C), FrameSize);
  auto *Frame = new AllocaInst(FrameTy, DL.getAllocaAddrSpace(), "", InsertPt);
  Frame->setAlignment(FrameAlign);
  auto *FrameVoidPtr =
      new BitCastInst(Frame, PointerType::getUnqual(C), "vFrame", InsertPt);

  for (auto *CB : CoroBegins) {
    CB->replaceAllUsesWith(FrameVoidPtr);
    CB->eraseFromParent();
  }

  // Since now coroutine frame lives on the stack we need to make sure that
  // any tail call referencing it, must be made non-tail call.
  removeTailCallAttribute(Frame, AA);
}

bool CoroIdElider::canCoroBeginEscape(
    const CoroBeginInst *CB, const SmallPtrSetImpl<BasicBlock *> &TIs) const {
  const auto &It = DestroyAddr.find(CB);
  assert(It != DestroyAddr.end());

  // Limit the number of blocks we visit.
  unsigned Limit = 32 * (1 + It->second.size());

  SmallVector<const BasicBlock *, 32> Worklist;
  Worklist.push_back(CB->getParent());

  SmallPtrSet<const BasicBlock *, 32> Visited;
  // Consider basicblock of coro.destroy as visited one, so that we
  // skip the path pass through coro.destroy.
  for (auto *DA : It->second)
    Visited.insert(DA->getParent());

  SmallPtrSet<const BasicBlock *, 32> EscapingBBs;
  for (auto *U : CB->users()) {
    // The use from coroutine intrinsics are not a problem.
    if (isa<CoroFreeInst, CoroSubFnInst, CoroSaveInst>(U))
      continue;

    // Think all other usages may be an escaping candidate conservatively.
    //
    // Note that the major user of switch ABI coroutine (the C++) will store
    // resume.fn, destroy.fn and the index to the coroutine frame immediately.
    // So the parent of the coro.begin in C++ will be always escaping.
    // Then we can't get any performance benefits for C++ by improving the
    // precision of the method.
    //
    // The reason why we still judge it is we want to make LLVM Coroutine in
    // switch ABIs to be self contained as much as possible instead of a
    // by-product of C++20 Coroutines.
    EscapingBBs.insert(cast<Instruction>(U)->getParent());
  }

  bool PotentiallyEscaped = false;

  do {
    const auto *BB = Worklist.pop_back_val();
    if (!Visited.insert(BB).second)
      continue;

    // A Path insensitive marker to test whether the coro.begin escapes.
    // It is intentional to make it path insensitive while it may not be
    // precise since we don't want the process to be too slow.
    PotentiallyEscaped |= EscapingBBs.count(BB);

    if (TIs.count(BB)) {
      if (isa<ReturnInst>(BB->getTerminator()) || PotentiallyEscaped)
        return true;

      // If the function ends with the exceptional terminator, the memory used
      // by the coroutine frame can be released by stack unwinding
      // automatically. So we can think the coro.begin doesn't escape if it
      // exits the function by exceptional terminator.

      continue;
    }

    // Conservatively say that there is potentially a path.
    if (!--Limit)
      return true;

    auto TI = BB->getTerminator();
    // Although the default dest of coro.suspend switches is suspend pointer
    // which means a escape path to normal terminator, it is reasonable to skip
    // it since coroutine frame doesn't change outside the coroutine body.
    if (isa<SwitchInst>(TI) &&
        FEI.CoroSuspendSwitches.count(cast<SwitchInst>(TI))) {
      Worklist.push_back(cast<SwitchInst>(TI)->getSuccessor(1));
      Worklist.push_back(cast<SwitchInst>(TI)->getSuccessor(2));
    } else
      Worklist.append(succ_begin(BB), succ_end(BB));

  } while (!Worklist.empty());

  // We have exhausted all possible paths and are certain that coro.begin can
  // not reach to any of terminators.
  return false;
}

bool CoroIdElider::lifetimeEligibleForElide() const {
  // If no CoroAllocs, we cannot suppress allocation, so elision is not
  // possible.
  if (CoroAllocs.empty())
    return false;

  // Check that for every coro.begin there is at least one coro.destroy directly
  // referencing the SSA value of that coro.begin along each
  // non-exceptional path.
  //
  // If the value escaped, then coro.destroy would have been referencing a
  // memory location storing that value and not the virtual register.

  SmallPtrSet<BasicBlock *, 8> Terminators;
  // First gather all of the terminators for the function.
  // Consider the final coro.suspend as the real terminator when the current
  // function is a coroutine.
  for (BasicBlock &B : *FEI.ContainingFunction) {
    auto *TI = B.getTerminator();

    if (TI->getNumSuccessors() != 0 || isa<UnreachableInst>(TI))
      continue;

    Terminators.insert(&B);
  }

  // Filter out the coro.destroy that lie along exceptional paths.
  for (const auto *CB : CoroBegins) {
    auto It = DestroyAddr.find(CB);

    // FIXME: If we have not found any destroys for this coro.begin, we
    // disqualify this elide.
    if (It == DestroyAddr.end())
      return false;

    const auto &CorrespondingDestroyAddrs = It->second;

    // If every terminators is dominated by coro.destroy, we could know the
    // corresponding coro.begin wouldn't escape.
    auto DominatesTerminator = [&](auto *TI) {
      return llvm::any_of(CorrespondingDestroyAddrs, [&](auto *Destroy) {
        return DT.dominates(Destroy, TI->getTerminator());
      });
    };

    if (llvm::all_of(Terminators, DominatesTerminator))
      continue;

    // Otherwise canCoroBeginEscape would decide whether there is any paths from
    // coro.begin to Terminators which not pass through any of the
    // coro.destroys. This is a slower analysis.
    //
    // canCoroBeginEscape is relatively slow, so we avoid to run it as much as
    // possible.
    if (canCoroBeginEscape(CB, Terminators))
      return false;
  }

  // We have checked all CoroBegins and their paths to the terminators without
  // finding disqualifying code patterns, so we can perform heap allocations.
  return true;
}

bool CoroIdElider::attemptElide() {
  // PostSplit coro.id refers to an array of subfunctions in its Info
  // argument.
  ConstantArray *Resumers = CoroId->getInfo().Resumers;
  assert(Resumers && "PostSplit coro.id Info argument must refer to an array"
                     "of coroutine subfunctions");
  auto *ResumeAddrConstant =
      Resumers->getAggregateElement(CoroSubFnInst::ResumeIndex);

  replaceWithConstant(ResumeAddrConstant, ResumeAddr);

  bool EligibleForElide = lifetimeEligibleForElide();

  auto *DestroyAddrConstant = Resumers->getAggregateElement(
      EligibleForElide ? CoroSubFnInst::CleanupIndex
                       : CoroSubFnInst::DestroyIndex);

  for (auto &It : DestroyAddr)
    replaceWithConstant(DestroyAddrConstant, It.second);

  auto FrameSizeAndAlign = getFrameLayout(cast<Function>(ResumeAddrConstant));

  auto CallerFunctionName = FEI.ContainingFunction->getName();
  auto CalleeCoroutineName = CoroId->getCoroutine()->getName();

  if (EligibleForElide && FrameSizeAndAlign) {
    elideHeapAllocations(FrameSizeAndAlign->first, FrameSizeAndAlign->second);
    coro::replaceCoroFree(CoroId, /*Elide=*/true);
    NumOfCoroElided++;

#ifndef NDEBUG
      if (!CoroElideInfoOutputFilename.empty())
        *getOrCreateLogFile() << "Elide " << CalleeCoroutineName << " in "
                              << FEI.ContainingFunction->getName() << "\n";
#endif

      ORE.emit([&]() {
        return OptimizationRemark(DEBUG_TYPE, "CoroElide", CoroId)
               << "'" << ore::NV("callee", CalleeCoroutineName)
               << "' elided in '" << ore::NV("caller", CallerFunctionName)
               << "' (frame_size="
               << ore::NV("frame_size", FrameSizeAndAlign->first) << ", align="
               << ore::NV("align", FrameSizeAndAlign->second.value()) << ")";
      });
  } else {
    ORE.emit([&]() {
      auto Remark = OptimizationRemarkMissed(DEBUG_TYPE, "CoroElide", CoroId)
                    << "'" << ore::NV("callee", CalleeCoroutineName)
                    << "' not elided in '"
                    << ore::NV("caller", CallerFunctionName);

      if (FrameSizeAndAlign)
        return Remark << "' (frame_size="
                      << ore::NV("frame_size", FrameSizeAndAlign->first)
                      << ", align="
                      << ore::NV("align", FrameSizeAndAlign->second.value())
                      << ")";
      else
        return Remark << "' (frame_size=unknown, align=unknown)";
    });
  }

  return true;
}

PreservedAnalyses CoroElidePass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &M = *F.getParent();
  if (!coro::declaresIntrinsics(M, {"llvm.coro.id"}))
    return PreservedAnalyses::all();

  FunctionElideInfo FEI{&F};
  // Elide is not necessary if there's no coro.id within the function.
  if (!FEI.hasCoroIds())
    return PreservedAnalyses::all();

  AAResults &AA = AM.getResult<AAManager>(F);
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);

  bool Changed = false;
  for (auto *CII : FEI.getCoroIds()) {
    CoroIdElider CIE(CII, FEI, AA, DT, ORE);
    Changed |= CIE.attemptElide();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
