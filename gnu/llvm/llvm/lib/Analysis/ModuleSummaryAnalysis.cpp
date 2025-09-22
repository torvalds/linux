//===- ModuleSummaryAnalysis.cpp - Module summary index builder -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass builds a ModuleSummaryIndex object for the module, to be written
// to bitcode or LLVM assembly.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/IndirectCallPromotionAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryProfileInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/InitializePasses.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;
using namespace llvm::memprof;

#define DEBUG_TYPE "module-summary-analysis"

// Option to force edges cold which will block importing when the
// -import-cold-multiplier is set to 0. Useful for debugging.
namespace llvm {
FunctionSummary::ForceSummaryHotnessType ForceSummaryEdgesCold =
    FunctionSummary::FSHT_None;
} // namespace llvm

static cl::opt<FunctionSummary::ForceSummaryHotnessType, true> FSEC(
    "force-summary-edges-cold", cl::Hidden, cl::location(ForceSummaryEdgesCold),
    cl::desc("Force all edges in the function summary to cold"),
    cl::values(clEnumValN(FunctionSummary::FSHT_None, "none", "None."),
               clEnumValN(FunctionSummary::FSHT_AllNonCritical,
                          "all-non-critical", "All non-critical edges."),
               clEnumValN(FunctionSummary::FSHT_All, "all", "All edges.")));

static cl::opt<std::string> ModuleSummaryDotFile(
    "module-summary-dot-file", cl::Hidden, cl::value_desc("filename"),
    cl::desc("File to emit dot graph of new summary into"));

extern cl::opt<bool> ScalePartialSampleProfileWorkingSetSize;

extern cl::opt<unsigned> MaxNumVTableAnnotations;

extern cl::opt<bool> MemProfReportHintedSizes;

// Walk through the operands of a given User via worklist iteration and populate
// the set of GlobalValue references encountered. Invoked either on an
// Instruction or a GlobalVariable (which walks its initializer).
// Return true if any of the operands contains blockaddress. This is important
// to know when computing summary for global var, because if global variable
// references basic block address we can't import it separately from function
// containing that basic block. For simplicity we currently don't import such
// global vars at all. When importing function we aren't interested if any
// instruction in it takes an address of any basic block, because instruction
// can only take an address of basic block located in the same function.
// Set `RefLocalLinkageIFunc` to true if the analyzed value references a
// local-linkage ifunc.
static bool findRefEdges(ModuleSummaryIndex &Index, const User *CurUser,
                         SetVector<ValueInfo, std::vector<ValueInfo>> &RefEdges,
                         SmallPtrSet<const User *, 8> &Visited,
                         bool &RefLocalLinkageIFunc) {
  bool HasBlockAddress = false;
  SmallVector<const User *, 32> Worklist;
  if (Visited.insert(CurUser).second)
    Worklist.push_back(CurUser);

  while (!Worklist.empty()) {
    const User *U = Worklist.pop_back_val();
    const auto *CB = dyn_cast<CallBase>(U);

    for (const auto &OI : U->operands()) {
      const User *Operand = dyn_cast<User>(OI);
      if (!Operand)
        continue;
      if (isa<BlockAddress>(Operand)) {
        HasBlockAddress = true;
        continue;
      }
      if (auto *GV = dyn_cast<GlobalValue>(Operand)) {
        // We have a reference to a global value. This should be added to
        // the reference set unless it is a callee. Callees are handled
        // specially by WriteFunction and are added to a separate list.
        if (!(CB && CB->isCallee(&OI))) {
          // If an ifunc has local linkage, do not add it into ref edges, and
          // sets `RefLocalLinkageIFunc` to true. The referencer is not eligible
          // for import. An ifunc doesn't have summary and ThinLTO cannot
          // promote it; importing the referencer may cause linkage errors.
          if (auto *GI = dyn_cast_if_present<GlobalIFunc>(GV);
              GI && GI->hasLocalLinkage()) {
            RefLocalLinkageIFunc = true;
            continue;
          }
          RefEdges.insert(Index.getOrInsertValueInfo(GV));
        }
        continue;
      }
      if (Visited.insert(Operand).second)
        Worklist.push_back(Operand);
    }
  }

  const Instruction *I = dyn_cast<Instruction>(CurUser);
  if (I) {
    uint64_t TotalCount = 0;
    // MaxNumVTableAnnotations is the maximum number of vtables annotated on
    // the instruction.
    auto ValueDataArray = getValueProfDataFromInst(
        *I, IPVK_VTableTarget, MaxNumVTableAnnotations, TotalCount);

    for (const auto &V : ValueDataArray)
      RefEdges.insert(Index.getOrInsertValueInfo(/* VTableGUID = */
                                                 V.Value));
  }
  return HasBlockAddress;
}

static CalleeInfo::HotnessType getHotness(uint64_t ProfileCount,
                                          ProfileSummaryInfo *PSI) {
  if (!PSI)
    return CalleeInfo::HotnessType::Unknown;
  if (PSI->isHotCount(ProfileCount))
    return CalleeInfo::HotnessType::Hot;
  if (PSI->isColdCount(ProfileCount))
    return CalleeInfo::HotnessType::Cold;
  return CalleeInfo::HotnessType::None;
}

static bool isNonRenamableLocal(const GlobalValue &GV) {
  return GV.hasSection() && GV.hasLocalLinkage();
}

/// Determine whether this call has all constant integer arguments (excluding
/// "this") and summarize it to VCalls or ConstVCalls as appropriate.
static void addVCallToSet(
    DevirtCallSite Call, GlobalValue::GUID Guid,
    SetVector<FunctionSummary::VFuncId, std::vector<FunctionSummary::VFuncId>>
        &VCalls,
    SetVector<FunctionSummary::ConstVCall,
              std::vector<FunctionSummary::ConstVCall>> &ConstVCalls) {
  std::vector<uint64_t> Args;
  // Start from the second argument to skip the "this" pointer.
  for (auto &Arg : drop_begin(Call.CB.args())) {
    auto *CI = dyn_cast<ConstantInt>(Arg);
    if (!CI || CI->getBitWidth() > 64) {
      VCalls.insert({Guid, Call.Offset});
      return;
    }
    Args.push_back(CI->getZExtValue());
  }
  ConstVCalls.insert({{Guid, Call.Offset}, std::move(Args)});
}

/// If this intrinsic call requires that we add information to the function
/// summary, do so via the non-constant reference arguments.
static void addIntrinsicToSummary(
    const CallInst *CI,
    SetVector<GlobalValue::GUID, std::vector<GlobalValue::GUID>> &TypeTests,
    SetVector<FunctionSummary::VFuncId, std::vector<FunctionSummary::VFuncId>>
        &TypeTestAssumeVCalls,
    SetVector<FunctionSummary::VFuncId, std::vector<FunctionSummary::VFuncId>>
        &TypeCheckedLoadVCalls,
    SetVector<FunctionSummary::ConstVCall,
              std::vector<FunctionSummary::ConstVCall>>
        &TypeTestAssumeConstVCalls,
    SetVector<FunctionSummary::ConstVCall,
              std::vector<FunctionSummary::ConstVCall>>
        &TypeCheckedLoadConstVCalls,
    DominatorTree &DT) {
  switch (CI->getCalledFunction()->getIntrinsicID()) {
  case Intrinsic::type_test:
  case Intrinsic::public_type_test: {
    auto *TypeMDVal = cast<MetadataAsValue>(CI->getArgOperand(1));
    auto *TypeId = dyn_cast<MDString>(TypeMDVal->getMetadata());
    if (!TypeId)
      break;
    GlobalValue::GUID Guid = GlobalValue::getGUID(TypeId->getString());

    // Produce a summary from type.test intrinsics. We only summarize type.test
    // intrinsics that are used other than by an llvm.assume intrinsic.
    // Intrinsics that are assumed are relevant only to the devirtualization
    // pass, not the type test lowering pass.
    bool HasNonAssumeUses = llvm::any_of(CI->uses(), [](const Use &CIU) {
      return !isa<AssumeInst>(CIU.getUser());
    });
    if (HasNonAssumeUses)
      TypeTests.insert(Guid);

    SmallVector<DevirtCallSite, 4> DevirtCalls;
    SmallVector<CallInst *, 4> Assumes;
    findDevirtualizableCallsForTypeTest(DevirtCalls, Assumes, CI, DT);
    for (auto &Call : DevirtCalls)
      addVCallToSet(Call, Guid, TypeTestAssumeVCalls,
                    TypeTestAssumeConstVCalls);

    break;
  }

  case Intrinsic::type_checked_load_relative:
  case Intrinsic::type_checked_load: {
    auto *TypeMDVal = cast<MetadataAsValue>(CI->getArgOperand(2));
    auto *TypeId = dyn_cast<MDString>(TypeMDVal->getMetadata());
    if (!TypeId)
      break;
    GlobalValue::GUID Guid = GlobalValue::getGUID(TypeId->getString());

    SmallVector<DevirtCallSite, 4> DevirtCalls;
    SmallVector<Instruction *, 4> LoadedPtrs;
    SmallVector<Instruction *, 4> Preds;
    bool HasNonCallUses = false;
    findDevirtualizableCallsForTypeCheckedLoad(DevirtCalls, LoadedPtrs, Preds,
                                               HasNonCallUses, CI, DT);
    // Any non-call uses of the result of llvm.type.checked.load will
    // prevent us from optimizing away the llvm.type.test.
    if (HasNonCallUses)
      TypeTests.insert(Guid);
    for (auto &Call : DevirtCalls)
      addVCallToSet(Call, Guid, TypeCheckedLoadVCalls,
                    TypeCheckedLoadConstVCalls);

    break;
  }
  default:
    break;
  }
}

static bool isNonVolatileLoad(const Instruction *I) {
  if (const auto *LI = dyn_cast<LoadInst>(I))
    return !LI->isVolatile();

  return false;
}

static bool isNonVolatileStore(const Instruction *I) {
  if (const auto *SI = dyn_cast<StoreInst>(I))
    return !SI->isVolatile();

  return false;
}

// Returns true if the function definition must be unreachable.
//
// Note if this helper function returns true, `F` is guaranteed
// to be unreachable; if it returns false, `F` might still
// be unreachable but not covered by this helper function.
static bool mustBeUnreachableFunction(const Function &F) {
  // A function must be unreachable if its entry block ends with an
  // 'unreachable'.
  assert(!F.isDeclaration());
  return isa<UnreachableInst>(F.getEntryBlock().getTerminator());
}

static void computeFunctionSummary(
    ModuleSummaryIndex &Index, const Module &M, const Function &F,
    BlockFrequencyInfo *BFI, ProfileSummaryInfo *PSI, DominatorTree &DT,
    bool HasLocalsInUsedOrAsm, DenseSet<GlobalValue::GUID> &CantBePromoted,
    bool IsThinLTO,
    std::function<const StackSafetyInfo *(const Function &F)> GetSSICallback) {
  // Summary not currently supported for anonymous functions, they should
  // have been named.
  assert(F.hasName());

  unsigned NumInsts = 0;
  // Map from callee ValueId to profile count. Used to accumulate profile
  // counts for all static calls to a given callee.
  MapVector<ValueInfo, CalleeInfo, DenseMap<ValueInfo, unsigned>,
            std::vector<std::pair<ValueInfo, CalleeInfo>>>
      CallGraphEdges;
  SetVector<ValueInfo, std::vector<ValueInfo>> RefEdges, LoadRefEdges,
      StoreRefEdges;
  SetVector<GlobalValue::GUID, std::vector<GlobalValue::GUID>> TypeTests;
  SetVector<FunctionSummary::VFuncId, std::vector<FunctionSummary::VFuncId>>
      TypeTestAssumeVCalls, TypeCheckedLoadVCalls;
  SetVector<FunctionSummary::ConstVCall,
            std::vector<FunctionSummary::ConstVCall>>
      TypeTestAssumeConstVCalls, TypeCheckedLoadConstVCalls;
  ICallPromotionAnalysis ICallAnalysis;
  SmallPtrSet<const User *, 8> Visited;

  // Add personality function, prefix data and prologue data to function's ref
  // list.
  bool HasLocalIFuncCallOrRef = false;
  findRefEdges(Index, &F, RefEdges, Visited, HasLocalIFuncCallOrRef);
  std::vector<const Instruction *> NonVolatileLoads;
  std::vector<const Instruction *> NonVolatileStores;

  std::vector<CallsiteInfo> Callsites;
  std::vector<AllocInfo> Allocs;

#ifndef NDEBUG
  DenseSet<const CallBase *> CallsThatMayHaveMemprofSummary;
#endif

  bool HasInlineAsmMaybeReferencingInternal = false;
  bool HasIndirBranchToBlockAddress = false;
  bool HasUnknownCall = false;
  bool MayThrow = false;
  for (const BasicBlock &BB : F) {
    // We don't allow inlining of function with indirect branch to blockaddress.
    // If the blockaddress escapes the function, e.g., via a global variable,
    // inlining may lead to an invalid cross-function reference. So we shouldn't
    // import such function either.
    if (BB.hasAddressTaken()) {
      for (User *U : BlockAddress::get(const_cast<BasicBlock *>(&BB))->users())
        if (!isa<CallBrInst>(*U)) {
          HasIndirBranchToBlockAddress = true;
          break;
        }
    }

    for (const Instruction &I : BB) {
      if (I.isDebugOrPseudoInst())
        continue;
      ++NumInsts;

      // Regular LTO module doesn't participate in ThinLTO import,
      // so no reference from it can be read/writeonly, since this
      // would require importing variable as local copy
      if (IsThinLTO) {
        if (isNonVolatileLoad(&I)) {
          // Postpone processing of non-volatile load instructions
          // See comments below
          Visited.insert(&I);
          NonVolatileLoads.push_back(&I);
          continue;
        } else if (isNonVolatileStore(&I)) {
          Visited.insert(&I);
          NonVolatileStores.push_back(&I);
          // All references from second operand of store (destination address)
          // can be considered write-only if they're not referenced by any
          // non-store instruction. References from first operand of store
          // (stored value) can't be treated either as read- or as write-only
          // so we add them to RefEdges as we do with all other instructions
          // except non-volatile load.
          Value *Stored = I.getOperand(0);
          if (auto *GV = dyn_cast<GlobalValue>(Stored))
            // findRefEdges will try to examine GV operands, so instead
            // of calling it we should add GV to RefEdges directly.
            RefEdges.insert(Index.getOrInsertValueInfo(GV));
          else if (auto *U = dyn_cast<User>(Stored))
            findRefEdges(Index, U, RefEdges, Visited, HasLocalIFuncCallOrRef);
          continue;
        }
      }
      findRefEdges(Index, &I, RefEdges, Visited, HasLocalIFuncCallOrRef);
      const auto *CB = dyn_cast<CallBase>(&I);
      if (!CB) {
        if (I.mayThrow())
          MayThrow = true;
        continue;
      }

      const auto *CI = dyn_cast<CallInst>(&I);
      // Since we don't know exactly which local values are referenced in inline
      // assembly, conservatively mark the function as possibly referencing
      // a local value from inline assembly to ensure we don't export a
      // reference (which would require renaming and promotion of the
      // referenced value).
      if (HasLocalsInUsedOrAsm && CI && CI->isInlineAsm())
        HasInlineAsmMaybeReferencingInternal = true;

      auto *CalledValue = CB->getCalledOperand();
      auto *CalledFunction = CB->getCalledFunction();
      if (CalledValue && !CalledFunction) {
        CalledValue = CalledValue->stripPointerCasts();
        // Stripping pointer casts can reveal a called function.
        CalledFunction = dyn_cast<Function>(CalledValue);
      }
      // Check if this is an alias to a function. If so, get the
      // called aliasee for the checks below.
      if (auto *GA = dyn_cast<GlobalAlias>(CalledValue)) {
        assert(!CalledFunction && "Expected null called function in callsite for alias");
        CalledFunction = dyn_cast<Function>(GA->getAliaseeObject());
      }
      // Check if this is a direct call to a known function or a known
      // intrinsic, or an indirect call with profile data.
      if (CalledFunction) {
        if (CI && CalledFunction->isIntrinsic()) {
          addIntrinsicToSummary(
              CI, TypeTests, TypeTestAssumeVCalls, TypeCheckedLoadVCalls,
              TypeTestAssumeConstVCalls, TypeCheckedLoadConstVCalls, DT);
          continue;
        }
        // We should have named any anonymous globals
        assert(CalledFunction->hasName());
        auto ScaledCount = PSI->getProfileCount(*CB, BFI);
        auto Hotness = ScaledCount ? getHotness(*ScaledCount, PSI)
                                   : CalleeInfo::HotnessType::Unknown;
        if (ForceSummaryEdgesCold != FunctionSummary::FSHT_None)
          Hotness = CalleeInfo::HotnessType::Cold;

        // Use the original CalledValue, in case it was an alias. We want
        // to record the call edge to the alias in that case. Eventually
        // an alias summary will be created to associate the alias and
        // aliasee.
        auto &ValueInfo = CallGraphEdges[Index.getOrInsertValueInfo(
            cast<GlobalValue>(CalledValue))];
        ValueInfo.updateHotness(Hotness);
        if (CB->isTailCall())
          ValueInfo.setHasTailCall(true);
        // Add the relative block frequency to CalleeInfo if there is no profile
        // information.
        if (BFI != nullptr && Hotness == CalleeInfo::HotnessType::Unknown) {
          uint64_t BBFreq = BFI->getBlockFreq(&BB).getFrequency();
          uint64_t EntryFreq = BFI->getEntryFreq().getFrequency();
          ValueInfo.updateRelBlockFreq(BBFreq, EntryFreq);
        }
      } else {
        HasUnknownCall = true;
        // If F is imported, a local linkage ifunc (e.g. target_clones on a
        // static function) called by F will be cloned. Since summaries don't
        // track ifunc, we do not know implementation functions referenced by
        // the ifunc resolver need to be promoted in the exporter, and we will
        // get linker errors due to cloned declarations for implementation
        // functions. As a simple fix, just mark F as not eligible for import.
        // Non-local ifunc is not cloned and does not have the issue.
        if (auto *GI = dyn_cast_if_present<GlobalIFunc>(CalledValue))
          if (GI->hasLocalLinkage())
            HasLocalIFuncCallOrRef = true;
        // Skip inline assembly calls.
        if (CI && CI->isInlineAsm())
          continue;
        // Skip direct calls.
        if (!CalledValue || isa<Constant>(CalledValue))
          continue;

        // Check if the instruction has a callees metadata. If so, add callees
        // to CallGraphEdges to reflect the references from the metadata, and
        // to enable importing for subsequent indirect call promotion and
        // inlining.
        if (auto *MD = I.getMetadata(LLVMContext::MD_callees)) {
          for (const auto &Op : MD->operands()) {
            Function *Callee = mdconst::extract_or_null<Function>(Op);
            if (Callee)
              CallGraphEdges[Index.getOrInsertValueInfo(Callee)];
          }
        }

        uint32_t NumCandidates;
        uint64_t TotalCount;
        auto CandidateProfileData =
            ICallAnalysis.getPromotionCandidatesForInstruction(&I, TotalCount,
                                                               NumCandidates);
        for (const auto &Candidate : CandidateProfileData)
          CallGraphEdges[Index.getOrInsertValueInfo(Candidate.Value)]
              .updateHotness(getHotness(Candidate.Count, PSI));
      }

      // Summarize memprof related metadata. This is only needed for ThinLTO.
      if (!IsThinLTO)
        continue;

      // TODO: Skip indirect calls for now. Need to handle these better, likely
      // by creating multiple Callsites, one per target, then speculatively
      // devirtualize while applying clone info in the ThinLTO backends. This
      // will also be important because we will have a different set of clone
      // versions per target. This handling needs to match that in the ThinLTO
      // backend so we handle things consistently for matching of callsite
      // summaries to instructions.
      if (!CalledFunction)
        continue;

      // Ensure we keep this analysis in sync with the handling in the ThinLTO
      // backend (see MemProfContextDisambiguation::applyImport). Save this call
      // so that we can skip it in checking the reverse case later.
      assert(mayHaveMemprofSummary(CB));
#ifndef NDEBUG
      CallsThatMayHaveMemprofSummary.insert(CB);
#endif

      // Compute the list of stack ids first (so we can trim them from the stack
      // ids on any MIBs).
      CallStack<MDNode, MDNode::op_iterator> InstCallsite(
          I.getMetadata(LLVMContext::MD_callsite));
      auto *MemProfMD = I.getMetadata(LLVMContext::MD_memprof);
      if (MemProfMD) {
        std::vector<MIBInfo> MIBs;
        std::vector<uint64_t> TotalSizes;
        for (auto &MDOp : MemProfMD->operands()) {
          auto *MIBMD = cast<const MDNode>(MDOp);
          MDNode *StackNode = getMIBStackNode(MIBMD);
          assert(StackNode);
          SmallVector<unsigned> StackIdIndices;
          CallStack<MDNode, MDNode::op_iterator> StackContext(StackNode);
          // Collapse out any on the allocation call (inlining).
          for (auto ContextIter =
                   StackContext.beginAfterSharedPrefix(InstCallsite);
               ContextIter != StackContext.end(); ++ContextIter) {
            unsigned StackIdIdx = Index.addOrGetStackIdIndex(*ContextIter);
            // If this is a direct recursion, simply skip the duplicate
            // entries. If this is mutual recursion, handling is left to
            // the LTO link analysis client.
            if (StackIdIndices.empty() || StackIdIndices.back() != StackIdIdx)
              StackIdIndices.push_back(StackIdIdx);
          }
          MIBs.push_back(
              MIBInfo(getMIBAllocType(MIBMD), std::move(StackIdIndices)));
          if (MemProfReportHintedSizes) {
            auto TotalSize = getMIBTotalSize(MIBMD);
            assert(TotalSize);
            TotalSizes.push_back(TotalSize);
          }
        }
        Allocs.push_back(AllocInfo(std::move(MIBs)));
        if (MemProfReportHintedSizes) {
          assert(Allocs.back().MIBs.size() == TotalSizes.size());
          Allocs.back().TotalSizes = std::move(TotalSizes);
        }
      } else if (!InstCallsite.empty()) {
        SmallVector<unsigned> StackIdIndices;
        for (auto StackId : InstCallsite)
          StackIdIndices.push_back(Index.addOrGetStackIdIndex(StackId));
        // Use the original CalledValue, in case it was an alias. We want
        // to record the call edge to the alias in that case. Eventually
        // an alias summary will be created to associate the alias and
        // aliasee.
        auto CalleeValueInfo =
            Index.getOrInsertValueInfo(cast<GlobalValue>(CalledValue));
        Callsites.push_back({CalleeValueInfo, StackIdIndices});
      }
    }
  }

  if (PSI->hasPartialSampleProfile() && ScalePartialSampleProfileWorkingSetSize)
    Index.addBlockCount(F.size());

  std::vector<ValueInfo> Refs;
  if (IsThinLTO) {
    auto AddRefEdges = [&](const std::vector<const Instruction *> &Instrs,
                           SetVector<ValueInfo, std::vector<ValueInfo>> &Edges,
                           SmallPtrSet<const User *, 8> &Cache) {
      for (const auto *I : Instrs) {
        Cache.erase(I);
        findRefEdges(Index, I, Edges, Cache, HasLocalIFuncCallOrRef);
      }
    };

    // By now we processed all instructions in a function, except
    // non-volatile loads and non-volatile value stores. Let's find
    // ref edges for both of instruction sets
    AddRefEdges(NonVolatileLoads, LoadRefEdges, Visited);
    // We can add some values to the Visited set when processing load
    // instructions which are also used by stores in NonVolatileStores.
    // For example this can happen if we have following code:
    //
    // store %Derived* @foo, %Derived** bitcast (%Base** @bar to %Derived**)
    // %42 = load %Derived*, %Derived** bitcast (%Base** @bar to %Derived**)
    //
    // After processing loads we'll add bitcast to the Visited set, and if
    // we use the same set while processing stores, we'll never see store
    // to @bar and @bar will be mistakenly treated as readonly.
    SmallPtrSet<const llvm::User *, 8> StoreCache;
    AddRefEdges(NonVolatileStores, StoreRefEdges, StoreCache);

    // If both load and store instruction reference the same variable
    // we won't be able to optimize it. Add all such reference edges
    // to RefEdges set.
    for (const auto &VI : StoreRefEdges)
      if (LoadRefEdges.remove(VI))
        RefEdges.insert(VI);

    unsigned RefCnt = RefEdges.size();
    // All new reference edges inserted in two loops below are either
    // read or write only. They will be grouped in the end of RefEdges
    // vector, so we can use a single integer value to identify them.
    for (const auto &VI : LoadRefEdges)
      RefEdges.insert(VI);

    unsigned FirstWORef = RefEdges.size();
    for (const auto &VI : StoreRefEdges)
      RefEdges.insert(VI);

    Refs = RefEdges.takeVector();
    for (; RefCnt < FirstWORef; ++RefCnt)
      Refs[RefCnt].setReadOnly();

    for (; RefCnt < Refs.size(); ++RefCnt)
      Refs[RefCnt].setWriteOnly();
  } else {
    Refs = RefEdges.takeVector();
  }
  // Explicit add hot edges to enforce importing for designated GUIDs for
  // sample PGO, to enable the same inlines as the profiled optimized binary.
  for (auto &I : F.getImportGUIDs())
    CallGraphEdges[Index.getOrInsertValueInfo(I)].updateHotness(
        ForceSummaryEdgesCold == FunctionSummary::FSHT_All
            ? CalleeInfo::HotnessType::Cold
            : CalleeInfo::HotnessType::Critical);

#ifndef NDEBUG
  // Make sure that all calls we decided could not have memprof summaries get a
  // false value for mayHaveMemprofSummary, to ensure that this handling remains
  // in sync with the ThinLTO backend handling.
  if (IsThinLTO) {
    for (const BasicBlock &BB : F) {
      for (const Instruction &I : BB) {
        const auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;
        // We already checked these above.
        if (CallsThatMayHaveMemprofSummary.count(CB))
          continue;
        assert(!mayHaveMemprofSummary(CB));
      }
    }
  }
#endif

  bool NonRenamableLocal = isNonRenamableLocal(F);
  bool NotEligibleForImport =
      NonRenamableLocal || HasInlineAsmMaybeReferencingInternal ||
      HasIndirBranchToBlockAddress || HasLocalIFuncCallOrRef;
  GlobalValueSummary::GVFlags Flags(
      F.getLinkage(), F.getVisibility(), NotEligibleForImport,
      /* Live = */ false, F.isDSOLocal(), F.canBeOmittedFromSymbolTable(),
      GlobalValueSummary::ImportKind::Definition);
  FunctionSummary::FFlags FunFlags{
      F.doesNotAccessMemory(), F.onlyReadsMemory() && !F.doesNotAccessMemory(),
      F.hasFnAttribute(Attribute::NoRecurse), F.returnDoesNotAlias(),
      // FIXME: refactor this to use the same code that inliner is using.
      // Don't try to import functions with noinline attribute.
      F.getAttributes().hasFnAttr(Attribute::NoInline),
      F.hasFnAttribute(Attribute::AlwaysInline),
      F.hasFnAttribute(Attribute::NoUnwind), MayThrow, HasUnknownCall,
      mustBeUnreachableFunction(F)};
  std::vector<FunctionSummary::ParamAccess> ParamAccesses;
  if (auto *SSI = GetSSICallback(F))
    ParamAccesses = SSI->getParamAccesses(Index);
  auto FuncSummary = std::make_unique<FunctionSummary>(
      Flags, NumInsts, FunFlags, /*EntryCount=*/0, std::move(Refs),
      CallGraphEdges.takeVector(), TypeTests.takeVector(),
      TypeTestAssumeVCalls.takeVector(), TypeCheckedLoadVCalls.takeVector(),
      TypeTestAssumeConstVCalls.takeVector(),
      TypeCheckedLoadConstVCalls.takeVector(), std::move(ParamAccesses),
      std::move(Callsites), std::move(Allocs));
  if (NonRenamableLocal)
    CantBePromoted.insert(F.getGUID());
  Index.addGlobalValueSummary(F, std::move(FuncSummary));
}

/// Find function pointers referenced within the given vtable initializer
/// (or subset of an initializer) \p I. The starting offset of \p I within
/// the vtable initializer is \p StartingOffset. Any discovered function
/// pointers are added to \p VTableFuncs along with their cumulative offset
/// within the initializer.
static void findFuncPointers(const Constant *I, uint64_t StartingOffset,
                             const Module &M, ModuleSummaryIndex &Index,
                             VTableFuncList &VTableFuncs,
                             const GlobalVariable &OrigGV) {
  // First check if this is a function pointer.
  if (I->getType()->isPointerTy()) {
    auto C = I->stripPointerCasts();
    auto A = dyn_cast<GlobalAlias>(C);
    if (isa<Function>(C) || (A && isa<Function>(A->getAliasee()))) {
      auto GV = dyn_cast<GlobalValue>(C);
      assert(GV);
      // We can disregard __cxa_pure_virtual as a possible call target, as
      // calls to pure virtuals are UB.
      if (GV && GV->getName() != "__cxa_pure_virtual")
        VTableFuncs.push_back({Index.getOrInsertValueInfo(GV), StartingOffset});
      return;
    }
  }

  // Walk through the elements in the constant struct or array and recursively
  // look for virtual function pointers.
  const DataLayout &DL = M.getDataLayout();
  if (auto *C = dyn_cast<ConstantStruct>(I)) {
    StructType *STy = dyn_cast<StructType>(C->getType());
    assert(STy);
    const StructLayout *SL = DL.getStructLayout(C->getType());

    for (auto EI : llvm::enumerate(STy->elements())) {
      auto Offset = SL->getElementOffset(EI.index());
      unsigned Op = SL->getElementContainingOffset(Offset);
      findFuncPointers(cast<Constant>(I->getOperand(Op)),
                       StartingOffset + Offset, M, Index, VTableFuncs, OrigGV);
    }
  } else if (auto *C = dyn_cast<ConstantArray>(I)) {
    ArrayType *ATy = C->getType();
    Type *EltTy = ATy->getElementType();
    uint64_t EltSize = DL.getTypeAllocSize(EltTy);
    for (unsigned i = 0, e = ATy->getNumElements(); i != e; ++i) {
      findFuncPointers(cast<Constant>(I->getOperand(i)),
                       StartingOffset + i * EltSize, M, Index, VTableFuncs,
                       OrigGV);
    }
  } else if (const auto *CE = dyn_cast<ConstantExpr>(I)) {
    // For relative vtables, the next sub-component should be a trunc.
    if (CE->getOpcode() != Instruction::Trunc ||
        !(CE = dyn_cast<ConstantExpr>(CE->getOperand(0))))
      return;

    // If this constant can be reduced to the offset between a function and a
    // global, then we know this is a valid virtual function if the RHS is the
    // original vtable we're scanning through.
    if (CE->getOpcode() == Instruction::Sub) {
      GlobalValue *LHS, *RHS;
      APSInt LHSOffset, RHSOffset;
      if (IsConstantOffsetFromGlobal(CE->getOperand(0), LHS, LHSOffset, DL) &&
          IsConstantOffsetFromGlobal(CE->getOperand(1), RHS, RHSOffset, DL) &&
          RHS == &OrigGV &&

          // For relative vtables, this component should point to the callable
          // function without any offsets.
          LHSOffset == 0 &&

          // Also, the RHS should always point to somewhere within the vtable.
          RHSOffset <=
              static_cast<uint64_t>(DL.getTypeAllocSize(OrigGV.getInitializer()->getType()))) {
        findFuncPointers(LHS, StartingOffset, M, Index, VTableFuncs, OrigGV);
      }
    }
  }
}

// Identify the function pointers referenced by vtable definition \p V.
static void computeVTableFuncs(ModuleSummaryIndex &Index,
                               const GlobalVariable &V, const Module &M,
                               VTableFuncList &VTableFuncs) {
  if (!V.isConstant())
    return;

  findFuncPointers(V.getInitializer(), /*StartingOffset=*/0, M, Index,
                   VTableFuncs, V);

#ifndef NDEBUG
  // Validate that the VTableFuncs list is ordered by offset.
  uint64_t PrevOffset = 0;
  for (auto &P : VTableFuncs) {
    // The findVFuncPointers traversal should have encountered the
    // functions in offset order. We need to use ">=" since PrevOffset
    // starts at 0.
    assert(P.VTableOffset >= PrevOffset);
    PrevOffset = P.VTableOffset;
  }
#endif
}

/// Record vtable definition \p V for each type metadata it references.
static void
recordTypeIdCompatibleVtableReferences(ModuleSummaryIndex &Index,
                                       const GlobalVariable &V,
                                       SmallVectorImpl<MDNode *> &Types) {
  for (MDNode *Type : Types) {
    auto TypeID = Type->getOperand(1).get();

    uint64_t Offset =
        cast<ConstantInt>(
            cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
            ->getZExtValue();

    if (auto *TypeId = dyn_cast<MDString>(TypeID))
      Index.getOrInsertTypeIdCompatibleVtableSummary(TypeId->getString())
          .push_back({Offset, Index.getOrInsertValueInfo(&V)});
  }
}

static void computeVariableSummary(ModuleSummaryIndex &Index,
                                   const GlobalVariable &V,
                                   DenseSet<GlobalValue::GUID> &CantBePromoted,
                                   const Module &M,
                                   SmallVectorImpl<MDNode *> &Types) {
  SetVector<ValueInfo, std::vector<ValueInfo>> RefEdges;
  SmallPtrSet<const User *, 8> Visited;
  bool RefLocalIFunc = false;
  bool HasBlockAddress =
      findRefEdges(Index, &V, RefEdges, Visited, RefLocalIFunc);
  const bool NotEligibleForImport = (HasBlockAddress || RefLocalIFunc);
  bool NonRenamableLocal = isNonRenamableLocal(V);
  GlobalValueSummary::GVFlags Flags(
      V.getLinkage(), V.getVisibility(), NonRenamableLocal,
      /* Live = */ false, V.isDSOLocal(), V.canBeOmittedFromSymbolTable(),
      GlobalValueSummary::Definition);

  VTableFuncList VTableFuncs;
  // If splitting is not enabled, then we compute the summary information
  // necessary for index-based whole program devirtualization.
  if (!Index.enableSplitLTOUnit()) {
    Types.clear();
    V.getMetadata(LLVMContext::MD_type, Types);
    if (!Types.empty()) {
      // Identify the function pointers referenced by this vtable definition.
      computeVTableFuncs(Index, V, M, VTableFuncs);

      // Record this vtable definition for each type metadata it references.
      recordTypeIdCompatibleVtableReferences(Index, V, Types);
    }
  }

  // Don't mark variables we won't be able to internalize as read/write-only.
  bool CanBeInternalized =
      !V.hasComdat() && !V.hasAppendingLinkage() && !V.isInterposable() &&
      !V.hasAvailableExternallyLinkage() && !V.hasDLLExportStorageClass();
  bool Constant = V.isConstant();
  GlobalVarSummary::GVarFlags VarFlags(CanBeInternalized,
                                       Constant ? false : CanBeInternalized,
                                       Constant, V.getVCallVisibility());
  auto GVarSummary = std::make_unique<GlobalVarSummary>(Flags, VarFlags,
                                                         RefEdges.takeVector());
  if (NonRenamableLocal)
    CantBePromoted.insert(V.getGUID());
  if (NotEligibleForImport)
    GVarSummary->setNotEligibleToImport();
  if (!VTableFuncs.empty())
    GVarSummary->setVTableFuncs(VTableFuncs);
  Index.addGlobalValueSummary(V, std::move(GVarSummary));
}

static void computeAliasSummary(ModuleSummaryIndex &Index, const GlobalAlias &A,
                                DenseSet<GlobalValue::GUID> &CantBePromoted) {
  // Skip summary for indirect function aliases as summary for aliasee will not
  // be emitted.
  const GlobalObject *Aliasee = A.getAliaseeObject();
  if (isa<GlobalIFunc>(Aliasee))
    return;
  bool NonRenamableLocal = isNonRenamableLocal(A);
  GlobalValueSummary::GVFlags Flags(
      A.getLinkage(), A.getVisibility(), NonRenamableLocal,
      /* Live = */ false, A.isDSOLocal(), A.canBeOmittedFromSymbolTable(),
      GlobalValueSummary::Definition);
  auto AS = std::make_unique<AliasSummary>(Flags);
  auto AliaseeVI = Index.getValueInfo(Aliasee->getGUID());
  assert(AliaseeVI && "Alias expects aliasee summary to be available");
  assert(AliaseeVI.getSummaryList().size() == 1 &&
         "Expected a single entry per aliasee in per-module index");
  AS->setAliasee(AliaseeVI, AliaseeVI.getSummaryList()[0].get());
  if (NonRenamableLocal)
    CantBePromoted.insert(A.getGUID());
  Index.addGlobalValueSummary(A, std::move(AS));
}

// Set LiveRoot flag on entries matching the given value name.
static void setLiveRoot(ModuleSummaryIndex &Index, StringRef Name) {
  if (ValueInfo VI = Index.getValueInfo(GlobalValue::getGUID(Name)))
    for (const auto &Summary : VI.getSummaryList())
      Summary->setLive(true);
}

ModuleSummaryIndex llvm::buildModuleSummaryIndex(
    const Module &M,
    std::function<BlockFrequencyInfo *(const Function &F)> GetBFICallback,
    ProfileSummaryInfo *PSI,
    std::function<const StackSafetyInfo *(const Function &F)> GetSSICallback) {
  assert(PSI);
  bool EnableSplitLTOUnit = false;
  bool UnifiedLTO = false;
  if (auto *MD = mdconst::extract_or_null<ConstantInt>(
          M.getModuleFlag("EnableSplitLTOUnit")))
    EnableSplitLTOUnit = MD->getZExtValue();
  if (auto *MD =
          mdconst::extract_or_null<ConstantInt>(M.getModuleFlag("UnifiedLTO")))
    UnifiedLTO = MD->getZExtValue();
  ModuleSummaryIndex Index(/*HaveGVs=*/true, EnableSplitLTOUnit, UnifiedLTO);

  // Identify the local values in the llvm.used and llvm.compiler.used sets,
  // which should not be exported as they would then require renaming and
  // promotion, but we may have opaque uses e.g. in inline asm. We collect them
  // here because we use this information to mark functions containing inline
  // assembly calls as not importable.
  SmallPtrSet<GlobalValue *, 4> LocalsUsed;
  SmallVector<GlobalValue *, 4> Used;
  // First collect those in the llvm.used set.
  collectUsedGlobalVariables(M, Used, /*CompilerUsed=*/false);
  // Next collect those in the llvm.compiler.used set.
  collectUsedGlobalVariables(M, Used, /*CompilerUsed=*/true);
  DenseSet<GlobalValue::GUID> CantBePromoted;
  for (auto *V : Used) {
    if (V->hasLocalLinkage()) {
      LocalsUsed.insert(V);
      CantBePromoted.insert(V->getGUID());
    }
  }

  bool HasLocalInlineAsmSymbol = false;
  if (!M.getModuleInlineAsm().empty()) {
    // Collect the local values defined by module level asm, and set up
    // summaries for these symbols so that they can be marked as NoRename,
    // to prevent export of any use of them in regular IR that would require
    // renaming within the module level asm. Note we don't need to create a
    // summary for weak or global defs, as they don't need to be flagged as
    // NoRename, and defs in module level asm can't be imported anyway.
    // Also, any values used but not defined within module level asm should
    // be listed on the llvm.used or llvm.compiler.used global and marked as
    // referenced from there.
    ModuleSymbolTable::CollectAsmSymbols(
        M, [&](StringRef Name, object::BasicSymbolRef::Flags Flags) {
          // Symbols not marked as Weak or Global are local definitions.
          if (Flags & (object::BasicSymbolRef::SF_Weak |
                       object::BasicSymbolRef::SF_Global))
            return;
          HasLocalInlineAsmSymbol = true;
          GlobalValue *GV = M.getNamedValue(Name);
          if (!GV)
            return;
          assert(GV->isDeclaration() && "Def in module asm already has definition");
          GlobalValueSummary::GVFlags GVFlags(
              GlobalValue::InternalLinkage, GlobalValue::DefaultVisibility,
              /* NotEligibleToImport = */ true,
              /* Live = */ true,
              /* Local */ GV->isDSOLocal(), GV->canBeOmittedFromSymbolTable(),
              GlobalValueSummary::Definition);
          CantBePromoted.insert(GV->getGUID());
          // Create the appropriate summary type.
          if (Function *F = dyn_cast<Function>(GV)) {
            std::unique_ptr<FunctionSummary> Summary =
                std::make_unique<FunctionSummary>(
                    GVFlags, /*InstCount=*/0,
                    FunctionSummary::FFlags{
                        F->hasFnAttribute(Attribute::ReadNone),
                        F->hasFnAttribute(Attribute::ReadOnly),
                        F->hasFnAttribute(Attribute::NoRecurse),
                        F->returnDoesNotAlias(),
                        /* NoInline = */ false,
                        F->hasFnAttribute(Attribute::AlwaysInline),
                        F->hasFnAttribute(Attribute::NoUnwind),
                        /* MayThrow */ true,
                        /* HasUnknownCall */ true,
                        /* MustBeUnreachable */ false},
                    /*EntryCount=*/0, ArrayRef<ValueInfo>{},
                    ArrayRef<FunctionSummary::EdgeTy>{},
                    ArrayRef<GlobalValue::GUID>{},
                    ArrayRef<FunctionSummary::VFuncId>{},
                    ArrayRef<FunctionSummary::VFuncId>{},
                    ArrayRef<FunctionSummary::ConstVCall>{},
                    ArrayRef<FunctionSummary::ConstVCall>{},
                    ArrayRef<FunctionSummary::ParamAccess>{},
                    ArrayRef<CallsiteInfo>{}, ArrayRef<AllocInfo>{});
            Index.addGlobalValueSummary(*GV, std::move(Summary));
          } else {
            std::unique_ptr<GlobalVarSummary> Summary =
                std::make_unique<GlobalVarSummary>(
                    GVFlags,
                    GlobalVarSummary::GVarFlags(
                        false, false, cast<GlobalVariable>(GV)->isConstant(),
                        GlobalObject::VCallVisibilityPublic),
                    ArrayRef<ValueInfo>{});
            Index.addGlobalValueSummary(*GV, std::move(Summary));
          }
        });
  }

  bool IsThinLTO = true;
  if (auto *MD =
          mdconst::extract_or_null<ConstantInt>(M.getModuleFlag("ThinLTO")))
    IsThinLTO = MD->getZExtValue();

  // Compute summaries for all functions defined in module, and save in the
  // index.
  for (const auto &F : M) {
    if (F.isDeclaration())
      continue;

    DominatorTree DT(const_cast<Function &>(F));
    BlockFrequencyInfo *BFI = nullptr;
    std::unique_ptr<BlockFrequencyInfo> BFIPtr;
    if (GetBFICallback)
      BFI = GetBFICallback(F);
    else if (F.hasProfileData()) {
      LoopInfo LI{DT};
      BranchProbabilityInfo BPI{F, LI};
      BFIPtr = std::make_unique<BlockFrequencyInfo>(F, BPI, LI);
      BFI = BFIPtr.get();
    }

    computeFunctionSummary(Index, M, F, BFI, PSI, DT,
                           !LocalsUsed.empty() || HasLocalInlineAsmSymbol,
                           CantBePromoted, IsThinLTO, GetSSICallback);
  }

  // Compute summaries for all variables defined in module, and save in the
  // index.
  SmallVector<MDNode *, 2> Types;
  for (const GlobalVariable &G : M.globals()) {
    if (G.isDeclaration())
      continue;
    computeVariableSummary(Index, G, CantBePromoted, M, Types);
  }

  // Compute summaries for all aliases defined in module, and save in the
  // index.
  for (const GlobalAlias &A : M.aliases())
    computeAliasSummary(Index, A, CantBePromoted);

  // Iterate through ifuncs, set their resolvers all alive.
  for (const GlobalIFunc &I : M.ifuncs()) {
    I.applyAlongResolverPath([&Index](const GlobalValue &GV) {
      Index.getGlobalValueSummary(GV)->setLive(true);
    });
  }

  for (auto *V : LocalsUsed) {
    auto *Summary = Index.getGlobalValueSummary(*V);
    assert(Summary && "Missing summary for global value");
    Summary->setNotEligibleToImport();
  }

  // The linker doesn't know about these LLVM produced values, so we need
  // to flag them as live in the index to ensure index-based dead value
  // analysis treats them as live roots of the analysis.
  setLiveRoot(Index, "llvm.used");
  setLiveRoot(Index, "llvm.compiler.used");
  setLiveRoot(Index, "llvm.global_ctors");
  setLiveRoot(Index, "llvm.global_dtors");
  setLiveRoot(Index, "llvm.global.annotations");

  for (auto &GlobalList : Index) {
    // Ignore entries for references that are undefined in the current module.
    if (GlobalList.second.SummaryList.empty())
      continue;

    assert(GlobalList.second.SummaryList.size() == 1 &&
           "Expected module's index to have one summary per GUID");
    auto &Summary = GlobalList.second.SummaryList[0];
    if (!IsThinLTO) {
      Summary->setNotEligibleToImport();
      continue;
    }

    bool AllRefsCanBeExternallyReferenced =
        llvm::all_of(Summary->refs(), [&](const ValueInfo &VI) {
          return !CantBePromoted.count(VI.getGUID());
        });
    if (!AllRefsCanBeExternallyReferenced) {
      Summary->setNotEligibleToImport();
      continue;
    }

    if (auto *FuncSummary = dyn_cast<FunctionSummary>(Summary.get())) {
      bool AllCallsCanBeExternallyReferenced = llvm::all_of(
          FuncSummary->calls(), [&](const FunctionSummary::EdgeTy &Edge) {
            return !CantBePromoted.count(Edge.first.getGUID());
          });
      if (!AllCallsCanBeExternallyReferenced)
        Summary->setNotEligibleToImport();
    }
  }

  if (!ModuleSummaryDotFile.empty()) {
    std::error_code EC;
    raw_fd_ostream OSDot(ModuleSummaryDotFile, EC, sys::fs::OpenFlags::OF_Text);
    if (EC)
      report_fatal_error(Twine("Failed to open dot file ") +
                         ModuleSummaryDotFile + ": " + EC.message() + "\n");
    Index.exportToDot(OSDot, {});
  }

  return Index;
}

AnalysisKey ModuleSummaryIndexAnalysis::Key;

ModuleSummaryIndex
ModuleSummaryIndexAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
  ProfileSummaryInfo &PSI = AM.getResult<ProfileSummaryAnalysis>(M);
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  bool NeedSSI = needsParamAccessSummary(M);
  return buildModuleSummaryIndex(
      M,
      [&FAM](const Function &F) {
        return &FAM.getResult<BlockFrequencyAnalysis>(
            *const_cast<Function *>(&F));
      },
      &PSI,
      [&FAM, NeedSSI](const Function &F) -> const StackSafetyInfo * {
        return NeedSSI ? &FAM.getResult<StackSafetyAnalysis>(
                             const_cast<Function &>(F))
                       : nullptr;
      });
}

char ModuleSummaryIndexWrapperPass::ID = 0;

INITIALIZE_PASS_BEGIN(ModuleSummaryIndexWrapperPass, "module-summary-analysis",
                      "Module Summary Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(StackSafetyInfoWrapperPass)
INITIALIZE_PASS_END(ModuleSummaryIndexWrapperPass, "module-summary-analysis",
                    "Module Summary Analysis", false, true)

ModulePass *llvm::createModuleSummaryIndexWrapperPass() {
  return new ModuleSummaryIndexWrapperPass();
}

ModuleSummaryIndexWrapperPass::ModuleSummaryIndexWrapperPass()
    : ModulePass(ID) {
  initializeModuleSummaryIndexWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool ModuleSummaryIndexWrapperPass::runOnModule(Module &M) {
  auto *PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  bool NeedSSI = needsParamAccessSummary(M);
  Index.emplace(buildModuleSummaryIndex(
      M,
      [this](const Function &F) {
        return &(this->getAnalysis<BlockFrequencyInfoWrapperPass>(
                         *const_cast<Function *>(&F))
                     .getBFI());
      },
      PSI,
      [&](const Function &F) -> const StackSafetyInfo * {
        return NeedSSI ? &getAnalysis<StackSafetyInfoWrapperPass>(
                              const_cast<Function &>(F))
                              .getResult()
                       : nullptr;
      }));
  return false;
}

bool ModuleSummaryIndexWrapperPass::doFinalization(Module &M) {
  Index.reset();
  return false;
}

void ModuleSummaryIndexWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  AU.addRequired<StackSafetyInfoWrapperPass>();
}

char ImmutableModuleSummaryIndexWrapperPass::ID = 0;

ImmutableModuleSummaryIndexWrapperPass::ImmutableModuleSummaryIndexWrapperPass(
    const ModuleSummaryIndex *Index)
    : ImmutablePass(ID), Index(Index) {
  initializeImmutableModuleSummaryIndexWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

void ImmutableModuleSummaryIndexWrapperPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

ImmutablePass *llvm::createImmutableModuleSummaryIndexWrapperPass(
    const ModuleSummaryIndex *Index) {
  return new ImmutableModuleSummaryIndexWrapperPass(Index);
}

INITIALIZE_PASS(ImmutableModuleSummaryIndexWrapperPass, "module-summary-info",
                "Module summary info", false, true)

bool llvm::mayHaveMemprofSummary(const CallBase *CB) {
  if (!CB)
    return false;
  if (CB->isDebugOrPseudoInst())
    return false;
  auto *CI = dyn_cast<CallInst>(CB);
  auto *CalledValue = CB->getCalledOperand();
  auto *CalledFunction = CB->getCalledFunction();
  if (CalledValue && !CalledFunction) {
    CalledValue = CalledValue->stripPointerCasts();
    // Stripping pointer casts can reveal a called function.
    CalledFunction = dyn_cast<Function>(CalledValue);
  }
  // Check if this is an alias to a function. If so, get the
  // called aliasee for the checks below.
  if (auto *GA = dyn_cast<GlobalAlias>(CalledValue)) {
    assert(!CalledFunction &&
           "Expected null called function in callsite for alias");
    CalledFunction = dyn_cast<Function>(GA->getAliaseeObject());
  }
  // Check if this is a direct call to a known function or a known
  // intrinsic, or an indirect call with profile data.
  if (CalledFunction) {
    if (CI && CalledFunction->isIntrinsic())
      return false;
  } else {
    // TODO: For now skip indirect calls. See comments in
    // computeFunctionSummary for what is needed to handle this.
    return false;
  }
  return true;
}
