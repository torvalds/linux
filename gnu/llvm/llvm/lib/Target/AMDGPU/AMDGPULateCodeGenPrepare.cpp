//===-- AMDGPUCodeGenPrepare.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass does misc. AMDGPU optimizations on IR *just* before instruction
/// selection.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUTargetMachine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/Utils/Local.h"

#define DEBUG_TYPE "amdgpu-late-codegenprepare"

using namespace llvm;

// Scalar load widening needs running after load-store-vectorizer as that pass
// doesn't handle overlapping cases. In addition, this pass enhances the
// widening to handle cases where scalar sub-dword loads are naturally aligned
// only but not dword aligned.
static cl::opt<bool>
    WidenLoads("amdgpu-late-codegenprepare-widen-constant-loads",
               cl::desc("Widen sub-dword constant address space loads in "
                        "AMDGPULateCodeGenPrepare"),
               cl::ReallyHidden, cl::init(true));

namespace {

class AMDGPULateCodeGenPrepare
    : public FunctionPass,
      public InstVisitor<AMDGPULateCodeGenPrepare, bool> {
  Module *Mod = nullptr;
  const DataLayout *DL = nullptr;

  AssumptionCache *AC = nullptr;
  UniformityInfo *UA = nullptr;

  SmallVector<WeakTrackingVH, 8> DeadInsts;

public:
  static char ID;

  AMDGPULateCodeGenPrepare() : FunctionPass(ID) {}

  StringRef getPassName() const override {
    return "AMDGPU IR late optimizations";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<UniformityInfoWrapperPass>();
    AU.setPreservesAll();
  }

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;

  bool visitInstruction(Instruction &) { return false; }

  // Check if the specified value is at least DWORD aligned.
  bool isDWORDAligned(const Value *V) const {
    KnownBits Known = computeKnownBits(V, *DL, 0, AC);
    return Known.countMinTrailingZeros() >= 2;
  }

  bool canWidenScalarExtLoad(LoadInst &LI) const;
  bool visitLoadInst(LoadInst &LI);
};

using ValueToValueMap = DenseMap<const Value *, Value *>;

class LiveRegOptimizer {
private:
  Module *Mod = nullptr;
  const DataLayout *DL = nullptr;
  const GCNSubtarget *ST;
  /// The scalar type to convert to
  Type *ConvertToScalar;
  /// The set of visited Instructions
  SmallPtrSet<Instruction *, 4> Visited;
  /// Map of Value -> Converted Value
  ValueToValueMap ValMap;
  /// Map of containing conversions from Optimal Type -> Original Type per BB.
  DenseMap<BasicBlock *, ValueToValueMap> BBUseValMap;

public:
  /// Calculate the and \p return  the type to convert to given a problematic \p
  /// OriginalType. In some instances, we may widen the type (e.g. v2i8 -> i32).
  Type *calculateConvertType(Type *OriginalType);
  /// Convert the virtual register defined by \p V to the compatible vector of
  /// legal type
  Value *convertToOptType(Instruction *V, BasicBlock::iterator &InstPt);
  /// Convert the virtual register defined by \p V back to the original type \p
  /// ConvertType, stripping away the MSBs in cases where there was an imperfect
  /// fit (e.g. v2i32 -> v7i8)
  Value *convertFromOptType(Type *ConvertType, Instruction *V,
                            BasicBlock::iterator &InstPt,
                            BasicBlock *InsertBlock);
  /// Check for problematic PHI nodes or cross-bb values based on the value
  /// defined by \p I, and coerce to legal types if necessary. For problematic
  /// PHI node, we coerce all incoming values in a single invocation.
  bool optimizeLiveType(Instruction *I,
                        SmallVectorImpl<WeakTrackingVH> &DeadInsts);

  // Whether or not the type should be replaced to avoid inefficient
  // legalization code
  bool shouldReplace(Type *ITy) {
    FixedVectorType *VTy = dyn_cast<FixedVectorType>(ITy);
    if (!VTy)
      return false;

    auto TLI = ST->getTargetLowering();

    Type *EltTy = VTy->getElementType();
    // If the element size is not less than the convert to scalar size, then we
    // can't do any bit packing
    if (!EltTy->isIntegerTy() ||
        EltTy->getScalarSizeInBits() > ConvertToScalar->getScalarSizeInBits())
      return false;

    // Only coerce illegal types
    TargetLoweringBase::LegalizeKind LK =
        TLI->getTypeConversion(EltTy->getContext(), EVT::getEVT(EltTy, false));
    return LK.first != TargetLoweringBase::TypeLegal;
  }

  LiveRegOptimizer(Module *Mod, const GCNSubtarget *ST) : Mod(Mod), ST(ST) {
    DL = &Mod->getDataLayout();
    ConvertToScalar = Type::getInt32Ty(Mod->getContext());
  }
};

} // end anonymous namespace

bool AMDGPULateCodeGenPrepare::doInitialization(Module &M) {
  Mod = &M;
  DL = &Mod->getDataLayout();
  return false;
}

bool AMDGPULateCodeGenPrepare::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  const TargetPassConfig &TPC = getAnalysis<TargetPassConfig>();
  const TargetMachine &TM = TPC.getTM<TargetMachine>();
  const GCNSubtarget &ST = TM.getSubtarget<GCNSubtarget>(F);

  AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  UA = &getAnalysis<UniformityInfoWrapperPass>().getUniformityInfo();

  // "Optimize" the virtual regs that cross basic block boundaries. When
  // building the SelectionDAG, vectors of illegal types that cross basic blocks
  // will be scalarized and widened, with each scalar living in its
  // own register. To work around this, this optimization converts the
  // vectors to equivalent vectors of legal type (which are converted back
  // before uses in subsequent blocks), to pack the bits into fewer physical
  // registers (used in CopyToReg/CopyFromReg pairs).
  LiveRegOptimizer LRO(Mod, &ST);

  bool Changed = false;

  bool HasScalarSubwordLoads = ST.hasScalarSubwordLoads();

  for (auto &BB : reverse(F))
    for (Instruction &I : make_early_inc_range(reverse(BB))) {
      Changed |= !HasScalarSubwordLoads && visit(I);
      Changed |= LRO.optimizeLiveType(&I, DeadInsts);
    }

  RecursivelyDeleteTriviallyDeadInstructionsPermissive(DeadInsts);
  return Changed;
}

Type *LiveRegOptimizer::calculateConvertType(Type *OriginalType) {
  assert(OriginalType->getScalarSizeInBits() <=
         ConvertToScalar->getScalarSizeInBits());

  FixedVectorType *VTy = cast<FixedVectorType>(OriginalType);

  TypeSize OriginalSize = DL->getTypeSizeInBits(VTy);
  TypeSize ConvertScalarSize = DL->getTypeSizeInBits(ConvertToScalar);
  unsigned ConvertEltCount =
      (OriginalSize + ConvertScalarSize - 1) / ConvertScalarSize;

  if (OriginalSize <= ConvertScalarSize)
    return IntegerType::get(Mod->getContext(), ConvertScalarSize);

  return VectorType::get(Type::getIntNTy(Mod->getContext(), ConvertScalarSize),
                         ConvertEltCount, false);
}

Value *LiveRegOptimizer::convertToOptType(Instruction *V,
                                          BasicBlock::iterator &InsertPt) {
  FixedVectorType *VTy = cast<FixedVectorType>(V->getType());
  Type *NewTy = calculateConvertType(V->getType());

  TypeSize OriginalSize = DL->getTypeSizeInBits(VTy);
  TypeSize NewSize = DL->getTypeSizeInBits(NewTy);

  IRBuilder<> Builder(V->getParent(), InsertPt);
  // If there is a bitsize match, we can fit the old vector into a new vector of
  // desired type.
  if (OriginalSize == NewSize)
    return Builder.CreateBitCast(V, NewTy, V->getName() + ".bc");

  // If there is a bitsize mismatch, we must use a wider vector.
  assert(NewSize > OriginalSize);
  uint64_t ExpandedVecElementCount = NewSize / VTy->getScalarSizeInBits();

  SmallVector<int, 8> ShuffleMask;
  uint64_t OriginalElementCount = VTy->getElementCount().getFixedValue();
  for (unsigned I = 0; I < OriginalElementCount; I++)
    ShuffleMask.push_back(I);

  for (uint64_t I = OriginalElementCount; I < ExpandedVecElementCount; I++)
    ShuffleMask.push_back(OriginalElementCount);

  Value *ExpandedVec = Builder.CreateShuffleVector(V, ShuffleMask);
  return Builder.CreateBitCast(ExpandedVec, NewTy, V->getName() + ".bc");
}

Value *LiveRegOptimizer::convertFromOptType(Type *ConvertType, Instruction *V,
                                            BasicBlock::iterator &InsertPt,
                                            BasicBlock *InsertBB) {
  FixedVectorType *NewVTy = cast<FixedVectorType>(ConvertType);

  TypeSize OriginalSize = DL->getTypeSizeInBits(V->getType());
  TypeSize NewSize = DL->getTypeSizeInBits(NewVTy);

  IRBuilder<> Builder(InsertBB, InsertPt);
  // If there is a bitsize match, we simply convert back to the original type.
  if (OriginalSize == NewSize)
    return Builder.CreateBitCast(V, NewVTy, V->getName() + ".bc");

  // If there is a bitsize mismatch, then we must have used a wider value to
  // hold the bits.
  assert(OriginalSize > NewSize);
  // For wide scalars, we can just truncate the value.
  if (!V->getType()->isVectorTy()) {
    Instruction *Trunc = cast<Instruction>(
        Builder.CreateTrunc(V, IntegerType::get(Mod->getContext(), NewSize)));
    return cast<Instruction>(Builder.CreateBitCast(Trunc, NewVTy));
  }

  // For wider vectors, we must strip the MSBs to convert back to the original
  // type.
  VectorType *ExpandedVT = VectorType::get(
      Type::getIntNTy(Mod->getContext(), NewVTy->getScalarSizeInBits()),
      (OriginalSize / NewVTy->getScalarSizeInBits()), false);
  Instruction *Converted =
      cast<Instruction>(Builder.CreateBitCast(V, ExpandedVT));

  unsigned NarrowElementCount = NewVTy->getElementCount().getFixedValue();
  SmallVector<int, 8> ShuffleMask(NarrowElementCount);
  std::iota(ShuffleMask.begin(), ShuffleMask.end(), 0);

  return Builder.CreateShuffleVector(Converted, ShuffleMask);
}

bool LiveRegOptimizer::optimizeLiveType(
    Instruction *I, SmallVectorImpl<WeakTrackingVH> &DeadInsts) {
  SmallVector<Instruction *, 4> Worklist;
  SmallPtrSet<PHINode *, 4> PhiNodes;
  SmallPtrSet<Instruction *, 4> Defs;
  SmallPtrSet<Instruction *, 4> Uses;

  Worklist.push_back(cast<Instruction>(I));
  while (!Worklist.empty()) {
    Instruction *II = Worklist.pop_back_val();

    if (!Visited.insert(II).second)
      continue;

    if (!shouldReplace(II->getType()))
      continue;

    if (PHINode *Phi = dyn_cast<PHINode>(II)) {
      PhiNodes.insert(Phi);
      // Collect all the incoming values of problematic PHI nodes.
      for (Value *V : Phi->incoming_values()) {
        // Repeat the collection process for newly found PHI nodes.
        if (PHINode *OpPhi = dyn_cast<PHINode>(V)) {
          if (!PhiNodes.count(OpPhi) && !Visited.count(OpPhi))
            Worklist.push_back(OpPhi);
          continue;
        }

        Instruction *IncInst = dyn_cast<Instruction>(V);
        // Other incoming value types (e.g. vector literals) are unhandled
        if (!IncInst && !isa<ConstantAggregateZero>(V))
          return false;

        // Collect all other incoming values for coercion.
        if (IncInst)
          Defs.insert(IncInst);
      }
    }

    // Collect all relevant uses.
    for (User *V : II->users()) {
      // Repeat the collection process for problematic PHI nodes.
      if (PHINode *OpPhi = dyn_cast<PHINode>(V)) {
        if (!PhiNodes.count(OpPhi) && !Visited.count(OpPhi))
          Worklist.push_back(OpPhi);
        continue;
      }

      Instruction *UseInst = cast<Instruction>(V);
      // Collect all uses of PHINodes and any use the crosses BB boundaries.
      if (UseInst->getParent() != II->getParent() || isa<PHINode>(II)) {
        Uses.insert(UseInst);
        if (!Defs.count(II) && !isa<PHINode>(II)) {
          Defs.insert(II);
        }
      }
    }
  }

  // Coerce and track the defs.
  for (Instruction *D : Defs) {
    if (!ValMap.contains(D)) {
      BasicBlock::iterator InsertPt = std::next(D->getIterator());
      Value *ConvertVal = convertToOptType(D, InsertPt);
      assert(ConvertVal);
      ValMap[D] = ConvertVal;
    }
  }

  // Construct new-typed PHI nodes.
  for (PHINode *Phi : PhiNodes) {
    ValMap[Phi] = PHINode::Create(calculateConvertType(Phi->getType()),
                                  Phi->getNumIncomingValues(),
                                  Phi->getName() + ".tc", Phi->getIterator());
  }

  // Connect all the PHI nodes with their new incoming values.
  for (PHINode *Phi : PhiNodes) {
    PHINode *NewPhi = cast<PHINode>(ValMap[Phi]);
    bool MissingIncVal = false;
    for (int I = 0, E = Phi->getNumIncomingValues(); I < E; I++) {
      Value *IncVal = Phi->getIncomingValue(I);
      if (isa<ConstantAggregateZero>(IncVal)) {
        Type *NewType = calculateConvertType(Phi->getType());
        NewPhi->addIncoming(ConstantInt::get(NewType, 0, false),
                            Phi->getIncomingBlock(I));
      } else if (ValMap.contains(IncVal) && ValMap[IncVal])
        NewPhi->addIncoming(ValMap[IncVal], Phi->getIncomingBlock(I));
      else
        MissingIncVal = true;
    }
    if (MissingIncVal) {
      Value *DeadVal = ValMap[Phi];
      // The coercion chain of the PHI is broken. Delete the Phi
      // from the ValMap and any connected / user Phis.
      SmallVector<Value *, 4> PHIWorklist;
      SmallPtrSet<Value *, 4> VisitedPhis;
      PHIWorklist.push_back(DeadVal);
      while (!PHIWorklist.empty()) {
        Value *NextDeadValue = PHIWorklist.pop_back_val();
        VisitedPhis.insert(NextDeadValue);
        auto OriginalPhi =
            std::find_if(PhiNodes.begin(), PhiNodes.end(),
                         [this, &NextDeadValue](PHINode *CandPhi) {
                           return ValMap[CandPhi] == NextDeadValue;
                         });
        // This PHI may have already been removed from maps when
        // unwinding a previous Phi
        if (OriginalPhi != PhiNodes.end())
          ValMap.erase(*OriginalPhi);

        DeadInsts.emplace_back(cast<Instruction>(NextDeadValue));

        for (User *U : NextDeadValue->users()) {
          if (!VisitedPhis.contains(cast<PHINode>(U)))
            PHIWorklist.push_back(U);
        }
      }
    } else {
      DeadInsts.emplace_back(cast<Instruction>(Phi));
    }
  }
  // Coerce back to the original type and replace the uses.
  for (Instruction *U : Uses) {
    // Replace all converted operands for a use.
    for (auto [OpIdx, Op] : enumerate(U->operands())) {
      if (ValMap.contains(Op) && ValMap[Op]) {
        Value *NewVal = nullptr;
        if (BBUseValMap.contains(U->getParent()) &&
            BBUseValMap[U->getParent()].contains(ValMap[Op]))
          NewVal = BBUseValMap[U->getParent()][ValMap[Op]];
        else {
          BasicBlock::iterator InsertPt = U->getParent()->getFirstNonPHIIt();
          // We may pick up ops that were previously converted for users in
          // other blocks. If there is an originally typed definition of the Op
          // already in this block, simply reuse it.
          if (isa<Instruction>(Op) && !isa<PHINode>(Op) &&
              U->getParent() == cast<Instruction>(Op)->getParent()) {
            NewVal = Op;
          } else {
            NewVal =
                convertFromOptType(Op->getType(), cast<Instruction>(ValMap[Op]),
                                   InsertPt, U->getParent());
            BBUseValMap[U->getParent()][ValMap[Op]] = NewVal;
          }
        }
        assert(NewVal);
        U->setOperand(OpIdx, NewVal);
      }
    }
  }

  return true;
}

bool AMDGPULateCodeGenPrepare::canWidenScalarExtLoad(LoadInst &LI) const {
  unsigned AS = LI.getPointerAddressSpace();
  // Skip non-constant address space.
  if (AS != AMDGPUAS::CONSTANT_ADDRESS &&
      AS != AMDGPUAS::CONSTANT_ADDRESS_32BIT)
    return false;
  // Skip non-simple loads.
  if (!LI.isSimple())
    return false;
  Type *Ty = LI.getType();
  // Skip aggregate types.
  if (Ty->isAggregateType())
    return false;
  unsigned TySize = DL->getTypeStoreSize(Ty);
  // Only handle sub-DWORD loads.
  if (TySize >= 4)
    return false;
  // That load must be at least naturally aligned.
  if (LI.getAlign() < DL->getABITypeAlign(Ty))
    return false;
  // It should be uniform, i.e. a scalar load.
  return UA->isUniform(&LI);
}

bool AMDGPULateCodeGenPrepare::visitLoadInst(LoadInst &LI) {
  if (!WidenLoads)
    return false;

  // Skip if that load is already aligned on DWORD at least as it's handled in
  // SDAG.
  if (LI.getAlign() >= 4)
    return false;

  if (!canWidenScalarExtLoad(LI))
    return false;

  int64_t Offset = 0;
  auto *Base =
      GetPointerBaseWithConstantOffset(LI.getPointerOperand(), Offset, *DL);
  // If that base is not DWORD aligned, it's not safe to perform the following
  // transforms.
  if (!isDWORDAligned(Base))
    return false;

  int64_t Adjust = Offset & 0x3;
  if (Adjust == 0) {
    // With a zero adjust, the original alignment could be promoted with a
    // better one.
    LI.setAlignment(Align(4));
    return true;
  }

  IRBuilder<> IRB(&LI);
  IRB.SetCurrentDebugLocation(LI.getDebugLoc());

  unsigned LdBits = DL->getTypeStoreSizeInBits(LI.getType());
  auto IntNTy = Type::getIntNTy(LI.getContext(), LdBits);

  auto *NewPtr = IRB.CreateConstGEP1_64(
      IRB.getInt8Ty(),
      IRB.CreateAddrSpaceCast(Base, LI.getPointerOperand()->getType()),
      Offset - Adjust);

  LoadInst *NewLd = IRB.CreateAlignedLoad(IRB.getInt32Ty(), NewPtr, Align(4));
  NewLd->copyMetadata(LI);
  NewLd->setMetadata(LLVMContext::MD_range, nullptr);

  unsigned ShAmt = Adjust * 8;
  auto *NewVal = IRB.CreateBitCast(
      IRB.CreateTrunc(IRB.CreateLShr(NewLd, ShAmt), IntNTy), LI.getType());
  LI.replaceAllUsesWith(NewVal);
  DeadInsts.emplace_back(&LI);

  return true;
}

INITIALIZE_PASS_BEGIN(AMDGPULateCodeGenPrepare, DEBUG_TYPE,
                      "AMDGPU IR late optimizations", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(UniformityInfoWrapperPass)
INITIALIZE_PASS_END(AMDGPULateCodeGenPrepare, DEBUG_TYPE,
                    "AMDGPU IR late optimizations", false, false)

char AMDGPULateCodeGenPrepare::ID = 0;

FunctionPass *llvm::createAMDGPULateCodeGenPreparePass() {
  return new AMDGPULateCodeGenPrepare();
}
