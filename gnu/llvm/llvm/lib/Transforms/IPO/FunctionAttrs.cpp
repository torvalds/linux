//===- FunctionAttrs.cpp - Pass which marks functions attributes ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements interprocedural passes which walk the
/// call-graph deducing and/or propagating function attributes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <iterator>
#include <map>
#include <optional>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "function-attrs"

STATISTIC(NumMemoryAttr, "Number of functions with improved memory attribute");
STATISTIC(NumNoCapture, "Number of arguments marked nocapture");
STATISTIC(NumReturned, "Number of arguments marked returned");
STATISTIC(NumReadNoneArg, "Number of arguments marked readnone");
STATISTIC(NumReadOnlyArg, "Number of arguments marked readonly");
STATISTIC(NumWriteOnlyArg, "Number of arguments marked writeonly");
STATISTIC(NumNoAlias, "Number of function returns marked noalias");
STATISTIC(NumNonNullReturn, "Number of function returns marked nonnull");
STATISTIC(NumNoUndefReturn, "Number of function returns marked noundef");
STATISTIC(NumNoRecurse, "Number of functions marked as norecurse");
STATISTIC(NumNoUnwind, "Number of functions marked as nounwind");
STATISTIC(NumNoFree, "Number of functions marked as nofree");
STATISTIC(NumWillReturn, "Number of functions marked as willreturn");
STATISTIC(NumNoSync, "Number of functions marked as nosync");

STATISTIC(NumThinLinkNoRecurse,
          "Number of functions marked as norecurse during thinlink");
STATISTIC(NumThinLinkNoUnwind,
          "Number of functions marked as nounwind during thinlink");

static cl::opt<bool> EnableNonnullArgPropagation(
    "enable-nonnull-arg-prop", cl::init(true), cl::Hidden,
    cl::desc("Try to propagate nonnull argument attributes from callsites to "
             "caller functions."));

static cl::opt<bool> DisableNoUnwindInference(
    "disable-nounwind-inference", cl::Hidden,
    cl::desc("Stop inferring nounwind attribute during function-attrs pass"));

static cl::opt<bool> DisableNoFreeInference(
    "disable-nofree-inference", cl::Hidden,
    cl::desc("Stop inferring nofree attribute during function-attrs pass"));

static cl::opt<bool> DisableThinLTOPropagation(
    "disable-thinlto-funcattrs", cl::init(true), cl::Hidden,
    cl::desc("Don't propagate function-attrs in thinLTO"));

namespace {

using SCCNodeSet = SmallSetVector<Function *, 8>;

} // end anonymous namespace

static void addLocAccess(MemoryEffects &ME, const MemoryLocation &Loc,
                         ModRefInfo MR, AAResults &AAR) {
  // Ignore accesses to known-invariant or local memory.
  MR &= AAR.getModRefInfoMask(Loc, /*IgnoreLocal=*/true);
  if (isNoModRef(MR))
    return;

  const Value *UO = getUnderlyingObject(Loc.Ptr);
  assert(!isa<AllocaInst>(UO) &&
         "Should have been handled by getModRefInfoMask()");
  if (isa<Argument>(UO)) {
    ME |= MemoryEffects::argMemOnly(MR);
    return;
  }

  // If it's not an identified object, it might be an argument.
  if (!isIdentifiedObject(UO))
    ME |= MemoryEffects::argMemOnly(MR);
  ME |= MemoryEffects(IRMemLocation::Other, MR);
}

static void addArgLocs(MemoryEffects &ME, const CallBase *Call,
                       ModRefInfo ArgMR, AAResults &AAR) {
  for (const Value *Arg : Call->args()) {
    if (!Arg->getType()->isPtrOrPtrVectorTy())
      continue;

    addLocAccess(ME,
                 MemoryLocation::getBeforeOrAfter(Arg, Call->getAAMetadata()),
                 ArgMR, AAR);
  }
}

/// Returns the memory access attribute for function F using AAR for AA results,
/// where SCCNodes is the current SCC.
///
/// If ThisBody is true, this function may examine the function body and will
/// return a result pertaining to this copy of the function. If it is false, the
/// result will be based only on AA results for the function declaration; it
/// will be assumed that some other (perhaps less optimized) version of the
/// function may be selected at link time.
///
/// The return value is split into two parts: Memory effects that always apply,
/// and additional memory effects that apply if any of the functions in the SCC
/// can access argmem.
static std::pair<MemoryEffects, MemoryEffects>
checkFunctionMemoryAccess(Function &F, bool ThisBody, AAResults &AAR,
                          const SCCNodeSet &SCCNodes) {
  MemoryEffects OrigME = AAR.getMemoryEffects(&F);
  if (OrigME.doesNotAccessMemory())
    // Already perfect!
    return {OrigME, MemoryEffects::none()};

  if (!ThisBody)
    return {OrigME, MemoryEffects::none()};

  MemoryEffects ME = MemoryEffects::none();
  // Additional locations accessed if the SCC accesses argmem.
  MemoryEffects RecursiveArgME = MemoryEffects::none();

  // Inalloca and preallocated arguments are always clobbered by the call.
  if (F.getAttributes().hasAttrSomewhere(Attribute::InAlloca) ||
      F.getAttributes().hasAttrSomewhere(Attribute::Preallocated))
    ME |= MemoryEffects::argMemOnly(ModRefInfo::ModRef);

  // Scan the function body for instructions that may read or write memory.
  for (Instruction &I : instructions(F)) {
    // Some instructions can be ignored even if they read or write memory.
    // Detect these now, skipping to the next instruction if one is found.
    if (auto *Call = dyn_cast<CallBase>(&I)) {
      // We can optimistically ignore calls to functions in the same SCC, with
      // two caveats:
      //  * Calls with operand bundles may have additional effects.
      //  * Argument memory accesses may imply additional effects depending on
      //    what the argument location is.
      if (!Call->hasOperandBundles() && Call->getCalledFunction() &&
          SCCNodes.count(Call->getCalledFunction())) {
        // Keep track of which additional locations are accessed if the SCC
        // turns out to access argmem.
        addArgLocs(RecursiveArgME, Call, ModRefInfo::ModRef, AAR);
        continue;
      }

      MemoryEffects CallME = AAR.getMemoryEffects(Call);

      // If the call doesn't access memory, we're done.
      if (CallME.doesNotAccessMemory())
        continue;

      // A pseudo probe call shouldn't change any function attribute since it
      // doesn't translate to a real instruction. It comes with a memory access
      // tag to prevent itself being removed by optimizations and not block
      // other instructions being optimized.
      if (isa<PseudoProbeInst>(I))
        continue;

      ME |= CallME.getWithoutLoc(IRMemLocation::ArgMem);

      // If the call accesses captured memory (currently part of "other") and
      // an argument is captured (currently not tracked), then it may also
      // access argument memory.
      ModRefInfo OtherMR = CallME.getModRef(IRMemLocation::Other);
      ME |= MemoryEffects::argMemOnly(OtherMR);

      // Check whether all pointer arguments point to local memory, and
      // ignore calls that only access local memory.
      ModRefInfo ArgMR = CallME.getModRef(IRMemLocation::ArgMem);
      if (ArgMR != ModRefInfo::NoModRef)
        addArgLocs(ME, Call, ArgMR, AAR);
      continue;
    }

    ModRefInfo MR = ModRefInfo::NoModRef;
    if (I.mayWriteToMemory())
      MR |= ModRefInfo::Mod;
    if (I.mayReadFromMemory())
      MR |= ModRefInfo::Ref;
    if (MR == ModRefInfo::NoModRef)
      continue;

    std::optional<MemoryLocation> Loc = MemoryLocation::getOrNone(&I);
    if (!Loc) {
      // If no location is known, conservatively assume anything can be
      // accessed.
      ME |= MemoryEffects(MR);
      continue;
    }

    // Volatile operations may access inaccessible memory.
    if (I.isVolatile())
      ME |= MemoryEffects::inaccessibleMemOnly(MR);

    addLocAccess(ME, *Loc, MR, AAR);
  }

  return {OrigME & ME, RecursiveArgME};
}

MemoryEffects llvm::computeFunctionBodyMemoryAccess(Function &F,
                                                    AAResults &AAR) {
  return checkFunctionMemoryAccess(F, /*ThisBody=*/true, AAR, {}).first;
}

/// Deduce readonly/readnone/writeonly attributes for the SCC.
template <typename AARGetterT>
static void addMemoryAttrs(const SCCNodeSet &SCCNodes, AARGetterT &&AARGetter,
                           SmallSet<Function *, 8> &Changed) {
  MemoryEffects ME = MemoryEffects::none();
  MemoryEffects RecursiveArgME = MemoryEffects::none();
  for (Function *F : SCCNodes) {
    // Call the callable parameter to look up AA results for this function.
    AAResults &AAR = AARGetter(*F);
    // Non-exact function definitions may not be selected at link time, and an
    // alternative version that writes to memory may be selected.  See the
    // comment on GlobalValue::isDefinitionExact for more details.
    auto [FnME, FnRecursiveArgME] =
        checkFunctionMemoryAccess(*F, F->hasExactDefinition(), AAR, SCCNodes);
    ME |= FnME;
    RecursiveArgME |= FnRecursiveArgME;
    // Reached bottom of the lattice, we will not be able to improve the result.
    if (ME == MemoryEffects::unknown())
      return;
  }

  // If the SCC accesses argmem, add recursive accesses resulting from that.
  ModRefInfo ArgMR = ME.getModRef(IRMemLocation::ArgMem);
  if (ArgMR != ModRefInfo::NoModRef)
    ME |= RecursiveArgME & MemoryEffects(ArgMR);

  for (Function *F : SCCNodes) {
    MemoryEffects OldME = F->getMemoryEffects();
    MemoryEffects NewME = ME & OldME;
    if (NewME != OldME) {
      ++NumMemoryAttr;
      F->setMemoryEffects(NewME);
      // Remove conflicting writable attributes.
      if (!isModSet(NewME.getModRef(IRMemLocation::ArgMem)))
        for (Argument &A : F->args())
          A.removeAttr(Attribute::Writable);
      Changed.insert(F);
    }
  }
}

// Compute definitive function attributes for a function taking into account
// prevailing definitions and linkage types
static FunctionSummary *calculatePrevailingSummary(
    ValueInfo VI,
    DenseMap<ValueInfo, FunctionSummary *> &CachedPrevailingSummary,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        IsPrevailing) {

  if (CachedPrevailingSummary.count(VI))
    return CachedPrevailingSummary[VI];

  /// At this point, prevailing symbols have been resolved. The following leads
  /// to returning a conservative result:
  /// - Multiple instances with local linkage. Normally local linkage would be
  ///   unique per module
  ///   as the GUID includes the module path. We could have a guid alias if
  ///   there wasn't any distinguishing path when each file was compiled, but
  ///   that should be rare so we'll punt on those.

  /// These next 2 cases should not happen and will assert:
  /// - Multiple instances with external linkage. This should be caught in
  ///   symbol resolution
  /// - Non-existent FunctionSummary for Aliasee. This presents a hole in our
  ///   knowledge meaning we have to go conservative.

  /// Otherwise, we calculate attributes for a function as:
  ///   1. If we have a local linkage, take its attributes. If there's somehow
  ///      multiple, bail and go conservative.
  ///   2. If we have an external/WeakODR/LinkOnceODR linkage check that it is
  ///      prevailing, take its attributes.
  ///   3. If we have a Weak/LinkOnce linkage the copies can have semantic
  ///      differences. However, if the prevailing copy is known it will be used
  ///      so take its attributes. If the prevailing copy is in a native file
  ///      all IR copies will be dead and propagation will go conservative.
  ///   4. AvailableExternally summaries without a prevailing copy are known to
  ///      occur in a couple of circumstances:
  ///      a. An internal function gets imported due to its caller getting
  ///         imported, it becomes AvailableExternally but no prevailing
  ///         definition exists. Because it has to get imported along with its
  ///         caller the attributes will be captured by propagating on its
  ///         caller.
  ///      b. C++11 [temp.explicit]p10 can generate AvailableExternally
  ///         definitions of explicitly instanced template declarations
  ///         for inlining which are ultimately dropped from the TU. Since this
  ///         is localized to the TU the attributes will have already made it to
  ///         the callers.
  ///      These are edge cases and already captured by their callers so we
  ///      ignore these for now. If they become relevant to optimize in the
  ///      future this can be revisited.
  ///   5. Otherwise, go conservative.

  CachedPrevailingSummary[VI] = nullptr;
  FunctionSummary *Local = nullptr;
  FunctionSummary *Prevailing = nullptr;

  for (const auto &GVS : VI.getSummaryList()) {
    if (!GVS->isLive())
      continue;

    FunctionSummary *FS = dyn_cast<FunctionSummary>(GVS->getBaseObject());
    // Virtual and Unknown (e.g. indirect) calls require going conservative
    if (!FS || FS->fflags().HasUnknownCall)
      return nullptr;

    const auto &Linkage = GVS->linkage();
    if (GlobalValue::isLocalLinkage(Linkage)) {
      if (Local) {
        LLVM_DEBUG(
            dbgs()
            << "ThinLTO FunctionAttrs: Multiple Local Linkage, bailing on "
               "function "
            << VI.name() << " from " << FS->modulePath() << ". Previous module "
            << Local->modulePath() << "\n");
        return nullptr;
      }
      Local = FS;
    } else if (GlobalValue::isExternalLinkage(Linkage)) {
      assert(IsPrevailing(VI.getGUID(), GVS.get()));
      Prevailing = FS;
      break;
    } else if (GlobalValue::isWeakODRLinkage(Linkage) ||
               GlobalValue::isLinkOnceODRLinkage(Linkage) ||
               GlobalValue::isWeakAnyLinkage(Linkage) ||
               GlobalValue::isLinkOnceAnyLinkage(Linkage)) {
      if (IsPrevailing(VI.getGUID(), GVS.get())) {
        Prevailing = FS;
        break;
      }
    } else if (GlobalValue::isAvailableExternallyLinkage(Linkage)) {
      // TODO: Handle these cases if they become meaningful
      continue;
    }
  }

  if (Local) {
    assert(!Prevailing);
    CachedPrevailingSummary[VI] = Local;
  } else if (Prevailing) {
    assert(!Local);
    CachedPrevailingSummary[VI] = Prevailing;
  }

  return CachedPrevailingSummary[VI];
}

bool llvm::thinLTOPropagateFunctionAttrs(
    ModuleSummaryIndex &Index,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        IsPrevailing) {
  // TODO: implement addNoAliasAttrs once
  // there's more information about the return type in the summary
  if (DisableThinLTOPropagation)
    return false;

  DenseMap<ValueInfo, FunctionSummary *> CachedPrevailingSummary;
  bool Changed = false;

  auto PropagateAttributes = [&](std::vector<ValueInfo> &SCCNodes) {
    // Assume we can propagate unless we discover otherwise
    FunctionSummary::FFlags InferredFlags;
    InferredFlags.NoRecurse = (SCCNodes.size() == 1);
    InferredFlags.NoUnwind = true;

    for (auto &V : SCCNodes) {
      FunctionSummary *CallerSummary =
          calculatePrevailingSummary(V, CachedPrevailingSummary, IsPrevailing);

      // Function summaries can fail to contain information such as declarations
      if (!CallerSummary)
        return;

      if (CallerSummary->fflags().MayThrow)
        InferredFlags.NoUnwind = false;

      for (const auto &Callee : CallerSummary->calls()) {
        FunctionSummary *CalleeSummary = calculatePrevailingSummary(
            Callee.first, CachedPrevailingSummary, IsPrevailing);

        if (!CalleeSummary)
          return;

        if (!CalleeSummary->fflags().NoRecurse)
          InferredFlags.NoRecurse = false;

        if (!CalleeSummary->fflags().NoUnwind)
          InferredFlags.NoUnwind = false;

        if (!InferredFlags.NoUnwind && !InferredFlags.NoRecurse)
          break;
      }
    }

    if (InferredFlags.NoUnwind || InferredFlags.NoRecurse) {
      Changed = true;
      for (auto &V : SCCNodes) {
        if (InferredFlags.NoRecurse) {
          LLVM_DEBUG(dbgs() << "ThinLTO FunctionAttrs: Propagated NoRecurse to "
                            << V.name() << "\n");
          ++NumThinLinkNoRecurse;
        }

        if (InferredFlags.NoUnwind) {
          LLVM_DEBUG(dbgs() << "ThinLTO FunctionAttrs: Propagated NoUnwind to "
                            << V.name() << "\n");
          ++NumThinLinkNoUnwind;
        }

        for (const auto &S : V.getSummaryList()) {
          if (auto *FS = dyn_cast<FunctionSummary>(S.get())) {
            if (InferredFlags.NoRecurse)
              FS->setNoRecurse();

            if (InferredFlags.NoUnwind)
              FS->setNoUnwind();
          }
        }
      }
    }
  };

  // Call propagation functions on each SCC in the Index
  for (scc_iterator<ModuleSummaryIndex *> I = scc_begin(&Index); !I.isAtEnd();
       ++I) {
    std::vector<ValueInfo> Nodes(*I);
    PropagateAttributes(Nodes);
  }
  return Changed;
}

namespace {

/// For a given pointer Argument, this retains a list of Arguments of functions
/// in the same SCC that the pointer data flows into. We use this to build an
/// SCC of the arguments.
struct ArgumentGraphNode {
  Argument *Definition;
  SmallVector<ArgumentGraphNode *, 4> Uses;
};

class ArgumentGraph {
  // We store pointers to ArgumentGraphNode objects, so it's important that
  // that they not move around upon insert.
  using ArgumentMapTy = std::map<Argument *, ArgumentGraphNode>;

  ArgumentMapTy ArgumentMap;

  // There is no root node for the argument graph, in fact:
  //   void f(int *x, int *y) { if (...) f(x, y); }
  // is an example where the graph is disconnected. The SCCIterator requires a
  // single entry point, so we maintain a fake ("synthetic") root node that
  // uses every node. Because the graph is directed and nothing points into
  // the root, it will not participate in any SCCs (except for its own).
  ArgumentGraphNode SyntheticRoot;

public:
  ArgumentGraph() { SyntheticRoot.Definition = nullptr; }

  using iterator = SmallVectorImpl<ArgumentGraphNode *>::iterator;

  iterator begin() { return SyntheticRoot.Uses.begin(); }
  iterator end() { return SyntheticRoot.Uses.end(); }
  ArgumentGraphNode *getEntryNode() { return &SyntheticRoot; }

  ArgumentGraphNode *operator[](Argument *A) {
    ArgumentGraphNode &Node = ArgumentMap[A];
    Node.Definition = A;
    SyntheticRoot.Uses.push_back(&Node);
    return &Node;
  }
};

/// This tracker checks whether callees are in the SCC, and if so it does not
/// consider that a capture, instead adding it to the "Uses" list and
/// continuing with the analysis.
struct ArgumentUsesTracker : public CaptureTracker {
  ArgumentUsesTracker(const SCCNodeSet &SCCNodes) : SCCNodes(SCCNodes) {}

  void tooManyUses() override { Captured = true; }

  bool captured(const Use *U) override {
    CallBase *CB = dyn_cast<CallBase>(U->getUser());
    if (!CB) {
      Captured = true;
      return true;
    }

    Function *F = CB->getCalledFunction();
    if (!F || !F->hasExactDefinition() || !SCCNodes.count(F)) {
      Captured = true;
      return true;
    }

    assert(!CB->isCallee(U) && "callee operand reported captured?");
    const unsigned UseIndex = CB->getDataOperandNo(U);
    if (UseIndex >= CB->arg_size()) {
      // Data operand, but not a argument operand -- must be a bundle operand
      assert(CB->hasOperandBundles() && "Must be!");

      // CaptureTracking told us that we're being captured by an operand bundle
      // use.  In this case it does not matter if the callee is within our SCC
      // or not -- we've been captured in some unknown way, and we have to be
      // conservative.
      Captured = true;
      return true;
    }

    if (UseIndex >= F->arg_size()) {
      assert(F->isVarArg() && "More params than args in non-varargs call");
      Captured = true;
      return true;
    }

    Uses.push_back(&*std::next(F->arg_begin(), UseIndex));
    return false;
  }

  // True only if certainly captured (used outside our SCC).
  bool Captured = false;

  // Uses within our SCC.
  SmallVector<Argument *, 4> Uses;

  const SCCNodeSet &SCCNodes;
};

} // end anonymous namespace

namespace llvm {

template <> struct GraphTraits<ArgumentGraphNode *> {
  using NodeRef = ArgumentGraphNode *;
  using ChildIteratorType = SmallVectorImpl<ArgumentGraphNode *>::iterator;

  static NodeRef getEntryNode(NodeRef A) { return A; }
  static ChildIteratorType child_begin(NodeRef N) { return N->Uses.begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->Uses.end(); }
};

template <>
struct GraphTraits<ArgumentGraph *> : public GraphTraits<ArgumentGraphNode *> {
  static NodeRef getEntryNode(ArgumentGraph *AG) { return AG->getEntryNode(); }

  static ChildIteratorType nodes_begin(ArgumentGraph *AG) {
    return AG->begin();
  }

  static ChildIteratorType nodes_end(ArgumentGraph *AG) { return AG->end(); }
};

} // end namespace llvm

/// Returns Attribute::None, Attribute::ReadOnly or Attribute::ReadNone.
static Attribute::AttrKind
determinePointerAccessAttrs(Argument *A,
                            const SmallPtrSet<Argument *, 8> &SCCNodes) {
  SmallVector<Use *, 32> Worklist;
  SmallPtrSet<Use *, 32> Visited;

  // inalloca arguments are always clobbered by the call.
  if (A->hasInAllocaAttr() || A->hasPreallocatedAttr())
    return Attribute::None;

  bool IsRead = false;
  bool IsWrite = false;

  for (Use &U : A->uses()) {
    Visited.insert(&U);
    Worklist.push_back(&U);
  }

  while (!Worklist.empty()) {
    if (IsWrite && IsRead)
      // No point in searching further..
      return Attribute::None;

    Use *U = Worklist.pop_back_val();
    Instruction *I = cast<Instruction>(U->getUser());

    switch (I->getOpcode()) {
    case Instruction::BitCast:
    case Instruction::GetElementPtr:
    case Instruction::PHI:
    case Instruction::Select:
    case Instruction::AddrSpaceCast:
      // The original value is not read/written via this if the new value isn't.
      for (Use &UU : I->uses())
        if (Visited.insert(&UU).second)
          Worklist.push_back(&UU);
      break;

    case Instruction::Call:
    case Instruction::Invoke: {
      CallBase &CB = cast<CallBase>(*I);
      if (CB.isCallee(U)) {
        IsRead = true;
        // Note that indirect calls do not capture, see comment in
        // CaptureTracking for context
        continue;
      }

      // Given we've explictily handled the callee operand above, what's left
      // must be a data operand (e.g. argument or operand bundle)
      const unsigned UseIndex = CB.getDataOperandNo(U);

      // Some intrinsics (for instance ptrmask) do not capture their results,
      // but return results thas alias their pointer argument, and thus should
      // be handled like GEP or addrspacecast above.
      if (isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(
              &CB, /*MustPreserveNullness=*/false)) {
        for (Use &UU : CB.uses())
          if (Visited.insert(&UU).second)
            Worklist.push_back(&UU);
      } else if (!CB.doesNotCapture(UseIndex)) {
        if (!CB.onlyReadsMemory())
          // If the callee can save a copy into other memory, then simply
          // scanning uses of the call is insufficient.  We have no way
          // of tracking copies of the pointer through memory to see
          // if a reloaded copy is written to, thus we must give up.
          return Attribute::None;
        // Push users for processing once we finish this one
        if (!I->getType()->isVoidTy())
          for (Use &UU : I->uses())
            if (Visited.insert(&UU).second)
              Worklist.push_back(&UU);
      }

      ModRefInfo ArgMR = CB.getMemoryEffects().getModRef(IRMemLocation::ArgMem);
      if (isNoModRef(ArgMR))
        continue;

      if (Function *F = CB.getCalledFunction())
        if (CB.isArgOperand(U) && UseIndex < F->arg_size() &&
            SCCNodes.count(F->getArg(UseIndex)))
          // This is an argument which is part of the speculative SCC.  Note
          // that only operands corresponding to formal arguments of the callee
          // can participate in the speculation.
          break;

      // The accessors used on call site here do the right thing for calls and
      // invokes with operand bundles.
      if (CB.doesNotAccessMemory(UseIndex)) {
        /* nop */
      } else if (!isModSet(ArgMR) || CB.onlyReadsMemory(UseIndex)) {
        IsRead = true;
      } else if (!isRefSet(ArgMR) ||
                 CB.dataOperandHasImpliedAttr(UseIndex, Attribute::WriteOnly)) {
        IsWrite = true;
      } else {
        return Attribute::None;
      }
      break;
    }

    case Instruction::Load:
      // A volatile load has side effects beyond what readonly can be relied
      // upon.
      if (cast<LoadInst>(I)->isVolatile())
        return Attribute::None;

      IsRead = true;
      break;

    case Instruction::Store:
      if (cast<StoreInst>(I)->getValueOperand() == *U)
        // untrackable capture
        return Attribute::None;

      // A volatile store has side effects beyond what writeonly can be relied
      // upon.
      if (cast<StoreInst>(I)->isVolatile())
        return Attribute::None;

      IsWrite = true;
      break;

    case Instruction::ICmp:
    case Instruction::Ret:
      break;

    default:
      return Attribute::None;
    }
  }

  if (IsWrite && IsRead)
    return Attribute::None;
  else if (IsRead)
    return Attribute::ReadOnly;
  else if (IsWrite)
    return Attribute::WriteOnly;
  else
    return Attribute::ReadNone;
}

/// Deduce returned attributes for the SCC.
static void addArgumentReturnedAttrs(const SCCNodeSet &SCCNodes,
                                     SmallSet<Function *, 8> &Changed) {
  // Check each function in turn, determining if an argument is always returned.
  for (Function *F : SCCNodes) {
    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      continue;

    if (F->getReturnType()->isVoidTy())
      continue;

    // There is nothing to do if an argument is already marked as 'returned'.
    if (F->getAttributes().hasAttrSomewhere(Attribute::Returned))
      continue;

    auto FindRetArg = [&]() -> Argument * {
      Argument *RetArg = nullptr;
      for (BasicBlock &BB : *F)
        if (auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator())) {
          // Note that stripPointerCasts should look through functions with
          // returned arguments.
          auto *RetVal =
              dyn_cast<Argument>(Ret->getReturnValue()->stripPointerCasts());
          if (!RetVal || RetVal->getType() != F->getReturnType())
            return nullptr;

          if (!RetArg)
            RetArg = RetVal;
          else if (RetArg != RetVal)
            return nullptr;
        }

      return RetArg;
    };

    if (Argument *RetArg = FindRetArg()) {
      RetArg->addAttr(Attribute::Returned);
      ++NumReturned;
      Changed.insert(F);
    }
  }
}

/// If a callsite has arguments that are also arguments to the parent function,
/// try to propagate attributes from the callsite's arguments to the parent's
/// arguments. This may be important because inlining can cause information loss
/// when attribute knowledge disappears with the inlined call.
static bool addArgumentAttrsFromCallsites(Function &F) {
  if (!EnableNonnullArgPropagation)
    return false;

  bool Changed = false;

  // For an argument attribute to transfer from a callsite to the parent, the
  // call must be guaranteed to execute every time the parent is called.
  // Conservatively, just check for calls in the entry block that are guaranteed
  // to execute.
  // TODO: This could be enhanced by testing if the callsite post-dominates the
  // entry block or by doing simple forward walks or backward walks to the
  // callsite.
  BasicBlock &Entry = F.getEntryBlock();
  for (Instruction &I : Entry) {
    if (auto *CB = dyn_cast<CallBase>(&I)) {
      if (auto *CalledFunc = CB->getCalledFunction()) {
        for (auto &CSArg : CalledFunc->args()) {
          if (!CSArg.hasNonNullAttr(/* AllowUndefOrPoison */ false))
            continue;

          // If the non-null callsite argument operand is an argument to 'F'
          // (the caller) and the call is guaranteed to execute, then the value
          // must be non-null throughout 'F'.
          auto *FArg = dyn_cast<Argument>(CB->getArgOperand(CSArg.getArgNo()));
          if (FArg && !FArg->hasNonNullAttr()) {
            FArg->addAttr(Attribute::NonNull);
            Changed = true;
          }
        }
      }
    }
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      break;
  }

  return Changed;
}

static bool addAccessAttr(Argument *A, Attribute::AttrKind R) {
  assert((R == Attribute::ReadOnly || R == Attribute::ReadNone ||
          R == Attribute::WriteOnly)
         && "Must be an access attribute.");
  assert(A && "Argument must not be null.");

  // If the argument already has the attribute, nothing needs to be done.
  if (A->hasAttribute(R))
      return false;

  // Otherwise, remove potentially conflicting attribute, add the new one,
  // and update statistics.
  A->removeAttr(Attribute::WriteOnly);
  A->removeAttr(Attribute::ReadOnly);
  A->removeAttr(Attribute::ReadNone);
  // Remove conflicting writable attribute.
  if (R == Attribute::ReadNone || R == Attribute::ReadOnly)
    A->removeAttr(Attribute::Writable);
  A->addAttr(R);
  if (R == Attribute::ReadOnly)
    ++NumReadOnlyArg;
  else if (R == Attribute::WriteOnly)
    ++NumWriteOnlyArg;
  else
    ++NumReadNoneArg;
  return true;
}

/// Deduce nocapture attributes for the SCC.
static void addArgumentAttrs(const SCCNodeSet &SCCNodes,
                             SmallSet<Function *, 8> &Changed) {
  ArgumentGraph AG;

  // Check each function in turn, determining which pointer arguments are not
  // captured.
  for (Function *F : SCCNodes) {
    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      continue;

    if (addArgumentAttrsFromCallsites(*F))
      Changed.insert(F);

    // Functions that are readonly (or readnone) and nounwind and don't return
    // a value can't capture arguments. Don't analyze them.
    if (F->onlyReadsMemory() && F->doesNotThrow() &&
        F->getReturnType()->isVoidTy()) {
      for (Argument &A : F->args()) {
        if (A.getType()->isPointerTy() && !A.hasNoCaptureAttr()) {
          A.addAttr(Attribute::NoCapture);
          ++NumNoCapture;
          Changed.insert(F);
        }
      }
      continue;
    }

    for (Argument &A : F->args()) {
      if (!A.getType()->isPointerTy())
        continue;
      bool HasNonLocalUses = false;
      if (!A.hasNoCaptureAttr()) {
        ArgumentUsesTracker Tracker(SCCNodes);
        PointerMayBeCaptured(&A, &Tracker);
        if (!Tracker.Captured) {
          if (Tracker.Uses.empty()) {
            // If it's trivially not captured, mark it nocapture now.
            A.addAttr(Attribute::NoCapture);
            ++NumNoCapture;
            Changed.insert(F);
          } else {
            // If it's not trivially captured and not trivially not captured,
            // then it must be calling into another function in our SCC. Save
            // its particulars for Argument-SCC analysis later.
            ArgumentGraphNode *Node = AG[&A];
            for (Argument *Use : Tracker.Uses) {
              Node->Uses.push_back(AG[Use]);
              if (Use != &A)
                HasNonLocalUses = true;
            }
          }
        }
        // Otherwise, it's captured. Don't bother doing SCC analysis on it.
      }
      if (!HasNonLocalUses && !A.onlyReadsMemory()) {
        // Can we determine that it's readonly/readnone/writeonly without doing
        // an SCC? Note that we don't allow any calls at all here, or else our
        // result will be dependent on the iteration order through the
        // functions in the SCC.
        SmallPtrSet<Argument *, 8> Self;
        Self.insert(&A);
        Attribute::AttrKind R = determinePointerAccessAttrs(&A, Self);
        if (R != Attribute::None)
          if (addAccessAttr(&A, R))
            Changed.insert(F);
      }
    }
  }

  // The graph we've collected is partial because we stopped scanning for
  // argument uses once we solved the argument trivially. These partial nodes
  // show up as ArgumentGraphNode objects with an empty Uses list, and for
  // these nodes the final decision about whether they capture has already been
  // made.  If the definition doesn't have a 'nocapture' attribute by now, it
  // captures.

  for (scc_iterator<ArgumentGraph *> I = scc_begin(&AG); !I.isAtEnd(); ++I) {
    const std::vector<ArgumentGraphNode *> &ArgumentSCC = *I;
    if (ArgumentSCC.size() == 1) {
      if (!ArgumentSCC[0]->Definition)
        continue; // synthetic root node

      // eg. "void f(int* x) { if (...) f(x); }"
      if (ArgumentSCC[0]->Uses.size() == 1 &&
          ArgumentSCC[0]->Uses[0] == ArgumentSCC[0]) {
        Argument *A = ArgumentSCC[0]->Definition;
        A->addAttr(Attribute::NoCapture);
        ++NumNoCapture;
        Changed.insert(A->getParent());

        // Infer the access attributes given the new nocapture one
        SmallPtrSet<Argument *, 8> Self;
        Self.insert(&*A);
        Attribute::AttrKind R = determinePointerAccessAttrs(&*A, Self);
        if (R != Attribute::None)
          addAccessAttr(A, R);
      }
      continue;
    }

    bool SCCCaptured = false;
    for (ArgumentGraphNode *Node : ArgumentSCC) {
      if (Node->Uses.empty() && !Node->Definition->hasNoCaptureAttr()) {
        SCCCaptured = true;
        break;
      }
    }
    if (SCCCaptured)
      continue;

    SmallPtrSet<Argument *, 8> ArgumentSCCNodes;
    // Fill ArgumentSCCNodes with the elements of the ArgumentSCC.  Used for
    // quickly looking up whether a given Argument is in this ArgumentSCC.
    for (ArgumentGraphNode *I : ArgumentSCC) {
      ArgumentSCCNodes.insert(I->Definition);
    }

    for (ArgumentGraphNode *N : ArgumentSCC) {
      for (ArgumentGraphNode *Use : N->Uses) {
        Argument *A = Use->Definition;
        if (A->hasNoCaptureAttr() || ArgumentSCCNodes.count(A))
          continue;
        SCCCaptured = true;
        break;
      }
      if (SCCCaptured)
        break;
    }
    if (SCCCaptured)
      continue;

    for (ArgumentGraphNode *N : ArgumentSCC) {
      Argument *A = N->Definition;
      A->addAttr(Attribute::NoCapture);
      ++NumNoCapture;
      Changed.insert(A->getParent());
    }

    // We also want to compute readonly/readnone/writeonly. With a small number
    // of false negatives, we can assume that any pointer which is captured
    // isn't going to be provably readonly or readnone, since by definition
    // we can't analyze all uses of a captured pointer.
    //
    // The false negatives happen when the pointer is captured by a function
    // that promises readonly/readnone behaviour on the pointer, then the
    // pointer's lifetime ends before anything that writes to arbitrary memory.
    // Also, a readonly/readnone pointer may be returned, but returning a
    // pointer is capturing it.

    auto meetAccessAttr = [](Attribute::AttrKind A, Attribute::AttrKind B) {
      if (A == B)
        return A;
      if (A == Attribute::ReadNone)
        return B;
      if (B == Attribute::ReadNone)
        return A;
      return Attribute::None;
    };

    Attribute::AttrKind AccessAttr = Attribute::ReadNone;
    for (ArgumentGraphNode *N : ArgumentSCC) {
      Argument *A = N->Definition;
      Attribute::AttrKind K = determinePointerAccessAttrs(A, ArgumentSCCNodes);
      AccessAttr = meetAccessAttr(AccessAttr, K);
      if (AccessAttr == Attribute::None)
        break;
    }

    if (AccessAttr != Attribute::None) {
      for (ArgumentGraphNode *N : ArgumentSCC) {
        Argument *A = N->Definition;
        if (addAccessAttr(A, AccessAttr))
          Changed.insert(A->getParent());
      }
    }
  }
}

/// Tests whether a function is "malloc-like".
///
/// A function is "malloc-like" if it returns either null or a pointer that
/// doesn't alias any other pointer visible to the caller.
static bool isFunctionMallocLike(Function *F, const SCCNodeSet &SCCNodes) {
  SmallSetVector<Value *, 8> FlowsToReturn;
  for (BasicBlock &BB : *F)
    if (ReturnInst *Ret = dyn_cast<ReturnInst>(BB.getTerminator()))
      FlowsToReturn.insert(Ret->getReturnValue());

  for (unsigned i = 0; i != FlowsToReturn.size(); ++i) {
    Value *RetVal = FlowsToReturn[i];

    if (Constant *C = dyn_cast<Constant>(RetVal)) {
      if (!C->isNullValue() && !isa<UndefValue>(C))
        return false;

      continue;
    }

    if (isa<Argument>(RetVal))
      return false;

    if (Instruction *RVI = dyn_cast<Instruction>(RetVal))
      switch (RVI->getOpcode()) {
      // Extend the analysis by looking upwards.
      case Instruction::BitCast:
      case Instruction::GetElementPtr:
      case Instruction::AddrSpaceCast:
        FlowsToReturn.insert(RVI->getOperand(0));
        continue;
      case Instruction::Select: {
        SelectInst *SI = cast<SelectInst>(RVI);
        FlowsToReturn.insert(SI->getTrueValue());
        FlowsToReturn.insert(SI->getFalseValue());
        continue;
      }
      case Instruction::PHI: {
        PHINode *PN = cast<PHINode>(RVI);
        for (Value *IncValue : PN->incoming_values())
          FlowsToReturn.insert(IncValue);
        continue;
      }

      // Check whether the pointer came from an allocation.
      case Instruction::Alloca:
        break;
      case Instruction::Call:
      case Instruction::Invoke: {
        CallBase &CB = cast<CallBase>(*RVI);
        if (CB.hasRetAttr(Attribute::NoAlias))
          break;
        if (CB.getCalledFunction() && SCCNodes.count(CB.getCalledFunction()))
          break;
        [[fallthrough]];
      }
      default:
        return false; // Did not come from an allocation.
      }

    if (PointerMayBeCaptured(RetVal, false, /*StoreCaptures=*/false))
      return false;
  }

  return true;
}

/// Deduce noalias attributes for the SCC.
static void addNoAliasAttrs(const SCCNodeSet &SCCNodes,
                            SmallSet<Function *, 8> &Changed) {
  // Check each function in turn, determining which functions return noalias
  // pointers.
  for (Function *F : SCCNodes) {
    // Already noalias.
    if (F->returnDoesNotAlias())
      continue;

    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      return;

    // We annotate noalias return values, which are only applicable to
    // pointer types.
    if (!F->getReturnType()->isPointerTy())
      continue;

    if (!isFunctionMallocLike(F, SCCNodes))
      return;
  }

  for (Function *F : SCCNodes) {
    if (F->returnDoesNotAlias() ||
        !F->getReturnType()->isPointerTy())
      continue;

    F->setReturnDoesNotAlias();
    ++NumNoAlias;
    Changed.insert(F);
  }
}

/// Tests whether this function is known to not return null.
///
/// Requires that the function returns a pointer.
///
/// Returns true if it believes the function will not return a null, and sets
/// \p Speculative based on whether the returned conclusion is a speculative
/// conclusion due to SCC calls.
static bool isReturnNonNull(Function *F, const SCCNodeSet &SCCNodes,
                            bool &Speculative) {
  assert(F->getReturnType()->isPointerTy() &&
         "nonnull only meaningful on pointer types");
  Speculative = false;

  SmallSetVector<Value *, 8> FlowsToReturn;
  for (BasicBlock &BB : *F)
    if (auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator()))
      FlowsToReturn.insert(Ret->getReturnValue());

  auto &DL = F->getDataLayout();

  for (unsigned i = 0; i != FlowsToReturn.size(); ++i) {
    Value *RetVal = FlowsToReturn[i];

    // If this value is locally known to be non-null, we're good
    if (isKnownNonZero(RetVal, DL))
      continue;

    // Otherwise, we need to look upwards since we can't make any local
    // conclusions.
    Instruction *RVI = dyn_cast<Instruction>(RetVal);
    if (!RVI)
      return false;
    switch (RVI->getOpcode()) {
    // Extend the analysis by looking upwards.
    case Instruction::BitCast:
    case Instruction::AddrSpaceCast:
      FlowsToReturn.insert(RVI->getOperand(0));
      continue;
    case Instruction::GetElementPtr:
      if (cast<GEPOperator>(RVI)->isInBounds()) {
        FlowsToReturn.insert(RVI->getOperand(0));
        continue;
      }
      return false;
    case Instruction::Select: {
      SelectInst *SI = cast<SelectInst>(RVI);
      FlowsToReturn.insert(SI->getTrueValue());
      FlowsToReturn.insert(SI->getFalseValue());
      continue;
    }
    case Instruction::PHI: {
      PHINode *PN = cast<PHINode>(RVI);
      for (int i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
        FlowsToReturn.insert(PN->getIncomingValue(i));
      continue;
    }
    case Instruction::Call:
    case Instruction::Invoke: {
      CallBase &CB = cast<CallBase>(*RVI);
      Function *Callee = CB.getCalledFunction();
      // A call to a node within the SCC is assumed to return null until
      // proven otherwise
      if (Callee && SCCNodes.count(Callee)) {
        Speculative = true;
        continue;
      }
      return false;
    }
    default:
      return false; // Unknown source, may be null
    };
    llvm_unreachable("should have either continued or returned");
  }

  return true;
}

/// Deduce nonnull attributes for the SCC.
static void addNonNullAttrs(const SCCNodeSet &SCCNodes,
                            SmallSet<Function *, 8> &Changed) {
  // Speculative that all functions in the SCC return only nonnull
  // pointers.  We may refute this as we analyze functions.
  bool SCCReturnsNonNull = true;

  // Check each function in turn, determining which functions return nonnull
  // pointers.
  for (Function *F : SCCNodes) {
    // Already nonnull.
    if (F->getAttributes().hasRetAttr(Attribute::NonNull))
      continue;

    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      return;

    // We annotate nonnull return values, which are only applicable to
    // pointer types.
    if (!F->getReturnType()->isPointerTy())
      continue;

    bool Speculative = false;
    if (isReturnNonNull(F, SCCNodes, Speculative)) {
      if (!Speculative) {
        // Mark the function eagerly since we may discover a function
        // which prevents us from speculating about the entire SCC
        LLVM_DEBUG(dbgs() << "Eagerly marking " << F->getName()
                          << " as nonnull\n");
        F->addRetAttr(Attribute::NonNull);
        ++NumNonNullReturn;
        Changed.insert(F);
      }
      continue;
    }
    // At least one function returns something which could be null, can't
    // speculate any more.
    SCCReturnsNonNull = false;
  }

  if (SCCReturnsNonNull) {
    for (Function *F : SCCNodes) {
      if (F->getAttributes().hasRetAttr(Attribute::NonNull) ||
          !F->getReturnType()->isPointerTy())
        continue;

      LLVM_DEBUG(dbgs() << "SCC marking " << F->getName() << " as nonnull\n");
      F->addRetAttr(Attribute::NonNull);
      ++NumNonNullReturn;
      Changed.insert(F);
    }
  }
}

/// Deduce noundef attributes for the SCC.
static void addNoUndefAttrs(const SCCNodeSet &SCCNodes,
                            SmallSet<Function *, 8> &Changed) {
  // Check each function in turn, determining which functions return noundef
  // values.
  for (Function *F : SCCNodes) {
    // Already noundef.
    AttributeList Attrs = F->getAttributes();
    if (Attrs.hasRetAttr(Attribute::NoUndef))
      continue;

    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      return;

    // MemorySanitizer assumes that the definition and declaration of a
    // function will be consistent. A function with sanitize_memory attribute
    // should be skipped from inference.
    if (F->hasFnAttribute(Attribute::SanitizeMemory))
      continue;

    if (F->getReturnType()->isVoidTy())
      continue;

    const DataLayout &DL = F->getDataLayout();
    if (all_of(*F, [&](BasicBlock &BB) {
          if (auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator())) {
            // TODO: perform context-sensitive analysis?
            Value *RetVal = Ret->getReturnValue();
            if (!isGuaranteedNotToBeUndefOrPoison(RetVal))
              return false;

            // We know the original return value is not poison now, but it
            // could still be converted to poison by another return attribute.
            // Try to explicitly re-prove the relevant attributes.
            if (Attrs.hasRetAttr(Attribute::NonNull) &&
                !isKnownNonZero(RetVal, DL))
              return false;

            if (MaybeAlign Align = Attrs.getRetAlignment())
              if (RetVal->getPointerAlignment(DL) < *Align)
                return false;

            Attribute Attr = Attrs.getRetAttr(Attribute::Range);
            if (Attr.isValid() &&
                !Attr.getRange().contains(
                    computeConstantRange(RetVal, /*ForSigned=*/false)))
              return false;
          }
          return true;
        })) {
      F->addRetAttr(Attribute::NoUndef);
      ++NumNoUndefReturn;
      Changed.insert(F);
    }
  }
}

namespace {

/// Collects a set of attribute inference requests and performs them all in one
/// go on a single SCC Node. Inference involves scanning function bodies
/// looking for instructions that violate attribute assumptions.
/// As soon as all the bodies are fine we are free to set the attribute.
/// Customization of inference for individual attributes is performed by
/// providing a handful of predicates for each attribute.
class AttributeInferer {
public:
  /// Describes a request for inference of a single attribute.
  struct InferenceDescriptor {

    /// Returns true if this function does not have to be handled.
    /// General intent for this predicate is to provide an optimization
    /// for functions that do not need this attribute inference at all
    /// (say, for functions that already have the attribute).
    std::function<bool(const Function &)> SkipFunction;

    /// Returns true if this instruction violates attribute assumptions.
    std::function<bool(Instruction &)> InstrBreaksAttribute;

    /// Sets the inferred attribute for this function.
    std::function<void(Function &)> SetAttribute;

    /// Attribute we derive.
    Attribute::AttrKind AKind;

    /// If true, only "exact" definitions can be used to infer this attribute.
    /// See GlobalValue::isDefinitionExact.
    bool RequiresExactDefinition;

    InferenceDescriptor(Attribute::AttrKind AK,
                        std::function<bool(const Function &)> SkipFunc,
                        std::function<bool(Instruction &)> InstrScan,
                        std::function<void(Function &)> SetAttr,
                        bool ReqExactDef)
        : SkipFunction(SkipFunc), InstrBreaksAttribute(InstrScan),
          SetAttribute(SetAttr), AKind(AK),
          RequiresExactDefinition(ReqExactDef) {}
  };

private:
  SmallVector<InferenceDescriptor, 4> InferenceDescriptors;

public:
  void registerAttrInference(InferenceDescriptor AttrInference) {
    InferenceDescriptors.push_back(AttrInference);
  }

  void run(const SCCNodeSet &SCCNodes, SmallSet<Function *, 8> &Changed);
};

/// Perform all the requested attribute inference actions according to the
/// attribute predicates stored before.
void AttributeInferer::run(const SCCNodeSet &SCCNodes,
                           SmallSet<Function *, 8> &Changed) {
  SmallVector<InferenceDescriptor, 4> InferInSCC = InferenceDescriptors;
  // Go through all the functions in SCC and check corresponding attribute
  // assumptions for each of them. Attributes that are invalid for this SCC
  // will be removed from InferInSCC.
  for (Function *F : SCCNodes) {

    // No attributes whose assumptions are still valid - done.
    if (InferInSCC.empty())
      return;

    // Check if our attributes ever need scanning/can be scanned.
    llvm::erase_if(InferInSCC, [F](const InferenceDescriptor &ID) {
      if (ID.SkipFunction(*F))
        return false;

      // Remove from further inference (invalidate) when visiting a function
      // that has no instructions to scan/has an unsuitable definition.
      return F->isDeclaration() ||
             (ID.RequiresExactDefinition && !F->hasExactDefinition());
    });

    // For each attribute still in InferInSCC that doesn't explicitly skip F,
    // set up the F instructions scan to verify assumptions of the attribute.
    SmallVector<InferenceDescriptor, 4> InferInThisFunc;
    llvm::copy_if(
        InferInSCC, std::back_inserter(InferInThisFunc),
        [F](const InferenceDescriptor &ID) { return !ID.SkipFunction(*F); });

    if (InferInThisFunc.empty())
      continue;

    // Start instruction scan.
    for (Instruction &I : instructions(*F)) {
      llvm::erase_if(InferInThisFunc, [&](const InferenceDescriptor &ID) {
        if (!ID.InstrBreaksAttribute(I))
          return false;
        // Remove attribute from further inference on any other functions
        // because attribute assumptions have just been violated.
        llvm::erase_if(InferInSCC, [&ID](const InferenceDescriptor &D) {
          return D.AKind == ID.AKind;
        });
        // Remove attribute from the rest of current instruction scan.
        return true;
      });

      if (InferInThisFunc.empty())
        break;
    }
  }

  if (InferInSCC.empty())
    return;

  for (Function *F : SCCNodes)
    // At this point InferInSCC contains only functions that were either:
    //   - explicitly skipped from scan/inference, or
    //   - verified to have no instructions that break attribute assumptions.
    // Hence we just go and force the attribute for all non-skipped functions.
    for (auto &ID : InferInSCC) {
      if (ID.SkipFunction(*F))
        continue;
      Changed.insert(F);
      ID.SetAttribute(*F);
    }
}

struct SCCNodesResult {
  SCCNodeSet SCCNodes;
  bool HasUnknownCall;
};

} // end anonymous namespace

/// Helper for non-Convergent inference predicate InstrBreaksAttribute.
static bool InstrBreaksNonConvergent(Instruction &I,
                                     const SCCNodeSet &SCCNodes) {
  const CallBase *CB = dyn_cast<CallBase>(&I);
  // Breaks non-convergent assumption if CS is a convergent call to a function
  // not in the SCC.
  return CB && CB->isConvergent() &&
         !SCCNodes.contains(CB->getCalledFunction());
}

/// Helper for NoUnwind inference predicate InstrBreaksAttribute.
static bool InstrBreaksNonThrowing(Instruction &I, const SCCNodeSet &SCCNodes) {
  if (!I.mayThrow(/* IncludePhaseOneUnwind */ true))
    return false;
  if (const auto *CI = dyn_cast<CallInst>(&I)) {
    if (Function *Callee = CI->getCalledFunction()) {
      // I is a may-throw call to a function inside our SCC. This doesn't
      // invalidate our current working assumption that the SCC is no-throw; we
      // just have to scan that other function.
      if (SCCNodes.contains(Callee))
        return false;
    }
  }
  return true;
}

/// Helper for NoFree inference predicate InstrBreaksAttribute.
static bool InstrBreaksNoFree(Instruction &I, const SCCNodeSet &SCCNodes) {
  CallBase *CB = dyn_cast<CallBase>(&I);
  if (!CB)
    return false;

  if (CB->hasFnAttr(Attribute::NoFree))
    return false;

  // Speculatively assume in SCC.
  if (Function *Callee = CB->getCalledFunction())
    if (SCCNodes.contains(Callee))
      return false;

  return true;
}

// Return true if this is an atomic which has an ordering stronger than
// unordered.  Note that this is different than the predicate we use in
// Attributor.  Here we chose to be conservative and consider monotonic
// operations potentially synchronizing.  We generally don't do much with
// monotonic operations, so this is simply risk reduction.
static bool isOrderedAtomic(Instruction *I) {
  if (!I->isAtomic())
    return false;

  if (auto *FI = dyn_cast<FenceInst>(I))
    // All legal orderings for fence are stronger than monotonic.
    return FI->getSyncScopeID() != SyncScope::SingleThread;
  else if (isa<AtomicCmpXchgInst>(I) || isa<AtomicRMWInst>(I))
    return true;
  else if (auto *SI = dyn_cast<StoreInst>(I))
    return !SI->isUnordered();
  else if (auto *LI = dyn_cast<LoadInst>(I))
    return !LI->isUnordered();
  else {
    llvm_unreachable("unknown atomic instruction?");
  }
}

static bool InstrBreaksNoSync(Instruction &I, const SCCNodeSet &SCCNodes) {
  // Volatile may synchronize
  if (I.isVolatile())
    return true;

  // An ordered atomic may synchronize.  (See comment about on monotonic.)
  if (isOrderedAtomic(&I))
    return true;

  auto *CB = dyn_cast<CallBase>(&I);
  if (!CB)
    // Non call site cases covered by the two checks above
    return false;

  if (CB->hasFnAttr(Attribute::NoSync))
    return false;

  // Non volatile memset/memcpy/memmoves are nosync
  // NOTE: Only intrinsics with volatile flags should be handled here.  All
  // others should be marked in Intrinsics.td.
  if (auto *MI = dyn_cast<MemIntrinsic>(&I))
    if (!MI->isVolatile())
      return false;

  // Speculatively assume in SCC.
  if (Function *Callee = CB->getCalledFunction())
    if (SCCNodes.contains(Callee))
      return false;

  return true;
}

/// Attempt to remove convergent function attribute when possible.
///
/// Returns true if any changes to function attributes were made.
static void inferConvergent(const SCCNodeSet &SCCNodes,
                            SmallSet<Function *, 8> &Changed) {
  AttributeInferer AI;

  // Request to remove the convergent attribute from all functions in the SCC
  // if every callsite within the SCC is not convergent (except for calls
  // to functions within the SCC).
  // Note: Removal of the attr from the callsites will happen in
  // InstCombineCalls separately.
  AI.registerAttrInference(AttributeInferer::InferenceDescriptor{
      Attribute::Convergent,
      // Skip non-convergent functions.
      [](const Function &F) { return !F.isConvergent(); },
      // Instructions that break non-convergent assumption.
      [SCCNodes](Instruction &I) {
        return InstrBreaksNonConvergent(I, SCCNodes);
      },
      [](Function &F) {
        LLVM_DEBUG(dbgs() << "Removing convergent attr from fn " << F.getName()
                          << "\n");
        F.setNotConvergent();
      },
      /* RequiresExactDefinition= */ false});
  // Perform all the requested attribute inference actions.
  AI.run(SCCNodes, Changed);
}

/// Infer attributes from all functions in the SCC by scanning every
/// instruction for compliance to the attribute assumptions.
///
/// Returns true if any changes to function attributes were made.
static void inferAttrsFromFunctionBodies(const SCCNodeSet &SCCNodes,
                                         SmallSet<Function *, 8> &Changed) {
  AttributeInferer AI;

  if (!DisableNoUnwindInference)
    // Request to infer nounwind attribute for all the functions in the SCC if
    // every callsite within the SCC is not throwing (except for calls to
    // functions within the SCC). Note that nounwind attribute suffers from
    // derefinement - results may change depending on how functions are
    // optimized. Thus it can be inferred only from exact definitions.
    AI.registerAttrInference(AttributeInferer::InferenceDescriptor{
        Attribute::NoUnwind,
        // Skip non-throwing functions.
        [](const Function &F) { return F.doesNotThrow(); },
        // Instructions that break non-throwing assumption.
        [&SCCNodes](Instruction &I) {
          return InstrBreaksNonThrowing(I, SCCNodes);
        },
        [](Function &F) {
          LLVM_DEBUG(dbgs()
                     << "Adding nounwind attr to fn " << F.getName() << "\n");
          F.setDoesNotThrow();
          ++NumNoUnwind;
        },
        /* RequiresExactDefinition= */ true});

  if (!DisableNoFreeInference)
    // Request to infer nofree attribute for all the functions in the SCC if
    // every callsite within the SCC does not directly or indirectly free
    // memory (except for calls to functions within the SCC). Note that nofree
    // attribute suffers from derefinement - results may change depending on
    // how functions are optimized. Thus it can be inferred only from exact
    // definitions.
    AI.registerAttrInference(AttributeInferer::InferenceDescriptor{
        Attribute::NoFree,
        // Skip functions known not to free memory.
        [](const Function &F) { return F.doesNotFreeMemory(); },
        // Instructions that break non-deallocating assumption.
        [&SCCNodes](Instruction &I) {
          return InstrBreaksNoFree(I, SCCNodes);
        },
        [](Function &F) {
          LLVM_DEBUG(dbgs()
                     << "Adding nofree attr to fn " << F.getName() << "\n");
          F.setDoesNotFreeMemory();
          ++NumNoFree;
        },
        /* RequiresExactDefinition= */ true});

  AI.registerAttrInference(AttributeInferer::InferenceDescriptor{
      Attribute::NoSync,
      // Skip already marked functions.
      [](const Function &F) { return F.hasNoSync(); },
      // Instructions that break nosync assumption.
      [&SCCNodes](Instruction &I) {
        return InstrBreaksNoSync(I, SCCNodes);
      },
      [](Function &F) {
        LLVM_DEBUG(dbgs()
                   << "Adding nosync attr to fn " << F.getName() << "\n");
        F.setNoSync();
        ++NumNoSync;
      },
      /* RequiresExactDefinition= */ true});

  // Perform all the requested attribute inference actions.
  AI.run(SCCNodes, Changed);
}

static void addNoRecurseAttrs(const SCCNodeSet &SCCNodes,
                              SmallSet<Function *, 8> &Changed) {
  // Try and identify functions that do not recurse.

  // If the SCC contains multiple nodes we know for sure there is recursion.
  if (SCCNodes.size() != 1)
    return;

  Function *F = *SCCNodes.begin();
  if (!F || !F->hasExactDefinition() || F->doesNotRecurse())
    return;

  // If all of the calls in F are identifiable and are to norecurse functions, F
  // is norecurse. This check also detects self-recursion as F is not currently
  // marked norecurse, so any called from F to F will not be marked norecurse.
  for (auto &BB : *F)
    for (auto &I : BB.instructionsWithoutDebug())
      if (auto *CB = dyn_cast<CallBase>(&I)) {
        Function *Callee = CB->getCalledFunction();
        if (!Callee || Callee == F ||
            (!Callee->doesNotRecurse() &&
             !(Callee->isDeclaration() &&
               Callee->hasFnAttribute(Attribute::NoCallback))))
          // Function calls a potentially recursive function.
          return;
      }

  // Every call was to a non-recursive function other than this function, and
  // we have no indirect recursion as the SCC size is one. This function cannot
  // recurse.
  F->setDoesNotRecurse();
  ++NumNoRecurse;
  Changed.insert(F);
}

static bool instructionDoesNotReturn(Instruction &I) {
  if (auto *CB = dyn_cast<CallBase>(&I))
    return CB->hasFnAttr(Attribute::NoReturn);
  return false;
}

// A basic block can only return if it terminates with a ReturnInst and does not
// contain calls to noreturn functions.
static bool basicBlockCanReturn(BasicBlock &BB) {
  if (!isa<ReturnInst>(BB.getTerminator()))
    return false;
  return none_of(BB, instructionDoesNotReturn);
}

// FIXME: this doesn't handle recursion.
static bool canReturn(Function &F) {
  SmallVector<BasicBlock *, 16> Worklist;
  SmallPtrSet<BasicBlock *, 16> Visited;

  Visited.insert(&F.front());
  Worklist.push_back(&F.front());

  do {
    BasicBlock *BB = Worklist.pop_back_val();
    if (basicBlockCanReturn(*BB))
      return true;
    for (BasicBlock *Succ : successors(BB))
      if (Visited.insert(Succ).second)
        Worklist.push_back(Succ);
  } while (!Worklist.empty());

  return false;
}

// Set the noreturn function attribute if possible.
static void addNoReturnAttrs(const SCCNodeSet &SCCNodes,
                             SmallSet<Function *, 8> &Changed) {
  for (Function *F : SCCNodes) {
    if (!F || !F->hasExactDefinition() || F->hasFnAttribute(Attribute::Naked) ||
        F->doesNotReturn())
      continue;

    if (!canReturn(*F)) {
      F->setDoesNotReturn();
      Changed.insert(F);
    }
  }
}

static bool functionWillReturn(const Function &F) {
  // We can infer and propagate function attributes only when we know that the
  // definition we'll get at link time is *exactly* the definition we see now.
  // For more details, see GlobalValue::mayBeDerefined.
  if (!F.hasExactDefinition())
    return false;

  // Must-progress function without side-effects must return.
  if (F.mustProgress() && F.onlyReadsMemory())
    return true;

  // Can only analyze functions with a definition.
  if (F.isDeclaration())
    return false;

  // Functions with loops require more sophisticated analysis, as the loop
  // may be infinite. For now, don't try to handle them.
  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>> Backedges;
  FindFunctionBackedges(F, Backedges);
  if (!Backedges.empty())
    return false;

  // If there are no loops, then the function is willreturn if all calls in
  // it are willreturn.
  return all_of(instructions(F), [](const Instruction &I) {
    return I.willReturn();
  });
}

// Set the willreturn function attribute if possible.
static void addWillReturn(const SCCNodeSet &SCCNodes,
                          SmallSet<Function *, 8> &Changed) {
  for (Function *F : SCCNodes) {
    if (!F || F->willReturn() || !functionWillReturn(*F))
      continue;

    F->setWillReturn();
    NumWillReturn++;
    Changed.insert(F);
  }
}

static SCCNodesResult createSCCNodeSet(ArrayRef<Function *> Functions) {
  SCCNodesResult Res;
  Res.HasUnknownCall = false;
  for (Function *F : Functions) {
    if (!F || F->hasOptNone() || F->hasFnAttribute(Attribute::Naked) ||
        F->isPresplitCoroutine()) {
      // Treat any function we're trying not to optimize as if it were an
      // indirect call and omit it from the node set used below.
      Res.HasUnknownCall = true;
      continue;
    }
    // Track whether any functions in this SCC have an unknown call edge.
    // Note: if this is ever a performance hit, we can common it with
    // subsequent routines which also do scans over the instructions of the
    // function.
    if (!Res.HasUnknownCall) {
      for (Instruction &I : instructions(*F)) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (!CB->getCalledFunction()) {
            Res.HasUnknownCall = true;
            break;
          }
        }
      }
    }
    Res.SCCNodes.insert(F);
  }
  return Res;
}

template <typename AARGetterT>
static SmallSet<Function *, 8>
deriveAttrsInPostOrder(ArrayRef<Function *> Functions, AARGetterT &&AARGetter,
                       bool ArgAttrsOnly) {
  SCCNodesResult Nodes = createSCCNodeSet(Functions);

  // Bail if the SCC only contains optnone functions.
  if (Nodes.SCCNodes.empty())
    return {};

  SmallSet<Function *, 8> Changed;
  if (ArgAttrsOnly) {
    addArgumentAttrs(Nodes.SCCNodes, Changed);
    return Changed;
  }

  addArgumentReturnedAttrs(Nodes.SCCNodes, Changed);
  addMemoryAttrs(Nodes.SCCNodes, AARGetter, Changed);
  addArgumentAttrs(Nodes.SCCNodes, Changed);
  inferConvergent(Nodes.SCCNodes, Changed);
  addNoReturnAttrs(Nodes.SCCNodes, Changed);
  addWillReturn(Nodes.SCCNodes, Changed);
  addNoUndefAttrs(Nodes.SCCNodes, Changed);

  // If we have no external nodes participating in the SCC, we can deduce some
  // more precise attributes as well.
  if (!Nodes.HasUnknownCall) {
    addNoAliasAttrs(Nodes.SCCNodes, Changed);
    addNonNullAttrs(Nodes.SCCNodes, Changed);
    inferAttrsFromFunctionBodies(Nodes.SCCNodes, Changed);
    addNoRecurseAttrs(Nodes.SCCNodes, Changed);
  }

  // Finally, infer the maximal set of attributes from the ones we've inferred
  // above.  This is handling the cases where one attribute on a signature
  // implies another, but for implementation reasons the inference rule for
  // the later is missing (or simply less sophisticated).
  for (Function *F : Nodes.SCCNodes)
    if (F)
      if (inferAttributesFromOthers(*F))
        Changed.insert(F);

  return Changed;
}

PreservedAnalyses PostOrderFunctionAttrsPass::run(LazyCallGraph::SCC &C,
                                                  CGSCCAnalysisManager &AM,
                                                  LazyCallGraph &CG,
                                                  CGSCCUpdateResult &) {
  // Skip non-recursive functions if requested.
  // Only infer argument attributes for non-recursive functions, because
  // it can affect optimization behavior in conjunction with noalias.
  bool ArgAttrsOnly = false;
  if (C.size() == 1 && SkipNonRecursive) {
    LazyCallGraph::Node &N = *C.begin();
    if (!N->lookup(N))
      ArgAttrsOnly = true;
  }

  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

  // We pass a lambda into functions to wire them up to the analysis manager
  // for getting function analyses.
  auto AARGetter = [&](Function &F) -> AAResults & {
    return FAM.getResult<AAManager>(F);
  };

  SmallVector<Function *, 8> Functions;
  for (LazyCallGraph::Node &N : C) {
    Functions.push_back(&N.getFunction());
  }

  auto ChangedFunctions =
      deriveAttrsInPostOrder(Functions, AARGetter, ArgAttrsOnly);
  if (ChangedFunctions.empty())
    return PreservedAnalyses::all();

  // Invalidate analyses for modified functions so that we don't have to
  // invalidate all analyses for all functions in this SCC.
  PreservedAnalyses FuncPA;
  // We haven't changed the CFG for modified functions.
  FuncPA.preserveSet<CFGAnalyses>();
  for (Function *Changed : ChangedFunctions) {
    FAM.invalidate(*Changed, FuncPA);
    // Also invalidate any direct callers of changed functions since analyses
    // may care about attributes of direct callees. For example, MemorySSA cares
    // about whether or not a call's callee modifies memory and queries that
    // through function attributes.
    for (auto *U : Changed->users()) {
      if (auto *Call = dyn_cast<CallBase>(U)) {
        if (Call->getCalledFunction() == Changed)
          FAM.invalidate(*Call->getFunction(), FuncPA);
      }
    }
  }

  PreservedAnalyses PA;
  // We have not added or removed functions.
  PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
  // We already invalidated all relevant function analyses above.
  PA.preserveSet<AllAnalysesOn<Function>>();
  return PA;
}

void PostOrderFunctionAttrsPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<PostOrderFunctionAttrsPass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  if (SkipNonRecursive)
    OS << "<skip-non-recursive-function-attrs>";
}

template <typename AARGetterT>
static bool runImpl(CallGraphSCC &SCC, AARGetterT AARGetter) {
  SmallVector<Function *, 8> Functions;
  for (CallGraphNode *I : SCC) {
    Functions.push_back(I->getFunction());
  }

  return !deriveAttrsInPostOrder(Functions, AARGetter).empty();
}

static bool addNoRecurseAttrsTopDown(Function &F) {
  // We check the preconditions for the function prior to calling this to avoid
  // the cost of building up a reversible post-order list. We assert them here
  // to make sure none of the invariants this relies on were violated.
  assert(!F.isDeclaration() && "Cannot deduce norecurse without a definition!");
  assert(!F.doesNotRecurse() &&
         "This function has already been deduced as norecurs!");
  assert(F.hasInternalLinkage() &&
         "Can only do top-down deduction for internal linkage functions!");

  // If F is internal and all of its uses are calls from a non-recursive
  // functions, then none of its calls could in fact recurse without going
  // through a function marked norecurse, and so we can mark this function too
  // as norecurse. Note that the uses must actually be calls -- otherwise
  // a pointer to this function could be returned from a norecurse function but
  // this function could be recursively (indirectly) called. Note that this
  // also detects if F is directly recursive as F is not yet marked as
  // a norecurse function.
  for (auto &U : F.uses()) {
    auto *I = dyn_cast<Instruction>(U.getUser());
    if (!I)
      return false;
    CallBase *CB = dyn_cast<CallBase>(I);
    if (!CB || !CB->isCallee(&U) ||
        !CB->getParent()->getParent()->doesNotRecurse())
      return false;
  }
  F.setDoesNotRecurse();
  ++NumNoRecurse;
  return true;
}

static bool deduceFunctionAttributeInRPO(Module &M, LazyCallGraph &CG) {
  // We only have a post-order SCC traversal (because SCCs are inherently
  // discovered in post-order), so we accumulate them in a vector and then walk
  // it in reverse. This is simpler than using the RPO iterator infrastructure
  // because we need to combine SCC detection and the PO walk of the call
  // graph. We can also cheat egregiously because we're primarily interested in
  // synthesizing norecurse and so we can only save the singular SCCs as SCCs
  // with multiple functions in them will clearly be recursive.

  SmallVector<Function *, 16> Worklist;
  CG.buildRefSCCs();
  for (LazyCallGraph::RefSCC &RC : CG.postorder_ref_sccs()) {
    for (LazyCallGraph::SCC &SCC : RC) {
      if (SCC.size() != 1)
        continue;
      Function &F = SCC.begin()->getFunction();
      if (!F.isDeclaration() && !F.doesNotRecurse() && F.hasInternalLinkage())
        Worklist.push_back(&F);
    }
  }
  bool Changed = false;
  for (auto *F : llvm::reverse(Worklist))
    Changed |= addNoRecurseAttrsTopDown(*F);

  return Changed;
}

PreservedAnalyses
ReversePostOrderFunctionAttrsPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto &CG = AM.getResult<LazyCallGraphAnalysis>(M);

  if (!deduceFunctionAttributeInRPO(M, CG))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<LazyCallGraphAnalysis>();
  return PA;
}
