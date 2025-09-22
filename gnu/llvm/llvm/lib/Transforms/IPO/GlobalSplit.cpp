//===- GlobalSplit.cpp - global variable splitter -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass uses inrange annotations on GEP indices to split globals where
// beneficial. Clang currently attaches these annotations to references to
// virtual table globals under the Itanium ABI for the benefit of the
// whole-program virtual call optimization and control flow integrity passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/GlobalSplit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/IPO.h"
#include <cstdint>
#include <vector>

using namespace llvm;

static bool splitGlobal(GlobalVariable &GV) {
  // If the address of the global is taken outside of the module, we cannot
  // apply this transformation.
  if (!GV.hasLocalLinkage())
    return false;

  // We currently only know how to split ConstantStructs.
  auto *Init = dyn_cast_or_null<ConstantStruct>(GV.getInitializer());
  if (!Init)
    return false;

  const DataLayout &DL = GV.getDataLayout();
  const StructLayout *SL = DL.getStructLayout(Init->getType());
  ArrayRef<TypeSize> MemberOffsets = SL->getMemberOffsets();
  unsigned IndexWidth = DL.getIndexTypeSizeInBits(GV.getType());

  // Verify that each user of the global is an inrange getelementptr constant,
  // and collect information on how it relates to the global.
  struct GEPInfo {
    GEPOperator *GEP;
    unsigned MemberIndex;
    APInt MemberRelativeOffset;

    GEPInfo(GEPOperator *GEP, unsigned MemberIndex, APInt MemberRelativeOffset)
        : GEP(GEP), MemberIndex(MemberIndex),
          MemberRelativeOffset(std::move(MemberRelativeOffset)) {}
  };
  SmallVector<GEPInfo> Infos;
  for (User *U : GV.users()) {
    auto *GEP = dyn_cast<GEPOperator>(U);
    if (!GEP)
      return false;

    std::optional<ConstantRange> InRange = GEP->getInRange();
    if (!InRange)
      return false;

    APInt Offset(IndexWidth, 0);
    if (!GEP->accumulateConstantOffset(DL, Offset))
      return false;

    // Determine source-relative inrange.
    ConstantRange SrcInRange = InRange->sextOrTrunc(IndexWidth).add(Offset);

    // Check that the GEP offset is in the range (treating upper bound as
    // inclusive here).
    if (!SrcInRange.contains(Offset) && SrcInRange.getUpper() != Offset)
      return false;

    // Find which struct member the range corresponds to.
    if (SrcInRange.getLower().uge(SL->getSizeInBytes()))
      return false;

    unsigned MemberIndex =
        SL->getElementContainingOffset(SrcInRange.getLower().getZExtValue());
    TypeSize MemberStart = MemberOffsets[MemberIndex];
    TypeSize MemberEnd = MemberIndex == MemberOffsets.size() - 1
                             ? SL->getSizeInBytes()
                             : MemberOffsets[MemberIndex + 1];

    // Verify that the range matches that struct member.
    if (SrcInRange.getLower() != MemberStart ||
        SrcInRange.getUpper() != MemberEnd)
      return false;

    Infos.emplace_back(GEP, MemberIndex, Offset - MemberStart);
  }

  SmallVector<MDNode *, 2> Types;
  GV.getMetadata(LLVMContext::MD_type, Types);

  IntegerType *Int32Ty = Type::getInt32Ty(GV.getContext());

  std::vector<GlobalVariable *> SplitGlobals(Init->getNumOperands());
  for (unsigned I = 0; I != Init->getNumOperands(); ++I) {
    // Build a global representing this split piece.
    auto *SplitGV =
        new GlobalVariable(*GV.getParent(), Init->getOperand(I)->getType(),
                           GV.isConstant(), GlobalValue::PrivateLinkage,
                           Init->getOperand(I), GV.getName() + "." + utostr(I));
    SplitGlobals[I] = SplitGV;

    unsigned SplitBegin = SL->getElementOffset(I);
    unsigned SplitEnd = (I == Init->getNumOperands() - 1)
                            ? SL->getSizeInBytes()
                            : SL->getElementOffset(I + 1);

    // Rebuild type metadata, adjusting by the split offset.
    // FIXME: See if we can use DW_OP_piece to preserve debug metadata here.
    for (MDNode *Type : Types) {
      uint64_t ByteOffset = cast<ConstantInt>(
              cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
              ->getZExtValue();
      // Type metadata may be attached one byte after the end of the vtable, for
      // classes without virtual methods in Itanium ABI. AFAIK, it is never
      // attached to the first byte of a vtable. Subtract one to get the right
      // slice.
      // This is making an assumption that vtable groups are the only kinds of
      // global variables that !type metadata can be attached to, and that they
      // are either Itanium ABI vtable groups or contain a single vtable (i.e.
      // Microsoft ABI vtables).
      uint64_t AttachedTo = (ByteOffset == 0) ? ByteOffset : ByteOffset - 1;
      if (AttachedTo < SplitBegin || AttachedTo >= SplitEnd)
        continue;
      SplitGV->addMetadata(
          LLVMContext::MD_type,
          *MDNode::get(GV.getContext(),
                       {ConstantAsMetadata::get(
                            ConstantInt::get(Int32Ty, ByteOffset - SplitBegin)),
                        Type->getOperand(1)}));
    }

    if (GV.hasMetadata(LLVMContext::MD_vcall_visibility))
      SplitGV->setVCallVisibilityMetadata(GV.getVCallVisibility());
  }

  for (const GEPInfo &Info : Infos) {
    assert(Info.MemberIndex < SplitGlobals.size() && "Invalid member");
    auto *NewGEP = ConstantExpr::getGetElementPtr(
        Type::getInt8Ty(GV.getContext()), SplitGlobals[Info.MemberIndex],
        ConstantInt::get(GV.getContext(), Info.MemberRelativeOffset),
        Info.GEP->isInBounds());
    Info.GEP->replaceAllUsesWith(NewGEP);
  }

  // Finally, remove the original global. Any remaining uses refer to invalid
  // elements of the global, so replace with poison.
  if (!GV.use_empty())
    GV.replaceAllUsesWith(PoisonValue::get(GV.getType()));
  GV.eraseFromParent();
  return true;
}

static bool splitGlobals(Module &M) {
  // First, see if the module uses either of the llvm.type.test or
  // llvm.type.checked.load intrinsics, which indicates that splitting globals
  // may be beneficial.
  Function *TypeTestFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_test));
  Function *TypeCheckedLoadFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_checked_load));
  Function *TypeCheckedLoadRelativeFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_checked_load_relative));
  if ((!TypeTestFunc || TypeTestFunc->use_empty()) &&
      (!TypeCheckedLoadFunc || TypeCheckedLoadFunc->use_empty()) &&
      (!TypeCheckedLoadRelativeFunc ||
       TypeCheckedLoadRelativeFunc->use_empty()))
    return false;

  bool Changed = false;
  for (GlobalVariable &GV : llvm::make_early_inc_range(M.globals()))
    Changed |= splitGlobal(GV);
  return Changed;
}

PreservedAnalyses GlobalSplitPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (!splitGlobals(M))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
