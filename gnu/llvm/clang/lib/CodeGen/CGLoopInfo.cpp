//===---- CGLoopInfo.cpp - LLVM CodeGen for loop metadata -*- C++ -*-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CGLoopInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/CodeGenOptions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include <optional>
using namespace clang::CodeGen;
using namespace llvm;

MDNode *
LoopInfo::createLoopPropertiesMetadata(ArrayRef<Metadata *> LoopProperties) {
  LLVMContext &Ctx = Header->getContext();
  SmallVector<Metadata *, 4> NewLoopProperties;
  NewLoopProperties.push_back(nullptr);
  NewLoopProperties.append(LoopProperties.begin(), LoopProperties.end());

  MDNode *LoopID = MDNode::getDistinct(Ctx, NewLoopProperties);
  LoopID->replaceOperandWith(0, LoopID);
  return LoopID;
}

MDNode *LoopInfo::createPipeliningMetadata(const LoopAttributes &Attrs,
                                           ArrayRef<Metadata *> LoopProperties,
                                           bool &HasUserTransforms) {
  LLVMContext &Ctx = Header->getContext();

  std::optional<bool> Enabled;
  if (Attrs.PipelineDisabled)
    Enabled = false;
  else if (Attrs.PipelineInitiationInterval != 0)
    Enabled = true;

  if (Enabled != true) {
    SmallVector<Metadata *, 4> NewLoopProperties;
    if (Enabled == false) {
      NewLoopProperties.append(LoopProperties.begin(), LoopProperties.end());
      NewLoopProperties.push_back(
          MDNode::get(Ctx, {MDString::get(Ctx, "llvm.loop.pipeline.disable"),
                            ConstantAsMetadata::get(ConstantInt::get(
                                llvm::Type::getInt1Ty(Ctx), 1))}));
      LoopProperties = NewLoopProperties;
    }
    return createLoopPropertiesMetadata(LoopProperties);
  }

  SmallVector<Metadata *, 4> Args;
  Args.push_back(nullptr);
  Args.append(LoopProperties.begin(), LoopProperties.end());

  if (Attrs.PipelineInitiationInterval > 0) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.pipeline.initiationinterval"),
        ConstantAsMetadata::get(ConstantInt::get(
            llvm::Type::getInt32Ty(Ctx), Attrs.PipelineInitiationInterval))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // No follow-up: This is the last transformation.

  MDNode *LoopID = MDNode::getDistinct(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);
  HasUserTransforms = true;
  return LoopID;
}

MDNode *
LoopInfo::createPartialUnrollMetadata(const LoopAttributes &Attrs,
                                      ArrayRef<Metadata *> LoopProperties,
                                      bool &HasUserTransforms) {
  LLVMContext &Ctx = Header->getContext();

  std::optional<bool> Enabled;
  if (Attrs.UnrollEnable == LoopAttributes::Disable)
    Enabled = false;
  else if (Attrs.UnrollEnable == LoopAttributes::Full)
    Enabled = std::nullopt;
  else if (Attrs.UnrollEnable != LoopAttributes::Unspecified ||
           Attrs.UnrollCount != 0)
    Enabled = true;

  if (Enabled != true) {
    // createFullUnrollMetadata will already have added llvm.loop.unroll.disable
    // if unrolling is disabled.
    return createPipeliningMetadata(Attrs, LoopProperties, HasUserTransforms);
  }

  SmallVector<Metadata *, 4> FollowupLoopProperties;

  // Apply all loop properties to the unrolled loop.
  FollowupLoopProperties.append(LoopProperties.begin(), LoopProperties.end());

  // Don't unroll an already unrolled loop.
  FollowupLoopProperties.push_back(
      MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.disable")));

  bool FollowupHasTransforms = false;
  MDNode *Followup = createPipeliningMetadata(Attrs, FollowupLoopProperties,
                                              FollowupHasTransforms);

  SmallVector<Metadata *, 4> Args;
  Args.push_back(nullptr);
  Args.append(LoopProperties.begin(), LoopProperties.end());

  // Setting unroll.count
  if (Attrs.UnrollCount > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll.count"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            llvm::Type::getInt32Ty(Ctx), Attrs.UnrollCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting unroll.full or unroll.disable
  if (Attrs.UnrollEnable == LoopAttributes::Enable) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll.enable")};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (FollowupHasTransforms)
    Args.push_back(MDNode::get(
        Ctx, {MDString::get(Ctx, "llvm.loop.unroll.followup_all"), Followup}));

  MDNode *LoopID = MDNode::getDistinct(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);
  HasUserTransforms = true;
  return LoopID;
}

MDNode *
LoopInfo::createUnrollAndJamMetadata(const LoopAttributes &Attrs,
                                     ArrayRef<Metadata *> LoopProperties,
                                     bool &HasUserTransforms) {
  LLVMContext &Ctx = Header->getContext();

  std::optional<bool> Enabled;
  if (Attrs.UnrollAndJamEnable == LoopAttributes::Disable)
    Enabled = false;
  else if (Attrs.UnrollAndJamEnable == LoopAttributes::Enable ||
           Attrs.UnrollAndJamCount != 0)
    Enabled = true;

  if (Enabled != true) {
    SmallVector<Metadata *, 4> NewLoopProperties;
    if (Enabled == false) {
      NewLoopProperties.append(LoopProperties.begin(), LoopProperties.end());
      NewLoopProperties.push_back(MDNode::get(
          Ctx, MDString::get(Ctx, "llvm.loop.unroll_and_jam.disable")));
      LoopProperties = NewLoopProperties;
    }
    return createPartialUnrollMetadata(Attrs, LoopProperties,
                                       HasUserTransforms);
  }

  SmallVector<Metadata *, 4> FollowupLoopProperties;
  FollowupLoopProperties.append(LoopProperties.begin(), LoopProperties.end());
  FollowupLoopProperties.push_back(
      MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll_and_jam.disable")));

  bool FollowupHasTransforms = false;
  MDNode *Followup = createPartialUnrollMetadata(Attrs, FollowupLoopProperties,
                                                 FollowupHasTransforms);

  SmallVector<Metadata *, 4> Args;
  Args.push_back(nullptr);
  Args.append(LoopProperties.begin(), LoopProperties.end());

  // Setting unroll_and_jam.count
  if (Attrs.UnrollAndJamCount > 0) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.unroll_and_jam.count"),
        ConstantAsMetadata::get(ConstantInt::get(llvm::Type::getInt32Ty(Ctx),
                                                 Attrs.UnrollAndJamCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.UnrollAndJamEnable == LoopAttributes::Enable) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll_and_jam.enable")};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (FollowupHasTransforms)
    Args.push_back(MDNode::get(
        Ctx, {MDString::get(Ctx, "llvm.loop.unroll_and_jam.followup_outer"),
              Followup}));

  if (UnrollAndJamInnerFollowup)
    Args.push_back(MDNode::get(
        Ctx, {MDString::get(Ctx, "llvm.loop.unroll_and_jam.followup_inner"),
              UnrollAndJamInnerFollowup}));

  MDNode *LoopID = MDNode::getDistinct(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);
  HasUserTransforms = true;
  return LoopID;
}

MDNode *
LoopInfo::createLoopVectorizeMetadata(const LoopAttributes &Attrs,
                                      ArrayRef<Metadata *> LoopProperties,
                                      bool &HasUserTransforms) {
  LLVMContext &Ctx = Header->getContext();

  std::optional<bool> Enabled;
  if (Attrs.VectorizeEnable == LoopAttributes::Disable)
    Enabled = false;
  else if (Attrs.VectorizeEnable != LoopAttributes::Unspecified ||
           Attrs.VectorizePredicateEnable != LoopAttributes::Unspecified ||
           Attrs.InterleaveCount != 0 || Attrs.VectorizeWidth != 0 ||
           Attrs.VectorizeScalable != LoopAttributes::Unspecified)
    Enabled = true;

  if (Enabled != true) {
    SmallVector<Metadata *, 4> NewLoopProperties;
    if (Enabled == false) {
      NewLoopProperties.append(LoopProperties.begin(), LoopProperties.end());
      NewLoopProperties.push_back(
          MDNode::get(Ctx, {MDString::get(Ctx, "llvm.loop.vectorize.enable"),
                            ConstantAsMetadata::get(ConstantInt::get(
                                llvm::Type::getInt1Ty(Ctx), 0))}));
      LoopProperties = NewLoopProperties;
    }
    return createUnrollAndJamMetadata(Attrs, LoopProperties, HasUserTransforms);
  }

  // Apply all loop properties to the vectorized loop.
  SmallVector<Metadata *, 4> FollowupLoopProperties;
  FollowupLoopProperties.append(LoopProperties.begin(), LoopProperties.end());

  // Don't vectorize an already vectorized loop.
  FollowupLoopProperties.push_back(
      MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.isvectorized")));

  bool FollowupHasTransforms = false;
  MDNode *Followup = createUnrollAndJamMetadata(Attrs, FollowupLoopProperties,
                                                FollowupHasTransforms);

  SmallVector<Metadata *, 4> Args;
  Args.push_back(nullptr);
  Args.append(LoopProperties.begin(), LoopProperties.end());

  // Setting vectorize.predicate when it has been specified and vectorization
  // has not been disabled.
  bool IsVectorPredicateEnabled = false;
  if (Attrs.VectorizePredicateEnable != LoopAttributes::Unspecified) {
    IsVectorPredicateEnabled =
        (Attrs.VectorizePredicateEnable == LoopAttributes::Enable);

    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.vectorize.predicate.enable"),
        ConstantAsMetadata::get(ConstantInt::get(llvm::Type::getInt1Ty(Ctx),
                                                 IsVectorPredicateEnabled))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting vectorize.width
  if (Attrs.VectorizeWidth > 0) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.vectorize.width"),
        ConstantAsMetadata::get(ConstantInt::get(llvm::Type::getInt32Ty(Ctx),
                                                 Attrs.VectorizeWidth))};

    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.VectorizeScalable != LoopAttributes::Unspecified) {
    bool IsScalable = Attrs.VectorizeScalable == LoopAttributes::Enable;
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.vectorize.scalable.enable"),
        ConstantAsMetadata::get(
            ConstantInt::get(llvm::Type::getInt1Ty(Ctx), IsScalable))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting interleave.count
  if (Attrs.InterleaveCount > 0) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.interleave.count"),
        ConstantAsMetadata::get(ConstantInt::get(llvm::Type::getInt32Ty(Ctx),
                                                 Attrs.InterleaveCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // vectorize.enable is set if:
  // 1) loop hint vectorize.enable is set, or
  // 2) it is implied when vectorize.predicate is set, or
  // 3) it is implied when vectorize.width is set to a value > 1
  // 4) it is implied when vectorize.scalable.enable is true
  // 5) it is implied when vectorize.width is unset (0) and the user
  //    explicitly requested fixed-width vectorization, i.e.
  //    vectorize.scalable.enable is false.
  if (Attrs.VectorizeEnable != LoopAttributes::Unspecified ||
      (IsVectorPredicateEnabled && Attrs.VectorizeWidth != 1) ||
      Attrs.VectorizeWidth > 1 ||
      Attrs.VectorizeScalable == LoopAttributes::Enable ||
      (Attrs.VectorizeScalable == LoopAttributes::Disable &&
       Attrs.VectorizeWidth != 1)) {
    bool AttrVal = Attrs.VectorizeEnable != LoopAttributes::Disable;
    Args.push_back(
        MDNode::get(Ctx, {MDString::get(Ctx, "llvm.loop.vectorize.enable"),
                          ConstantAsMetadata::get(ConstantInt::get(
                              llvm::Type::getInt1Ty(Ctx), AttrVal))}));
  }

  if (FollowupHasTransforms)
    Args.push_back(MDNode::get(
        Ctx,
        {MDString::get(Ctx, "llvm.loop.vectorize.followup_all"), Followup}));

  MDNode *LoopID = MDNode::getDistinct(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);
  HasUserTransforms = true;
  return LoopID;
}

MDNode *
LoopInfo::createLoopDistributeMetadata(const LoopAttributes &Attrs,
                                       ArrayRef<Metadata *> LoopProperties,
                                       bool &HasUserTransforms) {
  LLVMContext &Ctx = Header->getContext();

  std::optional<bool> Enabled;
  if (Attrs.DistributeEnable == LoopAttributes::Disable)
    Enabled = false;
  if (Attrs.DistributeEnable == LoopAttributes::Enable)
    Enabled = true;

  if (Enabled != true) {
    SmallVector<Metadata *, 4> NewLoopProperties;
    if (Enabled == false) {
      NewLoopProperties.append(LoopProperties.begin(), LoopProperties.end());
      NewLoopProperties.push_back(
          MDNode::get(Ctx, {MDString::get(Ctx, "llvm.loop.distribute.enable"),
                            ConstantAsMetadata::get(ConstantInt::get(
                                llvm::Type::getInt1Ty(Ctx), 0))}));
      LoopProperties = NewLoopProperties;
    }
    return createLoopVectorizeMetadata(Attrs, LoopProperties,
                                       HasUserTransforms);
  }

  bool FollowupHasTransforms = false;
  MDNode *Followup =
      createLoopVectorizeMetadata(Attrs, LoopProperties, FollowupHasTransforms);

  SmallVector<Metadata *, 4> Args;
  Args.push_back(nullptr);
  Args.append(LoopProperties.begin(), LoopProperties.end());

  Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.distribute.enable"),
                      ConstantAsMetadata::get(ConstantInt::get(
                          llvm::Type::getInt1Ty(Ctx),
                          (Attrs.DistributeEnable == LoopAttributes::Enable)))};
  Args.push_back(MDNode::get(Ctx, Vals));

  if (FollowupHasTransforms)
    Args.push_back(MDNode::get(
        Ctx,
        {MDString::get(Ctx, "llvm.loop.distribute.followup_all"), Followup}));

  MDNode *LoopID = MDNode::getDistinct(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);
  HasUserTransforms = true;
  return LoopID;
}

MDNode *LoopInfo::createFullUnrollMetadata(const LoopAttributes &Attrs,
                                           ArrayRef<Metadata *> LoopProperties,
                                           bool &HasUserTransforms) {
  LLVMContext &Ctx = Header->getContext();

  std::optional<bool> Enabled;
  if (Attrs.UnrollEnable == LoopAttributes::Disable)
    Enabled = false;
  else if (Attrs.UnrollEnable == LoopAttributes::Full)
    Enabled = true;

  if (Enabled != true) {
    SmallVector<Metadata *, 4> NewLoopProperties;
    if (Enabled == false) {
      NewLoopProperties.append(LoopProperties.begin(), LoopProperties.end());
      NewLoopProperties.push_back(
          MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.disable")));
      LoopProperties = NewLoopProperties;
    }
    return createLoopDistributeMetadata(Attrs, LoopProperties,
                                        HasUserTransforms);
  }

  SmallVector<Metadata *, 4> Args;
  Args.push_back(nullptr);
  Args.append(LoopProperties.begin(), LoopProperties.end());
  Args.push_back(MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.full")));

  // No follow-up: there is no loop after full unrolling.
  // TODO: Warn if there are transformations after full unrolling.

  MDNode *LoopID = MDNode::getDistinct(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);
  HasUserTransforms = true;
  return LoopID;
}

MDNode *LoopInfo::createMetadata(
    const LoopAttributes &Attrs,
    llvm::ArrayRef<llvm::Metadata *> AdditionalLoopProperties,
    bool &HasUserTransforms) {
  SmallVector<Metadata *, 3> LoopProperties;

  // If we have a valid start debug location for the loop, add it.
  if (StartLoc) {
    LoopProperties.push_back(StartLoc.getAsMDNode());

    // If we also have a valid end debug location for the loop, add it.
    if (EndLoc)
      LoopProperties.push_back(EndLoc.getAsMDNode());
  }

  LLVMContext &Ctx = Header->getContext();
  if (Attrs.MustProgress)
    LoopProperties.push_back(
        MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.mustprogress")));

  assert(!!AccGroup == Attrs.IsParallel &&
         "There must be an access group iff the loop is parallel");
  if (Attrs.IsParallel) {
    LoopProperties.push_back(MDNode::get(
        Ctx, {MDString::get(Ctx, "llvm.loop.parallel_accesses"), AccGroup}));
  }

  // Setting clang::code_align attribute.
  if (Attrs.CodeAlign > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.align"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            llvm::Type::getInt32Ty(Ctx), Attrs.CodeAlign))};
    LoopProperties.push_back(MDNode::get(Ctx, Vals));
  }

  LoopProperties.insert(LoopProperties.end(), AdditionalLoopProperties.begin(),
                        AdditionalLoopProperties.end());
  return createFullUnrollMetadata(Attrs, LoopProperties, HasUserTransforms);
}

LoopAttributes::LoopAttributes(bool IsParallel)
    : IsParallel(IsParallel), VectorizeEnable(LoopAttributes::Unspecified),
      UnrollEnable(LoopAttributes::Unspecified),
      UnrollAndJamEnable(LoopAttributes::Unspecified),
      VectorizePredicateEnable(LoopAttributes::Unspecified), VectorizeWidth(0),
      VectorizeScalable(LoopAttributes::Unspecified), InterleaveCount(0),
      UnrollCount(0), UnrollAndJamCount(0),
      DistributeEnable(LoopAttributes::Unspecified), PipelineDisabled(false),
      PipelineInitiationInterval(0), CodeAlign(0), MustProgress(false) {}

void LoopAttributes::clear() {
  IsParallel = false;
  VectorizeWidth = 0;
  VectorizeScalable = LoopAttributes::Unspecified;
  InterleaveCount = 0;
  UnrollCount = 0;
  UnrollAndJamCount = 0;
  VectorizeEnable = LoopAttributes::Unspecified;
  UnrollEnable = LoopAttributes::Unspecified;
  UnrollAndJamEnable = LoopAttributes::Unspecified;
  VectorizePredicateEnable = LoopAttributes::Unspecified;
  DistributeEnable = LoopAttributes::Unspecified;
  PipelineDisabled = false;
  PipelineInitiationInterval = 0;
  CodeAlign = 0;
  MustProgress = false;
}

LoopInfo::LoopInfo(BasicBlock *Header, const LoopAttributes &Attrs,
                   const llvm::DebugLoc &StartLoc, const llvm::DebugLoc &EndLoc,
                   LoopInfo *Parent)
    : Header(Header), Attrs(Attrs), StartLoc(StartLoc), EndLoc(EndLoc),
      Parent(Parent) {

  if (Attrs.IsParallel) {
    // Create an access group for this loop.
    LLVMContext &Ctx = Header->getContext();
    AccGroup = MDNode::getDistinct(Ctx, {});
  }

  if (!Attrs.IsParallel && Attrs.VectorizeWidth == 0 &&
      Attrs.VectorizeScalable == LoopAttributes::Unspecified &&
      Attrs.InterleaveCount == 0 && Attrs.UnrollCount == 0 &&
      Attrs.UnrollAndJamCount == 0 && !Attrs.PipelineDisabled &&
      Attrs.PipelineInitiationInterval == 0 &&
      Attrs.VectorizePredicateEnable == LoopAttributes::Unspecified &&
      Attrs.VectorizeEnable == LoopAttributes::Unspecified &&
      Attrs.UnrollEnable == LoopAttributes::Unspecified &&
      Attrs.UnrollAndJamEnable == LoopAttributes::Unspecified &&
      Attrs.DistributeEnable == LoopAttributes::Unspecified &&
      Attrs.CodeAlign == 0 && !StartLoc && !EndLoc && !Attrs.MustProgress)
    return;

  TempLoopID = MDNode::getTemporary(Header->getContext(), std::nullopt);
}

void LoopInfo::finish() {
  // We did not annotate the loop body instructions because there are no
  // attributes for this loop.
  if (!TempLoopID)
    return;

  MDNode *LoopID;
  LoopAttributes CurLoopAttr = Attrs;
  LLVMContext &Ctx = Header->getContext();

  if (Parent && (Parent->Attrs.UnrollAndJamEnable ||
                 Parent->Attrs.UnrollAndJamCount != 0)) {
    // Parent unroll-and-jams this loop.
    // Split the transformations in those that happens before the unroll-and-jam
    // and those after.

    LoopAttributes BeforeJam, AfterJam;

    BeforeJam.IsParallel = AfterJam.IsParallel = Attrs.IsParallel;

    BeforeJam.VectorizeWidth = Attrs.VectorizeWidth;
    BeforeJam.VectorizeScalable = Attrs.VectorizeScalable;
    BeforeJam.InterleaveCount = Attrs.InterleaveCount;
    BeforeJam.VectorizeEnable = Attrs.VectorizeEnable;
    BeforeJam.DistributeEnable = Attrs.DistributeEnable;
    BeforeJam.VectorizePredicateEnable = Attrs.VectorizePredicateEnable;

    switch (Attrs.UnrollEnable) {
    case LoopAttributes::Unspecified:
    case LoopAttributes::Disable:
      BeforeJam.UnrollEnable = Attrs.UnrollEnable;
      AfterJam.UnrollEnable = Attrs.UnrollEnable;
      break;
    case LoopAttributes::Full:
      BeforeJam.UnrollEnable = LoopAttributes::Full;
      break;
    case LoopAttributes::Enable:
      AfterJam.UnrollEnable = LoopAttributes::Enable;
      break;
    }

    AfterJam.VectorizePredicateEnable = Attrs.VectorizePredicateEnable;
    AfterJam.UnrollCount = Attrs.UnrollCount;
    AfterJam.PipelineDisabled = Attrs.PipelineDisabled;
    AfterJam.PipelineInitiationInterval = Attrs.PipelineInitiationInterval;

    // If this loop is subject of an unroll-and-jam by the parent loop, and has
    // an unroll-and-jam annotation itself, we have to decide whether to first
    // apply the parent's unroll-and-jam or this loop's unroll-and-jam. The
    // UnrollAndJam pass processes loops from inner to outer, so we apply the
    // inner first.
    BeforeJam.UnrollAndJamCount = Attrs.UnrollAndJamCount;
    BeforeJam.UnrollAndJamEnable = Attrs.UnrollAndJamEnable;

    // Set the inner followup metadata to process by the outer loop. Only
    // consider the first inner loop.
    if (!Parent->UnrollAndJamInnerFollowup) {
      // Splitting the attributes into a BeforeJam and an AfterJam part will
      // stop 'llvm.loop.isvectorized' (generated by vectorization in BeforeJam)
      // to be forwarded to the AfterJam part. We detect the situation here and
      // add it manually.
      SmallVector<Metadata *, 1> BeforeLoopProperties;
      if (BeforeJam.VectorizeEnable != LoopAttributes::Unspecified ||
          BeforeJam.VectorizePredicateEnable != LoopAttributes::Unspecified ||
          BeforeJam.InterleaveCount != 0 || BeforeJam.VectorizeWidth != 0 ||
          BeforeJam.VectorizeScalable == LoopAttributes::Enable)
        BeforeLoopProperties.push_back(
            MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.isvectorized")));

      bool InnerFollowupHasTransform = false;
      MDNode *InnerFollowup = createMetadata(AfterJam, BeforeLoopProperties,
                                             InnerFollowupHasTransform);
      if (InnerFollowupHasTransform)
        Parent->UnrollAndJamInnerFollowup = InnerFollowup;
    }

    CurLoopAttr = BeforeJam;
  }

  bool HasUserTransforms = false;
  LoopID = createMetadata(CurLoopAttr, {}, HasUserTransforms);
  TempLoopID->replaceAllUsesWith(LoopID);
}

void LoopInfoStack::push(BasicBlock *Header, const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc) {
  Active.emplace_back(
      new LoopInfo(Header, StagedAttrs, StartLoc, EndLoc,
                   Active.empty() ? nullptr : Active.back().get()));
  // Clear the attributes so nested loops do not inherit them.
  StagedAttrs.clear();
}

void LoopInfoStack::push(BasicBlock *Header, clang::ASTContext &Ctx,
                         const clang::CodeGenOptions &CGOpts,
                         ArrayRef<const clang::Attr *> Attrs,
                         const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc, bool MustProgress) {
  // Identify loop hint attributes from Attrs.
  for (const auto *Attr : Attrs) {
    const LoopHintAttr *LH = dyn_cast<LoopHintAttr>(Attr);
    const OpenCLUnrollHintAttr *OpenCLHint =
        dyn_cast<OpenCLUnrollHintAttr>(Attr);
    const HLSLLoopHintAttr *HLSLLoopHint = dyn_cast<HLSLLoopHintAttr>(Attr);
    // Skip non loop hint attributes
    if (!LH && !OpenCLHint && !HLSLLoopHint) {
      continue;
    }

    LoopHintAttr::OptionType Option = LoopHintAttr::Unroll;
    LoopHintAttr::LoopHintState State = LoopHintAttr::Disable;
    unsigned ValueInt = 1;
    // Translate opencl_unroll_hint attribute argument to
    // equivalent LoopHintAttr enums.
    // OpenCL v2.0 s6.11.5:
    // 0 - enable unroll (no argument).
    // 1 - disable unroll.
    // other positive integer n - unroll by n.
    if (OpenCLHint) {
      ValueInt = OpenCLHint->getUnrollHint();
      if (ValueInt == 0) {
        State = LoopHintAttr::Enable;
      } else if (ValueInt != 1) {
        Option = LoopHintAttr::UnrollCount;
        State = LoopHintAttr::Numeric;
      }
    } else if (HLSLLoopHint) {
      ValueInt = HLSLLoopHint->getDirective();
      if (HLSLLoopHint->getSemanticSpelling() ==
          HLSLLoopHintAttr::Spelling::Microsoft_unroll) {
        if (ValueInt == 0)
          State = LoopHintAttr::Enable;
        if (ValueInt > 0) {
          Option = LoopHintAttr::UnrollCount;
          State = LoopHintAttr::Numeric;
        }
      }
    } else if (LH) {
      auto *ValueExpr = LH->getValue();
      if (ValueExpr) {
        llvm::APSInt ValueAPS = ValueExpr->EvaluateKnownConstInt(Ctx);
        ValueInt = ValueAPS.getSExtValue();
      }

      Option = LH->getOption();
      State = LH->getState();
    }
    switch (State) {
    case LoopHintAttr::Disable:
      switch (Option) {
      case LoopHintAttr::Vectorize:
        // Disable vectorization by specifying a width of 1.
        setVectorizeWidth(1);
        setVectorizeScalable(LoopAttributes::Unspecified);
        break;
      case LoopHintAttr::Interleave:
        // Disable interleaving by speciyfing a count of 1.
        setInterleaveCount(1);
        break;
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Disable);
        break;
      case LoopHintAttr::UnrollAndJam:
        setUnrollAndJamState(LoopAttributes::Disable);
        break;
      case LoopHintAttr::VectorizePredicate:
        setVectorizePredicateState(LoopAttributes::Disable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(false);
        break;
      case LoopHintAttr::PipelineDisabled:
        setPipelineDisabled(true);
        break;
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::PipelineInitiationInterval:
        llvm_unreachable("Options cannot be disabled.");
        break;
      }
      break;
    case LoopHintAttr::Enable:
      switch (Option) {
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
        setVectorizeEnable(true);
        break;
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Enable);
        break;
      case LoopHintAttr::UnrollAndJam:
        setUnrollAndJamState(LoopAttributes::Enable);
        break;
      case LoopHintAttr::VectorizePredicate:
        setVectorizePredicateState(LoopAttributes::Enable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(true);
        break;
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::PipelineDisabled:
      case LoopHintAttr::PipelineInitiationInterval:
        llvm_unreachable("Options cannot enabled.");
        break;
      }
      break;
    case LoopHintAttr::AssumeSafety:
      switch (Option) {
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
        // Apply "llvm.mem.parallel_loop_access" metadata to load/stores.
        setParallel(true);
        setVectorizeEnable(true);
        break;
      case LoopHintAttr::Unroll:
      case LoopHintAttr::UnrollAndJam:
      case LoopHintAttr::VectorizePredicate:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::PipelineDisabled:
      case LoopHintAttr::PipelineInitiationInterval:
        llvm_unreachable("Options cannot be used to assume mem safety.");
        break;
      }
      break;
    case LoopHintAttr::Full:
      switch (Option) {
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Full);
        break;
      case LoopHintAttr::UnrollAndJam:
        setUnrollAndJamState(LoopAttributes::Full);
        break;
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::PipelineDisabled:
      case LoopHintAttr::PipelineInitiationInterval:
      case LoopHintAttr::VectorizePredicate:
        llvm_unreachable("Options cannot be used with 'full' hint.");
        break;
      }
      break;
    case LoopHintAttr::FixedWidth:
    case LoopHintAttr::ScalableWidth:
      switch (Option) {
      case LoopHintAttr::VectorizeWidth:
        setVectorizeScalable(State == LoopHintAttr::ScalableWidth
                                 ? LoopAttributes::Enable
                                 : LoopAttributes::Disable);
        if (LH->getValue())
          setVectorizeWidth(ValueInt);
        break;
      default:
        llvm_unreachable("Options cannot be used with 'scalable' hint.");
        break;
      }
      break;
    case LoopHintAttr::Numeric:
      switch (Option) {
      case LoopHintAttr::InterleaveCount:
        setInterleaveCount(ValueInt);
        break;
      case LoopHintAttr::UnrollCount:
        setUnrollCount(ValueInt);
        break;
      case LoopHintAttr::UnrollAndJamCount:
        setUnrollAndJamCount(ValueInt);
        break;
      case LoopHintAttr::PipelineInitiationInterval:
        setPipelineInitiationInterval(ValueInt);
        break;
      case LoopHintAttr::Unroll:
      case LoopHintAttr::UnrollAndJam:
      case LoopHintAttr::VectorizePredicate:
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::PipelineDisabled:
        llvm_unreachable("Options cannot be assigned a value.");
        break;
      }
      break;
    }
  }

  // Identify loop attribute 'code_align' from Attrs.
  // For attribute code_align:
  // n - 'llvm.loop.align i32 n' metadata will be emitted.
  if (const auto *CodeAlign = getSpecificAttr<const CodeAlignAttr>(Attrs)) {
    const auto *CE = cast<ConstantExpr>(CodeAlign->getAlignment());
    llvm::APSInt ArgVal = CE->getResultAsAPSInt();
    setCodeAlign(ArgVal.getSExtValue());
  }

  setMustProgress(MustProgress);

  if (CGOpts.OptimizationLevel > 0)
    // Disable unrolling for the loop, if unrolling is disabled (via
    // -fno-unroll-loops) and no pragmas override the decision.
    if (!CGOpts.UnrollLoops &&
        (StagedAttrs.UnrollEnable == LoopAttributes::Unspecified &&
         StagedAttrs.UnrollCount == 0))
      setUnrollState(LoopAttributes::Disable);

  /// Stage the attributes.
  push(Header, StartLoc, EndLoc);
}

void LoopInfoStack::pop() {
  assert(!Active.empty() && "No active loops to pop");
  Active.back()->finish();
  Active.pop_back();
}

void LoopInfoStack::InsertHelper(Instruction *I) const {
  if (I->mayReadOrWriteMemory()) {
    SmallVector<Metadata *, 4> AccessGroups;
    for (const auto &AL : Active) {
      // Here we assume that every loop that has an access group is parallel.
      if (MDNode *Group = AL->getAccessGroup())
        AccessGroups.push_back(Group);
    }
    MDNode *UnionMD = nullptr;
    if (AccessGroups.size() == 1)
      UnionMD = cast<MDNode>(AccessGroups[0]);
    else if (AccessGroups.size() >= 2)
      UnionMD = MDNode::get(I->getContext(), AccessGroups);
    I->setMetadata("llvm.access.group", UnionMD);
  }

  if (!hasInfo())
    return;

  const LoopInfo &L = getInfo();
  if (!L.getLoopID())
    return;

  if (I->isTerminator()) {
    for (BasicBlock *Succ : successors(I))
      if (Succ == L.getHeader()) {
        I->setMetadata(llvm::LLVMContext::MD_loop, L.getLoopID());
        break;
      }
    return;
  }
}
