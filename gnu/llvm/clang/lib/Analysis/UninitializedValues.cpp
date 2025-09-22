//===- UninitializedValues.cpp - Find Uninitialized Values ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements uninitialized values analysis for source-level CFGs.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/UninitializedValues.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/DomainSpecific/ObjCNoReturn.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PackedVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <optional>

using namespace clang;

#define DEBUG_LOGGING 0

static bool recordIsNotEmpty(const RecordDecl *RD) {
  // We consider a record decl to be empty if it contains only unnamed bit-
  // fields, zero-width fields, and fields of empty record type.
  for (const auto *FD : RD->fields()) {
    if (FD->isUnnamedBitField())
      continue;
    if (FD->isZeroSize(FD->getASTContext()))
      continue;
    // The only case remaining to check is for a field declaration of record
    // type and whether that record itself is empty.
    if (const auto *FieldRD = FD->getType()->getAsRecordDecl();
        !FieldRD || recordIsNotEmpty(FieldRD))
      return true;
  }
  return false;
}

static bool isTrackedVar(const VarDecl *vd, const DeclContext *dc) {
  if (vd->isLocalVarDecl() && !vd->hasGlobalStorage() &&
      !vd->isExceptionVariable() && !vd->isInitCapture() && !vd->isImplicit() &&
      vd->getDeclContext() == dc) {
    QualType ty = vd->getType();
    if (const auto *RD = ty->getAsRecordDecl())
      return recordIsNotEmpty(RD);
    return ty->isScalarType() || ty->isVectorType() || ty->isRVVSizelessBuiltinType();
  }
  return false;
}

//------------------------------------------------------------------------====//
// DeclToIndex: a mapping from Decls we track to value indices.
//====------------------------------------------------------------------------//

namespace {

class DeclToIndex {
  llvm::DenseMap<const VarDecl *, unsigned> map;

public:
  DeclToIndex() = default;

  /// Compute the actual mapping from declarations to bits.
  void computeMap(const DeclContext &dc);

  /// Return the number of declarations in the map.
  unsigned size() const { return map.size(); }

  /// Returns the bit vector index for a given declaration.
  std::optional<unsigned> getValueIndex(const VarDecl *d) const;
};

} // namespace

void DeclToIndex::computeMap(const DeclContext &dc) {
  unsigned count = 0;
  DeclContext::specific_decl_iterator<VarDecl> I(dc.decls_begin()),
                                               E(dc.decls_end());
  for ( ; I != E; ++I) {
    const VarDecl *vd = *I;
    if (isTrackedVar(vd, &dc))
      map[vd] = count++;
  }
}

std::optional<unsigned> DeclToIndex::getValueIndex(const VarDecl *d) const {
  llvm::DenseMap<const VarDecl *, unsigned>::const_iterator I = map.find(d);
  if (I == map.end())
    return std::nullopt;
  return I->second;
}

//------------------------------------------------------------------------====//
// CFGBlockValues: dataflow values for CFG blocks.
//====------------------------------------------------------------------------//

// These values are defined in such a way that a merge can be done using
// a bitwise OR.
enum Value { Unknown = 0x0,         /* 00 */
             Initialized = 0x1,     /* 01 */
             Uninitialized = 0x2,   /* 10 */
             MayUninitialized = 0x3 /* 11 */ };

static bool isUninitialized(const Value v) {
  return v >= Uninitialized;
}

static bool isAlwaysUninit(const Value v) {
  return v == Uninitialized;
}

namespace {

using ValueVector = llvm::PackedVector<Value, 2, llvm::SmallBitVector>;

class CFGBlockValues {
  const CFG &cfg;
  SmallVector<ValueVector, 8> vals;
  ValueVector scratch;
  DeclToIndex declToIndex;

public:
  CFGBlockValues(const CFG &cfg);

  unsigned getNumEntries() const { return declToIndex.size(); }

  void computeSetOfDeclarations(const DeclContext &dc);

  ValueVector &getValueVector(const CFGBlock *block) {
    return vals[block->getBlockID()];
  }

  void setAllScratchValues(Value V);
  void mergeIntoScratch(ValueVector const &source, bool isFirst);
  bool updateValueVectorWithScratch(const CFGBlock *block);

  bool hasNoDeclarations() const {
    return declToIndex.size() == 0;
  }

  void resetScratch();

  ValueVector::reference operator[](const VarDecl *vd);

  Value getValue(const CFGBlock *block, const CFGBlock *dstBlock,
                 const VarDecl *vd) {
    std::optional<unsigned> idx = declToIndex.getValueIndex(vd);
    return getValueVector(block)[*idx];
  }
};

} // namespace

CFGBlockValues::CFGBlockValues(const CFG &c) : cfg(c), vals(0) {}

void CFGBlockValues::computeSetOfDeclarations(const DeclContext &dc) {
  declToIndex.computeMap(dc);
  unsigned decls = declToIndex.size();
  scratch.resize(decls);
  unsigned n = cfg.getNumBlockIDs();
  if (!n)
    return;
  vals.resize(n);
  for (auto &val : vals)
    val.resize(decls);
}

#if DEBUG_LOGGING
static void printVector(const CFGBlock *block, ValueVector &bv,
                        unsigned num) {
  llvm::errs() << block->getBlockID() << " :";
  for (const auto &i : bv)
    llvm::errs() << ' ' << i;
  llvm::errs() << " : " << num << '\n';
}
#endif

void CFGBlockValues::setAllScratchValues(Value V) {
  for (unsigned I = 0, E = scratch.size(); I != E; ++I)
    scratch[I] = V;
}

void CFGBlockValues::mergeIntoScratch(ValueVector const &source,
                                      bool isFirst) {
  if (isFirst)
    scratch = source;
  else
    scratch |= source;
}

bool CFGBlockValues::updateValueVectorWithScratch(const CFGBlock *block) {
  ValueVector &dst = getValueVector(block);
  bool changed = (dst != scratch);
  if (changed)
    dst = scratch;
#if DEBUG_LOGGING
  printVector(block, scratch, 0);
#endif
  return changed;
}

void CFGBlockValues::resetScratch() {
  scratch.reset();
}

ValueVector::reference CFGBlockValues::operator[](const VarDecl *vd) {
  return scratch[*declToIndex.getValueIndex(vd)];
}

//------------------------------------------------------------------------====//
// Classification of DeclRefExprs as use or initialization.
//====------------------------------------------------------------------------//

namespace {

class FindVarResult {
  const VarDecl *vd;
  const DeclRefExpr *dr;

public:
  FindVarResult(const VarDecl *vd, const DeclRefExpr *dr) : vd(vd), dr(dr) {}

  const DeclRefExpr *getDeclRefExpr() const { return dr; }
  const VarDecl *getDecl() const { return vd; }
};

} // namespace

static const Expr *stripCasts(ASTContext &C, const Expr *Ex) {
  while (Ex) {
    Ex = Ex->IgnoreParenNoopCasts(C);
    if (const auto *CE = dyn_cast<CastExpr>(Ex)) {
      if (CE->getCastKind() == CK_LValueBitCast) {
        Ex = CE->getSubExpr();
        continue;
      }
    }
    break;
  }
  return Ex;
}

/// If E is an expression comprising a reference to a single variable, find that
/// variable.
static FindVarResult findVar(const Expr *E, const DeclContext *DC) {
  if (const auto *DRE =
          dyn_cast<DeclRefExpr>(stripCasts(DC->getParentASTContext(), E)))
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      if (isTrackedVar(VD, DC))
        return FindVarResult(VD, DRE);
  return FindVarResult(nullptr, nullptr);
}

namespace {

/// Classify each DeclRefExpr as an initialization or a use. Any
/// DeclRefExpr which isn't explicitly classified will be assumed to have
/// escaped the analysis and will be treated as an initialization.
class ClassifyRefs : public StmtVisitor<ClassifyRefs> {
public:
  enum Class {
    Init,
    Use,
    SelfInit,
    ConstRefUse,
    Ignore
  };

private:
  const DeclContext *DC;
  llvm::DenseMap<const DeclRefExpr *, Class> Classification;

  bool isTrackedVar(const VarDecl *VD) const {
    return ::isTrackedVar(VD, DC);
  }

  void classify(const Expr *E, Class C);

public:
  ClassifyRefs(AnalysisDeclContext &AC) : DC(cast<DeclContext>(AC.getDecl())) {}

  void VisitDeclStmt(DeclStmt *DS);
  void VisitUnaryOperator(UnaryOperator *UO);
  void VisitBinaryOperator(BinaryOperator *BO);
  void VisitCallExpr(CallExpr *CE);
  void VisitCastExpr(CastExpr *CE);
  void VisitOMPExecutableDirective(OMPExecutableDirective *ED);

  void operator()(Stmt *S) { Visit(S); }

  Class get(const DeclRefExpr *DRE) const {
    llvm::DenseMap<const DeclRefExpr*, Class>::const_iterator I
        = Classification.find(DRE);
    if (I != Classification.end())
      return I->second;

    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD || !isTrackedVar(VD))
      return Ignore;

    return Init;
  }
};

} // namespace

static const DeclRefExpr *getSelfInitExpr(VarDecl *VD) {
  if (VD->getType()->isRecordType())
    return nullptr;
  if (Expr *Init = VD->getInit()) {
    const auto *DRE =
        dyn_cast<DeclRefExpr>(stripCasts(VD->getASTContext(), Init));
    if (DRE && DRE->getDecl() == VD)
      return DRE;
  }
  return nullptr;
}

void ClassifyRefs::classify(const Expr *E, Class C) {
  // The result of a ?: could also be an lvalue.
  E = E->IgnoreParens();
  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    classify(CO->getTrueExpr(), C);
    classify(CO->getFalseExpr(), C);
    return;
  }

  if (const auto *BCO = dyn_cast<BinaryConditionalOperator>(E)) {
    classify(BCO->getFalseExpr(), C);
    return;
  }

  if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E)) {
    classify(OVE->getSourceExpr(), C);
    return;
  }

  if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(ME->getMemberDecl())) {
      if (!VD->isStaticDataMember())
        classify(ME->getBase(), C);
    }
    return;
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
    case BO_PtrMemD:
    case BO_PtrMemI:
      classify(BO->getLHS(), C);
      return;
    case BO_Comma:
      classify(BO->getRHS(), C);
      return;
    default:
      return;
    }
  }

  FindVarResult Var = findVar(E, DC);
  if (const DeclRefExpr *DRE = Var.getDeclRefExpr())
    Classification[DRE] = std::max(Classification[DRE], C);
}

void ClassifyRefs::VisitDeclStmt(DeclStmt *DS) {
  for (auto *DI : DS->decls()) {
    auto *VD = dyn_cast<VarDecl>(DI);
    if (VD && isTrackedVar(VD))
      if (const DeclRefExpr *DRE = getSelfInitExpr(VD))
        Classification[DRE] = SelfInit;
  }
}

void ClassifyRefs::VisitBinaryOperator(BinaryOperator *BO) {
  // Ignore the evaluation of a DeclRefExpr on the LHS of an assignment. If this
  // is not a compound-assignment, we will treat it as initializing the variable
  // when TransferFunctions visits it. A compound-assignment does not affect
  // whether a variable is uninitialized, and there's no point counting it as a
  // use.
  if (BO->isCompoundAssignmentOp())
    classify(BO->getLHS(), Use);
  else if (BO->getOpcode() == BO_Assign || BO->getOpcode() == BO_Comma)
    classify(BO->getLHS(), Ignore);
}

void ClassifyRefs::VisitUnaryOperator(UnaryOperator *UO) {
  // Increment and decrement are uses despite there being no lvalue-to-rvalue
  // conversion.
  if (UO->isIncrementDecrementOp())
    classify(UO->getSubExpr(), Use);
}

void ClassifyRefs::VisitOMPExecutableDirective(OMPExecutableDirective *ED) {
  for (Stmt *S : OMPExecutableDirective::used_clauses_children(ED->clauses()))
    classify(cast<Expr>(S), Use);
}

static bool isPointerToConst(const QualType &QT) {
  return QT->isAnyPointerType() && QT->getPointeeType().isConstQualified();
}

static bool hasTrivialBody(CallExpr *CE) {
  if (FunctionDecl *FD = CE->getDirectCallee()) {
    if (FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
      return FTD->getTemplatedDecl()->hasTrivialBody();
    return FD->hasTrivialBody();
  }
  return false;
}

void ClassifyRefs::VisitCallExpr(CallExpr *CE) {
  // Classify arguments to std::move as used.
  if (CE->isCallToStdMove()) {
    // RecordTypes are handled in SemaDeclCXX.cpp.
    if (!CE->getArg(0)->getType()->isRecordType())
      classify(CE->getArg(0), Use);
    return;
  }
  bool isTrivialBody = hasTrivialBody(CE);
  // If a value is passed by const pointer to a function,
  // we should not assume that it is initialized by the call, and we
  // conservatively do not assume that it is used.
  // If a value is passed by const reference to a function,
  // it should already be initialized.
  for (CallExpr::arg_iterator I = CE->arg_begin(), E = CE->arg_end();
       I != E; ++I) {
    if ((*I)->isGLValue()) {
      if ((*I)->getType().isConstQualified())
        classify((*I), isTrivialBody ? Ignore : ConstRefUse);
    } else if (isPointerToConst((*I)->getType())) {
      const Expr *Ex = stripCasts(DC->getParentASTContext(), *I);
      const auto *UO = dyn_cast<UnaryOperator>(Ex);
      if (UO && UO->getOpcode() == UO_AddrOf)
        Ex = UO->getSubExpr();
      classify(Ex, Ignore);
    }
  }
}

void ClassifyRefs::VisitCastExpr(CastExpr *CE) {
  if (CE->getCastKind() == CK_LValueToRValue)
    classify(CE->getSubExpr(), Use);
  else if (const auto *CSE = dyn_cast<CStyleCastExpr>(CE)) {
    if (CSE->getType()->isVoidType()) {
      // Squelch any detected load of an uninitialized value if
      // we cast it to void.
      // e.g. (void) x;
      classify(CSE->getSubExpr(), Ignore);
    }
  }
}

//------------------------------------------------------------------------====//
// Transfer function for uninitialized values analysis.
//====------------------------------------------------------------------------//

namespace {

class TransferFunctions : public StmtVisitor<TransferFunctions> {
  CFGBlockValues &vals;
  const CFG &cfg;
  const CFGBlock *block;
  AnalysisDeclContext &ac;
  const ClassifyRefs &classification;
  ObjCNoReturn objCNoRet;
  UninitVariablesHandler &handler;

public:
  TransferFunctions(CFGBlockValues &vals, const CFG &cfg,
                    const CFGBlock *block, AnalysisDeclContext &ac,
                    const ClassifyRefs &classification,
                    UninitVariablesHandler &handler)
      : vals(vals), cfg(cfg), block(block), ac(ac),
        classification(classification), objCNoRet(ac.getASTContext()),
        handler(handler) {}

  void reportUse(const Expr *ex, const VarDecl *vd);
  void reportConstRefUse(const Expr *ex, const VarDecl *vd);

  void VisitBinaryOperator(BinaryOperator *bo);
  void VisitBlockExpr(BlockExpr *be);
  void VisitCallExpr(CallExpr *ce);
  void VisitDeclRefExpr(DeclRefExpr *dr);
  void VisitDeclStmt(DeclStmt *ds);
  void VisitGCCAsmStmt(GCCAsmStmt *as);
  void VisitObjCForCollectionStmt(ObjCForCollectionStmt *FS);
  void VisitObjCMessageExpr(ObjCMessageExpr *ME);
  void VisitOMPExecutableDirective(OMPExecutableDirective *ED);

  bool isTrackedVar(const VarDecl *vd) {
    return ::isTrackedVar(vd, cast<DeclContext>(ac.getDecl()));
  }

  FindVarResult findVar(const Expr *ex) {
    return ::findVar(ex, cast<DeclContext>(ac.getDecl()));
  }

  UninitUse getUninitUse(const Expr *ex, const VarDecl *vd, Value v) {
    UninitUse Use(ex, isAlwaysUninit(v));

    assert(isUninitialized(v));
    if (Use.getKind() == UninitUse::Always)
      return Use;

    // If an edge which leads unconditionally to this use did not initialize
    // the variable, we can say something stronger than 'may be uninitialized':
    // we can say 'either it's used uninitialized or you have dead code'.
    //
    // We track the number of successors of a node which have been visited, and
    // visit a node once we have visited all of its successors. Only edges where
    // the variable might still be uninitialized are followed. Since a variable
    // can't transfer from being initialized to being uninitialized, this will
    // trace out the subgraph which inevitably leads to the use and does not
    // initialize the variable. We do not want to skip past loops, since their
    // non-termination might be correlated with the initialization condition.
    //
    // For example:
    //
    //         void f(bool a, bool b) {
    // block1:   int n;
    //           if (a) {
    // block2:     if (b)
    // block3:       n = 1;
    // block4:   } else if (b) {
    // block5:     while (!a) {
    // block6:       do_work(&a);
    //               n = 2;
    //             }
    //           }
    // block7:   if (a)
    // block8:     g();
    // block9:   return n;
    //         }
    //
    // Starting from the maybe-uninitialized use in block 9:
    //  * Block 7 is not visited because we have only visited one of its two
    //    successors.
    //  * Block 8 is visited because we've visited its only successor.
    // From block 8:
    //  * Block 7 is visited because we've now visited both of its successors.
    // From block 7:
    //  * Blocks 1, 2, 4, 5, and 6 are not visited because we didn't visit all
    //    of their successors (we didn't visit 4, 3, 5, 6, and 5, respectively).
    //  * Block 3 is not visited because it initializes 'n'.
    // Now the algorithm terminates, having visited blocks 7 and 8, and having
    // found the frontier is blocks 2, 4, and 5.
    //
    // 'n' is definitely uninitialized for two edges into block 7 (from blocks 2
    // and 4), so we report that any time either of those edges is taken (in
    // each case when 'b == false'), 'n' is used uninitialized.
    SmallVector<const CFGBlock*, 32> Queue;
    SmallVector<unsigned, 32> SuccsVisited(cfg.getNumBlockIDs(), 0);
    Queue.push_back(block);
    // Specify that we've already visited all successors of the starting block.
    // This has the dual purpose of ensuring we never add it to the queue, and
    // of marking it as not being a candidate element of the frontier.
    SuccsVisited[block->getBlockID()] = block->succ_size();
    while (!Queue.empty()) {
      const CFGBlock *B = Queue.pop_back_val();

      // If the use is always reached from the entry block, make a note of that.
      if (B == &cfg.getEntry())
        Use.setUninitAfterCall();

      for (CFGBlock::const_pred_iterator I = B->pred_begin(), E = B->pred_end();
           I != E; ++I) {
        const CFGBlock *Pred = *I;
        if (!Pred)
          continue;

        Value AtPredExit = vals.getValue(Pred, B, vd);
        if (AtPredExit == Initialized)
          // This block initializes the variable.
          continue;
        if (AtPredExit == MayUninitialized &&
            vals.getValue(B, nullptr, vd) == Uninitialized) {
          // This block declares the variable (uninitialized), and is reachable
          // from a block that initializes the variable. We can't guarantee to
          // give an earlier location for the diagnostic (and it appears that
          // this code is intended to be reachable) so give a diagnostic here
          // and go no further down this path.
          Use.setUninitAfterDecl();
          continue;
        }

        unsigned &SV = SuccsVisited[Pred->getBlockID()];
        if (!SV) {
          // When visiting the first successor of a block, mark all NULL
          // successors as having been visited.
          for (CFGBlock::const_succ_iterator SI = Pred->succ_begin(),
                                             SE = Pred->succ_end();
               SI != SE; ++SI)
            if (!*SI)
              ++SV;
        }

        if (++SV == Pred->succ_size())
          // All paths from this block lead to the use and don't initialize the
          // variable.
          Queue.push_back(Pred);
      }
    }

    // Scan the frontier, looking for blocks where the variable was
    // uninitialized.
    for (const auto *Block : cfg) {
      unsigned BlockID = Block->getBlockID();
      const Stmt *Term = Block->getTerminatorStmt();
      if (SuccsVisited[BlockID] && SuccsVisited[BlockID] < Block->succ_size() &&
          Term) {
        // This block inevitably leads to the use. If we have an edge from here
        // to a post-dominator block, and the variable is uninitialized on that
        // edge, we have found a bug.
        for (CFGBlock::const_succ_iterator I = Block->succ_begin(),
             E = Block->succ_end(); I != E; ++I) {
          const CFGBlock *Succ = *I;
          if (Succ && SuccsVisited[Succ->getBlockID()] >= Succ->succ_size() &&
              vals.getValue(Block, Succ, vd) == Uninitialized) {
            // Switch cases are a special case: report the label to the caller
            // as the 'terminator', not the switch statement itself. Suppress
            // situations where no label matched: we can't be sure that's
            // possible.
            if (isa<SwitchStmt>(Term)) {
              const Stmt *Label = Succ->getLabel();
              if (!Label || !isa<SwitchCase>(Label))
                // Might not be possible.
                continue;
              UninitUse::Branch Branch;
              Branch.Terminator = Label;
              Branch.Output = 0; // Ignored.
              Use.addUninitBranch(Branch);
            } else {
              UninitUse::Branch Branch;
              Branch.Terminator = Term;
              Branch.Output = I - Block->succ_begin();
              Use.addUninitBranch(Branch);
            }
          }
        }
      }
    }

    return Use;
  }
};

} // namespace

void TransferFunctions::reportUse(const Expr *ex, const VarDecl *vd) {
  Value v = vals[vd];
  if (isUninitialized(v))
    handler.handleUseOfUninitVariable(vd, getUninitUse(ex, vd, v));
}

void TransferFunctions::reportConstRefUse(const Expr *ex, const VarDecl *vd) {
  Value v = vals[vd];
  if (isAlwaysUninit(v))
    handler.handleConstRefUseOfUninitVariable(vd, getUninitUse(ex, vd, v));
}

void TransferFunctions::VisitObjCForCollectionStmt(ObjCForCollectionStmt *FS) {
  // This represents an initialization of the 'element' value.
  if (const auto *DS = dyn_cast<DeclStmt>(FS->getElement())) {
    const auto *VD = cast<VarDecl>(DS->getSingleDecl());
    if (isTrackedVar(VD))
      vals[VD] = Initialized;
  }
}

void TransferFunctions::VisitOMPExecutableDirective(
    OMPExecutableDirective *ED) {
  for (Stmt *S : OMPExecutableDirective::used_clauses_children(ED->clauses())) {
    assert(S && "Expected non-null used-in-clause child.");
    Visit(S);
  }
  if (!ED->isStandaloneDirective())
    Visit(ED->getStructuredBlock());
}

void TransferFunctions::VisitBlockExpr(BlockExpr *be) {
  const BlockDecl *bd = be->getBlockDecl();
  for (const auto &I : bd->captures()) {
    const VarDecl *vd = I.getVariable();
    if (!isTrackedVar(vd))
      continue;
    if (I.isByRef()) {
      vals[vd] = Initialized;
      continue;
    }
    reportUse(be, vd);
  }
}

void TransferFunctions::VisitCallExpr(CallExpr *ce) {
  if (Decl *Callee = ce->getCalleeDecl()) {
    if (Callee->hasAttr<ReturnsTwiceAttr>()) {
      // After a call to a function like setjmp or vfork, any variable which is
      // initialized anywhere within this function may now be initialized. For
      // now, just assume such a call initializes all variables.  FIXME: Only
      // mark variables as initialized if they have an initializer which is
      // reachable from here.
      vals.setAllScratchValues(Initialized);
    }
    else if (Callee->hasAttr<AnalyzerNoReturnAttr>()) {
      // Functions labeled like "analyzer_noreturn" are often used to denote
      // "panic" functions that in special debug situations can still return,
      // but for the most part should not be treated as returning.  This is a
      // useful annotation borrowed from the static analyzer that is useful for
      // suppressing branch-specific false positives when we call one of these
      // functions but keep pretending the path continues (when in reality the
      // user doesn't care).
      vals.setAllScratchValues(Unknown);
    }
  }
}

void TransferFunctions::VisitDeclRefExpr(DeclRefExpr *dr) {
  switch (classification.get(dr)) {
  case ClassifyRefs::Ignore:
    break;
  case ClassifyRefs::Use:
    reportUse(dr, cast<VarDecl>(dr->getDecl()));
    break;
  case ClassifyRefs::Init:
    vals[cast<VarDecl>(dr->getDecl())] = Initialized;
    break;
  case ClassifyRefs::SelfInit:
    handler.handleSelfInit(cast<VarDecl>(dr->getDecl()));
    break;
  case ClassifyRefs::ConstRefUse:
    reportConstRefUse(dr, cast<VarDecl>(dr->getDecl()));
    break;
  }
}

void TransferFunctions::VisitBinaryOperator(BinaryOperator *BO) {
  if (BO->getOpcode() == BO_Assign) {
    FindVarResult Var = findVar(BO->getLHS());
    if (const VarDecl *VD = Var.getDecl())
      vals[VD] = Initialized;
  }
}

void TransferFunctions::VisitDeclStmt(DeclStmt *DS) {
  for (auto *DI : DS->decls()) {
    auto *VD = dyn_cast<VarDecl>(DI);
    if (VD && isTrackedVar(VD)) {
      if (getSelfInitExpr(VD)) {
        // If the initializer consists solely of a reference to itself, we
        // explicitly mark the variable as uninitialized. This allows code
        // like the following:
        //
        //   int x = x;
        //
        // to deliberately leave a variable uninitialized. Different analysis
        // clients can detect this pattern and adjust their reporting
        // appropriately, but we need to continue to analyze subsequent uses
        // of the variable.
        vals[VD] = Uninitialized;
      } else if (VD->getInit()) {
        // Treat the new variable as initialized.
        vals[VD] = Initialized;
      } else {
        // No initializer: the variable is now uninitialized. This matters
        // for cases like:
        //   while (...) {
        //     int n;
        //     use(n);
        //     n = 0;
        //   }
        // FIXME: Mark the variable as uninitialized whenever its scope is
        // left, since its scope could be re-entered by a jump over the
        // declaration.
        vals[VD] = Uninitialized;
      }
    }
  }
}

void TransferFunctions::VisitGCCAsmStmt(GCCAsmStmt *as) {
  // An "asm goto" statement is a terminator that may initialize some variables.
  if (!as->isAsmGoto())
    return;

  ASTContext &C = ac.getASTContext();
  for (const Expr *O : as->outputs()) {
    const Expr *Ex = stripCasts(C, O);

    // Strip away any unary operators. Invalid l-values are reported by other
    // semantic analysis passes.
    while (const auto *UO = dyn_cast<UnaryOperator>(Ex))
      Ex = stripCasts(C, UO->getSubExpr());

    // Mark the variable as potentially uninitialized for those cases where
    // it's used on an indirect path, where it's not guaranteed to be
    // defined.
    if (const VarDecl *VD = findVar(Ex).getDecl())
      if (vals[VD] != Initialized)
        vals[VD] = MayUninitialized;
  }
}

void TransferFunctions::VisitObjCMessageExpr(ObjCMessageExpr *ME) {
  // If the Objective-C message expression is an implicit no-return that
  // is not modeled in the CFG, set the tracked dataflow values to Unknown.
  if (objCNoRet.isImplicitNoReturn(ME)) {
    vals.setAllScratchValues(Unknown);
  }
}

//------------------------------------------------------------------------====//
// High-level "driver" logic for uninitialized values analysis.
//====------------------------------------------------------------------------//

static bool runOnBlock(const CFGBlock *block, const CFG &cfg,
                       AnalysisDeclContext &ac, CFGBlockValues &vals,
                       const ClassifyRefs &classification,
                       llvm::BitVector &wasAnalyzed,
                       UninitVariablesHandler &handler) {
  wasAnalyzed[block->getBlockID()] = true;
  vals.resetScratch();
  // Merge in values of predecessor blocks.
  bool isFirst = true;
  for (CFGBlock::const_pred_iterator I = block->pred_begin(),
       E = block->pred_end(); I != E; ++I) {
    const CFGBlock *pred = *I;
    if (!pred)
      continue;
    if (wasAnalyzed[pred->getBlockID()]) {
      vals.mergeIntoScratch(vals.getValueVector(pred), isFirst);
      isFirst = false;
    }
  }
  // Apply the transfer function.
  TransferFunctions tf(vals, cfg, block, ac, classification, handler);
  for (const auto &I : *block) {
    if (std::optional<CFGStmt> cs = I.getAs<CFGStmt>())
      tf.Visit(const_cast<Stmt *>(cs->getStmt()));
  }
  CFGTerminator terminator = block->getTerminator();
  if (auto *as = dyn_cast_or_null<GCCAsmStmt>(terminator.getStmt()))
    if (as->isAsmGoto())
      tf.Visit(as);
  return vals.updateValueVectorWithScratch(block);
}

namespace {

/// PruneBlocksHandler is a special UninitVariablesHandler that is used
/// to detect when a CFGBlock has any *potential* use of an uninitialized
/// variable.  It is mainly used to prune out work during the final
/// reporting pass.
struct PruneBlocksHandler : public UninitVariablesHandler {
  /// Records if a CFGBlock had a potential use of an uninitialized variable.
  llvm::BitVector hadUse;

  /// Records if any CFGBlock had a potential use of an uninitialized variable.
  bool hadAnyUse = false;

  /// The current block to scribble use information.
  unsigned currentBlock = 0;

  PruneBlocksHandler(unsigned numBlocks) : hadUse(numBlocks, false) {}

  ~PruneBlocksHandler() override = default;

  void handleUseOfUninitVariable(const VarDecl *vd,
                                 const UninitUse &use) override {
    hadUse[currentBlock] = true;
    hadAnyUse = true;
  }

  void handleConstRefUseOfUninitVariable(const VarDecl *vd,
                                         const UninitUse &use) override {
    hadUse[currentBlock] = true;
    hadAnyUse = true;
  }

  /// Called when the uninitialized variable analysis detects the
  /// idiom 'int x = x'.  All other uses of 'x' within the initializer
  /// are handled by handleUseOfUninitVariable.
  void handleSelfInit(const VarDecl *vd) override {
    hadUse[currentBlock] = true;
    hadAnyUse = true;
  }
};

} // namespace

void clang::runUninitializedVariablesAnalysis(
    const DeclContext &dc,
    const CFG &cfg,
    AnalysisDeclContext &ac,
    UninitVariablesHandler &handler,
    UninitVariablesAnalysisStats &stats) {
  CFGBlockValues vals(cfg);
  vals.computeSetOfDeclarations(dc);
  if (vals.hasNoDeclarations())
    return;

  stats.NumVariablesAnalyzed = vals.getNumEntries();

  // Precompute which expressions are uses and which are initializations.
  ClassifyRefs classification(ac);
  cfg.VisitBlockStmts(classification);

  // Mark all variables uninitialized at the entry.
  const CFGBlock &entry = cfg.getEntry();
  ValueVector &vec = vals.getValueVector(&entry);
  const unsigned n = vals.getNumEntries();
  for (unsigned j = 0; j < n; ++j) {
    vec[j] = Uninitialized;
  }

  // Proceed with the workist.
  ForwardDataflowWorklist worklist(cfg, ac);
  llvm::BitVector previouslyVisited(cfg.getNumBlockIDs());
  worklist.enqueueSuccessors(&cfg.getEntry());
  llvm::BitVector wasAnalyzed(cfg.getNumBlockIDs(), false);
  wasAnalyzed[cfg.getEntry().getBlockID()] = true;
  PruneBlocksHandler PBH(cfg.getNumBlockIDs());

  while (const CFGBlock *block = worklist.dequeue()) {
    PBH.currentBlock = block->getBlockID();

    // Did the block change?
    bool changed = runOnBlock(block, cfg, ac, vals,
                              classification, wasAnalyzed, PBH);
    ++stats.NumBlockVisits;
    if (changed || !previouslyVisited[block->getBlockID()])
      worklist.enqueueSuccessors(block);
    previouslyVisited[block->getBlockID()] = true;
  }

  if (!PBH.hadAnyUse)
    return;

  // Run through the blocks one more time, and report uninitialized variables.
  for (const auto *block : cfg)
    if (PBH.hadUse[block->getBlockID()]) {
      runOnBlock(block, cfg, ac, vals, classification, wasAnalyzed, handler);
      ++stats.NumBlockVisits;
    }
}

UninitVariablesHandler::~UninitVariablesHandler() = default;
