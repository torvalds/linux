//===- MLInlineAdvisor.cpp - machine learned InlineAdvisor ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the interface between the inliner and a learned model.
// It delegates model evaluation to either the AOT compiled model (the
// 'release' mode) or a runtime-loaded model (the 'development' case).
//
//===----------------------------------------------------------------------===//
#include "llvm/Analysis/MLInlineAdvisor.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/InlineModelFeatureMaps.h"
#include "llvm/Analysis/InteractiveModelRunner.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ReleaseModeModelRunner.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<std::string> InteractiveChannelBaseName(
    "inliner-interactive-channel-base", cl::Hidden,
    cl::desc(
        "Base file path for the interactive mode. The incoming filename should "
        "have the name <inliner-interactive-channel-base>.in, while the "
        "outgoing name should be <inliner-interactive-channel-base>.out"));
static const std::string InclDefaultMsg =
    (Twine("In interactive mode, also send the default policy decision: ") +
     DefaultDecisionName + ".")
        .str();
static cl::opt<bool>
    InteractiveIncludeDefault("inliner-interactive-include-default", cl::Hidden,
                              cl::desc(InclDefaultMsg));

enum class SkipMLPolicyCriteria { Never, IfCallerIsNotCold };

static cl::opt<SkipMLPolicyCriteria> SkipPolicy(
    "ml-inliner-skip-policy", cl::Hidden, cl::init(SkipMLPolicyCriteria::Never),
    cl::values(clEnumValN(SkipMLPolicyCriteria::Never, "never", "never"),
               clEnumValN(SkipMLPolicyCriteria::IfCallerIsNotCold,
                          "if-caller-not-cold", "if the caller is not cold")));

static cl::opt<std::string> ModelSelector("ml-inliner-model-selector",
                                          cl::Hidden, cl::init(""));

#if defined(LLVM_HAVE_TF_AOT_INLINERSIZEMODEL)
// codegen-ed file
#include "InlinerSizeModel.h" // NOLINT
using CompiledModelType = llvm::InlinerSizeModel;
#else
using CompiledModelType = NoopSavedModelImpl;
#endif

std::unique_ptr<InlineAdvisor>
llvm::getReleaseModeAdvisor(Module &M, ModuleAnalysisManager &MAM,
                            std::function<bool(CallBase &)> GetDefaultAdvice) {
  if (!llvm::isEmbeddedModelEvaluatorValid<CompiledModelType>() &&
      InteractiveChannelBaseName.empty())
    return nullptr;
  std::unique_ptr<MLModelRunner> AOTRunner;
  if (InteractiveChannelBaseName.empty())
    AOTRunner = std::make_unique<ReleaseModeModelRunner<CompiledModelType>>(
        M.getContext(), FeatureMap, DecisionName,
        EmbeddedModelRunnerOptions().setModelSelector(ModelSelector));
  else {
    auto Features = FeatureMap;
    if (InteractiveIncludeDefault)
      Features.push_back(DefaultDecisionSpec);
    AOTRunner = std::make_unique<InteractiveModelRunner>(
        M.getContext(), Features, InlineDecisionSpec,
        InteractiveChannelBaseName + ".out",
        InteractiveChannelBaseName + ".in");
  }
  return std::make_unique<MLInlineAdvisor>(M, MAM, std::move(AOTRunner),
                                           GetDefaultAdvice);
}

#define DEBUG_TYPE "inline-ml"

static cl::opt<float> SizeIncreaseThreshold(
    "ml-advisor-size-increase-threshold", cl::Hidden,
    cl::desc("Maximum factor by which expected native size may increase before "
             "blocking any further inlining."),
    cl::init(2.0));

static cl::opt<bool> KeepFPICache(
    "ml-advisor-keep-fpi-cache", cl::Hidden,
    cl::desc(
        "For test - keep the ML Inline advisor's FunctionPropertiesInfo cache"),
    cl::init(false));

// clang-format off
const std::vector<TensorSpec> llvm::FeatureMap{
#define POPULATE_NAMES(DTYPE, SHAPE, NAME, __) TensorSpec::createSpec<DTYPE>(#NAME, SHAPE),
// InlineCost features - these must come first
  INLINE_COST_FEATURE_ITERATOR(POPULATE_NAMES)

// Non-cost features
  INLINE_FEATURE_ITERATOR(POPULATE_NAMES)
#undef POPULATE_NAMES
};
// clang-format on

const char *const llvm::DecisionName = "inlining_decision";
const TensorSpec llvm::InlineDecisionSpec =
    TensorSpec::createSpec<int64_t>(DecisionName, {1});
const char *const llvm::DefaultDecisionName = "inlining_default";
const TensorSpec llvm::DefaultDecisionSpec =
    TensorSpec::createSpec<int64_t>(DefaultDecisionName, {1});
const char *const llvm::RewardName = "delta_size";

CallBase *getInlinableCS(Instruction &I) {
  if (auto *CS = dyn_cast<CallBase>(&I))
    if (Function *Callee = CS->getCalledFunction()) {
      if (!Callee->isDeclaration()) {
        return CS;
      }
    }
  return nullptr;
}

MLInlineAdvisor::MLInlineAdvisor(
    Module &M, ModuleAnalysisManager &MAM,
    std::unique_ptr<MLModelRunner> Runner,
    std::function<bool(CallBase &)> GetDefaultAdvice)
    : InlineAdvisor(
          M, MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager()),
      ModelRunner(std::move(Runner)), GetDefaultAdvice(GetDefaultAdvice),
      CG(MAM.getResult<LazyCallGraphAnalysis>(M)),
      InitialIRSize(getModuleIRSize()), CurrentIRSize(InitialIRSize),
      PSI(MAM.getResult<ProfileSummaryAnalysis>(M)) {
  assert(ModelRunner);
  ModelRunner->switchContext("");
  // Extract the 'call site height' feature - the position of a call site
  // relative to the farthest statically reachable SCC node. We don't mutate
  // this value while inlining happens. Empirically, this feature proved
  // critical in behavioral cloning - i.e. training a model to mimic the manual
  // heuristic's decisions - and, thus, equally important for training for
  // improvement.
  CallGraph CGraph(M);
  for (auto I = scc_begin(&CGraph); !I.isAtEnd(); ++I) {
    const std::vector<CallGraphNode *> &CGNodes = *I;
    unsigned Level = 0;
    for (auto *CGNode : CGNodes) {
      Function *F = CGNode->getFunction();
      if (!F || F->isDeclaration())
        continue;
      for (auto &I : instructions(F)) {
        if (auto *CS = getInlinableCS(I)) {
          auto *Called = CS->getCalledFunction();
          auto Pos = FunctionLevels.find(&CG.get(*Called));
          // In bottom up traversal, an inlinable callee is either in the
          // same SCC, or to a function in a visited SCC. So not finding its
          // level means we haven't visited it yet, meaning it's in this SCC.
          if (Pos == FunctionLevels.end())
            continue;
          Level = std::max(Level, Pos->second + 1);
        }
      }
    }
    for (auto *CGNode : CGNodes) {
      Function *F = CGNode->getFunction();
      if (F && !F->isDeclaration())
        FunctionLevels[&CG.get(*F)] = Level;
    }
  }
  for (auto KVP : FunctionLevels) {
    AllNodes.insert(KVP.first);
    EdgeCount += getLocalCalls(KVP.first->getFunction());
  }
  NodeCount = AllNodes.size();
}

unsigned MLInlineAdvisor::getInitialFunctionLevel(const Function &F) const {
  return CG.lookup(F) ? FunctionLevels.at(CG.lookup(F)) : 0;
}

void MLInlineAdvisor::onPassEntry(LazyCallGraph::SCC *CurSCC) {
  if (!CurSCC || ForceStop)
    return;
  FPICache.clear();
  // Function passes executed between InlinerPass runs may have changed the
  // module-wide features.
  // The cgscc pass manager rules are such that:
  // - if a pass leads to merging SCCs, then the pipeline is restarted on the
  // merged SCC
  // - if a pass leads to splitting the SCC, then we continue with one of the
  // splits
  // This means that the NodesInLastSCC is a superset (not strict) of the nodes
  // that subsequent passes would have processed
  // - in addition, if new Nodes were created by a pass (e.g. CoroSplit),
  // they'd be adjacent to Nodes in the last SCC. So we just need to check the
  // boundary of Nodes in NodesInLastSCC for Nodes we haven't seen. We don't
  // care about the nature of the Edge (call or ref). `FunctionLevels`-wise, we
  // record them at the same level as the original node (this is a choice, may
  // need revisiting).
  // - nodes are only deleted at the end of a call graph walk where they are
  // batch deleted, so we shouldn't see any dead nodes here.
  while (!NodesInLastSCC.empty()) {
    const auto *N = *NodesInLastSCC.begin();
    assert(!N->isDead());
    NodesInLastSCC.erase(N);
    EdgeCount += getLocalCalls(N->getFunction());
    const auto NLevel = FunctionLevels.at(N);
    for (const auto &E : *(*N)) {
      const auto *AdjNode = &E.getNode();
      assert(!AdjNode->isDead() && !AdjNode->getFunction().isDeclaration());
      auto I = AllNodes.insert(AdjNode);
      // We've discovered a new function.
      if (I.second) {
        ++NodeCount;
        NodesInLastSCC.insert(AdjNode);
        FunctionLevels[AdjNode] = NLevel;
      }
    }
  }

  EdgeCount -= EdgesOfLastSeenNodes;
  EdgesOfLastSeenNodes = 0;

  // (Re)use NodesInLastSCC to remember the nodes in the SCC right now,
  // in case the SCC is split before onPassExit and some nodes are split out
  assert(NodesInLastSCC.empty());
  for (const auto &N : *CurSCC)
    NodesInLastSCC.insert(&N);
}

void MLInlineAdvisor::onPassExit(LazyCallGraph::SCC *CurSCC) {
  // No need to keep this around - function passes will invalidate it.
  if (!KeepFPICache)
    FPICache.clear();
  if (!CurSCC || ForceStop)
    return;
  // Keep track of the nodes and edges we last saw. Then, in onPassEntry,
  // we update the node count and edge count from the subset of these nodes that
  // survived.
  EdgesOfLastSeenNodes = 0;

  // Check on nodes that were in SCC onPassEntry
  for (const LazyCallGraph::Node *N : NodesInLastSCC) {
    assert(!N->isDead());
    EdgesOfLastSeenNodes += getLocalCalls(N->getFunction());
  }

  // Check on nodes that may have got added to SCC
  for (const auto &N : *CurSCC) {
    assert(!N.isDead());
    auto I = NodesInLastSCC.insert(&N);
    if (I.second)
      EdgesOfLastSeenNodes += getLocalCalls(N.getFunction());
  }
  assert(NodeCount >= NodesInLastSCC.size());
  assert(EdgeCount >= EdgesOfLastSeenNodes);
}

int64_t MLInlineAdvisor::getLocalCalls(Function &F) {
  return getCachedFPI(F).DirectCallsToDefinedFunctions;
}

// Update the internal state of the advisor, and force invalidate feature
// analysis. Currently, we maintain minimal (and very simple) global state - the
// number of functions and the number of static calls. We also keep track of the
// total IR size in this module, to stop misbehaving policies at a certain bloat
// factor (SizeIncreaseThreshold)
void MLInlineAdvisor::onSuccessfulInlining(const MLInlineAdvice &Advice,
                                           bool CalleeWasDeleted) {
  assert(!ForceStop);
  Function *Caller = Advice.getCaller();
  Function *Callee = Advice.getCallee();
  // The caller features aren't valid anymore.
  {
    PreservedAnalyses PA = PreservedAnalyses::all();
    PA.abandon<FunctionPropertiesAnalysis>();
    PA.abandon<DominatorTreeAnalysis>();
    PA.abandon<LoopAnalysis>();
    FAM.invalidate(*Caller, PA);
  }
  Advice.updateCachedCallerFPI(FAM);
  int64_t IRSizeAfter =
      getIRSize(*Caller) + (CalleeWasDeleted ? 0 : Advice.CalleeIRSize);
  CurrentIRSize += IRSizeAfter - (Advice.CallerIRSize + Advice.CalleeIRSize);
  if (CurrentIRSize > SizeIncreaseThreshold * InitialIRSize)
    ForceStop = true;

  // We can delta-update module-wide features. We know the inlining only changed
  // the caller, and maybe the callee (by deleting the latter).
  // Nodes are simple to update.
  // For edges, we 'forget' the edges that the caller and callee used to have
  // before inlining, and add back what they currently have together.
  int64_t NewCallerAndCalleeEdges =
      getCachedFPI(*Caller).DirectCallsToDefinedFunctions;

  // A dead function's node is not actually removed from the call graph until
  // the end of the call graph walk, but the node no longer belongs to any valid
  // SCC.
  if (CalleeWasDeleted) {
    --NodeCount;
    NodesInLastSCC.erase(CG.lookup(*Callee));
    DeadFunctions.insert(Callee);
  } else {
    NewCallerAndCalleeEdges +=
        getCachedFPI(*Callee).DirectCallsToDefinedFunctions;
  }
  EdgeCount += (NewCallerAndCalleeEdges - Advice.CallerAndCalleeEdges);
  assert(CurrentIRSize >= 0 && EdgeCount >= 0 && NodeCount >= 0);
}

int64_t MLInlineAdvisor::getModuleIRSize() const {
  int64_t Ret = 0;
  for (auto &F : M)
    if (!F.isDeclaration())
      Ret += getIRSize(F);
  return Ret;
}

FunctionPropertiesInfo &MLInlineAdvisor::getCachedFPI(Function &F) const {
  auto InsertPair =
      FPICache.insert(std::make_pair(&F, FunctionPropertiesInfo()));
  if (!InsertPair.second)
    return InsertPair.first->second;
  InsertPair.first->second = FAM.getResult<FunctionPropertiesAnalysis>(F);
  return InsertPair.first->second;
}

std::unique_ptr<InlineAdvice> MLInlineAdvisor::getAdviceImpl(CallBase &CB) {
  if (auto Skip = getSkipAdviceIfUnreachableCallsite(CB))
    return Skip;

  auto &Caller = *CB.getCaller();
  auto &Callee = *CB.getCalledFunction();

  auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
    return FAM.getResult<AssumptionAnalysis>(F);
  };
  auto &TIR = FAM.getResult<TargetIRAnalysis>(Callee);
  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(Caller);

  if (SkipPolicy == SkipMLPolicyCriteria::IfCallerIsNotCold) {
    if (!PSI.isFunctionEntryCold(&Caller))
      return std::make_unique<InlineAdvice>(this, CB, ORE,
                                            GetDefaultAdvice(CB));
  }
  auto MandatoryKind = InlineAdvisor::getMandatoryKind(CB, FAM, ORE);
  // If this is a "never inline" case, there won't be any changes to internal
  // state we need to track, so we can just return the base InlineAdvice, which
  // will do nothing interesting.
  // Same thing if this is a recursive case.
  if (MandatoryKind == InlineAdvisor::MandatoryInliningKind::Never ||
      &Caller == &Callee)
    return getMandatoryAdvice(CB, false);

  bool Mandatory =
      MandatoryKind == InlineAdvisor::MandatoryInliningKind::Always;

  // If we need to stop, we won't want to track anymore any state changes, so
  // we just return the base InlineAdvice, which acts as a noop.
  if (ForceStop) {
    ORE.emit([&] {
      return OptimizationRemarkMissed(DEBUG_TYPE, "ForceStop", &CB)
             << "Won't attempt inlining because module size grew too much.";
    });
    return std::make_unique<InlineAdvice>(this, CB, ORE, Mandatory);
  }

  int CostEstimate = 0;
  if (!Mandatory) {
    auto IsCallSiteInlinable =
        llvm::getInliningCostEstimate(CB, TIR, GetAssumptionCache);
    if (!IsCallSiteInlinable) {
      // We can't inline this for correctness reasons, so return the base
      // InlineAdvice, as we don't care about tracking any state changes (which
      // won't happen).
      return std::make_unique<InlineAdvice>(this, CB, ORE, false);
    }
    CostEstimate = *IsCallSiteInlinable;
  }

  const auto CostFeatures =
      llvm::getInliningCostFeatures(CB, TIR, GetAssumptionCache);
  if (!CostFeatures) {
    return std::make_unique<InlineAdvice>(this, CB, ORE, false);
  }

  if (Mandatory)
    return getMandatoryAdvice(CB, true);

  auto NrCtantParams = 0;
  for (auto I = CB.arg_begin(), E = CB.arg_end(); I != E; ++I) {
    NrCtantParams += (isa<Constant>(*I));
  }

  auto &CallerBefore = getCachedFPI(Caller);
  auto &CalleeBefore = getCachedFPI(Callee);

  *ModelRunner->getTensor<int64_t>(FeatureIndex::callee_basic_block_count) =
      CalleeBefore.BasicBlockCount;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::callsite_height) =
      getInitialFunctionLevel(Caller);
  *ModelRunner->getTensor<int64_t>(FeatureIndex::node_count) = NodeCount;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::nr_ctant_params) =
      NrCtantParams;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::edge_count) = EdgeCount;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::caller_users) =
      CallerBefore.Uses;
  *ModelRunner->getTensor<int64_t>(
      FeatureIndex::caller_conditionally_executed_blocks) =
      CallerBefore.BlocksReachedFromConditionalInstruction;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::caller_basic_block_count) =
      CallerBefore.BasicBlockCount;
  *ModelRunner->getTensor<int64_t>(
      FeatureIndex::callee_conditionally_executed_blocks) =
      CalleeBefore.BlocksReachedFromConditionalInstruction;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::callee_users) =
      CalleeBefore.Uses;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::cost_estimate) = CostEstimate;
  *ModelRunner->getTensor<int64_t>(FeatureIndex::is_callee_avail_external) =
      Callee.hasAvailableExternallyLinkage();
  *ModelRunner->getTensor<int64_t>(FeatureIndex::is_caller_avail_external) =
      Caller.hasAvailableExternallyLinkage();

  // Add the cost features
  for (size_t I = 0;
       I < static_cast<size_t>(InlineCostFeatureIndex::NumberOfFeatures); ++I) {
    *ModelRunner->getTensor<int64_t>(inlineCostFeatureToMlFeature(
        static_cast<InlineCostFeatureIndex>(I))) = CostFeatures->at(I);
  }
  // This one would have been set up to be right at the end.
  if (!InteractiveChannelBaseName.empty() && InteractiveIncludeDefault)
    *ModelRunner->getTensor<int64_t>(InlineCostFeatureIndex::NumberOfFeatures) =
        GetDefaultAdvice(CB);
  return getAdviceFromModel(CB, ORE);
}

std::unique_ptr<MLInlineAdvice>
MLInlineAdvisor::getAdviceFromModel(CallBase &CB,
                                    OptimizationRemarkEmitter &ORE) {
  return std::make_unique<MLInlineAdvice>(
      this, CB, ORE, static_cast<bool>(ModelRunner->evaluate<int64_t>()));
}

std::unique_ptr<InlineAdvice>
MLInlineAdvisor::getSkipAdviceIfUnreachableCallsite(CallBase &CB) {
  if (!FAM.getResult<DominatorTreeAnalysis>(*CB.getCaller())
           .isReachableFromEntry(CB.getParent()))
    return std::make_unique<InlineAdvice>(this, CB, getCallerORE(CB), false);
  return nullptr;
}

std::unique_ptr<InlineAdvice> MLInlineAdvisor::getMandatoryAdvice(CallBase &CB,
                                                                  bool Advice) {
  // Make sure we track inlinings in all cases - mandatory or not.
  if (auto Skip = getSkipAdviceIfUnreachableCallsite(CB))
    return Skip;
  if (Advice && !ForceStop)
    return getMandatoryAdviceImpl(CB);

  // If this is a "never inline" case, there won't be any changes to internal
  // state we need to track, so we can just return the base InlineAdvice, which
  // will do nothing interesting.
  // Same if we are forced to stop - we don't track anymore.
  return std::make_unique<InlineAdvice>(this, CB, getCallerORE(CB), Advice);
}

std::unique_ptr<MLInlineAdvice>
MLInlineAdvisor::getMandatoryAdviceImpl(CallBase &CB) {
  return std::make_unique<MLInlineAdvice>(this, CB, getCallerORE(CB), true);
}

void MLInlineAdvisor::print(raw_ostream &OS) const {
  OS << "[MLInlineAdvisor] Nodes: " << NodeCount << " Edges: " << EdgeCount
     << " EdgesOfLastSeenNodes: " << EdgesOfLastSeenNodes << "\n";
  OS << "[MLInlineAdvisor] FPI:\n";
  for (auto I : FPICache) {
    OS << I.first->getName() << ":\n";
    I.second.print(OS);
    OS << "\n";
  }
  OS << "\n";
  OS << "[MLInlineAdvisor] FuncLevels:\n";
  for (auto I : FunctionLevels)
    OS << (DeadFunctions.contains(&I.first->getFunction())
               ? "<deleted>"
               : I.first->getFunction().getName())
       << " : " << I.second << "\n";

  OS << "\n";
}

MLInlineAdvice::MLInlineAdvice(MLInlineAdvisor *Advisor, CallBase &CB,
                               OptimizationRemarkEmitter &ORE,
                               bool Recommendation)
    : InlineAdvice(Advisor, CB, ORE, Recommendation),
      CallerIRSize(Advisor->isForcedToStop() ? 0 : Advisor->getIRSize(*Caller)),
      CalleeIRSize(Advisor->isForcedToStop() ? 0 : Advisor->getIRSize(*Callee)),
      CallerAndCalleeEdges(Advisor->isForcedToStop()
                               ? 0
                               : (Advisor->getLocalCalls(*Caller) +
                                  Advisor->getLocalCalls(*Callee))),
      PreInlineCallerFPI(Advisor->getCachedFPI(*Caller)) {
  if (Recommendation)
    FPU.emplace(Advisor->getCachedFPI(*getCaller()), CB);
}

void MLInlineAdvice::reportContextForRemark(
    DiagnosticInfoOptimizationBase &OR) {
  using namespace ore;
  OR << NV("Callee", Callee->getName());
  for (size_t I = 0; I < NumberOfFeatures; ++I)
    OR << NV(FeatureMap[I].name(),
             *getAdvisor()->getModelRunner().getTensor<int64_t>(I));
  OR << NV("ShouldInline", isInliningRecommended());
}

void MLInlineAdvice::updateCachedCallerFPI(FunctionAnalysisManager &FAM) const {
  FPU->finish(FAM);
}

void MLInlineAdvice::recordInliningImpl() {
  ORE.emit([&]() {
    OptimizationRemark R(DEBUG_TYPE, "InliningSuccess", DLoc, Block);
    reportContextForRemark(R);
    return R;
  });
  getAdvisor()->onSuccessfulInlining(*this, /*CalleeWasDeleted*/ false);
}

void MLInlineAdvice::recordInliningWithCalleeDeletedImpl() {
  ORE.emit([&]() {
    OptimizationRemark R(DEBUG_TYPE, "InliningSuccessWithCalleeDeleted", DLoc,
                         Block);
    reportContextForRemark(R);
    return R;
  });
  getAdvisor()->onSuccessfulInlining(*this, /*CalleeWasDeleted*/ true);
}

void MLInlineAdvice::recordUnsuccessfulInliningImpl(
    const InlineResult &Result) {
  getAdvisor()->getCachedFPI(*Caller) = PreInlineCallerFPI;
  ORE.emit([&]() {
    OptimizationRemarkMissed R(DEBUG_TYPE, "InliningAttemptedAndUnsuccessful",
                               DLoc, Block);
    reportContextForRemark(R);
    return R;
  });
}
void MLInlineAdvice::recordUnattemptedInliningImpl() {
  assert(!FPU);
  ORE.emit([&]() {
    OptimizationRemarkMissed R(DEBUG_TYPE, "IniningNotAttempted", DLoc, Block);
    reportContextForRemark(R);
    return R;
  });
}
