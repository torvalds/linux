//=- LiveVariables.cpp - Live Variable Analysis for Source CFGs ----------*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Live Variables analysis for source-level CFGs.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <optional>
#include <vector>

using namespace clang;

namespace {
class LiveVariablesImpl {
public:
  AnalysisDeclContext &analysisContext;
  llvm::ImmutableSet<const Expr *>::Factory ESetFact;
  llvm::ImmutableSet<const VarDecl *>::Factory DSetFact;
  llvm::ImmutableSet<const BindingDecl *>::Factory BSetFact;
  llvm::DenseMap<const CFGBlock *, LiveVariables::LivenessValues> blocksEndToLiveness;
  llvm::DenseMap<const CFGBlock *, LiveVariables::LivenessValues> blocksBeginToLiveness;
  llvm::DenseMap<const Stmt *, LiveVariables::LivenessValues> stmtsToLiveness;
  llvm::DenseMap<const DeclRefExpr *, unsigned> inAssignment;
  const bool killAtAssign;

  LiveVariables::LivenessValues
  merge(LiveVariables::LivenessValues valsA,
        LiveVariables::LivenessValues valsB);

  LiveVariables::LivenessValues
  runOnBlock(const CFGBlock *block, LiveVariables::LivenessValues val,
             LiveVariables::Observer *obs = nullptr);

  void dumpBlockLiveness(const SourceManager& M);
  void dumpExprLiveness(const SourceManager& M);

  LiveVariablesImpl(AnalysisDeclContext &ac, bool KillAtAssign)
      : analysisContext(ac),
        ESetFact(false), // Do not canonicalize ImmutableSets by default.
        DSetFact(false), // This is a *major* performance win.
        BSetFact(false), killAtAssign(KillAtAssign) {}
};
} // namespace

static LiveVariablesImpl &getImpl(void *x) {
  return *((LiveVariablesImpl *) x);
}

//===----------------------------------------------------------------------===//
// Operations and queries on LivenessValues.
//===----------------------------------------------------------------------===//

bool LiveVariables::LivenessValues::isLive(const Expr *E) const {
  return liveExprs.contains(E);
}

bool LiveVariables::LivenessValues::isLive(const VarDecl *D) const {
  if (const auto *DD = dyn_cast<DecompositionDecl>(D)) {
    bool alive = false;
    for (const BindingDecl *BD : DD->bindings())
      alive |= liveBindings.contains(BD);

    // Note: the only known case this condition is necessary, is when a bindig
    // to a tuple-like structure is created. The HoldingVar initializers have a
    // DeclRefExpr to the DecompositionDecl.
    alive |= liveDecls.contains(DD);
    return alive;
  }
  return liveDecls.contains(D);
}

namespace {
  template <typename SET>
  SET mergeSets(SET A, SET B) {
    if (A.isEmpty())
      return B;

    for (typename SET::iterator it = B.begin(), ei = B.end(); it != ei; ++it) {
      A = A.add(*it);
    }
    return A;
  }
} // namespace

void LiveVariables::Observer::anchor() { }

LiveVariables::LivenessValues
LiveVariablesImpl::merge(LiveVariables::LivenessValues valsA,
                         LiveVariables::LivenessValues valsB) {

  llvm::ImmutableSetRef<const Expr *> SSetRefA(
      valsA.liveExprs.getRootWithoutRetain(), ESetFact.getTreeFactory()),
      SSetRefB(valsB.liveExprs.getRootWithoutRetain(),
               ESetFact.getTreeFactory());

  llvm::ImmutableSetRef<const VarDecl *>
    DSetRefA(valsA.liveDecls.getRootWithoutRetain(), DSetFact.getTreeFactory()),
    DSetRefB(valsB.liveDecls.getRootWithoutRetain(), DSetFact.getTreeFactory());

  llvm::ImmutableSetRef<const BindingDecl *>
    BSetRefA(valsA.liveBindings.getRootWithoutRetain(), BSetFact.getTreeFactory()),
    BSetRefB(valsB.liveBindings.getRootWithoutRetain(), BSetFact.getTreeFactory());

  SSetRefA = mergeSets(SSetRefA, SSetRefB);
  DSetRefA = mergeSets(DSetRefA, DSetRefB);
  BSetRefA = mergeSets(BSetRefA, BSetRefB);

  // asImmutableSet() canonicalizes the tree, allowing us to do an easy
  // comparison afterwards.
  return LiveVariables::LivenessValues(SSetRefA.asImmutableSet(),
                                       DSetRefA.asImmutableSet(),
                                       BSetRefA.asImmutableSet());
}

bool LiveVariables::LivenessValues::equals(const LivenessValues &V) const {
  return liveExprs == V.liveExprs && liveDecls == V.liveDecls;
}

//===----------------------------------------------------------------------===//
// Query methods.
//===----------------------------------------------------------------------===//

static bool isAlwaysAlive(const VarDecl *D) {
  return D->hasGlobalStorage();
}

bool LiveVariables::isLive(const CFGBlock *B, const VarDecl *D) {
  return isAlwaysAlive(D) || getImpl(impl).blocksEndToLiveness[B].isLive(D);
}

bool LiveVariables::isLive(const Stmt *S, const VarDecl *D) {
  return isAlwaysAlive(D) || getImpl(impl).stmtsToLiveness[S].isLive(D);
}

bool LiveVariables::isLive(const Stmt *Loc, const Expr *Val) {
  return getImpl(impl).stmtsToLiveness[Loc].isLive(Val);
}

//===----------------------------------------------------------------------===//
// Dataflow computation.
//===----------------------------------------------------------------------===//

namespace {
class TransferFunctions : public StmtVisitor<TransferFunctions> {
  LiveVariablesImpl &LV;
  LiveVariables::LivenessValues &val;
  LiveVariables::Observer *observer;
  const CFGBlock *currentBlock;
public:
  TransferFunctions(LiveVariablesImpl &im,
                    LiveVariables::LivenessValues &Val,
                    LiveVariables::Observer *Observer,
                    const CFGBlock *CurrentBlock)
  : LV(im), val(Val), observer(Observer), currentBlock(CurrentBlock) {}

  void VisitBinaryOperator(BinaryOperator *BO);
  void VisitBlockExpr(BlockExpr *BE);
  void VisitDeclRefExpr(DeclRefExpr *DR);
  void VisitDeclStmt(DeclStmt *DS);
  void VisitObjCForCollectionStmt(ObjCForCollectionStmt *OS);
  void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *UE);
  void VisitUnaryOperator(UnaryOperator *UO);
  void Visit(Stmt *S);
};
} // namespace

static const VariableArrayType *FindVA(QualType Ty) {
  const Type *ty = Ty.getTypePtr();
  while (const ArrayType *VT = dyn_cast<ArrayType>(ty)) {
    if (const VariableArrayType *VAT = dyn_cast<VariableArrayType>(VT))
      if (VAT->getSizeExpr())
        return VAT;

    ty = VT->getElementType().getTypePtr();
  }

  return nullptr;
}

static const Expr *LookThroughExpr(const Expr *E) {
  while (E) {
    if (const Expr *Ex = dyn_cast<Expr>(E))
      E = Ex->IgnoreParens();
    if (const FullExpr *FE = dyn_cast<FullExpr>(E)) {
      E = FE->getSubExpr();
      continue;
    }
    if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E)) {
      E = OVE->getSourceExpr();
      continue;
    }
    break;
  }
  return E;
}

static void AddLiveExpr(llvm::ImmutableSet<const Expr *> &Set,
                        llvm::ImmutableSet<const Expr *>::Factory &F,
                        const Expr *E) {
  Set = F.add(Set, LookThroughExpr(E));
}

void TransferFunctions::Visit(Stmt *S) {
  if (observer)
    observer->observeStmt(S, currentBlock, val);

  StmtVisitor<TransferFunctions>::Visit(S);

  if (const auto *E = dyn_cast<Expr>(S)) {
    val.liveExprs = LV.ESetFact.remove(val.liveExprs, E);
  }

  // Mark all children expressions live.

  switch (S->getStmtClass()) {
    default:
      break;
    case Stmt::StmtExprClass: {
      // For statement expressions, look through the compound statement.
      S = cast<StmtExpr>(S)->getSubStmt();
      break;
    }
    case Stmt::CXXMemberCallExprClass: {
      // Include the implicit "this" pointer as being live.
      CXXMemberCallExpr *CE = cast<CXXMemberCallExpr>(S);
      if (Expr *ImplicitObj = CE->getImplicitObjectArgument()) {
        AddLiveExpr(val.liveExprs, LV.ESetFact, ImplicitObj);
      }
      break;
    }
    case Stmt::ObjCMessageExprClass: {
      // In calls to super, include the implicit "self" pointer as being live.
      ObjCMessageExpr *CE = cast<ObjCMessageExpr>(S);
      if (CE->getReceiverKind() == ObjCMessageExpr::SuperInstance)
        val.liveDecls = LV.DSetFact.add(val.liveDecls,
                                        LV.analysisContext.getSelfDecl());
      break;
    }
    case Stmt::DeclStmtClass: {
      const DeclStmt *DS = cast<DeclStmt>(S);
      if (const VarDecl *VD = dyn_cast<VarDecl>(DS->getSingleDecl())) {
        for (const VariableArrayType* VA = FindVA(VD->getType());
             VA != nullptr; VA = FindVA(VA->getElementType())) {
          AddLiveExpr(val.liveExprs, LV.ESetFact, VA->getSizeExpr());
        }
      }
      break;
    }
    case Stmt::PseudoObjectExprClass: {
      // A pseudo-object operation only directly consumes its result
      // expression.
      Expr *child = cast<PseudoObjectExpr>(S)->getResultExpr();
      if (!child) return;
      if (OpaqueValueExpr *OV = dyn_cast<OpaqueValueExpr>(child))
        child = OV->getSourceExpr();
      child = child->IgnoreParens();
      val.liveExprs = LV.ESetFact.add(val.liveExprs, child);
      return;
    }

    // FIXME: These cases eventually shouldn't be needed.
    case Stmt::ExprWithCleanupsClass: {
      S = cast<ExprWithCleanups>(S)->getSubExpr();
      break;
    }
    case Stmt::CXXBindTemporaryExprClass: {
      S = cast<CXXBindTemporaryExpr>(S)->getSubExpr();
      break;
    }
    case Stmt::UnaryExprOrTypeTraitExprClass: {
      // No need to unconditionally visit subexpressions.
      return;
    }
    case Stmt::IfStmtClass: {
      // If one of the branches is an expression rather than a compound
      // statement, it will be bad if we mark it as live at the terminator
      // of the if-statement (i.e., immediately after the condition expression).
      AddLiveExpr(val.liveExprs, LV.ESetFact, cast<IfStmt>(S)->getCond());
      return;
    }
    case Stmt::WhileStmtClass: {
      // If the loop body is an expression rather than a compound statement,
      // it will be bad if we mark it as live at the terminator of the loop
      // (i.e., immediately after the condition expression).
      AddLiveExpr(val.liveExprs, LV.ESetFact, cast<WhileStmt>(S)->getCond());
      return;
    }
    case Stmt::DoStmtClass: {
      // If the loop body is an expression rather than a compound statement,
      // it will be bad if we mark it as live at the terminator of the loop
      // (i.e., immediately after the condition expression).
      AddLiveExpr(val.liveExprs, LV.ESetFact, cast<DoStmt>(S)->getCond());
      return;
    }
    case Stmt::ForStmtClass: {
      // If the loop body is an expression rather than a compound statement,
      // it will be bad if we mark it as live at the terminator of the loop
      // (i.e., immediately after the condition expression).
      AddLiveExpr(val.liveExprs, LV.ESetFact, cast<ForStmt>(S)->getCond());
      return;
    }

  }

  // HACK + FIXME: What is this? One could only guess that this is an attempt to
  // fish for live values, for example, arguments from a call expression.
  // Maybe we could take inspiration from UninitializedVariable analysis?
  for (Stmt *Child : S->children()) {
    if (const auto *E = dyn_cast_or_null<Expr>(Child))
      AddLiveExpr(val.liveExprs, LV.ESetFact, E);
  }
}

static bool writeShouldKill(const VarDecl *VD) {
  return VD && !VD->getType()->isReferenceType() &&
    !isAlwaysAlive(VD);
}

void TransferFunctions::VisitBinaryOperator(BinaryOperator *B) {
  if (LV.killAtAssign && B->getOpcode() == BO_Assign) {
    if (const auto *DR = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParens())) {
      LV.inAssignment[DR] = 1;
    }
  }
  if (B->isAssignmentOp()) {
    if (!LV.killAtAssign)
      return;

    // Assigning to a variable?
    Expr *LHS = B->getLHS()->IgnoreParens();

    if (DeclRefExpr *DR = dyn_cast<DeclRefExpr>(LHS)) {
      const Decl* D = DR->getDecl();
      bool Killed = false;

      if (const BindingDecl* BD = dyn_cast<BindingDecl>(D)) {
        Killed = !BD->getType()->isReferenceType();
        if (Killed) {
          if (const auto *HV = BD->getHoldingVar())
            val.liveDecls = LV.DSetFact.remove(val.liveDecls, HV);

          val.liveBindings = LV.BSetFact.remove(val.liveBindings, BD);
        }
      } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
        Killed = writeShouldKill(VD);
        if (Killed)
          val.liveDecls = LV.DSetFact.remove(val.liveDecls, VD);

      }

      if (Killed && observer)
        observer->observerKill(DR);
    }
  }
}

void TransferFunctions::VisitBlockExpr(BlockExpr *BE) {
  for (const VarDecl *VD :
       LV.analysisContext.getReferencedBlockVars(BE->getBlockDecl())) {
    if (isAlwaysAlive(VD))
      continue;
    val.liveDecls = LV.DSetFact.add(val.liveDecls, VD);
  }
}

void TransferFunctions::VisitDeclRefExpr(DeclRefExpr *DR) {
  const Decl* D = DR->getDecl();
  bool InAssignment = LV.inAssignment[DR];
  if (const auto *BD = dyn_cast<BindingDecl>(D)) {
    if (!InAssignment) {
      if (const auto *HV = BD->getHoldingVar())
        val.liveDecls = LV.DSetFact.add(val.liveDecls, HV);

      val.liveBindings = LV.BSetFact.add(val.liveBindings, BD);
    }
  } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (!InAssignment && !isAlwaysAlive(VD))
      val.liveDecls = LV.DSetFact.add(val.liveDecls, VD);
  }
}

void TransferFunctions::VisitDeclStmt(DeclStmt *DS) {
  for (const auto *DI : DS->decls()) {
    if (const auto *DD = dyn_cast<DecompositionDecl>(DI)) {
      for (const auto *BD : DD->bindings()) {
        if (const auto *HV = BD->getHoldingVar())
          val.liveDecls = LV.DSetFact.remove(val.liveDecls, HV);

        val.liveBindings = LV.BSetFact.remove(val.liveBindings, BD);
      }

      // When a bindig to a tuple-like structure is created, the HoldingVar
      // initializers have a DeclRefExpr to the DecompositionDecl.
      val.liveDecls = LV.DSetFact.remove(val.liveDecls, DD);
    } else if (const auto *VD = dyn_cast<VarDecl>(DI)) {
      if (!isAlwaysAlive(VD))
        val.liveDecls = LV.DSetFact.remove(val.liveDecls, VD);
    }
  }
}

void TransferFunctions::VisitObjCForCollectionStmt(ObjCForCollectionStmt *OS) {
  // Kill the iteration variable.
  DeclRefExpr *DR = nullptr;
  const VarDecl *VD = nullptr;

  Stmt *element = OS->getElement();
  if (DeclStmt *DS = dyn_cast<DeclStmt>(element)) {
    VD = cast<VarDecl>(DS->getSingleDecl());
  }
  else if ((DR = dyn_cast<DeclRefExpr>(cast<Expr>(element)->IgnoreParens()))) {
    VD = cast<VarDecl>(DR->getDecl());
  }

  if (VD) {
    val.liveDecls = LV.DSetFact.remove(val.liveDecls, VD);
    if (observer && DR)
      observer->observerKill(DR);
  }
}

void TransferFunctions::
VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *UE)
{
  // While sizeof(var) doesn't technically extend the liveness of 'var', it
  // does extent the liveness of metadata if 'var' is a VariableArrayType.
  // We handle that special case here.
  if (UE->getKind() != UETT_SizeOf || UE->isArgumentType())
    return;

  const Expr *subEx = UE->getArgumentExpr();
  if (subEx->getType()->isVariableArrayType()) {
    assert(subEx->isLValue());
    val.liveExprs = LV.ESetFact.add(val.liveExprs, subEx->IgnoreParens());
  }
}

void TransferFunctions::VisitUnaryOperator(UnaryOperator *UO) {
  // Treat ++/-- as a kill.
  // Note we don't actually have to do anything if we don't have an observer,
  // since a ++/-- acts as both a kill and a "use".
  if (!observer)
    return;

  switch (UO->getOpcode()) {
  default:
    return;
  case UO_PostInc:
  case UO_PostDec:
  case UO_PreInc:
  case UO_PreDec:
    break;
  }

  if (auto *DR = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParens())) {
    const Decl *D = DR->getDecl();
    if (isa<VarDecl>(D) || isa<BindingDecl>(D)) {
      // Treat ++/-- as a kill.
      observer->observerKill(DR);
    }
  }
}

LiveVariables::LivenessValues
LiveVariablesImpl::runOnBlock(const CFGBlock *block,
                              LiveVariables::LivenessValues val,
                              LiveVariables::Observer *obs) {

  TransferFunctions TF(*this, val, obs, block);

  // Visit the terminator (if any).
  if (const Stmt *term = block->getTerminatorStmt())
    TF.Visit(const_cast<Stmt*>(term));

  // Apply the transfer function for all Stmts in the block.
  for (CFGBlock::const_reverse_iterator it = block->rbegin(),
       ei = block->rend(); it != ei; ++it) {
    const CFGElement &elem = *it;

    if (std::optional<CFGAutomaticObjDtor> Dtor =
            elem.getAs<CFGAutomaticObjDtor>()) {
      val.liveDecls = DSetFact.add(val.liveDecls, Dtor->getVarDecl());
      continue;
    }

    if (!elem.getAs<CFGStmt>())
      continue;

    const Stmt *S = elem.castAs<CFGStmt>().getStmt();
    TF.Visit(const_cast<Stmt*>(S));
    stmtsToLiveness[S] = val;
  }
  return val;
}

void LiveVariables::runOnAllBlocks(LiveVariables::Observer &obs) {
  const CFG *cfg = getImpl(impl).analysisContext.getCFG();
  for (CFG::const_iterator it = cfg->begin(), ei = cfg->end(); it != ei; ++it)
    getImpl(impl).runOnBlock(*it, getImpl(impl).blocksEndToLiveness[*it], &obs);
}

LiveVariables::LiveVariables(void *im) : impl(im) {}

LiveVariables::~LiveVariables() {
  delete (LiveVariablesImpl*) impl;
}

std::unique_ptr<LiveVariables>
LiveVariables::computeLiveness(AnalysisDeclContext &AC, bool killAtAssign) {

  // No CFG?  Bail out.
  CFG *cfg = AC.getCFG();
  if (!cfg)
    return nullptr;

  // The analysis currently has scalability issues for very large CFGs.
  // Bail out if it looks too large.
  if (cfg->getNumBlockIDs() > 300000)
    return nullptr;

  LiveVariablesImpl *LV = new LiveVariablesImpl(AC, killAtAssign);

  // Construct the dataflow worklist.  Enqueue the exit block as the
  // start of the analysis.
  BackwardDataflowWorklist worklist(*cfg, AC);
  llvm::BitVector everAnalyzedBlock(cfg->getNumBlockIDs());

  // FIXME: we should enqueue using post order.
  for (const CFGBlock *B : cfg->nodes()) {
    worklist.enqueueBlock(B);
  }

  while (const CFGBlock *block = worklist.dequeue()) {
    // Determine if the block's end value has changed.  If not, we
    // have nothing left to do for this block.
    LivenessValues &prevVal = LV->blocksEndToLiveness[block];

    // Merge the values of all successor blocks.
    LivenessValues val;
    for (CFGBlock::const_succ_iterator it = block->succ_begin(),
                                       ei = block->succ_end(); it != ei; ++it) {
      if (const CFGBlock *succ = *it) {
        val = LV->merge(val, LV->blocksBeginToLiveness[succ]);
      }
    }

    if (!everAnalyzedBlock[block->getBlockID()])
      everAnalyzedBlock[block->getBlockID()] = true;
    else if (prevVal.equals(val))
      continue;

    prevVal = val;

    // Update the dataflow value for the start of this block.
    LV->blocksBeginToLiveness[block] = LV->runOnBlock(block, val);

    // Enqueue the value to the predecessors.
    worklist.enqueuePredecessors(block);
  }

  return std::unique_ptr<LiveVariables>(new LiveVariables(LV));
}

void LiveVariables::dumpBlockLiveness(const SourceManager &M) {
  getImpl(impl).dumpBlockLiveness(M);
}

void LiveVariablesImpl::dumpBlockLiveness(const SourceManager &M) {
  std::vector<const CFGBlock *> vec;
  for (llvm::DenseMap<const CFGBlock *, LiveVariables::LivenessValues>::iterator
       it = blocksEndToLiveness.begin(), ei = blocksEndToLiveness.end();
       it != ei; ++it) {
    vec.push_back(it->first);
  }
  llvm::sort(vec, [](const CFGBlock *A, const CFGBlock *B) {
    return A->getBlockID() < B->getBlockID();
  });

  std::vector<const VarDecl*> declVec;

  for (std::vector<const CFGBlock *>::iterator
        it = vec.begin(), ei = vec.end(); it != ei; ++it) {
    llvm::errs() << "\n[ B" << (*it)->getBlockID()
                 << " (live variables at block exit) ]\n";

    LiveVariables::LivenessValues vals = blocksEndToLiveness[*it];
    declVec.clear();

    for (llvm::ImmutableSet<const VarDecl *>::iterator si =
          vals.liveDecls.begin(),
          se = vals.liveDecls.end(); si != se; ++si) {
      declVec.push_back(*si);
    }

    llvm::sort(declVec, [](const Decl *A, const Decl *B) {
      return A->getBeginLoc() < B->getBeginLoc();
    });

    for (std::vector<const VarDecl*>::iterator di = declVec.begin(),
         de = declVec.end(); di != de; ++di) {
      llvm::errs() << " " << (*di)->getDeclName().getAsString()
                   << " <";
      (*di)->getLocation().print(llvm::errs(), M);
      llvm::errs() << ">\n";
    }
  }
  llvm::errs() << "\n";
}

void LiveVariables::dumpExprLiveness(const SourceManager &M) {
  getImpl(impl).dumpExprLiveness(M);
}

void LiveVariablesImpl::dumpExprLiveness(const SourceManager &M) {
  // Don't iterate over blockEndsToLiveness directly because it's not sorted.
  for (const CFGBlock *B : *analysisContext.getCFG()) {

    llvm::errs() << "\n[ B" << B->getBlockID()
                 << " (live expressions at block exit) ]\n";
    for (const Expr *E : blocksEndToLiveness[B].liveExprs) {
      llvm::errs() << "\n";
      E->dump();
    }
    llvm::errs() << "\n";
  }
}

const void *LiveVariables::getTag() { static int x; return &x; }
const void *RelaxedLiveVariables::getTag() { static int x; return &x; }
