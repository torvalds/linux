//===- InlineAdvisor.cpp - analysis pass implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements InlineAdvisorAnalysis and DefaultInlineAdvisor, and
// related types.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ReplayInlineAdvisor.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/Utils/ImportedFunctionsInliningStatistics.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
#define DEBUG_TYPE "inline"
#ifdef LLVM_HAVE_TF_AOT_INLINERSIZEMODEL
#define LLVM_HAVE_TF_AOT
#endif

// This weirdly named statistic tracks the number of times that, when attempting
// to inline a function A into B, we analyze the callers of B in order to see
// if those would be more profitable and blocked inline steps.
STATISTIC(NumCallerCallersAnalyzed, "Number of caller-callers analyzed");

/// Flag to add inline messages as callsite attributes 'inline-remark'.
static cl::opt<bool>
    InlineRemarkAttribute("inline-remark-attribute", cl::init(false),
                          cl::Hidden,
                          cl::desc("Enable adding inline-remark attribute to"
                                   " callsites processed by inliner but decided"
                                   " to be not inlined"));

static cl::opt<bool> EnableInlineDeferral("inline-deferral", cl::init(false),
                                          cl::Hidden,
                                          cl::desc("Enable deferred inlining"));

// An integer used to limit the cost of inline deferral.  The default negative
// number tells shouldBeDeferred to only take the secondary cost into account.
static cl::opt<int>
    InlineDeferralScale("inline-deferral-scale",
                        cl::desc("Scale to limit the cost of inline deferral"),
                        cl::init(2), cl::Hidden);

static cl::opt<bool>
    AnnotateInlinePhase("annotate-inline-phase", cl::Hidden, cl::init(false),
                        cl::desc("If true, annotate inline advisor remarks "
                                 "with LTO and pass information."));

namespace llvm {
extern cl::opt<InlinerFunctionImportStatsOpts> InlinerFunctionImportStats;
} // namespace llvm

namespace {
using namespace llvm::ore;
class MandatoryInlineAdvice : public InlineAdvice {
public:
  MandatoryInlineAdvice(InlineAdvisor *Advisor, CallBase &CB,
                        OptimizationRemarkEmitter &ORE,
                        bool IsInliningMandatory)
      : InlineAdvice(Advisor, CB, ORE, IsInliningMandatory) {}

private:
  void recordInliningWithCalleeDeletedImpl() override { recordInliningImpl(); }

  void recordInliningImpl() override {
    if (IsInliningRecommended)
      emitInlinedInto(ORE, DLoc, Block, *Callee, *Caller, IsInliningRecommended,
                      [&](OptimizationRemark &Remark) {
                        Remark << ": always inline attribute";
                      });
  }

  void recordUnsuccessfulInliningImpl(const InlineResult &Result) override {
    if (IsInliningRecommended)
      ORE.emit([&]() {
        return OptimizationRemarkMissed(Advisor->getAnnotatedInlinePassName(),
                                        "NotInlined", DLoc, Block)
               << "'" << NV("Callee", Callee) << "' is not AlwaysInline into '"
               << NV("Caller", Caller)
               << "': " << NV("Reason", Result.getFailureReason());
      });
  }

  void recordUnattemptedInliningImpl() override {
    assert(!IsInliningRecommended && "Expected to attempt inlining");
  }
};
} // namespace

void DefaultInlineAdvice::recordUnsuccessfulInliningImpl(
    const InlineResult &Result) {
  using namespace ore;
  llvm::setInlineRemark(*OriginalCB, std::string(Result.getFailureReason()) +
                                         "; " + inlineCostStr(*OIC));
  ORE.emit([&]() {
    return OptimizationRemarkMissed(Advisor->getAnnotatedInlinePassName(),
                                    "NotInlined", DLoc, Block)
           << "'" << NV("Callee", Callee) << "' is not inlined into '"
           << NV("Caller", Caller)
           << "': " << NV("Reason", Result.getFailureReason());
  });
}

void DefaultInlineAdvice::recordInliningWithCalleeDeletedImpl() {
  if (EmitRemarks)
    emitInlinedIntoBasedOnCost(ORE, DLoc, Block, *Callee, *Caller, *OIC,
                               /* ForProfileContext= */ false,
                               Advisor->getAnnotatedInlinePassName());
}

void DefaultInlineAdvice::recordInliningImpl() {
  if (EmitRemarks)
    emitInlinedIntoBasedOnCost(ORE, DLoc, Block, *Callee, *Caller, *OIC,
                               /* ForProfileContext= */ false,
                               Advisor->getAnnotatedInlinePassName());
}

std::optional<llvm::InlineCost> static getDefaultInlineAdvice(
    CallBase &CB, FunctionAnalysisManager &FAM, const InlineParams &Params) {
  Function &Caller = *CB.getCaller();
  ProfileSummaryInfo *PSI =
      FAM.getResult<ModuleAnalysisManagerFunctionProxy>(Caller)
          .getCachedResult<ProfileSummaryAnalysis>(
              *CB.getParent()->getParent()->getParent());

  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(Caller);
  auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
    return FAM.getResult<AssumptionAnalysis>(F);
  };
  auto GetBFI = [&](Function &F) -> BlockFrequencyInfo & {
    return FAM.getResult<BlockFrequencyAnalysis>(F);
  };
  auto GetTLI = [&](Function &F) -> const TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };

  auto GetInlineCost = [&](CallBase &CB) {
    Function &Callee = *CB.getCalledFunction();
    auto &CalleeTTI = FAM.getResult<TargetIRAnalysis>(Callee);
    bool RemarksEnabled =
        Callee.getContext().getDiagHandlerPtr()->isMissedOptRemarkEnabled(
            DEBUG_TYPE);
    return getInlineCost(CB, Params, CalleeTTI, GetAssumptionCache, GetTLI,
                         GetBFI, PSI, RemarksEnabled ? &ORE : nullptr);
  };
  return llvm::shouldInline(
      CB, GetInlineCost, ORE,
      Params.EnableDeferral.value_or(EnableInlineDeferral));
}

std::unique_ptr<InlineAdvice>
DefaultInlineAdvisor::getAdviceImpl(CallBase &CB) {
  auto OIC = getDefaultInlineAdvice(CB, FAM, Params);
  return std::make_unique<DefaultInlineAdvice>(
      this, CB, OIC,
      FAM.getResult<OptimizationRemarkEmitterAnalysis>(*CB.getCaller()));
}

InlineAdvice::InlineAdvice(InlineAdvisor *Advisor, CallBase &CB,
                           OptimizationRemarkEmitter &ORE,
                           bool IsInliningRecommended)
    : Advisor(Advisor), Caller(CB.getCaller()), Callee(CB.getCalledFunction()),
      DLoc(CB.getDebugLoc()), Block(CB.getParent()), ORE(ORE),
      IsInliningRecommended(IsInliningRecommended) {}

void InlineAdvice::recordInlineStatsIfNeeded() {
  if (Advisor->ImportedFunctionsStats)
    Advisor->ImportedFunctionsStats->recordInline(*Caller, *Callee);
}

void InlineAdvice::recordInlining() {
  markRecorded();
  recordInlineStatsIfNeeded();
  recordInliningImpl();
}

void InlineAdvice::recordInliningWithCalleeDeleted() {
  markRecorded();
  recordInlineStatsIfNeeded();
  recordInliningWithCalleeDeletedImpl();
}

AnalysisKey InlineAdvisorAnalysis::Key;
AnalysisKey PluginInlineAdvisorAnalysis::Key;
bool PluginInlineAdvisorAnalysis::HasBeenRegistered = false;

bool InlineAdvisorAnalysis::Result::tryCreate(
    InlineParams Params, InliningAdvisorMode Mode,
    const ReplayInlinerSettings &ReplaySettings, InlineContext IC) {
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  if (PluginInlineAdvisorAnalysis::HasBeenRegistered) {
    auto &DA = MAM.getResult<PluginInlineAdvisorAnalysis>(M);
    Advisor.reset(DA.Factory(M, FAM, Params, IC));
    return !!Advisor;
  }
  auto GetDefaultAdvice = [&FAM, Params](CallBase &CB) {
    auto OIC = getDefaultInlineAdvice(CB, FAM, Params);
    return OIC.has_value();
  };
  switch (Mode) {
  case InliningAdvisorMode::Default:
    LLVM_DEBUG(dbgs() << "Using default inliner heuristic.\n");
    Advisor.reset(new DefaultInlineAdvisor(M, FAM, Params, IC));
    // Restrict replay to default advisor, ML advisors are stateful so
    // replay will need augmentations to interleave with them correctly.
    if (!ReplaySettings.ReplayFile.empty()) {
      Advisor = llvm::getReplayInlineAdvisor(M, FAM, M.getContext(),
                                             std::move(Advisor), ReplaySettings,
                                             /* EmitRemarks =*/true, IC);
    }
    break;
  case InliningAdvisorMode::Development:
#ifdef LLVM_HAVE_TFLITE
    LLVM_DEBUG(dbgs() << "Using development-mode inliner policy.\n");
    Advisor = llvm::getDevelopmentModeAdvisor(M, MAM, GetDefaultAdvice);
#endif
    break;
  case InliningAdvisorMode::Release:
    LLVM_DEBUG(dbgs() << "Using release-mode inliner policy.\n");
    Advisor = llvm::getReleaseModeAdvisor(M, MAM, GetDefaultAdvice);
    break;
  }

  return !!Advisor;
}

/// Return true if inlining of CB can block the caller from being
/// inlined which is proved to be more beneficial. \p IC is the
/// estimated inline cost associated with callsite \p CB.
/// \p TotalSecondaryCost will be set to the estimated cost of inlining the
/// caller if \p CB is suppressed for inlining.
static bool
shouldBeDeferred(Function *Caller, InlineCost IC, int &TotalSecondaryCost,
                 function_ref<InlineCost(CallBase &CB)> GetInlineCost) {
  // For now we only handle local or inline functions.
  if (!Caller->hasLocalLinkage() && !Caller->hasLinkOnceODRLinkage())
    return false;
  // If the cost of inlining CB is non-positive, it is not going to prevent the
  // caller from being inlined into its callers and hence we don't need to
  // defer.
  if (IC.getCost() <= 0)
    return false;
  // Try to detect the case where the current inlining candidate caller (call
  // it B) is a static or linkonce-ODR function and is an inlining candidate
  // elsewhere, and the current candidate callee (call it C) is large enough
  // that inlining it into B would make B too big to inline later. In these
  // circumstances it may be best not to inline C into B, but to inline B into
  // its callers.
  //
  // This only applies to static and linkonce-ODR functions because those are
  // expected to be available for inlining in the translation units where they
  // are used. Thus we will always have the opportunity to make local inlining
  // decisions. Importantly the linkonce-ODR linkage covers inline functions
  // and templates in C++.
  //
  // FIXME: All of this logic should be sunk into getInlineCost. It relies on
  // the internal implementation of the inline cost metrics rather than
  // treating them as truly abstract units etc.
  TotalSecondaryCost = 0;
  // The candidate cost to be imposed upon the current function.
  int CandidateCost = IC.getCost() - 1;
  // If the caller has local linkage and can be inlined to all its callers, we
  // can apply a huge negative bonus to TotalSecondaryCost.
  bool ApplyLastCallBonus = Caller->hasLocalLinkage() && !Caller->hasOneUse();
  // This bool tracks what happens if we DO inline C into B.
  bool InliningPreventsSomeOuterInline = false;
  unsigned NumCallerUsers = 0;
  for (User *U : Caller->users()) {
    CallBase *CS2 = dyn_cast<CallBase>(U);

    // If this isn't a call to Caller (it could be some other sort
    // of reference) skip it.  Such references will prevent the caller
    // from being removed.
    if (!CS2 || CS2->getCalledFunction() != Caller) {
      ApplyLastCallBonus = false;
      continue;
    }

    InlineCost IC2 = GetInlineCost(*CS2);
    ++NumCallerCallersAnalyzed;
    if (!IC2) {
      ApplyLastCallBonus = false;
      continue;
    }
    if (IC2.isAlways())
      continue;

    // See if inlining of the original callsite would erase the cost delta of
    // this callsite. We subtract off the penalty for the call instruction,
    // which we would be deleting.
    if (IC2.getCostDelta() <= CandidateCost) {
      InliningPreventsSomeOuterInline = true;
      TotalSecondaryCost += IC2.getCost();
      NumCallerUsers++;
    }
  }

  if (!InliningPreventsSomeOuterInline)
    return false;

  // If all outer calls to Caller would get inlined, the cost for the last
  // one is set very low by getInlineCost, in anticipation that Caller will
  // be removed entirely.  We did not account for this above unless there
  // is only one caller of Caller.
  if (ApplyLastCallBonus)
    TotalSecondaryCost -= InlineConstants::LastCallToStaticBonus;

  // If InlineDeferralScale is negative, then ignore the cost of primary
  // inlining -- IC.getCost() multiplied by the number of callers to Caller.
  if (InlineDeferralScale < 0)
    return TotalSecondaryCost < IC.getCost();

  int TotalCost = TotalSecondaryCost + IC.getCost() * NumCallerUsers;
  int Allowance = IC.getCost() * InlineDeferralScale;
  return TotalCost < Allowance;
}

namespace llvm {
static raw_ostream &operator<<(raw_ostream &R, const ore::NV &Arg) {
  return R << Arg.Val;
}

template <class RemarkT>
RemarkT &operator<<(RemarkT &&R, const InlineCost &IC) {
  using namespace ore;
  if (IC.isAlways()) {
    R << "(cost=always)";
  } else if (IC.isNever()) {
    R << "(cost=never)";
  } else {
    R << "(cost=" << ore::NV("Cost", IC.getCost())
      << ", threshold=" << ore::NV("Threshold", IC.getThreshold()) << ")";
  }
  if (const char *Reason = IC.getReason())
    R << ": " << ore::NV("Reason", Reason);
  return R;
}
} // namespace llvm

std::string llvm::inlineCostStr(const InlineCost &IC) {
  std::string Buffer;
  raw_string_ostream Remark(Buffer);
  Remark << IC;
  return Remark.str();
}

void llvm::setInlineRemark(CallBase &CB, StringRef Message) {
  if (!InlineRemarkAttribute)
    return;

  Attribute Attr = Attribute::get(CB.getContext(), "inline-remark", Message);
  CB.addFnAttr(Attr);
}

/// Return the cost only if the inliner should attempt to inline at the given
/// CallSite. If we return the cost, we will emit an optimisation remark later
/// using that cost, so we won't do so from this function. Return std::nullopt
/// if inlining should not be attempted.
std::optional<InlineCost>
llvm::shouldInline(CallBase &CB,
                   function_ref<InlineCost(CallBase &CB)> GetInlineCost,
                   OptimizationRemarkEmitter &ORE, bool EnableDeferral) {
  using namespace ore;

  InlineCost IC = GetInlineCost(CB);
  Instruction *Call = &CB;
  Function *Callee = CB.getCalledFunction();
  Function *Caller = CB.getCaller();

  if (IC.isAlways()) {
    LLVM_DEBUG(dbgs() << "    Inlining " << inlineCostStr(IC)
                      << ", Call: " << CB << "\n");
    return IC;
  }

  if (!IC) {
    LLVM_DEBUG(dbgs() << "    NOT Inlining " << inlineCostStr(IC)
                      << ", Call: " << CB << "\n");
    if (IC.isNever()) {
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "NeverInline", Call)
               << "'" << NV("Callee", Callee) << "' not inlined into '"
               << NV("Caller", Caller)
               << "' because it should never be inlined " << IC;
      });
    } else {
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "TooCostly", Call)
               << "'" << NV("Callee", Callee) << "' not inlined into '"
               << NV("Caller", Caller) << "' because too costly to inline "
               << IC;
      });
    }
    setInlineRemark(CB, inlineCostStr(IC));
    return std::nullopt;
  }

  int TotalSecondaryCost = 0;
  if (EnableDeferral &&
      shouldBeDeferred(Caller, IC, TotalSecondaryCost, GetInlineCost)) {
    LLVM_DEBUG(dbgs() << "    NOT Inlining: " << CB
                      << " Cost = " << IC.getCost()
                      << ", outer Cost = " << TotalSecondaryCost << '\n');
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "IncreaseCostInOtherContexts",
                                      Call)
             << "Not inlining. Cost of inlining '" << NV("Callee", Callee)
             << "' increases the cost of inlining '" << NV("Caller", Caller)
             << "' in other contexts";
    });
    setInlineRemark(CB, "deferred");
    return std::nullopt;
  }

  LLVM_DEBUG(dbgs() << "    Inlining " << inlineCostStr(IC) << ", Call: " << CB
                    << '\n');
  return IC;
}

std::string llvm::formatCallSiteLocation(DebugLoc DLoc,
                                         const CallSiteFormat &Format) {
  std::string Buffer;
  raw_string_ostream CallSiteLoc(Buffer);
  bool First = true;
  for (DILocation *DIL = DLoc.get(); DIL; DIL = DIL->getInlinedAt()) {
    if (!First)
      CallSiteLoc << " @ ";
    // Note that negative line offset is actually possible, but we use
    // unsigned int to match line offset representation in remarks so
    // it's directly consumable by relay advisor.
    uint32_t Offset =
        DIL->getLine() - DIL->getScope()->getSubprogram()->getLine();
    uint32_t Discriminator = DIL->getBaseDiscriminator();
    StringRef Name = DIL->getScope()->getSubprogram()->getLinkageName();
    if (Name.empty())
      Name = DIL->getScope()->getSubprogram()->getName();
    CallSiteLoc << Name.str() << ":" << llvm::utostr(Offset);
    if (Format.outputColumn())
      CallSiteLoc << ":" << llvm::utostr(DIL->getColumn());
    if (Format.outputDiscriminator() && Discriminator)
      CallSiteLoc << "." << llvm::utostr(Discriminator);
    First = false;
  }

  return CallSiteLoc.str();
}

void llvm::addLocationToRemarks(OptimizationRemark &Remark, DebugLoc DLoc) {
  if (!DLoc) {
    return;
  }

  bool First = true;
  Remark << " at callsite ";
  for (DILocation *DIL = DLoc.get(); DIL; DIL = DIL->getInlinedAt()) {
    if (!First)
      Remark << " @ ";
    unsigned int Offset = DIL->getLine();
    Offset -= DIL->getScope()->getSubprogram()->getLine();
    unsigned int Discriminator = DIL->getBaseDiscriminator();
    StringRef Name = DIL->getScope()->getSubprogram()->getLinkageName();
    if (Name.empty())
      Name = DIL->getScope()->getSubprogram()->getName();
    Remark << Name << ":" << ore::NV("Line", Offset) << ":"
           << ore::NV("Column", DIL->getColumn());
    if (Discriminator)
      Remark << "." << ore::NV("Disc", Discriminator);
    First = false;
  }

  Remark << ";";
}

void llvm::emitInlinedInto(
    OptimizationRemarkEmitter &ORE, DebugLoc DLoc, const BasicBlock *Block,
    const Function &Callee, const Function &Caller, bool AlwaysInline,
    function_ref<void(OptimizationRemark &)> ExtraContext,
    const char *PassName) {
  ORE.emit([&]() {
    StringRef RemarkName = AlwaysInline ? "AlwaysInline" : "Inlined";
    OptimizationRemark Remark(PassName ? PassName : DEBUG_TYPE, RemarkName,
                              DLoc, Block);
    Remark << "'" << ore::NV("Callee", &Callee) << "' inlined into '"
           << ore::NV("Caller", &Caller) << "'";
    if (ExtraContext)
      ExtraContext(Remark);
    addLocationToRemarks(Remark, DLoc);
    return Remark;
  });
}

void llvm::emitInlinedIntoBasedOnCost(
    OptimizationRemarkEmitter &ORE, DebugLoc DLoc, const BasicBlock *Block,
    const Function &Callee, const Function &Caller, const InlineCost &IC,
    bool ForProfileContext, const char *PassName) {
  llvm::emitInlinedInto(
      ORE, DLoc, Block, Callee, Caller, IC.isAlways(),
      [&](OptimizationRemark &Remark) {
        if (ForProfileContext)
          Remark << " to match profiling context";
        Remark << " with " << IC;
      },
      PassName);
}

InlineAdvisor::InlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                             std::optional<InlineContext> IC)
    : M(M), FAM(FAM), IC(IC),
      AnnotatedInlinePassName((IC && AnnotateInlinePhase)
                                  ? llvm::AnnotateInlinePassName(*IC)
                                  : DEBUG_TYPE) {
  if (InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No) {
    ImportedFunctionsStats =
        std::make_unique<ImportedFunctionsInliningStatistics>();
    ImportedFunctionsStats->setModuleInfo(M);
  }
}

InlineAdvisor::~InlineAdvisor() {
  if (ImportedFunctionsStats) {
    assert(InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No);
    ImportedFunctionsStats->dump(InlinerFunctionImportStats ==
                                 InlinerFunctionImportStatsOpts::Verbose);
  }
}

std::unique_ptr<InlineAdvice> InlineAdvisor::getMandatoryAdvice(CallBase &CB,
                                                                bool Advice) {
  return std::make_unique<MandatoryInlineAdvice>(this, CB, getCallerORE(CB),
                                                 Advice);
}

static inline const char *getLTOPhase(ThinOrFullLTOPhase LTOPhase) {
  switch (LTOPhase) {
  case (ThinOrFullLTOPhase::None):
    return "main";
  case (ThinOrFullLTOPhase::ThinLTOPreLink):
  case (ThinOrFullLTOPhase::FullLTOPreLink):
    return "prelink";
  case (ThinOrFullLTOPhase::ThinLTOPostLink):
  case (ThinOrFullLTOPhase::FullLTOPostLink):
    return "postlink";
  }
  llvm_unreachable("unreachable");
}

static inline const char *getInlineAdvisorContext(InlinePass IP) {
  switch (IP) {
  case (InlinePass::AlwaysInliner):
    return "always-inline";
  case (InlinePass::CGSCCInliner):
    return "cgscc-inline";
  case (InlinePass::EarlyInliner):
    return "early-inline";
  case (InlinePass::MLInliner):
    return "ml-inline";
  case (InlinePass::ModuleInliner):
    return "module-inline";
  case (InlinePass::ReplayCGSCCInliner):
    return "replay-cgscc-inline";
  case (InlinePass::ReplaySampleProfileInliner):
    return "replay-sample-profile-inline";
  case (InlinePass::SampleProfileInliner):
    return "sample-profile-inline";
  }

  llvm_unreachable("unreachable");
}

std::string llvm::AnnotateInlinePassName(InlineContext IC) {
  return std::string(getLTOPhase(IC.LTOPhase)) + "-" +
         std::string(getInlineAdvisorContext(IC.Pass));
}

InlineAdvisor::MandatoryInliningKind
InlineAdvisor::getMandatoryKind(CallBase &CB, FunctionAnalysisManager &FAM,
                                OptimizationRemarkEmitter &ORE) {
  auto &Callee = *CB.getCalledFunction();

  auto GetTLI = [&](Function &F) -> const TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };

  auto &TIR = FAM.getResult<TargetIRAnalysis>(Callee);

  auto TrivialDecision =
      llvm::getAttributeBasedInliningDecision(CB, &Callee, TIR, GetTLI);

  if (TrivialDecision) {
    if (TrivialDecision->isSuccess())
      return MandatoryInliningKind::Always;
    else
      return MandatoryInliningKind::Never;
  }
  return MandatoryInliningKind::NotMandatory;
}

std::unique_ptr<InlineAdvice> InlineAdvisor::getAdvice(CallBase &CB,
                                                       bool MandatoryOnly) {
  if (!MandatoryOnly)
    return getAdviceImpl(CB);
  bool Advice = CB.getCaller() != CB.getCalledFunction() &&
                MandatoryInliningKind::Always ==
                    getMandatoryKind(CB, FAM, getCallerORE(CB));
  return getMandatoryAdvice(CB, Advice);
}

OptimizationRemarkEmitter &InlineAdvisor::getCallerORE(CallBase &CB) {
  return FAM.getResult<OptimizationRemarkEmitterAnalysis>(*CB.getCaller());
}

PreservedAnalyses
InlineAdvisorAnalysisPrinterPass::run(Module &M, ModuleAnalysisManager &MAM) {
  const auto *IA = MAM.getCachedResult<InlineAdvisorAnalysis>(M);
  if (!IA)
    OS << "No Inline Advisor\n";
  else
    IA->getAdvisor()->print(OS);
  return PreservedAnalyses::all();
}

PreservedAnalyses InlineAdvisorAnalysisPrinterPass::run(
    LazyCallGraph::SCC &InitialC, CGSCCAnalysisManager &AM, LazyCallGraph &CG,
    CGSCCUpdateResult &UR) {
  const auto &MAMProxy =
      AM.getResult<ModuleAnalysisManagerCGSCCProxy>(InitialC, CG);

  if (InitialC.size() == 0) {
    OS << "SCC is empty!\n";
    return PreservedAnalyses::all();
  }
  Module &M = *InitialC.begin()->getFunction().getParent();
  const auto *IA = MAMProxy.getCachedResult<InlineAdvisorAnalysis>(M);
  if (!IA)
    OS << "No Inline Advisor\n";
  else
    IA->getAdvisor()->print(OS);
  return PreservedAnalyses::all();
}
