//===---- Delinearization.cpp - MultiDimensional Index Delinearization ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements an analysis pass that tries to delinearize all GEP
// instructions in all loops using the SCEV analysis functionality. This pass is
// only used for testing purposes: if your pass needs delinearization, please
// use the on-demand SCEVAddRecExpr::delinearize() function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Delinearization.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionDivision.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DL_NAME "delinearize"
#define DEBUG_TYPE DL_NAME

// Return true when S contains at least an undef value.
static inline bool containsUndefs(const SCEV *S) {
  return SCEVExprContains(S, [](const SCEV *S) {
    if (const auto *SU = dyn_cast<SCEVUnknown>(S))
      return isa<UndefValue>(SU->getValue());
    return false;
  });
}

namespace {

// Collect all steps of SCEV expressions.
struct SCEVCollectStrides {
  ScalarEvolution &SE;
  SmallVectorImpl<const SCEV *> &Strides;

  SCEVCollectStrides(ScalarEvolution &SE, SmallVectorImpl<const SCEV *> &S)
      : SE(SE), Strides(S) {}

  bool follow(const SCEV *S) {
    if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S))
      Strides.push_back(AR->getStepRecurrence(SE));
    return true;
  }

  bool isDone() const { return false; }
};

// Collect all SCEVUnknown and SCEVMulExpr expressions.
struct SCEVCollectTerms {
  SmallVectorImpl<const SCEV *> &Terms;

  SCEVCollectTerms(SmallVectorImpl<const SCEV *> &T) : Terms(T) {}

  bool follow(const SCEV *S) {
    if (isa<SCEVUnknown>(S) || isa<SCEVMulExpr>(S) ||
        isa<SCEVSignExtendExpr>(S)) {
      if (!containsUndefs(S))
        Terms.push_back(S);

      // Stop recursion: once we collected a term, do not walk its operands.
      return false;
    }

    // Keep looking.
    return true;
  }

  bool isDone() const { return false; }
};

// Check if a SCEV contains an AddRecExpr.
struct SCEVHasAddRec {
  bool &ContainsAddRec;

  SCEVHasAddRec(bool &ContainsAddRec) : ContainsAddRec(ContainsAddRec) {
    ContainsAddRec = false;
  }

  bool follow(const SCEV *S) {
    if (isa<SCEVAddRecExpr>(S)) {
      ContainsAddRec = true;

      // Stop recursion: once we collected a term, do not walk its operands.
      return false;
    }

    // Keep looking.
    return true;
  }

  bool isDone() const { return false; }
};

// Find factors that are multiplied with an expression that (possibly as a
// subexpression) contains an AddRecExpr. In the expression:
//
//  8 * (100 +  %p * %q * (%a + {0, +, 1}_loop))
//
// "%p * %q" are factors multiplied by the expression "(%a + {0, +, 1}_loop)"
// that contains the AddRec {0, +, 1}_loop. %p * %q are likely to be array size
// parameters as they form a product with an induction variable.
//
// This collector expects all array size parameters to be in the same MulExpr.
// It might be necessary to later add support for collecting parameters that are
// spread over different nested MulExpr.
struct SCEVCollectAddRecMultiplies {
  SmallVectorImpl<const SCEV *> &Terms;
  ScalarEvolution &SE;

  SCEVCollectAddRecMultiplies(SmallVectorImpl<const SCEV *> &T,
                              ScalarEvolution &SE)
      : Terms(T), SE(SE) {}

  bool follow(const SCEV *S) {
    if (auto *Mul = dyn_cast<SCEVMulExpr>(S)) {
      bool HasAddRec = false;
      SmallVector<const SCEV *, 0> Operands;
      for (const auto *Op : Mul->operands()) {
        const SCEVUnknown *Unknown = dyn_cast<SCEVUnknown>(Op);
        if (Unknown && !isa<CallInst>(Unknown->getValue())) {
          Operands.push_back(Op);
        } else if (Unknown) {
          HasAddRec = true;
        } else {
          bool ContainsAddRec = false;
          SCEVHasAddRec ContiansAddRec(ContainsAddRec);
          visitAll(Op, ContiansAddRec);
          HasAddRec |= ContainsAddRec;
        }
      }
      if (Operands.size() == 0)
        return true;

      if (!HasAddRec)
        return false;

      Terms.push_back(SE.getMulExpr(Operands));
      // Stop recursion: once we collected a term, do not walk its operands.
      return false;
    }

    // Keep looking.
    return true;
  }

  bool isDone() const { return false; }
};

} // end anonymous namespace

/// Find parametric terms in this SCEVAddRecExpr. We first for parameters in
/// two places:
///   1) The strides of AddRec expressions.
///   2) Unknowns that are multiplied with AddRec expressions.
void llvm::collectParametricTerms(ScalarEvolution &SE, const SCEV *Expr,
                                  SmallVectorImpl<const SCEV *> &Terms) {
  SmallVector<const SCEV *, 4> Strides;
  SCEVCollectStrides StrideCollector(SE, Strides);
  visitAll(Expr, StrideCollector);

  LLVM_DEBUG({
    dbgs() << "Strides:\n";
    for (const SCEV *S : Strides)
      dbgs() << *S << "\n";
  });

  for (const SCEV *S : Strides) {
    SCEVCollectTerms TermCollector(Terms);
    visitAll(S, TermCollector);
  }

  LLVM_DEBUG({
    dbgs() << "Terms:\n";
    for (const SCEV *T : Terms)
      dbgs() << *T << "\n";
  });

  SCEVCollectAddRecMultiplies MulCollector(Terms, SE);
  visitAll(Expr, MulCollector);
}

static bool findArrayDimensionsRec(ScalarEvolution &SE,
                                   SmallVectorImpl<const SCEV *> &Terms,
                                   SmallVectorImpl<const SCEV *> &Sizes) {
  int Last = Terms.size() - 1;
  const SCEV *Step = Terms[Last];

  // End of recursion.
  if (Last == 0) {
    if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(Step)) {
      SmallVector<const SCEV *, 2> Qs;
      for (const SCEV *Op : M->operands())
        if (!isa<SCEVConstant>(Op))
          Qs.push_back(Op);

      Step = SE.getMulExpr(Qs);
    }

    Sizes.push_back(Step);
    return true;
  }

  for (const SCEV *&Term : Terms) {
    // Normalize the terms before the next call to findArrayDimensionsRec.
    const SCEV *Q, *R;
    SCEVDivision::divide(SE, Term, Step, &Q, &R);

    // Bail out when GCD does not evenly divide one of the terms.
    if (!R->isZero())
      return false;

    Term = Q;
  }

  // Remove all SCEVConstants.
  erase_if(Terms, [](const SCEV *E) { return isa<SCEVConstant>(E); });

  if (Terms.size() > 0)
    if (!findArrayDimensionsRec(SE, Terms, Sizes))
      return false;

  Sizes.push_back(Step);
  return true;
}

// Returns true when one of the SCEVs of Terms contains a SCEVUnknown parameter.
static inline bool containsParameters(SmallVectorImpl<const SCEV *> &Terms) {
  for (const SCEV *T : Terms)
    if (SCEVExprContains(T, [](const SCEV *S) { return isa<SCEVUnknown>(S); }))
      return true;

  return false;
}

// Return the number of product terms in S.
static inline int numberOfTerms(const SCEV *S) {
  if (const SCEVMulExpr *Expr = dyn_cast<SCEVMulExpr>(S))
    return Expr->getNumOperands();
  return 1;
}

static const SCEV *removeConstantFactors(ScalarEvolution &SE, const SCEV *T) {
  if (isa<SCEVConstant>(T))
    return nullptr;

  if (isa<SCEVUnknown>(T))
    return T;

  if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(T)) {
    SmallVector<const SCEV *, 2> Factors;
    for (const SCEV *Op : M->operands())
      if (!isa<SCEVConstant>(Op))
        Factors.push_back(Op);

    return SE.getMulExpr(Factors);
  }

  return T;
}

void llvm::findArrayDimensions(ScalarEvolution &SE,
                               SmallVectorImpl<const SCEV *> &Terms,
                               SmallVectorImpl<const SCEV *> &Sizes,
                               const SCEV *ElementSize) {
  if (Terms.size() < 1 || !ElementSize)
    return;

  // Early return when Terms do not contain parameters: we do not delinearize
  // non parametric SCEVs.
  if (!containsParameters(Terms))
    return;

  LLVM_DEBUG({
    dbgs() << "Terms:\n";
    for (const SCEV *T : Terms)
      dbgs() << *T << "\n";
  });

  // Remove duplicates.
  array_pod_sort(Terms.begin(), Terms.end());
  Terms.erase(llvm::unique(Terms), Terms.end());

  // Put larger terms first.
  llvm::sort(Terms, [](const SCEV *LHS, const SCEV *RHS) {
    return numberOfTerms(LHS) > numberOfTerms(RHS);
  });

  // Try to divide all terms by the element size. If term is not divisible by
  // element size, proceed with the original term.
  for (const SCEV *&Term : Terms) {
    const SCEV *Q, *R;
    SCEVDivision::divide(SE, Term, ElementSize, &Q, &R);
    if (!Q->isZero())
      Term = Q;
  }

  SmallVector<const SCEV *, 4> NewTerms;

  // Remove constant factors.
  for (const SCEV *T : Terms)
    if (const SCEV *NewT = removeConstantFactors(SE, T))
      NewTerms.push_back(NewT);

  LLVM_DEBUG({
    dbgs() << "Terms after sorting:\n";
    for (const SCEV *T : NewTerms)
      dbgs() << *T << "\n";
  });

  if (NewTerms.empty() || !findArrayDimensionsRec(SE, NewTerms, Sizes)) {
    Sizes.clear();
    return;
  }

  // The last element to be pushed into Sizes is the size of an element.
  Sizes.push_back(ElementSize);

  LLVM_DEBUG({
    dbgs() << "Sizes:\n";
    for (const SCEV *S : Sizes)
      dbgs() << *S << "\n";
  });
}

void llvm::computeAccessFunctions(ScalarEvolution &SE, const SCEV *Expr,
                                  SmallVectorImpl<const SCEV *> &Subscripts,
                                  SmallVectorImpl<const SCEV *> &Sizes) {
  // Early exit in case this SCEV is not an affine multivariate function.
  if (Sizes.empty())
    return;

  if (auto *AR = dyn_cast<SCEVAddRecExpr>(Expr))
    if (!AR->isAffine())
      return;

  const SCEV *Res = Expr;
  int Last = Sizes.size() - 1;
  for (int i = Last; i >= 0; i--) {
    const SCEV *Q, *R;
    SCEVDivision::divide(SE, Res, Sizes[i], &Q, &R);

    LLVM_DEBUG({
      dbgs() << "Res: " << *Res << "\n";
      dbgs() << "Sizes[i]: " << *Sizes[i] << "\n";
      dbgs() << "Res divided by Sizes[i]:\n";
      dbgs() << "Quotient: " << *Q << "\n";
      dbgs() << "Remainder: " << *R << "\n";
    });

    Res = Q;

    // Do not record the last subscript corresponding to the size of elements in
    // the array.
    if (i == Last) {

      // Bail out if the byte offset is non-zero.
      if (!R->isZero()) {
        Subscripts.clear();
        Sizes.clear();
        return;
      }

      continue;
    }

    // Record the access function for the current subscript.
    Subscripts.push_back(R);
  }

  // Also push in last position the remainder of the last division: it will be
  // the access function of the innermost dimension.
  Subscripts.push_back(Res);

  std::reverse(Subscripts.begin(), Subscripts.end());

  LLVM_DEBUG({
    dbgs() << "Subscripts:\n";
    for (const SCEV *S : Subscripts)
      dbgs() << *S << "\n";
  });
}

/// Splits the SCEV into two vectors of SCEVs representing the subscripts and
/// sizes of an array access. Returns the remainder of the delinearization that
/// is the offset start of the array.  The SCEV->delinearize algorithm computes
/// the multiples of SCEV coefficients: that is a pattern matching of sub
/// expressions in the stride and base of a SCEV corresponding to the
/// computation of a GCD (greatest common divisor) of base and stride.  When
/// SCEV->delinearize fails, it returns the SCEV unchanged.
///
/// For example: when analyzing the memory access A[i][j][k] in this loop nest
///
///  void foo(long n, long m, long o, double A[n][m][o]) {
///
///    for (long i = 0; i < n; i++)
///      for (long j = 0; j < m; j++)
///        for (long k = 0; k < o; k++)
///          A[i][j][k] = 1.0;
///  }
///
/// the delinearization input is the following AddRec SCEV:
///
///  AddRec: {{{%A,+,(8 * %m * %o)}<%for.i>,+,(8 * %o)}<%for.j>,+,8}<%for.k>
///
/// From this SCEV, we are able to say that the base offset of the access is %A
/// because it appears as an offset that does not divide any of the strides in
/// the loops:
///
///  CHECK: Base offset: %A
///
/// and then SCEV->delinearize determines the size of some of the dimensions of
/// the array as these are the multiples by which the strides are happening:
///
///  CHECK: ArrayDecl[UnknownSize][%m][%o] with elements of sizeof(double)
///  bytes.
///
/// Note that the outermost dimension remains of UnknownSize because there are
/// no strides that would help identifying the size of the last dimension: when
/// the array has been statically allocated, one could compute the size of that
/// dimension by dividing the overall size of the array by the size of the known
/// dimensions: %m * %o * 8.
///
/// Finally delinearize provides the access functions for the array reference
/// that does correspond to A[i][j][k] of the above C testcase:
///
///  CHECK: ArrayRef[{0,+,1}<%for.i>][{0,+,1}<%for.j>][{0,+,1}<%for.k>]
///
/// The testcases are checking the output of a function pass:
/// DelinearizationPass that walks through all loads and stores of a function
/// asking for the SCEV of the memory access with respect to all enclosing
/// loops, calling SCEV->delinearize on that and printing the results.
void llvm::delinearize(ScalarEvolution &SE, const SCEV *Expr,
                       SmallVectorImpl<const SCEV *> &Subscripts,
                       SmallVectorImpl<const SCEV *> &Sizes,
                       const SCEV *ElementSize) {
  // First step: collect parametric terms.
  SmallVector<const SCEV *, 4> Terms;
  collectParametricTerms(SE, Expr, Terms);

  if (Terms.empty())
    return;

  // Second step: find subscript sizes.
  findArrayDimensions(SE, Terms, Sizes, ElementSize);

  if (Sizes.empty())
    return;

  // Third step: compute the access functions for each subscript.
  computeAccessFunctions(SE, Expr, Subscripts, Sizes);

  if (Subscripts.empty())
    return;

  LLVM_DEBUG({
    dbgs() << "succeeded to delinearize " << *Expr << "\n";
    dbgs() << "ArrayDecl[UnknownSize]";
    for (const SCEV *S : Sizes)
      dbgs() << "[" << *S << "]";

    dbgs() << "\nArrayRef";
    for (const SCEV *S : Subscripts)
      dbgs() << "[" << *S << "]";
    dbgs() << "\n";
  });
}

bool llvm::getIndexExpressionsFromGEP(ScalarEvolution &SE,
                                      const GetElementPtrInst *GEP,
                                      SmallVectorImpl<const SCEV *> &Subscripts,
                                      SmallVectorImpl<int> &Sizes) {
  assert(Subscripts.empty() && Sizes.empty() &&
         "Expected output lists to be empty on entry to this function.");
  assert(GEP && "getIndexExpressionsFromGEP called with a null GEP");
  Type *Ty = nullptr;
  bool DroppedFirstDim = false;
  for (unsigned i = 1; i < GEP->getNumOperands(); i++) {
    const SCEV *Expr = SE.getSCEV(GEP->getOperand(i));
    if (i == 1) {
      Ty = GEP->getSourceElementType();
      if (auto *Const = dyn_cast<SCEVConstant>(Expr))
        if (Const->getValue()->isZero()) {
          DroppedFirstDim = true;
          continue;
        }
      Subscripts.push_back(Expr);
      continue;
    }

    auto *ArrayTy = dyn_cast<ArrayType>(Ty);
    if (!ArrayTy) {
      Subscripts.clear();
      Sizes.clear();
      return false;
    }

    Subscripts.push_back(Expr);
    if (!(DroppedFirstDim && i == 2))
      Sizes.push_back(ArrayTy->getNumElements());

    Ty = ArrayTy->getElementType();
  }
  return !Subscripts.empty();
}

bool llvm::tryDelinearizeFixedSizeImpl(
    ScalarEvolution *SE, Instruction *Inst, const SCEV *AccessFn,
    SmallVectorImpl<const SCEV *> &Subscripts, SmallVectorImpl<int> &Sizes) {
  Value *SrcPtr = getLoadStorePointerOperand(Inst);

  // Check the simple case where the array dimensions are fixed size.
  auto *SrcGEP = dyn_cast<GetElementPtrInst>(SrcPtr);
  if (!SrcGEP)
    return false;

  getIndexExpressionsFromGEP(*SE, SrcGEP, Subscripts, Sizes);

  // Check that the two size arrays are non-empty and equal in length and
  // value.
  // TODO: it would be better to let the caller to clear Subscripts, similar
  // to how we handle Sizes.
  if (Sizes.empty() || Subscripts.size() <= 1) {
    Subscripts.clear();
    return false;
  }

  // Check that for identical base pointers we do not miss index offsets
  // that have been added before this GEP is applied.
  Value *SrcBasePtr = SrcGEP->getOperand(0)->stripPointerCasts();
  const SCEVUnknown *SrcBase =
      dyn_cast<SCEVUnknown>(SE->getPointerBase(AccessFn));
  if (!SrcBase || SrcBasePtr != SrcBase->getValue()) {
    Subscripts.clear();
    return false;
  }

  assert(Subscripts.size() == Sizes.size() + 1 &&
         "Expected equal number of entries in the list of size and "
         "subscript.");

  return true;
}

namespace {

void printDelinearization(raw_ostream &O, Function *F, LoopInfo *LI,
                          ScalarEvolution *SE) {
  O << "Delinearization on function " << F->getName() << ":\n";
  for (Instruction &Inst : instructions(F)) {
    // Only analyze loads and stores.
    if (!isa<StoreInst>(&Inst) && !isa<LoadInst>(&Inst) &&
        !isa<GetElementPtrInst>(&Inst))
      continue;

    const BasicBlock *BB = Inst.getParent();
    // Delinearize the memory access as analyzed in all the surrounding loops.
    // Do not analyze memory accesses outside loops.
    for (Loop *L = LI->getLoopFor(BB); L != nullptr; L = L->getParentLoop()) {
      const SCEV *AccessFn = SE->getSCEVAtScope(getPointerOperand(&Inst), L);

      const SCEVUnknown *BasePointer =
          dyn_cast<SCEVUnknown>(SE->getPointerBase(AccessFn));
      // Do not delinearize if we cannot find the base pointer.
      if (!BasePointer)
        break;
      AccessFn = SE->getMinusSCEV(AccessFn, BasePointer);

      O << "\n";
      O << "Inst:" << Inst << "\n";
      O << "In Loop with Header: " << L->getHeader()->getName() << "\n";
      O << "AccessFunction: " << *AccessFn << "\n";

      SmallVector<const SCEV *, 3> Subscripts, Sizes;
      delinearize(*SE, AccessFn, Subscripts, Sizes, SE->getElementSize(&Inst));
      if (Subscripts.size() == 0 || Sizes.size() == 0 ||
          Subscripts.size() != Sizes.size()) {
        O << "failed to delinearize\n";
        continue;
      }

      O << "Base offset: " << *BasePointer << "\n";
      O << "ArrayDecl[UnknownSize]";
      int Size = Subscripts.size();
      for (int i = 0; i < Size - 1; i++)
        O << "[" << *Sizes[i] << "]";
      O << " with elements of " << *Sizes[Size - 1] << " bytes.\n";

      O << "ArrayRef";
      for (int i = 0; i < Size; i++)
        O << "[" << *Subscripts[i] << "]";
      O << "\n";
    }
  }
}

} // end anonymous namespace

DelinearizationPrinterPass::DelinearizationPrinterPass(raw_ostream &OS)
    : OS(OS) {}
PreservedAnalyses DelinearizationPrinterPass::run(Function &F,
                                                  FunctionAnalysisManager &AM) {
  printDelinearization(OS, &F, &AM.getResult<LoopAnalysis>(F),
                       &AM.getResult<ScalarEvolutionAnalysis>(F));
  return PreservedAnalyses::all();
}
