//===- Target/DirectX/PointerTypeAnalisis.cpp - PointerType analysis ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Analysis pass to assign types to opaque pointers.
//
//===----------------------------------------------------------------------===//

#include "PointerTypeAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace llvm::dxil;

namespace {

// Classifies the type of the value passed in by walking the value's users to
// find a typed instruction to materialize a type from.
Type *classifyPointerType(const Value *V, PointerTypeMap &Map) {
  assert(V->getType()->isPointerTy() &&
         "classifyPointerType called with non-pointer");
  auto It = Map.find(V);
  if (It != Map.end())
    return It->second;

  Type *PointeeTy = nullptr;
  if (auto *Inst = dyn_cast<GetElementPtrInst>(V)) {
    if (!Inst->getResultElementType()->isPointerTy())
      PointeeTy = Inst->getResultElementType();
  } else if (auto *Inst = dyn_cast<AllocaInst>(V)) {
    PointeeTy = Inst->getAllocatedType();
  } else if (auto *GV = dyn_cast<GlobalVariable>(V)) {
    PointeeTy = GV->getValueType();
  }

  for (const auto *User : V->users()) {
    Type *NewPointeeTy = nullptr;
    if (const auto *Inst = dyn_cast<LoadInst>(User)) {
      NewPointeeTy = Inst->getType();
    } else if (const auto *Inst = dyn_cast<StoreInst>(User)) {
      NewPointeeTy = Inst->getValueOperand()->getType();
      // When store value is ptr type, cannot get more type info.
      if (NewPointeeTy->isPointerTy())
        continue;
    } else if (const auto *Inst = dyn_cast<GetElementPtrInst>(User)) {
      NewPointeeTy = Inst->getSourceElementType();
    }
    if (NewPointeeTy) {
      // HLSL doesn't support pointers, so it is unlikely to get more than one
      // or two levels of indirection in the IR. Because of this, recursion is
      // pretty safe.
      if (NewPointeeTy->isPointerTy()) {
        PointeeTy = classifyPointerType(User, Map);
        break;
      }
      if (!PointeeTy)
        PointeeTy = NewPointeeTy;
      else if (PointeeTy != NewPointeeTy)
        PointeeTy = Type::getInt8Ty(V->getContext());
    }
  }
  // If we were unable to determine the pointee type, set to i8
  if (!PointeeTy)
    PointeeTy = Type::getInt8Ty(V->getContext());
  auto *TypedPtrTy =
      TypedPointerType::get(PointeeTy, V->getType()->getPointerAddressSpace());

  Map[V] = TypedPtrTy;
  return TypedPtrTy;
}

// This function constructs a function type accepting typed pointers. It only
// handles function arguments and return types, and assigns the function type to
// the function's value in the type map.
Type *classifyFunctionType(const Function &F, PointerTypeMap &Map) {
  auto It = Map.find(&F);
  if (It != Map.end())
    return It->second;

  SmallVector<Type *, 8> NewArgs;
  Type *RetTy = F.getReturnType();
  LLVMContext &Ctx = F.getContext();
  if (RetTy->isPointerTy()) {
    RetTy = nullptr;
    for (const auto &B : F) {
      const auto *RetInst = dyn_cast_or_null<ReturnInst>(B.getTerminator());
      if (!RetInst)
        continue;

      Type *NewRetTy = classifyPointerType(RetInst->getReturnValue(), Map);
      if (!RetTy)
        RetTy = NewRetTy;
      else if (RetTy != NewRetTy)
        RetTy = TypedPointerType::get(
            Type::getInt8Ty(Ctx), F.getReturnType()->getPointerAddressSpace());
    }
    // For function decl.
    if (!RetTy)
      RetTy = TypedPointerType::get(
          Type::getInt8Ty(Ctx), F.getReturnType()->getPointerAddressSpace());
  }
  for (auto &A : F.args()) {
    Type *ArgTy = A.getType();
    if (ArgTy->isPointerTy())
      ArgTy = classifyPointerType(&A, Map);
    NewArgs.push_back(ArgTy);
  }
  auto *TypedPtrTy =
      TypedPointerType::get(FunctionType::get(RetTy, NewArgs, false), 0);
  Map[&F] = TypedPtrTy;
  return TypedPtrTy;
}
} // anonymous namespace

static Type *classifyConstantWithOpaquePtr(const Constant *C,
                                           PointerTypeMap &Map) {
  // FIXME: support ConstantPointerNull which could map to more than one
  // TypedPointerType.
  // See https://github.com/llvm/llvm-project/issues/57942.
  if (isa<ConstantPointerNull>(C))
    return TypedPointerType::get(Type::getInt8Ty(C->getContext()),
                                 C->getType()->getPointerAddressSpace());

  // Skip ConstantData which cannot have opaque ptr.
  if (isa<ConstantData>(C))
    return C->getType();

  auto It = Map.find(C);
  if (It != Map.end())
    return It->second;

  if (const auto *F = dyn_cast<Function>(C))
    return classifyFunctionType(*F, Map);

  Type *Ty = C->getType();
  Type *TargetTy = nullptr;
  if (auto *CS = dyn_cast<ConstantStruct>(C)) {
    SmallVector<Type *> EltTys;
    for (unsigned int I = 0; I < CS->getNumOperands(); ++I) {
      const Constant *Elt = C->getAggregateElement(I);
      Type *EltTy = classifyConstantWithOpaquePtr(Elt, Map);
      EltTys.emplace_back(EltTy);
    }
    TargetTy = StructType::get(C->getContext(), EltTys);
  } else if (auto *CA = dyn_cast<ConstantAggregate>(C)) {

    Type *TargetEltTy = nullptr;
    for (auto &Elt : CA->operands()) {
      Type *EltTy = classifyConstantWithOpaquePtr(cast<Constant>(&Elt), Map);
      assert(TargetEltTy == EltTy || TargetEltTy == nullptr);
      TargetEltTy = EltTy;
    }

    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      TargetTy = ArrayType::get(TargetEltTy, AT->getNumElements());
    } else {
      // Not struct, not array, must be vector here.
      auto *VT = cast<VectorType>(Ty);
      TargetTy = VectorType::get(TargetEltTy, VT);
    }
  }
  // Must have a target ty when map.
  assert(TargetTy && "PointerTypeAnalyisis failed to identify target type");

  // Same type, no need to map.
  if (TargetTy == Ty)
    return Ty;

  Map[C] = TargetTy;
  return TargetTy;
}

static void classifyGlobalCtorPointerType(const GlobalVariable &GV,
                                          PointerTypeMap &Map) {
  const auto *CA = cast<ConstantArray>(GV.getInitializer());
  // Type for global ctor should be array of { i32, void ()*, i8* }.
  Type *CtorArrayTy = classifyConstantWithOpaquePtr(CA, Map);

  // Map the global type.
  Map[&GV] = TypedPointerType::get(CtorArrayTy,
                                   GV.getType()->getPointerAddressSpace());
}

PointerTypeMap PointerTypeAnalysis::run(const Module &M) {
  PointerTypeMap Map;
  for (auto &G : M.globals()) {
    if (G.getType()->isPointerTy())
      classifyPointerType(&G, Map);
    if (G.getName() == "llvm.global_ctors")
      classifyGlobalCtorPointerType(G, Map);
  }

  for (auto &F : M) {
    classifyFunctionType(F, Map);

    for (const auto &B : F) {
      for (const auto &I : B) {
        if (I.getType()->isPointerTy())
          classifyPointerType(&I, Map);
      }
    }
  }
  return Map;
}
