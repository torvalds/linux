//===- InferAddressSpace.cpp - --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CUDA C/C++ includes memory space designation as variable type qualifers (such
// as __global__ and __shared__). Knowing the space of a memory access allows
// CUDA compilers to emit faster PTX loads and stores. For example, a load from
// shared memory can be translated to `ld.shared` which is roughly 10% faster
// than a generic `ld` on an NVIDIA Tesla K40c.
//
// Unfortunately, type qualifiers only apply to variable declarations, so CUDA
// compilers must infer the memory space of an address expression from
// type-qualified variables.
//
// LLVM IR uses non-zero (so-called) specific address spaces to represent memory
// spaces (e.g. addrspace(3) means shared memory). The Clang frontend
// places only type-qualified variables in specific address spaces, and then
// conservatively `addrspacecast`s each type-qualified variable to addrspace(0)
// (so-called the generic address space) for other instructions to use.
//
// For example, the Clang translates the following CUDA code
//   __shared__ float a[10];
//   float v = a[i];
// to
//   %0 = addrspacecast [10 x float] addrspace(3)* @a to [10 x float]*
//   %1 = gep [10 x float], [10 x float]* %0, i64 0, i64 %i
//   %v = load float, float* %1 ; emits ld.f32
// @a is in addrspace(3) since it's type-qualified, but its use from %1 is
// redirected to %0 (the generic version of @a).
//
// The optimization implemented in this file propagates specific address spaces
// from type-qualified variable declarations to its users. For example, it
// optimizes the above IR to
//   %1 = gep [10 x float] addrspace(3)* @a, i64 0, i64 %i
//   %v = load float addrspace(3)* %1 ; emits ld.shared.f32
// propagating the addrspace(3) from @a to %1. As the result, the NVPTX
// codegen is able to emit ld.shared.f32 for %v.
//
// Address space inference works in two steps. First, it uses a data-flow
// analysis to infer as many generic pointers as possible to point to only one
// specific address space. In the above example, it can prove that %1 only
// points to addrspace(3). This algorithm was published in
//   CUDA: Compiling and optimizing for a GPU platform
//   Chakrabarti, Grover, Aarts, Kong, Kudlur, Lin, Marathe, Murphy, Wang
//   ICCS 2012
//
// Then, address space inference replaces all refinable generic pointers with
// equivalent specific pointers.
//
// The major challenge of implementing this optimization is handling PHINodes,
// which may create loops in the data flow graph. This brings two complications.
//
// First, the data flow analysis in Step 1 needs to be circular. For example,
//     %generic.input = addrspacecast float addrspace(3)* %input to float*
//   loop:
//     %y = phi [ %generic.input, %y2 ]
//     %y2 = getelementptr %y, 1
//     %v = load %y2
//     br ..., label %loop, ...
// proving %y specific requires proving both %generic.input and %y2 specific,
// but proving %y2 specific circles back to %y. To address this complication,
// the data flow analysis operates on a lattice:
//   uninitialized > specific address spaces > generic.
// All address expressions (our implementation only considers phi, bitcast,
// addrspacecast, and getelementptr) start with the uninitialized address space.
// The monotone transfer function moves the address space of a pointer down a
// lattice path from uninitialized to specific and then to generic. A join
// operation of two different specific address spaces pushes the expression down
// to the generic address space. The analysis completes once it reaches a fixed
// point.
//
// Second, IR rewriting in Step 2 also needs to be circular. For example,
// converting %y to addrspace(3) requires the compiler to know the converted
// %y2, but converting %y2 needs the converted %y. To address this complication,
// we break these cycles using "poison" placeholders. When converting an
// instruction `I` to a new address space, if its operand `Op` is not converted
// yet, we let `I` temporarily use `poison` and fix all the uses later.
// For instance, our algorithm first converts %y to
//   %y' = phi float addrspace(3)* [ %input, poison ]
// Then, it converts %y2 to
//   %y2' = getelementptr %y', 1
// Finally, it fixes the poison in %y' so that
//   %y' = phi float addrspace(3)* [ %input, %y2' ]
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/InferAddressSpaces.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#define DEBUG_TYPE "infer-address-spaces"

using namespace llvm;

static cl::opt<bool> AssumeDefaultIsFlatAddressSpace(
    "assume-default-is-flat-addrspace", cl::init(false), cl::ReallyHidden,
    cl::desc("The default address space is assumed as the flat address space. "
             "This is mainly for test purpose."));

static const unsigned UninitializedAddressSpace =
    std::numeric_limits<unsigned>::max();

namespace {

using ValueToAddrSpaceMapTy = DenseMap<const Value *, unsigned>;
// Different from ValueToAddrSpaceMapTy, where a new addrspace is inferred on
// the *def* of a value, PredicatedAddrSpaceMapTy is map where a new
// addrspace is inferred on the *use* of a pointer. This map is introduced to
// infer addrspace from the addrspace predicate assumption built from assume
// intrinsic. In that scenario, only specific uses (under valid assumption
// context) could be inferred with a new addrspace.
using PredicatedAddrSpaceMapTy =
    DenseMap<std::pair<const Value *, const Value *>, unsigned>;
using PostorderStackTy = llvm::SmallVector<PointerIntPair<Value *, 1, bool>, 4>;

class InferAddressSpaces : public FunctionPass {
  unsigned FlatAddrSpace = 0;

public:
  static char ID;

  InferAddressSpaces()
      : FunctionPass(ID), FlatAddrSpace(UninitializedAddressSpace) {
    initializeInferAddressSpacesPass(*PassRegistry::getPassRegistry());
  }
  InferAddressSpaces(unsigned AS) : FunctionPass(ID), FlatAddrSpace(AS) {
    initializeInferAddressSpacesPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

  bool runOnFunction(Function &F) override;
};

class InferAddressSpacesImpl {
  AssumptionCache &AC;
  const DominatorTree *DT = nullptr;
  const TargetTransformInfo *TTI = nullptr;
  const DataLayout *DL = nullptr;

  /// Target specific address space which uses of should be replaced if
  /// possible.
  unsigned FlatAddrSpace = 0;

  // Try to update the address space of V. If V is updated, returns true and
  // false otherwise.
  bool updateAddressSpace(const Value &V,
                          ValueToAddrSpaceMapTy &InferredAddrSpace,
                          PredicatedAddrSpaceMapTy &PredicatedAS) const;

  // Tries to infer the specific address space of each address expression in
  // Postorder.
  void inferAddressSpaces(ArrayRef<WeakTrackingVH> Postorder,
                          ValueToAddrSpaceMapTy &InferredAddrSpace,
                          PredicatedAddrSpaceMapTy &PredicatedAS) const;

  bool isSafeToCastConstAddrSpace(Constant *C, unsigned NewAS) const;

  Value *cloneInstructionWithNewAddressSpace(
      Instruction *I, unsigned NewAddrSpace,
      const ValueToValueMapTy &ValueWithNewAddrSpace,
      const PredicatedAddrSpaceMapTy &PredicatedAS,
      SmallVectorImpl<const Use *> *PoisonUsesToFix) const;

  // Changes the flat address expressions in function F to point to specific
  // address spaces if InferredAddrSpace says so. Postorder is the postorder of
  // all flat expressions in the use-def graph of function F.
  bool
  rewriteWithNewAddressSpaces(ArrayRef<WeakTrackingVH> Postorder,
                              const ValueToAddrSpaceMapTy &InferredAddrSpace,
                              const PredicatedAddrSpaceMapTy &PredicatedAS,
                              Function *F) const;

  void appendsFlatAddressExpressionToPostorderStack(
      Value *V, PostorderStackTy &PostorderStack,
      DenseSet<Value *> &Visited) const;

  bool rewriteIntrinsicOperands(IntrinsicInst *II, Value *OldV,
                                Value *NewV) const;
  void collectRewritableIntrinsicOperands(IntrinsicInst *II,
                                          PostorderStackTy &PostorderStack,
                                          DenseSet<Value *> &Visited) const;

  std::vector<WeakTrackingVH> collectFlatAddressExpressions(Function &F) const;

  Value *cloneValueWithNewAddressSpace(
      Value *V, unsigned NewAddrSpace,
      const ValueToValueMapTy &ValueWithNewAddrSpace,
      const PredicatedAddrSpaceMapTy &PredicatedAS,
      SmallVectorImpl<const Use *> *PoisonUsesToFix) const;
  unsigned joinAddressSpaces(unsigned AS1, unsigned AS2) const;

  unsigned getPredicatedAddrSpace(const Value &V, Value *Opnd) const;

public:
  InferAddressSpacesImpl(AssumptionCache &AC, const DominatorTree *DT,
                         const TargetTransformInfo *TTI, unsigned FlatAddrSpace)
      : AC(AC), DT(DT), TTI(TTI), FlatAddrSpace(FlatAddrSpace) {}
  bool run(Function &F);
};

} // end anonymous namespace

char InferAddressSpaces::ID = 0;

INITIALIZE_PASS_BEGIN(InferAddressSpaces, DEBUG_TYPE, "Infer address spaces",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(InferAddressSpaces, DEBUG_TYPE, "Infer address spaces",
                    false, false)

static Type *getPtrOrVecOfPtrsWithNewAS(Type *Ty, unsigned NewAddrSpace) {
  assert(Ty->isPtrOrPtrVectorTy());
  PointerType *NPT = PointerType::get(Ty->getContext(), NewAddrSpace);
  return Ty->getWithNewType(NPT);
}

// Check whether that's no-op pointer bicast using a pair of
// `ptrtoint`/`inttoptr` due to the missing no-op pointer bitcast over
// different address spaces.
static bool isNoopPtrIntCastPair(const Operator *I2P, const DataLayout &DL,
                                 const TargetTransformInfo *TTI) {
  assert(I2P->getOpcode() == Instruction::IntToPtr);
  auto *P2I = dyn_cast<Operator>(I2P->getOperand(0));
  if (!P2I || P2I->getOpcode() != Instruction::PtrToInt)
    return false;
  // Check it's really safe to treat that pair of `ptrtoint`/`inttoptr` as a
  // no-op cast. Besides checking both of them are no-op casts, as the
  // reinterpreted pointer may be used in other pointer arithmetic, we also
  // need to double-check that through the target-specific hook. That ensures
  // the underlying target also agrees that's a no-op address space cast and
  // pointer bits are preserved.
  // The current IR spec doesn't have clear rules on address space casts,
  // especially a clear definition for pointer bits in non-default address
  // spaces. It would be undefined if that pointer is dereferenced after an
  // invalid reinterpret cast. Also, due to the unclearness for the meaning of
  // bits in non-default address spaces in the current spec, the pointer
  // arithmetic may also be undefined after invalid pointer reinterpret cast.
  // However, as we confirm through the target hooks that it's a no-op
  // addrspacecast, it doesn't matter since the bits should be the same.
  unsigned P2IOp0AS = P2I->getOperand(0)->getType()->getPointerAddressSpace();
  unsigned I2PAS = I2P->getType()->getPointerAddressSpace();
  return CastInst::isNoopCast(Instruction::CastOps(I2P->getOpcode()),
                              I2P->getOperand(0)->getType(), I2P->getType(),
                              DL) &&
         CastInst::isNoopCast(Instruction::CastOps(P2I->getOpcode()),
                              P2I->getOperand(0)->getType(), P2I->getType(),
                              DL) &&
         (P2IOp0AS == I2PAS || TTI->isNoopAddrSpaceCast(P2IOp0AS, I2PAS));
}

// Returns true if V is an address expression.
// TODO: Currently, we consider only phi, bitcast, addrspacecast, and
// getelementptr operators.
static bool isAddressExpression(const Value &V, const DataLayout &DL,
                                const TargetTransformInfo *TTI) {
  const Operator *Op = dyn_cast<Operator>(&V);
  if (!Op)
    return false;

  switch (Op->getOpcode()) {
  case Instruction::PHI:
    assert(Op->getType()->isPtrOrPtrVectorTy());
    return true;
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  case Instruction::GetElementPtr:
    return true;
  case Instruction::Select:
    return Op->getType()->isPtrOrPtrVectorTy();
  case Instruction::Call: {
    const IntrinsicInst *II = dyn_cast<IntrinsicInst>(&V);
    return II && II->getIntrinsicID() == Intrinsic::ptrmask;
  }
  case Instruction::IntToPtr:
    return isNoopPtrIntCastPair(Op, DL, TTI);
  default:
    // That value is an address expression if it has an assumed address space.
    return TTI->getAssumedAddrSpace(&V) != UninitializedAddressSpace;
  }
}

// Returns the pointer operands of V.
//
// Precondition: V is an address expression.
static SmallVector<Value *, 2>
getPointerOperands(const Value &V, const DataLayout &DL,
                   const TargetTransformInfo *TTI) {
  const Operator &Op = cast<Operator>(V);
  switch (Op.getOpcode()) {
  case Instruction::PHI: {
    auto IncomingValues = cast<PHINode>(Op).incoming_values();
    return {IncomingValues.begin(), IncomingValues.end()};
  }
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  case Instruction::GetElementPtr:
    return {Op.getOperand(0)};
  case Instruction::Select:
    return {Op.getOperand(1), Op.getOperand(2)};
  case Instruction::Call: {
    const IntrinsicInst &II = cast<IntrinsicInst>(Op);
    assert(II.getIntrinsicID() == Intrinsic::ptrmask &&
           "unexpected intrinsic call");
    return {II.getArgOperand(0)};
  }
  case Instruction::IntToPtr: {
    assert(isNoopPtrIntCastPair(&Op, DL, TTI));
    auto *P2I = cast<Operator>(Op.getOperand(0));
    return {P2I->getOperand(0)};
  }
  default:
    llvm_unreachable("Unexpected instruction type.");
  }
}

bool InferAddressSpacesImpl::rewriteIntrinsicOperands(IntrinsicInst *II,
                                                      Value *OldV,
                                                      Value *NewV) const {
  Module *M = II->getParent()->getParent()->getParent();

  switch (II->getIntrinsicID()) {
  case Intrinsic::objectsize: {
    Type *DestTy = II->getType();
    Type *SrcTy = NewV->getType();
    Function *NewDecl =
        Intrinsic::getDeclaration(M, II->getIntrinsicID(), {DestTy, SrcTy});
    II->setArgOperand(0, NewV);
    II->setCalledFunction(NewDecl);
    return true;
  }
  case Intrinsic::ptrmask:
    // This is handled as an address expression, not as a use memory operation.
    return false;
  case Intrinsic::masked_gather: {
    Type *RetTy = II->getType();
    Type *NewPtrTy = NewV->getType();
    Function *NewDecl =
        Intrinsic::getDeclaration(M, II->getIntrinsicID(), {RetTy, NewPtrTy});
    II->setArgOperand(0, NewV);
    II->setCalledFunction(NewDecl);
    return true;
  }
  case Intrinsic::masked_scatter: {
    Type *ValueTy = II->getOperand(0)->getType();
    Type *NewPtrTy = NewV->getType();
    Function *NewDecl =
        Intrinsic::getDeclaration(M, II->getIntrinsicID(), {ValueTy, NewPtrTy});
    II->setArgOperand(1, NewV);
    II->setCalledFunction(NewDecl);
    return true;
  }
  default: {
    Value *Rewrite = TTI->rewriteIntrinsicWithAddressSpace(II, OldV, NewV);
    if (!Rewrite)
      return false;
    if (Rewrite != II)
      II->replaceAllUsesWith(Rewrite);
    return true;
  }
  }
}

void InferAddressSpacesImpl::collectRewritableIntrinsicOperands(
    IntrinsicInst *II, PostorderStackTy &PostorderStack,
    DenseSet<Value *> &Visited) const {
  auto IID = II->getIntrinsicID();
  switch (IID) {
  case Intrinsic::ptrmask:
  case Intrinsic::objectsize:
    appendsFlatAddressExpressionToPostorderStack(II->getArgOperand(0),
                                                 PostorderStack, Visited);
    break;
  case Intrinsic::masked_gather:
    appendsFlatAddressExpressionToPostorderStack(II->getArgOperand(0),
                                                 PostorderStack, Visited);
    break;
  case Intrinsic::masked_scatter:
    appendsFlatAddressExpressionToPostorderStack(II->getArgOperand(1),
                                                 PostorderStack, Visited);
    break;
  default:
    SmallVector<int, 2> OpIndexes;
    if (TTI->collectFlatAddressOperands(OpIndexes, IID)) {
      for (int Idx : OpIndexes) {
        appendsFlatAddressExpressionToPostorderStack(II->getArgOperand(Idx),
                                                     PostorderStack, Visited);
      }
    }
    break;
  }
}

// Returns all flat address expressions in function F. The elements are
// If V is an unvisited flat address expression, appends V to PostorderStack
// and marks it as visited.
void InferAddressSpacesImpl::appendsFlatAddressExpressionToPostorderStack(
    Value *V, PostorderStackTy &PostorderStack,
    DenseSet<Value *> &Visited) const {
  assert(V->getType()->isPtrOrPtrVectorTy());

  // Generic addressing expressions may be hidden in nested constant
  // expressions.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    // TODO: Look in non-address parts, like icmp operands.
    if (isAddressExpression(*CE, *DL, TTI) && Visited.insert(CE).second)
      PostorderStack.emplace_back(CE, false);

    return;
  }

  if (V->getType()->getPointerAddressSpace() == FlatAddrSpace &&
      isAddressExpression(*V, *DL, TTI)) {
    if (Visited.insert(V).second) {
      PostorderStack.emplace_back(V, false);

      Operator *Op = cast<Operator>(V);
      for (unsigned I = 0, E = Op->getNumOperands(); I != E; ++I) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Op->getOperand(I))) {
          if (isAddressExpression(*CE, *DL, TTI) && Visited.insert(CE).second)
            PostorderStack.emplace_back(CE, false);
        }
      }
    }
  }
}

// Returns all flat address expressions in function F. The elements are ordered
// in postorder.
std::vector<WeakTrackingVH>
InferAddressSpacesImpl::collectFlatAddressExpressions(Function &F) const {
  // This function implements a non-recursive postorder traversal of a partial
  // use-def graph of function F.
  PostorderStackTy PostorderStack;
  // The set of visited expressions.
  DenseSet<Value *> Visited;

  auto PushPtrOperand = [&](Value *Ptr) {
    appendsFlatAddressExpressionToPostorderStack(Ptr, PostorderStack, Visited);
  };

  // Look at operations that may be interesting accelerate by moving to a known
  // address space. We aim at generating after loads and stores, but pure
  // addressing calculations may also be faster.
  for (Instruction &I : instructions(F)) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
      PushPtrOperand(GEP->getPointerOperand());
    } else if (auto *LI = dyn_cast<LoadInst>(&I))
      PushPtrOperand(LI->getPointerOperand());
    else if (auto *SI = dyn_cast<StoreInst>(&I))
      PushPtrOperand(SI->getPointerOperand());
    else if (auto *RMW = dyn_cast<AtomicRMWInst>(&I))
      PushPtrOperand(RMW->getPointerOperand());
    else if (auto *CmpX = dyn_cast<AtomicCmpXchgInst>(&I))
      PushPtrOperand(CmpX->getPointerOperand());
    else if (auto *MI = dyn_cast<MemIntrinsic>(&I)) {
      // For memset/memcpy/memmove, any pointer operand can be replaced.
      PushPtrOperand(MI->getRawDest());

      // Handle 2nd operand for memcpy/memmove.
      if (auto *MTI = dyn_cast<MemTransferInst>(MI))
        PushPtrOperand(MTI->getRawSource());
    } else if (auto *II = dyn_cast<IntrinsicInst>(&I))
      collectRewritableIntrinsicOperands(II, PostorderStack, Visited);
    else if (ICmpInst *Cmp = dyn_cast<ICmpInst>(&I)) {
      if (Cmp->getOperand(0)->getType()->isPtrOrPtrVectorTy()) {
        PushPtrOperand(Cmp->getOperand(0));
        PushPtrOperand(Cmp->getOperand(1));
      }
    } else if (auto *ASC = dyn_cast<AddrSpaceCastInst>(&I)) {
      PushPtrOperand(ASC->getPointerOperand());
    } else if (auto *I2P = dyn_cast<IntToPtrInst>(&I)) {
      if (isNoopPtrIntCastPair(cast<Operator>(I2P), *DL, TTI))
        PushPtrOperand(cast<Operator>(I2P->getOperand(0))->getOperand(0));
    } else if (auto *RI = dyn_cast<ReturnInst>(&I)) {
      if (auto *RV = RI->getReturnValue();
          RV && RV->getType()->isPtrOrPtrVectorTy())
        PushPtrOperand(RV);
    }
  }

  std::vector<WeakTrackingVH> Postorder; // The resultant postorder.
  while (!PostorderStack.empty()) {
    Value *TopVal = PostorderStack.back().getPointer();
    // If the operands of the expression on the top are already explored,
    // adds that expression to the resultant postorder.
    if (PostorderStack.back().getInt()) {
      if (TopVal->getType()->getPointerAddressSpace() == FlatAddrSpace)
        Postorder.push_back(TopVal);
      PostorderStack.pop_back();
      continue;
    }
    // Otherwise, adds its operands to the stack and explores them.
    PostorderStack.back().setInt(true);
    // Skip values with an assumed address space.
    if (TTI->getAssumedAddrSpace(TopVal) == UninitializedAddressSpace) {
      for (Value *PtrOperand : getPointerOperands(*TopVal, *DL, TTI)) {
        appendsFlatAddressExpressionToPostorderStack(PtrOperand, PostorderStack,
                                                     Visited);
      }
    }
  }
  return Postorder;
}

// A helper function for cloneInstructionWithNewAddressSpace. Returns the clone
// of OperandUse.get() in the new address space. If the clone is not ready yet,
// returns poison in the new address space as a placeholder.
static Value *operandWithNewAddressSpaceOrCreatePoison(
    const Use &OperandUse, unsigned NewAddrSpace,
    const ValueToValueMapTy &ValueWithNewAddrSpace,
    const PredicatedAddrSpaceMapTy &PredicatedAS,
    SmallVectorImpl<const Use *> *PoisonUsesToFix) {
  Value *Operand = OperandUse.get();

  Type *NewPtrTy = getPtrOrVecOfPtrsWithNewAS(Operand->getType(), NewAddrSpace);

  if (Constant *C = dyn_cast<Constant>(Operand))
    return ConstantExpr::getAddrSpaceCast(C, NewPtrTy);

  if (Value *NewOperand = ValueWithNewAddrSpace.lookup(Operand))
    return NewOperand;

  Instruction *Inst = cast<Instruction>(OperandUse.getUser());
  auto I = PredicatedAS.find(std::make_pair(Inst, Operand));
  if (I != PredicatedAS.end()) {
    // Insert an addrspacecast on that operand before the user.
    unsigned NewAS = I->second;
    Type *NewPtrTy = getPtrOrVecOfPtrsWithNewAS(Operand->getType(), NewAS);
    auto *NewI = new AddrSpaceCastInst(Operand, NewPtrTy);
    NewI->insertBefore(Inst);
    NewI->setDebugLoc(Inst->getDebugLoc());
    return NewI;
  }

  PoisonUsesToFix->push_back(&OperandUse);
  return PoisonValue::get(NewPtrTy);
}

// Returns a clone of `I` with its operands converted to those specified in
// ValueWithNewAddrSpace. Due to potential cycles in the data flow graph, an
// operand whose address space needs to be modified might not exist in
// ValueWithNewAddrSpace. In that case, uses poison as a placeholder operand and
// adds that operand use to PoisonUsesToFix so that caller can fix them later.
//
// Note that we do not necessarily clone `I`, e.g., if it is an addrspacecast
// from a pointer whose type already matches. Therefore, this function returns a
// Value* instead of an Instruction*.
//
// This may also return nullptr in the case the instruction could not be
// rewritten.
Value *InferAddressSpacesImpl::cloneInstructionWithNewAddressSpace(
    Instruction *I, unsigned NewAddrSpace,
    const ValueToValueMapTy &ValueWithNewAddrSpace,
    const PredicatedAddrSpaceMapTy &PredicatedAS,
    SmallVectorImpl<const Use *> *PoisonUsesToFix) const {
  Type *NewPtrType = getPtrOrVecOfPtrsWithNewAS(I->getType(), NewAddrSpace);

  if (I->getOpcode() == Instruction::AddrSpaceCast) {
    Value *Src = I->getOperand(0);
    // Because `I` is flat, the source address space must be specific.
    // Therefore, the inferred address space must be the source space, according
    // to our algorithm.
    assert(Src->getType()->getPointerAddressSpace() == NewAddrSpace);
    if (Src->getType() != NewPtrType)
      return new BitCastInst(Src, NewPtrType);
    return Src;
  }

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    // Technically the intrinsic ID is a pointer typed argument, so specially
    // handle calls early.
    assert(II->getIntrinsicID() == Intrinsic::ptrmask);
    Value *NewPtr = operandWithNewAddressSpaceOrCreatePoison(
        II->getArgOperandUse(0), NewAddrSpace, ValueWithNewAddrSpace,
        PredicatedAS, PoisonUsesToFix);
    Value *Rewrite =
        TTI->rewriteIntrinsicWithAddressSpace(II, II->getArgOperand(0), NewPtr);
    if (Rewrite) {
      assert(Rewrite != II && "cannot modify this pointer operation in place");
      return Rewrite;
    }

    return nullptr;
  }

  unsigned AS = TTI->getAssumedAddrSpace(I);
  if (AS != UninitializedAddressSpace) {
    // For the assumed address space, insert an `addrspacecast` to make that
    // explicit.
    Type *NewPtrTy = getPtrOrVecOfPtrsWithNewAS(I->getType(), AS);
    auto *NewI = new AddrSpaceCastInst(I, NewPtrTy);
    NewI->insertAfter(I);
    NewI->setDebugLoc(I->getDebugLoc());
    return NewI;
  }

  // Computes the converted pointer operands.
  SmallVector<Value *, 4> NewPointerOperands;
  for (const Use &OperandUse : I->operands()) {
    if (!OperandUse.get()->getType()->isPtrOrPtrVectorTy())
      NewPointerOperands.push_back(nullptr);
    else
      NewPointerOperands.push_back(operandWithNewAddressSpaceOrCreatePoison(
          OperandUse, NewAddrSpace, ValueWithNewAddrSpace, PredicatedAS,
          PoisonUsesToFix));
  }

  switch (I->getOpcode()) {
  case Instruction::BitCast:
    return new BitCastInst(NewPointerOperands[0], NewPtrType);
  case Instruction::PHI: {
    assert(I->getType()->isPtrOrPtrVectorTy());
    PHINode *PHI = cast<PHINode>(I);
    PHINode *NewPHI = PHINode::Create(NewPtrType, PHI->getNumIncomingValues());
    for (unsigned Index = 0; Index < PHI->getNumIncomingValues(); ++Index) {
      unsigned OperandNo = PHINode::getOperandNumForIncomingValue(Index);
      NewPHI->addIncoming(NewPointerOperands[OperandNo],
                          PHI->getIncomingBlock(Index));
    }
    return NewPHI;
  }
  case Instruction::GetElementPtr: {
    GetElementPtrInst *GEP = cast<GetElementPtrInst>(I);
    GetElementPtrInst *NewGEP = GetElementPtrInst::Create(
        GEP->getSourceElementType(), NewPointerOperands[0],
        SmallVector<Value *, 4>(GEP->indices()));
    NewGEP->setIsInBounds(GEP->isInBounds());
    return NewGEP;
  }
  case Instruction::Select:
    assert(I->getType()->isPtrOrPtrVectorTy());
    return SelectInst::Create(I->getOperand(0), NewPointerOperands[1],
                              NewPointerOperands[2], "", nullptr, I);
  case Instruction::IntToPtr: {
    assert(isNoopPtrIntCastPair(cast<Operator>(I), *DL, TTI));
    Value *Src = cast<Operator>(I->getOperand(0))->getOperand(0);
    if (Src->getType() == NewPtrType)
      return Src;

    // If we had a no-op inttoptr/ptrtoint pair, we may still have inferred a
    // source address space from a generic pointer source need to insert a cast
    // back.
    return CastInst::CreatePointerBitCastOrAddrSpaceCast(Src, NewPtrType);
  }
  default:
    llvm_unreachable("Unexpected opcode");
  }
}

// Similar to cloneInstructionWithNewAddressSpace, returns a clone of the
// constant expression `CE` with its operands replaced as specified in
// ValueWithNewAddrSpace.
static Value *cloneConstantExprWithNewAddressSpace(
    ConstantExpr *CE, unsigned NewAddrSpace,
    const ValueToValueMapTy &ValueWithNewAddrSpace, const DataLayout *DL,
    const TargetTransformInfo *TTI) {
  Type *TargetType =
      CE->getType()->isPtrOrPtrVectorTy()
          ? getPtrOrVecOfPtrsWithNewAS(CE->getType(), NewAddrSpace)
          : CE->getType();

  if (CE->getOpcode() == Instruction::AddrSpaceCast) {
    // Because CE is flat, the source address space must be specific.
    // Therefore, the inferred address space must be the source space according
    // to our algorithm.
    assert(CE->getOperand(0)->getType()->getPointerAddressSpace() ==
           NewAddrSpace);
    return ConstantExpr::getBitCast(CE->getOperand(0), TargetType);
  }

  if (CE->getOpcode() == Instruction::BitCast) {
    if (Value *NewOperand = ValueWithNewAddrSpace.lookup(CE->getOperand(0)))
      return ConstantExpr::getBitCast(cast<Constant>(NewOperand), TargetType);
    return ConstantExpr::getAddrSpaceCast(CE, TargetType);
  }

  if (CE->getOpcode() == Instruction::IntToPtr) {
    assert(isNoopPtrIntCastPair(cast<Operator>(CE), *DL, TTI));
    Constant *Src = cast<ConstantExpr>(CE->getOperand(0))->getOperand(0);
    assert(Src->getType()->getPointerAddressSpace() == NewAddrSpace);
    return ConstantExpr::getBitCast(Src, TargetType);
  }

  // Computes the operands of the new constant expression.
  bool IsNew = false;
  SmallVector<Constant *, 4> NewOperands;
  for (unsigned Index = 0; Index < CE->getNumOperands(); ++Index) {
    Constant *Operand = CE->getOperand(Index);
    // If the address space of `Operand` needs to be modified, the new operand
    // with the new address space should already be in ValueWithNewAddrSpace
    // because (1) the constant expressions we consider (i.e. addrspacecast,
    // bitcast, and getelementptr) do not incur cycles in the data flow graph
    // and (2) this function is called on constant expressions in postorder.
    if (Value *NewOperand = ValueWithNewAddrSpace.lookup(Operand)) {
      IsNew = true;
      NewOperands.push_back(cast<Constant>(NewOperand));
      continue;
    }
    if (auto *CExpr = dyn_cast<ConstantExpr>(Operand))
      if (Value *NewOperand = cloneConstantExprWithNewAddressSpace(
              CExpr, NewAddrSpace, ValueWithNewAddrSpace, DL, TTI)) {
        IsNew = true;
        NewOperands.push_back(cast<Constant>(NewOperand));
        continue;
      }
    // Otherwise, reuses the old operand.
    NewOperands.push_back(Operand);
  }

  // If !IsNew, we will replace the Value with itself. However, replaced values
  // are assumed to wrapped in an addrspacecast cast later so drop it now.
  if (!IsNew)
    return nullptr;

  if (CE->getOpcode() == Instruction::GetElementPtr) {
    // Needs to specify the source type while constructing a getelementptr
    // constant expression.
    return CE->getWithOperands(NewOperands, TargetType, /*OnlyIfReduced=*/false,
                               cast<GEPOperator>(CE)->getSourceElementType());
  }

  return CE->getWithOperands(NewOperands, TargetType);
}

// Returns a clone of the value `V`, with its operands replaced as specified in
// ValueWithNewAddrSpace. This function is called on every flat address
// expression whose address space needs to be modified, in postorder.
//
// See cloneInstructionWithNewAddressSpace for the meaning of PoisonUsesToFix.
Value *InferAddressSpacesImpl::cloneValueWithNewAddressSpace(
    Value *V, unsigned NewAddrSpace,
    const ValueToValueMapTy &ValueWithNewAddrSpace,
    const PredicatedAddrSpaceMapTy &PredicatedAS,
    SmallVectorImpl<const Use *> *PoisonUsesToFix) const {
  // All values in Postorder are flat address expressions.
  assert(V->getType()->getPointerAddressSpace() == FlatAddrSpace &&
         isAddressExpression(*V, *DL, TTI));

  if (Instruction *I = dyn_cast<Instruction>(V)) {
    Value *NewV = cloneInstructionWithNewAddressSpace(
        I, NewAddrSpace, ValueWithNewAddrSpace, PredicatedAS, PoisonUsesToFix);
    if (Instruction *NewI = dyn_cast_or_null<Instruction>(NewV)) {
      if (NewI->getParent() == nullptr) {
        NewI->insertBefore(I);
        NewI->takeName(I);
        NewI->setDebugLoc(I->getDebugLoc());
      }
    }
    return NewV;
  }

  return cloneConstantExprWithNewAddressSpace(
      cast<ConstantExpr>(V), NewAddrSpace, ValueWithNewAddrSpace, DL, TTI);
}

// Defines the join operation on the address space lattice (see the file header
// comments).
unsigned InferAddressSpacesImpl::joinAddressSpaces(unsigned AS1,
                                                   unsigned AS2) const {
  if (AS1 == FlatAddrSpace || AS2 == FlatAddrSpace)
    return FlatAddrSpace;

  if (AS1 == UninitializedAddressSpace)
    return AS2;
  if (AS2 == UninitializedAddressSpace)
    return AS1;

  // The join of two different specific address spaces is flat.
  return (AS1 == AS2) ? AS1 : FlatAddrSpace;
}

bool InferAddressSpacesImpl::run(Function &F) {
  DL = &F.getDataLayout();

  if (AssumeDefaultIsFlatAddressSpace)
    FlatAddrSpace = 0;

  if (FlatAddrSpace == UninitializedAddressSpace) {
    FlatAddrSpace = TTI->getFlatAddressSpace();
    if (FlatAddrSpace == UninitializedAddressSpace)
      return false;
  }

  // Collects all flat address expressions in postorder.
  std::vector<WeakTrackingVH> Postorder = collectFlatAddressExpressions(F);

  // Runs a data-flow analysis to refine the address spaces of every expression
  // in Postorder.
  ValueToAddrSpaceMapTy InferredAddrSpace;
  PredicatedAddrSpaceMapTy PredicatedAS;
  inferAddressSpaces(Postorder, InferredAddrSpace, PredicatedAS);

  // Changes the address spaces of the flat address expressions who are inferred
  // to point to a specific address space.
  return rewriteWithNewAddressSpaces(Postorder, InferredAddrSpace, PredicatedAS,
                                     &F);
}

// Constants need to be tracked through RAUW to handle cases with nested
// constant expressions, so wrap values in WeakTrackingVH.
void InferAddressSpacesImpl::inferAddressSpaces(
    ArrayRef<WeakTrackingVH> Postorder,
    ValueToAddrSpaceMapTy &InferredAddrSpace,
    PredicatedAddrSpaceMapTy &PredicatedAS) const {
  SetVector<Value *> Worklist(Postorder.begin(), Postorder.end());
  // Initially, all expressions are in the uninitialized address space.
  for (Value *V : Postorder)
    InferredAddrSpace[V] = UninitializedAddressSpace;

  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();

    // Try to update the address space of the stack top according to the
    // address spaces of its operands.
    if (!updateAddressSpace(*V, InferredAddrSpace, PredicatedAS))
      continue;

    for (Value *User : V->users()) {
      // Skip if User is already in the worklist.
      if (Worklist.count(User))
        continue;

      auto Pos = InferredAddrSpace.find(User);
      // Our algorithm only updates the address spaces of flat address
      // expressions, which are those in InferredAddrSpace.
      if (Pos == InferredAddrSpace.end())
        continue;

      // Function updateAddressSpace moves the address space down a lattice
      // path. Therefore, nothing to do if User is already inferred as flat (the
      // bottom element in the lattice).
      if (Pos->second == FlatAddrSpace)
        continue;

      Worklist.insert(User);
    }
  }
}

unsigned InferAddressSpacesImpl::getPredicatedAddrSpace(const Value &V,
                                                        Value *Opnd) const {
  const Instruction *I = dyn_cast<Instruction>(&V);
  if (!I)
    return UninitializedAddressSpace;

  Opnd = Opnd->stripInBoundsOffsets();
  for (auto &AssumeVH : AC.assumptionsFor(Opnd)) {
    if (!AssumeVH)
      continue;
    CallInst *CI = cast<CallInst>(AssumeVH);
    if (!isValidAssumeForContext(CI, I, DT))
      continue;

    const Value *Ptr;
    unsigned AS;
    std::tie(Ptr, AS) = TTI->getPredicatedAddrSpace(CI->getArgOperand(0));
    if (Ptr)
      return AS;
  }

  return UninitializedAddressSpace;
}

bool InferAddressSpacesImpl::updateAddressSpace(
    const Value &V, ValueToAddrSpaceMapTy &InferredAddrSpace,
    PredicatedAddrSpaceMapTy &PredicatedAS) const {
  assert(InferredAddrSpace.count(&V));

  LLVM_DEBUG(dbgs() << "Updating the address space of\n  " << V << '\n');

  // The new inferred address space equals the join of the address spaces
  // of all its pointer operands.
  unsigned NewAS = UninitializedAddressSpace;

  const Operator &Op = cast<Operator>(V);
  if (Op.getOpcode() == Instruction::Select) {
    Value *Src0 = Op.getOperand(1);
    Value *Src1 = Op.getOperand(2);

    auto I = InferredAddrSpace.find(Src0);
    unsigned Src0AS = (I != InferredAddrSpace.end())
                          ? I->second
                          : Src0->getType()->getPointerAddressSpace();

    auto J = InferredAddrSpace.find(Src1);
    unsigned Src1AS = (J != InferredAddrSpace.end())
                          ? J->second
                          : Src1->getType()->getPointerAddressSpace();

    auto *C0 = dyn_cast<Constant>(Src0);
    auto *C1 = dyn_cast<Constant>(Src1);

    // If one of the inputs is a constant, we may be able to do a constant
    // addrspacecast of it. Defer inferring the address space until the input
    // address space is known.
    if ((C1 && Src0AS == UninitializedAddressSpace) ||
        (C0 && Src1AS == UninitializedAddressSpace))
      return false;

    if (C0 && isSafeToCastConstAddrSpace(C0, Src1AS))
      NewAS = Src1AS;
    else if (C1 && isSafeToCastConstAddrSpace(C1, Src0AS))
      NewAS = Src0AS;
    else
      NewAS = joinAddressSpaces(Src0AS, Src1AS);
  } else {
    unsigned AS = TTI->getAssumedAddrSpace(&V);
    if (AS != UninitializedAddressSpace) {
      // Use the assumed address space directly.
      NewAS = AS;
    } else {
      // Otherwise, infer the address space from its pointer operands.
      for (Value *PtrOperand : getPointerOperands(V, *DL, TTI)) {
        auto I = InferredAddrSpace.find(PtrOperand);
        unsigned OperandAS;
        if (I == InferredAddrSpace.end()) {
          OperandAS = PtrOperand->getType()->getPointerAddressSpace();
          if (OperandAS == FlatAddrSpace) {
            // Check AC for assumption dominating V.
            unsigned AS = getPredicatedAddrSpace(V, PtrOperand);
            if (AS != UninitializedAddressSpace) {
              LLVM_DEBUG(dbgs()
                         << "  deduce operand AS from the predicate addrspace "
                         << AS << '\n');
              OperandAS = AS;
              // Record this use with the predicated AS.
              PredicatedAS[std::make_pair(&V, PtrOperand)] = OperandAS;
            }
          }
        } else
          OperandAS = I->second;

        // join(flat, *) = flat. So we can break if NewAS is already flat.
        NewAS = joinAddressSpaces(NewAS, OperandAS);
        if (NewAS == FlatAddrSpace)
          break;
      }
    }
  }

  unsigned OldAS = InferredAddrSpace.lookup(&V);
  assert(OldAS != FlatAddrSpace);
  if (OldAS == NewAS)
    return false;

  // If any updates are made, grabs its users to the worklist because
  // their address spaces can also be possibly updated.
  LLVM_DEBUG(dbgs() << "  to " << NewAS << '\n');
  InferredAddrSpace[&V] = NewAS;
  return true;
}

/// \p returns true if \p U is the pointer operand of a memory instruction with
/// a single pointer operand that can have its address space changed by simply
/// mutating the use to a new value. If the memory instruction is volatile,
/// return true only if the target allows the memory instruction to be volatile
/// in the new address space.
static bool isSimplePointerUseValidToReplace(const TargetTransformInfo &TTI,
                                             Use &U, unsigned AddrSpace) {
  User *Inst = U.getUser();
  unsigned OpNo = U.getOperandNo();
  bool VolatileIsAllowed = false;
  if (auto *I = dyn_cast<Instruction>(Inst))
    VolatileIsAllowed = TTI.hasVolatileVariant(I, AddrSpace);

  if (auto *LI = dyn_cast<LoadInst>(Inst))
    return OpNo == LoadInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !LI->isVolatile());

  if (auto *SI = dyn_cast<StoreInst>(Inst))
    return OpNo == StoreInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !SI->isVolatile());

  if (auto *RMW = dyn_cast<AtomicRMWInst>(Inst))
    return OpNo == AtomicRMWInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !RMW->isVolatile());

  if (auto *CmpX = dyn_cast<AtomicCmpXchgInst>(Inst))
    return OpNo == AtomicCmpXchgInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !CmpX->isVolatile());

  return false;
}

/// Update memory intrinsic uses that require more complex processing than
/// simple memory instructions. These require re-mangling and may have multiple
/// pointer operands.
static bool handleMemIntrinsicPtrUse(MemIntrinsic *MI, Value *OldV,
                                     Value *NewV) {
  IRBuilder<> B(MI);
  MDNode *TBAA = MI->getMetadata(LLVMContext::MD_tbaa);
  MDNode *ScopeMD = MI->getMetadata(LLVMContext::MD_alias_scope);
  MDNode *NoAliasMD = MI->getMetadata(LLVMContext::MD_noalias);

  if (auto *MSI = dyn_cast<MemSetInst>(MI)) {
    B.CreateMemSet(NewV, MSI->getValue(), MSI->getLength(), MSI->getDestAlign(),
                   false, // isVolatile
                   TBAA, ScopeMD, NoAliasMD);
  } else if (auto *MTI = dyn_cast<MemTransferInst>(MI)) {
    Value *Src = MTI->getRawSource();
    Value *Dest = MTI->getRawDest();

    // Be careful in case this is a self-to-self copy.
    if (Src == OldV)
      Src = NewV;

    if (Dest == OldV)
      Dest = NewV;

    if (isa<MemCpyInlineInst>(MTI)) {
      MDNode *TBAAStruct = MTI->getMetadata(LLVMContext::MD_tbaa_struct);
      B.CreateMemCpyInline(Dest, MTI->getDestAlign(), Src,
                           MTI->getSourceAlign(), MTI->getLength(),
                           false, // isVolatile
                           TBAA, TBAAStruct, ScopeMD, NoAliasMD);
    } else if (isa<MemCpyInst>(MTI)) {
      MDNode *TBAAStruct = MTI->getMetadata(LLVMContext::MD_tbaa_struct);
      B.CreateMemCpy(Dest, MTI->getDestAlign(), Src, MTI->getSourceAlign(),
                     MTI->getLength(),
                     false, // isVolatile
                     TBAA, TBAAStruct, ScopeMD, NoAliasMD);
    } else {
      assert(isa<MemMoveInst>(MTI));
      B.CreateMemMove(Dest, MTI->getDestAlign(), Src, MTI->getSourceAlign(),
                      MTI->getLength(),
                      false, // isVolatile
                      TBAA, ScopeMD, NoAliasMD);
    }
  } else
    llvm_unreachable("unhandled MemIntrinsic");

  MI->eraseFromParent();
  return true;
}

// \p returns true if it is OK to change the address space of constant \p C with
// a ConstantExpr addrspacecast.
bool InferAddressSpacesImpl::isSafeToCastConstAddrSpace(Constant *C,
                                                        unsigned NewAS) const {
  assert(NewAS != UninitializedAddressSpace);

  unsigned SrcAS = C->getType()->getPointerAddressSpace();
  if (SrcAS == NewAS || isa<UndefValue>(C))
    return true;

  // Prevent illegal casts between different non-flat address spaces.
  if (SrcAS != FlatAddrSpace && NewAS != FlatAddrSpace)
    return false;

  if (isa<ConstantPointerNull>(C))
    return true;

  if (auto *Op = dyn_cast<Operator>(C)) {
    // If we already have a constant addrspacecast, it should be safe to cast it
    // off.
    if (Op->getOpcode() == Instruction::AddrSpaceCast)
      return isSafeToCastConstAddrSpace(cast<Constant>(Op->getOperand(0)),
                                        NewAS);

    if (Op->getOpcode() == Instruction::IntToPtr &&
        Op->getType()->getPointerAddressSpace() == FlatAddrSpace)
      return true;
  }

  return false;
}

static Value::use_iterator skipToNextUser(Value::use_iterator I,
                                          Value::use_iterator End) {
  User *CurUser = I->getUser();
  ++I;

  while (I != End && I->getUser() == CurUser)
    ++I;

  return I;
}

bool InferAddressSpacesImpl::rewriteWithNewAddressSpaces(
    ArrayRef<WeakTrackingVH> Postorder,
    const ValueToAddrSpaceMapTy &InferredAddrSpace,
    const PredicatedAddrSpaceMapTy &PredicatedAS, Function *F) const {
  // For each address expression to be modified, creates a clone of it with its
  // pointer operands converted to the new address space. Since the pointer
  // operands are converted, the clone is naturally in the new address space by
  // construction.
  ValueToValueMapTy ValueWithNewAddrSpace;
  SmallVector<const Use *, 32> PoisonUsesToFix;
  for (Value *V : Postorder) {
    unsigned NewAddrSpace = InferredAddrSpace.lookup(V);

    // In some degenerate cases (e.g. invalid IR in unreachable code), we may
    // not even infer the value to have its original address space.
    if (NewAddrSpace == UninitializedAddressSpace)
      continue;

    if (V->getType()->getPointerAddressSpace() != NewAddrSpace) {
      Value *New =
          cloneValueWithNewAddressSpace(V, NewAddrSpace, ValueWithNewAddrSpace,
                                        PredicatedAS, &PoisonUsesToFix);
      if (New)
        ValueWithNewAddrSpace[V] = New;
    }
  }

  if (ValueWithNewAddrSpace.empty())
    return false;

  // Fixes all the poison uses generated by cloneInstructionWithNewAddressSpace.
  for (const Use *PoisonUse : PoisonUsesToFix) {
    User *V = PoisonUse->getUser();
    User *NewV = cast_or_null<User>(ValueWithNewAddrSpace.lookup(V));
    if (!NewV)
      continue;

    unsigned OperandNo = PoisonUse->getOperandNo();
    assert(isa<PoisonValue>(NewV->getOperand(OperandNo)));
    NewV->setOperand(OperandNo, ValueWithNewAddrSpace.lookup(PoisonUse->get()));
  }

  SmallVector<Instruction *, 16> DeadInstructions;
  ValueToValueMapTy VMap;
  ValueMapper VMapper(VMap, RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);

  // Replaces the uses of the old address expressions with the new ones.
  for (const WeakTrackingVH &WVH : Postorder) {
    assert(WVH && "value was unexpectedly deleted");
    Value *V = WVH;
    Value *NewV = ValueWithNewAddrSpace.lookup(V);
    if (NewV == nullptr)
      continue;

    LLVM_DEBUG(dbgs() << "Replacing the uses of " << *V << "\n  with\n  "
                      << *NewV << '\n');

    if (Constant *C = dyn_cast<Constant>(V)) {
      Constant *Replace =
          ConstantExpr::getAddrSpaceCast(cast<Constant>(NewV), C->getType());
      if (C != Replace) {
        LLVM_DEBUG(dbgs() << "Inserting replacement const cast: " << Replace
                          << ": " << *Replace << '\n');
        SmallVector<User *, 16> WorkList;
        for (User *U : make_early_inc_range(C->users())) {
          if (auto *I = dyn_cast<Instruction>(U)) {
            if (I->getFunction() == F)
              I->replaceUsesOfWith(C, Replace);
          } else {
            WorkList.append(U->user_begin(), U->user_end());
          }
        }
        if (!WorkList.empty()) {
          VMap[C] = Replace;
          DenseSet<User *> Visited{WorkList.begin(), WorkList.end()};
          while (!WorkList.empty()) {
            User *U = WorkList.pop_back_val();
            if (auto *I = dyn_cast<Instruction>(U)) {
              if (I->getFunction() == F)
                VMapper.remapInstruction(*I);
              continue;
            }
            for (User *U2 : U->users())
              if (Visited.insert(U2).second)
                WorkList.push_back(U2);
          }
        }
        V = Replace;
      }
    }

    Value::use_iterator I, E, Next;
    for (I = V->use_begin(), E = V->use_end(); I != E;) {
      Use &U = *I;
      User *CurUser = U.getUser();

      // Some users may see the same pointer operand in multiple operands. Skip
      // to the next instruction.
      I = skipToNextUser(I, E);

      if (isSimplePointerUseValidToReplace(
              *TTI, U, V->getType()->getPointerAddressSpace())) {
        // If V is used as the pointer operand of a compatible memory operation,
        // sets the pointer operand to NewV. This replacement does not change
        // the element type, so the resultant load/store is still valid.
        U.set(NewV);
        continue;
      }

      // Skip if the current user is the new value itself.
      if (CurUser == NewV)
        continue;

      if (auto *CurUserI = dyn_cast<Instruction>(CurUser);
          CurUserI && CurUserI->getFunction() != F)
        continue;

      // Handle more complex cases like intrinsic that need to be remangled.
      if (auto *MI = dyn_cast<MemIntrinsic>(CurUser)) {
        if (!MI->isVolatile() && handleMemIntrinsicPtrUse(MI, V, NewV))
          continue;
      }

      if (auto *II = dyn_cast<IntrinsicInst>(CurUser)) {
        if (rewriteIntrinsicOperands(II, V, NewV))
          continue;
      }

      if (isa<Instruction>(CurUser)) {
        if (ICmpInst *Cmp = dyn_cast<ICmpInst>(CurUser)) {
          // If we can infer that both pointers are in the same addrspace,
          // transform e.g.
          //   %cmp = icmp eq float* %p, %q
          // into
          //   %cmp = icmp eq float addrspace(3)* %new_p, %new_q

          unsigned NewAS = NewV->getType()->getPointerAddressSpace();
          int SrcIdx = U.getOperandNo();
          int OtherIdx = (SrcIdx == 0) ? 1 : 0;
          Value *OtherSrc = Cmp->getOperand(OtherIdx);

          if (Value *OtherNewV = ValueWithNewAddrSpace.lookup(OtherSrc)) {
            if (OtherNewV->getType()->getPointerAddressSpace() == NewAS) {
              Cmp->setOperand(OtherIdx, OtherNewV);
              Cmp->setOperand(SrcIdx, NewV);
              continue;
            }
          }

          // Even if the type mismatches, we can cast the constant.
          if (auto *KOtherSrc = dyn_cast<Constant>(OtherSrc)) {
            if (isSafeToCastConstAddrSpace(KOtherSrc, NewAS)) {
              Cmp->setOperand(SrcIdx, NewV);
              Cmp->setOperand(OtherIdx, ConstantExpr::getAddrSpaceCast(
                                            KOtherSrc, NewV->getType()));
              continue;
            }
          }
        }

        if (AddrSpaceCastInst *ASC = dyn_cast<AddrSpaceCastInst>(CurUser)) {
          unsigned NewAS = NewV->getType()->getPointerAddressSpace();
          if (ASC->getDestAddressSpace() == NewAS) {
            ASC->replaceAllUsesWith(NewV);
            DeadInstructions.push_back(ASC);
            continue;
          }
        }

        // Otherwise, replaces the use with flat(NewV).
        if (Instruction *VInst = dyn_cast<Instruction>(V)) {
          // Don't create a copy of the original addrspacecast.
          if (U == V && isa<AddrSpaceCastInst>(V))
            continue;

          // Insert the addrspacecast after NewV.
          BasicBlock::iterator InsertPos;
          if (Instruction *NewVInst = dyn_cast<Instruction>(NewV))
            InsertPos = std::next(NewVInst->getIterator());
          else
            InsertPos = std::next(VInst->getIterator());

          while (isa<PHINode>(InsertPos))
            ++InsertPos;
          // This instruction may contain multiple uses of V, update them all.
          CurUser->replaceUsesOfWith(
              V, new AddrSpaceCastInst(NewV, V->getType(), "", InsertPos));
        } else {
          CurUser->replaceUsesOfWith(
              V, ConstantExpr::getAddrSpaceCast(cast<Constant>(NewV),
                                                V->getType()));
        }
      }
    }

    if (V->use_empty()) {
      if (Instruction *I = dyn_cast<Instruction>(V))
        DeadInstructions.push_back(I);
    }
  }

  for (Instruction *I : DeadInstructions)
    RecursivelyDeleteTriviallyDeadInstructions(I);

  return true;
}

bool InferAddressSpaces::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  DominatorTree *DT = DTWP ? &DTWP->getDomTree() : nullptr;
  return InferAddressSpacesImpl(
             getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F), DT,
             &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F),
             FlatAddrSpace)
      .run(F);
}

FunctionPass *llvm::createInferAddressSpacesPass(unsigned AddressSpace) {
  return new InferAddressSpaces(AddressSpace);
}

InferAddressSpacesPass::InferAddressSpacesPass()
    : FlatAddrSpace(UninitializedAddressSpace) {}
InferAddressSpacesPass::InferAddressSpacesPass(unsigned AddressSpace)
    : FlatAddrSpace(AddressSpace) {}

PreservedAnalyses InferAddressSpacesPass::run(Function &F,
                                              FunctionAnalysisManager &AM) {
  bool Changed =
      InferAddressSpacesImpl(AM.getResult<AssumptionAnalysis>(F),
                             AM.getCachedResult<DominatorTreeAnalysis>(F),
                             &AM.getResult<TargetIRAnalysis>(F), FlatAddrSpace)
          .run(F);
  if (Changed) {
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    PA.preserve<DominatorTreeAnalysis>();
    return PA;
  }
  return PreservedAnalyses::all();
}
