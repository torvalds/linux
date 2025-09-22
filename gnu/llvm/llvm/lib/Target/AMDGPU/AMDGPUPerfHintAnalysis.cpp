//===- AMDGPUPerfHintAnalysis.cpp - analysis of functions memory traffic --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Analyzes if a function potentially memory bound and if a kernel
/// kernel may benefit from limiting number of waves to reduce cache thrashing.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUPerfHintAnalysis.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-perf-hint"

static cl::opt<unsigned>
    MemBoundThresh("amdgpu-membound-threshold", cl::init(50), cl::Hidden,
                   cl::desc("Function mem bound threshold in %"));

static cl::opt<unsigned>
    LimitWaveThresh("amdgpu-limit-wave-threshold", cl::init(50), cl::Hidden,
                    cl::desc("Kernel limit wave threshold in %"));

static cl::opt<unsigned>
    IAWeight("amdgpu-indirect-access-weight", cl::init(1000), cl::Hidden,
             cl::desc("Indirect access memory instruction weight"));

static cl::opt<unsigned>
    LSWeight("amdgpu-large-stride-weight", cl::init(1000), cl::Hidden,
             cl::desc("Large stride memory access weight"));

static cl::opt<unsigned>
    LargeStrideThresh("amdgpu-large-stride-threshold", cl::init(64), cl::Hidden,
                      cl::desc("Large stride memory access threshold"));

STATISTIC(NumMemBound, "Number of functions marked as memory bound");
STATISTIC(NumLimitWave, "Number of functions marked as needing limit wave");

char llvm::AMDGPUPerfHintAnalysis::ID = 0;
char &llvm::AMDGPUPerfHintAnalysisID = AMDGPUPerfHintAnalysis::ID;

INITIALIZE_PASS(AMDGPUPerfHintAnalysis, DEBUG_TYPE,
                "Analysis if a function is memory bound", true, true)

namespace {

struct AMDGPUPerfHint {
  friend AMDGPUPerfHintAnalysis;

public:
  AMDGPUPerfHint(AMDGPUPerfHintAnalysis::FuncInfoMap &FIM_,
                 const TargetLowering *TLI_)
      : FIM(FIM_), TLI(TLI_) {}

  bool runOnFunction(Function &F);

private:
  struct MemAccessInfo {
    const Value *V = nullptr;
    const Value *Base = nullptr;
    int64_t Offset = 0;
    MemAccessInfo() = default;
    bool isLargeStride(MemAccessInfo &Reference) const;
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    Printable print() const {
      return Printable([this](raw_ostream &OS) {
        OS << "Value: " << *V << '\n'
           << "Base: " << *Base << " Offset: " << Offset << '\n';
      });
    }
#endif
  };

  MemAccessInfo makeMemAccessInfo(Instruction *) const;

  MemAccessInfo LastAccess; // Last memory access info

  AMDGPUPerfHintAnalysis::FuncInfoMap &FIM;

  const DataLayout *DL = nullptr;

  const TargetLowering *TLI;

  AMDGPUPerfHintAnalysis::FuncInfo *visit(const Function &F);
  static bool isMemBound(const AMDGPUPerfHintAnalysis::FuncInfo &F);
  static bool needLimitWave(const AMDGPUPerfHintAnalysis::FuncInfo &F);

  bool isIndirectAccess(const Instruction *Inst) const;

  /// Check if the instruction is large stride.
  /// The purpose is to identify memory access pattern like:
  /// x = a[i];
  /// y = a[i+1000];
  /// z = a[i+2000];
  /// In the above example, the second and third memory access will be marked
  /// large stride memory access.
  bool isLargeStride(const Instruction *Inst);

  bool isGlobalAddr(const Value *V) const;
  bool isLocalAddr(const Value *V) const;
  bool isGlobalLoadUsedInBB(const Instruction &) const;
};

static std::pair<const Value *, const Type *> getMemoryInstrPtrAndType(
    const Instruction *Inst) {
  if (auto LI = dyn_cast<LoadInst>(Inst))
    return {LI->getPointerOperand(), LI->getType()};
  if (auto SI = dyn_cast<StoreInst>(Inst))
    return {SI->getPointerOperand(), SI->getValueOperand()->getType()};
  if (auto AI = dyn_cast<AtomicCmpXchgInst>(Inst))
    return {AI->getPointerOperand(), AI->getCompareOperand()->getType()};
  if (auto AI = dyn_cast<AtomicRMWInst>(Inst))
    return {AI->getPointerOperand(), AI->getValOperand()->getType()};
  if (auto MI = dyn_cast<AnyMemIntrinsic>(Inst))
    return {MI->getRawDest(), Type::getInt8Ty(MI->getContext())};

  return {nullptr, nullptr};
}

bool AMDGPUPerfHint::isIndirectAccess(const Instruction *Inst) const {
  LLVM_DEBUG(dbgs() << "[isIndirectAccess] " << *Inst << '\n');
  SmallSet<const Value *, 32> WorkSet;
  SmallSet<const Value *, 32> Visited;
  if (const Value *MO = getMemoryInstrPtrAndType(Inst).first) {
    if (isGlobalAddr(MO))
      WorkSet.insert(MO);
  }

  while (!WorkSet.empty()) {
    const Value *V = *WorkSet.begin();
    WorkSet.erase(*WorkSet.begin());
    if (!Visited.insert(V).second)
      continue;
    LLVM_DEBUG(dbgs() << "  check: " << *V << '\n');

    if (auto LD = dyn_cast<LoadInst>(V)) {
      auto M = LD->getPointerOperand();
      if (isGlobalAddr(M)) {
        LLVM_DEBUG(dbgs() << "    is IA\n");
        return true;
      }
      continue;
    }

    if (auto GEP = dyn_cast<GetElementPtrInst>(V)) {
      auto P = GEP->getPointerOperand();
      WorkSet.insert(P);
      for (unsigned I = 1, E = GEP->getNumIndices() + 1; I != E; ++I)
        WorkSet.insert(GEP->getOperand(I));
      continue;
    }

    if (auto U = dyn_cast<UnaryInstruction>(V)) {
      WorkSet.insert(U->getOperand(0));
      continue;
    }

    if (auto BO = dyn_cast<BinaryOperator>(V)) {
      WorkSet.insert(BO->getOperand(0));
      WorkSet.insert(BO->getOperand(1));
      continue;
    }

    if (auto S = dyn_cast<SelectInst>(V)) {
      WorkSet.insert(S->getFalseValue());
      WorkSet.insert(S->getTrueValue());
      continue;
    }

    if (auto E = dyn_cast<ExtractElementInst>(V)) {
      WorkSet.insert(E->getVectorOperand());
      continue;
    }

    LLVM_DEBUG(dbgs() << "    dropped\n");
  }

  LLVM_DEBUG(dbgs() << "  is not IA\n");
  return false;
}

// Returns true if the global load `I` is used in its own basic block.
bool AMDGPUPerfHint::isGlobalLoadUsedInBB(const Instruction &I) const {
  const auto *Ld = dyn_cast<LoadInst>(&I);
  if (!Ld)
    return false;
  if (!isGlobalAddr(Ld->getPointerOperand()))
    return false;

  for (const User *Usr : Ld->users()) {
    if (const Instruction *UsrInst = dyn_cast<Instruction>(Usr)) {
      if (UsrInst->getParent() == I.getParent())
        return true;
    }
  }

  return false;
}

AMDGPUPerfHintAnalysis::FuncInfo *AMDGPUPerfHint::visit(const Function &F) {
  AMDGPUPerfHintAnalysis::FuncInfo &FI = FIM[&F];

  LLVM_DEBUG(dbgs() << "[AMDGPUPerfHint] process " << F.getName() << '\n');

  for (auto &B : F) {
    LastAccess = MemAccessInfo();
    unsigned UsedGlobalLoadsInBB = 0;
    for (auto &I : B) {
      if (const Type *Ty = getMemoryInstrPtrAndType(&I).second) {
        unsigned Size = divideCeil(Ty->getPrimitiveSizeInBits(), 32);
        // TODO: Check if the global load and its user are close to each other
        // instead (Or do this analysis in GCNSchedStrategy?).
        if (isGlobalLoadUsedInBB(I))
          UsedGlobalLoadsInBB += Size;
        if (isIndirectAccess(&I))
          FI.IAMInstCost += Size;
        if (isLargeStride(&I))
          FI.LSMInstCost += Size;
        FI.MemInstCost += Size;
        FI.InstCost += Size;
        continue;
      }
      if (auto *CB = dyn_cast<CallBase>(&I)) {
        Function *Callee = CB->getCalledFunction();
        if (!Callee || Callee->isDeclaration()) {
          ++FI.InstCost;
          continue;
        }
        if (&F == Callee) // Handle immediate recursion
          continue;

        auto Loc = FIM.find(Callee);
        if (Loc == FIM.end())
          continue;

        FI.MemInstCost += Loc->second.MemInstCost;
        FI.InstCost += Loc->second.InstCost;
        FI.IAMInstCost += Loc->second.IAMInstCost;
        FI.LSMInstCost += Loc->second.LSMInstCost;
      } else if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
        TargetLoweringBase::AddrMode AM;
        auto *Ptr = GetPointerBaseWithConstantOffset(GEP, AM.BaseOffs, *DL);
        AM.BaseGV = dyn_cast_or_null<GlobalValue>(const_cast<Value *>(Ptr));
        AM.HasBaseReg = !AM.BaseGV;
        if (TLI->isLegalAddressingMode(*DL, AM, GEP->getResultElementType(),
                                       GEP->getPointerAddressSpace()))
          // Offset will likely be folded into load or store
          continue;
        ++FI.InstCost;
      } else {
        ++FI.InstCost;
      }
    }

    if (!FI.HasDenseGlobalMemAcc) {
      unsigned GlobalMemAccPercentage = UsedGlobalLoadsInBB * 100 / B.size();
      if (GlobalMemAccPercentage > 50) {
        LLVM_DEBUG(dbgs() << "[HasDenseGlobalMemAcc] Set to true since "
                          << B.getName() << " has " << GlobalMemAccPercentage
                          << "% global memory access\n");
        FI.HasDenseGlobalMemAcc = true;
      }
    }
  }

  return &FI;
}

bool AMDGPUPerfHint::runOnFunction(Function &F) {
  const Module &M = *F.getParent();
  DL = &M.getDataLayout();

  if (F.hasFnAttribute("amdgpu-wave-limiter") &&
      F.hasFnAttribute("amdgpu-memory-bound"))
    return false;

  const AMDGPUPerfHintAnalysis::FuncInfo *Info = visit(F);

  LLVM_DEBUG(dbgs() << F.getName() << " MemInst cost: " << Info->MemInstCost
                    << '\n'
                    << " IAMInst cost: " << Info->IAMInstCost << '\n'
                    << " LSMInst cost: " << Info->LSMInstCost << '\n'
                    << " TotalInst cost: " << Info->InstCost << '\n');

  bool Changed = false;

  if (isMemBound(*Info)) {
    LLVM_DEBUG(dbgs() << F.getName() << " is memory bound\n");
    NumMemBound++;
    F.addFnAttr("amdgpu-memory-bound", "true");
    Changed = true;
  }

  if (AMDGPU::isEntryFunctionCC(F.getCallingConv()) && needLimitWave(*Info)) {
    LLVM_DEBUG(dbgs() << F.getName() << " needs limit wave\n");
    NumLimitWave++;
    F.addFnAttr("amdgpu-wave-limiter", "true");
    Changed = true;
  }

  return Changed;
}

bool AMDGPUPerfHint::isMemBound(const AMDGPUPerfHintAnalysis::FuncInfo &FI) {
  // Reverting optimal scheduling in favour of occupancy with basic block(s)
  // having dense global memory access can potentially hurt performance.
  if (FI.HasDenseGlobalMemAcc)
    return true;

  return FI.MemInstCost * 100 / FI.InstCost > MemBoundThresh;
}

bool AMDGPUPerfHint::needLimitWave(const AMDGPUPerfHintAnalysis::FuncInfo &FI) {
  return ((FI.MemInstCost + FI.IAMInstCost * IAWeight +
           FI.LSMInstCost * LSWeight) * 100 / FI.InstCost) > LimitWaveThresh;
}

bool AMDGPUPerfHint::isGlobalAddr(const Value *V) const {
  if (auto PT = dyn_cast<PointerType>(V->getType())) {
    unsigned As = PT->getAddressSpace();
    // Flat likely points to global too.
    return As == AMDGPUAS::GLOBAL_ADDRESS || As == AMDGPUAS::FLAT_ADDRESS;
  }
  return false;
}

bool AMDGPUPerfHint::isLocalAddr(const Value *V) const {
  if (auto PT = dyn_cast<PointerType>(V->getType()))
    return PT->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS;
  return false;
}

bool AMDGPUPerfHint::isLargeStride(const Instruction *Inst) {
  LLVM_DEBUG(dbgs() << "[isLargeStride] " << *Inst << '\n');

  MemAccessInfo MAI = makeMemAccessInfo(const_cast<Instruction *>(Inst));
  bool IsLargeStride = MAI.isLargeStride(LastAccess);
  if (MAI.Base)
    LastAccess = std::move(MAI);

  return IsLargeStride;
}

AMDGPUPerfHint::MemAccessInfo
AMDGPUPerfHint::makeMemAccessInfo(Instruction *Inst) const {
  MemAccessInfo MAI;
  const Value *MO = getMemoryInstrPtrAndType(Inst).first;

  LLVM_DEBUG(dbgs() << "[isLargeStride] MO: " << *MO << '\n');
  // Do not treat local-addr memory access as large stride.
  if (isLocalAddr(MO))
    return MAI;

  MAI.V = MO;
  MAI.Base = GetPointerBaseWithConstantOffset(MO, MAI.Offset, *DL);
  return MAI;
}

bool AMDGPUPerfHint::MemAccessInfo::isLargeStride(
    MemAccessInfo &Reference) const {

  if (!Base || !Reference.Base || Base != Reference.Base)
    return false;

  uint64_t Diff = Offset > Reference.Offset ? Offset - Reference.Offset
                                            : Reference.Offset - Offset;
  bool Result = Diff > LargeStrideThresh;
  LLVM_DEBUG(dbgs() << "[isLargeStride compare]\n"
               << print() << "<=>\n"
               << Reference.print() << "Result:" << Result << '\n');
  return Result;
}
} // namespace

bool AMDGPUPerfHintAnalysis::runOnSCC(CallGraphSCC &SCC) {
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  const TargetMachine &TM = TPC->getTM<TargetMachine>();

  bool Changed = false;
  for (CallGraphNode *I : SCC) {
    Function *F = I->getFunction();
    if (!F || F->isDeclaration())
      continue;

    const TargetSubtargetInfo *ST = TM.getSubtargetImpl(*F);
    AMDGPUPerfHint Analyzer(FIM, ST->getTargetLowering());

    if (Analyzer.runOnFunction(*F))
      Changed = true;
  }

  return Changed;
}

bool AMDGPUPerfHintAnalysis::isMemoryBound(const Function *F) const {
  auto FI = FIM.find(F);
  if (FI == FIM.end())
    return false;

  return AMDGPUPerfHint::isMemBound(FI->second);
}

bool AMDGPUPerfHintAnalysis::needsWaveLimiter(const Function *F) const {
  auto FI = FIM.find(F);
  if (FI == FIM.end())
    return false;

  return AMDGPUPerfHint::needLimitWave(FI->second);
}
