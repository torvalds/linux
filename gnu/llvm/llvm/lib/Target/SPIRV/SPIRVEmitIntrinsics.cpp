//===-- SPIRVEmitIntrinsics.cpp - emit SPIRV intrinsics ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The pass emits SPIRV intrinsics keeping essential high-level information for
// the translation of LLVM IR to SPIR-V.
//
//===----------------------------------------------------------------------===//

#include "SPIRV.h"
#include "SPIRVBuiltins.h"
#include "SPIRVMetadata.h"
#include "SPIRVSubtarget.h"
#include "SPIRVTargetMachine.h"
#include "SPIRVUtils.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/TypedPointerType.h"

#include <queue>

// This pass performs the following transformation on LLVM IR level required
// for the following translation to SPIR-V:
// - replaces direct usages of aggregate constants with target-specific
//   intrinsics;
// - replaces aggregates-related instructions (extract/insert, ld/st, etc)
//   with a target-specific intrinsics;
// - emits intrinsics for the global variable initializers since IRTranslator
//   doesn't handle them and it's not very convenient to translate them
//   ourselves;
// - emits intrinsics to keep track of the string names assigned to the values;
// - emits intrinsics to keep track of constants (this is necessary to have an
//   LLVM IR constant after the IRTranslation is completed) for their further
//   deduplication;
// - emits intrinsics to keep track of original LLVM types of the values
//   to be able to emit proper SPIR-V types eventually.
//
// TODO: consider removing spv.track.constant in favor of spv.assign.type.

using namespace llvm;

namespace llvm {
namespace SPIRV {
#define GET_BuiltinGroup_DECL
#include "SPIRVGenTables.inc"
} // namespace SPIRV
void initializeSPIRVEmitIntrinsicsPass(PassRegistry &);
} // namespace llvm

namespace {

inline MetadataAsValue *buildMD(Value *Arg) {
  LLVMContext &Ctx = Arg->getContext();
  return MetadataAsValue::get(
      Ctx, MDNode::get(Ctx, ValueAsMetadata::getConstant(Arg)));
}

class SPIRVEmitIntrinsics
    : public ModulePass,
      public InstVisitor<SPIRVEmitIntrinsics, Instruction *> {
  SPIRVTargetMachine *TM = nullptr;
  SPIRVGlobalRegistry *GR = nullptr;
  Function *F = nullptr;
  bool TrackConstants = true;
  DenseMap<Instruction *, Constant *> AggrConsts;
  DenseMap<Instruction *, Type *> AggrConstTypes;
  DenseSet<Instruction *> AggrStores;
  SPIRV::InstructionSet::InstructionSet InstrSet;

  // a register of Instructions that don't have a complete type definition
  SmallPtrSet<Value *, 8> UncompleteTypeInfo;
  SmallVector<Instruction *> PostprocessWorklist;

  // well known result types of builtins
  enum WellKnownTypes { Event };

  // deduce element type of untyped pointers
  Type *deduceElementType(Value *I, bool UnknownElemTypeI8);
  Type *deduceElementTypeHelper(Value *I, bool UnknownElemTypeI8);
  Type *deduceElementTypeHelper(Value *I, std::unordered_set<Value *> &Visited,
                                bool UnknownElemTypeI8);
  Type *deduceElementTypeByValueDeep(Type *ValueTy, Value *Operand,
                                     bool UnknownElemTypeI8);
  Type *deduceElementTypeByValueDeep(Type *ValueTy, Value *Operand,
                                     std::unordered_set<Value *> &Visited,
                                     bool UnknownElemTypeI8);
  Type *deduceElementTypeByUsersDeep(Value *Op,
                                     std::unordered_set<Value *> &Visited,
                                     bool UnknownElemTypeI8);
  void maybeAssignPtrType(Type *&Ty, Value *I, Type *RefTy,
                          bool UnknownElemTypeI8);

  // deduce nested types of composites
  Type *deduceNestedTypeHelper(User *U, bool UnknownElemTypeI8);
  Type *deduceNestedTypeHelper(User *U, Type *Ty,
                               std::unordered_set<Value *> &Visited,
                               bool UnknownElemTypeI8);

  // deduce Types of operands of the Instruction if possible
  void deduceOperandElementType(Instruction *I, Instruction *AskOp = 0,
                                Type *AskTy = 0, CallInst *AssignCI = 0);

  void preprocessCompositeConstants(IRBuilder<> &B);
  void preprocessUndefs(IRBuilder<> &B);

  CallInst *buildIntrWithMD(Intrinsic::ID IntrID, ArrayRef<Type *> Types,
                            Value *Arg, Value *Arg2, ArrayRef<Constant *> Imms,
                            IRBuilder<> &B) {
    SmallVector<Value *, 4> Args;
    Args.push_back(Arg2);
    Args.push_back(buildMD(Arg));
    for (auto *Imm : Imms)
      Args.push_back(Imm);
    return B.CreateIntrinsic(IntrID, {Types}, Args);
  }

  void buildAssignType(IRBuilder<> &B, Type *ElemTy, Value *Arg);
  void buildAssignPtr(IRBuilder<> &B, Type *ElemTy, Value *Arg);
  void updateAssignType(CallInst *AssignCI, Value *Arg, Value *OfType);

  void replaceMemInstrUses(Instruction *Old, Instruction *New, IRBuilder<> &B);
  void processInstrAfterVisit(Instruction *I, IRBuilder<> &B);
  bool insertAssignPtrTypeIntrs(Instruction *I, IRBuilder<> &B,
                                bool UnknownElemTypeI8);
  void insertAssignTypeIntrs(Instruction *I, IRBuilder<> &B);
  void insertAssignPtrTypeTargetExt(TargetExtType *AssignedType, Value *V,
                                    IRBuilder<> &B);
  void replacePointerOperandWithPtrCast(Instruction *I, Value *Pointer,
                                        Type *ExpectedElementType,
                                        unsigned OperandToReplace,
                                        IRBuilder<> &B);
  void insertPtrCastOrAssignTypeInstr(Instruction *I, IRBuilder<> &B);
  void insertSpirvDecorations(Instruction *I, IRBuilder<> &B);
  void processGlobalValue(GlobalVariable &GV, IRBuilder<> &B);
  void processParamTypes(Function *F, IRBuilder<> &B);
  void processParamTypesByFunHeader(Function *F, IRBuilder<> &B);
  Type *deduceFunParamElementType(Function *F, unsigned OpIdx);
  Type *deduceFunParamElementType(Function *F, unsigned OpIdx,
                                  std::unordered_set<Function *> &FVisited);

public:
  static char ID;
  SPIRVEmitIntrinsics() : ModulePass(ID) {
    initializeSPIRVEmitIntrinsicsPass(*PassRegistry::getPassRegistry());
  }
  SPIRVEmitIntrinsics(SPIRVTargetMachine *_TM) : ModulePass(ID), TM(_TM) {
    initializeSPIRVEmitIntrinsicsPass(*PassRegistry::getPassRegistry());
  }
  Instruction *visitInstruction(Instruction &I) { return &I; }
  Instruction *visitSwitchInst(SwitchInst &I);
  Instruction *visitGetElementPtrInst(GetElementPtrInst &I);
  Instruction *visitBitCastInst(BitCastInst &I);
  Instruction *visitInsertElementInst(InsertElementInst &I);
  Instruction *visitExtractElementInst(ExtractElementInst &I);
  Instruction *visitInsertValueInst(InsertValueInst &I);
  Instruction *visitExtractValueInst(ExtractValueInst &I);
  Instruction *visitLoadInst(LoadInst &I);
  Instruction *visitStoreInst(StoreInst &I);
  Instruction *visitAllocaInst(AllocaInst &I);
  Instruction *visitAtomicCmpXchgInst(AtomicCmpXchgInst &I);
  Instruction *visitUnreachableInst(UnreachableInst &I);
  Instruction *visitCallInst(CallInst &I);

  StringRef getPassName() const override { return "SPIRV emit intrinsics"; }

  bool runOnModule(Module &M) override;
  bool runOnFunction(Function &F);
  bool postprocessTypes();

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    ModulePass::getAnalysisUsage(AU);
  }
};

bool isConvergenceIntrinsic(const Instruction *I) {
  const auto *II = dyn_cast<IntrinsicInst>(I);
  if (!II)
    return false;

  return II->getIntrinsicID() == Intrinsic::experimental_convergence_entry ||
         II->getIntrinsicID() == Intrinsic::experimental_convergence_loop ||
         II->getIntrinsicID() == Intrinsic::experimental_convergence_anchor;
}
} // namespace

char SPIRVEmitIntrinsics::ID = 0;

INITIALIZE_PASS(SPIRVEmitIntrinsics, "emit-intrinsics", "SPIRV emit intrinsics",
                false, false)

static inline bool isAssignTypeInstr(const Instruction *I) {
  return isa<IntrinsicInst>(I) &&
         cast<IntrinsicInst>(I)->getIntrinsicID() == Intrinsic::spv_assign_type;
}

static bool isMemInstrToReplace(Instruction *I) {
  return isa<StoreInst>(I) || isa<LoadInst>(I) || isa<InsertValueInst>(I) ||
         isa<ExtractValueInst>(I) || isa<AtomicCmpXchgInst>(I);
}

static bool isAggrConstForceInt32(const Value *V) {
  return isa<ConstantArray>(V) || isa<ConstantStruct>(V) ||
         isa<ConstantDataArray>(V) ||
         (isa<ConstantAggregateZero>(V) && !V->getType()->isVectorTy());
}

static void setInsertPointSkippingPhis(IRBuilder<> &B, Instruction *I) {
  if (isa<PHINode>(I))
    B.SetInsertPoint(I->getParent()->getFirstNonPHIOrDbgOrAlloca());
  else
    B.SetInsertPoint(I);
}

static void setInsertPointAfterDef(IRBuilder<> &B, Instruction *I) {
  B.SetCurrentDebugLocation(I->getDebugLoc());
  if (I->getType()->isVoidTy())
    B.SetInsertPoint(I->getNextNode());
  else
    B.SetInsertPoint(*I->getInsertionPointAfterDef());
}

static bool requireAssignType(Instruction *I) {
  IntrinsicInst *Intr = dyn_cast<IntrinsicInst>(I);
  if (Intr) {
    switch (Intr->getIntrinsicID()) {
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
      return false;
    }
  }
  return true;
}

static inline void reportFatalOnTokenType(const Instruction *I) {
  if (I->getType()->isTokenTy())
    report_fatal_error("A token is encountered but SPIR-V without extensions "
                       "does not support token type",
                       false);
}

static bool IsKernelArgInt8(Function *F, StoreInst *SI) {
  return SI && F->getCallingConv() == CallingConv::SPIR_KERNEL &&
         isPointerTy(SI->getValueOperand()->getType()) &&
         isa<Argument>(SI->getValueOperand());
}

// Maybe restore original function return type.
static inline Type *restoreMutatedType(SPIRVGlobalRegistry *GR, Instruction *I,
                                       Type *Ty) {
  CallInst *CI = dyn_cast<CallInst>(I);
  if (!CI || CI->isIndirectCall() || CI->isInlineAsm() ||
      !CI->getCalledFunction() || CI->getCalledFunction()->isIntrinsic())
    return Ty;
  if (Type *OriginalTy = GR->findMutated(CI->getCalledFunction()))
    return OriginalTy;
  return Ty;
}

// Reconstruct type with nested element types according to deduced type info.
// Return nullptr if no detailed type info is available.
static inline Type *reconstructType(SPIRVGlobalRegistry *GR, Value *Op) {
  Type *Ty = Op->getType();
  if (!isUntypedPointerTy(Ty))
    return Ty;
  // try to find the pointee type
  if (Type *NestedTy = GR->findDeducedElementType(Op))
    return getTypedPointerWrapper(NestedTy, getPointerAddressSpace(Ty));
  // not a pointer according to the type info (e.g., Event object)
  CallInst *CI = GR->findAssignPtrTypeInstr(Op);
  if (!CI)
    return nullptr;
  MetadataAsValue *MD = cast<MetadataAsValue>(CI->getArgOperand(1));
  return cast<ConstantAsMetadata>(MD->getMetadata())->getType();
}

void SPIRVEmitIntrinsics::buildAssignType(IRBuilder<> &B, Type *Ty,
                                          Value *Arg) {
  Value *OfType = PoisonValue::get(Ty);
  CallInst *AssignCI = buildIntrWithMD(Intrinsic::spv_assign_type,
                                       {Arg->getType()}, OfType, Arg, {}, B);
  GR->addAssignPtrTypeInstr(Arg, AssignCI);
}

void SPIRVEmitIntrinsics::buildAssignPtr(IRBuilder<> &B, Type *ElemTy,
                                         Value *Arg) {
  Value *OfType = PoisonValue::get(ElemTy);
  CallInst *AssignPtrTyCI = GR->findAssignPtrTypeInstr(Arg);
  if (AssignPtrTyCI == nullptr ||
      AssignPtrTyCI->getParent()->getParent() != F) {
    AssignPtrTyCI = buildIntrWithMD(
        Intrinsic::spv_assign_ptr_type, {Arg->getType()}, OfType, Arg,
        {B.getInt32(getPointerAddressSpace(Arg->getType()))}, B);
    GR->addDeducedElementType(AssignPtrTyCI, ElemTy);
    GR->addDeducedElementType(Arg, ElemTy);
    GR->addAssignPtrTypeInstr(Arg, AssignPtrTyCI);
  } else {
    updateAssignType(AssignPtrTyCI, Arg, OfType);
  }
}

void SPIRVEmitIntrinsics::updateAssignType(CallInst *AssignCI, Value *Arg,
                                           Value *OfType) {
  AssignCI->setArgOperand(1, buildMD(OfType));
  if (cast<IntrinsicInst>(AssignCI)->getIntrinsicID() !=
      Intrinsic::spv_assign_ptr_type)
    return;

  // update association with the pointee type
  Type *ElemTy = OfType->getType();
  GR->addDeducedElementType(AssignCI, ElemTy);
  GR->addDeducedElementType(Arg, ElemTy);
}

// Set element pointer type to the given value of ValueTy and tries to
// specify this type further (recursively) by Operand value, if needed.
Type *
SPIRVEmitIntrinsics::deduceElementTypeByValueDeep(Type *ValueTy, Value *Operand,
                                                  bool UnknownElemTypeI8) {
  std::unordered_set<Value *> Visited;
  return deduceElementTypeByValueDeep(ValueTy, Operand, Visited,
                                      UnknownElemTypeI8);
}

Type *SPIRVEmitIntrinsics::deduceElementTypeByValueDeep(
    Type *ValueTy, Value *Operand, std::unordered_set<Value *> &Visited,
    bool UnknownElemTypeI8) {
  Type *Ty = ValueTy;
  if (Operand) {
    if (auto *PtrTy = dyn_cast<PointerType>(Ty)) {
      if (Type *NestedTy =
              deduceElementTypeHelper(Operand, Visited, UnknownElemTypeI8))
        Ty = getTypedPointerWrapper(NestedTy, PtrTy->getAddressSpace());
    } else {
      Ty = deduceNestedTypeHelper(dyn_cast<User>(Operand), Ty, Visited,
                                  UnknownElemTypeI8);
    }
  }
  return Ty;
}

// Traverse User instructions to deduce an element pointer type of the operand.
Type *SPIRVEmitIntrinsics::deduceElementTypeByUsersDeep(
    Value *Op, std::unordered_set<Value *> &Visited, bool UnknownElemTypeI8) {
  if (!Op || !isPointerTy(Op->getType()))
    return nullptr;

  if (auto ElemTy = getPointeeType(Op->getType()))
    return ElemTy;

  // maybe we already know operand's element type
  if (Type *KnownTy = GR->findDeducedElementType(Op))
    return KnownTy;

  for (User *OpU : Op->users()) {
    if (Instruction *Inst = dyn_cast<Instruction>(OpU)) {
      if (Type *Ty = deduceElementTypeHelper(Inst, Visited, UnknownElemTypeI8))
        return Ty;
    }
  }
  return nullptr;
}

// Implements what we know in advance about intrinsics and builtin calls
// TODO: consider feasibility of this particular case to be generalized by
// encoding knowledge about intrinsics and builtin calls by corresponding
// specification rules
static Type *getPointeeTypeByCallInst(StringRef DemangledName,
                                      Function *CalledF, unsigned OpIdx) {
  if ((DemangledName.starts_with("__spirv_ocl_printf(") ||
       DemangledName.starts_with("printf(")) &&
      OpIdx == 0)
    return IntegerType::getInt8Ty(CalledF->getContext());
  return nullptr;
}

// Deduce and return a successfully deduced Type of the Instruction,
// or nullptr otherwise.
Type *SPIRVEmitIntrinsics::deduceElementTypeHelper(Value *I,
                                                   bool UnknownElemTypeI8) {
  std::unordered_set<Value *> Visited;
  return deduceElementTypeHelper(I, Visited, UnknownElemTypeI8);
}

void SPIRVEmitIntrinsics::maybeAssignPtrType(Type *&Ty, Value *Op, Type *RefTy,
                                             bool UnknownElemTypeI8) {
  if (isUntypedPointerTy(RefTy)) {
    if (!UnknownElemTypeI8)
      return;
    if (auto *I = dyn_cast<Instruction>(Op)) {
      UncompleteTypeInfo.insert(I);
      PostprocessWorklist.push_back(I);
    }
  }
  Ty = RefTy;
}

Type *SPIRVEmitIntrinsics::deduceElementTypeHelper(
    Value *I, std::unordered_set<Value *> &Visited, bool UnknownElemTypeI8) {
  // allow to pass nullptr as an argument
  if (!I)
    return nullptr;

  // maybe already known
  if (Type *KnownTy = GR->findDeducedElementType(I))
    return KnownTy;

  // maybe a cycle
  if (Visited.find(I) != Visited.end())
    return nullptr;
  Visited.insert(I);

  // fallback value in case when we fail to deduce a type
  Type *Ty = nullptr;
  // look for known basic patterns of type inference
  if (auto *Ref = dyn_cast<AllocaInst>(I)) {
    maybeAssignPtrType(Ty, I, Ref->getAllocatedType(), UnknownElemTypeI8);
  } else if (auto *Ref = dyn_cast<GetElementPtrInst>(I)) {
    Ty = Ref->getResultElementType();
  } else if (auto *Ref = dyn_cast<GlobalValue>(I)) {
    Ty = deduceElementTypeByValueDeep(
        Ref->getValueType(),
        Ref->getNumOperands() > 0 ? Ref->getOperand(0) : nullptr, Visited,
        UnknownElemTypeI8);
  } else if (auto *Ref = dyn_cast<AddrSpaceCastInst>(I)) {
    Type *RefTy = deduceElementTypeHelper(Ref->getPointerOperand(), Visited,
                                          UnknownElemTypeI8);
    maybeAssignPtrType(Ty, I, RefTy, UnknownElemTypeI8);
  } else if (auto *Ref = dyn_cast<BitCastInst>(I)) {
    if (Type *Src = Ref->getSrcTy(), *Dest = Ref->getDestTy();
        isPointerTy(Src) && isPointerTy(Dest))
      Ty = deduceElementTypeHelper(Ref->getOperand(0), Visited,
                                   UnknownElemTypeI8);
  } else if (auto *Ref = dyn_cast<AtomicCmpXchgInst>(I)) {
    Value *Op = Ref->getNewValOperand();
    if (isPointerTy(Op->getType()))
      Ty = deduceElementTypeHelper(Op, Visited, UnknownElemTypeI8);
  } else if (auto *Ref = dyn_cast<AtomicRMWInst>(I)) {
    Value *Op = Ref->getValOperand();
    if (isPointerTy(Op->getType()))
      Ty = deduceElementTypeHelper(Op, Visited, UnknownElemTypeI8);
  } else if (auto *Ref = dyn_cast<PHINode>(I)) {
    for (unsigned i = 0; i < Ref->getNumIncomingValues(); i++) {
      Ty = deduceElementTypeByUsersDeep(Ref->getIncomingValue(i), Visited,
                                        UnknownElemTypeI8);
      if (Ty)
        break;
    }
  } else if (auto *Ref = dyn_cast<SelectInst>(I)) {
    for (Value *Op : {Ref->getTrueValue(), Ref->getFalseValue()}) {
      Ty = deduceElementTypeByUsersDeep(Op, Visited, UnknownElemTypeI8);
      if (Ty)
        break;
    }
  } else if (auto *CI = dyn_cast<CallInst>(I)) {
    static StringMap<unsigned> ResTypeByArg = {
        {"to_global", 0},
        {"to_local", 0},
        {"to_private", 0},
        {"__spirv_GenericCastToPtr_ToGlobal", 0},
        {"__spirv_GenericCastToPtr_ToLocal", 0},
        {"__spirv_GenericCastToPtr_ToPrivate", 0},
        {"__spirv_GenericCastToPtrExplicit_ToGlobal", 0},
        {"__spirv_GenericCastToPtrExplicit_ToLocal", 0},
        {"__spirv_GenericCastToPtrExplicit_ToPrivate", 0}};
    // TODO: maybe improve performance by caching demangled names
    if (Function *CalledF = CI->getCalledFunction()) {
      std::string DemangledName =
          getOclOrSpirvBuiltinDemangledName(CalledF->getName());
      if (DemangledName.length() > 0)
        DemangledName = SPIRV::lookupBuiltinNameHelper(DemangledName);
      auto AsArgIt = ResTypeByArg.find(DemangledName);
      if (AsArgIt != ResTypeByArg.end()) {
        Ty = deduceElementTypeHelper(CI->getArgOperand(AsArgIt->second),
                                     Visited, UnknownElemTypeI8);
      }
    }
  }

  // remember the found relationship
  if (Ty) {
    // specify nested types if needed, otherwise return unchanged
    GR->addDeducedElementType(I, Ty);
  }

  return Ty;
}

// Re-create a type of the value if it has untyped pointer fields, also nested.
// Return the original value type if no corrections of untyped pointer
// information is found or needed.
Type *SPIRVEmitIntrinsics::deduceNestedTypeHelper(User *U,
                                                  bool UnknownElemTypeI8) {
  std::unordered_set<Value *> Visited;
  return deduceNestedTypeHelper(U, U->getType(), Visited, UnknownElemTypeI8);
}

Type *SPIRVEmitIntrinsics::deduceNestedTypeHelper(
    User *U, Type *OrigTy, std::unordered_set<Value *> &Visited,
    bool UnknownElemTypeI8) {
  if (!U)
    return OrigTy;

  // maybe already known
  if (Type *KnownTy = GR->findDeducedCompositeType(U))
    return KnownTy;

  // maybe a cycle
  if (Visited.find(U) != Visited.end())
    return OrigTy;
  Visited.insert(U);

  if (dyn_cast<StructType>(OrigTy)) {
    SmallVector<Type *> Tys;
    bool Change = false;
    for (unsigned i = 0; i < U->getNumOperands(); ++i) {
      Value *Op = U->getOperand(i);
      Type *OpTy = Op->getType();
      Type *Ty = OpTy;
      if (Op) {
        if (auto *PtrTy = dyn_cast<PointerType>(OpTy)) {
          if (Type *NestedTy =
                  deduceElementTypeHelper(Op, Visited, UnknownElemTypeI8))
            Ty = TypedPointerType::get(NestedTy, PtrTy->getAddressSpace());
        } else {
          Ty = deduceNestedTypeHelper(dyn_cast<User>(Op), OpTy, Visited,
                                      UnknownElemTypeI8);
        }
      }
      Tys.push_back(Ty);
      Change |= Ty != OpTy;
    }
    if (Change) {
      Type *NewTy = StructType::create(Tys);
      GR->addDeducedCompositeType(U, NewTy);
      return NewTy;
    }
  } else if (auto *ArrTy = dyn_cast<ArrayType>(OrigTy)) {
    if (Value *Op = U->getNumOperands() > 0 ? U->getOperand(0) : nullptr) {
      Type *OpTy = ArrTy->getElementType();
      Type *Ty = OpTy;
      if (auto *PtrTy = dyn_cast<PointerType>(OpTy)) {
        if (Type *NestedTy =
                deduceElementTypeHelper(Op, Visited, UnknownElemTypeI8))
          Ty = TypedPointerType::get(NestedTy, PtrTy->getAddressSpace());
      } else {
        Ty = deduceNestedTypeHelper(dyn_cast<User>(Op), OpTy, Visited,
                                    UnknownElemTypeI8);
      }
      if (Ty != OpTy) {
        Type *NewTy = ArrayType::get(Ty, ArrTy->getNumElements());
        GR->addDeducedCompositeType(U, NewTy);
        return NewTy;
      }
    }
  } else if (auto *VecTy = dyn_cast<VectorType>(OrigTy)) {
    if (Value *Op = U->getNumOperands() > 0 ? U->getOperand(0) : nullptr) {
      Type *OpTy = VecTy->getElementType();
      Type *Ty = OpTy;
      if (auto *PtrTy = dyn_cast<PointerType>(OpTy)) {
        if (Type *NestedTy =
                deduceElementTypeHelper(Op, Visited, UnknownElemTypeI8))
          Ty = getTypedPointerWrapper(NestedTy, PtrTy->getAddressSpace());
      } else {
        Ty = deduceNestedTypeHelper(dyn_cast<User>(Op), OpTy, Visited,
                                    UnknownElemTypeI8);
      }
      if (Ty != OpTy) {
        Type *NewTy = VectorType::get(Ty, VecTy->getElementCount());
        GR->addDeducedCompositeType(U, NewTy);
        return NewTy;
      }
    }
  }

  return OrigTy;
}

Type *SPIRVEmitIntrinsics::deduceElementType(Value *I, bool UnknownElemTypeI8) {
  if (Type *Ty = deduceElementTypeHelper(I, UnknownElemTypeI8))
    return Ty;
  if (!UnknownElemTypeI8)
    return nullptr;
  if (auto *Instr = dyn_cast<Instruction>(I)) {
    UncompleteTypeInfo.insert(Instr);
    PostprocessWorklist.push_back(Instr);
  }
  return IntegerType::getInt8Ty(I->getContext());
}

static inline Type *getAtomicElemTy(SPIRVGlobalRegistry *GR, Instruction *I,
                                    Value *PointerOperand) {
  Type *PointeeTy = GR->findDeducedElementType(PointerOperand);
  if (PointeeTy && !isUntypedPointerTy(PointeeTy))
    return nullptr;
  auto *PtrTy = dyn_cast<PointerType>(I->getType());
  if (!PtrTy)
    return I->getType();
  if (Type *NestedTy = GR->findDeducedElementType(I))
    return getTypedPointerWrapper(NestedTy, PtrTy->getAddressSpace());
  return nullptr;
}

// If the Instruction has Pointer operands with unresolved types, this function
// tries to deduce them. If the Instruction has Pointer operands with known
// types which differ from expected, this function tries to insert a bitcast to
// resolve the issue.
void SPIRVEmitIntrinsics::deduceOperandElementType(Instruction *I,
                                                   Instruction *AskOp,
                                                   Type *AskTy,
                                                   CallInst *AskCI) {
  SmallVector<std::pair<Value *, unsigned>> Ops;
  Type *KnownElemTy = nullptr;
  // look for known basic patterns of type inference
  if (auto *Ref = dyn_cast<PHINode>(I)) {
    if (!isPointerTy(I->getType()) ||
        !(KnownElemTy = GR->findDeducedElementType(I)))
      return;
    for (unsigned i = 0; i < Ref->getNumIncomingValues(); i++) {
      Value *Op = Ref->getIncomingValue(i);
      if (isPointerTy(Op->getType()))
        Ops.push_back(std::make_pair(Op, i));
    }
  } else if (auto *Ref = dyn_cast<AddrSpaceCastInst>(I)) {
    KnownElemTy = GR->findDeducedElementType(I);
    if (!KnownElemTy)
      return;
    Ops.push_back(std::make_pair(Ref->getPointerOperand(), 0));
  } else if (auto *Ref = dyn_cast<GetElementPtrInst>(I)) {
    KnownElemTy = Ref->getSourceElementType();
    if (isUntypedPointerTy(KnownElemTy))
      return;
    Type *PointeeTy = GR->findDeducedElementType(Ref->getPointerOperand());
    if (PointeeTy && !isUntypedPointerTy(PointeeTy))
      return;
    Ops.push_back(std::make_pair(Ref->getPointerOperand(),
                                 GetElementPtrInst::getPointerOperandIndex()));
  } else if (auto *Ref = dyn_cast<LoadInst>(I)) {
    KnownElemTy = I->getType();
    if (isUntypedPointerTy(KnownElemTy))
      return;
    Type *PointeeTy = GR->findDeducedElementType(Ref->getPointerOperand());
    if (PointeeTy && !isUntypedPointerTy(PointeeTy))
      return;
    Ops.push_back(std::make_pair(Ref->getPointerOperand(),
                                 LoadInst::getPointerOperandIndex()));
  } else if (auto *Ref = dyn_cast<StoreInst>(I)) {
    if (IsKernelArgInt8(Ref->getParent()->getParent(), Ref))
      return;
    if (!(KnownElemTy = reconstructType(GR, Ref->getValueOperand())))
      return;
    Type *PointeeTy = GR->findDeducedElementType(Ref->getPointerOperand());
    if (PointeeTy && !isUntypedPointerTy(PointeeTy))
      return;
    Ops.push_back(std::make_pair(Ref->getPointerOperand(),
                                 StoreInst::getPointerOperandIndex()));
  } else if (auto *Ref = dyn_cast<AtomicCmpXchgInst>(I)) {
    KnownElemTy = getAtomicElemTy(GR, I, Ref->getPointerOperand());
    if (!KnownElemTy)
      return;
    Ops.push_back(std::make_pair(Ref->getPointerOperand(),
                                 AtomicCmpXchgInst::getPointerOperandIndex()));
  } else if (auto *Ref = dyn_cast<AtomicRMWInst>(I)) {
    KnownElemTy = getAtomicElemTy(GR, I, Ref->getPointerOperand());
    if (!KnownElemTy)
      return;
    Ops.push_back(std::make_pair(Ref->getPointerOperand(),
                                 AtomicRMWInst::getPointerOperandIndex()));
  } else if (auto *Ref = dyn_cast<SelectInst>(I)) {
    if (!isPointerTy(I->getType()) ||
        !(KnownElemTy = GR->findDeducedElementType(I)))
      return;
    for (unsigned i = 0; i < Ref->getNumOperands(); i++) {
      Value *Op = Ref->getOperand(i);
      if (isPointerTy(Op->getType()))
        Ops.push_back(std::make_pair(Op, i));
    }
  } else if (auto *Ref = dyn_cast<ReturnInst>(I)) {
    Type *RetTy = F->getReturnType();
    if (!isPointerTy(RetTy))
      return;
    Value *Op = Ref->getReturnValue();
    if (!Op)
      return;
    if (!(KnownElemTy = GR->findDeducedElementType(F))) {
      if (Type *OpElemTy = GR->findDeducedElementType(Op)) {
        GR->addDeducedElementType(F, OpElemTy);
        TypedPointerType *DerivedTy =
            TypedPointerType::get(OpElemTy, getPointerAddressSpace(RetTy));
        GR->addReturnType(F, DerivedTy);
      }
      return;
    }
    Ops.push_back(std::make_pair(Op, 0));
  } else if (auto *Ref = dyn_cast<ICmpInst>(I)) {
    if (!isPointerTy(Ref->getOperand(0)->getType()))
      return;
    Value *Op0 = Ref->getOperand(0);
    Value *Op1 = Ref->getOperand(1);
    Type *ElemTy0 = GR->findDeducedElementType(Op0);
    Type *ElemTy1 = GR->findDeducedElementType(Op1);
    if (ElemTy0) {
      KnownElemTy = ElemTy0;
      Ops.push_back(std::make_pair(Op1, 1));
    } else if (ElemTy1) {
      KnownElemTy = ElemTy1;
      Ops.push_back(std::make_pair(Op0, 0));
    }
  } else if (auto *CI = dyn_cast<CallInst>(I)) {
    if (Function *CalledF = CI->getCalledFunction()) {
      std::string DemangledName =
          getOclOrSpirvBuiltinDemangledName(CalledF->getName());
      if (DemangledName.length() > 0 &&
          !StringRef(DemangledName).starts_with("llvm.")) {
        auto [Grp, Opcode, ExtNo] =
            SPIRV::mapBuiltinToOpcode(DemangledName, InstrSet);
        if (Opcode == SPIRV::OpGroupAsyncCopy) {
          for (unsigned i = 0, PtrCnt = 0; i < CI->arg_size() && PtrCnt < 2;
               ++i) {
            Value *Op = CI->getArgOperand(i);
            if (!isPointerTy(Op->getType()))
              continue;
            ++PtrCnt;
            if (Type *ElemTy = GR->findDeducedElementType(Op))
              KnownElemTy = ElemTy; // src will rewrite dest if both are defined
            Ops.push_back(std::make_pair(Op, i));
          }
        } else if (Grp == SPIRV::Atomic || Grp == SPIRV::AtomicFloating) {
          if (CI->arg_size() < 2)
            return;
          Value *Op = CI->getArgOperand(0);
          if (!isPointerTy(Op->getType()))
            return;
          switch (Opcode) {
          case SPIRV::OpAtomicLoad:
          case SPIRV::OpAtomicCompareExchangeWeak:
          case SPIRV::OpAtomicCompareExchange:
          case SPIRV::OpAtomicExchange:
          case SPIRV::OpAtomicIAdd:
          case SPIRV::OpAtomicISub:
          case SPIRV::OpAtomicOr:
          case SPIRV::OpAtomicXor:
          case SPIRV::OpAtomicAnd:
          case SPIRV::OpAtomicUMin:
          case SPIRV::OpAtomicUMax:
          case SPIRV::OpAtomicSMin:
          case SPIRV::OpAtomicSMax: {
            KnownElemTy = getAtomicElemTy(GR, I, Op);
            if (!KnownElemTy)
              return;
            Ops.push_back(std::make_pair(Op, 0));
          } break;
          }
        }
      }
    }
  }

  // There is no enough info to deduce types or all is valid.
  if (!KnownElemTy || Ops.size() == 0)
    return;

  LLVMContext &Ctx = F->getContext();
  IRBuilder<> B(Ctx);
  for (auto &OpIt : Ops) {
    Value *Op = OpIt.first;
    if (Op->use_empty() || (AskOp && Op != AskOp))
      continue;
    Type *Ty = AskOp ? AskTy : GR->findDeducedElementType(Op);
    if (Ty == KnownElemTy)
      continue;
    Value *OpTyVal = PoisonValue::get(KnownElemTy);
    Type *OpTy = Op->getType();
    if (!Ty || AskTy || isUntypedPointerTy(Ty) ||
        UncompleteTypeInfo.contains(Op)) {
      GR->addDeducedElementType(Op, KnownElemTy);
      // check if there is existing Intrinsic::spv_assign_ptr_type instruction
      CallInst *AssignCI = AskCI ? AskCI : GR->findAssignPtrTypeInstr(Op);
      if (AssignCI == nullptr) {
        Instruction *User = dyn_cast<Instruction>(Op->use_begin()->get());
        setInsertPointSkippingPhis(B, User ? User->getNextNode() : I);
        CallInst *CI =
            buildIntrWithMD(Intrinsic::spv_assign_ptr_type, {OpTy}, OpTyVal, Op,
                            {B.getInt32(getPointerAddressSpace(OpTy))}, B);
        GR->addAssignPtrTypeInstr(Op, CI);
      } else {
        updateAssignType(AssignCI, Op, OpTyVal);
      }
    } else {
      if (auto *OpI = dyn_cast<Instruction>(Op)) {
        // spv_ptrcast's argument Op denotes an instruction that generates
        // a value, and we may use getInsertionPointAfterDef()
        B.SetInsertPoint(*OpI->getInsertionPointAfterDef());
        B.SetCurrentDebugLocation(OpI->getDebugLoc());
      } else if (auto *OpA = dyn_cast<Argument>(Op)) {
        B.SetInsertPointPastAllocas(OpA->getParent());
        B.SetCurrentDebugLocation(DebugLoc());
      } else {
        B.SetInsertPoint(F->getEntryBlock().getFirstNonPHIOrDbgOrAlloca());
      }
      SmallVector<Type *, 2> Types = {OpTy, OpTy};
      SmallVector<Value *, 2> Args = {Op, buildMD(OpTyVal),
                                      B.getInt32(getPointerAddressSpace(OpTy))};
      CallInst *PtrCastI =
          B.CreateIntrinsic(Intrinsic::spv_ptrcast, {Types}, Args);
      I->setOperand(OpIt.second, PtrCastI);
    }
  }
}

void SPIRVEmitIntrinsics::replaceMemInstrUses(Instruction *Old,
                                              Instruction *New,
                                              IRBuilder<> &B) {
  while (!Old->user_empty()) {
    auto *U = Old->user_back();
    if (isAssignTypeInstr(U)) {
      B.SetInsertPoint(U);
      SmallVector<Value *, 2> Args = {New, U->getOperand(1)};
      CallInst *AssignCI =
          B.CreateIntrinsic(Intrinsic::spv_assign_type, {New->getType()}, Args);
      GR->addAssignPtrTypeInstr(New, AssignCI);
      U->eraseFromParent();
    } else if (isMemInstrToReplace(U) || isa<ReturnInst>(U) ||
               isa<CallInst>(U)) {
      U->replaceUsesOfWith(Old, New);
    } else {
      llvm_unreachable("illegal aggregate intrinsic user");
    }
  }
  Old->eraseFromParent();
}

void SPIRVEmitIntrinsics::preprocessUndefs(IRBuilder<> &B) {
  std::queue<Instruction *> Worklist;
  for (auto &I : instructions(F))
    Worklist.push(&I);

  while (!Worklist.empty()) {
    Instruction *I = Worklist.front();
    bool BPrepared = false;
    Worklist.pop();

    for (auto &Op : I->operands()) {
      auto *AggrUndef = dyn_cast<UndefValue>(Op);
      if (!AggrUndef || !Op->getType()->isAggregateType())
        continue;

      if (!BPrepared) {
        setInsertPointSkippingPhis(B, I);
        BPrepared = true;
      }
      auto *IntrUndef = B.CreateIntrinsic(Intrinsic::spv_undef, {}, {});
      Worklist.push(IntrUndef);
      I->replaceUsesOfWith(Op, IntrUndef);
      AggrConsts[IntrUndef] = AggrUndef;
      AggrConstTypes[IntrUndef] = AggrUndef->getType();
    }
  }
}

void SPIRVEmitIntrinsics::preprocessCompositeConstants(IRBuilder<> &B) {
  std::queue<Instruction *> Worklist;
  for (auto &I : instructions(F))
    Worklist.push(&I);

  while (!Worklist.empty()) {
    auto *I = Worklist.front();
    bool IsPhi = isa<PHINode>(I), BPrepared = false;
    assert(I);
    bool KeepInst = false;
    for (const auto &Op : I->operands()) {
      Constant *AggrConst = nullptr;
      Type *ResTy = nullptr;
      if (auto *COp = dyn_cast<ConstantVector>(Op)) {
        AggrConst = cast<Constant>(COp);
        ResTy = COp->getType();
      } else if (auto *COp = dyn_cast<ConstantArray>(Op)) {
        AggrConst = cast<Constant>(COp);
        ResTy = B.getInt32Ty();
      } else if (auto *COp = dyn_cast<ConstantStruct>(Op)) {
        AggrConst = cast<Constant>(COp);
        ResTy = B.getInt32Ty();
      } else if (auto *COp = dyn_cast<ConstantDataArray>(Op)) {
        AggrConst = cast<Constant>(COp);
        ResTy = B.getInt32Ty();
      } else if (auto *COp = dyn_cast<ConstantAggregateZero>(Op)) {
        AggrConst = cast<Constant>(COp);
        ResTy = Op->getType()->isVectorTy() ? COp->getType() : B.getInt32Ty();
      }
      if (AggrConst) {
        SmallVector<Value *> Args;
        if (auto *COp = dyn_cast<ConstantDataSequential>(Op))
          for (unsigned i = 0; i < COp->getNumElements(); ++i)
            Args.push_back(COp->getElementAsConstant(i));
        else
          for (auto &COp : AggrConst->operands())
            Args.push_back(COp);
        if (!BPrepared) {
          IsPhi ? B.SetInsertPointPastAllocas(I->getParent()->getParent())
                : B.SetInsertPoint(I);
          BPrepared = true;
        }
        auto *CI =
            B.CreateIntrinsic(Intrinsic::spv_const_composite, {ResTy}, {Args});
        Worklist.push(CI);
        I->replaceUsesOfWith(Op, CI);
        KeepInst = true;
        AggrConsts[CI] = AggrConst;
        AggrConstTypes[CI] = deduceNestedTypeHelper(AggrConst, false);
      }
    }
    if (!KeepInst)
      Worklist.pop();
  }
}

Instruction *SPIRVEmitIntrinsics::visitCallInst(CallInst &Call) {
  if (!Call.isInlineAsm())
    return &Call;

  const InlineAsm *IA = cast<InlineAsm>(Call.getCalledOperand());
  LLVMContext &Ctx = F->getContext();

  Constant *TyC = UndefValue::get(IA->getFunctionType());
  MDString *ConstraintString = MDString::get(Ctx, IA->getConstraintString());
  SmallVector<Value *> Args = {
      buildMD(TyC),
      MetadataAsValue::get(Ctx, MDNode::get(Ctx, ConstraintString))};
  for (unsigned OpIdx = 0; OpIdx < Call.arg_size(); OpIdx++)
    Args.push_back(Call.getArgOperand(OpIdx));

  IRBuilder<> B(Call.getParent());
  B.SetInsertPoint(&Call);
  B.CreateIntrinsic(Intrinsic::spv_inline_asm, {}, {Args});
  return &Call;
}

Instruction *SPIRVEmitIntrinsics::visitSwitchInst(SwitchInst &I) {
  BasicBlock *ParentBB = I.getParent();
  IRBuilder<> B(ParentBB);
  B.SetInsertPoint(&I);
  SmallVector<Value *, 4> Args;
  SmallVector<BasicBlock *> BBCases;
  for (auto &Op : I.operands()) {
    if (Op.get()->getType()->isSized()) {
      Args.push_back(Op);
    } else if (BasicBlock *BB = dyn_cast<BasicBlock>(Op.get())) {
      BBCases.push_back(BB);
      Args.push_back(BlockAddress::get(BB->getParent(), BB));
    } else {
      report_fatal_error("Unexpected switch operand");
    }
  }
  CallInst *NewI = B.CreateIntrinsic(Intrinsic::spv_switch,
                                     {I.getOperand(0)->getType()}, {Args});
  // remove switch to avoid its unneeded and undesirable unwrap into branches
  // and conditions
  I.replaceAllUsesWith(NewI);
  I.eraseFromParent();
  // insert artificial and temporary instruction to preserve valid CFG,
  // it will be removed after IR translation pass
  B.SetInsertPoint(ParentBB);
  IndirectBrInst *BrI = B.CreateIndirectBr(
      Constant::getNullValue(PointerType::getUnqual(ParentBB->getContext())),
      BBCases.size());
  for (BasicBlock *BBCase : BBCases)
    BrI->addDestination(BBCase);
  return BrI;
}

Instruction *SPIRVEmitIntrinsics::visitGetElementPtrInst(GetElementPtrInst &I) {
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  SmallVector<Type *, 2> Types = {I.getType(), I.getOperand(0)->getType()};
  SmallVector<Value *, 4> Args;
  Args.push_back(B.getInt1(I.isInBounds()));
  for (auto &Op : I.operands())
    Args.push_back(Op);
  auto *NewI = B.CreateIntrinsic(Intrinsic::spv_gep, {Types}, {Args});
  I.replaceAllUsesWith(NewI);
  I.eraseFromParent();
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitBitCastInst(BitCastInst &I) {
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  Value *Source = I.getOperand(0);

  // SPIR-V, contrary to LLVM 17+ IR, supports bitcasts between pointers of
  // varying element types. In case of IR coming from older versions of LLVM
  // such bitcasts do not provide sufficient information, should be just skipped
  // here, and handled in insertPtrCastOrAssignTypeInstr.
  if (isPointerTy(I.getType())) {
    I.replaceAllUsesWith(Source);
    I.eraseFromParent();
    return nullptr;
  }

  SmallVector<Type *, 2> Types = {I.getType(), Source->getType()};
  SmallVector<Value *> Args(I.op_begin(), I.op_end());
  auto *NewI = B.CreateIntrinsic(Intrinsic::spv_bitcast, {Types}, {Args});
  std::string InstName = I.hasName() ? I.getName().str() : "";
  I.replaceAllUsesWith(NewI);
  I.eraseFromParent();
  NewI->setName(InstName);
  return NewI;
}

void SPIRVEmitIntrinsics::insertAssignPtrTypeTargetExt(
    TargetExtType *AssignedType, Value *V, IRBuilder<> &B) {
  Type *VTy = V->getType();

  // A couple of sanity checks.
  assert(isPointerTy(VTy) && "Expect a pointer type!");
  if (auto PType = dyn_cast<TypedPointerType>(VTy))
    if (PType->getElementType() != AssignedType)
      report_fatal_error("Unexpected pointer element type!");

  CallInst *AssignCI = GR->findAssignPtrTypeInstr(V);
  if (!AssignCI) {
    buildAssignType(B, AssignedType, V);
    return;
  }

  Type *CurrentType =
      dyn_cast<ConstantAsMetadata>(
          cast<MetadataAsValue>(AssignCI->getOperand(1))->getMetadata())
          ->getType();
  if (CurrentType == AssignedType)
    return;

  // Builtin types cannot be redeclared or casted.
  if (CurrentType->isTargetExtTy())
    report_fatal_error("Type mismatch " + CurrentType->getTargetExtName() +
                           "/" + AssignedType->getTargetExtName() +
                           " for value " + V->getName(),
                       false);

  // Our previous guess about the type seems to be wrong, let's update
  // inferred type according to a new, more precise type information.
  updateAssignType(AssignCI, V, PoisonValue::get(AssignedType));
}

void SPIRVEmitIntrinsics::replacePointerOperandWithPtrCast(
    Instruction *I, Value *Pointer, Type *ExpectedElementType,
    unsigned OperandToReplace, IRBuilder<> &B) {
  // If Pointer is the result of nop BitCastInst (ptr -> ptr), use the source
  // pointer instead. The BitCastInst should be later removed when visited.
  while (BitCastInst *BC = dyn_cast<BitCastInst>(Pointer))
    Pointer = BC->getOperand(0);

  // Do not emit spv_ptrcast if Pointer's element type is ExpectedElementType
  Type *PointerElemTy = deduceElementTypeHelper(Pointer, false);
  if (PointerElemTy == ExpectedElementType ||
      isEquivalentTypes(PointerElemTy, ExpectedElementType))
    return;

  setInsertPointSkippingPhis(B, I);
  MetadataAsValue *VMD = buildMD(PoisonValue::get(ExpectedElementType));
  unsigned AddressSpace = getPointerAddressSpace(Pointer->getType());
  bool FirstPtrCastOrAssignPtrType = true;

  // Do not emit new spv_ptrcast if equivalent one already exists or when
  // spv_assign_ptr_type already targets this pointer with the same element
  // type.
  for (auto User : Pointer->users()) {
    auto *II = dyn_cast<IntrinsicInst>(User);
    if (!II ||
        (II->getIntrinsicID() != Intrinsic::spv_assign_ptr_type &&
         II->getIntrinsicID() != Intrinsic::spv_ptrcast) ||
        II->getOperand(0) != Pointer)
      continue;

    // There is some spv_ptrcast/spv_assign_ptr_type already targeting this
    // pointer.
    FirstPtrCastOrAssignPtrType = false;
    if (II->getOperand(1) != VMD ||
        dyn_cast<ConstantInt>(II->getOperand(2))->getSExtValue() !=
            AddressSpace)
      continue;

    // The spv_ptrcast/spv_assign_ptr_type targeting this pointer is of the same
    // element type and address space.
    if (II->getIntrinsicID() != Intrinsic::spv_ptrcast)
      return;

    // This must be a spv_ptrcast, do not emit new if this one has the same BB
    // as I. Otherwise, search for other spv_ptrcast/spv_assign_ptr_type.
    if (II->getParent() != I->getParent())
      continue;

    I->setOperand(OperandToReplace, II);
    return;
  }

  // // Do not emit spv_ptrcast if it would cast to the default pointer element
  // // type (i8) of the same address space.
  // if (ExpectedElementType->isIntegerTy(8))
  //   return;

  // If this would be the first spv_ptrcast, do not emit spv_ptrcast and emit
  // spv_assign_ptr_type instead.
  if (FirstPtrCastOrAssignPtrType &&
      (isa<Instruction>(Pointer) || isa<Argument>(Pointer))) {
    buildAssignPtr(B, ExpectedElementType, Pointer);
    return;
  }

  // Emit spv_ptrcast
  SmallVector<Type *, 2> Types = {Pointer->getType(), Pointer->getType()};
  SmallVector<Value *, 2> Args = {Pointer, VMD, B.getInt32(AddressSpace)};
  auto *PtrCastI = B.CreateIntrinsic(Intrinsic::spv_ptrcast, {Types}, Args);
  I->setOperand(OperandToReplace, PtrCastI);
}

void SPIRVEmitIntrinsics::insertPtrCastOrAssignTypeInstr(Instruction *I,
                                                         IRBuilder<> &B) {
  // Handle basic instructions:
  StoreInst *SI = dyn_cast<StoreInst>(I);
  if (IsKernelArgInt8(F, SI)) {
    return replacePointerOperandWithPtrCast(
        I, SI->getValueOperand(), IntegerType::getInt8Ty(F->getContext()), 0,
        B);
  } else if (SI) {
    Value *Op = SI->getValueOperand();
    Type *OpTy = Op->getType();
    if (auto *OpI = dyn_cast<Instruction>(Op))
      OpTy = restoreMutatedType(GR, OpI, OpTy);
    if (OpTy == Op->getType())
      OpTy = deduceElementTypeByValueDeep(OpTy, Op, false);
    return replacePointerOperandWithPtrCast(I, SI->getPointerOperand(), OpTy, 1,
                                            B);
  } else if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    return replacePointerOperandWithPtrCast(I, LI->getPointerOperand(),
                                            LI->getType(), 0, B);
  } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I)) {
    return replacePointerOperandWithPtrCast(I, GEPI->getPointerOperand(),
                                            GEPI->getSourceElementType(), 0, B);
  }

  // Handle calls to builtins (non-intrinsics):
  CallInst *CI = dyn_cast<CallInst>(I);
  if (!CI || CI->isIndirectCall() || CI->isInlineAsm() ||
      !CI->getCalledFunction() || CI->getCalledFunction()->isIntrinsic())
    return;

  // collect information about formal parameter types
  std::string DemangledName =
      getOclOrSpirvBuiltinDemangledName(CI->getCalledFunction()->getName());
  Function *CalledF = CI->getCalledFunction();
  SmallVector<Type *, 4> CalledArgTys;
  bool HaveTypes = false;
  for (unsigned OpIdx = 0; OpIdx < CalledF->arg_size(); ++OpIdx) {
    Argument *CalledArg = CalledF->getArg(OpIdx);
    Type *ArgType = CalledArg->getType();
    if (!isPointerTy(ArgType)) {
      CalledArgTys.push_back(nullptr);
    } else if (isTypedPointerTy(ArgType)) {
      CalledArgTys.push_back(cast<TypedPointerType>(ArgType)->getElementType());
      HaveTypes = true;
    } else {
      Type *ElemTy = GR->findDeducedElementType(CalledArg);
      if (!ElemTy && hasPointeeTypeAttr(CalledArg))
        ElemTy = getPointeeTypeByAttr(CalledArg);
      if (!ElemTy) {
        ElemTy = getPointeeTypeByCallInst(DemangledName, CalledF, OpIdx);
        if (ElemTy) {
          GR->addDeducedElementType(CalledArg, ElemTy);
        } else {
          for (User *U : CalledArg->users()) {
            if (Instruction *Inst = dyn_cast<Instruction>(U)) {
              if ((ElemTy = deduceElementTypeHelper(Inst, false)) != nullptr)
                break;
            }
          }
        }
      }
      HaveTypes |= ElemTy != nullptr;
      CalledArgTys.push_back(ElemTy);
    }
  }

  if (DemangledName.empty() && !HaveTypes)
    return;

  for (unsigned OpIdx = 0; OpIdx < CI->arg_size(); OpIdx++) {
    Value *ArgOperand = CI->getArgOperand(OpIdx);
    if (!isPointerTy(ArgOperand->getType()))
      continue;

    // Constants (nulls/undefs) are handled in insertAssignPtrTypeIntrs()
    if (!isa<Instruction>(ArgOperand) && !isa<Argument>(ArgOperand)) {
      // However, we may have assumptions about the formal argument's type and
      // may have a need to insert a ptr cast for the actual parameter of this
      // call.
      Argument *CalledArg = CalledF->getArg(OpIdx);
      if (!GR->findDeducedElementType(CalledArg))
        continue;
    }

    Type *ExpectedType =
        OpIdx < CalledArgTys.size() ? CalledArgTys[OpIdx] : nullptr;
    if (!ExpectedType && !DemangledName.empty())
      ExpectedType = SPIRV::parseBuiltinCallArgumentBaseType(
          DemangledName, OpIdx, I->getContext());
    if (!ExpectedType || ExpectedType->isVoidTy())
      continue;

    if (ExpectedType->isTargetExtTy())
      insertAssignPtrTypeTargetExt(cast<TargetExtType>(ExpectedType),
                                   ArgOperand, B);
    else
      replacePointerOperandWithPtrCast(CI, ArgOperand, ExpectedType, OpIdx, B);
  }
}

Instruction *SPIRVEmitIntrinsics::visitInsertElementInst(InsertElementInst &I) {
  SmallVector<Type *, 4> Types = {I.getType(), I.getOperand(0)->getType(),
                                  I.getOperand(1)->getType(),
                                  I.getOperand(2)->getType()};
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  SmallVector<Value *> Args(I.op_begin(), I.op_end());
  auto *NewI = B.CreateIntrinsic(Intrinsic::spv_insertelt, {Types}, {Args});
  std::string InstName = I.hasName() ? I.getName().str() : "";
  I.replaceAllUsesWith(NewI);
  I.eraseFromParent();
  NewI->setName(InstName);
  return NewI;
}

Instruction *
SPIRVEmitIntrinsics::visitExtractElementInst(ExtractElementInst &I) {
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  SmallVector<Type *, 3> Types = {I.getType(), I.getVectorOperandType(),
                                  I.getIndexOperand()->getType()};
  SmallVector<Value *, 2> Args = {I.getVectorOperand(), I.getIndexOperand()};
  auto *NewI = B.CreateIntrinsic(Intrinsic::spv_extractelt, {Types}, {Args});
  std::string InstName = I.hasName() ? I.getName().str() : "";
  I.replaceAllUsesWith(NewI);
  I.eraseFromParent();
  NewI->setName(InstName);
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitInsertValueInst(InsertValueInst &I) {
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  SmallVector<Type *, 1> Types = {I.getInsertedValueOperand()->getType()};
  SmallVector<Value *> Args;
  for (auto &Op : I.operands())
    if (isa<UndefValue>(Op))
      Args.push_back(UndefValue::get(B.getInt32Ty()));
    else
      Args.push_back(Op);
  for (auto &Op : I.indices())
    Args.push_back(B.getInt32(Op));
  Instruction *NewI =
      B.CreateIntrinsic(Intrinsic::spv_insertv, {Types}, {Args});
  replaceMemInstrUses(&I, NewI, B);
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitExtractValueInst(ExtractValueInst &I) {
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  SmallVector<Value *> Args;
  for (auto &Op : I.operands())
    Args.push_back(Op);
  for (auto &Op : I.indices())
    Args.push_back(B.getInt32(Op));
  auto *NewI =
      B.CreateIntrinsic(Intrinsic::spv_extractv, {I.getType()}, {Args});
  I.replaceAllUsesWith(NewI);
  I.eraseFromParent();
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitLoadInst(LoadInst &I) {
  if (!I.getType()->isAggregateType())
    return &I;
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  TrackConstants = false;
  const auto *TLI = TM->getSubtargetImpl()->getTargetLowering();
  MachineMemOperand::Flags Flags =
      TLI->getLoadMemOperandFlags(I, F->getDataLayout());
  auto *NewI =
      B.CreateIntrinsic(Intrinsic::spv_load, {I.getOperand(0)->getType()},
                        {I.getPointerOperand(), B.getInt16(Flags),
                         B.getInt8(I.getAlign().value())});
  replaceMemInstrUses(&I, NewI, B);
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitStoreInst(StoreInst &I) {
  if (!AggrStores.contains(&I))
    return &I;
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  TrackConstants = false;
  const auto *TLI = TM->getSubtargetImpl()->getTargetLowering();
  MachineMemOperand::Flags Flags =
      TLI->getStoreMemOperandFlags(I, F->getDataLayout());
  auto *PtrOp = I.getPointerOperand();
  auto *NewI = B.CreateIntrinsic(
      Intrinsic::spv_store, {I.getValueOperand()->getType(), PtrOp->getType()},
      {I.getValueOperand(), PtrOp, B.getInt16(Flags),
       B.getInt8(I.getAlign().value())});
  I.eraseFromParent();
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitAllocaInst(AllocaInst &I) {
  Value *ArraySize = nullptr;
  if (I.isArrayAllocation()) {
    const SPIRVSubtarget *STI = TM->getSubtargetImpl(*I.getFunction());
    if (!STI->canUseExtension(
            SPIRV::Extension::SPV_INTEL_variable_length_array))
      report_fatal_error(
          "array allocation: this instruction requires the following "
          "SPIR-V extension: SPV_INTEL_variable_length_array",
          false);
    ArraySize = I.getArraySize();
  }
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  TrackConstants = false;
  Type *PtrTy = I.getType();
  auto *NewI =
      ArraySize ? B.CreateIntrinsic(Intrinsic::spv_alloca_array,
                                    {PtrTy, ArraySize->getType()}, {ArraySize})
                : B.CreateIntrinsic(Intrinsic::spv_alloca, {PtrTy}, {});
  std::string InstName = I.hasName() ? I.getName().str() : "";
  I.replaceAllUsesWith(NewI);
  I.eraseFromParent();
  NewI->setName(InstName);
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) {
  assert(I.getType()->isAggregateType() && "Aggregate result is expected");
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  SmallVector<Value *> Args;
  for (auto &Op : I.operands())
    Args.push_back(Op);
  Args.push_back(B.getInt32(I.getSyncScopeID()));
  Args.push_back(B.getInt32(
      static_cast<uint32_t>(getMemSemantics(I.getSuccessOrdering()))));
  Args.push_back(B.getInt32(
      static_cast<uint32_t>(getMemSemantics(I.getFailureOrdering()))));
  auto *NewI = B.CreateIntrinsic(Intrinsic::spv_cmpxchg,
                                 {I.getPointerOperand()->getType()}, {Args});
  replaceMemInstrUses(&I, NewI, B);
  return NewI;
}

Instruction *SPIRVEmitIntrinsics::visitUnreachableInst(UnreachableInst &I) {
  IRBuilder<> B(I.getParent());
  B.SetInsertPoint(&I);
  B.CreateIntrinsic(Intrinsic::spv_unreachable, {}, {});
  return &I;
}

void SPIRVEmitIntrinsics::processGlobalValue(GlobalVariable &GV,
                                             IRBuilder<> &B) {
  // Skip special artifical variable llvm.global.annotations.
  if (GV.getName() == "llvm.global.annotations")
    return;
  if (GV.hasInitializer() && !isa<UndefValue>(GV.getInitializer())) {
    // Deduce element type and store results in Global Registry.
    // Result is ignored, because TypedPointerType is not supported
    // by llvm IR general logic.
    deduceElementTypeHelper(&GV, false);
    Constant *Init = GV.getInitializer();
    Type *Ty = isAggrConstForceInt32(Init) ? B.getInt32Ty() : Init->getType();
    Constant *Const = isAggrConstForceInt32(Init) ? B.getInt32(1) : Init;
    auto *InitInst = B.CreateIntrinsic(Intrinsic::spv_init_global,
                                       {GV.getType(), Ty}, {&GV, Const});
    InitInst->setArgOperand(1, Init);
  }
  if ((!GV.hasInitializer() || isa<UndefValue>(GV.getInitializer())) &&
      GV.getNumUses() == 0)
    B.CreateIntrinsic(Intrinsic::spv_unref_global, GV.getType(), &GV);
}

// Return true, if we can't decide what is the pointee type now and will get
// back to the question later. Return false is spv_assign_ptr_type is not needed
// or can be inserted immediately.
bool SPIRVEmitIntrinsics::insertAssignPtrTypeIntrs(Instruction *I,
                                                   IRBuilder<> &B,
                                                   bool UnknownElemTypeI8) {
  reportFatalOnTokenType(I);
  if (!isPointerTy(I->getType()) || !requireAssignType(I) ||
      isa<BitCastInst>(I))
    return false;

  setInsertPointAfterDef(B, I);
  if (Type *ElemTy = deduceElementType(I, UnknownElemTypeI8)) {
    buildAssignPtr(B, ElemTy, I);
    return false;
  }
  return true;
}

void SPIRVEmitIntrinsics::insertAssignTypeIntrs(Instruction *I,
                                                IRBuilder<> &B) {
  // TODO: extend the list of functions with known result types
  static StringMap<unsigned> ResTypeWellKnown = {
      {"async_work_group_copy", WellKnownTypes::Event},
      {"async_work_group_strided_copy", WellKnownTypes::Event},
      {"__spirv_GroupAsyncCopy", WellKnownTypes::Event}};

  reportFatalOnTokenType(I);

  bool IsKnown = false;
  if (auto *CI = dyn_cast<CallInst>(I)) {
    if (!CI->isIndirectCall() && !CI->isInlineAsm() &&
        CI->getCalledFunction() && !CI->getCalledFunction()->isIntrinsic()) {
      Function *CalledF = CI->getCalledFunction();
      std::string DemangledName =
          getOclOrSpirvBuiltinDemangledName(CalledF->getName());
      if (DemangledName.length() > 0)
        DemangledName = SPIRV::lookupBuiltinNameHelper(DemangledName);
      auto ResIt = ResTypeWellKnown.find(DemangledName);
      if (ResIt != ResTypeWellKnown.end()) {
        IsKnown = true;
        setInsertPointAfterDef(B, I);
        switch (ResIt->second) {
        case WellKnownTypes::Event:
          buildAssignType(B, TargetExtType::get(I->getContext(), "spirv.Event"),
                          I);
          break;
        }
      }
    }
  }

  Type *Ty = I->getType();
  if (!IsKnown && !Ty->isVoidTy() && !isPointerTy(Ty) && requireAssignType(I)) {
    setInsertPointAfterDef(B, I);
    Type *TypeToAssign = Ty;
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      if (II->getIntrinsicID() == Intrinsic::spv_const_composite ||
          II->getIntrinsicID() == Intrinsic::spv_undef) {
        auto It = AggrConstTypes.find(II);
        if (It == AggrConstTypes.end())
          report_fatal_error("Unknown composite intrinsic type");
        TypeToAssign = It->second;
      }
    }
    TypeToAssign = restoreMutatedType(GR, I, TypeToAssign);
    buildAssignType(B, TypeToAssign, I);
  }
  for (const auto &Op : I->operands()) {
    if (isa<ConstantPointerNull>(Op) || isa<UndefValue>(Op) ||
        // Check GetElementPtrConstantExpr case.
        (isa<ConstantExpr>(Op) && isa<GEPOperator>(Op))) {
      setInsertPointSkippingPhis(B, I);
      Type *OpTy = Op->getType();
      if (isa<UndefValue>(Op) && OpTy->isAggregateType()) {
        CallInst *AssignCI =
            buildIntrWithMD(Intrinsic::spv_assign_type, {B.getInt32Ty()}, Op,
                            UndefValue::get(B.getInt32Ty()), {}, B);
        GR->addAssignPtrTypeInstr(Op, AssignCI);
      } else if (!isa<Instruction>(Op)) {
        Type *OpTy = Op->getType();
        if (auto PType = dyn_cast<TypedPointerType>(OpTy)) {
          buildAssignPtr(B, PType->getElementType(), Op);
        } else if (isPointerTy(OpTy)) {
          Type *ElemTy = GR->findDeducedElementType(Op);
          buildAssignPtr(B, ElemTy ? ElemTy : deduceElementType(Op, true), Op);
        } else {
          CallInst *AssignCI = buildIntrWithMD(Intrinsic::spv_assign_type,
                                               {OpTy}, Op, Op, {}, B);
          GR->addAssignPtrTypeInstr(Op, AssignCI);
        }
      }
    }
  }
}

void SPIRVEmitIntrinsics::insertSpirvDecorations(Instruction *I,
                                                 IRBuilder<> &B) {
  if (MDNode *MD = I->getMetadata("spirv.Decorations")) {
    setInsertPointAfterDef(B, I);
    B.CreateIntrinsic(Intrinsic::spv_assign_decoration, {I->getType()},
                      {I, MetadataAsValue::get(I->getContext(), MD)});
  }
}

void SPIRVEmitIntrinsics::processInstrAfterVisit(Instruction *I,
                                                 IRBuilder<> &B) {
  auto *II = dyn_cast<IntrinsicInst>(I);
  if (II && II->getIntrinsicID() == Intrinsic::spv_const_composite &&
      TrackConstants) {
    setInsertPointAfterDef(B, I);
    auto t = AggrConsts.find(I);
    assert(t != AggrConsts.end());
    auto *NewOp =
        buildIntrWithMD(Intrinsic::spv_track_constant,
                        {II->getType(), II->getType()}, t->second, I, {}, B);
    I->replaceAllUsesWith(NewOp);
    NewOp->setArgOperand(0, I);
  }
  bool IsPhi = isa<PHINode>(I), BPrepared = false;
  for (const auto &Op : I->operands()) {
    if (isa<PHINode>(I) || isa<SwitchInst>(I))
      TrackConstants = false;
    if ((isa<ConstantData>(Op) || isa<ConstantExpr>(Op)) && TrackConstants) {
      unsigned OpNo = Op.getOperandNo();
      if (II && ((II->getIntrinsicID() == Intrinsic::spv_gep && OpNo == 0) ||
                 (II->paramHasAttr(OpNo, Attribute::ImmArg))))
        continue;
      if (!BPrepared) {
        IsPhi ? B.SetInsertPointPastAllocas(I->getParent()->getParent())
              : B.SetInsertPoint(I);
        BPrepared = true;
      }
      Value *OpTyVal = Op;
      if (Op->getType()->isTargetExtTy())
        OpTyVal = PoisonValue::get(Op->getType());
      auto *NewOp = buildIntrWithMD(Intrinsic::spv_track_constant,
                                    {Op->getType(), OpTyVal->getType()}, Op,
                                    OpTyVal, {}, B);
      I->setOperand(OpNo, NewOp);
    }
  }
  if (I->hasName()) {
    reportFatalOnTokenType(I);
    setInsertPointAfterDef(B, I);
    std::vector<Value *> Args = {I};
    addStringImm(I->getName(), B, Args);
    B.CreateIntrinsic(Intrinsic::spv_assign_name, {I->getType()}, Args);
  }
}

Type *SPIRVEmitIntrinsics::deduceFunParamElementType(Function *F,
                                                     unsigned OpIdx) {
  std::unordered_set<Function *> FVisited;
  return deduceFunParamElementType(F, OpIdx, FVisited);
}

Type *SPIRVEmitIntrinsics::deduceFunParamElementType(
    Function *F, unsigned OpIdx, std::unordered_set<Function *> &FVisited) {
  // maybe a cycle
  if (FVisited.find(F) != FVisited.end())
    return nullptr;
  FVisited.insert(F);

  std::unordered_set<Value *> Visited;
  SmallVector<std::pair<Function *, unsigned>> Lookup;
  // search in function's call sites
  for (User *U : F->users()) {
    CallInst *CI = dyn_cast<CallInst>(U);
    if (!CI || OpIdx >= CI->arg_size())
      continue;
    Value *OpArg = CI->getArgOperand(OpIdx);
    if (!isPointerTy(OpArg->getType()))
      continue;
    // maybe we already know operand's element type
    if (Type *KnownTy = GR->findDeducedElementType(OpArg))
      return KnownTy;
    // try to deduce from the operand itself
    Visited.clear();
    if (Type *Ty = deduceElementTypeHelper(OpArg, Visited, false))
      return Ty;
    // search in actual parameter's users
    for (User *OpU : OpArg->users()) {
      Instruction *Inst = dyn_cast<Instruction>(OpU);
      if (!Inst || Inst == CI)
        continue;
      Visited.clear();
      if (Type *Ty = deduceElementTypeHelper(Inst, Visited, false))
        return Ty;
    }
    // check if it's a formal parameter of the outer function
    if (!CI->getParent() || !CI->getParent()->getParent())
      continue;
    Function *OuterF = CI->getParent()->getParent();
    if (FVisited.find(OuterF) != FVisited.end())
      continue;
    for (unsigned i = 0; i < OuterF->arg_size(); ++i) {
      if (OuterF->getArg(i) == OpArg) {
        Lookup.push_back(std::make_pair(OuterF, i));
        break;
      }
    }
  }

  // search in function parameters
  for (auto &Pair : Lookup) {
    if (Type *Ty = deduceFunParamElementType(Pair.first, Pair.second, FVisited))
      return Ty;
  }

  return nullptr;
}

void SPIRVEmitIntrinsics::processParamTypesByFunHeader(Function *F,
                                                       IRBuilder<> &B) {
  B.SetInsertPointPastAllocas(F);
  for (unsigned OpIdx = 0; OpIdx < F->arg_size(); ++OpIdx) {
    Argument *Arg = F->getArg(OpIdx);
    if (!isUntypedPointerTy(Arg->getType()))
      continue;
    Type *ElemTy = GR->findDeducedElementType(Arg);
    if (!ElemTy && hasPointeeTypeAttr(Arg) &&
        (ElemTy = getPointeeTypeByAttr(Arg)) != nullptr)
      buildAssignPtr(B, ElemTy, Arg);
  }
}

void SPIRVEmitIntrinsics::processParamTypes(Function *F, IRBuilder<> &B) {
  B.SetInsertPointPastAllocas(F);
  for (unsigned OpIdx = 0; OpIdx < F->arg_size(); ++OpIdx) {
    Argument *Arg = F->getArg(OpIdx);
    if (!isUntypedPointerTy(Arg->getType()))
      continue;
    Type *ElemTy = GR->findDeducedElementType(Arg);
    if (!ElemTy && (ElemTy = deduceFunParamElementType(F, OpIdx)) != nullptr)
      buildAssignPtr(B, ElemTy, Arg);
  }
}

bool SPIRVEmitIntrinsics::runOnFunction(Function &Func) {
  if (Func.isDeclaration())
    return false;

  const SPIRVSubtarget &ST = TM->getSubtarget<SPIRVSubtarget>(Func);
  GR = ST.getSPIRVGlobalRegistry();
  InstrSet = ST.isOpenCLEnv() ? SPIRV::InstructionSet::OpenCL_std
                              : SPIRV::InstructionSet::GLSL_std_450;

  F = &Func;
  IRBuilder<> B(Func.getContext());
  AggrConsts.clear();
  AggrConstTypes.clear();
  AggrStores.clear();

  processParamTypesByFunHeader(F, B);

  // StoreInst's operand type can be changed during the next transformations,
  // so we need to store it in the set. Also store already transformed types.
  for (auto &I : instructions(Func)) {
    StoreInst *SI = dyn_cast<StoreInst>(&I);
    if (!SI)
      continue;
    Type *ElTy = SI->getValueOperand()->getType();
    if (ElTy->isAggregateType() || ElTy->isVectorTy())
      AggrStores.insert(&I);
  }

  B.SetInsertPoint(&Func.getEntryBlock(), Func.getEntryBlock().begin());
  for (auto &GV : Func.getParent()->globals())
    processGlobalValue(GV, B);

  preprocessUndefs(B);
  preprocessCompositeConstants(B);
  SmallVector<Instruction *> Worklist;
  for (auto &I : instructions(Func))
    Worklist.push_back(&I);

  for (auto &I : Worklist) {
    // Don't emit intrinsincs for convergence intrinsics.
    if (isConvergenceIntrinsic(I))
      continue;

    bool Postpone = insertAssignPtrTypeIntrs(I, B, false);
    // if Postpone is true, we can't decide on pointee type yet
    insertAssignTypeIntrs(I, B);
    insertPtrCastOrAssignTypeInstr(I, B);
    insertSpirvDecorations(I, B);
    // if instruction requires a pointee type set, let's check if we know it
    // already, and force it to be i8 if not
    if (Postpone && !GR->findAssignPtrTypeInstr(I))
      insertAssignPtrTypeIntrs(I, B, true);
  }

  for (auto &I : instructions(Func))
    deduceOperandElementType(&I);

  for (auto *I : Worklist) {
    TrackConstants = true;
    if (!I->getType()->isVoidTy() || isa<StoreInst>(I))
      setInsertPointAfterDef(B, I);
    // Visitors return either the original/newly created instruction for further
    // processing, nullptr otherwise.
    I = visit(*I);
    if (!I)
      continue;

    // Don't emit intrinsics for convergence operations.
    if (isConvergenceIntrinsic(I))
      continue;

    processInstrAfterVisit(I, B);
  }

  return true;
}

// Try to deduce a better type for pointers to untyped ptr.
bool SPIRVEmitIntrinsics::postprocessTypes() {
  bool Changed = false;
  if (!GR)
    return Changed;
  for (auto IB = PostprocessWorklist.rbegin(), IE = PostprocessWorklist.rend();
       IB != IE; ++IB) {
    CallInst *AssignCI = GR->findAssignPtrTypeInstr(*IB);
    Type *KnownTy = GR->findDeducedElementType(*IB);
    if (!KnownTy || !AssignCI || !isa<Instruction>(AssignCI->getArgOperand(0)))
      continue;
    Instruction *I = cast<Instruction>(AssignCI->getArgOperand(0));
    for (User *U : I->users()) {
      Instruction *Inst = dyn_cast<Instruction>(U);
      if (!Inst || isa<IntrinsicInst>(Inst))
        continue;
      deduceOperandElementType(Inst, I, KnownTy, AssignCI);
      if (KnownTy != GR->findDeducedElementType(I)) {
        Changed = true;
        break;
      }
    }
  }
  return Changed;
}

bool SPIRVEmitIntrinsics::runOnModule(Module &M) {
  bool Changed = false;

  UncompleteTypeInfo.clear();
  PostprocessWorklist.clear();
  for (auto &F : M)
    Changed |= runOnFunction(F);

  for (auto &F : M) {
    // check if function parameter types are set
    if (!F.isDeclaration() && !F.isIntrinsic()) {
      const SPIRVSubtarget &ST = TM->getSubtarget<SPIRVSubtarget>(F);
      GR = ST.getSPIRVGlobalRegistry();
      IRBuilder<> B(F.getContext());
      processParamTypes(&F, B);
    }
  }

  Changed |= postprocessTypes();

  return Changed;
}

ModulePass *llvm::createSPIRVEmitIntrinsicsPass(SPIRVTargetMachine *TM) {
  return new SPIRVEmitIntrinsics(TM);
}
