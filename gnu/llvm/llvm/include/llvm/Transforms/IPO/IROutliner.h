//===- IROutliner.h - Extract similar IR regions into functions --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// The interface file for the IROutliner which is used by the IROutliner Pass.
//
// The outliner uses the IRSimilarityIdentifier to identify the similar regions
// of code.  It evaluates each set of IRSimilarityCandidates with an estimate of
// whether it will provide code size reduction.  Each region is extracted using
// the code extractor.  These extracted functions are consolidated into a single
// function and called from the extracted call site.
//
// For example:
// \code
//   %1 = add i32 %a, %b
//   %2 = add i32 %b, %a
//   %3 = add i32 %b, %a
//   %4 = add i32 %a, %b
// \endcode
// would become function
// \code
// define internal void outlined_ir_function(i32 %0, i32 %1) {
//   %1 = add i32 %0, %1
//   %2 = add i32 %1, %0
//   ret void
// }
// \endcode
// with calls:
// \code
//   call void outlined_ir_function(i32 %a, i32 %b)
//   call void outlined_ir_function(i32 %b, i32 %a)
// \endcode
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_IROUTLINER_H
#define LLVM_TRANSFORMS_IPO_IROUTLINER_H

#include "llvm/Analysis/IRSimilarityIdentifier.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

struct OutlinableGroup;

namespace llvm {
using namespace CallingConv;
using namespace IRSimilarity;

class Module;
class TargetTransformInfo;
class OptimizationRemarkEmitter;

/// The OutlinableRegion holds all the information for a specific region, or
/// sequence of instructions. This includes what values need to be hoisted to
/// arguments from the extracted function, inputs and outputs to the region, and
/// mapping from the extracted function arguments to overall function arguments.
struct OutlinableRegion {
  /// Describes the region of code.
  IRSimilarityCandidate *Candidate = nullptr;

  /// If this region is outlined, the front and back IRInstructionData could
  /// potentially become invalidated if the only new instruction is a call.
  /// This ensures that we replace in the instruction in the IRInstructionData.
  IRInstructionData *NewFront = nullptr;
  IRInstructionData *NewBack = nullptr;

  /// The number of extracted inputs from the CodeExtractor.
  unsigned NumExtractedInputs = 0;

  /// The corresponding BasicBlock with the appropriate stores for this
  /// OutlinableRegion in the overall function.
  unsigned OutputBlockNum = -1;

  /// Mapping the extracted argument number to the argument number in the
  /// overall function.  Since there will be inputs, such as elevated constants
  /// that are not the same in each region in a SimilarityGroup, or values that
  /// cannot be sunk into the extracted section in every region, we must keep
  /// track of which extracted argument maps to which overall argument.
  DenseMap<unsigned, unsigned> ExtractedArgToAgg;
  DenseMap<unsigned, unsigned> AggArgToExtracted;

  /// Values in the outlined functions will often be replaced by arguments. When
  /// finding corresponding values from one region to another, the found value
  /// will be the value the argument previously replaced.  This structure maps
  /// any replaced values for the region to the aggregate aggregate argument
  /// in the overall function.
  DenseMap<Value *, Value *> RemappedArguments;

  /// Marks whether we need to change the order of the arguments when mapping
  /// the old extracted function call to the new aggregate outlined function
  /// call.
  bool ChangedArgOrder = false;

  /// Marks whether this region ends in a branch, there is special handling
  /// required for the following basic blocks in this case.
  bool EndsInBranch = false;

  /// The PHIBlocks with their corresponding return block based on the return
  /// value as the key.
  DenseMap<Value *, BasicBlock *> PHIBlocks;

  /// Mapping of the argument number in the deduplicated function
  /// to a given constant, which is used when creating the arguments to the call
  /// to the newly created deduplicated function.  This is handled separately
  /// since the CodeExtractor does not recognize constants.
  DenseMap<unsigned, Constant *> AggArgToConstant;

  /// The global value numbers that are used as outputs for this section. Once
  /// extracted, each output will be stored to an output register.  This
  /// documents the global value numbers that are used in this pattern.
  SmallVector<unsigned, 4> GVNStores;

  /// Used to create an outlined function.
  CodeExtractor *CE = nullptr;

  /// The call site of the extracted region.
  CallInst *Call = nullptr;

  /// The function for the extracted region.
  Function *ExtractedFunction = nullptr;

  /// Flag for whether we have split out the IRSimilarityCanidate. That is,
  /// make the region contained the IRSimilarityCandidate its own BasicBlock.
  bool CandidateSplit = false;

  /// Flag for whether we should not consider this region for extraction.
  bool IgnoreRegion = false;

  /// The BasicBlock that is before the start of the region BasicBlock,
  /// only defined when the region has been split.
  BasicBlock *PrevBB = nullptr;

  /// The BasicBlock that contains the starting instruction of the region.
  BasicBlock *StartBB = nullptr;

  /// The BasicBlock that contains the ending instruction of the region.
  BasicBlock *EndBB = nullptr;

  /// The BasicBlock that is after the start of the region BasicBlock,
  /// only defined when the region has been split.
  BasicBlock *FollowBB = nullptr;

  /// The Outlinable Group that contains this region and structurally similar
  /// regions to this region.
  OutlinableGroup *Parent = nullptr;

  OutlinableRegion(IRSimilarityCandidate &C, OutlinableGroup &Group)
      : Candidate(&C), Parent(&Group) {
    StartBB = C.getStartBB();
    EndBB = C.getEndBB();
  }

  /// For the contained region, split the parent BasicBlock at the starting and
  /// ending instructions of the contained IRSimilarityCandidate.
  void splitCandidate();

  /// For the contained region, reattach the BasicBlock at the starting and
  /// ending instructions of the contained IRSimilarityCandidate, or if the
  /// function has been extracted, the start and end of the BasicBlock
  /// containing the called function.
  void reattachCandidate();

  /// Find a corresponding value for \p V in similar OutlinableRegion \p Other.
  ///
  /// \param Other [in] - The OutlinableRegion to find the corresponding Value
  /// in.
  /// \param V [in] - The Value to look for in the other region.
  /// \return The corresponding Value to \p V if it exists, otherwise nullptr.
  Value *findCorrespondingValueIn(const OutlinableRegion &Other, Value *V);

  /// Find a corresponding BasicBlock for \p BB in similar OutlinableRegion \p Other.
  ///
  /// \param Other [in] - The OutlinableRegion to find the corresponding
  /// BasicBlock in.
  /// \param BB [in] - The BasicBlock to look for in the other region.
  /// \return The corresponding Value to \p V if it exists, otherwise nullptr.
  BasicBlock *findCorrespondingBlockIn(const OutlinableRegion &Other,
                                       BasicBlock *BB);

  /// Get the size of the code removed from the region.
  ///
  /// \param [in] TTI - The TargetTransformInfo for the parent function.
  /// \returns the code size of the region
  InstructionCost getBenefit(TargetTransformInfo &TTI);
};

/// This class is a pass that identifies similarity in a Module, extracts
/// instances of the similarity, and then consolidating the similar regions
/// in an effort to reduce code size.  It uses the IRSimilarityIdentifier pass
/// to identify the similar regions of code, and then extracts the similar
/// sections into a single function.  See the above for an example as to
/// how code is extracted and consolidated into a single function.
class IROutliner {
public:
  IROutliner(function_ref<TargetTransformInfo &(Function &)> GTTI,
             function_ref<IRSimilarityIdentifier &(Module &)> GIRSI,
             function_ref<OptimizationRemarkEmitter &(Function &)> GORE)
      : getTTI(GTTI), getIRSI(GIRSI), getORE(GORE) {
    
    // Check that the DenseMap implementation has not changed.
    assert(DenseMapInfo<unsigned>::getEmptyKey() == (unsigned)-1 &&
           "DenseMapInfo<unsigned>'s empty key isn't -1!");
    assert(DenseMapInfo<unsigned>::getTombstoneKey() == (unsigned)-2 &&
           "DenseMapInfo<unsigned>'s tombstone key isn't -2!");
  }
  bool run(Module &M);

private:
  /// Find repeated similar code sequences in \p M and outline them into new
  /// Functions.
  ///
  /// \param [in] M - The module to outline from.
  /// \returns The number of Functions created.
  unsigned doOutline(Module &M);

  /// Check whether an OutlinableRegion is incompatible with code already
  /// outlined. OutlinableRegions are incomptaible when there are overlapping
  /// instructions, or code that has not been recorded has been added to the
  /// instructions.
  ///
  /// \param [in] Region - The OutlinableRegion to check for conflicts with
  /// already outlined code.
  /// \returns whether the region can safely be outlined.
  bool isCompatibleWithAlreadyOutlinedCode(const OutlinableRegion &Region);

  /// Remove all the IRSimilarityCandidates from \p CandidateVec that have
  /// instructions contained in a previously outlined region and put the
  /// remaining regions in \p CurrentGroup.
  ///
  /// \param [in] CandidateVec - List of similarity candidates for regions with
  /// the same similarity structure.
  /// \param [in,out] CurrentGroup - Contains the potential sections to
  /// be outlined.
  void
  pruneIncompatibleRegions(std::vector<IRSimilarityCandidate> &CandidateVec,
                           OutlinableGroup &CurrentGroup);

  /// Create the function based on the overall types found in the current
  /// regions being outlined.
  ///
  /// \param M - The module to outline from.
  /// \param [in,out] CG - The OutlinableGroup for the regions to be outlined.
  /// \param [in] FunctionNameSuffix - How many functions have we previously
  /// created.
  /// \returns the newly created function.
  Function *createFunction(Module &M, OutlinableGroup &CG,
                           unsigned FunctionNameSuffix);

  /// Identify the needed extracted inputs in a section, and add to the overall
  /// function if needed.
  ///
  /// \param [in] M - The module to outline from.
  /// \param [in,out] Region - The region to be extracted.
  /// \param [in] NotSame - The global value numbers of the Values in the region
  /// that do not have the same Constant in each strucutrally similar region.
  void findAddInputsOutputs(Module &M, OutlinableRegion &Region,
                            DenseSet<unsigned> &NotSame);

  /// Find the number of instructions that will be removed by extracting the
  /// OutlinableRegions in \p CurrentGroup.
  ///
  /// \param [in] CurrentGroup - The collection of OutlinableRegions to be
  /// analyzed.
  /// \returns the number of outlined instructions across all regions.
  InstructionCost findBenefitFromAllRegions(OutlinableGroup &CurrentGroup);

  /// Find the number of instructions that will be added by reloading arguments.
  ///
  /// \param [in] CurrentGroup - The collection of OutlinableRegions to be
  /// analyzed.
  /// \returns the number of added reload instructions across all regions.
  InstructionCost findCostOutputReloads(OutlinableGroup &CurrentGroup);

  /// Find the cost and the benefit of \p CurrentGroup and save it back to
  /// \p CurrentGroup.
  ///
  /// \param [in] M - The module being analyzed
  /// \param [in,out] CurrentGroup - The overall outlined section
  void findCostBenefit(Module &M, OutlinableGroup &CurrentGroup);

  /// Update the output mapping based on the load instruction, and the outputs
  /// of the extracted function.
  ///
  /// \param Region - The region extracted
  /// \param Outputs - The outputs from the extracted function.
  /// \param LI - The load instruction used to update the mapping.
  void updateOutputMapping(OutlinableRegion &Region,
                           ArrayRef<Value *> Outputs, LoadInst *LI);

  /// Extract \p Region into its own function.
  ///
  /// \param [in] Region - The region to be extracted into its own function.
  /// \returns True if it was successfully outlined.
  bool extractSection(OutlinableRegion &Region);

  /// For the similarities found, and the extracted sections, create a single
  /// outlined function with appropriate output blocks as necessary.
  ///
  /// \param [in] M - The module to outline from
  /// \param [in] CurrentGroup - The set of extracted sections to consolidate.
  /// \param [in,out] FuncsToRemove - List of functions to remove from the
  /// module after outlining is completed.
  /// \param [in,out] OutlinedFunctionNum - the number of new outlined
  /// functions.
  void deduplicateExtractedSections(Module &M, OutlinableGroup &CurrentGroup,
                                    std::vector<Function *> &FuncsToRemove,
                                    unsigned &OutlinedFunctionNum);

  /// If true, enables us to outline from functions that have LinkOnceFromODR
  /// linkages.
  bool OutlineFromLinkODRs = false;

  /// If false, we do not worry if the cost is greater than the benefit.  This
  /// is for debugging and testing, so that we can test small cases to ensure
  /// that the outlining is being done correctly.
  bool CostModel = true;

  /// The set of outlined Instructions, identified by their location in the
  /// sequential ordering of instructions in a Module.
  DenseSet<unsigned> Outlined;

  /// TargetTransformInfo lambda for target specific information.
  function_ref<TargetTransformInfo &(Function &)> getTTI;

  /// A mapping from newly created reloaded output values to the original value.
  /// If an value is replace by an output from an outlined region, this maps
  /// that Value, back to its original Value.
  DenseMap<Value *, Value *> OutputMappings;

  /// IRSimilarityIdentifier lambda to retrieve IRSimilarityIdentifier.
  function_ref<IRSimilarityIdentifier &(Module &)> getIRSI;

  /// The optimization remark emitter for the pass.
  function_ref<OptimizationRemarkEmitter &(Function &)> getORE;

  /// The memory allocator used to allocate the CodeExtractors.
  SpecificBumpPtrAllocator<CodeExtractor> ExtractorAllocator;

  /// The memory allocator used to allocate the OutlinableRegions.
  SpecificBumpPtrAllocator<OutlinableRegion> RegionAllocator;

  /// The memory allocator used to allocate new IRInstructionData.
  SpecificBumpPtrAllocator<IRInstructionData> InstDataAllocator;

  /// Custom InstVisitor to classify different instructions for whether it can
  /// be analyzed for similarity.  This is needed as there may be instruction we
  /// can identify as having similarity, but are more complicated to outline.
  struct InstructionAllowed : public InstVisitor<InstructionAllowed, bool> {
    InstructionAllowed() = default;

    bool visitBranchInst(BranchInst &BI) { return EnableBranches; }
    bool visitPHINode(PHINode &PN) { return EnableBranches; }
    // TODO: Handle allocas.
    bool visitAllocaInst(AllocaInst &AI) { return false; }
    // VAArg instructions are not allowed since this could cause difficulty when
    // differentiating between different sets of variable instructions in
    // the deduplicated outlined regions.
    bool visitVAArgInst(VAArgInst &VI) { return false; }
    // We exclude all exception handling cases since they are so context
    // dependent.
    bool visitLandingPadInst(LandingPadInst &LPI) { return false; }
    bool visitFuncletPadInst(FuncletPadInst &FPI) { return false; }
    // DebugInfo should be included in the regions, but should not be
    // analyzed for similarity as it has no bearing on the outcome of the
    // program.
    bool visitDbgInfoIntrinsic(DbgInfoIntrinsic &DII) { return true; }
    // TODO: Handle specific intrinsics individually from those that can be
    // handled.
    bool IntrinsicInst(IntrinsicInst &II) { return EnableIntrinsics; }
    // We only handle CallInsts that are not indirect, since we cannot guarantee
    // that they have a name in these cases.
    bool visitCallInst(CallInst &CI) {
      Function *F = CI.getCalledFunction();
      bool IsIndirectCall = CI.isIndirectCall();
      if (IsIndirectCall && !EnableIndirectCalls)
        return false;
      if (!F && !IsIndirectCall)
        return false;
      // Returning twice can cause issues with the state of the function call
      // that were not expected when the function was used, so we do not include
      // the call in outlined functions.
      if (CI.canReturnTwice())
        return false;
      // TODO: Update the outliner to capture whether the outlined function
      // needs these extra attributes.

      // Functions marked with the swifttailcc and tailcc calling conventions
      // require special handling when outlining musttail functions.  The
      // calling convention must be passed down to the outlined function as
      // well. Further, there is special handling for musttail calls as well,
      // requiring a return call directly after.  For now, the outliner does not
      // support this.
      bool IsTailCC = CI.getCallingConv() == CallingConv::SwiftTail ||
                      CI.getCallingConv() == CallingConv::Tail;
      if (IsTailCC && !EnableMustTailCalls)
        return false;
      if (CI.isMustTailCall() && !EnableMustTailCalls)
        return false;
      // The outliner can only handle musttail items if it is also accompanied
      // by the tailcc or swifttailcc calling convention.
      if (CI.isMustTailCall() && !IsTailCC)
        return false;
      return true;
    }
    // TODO: Handle FreezeInsts.  Since a frozen value could be frozen inside
    // the outlined region, and then returned as an output, this will have to be
    // handled differently.
    bool visitFreezeInst(FreezeInst &CI) { return false; }
    // TODO: We do not current handle similarity that changes the control flow.
    bool visitInvokeInst(InvokeInst &II) { return false; }
    // TODO: We do not current handle similarity that changes the control flow.
    bool visitCallBrInst(CallBrInst &CBI) { return false; }
    // TODO: Handle interblock similarity.
    bool visitTerminator(Instruction &I) { return false; }
    bool visitInstruction(Instruction &I) { return true; }

    // The flag variable that marks whether we should allow branch instructions
    // to be outlined.
    bool EnableBranches = false;

    // The flag variable that marks whether we should allow indirect calls
    // to be outlined.
    bool EnableIndirectCalls = true;

    // The flag variable that marks whether we should allow intrinsics
    // instructions to be outlined.
    bool EnableIntrinsics = false;

    // The flag variable that marks whether we should allow musttail calls.
    bool EnableMustTailCalls = false;
  };

  /// A InstVisitor used to exclude certain instructions from being outlined.
  InstructionAllowed InstructionClassifier;
};

/// Pass to outline similar regions.
class IROutlinerPass : public PassInfoMixin<IROutlinerPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_IROUTLINER_H
