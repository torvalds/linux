//===--- TransZeroOutPropsInDealloc.cpp - Transformations to ARC mode -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// removeZeroOutPropsInDealloc:
//
// Removes zero'ing out "strong" @synthesized properties in a -dealloc method.
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class ZeroOutInDeallocRemover :
                           public RecursiveASTVisitor<ZeroOutInDeallocRemover> {
  typedef RecursiveASTVisitor<ZeroOutInDeallocRemover> base;

  MigrationPass &Pass;

  llvm::DenseMap<ObjCPropertyDecl*, ObjCPropertyImplDecl*> SynthesizedProperties;
  ImplicitParamDecl *SelfD;
  ExprSet Removables;
  Selector FinalizeSel;

public:
  ZeroOutInDeallocRemover(MigrationPass &pass) : Pass(pass), SelfD(nullptr) {
    FinalizeSel =
        Pass.Ctx.Selectors.getNullarySelector(&Pass.Ctx.Idents.get("finalize"));
  }

  bool VisitObjCMessageExpr(ObjCMessageExpr *ME) {
    ASTContext &Ctx = Pass.Ctx;
    TransformActions &TA = Pass.TA;

    if (ME->getReceiverKind() != ObjCMessageExpr::Instance)
      return true;
    Expr *receiver = ME->getInstanceReceiver();
    if (!receiver)
      return true;

    DeclRefExpr *refE = dyn_cast<DeclRefExpr>(receiver->IgnoreParenCasts());
    if (!refE || refE->getDecl() != SelfD)
      return true;

    bool BackedBySynthesizeSetter = false;
    for (llvm::DenseMap<ObjCPropertyDecl*, ObjCPropertyImplDecl*>::iterator
         P = SynthesizedProperties.begin(),
         E = SynthesizedProperties.end(); P != E; ++P) {
      ObjCPropertyDecl *PropDecl = P->first;
      if (PropDecl->getSetterName() == ME->getSelector()) {
        BackedBySynthesizeSetter = true;
        break;
      }
    }
    if (!BackedBySynthesizeSetter)
      return true;

    // Remove the setter message if RHS is null
    Transaction Trans(TA);
    Expr *RHS = ME->getArg(0);
    bool RHSIsNull =
      RHS->isNullPointerConstant(Ctx,
                                 Expr::NPC_ValueDependentIsNull);
    if (RHSIsNull && isRemovable(ME))
      TA.removeStmt(ME);

    return true;
  }

  bool VisitPseudoObjectExpr(PseudoObjectExpr *POE) {
    if (isZeroingPropIvar(POE) && isRemovable(POE)) {
      Transaction Trans(Pass.TA);
      Pass.TA.removeStmt(POE);
    }

    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *BOE) {
    if (isZeroingPropIvar(BOE) && isRemovable(BOE)) {
      Transaction Trans(Pass.TA);
      Pass.TA.removeStmt(BOE);
    }

    return true;
  }

  bool TraverseObjCMethodDecl(ObjCMethodDecl *D) {
    if (D->getMethodFamily() != OMF_dealloc &&
        !(D->isInstanceMethod() && D->getSelector() == FinalizeSel))
      return true;
    if (!D->hasBody())
      return true;

    ObjCImplDecl *IMD = dyn_cast<ObjCImplDecl>(D->getDeclContext());
    if (!IMD)
      return true;

    SelfD = D->getSelfDecl();
    collectRemovables(D->getBody(), Removables);

    // For a 'dealloc' method use, find all property implementations in
    // this class implementation.
    for (auto *PID : IMD->property_impls()) {
      if (PID->getPropertyImplementation() ==
          ObjCPropertyImplDecl::Synthesize) {
        ObjCPropertyDecl *PD = PID->getPropertyDecl();
        ObjCMethodDecl *setterM = PD->getSetterMethodDecl();
        if (!(setterM && setterM->isDefined())) {
          ObjCPropertyAttribute::Kind AttrKind = PD->getPropertyAttributes();
          if (AttrKind & (ObjCPropertyAttribute::kind_retain |
                          ObjCPropertyAttribute::kind_copy |
                          ObjCPropertyAttribute::kind_strong))
            SynthesizedProperties[PD] = PID;
        }
      }
    }

    // Now, remove all zeroing of ivars etc.
    base::TraverseObjCMethodDecl(D);

    // clear out for next method.
    SynthesizedProperties.clear();
    SelfD = nullptr;
    Removables.clear();
    return true;
  }

  bool TraverseFunctionDecl(FunctionDecl *D) { return true; }
  bool TraverseBlockDecl(BlockDecl *block) { return true; }
  bool TraverseBlockExpr(BlockExpr *block) { return true; }

private:
  bool isRemovable(Expr *E) const {
    return Removables.count(E);
  }

  bool isZeroingPropIvar(Expr *E) {
    E = E->IgnoreParens();
    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E))
      return isZeroingPropIvar(BO);
    if (PseudoObjectExpr *PO = dyn_cast<PseudoObjectExpr>(E))
      return isZeroingPropIvar(PO);
    return false;
  }

  bool isZeroingPropIvar(BinaryOperator *BOE) {
    if (BOE->getOpcode() == BO_Comma)
      return isZeroingPropIvar(BOE->getLHS()) &&
             isZeroingPropIvar(BOE->getRHS());

    if (BOE->getOpcode() != BO_Assign)
      return false;

    Expr *LHS = BOE->getLHS();
    if (ObjCIvarRefExpr *IV = dyn_cast<ObjCIvarRefExpr>(LHS)) {
      ObjCIvarDecl *IVDecl = IV->getDecl();
      if (!IVDecl->getType()->isObjCObjectPointerType())
        return false;
      bool IvarBacksPropertySynthesis = false;
      for (llvm::DenseMap<ObjCPropertyDecl*, ObjCPropertyImplDecl*>::iterator
           P = SynthesizedProperties.begin(),
           E = SynthesizedProperties.end(); P != E; ++P) {
        ObjCPropertyImplDecl *PropImpDecl = P->second;
        if (PropImpDecl && PropImpDecl->getPropertyIvarDecl() == IVDecl) {
          IvarBacksPropertySynthesis = true;
          break;
        }
      }
      if (!IvarBacksPropertySynthesis)
        return false;
    }
    else
        return false;

    return isZero(BOE->getRHS());
  }

  bool isZeroingPropIvar(PseudoObjectExpr *PO) {
    BinaryOperator *BO = dyn_cast<BinaryOperator>(PO->getSyntacticForm());
    if (!BO) return false;
    if (BO->getOpcode() != BO_Assign) return false;

    ObjCPropertyRefExpr *PropRefExp =
      dyn_cast<ObjCPropertyRefExpr>(BO->getLHS()->IgnoreParens());
    if (!PropRefExp) return false;

    // TODO: Using implicit property decl.
    if (PropRefExp->isImplicitProperty())
      return false;

    if (ObjCPropertyDecl *PDecl = PropRefExp->getExplicitProperty()) {
      if (!SynthesizedProperties.count(PDecl))
        return false;
    }

    return isZero(cast<OpaqueValueExpr>(BO->getRHS())->getSourceExpr());
  }

  bool isZero(Expr *E) {
    if (E->isNullPointerConstant(Pass.Ctx, Expr::NPC_ValueDependentIsNull))
      return true;

    return isZeroingPropIvar(E);
  }
};

} // anonymous namespace

void trans::removeZeroOutPropsInDeallocFinalize(MigrationPass &pass) {
  ZeroOutInDeallocRemover trans(pass);
  trans.TraverseDecl(pass.Ctx.getTranslationUnitDecl());
}
