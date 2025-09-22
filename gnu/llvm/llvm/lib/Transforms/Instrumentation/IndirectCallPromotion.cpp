//===- IndirectCallPromotion.cpp - Optimizations based on value profiling -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the transformation that promotes indirect calls to
// conditional direct calls when the indirect-call value profile metadata is
// available.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/IndirectCallPromotionAnalysis.h"
#include "llvm/Analysis/IndirectCallVisitor.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Value.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Utils/CallPromotionUtils.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "pgo-icall-prom"

STATISTIC(NumOfPGOICallPromotion, "Number of indirect call promotions.");
STATISTIC(NumOfPGOICallsites, "Number of indirect call candidate sites.");

extern cl::opt<unsigned> MaxNumVTableAnnotations;

namespace llvm {
extern cl::opt<bool> EnableVTableProfileUse;
}

// Command line option to disable indirect-call promotion with the default as
// false. This is for debug purpose.
static cl::opt<bool> DisableICP("disable-icp", cl::init(false), cl::Hidden,
                                cl::desc("Disable indirect call promotion"));

// Set the cutoff value for the promotion. If the value is other than 0, we
// stop the transformation once the total number of promotions equals the cutoff
// value.
// For debug use only.
static cl::opt<unsigned>
    ICPCutOff("icp-cutoff", cl::init(0), cl::Hidden,
              cl::desc("Max number of promotions for this compilation"));

// If ICPCSSkip is non zero, the first ICPCSSkip callsites will be skipped.
// For debug use only.
static cl::opt<unsigned>
    ICPCSSkip("icp-csskip", cl::init(0), cl::Hidden,
              cl::desc("Skip Callsite up to this number for this compilation"));

// Set if the pass is called in LTO optimization. The difference for LTO mode
// is the pass won't prefix the source module name to the internal linkage
// symbols.
static cl::opt<bool> ICPLTOMode("icp-lto", cl::init(false), cl::Hidden,
                                cl::desc("Run indirect-call promotion in LTO "
                                         "mode"));

// Set if the pass is called in SamplePGO mode. The difference for SamplePGO
// mode is it will add prof metadatato the created direct call.
static cl::opt<bool>
    ICPSamplePGOMode("icp-samplepgo", cl::init(false), cl::Hidden,
                     cl::desc("Run indirect-call promotion in SamplePGO mode"));

// If the option is set to true, only call instructions will be considered for
// transformation -- invoke instructions will be ignored.
static cl::opt<bool>
    ICPCallOnly("icp-call-only", cl::init(false), cl::Hidden,
                cl::desc("Run indirect-call promotion for call instructions "
                         "only"));

// If the option is set to true, only invoke instructions will be considered for
// transformation -- call instructions will be ignored.
static cl::opt<bool> ICPInvokeOnly("icp-invoke-only", cl::init(false),
                                   cl::Hidden,
                                   cl::desc("Run indirect-call promotion for "
                                            "invoke instruction only"));

// Dump the function level IR if the transformation happened in this
// function. For debug use only.
static cl::opt<bool>
    ICPDUMPAFTER("icp-dumpafter", cl::init(false), cl::Hidden,
                 cl::desc("Dump IR after transformation happens"));

// Indirect call promotion pass will fall back to function-based comparison if
// vtable-count / function-count is smaller than this threshold.
static cl::opt<float> ICPVTablePercentageThreshold(
    "icp-vtable-percentage-threshold", cl::init(0.99), cl::Hidden,
    cl::desc("The percentage threshold of vtable-count / function-count for "
             "cost-benefit analysis."));

// Although comparing vtables can save a vtable load, we may need to compare
// vtable pointer with multiple vtable address points due to class inheritance.
// Comparing with multiple vtables inserts additional instructions on hot code
// path, and doing so for an earlier candidate delays the comparisons for later
// candidates. For the last candidate, only the fallback path is affected.
// We allow multiple vtable comparison for the last function candidate and use
// the option below to cap the number of vtables.
static cl::opt<int> ICPMaxNumVTableLastCandidate(
    "icp-max-num-vtable-last-candidate", cl::init(1), cl::Hidden,
    cl::desc("The maximum number of vtable for the last candidate."));

namespace {

// The key is a vtable global variable, and the value is a map.
// In the inner map, the key represents address point offsets and the value is a
// constant for this address point.
using VTableAddressPointOffsetValMap =
    SmallDenseMap<const GlobalVariable *, std::unordered_map<int, Constant *>>;

// A struct to collect type information for a virtual call site.
struct VirtualCallSiteInfo {
  // The offset from the address point to virtual function in the vtable.
  uint64_t FunctionOffset;
  // The instruction that computes the address point of vtable.
  Instruction *VPtr;
  // The compatible type used in LLVM type intrinsics.
  StringRef CompatibleTypeStr;
};

// The key is a virtual call, and value is its type information.
using VirtualCallSiteTypeInfoMap =
    SmallDenseMap<const CallBase *, VirtualCallSiteInfo>;

// The key is vtable GUID, and value is its value profile count.
using VTableGUIDCountsMap = SmallDenseMap<uint64_t, uint64_t, 16>;

// Return the address point offset of the given compatible type.
//
// Type metadata of a vtable specifies the types that can contain a pointer to
// this vtable, for example, `Base*` can be a pointer to an derived type
// but not vice versa. See also https://llvm.org/docs/TypeMetadata.html
static std::optional<uint64_t>
getAddressPointOffset(const GlobalVariable &VTableVar,
                      StringRef CompatibleType) {
  SmallVector<MDNode *> Types;
  VTableVar.getMetadata(LLVMContext::MD_type, Types);

  for (MDNode *Type : Types)
    if (auto *TypeId = dyn_cast<MDString>(Type->getOperand(1).get());
        TypeId && TypeId->getString() == CompatibleType)
      return cast<ConstantInt>(
                 cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
          ->getZExtValue();

  return std::nullopt;
}

// Return a constant representing the vtable's address point specified by the
// offset.
static Constant *getVTableAddressPointOffset(GlobalVariable *VTable,
                                             uint32_t AddressPointOffset) {
  Module &M = *VTable->getParent();
  LLVMContext &Context = M.getContext();
  assert(AddressPointOffset <
             M.getDataLayout().getTypeAllocSize(VTable->getValueType()) &&
         "Out-of-bound access");

  return ConstantExpr::getInBoundsGetElementPtr(
      Type::getInt8Ty(Context), VTable,
      llvm::ConstantInt::get(Type::getInt32Ty(Context), AddressPointOffset));
}

// Return the basic block in which Use `U` is used via its `UserInst`.
static BasicBlock *getUserBasicBlock(Use &U, Instruction *UserInst) {
  if (PHINode *PN = dyn_cast<PHINode>(UserInst))
    return PN->getIncomingBlock(U);

  return UserInst->getParent();
}

// `DestBB` is a suitable basic block to sink `Inst` into when `Inst` have users
// and all users are in `DestBB`. The caller guarantees that `Inst->getParent()`
// is the sole predecessor of `DestBB` and `DestBB` is dominated by
// `Inst->getParent()`.
static bool isDestBBSuitableForSink(Instruction *Inst, BasicBlock *DestBB) {
  // 'BB' is used only by assert.
  [[maybe_unused]] BasicBlock *BB = Inst->getParent();

  assert(BB != DestBB && BB->getTerminator()->getNumSuccessors() == 2 &&
         DestBB->getUniquePredecessor() == BB &&
         "Guaranteed by ICP transformation");

  BasicBlock *UserBB = nullptr;
  for (Use &Use : Inst->uses()) {
    User *User = Use.getUser();
    // Do checked cast since IR verifier guarantees that the user of an
    // instruction must be an instruction. See `Verifier::visitInstruction`.
    Instruction *UserInst = cast<Instruction>(User);
    // We can sink debug or pseudo instructions together with Inst.
    if (UserInst->isDebugOrPseudoInst())
      continue;
    UserBB = getUserBasicBlock(Use, UserInst);
    // Do not sink if Inst is used in a basic block that is not DestBB.
    // TODO: Sink to the common dominator of all user blocks.
    if (UserBB != DestBB)
      return false;
  }
  return UserBB != nullptr;
}

// For the virtual call dispatch sequence, try to sink vtable load instructions
// to the cold indirect call fallback.
// FIXME: Move the sink eligibility check below to a utility function in
// Transforms/Utils/ directory.
static bool tryToSinkInstruction(Instruction *I, BasicBlock *DestBlock) {
  if (!isDestBBSuitableForSink(I, DestBlock))
    return false;

  // Do not move control-flow-involving, volatile loads, vaarg, alloca
  // instructions, etc.
  if (isa<PHINode>(I) || I->isEHPad() || I->mayThrow() || !I->willReturn() ||
      isa<AllocaInst>(I))
    return false;

  // Do not sink convergent call instructions.
  if (const auto *C = dyn_cast<CallBase>(I))
    if (C->isInlineAsm() || C->cannotMerge() || C->isConvergent())
      return false;

  // Do not move an instruction that may write to memory.
  if (I->mayWriteToMemory())
    return false;

  // We can only sink load instructions if there is nothing between the load and
  // the end of block that could change the value.
  if (I->mayReadFromMemory()) {
    // We already know that SrcBlock is the unique predecessor of DestBlock.
    for (BasicBlock::iterator Scan = std::next(I->getIterator()),
                              E = I->getParent()->end();
         Scan != E; ++Scan) {
      // Note analysis analysis can tell whether two pointers can point to the
      // same object in memory or not thereby find further opportunities to
      // sink.
      if (Scan->mayWriteToMemory())
        return false;
    }
  }

  BasicBlock::iterator InsertPos = DestBlock->getFirstInsertionPt();
  I->moveBefore(*DestBlock, InsertPos);

  // TODO: Sink debug intrinsic users of I to 'DestBlock'.
  // 'InstCombinerImpl::tryToSinkInstructionDbgValues' and
  // 'InstCombinerImpl::tryToSinkInstructionDbgVariableRecords' already have
  // the core logic to do this.
  return true;
}

// Try to sink instructions after VPtr to the indirect call fallback.
// Return the number of sunk IR instructions.
static int tryToSinkInstructions(BasicBlock *OriginalBB,
                                 BasicBlock *IndirectCallBB) {
  int SinkCount = 0;
  // Do not sink across a critical edge for simplicity.
  if (IndirectCallBB->getUniquePredecessor() != OriginalBB)
    return SinkCount;
  // Sink all eligible instructions in OriginalBB in reverse order.
  for (Instruction &I :
       llvm::make_early_inc_range(llvm::drop_begin(llvm::reverse(*OriginalBB))))
    if (tryToSinkInstruction(&I, IndirectCallBB))
      SinkCount++;

  return SinkCount;
}

// Promote indirect calls to conditional direct calls, keeping track of
// thresholds.
class IndirectCallPromoter {
private:
  Function &F;
  Module &M;

  ProfileSummaryInfo *PSI = nullptr;

  // Symtab that maps indirect call profile values to function names and
  // defines.
  InstrProfSymtab *const Symtab;

  const bool SamplePGO;

  // A map from a virtual call to its type information.
  const VirtualCallSiteTypeInfoMap &VirtualCSInfo;

  VTableAddressPointOffsetValMap &VTableAddressPointOffsetVal;

  OptimizationRemarkEmitter &ORE;

  // A struct that records the direct target and it's call count.
  struct PromotionCandidate {
    Function *const TargetFunction;
    const uint64_t Count;

    // The following fields only exists for promotion candidates with vtable
    // information.
    //
    // Due to class inheritance, one virtual call candidate can come from
    // multiple vtables. `VTableGUIDAndCounts` tracks the vtable GUIDs and
    // counts for 'TargetFunction'. `AddressPoints` stores the vtable address
    // points for comparison.
    VTableGUIDCountsMap VTableGUIDAndCounts;
    SmallVector<Constant *> AddressPoints;

    PromotionCandidate(Function *F, uint64_t C) : TargetFunction(F), Count(C) {}
  };

  // Check if the indirect-call call site should be promoted. Return the number
  // of promotions. Inst is the candidate indirect call, ValueDataRef
  // contains the array of value profile data for profiled targets,
  // TotalCount is the total profiled count of call executions, and
  // NumCandidates is the number of candidate entries in ValueDataRef.
  std::vector<PromotionCandidate> getPromotionCandidatesForCallSite(
      const CallBase &CB, ArrayRef<InstrProfValueData> ValueDataRef,
      uint64_t TotalCount, uint32_t NumCandidates);

  // Promote a list of targets for one indirect-call callsite by comparing
  // indirect callee with functions. Return true if there are IR
  // transformations and false otherwise.
  bool tryToPromoteWithFuncCmp(CallBase &CB, Instruction *VPtr,
                               ArrayRef<PromotionCandidate> Candidates,
                               uint64_t TotalCount,
                               ArrayRef<InstrProfValueData> ICallProfDataRef,
                               uint32_t NumCandidates,
                               VTableGUIDCountsMap &VTableGUIDCounts);

  // Promote a list of targets for one indirect call by comparing vtables with
  // functions. Return true if there are IR transformations and false
  // otherwise.
  bool tryToPromoteWithVTableCmp(
      CallBase &CB, Instruction *VPtr, ArrayRef<PromotionCandidate> Candidates,
      uint64_t TotalFuncCount, uint32_t NumCandidates,
      MutableArrayRef<InstrProfValueData> ICallProfDataRef,
      VTableGUIDCountsMap &VTableGUIDCounts);

  // Return true if it's profitable to compare vtables for the callsite.
  bool isProfitableToCompareVTables(const CallBase &CB,
                                    ArrayRef<PromotionCandidate> Candidates,
                                    uint64_t TotalCount);

  // Given an indirect callsite and the list of function candidates, compute
  // the following vtable information in output parameters and return vtable
  // pointer if type profiles exist.
  // - Populate `VTableGUIDCounts` with <vtable-guid, count> using !prof
  // metadata attached on the vtable pointer.
  // - For each function candidate, finds out the vtables from which it gets
  // called and stores the <vtable-guid, count> in promotion candidate.
  Instruction *computeVTableInfos(const CallBase *CB,
                                  VTableGUIDCountsMap &VTableGUIDCounts,
                                  std::vector<PromotionCandidate> &Candidates);

  Constant *getOrCreateVTableAddressPointVar(GlobalVariable *GV,
                                             uint64_t AddressPointOffset);

  void updateFuncValueProfiles(CallBase &CB, ArrayRef<InstrProfValueData> VDs,
                               uint64_t Sum, uint32_t MaxMDCount);

  void updateVPtrValueProfiles(Instruction *VPtr,
                               VTableGUIDCountsMap &VTableGUIDCounts);

public:
  IndirectCallPromoter(
      Function &Func, Module &M, ProfileSummaryInfo *PSI,
      InstrProfSymtab *Symtab, bool SamplePGO,
      const VirtualCallSiteTypeInfoMap &VirtualCSInfo,
      VTableAddressPointOffsetValMap &VTableAddressPointOffsetVal,
      OptimizationRemarkEmitter &ORE)
      : F(Func), M(M), PSI(PSI), Symtab(Symtab), SamplePGO(SamplePGO),
        VirtualCSInfo(VirtualCSInfo),
        VTableAddressPointOffsetVal(VTableAddressPointOffsetVal), ORE(ORE) {}
  IndirectCallPromoter(const IndirectCallPromoter &) = delete;
  IndirectCallPromoter &operator=(const IndirectCallPromoter &) = delete;

  bool processFunction(ProfileSummaryInfo *PSI);
};

} // end anonymous namespace

// Indirect-call promotion heuristic. The direct targets are sorted based on
// the count. Stop at the first target that is not promoted.
std::vector<IndirectCallPromoter::PromotionCandidate>
IndirectCallPromoter::getPromotionCandidatesForCallSite(
    const CallBase &CB, ArrayRef<InstrProfValueData> ValueDataRef,
    uint64_t TotalCount, uint32_t NumCandidates) {
  std::vector<PromotionCandidate> Ret;

  LLVM_DEBUG(dbgs() << " \nWork on callsite #" << NumOfPGOICallsites << CB
                    << " Num_targets: " << ValueDataRef.size()
                    << " Num_candidates: " << NumCandidates << "\n");
  NumOfPGOICallsites++;
  if (ICPCSSkip != 0 && NumOfPGOICallsites <= ICPCSSkip) {
    LLVM_DEBUG(dbgs() << " Skip: User options.\n");
    return Ret;
  }

  for (uint32_t I = 0; I < NumCandidates; I++) {
    uint64_t Count = ValueDataRef[I].Count;
    assert(Count <= TotalCount);
    (void)TotalCount;
    uint64_t Target = ValueDataRef[I].Value;
    LLVM_DEBUG(dbgs() << " Candidate " << I << " Count=" << Count
                      << "  Target_func: " << Target << "\n");

    if (ICPInvokeOnly && isa<CallInst>(CB)) {
      LLVM_DEBUG(dbgs() << " Not promote: User options.\n");
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "UserOptions", &CB)
               << " Not promote: User options";
      });
      break;
    }
    if (ICPCallOnly && isa<InvokeInst>(CB)) {
      LLVM_DEBUG(dbgs() << " Not promote: User option.\n");
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "UserOptions", &CB)
               << " Not promote: User options";
      });
      break;
    }
    if (ICPCutOff != 0 && NumOfPGOICallPromotion >= ICPCutOff) {
      LLVM_DEBUG(dbgs() << " Not promote: Cutoff reached.\n");
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "CutOffReached", &CB)
               << " Not promote: Cutoff reached";
      });
      break;
    }

    // Don't promote if the symbol is not defined in the module. This avoids
    // creating a reference to a symbol that doesn't exist in the module
    // This can happen when we compile with a sample profile collected from
    // one binary but used for another, which may have profiled targets that
    // aren't used in the new binary. We might have a declaration initially in
    // the case where the symbol is globally dead in the binary and removed by
    // ThinLTO.
    Function *TargetFunction = Symtab->getFunction(Target);
    if (TargetFunction == nullptr || TargetFunction->isDeclaration()) {
      LLVM_DEBUG(dbgs() << " Not promote: Cannot find the target\n");
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "UnableToFindTarget", &CB)
               << "Cannot promote indirect call: target with md5sum "
               << ore::NV("target md5sum", Target) << " not found";
      });
      break;
    }

    const char *Reason = nullptr;
    if (!isLegalToPromote(CB, TargetFunction, &Reason)) {
      using namespace ore;

      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "UnableToPromote", &CB)
               << "Cannot promote indirect call to "
               << NV("TargetFunction", TargetFunction) << " with count of "
               << NV("Count", Count) << ": " << Reason;
      });
      break;
    }

    Ret.push_back(PromotionCandidate(TargetFunction, Count));
    TotalCount -= Count;
  }
  return Ret;
}

Constant *IndirectCallPromoter::getOrCreateVTableAddressPointVar(
    GlobalVariable *GV, uint64_t AddressPointOffset) {
  auto [Iter, Inserted] =
      VTableAddressPointOffsetVal[GV].try_emplace(AddressPointOffset, nullptr);
  if (Inserted)
    Iter->second = getVTableAddressPointOffset(GV, AddressPointOffset);
  return Iter->second;
}

Instruction *IndirectCallPromoter::computeVTableInfos(
    const CallBase *CB, VTableGUIDCountsMap &GUIDCountsMap,
    std::vector<PromotionCandidate> &Candidates) {
  if (!EnableVTableProfileUse)
    return nullptr;

  // Take the following code sequence as an example, here is how the code works
  //   @vtable1 = {[n x ptr] [... ptr @func1]}
  //   @vtable2 = {[m x ptr] [... ptr @func2]}
  //
  //   %vptr = load ptr, ptr %d, !prof !0
  //   %0 = tail call i1 @llvm.type.test(ptr %vptr, metadata !"vtable1")
  //   tail call void @llvm.assume(i1 %0)
  //   %vfn = getelementptr inbounds ptr, ptr %vptr, i64 1
  //   %1 = load ptr, ptr %vfn
  //   call void %1(ptr %d), !prof !1
  //
  //   !0 = !{!"VP", i32 2, i64 100, i64 123, i64 50, i64 456, i64 50}
  //   !1 = !{!"VP", i32 0, i64 100, i64 789, i64 50, i64 579, i64 50}
  //
  // Step 1. Find out the %vptr instruction for indirect call and use its !prof
  // to populate `GUIDCountsMap`.
  // Step 2. For each vtable-guid, look up its definition from symtab. LTO can
  // make vtable definitions visible across modules.
  // Step 3. Compute the byte offset of the virtual call, by adding vtable
  // address point offset and function's offset relative to vtable address
  // point. For each function candidate, this step tells us the vtable from
  // which it comes from, and the vtable address point to compare %vptr with.

  // Only virtual calls have virtual call site info.
  auto Iter = VirtualCSInfo.find(CB);
  if (Iter == VirtualCSInfo.end())
    return nullptr;

  LLVM_DEBUG(dbgs() << "\nComputing vtable infos for callsite #"
                    << NumOfPGOICallsites << "\n");

  const auto &VirtualCallInfo = Iter->second;
  Instruction *VPtr = VirtualCallInfo.VPtr;

  SmallDenseMap<Function *, int, 4> CalleeIndexMap;
  for (size_t I = 0; I < Candidates.size(); I++)
    CalleeIndexMap[Candidates[I].TargetFunction] = I;

  uint64_t TotalVTableCount = 0;
  auto VTableValueDataArray =
      getValueProfDataFromInst(*VirtualCallInfo.VPtr, IPVK_VTableTarget,
                               MaxNumVTableAnnotations, TotalVTableCount);
  if (VTableValueDataArray.empty())
    return VPtr;

  // Compute the functions and counts from by each vtable.
  for (const auto &V : VTableValueDataArray) {
    uint64_t VTableVal = V.Value;
    GUIDCountsMap[VTableVal] = V.Count;
    GlobalVariable *VTableVar = Symtab->getGlobalVariable(VTableVal);
    if (!VTableVar) {
      LLVM_DEBUG(dbgs() << "  Cannot find vtable definition for " << VTableVal
                        << "; maybe the vtable isn't imported\n");
      continue;
    }

    std::optional<uint64_t> MaybeAddressPointOffset =
        getAddressPointOffset(*VTableVar, VirtualCallInfo.CompatibleTypeStr);
    if (!MaybeAddressPointOffset)
      continue;

    const uint64_t AddressPointOffset = *MaybeAddressPointOffset;

    Function *Callee = nullptr;
    std::tie(Callee, std::ignore) = getFunctionAtVTableOffset(
        VTableVar, AddressPointOffset + VirtualCallInfo.FunctionOffset, M);
    if (!Callee)
      continue;
    auto CalleeIndexIter = CalleeIndexMap.find(Callee);
    if (CalleeIndexIter == CalleeIndexMap.end())
      continue;

    auto &Candidate = Candidates[CalleeIndexIter->second];
    // There shouldn't be duplicate GUIDs in one !prof metadata (except
    // duplicated zeros), so assign counters directly won't cause overwrite or
    // counter loss.
    Candidate.VTableGUIDAndCounts[VTableVal] = V.Count;
    Candidate.AddressPoints.push_back(
        getOrCreateVTableAddressPointVar(VTableVar, AddressPointOffset));
  }

  return VPtr;
}

// Creates 'branch_weights' prof metadata using TrueWeight and FalseWeight.
// Scales uint64_t counters down to uint32_t if necessary to prevent overflow.
static MDNode *createBranchWeights(LLVMContext &Context, uint64_t TrueWeight,
                                   uint64_t FalseWeight) {
  MDBuilder MDB(Context);
  uint64_t Scale = calculateCountScale(std::max(TrueWeight, FalseWeight));
  return MDB.createBranchWeights(scaleBranchCount(TrueWeight, Scale),
                                 scaleBranchCount(FalseWeight, Scale));
}

CallBase &llvm::pgo::promoteIndirectCall(CallBase &CB, Function *DirectCallee,
                                         uint64_t Count, uint64_t TotalCount,
                                         bool AttachProfToDirectCall,
                                         OptimizationRemarkEmitter *ORE) {
  CallBase &NewInst = promoteCallWithIfThenElse(
      CB, DirectCallee,
      createBranchWeights(CB.getContext(), Count, TotalCount - Count));

  if (AttachProfToDirectCall)
    setBranchWeights(NewInst, {static_cast<uint32_t>(Count)},
                     /*IsExpected=*/false);

  using namespace ore;

  if (ORE)
    ORE->emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "Promoted", &CB)
             << "Promote indirect call to " << NV("DirectCallee", DirectCallee)
             << " with count " << NV("Count", Count) << " out of "
             << NV("TotalCount", TotalCount);
    });
  return NewInst;
}

// Promote indirect-call to conditional direct-call for one callsite.
bool IndirectCallPromoter::tryToPromoteWithFuncCmp(
    CallBase &CB, Instruction *VPtr, ArrayRef<PromotionCandidate> Candidates,
    uint64_t TotalCount, ArrayRef<InstrProfValueData> ICallProfDataRef,
    uint32_t NumCandidates, VTableGUIDCountsMap &VTableGUIDCounts) {
  uint32_t NumPromoted = 0;

  for (const auto &C : Candidates) {
    uint64_t FuncCount = C.Count;
    pgo::promoteIndirectCall(CB, C.TargetFunction, FuncCount, TotalCount,
                             SamplePGO, &ORE);
    assert(TotalCount >= FuncCount);
    TotalCount -= FuncCount;
    NumOfPGOICallPromotion++;
    NumPromoted++;

    if (!EnableVTableProfileUse || C.VTableGUIDAndCounts.empty())
      continue;

    // After a virtual call candidate gets promoted, update the vtable's counts
    // proportionally. Each vtable-guid in `C.VTableGUIDAndCounts` represents
    // a vtable from which the virtual call is loaded. Compute the sum and use
    // 128-bit APInt to improve accuracy.
    uint64_t SumVTableCount = 0;
    for (const auto &[GUID, VTableCount] : C.VTableGUIDAndCounts)
      SumVTableCount += VTableCount;

    for (const auto &[GUID, VTableCount] : C.VTableGUIDAndCounts) {
      APInt APFuncCount((unsigned)128, FuncCount, false /*signed*/);
      APFuncCount *= VTableCount;
      VTableGUIDCounts[GUID] -= APFuncCount.udiv(SumVTableCount).getZExtValue();
    }
  }
  if (NumPromoted == 0)
    return false;

  assert(NumPromoted <= ICallProfDataRef.size() &&
         "Number of promoted functions should not be greater than the number "
         "of values in profile metadata");

  // Update value profiles on the indirect call.
  updateFuncValueProfiles(CB, ICallProfDataRef.slice(NumPromoted), TotalCount,
                          NumCandidates);
  updateVPtrValueProfiles(VPtr, VTableGUIDCounts);
  return true;
}

void IndirectCallPromoter::updateFuncValueProfiles(
    CallBase &CB, ArrayRef<InstrProfValueData> CallVDs, uint64_t TotalCount,
    uint32_t MaxMDCount) {
  // First clear the existing !prof.
  CB.setMetadata(LLVMContext::MD_prof, nullptr);
  // Annotate the remaining value profiles if counter is not zero.
  if (TotalCount != 0)
    annotateValueSite(M, CB, CallVDs, TotalCount, IPVK_IndirectCallTarget,
                      MaxMDCount);
}

void IndirectCallPromoter::updateVPtrValueProfiles(
    Instruction *VPtr, VTableGUIDCountsMap &VTableGUIDCounts) {
  if (!EnableVTableProfileUse || VPtr == nullptr ||
      !VPtr->getMetadata(LLVMContext::MD_prof))
    return;
  VPtr->setMetadata(LLVMContext::MD_prof, nullptr);
  std::vector<InstrProfValueData> VTableValueProfiles;
  uint64_t TotalVTableCount = 0;
  for (auto [GUID, Count] : VTableGUIDCounts) {
    if (Count == 0)
      continue;

    VTableValueProfiles.push_back({GUID, Count});
    TotalVTableCount += Count;
  }
  llvm::sort(VTableValueProfiles,
             [](const InstrProfValueData &LHS, const InstrProfValueData &RHS) {
               return LHS.Count > RHS.Count;
             });

  annotateValueSite(M, *VPtr, VTableValueProfiles, TotalVTableCount,
                    IPVK_VTableTarget, VTableValueProfiles.size());
}

bool IndirectCallPromoter::tryToPromoteWithVTableCmp(
    CallBase &CB, Instruction *VPtr, ArrayRef<PromotionCandidate> Candidates,
    uint64_t TotalFuncCount, uint32_t NumCandidates,
    MutableArrayRef<InstrProfValueData> ICallProfDataRef,
    VTableGUIDCountsMap &VTableGUIDCounts) {
  SmallVector<uint64_t, 4> PromotedFuncCount;

  for (const auto &Candidate : Candidates) {
    for (auto &[GUID, Count] : Candidate.VTableGUIDAndCounts)
      VTableGUIDCounts[GUID] -= Count;

    // 'OriginalBB' is the basic block of indirect call. After each candidate
    // is promoted, a new basic block is created for the indirect fallback basic
    // block and indirect call `CB` is moved into this new BB.
    BasicBlock *OriginalBB = CB.getParent();
    promoteCallWithVTableCmp(
        CB, VPtr, Candidate.TargetFunction, Candidate.AddressPoints,
        createBranchWeights(CB.getContext(), Candidate.Count,
                            TotalFuncCount - Candidate.Count));

    int SinkCount = tryToSinkInstructions(OriginalBB, CB.getParent());

    ORE.emit([&]() {
      OptimizationRemark Remark(DEBUG_TYPE, "Promoted", &CB);

      const auto &VTableGUIDAndCounts = Candidate.VTableGUIDAndCounts;
      Remark << "Promote indirect call to "
             << ore::NV("DirectCallee", Candidate.TargetFunction)
             << " with count " << ore::NV("Count", Candidate.Count)
             << " out of " << ore::NV("TotalCount", TotalFuncCount) << ", sink "
             << ore::NV("SinkCount", SinkCount)
             << " instruction(s) and compare "
             << ore::NV("VTable", VTableGUIDAndCounts.size())
             << " vtable(s): {";

      // Sort GUIDs so remark message is deterministic.
      std::set<uint64_t> GUIDSet;
      for (auto [GUID, Count] : VTableGUIDAndCounts)
        GUIDSet.insert(GUID);
      for (auto Iter = GUIDSet.begin(); Iter != GUIDSet.end(); Iter++) {
        if (Iter != GUIDSet.begin())
          Remark << ", ";
        Remark << ore::NV("VTable", Symtab->getGlobalVariable(*Iter));
      }

      Remark << "}";

      return Remark;
    });

    PromotedFuncCount.push_back(Candidate.Count);

    assert(TotalFuncCount >= Candidate.Count &&
           "Within one prof metadata, total count is the sum of counts from "
           "individual <target, count> pairs");
    // Use std::min since 'TotalFuncCount' is the saturated sum of individual
    // counts, see
    // https://github.com/llvm/llvm-project/blob/abedb3b8356d5d56f1c575c4f7682fba2cb19787/llvm/lib/ProfileData/InstrProf.cpp#L1281-L1288
    TotalFuncCount -= std::min(TotalFuncCount, Candidate.Count);
    NumOfPGOICallPromotion++;
  }

  if (PromotedFuncCount.empty())
    return false;

  // Update value profiles for 'CB' and 'VPtr', assuming that each 'CB' has a
  // a distinct 'VPtr'.
  // FIXME: When Clang `-fstrict-vtable-pointers` is enabled, a vtable might be
  // used to load multiple virtual functions. The vtable profiles needs to be
  // updated properly in that case (e.g, for each indirect call annotate both
  // type profiles and function profiles in one !prof).
  for (size_t I = 0; I < PromotedFuncCount.size(); I++)
    ICallProfDataRef[I].Count -=
        std::max(PromotedFuncCount[I], ICallProfDataRef[I].Count);
  // Sort value profiles by count in descending order.
  llvm::stable_sort(ICallProfDataRef, [](const InstrProfValueData &LHS,
                                         const InstrProfValueData &RHS) {
    return LHS.Count > RHS.Count;
  });
  // Drop the <target-value, count> pair if count is zero.
  ArrayRef<InstrProfValueData> VDs(
      ICallProfDataRef.begin(),
      llvm::upper_bound(ICallProfDataRef, 0U,
                        [](uint64_t Count, const InstrProfValueData &ProfData) {
                          return ProfData.Count <= Count;
                        }));
  updateFuncValueProfiles(CB, VDs, TotalFuncCount, NumCandidates);
  updateVPtrValueProfiles(VPtr, VTableGUIDCounts);
  return true;
}

// Traverse all the indirect-call callsite and get the value profile
// annotation to perform indirect-call promotion.
bool IndirectCallPromoter::processFunction(ProfileSummaryInfo *PSI) {
  bool Changed = false;
  ICallPromotionAnalysis ICallAnalysis;
  for (auto *CB : findIndirectCalls(F)) {
    uint32_t NumCandidates;
    uint64_t TotalCount;
    auto ICallProfDataRef = ICallAnalysis.getPromotionCandidatesForInstruction(
        CB, TotalCount, NumCandidates);
    if (!NumCandidates ||
        (PSI && PSI->hasProfileSummary() && !PSI->isHotCount(TotalCount)))
      continue;

    auto PromotionCandidates = getPromotionCandidatesForCallSite(
        *CB, ICallProfDataRef, TotalCount, NumCandidates);

    VTableGUIDCountsMap VTableGUIDCounts;
    Instruction *VPtr =
        computeVTableInfos(CB, VTableGUIDCounts, PromotionCandidates);

    if (isProfitableToCompareVTables(*CB, PromotionCandidates, TotalCount))
      Changed |= tryToPromoteWithVTableCmp(*CB, VPtr, PromotionCandidates,
                                           TotalCount, NumCandidates,
                                           ICallProfDataRef, VTableGUIDCounts);
    else
      Changed |= tryToPromoteWithFuncCmp(*CB, VPtr, PromotionCandidates,
                                         TotalCount, ICallProfDataRef,
                                         NumCandidates, VTableGUIDCounts);
  }
  return Changed;
}

// TODO: Return false if the function addressing and vtable load instructions
// cannot sink to indirect fallback.
bool IndirectCallPromoter::isProfitableToCompareVTables(
    const CallBase &CB, ArrayRef<PromotionCandidate> Candidates,
    uint64_t TotalCount) {
  if (!EnableVTableProfileUse || Candidates.empty())
    return false;
  LLVM_DEBUG(dbgs() << "\nEvaluating vtable profitability for callsite #"
                    << NumOfPGOICallsites << CB << "\n");
  uint64_t RemainingVTableCount = TotalCount;
  const size_t CandidateSize = Candidates.size();
  for (size_t I = 0; I < CandidateSize; I++) {
    auto &Candidate = Candidates[I];
    auto &VTableGUIDAndCounts = Candidate.VTableGUIDAndCounts;

    LLVM_DEBUG(dbgs() << "  Candidate " << I << " FunctionCount: "
                      << Candidate.Count << ", VTableCounts:");
    // Add [[maybe_unused]] since <GUID, Count> are only used by LLVM_DEBUG.
    for ([[maybe_unused]] auto &[GUID, Count] : VTableGUIDAndCounts)
      LLVM_DEBUG(dbgs() << " {" << Symtab->getGlobalVariable(GUID)->getName()
                        << ", " << Count << "}");
    LLVM_DEBUG(dbgs() << "\n");

    uint64_t CandidateVTableCount = 0;
    for (auto &[GUID, Count] : VTableGUIDAndCounts)
      CandidateVTableCount += Count;

    if (CandidateVTableCount < Candidate.Count * ICPVTablePercentageThreshold) {
      LLVM_DEBUG(
          dbgs() << "    function count " << Candidate.Count
                 << " and its vtable sum count " << CandidateVTableCount
                 << " have discrepancies. Bail out vtable comparison.\n");
      return false;
    }

    RemainingVTableCount -= Candidate.Count;

    // 'MaxNumVTable' limits the number of vtables to make vtable comparison
    // profitable. Comparing multiple vtables for one function candidate will
    // insert additional instructions on the hot path, and allowing more than
    // one vtable for non last candidates may or may not elongate the dependency
    // chain for the subsequent candidates. Set its value to 1 for non-last
    // candidate and allow option to override it for the last candidate.
    int MaxNumVTable = 1;
    if (I == CandidateSize - 1)
      MaxNumVTable = ICPMaxNumVTableLastCandidate;

    if ((int)Candidate.AddressPoints.size() > MaxNumVTable) {
      LLVM_DEBUG(dbgs() << "    allow at most " << MaxNumVTable << " and got "
                        << Candidate.AddressPoints.size()
                        << " vtables. Bail out for vtable comparison.\n");
      return false;
    }
  }

  // If the indirect fallback is not cold, don't compare vtables.
  if (PSI && PSI->hasProfileSummary() &&
      !PSI->isColdCount(RemainingVTableCount)) {
    LLVM_DEBUG(dbgs() << "    Indirect fallback basic block is not cold. Bail "
                         "out for vtable comparison.\n");
    return false;
  }

  return true;
}

// For virtual calls in the module, collect per-callsite information which will
// be used to associate an ICP candidate with a vtable and a specific function
// in the vtable. With type intrinsics (llvm.type.test), we can find virtual
// calls in a compile-time efficient manner (by iterating its users) and more
// importantly use the compatible type later to figure out the function byte
// offset relative to the start of vtables.
static void
computeVirtualCallSiteTypeInfoMap(Module &M, ModuleAnalysisManager &MAM,
                                  VirtualCallSiteTypeInfoMap &VirtualCSInfo) {
  // Right now only llvm.type.test is used to find out virtual call sites.
  // With ThinLTO and whole-program-devirtualization, llvm.type.test and
  // llvm.public.type.test are emitted, and llvm.public.type.test is either
  // refined to llvm.type.test or dropped before indirect-call-promotion pass.
  //
  // FIXME: For fullLTO with VFE, `llvm.type.checked.load intrinsic` is emitted.
  // Find out virtual calls by looking at users of llvm.type.checked.load in
  // that case.
  Function *TypeTestFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_test));
  if (!TypeTestFunc || TypeTestFunc->use_empty())
    return;

  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto LookupDomTree = [&FAM](Function &F) -> DominatorTree & {
    return FAM.getResult<DominatorTreeAnalysis>(F);
  };
  // Iterate all type.test calls to find all indirect calls.
  for (Use &U : llvm::make_early_inc_range(TypeTestFunc->uses())) {
    auto *CI = dyn_cast<CallInst>(U.getUser());
    if (!CI)
      continue;
    auto *TypeMDVal = cast<MetadataAsValue>(CI->getArgOperand(1));
    if (!TypeMDVal)
      continue;
    auto *CompatibleTypeId = dyn_cast<MDString>(TypeMDVal->getMetadata());
    if (!CompatibleTypeId)
      continue;

    // Find out all devirtualizable call sites given a llvm.type.test
    // intrinsic call.
    SmallVector<DevirtCallSite, 1> DevirtCalls;
    SmallVector<CallInst *, 1> Assumes;
    auto &DT = LookupDomTree(*CI->getFunction());
    findDevirtualizableCallsForTypeTest(DevirtCalls, Assumes, CI, DT);

    for (auto &DevirtCall : DevirtCalls) {
      CallBase &CB = DevirtCall.CB;
      // Given an indirect call, try find the instruction which loads a
      // pointer to virtual table.
      Instruction *VTablePtr =
          PGOIndirectCallVisitor::tryGetVTableInstruction(&CB);
      if (!VTablePtr)
        continue;
      VirtualCSInfo[&CB] = {DevirtCall.Offset, VTablePtr,
                            CompatibleTypeId->getString()};
    }
  }
}

// A wrapper function that does the actual work.
static bool promoteIndirectCalls(Module &M, ProfileSummaryInfo *PSI, bool InLTO,
                                 bool SamplePGO, ModuleAnalysisManager &MAM) {
  if (DisableICP)
    return false;
  InstrProfSymtab Symtab;
  if (Error E = Symtab.create(M, InLTO)) {
    std::string SymtabFailure = toString(std::move(E));
    M.getContext().emitError("Failed to create symtab: " + SymtabFailure);
    return false;
  }
  bool Changed = false;
  VirtualCallSiteTypeInfoMap VirtualCSInfo;

  if (EnableVTableProfileUse)
    computeVirtualCallSiteTypeInfoMap(M, MAM, VirtualCSInfo);

  // VTableAddressPointOffsetVal stores the vtable address points. The vtable
  // address point of a given <vtable, address point offset> is static (doesn't
  // change after being computed once).
  // IndirectCallPromoter::getOrCreateVTableAddressPointVar creates the map
  // entry the first time a <vtable, offset> pair is seen, as
  // promoteIndirectCalls processes an IR module and calls IndirectCallPromoter
  // repeatedly on each function.
  VTableAddressPointOffsetValMap VTableAddressPointOffsetVal;

  for (auto &F : M) {
    if (F.isDeclaration() || F.hasOptNone())
      continue;

    auto &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);

    IndirectCallPromoter CallPromoter(F, M, PSI, &Symtab, SamplePGO,
                                      VirtualCSInfo,
                                      VTableAddressPointOffsetVal, ORE);
    bool FuncChanged = CallPromoter.processFunction(PSI);
    if (ICPDUMPAFTER && FuncChanged) {
      LLVM_DEBUG(dbgs() << "\n== IR Dump After =="; F.print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
    }
    Changed |= FuncChanged;
    if (ICPCutOff != 0 && NumOfPGOICallPromotion >= ICPCutOff) {
      LLVM_DEBUG(dbgs() << " Stop: Cutoff reached.\n");
      break;
    }
  }
  return Changed;
}

PreservedAnalyses PGOIndirectCallPromotion::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  ProfileSummaryInfo *PSI = &MAM.getResult<ProfileSummaryAnalysis>(M);

  if (!promoteIndirectCalls(M, PSI, InLTO | ICPLTOMode,
                            SamplePGO | ICPSamplePGOMode, MAM))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}
