//===- IROutliner.cpp -- Outline Similar Regions ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
// Implementation for the IROutliner which is used by the IROutliner Pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/IROutliner.h"
#include "llvm/Analysis/IRSimilarityIdentifier.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/IPO.h"
#include <optional>
#include <vector>

#define DEBUG_TYPE "iroutliner"

using namespace llvm;
using namespace IRSimilarity;

// A command flag to be used for debugging to exclude branches from similarity
// matching and outlining.
namespace llvm {
extern cl::opt<bool> DisableBranches;

// A command flag to be used for debugging to indirect calls from similarity
// matching and outlining.
extern cl::opt<bool> DisableIndirectCalls;

// A command flag to be used for debugging to exclude intrinsics from similarity
// matching and outlining.
extern cl::opt<bool> DisableIntrinsics;

} // namespace llvm

// Set to true if the user wants the ir outliner to run on linkonceodr linkage
// functions. This is false by default because the linker can dedupe linkonceodr
// functions. Since the outliner is confined to a single module (modulo LTO),
// this is off by default. It should, however, be the default behavior in
// LTO.
static cl::opt<bool> EnableLinkOnceODRIROutlining(
    "enable-linkonceodr-ir-outlining", cl::Hidden,
    cl::desc("Enable the IR outliner on linkonceodr functions"),
    cl::init(false));

// This is a debug option to test small pieces of code to ensure that outlining
// works correctly.
static cl::opt<bool> NoCostModel(
    "ir-outlining-no-cost", cl::init(false), cl::ReallyHidden,
    cl::desc("Debug option to outline greedily, without restriction that "
             "calculated benefit outweighs cost"));

/// The OutlinableGroup holds all the overarching information for outlining
/// a set of regions that are structurally similar to one another, such as the
/// types of the overall function, the output blocks, the sets of stores needed
/// and a list of the different regions. This information is used in the
/// deduplication of extracted regions with the same structure.
struct OutlinableGroup {
  /// The sections that could be outlined
  std::vector<OutlinableRegion *> Regions;

  /// The argument types for the function created as the overall function to
  /// replace the extracted function for each region.
  std::vector<Type *> ArgumentTypes;
  /// The FunctionType for the overall function.
  FunctionType *OutlinedFunctionType = nullptr;
  /// The Function for the collective overall function.
  Function *OutlinedFunction = nullptr;

  /// Flag for whether we should not consider this group of OutlinableRegions
  /// for extraction.
  bool IgnoreGroup = false;

  /// The return blocks for the overall function.
  DenseMap<Value *, BasicBlock *> EndBBs;

  /// The PHIBlocks with their corresponding return block based on the return
  /// value as the key.
  DenseMap<Value *, BasicBlock *> PHIBlocks;

  /// A set containing the different GVN store sets needed. Each array contains
  /// a sorted list of the different values that need to be stored into output
  /// registers.
  DenseSet<ArrayRef<unsigned>> OutputGVNCombinations;

  /// Flag for whether the \ref ArgumentTypes have been defined after the
  /// extraction of the first region.
  bool InputTypesSet = false;

  /// The number of input values in \ref ArgumentTypes.  Anything after this
  /// index in ArgumentTypes is an output argument.
  unsigned NumAggregateInputs = 0;

  /// The mapping of the canonical numbering of the values in outlined sections
  /// to specific arguments.
  DenseMap<unsigned, unsigned> CanonicalNumberToAggArg;

  /// The number of branches in the region target a basic block that is outside
  /// of the region.
  unsigned BranchesToOutside = 0;

  /// Tracker counting backwards from the highest unsigned value possible to
  /// avoid conflicting with the GVNs of assigned values.  We start at -3 since
  /// -2 and -1 are assigned by the DenseMap.
  unsigned PHINodeGVNTracker = -3;

  DenseMap<unsigned,
           std::pair<std::pair<unsigned, unsigned>, SmallVector<unsigned, 2>>>
      PHINodeGVNToGVNs;
  DenseMap<hash_code, unsigned> GVNsToPHINodeGVN;

  /// The number of instructions that will be outlined by extracting \ref
  /// Regions.
  InstructionCost Benefit = 0;
  /// The number of added instructions needed for the outlining of the \ref
  /// Regions.
  InstructionCost Cost = 0;

  /// The argument that needs to be marked with the swifterr attribute.  If not
  /// needed, there is no value.
  std::optional<unsigned> SwiftErrorArgument;

  /// For the \ref Regions, we look at every Value.  If it is a constant,
  /// we check whether it is the same in Region.
  ///
  /// \param [in,out] NotSame contains the global value numbers where the
  /// constant is not always the same, and must be passed in as an argument.
  void findSameConstants(DenseSet<unsigned> &NotSame);

  /// For the regions, look at each set of GVN stores needed and account for
  /// each combination.  Add an argument to the argument types if there is
  /// more than one combination.
  ///
  /// \param [in] M - The module we are outlining from.
  void collectGVNStoreSets(Module &M);
};

/// Move the contents of \p SourceBB to before the last instruction of \p
/// TargetBB.
/// \param SourceBB - the BasicBlock to pull Instructions from.
/// \param TargetBB - the BasicBlock to put Instruction into.
static void moveBBContents(BasicBlock &SourceBB, BasicBlock &TargetBB) {
  TargetBB.splice(TargetBB.end(), &SourceBB);
}

/// A function to sort the keys of \p Map, which must be a mapping of constant
/// values to basic blocks and return it in \p SortedKeys
///
/// \param SortedKeys - The vector the keys will be return in and sorted.
/// \param Map - The DenseMap containing keys to sort.
static void getSortedConstantKeys(std::vector<Value *> &SortedKeys,
                                  DenseMap<Value *, BasicBlock *> &Map) {
  for (auto &VtoBB : Map)
    SortedKeys.push_back(VtoBB.first);

  // Here we expect to have either 1 value that is void (nullptr) or multiple
  // values that are all constant integers.
  if (SortedKeys.size() == 1) {
    assert(!SortedKeys[0] && "Expected a single void value.");
    return;
  }

  stable_sort(SortedKeys, [](const Value *LHS, const Value *RHS) {
    assert(LHS && RHS && "Expected non void values.");
    const ConstantInt *LHSC = cast<ConstantInt>(LHS);
    const ConstantInt *RHSC = cast<ConstantInt>(RHS);

    return LHSC->getLimitedValue() < RHSC->getLimitedValue();
  });
}

Value *OutlinableRegion::findCorrespondingValueIn(const OutlinableRegion &Other,
                                                  Value *V) {
  std::optional<unsigned> GVN = Candidate->getGVN(V);
  assert(GVN && "No GVN for incoming value");
  std::optional<unsigned> CanonNum = Candidate->getCanonicalNum(*GVN);
  std::optional<unsigned> FirstGVN =
      Other.Candidate->fromCanonicalNum(*CanonNum);
  std::optional<Value *> FoundValueOpt = Other.Candidate->fromGVN(*FirstGVN);
  return FoundValueOpt.value_or(nullptr);
}

BasicBlock *
OutlinableRegion::findCorrespondingBlockIn(const OutlinableRegion &Other,
                                           BasicBlock *BB) {
  Instruction *FirstNonPHI = BB->getFirstNonPHIOrDbg();
  assert(FirstNonPHI && "block is empty?");
  Value *CorrespondingVal = findCorrespondingValueIn(Other, FirstNonPHI);
  if (!CorrespondingVal)
    return nullptr;
  BasicBlock *CorrespondingBlock =
      cast<Instruction>(CorrespondingVal)->getParent();
  return CorrespondingBlock;
}

/// Rewrite the BranchInsts in the incoming blocks to \p PHIBlock that are found
/// in \p Included to branch to BasicBlock \p Replace if they currently branch
/// to the BasicBlock \p Find.  This is used to fix up the incoming basic blocks
/// when PHINodes are included in outlined regions.
///
/// \param PHIBlock - The BasicBlock containing the PHINodes that need to be
/// checked.
/// \param Find - The successor block to be replaced.
/// \param Replace - The new succesor block to branch to.
/// \param Included - The set of blocks about to be outlined.
static void replaceTargetsFromPHINode(BasicBlock *PHIBlock, BasicBlock *Find,
                                      BasicBlock *Replace,
                                      DenseSet<BasicBlock *> &Included) {
  for (PHINode &PN : PHIBlock->phis()) {
    for (unsigned Idx = 0, PNEnd = PN.getNumIncomingValues(); Idx != PNEnd;
         ++Idx) {
      // Check if the incoming block is included in the set of blocks being
      // outlined.
      BasicBlock *Incoming = PN.getIncomingBlock(Idx);
      if (!Included.contains(Incoming))
        continue;

      BranchInst *BI = dyn_cast<BranchInst>(Incoming->getTerminator());
      assert(BI && "Not a branch instruction?");
      // Look over the branching instructions into this block to see if we
      // used to branch to Find in this outlined block.
      for (unsigned Succ = 0, End = BI->getNumSuccessors(); Succ != End;
           Succ++) {
        // If we have found the block to replace, we do so here.
        if (BI->getSuccessor(Succ) != Find)
          continue;
        BI->setSuccessor(Succ, Replace);
      }
    }
  }
}


void OutlinableRegion::splitCandidate() {
  assert(!CandidateSplit && "Candidate already split!");

  Instruction *BackInst = Candidate->backInstruction();

  Instruction *EndInst = nullptr;
  // Check whether the last instruction is a terminator, if it is, we do
  // not split on the following instruction. We leave the block as it is.  We
  // also check that this is not the last instruction in the Module, otherwise
  // the check for whether the current following instruction matches the
  // previously recorded instruction will be incorrect.
  if (!BackInst->isTerminator() ||
      BackInst->getParent() != &BackInst->getFunction()->back()) {
    EndInst = Candidate->end()->Inst;
    assert(EndInst && "Expected an end instruction?");
  }

  // We check if the current instruction following the last instruction in the
  // region is the same as the recorded instruction following the last
  // instruction. If they do not match, there could be problems in rewriting
  // the program after outlining, so we ignore it.
  if (!BackInst->isTerminator() &&
      EndInst != BackInst->getNextNonDebugInstruction())
    return;

  Instruction *StartInst = (*Candidate->begin()).Inst;
  assert(StartInst && "Expected a start instruction?");
  StartBB = StartInst->getParent();
  PrevBB = StartBB;

  DenseSet<BasicBlock *> BBSet;
  Candidate->getBasicBlocks(BBSet);

  // We iterate over the instructions in the region, if we find a PHINode, we
  // check if there are predecessors outside of the region, if there are,
  // we ignore this region since we are unable to handle the severing of the
  // phi node right now.

  // TODO: Handle extraneous inputs for PHINodes through variable number of
  // inputs, similar to how outputs are handled.
  BasicBlock::iterator It = StartInst->getIterator();
  EndBB = BackInst->getParent();
  BasicBlock *IBlock;
  BasicBlock *PHIPredBlock = nullptr;
  bool EndBBTermAndBackInstDifferent = EndBB->getTerminator() != BackInst;
  while (PHINode *PN = dyn_cast<PHINode>(&*It)) {
    unsigned NumPredsOutsideRegion = 0;
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
      if (!BBSet.contains(PN->getIncomingBlock(i))) {
        PHIPredBlock = PN->getIncomingBlock(i);
        ++NumPredsOutsideRegion;
        continue;
      }

      // We must consider the case there the incoming block to the PHINode is
      // the same as the final block of the OutlinableRegion.  If this is the
      // case, the branch from this block must also be outlined to be valid.
      IBlock = PN->getIncomingBlock(i);
      if (IBlock == EndBB && EndBBTermAndBackInstDifferent) {
        PHIPredBlock = PN->getIncomingBlock(i);
        ++NumPredsOutsideRegion;
      }
    }

    if (NumPredsOutsideRegion > 1)
      return;
    
    It++;
  }

  // If the region starts with a PHINode, but is not the initial instruction of
  // the BasicBlock, we ignore this region for now.
  if (isa<PHINode>(StartInst) && StartInst != &*StartBB->begin())
    return;
  
  // If the region ends with a PHINode, but does not contain all of the phi node
  // instructions of the region, we ignore it for now.
  if (isa<PHINode>(BackInst) &&
      BackInst != &*std::prev(EndBB->getFirstInsertionPt()))
    return;

  // The basic block gets split like so:
  // block:                 block:
  //   inst1                  inst1
  //   inst2                  inst2
  //   region1               br block_to_outline
  //   region2              block_to_outline:
  //   region3          ->    region1
  //   region4                region2
  //   inst3                  region3
  //   inst4                  region4
  //                          br block_after_outline
  //                        block_after_outline:
  //                          inst3
  //                          inst4

  std::string OriginalName = PrevBB->getName().str();

  StartBB = PrevBB->splitBasicBlock(StartInst, OriginalName + "_to_outline");
  PrevBB->replaceSuccessorsPhiUsesWith(PrevBB, StartBB);
  // If there was a PHINode with an incoming block outside the region,
  // make sure is correctly updated in the newly split block.
  if (PHIPredBlock)
    PrevBB->replaceSuccessorsPhiUsesWith(PHIPredBlock, PrevBB);

  CandidateSplit = true;
  if (!BackInst->isTerminator()) {
    EndBB = EndInst->getParent();
    FollowBB = EndBB->splitBasicBlock(EndInst, OriginalName + "_after_outline");
    EndBB->replaceSuccessorsPhiUsesWith(EndBB, FollowBB);
    FollowBB->replaceSuccessorsPhiUsesWith(PrevBB, FollowBB);
  } else {
    EndBB = BackInst->getParent();
    EndsInBranch = true;
    FollowBB = nullptr;
  }

  // Refind the basic block set.
  BBSet.clear();
  Candidate->getBasicBlocks(BBSet);
  // For the phi nodes in the new starting basic block of the region, we
  // reassign the targets of the basic blocks branching instructions.
  replaceTargetsFromPHINode(StartBB, PrevBB, StartBB, BBSet);
  if (FollowBB)
    replaceTargetsFromPHINode(FollowBB, EndBB, FollowBB, BBSet);
}

void OutlinableRegion::reattachCandidate() {
  assert(CandidateSplit && "Candidate is not split!");

  // The basic block gets reattached like so:
  // block:                        block:
  //   inst1                         inst1
  //   inst2                         inst2
  //   br block_to_outline           region1
  // block_to_outline:        ->     region2
  //   region1                       region3
  //   region2                       region4
  //   region3                       inst3
  //   region4                       inst4
  //   br block_after_outline
  // block_after_outline:
  //   inst3
  //   inst4
  assert(StartBB != nullptr && "StartBB for Candidate is not defined!");

  assert(PrevBB->getTerminator() && "Terminator removed from PrevBB!");
  // Make sure PHINode references to the block we are merging into are
  // updated to be incoming blocks from the predecessor to the current block.

  // NOTE: If this is updated such that the outlined block can have more than
  // one incoming block to a PHINode, this logic will have to updated
  // to handle multiple precessors instead.

  // We only need to update this if the outlined section contains a PHINode, if
  // it does not, then the incoming block was never changed in the first place.
  // On the other hand, if PrevBB has no predecessors, it means that all
  // incoming blocks to the first block are contained in the region, and there
  // will be nothing to update.
  Instruction *StartInst = (*Candidate->begin()).Inst;
  if (isa<PHINode>(StartInst) && !PrevBB->hasNPredecessors(0)) {
    assert(!PrevBB->hasNPredecessorsOrMore(2) &&
         "PrevBB has more than one predecessor. Should be 0 or 1.");
    BasicBlock *BeforePrevBB = PrevBB->getSinglePredecessor();
    PrevBB->replaceSuccessorsPhiUsesWith(PrevBB, BeforePrevBB);
  }
  PrevBB->getTerminator()->eraseFromParent();

  // If we reattaching after outlining, we iterate over the phi nodes to
  // the initial block, and reassign the branch instructions of the incoming
  // blocks to the block we are remerging into.
  if (!ExtractedFunction) {
    DenseSet<BasicBlock *> BBSet;
    Candidate->getBasicBlocks(BBSet);

    replaceTargetsFromPHINode(StartBB, StartBB, PrevBB, BBSet);
    if (!EndsInBranch)
      replaceTargetsFromPHINode(FollowBB, FollowBB, EndBB, BBSet);
  }

  moveBBContents(*StartBB, *PrevBB);

  BasicBlock *PlacementBB = PrevBB;
  if (StartBB != EndBB)
    PlacementBB = EndBB;
  if (!EndsInBranch && PlacementBB->getUniqueSuccessor() != nullptr) {
    assert(FollowBB != nullptr && "FollowBB for Candidate is not defined!");
    assert(PlacementBB->getTerminator() && "Terminator removed from EndBB!");
    PlacementBB->getTerminator()->eraseFromParent();
    moveBBContents(*FollowBB, *PlacementBB);
    PlacementBB->replaceSuccessorsPhiUsesWith(FollowBB, PlacementBB);
    FollowBB->eraseFromParent();
  }

  PrevBB->replaceSuccessorsPhiUsesWith(StartBB, PrevBB);
  StartBB->eraseFromParent();

  // Make sure to save changes back to the StartBB.
  StartBB = PrevBB;
  EndBB = nullptr;
  PrevBB = nullptr;
  FollowBB = nullptr;

  CandidateSplit = false;
}

/// Find whether \p V matches the Constants previously found for the \p GVN.
///
/// \param V - The value to check for consistency.
/// \param GVN - The global value number assigned to \p V.
/// \param GVNToConstant - The mapping of global value number to Constants.
/// \returns true if the Value matches the Constant mapped to by V and false if
/// it \p V is a Constant but does not match.
/// \returns std::nullopt if \p V is not a Constant.
static std::optional<bool>
constantMatches(Value *V, unsigned GVN,
                DenseMap<unsigned, Constant *> &GVNToConstant) {
  // See if we have a constants
  Constant *CST = dyn_cast<Constant>(V);
  if (!CST)
    return std::nullopt;

  // Holds a mapping from a global value number to a Constant.
  DenseMap<unsigned, Constant *>::iterator GVNToConstantIt;
  bool Inserted;


  // If we have a constant, try to make a new entry in the GVNToConstant.
  std::tie(GVNToConstantIt, Inserted) =
      GVNToConstant.insert(std::make_pair(GVN, CST));
  // If it was found and is not equal, it is not the same. We do not
  // handle this case yet, and exit early.
  if (Inserted || (GVNToConstantIt->second == CST))
    return true;

  return false;
}

InstructionCost OutlinableRegion::getBenefit(TargetTransformInfo &TTI) {
  InstructionCost Benefit = 0;

  // Estimate the benefit of outlining a specific sections of the program.  We
  // delegate mostly this task to the TargetTransformInfo so that if the target
  // has specific changes, we can have a more accurate estimate.

  // However, getInstructionCost delegates the code size calculation for
  // arithmetic instructions to getArithmeticInstrCost in
  // include/Analysis/TargetTransformImpl.h, where it always estimates that the
  // code size for a division and remainder instruction to be equal to 4, and
  // everything else to 1.  This is not an accurate representation of the
  // division instruction for targets that have a native division instruction.
  // To be overly conservative, we only add 1 to the number of instructions for
  // each division instruction.
  for (IRInstructionData &ID : *Candidate) {
    Instruction *I = ID.Inst;
    switch (I->getOpcode()) {
    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::SDiv:
    case Instruction::SRem:
    case Instruction::UDiv:
    case Instruction::URem:
      Benefit += 1;
      break;
    default:
      Benefit += TTI.getInstructionCost(I, TargetTransformInfo::TCK_CodeSize);
      break;
    }
  }

  return Benefit;
}

/// Check the \p OutputMappings structure for value \p Input, if it exists
/// it has been used as an output for outlining, and has been renamed, and we
/// return the new value, otherwise, we return the same value.
///
/// \param OutputMappings [in] - The mapping of values to their renamed value
/// after being used as an output for an outlined region.
/// \param Input [in] - The value to find the remapped value of, if it exists.
/// \return The remapped value if it has been renamed, and the same value if has
/// not.
static Value *findOutputMapping(const DenseMap<Value *, Value *> OutputMappings,
                                Value *Input) {
  DenseMap<Value *, Value *>::const_iterator OutputMapping =
      OutputMappings.find(Input);
  if (OutputMapping != OutputMappings.end())
    return OutputMapping->second;
  return Input;
}

/// Find whether \p Region matches the global value numbering to Constant
/// mapping found so far.
///
/// \param Region - The OutlinableRegion we are checking for constants
/// \param GVNToConstant - The mapping of global value number to Constants.
/// \param NotSame - The set of global value numbers that do not have the same
/// constant in each region.
/// \returns true if all Constants are the same in every use of a Constant in \p
/// Region and false if not
static bool
collectRegionsConstants(OutlinableRegion &Region,
                        DenseMap<unsigned, Constant *> &GVNToConstant,
                        DenseSet<unsigned> &NotSame) {
  bool ConstantsTheSame = true;

  IRSimilarityCandidate &C = *Region.Candidate;
  for (IRInstructionData &ID : C) {

    // Iterate over the operands in an instruction. If the global value number,
    // assigned by the IRSimilarityCandidate, has been seen before, we check if
    // the number has been found to be not the same value in each instance.
    for (Value *V : ID.OperVals) {
      std::optional<unsigned> GVNOpt = C.getGVN(V);
      assert(GVNOpt && "Expected a GVN for operand?");
      unsigned GVN = *GVNOpt;

      // Check if this global value has been found to not be the same already.
      if (NotSame.contains(GVN)) {
        if (isa<Constant>(V))
          ConstantsTheSame = false;
        continue;
      }

      // If it has been the same so far, we check the value for if the
      // associated Constant value match the previous instances of the same
      // global value number.  If the global value does not map to a Constant,
      // it is considered to not be the same value.
      std::optional<bool> ConstantMatches =
          constantMatches(V, GVN, GVNToConstant);
      if (ConstantMatches) {
        if (*ConstantMatches)
          continue;
        else
          ConstantsTheSame = false;
      }

      // While this value is a register, it might not have been previously,
      // make sure we don't already have a constant mapped to this global value
      // number.
      if (GVNToConstant.contains(GVN))
        ConstantsTheSame = false;

      NotSame.insert(GVN);
    }
  }

  return ConstantsTheSame;
}

void OutlinableGroup::findSameConstants(DenseSet<unsigned> &NotSame) {
  DenseMap<unsigned, Constant *> GVNToConstant;

  for (OutlinableRegion *Region : Regions)
    collectRegionsConstants(*Region, GVNToConstant, NotSame);
}

void OutlinableGroup::collectGVNStoreSets(Module &M) {
  for (OutlinableRegion *OS : Regions)
    OutputGVNCombinations.insert(OS->GVNStores);

  // We are adding an extracted argument to decide between which output path
  // to use in the basic block.  It is used in a switch statement and only
  // needs to be an integer.
  if (OutputGVNCombinations.size() > 1)
    ArgumentTypes.push_back(Type::getInt32Ty(M.getContext()));
}

/// Get the subprogram if it exists for one of the outlined regions.
///
/// \param [in] Group - The set of regions to find a subprogram for.
/// \returns the subprogram if it exists, or nullptr.
static DISubprogram *getSubprogramOrNull(OutlinableGroup &Group) {
  for (OutlinableRegion *OS : Group.Regions)
    if (Function *F = OS->Call->getFunction())
      if (DISubprogram *SP = F->getSubprogram())
        return SP;

  return nullptr;
}

Function *IROutliner::createFunction(Module &M, OutlinableGroup &Group,
                                     unsigned FunctionNameSuffix) {
  assert(!Group.OutlinedFunction && "Function is already defined!");

  Type *RetTy = Type::getVoidTy(M.getContext());
  // All extracted functions _should_ have the same return type at this point
  // since the similarity identifier ensures that all branches outside of the
  // region occur in the same place.

  // NOTE: Should we ever move to the model that uses a switch at every point
  // needed, meaning that we could branch within the region or out, it is
  // possible that we will need to switch to using the most general case all of
  // the time.
  for (OutlinableRegion *R : Group.Regions) {
    Type *ExtractedFuncType = R->ExtractedFunction->getReturnType();
    if ((RetTy->isVoidTy() && !ExtractedFuncType->isVoidTy()) ||
        (RetTy->isIntegerTy(1) && ExtractedFuncType->isIntegerTy(16)))
      RetTy = ExtractedFuncType;
  }

  Group.OutlinedFunctionType = FunctionType::get(
      RetTy, Group.ArgumentTypes, false);

  // These functions will only be called from within the same module, so
  // we can set an internal linkage.
  Group.OutlinedFunction = Function::Create(
      Group.OutlinedFunctionType, GlobalValue::InternalLinkage,
      "outlined_ir_func_" + std::to_string(FunctionNameSuffix), M);

  // Transfer the swifterr attribute to the correct function parameter.
  if (Group.SwiftErrorArgument)
    Group.OutlinedFunction->addParamAttr(*Group.SwiftErrorArgument,
                                         Attribute::SwiftError);

  Group.OutlinedFunction->addFnAttr(Attribute::OptimizeForSize);
  Group.OutlinedFunction->addFnAttr(Attribute::MinSize);

  // If there's a DISubprogram associated with this outlined function, then
  // emit debug info for the outlined function.
  if (DISubprogram *SP = getSubprogramOrNull(Group)) {
    Function *F = Group.OutlinedFunction;
    // We have a DISubprogram. Get its DICompileUnit.
    DICompileUnit *CU = SP->getUnit();
    DIBuilder DB(M, true, CU);
    DIFile *Unit = SP->getFile();
    Mangler Mg;
    // Get the mangled name of the function for the linkage name.
    std::string Dummy;
    llvm::raw_string_ostream MangledNameStream(Dummy);
    Mg.getNameWithPrefix(MangledNameStream, F, false);

    DISubprogram *OutlinedSP = DB.createFunction(
        Unit /* Context */, F->getName(), Dummy,
        Unit /* File */,
        0 /* Line 0 is reserved for compiler-generated code. */,
        DB.createSubroutineType(
            DB.getOrCreateTypeArray(std::nullopt)), /* void type */
        0, /* Line 0 is reserved for compiler-generated code. */
        DINode::DIFlags::FlagArtificial /* Compiler-generated code. */,
        /* Outlined code is optimized code by definition. */
        DISubprogram::SPFlagDefinition | DISubprogram::SPFlagOptimized);

    // Don't add any new variables to the subprogram.
    DB.finalizeSubprogram(OutlinedSP);

    // Attach subprogram to the function.
    F->setSubprogram(OutlinedSP);
    // We're done with the DIBuilder.
    DB.finalize();
  }

  return Group.OutlinedFunction;
}

/// Move each BasicBlock in \p Old to \p New.
///
/// \param [in] Old - The function to move the basic blocks from.
/// \param [in] New - The function to move the basic blocks to.
/// \param [out] NewEnds - The return blocks of the new overall function.
static void moveFunctionData(Function &Old, Function &New,
                             DenseMap<Value *, BasicBlock *> &NewEnds) {
  for (BasicBlock &CurrBB : llvm::make_early_inc_range(Old)) {
    CurrBB.removeFromParent();
    CurrBB.insertInto(&New);
    Instruction *I = CurrBB.getTerminator();

    // For each block we find a return instruction is, it is a potential exit
    // path for the function.  We keep track of each block based on the return
    // value here.
    if (ReturnInst *RI = dyn_cast<ReturnInst>(I))
      NewEnds.insert(std::make_pair(RI->getReturnValue(), &CurrBB));

    std::vector<Instruction *> DebugInsts;

    for (Instruction &Val : CurrBB) {
      // Since debug-info originates from many different locations in the
      // program, it will cause incorrect reporting from a debugger if we keep
      // the same debug instructions. Drop non-intrinsic DbgVariableRecords
      // here, collect intrinsics for removal later.
      Val.dropDbgRecords();

      // We must handle the scoping of called functions differently than
      // other outlined instructions.
      if (!isa<CallInst>(&Val)) {
        // Remove the debug information for outlined functions.
        Val.setDebugLoc(DebugLoc());

        // Loop info metadata may contain line locations. Update them to have no
        // value in the new subprogram since the outlined code could be from
        // several locations.
        auto updateLoopInfoLoc = [&New](Metadata *MD) -> Metadata * {
          if (DISubprogram *SP = New.getSubprogram())
            if (auto *Loc = dyn_cast_or_null<DILocation>(MD))
              return DILocation::get(New.getContext(), Loc->getLine(),
                                     Loc->getColumn(), SP, nullptr);
          return MD;
        };
        updateLoopMetadataDebugLocations(Val, updateLoopInfoLoc);
        continue;
      }

      // From this point we are only handling call instructions.
      CallInst *CI = cast<CallInst>(&Val);

      // Collect debug intrinsics for later removal.
      if (isa<DbgInfoIntrinsic>(CI)) {
        DebugInsts.push_back(&Val);
        continue;
      }

      // Edit the scope of called functions inside of outlined functions.
      if (DISubprogram *SP = New.getSubprogram()) {
        DILocation *DI = DILocation::get(New.getContext(), 0, 0, SP);
        Val.setDebugLoc(DI);
      }
    }

    for (Instruction *I : DebugInsts)
      I->eraseFromParent();
  }
}

/// Find the constants that will need to be lifted into arguments
/// as they are not the same in each instance of the region.
///
/// \param [in] C - The IRSimilarityCandidate containing the region we are
/// analyzing.
/// \param [in] NotSame - The set of global value numbers that do not have a
/// single Constant across all OutlinableRegions similar to \p C.
/// \param [out] Inputs - The list containing the global value numbers of the
/// arguments needed for the region of code.
static void findConstants(IRSimilarityCandidate &C, DenseSet<unsigned> &NotSame,
                          std::vector<unsigned> &Inputs) {
  DenseSet<unsigned> Seen;
  // Iterate over the instructions, and find what constants will need to be
  // extracted into arguments.
  for (IRInstructionDataList::iterator IDIt = C.begin(), EndIDIt = C.end();
       IDIt != EndIDIt; IDIt++) {
    for (Value *V : (*IDIt).OperVals) {
      // Since these are stored before any outlining, they will be in the
      // global value numbering.
      unsigned GVN = *C.getGVN(V);
      if (isa<Constant>(V))
        if (NotSame.contains(GVN) && !Seen.contains(GVN)) {
          Inputs.push_back(GVN);
          Seen.insert(GVN);
        }
    }
  }
}

/// Find the GVN for the inputs that have been found by the CodeExtractor.
///
/// \param [in] C - The IRSimilarityCandidate containing the region we are
/// analyzing.
/// \param [in] CurrentInputs - The set of inputs found by the
/// CodeExtractor.
/// \param [in] OutputMappings - The mapping of values that have been replaced
/// by a new output value.
/// \param [out] EndInputNumbers - The global value numbers for the extracted
/// arguments.
static void mapInputsToGVNs(IRSimilarityCandidate &C,
                            SetVector<Value *> &CurrentInputs,
                            const DenseMap<Value *, Value *> &OutputMappings,
                            std::vector<unsigned> &EndInputNumbers) {
  // Get the Global Value Number for each input.  We check if the Value has been
  // replaced by a different value at output, and use the original value before
  // replacement.
  for (Value *Input : CurrentInputs) {
    assert(Input && "Have a nullptr as an input");
    if (OutputMappings.contains(Input))
      Input = OutputMappings.find(Input)->second;
    assert(C.getGVN(Input) && "Could not find a numbering for the given input");
    EndInputNumbers.push_back(*C.getGVN(Input));
  }
}

/// Find the original value for the \p ArgInput values if any one of them was
/// replaced during a previous extraction.
///
/// \param [in] ArgInputs - The inputs to be extracted by the code extractor.
/// \param [in] OutputMappings - The mapping of values that have been replaced
/// by a new output value.
/// \param [out] RemappedArgInputs - The remapped values according to
/// \p OutputMappings that will be extracted.
static void
remapExtractedInputs(const ArrayRef<Value *> ArgInputs,
                     const DenseMap<Value *, Value *> &OutputMappings,
                     SetVector<Value *> &RemappedArgInputs) {
  // Get the global value number for each input that will be extracted as an
  // argument by the code extractor, remapping if needed for reloaded values.
  for (Value *Input : ArgInputs) {
    if (OutputMappings.contains(Input))
      Input = OutputMappings.find(Input)->second;
    RemappedArgInputs.insert(Input);
  }
}

/// Find the input GVNs and the output values for a region of Instructions.
/// Using the code extractor, we collect the inputs to the extracted function.
///
/// The \p Region can be identified as needing to be ignored in this function.
/// It should be checked whether it should be ignored after a call to this
/// function.
///
/// \param [in,out] Region - The region of code to be analyzed.
/// \param [out] InputGVNs - The global value numbers for the extracted
/// arguments.
/// \param [in] NotSame - The global value numbers in the region that do not
/// have the same constant value in the regions structurally similar to
/// \p Region.
/// \param [in] OutputMappings - The mapping of values that have been replaced
/// by a new output value after extraction.
/// \param [out] ArgInputs - The values of the inputs to the extracted function.
/// \param [out] Outputs - The set of values extracted by the CodeExtractor
/// as outputs.
static void getCodeExtractorArguments(
    OutlinableRegion &Region, std::vector<unsigned> &InputGVNs,
    DenseSet<unsigned> &NotSame, DenseMap<Value *, Value *> &OutputMappings,
    SetVector<Value *> &ArgInputs, SetVector<Value *> &Outputs) {
  IRSimilarityCandidate &C = *Region.Candidate;

  // OverallInputs are the inputs to the region found by the CodeExtractor,
  // SinkCands and HoistCands are used by the CodeExtractor to find sunken
  // allocas of values whose lifetimes are contained completely within the
  // outlined region. PremappedInputs are the arguments found by the
  // CodeExtractor, removing conditions such as sunken allocas, but that
  // may need to be remapped due to the extracted output values replacing
  // the original values. We use DummyOutputs for this first run of finding
  // inputs and outputs since the outputs could change during findAllocas,
  // the correct set of extracted outputs will be in the final Outputs ValueSet.
  SetVector<Value *> OverallInputs, PremappedInputs, SinkCands, HoistCands,
      DummyOutputs;

  // Use the code extractor to get the inputs and outputs, without sunken
  // allocas or removing llvm.assumes.
  CodeExtractor *CE = Region.CE;
  CE->findInputsOutputs(OverallInputs, DummyOutputs, SinkCands);
  assert(Region.StartBB && "Region must have a start BasicBlock!");
  Function *OrigF = Region.StartBB->getParent();
  CodeExtractorAnalysisCache CEAC(*OrigF);
  BasicBlock *Dummy = nullptr;

  // The region may be ineligible due to VarArgs in the parent function. In this
  // case we ignore the region.
  if (!CE->isEligible()) {
    Region.IgnoreRegion = true;
    return;
  }

  // Find if any values are going to be sunk into the function when extracted
  CE->findAllocas(CEAC, SinkCands, HoistCands, Dummy);
  CE->findInputsOutputs(PremappedInputs, Outputs, SinkCands);

  // TODO: Support regions with sunken allocas: values whose lifetimes are
  // contained completely within the outlined region.  These are not guaranteed
  // to be the same in every region, so we must elevate them all to arguments
  // when they appear.  If these values are not equal, it means there is some
  // Input in OverallInputs that was removed for ArgInputs.
  if (OverallInputs.size() != PremappedInputs.size()) {
    Region.IgnoreRegion = true;
    return;
  }

  findConstants(C, NotSame, InputGVNs);

  mapInputsToGVNs(C, OverallInputs, OutputMappings, InputGVNs);

  remapExtractedInputs(PremappedInputs.getArrayRef(), OutputMappings,
                       ArgInputs);

  // Sort the GVNs, since we now have constants included in the \ref InputGVNs
  // we need to make sure they are in a deterministic order.
  stable_sort(InputGVNs);
}

/// Look over the inputs and map each input argument to an argument in the
/// overall function for the OutlinableRegions.  This creates a way to replace
/// the arguments of the extracted function with the arguments of the new
/// overall function.
///
/// \param [in,out] Region - The region of code to be analyzed.
/// \param [in] InputGVNs - The global value numbering of the input values
/// collected.
/// \param [in] ArgInputs - The values of the arguments to the extracted
/// function.
static void
findExtractedInputToOverallInputMapping(OutlinableRegion &Region,
                                        std::vector<unsigned> &InputGVNs,
                                        SetVector<Value *> &ArgInputs) {

  IRSimilarityCandidate &C = *Region.Candidate;
  OutlinableGroup &Group = *Region.Parent;

  // This counts the argument number in the overall function.
  unsigned TypeIndex = 0;

  // This counts the argument number in the extracted function.
  unsigned OriginalIndex = 0;

  // Find the mapping of the extracted arguments to the arguments for the
  // overall function. Since there may be extra arguments in the overall
  // function to account for the extracted constants, we have two different
  // counters as we find extracted arguments, and as we come across overall
  // arguments.

  // Additionally, in our first pass, for the first extracted function,
  // we find argument locations for the canonical value numbering.  This
  // numbering overrides any discovered location for the extracted code.
  for (unsigned InputVal : InputGVNs) {
    std::optional<unsigned> CanonicalNumberOpt = C.getCanonicalNum(InputVal);
    assert(CanonicalNumberOpt && "Canonical number not found?");
    unsigned CanonicalNumber = *CanonicalNumberOpt;

    std::optional<Value *> InputOpt = C.fromGVN(InputVal);
    assert(InputOpt && "Global value number not found?");
    Value *Input = *InputOpt;

    DenseMap<unsigned, unsigned>::iterator AggArgIt =
        Group.CanonicalNumberToAggArg.find(CanonicalNumber);

    if (!Group.InputTypesSet) {
      Group.ArgumentTypes.push_back(Input->getType());
      // If the input value has a swifterr attribute, make sure to mark the
      // argument in the overall function.
      if (Input->isSwiftError()) {
        assert(
            !Group.SwiftErrorArgument &&
            "Argument already marked with swifterr for this OutlinableGroup!");
        Group.SwiftErrorArgument = TypeIndex;
      }
    }

    // Check if we have a constant. If we do add it to the overall argument
    // number to Constant map for the region, and continue to the next input.
    if (Constant *CST = dyn_cast<Constant>(Input)) {
      if (AggArgIt != Group.CanonicalNumberToAggArg.end())
        Region.AggArgToConstant.insert(std::make_pair(AggArgIt->second, CST));
      else {
        Group.CanonicalNumberToAggArg.insert(
            std::make_pair(CanonicalNumber, TypeIndex));
        Region.AggArgToConstant.insert(std::make_pair(TypeIndex, CST));
      }
      TypeIndex++;
      continue;
    }

    // It is not a constant, we create the mapping from extracted argument list
    // to the overall argument list, using the canonical location, if it exists.
    assert(ArgInputs.count(Input) && "Input cannot be found!");

    if (AggArgIt != Group.CanonicalNumberToAggArg.end()) {
      if (OriginalIndex != AggArgIt->second)
        Region.ChangedArgOrder = true;
      Region.ExtractedArgToAgg.insert(
          std::make_pair(OriginalIndex, AggArgIt->second));
      Region.AggArgToExtracted.insert(
          std::make_pair(AggArgIt->second, OriginalIndex));
    } else {
      Group.CanonicalNumberToAggArg.insert(
          std::make_pair(CanonicalNumber, TypeIndex));
      Region.ExtractedArgToAgg.insert(std::make_pair(OriginalIndex, TypeIndex));
      Region.AggArgToExtracted.insert(std::make_pair(TypeIndex, OriginalIndex));
    }
    OriginalIndex++;
    TypeIndex++;
  }

  // If the function type definitions for the OutlinableGroup holding the region
  // have not been set, set the length of the inputs here.  We should have the
  // same inputs for all of the different regions contained in the
  // OutlinableGroup since they are all structurally similar to one another.
  if (!Group.InputTypesSet) {
    Group.NumAggregateInputs = TypeIndex;
    Group.InputTypesSet = true;
  }

  Region.NumExtractedInputs = OriginalIndex;
}

/// Check if the \p V has any uses outside of the region other than \p PN.
///
/// \param V [in] - The value to check.
/// \param PHILoc [in] - The location in the PHINode of \p V.
/// \param PN [in] - The PHINode using \p V.
/// \param Exits [in] - The potential blocks we exit to from the outlined
/// region.
/// \param BlocksInRegion [in] - The basic blocks contained in the region.
/// \returns true if \p V has any use soutside its region other than \p PN.
static bool outputHasNonPHI(Value *V, unsigned PHILoc, PHINode &PN,
                            SmallPtrSet<BasicBlock *, 1> &Exits,
                            DenseSet<BasicBlock *> &BlocksInRegion) {
  // We check to see if the value is used by the PHINode from some other
  // predecessor not included in the region.  If it is, we make sure
  // to keep it as an output.
  if (any_of(llvm::seq<unsigned>(0, PN.getNumIncomingValues()),
             [PHILoc, &PN, V, &BlocksInRegion](unsigned Idx) {
               return (Idx != PHILoc && V == PN.getIncomingValue(Idx) &&
                       !BlocksInRegion.contains(PN.getIncomingBlock(Idx)));
             }))
    return true;

  // Check if the value is used by any other instructions outside the region.
  return any_of(V->users(), [&Exits, &BlocksInRegion](User *U) {
    Instruction *I = dyn_cast<Instruction>(U);
    if (!I)
      return false;

    // If the use of the item is inside the region, we skip it.  Uses
    // inside the region give us useful information about how the item could be
    // used as an output.
    BasicBlock *Parent = I->getParent();
    if (BlocksInRegion.contains(Parent))
      return false;

    // If it's not a PHINode then we definitely know the use matters.  This
    // output value will not completely combined with another item in a PHINode
    // as it is directly reference by another non-phi instruction
    if (!isa<PHINode>(I))
      return true;

    // If we have a PHINode outside one of the exit locations, then it
    // can be considered an outside use as well.  If there is a PHINode
    // contained in the Exit where this values use matters, it will be
    // caught when we analyze that PHINode.
    if (!Exits.contains(Parent))
      return true;

    return false;
  });
}

/// Test whether \p CurrentExitFromRegion contains any PhiNodes that should be
/// considered outputs. A PHINodes is an output when more than one incoming
/// value has been marked by the CodeExtractor as an output.
///
/// \param CurrentExitFromRegion [in] - The block to analyze.
/// \param PotentialExitsFromRegion [in] - The potential exit blocks from the
/// region.
/// \param RegionBlocks [in] - The basic blocks in the region.
/// \param Outputs [in, out] - The existing outputs for the region, we may add
/// PHINodes to this as we find that they replace output values.
/// \param OutputsReplacedByPHINode [out] - A set containing outputs that are
/// totally replaced  by a PHINode.
/// \param OutputsWithNonPhiUses [out] - A set containing outputs that are used
/// in PHINodes, but have other uses, and should still be considered outputs.
static void analyzeExitPHIsForOutputUses(
    BasicBlock *CurrentExitFromRegion,
    SmallPtrSet<BasicBlock *, 1> &PotentialExitsFromRegion,
    DenseSet<BasicBlock *> &RegionBlocks, SetVector<Value *> &Outputs,
    DenseSet<Value *> &OutputsReplacedByPHINode,
    DenseSet<Value *> &OutputsWithNonPhiUses) {
  for (PHINode &PN : CurrentExitFromRegion->phis()) {
    // Find all incoming values from the outlining region.
    SmallVector<unsigned, 2> IncomingVals;
    for (unsigned I = 0, E = PN.getNumIncomingValues(); I < E; ++I)
      if (RegionBlocks.contains(PN.getIncomingBlock(I)))
        IncomingVals.push_back(I);

    // Do not process PHI if there are no predecessors from region.
    unsigned NumIncomingVals = IncomingVals.size();
    if (NumIncomingVals == 0)
      continue;

    // If there is one predecessor, we mark it as a value that needs to be kept
    // as an output.
    if (NumIncomingVals == 1) {
      Value *V = PN.getIncomingValue(*IncomingVals.begin());
      OutputsWithNonPhiUses.insert(V);
      OutputsReplacedByPHINode.erase(V);
      continue;
    }

    // This PHINode will be used as an output value, so we add it to our list.
    Outputs.insert(&PN);

    // Not all of the incoming values should be ignored as other inputs and
    // outputs may have uses in outlined region.  If they have other uses
    // outside of the single PHINode we should not skip over it.
    for (unsigned Idx : IncomingVals) {
      Value *V = PN.getIncomingValue(Idx);
      if (outputHasNonPHI(V, Idx, PN, PotentialExitsFromRegion, RegionBlocks)) {
        OutputsWithNonPhiUses.insert(V);
        OutputsReplacedByPHINode.erase(V);
        continue;
      }
      if (!OutputsWithNonPhiUses.contains(V))
        OutputsReplacedByPHINode.insert(V);
    }
  }
}

// Represents the type for the unsigned number denoting the output number for
// phi node, along with the canonical number for the exit block.
using ArgLocWithBBCanon = std::pair<unsigned, unsigned>;
// The list of canonical numbers for the incoming values to a PHINode.
using CanonList = SmallVector<unsigned, 2>;
// The pair type representing the set of canonical values being combined in the
// PHINode, along with the location data for the PHINode.
using PHINodeData = std::pair<ArgLocWithBBCanon, CanonList>;

/// Encode \p PND as an integer for easy lookup based on the argument location,
/// the parent BasicBlock canonical numbering, and the canonical numbering of
/// the values stored in the PHINode.
///
/// \param PND - The data to hash.
/// \returns The hash code of \p PND.
static hash_code encodePHINodeData(PHINodeData &PND) {
  return llvm::hash_combine(
      llvm::hash_value(PND.first.first), llvm::hash_value(PND.first.second),
      llvm::hash_combine_range(PND.second.begin(), PND.second.end()));
}

/// Create a special GVN for PHINodes that will be used outside of
/// the region.  We create a hash code based on the Canonical number of the
/// parent BasicBlock, the canonical numbering of the values stored in the
/// PHINode and the aggregate argument location.  This is used to find whether
/// this PHINode type has been given a canonical numbering already.  If not, we
/// assign it a value and store it for later use.  The value is returned to
/// identify different output schemes for the set of regions.
///
/// \param Region - The region that \p PN is an output for.
/// \param PN - The PHINode we are analyzing.
/// \param Blocks - The blocks for the region we are analyzing.
/// \param AggArgIdx - The argument \p PN will be stored into.
/// \returns An optional holding the assigned canonical number, or std::nullopt
/// if there is some attribute of the PHINode blocking it from being used.
static std::optional<unsigned> getGVNForPHINode(OutlinableRegion &Region,
                                                PHINode *PN,
                                                DenseSet<BasicBlock *> &Blocks,
                                                unsigned AggArgIdx) {
  OutlinableGroup &Group = *Region.Parent;
  IRSimilarityCandidate &Cand = *Region.Candidate;
  BasicBlock *PHIBB = PN->getParent();
  CanonList PHIGVNs;
  Value *Incoming;
  BasicBlock *IncomingBlock;
  for (unsigned Idx = 0, EIdx = PN->getNumIncomingValues(); Idx < EIdx; Idx++) {
    Incoming = PN->getIncomingValue(Idx);
    IncomingBlock = PN->getIncomingBlock(Idx);
    // If we cannot find a GVN, and the incoming block is included in the region
    // this means that the input to the PHINode is not included in the region we
    // are trying to analyze, meaning, that if it was outlined, we would be
    // adding an extra input.  We ignore this case for now, and so ignore the
    // region.
    std::optional<unsigned> OGVN = Cand.getGVN(Incoming);
    if (!OGVN && Blocks.contains(IncomingBlock)) {
      Region.IgnoreRegion = true;
      return std::nullopt;
    }

    // If the incoming block isn't in the region, we don't have to worry about
    // this incoming value.
    if (!Blocks.contains(IncomingBlock))
      continue;

    // Collect the canonical numbers of the values in the PHINode.
    unsigned GVN = *OGVN;
    OGVN = Cand.getCanonicalNum(GVN);
    assert(OGVN && "No GVN found for incoming value?");
    PHIGVNs.push_back(*OGVN);

    // Find the incoming block and use the canonical numbering as well to define
    // the hash for the PHINode.
    OGVN = Cand.getGVN(IncomingBlock);

    // If there is no number for the incoming block, it is because we have
    // split the candidate basic blocks.  So we use the previous block that it
    // was split from to find the valid global value numbering for the PHINode.
    if (!OGVN) {
      assert(Cand.getStartBB() == IncomingBlock &&
             "Unknown basic block used in exit path PHINode.");

      BasicBlock *PrevBlock = nullptr;
      // Iterate over the predecessors to the incoming block of the
      // PHINode, when we find a block that is not contained in the region
      // we know that this is the first block that we split from, and should
      // have a valid global value numbering.
      for (BasicBlock *Pred : predecessors(IncomingBlock))
        if (!Blocks.contains(Pred)) {
          PrevBlock = Pred;
          break;
        }
      assert(PrevBlock && "Expected a predecessor not in the reigon!");
      OGVN = Cand.getGVN(PrevBlock);
    }
    GVN = *OGVN;
    OGVN = Cand.getCanonicalNum(GVN);
    assert(OGVN && "No GVN found for incoming block?");
    PHIGVNs.push_back(*OGVN);
  }

  // Now that we have the GVNs for the incoming values, we are going to combine
  // them with the GVN of the incoming bock, and the output location of the
  // PHINode to generate a hash value representing this instance of the PHINode.
  DenseMap<hash_code, unsigned>::iterator GVNToPHIIt;
  DenseMap<unsigned, PHINodeData>::iterator PHIToGVNIt;
  std::optional<unsigned> BBGVN = Cand.getGVN(PHIBB);
  assert(BBGVN && "Could not find GVN for the incoming block!");

  BBGVN = Cand.getCanonicalNum(*BBGVN);
  assert(BBGVN && "Could not find canonical number for the incoming block!");
  // Create a pair of the exit block canonical value, and the aggregate
  // argument location, connected to the canonical numbers stored in the
  // PHINode.
  PHINodeData TemporaryPair =
      std::make_pair(std::make_pair(*BBGVN, AggArgIdx), PHIGVNs);
  hash_code PHINodeDataHash = encodePHINodeData(TemporaryPair);

  // Look for and create a new entry in our connection between canonical
  // numbers for PHINodes, and the set of objects we just created.
  GVNToPHIIt = Group.GVNsToPHINodeGVN.find(PHINodeDataHash);
  if (GVNToPHIIt == Group.GVNsToPHINodeGVN.end()) {
    bool Inserted = false;
    std::tie(PHIToGVNIt, Inserted) = Group.PHINodeGVNToGVNs.insert(
        std::make_pair(Group.PHINodeGVNTracker, TemporaryPair));
    std::tie(GVNToPHIIt, Inserted) = Group.GVNsToPHINodeGVN.insert(
        std::make_pair(PHINodeDataHash, Group.PHINodeGVNTracker--));
  }

  return GVNToPHIIt->second;
}

/// Create a mapping of the output arguments for the \p Region to the output
/// arguments of the overall outlined function.
///
/// \param [in,out] Region - The region of code to be analyzed.
/// \param [in] Outputs - The values found by the code extractor.
static void
findExtractedOutputToOverallOutputMapping(Module &M, OutlinableRegion &Region,
                                          SetVector<Value *> &Outputs) {
  OutlinableGroup &Group = *Region.Parent;
  IRSimilarityCandidate &C = *Region.Candidate;

  SmallVector<BasicBlock *> BE;
  DenseSet<BasicBlock *> BlocksInRegion;
  C.getBasicBlocks(BlocksInRegion, BE);

  // Find the exits to the region.
  SmallPtrSet<BasicBlock *, 1> Exits;
  for (BasicBlock *Block : BE)
    for (BasicBlock *Succ : successors(Block))
      if (!BlocksInRegion.contains(Succ))
        Exits.insert(Succ);

  // After determining which blocks exit to PHINodes, we add these PHINodes to
  // the set of outputs to be processed.  We also check the incoming values of
  // the PHINodes for whether they should no longer be considered outputs.
  DenseSet<Value *> OutputsReplacedByPHINode;
  DenseSet<Value *> OutputsWithNonPhiUses;
  for (BasicBlock *ExitBB : Exits)
    analyzeExitPHIsForOutputUses(ExitBB, Exits, BlocksInRegion, Outputs,
                                 OutputsReplacedByPHINode,
                                 OutputsWithNonPhiUses);

  // This counts the argument number in the extracted function.
  unsigned OriginalIndex = Region.NumExtractedInputs;

  // This counts the argument number in the overall function.
  unsigned TypeIndex = Group.NumAggregateInputs;
  bool TypeFound;
  DenseSet<unsigned> AggArgsUsed;

  // Iterate over the output types and identify if there is an aggregate pointer
  // type whose base type matches the current output type. If there is, we mark
  // that we will use this output register for this value. If not we add another
  // type to the overall argument type list. We also store the GVNs used for
  // stores to identify which values will need to be moved into an special
  // block that holds the stores to the output registers.
  for (Value *Output : Outputs) {
    TypeFound = false;
    // We can do this since it is a result value, and will have a number
    // that is necessarily the same. BUT if in the future, the instructions
    // do not have to be in same order, but are functionally the same, we will
    // have to use a different scheme, as one-to-one correspondence is not
    // guaranteed.
    unsigned ArgumentSize = Group.ArgumentTypes.size();

    // If the output is combined in a PHINode, we make sure to skip over it.
    if (OutputsReplacedByPHINode.contains(Output))
      continue;

    unsigned AggArgIdx = 0;
    for (unsigned Jdx = TypeIndex; Jdx < ArgumentSize; Jdx++) {
      if (!isa<PointerType>(Group.ArgumentTypes[Jdx]))
        continue;

      if (AggArgsUsed.contains(Jdx))
        continue;

      TypeFound = true;
      AggArgsUsed.insert(Jdx);
      Region.ExtractedArgToAgg.insert(std::make_pair(OriginalIndex, Jdx));
      Region.AggArgToExtracted.insert(std::make_pair(Jdx, OriginalIndex));
      AggArgIdx = Jdx;
      break;
    }

    // We were unable to find an unused type in the output type set that matches
    // the output, so we add a pointer type to the argument types of the overall
    // function to handle this output and create a mapping to it.
    if (!TypeFound) {
      Group.ArgumentTypes.push_back(PointerType::get(Output->getContext(),
          M.getDataLayout().getAllocaAddrSpace()));
      // Mark the new pointer type as the last value in the aggregate argument
      // list.
      unsigned ArgTypeIdx = Group.ArgumentTypes.size() - 1;
      AggArgsUsed.insert(ArgTypeIdx);
      Region.ExtractedArgToAgg.insert(
          std::make_pair(OriginalIndex, ArgTypeIdx));
      Region.AggArgToExtracted.insert(
          std::make_pair(ArgTypeIdx, OriginalIndex));
      AggArgIdx = ArgTypeIdx;
    }

    // TODO: Adapt to the extra input from the PHINode.
    PHINode *PN = dyn_cast<PHINode>(Output);

    std::optional<unsigned> GVN;
    if (PN && !BlocksInRegion.contains(PN->getParent())) {
      // Values outside the region can be combined into PHINode when we
      // have multiple exits. We collect both of these into a list to identify
      // which values are being used in the PHINode. Each list identifies a
      // different PHINode, and a different output. We store the PHINode as it's
      // own canonical value.  These canonical values are also dependent on the
      // output argument it is saved to.

      // If two PHINodes have the same canonical values, but different aggregate
      // argument locations, then they will have distinct Canonical Values.
      GVN = getGVNForPHINode(Region, PN, BlocksInRegion, AggArgIdx);
      if (!GVN)
        return;
    } else {
      // If we do not have a PHINode we use the global value numbering for the
      // output value, to find the canonical number to add to the set of stored
      // values.
      GVN = C.getGVN(Output);
      GVN = C.getCanonicalNum(*GVN);
    }

    // Each region has a potentially unique set of outputs.  We save which
    // values are output in a list of canonical values so we can differentiate
    // among the different store schemes.
    Region.GVNStores.push_back(*GVN);

    OriginalIndex++;
    TypeIndex++;
  }

  // We sort the stored values to make sure that we are not affected by analysis
  // order when determining what combination of items were stored.
  stable_sort(Region.GVNStores);
}

void IROutliner::findAddInputsOutputs(Module &M, OutlinableRegion &Region,
                                      DenseSet<unsigned> &NotSame) {
  std::vector<unsigned> Inputs;
  SetVector<Value *> ArgInputs, Outputs;

  getCodeExtractorArguments(Region, Inputs, NotSame, OutputMappings, ArgInputs,
                            Outputs);

  if (Region.IgnoreRegion)
    return;

  // Map the inputs found by the CodeExtractor to the arguments found for
  // the overall function.
  findExtractedInputToOverallInputMapping(Region, Inputs, ArgInputs);

  // Map the outputs found by the CodeExtractor to the arguments found for
  // the overall function.
  findExtractedOutputToOverallOutputMapping(M, Region, Outputs);
}

/// Replace the extracted function in the Region with a call to the overall
/// function constructed from the deduplicated similar regions, replacing and
/// remapping the values passed to the extracted function as arguments to the
/// new arguments of the overall function.
///
/// \param [in] M - The module to outline from.
/// \param [in] Region - The regions of extracted code to be replaced with a new
/// function.
/// \returns a call instruction with the replaced function.
CallInst *replaceCalledFunction(Module &M, OutlinableRegion &Region) {
  std::vector<Value *> NewCallArgs;
  DenseMap<unsigned, unsigned>::iterator ArgPair;

  OutlinableGroup &Group = *Region.Parent;
  CallInst *Call = Region.Call;
  assert(Call && "Call to replace is nullptr?");
  Function *AggFunc = Group.OutlinedFunction;
  assert(AggFunc && "Function to replace with is nullptr?");

  // If the arguments are the same size, there are not values that need to be
  // made into an argument, the argument ordering has not been change, or
  // different output registers to handle.  We can simply replace the called
  // function in this case.
  if (!Region.ChangedArgOrder && AggFunc->arg_size() == Call->arg_size()) {
    LLVM_DEBUG(dbgs() << "Replace call to " << *Call << " with call to "
                      << *AggFunc << " with same number of arguments\n");
    Call->setCalledFunction(AggFunc);
    return Call;
  }

  // We have a different number of arguments than the new function, so
  // we need to use our previously mappings off extracted argument to overall
  // function argument, and constants to overall function argument to create the
  // new argument list.
  for (unsigned AggArgIdx = 0; AggArgIdx < AggFunc->arg_size(); AggArgIdx++) {

    if (AggArgIdx == AggFunc->arg_size() - 1 &&
        Group.OutputGVNCombinations.size() > 1) {
      // If we are on the last argument, and we need to differentiate between
      // output blocks, add an integer to the argument list to determine
      // what block to take
      LLVM_DEBUG(dbgs() << "Set switch block argument to "
                        << Region.OutputBlockNum << "\n");
      NewCallArgs.push_back(ConstantInt::get(Type::getInt32Ty(M.getContext()),
                                             Region.OutputBlockNum));
      continue;
    }

    ArgPair = Region.AggArgToExtracted.find(AggArgIdx);
    if (ArgPair != Region.AggArgToExtracted.end()) {
      Value *ArgumentValue = Call->getArgOperand(ArgPair->second);
      // If we found the mapping from the extracted function to the overall
      // function, we simply add it to the argument list.  We use the same
      // value, it just needs to honor the new order of arguments.
      LLVM_DEBUG(dbgs() << "Setting argument " << AggArgIdx << " to value "
                        << *ArgumentValue << "\n");
      NewCallArgs.push_back(ArgumentValue);
      continue;
    }

    // If it is a constant, we simply add it to the argument list as a value.
    if (Region.AggArgToConstant.contains(AggArgIdx)) {
      Constant *CST = Region.AggArgToConstant.find(AggArgIdx)->second;
      LLVM_DEBUG(dbgs() << "Setting argument " << AggArgIdx << " to value "
                        << *CST << "\n");
      NewCallArgs.push_back(CST);
      continue;
    }

    // Add a nullptr value if the argument is not found in the extracted
    // function.  If we cannot find a value, it means it is not in use
    // for the region, so we should not pass anything to it.
    LLVM_DEBUG(dbgs() << "Setting argument " << AggArgIdx << " to nullptr\n");
    NewCallArgs.push_back(ConstantPointerNull::get(
        static_cast<PointerType *>(AggFunc->getArg(AggArgIdx)->getType())));
  }

  LLVM_DEBUG(dbgs() << "Replace call to " << *Call << " with call to "
                    << *AggFunc << " with new set of arguments\n");
  // Create the new call instruction and erase the old one.
  Call = CallInst::Create(AggFunc->getFunctionType(), AggFunc, NewCallArgs, "",
                          Call->getIterator());

  // It is possible that the call to the outlined function is either the first
  // instruction is in the new block, the last instruction, or both.  If either
  // of these is the case, we need to make sure that we replace the instruction
  // in the IRInstructionData struct with the new call.
  CallInst *OldCall = Region.Call;
  if (Region.NewFront->Inst == OldCall)
    Region.NewFront->Inst = Call;
  if (Region.NewBack->Inst == OldCall)
    Region.NewBack->Inst = Call;

  // Transfer any debug information.
  Call->setDebugLoc(Region.Call->getDebugLoc());
  // Since our output may determine which branch we go to, we make sure to
  // propogate this new call value through the module.
  OldCall->replaceAllUsesWith(Call);

  // Remove the old instruction.
  OldCall->eraseFromParent();
  Region.Call = Call;

  // Make sure that the argument in the new function has the SwiftError
  // argument.
  if (Group.SwiftErrorArgument)
    Call->addParamAttr(*Group.SwiftErrorArgument, Attribute::SwiftError);

  return Call;
}

/// Find or create a BasicBlock in the outlined function containing PhiBlocks
/// for \p RetVal.
///
/// \param Group - The OutlinableGroup containing the information about the
/// overall outlined function.
/// \param RetVal - The return value or exit option that we are currently
/// evaluating.
/// \returns The found or newly created BasicBlock to contain the needed
/// PHINodes to be used as outputs.
static BasicBlock *findOrCreatePHIBlock(OutlinableGroup &Group, Value *RetVal) {
  DenseMap<Value *, BasicBlock *>::iterator PhiBlockForRetVal,
      ReturnBlockForRetVal;
  PhiBlockForRetVal = Group.PHIBlocks.find(RetVal);
  ReturnBlockForRetVal = Group.EndBBs.find(RetVal);
  assert(ReturnBlockForRetVal != Group.EndBBs.end() &&
         "Could not find output value!");
  BasicBlock *ReturnBB = ReturnBlockForRetVal->second;

  // Find if a PHIBlock exists for this return value already.  If it is
  // the first time we are analyzing this, we will not, so we record it.
  PhiBlockForRetVal = Group.PHIBlocks.find(RetVal);
  if (PhiBlockForRetVal != Group.PHIBlocks.end())
    return PhiBlockForRetVal->second;
  
  // If we did not find a block, we create one, and insert it into the
  // overall function and record it.
  bool Inserted = false;
  BasicBlock *PHIBlock = BasicBlock::Create(ReturnBB->getContext(), "phi_block",
                                            ReturnBB->getParent());
  std::tie(PhiBlockForRetVal, Inserted) =
      Group.PHIBlocks.insert(std::make_pair(RetVal, PHIBlock));

  // We find the predecessors of the return block in the newly created outlined
  // function in order to point them to the new PHIBlock rather than the already
  // existing return block.
  SmallVector<BranchInst *, 2> BranchesToChange;
  for (BasicBlock *Pred : predecessors(ReturnBB))
    BranchesToChange.push_back(cast<BranchInst>(Pred->getTerminator()));

  // Now we mark the branch instructions found, and change the references of the
  // return block to the newly created PHIBlock.
  for (BranchInst *BI : BranchesToChange)
    for (unsigned Succ = 0, End = BI->getNumSuccessors(); Succ < End; Succ++) {
      if (BI->getSuccessor(Succ) != ReturnBB)
        continue;
      BI->setSuccessor(Succ, PHIBlock);
    }

  BranchInst::Create(ReturnBB, PHIBlock);

  return PhiBlockForRetVal->second;
}

/// For the function call now representing the \p Region, find the passed value
/// to that call that represents Argument \p A at the call location if the
/// call has already been replaced with a call to the  overall, aggregate
/// function.
///
/// \param A - The Argument to get the passed value for.
/// \param Region - The extracted Region corresponding to the outlined function.
/// \returns The Value representing \p A at the call site.
static Value *
getPassedArgumentInAlreadyOutlinedFunction(const Argument *A,
                                           const OutlinableRegion &Region) {
  // If we don't need to adjust the argument number at all (since the call
  // has already been replaced by a call to the overall outlined function)
  // we can just get the specified argument.
  return Region.Call->getArgOperand(A->getArgNo());
}

/// For the function call now representing the \p Region, find the passed value
/// to that call that represents Argument \p A at the call location if the
/// call has only been replaced by the call to the aggregate function.
///
/// \param A - The Argument to get the passed value for.
/// \param Region - The extracted Region corresponding to the outlined function.
/// \returns The Value representing \p A at the call site.
static Value *
getPassedArgumentAndAdjustArgumentLocation(const Argument *A,
                                           const OutlinableRegion &Region) {
  unsigned ArgNum = A->getArgNo();
  
  // If it is a constant, we can look at our mapping from when we created
  // the outputs to figure out what the constant value is.
  if (Region.AggArgToConstant.count(ArgNum))
    return Region.AggArgToConstant.find(ArgNum)->second;
  
  // If it is not a constant, and we are not looking at the overall function, we
  // need to adjust which argument we are looking at.
  ArgNum = Region.AggArgToExtracted.find(ArgNum)->second;
  return Region.Call->getArgOperand(ArgNum);
}

/// Find the canonical numbering for the incoming Values into the PHINode \p PN.
///
/// \param PN [in] - The PHINode that we are finding the canonical numbers for.
/// \param Region [in] - The OutlinableRegion containing \p PN.
/// \param OutputMappings [in] - The mapping of output values from outlined
/// region to their original values.
/// \param CanonNums [out] - The canonical numbering for the incoming values to
/// \p PN paired with their incoming block.
/// \param ReplacedWithOutlinedCall - A flag to use the extracted function call
/// of \p Region rather than the overall function's call.
static void findCanonNumsForPHI(
    PHINode *PN, OutlinableRegion &Region,
    const DenseMap<Value *, Value *> &OutputMappings,
    SmallVector<std::pair<unsigned, BasicBlock *>> &CanonNums,
    bool ReplacedWithOutlinedCall = true) {
  // Iterate over the incoming values.
  for (unsigned Idx = 0, EIdx = PN->getNumIncomingValues(); Idx < EIdx; Idx++) {
    Value *IVal = PN->getIncomingValue(Idx);
    BasicBlock *IBlock = PN->getIncomingBlock(Idx);
    // If we have an argument as incoming value, we need to grab the passed
    // value from the call itself.
    if (Argument *A = dyn_cast<Argument>(IVal)) {
      if (ReplacedWithOutlinedCall)
        IVal = getPassedArgumentInAlreadyOutlinedFunction(A, Region);
      else
        IVal = getPassedArgumentAndAdjustArgumentLocation(A, Region);
    }

    // Get the original value if it has been replaced by an output value.
    IVal = findOutputMapping(OutputMappings, IVal);

    // Find and add the canonical number for the incoming value.
    std::optional<unsigned> GVN = Region.Candidate->getGVN(IVal);
    assert(GVN && "No GVN for incoming value");
    std::optional<unsigned> CanonNum = Region.Candidate->getCanonicalNum(*GVN);
    assert(CanonNum && "No Canonical Number for GVN");
    CanonNums.push_back(std::make_pair(*CanonNum, IBlock));
  }
}

/// Find, or add PHINode \p PN to the combined PHINode Block \p OverallPHIBlock
/// in order to condense the number of instructions added to the outlined
/// function.
///
/// \param PN [in] - The PHINode that we are finding the canonical numbers for.
/// \param Region [in] - The OutlinableRegion containing \p PN.
/// \param OverallPhiBlock [in] - The overall PHIBlock we are trying to find
/// \p PN in.
/// \param OutputMappings [in] - The mapping of output values from outlined
/// region to their original values.
/// \param UsedPHIs [in, out] - The PHINodes in the block that have already been
/// matched.
/// \return the newly found or created PHINode in \p OverallPhiBlock.
static PHINode*
findOrCreatePHIInBlock(PHINode &PN, OutlinableRegion &Region,
                       BasicBlock *OverallPhiBlock,
                       const DenseMap<Value *, Value *> &OutputMappings,
                       DenseSet<PHINode *> &UsedPHIs) {
  OutlinableGroup &Group = *Region.Parent;
  
  
  // A list of the canonical numbering assigned to each incoming value, paired
  // with the incoming block for the PHINode passed into this function.
  SmallVector<std::pair<unsigned, BasicBlock *>> PNCanonNums;

  // We have to use the extracted function since we have merged this region into
  // the overall function yet.  We make sure to reassign the argument numbering
  // since it is possible that the argument ordering is different between the
  // functions.
  findCanonNumsForPHI(&PN, Region, OutputMappings, PNCanonNums,
                      /* ReplacedWithOutlinedCall = */ false);

  OutlinableRegion *FirstRegion = Group.Regions[0];

  // A list of the canonical numbering assigned to each incoming value, paired
  // with the incoming block for the PHINode that we are currently comparing
  // the passed PHINode to.
  SmallVector<std::pair<unsigned, BasicBlock *>> CurrentCanonNums;

  // Find the Canonical Numbering for each PHINode, if it matches, we replace
  // the uses of the PHINode we are searching for, with the found PHINode.
  for (PHINode &CurrPN : OverallPhiBlock->phis()) {
    // If this PHINode has already been matched to another PHINode to be merged,
    // we skip it.
    if (UsedPHIs.contains(&CurrPN))
      continue;

    CurrentCanonNums.clear();
    findCanonNumsForPHI(&CurrPN, *FirstRegion, OutputMappings, CurrentCanonNums,
                        /* ReplacedWithOutlinedCall = */ true);

    // If the list of incoming values is not the same length, then they cannot
    // match since there is not an analogue for each incoming value.
    if (PNCanonNums.size() != CurrentCanonNums.size())
      continue;

    bool FoundMatch = true;

    // We compare the canonical value for each incoming value in the passed
    // in PHINode to one already present in the outlined region.  If the
    // incoming values do not match, then the PHINodes do not match.

    // We also check to make sure that the incoming block matches as well by
    // finding the corresponding incoming block in the combined outlined region
    // for the current outlined region.
    for (unsigned Idx = 0, Edx = PNCanonNums.size(); Idx < Edx; ++Idx) {
      std::pair<unsigned, BasicBlock *> ToCompareTo = CurrentCanonNums[Idx];
      std::pair<unsigned, BasicBlock *> ToAdd = PNCanonNums[Idx];
      if (ToCompareTo.first != ToAdd.first) {
        FoundMatch = false;
        break;
      }

      BasicBlock *CorrespondingBlock =
          Region.findCorrespondingBlockIn(*FirstRegion, ToAdd.second);
      assert(CorrespondingBlock && "Found block is nullptr");
      if (CorrespondingBlock != ToCompareTo.second) {
        FoundMatch = false;
        break;
      }
    }

    // If all incoming values and branches matched, then we can merge
    // into the found PHINode.
    if (FoundMatch) {
      UsedPHIs.insert(&CurrPN);
      return &CurrPN;
    }
  }

  // If we've made it here, it means we weren't able to replace the PHINode, so
  // we must insert it ourselves.
  PHINode *NewPN = cast<PHINode>(PN.clone());
  NewPN->insertBefore(&*OverallPhiBlock->begin());
  for (unsigned Idx = 0, Edx = NewPN->getNumIncomingValues(); Idx < Edx;
       Idx++) {
    Value *IncomingVal = NewPN->getIncomingValue(Idx);
    BasicBlock *IncomingBlock = NewPN->getIncomingBlock(Idx);

    // Find corresponding basic block in the overall function for the incoming
    // block.
    BasicBlock *BlockToUse =
        Region.findCorrespondingBlockIn(*FirstRegion, IncomingBlock);
    NewPN->setIncomingBlock(Idx, BlockToUse);

    // If we have an argument we make sure we replace using the argument from
    // the correct function.
    if (Argument *A = dyn_cast<Argument>(IncomingVal)) {
      Value *Val = Group.OutlinedFunction->getArg(A->getArgNo());
      NewPN->setIncomingValue(Idx, Val);
      continue;
    }
    
    // Find the corresponding value in the overall function.
    IncomingVal = findOutputMapping(OutputMappings, IncomingVal);
    Value *Val = Region.findCorrespondingValueIn(*FirstRegion, IncomingVal);
    assert(Val && "Value is nullptr?");
    DenseMap<Value *, Value *>::iterator RemappedIt =
        FirstRegion->RemappedArguments.find(Val);
    if (RemappedIt != FirstRegion->RemappedArguments.end())
      Val = RemappedIt->second;
    NewPN->setIncomingValue(Idx, Val);
  }
  return NewPN;
}

// Within an extracted function, replace the argument uses of the extracted
// region with the arguments of the function for an OutlinableGroup.
//
/// \param [in] Region - The region of extracted code to be changed.
/// \param [in,out] OutputBBs - The BasicBlock for the output stores for this
/// region.
/// \param [in] FirstFunction - A flag to indicate whether we are using this
/// function to define the overall outlined function for all the regions, or
/// if we are operating on one of the following regions.
static void
replaceArgumentUses(OutlinableRegion &Region,
                    DenseMap<Value *, BasicBlock *> &OutputBBs,
                    const DenseMap<Value *, Value *> &OutputMappings,
                    bool FirstFunction = false) {
  OutlinableGroup &Group = *Region.Parent;
  assert(Region.ExtractedFunction && "Region has no extracted function?");

  Function *DominatingFunction = Region.ExtractedFunction;
  if (FirstFunction)
    DominatingFunction = Group.OutlinedFunction;
  DominatorTree DT(*DominatingFunction);
  DenseSet<PHINode *> UsedPHIs;

  for (unsigned ArgIdx = 0; ArgIdx < Region.ExtractedFunction->arg_size();
       ArgIdx++) {
    assert(Region.ExtractedArgToAgg.contains(ArgIdx) &&
           "No mapping from extracted to outlined?");
    unsigned AggArgIdx = Region.ExtractedArgToAgg.find(ArgIdx)->second;
    Argument *AggArg = Group.OutlinedFunction->getArg(AggArgIdx);
    Argument *Arg = Region.ExtractedFunction->getArg(ArgIdx);
    // The argument is an input, so we can simply replace it with the overall
    // argument value
    if (ArgIdx < Region.NumExtractedInputs) {
      LLVM_DEBUG(dbgs() << "Replacing uses of input " << *Arg << " in function "
                        << *Region.ExtractedFunction << " with " << *AggArg
                        << " in function " << *Group.OutlinedFunction << "\n");
      Arg->replaceAllUsesWith(AggArg);
      Value *V = Region.Call->getArgOperand(ArgIdx);
      Region.RemappedArguments.insert(std::make_pair(V, AggArg));
      continue;
    }

    // If we are replacing an output, we place the store value in its own
    // block inside the overall function before replacing the use of the output
    // in the function.
    assert(Arg->hasOneUse() && "Output argument can only have one use");
    User *InstAsUser = Arg->user_back();
    assert(InstAsUser && "User is nullptr!");

    Instruction *I = cast<Instruction>(InstAsUser);
    BasicBlock *BB = I->getParent();
    SmallVector<BasicBlock *, 4> Descendants;
    DT.getDescendants(BB, Descendants);
    bool EdgeAdded = false;
    if (Descendants.size() == 0) {
      EdgeAdded = true;
      DT.insertEdge(&DominatingFunction->getEntryBlock(), BB);
      DT.getDescendants(BB, Descendants);
    }

    // Iterate over the following blocks, looking for return instructions,
    // if we find one, find the corresponding output block for the return value
    // and move our store instruction there.
    for (BasicBlock *DescendBB : Descendants) {
      ReturnInst *RI = dyn_cast<ReturnInst>(DescendBB->getTerminator());
      if (!RI)
        continue;
      Value *RetVal = RI->getReturnValue();
      auto VBBIt = OutputBBs.find(RetVal);
      assert(VBBIt != OutputBBs.end() && "Could not find output value!");

      // If this is storing a PHINode, we must make sure it is included in the
      // overall function.
      StoreInst *SI = cast<StoreInst>(I);

      Value *ValueOperand = SI->getValueOperand();

      StoreInst *NewI = cast<StoreInst>(I->clone());
      NewI->setDebugLoc(DebugLoc());
      BasicBlock *OutputBB = VBBIt->second;
      NewI->insertInto(OutputBB, OutputBB->end());
      LLVM_DEBUG(dbgs() << "Move store for instruction " << *I << " to "
                        << *OutputBB << "\n");

      // If this is storing a PHINode, we must make sure it is included in the
      // overall function.
      if (!isa<PHINode>(ValueOperand) ||
          Region.Candidate->getGVN(ValueOperand).has_value()) {
        if (FirstFunction)
          continue;
        Value *CorrVal =
            Region.findCorrespondingValueIn(*Group.Regions[0], ValueOperand);
        assert(CorrVal && "Value is nullptr?");
        NewI->setOperand(0, CorrVal);
        continue;
      }
      PHINode *PN = cast<PHINode>(SI->getValueOperand());
      // If it has a value, it was not split by the code extractor, which
      // is what we are looking for.
      if (Region.Candidate->getGVN(PN))
        continue;

      // We record the parent block for the PHINode in the Region so that
      // we can exclude it from checks later on.
      Region.PHIBlocks.insert(std::make_pair(RetVal, PN->getParent()));

      // If this is the first function, we do not need to worry about mergiing
      // this with any other block in the overall outlined function, so we can
      // just continue.
      if (FirstFunction) {
        BasicBlock *PHIBlock = PN->getParent();
        Group.PHIBlocks.insert(std::make_pair(RetVal, PHIBlock));
        continue;
      }

      // We look for the aggregate block that contains the PHINodes leading into
      // this exit path. If we can't find one, we create one.
      BasicBlock *OverallPhiBlock = findOrCreatePHIBlock(Group, RetVal);

      // For our PHINode, we find the combined canonical numbering, and
      // attempt to find a matching PHINode in the overall PHIBlock.  If we
      // cannot, we copy the PHINode and move it into this new block.
      PHINode *NewPN = findOrCreatePHIInBlock(*PN, Region, OverallPhiBlock,
                                              OutputMappings, UsedPHIs);
      NewI->setOperand(0, NewPN);
    }

    // If we added an edge for basic blocks without a predecessor, we remove it
    // here.
    if (EdgeAdded)
      DT.deleteEdge(&DominatingFunction->getEntryBlock(), BB);
    I->eraseFromParent();

    LLVM_DEBUG(dbgs() << "Replacing uses of output " << *Arg << " in function "
                      << *Region.ExtractedFunction << " with " << *AggArg
                      << " in function " << *Group.OutlinedFunction << "\n");
    Arg->replaceAllUsesWith(AggArg);
  }
}

/// Within an extracted function, replace the constants that need to be lifted
/// into arguments with the actual argument.
///
/// \param Region [in] - The region of extracted code to be changed.
void replaceConstants(OutlinableRegion &Region) {
  OutlinableGroup &Group = *Region.Parent;
  // Iterate over the constants that need to be elevated into arguments
  for (std::pair<unsigned, Constant *> &Const : Region.AggArgToConstant) {
    unsigned AggArgIdx = Const.first;
    Function *OutlinedFunction = Group.OutlinedFunction;
    assert(OutlinedFunction && "Overall Function is not defined?");
    Constant *CST = Const.second;
    Argument *Arg = Group.OutlinedFunction->getArg(AggArgIdx);
    // Identify the argument it will be elevated to, and replace instances of
    // that constant in the function.

    // TODO: If in the future constants do not have one global value number,
    // i.e. a constant 1 could be mapped to several values, this check will
    // have to be more strict.  It cannot be using only replaceUsesWithIf.

    LLVM_DEBUG(dbgs() << "Replacing uses of constant " << *CST
                      << " in function " << *OutlinedFunction << " with "
                      << *Arg << "\n");
    CST->replaceUsesWithIf(Arg, [OutlinedFunction](Use &U) {
      if (Instruction *I = dyn_cast<Instruction>(U.getUser()))
        return I->getFunction() == OutlinedFunction;
      return false;
    });
  }
}

/// It is possible that there is a basic block that already performs the same
/// stores. This returns a duplicate block, if it exists
///
/// \param OutputBBs [in] the blocks we are looking for a duplicate of.
/// \param OutputStoreBBs [in] The existing output blocks.
/// \returns an optional value with the number output block if there is a match.
std::optional<unsigned> findDuplicateOutputBlock(
    DenseMap<Value *, BasicBlock *> &OutputBBs,
    std::vector<DenseMap<Value *, BasicBlock *>> &OutputStoreBBs) {

  bool Mismatch = false;
  unsigned MatchingNum = 0;
  // We compare the new set output blocks to the other sets of output blocks.
  // If they are the same number, and have identical instructions, they are
  // considered to be the same.
  for (DenseMap<Value *, BasicBlock *> &CompBBs : OutputStoreBBs) {
    Mismatch = false;
    for (std::pair<Value *, BasicBlock *> &VToB : CompBBs) {
      DenseMap<Value *, BasicBlock *>::iterator OutputBBIt =
          OutputBBs.find(VToB.first);
      if (OutputBBIt == OutputBBs.end()) {
        Mismatch = true;
        break;
      }

      BasicBlock *CompBB = VToB.second;
      BasicBlock *OutputBB = OutputBBIt->second;
      if (CompBB->size() - 1 != OutputBB->size()) {
        Mismatch = true;
        break;
      }

      BasicBlock::iterator NIt = OutputBB->begin();
      for (Instruction &I : *CompBB) {
        if (isa<BranchInst>(&I))
          continue;

        if (!I.isIdenticalTo(&(*NIt))) {
          Mismatch = true;
          break;
        }

        NIt++;
      }
    }

    if (!Mismatch)
      return MatchingNum;

    MatchingNum++;
  }

  return std::nullopt;
}

/// Remove empty output blocks from the outlined region.
///
/// \param BlocksToPrune - Mapping of return values output blocks for the \p
/// Region.
/// \param Region - The OutlinableRegion we are analyzing.
static bool
analyzeAndPruneOutputBlocks(DenseMap<Value *, BasicBlock *> &BlocksToPrune,
                            OutlinableRegion &Region) {
  bool AllRemoved = true;
  Value *RetValueForBB;
  BasicBlock *NewBB;
  SmallVector<Value *, 4> ToRemove;
  // Iterate over the output blocks created in the outlined section.
  for (std::pair<Value *, BasicBlock *> &VtoBB : BlocksToPrune) {
    RetValueForBB = VtoBB.first;
    NewBB = VtoBB.second;
  
    // If there are no instructions, we remove it from the module, and also
    // mark the value for removal from the return value to output block mapping.
    if (NewBB->size() == 0) {
      NewBB->eraseFromParent();
      ToRemove.push_back(RetValueForBB);
      continue;
    }
    
    // Mark that we could not remove all the blocks since they were not all
    // empty.
    AllRemoved = false;
  }

  // Remove the return value from the mapping.
  for (Value *V : ToRemove)
    BlocksToPrune.erase(V);

  // Mark the region as having the no output scheme.
  if (AllRemoved)
    Region.OutputBlockNum = -1;
  
  return AllRemoved;
}

/// For the outlined section, move needed the StoreInsts for the output
/// registers into their own block. Then, determine if there is a duplicate
/// output block already created.
///
/// \param [in] OG - The OutlinableGroup of regions to be outlined.
/// \param [in] Region - The OutlinableRegion that is being analyzed.
/// \param [in,out] OutputBBs - the blocks that stores for this region will be
/// placed in.
/// \param [in] EndBBs - the final blocks of the extracted function.
/// \param [in] OutputMappings - OutputMappings the mapping of values that have
/// been replaced by a new output value.
/// \param [in,out] OutputStoreBBs - The existing output blocks.
static void alignOutputBlockWithAggFunc(
    OutlinableGroup &OG, OutlinableRegion &Region,
    DenseMap<Value *, BasicBlock *> &OutputBBs,
    DenseMap<Value *, BasicBlock *> &EndBBs,
    const DenseMap<Value *, Value *> &OutputMappings,
    std::vector<DenseMap<Value *, BasicBlock *>> &OutputStoreBBs) {
  // If none of the output blocks have any instructions, this means that we do
  // not have to determine if it matches any of the other output schemes, and we
  // don't have to do anything else.
  if (analyzeAndPruneOutputBlocks(OutputBBs, Region))
    return;

  // Determine is there is a duplicate set of blocks.
  std::optional<unsigned> MatchingBB =
      findDuplicateOutputBlock(OutputBBs, OutputStoreBBs);

  // If there is, we remove the new output blocks.  If it does not,
  // we add it to our list of sets of output blocks.
  if (MatchingBB) {
    LLVM_DEBUG(dbgs() << "Set output block for region in function"
                      << Region.ExtractedFunction << " to " << *MatchingBB);

    Region.OutputBlockNum = *MatchingBB;
    for (std::pair<Value *, BasicBlock *> &VtoBB : OutputBBs)
      VtoBB.second->eraseFromParent();
    return;
  }

  Region.OutputBlockNum = OutputStoreBBs.size();

  Value *RetValueForBB;
  BasicBlock *NewBB;
  OutputStoreBBs.push_back(DenseMap<Value *, BasicBlock *>());
  for (std::pair<Value *, BasicBlock *> &VtoBB : OutputBBs) {
    RetValueForBB = VtoBB.first;
    NewBB = VtoBB.second;
    DenseMap<Value *, BasicBlock *>::iterator VBBIt =
        EndBBs.find(RetValueForBB);
    LLVM_DEBUG(dbgs() << "Create output block for region in"
                      << Region.ExtractedFunction << " to "
                      << *NewBB);
    BranchInst::Create(VBBIt->second, NewBB);
    OutputStoreBBs.back().insert(std::make_pair(RetValueForBB, NewBB));
  }
}

/// Takes in a mapping, \p OldMap of ConstantValues to BasicBlocks, sorts keys,
/// before creating a basic block for each \p NewMap, and inserting into the new
/// block. Each BasicBlock is named with the scheme "<basename>_<key_idx>".
///
/// \param OldMap [in] - The mapping to base the new mapping off of.
/// \param NewMap [out] - The output mapping using the keys of \p OldMap.
/// \param ParentFunc [in] - The function to put the new basic block in.
/// \param BaseName [in] - The start of the BasicBlock names to be appended to
/// by an index value.
static void createAndInsertBasicBlocks(DenseMap<Value *, BasicBlock *> &OldMap,
                                       DenseMap<Value *, BasicBlock *> &NewMap,
                                       Function *ParentFunc, Twine BaseName) {
  unsigned Idx = 0;
  std::vector<Value *> SortedKeys;
  
  getSortedConstantKeys(SortedKeys, OldMap);

  for (Value *RetVal : SortedKeys) {
    BasicBlock *NewBB = BasicBlock::Create(
        ParentFunc->getContext(),
        Twine(BaseName) + Twine("_") + Twine(static_cast<unsigned>(Idx++)),
        ParentFunc);
    NewMap.insert(std::make_pair(RetVal, NewBB));
  }
}

/// Create the switch statement for outlined function to differentiate between
/// all the output blocks.
///
/// For the outlined section, determine if an outlined block already exists that
/// matches the needed stores for the extracted section.
/// \param [in] M - The module we are outlining from.
/// \param [in] OG - The group of regions to be outlined.
/// \param [in] EndBBs - The final blocks of the extracted function.
/// \param [in,out] OutputStoreBBs - The existing output blocks.
void createSwitchStatement(
    Module &M, OutlinableGroup &OG, DenseMap<Value *, BasicBlock *> &EndBBs,
    std::vector<DenseMap<Value *, BasicBlock *>> &OutputStoreBBs) {
  // We only need the switch statement if there is more than one store
  // combination, or there is more than one set of output blocks.  The first
  // will occur when we store different sets of values for two different
  // regions.  The second will occur when we have two outputs that are combined
  // in a PHINode outside of the region in one outlined instance, and are used
  // seaparately in another. This will create the same set of OutputGVNs, but
  // will generate two different output schemes.
  if (OG.OutputGVNCombinations.size() > 1) {
    Function *AggFunc = OG.OutlinedFunction;
    // Create a final block for each different return block.
    DenseMap<Value *, BasicBlock *> ReturnBBs;
    createAndInsertBasicBlocks(OG.EndBBs, ReturnBBs, AggFunc, "final_block");

    for (std::pair<Value *, BasicBlock *> &RetBlockPair : ReturnBBs) {
      std::pair<Value *, BasicBlock *> &OutputBlock =
          *OG.EndBBs.find(RetBlockPair.first);
      BasicBlock *ReturnBlock = RetBlockPair.second;
      BasicBlock *EndBB = OutputBlock.second;
      Instruction *Term = EndBB->getTerminator();
      // Move the return value to the final block instead of the original exit
      // stub.
      Term->moveBefore(*ReturnBlock, ReturnBlock->end());
      // Put the switch statement in the old end basic block for the function
      // with a fall through to the new return block.
      LLVM_DEBUG(dbgs() << "Create switch statement in " << *AggFunc << " for "
                        << OutputStoreBBs.size() << "\n");
      SwitchInst *SwitchI =
          SwitchInst::Create(AggFunc->getArg(AggFunc->arg_size() - 1),
                             ReturnBlock, OutputStoreBBs.size(), EndBB);

      unsigned Idx = 0;
      for (DenseMap<Value *, BasicBlock *> &OutputStoreBB : OutputStoreBBs) {
        DenseMap<Value *, BasicBlock *>::iterator OSBBIt =
            OutputStoreBB.find(OutputBlock.first);

        if (OSBBIt == OutputStoreBB.end())
          continue;

        BasicBlock *BB = OSBBIt->second;
        SwitchI->addCase(
            ConstantInt::get(Type::getInt32Ty(M.getContext()), Idx), BB);
        Term = BB->getTerminator();
        Term->setSuccessor(0, ReturnBlock);
        Idx++;
      }
    }
    return;
  }

  assert(OutputStoreBBs.size() < 2 && "Different store sets not handled!");

  // If there needs to be stores, move them from the output blocks to their
  // corresponding ending block.  We do not check that the OutputGVNCombinations
  // is equal to 1 here since that could just been the case where there are 0
  // outputs. Instead, we check whether there is more than one set of output
  // blocks since this is the only case where we would have to move the
  // stores, and erase the extraneous blocks.
  if (OutputStoreBBs.size() == 1) {
    LLVM_DEBUG(dbgs() << "Move store instructions to the end block in "
                      << *OG.OutlinedFunction << "\n");
    DenseMap<Value *, BasicBlock *> OutputBlocks = OutputStoreBBs[0];
    for (std::pair<Value *, BasicBlock *> &VBPair : OutputBlocks) {
      DenseMap<Value *, BasicBlock *>::iterator EndBBIt =
          EndBBs.find(VBPair.first);
      assert(EndBBIt != EndBBs.end() && "Could not find end block");
      BasicBlock *EndBB = EndBBIt->second;
      BasicBlock *OutputBB = VBPair.second;
      Instruction *Term = OutputBB->getTerminator();
      Term->eraseFromParent();
      Term = EndBB->getTerminator();
      moveBBContents(*OutputBB, *EndBB);
      Term->moveBefore(*EndBB, EndBB->end());
      OutputBB->eraseFromParent();
    }
  }
}

/// Fill the new function that will serve as the replacement function for all of
/// the extracted regions of a certain structure from the first region in the
/// list of regions.  Replace this first region's extracted function with the
/// new overall function.
///
/// \param [in] M - The module we are outlining from.
/// \param [in] CurrentGroup - The group of regions to be outlined.
/// \param [in,out] OutputStoreBBs - The output blocks for each different
/// set of stores needed for the different functions.
/// \param [in,out] FuncsToRemove - Extracted functions to erase from module
/// once outlining is complete.
/// \param [in] OutputMappings - Extracted functions to erase from module
/// once outlining is complete.
static void fillOverallFunction(
    Module &M, OutlinableGroup &CurrentGroup,
    std::vector<DenseMap<Value *, BasicBlock *>> &OutputStoreBBs,
    std::vector<Function *> &FuncsToRemove,
    const DenseMap<Value *, Value *> &OutputMappings) {
  OutlinableRegion *CurrentOS = CurrentGroup.Regions[0];

  // Move first extracted function's instructions into new function.
  LLVM_DEBUG(dbgs() << "Move instructions from "
                    << *CurrentOS->ExtractedFunction << " to instruction "
                    << *CurrentGroup.OutlinedFunction << "\n");
  moveFunctionData(*CurrentOS->ExtractedFunction,
                   *CurrentGroup.OutlinedFunction, CurrentGroup.EndBBs);

  // Transfer the attributes from the function to the new function.
  for (Attribute A : CurrentOS->ExtractedFunction->getAttributes().getFnAttrs())
    CurrentGroup.OutlinedFunction->addFnAttr(A);

  // Create a new set of output blocks for the first extracted function.
  DenseMap<Value *, BasicBlock *> NewBBs;
  createAndInsertBasicBlocks(CurrentGroup.EndBBs, NewBBs,
                             CurrentGroup.OutlinedFunction, "output_block_0");
  CurrentOS->OutputBlockNum = 0;

  replaceArgumentUses(*CurrentOS, NewBBs, OutputMappings, true);
  replaceConstants(*CurrentOS);

  // We first identify if any output blocks are empty, if they are we remove
  // them. We then create a branch instruction to the basic block to the return
  // block for the function for each non empty output block.
  if (!analyzeAndPruneOutputBlocks(NewBBs, *CurrentOS)) {
    OutputStoreBBs.push_back(DenseMap<Value *, BasicBlock *>());
    for (std::pair<Value *, BasicBlock *> &VToBB : NewBBs) {
      DenseMap<Value *, BasicBlock *>::iterator VBBIt =
          CurrentGroup.EndBBs.find(VToBB.first);
      BasicBlock *EndBB = VBBIt->second;
      BranchInst::Create(EndBB, VToBB.second);
      OutputStoreBBs.back().insert(VToBB);
    }
  }

  // Replace the call to the extracted function with the outlined function.
  CurrentOS->Call = replaceCalledFunction(M, *CurrentOS);

  // We only delete the extracted functions at the end since we may need to
  // reference instructions contained in them for mapping purposes.
  FuncsToRemove.push_back(CurrentOS->ExtractedFunction);
}

void IROutliner::deduplicateExtractedSections(
    Module &M, OutlinableGroup &CurrentGroup,
    std::vector<Function *> &FuncsToRemove, unsigned &OutlinedFunctionNum) {
  createFunction(M, CurrentGroup, OutlinedFunctionNum);

  std::vector<DenseMap<Value *, BasicBlock *>> OutputStoreBBs;

  OutlinableRegion *CurrentOS;

  fillOverallFunction(M, CurrentGroup, OutputStoreBBs, FuncsToRemove,
                      OutputMappings);

  std::vector<Value *> SortedKeys;
  for (unsigned Idx = 1; Idx < CurrentGroup.Regions.size(); Idx++) {
    CurrentOS = CurrentGroup.Regions[Idx];
    AttributeFuncs::mergeAttributesForOutlining(*CurrentGroup.OutlinedFunction,
                                               *CurrentOS->ExtractedFunction);

    // Create a set of BasicBlocks, one for each return block, to hold the
    // needed store instructions.
    DenseMap<Value *, BasicBlock *> NewBBs;
    createAndInsertBasicBlocks(
        CurrentGroup.EndBBs, NewBBs, CurrentGroup.OutlinedFunction,
        "output_block_" + Twine(static_cast<unsigned>(Idx)));
    replaceArgumentUses(*CurrentOS, NewBBs, OutputMappings);
    alignOutputBlockWithAggFunc(CurrentGroup, *CurrentOS, NewBBs,
                                CurrentGroup.EndBBs, OutputMappings,
                                OutputStoreBBs);

    CurrentOS->Call = replaceCalledFunction(M, *CurrentOS);
    FuncsToRemove.push_back(CurrentOS->ExtractedFunction);
  }

  // Create a switch statement to handle the different output schemes.
  createSwitchStatement(M, CurrentGroup, CurrentGroup.EndBBs, OutputStoreBBs);

  OutlinedFunctionNum++;
}

/// Checks that the next instruction in the InstructionDataList matches the
/// next instruction in the module.  If they do not, there could be the
/// possibility that extra code has been inserted, and we must ignore it.
///
/// \param ID - The IRInstructionData to check the next instruction of.
/// \returns true if the InstructionDataList and actual instruction match.
static bool nextIRInstructionDataMatchesNextInst(IRInstructionData &ID) {
  // We check if there is a discrepancy between the InstructionDataList
  // and the actual next instruction in the module.  If there is, it means
  // that an extra instruction was added, likely by the CodeExtractor.

  // Since we do not have any similarity data about this particular
  // instruction, we cannot confidently outline it, and must discard this
  // candidate.
  IRInstructionDataList::iterator NextIDIt = std::next(ID.getIterator());
  Instruction *NextIDLInst = NextIDIt->Inst;
  Instruction *NextModuleInst = nullptr;
  if (!ID.Inst->isTerminator())
    NextModuleInst = ID.Inst->getNextNonDebugInstruction();
  else if (NextIDLInst != nullptr)
    NextModuleInst =
        &*NextIDIt->Inst->getParent()->instructionsWithoutDebug().begin();

  if (NextIDLInst && NextIDLInst != NextModuleInst)
    return false;

  return true;
}

bool IROutliner::isCompatibleWithAlreadyOutlinedCode(
    const OutlinableRegion &Region) {
  IRSimilarityCandidate *IRSC = Region.Candidate;
  unsigned StartIdx = IRSC->getStartIdx();
  unsigned EndIdx = IRSC->getEndIdx();

  // A check to make sure that we are not about to attempt to outline something
  // that has already been outlined.
  for (unsigned Idx = StartIdx; Idx <= EndIdx; Idx++)
    if (Outlined.contains(Idx))
      return false;

  // We check if the recorded instruction matches the actual next instruction,
  // if it does not, we fix it in the InstructionDataList.
  if (!Region.Candidate->backInstruction()->isTerminator()) {
    Instruction *NewEndInst =
        Region.Candidate->backInstruction()->getNextNonDebugInstruction();
    assert(NewEndInst && "Next instruction is a nullptr?");
    if (Region.Candidate->end()->Inst != NewEndInst) {
      IRInstructionDataList *IDL = Region.Candidate->front()->IDL;
      IRInstructionData *NewEndIRID = new (InstDataAllocator.Allocate())
          IRInstructionData(*NewEndInst,
                            InstructionClassifier.visit(*NewEndInst), *IDL);

      // Insert the first IRInstructionData of the new region after the
      // last IRInstructionData of the IRSimilarityCandidate.
      IDL->insert(Region.Candidate->end(), *NewEndIRID);
    }
  }

  return none_of(*IRSC, [this](IRInstructionData &ID) {
    if (!nextIRInstructionDataMatchesNextInst(ID))
      return true;

    return !this->InstructionClassifier.visit(ID.Inst);
  });
}

void IROutliner::pruneIncompatibleRegions(
    std::vector<IRSimilarityCandidate> &CandidateVec,
    OutlinableGroup &CurrentGroup) {
  bool PreviouslyOutlined;

  // Sort from beginning to end, so the IRSimilarityCandidates are in order.
  stable_sort(CandidateVec, [](const IRSimilarityCandidate &LHS,
                               const IRSimilarityCandidate &RHS) {
    return LHS.getStartIdx() < RHS.getStartIdx();
  });

  IRSimilarityCandidate &FirstCandidate = CandidateVec[0];
  // Since outlining a call and a branch instruction will be the same as only
  // outlinining a call instruction, we ignore it as a space saving.
  if (FirstCandidate.getLength() == 2) {
    if (isa<CallInst>(FirstCandidate.front()->Inst) &&
        isa<BranchInst>(FirstCandidate.back()->Inst))
      return;
  }

  unsigned CurrentEndIdx = 0;
  for (IRSimilarityCandidate &IRSC : CandidateVec) {
    PreviouslyOutlined = false;
    unsigned StartIdx = IRSC.getStartIdx();
    unsigned EndIdx = IRSC.getEndIdx();
    const Function &FnForCurrCand = *IRSC.getFunction();

    for (unsigned Idx = StartIdx; Idx <= EndIdx; Idx++)
      if (Outlined.contains(Idx)) {
        PreviouslyOutlined = true;
        break;
      }

    if (PreviouslyOutlined)
      continue;

    // Check over the instructions, and if the basic block has its address
    // taken for use somewhere else, we do not outline that block.
    bool BBHasAddressTaken = any_of(IRSC, [](IRInstructionData &ID){
      return ID.Inst->getParent()->hasAddressTaken();
    });

    if (BBHasAddressTaken)
      continue;

    if (FnForCurrCand.hasOptNone())
      continue;

    if (FnForCurrCand.hasFnAttribute("nooutline")) {
      LLVM_DEBUG({
        dbgs() << "... Skipping function with nooutline attribute: "
               << FnForCurrCand.getName() << "\n";
      });
      continue;
    }

    if (IRSC.front()->Inst->getFunction()->hasLinkOnceODRLinkage() &&
        !OutlineFromLinkODRs)
      continue;

    // Greedily prune out any regions that will overlap with already chosen
    // regions.
    if (CurrentEndIdx != 0 && StartIdx <= CurrentEndIdx)
      continue;

    bool BadInst = any_of(IRSC, [this](IRInstructionData &ID) {
      if (!nextIRInstructionDataMatchesNextInst(ID))
        return true;

      return !this->InstructionClassifier.visit(ID.Inst);
    });

    if (BadInst)
      continue;

    OutlinableRegion *OS = new (RegionAllocator.Allocate())
        OutlinableRegion(IRSC, CurrentGroup);
    CurrentGroup.Regions.push_back(OS);

    CurrentEndIdx = EndIdx;
  }
}

InstructionCost
IROutliner::findBenefitFromAllRegions(OutlinableGroup &CurrentGroup) {
  InstructionCost RegionBenefit = 0;
  for (OutlinableRegion *Region : CurrentGroup.Regions) {
    TargetTransformInfo &TTI = getTTI(*Region->StartBB->getParent());
    // We add the number of instructions in the region to the benefit as an
    // estimate as to how much will be removed.
    RegionBenefit += Region->getBenefit(TTI);
    LLVM_DEBUG(dbgs() << "Adding: " << RegionBenefit
                      << " saved instructions to overfall benefit.\n");
  }

  return RegionBenefit;
}

/// For the \p OutputCanon number passed in find the value represented by this
/// canonical number. If it is from a PHINode, we pick the first incoming
/// value and return that Value instead.
///
/// \param Region - The OutlinableRegion to get the Value from.
/// \param OutputCanon - The canonical number to find the Value from.
/// \returns The Value represented by a canonical number \p OutputCanon in \p
/// Region.
static Value *findOutputValueInRegion(OutlinableRegion &Region,
                                      unsigned OutputCanon) {
  OutlinableGroup &CurrentGroup = *Region.Parent;
  // If the value is greater than the value in the tracker, we have a
  // PHINode and will instead use one of the incoming values to find the
  // type.
  if (OutputCanon > CurrentGroup.PHINodeGVNTracker) {
    auto It = CurrentGroup.PHINodeGVNToGVNs.find(OutputCanon);
    assert(It != CurrentGroup.PHINodeGVNToGVNs.end() &&
           "Could not find GVN set for PHINode number!");
    assert(It->second.second.size() > 0 && "PHINode does not have any values!");
    OutputCanon = *It->second.second.begin();
  }
  std::optional<unsigned> OGVN =
      Region.Candidate->fromCanonicalNum(OutputCanon);
  assert(OGVN && "Could not find GVN for Canonical Number?");
  std::optional<Value *> OV = Region.Candidate->fromGVN(*OGVN);
  assert(OV && "Could not find value for GVN?");
  return *OV;
}

InstructionCost
IROutliner::findCostOutputReloads(OutlinableGroup &CurrentGroup) {
  InstructionCost OverallCost = 0;
  for (OutlinableRegion *Region : CurrentGroup.Regions) {
    TargetTransformInfo &TTI = getTTI(*Region->StartBB->getParent());

    // Each output incurs a load after the call, so we add that to the cost.
    for (unsigned OutputCanon : Region->GVNStores) {
      Value *V = findOutputValueInRegion(*Region, OutputCanon);
      InstructionCost LoadCost =
          TTI.getMemoryOpCost(Instruction::Load, V->getType(), Align(1), 0,
                              TargetTransformInfo::TCK_CodeSize);

      LLVM_DEBUG(dbgs() << "Adding: " << LoadCost
                        << " instructions to cost for output of type "
                        << *V->getType() << "\n");
      OverallCost += LoadCost;
    }
  }

  return OverallCost;
}

/// Find the extra instructions needed to handle any output values for the
/// region.
///
/// \param [in] M - The Module to outline from.
/// \param [in] CurrentGroup - The collection of OutlinableRegions to analyze.
/// \param [in] TTI - The TargetTransformInfo used to collect information for
/// new instruction costs.
/// \returns the additional cost to handle the outputs.
static InstructionCost findCostForOutputBlocks(Module &M,
                                               OutlinableGroup &CurrentGroup,
                                               TargetTransformInfo &TTI) {
  InstructionCost OutputCost = 0;
  unsigned NumOutputBranches = 0;

  OutlinableRegion &FirstRegion = *CurrentGroup.Regions[0];
  IRSimilarityCandidate &Candidate = *CurrentGroup.Regions[0]->Candidate;
  DenseSet<BasicBlock *> CandidateBlocks;
  Candidate.getBasicBlocks(CandidateBlocks);

  // Count the number of different output branches that point to blocks outside
  // of the region.
  DenseSet<BasicBlock *> FoundBlocks;
  for (IRInstructionData &ID : Candidate) {
    if (!isa<BranchInst>(ID.Inst))
      continue;

    for (Value *V : ID.OperVals) {
      BasicBlock *BB = static_cast<BasicBlock *>(V);
      if (!CandidateBlocks.contains(BB) && FoundBlocks.insert(BB).second)
        NumOutputBranches++;
    }
  }

  CurrentGroup.BranchesToOutside = NumOutputBranches;

  for (const ArrayRef<unsigned> &OutputUse :
       CurrentGroup.OutputGVNCombinations) {
    for (unsigned OutputCanon : OutputUse) {
      Value *V = findOutputValueInRegion(FirstRegion, OutputCanon);
      InstructionCost StoreCost =
          TTI.getMemoryOpCost(Instruction::Load, V->getType(), Align(1), 0,
                              TargetTransformInfo::TCK_CodeSize);

      // An instruction cost is added for each store set that needs to occur for
      // various output combinations inside the function, plus a branch to
      // return to the exit block.
      LLVM_DEBUG(dbgs() << "Adding: " << StoreCost
                        << " instructions to cost for output of type "
                        << *V->getType() << "\n");
      OutputCost += StoreCost * NumOutputBranches;
    }

    InstructionCost BranchCost =
        TTI.getCFInstrCost(Instruction::Br, TargetTransformInfo::TCK_CodeSize);
    LLVM_DEBUG(dbgs() << "Adding " << BranchCost << " to the current cost for"
                      << " a branch instruction\n");
    OutputCost += BranchCost * NumOutputBranches;
  }

  // If there is more than one output scheme, we must have a comparison and
  // branch for each different item in the switch statement.
  if (CurrentGroup.OutputGVNCombinations.size() > 1) {
    InstructionCost ComparisonCost = TTI.getCmpSelInstrCost(
        Instruction::ICmp, Type::getInt32Ty(M.getContext()),
        Type::getInt32Ty(M.getContext()), CmpInst::BAD_ICMP_PREDICATE,
        TargetTransformInfo::TCK_CodeSize);
    InstructionCost BranchCost =
        TTI.getCFInstrCost(Instruction::Br, TargetTransformInfo::TCK_CodeSize);

    unsigned DifferentBlocks = CurrentGroup.OutputGVNCombinations.size();
    InstructionCost TotalCost = ComparisonCost * BranchCost * DifferentBlocks;

    LLVM_DEBUG(dbgs() << "Adding: " << TotalCost
                      << " instructions for each switch case for each different"
                      << " output path in a function\n");
    OutputCost += TotalCost * NumOutputBranches;
  }

  return OutputCost;
}

void IROutliner::findCostBenefit(Module &M, OutlinableGroup &CurrentGroup) {
  InstructionCost RegionBenefit = findBenefitFromAllRegions(CurrentGroup);
  CurrentGroup.Benefit += RegionBenefit;
  LLVM_DEBUG(dbgs() << "Current Benefit: " << CurrentGroup.Benefit << "\n");

  InstructionCost OutputReloadCost = findCostOutputReloads(CurrentGroup);
  CurrentGroup.Cost += OutputReloadCost;
  LLVM_DEBUG(dbgs() << "Current Cost: " << CurrentGroup.Cost << "\n");

  InstructionCost AverageRegionBenefit =
      RegionBenefit / CurrentGroup.Regions.size();
  unsigned OverallArgumentNum = CurrentGroup.ArgumentTypes.size();
  unsigned NumRegions = CurrentGroup.Regions.size();
  TargetTransformInfo &TTI =
      getTTI(*CurrentGroup.Regions[0]->Candidate->getFunction());

  // We add one region to the cost once, to account for the instructions added
  // inside of the newly created function.
  LLVM_DEBUG(dbgs() << "Adding: " << AverageRegionBenefit
                    << " instructions to cost for body of new function.\n");
  CurrentGroup.Cost += AverageRegionBenefit;
  LLVM_DEBUG(dbgs() << "Current Cost: " << CurrentGroup.Cost << "\n");

  // For each argument, we must add an instruction for loading the argument
  // out of the register and into a value inside of the newly outlined function.
  LLVM_DEBUG(dbgs() << "Adding: " << OverallArgumentNum
                    << " instructions to cost for each argument in the new"
                    << " function.\n");
  CurrentGroup.Cost +=
      OverallArgumentNum * TargetTransformInfo::TCC_Basic;
  LLVM_DEBUG(dbgs() << "Current Cost: " << CurrentGroup.Cost << "\n");

  // Each argument needs to either be loaded into a register or onto the stack.
  // Some arguments will only be loaded into the stack once the argument
  // registers are filled.
  LLVM_DEBUG(dbgs() << "Adding: " << OverallArgumentNum
                    << " instructions to cost for each argument in the new"
                    << " function " << NumRegions << " times for the "
                    << "needed argument handling at the call site.\n");
  CurrentGroup.Cost +=
      2 * OverallArgumentNum * TargetTransformInfo::TCC_Basic * NumRegions;
  LLVM_DEBUG(dbgs() << "Current Cost: " << CurrentGroup.Cost << "\n");

  CurrentGroup.Cost += findCostForOutputBlocks(M, CurrentGroup, TTI);
  LLVM_DEBUG(dbgs() << "Current Cost: " << CurrentGroup.Cost << "\n");
}

void IROutliner::updateOutputMapping(OutlinableRegion &Region,
                                     ArrayRef<Value *> Outputs,
                                     LoadInst *LI) {
  // For and load instructions following the call
  Value *Operand = LI->getPointerOperand();
  std::optional<unsigned> OutputIdx;
  // Find if the operand it is an output register.
  for (unsigned ArgIdx = Region.NumExtractedInputs;
       ArgIdx < Region.Call->arg_size(); ArgIdx++) {
    if (Operand == Region.Call->getArgOperand(ArgIdx)) {
      OutputIdx = ArgIdx - Region.NumExtractedInputs;
      break;
    }
  }

  // If we found an output register, place a mapping of the new value
  // to the original in the mapping.
  if (!OutputIdx)
    return;

  if (!OutputMappings.contains(Outputs[*OutputIdx])) {
    LLVM_DEBUG(dbgs() << "Mapping extracted output " << *LI << " to "
                      << *Outputs[*OutputIdx] << "\n");
    OutputMappings.insert(std::make_pair(LI, Outputs[*OutputIdx]));
  } else {
    Value *Orig = OutputMappings.find(Outputs[*OutputIdx])->second;
    LLVM_DEBUG(dbgs() << "Mapping extracted output " << *Orig << " to "
                      << *Outputs[*OutputIdx] << "\n");
    OutputMappings.insert(std::make_pair(LI, Orig));
  }
}

bool IROutliner::extractSection(OutlinableRegion &Region) {
  SetVector<Value *> ArgInputs, Outputs, SinkCands;
  assert(Region.StartBB && "StartBB for the OutlinableRegion is nullptr!");
  BasicBlock *InitialStart = Region.StartBB;
  Function *OrigF = Region.StartBB->getParent();
  CodeExtractorAnalysisCache CEAC(*OrigF);
  Region.ExtractedFunction =
      Region.CE->extractCodeRegion(CEAC, ArgInputs, Outputs);

  // If the extraction was successful, find the BasicBlock, and reassign the
  // OutlinableRegion blocks
  if (!Region.ExtractedFunction) {
    LLVM_DEBUG(dbgs() << "CodeExtractor failed to outline " << Region.StartBB
                      << "\n");
    Region.reattachCandidate();
    return false;
  }

  // Get the block containing the called branch, and reassign the blocks as
  // necessary.  If the original block still exists, it is because we ended on
  // a branch instruction, and so we move the contents into the block before
  // and assign the previous block correctly.
  User *InstAsUser = Region.ExtractedFunction->user_back();
  BasicBlock *RewrittenBB = cast<Instruction>(InstAsUser)->getParent();
  Region.PrevBB = RewrittenBB->getSinglePredecessor();
  assert(Region.PrevBB && "PrevBB is nullptr?");
  if (Region.PrevBB == InitialStart) {
    BasicBlock *NewPrev = InitialStart->getSinglePredecessor();
    Instruction *BI = NewPrev->getTerminator();
    BI->eraseFromParent();
    moveBBContents(*InitialStart, *NewPrev);
    Region.PrevBB = NewPrev;
    InitialStart->eraseFromParent();
  }

  Region.StartBB = RewrittenBB;
  Region.EndBB = RewrittenBB;

  // The sequences of outlinable regions has now changed.  We must fix the
  // IRInstructionDataList for consistency.  Although they may not be illegal
  // instructions, they should not be compared with anything else as they
  // should not be outlined in this round.  So marking these as illegal is
  // allowed.
  IRInstructionDataList *IDL = Region.Candidate->front()->IDL;
  Instruction *BeginRewritten = &*RewrittenBB->begin();
  Instruction *EndRewritten = &*RewrittenBB->begin();
  Region.NewFront = new (InstDataAllocator.Allocate()) IRInstructionData(
      *BeginRewritten, InstructionClassifier.visit(*BeginRewritten), *IDL);
  Region.NewBack = new (InstDataAllocator.Allocate()) IRInstructionData(
      *EndRewritten, InstructionClassifier.visit(*EndRewritten), *IDL);

  // Insert the first IRInstructionData of the new region in front of the
  // first IRInstructionData of the IRSimilarityCandidate.
  IDL->insert(Region.Candidate->begin(), *Region.NewFront);
  // Insert the first IRInstructionData of the new region after the
  // last IRInstructionData of the IRSimilarityCandidate.
  IDL->insert(Region.Candidate->end(), *Region.NewBack);
  // Remove the IRInstructionData from the IRSimilarityCandidate.
  IDL->erase(Region.Candidate->begin(), std::prev(Region.Candidate->end()));

  assert(RewrittenBB != nullptr &&
         "Could not find a predecessor after extraction!");

  // Iterate over the new set of instructions to find the new call
  // instruction.
  for (Instruction &I : *RewrittenBB)
    if (CallInst *CI = dyn_cast<CallInst>(&I)) {
      if (Region.ExtractedFunction == CI->getCalledFunction())
        Region.Call = CI;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(&I))
      updateOutputMapping(Region, Outputs.getArrayRef(), LI);
  Region.reattachCandidate();
  return true;
}

unsigned IROutliner::doOutline(Module &M) {
  // Find the possible similarity sections.
  InstructionClassifier.EnableBranches = !DisableBranches;
  InstructionClassifier.EnableIndirectCalls = !DisableIndirectCalls;
  InstructionClassifier.EnableIntrinsics = !DisableIntrinsics;

  IRSimilarityIdentifier &Identifier = getIRSI(M);
  SimilarityGroupList &SimilarityCandidates = *Identifier.getSimilarity();

  // Sort them by size of extracted sections
  unsigned OutlinedFunctionNum = 0;
  // If we only have one SimilarityGroup in SimilarityCandidates, we do not have
  // to sort them by the potential number of instructions to be outlined
  if (SimilarityCandidates.size() > 1)
    llvm::stable_sort(SimilarityCandidates,
                      [](const std::vector<IRSimilarityCandidate> &LHS,
                         const std::vector<IRSimilarityCandidate> &RHS) {
                        return LHS[0].getLength() * LHS.size() >
                               RHS[0].getLength() * RHS.size();
                      });
  // Creating OutlinableGroups for each SimilarityCandidate to be used in
  // each of the following for loops to avoid making an allocator.
  std::vector<OutlinableGroup> PotentialGroups(SimilarityCandidates.size());

  DenseSet<unsigned> NotSame;
  std::vector<OutlinableGroup *> NegativeCostGroups;
  std::vector<OutlinableRegion *> OutlinedRegions;
  // Iterate over the possible sets of similarity.
  unsigned PotentialGroupIdx = 0;
  for (SimilarityGroup &CandidateVec : SimilarityCandidates) {
    OutlinableGroup &CurrentGroup = PotentialGroups[PotentialGroupIdx++];

    // Remove entries that were previously outlined
    pruneIncompatibleRegions(CandidateVec, CurrentGroup);

    // We pruned the number of regions to 0 to 1, meaning that it's not worth
    // trying to outlined since there is no compatible similar instance of this
    // code.
    if (CurrentGroup.Regions.size() < 2)
      continue;

    // Determine if there are any values that are the same constant throughout
    // each section in the set.
    NotSame.clear();
    CurrentGroup.findSameConstants(NotSame);

    if (CurrentGroup.IgnoreGroup)
      continue;

    // Create a CodeExtractor for each outlinable region. Identify inputs and
    // outputs for each section using the code extractor and create the argument
    // types for the Aggregate Outlining Function.
    OutlinedRegions.clear();
    for (OutlinableRegion *OS : CurrentGroup.Regions) {
      // Break the outlinable region out of its parent BasicBlock into its own
      // BasicBlocks (see function implementation).
      OS->splitCandidate();

      // There's a chance that when the region is split, extra instructions are
      // added to the region. This makes the region no longer viable
      // to be split, so we ignore it for outlining.
      if (!OS->CandidateSplit)
        continue;

      SmallVector<BasicBlock *> BE;
      DenseSet<BasicBlock *> BlocksInRegion;
      OS->Candidate->getBasicBlocks(BlocksInRegion, BE);
      OS->CE = new (ExtractorAllocator.Allocate())
          CodeExtractor(BE, nullptr, false, nullptr, nullptr, nullptr, false,
                        false, nullptr, "outlined");
      findAddInputsOutputs(M, *OS, NotSame);
      if (!OS->IgnoreRegion)
        OutlinedRegions.push_back(OS);

      // We recombine the blocks together now that we have gathered all the
      // needed information.
      OS->reattachCandidate();
    }

    CurrentGroup.Regions = std::move(OutlinedRegions);

    if (CurrentGroup.Regions.empty())
      continue;

    CurrentGroup.collectGVNStoreSets(M);

    if (CostModel)
      findCostBenefit(M, CurrentGroup);

    // If we are adhering to the cost model, skip those groups where the cost
    // outweighs the benefits.
    if (CurrentGroup.Cost >= CurrentGroup.Benefit && CostModel) {
      OptimizationRemarkEmitter &ORE =
          getORE(*CurrentGroup.Regions[0]->Candidate->getFunction());
      ORE.emit([&]() {
        IRSimilarityCandidate *C = CurrentGroup.Regions[0]->Candidate;
        OptimizationRemarkMissed R(DEBUG_TYPE, "WouldNotDecreaseSize",
                                   C->frontInstruction());
        R << "did not outline "
          << ore::NV(std::to_string(CurrentGroup.Regions.size()))
          << " regions due to estimated increase of "
          << ore::NV("InstructionIncrease",
                     CurrentGroup.Cost - CurrentGroup.Benefit)
          << " instructions at locations ";
        interleave(
            CurrentGroup.Regions.begin(), CurrentGroup.Regions.end(),
            [&R](OutlinableRegion *Region) {
              R << ore::NV(
                  "DebugLoc",
                  Region->Candidate->frontInstruction()->getDebugLoc());
            },
            [&R]() { R << " "; });
        return R;
      });
      continue;
    }

    NegativeCostGroups.push_back(&CurrentGroup);
  }

  ExtractorAllocator.DestroyAll();

  if (NegativeCostGroups.size() > 1)
    stable_sort(NegativeCostGroups,
                [](const OutlinableGroup *LHS, const OutlinableGroup *RHS) {
                  return LHS->Benefit - LHS->Cost > RHS->Benefit - RHS->Cost;
                });

  std::vector<Function *> FuncsToRemove;
  for (OutlinableGroup *CG : NegativeCostGroups) {
    OutlinableGroup &CurrentGroup = *CG;

    OutlinedRegions.clear();
    for (OutlinableRegion *Region : CurrentGroup.Regions) {
      // We check whether our region is compatible with what has already been
      // outlined, and whether we need to ignore this item.
      if (!isCompatibleWithAlreadyOutlinedCode(*Region))
        continue;
      OutlinedRegions.push_back(Region);
    }

    if (OutlinedRegions.size() < 2)
      continue;

    // Reestimate the cost and benefit of the OutlinableGroup. Continue only if
    // we are still outlining enough regions to make up for the added cost.
    CurrentGroup.Regions = std::move(OutlinedRegions);
    if (CostModel) {
      CurrentGroup.Benefit = 0;
      CurrentGroup.Cost = 0;
      findCostBenefit(M, CurrentGroup);
      if (CurrentGroup.Cost >= CurrentGroup.Benefit)
        continue;
    }
    OutlinedRegions.clear();
    for (OutlinableRegion *Region : CurrentGroup.Regions) {
      Region->splitCandidate();
      if (!Region->CandidateSplit)
        continue;
      OutlinedRegions.push_back(Region);
    }

    CurrentGroup.Regions = std::move(OutlinedRegions);
    if (CurrentGroup.Regions.size() < 2) {
      for (OutlinableRegion *R : CurrentGroup.Regions)
        R->reattachCandidate();
      continue;
    }

    LLVM_DEBUG(dbgs() << "Outlining regions with cost " << CurrentGroup.Cost
                      << " and benefit " << CurrentGroup.Benefit << "\n");

    // Create functions out of all the sections, and mark them as outlined.
    OutlinedRegions.clear();
    for (OutlinableRegion *OS : CurrentGroup.Regions) {
      SmallVector<BasicBlock *> BE;
      DenseSet<BasicBlock *> BlocksInRegion;
      OS->Candidate->getBasicBlocks(BlocksInRegion, BE);
      OS->CE = new (ExtractorAllocator.Allocate())
          CodeExtractor(BE, nullptr, false, nullptr, nullptr, nullptr, false,
                        false, nullptr, "outlined");
      bool FunctionOutlined = extractSection(*OS);
      if (FunctionOutlined) {
        unsigned StartIdx = OS->Candidate->getStartIdx();
        unsigned EndIdx = OS->Candidate->getEndIdx();
        for (unsigned Idx = StartIdx; Idx <= EndIdx; Idx++)
          Outlined.insert(Idx);

        OutlinedRegions.push_back(OS);
      }
    }

    LLVM_DEBUG(dbgs() << "Outlined " << OutlinedRegions.size()
                      << " with benefit " << CurrentGroup.Benefit
                      << " and cost " << CurrentGroup.Cost << "\n");

    CurrentGroup.Regions = std::move(OutlinedRegions);

    if (CurrentGroup.Regions.empty())
      continue;

    OptimizationRemarkEmitter &ORE =
        getORE(*CurrentGroup.Regions[0]->Call->getFunction());
    ORE.emit([&]() {
      IRSimilarityCandidate *C = CurrentGroup.Regions[0]->Candidate;
      OptimizationRemark R(DEBUG_TYPE, "Outlined", C->front()->Inst);
      R << "outlined " << ore::NV(std::to_string(CurrentGroup.Regions.size()))
        << " regions with decrease of "
        << ore::NV("Benefit", CurrentGroup.Benefit - CurrentGroup.Cost)
        << " instructions at locations ";
      interleave(
          CurrentGroup.Regions.begin(), CurrentGroup.Regions.end(),
          [&R](OutlinableRegion *Region) {
            R << ore::NV("DebugLoc",
                         Region->Candidate->frontInstruction()->getDebugLoc());
          },
          [&R]() { R << " "; });
      return R;
    });

    deduplicateExtractedSections(M, CurrentGroup, FuncsToRemove,
                                 OutlinedFunctionNum);
  }

  for (Function *F : FuncsToRemove)
    F->eraseFromParent();

  return OutlinedFunctionNum;
}

bool IROutliner::run(Module &M) {
  CostModel = !NoCostModel;
  OutlineFromLinkODRs = EnableLinkOnceODRIROutlining;

  return doOutline(M) > 0;
}

PreservedAnalyses IROutlinerPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  std::function<TargetTransformInfo &(Function &)> GTTI =
      [&FAM](Function &F) -> TargetTransformInfo & {
    return FAM.getResult<TargetIRAnalysis>(F);
  };

  std::function<IRSimilarityIdentifier &(Module &)> GIRSI =
      [&AM](Module &M) -> IRSimilarityIdentifier & {
    return AM.getResult<IRSimilarityAnalysis>(M);
  };

  std::unique_ptr<OptimizationRemarkEmitter> ORE;
  std::function<OptimizationRemarkEmitter &(Function &)> GORE =
      [&ORE](Function &F) -> OptimizationRemarkEmitter & {
    ORE.reset(new OptimizationRemarkEmitter(&F));
    return *ORE;
  };

  if (IROutliner(GTTI, GIRSI, GORE).run(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
