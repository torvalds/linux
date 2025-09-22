//===- PGOInstrumentation.cpp - MST-based PGO Instrumentation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements PGO instrumentation using a minimum spanning tree based
// on the following paper:
//   [1] Donald E. Knuth, Francis R. Stevenson. Optimal measurement of points
//   for program frequency counts. BIT Numerical Mathematics 1973, Volume 13,
//   Issue 3, pp 313-322
// The idea of the algorithm based on the fact that for each node (except for
// the entry and exit), the sum of incoming edge counts equals the sum of
// outgoing edge counts. The count of edge on spanning tree can be derived from
// those edges not on the spanning tree. Knuth proves this method instruments
// the minimum number of edges.
//
// The minimal spanning tree here is actually a maximum weight tree -- on-tree
// edges have higher frequencies (more likely to execute). The idea is to
// instrument those less frequently executed edges to reduce the runtime
// overhead of instrumented binaries.
//
// This file contains two passes:
// (1) Pass PGOInstrumentationGen which instruments the IR to generate edge
// count profile, and generates the instrumentation for indirect call
// profiling.
// (2) Pass PGOInstrumentationUse which reads the edge count profile and
// annotates the branch weights. It also reads the indirect call value
// profiling records and annotate the indirect call instructions.
//
// To get the precise counter information, These two passes need to invoke at
// the same compilation point (so they see the same IR). For pass
// PGOInstrumentationGen, the real work is done in instrumentOneFunc(). For
// pass PGOInstrumentationUse, the real work in done in class PGOUseFunc and
// the profile is opened in module level and passed to each PGOUseFunc instance.
// The shared code for PGOInstrumentationGen and PGOInstrumentationUse is put
// in class FuncPGOInstrumentation.
//
// Class PGOEdge represents a CFG edge and some auxiliary information. Class
// BBInfo contains auxiliary information for each BB. These two classes are used
// in pass PGOInstrumentationGen. Class PGOUseEdge and UseBBInfo are the derived
// class of PGOEdge and BBInfo, respectively. They contains extra data structure
// used in populating profile counters.
// The MST implementation is in Class CFGMST (CFGMST.h).
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "ValueProfileCollector.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/BlockCoverageInference.h"
#include "llvm/Transforms/Instrumentation/CFGMST.h"
#include "llvm/Transforms/Instrumentation/PGOCtxProfLowering.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/MisExpect.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace llvm;
using ProfileCount = Function::ProfileCount;
using VPCandidateInfo = ValueProfileCollector::CandidateInfo;

#define DEBUG_TYPE "pgo-instrumentation"

STATISTIC(NumOfPGOInstrument, "Number of edges instrumented.");
STATISTIC(NumOfPGOSelectInsts, "Number of select instruction instrumented.");
STATISTIC(NumOfPGOMemIntrinsics, "Number of mem intrinsics instrumented.");
STATISTIC(NumOfPGOEdge, "Number of edges.");
STATISTIC(NumOfPGOBB, "Number of basic-blocks.");
STATISTIC(NumOfPGOSplit, "Number of critical edge splits.");
STATISTIC(NumOfPGOFunc, "Number of functions having valid profile counts.");
STATISTIC(NumOfPGOMismatch, "Number of functions having mismatch profile.");
STATISTIC(NumOfPGOMissing, "Number of functions without profile.");
STATISTIC(NumOfPGOICall, "Number of indirect call value instrumentations.");
STATISTIC(NumOfCSPGOInstrument, "Number of edges instrumented in CSPGO.");
STATISTIC(NumOfCSPGOSelectInsts,
          "Number of select instruction instrumented in CSPGO.");
STATISTIC(NumOfCSPGOMemIntrinsics,
          "Number of mem intrinsics instrumented in CSPGO.");
STATISTIC(NumOfCSPGOEdge, "Number of edges in CSPGO.");
STATISTIC(NumOfCSPGOBB, "Number of basic-blocks in CSPGO.");
STATISTIC(NumOfCSPGOSplit, "Number of critical edge splits in CSPGO.");
STATISTIC(NumOfCSPGOFunc,
          "Number of functions having valid profile counts in CSPGO.");
STATISTIC(NumOfCSPGOMismatch,
          "Number of functions having mismatch profile in CSPGO.");
STATISTIC(NumOfCSPGOMissing, "Number of functions without profile in CSPGO.");
STATISTIC(NumCoveredBlocks, "Number of basic blocks that were executed");

// Command line option to specify the file to read profile from. This is
// mainly used for testing.
static cl::opt<std::string>
    PGOTestProfileFile("pgo-test-profile-file", cl::init(""), cl::Hidden,
                       cl::value_desc("filename"),
                       cl::desc("Specify the path of profile data file. This is"
                                "mainly for test purpose."));
static cl::opt<std::string> PGOTestProfileRemappingFile(
    "pgo-test-profile-remapping-file", cl::init(""), cl::Hidden,
    cl::value_desc("filename"),
    cl::desc("Specify the path of profile remapping file. This is mainly for "
             "test purpose."));

// Command line option to disable value profiling. The default is false:
// i.e. value profiling is enabled by default. This is for debug purpose.
static cl::opt<bool> DisableValueProfiling("disable-vp", cl::init(false),
                                           cl::Hidden,
                                           cl::desc("Disable Value Profiling"));

// Command line option to set the maximum number of VP annotations to write to
// the metadata for a single indirect call callsite.
static cl::opt<unsigned> MaxNumAnnotations(
    "icp-max-annotations", cl::init(3), cl::Hidden,
    cl::desc("Max number of annotations for a single indirect "
             "call callsite"));

// Command line option to set the maximum number of value annotations
// to write to the metadata for a single memop intrinsic.
static cl::opt<unsigned> MaxNumMemOPAnnotations(
    "memop-max-annotations", cl::init(4), cl::Hidden,
    cl::desc("Max number of preicise value annotations for a single memop"
             "intrinsic"));

// Command line option to control appending FunctionHash to the name of a COMDAT
// function. This is to avoid the hash mismatch caused by the preinliner.
static cl::opt<bool> DoComdatRenaming(
    "do-comdat-renaming", cl::init(false), cl::Hidden,
    cl::desc("Append function hash to the name of COMDAT function to avoid "
             "function hash mismatch due to the preinliner"));

namespace llvm {
// Command line option to enable/disable the warning about missing profile
// information.
cl::opt<bool> PGOWarnMissing("pgo-warn-missing-function", cl::init(false),
                             cl::Hidden,
                             cl::desc("Use this option to turn on/off "
                                      "warnings about missing profile data for "
                                      "functions."));

// Command line option to enable/disable the warning about a hash mismatch in
// the profile data.
cl::opt<bool>
    NoPGOWarnMismatch("no-pgo-warn-mismatch", cl::init(false), cl::Hidden,
                      cl::desc("Use this option to turn off/on "
                               "warnings about profile cfg mismatch."));

// Command line option to enable/disable the warning about a hash mismatch in
// the profile data for Comdat functions, which often turns out to be false
// positive due to the pre-instrumentation inline.
cl::opt<bool> NoPGOWarnMismatchComdatWeak(
    "no-pgo-warn-mismatch-comdat-weak", cl::init(true), cl::Hidden,
    cl::desc("The option is used to turn on/off "
             "warnings about hash mismatch for comdat "
             "or weak functions."));
} // namespace llvm

// Command line option to enable/disable select instruction instrumentation.
static cl::opt<bool>
    PGOInstrSelect("pgo-instr-select", cl::init(true), cl::Hidden,
                   cl::desc("Use this option to turn on/off SELECT "
                            "instruction instrumentation. "));

// Command line option to turn on CFG dot or text dump of raw profile counts
static cl::opt<PGOViewCountsType> PGOViewRawCounts(
    "pgo-view-raw-counts", cl::Hidden,
    cl::desc("A boolean option to show CFG dag or text "
             "with raw profile counts from "
             "profile data. See also option "
             "-pgo-view-counts. To limit graph "
             "display to only one function, use "
             "filtering option -view-bfi-func-name."),
    cl::values(clEnumValN(PGOVCT_None, "none", "do not show."),
               clEnumValN(PGOVCT_Graph, "graph", "show a graph."),
               clEnumValN(PGOVCT_Text, "text", "show in text.")));

// Command line option to enable/disable memop intrinsic call.size profiling.
static cl::opt<bool>
    PGOInstrMemOP("pgo-instr-memop", cl::init(true), cl::Hidden,
                  cl::desc("Use this option to turn on/off "
                           "memory intrinsic size profiling."));

// Emit branch probability as optimization remarks.
static cl::opt<bool>
    EmitBranchProbability("pgo-emit-branch-prob", cl::init(false), cl::Hidden,
                          cl::desc("When this option is on, the annotated "
                                   "branch probability will be emitted as "
                                   "optimization remarks: -{Rpass|"
                                   "pass-remarks}=pgo-instrumentation"));

static cl::opt<bool> PGOInstrumentEntry(
    "pgo-instrument-entry", cl::init(false), cl::Hidden,
    cl::desc("Force to instrument function entry basicblock."));

static cl::opt<bool> PGOFunctionEntryCoverage(
    "pgo-function-entry-coverage", cl::Hidden,
    cl::desc(
        "Use this option to enable function entry coverage instrumentation."));

static cl::opt<bool> PGOBlockCoverage(
    "pgo-block-coverage",
    cl::desc("Use this option to enable basic block coverage instrumentation"));

static cl::opt<bool>
    PGOViewBlockCoverageGraph("pgo-view-block-coverage-graph",
                              cl::desc("Create a dot file of CFGs with block "
                                       "coverage inference information"));

static cl::opt<bool> PGOTemporalInstrumentation(
    "pgo-temporal-instrumentation",
    cl::desc("Use this option to enable temporal instrumentation"));

static cl::opt<bool>
    PGOFixEntryCount("pgo-fix-entry-count", cl::init(true), cl::Hidden,
                     cl::desc("Fix function entry count in profile use."));

static cl::opt<bool> PGOVerifyHotBFI(
    "pgo-verify-hot-bfi", cl::init(false), cl::Hidden,
    cl::desc("Print out the non-match BFI count if a hot raw profile count "
             "becomes non-hot, or a cold raw profile count becomes hot. "
             "The print is enabled under -Rpass-analysis=pgo, or "
             "internal option -pass-remakrs-analysis=pgo."));

static cl::opt<bool> PGOVerifyBFI(
    "pgo-verify-bfi", cl::init(false), cl::Hidden,
    cl::desc("Print out mismatched BFI counts after setting profile metadata "
             "The print is enabled under -Rpass-analysis=pgo, or "
             "internal option -pass-remakrs-analysis=pgo."));

static cl::opt<unsigned> PGOVerifyBFIRatio(
    "pgo-verify-bfi-ratio", cl::init(2), cl::Hidden,
    cl::desc("Set the threshold for pgo-verify-bfi:  only print out "
             "mismatched BFI if the difference percentage is greater than "
             "this value (in percentage)."));

static cl::opt<unsigned> PGOVerifyBFICutoff(
    "pgo-verify-bfi-cutoff", cl::init(5), cl::Hidden,
    cl::desc("Set the threshold for pgo-verify-bfi: skip the counts whose "
             "profile count value is below."));

static cl::opt<std::string> PGOTraceFuncHash(
    "pgo-trace-func-hash", cl::init("-"), cl::Hidden,
    cl::value_desc("function name"),
    cl::desc("Trace the hash of the function with this name."));

static cl::opt<unsigned> PGOFunctionSizeThreshold(
    "pgo-function-size-threshold", cl::Hidden,
    cl::desc("Do not instrument functions smaller than this threshold."));

static cl::opt<unsigned> PGOFunctionCriticalEdgeThreshold(
    "pgo-critical-edge-threshold", cl::init(20000), cl::Hidden,
    cl::desc("Do not instrument functions with the number of critical edges "
             " greater than this threshold."));

extern cl::opt<unsigned> MaxNumVTableAnnotations;

namespace llvm {
// Command line option to turn on CFG dot dump after profile annotation.
// Defined in Analysis/BlockFrequencyInfo.cpp:  -pgo-view-counts
extern cl::opt<PGOViewCountsType> PGOViewCounts;

// Command line option to specify the name of the function for CFG dump
// Defined in Analysis/BlockFrequencyInfo.cpp:  -view-bfi-func-name=
extern cl::opt<std::string> ViewBlockFreqFuncName;

// Command line option to enable vtable value profiling. Defined in
// ProfileData/InstrProf.cpp: -enable-vtable-value-profiling=
extern cl::opt<bool> EnableVTableValueProfiling;
extern cl::opt<bool> EnableVTableProfileUse;
extern cl::opt<InstrProfCorrelator::ProfCorrelatorKind> ProfileCorrelate;
} // namespace llvm

bool shouldInstrumentEntryBB() {
  return PGOInstrumentEntry ||
         PGOCtxProfLoweringPass::isContextualIRPGOEnabled();
}

// FIXME(mtrofin): re-enable this for ctx profiling, for non-indirect calls. Ctx
// profiling implicitly captures indirect call cases, but not other values.
// Supporting other values is relatively straight-forward - just another counter
// range within the context.
bool isValueProfilingDisabled() {
  return DisableValueProfiling ||
         PGOCtxProfLoweringPass::isContextualIRPGOEnabled();
}

// Return a string describing the branch condition that can be
// used in static branch probability heuristics:
static std::string getBranchCondString(Instruction *TI) {
  BranchInst *BI = dyn_cast<BranchInst>(TI);
  if (!BI || !BI->isConditional())
    return std::string();

  Value *Cond = BI->getCondition();
  ICmpInst *CI = dyn_cast<ICmpInst>(Cond);
  if (!CI)
    return std::string();

  std::string result;
  raw_string_ostream OS(result);
  OS << CI->getPredicate() << "_";
  CI->getOperand(0)->getType()->print(OS, true);

  Value *RHS = CI->getOperand(1);
  ConstantInt *CV = dyn_cast<ConstantInt>(RHS);
  if (CV) {
    if (CV->isZero())
      OS << "_Zero";
    else if (CV->isOne())
      OS << "_One";
    else if (CV->isMinusOne())
      OS << "_MinusOne";
    else
      OS << "_Const";
  }
  OS.flush();
  return result;
}

static const char *ValueProfKindDescr[] = {
#define VALUE_PROF_KIND(Enumerator, Value, Descr) Descr,
#include "llvm/ProfileData/InstrProfData.inc"
};

// Create a COMDAT variable INSTR_PROF_RAW_VERSION_VAR to make the runtime
// aware this is an ir_level profile so it can set the version flag.
static GlobalVariable *createIRLevelProfileFlagVar(Module &M, bool IsCS) {
  const StringRef VarName(INSTR_PROF_QUOTE(INSTR_PROF_RAW_VERSION_VAR));
  Type *IntTy64 = Type::getInt64Ty(M.getContext());
  uint64_t ProfileVersion = (INSTR_PROF_RAW_VERSION | VARIANT_MASK_IR_PROF);
  if (IsCS)
    ProfileVersion |= VARIANT_MASK_CSIR_PROF;
  if (shouldInstrumentEntryBB())
    ProfileVersion |= VARIANT_MASK_INSTR_ENTRY;
  if (DebugInfoCorrelate || ProfileCorrelate == InstrProfCorrelator::DEBUG_INFO)
    ProfileVersion |= VARIANT_MASK_DBG_CORRELATE;
  if (PGOFunctionEntryCoverage)
    ProfileVersion |=
        VARIANT_MASK_BYTE_COVERAGE | VARIANT_MASK_FUNCTION_ENTRY_ONLY;
  if (PGOBlockCoverage)
    ProfileVersion |= VARIANT_MASK_BYTE_COVERAGE;
  if (PGOTemporalInstrumentation)
    ProfileVersion |= VARIANT_MASK_TEMPORAL_PROF;
  auto IRLevelVersionVariable = new GlobalVariable(
      M, IntTy64, true, GlobalValue::WeakAnyLinkage,
      Constant::getIntegerValue(IntTy64, APInt(64, ProfileVersion)), VarName);
  IRLevelVersionVariable->setVisibility(GlobalValue::HiddenVisibility);
  Triple TT(M.getTargetTriple());
  if (TT.supportsCOMDAT()) {
    IRLevelVersionVariable->setLinkage(GlobalValue::ExternalLinkage);
    IRLevelVersionVariable->setComdat(M.getOrInsertComdat(VarName));
  }
  return IRLevelVersionVariable;
}

namespace {

/// The select instruction visitor plays three roles specified
/// by the mode. In \c VM_counting mode, it simply counts the number of
/// select instructions. In \c VM_instrument mode, it inserts code to count
/// the number times TrueValue of select is taken. In \c VM_annotate mode,
/// it reads the profile data and annotate the select instruction with metadata.
enum VisitMode { VM_counting, VM_instrument, VM_annotate };
class PGOUseFunc;

/// Instruction Visitor class to visit select instructions.
struct SelectInstVisitor : public InstVisitor<SelectInstVisitor> {
  Function &F;
  unsigned NSIs = 0;             // Number of select instructions instrumented.
  VisitMode Mode = VM_counting;  // Visiting mode.
  unsigned *CurCtrIdx = nullptr; // Pointer to current counter index.
  unsigned TotalNumCtrs = 0;     // Total number of counters
  GlobalVariable *FuncNameVar = nullptr;
  uint64_t FuncHash = 0;
  PGOUseFunc *UseFunc = nullptr;
  bool HasSingleByteCoverage;

  SelectInstVisitor(Function &Func, bool HasSingleByteCoverage)
      : F(Func), HasSingleByteCoverage(HasSingleByteCoverage) {}

  void countSelects() {
    NSIs = 0;
    Mode = VM_counting;
    visit(F);
  }

  // Visit the IR stream and instrument all select instructions. \p
  // Ind is a pointer to the counter index variable; \p TotalNC
  // is the total number of counters; \p FNV is the pointer to the
  // PGO function name var; \p FHash is the function hash.
  void instrumentSelects(unsigned *Ind, unsigned TotalNC, GlobalVariable *FNV,
                         uint64_t FHash) {
    Mode = VM_instrument;
    CurCtrIdx = Ind;
    TotalNumCtrs = TotalNC;
    FuncHash = FHash;
    FuncNameVar = FNV;
    visit(F);
  }

  // Visit the IR stream and annotate all select instructions.
  void annotateSelects(PGOUseFunc *UF, unsigned *Ind) {
    Mode = VM_annotate;
    UseFunc = UF;
    CurCtrIdx = Ind;
    visit(F);
  }

  void instrumentOneSelectInst(SelectInst &SI);
  void annotateOneSelectInst(SelectInst &SI);

  // Visit \p SI instruction and perform tasks according to visit mode.
  void visitSelectInst(SelectInst &SI);

  // Return the number of select instructions. This needs be called after
  // countSelects().
  unsigned getNumOfSelectInsts() const { return NSIs; }
};

/// This class implements the CFG edges for the Minimum Spanning Tree (MST)
/// based instrumentation.
/// Note that the CFG can be a multi-graph. So there might be multiple edges
/// with the same SrcBB and DestBB.
struct PGOEdge {
  BasicBlock *SrcBB;
  BasicBlock *DestBB;
  uint64_t Weight;
  bool InMST = false;
  bool Removed = false;
  bool IsCritical = false;

  PGOEdge(BasicBlock *Src, BasicBlock *Dest, uint64_t W = 1)
      : SrcBB(Src), DestBB(Dest), Weight(W) {}

  /// Return the information string of an edge.
  std::string infoString() const {
    return (Twine(Removed ? "-" : " ") + (InMST ? " " : "*") +
            (IsCritical ? "c" : " ") + "  W=" + Twine(Weight))
        .str();
  }
};

/// This class stores the auxiliary information for each BB in the MST.
struct PGOBBInfo {
  PGOBBInfo *Group;
  uint32_t Index;
  uint32_t Rank = 0;

  PGOBBInfo(unsigned IX) : Group(this), Index(IX) {}

  /// Return the information string of this object.
  std::string infoString() const {
    return (Twine("Index=") + Twine(Index)).str();
  }
};

// This class implements the CFG edges. Note the CFG can be a multi-graph.
template <class Edge, class BBInfo> class FuncPGOInstrumentation {
private:
  Function &F;

  // Is this is context-sensitive instrumentation.
  bool IsCS;

  // A map that stores the Comdat group in function F.
  std::unordered_multimap<Comdat *, GlobalValue *> &ComdatMembers;

  ValueProfileCollector VPC;

  void computeCFGHash();
  void renameComdatFunction();

public:
  const TargetLibraryInfo &TLI;
  std::vector<std::vector<VPCandidateInfo>> ValueSites;
  SelectInstVisitor SIVisitor;
  std::string FuncName;
  std::string DeprecatedFuncName;
  GlobalVariable *FuncNameVar;

  // CFG hash value for this function.
  uint64_t FunctionHash = 0;

  // The Minimum Spanning Tree of function CFG.
  CFGMST<Edge, BBInfo> MST;

  const std::optional<BlockCoverageInference> BCI;

  static std::optional<BlockCoverageInference>
  constructBCI(Function &Func, bool HasSingleByteCoverage,
               bool InstrumentFuncEntry) {
    if (HasSingleByteCoverage)
      return BlockCoverageInference(Func, InstrumentFuncEntry);
    return {};
  }

  // Collect all the BBs that will be instrumented, and store them in
  // InstrumentBBs.
  void getInstrumentBBs(std::vector<BasicBlock *> &InstrumentBBs);

  // Give an edge, find the BB that will be instrumented.
  // Return nullptr if there is no BB to be instrumented.
  BasicBlock *getInstrBB(Edge *E);

  // Return the auxiliary BB information.
  BBInfo &getBBInfo(const BasicBlock *BB) const { return MST.getBBInfo(BB); }

  // Return the auxiliary BB information if available.
  BBInfo *findBBInfo(const BasicBlock *BB) const { return MST.findBBInfo(BB); }

  // Dump edges and BB information.
  void dumpInfo(StringRef Str = "") const {
    MST.dumpEdges(dbgs(), Twine("Dump Function ") + FuncName +
                              " Hash: " + Twine(FunctionHash) + "\t" + Str);
  }

  FuncPGOInstrumentation(
      Function &Func, TargetLibraryInfo &TLI,
      std::unordered_multimap<Comdat *, GlobalValue *> &ComdatMembers,
      bool CreateGlobalVar = false, BranchProbabilityInfo *BPI = nullptr,
      BlockFrequencyInfo *BFI = nullptr, bool IsCS = false,
      bool InstrumentFuncEntry = true, bool HasSingleByteCoverage = false)
      : F(Func), IsCS(IsCS), ComdatMembers(ComdatMembers), VPC(Func, TLI),
        TLI(TLI), ValueSites(IPVK_Last + 1),
        SIVisitor(Func, HasSingleByteCoverage),
        MST(F, InstrumentFuncEntry, BPI, BFI),
        BCI(constructBCI(Func, HasSingleByteCoverage, InstrumentFuncEntry)) {
    if (BCI && PGOViewBlockCoverageGraph)
      BCI->viewBlockCoverageGraph();
    // This should be done before CFG hash computation.
    SIVisitor.countSelects();
    ValueSites[IPVK_MemOPSize] = VPC.get(IPVK_MemOPSize);
    if (!IsCS) {
      NumOfPGOSelectInsts += SIVisitor.getNumOfSelectInsts();
      NumOfPGOMemIntrinsics += ValueSites[IPVK_MemOPSize].size();
      NumOfPGOBB += MST.bbInfoSize();
      ValueSites[IPVK_IndirectCallTarget] = VPC.get(IPVK_IndirectCallTarget);
      if (EnableVTableValueProfiling)
        ValueSites[IPVK_VTableTarget] = VPC.get(IPVK_VTableTarget);
    } else {
      NumOfCSPGOSelectInsts += SIVisitor.getNumOfSelectInsts();
      NumOfCSPGOMemIntrinsics += ValueSites[IPVK_MemOPSize].size();
      NumOfCSPGOBB += MST.bbInfoSize();
    }

    FuncName = getIRPGOFuncName(F);
    DeprecatedFuncName = getPGOFuncName(F);
    computeCFGHash();
    if (!ComdatMembers.empty())
      renameComdatFunction();
    LLVM_DEBUG(dumpInfo("after CFGMST"));

    for (const auto &E : MST.allEdges()) {
      if (E->Removed)
        continue;
      IsCS ? NumOfCSPGOEdge++ : NumOfPGOEdge++;
      if (!E->InMST)
        IsCS ? NumOfCSPGOInstrument++ : NumOfPGOInstrument++;
    }

    if (CreateGlobalVar)
      FuncNameVar = createPGOFuncNameVar(F, FuncName);
  }
};

} // end anonymous namespace

// Compute Hash value for the CFG: the lower 32 bits are CRC32 of the index
// value of each BB in the CFG. The higher 32 bits are the CRC32 of the numbers
// of selects, indirect calls, mem ops and edges.
template <class Edge, class BBInfo>
void FuncPGOInstrumentation<Edge, BBInfo>::computeCFGHash() {
  std::vector<uint8_t> Indexes;
  JamCRC JC;
  for (auto &BB : F) {
    for (BasicBlock *Succ : successors(&BB)) {
      auto BI = findBBInfo(Succ);
      if (BI == nullptr)
        continue;
      uint32_t Index = BI->Index;
      for (int J = 0; J < 4; J++)
        Indexes.push_back((uint8_t)(Index >> (J * 8)));
    }
  }
  JC.update(Indexes);

  JamCRC JCH;
  // The higher 32 bits.
  auto updateJCH = [&JCH](uint64_t Num) {
    uint8_t Data[8];
    support::endian::write64le(Data, Num);
    JCH.update(Data);
  };
  updateJCH((uint64_t)SIVisitor.getNumOfSelectInsts());
  updateJCH((uint64_t)ValueSites[IPVK_IndirectCallTarget].size());
  updateJCH((uint64_t)ValueSites[IPVK_MemOPSize].size());
  if (BCI) {
    updateJCH(BCI->getInstrumentedBlocksHash());
  } else {
    updateJCH((uint64_t)MST.numEdges());
  }

  // Hash format for context sensitive profile. Reserve 4 bits for other
  // information.
  FunctionHash = (((uint64_t)JCH.getCRC()) << 28) + JC.getCRC();

  // Reserve bit 60-63 for other information purpose.
  FunctionHash &= 0x0FFFFFFFFFFFFFFF;
  if (IsCS)
    NamedInstrProfRecord::setCSFlagInHash(FunctionHash);
  LLVM_DEBUG(dbgs() << "Function Hash Computation for " << F.getName() << ":\n"
                    << " CRC = " << JC.getCRC()
                    << ", Selects = " << SIVisitor.getNumOfSelectInsts()
                    << ", Edges = " << MST.numEdges() << ", ICSites = "
                    << ValueSites[IPVK_IndirectCallTarget].size()
                    << ", Memops = " << ValueSites[IPVK_MemOPSize].size()
                    << ", High32 CRC = " << JCH.getCRC()
                    << ", Hash = " << FunctionHash << "\n";);

  if (PGOTraceFuncHash != "-" && F.getName().contains(PGOTraceFuncHash))
    dbgs() << "Funcname=" << F.getName() << ", Hash=" << FunctionHash
           << " in building " << F.getParent()->getSourceFileName() << "\n";
}

// Check if we can safely rename this Comdat function.
static bool canRenameComdat(
    Function &F,
    std::unordered_multimap<Comdat *, GlobalValue *> &ComdatMembers) {
  if (!DoComdatRenaming || !canRenameComdatFunc(F, true))
    return false;

  // FIXME: Current only handle those Comdat groups that only containing one
  // function.
  // (1) For a Comdat group containing multiple functions, we need to have a
  // unique postfix based on the hashes for each function. There is a
  // non-trivial code refactoring to do this efficiently.
  // (2) Variables can not be renamed, so we can not rename Comdat function in a
  // group including global vars.
  Comdat *C = F.getComdat();
  for (auto &&CM : make_range(ComdatMembers.equal_range(C))) {
    assert(!isa<GlobalAlias>(CM.second));
    Function *FM = dyn_cast<Function>(CM.second);
    if (FM != &F)
      return false;
  }
  return true;
}

// Append the CFGHash to the Comdat function name.
template <class Edge, class BBInfo>
void FuncPGOInstrumentation<Edge, BBInfo>::renameComdatFunction() {
  if (!canRenameComdat(F, ComdatMembers))
    return;
  std::string OrigName = F.getName().str();
  std::string NewFuncName =
      Twine(F.getName() + "." + Twine(FunctionHash)).str();
  F.setName(Twine(NewFuncName));
  GlobalAlias::create(GlobalValue::WeakAnyLinkage, OrigName, &F);
  FuncName = Twine(FuncName + "." + Twine(FunctionHash)).str();
  Comdat *NewComdat;
  Module *M = F.getParent();
  // For AvailableExternallyLinkage functions, change the linkage to
  // LinkOnceODR and put them into comdat. This is because after renaming, there
  // is no backup external copy available for the function.
  if (!F.hasComdat()) {
    assert(F.getLinkage() == GlobalValue::AvailableExternallyLinkage);
    NewComdat = M->getOrInsertComdat(StringRef(NewFuncName));
    F.setLinkage(GlobalValue::LinkOnceODRLinkage);
    F.setComdat(NewComdat);
    return;
  }

  // This function belongs to a single function Comdat group.
  Comdat *OrigComdat = F.getComdat();
  std::string NewComdatName =
      Twine(OrigComdat->getName() + "." + Twine(FunctionHash)).str();
  NewComdat = M->getOrInsertComdat(StringRef(NewComdatName));
  NewComdat->setSelectionKind(OrigComdat->getSelectionKind());

  for (auto &&CM : make_range(ComdatMembers.equal_range(OrigComdat))) {
    // Must be a function.
    cast<Function>(CM.second)->setComdat(NewComdat);
  }
}

/// Collect all the BBs that will be instruments and add them to
/// `InstrumentBBs`.
template <class Edge, class BBInfo>
void FuncPGOInstrumentation<Edge, BBInfo>::getInstrumentBBs(
    std::vector<BasicBlock *> &InstrumentBBs) {
  if (BCI) {
    for (auto &BB : F)
      if (BCI->shouldInstrumentBlock(BB))
        InstrumentBBs.push_back(&BB);
    return;
  }

  // Use a worklist as we will update the vector during the iteration.
  std::vector<Edge *> EdgeList;
  EdgeList.reserve(MST.numEdges());
  for (const auto &E : MST.allEdges())
    EdgeList.push_back(E.get());

  for (auto &E : EdgeList) {
    BasicBlock *InstrBB = getInstrBB(E);
    if (InstrBB)
      InstrumentBBs.push_back(InstrBB);
  }
}

// Given a CFG E to be instrumented, find which BB to place the instrumented
// code. The function will split the critical edge if necessary.
template <class Edge, class BBInfo>
BasicBlock *FuncPGOInstrumentation<Edge, BBInfo>::getInstrBB(Edge *E) {
  if (E->InMST || E->Removed)
    return nullptr;

  BasicBlock *SrcBB = E->SrcBB;
  BasicBlock *DestBB = E->DestBB;
  // For a fake edge, instrument the real BB.
  if (SrcBB == nullptr)
    return DestBB;
  if (DestBB == nullptr)
    return SrcBB;

  auto canInstrument = [](BasicBlock *BB) -> BasicBlock * {
    // There are basic blocks (such as catchswitch) cannot be instrumented.
    // If the returned first insertion point is the end of BB, skip this BB.
    if (BB->getFirstInsertionPt() == BB->end())
      return nullptr;
    return BB;
  };

  // Instrument the SrcBB if it has a single successor,
  // otherwise, the DestBB if this is not a critical edge.
  Instruction *TI = SrcBB->getTerminator();
  if (TI->getNumSuccessors() <= 1)
    return canInstrument(SrcBB);
  if (!E->IsCritical)
    return canInstrument(DestBB);

  // Some IndirectBr critical edges cannot be split by the previous
  // SplitIndirectBrCriticalEdges call. Bail out.
  unsigned SuccNum = GetSuccessorNumber(SrcBB, DestBB);
  BasicBlock *InstrBB =
      isa<IndirectBrInst>(TI) ? nullptr : SplitCriticalEdge(TI, SuccNum);
  if (!InstrBB) {
    LLVM_DEBUG(
        dbgs() << "Fail to split critical edge: not instrument this edge.\n");
    return nullptr;
  }
  // For a critical edge, we have to split. Instrument the newly
  // created BB.
  IsCS ? NumOfCSPGOSplit++ : NumOfPGOSplit++;
  LLVM_DEBUG(dbgs() << "Split critical edge: " << getBBInfo(SrcBB).Index
                    << " --> " << getBBInfo(DestBB).Index << "\n");
  // Need to add two new edges. First one: Add new edge of SrcBB->InstrBB.
  MST.addEdge(SrcBB, InstrBB, 0);
  // Second one: Add new edge of InstrBB->DestBB.
  Edge &NewEdge1 = MST.addEdge(InstrBB, DestBB, 0);
  NewEdge1.InMST = true;
  E->Removed = true;

  return canInstrument(InstrBB);
}

// When generating value profiling calls on Windows routines that make use of
// handler funclets for exception processing an operand bundle needs to attached
// to the called function. This routine will set \p OpBundles to contain the
// funclet information, if any is needed, that should be placed on the generated
// value profiling call for the value profile candidate call.
static void
populateEHOperandBundle(VPCandidateInfo &Cand,
                        DenseMap<BasicBlock *, ColorVector> &BlockColors,
                        SmallVectorImpl<OperandBundleDef> &OpBundles) {
  auto *OrigCall = dyn_cast<CallBase>(Cand.AnnotatedInst);
  if (!OrigCall)
    return;

  if (!isa<IntrinsicInst>(OrigCall)) {
    // The instrumentation call should belong to the same funclet as a
    // non-intrinsic call, so just copy the operand bundle, if any exists.
    std::optional<OperandBundleUse> ParentFunclet =
        OrigCall->getOperandBundle(LLVMContext::OB_funclet);
    if (ParentFunclet)
      OpBundles.emplace_back(OperandBundleDef(*ParentFunclet));
  } else {
    // Intrinsics or other instructions do not get funclet information from the
    // front-end. Need to use the BlockColors that was computed by the routine
    // colorEHFunclets to determine whether a funclet is needed.
    if (!BlockColors.empty()) {
      const ColorVector &CV = BlockColors.find(OrigCall->getParent())->second;
      assert(CV.size() == 1 && "non-unique color for block!");
      Instruction *EHPad = CV.front()->getFirstNonPHI();
      if (EHPad->isEHPad())
        OpBundles.emplace_back("funclet", EHPad);
    }
  }
}

// Visit all edge and instrument the edges not in MST, and do value profiling.
// Critical edges will be split.
static void instrumentOneFunc(
    Function &F, Module *M, TargetLibraryInfo &TLI, BranchProbabilityInfo *BPI,
    BlockFrequencyInfo *BFI,
    std::unordered_multimap<Comdat *, GlobalValue *> &ComdatMembers,
    bool IsCS) {
  if (!PGOBlockCoverage) {
    // Split indirectbr critical edges here before computing the MST rather than
    // later in getInstrBB() to avoid invalidating it.
    SplitIndirectBrCriticalEdges(F, /*IgnoreBlocksWithoutPHI=*/false, BPI, BFI);
  }

  FuncPGOInstrumentation<PGOEdge, PGOBBInfo> FuncInfo(
      F, TLI, ComdatMembers, true, BPI, BFI, IsCS, shouldInstrumentEntryBB(),
      PGOBlockCoverage);

  auto Name = FuncInfo.FuncNameVar;
  auto CFGHash = ConstantInt::get(Type::getInt64Ty(M->getContext()),
                                  FuncInfo.FunctionHash);
  if (PGOFunctionEntryCoverage) {
    auto &EntryBB = F.getEntryBlock();
    IRBuilder<> Builder(&EntryBB, EntryBB.getFirstInsertionPt());
    // llvm.instrprof.cover(i8* <name>, i64 <hash>, i32 <num-counters>,
    //                      i32 <index>)
    Builder.CreateCall(
        Intrinsic::getDeclaration(M, Intrinsic::instrprof_cover),
        {Name, CFGHash, Builder.getInt32(1), Builder.getInt32(0)});
    return;
  }

  std::vector<BasicBlock *> InstrumentBBs;
  FuncInfo.getInstrumentBBs(InstrumentBBs);
  unsigned NumCounters =
      InstrumentBBs.size() + FuncInfo.SIVisitor.getNumOfSelectInsts();

  if (PGOCtxProfLoweringPass::isContextualIRPGOEnabled()) {
    auto *CSIntrinsic =
        Intrinsic::getDeclaration(M, Intrinsic::instrprof_callsite);
    // We want to count the instrumentable callsites, then instrument them. This
    // is because the llvm.instrprof.callsite intrinsic has an argument (like
    // the other instrprof intrinsics) capturing the total number of
    // instrumented objects (counters, or callsites, in this case). In this
    // case, we want that value so we can readily pass it to the compiler-rt
    // APIs that may have to allocate memory based on the nr of callsites.
    // The traversal logic is the same for both counting and instrumentation,
    // just needs to be done in succession.
    auto Visit = [&](llvm::function_ref<void(CallBase * CB)> Visitor) {
      for (auto &BB : F)
        for (auto &Instr : BB)
          if (auto *CS = dyn_cast<CallBase>(&Instr)) {
            if ((CS->getCalledFunction() &&
                 CS->getCalledFunction()->isIntrinsic()) ||
                dyn_cast<InlineAsm>(CS->getCalledOperand()))
              continue;
            Visitor(CS);
          }
    };
    // First, count callsites.
    uint32_t TotalNrCallsites = 0;
    Visit([&TotalNrCallsites](auto *) { ++TotalNrCallsites; });

    // Now instrument.
    uint32_t CallsiteIndex = 0;
    Visit([&](auto *CB) {
      IRBuilder<> Builder(CB);
      Builder.CreateCall(CSIntrinsic,
                         {Name, CFGHash, Builder.getInt32(TotalNrCallsites),
                          Builder.getInt32(CallsiteIndex++),
                          CB->getCalledOperand()});
    });
  }

  uint32_t I = 0;
  if (PGOTemporalInstrumentation) {
    NumCounters += PGOBlockCoverage ? 8 : 1;
    auto &EntryBB = F.getEntryBlock();
    IRBuilder<> Builder(&EntryBB, EntryBB.getFirstInsertionPt());
    // llvm.instrprof.timestamp(i8* <name>, i64 <hash>, i32 <num-counters>,
    //                          i32 <index>)
    Builder.CreateCall(
        Intrinsic::getDeclaration(M, Intrinsic::instrprof_timestamp),
        {Name, CFGHash, Builder.getInt32(NumCounters), Builder.getInt32(I)});
    I += PGOBlockCoverage ? 8 : 1;
  }

  for (auto *InstrBB : InstrumentBBs) {
    IRBuilder<> Builder(InstrBB, InstrBB->getFirstInsertionPt());
    assert(Builder.GetInsertPoint() != InstrBB->end() &&
           "Cannot get the Instrumentation point");
    // llvm.instrprof.increment(i8* <name>, i64 <hash>, i32 <num-counters>,
    //                          i32 <index>)
    Builder.CreateCall(
        Intrinsic::getDeclaration(M, PGOBlockCoverage
                                         ? Intrinsic::instrprof_cover
                                         : Intrinsic::instrprof_increment),
        {Name, CFGHash, Builder.getInt32(NumCounters), Builder.getInt32(I++)});
  }

  // Now instrument select instructions:
  FuncInfo.SIVisitor.instrumentSelects(&I, NumCounters, FuncInfo.FuncNameVar,
                                       FuncInfo.FunctionHash);
  assert(I == NumCounters);

  if (isValueProfilingDisabled())
    return;

  NumOfPGOICall += FuncInfo.ValueSites[IPVK_IndirectCallTarget].size();

  // Intrinsic function calls do not have funclet operand bundles needed for
  // Windows exception handling attached to them. However, if value profiling is
  // inserted for one of these calls, then a funclet value will need to be set
  // on the instrumentation call based on the funclet coloring.
  DenseMap<BasicBlock *, ColorVector> BlockColors;
  if (F.hasPersonalityFn() &&
      isScopedEHPersonality(classifyEHPersonality(F.getPersonalityFn())))
    BlockColors = colorEHFunclets(F);

  // For each VP Kind, walk the VP candidates and instrument each one.
  for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind) {
    unsigned SiteIndex = 0;
    if (Kind == IPVK_MemOPSize && !PGOInstrMemOP)
      continue;

    for (VPCandidateInfo Cand : FuncInfo.ValueSites[Kind]) {
      LLVM_DEBUG(dbgs() << "Instrument one VP " << ValueProfKindDescr[Kind]
                        << " site: CallSite Index = " << SiteIndex << "\n");

      IRBuilder<> Builder(Cand.InsertPt);
      assert(Builder.GetInsertPoint() != Cand.InsertPt->getParent()->end() &&
             "Cannot get the Instrumentation point");

      Value *ToProfile = nullptr;
      if (Cand.V->getType()->isIntegerTy())
        ToProfile = Builder.CreateZExtOrTrunc(Cand.V, Builder.getInt64Ty());
      else if (Cand.V->getType()->isPointerTy())
        ToProfile = Builder.CreatePtrToInt(Cand.V, Builder.getInt64Ty());
      assert(ToProfile && "value profiling Value is of unexpected type");

      SmallVector<OperandBundleDef, 1> OpBundles;
      populateEHOperandBundle(Cand, BlockColors, OpBundles);
      Builder.CreateCall(
          Intrinsic::getDeclaration(M, Intrinsic::instrprof_value_profile),
          {FuncInfo.FuncNameVar, Builder.getInt64(FuncInfo.FunctionHash),
           ToProfile, Builder.getInt32(Kind), Builder.getInt32(SiteIndex++)},
          OpBundles);
    }
  } // IPVK_First <= Kind <= IPVK_Last
}

namespace {

// This class represents a CFG edge in profile use compilation.
struct PGOUseEdge : public PGOEdge {
  using PGOEdge::PGOEdge;

  std::optional<uint64_t> Count;

  // Set edge count value
  void setEdgeCount(uint64_t Value) { Count = Value; }

  // Return the information string for this object.
  std::string infoString() const {
    if (!Count)
      return PGOEdge::infoString();
    return (Twine(PGOEdge::infoString()) + "  Count=" + Twine(*Count)).str();
  }
};

using DirectEdges = SmallVector<PGOUseEdge *, 2>;

// This class stores the auxiliary information for each BB.
struct PGOUseBBInfo : public PGOBBInfo {
  std::optional<uint64_t> Count;
  int32_t UnknownCountInEdge = 0;
  int32_t UnknownCountOutEdge = 0;
  DirectEdges InEdges;
  DirectEdges OutEdges;

  PGOUseBBInfo(unsigned IX) : PGOBBInfo(IX) {}

  // Set the profile count value for this BB.
  void setBBInfoCount(uint64_t Value) { Count = Value; }

  // Return the information string of this object.
  std::string infoString() const {
    if (!Count)
      return PGOBBInfo::infoString();
    return (Twine(PGOBBInfo::infoString()) + "  Count=" + Twine(*Count)).str();
  }

  // Add an OutEdge and update the edge count.
  void addOutEdge(PGOUseEdge *E) {
    OutEdges.push_back(E);
    UnknownCountOutEdge++;
  }

  // Add an InEdge and update the edge count.
  void addInEdge(PGOUseEdge *E) {
    InEdges.push_back(E);
    UnknownCountInEdge++;
  }
};

} // end anonymous namespace

// Sum up the count values for all the edges.
static uint64_t sumEdgeCount(const ArrayRef<PGOUseEdge *> Edges) {
  uint64_t Total = 0;
  for (const auto &E : Edges) {
    if (E->Removed)
      continue;
    if (E->Count)
      Total += *E->Count;
  }
  return Total;
}

namespace {

class PGOUseFunc {
public:
  PGOUseFunc(Function &Func, Module *Modu, TargetLibraryInfo &TLI,
             std::unordered_multimap<Comdat *, GlobalValue *> &ComdatMembers,
             BranchProbabilityInfo *BPI, BlockFrequencyInfo *BFIin,
             ProfileSummaryInfo *PSI, bool IsCS, bool InstrumentFuncEntry,
             bool HasSingleByteCoverage)
      : F(Func), M(Modu), BFI(BFIin), PSI(PSI),
        FuncInfo(Func, TLI, ComdatMembers, false, BPI, BFIin, IsCS,
                 InstrumentFuncEntry, HasSingleByteCoverage),
        FreqAttr(FFA_Normal), IsCS(IsCS), VPC(Func, TLI) {}

  void handleInstrProfError(Error Err, uint64_t MismatchedFuncSum);

  // Read counts for the instrumented BB from profile.
  bool readCounters(IndexedInstrProfReader *PGOReader, bool &AllZeros,
                    InstrProfRecord::CountPseudoKind &PseudoKind);

  // Populate the counts for all BBs.
  void populateCounters();

  // Set block coverage based on profile coverage values.
  void populateCoverage(IndexedInstrProfReader *PGOReader);

  // Set the branch weights based on the count values.
  void setBranchWeights();

  // Annotate the value profile call sites for all value kind.
  void annotateValueSites();

  // Annotate the value profile call sites for one value kind.
  void annotateValueSites(uint32_t Kind);

  // Annotate the irreducible loop header weights.
  void annotateIrrLoopHeaderWeights();

  // The hotness of the function from the profile count.
  enum FuncFreqAttr { FFA_Normal, FFA_Cold, FFA_Hot };

  // Return the function hotness from the profile.
  FuncFreqAttr getFuncFreqAttr() const { return FreqAttr; }

  // Return the function hash.
  uint64_t getFuncHash() const { return FuncInfo.FunctionHash; }

  // Return the profile record for this function;
  InstrProfRecord &getProfileRecord() { return ProfileRecord; }

  // Return the auxiliary BB information.
  PGOUseBBInfo &getBBInfo(const BasicBlock *BB) const {
    return FuncInfo.getBBInfo(BB);
  }

  // Return the auxiliary BB information if available.
  PGOUseBBInfo *findBBInfo(const BasicBlock *BB) const {
    return FuncInfo.findBBInfo(BB);
  }

  Function &getFunc() const { return F; }

  void dumpInfo(StringRef Str = "") const { FuncInfo.dumpInfo(Str); }

  uint64_t getProgramMaxCount() const { return ProgramMaxCount; }

private:
  Function &F;
  Module *M;
  BlockFrequencyInfo *BFI;
  ProfileSummaryInfo *PSI;

  // This member stores the shared information with class PGOGenFunc.
  FuncPGOInstrumentation<PGOUseEdge, PGOUseBBInfo> FuncInfo;

  // The maximum count value in the profile. This is only used in PGO use
  // compilation.
  uint64_t ProgramMaxCount;

  // Position of counter that remains to be read.
  uint32_t CountPosition = 0;

  // Total size of the profile count for this function.
  uint32_t ProfileCountSize = 0;

  // ProfileRecord for this function.
  InstrProfRecord ProfileRecord;

  // Function hotness info derived from profile.
  FuncFreqAttr FreqAttr;

  // Is to use the context sensitive profile.
  bool IsCS;

  ValueProfileCollector VPC;

  // Find the Instrumented BB and set the value. Return false on error.
  bool setInstrumentedCounts(const std::vector<uint64_t> &CountFromProfile);

  // Set the edge counter value for the unknown edge -- there should be only
  // one unknown edge.
  void setEdgeCount(DirectEdges &Edges, uint64_t Value);

  // Set the hot/cold inline hints based on the count values.
  // FIXME: This function should be removed once the functionality in
  // the inliner is implemented.
  void markFunctionAttributes(uint64_t EntryCount, uint64_t MaxCount) {
    if (PSI->isHotCount(EntryCount))
      FreqAttr = FFA_Hot;
    else if (PSI->isColdCount(MaxCount))
      FreqAttr = FFA_Cold;
  }
};

} // end anonymous namespace

/// Set up InEdges/OutEdges for all BBs in the MST.
static void setupBBInfoEdges(
    const FuncPGOInstrumentation<PGOUseEdge, PGOUseBBInfo> &FuncInfo) {
  // This is not required when there is block coverage inference.
  if (FuncInfo.BCI)
    return;
  for (const auto &E : FuncInfo.MST.allEdges()) {
    if (E->Removed)
      continue;
    const BasicBlock *SrcBB = E->SrcBB;
    const BasicBlock *DestBB = E->DestBB;
    PGOUseBBInfo &SrcInfo = FuncInfo.getBBInfo(SrcBB);
    PGOUseBBInfo &DestInfo = FuncInfo.getBBInfo(DestBB);
    SrcInfo.addOutEdge(E.get());
    DestInfo.addInEdge(E.get());
  }
}

// Visit all the edges and assign the count value for the instrumented
// edges and the BB. Return false on error.
bool PGOUseFunc::setInstrumentedCounts(
    const std::vector<uint64_t> &CountFromProfile) {

  std::vector<BasicBlock *> InstrumentBBs;
  FuncInfo.getInstrumentBBs(InstrumentBBs);

  setupBBInfoEdges(FuncInfo);

  unsigned NumCounters =
      InstrumentBBs.size() + FuncInfo.SIVisitor.getNumOfSelectInsts();
  // The number of counters here should match the number of counters
  // in profile. Return if they mismatch.
  if (NumCounters != CountFromProfile.size()) {
    return false;
  }
  auto *FuncEntry = &*F.begin();

  // Set the profile count to the Instrumented BBs.
  uint32_t I = 0;
  for (BasicBlock *InstrBB : InstrumentBBs) {
    uint64_t CountValue = CountFromProfile[I++];
    PGOUseBBInfo &Info = getBBInfo(InstrBB);
    // If we reach here, we know that we have some nonzero count
    // values in this function. The entry count should not be 0.
    // Fix it if necessary.
    if (InstrBB == FuncEntry && CountValue == 0)
      CountValue = 1;
    Info.setBBInfoCount(CountValue);
  }
  ProfileCountSize = CountFromProfile.size();
  CountPosition = I;

  // Set the edge count and update the count of unknown edges for BBs.
  auto setEdgeCount = [this](PGOUseEdge *E, uint64_t Value) -> void {
    E->setEdgeCount(Value);
    this->getBBInfo(E->SrcBB).UnknownCountOutEdge--;
    this->getBBInfo(E->DestBB).UnknownCountInEdge--;
  };

  // Set the profile count the Instrumented edges. There are BBs that not in
  // MST but not instrumented. Need to set the edge count value so that we can
  // populate the profile counts later.
  for (const auto &E : FuncInfo.MST.allEdges()) {
    if (E->Removed || E->InMST)
      continue;
    const BasicBlock *SrcBB = E->SrcBB;
    PGOUseBBInfo &SrcInfo = getBBInfo(SrcBB);

    // If only one out-edge, the edge profile count should be the same as BB
    // profile count.
    if (SrcInfo.Count && SrcInfo.OutEdges.size() == 1)
      setEdgeCount(E.get(), *SrcInfo.Count);
    else {
      const BasicBlock *DestBB = E->DestBB;
      PGOUseBBInfo &DestInfo = getBBInfo(DestBB);
      // If only one in-edge, the edge profile count should be the same as BB
      // profile count.
      if (DestInfo.Count && DestInfo.InEdges.size() == 1)
        setEdgeCount(E.get(), *DestInfo.Count);
    }
    if (E->Count)
      continue;
    // E's count should have been set from profile. If not, this meenas E skips
    // the instrumentation. We set the count to 0.
    setEdgeCount(E.get(), 0);
  }
  return true;
}

// Set the count value for the unknown edge. There should be one and only one
// unknown edge in Edges vector.
void PGOUseFunc::setEdgeCount(DirectEdges &Edges, uint64_t Value) {
  for (auto &E : Edges) {
    if (E->Count)
      continue;
    E->setEdgeCount(Value);

    getBBInfo(E->SrcBB).UnknownCountOutEdge--;
    getBBInfo(E->DestBB).UnknownCountInEdge--;
    return;
  }
  llvm_unreachable("Cannot find the unknown count edge");
}

// Emit function metadata indicating PGO profile mismatch.
static void annotateFunctionWithHashMismatch(Function &F, LLVMContext &ctx) {
  const char MetadataName[] = "instr_prof_hash_mismatch";
  SmallVector<Metadata *, 2> Names;
  // If this metadata already exists, ignore.
  auto *Existing = F.getMetadata(LLVMContext::MD_annotation);
  if (Existing) {
    MDTuple *Tuple = cast<MDTuple>(Existing);
    for (const auto &N : Tuple->operands()) {
      if (N.equalsStr(MetadataName))
        return;
      Names.push_back(N.get());
    }
  }

  MDBuilder MDB(ctx);
  Names.push_back(MDB.createString(MetadataName));
  MDNode *MD = MDTuple::get(ctx, Names);
  F.setMetadata(LLVMContext::MD_annotation, MD);
}

void PGOUseFunc::handleInstrProfError(Error Err, uint64_t MismatchedFuncSum) {
  handleAllErrors(std::move(Err), [&](const InstrProfError &IPE) {
    auto &Ctx = M->getContext();
    auto Err = IPE.get();
    bool SkipWarning = false;
    LLVM_DEBUG(dbgs() << "Error in reading profile for Func "
                      << FuncInfo.FuncName << ": ");
    if (Err == instrprof_error::unknown_function) {
      IsCS ? NumOfCSPGOMissing++ : NumOfPGOMissing++;
      SkipWarning = !PGOWarnMissing;
      LLVM_DEBUG(dbgs() << "unknown function");
    } else if (Err == instrprof_error::hash_mismatch ||
               Err == instrprof_error::malformed) {
      IsCS ? NumOfCSPGOMismatch++ : NumOfPGOMismatch++;
      SkipWarning =
          NoPGOWarnMismatch ||
          (NoPGOWarnMismatchComdatWeak &&
           (F.hasComdat() || F.getLinkage() == GlobalValue::WeakAnyLinkage ||
            F.getLinkage() == GlobalValue::AvailableExternallyLinkage));
      LLVM_DEBUG(dbgs() << "hash mismatch (hash= " << FuncInfo.FunctionHash
                        << " skip=" << SkipWarning << ")");
      // Emit function metadata indicating PGO profile mismatch.
      annotateFunctionWithHashMismatch(F, M->getContext());
    }

    LLVM_DEBUG(dbgs() << " IsCS=" << IsCS << "\n");
    if (SkipWarning)
      return;

    std::string Msg =
        IPE.message() + std::string(" ") + F.getName().str() +
        std::string(" Hash = ") + std::to_string(FuncInfo.FunctionHash) +
        std::string(" up to ") + std::to_string(MismatchedFuncSum) +
        std::string(" count discarded");

    Ctx.diagnose(
        DiagnosticInfoPGOProfile(M->getName().data(), Msg, DS_Warning));
  });
}

// Read the profile from ProfileFileName and assign the value to the
// instrumented BB and the edges. This function also updates ProgramMaxCount.
// Return true if the profile are successfully read, and false on errors.
bool PGOUseFunc::readCounters(IndexedInstrProfReader *PGOReader, bool &AllZeros,
                              InstrProfRecord::CountPseudoKind &PseudoKind) {
  auto &Ctx = M->getContext();
  uint64_t MismatchedFuncSum = 0;
  Expected<InstrProfRecord> Result = PGOReader->getInstrProfRecord(
      FuncInfo.FuncName, FuncInfo.FunctionHash, FuncInfo.DeprecatedFuncName,
      &MismatchedFuncSum);
  if (Error E = Result.takeError()) {
    handleInstrProfError(std::move(E), MismatchedFuncSum);
    return false;
  }
  ProfileRecord = std::move(Result.get());
  PseudoKind = ProfileRecord.getCountPseudoKind();
  if (PseudoKind != InstrProfRecord::NotPseudo) {
    return true;
  }
  std::vector<uint64_t> &CountFromProfile = ProfileRecord.Counts;

  IsCS ? NumOfCSPGOFunc++ : NumOfPGOFunc++;
  LLVM_DEBUG(dbgs() << CountFromProfile.size() << " counts\n");

  uint64_t ValueSum = 0;
  for (unsigned I = 0, S = CountFromProfile.size(); I < S; I++) {
    LLVM_DEBUG(dbgs() << "  " << I << ": " << CountFromProfile[I] << "\n");
    ValueSum += CountFromProfile[I];
  }
  AllZeros = (ValueSum == 0);

  LLVM_DEBUG(dbgs() << "SUM =  " << ValueSum << "\n");

  getBBInfo(nullptr).UnknownCountOutEdge = 2;
  getBBInfo(nullptr).UnknownCountInEdge = 2;

  if (!setInstrumentedCounts(CountFromProfile)) {
    LLVM_DEBUG(
        dbgs() << "Inconsistent number of counts, skipping this function");
    Ctx.diagnose(DiagnosticInfoPGOProfile(
        M->getName().data(),
        Twine("Inconsistent number of counts in ") + F.getName().str() +
            Twine(": the profile may be stale or there is a function name "
                  "collision."),
        DS_Warning));
    return false;
  }
  ProgramMaxCount = PGOReader->getMaximumFunctionCount(IsCS);
  return true;
}

void PGOUseFunc::populateCoverage(IndexedInstrProfReader *PGOReader) {
  uint64_t MismatchedFuncSum = 0;
  Expected<InstrProfRecord> Result = PGOReader->getInstrProfRecord(
      FuncInfo.FuncName, FuncInfo.FunctionHash, FuncInfo.DeprecatedFuncName,
      &MismatchedFuncSum);
  if (auto Err = Result.takeError()) {
    handleInstrProfError(std::move(Err), MismatchedFuncSum);
    return;
  }
  IsCS ? NumOfCSPGOFunc++ : NumOfPGOFunc++;

  std::vector<uint64_t> &CountsFromProfile = Result.get().Counts;
  DenseMap<const BasicBlock *, bool> Coverage;
  unsigned Index = 0;
  for (auto &BB : F)
    if (FuncInfo.BCI->shouldInstrumentBlock(BB))
      Coverage[&BB] = (CountsFromProfile[Index++] != 0);
  assert(Index == CountsFromProfile.size());

  // For each B in InverseDependencies[A], if A is covered then B is covered.
  DenseMap<const BasicBlock *, DenseSet<const BasicBlock *>>
      InverseDependencies;
  for (auto &BB : F) {
    for (auto *Dep : FuncInfo.BCI->getDependencies(BB)) {
      // If Dep is covered then BB is covered.
      InverseDependencies[Dep].insert(&BB);
    }
  }

  // Infer coverage of the non-instrumented blocks using a flood-fill algorithm.
  std::stack<const BasicBlock *> CoveredBlocksToProcess;
  for (auto &[BB, IsCovered] : Coverage)
    if (IsCovered)
      CoveredBlocksToProcess.push(BB);

  while (!CoveredBlocksToProcess.empty()) {
    auto *CoveredBlock = CoveredBlocksToProcess.top();
    assert(Coverage[CoveredBlock]);
    CoveredBlocksToProcess.pop();
    for (auto *BB : InverseDependencies[CoveredBlock]) {
      // If CoveredBlock is covered then BB is covered.
      if (Coverage[BB])
        continue;
      Coverage[BB] = true;
      CoveredBlocksToProcess.push(BB);
    }
  }

  // Annotate block coverage.
  MDBuilder MDB(F.getContext());
  // We set the entry count to 10000 if the entry block is covered so that BFI
  // can propagate a fraction of this count to the other covered blocks.
  F.setEntryCount(Coverage[&F.getEntryBlock()] ? 10000 : 0);
  for (auto &BB : F) {
    // For a block A and its successor B, we set the edge weight as follows:
    // If A is covered and B is covered, set weight=1.
    // If A is covered and B is uncovered, set weight=0.
    // If A is uncovered, set weight=1.
    // This setup will allow BFI to give nonzero profile counts to only covered
    // blocks.
    SmallVector<uint32_t, 4> Weights;
    for (auto *Succ : successors(&BB))
      Weights.push_back((Coverage[Succ] || !Coverage[&BB]) ? 1 : 0);
    if (Weights.size() >= 2)
      llvm::setBranchWeights(*BB.getTerminator(), Weights,
                             /*IsExpected=*/false);
  }

  unsigned NumCorruptCoverage = 0;
  DominatorTree DT(F);
  LoopInfo LI(DT);
  BranchProbabilityInfo BPI(F, LI);
  BlockFrequencyInfo BFI(F, BPI, LI);
  auto IsBlockDead = [&](const BasicBlock &BB) -> std::optional<bool> {
    if (auto C = BFI.getBlockProfileCount(&BB))
      return C == 0;
    return {};
  };
  LLVM_DEBUG(dbgs() << "Block Coverage: (Instrumented=*, Covered=X)\n");
  for (auto &BB : F) {
    LLVM_DEBUG(dbgs() << (FuncInfo.BCI->shouldInstrumentBlock(BB) ? "* " : "  ")
                      << (Coverage[&BB] ? "X " : "  ") << " " << BB.getName()
                      << "\n");
    // In some cases it is possible to find a covered block that has no covered
    // successors, e.g., when a block calls a function that may call exit(). In
    // those cases, BFI could find its successor to be covered while BCI could
    // find its successor to be dead.
    if (Coverage[&BB] == IsBlockDead(BB).value_or(false)) {
      LLVM_DEBUG(
          dbgs() << "Found inconsistent block covearge for " << BB.getName()
                 << ": BCI=" << (Coverage[&BB] ? "Covered" : "Dead") << " BFI="
                 << (IsBlockDead(BB).value() ? "Dead" : "Covered") << "\n");
      ++NumCorruptCoverage;
    }
    if (Coverage[&BB])
      ++NumCoveredBlocks;
  }
  if (PGOVerifyBFI && NumCorruptCoverage) {
    auto &Ctx = M->getContext();
    Ctx.diagnose(DiagnosticInfoPGOProfile(
        M->getName().data(),
        Twine("Found inconsistent block coverage for function ") + F.getName() +
            " in " + Twine(NumCorruptCoverage) + " blocks.",
        DS_Warning));
  }
  if (PGOViewBlockCoverageGraph)
    FuncInfo.BCI->viewBlockCoverageGraph(&Coverage);
}

// Populate the counters from instrumented BBs to all BBs.
// In the end of this operation, all BBs should have a valid count value.
void PGOUseFunc::populateCounters() {
  bool Changes = true;
  unsigned NumPasses = 0;
  while (Changes) {
    NumPasses++;
    Changes = false;

    // For efficient traversal, it's better to start from the end as most
    // of the instrumented edges are at the end.
    for (auto &BB : reverse(F)) {
      PGOUseBBInfo *UseBBInfo = findBBInfo(&BB);
      if (UseBBInfo == nullptr)
        continue;
      if (!UseBBInfo->Count) {
        if (UseBBInfo->UnknownCountOutEdge == 0) {
          UseBBInfo->Count = sumEdgeCount(UseBBInfo->OutEdges);
          Changes = true;
        } else if (UseBBInfo->UnknownCountInEdge == 0) {
          UseBBInfo->Count = sumEdgeCount(UseBBInfo->InEdges);
          Changes = true;
        }
      }
      if (UseBBInfo->Count) {
        if (UseBBInfo->UnknownCountOutEdge == 1) {
          uint64_t Total = 0;
          uint64_t OutSum = sumEdgeCount(UseBBInfo->OutEdges);
          // If the one of the successor block can early terminate (no-return),
          // we can end up with situation where out edge sum count is larger as
          // the source BB's count is collected by a post-dominated block.
          if (*UseBBInfo->Count > OutSum)
            Total = *UseBBInfo->Count - OutSum;
          setEdgeCount(UseBBInfo->OutEdges, Total);
          Changes = true;
        }
        if (UseBBInfo->UnknownCountInEdge == 1) {
          uint64_t Total = 0;
          uint64_t InSum = sumEdgeCount(UseBBInfo->InEdges);
          if (*UseBBInfo->Count > InSum)
            Total = *UseBBInfo->Count - InSum;
          setEdgeCount(UseBBInfo->InEdges, Total);
          Changes = true;
        }
      }
    }
  }

  LLVM_DEBUG(dbgs() << "Populate counts in " << NumPasses << " passes.\n");
  (void)NumPasses;
#ifndef NDEBUG
  // Assert every BB has a valid counter.
  for (auto &BB : F) {
    auto BI = findBBInfo(&BB);
    if (BI == nullptr)
      continue;
    assert(BI->Count && "BB count is not valid");
  }
#endif
  uint64_t FuncEntryCount = *getBBInfo(&*F.begin()).Count;
  uint64_t FuncMaxCount = FuncEntryCount;
  for (auto &BB : F) {
    auto BI = findBBInfo(&BB);
    if (BI == nullptr)
      continue;
    FuncMaxCount = std::max(FuncMaxCount, *BI->Count);
  }

  // Fix the obviously inconsistent entry count.
  if (FuncMaxCount > 0 && FuncEntryCount == 0)
    FuncEntryCount = 1;
  F.setEntryCount(ProfileCount(FuncEntryCount, Function::PCT_Real));
  markFunctionAttributes(FuncEntryCount, FuncMaxCount);

  // Now annotate select instructions
  FuncInfo.SIVisitor.annotateSelects(this, &CountPosition);
  assert(CountPosition == ProfileCountSize);

  LLVM_DEBUG(FuncInfo.dumpInfo("after reading profile."));
}

// Assign the scaled count values to the BB with multiple out edges.
void PGOUseFunc::setBranchWeights() {
  // Generate MD_prof metadata for every branch instruction.
  LLVM_DEBUG(dbgs() << "\nSetting branch weights for func " << F.getName()
                    << " IsCS=" << IsCS << "\n");
  for (auto &BB : F) {
    Instruction *TI = BB.getTerminator();
    if (TI->getNumSuccessors() < 2)
      continue;
    if (!(isa<BranchInst>(TI) || isa<SwitchInst>(TI) ||
          isa<IndirectBrInst>(TI) || isa<InvokeInst>(TI) ||
          isa<CallBrInst>(TI)))
      continue;

    const PGOUseBBInfo &BBCountInfo = getBBInfo(&BB);
    if (!*BBCountInfo.Count)
      continue;

    // We have a non-zero Branch BB.

    // SuccessorCount can be greater than OutEdgesCount, because
    // removed edges don't appear in OutEdges.
    unsigned OutEdgesCount = BBCountInfo.OutEdges.size();
    unsigned SuccessorCount = BB.getTerminator()->getNumSuccessors();
    assert(OutEdgesCount <= SuccessorCount);

    SmallVector<uint64_t, 2> EdgeCounts(SuccessorCount, 0);
    uint64_t MaxCount = 0;
    for (unsigned It = 0; It < OutEdgesCount; It++) {
      const PGOUseEdge *E = BBCountInfo.OutEdges[It];
      const BasicBlock *SrcBB = E->SrcBB;
      const BasicBlock *DestBB = E->DestBB;
      if (DestBB == nullptr)
        continue;
      unsigned SuccNum = GetSuccessorNumber(SrcBB, DestBB);
      uint64_t EdgeCount = *E->Count;
      if (EdgeCount > MaxCount)
        MaxCount = EdgeCount;
      EdgeCounts[SuccNum] = EdgeCount;
    }

    if (MaxCount)
      setProfMetadata(M, TI, EdgeCounts, MaxCount);
    else {
      // A zero MaxCount can come about when we have a BB with a positive
      // count, and whose successor blocks all have 0 count. This can happen
      // when there is no exit block and the code exits via a noreturn function.
      auto &Ctx = M->getContext();
      Ctx.diagnose(DiagnosticInfoPGOProfile(
          M->getName().data(),
          Twine("Profile in ") + F.getName().str() +
              Twine(" partially ignored") +
              Twine(", possibly due to the lack of a return path."),
          DS_Warning));
    }
  }
}

static bool isIndirectBrTarget(BasicBlock *BB) {
  for (BasicBlock *Pred : predecessors(BB)) {
    if (isa<IndirectBrInst>(Pred->getTerminator()))
      return true;
  }
  return false;
}

void PGOUseFunc::annotateIrrLoopHeaderWeights() {
  LLVM_DEBUG(dbgs() << "\nAnnotating irreducible loop header weights.\n");
  // Find irr loop headers
  for (auto &BB : F) {
    // As a heuristic also annotate indrectbr targets as they have a high chance
    // to become an irreducible loop header after the indirectbr tail
    // duplication.
    if (BFI->isIrrLoopHeader(&BB) || isIndirectBrTarget(&BB)) {
      Instruction *TI = BB.getTerminator();
      const PGOUseBBInfo &BBCountInfo = getBBInfo(&BB);
      setIrrLoopHeaderMetadata(M, TI, *BBCountInfo.Count);
    }
  }
}

void SelectInstVisitor::instrumentOneSelectInst(SelectInst &SI) {
  Module *M = F.getParent();
  IRBuilder<> Builder(&SI);
  Type *Int64Ty = Builder.getInt64Ty();
  auto *Step = Builder.CreateZExt(SI.getCondition(), Int64Ty);
  Builder.CreateCall(
      Intrinsic::getDeclaration(M, Intrinsic::instrprof_increment_step),
      {FuncNameVar, Builder.getInt64(FuncHash), Builder.getInt32(TotalNumCtrs),
       Builder.getInt32(*CurCtrIdx), Step});
  ++(*CurCtrIdx);
}

void SelectInstVisitor::annotateOneSelectInst(SelectInst &SI) {
  std::vector<uint64_t> &CountFromProfile = UseFunc->getProfileRecord().Counts;
  assert(*CurCtrIdx < CountFromProfile.size() &&
         "Out of bound access of counters");
  uint64_t SCounts[2];
  SCounts[0] = CountFromProfile[*CurCtrIdx]; // True count
  ++(*CurCtrIdx);
  uint64_t TotalCount = 0;
  auto BI = UseFunc->findBBInfo(SI.getParent());
  if (BI != nullptr)
    TotalCount = *BI->Count;
  // False Count
  SCounts[1] = (TotalCount > SCounts[0] ? TotalCount - SCounts[0] : 0);
  uint64_t MaxCount = std::max(SCounts[0], SCounts[1]);
  if (MaxCount)
    setProfMetadata(F.getParent(), &SI, SCounts, MaxCount);
}

void SelectInstVisitor::visitSelectInst(SelectInst &SI) {
  if (!PGOInstrSelect || PGOFunctionEntryCoverage || HasSingleByteCoverage)
    return;
  // FIXME: do not handle this yet.
  if (SI.getCondition()->getType()->isVectorTy())
    return;

  switch (Mode) {
  case VM_counting:
    NSIs++;
    return;
  case VM_instrument:
    instrumentOneSelectInst(SI);
    return;
  case VM_annotate:
    annotateOneSelectInst(SI);
    return;
  }

  llvm_unreachable("Unknown visiting mode");
}

static uint32_t getMaxNumAnnotations(InstrProfValueKind ValueProfKind) {
  if (ValueProfKind == IPVK_MemOPSize)
    return MaxNumMemOPAnnotations;
  if (ValueProfKind == llvm::IPVK_VTableTarget)
    return MaxNumVTableAnnotations;
  return MaxNumAnnotations;
}

// Traverse all valuesites and annotate the instructions for all value kind.
void PGOUseFunc::annotateValueSites() {
  if (isValueProfilingDisabled())
    return;

  // Create the PGOFuncName meta data.
  createPGOFuncNameMetadata(F, FuncInfo.FuncName);

  for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind)
    annotateValueSites(Kind);
}

// Annotate the instructions for a specific value kind.
void PGOUseFunc::annotateValueSites(uint32_t Kind) {
  assert(Kind <= IPVK_Last);
  unsigned ValueSiteIndex = 0;

  unsigned NumValueSites = ProfileRecord.getNumValueSites(Kind);

  // Since there isn't a reliable or fast way for profile reader to tell if a
  // profile is generated with `-enable-vtable-value-profiling` on, we run the
  // value profile collector over the function IR to find the instrumented sites
  // iff function profile records shows the number of instrumented vtable sites
  // is not zero. Function cfg already takes the number of instrumented
  // indirect call sites into account so it doesn't hash the number of
  // instrumented vtables; as a side effect it makes it easier to enable
  // profiling and profile use in two steps if needed.
  // TODO: Remove this if/when -enable-vtable-value-profiling is on by default.
  if (NumValueSites > 0 && Kind == IPVK_VTableTarget &&
      NumValueSites != FuncInfo.ValueSites[IPVK_VTableTarget].size() &&
      MaxNumVTableAnnotations != 0)
    FuncInfo.ValueSites[IPVK_VTableTarget] = VPC.get(IPVK_VTableTarget);
  auto &ValueSites = FuncInfo.ValueSites[Kind];
  if (NumValueSites != ValueSites.size()) {
    auto &Ctx = M->getContext();
    Ctx.diagnose(DiagnosticInfoPGOProfile(
        M->getName().data(),
        Twine("Inconsistent number of value sites for ") +
            Twine(ValueProfKindDescr[Kind]) + Twine(" profiling in \"") +
            F.getName().str() +
            Twine("\", possibly due to the use of a stale profile."),
        DS_Warning));
    return;
  }

  for (VPCandidateInfo &I : ValueSites) {
    LLVM_DEBUG(dbgs() << "Read one value site profile (kind = " << Kind
                      << "): Index = " << ValueSiteIndex << " out of "
                      << NumValueSites << "\n");
    annotateValueSite(
        *M, *I.AnnotatedInst, ProfileRecord,
        static_cast<InstrProfValueKind>(Kind), ValueSiteIndex,
        getMaxNumAnnotations(static_cast<InstrProfValueKind>(Kind)));
    ValueSiteIndex++;
  }
}

// Collect the set of members for each Comdat in module M and store
// in ComdatMembers.
static void collectComdatMembers(
    Module &M,
    std::unordered_multimap<Comdat *, GlobalValue *> &ComdatMembers) {
  if (!DoComdatRenaming)
    return;
  for (Function &F : M)
    if (Comdat *C = F.getComdat())
      ComdatMembers.insert(std::make_pair(C, &F));
  for (GlobalVariable &GV : M.globals())
    if (Comdat *C = GV.getComdat())
      ComdatMembers.insert(std::make_pair(C, &GV));
  for (GlobalAlias &GA : M.aliases())
    if (Comdat *C = GA.getComdat())
      ComdatMembers.insert(std::make_pair(C, &GA));
}

// Return true if we should not find instrumentation data for this function
static bool skipPGOUse(const Function &F) {
  if (F.isDeclaration())
    return true;
  // If there are too many critical edges, PGO might cause
  // compiler time problem. Skip PGO if the number of
  // critical edges execeed the threshold.
  unsigned NumCriticalEdges = 0;
  for (auto &BB : F) {
    const Instruction *TI = BB.getTerminator();
    for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I) {
      if (isCriticalEdge(TI, I))
        NumCriticalEdges++;
    }
  }
  if (NumCriticalEdges > PGOFunctionCriticalEdgeThreshold) {
    LLVM_DEBUG(dbgs() << "In func " << F.getName()
                      << ", NumCriticalEdges=" << NumCriticalEdges
                      << " exceed the threshold. Skip PGO.\n");
    return true;
  }
  return false;
}

// Return true if we should not instrument this function
static bool skipPGOGen(const Function &F) {
  if (skipPGOUse(F))
    return true;
  if (F.hasFnAttribute(llvm::Attribute::Naked))
    return true;
  if (F.hasFnAttribute(llvm::Attribute::NoProfile))
    return true;
  if (F.hasFnAttribute(llvm::Attribute::SkipProfile))
    return true;
  if (F.getInstructionCount() < PGOFunctionSizeThreshold)
    return true;
  return false;
}

static bool InstrumentAllFunctions(
    Module &M, function_ref<TargetLibraryInfo &(Function &)> LookupTLI,
    function_ref<BranchProbabilityInfo *(Function &)> LookupBPI,
    function_ref<BlockFrequencyInfo *(Function &)> LookupBFI, bool IsCS) {
  // For the context-sensitve instrumentation, we should have a separated pass
  // (before LTO/ThinLTO linking) to create these variables.
  if (!IsCS && !PGOCtxProfLoweringPass::isContextualIRPGOEnabled())
    createIRLevelProfileFlagVar(M, /*IsCS=*/false);

  Triple TT(M.getTargetTriple());
  LLVMContext &Ctx = M.getContext();
  if (!TT.isOSBinFormatELF() && EnableVTableValueProfiling)
    Ctx.diagnose(DiagnosticInfoPGOProfile(
        M.getName().data(),
        Twine("VTable value profiling is presently not "
              "supported for non-ELF object formats"),
        DS_Warning));
  std::unordered_multimap<Comdat *, GlobalValue *> ComdatMembers;
  collectComdatMembers(M, ComdatMembers);

  for (auto &F : M) {
    if (skipPGOGen(F))
      continue;
    auto &TLI = LookupTLI(F);
    auto *BPI = LookupBPI(F);
    auto *BFI = LookupBFI(F);
    instrumentOneFunc(F, &M, TLI, BPI, BFI, ComdatMembers, IsCS);
  }
  return true;
}

PreservedAnalyses
PGOInstrumentationGenCreateVar::run(Module &M, ModuleAnalysisManager &MAM) {
  createProfileFileNameVar(M, CSInstrName);
  // The variable in a comdat may be discarded by LTO. Ensure the declaration
  // will be retained.
  appendToCompilerUsed(M, createIRLevelProfileFlagVar(M, /*IsCS=*/true));
  if (ProfileSampling)
    createProfileSamplingVar(M);
  PreservedAnalyses PA;
  PA.preserve<FunctionAnalysisManagerModuleProxy>();
  PA.preserveSet<AllAnalysesOn<Function>>();
  return PA;
}

PreservedAnalyses PGOInstrumentationGen::run(Module &M,
                                             ModuleAnalysisManager &MAM) {
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto LookupTLI = [&FAM](Function &F) -> TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };
  auto LookupBPI = [&FAM](Function &F) {
    return &FAM.getResult<BranchProbabilityAnalysis>(F);
  };
  auto LookupBFI = [&FAM](Function &F) {
    return &FAM.getResult<BlockFrequencyAnalysis>(F);
  };

  if (!InstrumentAllFunctions(M, LookupTLI, LookupBPI, LookupBFI, IsCS))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

// Using the ratio b/w sums of profile count values and BFI count values to
// adjust the func entry count.
static void fixFuncEntryCount(PGOUseFunc &Func, LoopInfo &LI,
                              BranchProbabilityInfo &NBPI) {
  Function &F = Func.getFunc();
  BlockFrequencyInfo NBFI(F, NBPI, LI);
#ifndef NDEBUG
  auto BFIEntryCount = F.getEntryCount();
  assert(BFIEntryCount && (BFIEntryCount->getCount() > 0) &&
         "Invalid BFI Entrycount");
#endif
  auto SumCount = APFloat::getZero(APFloat::IEEEdouble());
  auto SumBFICount = APFloat::getZero(APFloat::IEEEdouble());
  for (auto &BBI : F) {
    uint64_t CountValue = 0;
    uint64_t BFICountValue = 0;
    if (!Func.findBBInfo(&BBI))
      continue;
    auto BFICount = NBFI.getBlockProfileCount(&BBI);
    CountValue = *Func.getBBInfo(&BBI).Count;
    BFICountValue = *BFICount;
    SumCount.add(APFloat(CountValue * 1.0), APFloat::rmNearestTiesToEven);
    SumBFICount.add(APFloat(BFICountValue * 1.0), APFloat::rmNearestTiesToEven);
  }
  if (SumCount.isZero())
    return;

  assert(SumBFICount.compare(APFloat(0.0)) == APFloat::cmpGreaterThan &&
         "Incorrect sum of BFI counts");
  if (SumBFICount.compare(SumCount) == APFloat::cmpEqual)
    return;
  double Scale = (SumCount / SumBFICount).convertToDouble();
  if (Scale < 1.001 && Scale > 0.999)
    return;

  uint64_t FuncEntryCount = *Func.getBBInfo(&*F.begin()).Count;
  uint64_t NewEntryCount = 0.5 + FuncEntryCount * Scale;
  if (NewEntryCount == 0)
    NewEntryCount = 1;
  if (NewEntryCount != FuncEntryCount) {
    F.setEntryCount(ProfileCount(NewEntryCount, Function::PCT_Real));
    LLVM_DEBUG(dbgs() << "FixFuncEntryCount: in " << F.getName()
                      << ", entry_count " << FuncEntryCount << " --> "
                      << NewEntryCount << "\n");
  }
}

// Compare the profile count values with BFI count values, and print out
// the non-matching ones.
static void verifyFuncBFI(PGOUseFunc &Func, LoopInfo &LI,
                          BranchProbabilityInfo &NBPI,
                          uint64_t HotCountThreshold,
                          uint64_t ColdCountThreshold) {
  Function &F = Func.getFunc();
  BlockFrequencyInfo NBFI(F, NBPI, LI);
  //  bool PrintFunc = false;
  bool HotBBOnly = PGOVerifyHotBFI;
  StringRef Msg;
  OptimizationRemarkEmitter ORE(&F);

  unsigned BBNum = 0, BBMisMatchNum = 0, NonZeroBBNum = 0;
  for (auto &BBI : F) {
    uint64_t CountValue = 0;
    uint64_t BFICountValue = 0;

    CountValue = Func.getBBInfo(&BBI).Count.value_or(CountValue);

    BBNum++;
    if (CountValue)
      NonZeroBBNum++;
    auto BFICount = NBFI.getBlockProfileCount(&BBI);
    if (BFICount)
      BFICountValue = *BFICount;

    if (HotBBOnly) {
      bool rawIsHot = CountValue >= HotCountThreshold;
      bool BFIIsHot = BFICountValue >= HotCountThreshold;
      bool rawIsCold = CountValue <= ColdCountThreshold;
      bool ShowCount = false;
      if (rawIsHot && !BFIIsHot) {
        Msg = "raw-Hot to BFI-nonHot";
        ShowCount = true;
      } else if (rawIsCold && BFIIsHot) {
        Msg = "raw-Cold to BFI-Hot";
        ShowCount = true;
      }
      if (!ShowCount)
        continue;
    } else {
      if ((CountValue < PGOVerifyBFICutoff) &&
          (BFICountValue < PGOVerifyBFICutoff))
        continue;
      uint64_t Diff = (BFICountValue >= CountValue)
                          ? BFICountValue - CountValue
                          : CountValue - BFICountValue;
      if (Diff <= CountValue / 100 * PGOVerifyBFIRatio)
        continue;
    }
    BBMisMatchNum++;

    ORE.emit([&]() {
      OptimizationRemarkAnalysis Remark(DEBUG_TYPE, "bfi-verify",
                                        F.getSubprogram(), &BBI);
      Remark << "BB " << ore::NV("Block", BBI.getName())
             << " Count=" << ore::NV("Count", CountValue)
             << " BFI_Count=" << ore::NV("Count", BFICountValue);
      if (!Msg.empty())
        Remark << " (" << Msg << ")";
      return Remark;
    });
  }
  if (BBMisMatchNum)
    ORE.emit([&]() {
      return OptimizationRemarkAnalysis(DEBUG_TYPE, "bfi-verify",
                                        F.getSubprogram(), &F.getEntryBlock())
             << "In Func " << ore::NV("Function", F.getName())
             << ": Num_of_BB=" << ore::NV("Count", BBNum)
             << ", Num_of_non_zerovalue_BB=" << ore::NV("Count", NonZeroBBNum)
             << ", Num_of_mis_matching_BB=" << ore::NV("Count", BBMisMatchNum);
    });
}

static bool annotateAllFunctions(
    Module &M, StringRef ProfileFileName, StringRef ProfileRemappingFileName,
    vfs::FileSystem &FS,
    function_ref<TargetLibraryInfo &(Function &)> LookupTLI,
    function_ref<BranchProbabilityInfo *(Function &)> LookupBPI,
    function_ref<BlockFrequencyInfo *(Function &)> LookupBFI,
    ProfileSummaryInfo *PSI, bool IsCS) {
  LLVM_DEBUG(dbgs() << "Read in profile counters: ");
  auto &Ctx = M.getContext();
  // Read the counter array from file.
  auto ReaderOrErr = IndexedInstrProfReader::create(ProfileFileName, FS,
                                                    ProfileRemappingFileName);
  if (Error E = ReaderOrErr.takeError()) {
    handleAllErrors(std::move(E), [&](const ErrorInfoBase &EI) {
      Ctx.diagnose(
          DiagnosticInfoPGOProfile(ProfileFileName.data(), EI.message()));
    });
    return false;
  }

  std::unique_ptr<IndexedInstrProfReader> PGOReader =
      std::move(ReaderOrErr.get());
  if (!PGOReader) {
    Ctx.diagnose(DiagnosticInfoPGOProfile(ProfileFileName.data(),
                                          StringRef("Cannot get PGOReader")));
    return false;
  }
  if (!PGOReader->hasCSIRLevelProfile() && IsCS)
    return false;

  // TODO: might need to change the warning once the clang option is finalized.
  if (!PGOReader->isIRLevelProfile()) {
    Ctx.diagnose(DiagnosticInfoPGOProfile(
        ProfileFileName.data(), "Not an IR level instrumentation profile"));
    return false;
  }
  if (PGOReader->functionEntryOnly()) {
    Ctx.diagnose(DiagnosticInfoPGOProfile(
        ProfileFileName.data(),
        "Function entry profiles are not yet supported for optimization"));
    return false;
  }

  if (EnableVTableProfileUse) {
    for (GlobalVariable &G : M.globals()) {
      if (!G.hasName() || !G.hasMetadata(LLVMContext::MD_type))
        continue;

      // Create the PGOFuncName meta data.
      createPGONameMetadata(G, getPGOName(G, false /* InLTO*/));
    }
  }

  // Add the profile summary (read from the header of the indexed summary) here
  // so that we can use it below when reading counters (which checks if the
  // function should be marked with a cold or inlinehint attribute).
  M.setProfileSummary(PGOReader->getSummary(IsCS).getMD(M.getContext()),
                      IsCS ? ProfileSummary::PSK_CSInstr
                           : ProfileSummary::PSK_Instr);
  PSI->refresh();

  std::unordered_multimap<Comdat *, GlobalValue *> ComdatMembers;
  collectComdatMembers(M, ComdatMembers);
  std::vector<Function *> HotFunctions;
  std::vector<Function *> ColdFunctions;

  // If the profile marked as always instrument the entry BB, do the
  // same. Note this can be overwritten by the internal option in CFGMST.h
  bool InstrumentFuncEntry = PGOReader->instrEntryBBEnabled();
  if (PGOInstrumentEntry.getNumOccurrences() > 0)
    InstrumentFuncEntry = PGOInstrumentEntry;
  InstrumentFuncEntry |= PGOCtxProfLoweringPass::isContextualIRPGOEnabled();

  bool HasSingleByteCoverage = PGOReader->hasSingleByteCoverage();
  for (auto &F : M) {
    if (skipPGOUse(F))
      continue;
    auto &TLI = LookupTLI(F);
    auto *BPI = LookupBPI(F);
    auto *BFI = LookupBFI(F);
    if (!HasSingleByteCoverage) {
      // Split indirectbr critical edges here before computing the MST rather
      // than later in getInstrBB() to avoid invalidating it.
      SplitIndirectBrCriticalEdges(F, /*IgnoreBlocksWithoutPHI=*/false, BPI,
                                   BFI);
    }
    PGOUseFunc Func(F, &M, TLI, ComdatMembers, BPI, BFI, PSI, IsCS,
                    InstrumentFuncEntry, HasSingleByteCoverage);
    if (HasSingleByteCoverage) {
      Func.populateCoverage(PGOReader.get());
      continue;
    }
    // When PseudoKind is set to a vaule other than InstrProfRecord::NotPseudo,
    // it means the profile for the function is unrepresentative and this
    // function is actually hot / warm. We will reset the function hot / cold
    // attribute and drop all the profile counters.
    InstrProfRecord::CountPseudoKind PseudoKind = InstrProfRecord::NotPseudo;
    bool AllZeros = false;
    if (!Func.readCounters(PGOReader.get(), AllZeros, PseudoKind))
      continue;
    if (AllZeros) {
      F.setEntryCount(ProfileCount(0, Function::PCT_Real));
      if (Func.getProgramMaxCount() != 0)
        ColdFunctions.push_back(&F);
      continue;
    }
    if (PseudoKind != InstrProfRecord::NotPseudo) {
      // Clear function attribute cold.
      if (F.hasFnAttribute(Attribute::Cold))
        F.removeFnAttr(Attribute::Cold);
      // Set function attribute as hot.
      if (PseudoKind == InstrProfRecord::PseudoHot)
        F.addFnAttr(Attribute::Hot);
      continue;
    }
    Func.populateCounters();
    Func.setBranchWeights();
    Func.annotateValueSites();
    Func.annotateIrrLoopHeaderWeights();
    PGOUseFunc::FuncFreqAttr FreqAttr = Func.getFuncFreqAttr();
    if (FreqAttr == PGOUseFunc::FFA_Cold)
      ColdFunctions.push_back(&F);
    else if (FreqAttr == PGOUseFunc::FFA_Hot)
      HotFunctions.push_back(&F);
    if (PGOViewCounts != PGOVCT_None &&
        (ViewBlockFreqFuncName.empty() ||
         F.getName() == ViewBlockFreqFuncName)) {
      LoopInfo LI{DominatorTree(F)};
      std::unique_ptr<BranchProbabilityInfo> NewBPI =
          std::make_unique<BranchProbabilityInfo>(F, LI);
      std::unique_ptr<BlockFrequencyInfo> NewBFI =
          std::make_unique<BlockFrequencyInfo>(F, *NewBPI, LI);
      if (PGOViewCounts == PGOVCT_Graph)
        NewBFI->view();
      else if (PGOViewCounts == PGOVCT_Text) {
        dbgs() << "pgo-view-counts: " << Func.getFunc().getName() << "\n";
        NewBFI->print(dbgs());
      }
    }
    if (PGOViewRawCounts != PGOVCT_None &&
        (ViewBlockFreqFuncName.empty() ||
         F.getName() == ViewBlockFreqFuncName)) {
      if (PGOViewRawCounts == PGOVCT_Graph)
        if (ViewBlockFreqFuncName.empty())
          WriteGraph(&Func, Twine("PGORawCounts_") + Func.getFunc().getName());
        else
          ViewGraph(&Func, Twine("PGORawCounts_") + Func.getFunc().getName());
      else if (PGOViewRawCounts == PGOVCT_Text) {
        dbgs() << "pgo-view-raw-counts: " << Func.getFunc().getName() << "\n";
        Func.dumpInfo();
      }
    }

    if (PGOVerifyBFI || PGOVerifyHotBFI || PGOFixEntryCount) {
      LoopInfo LI{DominatorTree(F)};
      BranchProbabilityInfo NBPI(F, LI);

      // Fix func entry count.
      if (PGOFixEntryCount)
        fixFuncEntryCount(Func, LI, NBPI);

      // Verify BlockFrequency information.
      uint64_t HotCountThreshold = 0, ColdCountThreshold = 0;
      if (PGOVerifyHotBFI) {
        HotCountThreshold = PSI->getOrCompHotCountThreshold();
        ColdCountThreshold = PSI->getOrCompColdCountThreshold();
      }
      verifyFuncBFI(Func, LI, NBPI, HotCountThreshold, ColdCountThreshold);
    }
  }

  // Set function hotness attribute from the profile.
  // We have to apply these attributes at the end because their presence
  // can affect the BranchProbabilityInfo of any callers, resulting in an
  // inconsistent MST between prof-gen and prof-use.
  for (auto &F : HotFunctions) {
    F->addFnAttr(Attribute::InlineHint);
    LLVM_DEBUG(dbgs() << "Set inline attribute to function: " << F->getName()
                      << "\n");
  }
  for (auto &F : ColdFunctions) {
    // Only set when there is no Attribute::Hot set by the user. For Hot
    // attribute, user's annotation has the precedence over the profile.
    if (F->hasFnAttribute(Attribute::Hot)) {
      auto &Ctx = M.getContext();
      std::string Msg = std::string("Function ") + F->getName().str() +
                        std::string(" is annotated as a hot function but"
                                    " the profile is cold");
      Ctx.diagnose(
          DiagnosticInfoPGOProfile(M.getName().data(), Msg, DS_Warning));
      continue;
    }
    F->addFnAttr(Attribute::Cold);
    LLVM_DEBUG(dbgs() << "Set cold attribute to function: " << F->getName()
                      << "\n");
  }
  return true;
}

PGOInstrumentationUse::PGOInstrumentationUse(
    std::string Filename, std::string RemappingFilename, bool IsCS,
    IntrusiveRefCntPtr<vfs::FileSystem> VFS)
    : ProfileFileName(std::move(Filename)),
      ProfileRemappingFileName(std::move(RemappingFilename)), IsCS(IsCS),
      FS(std::move(VFS)) {
  if (!PGOTestProfileFile.empty())
    ProfileFileName = PGOTestProfileFile;
  if (!PGOTestProfileRemappingFile.empty())
    ProfileRemappingFileName = PGOTestProfileRemappingFile;
  if (!FS)
    FS = vfs::getRealFileSystem();
}

PreservedAnalyses PGOInstrumentationUse::run(Module &M,
                                             ModuleAnalysisManager &MAM) {

  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto LookupTLI = [&FAM](Function &F) -> TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };
  auto LookupBPI = [&FAM](Function &F) {
    return &FAM.getResult<BranchProbabilityAnalysis>(F);
  };
  auto LookupBFI = [&FAM](Function &F) {
    return &FAM.getResult<BlockFrequencyAnalysis>(F);
  };

  auto *PSI = &MAM.getResult<ProfileSummaryAnalysis>(M);
  if (!annotateAllFunctions(M, ProfileFileName, ProfileRemappingFileName, *FS,
                            LookupTLI, LookupBPI, LookupBFI, PSI, IsCS))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

static std::string getSimpleNodeName(const BasicBlock *Node) {
  if (!Node->getName().empty())
    return Node->getName().str();

  std::string SimpleNodeName;
  raw_string_ostream OS(SimpleNodeName);
  Node->printAsOperand(OS, false);
  return SimpleNodeName;
}

void llvm::setProfMetadata(Module *M, Instruction *TI,
                           ArrayRef<uint64_t> EdgeCounts, uint64_t MaxCount) {
  assert(MaxCount > 0 && "Bad max count");
  uint64_t Scale = calculateCountScale(MaxCount);
  SmallVector<unsigned, 4> Weights;
  for (const auto &ECI : EdgeCounts)
    Weights.push_back(scaleBranchCount(ECI, Scale));

  LLVM_DEBUG(dbgs() << "Weight is: "; for (const auto &W
                                           : Weights) {
    dbgs() << W << " ";
  } dbgs() << "\n";);

  misexpect::checkExpectAnnotations(*TI, Weights, /*IsFrontend=*/false);

  setBranchWeights(*TI, Weights, /*IsExpected=*/false);
  if (EmitBranchProbability) {
    std::string BrCondStr = getBranchCondString(TI);
    if (BrCondStr.empty())
      return;

    uint64_t WSum =
        std::accumulate(Weights.begin(), Weights.end(), (uint64_t)0,
                        [](uint64_t w1, uint64_t w2) { return w1 + w2; });
    uint64_t TotalCount =
        std::accumulate(EdgeCounts.begin(), EdgeCounts.end(), (uint64_t)0,
                        [](uint64_t c1, uint64_t c2) { return c1 + c2; });
    Scale = calculateCountScale(WSum);
    BranchProbability BP(scaleBranchCount(Weights[0], Scale),
                         scaleBranchCount(WSum, Scale));
    std::string BranchProbStr;
    raw_string_ostream OS(BranchProbStr);
    OS << BP;
    OS << " (total count : " << TotalCount << ")";
    OS.flush();
    Function *F = TI->getParent()->getParent();
    OptimizationRemarkEmitter ORE(F);
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "pgo-instrumentation", TI)
             << BrCondStr << " is true with probability : " << BranchProbStr;
    });
  }
}

namespace llvm {

void setIrrLoopHeaderMetadata(Module *M, Instruction *TI, uint64_t Count) {
  MDBuilder MDB(M->getContext());
  TI->setMetadata(llvm::LLVMContext::MD_irr_loop,
                  MDB.createIrrLoopHeaderWeight(Count));
}

template <> struct GraphTraits<PGOUseFunc *> {
  using NodeRef = const BasicBlock *;
  using ChildIteratorType = const_succ_iterator;
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  static NodeRef getEntryNode(const PGOUseFunc *G) {
    return &G->getFunc().front();
  }

  static ChildIteratorType child_begin(const NodeRef N) {
    return succ_begin(N);
  }

  static ChildIteratorType child_end(const NodeRef N) { return succ_end(N); }

  static nodes_iterator nodes_begin(const PGOUseFunc *G) {
    return nodes_iterator(G->getFunc().begin());
  }

  static nodes_iterator nodes_end(const PGOUseFunc *G) {
    return nodes_iterator(G->getFunc().end());
  }
};

template <> struct DOTGraphTraits<PGOUseFunc *> : DefaultDOTGraphTraits {
  explicit DOTGraphTraits(bool isSimple = false)
      : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(const PGOUseFunc *G) {
    return std::string(G->getFunc().getName());
  }

  std::string getNodeLabel(const BasicBlock *Node, const PGOUseFunc *Graph) {
    std::string Result;
    raw_string_ostream OS(Result);

    OS << getSimpleNodeName(Node) << ":\\l";
    PGOUseBBInfo *BI = Graph->findBBInfo(Node);
    OS << "Count : ";
    if (BI && BI->Count)
      OS << *BI->Count << "\\l";
    else
      OS << "Unknown\\l";

    if (!PGOInstrSelect)
      return Result;

    for (const Instruction &I : *Node) {
      if (!isa<SelectInst>(&I))
        continue;
      // Display scaled counts for SELECT instruction:
      OS << "SELECT : { T = ";
      uint64_t TC, FC;
      bool HasProf = extractBranchWeights(I, TC, FC);
      if (!HasProf)
        OS << "Unknown, F = Unknown }\\l";
      else
        OS << TC << ", F = " << FC << " }\\l";
    }
    return Result;
  }
};

} // end namespace llvm
