//===--- SemaPseudoObject.cpp - Semantic Analysis for Pseudo-Objects ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for expressions involving
//  pseudo-object references.  Pseudo-objects are conceptual objects
//  whose storage is entirely abstract and all accesses to which are
//  translated through some sort of abstraction barrier.
//
//  For example, Objective-C objects can have "properties", either
//  declared or undeclared.  A property may be accessed by writing
//    expr.prop
//  where 'expr' is an r-value of Objective-C pointer type and 'prop'
//  is the name of the property.  If this expression is used in a context
//  needing an r-value, it is treated as if it were a message-send
//  of the associated 'getter' selector, typically:
//    [expr prop]
//  If it is used as the LHS of a simple assignment, it is treated
//  as a message-send of the associated 'setter' selector, typically:
//    [expr setProp: RHS]
//  If it is used as the LHS of a compound assignment, or the operand
//  of a unary increment or decrement, both are required;  for example,
//  'expr.prop *= 100' would be translated to:
//    [expr setProp: [expr prop] * 100]
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaPseudoObject.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "llvm/ADT/SmallString.h"

using namespace clang;
using namespace sema;

namespace {
  // Basically just a very focused copy of TreeTransform.
  struct Rebuilder {
    Sema &S;
    unsigned MSPropertySubscriptCount;
    typedef llvm::function_ref<Expr *(Expr *, unsigned)> SpecificRebuilderRefTy;
    const SpecificRebuilderRefTy &SpecificCallback;
    Rebuilder(Sema &S, const SpecificRebuilderRefTy &SpecificCallback)
        : S(S), MSPropertySubscriptCount(0),
          SpecificCallback(SpecificCallback) {}

    Expr *rebuildObjCPropertyRefExpr(ObjCPropertyRefExpr *refExpr) {
      // Fortunately, the constraint that we're rebuilding something
      // with a base limits the number of cases here.
      if (refExpr->isClassReceiver() || refExpr->isSuperReceiver())
        return refExpr;

      if (refExpr->isExplicitProperty()) {
        return new (S.Context) ObjCPropertyRefExpr(
            refExpr->getExplicitProperty(), refExpr->getType(),
            refExpr->getValueKind(), refExpr->getObjectKind(),
            refExpr->getLocation(), SpecificCallback(refExpr->getBase(), 0));
      }
      return new (S.Context) ObjCPropertyRefExpr(
          refExpr->getImplicitPropertyGetter(),
          refExpr->getImplicitPropertySetter(), refExpr->getType(),
          refExpr->getValueKind(), refExpr->getObjectKind(),
          refExpr->getLocation(), SpecificCallback(refExpr->getBase(), 0));
    }
    Expr *rebuildObjCSubscriptRefExpr(ObjCSubscriptRefExpr *refExpr) {
      assert(refExpr->getBaseExpr());
      assert(refExpr->getKeyExpr());

      return new (S.Context) ObjCSubscriptRefExpr(
          SpecificCallback(refExpr->getBaseExpr(), 0),
          SpecificCallback(refExpr->getKeyExpr(), 1), refExpr->getType(),
          refExpr->getValueKind(), refExpr->getObjectKind(),
          refExpr->getAtIndexMethodDecl(), refExpr->setAtIndexMethodDecl(),
          refExpr->getRBracket());
    }
    Expr *rebuildMSPropertyRefExpr(MSPropertyRefExpr *refExpr) {
      assert(refExpr->getBaseExpr());

      return new (S.Context) MSPropertyRefExpr(
          SpecificCallback(refExpr->getBaseExpr(), 0),
          refExpr->getPropertyDecl(), refExpr->isArrow(), refExpr->getType(),
          refExpr->getValueKind(), refExpr->getQualifierLoc(),
          refExpr->getMemberLoc());
    }
    Expr *rebuildMSPropertySubscriptExpr(MSPropertySubscriptExpr *refExpr) {
      assert(refExpr->getBase());
      assert(refExpr->getIdx());

      auto *NewBase = rebuild(refExpr->getBase());
      ++MSPropertySubscriptCount;
      return new (S.Context) MSPropertySubscriptExpr(
          NewBase,
          SpecificCallback(refExpr->getIdx(), MSPropertySubscriptCount),
          refExpr->getType(), refExpr->getValueKind(), refExpr->getObjectKind(),
          refExpr->getRBracketLoc());
    }

    Expr *rebuild(Expr *e) {
      // Fast path: nothing to look through.
      if (auto *PRE = dyn_cast<ObjCPropertyRefExpr>(e))
        return rebuildObjCPropertyRefExpr(PRE);
      if (auto *SRE = dyn_cast<ObjCSubscriptRefExpr>(e))
        return rebuildObjCSubscriptRefExpr(SRE);
      if (auto *MSPRE = dyn_cast<MSPropertyRefExpr>(e))
        return rebuildMSPropertyRefExpr(MSPRE);
      if (auto *MSPSE = dyn_cast<MSPropertySubscriptExpr>(e))
        return rebuildMSPropertySubscriptExpr(MSPSE);

      // Otherwise, we should look through and rebuild anything that
      // IgnoreParens would.

      if (ParenExpr *parens = dyn_cast<ParenExpr>(e)) {
        e = rebuild(parens->getSubExpr());
        return new (S.Context) ParenExpr(parens->getLParen(),
                                         parens->getRParen(),
                                         e);
      }

      if (UnaryOperator *uop = dyn_cast<UnaryOperator>(e)) {
        assert(uop->getOpcode() == UO_Extension);
        e = rebuild(uop->getSubExpr());
        return UnaryOperator::Create(
            S.Context, e, uop->getOpcode(), uop->getType(), uop->getValueKind(),
            uop->getObjectKind(), uop->getOperatorLoc(), uop->canOverflow(),
            S.CurFPFeatureOverrides());
      }

      if (GenericSelectionExpr *gse = dyn_cast<GenericSelectionExpr>(e)) {
        assert(!gse->isResultDependent());
        unsigned resultIndex = gse->getResultIndex();
        unsigned numAssocs = gse->getNumAssocs();

        SmallVector<Expr *, 8> assocExprs;
        SmallVector<TypeSourceInfo *, 8> assocTypes;
        assocExprs.reserve(numAssocs);
        assocTypes.reserve(numAssocs);

        for (const GenericSelectionExpr::Association assoc :
             gse->associations()) {
          Expr *assocExpr = assoc.getAssociationExpr();
          if (assoc.isSelected())
            assocExpr = rebuild(assocExpr);
          assocExprs.push_back(assocExpr);
          assocTypes.push_back(assoc.getTypeSourceInfo());
        }

        if (gse->isExprPredicate())
          return GenericSelectionExpr::Create(
              S.Context, gse->getGenericLoc(), gse->getControllingExpr(),
              assocTypes, assocExprs, gse->getDefaultLoc(), gse->getRParenLoc(),
              gse->containsUnexpandedParameterPack(), resultIndex);
        return GenericSelectionExpr::Create(
            S.Context, gse->getGenericLoc(), gse->getControllingType(),
            assocTypes, assocExprs, gse->getDefaultLoc(), gse->getRParenLoc(),
            gse->containsUnexpandedParameterPack(), resultIndex);
      }

      if (ChooseExpr *ce = dyn_cast<ChooseExpr>(e)) {
        assert(!ce->isConditionDependent());

        Expr *LHS = ce->getLHS(), *RHS = ce->getRHS();
        Expr *&rebuiltExpr = ce->isConditionTrue() ? LHS : RHS;
        rebuiltExpr = rebuild(rebuiltExpr);

        return new (S.Context)
            ChooseExpr(ce->getBuiltinLoc(), ce->getCond(), LHS, RHS,
                       rebuiltExpr->getType(), rebuiltExpr->getValueKind(),
                       rebuiltExpr->getObjectKind(), ce->getRParenLoc(),
                       ce->isConditionTrue());
      }

      llvm_unreachable("bad expression to rebuild!");
    }
  };

  class PseudoOpBuilder {
  public:
    Sema &S;
    unsigned ResultIndex;
    SourceLocation GenericLoc;
    bool IsUnique;
    SmallVector<Expr *, 4> Semantics;

    PseudoOpBuilder(Sema &S, SourceLocation genericLoc, bool IsUnique)
      : S(S), ResultIndex(PseudoObjectExpr::NoResult),
        GenericLoc(genericLoc), IsUnique(IsUnique) {}

    virtual ~PseudoOpBuilder() {}

    /// Add a normal semantic expression.
    void addSemanticExpr(Expr *semantic) {
      Semantics.push_back(semantic);
    }

    /// Add the 'result' semantic expression.
    void addResultSemanticExpr(Expr *resultExpr) {
      assert(ResultIndex == PseudoObjectExpr::NoResult);
      ResultIndex = Semantics.size();
      Semantics.push_back(resultExpr);
      // An OVE is not unique if it is used as the result expression.
      if (auto *OVE = dyn_cast<OpaqueValueExpr>(Semantics.back()))
        OVE->setIsUnique(false);
    }

    ExprResult buildRValueOperation(Expr *op);
    ExprResult buildAssignmentOperation(Scope *Sc,
                                        SourceLocation opLoc,
                                        BinaryOperatorKind opcode,
                                        Expr *LHS, Expr *RHS);
    ExprResult buildIncDecOperation(Scope *Sc, SourceLocation opLoc,
                                    UnaryOperatorKind opcode,
                                    Expr *op);

    virtual ExprResult complete(Expr *syntacticForm);

    OpaqueValueExpr *capture(Expr *op);
    OpaqueValueExpr *captureValueAsResult(Expr *op);

    void setResultToLastSemantic() {
      assert(ResultIndex == PseudoObjectExpr::NoResult);
      ResultIndex = Semantics.size() - 1;
      // An OVE is not unique if it is used as the result expression.
      if (auto *OVE = dyn_cast<OpaqueValueExpr>(Semantics.back()))
        OVE->setIsUnique(false);
    }

    /// Return true if assignments have a non-void result.
    static bool CanCaptureValue(Expr *exp) {
      if (exp->isGLValue())
        return true;
      QualType ty = exp->getType();
      assert(!ty->isIncompleteType());
      assert(!ty->isDependentType());

      if (const CXXRecordDecl *ClassDecl = ty->getAsCXXRecordDecl())
        return ClassDecl->isTriviallyCopyable();
      return true;
    }

    virtual Expr *rebuildAndCaptureObject(Expr *) = 0;
    virtual ExprResult buildGet() = 0;
    virtual ExprResult buildSet(Expr *, SourceLocation,
                                bool captureSetValueAsResult) = 0;
    /// Should the result of an assignment be the formal result of the
    /// setter call or the value that was passed to the setter?
    ///
    /// Different pseudo-object language features use different language rules
    /// for this.
    /// The default is to use the set value.  Currently, this affects the
    /// behavior of simple assignments, compound assignments, and prefix
    /// increment and decrement.
    /// Postfix increment and decrement always use the getter result as the
    /// expression result.
    ///
    /// If this method returns true, and the set value isn't capturable for
    /// some reason, the result of the expression will be void.
    virtual bool captureSetValueAsResult() const { return true; }
  };

  /// A PseudoOpBuilder for Objective-C \@properties.
  class ObjCPropertyOpBuilder : public PseudoOpBuilder {
    ObjCPropertyRefExpr *RefExpr;
    ObjCPropertyRefExpr *SyntacticRefExpr;
    OpaqueValueExpr *InstanceReceiver;
    ObjCMethodDecl *Getter;

    ObjCMethodDecl *Setter;
    Selector SetterSelector;
    Selector GetterSelector;

  public:
    ObjCPropertyOpBuilder(Sema &S, ObjCPropertyRefExpr *refExpr, bool IsUnique)
        : PseudoOpBuilder(S, refExpr->getLocation(), IsUnique),
          RefExpr(refExpr), SyntacticRefExpr(nullptr),
          InstanceReceiver(nullptr), Getter(nullptr), Setter(nullptr) {
    }

    ExprResult buildRValueOperation(Expr *op);
    ExprResult buildAssignmentOperation(Scope *Sc,
                                        SourceLocation opLoc,
                                        BinaryOperatorKind opcode,
                                        Expr *LHS, Expr *RHS);
    ExprResult buildIncDecOperation(Scope *Sc, SourceLocation opLoc,
                                    UnaryOperatorKind opcode,
                                    Expr *op);

    bool tryBuildGetOfReference(Expr *op, ExprResult &result);
    bool findSetter(bool warn=true);
    bool findGetter();
    void DiagnoseUnsupportedPropertyUse();

    Expr *rebuildAndCaptureObject(Expr *syntacticBase) override;
    ExprResult buildGet() override;
    ExprResult buildSet(Expr *op, SourceLocation, bool) override;
    ExprResult complete(Expr *SyntacticForm) override;

    bool isWeakProperty() const;
  };

 /// A PseudoOpBuilder for Objective-C array/dictionary indexing.
 class ObjCSubscriptOpBuilder : public PseudoOpBuilder {
   ObjCSubscriptRefExpr *RefExpr;
   OpaqueValueExpr *InstanceBase;
   OpaqueValueExpr *InstanceKey;
   ObjCMethodDecl *AtIndexGetter;
   Selector AtIndexGetterSelector;

   ObjCMethodDecl *AtIndexSetter;
   Selector AtIndexSetterSelector;

 public:
   ObjCSubscriptOpBuilder(Sema &S, ObjCSubscriptRefExpr *refExpr, bool IsUnique)
       : PseudoOpBuilder(S, refExpr->getSourceRange().getBegin(), IsUnique),
         RefExpr(refExpr), InstanceBase(nullptr), InstanceKey(nullptr),
         AtIndexGetter(nullptr), AtIndexSetter(nullptr) {}

   ExprResult buildRValueOperation(Expr *op);
   ExprResult buildAssignmentOperation(Scope *Sc,
                                       SourceLocation opLoc,
                                       BinaryOperatorKind opcode,
                                       Expr *LHS, Expr *RHS);
   Expr *rebuildAndCaptureObject(Expr *syntacticBase) override;

   bool findAtIndexGetter();
   bool findAtIndexSetter();

   ExprResult buildGet() override;
   ExprResult buildSet(Expr *op, SourceLocation, bool) override;
 };

 class MSPropertyOpBuilder : public PseudoOpBuilder {
   MSPropertyRefExpr *RefExpr;
   OpaqueValueExpr *InstanceBase;
   SmallVector<Expr *, 4> CallArgs;

   MSPropertyRefExpr *getBaseMSProperty(MSPropertySubscriptExpr *E);

 public:
   MSPropertyOpBuilder(Sema &S, MSPropertyRefExpr *refExpr, bool IsUnique)
       : PseudoOpBuilder(S, refExpr->getSourceRange().getBegin(), IsUnique),
         RefExpr(refExpr), InstanceBase(nullptr) {}
   MSPropertyOpBuilder(Sema &S, MSPropertySubscriptExpr *refExpr, bool IsUnique)
       : PseudoOpBuilder(S, refExpr->getSourceRange().getBegin(), IsUnique),
         InstanceBase(nullptr) {
     RefExpr = getBaseMSProperty(refExpr);
   }

   Expr *rebuildAndCaptureObject(Expr *) override;
   ExprResult buildGet() override;
   ExprResult buildSet(Expr *op, SourceLocation, bool) override;
   bool captureSetValueAsResult() const override { return false; }
 };
}

/// Capture the given expression in an OpaqueValueExpr.
OpaqueValueExpr *PseudoOpBuilder::capture(Expr *e) {
  // Make a new OVE whose source is the given expression.
  OpaqueValueExpr *captured =
    new (S.Context) OpaqueValueExpr(GenericLoc, e->getType(),
                                    e->getValueKind(), e->getObjectKind(),
                                    e);
  if (IsUnique)
    captured->setIsUnique(true);

  // Make sure we bind that in the semantics.
  addSemanticExpr(captured);
  return captured;
}

/// Capture the given expression as the result of this pseudo-object
/// operation.  This routine is safe against expressions which may
/// already be captured.
///
/// \returns the captured expression, which will be the
///   same as the input if the input was already captured
OpaqueValueExpr *PseudoOpBuilder::captureValueAsResult(Expr *e) {
  assert(ResultIndex == PseudoObjectExpr::NoResult);

  // If the expression hasn't already been captured, just capture it
  // and set the new semantic
  if (!isa<OpaqueValueExpr>(e)) {
    OpaqueValueExpr *cap = capture(e);
    setResultToLastSemantic();
    return cap;
  }

  // Otherwise, it must already be one of our semantic expressions;
  // set ResultIndex to its index.
  unsigned index = 0;
  for (;; ++index) {
    assert(index < Semantics.size() &&
           "captured expression not found in semantics!");
    if (e == Semantics[index]) break;
  }
  ResultIndex = index;
  // An OVE is not unique if it is used as the result expression.
  cast<OpaqueValueExpr>(e)->setIsUnique(false);
  return cast<OpaqueValueExpr>(e);
}

/// The routine which creates the final PseudoObjectExpr.
ExprResult PseudoOpBuilder::complete(Expr *syntactic) {
  return PseudoObjectExpr::Create(S.Context, syntactic,
                                  Semantics, ResultIndex);
}

/// The main skeleton for building an r-value operation.
ExprResult PseudoOpBuilder::buildRValueOperation(Expr *op) {
  Expr *syntacticBase = rebuildAndCaptureObject(op);

  ExprResult getExpr = buildGet();
  if (getExpr.isInvalid()) return ExprError();
  addResultSemanticExpr(getExpr.get());

  return complete(syntacticBase);
}

/// The basic skeleton for building a simple or compound
/// assignment operation.
ExprResult
PseudoOpBuilder::buildAssignmentOperation(Scope *Sc, SourceLocation opcLoc,
                                          BinaryOperatorKind opcode,
                                          Expr *LHS, Expr *RHS) {
  assert(BinaryOperator::isAssignmentOp(opcode));

  Expr *syntacticLHS = rebuildAndCaptureObject(LHS);
  OpaqueValueExpr *capturedRHS = capture(RHS);

  // In some very specific cases, semantic analysis of the RHS as an
  // expression may require it to be rewritten.  In these cases, we
  // cannot safely keep the OVE around.  Fortunately, we don't really
  // need to: we don't use this particular OVE in multiple places, and
  // no clients rely that closely on matching up expressions in the
  // semantic expression with expressions from the syntactic form.
  Expr *semanticRHS = capturedRHS;
  if (RHS->hasPlaceholderType() || isa<InitListExpr>(RHS)) {
    semanticRHS = RHS;
    Semantics.pop_back();
  }

  Expr *syntactic;

  ExprResult result;
  if (opcode == BO_Assign) {
    result = semanticRHS;
    syntactic = BinaryOperator::Create(S.Context, syntacticLHS, capturedRHS,
                                       opcode, capturedRHS->getType(),
                                       capturedRHS->getValueKind(), OK_Ordinary,
                                       opcLoc, S.CurFPFeatureOverrides());

  } else {
    ExprResult opLHS = buildGet();
    if (opLHS.isInvalid()) return ExprError();

    // Build an ordinary, non-compound operation.
    BinaryOperatorKind nonCompound =
      BinaryOperator::getOpForCompoundAssignment(opcode);
    result = S.BuildBinOp(Sc, opcLoc, nonCompound, opLHS.get(), semanticRHS);
    if (result.isInvalid()) return ExprError();

    syntactic = CompoundAssignOperator::Create(
        S.Context, syntacticLHS, capturedRHS, opcode, result.get()->getType(),
        result.get()->getValueKind(), OK_Ordinary, opcLoc,
        S.CurFPFeatureOverrides(), opLHS.get()->getType(),
        result.get()->getType());
  }

  // The result of the assignment, if not void, is the value set into
  // the l-value.
  result = buildSet(result.get(), opcLoc, captureSetValueAsResult());
  if (result.isInvalid()) return ExprError();
  addSemanticExpr(result.get());
  if (!captureSetValueAsResult() && !result.get()->getType()->isVoidType() &&
      (result.get()->isTypeDependent() || CanCaptureValue(result.get())))
    setResultToLastSemantic();

  return complete(syntactic);
}

/// The basic skeleton for building an increment or decrement
/// operation.
ExprResult
PseudoOpBuilder::buildIncDecOperation(Scope *Sc, SourceLocation opcLoc,
                                      UnaryOperatorKind opcode,
                                      Expr *op) {
  assert(UnaryOperator::isIncrementDecrementOp(opcode));

  Expr *syntacticOp = rebuildAndCaptureObject(op);

  // Load the value.
  ExprResult result = buildGet();
  if (result.isInvalid()) return ExprError();

  QualType resultType = result.get()->getType();

  // That's the postfix result.
  if (UnaryOperator::isPostfix(opcode) &&
      (result.get()->isTypeDependent() || CanCaptureValue(result.get()))) {
    result = capture(result.get());
    setResultToLastSemantic();
  }

  // Add or subtract a literal 1.
  llvm::APInt oneV(S.Context.getTypeSize(S.Context.IntTy), 1);
  Expr *one = IntegerLiteral::Create(S.Context, oneV, S.Context.IntTy,
                                     GenericLoc);

  if (UnaryOperator::isIncrementOp(opcode)) {
    result = S.BuildBinOp(Sc, opcLoc, BO_Add, result.get(), one);
  } else {
    result = S.BuildBinOp(Sc, opcLoc, BO_Sub, result.get(), one);
  }
  if (result.isInvalid()) return ExprError();

  // Store that back into the result.  The value stored is the result
  // of a prefix operation.
  result = buildSet(result.get(), opcLoc, UnaryOperator::isPrefix(opcode) &&
                                              captureSetValueAsResult());
  if (result.isInvalid()) return ExprError();
  addSemanticExpr(result.get());
  if (UnaryOperator::isPrefix(opcode) && !captureSetValueAsResult() &&
      !result.get()->getType()->isVoidType() &&
      (result.get()->isTypeDependent() || CanCaptureValue(result.get())))
    setResultToLastSemantic();

  UnaryOperator *syntactic =
      UnaryOperator::Create(S.Context, syntacticOp, opcode, resultType,
                            VK_LValue, OK_Ordinary, opcLoc,
                            !resultType->isDependentType()
                                ? S.Context.getTypeSize(resultType) >=
                                      S.Context.getTypeSize(S.Context.IntTy)
                                : false,
                            S.CurFPFeatureOverrides());
  return complete(syntactic);
}


//===----------------------------------------------------------------------===//
//  Objective-C @property and implicit property references
//===----------------------------------------------------------------------===//

/// Look up a method in the receiver type of an Objective-C property
/// reference.
static ObjCMethodDecl *LookupMethodInReceiverType(Sema &S, Selector sel,
                                            const ObjCPropertyRefExpr *PRE) {
  if (PRE->isObjectReceiver()) {
    const ObjCObjectPointerType *PT =
      PRE->getBase()->getType()->castAs<ObjCObjectPointerType>();

    // Special case for 'self' in class method implementations.
    if (PT->isObjCClassType() &&
        S.ObjC().isSelfExpr(const_cast<Expr *>(PRE->getBase()))) {
      // This cast is safe because isSelfExpr is only true within
      // methods.
      ObjCMethodDecl *method =
        cast<ObjCMethodDecl>(S.CurContext->getNonClosureAncestor());
      return S.ObjC().LookupMethodInObjectType(
          sel, S.Context.getObjCInterfaceType(method->getClassInterface()),
          /*instance*/ false);
    }

    return S.ObjC().LookupMethodInObjectType(sel, PT->getPointeeType(), true);
  }

  if (PRE->isSuperReceiver()) {
    if (const ObjCObjectPointerType *PT =
        PRE->getSuperReceiverType()->getAs<ObjCObjectPointerType>())
      return S.ObjC().LookupMethodInObjectType(sel, PT->getPointeeType(), true);

    return S.ObjC().LookupMethodInObjectType(sel, PRE->getSuperReceiverType(),
                                             false);
  }

  assert(PRE->isClassReceiver() && "Invalid expression");
  QualType IT = S.Context.getObjCInterfaceType(PRE->getClassReceiver());
  return S.ObjC().LookupMethodInObjectType(sel, IT, false);
}

bool ObjCPropertyOpBuilder::isWeakProperty() const {
  QualType T;
  if (RefExpr->isExplicitProperty()) {
    const ObjCPropertyDecl *Prop = RefExpr->getExplicitProperty();
    if (Prop->getPropertyAttributes() & ObjCPropertyAttribute::kind_weak)
      return true;

    T = Prop->getType();
  } else if (Getter) {
    T = Getter->getReturnType();
  } else {
    return false;
  }

  return T.getObjCLifetime() == Qualifiers::OCL_Weak;
}

bool ObjCPropertyOpBuilder::findGetter() {
  if (Getter) return true;

  // For implicit properties, just trust the lookup we already did.
  if (RefExpr->isImplicitProperty()) {
    if ((Getter = RefExpr->getImplicitPropertyGetter())) {
      GetterSelector = Getter->getSelector();
      return true;
    }
    else {
      // Must build the getter selector the hard way.
      ObjCMethodDecl *setter = RefExpr->getImplicitPropertySetter();
      assert(setter && "both setter and getter are null - cannot happen");
      const IdentifierInfo *setterName =
          setter->getSelector().getIdentifierInfoForSlot(0);
      const IdentifierInfo *getterName =
          &S.Context.Idents.get(setterName->getName().substr(3));
      GetterSelector =
        S.PP.getSelectorTable().getNullarySelector(getterName);
      return false;
    }
  }

  ObjCPropertyDecl *prop = RefExpr->getExplicitProperty();
  Getter = LookupMethodInReceiverType(S, prop->getGetterName(), RefExpr);
  return (Getter != nullptr);
}

/// Try to find the most accurate setter declaration for the property
/// reference.
///
/// \return true if a setter was found, in which case Setter
bool ObjCPropertyOpBuilder::findSetter(bool warn) {
  // For implicit properties, just trust the lookup we already did.
  if (RefExpr->isImplicitProperty()) {
    if (ObjCMethodDecl *setter = RefExpr->getImplicitPropertySetter()) {
      Setter = setter;
      SetterSelector = setter->getSelector();
      return true;
    } else {
      const IdentifierInfo *getterName = RefExpr->getImplicitPropertyGetter()
                                             ->getSelector()
                                             .getIdentifierInfoForSlot(0);
      SetterSelector =
        SelectorTable::constructSetterSelector(S.PP.getIdentifierTable(),
                                               S.PP.getSelectorTable(),
                                               getterName);
      return false;
    }
  }

  // For explicit properties, this is more involved.
  ObjCPropertyDecl *prop = RefExpr->getExplicitProperty();
  SetterSelector = prop->getSetterName();

  // Do a normal method lookup first.
  if (ObjCMethodDecl *setter =
        LookupMethodInReceiverType(S, SetterSelector, RefExpr)) {
    if (setter->isPropertyAccessor() && warn)
      if (const ObjCInterfaceDecl *IFace =
          dyn_cast<ObjCInterfaceDecl>(setter->getDeclContext())) {
        StringRef thisPropertyName = prop->getName();
        // Try flipping the case of the first character.
        char front = thisPropertyName.front();
        front = isLowercase(front) ? toUppercase(front) : toLowercase(front);
        SmallString<100> PropertyName = thisPropertyName;
        PropertyName[0] = front;
        const IdentifierInfo *AltMember =
            &S.PP.getIdentifierTable().get(PropertyName);
        if (ObjCPropertyDecl *prop1 = IFace->FindPropertyDeclaration(
                AltMember, prop->getQueryKind()))
          if (prop != prop1 && (prop1->getSetterMethodDecl() == setter)) {
            S.Diag(RefExpr->getExprLoc(), diag::err_property_setter_ambiguous_use)
              << prop << prop1 << setter->getSelector();
            S.Diag(prop->getLocation(), diag::note_property_declare);
            S.Diag(prop1->getLocation(), diag::note_property_declare);
          }
      }
    Setter = setter;
    return true;
  }

  // That can fail in the somewhat crazy situation that we're
  // type-checking a message send within the @interface declaration
  // that declared the @property.  But it's not clear that that's
  // valuable to support.

  return false;
}

void ObjCPropertyOpBuilder::DiagnoseUnsupportedPropertyUse() {
  if (S.getCurLexicalContext()->isObjCContainer() &&
      S.getCurLexicalContext()->getDeclKind() != Decl::ObjCCategoryImpl &&
      S.getCurLexicalContext()->getDeclKind() != Decl::ObjCImplementation) {
    if (ObjCPropertyDecl *prop = RefExpr->getExplicitProperty()) {
        S.Diag(RefExpr->getLocation(),
               diag::err_property_function_in_objc_container);
        S.Diag(prop->getLocation(), diag::note_property_declare);
    }
  }
}

/// Capture the base object of an Objective-C property expression.
Expr *ObjCPropertyOpBuilder::rebuildAndCaptureObject(Expr *syntacticBase) {
  assert(InstanceReceiver == nullptr);

  // If we have a base, capture it in an OVE and rebuild the syntactic
  // form to use the OVE as its base.
  if (RefExpr->isObjectReceiver()) {
    InstanceReceiver = capture(RefExpr->getBase());
    syntacticBase = Rebuilder(S, [=](Expr *, unsigned) -> Expr * {
                      return InstanceReceiver;
                    }).rebuild(syntacticBase);
  }

  if (ObjCPropertyRefExpr *
        refE = dyn_cast<ObjCPropertyRefExpr>(syntacticBase->IgnoreParens()))
    SyntacticRefExpr = refE;

  return syntacticBase;
}

/// Load from an Objective-C property reference.
ExprResult ObjCPropertyOpBuilder::buildGet() {
  findGetter();
  if (!Getter) {
    DiagnoseUnsupportedPropertyUse();
    return ExprError();
  }

  if (SyntacticRefExpr)
    SyntacticRefExpr->setIsMessagingGetter();

  QualType receiverType = RefExpr->getReceiverType(S.Context);
  if (!Getter->isImplicit())
    S.DiagnoseUseOfDecl(Getter, GenericLoc, nullptr, true);
  // Build a message-send.
  ExprResult msg;
  if ((Getter->isInstanceMethod() && !RefExpr->isClassReceiver()) ||
      RefExpr->isObjectReceiver()) {
    assert(InstanceReceiver || RefExpr->isSuperReceiver());
    msg = S.ObjC().BuildInstanceMessageImplicit(
        InstanceReceiver, receiverType, GenericLoc, Getter->getSelector(),
        Getter, std::nullopt);
  } else {
    msg = S.ObjC().BuildClassMessageImplicit(
        receiverType, RefExpr->isSuperReceiver(), GenericLoc,
        Getter->getSelector(), Getter, std::nullopt);
  }
  return msg;
}

/// Store to an Objective-C property reference.
///
/// \param captureSetValueAsResult If true, capture the actual
///   value being set as the value of the property operation.
ExprResult ObjCPropertyOpBuilder::buildSet(Expr *op, SourceLocation opcLoc,
                                           bool captureSetValueAsResult) {
  if (!findSetter(false)) {
    DiagnoseUnsupportedPropertyUse();
    return ExprError();
  }

  if (SyntacticRefExpr)
    SyntacticRefExpr->setIsMessagingSetter();

  QualType receiverType = RefExpr->getReceiverType(S.Context);

  // Use assignment constraints when possible; they give us better
  // diagnostics.  "When possible" basically means anything except a
  // C++ class type.
  if (!S.getLangOpts().CPlusPlus || !op->getType()->isRecordType()) {
    QualType paramType = (*Setter->param_begin())->getType()
                           .substObjCMemberType(
                             receiverType,
                             Setter->getDeclContext(),
                             ObjCSubstitutionContext::Parameter);
    if (!S.getLangOpts().CPlusPlus || !paramType->isRecordType()) {
      ExprResult opResult = op;
      Sema::AssignConvertType assignResult
        = S.CheckSingleAssignmentConstraints(paramType, opResult);
      if (opResult.isInvalid() ||
          S.DiagnoseAssignmentResult(assignResult, opcLoc, paramType,
                                     op->getType(), opResult.get(),
                                     Sema::AA_Assigning))
        return ExprError();

      op = opResult.get();
      assert(op && "successful assignment left argument invalid?");
    }
  }

  // Arguments.
  Expr *args[] = { op };

  // Build a message-send.
  ExprResult msg;
  if (!Setter->isImplicit())
    S.DiagnoseUseOfDecl(Setter, GenericLoc, nullptr, true);
  if ((Setter->isInstanceMethod() && !RefExpr->isClassReceiver()) ||
      RefExpr->isObjectReceiver()) {
    msg = S.ObjC().BuildInstanceMessageImplicit(InstanceReceiver, receiverType,
                                                GenericLoc, SetterSelector,
                                                Setter, MultiExprArg(args, 1));
  } else {
    msg = S.ObjC().BuildClassMessageImplicit(
        receiverType, RefExpr->isSuperReceiver(), GenericLoc, SetterSelector,
        Setter, MultiExprArg(args, 1));
  }

  if (!msg.isInvalid() && captureSetValueAsResult) {
    ObjCMessageExpr *msgExpr =
      cast<ObjCMessageExpr>(msg.get()->IgnoreImplicit());
    Expr *arg = msgExpr->getArg(0);
    if (CanCaptureValue(arg))
      msgExpr->setArg(0, captureValueAsResult(arg));
  }

  return msg;
}

/// @property-specific behavior for doing lvalue-to-rvalue conversion.
ExprResult ObjCPropertyOpBuilder::buildRValueOperation(Expr *op) {
  // Explicit properties always have getters, but implicit ones don't.
  // Check that before proceeding.
  if (RefExpr->isImplicitProperty() && !RefExpr->getImplicitPropertyGetter()) {
    S.Diag(RefExpr->getLocation(), diag::err_getter_not_found)
        << RefExpr->getSourceRange();
    return ExprError();
  }

  ExprResult result = PseudoOpBuilder::buildRValueOperation(op);
  if (result.isInvalid()) return ExprError();

  if (RefExpr->isExplicitProperty() && !Getter->hasRelatedResultType())
    S.ObjC().DiagnosePropertyAccessorMismatch(RefExpr->getExplicitProperty(),
                                              Getter, RefExpr->getLocation());

  // As a special case, if the method returns 'id', try to get
  // a better type from the property.
  if (RefExpr->isExplicitProperty() && result.get()->isPRValue()) {
    QualType receiverType = RefExpr->getReceiverType(S.Context);
    QualType propType = RefExpr->getExplicitProperty()
                          ->getUsageType(receiverType);
    if (result.get()->getType()->isObjCIdType()) {
      if (const ObjCObjectPointerType *ptr
            = propType->getAs<ObjCObjectPointerType>()) {
        if (!ptr->isObjCIdType())
          result = S.ImpCastExprToType(result.get(), propType, CK_BitCast);
      }
    }
    if (propType.getObjCLifetime() == Qualifiers::OCL_Weak &&
        !S.Diags.isIgnored(diag::warn_arc_repeated_use_of_weak,
                           RefExpr->getLocation()))
      S.getCurFunction()->markSafeWeakUse(RefExpr);
  }

  return result;
}

/// Try to build this as a call to a getter that returns a reference.
///
/// \return true if it was possible, whether or not it actually
///   succeeded
bool ObjCPropertyOpBuilder::tryBuildGetOfReference(Expr *op,
                                                   ExprResult &result) {
  if (!S.getLangOpts().CPlusPlus) return false;

  findGetter();
  if (!Getter) {
    // The property has no setter and no getter! This can happen if the type is
    // invalid. Error have already been reported.
    result = ExprError();
    return true;
  }

  // Only do this if the getter returns an l-value reference type.
  QualType resultType = Getter->getReturnType();
  if (!resultType->isLValueReferenceType()) return false;

  result = buildRValueOperation(op);
  return true;
}

/// @property-specific behavior for doing assignments.
ExprResult
ObjCPropertyOpBuilder::buildAssignmentOperation(Scope *Sc,
                                                SourceLocation opcLoc,
                                                BinaryOperatorKind opcode,
                                                Expr *LHS, Expr *RHS) {
  assert(BinaryOperator::isAssignmentOp(opcode));

  // If there's no setter, we have no choice but to try to assign to
  // the result of the getter.
  if (!findSetter()) {
    ExprResult result;
    if (tryBuildGetOfReference(LHS, result)) {
      if (result.isInvalid()) return ExprError();
      return S.BuildBinOp(Sc, opcLoc, opcode, result.get(), RHS);
    }

    // Otherwise, it's an error.
    S.Diag(opcLoc, diag::err_nosetter_property_assignment)
      << unsigned(RefExpr->isImplicitProperty())
      << SetterSelector
      << LHS->getSourceRange() << RHS->getSourceRange();
    return ExprError();
  }

  // If there is a setter, we definitely want to use it.

  // Verify that we can do a compound assignment.
  if (opcode != BO_Assign && !findGetter()) {
    S.Diag(opcLoc, diag::err_nogetter_property_compound_assignment)
      << LHS->getSourceRange() << RHS->getSourceRange();
    return ExprError();
  }

  ExprResult result =
    PseudoOpBuilder::buildAssignmentOperation(Sc, opcLoc, opcode, LHS, RHS);
  if (result.isInvalid()) return ExprError();

  // Various warnings about property assignments in ARC.
  if (S.getLangOpts().ObjCAutoRefCount && InstanceReceiver) {
    S.ObjC().checkRetainCycles(InstanceReceiver->getSourceExpr(), RHS);
    S.checkUnsafeExprAssigns(opcLoc, LHS, RHS);
  }

  return result;
}

/// @property-specific behavior for doing increments and decrements.
ExprResult
ObjCPropertyOpBuilder::buildIncDecOperation(Scope *Sc, SourceLocation opcLoc,
                                            UnaryOperatorKind opcode,
                                            Expr *op) {
  // If there's no setter, we have no choice but to try to assign to
  // the result of the getter.
  if (!findSetter()) {
    ExprResult result;
    if (tryBuildGetOfReference(op, result)) {
      if (result.isInvalid()) return ExprError();
      return S.BuildUnaryOp(Sc, opcLoc, opcode, result.get());
    }

    // Otherwise, it's an error.
    S.Diag(opcLoc, diag::err_nosetter_property_incdec)
      << unsigned(RefExpr->isImplicitProperty())
      << unsigned(UnaryOperator::isDecrementOp(opcode))
      << SetterSelector
      << op->getSourceRange();
    return ExprError();
  }

  // If there is a setter, we definitely want to use it.

  // We also need a getter.
  if (!findGetter()) {
    assert(RefExpr->isImplicitProperty());
    S.Diag(opcLoc, diag::err_nogetter_property_incdec)
      << unsigned(UnaryOperator::isDecrementOp(opcode))
      << GetterSelector
      << op->getSourceRange();
    return ExprError();
  }

  return PseudoOpBuilder::buildIncDecOperation(Sc, opcLoc, opcode, op);
}

ExprResult ObjCPropertyOpBuilder::complete(Expr *SyntacticForm) {
  if (isWeakProperty() && !S.isUnevaluatedContext() &&
      !S.Diags.isIgnored(diag::warn_arc_repeated_use_of_weak,
                         SyntacticForm->getBeginLoc()))
    S.getCurFunction()->recordUseOfWeak(SyntacticRefExpr,
                                        SyntacticRefExpr->isMessagingGetter());

  return PseudoOpBuilder::complete(SyntacticForm);
}

// ObjCSubscript build stuff.
//

/// objective-c subscripting-specific behavior for doing lvalue-to-rvalue
/// conversion.
/// FIXME. Remove this routine if it is proven that no additional
/// specifity is needed.
ExprResult ObjCSubscriptOpBuilder::buildRValueOperation(Expr *op) {
  ExprResult result = PseudoOpBuilder::buildRValueOperation(op);
  if (result.isInvalid()) return ExprError();
  return result;
}

/// objective-c subscripting-specific  behavior for doing assignments.
ExprResult
ObjCSubscriptOpBuilder::buildAssignmentOperation(Scope *Sc,
                                                SourceLocation opcLoc,
                                                BinaryOperatorKind opcode,
                                                Expr *LHS, Expr *RHS) {
  assert(BinaryOperator::isAssignmentOp(opcode));
  // There must be a method to do the Index'ed assignment.
  if (!findAtIndexSetter())
    return ExprError();

  // Verify that we can do a compound assignment.
  if (opcode != BO_Assign && !findAtIndexGetter())
    return ExprError();

  ExprResult result =
  PseudoOpBuilder::buildAssignmentOperation(Sc, opcLoc, opcode, LHS, RHS);
  if (result.isInvalid()) return ExprError();

  // Various warnings about objc Index'ed assignments in ARC.
  if (S.getLangOpts().ObjCAutoRefCount && InstanceBase) {
    S.ObjC().checkRetainCycles(InstanceBase->getSourceExpr(), RHS);
    S.checkUnsafeExprAssigns(opcLoc, LHS, RHS);
  }

  return result;
}

/// Capture the base object of an Objective-C Index'ed expression.
Expr *ObjCSubscriptOpBuilder::rebuildAndCaptureObject(Expr *syntacticBase) {
  assert(InstanceBase == nullptr);

  // Capture base expression in an OVE and rebuild the syntactic
  // form to use the OVE as its base expression.
  InstanceBase = capture(RefExpr->getBaseExpr());
  InstanceKey = capture(RefExpr->getKeyExpr());

  syntacticBase =
      Rebuilder(S, [=](Expr *, unsigned Idx) -> Expr * {
        switch (Idx) {
        case 0:
          return InstanceBase;
        case 1:
          return InstanceKey;
        default:
          llvm_unreachable("Unexpected index for ObjCSubscriptExpr");
        }
      }).rebuild(syntacticBase);

  return syntacticBase;
}

/// CheckKeyForObjCARCConversion - This routine suggests bridge casting of CF
/// objects used as dictionary subscript key objects.
static void CheckKeyForObjCARCConversion(Sema &S, QualType ContainerT,
                                         Expr *Key) {
  if (ContainerT.isNull())
    return;
  // dictionary subscripting.
  // - (id)objectForKeyedSubscript:(id)key;
  const IdentifierInfo *KeyIdents[] = {
      &S.Context.Idents.get("objectForKeyedSubscript")};
  Selector GetterSelector = S.Context.Selectors.getSelector(1, KeyIdents);
  ObjCMethodDecl *Getter = S.ObjC().LookupMethodInObjectType(
      GetterSelector, ContainerT, true /*instance*/);
  if (!Getter)
    return;
  QualType T = Getter->parameters()[0]->getType();
  S.ObjC().CheckObjCConversion(Key->getSourceRange(), T, Key,
                               CheckedConversionKind::Implicit);
}

bool ObjCSubscriptOpBuilder::findAtIndexGetter() {
  if (AtIndexGetter)
    return true;

  Expr *BaseExpr = RefExpr->getBaseExpr();
  QualType BaseT = BaseExpr->getType();

  QualType ResultType;
  if (const ObjCObjectPointerType *PTy =
      BaseT->getAs<ObjCObjectPointerType>()) {
    ResultType = PTy->getPointeeType();
  }
  SemaObjC::ObjCSubscriptKind Res =
      S.ObjC().CheckSubscriptingKind(RefExpr->getKeyExpr());
  if (Res == SemaObjC::OS_Error) {
    if (S.getLangOpts().ObjCAutoRefCount)
      CheckKeyForObjCARCConversion(S, ResultType,
                                   RefExpr->getKeyExpr());
    return false;
  }
  bool arrayRef = (Res == SemaObjC::OS_Array);

  if (ResultType.isNull()) {
    S.Diag(BaseExpr->getExprLoc(), diag::err_objc_subscript_base_type)
      << BaseExpr->getType() << arrayRef;
    return false;
  }
  if (!arrayRef) {
    // dictionary subscripting.
    // - (id)objectForKeyedSubscript:(id)key;
    const IdentifierInfo *KeyIdents[] = {
        &S.Context.Idents.get("objectForKeyedSubscript")};
    AtIndexGetterSelector = S.Context.Selectors.getSelector(1, KeyIdents);
  }
  else {
    // - (id)objectAtIndexedSubscript:(size_t)index;
    const IdentifierInfo *KeyIdents[] = {
        &S.Context.Idents.get("objectAtIndexedSubscript")};

    AtIndexGetterSelector = S.Context.Selectors.getSelector(1, KeyIdents);
  }

  AtIndexGetter = S.ObjC().LookupMethodInObjectType(
      AtIndexGetterSelector, ResultType, true /*instance*/);

  if (!AtIndexGetter && S.getLangOpts().DebuggerObjCLiteral) {
    AtIndexGetter = ObjCMethodDecl::Create(
        S.Context, SourceLocation(), SourceLocation(), AtIndexGetterSelector,
        S.Context.getObjCIdType() /*ReturnType*/, nullptr /*TypeSourceInfo */,
        S.Context.getTranslationUnitDecl(), true /*Instance*/,
        false /*isVariadic*/,
        /*isPropertyAccessor=*/false,
        /*isSynthesizedAccessorStub=*/false,
        /*isImplicitlyDeclared=*/true, /*isDefined=*/false,
        ObjCImplementationControl::Required, false);
    ParmVarDecl *Argument = ParmVarDecl::Create(S.Context, AtIndexGetter,
                                                SourceLocation(), SourceLocation(),
                                                arrayRef ? &S.Context.Idents.get("index")
                                                         : &S.Context.Idents.get("key"),
                                                arrayRef ? S.Context.UnsignedLongTy
                                                         : S.Context.getObjCIdType(),
                                                /*TInfo=*/nullptr,
                                                SC_None,
                                                nullptr);
    AtIndexGetter->setMethodParams(S.Context, Argument, std::nullopt);
  }

  if (!AtIndexGetter) {
    if (!BaseT->isObjCIdType()) {
      S.Diag(BaseExpr->getExprLoc(), diag::err_objc_subscript_method_not_found)
      << BaseExpr->getType() << 0 << arrayRef;
      return false;
    }
    AtIndexGetter = S.ObjC().LookupInstanceMethodInGlobalPool(
        AtIndexGetterSelector, RefExpr->getSourceRange(), true);
  }

  if (AtIndexGetter) {
    QualType T = AtIndexGetter->parameters()[0]->getType();
    if ((arrayRef && !T->isIntegralOrEnumerationType()) ||
        (!arrayRef && !T->isObjCObjectPointerType())) {
      S.Diag(RefExpr->getKeyExpr()->getExprLoc(),
             arrayRef ? diag::err_objc_subscript_index_type
                      : diag::err_objc_subscript_key_type) << T;
      S.Diag(AtIndexGetter->parameters()[0]->getLocation(),
             diag::note_parameter_type) << T;
      return false;
    }
    QualType R = AtIndexGetter->getReturnType();
    if (!R->isObjCObjectPointerType()) {
      S.Diag(RefExpr->getKeyExpr()->getExprLoc(),
             diag::err_objc_indexing_method_result_type) << R << arrayRef;
      S.Diag(AtIndexGetter->getLocation(), diag::note_method_declared_at) <<
        AtIndexGetter->getDeclName();
    }
  }
  return true;
}

bool ObjCSubscriptOpBuilder::findAtIndexSetter() {
  if (AtIndexSetter)
    return true;

  Expr *BaseExpr = RefExpr->getBaseExpr();
  QualType BaseT = BaseExpr->getType();

  QualType ResultType;
  if (const ObjCObjectPointerType *PTy =
      BaseT->getAs<ObjCObjectPointerType>()) {
    ResultType = PTy->getPointeeType();
  }

  SemaObjC::ObjCSubscriptKind Res =
      S.ObjC().CheckSubscriptingKind(RefExpr->getKeyExpr());
  if (Res == SemaObjC::OS_Error) {
    if (S.getLangOpts().ObjCAutoRefCount)
      CheckKeyForObjCARCConversion(S, ResultType,
                                   RefExpr->getKeyExpr());
    return false;
  }
  bool arrayRef = (Res == SemaObjC::OS_Array);

  if (ResultType.isNull()) {
    S.Diag(BaseExpr->getExprLoc(), diag::err_objc_subscript_base_type)
      << BaseExpr->getType() << arrayRef;
    return false;
  }

  if (!arrayRef) {
    // dictionary subscripting.
    // - (void)setObject:(id)object forKeyedSubscript:(id)key;
    const IdentifierInfo *KeyIdents[] = {
        &S.Context.Idents.get("setObject"),
        &S.Context.Idents.get("forKeyedSubscript")};
    AtIndexSetterSelector = S.Context.Selectors.getSelector(2, KeyIdents);
  }
  else {
    // - (void)setObject:(id)object atIndexedSubscript:(NSInteger)index;
    const IdentifierInfo *KeyIdents[] = {
        &S.Context.Idents.get("setObject"),
        &S.Context.Idents.get("atIndexedSubscript")};
    AtIndexSetterSelector = S.Context.Selectors.getSelector(2, KeyIdents);
  }
  AtIndexSetter = S.ObjC().LookupMethodInObjectType(
      AtIndexSetterSelector, ResultType, true /*instance*/);

  if (!AtIndexSetter && S.getLangOpts().DebuggerObjCLiteral) {
    TypeSourceInfo *ReturnTInfo = nullptr;
    QualType ReturnType = S.Context.VoidTy;
    AtIndexSetter = ObjCMethodDecl::Create(
        S.Context, SourceLocation(), SourceLocation(), AtIndexSetterSelector,
        ReturnType, ReturnTInfo, S.Context.getTranslationUnitDecl(),
        true /*Instance*/, false /*isVariadic*/,
        /*isPropertyAccessor=*/false,
        /*isSynthesizedAccessorStub=*/false,
        /*isImplicitlyDeclared=*/true, /*isDefined=*/false,
        ObjCImplementationControl::Required, false);
    SmallVector<ParmVarDecl *, 2> Params;
    ParmVarDecl *object = ParmVarDecl::Create(S.Context, AtIndexSetter,
                                                SourceLocation(), SourceLocation(),
                                                &S.Context.Idents.get("object"),
                                                S.Context.getObjCIdType(),
                                                /*TInfo=*/nullptr,
                                                SC_None,
                                                nullptr);
    Params.push_back(object);
    ParmVarDecl *key = ParmVarDecl::Create(S.Context, AtIndexSetter,
                                                SourceLocation(), SourceLocation(),
                                                arrayRef ?  &S.Context.Idents.get("index")
                                                         :  &S.Context.Idents.get("key"),
                                                arrayRef ? S.Context.UnsignedLongTy
                                                         : S.Context.getObjCIdType(),
                                                /*TInfo=*/nullptr,
                                                SC_None,
                                                nullptr);
    Params.push_back(key);
    AtIndexSetter->setMethodParams(S.Context, Params, std::nullopt);
  }

  if (!AtIndexSetter) {
    if (!BaseT->isObjCIdType()) {
      S.Diag(BaseExpr->getExprLoc(),
             diag::err_objc_subscript_method_not_found)
      << BaseExpr->getType() << 1 << arrayRef;
      return false;
    }
    AtIndexSetter = S.ObjC().LookupInstanceMethodInGlobalPool(
        AtIndexSetterSelector, RefExpr->getSourceRange(), true);
  }

  bool err = false;
  if (AtIndexSetter && arrayRef) {
    QualType T = AtIndexSetter->parameters()[1]->getType();
    if (!T->isIntegralOrEnumerationType()) {
      S.Diag(RefExpr->getKeyExpr()->getExprLoc(),
             diag::err_objc_subscript_index_type) << T;
      S.Diag(AtIndexSetter->parameters()[1]->getLocation(),
             diag::note_parameter_type) << T;
      err = true;
    }
    T = AtIndexSetter->parameters()[0]->getType();
    if (!T->isObjCObjectPointerType()) {
      S.Diag(RefExpr->getBaseExpr()->getExprLoc(),
             diag::err_objc_subscript_object_type) << T << arrayRef;
      S.Diag(AtIndexSetter->parameters()[0]->getLocation(),
             diag::note_parameter_type) << T;
      err = true;
    }
  }
  else if (AtIndexSetter && !arrayRef)
    for (unsigned i=0; i <2; i++) {
      QualType T = AtIndexSetter->parameters()[i]->getType();
      if (!T->isObjCObjectPointerType()) {
        if (i == 1)
          S.Diag(RefExpr->getKeyExpr()->getExprLoc(),
                 diag::err_objc_subscript_key_type) << T;
        else
          S.Diag(RefExpr->getBaseExpr()->getExprLoc(),
                 diag::err_objc_subscript_dic_object_type) << T;
        S.Diag(AtIndexSetter->parameters()[i]->getLocation(),
               diag::note_parameter_type) << T;
        err = true;
      }
    }

  return !err;
}

// Get the object at "Index" position in the container.
// [BaseExpr objectAtIndexedSubscript : IndexExpr];
ExprResult ObjCSubscriptOpBuilder::buildGet() {
  if (!findAtIndexGetter())
    return ExprError();

  QualType receiverType = InstanceBase->getType();

  // Build a message-send.
  ExprResult msg;
  Expr *Index = InstanceKey;

  // Arguments.
  Expr *args[] = { Index };
  assert(InstanceBase);
  if (AtIndexGetter)
    S.DiagnoseUseOfDecl(AtIndexGetter, GenericLoc);
  msg = S.ObjC().BuildInstanceMessageImplicit(
      InstanceBase, receiverType, GenericLoc, AtIndexGetterSelector,
      AtIndexGetter, MultiExprArg(args, 1));
  return msg;
}

/// Store into the container the "op" object at "Index"'ed location
/// by building this messaging expression:
/// - (void)setObject:(id)object atIndexedSubscript:(NSInteger)index;
/// \param captureSetValueAsResult If true, capture the actual
///   value being set as the value of the property operation.
ExprResult ObjCSubscriptOpBuilder::buildSet(Expr *op, SourceLocation opcLoc,
                                           bool captureSetValueAsResult) {
  if (!findAtIndexSetter())
    return ExprError();
  if (AtIndexSetter)
    S.DiagnoseUseOfDecl(AtIndexSetter, GenericLoc);
  QualType receiverType = InstanceBase->getType();
  Expr *Index = InstanceKey;

  // Arguments.
  Expr *args[] = { op, Index };

  // Build a message-send.
  ExprResult msg = S.ObjC().BuildInstanceMessageImplicit(
      InstanceBase, receiverType, GenericLoc, AtIndexSetterSelector,
      AtIndexSetter, MultiExprArg(args, 2));

  if (!msg.isInvalid() && captureSetValueAsResult) {
    ObjCMessageExpr *msgExpr =
      cast<ObjCMessageExpr>(msg.get()->IgnoreImplicit());
    Expr *arg = msgExpr->getArg(0);
    if (CanCaptureValue(arg))
      msgExpr->setArg(0, captureValueAsResult(arg));
  }

  return msg;
}

//===----------------------------------------------------------------------===//
//  MSVC __declspec(property) references
//===----------------------------------------------------------------------===//

MSPropertyRefExpr *
MSPropertyOpBuilder::getBaseMSProperty(MSPropertySubscriptExpr *E) {
  CallArgs.insert(CallArgs.begin(), E->getIdx());
  Expr *Base = E->getBase()->IgnoreParens();
  while (auto *MSPropSubscript = dyn_cast<MSPropertySubscriptExpr>(Base)) {
    CallArgs.insert(CallArgs.begin(), MSPropSubscript->getIdx());
    Base = MSPropSubscript->getBase()->IgnoreParens();
  }
  return cast<MSPropertyRefExpr>(Base);
}

Expr *MSPropertyOpBuilder::rebuildAndCaptureObject(Expr *syntacticBase) {
  InstanceBase = capture(RefExpr->getBaseExpr());
  for (Expr *&Arg : CallArgs)
    Arg = capture(Arg);
  syntacticBase = Rebuilder(S, [=](Expr *, unsigned Idx) -> Expr * {
                    switch (Idx) {
                    case 0:
                      return InstanceBase;
                    default:
                      assert(Idx <= CallArgs.size());
                      return CallArgs[Idx - 1];
                    }
                  }).rebuild(syntacticBase);

  return syntacticBase;
}

ExprResult MSPropertyOpBuilder::buildGet() {
  if (!RefExpr->getPropertyDecl()->hasGetter()) {
    S.Diag(RefExpr->getMemberLoc(), diag::err_no_accessor_for_property)
      << 0 /* getter */ << RefExpr->getPropertyDecl();
    return ExprError();
  }

  UnqualifiedId GetterName;
  const IdentifierInfo *II = RefExpr->getPropertyDecl()->getGetterId();
  GetterName.setIdentifier(II, RefExpr->getMemberLoc());
  CXXScopeSpec SS;
  SS.Adopt(RefExpr->getQualifierLoc());
  ExprResult GetterExpr =
      S.ActOnMemberAccessExpr(S.getCurScope(), InstanceBase, SourceLocation(),
                              RefExpr->isArrow() ? tok::arrow : tok::period, SS,
                              SourceLocation(), GetterName, nullptr);
  if (GetterExpr.isInvalid()) {
    S.Diag(RefExpr->getMemberLoc(),
           diag::err_cannot_find_suitable_accessor) << 0 /* getter */
      << RefExpr->getPropertyDecl();
    return ExprError();
  }

  return S.BuildCallExpr(S.getCurScope(), GetterExpr.get(),
                         RefExpr->getSourceRange().getBegin(), CallArgs,
                         RefExpr->getSourceRange().getEnd());
}

ExprResult MSPropertyOpBuilder::buildSet(Expr *op, SourceLocation sl,
                                         bool captureSetValueAsResult) {
  if (!RefExpr->getPropertyDecl()->hasSetter()) {
    S.Diag(RefExpr->getMemberLoc(), diag::err_no_accessor_for_property)
      << 1 /* setter */ << RefExpr->getPropertyDecl();
    return ExprError();
  }

  UnqualifiedId SetterName;
  const IdentifierInfo *II = RefExpr->getPropertyDecl()->getSetterId();
  SetterName.setIdentifier(II, RefExpr->getMemberLoc());
  CXXScopeSpec SS;
  SS.Adopt(RefExpr->getQualifierLoc());
  ExprResult SetterExpr =
      S.ActOnMemberAccessExpr(S.getCurScope(), InstanceBase, SourceLocation(),
                              RefExpr->isArrow() ? tok::arrow : tok::period, SS,
                              SourceLocation(), SetterName, nullptr);
  if (SetterExpr.isInvalid()) {
    S.Diag(RefExpr->getMemberLoc(),
           diag::err_cannot_find_suitable_accessor) << 1 /* setter */
      << RefExpr->getPropertyDecl();
    return ExprError();
  }

  SmallVector<Expr*, 4> ArgExprs;
  ArgExprs.append(CallArgs.begin(), CallArgs.end());
  ArgExprs.push_back(op);
  return S.BuildCallExpr(S.getCurScope(), SetterExpr.get(),
                         RefExpr->getSourceRange().getBegin(), ArgExprs,
                         op->getSourceRange().getEnd());
}

//===----------------------------------------------------------------------===//
//  General Sema routines.
//===----------------------------------------------------------------------===//

ExprResult SemaPseudoObject::checkRValue(Expr *E) {
  Expr *opaqueRef = E->IgnoreParens();
  if (ObjCPropertyRefExpr *refExpr
        = dyn_cast<ObjCPropertyRefExpr>(opaqueRef)) {
    ObjCPropertyOpBuilder builder(SemaRef, refExpr, true);
    return builder.buildRValueOperation(E);
  }
  else if (ObjCSubscriptRefExpr *refExpr
           = dyn_cast<ObjCSubscriptRefExpr>(opaqueRef)) {
    ObjCSubscriptOpBuilder builder(SemaRef, refExpr, true);
    return builder.buildRValueOperation(E);
  } else if (MSPropertyRefExpr *refExpr
             = dyn_cast<MSPropertyRefExpr>(opaqueRef)) {
    MSPropertyOpBuilder builder(SemaRef, refExpr, true);
    return builder.buildRValueOperation(E);
  } else if (MSPropertySubscriptExpr *RefExpr =
                 dyn_cast<MSPropertySubscriptExpr>(opaqueRef)) {
    MSPropertyOpBuilder Builder(SemaRef, RefExpr, true);
    return Builder.buildRValueOperation(E);
  } else {
    llvm_unreachable("unknown pseudo-object kind!");
  }
}

/// Check an increment or decrement of a pseudo-object expression.
ExprResult SemaPseudoObject::checkIncDec(Scope *Sc, SourceLocation opcLoc,
                                         UnaryOperatorKind opcode, Expr *op) {
  // Do nothing if the operand is dependent.
  if (op->isTypeDependent())
    return UnaryOperator::Create(
        SemaRef.Context, op, opcode, SemaRef.Context.DependentTy, VK_PRValue,
        OK_Ordinary, opcLoc, false, SemaRef.CurFPFeatureOverrides());

  assert(UnaryOperator::isIncrementDecrementOp(opcode));
  Expr *opaqueRef = op->IgnoreParens();
  if (ObjCPropertyRefExpr *refExpr
        = dyn_cast<ObjCPropertyRefExpr>(opaqueRef)) {
    ObjCPropertyOpBuilder builder(SemaRef, refExpr, false);
    return builder.buildIncDecOperation(Sc, opcLoc, opcode, op);
  } else if (isa<ObjCSubscriptRefExpr>(opaqueRef)) {
    Diag(opcLoc, diag::err_illegal_container_subscripting_op);
    return ExprError();
  } else if (MSPropertyRefExpr *refExpr
             = dyn_cast<MSPropertyRefExpr>(opaqueRef)) {
    MSPropertyOpBuilder builder(SemaRef, refExpr, false);
    return builder.buildIncDecOperation(Sc, opcLoc, opcode, op);
  } else if (MSPropertySubscriptExpr *RefExpr
             = dyn_cast<MSPropertySubscriptExpr>(opaqueRef)) {
    MSPropertyOpBuilder Builder(SemaRef, RefExpr, false);
    return Builder.buildIncDecOperation(Sc, opcLoc, opcode, op);
  } else {
    llvm_unreachable("unknown pseudo-object kind!");
  }
}

ExprResult SemaPseudoObject::checkAssignment(Scope *S, SourceLocation opcLoc,
                                             BinaryOperatorKind opcode,
                                             Expr *LHS, Expr *RHS) {
  // Do nothing if either argument is dependent.
  if (LHS->isTypeDependent() || RHS->isTypeDependent())
    return BinaryOperator::Create(
        SemaRef.Context, LHS, RHS, opcode, SemaRef.Context.DependentTy,
        VK_PRValue, OK_Ordinary, opcLoc, SemaRef.CurFPFeatureOverrides());

  // Filter out non-overload placeholder types in the RHS.
  if (RHS->getType()->isNonOverloadPlaceholderType()) {
    ExprResult result = SemaRef.CheckPlaceholderExpr(RHS);
    if (result.isInvalid()) return ExprError();
    RHS = result.get();
  }

  bool IsSimpleAssign = opcode == BO_Assign;
  Expr *opaqueRef = LHS->IgnoreParens();
  if (ObjCPropertyRefExpr *refExpr
        = dyn_cast<ObjCPropertyRefExpr>(opaqueRef)) {
    ObjCPropertyOpBuilder builder(SemaRef, refExpr, IsSimpleAssign);
    return builder.buildAssignmentOperation(S, opcLoc, opcode, LHS, RHS);
  } else if (ObjCSubscriptRefExpr *refExpr
             = dyn_cast<ObjCSubscriptRefExpr>(opaqueRef)) {
    ObjCSubscriptOpBuilder builder(SemaRef, refExpr, IsSimpleAssign);
    return builder.buildAssignmentOperation(S, opcLoc, opcode, LHS, RHS);
  } else if (MSPropertyRefExpr *refExpr
             = dyn_cast<MSPropertyRefExpr>(opaqueRef)) {
    MSPropertyOpBuilder builder(SemaRef, refExpr, IsSimpleAssign);
    return builder.buildAssignmentOperation(S, opcLoc, opcode, LHS, RHS);
  } else if (MSPropertySubscriptExpr *RefExpr
             = dyn_cast<MSPropertySubscriptExpr>(opaqueRef)) {
    MSPropertyOpBuilder Builder(SemaRef, RefExpr, IsSimpleAssign);
    return Builder.buildAssignmentOperation(S, opcLoc, opcode, LHS, RHS);
  } else {
    llvm_unreachable("unknown pseudo-object kind!");
  }
}

/// Given a pseudo-object reference, rebuild it without the opaque
/// values.  Basically, undo the behavior of rebuildAndCaptureObject.
/// This should never operate in-place.
static Expr *stripOpaqueValuesFromPseudoObjectRef(Sema &S, Expr *E) {
  return Rebuilder(S,
                   [=](Expr *E, unsigned) -> Expr * {
                     return cast<OpaqueValueExpr>(E)->getSourceExpr();
                   })
      .rebuild(E);
}

/// Given a pseudo-object expression, recreate what it looks like
/// syntactically without the attendant OpaqueValueExprs.
///
/// This is a hack which should be removed when TreeTransform is
/// capable of rebuilding a tree without stripping implicit
/// operations.
Expr *SemaPseudoObject::recreateSyntacticForm(PseudoObjectExpr *E) {
  Expr *syntax = E->getSyntacticForm();
  if (UnaryOperator *uop = dyn_cast<UnaryOperator>(syntax)) {
    Expr *op = stripOpaqueValuesFromPseudoObjectRef(SemaRef, uop->getSubExpr());
    return UnaryOperator::Create(
        SemaRef.Context, op, uop->getOpcode(), uop->getType(),
        uop->getValueKind(), uop->getObjectKind(), uop->getOperatorLoc(),
        uop->canOverflow(), SemaRef.CurFPFeatureOverrides());
  } else if (CompoundAssignOperator *cop
               = dyn_cast<CompoundAssignOperator>(syntax)) {
    Expr *lhs = stripOpaqueValuesFromPseudoObjectRef(SemaRef, cop->getLHS());
    Expr *rhs = cast<OpaqueValueExpr>(cop->getRHS())->getSourceExpr();
    return CompoundAssignOperator::Create(
        SemaRef.Context, lhs, rhs, cop->getOpcode(), cop->getType(),
        cop->getValueKind(), cop->getObjectKind(), cop->getOperatorLoc(),
        SemaRef.CurFPFeatureOverrides(), cop->getComputationLHSType(),
        cop->getComputationResultType());

  } else if (BinaryOperator *bop = dyn_cast<BinaryOperator>(syntax)) {
    Expr *lhs = stripOpaqueValuesFromPseudoObjectRef(SemaRef, bop->getLHS());
    Expr *rhs = cast<OpaqueValueExpr>(bop->getRHS())->getSourceExpr();
    return BinaryOperator::Create(SemaRef.Context, lhs, rhs, bop->getOpcode(),
                                  bop->getType(), bop->getValueKind(),
                                  bop->getObjectKind(), bop->getOperatorLoc(),
                                  SemaRef.CurFPFeatureOverrides());

  } else if (isa<CallExpr>(syntax)) {
    return syntax;
  } else {
    assert(syntax->hasPlaceholderType(BuiltinType::PseudoObject));
    return stripOpaqueValuesFromPseudoObjectRef(SemaRef, syntax);
  }
}

SemaPseudoObject::SemaPseudoObject(Sema &S) : SemaBase(S) {}
