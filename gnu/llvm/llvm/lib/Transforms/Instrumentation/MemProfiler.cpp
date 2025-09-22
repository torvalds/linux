//===- MemProfiler.cpp - memory allocation and access profiler ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler. Memory accesses are instrumented
// to increment the access count held in a shadow memory location, or
// alternatively to call into the runtime. Memory intrinsic calls (memmove,
// memcpy, memset) are changed to call the memory profiling runtime version
// instead.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/MemProfiler.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryProfileInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <map>
#include <set>

using namespace llvm;
using namespace llvm::memprof;

#define DEBUG_TYPE "memprof"

namespace llvm {
extern cl::opt<bool> PGOWarnMissing;
extern cl::opt<bool> NoPGOWarnMismatch;
extern cl::opt<bool> NoPGOWarnMismatchComdatWeak;
} // namespace llvm

constexpr int LLVM_MEM_PROFILER_VERSION = 1;

// Size of memory mapped to a single shadow location.
constexpr uint64_t DefaultMemGranularity = 64;

// Scale from granularity down to shadow size.
constexpr uint64_t DefaultShadowScale = 3;

constexpr char MemProfModuleCtorName[] = "memprof.module_ctor";
constexpr uint64_t MemProfCtorAndDtorPriority = 1;
// On Emscripten, the system needs more than one priorities for constructors.
constexpr uint64_t MemProfEmscriptenCtorAndDtorPriority = 50;
constexpr char MemProfInitName[] = "__memprof_init";
constexpr char MemProfVersionCheckNamePrefix[] =
    "__memprof_version_mismatch_check_v";

constexpr char MemProfShadowMemoryDynamicAddress[] =
    "__memprof_shadow_memory_dynamic_address";

constexpr char MemProfFilenameVar[] = "__memprof_profile_filename";

constexpr char MemProfHistogramFlagVar[] = "__memprof_histogram";

// Command-line flags.

static cl::opt<bool> ClInsertVersionCheck(
    "memprof-guard-against-version-mismatch",
    cl::desc("Guard against compiler/runtime version mismatch."), cl::Hidden,
    cl::init(true));

// This flag may need to be replaced with -f[no-]memprof-reads.
static cl::opt<bool> ClInstrumentReads("memprof-instrument-reads",
                                       cl::desc("instrument read instructions"),
                                       cl::Hidden, cl::init(true));

static cl::opt<bool>
    ClInstrumentWrites("memprof-instrument-writes",
                       cl::desc("instrument write instructions"), cl::Hidden,
                       cl::init(true));

static cl::opt<bool> ClInstrumentAtomics(
    "memprof-instrument-atomics",
    cl::desc("instrument atomic instructions (rmw, cmpxchg)"), cl::Hidden,
    cl::init(true));

static cl::opt<bool> ClUseCalls(
    "memprof-use-callbacks",
    cl::desc("Use callbacks instead of inline instrumentation sequences."),
    cl::Hidden, cl::init(false));

static cl::opt<std::string>
    ClMemoryAccessCallbackPrefix("memprof-memory-access-callback-prefix",
                                 cl::desc("Prefix for memory access callbacks"),
                                 cl::Hidden, cl::init("__memprof_"));

// These flags allow to change the shadow mapping.
// The shadow mapping looks like
//    Shadow = ((Mem & mask) >> scale) + offset

static cl::opt<int> ClMappingScale("memprof-mapping-scale",
                                   cl::desc("scale of memprof shadow mapping"),
                                   cl::Hidden, cl::init(DefaultShadowScale));

static cl::opt<int>
    ClMappingGranularity("memprof-mapping-granularity",
                         cl::desc("granularity of memprof shadow mapping"),
                         cl::Hidden, cl::init(DefaultMemGranularity));

static cl::opt<bool> ClStack("memprof-instrument-stack",
                             cl::desc("Instrument scalar stack variables"),
                             cl::Hidden, cl::init(false));

// Debug flags.

static cl::opt<int> ClDebug("memprof-debug", cl::desc("debug"), cl::Hidden,
                            cl::init(0));

static cl::opt<std::string> ClDebugFunc("memprof-debug-func", cl::Hidden,
                                        cl::desc("Debug func"));

static cl::opt<int> ClDebugMin("memprof-debug-min", cl::desc("Debug min inst"),
                               cl::Hidden, cl::init(-1));

static cl::opt<int> ClDebugMax("memprof-debug-max", cl::desc("Debug max inst"),
                               cl::Hidden, cl::init(-1));

// By default disable matching of allocation profiles onto operator new that
// already explicitly pass a hot/cold hint, since we don't currently
// override these hints anyway.
static cl::opt<bool> ClMemProfMatchHotColdNew(
    "memprof-match-hot-cold-new",
 cl::desc(
        "Match allocation profiles onto existing hot/cold operator new calls"),
    cl::Hidden, cl::init(false));

static cl::opt<bool> ClHistogram("memprof-histogram",
                                 cl::desc("Collect access count histograms"),
                                 cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClPrintMemProfMatchInfo("memprof-print-match-info",
                            cl::desc("Print matching stats for each allocation "
                                     "context in this module's profiles"),
                            cl::Hidden, cl::init(false));

extern cl::opt<bool> MemProfReportHintedSizes;

// Instrumentation statistics
STATISTIC(NumInstrumentedReads, "Number of instrumented reads");
STATISTIC(NumInstrumentedWrites, "Number of instrumented writes");
STATISTIC(NumSkippedStackReads, "Number of non-instrumented stack reads");
STATISTIC(NumSkippedStackWrites, "Number of non-instrumented stack writes");

// Matching statistics
STATISTIC(NumOfMemProfMissing, "Number of functions without memory profile.");
STATISTIC(NumOfMemProfMismatch,
          "Number of functions having mismatched memory profile hash.");
STATISTIC(NumOfMemProfFunc, "Number of functions having valid memory profile.");
STATISTIC(NumOfMemProfAllocContextProfiles,
          "Number of alloc contexts in memory profile.");
STATISTIC(NumOfMemProfCallSiteProfiles,
          "Number of callsites in memory profile.");
STATISTIC(NumOfMemProfMatchedAllocContexts,
          "Number of matched memory profile alloc contexts.");
STATISTIC(NumOfMemProfMatchedAllocs,
          "Number of matched memory profile allocs.");
STATISTIC(NumOfMemProfMatchedCallSites,
          "Number of matched memory profile callsites.");

namespace {

/// This struct defines the shadow mapping using the rule:
///   shadow = ((mem & mask) >> Scale) ADD DynamicShadowOffset.
struct ShadowMapping {
  ShadowMapping() {
    Scale = ClMappingScale;
    Granularity = ClMappingGranularity;
    Mask = ~(Granularity - 1);
  }

  int Scale;
  int Granularity;
  uint64_t Mask; // Computed as ~(Granularity-1)
};

static uint64_t getCtorAndDtorPriority(Triple &TargetTriple) {
  return TargetTriple.isOSEmscripten() ? MemProfEmscriptenCtorAndDtorPriority
                                       : MemProfCtorAndDtorPriority;
}

struct InterestingMemoryAccess {
  Value *Addr = nullptr;
  bool IsWrite;
  Type *AccessTy;
  Value *MaybeMask = nullptr;
};

/// Instrument the code in module to profile memory accesses.
class MemProfiler {
public:
  MemProfiler(Module &M) {
    C = &(M.getContext());
    LongSize = M.getDataLayout().getPointerSizeInBits();
    IntptrTy = Type::getIntNTy(*C, LongSize);
    PtrTy = PointerType::getUnqual(*C);
  }

  /// If it is an interesting memory access, populate information
  /// about the access and return a InterestingMemoryAccess struct.
  /// Otherwise return std::nullopt.
  std::optional<InterestingMemoryAccess>
  isInterestingMemoryAccess(Instruction *I) const;

  void instrumentMop(Instruction *I, const DataLayout &DL,
                     InterestingMemoryAccess &Access);
  void instrumentAddress(Instruction *OrigIns, Instruction *InsertBefore,
                         Value *Addr, bool IsWrite);
  void instrumentMaskedLoadOrStore(const DataLayout &DL, Value *Mask,
                                   Instruction *I, Value *Addr, Type *AccessTy,
                                   bool IsWrite);
  void instrumentMemIntrinsic(MemIntrinsic *MI);
  Value *memToShadow(Value *Shadow, IRBuilder<> &IRB);
  bool instrumentFunction(Function &F);
  bool maybeInsertMemProfInitAtFunctionEntry(Function &F);
  bool insertDynamicShadowAtFunctionEntry(Function &F);

private:
  void initializeCallbacks(Module &M);

  LLVMContext *C;
  int LongSize;
  Type *IntptrTy;
  PointerType *PtrTy;
  ShadowMapping Mapping;

  // These arrays is indexed by AccessIsWrite
  FunctionCallee MemProfMemoryAccessCallback[2];

  FunctionCallee MemProfMemmove, MemProfMemcpy, MemProfMemset;
  Value *DynamicShadowOffset = nullptr;
};

class ModuleMemProfiler {
public:
  ModuleMemProfiler(Module &M) { TargetTriple = Triple(M.getTargetTriple()); }

  bool instrumentModule(Module &);

private:
  Triple TargetTriple;
  ShadowMapping Mapping;
  Function *MemProfCtorFunction = nullptr;
};

} // end anonymous namespace

MemProfilerPass::MemProfilerPass() = default;

PreservedAnalyses MemProfilerPass::run(Function &F,
                                       AnalysisManager<Function> &AM) {
  Module &M = *F.getParent();
  MemProfiler Profiler(M);
  if (Profiler.instrumentFunction(F))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

ModuleMemProfilerPass::ModuleMemProfilerPass() = default;

PreservedAnalyses ModuleMemProfilerPass::run(Module &M,
                                             AnalysisManager<Module> &AM) {

  assert((!ClHistogram || (ClHistogram && ClUseCalls)) &&
         "Cannot use -memprof-histogram without Callbacks. Set "
         "memprof-use-callbacks");

  ModuleMemProfiler Profiler(M);
  if (Profiler.instrumentModule(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

Value *MemProfiler::memToShadow(Value *Shadow, IRBuilder<> &IRB) {
  // (Shadow & mask) >> scale
  Shadow = IRB.CreateAnd(Shadow, Mapping.Mask);
  Shadow = IRB.CreateLShr(Shadow, Mapping.Scale);
  // (Shadow >> scale) | offset
  assert(DynamicShadowOffset);
  return IRB.CreateAdd(Shadow, DynamicShadowOffset);
}

// Instrument memset/memmove/memcpy
void MemProfiler::instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> IRB(MI);
  if (isa<MemTransferInst>(MI)) {
    IRB.CreateCall(isa<MemMoveInst>(MI) ? MemProfMemmove : MemProfMemcpy,
                   {MI->getOperand(0), MI->getOperand(1),
                    IRB.CreateIntCast(MI->getOperand(2), IntptrTy, false)});
  } else if (isa<MemSetInst>(MI)) {
    IRB.CreateCall(
        MemProfMemset,
        {MI->getOperand(0),
         IRB.CreateIntCast(MI->getOperand(1), IRB.getInt32Ty(), false),
         IRB.CreateIntCast(MI->getOperand(2), IntptrTy, false)});
  }
  MI->eraseFromParent();
}

std::optional<InterestingMemoryAccess>
MemProfiler::isInterestingMemoryAccess(Instruction *I) const {
  // Do not instrument the load fetching the dynamic shadow address.
  if (DynamicShadowOffset == I)
    return std::nullopt;

  InterestingMemoryAccess Access;

  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    if (!ClInstrumentReads)
      return std::nullopt;
    Access.IsWrite = false;
    Access.AccessTy = LI->getType();
    Access.Addr = LI->getPointerOperand();
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    if (!ClInstrumentWrites)
      return std::nullopt;
    Access.IsWrite = true;
    Access.AccessTy = SI->getValueOperand()->getType();
    Access.Addr = SI->getPointerOperand();
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    if (!ClInstrumentAtomics)
      return std::nullopt;
    Access.IsWrite = true;
    Access.AccessTy = RMW->getValOperand()->getType();
    Access.Addr = RMW->getPointerOperand();
  } else if (AtomicCmpXchgInst *XCHG = dyn_cast<AtomicCmpXchgInst>(I)) {
    if (!ClInstrumentAtomics)
      return std::nullopt;
    Access.IsWrite = true;
    Access.AccessTy = XCHG->getCompareOperand()->getType();
    Access.Addr = XCHG->getPointerOperand();
  } else if (auto *CI = dyn_cast<CallInst>(I)) {
    auto *F = CI->getCalledFunction();
    if (F && (F->getIntrinsicID() == Intrinsic::masked_load ||
              F->getIntrinsicID() == Intrinsic::masked_store)) {
      unsigned OpOffset = 0;
      if (F->getIntrinsicID() == Intrinsic::masked_store) {
        if (!ClInstrumentWrites)
          return std::nullopt;
        // Masked store has an initial operand for the value.
        OpOffset = 1;
        Access.AccessTy = CI->getArgOperand(0)->getType();
        Access.IsWrite = true;
      } else {
        if (!ClInstrumentReads)
          return std::nullopt;
        Access.AccessTy = CI->getType();
        Access.IsWrite = false;
      }

      auto *BasePtr = CI->getOperand(0 + OpOffset);
      Access.MaybeMask = CI->getOperand(2 + OpOffset);
      Access.Addr = BasePtr;
    }
  }

  if (!Access.Addr)
    return std::nullopt;

  // Do not instrument accesses from different address spaces; we cannot deal
  // with them.
  Type *PtrTy = cast<PointerType>(Access.Addr->getType()->getScalarType());
  if (PtrTy->getPointerAddressSpace() != 0)
    return std::nullopt;

  // Ignore swifterror addresses.
  // swifterror memory addresses are mem2reg promoted by instruction
  // selection. As such they cannot have regular uses like an instrumentation
  // function and it makes no sense to track them as memory.
  if (Access.Addr->isSwiftError())
    return std::nullopt;

  // Peel off GEPs and BitCasts.
  auto *Addr = Access.Addr->stripInBoundsOffsets();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    // Do not instrument PGO counter updates.
    if (GV->hasSection()) {
      StringRef SectionName = GV->getSection();
      // Check if the global is in the PGO counters section.
      auto OF = Triple(I->getModule()->getTargetTriple()).getObjectFormat();
      if (SectionName.ends_with(
              getInstrProfSectionName(IPSK_cnts, OF, /*AddSegmentInfo=*/false)))
        return std::nullopt;
    }

    // Do not instrument accesses to LLVM internal variables.
    if (GV->getName().starts_with("__llvm"))
      return std::nullopt;
  }

  return Access;
}

void MemProfiler::instrumentMaskedLoadOrStore(const DataLayout &DL, Value *Mask,
                                              Instruction *I, Value *Addr,
                                              Type *AccessTy, bool IsWrite) {
  auto *VTy = cast<FixedVectorType>(AccessTy);
  unsigned Num = VTy->getNumElements();
  auto *Zero = ConstantInt::get(IntptrTy, 0);
  for (unsigned Idx = 0; Idx < Num; ++Idx) {
    Value *InstrumentedAddress = nullptr;
    Instruction *InsertBefore = I;
    if (auto *Vector = dyn_cast<ConstantVector>(Mask)) {
      // dyn_cast as we might get UndefValue
      if (auto *Masked = dyn_cast<ConstantInt>(Vector->getOperand(Idx))) {
        if (Masked->isZero())
          // Mask is constant false, so no instrumentation needed.
          continue;
        // If we have a true or undef value, fall through to instrumentAddress.
        // with InsertBefore == I
      }
    } else {
      IRBuilder<> IRB(I);
      Value *MaskElem = IRB.CreateExtractElement(Mask, Idx);
      Instruction *ThenTerm = SplitBlockAndInsertIfThen(MaskElem, I, false);
      InsertBefore = ThenTerm;
    }

    IRBuilder<> IRB(InsertBefore);
    InstrumentedAddress =
        IRB.CreateGEP(VTy, Addr, {Zero, ConstantInt::get(IntptrTy, Idx)});
    instrumentAddress(I, InsertBefore, InstrumentedAddress, IsWrite);
  }
}

void MemProfiler::instrumentMop(Instruction *I, const DataLayout &DL,
                                InterestingMemoryAccess &Access) {
  // Skip instrumentation of stack accesses unless requested.
  if (!ClStack && isa<AllocaInst>(getUnderlyingObject(Access.Addr))) {
    if (Access.IsWrite)
      ++NumSkippedStackWrites;
    else
      ++NumSkippedStackReads;
    return;
  }

  if (Access.IsWrite)
    NumInstrumentedWrites++;
  else
    NumInstrumentedReads++;

  if (Access.MaybeMask) {
    instrumentMaskedLoadOrStore(DL, Access.MaybeMask, I, Access.Addr,
                                Access.AccessTy, Access.IsWrite);
  } else {
    // Since the access counts will be accumulated across the entire allocation,
    // we only update the shadow access count for the first location and thus
    // don't need to worry about alignment and type size.
    instrumentAddress(I, I, Access.Addr, Access.IsWrite);
  }
}

void MemProfiler::instrumentAddress(Instruction *OrigIns,
                                    Instruction *InsertBefore, Value *Addr,
                                    bool IsWrite) {
  IRBuilder<> IRB(InsertBefore);
  Value *AddrLong = IRB.CreatePointerCast(Addr, IntptrTy);

  if (ClUseCalls) {
    IRB.CreateCall(MemProfMemoryAccessCallback[IsWrite], AddrLong);
    return;
  }

  // Create an inline sequence to compute shadow location, and increment the
  // value by one.
  Type *ShadowTy = Type::getInt64Ty(*C);
  Type *ShadowPtrTy = PointerType::get(ShadowTy, 0);
  Value *ShadowPtr = memToShadow(AddrLong, IRB);
  Value *ShadowAddr = IRB.CreateIntToPtr(ShadowPtr, ShadowPtrTy);
  Value *ShadowValue = IRB.CreateLoad(ShadowTy, ShadowAddr);
  Value *Inc = ConstantInt::get(Type::getInt64Ty(*C), 1);
  ShadowValue = IRB.CreateAdd(ShadowValue, Inc);
  IRB.CreateStore(ShadowValue, ShadowAddr);
}

// Create the variable for the profile file name.
void createProfileFileNameVar(Module &M) {
  const MDString *MemProfFilename =
      dyn_cast_or_null<MDString>(M.getModuleFlag("MemProfProfileFilename"));
  if (!MemProfFilename)
    return;
  assert(!MemProfFilename->getString().empty() &&
         "Unexpected MemProfProfileFilename metadata with empty string");
  Constant *ProfileNameConst = ConstantDataArray::getString(
      M.getContext(), MemProfFilename->getString(), true);
  GlobalVariable *ProfileNameVar = new GlobalVariable(
      M, ProfileNameConst->getType(), /*isConstant=*/true,
      GlobalValue::WeakAnyLinkage, ProfileNameConst, MemProfFilenameVar);
  Triple TT(M.getTargetTriple());
  if (TT.supportsCOMDAT()) {
    ProfileNameVar->setLinkage(GlobalValue::ExternalLinkage);
    ProfileNameVar->setComdat(M.getOrInsertComdat(MemProfFilenameVar));
  }
}

// Set MemprofHistogramFlag as a Global veriable in IR. This makes it accessible
// to the runtime, changing shadow count behavior.
void createMemprofHistogramFlagVar(Module &M) {
  const StringRef VarName(MemProfHistogramFlagVar);
  Type *IntTy1 = Type::getInt1Ty(M.getContext());
  auto MemprofHistogramFlag = new GlobalVariable(
      M, IntTy1, true, GlobalValue::WeakAnyLinkage,
      Constant::getIntegerValue(IntTy1, APInt(1, ClHistogram)), VarName);
  Triple TT(M.getTargetTriple());
  if (TT.supportsCOMDAT()) {
    MemprofHistogramFlag->setLinkage(GlobalValue::ExternalLinkage);
    MemprofHistogramFlag->setComdat(M.getOrInsertComdat(VarName));
  }
  appendToCompilerUsed(M, MemprofHistogramFlag);
}

bool ModuleMemProfiler::instrumentModule(Module &M) {

  // Create a module constructor.
  std::string MemProfVersion = std::to_string(LLVM_MEM_PROFILER_VERSION);
  std::string VersionCheckName =
      ClInsertVersionCheck ? (MemProfVersionCheckNamePrefix + MemProfVersion)
                           : "";
  std::tie(MemProfCtorFunction, std::ignore) =
      createSanitizerCtorAndInitFunctions(M, MemProfModuleCtorName,
                                          MemProfInitName, /*InitArgTypes=*/{},
                                          /*InitArgs=*/{}, VersionCheckName);

  const uint64_t Priority = getCtorAndDtorPriority(TargetTriple);
  appendToGlobalCtors(M, MemProfCtorFunction, Priority);

  createProfileFileNameVar(M);

  createMemprofHistogramFlagVar(M);

  return true;
}

void MemProfiler::initializeCallbacks(Module &M) {
  IRBuilder<> IRB(*C);

  for (size_t AccessIsWrite = 0; AccessIsWrite <= 1; AccessIsWrite++) {
    const std::string TypeStr = AccessIsWrite ? "store" : "load";
    const std::string HistPrefix = ClHistogram ? "hist_" : "";

    SmallVector<Type *, 2> Args1{1, IntptrTy};
    MemProfMemoryAccessCallback[AccessIsWrite] = M.getOrInsertFunction(
        ClMemoryAccessCallbackPrefix + HistPrefix + TypeStr,
        FunctionType::get(IRB.getVoidTy(), Args1, false));
  }
  MemProfMemmove = M.getOrInsertFunction(
      ClMemoryAccessCallbackPrefix + "memmove", PtrTy, PtrTy, PtrTy, IntptrTy);
  MemProfMemcpy = M.getOrInsertFunction(ClMemoryAccessCallbackPrefix + "memcpy",
                                        PtrTy, PtrTy, PtrTy, IntptrTy);
  MemProfMemset =
      M.getOrInsertFunction(ClMemoryAccessCallbackPrefix + "memset", PtrTy,
                            PtrTy, IRB.getInt32Ty(), IntptrTy);
}

bool MemProfiler::maybeInsertMemProfInitAtFunctionEntry(Function &F) {
  // For each NSObject descendant having a +load method, this method is invoked
  // by the ObjC runtime before any of the static constructors is called.
  // Therefore we need to instrument such methods with a call to __memprof_init
  // at the beginning in order to initialize our runtime before any access to
  // the shadow memory.
  // We cannot just ignore these methods, because they may call other
  // instrumented functions.
  if (F.getName().contains(" load]")) {
    FunctionCallee MemProfInitFunction =
        declareSanitizerInitFunction(*F.getParent(), MemProfInitName, {});
    IRBuilder<> IRB(&F.front(), F.front().begin());
    IRB.CreateCall(MemProfInitFunction, {});
    return true;
  }
  return false;
}

bool MemProfiler::insertDynamicShadowAtFunctionEntry(Function &F) {
  IRBuilder<> IRB(&F.front().front());
  Value *GlobalDynamicAddress = F.getParent()->getOrInsertGlobal(
      MemProfShadowMemoryDynamicAddress, IntptrTy);
  if (F.getParent()->getPICLevel() == PICLevel::NotPIC)
    cast<GlobalVariable>(GlobalDynamicAddress)->setDSOLocal(true);
  DynamicShadowOffset = IRB.CreateLoad(IntptrTy, GlobalDynamicAddress);
  return true;
}

bool MemProfiler::instrumentFunction(Function &F) {
  if (F.getLinkage() == GlobalValue::AvailableExternallyLinkage)
    return false;
  if (ClDebugFunc == F.getName())
    return false;
  if (F.getName().starts_with("__memprof_"))
    return false;

  bool FunctionModified = false;

  // If needed, insert __memprof_init.
  // This function needs to be called even if the function body is not
  // instrumented.
  if (maybeInsertMemProfInitAtFunctionEntry(F))
    FunctionModified = true;

  LLVM_DEBUG(dbgs() << "MEMPROF instrumenting:\n" << F << "\n");

  initializeCallbacks(*F.getParent());

  SmallVector<Instruction *, 16> ToInstrument;

  // Fill the set of memory operations to instrument.
  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (isInterestingMemoryAccess(&Inst) || isa<MemIntrinsic>(Inst))
        ToInstrument.push_back(&Inst);
    }
  }

  if (ToInstrument.empty()) {
    LLVM_DEBUG(dbgs() << "MEMPROF done instrumenting: " << FunctionModified
                      << " " << F << "\n");

    return FunctionModified;
  }

  FunctionModified |= insertDynamicShadowAtFunctionEntry(F);

  int NumInstrumented = 0;
  for (auto *Inst : ToInstrument) {
    if (ClDebugMin < 0 || ClDebugMax < 0 ||
        (NumInstrumented >= ClDebugMin && NumInstrumented <= ClDebugMax)) {
      std::optional<InterestingMemoryAccess> Access =
          isInterestingMemoryAccess(Inst);
      if (Access)
        instrumentMop(Inst, F.getDataLayout(), *Access);
      else
        instrumentMemIntrinsic(cast<MemIntrinsic>(Inst));
    }
    NumInstrumented++;
  }

  if (NumInstrumented > 0)
    FunctionModified = true;

  LLVM_DEBUG(dbgs() << "MEMPROF done instrumenting: " << FunctionModified << " "
                    << F << "\n");

  return FunctionModified;
}

static void addCallsiteMetadata(Instruction &I,
                                std::vector<uint64_t> &InlinedCallStack,
                                LLVMContext &Ctx) {
  I.setMetadata(LLVMContext::MD_callsite,
                buildCallstackMetadata(InlinedCallStack, Ctx));
}

static uint64_t computeStackId(GlobalValue::GUID Function, uint32_t LineOffset,
                               uint32_t Column) {
  llvm::HashBuilder<llvm::TruncatedBLAKE3<8>, llvm::endianness::little>
      HashBuilder;
  HashBuilder.add(Function, LineOffset, Column);
  llvm::BLAKE3Result<8> Hash = HashBuilder.final();
  uint64_t Id;
  std::memcpy(&Id, Hash.data(), sizeof(Hash));
  return Id;
}

static uint64_t computeStackId(const memprof::Frame &Frame) {
  return computeStackId(Frame.Function, Frame.LineOffset, Frame.Column);
}

// Helper to generate a single hash id for a given callstack, used for emitting
// matching statistics and useful for uniquing such statistics across modules.
static uint64_t
computeFullStackId(const std::vector<memprof::Frame> &CallStack) {
  llvm::HashBuilder<llvm::TruncatedBLAKE3<8>, llvm::endianness::little>
      HashBuilder;
  for (auto &F : CallStack)
    HashBuilder.add(F.Function, F.LineOffset, F.Column);
  llvm::BLAKE3Result<8> Hash = HashBuilder.final();
  uint64_t Id;
  std::memcpy(&Id, Hash.data(), sizeof(Hash));
  return Id;
}

static AllocationType addCallStack(CallStackTrie &AllocTrie,
                                   const AllocationInfo *AllocInfo) {
  SmallVector<uint64_t> StackIds;
  for (const auto &StackFrame : AllocInfo->CallStack)
    StackIds.push_back(computeStackId(StackFrame));
  auto AllocType = getAllocType(AllocInfo->Info.getTotalLifetimeAccessDensity(),
                                AllocInfo->Info.getAllocCount(),
                                AllocInfo->Info.getTotalLifetime());
  uint64_t TotalSize = 0;
  if (MemProfReportHintedSizes) {
    TotalSize = AllocInfo->Info.getTotalSize();
    assert(TotalSize);
  }
  AllocTrie.addCallStack(AllocType, StackIds, TotalSize);
  return AllocType;
}

// Helper to compare the InlinedCallStack computed from an instruction's debug
// info to a list of Frames from profile data (either the allocation data or a
// callsite). For callsites, the StartIndex to use in the Frame array may be
// non-zero.
static bool
stackFrameIncludesInlinedCallStack(ArrayRef<Frame> ProfileCallStack,
                                   ArrayRef<uint64_t> InlinedCallStack,
                                   unsigned StartIndex = 0) {
  auto StackFrame = ProfileCallStack.begin() + StartIndex;
  auto InlCallStackIter = InlinedCallStack.begin();
  for (; StackFrame != ProfileCallStack.end() &&
         InlCallStackIter != InlinedCallStack.end();
       ++StackFrame, ++InlCallStackIter) {
    uint64_t StackId = computeStackId(*StackFrame);
    if (StackId != *InlCallStackIter)
      return false;
  }
  // Return true if we found and matched all stack ids from the call
  // instruction.
  return InlCallStackIter == InlinedCallStack.end();
}

static bool isNewWithHotColdVariant(Function *Callee,
                                    const TargetLibraryInfo &TLI) {
  if (!Callee)
    return false;
  LibFunc Func;
  if (!TLI.getLibFunc(*Callee, Func))
    return false;
  switch (Func) {
  case LibFunc_Znwm:
  case LibFunc_ZnwmRKSt9nothrow_t:
  case LibFunc_ZnwmSt11align_val_t:
  case LibFunc_ZnwmSt11align_val_tRKSt9nothrow_t:
  case LibFunc_Znam:
  case LibFunc_ZnamRKSt9nothrow_t:
  case LibFunc_ZnamSt11align_val_t:
  case LibFunc_ZnamSt11align_val_tRKSt9nothrow_t:
    return true;
  case LibFunc_Znwm12__hot_cold_t:
  case LibFunc_ZnwmRKSt9nothrow_t12__hot_cold_t:
  case LibFunc_ZnwmSt11align_val_t12__hot_cold_t:
  case LibFunc_ZnwmSt11align_val_tRKSt9nothrow_t12__hot_cold_t:
  case LibFunc_Znam12__hot_cold_t:
  case LibFunc_ZnamRKSt9nothrow_t12__hot_cold_t:
  case LibFunc_ZnamSt11align_val_t12__hot_cold_t:
  case LibFunc_ZnamSt11align_val_tRKSt9nothrow_t12__hot_cold_t:
    return ClMemProfMatchHotColdNew;
  default:
    return false;
  }
}

struct AllocMatchInfo {
  uint64_t TotalSize = 0;
  AllocationType AllocType = AllocationType::None;
  bool Matched = false;
};

static void
readMemprof(Module &M, Function &F, IndexedInstrProfReader *MemProfReader,
            const TargetLibraryInfo &TLI,
            std::map<uint64_t, AllocMatchInfo> &FullStackIdToAllocMatchInfo) {
  auto &Ctx = M.getContext();
  // Previously we used getIRPGOFuncName() here. If F is local linkage,
  // getIRPGOFuncName() returns FuncName with prefix 'FileName;'. But
  // llvm-profdata uses FuncName in dwarf to create GUID which doesn't
  // contain FileName's prefix. It caused local linkage function can't
  // find MemProfRecord. So we use getName() now.
  // 'unique-internal-linkage-names' can make MemProf work better for local
  // linkage function.
  auto FuncName = F.getName();
  auto FuncGUID = Function::getGUID(FuncName);
  std::optional<memprof::MemProfRecord> MemProfRec;
  auto Err = MemProfReader->getMemProfRecord(FuncGUID).moveInto(MemProfRec);
  if (Err) {
    handleAllErrors(std::move(Err), [&](const InstrProfError &IPE) {
      auto Err = IPE.get();
      bool SkipWarning = false;
      LLVM_DEBUG(dbgs() << "Error in reading profile for Func " << FuncName
                        << ": ");
      if (Err == instrprof_error::unknown_function) {
        NumOfMemProfMissing++;
        SkipWarning = !PGOWarnMissing;
        LLVM_DEBUG(dbgs() << "unknown function");
      } else if (Err == instrprof_error::hash_mismatch) {
        NumOfMemProfMismatch++;
        SkipWarning =
            NoPGOWarnMismatch ||
            (NoPGOWarnMismatchComdatWeak &&
             (F.hasComdat() ||
              F.getLinkage() == GlobalValue::AvailableExternallyLinkage));
        LLVM_DEBUG(dbgs() << "hash mismatch (skip=" << SkipWarning << ")");
      }

      if (SkipWarning)
        return;

      std::string Msg = (IPE.message() + Twine(" ") + F.getName().str() +
                         Twine(" Hash = ") + std::to_string(FuncGUID))
                            .str();

      Ctx.diagnose(
          DiagnosticInfoPGOProfile(M.getName().data(), Msg, DS_Warning));
    });
    return;
  }

  NumOfMemProfFunc++;

  // Detect if there are non-zero column numbers in the profile. If not,
  // treat all column numbers as 0 when matching (i.e. ignore any non-zero
  // columns in the IR). The profiled binary might have been built with
  // column numbers disabled, for example.
  bool ProfileHasColumns = false;

  // Build maps of the location hash to all profile data with that leaf location
  // (allocation info and the callsites).
  std::map<uint64_t, std::set<const AllocationInfo *>> LocHashToAllocInfo;
  // For the callsites we need to record the index of the associated frame in
  // the frame array (see comments below where the map entries are added).
  std::map<uint64_t, std::set<std::pair<const std::vector<Frame> *, unsigned>>>
      LocHashToCallSites;
  for (auto &AI : MemProfRec->AllocSites) {
    NumOfMemProfAllocContextProfiles++;
    // Associate the allocation info with the leaf frame. The later matching
    // code will match any inlined call sequences in the IR with a longer prefix
    // of call stack frames.
    uint64_t StackId = computeStackId(AI.CallStack[0]);
    LocHashToAllocInfo[StackId].insert(&AI);
    ProfileHasColumns |= AI.CallStack[0].Column;
  }
  for (auto &CS : MemProfRec->CallSites) {
    NumOfMemProfCallSiteProfiles++;
    // Need to record all frames from leaf up to and including this function,
    // as any of these may or may not have been inlined at this point.
    unsigned Idx = 0;
    for (auto &StackFrame : CS) {
      uint64_t StackId = computeStackId(StackFrame);
      LocHashToCallSites[StackId].insert(std::make_pair(&CS, Idx++));
      ProfileHasColumns |= StackFrame.Column;
      // Once we find this function, we can stop recording.
      if (StackFrame.Function == FuncGUID)
        break;
    }
    assert(Idx <= CS.size() && CS[Idx - 1].Function == FuncGUID);
  }

  auto GetOffset = [](const DILocation *DIL) {
    return (DIL->getLine() - DIL->getScope()->getSubprogram()->getLine()) &
           0xffff;
  };

  // Now walk the instructions, looking up the associated profile data using
  // debug locations.
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (I.isDebugOrPseudoInst())
        continue;
      // We are only interested in calls (allocation or interior call stack
      // context calls).
      auto *CI = dyn_cast<CallBase>(&I);
      if (!CI)
        continue;
      auto *CalledFunction = CI->getCalledFunction();
      if (CalledFunction && CalledFunction->isIntrinsic())
        continue;
      // List of call stack ids computed from the location hashes on debug
      // locations (leaf to inlined at root).
      std::vector<uint64_t> InlinedCallStack;
      // Was the leaf location found in one of the profile maps?
      bool LeafFound = false;
      // If leaf was found in a map, iterators pointing to its location in both
      // of the maps. It might exist in neither, one, or both (the latter case
      // can happen because we don't currently have discriminators to
      // distinguish the case when a single line/col maps to both an allocation
      // and another callsite).
      std::map<uint64_t, std::set<const AllocationInfo *>>::iterator
          AllocInfoIter;
      std::map<uint64_t, std::set<std::pair<const std::vector<Frame> *,
                                            unsigned>>>::iterator CallSitesIter;
      for (const DILocation *DIL = I.getDebugLoc(); DIL != nullptr;
           DIL = DIL->getInlinedAt()) {
        // Use C++ linkage name if possible. Need to compile with
        // -fdebug-info-for-profiling to get linkage name.
        StringRef Name = DIL->getScope()->getSubprogram()->getLinkageName();
        if (Name.empty())
          Name = DIL->getScope()->getSubprogram()->getName();
        auto CalleeGUID = Function::getGUID(Name);
        auto StackId = computeStackId(CalleeGUID, GetOffset(DIL),
                                      ProfileHasColumns ? DIL->getColumn() : 0);
        // Check if we have found the profile's leaf frame. If yes, collect
        // the rest of the call's inlined context starting here. If not, see if
        // we find a match further up the inlined context (in case the profile
        // was missing debug frames at the leaf).
        if (!LeafFound) {
          AllocInfoIter = LocHashToAllocInfo.find(StackId);
          CallSitesIter = LocHashToCallSites.find(StackId);
          if (AllocInfoIter != LocHashToAllocInfo.end() ||
              CallSitesIter != LocHashToCallSites.end())
            LeafFound = true;
        }
        if (LeafFound)
          InlinedCallStack.push_back(StackId);
      }
      // If leaf not in either of the maps, skip inst.
      if (!LeafFound)
        continue;

      // First add !memprof metadata from allocation info, if we found the
      // instruction's leaf location in that map, and if the rest of the
      // instruction's locations match the prefix Frame locations on an
      // allocation context with the same leaf.
      if (AllocInfoIter != LocHashToAllocInfo.end()) {
        // Only consider allocations via new, to reduce unnecessary metadata,
        // since those are the only allocations that will be targeted initially.
        if (!isNewWithHotColdVariant(CI->getCalledFunction(), TLI))
          continue;
        // We may match this instruction's location list to multiple MIB
        // contexts. Add them to a Trie specialized for trimming the contexts to
        // the minimal needed to disambiguate contexts with unique behavior.
        CallStackTrie AllocTrie;
        for (auto *AllocInfo : AllocInfoIter->second) {
          // Check the full inlined call stack against this one.
          // If we found and thus matched all frames on the call, include
          // this MIB.
          if (stackFrameIncludesInlinedCallStack(AllocInfo->CallStack,
                                                 InlinedCallStack)) {
            NumOfMemProfMatchedAllocContexts++;
            auto AllocType = addCallStack(AllocTrie, AllocInfo);
            // Record information about the allocation if match info printing
            // was requested.
            if (ClPrintMemProfMatchInfo) {
              auto FullStackId = computeFullStackId(AllocInfo->CallStack);
              FullStackIdToAllocMatchInfo[FullStackId] = {
                  AllocInfo->Info.getTotalSize(), AllocType, /*Matched=*/true};
            }
          }
        }
        // We might not have matched any to the full inlined call stack.
        // But if we did, create and attach metadata, or a function attribute if
        // all contexts have identical profiled behavior.
        if (!AllocTrie.empty()) {
          NumOfMemProfMatchedAllocs++;
          // MemprofMDAttached will be false if a function attribute was
          // attached.
          bool MemprofMDAttached = AllocTrie.buildAndAttachMIBMetadata(CI);
          assert(MemprofMDAttached == I.hasMetadata(LLVMContext::MD_memprof));
          if (MemprofMDAttached) {
            // Add callsite metadata for the instruction's location list so that
            // it simpler later on to identify which part of the MIB contexts
            // are from this particular instruction (including during inlining,
            // when the callsite metadata will be updated appropriately).
            // FIXME: can this be changed to strip out the matching stack
            // context ids from the MIB contexts and not add any callsite
            // metadata here to save space?
            addCallsiteMetadata(I, InlinedCallStack, Ctx);
          }
        }
        continue;
      }

      // Otherwise, add callsite metadata. If we reach here then we found the
      // instruction's leaf location in the callsites map and not the allocation
      // map.
      assert(CallSitesIter != LocHashToCallSites.end());
      for (auto CallStackIdx : CallSitesIter->second) {
        // If we found and thus matched all frames on the call, create and
        // attach call stack metadata.
        if (stackFrameIncludesInlinedCallStack(
                *CallStackIdx.first, InlinedCallStack, CallStackIdx.second)) {
          NumOfMemProfMatchedCallSites++;
          addCallsiteMetadata(I, InlinedCallStack, Ctx);
          // Only need to find one with a matching call stack and add a single
          // callsite metadata.
          break;
        }
      }
    }
  }
}

MemProfUsePass::MemProfUsePass(std::string MemoryProfileFile,
                               IntrusiveRefCntPtr<vfs::FileSystem> FS)
    : MemoryProfileFileName(MemoryProfileFile), FS(FS) {
  if (!FS)
    this->FS = vfs::getRealFileSystem();
}

PreservedAnalyses MemProfUsePass::run(Module &M, ModuleAnalysisManager &AM) {
  LLVM_DEBUG(dbgs() << "Read in memory profile:");
  auto &Ctx = M.getContext();
  auto ReaderOrErr = IndexedInstrProfReader::create(MemoryProfileFileName, *FS);
  if (Error E = ReaderOrErr.takeError()) {
    handleAllErrors(std::move(E), [&](const ErrorInfoBase &EI) {
      Ctx.diagnose(
          DiagnosticInfoPGOProfile(MemoryProfileFileName.data(), EI.message()));
    });
    return PreservedAnalyses::all();
  }

  std::unique_ptr<IndexedInstrProfReader> MemProfReader =
      std::move(ReaderOrErr.get());
  if (!MemProfReader) {
    Ctx.diagnose(DiagnosticInfoPGOProfile(
        MemoryProfileFileName.data(), StringRef("Cannot get MemProfReader")));
    return PreservedAnalyses::all();
  }

  if (!MemProfReader->hasMemoryProfile()) {
    Ctx.diagnose(DiagnosticInfoPGOProfile(MemoryProfileFileName.data(),
                                          "Not a memory profile"));
    return PreservedAnalyses::all();
  }

  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  // Map from the stack has of each allocation context in the function profiles
  // to the total profiled size (bytes), allocation type, and whether we matched
  // it to an allocation in the IR.
  std::map<uint64_t, AllocMatchInfo> FullStackIdToAllocMatchInfo;

  for (auto &F : M) {
    if (F.isDeclaration())
      continue;

    const TargetLibraryInfo &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
    readMemprof(M, F, MemProfReader.get(), TLI, FullStackIdToAllocMatchInfo);
  }

  if (ClPrintMemProfMatchInfo) {
    for (const auto &[Id, Info] : FullStackIdToAllocMatchInfo)
      errs() << "MemProf " << getAllocTypeAttributeString(Info.AllocType)
             << " context with id " << Id << " has total profiled size "
             << Info.TotalSize << (Info.Matched ? " is" : " not")
             << " matched\n";
  }

  return PreservedAnalyses::none();
}
