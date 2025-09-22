//===- CodeGenPrepare.cpp - Prepare a function for code generation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass munges the code in the input function to better prepare it for
// SelectionDAG-based code generation. This works around limitations in it's
// basic-block-at-a-time approach. It should eventually be removed.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CodeGenPrepare.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/BasicBlockSectionsProfileReader.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/BypassSlowDivision.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SimplifyLibCalls.h"
#include "llvm/Transforms/Utils/SizeOpts.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "codegenprepare"

STATISTIC(NumBlocksElim, "Number of blocks eliminated");
STATISTIC(NumPHIsElim, "Number of trivial PHIs eliminated");
STATISTIC(NumGEPsElim, "Number of GEPs converted to casts");
STATISTIC(NumCmpUses, "Number of uses of Cmp expressions replaced with uses of "
                      "sunken Cmps");
STATISTIC(NumCastUses, "Number of uses of Cast expressions replaced with uses "
                       "of sunken Casts");
STATISTIC(NumMemoryInsts, "Number of memory instructions whose address "
                          "computations were sunk");
STATISTIC(NumMemoryInstsPhiCreated,
          "Number of phis created when address "
          "computations were sunk to memory instructions");
STATISTIC(NumMemoryInstsSelectCreated,
          "Number of select created when address "
          "computations were sunk to memory instructions");
STATISTIC(NumExtsMoved, "Number of [s|z]ext instructions combined with loads");
STATISTIC(NumExtUses, "Number of uses of [s|z]ext instructions optimized");
STATISTIC(NumAndsAdded,
          "Number of and mask instructions added to form ext loads");
STATISTIC(NumAndUses, "Number of uses of and mask instructions optimized");
STATISTIC(NumRetsDup, "Number of return instructions duplicated");
STATISTIC(NumDbgValueMoved, "Number of debug value instructions moved");
STATISTIC(NumSelectsExpanded, "Number of selects turned into branches");
STATISTIC(NumStoreExtractExposed, "Number of store(extractelement) exposed");

static cl::opt<bool> DisableBranchOpts(
    "disable-cgp-branch-opts", cl::Hidden, cl::init(false),
    cl::desc("Disable branch optimizations in CodeGenPrepare"));

static cl::opt<bool>
    DisableGCOpts("disable-cgp-gc-opts", cl::Hidden, cl::init(false),
                  cl::desc("Disable GC optimizations in CodeGenPrepare"));

static cl::opt<bool>
    DisableSelectToBranch("disable-cgp-select2branch", cl::Hidden,
                          cl::init(false),
                          cl::desc("Disable select to branch conversion."));

static cl::opt<bool>
    AddrSinkUsingGEPs("addr-sink-using-gep", cl::Hidden, cl::init(true),
                      cl::desc("Address sinking in CGP using GEPs."));

static cl::opt<bool>
    EnableAndCmpSinking("enable-andcmp-sinking", cl::Hidden, cl::init(true),
                        cl::desc("Enable sinkinig and/cmp into branches."));

static cl::opt<bool> DisableStoreExtract(
    "disable-cgp-store-extract", cl::Hidden, cl::init(false),
    cl::desc("Disable store(extract) optimizations in CodeGenPrepare"));

static cl::opt<bool> StressStoreExtract(
    "stress-cgp-store-extract", cl::Hidden, cl::init(false),
    cl::desc("Stress test store(extract) optimizations in CodeGenPrepare"));

static cl::opt<bool> DisableExtLdPromotion(
    "disable-cgp-ext-ld-promotion", cl::Hidden, cl::init(false),
    cl::desc("Disable ext(promotable(ld)) -> promoted(ext(ld)) optimization in "
             "CodeGenPrepare"));

static cl::opt<bool> StressExtLdPromotion(
    "stress-cgp-ext-ld-promotion", cl::Hidden, cl::init(false),
    cl::desc("Stress test ext(promotable(ld)) -> promoted(ext(ld)) "
             "optimization in CodeGenPrepare"));

static cl::opt<bool> DisablePreheaderProtect(
    "disable-preheader-prot", cl::Hidden, cl::init(false),
    cl::desc("Disable protection against removing loop preheaders"));

static cl::opt<bool> ProfileGuidedSectionPrefix(
    "profile-guided-section-prefix", cl::Hidden, cl::init(true),
    cl::desc("Use profile info to add section prefix for hot/cold functions"));

static cl::opt<bool> ProfileUnknownInSpecialSection(
    "profile-unknown-in-special-section", cl::Hidden,
    cl::desc("In profiling mode like sampleFDO, if a function doesn't have "
             "profile, we cannot tell the function is cold for sure because "
             "it may be a function newly added without ever being sampled. "
             "With the flag enabled, compiler can put such profile unknown "
             "functions into a special section, so runtime system can choose "
             "to handle it in a different way than .text section, to save "
             "RAM for example. "));

static cl::opt<bool> BBSectionsGuidedSectionPrefix(
    "bbsections-guided-section-prefix", cl::Hidden, cl::init(true),
    cl::desc("Use the basic-block-sections profile to determine the text "
             "section prefix for hot functions. Functions with "
             "basic-block-sections profile will be placed in `.text.hot` "
             "regardless of their FDO profile info. Other functions won't be "
             "impacted, i.e., their prefixes will be decided by FDO/sampleFDO "
             "profiles."));

static cl::opt<uint64_t> FreqRatioToSkipMerge(
    "cgp-freq-ratio-to-skip-merge", cl::Hidden, cl::init(2),
    cl::desc("Skip merging empty blocks if (frequency of empty block) / "
             "(frequency of destination block) is greater than this ratio"));

static cl::opt<bool> ForceSplitStore(
    "force-split-store", cl::Hidden, cl::init(false),
    cl::desc("Force store splitting no matter what the target query says."));

static cl::opt<bool> EnableTypePromotionMerge(
    "cgp-type-promotion-merge", cl::Hidden,
    cl::desc("Enable merging of redundant sexts when one is dominating"
             " the other."),
    cl::init(true));

static cl::opt<bool> DisableComplexAddrModes(
    "disable-complex-addr-modes", cl::Hidden, cl::init(false),
    cl::desc("Disables combining addressing modes with different parts "
             "in optimizeMemoryInst."));

static cl::opt<bool>
    AddrSinkNewPhis("addr-sink-new-phis", cl::Hidden, cl::init(false),
                    cl::desc("Allow creation of Phis in Address sinking."));

static cl::opt<bool> AddrSinkNewSelects(
    "addr-sink-new-select", cl::Hidden, cl::init(true),
    cl::desc("Allow creation of selects in Address sinking."));

static cl::opt<bool> AddrSinkCombineBaseReg(
    "addr-sink-combine-base-reg", cl::Hidden, cl::init(true),
    cl::desc("Allow combining of BaseReg field in Address sinking."));

static cl::opt<bool> AddrSinkCombineBaseGV(
    "addr-sink-combine-base-gv", cl::Hidden, cl::init(true),
    cl::desc("Allow combining of BaseGV field in Address sinking."));

static cl::opt<bool> AddrSinkCombineBaseOffs(
    "addr-sink-combine-base-offs", cl::Hidden, cl::init(true),
    cl::desc("Allow combining of BaseOffs field in Address sinking."));

static cl::opt<bool> AddrSinkCombineScaledReg(
    "addr-sink-combine-scaled-reg", cl::Hidden, cl::init(true),
    cl::desc("Allow combining of ScaledReg field in Address sinking."));

static cl::opt<bool>
    EnableGEPOffsetSplit("cgp-split-large-offset-gep", cl::Hidden,
                         cl::init(true),
                         cl::desc("Enable splitting large offset of GEP."));

static cl::opt<bool> EnableICMP_EQToICMP_ST(
    "cgp-icmp-eq2icmp-st", cl::Hidden, cl::init(false),
    cl::desc("Enable ICMP_EQ to ICMP_S(L|G)T conversion."));

static cl::opt<bool>
    VerifyBFIUpdates("cgp-verify-bfi-updates", cl::Hidden, cl::init(false),
                     cl::desc("Enable BFI update verification for "
                              "CodeGenPrepare."));

static cl::opt<bool>
    OptimizePhiTypes("cgp-optimize-phi-types", cl::Hidden, cl::init(true),
                     cl::desc("Enable converting phi types in CodeGenPrepare"));

static cl::opt<unsigned>
    HugeFuncThresholdInCGPP("cgpp-huge-func", cl::init(10000), cl::Hidden,
                            cl::desc("Least BB number of huge function."));

static cl::opt<unsigned>
    MaxAddressUsersToScan("cgp-max-address-users-to-scan", cl::init(100),
                          cl::Hidden,
                          cl::desc("Max number of address users to look at"));

static cl::opt<bool>
    DisableDeletePHIs("disable-cgp-delete-phis", cl::Hidden, cl::init(false),
                      cl::desc("Disable elimination of dead PHI nodes."));

namespace {

enum ExtType {
  ZeroExtension, // Zero extension has been seen.
  SignExtension, // Sign extension has been seen.
  BothExtension  // This extension type is used if we saw sext after
                 // ZeroExtension had been set, or if we saw zext after
                 // SignExtension had been set. It makes the type
                 // information of a promoted instruction invalid.
};

enum ModifyDT {
  NotModifyDT, // Not Modify any DT.
  ModifyBBDT,  // Modify the Basic Block Dominator Tree.
  ModifyInstDT // Modify the Instruction Dominator in a Basic Block,
               // This usually means we move/delete/insert instruction
               // in a Basic Block. So we should re-iterate instructions
               // in such Basic Block.
};

using SetOfInstrs = SmallPtrSet<Instruction *, 16>;
using TypeIsSExt = PointerIntPair<Type *, 2, ExtType>;
using InstrToOrigTy = DenseMap<Instruction *, TypeIsSExt>;
using SExts = SmallVector<Instruction *, 16>;
using ValueToSExts = MapVector<Value *, SExts>;

class TypePromotionTransaction;

class CodeGenPrepare {
  friend class CodeGenPrepareLegacyPass;
  const TargetMachine *TM = nullptr;
  const TargetSubtargetInfo *SubtargetInfo = nullptr;
  const TargetLowering *TLI = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const TargetTransformInfo *TTI = nullptr;
  const BasicBlockSectionsProfileReader *BBSectionsProfileReader = nullptr;
  const TargetLibraryInfo *TLInfo = nullptr;
  LoopInfo *LI = nullptr;
  std::unique_ptr<BlockFrequencyInfo> BFI;
  std::unique_ptr<BranchProbabilityInfo> BPI;
  ProfileSummaryInfo *PSI = nullptr;

  /// As we scan instructions optimizing them, this is the next instruction
  /// to optimize. Transforms that can invalidate this should update it.
  BasicBlock::iterator CurInstIterator;

  /// Keeps track of non-local addresses that have been sunk into a block.
  /// This allows us to avoid inserting duplicate code for blocks with
  /// multiple load/stores of the same address. The usage of WeakTrackingVH
  /// enables SunkAddrs to be treated as a cache whose entries can be
  /// invalidated if a sunken address computation has been erased.
  ValueMap<Value *, WeakTrackingVH> SunkAddrs;

  /// Keeps track of all instructions inserted for the current function.
  SetOfInstrs InsertedInsts;

  /// Keeps track of the type of the related instruction before their
  /// promotion for the current function.
  InstrToOrigTy PromotedInsts;

  /// Keep track of instructions removed during promotion.
  SetOfInstrs RemovedInsts;

  /// Keep track of sext chains based on their initial value.
  DenseMap<Value *, Instruction *> SeenChainsForSExt;

  /// Keep track of GEPs accessing the same data structures such as structs or
  /// arrays that are candidates to be split later because of their large
  /// size.
  MapVector<AssertingVH<Value>,
            SmallVector<std::pair<AssertingVH<GetElementPtrInst>, int64_t>, 32>>
      LargeOffsetGEPMap;

  /// Keep track of new GEP base after splitting the GEPs having large offset.
  SmallSet<AssertingVH<Value>, 2> NewGEPBases;

  /// Map serial numbers to Large offset GEPs.
  DenseMap<AssertingVH<GetElementPtrInst>, int> LargeOffsetGEPID;

  /// Keep track of SExt promoted.
  ValueToSExts ValToSExtendedUses;

  /// True if the function has the OptSize attribute.
  bool OptSize;

  /// DataLayout for the Function being processed.
  const DataLayout *DL = nullptr;

  /// Building the dominator tree can be expensive, so we only build it
  /// lazily and update it when required.
  std::unique_ptr<DominatorTree> DT;

public:
  CodeGenPrepare(){};
  CodeGenPrepare(const TargetMachine *TM) : TM(TM){};
  /// If encounter huge function, we need to limit the build time.
  bool IsHugeFunc = false;

  /// FreshBBs is like worklist, it collected the updated BBs which need
  /// to be optimized again.
  /// Note: Consider building time in this pass, when a BB updated, we need
  /// to insert such BB into FreshBBs for huge function.
  SmallSet<BasicBlock *, 32> FreshBBs;

  void releaseMemory() {
    // Clear per function information.
    InsertedInsts.clear();
    PromotedInsts.clear();
    FreshBBs.clear();
    BPI.reset();
    BFI.reset();
  }

  bool run(Function &F, FunctionAnalysisManager &AM);

private:
  template <typename F>
  void resetIteratorIfInvalidatedWhileCalling(BasicBlock *BB, F f) {
    // Substituting can cause recursive simplifications, which can invalidate
    // our iterator.  Use a WeakTrackingVH to hold onto it in case this
    // happens.
    Value *CurValue = &*CurInstIterator;
    WeakTrackingVH IterHandle(CurValue);

    f();

    // If the iterator instruction was recursively deleted, start over at the
    // start of the block.
    if (IterHandle != CurValue) {
      CurInstIterator = BB->begin();
      SunkAddrs.clear();
    }
  }

  // Get the DominatorTree, building if necessary.
  DominatorTree &getDT(Function &F) {
    if (!DT)
      DT = std::make_unique<DominatorTree>(F);
    return *DT;
  }

  void removeAllAssertingVHReferences(Value *V);
  bool eliminateAssumptions(Function &F);
  bool eliminateFallThrough(Function &F, DominatorTree *DT = nullptr);
  bool eliminateMostlyEmptyBlocks(Function &F);
  BasicBlock *findDestBlockOfMergeableEmptyBlock(BasicBlock *BB);
  bool canMergeBlocks(const BasicBlock *BB, const BasicBlock *DestBB) const;
  void eliminateMostlyEmptyBlock(BasicBlock *BB);
  bool isMergingEmptyBlockProfitable(BasicBlock *BB, BasicBlock *DestBB,
                                     bool isPreheader);
  bool makeBitReverse(Instruction &I);
  bool optimizeBlock(BasicBlock &BB, ModifyDT &ModifiedDT);
  bool optimizeInst(Instruction *I, ModifyDT &ModifiedDT);
  bool optimizeMemoryInst(Instruction *MemoryInst, Value *Addr, Type *AccessTy,
                          unsigned AddrSpace);
  bool optimizeGatherScatterInst(Instruction *MemoryInst, Value *Ptr);
  bool optimizeInlineAsmInst(CallInst *CS);
  bool optimizeCallInst(CallInst *CI, ModifyDT &ModifiedDT);
  bool optimizeExt(Instruction *&I);
  bool optimizeExtUses(Instruction *I);
  bool optimizeLoadExt(LoadInst *Load);
  bool optimizeShiftInst(BinaryOperator *BO);
  bool optimizeFunnelShift(IntrinsicInst *Fsh);
  bool optimizeSelectInst(SelectInst *SI);
  bool optimizeShuffleVectorInst(ShuffleVectorInst *SVI);
  bool optimizeSwitchType(SwitchInst *SI);
  bool optimizeSwitchPhiConstants(SwitchInst *SI);
  bool optimizeSwitchInst(SwitchInst *SI);
  bool optimizeExtractElementInst(Instruction *Inst);
  bool dupRetToEnableTailCallOpts(BasicBlock *BB, ModifyDT &ModifiedDT);
  bool fixupDbgValue(Instruction *I);
  bool fixupDbgVariableRecord(DbgVariableRecord &I);
  bool fixupDbgVariableRecordsOnInst(Instruction &I);
  bool placeDbgValues(Function &F);
  bool placePseudoProbes(Function &F);
  bool canFormExtLd(const SmallVectorImpl<Instruction *> &MovedExts,
                    LoadInst *&LI, Instruction *&Inst, bool HasPromoted);
  bool tryToPromoteExts(TypePromotionTransaction &TPT,
                        const SmallVectorImpl<Instruction *> &Exts,
                        SmallVectorImpl<Instruction *> &ProfitablyMovedExts,
                        unsigned CreatedInstsCost = 0);
  bool mergeSExts(Function &F);
  bool splitLargeGEPOffsets();
  bool optimizePhiType(PHINode *Inst, SmallPtrSetImpl<PHINode *> &Visited,
                       SmallPtrSetImpl<Instruction *> &DeletedInstrs);
  bool optimizePhiTypes(Function &F);
  bool performAddressTypePromotion(
      Instruction *&Inst, bool AllowPromotionWithoutCommonHeader,
      bool HasPromoted, TypePromotionTransaction &TPT,
      SmallVectorImpl<Instruction *> &SpeculativelyMovedExts);
  bool splitBranchCondition(Function &F, ModifyDT &ModifiedDT);
  bool simplifyOffsetableRelocate(GCStatepointInst &I);

  bool tryToSinkFreeOperands(Instruction *I);
  bool replaceMathCmpWithIntrinsic(BinaryOperator *BO, Value *Arg0, Value *Arg1,
                                   CmpInst *Cmp, Intrinsic::ID IID);
  bool optimizeCmp(CmpInst *Cmp, ModifyDT &ModifiedDT);
  bool combineToUSubWithOverflow(CmpInst *Cmp, ModifyDT &ModifiedDT);
  bool combineToUAddWithOverflow(CmpInst *Cmp, ModifyDT &ModifiedDT);
  void verifyBFIUpdates(Function &F);
  bool _run(Function &F);
};

class CodeGenPrepareLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  CodeGenPrepareLegacyPass() : FunctionPass(ID) {
    initializeCodeGenPrepareLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override { return "CodeGen Prepare"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    // FIXME: When we can selectively preserve passes, preserve the domtree.
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addUsedIfAvailable<BasicBlockSectionsProfileReaderWrapperPass>();
  }
};

} // end anonymous namespace

char CodeGenPrepareLegacyPass::ID = 0;

bool CodeGenPrepareLegacyPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;
  auto TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
  CodeGenPrepare CGP(TM);
  CGP.DL = &F.getDataLayout();
  CGP.SubtargetInfo = TM->getSubtargetImpl(F);
  CGP.TLI = CGP.SubtargetInfo->getTargetLowering();
  CGP.TRI = CGP.SubtargetInfo->getRegisterInfo();
  CGP.TLInfo = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  CGP.TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  CGP.LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  CGP.BPI.reset(new BranchProbabilityInfo(F, *CGP.LI));
  CGP.BFI.reset(new BlockFrequencyInfo(F, *CGP.BPI, *CGP.LI));
  CGP.PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  auto BBSPRWP =
      getAnalysisIfAvailable<BasicBlockSectionsProfileReaderWrapperPass>();
  CGP.BBSectionsProfileReader = BBSPRWP ? &BBSPRWP->getBBSPR() : nullptr;

  return CGP._run(F);
}

INITIALIZE_PASS_BEGIN(CodeGenPrepareLegacyPass, DEBUG_TYPE,
                      "Optimize for code generation", false, false)
INITIALIZE_PASS_DEPENDENCY(BasicBlockSectionsProfileReaderWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(CodeGenPrepareLegacyPass, DEBUG_TYPE,
                    "Optimize for code generation", false, false)

FunctionPass *llvm::createCodeGenPrepareLegacyPass() {
  return new CodeGenPrepareLegacyPass();
}

PreservedAnalyses CodeGenPreparePass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  CodeGenPrepare CGP(TM);

  bool Changed = CGP.run(F, AM);
  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<TargetLibraryAnalysis>();
  PA.preserve<TargetIRAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}

bool CodeGenPrepare::run(Function &F, FunctionAnalysisManager &AM) {
  DL = &F.getDataLayout();
  SubtargetInfo = TM->getSubtargetImpl(F);
  TLI = SubtargetInfo->getTargetLowering();
  TRI = SubtargetInfo->getRegisterInfo();
  TLInfo = &AM.getResult<TargetLibraryAnalysis>(F);
  TTI = &AM.getResult<TargetIRAnalysis>(F);
  LI = &AM.getResult<LoopAnalysis>(F);
  BPI.reset(new BranchProbabilityInfo(F, *LI));
  BFI.reset(new BlockFrequencyInfo(F, *BPI, *LI));
  auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  PSI = MAMProxy.getCachedResult<ProfileSummaryAnalysis>(*F.getParent());
  BBSectionsProfileReader =
      AM.getCachedResult<BasicBlockSectionsProfileReaderAnalysis>(F);
  return _run(F);
}

bool CodeGenPrepare::_run(Function &F) {
  bool EverMadeChange = false;

  OptSize = F.hasOptSize();
  // Use the basic-block-sections profile to promote hot functions to .text.hot
  // if requested.
  if (BBSectionsGuidedSectionPrefix && BBSectionsProfileReader &&
      BBSectionsProfileReader->isFunctionHot(F.getName())) {
    F.setSectionPrefix("hot");
  } else if (ProfileGuidedSectionPrefix) {
    // The hot attribute overwrites profile count based hotness while profile
    // counts based hotness overwrite the cold attribute.
    // This is a conservative behabvior.
    if (F.hasFnAttribute(Attribute::Hot) ||
        PSI->isFunctionHotInCallGraph(&F, *BFI))
      F.setSectionPrefix("hot");
    // If PSI shows this function is not hot, we will placed the function
    // into unlikely section if (1) PSI shows this is a cold function, or
    // (2) the function has a attribute of cold.
    else if (PSI->isFunctionColdInCallGraph(&F, *BFI) ||
             F.hasFnAttribute(Attribute::Cold))
      F.setSectionPrefix("unlikely");
    else if (ProfileUnknownInSpecialSection && PSI->hasPartialSampleProfile() &&
             PSI->isFunctionHotnessUnknown(F))
      F.setSectionPrefix("unknown");
  }

  /// This optimization identifies DIV instructions that can be
  /// profitably bypassed and carried out with a shorter, faster divide.
  if (!OptSize && !PSI->hasHugeWorkingSetSize() && TLI->isSlowDivBypassed()) {
    const DenseMap<unsigned int, unsigned int> &BypassWidths =
        TLI->getBypassSlowDivWidths();
    BasicBlock *BB = &*F.begin();
    while (BB != nullptr) {
      // bypassSlowDivision may create new BBs, but we don't want to reapply the
      // optimization to those blocks.
      BasicBlock *Next = BB->getNextNode();
      // F.hasOptSize is already checked in the outer if statement.
      if (!llvm::shouldOptimizeForSize(BB, PSI, BFI.get()))
        EverMadeChange |= bypassSlowDivision(BB, BypassWidths);
      BB = Next;
    }
  }

  // Get rid of @llvm.assume builtins before attempting to eliminate empty
  // blocks, since there might be blocks that only contain @llvm.assume calls
  // (plus arguments that we can get rid of).
  EverMadeChange |= eliminateAssumptions(F);

  // Eliminate blocks that contain only PHI nodes and an
  // unconditional branch.
  EverMadeChange |= eliminateMostlyEmptyBlocks(F);

  ModifyDT ModifiedDT = ModifyDT::NotModifyDT;
  if (!DisableBranchOpts)
    EverMadeChange |= splitBranchCondition(F, ModifiedDT);

  // Split some critical edges where one of the sources is an indirect branch,
  // to help generate sane code for PHIs involving such edges.
  EverMadeChange |=
      SplitIndirectBrCriticalEdges(F, /*IgnoreBlocksWithoutPHI=*/true);

  // If we are optimzing huge function, we need to consider the build time.
  // Because the basic algorithm's complex is near O(N!).
  IsHugeFunc = F.size() > HugeFuncThresholdInCGPP;

  // Transformations above may invalidate dominator tree and/or loop info.
  DT.reset();
  LI->releaseMemory();
  LI->analyze(getDT(F));

  bool MadeChange = true;
  bool FuncIterated = false;
  while (MadeChange) {
    MadeChange = false;

    for (BasicBlock &BB : llvm::make_early_inc_range(F)) {
      if (FuncIterated && !FreshBBs.contains(&BB))
        continue;

      ModifyDT ModifiedDTOnIteration = ModifyDT::NotModifyDT;
      bool Changed = optimizeBlock(BB, ModifiedDTOnIteration);

      if (ModifiedDTOnIteration == ModifyDT::ModifyBBDT)
        DT.reset();

      MadeChange |= Changed;
      if (IsHugeFunc) {
        // If the BB is updated, it may still has chance to be optimized.
        // This usually happen at sink optimization.
        // For example:
        //
        // bb0：
        // %and = and i32 %a, 4
        // %cmp = icmp eq i32 %and, 0
        //
        // If the %cmp sink to other BB, the %and will has chance to sink.
        if (Changed)
          FreshBBs.insert(&BB);
        else if (FuncIterated)
          FreshBBs.erase(&BB);
      } else {
        // For small/normal functions, we restart BB iteration if the dominator
        // tree of the Function was changed.
        if (ModifiedDTOnIteration != ModifyDT::NotModifyDT)
          break;
      }
    }
    // We have iterated all the BB in the (only work for huge) function.
    FuncIterated = IsHugeFunc;

    if (EnableTypePromotionMerge && !ValToSExtendedUses.empty())
      MadeChange |= mergeSExts(F);
    if (!LargeOffsetGEPMap.empty())
      MadeChange |= splitLargeGEPOffsets();
    MadeChange |= optimizePhiTypes(F);

    if (MadeChange)
      eliminateFallThrough(F, DT.get());

#ifndef NDEBUG
    if (MadeChange && VerifyLoopInfo)
      LI->verify(getDT(F));
#endif

    // Really free removed instructions during promotion.
    for (Instruction *I : RemovedInsts)
      I->deleteValue();

    EverMadeChange |= MadeChange;
    SeenChainsForSExt.clear();
    ValToSExtendedUses.clear();
    RemovedInsts.clear();
    LargeOffsetGEPMap.clear();
    LargeOffsetGEPID.clear();
  }

  NewGEPBases.clear();
  SunkAddrs.clear();

  if (!DisableBranchOpts) {
    MadeChange = false;
    // Use a set vector to get deterministic iteration order. The order the
    // blocks are removed may affect whether or not PHI nodes in successors
    // are removed.
    SmallSetVector<BasicBlock *, 8> WorkList;
    for (BasicBlock &BB : F) {
      SmallVector<BasicBlock *, 2> Successors(successors(&BB));
      MadeChange |= ConstantFoldTerminator(&BB, true);
      if (!MadeChange)
        continue;

      for (BasicBlock *Succ : Successors)
        if (pred_empty(Succ))
          WorkList.insert(Succ);
    }

    // Delete the dead blocks and any of their dead successors.
    MadeChange |= !WorkList.empty();
    while (!WorkList.empty()) {
      BasicBlock *BB = WorkList.pop_back_val();
      SmallVector<BasicBlock *, 2> Successors(successors(BB));

      DeleteDeadBlock(BB);

      for (BasicBlock *Succ : Successors)
        if (pred_empty(Succ))
          WorkList.insert(Succ);
    }

    // Merge pairs of basic blocks with unconditional branches, connected by
    // a single edge.
    if (EverMadeChange || MadeChange)
      MadeChange |= eliminateFallThrough(F);

    EverMadeChange |= MadeChange;
  }

  if (!DisableGCOpts) {
    SmallVector<GCStatepointInst *, 2> Statepoints;
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        if (auto *SP = dyn_cast<GCStatepointInst>(&I))
          Statepoints.push_back(SP);
    for (auto &I : Statepoints)
      EverMadeChange |= simplifyOffsetableRelocate(*I);
  }

  // Do this last to clean up use-before-def scenarios introduced by other
  // preparatory transforms.
  EverMadeChange |= placeDbgValues(F);
  EverMadeChange |= placePseudoProbes(F);

#ifndef NDEBUG
  if (VerifyBFIUpdates)
    verifyBFIUpdates(F);
#endif

  return EverMadeChange;
}

bool CodeGenPrepare::eliminateAssumptions(Function &F) {
  bool MadeChange = false;
  for (BasicBlock &BB : F) {
    CurInstIterator = BB.begin();
    while (CurInstIterator != BB.end()) {
      Instruction *I = &*(CurInstIterator++);
      if (auto *Assume = dyn_cast<AssumeInst>(I)) {
        MadeChange = true;
        Value *Operand = Assume->getOperand(0);
        Assume->eraseFromParent();

        resetIteratorIfInvalidatedWhileCalling(&BB, [&]() {
          RecursivelyDeleteTriviallyDeadInstructions(Operand, TLInfo, nullptr);
        });
      }
    }
  }
  return MadeChange;
}

/// An instruction is about to be deleted, so remove all references to it in our
/// GEP-tracking data strcutures.
void CodeGenPrepare::removeAllAssertingVHReferences(Value *V) {
  LargeOffsetGEPMap.erase(V);
  NewGEPBases.erase(V);

  auto GEP = dyn_cast<GetElementPtrInst>(V);
  if (!GEP)
    return;

  LargeOffsetGEPID.erase(GEP);

  auto VecI = LargeOffsetGEPMap.find(GEP->getPointerOperand());
  if (VecI == LargeOffsetGEPMap.end())
    return;

  auto &GEPVector = VecI->second;
  llvm::erase_if(GEPVector, [=](auto &Elt) { return Elt.first == GEP; });

  if (GEPVector.empty())
    LargeOffsetGEPMap.erase(VecI);
}

// Verify BFI has been updated correctly by recomputing BFI and comparing them.
void LLVM_ATTRIBUTE_UNUSED CodeGenPrepare::verifyBFIUpdates(Function &F) {
  DominatorTree NewDT(F);
  LoopInfo NewLI(NewDT);
  BranchProbabilityInfo NewBPI(F, NewLI, TLInfo);
  BlockFrequencyInfo NewBFI(F, NewBPI, NewLI);
  NewBFI.verifyMatch(*BFI);
}

/// Merge basic blocks which are connected by a single edge, where one of the
/// basic blocks has a single successor pointing to the other basic block,
/// which has a single predecessor.
bool CodeGenPrepare::eliminateFallThrough(Function &F, DominatorTree *DT) {
  bool Changed = false;
  // Scan all of the blocks in the function, except for the entry block.
  // Use a temporary array to avoid iterator being invalidated when
  // deleting blocks.
  SmallVector<WeakTrackingVH, 16> Blocks;
  for (auto &Block : llvm::drop_begin(F))
    Blocks.push_back(&Block);

  SmallSet<WeakTrackingVH, 16> Preds;
  for (auto &Block : Blocks) {
    auto *BB = cast_or_null<BasicBlock>(Block);
    if (!BB)
      continue;
    // If the destination block has a single pred, then this is a trivial
    // edge, just collapse it.
    BasicBlock *SinglePred = BB->getSinglePredecessor();

    // Don't merge if BB's address is taken.
    if (!SinglePred || SinglePred == BB || BB->hasAddressTaken())
      continue;

    // Make an effort to skip unreachable blocks.
    if (DT && !DT->isReachableFromEntry(BB))
      continue;

    BranchInst *Term = dyn_cast<BranchInst>(SinglePred->getTerminator());
    if (Term && !Term->isConditional()) {
      Changed = true;
      LLVM_DEBUG(dbgs() << "To merge:\n" << *BB << "\n\n\n");

      // Merge BB into SinglePred and delete it.
      MergeBlockIntoPredecessor(BB, /* DTU */ nullptr, LI, /* MSSAU */ nullptr,
                                /* MemDep */ nullptr,
                                /* PredecessorWithTwoSuccessors */ false, DT);
      Preds.insert(SinglePred);

      if (IsHugeFunc) {
        // Update FreshBBs to optimize the merged BB.
        FreshBBs.insert(SinglePred);
        FreshBBs.erase(BB);
      }
    }
  }

  // (Repeatedly) merging blocks into their predecessors can create redundant
  // debug intrinsics.
  for (const auto &Pred : Preds)
    if (auto *BB = cast_or_null<BasicBlock>(Pred))
      RemoveRedundantDbgInstrs(BB);

  return Changed;
}

/// Find a destination block from BB if BB is mergeable empty block.
BasicBlock *CodeGenPrepare::findDestBlockOfMergeableEmptyBlock(BasicBlock *BB) {
  // If this block doesn't end with an uncond branch, ignore it.
  BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isUnconditional())
    return nullptr;

  // If the instruction before the branch (skipping debug info) isn't a phi
  // node, then other stuff is happening here.
  BasicBlock::iterator BBI = BI->getIterator();
  if (BBI != BB->begin()) {
    --BBI;
    while (isa<DbgInfoIntrinsic>(BBI)) {
      if (BBI == BB->begin())
        break;
      --BBI;
    }
    if (!isa<DbgInfoIntrinsic>(BBI) && !isa<PHINode>(BBI))
      return nullptr;
  }

  // Do not break infinite loops.
  BasicBlock *DestBB = BI->getSuccessor(0);
  if (DestBB == BB)
    return nullptr;

  if (!canMergeBlocks(BB, DestBB))
    DestBB = nullptr;

  return DestBB;
}

/// Eliminate blocks that contain only PHI nodes, debug info directives, and an
/// unconditional branch. Passes before isel (e.g. LSR/loopsimplify) often split
/// edges in ways that are non-optimal for isel. Start by eliminating these
/// blocks so we can split them the way we want them.
bool CodeGenPrepare::eliminateMostlyEmptyBlocks(Function &F) {
  SmallPtrSet<BasicBlock *, 16> Preheaders;
  SmallVector<Loop *, 16> LoopList(LI->begin(), LI->end());
  while (!LoopList.empty()) {
    Loop *L = LoopList.pop_back_val();
    llvm::append_range(LoopList, *L);
    if (BasicBlock *Preheader = L->getLoopPreheader())
      Preheaders.insert(Preheader);
  }

  bool MadeChange = false;
  // Copy blocks into a temporary array to avoid iterator invalidation issues
  // as we remove them.
  // Note that this intentionally skips the entry block.
  SmallVector<WeakTrackingVH, 16> Blocks;
  for (auto &Block : llvm::drop_begin(F)) {
    // Delete phi nodes that could block deleting other empty blocks.
    if (!DisableDeletePHIs)
      MadeChange |= DeleteDeadPHIs(&Block, TLInfo);
    Blocks.push_back(&Block);
  }

  for (auto &Block : Blocks) {
    BasicBlock *BB = cast_or_null<BasicBlock>(Block);
    if (!BB)
      continue;
    BasicBlock *DestBB = findDestBlockOfMergeableEmptyBlock(BB);
    if (!DestBB ||
        !isMergingEmptyBlockProfitable(BB, DestBB, Preheaders.count(BB)))
      continue;

    eliminateMostlyEmptyBlock(BB);
    MadeChange = true;
  }
  return MadeChange;
}

bool CodeGenPrepare::isMergingEmptyBlockProfitable(BasicBlock *BB,
                                                   BasicBlock *DestBB,
                                                   bool isPreheader) {
  // Do not delete loop preheaders if doing so would create a critical edge.
  // Loop preheaders can be good locations to spill registers. If the
  // preheader is deleted and we create a critical edge, registers may be
  // spilled in the loop body instead.
  if (!DisablePreheaderProtect && isPreheader &&
      !(BB->getSinglePredecessor() &&
        BB->getSinglePredecessor()->getSingleSuccessor()))
    return false;

  // Skip merging if the block's successor is also a successor to any callbr
  // that leads to this block.
  // FIXME: Is this really needed? Is this a correctness issue?
  for (BasicBlock *Pred : predecessors(BB)) {
    if (isa<CallBrInst>(Pred->getTerminator()) &&
        llvm::is_contained(successors(Pred), DestBB))
      return false;
  }

  // Try to skip merging if the unique predecessor of BB is terminated by a
  // switch or indirect branch instruction, and BB is used as an incoming block
  // of PHIs in DestBB. In such case, merging BB and DestBB would cause ISel to
  // add COPY instructions in the predecessor of BB instead of BB (if it is not
  // merged). Note that the critical edge created by merging such blocks wont be
  // split in MachineSink because the jump table is not analyzable. By keeping
  // such empty block (BB), ISel will place COPY instructions in BB, not in the
  // predecessor of BB.
  BasicBlock *Pred = BB->getUniquePredecessor();
  if (!Pred || !(isa<SwitchInst>(Pred->getTerminator()) ||
                 isa<IndirectBrInst>(Pred->getTerminator())))
    return true;

  if (BB->getTerminator() != BB->getFirstNonPHIOrDbg())
    return true;

  // We use a simple cost heuristic which determine skipping merging is
  // profitable if the cost of skipping merging is less than the cost of
  // merging : Cost(skipping merging) < Cost(merging BB), where the
  // Cost(skipping merging) is Freq(BB) * (Cost(Copy) + Cost(Branch)), and
  // the Cost(merging BB) is Freq(Pred) * Cost(Copy).
  // Assuming Cost(Copy) == Cost(Branch), we could simplify it to :
  //   Freq(Pred) / Freq(BB) > 2.
  // Note that if there are multiple empty blocks sharing the same incoming
  // value for the PHIs in the DestBB, we consider them together. In such
  // case, Cost(merging BB) will be the sum of their frequencies.

  if (!isa<PHINode>(DestBB->begin()))
    return true;

  SmallPtrSet<BasicBlock *, 16> SameIncomingValueBBs;

  // Find all other incoming blocks from which incoming values of all PHIs in
  // DestBB are the same as the ones from BB.
  for (BasicBlock *DestBBPred : predecessors(DestBB)) {
    if (DestBBPred == BB)
      continue;

    if (llvm::all_of(DestBB->phis(), [&](const PHINode &DestPN) {
          return DestPN.getIncomingValueForBlock(BB) ==
                 DestPN.getIncomingValueForBlock(DestBBPred);
        }))
      SameIncomingValueBBs.insert(DestBBPred);
  }

  // See if all BB's incoming values are same as the value from Pred. In this
  // case, no reason to skip merging because COPYs are expected to be place in
  // Pred already.
  if (SameIncomingValueBBs.count(Pred))
    return true;

  BlockFrequency PredFreq = BFI->getBlockFreq(Pred);
  BlockFrequency BBFreq = BFI->getBlockFreq(BB);

  for (auto *SameValueBB : SameIncomingValueBBs)
    if (SameValueBB->getUniquePredecessor() == Pred &&
        DestBB == findDestBlockOfMergeableEmptyBlock(SameValueBB))
      BBFreq += BFI->getBlockFreq(SameValueBB);

  std::optional<BlockFrequency> Limit = BBFreq.mul(FreqRatioToSkipMerge);
  return !Limit || PredFreq <= *Limit;
}

/// Return true if we can merge BB into DestBB if there is a single
/// unconditional branch between them, and BB contains no other non-phi
/// instructions.
bool CodeGenPrepare::canMergeBlocks(const BasicBlock *BB,
                                    const BasicBlock *DestBB) const {
  // We only want to eliminate blocks whose phi nodes are used by phi nodes in
  // the successor.  If there are more complex condition (e.g. preheaders),
  // don't mess around with them.
  for (const PHINode &PN : BB->phis()) {
    for (const User *U : PN.users()) {
      const Instruction *UI = cast<Instruction>(U);
      if (UI->getParent() != DestBB || !isa<PHINode>(UI))
        return false;
      // If User is inside DestBB block and it is a PHINode then check
      // incoming value. If incoming value is not from BB then this is
      // a complex condition (e.g. preheaders) we want to avoid here.
      if (UI->getParent() == DestBB) {
        if (const PHINode *UPN = dyn_cast<PHINode>(UI))
          for (unsigned I = 0, E = UPN->getNumIncomingValues(); I != E; ++I) {
            Instruction *Insn = dyn_cast<Instruction>(UPN->getIncomingValue(I));
            if (Insn && Insn->getParent() == BB &&
                Insn->getParent() != UPN->getIncomingBlock(I))
              return false;
          }
      }
    }
  }

  // If BB and DestBB contain any common predecessors, then the phi nodes in BB
  // and DestBB may have conflicting incoming values for the block.  If so, we
  // can't merge the block.
  const PHINode *DestBBPN = dyn_cast<PHINode>(DestBB->begin());
  if (!DestBBPN)
    return true; // no conflict.

  // Collect the preds of BB.
  SmallPtrSet<const BasicBlock *, 16> BBPreds;
  if (const PHINode *BBPN = dyn_cast<PHINode>(BB->begin())) {
    // It is faster to get preds from a PHI than with pred_iterator.
    for (unsigned i = 0, e = BBPN->getNumIncomingValues(); i != e; ++i)
      BBPreds.insert(BBPN->getIncomingBlock(i));
  } else {
    BBPreds.insert(pred_begin(BB), pred_end(BB));
  }

  // Walk the preds of DestBB.
  for (unsigned i = 0, e = DestBBPN->getNumIncomingValues(); i != e; ++i) {
    BasicBlock *Pred = DestBBPN->getIncomingBlock(i);
    if (BBPreds.count(Pred)) { // Common predecessor?
      for (const PHINode &PN : DestBB->phis()) {
        const Value *V1 = PN.getIncomingValueForBlock(Pred);
        const Value *V2 = PN.getIncomingValueForBlock(BB);

        // If V2 is a phi node in BB, look up what the mapped value will be.
        if (const PHINode *V2PN = dyn_cast<PHINode>(V2))
          if (V2PN->getParent() == BB)
            V2 = V2PN->getIncomingValueForBlock(Pred);

        // If there is a conflict, bail out.
        if (V1 != V2)
          return false;
      }
    }
  }

  return true;
}

/// Replace all old uses with new ones, and push the updated BBs into FreshBBs.
static void replaceAllUsesWith(Value *Old, Value *New,
                               SmallSet<BasicBlock *, 32> &FreshBBs,
                               bool IsHuge) {
  auto *OldI = dyn_cast<Instruction>(Old);
  if (OldI) {
    for (Value::user_iterator UI = OldI->user_begin(), E = OldI->user_end();
         UI != E; ++UI) {
      Instruction *User = cast<Instruction>(*UI);
      if (IsHuge)
        FreshBBs.insert(User->getParent());
    }
  }
  Old->replaceAllUsesWith(New);
}

/// Eliminate a basic block that has only phi's and an unconditional branch in
/// it.
void CodeGenPrepare::eliminateMostlyEmptyBlock(BasicBlock *BB) {
  BranchInst *BI = cast<BranchInst>(BB->getTerminator());
  BasicBlock *DestBB = BI->getSuccessor(0);

  LLVM_DEBUG(dbgs() << "MERGING MOSTLY EMPTY BLOCKS - BEFORE:\n"
                    << *BB << *DestBB);

  // If the destination block has a single pred, then this is a trivial edge,
  // just collapse it.
  if (BasicBlock *SinglePred = DestBB->getSinglePredecessor()) {
    if (SinglePred != DestBB) {
      assert(SinglePred == BB &&
             "Single predecessor not the same as predecessor");
      // Merge DestBB into SinglePred/BB and delete it.
      MergeBlockIntoPredecessor(DestBB);
      // Note: BB(=SinglePred) will not be deleted on this path.
      // DestBB(=its single successor) is the one that was deleted.
      LLVM_DEBUG(dbgs() << "AFTER:\n" << *SinglePred << "\n\n\n");

      if (IsHugeFunc) {
        // Update FreshBBs to optimize the merged BB.
        FreshBBs.insert(SinglePred);
        FreshBBs.erase(DestBB);
      }
      return;
    }
  }

  // Otherwise, we have multiple predecessors of BB.  Update the PHIs in DestBB
  // to handle the new incoming edges it is about to have.
  for (PHINode &PN : DestBB->phis()) {
    // Remove the incoming value for BB, and remember it.
    Value *InVal = PN.removeIncomingValue(BB, false);

    // Two options: either the InVal is a phi node defined in BB or it is some
    // value that dominates BB.
    PHINode *InValPhi = dyn_cast<PHINode>(InVal);
    if (InValPhi && InValPhi->getParent() == BB) {
      // Add all of the input values of the input PHI as inputs of this phi.
      for (unsigned i = 0, e = InValPhi->getNumIncomingValues(); i != e; ++i)
        PN.addIncoming(InValPhi->getIncomingValue(i),
                       InValPhi->getIncomingBlock(i));
    } else {
      // Otherwise, add one instance of the dominating value for each edge that
      // we will be adding.
      if (PHINode *BBPN = dyn_cast<PHINode>(BB->begin())) {
        for (unsigned i = 0, e = BBPN->getNumIncomingValues(); i != e; ++i)
          PN.addIncoming(InVal, BBPN->getIncomingBlock(i));
      } else {
        for (BasicBlock *Pred : predecessors(BB))
          PN.addIncoming(InVal, Pred);
      }
    }
  }

  // The PHIs are now updated, change everything that refers to BB to use
  // DestBB and remove BB.
  BB->replaceAllUsesWith(DestBB);
  BB->eraseFromParent();
  ++NumBlocksElim;

  LLVM_DEBUG(dbgs() << "AFTER:\n" << *DestBB << "\n\n\n");
}

// Computes a map of base pointer relocation instructions to corresponding
// derived pointer relocation instructions given a vector of all relocate calls
static void computeBaseDerivedRelocateMap(
    const SmallVectorImpl<GCRelocateInst *> &AllRelocateCalls,
    MapVector<GCRelocateInst *, SmallVector<GCRelocateInst *, 0>>
        &RelocateInstMap) {
  // Collect information in two maps: one primarily for locating the base object
  // while filling the second map; the second map is the final structure holding
  // a mapping between Base and corresponding Derived relocate calls
  MapVector<std::pair<unsigned, unsigned>, GCRelocateInst *> RelocateIdxMap;
  for (auto *ThisRelocate : AllRelocateCalls) {
    auto K = std::make_pair(ThisRelocate->getBasePtrIndex(),
                            ThisRelocate->getDerivedPtrIndex());
    RelocateIdxMap.insert(std::make_pair(K, ThisRelocate));
  }
  for (auto &Item : RelocateIdxMap) {
    std::pair<unsigned, unsigned> Key = Item.first;
    if (Key.first == Key.second)
      // Base relocation: nothing to insert
      continue;

    GCRelocateInst *I = Item.second;
    auto BaseKey = std::make_pair(Key.first, Key.first);

    // We're iterating over RelocateIdxMap so we cannot modify it.
    auto MaybeBase = RelocateIdxMap.find(BaseKey);
    if (MaybeBase == RelocateIdxMap.end())
      // TODO: We might want to insert a new base object relocate and gep off
      // that, if there are enough derived object relocates.
      continue;

    RelocateInstMap[MaybeBase->second].push_back(I);
  }
}

// Accepts a GEP and extracts the operands into a vector provided they're all
// small integer constants
static bool getGEPSmallConstantIntOffsetV(GetElementPtrInst *GEP,
                                          SmallVectorImpl<Value *> &OffsetV) {
  for (unsigned i = 1; i < GEP->getNumOperands(); i++) {
    // Only accept small constant integer operands
    auto *Op = dyn_cast<ConstantInt>(GEP->getOperand(i));
    if (!Op || Op->getZExtValue() > 20)
      return false;
  }

  for (unsigned i = 1; i < GEP->getNumOperands(); i++)
    OffsetV.push_back(GEP->getOperand(i));
  return true;
}

// Takes a RelocatedBase (base pointer relocation instruction) and Targets to
// replace, computes a replacement, and affects it.
static bool
simplifyRelocatesOffABase(GCRelocateInst *RelocatedBase,
                          const SmallVectorImpl<GCRelocateInst *> &Targets) {
  bool MadeChange = false;
  // We must ensure the relocation of derived pointer is defined after
  // relocation of base pointer. If we find a relocation corresponding to base
  // defined earlier than relocation of base then we move relocation of base
  // right before found relocation. We consider only relocation in the same
  // basic block as relocation of base. Relocations from other basic block will
  // be skipped by optimization and we do not care about them.
  for (auto R = RelocatedBase->getParent()->getFirstInsertionPt();
       &*R != RelocatedBase; ++R)
    if (auto *RI = dyn_cast<GCRelocateInst>(R))
      if (RI->getStatepoint() == RelocatedBase->getStatepoint())
        if (RI->getBasePtrIndex() == RelocatedBase->getBasePtrIndex()) {
          RelocatedBase->moveBefore(RI);
          MadeChange = true;
          break;
        }

  for (GCRelocateInst *ToReplace : Targets) {
    assert(ToReplace->getBasePtrIndex() == RelocatedBase->getBasePtrIndex() &&
           "Not relocating a derived object of the original base object");
    if (ToReplace->getBasePtrIndex() == ToReplace->getDerivedPtrIndex()) {
      // A duplicate relocate call. TODO: coalesce duplicates.
      continue;
    }

    if (RelocatedBase->getParent() != ToReplace->getParent()) {
      // Base and derived relocates are in different basic blocks.
      // In this case transform is only valid when base dominates derived
      // relocate. However it would be too expensive to check dominance
      // for each such relocate, so we skip the whole transformation.
      continue;
    }

    Value *Base = ToReplace->getBasePtr();
    auto *Derived = dyn_cast<GetElementPtrInst>(ToReplace->getDerivedPtr());
    if (!Derived || Derived->getPointerOperand() != Base)
      continue;

    SmallVector<Value *, 2> OffsetV;
    if (!getGEPSmallConstantIntOffsetV(Derived, OffsetV))
      continue;

    // Create a Builder and replace the target callsite with a gep
    assert(RelocatedBase->getNextNode() &&
           "Should always have one since it's not a terminator");

    // Insert after RelocatedBase
    IRBuilder<> Builder(RelocatedBase->getNextNode());
    Builder.SetCurrentDebugLocation(ToReplace->getDebugLoc());

    // If gc_relocate does not match the actual type, cast it to the right type.
    // In theory, there must be a bitcast after gc_relocate if the type does not
    // match, and we should reuse it to get the derived pointer. But it could be
    // cases like this:
    // bb1:
    //  ...
    //  %g1 = call coldcc i8 addrspace(1)*
    //  @llvm.experimental.gc.relocate.p1i8(...) br label %merge
    //
    // bb2:
    //  ...
    //  %g2 = call coldcc i8 addrspace(1)*
    //  @llvm.experimental.gc.relocate.p1i8(...) br label %merge
    //
    // merge:
    //  %p1 = phi i8 addrspace(1)* [ %g1, %bb1 ], [ %g2, %bb2 ]
    //  %cast = bitcast i8 addrspace(1)* %p1 in to i32 addrspace(1)*
    //
    // In this case, we can not find the bitcast any more. So we insert a new
    // bitcast no matter there is already one or not. In this way, we can handle
    // all cases, and the extra bitcast should be optimized away in later
    // passes.
    Value *ActualRelocatedBase = RelocatedBase;
    if (RelocatedBase->getType() != Base->getType()) {
      ActualRelocatedBase =
          Builder.CreateBitCast(RelocatedBase, Base->getType());
    }
    Value *Replacement =
        Builder.CreateGEP(Derived->getSourceElementType(), ActualRelocatedBase,
                          ArrayRef(OffsetV));
    Replacement->takeName(ToReplace);
    // If the newly generated derived pointer's type does not match the original
    // derived pointer's type, cast the new derived pointer to match it. Same
    // reasoning as above.
    Value *ActualReplacement = Replacement;
    if (Replacement->getType() != ToReplace->getType()) {
      ActualReplacement =
          Builder.CreateBitCast(Replacement, ToReplace->getType());
    }
    ToReplace->replaceAllUsesWith(ActualReplacement);
    ToReplace->eraseFromParent();

    MadeChange = true;
  }
  return MadeChange;
}

// Turns this:
//
// %base = ...
// %ptr = gep %base + 15
// %tok = statepoint (%fun, i32 0, i32 0, i32 0, %base, %ptr)
// %base' = relocate(%tok, i32 4, i32 4)
// %ptr' = relocate(%tok, i32 4, i32 5)
// %val = load %ptr'
//
// into this:
//
// %base = ...
// %ptr = gep %base + 15
// %tok = statepoint (%fun, i32 0, i32 0, i32 0, %base, %ptr)
// %base' = gc.relocate(%tok, i32 4, i32 4)
// %ptr' = gep %base' + 15
// %val = load %ptr'
bool CodeGenPrepare::simplifyOffsetableRelocate(GCStatepointInst &I) {
  bool MadeChange = false;
  SmallVector<GCRelocateInst *, 2> AllRelocateCalls;
  for (auto *U : I.users())
    if (GCRelocateInst *Relocate = dyn_cast<GCRelocateInst>(U))
      // Collect all the relocate calls associated with a statepoint
      AllRelocateCalls.push_back(Relocate);

  // We need at least one base pointer relocation + one derived pointer
  // relocation to mangle
  if (AllRelocateCalls.size() < 2)
    return false;

  // RelocateInstMap is a mapping from the base relocate instruction to the
  // corresponding derived relocate instructions
  MapVector<GCRelocateInst *, SmallVector<GCRelocateInst *, 0>> RelocateInstMap;
  computeBaseDerivedRelocateMap(AllRelocateCalls, RelocateInstMap);
  if (RelocateInstMap.empty())
    return false;

  for (auto &Item : RelocateInstMap)
    // Item.first is the RelocatedBase to offset against
    // Item.second is the vector of Targets to replace
    MadeChange = simplifyRelocatesOffABase(Item.first, Item.second);
  return MadeChange;
}

/// Sink the specified cast instruction into its user blocks.
static bool SinkCast(CastInst *CI) {
  BasicBlock *DefBB = CI->getParent();

  /// InsertedCasts - Only insert a cast in each block once.
  DenseMap<BasicBlock *, CastInst *> InsertedCasts;

  bool MadeChange = false;
  for (Value::user_iterator UI = CI->user_begin(), E = CI->user_end();
       UI != E;) {
    Use &TheUse = UI.getUse();
    Instruction *User = cast<Instruction>(*UI);

    // Figure out which BB this cast is used in.  For PHI's this is the
    // appropriate predecessor block.
    BasicBlock *UserBB = User->getParent();
    if (PHINode *PN = dyn_cast<PHINode>(User)) {
      UserBB = PN->getIncomingBlock(TheUse);
    }

    // Preincrement use iterator so we don't invalidate it.
    ++UI;

    // The first insertion point of a block containing an EH pad is after the
    // pad.  If the pad is the user, we cannot sink the cast past the pad.
    if (User->isEHPad())
      continue;

    // If the block selected to receive the cast is an EH pad that does not
    // allow non-PHI instructions before the terminator, we can't sink the
    // cast.
    if (UserBB->getTerminator()->isEHPad())
      continue;

    // If this user is in the same block as the cast, don't change the cast.
    if (UserBB == DefBB)
      continue;

    // If we have already inserted a cast into this block, use it.
    CastInst *&InsertedCast = InsertedCasts[UserBB];

    if (!InsertedCast) {
      BasicBlock::iterator InsertPt = UserBB->getFirstInsertionPt();
      assert(InsertPt != UserBB->end());
      InsertedCast = cast<CastInst>(CI->clone());
      InsertedCast->insertBefore(*UserBB, InsertPt);
    }

    // Replace a use of the cast with a use of the new cast.
    TheUse = InsertedCast;
    MadeChange = true;
    ++NumCastUses;
  }

  // If we removed all uses, nuke the cast.
  if (CI->use_empty()) {
    salvageDebugInfo(*CI);
    CI->eraseFromParent();
    MadeChange = true;
  }

  return MadeChange;
}

/// If the specified cast instruction is a noop copy (e.g. it's casting from
/// one pointer type to another, i32->i8 on PPC), sink it into user blocks to
/// reduce the number of virtual registers that must be created and coalesced.
///
/// Return true if any changes are made.
static bool OptimizeNoopCopyExpression(CastInst *CI, const TargetLowering &TLI,
                                       const DataLayout &DL) {
  // Sink only "cheap" (or nop) address-space casts.  This is a weaker condition
  // than sinking only nop casts, but is helpful on some platforms.
  if (auto *ASC = dyn_cast<AddrSpaceCastInst>(CI)) {
    if (!TLI.isFreeAddrSpaceCast(ASC->getSrcAddressSpace(),
                                 ASC->getDestAddressSpace()))
      return false;
  }

  // If this is a noop copy,
  EVT SrcVT = TLI.getValueType(DL, CI->getOperand(0)->getType());
  EVT DstVT = TLI.getValueType(DL, CI->getType());

  // This is an fp<->int conversion?
  if (SrcVT.isInteger() != DstVT.isInteger())
    return false;

  // If this is an extension, it will be a zero or sign extension, which
  // isn't a noop.
  if (SrcVT.bitsLT(DstVT))
    return false;

  // If these values will be promoted, find out what they will be promoted
  // to.  This helps us consider truncates on PPC as noop copies when they
  // are.
  if (TLI.getTypeAction(CI->getContext(), SrcVT) ==
      TargetLowering::TypePromoteInteger)
    SrcVT = TLI.getTypeToTransformTo(CI->getContext(), SrcVT);
  if (TLI.getTypeAction(CI->getContext(), DstVT) ==
      TargetLowering::TypePromoteInteger)
    DstVT = TLI.getTypeToTransformTo(CI->getContext(), DstVT);

  // If, after promotion, these are the same types, this is a noop copy.
  if (SrcVT != DstVT)
    return false;

  return SinkCast(CI);
}

// Match a simple increment by constant operation.  Note that if a sub is
// matched, the step is negated (as if the step had been canonicalized to
// an add, even though we leave the instruction alone.)
static bool matchIncrement(const Instruction *IVInc, Instruction *&LHS,
                           Constant *&Step) {
  if (match(IVInc, m_Add(m_Instruction(LHS), m_Constant(Step))) ||
      match(IVInc, m_ExtractValue<0>(m_Intrinsic<Intrinsic::uadd_with_overflow>(
                       m_Instruction(LHS), m_Constant(Step)))))
    return true;
  if (match(IVInc, m_Sub(m_Instruction(LHS), m_Constant(Step))) ||
      match(IVInc, m_ExtractValue<0>(m_Intrinsic<Intrinsic::usub_with_overflow>(
                       m_Instruction(LHS), m_Constant(Step))))) {
    Step = ConstantExpr::getNeg(Step);
    return true;
  }
  return false;
}

/// If given \p PN is an inductive variable with value IVInc coming from the
/// backedge, and on each iteration it gets increased by Step, return pair
/// <IVInc, Step>. Otherwise, return std::nullopt.
static std::optional<std::pair<Instruction *, Constant *>>
getIVIncrement(const PHINode *PN, const LoopInfo *LI) {
  const Loop *L = LI->getLoopFor(PN->getParent());
  if (!L || L->getHeader() != PN->getParent() || !L->getLoopLatch())
    return std::nullopt;
  auto *IVInc =
      dyn_cast<Instruction>(PN->getIncomingValueForBlock(L->getLoopLatch()));
  if (!IVInc || LI->getLoopFor(IVInc->getParent()) != L)
    return std::nullopt;
  Instruction *LHS = nullptr;
  Constant *Step = nullptr;
  if (matchIncrement(IVInc, LHS, Step) && LHS == PN)
    return std::make_pair(IVInc, Step);
  return std::nullopt;
}

static bool isIVIncrement(const Value *V, const LoopInfo *LI) {
  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;
  Instruction *LHS = nullptr;
  Constant *Step = nullptr;
  if (!matchIncrement(I, LHS, Step))
    return false;
  if (auto *PN = dyn_cast<PHINode>(LHS))
    if (auto IVInc = getIVIncrement(PN, LI))
      return IVInc->first == I;
  return false;
}

bool CodeGenPrepare::replaceMathCmpWithIntrinsic(BinaryOperator *BO,
                                                 Value *Arg0, Value *Arg1,
                                                 CmpInst *Cmp,
                                                 Intrinsic::ID IID) {
  auto IsReplacableIVIncrement = [this, &Cmp](BinaryOperator *BO) {
    if (!isIVIncrement(BO, LI))
      return false;
    const Loop *L = LI->getLoopFor(BO->getParent());
    assert(L && "L should not be null after isIVIncrement()");
    // Do not risk on moving increment into a child loop.
    if (LI->getLoopFor(Cmp->getParent()) != L)
      return false;

    // Finally, we need to ensure that the insert point will dominate all
    // existing uses of the increment.

    auto &DT = getDT(*BO->getParent()->getParent());
    if (DT.dominates(Cmp->getParent(), BO->getParent()))
      // If we're moving up the dom tree, all uses are trivially dominated.
      // (This is the common case for code produced by LSR.)
      return true;

    // Otherwise, special case the single use in the phi recurrence.
    return BO->hasOneUse() && DT.dominates(Cmp->getParent(), L->getLoopLatch());
  };
  if (BO->getParent() != Cmp->getParent() && !IsReplacableIVIncrement(BO)) {
    // We used to use a dominator tree here to allow multi-block optimization.
    // But that was problematic because:
    // 1. It could cause a perf regression by hoisting the math op into the
    //    critical path.
    // 2. It could cause a perf regression by creating a value that was live
    //    across multiple blocks and increasing register pressure.
    // 3. Use of a dominator tree could cause large compile-time regression.
    //    This is because we recompute the DT on every change in the main CGP
    //    run-loop. The recomputing is probably unnecessary in many cases, so if
    //    that was fixed, using a DT here would be ok.
    //
    // There is one important particular case we still want to handle: if BO is
    // the IV increment. Important properties that make it profitable:
    // - We can speculate IV increment anywhere in the loop (as long as the
    //   indvar Phi is its only user);
    // - Upon computing Cmp, we effectively compute something equivalent to the
    //   IV increment (despite it loops differently in the IR). So moving it up
    //   to the cmp point does not really increase register pressure.
    return false;
  }

  // We allow matching the canonical IR (add X, C) back to (usubo X, -C).
  if (BO->getOpcode() == Instruction::Add &&
      IID == Intrinsic::usub_with_overflow) {
    assert(isa<Constant>(Arg1) && "Unexpected input for usubo");
    Arg1 = ConstantExpr::getNeg(cast<Constant>(Arg1));
  }

  // Insert at the first instruction of the pair.
  Instruction *InsertPt = nullptr;
  for (Instruction &Iter : *Cmp->getParent()) {
    // If BO is an XOR, it is not guaranteed that it comes after both inputs to
    // the overflow intrinsic are defined.
    if ((BO->getOpcode() != Instruction::Xor && &Iter == BO) || &Iter == Cmp) {
      InsertPt = &Iter;
      break;
    }
  }
  assert(InsertPt != nullptr && "Parent block did not contain cmp or binop");

  IRBuilder<> Builder(InsertPt);
  Value *MathOV = Builder.CreateBinaryIntrinsic(IID, Arg0, Arg1);
  if (BO->getOpcode() != Instruction::Xor) {
    Value *Math = Builder.CreateExtractValue(MathOV, 0, "math");
    replaceAllUsesWith(BO, Math, FreshBBs, IsHugeFunc);
  } else
    assert(BO->hasOneUse() &&
           "Patterns with XOr should use the BO only in the compare");
  Value *OV = Builder.CreateExtractValue(MathOV, 1, "ov");
  replaceAllUsesWith(Cmp, OV, FreshBBs, IsHugeFunc);
  Cmp->eraseFromParent();
  BO->eraseFromParent();
  return true;
}

/// Match special-case patterns that check for unsigned add overflow.
static bool matchUAddWithOverflowConstantEdgeCases(CmpInst *Cmp,
                                                   BinaryOperator *&Add) {
  // Add = add A, 1; Cmp = icmp eq A,-1 (overflow if A is max val)
  // Add = add A,-1; Cmp = icmp ne A, 0 (overflow if A is non-zero)
  Value *A = Cmp->getOperand(0), *B = Cmp->getOperand(1);

  // We are not expecting non-canonical/degenerate code. Just bail out.
  if (isa<Constant>(A))
    return false;

  ICmpInst::Predicate Pred = Cmp->getPredicate();
  if (Pred == ICmpInst::ICMP_EQ && match(B, m_AllOnes()))
    B = ConstantInt::get(B->getType(), 1);
  else if (Pred == ICmpInst::ICMP_NE && match(B, m_ZeroInt()))
    B = ConstantInt::get(B->getType(), -1);
  else
    return false;

  // Check the users of the variable operand of the compare looking for an add
  // with the adjusted constant.
  for (User *U : A->users()) {
    if (match(U, m_Add(m_Specific(A), m_Specific(B)))) {
      Add = cast<BinaryOperator>(U);
      return true;
    }
  }
  return false;
}

/// Try to combine the compare into a call to the llvm.uadd.with.overflow
/// intrinsic. Return true if any changes were made.
bool CodeGenPrepare::combineToUAddWithOverflow(CmpInst *Cmp,
                                               ModifyDT &ModifiedDT) {
  bool EdgeCase = false;
  Value *A, *B;
  BinaryOperator *Add;
  if (!match(Cmp, m_UAddWithOverflow(m_Value(A), m_Value(B), m_BinOp(Add)))) {
    if (!matchUAddWithOverflowConstantEdgeCases(Cmp, Add))
      return false;
    // Set A and B in case we match matchUAddWithOverflowConstantEdgeCases.
    A = Add->getOperand(0);
    B = Add->getOperand(1);
    EdgeCase = true;
  }

  if (!TLI->shouldFormOverflowOp(ISD::UADDO,
                                 TLI->getValueType(*DL, Add->getType()),
                                 Add->hasNUsesOrMore(EdgeCase ? 1 : 2)))
    return false;

  // We don't want to move around uses of condition values this late, so we
  // check if it is legal to create the call to the intrinsic in the basic
  // block containing the icmp.
  if (Add->getParent() != Cmp->getParent() && !Add->hasOneUse())
    return false;

  if (!replaceMathCmpWithIntrinsic(Add, A, B, Cmp,
                                   Intrinsic::uadd_with_overflow))
    return false;

  // Reset callers - do not crash by iterating over a dead instruction.
  ModifiedDT = ModifyDT::ModifyInstDT;
  return true;
}

bool CodeGenPrepare::combineToUSubWithOverflow(CmpInst *Cmp,
                                               ModifyDT &ModifiedDT) {
  // We are not expecting non-canonical/degenerate code. Just bail out.
  Value *A = Cmp->getOperand(0), *B = Cmp->getOperand(1);
  if (isa<Constant>(A) && isa<Constant>(B))
    return false;

  // Convert (A u> B) to (A u< B) to simplify pattern matching.
  ICmpInst::Predicate Pred = Cmp->getPredicate();
  if (Pred == ICmpInst::ICMP_UGT) {
    std::swap(A, B);
    Pred = ICmpInst::ICMP_ULT;
  }
  // Convert special-case: (A == 0) is the same as (A u< 1).
  if (Pred == ICmpInst::ICMP_EQ && match(B, m_ZeroInt())) {
    B = ConstantInt::get(B->getType(), 1);
    Pred = ICmpInst::ICMP_ULT;
  }
  // Convert special-case: (A != 0) is the same as (0 u< A).
  if (Pred == ICmpInst::ICMP_NE && match(B, m_ZeroInt())) {
    std::swap(A, B);
    Pred = ICmpInst::ICMP_ULT;
  }
  if (Pred != ICmpInst::ICMP_ULT)
    return false;

  // Walk the users of a variable operand of a compare looking for a subtract or
  // add with that same operand. Also match the 2nd operand of the compare to
  // the add/sub, but that may be a negated constant operand of an add.
  Value *CmpVariableOperand = isa<Constant>(A) ? B : A;
  BinaryOperator *Sub = nullptr;
  for (User *U : CmpVariableOperand->users()) {
    // A - B, A u< B --> usubo(A, B)
    if (match(U, m_Sub(m_Specific(A), m_Specific(B)))) {
      Sub = cast<BinaryOperator>(U);
      break;
    }

    // A + (-C), A u< C (canonicalized form of (sub A, C))
    const APInt *CmpC, *AddC;
    if (match(U, m_Add(m_Specific(A), m_APInt(AddC))) &&
        match(B, m_APInt(CmpC)) && *AddC == -(*CmpC)) {
      Sub = cast<BinaryOperator>(U);
      break;
    }
  }
  if (!Sub)
    return false;

  if (!TLI->shouldFormOverflowOp(ISD::USUBO,
                                 TLI->getValueType(*DL, Sub->getType()),
                                 Sub->hasNUsesOrMore(1)))
    return false;

  if (!replaceMathCmpWithIntrinsic(Sub, Sub->getOperand(0), Sub->getOperand(1),
                                   Cmp, Intrinsic::usub_with_overflow))
    return false;

  // Reset callers - do not crash by iterating over a dead instruction.
  ModifiedDT = ModifyDT::ModifyInstDT;
  return true;
}

/// Sink the given CmpInst into user blocks to reduce the number of virtual
/// registers that must be created and coalesced. This is a clear win except on
/// targets with multiple condition code registers (PowerPC), where it might
/// lose; some adjustment may be wanted there.
///
/// Return true if any changes are made.
static bool sinkCmpExpression(CmpInst *Cmp, const TargetLowering &TLI) {
  if (TLI.hasMultipleConditionRegisters())
    return false;

  // Avoid sinking soft-FP comparisons, since this can move them into a loop.
  if (TLI.useSoftFloat() && isa<FCmpInst>(Cmp))
    return false;

  // Only insert a cmp in each block once.
  DenseMap<BasicBlock *, CmpInst *> InsertedCmps;

  bool MadeChange = false;
  for (Value::user_iterator UI = Cmp->user_begin(), E = Cmp->user_end();
       UI != E;) {
    Use &TheUse = UI.getUse();
    Instruction *User = cast<Instruction>(*UI);

    // Preincrement use iterator so we don't invalidate it.
    ++UI;

    // Don't bother for PHI nodes.
    if (isa<PHINode>(User))
      continue;

    // Figure out which BB this cmp is used in.
    BasicBlock *UserBB = User->getParent();
    BasicBlock *DefBB = Cmp->getParent();

    // If this user is in the same block as the cmp, don't change the cmp.
    if (UserBB == DefBB)
      continue;

    // If we have already inserted a cmp into this block, use it.
    CmpInst *&InsertedCmp = InsertedCmps[UserBB];

    if (!InsertedCmp) {
      BasicBlock::iterator InsertPt = UserBB->getFirstInsertionPt();
      assert(InsertPt != UserBB->end());
      InsertedCmp = CmpInst::Create(Cmp->getOpcode(), Cmp->getPredicate(),
                                    Cmp->getOperand(0), Cmp->getOperand(1), "");
      InsertedCmp->insertBefore(*UserBB, InsertPt);
      // Propagate the debug info.
      InsertedCmp->setDebugLoc(Cmp->getDebugLoc());
    }

    // Replace a use of the cmp with a use of the new cmp.
    TheUse = InsertedCmp;
    MadeChange = true;
    ++NumCmpUses;
  }

  // If we removed all uses, nuke the cmp.
  if (Cmp->use_empty()) {
    Cmp->eraseFromParent();
    MadeChange = true;
  }

  return MadeChange;
}

/// For pattern like:
///
///   DomCond = icmp sgt/slt CmpOp0, CmpOp1 (might not be in DomBB)
///   ...
/// DomBB:
///   ...
///   br DomCond, TrueBB, CmpBB
/// CmpBB: (with DomBB being the single predecessor)
///   ...
///   Cmp = icmp eq CmpOp0, CmpOp1
///   ...
///
/// It would use two comparison on targets that lowering of icmp sgt/slt is
/// different from lowering of icmp eq (PowerPC). This function try to convert
/// 'Cmp = icmp eq CmpOp0, CmpOp1' to ' Cmp = icmp slt/sgt CmpOp0, CmpOp1'.
/// After that, DomCond and Cmp can use the same comparison so reduce one
/// comparison.
///
/// Return true if any changes are made.
static bool foldICmpWithDominatingICmp(CmpInst *Cmp,
                                       const TargetLowering &TLI) {
  if (!EnableICMP_EQToICMP_ST && TLI.isEqualityCmpFoldedWithSignedCmp())
    return false;

  ICmpInst::Predicate Pred = Cmp->getPredicate();
  if (Pred != ICmpInst::ICMP_EQ)
    return false;

  // If icmp eq has users other than BranchInst and SelectInst, converting it to
  // icmp slt/sgt would introduce more redundant LLVM IR.
  for (User *U : Cmp->users()) {
    if (isa<BranchInst>(U))
      continue;
    if (isa<SelectInst>(U) && cast<SelectInst>(U)->getCondition() == Cmp)
      continue;
    return false;
  }

  // This is a cheap/incomplete check for dominance - just match a single
  // predecessor with a conditional branch.
  BasicBlock *CmpBB = Cmp->getParent();
  BasicBlock *DomBB = CmpBB->getSinglePredecessor();
  if (!DomBB)
    return false;

  // We want to ensure that the only way control gets to the comparison of
  // interest is that a less/greater than comparison on the same operands is
  // false.
  Value *DomCond;
  BasicBlock *TrueBB, *FalseBB;
  if (!match(DomBB->getTerminator(), m_Br(m_Value(DomCond), TrueBB, FalseBB)))
    return false;
  if (CmpBB != FalseBB)
    return false;

  Value *CmpOp0 = Cmp->getOperand(0), *CmpOp1 = Cmp->getOperand(1);
  ICmpInst::Predicate DomPred;
  if (!match(DomCond, m_ICmp(DomPred, m_Specific(CmpOp0), m_Specific(CmpOp1))))
    return false;
  if (DomPred != ICmpInst::ICMP_SGT && DomPred != ICmpInst::ICMP_SLT)
    return false;

  // Convert the equality comparison to the opposite of the dominating
  // comparison and swap the direction for all branch/select users.
  // We have conceptually converted:
  // Res = (a < b) ? <LT_RES> : (a == b) ? <EQ_RES> : <GT_RES>;
  // to
  // Res = (a < b) ? <LT_RES> : (a > b)  ? <GT_RES> : <EQ_RES>;
  // And similarly for branches.
  for (User *U : Cmp->users()) {
    if (auto *BI = dyn_cast<BranchInst>(U)) {
      assert(BI->isConditional() && "Must be conditional");
      BI->swapSuccessors();
      continue;
    }
    if (auto *SI = dyn_cast<SelectInst>(U)) {
      // Swap operands
      SI->swapValues();
      SI->swapProfMetadata();
      continue;
    }
    llvm_unreachable("Must be a branch or a select");
  }
  Cmp->setPredicate(CmpInst::getSwappedPredicate(DomPred));
  return true;
}

/// Many architectures use the same instruction for both subtract and cmp. Try
/// to swap cmp operands to match subtract operations to allow for CSE.
static bool swapICmpOperandsToExposeCSEOpportunities(CmpInst *Cmp) {
  Value *Op0 = Cmp->getOperand(0);
  Value *Op1 = Cmp->getOperand(1);
  if (!Op0->getType()->isIntegerTy() || isa<Constant>(Op0) ||
      isa<Constant>(Op1) || Op0 == Op1)
    return false;

  // If a subtract already has the same operands as a compare, swapping would be
  // bad. If a subtract has the same operands as a compare but in reverse order,
  // then swapping is good.
  int GoodToSwap = 0;
  unsigned NumInspected = 0;
  for (const User *U : Op0->users()) {
    // Avoid walking many users.
    if (++NumInspected > 128)
      return false;
    if (match(U, m_Sub(m_Specific(Op1), m_Specific(Op0))))
      GoodToSwap++;
    else if (match(U, m_Sub(m_Specific(Op0), m_Specific(Op1))))
      GoodToSwap--;
  }

  if (GoodToSwap > 0) {
    Cmp->swapOperands();
    return true;
  }
  return false;
}

static bool foldFCmpToFPClassTest(CmpInst *Cmp, const TargetLowering &TLI,
                                  const DataLayout &DL) {
  FCmpInst *FCmp = dyn_cast<FCmpInst>(Cmp);
  if (!FCmp)
    return false;

  // Don't fold if the target offers free fabs and the predicate is legal.
  EVT VT = TLI.getValueType(DL, Cmp->getOperand(0)->getType());
  if (TLI.isFAbsFree(VT) &&
      TLI.isCondCodeLegal(getFCmpCondCode(FCmp->getPredicate()),
                          VT.getSimpleVT()))
    return false;

  // Reverse the canonicalization if it is a FP class test
  auto ShouldReverseTransform = [](FPClassTest ClassTest) {
    return ClassTest == fcInf || ClassTest == (fcInf | fcNan);
  };
  auto [ClassVal, ClassTest] =
      fcmpToClassTest(FCmp->getPredicate(), *FCmp->getParent()->getParent(),
                      FCmp->getOperand(0), FCmp->getOperand(1));
  if (!ClassVal)
    return false;

  if (!ShouldReverseTransform(ClassTest) && !ShouldReverseTransform(~ClassTest))
    return false;

  IRBuilder<> Builder(Cmp);
  Value *IsFPClass = Builder.createIsFPClass(ClassVal, ClassTest);
  Cmp->replaceAllUsesWith(IsFPClass);
  RecursivelyDeleteTriviallyDeadInstructions(Cmp);
  return true;
}

bool CodeGenPrepare::optimizeCmp(CmpInst *Cmp, ModifyDT &ModifiedDT) {
  if (sinkCmpExpression(Cmp, *TLI))
    return true;

  if (combineToUAddWithOverflow(Cmp, ModifiedDT))
    return true;

  if (combineToUSubWithOverflow(Cmp, ModifiedDT))
    return true;

  if (foldICmpWithDominatingICmp(Cmp, *TLI))
    return true;

  if (swapICmpOperandsToExposeCSEOpportunities(Cmp))
    return true;

  if (foldFCmpToFPClassTest(Cmp, *TLI, *DL))
    return true;

  return false;
}

/// Duplicate and sink the given 'and' instruction into user blocks where it is
/// used in a compare to allow isel to generate better code for targets where
/// this operation can be combined.
///
/// Return true if any changes are made.
static bool sinkAndCmp0Expression(Instruction *AndI, const TargetLowering &TLI,
                                  SetOfInstrs &InsertedInsts) {
  // Double-check that we're not trying to optimize an instruction that was
  // already optimized by some other part of this pass.
  assert(!InsertedInsts.count(AndI) &&
         "Attempting to optimize already optimized and instruction");
  (void)InsertedInsts;

  // Nothing to do for single use in same basic block.
  if (AndI->hasOneUse() &&
      AndI->getParent() == cast<Instruction>(*AndI->user_begin())->getParent())
    return false;

  // Try to avoid cases where sinking/duplicating is likely to increase register
  // pressure.
  if (!isa<ConstantInt>(AndI->getOperand(0)) &&
      !isa<ConstantInt>(AndI->getOperand(1)) &&
      AndI->getOperand(0)->hasOneUse() && AndI->getOperand(1)->hasOneUse())
    return false;

  for (auto *U : AndI->users()) {
    Instruction *User = cast<Instruction>(U);

    // Only sink 'and' feeding icmp with 0.
    if (!isa<ICmpInst>(User))
      return false;

    auto *CmpC = dyn_cast<ConstantInt>(User->getOperand(1));
    if (!CmpC || !CmpC->isZero())
      return false;
  }

  if (!TLI.isMaskAndCmp0FoldingBeneficial(*AndI))
    return false;

  LLVM_DEBUG(dbgs() << "found 'and' feeding only icmp 0;\n");
  LLVM_DEBUG(AndI->getParent()->dump());

  // Push the 'and' into the same block as the icmp 0.  There should only be
  // one (icmp (and, 0)) in each block, since CSE/GVN should have removed any
  // others, so we don't need to keep track of which BBs we insert into.
  for (Value::user_iterator UI = AndI->user_begin(), E = AndI->user_end();
       UI != E;) {
    Use &TheUse = UI.getUse();
    Instruction *User = cast<Instruction>(*UI);

    // Preincrement use iterator so we don't invalidate it.
    ++UI;

    LLVM_DEBUG(dbgs() << "sinking 'and' use: " << *User << "\n");

    // Keep the 'and' in the same place if the use is already in the same block.
    Instruction *InsertPt =
        User->getParent() == AndI->getParent() ? AndI : User;
    Instruction *InsertedAnd = BinaryOperator::Create(
        Instruction::And, AndI->getOperand(0), AndI->getOperand(1), "",
        InsertPt->getIterator());
    // Propagate the debug info.
    InsertedAnd->setDebugLoc(AndI->getDebugLoc());

    // Replace a use of the 'and' with a use of the new 'and'.
    TheUse = InsertedAnd;
    ++NumAndUses;
    LLVM_DEBUG(User->getParent()->dump());
  }

  // We removed all uses, nuke the and.
  AndI->eraseFromParent();
  return true;
}

/// Check if the candidates could be combined with a shift instruction, which
/// includes:
/// 1. Truncate instruction
/// 2. And instruction and the imm is a mask of the low bits:
/// imm & (imm+1) == 0
static bool isExtractBitsCandidateUse(Instruction *User) {
  if (!isa<TruncInst>(User)) {
    if (User->getOpcode() != Instruction::And ||
        !isa<ConstantInt>(User->getOperand(1)))
      return false;

    const APInt &Cimm = cast<ConstantInt>(User->getOperand(1))->getValue();

    if ((Cimm & (Cimm + 1)).getBoolValue())
      return false;
  }
  return true;
}

/// Sink both shift and truncate instruction to the use of truncate's BB.
static bool
SinkShiftAndTruncate(BinaryOperator *ShiftI, Instruction *User, ConstantInt *CI,
                     DenseMap<BasicBlock *, BinaryOperator *> &InsertedShifts,
                     const TargetLowering &TLI, const DataLayout &DL) {
  BasicBlock *UserBB = User->getParent();
  DenseMap<BasicBlock *, CastInst *> InsertedTruncs;
  auto *TruncI = cast<TruncInst>(User);
  bool MadeChange = false;

  for (Value::user_iterator TruncUI = TruncI->user_begin(),
                            TruncE = TruncI->user_end();
       TruncUI != TruncE;) {

    Use &TruncTheUse = TruncUI.getUse();
    Instruction *TruncUser = cast<Instruction>(*TruncUI);
    // Preincrement use iterator so we don't invalidate it.

    ++TruncUI;

    int ISDOpcode = TLI.InstructionOpcodeToISD(TruncUser->getOpcode());
    if (!ISDOpcode)
      continue;

    // If the use is actually a legal node, there will not be an
    // implicit truncate.
    // FIXME: always querying the result type is just an
    // approximation; some nodes' legality is determined by the
    // operand or other means. There's no good way to find out though.
    if (TLI.isOperationLegalOrCustom(
            ISDOpcode, TLI.getValueType(DL, TruncUser->getType(), true)))
      continue;

    // Don't bother for PHI nodes.
    if (isa<PHINode>(TruncUser))
      continue;

    BasicBlock *TruncUserBB = TruncUser->getParent();

    if (UserBB == TruncUserBB)
      continue;

    BinaryOperator *&InsertedShift = InsertedShifts[TruncUserBB];
    CastInst *&InsertedTrunc = InsertedTruncs[TruncUserBB];

    if (!InsertedShift && !InsertedTrunc) {
      BasicBlock::iterator InsertPt = TruncUserBB->getFirstInsertionPt();
      assert(InsertPt != TruncUserBB->end());
      // Sink the shift
      if (ShiftI->getOpcode() == Instruction::AShr)
        InsertedShift =
            BinaryOperator::CreateAShr(ShiftI->getOperand(0), CI, "");
      else
        InsertedShift =
            BinaryOperator::CreateLShr(ShiftI->getOperand(0), CI, "");
      InsertedShift->setDebugLoc(ShiftI->getDebugLoc());
      InsertedShift->insertBefore(*TruncUserBB, InsertPt);

      // Sink the trunc
      BasicBlock::iterator TruncInsertPt = TruncUserBB->getFirstInsertionPt();
      TruncInsertPt++;
      // It will go ahead of any debug-info.
      TruncInsertPt.setHeadBit(true);
      assert(TruncInsertPt != TruncUserBB->end());

      InsertedTrunc = CastInst::Create(TruncI->getOpcode(), InsertedShift,
                                       TruncI->getType(), "");
      InsertedTrunc->insertBefore(*TruncUserBB, TruncInsertPt);
      InsertedTrunc->setDebugLoc(TruncI->getDebugLoc());

      MadeChange = true;

      TruncTheUse = InsertedTrunc;
    }
  }
  return MadeChange;
}

/// Sink the shift *right* instruction into user blocks if the uses could
/// potentially be combined with this shift instruction and generate BitExtract
/// instruction. It will only be applied if the architecture supports BitExtract
/// instruction. Here is an example:
/// BB1:
///   %x.extract.shift = lshr i64 %arg1, 32
/// BB2:
///   %x.extract.trunc = trunc i64 %x.extract.shift to i16
/// ==>
///
/// BB2:
///   %x.extract.shift.1 = lshr i64 %arg1, 32
///   %x.extract.trunc = trunc i64 %x.extract.shift.1 to i16
///
/// CodeGen will recognize the pattern in BB2 and generate BitExtract
/// instruction.
/// Return true if any changes are made.
static bool OptimizeExtractBits(BinaryOperator *ShiftI, ConstantInt *CI,
                                const TargetLowering &TLI,
                                const DataLayout &DL) {
  BasicBlock *DefBB = ShiftI->getParent();

  /// Only insert instructions in each block once.
  DenseMap<BasicBlock *, BinaryOperator *> InsertedShifts;

  bool shiftIsLegal = TLI.isTypeLegal(TLI.getValueType(DL, ShiftI->getType()));

  bool MadeChange = false;
  for (Value::user_iterator UI = ShiftI->user_begin(), E = ShiftI->user_end();
       UI != E;) {
    Use &TheUse = UI.getUse();
    Instruction *User = cast<Instruction>(*UI);
    // Preincrement use iterator so we don't invalidate it.
    ++UI;

    // Don't bother for PHI nodes.
    if (isa<PHINode>(User))
      continue;

    if (!isExtractBitsCandidateUse(User))
      continue;

    BasicBlock *UserBB = User->getParent();

    if (UserBB == DefBB) {
      // If the shift and truncate instruction are in the same BB. The use of
      // the truncate(TruncUse) may still introduce another truncate if not
      // legal. In this case, we would like to sink both shift and truncate
      // instruction to the BB of TruncUse.
      // for example:
      // BB1:
      // i64 shift.result = lshr i64 opnd, imm
      // trunc.result = trunc shift.result to i16
      //
      // BB2:
      //   ----> We will have an implicit truncate here if the architecture does
      //   not have i16 compare.
      // cmp i16 trunc.result, opnd2
      //
      if (isa<TruncInst>(User) &&
          shiftIsLegal
          // If the type of the truncate is legal, no truncate will be
          // introduced in other basic blocks.
          && (!TLI.isTypeLegal(TLI.getValueType(DL, User->getType()))))
        MadeChange =
            SinkShiftAndTruncate(ShiftI, User, CI, InsertedShifts, TLI, DL);

      continue;
    }
    // If we have already inserted a shift into this block, use it.
    BinaryOperator *&InsertedShift = InsertedShifts[UserBB];

    if (!InsertedShift) {
      BasicBlock::iterator InsertPt = UserBB->getFirstInsertionPt();
      assert(InsertPt != UserBB->end());

      if (ShiftI->getOpcode() == Instruction::AShr)
        InsertedShift =
            BinaryOperator::CreateAShr(ShiftI->getOperand(0), CI, "");
      else
        InsertedShift =
            BinaryOperator::CreateLShr(ShiftI->getOperand(0), CI, "");
      InsertedShift->insertBefore(*UserBB, InsertPt);
      InsertedShift->setDebugLoc(ShiftI->getDebugLoc());

      MadeChange = true;
    }

    // Replace a use of the shift with a use of the new shift.
    TheUse = InsertedShift;
  }

  // If we removed all uses, or there are none, nuke the shift.
  if (ShiftI->use_empty()) {
    salvageDebugInfo(*ShiftI);
    ShiftI->eraseFromParent();
    MadeChange = true;
  }

  return MadeChange;
}

/// If counting leading or trailing zeros is an expensive operation and a zero
/// input is defined, add a check for zero to avoid calling the intrinsic.
///
/// We want to transform:
///     %z = call i64 @llvm.cttz.i64(i64 %A, i1 false)
///
/// into:
///   entry:
///     %cmpz = icmp eq i64 %A, 0
///     br i1 %cmpz, label %cond.end, label %cond.false
///   cond.false:
///     %z = call i64 @llvm.cttz.i64(i64 %A, i1 true)
///     br label %cond.end
///   cond.end:
///     %ctz = phi i64 [ 64, %entry ], [ %z, %cond.false ]
///
/// If the transform is performed, return true and set ModifiedDT to true.
static bool despeculateCountZeros(IntrinsicInst *CountZeros,
                                  LoopInfo &LI,
                                  const TargetLowering *TLI,
                                  const DataLayout *DL, ModifyDT &ModifiedDT,
                                  SmallSet<BasicBlock *, 32> &FreshBBs,
                                  bool IsHugeFunc) {
  // If a zero input is undefined, it doesn't make sense to despeculate that.
  if (match(CountZeros->getOperand(1), m_One()))
    return false;

  // If it's cheap to speculate, there's nothing to do.
  Type *Ty = CountZeros->getType();
  auto IntrinsicID = CountZeros->getIntrinsicID();
  if ((IntrinsicID == Intrinsic::cttz && TLI->isCheapToSpeculateCttz(Ty)) ||
      (IntrinsicID == Intrinsic::ctlz && TLI->isCheapToSpeculateCtlz(Ty)))
    return false;

  // Only handle legal scalar cases. Anything else requires too much work.
  unsigned SizeInBits = Ty->getScalarSizeInBits();
  if (Ty->isVectorTy() || SizeInBits > DL->getLargestLegalIntTypeSizeInBits())
    return false;

  // Bail if the value is never zero.
  Use &Op = CountZeros->getOperandUse(0);
  if (isKnownNonZero(Op, *DL))
    return false;

  // The intrinsic will be sunk behind a compare against zero and branch.
  BasicBlock *StartBlock = CountZeros->getParent();
  BasicBlock *CallBlock = StartBlock->splitBasicBlock(CountZeros, "cond.false");
  if (IsHugeFunc)
    FreshBBs.insert(CallBlock);

  // Create another block after the count zero intrinsic. A PHI will be added
  // in this block to select the result of the intrinsic or the bit-width
  // constant if the input to the intrinsic is zero.
  BasicBlock::iterator SplitPt = std::next(BasicBlock::iterator(CountZeros));
  // Any debug-info after CountZeros should not be included.
  SplitPt.setHeadBit(true);
  BasicBlock *EndBlock = CallBlock->splitBasicBlock(SplitPt, "cond.end");
  if (IsHugeFunc)
    FreshBBs.insert(EndBlock);

  // Update the LoopInfo. The new blocks are in the same loop as the start
  // block.
  if (Loop *L = LI.getLoopFor(StartBlock)) {
    L->addBasicBlockToLoop(CallBlock, LI);
    L->addBasicBlockToLoop(EndBlock, LI);
  }

  // Set up a builder to create a compare, conditional branch, and PHI.
  IRBuilder<> Builder(CountZeros->getContext());
  Builder.SetInsertPoint(StartBlock->getTerminator());
  Builder.SetCurrentDebugLocation(CountZeros->getDebugLoc());

  // Replace the unconditional branch that was created by the first split with
  // a compare against zero and a conditional branch.
  Value *Zero = Constant::getNullValue(Ty);
  // Avoid introducing branch on poison. This also replaces the ctz operand.
  if (!isGuaranteedNotToBeUndefOrPoison(Op))
    Op = Builder.CreateFreeze(Op, Op->getName() + ".fr");
  Value *Cmp = Builder.CreateICmpEQ(Op, Zero, "cmpz");
  Builder.CreateCondBr(Cmp, EndBlock, CallBlock);
  StartBlock->getTerminator()->eraseFromParent();

  // Create a PHI in the end block to select either the output of the intrinsic
  // or the bit width of the operand.
  Builder.SetInsertPoint(EndBlock, EndBlock->begin());
  PHINode *PN = Builder.CreatePHI(Ty, 2, "ctz");
  replaceAllUsesWith(CountZeros, PN, FreshBBs, IsHugeFunc);
  Value *BitWidth = Builder.getInt(APInt(SizeInBits, SizeInBits));
  PN->addIncoming(BitWidth, StartBlock);
  PN->addIncoming(CountZeros, CallBlock);

  // We are explicitly handling the zero case, so we can set the intrinsic's
  // undefined zero argument to 'true'. This will also prevent reprocessing the
  // intrinsic; we only despeculate when a zero input is defined.
  CountZeros->setArgOperand(1, Builder.getTrue());
  ModifiedDT = ModifyDT::ModifyBBDT;
  return true;
}

bool CodeGenPrepare::optimizeCallInst(CallInst *CI, ModifyDT &ModifiedDT) {
  BasicBlock *BB = CI->getParent();

  // Lower inline assembly if we can.
  // If we found an inline asm expession, and if the target knows how to
  // lower it to normal LLVM code, do so now.
  if (CI->isInlineAsm()) {
    if (TLI->ExpandInlineAsm(CI)) {
      // Avoid invalidating the iterator.
      CurInstIterator = BB->begin();
      // Avoid processing instructions out of order, which could cause
      // reuse before a value is defined.
      SunkAddrs.clear();
      return true;
    }
    // Sink address computing for memory operands into the block.
    if (optimizeInlineAsmInst(CI))
      return true;
  }

  // Align the pointer arguments to this call if the target thinks it's a good
  // idea
  unsigned MinSize;
  Align PrefAlign;
  if (TLI->shouldAlignPointerArgs(CI, MinSize, PrefAlign)) {
    for (auto &Arg : CI->args()) {
      // We want to align both objects whose address is used directly and
      // objects whose address is used in casts and GEPs, though it only makes
      // sense for GEPs if the offset is a multiple of the desired alignment and
      // if size - offset meets the size threshold.
      if (!Arg->getType()->isPointerTy())
        continue;
      APInt Offset(DL->getIndexSizeInBits(
                       cast<PointerType>(Arg->getType())->getAddressSpace()),
                   0);
      Value *Val = Arg->stripAndAccumulateInBoundsConstantOffsets(*DL, Offset);
      uint64_t Offset2 = Offset.getLimitedValue();
      if (!isAligned(PrefAlign, Offset2))
        continue;
      AllocaInst *AI;
      if ((AI = dyn_cast<AllocaInst>(Val)) && AI->getAlign() < PrefAlign &&
          DL->getTypeAllocSize(AI->getAllocatedType()) >= MinSize + Offset2)
        AI->setAlignment(PrefAlign);
      // Global variables can only be aligned if they are defined in this
      // object (i.e. they are uniquely initialized in this object), and
      // over-aligning global variables that have an explicit section is
      // forbidden.
      GlobalVariable *GV;
      if ((GV = dyn_cast<GlobalVariable>(Val)) && GV->canIncreaseAlignment() &&
          GV->getPointerAlignment(*DL) < PrefAlign &&
          DL->getTypeAllocSize(GV->getValueType()) >= MinSize + Offset2)
        GV->setAlignment(PrefAlign);
    }
  }
  // If this is a memcpy (or similar) then we may be able to improve the
  // alignment.
  if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(CI)) {
    Align DestAlign = getKnownAlignment(MI->getDest(), *DL);
    MaybeAlign MIDestAlign = MI->getDestAlign();
    if (!MIDestAlign || DestAlign > *MIDestAlign)
      MI->setDestAlignment(DestAlign);
    if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(MI)) {
      MaybeAlign MTISrcAlign = MTI->getSourceAlign();
      Align SrcAlign = getKnownAlignment(MTI->getSource(), *DL);
      if (!MTISrcAlign || SrcAlign > *MTISrcAlign)
        MTI->setSourceAlignment(SrcAlign);
    }
  }

  // If we have a cold call site, try to sink addressing computation into the
  // cold block.  This interacts with our handling for loads and stores to
  // ensure that we can fold all uses of a potential addressing computation
  // into their uses.  TODO: generalize this to work over profiling data
  if (CI->hasFnAttr(Attribute::Cold) && !OptSize &&
      !llvm::shouldOptimizeForSize(BB, PSI, BFI.get()))
    for (auto &Arg : CI->args()) {
      if (!Arg->getType()->isPointerTy())
        continue;
      unsigned AS = Arg->getType()->getPointerAddressSpace();
      if (optimizeMemoryInst(CI, Arg, Arg->getType(), AS))
        return true;
    }

  IntrinsicInst *II = dyn_cast<IntrinsicInst>(CI);
  if (II) {
    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::assume:
      llvm_unreachable("llvm.assume should have been removed already");
    case Intrinsic::allow_runtime_check:
    case Intrinsic::allow_ubsan_check:
    case Intrinsic::experimental_widenable_condition: {
      // Give up on future widening opportunities so that we can fold away dead
      // paths and merge blocks before going into block-local instruction
      // selection.
      if (II->use_empty()) {
        II->eraseFromParent();
        return true;
      }
      Constant *RetVal = ConstantInt::getTrue(II->getContext());
      resetIteratorIfInvalidatedWhileCalling(BB, [&]() {
        replaceAndRecursivelySimplify(CI, RetVal, TLInfo, nullptr);
      });
      return true;
    }
    case Intrinsic::objectsize:
      llvm_unreachable("llvm.objectsize.* should have been lowered already");
    case Intrinsic::is_constant:
      llvm_unreachable("llvm.is.constant.* should have been lowered already");
    case Intrinsic::aarch64_stlxr:
    case Intrinsic::aarch64_stxr: {
      ZExtInst *ExtVal = dyn_cast<ZExtInst>(CI->getArgOperand(0));
      if (!ExtVal || !ExtVal->hasOneUse() ||
          ExtVal->getParent() == CI->getParent())
        return false;
      // Sink a zext feeding stlxr/stxr before it, so it can be folded into it.
      ExtVal->moveBefore(CI);
      // Mark this instruction as "inserted by CGP", so that other
      // optimizations don't touch it.
      InsertedInsts.insert(ExtVal);
      return true;
    }

    case Intrinsic::launder_invariant_group:
    case Intrinsic::strip_invariant_group: {
      Value *ArgVal = II->getArgOperand(0);
      auto it = LargeOffsetGEPMap.find(II);
      if (it != LargeOffsetGEPMap.end()) {
        // Merge entries in LargeOffsetGEPMap to reflect the RAUW.
        // Make sure not to have to deal with iterator invalidation
        // after possibly adding ArgVal to LargeOffsetGEPMap.
        auto GEPs = std::move(it->second);
        LargeOffsetGEPMap[ArgVal].append(GEPs.begin(), GEPs.end());
        LargeOffsetGEPMap.erase(II);
      }

      replaceAllUsesWith(II, ArgVal, FreshBBs, IsHugeFunc);
      II->eraseFromParent();
      return true;
    }
    case Intrinsic::cttz:
    case Intrinsic::ctlz:
      // If counting zeros is expensive, try to avoid it.
      return despeculateCountZeros(II, *LI, TLI, DL, ModifiedDT, FreshBBs,
                                   IsHugeFunc);
    case Intrinsic::fshl:
    case Intrinsic::fshr:
      return optimizeFunnelShift(II);
    case Intrinsic::dbg_assign:
    case Intrinsic::dbg_value:
      return fixupDbgValue(II);
    case Intrinsic::masked_gather:
      return optimizeGatherScatterInst(II, II->getArgOperand(0));
    case Intrinsic::masked_scatter:
      return optimizeGatherScatterInst(II, II->getArgOperand(1));
    }

    SmallVector<Value *, 2> PtrOps;
    Type *AccessTy;
    if (TLI->getAddrModeArguments(II, PtrOps, AccessTy))
      while (!PtrOps.empty()) {
        Value *PtrVal = PtrOps.pop_back_val();
        unsigned AS = PtrVal->getType()->getPointerAddressSpace();
        if (optimizeMemoryInst(II, PtrVal, AccessTy, AS))
          return true;
      }
  }

  // From here on out we're working with named functions.
  if (!CI->getCalledFunction())
    return false;

  // Lower all default uses of _chk calls.  This is very similar
  // to what InstCombineCalls does, but here we are only lowering calls
  // to fortified library functions (e.g. __memcpy_chk) that have the default
  // "don't know" as the objectsize.  Anything else should be left alone.
  FortifiedLibCallSimplifier Simplifier(TLInfo, true);
  IRBuilder<> Builder(CI);
  if (Value *V = Simplifier.optimizeCall(CI, Builder)) {
    replaceAllUsesWith(CI, V, FreshBBs, IsHugeFunc);
    CI->eraseFromParent();
    return true;
  }

  return false;
}

static bool isIntrinsicOrLFToBeTailCalled(const TargetLibraryInfo *TLInfo,
                                          const CallInst *CI) {
  assert(CI && CI->use_empty());

  if (const auto *II = dyn_cast<IntrinsicInst>(CI))
    switch (II->getIntrinsicID()) {
    case Intrinsic::memset:
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
      return true;
    default:
      return false;
    }

  LibFunc LF;
  Function *Callee = CI->getCalledFunction();
  if (Callee && TLInfo && TLInfo->getLibFunc(*Callee, LF))
    switch (LF) {
    case LibFunc_strcpy:
    case LibFunc_strncpy:
    case LibFunc_strcat:
    case LibFunc_strncat:
      return true;
    default:
      return false;
    }

  return false;
}

/// Look for opportunities to duplicate return instructions to the predecessor
/// to enable tail call optimizations. The case it is currently looking for is
/// the following one. Known intrinsics or library function that may be tail
/// called are taken into account as well.
/// @code
/// bb0:
///   %tmp0 = tail call i32 @f0()
///   br label %return
/// bb1:
///   %tmp1 = tail call i32 @f1()
///   br label %return
/// bb2:
///   %tmp2 = tail call i32 @f2()
///   br label %return
/// return:
///   %retval = phi i32 [ %tmp0, %bb0 ], [ %tmp1, %bb1 ], [ %tmp2, %bb2 ]
///   ret i32 %retval
/// @endcode
///
/// =>
///
/// @code
/// bb0:
///   %tmp0 = tail call i32 @f0()
///   ret i32 %tmp0
/// bb1:
///   %tmp1 = tail call i32 @f1()
///   ret i32 %tmp1
/// bb2:
///   %tmp2 = tail call i32 @f2()
///   ret i32 %tmp2
/// @endcode
bool CodeGenPrepare::dupRetToEnableTailCallOpts(BasicBlock *BB,
                                                ModifyDT &ModifiedDT) {
  if (!BB->getTerminator())
    return false;

  ReturnInst *RetI = dyn_cast<ReturnInst>(BB->getTerminator());
  if (!RetI)
    return false;

  assert(LI->getLoopFor(BB) == nullptr && "A return block cannot be in a loop");

  PHINode *PN = nullptr;
  ExtractValueInst *EVI = nullptr;
  BitCastInst *BCI = nullptr;
  Value *V = RetI->getReturnValue();
  if (V) {
    BCI = dyn_cast<BitCastInst>(V);
    if (BCI)
      V = BCI->getOperand(0);

    EVI = dyn_cast<ExtractValueInst>(V);
    if (EVI) {
      V = EVI->getOperand(0);
      if (!llvm::all_of(EVI->indices(), [](unsigned idx) { return idx == 0; }))
        return false;
    }

    PN = dyn_cast<PHINode>(V);
  }

  if (PN && PN->getParent() != BB)
    return false;

  auto isLifetimeEndOrBitCastFor = [](const Instruction *Inst) {
    const BitCastInst *BC = dyn_cast<BitCastInst>(Inst);
    if (BC && BC->hasOneUse())
      Inst = BC->user_back();

    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst))
      return II->getIntrinsicID() == Intrinsic::lifetime_end;
    return false;
  };

  // Make sure there are no instructions between the first instruction
  // and return.
  const Instruction *BI = BB->getFirstNonPHI();
  // Skip over debug and the bitcast.
  while (isa<DbgInfoIntrinsic>(BI) || BI == BCI || BI == EVI ||
         isa<PseudoProbeInst>(BI) || isLifetimeEndOrBitCastFor(BI))
    BI = BI->getNextNode();
  if (BI != RetI)
    return false;

  /// Only dup the ReturnInst if the CallInst is likely to be emitted as a tail
  /// call.
  const Function *F = BB->getParent();
  SmallVector<BasicBlock *, 4> TailCallBBs;
  if (PN) {
    for (unsigned I = 0, E = PN->getNumIncomingValues(); I != E; ++I) {
      // Look through bitcasts.
      Value *IncomingVal = PN->getIncomingValue(I)->stripPointerCasts();
      CallInst *CI = dyn_cast<CallInst>(IncomingVal);
      BasicBlock *PredBB = PN->getIncomingBlock(I);
      // Make sure the phi value is indeed produced by the tail call.
      if (CI && CI->hasOneUse() && CI->getParent() == PredBB &&
          TLI->mayBeEmittedAsTailCall(CI) &&
          attributesPermitTailCall(F, CI, RetI, *TLI)) {
        TailCallBBs.push_back(PredBB);
      } else {
        // Consider the cases in which the phi value is indirectly produced by
        // the tail call, for example when encountering memset(), memmove(),
        // strcpy(), whose return value may have been optimized out. In such
        // cases, the value needs to be the first function argument.
        //
        // bb0:
        //   tail call void @llvm.memset.p0.i64(ptr %0, i8 0, i64 %1)
        //   br label %return
        // return:
        //   %phi = phi ptr [ %0, %bb0 ], [ %2, %entry ]
        if (PredBB && PredBB->getSingleSuccessor() == BB)
          CI = dyn_cast_or_null<CallInst>(
              PredBB->getTerminator()->getPrevNonDebugInstruction(true));

        if (CI && CI->use_empty() &&
            isIntrinsicOrLFToBeTailCalled(TLInfo, CI) &&
            IncomingVal == CI->getArgOperand(0) &&
            TLI->mayBeEmittedAsTailCall(CI) &&
            attributesPermitTailCall(F, CI, RetI, *TLI))
          TailCallBBs.push_back(PredBB);
      }
    }
  } else {
    SmallPtrSet<BasicBlock *, 4> VisitedBBs;
    for (BasicBlock *Pred : predecessors(BB)) {
      if (!VisitedBBs.insert(Pred).second)
        continue;
      if (Instruction *I = Pred->rbegin()->getPrevNonDebugInstruction(true)) {
        CallInst *CI = dyn_cast<CallInst>(I);
        if (CI && CI->use_empty() && TLI->mayBeEmittedAsTailCall(CI) &&
            attributesPermitTailCall(F, CI, RetI, *TLI)) {
          // Either we return void or the return value must be the first
          // argument of a known intrinsic or library function.
          if (!V || isa<UndefValue>(V) ||
              (isIntrinsicOrLFToBeTailCalled(TLInfo, CI) &&
               V == CI->getArgOperand(0))) {
            TailCallBBs.push_back(Pred);
          }
        }
      }
    }
  }

  bool Changed = false;
  for (auto const &TailCallBB : TailCallBBs) {
    // Make sure the call instruction is followed by an unconditional branch to
    // the return block.
    BranchInst *BI = dyn_cast<BranchInst>(TailCallBB->getTerminator());
    if (!BI || !BI->isUnconditional() || BI->getSuccessor(0) != BB)
      continue;

    // Duplicate the return into TailCallBB.
    (void)FoldReturnIntoUncondBranch(RetI, BB, TailCallBB);
    assert(!VerifyBFIUpdates ||
           BFI->getBlockFreq(BB) >= BFI->getBlockFreq(TailCallBB));
    BFI->setBlockFreq(BB,
                      (BFI->getBlockFreq(BB) - BFI->getBlockFreq(TailCallBB)));
    ModifiedDT = ModifyDT::ModifyBBDT;
    Changed = true;
    ++NumRetsDup;
  }

  // If we eliminated all predecessors of the block, delete the block now.
  if (Changed && !BB->hasAddressTaken() && pred_empty(BB))
    BB->eraseFromParent();

  return Changed;
}

//===----------------------------------------------------------------------===//
// Memory Optimization
//===----------------------------------------------------------------------===//

namespace {

/// This is an extended version of TargetLowering::AddrMode
/// which holds actual Value*'s for register values.
struct ExtAddrMode : public TargetLowering::AddrMode {
  Value *BaseReg = nullptr;
  Value *ScaledReg = nullptr;
  Value *OriginalValue = nullptr;
  bool InBounds = true;

  enum FieldName {
    NoField = 0x00,
    BaseRegField = 0x01,
    BaseGVField = 0x02,
    BaseOffsField = 0x04,
    ScaledRegField = 0x08,
    ScaleField = 0x10,
    MultipleFields = 0xff
  };

  ExtAddrMode() = default;

  void print(raw_ostream &OS) const;
  void dump() const;

  FieldName compare(const ExtAddrMode &other) {
    // First check that the types are the same on each field, as differing types
    // is something we can't cope with later on.
    if (BaseReg && other.BaseReg &&
        BaseReg->getType() != other.BaseReg->getType())
      return MultipleFields;
    if (BaseGV && other.BaseGV && BaseGV->getType() != other.BaseGV->getType())
      return MultipleFields;
    if (ScaledReg && other.ScaledReg &&
        ScaledReg->getType() != other.ScaledReg->getType())
      return MultipleFields;

    // Conservatively reject 'inbounds' mismatches.
    if (InBounds != other.InBounds)
      return MultipleFields;

    // Check each field to see if it differs.
    unsigned Result = NoField;
    if (BaseReg != other.BaseReg)
      Result |= BaseRegField;
    if (BaseGV != other.BaseGV)
      Result |= BaseGVField;
    if (BaseOffs != other.BaseOffs)
      Result |= BaseOffsField;
    if (ScaledReg != other.ScaledReg)
      Result |= ScaledRegField;
    // Don't count 0 as being a different scale, because that actually means
    // unscaled (which will already be counted by having no ScaledReg).
    if (Scale && other.Scale && Scale != other.Scale)
      Result |= ScaleField;

    if (llvm::popcount(Result) > 1)
      return MultipleFields;
    else
      return static_cast<FieldName>(Result);
  }

  // An AddrMode is trivial if it involves no calculation i.e. it is just a base
  // with no offset.
  bool isTrivial() {
    // An AddrMode is (BaseGV + BaseReg + BaseOffs + ScaleReg * Scale) so it is
    // trivial if at most one of these terms is nonzero, except that BaseGV and
    // BaseReg both being zero actually means a null pointer value, which we
    // consider to be 'non-zero' here.
    return !BaseOffs && !Scale && !(BaseGV && BaseReg);
  }

  Value *GetFieldAsValue(FieldName Field, Type *IntPtrTy) {
    switch (Field) {
    default:
      return nullptr;
    case BaseRegField:
      return BaseReg;
    case BaseGVField:
      return BaseGV;
    case ScaledRegField:
      return ScaledReg;
    case BaseOffsField:
      return ConstantInt::get(IntPtrTy, BaseOffs);
    }
  }

  void SetCombinedField(FieldName Field, Value *V,
                        const SmallVectorImpl<ExtAddrMode> &AddrModes) {
    switch (Field) {
    default:
      llvm_unreachable("Unhandled fields are expected to be rejected earlier");
      break;
    case ExtAddrMode::BaseRegField:
      BaseReg = V;
      break;
    case ExtAddrMode::BaseGVField:
      // A combined BaseGV is an Instruction, not a GlobalValue, so it goes
      // in the BaseReg field.
      assert(BaseReg == nullptr);
      BaseReg = V;
      BaseGV = nullptr;
      break;
    case ExtAddrMode::ScaledRegField:
      ScaledReg = V;
      // If we have a mix of scaled and unscaled addrmodes then we want scale
      // to be the scale and not zero.
      if (!Scale)
        for (const ExtAddrMode &AM : AddrModes)
          if (AM.Scale) {
            Scale = AM.Scale;
            break;
          }
      break;
    case ExtAddrMode::BaseOffsField:
      // The offset is no longer a constant, so it goes in ScaledReg with a
      // scale of 1.
      assert(ScaledReg == nullptr);
      ScaledReg = V;
      Scale = 1;
      BaseOffs = 0;
      break;
    }
  }
};

#ifndef NDEBUG
static inline raw_ostream &operator<<(raw_ostream &OS, const ExtAddrMode &AM) {
  AM.print(OS);
  return OS;
}
#endif

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ExtAddrMode::print(raw_ostream &OS) const {
  bool NeedPlus = false;
  OS << "[";
  if (InBounds)
    OS << "inbounds ";
  if (BaseGV) {
    OS << "GV:";
    BaseGV->printAsOperand(OS, /*PrintType=*/false);
    NeedPlus = true;
  }

  if (BaseOffs) {
    OS << (NeedPlus ? " + " : "") << BaseOffs;
    NeedPlus = true;
  }

  if (BaseReg) {
    OS << (NeedPlus ? " + " : "") << "Base:";
    BaseReg->printAsOperand(OS, /*PrintType=*/false);
    NeedPlus = true;
  }
  if (Scale) {
    OS << (NeedPlus ? " + " : "") << Scale << "*";
    ScaledReg->printAsOperand(OS, /*PrintType=*/false);
  }

  OS << ']';
}

LLVM_DUMP_METHOD void ExtAddrMode::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

} // end anonymous namespace

namespace {

/// This class provides transaction based operation on the IR.
/// Every change made through this class is recorded in the internal state and
/// can be undone (rollback) until commit is called.
/// CGP does not check if instructions could be speculatively executed when
/// moved. Preserving the original location would pessimize the debugging
/// experience, as well as negatively impact the quality of sample PGO.
class TypePromotionTransaction {
  /// This represents the common interface of the individual transaction.
  /// Each class implements the logic for doing one specific modification on
  /// the IR via the TypePromotionTransaction.
  class TypePromotionAction {
  protected:
    /// The Instruction modified.
    Instruction *Inst;

  public:
    /// Constructor of the action.
    /// The constructor performs the related action on the IR.
    TypePromotionAction(Instruction *Inst) : Inst(Inst) {}

    virtual ~TypePromotionAction() = default;

    /// Undo the modification done by this action.
    /// When this method is called, the IR must be in the same state as it was
    /// before this action was applied.
    /// \pre Undoing the action works if and only if the IR is in the exact same
    /// state as it was directly after this action was applied.
    virtual void undo() = 0;

    /// Advocate every change made by this action.
    /// When the results on the IR of the action are to be kept, it is important
    /// to call this function, otherwise hidden information may be kept forever.
    virtual void commit() {
      // Nothing to be done, this action is not doing anything.
    }
  };

  /// Utility to remember the position of an instruction.
  class InsertionHandler {
    /// Position of an instruction.
    /// Either an instruction:
    /// - Is the first in a basic block: BB is used.
    /// - Has a previous instruction: PrevInst is used.
    union {
      Instruction *PrevInst;
      BasicBlock *BB;
    } Point;
    std::optional<DbgRecord::self_iterator> BeforeDbgRecord = std::nullopt;

    /// Remember whether or not the instruction had a previous instruction.
    bool HasPrevInstruction;

  public:
    /// Record the position of \p Inst.
    InsertionHandler(Instruction *Inst) {
      HasPrevInstruction = (Inst != &*(Inst->getParent()->begin()));
      BasicBlock *BB = Inst->getParent();

      // Record where we would have to re-insert the instruction in the sequence
      // of DbgRecords, if we ended up reinserting.
      if (BB->IsNewDbgInfoFormat)
        BeforeDbgRecord = Inst->getDbgReinsertionPosition();

      if (HasPrevInstruction) {
        Point.PrevInst = &*std::prev(Inst->getIterator());
      } else {
        Point.BB = BB;
      }
    }

    /// Insert \p Inst at the recorded position.
    void insert(Instruction *Inst) {
      if (HasPrevInstruction) {
        if (Inst->getParent())
          Inst->removeFromParent();
        Inst->insertAfter(&*Point.PrevInst);
      } else {
        BasicBlock::iterator Position = Point.BB->getFirstInsertionPt();
        if (Inst->getParent())
          Inst->moveBefore(*Point.BB, Position);
        else
          Inst->insertBefore(*Point.BB, Position);
      }

      Inst->getParent()->reinsertInstInDbgRecords(Inst, BeforeDbgRecord);
    }
  };

  /// Move an instruction before another.
  class InstructionMoveBefore : public TypePromotionAction {
    /// Original position of the instruction.
    InsertionHandler Position;

  public:
    /// Move \p Inst before \p Before.
    InstructionMoveBefore(Instruction *Inst, Instruction *Before)
        : TypePromotionAction(Inst), Position(Inst) {
      LLVM_DEBUG(dbgs() << "Do: move: " << *Inst << "\nbefore: " << *Before
                        << "\n");
      Inst->moveBefore(Before);
    }

    /// Move the instruction back to its original position.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: moveBefore: " << *Inst << "\n");
      Position.insert(Inst);
    }
  };

  /// Set the operand of an instruction with a new value.
  class OperandSetter : public TypePromotionAction {
    /// Original operand of the instruction.
    Value *Origin;

    /// Index of the modified instruction.
    unsigned Idx;

  public:
    /// Set \p Idx operand of \p Inst with \p NewVal.
    OperandSetter(Instruction *Inst, unsigned Idx, Value *NewVal)
        : TypePromotionAction(Inst), Idx(Idx) {
      LLVM_DEBUG(dbgs() << "Do: setOperand: " << Idx << "\n"
                        << "for:" << *Inst << "\n"
                        << "with:" << *NewVal << "\n");
      Origin = Inst->getOperand(Idx);
      Inst->setOperand(Idx, NewVal);
    }

    /// Restore the original value of the instruction.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: setOperand:" << Idx << "\n"
                        << "for: " << *Inst << "\n"
                        << "with: " << *Origin << "\n");
      Inst->setOperand(Idx, Origin);
    }
  };

  /// Hide the operands of an instruction.
  /// Do as if this instruction was not using any of its operands.
  class OperandsHider : public TypePromotionAction {
    /// The list of original operands.
    SmallVector<Value *, 4> OriginalValues;

  public:
    /// Remove \p Inst from the uses of the operands of \p Inst.
    OperandsHider(Instruction *Inst) : TypePromotionAction(Inst) {
      LLVM_DEBUG(dbgs() << "Do: OperandsHider: " << *Inst << "\n");
      unsigned NumOpnds = Inst->getNumOperands();
      OriginalValues.reserve(NumOpnds);
      for (unsigned It = 0; It < NumOpnds; ++It) {
        // Save the current operand.
        Value *Val = Inst->getOperand(It);
        OriginalValues.push_back(Val);
        // Set a dummy one.
        // We could use OperandSetter here, but that would imply an overhead
        // that we are not willing to pay.
        Inst->setOperand(It, UndefValue::get(Val->getType()));
      }
    }

    /// Restore the original list of uses.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: OperandsHider: " << *Inst << "\n");
      for (unsigned It = 0, EndIt = OriginalValues.size(); It != EndIt; ++It)
        Inst->setOperand(It, OriginalValues[It]);
    }
  };

  /// Build a truncate instruction.
  class TruncBuilder : public TypePromotionAction {
    Value *Val;

  public:
    /// Build a truncate instruction of \p Opnd producing a \p Ty
    /// result.
    /// trunc Opnd to Ty.
    TruncBuilder(Instruction *Opnd, Type *Ty) : TypePromotionAction(Opnd) {
      IRBuilder<> Builder(Opnd);
      Builder.SetCurrentDebugLocation(DebugLoc());
      Val = Builder.CreateTrunc(Opnd, Ty, "promoted");
      LLVM_DEBUG(dbgs() << "Do: TruncBuilder: " << *Val << "\n");
    }

    /// Get the built value.
    Value *getBuiltValue() { return Val; }

    /// Remove the built instruction.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: TruncBuilder: " << *Val << "\n");
      if (Instruction *IVal = dyn_cast<Instruction>(Val))
        IVal->eraseFromParent();
    }
  };

  /// Build a sign extension instruction.
  class SExtBuilder : public TypePromotionAction {
    Value *Val;

  public:
    /// Build a sign extension instruction of \p Opnd producing a \p Ty
    /// result.
    /// sext Opnd to Ty.
    SExtBuilder(Instruction *InsertPt, Value *Opnd, Type *Ty)
        : TypePromotionAction(InsertPt) {
      IRBuilder<> Builder(InsertPt);
      Val = Builder.CreateSExt(Opnd, Ty, "promoted");
      LLVM_DEBUG(dbgs() << "Do: SExtBuilder: " << *Val << "\n");
    }

    /// Get the built value.
    Value *getBuiltValue() { return Val; }

    /// Remove the built instruction.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: SExtBuilder: " << *Val << "\n");
      if (Instruction *IVal = dyn_cast<Instruction>(Val))
        IVal->eraseFromParent();
    }
  };

  /// Build a zero extension instruction.
  class ZExtBuilder : public TypePromotionAction {
    Value *Val;

  public:
    /// Build a zero extension instruction of \p Opnd producing a \p Ty
    /// result.
    /// zext Opnd to Ty.
    ZExtBuilder(Instruction *InsertPt, Value *Opnd, Type *Ty)
        : TypePromotionAction(InsertPt) {
      IRBuilder<> Builder(InsertPt);
      Builder.SetCurrentDebugLocation(DebugLoc());
      Val = Builder.CreateZExt(Opnd, Ty, "promoted");
      LLVM_DEBUG(dbgs() << "Do: ZExtBuilder: " << *Val << "\n");
    }

    /// Get the built value.
    Value *getBuiltValue() { return Val; }

    /// Remove the built instruction.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: ZExtBuilder: " << *Val << "\n");
      if (Instruction *IVal = dyn_cast<Instruction>(Val))
        IVal->eraseFromParent();
    }
  };

  /// Mutate an instruction to another type.
  class TypeMutator : public TypePromotionAction {
    /// Record the original type.
    Type *OrigTy;

  public:
    /// Mutate the type of \p Inst into \p NewTy.
    TypeMutator(Instruction *Inst, Type *NewTy)
        : TypePromotionAction(Inst), OrigTy(Inst->getType()) {
      LLVM_DEBUG(dbgs() << "Do: MutateType: " << *Inst << " with " << *NewTy
                        << "\n");
      Inst->mutateType(NewTy);
    }

    /// Mutate the instruction back to its original type.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: MutateType: " << *Inst << " with " << *OrigTy
                        << "\n");
      Inst->mutateType(OrigTy);
    }
  };

  /// Replace the uses of an instruction by another instruction.
  class UsesReplacer : public TypePromotionAction {
    /// Helper structure to keep track of the replaced uses.
    struct InstructionAndIdx {
      /// The instruction using the instruction.
      Instruction *Inst;

      /// The index where this instruction is used for Inst.
      unsigned Idx;

      InstructionAndIdx(Instruction *Inst, unsigned Idx)
          : Inst(Inst), Idx(Idx) {}
    };

    /// Keep track of the original uses (pair Instruction, Index).
    SmallVector<InstructionAndIdx, 4> OriginalUses;
    /// Keep track of the debug users.
    SmallVector<DbgValueInst *, 1> DbgValues;
    /// And non-instruction debug-users too.
    SmallVector<DbgVariableRecord *, 1> DbgVariableRecords;

    /// Keep track of the new value so that we can undo it by replacing
    /// instances of the new value with the original value.
    Value *New;

    using use_iterator = SmallVectorImpl<InstructionAndIdx>::iterator;

  public:
    /// Replace all the use of \p Inst by \p New.
    UsesReplacer(Instruction *Inst, Value *New)
        : TypePromotionAction(Inst), New(New) {
      LLVM_DEBUG(dbgs() << "Do: UsersReplacer: " << *Inst << " with " << *New
                        << "\n");
      // Record the original uses.
      for (Use &U : Inst->uses()) {
        Instruction *UserI = cast<Instruction>(U.getUser());
        OriginalUses.push_back(InstructionAndIdx(UserI, U.getOperandNo()));
      }
      // Record the debug uses separately. They are not in the instruction's
      // use list, but they are replaced by RAUW.
      findDbgValues(DbgValues, Inst, &DbgVariableRecords);

      // Now, we can replace the uses.
      Inst->replaceAllUsesWith(New);
    }

    /// Reassign the original uses of Inst to Inst.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: UsersReplacer: " << *Inst << "\n");
      for (InstructionAndIdx &Use : OriginalUses)
        Use.Inst->setOperand(Use.Idx, Inst);
      // RAUW has replaced all original uses with references to the new value,
      // including the debug uses. Since we are undoing the replacements,
      // the original debug uses must also be reinstated to maintain the
      // correctness and utility of debug value instructions.
      for (auto *DVI : DbgValues)
        DVI->replaceVariableLocationOp(New, Inst);
      // Similar story with DbgVariableRecords, the non-instruction
      // representation of dbg.values.
      for (DbgVariableRecord *DVR : DbgVariableRecords)
        DVR->replaceVariableLocationOp(New, Inst);
    }
  };

  /// Remove an instruction from the IR.
  class InstructionRemover : public TypePromotionAction {
    /// Original position of the instruction.
    InsertionHandler Inserter;

    /// Helper structure to hide all the link to the instruction. In other
    /// words, this helps to do as if the instruction was removed.
    OperandsHider Hider;

    /// Keep track of the uses replaced, if any.
    UsesReplacer *Replacer = nullptr;

    /// Keep track of instructions removed.
    SetOfInstrs &RemovedInsts;

  public:
    /// Remove all reference of \p Inst and optionally replace all its
    /// uses with New.
    /// \p RemovedInsts Keep track of the instructions removed by this Action.
    /// \pre If !Inst->use_empty(), then New != nullptr
    InstructionRemover(Instruction *Inst, SetOfInstrs &RemovedInsts,
                       Value *New = nullptr)
        : TypePromotionAction(Inst), Inserter(Inst), Hider(Inst),
          RemovedInsts(RemovedInsts) {
      if (New)
        Replacer = new UsesReplacer(Inst, New);
      LLVM_DEBUG(dbgs() << "Do: InstructionRemover: " << *Inst << "\n");
      RemovedInsts.insert(Inst);
      /// The instructions removed here will be freed after completing
      /// optimizeBlock() for all blocks as we need to keep track of the
      /// removed instructions during promotion.
      Inst->removeFromParent();
    }

    ~InstructionRemover() override { delete Replacer; }

    InstructionRemover &operator=(const InstructionRemover &other) = delete;
    InstructionRemover(const InstructionRemover &other) = delete;

    /// Resurrect the instruction and reassign it to the proper uses if
    /// new value was provided when build this action.
    void undo() override {
      LLVM_DEBUG(dbgs() << "Undo: InstructionRemover: " << *Inst << "\n");
      Inserter.insert(Inst);
      if (Replacer)
        Replacer->undo();
      Hider.undo();
      RemovedInsts.erase(Inst);
    }
  };

public:
  /// Restoration point.
  /// The restoration point is a pointer to an action instead of an iterator
  /// because the iterator may be invalidated but not the pointer.
  using ConstRestorationPt = const TypePromotionAction *;

  TypePromotionTransaction(SetOfInstrs &RemovedInsts)
      : RemovedInsts(RemovedInsts) {}

  /// Advocate every changes made in that transaction. Return true if any change
  /// happen.
  bool commit();

  /// Undo all the changes made after the given point.
  void rollback(ConstRestorationPt Point);

  /// Get the current restoration point.
  ConstRestorationPt getRestorationPoint() const;

  /// \name API for IR modification with state keeping to support rollback.
  /// @{
  /// Same as Instruction::setOperand.
  void setOperand(Instruction *Inst, unsigned Idx, Value *NewVal);

  /// Same as Instruction::eraseFromParent.
  void eraseInstruction(Instruction *Inst, Value *NewVal = nullptr);

  /// Same as Value::replaceAllUsesWith.
  void replaceAllUsesWith(Instruction *Inst, Value *New);

  /// Same as Value::mutateType.
  void mutateType(Instruction *Inst, Type *NewTy);

  /// Same as IRBuilder::createTrunc.
  Value *createTrunc(Instruction *Opnd, Type *Ty);

  /// Same as IRBuilder::createSExt.
  Value *createSExt(Instruction *Inst, Value *Opnd, Type *Ty);

  /// Same as IRBuilder::createZExt.
  Value *createZExt(Instruction *Inst, Value *Opnd, Type *Ty);

private:
  /// The ordered list of actions made so far.
  SmallVector<std::unique_ptr<TypePromotionAction>, 16> Actions;

  using CommitPt =
      SmallVectorImpl<std::unique_ptr<TypePromotionAction>>::iterator;

  SetOfInstrs &RemovedInsts;
};

} // end anonymous namespace

void TypePromotionTransaction::setOperand(Instruction *Inst, unsigned Idx,
                                          Value *NewVal) {
  Actions.push_back(std::make_unique<TypePromotionTransaction::OperandSetter>(
      Inst, Idx, NewVal));
}

void TypePromotionTransaction::eraseInstruction(Instruction *Inst,
                                                Value *NewVal) {
  Actions.push_back(
      std::make_unique<TypePromotionTransaction::InstructionRemover>(
          Inst, RemovedInsts, NewVal));
}

void TypePromotionTransaction::replaceAllUsesWith(Instruction *Inst,
                                                  Value *New) {
  Actions.push_back(
      std::make_unique<TypePromotionTransaction::UsesReplacer>(Inst, New));
}

void TypePromotionTransaction::mutateType(Instruction *Inst, Type *NewTy) {
  Actions.push_back(
      std::make_unique<TypePromotionTransaction::TypeMutator>(Inst, NewTy));
}

Value *TypePromotionTransaction::createTrunc(Instruction *Opnd, Type *Ty) {
  std::unique_ptr<TruncBuilder> Ptr(new TruncBuilder(Opnd, Ty));
  Value *Val = Ptr->getBuiltValue();
  Actions.push_back(std::move(Ptr));
  return Val;
}

Value *TypePromotionTransaction::createSExt(Instruction *Inst, Value *Opnd,
                                            Type *Ty) {
  std::unique_ptr<SExtBuilder> Ptr(new SExtBuilder(Inst, Opnd, Ty));
  Value *Val = Ptr->getBuiltValue();
  Actions.push_back(std::move(Ptr));
  return Val;
}

Value *TypePromotionTransaction::createZExt(Instruction *Inst, Value *Opnd,
                                            Type *Ty) {
  std::unique_ptr<ZExtBuilder> Ptr(new ZExtBuilder(Inst, Opnd, Ty));
  Value *Val = Ptr->getBuiltValue();
  Actions.push_back(std::move(Ptr));
  return Val;
}

TypePromotionTransaction::ConstRestorationPt
TypePromotionTransaction::getRestorationPoint() const {
  return !Actions.empty() ? Actions.back().get() : nullptr;
}

bool TypePromotionTransaction::commit() {
  for (std::unique_ptr<TypePromotionAction> &Action : Actions)
    Action->commit();
  bool Modified = !Actions.empty();
  Actions.clear();
  return Modified;
}

void TypePromotionTransaction::rollback(
    TypePromotionTransaction::ConstRestorationPt Point) {
  while (!Actions.empty() && Point != Actions.back().get()) {
    std::unique_ptr<TypePromotionAction> Curr = Actions.pop_back_val();
    Curr->undo();
  }
}

namespace {

/// A helper class for matching addressing modes.
///
/// This encapsulates the logic for matching the target-legal addressing modes.
class AddressingModeMatcher {
  SmallVectorImpl<Instruction *> &AddrModeInsts;
  const TargetLowering &TLI;
  const TargetRegisterInfo &TRI;
  const DataLayout &DL;
  const LoopInfo &LI;
  const std::function<const DominatorTree &()> getDTFn;

  /// AccessTy/MemoryInst - This is the type for the access (e.g. double) and
  /// the memory instruction that we're computing this address for.
  Type *AccessTy;
  unsigned AddrSpace;
  Instruction *MemoryInst;

  /// This is the addressing mode that we're building up. This is
  /// part of the return value of this addressing mode matching stuff.
  ExtAddrMode &AddrMode;

  /// The instructions inserted by other CodeGenPrepare optimizations.
  const SetOfInstrs &InsertedInsts;

  /// A map from the instructions to their type before promotion.
  InstrToOrigTy &PromotedInsts;

  /// The ongoing transaction where every action should be registered.
  TypePromotionTransaction &TPT;

  // A GEP which has too large offset to be folded into the addressing mode.
  std::pair<AssertingVH<GetElementPtrInst>, int64_t> &LargeOffsetGEP;

  /// This is set to true when we should not do profitability checks.
  /// When true, IsProfitableToFoldIntoAddressingMode always returns true.
  bool IgnoreProfitability;

  /// True if we are optimizing for size.
  bool OptSize = false;

  ProfileSummaryInfo *PSI;
  BlockFrequencyInfo *BFI;

  AddressingModeMatcher(
      SmallVectorImpl<Instruction *> &AMI, const TargetLowering &TLI,
      const TargetRegisterInfo &TRI, const LoopInfo &LI,
      const std::function<const DominatorTree &()> getDTFn, Type *AT,
      unsigned AS, Instruction *MI, ExtAddrMode &AM,
      const SetOfInstrs &InsertedInsts, InstrToOrigTy &PromotedInsts,
      TypePromotionTransaction &TPT,
      std::pair<AssertingVH<GetElementPtrInst>, int64_t> &LargeOffsetGEP,
      bool OptSize, ProfileSummaryInfo *PSI, BlockFrequencyInfo *BFI)
      : AddrModeInsts(AMI), TLI(TLI), TRI(TRI),
        DL(MI->getDataLayout()), LI(LI), getDTFn(getDTFn),
        AccessTy(AT), AddrSpace(AS), MemoryInst(MI), AddrMode(AM),
        InsertedInsts(InsertedInsts), PromotedInsts(PromotedInsts), TPT(TPT),
        LargeOffsetGEP(LargeOffsetGEP), OptSize(OptSize), PSI(PSI), BFI(BFI) {
    IgnoreProfitability = false;
  }

public:
  /// Find the maximal addressing mode that a load/store of V can fold,
  /// give an access type of AccessTy.  This returns a list of involved
  /// instructions in AddrModeInsts.
  /// \p InsertedInsts The instructions inserted by other CodeGenPrepare
  /// optimizations.
  /// \p PromotedInsts maps the instructions to their type before promotion.
  /// \p The ongoing transaction where every action should be registered.
  static ExtAddrMode
  Match(Value *V, Type *AccessTy, unsigned AS, Instruction *MemoryInst,
        SmallVectorImpl<Instruction *> &AddrModeInsts,
        const TargetLowering &TLI, const LoopInfo &LI,
        const std::function<const DominatorTree &()> getDTFn,
        const TargetRegisterInfo &TRI, const SetOfInstrs &InsertedInsts,
        InstrToOrigTy &PromotedInsts, TypePromotionTransaction &TPT,
        std::pair<AssertingVH<GetElementPtrInst>, int64_t> &LargeOffsetGEP,
        bool OptSize, ProfileSummaryInfo *PSI, BlockFrequencyInfo *BFI) {
    ExtAddrMode Result;

    bool Success = AddressingModeMatcher(AddrModeInsts, TLI, TRI, LI, getDTFn,
                                         AccessTy, AS, MemoryInst, Result,
                                         InsertedInsts, PromotedInsts, TPT,
                                         LargeOffsetGEP, OptSize, PSI, BFI)
                       .matchAddr(V, 0);
    (void)Success;
    assert(Success && "Couldn't select *anything*?");
    return Result;
  }

private:
  bool matchScaledValue(Value *ScaleReg, int64_t Scale, unsigned Depth);
  bool matchAddr(Value *Addr, unsigned Depth);
  bool matchOperationAddr(User *AddrInst, unsigned Opcode, unsigned Depth,
                          bool *MovedAway = nullptr);
  bool isProfitableToFoldIntoAddressingMode(Instruction *I,
                                            ExtAddrMode &AMBefore,
                                            ExtAddrMode &AMAfter);
  bool valueAlreadyLiveAtInst(Value *Val, Value *KnownLive1, Value *KnownLive2);
  bool isPromotionProfitable(unsigned NewCost, unsigned OldCost,
                             Value *PromotedOperand) const;
};

class PhiNodeSet;

/// An iterator for PhiNodeSet.
class PhiNodeSetIterator {
  PhiNodeSet *const Set;
  size_t CurrentIndex = 0;

public:
  /// The constructor. Start should point to either a valid element, or be equal
  /// to the size of the underlying SmallVector of the PhiNodeSet.
  PhiNodeSetIterator(PhiNodeSet *const Set, size_t Start);
  PHINode *operator*() const;
  PhiNodeSetIterator &operator++();
  bool operator==(const PhiNodeSetIterator &RHS) const;
  bool operator!=(const PhiNodeSetIterator &RHS) const;
};

/// Keeps a set of PHINodes.
///
/// This is a minimal set implementation for a specific use case:
/// It is very fast when there are very few elements, but also provides good
/// performance when there are many. It is similar to SmallPtrSet, but also
/// provides iteration by insertion order, which is deterministic and stable
/// across runs. It is also similar to SmallSetVector, but provides removing
/// elements in O(1) time. This is achieved by not actually removing the element
/// from the underlying vector, so comes at the cost of using more memory, but
/// that is fine, since PhiNodeSets are used as short lived objects.
class PhiNodeSet {
  friend class PhiNodeSetIterator;

  using MapType = SmallDenseMap<PHINode *, size_t, 32>;
  using iterator = PhiNodeSetIterator;

  /// Keeps the elements in the order of their insertion in the underlying
  /// vector. To achieve constant time removal, it never deletes any element.
  SmallVector<PHINode *, 32> NodeList;

  /// Keeps the elements in the underlying set implementation. This (and not the
  /// NodeList defined above) is the source of truth on whether an element
  /// is actually in the collection.
  MapType NodeMap;

  /// Points to the first valid (not deleted) element when the set is not empty
  /// and the value is not zero. Equals to the size of the underlying vector
  /// when the set is empty. When the value is 0, as in the beginning, the
  /// first element may or may not be valid.
  size_t FirstValidElement = 0;

public:
  /// Inserts a new element to the collection.
  /// \returns true if the element is actually added, i.e. was not in the
  /// collection before the operation.
  bool insert(PHINode *Ptr) {
    if (NodeMap.insert(std::make_pair(Ptr, NodeList.size())).second) {
      NodeList.push_back(Ptr);
      return true;
    }
    return false;
  }

  /// Removes the element from the collection.
  /// \returns whether the element is actually removed, i.e. was in the
  /// collection before the operation.
  bool erase(PHINode *Ptr) {
    if (NodeMap.erase(Ptr)) {
      SkipRemovedElements(FirstValidElement);
      return true;
    }
    return false;
  }

  /// Removes all elements and clears the collection.
  void clear() {
    NodeMap.clear();
    NodeList.clear();
    FirstValidElement = 0;
  }

  /// \returns an iterator that will iterate the elements in the order of
  /// insertion.
  iterator begin() {
    if (FirstValidElement == 0)
      SkipRemovedElements(FirstValidElement);
    return PhiNodeSetIterator(this, FirstValidElement);
  }

  /// \returns an iterator that points to the end of the collection.
  iterator end() { return PhiNodeSetIterator(this, NodeList.size()); }

  /// Returns the number of elements in the collection.
  size_t size() const { return NodeMap.size(); }

  /// \returns 1 if the given element is in the collection, and 0 if otherwise.
  size_t count(PHINode *Ptr) const { return NodeMap.count(Ptr); }

private:
  /// Updates the CurrentIndex so that it will point to a valid element.
  ///
  /// If the element of NodeList at CurrentIndex is valid, it does not
  /// change it. If there are no more valid elements, it updates CurrentIndex
  /// to point to the end of the NodeList.
  void SkipRemovedElements(size_t &CurrentIndex) {
    while (CurrentIndex < NodeList.size()) {
      auto it = NodeMap.find(NodeList[CurrentIndex]);
      // If the element has been deleted and added again later, NodeMap will
      // point to a different index, so CurrentIndex will still be invalid.
      if (it != NodeMap.end() && it->second == CurrentIndex)
        break;
      ++CurrentIndex;
    }
  }
};

PhiNodeSetIterator::PhiNodeSetIterator(PhiNodeSet *const Set, size_t Start)
    : Set(Set), CurrentIndex(Start) {}

PHINode *PhiNodeSetIterator::operator*() const {
  assert(CurrentIndex < Set->NodeList.size() &&
         "PhiNodeSet access out of range");
  return Set->NodeList[CurrentIndex];
}

PhiNodeSetIterator &PhiNodeSetIterator::operator++() {
  assert(CurrentIndex < Set->NodeList.size() &&
         "PhiNodeSet access out of range");
  ++CurrentIndex;
  Set->SkipRemovedElements(CurrentIndex);
  return *this;
}

bool PhiNodeSetIterator::operator==(const PhiNodeSetIterator &RHS) const {
  return CurrentIndex == RHS.CurrentIndex;
}

bool PhiNodeSetIterator::operator!=(const PhiNodeSetIterator &RHS) const {
  return !((*this) == RHS);
}

/// Keep track of simplification of Phi nodes.
/// Accept the set of all phi nodes and erase phi node from this set
/// if it is simplified.
class SimplificationTracker {
  DenseMap<Value *, Value *> Storage;
  const SimplifyQuery &SQ;
  // Tracks newly created Phi nodes. The elements are iterated by insertion
  // order.
  PhiNodeSet AllPhiNodes;
  // Tracks newly created Select nodes.
  SmallPtrSet<SelectInst *, 32> AllSelectNodes;

public:
  SimplificationTracker(const SimplifyQuery &sq) : SQ(sq) {}

  Value *Get(Value *V) {
    do {
      auto SV = Storage.find(V);
      if (SV == Storage.end())
        return V;
      V = SV->second;
    } while (true);
  }

  Value *Simplify(Value *Val) {
    SmallVector<Value *, 32> WorkList;
    SmallPtrSet<Value *, 32> Visited;
    WorkList.push_back(Val);
    while (!WorkList.empty()) {
      auto *P = WorkList.pop_back_val();
      if (!Visited.insert(P).second)
        continue;
      if (auto *PI = dyn_cast<Instruction>(P))
        if (Value *V = simplifyInstruction(cast<Instruction>(PI), SQ)) {
          for (auto *U : PI->users())
            WorkList.push_back(cast<Value>(U));
          Put(PI, V);
          PI->replaceAllUsesWith(V);
          if (auto *PHI = dyn_cast<PHINode>(PI))
            AllPhiNodes.erase(PHI);
          if (auto *Select = dyn_cast<SelectInst>(PI))
            AllSelectNodes.erase(Select);
          PI->eraseFromParent();
        }
    }
    return Get(Val);
  }

  void Put(Value *From, Value *To) { Storage.insert({From, To}); }

  void ReplacePhi(PHINode *From, PHINode *To) {
    Value *OldReplacement = Get(From);
    while (OldReplacement != From) {
      From = To;
      To = dyn_cast<PHINode>(OldReplacement);
      OldReplacement = Get(From);
    }
    assert(To && Get(To) == To && "Replacement PHI node is already replaced.");
    Put(From, To);
    From->replaceAllUsesWith(To);
    AllPhiNodes.erase(From);
    From->eraseFromParent();
  }

  PhiNodeSet &newPhiNodes() { return AllPhiNodes; }

  void insertNewPhi(PHINode *PN) { AllPhiNodes.insert(PN); }

  void insertNewSelect(SelectInst *SI) { AllSelectNodes.insert(SI); }

  unsigned countNewPhiNodes() const { return AllPhiNodes.size(); }

  unsigned countNewSelectNodes() const { return AllSelectNodes.size(); }

  void destroyNewNodes(Type *CommonType) {
    // For safe erasing, replace the uses with dummy value first.
    auto *Dummy = PoisonValue::get(CommonType);
    for (auto *I : AllPhiNodes) {
      I->replaceAllUsesWith(Dummy);
      I->eraseFromParent();
    }
    AllPhiNodes.clear();
    for (auto *I : AllSelectNodes) {
      I->replaceAllUsesWith(Dummy);
      I->eraseFromParent();
    }
    AllSelectNodes.clear();
  }
};

/// A helper class for combining addressing modes.
class AddressingModeCombiner {
  typedef DenseMap<Value *, Value *> FoldAddrToValueMapping;
  typedef std::pair<PHINode *, PHINode *> PHIPair;

private:
  /// The addressing modes we've collected.
  SmallVector<ExtAddrMode, 16> AddrModes;

  /// The field in which the AddrModes differ, when we have more than one.
  ExtAddrMode::FieldName DifferentField = ExtAddrMode::NoField;

  /// Are the AddrModes that we have all just equal to their original values?
  bool AllAddrModesTrivial = true;

  /// Common Type for all different fields in addressing modes.
  Type *CommonType = nullptr;

  /// SimplifyQuery for simplifyInstruction utility.
  const SimplifyQuery &SQ;

  /// Original Address.
  Value *Original;

  /// Common value among addresses
  Value *CommonValue = nullptr;

public:
  AddressingModeCombiner(const SimplifyQuery &_SQ, Value *OriginalValue)
      : SQ(_SQ), Original(OriginalValue) {}

  ~AddressingModeCombiner() { eraseCommonValueIfDead(); }

  /// Get the combined AddrMode
  const ExtAddrMode &getAddrMode() const { return AddrModes[0]; }

  /// Add a new AddrMode if it's compatible with the AddrModes we already
  /// have.
  /// \return True iff we succeeded in doing so.
  bool addNewAddrMode(ExtAddrMode &NewAddrMode) {
    // Take note of if we have any non-trivial AddrModes, as we need to detect
    // when all AddrModes are trivial as then we would introduce a phi or select
    // which just duplicates what's already there.
    AllAddrModesTrivial = AllAddrModesTrivial && NewAddrMode.isTrivial();

    // If this is the first addrmode then everything is fine.
    if (AddrModes.empty()) {
      AddrModes.emplace_back(NewAddrMode);
      return true;
    }

    // Figure out how different this is from the other address modes, which we
    // can do just by comparing against the first one given that we only care
    // about the cumulative difference.
    ExtAddrMode::FieldName ThisDifferentField =
        AddrModes[0].compare(NewAddrMode);
    if (DifferentField == ExtAddrMode::NoField)
      DifferentField = ThisDifferentField;
    else if (DifferentField != ThisDifferentField)
      DifferentField = ExtAddrMode::MultipleFields;

    // If NewAddrMode differs in more than one dimension we cannot handle it.
    bool CanHandle = DifferentField != ExtAddrMode::MultipleFields;

    // If Scale Field is different then we reject.
    CanHandle = CanHandle && DifferentField != ExtAddrMode::ScaleField;

    // We also must reject the case when base offset is different and
    // scale reg is not null, we cannot handle this case due to merge of
    // different offsets will be used as ScaleReg.
    CanHandle = CanHandle && (DifferentField != ExtAddrMode::BaseOffsField ||
                              !NewAddrMode.ScaledReg);

    // We also must reject the case when GV is different and BaseReg installed
    // due to we want to use base reg as a merge of GV values.
    CanHandle = CanHandle && (DifferentField != ExtAddrMode::BaseGVField ||
                              !NewAddrMode.HasBaseReg);

    // Even if NewAddMode is the same we still need to collect it due to
    // original value is different. And later we will need all original values
    // as anchors during finding the common Phi node.
    if (CanHandle)
      AddrModes.emplace_back(NewAddrMode);
    else
      AddrModes.clear();

    return CanHandle;
  }

  /// Combine the addressing modes we've collected into a single
  /// addressing mode.
  /// \return True iff we successfully combined them or we only had one so
  /// didn't need to combine them anyway.
  bool combineAddrModes() {
    // If we have no AddrModes then they can't be combined.
    if (AddrModes.size() == 0)
      return false;

    // A single AddrMode can trivially be combined.
    if (AddrModes.size() == 1 || DifferentField == ExtAddrMode::NoField)
      return true;

    // If the AddrModes we collected are all just equal to the value they are
    // derived from then combining them wouldn't do anything useful.
    if (AllAddrModesTrivial)
      return false;

    if (!addrModeCombiningAllowed())
      return false;

    // Build a map between <original value, basic block where we saw it> to
    // value of base register.
    // Bail out if there is no common type.
    FoldAddrToValueMapping Map;
    if (!initializeMap(Map))
      return false;

    CommonValue = findCommon(Map);
    if (CommonValue)
      AddrModes[0].SetCombinedField(DifferentField, CommonValue, AddrModes);
    return CommonValue != nullptr;
  }

private:
  /// `CommonValue` may be a placeholder inserted by us.
  /// If the placeholder is not used, we should remove this dead instruction.
  void eraseCommonValueIfDead() {
    if (CommonValue && CommonValue->getNumUses() == 0)
      if (Instruction *CommonInst = dyn_cast<Instruction>(CommonValue))
        CommonInst->eraseFromParent();
  }

  /// Initialize Map with anchor values. For address seen
  /// we set the value of different field saw in this address.
  /// At the same time we find a common type for different field we will
  /// use to create new Phi/Select nodes. Keep it in CommonType field.
  /// Return false if there is no common type found.
  bool initializeMap(FoldAddrToValueMapping &Map) {
    // Keep track of keys where the value is null. We will need to replace it
    // with constant null when we know the common type.
    SmallVector<Value *, 2> NullValue;
    Type *IntPtrTy = SQ.DL.getIntPtrType(AddrModes[0].OriginalValue->getType());
    for (auto &AM : AddrModes) {
      Value *DV = AM.GetFieldAsValue(DifferentField, IntPtrTy);
      if (DV) {
        auto *Type = DV->getType();
        if (CommonType && CommonType != Type)
          return false;
        CommonType = Type;
        Map[AM.OriginalValue] = DV;
      } else {
        NullValue.push_back(AM.OriginalValue);
      }
    }
    assert(CommonType && "At least one non-null value must be!");
    for (auto *V : NullValue)
      Map[V] = Constant::getNullValue(CommonType);
    return true;
  }

  /// We have mapping between value A and other value B where B was a field in
  /// addressing mode represented by A. Also we have an original value C
  /// representing an address we start with. Traversing from C through phi and
  /// selects we ended up with A's in a map. This utility function tries to find
  /// a value V which is a field in addressing mode C and traversing through phi
  /// nodes and selects we will end up in corresponded values B in a map.
  /// The utility will create a new Phi/Selects if needed.
  // The simple example looks as follows:
  // BB1:
  //   p1 = b1 + 40
  //   br cond BB2, BB3
  // BB2:
  //   p2 = b2 + 40
  //   br BB3
  // BB3:
  //   p = phi [p1, BB1], [p2, BB2]
  //   v = load p
  // Map is
  //   p1 -> b1
  //   p2 -> b2
  // Request is
  //   p -> ?
  // The function tries to find or build phi [b1, BB1], [b2, BB2] in BB3.
  Value *findCommon(FoldAddrToValueMapping &Map) {
    // Tracks the simplification of newly created phi nodes. The reason we use
    // this mapping is because we will add new created Phi nodes in AddrToBase.
    // Simplification of Phi nodes is recursive, so some Phi node may
    // be simplified after we added it to AddrToBase. In reality this
    // simplification is possible only if original phi/selects were not
    // simplified yet.
    // Using this mapping we can find the current value in AddrToBase.
    SimplificationTracker ST(SQ);

    // First step, DFS to create PHI nodes for all intermediate blocks.
    // Also fill traverse order for the second step.
    SmallVector<Value *, 32> TraverseOrder;
    InsertPlaceholders(Map, TraverseOrder, ST);

    // Second Step, fill new nodes by merged values and simplify if possible.
    FillPlaceholders(Map, TraverseOrder, ST);

    if (!AddrSinkNewSelects && ST.countNewSelectNodes() > 0) {
      ST.destroyNewNodes(CommonType);
      return nullptr;
    }

    // Now we'd like to match New Phi nodes to existed ones.
    unsigned PhiNotMatchedCount = 0;
    if (!MatchPhiSet(ST, AddrSinkNewPhis, PhiNotMatchedCount)) {
      ST.destroyNewNodes(CommonType);
      return nullptr;
    }

    auto *Result = ST.Get(Map.find(Original)->second);
    if (Result) {
      NumMemoryInstsPhiCreated += ST.countNewPhiNodes() + PhiNotMatchedCount;
      NumMemoryInstsSelectCreated += ST.countNewSelectNodes();
    }
    return Result;
  }

  /// Try to match PHI node to Candidate.
  /// Matcher tracks the matched Phi nodes.
  bool MatchPhiNode(PHINode *PHI, PHINode *Candidate,
                    SmallSetVector<PHIPair, 8> &Matcher,
                    PhiNodeSet &PhiNodesToMatch) {
    SmallVector<PHIPair, 8> WorkList;
    Matcher.insert({PHI, Candidate});
    SmallSet<PHINode *, 8> MatchedPHIs;
    MatchedPHIs.insert(PHI);
    WorkList.push_back({PHI, Candidate});
    SmallSet<PHIPair, 8> Visited;
    while (!WorkList.empty()) {
      auto Item = WorkList.pop_back_val();
      if (!Visited.insert(Item).second)
        continue;
      // We iterate over all incoming values to Phi to compare them.
      // If values are different and both of them Phi and the first one is a
      // Phi we added (subject to match) and both of them is in the same basic
      // block then we can match our pair if values match. So we state that
      // these values match and add it to work list to verify that.
      for (auto *B : Item.first->blocks()) {
        Value *FirstValue = Item.first->getIncomingValueForBlock(B);
        Value *SecondValue = Item.second->getIncomingValueForBlock(B);
        if (FirstValue == SecondValue)
          continue;

        PHINode *FirstPhi = dyn_cast<PHINode>(FirstValue);
        PHINode *SecondPhi = dyn_cast<PHINode>(SecondValue);

        // One of them is not Phi or
        // The first one is not Phi node from the set we'd like to match or
        // Phi nodes from different basic blocks then
        // we will not be able to match.
        if (!FirstPhi || !SecondPhi || !PhiNodesToMatch.count(FirstPhi) ||
            FirstPhi->getParent() != SecondPhi->getParent())
          return false;

        // If we already matched them then continue.
        if (Matcher.count({FirstPhi, SecondPhi}))
          continue;
        // So the values are different and does not match. So we need them to
        // match. (But we register no more than one match per PHI node, so that
        // we won't later try to replace them twice.)
        if (MatchedPHIs.insert(FirstPhi).second)
          Matcher.insert({FirstPhi, SecondPhi});
        // But me must check it.
        WorkList.push_back({FirstPhi, SecondPhi});
      }
    }
    return true;
  }

  /// For the given set of PHI nodes (in the SimplificationTracker) try
  /// to find their equivalents.
  /// Returns false if this matching fails and creation of new Phi is disabled.
  bool MatchPhiSet(SimplificationTracker &ST, bool AllowNewPhiNodes,
                   unsigned &PhiNotMatchedCount) {
    // Matched and PhiNodesToMatch iterate their elements in a deterministic
    // order, so the replacements (ReplacePhi) are also done in a deterministic
    // order.
    SmallSetVector<PHIPair, 8> Matched;
    SmallPtrSet<PHINode *, 8> WillNotMatch;
    PhiNodeSet &PhiNodesToMatch = ST.newPhiNodes();
    while (PhiNodesToMatch.size()) {
      PHINode *PHI = *PhiNodesToMatch.begin();

      // Add us, if no Phi nodes in the basic block we do not match.
      WillNotMatch.clear();
      WillNotMatch.insert(PHI);

      // Traverse all Phis until we found equivalent or fail to do that.
      bool IsMatched = false;
      for (auto &P : PHI->getParent()->phis()) {
        // Skip new Phi nodes.
        if (PhiNodesToMatch.count(&P))
          continue;
        if ((IsMatched = MatchPhiNode(PHI, &P, Matched, PhiNodesToMatch)))
          break;
        // If it does not match, collect all Phi nodes from matcher.
        // if we end up with no match, them all these Phi nodes will not match
        // later.
        for (auto M : Matched)
          WillNotMatch.insert(M.first);
        Matched.clear();
      }
      if (IsMatched) {
        // Replace all matched values and erase them.
        for (auto MV : Matched)
          ST.ReplacePhi(MV.first, MV.second);
        Matched.clear();
        continue;
      }
      // If we are not allowed to create new nodes then bail out.
      if (!AllowNewPhiNodes)
        return false;
      // Just remove all seen values in matcher. They will not match anything.
      PhiNotMatchedCount += WillNotMatch.size();
      for (auto *P : WillNotMatch)
        PhiNodesToMatch.erase(P);
    }
    return true;
  }
  /// Fill the placeholders with values from predecessors and simplify them.
  void FillPlaceholders(FoldAddrToValueMapping &Map,
                        SmallVectorImpl<Value *> &TraverseOrder,
                        SimplificationTracker &ST) {
    while (!TraverseOrder.empty()) {
      Value *Current = TraverseOrder.pop_back_val();
      assert(Map.contains(Current) && "No node to fill!!!");
      Value *V = Map[Current];

      if (SelectInst *Select = dyn_cast<SelectInst>(V)) {
        // CurrentValue also must be Select.
        auto *CurrentSelect = cast<SelectInst>(Current);
        auto *TrueValue = CurrentSelect->getTrueValue();
        assert(Map.contains(TrueValue) && "No True Value!");
        Select->setTrueValue(ST.Get(Map[TrueValue]));
        auto *FalseValue = CurrentSelect->getFalseValue();
        assert(Map.contains(FalseValue) && "No False Value!");
        Select->setFalseValue(ST.Get(Map[FalseValue]));
      } else {
        // Must be a Phi node then.
        auto *PHI = cast<PHINode>(V);
        // Fill the Phi node with values from predecessors.
        for (auto *B : predecessors(PHI->getParent())) {
          Value *PV = cast<PHINode>(Current)->getIncomingValueForBlock(B);
          assert(Map.contains(PV) && "No predecessor Value!");
          PHI->addIncoming(ST.Get(Map[PV]), B);
        }
      }
      Map[Current] = ST.Simplify(V);
    }
  }

  /// Starting from original value recursively iterates over def-use chain up to
  /// known ending values represented in a map. For each traversed phi/select
  /// inserts a placeholder Phi or Select.
  /// Reports all new created Phi/Select nodes by adding them to set.
  /// Also reports and order in what values have been traversed.
  void InsertPlaceholders(FoldAddrToValueMapping &Map,
                          SmallVectorImpl<Value *> &TraverseOrder,
                          SimplificationTracker &ST) {
    SmallVector<Value *, 32> Worklist;
    assert((isa<PHINode>(Original) || isa<SelectInst>(Original)) &&
           "Address must be a Phi or Select node");
    auto *Dummy = PoisonValue::get(CommonType);
    Worklist.push_back(Original);
    while (!Worklist.empty()) {
      Value *Current = Worklist.pop_back_val();
      // if it is already visited or it is an ending value then skip it.
      if (Map.contains(Current))
        continue;
      TraverseOrder.push_back(Current);

      // CurrentValue must be a Phi node or select. All others must be covered
      // by anchors.
      if (SelectInst *CurrentSelect = dyn_cast<SelectInst>(Current)) {
        // Is it OK to get metadata from OrigSelect?!
        // Create a Select placeholder with dummy value.
        SelectInst *Select =
            SelectInst::Create(CurrentSelect->getCondition(), Dummy, Dummy,
                               CurrentSelect->getName(),
                               CurrentSelect->getIterator(), CurrentSelect);
        Map[Current] = Select;
        ST.insertNewSelect(Select);
        // We are interested in True and False values.
        Worklist.push_back(CurrentSelect->getTrueValue());
        Worklist.push_back(CurrentSelect->getFalseValue());
      } else {
        // It must be a Phi node then.
        PHINode *CurrentPhi = cast<PHINode>(Current);
        unsigned PredCount = CurrentPhi->getNumIncomingValues();
        PHINode *PHI =
            PHINode::Create(CommonType, PredCount, "sunk_phi", CurrentPhi->getIterator());
        Map[Current] = PHI;
        ST.insertNewPhi(PHI);
        append_range(Worklist, CurrentPhi->incoming_values());
      }
    }
  }

  bool addrModeCombiningAllowed() {
    if (DisableComplexAddrModes)
      return false;
    switch (DifferentField) {
    default:
      return false;
    case ExtAddrMode::BaseRegField:
      return AddrSinkCombineBaseReg;
    case ExtAddrMode::BaseGVField:
      return AddrSinkCombineBaseGV;
    case ExtAddrMode::BaseOffsField:
      return AddrSinkCombineBaseOffs;
    case ExtAddrMode::ScaledRegField:
      return AddrSinkCombineScaledReg;
    }
  }
};
} // end anonymous namespace

/// Try adding ScaleReg*Scale to the current addressing mode.
/// Return true and update AddrMode if this addr mode is legal for the target,
/// false if not.
bool AddressingModeMatcher::matchScaledValue(Value *ScaleReg, int64_t Scale,
                                             unsigned Depth) {
  // If Scale is 1, then this is the same as adding ScaleReg to the addressing
  // mode.  Just process that directly.
  if (Scale == 1)
    return matchAddr(ScaleReg, Depth);

  // If the scale is 0, it takes nothing to add this.
  if (Scale == 0)
    return true;

  // If we already have a scale of this value, we can add to it, otherwise, we
  // need an available scale field.
  if (AddrMode.Scale != 0 && AddrMode.ScaledReg != ScaleReg)
    return false;

  ExtAddrMode TestAddrMode = AddrMode;

  // Add scale to turn X*4+X*3 -> X*7.  This could also do things like
  // [A+B + A*7] -> [B+A*8].
  TestAddrMode.Scale += Scale;
  TestAddrMode.ScaledReg = ScaleReg;

  // If the new address isn't legal, bail out.
  if (!TLI.isLegalAddressingMode(DL, TestAddrMode, AccessTy, AddrSpace))
    return false;

  // It was legal, so commit it.
  AddrMode = TestAddrMode;

  // Okay, we decided that we can add ScaleReg+Scale to AddrMode.  Check now
  // to see if ScaleReg is actually X+C.  If so, we can turn this into adding
  // X*Scale + C*Scale to addr mode. If we found available IV increment, do not
  // go any further: we can reuse it and cannot eliminate it.
  ConstantInt *CI = nullptr;
  Value *AddLHS = nullptr;
  if (isa<Instruction>(ScaleReg) && // not a constant expr.
      match(ScaleReg, m_Add(m_Value(AddLHS), m_ConstantInt(CI))) &&
      !isIVIncrement(ScaleReg, &LI) && CI->getValue().isSignedIntN(64)) {
    TestAddrMode.InBounds = false;
    TestAddrMode.ScaledReg = AddLHS;
    TestAddrMode.BaseOffs += CI->getSExtValue() * TestAddrMode.Scale;

    // If this addressing mode is legal, commit it and remember that we folded
    // this instruction.
    if (TLI.isLegalAddressingMode(DL, TestAddrMode, AccessTy, AddrSpace)) {
      AddrModeInsts.push_back(cast<Instruction>(ScaleReg));
      AddrMode = TestAddrMode;
      return true;
    }
    // Restore status quo.
    TestAddrMode = AddrMode;
  }

  // If this is an add recurrence with a constant step, return the increment
  // instruction and the canonicalized step.
  auto GetConstantStep =
      [this](const Value *V) -> std::optional<std::pair<Instruction *, APInt>> {
    auto *PN = dyn_cast<PHINode>(V);
    if (!PN)
      return std::nullopt;
    auto IVInc = getIVIncrement(PN, &LI);
    if (!IVInc)
      return std::nullopt;
    // TODO: The result of the intrinsics above is two-complement. However when
    // IV inc is expressed as add or sub, iv.next is potentially a poison value.
    // If it has nuw or nsw flags, we need to make sure that these flags are
    // inferrable at the point of memory instruction. Otherwise we are replacing
    // well-defined two-complement computation with poison. Currently, to avoid
    // potentially complex analysis needed to prove this, we reject such cases.
    if (auto *OIVInc = dyn_cast<OverflowingBinaryOperator>(IVInc->first))
      if (OIVInc->hasNoSignedWrap() || OIVInc->hasNoUnsignedWrap())
        return std::nullopt;
    if (auto *ConstantStep = dyn_cast<ConstantInt>(IVInc->second))
      return std::make_pair(IVInc->first, ConstantStep->getValue());
    return std::nullopt;
  };

  // Try to account for the following special case:
  // 1. ScaleReg is an inductive variable;
  // 2. We use it with non-zero offset;
  // 3. IV's increment is available at the point of memory instruction.
  //
  // In this case, we may reuse the IV increment instead of the IV Phi to
  // achieve the following advantages:
  // 1. If IV step matches the offset, we will have no need in the offset;
  // 2. Even if they don't match, we will reduce the overlap of living IV
  //    and IV increment, that will potentially lead to better register
  //    assignment.
  if (AddrMode.BaseOffs) {
    if (auto IVStep = GetConstantStep(ScaleReg)) {
      Instruction *IVInc = IVStep->first;
      // The following assert is important to ensure a lack of infinite loops.
      // This transforms is (intentionally) the inverse of the one just above.
      // If they don't agree on the definition of an increment, we'd alternate
      // back and forth indefinitely.
      assert(isIVIncrement(IVInc, &LI) && "implied by GetConstantStep");
      APInt Step = IVStep->second;
      APInt Offset = Step * AddrMode.Scale;
      if (Offset.isSignedIntN(64)) {
        TestAddrMode.InBounds = false;
        TestAddrMode.ScaledReg = IVInc;
        TestAddrMode.BaseOffs -= Offset.getLimitedValue();
        // If this addressing mode is legal, commit it..
        // (Note that we defer the (expensive) domtree base legality check
        // to the very last possible point.)
        if (TLI.isLegalAddressingMode(DL, TestAddrMode, AccessTy, AddrSpace) &&
            getDTFn().dominates(IVInc, MemoryInst)) {
          AddrModeInsts.push_back(cast<Instruction>(IVInc));
          AddrMode = TestAddrMode;
          return true;
        }
        // Restore status quo.
        TestAddrMode = AddrMode;
      }
    }
  }

  // Otherwise, just return what we have.
  return true;
}

/// This is a little filter, which returns true if an addressing computation
/// involving I might be folded into a load/store accessing it.
/// This doesn't need to be perfect, but needs to accept at least
/// the set of instructions that MatchOperationAddr can.
static bool MightBeFoldableInst(Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
    // Don't touch identity bitcasts.
    if (I->getType() == I->getOperand(0)->getType())
      return false;
    return I->getType()->isIntOrPtrTy();
  case Instruction::PtrToInt:
    // PtrToInt is always a noop, as we know that the int type is pointer sized.
    return true;
  case Instruction::IntToPtr:
    // We know the input is intptr_t, so this is foldable.
    return true;
  case Instruction::Add:
    return true;
  case Instruction::Mul:
  case Instruction::Shl:
    // Can only handle X*C and X << C.
    return isa<ConstantInt>(I->getOperand(1));
  case Instruction::GetElementPtr:
    return true;
  default:
    return false;
  }
}

/// Check whether or not \p Val is a legal instruction for \p TLI.
/// \note \p Val is assumed to be the product of some type promotion.
/// Therefore if \p Val has an undefined state in \p TLI, this is assumed
/// to be legal, as the non-promoted value would have had the same state.
static bool isPromotedInstructionLegal(const TargetLowering &TLI,
                                       const DataLayout &DL, Value *Val) {
  Instruction *PromotedInst = dyn_cast<Instruction>(Val);
  if (!PromotedInst)
    return false;
  int ISDOpcode = TLI.InstructionOpcodeToISD(PromotedInst->getOpcode());
  // If the ISDOpcode is undefined, it was undefined before the promotion.
  if (!ISDOpcode)
    return true;
  // Otherwise, check if the promoted instruction is legal or not.
  return TLI.isOperationLegalOrCustom(
      ISDOpcode, TLI.getValueType(DL, PromotedInst->getType()));
}

namespace {

/// Hepler class to perform type promotion.
class TypePromotionHelper {
  /// Utility function to add a promoted instruction \p ExtOpnd to
  /// \p PromotedInsts and record the type of extension we have seen.
  static void addPromotedInst(InstrToOrigTy &PromotedInsts,
                              Instruction *ExtOpnd, bool IsSExt) {
    ExtType ExtTy = IsSExt ? SignExtension : ZeroExtension;
    InstrToOrigTy::iterator It = PromotedInsts.find(ExtOpnd);
    if (It != PromotedInsts.end()) {
      // If the new extension is same as original, the information in
      // PromotedInsts[ExtOpnd] is still correct.
      if (It->second.getInt() == ExtTy)
        return;

      // Now the new extension is different from old extension, we make
      // the type information invalid by setting extension type to
      // BothExtension.
      ExtTy = BothExtension;
    }
    PromotedInsts[ExtOpnd] = TypeIsSExt(ExtOpnd->getType(), ExtTy);
  }

  /// Utility function to query the original type of instruction \p Opnd
  /// with a matched extension type. If the extension doesn't match, we
  /// cannot use the information we had on the original type.
  /// BothExtension doesn't match any extension type.
  static const Type *getOrigType(const InstrToOrigTy &PromotedInsts,
                                 Instruction *Opnd, bool IsSExt) {
    ExtType ExtTy = IsSExt ? SignExtension : ZeroExtension;
    InstrToOrigTy::const_iterator It = PromotedInsts.find(Opnd);
    if (It != PromotedInsts.end() && It->second.getInt() == ExtTy)
      return It->second.getPointer();
    return nullptr;
  }

  /// Utility function to check whether or not a sign or zero extension
  /// of \p Inst with \p ConsideredExtType can be moved through \p Inst by
  /// either using the operands of \p Inst or promoting \p Inst.
  /// The type of the extension is defined by \p IsSExt.
  /// In other words, check if:
  /// ext (Ty Inst opnd1 opnd2 ... opndN) to ConsideredExtType.
  /// #1 Promotion applies:
  /// ConsideredExtType Inst (ext opnd1 to ConsideredExtType, ...).
  /// #2 Operand reuses:
  /// ext opnd1 to ConsideredExtType.
  /// \p PromotedInsts maps the instructions to their type before promotion.
  static bool canGetThrough(const Instruction *Inst, Type *ConsideredExtType,
                            const InstrToOrigTy &PromotedInsts, bool IsSExt);

  /// Utility function to determine if \p OpIdx should be promoted when
  /// promoting \p Inst.
  static bool shouldExtOperand(const Instruction *Inst, int OpIdx) {
    return !(isa<SelectInst>(Inst) && OpIdx == 0);
  }

  /// Utility function to promote the operand of \p Ext when this
  /// operand is a promotable trunc or sext or zext.
  /// \p PromotedInsts maps the instructions to their type before promotion.
  /// \p CreatedInstsCost[out] contains the cost of all instructions
  /// created to promote the operand of Ext.
  /// Newly added extensions are inserted in \p Exts.
  /// Newly added truncates are inserted in \p Truncs.
  /// Should never be called directly.
  /// \return The promoted value which is used instead of Ext.
  static Value *promoteOperandForTruncAndAnyExt(
      Instruction *Ext, TypePromotionTransaction &TPT,
      InstrToOrigTy &PromotedInsts, unsigned &CreatedInstsCost,
      SmallVectorImpl<Instruction *> *Exts,
      SmallVectorImpl<Instruction *> *Truncs, const TargetLowering &TLI);

  /// Utility function to promote the operand of \p Ext when this
  /// operand is promotable and is not a supported trunc or sext.
  /// \p PromotedInsts maps the instructions to their type before promotion.
  /// \p CreatedInstsCost[out] contains the cost of all the instructions
  /// created to promote the operand of Ext.
  /// Newly added extensions are inserted in \p Exts.
  /// Newly added truncates are inserted in \p Truncs.
  /// Should never be called directly.
  /// \return The promoted value which is used instead of Ext.
  static Value *promoteOperandForOther(Instruction *Ext,
                                       TypePromotionTransaction &TPT,
                                       InstrToOrigTy &PromotedInsts,
                                       unsigned &CreatedInstsCost,
                                       SmallVectorImpl<Instruction *> *Exts,
                                       SmallVectorImpl<Instruction *> *Truncs,
                                       const TargetLowering &TLI, bool IsSExt);

  /// \see promoteOperandForOther.
  static Value *signExtendOperandForOther(
      Instruction *Ext, TypePromotionTransaction &TPT,
      InstrToOrigTy &PromotedInsts, unsigned &CreatedInstsCost,
      SmallVectorImpl<Instruction *> *Exts,
      SmallVectorImpl<Instruction *> *Truncs, const TargetLowering &TLI) {
    return promoteOperandForOther(Ext, TPT, PromotedInsts, CreatedInstsCost,
                                  Exts, Truncs, TLI, true);
  }

  /// \see promoteOperandForOther.
  static Value *zeroExtendOperandForOther(
      Instruction *Ext, TypePromotionTransaction &TPT,
      InstrToOrigTy &PromotedInsts, unsigned &CreatedInstsCost,
      SmallVectorImpl<Instruction *> *Exts,
      SmallVectorImpl<Instruction *> *Truncs, const TargetLowering &TLI) {
    return promoteOperandForOther(Ext, TPT, PromotedInsts, CreatedInstsCost,
                                  Exts, Truncs, TLI, false);
  }

public:
  /// Type for the utility function that promotes the operand of Ext.
  using Action = Value *(*)(Instruction *Ext, TypePromotionTransaction &TPT,
                            InstrToOrigTy &PromotedInsts,
                            unsigned &CreatedInstsCost,
                            SmallVectorImpl<Instruction *> *Exts,
                            SmallVectorImpl<Instruction *> *Truncs,
                            const TargetLowering &TLI);

  /// Given a sign/zero extend instruction \p Ext, return the appropriate
  /// action to promote the operand of \p Ext instead of using Ext.
  /// \return NULL if no promotable action is possible with the current
  /// sign extension.
  /// \p InsertedInsts keeps track of all the instructions inserted by the
  /// other CodeGenPrepare optimizations. This information is important
  /// because we do not want to promote these instructions as CodeGenPrepare
  /// will reinsert them later. Thus creating an infinite loop: create/remove.
  /// \p PromotedInsts maps the instructions to their type before promotion.
  static Action getAction(Instruction *Ext, const SetOfInstrs &InsertedInsts,
                          const TargetLowering &TLI,
                          const InstrToOrigTy &PromotedInsts);
};

} // end anonymous namespace

bool TypePromotionHelper::canGetThrough(const Instruction *Inst,
                                        Type *ConsideredExtType,
                                        const InstrToOrigTy &PromotedInsts,
                                        bool IsSExt) {
  // The promotion helper does not know how to deal with vector types yet.
  // To be able to fix that, we would need to fix the places where we
  // statically extend, e.g., constants and such.
  if (Inst->getType()->isVectorTy())
    return false;

  // We can always get through zext.
  if (isa<ZExtInst>(Inst))
    return true;

  // sext(sext) is ok too.
  if (IsSExt && isa<SExtInst>(Inst))
    return true;

  // We can get through binary operator, if it is legal. In other words, the
  // binary operator must have a nuw or nsw flag.
  if (const auto *BinOp = dyn_cast<BinaryOperator>(Inst))
    if (isa<OverflowingBinaryOperator>(BinOp) &&
        ((!IsSExt && BinOp->hasNoUnsignedWrap()) ||
         (IsSExt && BinOp->hasNoSignedWrap())))
      return true;

  // ext(and(opnd, cst)) --> and(ext(opnd), ext(cst))
  if ((Inst->getOpcode() == Instruction::And ||
       Inst->getOpcode() == Instruction::Or))
    return true;

  // ext(xor(opnd, cst)) --> xor(ext(opnd), ext(cst))
  if (Inst->getOpcode() == Instruction::Xor) {
    // Make sure it is not a NOT.
    if (const auto *Cst = dyn_cast<ConstantInt>(Inst->getOperand(1)))
      if (!Cst->getValue().isAllOnes())
        return true;
  }

  // zext(shrl(opnd, cst)) --> shrl(zext(opnd), zext(cst))
  // It may change a poisoned value into a regular value, like
  //     zext i32 (shrl i8 %val, 12)  -->  shrl i32 (zext i8 %val), 12
  //          poisoned value                    regular value
  // It should be OK since undef covers valid value.
  if (Inst->getOpcode() == Instruction::LShr && !IsSExt)
    return true;

  // and(ext(shl(opnd, cst)), cst) --> and(shl(ext(opnd), ext(cst)), cst)
  // It may change a poisoned value into a regular value, like
  //     zext i32 (shl i8 %val, 12)  -->  shl i32 (zext i8 %val), 12
  //          poisoned value                    regular value
  // It should be OK since undef covers valid value.
  if (Inst->getOpcode() == Instruction::Shl && Inst->hasOneUse()) {
    const auto *ExtInst = cast<const Instruction>(*Inst->user_begin());
    if (ExtInst->hasOneUse()) {
      const auto *AndInst = dyn_cast<const Instruction>(*ExtInst->user_begin());
      if (AndInst && AndInst->getOpcode() == Instruction::And) {
        const auto *Cst = dyn_cast<ConstantInt>(AndInst->getOperand(1));
        if (Cst &&
            Cst->getValue().isIntN(Inst->getType()->getIntegerBitWidth()))
          return true;
      }
    }
  }

  // Check if we can do the following simplification.
  // ext(trunc(opnd)) --> ext(opnd)
  if (!isa<TruncInst>(Inst))
    return false;

  Value *OpndVal = Inst->getOperand(0);
  // Check if we can use this operand in the extension.
  // If the type is larger than the result type of the extension, we cannot.
  if (!OpndVal->getType()->isIntegerTy() ||
      OpndVal->getType()->getIntegerBitWidth() >
          ConsideredExtType->getIntegerBitWidth())
    return false;

  // If the operand of the truncate is not an instruction, we will not have
  // any information on the dropped bits.
  // (Actually we could for constant but it is not worth the extra logic).
  Instruction *Opnd = dyn_cast<Instruction>(OpndVal);
  if (!Opnd)
    return false;

  // Check if the source of the type is narrow enough.
  // I.e., check that trunc just drops extended bits of the same kind of
  // the extension.
  // #1 get the type of the operand and check the kind of the extended bits.
  const Type *OpndType = getOrigType(PromotedInsts, Opnd, IsSExt);
  if (OpndType)
    ;
  else if ((IsSExt && isa<SExtInst>(Opnd)) || (!IsSExt && isa<ZExtInst>(Opnd)))
    OpndType = Opnd->getOperand(0)->getType();
  else
    return false;

  // #2 check that the truncate just drops extended bits.
  return Inst->getType()->getIntegerBitWidth() >=
         OpndType->getIntegerBitWidth();
}

TypePromotionHelper::Action TypePromotionHelper::getAction(
    Instruction *Ext, const SetOfInstrs &InsertedInsts,
    const TargetLowering &TLI, const InstrToOrigTy &PromotedInsts) {
  assert((isa<SExtInst>(Ext) || isa<ZExtInst>(Ext)) &&
         "Unexpected instruction type");
  Instruction *ExtOpnd = dyn_cast<Instruction>(Ext->getOperand(0));
  Type *ExtTy = Ext->getType();
  bool IsSExt = isa<SExtInst>(Ext);
  // If the operand of the extension is not an instruction, we cannot
  // get through.
  // If it, check we can get through.
  if (!ExtOpnd || !canGetThrough(ExtOpnd, ExtTy, PromotedInsts, IsSExt))
    return nullptr;

  // Do not promote if the operand has been added by codegenprepare.
  // Otherwise, it means we are undoing an optimization that is likely to be
  // redone, thus causing potential infinite loop.
  if (isa<TruncInst>(ExtOpnd) && InsertedInsts.count(ExtOpnd))
    return nullptr;

  // SExt or Trunc instructions.
  // Return the related handler.
  if (isa<SExtInst>(ExtOpnd) || isa<TruncInst>(ExtOpnd) ||
      isa<ZExtInst>(ExtOpnd))
    return promoteOperandForTruncAndAnyExt;

  // Regular instruction.
  // Abort early if we will have to insert non-free instructions.
  if (!ExtOpnd->hasOneUse() && !TLI.isTruncateFree(ExtTy, ExtOpnd->getType()))
    return nullptr;
  return IsSExt ? signExtendOperandForOther : zeroExtendOperandForOther;
}

Value *TypePromotionHelper::promoteOperandForTruncAndAnyExt(
    Instruction *SExt, TypePromotionTransaction &TPT,
    InstrToOrigTy &PromotedInsts, unsigned &CreatedInstsCost,
    SmallVectorImpl<Instruction *> *Exts,
    SmallVectorImpl<Instruction *> *Truncs, const TargetLowering &TLI) {
  // By construction, the operand of SExt is an instruction. Otherwise we cannot
  // get through it and this method should not be called.
  Instruction *SExtOpnd = cast<Instruction>(SExt->getOperand(0));
  Value *ExtVal = SExt;
  bool HasMergedNonFreeExt = false;
  if (isa<ZExtInst>(SExtOpnd)) {
    // Replace s|zext(zext(opnd))
    // => zext(opnd).
    HasMergedNonFreeExt = !TLI.isExtFree(SExtOpnd);
    Value *ZExt =
        TPT.createZExt(SExt, SExtOpnd->getOperand(0), SExt->getType());
    TPT.replaceAllUsesWith(SExt, ZExt);
    TPT.eraseInstruction(SExt);
    ExtVal = ZExt;
  } else {
    // Replace z|sext(trunc(opnd)) or sext(sext(opnd))
    // => z|sext(opnd).
    TPT.setOperand(SExt, 0, SExtOpnd->getOperand(0));
  }
  CreatedInstsCost = 0;

  // Remove dead code.
  if (SExtOpnd->use_empty())
    TPT.eraseInstruction(SExtOpnd);

  // Check if the extension is still needed.
  Instruction *ExtInst = dyn_cast<Instruction>(ExtVal);
  if (!ExtInst || ExtInst->getType() != ExtInst->getOperand(0)->getType()) {
    if (ExtInst) {
      if (Exts)
        Exts->push_back(ExtInst);
      CreatedInstsCost = !TLI.isExtFree(ExtInst) && !HasMergedNonFreeExt;
    }
    return ExtVal;
  }

  // At this point we have: ext ty opnd to ty.
  // Reassign the uses of ExtInst to the opnd and remove ExtInst.
  Value *NextVal = ExtInst->getOperand(0);
  TPT.eraseInstruction(ExtInst, NextVal);
  return NextVal;
}

Value *TypePromotionHelper::promoteOperandForOther(
    Instruction *Ext, TypePromotionTransaction &TPT,
    InstrToOrigTy &PromotedInsts, unsigned &CreatedInstsCost,
    SmallVectorImpl<Instruction *> *Exts,
    SmallVectorImpl<Instruction *> *Truncs, const TargetLowering &TLI,
    bool IsSExt) {
  // By construction, the operand of Ext is an instruction. Otherwise we cannot
  // get through it and this method should not be called.
  Instruction *ExtOpnd = cast<Instruction>(Ext->getOperand(0));
  CreatedInstsCost = 0;
  if (!ExtOpnd->hasOneUse()) {
    // ExtOpnd will be promoted.
    // All its uses, but Ext, will need to use a truncated value of the
    // promoted version.
    // Create the truncate now.
    Value *Trunc = TPT.createTrunc(Ext, ExtOpnd->getType());
    if (Instruction *ITrunc = dyn_cast<Instruction>(Trunc)) {
      // Insert it just after the definition.
      ITrunc->moveAfter(ExtOpnd);
      if (Truncs)
        Truncs->push_back(ITrunc);
    }

    TPT.replaceAllUsesWith(ExtOpnd, Trunc);
    // Restore the operand of Ext (which has been replaced by the previous call
    // to replaceAllUsesWith) to avoid creating a cycle trunc <-> sext.
    TPT.setOperand(Ext, 0, ExtOpnd);
  }

  // Get through the Instruction:
  // 1. Update its type.
  // 2. Replace the uses of Ext by Inst.
  // 3. Extend each operand that needs to be extended.

  // Remember the original type of the instruction before promotion.
  // This is useful to know that the high bits are sign extended bits.
  addPromotedInst(PromotedInsts, ExtOpnd, IsSExt);
  // Step #1.
  TPT.mutateType(ExtOpnd, Ext->getType());
  // Step #2.
  TPT.replaceAllUsesWith(Ext, ExtOpnd);
  // Step #3.
  LLVM_DEBUG(dbgs() << "Propagate Ext to operands\n");
  for (int OpIdx = 0, EndOpIdx = ExtOpnd->getNumOperands(); OpIdx != EndOpIdx;
       ++OpIdx) {
    LLVM_DEBUG(dbgs() << "Operand:\n" << *(ExtOpnd->getOperand(OpIdx)) << '\n');
    if (ExtOpnd->getOperand(OpIdx)->getType() == Ext->getType() ||
        !shouldExtOperand(ExtOpnd, OpIdx)) {
      LLVM_DEBUG(dbgs() << "No need to propagate\n");
      continue;
    }
    // Check if we can statically extend the operand.
    Value *Opnd = ExtOpnd->getOperand(OpIdx);
    if (const ConstantInt *Cst = dyn_cast<ConstantInt>(Opnd)) {
      LLVM_DEBUG(dbgs() << "Statically extend\n");
      unsigned BitWidth = Ext->getType()->getIntegerBitWidth();
      APInt CstVal = IsSExt ? Cst->getValue().sext(BitWidth)
                            : Cst->getValue().zext(BitWidth);
      TPT.setOperand(ExtOpnd, OpIdx, ConstantInt::get(Ext->getType(), CstVal));
      continue;
    }
    // UndefValue are typed, so we have to statically sign extend them.
    if (isa<UndefValue>(Opnd)) {
      LLVM_DEBUG(dbgs() << "Statically extend\n");
      TPT.setOperand(ExtOpnd, OpIdx, UndefValue::get(Ext->getType()));
      continue;
    }

    // Otherwise we have to explicitly sign extend the operand.
    Value *ValForExtOpnd = IsSExt
                               ? TPT.createSExt(ExtOpnd, Opnd, Ext->getType())
                               : TPT.createZExt(ExtOpnd, Opnd, Ext->getType());
    TPT.setOperand(ExtOpnd, OpIdx, ValForExtOpnd);
    Instruction *InstForExtOpnd = dyn_cast<Instruction>(ValForExtOpnd);
    if (!InstForExtOpnd)
      continue;

    if (Exts)
      Exts->push_back(InstForExtOpnd);

    CreatedInstsCost += !TLI.isExtFree(InstForExtOpnd);
  }
  LLVM_DEBUG(dbgs() << "Extension is useless now\n");
  TPT.eraseInstruction(Ext);
  return ExtOpnd;
}

/// Check whether or not promoting an instruction to a wider type is profitable.
/// \p NewCost gives the cost of extension instructions created by the
/// promotion.
/// \p OldCost gives the cost of extension instructions before the promotion
/// plus the number of instructions that have been
/// matched in the addressing mode the promotion.
/// \p PromotedOperand is the value that has been promoted.
/// \return True if the promotion is profitable, false otherwise.
bool AddressingModeMatcher::isPromotionProfitable(
    unsigned NewCost, unsigned OldCost, Value *PromotedOperand) const {
  LLVM_DEBUG(dbgs() << "OldCost: " << OldCost << "\tNewCost: " << NewCost
                    << '\n');
  // The cost of the new extensions is greater than the cost of the
  // old extension plus what we folded.
  // This is not profitable.
  if (NewCost > OldCost)
    return false;
  if (NewCost < OldCost)
    return true;
  // The promotion is neutral but it may help folding the sign extension in
  // loads for instance.
  // Check that we did not create an illegal instruction.
  return isPromotedInstructionLegal(TLI, DL, PromotedOperand);
}

/// Given an instruction or constant expr, see if we can fold the operation
/// into the addressing mode. If so, update the addressing mode and return
/// true, otherwise return false without modifying AddrMode.
/// If \p MovedAway is not NULL, it contains the information of whether or
/// not AddrInst has to be folded into the addressing mode on success.
/// If \p MovedAway == true, \p AddrInst will not be part of the addressing
/// because it has been moved away.
/// Thus AddrInst must not be added in the matched instructions.
/// This state can happen when AddrInst is a sext, since it may be moved away.
/// Therefore, AddrInst may not be valid when MovedAway is true and it must
/// not be referenced anymore.
bool AddressingModeMatcher::matchOperationAddr(User *AddrInst, unsigned Opcode,
                                               unsigned Depth,
                                               bool *MovedAway) {
  // Avoid exponential behavior on extremely deep expression trees.
  if (Depth >= 5)
    return false;

  // By default, all matched instructions stay in place.
  if (MovedAway)
    *MovedAway = false;

  switch (Opcode) {
  case Instruction::PtrToInt:
    // PtrToInt is always a noop, as we know that the int type is pointer sized.
    return matchAddr(AddrInst->getOperand(0), Depth);
  case Instruction::IntToPtr: {
    auto AS = AddrInst->getType()->getPointerAddressSpace();
    auto PtrTy = MVT::getIntegerVT(DL.getPointerSizeInBits(AS));
    // This inttoptr is a no-op if the integer type is pointer sized.
    if (TLI.getValueType(DL, AddrInst->getOperand(0)->getType()) == PtrTy)
      return matchAddr(AddrInst->getOperand(0), Depth);
    return false;
  }
  case Instruction::BitCast:
    // BitCast is always a noop, and we can handle it as long as it is
    // int->int or pointer->pointer (we don't want int<->fp or something).
    if (AddrInst->getOperand(0)->getType()->isIntOrPtrTy() &&
        // Don't touch identity bitcasts.  These were probably put here by LSR,
        // and we don't want to mess around with them.  Assume it knows what it
        // is doing.
        AddrInst->getOperand(0)->getType() != AddrInst->getType())
      return matchAddr(AddrInst->getOperand(0), Depth);
    return false;
  case Instruction::AddrSpaceCast: {
    unsigned SrcAS =
        AddrInst->getOperand(0)->getType()->getPointerAddressSpace();
    unsigned DestAS = AddrInst->getType()->getPointerAddressSpace();
    if (TLI.getTargetMachine().isNoopAddrSpaceCast(SrcAS, DestAS))
      return matchAddr(AddrInst->getOperand(0), Depth);
    return false;
  }
  case Instruction::Add: {
    // Check to see if we can merge in one operand, then the other.  If so, we
    // win.
    ExtAddrMode BackupAddrMode = AddrMode;
    unsigned OldSize = AddrModeInsts.size();
    // Start a transaction at this point.
    // The LHS may match but not the RHS.
    // Therefore, we need a higher level restoration point to undo partially
    // matched operation.
    TypePromotionTransaction::ConstRestorationPt LastKnownGood =
        TPT.getRestorationPoint();

    // Try to match an integer constant second to increase its chance of ending
    // up in `BaseOffs`, resp. decrease its chance of ending up in `BaseReg`.
    int First = 0, Second = 1;
    if (isa<ConstantInt>(AddrInst->getOperand(First))
      && !isa<ConstantInt>(AddrInst->getOperand(Second)))
        std::swap(First, Second);
    AddrMode.InBounds = false;
    if (matchAddr(AddrInst->getOperand(First), Depth + 1) &&
        matchAddr(AddrInst->getOperand(Second), Depth + 1))
      return true;

    // Restore the old addr mode info.
    AddrMode = BackupAddrMode;
    AddrModeInsts.resize(OldSize);
    TPT.rollback(LastKnownGood);

    // Otherwise this was over-aggressive.  Try merging operands in the opposite
    // order.
    if (matchAddr(AddrInst->getOperand(Second), Depth + 1) &&
        matchAddr(AddrInst->getOperand(First), Depth + 1))
      return true;

    // Otherwise we definitely can't merge the ADD in.
    AddrMode = BackupAddrMode;
    AddrModeInsts.resize(OldSize);
    TPT.rollback(LastKnownGood);
    break;
  }
  // case Instruction::Or:
  //  TODO: We can handle "Or Val, Imm" iff this OR is equivalent to an ADD.
  // break;
  case Instruction::Mul:
  case Instruction::Shl: {
    // Can only handle X*C and X << C.
    AddrMode.InBounds = false;
    ConstantInt *RHS = dyn_cast<ConstantInt>(AddrInst->getOperand(1));
    if (!RHS || RHS->getBitWidth() > 64)
      return false;
    int64_t Scale = Opcode == Instruction::Shl
                        ? 1LL << RHS->getLimitedValue(RHS->getBitWidth() - 1)
                        : RHS->getSExtValue();

    return matchScaledValue(AddrInst->getOperand(0), Scale, Depth);
  }
  case Instruction::GetElementPtr: {
    // Scan the GEP.  We check it if it contains constant offsets and at most
    // one variable offset.
    int VariableOperand = -1;
    unsigned VariableScale = 0;

    int64_t ConstantOffset = 0;
    gep_type_iterator GTI = gep_type_begin(AddrInst);
    for (unsigned i = 1, e = AddrInst->getNumOperands(); i != e; ++i, ++GTI) {
      if (StructType *STy = GTI.getStructTypeOrNull()) {
        const StructLayout *SL = DL.getStructLayout(STy);
        unsigned Idx =
            cast<ConstantInt>(AddrInst->getOperand(i))->getZExtValue();
        ConstantOffset += SL->getElementOffset(Idx);
      } else {
        TypeSize TS = GTI.getSequentialElementStride(DL);
        if (TS.isNonZero()) {
          // The optimisations below currently only work for fixed offsets.
          if (TS.isScalable())
            return false;
          int64_t TypeSize = TS.getFixedValue();
          if (ConstantInt *CI =
                  dyn_cast<ConstantInt>(AddrInst->getOperand(i))) {
            const APInt &CVal = CI->getValue();
            if (CVal.getSignificantBits() <= 64) {
              ConstantOffset += CVal.getSExtValue() * TypeSize;
              continue;
            }
          }
          // We only allow one variable index at the moment.
          if (VariableOperand != -1)
            return false;

          // Remember the variable index.
          VariableOperand = i;
          VariableScale = TypeSize;
        }
      }
    }

    // A common case is for the GEP to only do a constant offset.  In this case,
    // just add it to the disp field and check validity.
    if (VariableOperand == -1) {
      AddrMode.BaseOffs += ConstantOffset;
      if (matchAddr(AddrInst->getOperand(0), Depth + 1)) {
          if (!cast<GEPOperator>(AddrInst)->isInBounds())
            AddrMode.InBounds = false;
          return true;
      }
      AddrMode.BaseOffs -= ConstantOffset;

      if (EnableGEPOffsetSplit && isa<GetElementPtrInst>(AddrInst) &&
          TLI.shouldConsiderGEPOffsetSplit() && Depth == 0 &&
          ConstantOffset > 0) {
          // Record GEPs with non-zero offsets as candidates for splitting in
          // the event that the offset cannot fit into the r+i addressing mode.
          // Simple and common case that only one GEP is used in calculating the
          // address for the memory access.
          Value *Base = AddrInst->getOperand(0);
          auto *BaseI = dyn_cast<Instruction>(Base);
          auto *GEP = cast<GetElementPtrInst>(AddrInst);
          if (isa<Argument>(Base) || isa<GlobalValue>(Base) ||
              (BaseI && !isa<CastInst>(BaseI) &&
               !isa<GetElementPtrInst>(BaseI))) {
            // Make sure the parent block allows inserting non-PHI instructions
            // before the terminator.
            BasicBlock *Parent = BaseI ? BaseI->getParent()
                                       : &GEP->getFunction()->getEntryBlock();
            if (!Parent->getTerminator()->isEHPad())
            LargeOffsetGEP = std::make_pair(GEP, ConstantOffset);
          }
      }

      return false;
    }

    // Save the valid addressing mode in case we can't match.
    ExtAddrMode BackupAddrMode = AddrMode;
    unsigned OldSize = AddrModeInsts.size();

    // See if the scale and offset amount is valid for this target.
    AddrMode.BaseOffs += ConstantOffset;
    if (!cast<GEPOperator>(AddrInst)->isInBounds())
      AddrMode.InBounds = false;

    // Match the base operand of the GEP.
    if (!matchAddr(AddrInst->getOperand(0), Depth + 1)) {
      // If it couldn't be matched, just stuff the value in a register.
      if (AddrMode.HasBaseReg) {
        AddrMode = BackupAddrMode;
        AddrModeInsts.resize(OldSize);
        return false;
      }
      AddrMode.HasBaseReg = true;
      AddrMode.BaseReg = AddrInst->getOperand(0);
    }

    // Match the remaining variable portion of the GEP.
    if (!matchScaledValue(AddrInst->getOperand(VariableOperand), VariableScale,
                          Depth)) {
      // If it couldn't be matched, try stuffing the base into a register
      // instead of matching it, and retrying the match of the scale.
      AddrMode = BackupAddrMode;
      AddrModeInsts.resize(OldSize);
      if (AddrMode.HasBaseReg)
        return false;
      AddrMode.HasBaseReg = true;
      AddrMode.BaseReg = AddrInst->getOperand(0);
      AddrMode.BaseOffs += ConstantOffset;
      if (!matchScaledValue(AddrInst->getOperand(VariableOperand),
                            VariableScale, Depth)) {
        // If even that didn't work, bail.
        AddrMode = BackupAddrMode;
        AddrModeInsts.resize(OldSize);
        return false;
      }
    }

    return true;
  }
  case Instruction::SExt:
  case Instruction::ZExt: {
    Instruction *Ext = dyn_cast<Instruction>(AddrInst);
    if (!Ext)
      return false;

    // Try to move this ext out of the way of the addressing mode.
    // Ask for a method for doing so.
    TypePromotionHelper::Action TPH =
        TypePromotionHelper::getAction(Ext, InsertedInsts, TLI, PromotedInsts);
    if (!TPH)
      return false;

    TypePromotionTransaction::ConstRestorationPt LastKnownGood =
        TPT.getRestorationPoint();
    unsigned CreatedInstsCost = 0;
    unsigned ExtCost = !TLI.isExtFree(Ext);
    Value *PromotedOperand =
        TPH(Ext, TPT, PromotedInsts, CreatedInstsCost, nullptr, nullptr, TLI);
    // SExt has been moved away.
    // Thus either it will be rematched later in the recursive calls or it is
    // gone. Anyway, we must not fold it into the addressing mode at this point.
    // E.g.,
    // op = add opnd, 1
    // idx = ext op
    // addr = gep base, idx
    // is now:
    // promotedOpnd = ext opnd            <- no match here
    // op = promoted_add promotedOpnd, 1  <- match (later in recursive calls)
    // addr = gep base, op                <- match
    if (MovedAway)
      *MovedAway = true;

    assert(PromotedOperand &&
           "TypePromotionHelper should have filtered out those cases");

    ExtAddrMode BackupAddrMode = AddrMode;
    unsigned OldSize = AddrModeInsts.size();

    if (!matchAddr(PromotedOperand, Depth) ||
        // The total of the new cost is equal to the cost of the created
        // instructions.
        // The total of the old cost is equal to the cost of the extension plus
        // what we have saved in the addressing mode.
        !isPromotionProfitable(CreatedInstsCost,
                               ExtCost + (AddrModeInsts.size() - OldSize),
                               PromotedOperand)) {
      AddrMode = BackupAddrMode;
      AddrModeInsts.resize(OldSize);
      LLVM_DEBUG(dbgs() << "Sign extension does not pay off: rollback\n");
      TPT.rollback(LastKnownGood);
      return false;
    }
    return true;
  }
  case Instruction::Call:
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(AddrInst)) {
      if (II->getIntrinsicID() == Intrinsic::threadlocal_address) {
        GlobalValue &GV = cast<GlobalValue>(*II->getArgOperand(0));
        if (TLI.addressingModeSupportsTLS(GV))
          return matchAddr(AddrInst->getOperand(0), Depth);
      }
    }
    break;
  }
  return false;
}

/// If we can, try to add the value of 'Addr' into the current addressing mode.
/// If Addr can't be added to AddrMode this returns false and leaves AddrMode
/// unmodified. This assumes that Addr is either a pointer type or intptr_t
/// for the target.
///
bool AddressingModeMatcher::matchAddr(Value *Addr, unsigned Depth) {
  // Start a transaction at this point that we will rollback if the matching
  // fails.
  TypePromotionTransaction::ConstRestorationPt LastKnownGood =
      TPT.getRestorationPoint();
  if (ConstantInt *CI = dyn_cast<ConstantInt>(Addr)) {
    if (CI->getValue().isSignedIntN(64)) {
      // Fold in immediates if legal for the target.
      AddrMode.BaseOffs += CI->getSExtValue();
      if (TLI.isLegalAddressingMode(DL, AddrMode, AccessTy, AddrSpace))
        return true;
      AddrMode.BaseOffs -= CI->getSExtValue();
    }
  } else if (GlobalValue *GV = dyn_cast<GlobalValue>(Addr)) {
    // If this is a global variable, try to fold it into the addressing mode.
    if (!AddrMode.BaseGV) {
      AddrMode.BaseGV = GV;
      if (TLI.isLegalAddressingMode(DL, AddrMode, AccessTy, AddrSpace))
        return true;
      AddrMode.BaseGV = nullptr;
    }
  } else if (Instruction *I = dyn_cast<Instruction>(Addr)) {
    ExtAddrMode BackupAddrMode = AddrMode;
    unsigned OldSize = AddrModeInsts.size();

    // Check to see if it is possible to fold this operation.
    bool MovedAway = false;
    if (matchOperationAddr(I, I->getOpcode(), Depth, &MovedAway)) {
      // This instruction may have been moved away. If so, there is nothing
      // to check here.
      if (MovedAway)
        return true;
      // Okay, it's possible to fold this.  Check to see if it is actually
      // *profitable* to do so.  We use a simple cost model to avoid increasing
      // register pressure too much.
      if (I->hasOneUse() ||
          isProfitableToFoldIntoAddressingMode(I, BackupAddrMode, AddrMode)) {
        AddrModeInsts.push_back(I);
        return true;
      }

      // It isn't profitable to do this, roll back.
      AddrMode = BackupAddrMode;
      AddrModeInsts.resize(OldSize);
      TPT.rollback(LastKnownGood);
    }
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Addr)) {
    if (matchOperationAddr(CE, CE->getOpcode(), Depth))
      return true;
    TPT.rollback(LastKnownGood);
  } else if (isa<ConstantPointerNull>(Addr)) {
    // Null pointer gets folded without affecting the addressing mode.
    return true;
  }

  // Worse case, the target should support [reg] addressing modes. :)
  if (!AddrMode.HasBaseReg) {
    AddrMode.HasBaseReg = true;
    AddrMode.BaseReg = Addr;
    // Still check for legality in case the target supports [imm] but not [i+r].
    if (TLI.isLegalAddressingMode(DL, AddrMode, AccessTy, AddrSpace))
      return true;
    AddrMode.HasBaseReg = false;
    AddrMode.BaseReg = nullptr;
  }

  // If the base register is already taken, see if we can do [r+r].
  if (AddrMode.Scale == 0) {
    AddrMode.Scale = 1;
    AddrMode.ScaledReg = Addr;
    if (TLI.isLegalAddressingMode(DL, AddrMode, AccessTy, AddrSpace))
      return true;
    AddrMode.Scale = 0;
    AddrMode.ScaledReg = nullptr;
  }
  // Couldn't match.
  TPT.rollback(LastKnownGood);
  return false;
}

/// Check to see if all uses of OpVal by the specified inline asm call are due
/// to memory operands. If so, return true, otherwise return false.
static bool IsOperandAMemoryOperand(CallInst *CI, InlineAsm *IA, Value *OpVal,
                                    const TargetLowering &TLI,
                                    const TargetRegisterInfo &TRI) {
  const Function *F = CI->getFunction();
  TargetLowering::AsmOperandInfoVector TargetConstraints =
      TLI.ParseConstraints(F->getDataLayout(), &TRI, *CI);

  for (TargetLowering::AsmOperandInfo &OpInfo : TargetConstraints) {
    // Compute the constraint code and ConstraintType to use.
    TLI.ComputeConstraintToUse(OpInfo, SDValue());

    // If this asm operand is our Value*, and if it isn't an indirect memory
    // operand, we can't fold it!  TODO: Also handle C_Address?
    if (OpInfo.CallOperandVal == OpVal &&
        (OpInfo.ConstraintType != TargetLowering::C_Memory ||
         !OpInfo.isIndirect))
      return false;
  }

  return true;
}

/// Recursively walk all the uses of I until we find a memory use.
/// If we find an obviously non-foldable instruction, return true.
/// Add accessed addresses and types to MemoryUses.
static bool FindAllMemoryUses(
    Instruction *I, SmallVectorImpl<std::pair<Use *, Type *>> &MemoryUses,
    SmallPtrSetImpl<Instruction *> &ConsideredInsts, const TargetLowering &TLI,
    const TargetRegisterInfo &TRI, bool OptSize, ProfileSummaryInfo *PSI,
    BlockFrequencyInfo *BFI, unsigned &SeenInsts) {
  // If we already considered this instruction, we're done.
  if (!ConsideredInsts.insert(I).second)
    return false;

  // If this is an obviously unfoldable instruction, bail out.
  if (!MightBeFoldableInst(I))
    return true;

  // Loop over all the uses, recursively processing them.
  for (Use &U : I->uses()) {
    // Conservatively return true if we're seeing a large number or a deep chain
    // of users. This avoids excessive compilation times in pathological cases.
    if (SeenInsts++ >= MaxAddressUsersToScan)
      return true;

    Instruction *UserI = cast<Instruction>(U.getUser());
    if (LoadInst *LI = dyn_cast<LoadInst>(UserI)) {
      MemoryUses.push_back({&U, LI->getType()});
      continue;
    }

    if (StoreInst *SI = dyn_cast<StoreInst>(UserI)) {
      if (U.getOperandNo() != StoreInst::getPointerOperandIndex())
        return true; // Storing addr, not into addr.
      MemoryUses.push_back({&U, SI->getValueOperand()->getType()});
      continue;
    }

    if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(UserI)) {
      if (U.getOperandNo() != AtomicRMWInst::getPointerOperandIndex())
        return true; // Storing addr, not into addr.
      MemoryUses.push_back({&U, RMW->getValOperand()->getType()});
      continue;
    }

    if (AtomicCmpXchgInst *CmpX = dyn_cast<AtomicCmpXchgInst>(UserI)) {
      if (U.getOperandNo() != AtomicCmpXchgInst::getPointerOperandIndex())
        return true; // Storing addr, not into addr.
      MemoryUses.push_back({&U, CmpX->getCompareOperand()->getType()});
      continue;
    }

    if (CallInst *CI = dyn_cast<CallInst>(UserI)) {
      if (CI->hasFnAttr(Attribute::Cold)) {
        // If this is a cold call, we can sink the addressing calculation into
        // the cold path.  See optimizeCallInst
        bool OptForSize =
            OptSize || llvm::shouldOptimizeForSize(CI->getParent(), PSI, BFI);
        if (!OptForSize)
          continue;
      }

      InlineAsm *IA = dyn_cast<InlineAsm>(CI->getCalledOperand());
      if (!IA)
        return true;

      // If this is a memory operand, we're cool, otherwise bail out.
      if (!IsOperandAMemoryOperand(CI, IA, I, TLI, TRI))
        return true;
      continue;
    }

    if (FindAllMemoryUses(UserI, MemoryUses, ConsideredInsts, TLI, TRI, OptSize,
                          PSI, BFI, SeenInsts))
      return true;
  }

  return false;
}

static bool FindAllMemoryUses(
    Instruction *I, SmallVectorImpl<std::pair<Use *, Type *>> &MemoryUses,
    const TargetLowering &TLI, const TargetRegisterInfo &TRI, bool OptSize,
    ProfileSummaryInfo *PSI, BlockFrequencyInfo *BFI) {
  unsigned SeenInsts = 0;
  SmallPtrSet<Instruction *, 16> ConsideredInsts;
  return FindAllMemoryUses(I, MemoryUses, ConsideredInsts, TLI, TRI, OptSize,
                           PSI, BFI, SeenInsts);
}


/// Return true if Val is already known to be live at the use site that we're
/// folding it into. If so, there is no cost to include it in the addressing
/// mode. KnownLive1 and KnownLive2 are two values that we know are live at the
/// instruction already.
bool AddressingModeMatcher::valueAlreadyLiveAtInst(Value *Val,
                                                   Value *KnownLive1,
                                                   Value *KnownLive2) {
  // If Val is either of the known-live values, we know it is live!
  if (Val == nullptr || Val == KnownLive1 || Val == KnownLive2)
    return true;

  // All values other than instructions and arguments (e.g. constants) are live.
  if (!isa<Instruction>(Val) && !isa<Argument>(Val))
    return true;

  // If Val is a constant sized alloca in the entry block, it is live, this is
  // true because it is just a reference to the stack/frame pointer, which is
  // live for the whole function.
  if (AllocaInst *AI = dyn_cast<AllocaInst>(Val))
    if (AI->isStaticAlloca())
      return true;

  // Check to see if this value is already used in the memory instruction's
  // block.  If so, it's already live into the block at the very least, so we
  // can reasonably fold it.
  return Val->isUsedInBasicBlock(MemoryInst->getParent());
}

/// It is possible for the addressing mode of the machine to fold the specified
/// instruction into a load or store that ultimately uses it.
/// However, the specified instruction has multiple uses.
/// Given this, it may actually increase register pressure to fold it
/// into the load. For example, consider this code:
///
///     X = ...
///     Y = X+1
///     use(Y)   -> nonload/store
///     Z = Y+1
///     load Z
///
/// In this case, Y has multiple uses, and can be folded into the load of Z
/// (yielding load [X+2]).  However, doing this will cause both "X" and "X+1" to
/// be live at the use(Y) line.  If we don't fold Y into load Z, we use one
/// fewer register.  Since Y can't be folded into "use(Y)" we don't increase the
/// number of computations either.
///
/// Note that this (like most of CodeGenPrepare) is just a rough heuristic.  If
/// X was live across 'load Z' for other reasons, we actually *would* want to
/// fold the addressing mode in the Z case.  This would make Y die earlier.
bool AddressingModeMatcher::isProfitableToFoldIntoAddressingMode(
    Instruction *I, ExtAddrMode &AMBefore, ExtAddrMode &AMAfter) {
  if (IgnoreProfitability)
    return true;

  // AMBefore is the addressing mode before this instruction was folded into it,
  // and AMAfter is the addressing mode after the instruction was folded.  Get
  // the set of registers referenced by AMAfter and subtract out those
  // referenced by AMBefore: this is the set of values which folding in this
  // address extends the lifetime of.
  //
  // Note that there are only two potential values being referenced here,
  // BaseReg and ScaleReg (global addresses are always available, as are any
  // folded immediates).
  Value *BaseReg = AMAfter.BaseReg, *ScaledReg = AMAfter.ScaledReg;

  // If the BaseReg or ScaledReg was referenced by the previous addrmode, their
  // lifetime wasn't extended by adding this instruction.
  if (valueAlreadyLiveAtInst(BaseReg, AMBefore.BaseReg, AMBefore.ScaledReg))
    BaseReg = nullptr;
  if (valueAlreadyLiveAtInst(ScaledReg, AMBefore.BaseReg, AMBefore.ScaledReg))
    ScaledReg = nullptr;

  // If folding this instruction (and it's subexprs) didn't extend any live
  // ranges, we're ok with it.
  if (!BaseReg && !ScaledReg)
    return true;

  // If all uses of this instruction can have the address mode sunk into them,
  // we can remove the addressing mode and effectively trade one live register
  // for another (at worst.)  In this context, folding an addressing mode into
  // the use is just a particularly nice way of sinking it.
  SmallVector<std::pair<Use *, Type *>, 16> MemoryUses;
  if (FindAllMemoryUses(I, MemoryUses, TLI, TRI, OptSize, PSI, BFI))
    return false; // Has a non-memory, non-foldable use!

  // Now that we know that all uses of this instruction are part of a chain of
  // computation involving only operations that could theoretically be folded
  // into a memory use, loop over each of these memory operation uses and see
  // if they could  *actually* fold the instruction.  The assumption is that
  // addressing modes are cheap and that duplicating the computation involved
  // many times is worthwhile, even on a fastpath. For sinking candidates
  // (i.e. cold call sites), this serves as a way to prevent excessive code
  // growth since most architectures have some reasonable small and fast way to
  // compute an effective address.  (i.e LEA on x86)
  SmallVector<Instruction *, 32> MatchedAddrModeInsts;
  for (const std::pair<Use *, Type *> &Pair : MemoryUses) {
    Value *Address = Pair.first->get();
    Instruction *UserI = cast<Instruction>(Pair.first->getUser());
    Type *AddressAccessTy = Pair.second;
    unsigned AS = Address->getType()->getPointerAddressSpace();

    // Do a match against the root of this address, ignoring profitability. This
    // will tell us if the addressing mode for the memory operation will
    // *actually* cover the shared instruction.
    ExtAddrMode Result;
    std::pair<AssertingVH<GetElementPtrInst>, int64_t> LargeOffsetGEP(nullptr,
                                                                      0);
    TypePromotionTransaction::ConstRestorationPt LastKnownGood =
        TPT.getRestorationPoint();
    AddressingModeMatcher Matcher(MatchedAddrModeInsts, TLI, TRI, LI, getDTFn,
                                  AddressAccessTy, AS, UserI, Result,
                                  InsertedInsts, PromotedInsts, TPT,
                                  LargeOffsetGEP, OptSize, PSI, BFI);
    Matcher.IgnoreProfitability = true;
    bool Success = Matcher.matchAddr(Address, 0);
    (void)Success;
    assert(Success && "Couldn't select *anything*?");

    // The match was to check the profitability, the changes made are not
    // part of the original matcher. Therefore, they should be dropped
    // otherwise the original matcher will not present the right state.
    TPT.rollback(LastKnownGood);

    // If the match didn't cover I, then it won't be shared by it.
    if (!is_contained(MatchedAddrModeInsts, I))
      return false;

    MatchedAddrModeInsts.clear();
  }

  return true;
}

/// Return true if the specified values are defined in a
/// different basic block than BB.
static bool IsNonLocalValue(Value *V, BasicBlock *BB) {
  if (Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent() != BB;
  return false;
}

/// Sink addressing mode computation immediate before MemoryInst if doing so
/// can be done without increasing register pressure.  The need for the
/// register pressure constraint means this can end up being an all or nothing
/// decision for all uses of the same addressing computation.
///
/// Load and Store Instructions often have addressing modes that can do
/// significant amounts of computation. As such, instruction selection will try
/// to get the load or store to do as much computation as possible for the
/// program. The problem is that isel can only see within a single block. As
/// such, we sink as much legal addressing mode work into the block as possible.
///
/// This method is used to optimize both load/store and inline asms with memory
/// operands.  It's also used to sink addressing computations feeding into cold
/// call sites into their (cold) basic block.
///
/// The motivation for handling sinking into cold blocks is that doing so can
/// both enable other address mode sinking (by satisfying the register pressure
/// constraint above), and reduce register pressure globally (by removing the
/// addressing mode computation from the fast path entirely.).
bool CodeGenPrepare::optimizeMemoryInst(Instruction *MemoryInst, Value *Addr,
                                        Type *AccessTy, unsigned AddrSpace) {
  Value *Repl = Addr;

  // Try to collapse single-value PHI nodes.  This is necessary to undo
  // unprofitable PRE transformations.
  SmallVector<Value *, 8> worklist;
  SmallPtrSet<Value *, 16> Visited;
  worklist.push_back(Addr);

  // Use a worklist to iteratively look through PHI and select nodes, and
  // ensure that the addressing mode obtained from the non-PHI/select roots of
  // the graph are compatible.
  bool PhiOrSelectSeen = false;
  SmallVector<Instruction *, 16> AddrModeInsts;
  const SimplifyQuery SQ(*DL, TLInfo);
  AddressingModeCombiner AddrModes(SQ, Addr);
  TypePromotionTransaction TPT(RemovedInsts);
  TypePromotionTransaction::ConstRestorationPt LastKnownGood =
      TPT.getRestorationPoint();
  while (!worklist.empty()) {
    Value *V = worklist.pop_back_val();

    // We allow traversing cyclic Phi nodes.
    // In case of success after this loop we ensure that traversing through
    // Phi nodes ends up with all cases to compute address of the form
    //    BaseGV + Base + Scale * Index + Offset
    // where Scale and Offset are constans and BaseGV, Base and Index
    // are exactly the same Values in all cases.
    // It means that BaseGV, Scale and Offset dominate our memory instruction
    // and have the same value as they had in address computation represented
    // as Phi. So we can safely sink address computation to memory instruction.
    if (!Visited.insert(V).second)
      continue;

    // For a PHI node, push all of its incoming values.
    if (PHINode *P = dyn_cast<PHINode>(V)) {
      append_range(worklist, P->incoming_values());
      PhiOrSelectSeen = true;
      continue;
    }
    // Similar for select.
    if (SelectInst *SI = dyn_cast<SelectInst>(V)) {
      worklist.push_back(SI->getFalseValue());
      worklist.push_back(SI->getTrueValue());
      PhiOrSelectSeen = true;
      continue;
    }

    // For non-PHIs, determine the addressing mode being computed.  Note that
    // the result may differ depending on what other uses our candidate
    // addressing instructions might have.
    AddrModeInsts.clear();
    std::pair<AssertingVH<GetElementPtrInst>, int64_t> LargeOffsetGEP(nullptr,
                                                                      0);
    // Defer the query (and possible computation of) the dom tree to point of
    // actual use.  It's expected that most address matches don't actually need
    // the domtree.
    auto getDTFn = [MemoryInst, this]() -> const DominatorTree & {
      Function *F = MemoryInst->getParent()->getParent();
      return this->getDT(*F);
    };
    ExtAddrMode NewAddrMode = AddressingModeMatcher::Match(
        V, AccessTy, AddrSpace, MemoryInst, AddrModeInsts, *TLI, *LI, getDTFn,
        *TRI, InsertedInsts, PromotedInsts, TPT, LargeOffsetGEP, OptSize, PSI,
        BFI.get());

    GetElementPtrInst *GEP = LargeOffsetGEP.first;
    if (GEP && !NewGEPBases.count(GEP)) {
      // If splitting the underlying data structure can reduce the offset of a
      // GEP, collect the GEP.  Skip the GEPs that are the new bases of
      // previously split data structures.
      LargeOffsetGEPMap[GEP->getPointerOperand()].push_back(LargeOffsetGEP);
      LargeOffsetGEPID.insert(std::make_pair(GEP, LargeOffsetGEPID.size()));
    }

    NewAddrMode.OriginalValue = V;
    if (!AddrModes.addNewAddrMode(NewAddrMode))
      break;
  }

  // Try to combine the AddrModes we've collected. If we couldn't collect any,
  // or we have multiple but either couldn't combine them or combining them
  // wouldn't do anything useful, bail out now.
  if (!AddrModes.combineAddrModes()) {
    TPT.rollback(LastKnownGood);
    return false;
  }
  bool Modified = TPT.commit();

  // Get the combined AddrMode (or the only AddrMode, if we only had one).
  ExtAddrMode AddrMode = AddrModes.getAddrMode();

  // If all the instructions matched are already in this BB, don't do anything.
  // If we saw a Phi node then it is not local definitely, and if we saw a
  // select then we want to push the address calculation past it even if it's
  // already in this BB.
  if (!PhiOrSelectSeen && none_of(AddrModeInsts, [&](Value *V) {
        return IsNonLocalValue(V, MemoryInst->getParent());
      })) {
    LLVM_DEBUG(dbgs() << "CGP: Found      local addrmode: " << AddrMode
                      << "\n");
    return Modified;
  }

  // Insert this computation right after this user.  Since our caller is
  // scanning from the top of the BB to the bottom, reuse of the expr are
  // guaranteed to happen later.
  IRBuilder<> Builder(MemoryInst);

  // Now that we determined the addressing expression we want to use and know
  // that we have to sink it into this block.  Check to see if we have already
  // done this for some other load/store instr in this block.  If so, reuse
  // the computation.  Before attempting reuse, check if the address is valid
  // as it may have been erased.

  WeakTrackingVH SunkAddrVH = SunkAddrs[Addr];

  Value *SunkAddr = SunkAddrVH.pointsToAliveValue() ? SunkAddrVH : nullptr;
  Type *IntPtrTy = DL->getIntPtrType(Addr->getType());
  if (SunkAddr) {
    LLVM_DEBUG(dbgs() << "CGP: Reusing nonlocal addrmode: " << AddrMode
                      << " for " << *MemoryInst << "\n");
    if (SunkAddr->getType() != Addr->getType()) {
      if (SunkAddr->getType()->getPointerAddressSpace() !=
              Addr->getType()->getPointerAddressSpace() &&
          !DL->isNonIntegralPointerType(Addr->getType())) {
        // There are two reasons the address spaces might not match: a no-op
        // addrspacecast, or a ptrtoint/inttoptr pair. Either way, we emit a
        // ptrtoint/inttoptr pair to ensure we match the original semantics.
        // TODO: allow bitcast between different address space pointers with the
        // same size.
        SunkAddr = Builder.CreatePtrToInt(SunkAddr, IntPtrTy, "sunkaddr");
        SunkAddr =
            Builder.CreateIntToPtr(SunkAddr, Addr->getType(), "sunkaddr");
      } else
        SunkAddr = Builder.CreatePointerCast(SunkAddr, Addr->getType());
    }
  } else if (AddrSinkUsingGEPs || (!AddrSinkUsingGEPs.getNumOccurrences() &&
                                   SubtargetInfo->addrSinkUsingGEPs())) {
    // By default, we use the GEP-based method when AA is used later. This
    // prevents new inttoptr/ptrtoint pairs from degrading AA capabilities.
    LLVM_DEBUG(dbgs() << "CGP: SINKING nonlocal addrmode: " << AddrMode
                      << " for " << *MemoryInst << "\n");
    Value *ResultPtr = nullptr, *ResultIndex = nullptr;

    // First, find the pointer.
    if (AddrMode.BaseReg && AddrMode.BaseReg->getType()->isPointerTy()) {
      ResultPtr = AddrMode.BaseReg;
      AddrMode.BaseReg = nullptr;
    }

    if (AddrMode.Scale && AddrMode.ScaledReg->getType()->isPointerTy()) {
      // We can't add more than one pointer together, nor can we scale a
      // pointer (both of which seem meaningless).
      if (ResultPtr || AddrMode.Scale != 1)
        return Modified;

      ResultPtr = AddrMode.ScaledReg;
      AddrMode.Scale = 0;
    }

    // It is only safe to sign extend the BaseReg if we know that the math
    // required to create it did not overflow before we extend it. Since
    // the original IR value was tossed in favor of a constant back when
    // the AddrMode was created we need to bail out gracefully if widths
    // do not match instead of extending it.
    //
    // (See below for code to add the scale.)
    if (AddrMode.Scale) {
      Type *ScaledRegTy = AddrMode.ScaledReg->getType();
      if (cast<IntegerType>(IntPtrTy)->getBitWidth() >
          cast<IntegerType>(ScaledRegTy)->getBitWidth())
        return Modified;
    }

    GlobalValue *BaseGV = AddrMode.BaseGV;
    if (BaseGV != nullptr) {
      if (ResultPtr)
        return Modified;

      if (BaseGV->isThreadLocal()) {
        ResultPtr = Builder.CreateThreadLocalAddress(BaseGV);
      } else {
        ResultPtr = BaseGV;
      }
    }

    // If the real base value actually came from an inttoptr, then the matcher
    // will look through it and provide only the integer value. In that case,
    // use it here.
    if (!DL->isNonIntegralPointerType(Addr->getType())) {
      if (!ResultPtr && AddrMode.BaseReg) {
        ResultPtr = Builder.CreateIntToPtr(AddrMode.BaseReg, Addr->getType(),
                                           "sunkaddr");
        AddrMode.BaseReg = nullptr;
      } else if (!ResultPtr && AddrMode.Scale == 1) {
        ResultPtr = Builder.CreateIntToPtr(AddrMode.ScaledReg, Addr->getType(),
                                           "sunkaddr");
        AddrMode.Scale = 0;
      }
    }

    if (!ResultPtr && !AddrMode.BaseReg && !AddrMode.Scale &&
        !AddrMode.BaseOffs) {
      SunkAddr = Constant::getNullValue(Addr->getType());
    } else if (!ResultPtr) {
      return Modified;
    } else {
      Type *I8PtrTy =
          Builder.getPtrTy(Addr->getType()->getPointerAddressSpace());

      // Start with the base register. Do this first so that subsequent address
      // matching finds it last, which will prevent it from trying to match it
      // as the scaled value in case it happens to be a mul. That would be
      // problematic if we've sunk a different mul for the scale, because then
      // we'd end up sinking both muls.
      if (AddrMode.BaseReg) {
        Value *V = AddrMode.BaseReg;
        if (V->getType() != IntPtrTy)
          V = Builder.CreateIntCast(V, IntPtrTy, /*isSigned=*/true, "sunkaddr");

        ResultIndex = V;
      }

      // Add the scale value.
      if (AddrMode.Scale) {
        Value *V = AddrMode.ScaledReg;
        if (V->getType() == IntPtrTy) {
          // done.
        } else {
          assert(cast<IntegerType>(IntPtrTy)->getBitWidth() <
                     cast<IntegerType>(V->getType())->getBitWidth() &&
                 "We can't transform if ScaledReg is too narrow");
          V = Builder.CreateTrunc(V, IntPtrTy, "sunkaddr");
        }

        if (AddrMode.Scale != 1)
          V = Builder.CreateMul(V, ConstantInt::get(IntPtrTy, AddrMode.Scale),
                                "sunkaddr");
        if (ResultIndex)
          ResultIndex = Builder.CreateAdd(ResultIndex, V, "sunkaddr");
        else
          ResultIndex = V;
      }

      // Add in the Base Offset if present.
      if (AddrMode.BaseOffs) {
        Value *V = ConstantInt::get(IntPtrTy, AddrMode.BaseOffs);
        if (ResultIndex) {
          // We need to add this separately from the scale above to help with
          // SDAG consecutive load/store merging.
          if (ResultPtr->getType() != I8PtrTy)
            ResultPtr = Builder.CreatePointerCast(ResultPtr, I8PtrTy);
          ResultPtr = Builder.CreatePtrAdd(ResultPtr, ResultIndex, "sunkaddr",
                                           AddrMode.InBounds);
        }

        ResultIndex = V;
      }

      if (!ResultIndex) {
        SunkAddr = ResultPtr;
      } else {
        if (ResultPtr->getType() != I8PtrTy)
          ResultPtr = Builder.CreatePointerCast(ResultPtr, I8PtrTy);
        SunkAddr = Builder.CreatePtrAdd(ResultPtr, ResultIndex, "sunkaddr",
                                        AddrMode.InBounds);
      }

      if (SunkAddr->getType() != Addr->getType()) {
        if (SunkAddr->getType()->getPointerAddressSpace() !=
                Addr->getType()->getPointerAddressSpace() &&
            !DL->isNonIntegralPointerType(Addr->getType())) {
          // There are two reasons the address spaces might not match: a no-op
          // addrspacecast, or a ptrtoint/inttoptr pair. Either way, we emit a
          // ptrtoint/inttoptr pair to ensure we match the original semantics.
          // TODO: allow bitcast between different address space pointers with
          // the same size.
          SunkAddr = Builder.CreatePtrToInt(SunkAddr, IntPtrTy, "sunkaddr");
          SunkAddr =
              Builder.CreateIntToPtr(SunkAddr, Addr->getType(), "sunkaddr");
        } else
          SunkAddr = Builder.CreatePointerCast(SunkAddr, Addr->getType());
      }
    }
  } else {
    // We'd require a ptrtoint/inttoptr down the line, which we can't do for
    // non-integral pointers, so in that case bail out now.
    Type *BaseTy = AddrMode.BaseReg ? AddrMode.BaseReg->getType() : nullptr;
    Type *ScaleTy = AddrMode.Scale ? AddrMode.ScaledReg->getType() : nullptr;
    PointerType *BasePtrTy = dyn_cast_or_null<PointerType>(BaseTy);
    PointerType *ScalePtrTy = dyn_cast_or_null<PointerType>(ScaleTy);
    if (DL->isNonIntegralPointerType(Addr->getType()) ||
        (BasePtrTy && DL->isNonIntegralPointerType(BasePtrTy)) ||
        (ScalePtrTy && DL->isNonIntegralPointerType(ScalePtrTy)) ||
        (AddrMode.BaseGV &&
         DL->isNonIntegralPointerType(AddrMode.BaseGV->getType())))
      return Modified;

    LLVM_DEBUG(dbgs() << "CGP: SINKING nonlocal addrmode: " << AddrMode
                      << " for " << *MemoryInst << "\n");
    Type *IntPtrTy = DL->getIntPtrType(Addr->getType());
    Value *Result = nullptr;

    // Start with the base register. Do this first so that subsequent address
    // matching finds it last, which will prevent it from trying to match it
    // as the scaled value in case it happens to be a mul. That would be
    // problematic if we've sunk a different mul for the scale, because then
    // we'd end up sinking both muls.
    if (AddrMode.BaseReg) {
      Value *V = AddrMode.BaseReg;
      if (V->getType()->isPointerTy())
        V = Builder.CreatePtrToInt(V, IntPtrTy, "sunkaddr");
      if (V->getType() != IntPtrTy)
        V = Builder.CreateIntCast(V, IntPtrTy, /*isSigned=*/true, "sunkaddr");
      Result = V;
    }

    // Add the scale value.
    if (AddrMode.Scale) {
      Value *V = AddrMode.ScaledReg;
      if (V->getType() == IntPtrTy) {
        // done.
      } else if (V->getType()->isPointerTy()) {
        V = Builder.CreatePtrToInt(V, IntPtrTy, "sunkaddr");
      } else if (cast<IntegerType>(IntPtrTy)->getBitWidth() <
                 cast<IntegerType>(V->getType())->getBitWidth()) {
        V = Builder.CreateTrunc(V, IntPtrTy, "sunkaddr");
      } else {
        // It is only safe to sign extend the BaseReg if we know that the math
        // required to create it did not overflow before we extend it. Since
        // the original IR value was tossed in favor of a constant back when
        // the AddrMode was created we need to bail out gracefully if widths
        // do not match instead of extending it.
        Instruction *I = dyn_cast_or_null<Instruction>(Result);
        if (I && (Result != AddrMode.BaseReg))
          I->eraseFromParent();
        return Modified;
      }
      if (AddrMode.Scale != 1)
        V = Builder.CreateMul(V, ConstantInt::get(IntPtrTy, AddrMode.Scale),
                              "sunkaddr");
      if (Result)
        Result = Builder.CreateAdd(Result, V, "sunkaddr");
      else
        Result = V;
    }

    // Add in the BaseGV if present.
    GlobalValue *BaseGV = AddrMode.BaseGV;
    if (BaseGV != nullptr) {
      Value *BaseGVPtr;
      if (BaseGV->isThreadLocal()) {
        BaseGVPtr = Builder.CreateThreadLocalAddress(BaseGV);
      } else {
        BaseGVPtr = BaseGV;
      }
      Value *V = Builder.CreatePtrToInt(BaseGVPtr, IntPtrTy, "sunkaddr");
      if (Result)
        Result = Builder.CreateAdd(Result, V, "sunkaddr");
      else
        Result = V;
    }

    // Add in the Base Offset if present.
    if (AddrMode.BaseOffs) {
      Value *V = ConstantInt::get(IntPtrTy, AddrMode.BaseOffs);
      if (Result)
        Result = Builder.CreateAdd(Result, V, "sunkaddr");
      else
        Result = V;
    }

    if (!Result)
      SunkAddr = Constant::getNullValue(Addr->getType());
    else
      SunkAddr = Builder.CreateIntToPtr(Result, Addr->getType(), "sunkaddr");
  }

  MemoryInst->replaceUsesOfWith(Repl, SunkAddr);
  // Store the newly computed address into the cache. In the case we reused a
  // value, this should be idempotent.
  SunkAddrs[Addr] = WeakTrackingVH(SunkAddr);

  // If we have no uses, recursively delete the value and all dead instructions
  // using it.
  if (Repl->use_empty()) {
    resetIteratorIfInvalidatedWhileCalling(CurInstIterator->getParent(), [&]() {
      RecursivelyDeleteTriviallyDeadInstructions(
          Repl, TLInfo, nullptr,
          [&](Value *V) { removeAllAssertingVHReferences(V); });
    });
  }
  ++NumMemoryInsts;
  return true;
}

/// Rewrite GEP input to gather/scatter to enable SelectionDAGBuilder to find
/// a uniform base to use for ISD::MGATHER/MSCATTER. SelectionDAGBuilder can
/// only handle a 2 operand GEP in the same basic block or a splat constant
/// vector. The 2 operands to the GEP must have a scalar pointer and a vector
/// index.
///
/// If the existing GEP has a vector base pointer that is splat, we can look
/// through the splat to find the scalar pointer. If we can't find a scalar
/// pointer there's nothing we can do.
///
/// If we have a GEP with more than 2 indices where the middle indices are all
/// zeroes, we can replace it with 2 GEPs where the second has 2 operands.
///
/// If the final index isn't a vector or is a splat, we can emit a scalar GEP
/// followed by a GEP with an all zeroes vector index. This will enable
/// SelectionDAGBuilder to use the scalar GEP as the uniform base and have a
/// zero index.
bool CodeGenPrepare::optimizeGatherScatterInst(Instruction *MemoryInst,
                                               Value *Ptr) {
  Value *NewAddr;

  if (const auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    // Don't optimize GEPs that don't have indices.
    if (!GEP->hasIndices())
      return false;

    // If the GEP and the gather/scatter aren't in the same BB, don't optimize.
    // FIXME: We should support this by sinking the GEP.
    if (MemoryInst->getParent() != GEP->getParent())
      return false;

    SmallVector<Value *, 2> Ops(GEP->operands());

    bool RewriteGEP = false;

    if (Ops[0]->getType()->isVectorTy()) {
      Ops[0] = getSplatValue(Ops[0]);
      if (!Ops[0])
        return false;
      RewriteGEP = true;
    }

    unsigned FinalIndex = Ops.size() - 1;

    // Ensure all but the last index is 0.
    // FIXME: This isn't strictly required. All that's required is that they are
    // all scalars or splats.
    for (unsigned i = 1; i < FinalIndex; ++i) {
      auto *C = dyn_cast<Constant>(Ops[i]);
      if (!C)
        return false;
      if (isa<VectorType>(C->getType()))
        C = C->getSplatValue();
      auto *CI = dyn_cast_or_null<ConstantInt>(C);
      if (!CI || !CI->isZero())
        return false;
      // Scalarize the index if needed.
      Ops[i] = CI;
    }

    // Try to scalarize the final index.
    if (Ops[FinalIndex]->getType()->isVectorTy()) {
      if (Value *V = getSplatValue(Ops[FinalIndex])) {
        auto *C = dyn_cast<ConstantInt>(V);
        // Don't scalarize all zeros vector.
        if (!C || !C->isZero()) {
          Ops[FinalIndex] = V;
          RewriteGEP = true;
        }
      }
    }

    // If we made any changes or the we have extra operands, we need to generate
    // new instructions.
    if (!RewriteGEP && Ops.size() == 2)
      return false;

    auto NumElts = cast<VectorType>(Ptr->getType())->getElementCount();

    IRBuilder<> Builder(MemoryInst);

    Type *SourceTy = GEP->getSourceElementType();
    Type *ScalarIndexTy = DL->getIndexType(Ops[0]->getType()->getScalarType());

    // If the final index isn't a vector, emit a scalar GEP containing all ops
    // and a vector GEP with all zeroes final index.
    if (!Ops[FinalIndex]->getType()->isVectorTy()) {
      NewAddr = Builder.CreateGEP(SourceTy, Ops[0], ArrayRef(Ops).drop_front());
      auto *IndexTy = VectorType::get(ScalarIndexTy, NumElts);
      auto *SecondTy = GetElementPtrInst::getIndexedType(
          SourceTy, ArrayRef(Ops).drop_front());
      NewAddr =
          Builder.CreateGEP(SecondTy, NewAddr, Constant::getNullValue(IndexTy));
    } else {
      Value *Base = Ops[0];
      Value *Index = Ops[FinalIndex];

      // Create a scalar GEP if there are more than 2 operands.
      if (Ops.size() != 2) {
        // Replace the last index with 0.
        Ops[FinalIndex] =
            Constant::getNullValue(Ops[FinalIndex]->getType()->getScalarType());
        Base = Builder.CreateGEP(SourceTy, Base, ArrayRef(Ops).drop_front());
        SourceTy = GetElementPtrInst::getIndexedType(
            SourceTy, ArrayRef(Ops).drop_front());
      }

      // Now create the GEP with scalar pointer and vector index.
      NewAddr = Builder.CreateGEP(SourceTy, Base, Index);
    }
  } else if (!isa<Constant>(Ptr)) {
    // Not a GEP, maybe its a splat and we can create a GEP to enable
    // SelectionDAGBuilder to use it as a uniform base.
    Value *V = getSplatValue(Ptr);
    if (!V)
      return false;

    auto NumElts = cast<VectorType>(Ptr->getType())->getElementCount();

    IRBuilder<> Builder(MemoryInst);

    // Emit a vector GEP with a scalar pointer and all 0s vector index.
    Type *ScalarIndexTy = DL->getIndexType(V->getType()->getScalarType());
    auto *IndexTy = VectorType::get(ScalarIndexTy, NumElts);
    Type *ScalarTy;
    if (cast<IntrinsicInst>(MemoryInst)->getIntrinsicID() ==
        Intrinsic::masked_gather) {
      ScalarTy = MemoryInst->getType()->getScalarType();
    } else {
      assert(cast<IntrinsicInst>(MemoryInst)->getIntrinsicID() ==
             Intrinsic::masked_scatter);
      ScalarTy = MemoryInst->getOperand(0)->getType()->getScalarType();
    }
    NewAddr = Builder.CreateGEP(ScalarTy, V, Constant::getNullValue(IndexTy));
  } else {
    // Constant, SelectionDAGBuilder knows to check if its a splat.
    return false;
  }

  MemoryInst->replaceUsesOfWith(Ptr, NewAddr);

  // If we have no uses, recursively delete the value and all dead instructions
  // using it.
  if (Ptr->use_empty())
    RecursivelyDeleteTriviallyDeadInstructions(
        Ptr, TLInfo, nullptr,
        [&](Value *V) { removeAllAssertingVHReferences(V); });

  return true;
}

/// If there are any memory operands, use OptimizeMemoryInst to sink their
/// address computing into the block when possible / profitable.
bool CodeGenPrepare::optimizeInlineAsmInst(CallInst *CS) {
  bool MadeChange = false;

  const TargetRegisterInfo *TRI =
      TM->getSubtargetImpl(*CS->getFunction())->getRegisterInfo();
  TargetLowering::AsmOperandInfoVector TargetConstraints =
      TLI->ParseConstraints(*DL, TRI, *CS);
  unsigned ArgNo = 0;
  for (TargetLowering::AsmOperandInfo &OpInfo : TargetConstraints) {
    // Compute the constraint code and ConstraintType to use.
    TLI->ComputeConstraintToUse(OpInfo, SDValue());

    // TODO: Also handle C_Address?
    if (OpInfo.ConstraintType == TargetLowering::C_Memory &&
        OpInfo.isIndirect) {
      Value *OpVal = CS->getArgOperand(ArgNo++);
      MadeChange |= optimizeMemoryInst(CS, OpVal, OpVal->getType(), ~0u);
    } else if (OpInfo.Type == InlineAsm::isInput)
      ArgNo++;
  }

  return MadeChange;
}

/// Check if all the uses of \p Val are equivalent (or free) zero or
/// sign extensions.
static bool hasSameExtUse(Value *Val, const TargetLowering &TLI) {
  assert(!Val->use_empty() && "Input must have at least one use");
  const Instruction *FirstUser = cast<Instruction>(*Val->user_begin());
  bool IsSExt = isa<SExtInst>(FirstUser);
  Type *ExtTy = FirstUser->getType();
  for (const User *U : Val->users()) {
    const Instruction *UI = cast<Instruction>(U);
    if ((IsSExt && !isa<SExtInst>(UI)) || (!IsSExt && !isa<ZExtInst>(UI)))
      return false;
    Type *CurTy = UI->getType();
    // Same input and output types: Same instruction after CSE.
    if (CurTy == ExtTy)
      continue;

    // If IsSExt is true, we are in this situation:
    // a = Val
    // b = sext ty1 a to ty2
    // c = sext ty1 a to ty3
    // Assuming ty2 is shorter than ty3, this could be turned into:
    // a = Val
    // b = sext ty1 a to ty2
    // c = sext ty2 b to ty3
    // However, the last sext is not free.
    if (IsSExt)
      return false;

    // This is a ZExt, maybe this is free to extend from one type to another.
    // In that case, we would not account for a different use.
    Type *NarrowTy;
    Type *LargeTy;
    if (ExtTy->getScalarType()->getIntegerBitWidth() >
        CurTy->getScalarType()->getIntegerBitWidth()) {
      NarrowTy = CurTy;
      LargeTy = ExtTy;
    } else {
      NarrowTy = ExtTy;
      LargeTy = CurTy;
    }

    if (!TLI.isZExtFree(NarrowTy, LargeTy))
      return false;
  }
  // All uses are the same or can be derived from one another for free.
  return true;
}

/// Try to speculatively promote extensions in \p Exts and continue
/// promoting through newly promoted operands recursively as far as doing so is
/// profitable. Save extensions profitably moved up, in \p ProfitablyMovedExts.
/// When some promotion happened, \p TPT contains the proper state to revert
/// them.
///
/// \return true if some promotion happened, false otherwise.
bool CodeGenPrepare::tryToPromoteExts(
    TypePromotionTransaction &TPT, const SmallVectorImpl<Instruction *> &Exts,
    SmallVectorImpl<Instruction *> &ProfitablyMovedExts,
    unsigned CreatedInstsCost) {
  bool Promoted = false;

  // Iterate over all the extensions to try to promote them.
  for (auto *I : Exts) {
    // Early check if we directly have ext(load).
    if (isa<LoadInst>(I->getOperand(0))) {
      ProfitablyMovedExts.push_back(I);
      continue;
    }

    // Check whether or not we want to do any promotion.  The reason we have
    // this check inside the for loop is to catch the case where an extension
    // is directly fed by a load because in such case the extension can be moved
    // up without any promotion on its operands.
    if (!TLI->enableExtLdPromotion() || DisableExtLdPromotion)
      return false;

    // Get the action to perform the promotion.
    TypePromotionHelper::Action TPH =
        TypePromotionHelper::getAction(I, InsertedInsts, *TLI, PromotedInsts);
    // Check if we can promote.
    if (!TPH) {
      // Save the current extension as we cannot move up through its operand.
      ProfitablyMovedExts.push_back(I);
      continue;
    }

    // Save the current state.
    TypePromotionTransaction::ConstRestorationPt LastKnownGood =
        TPT.getRestorationPoint();
    SmallVector<Instruction *, 4> NewExts;
    unsigned NewCreatedInstsCost = 0;
    unsigned ExtCost = !TLI->isExtFree(I);
    // Promote.
    Value *PromotedVal = TPH(I, TPT, PromotedInsts, NewCreatedInstsCost,
                             &NewExts, nullptr, *TLI);
    assert(PromotedVal &&
           "TypePromotionHelper should have filtered out those cases");

    // We would be able to merge only one extension in a load.
    // Therefore, if we have more than 1 new extension we heuristically
    // cut this search path, because it means we degrade the code quality.
    // With exactly 2, the transformation is neutral, because we will merge
    // one extension but leave one. However, we optimistically keep going,
    // because the new extension may be removed too. Also avoid replacing a
    // single free extension with multiple extensions, as this increases the
    // number of IR instructions while not providing any savings.
    long long TotalCreatedInstsCost = CreatedInstsCost + NewCreatedInstsCost;
    // FIXME: It would be possible to propagate a negative value instead of
    // conservatively ceiling it to 0.
    TotalCreatedInstsCost =
        std::max((long long)0, (TotalCreatedInstsCost - ExtCost));
    if (!StressExtLdPromotion &&
        (TotalCreatedInstsCost > 1 ||
         !isPromotedInstructionLegal(*TLI, *DL, PromotedVal) ||
         (ExtCost == 0 && NewExts.size() > 1))) {
      // This promotion is not profitable, rollback to the previous state, and
      // save the current extension in ProfitablyMovedExts as the latest
      // speculative promotion turned out to be unprofitable.
      TPT.rollback(LastKnownGood);
      ProfitablyMovedExts.push_back(I);
      continue;
    }
    // Continue promoting NewExts as far as doing so is profitable.
    SmallVector<Instruction *, 2> NewlyMovedExts;
    (void)tryToPromoteExts(TPT, NewExts, NewlyMovedExts, TotalCreatedInstsCost);
    bool NewPromoted = false;
    for (auto *ExtInst : NewlyMovedExts) {
      Instruction *MovedExt = cast<Instruction>(ExtInst);
      Value *ExtOperand = MovedExt->getOperand(0);
      // If we have reached to a load, we need this extra profitability check
      // as it could potentially be merged into an ext(load).
      if (isa<LoadInst>(ExtOperand) &&
          !(StressExtLdPromotion || NewCreatedInstsCost <= ExtCost ||
            (ExtOperand->hasOneUse() || hasSameExtUse(ExtOperand, *TLI))))
        continue;

      ProfitablyMovedExts.push_back(MovedExt);
      NewPromoted = true;
    }

    // If none of speculative promotions for NewExts is profitable, rollback
    // and save the current extension (I) as the last profitable extension.
    if (!NewPromoted) {
      TPT.rollback(LastKnownGood);
      ProfitablyMovedExts.push_back(I);
      continue;
    }
    // The promotion is profitable.
    Promoted = true;
  }
  return Promoted;
}

/// Merging redundant sexts when one is dominating the other.
bool CodeGenPrepare::mergeSExts(Function &F) {
  bool Changed = false;
  for (auto &Entry : ValToSExtendedUses) {
    SExts &Insts = Entry.second;
    SExts CurPts;
    for (Instruction *Inst : Insts) {
      if (RemovedInsts.count(Inst) || !isa<SExtInst>(Inst) ||
          Inst->getOperand(0) != Entry.first)
        continue;
      bool inserted = false;
      for (auto &Pt : CurPts) {
        if (getDT(F).dominates(Inst, Pt)) {
          replaceAllUsesWith(Pt, Inst, FreshBBs, IsHugeFunc);
          RemovedInsts.insert(Pt);
          Pt->removeFromParent();
          Pt = Inst;
          inserted = true;
          Changed = true;
          break;
        }
        if (!getDT(F).dominates(Pt, Inst))
          // Give up if we need to merge in a common dominator as the
          // experiments show it is not profitable.
          continue;
        replaceAllUsesWith(Inst, Pt, FreshBBs, IsHugeFunc);
        RemovedInsts.insert(Inst);
        Inst->removeFromParent();
        inserted = true;
        Changed = true;
        break;
      }
      if (!inserted)
        CurPts.push_back(Inst);
    }
  }
  return Changed;
}

// Splitting large data structures so that the GEPs accessing them can have
// smaller offsets so that they can be sunk to the same blocks as their users.
// For example, a large struct starting from %base is split into two parts
// where the second part starts from %new_base.
//
// Before:
// BB0:
//   %base     =
//
// BB1:
//   %gep0     = gep %base, off0
//   %gep1     = gep %base, off1
//   %gep2     = gep %base, off2
//
// BB2:
//   %load1    = load %gep0
//   %load2    = load %gep1
//   %load3    = load %gep2
//
// After:
// BB0:
//   %base     =
//   %new_base = gep %base, off0
//
// BB1:
//   %new_gep0 = %new_base
//   %new_gep1 = gep %new_base, off1 - off0
//   %new_gep2 = gep %new_base, off2 - off0
//
// BB2:
//   %load1    = load i32, i32* %new_gep0
//   %load2    = load i32, i32* %new_gep1
//   %load3    = load i32, i32* %new_gep2
//
// %new_gep1 and %new_gep2 can be sunk to BB2 now after the splitting because
// their offsets are smaller enough to fit into the addressing mode.
bool CodeGenPrepare::splitLargeGEPOffsets() {
  bool Changed = false;
  for (auto &Entry : LargeOffsetGEPMap) {
    Value *OldBase = Entry.first;
    SmallVectorImpl<std::pair<AssertingVH<GetElementPtrInst>, int64_t>>
        &LargeOffsetGEPs = Entry.second;
    auto compareGEPOffset =
        [&](const std::pair<GetElementPtrInst *, int64_t> &LHS,
            const std::pair<GetElementPtrInst *, int64_t> &RHS) {
          if (LHS.first == RHS.first)
            return false;
          if (LHS.second != RHS.second)
            return LHS.second < RHS.second;
          return LargeOffsetGEPID[LHS.first] < LargeOffsetGEPID[RHS.first];
        };
    // Sorting all the GEPs of the same data structures based on the offsets.
    llvm::sort(LargeOffsetGEPs, compareGEPOffset);
    LargeOffsetGEPs.erase(llvm::unique(LargeOffsetGEPs), LargeOffsetGEPs.end());
    // Skip if all the GEPs have the same offsets.
    if (LargeOffsetGEPs.front().second == LargeOffsetGEPs.back().second)
      continue;
    GetElementPtrInst *BaseGEP = LargeOffsetGEPs.begin()->first;
    int64_t BaseOffset = LargeOffsetGEPs.begin()->second;
    Value *NewBaseGEP = nullptr;

    auto createNewBase = [&](int64_t BaseOffset, Value *OldBase,
                             GetElementPtrInst *GEP) {
      LLVMContext &Ctx = GEP->getContext();
      Type *PtrIdxTy = DL->getIndexType(GEP->getType());
      Type *I8PtrTy =
          PointerType::get(Ctx, GEP->getType()->getPointerAddressSpace());

      BasicBlock::iterator NewBaseInsertPt;
      BasicBlock *NewBaseInsertBB;
      if (auto *BaseI = dyn_cast<Instruction>(OldBase)) {
        // If the base of the struct is an instruction, the new base will be
        // inserted close to it.
        NewBaseInsertBB = BaseI->getParent();
        if (isa<PHINode>(BaseI))
          NewBaseInsertPt = NewBaseInsertBB->getFirstInsertionPt();
        else if (InvokeInst *Invoke = dyn_cast<InvokeInst>(BaseI)) {
          NewBaseInsertBB =
              SplitEdge(NewBaseInsertBB, Invoke->getNormalDest(), DT.get(), LI);
          NewBaseInsertPt = NewBaseInsertBB->getFirstInsertionPt();
        } else
          NewBaseInsertPt = std::next(BaseI->getIterator());
      } else {
        // If the current base is an argument or global value, the new base
        // will be inserted to the entry block.
        NewBaseInsertBB = &BaseGEP->getFunction()->getEntryBlock();
        NewBaseInsertPt = NewBaseInsertBB->getFirstInsertionPt();
      }
      IRBuilder<> NewBaseBuilder(NewBaseInsertBB, NewBaseInsertPt);
      // Create a new base.
      Value *BaseIndex = ConstantInt::get(PtrIdxTy, BaseOffset);
      NewBaseGEP = OldBase;
      if (NewBaseGEP->getType() != I8PtrTy)
        NewBaseGEP = NewBaseBuilder.CreatePointerCast(NewBaseGEP, I8PtrTy);
      NewBaseGEP =
          NewBaseBuilder.CreatePtrAdd(NewBaseGEP, BaseIndex, "splitgep");
      NewGEPBases.insert(NewBaseGEP);
      return;
    };

    // Check whether all the offsets can be encoded with prefered common base.
    if (int64_t PreferBase = TLI->getPreferredLargeGEPBaseOffset(
            LargeOffsetGEPs.front().second, LargeOffsetGEPs.back().second)) {
      BaseOffset = PreferBase;
      // Create a new base if the offset of the BaseGEP can be decoded with one
      // instruction.
      createNewBase(BaseOffset, OldBase, BaseGEP);
    }

    auto *LargeOffsetGEP = LargeOffsetGEPs.begin();
    while (LargeOffsetGEP != LargeOffsetGEPs.end()) {
      GetElementPtrInst *GEP = LargeOffsetGEP->first;
      int64_t Offset = LargeOffsetGEP->second;
      if (Offset != BaseOffset) {
        TargetLowering::AddrMode AddrMode;
        AddrMode.HasBaseReg = true;
        AddrMode.BaseOffs = Offset - BaseOffset;
        // The result type of the GEP might not be the type of the memory
        // access.
        if (!TLI->isLegalAddressingMode(*DL, AddrMode,
                                        GEP->getResultElementType(),
                                        GEP->getAddressSpace())) {
          // We need to create a new base if the offset to the current base is
          // too large to fit into the addressing mode. So, a very large struct
          // may be split into several parts.
          BaseGEP = GEP;
          BaseOffset = Offset;
          NewBaseGEP = nullptr;
        }
      }

      // Generate a new GEP to replace the current one.
      Type *PtrIdxTy = DL->getIndexType(GEP->getType());

      if (!NewBaseGEP) {
        // Create a new base if we don't have one yet.  Find the insertion
        // pointer for the new base first.
        createNewBase(BaseOffset, OldBase, GEP);
      }

      IRBuilder<> Builder(GEP);
      Value *NewGEP = NewBaseGEP;
      if (Offset != BaseOffset) {
        // Calculate the new offset for the new GEP.
        Value *Index = ConstantInt::get(PtrIdxTy, Offset - BaseOffset);
        NewGEP = Builder.CreatePtrAdd(NewBaseGEP, Index);
      }
      replaceAllUsesWith(GEP, NewGEP, FreshBBs, IsHugeFunc);
      LargeOffsetGEPID.erase(GEP);
      LargeOffsetGEP = LargeOffsetGEPs.erase(LargeOffsetGEP);
      GEP->eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

bool CodeGenPrepare::optimizePhiType(
    PHINode *I, SmallPtrSetImpl<PHINode *> &Visited,
    SmallPtrSetImpl<Instruction *> &DeletedInstrs) {
  // We are looking for a collection on interconnected phi nodes that together
  // only use loads/bitcasts and are used by stores/bitcasts, and the bitcasts
  // are of the same type. Convert the whole set of nodes to the type of the
  // bitcast.
  Type *PhiTy = I->getType();
  Type *ConvertTy = nullptr;
  if (Visited.count(I) ||
      (!I->getType()->isIntegerTy() && !I->getType()->isFloatingPointTy()))
    return false;

  SmallVector<Instruction *, 4> Worklist;
  Worklist.push_back(cast<Instruction>(I));
  SmallPtrSet<PHINode *, 4> PhiNodes;
  SmallPtrSet<ConstantData *, 4> Constants;
  PhiNodes.insert(I);
  Visited.insert(I);
  SmallPtrSet<Instruction *, 4> Defs;
  SmallPtrSet<Instruction *, 4> Uses;
  // This works by adding extra bitcasts between load/stores and removing
  // existing bicasts. If we have a phi(bitcast(load)) or a store(bitcast(phi))
  // we can get in the situation where we remove a bitcast in one iteration
  // just to add it again in the next. We need to ensure that at least one
  // bitcast we remove are anchored to something that will not change back.
  bool AnyAnchored = false;

  while (!Worklist.empty()) {
    Instruction *II = Worklist.pop_back_val();

    if (auto *Phi = dyn_cast<PHINode>(II)) {
      // Handle Defs, which might also be PHI's
      for (Value *V : Phi->incoming_values()) {
        if (auto *OpPhi = dyn_cast<PHINode>(V)) {
          if (!PhiNodes.count(OpPhi)) {
            if (!Visited.insert(OpPhi).second)
              return false;
            PhiNodes.insert(OpPhi);
            Worklist.push_back(OpPhi);
          }
        } else if (auto *OpLoad = dyn_cast<LoadInst>(V)) {
          if (!OpLoad->isSimple())
            return false;
          if (Defs.insert(OpLoad).second)
            Worklist.push_back(OpLoad);
        } else if (auto *OpEx = dyn_cast<ExtractElementInst>(V)) {
          if (Defs.insert(OpEx).second)
            Worklist.push_back(OpEx);
        } else if (auto *OpBC = dyn_cast<BitCastInst>(V)) {
          if (!ConvertTy)
            ConvertTy = OpBC->getOperand(0)->getType();
          if (OpBC->getOperand(0)->getType() != ConvertTy)
            return false;
          if (Defs.insert(OpBC).second) {
            Worklist.push_back(OpBC);
            AnyAnchored |= !isa<LoadInst>(OpBC->getOperand(0)) &&
                           !isa<ExtractElementInst>(OpBC->getOperand(0));
          }
        } else if (auto *OpC = dyn_cast<ConstantData>(V))
          Constants.insert(OpC);
        else
          return false;
      }
    }

    // Handle uses which might also be phi's
    for (User *V : II->users()) {
      if (auto *OpPhi = dyn_cast<PHINode>(V)) {
        if (!PhiNodes.count(OpPhi)) {
          if (Visited.count(OpPhi))
            return false;
          PhiNodes.insert(OpPhi);
          Visited.insert(OpPhi);
          Worklist.push_back(OpPhi);
        }
      } else if (auto *OpStore = dyn_cast<StoreInst>(V)) {
        if (!OpStore->isSimple() || OpStore->getOperand(0) != II)
          return false;
        Uses.insert(OpStore);
      } else if (auto *OpBC = dyn_cast<BitCastInst>(V)) {
        if (!ConvertTy)
          ConvertTy = OpBC->getType();
        if (OpBC->getType() != ConvertTy)
          return false;
        Uses.insert(OpBC);
        AnyAnchored |=
            any_of(OpBC->users(), [](User *U) { return !isa<StoreInst>(U); });
      } else {
        return false;
      }
    }
  }

  if (!ConvertTy || !AnyAnchored ||
      !TLI->shouldConvertPhiType(PhiTy, ConvertTy))
    return false;

  LLVM_DEBUG(dbgs() << "Converting " << *I << "\n  and connected nodes to "
                    << *ConvertTy << "\n");

  // Create all the new phi nodes of the new type, and bitcast any loads to the
  // correct type.
  ValueToValueMap ValMap;
  for (ConstantData *C : Constants)
    ValMap[C] = ConstantExpr::getBitCast(C, ConvertTy);
  for (Instruction *D : Defs) {
    if (isa<BitCastInst>(D)) {
      ValMap[D] = D->getOperand(0);
      DeletedInstrs.insert(D);
    } else {
      BasicBlock::iterator insertPt = std::next(D->getIterator());
      ValMap[D] = new BitCastInst(D, ConvertTy, D->getName() + ".bc", insertPt);
    }
  }
  for (PHINode *Phi : PhiNodes)
    ValMap[Phi] = PHINode::Create(ConvertTy, Phi->getNumIncomingValues(),
                                  Phi->getName() + ".tc", Phi->getIterator());
  // Pipe together all the PhiNodes.
  for (PHINode *Phi : PhiNodes) {
    PHINode *NewPhi = cast<PHINode>(ValMap[Phi]);
    for (int i = 0, e = Phi->getNumIncomingValues(); i < e; i++)
      NewPhi->addIncoming(ValMap[Phi->getIncomingValue(i)],
                          Phi->getIncomingBlock(i));
    Visited.insert(NewPhi);
  }
  // And finally pipe up the stores and bitcasts
  for (Instruction *U : Uses) {
    if (isa<BitCastInst>(U)) {
      DeletedInstrs.insert(U);
      replaceAllUsesWith(U, ValMap[U->getOperand(0)], FreshBBs, IsHugeFunc);
    } else {
      U->setOperand(0, new BitCastInst(ValMap[U->getOperand(0)], PhiTy, "bc",
                                       U->getIterator()));
    }
  }

  // Save the removed phis to be deleted later.
  for (PHINode *Phi : PhiNodes)
    DeletedInstrs.insert(Phi);
  return true;
}

bool CodeGenPrepare::optimizePhiTypes(Function &F) {
  if (!OptimizePhiTypes)
    return false;

  bool Changed = false;
  SmallPtrSet<PHINode *, 4> Visited;
  SmallPtrSet<Instruction *, 4> DeletedInstrs;

  // Attempt to optimize all the phis in the functions to the correct type.
  for (auto &BB : F)
    for (auto &Phi : BB.phis())
      Changed |= optimizePhiType(&Phi, Visited, DeletedInstrs);

  // Remove any old phi's that have been converted.
  for (auto *I : DeletedInstrs) {
    replaceAllUsesWith(I, PoisonValue::get(I->getType()), FreshBBs, IsHugeFunc);
    I->eraseFromParent();
  }

  return Changed;
}

/// Return true, if an ext(load) can be formed from an extension in
/// \p MovedExts.
bool CodeGenPrepare::canFormExtLd(
    const SmallVectorImpl<Instruction *> &MovedExts, LoadInst *&LI,
    Instruction *&Inst, bool HasPromoted) {
  for (auto *MovedExtInst : MovedExts) {
    if (isa<LoadInst>(MovedExtInst->getOperand(0))) {
      LI = cast<LoadInst>(MovedExtInst->getOperand(0));
      Inst = MovedExtInst;
      break;
    }
  }
  if (!LI)
    return false;

  // If they're already in the same block, there's nothing to do.
  // Make the cheap checks first if we did not promote.
  // If we promoted, we need to check if it is indeed profitable.
  if (!HasPromoted && LI->getParent() == Inst->getParent())
    return false;

  return TLI->isExtLoad(LI, Inst, *DL);
}

/// Move a zext or sext fed by a load into the same basic block as the load,
/// unless conditions are unfavorable. This allows SelectionDAG to fold the
/// extend into the load.
///
/// E.g.,
/// \code
/// %ld = load i32* %addr
/// %add = add nuw i32 %ld, 4
/// %zext = zext i32 %add to i64
// \endcode
/// =>
/// \code
/// %ld = load i32* %addr
/// %zext = zext i32 %ld to i64
/// %add = add nuw i64 %zext, 4
/// \encode
/// Note that the promotion in %add to i64 is done in tryToPromoteExts(), which
/// allow us to match zext(load i32*) to i64.
///
/// Also, try to promote the computations used to obtain a sign extended
/// value used into memory accesses.
/// E.g.,
/// \code
/// a = add nsw i32 b, 3
/// d = sext i32 a to i64
/// e = getelementptr ..., i64 d
/// \endcode
/// =>
/// \code
/// f = sext i32 b to i64
/// a = add nsw i64 f, 3
/// e = getelementptr ..., i64 a
/// \endcode
///
/// \p Inst[in/out] the extension may be modified during the process if some
/// promotions apply.
bool CodeGenPrepare::optimizeExt(Instruction *&Inst) {
  bool AllowPromotionWithoutCommonHeader = false;
  /// See if it is an interesting sext operations for the address type
  /// promotion before trying to promote it, e.g., the ones with the right
  /// type and used in memory accesses.
  bool ATPConsiderable = TTI->shouldConsiderAddressTypePromotion(
      *Inst, AllowPromotionWithoutCommonHeader);
  TypePromotionTransaction TPT(RemovedInsts);
  TypePromotionTransaction::ConstRestorationPt LastKnownGood =
      TPT.getRestorationPoint();
  SmallVector<Instruction *, 1> Exts;
  SmallVector<Instruction *, 2> SpeculativelyMovedExts;
  Exts.push_back(Inst);

  bool HasPromoted = tryToPromoteExts(TPT, Exts, SpeculativelyMovedExts);

  // Look for a load being extended.
  LoadInst *LI = nullptr;
  Instruction *ExtFedByLoad;

  // Try to promote a chain of computation if it allows to form an extended
  // load.
  if (canFormExtLd(SpeculativelyMovedExts, LI, ExtFedByLoad, HasPromoted)) {
    assert(LI && ExtFedByLoad && "Expect a valid load and extension");
    TPT.commit();
    // Move the extend into the same block as the load.
    ExtFedByLoad->moveAfter(LI);
    ++NumExtsMoved;
    Inst = ExtFedByLoad;
    return true;
  }

  // Continue promoting SExts if known as considerable depending on targets.
  if (ATPConsiderable &&
      performAddressTypePromotion(Inst, AllowPromotionWithoutCommonHeader,
                                  HasPromoted, TPT, SpeculativelyMovedExts))
    return true;

  TPT.rollback(LastKnownGood);
  return false;
}

// Perform address type promotion if doing so is profitable.
// If AllowPromotionWithoutCommonHeader == false, we should find other sext
// instructions that sign extended the same initial value. However, if
// AllowPromotionWithoutCommonHeader == true, we expect promoting the
// extension is just profitable.
bool CodeGenPrepare::performAddressTypePromotion(
    Instruction *&Inst, bool AllowPromotionWithoutCommonHeader,
    bool HasPromoted, TypePromotionTransaction &TPT,
    SmallVectorImpl<Instruction *> &SpeculativelyMovedExts) {
  bool Promoted = false;
  SmallPtrSet<Instruction *, 1> UnhandledExts;
  bool AllSeenFirst = true;
  for (auto *I : SpeculativelyMovedExts) {
    Value *HeadOfChain = I->getOperand(0);
    DenseMap<Value *, Instruction *>::iterator AlreadySeen =
        SeenChainsForSExt.find(HeadOfChain);
    // If there is an unhandled SExt which has the same header, try to promote
    // it as well.
    if (AlreadySeen != SeenChainsForSExt.end()) {
      if (AlreadySeen->second != nullptr)
        UnhandledExts.insert(AlreadySeen->second);
      AllSeenFirst = false;
    }
  }

  if (!AllSeenFirst || (AllowPromotionWithoutCommonHeader &&
                        SpeculativelyMovedExts.size() == 1)) {
    TPT.commit();
    if (HasPromoted)
      Promoted = true;
    for (auto *I : SpeculativelyMovedExts) {
      Value *HeadOfChain = I->getOperand(0);
      SeenChainsForSExt[HeadOfChain] = nullptr;
      ValToSExtendedUses[HeadOfChain].push_back(I);
    }
    // Update Inst as promotion happen.
    Inst = SpeculativelyMovedExts.pop_back_val();
  } else {
    // This is the first chain visited from the header, keep the current chain
    // as unhandled. Defer to promote this until we encounter another SExt
    // chain derived from the same header.
    for (auto *I : SpeculativelyMovedExts) {
      Value *HeadOfChain = I->getOperand(0);
      SeenChainsForSExt[HeadOfChain] = Inst;
    }
    return false;
  }

  if (!AllSeenFirst && !UnhandledExts.empty())
    for (auto *VisitedSExt : UnhandledExts) {
      if (RemovedInsts.count(VisitedSExt))
        continue;
      TypePromotionTransaction TPT(RemovedInsts);
      SmallVector<Instruction *, 1> Exts;
      SmallVector<Instruction *, 2> Chains;
      Exts.push_back(VisitedSExt);
      bool HasPromoted = tryToPromoteExts(TPT, Exts, Chains);
      TPT.commit();
      if (HasPromoted)
        Promoted = true;
      for (auto *I : Chains) {
        Value *HeadOfChain = I->getOperand(0);
        // Mark this as handled.
        SeenChainsForSExt[HeadOfChain] = nullptr;
        ValToSExtendedUses[HeadOfChain].push_back(I);
      }
    }
  return Promoted;
}

bool CodeGenPrepare::optimizeExtUses(Instruction *I) {
  BasicBlock *DefBB = I->getParent();

  // If the result of a {s|z}ext and its source are both live out, rewrite all
  // other uses of the source with result of extension.
  Value *Src = I->getOperand(0);
  if (Src->hasOneUse())
    return false;

  // Only do this xform if truncating is free.
  if (!TLI->isTruncateFree(I->getType(), Src->getType()))
    return false;

  // Only safe to perform the optimization if the source is also defined in
  // this block.
  if (!isa<Instruction>(Src) || DefBB != cast<Instruction>(Src)->getParent())
    return false;

  bool DefIsLiveOut = false;
  for (User *U : I->users()) {
    Instruction *UI = cast<Instruction>(U);

    // Figure out which BB this ext is used in.
    BasicBlock *UserBB = UI->getParent();
    if (UserBB == DefBB)
      continue;
    DefIsLiveOut = true;
    break;
  }
  if (!DefIsLiveOut)
    return false;

  // Make sure none of the uses are PHI nodes.
  for (User *U : Src->users()) {
    Instruction *UI = cast<Instruction>(U);
    BasicBlock *UserBB = UI->getParent();
    if (UserBB == DefBB)
      continue;
    // Be conservative. We don't want this xform to end up introducing
    // reloads just before load / store instructions.
    if (isa<PHINode>(UI) || isa<LoadInst>(UI) || isa<StoreInst>(UI))
      return false;
  }

  // InsertedTruncs - Only insert one trunc in each block once.
  DenseMap<BasicBlock *, Instruction *> InsertedTruncs;

  bool MadeChange = false;
  for (Use &U : Src->uses()) {
    Instruction *User = cast<Instruction>(U.getUser());

    // Figure out which BB this ext is used in.
    BasicBlock *UserBB = User->getParent();
    if (UserBB == DefBB)
      continue;

    // Both src and def are live in this block. Rewrite the use.
    Instruction *&InsertedTrunc = InsertedTruncs[UserBB];

    if (!InsertedTrunc) {
      BasicBlock::iterator InsertPt = UserBB->getFirstInsertionPt();
      assert(InsertPt != UserBB->end());
      InsertedTrunc = new TruncInst(I, Src->getType(), "");
      InsertedTrunc->insertBefore(*UserBB, InsertPt);
      InsertedInsts.insert(InsertedTrunc);
    }

    // Replace a use of the {s|z}ext source with a use of the result.
    U = InsertedTrunc;
    ++NumExtUses;
    MadeChange = true;
  }

  return MadeChange;
}

// Find loads whose uses only use some of the loaded value's bits.  Add an "and"
// just after the load if the target can fold this into one extload instruction,
// with the hope of eliminating some of the other later "and" instructions using
// the loaded value.  "and"s that are made trivially redundant by the insertion
// of the new "and" are removed by this function, while others (e.g. those whose
// path from the load goes through a phi) are left for isel to potentially
// remove.
//
// For example:
//
// b0:
//   x = load i32
//   ...
// b1:
//   y = and x, 0xff
//   z = use y
//
// becomes:
//
// b0:
//   x = load i32
//   x' = and x, 0xff
//   ...
// b1:
//   z = use x'
//
// whereas:
//
// b0:
//   x1 = load i32
//   ...
// b1:
//   x2 = load i32
//   ...
// b2:
//   x = phi x1, x2
//   y = and x, 0xff
//
// becomes (after a call to optimizeLoadExt for each load):
//
// b0:
//   x1 = load i32
//   x1' = and x1, 0xff
//   ...
// b1:
//   x2 = load i32
//   x2' = and x2, 0xff
//   ...
// b2:
//   x = phi x1', x2'
//   y = and x, 0xff
bool CodeGenPrepare::optimizeLoadExt(LoadInst *Load) {
  if (!Load->isSimple() || !Load->getType()->isIntOrPtrTy())
    return false;

  // Skip loads we've already transformed.
  if (Load->hasOneUse() &&
      InsertedInsts.count(cast<Instruction>(*Load->user_begin())))
    return false;

  // Look at all uses of Load, looking through phis, to determine how many bits
  // of the loaded value are needed.
  SmallVector<Instruction *, 8> WorkList;
  SmallPtrSet<Instruction *, 16> Visited;
  SmallVector<Instruction *, 8> AndsToMaybeRemove;
  for (auto *U : Load->users())
    WorkList.push_back(cast<Instruction>(U));

  EVT LoadResultVT = TLI->getValueType(*DL, Load->getType());
  unsigned BitWidth = LoadResultVT.getSizeInBits();
  // If the BitWidth is 0, do not try to optimize the type
  if (BitWidth == 0)
    return false;

  APInt DemandBits(BitWidth, 0);
  APInt WidestAndBits(BitWidth, 0);

  while (!WorkList.empty()) {
    Instruction *I = WorkList.pop_back_val();

    // Break use-def graph loops.
    if (!Visited.insert(I).second)
      continue;

    // For a PHI node, push all of its users.
    if (auto *Phi = dyn_cast<PHINode>(I)) {
      for (auto *U : Phi->users())
        WorkList.push_back(cast<Instruction>(U));
      continue;
    }

    switch (I->getOpcode()) {
    case Instruction::And: {
      auto *AndC = dyn_cast<ConstantInt>(I->getOperand(1));
      if (!AndC)
        return false;
      APInt AndBits = AndC->getValue();
      DemandBits |= AndBits;
      // Keep track of the widest and mask we see.
      if (AndBits.ugt(WidestAndBits))
        WidestAndBits = AndBits;
      if (AndBits == WidestAndBits && I->getOperand(0) == Load)
        AndsToMaybeRemove.push_back(I);
      break;
    }

    case Instruction::Shl: {
      auto *ShlC = dyn_cast<ConstantInt>(I->getOperand(1));
      if (!ShlC)
        return false;
      uint64_t ShiftAmt = ShlC->getLimitedValue(BitWidth - 1);
      DemandBits.setLowBits(BitWidth - ShiftAmt);
      break;
    }

    case Instruction::Trunc: {
      EVT TruncVT = TLI->getValueType(*DL, I->getType());
      unsigned TruncBitWidth = TruncVT.getSizeInBits();
      DemandBits.setLowBits(TruncBitWidth);
      break;
    }

    default:
      return false;
    }
  }

  uint32_t ActiveBits = DemandBits.getActiveBits();
  // Avoid hoisting (and (load x) 1) since it is unlikely to be folded by the
  // target even if isLoadExtLegal says an i1 EXTLOAD is valid.  For example,
  // for the AArch64 target isLoadExtLegal(ZEXTLOAD, i32, i1) returns true, but
  // (and (load x) 1) is not matched as a single instruction, rather as a LDR
  // followed by an AND.
  // TODO: Look into removing this restriction by fixing backends to either
  // return false for isLoadExtLegal for i1 or have them select this pattern to
  // a single instruction.
  //
  // Also avoid hoisting if we didn't see any ands with the exact DemandBits
  // mask, since these are the only ands that will be removed by isel.
  if (ActiveBits <= 1 || !DemandBits.isMask(ActiveBits) ||
      WidestAndBits != DemandBits)
    return false;

  LLVMContext &Ctx = Load->getType()->getContext();
  Type *TruncTy = Type::getIntNTy(Ctx, ActiveBits);
  EVT TruncVT = TLI->getValueType(*DL, TruncTy);

  // Reject cases that won't be matched as extloads.
  if (!LoadResultVT.bitsGT(TruncVT) || !TruncVT.isRound() ||
      !TLI->isLoadExtLegal(ISD::ZEXTLOAD, LoadResultVT, TruncVT))
    return false;

  IRBuilder<> Builder(Load->getNextNonDebugInstruction());
  auto *NewAnd = cast<Instruction>(
      Builder.CreateAnd(Load, ConstantInt::get(Ctx, DemandBits)));
  // Mark this instruction as "inserted by CGP", so that other
  // optimizations don't touch it.
  InsertedInsts.insert(NewAnd);

  // Replace all uses of load with new and (except for the use of load in the
  // new and itself).
  replaceAllUsesWith(Load, NewAnd, FreshBBs, IsHugeFunc);
  NewAnd->setOperand(0, Load);

  // Remove any and instructions that are now redundant.
  for (auto *And : AndsToMaybeRemove)
    // Check that the and mask is the same as the one we decided to put on the
    // new and.
    if (cast<ConstantInt>(And->getOperand(1))->getValue() == DemandBits) {
      replaceAllUsesWith(And, NewAnd, FreshBBs, IsHugeFunc);
      if (&*CurInstIterator == And)
        CurInstIterator = std::next(And->getIterator());
      And->eraseFromParent();
      ++NumAndUses;
    }

  ++NumAndsAdded;
  return true;
}

/// Check if V (an operand of a select instruction) is an expensive instruction
/// that is only used once.
static bool sinkSelectOperand(const TargetTransformInfo *TTI, Value *V) {
  auto *I = dyn_cast<Instruction>(V);
  // If it's safe to speculatively execute, then it should not have side
  // effects; therefore, it's safe to sink and possibly *not* execute.
  return I && I->hasOneUse() && isSafeToSpeculativelyExecute(I) &&
         TTI->isExpensiveToSpeculativelyExecute(I);
}

/// Returns true if a SelectInst should be turned into an explicit branch.
static bool isFormingBranchFromSelectProfitable(const TargetTransformInfo *TTI,
                                                const TargetLowering *TLI,
                                                SelectInst *SI) {
  // If even a predictable select is cheap, then a branch can't be cheaper.
  if (!TLI->isPredictableSelectExpensive())
    return false;

  // FIXME: This should use the same heuristics as IfConversion to determine
  // whether a select is better represented as a branch.

  // If metadata tells us that the select condition is obviously predictable,
  // then we want to replace the select with a branch.
  uint64_t TrueWeight, FalseWeight;
  if (extractBranchWeights(*SI, TrueWeight, FalseWeight)) {
    uint64_t Max = std::max(TrueWeight, FalseWeight);
    uint64_t Sum = TrueWeight + FalseWeight;
    if (Sum != 0) {
      auto Probability = BranchProbability::getBranchProbability(Max, Sum);
      if (Probability > TTI->getPredictableBranchThreshold())
        return true;
    }
  }

  CmpInst *Cmp = dyn_cast<CmpInst>(SI->getCondition());

  // If a branch is predictable, an out-of-order CPU can avoid blocking on its
  // comparison condition. If the compare has more than one use, there's
  // probably another cmov or setcc around, so it's not worth emitting a branch.
  if (!Cmp || !Cmp->hasOneUse())
    return false;

  // If either operand of the select is expensive and only needed on one side
  // of the select, we should form a branch.
  if (sinkSelectOperand(TTI, SI->getTrueValue()) ||
      sinkSelectOperand(TTI, SI->getFalseValue()))
    return true;

  return false;
}

/// If \p isTrue is true, return the true value of \p SI, otherwise return
/// false value of \p SI. If the true/false value of \p SI is defined by any
/// select instructions in \p Selects, look through the defining select
/// instruction until the true/false value is not defined in \p Selects.
static Value *
getTrueOrFalseValue(SelectInst *SI, bool isTrue,
                    const SmallPtrSet<const Instruction *, 2> &Selects) {
  Value *V = nullptr;

  for (SelectInst *DefSI = SI; DefSI != nullptr && Selects.count(DefSI);
       DefSI = dyn_cast<SelectInst>(V)) {
    assert(DefSI->getCondition() == SI->getCondition() &&
           "The condition of DefSI does not match with SI");
    V = (isTrue ? DefSI->getTrueValue() : DefSI->getFalseValue());
  }

  assert(V && "Failed to get select true/false value");
  return V;
}

bool CodeGenPrepare::optimizeShiftInst(BinaryOperator *Shift) {
  assert(Shift->isShift() && "Expected a shift");

  // If this is (1) a vector shift, (2) shifts by scalars are cheaper than
  // general vector shifts, and (3) the shift amount is a select-of-splatted
  // values, hoist the shifts before the select:
  //   shift Op0, (select Cond, TVal, FVal) -->
  //   select Cond, (shift Op0, TVal), (shift Op0, FVal)
  //
  // This is inverting a generic IR transform when we know that the cost of a
  // general vector shift is more than the cost of 2 shift-by-scalars.
  // We can't do this effectively in SDAG because we may not be able to
  // determine if the select operands are splats from within a basic block.
  Type *Ty = Shift->getType();
  if (!Ty->isVectorTy() || !TLI->isVectorShiftByScalarCheap(Ty))
    return false;
  Value *Cond, *TVal, *FVal;
  if (!match(Shift->getOperand(1),
             m_OneUse(m_Select(m_Value(Cond), m_Value(TVal), m_Value(FVal)))))
    return false;
  if (!isSplatValue(TVal) || !isSplatValue(FVal))
    return false;

  IRBuilder<> Builder(Shift);
  BinaryOperator::BinaryOps Opcode = Shift->getOpcode();
  Value *NewTVal = Builder.CreateBinOp(Opcode, Shift->getOperand(0), TVal);
  Value *NewFVal = Builder.CreateBinOp(Opcode, Shift->getOperand(0), FVal);
  Value *NewSel = Builder.CreateSelect(Cond, NewTVal, NewFVal);
  replaceAllUsesWith(Shift, NewSel, FreshBBs, IsHugeFunc);
  Shift->eraseFromParent();
  return true;
}

bool CodeGenPrepare::optimizeFunnelShift(IntrinsicInst *Fsh) {
  Intrinsic::ID Opcode = Fsh->getIntrinsicID();
  assert((Opcode == Intrinsic::fshl || Opcode == Intrinsic::fshr) &&
         "Expected a funnel shift");

  // If this is (1) a vector funnel shift, (2) shifts by scalars are cheaper
  // than general vector shifts, and (3) the shift amount is select-of-splatted
  // values, hoist the funnel shifts before the select:
  //   fsh Op0, Op1, (select Cond, TVal, FVal) -->
  //   select Cond, (fsh Op0, Op1, TVal), (fsh Op0, Op1, FVal)
  //
  // This is inverting a generic IR transform when we know that the cost of a
  // general vector shift is more than the cost of 2 shift-by-scalars.
  // We can't do this effectively in SDAG because we may not be able to
  // determine if the select operands are splats from within a basic block.
  Type *Ty = Fsh->getType();
  if (!Ty->isVectorTy() || !TLI->isVectorShiftByScalarCheap(Ty))
    return false;
  Value *Cond, *TVal, *FVal;
  if (!match(Fsh->getOperand(2),
             m_OneUse(m_Select(m_Value(Cond), m_Value(TVal), m_Value(FVal)))))
    return false;
  if (!isSplatValue(TVal) || !isSplatValue(FVal))
    return false;

  IRBuilder<> Builder(Fsh);
  Value *X = Fsh->getOperand(0), *Y = Fsh->getOperand(1);
  Value *NewTVal = Builder.CreateIntrinsic(Opcode, Ty, {X, Y, TVal});
  Value *NewFVal = Builder.CreateIntrinsic(Opcode, Ty, {X, Y, FVal});
  Value *NewSel = Builder.CreateSelect(Cond, NewTVal, NewFVal);
  replaceAllUsesWith(Fsh, NewSel, FreshBBs, IsHugeFunc);
  Fsh->eraseFromParent();
  return true;
}

/// If we have a SelectInst that will likely profit from branch prediction,
/// turn it into a branch.
bool CodeGenPrepare::optimizeSelectInst(SelectInst *SI) {
  if (DisableSelectToBranch)
    return false;

  // If the SelectOptimize pass is enabled, selects have already been optimized.
  if (!getCGPassBuilderOption().DisableSelectOptimize)
    return false;

  // Find all consecutive select instructions that share the same condition.
  SmallVector<SelectInst *, 2> ASI;
  ASI.push_back(SI);
  for (BasicBlock::iterator It = ++BasicBlock::iterator(SI);
       It != SI->getParent()->end(); ++It) {
    SelectInst *I = dyn_cast<SelectInst>(&*It);
    if (I && SI->getCondition() == I->getCondition()) {
      ASI.push_back(I);
    } else {
      break;
    }
  }

  SelectInst *LastSI = ASI.back();
  // Increment the current iterator to skip all the rest of select instructions
  // because they will be either "not lowered" or "all lowered" to branch.
  CurInstIterator = std::next(LastSI->getIterator());
  // Examine debug-info attached to the consecutive select instructions. They
  // won't be individually optimised by optimizeInst, so we need to perform
  // DbgVariableRecord maintenence here instead.
  for (SelectInst *SI : ArrayRef(ASI).drop_front())
    fixupDbgVariableRecordsOnInst(*SI);

  bool VectorCond = !SI->getCondition()->getType()->isIntegerTy(1);

  // Can we convert the 'select' to CF ?
  if (VectorCond || SI->getMetadata(LLVMContext::MD_unpredictable))
    return false;

  TargetLowering::SelectSupportKind SelectKind;
  if (SI->getType()->isVectorTy())
    SelectKind = TargetLowering::ScalarCondVectorVal;
  else
    SelectKind = TargetLowering::ScalarValSelect;

  if (TLI->isSelectSupported(SelectKind) &&
      (!isFormingBranchFromSelectProfitable(TTI, TLI, SI) || OptSize ||
       llvm::shouldOptimizeForSize(SI->getParent(), PSI, BFI.get())))
    return false;

  // The DominatorTree needs to be rebuilt by any consumers after this
  // transformation. We simply reset here rather than setting the ModifiedDT
  // flag to avoid restarting the function walk in runOnFunction for each
  // select optimized.
  DT.reset();

  // Transform a sequence like this:
  //    start:
  //       %cmp = cmp uge i32 %a, %b
  //       %sel = select i1 %cmp, i32 %c, i32 %d
  //
  // Into:
  //    start:
  //       %cmp = cmp uge i32 %a, %b
  //       %cmp.frozen = freeze %cmp
  //       br i1 %cmp.frozen, label %select.true, label %select.false
  //    select.true:
  //       br label %select.end
  //    select.false:
  //       br label %select.end
  //    select.end:
  //       %sel = phi i32 [ %c, %select.true ], [ %d, %select.false ]
  //
  // %cmp should be frozen, otherwise it may introduce undefined behavior.
  // In addition, we may sink instructions that produce %c or %d from
  // the entry block into the destination(s) of the new branch.
  // If the true or false blocks do not contain a sunken instruction, that
  // block and its branch may be optimized away. In that case, one side of the
  // first branch will point directly to select.end, and the corresponding PHI
  // predecessor block will be the start block.

  // Collect values that go on the true side and the values that go on the false
  // side.
  SmallVector<Instruction *> TrueInstrs, FalseInstrs;
  for (SelectInst *SI : ASI) {
    if (Value *V = SI->getTrueValue(); sinkSelectOperand(TTI, V))
      TrueInstrs.push_back(cast<Instruction>(V));
    if (Value *V = SI->getFalseValue(); sinkSelectOperand(TTI, V))
      FalseInstrs.push_back(cast<Instruction>(V));
  }

  // Split the select block, according to how many (if any) values go on each
  // side.
  BasicBlock *StartBlock = SI->getParent();
  BasicBlock::iterator SplitPt = std::next(BasicBlock::iterator(LastSI));
  // We should split before any debug-info.
  SplitPt.setHeadBit(true);

  IRBuilder<> IB(SI);
  auto *CondFr = IB.CreateFreeze(SI->getCondition(), SI->getName() + ".frozen");

  BasicBlock *TrueBlock = nullptr;
  BasicBlock *FalseBlock = nullptr;
  BasicBlock *EndBlock = nullptr;
  BranchInst *TrueBranch = nullptr;
  BranchInst *FalseBranch = nullptr;
  if (TrueInstrs.size() == 0) {
    FalseBranch = cast<BranchInst>(SplitBlockAndInsertIfElse(
        CondFr, SplitPt, false, nullptr, nullptr, LI));
    FalseBlock = FalseBranch->getParent();
    EndBlock = cast<BasicBlock>(FalseBranch->getOperand(0));
  } else if (FalseInstrs.size() == 0) {
    TrueBranch = cast<BranchInst>(SplitBlockAndInsertIfThen(
        CondFr, SplitPt, false, nullptr, nullptr, LI));
    TrueBlock = TrueBranch->getParent();
    EndBlock = cast<BasicBlock>(TrueBranch->getOperand(0));
  } else {
    Instruction *ThenTerm = nullptr;
    Instruction *ElseTerm = nullptr;
    SplitBlockAndInsertIfThenElse(CondFr, SplitPt, &ThenTerm, &ElseTerm,
                                  nullptr, nullptr, LI);
    TrueBranch = cast<BranchInst>(ThenTerm);
    FalseBranch = cast<BranchInst>(ElseTerm);
    TrueBlock = TrueBranch->getParent();
    FalseBlock = FalseBranch->getParent();
    EndBlock = cast<BasicBlock>(TrueBranch->getOperand(0));
  }

  EndBlock->setName("select.end");
  if (TrueBlock)
    TrueBlock->setName("select.true.sink");
  if (FalseBlock)
    FalseBlock->setName(FalseInstrs.size() == 0 ? "select.false"
                                                : "select.false.sink");

  if (IsHugeFunc) {
    if (TrueBlock)
      FreshBBs.insert(TrueBlock);
    if (FalseBlock)
      FreshBBs.insert(FalseBlock);
    FreshBBs.insert(EndBlock);
  }

  BFI->setBlockFreq(EndBlock, BFI->getBlockFreq(StartBlock));

  static const unsigned MD[] = {
      LLVMContext::MD_prof, LLVMContext::MD_unpredictable,
      LLVMContext::MD_make_implicit, LLVMContext::MD_dbg};
  StartBlock->getTerminator()->copyMetadata(*SI, MD);

  // Sink expensive instructions into the conditional blocks to avoid executing
  // them speculatively.
  for (Instruction *I : TrueInstrs)
    I->moveBefore(TrueBranch);
  for (Instruction *I : FalseInstrs)
    I->moveBefore(FalseBranch);

  // If we did not create a new block for one of the 'true' or 'false' paths
  // of the condition, it means that side of the branch goes to the end block
  // directly and the path originates from the start block from the point of
  // view of the new PHI.
  if (TrueBlock == nullptr)
    TrueBlock = StartBlock;
  else if (FalseBlock == nullptr)
    FalseBlock = StartBlock;

  SmallPtrSet<const Instruction *, 2> INS;
  INS.insert(ASI.begin(), ASI.end());
  // Use reverse iterator because later select may use the value of the
  // earlier select, and we need to propagate value through earlier select
  // to get the PHI operand.
  for (SelectInst *SI : llvm::reverse(ASI)) {
    // The select itself is replaced with a PHI Node.
    PHINode *PN = PHINode::Create(SI->getType(), 2, "");
    PN->insertBefore(EndBlock->begin());
    PN->takeName(SI);
    PN->addIncoming(getTrueOrFalseValue(SI, true, INS), TrueBlock);
    PN->addIncoming(getTrueOrFalseValue(SI, false, INS), FalseBlock);
    PN->setDebugLoc(SI->getDebugLoc());

    replaceAllUsesWith(SI, PN, FreshBBs, IsHugeFunc);
    SI->eraseFromParent();
    INS.erase(SI);
    ++NumSelectsExpanded;
  }

  // Instruct OptimizeBlock to skip to the next block.
  CurInstIterator = StartBlock->end();
  return true;
}

/// Some targets only accept certain types for splat inputs. For example a VDUP
/// in MVE takes a GPR (integer) register, and the instruction that incorporate
/// a VDUP (such as a VADD qd, qm, rm) also require a gpr register.
bool CodeGenPrepare::optimizeShuffleVectorInst(ShuffleVectorInst *SVI) {
  // Accept shuf(insertelem(undef/poison, val, 0), undef/poison, <0,0,..>) only
  if (!match(SVI, m_Shuffle(m_InsertElt(m_Undef(), m_Value(), m_ZeroInt()),
                            m_Undef(), m_ZeroMask())))
    return false;
  Type *NewType = TLI->shouldConvertSplatType(SVI);
  if (!NewType)
    return false;

  auto *SVIVecType = cast<FixedVectorType>(SVI->getType());
  assert(!NewType->isVectorTy() && "Expected a scalar type!");
  assert(NewType->getScalarSizeInBits() == SVIVecType->getScalarSizeInBits() &&
         "Expected a type of the same size!");
  auto *NewVecType =
      FixedVectorType::get(NewType, SVIVecType->getNumElements());

  // Create a bitcast (shuffle (insert (bitcast(..))))
  IRBuilder<> Builder(SVI->getContext());
  Builder.SetInsertPoint(SVI);
  Value *BC1 = Builder.CreateBitCast(
      cast<Instruction>(SVI->getOperand(0))->getOperand(1), NewType);
  Value *Shuffle = Builder.CreateVectorSplat(NewVecType->getNumElements(), BC1);
  Value *BC2 = Builder.CreateBitCast(Shuffle, SVIVecType);

  replaceAllUsesWith(SVI, BC2, FreshBBs, IsHugeFunc);
  RecursivelyDeleteTriviallyDeadInstructions(
      SVI, TLInfo, nullptr,
      [&](Value *V) { removeAllAssertingVHReferences(V); });

  // Also hoist the bitcast up to its operand if it they are not in the same
  // block.
  if (auto *BCI = dyn_cast<Instruction>(BC1))
    if (auto *Op = dyn_cast<Instruction>(BCI->getOperand(0)))
      if (BCI->getParent() != Op->getParent() && !isa<PHINode>(Op) &&
          !Op->isTerminator() && !Op->isEHPad())
        BCI->moveAfter(Op);

  return true;
}

bool CodeGenPrepare::tryToSinkFreeOperands(Instruction *I) {
  // If the operands of I can be folded into a target instruction together with
  // I, duplicate and sink them.
  SmallVector<Use *, 4> OpsToSink;
  if (!TLI->shouldSinkOperands(I, OpsToSink))
    return false;

  // OpsToSink can contain multiple uses in a use chain (e.g.
  // (%u1 with %u1 = shufflevector), (%u2 with %u2 = zext %u1)). The dominating
  // uses must come first, so we process the ops in reverse order so as to not
  // create invalid IR.
  BasicBlock *TargetBB = I->getParent();
  bool Changed = false;
  SmallVector<Use *, 4> ToReplace;
  Instruction *InsertPoint = I;
  DenseMap<const Instruction *, unsigned long> InstOrdering;
  unsigned long InstNumber = 0;
  for (const auto &I : *TargetBB)
    InstOrdering[&I] = InstNumber++;

  for (Use *U : reverse(OpsToSink)) {
    auto *UI = cast<Instruction>(U->get());
    if (isa<PHINode>(UI))
      continue;
    if (UI->getParent() == TargetBB) {
      if (InstOrdering[UI] < InstOrdering[InsertPoint])
        InsertPoint = UI;
      continue;
    }
    ToReplace.push_back(U);
  }

  SetVector<Instruction *> MaybeDead;
  DenseMap<Instruction *, Instruction *> NewInstructions;
  for (Use *U : ToReplace) {
    auto *UI = cast<Instruction>(U->get());
    Instruction *NI = UI->clone();

    if (IsHugeFunc) {
      // Now we clone an instruction, its operands' defs may sink to this BB
      // now. So we put the operands defs' BBs into FreshBBs to do optimization.
      for (unsigned I = 0; I < NI->getNumOperands(); ++I) {
        auto *OpDef = dyn_cast<Instruction>(NI->getOperand(I));
        if (!OpDef)
          continue;
        FreshBBs.insert(OpDef->getParent());
      }
    }

    NewInstructions[UI] = NI;
    MaybeDead.insert(UI);
    LLVM_DEBUG(dbgs() << "Sinking " << *UI << " to user " << *I << "\n");
    NI->insertBefore(InsertPoint);
    InsertPoint = NI;
    InsertedInsts.insert(NI);

    // Update the use for the new instruction, making sure that we update the
    // sunk instruction uses, if it is part of a chain that has already been
    // sunk.
    Instruction *OldI = cast<Instruction>(U->getUser());
    if (NewInstructions.count(OldI))
      NewInstructions[OldI]->setOperand(U->getOperandNo(), NI);
    else
      U->set(NI);
    Changed = true;
  }

  // Remove instructions that are dead after sinking.
  for (auto *I : MaybeDead) {
    if (!I->hasNUsesOrMore(1)) {
      LLVM_DEBUG(dbgs() << "Removing dead instruction: " << *I << "\n");
      I->eraseFromParent();
    }
  }

  return Changed;
}

bool CodeGenPrepare::optimizeSwitchType(SwitchInst *SI) {
  Value *Cond = SI->getCondition();
  Type *OldType = Cond->getType();
  LLVMContext &Context = Cond->getContext();
  EVT OldVT = TLI->getValueType(*DL, OldType);
  MVT RegType = TLI->getPreferredSwitchConditionType(Context, OldVT);
  unsigned RegWidth = RegType.getSizeInBits();

  if (RegWidth <= cast<IntegerType>(OldType)->getBitWidth())
    return false;

  // If the register width is greater than the type width, expand the condition
  // of the switch instruction and each case constant to the width of the
  // register. By widening the type of the switch condition, subsequent
  // comparisons (for case comparisons) will not need to be extended to the
  // preferred register width, so we will potentially eliminate N-1 extends,
  // where N is the number of cases in the switch.
  auto *NewType = Type::getIntNTy(Context, RegWidth);

  // Extend the switch condition and case constants using the target preferred
  // extend unless the switch condition is a function argument with an extend
  // attribute. In that case, we can avoid an unnecessary mask/extension by
  // matching the argument extension instead.
  Instruction::CastOps ExtType = Instruction::ZExt;
  // Some targets prefer SExt over ZExt.
  if (TLI->isSExtCheaperThanZExt(OldVT, RegType))
    ExtType = Instruction::SExt;

  if (auto *Arg = dyn_cast<Argument>(Cond)) {
    if (Arg->hasSExtAttr())
      ExtType = Instruction::SExt;
    if (Arg->hasZExtAttr())
      ExtType = Instruction::ZExt;
  }

  auto *ExtInst = CastInst::Create(ExtType, Cond, NewType);
  ExtInst->insertBefore(SI);
  ExtInst->setDebugLoc(SI->getDebugLoc());
  SI->setCondition(ExtInst);
  for (auto Case : SI->cases()) {
    const APInt &NarrowConst = Case.getCaseValue()->getValue();
    APInt WideConst = (ExtType == Instruction::ZExt)
                          ? NarrowConst.zext(RegWidth)
                          : NarrowConst.sext(RegWidth);
    Case.setValue(ConstantInt::get(Context, WideConst));
  }

  return true;
}

bool CodeGenPrepare::optimizeSwitchPhiConstants(SwitchInst *SI) {
  // The SCCP optimization tends to produce code like this:
  //   switch(x) { case 42: phi(42, ...) }
  // Materializing the constant for the phi-argument needs instructions; So we
  // change the code to:
  //   switch(x) { case 42: phi(x, ...) }

  Value *Condition = SI->getCondition();
  // Avoid endless loop in degenerate case.
  if (isa<ConstantInt>(*Condition))
    return false;

  bool Changed = false;
  BasicBlock *SwitchBB = SI->getParent();
  Type *ConditionType = Condition->getType();

  for (const SwitchInst::CaseHandle &Case : SI->cases()) {
    ConstantInt *CaseValue = Case.getCaseValue();
    BasicBlock *CaseBB = Case.getCaseSuccessor();
    // Set to true if we previously checked that `CaseBB` is only reached by
    // a single case from this switch.
    bool CheckedForSinglePred = false;
    for (PHINode &PHI : CaseBB->phis()) {
      Type *PHIType = PHI.getType();
      // If ZExt is free then we can also catch patterns like this:
      //   switch((i32)x) { case 42: phi((i64)42, ...); }
      // and replace `(i64)42` with `zext i32 %x to i64`.
      bool TryZExt =
          PHIType->isIntegerTy() &&
          PHIType->getIntegerBitWidth() > ConditionType->getIntegerBitWidth() &&
          TLI->isZExtFree(ConditionType, PHIType);
      if (PHIType == ConditionType || TryZExt) {
        // Set to true to skip this case because of multiple preds.
        bool SkipCase = false;
        Value *Replacement = nullptr;
        for (unsigned I = 0, E = PHI.getNumIncomingValues(); I != E; I++) {
          Value *PHIValue = PHI.getIncomingValue(I);
          if (PHIValue != CaseValue) {
            if (!TryZExt)
              continue;
            ConstantInt *PHIValueInt = dyn_cast<ConstantInt>(PHIValue);
            if (!PHIValueInt ||
                PHIValueInt->getValue() !=
                    CaseValue->getValue().zext(PHIType->getIntegerBitWidth()))
              continue;
          }
          if (PHI.getIncomingBlock(I) != SwitchBB)
            continue;
          // We cannot optimize if there are multiple case labels jumping to
          // this block.  This check may get expensive when there are many
          // case labels so we test for it last.
          if (!CheckedForSinglePred) {
            CheckedForSinglePred = true;
            if (SI->findCaseDest(CaseBB) == nullptr) {
              SkipCase = true;
              break;
            }
          }

          if (Replacement == nullptr) {
            if (PHIValue == CaseValue) {
              Replacement = Condition;
            } else {
              IRBuilder<> Builder(SI);
              Replacement = Builder.CreateZExt(Condition, PHIType);
            }
          }
          PHI.setIncomingValue(I, Replacement);
          Changed = true;
        }
        if (SkipCase)
          break;
      }
    }
  }
  return Changed;
}

bool CodeGenPrepare::optimizeSwitchInst(SwitchInst *SI) {
  bool Changed = optimizeSwitchType(SI);
  Changed |= optimizeSwitchPhiConstants(SI);
  return Changed;
}

namespace {

/// Helper class to promote a scalar operation to a vector one.
/// This class is used to move downward extractelement transition.
/// E.g.,
/// a = vector_op <2 x i32>
/// b = extractelement <2 x i32> a, i32 0
/// c = scalar_op b
/// store c
///
/// =>
/// a = vector_op <2 x i32>
/// c = vector_op a (equivalent to scalar_op on the related lane)
/// * d = extractelement <2 x i32> c, i32 0
/// * store d
/// Assuming both extractelement and store can be combine, we get rid of the
/// transition.
class VectorPromoteHelper {
  /// DataLayout associated with the current module.
  const DataLayout &DL;

  /// Used to perform some checks on the legality of vector operations.
  const TargetLowering &TLI;

  /// Used to estimated the cost of the promoted chain.
  const TargetTransformInfo &TTI;

  /// The transition being moved downwards.
  Instruction *Transition;

  /// The sequence of instructions to be promoted.
  SmallVector<Instruction *, 4> InstsToBePromoted;

  /// Cost of combining a store and an extract.
  unsigned StoreExtractCombineCost;

  /// Instruction that will be combined with the transition.
  Instruction *CombineInst = nullptr;

  /// The instruction that represents the current end of the transition.
  /// Since we are faking the promotion until we reach the end of the chain
  /// of computation, we need a way to get the current end of the transition.
  Instruction *getEndOfTransition() const {
    if (InstsToBePromoted.empty())
      return Transition;
    return InstsToBePromoted.back();
  }

  /// Return the index of the original value in the transition.
  /// E.g., for "extractelement <2 x i32> c, i32 1" the original value,
  /// c, is at index 0.
  unsigned getTransitionOriginalValueIdx() const {
    assert(isa<ExtractElementInst>(Transition) &&
           "Other kind of transitions are not supported yet");
    return 0;
  }

  /// Return the index of the index in the transition.
  /// E.g., for "extractelement <2 x i32> c, i32 0" the index
  /// is at index 1.
  unsigned getTransitionIdx() const {
    assert(isa<ExtractElementInst>(Transition) &&
           "Other kind of transitions are not supported yet");
    return 1;
  }

  /// Get the type of the transition.
  /// This is the type of the original value.
  /// E.g., for "extractelement <2 x i32> c, i32 1" the type of the
  /// transition is <2 x i32>.
  Type *getTransitionType() const {
    return Transition->getOperand(getTransitionOriginalValueIdx())->getType();
  }

  /// Promote \p ToBePromoted by moving \p Def downward through.
  /// I.e., we have the following sequence:
  /// Def = Transition <ty1> a to <ty2>
  /// b = ToBePromoted <ty2> Def, ...
  /// =>
  /// b = ToBePromoted <ty1> a, ...
  /// Def = Transition <ty1> ToBePromoted to <ty2>
  void promoteImpl(Instruction *ToBePromoted);

  /// Check whether or not it is profitable to promote all the
  /// instructions enqueued to be promoted.
  bool isProfitableToPromote() {
    Value *ValIdx = Transition->getOperand(getTransitionOriginalValueIdx());
    unsigned Index = isa<ConstantInt>(ValIdx)
                         ? cast<ConstantInt>(ValIdx)->getZExtValue()
                         : -1;
    Type *PromotedType = getTransitionType();

    StoreInst *ST = cast<StoreInst>(CombineInst);
    unsigned AS = ST->getPointerAddressSpace();
    // Check if this store is supported.
    if (!TLI.allowsMisalignedMemoryAccesses(
            TLI.getValueType(DL, ST->getValueOperand()->getType()), AS,
            ST->getAlign())) {
      // If this is not supported, there is no way we can combine
      // the extract with the store.
      return false;
    }

    // The scalar chain of computation has to pay for the transition
    // scalar to vector.
    // The vector chain has to account for the combining cost.
    enum TargetTransformInfo::TargetCostKind CostKind =
        TargetTransformInfo::TCK_RecipThroughput;
    InstructionCost ScalarCost =
        TTI.getVectorInstrCost(*Transition, PromotedType, CostKind, Index);
    InstructionCost VectorCost = StoreExtractCombineCost;
    for (const auto &Inst : InstsToBePromoted) {
      // Compute the cost.
      // By construction, all instructions being promoted are arithmetic ones.
      // Moreover, one argument is a constant that can be viewed as a splat
      // constant.
      Value *Arg0 = Inst->getOperand(0);
      bool IsArg0Constant = isa<UndefValue>(Arg0) || isa<ConstantInt>(Arg0) ||
                            isa<ConstantFP>(Arg0);
      TargetTransformInfo::OperandValueInfo Arg0Info, Arg1Info;
      if (IsArg0Constant)
        Arg0Info.Kind = TargetTransformInfo::OK_UniformConstantValue;
      else
        Arg1Info.Kind = TargetTransformInfo::OK_UniformConstantValue;

      ScalarCost += TTI.getArithmeticInstrCost(
          Inst->getOpcode(), Inst->getType(), CostKind, Arg0Info, Arg1Info);
      VectorCost += TTI.getArithmeticInstrCost(Inst->getOpcode(), PromotedType,
                                               CostKind, Arg0Info, Arg1Info);
    }
    LLVM_DEBUG(
        dbgs() << "Estimated cost of computation to be promoted:\nScalar: "
               << ScalarCost << "\nVector: " << VectorCost << '\n');
    return ScalarCost > VectorCost;
  }

  /// Generate a constant vector with \p Val with the same
  /// number of elements as the transition.
  /// \p UseSplat defines whether or not \p Val should be replicated
  /// across the whole vector.
  /// In other words, if UseSplat == true, we generate <Val, Val, ..., Val>,
  /// otherwise we generate a vector with as many undef as possible:
  /// <undef, ..., undef, Val, undef, ..., undef> where \p Val is only
  /// used at the index of the extract.
  Value *getConstantVector(Constant *Val, bool UseSplat) const {
    unsigned ExtractIdx = std::numeric_limits<unsigned>::max();
    if (!UseSplat) {
      // If we cannot determine where the constant must be, we have to
      // use a splat constant.
      Value *ValExtractIdx = Transition->getOperand(getTransitionIdx());
      if (ConstantInt *CstVal = dyn_cast<ConstantInt>(ValExtractIdx))
        ExtractIdx = CstVal->getSExtValue();
      else
        UseSplat = true;
    }

    ElementCount EC = cast<VectorType>(getTransitionType())->getElementCount();
    if (UseSplat)
      return ConstantVector::getSplat(EC, Val);

    if (!EC.isScalable()) {
      SmallVector<Constant *, 4> ConstVec;
      UndefValue *UndefVal = UndefValue::get(Val->getType());
      for (unsigned Idx = 0; Idx != EC.getKnownMinValue(); ++Idx) {
        if (Idx == ExtractIdx)
          ConstVec.push_back(Val);
        else
          ConstVec.push_back(UndefVal);
      }
      return ConstantVector::get(ConstVec);
    } else
      llvm_unreachable(
          "Generate scalable vector for non-splat is unimplemented");
  }

  /// Check if promoting to a vector type an operand at \p OperandIdx
  /// in \p Use can trigger undefined behavior.
  static bool canCauseUndefinedBehavior(const Instruction *Use,
                                        unsigned OperandIdx) {
    // This is not safe to introduce undef when the operand is on
    // the right hand side of a division-like instruction.
    if (OperandIdx != 1)
      return false;
    switch (Use->getOpcode()) {
    default:
      return false;
    case Instruction::SDiv:
    case Instruction::UDiv:
    case Instruction::SRem:
    case Instruction::URem:
      return true;
    case Instruction::FDiv:
    case Instruction::FRem:
      return !Use->hasNoNaNs();
    }
    llvm_unreachable(nullptr);
  }

public:
  VectorPromoteHelper(const DataLayout &DL, const TargetLowering &TLI,
                      const TargetTransformInfo &TTI, Instruction *Transition,
                      unsigned CombineCost)
      : DL(DL), TLI(TLI), TTI(TTI), Transition(Transition),
        StoreExtractCombineCost(CombineCost) {
    assert(Transition && "Do not know how to promote null");
  }

  /// Check if we can promote \p ToBePromoted to \p Type.
  bool canPromote(const Instruction *ToBePromoted) const {
    // We could support CastInst too.
    return isa<BinaryOperator>(ToBePromoted);
  }

  /// Check if it is profitable to promote \p ToBePromoted
  /// by moving downward the transition through.
  bool shouldPromote(const Instruction *ToBePromoted) const {
    // Promote only if all the operands can be statically expanded.
    // Indeed, we do not want to introduce any new kind of transitions.
    for (const Use &U : ToBePromoted->operands()) {
      const Value *Val = U.get();
      if (Val == getEndOfTransition()) {
        // If the use is a division and the transition is on the rhs,
        // we cannot promote the operation, otherwise we may create a
        // division by zero.
        if (canCauseUndefinedBehavior(ToBePromoted, U.getOperandNo()))
          return false;
        continue;
      }
      if (!isa<ConstantInt>(Val) && !isa<UndefValue>(Val) &&
          !isa<ConstantFP>(Val))
        return false;
    }
    // Check that the resulting operation is legal.
    int ISDOpcode = TLI.InstructionOpcodeToISD(ToBePromoted->getOpcode());
    if (!ISDOpcode)
      return false;
    return StressStoreExtract ||
           TLI.isOperationLegalOrCustom(
               ISDOpcode, TLI.getValueType(DL, getTransitionType(), true));
  }

  /// Check whether or not \p Use can be combined
  /// with the transition.
  /// I.e., is it possible to do Use(Transition) => AnotherUse?
  bool canCombine(const Instruction *Use) { return isa<StoreInst>(Use); }

  /// Record \p ToBePromoted as part of the chain to be promoted.
  void enqueueForPromotion(Instruction *ToBePromoted) {
    InstsToBePromoted.push_back(ToBePromoted);
  }

  /// Set the instruction that will be combined with the transition.
  void recordCombineInstruction(Instruction *ToBeCombined) {
    assert(canCombine(ToBeCombined) && "Unsupported instruction to combine");
    CombineInst = ToBeCombined;
  }

  /// Promote all the instructions enqueued for promotion if it is
  /// is profitable.
  /// \return True if the promotion happened, false otherwise.
  bool promote() {
    // Check if there is something to promote.
    // Right now, if we do not have anything to combine with,
    // we assume the promotion is not profitable.
    if (InstsToBePromoted.empty() || !CombineInst)
      return false;

    // Check cost.
    if (!StressStoreExtract && !isProfitableToPromote())
      return false;

    // Promote.
    for (auto &ToBePromoted : InstsToBePromoted)
      promoteImpl(ToBePromoted);
    InstsToBePromoted.clear();
    return true;
  }
};

} // end anonymous namespace

void VectorPromoteHelper::promoteImpl(Instruction *ToBePromoted) {
  // At this point, we know that all the operands of ToBePromoted but Def
  // can be statically promoted.
  // For Def, we need to use its parameter in ToBePromoted:
  // b = ToBePromoted ty1 a
  // Def = Transition ty1 b to ty2
  // Move the transition down.
  // 1. Replace all uses of the promoted operation by the transition.
  // = ... b => = ... Def.
  assert(ToBePromoted->getType() == Transition->getType() &&
         "The type of the result of the transition does not match "
         "the final type");
  ToBePromoted->replaceAllUsesWith(Transition);
  // 2. Update the type of the uses.
  // b = ToBePromoted ty2 Def => b = ToBePromoted ty1 Def.
  Type *TransitionTy = getTransitionType();
  ToBePromoted->mutateType(TransitionTy);
  // 3. Update all the operands of the promoted operation with promoted
  // operands.
  // b = ToBePromoted ty1 Def => b = ToBePromoted ty1 a.
  for (Use &U : ToBePromoted->operands()) {
    Value *Val = U.get();
    Value *NewVal = nullptr;
    if (Val == Transition)
      NewVal = Transition->getOperand(getTransitionOriginalValueIdx());
    else if (isa<UndefValue>(Val) || isa<ConstantInt>(Val) ||
             isa<ConstantFP>(Val)) {
      // Use a splat constant if it is not safe to use undef.
      NewVal = getConstantVector(
          cast<Constant>(Val),
          isa<UndefValue>(Val) ||
              canCauseUndefinedBehavior(ToBePromoted, U.getOperandNo()));
    } else
      llvm_unreachable("Did you modified shouldPromote and forgot to update "
                       "this?");
    ToBePromoted->setOperand(U.getOperandNo(), NewVal);
  }
  Transition->moveAfter(ToBePromoted);
  Transition->setOperand(getTransitionOriginalValueIdx(), ToBePromoted);
}

/// Some targets can do store(extractelement) with one instruction.
/// Try to push the extractelement towards the stores when the target
/// has this feature and this is profitable.
bool CodeGenPrepare::optimizeExtractElementInst(Instruction *Inst) {
  unsigned CombineCost = std::numeric_limits<unsigned>::max();
  if (DisableStoreExtract ||
      (!StressStoreExtract &&
       !TLI->canCombineStoreAndExtract(Inst->getOperand(0)->getType(),
                                       Inst->getOperand(1), CombineCost)))
    return false;

  // At this point we know that Inst is a vector to scalar transition.
  // Try to move it down the def-use chain, until:
  // - We can combine the transition with its single use
  //   => we got rid of the transition.
  // - We escape the current basic block
  //   => we would need to check that we are moving it at a cheaper place and
  //      we do not do that for now.
  BasicBlock *Parent = Inst->getParent();
  LLVM_DEBUG(dbgs() << "Found an interesting transition: " << *Inst << '\n');
  VectorPromoteHelper VPH(*DL, *TLI, *TTI, Inst, CombineCost);
  // If the transition has more than one use, assume this is not going to be
  // beneficial.
  while (Inst->hasOneUse()) {
    Instruction *ToBePromoted = cast<Instruction>(*Inst->user_begin());
    LLVM_DEBUG(dbgs() << "Use: " << *ToBePromoted << '\n');

    if (ToBePromoted->getParent() != Parent) {
      LLVM_DEBUG(dbgs() << "Instruction to promote is in a different block ("
                        << ToBePromoted->getParent()->getName()
                        << ") than the transition (" << Parent->getName()
                        << ").\n");
      return false;
    }

    if (VPH.canCombine(ToBePromoted)) {
      LLVM_DEBUG(dbgs() << "Assume " << *Inst << '\n'
                        << "will be combined with: " << *ToBePromoted << '\n');
      VPH.recordCombineInstruction(ToBePromoted);
      bool Changed = VPH.promote();
      NumStoreExtractExposed += Changed;
      return Changed;
    }

    LLVM_DEBUG(dbgs() << "Try promoting.\n");
    if (!VPH.canPromote(ToBePromoted) || !VPH.shouldPromote(ToBePromoted))
      return false;

    LLVM_DEBUG(dbgs() << "Promoting is possible... Enqueue for promotion!\n");

    VPH.enqueueForPromotion(ToBePromoted);
    Inst = ToBePromoted;
  }
  return false;
}

/// For the instruction sequence of store below, F and I values
/// are bundled together as an i64 value before being stored into memory.
/// Sometimes it is more efficient to generate separate stores for F and I,
/// which can remove the bitwise instructions or sink them to colder places.
///
///   (store (or (zext (bitcast F to i32) to i64),
///              (shl (zext I to i64), 32)), addr)  -->
///   (store F, addr) and (store I, addr+4)
///
/// Similarly, splitting for other merged store can also be beneficial, like:
/// For pair of {i32, i32}, i64 store --> two i32 stores.
/// For pair of {i32, i16}, i64 store --> two i32 stores.
/// For pair of {i16, i16}, i32 store --> two i16 stores.
/// For pair of {i16, i8},  i32 store --> two i16 stores.
/// For pair of {i8, i8},   i16 store --> two i8 stores.
///
/// We allow each target to determine specifically which kind of splitting is
/// supported.
///
/// The store patterns are commonly seen from the simple code snippet below
/// if only std::make_pair(...) is sroa transformed before inlined into hoo.
///   void goo(const std::pair<int, float> &);
///   hoo() {
///     ...
///     goo(std::make_pair(tmp, ftmp));
///     ...
///   }
///
/// Although we already have similar splitting in DAG Combine, we duplicate
/// it in CodeGenPrepare to catch the case in which pattern is across
/// multiple BBs. The logic in DAG Combine is kept to catch case generated
/// during code expansion.
static bool splitMergedValStore(StoreInst &SI, const DataLayout &DL,
                                const TargetLowering &TLI) {
  // Handle simple but common cases only.
  Type *StoreType = SI.getValueOperand()->getType();

  // The code below assumes shifting a value by <number of bits>,
  // whereas scalable vectors would have to be shifted by
  // <2log(vscale) + number of bits> in order to store the
  // low/high parts. Bailing out for now.
  if (StoreType->isScalableTy())
    return false;

  if (!DL.typeSizeEqualsStoreSize(StoreType) ||
      DL.getTypeSizeInBits(StoreType) == 0)
    return false;

  unsigned HalfValBitSize = DL.getTypeSizeInBits(StoreType) / 2;
  Type *SplitStoreType = Type::getIntNTy(SI.getContext(), HalfValBitSize);
  if (!DL.typeSizeEqualsStoreSize(SplitStoreType))
    return false;

  // Don't split the store if it is volatile.
  if (SI.isVolatile())
    return false;

  // Match the following patterns:
  // (store (or (zext LValue to i64),
  //            (shl (zext HValue to i64), 32)), HalfValBitSize)
  //  or
  // (store (or (shl (zext HValue to i64), 32)), HalfValBitSize)
  //            (zext LValue to i64),
  // Expect both operands of OR and the first operand of SHL have only
  // one use.
  Value *LValue, *HValue;
  if (!match(SI.getValueOperand(),
             m_c_Or(m_OneUse(m_ZExt(m_Value(LValue))),
                    m_OneUse(m_Shl(m_OneUse(m_ZExt(m_Value(HValue))),
                                   m_SpecificInt(HalfValBitSize))))))
    return false;

  // Check LValue and HValue are int with size less or equal than 32.
  if (!LValue->getType()->isIntegerTy() ||
      DL.getTypeSizeInBits(LValue->getType()) > HalfValBitSize ||
      !HValue->getType()->isIntegerTy() ||
      DL.getTypeSizeInBits(HValue->getType()) > HalfValBitSize)
    return false;

  // If LValue/HValue is a bitcast instruction, use the EVT before bitcast
  // as the input of target query.
  auto *LBC = dyn_cast<BitCastInst>(LValue);
  auto *HBC = dyn_cast<BitCastInst>(HValue);
  EVT LowTy = LBC ? EVT::getEVT(LBC->getOperand(0)->getType())
                  : EVT::getEVT(LValue->getType());
  EVT HighTy = HBC ? EVT::getEVT(HBC->getOperand(0)->getType())
                   : EVT::getEVT(HValue->getType());
  if (!ForceSplitStore && !TLI.isMultiStoresCheaperThanBitsMerge(LowTy, HighTy))
    return false;

  // Start to split store.
  IRBuilder<> Builder(SI.getContext());
  Builder.SetInsertPoint(&SI);

  // If LValue/HValue is a bitcast in another BB, create a new one in current
  // BB so it may be merged with the splitted stores by dag combiner.
  if (LBC && LBC->getParent() != SI.getParent())
    LValue = Builder.CreateBitCast(LBC->getOperand(0), LBC->getType());
  if (HBC && HBC->getParent() != SI.getParent())
    HValue = Builder.CreateBitCast(HBC->getOperand(0), HBC->getType());

  bool IsLE = SI.getDataLayout().isLittleEndian();
  auto CreateSplitStore = [&](Value *V, bool Upper) {
    V = Builder.CreateZExtOrBitCast(V, SplitStoreType);
    Value *Addr = SI.getPointerOperand();
    Align Alignment = SI.getAlign();
    const bool IsOffsetStore = (IsLE && Upper) || (!IsLE && !Upper);
    if (IsOffsetStore) {
      Addr = Builder.CreateGEP(
          SplitStoreType, Addr,
          ConstantInt::get(Type::getInt32Ty(SI.getContext()), 1));

      // When splitting the store in half, naturally one half will retain the
      // alignment of the original wider store, regardless of whether it was
      // over-aligned or not, while the other will require adjustment.
      Alignment = commonAlignment(Alignment, HalfValBitSize / 8);
    }
    Builder.CreateAlignedStore(V, Addr, Alignment);
  };

  CreateSplitStore(LValue, false);
  CreateSplitStore(HValue, true);

  // Delete the old store.
  SI.eraseFromParent();
  return true;
}

// Return true if the GEP has two operands, the first operand is of a sequential
// type, and the second operand is a constant.
static bool GEPSequentialConstIndexed(GetElementPtrInst *GEP) {
  gep_type_iterator I = gep_type_begin(*GEP);
  return GEP->getNumOperands() == 2 && I.isSequential() &&
         isa<ConstantInt>(GEP->getOperand(1));
}

// Try unmerging GEPs to reduce liveness interference (register pressure) across
// IndirectBr edges. Since IndirectBr edges tend to touch on many blocks,
// reducing liveness interference across those edges benefits global register
// allocation. Currently handles only certain cases.
//
// For example, unmerge %GEPI and %UGEPI as below.
//
// ---------- BEFORE ----------
// SrcBlock:
//   ...
//   %GEPIOp = ...
//   ...
//   %GEPI = gep %GEPIOp, Idx
//   ...
//   indirectbr ... [ label %DstB0, label %DstB1, ... label %DstBi ... ]
//   (* %GEPI is alive on the indirectbr edges due to other uses ahead)
//   (* %GEPIOp is alive on the indirectbr edges only because of it's used by
//   %UGEPI)
//
// DstB0: ... (there may be a gep similar to %UGEPI to be unmerged)
// DstB1: ... (there may be a gep similar to %UGEPI to be unmerged)
// ...
//
// DstBi:
//   ...
//   %UGEPI = gep %GEPIOp, UIdx
// ...
// ---------------------------
//
// ---------- AFTER ----------
// SrcBlock:
//   ... (same as above)
//    (* %GEPI is still alive on the indirectbr edges)
//    (* %GEPIOp is no longer alive on the indirectbr edges as a result of the
//    unmerging)
// ...
//
// DstBi:
//   ...
//   %UGEPI = gep %GEPI, (UIdx-Idx)
//   ...
// ---------------------------
//
// The register pressure on the IndirectBr edges is reduced because %GEPIOp is
// no longer alive on them.
//
// We try to unmerge GEPs here in CodGenPrepare, as opposed to limiting merging
// of GEPs in the first place in InstCombiner::visitGetElementPtrInst() so as
// not to disable further simplications and optimizations as a result of GEP
// merging.
//
// Note this unmerging may increase the length of the data flow critical path
// (the path from %GEPIOp to %UGEPI would go through %GEPI), which is a tradeoff
// between the register pressure and the length of data-flow critical
// path. Restricting this to the uncommon IndirectBr case would minimize the
// impact of potentially longer critical path, if any, and the impact on compile
// time.
static bool tryUnmergingGEPsAcrossIndirectBr(GetElementPtrInst *GEPI,
                                             const TargetTransformInfo *TTI) {
  BasicBlock *SrcBlock = GEPI->getParent();
  // Check that SrcBlock ends with an IndirectBr. If not, give up. The common
  // (non-IndirectBr) cases exit early here.
  if (!isa<IndirectBrInst>(SrcBlock->getTerminator()))
    return false;
  // Check that GEPI is a simple gep with a single constant index.
  if (!GEPSequentialConstIndexed(GEPI))
    return false;
  ConstantInt *GEPIIdx = cast<ConstantInt>(GEPI->getOperand(1));
  // Check that GEPI is a cheap one.
  if (TTI->getIntImmCost(GEPIIdx->getValue(), GEPIIdx->getType(),
                         TargetTransformInfo::TCK_SizeAndLatency) >
      TargetTransformInfo::TCC_Basic)
    return false;
  Value *GEPIOp = GEPI->getOperand(0);
  // Check that GEPIOp is an instruction that's also defined in SrcBlock.
  if (!isa<Instruction>(GEPIOp))
    return false;
  auto *GEPIOpI = cast<Instruction>(GEPIOp);
  if (GEPIOpI->getParent() != SrcBlock)
    return false;
  // Check that GEP is used outside the block, meaning it's alive on the
  // IndirectBr edge(s).
  if (llvm::none_of(GEPI->users(), [&](User *Usr) {
        if (auto *I = dyn_cast<Instruction>(Usr)) {
          if (I->getParent() != SrcBlock) {
            return true;
          }
        }
        return false;
      }))
    return false;
  // The second elements of the GEP chains to be unmerged.
  std::vector<GetElementPtrInst *> UGEPIs;
  // Check each user of GEPIOp to check if unmerging would make GEPIOp not alive
  // on IndirectBr edges.
  for (User *Usr : GEPIOp->users()) {
    if (Usr == GEPI)
      continue;
    // Check if Usr is an Instruction. If not, give up.
    if (!isa<Instruction>(Usr))
      return false;
    auto *UI = cast<Instruction>(Usr);
    // Check if Usr in the same block as GEPIOp, which is fine, skip.
    if (UI->getParent() == SrcBlock)
      continue;
    // Check if Usr is a GEP. If not, give up.
    if (!isa<GetElementPtrInst>(Usr))
      return false;
    auto *UGEPI = cast<GetElementPtrInst>(Usr);
    // Check if UGEPI is a simple gep with a single constant index and GEPIOp is
    // the pointer operand to it. If so, record it in the vector. If not, give
    // up.
    if (!GEPSequentialConstIndexed(UGEPI))
      return false;
    if (UGEPI->getOperand(0) != GEPIOp)
      return false;
    if (UGEPI->getSourceElementType() != GEPI->getSourceElementType())
      return false;
    if (GEPIIdx->getType() !=
        cast<ConstantInt>(UGEPI->getOperand(1))->getType())
      return false;
    ConstantInt *UGEPIIdx = cast<ConstantInt>(UGEPI->getOperand(1));
    if (TTI->getIntImmCost(UGEPIIdx->getValue(), UGEPIIdx->getType(),
                           TargetTransformInfo::TCK_SizeAndLatency) >
        TargetTransformInfo::TCC_Basic)
      return false;
    UGEPIs.push_back(UGEPI);
  }
  if (UGEPIs.size() == 0)
    return false;
  // Check the materializing cost of (Uidx-Idx).
  for (GetElementPtrInst *UGEPI : UGEPIs) {
    ConstantInt *UGEPIIdx = cast<ConstantInt>(UGEPI->getOperand(1));
    APInt NewIdx = UGEPIIdx->getValue() - GEPIIdx->getValue();
    InstructionCost ImmCost = TTI->getIntImmCost(
        NewIdx, GEPIIdx->getType(), TargetTransformInfo::TCK_SizeAndLatency);
    if (ImmCost > TargetTransformInfo::TCC_Basic)
      return false;
  }
  // Now unmerge between GEPI and UGEPIs.
  for (GetElementPtrInst *UGEPI : UGEPIs) {
    UGEPI->setOperand(0, GEPI);
    ConstantInt *UGEPIIdx = cast<ConstantInt>(UGEPI->getOperand(1));
    Constant *NewUGEPIIdx = ConstantInt::get(
        GEPIIdx->getType(), UGEPIIdx->getValue() - GEPIIdx->getValue());
    UGEPI->setOperand(1, NewUGEPIIdx);
    // If GEPI is not inbounds but UGEPI is inbounds, change UGEPI to not
    // inbounds to avoid UB.
    if (!GEPI->isInBounds()) {
      UGEPI->setIsInBounds(false);
    }
  }
  // After unmerging, verify that GEPIOp is actually only used in SrcBlock (not
  // alive on IndirectBr edges).
  assert(llvm::none_of(GEPIOp->users(),
                       [&](User *Usr) {
                         return cast<Instruction>(Usr)->getParent() != SrcBlock;
                       }) &&
         "GEPIOp is used outside SrcBlock");
  return true;
}

static bool optimizeBranch(BranchInst *Branch, const TargetLowering &TLI,
                           SmallSet<BasicBlock *, 32> &FreshBBs,
                           bool IsHugeFunc) {
  // Try and convert
  //  %c = icmp ult %x, 8
  //  br %c, bla, blb
  //  %tc = lshr %x, 3
  // to
  //  %tc = lshr %x, 3
  //  %c = icmp eq %tc, 0
  //  br %c, bla, blb
  // Creating the cmp to zero can be better for the backend, especially if the
  // lshr produces flags that can be used automatically.
  if (!TLI.preferZeroCompareBranch() || !Branch->isConditional())
    return false;

  ICmpInst *Cmp = dyn_cast<ICmpInst>(Branch->getCondition());
  if (!Cmp || !isa<ConstantInt>(Cmp->getOperand(1)) || !Cmp->hasOneUse())
    return false;

  Value *X = Cmp->getOperand(0);
  APInt CmpC = cast<ConstantInt>(Cmp->getOperand(1))->getValue();

  for (auto *U : X->users()) {
    Instruction *UI = dyn_cast<Instruction>(U);
    // A quick dominance check
    if (!UI ||
        (UI->getParent() != Branch->getParent() &&
         UI->getParent() != Branch->getSuccessor(0) &&
         UI->getParent() != Branch->getSuccessor(1)) ||
        (UI->getParent() != Branch->getParent() &&
         !UI->getParent()->getSinglePredecessor()))
      continue;

    if (CmpC.isPowerOf2() && Cmp->getPredicate() == ICmpInst::ICMP_ULT &&
        match(UI, m_Shr(m_Specific(X), m_SpecificInt(CmpC.logBase2())))) {
      IRBuilder<> Builder(Branch);
      if (UI->getParent() != Branch->getParent())
        UI->moveBefore(Branch);
      UI->dropPoisonGeneratingFlags();
      Value *NewCmp = Builder.CreateCmp(ICmpInst::ICMP_EQ, UI,
                                        ConstantInt::get(UI->getType(), 0));
      LLVM_DEBUG(dbgs() << "Converting " << *Cmp << "\n");
      LLVM_DEBUG(dbgs() << " to compare on zero: " << *NewCmp << "\n");
      replaceAllUsesWith(Cmp, NewCmp, FreshBBs, IsHugeFunc);
      return true;
    }
    if (Cmp->isEquality() &&
        (match(UI, m_Add(m_Specific(X), m_SpecificInt(-CmpC))) ||
         match(UI, m_Sub(m_Specific(X), m_SpecificInt(CmpC))))) {
      IRBuilder<> Builder(Branch);
      if (UI->getParent() != Branch->getParent())
        UI->moveBefore(Branch);
      UI->dropPoisonGeneratingFlags();
      Value *NewCmp = Builder.CreateCmp(Cmp->getPredicate(), UI,
                                        ConstantInt::get(UI->getType(), 0));
      LLVM_DEBUG(dbgs() << "Converting " << *Cmp << "\n");
      LLVM_DEBUG(dbgs() << " to compare on zero: " << *NewCmp << "\n");
      replaceAllUsesWith(Cmp, NewCmp, FreshBBs, IsHugeFunc);
      return true;
    }
  }
  return false;
}

bool CodeGenPrepare::optimizeInst(Instruction *I, ModifyDT &ModifiedDT) {
  bool AnyChange = false;
  AnyChange = fixupDbgVariableRecordsOnInst(*I);

  // Bail out if we inserted the instruction to prevent optimizations from
  // stepping on each other's toes.
  if (InsertedInsts.count(I))
    return AnyChange;

  // TODO: Move into the switch on opcode below here.
  if (PHINode *P = dyn_cast<PHINode>(I)) {
    // It is possible for very late stage optimizations (such as SimplifyCFG)
    // to introduce PHI nodes too late to be cleaned up.  If we detect such a
    // trivial PHI, go ahead and zap it here.
    if (Value *V = simplifyInstruction(P, {*DL, TLInfo})) {
      LargeOffsetGEPMap.erase(P);
      replaceAllUsesWith(P, V, FreshBBs, IsHugeFunc);
      P->eraseFromParent();
      ++NumPHIsElim;
      return true;
    }
    return AnyChange;
  }

  if (CastInst *CI = dyn_cast<CastInst>(I)) {
    // If the source of the cast is a constant, then this should have
    // already been constant folded.  The only reason NOT to constant fold
    // it is if something (e.g. LSR) was careful to place the constant
    // evaluation in a block other than then one that uses it (e.g. to hoist
    // the address of globals out of a loop).  If this is the case, we don't
    // want to forward-subst the cast.
    if (isa<Constant>(CI->getOperand(0)))
      return AnyChange;

    if (OptimizeNoopCopyExpression(CI, *TLI, *DL))
      return true;

    if ((isa<UIToFPInst>(I) || isa<SIToFPInst>(I) || isa<FPToUIInst>(I) ||
         isa<TruncInst>(I)) &&
        TLI->optimizeExtendOrTruncateConversion(
            I, LI->getLoopFor(I->getParent()), *TTI))
      return true;

    if (isa<ZExtInst>(I) || isa<SExtInst>(I)) {
      /// Sink a zext or sext into its user blocks if the target type doesn't
      /// fit in one register
      if (TLI->getTypeAction(CI->getContext(),
                             TLI->getValueType(*DL, CI->getType())) ==
          TargetLowering::TypeExpandInteger) {
        return SinkCast(CI);
      } else {
        if (TLI->optimizeExtendOrTruncateConversion(
                I, LI->getLoopFor(I->getParent()), *TTI))
          return true;

        bool MadeChange = optimizeExt(I);
        return MadeChange | optimizeExtUses(I);
      }
    }
    return AnyChange;
  }

  if (auto *Cmp = dyn_cast<CmpInst>(I))
    if (optimizeCmp(Cmp, ModifiedDT))
      return true;

  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    LI->setMetadata(LLVMContext::MD_invariant_group, nullptr);
    bool Modified = optimizeLoadExt(LI);
    unsigned AS = LI->getPointerAddressSpace();
    Modified |= optimizeMemoryInst(I, I->getOperand(0), LI->getType(), AS);
    return Modified;
  }

  if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    if (splitMergedValStore(*SI, *DL, *TLI))
      return true;
    SI->setMetadata(LLVMContext::MD_invariant_group, nullptr);
    unsigned AS = SI->getPointerAddressSpace();
    return optimizeMemoryInst(I, SI->getOperand(1),
                              SI->getOperand(0)->getType(), AS);
  }

  if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    unsigned AS = RMW->getPointerAddressSpace();
    return optimizeMemoryInst(I, RMW->getPointerOperand(), RMW->getType(), AS);
  }

  if (AtomicCmpXchgInst *CmpX = dyn_cast<AtomicCmpXchgInst>(I)) {
    unsigned AS = CmpX->getPointerAddressSpace();
    return optimizeMemoryInst(I, CmpX->getPointerOperand(),
                              CmpX->getCompareOperand()->getType(), AS);
  }

  BinaryOperator *BinOp = dyn_cast<BinaryOperator>(I);

  if (BinOp && BinOp->getOpcode() == Instruction::And && EnableAndCmpSinking &&
      sinkAndCmp0Expression(BinOp, *TLI, InsertedInsts))
    return true;

  // TODO: Move this into the switch on opcode - it handles shifts already.
  if (BinOp && (BinOp->getOpcode() == Instruction::AShr ||
                BinOp->getOpcode() == Instruction::LShr)) {
    ConstantInt *CI = dyn_cast<ConstantInt>(BinOp->getOperand(1));
    if (CI && TLI->hasExtractBitsInsn())
      if (OptimizeExtractBits(BinOp, CI, *TLI, *DL))
        return true;
  }

  if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I)) {
    if (GEPI->hasAllZeroIndices()) {
      /// The GEP operand must be a pointer, so must its result -> BitCast
      Instruction *NC = new BitCastInst(GEPI->getOperand(0), GEPI->getType(),
                                        GEPI->getName(), GEPI->getIterator());
      NC->setDebugLoc(GEPI->getDebugLoc());
      replaceAllUsesWith(GEPI, NC, FreshBBs, IsHugeFunc);
      RecursivelyDeleteTriviallyDeadInstructions(
          GEPI, TLInfo, nullptr,
          [&](Value *V) { removeAllAssertingVHReferences(V); });
      ++NumGEPsElim;
      optimizeInst(NC, ModifiedDT);
      return true;
    }
    if (tryUnmergingGEPsAcrossIndirectBr(GEPI, TTI)) {
      return true;
    }
  }

  if (FreezeInst *FI = dyn_cast<FreezeInst>(I)) {
    // freeze(icmp a, const)) -> icmp (freeze a), const
    // This helps generate efficient conditional jumps.
    Instruction *CmpI = nullptr;
    if (ICmpInst *II = dyn_cast<ICmpInst>(FI->getOperand(0)))
      CmpI = II;
    else if (FCmpInst *F = dyn_cast<FCmpInst>(FI->getOperand(0)))
      CmpI = F->getFastMathFlags().none() ? F : nullptr;

    if (CmpI && CmpI->hasOneUse()) {
      auto Op0 = CmpI->getOperand(0), Op1 = CmpI->getOperand(1);
      bool Const0 = isa<ConstantInt>(Op0) || isa<ConstantFP>(Op0) ||
                    isa<ConstantPointerNull>(Op0);
      bool Const1 = isa<ConstantInt>(Op1) || isa<ConstantFP>(Op1) ||
                    isa<ConstantPointerNull>(Op1);
      if (Const0 || Const1) {
        if (!Const0 || !Const1) {
          auto *F = new FreezeInst(Const0 ? Op1 : Op0, "", CmpI->getIterator());
          F->takeName(FI);
          CmpI->setOperand(Const0 ? 1 : 0, F);
        }
        replaceAllUsesWith(FI, CmpI, FreshBBs, IsHugeFunc);
        FI->eraseFromParent();
        return true;
      }
    }
    return AnyChange;
  }

  if (tryToSinkFreeOperands(I))
    return true;

  switch (I->getOpcode()) {
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    return optimizeShiftInst(cast<BinaryOperator>(I));
  case Instruction::Call:
    return optimizeCallInst(cast<CallInst>(I), ModifiedDT);
  case Instruction::Select:
    return optimizeSelectInst(cast<SelectInst>(I));
  case Instruction::ShuffleVector:
    return optimizeShuffleVectorInst(cast<ShuffleVectorInst>(I));
  case Instruction::Switch:
    return optimizeSwitchInst(cast<SwitchInst>(I));
  case Instruction::ExtractElement:
    return optimizeExtractElementInst(cast<ExtractElementInst>(I));
  case Instruction::Br:
    return optimizeBranch(cast<BranchInst>(I), *TLI, FreshBBs, IsHugeFunc);
  }

  return AnyChange;
}

/// Given an OR instruction, check to see if this is a bitreverse
/// idiom. If so, insert the new intrinsic and return true.
bool CodeGenPrepare::makeBitReverse(Instruction &I) {
  if (!I.getType()->isIntegerTy() ||
      !TLI->isOperationLegalOrCustom(ISD::BITREVERSE,
                                     TLI->getValueType(*DL, I.getType(), true)))
    return false;

  SmallVector<Instruction *, 4> Insts;
  if (!recognizeBSwapOrBitReverseIdiom(&I, false, true, Insts))
    return false;
  Instruction *LastInst = Insts.back();
  replaceAllUsesWith(&I, LastInst, FreshBBs, IsHugeFunc);
  RecursivelyDeleteTriviallyDeadInstructions(
      &I, TLInfo, nullptr,
      [&](Value *V) { removeAllAssertingVHReferences(V); });
  return true;
}

// In this pass we look for GEP and cast instructions that are used
// across basic blocks and rewrite them to improve basic-block-at-a-time
// selection.
bool CodeGenPrepare::optimizeBlock(BasicBlock &BB, ModifyDT &ModifiedDT) {
  SunkAddrs.clear();
  bool MadeChange = false;

  do {
    CurInstIterator = BB.begin();
    ModifiedDT = ModifyDT::NotModifyDT;
    while (CurInstIterator != BB.end()) {
      MadeChange |= optimizeInst(&*CurInstIterator++, ModifiedDT);
      if (ModifiedDT != ModifyDT::NotModifyDT) {
        // For huge function we tend to quickly go though the inner optmization
        // opportunities in the BB. So we go back to the BB head to re-optimize
        // each instruction instead of go back to the function head.
        if (IsHugeFunc) {
          DT.reset();
          getDT(*BB.getParent());
          break;
        } else {
          return true;
        }
      }
    }
  } while (ModifiedDT == ModifyDT::ModifyInstDT);

  bool MadeBitReverse = true;
  while (MadeBitReverse) {
    MadeBitReverse = false;
    for (auto &I : reverse(BB)) {
      if (makeBitReverse(I)) {
        MadeBitReverse = MadeChange = true;
        break;
      }
    }
  }
  MadeChange |= dupRetToEnableTailCallOpts(&BB, ModifiedDT);

  return MadeChange;
}

// Some CGP optimizations may move or alter what's computed in a block. Check
// whether a dbg.value intrinsic could be pointed at a more appropriate operand.
bool CodeGenPrepare::fixupDbgValue(Instruction *I) {
  assert(isa<DbgValueInst>(I));
  DbgValueInst &DVI = *cast<DbgValueInst>(I);

  // Does this dbg.value refer to a sunk address calculation?
  bool AnyChange = false;
  SmallDenseSet<Value *> LocationOps(DVI.location_ops().begin(),
                                     DVI.location_ops().end());
  for (Value *Location : LocationOps) {
    WeakTrackingVH SunkAddrVH = SunkAddrs[Location];
    Value *SunkAddr = SunkAddrVH.pointsToAliveValue() ? SunkAddrVH : nullptr;
    if (SunkAddr) {
      // Point dbg.value at locally computed address, which should give the best
      // opportunity to be accurately lowered. This update may change the type
      // of pointer being referred to; however this makes no difference to
      // debugging information, and we can't generate bitcasts that may affect
      // codegen.
      DVI.replaceVariableLocationOp(Location, SunkAddr);
      AnyChange = true;
    }
  }
  return AnyChange;
}

bool CodeGenPrepare::fixupDbgVariableRecordsOnInst(Instruction &I) {
  bool AnyChange = false;
  for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
    AnyChange |= fixupDbgVariableRecord(DVR);
  return AnyChange;
}

// FIXME: should updating debug-info really cause the "changed" flag to fire,
// which can cause a function to be reprocessed?
bool CodeGenPrepare::fixupDbgVariableRecord(DbgVariableRecord &DVR) {
  if (DVR.Type != DbgVariableRecord::LocationType::Value &&
      DVR.Type != DbgVariableRecord::LocationType::Assign)
    return false;

  // Does this DbgVariableRecord refer to a sunk address calculation?
  bool AnyChange = false;
  SmallDenseSet<Value *> LocationOps(DVR.location_ops().begin(),
                                     DVR.location_ops().end());
  for (Value *Location : LocationOps) {
    WeakTrackingVH SunkAddrVH = SunkAddrs[Location];
    Value *SunkAddr = SunkAddrVH.pointsToAliveValue() ? SunkAddrVH : nullptr;
    if (SunkAddr) {
      // Point dbg.value at locally computed address, which should give the best
      // opportunity to be accurately lowered. This update may change the type
      // of pointer being referred to; however this makes no difference to
      // debugging information, and we can't generate bitcasts that may affect
      // codegen.
      DVR.replaceVariableLocationOp(Location, SunkAddr);
      AnyChange = true;
    }
  }
  return AnyChange;
}

static void DbgInserterHelper(DbgValueInst *DVI, Instruction *VI) {
  DVI->removeFromParent();
  if (isa<PHINode>(VI))
    DVI->insertBefore(&*VI->getParent()->getFirstInsertionPt());
  else
    DVI->insertAfter(VI);
}

static void DbgInserterHelper(DbgVariableRecord *DVR, Instruction *VI) {
  DVR->removeFromParent();
  BasicBlock *VIBB = VI->getParent();
  if (isa<PHINode>(VI))
    VIBB->insertDbgRecordBefore(DVR, VIBB->getFirstInsertionPt());
  else
    VIBB->insertDbgRecordAfter(DVR, VI);
}

// A llvm.dbg.value may be using a value before its definition, due to
// optimizations in this pass and others. Scan for such dbg.values, and rescue
// them by moving the dbg.value to immediately after the value definition.
// FIXME: Ideally this should never be necessary, and this has the potential
// to re-order dbg.value intrinsics.
bool CodeGenPrepare::placeDbgValues(Function &F) {
  bool MadeChange = false;
  DominatorTree DT(F);

  auto DbgProcessor = [&](auto *DbgItem, Instruction *Position) {
    SmallVector<Instruction *, 4> VIs;
    for (Value *V : DbgItem->location_ops())
      if (Instruction *VI = dyn_cast_or_null<Instruction>(V))
        VIs.push_back(VI);

    // This item may depend on multiple instructions, complicating any
    // potential sink. This block takes the defensive approach, opting to
    // "undef" the item if it has more than one instruction and any of them do
    // not dominate iem.
    for (Instruction *VI : VIs) {
      if (VI->isTerminator())
        continue;

      // If VI is a phi in a block with an EHPad terminator, we can't insert
      // after it.
      if (isa<PHINode>(VI) && VI->getParent()->getTerminator()->isEHPad())
        continue;

      // If the defining instruction dominates the dbg.value, we do not need
      // to move the dbg.value.
      if (DT.dominates(VI, Position))
        continue;

      // If we depend on multiple instructions and any of them doesn't
      // dominate this DVI, we probably can't salvage it: moving it to
      // after any of the instructions could cause us to lose the others.
      if (VIs.size() > 1) {
        LLVM_DEBUG(
            dbgs()
            << "Unable to find valid location for Debug Value, undefing:\n"
            << *DbgItem);
        DbgItem->setKillLocation();
        break;
      }

      LLVM_DEBUG(dbgs() << "Moving Debug Value before :\n"
                        << *DbgItem << ' ' << *VI);
      DbgInserterHelper(DbgItem, VI);
      MadeChange = true;
      ++NumDbgValueMoved;
    }
  };

  for (BasicBlock &BB : F) {
    for (Instruction &Insn : llvm::make_early_inc_range(BB)) {
      // Process dbg.value intrinsics.
      DbgValueInst *DVI = dyn_cast<DbgValueInst>(&Insn);
      if (DVI) {
        DbgProcessor(DVI, DVI);
        continue;
      }

      // If this isn't a dbg.value, process any attached DbgVariableRecord
      // records attached to this instruction.
      for (DbgVariableRecord &DVR : llvm::make_early_inc_range(
               filterDbgVars(Insn.getDbgRecordRange()))) {
        if (DVR.Type != DbgVariableRecord::LocationType::Value)
          continue;
        DbgProcessor(&DVR, &Insn);
      }
    }
  }

  return MadeChange;
}

// Group scattered pseudo probes in a block to favor SelectionDAG. Scattered
// probes can be chained dependencies of other regular DAG nodes and block DAG
// combine optimizations.
bool CodeGenPrepare::placePseudoProbes(Function &F) {
  bool MadeChange = false;
  for (auto &Block : F) {
    // Move the rest probes to the beginning of the block.
    auto FirstInst = Block.getFirstInsertionPt();
    while (FirstInst != Block.end() && FirstInst->isDebugOrPseudoInst())
      ++FirstInst;
    BasicBlock::iterator I(FirstInst);
    I++;
    while (I != Block.end()) {
      if (auto *II = dyn_cast<PseudoProbeInst>(I++)) {
        II->moveBefore(&*FirstInst);
        MadeChange = true;
      }
    }
  }
  return MadeChange;
}

/// Scale down both weights to fit into uint32_t.
static void scaleWeights(uint64_t &NewTrue, uint64_t &NewFalse) {
  uint64_t NewMax = (NewTrue > NewFalse) ? NewTrue : NewFalse;
  uint32_t Scale = (NewMax / std::numeric_limits<uint32_t>::max()) + 1;
  NewTrue = NewTrue / Scale;
  NewFalse = NewFalse / Scale;
}

/// Some targets prefer to split a conditional branch like:
/// \code
///   %0 = icmp ne i32 %a, 0
///   %1 = icmp ne i32 %b, 0
///   %or.cond = or i1 %0, %1
///   br i1 %or.cond, label %TrueBB, label %FalseBB
/// \endcode
/// into multiple branch instructions like:
/// \code
///   bb1:
///     %0 = icmp ne i32 %a, 0
///     br i1 %0, label %TrueBB, label %bb2
///   bb2:
///     %1 = icmp ne i32 %b, 0
///     br i1 %1, label %TrueBB, label %FalseBB
/// \endcode
/// This usually allows instruction selection to do even further optimizations
/// and combine the compare with the branch instruction. Currently this is
/// applied for targets which have "cheap" jump instructions.
///
/// FIXME: Remove the (equivalent?) implementation in SelectionDAG.
///
bool CodeGenPrepare::splitBranchCondition(Function &F, ModifyDT &ModifiedDT) {
  if (!TM->Options.EnableFastISel || TLI->isJumpExpensive())
    return false;

  bool MadeChange = false;
  for (auto &BB : F) {
    // Does this BB end with the following?
    //   %cond1 = icmp|fcmp|binary instruction ...
    //   %cond2 = icmp|fcmp|binary instruction ...
    //   %cond.or = or|and i1 %cond1, cond2
    //   br i1 %cond.or label %dest1, label %dest2"
    Instruction *LogicOp;
    BasicBlock *TBB, *FBB;
    if (!match(BB.getTerminator(),
               m_Br(m_OneUse(m_Instruction(LogicOp)), TBB, FBB)))
      continue;

    auto *Br1 = cast<BranchInst>(BB.getTerminator());
    if (Br1->getMetadata(LLVMContext::MD_unpredictable))
      continue;

    // The merging of mostly empty BB can cause a degenerate branch.
    if (TBB == FBB)
      continue;

    unsigned Opc;
    Value *Cond1, *Cond2;
    if (match(LogicOp,
              m_LogicalAnd(m_OneUse(m_Value(Cond1)), m_OneUse(m_Value(Cond2)))))
      Opc = Instruction::And;
    else if (match(LogicOp, m_LogicalOr(m_OneUse(m_Value(Cond1)),
                                        m_OneUse(m_Value(Cond2)))))
      Opc = Instruction::Or;
    else
      continue;

    auto IsGoodCond = [](Value *Cond) {
      return match(
          Cond,
          m_CombineOr(m_Cmp(), m_CombineOr(m_LogicalAnd(m_Value(), m_Value()),
                                           m_LogicalOr(m_Value(), m_Value()))));
    };
    if (!IsGoodCond(Cond1) || !IsGoodCond(Cond2))
      continue;

    LLVM_DEBUG(dbgs() << "Before branch condition splitting\n"; BB.dump());

    // Create a new BB.
    auto *TmpBB =
        BasicBlock::Create(BB.getContext(), BB.getName() + ".cond.split",
                           BB.getParent(), BB.getNextNode());
    if (IsHugeFunc)
      FreshBBs.insert(TmpBB);

    // Update original basic block by using the first condition directly by the
    // branch instruction and removing the no longer needed and/or instruction.
    Br1->setCondition(Cond1);
    LogicOp->eraseFromParent();

    // Depending on the condition we have to either replace the true or the
    // false successor of the original branch instruction.
    if (Opc == Instruction::And)
      Br1->setSuccessor(0, TmpBB);
    else
      Br1->setSuccessor(1, TmpBB);

    // Fill in the new basic block.
    auto *Br2 = IRBuilder<>(TmpBB).CreateCondBr(Cond2, TBB, FBB);
    if (auto *I = dyn_cast<Instruction>(Cond2)) {
      I->removeFromParent();
      I->insertBefore(Br2);
    }

    // Update PHI nodes in both successors. The original BB needs to be
    // replaced in one successor's PHI nodes, because the branch comes now from
    // the newly generated BB (NewBB). In the other successor we need to add one
    // incoming edge to the PHI nodes, because both branch instructions target
    // now the same successor. Depending on the original branch condition
    // (and/or) we have to swap the successors (TrueDest, FalseDest), so that
    // we perform the correct update for the PHI nodes.
    // This doesn't change the successor order of the just created branch
    // instruction (or any other instruction).
    if (Opc == Instruction::Or)
      std::swap(TBB, FBB);

    // Replace the old BB with the new BB.
    TBB->replacePhiUsesWith(&BB, TmpBB);

    // Add another incoming edge from the new BB.
    for (PHINode &PN : FBB->phis()) {
      auto *Val = PN.getIncomingValueForBlock(&BB);
      PN.addIncoming(Val, TmpBB);
    }

    // Update the branch weights (from SelectionDAGBuilder::
    // FindMergedConditions).
    if (Opc == Instruction::Or) {
      // Codegen X | Y as:
      // BB1:
      //   jmp_if_X TBB
      //   jmp TmpBB
      // TmpBB:
      //   jmp_if_Y TBB
      //   jmp FBB
      //

      // We have flexibility in setting Prob for BB1 and Prob for NewBB.
      // The requirement is that
      //   TrueProb for BB1 + (FalseProb for BB1 * TrueProb for TmpBB)
      //     = TrueProb for original BB.
      // Assuming the original weights are A and B, one choice is to set BB1's
      // weights to A and A+2B, and set TmpBB's weights to A and 2B. This choice
      // assumes that
      //   TrueProb for BB1 == FalseProb for BB1 * TrueProb for TmpBB.
      // Another choice is to assume TrueProb for BB1 equals to TrueProb for
      // TmpBB, but the math is more complicated.
      uint64_t TrueWeight, FalseWeight;
      if (extractBranchWeights(*Br1, TrueWeight, FalseWeight)) {
        uint64_t NewTrueWeight = TrueWeight;
        uint64_t NewFalseWeight = TrueWeight + 2 * FalseWeight;
        scaleWeights(NewTrueWeight, NewFalseWeight);
        Br1->setMetadata(LLVMContext::MD_prof,
                         MDBuilder(Br1->getContext())
                             .createBranchWeights(TrueWeight, FalseWeight,
                                                  hasBranchWeightOrigin(*Br1)));

        NewTrueWeight = TrueWeight;
        NewFalseWeight = 2 * FalseWeight;
        scaleWeights(NewTrueWeight, NewFalseWeight);
        Br2->setMetadata(LLVMContext::MD_prof,
                         MDBuilder(Br2->getContext())
                             .createBranchWeights(TrueWeight, FalseWeight));
      }
    } else {
      // Codegen X & Y as:
      // BB1:
      //   jmp_if_X TmpBB
      //   jmp FBB
      // TmpBB:
      //   jmp_if_Y TBB
      //   jmp FBB
      //
      //  This requires creation of TmpBB after CurBB.

      // We have flexibility in setting Prob for BB1 and Prob for TmpBB.
      // The requirement is that
      //   FalseProb for BB1 + (TrueProb for BB1 * FalseProb for TmpBB)
      //     = FalseProb for original BB.
      // Assuming the original weights are A and B, one choice is to set BB1's
      // weights to 2A+B and B, and set TmpBB's weights to 2A and B. This choice
      // assumes that
      //   FalseProb for BB1 == TrueProb for BB1 * FalseProb for TmpBB.
      uint64_t TrueWeight, FalseWeight;
      if (extractBranchWeights(*Br1, TrueWeight, FalseWeight)) {
        uint64_t NewTrueWeight = 2 * TrueWeight + FalseWeight;
        uint64_t NewFalseWeight = FalseWeight;
        scaleWeights(NewTrueWeight, NewFalseWeight);
        Br1->setMetadata(LLVMContext::MD_prof,
                         MDBuilder(Br1->getContext())
                             .createBranchWeights(TrueWeight, FalseWeight));

        NewTrueWeight = 2 * TrueWeight;
        NewFalseWeight = FalseWeight;
        scaleWeights(NewTrueWeight, NewFalseWeight);
        Br2->setMetadata(LLVMContext::MD_prof,
                         MDBuilder(Br2->getContext())
                             .createBranchWeights(TrueWeight, FalseWeight));
      }
    }

    ModifiedDT = ModifyDT::ModifyBBDT;
    MadeChange = true;

    LLVM_DEBUG(dbgs() << "After branch condition splitting\n"; BB.dump();
               TmpBB->dump());
  }
  return MadeChange;
}
