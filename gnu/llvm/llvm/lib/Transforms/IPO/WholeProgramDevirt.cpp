//===- WholeProgramDevirt.cpp - Whole program virtual call optimization ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements whole program optimization of virtual calls in cases
// where we know (via !type metadata) that the list of callees is fixed. This
// includes the following:
// - Single implementation devirtualization: if a virtual call has a single
//   possible callee, replace all calls with a direct call to that callee.
// - Virtual constant propagation: if the virtual function's return type is an
//   integer <=64 bits and all possible callees are readnone, for each class and
//   each list of constant arguments: evaluate the function, store the return
//   value alongside the virtual table, and rewrite each virtual call as a load
//   from the virtual table.
// - Uniform return value optimization: if the conditions for virtual constant
//   propagation hold and each function returns the same constant value, replace
//   each virtual call with that constant.
// - Unique return value optimization for i1 return values: if the conditions
//   for virtual constant propagation hold and a single vtable's function
//   returns 0, or a single vtable's function returns 1, replace each virtual
//   call with a comparison of the vptr against that vtable's address.
//
// This pass is intended to be used during the regular and thin LTO pipelines:
//
// During regular LTO, the pass determines the best optimization for each
// virtual call and applies the resolutions directly to virtual calls that are
// eligible for virtual call optimization (i.e. calls that use either of the
// llvm.assume(llvm.type.test) or llvm.type.checked.load intrinsics).
//
// During hybrid Regular/ThinLTO, the pass operates in two phases:
// - Export phase: this is run during the thin link over a single merged module
//   that contains all vtables with !type metadata that participate in the link.
//   The pass computes a resolution for each virtual call and stores it in the
//   type identifier summary.
// - Import phase: this is run during the thin backends over the individual
//   modules. The pass applies the resolutions previously computed during the
//   import phase to each eligible virtual call.
//
// During ThinLTO, the pass operates in two phases:
// - Export phase: this is run during the thin link over the index which
//   contains a summary of all vtables with !type metadata that participate in
//   the link. It computes a resolution for each virtual call and stores it in
//   the type identifier summary. Only single implementation devirtualization
//   is supported.
// - Import phase: (same as with hybrid case above).
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndexYAML.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CallPromotionUtils.h"
#include "llvm/Transforms/Utils/Evaluator.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>

using namespace llvm;
using namespace wholeprogramdevirt;

#define DEBUG_TYPE "wholeprogramdevirt"

STATISTIC(NumDevirtTargets, "Number of whole program devirtualization targets");
STATISTIC(NumSingleImpl, "Number of single implementation devirtualizations");
STATISTIC(NumBranchFunnel, "Number of branch funnels");
STATISTIC(NumUniformRetVal, "Number of uniform return value optimizations");
STATISTIC(NumUniqueRetVal, "Number of unique return value optimizations");
STATISTIC(NumVirtConstProp1Bit,
          "Number of 1 bit virtual constant propagations");
STATISTIC(NumVirtConstProp, "Number of virtual constant propagations");

static cl::opt<PassSummaryAction> ClSummaryAction(
    "wholeprogramdevirt-summary-action",
    cl::desc("What to do with the summary when running this pass"),
    cl::values(clEnumValN(PassSummaryAction::None, "none", "Do nothing"),
               clEnumValN(PassSummaryAction::Import, "import",
                          "Import typeid resolutions from summary and globals"),
               clEnumValN(PassSummaryAction::Export, "export",
                          "Export typeid resolutions to summary and globals")),
    cl::Hidden);

static cl::opt<std::string> ClReadSummary(
    "wholeprogramdevirt-read-summary",
    cl::desc(
        "Read summary from given bitcode or YAML file before running pass"),
    cl::Hidden);

static cl::opt<std::string> ClWriteSummary(
    "wholeprogramdevirt-write-summary",
    cl::desc("Write summary to given bitcode or YAML file after running pass. "
             "Output file format is deduced from extension: *.bc means writing "
             "bitcode, otherwise YAML"),
    cl::Hidden);

static cl::opt<unsigned>
    ClThreshold("wholeprogramdevirt-branch-funnel-threshold", cl::Hidden,
                cl::init(10),
                cl::desc("Maximum number of call targets per "
                         "call site to enable branch funnels"));

static cl::opt<bool>
    PrintSummaryDevirt("wholeprogramdevirt-print-index-based", cl::Hidden,
                       cl::desc("Print index-based devirtualization messages"));

/// Provide a way to force enable whole program visibility in tests.
/// This is needed to support legacy tests that don't contain
/// !vcall_visibility metadata (the mere presense of type tests
/// previously implied hidden visibility).
static cl::opt<bool>
    WholeProgramVisibility("whole-program-visibility", cl::Hidden,
                           cl::desc("Enable whole program visibility"));

/// Provide a way to force disable whole program for debugging or workarounds,
/// when enabled via the linker.
static cl::opt<bool> DisableWholeProgramVisibility(
    "disable-whole-program-visibility", cl::Hidden,
    cl::desc("Disable whole program visibility (overrides enabling options)"));

/// Provide way to prevent certain function from being devirtualized
static cl::list<std::string>
    SkipFunctionNames("wholeprogramdevirt-skip",
                      cl::desc("Prevent function(s) from being devirtualized"),
                      cl::Hidden, cl::CommaSeparated);

/// Mechanism to add runtime checking of devirtualization decisions, optionally
/// trapping or falling back to indirect call on any that are not correct.
/// Trapping mode is useful for debugging undefined behavior leading to failures
/// with WPD. Fallback mode is useful for ensuring safety when whole program
/// visibility may be compromised.
enum WPDCheckMode { None, Trap, Fallback };
static cl::opt<WPDCheckMode> DevirtCheckMode(
    "wholeprogramdevirt-check", cl::Hidden,
    cl::desc("Type of checking for incorrect devirtualizations"),
    cl::values(clEnumValN(WPDCheckMode::None, "none", "No checking"),
               clEnumValN(WPDCheckMode::Trap, "trap", "Trap when incorrect"),
               clEnumValN(WPDCheckMode::Fallback, "fallback",
                          "Fallback to indirect when incorrect")));

namespace {
struct PatternList {
  std::vector<GlobPattern> Patterns;
  template <class T> void init(const T &StringList) {
    for (const auto &S : StringList)
      if (Expected<GlobPattern> Pat = GlobPattern::create(S))
        Patterns.push_back(std::move(*Pat));
  }
  bool match(StringRef S) {
    for (const GlobPattern &P : Patterns)
      if (P.match(S))
        return true;
    return false;
  }
};
} // namespace

// Find the minimum offset that we may store a value of size Size bits at. If
// IsAfter is set, look for an offset before the object, otherwise look for an
// offset after the object.
uint64_t
wholeprogramdevirt::findLowestOffset(ArrayRef<VirtualCallTarget> Targets,
                                     bool IsAfter, uint64_t Size) {
  // Find a minimum offset taking into account only vtable sizes.
  uint64_t MinByte = 0;
  for (const VirtualCallTarget &Target : Targets) {
    if (IsAfter)
      MinByte = std::max(MinByte, Target.minAfterBytes());
    else
      MinByte = std::max(MinByte, Target.minBeforeBytes());
  }

  // Build a vector of arrays of bytes covering, for each target, a slice of the
  // used region (see AccumBitVector::BytesUsed in
  // llvm/Transforms/IPO/WholeProgramDevirt.h) starting at MinByte. Effectively,
  // this aligns the used regions to start at MinByte.
  //
  // In this example, A, B and C are vtables, # is a byte already allocated for
  // a virtual function pointer, AAAA... (etc.) are the used regions for the
  // vtables and Offset(X) is the value computed for the Offset variable below
  // for X.
  //
  //                    Offset(A)
  //                    |       |
  //                            |MinByte
  // A: ################AAAAAAAA|AAAAAAAA
  // B: ########BBBBBBBBBBBBBBBB|BBBB
  // C: ########################|CCCCCCCCCCCCCCCC
  //            |   Offset(B)   |
  //
  // This code produces the slices of A, B and C that appear after the divider
  // at MinByte.
  std::vector<ArrayRef<uint8_t>> Used;
  for (const VirtualCallTarget &Target : Targets) {
    ArrayRef<uint8_t> VTUsed = IsAfter ? Target.TM->Bits->After.BytesUsed
                                       : Target.TM->Bits->Before.BytesUsed;
    uint64_t Offset = IsAfter ? MinByte - Target.minAfterBytes()
                              : MinByte - Target.minBeforeBytes();

    // Disregard used regions that are smaller than Offset. These are
    // effectively all-free regions that do not need to be checked.
    if (VTUsed.size() > Offset)
      Used.push_back(VTUsed.slice(Offset));
  }

  if (Size == 1) {
    // Find a free bit in each member of Used.
    for (unsigned I = 0;; ++I) {
      uint8_t BitsUsed = 0;
      for (auto &&B : Used)
        if (I < B.size())
          BitsUsed |= B[I];
      if (BitsUsed != 0xff)
        return (MinByte + I) * 8 + llvm::countr_zero(uint8_t(~BitsUsed));
    }
  } else {
    // Find a free (Size/8) byte region in each member of Used.
    // FIXME: see if alignment helps.
    for (unsigned I = 0;; ++I) {
      for (auto &&B : Used) {
        unsigned Byte = 0;
        while ((I + Byte) < B.size() && Byte < (Size / 8)) {
          if (B[I + Byte])
            goto NextI;
          ++Byte;
        }
      }
      return (MinByte + I) * 8;
    NextI:;
    }
  }
}

void wholeprogramdevirt::setBeforeReturnValues(
    MutableArrayRef<VirtualCallTarget> Targets, uint64_t AllocBefore,
    unsigned BitWidth, int64_t &OffsetByte, uint64_t &OffsetBit) {
  if (BitWidth == 1)
    OffsetByte = -(AllocBefore / 8 + 1);
  else
    OffsetByte = -((AllocBefore + 7) / 8 + (BitWidth + 7) / 8);
  OffsetBit = AllocBefore % 8;

  for (VirtualCallTarget &Target : Targets) {
    if (BitWidth == 1)
      Target.setBeforeBit(AllocBefore);
    else
      Target.setBeforeBytes(AllocBefore, (BitWidth + 7) / 8);
  }
}

void wholeprogramdevirt::setAfterReturnValues(
    MutableArrayRef<VirtualCallTarget> Targets, uint64_t AllocAfter,
    unsigned BitWidth, int64_t &OffsetByte, uint64_t &OffsetBit) {
  if (BitWidth == 1)
    OffsetByte = AllocAfter / 8;
  else
    OffsetByte = (AllocAfter + 7) / 8;
  OffsetBit = AllocAfter % 8;

  for (VirtualCallTarget &Target : Targets) {
    if (BitWidth == 1)
      Target.setAfterBit(AllocAfter);
    else
      Target.setAfterBytes(AllocAfter, (BitWidth + 7) / 8);
  }
}

VirtualCallTarget::VirtualCallTarget(GlobalValue *Fn, const TypeMemberInfo *TM)
    : Fn(Fn), TM(TM),
      IsBigEndian(Fn->getDataLayout().isBigEndian()),
      WasDevirt(false) {}

namespace {

// A slot in a set of virtual tables. The TypeID identifies the set of virtual
// tables, and the ByteOffset is the offset in bytes from the address point to
// the virtual function pointer.
struct VTableSlot {
  Metadata *TypeID;
  uint64_t ByteOffset;
};

} // end anonymous namespace

namespace llvm {

template <> struct DenseMapInfo<VTableSlot> {
  static VTableSlot getEmptyKey() {
    return {DenseMapInfo<Metadata *>::getEmptyKey(),
            DenseMapInfo<uint64_t>::getEmptyKey()};
  }
  static VTableSlot getTombstoneKey() {
    return {DenseMapInfo<Metadata *>::getTombstoneKey(),
            DenseMapInfo<uint64_t>::getTombstoneKey()};
  }
  static unsigned getHashValue(const VTableSlot &I) {
    return DenseMapInfo<Metadata *>::getHashValue(I.TypeID) ^
           DenseMapInfo<uint64_t>::getHashValue(I.ByteOffset);
  }
  static bool isEqual(const VTableSlot &LHS,
                      const VTableSlot &RHS) {
    return LHS.TypeID == RHS.TypeID && LHS.ByteOffset == RHS.ByteOffset;
  }
};

template <> struct DenseMapInfo<VTableSlotSummary> {
  static VTableSlotSummary getEmptyKey() {
    return {DenseMapInfo<StringRef>::getEmptyKey(),
            DenseMapInfo<uint64_t>::getEmptyKey()};
  }
  static VTableSlotSummary getTombstoneKey() {
    return {DenseMapInfo<StringRef>::getTombstoneKey(),
            DenseMapInfo<uint64_t>::getTombstoneKey()};
  }
  static unsigned getHashValue(const VTableSlotSummary &I) {
    return DenseMapInfo<StringRef>::getHashValue(I.TypeID) ^
           DenseMapInfo<uint64_t>::getHashValue(I.ByteOffset);
  }
  static bool isEqual(const VTableSlotSummary &LHS,
                      const VTableSlotSummary &RHS) {
    return LHS.TypeID == RHS.TypeID && LHS.ByteOffset == RHS.ByteOffset;
  }
};

} // end namespace llvm

// Returns true if the function must be unreachable based on ValueInfo.
//
// In particular, identifies a function as unreachable in the following
// conditions
//   1) All summaries are live.
//   2) All function summaries indicate it's unreachable
//   3) There is no non-function with the same GUID (which is rare)
static bool mustBeUnreachableFunction(ValueInfo TheFnVI) {
  if ((!TheFnVI) || TheFnVI.getSummaryList().empty()) {
    // Returns false if ValueInfo is absent, or the summary list is empty
    // (e.g., function declarations).
    return false;
  }

  for (const auto &Summary : TheFnVI.getSummaryList()) {
    // Conservatively returns false if any non-live functions are seen.
    // In general either all summaries should be live or all should be dead.
    if (!Summary->isLive())
      return false;
    if (auto *FS = dyn_cast<FunctionSummary>(Summary->getBaseObject())) {
      if (!FS->fflags().MustBeUnreachable)
        return false;
    }
    // Be conservative if a non-function has the same GUID (which is rare).
    else
      return false;
  }
  // All function summaries are live and all of them agree that the function is
  // unreachble.
  return true;
}

namespace {
// A virtual call site. VTable is the loaded virtual table pointer, and CS is
// the indirect virtual call.
struct VirtualCallSite {
  Value *VTable = nullptr;
  CallBase &CB;

  // If non-null, this field points to the associated unsafe use count stored in
  // the DevirtModule::NumUnsafeUsesForTypeTest map below. See the description
  // of that field for details.
  unsigned *NumUnsafeUses = nullptr;

  void
  emitRemark(const StringRef OptName, const StringRef TargetName,
             function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter) {
    Function *F = CB.getCaller();
    DebugLoc DLoc = CB.getDebugLoc();
    BasicBlock *Block = CB.getParent();

    using namespace ore;
    OREGetter(F).emit(OptimizationRemark(DEBUG_TYPE, OptName, DLoc, Block)
                      << NV("Optimization", OptName)
                      << ": devirtualized a call to "
                      << NV("FunctionName", TargetName));
  }

  void replaceAndErase(
      const StringRef OptName, const StringRef TargetName, bool RemarksEnabled,
      function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
      Value *New) {
    if (RemarksEnabled)
      emitRemark(OptName, TargetName, OREGetter);
    CB.replaceAllUsesWith(New);
    if (auto *II = dyn_cast<InvokeInst>(&CB)) {
      BranchInst::Create(II->getNormalDest(), CB.getIterator());
      II->getUnwindDest()->removePredecessor(II->getParent());
    }
    CB.eraseFromParent();
    // This use is no longer unsafe.
    if (NumUnsafeUses)
      --*NumUnsafeUses;
  }
};

// Call site information collected for a specific VTableSlot and possibly a list
// of constant integer arguments. The grouping by arguments is handled by the
// VTableSlotInfo class.
struct CallSiteInfo {
  /// The set of call sites for this slot. Used during regular LTO and the
  /// import phase of ThinLTO (as well as the export phase of ThinLTO for any
  /// call sites that appear in the merged module itself); in each of these
  /// cases we are directly operating on the call sites at the IR level.
  std::vector<VirtualCallSite> CallSites;

  /// Whether all call sites represented by this CallSiteInfo, including those
  /// in summaries, have been devirtualized. This starts off as true because a
  /// default constructed CallSiteInfo represents no call sites.
  bool AllCallSitesDevirted = true;

  // These fields are used during the export phase of ThinLTO and reflect
  // information collected from function summaries.

  /// Whether any function summary contains an llvm.assume(llvm.type.test) for
  /// this slot.
  bool SummaryHasTypeTestAssumeUsers = false;

  /// CFI-specific: a vector containing the list of function summaries that use
  /// the llvm.type.checked.load intrinsic and therefore will require
  /// resolutions for llvm.type.test in order to implement CFI checks if
  /// devirtualization was unsuccessful. If devirtualization was successful, the
  /// pass will clear this vector by calling markDevirt(). If at the end of the
  /// pass the vector is non-empty, we will need to add a use of llvm.type.test
  /// to each of the function summaries in the vector.
  std::vector<FunctionSummary *> SummaryTypeCheckedLoadUsers;
  std::vector<FunctionSummary *> SummaryTypeTestAssumeUsers;

  bool isExported() const {
    return SummaryHasTypeTestAssumeUsers ||
           !SummaryTypeCheckedLoadUsers.empty();
  }

  void addSummaryTypeCheckedLoadUser(FunctionSummary *FS) {
    SummaryTypeCheckedLoadUsers.push_back(FS);
    AllCallSitesDevirted = false;
  }

  void addSummaryTypeTestAssumeUser(FunctionSummary *FS) {
    SummaryTypeTestAssumeUsers.push_back(FS);
    SummaryHasTypeTestAssumeUsers = true;
    AllCallSitesDevirted = false;
  }

  void markDevirt() {
    AllCallSitesDevirted = true;

    // As explained in the comment for SummaryTypeCheckedLoadUsers.
    SummaryTypeCheckedLoadUsers.clear();
  }
};

// Call site information collected for a specific VTableSlot.
struct VTableSlotInfo {
  // The set of call sites which do not have all constant integer arguments
  // (excluding "this").
  CallSiteInfo CSInfo;

  // The set of call sites with all constant integer arguments (excluding
  // "this"), grouped by argument list.
  std::map<std::vector<uint64_t>, CallSiteInfo> ConstCSInfo;

  void addCallSite(Value *VTable, CallBase &CB, unsigned *NumUnsafeUses);

private:
  CallSiteInfo &findCallSiteInfo(CallBase &CB);
};

CallSiteInfo &VTableSlotInfo::findCallSiteInfo(CallBase &CB) {
  std::vector<uint64_t> Args;
  auto *CBType = dyn_cast<IntegerType>(CB.getType());
  if (!CBType || CBType->getBitWidth() > 64 || CB.arg_empty())
    return CSInfo;
  for (auto &&Arg : drop_begin(CB.args())) {
    auto *CI = dyn_cast<ConstantInt>(Arg);
    if (!CI || CI->getBitWidth() > 64)
      return CSInfo;
    Args.push_back(CI->getZExtValue());
  }
  return ConstCSInfo[Args];
}

void VTableSlotInfo::addCallSite(Value *VTable, CallBase &CB,
                                 unsigned *NumUnsafeUses) {
  auto &CSI = findCallSiteInfo(CB);
  CSI.AllCallSitesDevirted = false;
  CSI.CallSites.push_back({VTable, CB, NumUnsafeUses});
}

struct DevirtModule {
  Module &M;
  function_ref<AAResults &(Function &)> AARGetter;
  function_ref<DominatorTree &(Function &)> LookupDomTree;

  ModuleSummaryIndex *ExportSummary;
  const ModuleSummaryIndex *ImportSummary;

  IntegerType *Int8Ty;
  PointerType *Int8PtrTy;
  IntegerType *Int32Ty;
  IntegerType *Int64Ty;
  IntegerType *IntPtrTy;
  /// Sizeless array type, used for imported vtables. This provides a signal
  /// to analyzers that these imports may alias, as they do for example
  /// when multiple unique return values occur in the same vtable.
  ArrayType *Int8Arr0Ty;

  bool RemarksEnabled;
  function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter;

  MapVector<VTableSlot, VTableSlotInfo> CallSlots;

  // Calls that have already been optimized. We may add a call to multiple
  // VTableSlotInfos if vtable loads are coalesced and need to make sure not to
  // optimize a call more than once.
  SmallPtrSet<CallBase *, 8> OptimizedCalls;

  // Store calls that had their ptrauth bundle removed. They are to be deleted
  // at the end of the optimization.
  SmallVector<CallBase *, 8> CallsWithPtrAuthBundleRemoved;

  // This map keeps track of the number of "unsafe" uses of a loaded function
  // pointer. The key is the associated llvm.type.test intrinsic call generated
  // by this pass. An unsafe use is one that calls the loaded function pointer
  // directly. Every time we eliminate an unsafe use (for example, by
  // devirtualizing it or by applying virtual constant propagation), we
  // decrement the value stored in this map. If a value reaches zero, we can
  // eliminate the type check by RAUWing the associated llvm.type.test call with
  // true.
  std::map<CallInst *, unsigned> NumUnsafeUsesForTypeTest;
  PatternList FunctionsToSkip;

  DevirtModule(Module &M, function_ref<AAResults &(Function &)> AARGetter,
               function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
               function_ref<DominatorTree &(Function &)> LookupDomTree,
               ModuleSummaryIndex *ExportSummary,
               const ModuleSummaryIndex *ImportSummary)
      : M(M), AARGetter(AARGetter), LookupDomTree(LookupDomTree),
        ExportSummary(ExportSummary), ImportSummary(ImportSummary),
        Int8Ty(Type::getInt8Ty(M.getContext())),
        Int8PtrTy(PointerType::getUnqual(M.getContext())),
        Int32Ty(Type::getInt32Ty(M.getContext())),
        Int64Ty(Type::getInt64Ty(M.getContext())),
        IntPtrTy(M.getDataLayout().getIntPtrType(M.getContext(), 0)),
        Int8Arr0Ty(ArrayType::get(Type::getInt8Ty(M.getContext()), 0)),
        RemarksEnabled(areRemarksEnabled()), OREGetter(OREGetter) {
    assert(!(ExportSummary && ImportSummary));
    FunctionsToSkip.init(SkipFunctionNames);
  }

  bool areRemarksEnabled();

  void
  scanTypeTestUsers(Function *TypeTestFunc,
                    DenseMap<Metadata *, std::set<TypeMemberInfo>> &TypeIdMap);
  void scanTypeCheckedLoadUsers(Function *TypeCheckedLoadFunc);

  void buildTypeIdentifierMap(
      std::vector<VTableBits> &Bits,
      DenseMap<Metadata *, std::set<TypeMemberInfo>> &TypeIdMap);

  bool
  tryFindVirtualCallTargets(std::vector<VirtualCallTarget> &TargetsForSlot,
                            const std::set<TypeMemberInfo> &TypeMemberInfos,
                            uint64_t ByteOffset,
                            ModuleSummaryIndex *ExportSummary);

  void applySingleImplDevirt(VTableSlotInfo &SlotInfo, Constant *TheFn,
                             bool &IsExported);
  bool trySingleImplDevirt(ModuleSummaryIndex *ExportSummary,
                           MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                           VTableSlotInfo &SlotInfo,
                           WholeProgramDevirtResolution *Res);

  void applyICallBranchFunnel(VTableSlotInfo &SlotInfo, Constant *JT,
                              bool &IsExported);
  void tryICallBranchFunnel(MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                            VTableSlotInfo &SlotInfo,
                            WholeProgramDevirtResolution *Res, VTableSlot Slot);

  bool tryEvaluateFunctionsWithArgs(
      MutableArrayRef<VirtualCallTarget> TargetsForSlot,
      ArrayRef<uint64_t> Args);

  void applyUniformRetValOpt(CallSiteInfo &CSInfo, StringRef FnName,
                             uint64_t TheRetVal);
  bool tryUniformRetValOpt(MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                           CallSiteInfo &CSInfo,
                           WholeProgramDevirtResolution::ByArg *Res);

  // Returns the global symbol name that is used to export information about the
  // given vtable slot and list of arguments.
  std::string getGlobalName(VTableSlot Slot, ArrayRef<uint64_t> Args,
                            StringRef Name);

  bool shouldExportConstantsAsAbsoluteSymbols();

  // This function is called during the export phase to create a symbol
  // definition containing information about the given vtable slot and list of
  // arguments.
  void exportGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args, StringRef Name,
                    Constant *C);
  void exportConstant(VTableSlot Slot, ArrayRef<uint64_t> Args, StringRef Name,
                      uint32_t Const, uint32_t &Storage);

  // This function is called during the import phase to create a reference to
  // the symbol definition created during the export phase.
  Constant *importGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args,
                         StringRef Name);
  Constant *importConstant(VTableSlot Slot, ArrayRef<uint64_t> Args,
                           StringRef Name, IntegerType *IntTy,
                           uint32_t Storage);

  Constant *getMemberAddr(const TypeMemberInfo *M);

  void applyUniqueRetValOpt(CallSiteInfo &CSInfo, StringRef FnName, bool IsOne,
                            Constant *UniqueMemberAddr);
  bool tryUniqueRetValOpt(unsigned BitWidth,
                          MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                          CallSiteInfo &CSInfo,
                          WholeProgramDevirtResolution::ByArg *Res,
                          VTableSlot Slot, ArrayRef<uint64_t> Args);

  void applyVirtualConstProp(CallSiteInfo &CSInfo, StringRef FnName,
                             Constant *Byte, Constant *Bit);
  bool tryVirtualConstProp(MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                           VTableSlotInfo &SlotInfo,
                           WholeProgramDevirtResolution *Res, VTableSlot Slot);

  void rebuildGlobal(VTableBits &B);

  // Apply the summary resolution for Slot to all virtual calls in SlotInfo.
  void importResolution(VTableSlot Slot, VTableSlotInfo &SlotInfo);

  // If we were able to eliminate all unsafe uses for a type checked load,
  // eliminate the associated type tests by replacing them with true.
  void removeRedundantTypeTests();

  bool run();

  // Look up the corresponding ValueInfo entry of `TheFn` in `ExportSummary`.
  //
  // Caller guarantees that `ExportSummary` is not nullptr.
  static ValueInfo lookUpFunctionValueInfo(Function *TheFn,
                                           ModuleSummaryIndex *ExportSummary);

  // Returns true if the function definition must be unreachable.
  //
  // Note if this helper function returns true, `F` is guaranteed
  // to be unreachable; if it returns false, `F` might still
  // be unreachable but not covered by this helper function.
  //
  // Implementation-wise, if function definition is present, IR is analyzed; if
  // not, look up function flags from ExportSummary as a fallback.
  static bool mustBeUnreachableFunction(Function *const F,
                                        ModuleSummaryIndex *ExportSummary);

  // Lower the module using the action and summary passed as command line
  // arguments. For testing purposes only.
  static bool
  runForTesting(Module &M, function_ref<AAResults &(Function &)> AARGetter,
                function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
                function_ref<DominatorTree &(Function &)> LookupDomTree);
};

struct DevirtIndex {
  ModuleSummaryIndex &ExportSummary;
  // The set in which to record GUIDs exported from their module by
  // devirtualization, used by client to ensure they are not internalized.
  std::set<GlobalValue::GUID> &ExportedGUIDs;
  // A map in which to record the information necessary to locate the WPD
  // resolution for local targets in case they are exported by cross module
  // importing.
  std::map<ValueInfo, std::vector<VTableSlotSummary>> &LocalWPDTargetsMap;

  MapVector<VTableSlotSummary, VTableSlotInfo> CallSlots;

  PatternList FunctionsToSkip;

  DevirtIndex(
      ModuleSummaryIndex &ExportSummary,
      std::set<GlobalValue::GUID> &ExportedGUIDs,
      std::map<ValueInfo, std::vector<VTableSlotSummary>> &LocalWPDTargetsMap)
      : ExportSummary(ExportSummary), ExportedGUIDs(ExportedGUIDs),
        LocalWPDTargetsMap(LocalWPDTargetsMap) {
    FunctionsToSkip.init(SkipFunctionNames);
  }

  bool tryFindVirtualCallTargets(std::vector<ValueInfo> &TargetsForSlot,
                                 const TypeIdCompatibleVtableInfo TIdInfo,
                                 uint64_t ByteOffset);

  bool trySingleImplDevirt(MutableArrayRef<ValueInfo> TargetsForSlot,
                           VTableSlotSummary &SlotSummary,
                           VTableSlotInfo &SlotInfo,
                           WholeProgramDevirtResolution *Res,
                           std::set<ValueInfo> &DevirtTargets);

  void run();
};
} // end anonymous namespace

PreservedAnalyses WholeProgramDevirtPass::run(Module &M,
                                              ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto AARGetter = [&](Function &F) -> AAResults & {
    return FAM.getResult<AAManager>(F);
  };
  auto OREGetter = [&](Function *F) -> OptimizationRemarkEmitter & {
    return FAM.getResult<OptimizationRemarkEmitterAnalysis>(*F);
  };
  auto LookupDomTree = [&FAM](Function &F) -> DominatorTree & {
    return FAM.getResult<DominatorTreeAnalysis>(F);
  };
  if (UseCommandLine) {
    if (!DevirtModule::runForTesting(M, AARGetter, OREGetter, LookupDomTree))
      return PreservedAnalyses::all();
    return PreservedAnalyses::none();
  }
  if (!DevirtModule(M, AARGetter, OREGetter, LookupDomTree, ExportSummary,
                    ImportSummary)
           .run())
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}

// Enable whole program visibility if enabled by client (e.g. linker) or
// internal option, and not force disabled.
bool llvm::hasWholeProgramVisibility(bool WholeProgramVisibilityEnabledInLTO) {
  return (WholeProgramVisibilityEnabledInLTO || WholeProgramVisibility) &&
         !DisableWholeProgramVisibility;
}

static bool
typeIDVisibleToRegularObj(StringRef TypeID,
                          function_ref<bool(StringRef)> IsVisibleToRegularObj) {
  // TypeID for member function pointer type is an internal construct
  // and won't exist in IsVisibleToRegularObj. The full TypeID
  // will be present and participate in invalidation.
  if (TypeID.ends_with(".virtual"))
    return false;

  // TypeID that doesn't start with Itanium mangling (_ZTS) will be
  // non-externally visible types which cannot interact with
  // external native files. See CodeGenModule::CreateMetadataIdentifierImpl.
  if (!TypeID.consume_front("_ZTS"))
    return false;

  // TypeID is keyed off the type name symbol (_ZTS). However, the native
  // object may not contain this symbol if it does not contain a key
  // function for the base type and thus only contains a reference to the
  // type info (_ZTI). To catch this case we query using the type info
  // symbol corresponding to the TypeID.
  std::string typeInfo = ("_ZTI" + TypeID).str();
  return IsVisibleToRegularObj(typeInfo);
}

static bool
skipUpdateDueToValidation(GlobalVariable &GV,
                          function_ref<bool(StringRef)> IsVisibleToRegularObj) {
  SmallVector<MDNode *, 2> Types;
  GV.getMetadata(LLVMContext::MD_type, Types);

  for (auto Type : Types)
    if (auto *TypeID = dyn_cast<MDString>(Type->getOperand(1).get()))
      return typeIDVisibleToRegularObj(TypeID->getString(),
                                       IsVisibleToRegularObj);

  return false;
}

/// If whole program visibility asserted, then upgrade all public vcall
/// visibility metadata on vtable definitions to linkage unit visibility in
/// Module IR (for regular or hybrid LTO).
void llvm::updateVCallVisibilityInModule(
    Module &M, bool WholeProgramVisibilityEnabledInLTO,
    const DenseSet<GlobalValue::GUID> &DynamicExportSymbols,
    bool ValidateAllVtablesHaveTypeInfos,
    function_ref<bool(StringRef)> IsVisibleToRegularObj) {
  if (!hasWholeProgramVisibility(WholeProgramVisibilityEnabledInLTO))
    return;
  for (GlobalVariable &GV : M.globals()) {
    // Add linkage unit visibility to any variable with type metadata, which are
    // the vtable definitions. We won't have an existing vcall_visibility
    // metadata on vtable definitions with public visibility.
    if (GV.hasMetadata(LLVMContext::MD_type) &&
        GV.getVCallVisibility() == GlobalObject::VCallVisibilityPublic &&
        // Don't upgrade the visibility for symbols exported to the dynamic
        // linker, as we have no information on their eventual use.
        !DynamicExportSymbols.count(GV.getGUID()) &&
        // With validation enabled, we want to exclude symbols visible to
        // regular objects. Local symbols will be in this group due to the
        // current implementation but those with VCallVisibilityTranslationUnit
        // will have already been marked in clang so are unaffected.
        !(ValidateAllVtablesHaveTypeInfos &&
          skipUpdateDueToValidation(GV, IsVisibleToRegularObj)))
      GV.setVCallVisibilityMetadata(GlobalObject::VCallVisibilityLinkageUnit);
  }
}

void llvm::updatePublicTypeTestCalls(Module &M,
                                     bool WholeProgramVisibilityEnabledInLTO) {
  Function *PublicTypeTestFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::public_type_test));
  if (!PublicTypeTestFunc)
    return;
  if (hasWholeProgramVisibility(WholeProgramVisibilityEnabledInLTO)) {
    Function *TypeTestFunc =
        Intrinsic::getDeclaration(&M, Intrinsic::type_test);
    for (Use &U : make_early_inc_range(PublicTypeTestFunc->uses())) {
      auto *CI = cast<CallInst>(U.getUser());
      auto *NewCI = CallInst::Create(
          TypeTestFunc, {CI->getArgOperand(0), CI->getArgOperand(1)},
          std::nullopt, "", CI->getIterator());
      CI->replaceAllUsesWith(NewCI);
      CI->eraseFromParent();
    }
  } else {
    auto *True = ConstantInt::getTrue(M.getContext());
    for (Use &U : make_early_inc_range(PublicTypeTestFunc->uses())) {
      auto *CI = cast<CallInst>(U.getUser());
      CI->replaceAllUsesWith(True);
      CI->eraseFromParent();
    }
  }
}

/// Based on typeID string, get all associated vtable GUIDS that are
/// visible to regular objects.
void llvm::getVisibleToRegularObjVtableGUIDs(
    ModuleSummaryIndex &Index,
    DenseSet<GlobalValue::GUID> &VisibleToRegularObjSymbols,
    function_ref<bool(StringRef)> IsVisibleToRegularObj) {
  for (const auto &typeID : Index.typeIdCompatibleVtableMap()) {
    if (typeIDVisibleToRegularObj(typeID.first, IsVisibleToRegularObj))
      for (const TypeIdOffsetVtableInfo &P : typeID.second)
        VisibleToRegularObjSymbols.insert(P.VTableVI.getGUID());
  }
}

/// If whole program visibility asserted, then upgrade all public vcall
/// visibility metadata on vtable definition summaries to linkage unit
/// visibility in Module summary index (for ThinLTO).
void llvm::updateVCallVisibilityInIndex(
    ModuleSummaryIndex &Index, bool WholeProgramVisibilityEnabledInLTO,
    const DenseSet<GlobalValue::GUID> &DynamicExportSymbols,
    const DenseSet<GlobalValue::GUID> &VisibleToRegularObjSymbols) {
  if (!hasWholeProgramVisibility(WholeProgramVisibilityEnabledInLTO))
    return;
  for (auto &P : Index) {
    // Don't upgrade the visibility for symbols exported to the dynamic
    // linker, as we have no information on their eventual use.
    if (DynamicExportSymbols.count(P.first))
      continue;
    for (auto &S : P.second.SummaryList) {
      auto *GVar = dyn_cast<GlobalVarSummary>(S.get());
      if (!GVar ||
          GVar->getVCallVisibility() != GlobalObject::VCallVisibilityPublic)
        continue;
      // With validation enabled, we want to exclude symbols visible to regular
      // objects. Local symbols will be in this group due to the current
      // implementation but those with VCallVisibilityTranslationUnit will have
      // already been marked in clang so are unaffected.
      if (VisibleToRegularObjSymbols.count(P.first))
        continue;
      GVar->setVCallVisibility(GlobalObject::VCallVisibilityLinkageUnit);
    }
  }
}

void llvm::runWholeProgramDevirtOnIndex(
    ModuleSummaryIndex &Summary, std::set<GlobalValue::GUID> &ExportedGUIDs,
    std::map<ValueInfo, std::vector<VTableSlotSummary>> &LocalWPDTargetsMap) {
  DevirtIndex(Summary, ExportedGUIDs, LocalWPDTargetsMap).run();
}

void llvm::updateIndexWPDForExports(
    ModuleSummaryIndex &Summary,
    function_ref<bool(StringRef, ValueInfo)> isExported,
    std::map<ValueInfo, std::vector<VTableSlotSummary>> &LocalWPDTargetsMap) {
  for (auto &T : LocalWPDTargetsMap) {
    auto &VI = T.first;
    // This was enforced earlier during trySingleImplDevirt.
    assert(VI.getSummaryList().size() == 1 &&
           "Devirt of local target has more than one copy");
    auto &S = VI.getSummaryList()[0];
    if (!isExported(S->modulePath(), VI))
      continue;

    // It's been exported by a cross module import.
    for (auto &SlotSummary : T.second) {
      auto *TIdSum = Summary.getTypeIdSummary(SlotSummary.TypeID);
      assert(TIdSum);
      auto WPDRes = TIdSum->WPDRes.find(SlotSummary.ByteOffset);
      assert(WPDRes != TIdSum->WPDRes.end());
      WPDRes->second.SingleImplName = ModuleSummaryIndex::getGlobalNameForLocal(
          WPDRes->second.SingleImplName,
          Summary.getModuleHash(S->modulePath()));
    }
  }
}

static Error checkCombinedSummaryForTesting(ModuleSummaryIndex *Summary) {
  // Check that summary index contains regular LTO module when performing
  // export to prevent occasional use of index from pure ThinLTO compilation
  // (-fno-split-lto-module). This kind of summary index is passed to
  // DevirtIndex::run, not to DevirtModule::run used by opt/runForTesting.
  const auto &ModPaths = Summary->modulePaths();
  if (ClSummaryAction != PassSummaryAction::Import &&
      !ModPaths.contains(ModuleSummaryIndex::getRegularLTOModuleName()))
    return createStringError(
        errc::invalid_argument,
        "combined summary should contain Regular LTO module");
  return ErrorSuccess();
}

bool DevirtModule::runForTesting(
    Module &M, function_ref<AAResults &(Function &)> AARGetter,
    function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
    function_ref<DominatorTree &(Function &)> LookupDomTree) {
  std::unique_ptr<ModuleSummaryIndex> Summary =
      std::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/false);

  // Handle the command-line summary arguments. This code is for testing
  // purposes only, so we handle errors directly.
  if (!ClReadSummary.empty()) {
    ExitOnError ExitOnErr("-wholeprogramdevirt-read-summary: " + ClReadSummary +
                          ": ");
    auto ReadSummaryFile =
        ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(ClReadSummary)));
    if (Expected<std::unique_ptr<ModuleSummaryIndex>> SummaryOrErr =
            getModuleSummaryIndex(*ReadSummaryFile)) {
      Summary = std::move(*SummaryOrErr);
      ExitOnErr(checkCombinedSummaryForTesting(Summary.get()));
    } else {
      // Try YAML if we've failed with bitcode.
      consumeError(SummaryOrErr.takeError());
      yaml::Input In(ReadSummaryFile->getBuffer());
      In >> *Summary;
      ExitOnErr(errorCodeToError(In.error()));
    }
  }

  bool Changed =
      DevirtModule(M, AARGetter, OREGetter, LookupDomTree,
                   ClSummaryAction == PassSummaryAction::Export ? Summary.get()
                                                                : nullptr,
                   ClSummaryAction == PassSummaryAction::Import ? Summary.get()
                                                                : nullptr)
          .run();

  if (!ClWriteSummary.empty()) {
    ExitOnError ExitOnErr(
        "-wholeprogramdevirt-write-summary: " + ClWriteSummary + ": ");
    std::error_code EC;
    if (StringRef(ClWriteSummary).ends_with(".bc")) {
      raw_fd_ostream OS(ClWriteSummary, EC, sys::fs::OF_None);
      ExitOnErr(errorCodeToError(EC));
      writeIndexToFile(*Summary, OS);
    } else {
      raw_fd_ostream OS(ClWriteSummary, EC, sys::fs::OF_TextWithCRLF);
      ExitOnErr(errorCodeToError(EC));
      yaml::Output Out(OS);
      Out << *Summary;
    }
  }

  return Changed;
}

void DevirtModule::buildTypeIdentifierMap(
    std::vector<VTableBits> &Bits,
    DenseMap<Metadata *, std::set<TypeMemberInfo>> &TypeIdMap) {
  DenseMap<GlobalVariable *, VTableBits *> GVToBits;
  Bits.reserve(M.global_size());
  SmallVector<MDNode *, 2> Types;
  for (GlobalVariable &GV : M.globals()) {
    Types.clear();
    GV.getMetadata(LLVMContext::MD_type, Types);
    if (GV.isDeclaration() || Types.empty())
      continue;

    VTableBits *&BitsPtr = GVToBits[&GV];
    if (!BitsPtr) {
      Bits.emplace_back();
      Bits.back().GV = &GV;
      Bits.back().ObjectSize =
          M.getDataLayout().getTypeAllocSize(GV.getInitializer()->getType());
      BitsPtr = &Bits.back();
    }

    for (MDNode *Type : Types) {
      auto TypeID = Type->getOperand(1).get();

      uint64_t Offset =
          cast<ConstantInt>(
              cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
              ->getZExtValue();

      TypeIdMap[TypeID].insert({BitsPtr, Offset});
    }
  }
}

bool DevirtModule::tryFindVirtualCallTargets(
    std::vector<VirtualCallTarget> &TargetsForSlot,
    const std::set<TypeMemberInfo> &TypeMemberInfos, uint64_t ByteOffset,
    ModuleSummaryIndex *ExportSummary) {
  for (const TypeMemberInfo &TM : TypeMemberInfos) {
    if (!TM.Bits->GV->isConstant())
      return false;

    // We cannot perform whole program devirtualization analysis on a vtable
    // with public LTO visibility.
    if (TM.Bits->GV->getVCallVisibility() ==
        GlobalObject::VCallVisibilityPublic)
      return false;

    Function *Fn = nullptr;
    Constant *C = nullptr;
    std::tie(Fn, C) =
        getFunctionAtVTableOffset(TM.Bits->GV, TM.Offset + ByteOffset, M);

    if (!Fn)
      return false;

    if (FunctionsToSkip.match(Fn->getName()))
      return false;

    // We can disregard __cxa_pure_virtual as a possible call target, as
    // calls to pure virtuals are UB.
    if (Fn->getName() == "__cxa_pure_virtual")
      continue;

    // We can disregard unreachable functions as possible call targets, as
    // unreachable functions shouldn't be called.
    if (mustBeUnreachableFunction(Fn, ExportSummary))
      continue;

    // Save the symbol used in the vtable to use as the devirtualization
    // target.
    auto GV = dyn_cast<GlobalValue>(C);
    assert(GV);
    TargetsForSlot.push_back({GV, &TM});
  }

  // Give up if we couldn't find any targets.
  return !TargetsForSlot.empty();
}

bool DevirtIndex::tryFindVirtualCallTargets(
    std::vector<ValueInfo> &TargetsForSlot,
    const TypeIdCompatibleVtableInfo TIdInfo, uint64_t ByteOffset) {
  for (const TypeIdOffsetVtableInfo &P : TIdInfo) {
    // Find a representative copy of the vtable initializer.
    // We can have multiple available_externally, linkonce_odr and weak_odr
    // vtable initializers. We can also have multiple external vtable
    // initializers in the case of comdats, which we cannot check here.
    // The linker should give an error in this case.
    //
    // Also, handle the case of same-named local Vtables with the same path
    // and therefore the same GUID. This can happen if there isn't enough
    // distinguishing path when compiling the source file. In that case we
    // conservatively return false early.
    const GlobalVarSummary *VS = nullptr;
    bool LocalFound = false;
    for (const auto &S : P.VTableVI.getSummaryList()) {
      if (GlobalValue::isLocalLinkage(S->linkage())) {
        if (LocalFound)
          return false;
        LocalFound = true;
      }
      auto *CurVS = cast<GlobalVarSummary>(S->getBaseObject());
      if (!CurVS->vTableFuncs().empty() ||
          // Previously clang did not attach the necessary type metadata to
          // available_externally vtables, in which case there would not
          // be any vtable functions listed in the summary and we need
          // to treat this case conservatively (in case the bitcode is old).
          // However, we will also not have any vtable functions in the
          // case of a pure virtual base class. In that case we do want
          // to set VS to avoid treating it conservatively.
          !GlobalValue::isAvailableExternallyLinkage(S->linkage())) {
        VS = CurVS;
        // We cannot perform whole program devirtualization analysis on a vtable
        // with public LTO visibility.
        if (VS->getVCallVisibility() == GlobalObject::VCallVisibilityPublic)
          return false;
      }
    }
    // There will be no VS if all copies are available_externally having no
    // type metadata. In that case we can't safely perform WPD.
    if (!VS)
      return false;
    if (!VS->isLive())
      continue;
    for (auto VTP : VS->vTableFuncs()) {
      if (VTP.VTableOffset != P.AddressPointOffset + ByteOffset)
        continue;

      if (mustBeUnreachableFunction(VTP.FuncVI))
        continue;

      TargetsForSlot.push_back(VTP.FuncVI);
    }
  }

  // Give up if we couldn't find any targets.
  return !TargetsForSlot.empty();
}

void DevirtModule::applySingleImplDevirt(VTableSlotInfo &SlotInfo,
                                         Constant *TheFn, bool &IsExported) {
  // Don't devirtualize function if we're told to skip it
  // in -wholeprogramdevirt-skip.
  if (FunctionsToSkip.match(TheFn->stripPointerCasts()->getName()))
    return;
  auto Apply = [&](CallSiteInfo &CSInfo) {
    for (auto &&VCallSite : CSInfo.CallSites) {
      if (!OptimizedCalls.insert(&VCallSite.CB).second)
        continue;

      if (RemarksEnabled)
        VCallSite.emitRemark("single-impl",
                             TheFn->stripPointerCasts()->getName(), OREGetter);
      NumSingleImpl++;
      auto &CB = VCallSite.CB;
      assert(!CB.getCalledFunction() && "devirtualizing direct call?");
      IRBuilder<> Builder(&CB);
      Value *Callee =
          Builder.CreateBitCast(TheFn, CB.getCalledOperand()->getType());

      // If trap checking is enabled, add support to compare the virtual
      // function pointer to the devirtualized target. In case of a mismatch,
      // perform a debug trap.
      if (DevirtCheckMode == WPDCheckMode::Trap) {
        auto *Cond = Builder.CreateICmpNE(CB.getCalledOperand(), Callee);
        Instruction *ThenTerm =
            SplitBlockAndInsertIfThen(Cond, &CB, /*Unreachable=*/false);
        Builder.SetInsertPoint(ThenTerm);
        Function *TrapFn = Intrinsic::getDeclaration(&M, Intrinsic::debugtrap);
        auto *CallTrap = Builder.CreateCall(TrapFn);
        CallTrap->setDebugLoc(CB.getDebugLoc());
      }

      // If fallback checking is enabled, add support to compare the virtual
      // function pointer to the devirtualized target. In case of a mismatch,
      // fall back to indirect call.
      if (DevirtCheckMode == WPDCheckMode::Fallback) {
        MDNode *Weights = MDBuilder(M.getContext()).createLikelyBranchWeights();
        // Version the indirect call site. If the called value is equal to the
        // given callee, 'NewInst' will be executed, otherwise the original call
        // site will be executed.
        CallBase &NewInst = versionCallSite(CB, Callee, Weights);
        NewInst.setCalledOperand(Callee);
        // Since the new call site is direct, we must clear metadata that
        // is only appropriate for indirect calls. This includes !prof and
        // !callees metadata.
        NewInst.setMetadata(LLVMContext::MD_prof, nullptr);
        NewInst.setMetadata(LLVMContext::MD_callees, nullptr);
        // Additionally, we should remove them from the fallback indirect call,
        // so that we don't attempt to perform indirect call promotion later.
        CB.setMetadata(LLVMContext::MD_prof, nullptr);
        CB.setMetadata(LLVMContext::MD_callees, nullptr);
      }

      // In either trapping or non-checking mode, devirtualize original call.
      else {
        // Devirtualize unconditionally.
        CB.setCalledOperand(Callee);
        // Since the call site is now direct, we must clear metadata that
        // is only appropriate for indirect calls. This includes !prof and
        // !callees metadata.
        CB.setMetadata(LLVMContext::MD_prof, nullptr);
        CB.setMetadata(LLVMContext::MD_callees, nullptr);
        if (CB.getCalledOperand() &&
            CB.getOperandBundle(LLVMContext::OB_ptrauth)) {
          auto *NewCS = CallBase::removeOperandBundle(
              &CB, LLVMContext::OB_ptrauth, CB.getIterator());
          CB.replaceAllUsesWith(NewCS);
          // Schedule for deletion at the end of pass run.
          CallsWithPtrAuthBundleRemoved.push_back(&CB);
        }
      }

      // This use is no longer unsafe.
      if (VCallSite.NumUnsafeUses)
        --*VCallSite.NumUnsafeUses;
    }
    if (CSInfo.isExported())
      IsExported = true;
    CSInfo.markDevirt();
  };
  Apply(SlotInfo.CSInfo);
  for (auto &P : SlotInfo.ConstCSInfo)
    Apply(P.second);
}

static bool AddCalls(VTableSlotInfo &SlotInfo, const ValueInfo &Callee) {
  // We can't add calls if we haven't seen a definition
  if (Callee.getSummaryList().empty())
    return false;

  // Insert calls into the summary index so that the devirtualized targets
  // are eligible for import.
  // FIXME: Annotate type tests with hotness. For now, mark these as hot
  // to better ensure we have the opportunity to inline them.
  bool IsExported = false;
  auto &S = Callee.getSummaryList()[0];
  CalleeInfo CI(CalleeInfo::HotnessType::Hot, /* HasTailCall = */ false,
                /* RelBF = */ 0);
  auto AddCalls = [&](CallSiteInfo &CSInfo) {
    for (auto *FS : CSInfo.SummaryTypeCheckedLoadUsers) {
      FS->addCall({Callee, CI});
      IsExported |= S->modulePath() != FS->modulePath();
    }
    for (auto *FS : CSInfo.SummaryTypeTestAssumeUsers) {
      FS->addCall({Callee, CI});
      IsExported |= S->modulePath() != FS->modulePath();
    }
  };
  AddCalls(SlotInfo.CSInfo);
  for (auto &P : SlotInfo.ConstCSInfo)
    AddCalls(P.second);
  return IsExported;
}

bool DevirtModule::trySingleImplDevirt(
    ModuleSummaryIndex *ExportSummary,
    MutableArrayRef<VirtualCallTarget> TargetsForSlot, VTableSlotInfo &SlotInfo,
    WholeProgramDevirtResolution *Res) {
  // See if the program contains a single implementation of this virtual
  // function.
  auto *TheFn = TargetsForSlot[0].Fn;
  for (auto &&Target : TargetsForSlot)
    if (TheFn != Target.Fn)
      return false;

  // If so, update each call site to call that implementation directly.
  if (RemarksEnabled || AreStatisticsEnabled())
    TargetsForSlot[0].WasDevirt = true;

  bool IsExported = false;
  applySingleImplDevirt(SlotInfo, TheFn, IsExported);
  if (!IsExported)
    return false;

  // If the only implementation has local linkage, we must promote to external
  // to make it visible to thin LTO objects. We can only get here during the
  // ThinLTO export phase.
  if (TheFn->hasLocalLinkage()) {
    std::string NewName = (TheFn->getName() + ".llvm.merged").str();

    // Since we are renaming the function, any comdats with the same name must
    // also be renamed. This is required when targeting COFF, as the comdat name
    // must match one of the names of the symbols in the comdat.
    if (Comdat *C = TheFn->getComdat()) {
      if (C->getName() == TheFn->getName()) {
        Comdat *NewC = M.getOrInsertComdat(NewName);
        NewC->setSelectionKind(C->getSelectionKind());
        for (GlobalObject &GO : M.global_objects())
          if (GO.getComdat() == C)
            GO.setComdat(NewC);
      }
    }

    TheFn->setLinkage(GlobalValue::ExternalLinkage);
    TheFn->setVisibility(GlobalValue::HiddenVisibility);
    TheFn->setName(NewName);
  }
  if (ValueInfo TheFnVI = ExportSummary->getValueInfo(TheFn->getGUID()))
    // Any needed promotion of 'TheFn' has already been done during
    // LTO unit split, so we can ignore return value of AddCalls.
    AddCalls(SlotInfo, TheFnVI);

  Res->TheKind = WholeProgramDevirtResolution::SingleImpl;
  Res->SingleImplName = std::string(TheFn->getName());

  return true;
}

bool DevirtIndex::trySingleImplDevirt(MutableArrayRef<ValueInfo> TargetsForSlot,
                                      VTableSlotSummary &SlotSummary,
                                      VTableSlotInfo &SlotInfo,
                                      WholeProgramDevirtResolution *Res,
                                      std::set<ValueInfo> &DevirtTargets) {
  // See if the program contains a single implementation of this virtual
  // function.
  auto TheFn = TargetsForSlot[0];
  for (auto &&Target : TargetsForSlot)
    if (TheFn != Target)
      return false;

  // Don't devirtualize if we don't have target definition.
  auto Size = TheFn.getSummaryList().size();
  if (!Size)
    return false;

  // Don't devirtualize function if we're told to skip it
  // in -wholeprogramdevirt-skip.
  if (FunctionsToSkip.match(TheFn.name()))
    return false;

  // If the summary list contains multiple summaries where at least one is
  // a local, give up, as we won't know which (possibly promoted) name to use.
  for (const auto &S : TheFn.getSummaryList())
    if (GlobalValue::isLocalLinkage(S->linkage()) && Size > 1)
      return false;

  // Collect functions devirtualized at least for one call site for stats.
  if (PrintSummaryDevirt || AreStatisticsEnabled())
    DevirtTargets.insert(TheFn);

  auto &S = TheFn.getSummaryList()[0];
  bool IsExported = AddCalls(SlotInfo, TheFn);
  if (IsExported)
    ExportedGUIDs.insert(TheFn.getGUID());

  // Record in summary for use in devirtualization during the ThinLTO import
  // step.
  Res->TheKind = WholeProgramDevirtResolution::SingleImpl;
  if (GlobalValue::isLocalLinkage(S->linkage())) {
    if (IsExported)
      // If target is a local function and we are exporting it by
      // devirtualizing a call in another module, we need to record the
      // promoted name.
      Res->SingleImplName = ModuleSummaryIndex::getGlobalNameForLocal(
          TheFn.name(), ExportSummary.getModuleHash(S->modulePath()));
    else {
      LocalWPDTargetsMap[TheFn].push_back(SlotSummary);
      Res->SingleImplName = std::string(TheFn.name());
    }
  } else
    Res->SingleImplName = std::string(TheFn.name());

  // Name will be empty if this thin link driven off of serialized combined
  // index (e.g. llvm-lto). However, WPD is not supported/invoked for the
  // legacy LTO API anyway.
  assert(!Res->SingleImplName.empty());

  return true;
}

void DevirtModule::tryICallBranchFunnel(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot, VTableSlotInfo &SlotInfo,
    WholeProgramDevirtResolution *Res, VTableSlot Slot) {
  Triple T(M.getTargetTriple());
  if (T.getArch() != Triple::x86_64)
    return;

  if (TargetsForSlot.size() > ClThreshold)
    return;

  bool HasNonDevirt = !SlotInfo.CSInfo.AllCallSitesDevirted;
  if (!HasNonDevirt)
    for (auto &P : SlotInfo.ConstCSInfo)
      if (!P.second.AllCallSitesDevirted) {
        HasNonDevirt = true;
        break;
      }

  if (!HasNonDevirt)
    return;

  FunctionType *FT =
      FunctionType::get(Type::getVoidTy(M.getContext()), {Int8PtrTy}, true);
  Function *JT;
  if (isa<MDString>(Slot.TypeID)) {
    JT = Function::Create(FT, Function::ExternalLinkage,
                          M.getDataLayout().getProgramAddressSpace(),
                          getGlobalName(Slot, {}, "branch_funnel"), &M);
    JT->setVisibility(GlobalValue::HiddenVisibility);
  } else {
    JT = Function::Create(FT, Function::InternalLinkage,
                          M.getDataLayout().getProgramAddressSpace(),
                          "branch_funnel", &M);
  }
  JT->addParamAttr(0, Attribute::Nest);

  std::vector<Value *> JTArgs;
  JTArgs.push_back(JT->arg_begin());
  for (auto &T : TargetsForSlot) {
    JTArgs.push_back(getMemberAddr(T.TM));
    JTArgs.push_back(T.Fn);
  }

  BasicBlock *BB = BasicBlock::Create(M.getContext(), "", JT, nullptr);
  Function *Intr =
      Intrinsic::getDeclaration(&M, llvm::Intrinsic::icall_branch_funnel, {});

  auto *CI = CallInst::Create(Intr, JTArgs, "", BB);
  CI->setTailCallKind(CallInst::TCK_MustTail);
  ReturnInst::Create(M.getContext(), nullptr, BB);

  bool IsExported = false;
  applyICallBranchFunnel(SlotInfo, JT, IsExported);
  if (IsExported)
    Res->TheKind = WholeProgramDevirtResolution::BranchFunnel;
}

void DevirtModule::applyICallBranchFunnel(VTableSlotInfo &SlotInfo,
                                          Constant *JT, bool &IsExported) {
  auto Apply = [&](CallSiteInfo &CSInfo) {
    if (CSInfo.isExported())
      IsExported = true;
    if (CSInfo.AllCallSitesDevirted)
      return;

    std::map<CallBase *, CallBase *> CallBases;
    for (auto &&VCallSite : CSInfo.CallSites) {
      CallBase &CB = VCallSite.CB;

      if (CallBases.find(&CB) != CallBases.end()) {
        // When finding devirtualizable calls, it's possible to find the same
        // vtable passed to multiple llvm.type.test or llvm.type.checked.load
        // calls, which can cause duplicate call sites to be recorded in
        // [Const]CallSites. If we've already found one of these
        // call instances, just ignore it. It will be replaced later.
        continue;
      }

      // Jump tables are only profitable if the retpoline mitigation is enabled.
      Attribute FSAttr = CB.getCaller()->getFnAttribute("target-features");
      if (!FSAttr.isValid() ||
          !FSAttr.getValueAsString().contains("+retpoline"))
        continue;

      NumBranchFunnel++;
      if (RemarksEnabled)
        VCallSite.emitRemark("branch-funnel",
                             JT->stripPointerCasts()->getName(), OREGetter);

      // Pass the address of the vtable in the nest register, which is r10 on
      // x86_64.
      std::vector<Type *> NewArgs;
      NewArgs.push_back(Int8PtrTy);
      append_range(NewArgs, CB.getFunctionType()->params());
      FunctionType *NewFT =
          FunctionType::get(CB.getFunctionType()->getReturnType(), NewArgs,
                            CB.getFunctionType()->isVarArg());
      PointerType *NewFTPtr = PointerType::getUnqual(NewFT);

      IRBuilder<> IRB(&CB);
      std::vector<Value *> Args;
      Args.push_back(VCallSite.VTable);
      llvm::append_range(Args, CB.args());

      CallBase *NewCS = nullptr;
      if (isa<CallInst>(CB))
        NewCS = IRB.CreateCall(NewFT, IRB.CreateBitCast(JT, NewFTPtr), Args);
      else
        NewCS = IRB.CreateInvoke(NewFT, IRB.CreateBitCast(JT, NewFTPtr),
                                 cast<InvokeInst>(CB).getNormalDest(),
                                 cast<InvokeInst>(CB).getUnwindDest(), Args);
      NewCS->setCallingConv(CB.getCallingConv());

      AttributeList Attrs = CB.getAttributes();
      std::vector<AttributeSet> NewArgAttrs;
      NewArgAttrs.push_back(AttributeSet::get(
          M.getContext(), ArrayRef<Attribute>{Attribute::get(
                              M.getContext(), Attribute::Nest)}));
      for (unsigned I = 0; I + 2 <  Attrs.getNumAttrSets(); ++I)
        NewArgAttrs.push_back(Attrs.getParamAttrs(I));
      NewCS->setAttributes(
          AttributeList::get(M.getContext(), Attrs.getFnAttrs(),
                             Attrs.getRetAttrs(), NewArgAttrs));

      CallBases[&CB] = NewCS;

      // This use is no longer unsafe.
      if (VCallSite.NumUnsafeUses)
        --*VCallSite.NumUnsafeUses;
    }
    // Don't mark as devirtualized because there may be callers compiled without
    // retpoline mitigation, which would mean that they are lowered to
    // llvm.type.test and therefore require an llvm.type.test resolution for the
    // type identifier.

    for (auto &[Old, New] : CallBases) {
      Old->replaceAllUsesWith(New);
      Old->eraseFromParent();
    }
  };
  Apply(SlotInfo.CSInfo);
  for (auto &P : SlotInfo.ConstCSInfo)
    Apply(P.second);
}

bool DevirtModule::tryEvaluateFunctionsWithArgs(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot,
    ArrayRef<uint64_t> Args) {
  // Evaluate each function and store the result in each target's RetVal
  // field.
  for (VirtualCallTarget &Target : TargetsForSlot) {
    // TODO: Skip for now if the vtable symbol was an alias to a function,
    // need to evaluate whether it would be correct to analyze the aliasee
    // function for this optimization.
    auto Fn = dyn_cast<Function>(Target.Fn);
    if (!Fn)
      return false;

    if (Fn->arg_size() != Args.size() + 1)
      return false;

    Evaluator Eval(M.getDataLayout(), nullptr);
    SmallVector<Constant *, 2> EvalArgs;
    EvalArgs.push_back(
        Constant::getNullValue(Fn->getFunctionType()->getParamType(0)));
    for (unsigned I = 0; I != Args.size(); ++I) {
      auto *ArgTy =
          dyn_cast<IntegerType>(Fn->getFunctionType()->getParamType(I + 1));
      if (!ArgTy)
        return false;
      EvalArgs.push_back(ConstantInt::get(ArgTy, Args[I]));
    }

    Constant *RetVal;
    if (!Eval.EvaluateFunction(Fn, RetVal, EvalArgs) ||
        !isa<ConstantInt>(RetVal))
      return false;
    Target.RetVal = cast<ConstantInt>(RetVal)->getZExtValue();
  }
  return true;
}

void DevirtModule::applyUniformRetValOpt(CallSiteInfo &CSInfo, StringRef FnName,
                                         uint64_t TheRetVal) {
  for (auto Call : CSInfo.CallSites) {
    if (!OptimizedCalls.insert(&Call.CB).second)
      continue;
    NumUniformRetVal++;
    Call.replaceAndErase(
        "uniform-ret-val", FnName, RemarksEnabled, OREGetter,
        ConstantInt::get(cast<IntegerType>(Call.CB.getType()), TheRetVal));
  }
  CSInfo.markDevirt();
}

bool DevirtModule::tryUniformRetValOpt(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot, CallSiteInfo &CSInfo,
    WholeProgramDevirtResolution::ByArg *Res) {
  // Uniform return value optimization. If all functions return the same
  // constant, replace all calls with that constant.
  uint64_t TheRetVal = TargetsForSlot[0].RetVal;
  for (const VirtualCallTarget &Target : TargetsForSlot)
    if (Target.RetVal != TheRetVal)
      return false;

  if (CSInfo.isExported()) {
    Res->TheKind = WholeProgramDevirtResolution::ByArg::UniformRetVal;
    Res->Info = TheRetVal;
  }

  applyUniformRetValOpt(CSInfo, TargetsForSlot[0].Fn->getName(), TheRetVal);
  if (RemarksEnabled || AreStatisticsEnabled())
    for (auto &&Target : TargetsForSlot)
      Target.WasDevirt = true;
  return true;
}

std::string DevirtModule::getGlobalName(VTableSlot Slot,
                                        ArrayRef<uint64_t> Args,
                                        StringRef Name) {
  std::string FullName = "__typeid_";
  raw_string_ostream OS(FullName);
  OS << cast<MDString>(Slot.TypeID)->getString() << '_' << Slot.ByteOffset;
  for (uint64_t Arg : Args)
    OS << '_' << Arg;
  OS << '_' << Name;
  return FullName;
}

bool DevirtModule::shouldExportConstantsAsAbsoluteSymbols() {
  Triple T(M.getTargetTriple());
  return T.isX86() && T.getObjectFormat() == Triple::ELF;
}

void DevirtModule::exportGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                StringRef Name, Constant *C) {
  GlobalAlias *GA = GlobalAlias::create(Int8Ty, 0, GlobalValue::ExternalLinkage,
                                        getGlobalName(Slot, Args, Name), C, &M);
  GA->setVisibility(GlobalValue::HiddenVisibility);
}

void DevirtModule::exportConstant(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                  StringRef Name, uint32_t Const,
                                  uint32_t &Storage) {
  if (shouldExportConstantsAsAbsoluteSymbols()) {
    exportGlobal(
        Slot, Args, Name,
        ConstantExpr::getIntToPtr(ConstantInt::get(Int32Ty, Const), Int8PtrTy));
    return;
  }

  Storage = Const;
}

Constant *DevirtModule::importGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                     StringRef Name) {
  Constant *C =
      M.getOrInsertGlobal(getGlobalName(Slot, Args, Name), Int8Arr0Ty);
  auto *GV = dyn_cast<GlobalVariable>(C);
  if (GV)
    GV->setVisibility(GlobalValue::HiddenVisibility);
  return C;
}

Constant *DevirtModule::importConstant(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                       StringRef Name, IntegerType *IntTy,
                                       uint32_t Storage) {
  if (!shouldExportConstantsAsAbsoluteSymbols())
    return ConstantInt::get(IntTy, Storage);

  Constant *C = importGlobal(Slot, Args, Name);
  auto *GV = cast<GlobalVariable>(C->stripPointerCasts());
  C = ConstantExpr::getPtrToInt(C, IntTy);

  // We only need to set metadata if the global is newly created, in which
  // case it would not have hidden visibility.
  if (GV->hasMetadata(LLVMContext::MD_absolute_symbol))
    return C;

  auto SetAbsRange = [&](uint64_t Min, uint64_t Max) {
    auto *MinC = ConstantAsMetadata::get(ConstantInt::get(IntPtrTy, Min));
    auto *MaxC = ConstantAsMetadata::get(ConstantInt::get(IntPtrTy, Max));
    GV->setMetadata(LLVMContext::MD_absolute_symbol,
                    MDNode::get(M.getContext(), {MinC, MaxC}));
  };
  unsigned AbsWidth = IntTy->getBitWidth();
  if (AbsWidth == IntPtrTy->getBitWidth())
    SetAbsRange(~0ull, ~0ull); // Full set.
  else
    SetAbsRange(0, 1ull << AbsWidth);
  return C;
}

void DevirtModule::applyUniqueRetValOpt(CallSiteInfo &CSInfo, StringRef FnName,
                                        bool IsOne,
                                        Constant *UniqueMemberAddr) {
  for (auto &&Call : CSInfo.CallSites) {
    if (!OptimizedCalls.insert(&Call.CB).second)
      continue;
    IRBuilder<> B(&Call.CB);
    Value *Cmp =
        B.CreateICmp(IsOne ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE, Call.VTable,
                     B.CreateBitCast(UniqueMemberAddr, Call.VTable->getType()));
    Cmp = B.CreateZExt(Cmp, Call.CB.getType());
    NumUniqueRetVal++;
    Call.replaceAndErase("unique-ret-val", FnName, RemarksEnabled, OREGetter,
                         Cmp);
  }
  CSInfo.markDevirt();
}

Constant *DevirtModule::getMemberAddr(const TypeMemberInfo *M) {
  return ConstantExpr::getGetElementPtr(Int8Ty, M->Bits->GV,
                                        ConstantInt::get(Int64Ty, M->Offset));
}

bool DevirtModule::tryUniqueRetValOpt(
    unsigned BitWidth, MutableArrayRef<VirtualCallTarget> TargetsForSlot,
    CallSiteInfo &CSInfo, WholeProgramDevirtResolution::ByArg *Res,
    VTableSlot Slot, ArrayRef<uint64_t> Args) {
  // IsOne controls whether we look for a 0 or a 1.
  auto tryUniqueRetValOptFor = [&](bool IsOne) {
    const TypeMemberInfo *UniqueMember = nullptr;
    for (const VirtualCallTarget &Target : TargetsForSlot) {
      if (Target.RetVal == (IsOne ? 1 : 0)) {
        if (UniqueMember)
          return false;
        UniqueMember = Target.TM;
      }
    }

    // We should have found a unique member or bailed out by now. We already
    // checked for a uniform return value in tryUniformRetValOpt.
    assert(UniqueMember);

    Constant *UniqueMemberAddr = getMemberAddr(UniqueMember);
    if (CSInfo.isExported()) {
      Res->TheKind = WholeProgramDevirtResolution::ByArg::UniqueRetVal;
      Res->Info = IsOne;

      exportGlobal(Slot, Args, "unique_member", UniqueMemberAddr);
    }

    // Replace each call with the comparison.
    applyUniqueRetValOpt(CSInfo, TargetsForSlot[0].Fn->getName(), IsOne,
                         UniqueMemberAddr);

    // Update devirtualization statistics for targets.
    if (RemarksEnabled || AreStatisticsEnabled())
      for (auto &&Target : TargetsForSlot)
        Target.WasDevirt = true;

    return true;
  };

  if (BitWidth == 1) {
    if (tryUniqueRetValOptFor(true))
      return true;
    if (tryUniqueRetValOptFor(false))
      return true;
  }
  return false;
}

void DevirtModule::applyVirtualConstProp(CallSiteInfo &CSInfo, StringRef FnName,
                                         Constant *Byte, Constant *Bit) {
  for (auto Call : CSInfo.CallSites) {
    if (!OptimizedCalls.insert(&Call.CB).second)
      continue;
    auto *RetType = cast<IntegerType>(Call.CB.getType());
    IRBuilder<> B(&Call.CB);
    Value *Addr = B.CreatePtrAdd(Call.VTable, Byte);
    if (RetType->getBitWidth() == 1) {
      Value *Bits = B.CreateLoad(Int8Ty, Addr);
      Value *BitsAndBit = B.CreateAnd(Bits, Bit);
      auto IsBitSet = B.CreateICmpNE(BitsAndBit, ConstantInt::get(Int8Ty, 0));
      NumVirtConstProp1Bit++;
      Call.replaceAndErase("virtual-const-prop-1-bit", FnName, RemarksEnabled,
                           OREGetter, IsBitSet);
    } else {
      Value *Val = B.CreateLoad(RetType, Addr);
      NumVirtConstProp++;
      Call.replaceAndErase("virtual-const-prop", FnName, RemarksEnabled,
                           OREGetter, Val);
    }
  }
  CSInfo.markDevirt();
}

bool DevirtModule::tryVirtualConstProp(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot, VTableSlotInfo &SlotInfo,
    WholeProgramDevirtResolution *Res, VTableSlot Slot) {
  // TODO: Skip for now if the vtable symbol was an alias to a function,
  // need to evaluate whether it would be correct to analyze the aliasee
  // function for this optimization.
  auto Fn = dyn_cast<Function>(TargetsForSlot[0].Fn);
  if (!Fn)
    return false;
  // This only works if the function returns an integer.
  auto RetType = dyn_cast<IntegerType>(Fn->getReturnType());
  if (!RetType)
    return false;
  unsigned BitWidth = RetType->getBitWidth();
  if (BitWidth > 64)
    return false;

  // Make sure that each function is defined, does not access memory, takes at
  // least one argument, does not use its first argument (which we assume is
  // 'this'), and has the same return type.
  //
  // Note that we test whether this copy of the function is readnone, rather
  // than testing function attributes, which must hold for any copy of the
  // function, even a less optimized version substituted at link time. This is
  // sound because the virtual constant propagation optimizations effectively
  // inline all implementations of the virtual function into each call site,
  // rather than using function attributes to perform local optimization.
  for (VirtualCallTarget &Target : TargetsForSlot) {
    // TODO: Skip for now if the vtable symbol was an alias to a function,
    // need to evaluate whether it would be correct to analyze the aliasee
    // function for this optimization.
    auto Fn = dyn_cast<Function>(Target.Fn);
    if (!Fn)
      return false;

    if (Fn->isDeclaration() ||
        !computeFunctionBodyMemoryAccess(*Fn, AARGetter(*Fn))
             .doesNotAccessMemory() ||
        Fn->arg_empty() || !Fn->arg_begin()->use_empty() ||
        Fn->getReturnType() != RetType)
      return false;
  }

  for (auto &&CSByConstantArg : SlotInfo.ConstCSInfo) {
    if (!tryEvaluateFunctionsWithArgs(TargetsForSlot, CSByConstantArg.first))
      continue;

    WholeProgramDevirtResolution::ByArg *ResByArg = nullptr;
    if (Res)
      ResByArg = &Res->ResByArg[CSByConstantArg.first];

    if (tryUniformRetValOpt(TargetsForSlot, CSByConstantArg.second, ResByArg))
      continue;

    if (tryUniqueRetValOpt(BitWidth, TargetsForSlot, CSByConstantArg.second,
                           ResByArg, Slot, CSByConstantArg.first))
      continue;

    // Find an allocation offset in bits in all vtables associated with the
    // type.
    uint64_t AllocBefore =
        findLowestOffset(TargetsForSlot, /*IsAfter=*/false, BitWidth);
    uint64_t AllocAfter =
        findLowestOffset(TargetsForSlot, /*IsAfter=*/true, BitWidth);

    // Calculate the total amount of padding needed to store a value at both
    // ends of the object.
    uint64_t TotalPaddingBefore = 0, TotalPaddingAfter = 0;
    for (auto &&Target : TargetsForSlot) {
      TotalPaddingBefore += std::max<int64_t>(
          (AllocBefore + 7) / 8 - Target.allocatedBeforeBytes() - 1, 0);
      TotalPaddingAfter += std::max<int64_t>(
          (AllocAfter + 7) / 8 - Target.allocatedAfterBytes() - 1, 0);
    }

    // If the amount of padding is too large, give up.
    // FIXME: do something smarter here.
    if (std::min(TotalPaddingBefore, TotalPaddingAfter) > 128)
      continue;

    // Calculate the offset to the value as a (possibly negative) byte offset
    // and (if applicable) a bit offset, and store the values in the targets.
    int64_t OffsetByte;
    uint64_t OffsetBit;
    if (TotalPaddingBefore <= TotalPaddingAfter)
      setBeforeReturnValues(TargetsForSlot, AllocBefore, BitWidth, OffsetByte,
                            OffsetBit);
    else
      setAfterReturnValues(TargetsForSlot, AllocAfter, BitWidth, OffsetByte,
                           OffsetBit);

    if (RemarksEnabled || AreStatisticsEnabled())
      for (auto &&Target : TargetsForSlot)
        Target.WasDevirt = true;


    if (CSByConstantArg.second.isExported()) {
      ResByArg->TheKind = WholeProgramDevirtResolution::ByArg::VirtualConstProp;
      exportConstant(Slot, CSByConstantArg.first, "byte", OffsetByte,
                     ResByArg->Byte);
      exportConstant(Slot, CSByConstantArg.first, "bit", 1ULL << OffsetBit,
                     ResByArg->Bit);
    }

    // Rewrite each call to a load from OffsetByte/OffsetBit.
    Constant *ByteConst = ConstantInt::get(Int32Ty, OffsetByte);
    Constant *BitConst = ConstantInt::get(Int8Ty, 1ULL << OffsetBit);
    applyVirtualConstProp(CSByConstantArg.second,
                          TargetsForSlot[0].Fn->getName(), ByteConst, BitConst);
  }
  return true;
}

void DevirtModule::rebuildGlobal(VTableBits &B) {
  if (B.Before.Bytes.empty() && B.After.Bytes.empty())
    return;

  // Align the before byte array to the global's minimum alignment so that we
  // don't break any alignment requirements on the global.
  Align Alignment = M.getDataLayout().getValueOrABITypeAlignment(
      B.GV->getAlign(), B.GV->getValueType());
  B.Before.Bytes.resize(alignTo(B.Before.Bytes.size(), Alignment));

  // Before was stored in reverse order; flip it now.
  for (size_t I = 0, Size = B.Before.Bytes.size(); I != Size / 2; ++I)
    std::swap(B.Before.Bytes[I], B.Before.Bytes[Size - 1 - I]);

  // Build an anonymous global containing the before bytes, followed by the
  // original initializer, followed by the after bytes.
  auto NewInit = ConstantStruct::getAnon(
      {ConstantDataArray::get(M.getContext(), B.Before.Bytes),
       B.GV->getInitializer(),
       ConstantDataArray::get(M.getContext(), B.After.Bytes)});
  auto NewGV =
      new GlobalVariable(M, NewInit->getType(), B.GV->isConstant(),
                         GlobalVariable::PrivateLinkage, NewInit, "", B.GV);
  NewGV->setSection(B.GV->getSection());
  NewGV->setComdat(B.GV->getComdat());
  NewGV->setAlignment(B.GV->getAlign());

  // Copy the original vtable's metadata to the anonymous global, adjusting
  // offsets as required.
  NewGV->copyMetadata(B.GV, B.Before.Bytes.size());

  // Build an alias named after the original global, pointing at the second
  // element (the original initializer).
  auto Alias = GlobalAlias::create(
      B.GV->getInitializer()->getType(), 0, B.GV->getLinkage(), "",
      ConstantExpr::getInBoundsGetElementPtr(
          NewInit->getType(), NewGV,
          ArrayRef<Constant *>{ConstantInt::get(Int32Ty, 0),
                               ConstantInt::get(Int32Ty, 1)}),
      &M);
  Alias->setVisibility(B.GV->getVisibility());
  Alias->takeName(B.GV);

  B.GV->replaceAllUsesWith(Alias);
  B.GV->eraseFromParent();
}

bool DevirtModule::areRemarksEnabled() {
  const auto &FL = M.getFunctionList();
  for (const Function &Fn : FL) {
    if (Fn.empty())
      continue;
    auto DI = OptimizationRemark(DEBUG_TYPE, "", DebugLoc(), &Fn.front());
    return DI.isEnabled();
  }
  return false;
}

void DevirtModule::scanTypeTestUsers(
    Function *TypeTestFunc,
    DenseMap<Metadata *, std::set<TypeMemberInfo>> &TypeIdMap) {
  // Find all virtual calls via a virtual table pointer %p under an assumption
  // of the form llvm.assume(llvm.type.test(%p, %md)). This indicates that %p
  // points to a member of the type identifier %md. Group calls by (type ID,
  // offset) pair (effectively the identity of the virtual function) and store
  // to CallSlots.
  for (Use &U : llvm::make_early_inc_range(TypeTestFunc->uses())) {
    auto *CI = dyn_cast<CallInst>(U.getUser());
    if (!CI)
      continue;

    // Search for virtual calls based on %p and add them to DevirtCalls.
    SmallVector<DevirtCallSite, 1> DevirtCalls;
    SmallVector<CallInst *, 1> Assumes;
    auto &DT = LookupDomTree(*CI->getFunction());
    findDevirtualizableCallsForTypeTest(DevirtCalls, Assumes, CI, DT);

    Metadata *TypeId =
        cast<MetadataAsValue>(CI->getArgOperand(1))->getMetadata();
    // If we found any, add them to CallSlots.
    if (!Assumes.empty()) {
      Value *Ptr = CI->getArgOperand(0)->stripPointerCasts();
      for (DevirtCallSite Call : DevirtCalls)
        CallSlots[{TypeId, Call.Offset}].addCallSite(Ptr, Call.CB, nullptr);
    }

    auto RemoveTypeTestAssumes = [&]() {
      // We no longer need the assumes or the type test.
      for (auto *Assume : Assumes)
        Assume->eraseFromParent();
      // We can't use RecursivelyDeleteTriviallyDeadInstructions here because we
      // may use the vtable argument later.
      if (CI->use_empty())
        CI->eraseFromParent();
    };

    // At this point we could remove all type test assume sequences, as they
    // were originally inserted for WPD. However, we can keep these in the
    // code stream for later analysis (e.g. to help drive more efficient ICP
    // sequences). They will eventually be removed by a second LowerTypeTests
    // invocation that cleans them up. In order to do this correctly, the first
    // LowerTypeTests invocation needs to know that they have "Unknown" type
    // test resolution, so that they aren't treated as Unsat and lowered to
    // False, which will break any uses on assumes. Below we remove any type
    // test assumes that will not be treated as Unknown by LTT.

    // The type test assumes will be treated by LTT as Unsat if the type id is
    // not used on a global (in which case it has no entry in the TypeIdMap).
    if (!TypeIdMap.count(TypeId))
      RemoveTypeTestAssumes();

    // For ThinLTO importing, we need to remove the type test assumes if this is
    // an MDString type id without a corresponding TypeIdSummary. Any
    // non-MDString type ids are ignored and treated as Unknown by LTT, so their
    // type test assumes can be kept. If the MDString type id is missing a
    // TypeIdSummary (e.g. because there was no use on a vcall, preventing the
    // exporting phase of WPD from analyzing it), then it would be treated as
    // Unsat by LTT and we need to remove its type test assumes here. If not
    // used on a vcall we don't need them for later optimization use in any
    // case.
    else if (ImportSummary && isa<MDString>(TypeId)) {
      const TypeIdSummary *TidSummary =
          ImportSummary->getTypeIdSummary(cast<MDString>(TypeId)->getString());
      if (!TidSummary)
        RemoveTypeTestAssumes();
      else
        // If one was created it should not be Unsat, because if we reached here
        // the type id was used on a global.
        assert(TidSummary->TTRes.TheKind != TypeTestResolution::Unsat);
    }
  }
}

void DevirtModule::scanTypeCheckedLoadUsers(Function *TypeCheckedLoadFunc) {
  Function *TypeTestFunc = Intrinsic::getDeclaration(&M, Intrinsic::type_test);

  for (Use &U : llvm::make_early_inc_range(TypeCheckedLoadFunc->uses())) {
    auto *CI = dyn_cast<CallInst>(U.getUser());
    if (!CI)
      continue;

    Value *Ptr = CI->getArgOperand(0);
    Value *Offset = CI->getArgOperand(1);
    Value *TypeIdValue = CI->getArgOperand(2);
    Metadata *TypeId = cast<MetadataAsValue>(TypeIdValue)->getMetadata();

    SmallVector<DevirtCallSite, 1> DevirtCalls;
    SmallVector<Instruction *, 1> LoadedPtrs;
    SmallVector<Instruction *, 1> Preds;
    bool HasNonCallUses = false;
    auto &DT = LookupDomTree(*CI->getFunction());
    findDevirtualizableCallsForTypeCheckedLoad(DevirtCalls, LoadedPtrs, Preds,
                                               HasNonCallUses, CI, DT);

    // Start by generating "pessimistic" code that explicitly loads the function
    // pointer from the vtable and performs the type check. If possible, we will
    // eliminate the load and the type check later.

    // If possible, only generate the load at the point where it is used.
    // This helps avoid unnecessary spills.
    IRBuilder<> LoadB(
        (LoadedPtrs.size() == 1 && !HasNonCallUses) ? LoadedPtrs[0] : CI);

    Value *LoadedValue = nullptr;
    if (TypeCheckedLoadFunc->getIntrinsicID() ==
        Intrinsic::type_checked_load_relative) {
      Value *GEP = LoadB.CreatePtrAdd(Ptr, Offset);
      LoadedValue = LoadB.CreateLoad(Int32Ty, GEP);
      LoadedValue = LoadB.CreateSExt(LoadedValue, IntPtrTy);
      GEP = LoadB.CreatePtrToInt(GEP, IntPtrTy);
      LoadedValue = LoadB.CreateAdd(GEP, LoadedValue);
      LoadedValue = LoadB.CreateIntToPtr(LoadedValue, Int8PtrTy);
    } else {
      Value *GEP = LoadB.CreatePtrAdd(Ptr, Offset);
      LoadedValue = LoadB.CreateLoad(Int8PtrTy, GEP);
    }

    for (Instruction *LoadedPtr : LoadedPtrs) {
      LoadedPtr->replaceAllUsesWith(LoadedValue);
      LoadedPtr->eraseFromParent();
    }

    // Likewise for the type test.
    IRBuilder<> CallB((Preds.size() == 1 && !HasNonCallUses) ? Preds[0] : CI);
    CallInst *TypeTestCall = CallB.CreateCall(TypeTestFunc, {Ptr, TypeIdValue});

    for (Instruction *Pred : Preds) {
      Pred->replaceAllUsesWith(TypeTestCall);
      Pred->eraseFromParent();
    }

    // We have already erased any extractvalue instructions that refer to the
    // intrinsic call, but the intrinsic may have other non-extractvalue uses
    // (although this is unlikely). In that case, explicitly build a pair and
    // RAUW it.
    if (!CI->use_empty()) {
      Value *Pair = PoisonValue::get(CI->getType());
      IRBuilder<> B(CI);
      Pair = B.CreateInsertValue(Pair, LoadedValue, {0});
      Pair = B.CreateInsertValue(Pair, TypeTestCall, {1});
      CI->replaceAllUsesWith(Pair);
    }

    // The number of unsafe uses is initially the number of uses.
    auto &NumUnsafeUses = NumUnsafeUsesForTypeTest[TypeTestCall];
    NumUnsafeUses = DevirtCalls.size();

    // If the function pointer has a non-call user, we cannot eliminate the type
    // check, as one of those users may eventually call the pointer. Increment
    // the unsafe use count to make sure it cannot reach zero.
    if (HasNonCallUses)
      ++NumUnsafeUses;
    for (DevirtCallSite Call : DevirtCalls) {
      CallSlots[{TypeId, Call.Offset}].addCallSite(Ptr, Call.CB,
                                                   &NumUnsafeUses);
    }

    CI->eraseFromParent();
  }
}

void DevirtModule::importResolution(VTableSlot Slot, VTableSlotInfo &SlotInfo) {
  auto *TypeId = dyn_cast<MDString>(Slot.TypeID);
  if (!TypeId)
    return;
  const TypeIdSummary *TidSummary =
      ImportSummary->getTypeIdSummary(TypeId->getString());
  if (!TidSummary)
    return;
  auto ResI = TidSummary->WPDRes.find(Slot.ByteOffset);
  if (ResI == TidSummary->WPDRes.end())
    return;
  const WholeProgramDevirtResolution &Res = ResI->second;

  if (Res.TheKind == WholeProgramDevirtResolution::SingleImpl) {
    assert(!Res.SingleImplName.empty());
    // The type of the function in the declaration is irrelevant because every
    // call site will cast it to the correct type.
    Constant *SingleImpl =
        cast<Constant>(M.getOrInsertFunction(Res.SingleImplName,
                                             Type::getVoidTy(M.getContext()))
                           .getCallee());

    // This is the import phase so we should not be exporting anything.
    bool IsExported = false;
    applySingleImplDevirt(SlotInfo, SingleImpl, IsExported);
    assert(!IsExported);
  }

  for (auto &CSByConstantArg : SlotInfo.ConstCSInfo) {
    auto I = Res.ResByArg.find(CSByConstantArg.first);
    if (I == Res.ResByArg.end())
      continue;
    auto &ResByArg = I->second;
    // FIXME: We should figure out what to do about the "function name" argument
    // to the apply* functions, as the function names are unavailable during the
    // importing phase. For now we just pass the empty string. This does not
    // impact correctness because the function names are just used for remarks.
    switch (ResByArg.TheKind) {
    case WholeProgramDevirtResolution::ByArg::UniformRetVal:
      applyUniformRetValOpt(CSByConstantArg.second, "", ResByArg.Info);
      break;
    case WholeProgramDevirtResolution::ByArg::UniqueRetVal: {
      Constant *UniqueMemberAddr =
          importGlobal(Slot, CSByConstantArg.first, "unique_member");
      applyUniqueRetValOpt(CSByConstantArg.second, "", ResByArg.Info,
                           UniqueMemberAddr);
      break;
    }
    case WholeProgramDevirtResolution::ByArg::VirtualConstProp: {
      Constant *Byte = importConstant(Slot, CSByConstantArg.first, "byte",
                                      Int32Ty, ResByArg.Byte);
      Constant *Bit = importConstant(Slot, CSByConstantArg.first, "bit", Int8Ty,
                                     ResByArg.Bit);
      applyVirtualConstProp(CSByConstantArg.second, "", Byte, Bit);
      break;
    }
    default:
      break;
    }
  }

  if (Res.TheKind == WholeProgramDevirtResolution::BranchFunnel) {
    // The type of the function is irrelevant, because it's bitcast at calls
    // anyhow.
    Constant *JT = cast<Constant>(
        M.getOrInsertFunction(getGlobalName(Slot, {}, "branch_funnel"),
                              Type::getVoidTy(M.getContext()))
            .getCallee());
    bool IsExported = false;
    applyICallBranchFunnel(SlotInfo, JT, IsExported);
    assert(!IsExported);
  }
}

void DevirtModule::removeRedundantTypeTests() {
  auto True = ConstantInt::getTrue(M.getContext());
  for (auto &&U : NumUnsafeUsesForTypeTest) {
    if (U.second == 0) {
      U.first->replaceAllUsesWith(True);
      U.first->eraseFromParent();
    }
  }
}

ValueInfo
DevirtModule::lookUpFunctionValueInfo(Function *TheFn,
                                      ModuleSummaryIndex *ExportSummary) {
  assert((ExportSummary != nullptr) &&
         "Caller guarantees ExportSummary is not nullptr");

  const auto TheFnGUID = TheFn->getGUID();
  const auto TheFnGUIDWithExportedName = GlobalValue::getGUID(TheFn->getName());
  // Look up ValueInfo with the GUID in the current linkage.
  ValueInfo TheFnVI = ExportSummary->getValueInfo(TheFnGUID);
  // If no entry is found and GUID is different from GUID computed using
  // exported name, look up ValueInfo with the exported name unconditionally.
  // This is a fallback.
  //
  // The reason to have a fallback:
  // 1. LTO could enable global value internalization via
  // `enable-lto-internalization`.
  // 2. The GUID in ExportedSummary is computed using exported name.
  if ((!TheFnVI) && (TheFnGUID != TheFnGUIDWithExportedName)) {
    TheFnVI = ExportSummary->getValueInfo(TheFnGUIDWithExportedName);
  }
  return TheFnVI;
}

bool DevirtModule::mustBeUnreachableFunction(
    Function *const F, ModuleSummaryIndex *ExportSummary) {
  // First, learn unreachability by analyzing function IR.
  if (!F->isDeclaration()) {
    // A function must be unreachable if its entry block ends with an
    // 'unreachable'.
    return isa<UnreachableInst>(F->getEntryBlock().getTerminator());
  }
  // Learn unreachability from ExportSummary if ExportSummary is present.
  return ExportSummary &&
         ::mustBeUnreachableFunction(
             DevirtModule::lookUpFunctionValueInfo(F, ExportSummary));
}

bool DevirtModule::run() {
  // If only some of the modules were split, we cannot correctly perform
  // this transformation. We already checked for the presense of type tests
  // with partially split modules during the thin link, and would have emitted
  // an error if any were found, so here we can simply return.
  if ((ExportSummary && ExportSummary->partiallySplitLTOUnits()) ||
      (ImportSummary && ImportSummary->partiallySplitLTOUnits()))
    return false;

  Function *TypeTestFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_test));
  Function *TypeCheckedLoadFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_checked_load));
  Function *TypeCheckedLoadRelativeFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_checked_load_relative));
  Function *AssumeFunc = M.getFunction(Intrinsic::getName(Intrinsic::assume));

  // Normally if there are no users of the devirtualization intrinsics in the
  // module, this pass has nothing to do. But if we are exporting, we also need
  // to handle any users that appear only in the function summaries.
  if (!ExportSummary &&
      (!TypeTestFunc || TypeTestFunc->use_empty() || !AssumeFunc ||
       AssumeFunc->use_empty()) &&
      (!TypeCheckedLoadFunc || TypeCheckedLoadFunc->use_empty()) &&
      (!TypeCheckedLoadRelativeFunc ||
       TypeCheckedLoadRelativeFunc->use_empty()))
    return false;

  // Rebuild type metadata into a map for easy lookup.
  std::vector<VTableBits> Bits;
  DenseMap<Metadata *, std::set<TypeMemberInfo>> TypeIdMap;
  buildTypeIdentifierMap(Bits, TypeIdMap);

  if (TypeTestFunc && AssumeFunc)
    scanTypeTestUsers(TypeTestFunc, TypeIdMap);

  if (TypeCheckedLoadFunc)
    scanTypeCheckedLoadUsers(TypeCheckedLoadFunc);

  if (TypeCheckedLoadRelativeFunc)
    scanTypeCheckedLoadUsers(TypeCheckedLoadRelativeFunc);

  if (ImportSummary) {
    for (auto &S : CallSlots)
      importResolution(S.first, S.second);

    removeRedundantTypeTests();

    // We have lowered or deleted the type intrinsics, so we will no longer have
    // enough information to reason about the liveness of virtual function
    // pointers in GlobalDCE.
    for (GlobalVariable &GV : M.globals())
      GV.eraseMetadata(LLVMContext::MD_vcall_visibility);

    // The rest of the code is only necessary when exporting or during regular
    // LTO, so we are done.
    return true;
  }

  if (TypeIdMap.empty())
    return true;

  // Collect information from summary about which calls to try to devirtualize.
  if (ExportSummary) {
    DenseMap<GlobalValue::GUID, TinyPtrVector<Metadata *>> MetadataByGUID;
    for (auto &P : TypeIdMap) {
      if (auto *TypeId = dyn_cast<MDString>(P.first))
        MetadataByGUID[GlobalValue::getGUID(TypeId->getString())].push_back(
            TypeId);
    }

    for (auto &P : *ExportSummary) {
      for (auto &S : P.second.SummaryList) {
        auto *FS = dyn_cast<FunctionSummary>(S.get());
        if (!FS)
          continue;
        // FIXME: Only add live functions.
        for (FunctionSummary::VFuncId VF : FS->type_test_assume_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VF.GUID]) {
            CallSlots[{MD, VF.Offset}].CSInfo.addSummaryTypeTestAssumeUser(FS);
          }
        }
        for (FunctionSummary::VFuncId VF : FS->type_checked_load_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VF.GUID]) {
            CallSlots[{MD, VF.Offset}].CSInfo.addSummaryTypeCheckedLoadUser(FS);
          }
        }
        for (const FunctionSummary::ConstVCall &VC :
             FS->type_test_assume_const_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VC.VFunc.GUID]) {
            CallSlots[{MD, VC.VFunc.Offset}]
                .ConstCSInfo[VC.Args]
                .addSummaryTypeTestAssumeUser(FS);
          }
        }
        for (const FunctionSummary::ConstVCall &VC :
             FS->type_checked_load_const_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VC.VFunc.GUID]) {
            CallSlots[{MD, VC.VFunc.Offset}]
                .ConstCSInfo[VC.Args]
                .addSummaryTypeCheckedLoadUser(FS);
          }
        }
      }
    }
  }

  // For each (type, offset) pair:
  bool DidVirtualConstProp = false;
  std::map<std::string, GlobalValue *> DevirtTargets;
  for (auto &S : CallSlots) {
    // Search each of the members of the type identifier for the virtual
    // function implementation at offset S.first.ByteOffset, and add to
    // TargetsForSlot.
    std::vector<VirtualCallTarget> TargetsForSlot;
    WholeProgramDevirtResolution *Res = nullptr;
    const std::set<TypeMemberInfo> &TypeMemberInfos = TypeIdMap[S.first.TypeID];
    if (ExportSummary && isa<MDString>(S.first.TypeID) &&
        TypeMemberInfos.size())
      // For any type id used on a global's type metadata, create the type id
      // summary resolution regardless of whether we can devirtualize, so that
      // lower type tests knows the type id is not Unsat. If it was not used on
      // a global's type metadata, the TypeIdMap entry set will be empty, and
      // we don't want to create an entry (with the default Unknown type
      // resolution), which can prevent detection of the Unsat.
      Res = &ExportSummary
                 ->getOrInsertTypeIdSummary(
                     cast<MDString>(S.first.TypeID)->getString())
                 .WPDRes[S.first.ByteOffset];
    if (tryFindVirtualCallTargets(TargetsForSlot, TypeMemberInfos,
                                  S.first.ByteOffset, ExportSummary)) {

      if (!trySingleImplDevirt(ExportSummary, TargetsForSlot, S.second, Res)) {
        DidVirtualConstProp |=
            tryVirtualConstProp(TargetsForSlot, S.second, Res, S.first);

        tryICallBranchFunnel(TargetsForSlot, S.second, Res, S.first);
      }

      // Collect functions devirtualized at least for one call site for stats.
      if (RemarksEnabled || AreStatisticsEnabled())
        for (const auto &T : TargetsForSlot)
          if (T.WasDevirt)
            DevirtTargets[std::string(T.Fn->getName())] = T.Fn;
    }

    // CFI-specific: if we are exporting and any llvm.type.checked.load
    // intrinsics were *not* devirtualized, we need to add the resulting
    // llvm.type.test intrinsics to the function summaries so that the
    // LowerTypeTests pass will export them.
    if (ExportSummary && isa<MDString>(S.first.TypeID)) {
      auto GUID =
          GlobalValue::getGUID(cast<MDString>(S.first.TypeID)->getString());
      for (auto *FS : S.second.CSInfo.SummaryTypeCheckedLoadUsers)
        FS->addTypeTest(GUID);
      for (auto &CCS : S.second.ConstCSInfo)
        for (auto *FS : CCS.second.SummaryTypeCheckedLoadUsers)
          FS->addTypeTest(GUID);
    }
  }

  if (RemarksEnabled) {
    // Generate remarks for each devirtualized function.
    for (const auto &DT : DevirtTargets) {
      GlobalValue *GV = DT.second;
      auto F = dyn_cast<Function>(GV);
      if (!F) {
        auto A = dyn_cast<GlobalAlias>(GV);
        assert(A && isa<Function>(A->getAliasee()));
        F = dyn_cast<Function>(A->getAliasee());
        assert(F);
      }

      using namespace ore;
      OREGetter(F).emit(OptimizationRemark(DEBUG_TYPE, "Devirtualized", F)
                        << "devirtualized "
                        << NV("FunctionName", DT.first));
    }
  }

  NumDevirtTargets += DevirtTargets.size();

  removeRedundantTypeTests();

  // Rebuild each global we touched as part of virtual constant propagation to
  // include the before and after bytes.
  if (DidVirtualConstProp)
    for (VTableBits &B : Bits)
      rebuildGlobal(B);

  // We have lowered or deleted the type intrinsics, so we will no longer have
  // enough information to reason about the liveness of virtual function
  // pointers in GlobalDCE.
  for (GlobalVariable &GV : M.globals())
    GV.eraseMetadata(LLVMContext::MD_vcall_visibility);

  for (auto *CI : CallsWithPtrAuthBundleRemoved)
    CI->eraseFromParent();

  return true;
}

void DevirtIndex::run() {
  if (ExportSummary.typeIdCompatibleVtableMap().empty())
    return;

  DenseMap<GlobalValue::GUID, std::vector<StringRef>> NameByGUID;
  for (const auto &P : ExportSummary.typeIdCompatibleVtableMap()) {
    NameByGUID[GlobalValue::getGUID(P.first)].push_back(P.first);
    // Create the type id summary resolution regardlness of whether we can
    // devirtualize, so that lower type tests knows the type id is used on
    // a global and not Unsat. We do this here rather than in the loop over the
    // CallSlots, since that handling will only see type tests that directly
    // feed assumes, and we would miss any that aren't currently handled by WPD
    // (such as type tests that feed assumes via phis).
    ExportSummary.getOrInsertTypeIdSummary(P.first);
  }

  // Collect information from summary about which calls to try to devirtualize.
  for (auto &P : ExportSummary) {
    for (auto &S : P.second.SummaryList) {
      auto *FS = dyn_cast<FunctionSummary>(S.get());
      if (!FS)
        continue;
      // FIXME: Only add live functions.
      for (FunctionSummary::VFuncId VF : FS->type_test_assume_vcalls()) {
        for (StringRef Name : NameByGUID[VF.GUID]) {
          CallSlots[{Name, VF.Offset}].CSInfo.addSummaryTypeTestAssumeUser(FS);
        }
      }
      for (FunctionSummary::VFuncId VF : FS->type_checked_load_vcalls()) {
        for (StringRef Name : NameByGUID[VF.GUID]) {
          CallSlots[{Name, VF.Offset}].CSInfo.addSummaryTypeCheckedLoadUser(FS);
        }
      }
      for (const FunctionSummary::ConstVCall &VC :
           FS->type_test_assume_const_vcalls()) {
        for (StringRef Name : NameByGUID[VC.VFunc.GUID]) {
          CallSlots[{Name, VC.VFunc.Offset}]
              .ConstCSInfo[VC.Args]
              .addSummaryTypeTestAssumeUser(FS);
        }
      }
      for (const FunctionSummary::ConstVCall &VC :
           FS->type_checked_load_const_vcalls()) {
        for (StringRef Name : NameByGUID[VC.VFunc.GUID]) {
          CallSlots[{Name, VC.VFunc.Offset}]
              .ConstCSInfo[VC.Args]
              .addSummaryTypeCheckedLoadUser(FS);
        }
      }
    }
  }

  std::set<ValueInfo> DevirtTargets;
  // For each (type, offset) pair:
  for (auto &S : CallSlots) {
    // Search each of the members of the type identifier for the virtual
    // function implementation at offset S.first.ByteOffset, and add to
    // TargetsForSlot.
    std::vector<ValueInfo> TargetsForSlot;
    auto TidSummary = ExportSummary.getTypeIdCompatibleVtableSummary(S.first.TypeID);
    assert(TidSummary);
    // The type id summary would have been created while building the NameByGUID
    // map earlier.
    WholeProgramDevirtResolution *Res =
        &ExportSummary.getTypeIdSummary(S.first.TypeID)
             ->WPDRes[S.first.ByteOffset];
    if (tryFindVirtualCallTargets(TargetsForSlot, *TidSummary,
                                  S.first.ByteOffset)) {

      if (!trySingleImplDevirt(TargetsForSlot, S.first, S.second, Res,
                               DevirtTargets))
        continue;
    }
  }

  // Optionally have the thin link print message for each devirtualized
  // function.
  if (PrintSummaryDevirt)
    for (const auto &DT : DevirtTargets)
      errs() << "Devirtualized call to " << DT << "\n";

  NumDevirtTargets += DevirtTargets.size();
}
