//===- GVN.cpp - Eliminate redundant values and loads ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs global value numbering to eliminate fully redundant
// instructions.  It also performs simple dead load elimination.
//
// Note that this pass does the value numbering itself; it does not use the
// ValueNumbering analysis passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionPrecedenceTracking.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/VNCoercion.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

using namespace llvm;
using namespace llvm::gvn;
using namespace llvm::VNCoercion;
using namespace PatternMatch;

#define DEBUG_TYPE "gvn"

STATISTIC(NumGVNInstr, "Number of instructions deleted");
STATISTIC(NumGVNLoad, "Number of loads deleted");
STATISTIC(NumGVNPRE, "Number of instructions PRE'd");
STATISTIC(NumGVNBlocks, "Number of blocks merged");
STATISTIC(NumGVNSimpl, "Number of instructions simplified");
STATISTIC(NumGVNEqProp, "Number of equalities propagated");
STATISTIC(NumPRELoad, "Number of loads PRE'd");
STATISTIC(NumPRELoopLoad, "Number of loop loads PRE'd");
STATISTIC(NumPRELoadMoved2CEPred,
          "Number of loads moved to predecessor of a critical edge in PRE");

STATISTIC(IsValueFullyAvailableInBlockNumSpeculationsMax,
          "Number of blocks speculated as available in "
          "IsValueFullyAvailableInBlock(), max");
STATISTIC(MaxBBSpeculationCutoffReachedTimes,
          "Number of times we we reached gvn-max-block-speculations cut-off "
          "preventing further exploration");

static cl::opt<bool> GVNEnablePRE("enable-pre", cl::init(true), cl::Hidden);
static cl::opt<bool> GVNEnableLoadPRE("enable-load-pre", cl::init(true));
static cl::opt<bool> GVNEnableLoadInLoopPRE("enable-load-in-loop-pre",
                                            cl::init(true));
static cl::opt<bool>
GVNEnableSplitBackedgeInLoadPRE("enable-split-backedge-in-load-pre",
                                cl::init(false));
static cl::opt<bool> GVNEnableMemDep("enable-gvn-memdep", cl::init(true));

static cl::opt<uint32_t> MaxNumDeps(
    "gvn-max-num-deps", cl::Hidden, cl::init(100),
    cl::desc("Max number of dependences to attempt Load PRE (default = 100)"));

// This is based on IsValueFullyAvailableInBlockNumSpeculationsMax stat.
static cl::opt<uint32_t> MaxBBSpeculations(
    "gvn-max-block-speculations", cl::Hidden, cl::init(600),
    cl::desc("Max number of blocks we're willing to speculate on (and recurse "
             "into) when deducing if a value is fully available or not in GVN "
             "(default = 600)"));

static cl::opt<uint32_t> MaxNumVisitedInsts(
    "gvn-max-num-visited-insts", cl::Hidden, cl::init(100),
    cl::desc("Max number of visited instructions when trying to find "
             "dominating value of select dependency (default = 100)"));

static cl::opt<uint32_t> MaxNumInsnsPerBlock(
    "gvn-max-num-insns", cl::Hidden, cl::init(100),
    cl::desc("Max number of instructions to scan in each basic block in GVN "
             "(default = 100)"));

struct llvm::GVNPass::Expression {
  uint32_t opcode;
  bool commutative = false;
  // The type is not necessarily the result type of the expression, it may be
  // any additional type needed to disambiguate the expression.
  Type *type = nullptr;
  SmallVector<uint32_t, 4> varargs;

  Expression(uint32_t o = ~2U) : opcode(o) {}

  bool operator==(const Expression &other) const {
    if (opcode != other.opcode)
      return false;
    if (opcode == ~0U || opcode == ~1U)
      return true;
    if (type != other.type)
      return false;
    if (varargs != other.varargs)
      return false;
    return true;
  }

  friend hash_code hash_value(const Expression &Value) {
    return hash_combine(
        Value.opcode, Value.type,
        hash_combine_range(Value.varargs.begin(), Value.varargs.end()));
  }
};

namespace llvm {

template <> struct DenseMapInfo<GVNPass::Expression> {
  static inline GVNPass::Expression getEmptyKey() { return ~0U; }
  static inline GVNPass::Expression getTombstoneKey() { return ~1U; }

  static unsigned getHashValue(const GVNPass::Expression &e) {
    using llvm::hash_value;

    return static_cast<unsigned>(hash_value(e));
  }

  static bool isEqual(const GVNPass::Expression &LHS,
                      const GVNPass::Expression &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

/// Represents a particular available value that we know how to materialize.
/// Materialization of an AvailableValue never fails.  An AvailableValue is
/// implicitly associated with a rematerialization point which is the
/// location of the instruction from which it was formed.
struct llvm::gvn::AvailableValue {
  enum class ValType {
    SimpleVal, // A simple offsetted value that is accessed.
    LoadVal,   // A value produced by a load.
    MemIntrin, // A memory intrinsic which is loaded from.
    UndefVal,  // A UndefValue representing a value from dead block (which
               // is not yet physically removed from the CFG).
    SelectVal, // A pointer select which is loaded from and for which the load
               // can be replace by a value select.
  };

  /// Val - The value that is live out of the block.
  Value *Val;
  /// Kind of the live-out value.
  ValType Kind;

  /// Offset - The byte offset in Val that is interesting for the load query.
  unsigned Offset = 0;
  /// V1, V2 - The dominating non-clobbered values of SelectVal.
  Value *V1 = nullptr, *V2 = nullptr;

  static AvailableValue get(Value *V, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val = V;
    Res.Kind = ValType::SimpleVal;
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getMI(MemIntrinsic *MI, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val = MI;
    Res.Kind = ValType::MemIntrin;
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getLoad(LoadInst *Load, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val = Load;
    Res.Kind = ValType::LoadVal;
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getUndef() {
    AvailableValue Res;
    Res.Val = nullptr;
    Res.Kind = ValType::UndefVal;
    Res.Offset = 0;
    return Res;
  }

  static AvailableValue getSelect(SelectInst *Sel, Value *V1, Value *V2) {
    AvailableValue Res;
    Res.Val = Sel;
    Res.Kind = ValType::SelectVal;
    Res.Offset = 0;
    Res.V1 = V1;
    Res.V2 = V2;
    return Res;
  }

  bool isSimpleValue() const { return Kind == ValType::SimpleVal; }
  bool isCoercedLoadValue() const { return Kind == ValType::LoadVal; }
  bool isMemIntrinValue() const { return Kind == ValType::MemIntrin; }
  bool isUndefValue() const { return Kind == ValType::UndefVal; }
  bool isSelectValue() const { return Kind == ValType::SelectVal; }

  Value *getSimpleValue() const {
    assert(isSimpleValue() && "Wrong accessor");
    return Val;
  }

  LoadInst *getCoercedLoadValue() const {
    assert(isCoercedLoadValue() && "Wrong accessor");
    return cast<LoadInst>(Val);
  }

  MemIntrinsic *getMemIntrinValue() const {
    assert(isMemIntrinValue() && "Wrong accessor");
    return cast<MemIntrinsic>(Val);
  }

  SelectInst *getSelectValue() const {
    assert(isSelectValue() && "Wrong accessor");
    return cast<SelectInst>(Val);
  }

  /// Emit code at the specified insertion point to adjust the value defined
  /// here to the specified type. This handles various coercion cases.
  Value *MaterializeAdjustedValue(LoadInst *Load, Instruction *InsertPt,
                                  GVNPass &gvn) const;
};

/// Represents an AvailableValue which can be rematerialized at the end of
/// the associated BasicBlock.
struct llvm::gvn::AvailableValueInBlock {
  /// BB - The basic block in question.
  BasicBlock *BB = nullptr;

  /// AV - The actual available value
  AvailableValue AV;

  static AvailableValueInBlock get(BasicBlock *BB, AvailableValue &&AV) {
    AvailableValueInBlock Res;
    Res.BB = BB;
    Res.AV = std::move(AV);
    return Res;
  }

  static AvailableValueInBlock get(BasicBlock *BB, Value *V,
                                   unsigned Offset = 0) {
    return get(BB, AvailableValue::get(V, Offset));
  }

  static AvailableValueInBlock getUndef(BasicBlock *BB) {
    return get(BB, AvailableValue::getUndef());
  }

  static AvailableValueInBlock getSelect(BasicBlock *BB, SelectInst *Sel,
                                         Value *V1, Value *V2) {
    return get(BB, AvailableValue::getSelect(Sel, V1, V2));
  }

  /// Emit code at the end of this block to adjust the value defined here to
  /// the specified type. This handles various coercion cases.
  Value *MaterializeAdjustedValue(LoadInst *Load, GVNPass &gvn) const {
    return AV.MaterializeAdjustedValue(Load, BB->getTerminator(), gvn);
  }
};

//===----------------------------------------------------------------------===//
//                     ValueTable Internal Functions
//===----------------------------------------------------------------------===//

GVNPass::Expression GVNPass::ValueTable::createExpr(Instruction *I) {
  Expression e;
  e.type = I->getType();
  e.opcode = I->getOpcode();
  if (const GCRelocateInst *GCR = dyn_cast<GCRelocateInst>(I)) {
    // gc.relocate is 'special' call: its second and third operands are
    // not real values, but indices into statepoint's argument list.
    // Use the refered to values for purposes of identity.
    e.varargs.push_back(lookupOrAdd(GCR->getOperand(0)));
    e.varargs.push_back(lookupOrAdd(GCR->getBasePtr()));
    e.varargs.push_back(lookupOrAdd(GCR->getDerivedPtr()));
  } else {
    for (Use &Op : I->operands())
      e.varargs.push_back(lookupOrAdd(Op));
  }
  if (I->isCommutative()) {
    // Ensure that commutative instructions that only differ by a permutation
    // of their operands get the same value number by sorting the operand value
    // numbers.  Since commutative operands are the 1st two operands it is more
    // efficient to sort by hand rather than using, say, std::sort.
    assert(I->getNumOperands() >= 2 && "Unsupported commutative instruction!");
    if (e.varargs[0] > e.varargs[1])
      std::swap(e.varargs[0], e.varargs[1]);
    e.commutative = true;
  }

  if (auto *C = dyn_cast<CmpInst>(I)) {
    // Sort the operand value numbers so x<y and y>x get the same value number.
    CmpInst::Predicate Predicate = C->getPredicate();
    if (e.varargs[0] > e.varargs[1]) {
      std::swap(e.varargs[0], e.varargs[1]);
      Predicate = CmpInst::getSwappedPredicate(Predicate);
    }
    e.opcode = (C->getOpcode() << 8) | Predicate;
    e.commutative = true;
  } else if (auto *E = dyn_cast<InsertValueInst>(I)) {
    e.varargs.append(E->idx_begin(), E->idx_end());
  } else if (auto *SVI = dyn_cast<ShuffleVectorInst>(I)) {
    ArrayRef<int> ShuffleMask = SVI->getShuffleMask();
    e.varargs.append(ShuffleMask.begin(), ShuffleMask.end());
  }

  return e;
}

GVNPass::Expression GVNPass::ValueTable::createCmpExpr(
    unsigned Opcode, CmpInst::Predicate Predicate, Value *LHS, Value *RHS) {
  assert((Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) &&
         "Not a comparison!");
  Expression e;
  e.type = CmpInst::makeCmpResultType(LHS->getType());
  e.varargs.push_back(lookupOrAdd(LHS));
  e.varargs.push_back(lookupOrAdd(RHS));

  // Sort the operand value numbers so x<y and y>x get the same value number.
  if (e.varargs[0] > e.varargs[1]) {
    std::swap(e.varargs[0], e.varargs[1]);
    Predicate = CmpInst::getSwappedPredicate(Predicate);
  }
  e.opcode = (Opcode << 8) | Predicate;
  e.commutative = true;
  return e;
}

GVNPass::Expression
GVNPass::ValueTable::createExtractvalueExpr(ExtractValueInst *EI) {
  assert(EI && "Not an ExtractValueInst?");
  Expression e;
  e.type = EI->getType();
  e.opcode = 0;

  WithOverflowInst *WO = dyn_cast<WithOverflowInst>(EI->getAggregateOperand());
  if (WO != nullptr && EI->getNumIndices() == 1 && *EI->idx_begin() == 0) {
    // EI is an extract from one of our with.overflow intrinsics. Synthesize
    // a semantically equivalent expression instead of an extract value
    // expression.
    e.opcode = WO->getBinaryOp();
    e.varargs.push_back(lookupOrAdd(WO->getLHS()));
    e.varargs.push_back(lookupOrAdd(WO->getRHS()));
    return e;
  }

  // Not a recognised intrinsic. Fall back to producing an extract value
  // expression.
  e.opcode = EI->getOpcode();
  for (Use &Op : EI->operands())
    e.varargs.push_back(lookupOrAdd(Op));

  append_range(e.varargs, EI->indices());

  return e;
}

GVNPass::Expression GVNPass::ValueTable::createGEPExpr(GetElementPtrInst *GEP) {
  Expression E;
  Type *PtrTy = GEP->getType()->getScalarType();
  const DataLayout &DL = GEP->getDataLayout();
  unsigned BitWidth = DL.getIndexTypeSizeInBits(PtrTy);
  MapVector<Value *, APInt> VariableOffsets;
  APInt ConstantOffset(BitWidth, 0);
  if (GEP->collectOffset(DL, BitWidth, VariableOffsets, ConstantOffset)) {
    // Convert into offset representation, to recognize equivalent address
    // calculations that use different type encoding.
    LLVMContext &Context = GEP->getContext();
    E.opcode = GEP->getOpcode();
    E.type = nullptr;
    E.varargs.push_back(lookupOrAdd(GEP->getPointerOperand()));
    for (const auto &Pair : VariableOffsets) {
      E.varargs.push_back(lookupOrAdd(Pair.first));
      E.varargs.push_back(lookupOrAdd(ConstantInt::get(Context, Pair.second)));
    }
    if (!ConstantOffset.isZero())
      E.varargs.push_back(
          lookupOrAdd(ConstantInt::get(Context, ConstantOffset)));
  } else {
    // If converting to offset representation fails (for scalable vectors),
    // fall back to type-based implementation:
    E.opcode = GEP->getOpcode();
    E.type = GEP->getSourceElementType();
    for (Use &Op : GEP->operands())
      E.varargs.push_back(lookupOrAdd(Op));
  }
  return E;
}

//===----------------------------------------------------------------------===//
//                     ValueTable External Functions
//===----------------------------------------------------------------------===//

GVNPass::ValueTable::ValueTable() = default;
GVNPass::ValueTable::ValueTable(const ValueTable &) = default;
GVNPass::ValueTable::ValueTable(ValueTable &&) = default;
GVNPass::ValueTable::~ValueTable() = default;
GVNPass::ValueTable &
GVNPass::ValueTable::operator=(const GVNPass::ValueTable &Arg) = default;

/// add - Insert a value into the table with a specified value number.
void GVNPass::ValueTable::add(Value *V, uint32_t num) {
  valueNumbering.insert(std::make_pair(V, num));
  if (PHINode *PN = dyn_cast<PHINode>(V))
    NumberingPhi[num] = PN;
}

uint32_t GVNPass::ValueTable::lookupOrAddCall(CallInst *C) {
  // FIXME: Currently the calls which may access the thread id may
  // be considered as not accessing the memory. But this is
  // problematic for coroutines, since coroutines may resume in a
  // different thread. So we disable the optimization here for the
  // correctness. However, it may block many other correct
  // optimizations. Revert this one when we detect the memory
  // accessing kind more precisely.
  if (C->getFunction()->isPresplitCoroutine()) {
    valueNumbering[C] = nextValueNumber;
    return nextValueNumber++;
  }

  // Do not combine convergent calls since they implicitly depend on the set of
  // threads that is currently executing, and they might be in different basic
  // blocks.
  if (C->isConvergent()) {
    valueNumbering[C] = nextValueNumber;
    return nextValueNumber++;
  }

  if (AA->doesNotAccessMemory(C)) {
    Expression exp = createExpr(C);
    uint32_t e = assignExpNewValueNum(exp).first;
    valueNumbering[C] = e;
    return e;
  }

  if (MD && AA->onlyReadsMemory(C)) {
    Expression exp = createExpr(C);
    auto ValNum = assignExpNewValueNum(exp);
    if (ValNum.second) {
      valueNumbering[C] = ValNum.first;
      return ValNum.first;
    }

    MemDepResult local_dep = MD->getDependency(C);

    if (!local_dep.isDef() && !local_dep.isNonLocal()) {
      valueNumbering[C] =  nextValueNumber;
      return nextValueNumber++;
    }

    if (local_dep.isDef()) {
      // For masked load/store intrinsics, the local_dep may actually be
      // a normal load or store instruction.
      CallInst *local_cdep = dyn_cast<CallInst>(local_dep.getInst());

      if (!local_cdep || local_cdep->arg_size() != C->arg_size()) {
        valueNumbering[C] = nextValueNumber;
        return nextValueNumber++;
      }

      for (unsigned i = 0, e = C->arg_size(); i < e; ++i) {
        uint32_t c_vn = lookupOrAdd(C->getArgOperand(i));
        uint32_t cd_vn = lookupOrAdd(local_cdep->getArgOperand(i));
        if (c_vn != cd_vn) {
          valueNumbering[C] = nextValueNumber;
          return nextValueNumber++;
        }
      }

      uint32_t v = lookupOrAdd(local_cdep);
      valueNumbering[C] = v;
      return v;
    }

    // Non-local case.
    const MemoryDependenceResults::NonLocalDepInfo &deps =
        MD->getNonLocalCallDependency(C);
    // FIXME: Move the checking logic to MemDep!
    CallInst* cdep = nullptr;

    // Check to see if we have a single dominating call instruction that is
    // identical to C.
    for (const NonLocalDepEntry &I : deps) {
      if (I.getResult().isNonLocal())
        continue;

      // We don't handle non-definitions.  If we already have a call, reject
      // instruction dependencies.
      if (!I.getResult().isDef() || cdep != nullptr) {
        cdep = nullptr;
        break;
      }

      CallInst *NonLocalDepCall = dyn_cast<CallInst>(I.getResult().getInst());
      // FIXME: All duplicated with non-local case.
      if (NonLocalDepCall && DT->properlyDominates(I.getBB(), C->getParent())) {
        cdep = NonLocalDepCall;
        continue;
      }

      cdep = nullptr;
      break;
    }

    if (!cdep) {
      valueNumbering[C] = nextValueNumber;
      return nextValueNumber++;
    }

    if (cdep->arg_size() != C->arg_size()) {
      valueNumbering[C] = nextValueNumber;
      return nextValueNumber++;
    }
    for (unsigned i = 0, e = C->arg_size(); i < e; ++i) {
      uint32_t c_vn = lookupOrAdd(C->getArgOperand(i));
      uint32_t cd_vn = lookupOrAdd(cdep->getArgOperand(i));
      if (c_vn != cd_vn) {
        valueNumbering[C] = nextValueNumber;
        return nextValueNumber++;
      }
    }

    uint32_t v = lookupOrAdd(cdep);
    valueNumbering[C] = v;
    return v;
  }

  valueNumbering[C] = nextValueNumber;
  return nextValueNumber++;
}

/// Returns true if a value number exists for the specified value.
bool GVNPass::ValueTable::exists(Value *V) const {
  return valueNumbering.contains(V);
}

/// lookup_or_add - Returns the value number for the specified value, assigning
/// it a new number if it did not have one before.
uint32_t GVNPass::ValueTable::lookupOrAdd(Value *V) {
  DenseMap<Value*, uint32_t>::iterator VI = valueNumbering.find(V);
  if (VI != valueNumbering.end())
    return VI->second;

  auto *I = dyn_cast<Instruction>(V);
  if (!I) {
    valueNumbering[V] = nextValueNumber;
    return nextValueNumber++;
  }

  Expression exp;
  switch (I->getOpcode()) {
    case Instruction::Call:
      return lookupOrAddCall(cast<CallInst>(I));
    case Instruction::FNeg:
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::AddrSpaceCast:
    case Instruction::BitCast:
    case Instruction::Select:
    case Instruction::Freeze:
    case Instruction::ExtractElement:
    case Instruction::InsertElement:
    case Instruction::ShuffleVector:
    case Instruction::InsertValue:
      exp = createExpr(I);
      break;
    case Instruction::GetElementPtr:
      exp = createGEPExpr(cast<GetElementPtrInst>(I));
      break;
    case Instruction::ExtractValue:
      exp = createExtractvalueExpr(cast<ExtractValueInst>(I));
      break;
    case Instruction::PHI:
      valueNumbering[V] = nextValueNumber;
      NumberingPhi[nextValueNumber] = cast<PHINode>(V);
      return nextValueNumber++;
    default:
      valueNumbering[V] = nextValueNumber;
      return nextValueNumber++;
  }

  uint32_t e = assignExpNewValueNum(exp).first;
  valueNumbering[V] = e;
  return e;
}

/// Returns the value number of the specified value. Fails if
/// the value has not yet been numbered.
uint32_t GVNPass::ValueTable::lookup(Value *V, bool Verify) const {
  DenseMap<Value*, uint32_t>::const_iterator VI = valueNumbering.find(V);
  if (Verify) {
    assert(VI != valueNumbering.end() && "Value not numbered?");
    return VI->second;
  }
  return (VI != valueNumbering.end()) ? VI->second : 0;
}

/// Returns the value number of the given comparison,
/// assigning it a new number if it did not have one before.  Useful when
/// we deduced the result of a comparison, but don't immediately have an
/// instruction realizing that comparison to hand.
uint32_t GVNPass::ValueTable::lookupOrAddCmp(unsigned Opcode,
                                             CmpInst::Predicate Predicate,
                                             Value *LHS, Value *RHS) {
  Expression exp = createCmpExpr(Opcode, Predicate, LHS, RHS);
  return assignExpNewValueNum(exp).first;
}

/// Remove all entries from the ValueTable.
void GVNPass::ValueTable::clear() {
  valueNumbering.clear();
  expressionNumbering.clear();
  NumberingPhi.clear();
  PhiTranslateTable.clear();
  nextValueNumber = 1;
  Expressions.clear();
  ExprIdx.clear();
  nextExprNumber = 0;
}

/// Remove a value from the value numbering.
void GVNPass::ValueTable::erase(Value *V) {
  uint32_t Num = valueNumbering.lookup(V);
  valueNumbering.erase(V);
  // If V is PHINode, V <--> value number is an one-to-one mapping.
  if (isa<PHINode>(V))
    NumberingPhi.erase(Num);
}

/// verifyRemoved - Verify that the value is removed from all internal data
/// structures.
void GVNPass::ValueTable::verifyRemoved(const Value *V) const {
  assert(!valueNumbering.contains(V) &&
         "Inst still occurs in value numbering map!");
}

//===----------------------------------------------------------------------===//
//                     LeaderMap External Functions
//===----------------------------------------------------------------------===//

/// Push a new Value to the LeaderTable onto the list for its value number.
void GVNPass::LeaderMap::insert(uint32_t N, Value *V, const BasicBlock *BB) {
  LeaderListNode &Curr = NumToLeaders[N];
  if (!Curr.Entry.Val) {
    Curr.Entry.Val = V;
    Curr.Entry.BB = BB;
    return;
  }

  LeaderListNode *Node = TableAllocator.Allocate<LeaderListNode>();
  Node->Entry.Val = V;
  Node->Entry.BB = BB;
  Node->Next = Curr.Next;
  Curr.Next = Node;
}

/// Scan the list of values corresponding to a given
/// value number, and remove the given instruction if encountered.
void GVNPass::LeaderMap::erase(uint32_t N, Instruction *I,
                               const BasicBlock *BB) {
  LeaderListNode *Prev = nullptr;
  LeaderListNode *Curr = &NumToLeaders[N];

  while (Curr && (Curr->Entry.Val != I || Curr->Entry.BB != BB)) {
    Prev = Curr;
    Curr = Curr->Next;
  }

  if (!Curr)
    return;

  if (Prev) {
    Prev->Next = Curr->Next;
  } else {
    if (!Curr->Next) {
      Curr->Entry.Val = nullptr;
      Curr->Entry.BB = nullptr;
    } else {
      LeaderListNode *Next = Curr->Next;
      Curr->Entry.Val = Next->Entry.Val;
      Curr->Entry.BB = Next->Entry.BB;
      Curr->Next = Next->Next;
    }
  }
}

void GVNPass::LeaderMap::verifyRemoved(const Value *V) const {
  // Walk through the value number scope to make sure the instruction isn't
  // ferreted away in it.
  for (const auto &I : NumToLeaders) {
    (void)I;
    assert(I.second.Entry.Val != V && "Inst still in value numbering scope!");
    assert(
        std::none_of(leader_iterator(&I.second), leader_iterator(nullptr),
                     [=](const LeaderTableEntry &E) { return E.Val == V; }) &&
        "Inst still in value numbering scope!");
  }
}

//===----------------------------------------------------------------------===//
//                                GVN Pass
//===----------------------------------------------------------------------===//

bool GVNPass::isPREEnabled() const {
  return Options.AllowPRE.value_or(GVNEnablePRE);
}

bool GVNPass::isLoadPREEnabled() const {
  return Options.AllowLoadPRE.value_or(GVNEnableLoadPRE);
}

bool GVNPass::isLoadInLoopPREEnabled() const {
  return Options.AllowLoadInLoopPRE.value_or(GVNEnableLoadInLoopPRE);
}

bool GVNPass::isLoadPRESplitBackedgeEnabled() const {
  return Options.AllowLoadPRESplitBackedge.value_or(
      GVNEnableSplitBackedgeInLoadPRE);
}

bool GVNPass::isMemDepEnabled() const {
  return Options.AllowMemDep.value_or(GVNEnableMemDep);
}

PreservedAnalyses GVNPass::run(Function &F, FunctionAnalysisManager &AM) {
  // FIXME: The order of evaluation of these 'getResult' calls is very
  // significant! Re-ordering these variables will cause GVN when run alone to
  // be less effective! We should fix memdep and basic-aa to not exhibit this
  // behavior, but until then don't change the order here.
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  auto *MemDep =
      isMemDepEnabled() ? &AM.getResult<MemoryDependenceAnalysis>(F) : nullptr;
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto *MSSA = AM.getCachedResult<MemorySSAAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  bool Changed = runImpl(F, AC, DT, TLI, AA, MemDep, LI, &ORE,
                         MSSA ? &MSSA->getMSSA() : nullptr);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<TargetLibraryAnalysis>();
  if (MSSA)
    PA.preserve<MemorySSAAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}

void GVNPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<GVNPass> *>(this)->printPipeline(
      OS, MapClassName2PassName);

  OS << '<';
  if (Options.AllowPRE != std::nullopt)
    OS << (*Options.AllowPRE ? "" : "no-") << "pre;";
  if (Options.AllowLoadPRE != std::nullopt)
    OS << (*Options.AllowLoadPRE ? "" : "no-") << "load-pre;";
  if (Options.AllowLoadPRESplitBackedge != std::nullopt)
    OS << (*Options.AllowLoadPRESplitBackedge ? "" : "no-")
       << "split-backedge-load-pre;";
  if (Options.AllowMemDep != std::nullopt)
    OS << (*Options.AllowMemDep ? "" : "no-") << "memdep";
  OS << '>';
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void GVNPass::dump(DenseMap<uint32_t, Value *> &d) const {
  errs() << "{\n";
  for (auto &I : d) {
    errs() << I.first << "\n";
    I.second->dump();
  }
  errs() << "}\n";
}
#endif

enum class AvailabilityState : char {
  /// We know the block *is not* fully available. This is a fixpoint.
  Unavailable = 0,
  /// We know the block *is* fully available. This is a fixpoint.
  Available = 1,
  /// We do not know whether the block is fully available or not,
  /// but we are currently speculating that it will be.
  /// If it would have turned out that the block was, in fact, not fully
  /// available, this would have been cleaned up into an Unavailable.
  SpeculativelyAvailable = 2,
};

/// Return true if we can prove that the value
/// we're analyzing is fully available in the specified block.  As we go, keep
/// track of which blocks we know are fully alive in FullyAvailableBlocks.  This
/// map is actually a tri-state map with the following values:
///   0) we know the block *is not* fully available.
///   1) we know the block *is* fully available.
///   2) we do not know whether the block is fully available or not, but we are
///      currently speculating that it will be.
static bool IsValueFullyAvailableInBlock(
    BasicBlock *BB,
    DenseMap<BasicBlock *, AvailabilityState> &FullyAvailableBlocks) {
  SmallVector<BasicBlock *, 32> Worklist;
  std::optional<BasicBlock *> UnavailableBB;

  // The number of times we didn't find an entry for a block in a map and
  // optimistically inserted an entry marking block as speculatively available.
  unsigned NumNewNewSpeculativelyAvailableBBs = 0;

#ifndef NDEBUG
  SmallSet<BasicBlock *, 32> NewSpeculativelyAvailableBBs;
  SmallVector<BasicBlock *, 32> AvailableBBs;
#endif

  Worklist.emplace_back(BB);
  while (!Worklist.empty()) {
    BasicBlock *CurrBB = Worklist.pop_back_val(); // LoadFO - depth-first!
    // Optimistically assume that the block is Speculatively Available and check
    // to see if we already know about this block in one lookup.
    std::pair<DenseMap<BasicBlock *, AvailabilityState>::iterator, bool> IV =
        FullyAvailableBlocks.try_emplace(
            CurrBB, AvailabilityState::SpeculativelyAvailable);
    AvailabilityState &State = IV.first->second;

    // Did the entry already exist for this block?
    if (!IV.second) {
      if (State == AvailabilityState::Unavailable) {
        UnavailableBB = CurrBB;
        break; // Backpropagate unavailability info.
      }

#ifndef NDEBUG
      AvailableBBs.emplace_back(CurrBB);
#endif
      continue; // Don't recurse further, but continue processing worklist.
    }

    // No entry found for block.
    ++NumNewNewSpeculativelyAvailableBBs;
    bool OutOfBudget = NumNewNewSpeculativelyAvailableBBs > MaxBBSpeculations;

    // If we have exhausted our budget, mark this block as unavailable.
    // Also, if this block has no predecessors, the value isn't live-in here.
    if (OutOfBudget || pred_empty(CurrBB)) {
      MaxBBSpeculationCutoffReachedTimes += (int)OutOfBudget;
      State = AvailabilityState::Unavailable;
      UnavailableBB = CurrBB;
      break; // Backpropagate unavailability info.
    }

    // Tentatively consider this block as speculatively available.
#ifndef NDEBUG
    NewSpeculativelyAvailableBBs.insert(CurrBB);
#endif
    // And further recurse into block's predecessors, in depth-first order!
    Worklist.append(pred_begin(CurrBB), pred_end(CurrBB));
  }

#if LLVM_ENABLE_STATS
  IsValueFullyAvailableInBlockNumSpeculationsMax.updateMax(
      NumNewNewSpeculativelyAvailableBBs);
#endif

  // If the block isn't marked as fixpoint yet
  // (the Unavailable and Available states are fixpoints)
  auto MarkAsFixpointAndEnqueueSuccessors =
      [&](BasicBlock *BB, AvailabilityState FixpointState) {
        auto It = FullyAvailableBlocks.find(BB);
        if (It == FullyAvailableBlocks.end())
          return; // Never queried this block, leave as-is.
        switch (AvailabilityState &State = It->second) {
        case AvailabilityState::Unavailable:
        case AvailabilityState::Available:
          return; // Don't backpropagate further, continue processing worklist.
        case AvailabilityState::SpeculativelyAvailable: // Fix it!
          State = FixpointState;
#ifndef NDEBUG
          assert(NewSpeculativelyAvailableBBs.erase(BB) &&
                 "Found a speculatively available successor leftover?");
#endif
          // Queue successors for further processing.
          Worklist.append(succ_begin(BB), succ_end(BB));
          return;
        }
      };

  if (UnavailableBB) {
    // Okay, we have encountered an unavailable block.
    // Mark speculatively available blocks reachable from UnavailableBB as
    // unavailable as well. Paths are terminated when they reach blocks not in
    // FullyAvailableBlocks or they are not marked as speculatively available.
    Worklist.clear();
    Worklist.append(succ_begin(*UnavailableBB), succ_end(*UnavailableBB));
    while (!Worklist.empty())
      MarkAsFixpointAndEnqueueSuccessors(Worklist.pop_back_val(),
                                         AvailabilityState::Unavailable);
  }

#ifndef NDEBUG
  Worklist.clear();
  for (BasicBlock *AvailableBB : AvailableBBs)
    Worklist.append(succ_begin(AvailableBB), succ_end(AvailableBB));
  while (!Worklist.empty())
    MarkAsFixpointAndEnqueueSuccessors(Worklist.pop_back_val(),
                                       AvailabilityState::Available);

  assert(NewSpeculativelyAvailableBBs.empty() &&
         "Must have fixed all the new speculatively available blocks.");
#endif

  return !UnavailableBB;
}

/// If the specified OldValue exists in ValuesPerBlock, replace its value with
/// NewValue.
static void replaceValuesPerBlockEntry(
    SmallVectorImpl<AvailableValueInBlock> &ValuesPerBlock, Value *OldValue,
    Value *NewValue) {
  for (AvailableValueInBlock &V : ValuesPerBlock) {
    if (V.AV.Val == OldValue)
      V.AV.Val = NewValue;
    if (V.AV.isSelectValue()) {
      if (V.AV.V1 == OldValue)
        V.AV.V1 = NewValue;
      if (V.AV.V2 == OldValue)
        V.AV.V2 = NewValue;
    }
  }
}

/// Given a set of loads specified by ValuesPerBlock,
/// construct SSA form, allowing us to eliminate Load.  This returns the value
/// that should be used at Load's definition site.
static Value *
ConstructSSAForLoadSet(LoadInst *Load,
                       SmallVectorImpl<AvailableValueInBlock> &ValuesPerBlock,
                       GVNPass &gvn) {
  // Check for the fully redundant, dominating load case.  In this case, we can
  // just use the dominating value directly.
  if (ValuesPerBlock.size() == 1 &&
      gvn.getDominatorTree().properlyDominates(ValuesPerBlock[0].BB,
                                               Load->getParent())) {
    assert(!ValuesPerBlock[0].AV.isUndefValue() &&
           "Dead BB dominate this block");
    return ValuesPerBlock[0].MaterializeAdjustedValue(Load, gvn);
  }

  // Otherwise, we have to construct SSA form.
  SmallVector<PHINode*, 8> NewPHIs;
  SSAUpdater SSAUpdate(&NewPHIs);
  SSAUpdate.Initialize(Load->getType(), Load->getName());

  for (const AvailableValueInBlock &AV : ValuesPerBlock) {
    BasicBlock *BB = AV.BB;

    if (AV.AV.isUndefValue())
      continue;

    if (SSAUpdate.HasValueForBlock(BB))
      continue;

    // If the value is the load that we will be eliminating, and the block it's
    // available in is the block that the load is in, then don't add it as
    // SSAUpdater will resolve the value to the relevant phi which may let it
    // avoid phi construction entirely if there's actually only one value.
    if (BB == Load->getParent() &&
        ((AV.AV.isSimpleValue() && AV.AV.getSimpleValue() == Load) ||
         (AV.AV.isCoercedLoadValue() && AV.AV.getCoercedLoadValue() == Load)))
      continue;

    SSAUpdate.AddAvailableValue(BB, AV.MaterializeAdjustedValue(Load, gvn));
  }

  // Perform PHI construction.
  return SSAUpdate.GetValueInMiddleOfBlock(Load->getParent());
}

Value *AvailableValue::MaterializeAdjustedValue(LoadInst *Load,
                                                Instruction *InsertPt,
                                                GVNPass &gvn) const {
  Value *Res;
  Type *LoadTy = Load->getType();
  const DataLayout &DL = Load->getDataLayout();
  if (isSimpleValue()) {
    Res = getSimpleValue();
    if (Res->getType() != LoadTy) {
      Res = getValueForLoad(Res, Offset, LoadTy, InsertPt, DL);

      LLVM_DEBUG(dbgs() << "GVN COERCED NONLOCAL VAL:\nOffset: " << Offset
                        << "  " << *getSimpleValue() << '\n'
                        << *Res << '\n'
                        << "\n\n\n");
    }
  } else if (isCoercedLoadValue()) {
    LoadInst *CoercedLoad = getCoercedLoadValue();
    if (CoercedLoad->getType() == LoadTy && Offset == 0) {
      Res = CoercedLoad;
      combineMetadataForCSE(CoercedLoad, Load, false);
    } else {
      Res = getValueForLoad(CoercedLoad, Offset, LoadTy, InsertPt, DL);
      // We are adding a new user for this load, for which the original
      // metadata may not hold. Additionally, the new load may have a different
      // size and type, so their metadata cannot be combined in any
      // straightforward way.
      // Drop all metadata that is not known to cause immediate UB on violation,
      // unless the load has !noundef, in which case all metadata violations
      // will be promoted to UB.
      // TODO: We can combine noalias/alias.scope metadata here, because it is
      // independent of the load type.
      if (!CoercedLoad->hasMetadata(LLVMContext::MD_noundef))
        CoercedLoad->dropUnknownNonDebugMetadata(
            {LLVMContext::MD_dereferenceable,
             LLVMContext::MD_dereferenceable_or_null,
             LLVMContext::MD_invariant_load, LLVMContext::MD_invariant_group});
      LLVM_DEBUG(dbgs() << "GVN COERCED NONLOCAL LOAD:\nOffset: " << Offset
                        << "  " << *getCoercedLoadValue() << '\n'
                        << *Res << '\n'
                        << "\n\n\n");
    }
  } else if (isMemIntrinValue()) {
    Res = getMemInstValueForLoad(getMemIntrinValue(), Offset, LoadTy,
                                 InsertPt, DL);
    LLVM_DEBUG(dbgs() << "GVN COERCED NONLOCAL MEM INTRIN:\nOffset: " << Offset
                      << "  " << *getMemIntrinValue() << '\n'
                      << *Res << '\n'
                      << "\n\n\n");
  } else if (isSelectValue()) {
    // Introduce a new value select for a load from an eligible pointer select.
    SelectInst *Sel = getSelectValue();
    assert(V1 && V2 && "both value operands of the select must be present");
    Res =
        SelectInst::Create(Sel->getCondition(), V1, V2, "", Sel->getIterator());
  } else {
    llvm_unreachable("Should not materialize value from dead block");
  }
  assert(Res && "failed to materialize?");
  return Res;
}

static bool isLifetimeStart(const Instruction *Inst) {
  if (const IntrinsicInst* II = dyn_cast<IntrinsicInst>(Inst))
    return II->getIntrinsicID() == Intrinsic::lifetime_start;
  return false;
}

/// Assuming To can be reached from both From and Between, does Between lie on
/// every path from From to To?
static bool liesBetween(const Instruction *From, Instruction *Between,
                        const Instruction *To, DominatorTree *DT) {
  if (From->getParent() == Between->getParent())
    return DT->dominates(From, Between);
  SmallSet<BasicBlock *, 1> Exclusion;
  Exclusion.insert(Between->getParent());
  return !isPotentiallyReachable(From, To, &Exclusion, DT);
}

/// Try to locate the three instruction involved in a missed
/// load-elimination case that is due to an intervening store.
static void reportMayClobberedLoad(LoadInst *Load, MemDepResult DepInfo,
                                   DominatorTree *DT,
                                   OptimizationRemarkEmitter *ORE) {
  using namespace ore;

  Instruction *OtherAccess = nullptr;

  OptimizationRemarkMissed R(DEBUG_TYPE, "LoadClobbered", Load);
  R << "load of type " << NV("Type", Load->getType()) << " not eliminated"
    << setExtraArgs();

  for (auto *U : Load->getPointerOperand()->users()) {
    if (U != Load && (isa<LoadInst>(U) || isa<StoreInst>(U))) {
      auto *I = cast<Instruction>(U);
      if (I->getFunction() == Load->getFunction() && DT->dominates(I, Load)) {
        // Use the most immediately dominating value
        if (OtherAccess) {
          if (DT->dominates(OtherAccess, I))
            OtherAccess = I;
          else
            assert(U == OtherAccess || DT->dominates(I, OtherAccess));
        } else
          OtherAccess = I;
      }
    }
  }

  if (!OtherAccess) {
    // There is no dominating use, check if we can find a closest non-dominating
    // use that lies between any other potentially available use and Load.
    for (auto *U : Load->getPointerOperand()->users()) {
      if (U != Load && (isa<LoadInst>(U) || isa<StoreInst>(U))) {
        auto *I = cast<Instruction>(U);
        if (I->getFunction() == Load->getFunction() &&
            isPotentiallyReachable(I, Load, nullptr, DT)) {
          if (OtherAccess) {
            if (liesBetween(OtherAccess, I, Load, DT)) {
              OtherAccess = I;
            } else if (!liesBetween(I, OtherAccess, Load, DT)) {
              // These uses are both partially available at Load were it not for
              // the clobber, but neither lies strictly after the other.
              OtherAccess = nullptr;
              break;
            } // else: keep current OtherAccess since it lies between U and Load
          } else {
            OtherAccess = I;
          }
        }
      }
    }
  }

  if (OtherAccess)
    R << " in favor of " << NV("OtherAccess", OtherAccess);

  R << " because it is clobbered by " << NV("ClobberedBy", DepInfo.getInst());

  ORE->emit(R);
}

// Find non-clobbered value for Loc memory location in extended basic block
// (chain of basic blocks with single predecessors) starting From instruction.
static Value *findDominatingValue(const MemoryLocation &Loc, Type *LoadTy,
                                  Instruction *From, AAResults *AA) {
  uint32_t NumVisitedInsts = 0;
  BasicBlock *FromBB = From->getParent();
  BatchAAResults BatchAA(*AA);
  for (BasicBlock *BB = FromBB; BB; BB = BB->getSinglePredecessor())
    for (auto *Inst = BB == FromBB ? From : BB->getTerminator();
         Inst != nullptr; Inst = Inst->getPrevNonDebugInstruction()) {
      // Stop the search if limit is reached.
      if (++NumVisitedInsts > MaxNumVisitedInsts)
        return nullptr;
      if (isModSet(BatchAA.getModRefInfo(Inst, Loc)))
        return nullptr;
      if (auto *LI = dyn_cast<LoadInst>(Inst))
        if (LI->getPointerOperand() == Loc.Ptr && LI->getType() == LoadTy)
          return LI;
    }
  return nullptr;
}

std::optional<AvailableValue>
GVNPass::AnalyzeLoadAvailability(LoadInst *Load, MemDepResult DepInfo,
                                 Value *Address) {
  assert(Load->isUnordered() && "rules below are incorrect for ordered access");
  assert(DepInfo.isLocal() && "expected a local dependence");

  Instruction *DepInst = DepInfo.getInst();

  const DataLayout &DL = Load->getDataLayout();
  if (DepInfo.isClobber()) {
    // If the dependence is to a store that writes to a superset of the bits
    // read by the load, we can extract the bits we need for the load from the
    // stored value.
    if (StoreInst *DepSI = dyn_cast<StoreInst>(DepInst)) {
      // Can't forward from non-atomic to atomic without violating memory model.
      if (Address && Load->isAtomic() <= DepSI->isAtomic()) {
        int Offset =
            analyzeLoadFromClobberingStore(Load->getType(), Address, DepSI, DL);
        if (Offset != -1)
          return AvailableValue::get(DepSI->getValueOperand(), Offset);
      }
    }

    // Check to see if we have something like this:
    //    load i32* P
    //    load i8* (P+1)
    // if we have this, replace the later with an extraction from the former.
    if (LoadInst *DepLoad = dyn_cast<LoadInst>(DepInst)) {
      // If this is a clobber and L is the first instruction in its block, then
      // we have the first instruction in the entry block.
      // Can't forward from non-atomic to atomic without violating memory model.
      if (DepLoad != Load && Address &&
          Load->isAtomic() <= DepLoad->isAtomic()) {
        Type *LoadType = Load->getType();
        int Offset = -1;

        // If MD reported clobber, check it was nested.
        if (DepInfo.isClobber() &&
            canCoerceMustAliasedValueToLoad(DepLoad, LoadType, DL)) {
          const auto ClobberOff = MD->getClobberOffset(DepLoad);
          // GVN has no deal with a negative offset.
          Offset = (ClobberOff == std::nullopt || *ClobberOff < 0)
                       ? -1
                       : *ClobberOff;
        }
        if (Offset == -1)
          Offset =
              analyzeLoadFromClobberingLoad(LoadType, Address, DepLoad, DL);
        if (Offset != -1)
          return AvailableValue::getLoad(DepLoad, Offset);
      }
    }

    // If the clobbering value is a memset/memcpy/memmove, see if we can
    // forward a value on from it.
    if (MemIntrinsic *DepMI = dyn_cast<MemIntrinsic>(DepInst)) {
      if (Address && !Load->isAtomic()) {
        int Offset = analyzeLoadFromClobberingMemInst(Load->getType(), Address,
                                                      DepMI, DL);
        if (Offset != -1)
          return AvailableValue::getMI(DepMI, Offset);
      }
    }

    // Nothing known about this clobber, have to be conservative
    LLVM_DEBUG(
        // fast print dep, using operator<< on instruction is too slow.
        dbgs() << "GVN: load "; Load->printAsOperand(dbgs());
        dbgs() << " is clobbered by " << *DepInst << '\n';);
    if (ORE->allowExtraAnalysis(DEBUG_TYPE))
      reportMayClobberedLoad(Load, DepInfo, DT, ORE);

    return std::nullopt;
  }
  assert(DepInfo.isDef() && "follows from above");

  // Loading the alloca -> undef.
  // Loading immediately after lifetime begin -> undef.
  if (isa<AllocaInst>(DepInst) || isLifetimeStart(DepInst))
    return AvailableValue::get(UndefValue::get(Load->getType()));

  if (Constant *InitVal =
          getInitialValueOfAllocation(DepInst, TLI, Load->getType()))
    return AvailableValue::get(InitVal);

  if (StoreInst *S = dyn_cast<StoreInst>(DepInst)) {
    // Reject loads and stores that are to the same address but are of
    // different types if we have to. If the stored value is convertable to
    // the loaded value, we can reuse it.
    if (!canCoerceMustAliasedValueToLoad(S->getValueOperand(), Load->getType(),
                                         DL))
      return std::nullopt;

    // Can't forward from non-atomic to atomic without violating memory model.
    if (S->isAtomic() < Load->isAtomic())
      return std::nullopt;

    return AvailableValue::get(S->getValueOperand());
  }

  if (LoadInst *LD = dyn_cast<LoadInst>(DepInst)) {
    // If the types mismatch and we can't handle it, reject reuse of the load.
    // If the stored value is larger or equal to the loaded value, we can reuse
    // it.
    if (!canCoerceMustAliasedValueToLoad(LD, Load->getType(), DL))
      return std::nullopt;

    // Can't forward from non-atomic to atomic without violating memory model.
    if (LD->isAtomic() < Load->isAtomic())
      return std::nullopt;

    return AvailableValue::getLoad(LD);
  }

  // Check if load with Addr dependent from select can be converted to select
  // between load values. There must be no instructions between the found
  // loads and DepInst that may clobber the loads.
  if (auto *Sel = dyn_cast<SelectInst>(DepInst)) {
    assert(Sel->getType() == Load->getPointerOperandType());
    auto Loc = MemoryLocation::get(Load);
    Value *V1 =
        findDominatingValue(Loc.getWithNewPtr(Sel->getTrueValue()),
                            Load->getType(), DepInst, getAliasAnalysis());
    if (!V1)
      return std::nullopt;
    Value *V2 =
        findDominatingValue(Loc.getWithNewPtr(Sel->getFalseValue()),
                            Load->getType(), DepInst, getAliasAnalysis());
    if (!V2)
      return std::nullopt;
    return AvailableValue::getSelect(Sel, V1, V2);
  }

  // Unknown def - must be conservative
  LLVM_DEBUG(
      // fast print dep, using operator<< on instruction is too slow.
      dbgs() << "GVN: load "; Load->printAsOperand(dbgs());
      dbgs() << " has unknown def " << *DepInst << '\n';);
  return std::nullopt;
}

void GVNPass::AnalyzeLoadAvailability(LoadInst *Load, LoadDepVect &Deps,
                                      AvailValInBlkVect &ValuesPerBlock,
                                      UnavailBlkVect &UnavailableBlocks) {
  // Filter out useless results (non-locals, etc).  Keep track of the blocks
  // where we have a value available in repl, also keep track of whether we see
  // dependencies that produce an unknown value for the load (such as a call
  // that could potentially clobber the load).
  for (const auto &Dep : Deps) {
    BasicBlock *DepBB = Dep.getBB();
    MemDepResult DepInfo = Dep.getResult();

    if (DeadBlocks.count(DepBB)) {
      // Dead dependent mem-op disguise as a load evaluating the same value
      // as the load in question.
      ValuesPerBlock.push_back(AvailableValueInBlock::getUndef(DepBB));
      continue;
    }

    if (!DepInfo.isLocal()) {
      UnavailableBlocks.push_back(DepBB);
      continue;
    }

    // The address being loaded in this non-local block may not be the same as
    // the pointer operand of the load if PHI translation occurs.  Make sure
    // to consider the right address.
    if (auto AV = AnalyzeLoadAvailability(Load, DepInfo, Dep.getAddress())) {
      // subtlety: because we know this was a non-local dependency, we know
      // it's safe to materialize anywhere between the instruction within
      // DepInfo and the end of it's block.
      ValuesPerBlock.push_back(
          AvailableValueInBlock::get(DepBB, std::move(*AV)));
    } else {
      UnavailableBlocks.push_back(DepBB);
    }
  }

  assert(Deps.size() == ValuesPerBlock.size() + UnavailableBlocks.size() &&
         "post condition violation");
}

/// Given the following code, v1 is partially available on some edges, but not
/// available on the edge from PredBB. This function tries to find if there is
/// another identical load in the other successor of PredBB.
///
///      v0 = load %addr
///      br %LoadBB
///
///   LoadBB:
///      v1 = load %addr
///      ...
///
///   PredBB:
///      ...
///      br %cond, label %LoadBB, label %SuccBB
///
///   SuccBB:
///      v2 = load %addr
///      ...
///
LoadInst *GVNPass::findLoadToHoistIntoPred(BasicBlock *Pred, BasicBlock *LoadBB,
                                           LoadInst *Load) {
  // For simplicity we handle a Pred has 2 successors only.
  auto *Term = Pred->getTerminator();
  if (Term->getNumSuccessors() != 2 || Term->isSpecialTerminator())
    return nullptr;
  auto *SuccBB = Term->getSuccessor(0);
  if (SuccBB == LoadBB)
    SuccBB = Term->getSuccessor(1);
  if (!SuccBB->getSinglePredecessor())
    return nullptr;

  unsigned int NumInsts = MaxNumInsnsPerBlock;
  for (Instruction &Inst : *SuccBB) {
    if (Inst.isDebugOrPseudoInst())
      continue;
    if (--NumInsts == 0)
      return nullptr;

    if (!Inst.isIdenticalTo(Load))
      continue;

    MemDepResult Dep = MD->getDependency(&Inst);
    // If an identical load doesn't depends on any local instructions, it can
    // be safely moved to PredBB.
    // Also check for the implicit control flow instructions. See the comments
    // in PerformLoadPRE for details.
    if (Dep.isNonLocal() && !ICF->isDominatedByICFIFromSameBlock(&Inst))
      return cast<LoadInst>(&Inst);

    // Otherwise there is something in the same BB clobbers the memory, we can't
    // move this and later load to PredBB.
    return nullptr;
  }

  return nullptr;
}

void GVNPass::eliminatePartiallyRedundantLoad(
    LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
    MapVector<BasicBlock *, Value *> &AvailableLoads,
    MapVector<BasicBlock *, LoadInst *> *CriticalEdgePredAndLoad) {
  for (const auto &AvailableLoad : AvailableLoads) {
    BasicBlock *UnavailableBlock = AvailableLoad.first;
    Value *LoadPtr = AvailableLoad.second;

    auto *NewLoad = new LoadInst(
        Load->getType(), LoadPtr, Load->getName() + ".pre", Load->isVolatile(),
        Load->getAlign(), Load->getOrdering(), Load->getSyncScopeID(),
        UnavailableBlock->getTerminator()->getIterator());
    NewLoad->setDebugLoc(Load->getDebugLoc());
    if (MSSAU) {
      auto *NewAccess = MSSAU->createMemoryAccessInBB(
          NewLoad, nullptr, NewLoad->getParent(), MemorySSA::BeforeTerminator);
      if (auto *NewDef = dyn_cast<MemoryDef>(NewAccess))
        MSSAU->insertDef(NewDef, /*RenameUses=*/true);
      else
        MSSAU->insertUse(cast<MemoryUse>(NewAccess), /*RenameUses=*/true);
    }

    // Transfer the old load's AA tags to the new load.
    AAMDNodes Tags = Load->getAAMetadata();
    if (Tags)
      NewLoad->setAAMetadata(Tags);

    if (auto *MD = Load->getMetadata(LLVMContext::MD_invariant_load))
      NewLoad->setMetadata(LLVMContext::MD_invariant_load, MD);
    if (auto *InvGroupMD = Load->getMetadata(LLVMContext::MD_invariant_group))
      NewLoad->setMetadata(LLVMContext::MD_invariant_group, InvGroupMD);
    if (auto *RangeMD = Load->getMetadata(LLVMContext::MD_range))
      NewLoad->setMetadata(LLVMContext::MD_range, RangeMD);
    if (auto *AccessMD = Load->getMetadata(LLVMContext::MD_access_group))
      if (LI->getLoopFor(Load->getParent()) == LI->getLoopFor(UnavailableBlock))
        NewLoad->setMetadata(LLVMContext::MD_access_group, AccessMD);

    // We do not propagate the old load's debug location, because the new
    // load now lives in a different BB, and we want to avoid a jumpy line
    // table.
    // FIXME: How do we retain source locations without causing poor debugging
    // behavior?

    // Add the newly created load.
    ValuesPerBlock.push_back(
        AvailableValueInBlock::get(UnavailableBlock, NewLoad));
    MD->invalidateCachedPointerInfo(LoadPtr);
    LLVM_DEBUG(dbgs() << "GVN INSERTED " << *NewLoad << '\n');

    // For PredBB in CriticalEdgePredAndLoad we need to replace the uses of old
    // load instruction with the new created load instruction.
    if (CriticalEdgePredAndLoad) {
      auto I = CriticalEdgePredAndLoad->find(UnavailableBlock);
      if (I != CriticalEdgePredAndLoad->end()) {
        ++NumPRELoadMoved2CEPred;
        ICF->insertInstructionTo(NewLoad, UnavailableBlock);
        LoadInst *OldLoad = I->second;
        combineMetadataForCSE(NewLoad, OldLoad, false);
        OldLoad->replaceAllUsesWith(NewLoad);
        replaceValuesPerBlockEntry(ValuesPerBlock, OldLoad, NewLoad);
        if (uint32_t ValNo = VN.lookup(OldLoad, false))
          LeaderTable.erase(ValNo, OldLoad, OldLoad->getParent());
        VN.erase(OldLoad);
        removeInstruction(OldLoad);
      }
    }
  }

  // Perform PHI construction.
  Value *V = ConstructSSAForLoadSet(Load, ValuesPerBlock, *this);
  // ConstructSSAForLoadSet is responsible for combining metadata.
  ICF->removeUsersOf(Load);
  Load->replaceAllUsesWith(V);
  if (isa<PHINode>(V))
    V->takeName(Load);
  if (Instruction *I = dyn_cast<Instruction>(V))
    I->setDebugLoc(Load->getDebugLoc());
  if (V->getType()->isPtrOrPtrVectorTy())
    MD->invalidateCachedPointerInfo(V);
  markInstructionForDeletion(Load);
  ORE->emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "LoadPRE", Load)
           << "load eliminated by PRE";
  });
}

bool GVNPass::PerformLoadPRE(LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
                             UnavailBlkVect &UnavailableBlocks) {
  // Okay, we have *some* definitions of the value.  This means that the value
  // is available in some of our (transitive) predecessors.  Lets think about
  // doing PRE of this load.  This will involve inserting a new load into the
  // predecessor when it's not available.  We could do this in general, but
  // prefer to not increase code size.  As such, we only do this when we know
  // that we only have to insert *one* load (which means we're basically moving
  // the load, not inserting a new one).

  SmallPtrSet<BasicBlock *, 4> Blockers(UnavailableBlocks.begin(),
                                        UnavailableBlocks.end());

  // Let's find the first basic block with more than one predecessor.  Walk
  // backwards through predecessors if needed.
  BasicBlock *LoadBB = Load->getParent();
  BasicBlock *TmpBB = LoadBB;

  // Check that there is no implicit control flow instructions above our load in
  // its block. If there is an instruction that doesn't always pass the
  // execution to the following instruction, then moving through it may become
  // invalid. For example:
  //
  // int arr[LEN];
  // int index = ???;
  // ...
  // guard(0 <= index && index < LEN);
  // use(arr[index]);
  //
  // It is illegal to move the array access to any point above the guard,
  // because if the index is out of bounds we should deoptimize rather than
  // access the array.
  // Check that there is no guard in this block above our instruction.
  bool MustEnsureSafetyOfSpeculativeExecution =
      ICF->isDominatedByICFIFromSameBlock(Load);

  while (TmpBB->getSinglePredecessor()) {
    TmpBB = TmpBB->getSinglePredecessor();
    if (TmpBB == LoadBB) // Infinite (unreachable) loop.
      return false;
    if (Blockers.count(TmpBB))
      return false;

    // If any of these blocks has more than one successor (i.e. if the edge we
    // just traversed was critical), then there are other paths through this
    // block along which the load may not be anticipated.  Hoisting the load
    // above this block would be adding the load to execution paths along
    // which it was not previously executed.
    if (TmpBB->getTerminator()->getNumSuccessors() != 1)
      return false;

    // Check that there is no implicit control flow in a block above.
    MustEnsureSafetyOfSpeculativeExecution =
        MustEnsureSafetyOfSpeculativeExecution || ICF->hasICF(TmpBB);
  }

  assert(TmpBB);
  LoadBB = TmpBB;

  // Check to see how many predecessors have the loaded value fully
  // available.
  MapVector<BasicBlock *, Value *> PredLoads;
  DenseMap<BasicBlock *, AvailabilityState> FullyAvailableBlocks;
  for (const AvailableValueInBlock &AV : ValuesPerBlock)
    FullyAvailableBlocks[AV.BB] = AvailabilityState::Available;
  for (BasicBlock *UnavailableBB : UnavailableBlocks)
    FullyAvailableBlocks[UnavailableBB] = AvailabilityState::Unavailable;

  // The edge from Pred to LoadBB is a critical edge will be splitted.
  SmallVector<BasicBlock *, 4> CriticalEdgePredSplit;
  // The edge from Pred to LoadBB is a critical edge, another successor of Pred
  // contains a load can be moved to Pred. This data structure maps the Pred to
  // the movable load.
  MapVector<BasicBlock *, LoadInst *> CriticalEdgePredAndLoad;
  for (BasicBlock *Pred : predecessors(LoadBB)) {
    // If any predecessor block is an EH pad that does not allow non-PHI
    // instructions before the terminator, we can't PRE the load.
    if (Pred->getTerminator()->isEHPad()) {
      LLVM_DEBUG(
          dbgs() << "COULD NOT PRE LOAD BECAUSE OF AN EH PAD PREDECESSOR '"
                 << Pred->getName() << "': " << *Load << '\n');
      return false;
    }

    if (IsValueFullyAvailableInBlock(Pred, FullyAvailableBlocks)) {
      continue;
    }

    if (Pred->getTerminator()->getNumSuccessors() != 1) {
      if (isa<IndirectBrInst>(Pred->getTerminator())) {
        LLVM_DEBUG(
            dbgs() << "COULD NOT PRE LOAD BECAUSE OF INDBR CRITICAL EDGE '"
                   << Pred->getName() << "': " << *Load << '\n');
        return false;
      }

      if (LoadBB->isEHPad()) {
        LLVM_DEBUG(
            dbgs() << "COULD NOT PRE LOAD BECAUSE OF AN EH PAD CRITICAL EDGE '"
                   << Pred->getName() << "': " << *Load << '\n');
        return false;
      }

      // Do not split backedge as it will break the canonical loop form.
      if (!isLoadPRESplitBackedgeEnabled())
        if (DT->dominates(LoadBB, Pred)) {
          LLVM_DEBUG(
              dbgs()
              << "COULD NOT PRE LOAD BECAUSE OF A BACKEDGE CRITICAL EDGE '"
              << Pred->getName() << "': " << *Load << '\n');
          return false;
        }

      if (LoadInst *LI = findLoadToHoistIntoPred(Pred, LoadBB, Load))
        CriticalEdgePredAndLoad[Pred] = LI;
      else
        CriticalEdgePredSplit.push_back(Pred);
    } else {
      // Only add the predecessors that will not be split for now.
      PredLoads[Pred] = nullptr;
    }
  }

  // Decide whether PRE is profitable for this load.
  unsigned NumInsertPreds = PredLoads.size() + CriticalEdgePredSplit.size();
  unsigned NumUnavailablePreds = NumInsertPreds +
      CriticalEdgePredAndLoad.size();
  assert(NumUnavailablePreds != 0 &&
         "Fully available value should already be eliminated!");
  (void)NumUnavailablePreds;

  // If we need to insert new load in multiple predecessors, reject it.
  // FIXME: If we could restructure the CFG, we could make a common pred with
  // all the preds that don't have an available Load and insert a new load into
  // that one block.
  if (NumInsertPreds > 1)
      return false;

  // Now we know where we will insert load. We must ensure that it is safe
  // to speculatively execute the load at that points.
  if (MustEnsureSafetyOfSpeculativeExecution) {
    if (CriticalEdgePredSplit.size())
      if (!isSafeToSpeculativelyExecute(Load, LoadBB->getFirstNonPHI(), AC, DT))
        return false;
    for (auto &PL : PredLoads)
      if (!isSafeToSpeculativelyExecute(Load, PL.first->getTerminator(), AC,
                                        DT))
        return false;
    for (auto &CEP : CriticalEdgePredAndLoad)
      if (!isSafeToSpeculativelyExecute(Load, CEP.first->getTerminator(), AC,
                                        DT))
        return false;
  }

  // Split critical edges, and update the unavailable predecessors accordingly.
  for (BasicBlock *OrigPred : CriticalEdgePredSplit) {
    BasicBlock *NewPred = splitCriticalEdges(OrigPred, LoadBB);
    assert(!PredLoads.count(OrigPred) && "Split edges shouldn't be in map!");
    PredLoads[NewPred] = nullptr;
    LLVM_DEBUG(dbgs() << "Split critical edge " << OrigPred->getName() << "->"
                      << LoadBB->getName() << '\n');
  }

  for (auto &CEP : CriticalEdgePredAndLoad)
    PredLoads[CEP.first] = nullptr;

  // Check if the load can safely be moved to all the unavailable predecessors.
  bool CanDoPRE = true;
  const DataLayout &DL = Load->getDataLayout();
  SmallVector<Instruction*, 8> NewInsts;
  for (auto &PredLoad : PredLoads) {
    BasicBlock *UnavailablePred = PredLoad.first;

    // Do PHI translation to get its value in the predecessor if necessary.  The
    // returned pointer (if non-null) is guaranteed to dominate UnavailablePred.
    // We do the translation for each edge we skipped by going from Load's block
    // to LoadBB, otherwise we might miss pieces needing translation.

    // If all preds have a single successor, then we know it is safe to insert
    // the load on the pred (?!?), so we can insert code to materialize the
    // pointer if it is not available.
    Value *LoadPtr = Load->getPointerOperand();
    BasicBlock *Cur = Load->getParent();
    while (Cur != LoadBB) {
      PHITransAddr Address(LoadPtr, DL, AC);
      LoadPtr = Address.translateWithInsertion(Cur, Cur->getSinglePredecessor(),
                                               *DT, NewInsts);
      if (!LoadPtr) {
        CanDoPRE = false;
        break;
      }
      Cur = Cur->getSinglePredecessor();
    }

    if (LoadPtr) {
      PHITransAddr Address(LoadPtr, DL, AC);
      LoadPtr = Address.translateWithInsertion(LoadBB, UnavailablePred, *DT,
                                               NewInsts);
    }
    // If we couldn't find or insert a computation of this phi translated value,
    // we fail PRE.
    if (!LoadPtr) {
      LLVM_DEBUG(dbgs() << "COULDN'T INSERT PHI TRANSLATED VALUE OF: "
                        << *Load->getPointerOperand() << "\n");
      CanDoPRE = false;
      break;
    }

    PredLoad.second = LoadPtr;
  }

  if (!CanDoPRE) {
    while (!NewInsts.empty()) {
      // Erase instructions generated by the failed PHI translation before
      // trying to number them. PHI translation might insert instructions
      // in basic blocks other than the current one, and we delete them
      // directly, as markInstructionForDeletion only allows removing from the
      // current basic block.
      NewInsts.pop_back_val()->eraseFromParent();
    }
    // HINT: Don't revert the edge-splitting as following transformation may
    // also need to split these critical edges.
    return !CriticalEdgePredSplit.empty();
  }

  // Okay, we can eliminate this load by inserting a reload in the predecessor
  // and using PHI construction to get the value in the other predecessors, do
  // it.
  LLVM_DEBUG(dbgs() << "GVN REMOVING PRE LOAD: " << *Load << '\n');
  LLVM_DEBUG(if (!NewInsts.empty()) dbgs() << "INSERTED " << NewInsts.size()
                                           << " INSTS: " << *NewInsts.back()
                                           << '\n');

  // Assign value numbers to the new instructions.
  for (Instruction *I : NewInsts) {
    // Instructions that have been inserted in predecessor(s) to materialize
    // the load address do not retain their original debug locations. Doing
    // so could lead to confusing (but correct) source attributions.
    I->updateLocationAfterHoist();

    // FIXME: We really _ought_ to insert these value numbers into their
    // parent's availability map.  However, in doing so, we risk getting into
    // ordering issues.  If a block hasn't been processed yet, we would be
    // marking a value as AVAIL-IN, which isn't what we intend.
    VN.lookupOrAdd(I);
  }

  eliminatePartiallyRedundantLoad(Load, ValuesPerBlock, PredLoads,
                                  &CriticalEdgePredAndLoad);
  ++NumPRELoad;
  return true;
}

bool GVNPass::performLoopLoadPRE(LoadInst *Load,
                                 AvailValInBlkVect &ValuesPerBlock,
                                 UnavailBlkVect &UnavailableBlocks) {
  const Loop *L = LI->getLoopFor(Load->getParent());
  // TODO: Generalize to other loop blocks that dominate the latch.
  if (!L || L->getHeader() != Load->getParent())
    return false;

  BasicBlock *Preheader = L->getLoopPreheader();
  BasicBlock *Latch = L->getLoopLatch();
  if (!Preheader || !Latch)
    return false;

  Value *LoadPtr = Load->getPointerOperand();
  // Must be available in preheader.
  if (!L->isLoopInvariant(LoadPtr))
    return false;

  // We plan to hoist the load to preheader without introducing a new fault.
  // In order to do it, we need to prove that we cannot side-exit the loop
  // once loop header is first entered before execution of the load.
  if (ICF->isDominatedByICFIFromSameBlock(Load))
    return false;

  BasicBlock *LoopBlock = nullptr;
  for (auto *Blocker : UnavailableBlocks) {
    // Blockers from outside the loop are handled in preheader.
    if (!L->contains(Blocker))
      continue;

    // Only allow one loop block. Loop header is not less frequently executed
    // than each loop block, and likely it is much more frequently executed. But
    // in case of multiple loop blocks, we need extra information (such as block
    // frequency info) to understand whether it is profitable to PRE into
    // multiple loop blocks.
    if (LoopBlock)
      return false;

    // Do not sink into inner loops. This may be non-profitable.
    if (L != LI->getLoopFor(Blocker))
      return false;

    // Blocks that dominate the latch execute on every single iteration, maybe
    // except the last one. So PREing into these blocks doesn't make much sense
    // in most cases. But the blocks that do not necessarily execute on each
    // iteration are sometimes much colder than the header, and this is when
    // PRE is potentially profitable.
    if (DT->dominates(Blocker, Latch))
      return false;

    // Make sure that the terminator itself doesn't clobber.
    if (Blocker->getTerminator()->mayWriteToMemory())
      return false;

    LoopBlock = Blocker;
  }

  if (!LoopBlock)
    return false;

  // Make sure the memory at this pointer cannot be freed, therefore we can
  // safely reload from it after clobber.
  if (LoadPtr->canBeFreed())
    return false;

  // TODO: Support critical edge splitting if blocker has more than 1 successor.
  MapVector<BasicBlock *, Value *> AvailableLoads;
  AvailableLoads[LoopBlock] = LoadPtr;
  AvailableLoads[Preheader] = LoadPtr;

  LLVM_DEBUG(dbgs() << "GVN REMOVING PRE LOOP LOAD: " << *Load << '\n');
  eliminatePartiallyRedundantLoad(Load, ValuesPerBlock, AvailableLoads,
                                  /*CriticalEdgePredAndLoad*/ nullptr);
  ++NumPRELoopLoad;
  return true;
}

static void reportLoadElim(LoadInst *Load, Value *AvailableValue,
                           OptimizationRemarkEmitter *ORE) {
  using namespace ore;

  ORE->emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "LoadElim", Load)
           << "load of type " << NV("Type", Load->getType()) << " eliminated"
           << setExtraArgs() << " in favor of "
           << NV("InfavorOfValue", AvailableValue);
  });
}

/// Attempt to eliminate a load whose dependencies are
/// non-local by performing PHI construction.
bool GVNPass::processNonLocalLoad(LoadInst *Load) {
  // non-local speculations are not allowed under asan.
  if (Load->getParent()->getParent()->hasFnAttribute(
          Attribute::SanitizeAddress) ||
      Load->getParent()->getParent()->hasFnAttribute(
          Attribute::SanitizeHWAddress))
    return false;

  // Step 1: Find the non-local dependencies of the load.
  LoadDepVect Deps;
  MD->getNonLocalPointerDependency(Load, Deps);

  // If we had to process more than one hundred blocks to find the
  // dependencies, this load isn't worth worrying about.  Optimizing
  // it will be too expensive.
  unsigned NumDeps = Deps.size();
  if (NumDeps > MaxNumDeps)
    return false;

  // If we had a phi translation failure, we'll have a single entry which is a
  // clobber in the current block.  Reject this early.
  if (NumDeps == 1 &&
      !Deps[0].getResult().isDef() && !Deps[0].getResult().isClobber()) {
    LLVM_DEBUG(dbgs() << "GVN: non-local load "; Load->printAsOperand(dbgs());
               dbgs() << " has unknown dependencies\n";);
    return false;
  }

  bool Changed = false;
  // If this load follows a GEP, see if we can PRE the indices before analyzing.
  if (GetElementPtrInst *GEP =
          dyn_cast<GetElementPtrInst>(Load->getOperand(0))) {
    for (Use &U : GEP->indices())
      if (Instruction *I = dyn_cast<Instruction>(U.get()))
        Changed |= performScalarPRE(I);
  }

  // Step 2: Analyze the availability of the load
  AvailValInBlkVect ValuesPerBlock;
  UnavailBlkVect UnavailableBlocks;
  AnalyzeLoadAvailability(Load, Deps, ValuesPerBlock, UnavailableBlocks);

  // If we have no predecessors that produce a known value for this load, exit
  // early.
  if (ValuesPerBlock.empty())
    return Changed;

  // Step 3: Eliminate fully redundancy.
  //
  // If all of the instructions we depend on produce a known value for this
  // load, then it is fully redundant and we can use PHI insertion to compute
  // its value.  Insert PHIs and remove the fully redundant value now.
  if (UnavailableBlocks.empty()) {
    LLVM_DEBUG(dbgs() << "GVN REMOVING NONLOCAL LOAD: " << *Load << '\n');

    // Perform PHI construction.
    Value *V = ConstructSSAForLoadSet(Load, ValuesPerBlock, *this);
    // ConstructSSAForLoadSet is responsible for combining metadata.
    ICF->removeUsersOf(Load);
    Load->replaceAllUsesWith(V);

    if (isa<PHINode>(V))
      V->takeName(Load);
    if (Instruction *I = dyn_cast<Instruction>(V))
      // If instruction I has debug info, then we should not update it.
      // Also, if I has a null DebugLoc, then it is still potentially incorrect
      // to propagate Load's DebugLoc because Load may not post-dominate I.
      if (Load->getDebugLoc() && Load->getParent() == I->getParent())
        I->setDebugLoc(Load->getDebugLoc());
    if (V->getType()->isPtrOrPtrVectorTy())
      MD->invalidateCachedPointerInfo(V);
    markInstructionForDeletion(Load);
    ++NumGVNLoad;
    reportLoadElim(Load, V, ORE);
    return true;
  }

  // Step 4: Eliminate partial redundancy.
  if (!isPREEnabled() || !isLoadPREEnabled())
    return Changed;
  if (!isLoadInLoopPREEnabled() && LI->getLoopFor(Load->getParent()))
    return Changed;

  if (performLoopLoadPRE(Load, ValuesPerBlock, UnavailableBlocks) ||
      PerformLoadPRE(Load, ValuesPerBlock, UnavailableBlocks))
    return true;

  return Changed;
}

static bool impliesEquivalanceIfTrue(CmpInst* Cmp) {
  if (Cmp->getPredicate() == CmpInst::Predicate::ICMP_EQ)
    return true;

  // Floating point comparisons can be equal, but not equivalent.  Cases:
  // NaNs for unordered operators
  // +0.0 vs 0.0 for all operators
  if (Cmp->getPredicate() == CmpInst::Predicate::FCMP_OEQ ||
      (Cmp->getPredicate() == CmpInst::Predicate::FCMP_UEQ &&
       Cmp->getFastMathFlags().noNaNs())) {
      Value *LHS = Cmp->getOperand(0);
      Value *RHS = Cmp->getOperand(1);
      // If we can prove either side non-zero, then equality must imply
      // equivalence.
      // FIXME: We should do this optimization if 'no signed zeros' is
      // applicable via an instruction-level fast-math-flag or some other
      // indicator that relaxed FP semantics are being used.
      if (isa<ConstantFP>(LHS) && !cast<ConstantFP>(LHS)->isZero())
        return true;
      if (isa<ConstantFP>(RHS) && !cast<ConstantFP>(RHS)->isZero())
        return true;
      // TODO: Handle vector floating point constants
  }
  return false;
}

static bool impliesEquivalanceIfFalse(CmpInst* Cmp) {
  if (Cmp->getPredicate() == CmpInst::Predicate::ICMP_NE)
    return true;

  // Floating point comparisons can be equal, but not equivelent.  Cases:
  // NaNs for unordered operators
  // +0.0 vs 0.0 for all operators
  if ((Cmp->getPredicate() == CmpInst::Predicate::FCMP_ONE &&
       Cmp->getFastMathFlags().noNaNs()) ||
      Cmp->getPredicate() == CmpInst::Predicate::FCMP_UNE) {
      Value *LHS = Cmp->getOperand(0);
      Value *RHS = Cmp->getOperand(1);
      // If we can prove either side non-zero, then equality must imply
      // equivalence.
      // FIXME: We should do this optimization if 'no signed zeros' is
      // applicable via an instruction-level fast-math-flag or some other
      // indicator that relaxed FP semantics are being used.
      if (isa<ConstantFP>(LHS) && !cast<ConstantFP>(LHS)->isZero())
        return true;
      if (isa<ConstantFP>(RHS) && !cast<ConstantFP>(RHS)->isZero())
        return true;
      // TODO: Handle vector floating point constants
  }
  return false;
}


static bool hasUsersIn(Value *V, BasicBlock *BB) {
  return llvm::any_of(V->users(), [BB](User *U) {
    auto *I = dyn_cast<Instruction>(U);
    return I && I->getParent() == BB;
  });
}

bool GVNPass::processAssumeIntrinsic(AssumeInst *IntrinsicI) {
  Value *V = IntrinsicI->getArgOperand(0);

  if (ConstantInt *Cond = dyn_cast<ConstantInt>(V)) {
    if (Cond->isZero()) {
      Type *Int8Ty = Type::getInt8Ty(V->getContext());
      Type *PtrTy = PointerType::get(V->getContext(), 0);
      // Insert a new store to null instruction before the load to indicate that
      // this code is not reachable.  FIXME: We could insert unreachable
      // instruction directly because we can modify the CFG.
      auto *NewS =
          new StoreInst(PoisonValue::get(Int8Ty), Constant::getNullValue(PtrTy),
                        IntrinsicI->getIterator());
      if (MSSAU) {
        const MemoryUseOrDef *FirstNonDom = nullptr;
        const auto *AL =
            MSSAU->getMemorySSA()->getBlockAccesses(IntrinsicI->getParent());

        // If there are accesses in the current basic block, find the first one
        // that does not come before NewS. The new memory access is inserted
        // after the found access or before the terminator if no such access is
        // found.
        if (AL) {
          for (const auto &Acc : *AL) {
            if (auto *Current = dyn_cast<MemoryUseOrDef>(&Acc))
              if (!Current->getMemoryInst()->comesBefore(NewS)) {
                FirstNonDom = Current;
                break;
              }
          }
        }

        auto *NewDef =
            FirstNonDom ? MSSAU->createMemoryAccessBefore(
                              NewS, nullptr,
                              const_cast<MemoryUseOrDef *>(FirstNonDom))
                        : MSSAU->createMemoryAccessInBB(
                              NewS, nullptr,
                              NewS->getParent(), MemorySSA::BeforeTerminator);

        MSSAU->insertDef(cast<MemoryDef>(NewDef), /*RenameUses=*/false);
      }
    }
    if (isAssumeWithEmptyBundle(*IntrinsicI)) {
      markInstructionForDeletion(IntrinsicI);
      return true;
    }
    return false;
  }

  if (isa<Constant>(V)) {
    // If it's not false, and constant, it must evaluate to true. This means our
    // assume is assume(true), and thus, pointless, and we don't want to do
    // anything more here.
    return false;
  }

  Constant *True = ConstantInt::getTrue(V->getContext());
  bool Changed = false;

  for (BasicBlock *Successor : successors(IntrinsicI->getParent())) {
    BasicBlockEdge Edge(IntrinsicI->getParent(), Successor);

    // This property is only true in dominated successors, propagateEquality
    // will check dominance for us.
    Changed |= propagateEquality(V, True, Edge, false);
  }

  // We can replace assume value with true, which covers cases like this:
  // call void @llvm.assume(i1 %cmp)
  // br i1 %cmp, label %bb1, label %bb2 ; will change %cmp to true
  ReplaceOperandsWithMap[V] = True;

  // Similarly, after assume(!NotV) we know that NotV == false.
  Value *NotV;
  if (match(V, m_Not(m_Value(NotV))))
    ReplaceOperandsWithMap[NotV] = ConstantInt::getFalse(V->getContext());

  // If we find an equality fact, canonicalize all dominated uses in this block
  // to one of the two values.  We heuristically choice the "oldest" of the
  // two where age is determined by value number. (Note that propagateEquality
  // above handles the cross block case.)
  //
  // Key case to cover are:
  // 1)
  // %cmp = fcmp oeq float 3.000000e+00, %0 ; const on lhs could happen
  // call void @llvm.assume(i1 %cmp)
  // ret float %0 ; will change it to ret float 3.000000e+00
  // 2)
  // %load = load float, float* %addr
  // %cmp = fcmp oeq float %load, %0
  // call void @llvm.assume(i1 %cmp)
  // ret float %load ; will change it to ret float %0
  if (auto *CmpI = dyn_cast<CmpInst>(V)) {
    if (impliesEquivalanceIfTrue(CmpI)) {
      Value *CmpLHS = CmpI->getOperand(0);
      Value *CmpRHS = CmpI->getOperand(1);
      // Heuristically pick the better replacement -- the choice of heuristic
      // isn't terribly important here, but the fact we canonicalize on some
      // replacement is for exposing other simplifications.
      // TODO: pull this out as a helper function and reuse w/existing
      // (slightly different) logic.
      if (isa<Constant>(CmpLHS) && !isa<Constant>(CmpRHS))
        std::swap(CmpLHS, CmpRHS);
      if (!isa<Instruction>(CmpLHS) && isa<Instruction>(CmpRHS))
        std::swap(CmpLHS, CmpRHS);
      if ((isa<Argument>(CmpLHS) && isa<Argument>(CmpRHS)) ||
          (isa<Instruction>(CmpLHS) && isa<Instruction>(CmpRHS))) {
        // Move the 'oldest' value to the right-hand side, using the value
        // number as a proxy for age.
        uint32_t LVN = VN.lookupOrAdd(CmpLHS);
        uint32_t RVN = VN.lookupOrAdd(CmpRHS);
        if (LVN < RVN)
          std::swap(CmpLHS, CmpRHS);
      }

      // Handle degenerate case where we either haven't pruned a dead path or a
      // removed a trivial assume yet.
      if (isa<Constant>(CmpLHS) && isa<Constant>(CmpRHS))
        return Changed;

      LLVM_DEBUG(dbgs() << "Replacing dominated uses of "
                 << *CmpLHS << " with "
                 << *CmpRHS << " in block "
                 << IntrinsicI->getParent()->getName() << "\n");


      // Setup the replacement map - this handles uses within the same block
      if (hasUsersIn(CmpLHS, IntrinsicI->getParent()))
        ReplaceOperandsWithMap[CmpLHS] = CmpRHS;

      // NOTE: The non-block local cases are handled by the call to
      // propagateEquality above; this block is just about handling the block
      // local cases.  TODO: There's a bunch of logic in propagateEqualiy which
      // isn't duplicated for the block local case, can we share it somehow?
    }
  }
  return Changed;
}

static void patchAndReplaceAllUsesWith(Instruction *I, Value *Repl) {
  patchReplacementInstruction(I, Repl);
  I->replaceAllUsesWith(Repl);
}

/// Attempt to eliminate a load, first by eliminating it
/// locally, and then attempting non-local elimination if that fails.
bool GVNPass::processLoad(LoadInst *L) {
  if (!MD)
    return false;

  // This code hasn't been audited for ordered or volatile memory access
  if (!L->isUnordered())
    return false;

  if (L->use_empty()) {
    markInstructionForDeletion(L);
    return true;
  }

  // ... to a pointer that has been loaded from before...
  MemDepResult Dep = MD->getDependency(L);

  // If it is defined in another block, try harder.
  if (Dep.isNonLocal())
    return processNonLocalLoad(L);

  // Only handle the local case below
  if (!Dep.isLocal()) {
    // This might be a NonFuncLocal or an Unknown
    LLVM_DEBUG(
        // fast print dep, using operator<< on instruction is too slow.
        dbgs() << "GVN: load "; L->printAsOperand(dbgs());
        dbgs() << " has unknown dependence\n";);
    return false;
  }

  auto AV = AnalyzeLoadAvailability(L, Dep, L->getPointerOperand());
  if (!AV)
    return false;

  Value *AvailableValue = AV->MaterializeAdjustedValue(L, L, *this);

  // MaterializeAdjustedValue is responsible for combining metadata.
  ICF->removeUsersOf(L);
  L->replaceAllUsesWith(AvailableValue);
  markInstructionForDeletion(L);
  if (MSSAU)
    MSSAU->removeMemoryAccess(L);
  ++NumGVNLoad;
  reportLoadElim(L, AvailableValue, ORE);
  // Tell MDA to reexamine the reused pointer since we might have more
  // information after forwarding it.
  if (MD && AvailableValue->getType()->isPtrOrPtrVectorTy())
    MD->invalidateCachedPointerInfo(AvailableValue);
  return true;
}

/// Return a pair the first field showing the value number of \p Exp and the
/// second field showing whether it is a value number newly created.
std::pair<uint32_t, bool>
GVNPass::ValueTable::assignExpNewValueNum(Expression &Exp) {
  uint32_t &e = expressionNumbering[Exp];
  bool CreateNewValNum = !e;
  if (CreateNewValNum) {
    Expressions.push_back(Exp);
    if (ExprIdx.size() < nextValueNumber + 1)
      ExprIdx.resize(nextValueNumber * 2);
    e = nextValueNumber;
    ExprIdx[nextValueNumber++] = nextExprNumber++;
  }
  return {e, CreateNewValNum};
}

/// Return whether all the values related with the same \p num are
/// defined in \p BB.
bool GVNPass::ValueTable::areAllValsInBB(uint32_t Num, const BasicBlock *BB,
                                         GVNPass &Gvn) {
  return all_of(
      Gvn.LeaderTable.getLeaders(Num),
      [=](const LeaderMap::LeaderTableEntry &L) { return L.BB == BB; });
}

/// Wrap phiTranslateImpl to provide caching functionality.
uint32_t GVNPass::ValueTable::phiTranslate(const BasicBlock *Pred,
                                           const BasicBlock *PhiBlock,
                                           uint32_t Num, GVNPass &Gvn) {
  auto FindRes = PhiTranslateTable.find({Num, Pred});
  if (FindRes != PhiTranslateTable.end())
    return FindRes->second;
  uint32_t NewNum = phiTranslateImpl(Pred, PhiBlock, Num, Gvn);
  PhiTranslateTable.insert({{Num, Pred}, NewNum});
  return NewNum;
}

// Return true if the value number \p Num and NewNum have equal value.
// Return false if the result is unknown.
bool GVNPass::ValueTable::areCallValsEqual(uint32_t Num, uint32_t NewNum,
                                           const BasicBlock *Pred,
                                           const BasicBlock *PhiBlock,
                                           GVNPass &Gvn) {
  CallInst *Call = nullptr;
  auto Leaders = Gvn.LeaderTable.getLeaders(Num);
  for (const auto &Entry : Leaders) {
    Call = dyn_cast<CallInst>(Entry.Val);
    if (Call && Call->getParent() == PhiBlock)
      break;
  }

  if (AA->doesNotAccessMemory(Call))
    return true;

  if (!MD || !AA->onlyReadsMemory(Call))
    return false;

  MemDepResult local_dep = MD->getDependency(Call);
  if (!local_dep.isNonLocal())
    return false;

  const MemoryDependenceResults::NonLocalDepInfo &deps =
      MD->getNonLocalCallDependency(Call);

  // Check to see if the Call has no function local clobber.
  for (const NonLocalDepEntry &D : deps) {
    if (D.getResult().isNonFuncLocal())
      return true;
  }
  return false;
}

/// Translate value number \p Num using phis, so that it has the values of
/// the phis in BB.
uint32_t GVNPass::ValueTable::phiTranslateImpl(const BasicBlock *Pred,
                                               const BasicBlock *PhiBlock,
                                               uint32_t Num, GVNPass &Gvn) {
  if (PHINode *PN = NumberingPhi[Num]) {
    for (unsigned i = 0; i != PN->getNumIncomingValues(); ++i) {
      if (PN->getParent() == PhiBlock && PN->getIncomingBlock(i) == Pred)
        if (uint32_t TransVal = lookup(PN->getIncomingValue(i), false))
          return TransVal;
    }
    return Num;
  }

  // If there is any value related with Num is defined in a BB other than
  // PhiBlock, it cannot depend on a phi in PhiBlock without going through
  // a backedge. We can do an early exit in that case to save compile time.
  if (!areAllValsInBB(Num, PhiBlock, Gvn))
    return Num;

  if (Num >= ExprIdx.size() || ExprIdx[Num] == 0)
    return Num;
  Expression Exp = Expressions[ExprIdx[Num]];

  for (unsigned i = 0; i < Exp.varargs.size(); i++) {
    // For InsertValue and ExtractValue, some varargs are index numbers
    // instead of value numbers. Those index numbers should not be
    // translated.
    if ((i > 1 && Exp.opcode == Instruction::InsertValue) ||
        (i > 0 && Exp.opcode == Instruction::ExtractValue) ||
        (i > 1 && Exp.opcode == Instruction::ShuffleVector))
      continue;
    Exp.varargs[i] = phiTranslate(Pred, PhiBlock, Exp.varargs[i], Gvn);
  }

  if (Exp.commutative) {
    assert(Exp.varargs.size() >= 2 && "Unsupported commutative instruction!");
    if (Exp.varargs[0] > Exp.varargs[1]) {
      std::swap(Exp.varargs[0], Exp.varargs[1]);
      uint32_t Opcode = Exp.opcode >> 8;
      if (Opcode == Instruction::ICmp || Opcode == Instruction::FCmp)
        Exp.opcode = (Opcode << 8) |
                     CmpInst::getSwappedPredicate(
                         static_cast<CmpInst::Predicate>(Exp.opcode & 255));
    }
  }

  if (uint32_t NewNum = expressionNumbering[Exp]) {
    if (Exp.opcode == Instruction::Call && NewNum != Num)
      return areCallValsEqual(Num, NewNum, Pred, PhiBlock, Gvn) ? NewNum : Num;
    return NewNum;
  }
  return Num;
}

/// Erase stale entry from phiTranslate cache so phiTranslate can be computed
/// again.
void GVNPass::ValueTable::eraseTranslateCacheEntry(
    uint32_t Num, const BasicBlock &CurrBlock) {
  for (const BasicBlock *Pred : predecessors(&CurrBlock))
    PhiTranslateTable.erase({Num, Pred});
}

// In order to find a leader for a given value number at a
// specific basic block, we first obtain the list of all Values for that number,
// and then scan the list to find one whose block dominates the block in
// question.  This is fast because dominator tree queries consist of only
// a few comparisons of DFS numbers.
Value *GVNPass::findLeader(const BasicBlock *BB, uint32_t num) {
  auto Leaders = LeaderTable.getLeaders(num);
  if (Leaders.empty())
    return nullptr;

  Value *Val = nullptr;
  for (const auto &Entry : Leaders) {
    if (DT->dominates(Entry.BB, BB)) {
      Val = Entry.Val;
      if (isa<Constant>(Val))
        return Val;
    }
  }

  return Val;
}

/// There is an edge from 'Src' to 'Dst'.  Return
/// true if every path from the entry block to 'Dst' passes via this edge.  In
/// particular 'Dst' must not be reachable via another edge from 'Src'.
static bool isOnlyReachableViaThisEdge(const BasicBlockEdge &E,
                                       DominatorTree *DT) {
  // While in theory it is interesting to consider the case in which Dst has
  // more than one predecessor, because Dst might be part of a loop which is
  // only reachable from Src, in practice it is pointless since at the time
  // GVN runs all such loops have preheaders, which means that Dst will have
  // been changed to have only one predecessor, namely Src.
  const BasicBlock *Pred = E.getEnd()->getSinglePredecessor();
  assert((!Pred || Pred == E.getStart()) &&
         "No edge between these basic blocks!");
  return Pred != nullptr;
}

void GVNPass::assignBlockRPONumber(Function &F) {
  BlockRPONumber.clear();
  uint32_t NextBlockNumber = 1;
  ReversePostOrderTraversal<Function *> RPOT(&F);
  for (BasicBlock *BB : RPOT)
    BlockRPONumber[BB] = NextBlockNumber++;
  InvalidBlockRPONumbers = false;
}

bool GVNPass::replaceOperandsForInBlockEquality(Instruction *Instr) const {
  bool Changed = false;
  for (unsigned OpNum = 0; OpNum < Instr->getNumOperands(); ++OpNum) {
    Value *Operand = Instr->getOperand(OpNum);
    auto it = ReplaceOperandsWithMap.find(Operand);
    if (it != ReplaceOperandsWithMap.end()) {
      LLVM_DEBUG(dbgs() << "GVN replacing: " << *Operand << " with "
                        << *it->second << " in instruction " << *Instr << '\n');
      Instr->setOperand(OpNum, it->second);
      Changed = true;
    }
  }
  return Changed;
}

/// The given values are known to be equal in every block
/// dominated by 'Root'.  Exploit this, for example by replacing 'LHS' with
/// 'RHS' everywhere in the scope.  Returns whether a change was made.
/// If DominatesByEdge is false, then it means that we will propagate the RHS
/// value starting from the end of Root.Start.
bool GVNPass::propagateEquality(Value *LHS, Value *RHS,
                                const BasicBlockEdge &Root,
                                bool DominatesByEdge) {
  SmallVector<std::pair<Value*, Value*>, 4> Worklist;
  Worklist.push_back(std::make_pair(LHS, RHS));
  bool Changed = false;
  // For speed, compute a conservative fast approximation to
  // DT->dominates(Root, Root.getEnd());
  const bool RootDominatesEnd = isOnlyReachableViaThisEdge(Root, DT);

  while (!Worklist.empty()) {
    std::pair<Value*, Value*> Item = Worklist.pop_back_val();
    LHS = Item.first; RHS = Item.second;

    if (LHS == RHS)
      continue;
    assert(LHS->getType() == RHS->getType() && "Equality but unequal types!");

    // Don't try to propagate equalities between constants.
    if (isa<Constant>(LHS) && isa<Constant>(RHS))
      continue;

    // Prefer a constant on the right-hand side, or an Argument if no constants.
    if (isa<Constant>(LHS) || (isa<Argument>(LHS) && !isa<Constant>(RHS)))
      std::swap(LHS, RHS);
    assert((isa<Argument>(LHS) || isa<Instruction>(LHS)) && "Unexpected value!");
    const DataLayout &DL =
        isa<Argument>(LHS)
            ? cast<Argument>(LHS)->getParent()->getDataLayout()
            : cast<Instruction>(LHS)->getDataLayout();

    // If there is no obvious reason to prefer the left-hand side over the
    // right-hand side, ensure the longest lived term is on the right-hand side,
    // so the shortest lived term will be replaced by the longest lived.
    // This tends to expose more simplifications.
    uint32_t LVN = VN.lookupOrAdd(LHS);
    if ((isa<Argument>(LHS) && isa<Argument>(RHS)) ||
        (isa<Instruction>(LHS) && isa<Instruction>(RHS))) {
      // Move the 'oldest' value to the right-hand side, using the value number
      // as a proxy for age.
      uint32_t RVN = VN.lookupOrAdd(RHS);
      if (LVN < RVN) {
        std::swap(LHS, RHS);
        LVN = RVN;
      }
    }

    // If value numbering later sees that an instruction in the scope is equal
    // to 'LHS' then ensure it will be turned into 'RHS'.  In order to preserve
    // the invariant that instructions only occur in the leader table for their
    // own value number (this is used by removeFromLeaderTable), do not do this
    // if RHS is an instruction (if an instruction in the scope is morphed into
    // LHS then it will be turned into RHS by the next GVN iteration anyway, so
    // using the leader table is about compiling faster, not optimizing better).
    // The leader table only tracks basic blocks, not edges. Only add to if we
    // have the simple case where the edge dominates the end.
    if (RootDominatesEnd && !isa<Instruction>(RHS) &&
        canReplacePointersIfEqual(LHS, RHS, DL))
      LeaderTable.insert(LVN, RHS, Root.getEnd());

    // Replace all occurrences of 'LHS' with 'RHS' everywhere in the scope.  As
    // LHS always has at least one use that is not dominated by Root, this will
    // never do anything if LHS has only one use.
    if (!LHS->hasOneUse()) {
      // Create a callback that captures the DL.
      auto canReplacePointersCallBack = [&DL](const Use &U, const Value *To) {
        return canReplacePointersInUseIfEqual(U, To, DL);
      };
      unsigned NumReplacements =
          DominatesByEdge
              ? replaceDominatedUsesWithIf(LHS, RHS, *DT, Root,
                                           canReplacePointersCallBack)
              : replaceDominatedUsesWithIf(LHS, RHS, *DT, Root.getStart(),
                                           canReplacePointersCallBack);

      if (NumReplacements > 0) {
        Changed = true;
        NumGVNEqProp += NumReplacements;
        // Cached information for anything that uses LHS will be invalid.
        if (MD)
          MD->invalidateCachedPointerInfo(LHS);
      }
    }

    // Now try to deduce additional equalities from this one. For example, if
    // the known equality was "(A != B)" == "false" then it follows that A and B
    // are equal in the scope. Only boolean equalities with an explicit true or
    // false RHS are currently supported.
    if (!RHS->getType()->isIntegerTy(1))
      // Not a boolean equality - bail out.
      continue;
    ConstantInt *CI = dyn_cast<ConstantInt>(RHS);
    if (!CI)
      // RHS neither 'true' nor 'false' - bail out.
      continue;
    // Whether RHS equals 'true'.  Otherwise it equals 'false'.
    bool isKnownTrue = CI->isMinusOne();
    bool isKnownFalse = !isKnownTrue;

    // If "A && B" is known true then both A and B are known true.  If "A || B"
    // is known false then both A and B are known false.
    Value *A, *B;
    if ((isKnownTrue && match(LHS, m_LogicalAnd(m_Value(A), m_Value(B)))) ||
        (isKnownFalse && match(LHS, m_LogicalOr(m_Value(A), m_Value(B))))) {
      Worklist.push_back(std::make_pair(A, RHS));
      Worklist.push_back(std::make_pair(B, RHS));
      continue;
    }

    // If we are propagating an equality like "(A == B)" == "true" then also
    // propagate the equality A == B.  When propagating a comparison such as
    // "(A >= B)" == "true", replace all instances of "A < B" with "false".
    if (CmpInst *Cmp = dyn_cast<CmpInst>(LHS)) {
      Value *Op0 = Cmp->getOperand(0), *Op1 = Cmp->getOperand(1);

      // If "A == B" is known true, or "A != B" is known false, then replace
      // A with B everywhere in the scope.  For floating point operations, we
      // have to be careful since equality does not always imply equivalance.
      if ((isKnownTrue && impliesEquivalanceIfTrue(Cmp)) ||
          (isKnownFalse && impliesEquivalanceIfFalse(Cmp)))
        Worklist.push_back(std::make_pair(Op0, Op1));

      // If "A >= B" is known true, replace "A < B" with false everywhere.
      CmpInst::Predicate NotPred = Cmp->getInversePredicate();
      Constant *NotVal = ConstantInt::get(Cmp->getType(), isKnownFalse);
      // Since we don't have the instruction "A < B" immediately to hand, work
      // out the value number that it would have and use that to find an
      // appropriate instruction (if any).
      uint32_t NextNum = VN.getNextUnusedValueNumber();
      uint32_t Num = VN.lookupOrAddCmp(Cmp->getOpcode(), NotPred, Op0, Op1);
      // If the number we were assigned was brand new then there is no point in
      // looking for an instruction realizing it: there cannot be one!
      if (Num < NextNum) {
        Value *NotCmp = findLeader(Root.getEnd(), Num);
        if (NotCmp && isa<Instruction>(NotCmp)) {
          unsigned NumReplacements =
              DominatesByEdge
                  ? replaceDominatedUsesWith(NotCmp, NotVal, *DT, Root)
                  : replaceDominatedUsesWith(NotCmp, NotVal, *DT,
                                             Root.getStart());
          Changed |= NumReplacements > 0;
          NumGVNEqProp += NumReplacements;
          // Cached information for anything that uses NotCmp will be invalid.
          if (MD)
            MD->invalidateCachedPointerInfo(NotCmp);
        }
      }
      // Ensure that any instruction in scope that gets the "A < B" value number
      // is replaced with false.
      // The leader table only tracks basic blocks, not edges. Only add to if we
      // have the simple case where the edge dominates the end.
      if (RootDominatesEnd)
        LeaderTable.insert(Num, NotVal, Root.getEnd());

      continue;
    }
  }

  return Changed;
}

/// When calculating availability, handle an instruction
/// by inserting it into the appropriate sets
bool GVNPass::processInstruction(Instruction *I) {
  // Ignore dbg info intrinsics.
  if (isa<DbgInfoIntrinsic>(I))
    return false;

  // If the instruction can be easily simplified then do so now in preference
  // to value numbering it.  Value numbering often exposes redundancies, for
  // example if it determines that %y is equal to %x then the instruction
  // "%z = and i32 %x, %y" becomes "%z = and i32 %x, %x" which we now simplify.
  const DataLayout &DL = I->getDataLayout();
  if (Value *V = simplifyInstruction(I, {DL, TLI, DT, AC})) {
    bool Changed = false;
    if (!I->use_empty()) {
      // Simplification can cause a special instruction to become not special.
      // For example, devirtualization to a willreturn function.
      ICF->removeUsersOf(I);
      I->replaceAllUsesWith(V);
      Changed = true;
    }
    if (isInstructionTriviallyDead(I, TLI)) {
      markInstructionForDeletion(I);
      Changed = true;
    }
    if (Changed) {
      if (MD && V->getType()->isPtrOrPtrVectorTy())
        MD->invalidateCachedPointerInfo(V);
      ++NumGVNSimpl;
      return true;
    }
  }

  if (auto *Assume = dyn_cast<AssumeInst>(I))
    return processAssumeIntrinsic(Assume);

  if (LoadInst *Load = dyn_cast<LoadInst>(I)) {
    if (processLoad(Load))
      return true;

    unsigned Num = VN.lookupOrAdd(Load);
    LeaderTable.insert(Num, Load, Load->getParent());
    return false;
  }

  // For conditional branches, we can perform simple conditional propagation on
  // the condition value itself.
  if (BranchInst *BI = dyn_cast<BranchInst>(I)) {
    if (!BI->isConditional())
      return false;

    if (isa<Constant>(BI->getCondition()))
      return processFoldableCondBr(BI);

    Value *BranchCond = BI->getCondition();
    BasicBlock *TrueSucc = BI->getSuccessor(0);
    BasicBlock *FalseSucc = BI->getSuccessor(1);
    // Avoid multiple edges early.
    if (TrueSucc == FalseSucc)
      return false;

    BasicBlock *Parent = BI->getParent();
    bool Changed = false;

    Value *TrueVal = ConstantInt::getTrue(TrueSucc->getContext());
    BasicBlockEdge TrueE(Parent, TrueSucc);
    Changed |= propagateEquality(BranchCond, TrueVal, TrueE, true);

    Value *FalseVal = ConstantInt::getFalse(FalseSucc->getContext());
    BasicBlockEdge FalseE(Parent, FalseSucc);
    Changed |= propagateEquality(BranchCond, FalseVal, FalseE, true);

    return Changed;
  }

  // For switches, propagate the case values into the case destinations.
  if (SwitchInst *SI = dyn_cast<SwitchInst>(I)) {
    Value *SwitchCond = SI->getCondition();
    BasicBlock *Parent = SI->getParent();
    bool Changed = false;

    // Remember how many outgoing edges there are to every successor.
    SmallDenseMap<BasicBlock *, unsigned, 16> SwitchEdges;
    for (BasicBlock *Succ : successors(Parent))
      ++SwitchEdges[Succ];

    for (SwitchInst::CaseIt i = SI->case_begin(), e = SI->case_end();
         i != e; ++i) {
      BasicBlock *Dst = i->getCaseSuccessor();
      // If there is only a single edge, propagate the case value into it.
      if (SwitchEdges.lookup(Dst) == 1) {
        BasicBlockEdge E(Parent, Dst);
        Changed |= propagateEquality(SwitchCond, i->getCaseValue(), E, true);
      }
    }
    return Changed;
  }

  // Instructions with void type don't return a value, so there's
  // no point in trying to find redundancies in them.
  if (I->getType()->isVoidTy())
    return false;

  uint32_t NextNum = VN.getNextUnusedValueNumber();
  unsigned Num = VN.lookupOrAdd(I);

  // Allocations are always uniquely numbered, so we can save time and memory
  // by fast failing them.
  if (isa<AllocaInst>(I) || I->isTerminator() || isa<PHINode>(I)) {
    LeaderTable.insert(Num, I, I->getParent());
    return false;
  }

  // If the number we were assigned was a brand new VN, then we don't
  // need to do a lookup to see if the number already exists
  // somewhere in the domtree: it can't!
  if (Num >= NextNum) {
    LeaderTable.insert(Num, I, I->getParent());
    return false;
  }

  // Perform fast-path value-number based elimination of values inherited from
  // dominators.
  Value *Repl = findLeader(I->getParent(), Num);
  if (!Repl) {
    // Failure, just remember this instance for future use.
    LeaderTable.insert(Num, I, I->getParent());
    return false;
  }

  if (Repl == I) {
    // If I was the result of a shortcut PRE, it might already be in the table
    // and the best replacement for itself. Nothing to do.
    return false;
  }

  // Remove it!
  patchAndReplaceAllUsesWith(I, Repl);
  if (MD && Repl->getType()->isPtrOrPtrVectorTy())
    MD->invalidateCachedPointerInfo(Repl);
  markInstructionForDeletion(I);
  return true;
}

/// runOnFunction - This is the main transformation entry point for a function.
bool GVNPass::runImpl(Function &F, AssumptionCache &RunAC, DominatorTree &RunDT,
                      const TargetLibraryInfo &RunTLI, AAResults &RunAA,
                      MemoryDependenceResults *RunMD, LoopInfo &LI,
                      OptimizationRemarkEmitter *RunORE, MemorySSA *MSSA) {
  AC = &RunAC;
  DT = &RunDT;
  VN.setDomTree(DT);
  TLI = &RunTLI;
  VN.setAliasAnalysis(&RunAA);
  MD = RunMD;
  ImplicitControlFlowTracking ImplicitCFT;
  ICF = &ImplicitCFT;
  this->LI = &LI;
  VN.setMemDep(MD);
  ORE = RunORE;
  InvalidBlockRPONumbers = true;
  MemorySSAUpdater Updater(MSSA);
  MSSAU = MSSA ? &Updater : nullptr;

  bool Changed = false;
  bool ShouldContinue = true;

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  // Merge unconditional branches, allowing PRE to catch more
  // optimization opportunities.
  for (BasicBlock &BB : llvm::make_early_inc_range(F)) {
    bool removedBlock = MergeBlockIntoPredecessor(&BB, &DTU, &LI, MSSAU, MD);
    if (removedBlock)
      ++NumGVNBlocks;

    Changed |= removedBlock;
  }
  DTU.flush();

  unsigned Iteration = 0;
  while (ShouldContinue) {
    LLVM_DEBUG(dbgs() << "GVN iteration: " << Iteration << "\n");
    (void) Iteration;
    ShouldContinue = iterateOnFunction(F);
    Changed |= ShouldContinue;
    ++Iteration;
  }

  if (isPREEnabled()) {
    // Fabricate val-num for dead-code in order to suppress assertion in
    // performPRE().
    assignValNumForDeadCode();
    bool PREChanged = true;
    while (PREChanged) {
      PREChanged = performPRE(F);
      Changed |= PREChanged;
    }
  }

  // FIXME: Should perform GVN again after PRE does something.  PRE can move
  // computations into blocks where they become fully redundant.  Note that
  // we can't do this until PRE's critical edge splitting updates memdep.
  // Actually, when this happens, we should just fully integrate PRE into GVN.

  cleanupGlobalSets();
  // Do not cleanup DeadBlocks in cleanupGlobalSets() as it's called for each
  // iteration.
  DeadBlocks.clear();

  if (MSSA && VerifyMemorySSA)
    MSSA->verifyMemorySSA();

  return Changed;
}

bool GVNPass::processBlock(BasicBlock *BB) {
  // FIXME: Kill off InstrsToErase by doing erasing eagerly in a helper function
  // (and incrementing BI before processing an instruction).
  assert(InstrsToErase.empty() &&
         "We expect InstrsToErase to be empty across iterations");
  if (DeadBlocks.count(BB))
    return false;

  // Clearing map before every BB because it can be used only for single BB.
  ReplaceOperandsWithMap.clear();
  bool ChangedFunction = false;

  // Since we may not have visited the input blocks of the phis, we can't
  // use our normal hash approach for phis.  Instead, simply look for
  // obvious duplicates.  The first pass of GVN will tend to create
  // identical phis, and the second or later passes can eliminate them.
  SmallPtrSet<PHINode *, 8> PHINodesToRemove;
  ChangedFunction |= EliminateDuplicatePHINodes(BB, PHINodesToRemove);
  for (PHINode *PN : PHINodesToRemove) {
    VN.erase(PN);
    removeInstruction(PN);
  }

  for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
       BI != BE;) {
    if (!ReplaceOperandsWithMap.empty())
      ChangedFunction |= replaceOperandsForInBlockEquality(&*BI);
    ChangedFunction |= processInstruction(&*BI);

    if (InstrsToErase.empty()) {
      ++BI;
      continue;
    }

    // If we need some instructions deleted, do it now.
    NumGVNInstr += InstrsToErase.size();

    // Avoid iterator invalidation.
    bool AtStart = BI == BB->begin();
    if (!AtStart)
      --BI;

    for (auto *I : InstrsToErase) {
      assert(I->getParent() == BB && "Removing instruction from wrong block?");
      LLVM_DEBUG(dbgs() << "GVN removed: " << *I << '\n');
      salvageKnowledge(I, AC);
      salvageDebugInfo(*I);
      removeInstruction(I);
    }
    InstrsToErase.clear();

    if (AtStart)
      BI = BB->begin();
    else
      ++BI;
  }

  return ChangedFunction;
}

// Instantiate an expression in a predecessor that lacked it.
bool GVNPass::performScalarPREInsertion(Instruction *Instr, BasicBlock *Pred,
                                        BasicBlock *Curr, unsigned int ValNo) {
  // Because we are going top-down through the block, all value numbers
  // will be available in the predecessor by the time we need them.  Any
  // that weren't originally present will have been instantiated earlier
  // in this loop.
  bool success = true;
  for (unsigned i = 0, e = Instr->getNumOperands(); i != e; ++i) {
    Value *Op = Instr->getOperand(i);
    if (isa<Argument>(Op) || isa<Constant>(Op) || isa<GlobalValue>(Op))
      continue;
    // This could be a newly inserted instruction, in which case, we won't
    // find a value number, and should give up before we hurt ourselves.
    // FIXME: Rewrite the infrastructure to let it easier to value number
    // and process newly inserted instructions.
    if (!VN.exists(Op)) {
      success = false;
      break;
    }
    uint32_t TValNo =
        VN.phiTranslate(Pred, Curr, VN.lookup(Op), *this);
    if (Value *V = findLeader(Pred, TValNo)) {
      Instr->setOperand(i, V);
    } else {
      success = false;
      break;
    }
  }

  // Fail out if we encounter an operand that is not available in
  // the PRE predecessor.  This is typically because of loads which
  // are not value numbered precisely.
  if (!success)
    return false;

  Instr->insertBefore(Pred->getTerminator());
  Instr->setName(Instr->getName() + ".pre");
  Instr->setDebugLoc(Instr->getDebugLoc());

  ICF->insertInstructionTo(Instr, Pred);

  unsigned Num = VN.lookupOrAdd(Instr);
  VN.add(Instr, Num);

  // Update the availability map to include the new instruction.
  LeaderTable.insert(Num, Instr, Pred);
  return true;
}

bool GVNPass::performScalarPRE(Instruction *CurInst) {
  if (isa<AllocaInst>(CurInst) || CurInst->isTerminator() ||
      isa<PHINode>(CurInst) || CurInst->getType()->isVoidTy() ||
      CurInst->mayReadFromMemory() || CurInst->mayHaveSideEffects() ||
      isa<DbgInfoIntrinsic>(CurInst))
    return false;

  // Don't do PRE on compares. The PHI would prevent CodeGenPrepare from
  // sinking the compare again, and it would force the code generator to
  // move the i1 from processor flags or predicate registers into a general
  // purpose register.
  if (isa<CmpInst>(CurInst))
    return false;

  // Don't do PRE on GEPs. The inserted PHI would prevent CodeGenPrepare from
  // sinking the addressing mode computation back to its uses. Extending the
  // GEP's live range increases the register pressure, and therefore it can
  // introduce unnecessary spills.
  //
  // This doesn't prevent Load PRE. PHI translation will make the GEP available
  // to the load by moving it to the predecessor block if necessary.
  if (isa<GetElementPtrInst>(CurInst))
    return false;

  if (auto *CallB = dyn_cast<CallBase>(CurInst)) {
    // We don't currently value number ANY inline asm calls.
    if (CallB->isInlineAsm())
      return false;
  }

  uint32_t ValNo = VN.lookup(CurInst);

  // Look for the predecessors for PRE opportunities.  We're
  // only trying to solve the basic diamond case, where
  // a value is computed in the successor and one predecessor,
  // but not the other.  We also explicitly disallow cases
  // where the successor is its own predecessor, because they're
  // more complicated to get right.
  unsigned NumWith = 0;
  unsigned NumWithout = 0;
  BasicBlock *PREPred = nullptr;
  BasicBlock *CurrentBlock = CurInst->getParent();

  // Update the RPO numbers for this function.
  if (InvalidBlockRPONumbers)
    assignBlockRPONumber(*CurrentBlock->getParent());

  SmallVector<std::pair<Value *, BasicBlock *>, 8> predMap;
  for (BasicBlock *P : predecessors(CurrentBlock)) {
    // We're not interested in PRE where blocks with predecessors that are
    // not reachable.
    if (!DT->isReachableFromEntry(P)) {
      NumWithout = 2;
      break;
    }
    // It is not safe to do PRE when P->CurrentBlock is a loop backedge.
    assert(BlockRPONumber.count(P) && BlockRPONumber.count(CurrentBlock) &&
           "Invalid BlockRPONumber map.");
    if (BlockRPONumber[P] >= BlockRPONumber[CurrentBlock]) {
      NumWithout = 2;
      break;
    }

    uint32_t TValNo = VN.phiTranslate(P, CurrentBlock, ValNo, *this);
    Value *predV = findLeader(P, TValNo);
    if (!predV) {
      predMap.push_back(std::make_pair(static_cast<Value *>(nullptr), P));
      PREPred = P;
      ++NumWithout;
    } else if (predV == CurInst) {
      /* CurInst dominates this predecessor. */
      NumWithout = 2;
      break;
    } else {
      predMap.push_back(std::make_pair(predV, P));
      ++NumWith;
    }
  }

  // Don't do PRE when it might increase code size, i.e. when
  // we would need to insert instructions in more than one pred.
  if (NumWithout > 1 || NumWith == 0)
    return false;

  // We may have a case where all predecessors have the instruction,
  // and we just need to insert a phi node. Otherwise, perform
  // insertion.
  Instruction *PREInstr = nullptr;

  if (NumWithout != 0) {
    if (!isSafeToSpeculativelyExecute(CurInst)) {
      // It is only valid to insert a new instruction if the current instruction
      // is always executed. An instruction with implicit control flow could
      // prevent us from doing it. If we cannot speculate the execution, then
      // PRE should be prohibited.
      if (ICF->isDominatedByICFIFromSameBlock(CurInst))
        return false;
    }

    // Don't do PRE across indirect branch.
    if (isa<IndirectBrInst>(PREPred->getTerminator()))
      return false;

    // We can't do PRE safely on a critical edge, so instead we schedule
    // the edge to be split and perform the PRE the next time we iterate
    // on the function.
    unsigned SuccNum = GetSuccessorNumber(PREPred, CurrentBlock);
    if (isCriticalEdge(PREPred->getTerminator(), SuccNum)) {
      toSplit.push_back(std::make_pair(PREPred->getTerminator(), SuccNum));
      return false;
    }
    // We need to insert somewhere, so let's give it a shot
    PREInstr = CurInst->clone();
    if (!performScalarPREInsertion(PREInstr, PREPred, CurrentBlock, ValNo)) {
      // If we failed insertion, make sure we remove the instruction.
#ifndef NDEBUG
      verifyRemoved(PREInstr);
#endif
      PREInstr->deleteValue();
      return false;
    }
  }

  // Either we should have filled in the PRE instruction, or we should
  // not have needed insertions.
  assert(PREInstr != nullptr || NumWithout == 0);

  ++NumGVNPRE;

  // Create a PHI to make the value available in this block.
  PHINode *Phi = PHINode::Create(CurInst->getType(), predMap.size(),
                                 CurInst->getName() + ".pre-phi");
  Phi->insertBefore(CurrentBlock->begin());
  for (unsigned i = 0, e = predMap.size(); i != e; ++i) {
    if (Value *V = predMap[i].first) {
      // If we use an existing value in this phi, we have to patch the original
      // value because the phi will be used to replace a later value.
      patchReplacementInstruction(CurInst, V);
      Phi->addIncoming(V, predMap[i].second);
    } else
      Phi->addIncoming(PREInstr, PREPred);
  }

  VN.add(Phi, ValNo);
  // After creating a new PHI for ValNo, the phi translate result for ValNo will
  // be changed, so erase the related stale entries in phi translate cache.
  VN.eraseTranslateCacheEntry(ValNo, *CurrentBlock);
  LeaderTable.insert(ValNo, Phi, CurrentBlock);
  Phi->setDebugLoc(CurInst->getDebugLoc());
  CurInst->replaceAllUsesWith(Phi);
  if (MD && Phi->getType()->isPtrOrPtrVectorTy())
    MD->invalidateCachedPointerInfo(Phi);
  VN.erase(CurInst);
  LeaderTable.erase(ValNo, CurInst, CurrentBlock);

  LLVM_DEBUG(dbgs() << "GVN PRE removed: " << *CurInst << '\n');
  removeInstruction(CurInst);
  ++NumGVNInstr;

  return true;
}

/// Perform a purely local form of PRE that looks for diamond
/// control flow patterns and attempts to perform simple PRE at the join point.
bool GVNPass::performPRE(Function &F) {
  bool Changed = false;
  for (BasicBlock *CurrentBlock : depth_first(&F.getEntryBlock())) {
    // Nothing to PRE in the entry block.
    if (CurrentBlock == &F.getEntryBlock())
      continue;

    // Don't perform PRE on an EH pad.
    if (CurrentBlock->isEHPad())
      continue;

    for (BasicBlock::iterator BI = CurrentBlock->begin(),
                              BE = CurrentBlock->end();
         BI != BE;) {
      Instruction *CurInst = &*BI++;
      Changed |= performScalarPRE(CurInst);
    }
  }

  if (splitCriticalEdges())
    Changed = true;

  return Changed;
}

/// Split the critical edge connecting the given two blocks, and return
/// the block inserted to the critical edge.
BasicBlock *GVNPass::splitCriticalEdges(BasicBlock *Pred, BasicBlock *Succ) {
  // GVN does not require loop-simplify, do not try to preserve it if it is not
  // possible.
  BasicBlock *BB = SplitCriticalEdge(
      Pred, Succ,
      CriticalEdgeSplittingOptions(DT, LI, MSSAU).unsetPreserveLoopSimplify());
  if (BB) {
    if (MD)
      MD->invalidateCachedPredecessors();
    InvalidBlockRPONumbers = true;
  }
  return BB;
}

/// Split critical edges found during the previous
/// iteration that may enable further optimization.
bool GVNPass::splitCriticalEdges() {
  if (toSplit.empty())
    return false;

  bool Changed = false;
  do {
    std::pair<Instruction *, unsigned> Edge = toSplit.pop_back_val();
    Changed |= SplitCriticalEdge(Edge.first, Edge.second,
                                 CriticalEdgeSplittingOptions(DT, LI, MSSAU)) !=
               nullptr;
  } while (!toSplit.empty());
  if (Changed) {
    if (MD)
      MD->invalidateCachedPredecessors();
    InvalidBlockRPONumbers = true;
  }
  return Changed;
}

/// Executes one iteration of GVN
bool GVNPass::iterateOnFunction(Function &F) {
  cleanupGlobalSets();

  // Top-down walk of the dominator tree
  bool Changed = false;
  // Needed for value numbering with phi construction to work.
  // RPOT walks the graph in its constructor and will not be invalidated during
  // processBlock.
  ReversePostOrderTraversal<Function *> RPOT(&F);

  for (BasicBlock *BB : RPOT)
    Changed |= processBlock(BB);

  return Changed;
}

void GVNPass::cleanupGlobalSets() {
  VN.clear();
  LeaderTable.clear();
  BlockRPONumber.clear();
  ICF->clear();
  InvalidBlockRPONumbers = true;
}

void GVNPass::removeInstruction(Instruction *I) {
  if (MD) MD->removeInstruction(I);
  if (MSSAU)
    MSSAU->removeMemoryAccess(I);
#ifndef NDEBUG
  verifyRemoved(I);
#endif
  ICF->removeInstruction(I);
  I->eraseFromParent();
}

/// Verify that the specified instruction does not occur in our
/// internal data structures.
void GVNPass::verifyRemoved(const Instruction *Inst) const {
  VN.verifyRemoved(Inst);
  LeaderTable.verifyRemoved(Inst);
}

/// BB is declared dead, which implied other blocks become dead as well. This
/// function is to add all these blocks to "DeadBlocks". For the dead blocks'
/// live successors, update their phi nodes by replacing the operands
/// corresponding to dead blocks with UndefVal.
void GVNPass::addDeadBlock(BasicBlock *BB) {
  SmallVector<BasicBlock *, 4> NewDead;
  SmallSetVector<BasicBlock *, 4> DF;

  NewDead.push_back(BB);
  while (!NewDead.empty()) {
    BasicBlock *D = NewDead.pop_back_val();
    if (DeadBlocks.count(D))
      continue;

    // All blocks dominated by D are dead.
    SmallVector<BasicBlock *, 8> Dom;
    DT->getDescendants(D, Dom);
    DeadBlocks.insert(Dom.begin(), Dom.end());

    // Figure out the dominance-frontier(D).
    for (BasicBlock *B : Dom) {
      for (BasicBlock *S : successors(B)) {
        if (DeadBlocks.count(S))
          continue;

        bool AllPredDead = true;
        for (BasicBlock *P : predecessors(S))
          if (!DeadBlocks.count(P)) {
            AllPredDead = false;
            break;
          }

        if (!AllPredDead) {
          // S could be proved dead later on. That is why we don't update phi
          // operands at this moment.
          DF.insert(S);
        } else {
          // While S is not dominated by D, it is dead by now. This could take
          // place if S already have a dead predecessor before D is declared
          // dead.
          NewDead.push_back(S);
        }
      }
    }
  }

  // For the dead blocks' live successors, update their phi nodes by replacing
  // the operands corresponding to dead blocks with UndefVal.
  for (BasicBlock *B : DF) {
    if (DeadBlocks.count(B))
      continue;

    // First, split the critical edges. This might also create additional blocks
    // to preserve LoopSimplify form and adjust edges accordingly.
    SmallVector<BasicBlock *, 4> Preds(predecessors(B));
    for (BasicBlock *P : Preds) {
      if (!DeadBlocks.count(P))
        continue;

      if (llvm::is_contained(successors(P), B) &&
          isCriticalEdge(P->getTerminator(), B)) {
        if (BasicBlock *S = splitCriticalEdges(P, B))
          DeadBlocks.insert(P = S);
      }
    }

    // Now poison the incoming values from the dead predecessors.
    for (BasicBlock *P : predecessors(B)) {
      if (!DeadBlocks.count(P))
        continue;
      for (PHINode &Phi : B->phis()) {
        Phi.setIncomingValueForBlock(P, PoisonValue::get(Phi.getType()));
        if (MD)
          MD->invalidateCachedPointerInfo(&Phi);
      }
    }
  }
}

// If the given branch is recognized as a foldable branch (i.e. conditional
// branch with constant condition), it will perform following analyses and
// transformation.
//  1) If the dead out-coming edge is a critical-edge, split it. Let
//     R be the target of the dead out-coming edge.
//  1) Identify the set of dead blocks implied by the branch's dead outcoming
//     edge. The result of this step will be {X| X is dominated by R}
//  2) Identify those blocks which haves at least one dead predecessor. The
//     result of this step will be dominance-frontier(R).
//  3) Update the PHIs in DF(R) by replacing the operands corresponding to
//     dead blocks with "UndefVal" in an hope these PHIs will optimized away.
//
// Return true iff *NEW* dead code are found.
bool GVNPass::processFoldableCondBr(BranchInst *BI) {
  if (!BI || BI->isUnconditional())
    return false;

  // If a branch has two identical successors, we cannot declare either dead.
  if (BI->getSuccessor(0) == BI->getSuccessor(1))
    return false;

  ConstantInt *Cond = dyn_cast<ConstantInt>(BI->getCondition());
  if (!Cond)
    return false;

  BasicBlock *DeadRoot =
      Cond->getZExtValue() ? BI->getSuccessor(1) : BI->getSuccessor(0);
  if (DeadBlocks.count(DeadRoot))
    return false;

  if (!DeadRoot->getSinglePredecessor())
    DeadRoot = splitCriticalEdges(BI->getParent(), DeadRoot);

  addDeadBlock(DeadRoot);
  return true;
}

// performPRE() will trigger assert if it comes across an instruction without
// associated val-num. As it normally has far more live instructions than dead
// instructions, it makes more sense just to "fabricate" a val-number for the
// dead code than checking if instruction involved is dead or not.
void GVNPass::assignValNumForDeadCode() {
  for (BasicBlock *BB : DeadBlocks) {
    for (Instruction &Inst : *BB) {
      unsigned ValNum = VN.lookupOrAdd(&Inst);
      LeaderTable.insert(ValNum, &Inst, BB);
    }
  }
}

class llvm::gvn::GVNLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  explicit GVNLegacyPass(bool NoMemDepAnalysis = !GVNEnableMemDep)
      : FunctionPass(ID), Impl(GVNOptions().setMemDep(!NoMemDepAnalysis)) {
    initializeGVNLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto *MSSAWP = getAnalysisIfAvailable<MemorySSAWrapperPass>();
    return Impl.runImpl(
        F, getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F),
        getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
        getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F),
        getAnalysis<AAResultsWrapperPass>().getAAResults(),
        Impl.isMemDepEnabled()
            ? &getAnalysis<MemoryDependenceWrapperPass>().getMemDep()
            : nullptr,
        getAnalysis<LoopInfoWrapperPass>().getLoopInfo(),
        &getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE(),
        MSSAWP ? &MSSAWP->getMSSA() : nullptr);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    if (Impl.isMemDepEnabled())
      AU.addRequired<MemoryDependenceWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<TargetLibraryInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
    AU.addPreserved<MemorySSAWrapperPass>();
  }

private:
  GVNPass Impl;
};

char GVNLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(GVNLegacyPass, "gvn", "Global Value Numbering", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(GVNLegacyPass, "gvn", "Global Value Numbering", false, false)

// The public interface to this file...
FunctionPass *llvm::createGVNPass(bool NoMemDepAnalysis) {
  return new GVNLegacyPass(NoMemDepAnalysis);
}
