//===- MergeFunctions.cpp - Merge identical functions ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass looks for equivalent functions that are mergable and folds them.
//
// Order relation is defined on set of functions. It was made through
// special function comparison procedure that returns
// 0 when functions are equal,
// -1 when Left function is less than right function, and
// 1 for opposite case. We need total-ordering, so we need to maintain
// four properties on the functions set:
// a <= a (reflexivity)
// if a <= b and b <= a then a = b (antisymmetry)
// if a <= b and b <= c then a <= c (transitivity).
// for all a and b: a <= b or b <= a (totality).
//
// Comparison iterates through each instruction in each basic block.
// Functions are kept on binary tree. For each new function F we perform
// lookup in binary tree.
// In practice it works the following way:
// -- We define Function* container class with custom "operator<" (FunctionPtr).
// -- "FunctionPtr" instances are stored in std::set collection, so every
//    std::set::insert operation will give you result in log(N) time.
//
// As an optimization, a hash of the function structure is calculated first, and
// two functions are only compared if they have the same hash. This hash is
// cheap to compute, and has the property that if function F == G according to
// the comparison function, then hash(F) == hash(G). This consistency property
// is critical to ensuring all possible merging opportunities are exploited.
// Collisions in the hash affect the speed of the pass but not the correctness
// or determinism of the resulting transformation.
//
// When a match is found the functions are folded. If both functions are
// overridable, we move the functionality into a new internal function and
// leave two overridable thunks to it.
//
//===----------------------------------------------------------------------===//
//
// Future work:
//
// * virtual functions.
//
// Many functions have their address taken by the virtual function table for
// the object they belong to. However, as long as it's only used for a lookup
// and call, this is irrelevant, and we'd like to fold such functions.
//
// * be smarter about bitcasts.
//
// In order to fold functions, we will sometimes add either bitcast instructions
// or bitcast constant expressions. Unfortunately, this can confound further
// analysis since the two functions differ where one has a bitcast and the
// other doesn't. We should learn to look through bitcasts.
//
// * Compare complex types with pointer types inside.
// * Compare cross-reference cases.
// * Compare complex expressions.
//
// All the three issues above could be described as ability to prove that
// fA == fB == fC == fE == fF == fG in example below:
//
//  void fA() {
//    fB();
//  }
//  void fB() {
//    fA();
//  }
//
//  void fE() {
//    fF();
//  }
//  void fF() {
//    fG();
//  }
//  void fG() {
//    fE();
//  }
//
// Simplest cross-reference case (fA <--> fB) was implemented in previous
// versions of MergeFunctions, though it presented only in two function pairs
// in test-suite (that counts >50k functions)
// Though possibility to detect complex cross-referencing (e.g.: A->B->C->D->A)
// could cover much more cases.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/MergeFunctions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/StructuralHash.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/FunctionComparator.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "mergefunc"

STATISTIC(NumFunctionsMerged, "Number of functions merged");
STATISTIC(NumThunksWritten, "Number of thunks generated");
STATISTIC(NumAliasesWritten, "Number of aliases generated");
STATISTIC(NumDoubleWeak, "Number of new functions created");

static cl::opt<unsigned> NumFunctionsForVerificationCheck(
    "mergefunc-verify",
    cl::desc("How many functions in a module could be used for "
             "MergeFunctions to pass a basic correctness check. "
             "'0' disables this check. Works only with '-debug' key."),
    cl::init(0), cl::Hidden);

// Under option -mergefunc-preserve-debug-info we:
// - Do not create a new function for a thunk.
// - Retain the debug info for a thunk's parameters (and associated
//   instructions for the debug info) from the entry block.
//   Note: -debug will display the algorithm at work.
// - Create debug-info for the call (to the shared implementation) made by
//   a thunk and its return value.
// - Erase the rest of the function, retaining the (minimally sized) entry
//   block to create a thunk.
// - Preserve a thunk's call site to point to the thunk even when both occur
//   within the same translation unit, to aid debugability. Note that this
//   behaviour differs from the underlying -mergefunc implementation which
//   modifies the thunk's call site to point to the shared implementation
//   when both occur within the same translation unit.
static cl::opt<bool>
    MergeFunctionsPDI("mergefunc-preserve-debug-info", cl::Hidden,
                      cl::init(false),
                      cl::desc("Preserve debug info in thunk when mergefunc "
                               "transformations are made."));

static cl::opt<bool>
    MergeFunctionsAliases("mergefunc-use-aliases", cl::Hidden,
                          cl::init(false),
                          cl::desc("Allow mergefunc to create aliases"));

namespace {

class FunctionNode {
  mutable AssertingVH<Function> F;
  IRHash Hash;

public:
  // Note the hash is recalculated potentially multiple times, but it is cheap.
  FunctionNode(Function *F) : F(F), Hash(StructuralHash(*F)) {}

  Function *getFunc() const { return F; }
  IRHash getHash() const { return Hash; }

  /// Replace the reference to the function F by the function G, assuming their
  /// implementations are equal.
  void replaceBy(Function *G) const {
    F = G;
  }
};

/// MergeFunctions finds functions which will generate identical machine code,
/// by considering all pointer types to be equivalent. Once identified,
/// MergeFunctions will fold them by replacing a call to one to a call to a
/// bitcast of the other.
class MergeFunctions {
public:
  MergeFunctions() : FnTree(FunctionNodeCmp(&GlobalNumbers)) {
  }

  bool runOnModule(Module &M);

private:
  // The function comparison operator is provided here so that FunctionNodes do
  // not need to become larger with another pointer.
  class FunctionNodeCmp {
    GlobalNumberState* GlobalNumbers;

  public:
    FunctionNodeCmp(GlobalNumberState* GN) : GlobalNumbers(GN) {}

    bool operator()(const FunctionNode &LHS, const FunctionNode &RHS) const {
      // Order first by hashes, then full function comparison.
      if (LHS.getHash() != RHS.getHash())
        return LHS.getHash() < RHS.getHash();
      FunctionComparator FCmp(LHS.getFunc(), RHS.getFunc(), GlobalNumbers);
      return FCmp.compare() < 0;
    }
  };
  using FnTreeType = std::set<FunctionNode, FunctionNodeCmp>;

  GlobalNumberState GlobalNumbers;

  /// A work queue of functions that may have been modified and should be
  /// analyzed again.
  std::vector<WeakTrackingVH> Deferred;

  /// Set of values marked as used in llvm.used and llvm.compiler.used.
  SmallPtrSet<GlobalValue *, 4> Used;

#ifndef NDEBUG
  /// Checks the rules of order relation introduced among functions set.
  /// Returns true, if check has been passed, and false if failed.
  bool doFunctionalCheck(std::vector<WeakTrackingVH> &Worklist);
#endif

  /// Insert a ComparableFunction into the FnTree, or merge it away if it's
  /// equal to one that's already present.
  bool insert(Function *NewFunction);

  /// Remove a Function from the FnTree and queue it up for a second sweep of
  /// analysis.
  void remove(Function *F);

  /// Find the functions that use this Value and remove them from FnTree and
  /// queue the functions.
  void removeUsers(Value *V);

  /// Replace all direct calls of Old with calls of New. Will bitcast New if
  /// necessary to make types match.
  void replaceDirectCallers(Function *Old, Function *New);

  /// Merge two equivalent functions. Upon completion, G may be deleted, or may
  /// be converted into a thunk. In either case, it should never be visited
  /// again.
  void mergeTwoFunctions(Function *F, Function *G);

  /// Fill PDIUnrelatedWL with instructions from the entry block that are
  /// unrelated to parameter related debug info.
  /// \param PDVRUnrelatedWL The equivalent non-intrinsic debug records.
  void
  filterInstsUnrelatedToPDI(BasicBlock *GEntryBlock,
                            std::vector<Instruction *> &PDIUnrelatedWL,
                            std::vector<DbgVariableRecord *> &PDVRUnrelatedWL);

  /// Erase the rest of the CFG (i.e. barring the entry block).
  void eraseTail(Function *G);

  /// Erase the instructions in PDIUnrelatedWL as they are unrelated to the
  /// parameter debug info, from the entry block.
  /// \param PDVRUnrelatedWL contains the equivalent set of non-instruction
  /// debug-info records.
  void
  eraseInstsUnrelatedToPDI(std::vector<Instruction *> &PDIUnrelatedWL,
                           std::vector<DbgVariableRecord *> &PDVRUnrelatedWL);

  /// Replace G with a simple tail call to bitcast(F). Also (unless
  /// MergeFunctionsPDI holds) replace direct uses of G with bitcast(F),
  /// delete G.
  void writeThunk(Function *F, Function *G);

  // Replace G with an alias to F (deleting function G)
  void writeAlias(Function *F, Function *G);

  // Replace G with an alias to F if possible, or a thunk to F if possible.
  // Returns false if neither is the case.
  bool writeThunkOrAlias(Function *F, Function *G);

  /// Replace function F with function G in the function tree.
  void replaceFunctionInTree(const FunctionNode &FN, Function *G);

  /// The set of all distinct functions. Use the insert() and remove() methods
  /// to modify it. The map allows efficient lookup and deferring of Functions.
  FnTreeType FnTree;

  // Map functions to the iterators of the FunctionNode which contains them
  // in the FnTree. This must be updated carefully whenever the FnTree is
  // modified, i.e. in insert(), remove(), and replaceFunctionInTree(), to avoid
  // dangling iterators into FnTree. The invariant that preserves this is that
  // there is exactly one mapping F -> FN for each FunctionNode FN in FnTree.
  DenseMap<AssertingVH<Function>, FnTreeType::iterator> FNodesInTree;
};
} // end anonymous namespace

PreservedAnalyses MergeFunctionsPass::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  MergeFunctions MF;
  if (!MF.runOnModule(M))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}

#ifndef NDEBUG
bool MergeFunctions::doFunctionalCheck(std::vector<WeakTrackingVH> &Worklist) {
  if (const unsigned Max = NumFunctionsForVerificationCheck) {
    unsigned TripleNumber = 0;
    bool Valid = true;

    dbgs() << "MERGEFUNC-VERIFY: Started for first " << Max << " functions.\n";

    unsigned i = 0;
    for (std::vector<WeakTrackingVH>::iterator I = Worklist.begin(),
                                               E = Worklist.end();
         I != E && i < Max; ++I, ++i) {
      unsigned j = i;
      for (std::vector<WeakTrackingVH>::iterator J = I; J != E && j < Max;
           ++J, ++j) {
        Function *F1 = cast<Function>(*I);
        Function *F2 = cast<Function>(*J);
        int Res1 = FunctionComparator(F1, F2, &GlobalNumbers).compare();
        int Res2 = FunctionComparator(F2, F1, &GlobalNumbers).compare();

        // If F1 <= F2, then F2 >= F1, otherwise report failure.
        if (Res1 != -Res2) {
          dbgs() << "MERGEFUNC-VERIFY: Non-symmetric; triple: " << TripleNumber
                 << "\n";
          dbgs() << *F1 << '\n' << *F2 << '\n';
          Valid = false;
        }

        if (Res1 == 0)
          continue;

        unsigned k = j;
        for (std::vector<WeakTrackingVH>::iterator K = J; K != E && k < Max;
             ++k, ++K, ++TripleNumber) {
          if (K == J)
            continue;

          Function *F3 = cast<Function>(*K);
          int Res3 = FunctionComparator(F1, F3, &GlobalNumbers).compare();
          int Res4 = FunctionComparator(F2, F3, &GlobalNumbers).compare();

          bool Transitive = true;

          if (Res1 != 0 && Res1 == Res4) {
            // F1 > F2, F2 > F3 => F1 > F3
            Transitive = Res3 == Res1;
          } else if (Res3 != 0 && Res3 == -Res4) {
            // F1 > F3, F3 > F2 => F1 > F2
            Transitive = Res3 == Res1;
          } else if (Res4 != 0 && -Res3 == Res4) {
            // F2 > F3, F3 > F1 => F2 > F1
            Transitive = Res4 == -Res1;
          }

          if (!Transitive) {
            dbgs() << "MERGEFUNC-VERIFY: Non-transitive; triple: "
                   << TripleNumber << "\n";
            dbgs() << "Res1, Res3, Res4: " << Res1 << ", " << Res3 << ", "
                   << Res4 << "\n";
            dbgs() << *F1 << '\n' << *F2 << '\n' << *F3 << '\n';
            Valid = false;
          }
        }
      }
    }

    dbgs() << "MERGEFUNC-VERIFY: " << (Valid ? "Passed." : "Failed.") << "\n";
    return Valid;
  }
  return true;
}
#endif

/// Check whether \p F has an intrinsic which references
/// distinct metadata as an operand. The most common
/// instance of this would be CFI checks for function-local types.
static bool hasDistinctMetadataIntrinsic(const Function &F) {
  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB.instructionsWithoutDebug()) {
      if (!isa<IntrinsicInst>(&I))
        continue;

      for (Value *Op : I.operands()) {
        auto *MDL = dyn_cast<MetadataAsValue>(Op);
        if (!MDL)
          continue;
        if (MDNode *N = dyn_cast<MDNode>(MDL->getMetadata()))
          if (N->isDistinct())
            return true;
      }
    }
  }
  return false;
}

/// Check whether \p F is eligible for function merging.
static bool isEligibleForMerging(Function &F) {
  return !F.isDeclaration() && !F.hasAvailableExternallyLinkage() &&
         !hasDistinctMetadataIntrinsic(F);
}

bool MergeFunctions::runOnModule(Module &M) {
  bool Changed = false;

  SmallVector<GlobalValue *, 4> UsedV;
  collectUsedGlobalVariables(M, UsedV, /*CompilerUsed=*/false);
  collectUsedGlobalVariables(M, UsedV, /*CompilerUsed=*/true);
  Used.insert(UsedV.begin(), UsedV.end());

  // All functions in the module, ordered by hash. Functions with a unique
  // hash value are easily eliminated.
  std::vector<std::pair<IRHash, Function *>> HashedFuncs;
  for (Function &Func : M) {
    if (isEligibleForMerging(Func)) {
      HashedFuncs.push_back({StructuralHash(Func), &Func});
    }
  }

  llvm::stable_sort(HashedFuncs, less_first());

  auto S = HashedFuncs.begin();
  for (auto I = HashedFuncs.begin(), IE = HashedFuncs.end(); I != IE; ++I) {
    // If the hash value matches the previous value or the next one, we must
    // consider merging it. Otherwise it is dropped and never considered again.
    if ((I != S && std::prev(I)->first == I->first) ||
        (std::next(I) != IE && std::next(I)->first == I->first) ) {
      Deferred.push_back(WeakTrackingVH(I->second));
    }
  }

  do {
    std::vector<WeakTrackingVH> Worklist;
    Deferred.swap(Worklist);

    LLVM_DEBUG(doFunctionalCheck(Worklist));

    LLVM_DEBUG(dbgs() << "size of module: " << M.size() << '\n');
    LLVM_DEBUG(dbgs() << "size of worklist: " << Worklist.size() << '\n');

    // Insert functions and merge them.
    for (WeakTrackingVH &I : Worklist) {
      if (!I)
        continue;
      Function *F = cast<Function>(I);
      if (!F->isDeclaration() && !F->hasAvailableExternallyLinkage()) {
        Changed |= insert(F);
      }
    }
    LLVM_DEBUG(dbgs() << "size of FnTree: " << FnTree.size() << '\n');
  } while (!Deferred.empty());

  FnTree.clear();
  FNodesInTree.clear();
  GlobalNumbers.clear();
  Used.clear();

  return Changed;
}

// Replace direct callers of Old with New.
void MergeFunctions::replaceDirectCallers(Function *Old, Function *New) {
  for (Use &U : llvm::make_early_inc_range(Old->uses())) {
    CallBase *CB = dyn_cast<CallBase>(U.getUser());
    if (CB && CB->isCallee(&U)) {
      // Do not copy attributes from the called function to the call-site.
      // Function comparison ensures that the attributes are the same up to
      // type congruences in byval(), in which case we need to keep the byval
      // type of the call-site, not the callee function.
      remove(CB->getFunction());
      U.set(New);
    }
  }
}

// Helper for writeThunk,
// Selects proper bitcast operation,
// but a bit simpler then CastInst::getCastOpcode.
static Value *createCast(IRBuilder<> &Builder, Value *V, Type *DestTy) {
  Type *SrcTy = V->getType();
  if (SrcTy->isStructTy()) {
    assert(DestTy->isStructTy());
    assert(SrcTy->getStructNumElements() == DestTy->getStructNumElements());
    Value *Result = PoisonValue::get(DestTy);
    for (unsigned int I = 0, E = SrcTy->getStructNumElements(); I < E; ++I) {
      Value *Element =
          createCast(Builder, Builder.CreateExtractValue(V, ArrayRef(I)),
                     DestTy->getStructElementType(I));

      Result = Builder.CreateInsertValue(Result, Element, ArrayRef(I));
    }
    return Result;
  }
  assert(!DestTy->isStructTy());
  if (SrcTy->isIntegerTy() && DestTy->isPointerTy())
    return Builder.CreateIntToPtr(V, DestTy);
  else if (SrcTy->isPointerTy() && DestTy->isIntegerTy())
    return Builder.CreatePtrToInt(V, DestTy);
  else
    return Builder.CreateBitCast(V, DestTy);
}

// Erase the instructions in PDIUnrelatedWL as they are unrelated to the
// parameter debug info, from the entry block.
void MergeFunctions::eraseInstsUnrelatedToPDI(
    std::vector<Instruction *> &PDIUnrelatedWL,
    std::vector<DbgVariableRecord *> &PDVRUnrelatedWL) {
  LLVM_DEBUG(
      dbgs() << " Erasing instructions (in reverse order of appearance in "
                "entry block) unrelated to parameter debug info from entry "
                "block: {\n");
  while (!PDIUnrelatedWL.empty()) {
    Instruction *I = PDIUnrelatedWL.back();
    LLVM_DEBUG(dbgs() << "  Deleting Instruction: ");
    LLVM_DEBUG(I->print(dbgs()));
    LLVM_DEBUG(dbgs() << "\n");
    I->eraseFromParent();
    PDIUnrelatedWL.pop_back();
  }

  while (!PDVRUnrelatedWL.empty()) {
    DbgVariableRecord *DVR = PDVRUnrelatedWL.back();
    LLVM_DEBUG(dbgs() << "  Deleting DbgVariableRecord ");
    LLVM_DEBUG(DVR->print(dbgs()));
    LLVM_DEBUG(dbgs() << "\n");
    DVR->eraseFromParent();
    PDVRUnrelatedWL.pop_back();
  }

  LLVM_DEBUG(dbgs() << " } // Done erasing instructions unrelated to parameter "
                       "debug info from entry block. \n");
}

// Reduce G to its entry block.
void MergeFunctions::eraseTail(Function *G) {
  std::vector<BasicBlock *> WorklistBB;
  for (BasicBlock &BB : drop_begin(*G)) {
    BB.dropAllReferences();
    WorklistBB.push_back(&BB);
  }
  while (!WorklistBB.empty()) {
    BasicBlock *BB = WorklistBB.back();
    BB->eraseFromParent();
    WorklistBB.pop_back();
  }
}

// We are interested in the following instructions from the entry block as being
// related to parameter debug info:
// - @llvm.dbg.declare
// - stores from the incoming parameters to locations on the stack-frame
// - allocas that create these locations on the stack-frame
// - @llvm.dbg.value
// - the entry block's terminator
// The rest are unrelated to debug info for the parameters; fill up
// PDIUnrelatedWL with such instructions.
void MergeFunctions::filterInstsUnrelatedToPDI(
    BasicBlock *GEntryBlock, std::vector<Instruction *> &PDIUnrelatedWL,
    std::vector<DbgVariableRecord *> &PDVRUnrelatedWL) {
  std::set<Instruction *> PDIRelated;
  std::set<DbgVariableRecord *> PDVRRelated;

  // Work out whether a dbg.value intrinsic or an equivalent DbgVariableRecord
  // is a parameter to be preserved.
  auto ExamineDbgValue = [](auto *DbgVal, auto &Container) {
    LLVM_DEBUG(dbgs() << " Deciding: ");
    LLVM_DEBUG(DbgVal->print(dbgs()));
    LLVM_DEBUG(dbgs() << "\n");
    DILocalVariable *DILocVar = DbgVal->getVariable();
    if (DILocVar->isParameter()) {
      LLVM_DEBUG(dbgs() << "  Include (parameter): ");
      LLVM_DEBUG(DbgVal->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
      Container.insert(DbgVal);
    } else {
      LLVM_DEBUG(dbgs() << "  Delete (!parameter): ");
      LLVM_DEBUG(DbgVal->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
    }
  };

  auto ExamineDbgDeclare = [&PDIRelated](auto *DbgDecl, auto &Container) {
    LLVM_DEBUG(dbgs() << " Deciding: ");
    LLVM_DEBUG(DbgDecl->print(dbgs()));
    LLVM_DEBUG(dbgs() << "\n");
    DILocalVariable *DILocVar = DbgDecl->getVariable();
    if (DILocVar->isParameter()) {
      LLVM_DEBUG(dbgs() << "  Parameter: ");
      LLVM_DEBUG(DILocVar->print(dbgs()));
      AllocaInst *AI = dyn_cast_or_null<AllocaInst>(DbgDecl->getAddress());
      if (AI) {
        LLVM_DEBUG(dbgs() << "  Processing alloca users: ");
        LLVM_DEBUG(dbgs() << "\n");
        for (User *U : AI->users()) {
          if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
            if (Value *Arg = SI->getValueOperand()) {
              if (isa<Argument>(Arg)) {
                LLVM_DEBUG(dbgs() << "  Include: ");
                LLVM_DEBUG(AI->print(dbgs()));
                LLVM_DEBUG(dbgs() << "\n");
                PDIRelated.insert(AI);
                LLVM_DEBUG(dbgs() << "   Include (parameter): ");
                LLVM_DEBUG(SI->print(dbgs()));
                LLVM_DEBUG(dbgs() << "\n");
                PDIRelated.insert(SI);
                LLVM_DEBUG(dbgs() << "  Include: ");
                LLVM_DEBUG(DbgDecl->print(dbgs()));
                LLVM_DEBUG(dbgs() << "\n");
                Container.insert(DbgDecl);
              } else {
                LLVM_DEBUG(dbgs() << "   Delete (!parameter): ");
                LLVM_DEBUG(SI->print(dbgs()));
                LLVM_DEBUG(dbgs() << "\n");
              }
            }
          } else {
            LLVM_DEBUG(dbgs() << "   Defer: ");
            LLVM_DEBUG(U->print(dbgs()));
            LLVM_DEBUG(dbgs() << "\n");
          }
        }
      } else {
        LLVM_DEBUG(dbgs() << "  Delete (alloca NULL): ");
        LLVM_DEBUG(DbgDecl->print(dbgs()));
        LLVM_DEBUG(dbgs() << "\n");
      }
    } else {
      LLVM_DEBUG(dbgs() << "  Delete (!parameter): ");
      LLVM_DEBUG(DbgDecl->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
    }
  };

  for (BasicBlock::iterator BI = GEntryBlock->begin(), BIE = GEntryBlock->end();
       BI != BIE; ++BI) {
    // Examine DbgVariableRecords as they happen "before" the instruction. Are
    // they connected to parameters?
    for (DbgVariableRecord &DVR : filterDbgVars(BI->getDbgRecordRange())) {
      if (DVR.isDbgValue() || DVR.isDbgAssign()) {
        ExamineDbgValue(&DVR, PDVRRelated);
      } else {
        assert(DVR.isDbgDeclare());
        ExamineDbgDeclare(&DVR, PDVRRelated);
      }
    }

    if (auto *DVI = dyn_cast<DbgValueInst>(&*BI)) {
      ExamineDbgValue(DVI, PDIRelated);
    } else if (auto *DDI = dyn_cast<DbgDeclareInst>(&*BI)) {
      ExamineDbgDeclare(DDI, PDIRelated);
    } else if (BI->isTerminator() && &*BI == GEntryBlock->getTerminator()) {
      LLVM_DEBUG(dbgs() << " Will Include Terminator: ");
      LLVM_DEBUG(BI->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
      PDIRelated.insert(&*BI);
    } else {
      LLVM_DEBUG(dbgs() << " Defer: ");
      LLVM_DEBUG(BI->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
    }
  }
  LLVM_DEBUG(
      dbgs()
      << " Report parameter debug info related/related instructions: {\n");

  auto IsPDIRelated = [](auto *Rec, auto &Container, auto &UnrelatedCont) {
    if (Container.find(Rec) == Container.end()) {
      LLVM_DEBUG(dbgs() << "  !PDIRelated: ");
      LLVM_DEBUG(Rec->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
      UnrelatedCont.push_back(Rec);
    } else {
      LLVM_DEBUG(dbgs() << "   PDIRelated: ");
      LLVM_DEBUG(Rec->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
    }
  };

  // Collect the set of unrelated instructions and debug records.
  for (Instruction &I : *GEntryBlock) {
    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
      IsPDIRelated(&DVR, PDVRRelated, PDVRUnrelatedWL);
    IsPDIRelated(&I, PDIRelated, PDIUnrelatedWL);
  }
  LLVM_DEBUG(dbgs() << " }\n");
}

/// Whether this function may be replaced by a forwarding thunk.
static bool canCreateThunkFor(Function *F) {
  if (F->isVarArg())
    return false;

  // Don't merge tiny functions using a thunk, since it can just end up
  // making the function larger.
  if (F->size() == 1) {
    if (F->front().sizeWithoutDebug() < 2) {
      LLVM_DEBUG(dbgs() << "canCreateThunkFor: " << F->getName()
                        << " is too small to bother creating a thunk for\n");
      return false;
    }
  }
  return true;
}

/// Copy all metadata of a specific kind from one function to another.
static void copyMetadataIfPresent(Function *From, Function *To,
                                  StringRef Kind) {
  SmallVector<MDNode *, 4> MDs;
  From->getMetadata(Kind, MDs);
  for (MDNode *MD : MDs)
    To->addMetadata(Kind, *MD);
}

// Replace G with a simple tail call to bitcast(F). Also (unless
// MergeFunctionsPDI holds) replace direct uses of G with bitcast(F),
// delete G. Under MergeFunctionsPDI, we use G itself for creating
// the thunk as we preserve the debug info (and associated instructions)
// from G's entry block pertaining to G's incoming arguments which are
// passed on as corresponding arguments in the call that G makes to F.
// For better debugability, under MergeFunctionsPDI, we do not modify G's
// call sites to point to F even when within the same translation unit.
void MergeFunctions::writeThunk(Function *F, Function *G) {
  BasicBlock *GEntryBlock = nullptr;
  std::vector<Instruction *> PDIUnrelatedWL;
  std::vector<DbgVariableRecord *> PDVRUnrelatedWL;
  BasicBlock *BB = nullptr;
  Function *NewG = nullptr;
  if (MergeFunctionsPDI) {
    LLVM_DEBUG(dbgs() << "writeThunk: (MergeFunctionsPDI) Do not create a new "
                         "function as thunk; retain original: "
                      << G->getName() << "()\n");
    GEntryBlock = &G->getEntryBlock();
    LLVM_DEBUG(
        dbgs() << "writeThunk: (MergeFunctionsPDI) filter parameter related "
                  "debug info for "
               << G->getName() << "() {\n");
    filterInstsUnrelatedToPDI(GEntryBlock, PDIUnrelatedWL, PDVRUnrelatedWL);
    GEntryBlock->getTerminator()->eraseFromParent();
    BB = GEntryBlock;
  } else {
    NewG = Function::Create(G->getFunctionType(), G->getLinkage(),
                            G->getAddressSpace(), "", G->getParent());
    NewG->setComdat(G->getComdat());
    NewG->IsNewDbgInfoFormat = G->IsNewDbgInfoFormat;
    BB = BasicBlock::Create(F->getContext(), "", NewG);
  }

  IRBuilder<> Builder(BB);
  Function *H = MergeFunctionsPDI ? G : NewG;
  SmallVector<Value *, 16> Args;
  unsigned i = 0;
  FunctionType *FFTy = F->getFunctionType();
  for (Argument &AI : H->args()) {
    Args.push_back(createCast(Builder, &AI, FFTy->getParamType(i)));
    ++i;
  }

  CallInst *CI = Builder.CreateCall(F, Args);
  ReturnInst *RI = nullptr;
  bool isSwiftTailCall = F->getCallingConv() == CallingConv::SwiftTail &&
                         G->getCallingConv() == CallingConv::SwiftTail;
  CI->setTailCallKind(isSwiftTailCall ? llvm::CallInst::TCK_MustTail
                                      : llvm::CallInst::TCK_Tail);
  CI->setCallingConv(F->getCallingConv());
  CI->setAttributes(F->getAttributes());
  if (H->getReturnType()->isVoidTy()) {
    RI = Builder.CreateRetVoid();
  } else {
    RI = Builder.CreateRet(createCast(Builder, CI, H->getReturnType()));
  }

  if (MergeFunctionsPDI) {
    DISubprogram *DIS = G->getSubprogram();
    if (DIS) {
      DebugLoc CIDbgLoc =
          DILocation::get(DIS->getContext(), DIS->getScopeLine(), 0, DIS);
      DebugLoc RIDbgLoc =
          DILocation::get(DIS->getContext(), DIS->getScopeLine(), 0, DIS);
      CI->setDebugLoc(CIDbgLoc);
      RI->setDebugLoc(RIDbgLoc);
    } else {
      LLVM_DEBUG(
          dbgs() << "writeThunk: (MergeFunctionsPDI) No DISubprogram for "
                 << G->getName() << "()\n");
    }
    eraseTail(G);
    eraseInstsUnrelatedToPDI(PDIUnrelatedWL, PDVRUnrelatedWL);
    LLVM_DEBUG(
        dbgs() << "} // End of parameter related debug info filtering for: "
               << G->getName() << "()\n");
  } else {
    NewG->copyAttributesFrom(G);
    NewG->takeName(G);
    // Ensure CFI type metadata is propagated to the new function.
    copyMetadataIfPresent(G, NewG, "type");
    copyMetadataIfPresent(G, NewG, "kcfi_type");
    removeUsers(G);
    G->replaceAllUsesWith(NewG);
    G->eraseFromParent();
  }

  LLVM_DEBUG(dbgs() << "writeThunk: " << H->getName() << '\n');
  ++NumThunksWritten;
}

// Whether this function may be replaced by an alias
static bool canCreateAliasFor(Function *F) {
  if (!MergeFunctionsAliases || !F->hasGlobalUnnamedAddr())
    return false;

  // We should only see linkages supported by aliases here
  assert(F->hasLocalLinkage() || F->hasExternalLinkage()
      || F->hasWeakLinkage() || F->hasLinkOnceLinkage());
  return true;
}

// Replace G with an alias to F (deleting function G)
void MergeFunctions::writeAlias(Function *F, Function *G) {
  PointerType *PtrType = G->getType();
  auto *GA = GlobalAlias::create(G->getValueType(), PtrType->getAddressSpace(),
                                 G->getLinkage(), "", F, G->getParent());

  const MaybeAlign FAlign = F->getAlign();
  const MaybeAlign GAlign = G->getAlign();
  if (FAlign || GAlign)
    F->setAlignment(std::max(FAlign.valueOrOne(), GAlign.valueOrOne()));
  else
    F->setAlignment(std::nullopt);
  GA->takeName(G);
  GA->setVisibility(G->getVisibility());
  GA->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  removeUsers(G);
  G->replaceAllUsesWith(GA);
  G->eraseFromParent();

  LLVM_DEBUG(dbgs() << "writeAlias: " << GA->getName() << '\n');
  ++NumAliasesWritten;
}

// Replace G with an alias to F if possible, or a thunk to F if
// profitable. Returns false if neither is the case.
bool MergeFunctions::writeThunkOrAlias(Function *F, Function *G) {
  if (canCreateAliasFor(G)) {
    writeAlias(F, G);
    return true;
  }
  if (canCreateThunkFor(F)) {
    writeThunk(F, G);
    return true;
  }
  return false;
}

// Merge two equivalent functions. Upon completion, Function G is deleted.
void MergeFunctions::mergeTwoFunctions(Function *F, Function *G) {
  if (F->isInterposable()) {
    assert(G->isInterposable());

    // Both writeThunkOrAlias() calls below must succeed, either because we can
    // create aliases for G and NewF, or because a thunk for F is profitable.
    // F here has the same signature as NewF below, so that's what we check.
    if (!canCreateThunkFor(F) &&
        (!canCreateAliasFor(F) || !canCreateAliasFor(G)))
      return;

    // Make them both thunks to the same internal function.
    Function *NewF = Function::Create(F->getFunctionType(), F->getLinkage(),
                                      F->getAddressSpace(), "", F->getParent());
    NewF->copyAttributesFrom(F);
    NewF->takeName(F);
    NewF->IsNewDbgInfoFormat = F->IsNewDbgInfoFormat;
    // Ensure CFI type metadata is propagated to the new function.
    copyMetadataIfPresent(F, NewF, "type");
    copyMetadataIfPresent(F, NewF, "kcfi_type");
    removeUsers(F);
    F->replaceAllUsesWith(NewF);

    // We collect alignment before writeThunkOrAlias that overwrites NewF and
    // G's content.
    const MaybeAlign NewFAlign = NewF->getAlign();
    const MaybeAlign GAlign = G->getAlign();

    writeThunkOrAlias(F, G);
    writeThunkOrAlias(F, NewF);

    if (NewFAlign || GAlign)
      F->setAlignment(std::max(NewFAlign.valueOrOne(), GAlign.valueOrOne()));
    else
      F->setAlignment(std::nullopt);
    F->setLinkage(GlobalValue::PrivateLinkage);
    ++NumDoubleWeak;
    ++NumFunctionsMerged;
  } else {
    // For better debugability, under MergeFunctionsPDI, we do not modify G's
    // call sites to point to F even when within the same translation unit.
    if (!G->isInterposable() && !MergeFunctionsPDI) {
      // Functions referred to by llvm.used/llvm.compiler.used are special:
      // there are uses of the symbol name that are not visible to LLVM,
      // usually from inline asm.
      if (G->hasGlobalUnnamedAddr() && !Used.contains(G)) {
        // G might have been a key in our GlobalNumberState, and it's illegal
        // to replace a key in ValueMap<GlobalValue *> with a non-global.
        GlobalNumbers.erase(G);
        // If G's address is not significant, replace it entirely.
        removeUsers(G);
        G->replaceAllUsesWith(F);
      } else {
        // Redirect direct callers of G to F. (See note on MergeFunctionsPDI
        // above).
        replaceDirectCallers(G, F);
      }
    }

    // If G was internal then we may have replaced all uses of G with F. If so,
    // stop here and delete G. There's no need for a thunk. (See note on
    // MergeFunctionsPDI above).
    if (G->isDiscardableIfUnused() && G->use_empty() && !MergeFunctionsPDI) {
      G->eraseFromParent();
      ++NumFunctionsMerged;
      return;
    }

    if (writeThunkOrAlias(F, G)) {
      ++NumFunctionsMerged;
    }
  }
}

/// Replace function F by function G.
void MergeFunctions::replaceFunctionInTree(const FunctionNode &FN,
                                           Function *G) {
  Function *F = FN.getFunc();
  assert(FunctionComparator(F, G, &GlobalNumbers).compare() == 0 &&
         "The two functions must be equal");

  auto I = FNodesInTree.find(F);
  assert(I != FNodesInTree.end() && "F should be in FNodesInTree");
  assert(FNodesInTree.count(G) == 0 && "FNodesInTree should not contain G");

  FnTreeType::iterator IterToFNInFnTree = I->second;
  assert(&(*IterToFNInFnTree) == &FN && "F should map to FN in FNodesInTree.");
  // Remove F -> FN and insert G -> FN
  FNodesInTree.erase(I);
  FNodesInTree.insert({G, IterToFNInFnTree});
  // Replace F with G in FN, which is stored inside the FnTree.
  FN.replaceBy(G);
}

// Ordering for functions that are equal under FunctionComparator
static bool isFuncOrderCorrect(const Function *F, const Function *G) {
  if (F->isInterposable() != G->isInterposable()) {
    // Strong before weak, because the weak function may call the strong
    // one, but not the other way around.
    return !F->isInterposable();
  }
  if (F->hasLocalLinkage() != G->hasLocalLinkage()) {
    // External before local, because we definitely have to keep the external
    // function, but may be able to drop the local one.
    return !F->hasLocalLinkage();
  }
  // Impose a total order (by name) on the replacement of functions. This is
  // important when operating on more than one module independently to prevent
  // cycles of thunks calling each other when the modules are linked together.
  return F->getName() <= G->getName();
}

// Insert a ComparableFunction into the FnTree, or merge it away if equal to one
// that was already inserted.
bool MergeFunctions::insert(Function *NewFunction) {
  std::pair<FnTreeType::iterator, bool> Result =
      FnTree.insert(FunctionNode(NewFunction));

  if (Result.second) {
    assert(FNodesInTree.count(NewFunction) == 0);
    FNodesInTree.insert({NewFunction, Result.first});
    LLVM_DEBUG(dbgs() << "Inserting as unique: " << NewFunction->getName()
                      << '\n');
    return false;
  }

  const FunctionNode &OldF = *Result.first;

  if (!isFuncOrderCorrect(OldF.getFunc(), NewFunction)) {
    // Swap the two functions.
    Function *F = OldF.getFunc();
    replaceFunctionInTree(*Result.first, NewFunction);
    NewFunction = F;
    assert(OldF.getFunc() != F && "Must have swapped the functions.");
  }

  LLVM_DEBUG(dbgs() << "  " << OldF.getFunc()->getName()
                    << " == " << NewFunction->getName() << '\n');

  Function *DeleteF = NewFunction;
  mergeTwoFunctions(OldF.getFunc(), DeleteF);
  return true;
}

// Remove a function from FnTree. If it was already in FnTree, add
// it to Deferred so that we'll look at it in the next round.
void MergeFunctions::remove(Function *F) {
  auto I = FNodesInTree.find(F);
  if (I != FNodesInTree.end()) {
    LLVM_DEBUG(dbgs() << "Deferred " << F->getName() << ".\n");
    FnTree.erase(I->second);
    // I->second has been invalidated, remove it from the FNodesInTree map to
    // preserve the invariant.
    FNodesInTree.erase(I);
    Deferred.emplace_back(F);
  }
}

// For each instruction used by the value, remove() the function that contains
// the instruction. This should happen right before a call to RAUW.
void MergeFunctions::removeUsers(Value *V) {
  for (User *U : V->users())
    if (auto *I = dyn_cast<Instruction>(U))
      remove(I->getFunction());
}
