//===- ReduceAttributes.cpp - Specialized Delta Pass ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting attributes.
//
//===----------------------------------------------------------------------===//

#include "ReduceAttributes.h"
#include "Delta.h"
#include "TestRunner.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace llvm {
class LLVMContext;
} // namespace llvm

using namespace llvm;

namespace {

/// Given ChunksToKeep, produce a map of global variables/functions/calls
/// and indexes of attributes to be preserved for each of them.
class AttributeRemapper : public InstVisitor<AttributeRemapper> {
  Oracle &O;
  LLVMContext &Context;

public:
  AttributeRemapper(Oracle &O, LLVMContext &C) : O(O), Context(C) {}

  void visitModule(Module &M) {
    for (GlobalVariable &GV : M.globals())
      visitGlobalVariable(GV);
  }

  void visitGlobalVariable(GlobalVariable &GV) {
    // Global variables only have one attribute set.
    AttributeSet AS = GV.getAttributes();
    if (AS.hasAttributes()) {
      AttrBuilder AttrsToPreserve(Context);
      visitAttributeSet(AS, AttrsToPreserve);
      GV.setAttributes(AttributeSet::get(Context, AttrsToPreserve));
    }
  }

  void visitFunction(Function &F) {
    // We can neither add nor remove attributes from intrinsics.
    if (F.getIntrinsicID() == Intrinsic::not_intrinsic)
      F.setAttributes(visitAttributeList(F.getAttributes()));
  }

  void visitCallBase(CallBase &CB) {
    CB.setAttributes(visitAttributeList(CB.getAttributes()));
  }

  AttributeSet visitAttributeIndex(AttributeList AL, unsigned Index) {
    AttrBuilder AttributesToPreserve(Context);
    visitAttributeSet(AL.getAttributes(Index), AttributesToPreserve);

    if (AttributesToPreserve.attrs().empty())
      return {};
    return AttributeSet::get(Context, AttributesToPreserve);
  }

  AttributeList visitAttributeList(AttributeList AL) {
    SmallVector<std::pair<unsigned, AttributeSet>> NewAttrList;
    NewAttrList.reserve(AL.getNumAttrSets());

    for (unsigned SetIdx : AL.indexes()) {
      if (SetIdx == AttributeList::FunctionIndex)
        continue;

      AttributeSet AttrSet = visitAttributeIndex(AL, SetIdx);
      if (AttrSet.hasAttributes())
        NewAttrList.emplace_back(SetIdx, AttrSet);
    }

    // FIXME: It's ridiculous that indexes() doesn't give us the correct order
    // for contructing a new AttributeList. Special case the function index so
    // we don't have to sort.
    AttributeSet FnAttrSet =
        visitAttributeIndex(AL, AttributeList::FunctionIndex);
    if (FnAttrSet.hasAttributes())
      NewAttrList.emplace_back(AttributeList::FunctionIndex, FnAttrSet);

    return AttributeList::get(Context, NewAttrList);
  }

  void visitAttributeSet(const AttributeSet &AS, AttrBuilder &AttrsToPreserve) {
    // Optnone requires noinline, so removing noinline requires removing the
    // pair.
    Attribute NoInline = AS.getAttribute(Attribute::NoInline);
    bool RemoveNoInline = false;
    if (NoInline.isValid()) {
      RemoveNoInline = !O.shouldKeep();
      if (!RemoveNoInline)
        AttrsToPreserve.addAttribute(NoInline);
    }

    for (Attribute A : AS) {
      if (A.isEnumAttribute()) {
        Attribute::AttrKind Kind = A.getKindAsEnum();
        if (Kind == Attribute::NoInline)
          continue;

        if (RemoveNoInline && Kind == Attribute::OptimizeNone)
          continue;

        // TODO: Could only remove this if there are no constrained calls in the
        // function.
        if (Kind == Attribute::StrictFP) {
          AttrsToPreserve.addAttribute(A);
          continue;
        }
      }

      if (O.shouldKeep())
        AttrsToPreserve.addAttribute(A);
    }
  }
};

} // namespace

/// Removes out-of-chunk attributes from module.
static void extractAttributesFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  AttributeRemapper R(O, WorkItem.getContext());
  R.visit(WorkItem.getModule());
}

void llvm::reduceAttributesDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractAttributesFromModule, "Reducing Attributes");
}
