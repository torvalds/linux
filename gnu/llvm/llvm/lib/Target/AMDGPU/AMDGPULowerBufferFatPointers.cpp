//===-- AMDGPULowerBufferFatPointers.cpp ---------------------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers operations on buffer fat pointers (addrspace 7) to
// operations on buffer resources (addrspace 8) and is needed for correct
// codegen.
//
// # Background
//
// Address space 7 (the buffer fat pointer) is a 160-bit pointer that consists
// of a 128-bit buffer descriptor and a 32-bit offset into that descriptor.
// The buffer resource part needs to be it needs to be a "raw" buffer resource
// (it must have a stride of 0 and bounds checks must be in raw buffer mode
// or disabled).
//
// When these requirements are met, a buffer resource can be treated as a
// typical (though quite wide) pointer that follows typical LLVM pointer
// semantics. This allows the frontend to reason about such buffers (which are
// often encountered in the context of SPIR-V kernels).
//
// However, because of their non-power-of-2 size, these fat pointers cannot be
// present during translation to MIR (though this restriction may be lifted
// during the transition to GlobalISel). Therefore, this pass is needed in order
// to correctly implement these fat pointers.
//
// The resource intrinsics take the resource part (the address space 8 pointer)
// and the offset part (the 32-bit integer) as separate arguments. In addition,
// many users of these buffers manipulate the offset while leaving the resource
// part alone. For these reasons, we want to typically separate the resource
// and offset parts into separate variables, but combine them together when
// encountering cases where this is required, such as by inserting these values
// into aggretates or moving them to memory.
//
// Therefore, at a high level, `ptr addrspace(7) %x` becomes `ptr addrspace(8)
// %x.rsrc` and `i32 %x.off`, which will be combined into `{ptr addrspace(8),
// i32} %x = {%x.rsrc, %x.off}` if needed. Similarly, `vector<Nxp7>` becomes
// `{vector<Nxp8>, vector<Nxi32 >}` and its component parts.
//
// # Implementation
//
// This pass proceeds in three main phases:
//
// ## Rewriting loads and stores of p7
//
// The first phase is to rewrite away all loads and stors of `ptr addrspace(7)`,
// including aggregates containing such pointers, to ones that use `i160`. This
// is handled by `StoreFatPtrsAsIntsVisitor` , which visits loads, stores, and
// allocas and, if the loaded or stored type contains `ptr addrspace(7)`,
// rewrites that type to one where the p7s are replaced by i160s, copying other
// parts of aggregates as needed. In the case of a store, each pointer is
// `ptrtoint`d to i160 before storing, and load integers are `inttoptr`d back.
// This same transformation is applied to vectors of pointers.
//
// Such a transformation allows the later phases of the pass to not need
// to handle buffer fat pointers moving to and from memory, where we load
// have to handle the incompatibility between a `{Nxp8, Nxi32}` representation
// and `Nxi60` directly. Instead, that transposing action (where the vectors
// of resources and vectors of offsets are concatentated before being stored to
// memory) are handled through implementing `inttoptr` and `ptrtoint` only.
//
// Atomics operations on `ptr addrspace(7)` values are not suppported, as the
// hardware does not include a 160-bit atomic.
//
// ## Type remapping
//
// We use a `ValueMapper` to mangle uses of [vectors of] buffer fat pointers
// to the corresponding struct type, which has a resource part and an offset
// part.
//
// This uses a `BufferFatPtrToStructTypeMap` and a `FatPtrConstMaterializer`
// to, usually by way of `setType`ing values. Constants are handled here
// because there isn't a good way to fix them up later.
//
// This has the downside of leaving the IR in an invalid state (for example,
// the instruction `getelementptr {ptr addrspace(8), i32} %p, ...` will exist),
// but all such invalid states will be resolved by the third phase.
//
// Functions that don't take buffer fat pointers are modified in place. Those
// that do take such pointers have their basic blocks moved to a new function
// with arguments that are {ptr addrspace(8), i32} arguments and return values.
// This phase also records intrinsics so that they can be remangled or deleted
// later.
//
//
// ## Splitting pointer structs
//
// The meat of this pass consists of defining semantics for operations that
// produce or consume [vectors of] buffer fat pointers in terms of their
// resource and offset parts. This is accomplished throgh the `SplitPtrStructs`
// visitor.
//
// In the first pass through each function that is being lowered, the splitter
// inserts new instructions to implement the split-structures behavior, which is
// needed for correctness and performance. It records a list of "split users",
// instructions that are being replaced by operations on the resource and offset
// parts.
//
// Split users do not necessarily need to produce parts themselves (
// a `load float, ptr addrspace(7)` does not, for example), but, if they do not
// generate fat buffer pointers, they must RAUW in their replacement
// instructions during the initial visit.
//
// When these new instructions are created, they use the split parts recorded
// for their initial arguments in order to generate their replacements, creating
// a parallel set of instructions that does not refer to the original fat
// pointer values but instead to their resource and offset components.
//
// Instructions, such as `extractvalue`, that produce buffer fat pointers from
// sources that do not have split parts, have such parts generated using
// `extractvalue`. This is also the initial handling of PHI nodes, which
// are then cleaned up.
//
// ### Conditionals
//
// PHI nodes are initially given resource parts via `extractvalue`. However,
// this is not an efficient rewrite of such nodes, as, in most cases, the
// resource part in a conditional or loop remains constant throughout the loop
// and only the offset varies. Failing to optimize away these constant resources
// would cause additional registers to be sent around loops and might lead to
// waterfall loops being generated for buffer operations due to the
// "non-uniform" resource argument.
//
// Therefore, after all instructions have been visited, the pointer splitter
// post-processes all encountered conditionals. Given a PHI node or select,
// getPossibleRsrcRoots() collects all values that the resource parts of that
// conditional's input could come from as well as collecting all conditional
// instructions encountered during the search. If, after filtering out the
// initial node itself, the set of encountered conditionals is a subset of the
// potential roots and there is a single potential resource that isn't in the
// conditional set, that value is the only possible value the resource argument
// could have throughout the control flow.
//
// If that condition is met, then a PHI node can have its resource part changed
// to the singleton value and then be replaced by a PHI on the offsets.
// Otherwise, each PHI node is split into two, one for the resource part and one
// for the offset part, which replace the temporary `extractvalue` instructions
// that were added during the first pass.
//
// Similar logic applies to `select`, where
// `%z = select i1 %cond, %cond, ptr addrspace(7) %x, ptr addrspace(7) %y`
// can be split into `%z.rsrc = %x.rsrc` and
// `%z.off = select i1 %cond, ptr i32 %x.off, i32 %y.off`
// if both `%x` and `%y` have the same resource part, but two `select`
// operations will be needed if they do not.
//
// ### Final processing
//
// After conditionals have been cleaned up, the IR for each function is
// rewritten to remove all the old instructions that have been split up.
//
// Any instruction that used to produce a buffer fat pointer (and therefore now
// produces a resource-and-offset struct after type remapping) is
// replaced as follows:
// 1. All debug value annotations are cloned to reflect that the resource part
//    and offset parts are computed separately and constitute different
//    fragments of the underlying source language variable.
// 2. All uses that were themselves split are replaced by a `poison` of the
//    struct type, as they will themselves be erased soon. This rule, combined
//    with debug handling, should leave the use lists of split instructions
//    empty in almost all cases.
// 3. If a user of the original struct-valued result remains, the structure
//    needed for the new types to work is constructed out of the newly-defined
//    parts, and the original instruction is replaced by this structure
//    before being erased. Instructions requiring this construction include
//    `ret` and `insertvalue`.
//
// # Consequences
//
// This pass does not alter the CFG.
//
// Alias analysis information will become coarser, as the LLVM alias analyzer
// cannot handle the buffer intrinsics. Specifically, while we can determine
// that the following two loads do not alias:
// ```
//   %y = getelementptr i32, ptr addrspace(7) %x, i32 1
//   %a = load i32, ptr addrspace(7) %x
//   %b = load i32, ptr addrspace(7) %y
// ```
// we cannot (except through some code that runs during scheduling) determine
// that the rewritten loads below do not alias.
// ```
//   %y.off = add i32 %x.off, 1
//   %a = call @llvm.amdgcn.raw.ptr.buffer.load(ptr addrspace(8) %x.rsrc, i32
//     %x.off, ...)
//   %b = call @llvm.amdgcn.raw.ptr.buffer.load(ptr addrspace(8)
//     %x.rsrc, i32 %y.off, ...)
// ```
// However, existing alias information is preserved.
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUTargetMachine.h"
#include "GCNSubtarget.h"
#include "SIDefines.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ReplaceConstant.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#define DEBUG_TYPE "amdgpu-lower-buffer-fat-pointers"

using namespace llvm;

static constexpr unsigned BufferOffsetWidth = 32;

namespace {
/// Recursively replace instances of ptr addrspace(7) and vector<Nxptr
/// addrspace(7)> with some other type as defined by the relevant subclass.
class BufferFatPtrTypeLoweringBase : public ValueMapTypeRemapper {
  DenseMap<Type *, Type *> Map;

  Type *remapTypeImpl(Type *Ty, SmallPtrSetImpl<StructType *> &Seen);

protected:
  virtual Type *remapScalar(PointerType *PT) = 0;
  virtual Type *remapVector(VectorType *VT) = 0;

  const DataLayout &DL;

public:
  BufferFatPtrTypeLoweringBase(const DataLayout &DL) : DL(DL) {}
  Type *remapType(Type *SrcTy) override;
  void clear() { Map.clear(); }
};

/// Remap ptr addrspace(7) to i160 and vector<Nxptr addrspace(7)> to
/// vector<Nxi60> in order to correctly handling loading/storing these values
/// from memory.
class BufferFatPtrToIntTypeMap : public BufferFatPtrTypeLoweringBase {
  using BufferFatPtrTypeLoweringBase::BufferFatPtrTypeLoweringBase;

protected:
  Type *remapScalar(PointerType *PT) override { return DL.getIntPtrType(PT); }
  Type *remapVector(VectorType *VT) override { return DL.getIntPtrType(VT); }
};

/// Remap ptr addrspace(7) to {ptr addrspace(8), i32} (the resource and offset
/// parts of the pointer) so that we can easily rewrite operations on these
/// values that aren't loading them from or storing them to memory.
class BufferFatPtrToStructTypeMap : public BufferFatPtrTypeLoweringBase {
  using BufferFatPtrTypeLoweringBase::BufferFatPtrTypeLoweringBase;

protected:
  Type *remapScalar(PointerType *PT) override;
  Type *remapVector(VectorType *VT) override;
};
} // namespace

// This code is adapted from the type remapper in lib/Linker/IRMover.cpp
Type *BufferFatPtrTypeLoweringBase::remapTypeImpl(
    Type *Ty, SmallPtrSetImpl<StructType *> &Seen) {
  Type **Entry = &Map[Ty];
  if (*Entry)
    return *Entry;
  if (auto *PT = dyn_cast<PointerType>(Ty)) {
    if (PT->getAddressSpace() == AMDGPUAS::BUFFER_FAT_POINTER) {
      return *Entry = remapScalar(PT);
    }
  }
  if (auto *VT = dyn_cast<VectorType>(Ty)) {
    auto *PT = dyn_cast<PointerType>(VT->getElementType());
    if (PT && PT->getAddressSpace() == AMDGPUAS::BUFFER_FAT_POINTER) {
      return *Entry = remapVector(VT);
    }
    return *Entry = Ty;
  }
  // Whether the type is one that is structurally uniqued - that is, if it is
  // not a named struct (the only kind of type where multiple structurally
  // identical types that have a distinct `Type*`)
  StructType *TyAsStruct = dyn_cast<StructType>(Ty);
  bool IsUniqued = !TyAsStruct || TyAsStruct->isLiteral();
  // Base case for ints, floats, opaque pointers, and so on, which don't
  // require recursion.
  if (Ty->getNumContainedTypes() == 0 && IsUniqued)
    return *Entry = Ty;
  if (!IsUniqued) {
    // Create a dummy type for recursion purposes.
    if (!Seen.insert(TyAsStruct).second) {
      StructType *Placeholder = StructType::create(Ty->getContext());
      return *Entry = Placeholder;
    }
  }
  bool Changed = false;
  SmallVector<Type *> ElementTypes(Ty->getNumContainedTypes(), nullptr);
  for (unsigned int I = 0, E = Ty->getNumContainedTypes(); I < E; ++I) {
    Type *OldElem = Ty->getContainedType(I);
    Type *NewElem = remapTypeImpl(OldElem, Seen);
    ElementTypes[I] = NewElem;
    Changed |= (OldElem != NewElem);
  }
  // Recursive calls to remapTypeImpl() may have invalidated pointer.
  Entry = &Map[Ty];
  if (!Changed) {
    return *Entry = Ty;
  }
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty))
    return *Entry = ArrayType::get(ElementTypes[0], ArrTy->getNumElements());
  if (auto *FnTy = dyn_cast<FunctionType>(Ty))
    return *Entry = FunctionType::get(ElementTypes[0],
                                      ArrayRef(ElementTypes).slice(1),
                                      FnTy->isVarArg());
  if (auto *STy = dyn_cast<StructType>(Ty)) {
    // Genuine opaque types don't have a remapping.
    if (STy->isOpaque())
      return *Entry = Ty;
    bool IsPacked = STy->isPacked();
    if (IsUniqued)
      return *Entry = StructType::get(Ty->getContext(), ElementTypes, IsPacked);
    SmallString<16> Name(STy->getName());
    STy->setName("");
    Type **RecursionEntry = &Map[Ty];
    if (*RecursionEntry) {
      auto *Placeholder = cast<StructType>(*RecursionEntry);
      Placeholder->setBody(ElementTypes, IsPacked);
      Placeholder->setName(Name);
      return *Entry = Placeholder;
    }
    return *Entry = StructType::create(Ty->getContext(), ElementTypes, Name,
                                       IsPacked);
  }
  llvm_unreachable("Unknown type of type that contains elements");
}

Type *BufferFatPtrTypeLoweringBase::remapType(Type *SrcTy) {
  SmallPtrSet<StructType *, 2> Visited;
  return remapTypeImpl(SrcTy, Visited);
}

Type *BufferFatPtrToStructTypeMap::remapScalar(PointerType *PT) {
  LLVMContext &Ctx = PT->getContext();
  return StructType::get(PointerType::get(Ctx, AMDGPUAS::BUFFER_RESOURCE),
                         IntegerType::get(Ctx, BufferOffsetWidth));
}

Type *BufferFatPtrToStructTypeMap::remapVector(VectorType *VT) {
  ElementCount EC = VT->getElementCount();
  LLVMContext &Ctx = VT->getContext();
  Type *RsrcVec =
      VectorType::get(PointerType::get(Ctx, AMDGPUAS::BUFFER_RESOURCE), EC);
  Type *OffVec = VectorType::get(IntegerType::get(Ctx, BufferOffsetWidth), EC);
  return StructType::get(RsrcVec, OffVec);
}

static bool isBufferFatPtrOrVector(Type *Ty) {
  if (auto *PT = dyn_cast<PointerType>(Ty->getScalarType()))
    return PT->getAddressSpace() == AMDGPUAS::BUFFER_FAT_POINTER;
  return false;
}

// True if the type is {ptr addrspace(8), i32} or a struct containing vectors of
// those types. Used to quickly skip instructions we don't need to process.
static bool isSplitFatPtr(Type *Ty) {
  auto *ST = dyn_cast<StructType>(Ty);
  if (!ST)
    return false;
  if (!ST->isLiteral() || ST->getNumElements() != 2)
    return false;
  auto *MaybeRsrc =
      dyn_cast<PointerType>(ST->getElementType(0)->getScalarType());
  auto *MaybeOff =
      dyn_cast<IntegerType>(ST->getElementType(1)->getScalarType());
  return MaybeRsrc && MaybeOff &&
         MaybeRsrc->getAddressSpace() == AMDGPUAS::BUFFER_RESOURCE &&
         MaybeOff->getBitWidth() == BufferOffsetWidth;
}

// True if the result type or any argument types are buffer fat pointers.
static bool isBufferFatPtrConst(Constant *C) {
  Type *T = C->getType();
  return isBufferFatPtrOrVector(T) || any_of(C->operands(), [](const Use &U) {
           return isBufferFatPtrOrVector(U.get()->getType());
         });
}

namespace {
/// Convert [vectors of] buffer fat pointers to integers when they are read from
/// or stored to memory. This ensures that these pointers will have the same
/// memory layout as before they are lowered, even though they will no longer
/// have their previous layout in registers/in the program (they'll be broken
/// down into resource and offset parts). This has the downside of imposing
/// marshalling costs when reading or storing these values, but since placing
/// such pointers into memory is an uncommon operation at best, we feel that
/// this cost is acceptable for better performance in the common case.
class StoreFatPtrsAsIntsVisitor
    : public InstVisitor<StoreFatPtrsAsIntsVisitor, bool> {
  BufferFatPtrToIntTypeMap *TypeMap;

  ValueToValueMapTy ConvertedForStore;

  IRBuilder<> IRB;

  // Convert all the buffer fat pointers within the input value to inttegers
  // so that it can be stored in memory.
  Value *fatPtrsToInts(Value *V, Type *From, Type *To, const Twine &Name);
  // Convert all the i160s that need to be buffer fat pointers (as specified)
  // by the To type) into those pointers to preserve the semantics of the rest
  // of the program.
  Value *intsToFatPtrs(Value *V, Type *From, Type *To, const Twine &Name);

public:
  StoreFatPtrsAsIntsVisitor(BufferFatPtrToIntTypeMap *TypeMap, LLVMContext &Ctx)
      : TypeMap(TypeMap), IRB(Ctx) {}
  bool processFunction(Function &F);

  bool visitInstruction(Instruction &I) { return false; }
  bool visitAllocaInst(AllocaInst &I);
  bool visitLoadInst(LoadInst &LI);
  bool visitStoreInst(StoreInst &SI);
  bool visitGetElementPtrInst(GetElementPtrInst &I);
};
} // namespace

Value *StoreFatPtrsAsIntsVisitor::fatPtrsToInts(Value *V, Type *From, Type *To,
                                                const Twine &Name) {
  if (From == To)
    return V;
  ValueToValueMapTy::iterator Find = ConvertedForStore.find(V);
  if (Find != ConvertedForStore.end())
    return Find->second;
  if (isBufferFatPtrOrVector(From)) {
    Value *Cast = IRB.CreatePtrToInt(V, To, Name + ".int");
    ConvertedForStore[V] = Cast;
    return Cast;
  }
  if (From->getNumContainedTypes() == 0)
    return V;
  // Structs, arrays, and other compound types.
  Value *Ret = PoisonValue::get(To);
  if (auto *AT = dyn_cast<ArrayType>(From)) {
    Type *FromPart = AT->getArrayElementType();
    Type *ToPart = cast<ArrayType>(To)->getElementType();
    for (uint64_t I = 0, E = AT->getArrayNumElements(); I < E; ++I) {
      Value *Field = IRB.CreateExtractValue(V, I);
      Value *NewField =
          fatPtrsToInts(Field, FromPart, ToPart, Name + "." + Twine(I));
      Ret = IRB.CreateInsertValue(Ret, NewField, I);
    }
  } else {
    for (auto [Idx, FromPart, ToPart] :
         enumerate(From->subtypes(), To->subtypes())) {
      Value *Field = IRB.CreateExtractValue(V, Idx);
      Value *NewField =
          fatPtrsToInts(Field, FromPart, ToPart, Name + "." + Twine(Idx));
      Ret = IRB.CreateInsertValue(Ret, NewField, Idx);
    }
  }
  ConvertedForStore[V] = Ret;
  return Ret;
}

Value *StoreFatPtrsAsIntsVisitor::intsToFatPtrs(Value *V, Type *From, Type *To,
                                                const Twine &Name) {
  if (From == To)
    return V;
  if (isBufferFatPtrOrVector(To)) {
    Value *Cast = IRB.CreateIntToPtr(V, To, Name + ".ptr");
    return Cast;
  }
  if (From->getNumContainedTypes() == 0)
    return V;
  // Structs, arrays, and other compound types.
  Value *Ret = PoisonValue::get(To);
  if (auto *AT = dyn_cast<ArrayType>(From)) {
    Type *FromPart = AT->getArrayElementType();
    Type *ToPart = cast<ArrayType>(To)->getElementType();
    for (uint64_t I = 0, E = AT->getArrayNumElements(); I < E; ++I) {
      Value *Field = IRB.CreateExtractValue(V, I);
      Value *NewField =
          intsToFatPtrs(Field, FromPart, ToPart, Name + "." + Twine(I));
      Ret = IRB.CreateInsertValue(Ret, NewField, I);
    }
  } else {
    for (auto [Idx, FromPart, ToPart] :
         enumerate(From->subtypes(), To->subtypes())) {
      Value *Field = IRB.CreateExtractValue(V, Idx);
      Value *NewField =
          intsToFatPtrs(Field, FromPart, ToPart, Name + "." + Twine(Idx));
      Ret = IRB.CreateInsertValue(Ret, NewField, Idx);
    }
  }
  return Ret;
}

bool StoreFatPtrsAsIntsVisitor::processFunction(Function &F) {
  bool Changed = false;
  // The visitors will mutate GEPs and allocas, but will push loads and stores
  // to the worklist to avoid invalidation.
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    Changed |= visit(I);
  }
  ConvertedForStore.clear();
  return Changed;
}

bool StoreFatPtrsAsIntsVisitor::visitAllocaInst(AllocaInst &I) {
  Type *Ty = I.getAllocatedType();
  Type *NewTy = TypeMap->remapType(Ty);
  if (Ty == NewTy)
    return false;
  I.setAllocatedType(NewTy);
  return true;
}

bool StoreFatPtrsAsIntsVisitor::visitGetElementPtrInst(GetElementPtrInst &I) {
  Type *Ty = I.getSourceElementType();
  Type *NewTy = TypeMap->remapType(Ty);
  if (Ty == NewTy)
    return false;
  // We'll be rewriting the type `ptr addrspace(7)` out of existence soon, so
  // make sure GEPs don't have different semantics with the new type.
  I.setSourceElementType(NewTy);
  I.setResultElementType(TypeMap->remapType(I.getResultElementType()));
  return true;
}

bool StoreFatPtrsAsIntsVisitor::visitLoadInst(LoadInst &LI) {
  Type *Ty = LI.getType();
  Type *IntTy = TypeMap->remapType(Ty);
  if (Ty == IntTy)
    return false;

  IRB.SetInsertPoint(&LI);
  auto *NLI = cast<LoadInst>(LI.clone());
  NLI->mutateType(IntTy);
  NLI = IRB.Insert(NLI);
  copyMetadataForLoad(*NLI, LI);
  NLI->takeName(&LI);

  Value *CastBack = intsToFatPtrs(NLI, IntTy, Ty, NLI->getName());
  LI.replaceAllUsesWith(CastBack);
  LI.eraseFromParent();
  return true;
}

bool StoreFatPtrsAsIntsVisitor::visitStoreInst(StoreInst &SI) {
  Value *V = SI.getValueOperand();
  Type *Ty = V->getType();
  Type *IntTy = TypeMap->remapType(Ty);
  if (Ty == IntTy)
    return false;

  IRB.SetInsertPoint(&SI);
  Value *IntV = fatPtrsToInts(V, Ty, IntTy, V->getName());
  for (auto *Dbg : at::getAssignmentMarkers(&SI))
    Dbg->setValue(IntV);

  SI.setOperand(0, IntV);
  return true;
}

/// Return the ptr addrspace(8) and i32 (resource and offset parts) in a lowered
/// buffer fat pointer constant.
static std::pair<Constant *, Constant *>
splitLoweredFatBufferConst(Constant *C) {
  assert(isSplitFatPtr(C->getType()) && "Not a split fat buffer pointer");
  return std::make_pair(C->getAggregateElement(0u), C->getAggregateElement(1u));
}

namespace {
/// Handle the remapping of ptr addrspace(7) constants.
class FatPtrConstMaterializer final : public ValueMaterializer {
  BufferFatPtrToStructTypeMap *TypeMap;
  // An internal mapper that is used to recurse into the arguments of constants.
  // While the documentation for `ValueMapper` specifies not to use it
  // recursively, examination of the logic in mapValue() shows that it can
  // safely be used recursively when handling constants, like it does in its own
  // logic.
  ValueMapper InternalMapper;

  Constant *materializeBufferFatPtrConst(Constant *C);

public:
  // UnderlyingMap is the value map this materializer will be filling.
  FatPtrConstMaterializer(BufferFatPtrToStructTypeMap *TypeMap,
                          ValueToValueMapTy &UnderlyingMap)
      : TypeMap(TypeMap),
        InternalMapper(UnderlyingMap, RF_None, TypeMap, this) {}
  virtual ~FatPtrConstMaterializer() = default;

  Value *materialize(Value *V) override;
};
} // namespace

Constant *FatPtrConstMaterializer::materializeBufferFatPtrConst(Constant *C) {
  Type *SrcTy = C->getType();
  auto *NewTy = dyn_cast<StructType>(TypeMap->remapType(SrcTy));
  if (C->isNullValue())
    return ConstantAggregateZero::getNullValue(NewTy);
  if (isa<PoisonValue>(C)) {
    return ConstantStruct::get(NewTy,
                               {PoisonValue::get(NewTy->getElementType(0)),
                                PoisonValue::get(NewTy->getElementType(1))});
  }
  if (isa<UndefValue>(C)) {
    return ConstantStruct::get(NewTy,
                               {UndefValue::get(NewTy->getElementType(0)),
                                UndefValue::get(NewTy->getElementType(1))});
  }

  if (auto *VC = dyn_cast<ConstantVector>(C)) {
    if (Constant *S = VC->getSplatValue()) {
      Constant *NewS = InternalMapper.mapConstant(*S);
      if (!NewS)
        return nullptr;
      auto [Rsrc, Off] = splitLoweredFatBufferConst(NewS);
      auto EC = VC->getType()->getElementCount();
      return ConstantStruct::get(NewTy, {ConstantVector::getSplat(EC, Rsrc),
                                         ConstantVector::getSplat(EC, Off)});
    }
    SmallVector<Constant *> Rsrcs;
    SmallVector<Constant *> Offs;
    for (Value *Op : VC->operand_values()) {
      auto *NewOp = dyn_cast_or_null<Constant>(InternalMapper.mapValue(*Op));
      if (!NewOp)
        return nullptr;
      auto [Rsrc, Off] = splitLoweredFatBufferConst(NewOp);
      Rsrcs.push_back(Rsrc);
      Offs.push_back(Off);
    }
    Constant *RsrcVec = ConstantVector::get(Rsrcs);
    Constant *OffVec = ConstantVector::get(Offs);
    return ConstantStruct::get(NewTy, {RsrcVec, OffVec});
  }

  if (isa<GlobalValue>(C))
    report_fatal_error("Global values containing ptr addrspace(7) (buffer "
                       "fat pointer) values are not supported");

  if (isa<ConstantExpr>(C))
    report_fatal_error("Constant exprs containing ptr addrspace(7) (buffer "
                       "fat pointer) values should have been expanded earlier");

  return nullptr;
}

Value *FatPtrConstMaterializer::materialize(Value *V) {
  Constant *C = dyn_cast<Constant>(V);
  if (!C)
    return nullptr;
  // Structs and other types that happen to contain fat pointers get remapped
  // by the mapValue() logic.
  if (!isBufferFatPtrConst(C))
    return nullptr;
  return materializeBufferFatPtrConst(C);
}

using PtrParts = std::pair<Value *, Value *>;
namespace {
// The visitor returns the resource and offset parts for an instruction if they
// can be computed, or (nullptr, nullptr) for cases that don't have a meaningful
// value mapping.
class SplitPtrStructs : public InstVisitor<SplitPtrStructs, PtrParts> {
  ValueToValueMapTy RsrcParts;
  ValueToValueMapTy OffParts;

  // Track instructions that have been rewritten into a user of the component
  // parts of their ptr addrspace(7) input. Instructions that produced
  // ptr addrspace(7) parts should **not** be RAUW'd before being added to this
  // set, as that replacement will be handled in a post-visit step. However,
  // instructions that yield values that aren't fat pointers (ex. ptrtoint)
  // should RAUW themselves with new instructions that use the split parts
  // of their arguments during processing.
  DenseSet<Instruction *> SplitUsers;

  // Nodes that need a second look once we've computed the parts for all other
  // instructions to see if, for example, we really need to phi on the resource
  // part.
  SmallVector<Instruction *> Conditionals;
  // Temporary instructions produced while lowering conditionals that should be
  // killed.
  SmallVector<Instruction *> ConditionalTemps;

  // Subtarget info, needed for determining what cache control bits to set.
  const TargetMachine *TM;
  const GCNSubtarget *ST = nullptr;

  IRBuilder<> IRB;

  // Copy metadata between instructions if applicable.
  void copyMetadata(Value *Dest, Value *Src);

  // Get the resource and offset parts of the value V, inserting appropriate
  // extractvalue calls if needed.
  PtrParts getPtrParts(Value *V);

  // Given an instruction that could produce multiple resource parts (a PHI or
  // select), collect the set of possible instructions that could have provided
  // its resource parts  that it could have (the `Roots`) and the set of
  // conditional instructions visited during the search (`Seen`). If, after
  // removing the root of the search from `Seen` and `Roots`, `Seen` is a subset
  // of `Roots` and `Roots - Seen` contains one element, the resource part of
  // that element can replace the resource part of all other elements in `Seen`.
  void getPossibleRsrcRoots(Instruction *I, SmallPtrSetImpl<Value *> &Roots,
                            SmallPtrSetImpl<Value *> &Seen);
  void processConditionals();

  // If an instruction hav been split into resource and offset parts,
  // delete that instruction. If any of its uses have not themselves been split
  // into parts (for example, an insertvalue), construct the structure
  // that the type rewrites declared should be produced by the dying instruction
  // and use that.
  // Also, kill the temporary extractvalue operations produced by the two-stage
  // lowering of PHIs and conditionals.
  void killAndReplaceSplitInstructions(SmallVectorImpl<Instruction *> &Origs);

  void setAlign(CallInst *Intr, Align A, unsigned RsrcArgIdx);
  void insertPreMemOpFence(AtomicOrdering Order, SyncScope::ID SSID);
  void insertPostMemOpFence(AtomicOrdering Order, SyncScope::ID SSID);
  Value *handleMemoryInst(Instruction *I, Value *Arg, Value *Ptr, Type *Ty,
                          Align Alignment, AtomicOrdering Order,
                          bool IsVolatile, SyncScope::ID SSID);

public:
  SplitPtrStructs(LLVMContext &Ctx, const TargetMachine *TM)
      : TM(TM), IRB(Ctx) {}

  void processFunction(Function &F);

  PtrParts visitInstruction(Instruction &I);
  PtrParts visitLoadInst(LoadInst &LI);
  PtrParts visitStoreInst(StoreInst &SI);
  PtrParts visitAtomicRMWInst(AtomicRMWInst &AI);
  PtrParts visitAtomicCmpXchgInst(AtomicCmpXchgInst &AI);
  PtrParts visitGetElementPtrInst(GetElementPtrInst &GEP);

  PtrParts visitPtrToIntInst(PtrToIntInst &PI);
  PtrParts visitIntToPtrInst(IntToPtrInst &IP);
  PtrParts visitAddrSpaceCastInst(AddrSpaceCastInst &I);
  PtrParts visitICmpInst(ICmpInst &Cmp);
  PtrParts visitFreezeInst(FreezeInst &I);

  PtrParts visitExtractElementInst(ExtractElementInst &I);
  PtrParts visitInsertElementInst(InsertElementInst &I);
  PtrParts visitShuffleVectorInst(ShuffleVectorInst &I);

  PtrParts visitPHINode(PHINode &PHI);
  PtrParts visitSelectInst(SelectInst &SI);

  PtrParts visitIntrinsicInst(IntrinsicInst &II);
};
} // namespace

void SplitPtrStructs::copyMetadata(Value *Dest, Value *Src) {
  auto *DestI = dyn_cast<Instruction>(Dest);
  auto *SrcI = dyn_cast<Instruction>(Src);

  if (!DestI || !SrcI)
    return;

  DestI->copyMetadata(*SrcI);
}

PtrParts SplitPtrStructs::getPtrParts(Value *V) {
  assert(isSplitFatPtr(V->getType()) && "it's not meaningful to get the parts "
                                        "of something that wasn't rewritten");
  auto *RsrcEntry = &RsrcParts[V];
  auto *OffEntry = &OffParts[V];
  if (*RsrcEntry && *OffEntry)
    return {*RsrcEntry, *OffEntry};

  if (auto *C = dyn_cast<Constant>(V)) {
    auto [Rsrc, Off] = splitLoweredFatBufferConst(C);
    return {*RsrcEntry = Rsrc, *OffEntry = Off};
  }

  IRBuilder<>::InsertPointGuard Guard(IRB);
  if (auto *I = dyn_cast<Instruction>(V)) {
    LLVM_DEBUG(dbgs() << "Recursing to split parts of " << *I << "\n");
    auto [Rsrc, Off] = visit(*I);
    if (Rsrc && Off)
      return {*RsrcEntry = Rsrc, *OffEntry = Off};
    // We'll be creating the new values after the relevant instruction.
    // This instruction generates a value and so isn't a terminator.
    IRB.SetInsertPoint(*I->getInsertionPointAfterDef());
    IRB.SetCurrentDebugLocation(I->getDebugLoc());
  } else if (auto *A = dyn_cast<Argument>(V)) {
    IRB.SetInsertPointPastAllocas(A->getParent());
    IRB.SetCurrentDebugLocation(DebugLoc());
  }
  Value *Rsrc = IRB.CreateExtractValue(V, 0, V->getName() + ".rsrc");
  Value *Off = IRB.CreateExtractValue(V, 1, V->getName() + ".off");
  return {*RsrcEntry = Rsrc, *OffEntry = Off};
}

/// Returns the instruction that defines the resource part of the value V.
/// Note that this is not getUnderlyingObject(), since that looks through
/// operations like ptrmask which might modify the resource part.
///
/// We can limit ourselves to just looking through GEPs followed by looking
/// through addrspacecasts because only those two operations preserve the
/// resource part, and because operations on an `addrspace(8)` (which is the
/// legal input to this addrspacecast) would produce a different resource part.
static Value *rsrcPartRoot(Value *V) {
  while (auto *GEP = dyn_cast<GEPOperator>(V))
    V = GEP->getPointerOperand();
  while (auto *ASC = dyn_cast<AddrSpaceCastOperator>(V))
    V = ASC->getPointerOperand();
  return V;
}

void SplitPtrStructs::getPossibleRsrcRoots(Instruction *I,
                                           SmallPtrSetImpl<Value *> &Roots,
                                           SmallPtrSetImpl<Value *> &Seen) {
  if (auto *PHI = dyn_cast<PHINode>(I)) {
    if (!Seen.insert(I).second)
      return;
    for (Value *In : PHI->incoming_values()) {
      In = rsrcPartRoot(In);
      Roots.insert(In);
      if (isa<PHINode, SelectInst>(In))
        getPossibleRsrcRoots(cast<Instruction>(In), Roots, Seen);
    }
  } else if (auto *SI = dyn_cast<SelectInst>(I)) {
    if (!Seen.insert(SI).second)
      return;
    Value *TrueVal = rsrcPartRoot(SI->getTrueValue());
    Value *FalseVal = rsrcPartRoot(SI->getFalseValue());
    Roots.insert(TrueVal);
    Roots.insert(FalseVal);
    if (isa<PHINode, SelectInst>(TrueVal))
      getPossibleRsrcRoots(cast<Instruction>(TrueVal), Roots, Seen);
    if (isa<PHINode, SelectInst>(FalseVal))
      getPossibleRsrcRoots(cast<Instruction>(FalseVal), Roots, Seen);
  } else {
    llvm_unreachable("getPossibleRsrcParts() only works on phi and select");
  }
}

void SplitPtrStructs::processConditionals() {
  SmallDenseMap<Instruction *, Value *> FoundRsrcs;
  SmallPtrSet<Value *, 4> Roots;
  SmallPtrSet<Value *, 4> Seen;
  for (Instruction *I : Conditionals) {
    // These have to exist by now because we've visited these nodes.
    Value *Rsrc = RsrcParts[I];
    Value *Off = OffParts[I];
    assert(Rsrc && Off && "must have visited conditionals by now");

    std::optional<Value *> MaybeRsrc;
    auto MaybeFoundRsrc = FoundRsrcs.find(I);
    if (MaybeFoundRsrc != FoundRsrcs.end()) {
      MaybeRsrc = MaybeFoundRsrc->second;
    } else {
      IRBuilder<>::InsertPointGuard Guard(IRB);
      Roots.clear();
      Seen.clear();
      getPossibleRsrcRoots(I, Roots, Seen);
      LLVM_DEBUG(dbgs() << "Processing conditional: " << *I << "\n");
#ifndef NDEBUG
      for (Value *V : Roots)
        LLVM_DEBUG(dbgs() << "Root: " << *V << "\n");
      for (Value *V : Seen)
        LLVM_DEBUG(dbgs() << "Seen: " << *V << "\n");
#endif
      // If we are our own possible root, then we shouldn't block our
      // replacement with a valid incoming value.
      Roots.erase(I);
      // We don't want to block the optimization for conditionals that don't
      // refer to themselves but did see themselves during the traversal.
      Seen.erase(I);

      if (set_is_subset(Seen, Roots)) {
        auto Diff = set_difference(Roots, Seen);
        if (Diff.size() == 1) {
          Value *RootVal = *Diff.begin();
          // Handle the case where previous loops already looked through
          // an addrspacecast.
          if (isSplitFatPtr(RootVal->getType()))
            MaybeRsrc = std::get<0>(getPtrParts(RootVal));
          else
            MaybeRsrc = RootVal;
        }
      }
    }

    if (auto *PHI = dyn_cast<PHINode>(I)) {
      Value *NewRsrc;
      StructType *PHITy = cast<StructType>(PHI->getType());
      IRB.SetInsertPoint(*PHI->getInsertionPointAfterDef());
      IRB.SetCurrentDebugLocation(PHI->getDebugLoc());
      if (MaybeRsrc) {
        NewRsrc = *MaybeRsrc;
      } else {
        Type *RsrcTy = PHITy->getElementType(0);
        auto *RsrcPHI = IRB.CreatePHI(RsrcTy, PHI->getNumIncomingValues());
        RsrcPHI->takeName(Rsrc);
        for (auto [V, BB] : llvm::zip(PHI->incoming_values(), PHI->blocks())) {
          Value *VRsrc = std::get<0>(getPtrParts(V));
          RsrcPHI->addIncoming(VRsrc, BB);
        }
        copyMetadata(RsrcPHI, PHI);
        NewRsrc = RsrcPHI;
      }

      Type *OffTy = PHITy->getElementType(1);
      auto *NewOff = IRB.CreatePHI(OffTy, PHI->getNumIncomingValues());
      NewOff->takeName(Off);
      for (auto [V, BB] : llvm::zip(PHI->incoming_values(), PHI->blocks())) {
        assert(OffParts.count(V) && "An offset part had to be created by now");
        Value *VOff = std::get<1>(getPtrParts(V));
        NewOff->addIncoming(VOff, BB);
      }
      copyMetadata(NewOff, PHI);

      // Note: We don't eraseFromParent() the temporaries because we don't want
      // to put the corrections maps in an inconstent state. That'll be handed
      // during the rest of the killing. Also, `ValueToValueMapTy` guarantees
      // that references in that map will be updated as well.
      ConditionalTemps.push_back(cast<Instruction>(Rsrc));
      ConditionalTemps.push_back(cast<Instruction>(Off));
      Rsrc->replaceAllUsesWith(NewRsrc);
      Off->replaceAllUsesWith(NewOff);

      // Save on recomputing the cycle traversals in known-root cases.
      if (MaybeRsrc)
        for (Value *V : Seen)
          FoundRsrcs[cast<Instruction>(V)] = NewRsrc;
    } else if (isa<SelectInst>(I)) {
      if (MaybeRsrc) {
        ConditionalTemps.push_back(cast<Instruction>(Rsrc));
        Rsrc->replaceAllUsesWith(*MaybeRsrc);
        for (Value *V : Seen)
          FoundRsrcs[cast<Instruction>(V)] = *MaybeRsrc;
      }
    } else {
      llvm_unreachable("Only PHIs and selects go in the conditionals list");
    }
  }
}

void SplitPtrStructs::killAndReplaceSplitInstructions(
    SmallVectorImpl<Instruction *> &Origs) {
  for (Instruction *I : ConditionalTemps)
    I->eraseFromParent();

  for (Instruction *I : Origs) {
    if (!SplitUsers.contains(I))
      continue;

    SmallVector<DbgValueInst *> Dbgs;
    findDbgValues(Dbgs, I);
    for (auto *Dbg : Dbgs) {
      IRB.SetInsertPoint(Dbg);
      auto &DL = I->getDataLayout();
      assert(isSplitFatPtr(I->getType()) &&
             "We should've RAUW'd away loads, stores, etc. at this point");
      auto *OffDbg = cast<DbgValueInst>(Dbg->clone());
      copyMetadata(OffDbg, Dbg);
      auto [Rsrc, Off] = getPtrParts(I);

      int64_t RsrcSz = DL.getTypeSizeInBits(Rsrc->getType());
      int64_t OffSz = DL.getTypeSizeInBits(Off->getType());

      std::optional<DIExpression *> RsrcExpr =
          DIExpression::createFragmentExpression(Dbg->getExpression(), 0,
                                                 RsrcSz);
      std::optional<DIExpression *> OffExpr =
          DIExpression::createFragmentExpression(Dbg->getExpression(), RsrcSz,
                                                 OffSz);
      if (OffExpr) {
        OffDbg->setExpression(*OffExpr);
        OffDbg->replaceVariableLocationOp(I, Off);
        IRB.Insert(OffDbg);
      } else {
        OffDbg->deleteValue();
      }
      if (RsrcExpr) {
        Dbg->setExpression(*RsrcExpr);
        Dbg->replaceVariableLocationOp(I, Rsrc);
      } else {
        Dbg->replaceVariableLocationOp(I, UndefValue::get(I->getType()));
      }
    }

    Value *Poison = PoisonValue::get(I->getType());
    I->replaceUsesWithIf(Poison, [&](const Use &U) -> bool {
      if (const auto *UI = dyn_cast<Instruction>(U.getUser()))
        return SplitUsers.contains(UI);
      return false;
    });

    if (I->use_empty()) {
      I->eraseFromParent();
      continue;
    }
    IRB.SetInsertPoint(*I->getInsertionPointAfterDef());
    IRB.SetCurrentDebugLocation(I->getDebugLoc());
    auto [Rsrc, Off] = getPtrParts(I);
    Value *Struct = PoisonValue::get(I->getType());
    Struct = IRB.CreateInsertValue(Struct, Rsrc, 0);
    Struct = IRB.CreateInsertValue(Struct, Off, 1);
    copyMetadata(Struct, I);
    Struct->takeName(I);
    I->replaceAllUsesWith(Struct);
    I->eraseFromParent();
  }
}

void SplitPtrStructs::setAlign(CallInst *Intr, Align A, unsigned RsrcArgIdx) {
  LLVMContext &Ctx = Intr->getContext();
  Intr->addParamAttr(RsrcArgIdx, Attribute::getWithAlignment(Ctx, A));
}

void SplitPtrStructs::insertPreMemOpFence(AtomicOrdering Order,
                                          SyncScope::ID SSID) {
  switch (Order) {
  case AtomicOrdering::Release:
  case AtomicOrdering::AcquireRelease:
  case AtomicOrdering::SequentiallyConsistent:
    IRB.CreateFence(AtomicOrdering::Release, SSID);
    break;
  default:
    break;
  }
}

void SplitPtrStructs::insertPostMemOpFence(AtomicOrdering Order,
                                           SyncScope::ID SSID) {
  switch (Order) {
  case AtomicOrdering::Acquire:
  case AtomicOrdering::AcquireRelease:
  case AtomicOrdering::SequentiallyConsistent:
    IRB.CreateFence(AtomicOrdering::Acquire, SSID);
    break;
  default:
    break;
  }
}

Value *SplitPtrStructs::handleMemoryInst(Instruction *I, Value *Arg, Value *Ptr,
                                         Type *Ty, Align Alignment,
                                         AtomicOrdering Order, bool IsVolatile,
                                         SyncScope::ID SSID) {
  IRB.SetInsertPoint(I);

  auto [Rsrc, Off] = getPtrParts(Ptr);
  SmallVector<Value *, 5> Args;
  if (Arg)
    Args.push_back(Arg);
  Args.push_back(Rsrc);
  Args.push_back(Off);
  insertPreMemOpFence(Order, SSID);
  // soffset is always 0 for these cases, where we always want any offset to be
  // part of bounds checking and we don't know which parts of the GEPs is
  // uniform.
  Args.push_back(IRB.getInt32(0));

  uint32_t Aux = 0;
  bool IsInvariant =
      (isa<LoadInst>(I) && I->getMetadata(LLVMContext::MD_invariant_load));
  bool IsNonTemporal = I->getMetadata(LLVMContext::MD_nontemporal);
  // Atomic loads and stores need glc, atomic read-modify-write doesn't.
  bool IsOneWayAtomic =
      !isa<AtomicRMWInst>(I) && Order != AtomicOrdering::NotAtomic;
  if (IsOneWayAtomic)
    Aux |= AMDGPU::CPol::GLC;
  if (IsNonTemporal && !IsInvariant)
    Aux |= AMDGPU::CPol::SLC;
  if (isa<LoadInst>(I) && ST->getGeneration() == AMDGPUSubtarget::GFX10)
    Aux |= (Aux & AMDGPU::CPol::GLC ? AMDGPU::CPol::DLC : 0);
  if (IsVolatile)
    Aux |= AMDGPU::CPol::VOLATILE;
  Args.push_back(IRB.getInt32(Aux));

  Intrinsic::ID IID = Intrinsic::not_intrinsic;
  if (isa<LoadInst>(I))
    IID = Order == AtomicOrdering::NotAtomic
              ? Intrinsic::amdgcn_raw_ptr_buffer_load
              : Intrinsic::amdgcn_raw_ptr_atomic_buffer_load;
  else if (isa<StoreInst>(I))
    IID = Intrinsic::amdgcn_raw_ptr_buffer_store;
  else if (auto *RMW = dyn_cast<AtomicRMWInst>(I)) {
    switch (RMW->getOperation()) {
    case AtomicRMWInst::Xchg:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_swap;
      break;
    case AtomicRMWInst::Add:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_add;
      break;
    case AtomicRMWInst::Sub:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_sub;
      break;
    case AtomicRMWInst::And:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_and;
      break;
    case AtomicRMWInst::Or:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_or;
      break;
    case AtomicRMWInst::Xor:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_xor;
      break;
    case AtomicRMWInst::Max:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_smax;
      break;
    case AtomicRMWInst::Min:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_smin;
      break;
    case AtomicRMWInst::UMax:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_umax;
      break;
    case AtomicRMWInst::UMin:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_umin;
      break;
    case AtomicRMWInst::FAdd:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_fadd;
      break;
    case AtomicRMWInst::FMax:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_fmax;
      break;
    case AtomicRMWInst::FMin:
      IID = Intrinsic::amdgcn_raw_ptr_buffer_atomic_fmin;
      break;
    case AtomicRMWInst::FSub: {
      report_fatal_error("atomic floating point subtraction not supported for "
                         "buffer resources and should've been expanded away");
      break;
    }
    case AtomicRMWInst::Nand:
      report_fatal_error("atomic nand not supported for buffer resources and "
                         "should've been expanded away");
      break;
    case AtomicRMWInst::UIncWrap:
    case AtomicRMWInst::UDecWrap:
      report_fatal_error("wrapping increment/decrement not supported for "
                         "buffer resources and should've ben expanded away");
      break;
    case AtomicRMWInst::BAD_BINOP:
      llvm_unreachable("Not sure how we got a bad binop");
    }
  }

  auto *Call = IRB.CreateIntrinsic(IID, Ty, Args);
  copyMetadata(Call, I);
  setAlign(Call, Alignment, Arg ? 1 : 0);
  Call->takeName(I);

  insertPostMemOpFence(Order, SSID);
  // The "no moving p7 directly" rewrites ensure that this load or store won't
  // itself need to be split into parts.
  SplitUsers.insert(I);
  I->replaceAllUsesWith(Call);
  return Call;
}

PtrParts SplitPtrStructs::visitInstruction(Instruction &I) {
  return {nullptr, nullptr};
}

PtrParts SplitPtrStructs::visitLoadInst(LoadInst &LI) {
  if (!isSplitFatPtr(LI.getPointerOperandType()))
    return {nullptr, nullptr};
  handleMemoryInst(&LI, nullptr, LI.getPointerOperand(), LI.getType(),
                   LI.getAlign(), LI.getOrdering(), LI.isVolatile(),
                   LI.getSyncScopeID());
  return {nullptr, nullptr};
}

PtrParts SplitPtrStructs::visitStoreInst(StoreInst &SI) {
  if (!isSplitFatPtr(SI.getPointerOperandType()))
    return {nullptr, nullptr};
  Value *Arg = SI.getValueOperand();
  handleMemoryInst(&SI, Arg, SI.getPointerOperand(), Arg->getType(),
                   SI.getAlign(), SI.getOrdering(), SI.isVolatile(),
                   SI.getSyncScopeID());
  return {nullptr, nullptr};
}

PtrParts SplitPtrStructs::visitAtomicRMWInst(AtomicRMWInst &AI) {
  if (!isSplitFatPtr(AI.getPointerOperand()->getType()))
    return {nullptr, nullptr};
  Value *Arg = AI.getValOperand();
  handleMemoryInst(&AI, Arg, AI.getPointerOperand(), Arg->getType(),
                   AI.getAlign(), AI.getOrdering(), AI.isVolatile(),
                   AI.getSyncScopeID());
  return {nullptr, nullptr};
}

// Unlike load, store, and RMW, cmpxchg needs special handling to account
// for the boolean argument.
PtrParts SplitPtrStructs::visitAtomicCmpXchgInst(AtomicCmpXchgInst &AI) {
  Value *Ptr = AI.getPointerOperand();
  if (!isSplitFatPtr(Ptr->getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&AI);

  Type *Ty = AI.getNewValOperand()->getType();
  AtomicOrdering Order = AI.getMergedOrdering();
  SyncScope::ID SSID = AI.getSyncScopeID();
  bool IsNonTemporal = AI.getMetadata(LLVMContext::MD_nontemporal);

  auto [Rsrc, Off] = getPtrParts(Ptr);
  insertPreMemOpFence(Order, SSID);

  uint32_t Aux = 0;
  if (IsNonTemporal)
    Aux |= AMDGPU::CPol::SLC;
  if (AI.isVolatile())
    Aux |= AMDGPU::CPol::VOLATILE;
  auto *Call =
      IRB.CreateIntrinsic(Intrinsic::amdgcn_raw_ptr_buffer_atomic_cmpswap, Ty,
                          {AI.getNewValOperand(), AI.getCompareOperand(), Rsrc,
                           Off, IRB.getInt32(0), IRB.getInt32(Aux)});
  copyMetadata(Call, &AI);
  setAlign(Call, AI.getAlign(), 2);
  Call->takeName(&AI);
  insertPostMemOpFence(Order, SSID);

  Value *Res = PoisonValue::get(AI.getType());
  Res = IRB.CreateInsertValue(Res, Call, 0);
  if (!AI.isWeak()) {
    Value *Succeeded = IRB.CreateICmpEQ(Call, AI.getCompareOperand());
    Res = IRB.CreateInsertValue(Res, Succeeded, 1);
  }
  SplitUsers.insert(&AI);
  AI.replaceAllUsesWith(Res);
  return {nullptr, nullptr};
}

PtrParts SplitPtrStructs::visitGetElementPtrInst(GetElementPtrInst &GEP) {
  using namespace llvm::PatternMatch;
  Value *Ptr = GEP.getPointerOperand();
  if (!isSplitFatPtr(Ptr->getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&GEP);

  auto [Rsrc, Off] = getPtrParts(Ptr);
  const DataLayout &DL = GEP.getDataLayout();
  bool InBounds = GEP.isInBounds();

  // In order to call emitGEPOffset() and thus not have to reimplement it,
  // we need the GEP result to have ptr addrspace(7) type.
  Type *FatPtrTy = IRB.getPtrTy(AMDGPUAS::BUFFER_FAT_POINTER);
  if (auto *VT = dyn_cast<VectorType>(Off->getType()))
    FatPtrTy = VectorType::get(FatPtrTy, VT->getElementCount());
  GEP.mutateType(FatPtrTy);
  Value *OffAccum = emitGEPOffset(&IRB, DL, &GEP);
  GEP.mutateType(Ptr->getType());
  if (match(OffAccum, m_Zero())) { // Constant-zero offset
    SplitUsers.insert(&GEP);
    return {Rsrc, Off};
  }

  bool HasNonNegativeOff = false;
  if (auto *CI = dyn_cast<ConstantInt>(OffAccum)) {
    HasNonNegativeOff = !CI->isNegative();
  }
  Value *NewOff;
  if (match(Off, m_Zero())) {
    NewOff = OffAccum;
  } else {
    NewOff = IRB.CreateAdd(Off, OffAccum, "",
                           /*hasNUW=*/InBounds && HasNonNegativeOff,
                           /*hasNSW=*/false);
  }
  copyMetadata(NewOff, &GEP);
  NewOff->takeName(&GEP);
  SplitUsers.insert(&GEP);
  return {Rsrc, NewOff};
}

PtrParts SplitPtrStructs::visitPtrToIntInst(PtrToIntInst &PI) {
  Value *Ptr = PI.getPointerOperand();
  if (!isSplitFatPtr(Ptr->getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&PI);

  Type *ResTy = PI.getType();
  unsigned Width = ResTy->getScalarSizeInBits();

  auto [Rsrc, Off] = getPtrParts(Ptr);
  const DataLayout &DL = PI.getDataLayout();
  unsigned FatPtrWidth = DL.getPointerSizeInBits(AMDGPUAS::BUFFER_FAT_POINTER);

  Value *Res;
  if (Width <= BufferOffsetWidth) {
    Res = IRB.CreateIntCast(Off, ResTy, /*isSigned=*/false,
                            PI.getName() + ".off");
  } else {
    Value *RsrcInt = IRB.CreatePtrToInt(Rsrc, ResTy, PI.getName() + ".rsrc");
    Value *Shl = IRB.CreateShl(
        RsrcInt,
        ConstantExpr::getIntegerValue(ResTy, APInt(Width, BufferOffsetWidth)),
        "", Width >= FatPtrWidth, Width > FatPtrWidth);
    Value *OffCast = IRB.CreateIntCast(Off, ResTy, /*isSigned=*/false,
                                       PI.getName() + ".off");
    Res = IRB.CreateOr(Shl, OffCast);
  }

  copyMetadata(Res, &PI);
  Res->takeName(&PI);
  SplitUsers.insert(&PI);
  PI.replaceAllUsesWith(Res);
  return {nullptr, nullptr};
}

PtrParts SplitPtrStructs::visitIntToPtrInst(IntToPtrInst &IP) {
  if (!isSplitFatPtr(IP.getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&IP);
  const DataLayout &DL = IP.getDataLayout();
  unsigned RsrcPtrWidth = DL.getPointerSizeInBits(AMDGPUAS::BUFFER_RESOURCE);
  Value *Int = IP.getOperand(0);
  Type *IntTy = Int->getType();
  Type *RsrcIntTy = IntTy->getWithNewBitWidth(RsrcPtrWidth);
  unsigned Width = IntTy->getScalarSizeInBits();

  auto *RetTy = cast<StructType>(IP.getType());
  Type *RsrcTy = RetTy->getElementType(0);
  Type *OffTy = RetTy->getElementType(1);
  Value *RsrcPart = IRB.CreateLShr(
      Int,
      ConstantExpr::getIntegerValue(IntTy, APInt(Width, BufferOffsetWidth)));
  Value *RsrcInt = IRB.CreateIntCast(RsrcPart, RsrcIntTy, /*isSigned=*/false);
  Value *Rsrc = IRB.CreateIntToPtr(RsrcInt, RsrcTy, IP.getName() + ".rsrc");
  Value *Off =
      IRB.CreateIntCast(Int, OffTy, /*IsSigned=*/false, IP.getName() + ".off");

  copyMetadata(Rsrc, &IP);
  SplitUsers.insert(&IP);
  return {Rsrc, Off};
}

PtrParts SplitPtrStructs::visitAddrSpaceCastInst(AddrSpaceCastInst &I) {
  if (!isSplitFatPtr(I.getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&I);
  Value *In = I.getPointerOperand();
  // No-op casts preserve parts
  if (In->getType() == I.getType()) {
    auto [Rsrc, Off] = getPtrParts(In);
    SplitUsers.insert(&I);
    return {Rsrc, Off};
  }
  if (I.getSrcAddressSpace() != AMDGPUAS::BUFFER_RESOURCE)
    report_fatal_error("Only buffer resources (addrspace 8) can be cast to "
                       "buffer fat pointers (addrspace 7)");
  Type *OffTy = cast<StructType>(I.getType())->getElementType(1);
  Value *ZeroOff = Constant::getNullValue(OffTy);
  SplitUsers.insert(&I);
  return {In, ZeroOff};
}

PtrParts SplitPtrStructs::visitICmpInst(ICmpInst &Cmp) {
  Value *Lhs = Cmp.getOperand(0);
  if (!isSplitFatPtr(Lhs->getType()))
    return {nullptr, nullptr};
  Value *Rhs = Cmp.getOperand(1);
  IRB.SetInsertPoint(&Cmp);
  ICmpInst::Predicate Pred = Cmp.getPredicate();

  assert((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE) &&
         "Pointer comparison is only equal or unequal");
  auto [LhsRsrc, LhsOff] = getPtrParts(Lhs);
  auto [RhsRsrc, RhsOff] = getPtrParts(Rhs);
  Value *RsrcCmp =
      IRB.CreateICmp(Pred, LhsRsrc, RhsRsrc, Cmp.getName() + ".rsrc");
  copyMetadata(RsrcCmp, &Cmp);
  Value *OffCmp = IRB.CreateICmp(Pred, LhsOff, RhsOff, Cmp.getName() + ".off");
  copyMetadata(OffCmp, &Cmp);

  Value *Res = nullptr;
  if (Pred == ICmpInst::ICMP_EQ)
    Res = IRB.CreateAnd(RsrcCmp, OffCmp);
  else if (Pred == ICmpInst::ICMP_NE)
    Res = IRB.CreateOr(RsrcCmp, OffCmp);
  copyMetadata(Res, &Cmp);
  Res->takeName(&Cmp);
  SplitUsers.insert(&Cmp);
  Cmp.replaceAllUsesWith(Res);
  return {nullptr, nullptr};
}

PtrParts SplitPtrStructs::visitFreezeInst(FreezeInst &I) {
  if (!isSplitFatPtr(I.getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&I);
  auto [Rsrc, Off] = getPtrParts(I.getOperand(0));

  Value *RsrcRes = IRB.CreateFreeze(Rsrc, I.getName() + ".rsrc");
  copyMetadata(RsrcRes, &I);
  Value *OffRes = IRB.CreateFreeze(Off, I.getName() + ".off");
  copyMetadata(OffRes, &I);
  SplitUsers.insert(&I);
  return {RsrcRes, OffRes};
}

PtrParts SplitPtrStructs::visitExtractElementInst(ExtractElementInst &I) {
  if (!isSplitFatPtr(I.getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&I);
  Value *Vec = I.getVectorOperand();
  Value *Idx = I.getIndexOperand();
  auto [Rsrc, Off] = getPtrParts(Vec);

  Value *RsrcRes = IRB.CreateExtractElement(Rsrc, Idx, I.getName() + ".rsrc");
  copyMetadata(RsrcRes, &I);
  Value *OffRes = IRB.CreateExtractElement(Off, Idx, I.getName() + ".off");
  copyMetadata(OffRes, &I);
  SplitUsers.insert(&I);
  return {RsrcRes, OffRes};
}

PtrParts SplitPtrStructs::visitInsertElementInst(InsertElementInst &I) {
  // The mutated instructions temporarily don't return vectors, and so
  // we need the generic getType() here to avoid crashes.
  if (!isSplitFatPtr(cast<Instruction>(I).getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&I);
  Value *Vec = I.getOperand(0);
  Value *Elem = I.getOperand(1);
  Value *Idx = I.getOperand(2);
  auto [VecRsrc, VecOff] = getPtrParts(Vec);
  auto [ElemRsrc, ElemOff] = getPtrParts(Elem);

  Value *RsrcRes =
      IRB.CreateInsertElement(VecRsrc, ElemRsrc, Idx, I.getName() + ".rsrc");
  copyMetadata(RsrcRes, &I);
  Value *OffRes =
      IRB.CreateInsertElement(VecOff, ElemOff, Idx, I.getName() + ".off");
  copyMetadata(OffRes, &I);
  SplitUsers.insert(&I);
  return {RsrcRes, OffRes};
}

PtrParts SplitPtrStructs::visitShuffleVectorInst(ShuffleVectorInst &I) {
  // Cast is needed for the same reason as insertelement's.
  if (!isSplitFatPtr(cast<Instruction>(I).getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&I);

  Value *V1 = I.getOperand(0);
  Value *V2 = I.getOperand(1);
  ArrayRef<int> Mask = I.getShuffleMask();
  auto [V1Rsrc, V1Off] = getPtrParts(V1);
  auto [V2Rsrc, V2Off] = getPtrParts(V2);

  Value *RsrcRes =
      IRB.CreateShuffleVector(V1Rsrc, V2Rsrc, Mask, I.getName() + ".rsrc");
  copyMetadata(RsrcRes, &I);
  Value *OffRes =
      IRB.CreateShuffleVector(V1Off, V2Off, Mask, I.getName() + ".off");
  copyMetadata(OffRes, &I);
  SplitUsers.insert(&I);
  return {RsrcRes, OffRes};
}

PtrParts SplitPtrStructs::visitPHINode(PHINode &PHI) {
  if (!isSplitFatPtr(PHI.getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(*PHI.getInsertionPointAfterDef());
  // Phi nodes will be handled in post-processing after we've visited every
  // instruction. However, instead of just returning {nullptr, nullptr},
  // we explicitly create the temporary extractvalue operations that are our
  // temporary results so that they end up at the beginning of the block with
  // the PHIs.
  Value *TmpRsrc = IRB.CreateExtractValue(&PHI, 0, PHI.getName() + ".rsrc");
  Value *TmpOff = IRB.CreateExtractValue(&PHI, 1, PHI.getName() + ".off");
  Conditionals.push_back(&PHI);
  SplitUsers.insert(&PHI);
  return {TmpRsrc, TmpOff};
}

PtrParts SplitPtrStructs::visitSelectInst(SelectInst &SI) {
  if (!isSplitFatPtr(SI.getType()))
    return {nullptr, nullptr};
  IRB.SetInsertPoint(&SI);

  Value *Cond = SI.getCondition();
  Value *True = SI.getTrueValue();
  Value *False = SI.getFalseValue();
  auto [TrueRsrc, TrueOff] = getPtrParts(True);
  auto [FalseRsrc, FalseOff] = getPtrParts(False);

  Value *RsrcRes =
      IRB.CreateSelect(Cond, TrueRsrc, FalseRsrc, SI.getName() + ".rsrc", &SI);
  copyMetadata(RsrcRes, &SI);
  Conditionals.push_back(&SI);
  Value *OffRes =
      IRB.CreateSelect(Cond, TrueOff, FalseOff, SI.getName() + ".off", &SI);
  copyMetadata(OffRes, &SI);
  SplitUsers.insert(&SI);
  return {RsrcRes, OffRes};
}

/// Returns true if this intrinsic needs to be removed when it is
/// applied to `ptr addrspace(7)` values. Calls to these intrinsics are
/// rewritten into calls to versions of that intrinsic on the resource
/// descriptor.
static bool isRemovablePointerIntrinsic(Intrinsic::ID IID) {
  switch (IID) {
  default:
    return false;
  case Intrinsic::ptrmask:
  case Intrinsic::invariant_start:
  case Intrinsic::invariant_end:
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
    return true;
  }
}

PtrParts SplitPtrStructs::visitIntrinsicInst(IntrinsicInst &I) {
  Intrinsic::ID IID = I.getIntrinsicID();
  switch (IID) {
  default:
    break;
  case Intrinsic::ptrmask: {
    Value *Ptr = I.getArgOperand(0);
    if (!isSplitFatPtr(Ptr->getType()))
      return {nullptr, nullptr};
    Value *Mask = I.getArgOperand(1);
    IRB.SetInsertPoint(&I);
    auto [Rsrc, Off] = getPtrParts(Ptr);
    if (Mask->getType() != Off->getType())
      report_fatal_error("offset width is not equal to index width of fat "
                         "pointer (data layout not set up correctly?)");
    Value *OffRes = IRB.CreateAnd(Off, Mask, I.getName() + ".off");
    copyMetadata(OffRes, &I);
    SplitUsers.insert(&I);
    return {Rsrc, OffRes};
  }
  // Pointer annotation intrinsics that, given their object-wide nature
  // operate on the resource part.
  case Intrinsic::invariant_start: {
    Value *Ptr = I.getArgOperand(1);
    if (!isSplitFatPtr(Ptr->getType()))
      return {nullptr, nullptr};
    IRB.SetInsertPoint(&I);
    auto [Rsrc, Off] = getPtrParts(Ptr);
    Type *NewTy = PointerType::get(I.getContext(), AMDGPUAS::BUFFER_RESOURCE);
    auto *NewRsrc = IRB.CreateIntrinsic(IID, {NewTy}, {I.getOperand(0), Rsrc});
    copyMetadata(NewRsrc, &I);
    NewRsrc->takeName(&I);
    SplitUsers.insert(&I);
    I.replaceAllUsesWith(NewRsrc);
    return {nullptr, nullptr};
  }
  case Intrinsic::invariant_end: {
    Value *RealPtr = I.getArgOperand(2);
    if (!isSplitFatPtr(RealPtr->getType()))
      return {nullptr, nullptr};
    IRB.SetInsertPoint(&I);
    Value *RealRsrc = getPtrParts(RealPtr).first;
    Value *InvPtr = I.getArgOperand(0);
    Value *Size = I.getArgOperand(1);
    Value *NewRsrc = IRB.CreateIntrinsic(IID, {RealRsrc->getType()},
                                         {InvPtr, Size, RealRsrc});
    copyMetadata(NewRsrc, &I);
    NewRsrc->takeName(&I);
    SplitUsers.insert(&I);
    I.replaceAllUsesWith(NewRsrc);
    return {nullptr, nullptr};
  }
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group: {
    Value *Ptr = I.getArgOperand(0);
    if (!isSplitFatPtr(Ptr->getType()))
      return {nullptr, nullptr};
    IRB.SetInsertPoint(&I);
    auto [Rsrc, Off] = getPtrParts(Ptr);
    Value *NewRsrc = IRB.CreateIntrinsic(IID, {Rsrc->getType()}, {Rsrc});
    copyMetadata(NewRsrc, &I);
    NewRsrc->takeName(&I);
    SplitUsers.insert(&I);
    return {NewRsrc, Off};
  }
  }
  return {nullptr, nullptr};
}

void SplitPtrStructs::processFunction(Function &F) {
  ST = &TM->getSubtarget<GCNSubtarget>(F);
  SmallVector<Instruction *, 0> Originals;
  LLVM_DEBUG(dbgs() << "Splitting pointer structs in function: " << F.getName()
                    << "\n");
  for (Instruction &I : instructions(F))
    Originals.push_back(&I);
  for (Instruction *I : Originals) {
    auto [Rsrc, Off] = visit(I);
    assert(((Rsrc && Off) || (!Rsrc && !Off)) &&
           "Can't have a resource but no offset");
    if (Rsrc)
      RsrcParts[I] = Rsrc;
    if (Off)
      OffParts[I] = Off;
  }
  processConditionals();
  killAndReplaceSplitInstructions(Originals);

  // Clean up after ourselves to save on memory.
  RsrcParts.clear();
  OffParts.clear();
  SplitUsers.clear();
  Conditionals.clear();
  ConditionalTemps.clear();
}

namespace {
class AMDGPULowerBufferFatPointers : public ModulePass {
public:
  static char ID;

  AMDGPULowerBufferFatPointers() : ModulePass(ID) {
    initializeAMDGPULowerBufferFatPointersPass(
        *PassRegistry::getPassRegistry());
  }

  bool run(Module &M, const TargetMachine &TM);
  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};
} // namespace

/// Returns true if there are values that have a buffer fat pointer in them,
/// which means we'll need to perform rewrites on this function. As a side
/// effect, this will populate the type remapping cache.
static bool containsBufferFatPointers(const Function &F,
                                      BufferFatPtrToStructTypeMap *TypeMap) {
  bool HasFatPointers = false;
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      HasFatPointers |= (I.getType() != TypeMap->remapType(I.getType()));
  return HasFatPointers;
}

static bool hasFatPointerInterface(const Function &F,
                                   BufferFatPtrToStructTypeMap *TypeMap) {
  Type *Ty = F.getFunctionType();
  return Ty != TypeMap->remapType(Ty);
}

/// Move the body of `OldF` into a new function, returning it.
static Function *moveFunctionAdaptingType(Function *OldF, FunctionType *NewTy,
                                          ValueToValueMapTy &CloneMap) {
  bool IsIntrinsic = OldF->isIntrinsic();
  Function *NewF =
      Function::Create(NewTy, OldF->getLinkage(), OldF->getAddressSpace());
  NewF->IsNewDbgInfoFormat = OldF->IsNewDbgInfoFormat;
  NewF->copyAttributesFrom(OldF);
  NewF->copyMetadata(OldF, 0);
  NewF->takeName(OldF);
  NewF->updateAfterNameChange();
  NewF->setDLLStorageClass(OldF->getDLLStorageClass());
  OldF->getParent()->getFunctionList().insertAfter(OldF->getIterator(), NewF);

  while (!OldF->empty()) {
    BasicBlock *BB = &OldF->front();
    BB->removeFromParent();
    BB->insertInto(NewF);
    CloneMap[BB] = BB;
    for (Instruction &I : *BB) {
      CloneMap[&I] = &I;
    }
  }

  AttributeMask PtrOnlyAttrs;
  for (auto K :
       {Attribute::Dereferenceable, Attribute::DereferenceableOrNull,
        Attribute::NoAlias, Attribute::NoCapture, Attribute::NoFree,
        Attribute::NonNull, Attribute::NullPointerIsValid, Attribute::ReadNone,
        Attribute::ReadOnly, Attribute::WriteOnly}) {
    PtrOnlyAttrs.addAttribute(K);
  }
  SmallVector<AttributeSet> ArgAttrs;
  AttributeList OldAttrs = OldF->getAttributes();

  for (auto [I, OldArg, NewArg] : enumerate(OldF->args(), NewF->args())) {
    CloneMap[&NewArg] = &OldArg;
    NewArg.takeName(&OldArg);
    Type *OldArgTy = OldArg.getType(), *NewArgTy = NewArg.getType();
    // Temporarily mutate type of `NewArg` to allow RAUW to work.
    NewArg.mutateType(OldArgTy);
    OldArg.replaceAllUsesWith(&NewArg);
    NewArg.mutateType(NewArgTy);

    AttributeSet ArgAttr = OldAttrs.getParamAttrs(I);
    // Intrinsics get their attributes fixed later.
    if (OldArgTy != NewArgTy && !IsIntrinsic)
      ArgAttr = ArgAttr.removeAttributes(NewF->getContext(), PtrOnlyAttrs);
    ArgAttrs.push_back(ArgAttr);
  }
  AttributeSet RetAttrs = OldAttrs.getRetAttrs();
  if (OldF->getReturnType() != NewF->getReturnType() && !IsIntrinsic)
    RetAttrs = RetAttrs.removeAttributes(NewF->getContext(), PtrOnlyAttrs);
  NewF->setAttributes(AttributeList::get(
      NewF->getContext(), OldAttrs.getFnAttrs(), RetAttrs, ArgAttrs));
  return NewF;
}

static void makeCloneInPraceMap(Function *F, ValueToValueMapTy &CloneMap) {
  for (Argument &A : F->args())
    CloneMap[&A] = &A;
  for (BasicBlock &BB : *F) {
    CloneMap[&BB] = &BB;
    for (Instruction &I : BB)
      CloneMap[&I] = &I;
  }
}

bool AMDGPULowerBufferFatPointers::run(Module &M, const TargetMachine &TM) {
  bool Changed = false;
  const DataLayout &DL = M.getDataLayout();
  // Record the functions which need to be remapped.
  // The second element of the pair indicates whether the function has to have
  // its arguments or return types adjusted.
  SmallVector<std::pair<Function *, bool>> NeedsRemap;

  BufferFatPtrToStructTypeMap StructTM(DL);
  BufferFatPtrToIntTypeMap IntTM(DL);
  for (const GlobalVariable &GV : M.globals()) {
    if (GV.getAddressSpace() == AMDGPUAS::BUFFER_FAT_POINTER)
      report_fatal_error("Global variables with a buffer fat pointer address "
                         "space (7) are not supported");
    Type *VT = GV.getValueType();
    if (VT != StructTM.remapType(VT))
      report_fatal_error("Global variables that contain buffer fat pointers "
                         "(address space 7 pointers) are unsupported. Use "
                         "buffer resource pointers (address space 8) instead.");
  }

  {
    // Collect all constant exprs and aggregates referenced by any function.
    SmallVector<Constant *, 8> Worklist;
    for (Function &F : M.functions())
      for (Instruction &I : instructions(F))
        for (Value *Op : I.operands())
          if (isa<ConstantExpr>(Op) || isa<ConstantAggregate>(Op))
            Worklist.push_back(cast<Constant>(Op));

    // Recursively look for any referenced buffer pointer constants.
    SmallPtrSet<Constant *, 8> Visited;
    SetVector<Constant *> BufferFatPtrConsts;
    while (!Worklist.empty()) {
      Constant *C = Worklist.pop_back_val();
      if (!Visited.insert(C).second)
        continue;
      if (isBufferFatPtrOrVector(C->getType()))
        BufferFatPtrConsts.insert(C);
      for (Value *Op : C->operands())
        if (isa<ConstantExpr>(Op) || isa<ConstantAggregate>(Op))
          Worklist.push_back(cast<Constant>(Op));
    }

    // Expand all constant expressions using fat buffer pointers to
    // instructions.
    Changed |= convertUsersOfConstantsToInstructions(
        BufferFatPtrConsts.getArrayRef(), /*RestrictToFunc=*/nullptr,
        /*RemoveDeadConstants=*/false, /*IncludeSelf=*/true);
  }

  StoreFatPtrsAsIntsVisitor MemOpsRewrite(&IntTM, M.getContext());
  for (Function &F : M.functions()) {
    bool InterfaceChange = hasFatPointerInterface(F, &StructTM);
    bool BodyChanges = containsBufferFatPointers(F, &StructTM);
    Changed |= MemOpsRewrite.processFunction(F);
    if (InterfaceChange || BodyChanges)
      NeedsRemap.push_back(std::make_pair(&F, InterfaceChange));
  }
  if (NeedsRemap.empty())
    return Changed;

  SmallVector<Function *> NeedsPostProcess;
  SmallVector<Function *> Intrinsics;
  // Keep one big map so as to memoize constants across functions.
  ValueToValueMapTy CloneMap;
  FatPtrConstMaterializer Materializer(&StructTM, CloneMap);

  ValueMapper LowerInFuncs(CloneMap, RF_None, &StructTM, &Materializer);
  for (auto [F, InterfaceChange] : NeedsRemap) {
    Function *NewF = F;
    if (InterfaceChange)
      NewF = moveFunctionAdaptingType(
          F, cast<FunctionType>(StructTM.remapType(F->getFunctionType())),
          CloneMap);
    else
      makeCloneInPraceMap(F, CloneMap);
    LowerInFuncs.remapFunction(*NewF);
    if (NewF->isIntrinsic())
      Intrinsics.push_back(NewF);
    else
      NeedsPostProcess.push_back(NewF);
    if (InterfaceChange) {
      F->replaceAllUsesWith(NewF);
      F->eraseFromParent();
    }
    Changed = true;
  }
  StructTM.clear();
  IntTM.clear();
  CloneMap.clear();

  SplitPtrStructs Splitter(M.getContext(), &TM);
  for (Function *F : NeedsPostProcess)
    Splitter.processFunction(*F);
  for (Function *F : Intrinsics) {
    if (isRemovablePointerIntrinsic(F->getIntrinsicID())) {
      F->eraseFromParent();
    } else {
      std::optional<Function *> NewF = Intrinsic::remangleIntrinsicFunction(F);
      if (NewF)
        F->replaceAllUsesWith(*NewF);
    }
  }
  return Changed;
}

bool AMDGPULowerBufferFatPointers::runOnModule(Module &M) {
  TargetPassConfig &TPC = getAnalysis<TargetPassConfig>();
  const TargetMachine &TM = TPC.getTM<TargetMachine>();
  return run(M, TM);
}

char AMDGPULowerBufferFatPointers::ID = 0;

char &llvm::AMDGPULowerBufferFatPointersID = AMDGPULowerBufferFatPointers::ID;

void AMDGPULowerBufferFatPointers::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
}

#define PASS_DESC "Lower buffer fat pointer operations to buffer resources"
INITIALIZE_PASS_BEGIN(AMDGPULowerBufferFatPointers, DEBUG_TYPE, PASS_DESC,
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(AMDGPULowerBufferFatPointers, DEBUG_TYPE, PASS_DESC, false,
                    false)
#undef PASS_DESC

ModulePass *llvm::createAMDGPULowerBufferFatPointersPass() {
  return new AMDGPULowerBufferFatPointers();
}

PreservedAnalyses
AMDGPULowerBufferFatPointersPass::run(Module &M, ModuleAnalysisManager &MA) {
  return AMDGPULowerBufferFatPointers().run(M, TM) ? PreservedAnalyses::none()
                                                   : PreservedAnalyses::all();
}
