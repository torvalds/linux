//===---- CGLoopInfo.h - LLVM CodeGen for loop metadata -*- C++ -*---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the internal state used for llvm translation for loop statement
// metadata.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGLOOPINFO_H
#define LLVM_CLANG_LIB_CODEGEN_CGLOOPINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class BasicBlock;
class Instruction;
class MDNode;
} // end namespace llvm

namespace clang {
class Attr;
class ASTContext;
class CodeGenOptions;
namespace CodeGen {

/// Attributes that may be specified on loops.
struct LoopAttributes {
  explicit LoopAttributes(bool IsParallel = false);
  void clear();

  /// Generate llvm.loop.parallel metadata for loads and stores.
  bool IsParallel;

  /// State of loop vectorization or unrolling.
  enum LVEnableState { Unspecified, Enable, Disable, Full };

  /// Value for llvm.loop.vectorize.enable metadata.
  LVEnableState VectorizeEnable;

  /// Value for llvm.loop.unroll.* metadata (enable, disable, or full).
  LVEnableState UnrollEnable;

  /// Value for llvm.loop.unroll_and_jam.* metadata (enable, disable, or full).
  LVEnableState UnrollAndJamEnable;

  /// Value for llvm.loop.vectorize.predicate metadata
  LVEnableState VectorizePredicateEnable;

  /// Value for llvm.loop.vectorize.width metadata.
  unsigned VectorizeWidth;

  // Value for llvm.loop.vectorize.scalable.enable
  LVEnableState VectorizeScalable;

  /// Value for llvm.loop.interleave.count metadata.
  unsigned InterleaveCount;

  /// llvm.unroll.
  unsigned UnrollCount;

  /// llvm.unroll.
  unsigned UnrollAndJamCount;

  /// Value for llvm.loop.distribute.enable metadata.
  LVEnableState DistributeEnable;

  /// Value for llvm.loop.pipeline.disable metadata.
  bool PipelineDisabled;

  /// Value for llvm.loop.pipeline.iicount metadata.
  unsigned PipelineInitiationInterval;

  /// Value for 'llvm.loop.align' metadata.
  unsigned CodeAlign;

  /// Value for whether the loop is required to make progress.
  bool MustProgress;
};

/// Information used when generating a structured loop.
class LoopInfo {
public:
  /// Construct a new LoopInfo for the loop with entry Header.
  LoopInfo(llvm::BasicBlock *Header, const LoopAttributes &Attrs,
           const llvm::DebugLoc &StartLoc, const llvm::DebugLoc &EndLoc,
           LoopInfo *Parent);

  /// Get the loop id metadata for this loop.
  llvm::MDNode *getLoopID() const { return TempLoopID.get(); }

  /// Get the header block of this loop.
  llvm::BasicBlock *getHeader() const { return Header; }

  /// Get the set of attributes active for this loop.
  const LoopAttributes &getAttributes() const { return Attrs; }

  /// Return this loop's access group or nullptr if it does not have one.
  llvm::MDNode *getAccessGroup() const { return AccGroup; }

  /// Create the loop's metadata. Must be called after its nested loops have
  /// been processed.
  void finish();

  /// Returns the first outer loop containing this loop if any, nullptr
  /// otherwise.
  const LoopInfo *getParent() const { return Parent; }

private:
  /// Loop ID metadata.
  llvm::TempMDTuple TempLoopID;
  /// Header block of this loop.
  llvm::BasicBlock *Header;
  /// The attributes for this loop.
  LoopAttributes Attrs;
  /// The access group for memory accesses parallel to this loop.
  llvm::MDNode *AccGroup = nullptr;
  /// Start location of this loop.
  llvm::DebugLoc StartLoc;
  /// End location of this loop.
  llvm::DebugLoc EndLoc;
  /// The next outer loop, or nullptr if this is the outermost loop.
  LoopInfo *Parent;
  /// If this loop has unroll-and-jam metadata, this can be set by the inner
  /// loop's LoopInfo to set the llvm.loop.unroll_and_jam.followup_inner
  /// metadata.
  llvm::MDNode *UnrollAndJamInnerFollowup = nullptr;

  /// Create a LoopID without any transformations.
  llvm::MDNode *
  createLoopPropertiesMetadata(llvm::ArrayRef<llvm::Metadata *> LoopProperties);

  /// Create a LoopID for transformations.
  ///
  /// The methods call each other in case multiple transformations are applied
  /// to a loop. The transformation first to be applied will use LoopID of the
  /// next transformation in its followup attribute.
  ///
  /// @param Attrs             The loop's transformations.
  /// @param LoopProperties    Non-transformation properties such as debug
  ///                          location, parallel accesses and disabled
  ///                          transformations. These are added to the returned
  ///                          LoopID.
  /// @param HasUserTransforms [out] Set to true if the returned MDNode encodes
  ///                          at least one transformation.
  ///
  /// @return A LoopID (metadata node) that can be used for the llvm.loop
  ///         annotation or followup-attribute.
  /// @{
  llvm::MDNode *
  createPipeliningMetadata(const LoopAttributes &Attrs,
                           llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                           bool &HasUserTransforms);
  llvm::MDNode *
  createPartialUnrollMetadata(const LoopAttributes &Attrs,
                              llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                              bool &HasUserTransforms);
  llvm::MDNode *
  createUnrollAndJamMetadata(const LoopAttributes &Attrs,
                             llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                             bool &HasUserTransforms);
  llvm::MDNode *
  createLoopVectorizeMetadata(const LoopAttributes &Attrs,
                              llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                              bool &HasUserTransforms);
  llvm::MDNode *
  createLoopDistributeMetadata(const LoopAttributes &Attrs,
                               llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                               bool &HasUserTransforms);
  llvm::MDNode *
  createFullUnrollMetadata(const LoopAttributes &Attrs,
                           llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                           bool &HasUserTransforms);
  /// @}

  /// Create a LoopID for this loop, including transformation-unspecific
  /// metadata such as debug location.
  ///
  /// @param Attrs             This loop's attributes and transformations.
  /// @param LoopProperties    Additional non-transformation properties to add
  ///                          to the LoopID, such as transformation-specific
  ///                          metadata that are not covered by @p Attrs.
  /// @param HasUserTransforms [out] Set to true if the returned MDNode encodes
  ///                          at least one transformation.
  ///
  /// @return A LoopID (metadata node) that can be used for the llvm.loop
  ///         annotation.
  llvm::MDNode *createMetadata(const LoopAttributes &Attrs,
                               llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                               bool &HasUserTransforms);
};

/// A stack of loop information corresponding to loop nesting levels.
/// This stack can be used to prepare attributes which are applied when a loop
/// is emitted.
class LoopInfoStack {
  LoopInfoStack(const LoopInfoStack &) = delete;
  void operator=(const LoopInfoStack &) = delete;

public:
  LoopInfoStack() {}

  /// Begin a new structured loop. The set of staged attributes will be
  /// applied to the loop and then cleared.
  void push(llvm::BasicBlock *Header, const llvm::DebugLoc &StartLoc,
            const llvm::DebugLoc &EndLoc);

  /// Begin a new structured loop. Stage attributes from the Attrs list.
  /// The staged attributes are applied to the loop and then cleared.
  void push(llvm::BasicBlock *Header, clang::ASTContext &Ctx,
            const clang::CodeGenOptions &CGOpts,
            llvm::ArrayRef<const Attr *> Attrs, const llvm::DebugLoc &StartLoc,
            const llvm::DebugLoc &EndLoc, bool MustProgress = false);

  /// End the current loop.
  void pop();

  /// Return the top loop id metadata.
  llvm::MDNode *getCurLoopID() const { return getInfo().getLoopID(); }

  /// Return true if the top loop is parallel.
  bool getCurLoopParallel() const {
    return hasInfo() ? getInfo().getAttributes().IsParallel : false;
  }

  /// Function called by the CodeGenFunction when an instruction is
  /// created.
  void InsertHelper(llvm::Instruction *I) const;

  /// Set the next pushed loop as parallel.
  void setParallel(bool Enable = true) { StagedAttrs.IsParallel = Enable; }

  /// Set the next pushed loop 'vectorize.enable'
  void setVectorizeEnable(bool Enable = true) {
    StagedAttrs.VectorizeEnable =
        Enable ? LoopAttributes::Enable : LoopAttributes::Disable;
  }

  /// Set the next pushed loop as a distribution candidate.
  void setDistributeState(bool Enable = true) {
    StagedAttrs.DistributeEnable =
        Enable ? LoopAttributes::Enable : LoopAttributes::Disable;
  }

  /// Set the next pushed loop unroll state.
  void setUnrollState(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.UnrollEnable = State;
  }

  /// Set the next pushed vectorize predicate state.
  void setVectorizePredicateState(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.VectorizePredicateEnable = State;
  }

  /// Set the next pushed loop unroll_and_jam state.
  void setUnrollAndJamState(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.UnrollAndJamEnable = State;
  }

  /// Set the vectorize width for the next loop pushed.
  void setVectorizeWidth(unsigned W) { StagedAttrs.VectorizeWidth = W; }

  void setVectorizeScalable(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.VectorizeScalable = State;
  }

  /// Set the interleave count for the next loop pushed.
  void setInterleaveCount(unsigned C) { StagedAttrs.InterleaveCount = C; }

  /// Set the unroll count for the next loop pushed.
  void setUnrollCount(unsigned C) { StagedAttrs.UnrollCount = C; }

  /// \brief Set the unroll count for the next loop pushed.
  void setUnrollAndJamCount(unsigned C) { StagedAttrs.UnrollAndJamCount = C; }

  /// Set the pipeline disabled state.
  void setPipelineDisabled(bool S) { StagedAttrs.PipelineDisabled = S; }

  /// Set the pipeline initiation interval.
  void setPipelineInitiationInterval(unsigned C) {
    StagedAttrs.PipelineInitiationInterval = C;
  }

  /// Set value of code align for the next loop pushed.
  void setCodeAlign(unsigned C) { StagedAttrs.CodeAlign = C; }

  /// Set no progress for the next loop pushed.
  void setMustProgress(bool P) { StagedAttrs.MustProgress = P; }

  /// Returns true if there is LoopInfo on the stack.
  bool hasInfo() const { return !Active.empty(); }
  /// Return the LoopInfo for the current loop. HasInfo should be called
  /// first to ensure LoopInfo is present.
  const LoopInfo &getInfo() const { return *Active.back(); }

private:
  /// The set of attributes that will be applied to the next pushed loop.
  LoopAttributes StagedAttrs;
  /// Stack of active loops.
  llvm::SmallVector<std::unique_ptr<LoopInfo>, 4> Active;
};

} // end namespace CodeGen
} // end namespace clang

#endif
