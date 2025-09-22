//===- TypeFinder.cpp - Implement the TypeFinder class --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TypeFinder class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/TypeFinder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <utility>

using namespace llvm;

void TypeFinder::run(const Module &M, bool onlyNamed) {
  OnlyNamed = onlyNamed;

  // Get types from global variables.
  for (const auto &G : M.globals()) {
    incorporateType(G.getValueType());
    if (G.hasInitializer())
      incorporateValue(G.getInitializer());
  }

  // Get types from aliases.
  for (const auto &A : M.aliases()) {
    incorporateType(A.getValueType());
    if (const Value *Aliasee = A.getAliasee())
      incorporateValue(Aliasee);
  }

  // Get types from ifuncs.
  for (const auto &GI : M.ifuncs())
    incorporateType(GI.getValueType());

  // Get types from functions.
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDForInst;
  for (const Function &FI : M) {
    incorporateType(FI.getFunctionType());
    incorporateAttributes(FI.getAttributes());

    for (const Use &U : FI.operands())
      incorporateValue(U.get());

    // First incorporate the arguments.
    for (const auto &A : FI.args())
      incorporateValue(&A);

    for (const BasicBlock &BB : FI)
      for (const Instruction &I : BB) {
        // Incorporate the type of the instruction.
        incorporateType(I.getType());

        // Incorporate non-instruction operand types. (We are incorporating all
        // instructions with this loop.)
        for (const auto &O : I.operands())
          if (&*O && !isa<Instruction>(&*O))
            incorporateValue(&*O);

        if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
          incorporateType(GEP->getSourceElementType());
        if (auto *AI = dyn_cast<AllocaInst>(&I))
          incorporateType(AI->getAllocatedType());
        if (const auto *CB = dyn_cast<CallBase>(&I))
          incorporateAttributes(CB->getAttributes());

        // Incorporate types hiding in metadata.
        I.getAllMetadataOtherThanDebugLoc(MDForInst);
        for (const auto &MD : MDForInst)
          incorporateMDNode(MD.second);
        MDForInst.clear();

        // Incorporate types hiding in variable-location information.
        for (const auto &Dbg : I.getDbgRecordRange()) {
          // Pick out records that have Values.
          if (const DbgVariableRecord *DVI =
                  dyn_cast<DbgVariableRecord>(&Dbg)) {
            for (Value *V : DVI->location_ops())
              incorporateValue(V);
            if (DVI->isDbgAssign()) {
              if (Value *Addr = DVI->getAddress())
                incorporateValue(Addr);
            }
          }
        }
      }
  }

  for (const auto &NMD : M.named_metadata())
    for (const auto *MDOp : NMD.operands())
      incorporateMDNode(MDOp);
}

void TypeFinder::clear() {
  VisitedConstants.clear();
  VisitedTypes.clear();
  StructTypes.clear();
}

/// incorporateType - This method adds the type to the list of used structures
/// if it's not in there already.
void TypeFinder::incorporateType(Type *Ty) {
  // Check to see if we've already visited this type.
  if (!VisitedTypes.insert(Ty).second)
    return;

  SmallVector<Type *, 4> TypeWorklist;
  TypeWorklist.push_back(Ty);
  do {
    Ty = TypeWorklist.pop_back_val();

    // If this is a structure or opaque type, add a name for the type.
    if (StructType *STy = dyn_cast<StructType>(Ty))
      if (!OnlyNamed || STy->hasName())
        StructTypes.push_back(STy);

    // Add all unvisited subtypes to worklist for processing
    for (Type *SubTy : llvm::reverse(Ty->subtypes()))
      if (VisitedTypes.insert(SubTy).second)
        TypeWorklist.push_back(SubTy);
  } while (!TypeWorklist.empty());
}

/// incorporateValue - This method is used to walk operand lists finding types
/// hiding in constant expressions and other operands that won't be walked in
/// other ways.  GlobalValues, basic blocks, instructions, and inst operands are
/// all explicitly enumerated.
void TypeFinder::incorporateValue(const Value *V) {
  if (const auto *M = dyn_cast<MetadataAsValue>(V)) {
    if (const auto *N = dyn_cast<MDNode>(M->getMetadata()))
      return incorporateMDNode(N);
    if (const auto *MDV = dyn_cast<ValueAsMetadata>(M->getMetadata()))
      return incorporateValue(MDV->getValue());
    if (const auto *AL = dyn_cast<DIArgList>(M->getMetadata())) {
      for (auto *Arg : AL->getArgs())
        incorporateValue(Arg->getValue());
      return;
    }
    return;
  }

  if (!isa<Constant>(V) || isa<GlobalValue>(V)) return;

  // Already visited?
  if (!VisitedConstants.insert(V).second)
    return;

  // Check this type.
  incorporateType(V->getType());

  // If this is an instruction, we incorporate it separately.
  if (isa<Instruction>(V))
    return;

  if (auto *GEP = dyn_cast<GEPOperator>(V))
    incorporateType(GEP->getSourceElementType());

  // Look in operands for types.
  const User *U = cast<User>(V);
  for (const auto &I : U->operands())
    incorporateValue(&*I);
}

/// incorporateMDNode - This method is used to walk the operands of an MDNode to
/// find types hiding within.
void TypeFinder::incorporateMDNode(const MDNode *V) {
  // Already visited?
  if (!VisitedMetadata.insert(V).second)
    return;

  // Look in operands for types.
  for (Metadata *Op : V->operands()) {
    if (!Op)
      continue;
    if (auto *N = dyn_cast<MDNode>(Op)) {
      incorporateMDNode(N);
      continue;
    }
    if (auto *C = dyn_cast<ConstantAsMetadata>(Op)) {
      incorporateValue(C->getValue());
      continue;
    }
  }
}

void TypeFinder::incorporateAttributes(AttributeList AL) {
  if (!VisitedAttributes.insert(AL).second)
    return;

  for (AttributeSet AS : AL)
    for (Attribute A : AS)
      if (A.isTypeAttribute())
        incorporateType(A.getValueAsType());
}
