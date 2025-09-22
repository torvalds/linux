//===--- CheckExprLifetime.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CheckExprLifetime.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/PointerIntPair.h"

namespace clang::sema {
namespace {
enum LifetimeKind {
  /// The lifetime of a temporary bound to this entity ends at the end of the
  /// full-expression, and that's (probably) fine.
  LK_FullExpression,

  /// The lifetime of a temporary bound to this entity is extended to the
  /// lifeitme of the entity itself.
  LK_Extended,

  /// The lifetime of a temporary bound to this entity probably ends too soon,
  /// because the entity is allocated in a new-expression.
  LK_New,

  /// The lifetime of a temporary bound to this entity ends too soon, because
  /// the entity is a return object.
  LK_Return,

  /// The lifetime of a temporary bound to this entity ends too soon, because
  /// the entity is the result of a statement expression.
  LK_StmtExprResult,

  /// This is a mem-initializer: if it would extend a temporary (other than via
  /// a default member initializer), the program is ill-formed.
  LK_MemInitializer,

  /// The lifetime of a temporary bound to this entity probably ends too soon,
  /// because the entity is a pointer and we assign the address of a temporary
  /// object to it.
  LK_Assignment,
};
using LifetimeResult =
    llvm::PointerIntPair<const InitializedEntity *, 3, LifetimeKind>;
} // namespace

/// Determine the declaration which an initialized entity ultimately refers to,
/// for the purpose of lifetime-extending a temporary bound to a reference in
/// the initialization of \p Entity.
static LifetimeResult
getEntityLifetime(const InitializedEntity *Entity,
                  const InitializedEntity *InitField = nullptr) {
  // C++11 [class.temporary]p5:
  switch (Entity->getKind()) {
  case InitializedEntity::EK_Variable:
    //   The temporary [...] persists for the lifetime of the reference
    return {Entity, LK_Extended};

  case InitializedEntity::EK_Member:
    // For subobjects, we look at the complete object.
    if (Entity->getParent())
      return getEntityLifetime(Entity->getParent(), Entity);

    //   except:
    // C++17 [class.base.init]p8:
    //   A temporary expression bound to a reference member in a
    //   mem-initializer is ill-formed.
    // C++17 [class.base.init]p11:
    //   A temporary expression bound to a reference member from a
    //   default member initializer is ill-formed.
    //
    // The context of p11 and its example suggest that it's only the use of a
    // default member initializer from a constructor that makes the program
    // ill-formed, not its mere existence, and that it can even be used by
    // aggregate initialization.
    return {Entity, Entity->isDefaultMemberInitializer() ? LK_Extended
                                                         : LK_MemInitializer};

  case InitializedEntity::EK_Binding:
    // Per [dcl.decomp]p3, the binding is treated as a variable of reference
    // type.
    return {Entity, LK_Extended};

  case InitializedEntity::EK_Parameter:
  case InitializedEntity::EK_Parameter_CF_Audited:
    //   -- A temporary bound to a reference parameter in a function call
    //      persists until the completion of the full-expression containing
    //      the call.
    return {nullptr, LK_FullExpression};

  case InitializedEntity::EK_TemplateParameter:
    // FIXME: This will always be ill-formed; should we eagerly diagnose it
    // here?
    return {nullptr, LK_FullExpression};

  case InitializedEntity::EK_Result:
    //   -- The lifetime of a temporary bound to the returned value in a
    //      function return statement is not extended; the temporary is
    //      destroyed at the end of the full-expression in the return statement.
    return {nullptr, LK_Return};

  case InitializedEntity::EK_StmtExprResult:
    // FIXME: Should we lifetime-extend through the result of a statement
    // expression?
    return {nullptr, LK_StmtExprResult};

  case InitializedEntity::EK_New:
    //   -- A temporary bound to a reference in a new-initializer persists
    //      until the completion of the full-expression containing the
    //      new-initializer.
    return {nullptr, LK_New};

  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_CompoundLiteralInit:
  case InitializedEntity::EK_RelatedResult:
    // We don't yet know the storage duration of the surrounding temporary.
    // Assume it's got full-expression duration for now, it will patch up our
    // storage duration if that's not correct.
    return {nullptr, LK_FullExpression};

  case InitializedEntity::EK_ArrayElement:
    // For subobjects, we look at the complete object.
    return getEntityLifetime(Entity->getParent(), InitField);

  case InitializedEntity::EK_Base:
    // For subobjects, we look at the complete object.
    if (Entity->getParent())
      return getEntityLifetime(Entity->getParent(), InitField);
    return {InitField, LK_MemInitializer};

  case InitializedEntity::EK_Delegating:
    // We can reach this case for aggregate initialization in a constructor:
    //   struct A { int &&r; };
    //   struct B : A { B() : A{0} {} };
    // In this case, use the outermost field decl as the context.
    return {InitField, LK_MemInitializer};

  case InitializedEntity::EK_BlockElement:
  case InitializedEntity::EK_LambdaToBlockConversionBlockElement:
  case InitializedEntity::EK_LambdaCapture:
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
    return {nullptr, LK_FullExpression};

  case InitializedEntity::EK_Exception:
    // FIXME: Can we diagnose lifetime problems with exceptions?
    return {nullptr, LK_FullExpression};

  case InitializedEntity::EK_ParenAggInitMember:
    //   -- A temporary object bound to a reference element of an aggregate of
    //      class type initialized from a parenthesized expression-list
    //      [dcl.init, 9.3] persists until the completion of the full-expression
    //      containing the expression-list.
    return {nullptr, LK_FullExpression};
  }

  llvm_unreachable("unknown entity kind");
}

namespace {
enum ReferenceKind {
  /// Lifetime would be extended by a reference binding to a temporary.
  RK_ReferenceBinding,
  /// Lifetime would be extended by a std::initializer_list object binding to
  /// its backing array.
  RK_StdInitializerList,
};

/// A temporary or local variable. This will be one of:
///  * A MaterializeTemporaryExpr.
///  * A DeclRefExpr whose declaration is a local.
///  * An AddrLabelExpr.
///  * A BlockExpr for a block with captures.
using Local = Expr *;

/// Expressions we stepped over when looking for the local state. Any steps
/// that would inhibit lifetime extension or take us out of subexpressions of
/// the initializer are included.
struct IndirectLocalPathEntry {
  enum EntryKind {
    DefaultInit,
    AddressOf,
    VarInit,
    LValToRVal,
    LifetimeBoundCall,
    TemporaryCopy,
    LambdaCaptureInit,
    GslReferenceInit,
    GslPointerInit,
    GslPointerAssignment,
  } Kind;
  Expr *E;
  union {
    const Decl *D = nullptr;
    const LambdaCapture *Capture;
  };
  IndirectLocalPathEntry() {}
  IndirectLocalPathEntry(EntryKind K, Expr *E) : Kind(K), E(E) {}
  IndirectLocalPathEntry(EntryKind K, Expr *E, const Decl *D)
      : Kind(K), E(E), D(D) {}
  IndirectLocalPathEntry(EntryKind K, Expr *E, const LambdaCapture *Capture)
      : Kind(K), E(E), Capture(Capture) {}
};

using IndirectLocalPath = llvm::SmallVectorImpl<IndirectLocalPathEntry>;

struct RevertToOldSizeRAII {
  IndirectLocalPath &Path;
  unsigned OldSize = Path.size();
  RevertToOldSizeRAII(IndirectLocalPath &Path) : Path(Path) {}
  ~RevertToOldSizeRAII() { Path.resize(OldSize); }
};

using LocalVisitor = llvm::function_ref<bool(IndirectLocalPath &Path, Local L,
                                             ReferenceKind RK)>;
} // namespace

static bool isVarOnPath(IndirectLocalPath &Path, VarDecl *VD) {
  for (auto E : Path)
    if (E.Kind == IndirectLocalPathEntry::VarInit && E.D == VD)
      return true;
  return false;
}

static bool pathContainsInit(IndirectLocalPath &Path) {
  return llvm::any_of(Path, [=](IndirectLocalPathEntry E) {
    return E.Kind == IndirectLocalPathEntry::DefaultInit ||
           E.Kind == IndirectLocalPathEntry::VarInit;
  });
}

static void visitLocalsRetainedByInitializer(IndirectLocalPath &Path,
                                             Expr *Init, LocalVisitor Visit,
                                             bool RevisitSubinits,
                                             bool EnableLifetimeWarnings);

static void visitLocalsRetainedByReferenceBinding(IndirectLocalPath &Path,
                                                  Expr *Init, ReferenceKind RK,
                                                  LocalVisitor Visit,
                                                  bool EnableLifetimeWarnings);

template <typename T> static bool isRecordWithAttr(QualType Type) {
  if (auto *RD = Type->getAsCXXRecordDecl())
    return RD->hasAttr<T>();
  return false;
}

// Decl::isInStdNamespace will return false for iterators in some STL
// implementations due to them being defined in a namespace outside of the std
// namespace.
static bool isInStlNamespace(const Decl *D) {
  const DeclContext *DC = D->getDeclContext();
  if (!DC)
    return false;
  if (const auto *ND = dyn_cast<NamespaceDecl>(DC))
    if (const IdentifierInfo *II = ND->getIdentifier()) {
      StringRef Name = II->getName();
      if (Name.size() >= 2 && Name.front() == '_' &&
          (Name[1] == '_' || isUppercase(Name[1])))
        return true;
    }

  return DC->isStdNamespace();
}

static bool shouldTrackImplicitObjectArg(const CXXMethodDecl *Callee) {
  if (auto *Conv = dyn_cast_or_null<CXXConversionDecl>(Callee))
    if (isRecordWithAttr<PointerAttr>(Conv->getConversionType()))
      return true;
  if (!isInStlNamespace(Callee->getParent()))
    return false;
  if (!isRecordWithAttr<PointerAttr>(
          Callee->getFunctionObjectParameterType()) &&
      !isRecordWithAttr<OwnerAttr>(Callee->getFunctionObjectParameterType()))
    return false;
  if (Callee->getReturnType()->isPointerType() ||
      isRecordWithAttr<PointerAttr>(Callee->getReturnType())) {
    if (!Callee->getIdentifier())
      return false;
    return llvm::StringSwitch<bool>(Callee->getName())
        .Cases("begin", "rbegin", "cbegin", "crbegin", true)
        .Cases("end", "rend", "cend", "crend", true)
        .Cases("c_str", "data", "get", true)
        // Map and set types.
        .Cases("find", "equal_range", "lower_bound", "upper_bound", true)
        .Default(false);
  } else if (Callee->getReturnType()->isReferenceType()) {
    if (!Callee->getIdentifier()) {
      auto OO = Callee->getOverloadedOperator();
      return OO == OverloadedOperatorKind::OO_Subscript ||
             OO == OverloadedOperatorKind::OO_Star;
    }
    return llvm::StringSwitch<bool>(Callee->getName())
        .Cases("front", "back", "at", "top", "value", true)
        .Default(false);
  }
  return false;
}

static bool shouldTrackFirstArgument(const FunctionDecl *FD) {
  if (!FD->getIdentifier() || FD->getNumParams() != 1)
    return false;
  const auto *RD = FD->getParamDecl(0)->getType()->getPointeeCXXRecordDecl();
  if (!FD->isInStdNamespace() || !RD || !RD->isInStdNamespace())
    return false;
  if (!RD->hasAttr<PointerAttr>() && !RD->hasAttr<OwnerAttr>())
    return false;
  if (FD->getReturnType()->isPointerType() ||
      isRecordWithAttr<PointerAttr>(FD->getReturnType())) {
    return llvm::StringSwitch<bool>(FD->getName())
        .Cases("begin", "rbegin", "cbegin", "crbegin", true)
        .Cases("end", "rend", "cend", "crend", true)
        .Case("data", true)
        .Default(false);
  } else if (FD->getReturnType()->isReferenceType()) {
    return llvm::StringSwitch<bool>(FD->getName())
        .Cases("get", "any_cast", true)
        .Default(false);
  }
  return false;
}

static void handleGslAnnotatedTypes(IndirectLocalPath &Path, Expr *Call,
                                    LocalVisitor Visit) {
  auto VisitPointerArg = [&](const Decl *D, Expr *Arg, bool Value) {
    // We are not interested in the temporary base objects of gsl Pointers:
    //   Temp().ptr; // Here ptr might not dangle.
    if (isa<MemberExpr>(Arg->IgnoreImpCasts()))
      return;
    // Once we initialized a value with a reference, it can no longer dangle.
    if (!Value) {
      for (const IndirectLocalPathEntry &PE : llvm::reverse(Path)) {
        if (PE.Kind == IndirectLocalPathEntry::GslReferenceInit)
          continue;
        if (PE.Kind == IndirectLocalPathEntry::GslPointerInit ||
            PE.Kind == IndirectLocalPathEntry::GslPointerAssignment)
          return;
        break;
      }
    }
    Path.push_back({Value ? IndirectLocalPathEntry::GslPointerInit
                          : IndirectLocalPathEntry::GslReferenceInit,
                    Arg, D});
    if (Arg->isGLValue())
      visitLocalsRetainedByReferenceBinding(Path, Arg, RK_ReferenceBinding,
                                            Visit,
                                            /*EnableLifetimeWarnings=*/true);
    else
      visitLocalsRetainedByInitializer(Path, Arg, Visit, true,
                                       /*EnableLifetimeWarnings=*/true);
    Path.pop_back();
  };

  if (auto *MCE = dyn_cast<CXXMemberCallExpr>(Call)) {
    const auto *MD = cast_or_null<CXXMethodDecl>(MCE->getDirectCallee());
    if (MD && shouldTrackImplicitObjectArg(MD))
      VisitPointerArg(MD, MCE->getImplicitObjectArgument(),
                      !MD->getReturnType()->isReferenceType());
    return;
  } else if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(Call)) {
    FunctionDecl *Callee = OCE->getDirectCallee();
    if (Callee && Callee->isCXXInstanceMember() &&
        shouldTrackImplicitObjectArg(cast<CXXMethodDecl>(Callee)))
      VisitPointerArg(Callee, OCE->getArg(0),
                      !Callee->getReturnType()->isReferenceType());
    return;
  } else if (auto *CE = dyn_cast<CallExpr>(Call)) {
    FunctionDecl *Callee = CE->getDirectCallee();
    if (Callee && shouldTrackFirstArgument(Callee))
      VisitPointerArg(Callee, CE->getArg(0),
                      !Callee->getReturnType()->isReferenceType());
    return;
  }

  if (auto *CCE = dyn_cast<CXXConstructExpr>(Call)) {
    const auto *Ctor = CCE->getConstructor();
    const CXXRecordDecl *RD = Ctor->getParent();
    if (CCE->getNumArgs() > 0 && RD->hasAttr<PointerAttr>())
      VisitPointerArg(Ctor->getParamDecl(0), CCE->getArgs()[0], true);
  }
}

static bool implicitObjectParamIsLifetimeBound(const FunctionDecl *FD) {
  const TypeSourceInfo *TSI = FD->getTypeSourceInfo();
  if (!TSI)
    return false;
  // Don't declare this variable in the second operand of the for-statement;
  // GCC miscompiles that by ending its lifetime before evaluating the
  // third operand. See gcc.gnu.org/PR86769.
  AttributedTypeLoc ATL;
  for (TypeLoc TL = TSI->getTypeLoc();
       (ATL = TL.getAsAdjusted<AttributedTypeLoc>());
       TL = ATL.getModifiedLoc()) {
    if (ATL.getAttrAs<LifetimeBoundAttr>())
      return true;
  }

  // Assume that all assignment operators with a "normal" return type return
  // *this, that is, an lvalue reference that is the same type as the implicit
  // object parameter (or the LHS for a non-member operator$=).
  OverloadedOperatorKind OO = FD->getDeclName().getCXXOverloadedOperator();
  if (OO == OO_Equal || isCompoundAssignmentOperator(OO)) {
    QualType RetT = FD->getReturnType();
    if (RetT->isLValueReferenceType()) {
      ASTContext &Ctx = FD->getASTContext();
      QualType LHST;
      auto *MD = dyn_cast<CXXMethodDecl>(FD);
      if (MD && MD->isCXXInstanceMember())
        LHST = Ctx.getLValueReferenceType(MD->getFunctionObjectParameterType());
      else
        LHST = MD->getParamDecl(0)->getType();
      if (Ctx.hasSameType(RetT, LHST))
        return true;
    }
  }

  return false;
}

static void visitLifetimeBoundArguments(IndirectLocalPath &Path, Expr *Call,
                                        LocalVisitor Visit) {
  const FunctionDecl *Callee;
  ArrayRef<Expr *> Args;

  if (auto *CE = dyn_cast<CallExpr>(Call)) {
    Callee = CE->getDirectCallee();
    Args = llvm::ArrayRef(CE->getArgs(), CE->getNumArgs());
  } else {
    auto *CCE = cast<CXXConstructExpr>(Call);
    Callee = CCE->getConstructor();
    Args = llvm::ArrayRef(CCE->getArgs(), CCE->getNumArgs());
  }
  if (!Callee)
    return;

  Expr *ObjectArg = nullptr;
  if (isa<CXXOperatorCallExpr>(Call) && Callee->isCXXInstanceMember()) {
    ObjectArg = Args[0];
    Args = Args.slice(1);
  } else if (auto *MCE = dyn_cast<CXXMemberCallExpr>(Call)) {
    ObjectArg = MCE->getImplicitObjectArgument();
  }

  auto VisitLifetimeBoundArg = [&](const Decl *D, Expr *Arg) {
    Path.push_back({IndirectLocalPathEntry::LifetimeBoundCall, Arg, D});
    if (Arg->isGLValue())
      visitLocalsRetainedByReferenceBinding(Path, Arg, RK_ReferenceBinding,
                                            Visit,
                                            /*EnableLifetimeWarnings=*/false);
    else
      visitLocalsRetainedByInitializer(Path, Arg, Visit, true,
                                       /*EnableLifetimeWarnings=*/false);
    Path.pop_back();
  };

  bool CheckCoroCall = false;
  if (const auto *RD = Callee->getReturnType()->getAsRecordDecl()) {
    CheckCoroCall = RD->hasAttr<CoroLifetimeBoundAttr>() &&
                    RD->hasAttr<CoroReturnTypeAttr>() &&
                    !Callee->hasAttr<CoroDisableLifetimeBoundAttr>();
  }

  if (ObjectArg) {
    bool CheckCoroObjArg = CheckCoroCall;
    // Coroutine lambda objects with empty capture list are not lifetimebound.
    if (auto *LE = dyn_cast<LambdaExpr>(ObjectArg->IgnoreImplicit());
        LE && LE->captures().empty())
      CheckCoroObjArg = false;
    // Allow `get_return_object()` as the object param (__promise) is not
    // lifetimebound.
    if (Sema::CanBeGetReturnObject(Callee))
      CheckCoroObjArg = false;
    if (implicitObjectParamIsLifetimeBound(Callee) || CheckCoroObjArg)
      VisitLifetimeBoundArg(Callee, ObjectArg);
  }

  for (unsigned I = 0,
                N = std::min<unsigned>(Callee->getNumParams(), Args.size());
       I != N; ++I) {
    if (CheckCoroCall || Callee->getParamDecl(I)->hasAttr<LifetimeBoundAttr>())
      VisitLifetimeBoundArg(Callee->getParamDecl(I), Args[I]);
  }
}

/// Visit the locals that would be reachable through a reference bound to the
/// glvalue expression \c Init.
static void visitLocalsRetainedByReferenceBinding(IndirectLocalPath &Path,
                                                  Expr *Init, ReferenceKind RK,
                                                  LocalVisitor Visit,
                                                  bool EnableLifetimeWarnings) {
  RevertToOldSizeRAII RAII(Path);

  // Walk past any constructs which we can lifetime-extend across.
  Expr *Old;
  do {
    Old = Init;

    if (auto *FE = dyn_cast<FullExpr>(Init))
      Init = FE->getSubExpr();

    if (InitListExpr *ILE = dyn_cast<InitListExpr>(Init)) {
      // If this is just redundant braces around an initializer, step over it.
      if (ILE->isTransparent())
        Init = ILE->getInit(0);
    }

    // Step over any subobject adjustments; we may have a materialized
    // temporary inside them.
    Init = const_cast<Expr *>(Init->skipRValueSubobjectAdjustments());

    // Per current approach for DR1376, look through casts to reference type
    // when performing lifetime extension.
    if (CastExpr *CE = dyn_cast<CastExpr>(Init))
      if (CE->getSubExpr()->isGLValue())
        Init = CE->getSubExpr();

    // Per the current approach for DR1299, look through array element access
    // on array glvalues when performing lifetime extension.
    if (auto *ASE = dyn_cast<ArraySubscriptExpr>(Init)) {
      Init = ASE->getBase();
      auto *ICE = dyn_cast<ImplicitCastExpr>(Init);
      if (ICE && ICE->getCastKind() == CK_ArrayToPointerDecay)
        Init = ICE->getSubExpr();
      else
        // We can't lifetime extend through this but we might still find some
        // retained temporaries.
        return visitLocalsRetainedByInitializer(Path, Init, Visit, true,
                                                EnableLifetimeWarnings);
    }

    // Step into CXXDefaultInitExprs so we can diagnose cases where a
    // constructor inherits one as an implicit mem-initializer.
    if (auto *DIE = dyn_cast<CXXDefaultInitExpr>(Init)) {
      Path.push_back(
          {IndirectLocalPathEntry::DefaultInit, DIE, DIE->getField()});
      Init = DIE->getExpr();
    }
  } while (Init != Old);

  if (auto *MTE = dyn_cast<MaterializeTemporaryExpr>(Init)) {
    if (Visit(Path, Local(MTE), RK))
      visitLocalsRetainedByInitializer(Path, MTE->getSubExpr(), Visit, true,
                                       EnableLifetimeWarnings);
  }

  if (auto *M = dyn_cast<MemberExpr>(Init)) {
    // Lifetime of a non-reference type field is same as base object.
    if (auto *F = dyn_cast<FieldDecl>(M->getMemberDecl());
        F && !F->getType()->isReferenceType())
      visitLocalsRetainedByInitializer(Path, M->getBase(), Visit, true,
                                       EnableLifetimeWarnings);
  }

  if (isa<CallExpr>(Init)) {
    if (EnableLifetimeWarnings)
      handleGslAnnotatedTypes(Path, Init, Visit);
    return visitLifetimeBoundArguments(Path, Init, Visit);
  }

  switch (Init->getStmtClass()) {
  case Stmt::DeclRefExprClass: {
    // If we find the name of a local non-reference parameter, we could have a
    // lifetime problem.
    auto *DRE = cast<DeclRefExpr>(Init);
    auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (VD && VD->hasLocalStorage() &&
        !DRE->refersToEnclosingVariableOrCapture()) {
      if (!VD->getType()->isReferenceType()) {
        Visit(Path, Local(DRE), RK);
      } else if (isa<ParmVarDecl>(DRE->getDecl())) {
        // The lifetime of a reference parameter is unknown; assume it's OK
        // for now.
        break;
      } else if (VD->getInit() && !isVarOnPath(Path, VD)) {
        Path.push_back({IndirectLocalPathEntry::VarInit, DRE, VD});
        visitLocalsRetainedByReferenceBinding(Path, VD->getInit(),
                                              RK_ReferenceBinding, Visit,
                                              EnableLifetimeWarnings);
      }
    }
    break;
  }

  case Stmt::UnaryOperatorClass: {
    // The only unary operator that make sense to handle here
    // is Deref.  All others don't resolve to a "name."  This includes
    // handling all sorts of rvalues passed to a unary operator.
    const UnaryOperator *U = cast<UnaryOperator>(Init);
    if (U->getOpcode() == UO_Deref)
      visitLocalsRetainedByInitializer(Path, U->getSubExpr(), Visit, true,
                                       EnableLifetimeWarnings);
    break;
  }

  case Stmt::ArraySectionExprClass: {
    visitLocalsRetainedByInitializer(Path,
                                     cast<ArraySectionExpr>(Init)->getBase(),
                                     Visit, true, EnableLifetimeWarnings);
    break;
  }

  case Stmt::ConditionalOperatorClass:
  case Stmt::BinaryConditionalOperatorClass: {
    auto *C = cast<AbstractConditionalOperator>(Init);
    if (!C->getTrueExpr()->getType()->isVoidType())
      visitLocalsRetainedByReferenceBinding(Path, C->getTrueExpr(), RK, Visit,
                                            EnableLifetimeWarnings);
    if (!C->getFalseExpr()->getType()->isVoidType())
      visitLocalsRetainedByReferenceBinding(Path, C->getFalseExpr(), RK, Visit,
                                            EnableLifetimeWarnings);
    break;
  }

  case Stmt::CompoundLiteralExprClass: {
    if (auto *CLE = dyn_cast<CompoundLiteralExpr>(Init)) {
      if (!CLE->isFileScope())
        Visit(Path, Local(CLE), RK);
    }
    break;
  }

    // FIXME: Visit the left-hand side of an -> or ->*.

  default:
    break;
  }
}

/// Visit the locals that would be reachable through an object initialized by
/// the prvalue expression \c Init.
static void visitLocalsRetainedByInitializer(IndirectLocalPath &Path,
                                             Expr *Init, LocalVisitor Visit,
                                             bool RevisitSubinits,
                                             bool EnableLifetimeWarnings) {
  RevertToOldSizeRAII RAII(Path);

  Expr *Old;
  do {
    Old = Init;

    // Step into CXXDefaultInitExprs so we can diagnose cases where a
    // constructor inherits one as an implicit mem-initializer.
    if (auto *DIE = dyn_cast<CXXDefaultInitExpr>(Init)) {
      Path.push_back(
          {IndirectLocalPathEntry::DefaultInit, DIE, DIE->getField()});
      Init = DIE->getExpr();
    }

    if (auto *FE = dyn_cast<FullExpr>(Init))
      Init = FE->getSubExpr();

    // Dig out the expression which constructs the extended temporary.
    Init = const_cast<Expr *>(Init->skipRValueSubobjectAdjustments());

    if (CXXBindTemporaryExpr *BTE = dyn_cast<CXXBindTemporaryExpr>(Init))
      Init = BTE->getSubExpr();

    Init = Init->IgnoreParens();

    // Step over value-preserving rvalue casts.
    if (auto *CE = dyn_cast<CastExpr>(Init)) {
      switch (CE->getCastKind()) {
      case CK_LValueToRValue:
        // If we can match the lvalue to a const object, we can look at its
        // initializer.
        Path.push_back({IndirectLocalPathEntry::LValToRVal, CE});
        return visitLocalsRetainedByReferenceBinding(
            Path, Init, RK_ReferenceBinding,
            [&](IndirectLocalPath &Path, Local L, ReferenceKind RK) -> bool {
              if (auto *DRE = dyn_cast<DeclRefExpr>(L)) {
                auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
                if (VD && VD->getType().isConstQualified() && VD->getInit() &&
                    !isVarOnPath(Path, VD)) {
                  Path.push_back({IndirectLocalPathEntry::VarInit, DRE, VD});
                  visitLocalsRetainedByInitializer(
                      Path, VD->getInit(), Visit, true, EnableLifetimeWarnings);
                }
              } else if (auto *MTE = dyn_cast<MaterializeTemporaryExpr>(L)) {
                if (MTE->getType().isConstQualified())
                  visitLocalsRetainedByInitializer(Path, MTE->getSubExpr(),
                                                   Visit, true,
                                                   EnableLifetimeWarnings);
              }
              return false;
            },
            EnableLifetimeWarnings);

        // We assume that objects can be retained by pointers cast to integers,
        // but not if the integer is cast to floating-point type or to _Complex.
        // We assume that casts to 'bool' do not preserve enough information to
        // retain a local object.
      case CK_NoOp:
      case CK_BitCast:
      case CK_BaseToDerived:
      case CK_DerivedToBase:
      case CK_UncheckedDerivedToBase:
      case CK_Dynamic:
      case CK_ToUnion:
      case CK_UserDefinedConversion:
      case CK_ConstructorConversion:
      case CK_IntegralToPointer:
      case CK_PointerToIntegral:
      case CK_VectorSplat:
      case CK_IntegralCast:
      case CK_CPointerToObjCPointerCast:
      case CK_BlockPointerToObjCPointerCast:
      case CK_AnyPointerToBlockPointerCast:
      case CK_AddressSpaceConversion:
        break;

      case CK_ArrayToPointerDecay:
        // Model array-to-pointer decay as taking the address of the array
        // lvalue.
        Path.push_back({IndirectLocalPathEntry::AddressOf, CE});
        return visitLocalsRetainedByReferenceBinding(Path, CE->getSubExpr(),
                                                     RK_ReferenceBinding, Visit,
                                                     EnableLifetimeWarnings);

      default:
        return;
      }

      Init = CE->getSubExpr();
    }
  } while (Old != Init);

  // C++17 [dcl.init.list]p6:
  //   initializing an initializer_list object from the array extends the
  //   lifetime of the array exactly like binding a reference to a temporary.
  if (auto *ILE = dyn_cast<CXXStdInitializerListExpr>(Init))
    return visitLocalsRetainedByReferenceBinding(Path, ILE->getSubExpr(),
                                                 RK_StdInitializerList, Visit,
                                                 EnableLifetimeWarnings);

  if (InitListExpr *ILE = dyn_cast<InitListExpr>(Init)) {
    // We already visited the elements of this initializer list while
    // performing the initialization. Don't visit them again unless we've
    // changed the lifetime of the initialized entity.
    if (!RevisitSubinits)
      return;

    if (ILE->isTransparent())
      return visitLocalsRetainedByInitializer(Path, ILE->getInit(0), Visit,
                                              RevisitSubinits,
                                              EnableLifetimeWarnings);

    if (ILE->getType()->isArrayType()) {
      for (unsigned I = 0, N = ILE->getNumInits(); I != N; ++I)
        visitLocalsRetainedByInitializer(Path, ILE->getInit(I), Visit,
                                         RevisitSubinits,
                                         EnableLifetimeWarnings);
      return;
    }

    if (CXXRecordDecl *RD = ILE->getType()->getAsCXXRecordDecl()) {
      assert(RD->isAggregate() && "aggregate init on non-aggregate");

      // If we lifetime-extend a braced initializer which is initializing an
      // aggregate, and that aggregate contains reference members which are
      // bound to temporaries, those temporaries are also lifetime-extended.
      if (RD->isUnion() && ILE->getInitializedFieldInUnion() &&
          ILE->getInitializedFieldInUnion()->getType()->isReferenceType())
        visitLocalsRetainedByReferenceBinding(Path, ILE->getInit(0),
                                              RK_ReferenceBinding, Visit,
                                              EnableLifetimeWarnings);
      else {
        unsigned Index = 0;
        for (; Index < RD->getNumBases() && Index < ILE->getNumInits(); ++Index)
          visitLocalsRetainedByInitializer(Path, ILE->getInit(Index), Visit,
                                           RevisitSubinits,
                                           EnableLifetimeWarnings);
        for (const auto *I : RD->fields()) {
          if (Index >= ILE->getNumInits())
            break;
          if (I->isUnnamedBitField())
            continue;
          Expr *SubInit = ILE->getInit(Index);
          if (I->getType()->isReferenceType())
            visitLocalsRetainedByReferenceBinding(Path, SubInit,
                                                  RK_ReferenceBinding, Visit,
                                                  EnableLifetimeWarnings);
          else
            // This might be either aggregate-initialization of a member or
            // initialization of a std::initializer_list object. Regardless,
            // we should recursively lifetime-extend that initializer.
            visitLocalsRetainedByInitializer(
                Path, SubInit, Visit, RevisitSubinits, EnableLifetimeWarnings);
          ++Index;
        }
      }
    }
    return;
  }

  // The lifetime of an init-capture is that of the closure object constructed
  // by a lambda-expression.
  if (auto *LE = dyn_cast<LambdaExpr>(Init)) {
    LambdaExpr::capture_iterator CapI = LE->capture_begin();
    for (Expr *E : LE->capture_inits()) {
      assert(CapI != LE->capture_end());
      const LambdaCapture &Cap = *CapI++;
      if (!E)
        continue;
      if (Cap.capturesVariable())
        Path.push_back({IndirectLocalPathEntry::LambdaCaptureInit, E, &Cap});
      if (E->isGLValue())
        visitLocalsRetainedByReferenceBinding(Path, E, RK_ReferenceBinding,
                                              Visit, EnableLifetimeWarnings);
      else
        visitLocalsRetainedByInitializer(Path, E, Visit, true,
                                         EnableLifetimeWarnings);
      if (Cap.capturesVariable())
        Path.pop_back();
    }
  }

  // Assume that a copy or move from a temporary references the same objects
  // that the temporary does.
  if (auto *CCE = dyn_cast<CXXConstructExpr>(Init)) {
    if (CCE->getConstructor()->isCopyOrMoveConstructor()) {
      if (auto *MTE = dyn_cast<MaterializeTemporaryExpr>(CCE->getArg(0))) {
        // assert(false && "hit temporary copy path");
        Expr *Arg = MTE->getSubExpr();
        Path.push_back({IndirectLocalPathEntry::TemporaryCopy, Arg,
                        CCE->getConstructor()});
        visitLocalsRetainedByInitializer(Path, Arg, Visit, true,
                                         /*EnableLifetimeWarnings*/ false);
        Path.pop_back();
      }
    }
  }

  if (isa<CallExpr>(Init) || isa<CXXConstructExpr>(Init)) {
    if (EnableLifetimeWarnings)
      handleGslAnnotatedTypes(Path, Init, Visit);
    return visitLifetimeBoundArguments(Path, Init, Visit);
  }

  switch (Init->getStmtClass()) {
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(Init);
    // If the initializer is the address of a local, we could have a lifetime
    // problem.
    if (UO->getOpcode() == UO_AddrOf) {
      // If this is &rvalue, then it's ill-formed and we have already diagnosed
      // it. Don't produce a redundant warning about the lifetime of the
      // temporary.
      if (isa<MaterializeTemporaryExpr>(UO->getSubExpr()))
        return;

      Path.push_back({IndirectLocalPathEntry::AddressOf, UO});
      visitLocalsRetainedByReferenceBinding(Path, UO->getSubExpr(),
                                            RK_ReferenceBinding, Visit,
                                            EnableLifetimeWarnings);
    }
    break;
  }

  case Stmt::BinaryOperatorClass: {
    // Handle pointer arithmetic.
    auto *BO = cast<BinaryOperator>(Init);
    BinaryOperatorKind BOK = BO->getOpcode();
    if (!BO->getType()->isPointerType() || (BOK != BO_Add && BOK != BO_Sub))
      break;

    if (BO->getLHS()->getType()->isPointerType())
      visitLocalsRetainedByInitializer(Path, BO->getLHS(), Visit, true,
                                       EnableLifetimeWarnings);
    else if (BO->getRHS()->getType()->isPointerType())
      visitLocalsRetainedByInitializer(Path, BO->getRHS(), Visit, true,
                                       EnableLifetimeWarnings);
    break;
  }

  case Stmt::ConditionalOperatorClass:
  case Stmt::BinaryConditionalOperatorClass: {
    auto *C = cast<AbstractConditionalOperator>(Init);
    // In C++, we can have a throw-expression operand, which has 'void' type
    // and isn't interesting from a lifetime perspective.
    if (!C->getTrueExpr()->getType()->isVoidType())
      visitLocalsRetainedByInitializer(Path, C->getTrueExpr(), Visit, true,
                                       EnableLifetimeWarnings);
    if (!C->getFalseExpr()->getType()->isVoidType())
      visitLocalsRetainedByInitializer(Path, C->getFalseExpr(), Visit, true,
                                       EnableLifetimeWarnings);
    break;
  }

  case Stmt::BlockExprClass:
    if (cast<BlockExpr>(Init)->getBlockDecl()->hasCaptures()) {
      // This is a local block, whose lifetime is that of the function.
      Visit(Path, Local(cast<BlockExpr>(Init)), RK_ReferenceBinding);
    }
    break;

  case Stmt::AddrLabelExprClass:
    // We want to warn if the address of a label would escape the function.
    Visit(Path, Local(cast<AddrLabelExpr>(Init)), RK_ReferenceBinding);
    break;

  default:
    break;
  }
}

/// Whether a path to an object supports lifetime extension.
enum PathLifetimeKind {
  /// Lifetime-extend along this path.
  Extend,
  /// We should lifetime-extend, but we don't because (due to technical
  /// limitations) we can't. This happens for default member initializers,
  /// which we don't clone for every use, so we don't have a unique
  /// MaterializeTemporaryExpr to update.
  ShouldExtend,
  /// Do not lifetime extend along this path.
  NoExtend
};

/// Determine whether this is an indirect path to a temporary that we are
/// supposed to lifetime-extend along.
static PathLifetimeKind
shouldLifetimeExtendThroughPath(const IndirectLocalPath &Path) {
  PathLifetimeKind Kind = PathLifetimeKind::Extend;
  for (auto Elem : Path) {
    if (Elem.Kind == IndirectLocalPathEntry::DefaultInit)
      Kind = PathLifetimeKind::ShouldExtend;
    else if (Elem.Kind != IndirectLocalPathEntry::LambdaCaptureInit)
      return PathLifetimeKind::NoExtend;
  }
  return Kind;
}

/// Find the range for the first interesting entry in the path at or after I.
static SourceRange nextPathEntryRange(const IndirectLocalPath &Path, unsigned I,
                                      Expr *E) {
  for (unsigned N = Path.size(); I != N; ++I) {
    switch (Path[I].Kind) {
    case IndirectLocalPathEntry::AddressOf:
    case IndirectLocalPathEntry::LValToRVal:
    case IndirectLocalPathEntry::LifetimeBoundCall:
    case IndirectLocalPathEntry::TemporaryCopy:
    case IndirectLocalPathEntry::GslReferenceInit:
    case IndirectLocalPathEntry::GslPointerInit:
    case IndirectLocalPathEntry::GslPointerAssignment:
      // These exist primarily to mark the path as not permitting or
      // supporting lifetime extension.
      break;

    case IndirectLocalPathEntry::VarInit:
      if (cast<VarDecl>(Path[I].D)->isImplicit())
        return SourceRange();
      [[fallthrough]];
    case IndirectLocalPathEntry::DefaultInit:
      return Path[I].E->getSourceRange();

    case IndirectLocalPathEntry::LambdaCaptureInit:
      if (!Path[I].Capture->capturesVariable())
        continue;
      return Path[I].E->getSourceRange();
    }
  }
  return E->getSourceRange();
}

static bool pathOnlyHandlesGslPointer(IndirectLocalPath &Path) {
  for (const auto &It : llvm::reverse(Path)) {
    switch (It.Kind) {
    case IndirectLocalPathEntry::VarInit:
    case IndirectLocalPathEntry::AddressOf:
    case IndirectLocalPathEntry::LifetimeBoundCall:
      continue;
    case IndirectLocalPathEntry::GslPointerInit:
    case IndirectLocalPathEntry::GslReferenceInit:
    case IndirectLocalPathEntry::GslPointerAssignment:
      return true;
    default:
      return false;
    }
  }
  return false;
}

static void checkExprLifetimeImpl(Sema &SemaRef,
                                  const InitializedEntity *InitEntity,
                                  const InitializedEntity *ExtendingEntity,
                                  LifetimeKind LK,
                                  const AssignedEntity *AEntity, Expr *Init,
                                  bool EnableLifetimeWarnings) {
  assert((AEntity && LK == LK_Assignment) ||
         (InitEntity && LK != LK_Assignment));
  // If this entity doesn't have an interesting lifetime, don't bother looking
  // for temporaries within its initializer.
  if (LK == LK_FullExpression)
    return;

  // FIXME: consider moving the TemporaryVisitor and visitLocalsRetained*
  // functions to a dedicated class.
  auto TemporaryVisitor = [&](IndirectLocalPath &Path, Local L,
                              ReferenceKind RK) -> bool {
    SourceRange DiagRange = nextPathEntryRange(Path, 0, L);
    SourceLocation DiagLoc = DiagRange.getBegin();

    auto *MTE = dyn_cast<MaterializeTemporaryExpr>(L);

    bool IsGslPtrValueFromGslTempOwner = false;
    bool IsLocalGslOwner = false;
    if (pathOnlyHandlesGslPointer(Path)) {
      if (isa<DeclRefExpr>(L)) {
        // We do not want to follow the references when returning a pointer
        // originating from a local owner to avoid the following false positive:
        //   int &p = *localUniquePtr;
        //   someContainer.add(std::move(localUniquePtr));
        //   return p;
        IsLocalGslOwner = isRecordWithAttr<OwnerAttr>(L->getType());
        if (pathContainsInit(Path) || !IsLocalGslOwner)
          return false;
      } else {
        IsGslPtrValueFromGslTempOwner =
            MTE && !MTE->getExtendingDecl() &&
            isRecordWithAttr<OwnerAttr>(MTE->getType());
        // Skipping a chain of initializing gsl::Pointer annotated objects.
        // We are looking only for the final source to find out if it was
        // a local or temporary owner or the address of a local variable/param.
        if (!IsGslPtrValueFromGslTempOwner)
          return true;
      }
    }

    switch (LK) {
    case LK_FullExpression:
      llvm_unreachable("already handled this");

    case LK_Extended: {
      if (!MTE) {
        // The initialized entity has lifetime beyond the full-expression,
        // and the local entity does too, so don't warn.
        //
        // FIXME: We should consider warning if a static / thread storage
        // duration variable retains an automatic storage duration local.
        return false;
      }

      if (IsGslPtrValueFromGslTempOwner && DiagLoc.isValid()) {
        SemaRef.Diag(DiagLoc, diag::warn_dangling_lifetime_pointer)
            << DiagRange;
        return false;
      }

      switch (shouldLifetimeExtendThroughPath(Path)) {
      case PathLifetimeKind::Extend:
        // Update the storage duration of the materialized temporary.
        // FIXME: Rebuild the expression instead of mutating it.
        MTE->setExtendingDecl(ExtendingEntity->getDecl(),
                              ExtendingEntity->allocateManglingNumber());
        // Also visit the temporaries lifetime-extended by this initializer.
        return true;

      case PathLifetimeKind::ShouldExtend:
        // We're supposed to lifetime-extend the temporary along this path (per
        // the resolution of DR1815), but we don't support that yet.
        //
        // FIXME: Properly handle this situation. Perhaps the easiest approach
        // would be to clone the initializer expression on each use that would
        // lifetime extend its temporaries.
        SemaRef.Diag(DiagLoc, diag::warn_unsupported_lifetime_extension)
            << RK << DiagRange;
        break;

      case PathLifetimeKind::NoExtend:
        // If the path goes through the initialization of a variable or field,
        // it can't possibly reach a temporary created in this full-expression.
        // We will have already diagnosed any problems with the initializer.
        if (pathContainsInit(Path))
          return false;

        SemaRef.Diag(DiagLoc, diag::warn_dangling_variable)
            << RK << !InitEntity->getParent()
            << ExtendingEntity->getDecl()->isImplicit()
            << ExtendingEntity->getDecl() << Init->isGLValue() << DiagRange;
        break;
      }
      break;
    }

    case LK_Assignment: {
      if (!MTE || pathContainsInit(Path))
        return false;
      assert(shouldLifetimeExtendThroughPath(Path) ==
                 PathLifetimeKind::NoExtend &&
             "No lifetime extension for assignments");
      SemaRef.Diag(DiagLoc,
                   IsGslPtrValueFromGslTempOwner
                       ? diag::warn_dangling_lifetime_pointer_assignment
                       : diag::warn_dangling_pointer_assignment)
          << AEntity->LHS << DiagRange;
      return false;
    }
    case LK_MemInitializer: {
      if (MTE) {
        // Under C++ DR1696, if a mem-initializer (or a default member
        // initializer used by the absence of one) would lifetime-extend a
        // temporary, the program is ill-formed.
        if (auto *ExtendingDecl =
                ExtendingEntity ? ExtendingEntity->getDecl() : nullptr) {
          if (IsGslPtrValueFromGslTempOwner) {
            SemaRef.Diag(DiagLoc, diag::warn_dangling_lifetime_pointer_member)
                << ExtendingDecl << DiagRange;
            SemaRef.Diag(ExtendingDecl->getLocation(),
                         diag::note_ref_or_ptr_member_declared_here)
                << true;
            return false;
          }
          bool IsSubobjectMember = ExtendingEntity != InitEntity;
          SemaRef.Diag(DiagLoc, shouldLifetimeExtendThroughPath(Path) !=
                                        PathLifetimeKind::NoExtend
                                    ? diag::err_dangling_member
                                    : diag::warn_dangling_member)
              << ExtendingDecl << IsSubobjectMember << RK << DiagRange;
          // Don't bother adding a note pointing to the field if we're inside
          // its default member initializer; our primary diagnostic points to
          // the same place in that case.
          if (Path.empty() ||
              Path.back().Kind != IndirectLocalPathEntry::DefaultInit) {
            SemaRef.Diag(ExtendingDecl->getLocation(),
                         diag::note_lifetime_extending_member_declared_here)
                << RK << IsSubobjectMember;
          }
        } else {
          // We have a mem-initializer but no particular field within it; this
          // is either a base class or a delegating initializer directly
          // initializing the base-class from something that doesn't live long
          // enough.
          //
          // FIXME: Warn on this.
          return false;
        }
      } else {
        // Paths via a default initializer can only occur during error recovery
        // (there's no other way that a default initializer can refer to a
        // local). Don't produce a bogus warning on those cases.
        if (pathContainsInit(Path))
          return false;

        // Suppress false positives for code like the one below:
        //   Ctor(unique_ptr<T> up) : member(*up), member2(move(up)) {}
        if (IsLocalGslOwner && pathOnlyHandlesGslPointer(Path))
          return false;

        auto *DRE = dyn_cast<DeclRefExpr>(L);
        auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
        if (!VD) {
          // A member was initialized to a local block.
          // FIXME: Warn on this.
          return false;
        }

        if (auto *Member =
                ExtendingEntity ? ExtendingEntity->getDecl() : nullptr) {
          bool IsPointer = !Member->getType()->isReferenceType();
          SemaRef.Diag(DiagLoc,
                       IsPointer ? diag::warn_init_ptr_member_to_parameter_addr
                                 : diag::warn_bind_ref_member_to_parameter)
              << Member << VD << isa<ParmVarDecl>(VD) << DiagRange;
          SemaRef.Diag(Member->getLocation(),
                       diag::note_ref_or_ptr_member_declared_here)
              << (unsigned)IsPointer;
        }
      }
      break;
    }

    case LK_New:
      if (isa<MaterializeTemporaryExpr>(L)) {
        if (IsGslPtrValueFromGslTempOwner)
          SemaRef.Diag(DiagLoc, diag::warn_dangling_lifetime_pointer)
              << DiagRange;
        else
          SemaRef.Diag(DiagLoc, RK == RK_ReferenceBinding
                                    ? diag::warn_new_dangling_reference
                                    : diag::warn_new_dangling_initializer_list)
              << !InitEntity->getParent() << DiagRange;
      } else {
        // We can't determine if the allocation outlives the local declaration.
        return false;
      }
      break;

    case LK_Return:
    case LK_StmtExprResult:
      if (auto *DRE = dyn_cast<DeclRefExpr>(L)) {
        // We can't determine if the local variable outlives the statement
        // expression.
        if (LK == LK_StmtExprResult)
          return false;
        SemaRef.Diag(DiagLoc, diag::warn_ret_stack_addr_ref)
            << InitEntity->getType()->isReferenceType() << DRE->getDecl()
            << isa<ParmVarDecl>(DRE->getDecl()) << DiagRange;
      } else if (isa<BlockExpr>(L)) {
        SemaRef.Diag(DiagLoc, diag::err_ret_local_block) << DiagRange;
      } else if (isa<AddrLabelExpr>(L)) {
        // Don't warn when returning a label from a statement expression.
        // Leaving the scope doesn't end its lifetime.
        if (LK == LK_StmtExprResult)
          return false;
        SemaRef.Diag(DiagLoc, diag::warn_ret_addr_label) << DiagRange;
      } else if (auto *CLE = dyn_cast<CompoundLiteralExpr>(L)) {
        SemaRef.Diag(DiagLoc, diag::warn_ret_stack_addr_ref)
            << InitEntity->getType()->isReferenceType() << CLE->getInitializer()
            << 2 << DiagRange;
      } else {
        // P2748R5: Disallow Binding a Returned Glvalue to a Temporary.
        // [stmt.return]/p6: In a function whose return type is a reference,
        // other than an invented function for std::is_convertible ([meta.rel]),
        // a return statement that binds the returned reference to a temporary
        // expression ([class.temporary]) is ill-formed.
        if (SemaRef.getLangOpts().CPlusPlus26 &&
            InitEntity->getType()->isReferenceType())
          SemaRef.Diag(DiagLoc, diag::err_ret_local_temp_ref)
              << InitEntity->getType()->isReferenceType() << DiagRange;
        else
          SemaRef.Diag(DiagLoc, diag::warn_ret_local_temp_addr_ref)
              << InitEntity->getType()->isReferenceType() << DiagRange;
      }
      break;
    }

    for (unsigned I = 0; I != Path.size(); ++I) {
      auto Elem = Path[I];

      switch (Elem.Kind) {
      case IndirectLocalPathEntry::AddressOf:
      case IndirectLocalPathEntry::LValToRVal:
        // These exist primarily to mark the path as not permitting or
        // supporting lifetime extension.
        break;

      case IndirectLocalPathEntry::LifetimeBoundCall:
      case IndirectLocalPathEntry::TemporaryCopy:
      case IndirectLocalPathEntry::GslPointerInit:
      case IndirectLocalPathEntry::GslReferenceInit:
      case IndirectLocalPathEntry::GslPointerAssignment:
        // FIXME: Consider adding a note for these.
        break;

      case IndirectLocalPathEntry::DefaultInit: {
        auto *FD = cast<FieldDecl>(Elem.D);
        SemaRef.Diag(FD->getLocation(),
                     diag::note_init_with_default_member_initializer)
            << FD << nextPathEntryRange(Path, I + 1, L);
        break;
      }

      case IndirectLocalPathEntry::VarInit: {
        const VarDecl *VD = cast<VarDecl>(Elem.D);
        SemaRef.Diag(VD->getLocation(), diag::note_local_var_initializer)
            << VD->getType()->isReferenceType() << VD->isImplicit()
            << VD->getDeclName() << nextPathEntryRange(Path, I + 1, L);
        break;
      }

      case IndirectLocalPathEntry::LambdaCaptureInit:
        if (!Elem.Capture->capturesVariable())
          break;
        // FIXME: We can't easily tell apart an init-capture from a nested
        // capture of an init-capture.
        const ValueDecl *VD = Elem.Capture->getCapturedVar();
        SemaRef.Diag(Elem.Capture->getLocation(),
                     diag::note_lambda_capture_initializer)
            << VD << VD->isInitCapture() << Elem.Capture->isExplicit()
            << (Elem.Capture->getCaptureKind() == LCK_ByRef) << VD
            << nextPathEntryRange(Path, I + 1, L);
        break;
      }
    }

    // We didn't lifetime-extend, so don't go any further; we don't need more
    // warnings or errors on inner temporaries within this one's initializer.
    return false;
  };

  llvm::SmallVector<IndirectLocalPathEntry, 8> Path;
  if (EnableLifetimeWarnings && LK == LK_Assignment &&
      isRecordWithAttr<PointerAttr>(AEntity->LHS->getType()))
    Path.push_back({IndirectLocalPathEntry::GslPointerAssignment, Init});

  if (Init->isGLValue())
    visitLocalsRetainedByReferenceBinding(Path, Init, RK_ReferenceBinding,
                                          TemporaryVisitor,
                                          EnableLifetimeWarnings);
  else
    visitLocalsRetainedByInitializer(
        Path, Init, TemporaryVisitor,
        // Don't revisit the sub inits for the intialization case.
        /*RevisitSubinits=*/!InitEntity, EnableLifetimeWarnings);
}

void checkExprLifetime(Sema &SemaRef, const InitializedEntity &Entity,
                       Expr *Init) {
  auto LTResult = getEntityLifetime(&Entity);
  LifetimeKind LK = LTResult.getInt();
  const InitializedEntity *ExtendingEntity = LTResult.getPointer();
  bool EnableLifetimeWarnings = !SemaRef.getDiagnostics().isIgnored(
      diag::warn_dangling_lifetime_pointer, SourceLocation());
  checkExprLifetimeImpl(SemaRef, &Entity, ExtendingEntity, LK,
                        /*AEntity*/ nullptr, Init, EnableLifetimeWarnings);
}

void checkExprLifetime(Sema &SemaRef, const AssignedEntity &Entity,
                       Expr *Init) {
  bool EnableLifetimeWarnings = !SemaRef.getDiagnostics().isIgnored(
      diag::warn_dangling_lifetime_pointer, SourceLocation());
  bool RunAnalysis = Entity.LHS->getType()->isPointerType() ||
                     (EnableLifetimeWarnings &&
                      isRecordWithAttr<PointerAttr>(Entity.LHS->getType()));

  if (!RunAnalysis)
    return;

  checkExprLifetimeImpl(SemaRef, /*InitEntity=*/nullptr,
                        /*ExtendingEntity=*/nullptr, LK_Assignment, &Entity,
                        Init, EnableLifetimeWarnings);
}

} // namespace clang::sema
