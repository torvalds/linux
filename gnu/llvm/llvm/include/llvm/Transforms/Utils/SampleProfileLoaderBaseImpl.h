////===- SampleProfileLoadBaseImpl.h - Profile loader base impl --*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for the sampled PGO profile loader base
/// implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEIMPL_H
#define LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEIMPL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/ProfileData/SampleProfReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/SampleProfileInference.h"
#include "llvm/Transforms/Utils/SampleProfileLoaderBaseUtil.h"

namespace llvm {
using namespace sampleprof;
using namespace sampleprofutil;
using ProfileCount = Function::ProfileCount;

namespace vfs {
class FileSystem;
} // namespace vfs

#define DEBUG_TYPE "sample-profile-impl"

namespace afdo_detail {

template <typename BlockT> struct IRTraits;
template <> struct IRTraits<BasicBlock> {
  using InstructionT = Instruction;
  using BasicBlockT = BasicBlock;
  using FunctionT = Function;
  using BlockFrequencyInfoT = BlockFrequencyInfo;
  using LoopT = Loop;
  using LoopInfoPtrT = std::unique_ptr<LoopInfo>;
  using DominatorTreePtrT = std::unique_ptr<DominatorTree>;
  using PostDominatorTreeT = PostDominatorTree;
  using PostDominatorTreePtrT = std::unique_ptr<PostDominatorTree>;
  using OptRemarkEmitterT = OptimizationRemarkEmitter;
  using OptRemarkAnalysisT = OptimizationRemarkAnalysis;
  using PredRangeT = pred_range;
  using SuccRangeT = succ_range;
  static Function &getFunction(Function &F) { return F; }
  static const BasicBlock *getEntryBB(const Function *F) {
    return &F->getEntryBlock();
  }
  static pred_range getPredecessors(BasicBlock *BB) { return predecessors(BB); }
  static succ_range getSuccessors(BasicBlock *BB) { return successors(BB); }
};

} // end namespace afdo_detail

// This class serves sample counts correlation for SampleProfileLoader by
// analyzing pseudo probes and their function descriptors injected by
// SampleProfileProber.
class PseudoProbeManager {
  DenseMap<uint64_t, PseudoProbeDescriptor> GUIDToProbeDescMap;

public:
  PseudoProbeManager(const Module &M) {
    if (NamedMDNode *FuncInfo =
            M.getNamedMetadata(PseudoProbeDescMetadataName)) {
      for (const auto *Operand : FuncInfo->operands()) {
        const auto *MD = cast<MDNode>(Operand);
        auto GUID = mdconst::dyn_extract<ConstantInt>(MD->getOperand(0))
                        ->getZExtValue();
        auto Hash = mdconst::dyn_extract<ConstantInt>(MD->getOperand(1))
                        ->getZExtValue();
        GUIDToProbeDescMap.try_emplace(GUID, PseudoProbeDescriptor(GUID, Hash));
      }
    }
  }

  const PseudoProbeDescriptor *getDesc(uint64_t GUID) const {
    auto I = GUIDToProbeDescMap.find(GUID);
    return I == GUIDToProbeDescMap.end() ? nullptr : &I->second;
  }

  const PseudoProbeDescriptor *getDesc(StringRef FProfileName) const {
    return getDesc(Function::getGUID(FProfileName));
  }

  const PseudoProbeDescriptor *getDesc(const Function &F) const {
    return getDesc(Function::getGUID(FunctionSamples::getCanonicalFnName(F)));
  }

  bool profileIsHashMismatched(const PseudoProbeDescriptor &FuncDesc,
                               const FunctionSamples &Samples) const {
    return FuncDesc.getFunctionHash() != Samples.getFunctionHash();
  }

  bool moduleIsProbed(const Module &M) const {
    return M.getNamedMetadata(PseudoProbeDescMetadataName);
  }

  bool profileIsValid(const Function &F, const FunctionSamples &Samples) const {
    const auto *Desc = getDesc(F);
    bool IsAvailableExternallyLinkage =
        GlobalValue::isAvailableExternallyLinkage(F.getLinkage());
    // Always check the function attribute to determine checksum mismatch for
    // `available_externally` functions even if their desc are available. This
    // is because the desc is computed based on the original internal function
    // and it's substituted by the `available_externally` function during link
    // time. However, when unstable IR or ODR violation issue occurs, the
    // definitions of the same function across different translation units could
    // be different and result in different checksums. So we should use the
    // state from the new (available_externally) function, which is saved in its
    // attribute.
    // TODO: If the function's profile only exists as nested inlinee profile in
    // a different module, we don't have the attr mismatch state(unknown), we
    // need to fix it later.
    if (IsAvailableExternallyLinkage || !Desc)
      return !F.hasFnAttribute("profile-checksum-mismatch");

    return Desc && !profileIsHashMismatched(*Desc, Samples);
  }
};



extern cl::opt<bool> SampleProfileUseProfi;

static inline bool skipProfileForFunction(const Function &F) {
  return F.isDeclaration() || !F.hasFnAttribute("use-sample-profile");
}

static inline void
buildTopDownFuncOrder(LazyCallGraph &CG,
                      std::vector<Function *> &FunctionOrderList) {
  CG.buildRefSCCs();
  for (LazyCallGraph::RefSCC &RC : CG.postorder_ref_sccs()) {
    for (LazyCallGraph::SCC &C : RC) {
      for (LazyCallGraph::Node &N : C) {
        Function &F = N.getFunction();
        if (!skipProfileForFunction(F))
          FunctionOrderList.push_back(&F);
      }
    }
  }
  std::reverse(FunctionOrderList.begin(), FunctionOrderList.end());
}

template <typename FT> class SampleProfileLoaderBaseImpl {
public:
  SampleProfileLoaderBaseImpl(std::string Name, std::string RemapName,
                              IntrusiveRefCntPtr<vfs::FileSystem> FS)
      : Filename(Name), RemappingFilename(RemapName), FS(std::move(FS)) {}
  void dump() { Reader->dump(); }

  using NodeRef = typename GraphTraits<FT *>::NodeRef;
  using BT = std::remove_pointer_t<NodeRef>;
  using InstructionT = typename afdo_detail::IRTraits<BT>::InstructionT;
  using BasicBlockT = typename afdo_detail::IRTraits<BT>::BasicBlockT;
  using BlockFrequencyInfoT =
      typename afdo_detail::IRTraits<BT>::BlockFrequencyInfoT;
  using FunctionT = typename afdo_detail::IRTraits<BT>::FunctionT;
  using LoopT = typename afdo_detail::IRTraits<BT>::LoopT;
  using LoopInfoPtrT = typename afdo_detail::IRTraits<BT>::LoopInfoPtrT;
  using DominatorTreePtrT =
      typename afdo_detail::IRTraits<BT>::DominatorTreePtrT;
  using PostDominatorTreePtrT =
      typename afdo_detail::IRTraits<BT>::PostDominatorTreePtrT;
  using PostDominatorTreeT =
      typename afdo_detail::IRTraits<BT>::PostDominatorTreeT;
  using OptRemarkEmitterT =
      typename afdo_detail::IRTraits<BT>::OptRemarkEmitterT;
  using OptRemarkAnalysisT =
      typename afdo_detail::IRTraits<BT>::OptRemarkAnalysisT;
  using PredRangeT = typename afdo_detail::IRTraits<BT>::PredRangeT;
  using SuccRangeT = typename afdo_detail::IRTraits<BT>::SuccRangeT;

  using BlockWeightMap = DenseMap<const BasicBlockT *, uint64_t>;
  using EquivalenceClassMap =
      DenseMap<const BasicBlockT *, const BasicBlockT *>;
  using Edge = std::pair<const BasicBlockT *, const BasicBlockT *>;
  using EdgeWeightMap = DenseMap<Edge, uint64_t>;
  using BlockEdgeMap =
      DenseMap<const BasicBlockT *, SmallVector<const BasicBlockT *, 8>>;

protected:
  ~SampleProfileLoaderBaseImpl() = default;
  friend class SampleCoverageTracker;

  Function &getFunction(FunctionT &F) {
    return afdo_detail::IRTraits<BT>::getFunction(F);
  }
  const BasicBlockT *getEntryBB(const FunctionT *F) {
    return afdo_detail::IRTraits<BT>::getEntryBB(F);
  }
  PredRangeT getPredecessors(BasicBlockT *BB) {
    return afdo_detail::IRTraits<BT>::getPredecessors(BB);
  }
  SuccRangeT getSuccessors(BasicBlockT *BB) {
    return afdo_detail::IRTraits<BT>::getSuccessors(BB);
  }

  unsigned getFunctionLoc(FunctionT &Func);
  virtual ErrorOr<uint64_t> getInstWeight(const InstructionT &Inst);
  ErrorOr<uint64_t> getInstWeightImpl(const InstructionT &Inst);
  virtual ErrorOr<uint64_t> getProbeWeight(const InstructionT &Inst);
  ErrorOr<uint64_t> getBlockWeight(const BasicBlockT *BB);
  mutable DenseMap<const DILocation *, const FunctionSamples *>
      DILocation2SampleMap;
  virtual const FunctionSamples *
  findFunctionSamples(const InstructionT &I) const;
  void printEdgeWeight(raw_ostream &OS, Edge E);
  void printBlockWeight(raw_ostream &OS, const BasicBlockT *BB) const;
  void printBlockEquivalence(raw_ostream &OS, const BasicBlockT *BB);
  bool computeBlockWeights(FunctionT &F);
  void findEquivalenceClasses(FunctionT &F);
  void findEquivalencesFor(BasicBlockT *BB1,
                           ArrayRef<BasicBlockT *> Descendants,
                           PostDominatorTreeT *DomTree);
  void propagateWeights(FunctionT &F);
  void applyProfi(FunctionT &F, BlockEdgeMap &Successors,
                  BlockWeightMap &SampleBlockWeights,
                  BlockWeightMap &BlockWeights, EdgeWeightMap &EdgeWeights);
  uint64_t visitEdge(Edge E, unsigned *NumUnknownEdges, Edge *UnknownEdge);
  void buildEdges(FunctionT &F);
  bool propagateThroughEdges(FunctionT &F, bool UpdateBlockCount);
  void clearFunctionData(bool ResetDT = true);
  void computeDominanceAndLoopInfo(FunctionT &F);
  bool
  computeAndPropagateWeights(FunctionT &F,
                             const DenseSet<GlobalValue::GUID> &InlinedGUIDs);
  void initWeightPropagation(FunctionT &F,
                             const DenseSet<GlobalValue::GUID> &InlinedGUIDs);
  void
  finalizeWeightPropagation(FunctionT &F,
                            const DenseSet<GlobalValue::GUID> &InlinedGUIDs);
  void emitCoverageRemarks(FunctionT &F);

  /// Map basic blocks to their computed weights.
  ///
  /// The weight of a basic block is defined to be the maximum
  /// of all the instruction weights in that block.
  BlockWeightMap BlockWeights;

  /// Map edges to their computed weights.
  ///
  /// Edge weights are computed by propagating basic block weights in
  /// SampleProfile::propagateWeights.
  EdgeWeightMap EdgeWeights;

  /// Set of visited blocks during propagation.
  SmallPtrSet<const BasicBlockT *, 32> VisitedBlocks;

  /// Set of visited edges during propagation.
  SmallSet<Edge, 32> VisitedEdges;

  /// Equivalence classes for block weights.
  ///
  /// Two blocks BB1 and BB2 are in the same equivalence class if they
  /// dominate and post-dominate each other, and they are in the same loop
  /// nest. When this happens, the two blocks are guaranteed to execute
  /// the same number of times.
  EquivalenceClassMap EquivalenceClass;

  /// Dominance, post-dominance and loop information.
  DominatorTreePtrT DT;
  PostDominatorTreePtrT PDT;
  LoopInfoPtrT LI;

  /// Predecessors for each basic block in the CFG.
  BlockEdgeMap Predecessors;

  /// Successors for each basic block in the CFG.
  BlockEdgeMap Successors;

  /// Profile coverage tracker.
  SampleCoverageTracker CoverageTracker;

  /// Profile reader object.
  std::unique_ptr<SampleProfileReader> Reader;

  /// Synthetic samples created by duplicating the samples of inlined functions
  /// from the original profile as if they were top level sample profiles.
  /// Use std::map because insertion may happen while its content is referenced.
  std::map<SampleContext, FunctionSamples> OutlineFunctionSamples;

  // A pseudo probe helper to correlate the imported sample counts.
  std::unique_ptr<PseudoProbeManager> ProbeManager;

  /// Samples collected for the body of this function.
  FunctionSamples *Samples = nullptr;

  /// Name of the profile file to load.
  std::string Filename;

  /// Name of the profile remapping file to load.
  std::string RemappingFilename;

  /// VirtualFileSystem to load profile files from.
  IntrusiveRefCntPtr<vfs::FileSystem> FS;

  /// Profile Summary Info computed from sample profile.
  ProfileSummaryInfo *PSI = nullptr;

  /// Optimization Remark Emitter used to emit diagnostic remarks.
  OptRemarkEmitterT *ORE = nullptr;
};

/// Clear all the per-function data used to load samples and propagate weights.
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::clearFunctionData(bool ResetDT) {
  BlockWeights.clear();
  EdgeWeights.clear();
  VisitedBlocks.clear();
  VisitedEdges.clear();
  EquivalenceClass.clear();
  if (ResetDT) {
    DT = nullptr;
    PDT = nullptr;
    LI = nullptr;
  }
  Predecessors.clear();
  Successors.clear();
  CoverageTracker.clear();
}

#ifndef NDEBUG
/// Print the weight of edge \p E on stream \p OS.
///
/// \param OS  Stream to emit the output to.
/// \param E  Edge to print.
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::printEdgeWeight(raw_ostream &OS, Edge E) {
  OS << "weight[" << E.first->getName() << "->" << E.second->getName()
     << "]: " << EdgeWeights[E] << "\n";
}

/// Print the equivalence class of block \p BB on stream \p OS.
///
/// \param OS  Stream to emit the output to.
/// \param BB  Block to print.
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::printBlockEquivalence(
    raw_ostream &OS, const BasicBlockT *BB) {
  const BasicBlockT *Equiv = EquivalenceClass[BB];
  OS << "equivalence[" << BB->getName()
     << "]: " << ((Equiv) ? EquivalenceClass[BB]->getName() : "NONE") << "\n";
}

/// Print the weight of block \p BB on stream \p OS.
///
/// \param OS  Stream to emit the output to.
/// \param BB  Block to print.
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::printBlockWeight(
    raw_ostream &OS, const BasicBlockT *BB) const {
  const auto &I = BlockWeights.find(BB);
  uint64_t W = (I == BlockWeights.end() ? 0 : I->second);
  OS << "weight[" << BB->getName() << "]: " << W << "\n";
}
#endif

/// Get the weight for an instruction.
///
/// The "weight" of an instruction \p Inst is the number of samples
/// collected on that instruction at runtime. To retrieve it, we
/// need to compute the line number of \p Inst relative to the start of its
/// function. We use HeaderLineno to compute the offset. We then
/// look up the samples collected for \p Inst using BodySamples.
///
/// \param Inst Instruction to query.
///
/// \returns the weight of \p Inst.
template <typename BT>
ErrorOr<uint64_t>
SampleProfileLoaderBaseImpl<BT>::getInstWeight(const InstructionT &Inst) {
  if (FunctionSamples::ProfileIsProbeBased)
    return getProbeWeight(Inst);
  return getInstWeightImpl(Inst);
}

template <typename BT>
ErrorOr<uint64_t>
SampleProfileLoaderBaseImpl<BT>::getInstWeightImpl(const InstructionT &Inst) {
  const FunctionSamples *FS = findFunctionSamples(Inst);
  if (!FS)
    return std::error_code();

  const DebugLoc &DLoc = Inst.getDebugLoc();
  if (!DLoc)
    return std::error_code();

  const DILocation *DIL = DLoc;
  uint32_t LineOffset = FunctionSamples::getOffset(DIL);
  uint32_t Discriminator;
  if (EnableFSDiscriminator)
    Discriminator = DIL->getDiscriminator();
  else
    Discriminator = DIL->getBaseDiscriminator();

  ErrorOr<uint64_t> R = FS->findSamplesAt(LineOffset, Discriminator);
  if (R) {
    bool FirstMark =
        CoverageTracker.markSamplesUsed(FS, LineOffset, Discriminator, R.get());
    if (FirstMark) {
      ORE->emit([&]() {
        OptRemarkAnalysisT Remark(DEBUG_TYPE, "AppliedSamples", &Inst);
        Remark << "Applied " << ore::NV("NumSamples", *R);
        Remark << " samples from profile (offset: ";
        Remark << ore::NV("LineOffset", LineOffset);
        if (Discriminator) {
          Remark << ".";
          Remark << ore::NV("Discriminator", Discriminator);
        }
        Remark << ")";
        return Remark;
      });
    }
    LLVM_DEBUG(dbgs() << "    " << DLoc.getLine() << "." << Discriminator << ":"
                      << Inst << " (line offset: " << LineOffset << "."
                      << Discriminator << " - weight: " << R.get() << ")\n");
  }
  return R;
}

// Here use error_code to represent: 1) The dangling probe. 2) Ignore the weight
// of non-probe instruction. So if all instructions of the BB give error_code,
// tell the inference algorithm to infer the BB weight.
template <typename BT>
ErrorOr<uint64_t>
SampleProfileLoaderBaseImpl<BT>::getProbeWeight(const InstructionT &Inst) {
  assert(FunctionSamples::ProfileIsProbeBased &&
         "Profile is not pseudo probe based");
  std::optional<PseudoProbe> Probe = extractProbe(Inst);
  // Ignore the non-probe instruction. If none of the instruction in the BB is
  // probe, we choose to infer the BB's weight.
  if (!Probe)
    return std::error_code();

  const FunctionSamples *FS = findFunctionSamples(Inst);
  // If none of the instruction has FunctionSample, we choose to return zero
  // value sample to indicate the BB is cold. This could happen when the
  // instruction is from inlinee and no profile data is found.
  // FIXME: This should not be affected by the source drift issue as 1) if the
  // newly added function is top-level inliner, it won't match the CFG checksum
  // in the function profile or 2) if it's the inlinee, the inlinee should have
  // a profile, otherwise it wouldn't be inlined. For non-probe based profile,
  // we can improve it by adding a switch for profile-sample-block-accurate for
  // block level counts in the future.
  if (!FS)
    return 0;

  auto R = FS->findSamplesAt(Probe->Id, Probe->Discriminator);
  if (R) {
    uint64_t Samples = R.get() * Probe->Factor;
    bool FirstMark = CoverageTracker.markSamplesUsed(FS, Probe->Id, 0, Samples);
    if (FirstMark) {
      ORE->emit([&]() {
        OptRemarkAnalysisT Remark(DEBUG_TYPE, "AppliedSamples", &Inst);
        Remark << "Applied " << ore::NV("NumSamples", Samples);
        Remark << " samples from profile (ProbeId=";
        Remark << ore::NV("ProbeId", Probe->Id);
        if (Probe->Discriminator) {
          Remark << ".";
          Remark << ore::NV("Discriminator", Probe->Discriminator);
        }
        Remark << ", Factor=";
        Remark << ore::NV("Factor", Probe->Factor);
        Remark << ", OriginalSamples=";
        Remark << ore::NV("OriginalSamples", R.get());
        Remark << ")";
        return Remark;
      });
    }
    LLVM_DEBUG({dbgs() << "    " << Probe->Id;
      if (Probe->Discriminator)
        dbgs() << "." << Probe->Discriminator;
      dbgs() << ":" << Inst << " - weight: " << R.get()
             << " - factor: " << format("%0.2f", Probe->Factor) << ")\n";});
    return Samples;
  }
  return R;
}

/// Compute the weight of a basic block.
///
/// The weight of basic block \p BB is the maximum weight of all the
/// instructions in BB.
///
/// \param BB The basic block to query.
///
/// \returns the weight for \p BB.
template <typename BT>
ErrorOr<uint64_t>
SampleProfileLoaderBaseImpl<BT>::getBlockWeight(const BasicBlockT *BB) {
  uint64_t Max = 0;
  bool HasWeight = false;
  for (auto &I : *BB) {
    const ErrorOr<uint64_t> &R = getInstWeight(I);
    if (R) {
      Max = std::max(Max, R.get());
      HasWeight = true;
    }
  }
  return HasWeight ? ErrorOr<uint64_t>(Max) : std::error_code();
}

/// Compute and store the weights of every basic block.
///
/// This populates the BlockWeights map by computing
/// the weights of every basic block in the CFG.
///
/// \param F The function to query.
template <typename BT>
bool SampleProfileLoaderBaseImpl<BT>::computeBlockWeights(FunctionT &F) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Block weights\n");
  for (const auto &BB : F) {
    ErrorOr<uint64_t> Weight = getBlockWeight(&BB);
    if (Weight) {
      BlockWeights[&BB] = Weight.get();
      VisitedBlocks.insert(&BB);
      Changed = true;
    }
    LLVM_DEBUG(printBlockWeight(dbgs(), &BB));
  }

  return Changed;
}

/// Get the FunctionSamples for an instruction.
///
/// The FunctionSamples of an instruction \p Inst is the inlined instance
/// in which that instruction is coming from. We traverse the inline stack
/// of that instruction, and match it with the tree nodes in the profile.
///
/// \param Inst Instruction to query.
///
/// \returns the FunctionSamples pointer to the inlined instance.
template <typename BT>
const FunctionSamples *SampleProfileLoaderBaseImpl<BT>::findFunctionSamples(
    const InstructionT &Inst) const {
  const DILocation *DIL = Inst.getDebugLoc();
  if (!DIL)
    return Samples;

  auto it = DILocation2SampleMap.try_emplace(DIL, nullptr);
  if (it.second) {
    it.first->second = Samples->findFunctionSamples(DIL, Reader->getRemapper());
  }
  return it.first->second;
}

/// Find equivalence classes for the given block.
///
/// This finds all the blocks that are guaranteed to execute the same
/// number of times as \p BB1. To do this, it traverses all the
/// descendants of \p BB1 in the dominator or post-dominator tree.
///
/// A block BB2 will be in the same equivalence class as \p BB1 if
/// the following holds:
///
/// 1- \p BB1 is a descendant of BB2 in the opposite tree. So, if BB2
///    is a descendant of \p BB1 in the dominator tree, then BB2 should
///    dominate BB1 in the post-dominator tree.
///
/// 2- Both BB2 and \p BB1 must be in the same loop.
///
/// For every block BB2 that meets those two requirements, we set BB2's
/// equivalence class to \p BB1.
///
/// \param BB1  Block to check.
/// \param Descendants  Descendants of \p BB1 in either the dom or pdom tree.
/// \param DomTree  Opposite dominator tree. If \p Descendants is filled
///                 with blocks from \p BB1's dominator tree, then
///                 this is the post-dominator tree, and vice versa.
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::findEquivalencesFor(
    BasicBlockT *BB1, ArrayRef<BasicBlockT *> Descendants,
    PostDominatorTreeT *DomTree) {
  const BasicBlockT *EC = EquivalenceClass[BB1];
  uint64_t Weight = BlockWeights[EC];
  for (const auto *BB2 : Descendants) {
    bool IsDomParent = DomTree->dominates(BB2, BB1);
    bool IsInSameLoop = LI->getLoopFor(BB1) == LI->getLoopFor(BB2);
    if (BB1 != BB2 && IsDomParent && IsInSameLoop) {
      EquivalenceClass[BB2] = EC;
      // If BB2 is visited, then the entire EC should be marked as visited.
      if (VisitedBlocks.count(BB2)) {
        VisitedBlocks.insert(EC);
      }

      // If BB2 is heavier than BB1, make BB2 have the same weight
      // as BB1.
      //
      // Note that we don't worry about the opposite situation here
      // (when BB2 is lighter than BB1). We will deal with this
      // during the propagation phase. Right now, we just want to
      // make sure that BB1 has the largest weight of all the
      // members of its equivalence set.
      Weight = std::max(Weight, BlockWeights[BB2]);
    }
  }
  const BasicBlockT *EntryBB = getEntryBB(EC->getParent());
  if (EC == EntryBB) {
    BlockWeights[EC] = Samples->getHeadSamples() + 1;
  } else {
    BlockWeights[EC] = Weight;
  }
}

/// Find equivalence classes.
///
/// Since samples may be missing from blocks, we can fill in the gaps by setting
/// the weights of all the blocks in the same equivalence class to the same
/// weight. To compute the concept of equivalence, we use dominance and loop
/// information. Two blocks B1 and B2 are in the same equivalence class if B1
/// dominates B2, B2 post-dominates B1 and both are in the same loop.
///
/// \param F The function to query.
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::findEquivalenceClasses(FunctionT &F) {
  SmallVector<BasicBlockT *, 8> DominatedBBs;
  LLVM_DEBUG(dbgs() << "\nBlock equivalence classes\n");
  // Find equivalence sets based on dominance and post-dominance information.
  for (auto &BB : F) {
    BasicBlockT *BB1 = &BB;

    // Compute BB1's equivalence class once.
    if (EquivalenceClass.count(BB1)) {
      LLVM_DEBUG(printBlockEquivalence(dbgs(), BB1));
      continue;
    }

    // By default, blocks are in their own equivalence class.
    EquivalenceClass[BB1] = BB1;

    // Traverse all the blocks dominated by BB1. We are looking for
    // every basic block BB2 such that:
    //
    // 1- BB1 dominates BB2.
    // 2- BB2 post-dominates BB1.
    // 3- BB1 and BB2 are in the same loop nest.
    //
    // If all those conditions hold, it means that BB2 is executed
    // as many times as BB1, so they are placed in the same equivalence
    // class by making BB2's equivalence class be BB1.
    DominatedBBs.clear();
    DT->getDescendants(BB1, DominatedBBs);
    findEquivalencesFor(BB1, DominatedBBs, &*PDT);

    LLVM_DEBUG(printBlockEquivalence(dbgs(), BB1));
  }

  // Assign weights to equivalence classes.
  //
  // All the basic blocks in the same equivalence class will execute
  // the same number of times. Since we know that the head block in
  // each equivalence class has the largest weight, assign that weight
  // to all the blocks in that equivalence class.
  LLVM_DEBUG(
      dbgs() << "\nAssign the same weight to all blocks in the same class\n");
  for (auto &BI : F) {
    const BasicBlockT *BB = &BI;
    const BasicBlockT *EquivBB = EquivalenceClass[BB];
    if (BB != EquivBB)
      BlockWeights[BB] = BlockWeights[EquivBB];
    LLVM_DEBUG(printBlockWeight(dbgs(), BB));
  }
}

/// Visit the given edge to decide if it has a valid weight.
///
/// If \p E has not been visited before, we copy to \p UnknownEdge
/// and increment the count of unknown edges.
///
/// \param E  Edge to visit.
/// \param NumUnknownEdges  Current number of unknown edges.
/// \param UnknownEdge  Set if E has not been visited before.
///
/// \returns E's weight, if known. Otherwise, return 0.
template <typename BT>
uint64_t SampleProfileLoaderBaseImpl<BT>::visitEdge(Edge E,
                                                    unsigned *NumUnknownEdges,
                                                    Edge *UnknownEdge) {
  if (!VisitedEdges.count(E)) {
    (*NumUnknownEdges)++;
    *UnknownEdge = E;
    return 0;
  }

  return EdgeWeights[E];
}

/// Propagate weights through incoming/outgoing edges.
///
/// If the weight of a basic block is known, and there is only one edge
/// with an unknown weight, we can calculate the weight of that edge.
///
/// Similarly, if all the edges have a known count, we can calculate the
/// count of the basic block, if needed.
///
/// \param F  Function to process.
/// \param UpdateBlockCount  Whether we should update basic block counts that
///                          has already been annotated.
///
/// \returns  True if new weights were assigned to edges or blocks.
template <typename BT>
bool SampleProfileLoaderBaseImpl<BT>::propagateThroughEdges(
    FunctionT &F, bool UpdateBlockCount) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "\nPropagation through edges\n");
  for (const auto &BI : F) {
    const BasicBlockT *BB = &BI;
    const BasicBlockT *EC = EquivalenceClass[BB];

    // Visit all the predecessor and successor edges to determine
    // which ones have a weight assigned already. Note that it doesn't
    // matter that we only keep track of a single unknown edge. The
    // only case we are interested in handling is when only a single
    // edge is unknown (see setEdgeOrBlockWeight).
    for (unsigned i = 0; i < 2; i++) {
      uint64_t TotalWeight = 0;
      unsigned NumUnknownEdges = 0, NumTotalEdges = 0;
      Edge UnknownEdge, SelfReferentialEdge, SingleEdge;

      if (i == 0) {
        // First, visit all predecessor edges.
        NumTotalEdges = Predecessors[BB].size();
        for (auto *Pred : Predecessors[BB]) {
          Edge E = std::make_pair(Pred, BB);
          TotalWeight += visitEdge(E, &NumUnknownEdges, &UnknownEdge);
          if (E.first == E.second)
            SelfReferentialEdge = E;
        }
        if (NumTotalEdges == 1) {
          SingleEdge = std::make_pair(Predecessors[BB][0], BB);
        }
      } else {
        // On the second round, visit all successor edges.
        NumTotalEdges = Successors[BB].size();
        for (auto *Succ : Successors[BB]) {
          Edge E = std::make_pair(BB, Succ);
          TotalWeight += visitEdge(E, &NumUnknownEdges, &UnknownEdge);
        }
        if (NumTotalEdges == 1) {
          SingleEdge = std::make_pair(BB, Successors[BB][0]);
        }
      }

      // After visiting all the edges, there are three cases that we
      // can handle immediately:
      //
      // - All the edge weights are known (i.e., NumUnknownEdges == 0).
      //   In this case, we simply check that the sum of all the edges
      //   is the same as BB's weight. If not, we change BB's weight
      //   to match. Additionally, if BB had not been visited before,
      //   we mark it visited.
      //
      // - Only one edge is unknown and BB has already been visited.
      //   In this case, we can compute the weight of the edge by
      //   subtracting the total block weight from all the known
      //   edge weights. If the edges weight more than BB, then the
      //   edge of the last remaining edge is set to zero.
      //
      // - There exists a self-referential edge and the weight of BB is
      //   known. In this case, this edge can be based on BB's weight.
      //   We add up all the other known edges and set the weight on
      //   the self-referential edge as we did in the previous case.
      //
      // In any other case, we must continue iterating. Eventually,
      // all edges will get a weight, or iteration will stop when
      // it reaches SampleProfileMaxPropagateIterations.
      if (NumUnknownEdges <= 1) {
        uint64_t &BBWeight = BlockWeights[EC];
        if (NumUnknownEdges == 0) {
          if (!VisitedBlocks.count(EC)) {
            // If we already know the weight of all edges, the weight of the
            // basic block can be computed. It should be no larger than the sum
            // of all edge weights.
            if (TotalWeight > BBWeight) {
              BBWeight = TotalWeight;
              Changed = true;
              LLVM_DEBUG(dbgs() << "All edge weights for " << BB->getName()
                                << " known. Set weight for block: ";
                         printBlockWeight(dbgs(), BB););
            }
          } else if (NumTotalEdges == 1 &&
                     EdgeWeights[SingleEdge] < BlockWeights[EC]) {
            // If there is only one edge for the visited basic block, use the
            // block weight to adjust edge weight if edge weight is smaller.
            EdgeWeights[SingleEdge] = BlockWeights[EC];
            Changed = true;
          }
        } else if (NumUnknownEdges == 1 && VisitedBlocks.count(EC)) {
          // If there is a single unknown edge and the block has been
          // visited, then we can compute E's weight.
          if (BBWeight >= TotalWeight)
            EdgeWeights[UnknownEdge] = BBWeight - TotalWeight;
          else
            EdgeWeights[UnknownEdge] = 0;
          const BasicBlockT *OtherEC;
          if (i == 0)
            OtherEC = EquivalenceClass[UnknownEdge.first];
          else
            OtherEC = EquivalenceClass[UnknownEdge.second];
          // Edge weights should never exceed the BB weights it connects.
          if (VisitedBlocks.count(OtherEC) &&
              EdgeWeights[UnknownEdge] > BlockWeights[OtherEC])
            EdgeWeights[UnknownEdge] = BlockWeights[OtherEC];
          VisitedEdges.insert(UnknownEdge);
          Changed = true;
          LLVM_DEBUG(dbgs() << "Set weight for edge: ";
                     printEdgeWeight(dbgs(), UnknownEdge));
        }
      } else if (VisitedBlocks.count(EC) && BlockWeights[EC] == 0) {
        // If a block Weights 0, all its in/out edges should weight 0.
        if (i == 0) {
          for (auto *Pred : Predecessors[BB]) {
            Edge E = std::make_pair(Pred, BB);
            EdgeWeights[E] = 0;
            VisitedEdges.insert(E);
          }
        } else {
          for (auto *Succ : Successors[BB]) {
            Edge E = std::make_pair(BB, Succ);
            EdgeWeights[E] = 0;
            VisitedEdges.insert(E);
          }
        }
      } else if (SelfReferentialEdge.first && VisitedBlocks.count(EC)) {
        uint64_t &BBWeight = BlockWeights[BB];
        // We have a self-referential edge and the weight of BB is known.
        if (BBWeight >= TotalWeight)
          EdgeWeights[SelfReferentialEdge] = BBWeight - TotalWeight;
        else
          EdgeWeights[SelfReferentialEdge] = 0;
        VisitedEdges.insert(SelfReferentialEdge);
        Changed = true;
        LLVM_DEBUG(dbgs() << "Set self-referential edge weight to: ";
                   printEdgeWeight(dbgs(), SelfReferentialEdge));
      }
      if (UpdateBlockCount && !VisitedBlocks.count(EC) && TotalWeight > 0) {
        BlockWeights[EC] = TotalWeight;
        VisitedBlocks.insert(EC);
        Changed = true;
      }
    }
  }

  return Changed;
}

/// Build in/out edge lists for each basic block in the CFG.
///
/// We are interested in unique edges. If a block B1 has multiple
/// edges to another block B2, we only add a single B1->B2 edge.
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::buildEdges(FunctionT &F) {
  for (auto &BI : F) {
    BasicBlockT *B1 = &BI;

    // Add predecessors for B1.
    SmallPtrSet<BasicBlockT *, 16> Visited;
    if (!Predecessors[B1].empty())
      llvm_unreachable("Found a stale predecessors list in a basic block.");
    for (auto *B2 : getPredecessors(B1))
      if (Visited.insert(B2).second)
        Predecessors[B1].push_back(B2);

    // Add successors for B1.
    Visited.clear();
    if (!Successors[B1].empty())
      llvm_unreachable("Found a stale successors list in a basic block.");
    for (auto *B2 : getSuccessors(B1))
      if (Visited.insert(B2).second)
        Successors[B1].push_back(B2);
  }
}

/// Propagate weights into edges
///
/// The following rules are applied to every block BB in the CFG:
///
/// - If BB has a single predecessor/successor, then the weight
///   of that edge is the weight of the block.
///
/// - If all incoming or outgoing edges are known except one, and the
///   weight of the block is already known, the weight of the unknown
///   edge will be the weight of the block minus the sum of all the known
///   edges. If the sum of all the known edges is larger than BB's weight,
///   we set the unknown edge weight to zero.
///
/// - If there is a self-referential edge, and the weight of the block is
///   known, the weight for that edge is set to the weight of the block
///   minus the weight of the other incoming edges to that block (if
///   known).
template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::propagateWeights(FunctionT &F) {
  // Flow-based profile inference is only usable with BasicBlock instantiation
  // of SampleProfileLoaderBaseImpl.
  if (SampleProfileUseProfi) {
    // Prepare block sample counts for inference.
    BlockWeightMap SampleBlockWeights;
    for (const auto &BI : F) {
      ErrorOr<uint64_t> Weight = getBlockWeight(&BI);
      if (Weight)
        SampleBlockWeights[&BI] = Weight.get();
    }
    // Fill in BlockWeights and EdgeWeights using an inference algorithm.
    applyProfi(F, Successors, SampleBlockWeights, BlockWeights, EdgeWeights);
  } else {
    bool Changed = true;
    unsigned I = 0;

    // If BB weight is larger than its corresponding loop's header BB weight,
    // use the BB weight to replace the loop header BB weight.
    for (auto &BI : F) {
      BasicBlockT *BB = &BI;
      LoopT *L = LI->getLoopFor(BB);
      if (!L) {
        continue;
      }
      BasicBlockT *Header = L->getHeader();
      if (Header && BlockWeights[BB] > BlockWeights[Header]) {
        BlockWeights[Header] = BlockWeights[BB];
      }
    }

    // Propagate until we converge or we go past the iteration limit.
    while (Changed && I++ < SampleProfileMaxPropagateIterations) {
      Changed = propagateThroughEdges(F, false);
    }

    // The first propagation propagates BB counts from annotated BBs to unknown
    // BBs. The 2nd propagation pass resets edges weights, and use all BB
    // weights to propagate edge weights.
    VisitedEdges.clear();
    Changed = true;
    while (Changed && I++ < SampleProfileMaxPropagateIterations) {
      Changed = propagateThroughEdges(F, false);
    }

    // The 3rd propagation pass allows adjust annotated BB weights that are
    // obviously wrong.
    Changed = true;
    while (Changed && I++ < SampleProfileMaxPropagateIterations) {
      Changed = propagateThroughEdges(F, true);
    }
  }
}

template <typename FT>
void SampleProfileLoaderBaseImpl<FT>::applyProfi(
    FunctionT &F, BlockEdgeMap &Successors, BlockWeightMap &SampleBlockWeights,
    BlockWeightMap &BlockWeights, EdgeWeightMap &EdgeWeights) {
  auto Infer = SampleProfileInference<FT>(F, Successors, SampleBlockWeights);
  Infer.apply(BlockWeights, EdgeWeights);
}

/// Generate branch weight metadata for all branches in \p F.
///
/// Branch weights are computed out of instruction samples using a
/// propagation heuristic. Propagation proceeds in 3 phases:
///
/// 1- Assignment of block weights. All the basic blocks in the function
///    are initial assigned the same weight as their most frequently
///    executed instruction.
///
/// 2- Creation of equivalence classes. Since samples may be missing from
///    blocks, we can fill in the gaps by setting the weights of all the
///    blocks in the same equivalence class to the same weight. To compute
///    the concept of equivalence, we use dominance and loop information.
///    Two blocks B1 and B2 are in the same equivalence class if B1
///    dominates B2, B2 post-dominates B1 and both are in the same loop.
///
/// 3- Propagation of block weights into edges. This uses a simple
///    propagation heuristic. The following rules are applied to every
///    block BB in the CFG:
///
///    - If BB has a single predecessor/successor, then the weight
///      of that edge is the weight of the block.
///
///    - If all the edges are known except one, and the weight of the
///      block is already known, the weight of the unknown edge will
///      be the weight of the block minus the sum of all the known
///      edges. If the sum of all the known edges is larger than BB's weight,
///      we set the unknown edge weight to zero.
///
///    - If there is a self-referential edge, and the weight of the block is
///      known, the weight for that edge is set to the weight of the block
///      minus the weight of the other incoming edges to that block (if
///      known).
///
/// Since this propagation is not guaranteed to finalize for every CFG, we
/// only allow it to proceed for a limited number of iterations (controlled
/// by -sample-profile-max-propagate-iterations).
///
/// FIXME: Try to replace this propagation heuristic with a scheme
/// that is guaranteed to finalize. A work-list approach similar to
/// the standard value propagation algorithm used by SSA-CCP might
/// work here.
///
/// \param F The function to query.
///
/// \returns true if \p F was modified. Returns false, otherwise.
template <typename BT>
bool SampleProfileLoaderBaseImpl<BT>::computeAndPropagateWeights(
    FunctionT &F, const DenseSet<GlobalValue::GUID> &InlinedGUIDs) {
  bool Changed = (InlinedGUIDs.size() != 0);

  // Compute basic block weights.
  Changed |= computeBlockWeights(F);

  if (Changed) {
    // Initialize propagation.
    initWeightPropagation(F, InlinedGUIDs);

    // Propagate weights to all edges.
    propagateWeights(F);

    // Post-process propagated weights.
    finalizeWeightPropagation(F, InlinedGUIDs);
  }

  return Changed;
}

template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::initWeightPropagation(
    FunctionT &F, const DenseSet<GlobalValue::GUID> &InlinedGUIDs) {
  // Add an entry count to the function using the samples gathered at the
  // function entry.
  // Sets the GUIDs that are inlined in the profiled binary. This is used
  // for ThinLink to make correct liveness analysis, and also make the IR
  // match the profiled binary before annotation.
  getFunction(F).setEntryCount(
      ProfileCount(Samples->getHeadSamples() + 1, Function::PCT_Real),
      &InlinedGUIDs);

  if (!SampleProfileUseProfi) {
    // Compute dominance and loop info needed for propagation.
    computeDominanceAndLoopInfo(F);

    // Find equivalence classes.
    findEquivalenceClasses(F);
  }

  // Before propagation starts, build, for each block, a list of
  // unique predecessors and successors. This is necessary to handle
  // identical edges in multiway branches. Since we visit all blocks and all
  // edges of the CFG, it is cleaner to build these lists once at the start
  // of the pass.
  buildEdges(F);
}

template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::finalizeWeightPropagation(
    FunctionT &F, const DenseSet<GlobalValue::GUID> &InlinedGUIDs) {
  // If we utilize a flow-based count inference, then we trust the computed
  // counts and set the entry count as computed by the algorithm. This is
  // primarily done to sync the counts produced by profi and BFI inference,
  // which uses the entry count for mass propagation.
  // If profi produces a zero-value for the entry count, we fallback to
  // Samples->getHeadSamples() + 1 to avoid functions with zero count.
  if (SampleProfileUseProfi) {
    const BasicBlockT *EntryBB = getEntryBB(&F);
    ErrorOr<uint64_t> EntryWeight = getBlockWeight(EntryBB);
    if (BlockWeights[EntryBB] > 0) {
      getFunction(F).setEntryCount(
          ProfileCount(BlockWeights[EntryBB], Function::PCT_Real),
          &InlinedGUIDs);
    }
  }
}

template <typename BT>
void SampleProfileLoaderBaseImpl<BT>::emitCoverageRemarks(FunctionT &F) {
  // If coverage checking was requested, compute it now.
  const Function &Func = getFunction(F);
  if (SampleProfileRecordCoverage) {
    unsigned Used = CoverageTracker.countUsedRecords(Samples, PSI);
    unsigned Total = CoverageTracker.countBodyRecords(Samples, PSI);
    unsigned Coverage = CoverageTracker.computeCoverage(Used, Total);
    if (Coverage < SampleProfileRecordCoverage) {
      Func.getContext().diagnose(DiagnosticInfoSampleProfile(
          Func.getSubprogram()->getFilename(), getFunctionLoc(F),
          Twine(Used) + " of " + Twine(Total) + " available profile records (" +
              Twine(Coverage) + "%) were applied",
          DS_Warning));
    }
  }

  if (SampleProfileSampleCoverage) {
    uint64_t Used = CoverageTracker.getTotalUsedSamples();
    uint64_t Total = CoverageTracker.countBodySamples(Samples, PSI);
    unsigned Coverage = CoverageTracker.computeCoverage(Used, Total);
    if (Coverage < SampleProfileSampleCoverage) {
      Func.getContext().diagnose(DiagnosticInfoSampleProfile(
          Func.getSubprogram()->getFilename(), getFunctionLoc(F),
          Twine(Used) + " of " + Twine(Total) + " available profile samples (" +
              Twine(Coverage) + "%) were applied",
          DS_Warning));
    }
  }
}

/// Get the line number for the function header.
///
/// This looks up function \p F in the current compilation unit and
/// retrieves the line number where the function is defined. This is
/// line 0 for all the samples read from the profile file. Every line
/// number is relative to this line.
///
/// \param F  Function object to query.
///
/// \returns the line number where \p F is defined. If it returns 0,
///          it means that there is no debug information available for \p F.
template <typename BT>
unsigned SampleProfileLoaderBaseImpl<BT>::getFunctionLoc(FunctionT &F) {
  const Function &Func = getFunction(F);
  if (DISubprogram *S = Func.getSubprogram())
    return S->getLine();

  if (NoWarnSampleUnused)
    return 0;

  // If the start of \p F is missing, emit a diagnostic to inform the user
  // about the missed opportunity.
  Func.getContext().diagnose(DiagnosticInfoSampleProfile(
      "No debug information found in function " + Func.getName() +
          ": Function profile not used",
      DS_Warning));
  return 0;
}

#undef DEBUG_TYPE

} // namespace llvm
#endif // LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEIMPL_H
