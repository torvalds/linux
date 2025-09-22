//===------ SemaDeclCXX.cpp - Semantic Analysis for C++ Declarations ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ declarations.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/CXXFieldCollector.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/SaveAndRestore.h"
#include <map>
#include <optional>
#include <set>

using namespace clang;

//===----------------------------------------------------------------------===//
// CheckDefaultArgumentVisitor
//===----------------------------------------------------------------------===//

namespace {
/// CheckDefaultArgumentVisitor - C++ [dcl.fct.default] Traverses
/// the default argument of a parameter to determine whether it
/// contains any ill-formed subexpressions. For example, this will
/// diagnose the use of local variables or parameters within the
/// default argument expression.
class CheckDefaultArgumentVisitor
    : public ConstStmtVisitor<CheckDefaultArgumentVisitor, bool> {
  Sema &S;
  const Expr *DefaultArg;

public:
  CheckDefaultArgumentVisitor(Sema &S, const Expr *DefaultArg)
      : S(S), DefaultArg(DefaultArg) {}

  bool VisitExpr(const Expr *Node);
  bool VisitDeclRefExpr(const DeclRefExpr *DRE);
  bool VisitCXXThisExpr(const CXXThisExpr *ThisE);
  bool VisitLambdaExpr(const LambdaExpr *Lambda);
  bool VisitPseudoObjectExpr(const PseudoObjectExpr *POE);
};

/// VisitExpr - Visit all of the children of this expression.
bool CheckDefaultArgumentVisitor::VisitExpr(const Expr *Node) {
  bool IsInvalid = false;
  for (const Stmt *SubStmt : Node->children())
    if (SubStmt)
      IsInvalid |= Visit(SubStmt);
  return IsInvalid;
}

/// VisitDeclRefExpr - Visit a reference to a declaration, to
/// determine whether this declaration can be used in the default
/// argument expression.
bool CheckDefaultArgumentVisitor::VisitDeclRefExpr(const DeclRefExpr *DRE) {
  const ValueDecl *Decl = dyn_cast<ValueDecl>(DRE->getDecl());

  if (!isa<VarDecl, BindingDecl>(Decl))
    return false;

  if (const auto *Param = dyn_cast<ParmVarDecl>(Decl)) {
    // C++ [dcl.fct.default]p9:
    //   [...] parameters of a function shall not be used in default
    //   argument expressions, even if they are not evaluated. [...]
    //
    // C++17 [dcl.fct.default]p9 (by CWG 2082):
    //   [...] A parameter shall not appear as a potentially-evaluated
    //   expression in a default argument. [...]
    //
    if (DRE->isNonOdrUse() != NOUR_Unevaluated)
      return S.Diag(DRE->getBeginLoc(),
                    diag::err_param_default_argument_references_param)
             << Param->getDeclName() << DefaultArg->getSourceRange();
  } else if (auto *VD = Decl->getPotentiallyDecomposedVarDecl()) {
    // C++ [dcl.fct.default]p7:
    //   Local variables shall not be used in default argument
    //   expressions.
    //
    // C++17 [dcl.fct.default]p7 (by CWG 2082):
    //   A local variable shall not appear as a potentially-evaluated
    //   expression in a default argument.
    //
    // C++20 [dcl.fct.default]p7 (DR as part of P0588R1, see also CWG 2346):
    //   Note: A local variable cannot be odr-used (6.3) in a default
    //   argument.
    //
    if (VD->isLocalVarDecl() && !DRE->isNonOdrUse())
      return S.Diag(DRE->getBeginLoc(),
                    diag::err_param_default_argument_references_local)
             << Decl << DefaultArg->getSourceRange();
  }
  return false;
}

/// VisitCXXThisExpr - Visit a C++ "this" expression.
bool CheckDefaultArgumentVisitor::VisitCXXThisExpr(const CXXThisExpr *ThisE) {
  // C++ [dcl.fct.default]p8:
  //   The keyword this shall not be used in a default argument of a
  //   member function.
  return S.Diag(ThisE->getBeginLoc(),
                diag::err_param_default_argument_references_this)
         << ThisE->getSourceRange();
}

bool CheckDefaultArgumentVisitor::VisitPseudoObjectExpr(
    const PseudoObjectExpr *POE) {
  bool Invalid = false;
  for (const Expr *E : POE->semantics()) {
    // Look through bindings.
    if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E)) {
      E = OVE->getSourceExpr();
      assert(E && "pseudo-object binding without source expression?");
    }

    Invalid |= Visit(E);
  }
  return Invalid;
}

bool CheckDefaultArgumentVisitor::VisitLambdaExpr(const LambdaExpr *Lambda) {
  // [expr.prim.lambda.capture]p9
  // a lambda-expression appearing in a default argument cannot implicitly or
  // explicitly capture any local entity. Such a lambda-expression can still
  // have an init-capture if any full-expression in its initializer satisfies
  // the constraints of an expression appearing in a default argument.
  bool Invalid = false;
  for (const LambdaCapture &LC : Lambda->captures()) {
    if (!Lambda->isInitCapture(&LC))
      return S.Diag(LC.getLocation(), diag::err_lambda_capture_default_arg);
    // Init captures are always VarDecl.
    auto *D = cast<VarDecl>(LC.getCapturedVar());
    Invalid |= Visit(D->getInit());
  }
  return Invalid;
}
} // namespace

void
Sema::ImplicitExceptionSpecification::CalledDecl(SourceLocation CallLoc,
                                                 const CXXMethodDecl *Method) {
  // If we have an MSAny spec already, don't bother.
  if (!Method || ComputedEST == EST_MSAny)
    return;

  const FunctionProtoType *Proto
    = Method->getType()->getAs<FunctionProtoType>();
  Proto = Self->ResolveExceptionSpec(CallLoc, Proto);
  if (!Proto)
    return;

  ExceptionSpecificationType EST = Proto->getExceptionSpecType();

  // If we have a throw-all spec at this point, ignore the function.
  if (ComputedEST == EST_None)
    return;

  if (EST == EST_None && Method->hasAttr<NoThrowAttr>())
    EST = EST_BasicNoexcept;

  switch (EST) {
  case EST_Unparsed:
  case EST_Uninstantiated:
  case EST_Unevaluated:
    llvm_unreachable("should not see unresolved exception specs here");

  // If this function can throw any exceptions, make a note of that.
  case EST_MSAny:
  case EST_None:
    // FIXME: Whichever we see last of MSAny and None determines our result.
    // We should make a consistent, order-independent choice here.
    ClearExceptions();
    ComputedEST = EST;
    return;
  case EST_NoexceptFalse:
    ClearExceptions();
    ComputedEST = EST_None;
    return;
  // FIXME: If the call to this decl is using any of its default arguments, we
  // need to search them for potentially-throwing calls.
  // If this function has a basic noexcept, it doesn't affect the outcome.
  case EST_BasicNoexcept:
  case EST_NoexceptTrue:
  case EST_NoThrow:
    return;
  // If we're still at noexcept(true) and there's a throw() callee,
  // change to that specification.
  case EST_DynamicNone:
    if (ComputedEST == EST_BasicNoexcept)
      ComputedEST = EST_DynamicNone;
    return;
  case EST_DependentNoexcept:
    llvm_unreachable(
        "should not generate implicit declarations for dependent cases");
  case EST_Dynamic:
    break;
  }
  assert(EST == EST_Dynamic && "EST case not considered earlier.");
  assert(ComputedEST != EST_None &&
         "Shouldn't collect exceptions when throw-all is guaranteed.");
  ComputedEST = EST_Dynamic;
  // Record the exceptions in this function's exception specification.
  for (const auto &E : Proto->exceptions())
    if (ExceptionsSeen.insert(Self->Context.getCanonicalType(E)).second)
      Exceptions.push_back(E);
}

void Sema::ImplicitExceptionSpecification::CalledStmt(Stmt *S) {
  if (!S || ComputedEST == EST_MSAny)
    return;

  // FIXME:
  //
  // C++0x [except.spec]p14:
  //   [An] implicit exception-specification specifies the type-id T if and
  // only if T is allowed by the exception-specification of a function directly
  // invoked by f's implicit definition; f shall allow all exceptions if any
  // function it directly invokes allows all exceptions, and f shall allow no
  // exceptions if every function it directly invokes allows no exceptions.
  //
  // Note in particular that if an implicit exception-specification is generated
  // for a function containing a throw-expression, that specification can still
  // be noexcept(true).
  //
  // Note also that 'directly invoked' is not defined in the standard, and there
  // is no indication that we should only consider potentially-evaluated calls.
  //
  // Ultimately we should implement the intent of the standard: the exception
  // specification should be the set of exceptions which can be thrown by the
  // implicit definition. For now, we assume that any non-nothrow expression can
  // throw any exception.

  if (Self->canThrow(S))
    ComputedEST = EST_None;
}

ExprResult Sema::ConvertParamDefaultArgument(ParmVarDecl *Param, Expr *Arg,
                                             SourceLocation EqualLoc) {
  if (RequireCompleteType(Param->getLocation(), Param->getType(),
                          diag::err_typecheck_decl_incomplete_type))
    return true;

  // C++ [dcl.fct.default]p5
  //   A default argument expression is implicitly converted (clause
  //   4) to the parameter type. The default argument expression has
  //   the same semantic constraints as the initializer expression in
  //   a declaration of a variable of the parameter type, using the
  //   copy-initialization semantics (8.5).
  InitializedEntity Entity = InitializedEntity::InitializeParameter(Context,
                                                                    Param);
  InitializationKind Kind = InitializationKind::CreateCopy(Param->getLocation(),
                                                           EqualLoc);
  InitializationSequence InitSeq(*this, Entity, Kind, Arg);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Arg);
  if (Result.isInvalid())
    return true;
  Arg = Result.getAs<Expr>();

  CheckCompletedExpr(Arg, EqualLoc);
  Arg = MaybeCreateExprWithCleanups(Arg);

  return Arg;
}

void Sema::SetParamDefaultArgument(ParmVarDecl *Param, Expr *Arg,
                                   SourceLocation EqualLoc) {
  // Add the default argument to the parameter
  Param->setDefaultArg(Arg);

  // We have already instantiated this parameter; provide each of the
  // instantiations with the uninstantiated default argument.
  UnparsedDefaultArgInstantiationsMap::iterator InstPos
    = UnparsedDefaultArgInstantiations.find(Param);
  if (InstPos != UnparsedDefaultArgInstantiations.end()) {
    for (unsigned I = 0, N = InstPos->second.size(); I != N; ++I)
      InstPos->second[I]->setUninstantiatedDefaultArg(Arg);

    // We're done tracking this parameter's instantiations.
    UnparsedDefaultArgInstantiations.erase(InstPos);
  }
}

void
Sema::ActOnParamDefaultArgument(Decl *param, SourceLocation EqualLoc,
                                Expr *DefaultArg) {
  if (!param || !DefaultArg)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  UnparsedDefaultArgLocs.erase(Param);

  // Default arguments are only permitted in C++
  if (!getLangOpts().CPlusPlus) {
    Diag(EqualLoc, diag::err_param_default_argument)
      << DefaultArg->getSourceRange();
    return ActOnParamDefaultArgumentError(param, EqualLoc, DefaultArg);
  }

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(DefaultArg, UPPC_DefaultArgument))
    return ActOnParamDefaultArgumentError(param, EqualLoc, DefaultArg);

  // C++11 [dcl.fct.default]p3
  //   A default argument expression [...] shall not be specified for a
  //   parameter pack.
  if (Param->isParameterPack()) {
    Diag(EqualLoc, diag::err_param_default_argument_on_parameter_pack)
        << DefaultArg->getSourceRange();
    // Recover by discarding the default argument.
    Param->setDefaultArg(nullptr);
    return;
  }

  ExprResult Result = ConvertParamDefaultArgument(Param, DefaultArg, EqualLoc);
  if (Result.isInvalid())
    return ActOnParamDefaultArgumentError(param, EqualLoc, DefaultArg);

  DefaultArg = Result.getAs<Expr>();

  // Check that the default argument is well-formed
  CheckDefaultArgumentVisitor DefaultArgChecker(*this, DefaultArg);
  if (DefaultArgChecker.Visit(DefaultArg))
    return ActOnParamDefaultArgumentError(param, EqualLoc, DefaultArg);

  SetParamDefaultArgument(Param, DefaultArg, EqualLoc);
}

void Sema::ActOnParamUnparsedDefaultArgument(Decl *param,
                                             SourceLocation EqualLoc,
                                             SourceLocation ArgLoc) {
  if (!param)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  Param->setUnparsedDefaultArg();
  UnparsedDefaultArgLocs[Param] = ArgLoc;
}

void Sema::ActOnParamDefaultArgumentError(Decl *param, SourceLocation EqualLoc,
                                          Expr *DefaultArg) {
  if (!param)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  Param->setInvalidDecl();
  UnparsedDefaultArgLocs.erase(Param);
  ExprResult RE;
  if (DefaultArg) {
    RE = CreateRecoveryExpr(EqualLoc, DefaultArg->getEndLoc(), {DefaultArg},
                            Param->getType().getNonReferenceType());
  } else {
    RE = CreateRecoveryExpr(EqualLoc, EqualLoc, {},
                            Param->getType().getNonReferenceType());
  }
  Param->setDefaultArg(RE.get());
}

void Sema::CheckExtraCXXDefaultArguments(Declarator &D) {
  // C++ [dcl.fct.default]p3
  //   A default argument expression shall be specified only in the
  //   parameter-declaration-clause of a function declaration or in a
  //   template-parameter (14.1). It shall not be specified for a
  //   parameter pack. If it is specified in a
  //   parameter-declaration-clause, it shall not occur within a
  //   declarator or abstract-declarator of a parameter-declaration.
  bool MightBeFunction = D.isFunctionDeclarationContext();
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &chunk = D.getTypeObject(i);
    if (chunk.Kind == DeclaratorChunk::Function) {
      if (MightBeFunction) {
        // This is a function declaration. It can have default arguments, but
        // keep looking in case its return type is a function type with default
        // arguments.
        MightBeFunction = false;
        continue;
      }
      for (unsigned argIdx = 0, e = chunk.Fun.NumParams; argIdx != e;
           ++argIdx) {
        ParmVarDecl *Param = cast<ParmVarDecl>(chunk.Fun.Params[argIdx].Param);
        if (Param->hasUnparsedDefaultArg()) {
          std::unique_ptr<CachedTokens> Toks =
              std::move(chunk.Fun.Params[argIdx].DefaultArgTokens);
          SourceRange SR;
          if (Toks->size() > 1)
            SR = SourceRange((*Toks)[1].getLocation(),
                             Toks->back().getLocation());
          else
            SR = UnparsedDefaultArgLocs[Param];
          Diag(Param->getLocation(), diag::err_param_default_argument_nonfunc)
            << SR;
        } else if (Param->getDefaultArg()) {
          Diag(Param->getLocation(), diag::err_param_default_argument_nonfunc)
            << Param->getDefaultArg()->getSourceRange();
          Param->setDefaultArg(nullptr);
        }
      }
    } else if (chunk.Kind != DeclaratorChunk::Paren) {
      MightBeFunction = false;
    }
  }
}

static bool functionDeclHasDefaultArgument(const FunctionDecl *FD) {
  return llvm::any_of(FD->parameters(), [](ParmVarDecl *P) {
    return P->hasDefaultArg() && !P->hasInheritedDefaultArg();
  });
}

bool Sema::MergeCXXFunctionDecl(FunctionDecl *New, FunctionDecl *Old,
                                Scope *S) {
  bool Invalid = false;

  // The declaration context corresponding to the scope is the semantic
  // parent, unless this is a local function declaration, in which case
  // it is that surrounding function.
  DeclContext *ScopeDC = New->isLocalExternDecl()
                             ? New->getLexicalDeclContext()
                             : New->getDeclContext();

  // Find the previous declaration for the purpose of default arguments.
  FunctionDecl *PrevForDefaultArgs = Old;
  for (/**/; PrevForDefaultArgs;
       // Don't bother looking back past the latest decl if this is a local
       // extern declaration; nothing else could work.
       PrevForDefaultArgs = New->isLocalExternDecl()
                                ? nullptr
                                : PrevForDefaultArgs->getPreviousDecl()) {
    // Ignore hidden declarations.
    if (!LookupResult::isVisible(*this, PrevForDefaultArgs))
      continue;

    if (S && !isDeclInScope(PrevForDefaultArgs, ScopeDC, S) &&
        !New->isCXXClassMember()) {
      // Ignore default arguments of old decl if they are not in
      // the same scope and this is not an out-of-line definition of
      // a member function.
      continue;
    }

    if (PrevForDefaultArgs->isLocalExternDecl() != New->isLocalExternDecl()) {
      // If only one of these is a local function declaration, then they are
      // declared in different scopes, even though isDeclInScope may think
      // they're in the same scope. (If both are local, the scope check is
      // sufficient, and if neither is local, then they are in the same scope.)
      continue;
    }

    // We found the right previous declaration.
    break;
  }

  // C++ [dcl.fct.default]p4:
  //   For non-template functions, default arguments can be added in
  //   later declarations of a function in the same
  //   scope. Declarations in different scopes have completely
  //   distinct sets of default arguments. That is, declarations in
  //   inner scopes do not acquire default arguments from
  //   declarations in outer scopes, and vice versa. In a given
  //   function declaration, all parameters subsequent to a
  //   parameter with a default argument shall have default
  //   arguments supplied in this or previous declarations. A
  //   default argument shall not be redefined by a later
  //   declaration (not even to the same value).
  //
  // C++ [dcl.fct.default]p6:
  //   Except for member functions of class templates, the default arguments
  //   in a member function definition that appears outside of the class
  //   definition are added to the set of default arguments provided by the
  //   member function declaration in the class definition.
  for (unsigned p = 0, NumParams = PrevForDefaultArgs
                                       ? PrevForDefaultArgs->getNumParams()
                                       : 0;
       p < NumParams; ++p) {
    ParmVarDecl *OldParam = PrevForDefaultArgs->getParamDecl(p);
    ParmVarDecl *NewParam = New->getParamDecl(p);

    bool OldParamHasDfl = OldParam ? OldParam->hasDefaultArg() : false;
    bool NewParamHasDfl = NewParam->hasDefaultArg();

    if (OldParamHasDfl && NewParamHasDfl) {
      unsigned DiagDefaultParamID =
        diag::err_param_default_argument_redefinition;

      // MSVC accepts that default parameters be redefined for member functions
      // of template class. The new default parameter's value is ignored.
      Invalid = true;
      if (getLangOpts().MicrosoftExt) {
        CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(New);
        if (MD && MD->getParent()->getDescribedClassTemplate()) {
          // Merge the old default argument into the new parameter.
          NewParam->setHasInheritedDefaultArg();
          if (OldParam->hasUninstantiatedDefaultArg())
            NewParam->setUninstantiatedDefaultArg(
                                      OldParam->getUninstantiatedDefaultArg());
          else
            NewParam->setDefaultArg(OldParam->getInit());
          DiagDefaultParamID = diag::ext_param_default_argument_redefinition;
          Invalid = false;
        }
      }

      // FIXME: If we knew where the '=' was, we could easily provide a fix-it
      // hint here. Alternatively, we could walk the type-source information
      // for NewParam to find the last source location in the type... but it
      // isn't worth the effort right now. This is the kind of test case that
      // is hard to get right:
      //   int f(int);
      //   void g(int (*fp)(int) = f);
      //   void g(int (*fp)(int) = &f);
      Diag(NewParam->getLocation(), DiagDefaultParamID)
        << NewParam->getDefaultArgRange();

      // Look for the function declaration where the default argument was
      // actually written, which may be a declaration prior to Old.
      for (auto Older = PrevForDefaultArgs;
           OldParam->hasInheritedDefaultArg(); /**/) {
        Older = Older->getPreviousDecl();
        OldParam = Older->getParamDecl(p);
      }

      Diag(OldParam->getLocation(), diag::note_previous_definition)
        << OldParam->getDefaultArgRange();
    } else if (OldParamHasDfl) {
      // Merge the old default argument into the new parameter unless the new
      // function is a friend declaration in a template class. In the latter
      // case the default arguments will be inherited when the friend
      // declaration will be instantiated.
      if (New->getFriendObjectKind() == Decl::FOK_None ||
          !New->getLexicalDeclContext()->isDependentContext()) {
        // It's important to use getInit() here;  getDefaultArg()
        // strips off any top-level ExprWithCleanups.
        NewParam->setHasInheritedDefaultArg();
        if (OldParam->hasUnparsedDefaultArg())
          NewParam->setUnparsedDefaultArg();
        else if (OldParam->hasUninstantiatedDefaultArg())
          NewParam->setUninstantiatedDefaultArg(
                                       OldParam->getUninstantiatedDefaultArg());
        else
          NewParam->setDefaultArg(OldParam->getInit());
      }
    } else if (NewParamHasDfl) {
      if (New->getDescribedFunctionTemplate()) {
        // Paragraph 4, quoted above, only applies to non-template functions.
        Diag(NewParam->getLocation(),
             diag::err_param_default_argument_template_redecl)
          << NewParam->getDefaultArgRange();
        Diag(PrevForDefaultArgs->getLocation(),
             diag::note_template_prev_declaration)
            << false;
      } else if (New->getTemplateSpecializationKind()
                   != TSK_ImplicitInstantiation &&
                 New->getTemplateSpecializationKind() != TSK_Undeclared) {
        // C++ [temp.expr.spec]p21:
        //   Default function arguments shall not be specified in a declaration
        //   or a definition for one of the following explicit specializations:
        //     - the explicit specialization of a function template;
        //     - the explicit specialization of a member function template;
        //     - the explicit specialization of a member function of a class
        //       template where the class template specialization to which the
        //       member function specialization belongs is implicitly
        //       instantiated.
        Diag(NewParam->getLocation(), diag::err_template_spec_default_arg)
          << (New->getTemplateSpecializationKind() ==TSK_ExplicitSpecialization)
          << New->getDeclName()
          << NewParam->getDefaultArgRange();
      } else if (New->getDeclContext()->isDependentContext()) {
        // C++ [dcl.fct.default]p6 (DR217):
        //   Default arguments for a member function of a class template shall
        //   be specified on the initial declaration of the member function
        //   within the class template.
        //
        // Reading the tea leaves a bit in DR217 and its reference to DR205
        // leads me to the conclusion that one cannot add default function
        // arguments for an out-of-line definition of a member function of a
        // dependent type.
        int WhichKind = 2;
        if (CXXRecordDecl *Record
              = dyn_cast<CXXRecordDecl>(New->getDeclContext())) {
          if (Record->getDescribedClassTemplate())
            WhichKind = 0;
          else if (isa<ClassTemplatePartialSpecializationDecl>(Record))
            WhichKind = 1;
          else
            WhichKind = 2;
        }

        Diag(NewParam->getLocation(),
             diag::err_param_default_argument_member_template_redecl)
          << WhichKind
          << NewParam->getDefaultArgRange();
      }
    }
  }

  // DR1344: If a default argument is added outside a class definition and that
  // default argument makes the function a special member function, the program
  // is ill-formed. This can only happen for constructors.
  if (isa<CXXConstructorDecl>(New) &&
      New->getMinRequiredArguments() < Old->getMinRequiredArguments()) {
    CXXSpecialMemberKind NewSM = getSpecialMember(cast<CXXMethodDecl>(New)),
                         OldSM = getSpecialMember(cast<CXXMethodDecl>(Old));
    if (NewSM != OldSM) {
      ParmVarDecl *NewParam = New->getParamDecl(New->getMinRequiredArguments());
      assert(NewParam->hasDefaultArg());
      Diag(NewParam->getLocation(), diag::err_default_arg_makes_ctor_special)
          << NewParam->getDefaultArgRange() << llvm::to_underlying(NewSM);
      Diag(Old->getLocation(), diag::note_previous_declaration);
    }
  }

  const FunctionDecl *Def;
  // C++11 [dcl.constexpr]p1: If any declaration of a function or function
  // template has a constexpr specifier then all its declarations shall
  // contain the constexpr specifier.
  if (New->getConstexprKind() != Old->getConstexprKind()) {
    Diag(New->getLocation(), diag::err_constexpr_redecl_mismatch)
        << New << static_cast<int>(New->getConstexprKind())
        << static_cast<int>(Old->getConstexprKind());
    Diag(Old->getLocation(), diag::note_previous_declaration);
    Invalid = true;
  } else if (!Old->getMostRecentDecl()->isInlined() && New->isInlined() &&
             Old->isDefined(Def) &&
             // If a friend function is inlined but does not have 'inline'
             // specifier, it is a definition. Do not report attribute conflict
             // in this case, redefinition will be diagnosed later.
             (New->isInlineSpecified() ||
              New->getFriendObjectKind() == Decl::FOK_None)) {
    // C++11 [dcl.fcn.spec]p4:
    //   If the definition of a function appears in a translation unit before its
    //   first declaration as inline, the program is ill-formed.
    Diag(New->getLocation(), diag::err_inline_decl_follows_def) << New;
    Diag(Def->getLocation(), diag::note_previous_definition);
    Invalid = true;
  }

  // C++17 [temp.deduct.guide]p3:
  //   Two deduction guide declarations in the same translation unit
  //   for the same class template shall not have equivalent
  //   parameter-declaration-clauses.
  if (isa<CXXDeductionGuideDecl>(New) &&
      !New->isFunctionTemplateSpecialization() && isVisible(Old)) {
    Diag(New->getLocation(), diag::err_deduction_guide_redeclared);
    Diag(Old->getLocation(), diag::note_previous_declaration);
  }

  // C++11 [dcl.fct.default]p4: If a friend declaration specifies a default
  // argument expression, that declaration shall be a definition and shall be
  // the only declaration of the function or function template in the
  // translation unit.
  if (Old->getFriendObjectKind() == Decl::FOK_Undeclared &&
      functionDeclHasDefaultArgument(Old)) {
    Diag(New->getLocation(), diag::err_friend_decl_with_def_arg_redeclared);
    Diag(Old->getLocation(), diag::note_previous_declaration);
    Invalid = true;
  }

  // C++11 [temp.friend]p4 (DR329):
  //   When a function is defined in a friend function declaration in a class
  //   template, the function is instantiated when the function is odr-used.
  //   The same restrictions on multiple declarations and definitions that
  //   apply to non-template function declarations and definitions also apply
  //   to these implicit definitions.
  const FunctionDecl *OldDefinition = nullptr;
  if (New->isThisDeclarationInstantiatedFromAFriendDefinition() &&
      Old->isDefined(OldDefinition, true))
    CheckForFunctionRedefinition(New, OldDefinition);

  return Invalid;
}

void Sema::DiagPlaceholderVariableDefinition(SourceLocation Loc) {
  Diag(Loc, getLangOpts().CPlusPlus26
                ? diag::warn_cxx23_placeholder_var_definition
                : diag::ext_placeholder_var_definition);
}

NamedDecl *
Sema::ActOnDecompositionDeclarator(Scope *S, Declarator &D,
                                   MultiTemplateParamsArg TemplateParamLists) {
  assert(D.isDecompositionDeclarator());
  const DecompositionDeclarator &Decomp = D.getDecompositionDeclarator();

  // The syntax only allows a decomposition declarator as a simple-declaration,
  // a for-range-declaration, or a condition in Clang, but we parse it in more
  // cases than that.
  if (!D.mayHaveDecompositionDeclarator()) {
    Diag(Decomp.getLSquareLoc(), diag::err_decomp_decl_context)
      << Decomp.getSourceRange();
    return nullptr;
  }

  if (!TemplateParamLists.empty()) {
    // FIXME: There's no rule against this, but there are also no rules that
    // would actually make it usable, so we reject it for now.
    Diag(TemplateParamLists.front()->getTemplateLoc(),
         diag::err_decomp_decl_template);
    return nullptr;
  }

  Diag(Decomp.getLSquareLoc(),
       !getLangOpts().CPlusPlus17
           ? diag::ext_decomp_decl
           : D.getContext() == DeclaratorContext::Condition
                 ? diag::ext_decomp_decl_cond
                 : diag::warn_cxx14_compat_decomp_decl)
      << Decomp.getSourceRange();

  // The semantic context is always just the current context.
  DeclContext *const DC = CurContext;

  // C++17 [dcl.dcl]/8:
  //   The decl-specifier-seq shall contain only the type-specifier auto
  //   and cv-qualifiers.
  // C++20 [dcl.dcl]/8:
  //   If decl-specifier-seq contains any decl-specifier other than static,
  //   thread_local, auto, or cv-qualifiers, the program is ill-formed.
  // C++23 [dcl.pre]/6:
  //   Each decl-specifier in the decl-specifier-seq shall be static,
  //   thread_local, auto (9.2.9.6 [dcl.spec.auto]), or a cv-qualifier.
  auto &DS = D.getDeclSpec();
  {
    // Note: While constrained-auto needs to be checked, we do so separately so
    // we can emit a better diagnostic.
    SmallVector<StringRef, 8> BadSpecifiers;
    SmallVector<SourceLocation, 8> BadSpecifierLocs;
    SmallVector<StringRef, 8> CPlusPlus20Specifiers;
    SmallVector<SourceLocation, 8> CPlusPlus20SpecifierLocs;
    if (auto SCS = DS.getStorageClassSpec()) {
      if (SCS == DeclSpec::SCS_static) {
        CPlusPlus20Specifiers.push_back(DeclSpec::getSpecifierName(SCS));
        CPlusPlus20SpecifierLocs.push_back(DS.getStorageClassSpecLoc());
      } else {
        BadSpecifiers.push_back(DeclSpec::getSpecifierName(SCS));
        BadSpecifierLocs.push_back(DS.getStorageClassSpecLoc());
      }
    }
    if (auto TSCS = DS.getThreadStorageClassSpec()) {
      CPlusPlus20Specifiers.push_back(DeclSpec::getSpecifierName(TSCS));
      CPlusPlus20SpecifierLocs.push_back(DS.getThreadStorageClassSpecLoc());
    }
    if (DS.hasConstexprSpecifier()) {
      BadSpecifiers.push_back(
          DeclSpec::getSpecifierName(DS.getConstexprSpecifier()));
      BadSpecifierLocs.push_back(DS.getConstexprSpecLoc());
    }
    if (DS.isInlineSpecified()) {
      BadSpecifiers.push_back("inline");
      BadSpecifierLocs.push_back(DS.getInlineSpecLoc());
    }

    if (!BadSpecifiers.empty()) {
      auto &&Err = Diag(BadSpecifierLocs.front(), diag::err_decomp_decl_spec);
      Err << (int)BadSpecifiers.size()
          << llvm::join(BadSpecifiers.begin(), BadSpecifiers.end(), " ");
      // Don't add FixItHints to remove the specifiers; we do still respect
      // them when building the underlying variable.
      for (auto Loc : BadSpecifierLocs)
        Err << SourceRange(Loc, Loc);
    } else if (!CPlusPlus20Specifiers.empty()) {
      auto &&Warn = Diag(CPlusPlus20SpecifierLocs.front(),
                         getLangOpts().CPlusPlus20
                             ? diag::warn_cxx17_compat_decomp_decl_spec
                             : diag::ext_decomp_decl_spec);
      Warn << (int)CPlusPlus20Specifiers.size()
           << llvm::join(CPlusPlus20Specifiers.begin(),
                         CPlusPlus20Specifiers.end(), " ");
      for (auto Loc : CPlusPlus20SpecifierLocs)
        Warn << SourceRange(Loc, Loc);
    }
    // We can't recover from it being declared as a typedef.
    if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef)
      return nullptr;
  }

  // C++2a [dcl.struct.bind]p1:
  //   A cv that includes volatile is deprecated
  if ((DS.getTypeQualifiers() & DeclSpec::TQ_volatile) &&
      getLangOpts().CPlusPlus20)
    Diag(DS.getVolatileSpecLoc(),
         diag::warn_deprecated_volatile_structured_binding);

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  QualType R = TInfo->getType();

  if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                      UPPC_DeclarationType))
    D.setInvalidType();

  // The syntax only allows a single ref-qualifier prior to the decomposition
  // declarator. No other declarator chunks are permitted. Also check the type
  // specifier here.
  if (DS.getTypeSpecType() != DeclSpec::TST_auto ||
      D.hasGroupingParens() || D.getNumTypeObjects() > 1 ||
      (D.getNumTypeObjects() == 1 &&
       D.getTypeObject(0).Kind != DeclaratorChunk::Reference)) {
    Diag(Decomp.getLSquareLoc(),
         (D.hasGroupingParens() ||
          (D.getNumTypeObjects() &&
           D.getTypeObject(0).Kind == DeclaratorChunk::Paren))
             ? diag::err_decomp_decl_parens
             : diag::err_decomp_decl_type)
        << R;

    // In most cases, there's no actual problem with an explicitly-specified
    // type, but a function type won't work here, and ActOnVariableDeclarator
    // shouldn't be called for such a type.
    if (R->isFunctionType())
      D.setInvalidType();
  }

  // Constrained auto is prohibited by [decl.pre]p6, so check that here.
  if (DS.isConstrainedAuto()) {
    TemplateIdAnnotation *TemplRep = DS.getRepAsTemplateId();
    assert(TemplRep->Kind == TNK_Concept_template &&
           "No other template kind should be possible for a constrained auto");

    SourceRange TemplRange{TemplRep->TemplateNameLoc,
                           TemplRep->RAngleLoc.isValid()
                               ? TemplRep->RAngleLoc
                               : TemplRep->TemplateNameLoc};
    Diag(TemplRep->TemplateNameLoc, diag::err_decomp_decl_constraint)
        << TemplRange << FixItHint::CreateRemoval(TemplRange);
  }

  // Build the BindingDecls.
  SmallVector<BindingDecl*, 8> Bindings;

  // Build the BindingDecls.
  for (auto &B : D.getDecompositionDeclarator().bindings()) {
    // Check for name conflicts.
    DeclarationNameInfo NameInfo(B.Name, B.NameLoc);
    IdentifierInfo *VarName = B.Name;
    assert(VarName && "Cannot have an unnamed binding declaration");

    LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                          RedeclarationKind::ForVisibleRedeclaration);
    LookupName(Previous, S,
               /*CreateBuiltins*/DC->getRedeclContext()->isTranslationUnit());

    // It's not permitted to shadow a template parameter name.
    if (Previous.isSingleResult() &&
        Previous.getFoundDecl()->isTemplateParameter()) {
      DiagnoseTemplateParameterShadow(D.getIdentifierLoc(),
                                      Previous.getFoundDecl());
      Previous.clear();
    }

    auto *BD = BindingDecl::Create(Context, DC, B.NameLoc, VarName);

    ProcessDeclAttributeList(S, BD, *B.Attrs);

    // Find the shadowed declaration before filtering for scope.
    NamedDecl *ShadowedDecl = D.getCXXScopeSpec().isEmpty()
                                  ? getShadowedDeclaration(BD, Previous)
                                  : nullptr;

    bool ConsiderLinkage = DC->isFunctionOrMethod() &&
                           DS.getStorageClassSpec() == DeclSpec::SCS_extern;
    FilterLookupForScope(Previous, DC, S, ConsiderLinkage,
                         /*AllowInlineNamespace*/false);

    bool IsPlaceholder = DS.getStorageClassSpec() != DeclSpec::SCS_static &&
                         DC->isFunctionOrMethod() && VarName->isPlaceholder();
    if (!Previous.empty()) {
      if (IsPlaceholder) {
        bool sameDC = (Previous.end() - 1)
                          ->getDeclContext()
                          ->getRedeclContext()
                          ->Equals(DC->getRedeclContext());
        if (sameDC &&
            isDeclInScope(*(Previous.end() - 1), CurContext, S, false)) {
          Previous.clear();
          DiagPlaceholderVariableDefinition(B.NameLoc);
        }
      } else {
        auto *Old = Previous.getRepresentativeDecl();
        Diag(B.NameLoc, diag::err_redefinition) << B.Name;
        Diag(Old->getLocation(), diag::note_previous_definition);
      }
    } else if (ShadowedDecl && !D.isRedeclaration()) {
      CheckShadow(BD, ShadowedDecl, Previous);
    }
    PushOnScopeChains(BD, S, true);
    Bindings.push_back(BD);
    ParsingInitForAutoVars.insert(BD);
  }

  // There are no prior lookup results for the variable itself, because it
  // is unnamed.
  DeclarationNameInfo NameInfo((IdentifierInfo *)nullptr,
                               Decomp.getLSquareLoc());
  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        RedeclarationKind::ForVisibleRedeclaration);

  // Build the variable that holds the non-decomposed object.
  bool AddToScope = true;
  NamedDecl *New =
      ActOnVariableDeclarator(S, D, DC, TInfo, Previous,
                              MultiTemplateParamsArg(), AddToScope, Bindings);
  if (AddToScope) {
    S->AddDecl(New);
    CurContext->addHiddenDecl(New);
  }

  if (OpenMP().isInOpenMPDeclareTargetContext())
    OpenMP().checkDeclIsAllowedInOpenMPTarget(nullptr, New);

  return New;
}

static bool checkSimpleDecomposition(
    Sema &S, ArrayRef<BindingDecl *> Bindings, ValueDecl *Src,
    QualType DecompType, const llvm::APSInt &NumElems, QualType ElemType,
    llvm::function_ref<ExprResult(SourceLocation, Expr *, unsigned)> GetInit) {
  if ((int64_t)Bindings.size() != NumElems) {
    S.Diag(Src->getLocation(), diag::err_decomp_decl_wrong_number_bindings)
        << DecompType << (unsigned)Bindings.size()
        << (unsigned)NumElems.getLimitedValue(UINT_MAX)
        << toString(NumElems, 10) << (NumElems < Bindings.size());
    return true;
  }

  unsigned I = 0;
  for (auto *B : Bindings) {
    SourceLocation Loc = B->getLocation();
    ExprResult E = S.BuildDeclRefExpr(Src, DecompType, VK_LValue, Loc);
    if (E.isInvalid())
      return true;
    E = GetInit(Loc, E.get(), I++);
    if (E.isInvalid())
      return true;
    B->setBinding(ElemType, E.get());
  }

  return false;
}

static bool checkArrayLikeDecomposition(Sema &S,
                                        ArrayRef<BindingDecl *> Bindings,
                                        ValueDecl *Src, QualType DecompType,
                                        const llvm::APSInt &NumElems,
                                        QualType ElemType) {
  return checkSimpleDecomposition(
      S, Bindings, Src, DecompType, NumElems, ElemType,
      [&](SourceLocation Loc, Expr *Base, unsigned I) -> ExprResult {
        ExprResult E = S.ActOnIntegerConstant(Loc, I);
        if (E.isInvalid())
          return ExprError();
        return S.CreateBuiltinArraySubscriptExpr(Base, Loc, E.get(), Loc);
      });
}

static bool checkArrayDecomposition(Sema &S, ArrayRef<BindingDecl*> Bindings,
                                    ValueDecl *Src, QualType DecompType,
                                    const ConstantArrayType *CAT) {
  return checkArrayLikeDecomposition(S, Bindings, Src, DecompType,
                                     llvm::APSInt(CAT->getSize()),
                                     CAT->getElementType());
}

static bool checkVectorDecomposition(Sema &S, ArrayRef<BindingDecl*> Bindings,
                                     ValueDecl *Src, QualType DecompType,
                                     const VectorType *VT) {
  return checkArrayLikeDecomposition(
      S, Bindings, Src, DecompType, llvm::APSInt::get(VT->getNumElements()),
      S.Context.getQualifiedType(VT->getElementType(),
                                 DecompType.getQualifiers()));
}

static bool checkComplexDecomposition(Sema &S,
                                      ArrayRef<BindingDecl *> Bindings,
                                      ValueDecl *Src, QualType DecompType,
                                      const ComplexType *CT) {
  return checkSimpleDecomposition(
      S, Bindings, Src, DecompType, llvm::APSInt::get(2),
      S.Context.getQualifiedType(CT->getElementType(),
                                 DecompType.getQualifiers()),
      [&](SourceLocation Loc, Expr *Base, unsigned I) -> ExprResult {
        return S.CreateBuiltinUnaryOp(Loc, I ? UO_Imag : UO_Real, Base);
      });
}

static std::string printTemplateArgs(const PrintingPolicy &PrintingPolicy,
                                     TemplateArgumentListInfo &Args,
                                     const TemplateParameterList *Params) {
  SmallString<128> SS;
  llvm::raw_svector_ostream OS(SS);
  bool First = true;
  unsigned I = 0;
  for (auto &Arg : Args.arguments()) {
    if (!First)
      OS << ", ";
    Arg.getArgument().print(PrintingPolicy, OS,
                            TemplateParameterList::shouldIncludeTypeForArgument(
                                PrintingPolicy, Params, I));
    First = false;
    I++;
  }
  return std::string(OS.str());
}

static bool lookupStdTypeTraitMember(Sema &S, LookupResult &TraitMemberLookup,
                                     SourceLocation Loc, StringRef Trait,
                                     TemplateArgumentListInfo &Args,
                                     unsigned DiagID) {
  auto DiagnoseMissing = [&] {
    if (DiagID)
      S.Diag(Loc, DiagID) << printTemplateArgs(S.Context.getPrintingPolicy(),
                                               Args, /*Params*/ nullptr);
    return true;
  };

  // FIXME: Factor out duplication with lookupPromiseType in SemaCoroutine.
  NamespaceDecl *Std = S.getStdNamespace();
  if (!Std)
    return DiagnoseMissing();

  // Look up the trait itself, within namespace std. We can diagnose various
  // problems with this lookup even if we've been asked to not diagnose a
  // missing specialization, because this can only fail if the user has been
  // declaring their own names in namespace std or we don't support the
  // standard library implementation in use.
  LookupResult Result(S, &S.PP.getIdentifierTable().get(Trait),
                      Loc, Sema::LookupOrdinaryName);
  if (!S.LookupQualifiedName(Result, Std))
    return DiagnoseMissing();
  if (Result.isAmbiguous())
    return true;

  ClassTemplateDecl *TraitTD = Result.getAsSingle<ClassTemplateDecl>();
  if (!TraitTD) {
    Result.suppressDiagnostics();
    NamedDecl *Found = *Result.begin();
    S.Diag(Loc, diag::err_std_type_trait_not_class_template) << Trait;
    S.Diag(Found->getLocation(), diag::note_declared_at);
    return true;
  }

  // Build the template-id.
  QualType TraitTy = S.CheckTemplateIdType(TemplateName(TraitTD), Loc, Args);
  if (TraitTy.isNull())
    return true;
  if (!S.isCompleteType(Loc, TraitTy)) {
    if (DiagID)
      S.RequireCompleteType(
          Loc, TraitTy, DiagID,
          printTemplateArgs(S.Context.getPrintingPolicy(), Args,
                            TraitTD->getTemplateParameters()));
    return true;
  }

  CXXRecordDecl *RD = TraitTy->getAsCXXRecordDecl();
  assert(RD && "specialization of class template is not a class?");

  // Look up the member of the trait type.
  S.LookupQualifiedName(TraitMemberLookup, RD);
  return TraitMemberLookup.isAmbiguous();
}

static TemplateArgumentLoc
getTrivialIntegralTemplateArgument(Sema &S, SourceLocation Loc, QualType T,
                                   uint64_t I) {
  TemplateArgument Arg(S.Context, S.Context.MakeIntValue(I, T), T);
  return S.getTrivialTemplateArgumentLoc(Arg, T, Loc);
}

static TemplateArgumentLoc
getTrivialTypeTemplateArgument(Sema &S, SourceLocation Loc, QualType T) {
  return S.getTrivialTemplateArgumentLoc(TemplateArgument(T), QualType(), Loc);
}

namespace { enum class IsTupleLike { TupleLike, NotTupleLike, Error }; }

static IsTupleLike isTupleLike(Sema &S, SourceLocation Loc, QualType T,
                               llvm::APSInt &Size) {
  EnterExpressionEvaluationContext ContextRAII(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  DeclarationName Value = S.PP.getIdentifierInfo("value");
  LookupResult R(S, Value, Loc, Sema::LookupOrdinaryName);

  // Form template argument list for tuple_size<T>.
  TemplateArgumentListInfo Args(Loc, Loc);
  Args.addArgument(getTrivialTypeTemplateArgument(S, Loc, T));

  // If there's no tuple_size specialization or the lookup of 'value' is empty,
  // it's not tuple-like.
  if (lookupStdTypeTraitMember(S, R, Loc, "tuple_size", Args, /*DiagID*/ 0) ||
      R.empty())
    return IsTupleLike::NotTupleLike;

  // If we get this far, we've committed to the tuple interpretation, but
  // we can still fail if there actually isn't a usable ::value.

  struct ICEDiagnoser : Sema::VerifyICEDiagnoser {
    LookupResult &R;
    TemplateArgumentListInfo &Args;
    ICEDiagnoser(LookupResult &R, TemplateArgumentListInfo &Args)
        : R(R), Args(Args) {}
    Sema::SemaDiagnosticBuilder diagnoseNotICE(Sema &S,
                                               SourceLocation Loc) override {
      return S.Diag(Loc, diag::err_decomp_decl_std_tuple_size_not_constant)
             << printTemplateArgs(S.Context.getPrintingPolicy(), Args,
                                  /*Params*/ nullptr);
    }
  } Diagnoser(R, Args);

  ExprResult E =
      S.BuildDeclarationNameExpr(CXXScopeSpec(), R, /*NeedsADL*/false);
  if (E.isInvalid())
    return IsTupleLike::Error;

  E = S.VerifyIntegerConstantExpression(E.get(), &Size, Diagnoser);
  if (E.isInvalid())
    return IsTupleLike::Error;

  return IsTupleLike::TupleLike;
}

/// \return std::tuple_element<I, T>::type.
static QualType getTupleLikeElementType(Sema &S, SourceLocation Loc,
                                        unsigned I, QualType T) {
  // Form template argument list for tuple_element<I, T>.
  TemplateArgumentListInfo Args(Loc, Loc);
  Args.addArgument(
      getTrivialIntegralTemplateArgument(S, Loc, S.Context.getSizeType(), I));
  Args.addArgument(getTrivialTypeTemplateArgument(S, Loc, T));

  DeclarationName TypeDN = S.PP.getIdentifierInfo("type");
  LookupResult R(S, TypeDN, Loc, Sema::LookupOrdinaryName);
  if (lookupStdTypeTraitMember(
          S, R, Loc, "tuple_element", Args,
          diag::err_decomp_decl_std_tuple_element_not_specialized))
    return QualType();

  auto *TD = R.getAsSingle<TypeDecl>();
  if (!TD) {
    R.suppressDiagnostics();
    S.Diag(Loc, diag::err_decomp_decl_std_tuple_element_not_specialized)
        << printTemplateArgs(S.Context.getPrintingPolicy(), Args,
                             /*Params*/ nullptr);
    if (!R.empty())
      S.Diag(R.getRepresentativeDecl()->getLocation(), diag::note_declared_at);
    return QualType();
  }

  return S.Context.getTypeDeclType(TD);
}

namespace {
struct InitializingBinding {
  Sema &S;
  InitializingBinding(Sema &S, BindingDecl *BD) : S(S) {
    Sema::CodeSynthesisContext Ctx;
    Ctx.Kind = Sema::CodeSynthesisContext::InitializingStructuredBinding;
    Ctx.PointOfInstantiation = BD->getLocation();
    Ctx.Entity = BD;
    S.pushCodeSynthesisContext(Ctx);
  }
  ~InitializingBinding() {
    S.popCodeSynthesisContext();
  }
};
}

static bool checkTupleLikeDecomposition(Sema &S,
                                        ArrayRef<BindingDecl *> Bindings,
                                        VarDecl *Src, QualType DecompType,
                                        const llvm::APSInt &TupleSize) {
  if ((int64_t)Bindings.size() != TupleSize) {
    S.Diag(Src->getLocation(), diag::err_decomp_decl_wrong_number_bindings)
        << DecompType << (unsigned)Bindings.size()
        << (unsigned)TupleSize.getLimitedValue(UINT_MAX)
        << toString(TupleSize, 10) << (TupleSize < Bindings.size());
    return true;
  }

  if (Bindings.empty())
    return false;

  DeclarationName GetDN = S.PP.getIdentifierInfo("get");

  // [dcl.decomp]p3:
  //   The unqualified-id get is looked up in the scope of E by class member
  //   access lookup ...
  LookupResult MemberGet(S, GetDN, Src->getLocation(), Sema::LookupMemberName);
  bool UseMemberGet = false;
  if (S.isCompleteType(Src->getLocation(), DecompType)) {
    if (auto *RD = DecompType->getAsCXXRecordDecl())
      S.LookupQualifiedName(MemberGet, RD);
    if (MemberGet.isAmbiguous())
      return true;
    //   ... and if that finds at least one declaration that is a function
    //   template whose first template parameter is a non-type parameter ...
    for (NamedDecl *D : MemberGet) {
      if (FunctionTemplateDecl *FTD =
              dyn_cast<FunctionTemplateDecl>(D->getUnderlyingDecl())) {
        TemplateParameterList *TPL = FTD->getTemplateParameters();
        if (TPL->size() != 0 &&
            isa<NonTypeTemplateParmDecl>(TPL->getParam(0))) {
          //   ... the initializer is e.get<i>().
          UseMemberGet = true;
          break;
        }
      }
    }
  }

  unsigned I = 0;
  for (auto *B : Bindings) {
    InitializingBinding InitContext(S, B);
    SourceLocation Loc = B->getLocation();

    ExprResult E = S.BuildDeclRefExpr(Src, DecompType, VK_LValue, Loc);
    if (E.isInvalid())
      return true;

    //   e is an lvalue if the type of the entity is an lvalue reference and
    //   an xvalue otherwise
    if (!Src->getType()->isLValueReferenceType())
      E = ImplicitCastExpr::Create(S.Context, E.get()->getType(), CK_NoOp,
                                   E.get(), nullptr, VK_XValue,
                                   FPOptionsOverride());

    TemplateArgumentListInfo Args(Loc, Loc);
    Args.addArgument(
        getTrivialIntegralTemplateArgument(S, Loc, S.Context.getSizeType(), I));

    if (UseMemberGet) {
      //   if [lookup of member get] finds at least one declaration, the
      //   initializer is e.get<i-1>().
      E = S.BuildMemberReferenceExpr(E.get(), DecompType, Loc, false,
                                     CXXScopeSpec(), SourceLocation(), nullptr,
                                     MemberGet, &Args, nullptr);
      if (E.isInvalid())
        return true;

      E = S.BuildCallExpr(nullptr, E.get(), Loc, std::nullopt, Loc);
    } else {
      //   Otherwise, the initializer is get<i-1>(e), where get is looked up
      //   in the associated namespaces.
      Expr *Get = UnresolvedLookupExpr::Create(
          S.Context, nullptr, NestedNameSpecifierLoc(), SourceLocation(),
          DeclarationNameInfo(GetDN, Loc), /*RequiresADL=*/true, &Args,
          UnresolvedSetIterator(), UnresolvedSetIterator(),
          /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false);

      Expr *Arg = E.get();
      E = S.BuildCallExpr(nullptr, Get, Loc, Arg, Loc);
    }
    if (E.isInvalid())
      return true;
    Expr *Init = E.get();

    //   Given the type T designated by std::tuple_element<i - 1, E>::type,
    QualType T = getTupleLikeElementType(S, Loc, I, DecompType);
    if (T.isNull())
      return true;

    //   each vi is a variable of type "reference to T" initialized with the
    //   initializer, where the reference is an lvalue reference if the
    //   initializer is an lvalue and an rvalue reference otherwise
    QualType RefType =
        S.BuildReferenceType(T, E.get()->isLValue(), Loc, B->getDeclName());
    if (RefType.isNull())
      return true;
    auto *RefVD = VarDecl::Create(
        S.Context, Src->getDeclContext(), Loc, Loc,
        B->getDeclName().getAsIdentifierInfo(), RefType,
        S.Context.getTrivialTypeSourceInfo(T, Loc), Src->getStorageClass());
    RefVD->setLexicalDeclContext(Src->getLexicalDeclContext());
    RefVD->setTSCSpec(Src->getTSCSpec());
    RefVD->setImplicit();
    if (Src->isInlineSpecified())
      RefVD->setInlineSpecified();
    RefVD->getLexicalDeclContext()->addHiddenDecl(RefVD);

    InitializedEntity Entity = InitializedEntity::InitializeBinding(RefVD);
    InitializationKind Kind = InitializationKind::CreateCopy(Loc, Loc);
    InitializationSequence Seq(S, Entity, Kind, Init);
    E = Seq.Perform(S, Entity, Kind, Init);
    if (E.isInvalid())
      return true;
    E = S.ActOnFinishFullExpr(E.get(), Loc, /*DiscardedValue*/ false);
    if (E.isInvalid())
      return true;
    RefVD->setInit(E.get());
    S.CheckCompleteVariableDeclaration(RefVD);

    E = S.BuildDeclarationNameExpr(CXXScopeSpec(),
                                   DeclarationNameInfo(B->getDeclName(), Loc),
                                   RefVD);
    if (E.isInvalid())
      return true;

    B->setBinding(T, E.get());
    I++;
  }

  return false;
}

/// Find the base class to decompose in a built-in decomposition of a class type.
/// This base class search is, unfortunately, not quite like any other that we
/// perform anywhere else in C++.
static DeclAccessPair findDecomposableBaseClass(Sema &S, SourceLocation Loc,
                                                const CXXRecordDecl *RD,
                                                CXXCastPath &BasePath) {
  auto BaseHasFields = [](const CXXBaseSpecifier *Specifier,
                          CXXBasePath &Path) {
    return Specifier->getType()->getAsCXXRecordDecl()->hasDirectFields();
  };

  const CXXRecordDecl *ClassWithFields = nullptr;
  AccessSpecifier AS = AS_public;
  if (RD->hasDirectFields())
    // [dcl.decomp]p4:
    //   Otherwise, all of E's non-static data members shall be public direct
    //   members of E ...
    ClassWithFields = RD;
  else {
    //   ... or of ...
    CXXBasePaths Paths;
    Paths.setOrigin(const_cast<CXXRecordDecl*>(RD));
    if (!RD->lookupInBases(BaseHasFields, Paths)) {
      // If no classes have fields, just decompose RD itself. (This will work
      // if and only if zero bindings were provided.)
      return DeclAccessPair::make(const_cast<CXXRecordDecl*>(RD), AS_public);
    }

    CXXBasePath *BestPath = nullptr;
    for (auto &P : Paths) {
      if (!BestPath)
        BestPath = &P;
      else if (!S.Context.hasSameType(P.back().Base->getType(),
                                      BestPath->back().Base->getType())) {
        //   ... the same ...
        S.Diag(Loc, diag::err_decomp_decl_multiple_bases_with_members)
          << false << RD << BestPath->back().Base->getType()
          << P.back().Base->getType();
        return DeclAccessPair();
      } else if (P.Access < BestPath->Access) {
        BestPath = &P;
      }
    }

    //   ... unambiguous ...
    QualType BaseType = BestPath->back().Base->getType();
    if (Paths.isAmbiguous(S.Context.getCanonicalType(BaseType))) {
      S.Diag(Loc, diag::err_decomp_decl_ambiguous_base)
        << RD << BaseType << S.getAmbiguousPathsDisplayString(Paths);
      return DeclAccessPair();
    }

    //   ... [accessible, implied by other rules] base class of E.
    S.CheckBaseClassAccess(Loc, BaseType, S.Context.getRecordType(RD),
                           *BestPath, diag::err_decomp_decl_inaccessible_base);
    AS = BestPath->Access;

    ClassWithFields = BaseType->getAsCXXRecordDecl();
    S.BuildBasePathArray(Paths, BasePath);
  }

  // The above search did not check whether the selected class itself has base
  // classes with fields, so check that now.
  CXXBasePaths Paths;
  if (ClassWithFields->lookupInBases(BaseHasFields, Paths)) {
    S.Diag(Loc, diag::err_decomp_decl_multiple_bases_with_members)
      << (ClassWithFields == RD) << RD << ClassWithFields
      << Paths.front().back().Base->getType();
    return DeclAccessPair();
  }

  return DeclAccessPair::make(const_cast<CXXRecordDecl*>(ClassWithFields), AS);
}

static bool checkMemberDecomposition(Sema &S, ArrayRef<BindingDecl*> Bindings,
                                     ValueDecl *Src, QualType DecompType,
                                     const CXXRecordDecl *OrigRD) {
  if (S.RequireCompleteType(Src->getLocation(), DecompType,
                            diag::err_incomplete_type))
    return true;

  CXXCastPath BasePath;
  DeclAccessPair BasePair =
      findDecomposableBaseClass(S, Src->getLocation(), OrigRD, BasePath);
  const CXXRecordDecl *RD = cast_or_null<CXXRecordDecl>(BasePair.getDecl());
  if (!RD)
    return true;
  QualType BaseType = S.Context.getQualifiedType(S.Context.getRecordType(RD),
                                                 DecompType.getQualifiers());

  auto DiagnoseBadNumberOfBindings = [&]() -> bool {
    unsigned NumFields = llvm::count_if(
        RD->fields(), [](FieldDecl *FD) { return !FD->isUnnamedBitField(); });
    assert(Bindings.size() != NumFields);
    S.Diag(Src->getLocation(), diag::err_decomp_decl_wrong_number_bindings)
        << DecompType << (unsigned)Bindings.size() << NumFields << NumFields
        << (NumFields < Bindings.size());
    return true;
  };

  //   all of E's non-static data members shall be [...] well-formed
  //   when named as e.name in the context of the structured binding,
  //   E shall not have an anonymous union member, ...
  unsigned I = 0;
  for (auto *FD : RD->fields()) {
    if (FD->isUnnamedBitField())
      continue;

    // All the non-static data members are required to be nameable, so they
    // must all have names.
    if (!FD->getDeclName()) {
      if (RD->isLambda()) {
        S.Diag(Src->getLocation(), diag::err_decomp_decl_lambda);
        S.Diag(RD->getLocation(), diag::note_lambda_decl);
        return true;
      }

      if (FD->isAnonymousStructOrUnion()) {
        S.Diag(Src->getLocation(), diag::err_decomp_decl_anon_union_member)
          << DecompType << FD->getType()->isUnionType();
        S.Diag(FD->getLocation(), diag::note_declared_at);
        return true;
      }

      // FIXME: Are there any other ways we could have an anonymous member?
    }

    // We have a real field to bind.
    if (I >= Bindings.size())
      return DiagnoseBadNumberOfBindings();
    auto *B = Bindings[I++];
    SourceLocation Loc = B->getLocation();

    // The field must be accessible in the context of the structured binding.
    // We already checked that the base class is accessible.
    // FIXME: Add 'const' to AccessedEntity's classes so we can remove the
    // const_cast here.
    S.CheckStructuredBindingMemberAccess(
        Loc, const_cast<CXXRecordDecl *>(OrigRD),
        DeclAccessPair::make(FD, CXXRecordDecl::MergeAccess(
                                     BasePair.getAccess(), FD->getAccess())));

    // Initialize the binding to Src.FD.
    ExprResult E = S.BuildDeclRefExpr(Src, DecompType, VK_LValue, Loc);
    if (E.isInvalid())
      return true;
    E = S.ImpCastExprToType(E.get(), BaseType, CK_UncheckedDerivedToBase,
                            VK_LValue, &BasePath);
    if (E.isInvalid())
      return true;
    E = S.BuildFieldReferenceExpr(E.get(), /*IsArrow*/ false, Loc,
                                  CXXScopeSpec(), FD,
                                  DeclAccessPair::make(FD, FD->getAccess()),
                                  DeclarationNameInfo(FD->getDeclName(), Loc));
    if (E.isInvalid())
      return true;

    // If the type of the member is T, the referenced type is cv T, where cv is
    // the cv-qualification of the decomposition expression.
    //
    // FIXME: We resolve a defect here: if the field is mutable, we do not add
    // 'const' to the type of the field.
    Qualifiers Q = DecompType.getQualifiers();
    if (FD->isMutable())
      Q.removeConst();
    B->setBinding(S.BuildQualifiedType(FD->getType(), Loc, Q), E.get());
  }

  if (I != Bindings.size())
    return DiagnoseBadNumberOfBindings();

  return false;
}

void Sema::CheckCompleteDecompositionDeclaration(DecompositionDecl *DD) {
  QualType DecompType = DD->getType();

  // If the type of the decomposition is dependent, then so is the type of
  // each binding.
  if (DecompType->isDependentType()) {
    for (auto *B : DD->bindings())
      B->setType(Context.DependentTy);
    return;
  }

  DecompType = DecompType.getNonReferenceType();
  ArrayRef<BindingDecl*> Bindings = DD->bindings();

  // C++1z [dcl.decomp]/2:
  //   If E is an array type [...]
  // As an extension, we also support decomposition of built-in complex and
  // vector types.
  if (auto *CAT = Context.getAsConstantArrayType(DecompType)) {
    if (checkArrayDecomposition(*this, Bindings, DD, DecompType, CAT))
      DD->setInvalidDecl();
    return;
  }
  if (auto *VT = DecompType->getAs<VectorType>()) {
    if (checkVectorDecomposition(*this, Bindings, DD, DecompType, VT))
      DD->setInvalidDecl();
    return;
  }
  if (auto *CT = DecompType->getAs<ComplexType>()) {
    if (checkComplexDecomposition(*this, Bindings, DD, DecompType, CT))
      DD->setInvalidDecl();
    return;
  }

  // C++1z [dcl.decomp]/3:
  //   if the expression std::tuple_size<E>::value is a well-formed integral
  //   constant expression, [...]
  llvm::APSInt TupleSize(32);
  switch (isTupleLike(*this, DD->getLocation(), DecompType, TupleSize)) {
  case IsTupleLike::Error:
    DD->setInvalidDecl();
    return;

  case IsTupleLike::TupleLike:
    if (checkTupleLikeDecomposition(*this, Bindings, DD, DecompType, TupleSize))
      DD->setInvalidDecl();
    return;

  case IsTupleLike::NotTupleLike:
    break;
  }

  // C++1z [dcl.dcl]/8:
  //   [E shall be of array or non-union class type]
  CXXRecordDecl *RD = DecompType->getAsCXXRecordDecl();
  if (!RD || RD->isUnion()) {
    Diag(DD->getLocation(), diag::err_decomp_decl_unbindable_type)
        << DD << !RD << DecompType;
    DD->setInvalidDecl();
    return;
  }

  // C++1z [dcl.decomp]/4:
  //   all of E's non-static data members shall be [...] direct members of
  //   E or of the same unambiguous public base class of E, ...
  if (checkMemberDecomposition(*this, Bindings, DD, DecompType, RD))
    DD->setInvalidDecl();
}

void Sema::MergeVarDeclExceptionSpecs(VarDecl *New, VarDecl *Old) {
  // Shortcut if exceptions are disabled.
  if (!getLangOpts().CXXExceptions)
    return;

  assert(Context.hasSameType(New->getType(), Old->getType()) &&
         "Should only be called if types are otherwise the same.");

  QualType NewType = New->getType();
  QualType OldType = Old->getType();

  // We're only interested in pointers and references to functions, as well
  // as pointers to member functions.
  if (const ReferenceType *R = NewType->getAs<ReferenceType>()) {
    NewType = R->getPointeeType();
    OldType = OldType->castAs<ReferenceType>()->getPointeeType();
  } else if (const PointerType *P = NewType->getAs<PointerType>()) {
    NewType = P->getPointeeType();
    OldType = OldType->castAs<PointerType>()->getPointeeType();
  } else if (const MemberPointerType *M = NewType->getAs<MemberPointerType>()) {
    NewType = M->getPointeeType();
    OldType = OldType->castAs<MemberPointerType>()->getPointeeType();
  }

  if (!NewType->isFunctionProtoType())
    return;

  // There's lots of special cases for functions. For function pointers, system
  // libraries are hopefully not as broken so that we don't need these
  // workarounds.
  if (CheckEquivalentExceptionSpec(
        OldType->getAs<FunctionProtoType>(), Old->getLocation(),
        NewType->getAs<FunctionProtoType>(), New->getLocation())) {
    New->setInvalidDecl();
  }
}

/// CheckCXXDefaultArguments - Verify that the default arguments for a
/// function declaration are well-formed according to C++
/// [dcl.fct.default].
void Sema::CheckCXXDefaultArguments(FunctionDecl *FD) {
  // This checking doesn't make sense for explicit specializations; their
  // default arguments are determined by the declaration we're specializing,
  // not by FD.
  if (FD->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
    return;
  if (auto *FTD = FD->getDescribedFunctionTemplate())
    if (FTD->isMemberSpecialization())
      return;

  unsigned NumParams = FD->getNumParams();
  unsigned ParamIdx = 0;

  // Find first parameter with a default argument
  for (; ParamIdx < NumParams; ++ParamIdx) {
    ParmVarDecl *Param = FD->getParamDecl(ParamIdx);
    if (Param->hasDefaultArg())
      break;
  }

  // C++20 [dcl.fct.default]p4:
  //   In a given function declaration, each parameter subsequent to a parameter
  //   with a default argument shall have a default argument supplied in this or
  //   a previous declaration, unless the parameter was expanded from a
  //   parameter pack, or shall be a function parameter pack.
  for (++ParamIdx; ParamIdx < NumParams; ++ParamIdx) {
    ParmVarDecl *Param = FD->getParamDecl(ParamIdx);
    if (Param->hasDefaultArg() || Param->isParameterPack() ||
        (CurrentInstantiationScope &&
         CurrentInstantiationScope->isLocalPackExpansion(Param)))
      continue;
    if (Param->isInvalidDecl())
      /* We already complained about this parameter. */;
    else if (Param->getIdentifier())
      Diag(Param->getLocation(), diag::err_param_default_argument_missing_name)
          << Param->getIdentifier();
    else
      Diag(Param->getLocation(), diag::err_param_default_argument_missing);
  }
}

/// Check that the given type is a literal type. Issue a diagnostic if not,
/// if Kind is Diagnose.
/// \return \c true if a problem has been found (and optionally diagnosed).
template <typename... Ts>
static bool CheckLiteralType(Sema &SemaRef, Sema::CheckConstexprKind Kind,
                             SourceLocation Loc, QualType T, unsigned DiagID,
                             Ts &&...DiagArgs) {
  if (T->isDependentType())
    return false;

  switch (Kind) {
  case Sema::CheckConstexprKind::Diagnose:
    return SemaRef.RequireLiteralType(Loc, T, DiagID,
                                      std::forward<Ts>(DiagArgs)...);

  case Sema::CheckConstexprKind::CheckValid:
    return !T->isLiteralType(SemaRef.Context);
  }

  llvm_unreachable("unknown CheckConstexprKind");
}

/// Determine whether a destructor cannot be constexpr due to
static bool CheckConstexprDestructorSubobjects(Sema &SemaRef,
                                               const CXXDestructorDecl *DD,
                                               Sema::CheckConstexprKind Kind) {
  assert(!SemaRef.getLangOpts().CPlusPlus23 &&
         "this check is obsolete for C++23");
  auto Check = [&](SourceLocation Loc, QualType T, const FieldDecl *FD) {
    const CXXRecordDecl *RD =
        T->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
    if (!RD || RD->hasConstexprDestructor())
      return true;

    if (Kind == Sema::CheckConstexprKind::Diagnose) {
      SemaRef.Diag(DD->getLocation(), diag::err_constexpr_dtor_subobject)
          << static_cast<int>(DD->getConstexprKind()) << !FD
          << (FD ? FD->getDeclName() : DeclarationName()) << T;
      SemaRef.Diag(Loc, diag::note_constexpr_dtor_subobject)
          << !FD << (FD ? FD->getDeclName() : DeclarationName()) << T;
    }
    return false;
  };

  const CXXRecordDecl *RD = DD->getParent();
  for (const CXXBaseSpecifier &B : RD->bases())
    if (!Check(B.getBaseTypeLoc(), B.getType(), nullptr))
      return false;
  for (const FieldDecl *FD : RD->fields())
    if (!Check(FD->getLocation(), FD->getType(), FD))
      return false;
  return true;
}

/// Check whether a function's parameter types are all literal types. If so,
/// return true. If not, produce a suitable diagnostic and return false.
static bool CheckConstexprParameterTypes(Sema &SemaRef,
                                         const FunctionDecl *FD,
                                         Sema::CheckConstexprKind Kind) {
  assert(!SemaRef.getLangOpts().CPlusPlus23 &&
         "this check is obsolete for C++23");
  unsigned ArgIndex = 0;
  const auto *FT = FD->getType()->castAs<FunctionProtoType>();
  for (FunctionProtoType::param_type_iterator i = FT->param_type_begin(),
                                              e = FT->param_type_end();
       i != e; ++i, ++ArgIndex) {
    const ParmVarDecl *PD = FD->getParamDecl(ArgIndex);
    assert(PD && "null in a parameter list");
    SourceLocation ParamLoc = PD->getLocation();
    if (CheckLiteralType(SemaRef, Kind, ParamLoc, *i,
                         diag::err_constexpr_non_literal_param, ArgIndex + 1,
                         PD->getSourceRange(), isa<CXXConstructorDecl>(FD),
                         FD->isConsteval()))
      return false;
  }
  return true;
}

/// Check whether a function's return type is a literal type. If so, return
/// true. If not, produce a suitable diagnostic and return false.
static bool CheckConstexprReturnType(Sema &SemaRef, const FunctionDecl *FD,
                                     Sema::CheckConstexprKind Kind) {
  assert(!SemaRef.getLangOpts().CPlusPlus23 &&
         "this check is obsolete for C++23");
  if (CheckLiteralType(SemaRef, Kind, FD->getLocation(), FD->getReturnType(),
                       diag::err_constexpr_non_literal_return,
                       FD->isConsteval()))
    return false;
  return true;
}

/// Get diagnostic %select index for tag kind for
/// record diagnostic message.
/// WARNING: Indexes apply to particular diagnostics only!
///
/// \returns diagnostic %select index.
static unsigned getRecordDiagFromTagKind(TagTypeKind Tag) {
  switch (Tag) {
  case TagTypeKind::Struct:
    return 0;
  case TagTypeKind::Interface:
    return 1;
  case TagTypeKind::Class:
    return 2;
  default: llvm_unreachable("Invalid tag kind for record diagnostic!");
  }
}

static bool CheckConstexprFunctionBody(Sema &SemaRef, const FunctionDecl *Dcl,
                                       Stmt *Body,
                                       Sema::CheckConstexprKind Kind);
static bool CheckConstexprMissingReturn(Sema &SemaRef, const FunctionDecl *Dcl);

bool Sema::CheckConstexprFunctionDefinition(const FunctionDecl *NewFD,
                                            CheckConstexprKind Kind) {
  const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(NewFD);
  if (MD && MD->isInstance()) {
    // C++11 [dcl.constexpr]p4:
    //  The definition of a constexpr constructor shall satisfy the following
    //  constraints:
    //  - the class shall not have any virtual base classes;
    //
    // FIXME: This only applies to constructors and destructors, not arbitrary
    // member functions.
    const CXXRecordDecl *RD = MD->getParent();
    if (RD->getNumVBases()) {
      if (Kind == CheckConstexprKind::CheckValid)
        return false;

      Diag(NewFD->getLocation(), diag::err_constexpr_virtual_base)
        << isa<CXXConstructorDecl>(NewFD)
        << getRecordDiagFromTagKind(RD->getTagKind()) << RD->getNumVBases();
      for (const auto &I : RD->vbases())
        Diag(I.getBeginLoc(), diag::note_constexpr_virtual_base_here)
            << I.getSourceRange();
      return false;
    }
  }

  if (!isa<CXXConstructorDecl>(NewFD)) {
    // C++11 [dcl.constexpr]p3:
    //  The definition of a constexpr function shall satisfy the following
    //  constraints:
    // - it shall not be virtual; (removed in C++20)
    const CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(NewFD);
    if (Method && Method->isVirtual()) {
      if (getLangOpts().CPlusPlus20) {
        if (Kind == CheckConstexprKind::Diagnose)
          Diag(Method->getLocation(), diag::warn_cxx17_compat_constexpr_virtual);
      } else {
        if (Kind == CheckConstexprKind::CheckValid)
          return false;

        Method = Method->getCanonicalDecl();
        Diag(Method->getLocation(), diag::err_constexpr_virtual);

        // If it's not obvious why this function is virtual, find an overridden
        // function which uses the 'virtual' keyword.
        const CXXMethodDecl *WrittenVirtual = Method;
        while (!WrittenVirtual->isVirtualAsWritten())
          WrittenVirtual = *WrittenVirtual->begin_overridden_methods();
        if (WrittenVirtual != Method)
          Diag(WrittenVirtual->getLocation(),
               diag::note_overridden_virtual_function);
        return false;
      }
    }

    // - its return type shall be a literal type; (removed in C++23)
    if (!getLangOpts().CPlusPlus23 &&
        !CheckConstexprReturnType(*this, NewFD, Kind))
      return false;
  }

  if (auto *Dtor = dyn_cast<CXXDestructorDecl>(NewFD)) {
    // A destructor can be constexpr only if the defaulted destructor could be;
    // we don't need to check the members and bases if we already know they all
    // have constexpr destructors. (removed in C++23)
    if (!getLangOpts().CPlusPlus23 &&
        !Dtor->getParent()->defaultedDestructorIsConstexpr()) {
      if (Kind == CheckConstexprKind::CheckValid)
        return false;
      if (!CheckConstexprDestructorSubobjects(*this, Dtor, Kind))
        return false;
    }
  }

  // - each of its parameter types shall be a literal type; (removed in C++23)
  if (!getLangOpts().CPlusPlus23 &&
      !CheckConstexprParameterTypes(*this, NewFD, Kind))
    return false;

  Stmt *Body = NewFD->getBody();
  assert(Body &&
         "CheckConstexprFunctionDefinition called on function with no body");
  return CheckConstexprFunctionBody(*this, NewFD, Body, Kind);
}

/// Check the given declaration statement is legal within a constexpr function
/// body. C++11 [dcl.constexpr]p3,p4, and C++1y [dcl.constexpr]p3.
///
/// \return true if the body is OK (maybe only as an extension), false if we
///         have diagnosed a problem.
static bool CheckConstexprDeclStmt(Sema &SemaRef, const FunctionDecl *Dcl,
                                   DeclStmt *DS, SourceLocation &Cxx1yLoc,
                                   Sema::CheckConstexprKind Kind) {
  // C++11 [dcl.constexpr]p3 and p4:
  //  The definition of a constexpr function(p3) or constructor(p4) [...] shall
  //  contain only
  for (const auto *DclIt : DS->decls()) {
    switch (DclIt->getKind()) {
    case Decl::StaticAssert:
    case Decl::Using:
    case Decl::UsingShadow:
    case Decl::UsingDirective:
    case Decl::UnresolvedUsingTypename:
    case Decl::UnresolvedUsingValue:
    case Decl::UsingEnum:
      //   - static_assert-declarations
      //   - using-declarations,
      //   - using-directives,
      //   - using-enum-declaration
      continue;

    case Decl::Typedef:
    case Decl::TypeAlias: {
      //   - typedef declarations and alias-declarations that do not define
      //     classes or enumerations,
      const auto *TN = cast<TypedefNameDecl>(DclIt);
      if (TN->getUnderlyingType()->isVariablyModifiedType()) {
        // Don't allow variably-modified types in constexpr functions.
        if (Kind == Sema::CheckConstexprKind::Diagnose) {
          TypeLoc TL = TN->getTypeSourceInfo()->getTypeLoc();
          SemaRef.Diag(TL.getBeginLoc(), diag::err_constexpr_vla)
            << TL.getSourceRange() << TL.getType()
            << isa<CXXConstructorDecl>(Dcl);
        }
        return false;
      }
      continue;
    }

    case Decl::Enum:
    case Decl::CXXRecord:
      // C++1y allows types to be defined, not just declared.
      if (cast<TagDecl>(DclIt)->isThisDeclarationADefinition()) {
        if (Kind == Sema::CheckConstexprKind::Diagnose) {
          SemaRef.Diag(DS->getBeginLoc(),
                       SemaRef.getLangOpts().CPlusPlus14
                           ? diag::warn_cxx11_compat_constexpr_type_definition
                           : diag::ext_constexpr_type_definition)
              << isa<CXXConstructorDecl>(Dcl);
        } else if (!SemaRef.getLangOpts().CPlusPlus14) {
          return false;
        }
      }
      continue;

    case Decl::EnumConstant:
    case Decl::IndirectField:
    case Decl::ParmVar:
      // These can only appear with other declarations which are banned in
      // C++11 and permitted in C++1y, so ignore them.
      continue;

    case Decl::Var:
    case Decl::Decomposition: {
      // C++1y [dcl.constexpr]p3 allows anything except:
      //   a definition of a variable of non-literal type or of static or
      //   thread storage duration or [before C++2a] for which no
      //   initialization is performed.
      const auto *VD = cast<VarDecl>(DclIt);
      if (VD->isThisDeclarationADefinition()) {
        if (VD->isStaticLocal()) {
          if (Kind == Sema::CheckConstexprKind::Diagnose) {
            SemaRef.Diag(VD->getLocation(),
                         SemaRef.getLangOpts().CPlusPlus23
                             ? diag::warn_cxx20_compat_constexpr_var
                             : diag::ext_constexpr_static_var)
                << isa<CXXConstructorDecl>(Dcl)
                << (VD->getTLSKind() == VarDecl::TLS_Dynamic);
          } else if (!SemaRef.getLangOpts().CPlusPlus23) {
            return false;
          }
        }
        if (SemaRef.LangOpts.CPlusPlus23) {
          CheckLiteralType(SemaRef, Kind, VD->getLocation(), VD->getType(),
                           diag::warn_cxx20_compat_constexpr_var,
                           isa<CXXConstructorDecl>(Dcl),
                           /*variable of non-literal type*/ 2);
        } else if (CheckLiteralType(
                       SemaRef, Kind, VD->getLocation(), VD->getType(),
                       diag::err_constexpr_local_var_non_literal_type,
                       isa<CXXConstructorDecl>(Dcl))) {
          return false;
        }
        if (!VD->getType()->isDependentType() &&
            !VD->hasInit() && !VD->isCXXForRangeDecl()) {
          if (Kind == Sema::CheckConstexprKind::Diagnose) {
            SemaRef.Diag(
                VD->getLocation(),
                SemaRef.getLangOpts().CPlusPlus20
                    ? diag::warn_cxx17_compat_constexpr_local_var_no_init
                    : diag::ext_constexpr_local_var_no_init)
                << isa<CXXConstructorDecl>(Dcl);
          } else if (!SemaRef.getLangOpts().CPlusPlus20) {
            return false;
          }
          continue;
        }
      }
      if (Kind == Sema::CheckConstexprKind::Diagnose) {
        SemaRef.Diag(VD->getLocation(),
                     SemaRef.getLangOpts().CPlusPlus14
                      ? diag::warn_cxx11_compat_constexpr_local_var
                      : diag::ext_constexpr_local_var)
          << isa<CXXConstructorDecl>(Dcl);
      } else if (!SemaRef.getLangOpts().CPlusPlus14) {
        return false;
      }
      continue;
    }

    case Decl::NamespaceAlias:
    case Decl::Function:
      // These are disallowed in C++11 and permitted in C++1y. Allow them
      // everywhere as an extension.
      if (!Cxx1yLoc.isValid())
        Cxx1yLoc = DS->getBeginLoc();
      continue;

    default:
      if (Kind == Sema::CheckConstexprKind::Diagnose) {
        SemaRef.Diag(DS->getBeginLoc(), diag::err_constexpr_body_invalid_stmt)
            << isa<CXXConstructorDecl>(Dcl) << Dcl->isConsteval();
      }
      return false;
    }
  }

  return true;
}

/// Check that the given field is initialized within a constexpr constructor.
///
/// \param Dcl The constexpr constructor being checked.
/// \param Field The field being checked. This may be a member of an anonymous
///        struct or union nested within the class being checked.
/// \param Inits All declarations, including anonymous struct/union members and
///        indirect members, for which any initialization was provided.
/// \param Diagnosed Whether we've emitted the error message yet. Used to attach
///        multiple notes for different members to the same error.
/// \param Kind Whether we're diagnosing a constructor as written or determining
///        whether the formal requirements are satisfied.
/// \return \c false if we're checking for validity and the constructor does
///         not satisfy the requirements on a constexpr constructor.
static bool CheckConstexprCtorInitializer(Sema &SemaRef,
                                          const FunctionDecl *Dcl,
                                          FieldDecl *Field,
                                          llvm::SmallSet<Decl*, 16> &Inits,
                                          bool &Diagnosed,
                                          Sema::CheckConstexprKind Kind) {
  // In C++20 onwards, there's nothing to check for validity.
  if (Kind == Sema::CheckConstexprKind::CheckValid &&
      SemaRef.getLangOpts().CPlusPlus20)
    return true;

  if (Field->isInvalidDecl())
    return true;

  if (Field->isUnnamedBitField())
    return true;

  // Anonymous unions with no variant members and empty anonymous structs do not
  // need to be explicitly initialized. FIXME: Anonymous structs that contain no
  // indirect fields don't need initializing.
  if (Field->isAnonymousStructOrUnion() &&
      (Field->getType()->isUnionType()
           ? !Field->getType()->getAsCXXRecordDecl()->hasVariantMembers()
           : Field->getType()->getAsCXXRecordDecl()->isEmpty()))
    return true;

  if (!Inits.count(Field)) {
    if (Kind == Sema::CheckConstexprKind::Diagnose) {
      if (!Diagnosed) {
        SemaRef.Diag(Dcl->getLocation(),
                     SemaRef.getLangOpts().CPlusPlus20
                         ? diag::warn_cxx17_compat_constexpr_ctor_missing_init
                         : diag::ext_constexpr_ctor_missing_init);
        Diagnosed = true;
      }
      SemaRef.Diag(Field->getLocation(),
                   diag::note_constexpr_ctor_missing_init);
    } else if (!SemaRef.getLangOpts().CPlusPlus20) {
      return false;
    }
  } else if (Field->isAnonymousStructOrUnion()) {
    const RecordDecl *RD = Field->getType()->castAs<RecordType>()->getDecl();
    for (auto *I : RD->fields())
      // If an anonymous union contains an anonymous struct of which any member
      // is initialized, all members must be initialized.
      if (!RD->isUnion() || Inits.count(I))
        if (!CheckConstexprCtorInitializer(SemaRef, Dcl, I, Inits, Diagnosed,
                                           Kind))
          return false;
  }
  return true;
}

/// Check the provided statement is allowed in a constexpr function
/// definition.
static bool
CheckConstexprFunctionStmt(Sema &SemaRef, const FunctionDecl *Dcl, Stmt *S,
                           SmallVectorImpl<SourceLocation> &ReturnStmts,
                           SourceLocation &Cxx1yLoc, SourceLocation &Cxx2aLoc,
                           SourceLocation &Cxx2bLoc,
                           Sema::CheckConstexprKind Kind) {
  // - its function-body shall be [...] a compound-statement that contains only
  switch (S->getStmtClass()) {
  case Stmt::NullStmtClass:
    //   - null statements,
    return true;

  case Stmt::DeclStmtClass:
    //   - static_assert-declarations
    //   - using-declarations,
    //   - using-directives,
    //   - typedef declarations and alias-declarations that do not define
    //     classes or enumerations,
    if (!CheckConstexprDeclStmt(SemaRef, Dcl, cast<DeclStmt>(S), Cxx1yLoc, Kind))
      return false;
    return true;

  case Stmt::ReturnStmtClass:
    //   - and exactly one return statement;
    if (isa<CXXConstructorDecl>(Dcl)) {
      // C++1y allows return statements in constexpr constructors.
      if (!Cxx1yLoc.isValid())
        Cxx1yLoc = S->getBeginLoc();
      return true;
    }

    ReturnStmts.push_back(S->getBeginLoc());
    return true;

  case Stmt::AttributedStmtClass:
    // Attributes on a statement don't affect its formal kind and hence don't
    // affect its validity in a constexpr function.
    return CheckConstexprFunctionStmt(
        SemaRef, Dcl, cast<AttributedStmt>(S)->getSubStmt(), ReturnStmts,
        Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind);

  case Stmt::CompoundStmtClass: {
    // C++1y allows compound-statements.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();

    CompoundStmt *CompStmt = cast<CompoundStmt>(S);
    for (auto *BodyIt : CompStmt->body()) {
      if (!CheckConstexprFunctionStmt(SemaRef, Dcl, BodyIt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
        return false;
    }
    return true;
  }

  case Stmt::IfStmtClass: {
    // C++1y allows if-statements.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();

    IfStmt *If = cast<IfStmt>(S);
    if (!CheckConstexprFunctionStmt(SemaRef, Dcl, If->getThen(), ReturnStmts,
                                    Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
      return false;
    if (If->getElse() &&
        !CheckConstexprFunctionStmt(SemaRef, Dcl, If->getElse(), ReturnStmts,
                                    Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
      return false;
    return true;
  }

  case Stmt::WhileStmtClass:
  case Stmt::DoStmtClass:
  case Stmt::ForStmtClass:
  case Stmt::CXXForRangeStmtClass:
  case Stmt::ContinueStmtClass:
    // C++1y allows all of these. We don't allow them as extensions in C++11,
    // because they don't make sense without variable mutation.
    if (!SemaRef.getLangOpts().CPlusPlus14)
      break;
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();
    for (Stmt *SubStmt : S->children()) {
      if (SubStmt &&
          !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
        return false;
    }
    return true;

  case Stmt::SwitchStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::BreakStmtClass:
    // C++1y allows switch-statements, and since they don't need variable
    // mutation, we can reasonably allow them in C++11 as an extension.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();
    for (Stmt *SubStmt : S->children()) {
      if (SubStmt &&
          !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
        return false;
    }
    return true;

  case Stmt::LabelStmtClass:
  case Stmt::GotoStmtClass:
    if (Cxx2bLoc.isInvalid())
      Cxx2bLoc = S->getBeginLoc();
    for (Stmt *SubStmt : S->children()) {
      if (SubStmt &&
          !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
        return false;
    }
    return true;

  case Stmt::GCCAsmStmtClass:
  case Stmt::MSAsmStmtClass:
    // C++2a allows inline assembly statements.
  case Stmt::CXXTryStmtClass:
    if (Cxx2aLoc.isInvalid())
      Cxx2aLoc = S->getBeginLoc();
    for (Stmt *SubStmt : S->children()) {
      if (SubStmt &&
          !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
        return false;
    }
    return true;

  case Stmt::CXXCatchStmtClass:
    // Do not bother checking the language mode (already covered by the
    // try block check).
    if (!CheckConstexprFunctionStmt(
            SemaRef, Dcl, cast<CXXCatchStmt>(S)->getHandlerBlock(), ReturnStmts,
            Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
      return false;
    return true;

  default:
    if (!isa<Expr>(S))
      break;

    // C++1y allows expression-statements.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();
    return true;
  }

  if (Kind == Sema::CheckConstexprKind::Diagnose) {
    SemaRef.Diag(S->getBeginLoc(), diag::err_constexpr_body_invalid_stmt)
        << isa<CXXConstructorDecl>(Dcl) << Dcl->isConsteval();
  }
  return false;
}

/// Check the body for the given constexpr function declaration only contains
/// the permitted types of statement. C++11 [dcl.constexpr]p3,p4.
///
/// \return true if the body is OK, false if we have found or diagnosed a
/// problem.
static bool CheckConstexprFunctionBody(Sema &SemaRef, const FunctionDecl *Dcl,
                                       Stmt *Body,
                                       Sema::CheckConstexprKind Kind) {
  SmallVector<SourceLocation, 4> ReturnStmts;

  if (isa<CXXTryStmt>(Body)) {
    // C++11 [dcl.constexpr]p3:
    //  The definition of a constexpr function shall satisfy the following
    //  constraints: [...]
    // - its function-body shall be = delete, = default, or a
    //   compound-statement
    //
    // C++11 [dcl.constexpr]p4:
    //  In the definition of a constexpr constructor, [...]
    // - its function-body shall not be a function-try-block;
    //
    // This restriction is lifted in C++2a, as long as inner statements also
    // apply the general constexpr rules.
    switch (Kind) {
    case Sema::CheckConstexprKind::CheckValid:
      if (!SemaRef.getLangOpts().CPlusPlus20)
        return false;
      break;

    case Sema::CheckConstexprKind::Diagnose:
      SemaRef.Diag(Body->getBeginLoc(),
           !SemaRef.getLangOpts().CPlusPlus20
               ? diag::ext_constexpr_function_try_block_cxx20
               : diag::warn_cxx17_compat_constexpr_function_try_block)
          << isa<CXXConstructorDecl>(Dcl);
      break;
    }
  }

  // - its function-body shall be [...] a compound-statement that contains only
  //   [... list of cases ...]
  //
  // Note that walking the children here is enough to properly check for
  // CompoundStmt and CXXTryStmt body.
  SourceLocation Cxx1yLoc, Cxx2aLoc, Cxx2bLoc;
  for (Stmt *SubStmt : Body->children()) {
    if (SubStmt &&
        !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                    Cxx1yLoc, Cxx2aLoc, Cxx2bLoc, Kind))
      return false;
  }

  if (Kind == Sema::CheckConstexprKind::CheckValid) {
    // If this is only valid as an extension, report that we don't satisfy the
    // constraints of the current language.
    if ((Cxx2bLoc.isValid() && !SemaRef.getLangOpts().CPlusPlus23) ||
        (Cxx2aLoc.isValid() && !SemaRef.getLangOpts().CPlusPlus20) ||
        (Cxx1yLoc.isValid() && !SemaRef.getLangOpts().CPlusPlus17))
      return false;
  } else if (Cxx2bLoc.isValid()) {
    SemaRef.Diag(Cxx2bLoc,
                 SemaRef.getLangOpts().CPlusPlus23
                     ? diag::warn_cxx20_compat_constexpr_body_invalid_stmt
                     : diag::ext_constexpr_body_invalid_stmt_cxx23)
        << isa<CXXConstructorDecl>(Dcl);
  } else if (Cxx2aLoc.isValid()) {
    SemaRef.Diag(Cxx2aLoc,
         SemaRef.getLangOpts().CPlusPlus20
           ? diag::warn_cxx17_compat_constexpr_body_invalid_stmt
           : diag::ext_constexpr_body_invalid_stmt_cxx20)
      << isa<CXXConstructorDecl>(Dcl);
  } else if (Cxx1yLoc.isValid()) {
    SemaRef.Diag(Cxx1yLoc,
         SemaRef.getLangOpts().CPlusPlus14
           ? diag::warn_cxx11_compat_constexpr_body_invalid_stmt
           : diag::ext_constexpr_body_invalid_stmt)
      << isa<CXXConstructorDecl>(Dcl);
  }

  if (const CXXConstructorDecl *Constructor
        = dyn_cast<CXXConstructorDecl>(Dcl)) {
    const CXXRecordDecl *RD = Constructor->getParent();
    // DR1359:
    // - every non-variant non-static data member and base class sub-object
    //   shall be initialized;
    // DR1460:
    // - if the class is a union having variant members, exactly one of them
    //   shall be initialized;
    if (RD->isUnion()) {
      if (Constructor->getNumCtorInitializers() == 0 &&
          RD->hasVariantMembers()) {
        if (Kind == Sema::CheckConstexprKind::Diagnose) {
          SemaRef.Diag(
              Dcl->getLocation(),
              SemaRef.getLangOpts().CPlusPlus20
                  ? diag::warn_cxx17_compat_constexpr_union_ctor_no_init
                  : diag::ext_constexpr_union_ctor_no_init);
        } else if (!SemaRef.getLangOpts().CPlusPlus20) {
          return false;
        }
      }
    } else if (!Constructor->isDependentContext() &&
               !Constructor->isDelegatingConstructor()) {
      assert(RD->getNumVBases() == 0 && "constexpr ctor with virtual bases");

      // Skip detailed checking if we have enough initializers, and we would
      // allow at most one initializer per member.
      bool AnyAnonStructUnionMembers = false;
      unsigned Fields = 0;
      for (CXXRecordDecl::field_iterator I = RD->field_begin(),
           E = RD->field_end(); I != E; ++I, ++Fields) {
        if (I->isAnonymousStructOrUnion()) {
          AnyAnonStructUnionMembers = true;
          break;
        }
      }
      // DR1460:
      // - if the class is a union-like class, but is not a union, for each of
      //   its anonymous union members having variant members, exactly one of
      //   them shall be initialized;
      if (AnyAnonStructUnionMembers ||
          Constructor->getNumCtorInitializers() != RD->getNumBases() + Fields) {
        // Check initialization of non-static data members. Base classes are
        // always initialized so do not need to be checked. Dependent bases
        // might not have initializers in the member initializer list.
        llvm::SmallSet<Decl*, 16> Inits;
        for (const auto *I: Constructor->inits()) {
          if (FieldDecl *FD = I->getMember())
            Inits.insert(FD);
          else if (IndirectFieldDecl *ID = I->getIndirectMember())
            Inits.insert(ID->chain_begin(), ID->chain_end());
        }

        bool Diagnosed = false;
        for (auto *I : RD->fields())
          if (!CheckConstexprCtorInitializer(SemaRef, Dcl, I, Inits, Diagnosed,
                                             Kind))
            return false;
      }
    }
  } else {
    if (ReturnStmts.empty()) {
      switch (Kind) {
      case Sema::CheckConstexprKind::Diagnose:
        if (!CheckConstexprMissingReturn(SemaRef, Dcl))
          return false;
        break;

      case Sema::CheckConstexprKind::CheckValid:
        // The formal requirements don't include this rule in C++14, even
        // though the "must be able to produce a constant expression" rules
        // still imply it in some cases.
        if (!SemaRef.getLangOpts().CPlusPlus14)
          return false;
        break;
      }
    } else if (ReturnStmts.size() > 1) {
      switch (Kind) {
      case Sema::CheckConstexprKind::Diagnose:
        SemaRef.Diag(
            ReturnStmts.back(),
            SemaRef.getLangOpts().CPlusPlus14
                ? diag::warn_cxx11_compat_constexpr_body_multiple_return
                : diag::ext_constexpr_body_multiple_return);
        for (unsigned I = 0; I < ReturnStmts.size() - 1; ++I)
          SemaRef.Diag(ReturnStmts[I],
                       diag::note_constexpr_body_previous_return);
        break;

      case Sema::CheckConstexprKind::CheckValid:
        if (!SemaRef.getLangOpts().CPlusPlus14)
          return false;
        break;
      }
    }
  }

  // C++11 [dcl.constexpr]p5:
  //   if no function argument values exist such that the function invocation
  //   substitution would produce a constant expression, the program is
  //   ill-formed; no diagnostic required.
  // C++11 [dcl.constexpr]p3:
  //   - every constructor call and implicit conversion used in initializing the
  //     return value shall be one of those allowed in a constant expression.
  // C++11 [dcl.constexpr]p4:
  //   - every constructor involved in initializing non-static data members and
  //     base class sub-objects shall be a constexpr constructor.
  //
  // Note that this rule is distinct from the "requirements for a constexpr
  // function", so is not checked in CheckValid mode. Because the check for
  // constexpr potential is expensive, skip the check if the diagnostic is
  // disabled, the function is declared in a system header, or we're in C++23
  // or later mode (see https://wg21.link/P2448).
  bool SkipCheck =
      !SemaRef.getLangOpts().CheckConstexprFunctionBodies ||
      SemaRef.getSourceManager().isInSystemHeader(Dcl->getLocation()) ||
      SemaRef.getDiagnostics().isIgnored(
          diag::ext_constexpr_function_never_constant_expr, Dcl->getLocation());
  SmallVector<PartialDiagnosticAt, 8> Diags;
  if (Kind == Sema::CheckConstexprKind::Diagnose && !SkipCheck &&
      !Expr::isPotentialConstantExpr(Dcl, Diags)) {
    SemaRef.Diag(Dcl->getLocation(),
                 diag::ext_constexpr_function_never_constant_expr)
        << isa<CXXConstructorDecl>(Dcl) << Dcl->isConsteval()
        << Dcl->getNameInfo().getSourceRange();
    for (size_t I = 0, N = Diags.size(); I != N; ++I)
      SemaRef.Diag(Diags[I].first, Diags[I].second);
    // Don't return false here: we allow this for compatibility in
    // system headers.
  }

  return true;
}

static bool CheckConstexprMissingReturn(Sema &SemaRef,
                                        const FunctionDecl *Dcl) {
  bool IsVoidOrDependentType = Dcl->getReturnType()->isVoidType() ||
                               Dcl->getReturnType()->isDependentType();
  // Skip emitting a missing return error diagnostic for non-void functions
  // since C++23 no longer mandates constexpr functions to yield constant
  // expressions.
  if (SemaRef.getLangOpts().CPlusPlus23 && !IsVoidOrDependentType)
    return true;

  // C++14 doesn't require constexpr functions to contain a 'return'
  // statement. We still do, unless the return type might be void, because
  // otherwise if there's no return statement, the function cannot
  // be used in a core constant expression.
  bool OK = SemaRef.getLangOpts().CPlusPlus14 && IsVoidOrDependentType;
  SemaRef.Diag(Dcl->getLocation(),
               OK ? diag::warn_cxx11_compat_constexpr_body_no_return
                  : diag::err_constexpr_body_no_return)
      << Dcl->isConsteval();
  return OK;
}

bool Sema::CheckImmediateEscalatingFunctionDefinition(
    FunctionDecl *FD, const sema::FunctionScopeInfo *FSI) {
  if (!getLangOpts().CPlusPlus20 || !FD->isImmediateEscalating())
    return true;
  FD->setBodyContainsImmediateEscalatingExpressions(
      FSI->FoundImmediateEscalatingExpression);
  if (FSI->FoundImmediateEscalatingExpression) {
    auto it = UndefinedButUsed.find(FD->getCanonicalDecl());
    if (it != UndefinedButUsed.end()) {
      Diag(it->second, diag::err_immediate_function_used_before_definition)
          << it->first;
      Diag(FD->getLocation(), diag::note_defined_here) << FD;
      if (FD->isImmediateFunction() && !FD->isConsteval())
        DiagnoseImmediateEscalatingReason(FD);
      return false;
    }
  }
  return true;
}

void Sema::DiagnoseImmediateEscalatingReason(FunctionDecl *FD) {
  assert(FD->isImmediateEscalating() && !FD->isConsteval() &&
         "expected an immediate function");
  assert(FD->hasBody() && "expected the function to have a body");
  struct ImmediateEscalatingExpressionsVisitor
      : public RecursiveASTVisitor<ImmediateEscalatingExpressionsVisitor> {

    using Base = RecursiveASTVisitor<ImmediateEscalatingExpressionsVisitor>;
    Sema &SemaRef;

    const FunctionDecl *ImmediateFn;
    bool ImmediateFnIsConstructor;
    CXXConstructorDecl *CurrentConstructor = nullptr;
    CXXCtorInitializer *CurrentInit = nullptr;

    ImmediateEscalatingExpressionsVisitor(Sema &SemaRef, FunctionDecl *FD)
        : SemaRef(SemaRef), ImmediateFn(FD),
          ImmediateFnIsConstructor(isa<CXXConstructorDecl>(FD)) {}

    bool shouldVisitImplicitCode() const { return true; }
    bool shouldVisitLambdaBody() const { return false; }

    void Diag(const Expr *E, const FunctionDecl *Fn, bool IsCall) {
      SourceLocation Loc = E->getBeginLoc();
      SourceRange Range = E->getSourceRange();
      if (CurrentConstructor && CurrentInit) {
        Loc = CurrentConstructor->getLocation();
        Range = CurrentInit->isWritten() ? CurrentInit->getSourceRange()
                                         : SourceRange();
      }

      FieldDecl* InitializedField = CurrentInit ? CurrentInit->getAnyMember() : nullptr;

      SemaRef.Diag(Loc, diag::note_immediate_function_reason)
          << ImmediateFn << Fn << Fn->isConsteval() << IsCall
          << isa<CXXConstructorDecl>(Fn) << ImmediateFnIsConstructor
          << (InitializedField != nullptr)
          << (CurrentInit && !CurrentInit->isWritten())
          << InitializedField << Range;
    }
    bool TraverseCallExpr(CallExpr *E) {
      if (const auto *DR =
              dyn_cast<DeclRefExpr>(E->getCallee()->IgnoreImplicit());
          DR && DR->isImmediateEscalating()) {
        Diag(E, E->getDirectCallee(), /*IsCall=*/true);
        return false;
      }

      for (Expr *A : E->arguments())
        if (!getDerived().TraverseStmt(A))
          return false;

      return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr *E) {
      if (const auto *ReferencedFn = dyn_cast<FunctionDecl>(E->getDecl());
          ReferencedFn && E->isImmediateEscalating()) {
        Diag(E, ReferencedFn, /*IsCall=*/false);
        return false;
      }

      return true;
    }

    bool VisitCXXConstructExpr(CXXConstructExpr *E) {
      CXXConstructorDecl *D = E->getConstructor();
      if (E->isImmediateEscalating()) {
        Diag(E, D, /*IsCall=*/true);
        return false;
      }
      return true;
    }

    bool TraverseConstructorInitializer(CXXCtorInitializer *Init) {
      llvm::SaveAndRestore RAII(CurrentInit, Init);
      return Base::TraverseConstructorInitializer(Init);
    }

    bool TraverseCXXConstructorDecl(CXXConstructorDecl *Ctr) {
      llvm::SaveAndRestore RAII(CurrentConstructor, Ctr);
      return Base::TraverseCXXConstructorDecl(Ctr);
    }

    bool TraverseType(QualType T) { return true; }
    bool VisitBlockExpr(BlockExpr *T) { return true; }

  } Visitor(*this, FD);
  Visitor.TraverseDecl(FD);
}

CXXRecordDecl *Sema::getCurrentClass(Scope *, const CXXScopeSpec *SS) {
  assert(getLangOpts().CPlusPlus && "No class names in C!");

  if (SS && SS->isInvalid())
    return nullptr;

  if (SS && SS->isNotEmpty()) {
    DeclContext *DC = computeDeclContext(*SS, true);
    return dyn_cast_or_null<CXXRecordDecl>(DC);
  }

  return dyn_cast_or_null<CXXRecordDecl>(CurContext);
}

bool Sema::isCurrentClassName(const IdentifierInfo &II, Scope *S,
                              const CXXScopeSpec *SS) {
  CXXRecordDecl *CurDecl = getCurrentClass(S, SS);
  return CurDecl && &II == CurDecl->getIdentifier();
}

bool Sema::isCurrentClassNameTypo(IdentifierInfo *&II, const CXXScopeSpec *SS) {
  assert(getLangOpts().CPlusPlus && "No class names in C!");

  if (!getLangOpts().SpellChecking)
    return false;

  CXXRecordDecl *CurDecl;
  if (SS && SS->isSet() && !SS->isInvalid()) {
    DeclContext *DC = computeDeclContext(*SS, true);
    CurDecl = dyn_cast_or_null<CXXRecordDecl>(DC);
  } else
    CurDecl = dyn_cast_or_null<CXXRecordDecl>(CurContext);

  if (CurDecl && CurDecl->getIdentifier() && II != CurDecl->getIdentifier() &&
      3 * II->getName().edit_distance(CurDecl->getIdentifier()->getName())
          < II->getLength()) {
    II = CurDecl->getIdentifier();
    return true;
  }

  return false;
}

CXXBaseSpecifier *Sema::CheckBaseSpecifier(CXXRecordDecl *Class,
                                           SourceRange SpecifierRange,
                                           bool Virtual, AccessSpecifier Access,
                                           TypeSourceInfo *TInfo,
                                           SourceLocation EllipsisLoc) {
  QualType BaseType = TInfo->getType();
  SourceLocation BaseLoc = TInfo->getTypeLoc().getBeginLoc();
  if (BaseType->containsErrors()) {
    // Already emitted a diagnostic when parsing the error type.
    return nullptr;
  }

  if (EllipsisLoc.isValid() && !BaseType->containsUnexpandedParameterPack()) {
    Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
      << TInfo->getTypeLoc().getSourceRange();
    EllipsisLoc = SourceLocation();
  }

  auto *BaseDecl =
      dyn_cast_if_present<CXXRecordDecl>(computeDeclContext(BaseType));
  // C++ [class.derived.general]p2:
  //   A class-or-decltype shall denote a (possibly cv-qualified) class type
  //   that is not an incompletely defined class; any cv-qualifiers are
  //   ignored.
  if (BaseDecl) {
    // C++ [class.union.general]p4:
    // [...]  A union shall not be used as a base class.
    if (BaseDecl->isUnion()) {
      Diag(BaseLoc, diag::err_union_as_base_class) << SpecifierRange;
      return nullptr;
    }

    // For the MS ABI, propagate DLL attributes to base class templates.
    if (Context.getTargetInfo().getCXXABI().isMicrosoft() ||
        Context.getTargetInfo().getTriple().isPS()) {
      if (Attr *ClassAttr = getDLLAttr(Class)) {
        if (auto *BaseSpec =
                dyn_cast<ClassTemplateSpecializationDecl>(BaseDecl)) {
          propagateDLLAttrToBaseClassTemplate(Class, ClassAttr, BaseSpec,
                                              BaseLoc);
        }
      }
    }

    if (RequireCompleteType(BaseLoc, BaseType, diag::err_incomplete_base_class,
                            SpecifierRange)) {
      Class->setInvalidDecl();
      return nullptr;
    }

    BaseDecl = BaseDecl->getDefinition();
    assert(BaseDecl && "Base type is not incomplete, but has no definition");

    // Microsoft docs say:
    // "If a base-class has a code_seg attribute, derived classes must have the
    // same attribute."
    const auto *BaseCSA = BaseDecl->getAttr<CodeSegAttr>();
    const auto *DerivedCSA = Class->getAttr<CodeSegAttr>();
    if ((DerivedCSA || BaseCSA) &&
        (!BaseCSA || !DerivedCSA ||
         BaseCSA->getName() != DerivedCSA->getName())) {
      Diag(Class->getLocation(), diag::err_mismatched_code_seg_base);
      Diag(BaseDecl->getLocation(), diag::note_base_class_specified_here)
          << BaseDecl;
      return nullptr;
    }

    // A class which contains a flexible array member is not suitable for use as
    // a base class:
    //   - If the layout determines that a base comes before another base,
    //     the flexible array member would index into the subsequent base.
    //   - If the layout determines that base comes before the derived class,
    //     the flexible array member would index into the derived class.
    if (BaseDecl->hasFlexibleArrayMember()) {
      Diag(BaseLoc, diag::err_base_class_has_flexible_array_member)
          << BaseDecl->getDeclName();
      return nullptr;
    }

    // C++ [class]p3:
    //   If a class is marked final and it appears as a base-type-specifier in
    //   base-clause, the program is ill-formed.
    if (FinalAttr *FA = BaseDecl->getAttr<FinalAttr>()) {
      Diag(BaseLoc, diag::err_class_marked_final_used_as_base)
          << BaseDecl->getDeclName() << FA->isSpelledAsSealed();
      Diag(BaseDecl->getLocation(), diag::note_entity_declared_at)
          << BaseDecl->getDeclName() << FA->getRange();
      return nullptr;
    }

    // If the base class is invalid the derived class is as well.
    if (BaseDecl->isInvalidDecl())
      Class->setInvalidDecl();
  } else if (BaseType->isDependentType()) {
    // Make sure that we don't make an ill-formed AST where the type of the
    // Class is non-dependent and its attached base class specifier is an
    // dependent type, which violates invariants in many clang code paths (e.g.
    // constexpr evaluator). If this case happens (in errory-recovery mode), we
    // explicitly mark the Class decl invalid. The diagnostic was already
    // emitted.
    if (!Class->isDependentContext())
      Class->setInvalidDecl();
  } else {
    // The base class is some non-dependent non-class type.
    Diag(BaseLoc, diag::err_base_must_be_class) << SpecifierRange;
    return nullptr;
  }

  // In HLSL, unspecified class access is public rather than private.
  if (getLangOpts().HLSL && Class->getTagKind() == TagTypeKind::Class &&
      Access == AS_none)
    Access = AS_public;

  // Create the base specifier.
  return new (Context) CXXBaseSpecifier(
      SpecifierRange, Virtual, Class->getTagKind() == TagTypeKind::Class,
      Access, TInfo, EllipsisLoc);
}

BaseResult Sema::ActOnBaseSpecifier(Decl *classdecl, SourceRange SpecifierRange,
                                    const ParsedAttributesView &Attributes,
                                    bool Virtual, AccessSpecifier Access,
                                    ParsedType basetype, SourceLocation BaseLoc,
                                    SourceLocation EllipsisLoc) {
  if (!classdecl)
    return true;

  AdjustDeclIfTemplate(classdecl);
  CXXRecordDecl *Class = dyn_cast<CXXRecordDecl>(classdecl);
  if (!Class)
    return true;

  // We haven't yet attached the base specifiers.
  Class->setIsParsingBaseSpecifiers();

  // We do not support any C++11 attributes on base-specifiers yet.
  // Diagnose any attributes we see.
  for (const ParsedAttr &AL : Attributes) {
    if (AL.isInvalid() || AL.getKind() == ParsedAttr::IgnoredAttribute)
      continue;
    if (AL.getKind() == ParsedAttr::UnknownAttribute)
      Diag(AL.getLoc(), diag::warn_unknown_attribute_ignored)
          << AL << AL.getRange();
    else
      Diag(AL.getLoc(), diag::err_base_specifier_attribute)
          << AL << AL.isRegularKeywordAttribute() << AL.getRange();
  }

  TypeSourceInfo *TInfo = nullptr;
  GetTypeFromParser(basetype, &TInfo);

  if (EllipsisLoc.isInvalid() &&
      DiagnoseUnexpandedParameterPack(SpecifierRange.getBegin(), TInfo,
                                      UPPC_BaseType))
    return true;

  // C++ [class.union.general]p4:
  //   [...] A union shall not have base classes.
  if (Class->isUnion()) {
    Diag(Class->getLocation(), diag::err_base_clause_on_union)
        << SpecifierRange;
    return true;
  }

  if (CXXBaseSpecifier *BaseSpec = CheckBaseSpecifier(Class, SpecifierRange,
                                                      Virtual, Access, TInfo,
                                                      EllipsisLoc))
    return BaseSpec;

  Class->setInvalidDecl();
  return true;
}

/// Use small set to collect indirect bases.  As this is only used
/// locally, there's no need to abstract the small size parameter.
typedef llvm::SmallPtrSet<QualType, 4> IndirectBaseSet;

/// Recursively add the bases of Type.  Don't add Type itself.
static void
NoteIndirectBases(ASTContext &Context, IndirectBaseSet &Set,
                  const QualType &Type)
{
  // Even though the incoming type is a base, it might not be
  // a class -- it could be a template parm, for instance.
  if (auto Rec = Type->getAs<RecordType>()) {
    auto Decl = Rec->getAsCXXRecordDecl();

    // Iterate over its bases.
    for (const auto &BaseSpec : Decl->bases()) {
      QualType Base = Context.getCanonicalType(BaseSpec.getType())
        .getUnqualifiedType();
      if (Set.insert(Base).second)
        // If we've not already seen it, recurse.
        NoteIndirectBases(Context, Set, Base);
    }
  }
}

bool Sema::AttachBaseSpecifiers(CXXRecordDecl *Class,
                                MutableArrayRef<CXXBaseSpecifier *> Bases) {
 if (Bases.empty())
    return false;

  // Used to keep track of which base types we have already seen, so
  // that we can properly diagnose redundant direct base types. Note
  // that the key is always the unqualified canonical type of the base
  // class.
  std::map<QualType, CXXBaseSpecifier*, QualTypeOrdering> KnownBaseTypes;

  // Used to track indirect bases so we can see if a direct base is
  // ambiguous.
  IndirectBaseSet IndirectBaseTypes;

  // Copy non-redundant base specifiers into permanent storage.
  unsigned NumGoodBases = 0;
  bool Invalid = false;
  for (unsigned idx = 0; idx < Bases.size(); ++idx) {
    QualType NewBaseType
      = Context.getCanonicalType(Bases[idx]->getType());
    NewBaseType = NewBaseType.getLocalUnqualifiedType();

    CXXBaseSpecifier *&KnownBase = KnownBaseTypes[NewBaseType];
    if (KnownBase) {
      // C++ [class.mi]p3:
      //   A class shall not be specified as a direct base class of a
      //   derived class more than once.
      Diag(Bases[idx]->getBeginLoc(), diag::err_duplicate_base_class)
          << KnownBase->getType() << Bases[idx]->getSourceRange();

      // Delete the duplicate base class specifier; we're going to
      // overwrite its pointer later.
      Context.Deallocate(Bases[idx]);

      Invalid = true;
    } else {
      // Okay, add this new base class.
      KnownBase = Bases[idx];
      Bases[NumGoodBases++] = Bases[idx];

      if (NewBaseType->isDependentType())
        continue;
      // Note this base's direct & indirect bases, if there could be ambiguity.
      if (Bases.size() > 1)
        NoteIndirectBases(Context, IndirectBaseTypes, NewBaseType);

      if (const RecordType *Record = NewBaseType->getAs<RecordType>()) {
        const CXXRecordDecl *RD = cast<CXXRecordDecl>(Record->getDecl());
        if (Class->isInterface() &&
              (!RD->isInterfaceLike() ||
               KnownBase->getAccessSpecifier() != AS_public)) {
          // The Microsoft extension __interface does not permit bases that
          // are not themselves public interfaces.
          Diag(KnownBase->getBeginLoc(), diag::err_invalid_base_in_interface)
              << getRecordDiagFromTagKind(RD->getTagKind()) << RD
              << RD->getSourceRange();
          Invalid = true;
        }
        if (RD->hasAttr<WeakAttr>())
          Class->addAttr(WeakAttr::CreateImplicit(Context));
      }
    }
  }

  // Attach the remaining base class specifiers to the derived class.
  Class->setBases(Bases.data(), NumGoodBases);

  // Check that the only base classes that are duplicate are virtual.
  for (unsigned idx = 0; idx < NumGoodBases; ++idx) {
    // Check whether this direct base is inaccessible due to ambiguity.
    QualType BaseType = Bases[idx]->getType();

    // Skip all dependent types in templates being used as base specifiers.
    // Checks below assume that the base specifier is a CXXRecord.
    if (BaseType->isDependentType())
      continue;

    CanQualType CanonicalBase = Context.getCanonicalType(BaseType)
      .getUnqualifiedType();

    if (IndirectBaseTypes.count(CanonicalBase)) {
      CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                         /*DetectVirtual=*/true);
      bool found
        = Class->isDerivedFrom(CanonicalBase->getAsCXXRecordDecl(), Paths);
      assert(found);
      (void)found;

      if (Paths.isAmbiguous(CanonicalBase))
        Diag(Bases[idx]->getBeginLoc(), diag::warn_inaccessible_base_class)
            << BaseType << getAmbiguousPathsDisplayString(Paths)
            << Bases[idx]->getSourceRange();
      else
        assert(Bases[idx]->isVirtual());
    }

    // Delete the base class specifier, since its data has been copied
    // into the CXXRecordDecl.
    Context.Deallocate(Bases[idx]);
  }

  return Invalid;
}

void Sema::ActOnBaseSpecifiers(Decl *ClassDecl,
                               MutableArrayRef<CXXBaseSpecifier *> Bases) {
  if (!ClassDecl || Bases.empty())
    return;

  AdjustDeclIfTemplate(ClassDecl);
  AttachBaseSpecifiers(cast<CXXRecordDecl>(ClassDecl), Bases);
}

bool Sema::IsDerivedFrom(SourceLocation Loc, QualType Derived, QualType Base) {
  if (!getLangOpts().CPlusPlus)
    return false;

  CXXRecordDecl *DerivedRD = Derived->getAsCXXRecordDecl();
  if (!DerivedRD)
    return false;

  CXXRecordDecl *BaseRD = Base->getAsCXXRecordDecl();
  if (!BaseRD)
    return false;

  // If either the base or the derived type is invalid, don't try to
  // check whether one is derived from the other.
  if (BaseRD->isInvalidDecl() || DerivedRD->isInvalidDecl())
    return false;

  // FIXME: In a modules build, do we need the entire path to be visible for us
  // to be able to use the inheritance relationship?
  if (!isCompleteType(Loc, Derived) && !DerivedRD->isBeingDefined())
    return false;

  return DerivedRD->isDerivedFrom(BaseRD);
}

bool Sema::IsDerivedFrom(SourceLocation Loc, QualType Derived, QualType Base,
                         CXXBasePaths &Paths) {
  if (!getLangOpts().CPlusPlus)
    return false;

  CXXRecordDecl *DerivedRD = Derived->getAsCXXRecordDecl();
  if (!DerivedRD)
    return false;

  CXXRecordDecl *BaseRD = Base->getAsCXXRecordDecl();
  if (!BaseRD)
    return false;

  if (!isCompleteType(Loc, Derived) && !DerivedRD->isBeingDefined())
    return false;

  return DerivedRD->isDerivedFrom(BaseRD, Paths);
}

static void BuildBasePathArray(const CXXBasePath &Path,
                               CXXCastPath &BasePathArray) {
  // We first go backward and check if we have a virtual base.
  // FIXME: It would be better if CXXBasePath had the base specifier for
  // the nearest virtual base.
  unsigned Start = 0;
  for (unsigned I = Path.size(); I != 0; --I) {
    if (Path[I - 1].Base->isVirtual()) {
      Start = I - 1;
      break;
    }
  }

  // Now add all bases.
  for (unsigned I = Start, E = Path.size(); I != E; ++I)
    BasePathArray.push_back(const_cast<CXXBaseSpecifier*>(Path[I].Base));
}


void Sema::BuildBasePathArray(const CXXBasePaths &Paths,
                              CXXCastPath &BasePathArray) {
  assert(BasePathArray.empty() && "Base path array must be empty!");
  assert(Paths.isRecordingPaths() && "Must record paths!");
  return ::BuildBasePathArray(Paths.front(), BasePathArray);
}

bool
Sema::CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                   unsigned InaccessibleBaseID,
                                   unsigned AmbiguousBaseConvID,
                                   SourceLocation Loc, SourceRange Range,
                                   DeclarationName Name,
                                   CXXCastPath *BasePath,
                                   bool IgnoreAccess) {
  // First, determine whether the path from Derived to Base is
  // ambiguous. This is slightly more expensive than checking whether
  // the Derived to Base conversion exists, because here we need to
  // explore multiple paths to determine if there is an ambiguity.
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  bool DerivationOkay = IsDerivedFrom(Loc, Derived, Base, Paths);
  if (!DerivationOkay)
    return true;

  const CXXBasePath *Path = nullptr;
  if (!Paths.isAmbiguous(Context.getCanonicalType(Base).getUnqualifiedType()))
    Path = &Paths.front();

  // For MSVC compatibility, check if Derived directly inherits from Base. Clang
  // warns about this hierarchy under -Winaccessible-base, but MSVC allows the
  // user to access such bases.
  if (!Path && getLangOpts().MSVCCompat) {
    for (const CXXBasePath &PossiblePath : Paths) {
      if (PossiblePath.size() == 1) {
        Path = &PossiblePath;
        if (AmbiguousBaseConvID)
          Diag(Loc, diag::ext_ms_ambiguous_direct_base)
              << Base << Derived << Range;
        break;
      }
    }
  }

  if (Path) {
    if (!IgnoreAccess) {
      // Check that the base class can be accessed.
      switch (
          CheckBaseClassAccess(Loc, Base, Derived, *Path, InaccessibleBaseID)) {
      case AR_inaccessible:
        return true;
      case AR_accessible:
      case AR_dependent:
      case AR_delayed:
        break;
      }
    }

    // Build a base path if necessary.
    if (BasePath)
      ::BuildBasePathArray(*Path, *BasePath);
    return false;
  }

  if (AmbiguousBaseConvID) {
    // We know that the derived-to-base conversion is ambiguous, and
    // we're going to produce a diagnostic. Perform the derived-to-base
    // search just one more time to compute all of the possible paths so
    // that we can print them out. This is more expensive than any of
    // the previous derived-to-base checks we've done, but at this point
    // performance isn't as much of an issue.
    Paths.clear();
    Paths.setRecordingPaths(true);
    bool StillOkay = IsDerivedFrom(Loc, Derived, Base, Paths);
    assert(StillOkay && "Can only be used with a derived-to-base conversion");
    (void)StillOkay;

    // Build up a textual representation of the ambiguous paths, e.g.,
    // D -> B -> A, that will be used to illustrate the ambiguous
    // conversions in the diagnostic. We only print one of the paths
    // to each base class subobject.
    std::string PathDisplayStr = getAmbiguousPathsDisplayString(Paths);

    Diag(Loc, AmbiguousBaseConvID)
    << Derived << Base << PathDisplayStr << Range << Name;
  }
  return true;
}

bool
Sema::CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                   SourceLocation Loc, SourceRange Range,
                                   CXXCastPath *BasePath,
                                   bool IgnoreAccess) {
  return CheckDerivedToBaseConversion(
      Derived, Base, diag::err_upcast_to_inaccessible_base,
      diag::err_ambiguous_derived_to_base_conv, Loc, Range, DeclarationName(),
      BasePath, IgnoreAccess);
}

std::string Sema::getAmbiguousPathsDisplayString(CXXBasePaths &Paths) {
  std::string PathDisplayStr;
  std::set<unsigned> DisplayedPaths;
  for (CXXBasePaths::paths_iterator Path = Paths.begin();
       Path != Paths.end(); ++Path) {
    if (DisplayedPaths.insert(Path->back().SubobjectNumber).second) {
      // We haven't displayed a path to this particular base
      // class subobject yet.
      PathDisplayStr += "\n    ";
      PathDisplayStr += Context.getTypeDeclType(Paths.getOrigin()).getAsString();
      for (CXXBasePath::const_iterator Element = Path->begin();
           Element != Path->end(); ++Element)
        PathDisplayStr += " -> " + Element->Base->getType().getAsString();
    }
  }

  return PathDisplayStr;
}

//===----------------------------------------------------------------------===//
// C++ class member Handling
//===----------------------------------------------------------------------===//

bool Sema::ActOnAccessSpecifier(AccessSpecifier Access, SourceLocation ASLoc,
                                SourceLocation ColonLoc,
                                const ParsedAttributesView &Attrs) {
  assert(Access != AS_none && "Invalid kind for syntactic access specifier!");
  AccessSpecDecl *ASDecl = AccessSpecDecl::Create(Context, Access, CurContext,
                                                  ASLoc, ColonLoc);
  CurContext->addHiddenDecl(ASDecl);
  return ProcessAccessDeclAttributeList(ASDecl, Attrs);
}

void Sema::CheckOverrideControl(NamedDecl *D) {
  if (D->isInvalidDecl())
    return;

  // We only care about "override" and "final" declarations.
  if (!D->hasAttr<OverrideAttr>() && !D->hasAttr<FinalAttr>())
    return;

  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D);

  // We can't check dependent instance methods.
  if (MD && MD->isInstance() &&
      (MD->getParent()->hasAnyDependentBases() ||
       MD->getType()->isDependentType()))
    return;

  if (MD && !MD->isVirtual()) {
    // If we have a non-virtual method, check if it hides a virtual method.
    // (In that case, it's most likely the method has the wrong type.)
    SmallVector<CXXMethodDecl *, 8> OverloadedMethods;
    FindHiddenVirtualMethods(MD, OverloadedMethods);

    if (!OverloadedMethods.empty()) {
      if (OverrideAttr *OA = D->getAttr<OverrideAttr>()) {
        Diag(OA->getLocation(),
             diag::override_keyword_hides_virtual_member_function)
          << "override" << (OverloadedMethods.size() > 1);
      } else if (FinalAttr *FA = D->getAttr<FinalAttr>()) {
        Diag(FA->getLocation(),
             diag::override_keyword_hides_virtual_member_function)
          << (FA->isSpelledAsSealed() ? "sealed" : "final")
          << (OverloadedMethods.size() > 1);
      }
      NoteHiddenVirtualMethods(MD, OverloadedMethods);
      MD->setInvalidDecl();
      return;
    }
    // Fall through into the general case diagnostic.
    // FIXME: We might want to attempt typo correction here.
  }

  if (!MD || !MD->isVirtual()) {
    if (OverrideAttr *OA = D->getAttr<OverrideAttr>()) {
      Diag(OA->getLocation(),
           diag::override_keyword_only_allowed_on_virtual_member_functions)
        << "override" << FixItHint::CreateRemoval(OA->getLocation());
      D->dropAttr<OverrideAttr>();
    }
    if (FinalAttr *FA = D->getAttr<FinalAttr>()) {
      Diag(FA->getLocation(),
           diag::override_keyword_only_allowed_on_virtual_member_functions)
        << (FA->isSpelledAsSealed() ? "sealed" : "final")
        << FixItHint::CreateRemoval(FA->getLocation());
      D->dropAttr<FinalAttr>();
    }
    return;
  }

  // C++11 [class.virtual]p5:
  //   If a function is marked with the virt-specifier override and
  //   does not override a member function of a base class, the program is
  //   ill-formed.
  bool HasOverriddenMethods = MD->size_overridden_methods() != 0;
  if (MD->hasAttr<OverrideAttr>() && !HasOverriddenMethods)
    Diag(MD->getLocation(), diag::err_function_marked_override_not_overriding)
      << MD->getDeclName();
}

void Sema::DiagnoseAbsenceOfOverrideControl(NamedDecl *D, bool Inconsistent) {
  if (D->isInvalidDecl() || D->hasAttr<OverrideAttr>())
    return;
  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D);
  if (!MD || MD->isImplicit() || MD->hasAttr<FinalAttr>())
    return;

  SourceLocation Loc = MD->getLocation();
  SourceLocation SpellingLoc = Loc;
  if (getSourceManager().isMacroArgExpansion(Loc))
    SpellingLoc = getSourceManager().getImmediateExpansionRange(Loc).getBegin();
  SpellingLoc = getSourceManager().getSpellingLoc(SpellingLoc);
  if (SpellingLoc.isValid() && getSourceManager().isInSystemHeader(SpellingLoc))
      return;

  if (MD->size_overridden_methods() > 0) {
    auto EmitDiag = [&](unsigned DiagInconsistent, unsigned DiagSuggest) {
      unsigned DiagID =
          Inconsistent && !Diags.isIgnored(DiagInconsistent, MD->getLocation())
              ? DiagInconsistent
              : DiagSuggest;
      Diag(MD->getLocation(), DiagID) << MD->getDeclName();
      const CXXMethodDecl *OMD = *MD->begin_overridden_methods();
      Diag(OMD->getLocation(), diag::note_overridden_virtual_function);
    };
    if (isa<CXXDestructorDecl>(MD))
      EmitDiag(
          diag::warn_inconsistent_destructor_marked_not_override_overriding,
          diag::warn_suggest_destructor_marked_not_override_overriding);
    else
      EmitDiag(diag::warn_inconsistent_function_marked_not_override_overriding,
               diag::warn_suggest_function_marked_not_override_overriding);
  }
}

bool Sema::CheckIfOverriddenFunctionIsMarkedFinal(const CXXMethodDecl *New,
                                                  const CXXMethodDecl *Old) {
  FinalAttr *FA = Old->getAttr<FinalAttr>();
  if (!FA)
    return false;

  Diag(New->getLocation(), diag::err_final_function_overridden)
    << New->getDeclName()
    << FA->isSpelledAsSealed();
  Diag(Old->getLocation(), diag::note_overridden_virtual_function);
  return true;
}

static bool InitializationHasSideEffects(const FieldDecl &FD) {
  const Type *T = FD.getType()->getBaseElementTypeUnsafe();
  // FIXME: Destruction of ObjC lifetime types has side-effects.
  if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
    return !RD->isCompleteDefinition() ||
           !RD->hasTrivialDefaultConstructor() ||
           !RD->hasTrivialDestructor();
  return false;
}

void Sema::CheckShadowInheritedFields(const SourceLocation &Loc,
                                      DeclarationName FieldName,
                                      const CXXRecordDecl *RD,
                                      bool DeclIsField) {
  if (Diags.isIgnored(diag::warn_shadow_field, Loc))
    return;

  // To record a shadowed field in a base
  std::map<CXXRecordDecl*, NamedDecl*> Bases;
  auto FieldShadowed = [&](const CXXBaseSpecifier *Specifier,
                           CXXBasePath &Path) {
    const auto Base = Specifier->getType()->getAsCXXRecordDecl();
    // Record an ambiguous path directly
    if (Bases.find(Base) != Bases.end())
      return true;
    for (const auto Field : Base->lookup(FieldName)) {
      if ((isa<FieldDecl>(Field) || isa<IndirectFieldDecl>(Field)) &&
          Field->getAccess() != AS_private) {
        assert(Field->getAccess() != AS_none);
        assert(Bases.find(Base) == Bases.end());
        Bases[Base] = Field;
        return true;
      }
    }
    return false;
  };

  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/true);
  if (!RD->lookupInBases(FieldShadowed, Paths))
    return;

  for (const auto &P : Paths) {
    auto Base = P.back().Base->getType()->getAsCXXRecordDecl();
    auto It = Bases.find(Base);
    // Skip duplicated bases
    if (It == Bases.end())
      continue;
    auto BaseField = It->second;
    assert(BaseField->getAccess() != AS_private);
    if (AS_none !=
        CXXRecordDecl::MergeAccess(P.Access, BaseField->getAccess())) {
      Diag(Loc, diag::warn_shadow_field)
        << FieldName << RD << Base << DeclIsField;
      Diag(BaseField->getLocation(), diag::note_shadow_field);
      Bases.erase(It);
    }
  }
}

NamedDecl *
Sema::ActOnCXXMemberDeclarator(Scope *S, AccessSpecifier AS, Declarator &D,
                               MultiTemplateParamsArg TemplateParameterLists,
                               Expr *BW, const VirtSpecifiers &VS,
                               InClassInitStyle InitStyle) {
  const DeclSpec &DS = D.getDeclSpec();
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();
  SourceLocation Loc = NameInfo.getLoc();

  // For anonymous bitfields, the location should point to the type.
  if (Loc.isInvalid())
    Loc = D.getBeginLoc();

  Expr *BitWidth = static_cast<Expr*>(BW);

  assert(isa<CXXRecordDecl>(CurContext));
  assert(!DS.isFriendSpecified());

  bool isFunc = D.isDeclarationOfFunction();
  const ParsedAttr *MSPropertyAttr =
      D.getDeclSpec().getAttributes().getMSPropertyAttr();

  if (cast<CXXRecordDecl>(CurContext)->isInterface()) {
    // The Microsoft extension __interface only permits public member functions
    // and prohibits constructors, destructors, operators, non-public member
    // functions, static methods and data members.
    unsigned InvalidDecl;
    bool ShowDeclName = true;
    if (!isFunc &&
        (DS.getStorageClassSpec() == DeclSpec::SCS_typedef || MSPropertyAttr))
      InvalidDecl = 0;
    else if (!isFunc)
      InvalidDecl = 1;
    else if (AS != AS_public)
      InvalidDecl = 2;
    else if (DS.getStorageClassSpec() == DeclSpec::SCS_static)
      InvalidDecl = 3;
    else switch (Name.getNameKind()) {
      case DeclarationName::CXXConstructorName:
        InvalidDecl = 4;
        ShowDeclName = false;
        break;

      case DeclarationName::CXXDestructorName:
        InvalidDecl = 5;
        ShowDeclName = false;
        break;

      case DeclarationName::CXXOperatorName:
      case DeclarationName::CXXConversionFunctionName:
        InvalidDecl = 6;
        break;

      default:
        InvalidDecl = 0;
        break;
    }

    if (InvalidDecl) {
      if (ShowDeclName)
        Diag(Loc, diag::err_invalid_member_in_interface)
          << (InvalidDecl-1) << Name;
      else
        Diag(Loc, diag::err_invalid_member_in_interface)
          << (InvalidDecl-1) << "";
      return nullptr;
    }
  }

  // C++ 9.2p6: A member shall not be declared to have automatic storage
  // duration (auto, register) or with the extern storage-class-specifier.
  // C++ 7.1.1p8: The mutable specifier can be applied only to names of class
  // data members and cannot be applied to names declared const or static,
  // and cannot be applied to reference members.
  switch (DS.getStorageClassSpec()) {
  case DeclSpec::SCS_unspecified:
  case DeclSpec::SCS_typedef:
  case DeclSpec::SCS_static:
    break;
  case DeclSpec::SCS_mutable:
    if (isFunc) {
      Diag(DS.getStorageClassSpecLoc(), diag::err_mutable_function);

      // FIXME: It would be nicer if the keyword was ignored only for this
      // declarator. Otherwise we could get follow-up errors.
      D.getMutableDeclSpec().ClearStorageClassSpecs();
    }
    break;
  default:
    Diag(DS.getStorageClassSpecLoc(),
         diag::err_storageclass_invalid_for_member);
    D.getMutableDeclSpec().ClearStorageClassSpecs();
    break;
  }

  bool isInstField = (DS.getStorageClassSpec() == DeclSpec::SCS_unspecified ||
                      DS.getStorageClassSpec() == DeclSpec::SCS_mutable) &&
                     !isFunc && TemplateParameterLists.empty();

  if (DS.hasConstexprSpecifier() && isInstField) {
    SemaDiagnosticBuilder B =
        Diag(DS.getConstexprSpecLoc(), diag::err_invalid_constexpr_member);
    SourceLocation ConstexprLoc = DS.getConstexprSpecLoc();
    if (InitStyle == ICIS_NoInit) {
      B << 0 << 0;
      if (D.getDeclSpec().getTypeQualifiers() & DeclSpec::TQ_const)
        B << FixItHint::CreateRemoval(ConstexprLoc);
      else {
        B << FixItHint::CreateReplacement(ConstexprLoc, "const");
        D.getMutableDeclSpec().ClearConstexprSpec();
        const char *PrevSpec;
        unsigned DiagID;
        bool Failed = D.getMutableDeclSpec().SetTypeQual(
            DeclSpec::TQ_const, ConstexprLoc, PrevSpec, DiagID, getLangOpts());
        (void)Failed;
        assert(!Failed && "Making a constexpr member const shouldn't fail");
      }
    } else {
      B << 1;
      const char *PrevSpec;
      unsigned DiagID;
      if (D.getMutableDeclSpec().SetStorageClassSpec(
          *this, DeclSpec::SCS_static, ConstexprLoc, PrevSpec, DiagID,
          Context.getPrintingPolicy())) {
        assert(DS.getStorageClassSpec() == DeclSpec::SCS_mutable &&
               "This is the only DeclSpec that should fail to be applied");
        B << 1;
      } else {
        B << 0 << FixItHint::CreateInsertion(ConstexprLoc, "static ");
        isInstField = false;
      }
    }
  }

  NamedDecl *Member;
  if (isInstField) {
    CXXScopeSpec &SS = D.getCXXScopeSpec();

    // Data members must have identifiers for names.
    if (!Name.isIdentifier()) {
      Diag(Loc, diag::err_bad_variable_name)
        << Name;
      return nullptr;
    }

    IdentifierInfo *II = Name.getAsIdentifierInfo();
    if (D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId) {
      Diag(D.getIdentifierLoc(), diag::err_member_with_template_arguments)
          << II
          << SourceRange(D.getName().TemplateId->LAngleLoc,
                         D.getName().TemplateId->RAngleLoc)
          << D.getName().TemplateId->LAngleLoc;
      D.SetIdentifier(II, Loc);
    }

    if (SS.isSet() && !SS.isInvalid()) {
      // The user provided a superfluous scope specifier inside a class
      // definition:
      //
      // class X {
      //   int X::member;
      // };
      if (DeclContext *DC = computeDeclContext(SS, false)) {
        TemplateIdAnnotation *TemplateId =
            D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId
                ? D.getName().TemplateId
                : nullptr;
        diagnoseQualifiedDeclaration(SS, DC, Name, D.getIdentifierLoc(),
                                     TemplateId,
                                     /*IsMemberSpecialization=*/false);
      } else {
        Diag(D.getIdentifierLoc(), diag::err_member_qualification)
          << Name << SS.getRange();
      }
      SS.clear();
    }

    if (MSPropertyAttr) {
      Member = HandleMSProperty(S, cast<CXXRecordDecl>(CurContext), Loc, D,
                                BitWidth, InitStyle, AS, *MSPropertyAttr);
      if (!Member)
        return nullptr;
      isInstField = false;
    } else {
      Member = HandleField(S, cast<CXXRecordDecl>(CurContext), Loc, D,
                                BitWidth, InitStyle, AS);
      if (!Member)
        return nullptr;
    }

    CheckShadowInheritedFields(Loc, Name, cast<CXXRecordDecl>(CurContext));
  } else {
    Member = HandleDeclarator(S, D, TemplateParameterLists);
    if (!Member)
      return nullptr;

    // Non-instance-fields can't have a bitfield.
    if (BitWidth) {
      if (Member->isInvalidDecl()) {
        // don't emit another diagnostic.
      } else if (isa<VarDecl>(Member) || isa<VarTemplateDecl>(Member)) {
        // C++ 9.6p3: A bit-field shall not be a static member.
        // "static member 'A' cannot be a bit-field"
        Diag(Loc, diag::err_static_not_bitfield)
          << Name << BitWidth->getSourceRange();
      } else if (isa<TypedefDecl>(Member)) {
        // "typedef member 'x' cannot be a bit-field"
        Diag(Loc, diag::err_typedef_not_bitfield)
          << Name << BitWidth->getSourceRange();
      } else {
        // A function typedef ("typedef int f(); f a;").
        // C++ 9.6p3: A bit-field shall have integral or enumeration type.
        Diag(Loc, diag::err_not_integral_type_bitfield)
          << Name << cast<ValueDecl>(Member)->getType()
          << BitWidth->getSourceRange();
      }

      BitWidth = nullptr;
      Member->setInvalidDecl();
    }

    NamedDecl *NonTemplateMember = Member;
    if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(Member))
      NonTemplateMember = FunTmpl->getTemplatedDecl();
    else if (VarTemplateDecl *VarTmpl = dyn_cast<VarTemplateDecl>(Member))
      NonTemplateMember = VarTmpl->getTemplatedDecl();

    Member->setAccess(AS);

    // If we have declared a member function template or static data member
    // template, set the access of the templated declaration as well.
    if (NonTemplateMember != Member)
      NonTemplateMember->setAccess(AS);

    // C++ [temp.deduct.guide]p3:
    //   A deduction guide [...] for a member class template [shall be
    //   declared] with the same access [as the template].
    if (auto *DG = dyn_cast<CXXDeductionGuideDecl>(NonTemplateMember)) {
      auto *TD = DG->getDeducedTemplate();
      // Access specifiers are only meaningful if both the template and the
      // deduction guide are from the same scope.
      if (AS != TD->getAccess() &&
          TD->getDeclContext()->getRedeclContext()->Equals(
              DG->getDeclContext()->getRedeclContext())) {
        Diag(DG->getBeginLoc(), diag::err_deduction_guide_wrong_access);
        Diag(TD->getBeginLoc(), diag::note_deduction_guide_template_access)
            << TD->getAccess();
        const AccessSpecDecl *LastAccessSpec = nullptr;
        for (const auto *D : cast<CXXRecordDecl>(CurContext)->decls()) {
          if (const auto *AccessSpec = dyn_cast<AccessSpecDecl>(D))
            LastAccessSpec = AccessSpec;
        }
        assert(LastAccessSpec && "differing access with no access specifier");
        Diag(LastAccessSpec->getBeginLoc(), diag::note_deduction_guide_access)
            << AS;
      }
    }
  }

  if (VS.isOverrideSpecified())
    Member->addAttr(OverrideAttr::Create(Context, VS.getOverrideLoc()));
  if (VS.isFinalSpecified())
    Member->addAttr(FinalAttr::Create(Context, VS.getFinalLoc(),
                                      VS.isFinalSpelledSealed()
                                          ? FinalAttr::Keyword_sealed
                                          : FinalAttr::Keyword_final));

  if (VS.getLastLocation().isValid()) {
    // Update the end location of a method that has a virt-specifiers.
    if (CXXMethodDecl *MD = dyn_cast_or_null<CXXMethodDecl>(Member))
      MD->setRangeEnd(VS.getLastLocation());
  }

  CheckOverrideControl(Member);

  assert((Name || isInstField) && "No identifier for non-field ?");

  if (isInstField) {
    FieldDecl *FD = cast<FieldDecl>(Member);
    FieldCollector->Add(FD);

    if (!Diags.isIgnored(diag::warn_unused_private_field, FD->getLocation())) {
      // Remember all explicit private FieldDecls that have a name, no side
      // effects and are not part of a dependent type declaration.

      auto DeclHasUnusedAttr = [](const QualType &T) {
        if (const TagDecl *TD = T->getAsTagDecl())
          return TD->hasAttr<UnusedAttr>();
        if (const TypedefType *TDT = T->getAs<TypedefType>())
          return TDT->getDecl()->hasAttr<UnusedAttr>();
        return false;
      };

      if (!FD->isImplicit() && FD->getDeclName() &&
          FD->getAccess() == AS_private &&
          !FD->hasAttr<UnusedAttr>() &&
          !FD->getParent()->isDependentContext() &&
          !DeclHasUnusedAttr(FD->getType()) &&
          !InitializationHasSideEffects(*FD))
        UnusedPrivateFields.insert(FD);
    }
  }

  return Member;
}

namespace {
  class UninitializedFieldVisitor
      : public EvaluatedExprVisitor<UninitializedFieldVisitor> {
    Sema &S;
    // List of Decls to generate a warning on.  Also remove Decls that become
    // initialized.
    llvm::SmallPtrSetImpl<ValueDecl*> &Decls;
    // List of base classes of the record.  Classes are removed after their
    // initializers.
    llvm::SmallPtrSetImpl<QualType> &BaseClasses;
    // Vector of decls to be removed from the Decl set prior to visiting the
    // nodes.  These Decls may have been initialized in the prior initializer.
    llvm::SmallVector<ValueDecl*, 4> DeclsToRemove;
    // If non-null, add a note to the warning pointing back to the constructor.
    const CXXConstructorDecl *Constructor;
    // Variables to hold state when processing an initializer list.  When
    // InitList is true, special case initialization of FieldDecls matching
    // InitListFieldDecl.
    bool InitList;
    FieldDecl *InitListFieldDecl;
    llvm::SmallVector<unsigned, 4> InitFieldIndex;

  public:
    typedef EvaluatedExprVisitor<UninitializedFieldVisitor> Inherited;
    UninitializedFieldVisitor(Sema &S,
                              llvm::SmallPtrSetImpl<ValueDecl*> &Decls,
                              llvm::SmallPtrSetImpl<QualType> &BaseClasses)
      : Inherited(S.Context), S(S), Decls(Decls), BaseClasses(BaseClasses),
        Constructor(nullptr), InitList(false), InitListFieldDecl(nullptr) {}

    // Returns true if the use of ME is not an uninitialized use.
    bool IsInitListMemberExprInitialized(MemberExpr *ME,
                                         bool CheckReferenceOnly) {
      llvm::SmallVector<FieldDecl*, 4> Fields;
      bool ReferenceField = false;
      while (ME) {
        FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (!FD)
          return false;
        Fields.push_back(FD);
        if (FD->getType()->isReferenceType())
          ReferenceField = true;
        ME = dyn_cast<MemberExpr>(ME->getBase()->IgnoreParenImpCasts());
      }

      // Binding a reference to an uninitialized field is not an
      // uninitialized use.
      if (CheckReferenceOnly && !ReferenceField)
        return true;

      llvm::SmallVector<unsigned, 4> UsedFieldIndex;
      // Discard the first field since it is the field decl that is being
      // initialized.
      for (const FieldDecl *FD : llvm::drop_begin(llvm::reverse(Fields)))
        UsedFieldIndex.push_back(FD->getFieldIndex());

      for (auto UsedIter = UsedFieldIndex.begin(),
                UsedEnd = UsedFieldIndex.end(),
                OrigIter = InitFieldIndex.begin(),
                OrigEnd = InitFieldIndex.end();
           UsedIter != UsedEnd && OrigIter != OrigEnd; ++UsedIter, ++OrigIter) {
        if (*UsedIter < *OrigIter)
          return true;
        if (*UsedIter > *OrigIter)
          break;
      }

      return false;
    }

    void HandleMemberExpr(MemberExpr *ME, bool CheckReferenceOnly,
                          bool AddressOf) {
      if (isa<EnumConstantDecl>(ME->getMemberDecl()))
        return;

      // FieldME is the inner-most MemberExpr that is not an anonymous struct
      // or union.
      MemberExpr *FieldME = ME;

      bool AllPODFields = FieldME->getType().isPODType(S.Context);

      Expr *Base = ME;
      while (MemberExpr *SubME =
                 dyn_cast<MemberExpr>(Base->IgnoreParenImpCasts())) {

        if (isa<VarDecl>(SubME->getMemberDecl()))
          return;

        if (FieldDecl *FD = dyn_cast<FieldDecl>(SubME->getMemberDecl()))
          if (!FD->isAnonymousStructOrUnion())
            FieldME = SubME;

        if (!FieldME->getType().isPODType(S.Context))
          AllPODFields = false;

        Base = SubME->getBase();
      }

      if (!isa<CXXThisExpr>(Base->IgnoreParenImpCasts())) {
        Visit(Base);
        return;
      }

      if (AddressOf && AllPODFields)
        return;

      ValueDecl* FoundVD = FieldME->getMemberDecl();

      if (ImplicitCastExpr *BaseCast = dyn_cast<ImplicitCastExpr>(Base)) {
        while (isa<ImplicitCastExpr>(BaseCast->getSubExpr())) {
          BaseCast = cast<ImplicitCastExpr>(BaseCast->getSubExpr());
        }

        if (BaseCast->getCastKind() == CK_UncheckedDerivedToBase) {
          QualType T = BaseCast->getType();
          if (T->isPointerType() &&
              BaseClasses.count(T->getPointeeType())) {
            S.Diag(FieldME->getExprLoc(), diag::warn_base_class_is_uninit)
                << T->getPointeeType() << FoundVD;
          }
        }
      }

      if (!Decls.count(FoundVD))
        return;

      const bool IsReference = FoundVD->getType()->isReferenceType();

      if (InitList && !AddressOf && FoundVD == InitListFieldDecl) {
        // Special checking for initializer lists.
        if (IsInitListMemberExprInitialized(ME, CheckReferenceOnly)) {
          return;
        }
      } else {
        // Prevent double warnings on use of unbounded references.
        if (CheckReferenceOnly && !IsReference)
          return;
      }

      unsigned diag = IsReference
          ? diag::warn_reference_field_is_uninit
          : diag::warn_field_is_uninit;
      S.Diag(FieldME->getExprLoc(), diag) << FoundVD;
      if (Constructor)
        S.Diag(Constructor->getLocation(),
               diag::note_uninit_in_this_constructor)
          << (Constructor->isDefaultConstructor() && Constructor->isImplicit());

    }

    void HandleValue(Expr *E, bool AddressOf) {
      E = E->IgnoreParens();

      if (MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
        HandleMemberExpr(ME, false /*CheckReferenceOnly*/,
                         AddressOf /*AddressOf*/);
        return;
      }

      if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
        Visit(CO->getCond());
        HandleValue(CO->getTrueExpr(), AddressOf);
        HandleValue(CO->getFalseExpr(), AddressOf);
        return;
      }

      if (BinaryConditionalOperator *BCO =
              dyn_cast<BinaryConditionalOperator>(E)) {
        Visit(BCO->getCond());
        HandleValue(BCO->getFalseExpr(), AddressOf);
        return;
      }

      if (OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E)) {
        HandleValue(OVE->getSourceExpr(), AddressOf);
        return;
      }

      if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
        switch (BO->getOpcode()) {
        default:
          break;
        case(BO_PtrMemD):
        case(BO_PtrMemI):
          HandleValue(BO->getLHS(), AddressOf);
          Visit(BO->getRHS());
          return;
        case(BO_Comma):
          Visit(BO->getLHS());
          HandleValue(BO->getRHS(), AddressOf);
          return;
        }
      }

      Visit(E);
    }

    void CheckInitListExpr(InitListExpr *ILE) {
      InitFieldIndex.push_back(0);
      for (auto *Child : ILE->children()) {
        if (InitListExpr *SubList = dyn_cast<InitListExpr>(Child)) {
          CheckInitListExpr(SubList);
        } else {
          Visit(Child);
        }
        ++InitFieldIndex.back();
      }
      InitFieldIndex.pop_back();
    }

    void CheckInitializer(Expr *E, const CXXConstructorDecl *FieldConstructor,
                          FieldDecl *Field, const Type *BaseClass) {
      // Remove Decls that may have been initialized in the previous
      // initializer.
      for (ValueDecl* VD : DeclsToRemove)
        Decls.erase(VD);
      DeclsToRemove.clear();

      Constructor = FieldConstructor;
      InitListExpr *ILE = dyn_cast<InitListExpr>(E);

      if (ILE && Field) {
        InitList = true;
        InitListFieldDecl = Field;
        InitFieldIndex.clear();
        CheckInitListExpr(ILE);
      } else {
        InitList = false;
        Visit(E);
      }

      if (Field)
        Decls.erase(Field);
      if (BaseClass)
        BaseClasses.erase(BaseClass->getCanonicalTypeInternal());
    }

    void VisitMemberExpr(MemberExpr *ME) {
      // All uses of unbounded reference fields will warn.
      HandleMemberExpr(ME, true /*CheckReferenceOnly*/, false /*AddressOf*/);
    }

    void VisitImplicitCastExpr(ImplicitCastExpr *E) {
      if (E->getCastKind() == CK_LValueToRValue) {
        HandleValue(E->getSubExpr(), false /*AddressOf*/);
        return;
      }

      Inherited::VisitImplicitCastExpr(E);
    }

    void VisitCXXConstructExpr(CXXConstructExpr *E) {
      if (E->getConstructor()->isCopyConstructor()) {
        Expr *ArgExpr = E->getArg(0);
        if (InitListExpr *ILE = dyn_cast<InitListExpr>(ArgExpr))
          if (ILE->getNumInits() == 1)
            ArgExpr = ILE->getInit(0);
        if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgExpr))
          if (ICE->getCastKind() == CK_NoOp)
            ArgExpr = ICE->getSubExpr();
        HandleValue(ArgExpr, false /*AddressOf*/);
        return;
      }
      Inherited::VisitCXXConstructExpr(E);
    }

    void VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
      Expr *Callee = E->getCallee();
      if (isa<MemberExpr>(Callee)) {
        HandleValue(Callee, false /*AddressOf*/);
        for (auto *Arg : E->arguments())
          Visit(Arg);
        return;
      }

      Inherited::VisitCXXMemberCallExpr(E);
    }

    void VisitCallExpr(CallExpr *E) {
      // Treat std::move as a use.
      if (E->isCallToStdMove()) {
        HandleValue(E->getArg(0), /*AddressOf=*/false);
        return;
      }

      Inherited::VisitCallExpr(E);
    }

    void VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
      Expr *Callee = E->getCallee();

      if (isa<UnresolvedLookupExpr>(Callee))
        return Inherited::VisitCXXOperatorCallExpr(E);

      Visit(Callee);
      for (auto *Arg : E->arguments())
        HandleValue(Arg->IgnoreParenImpCasts(), false /*AddressOf*/);
    }

    void VisitBinaryOperator(BinaryOperator *E) {
      // If a field assignment is detected, remove the field from the
      // uninitiailized field set.
      if (E->getOpcode() == BO_Assign)
        if (MemberExpr *ME = dyn_cast<MemberExpr>(E->getLHS()))
          if (FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl()))
            if (!FD->getType()->isReferenceType())
              DeclsToRemove.push_back(FD);

      if (E->isCompoundAssignmentOp()) {
        HandleValue(E->getLHS(), false /*AddressOf*/);
        Visit(E->getRHS());
        return;
      }

      Inherited::VisitBinaryOperator(E);
    }

    void VisitUnaryOperator(UnaryOperator *E) {
      if (E->isIncrementDecrementOp()) {
        HandleValue(E->getSubExpr(), false /*AddressOf*/);
        return;
      }
      if (E->getOpcode() == UO_AddrOf) {
        if (MemberExpr *ME = dyn_cast<MemberExpr>(E->getSubExpr())) {
          HandleValue(ME->getBase(), true /*AddressOf*/);
          return;
        }
      }

      Inherited::VisitUnaryOperator(E);
    }
  };

  // Diagnose value-uses of fields to initialize themselves, e.g.
  //   foo(foo)
  // where foo is not also a parameter to the constructor.
  // Also diagnose across field uninitialized use such as
  //   x(y), y(x)
  // TODO: implement -Wuninitialized and fold this into that framework.
  static void DiagnoseUninitializedFields(
      Sema &SemaRef, const CXXConstructorDecl *Constructor) {

    if (SemaRef.getDiagnostics().isIgnored(diag::warn_field_is_uninit,
                                           Constructor->getLocation())) {
      return;
    }

    if (Constructor->isInvalidDecl())
      return;

    const CXXRecordDecl *RD = Constructor->getParent();

    if (RD->isDependentContext())
      return;

    // Holds fields that are uninitialized.
    llvm::SmallPtrSet<ValueDecl*, 4> UninitializedFields;

    // At the beginning, all fields are uninitialized.
    for (auto *I : RD->decls()) {
      if (auto *FD = dyn_cast<FieldDecl>(I)) {
        UninitializedFields.insert(FD);
      } else if (auto *IFD = dyn_cast<IndirectFieldDecl>(I)) {
        UninitializedFields.insert(IFD->getAnonField());
      }
    }

    llvm::SmallPtrSet<QualType, 4> UninitializedBaseClasses;
    for (const auto &I : RD->bases())
      UninitializedBaseClasses.insert(I.getType().getCanonicalType());

    if (UninitializedFields.empty() && UninitializedBaseClasses.empty())
      return;

    UninitializedFieldVisitor UninitializedChecker(SemaRef,
                                                   UninitializedFields,
                                                   UninitializedBaseClasses);

    for (const auto *FieldInit : Constructor->inits()) {
      if (UninitializedFields.empty() && UninitializedBaseClasses.empty())
        break;

      Expr *InitExpr = FieldInit->getInit();
      if (!InitExpr)
        continue;

      if (CXXDefaultInitExpr *Default =
              dyn_cast<CXXDefaultInitExpr>(InitExpr)) {
        InitExpr = Default->getExpr();
        if (!InitExpr)
          continue;
        // In class initializers will point to the constructor.
        UninitializedChecker.CheckInitializer(InitExpr, Constructor,
                                              FieldInit->getAnyMember(),
                                              FieldInit->getBaseClass());
      } else {
        UninitializedChecker.CheckInitializer(InitExpr, nullptr,
                                              FieldInit->getAnyMember(),
                                              FieldInit->getBaseClass());
      }
    }
  }
} // namespace

void Sema::ActOnStartCXXInClassMemberInitializer() {
  // Create a synthetic function scope to represent the call to the constructor
  // that notionally surrounds a use of this initializer.
  PushFunctionScope();
}

void Sema::ActOnStartTrailingRequiresClause(Scope *S, Declarator &D) {
  if (!D.isFunctionDeclarator())
    return;
  auto &FTI = D.getFunctionTypeInfo();
  if (!FTI.Params)
    return;
  for (auto &Param : ArrayRef<DeclaratorChunk::ParamInfo>(FTI.Params,
                                                          FTI.NumParams)) {
    auto *ParamDecl = cast<NamedDecl>(Param.Param);
    if (ParamDecl->getDeclName())
      PushOnScopeChains(ParamDecl, S, /*AddToContext=*/false);
  }
}

ExprResult Sema::ActOnFinishTrailingRequiresClause(ExprResult ConstraintExpr) {
  return ActOnRequiresClause(ConstraintExpr);
}

ExprResult Sema::ActOnRequiresClause(ExprResult ConstraintExpr) {
  if (ConstraintExpr.isInvalid())
    return ExprError();

  ConstraintExpr = CorrectDelayedTyposInExpr(ConstraintExpr);
  if (ConstraintExpr.isInvalid())
    return ExprError();

  if (DiagnoseUnexpandedParameterPack(ConstraintExpr.get(),
                                      UPPC_RequiresClause))
    return ExprError();

  return ConstraintExpr;
}

ExprResult Sema::ConvertMemberDefaultInitExpression(FieldDecl *FD,
                                                    Expr *InitExpr,
                                                    SourceLocation InitLoc) {
  InitializedEntity Entity =
      InitializedEntity::InitializeMemberFromDefaultMemberInitializer(FD);
  InitializationKind Kind =
      FD->getInClassInitStyle() == ICIS_ListInit
          ? InitializationKind::CreateDirectList(InitExpr->getBeginLoc(),
                                                 InitExpr->getBeginLoc(),
                                                 InitExpr->getEndLoc())
          : InitializationKind::CreateCopy(InitExpr->getBeginLoc(), InitLoc);
  InitializationSequence Seq(*this, Entity, Kind, InitExpr);
  return Seq.Perform(*this, Entity, Kind, InitExpr);
}

void Sema::ActOnFinishCXXInClassMemberInitializer(Decl *D,
                                                  SourceLocation InitLoc,
                                                  Expr *InitExpr) {
  // Pop the notional constructor scope we created earlier.
  PopFunctionScopeInfo(nullptr, D);

  FieldDecl *FD = dyn_cast<FieldDecl>(D);
  assert((isa<MSPropertyDecl>(D) || FD->getInClassInitStyle() != ICIS_NoInit) &&
         "must set init style when field is created");

  if (!InitExpr) {
    D->setInvalidDecl();
    if (FD)
      FD->removeInClassInitializer();
    return;
  }

  if (DiagnoseUnexpandedParameterPack(InitExpr, UPPC_Initializer)) {
    FD->setInvalidDecl();
    FD->removeInClassInitializer();
    return;
  }

  ExprResult Init = CorrectDelayedTyposInExpr(InitExpr, /*InitDecl=*/nullptr,
                                              /*RecoverUncorrectedTypos=*/true);
  assert(Init.isUsable() && "Init should at least have a RecoveryExpr");
  if (!FD->getType()->isDependentType() && !Init.get()->isTypeDependent()) {
    Init = ConvertMemberDefaultInitExpression(FD, Init.get(), InitLoc);
    // C++11 [class.base.init]p7:
    //   The initialization of each base and member constitutes a
    //   full-expression.
    if (!Init.isInvalid())
      Init = ActOnFinishFullExpr(Init.get(), /*DiscarededValue=*/false);
    if (Init.isInvalid()) {
      FD->setInvalidDecl();
      return;
    }
  }

  FD->setInClassInitializer(Init.get());
}

/// Find the direct and/or virtual base specifiers that
/// correspond to the given base type, for use in base initialization
/// within a constructor.
static bool FindBaseInitializer(Sema &SemaRef,
                                CXXRecordDecl *ClassDecl,
                                QualType BaseType,
                                const CXXBaseSpecifier *&DirectBaseSpec,
                                const CXXBaseSpecifier *&VirtualBaseSpec) {
  // First, check for a direct base class.
  DirectBaseSpec = nullptr;
  for (const auto &Base : ClassDecl->bases()) {
    if (SemaRef.Context.hasSameUnqualifiedType(BaseType, Base.getType())) {
      // We found a direct base of this type. That's what we're
      // initializing.
      DirectBaseSpec = &Base;
      break;
    }
  }

  // Check for a virtual base class.
  // FIXME: We might be able to short-circuit this if we know in advance that
  // there are no virtual bases.
  VirtualBaseSpec = nullptr;
  if (!DirectBaseSpec || !DirectBaseSpec->isVirtual()) {
    // We haven't found a base yet; search the class hierarchy for a
    // virtual base class.
    CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                       /*DetectVirtual=*/false);
    if (SemaRef.IsDerivedFrom(ClassDecl->getLocation(),
                              SemaRef.Context.getTypeDeclType(ClassDecl),
                              BaseType, Paths)) {
      for (CXXBasePaths::paths_iterator Path = Paths.begin();
           Path != Paths.end(); ++Path) {
        if (Path->back().Base->isVirtual()) {
          VirtualBaseSpec = Path->back().Base;
          break;
        }
      }
    }
  }

  return DirectBaseSpec || VirtualBaseSpec;
}

MemInitResult
Sema::ActOnMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          const DeclSpec &DS,
                          SourceLocation IdLoc,
                          Expr *InitList,
                          SourceLocation EllipsisLoc) {
  return BuildMemInitializer(ConstructorD, S, SS, MemberOrBase, TemplateTypeTy,
                             DS, IdLoc, InitList,
                             EllipsisLoc);
}

MemInitResult
Sema::ActOnMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          const DeclSpec &DS,
                          SourceLocation IdLoc,
                          SourceLocation LParenLoc,
                          ArrayRef<Expr *> Args,
                          SourceLocation RParenLoc,
                          SourceLocation EllipsisLoc) {
  Expr *List = ParenListExpr::Create(Context, LParenLoc, Args, RParenLoc);
  return BuildMemInitializer(ConstructorD, S, SS, MemberOrBase, TemplateTypeTy,
                             DS, IdLoc, List, EllipsisLoc);
}

namespace {

// Callback to only accept typo corrections that can be a valid C++ member
// initializer: either a non-static field member or a base class.
class MemInitializerValidatorCCC final : public CorrectionCandidateCallback {
public:
  explicit MemInitializerValidatorCCC(CXXRecordDecl *ClassDecl)
      : ClassDecl(ClassDecl) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (NamedDecl *ND = candidate.getCorrectionDecl()) {
      if (FieldDecl *Member = dyn_cast<FieldDecl>(ND))
        return Member->getDeclContext()->getRedeclContext()->Equals(ClassDecl);
      return isa<TypeDecl>(ND);
    }
    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<MemInitializerValidatorCCC>(*this);
  }

private:
  CXXRecordDecl *ClassDecl;
};

}

bool Sema::DiagRedefinedPlaceholderFieldDecl(SourceLocation Loc,
                                             RecordDecl *ClassDecl,
                                             const IdentifierInfo *Name) {
  DeclContextLookupResult Result = ClassDecl->lookup(Name);
  DeclContextLookupResult::iterator Found =
      llvm::find_if(Result, [this](const NamedDecl *Elem) {
        return isa<FieldDecl, IndirectFieldDecl>(Elem) &&
               Elem->isPlaceholderVar(getLangOpts());
      });
  // We did not find a placeholder variable
  if (Found == Result.end())
    return false;
  Diag(Loc, diag::err_using_placeholder_variable) << Name;
  for (DeclContextLookupResult::iterator It = Found; It != Result.end(); It++) {
    const NamedDecl *ND = *It;
    if (ND->getDeclContext() != ND->getDeclContext())
      break;
    if (isa<FieldDecl, IndirectFieldDecl>(ND) &&
        ND->isPlaceholderVar(getLangOpts()))
      Diag(ND->getLocation(), diag::note_reference_placeholder) << ND;
  }
  return true;
}

ValueDecl *
Sema::tryLookupUnambiguousFieldDecl(RecordDecl *ClassDecl,
                                    const IdentifierInfo *MemberOrBase) {
  ValueDecl *ND = nullptr;
  for (auto *D : ClassDecl->lookup(MemberOrBase)) {
    if (isa<FieldDecl, IndirectFieldDecl>(D)) {
      bool IsPlaceholder = D->isPlaceholderVar(getLangOpts());
      if (ND) {
        if (IsPlaceholder && D->getDeclContext() == ND->getDeclContext())
          return nullptr;
        break;
      }
      if (!IsPlaceholder)
        return cast<ValueDecl>(D);
      ND = cast<ValueDecl>(D);
    }
  }
  return ND;
}

ValueDecl *Sema::tryLookupCtorInitMemberDecl(CXXRecordDecl *ClassDecl,
                                             CXXScopeSpec &SS,
                                             ParsedType TemplateTypeTy,
                                             IdentifierInfo *MemberOrBase) {
  if (SS.getScopeRep() || TemplateTypeTy)
    return nullptr;
  return tryLookupUnambiguousFieldDecl(ClassDecl, MemberOrBase);
}

MemInitResult
Sema::BuildMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          const DeclSpec &DS,
                          SourceLocation IdLoc,
                          Expr *Init,
                          SourceLocation EllipsisLoc) {
  ExprResult Res = CorrectDelayedTyposInExpr(Init, /*InitDecl=*/nullptr,
                                             /*RecoverUncorrectedTypos=*/true);
  if (!Res.isUsable())
    return true;
  Init = Res.get();

  if (!ConstructorD)
    return true;

  AdjustDeclIfTemplate(ConstructorD);

  CXXConstructorDecl *Constructor
    = dyn_cast<CXXConstructorDecl>(ConstructorD);
  if (!Constructor) {
    // The user wrote a constructor initializer on a function that is
    // not a C++ constructor. Ignore the error for now, because we may
    // have more member initializers coming; we'll diagnose it just
    // once in ActOnMemInitializers.
    return true;
  }

  CXXRecordDecl *ClassDecl = Constructor->getParent();

  // C++ [class.base.init]p2:
  //   Names in a mem-initializer-id are looked up in the scope of the
  //   constructor's class and, if not found in that scope, are looked
  //   up in the scope containing the constructor's definition.
  //   [Note: if the constructor's class contains a member with the
  //   same name as a direct or virtual base class of the class, a
  //   mem-initializer-id naming the member or base class and composed
  //   of a single identifier refers to the class member. A
  //   mem-initializer-id for the hidden base class may be specified
  //   using a qualified name. ]

  // Look for a member, first.
  if (ValueDecl *Member = tryLookupCtorInitMemberDecl(
          ClassDecl, SS, TemplateTypeTy, MemberOrBase)) {
    if (EllipsisLoc.isValid())
      Diag(EllipsisLoc, diag::err_pack_expansion_member_init)
          << MemberOrBase
          << SourceRange(IdLoc, Init->getSourceRange().getEnd());

    return BuildMemberInitializer(Member, Init, IdLoc);
  }
  // It didn't name a member, so see if it names a class.
  QualType BaseType;
  TypeSourceInfo *TInfo = nullptr;

  if (TemplateTypeTy) {
    BaseType = GetTypeFromParser(TemplateTypeTy, &TInfo);
    if (BaseType.isNull())
      return true;
  } else if (DS.getTypeSpecType() == TST_decltype) {
    BaseType = BuildDecltypeType(DS.getRepAsExpr());
  } else if (DS.getTypeSpecType() == TST_decltype_auto) {
    Diag(DS.getTypeSpecTypeLoc(), diag::err_decltype_auto_invalid);
    return true;
  } else if (DS.getTypeSpecType() == TST_typename_pack_indexing) {
    BaseType =
        BuildPackIndexingType(DS.getRepAsType().get(), DS.getPackIndexingExpr(),
                              DS.getBeginLoc(), DS.getEllipsisLoc());
  } else {
    LookupResult R(*this, MemberOrBase, IdLoc, LookupOrdinaryName);
    LookupParsedName(R, S, &SS, /*ObjectType=*/QualType());

    TypeDecl *TyD = R.getAsSingle<TypeDecl>();
    if (!TyD) {
      if (R.isAmbiguous()) return true;

      // We don't want access-control diagnostics here.
      R.suppressDiagnostics();

      if (SS.isSet() && isDependentScopeSpecifier(SS)) {
        bool NotUnknownSpecialization = false;
        DeclContext *DC = computeDeclContext(SS, false);
        if (CXXRecordDecl *Record = dyn_cast_or_null<CXXRecordDecl>(DC))
          NotUnknownSpecialization = !Record->hasAnyDependentBases();

        if (!NotUnknownSpecialization) {
          // When the scope specifier can refer to a member of an unknown
          // specialization, we take it as a type name.
          BaseType = CheckTypenameType(
              ElaboratedTypeKeyword::None, SourceLocation(),
              SS.getWithLocInContext(Context), *MemberOrBase, IdLoc);
          if (BaseType.isNull())
            return true;

          TInfo = Context.CreateTypeSourceInfo(BaseType);
          DependentNameTypeLoc TL =
              TInfo->getTypeLoc().castAs<DependentNameTypeLoc>();
          if (!TL.isNull()) {
            TL.setNameLoc(IdLoc);
            TL.setElaboratedKeywordLoc(SourceLocation());
            TL.setQualifierLoc(SS.getWithLocInContext(Context));
          }

          R.clear();
          R.setLookupName(MemberOrBase);
        }
      }

      if (getLangOpts().MSVCCompat && !getLangOpts().CPlusPlus20) {
        if (auto UnqualifiedBase = R.getAsSingle<ClassTemplateDecl>()) {
          auto *TempSpec = cast<TemplateSpecializationType>(
              UnqualifiedBase->getInjectedClassNameSpecialization());
          TemplateName TN = TempSpec->getTemplateName();
          for (auto const &Base : ClassDecl->bases()) {
            auto BaseTemplate =
                Base.getType()->getAs<TemplateSpecializationType>();
            if (BaseTemplate && Context.hasSameTemplateName(
                                    BaseTemplate->getTemplateName(), TN)) {
              Diag(IdLoc, diag::ext_unqualified_base_class)
                  << SourceRange(IdLoc, Init->getSourceRange().getEnd());
              BaseType = Base.getType();
              break;
            }
          }
        }
      }

      // If no results were found, try to correct typos.
      TypoCorrection Corr;
      MemInitializerValidatorCCC CCC(ClassDecl);
      if (R.empty() && BaseType.isNull() &&
          (Corr = CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), S, &SS,
                              CCC, CTK_ErrorRecovery, ClassDecl))) {
        if (FieldDecl *Member = Corr.getCorrectionDeclAs<FieldDecl>()) {
          // We have found a non-static data member with a similar
          // name to what was typed; complain and initialize that
          // member.
          diagnoseTypo(Corr,
                       PDiag(diag::err_mem_init_not_member_or_class_suggest)
                         << MemberOrBase << true);
          return BuildMemberInitializer(Member, Init, IdLoc);
        } else if (TypeDecl *Type = Corr.getCorrectionDeclAs<TypeDecl>()) {
          const CXXBaseSpecifier *DirectBaseSpec;
          const CXXBaseSpecifier *VirtualBaseSpec;
          if (FindBaseInitializer(*this, ClassDecl,
                                  Context.getTypeDeclType(Type),
                                  DirectBaseSpec, VirtualBaseSpec)) {
            // We have found a direct or virtual base class with a
            // similar name to what was typed; complain and initialize
            // that base class.
            diagnoseTypo(Corr,
                         PDiag(diag::err_mem_init_not_member_or_class_suggest)
                           << MemberOrBase << false,
                         PDiag() /*Suppress note, we provide our own.*/);

            const CXXBaseSpecifier *BaseSpec = DirectBaseSpec ? DirectBaseSpec
                                                              : VirtualBaseSpec;
            Diag(BaseSpec->getBeginLoc(), diag::note_base_class_specified_here)
                << BaseSpec->getType() << BaseSpec->getSourceRange();

            TyD = Type;
          }
        }
      }

      if (!TyD && BaseType.isNull()) {
        Diag(IdLoc, diag::err_mem_init_not_member_or_class)
          << MemberOrBase << SourceRange(IdLoc,Init->getSourceRange().getEnd());
        return true;
      }
    }

    if (BaseType.isNull()) {
      BaseType = getElaboratedType(ElaboratedTypeKeyword::None, SS,
                                   Context.getTypeDeclType(TyD));
      MarkAnyDeclReferenced(TyD->getLocation(), TyD, /*OdrUse=*/false);
      TInfo = Context.CreateTypeSourceInfo(BaseType);
      ElaboratedTypeLoc TL = TInfo->getTypeLoc().castAs<ElaboratedTypeLoc>();
      TL.getNamedTypeLoc().castAs<TypeSpecTypeLoc>().setNameLoc(IdLoc);
      TL.setElaboratedKeywordLoc(SourceLocation());
      TL.setQualifierLoc(SS.getWithLocInContext(Context));
    }
  }

  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(BaseType, IdLoc);

  return BuildBaseInitializer(BaseType, TInfo, Init, ClassDecl, EllipsisLoc);
}

MemInitResult
Sema::BuildMemberInitializer(ValueDecl *Member, Expr *Init,
                             SourceLocation IdLoc) {
  FieldDecl *DirectMember = dyn_cast<FieldDecl>(Member);
  IndirectFieldDecl *IndirectMember = dyn_cast<IndirectFieldDecl>(Member);
  assert((DirectMember || IndirectMember) &&
         "Member must be a FieldDecl or IndirectFieldDecl");

  if (DiagnoseUnexpandedParameterPack(Init, UPPC_Initializer))
    return true;

  if (Member->isInvalidDecl())
    return true;

  MultiExprArg Args;
  if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
    Args = MultiExprArg(ParenList->getExprs(), ParenList->getNumExprs());
  } else if (InitListExpr *InitList = dyn_cast<InitListExpr>(Init)) {
    Args = MultiExprArg(InitList->getInits(), InitList->getNumInits());
  } else {
    // Template instantiation doesn't reconstruct ParenListExprs for us.
    Args = Init;
  }

  SourceRange InitRange = Init->getSourceRange();

  if (Member->getType()->isDependentType() || Init->isTypeDependent()) {
    // Can't check initialization for a member of dependent type or when
    // any of the arguments are type-dependent expressions.
    DiscardCleanupsInEvaluationContext();
  } else {
    bool InitList = false;
    if (isa<InitListExpr>(Init)) {
      InitList = true;
      Args = Init;
    }

    // Initialize the member.
    InitializedEntity MemberEntity =
      DirectMember ? InitializedEntity::InitializeMember(DirectMember, nullptr)
                   : InitializedEntity::InitializeMember(IndirectMember,
                                                         nullptr);
    InitializationKind Kind =
        InitList ? InitializationKind::CreateDirectList(
                       IdLoc, Init->getBeginLoc(), Init->getEndLoc())
                 : InitializationKind::CreateDirect(IdLoc, InitRange.getBegin(),
                                                    InitRange.getEnd());

    InitializationSequence InitSeq(*this, MemberEntity, Kind, Args);
    ExprResult MemberInit = InitSeq.Perform(*this, MemberEntity, Kind, Args,
                                            nullptr);
    if (!MemberInit.isInvalid()) {
      // C++11 [class.base.init]p7:
      //   The initialization of each base and member constitutes a
      //   full-expression.
      MemberInit = ActOnFinishFullExpr(MemberInit.get(), InitRange.getBegin(),
                                       /*DiscardedValue*/ false);
    }

    if (MemberInit.isInvalid()) {
      // Args were sensible expressions but we couldn't initialize the member
      // from them. Preserve them in a RecoveryExpr instead.
      Init = CreateRecoveryExpr(InitRange.getBegin(), InitRange.getEnd(), Args,
                                Member->getType())
                 .get();
      if (!Init)
        return true;
    } else {
      Init = MemberInit.get();
    }
  }

  if (DirectMember) {
    return new (Context) CXXCtorInitializer(Context, DirectMember, IdLoc,
                                            InitRange.getBegin(), Init,
                                            InitRange.getEnd());
  } else {
    return new (Context) CXXCtorInitializer(Context, IndirectMember, IdLoc,
                                            InitRange.getBegin(), Init,
                                            InitRange.getEnd());
  }
}

MemInitResult
Sema::BuildDelegatingInitializer(TypeSourceInfo *TInfo, Expr *Init,
                                 CXXRecordDecl *ClassDecl) {
  SourceLocation NameLoc = TInfo->getTypeLoc().getSourceRange().getBegin();
  if (!LangOpts.CPlusPlus11)
    return Diag(NameLoc, diag::err_delegating_ctor)
           << TInfo->getTypeLoc().getSourceRange();
  Diag(NameLoc, diag::warn_cxx98_compat_delegating_ctor);

  bool InitList = true;
  MultiExprArg Args = Init;
  if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
    InitList = false;
    Args = MultiExprArg(ParenList->getExprs(), ParenList->getNumExprs());
  }

  SourceRange InitRange = Init->getSourceRange();
  // Initialize the object.
  InitializedEntity DelegationEntity = InitializedEntity::InitializeDelegation(
                                     QualType(ClassDecl->getTypeForDecl(), 0));
  InitializationKind Kind =
      InitList ? InitializationKind::CreateDirectList(
                     NameLoc, Init->getBeginLoc(), Init->getEndLoc())
               : InitializationKind::CreateDirect(NameLoc, InitRange.getBegin(),
                                                  InitRange.getEnd());
  InitializationSequence InitSeq(*this, DelegationEntity, Kind, Args);
  ExprResult DelegationInit = InitSeq.Perform(*this, DelegationEntity, Kind,
                                              Args, nullptr);
  if (!DelegationInit.isInvalid()) {
    assert((DelegationInit.get()->containsErrors() ||
            cast<CXXConstructExpr>(DelegationInit.get())->getConstructor()) &&
           "Delegating constructor with no target?");

    // C++11 [class.base.init]p7:
    //   The initialization of each base and member constitutes a
    //   full-expression.
    DelegationInit = ActOnFinishFullExpr(
        DelegationInit.get(), InitRange.getBegin(), /*DiscardedValue*/ false);
  }

  if (DelegationInit.isInvalid()) {
    DelegationInit =
        CreateRecoveryExpr(InitRange.getBegin(), InitRange.getEnd(), Args,
                           QualType(ClassDecl->getTypeForDecl(), 0));
    if (DelegationInit.isInvalid())
      return true;
  } else {
    // If we are in a dependent context, template instantiation will
    // perform this type-checking again. Just save the arguments that we
    // received in a ParenListExpr.
    // FIXME: This isn't quite ideal, since our ASTs don't capture all
    // of the information that we have about the base
    // initializer. However, deconstructing the ASTs is a dicey process,
    // and this approach is far more likely to get the corner cases right.
    if (CurContext->isDependentContext())
      DelegationInit = Init;
  }

  return new (Context) CXXCtorInitializer(Context, TInfo, InitRange.getBegin(),
                                          DelegationInit.getAs<Expr>(),
                                          InitRange.getEnd());
}

MemInitResult
Sema::BuildBaseInitializer(QualType BaseType, TypeSourceInfo *BaseTInfo,
                           Expr *Init, CXXRecordDecl *ClassDecl,
                           SourceLocation EllipsisLoc) {
  SourceLocation BaseLoc = BaseTInfo->getTypeLoc().getBeginLoc();

  if (!BaseType->isDependentType() && !BaseType->isRecordType())
    return Diag(BaseLoc, diag::err_base_init_does_not_name_class)
           << BaseType << BaseTInfo->getTypeLoc().getSourceRange();

  // C++ [class.base.init]p2:
  //   [...] Unless the mem-initializer-id names a nonstatic data
  //   member of the constructor's class or a direct or virtual base
  //   of that class, the mem-initializer is ill-formed. A
  //   mem-initializer-list can initialize a base class using any
  //   name that denotes that base class type.

  // We can store the initializers in "as-written" form and delay analysis until
  // instantiation if the constructor is dependent. But not for dependent
  // (broken) code in a non-template! SetCtorInitializers does not expect this.
  bool Dependent = CurContext->isDependentContext() &&
                   (BaseType->isDependentType() || Init->isTypeDependent());

  SourceRange InitRange = Init->getSourceRange();
  if (EllipsisLoc.isValid()) {
    // This is a pack expansion.
    if (!BaseType->containsUnexpandedParameterPack())  {
      Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
        << SourceRange(BaseLoc, InitRange.getEnd());

      EllipsisLoc = SourceLocation();
    }
  } else {
    // Check for any unexpanded parameter packs.
    if (DiagnoseUnexpandedParameterPack(BaseLoc, BaseTInfo, UPPC_Initializer))
      return true;

    if (DiagnoseUnexpandedParameterPack(Init, UPPC_Initializer))
      return true;
  }

  // Check for direct and virtual base classes.
  const CXXBaseSpecifier *DirectBaseSpec = nullptr;
  const CXXBaseSpecifier *VirtualBaseSpec = nullptr;
  if (!Dependent) {
    if (Context.hasSameUnqualifiedType(QualType(ClassDecl->getTypeForDecl(),0),
                                       BaseType))
      return BuildDelegatingInitializer(BaseTInfo, Init, ClassDecl);

    FindBaseInitializer(*this, ClassDecl, BaseType, DirectBaseSpec,
                        VirtualBaseSpec);

    // C++ [base.class.init]p2:
    // Unless the mem-initializer-id names a nonstatic data member of the
    // constructor's class or a direct or virtual base of that class, the
    // mem-initializer is ill-formed.
    if (!DirectBaseSpec && !VirtualBaseSpec) {
      // If the class has any dependent bases, then it's possible that
      // one of those types will resolve to the same type as
      // BaseType. Therefore, just treat this as a dependent base
      // class initialization.  FIXME: Should we try to check the
      // initialization anyway? It seems odd.
      if (ClassDecl->hasAnyDependentBases())
        Dependent = true;
      else
        return Diag(BaseLoc, diag::err_not_direct_base_or_virtual)
               << BaseType << Context.getTypeDeclType(ClassDecl)
               << BaseTInfo->getTypeLoc().getSourceRange();
    }
  }

  if (Dependent) {
    DiscardCleanupsInEvaluationContext();

    return new (Context) CXXCtorInitializer(Context, BaseTInfo,
                                            /*IsVirtual=*/false,
                                            InitRange.getBegin(), Init,
                                            InitRange.getEnd(), EllipsisLoc);
  }

  // C++ [base.class.init]p2:
  //   If a mem-initializer-id is ambiguous because it designates both
  //   a direct non-virtual base class and an inherited virtual base
  //   class, the mem-initializer is ill-formed.
  if (DirectBaseSpec && VirtualBaseSpec)
    return Diag(BaseLoc, diag::err_base_init_direct_and_virtual)
      << BaseType << BaseTInfo->getTypeLoc().getLocalSourceRange();

  const CXXBaseSpecifier *BaseSpec = DirectBaseSpec;
  if (!BaseSpec)
    BaseSpec = VirtualBaseSpec;

  // Initialize the base.
  bool InitList = true;
  MultiExprArg Args = Init;
  if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
    InitList = false;
    Args = MultiExprArg(ParenList->getExprs(), ParenList->getNumExprs());
  }

  InitializedEntity BaseEntity =
    InitializedEntity::InitializeBase(Context, BaseSpec, VirtualBaseSpec);
  InitializationKind Kind =
      InitList ? InitializationKind::CreateDirectList(BaseLoc)
               : InitializationKind::CreateDirect(BaseLoc, InitRange.getBegin(),
                                                  InitRange.getEnd());
  InitializationSequence InitSeq(*this, BaseEntity, Kind, Args);
  ExprResult BaseInit = InitSeq.Perform(*this, BaseEntity, Kind, Args, nullptr);
  if (!BaseInit.isInvalid()) {
    // C++11 [class.base.init]p7:
    //   The initialization of each base and member constitutes a
    //   full-expression.
    BaseInit = ActOnFinishFullExpr(BaseInit.get(), InitRange.getBegin(),
                                   /*DiscardedValue*/ false);
  }

  if (BaseInit.isInvalid()) {
    BaseInit = CreateRecoveryExpr(InitRange.getBegin(), InitRange.getEnd(),
                                  Args, BaseType);
    if (BaseInit.isInvalid())
      return true;
  } else {
    // If we are in a dependent context, template instantiation will
    // perform this type-checking again. Just save the arguments that we
    // received in a ParenListExpr.
    // FIXME: This isn't quite ideal, since our ASTs don't capture all
    // of the information that we have about the base
    // initializer. However, deconstructing the ASTs is a dicey process,
    // and this approach is far more likely to get the corner cases right.
    if (CurContext->isDependentContext())
      BaseInit = Init;
  }

  return new (Context) CXXCtorInitializer(Context, BaseTInfo,
                                          BaseSpec->isVirtual(),
                                          InitRange.getBegin(),
                                          BaseInit.getAs<Expr>(),
                                          InitRange.getEnd(), EllipsisLoc);
}

// Create a static_cast\<T&&>(expr).
static Expr *CastForMoving(Sema &SemaRef, Expr *E) {
  QualType TargetType =
      SemaRef.BuildReferenceType(E->getType(), /*SpelledAsLValue*/ false,
                                 SourceLocation(), DeclarationName());
  SourceLocation ExprLoc = E->getBeginLoc();
  TypeSourceInfo *TargetLoc = SemaRef.Context.getTrivialTypeSourceInfo(
      TargetType, ExprLoc);

  return SemaRef.BuildCXXNamedCast(ExprLoc, tok::kw_static_cast, TargetLoc, E,
                                   SourceRange(ExprLoc, ExprLoc),
                                   E->getSourceRange()).get();
}

/// ImplicitInitializerKind - How an implicit base or member initializer should
/// initialize its base or member.
enum ImplicitInitializerKind {
  IIK_Default,
  IIK_Copy,
  IIK_Move,
  IIK_Inherit
};

static bool
BuildImplicitBaseInitializer(Sema &SemaRef, CXXConstructorDecl *Constructor,
                             ImplicitInitializerKind ImplicitInitKind,
                             CXXBaseSpecifier *BaseSpec,
                             bool IsInheritedVirtualBase,
                             CXXCtorInitializer *&CXXBaseInit) {
  InitializedEntity InitEntity
    = InitializedEntity::InitializeBase(SemaRef.Context, BaseSpec,
                                        IsInheritedVirtualBase);

  ExprResult BaseInit;

  switch (ImplicitInitKind) {
  case IIK_Inherit:
  case IIK_Default: {
    InitializationKind InitKind
      = InitializationKind::CreateDefault(Constructor->getLocation());
    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, std::nullopt);
    BaseInit = InitSeq.Perform(SemaRef, InitEntity, InitKind, std::nullopt);
    break;
  }

  case IIK_Move:
  case IIK_Copy: {
    bool Moving = ImplicitInitKind == IIK_Move;
    ParmVarDecl *Param = Constructor->getParamDecl(0);
    QualType ParamType = Param->getType().getNonReferenceType();

    Expr *CopyCtorArg =
      DeclRefExpr::Create(SemaRef.Context, NestedNameSpecifierLoc(),
                          SourceLocation(), Param, false,
                          Constructor->getLocation(), ParamType,
                          VK_LValue, nullptr);

    SemaRef.MarkDeclRefReferenced(cast<DeclRefExpr>(CopyCtorArg));

    // Cast to the base class to avoid ambiguities.
    QualType ArgTy =
      SemaRef.Context.getQualifiedType(BaseSpec->getType().getUnqualifiedType(),
                                       ParamType.getQualifiers());

    if (Moving) {
      CopyCtorArg = CastForMoving(SemaRef, CopyCtorArg);
    }

    CXXCastPath BasePath;
    BasePath.push_back(BaseSpec);
    CopyCtorArg = SemaRef.ImpCastExprToType(CopyCtorArg, ArgTy,
                                            CK_UncheckedDerivedToBase,
                                            Moving ? VK_XValue : VK_LValue,
                                            &BasePath).get();

    InitializationKind InitKind
      = InitializationKind::CreateDirect(Constructor->getLocation(),
                                         SourceLocation(), SourceLocation());
    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, CopyCtorArg);
    BaseInit = InitSeq.Perform(SemaRef, InitEntity, InitKind, CopyCtorArg);
    break;
  }
  }

  BaseInit = SemaRef.MaybeCreateExprWithCleanups(BaseInit);
  if (BaseInit.isInvalid())
    return true;

  CXXBaseInit =
    new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
               SemaRef.Context.getTrivialTypeSourceInfo(BaseSpec->getType(),
                                                        SourceLocation()),
                                             BaseSpec->isVirtual(),
                                             SourceLocation(),
                                             BaseInit.getAs<Expr>(),
                                             SourceLocation(),
                                             SourceLocation());

  return false;
}

static bool RefersToRValueRef(Expr *MemRef) {
  ValueDecl *Referenced = cast<MemberExpr>(MemRef)->getMemberDecl();
  return Referenced->getType()->isRValueReferenceType();
}

static bool
BuildImplicitMemberInitializer(Sema &SemaRef, CXXConstructorDecl *Constructor,
                               ImplicitInitializerKind ImplicitInitKind,
                               FieldDecl *Field, IndirectFieldDecl *Indirect,
                               CXXCtorInitializer *&CXXMemberInit) {
  if (Field->isInvalidDecl())
    return true;

  SourceLocation Loc = Constructor->getLocation();

  if (ImplicitInitKind == IIK_Copy || ImplicitInitKind == IIK_Move) {
    bool Moving = ImplicitInitKind == IIK_Move;
    ParmVarDecl *Param = Constructor->getParamDecl(0);
    QualType ParamType = Param->getType().getNonReferenceType();

    // Suppress copying zero-width bitfields.
    if (Field->isZeroLengthBitField(SemaRef.Context))
      return false;

    Expr *MemberExprBase =
      DeclRefExpr::Create(SemaRef.Context, NestedNameSpecifierLoc(),
                          SourceLocation(), Param, false,
                          Loc, ParamType, VK_LValue, nullptr);

    SemaRef.MarkDeclRefReferenced(cast<DeclRefExpr>(MemberExprBase));

    if (Moving) {
      MemberExprBase = CastForMoving(SemaRef, MemberExprBase);
    }

    // Build a reference to this field within the parameter.
    CXXScopeSpec SS;
    LookupResult MemberLookup(SemaRef, Field->getDeclName(), Loc,
                              Sema::LookupMemberName);
    MemberLookup.addDecl(Indirect ? cast<ValueDecl>(Indirect)
                                  : cast<ValueDecl>(Field), AS_public);
    MemberLookup.resolveKind();
    ExprResult CtorArg
      = SemaRef.BuildMemberReferenceExpr(MemberExprBase,
                                         ParamType, Loc,
                                         /*IsArrow=*/false,
                                         SS,
                                         /*TemplateKWLoc=*/SourceLocation(),
                                         /*FirstQualifierInScope=*/nullptr,
                                         MemberLookup,
                                         /*TemplateArgs=*/nullptr,
                                         /*S*/nullptr);
    if (CtorArg.isInvalid())
      return true;

    // C++11 [class.copy]p15:
    //   - if a member m has rvalue reference type T&&, it is direct-initialized
    //     with static_cast<T&&>(x.m);
    if (RefersToRValueRef(CtorArg.get())) {
      CtorArg = CastForMoving(SemaRef, CtorArg.get());
    }

    InitializedEntity Entity =
        Indirect ? InitializedEntity::InitializeMember(Indirect, nullptr,
                                                       /*Implicit*/ true)
                 : InitializedEntity::InitializeMember(Field, nullptr,
                                                       /*Implicit*/ true);

    // Direct-initialize to use the copy constructor.
    InitializationKind InitKind =
      InitializationKind::CreateDirect(Loc, SourceLocation(), SourceLocation());

    Expr *CtorArgE = CtorArg.getAs<Expr>();
    InitializationSequence InitSeq(SemaRef, Entity, InitKind, CtorArgE);
    ExprResult MemberInit =
        InitSeq.Perform(SemaRef, Entity, InitKind, MultiExprArg(&CtorArgE, 1));
    MemberInit = SemaRef.MaybeCreateExprWithCleanups(MemberInit);
    if (MemberInit.isInvalid())
      return true;

    if (Indirect)
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(
          SemaRef.Context, Indirect, Loc, Loc, MemberInit.getAs<Expr>(), Loc);
    else
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(
          SemaRef.Context, Field, Loc, Loc, MemberInit.getAs<Expr>(), Loc);
    return false;
  }

  assert((ImplicitInitKind == IIK_Default || ImplicitInitKind == IIK_Inherit) &&
         "Unhandled implicit init kind!");

  QualType FieldBaseElementType =
    SemaRef.Context.getBaseElementType(Field->getType());

  if (FieldBaseElementType->isRecordType()) {
    InitializedEntity InitEntity =
        Indirect ? InitializedEntity::InitializeMember(Indirect, nullptr,
                                                       /*Implicit*/ true)
                 : InitializedEntity::InitializeMember(Field, nullptr,
                                                       /*Implicit*/ true);
    InitializationKind InitKind =
      InitializationKind::CreateDefault(Loc);

    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, std::nullopt);
    ExprResult MemberInit =
        InitSeq.Perform(SemaRef, InitEntity, InitKind, std::nullopt);

    MemberInit = SemaRef.MaybeCreateExprWithCleanups(MemberInit);
    if (MemberInit.isInvalid())
      return true;

    if (Indirect)
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
                                                               Indirect, Loc,
                                                               Loc,
                                                               MemberInit.get(),
                                                               Loc);
    else
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
                                                               Field, Loc, Loc,
                                                               MemberInit.get(),
                                                               Loc);
    return false;
  }

  if (!Field->getParent()->isUnion()) {
    if (FieldBaseElementType->isReferenceType()) {
      SemaRef.Diag(Constructor->getLocation(),
                   diag::err_uninitialized_member_in_ctor)
      << (int)Constructor->isImplicit()
      << SemaRef.Context.getTagDeclType(Constructor->getParent())
      << 0 << Field->getDeclName();
      SemaRef.Diag(Field->getLocation(), diag::note_declared_at);
      return true;
    }

    if (FieldBaseElementType.isConstQualified()) {
      SemaRef.Diag(Constructor->getLocation(),
                   diag::err_uninitialized_member_in_ctor)
      << (int)Constructor->isImplicit()
      << SemaRef.Context.getTagDeclType(Constructor->getParent())
      << 1 << Field->getDeclName();
      SemaRef.Diag(Field->getLocation(), diag::note_declared_at);
      return true;
    }
  }

  if (FieldBaseElementType.hasNonTrivialObjCLifetime()) {
    // ARC and Weak:
    //   Default-initialize Objective-C pointers to NULL.
    CXXMemberInit
      = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context, Field,
                                                 Loc, Loc,
                 new (SemaRef.Context) ImplicitValueInitExpr(Field->getType()),
                                                 Loc);
    return false;
  }

  // Nothing to initialize.
  CXXMemberInit = nullptr;
  return false;
}

namespace {
struct BaseAndFieldInfo {
  Sema &S;
  CXXConstructorDecl *Ctor;
  bool AnyErrorsInInits;
  ImplicitInitializerKind IIK;
  llvm::DenseMap<const void *, CXXCtorInitializer*> AllBaseFields;
  SmallVector<CXXCtorInitializer*, 8> AllToInit;
  llvm::DenseMap<TagDecl*, FieldDecl*> ActiveUnionMember;

  BaseAndFieldInfo(Sema &S, CXXConstructorDecl *Ctor, bool ErrorsInInits)
    : S(S), Ctor(Ctor), AnyErrorsInInits(ErrorsInInits) {
    bool Generated = Ctor->isImplicit() || Ctor->isDefaulted();
    if (Ctor->getInheritedConstructor())
      IIK = IIK_Inherit;
    else if (Generated && Ctor->isCopyConstructor())
      IIK = IIK_Copy;
    else if (Generated && Ctor->isMoveConstructor())
      IIK = IIK_Move;
    else
      IIK = IIK_Default;
  }

  bool isImplicitCopyOrMove() const {
    switch (IIK) {
    case IIK_Copy:
    case IIK_Move:
      return true;

    case IIK_Default:
    case IIK_Inherit:
      return false;
    }

    llvm_unreachable("Invalid ImplicitInitializerKind!");
  }

  bool addFieldInitializer(CXXCtorInitializer *Init) {
    AllToInit.push_back(Init);

    // Check whether this initializer makes the field "used".
    if (Init->getInit()->HasSideEffects(S.Context))
      S.UnusedPrivateFields.remove(Init->getAnyMember());

    return false;
  }

  bool isInactiveUnionMember(FieldDecl *Field) {
    RecordDecl *Record = Field->getParent();
    if (!Record->isUnion())
      return false;

    if (FieldDecl *Active =
            ActiveUnionMember.lookup(Record->getCanonicalDecl()))
      return Active != Field->getCanonicalDecl();

    // In an implicit copy or move constructor, ignore any in-class initializer.
    if (isImplicitCopyOrMove())
      return true;

    // If there's no explicit initialization, the field is active only if it
    // has an in-class initializer...
    if (Field->hasInClassInitializer())
      return false;
    // ... or it's an anonymous struct or union whose class has an in-class
    // initializer.
    if (!Field->isAnonymousStructOrUnion())
      return true;
    CXXRecordDecl *FieldRD = Field->getType()->getAsCXXRecordDecl();
    return !FieldRD->hasInClassInitializer();
  }

  /// Determine whether the given field is, or is within, a union member
  /// that is inactive (because there was an initializer given for a different
  /// member of the union, or because the union was not initialized at all).
  bool isWithinInactiveUnionMember(FieldDecl *Field,
                                   IndirectFieldDecl *Indirect) {
    if (!Indirect)
      return isInactiveUnionMember(Field);

    for (auto *C : Indirect->chain()) {
      FieldDecl *Field = dyn_cast<FieldDecl>(C);
      if (Field && isInactiveUnionMember(Field))
        return true;
    }
    return false;
  }
};
}

/// Determine whether the given type is an incomplete or zero-lenfgth
/// array type.
static bool isIncompleteOrZeroLengthArrayType(ASTContext &Context, QualType T) {
  if (T->isIncompleteArrayType())
    return true;

  while (const ConstantArrayType *ArrayT = Context.getAsConstantArrayType(T)) {
    if (ArrayT->isZeroSize())
      return true;

    T = ArrayT->getElementType();
  }

  return false;
}

static bool CollectFieldInitializer(Sema &SemaRef, BaseAndFieldInfo &Info,
                                    FieldDecl *Field,
                                    IndirectFieldDecl *Indirect = nullptr) {
  if (Field->isInvalidDecl())
    return false;

  // Overwhelmingly common case: we have a direct initializer for this field.
  if (CXXCtorInitializer *Init =
          Info.AllBaseFields.lookup(Field->getCanonicalDecl()))
    return Info.addFieldInitializer(Init);

  // C++11 [class.base.init]p8:
  //   if the entity is a non-static data member that has a
  //   brace-or-equal-initializer and either
  //   -- the constructor's class is a union and no other variant member of that
  //      union is designated by a mem-initializer-id or
  //   -- the constructor's class is not a union, and, if the entity is a member
  //      of an anonymous union, no other member of that union is designated by
  //      a mem-initializer-id,
  //   the entity is initialized as specified in [dcl.init].
  //
  // We also apply the same rules to handle anonymous structs within anonymous
  // unions.
  if (Info.isWithinInactiveUnionMember(Field, Indirect))
    return false;

  if (Field->hasInClassInitializer() && !Info.isImplicitCopyOrMove()) {
    ExprResult DIE =
        SemaRef.BuildCXXDefaultInitExpr(Info.Ctor->getLocation(), Field);
    if (DIE.isInvalid())
      return true;

    auto Entity = InitializedEntity::InitializeMember(Field, nullptr, true);
    SemaRef.checkInitializerLifetime(Entity, DIE.get());

    CXXCtorInitializer *Init;
    if (Indirect)
      Init = new (SemaRef.Context)
          CXXCtorInitializer(SemaRef.Context, Indirect, SourceLocation(),
                             SourceLocation(), DIE.get(), SourceLocation());
    else
      Init = new (SemaRef.Context)
          CXXCtorInitializer(SemaRef.Context, Field, SourceLocation(),
                             SourceLocation(), DIE.get(), SourceLocation());
    return Info.addFieldInitializer(Init);
  }

  // Don't initialize incomplete or zero-length arrays.
  if (isIncompleteOrZeroLengthArrayType(SemaRef.Context, Field->getType()))
    return false;

  // Don't try to build an implicit initializer if there were semantic
  // errors in any of the initializers (and therefore we might be
  // missing some that the user actually wrote).
  if (Info.AnyErrorsInInits)
    return false;

  CXXCtorInitializer *Init = nullptr;
  if (BuildImplicitMemberInitializer(Info.S, Info.Ctor, Info.IIK, Field,
                                     Indirect, Init))
    return true;

  if (!Init)
    return false;

  return Info.addFieldInitializer(Init);
}

bool
Sema::SetDelegatingInitializer(CXXConstructorDecl *Constructor,
                               CXXCtorInitializer *Initializer) {
  assert(Initializer->isDelegatingInitializer());
  Constructor->setNumCtorInitializers(1);
  CXXCtorInitializer **initializer =
    new (Context) CXXCtorInitializer*[1];
  memcpy(initializer, &Initializer, sizeof (CXXCtorInitializer*));
  Constructor->setCtorInitializers(initializer);

  if (CXXDestructorDecl *Dtor = LookupDestructor(Constructor->getParent())) {
    MarkFunctionReferenced(Initializer->getSourceLocation(), Dtor);
    DiagnoseUseOfDecl(Dtor, Initializer->getSourceLocation());
  }

  DelegatingCtorDecls.push_back(Constructor);

  DiagnoseUninitializedFields(*this, Constructor);

  return false;
}

bool Sema::SetCtorInitializers(CXXConstructorDecl *Constructor, bool AnyErrors,
                               ArrayRef<CXXCtorInitializer *> Initializers) {
  if (Constructor->isDependentContext()) {
    // Just store the initializers as written, they will be checked during
    // instantiation.
    if (!Initializers.empty()) {
      Constructor->setNumCtorInitializers(Initializers.size());
      CXXCtorInitializer **baseOrMemberInitializers =
        new (Context) CXXCtorInitializer*[Initializers.size()];
      memcpy(baseOrMemberInitializers, Initializers.data(),
             Initializers.size() * sizeof(CXXCtorInitializer*));
      Constructor->setCtorInitializers(baseOrMemberInitializers);
    }

    // Let template instantiation know whether we had errors.
    if (AnyErrors)
      Constructor->setInvalidDecl();

    return false;
  }

  BaseAndFieldInfo Info(*this, Constructor, AnyErrors);

  // We need to build the initializer AST according to order of construction
  // and not what user specified in the Initializers list.
  CXXRecordDecl *ClassDecl = Constructor->getParent()->getDefinition();
  if (!ClassDecl)
    return true;

  bool HadError = false;

  for (unsigned i = 0; i < Initializers.size(); i++) {
    CXXCtorInitializer *Member = Initializers[i];

    if (Member->isBaseInitializer())
      Info.AllBaseFields[Member->getBaseClass()->getAs<RecordType>()] = Member;
    else {
      Info.AllBaseFields[Member->getAnyMember()->getCanonicalDecl()] = Member;

      if (IndirectFieldDecl *F = Member->getIndirectMember()) {
        for (auto *C : F->chain()) {
          FieldDecl *FD = dyn_cast<FieldDecl>(C);
          if (FD && FD->getParent()->isUnion())
            Info.ActiveUnionMember.insert(std::make_pair(
                FD->getParent()->getCanonicalDecl(), FD->getCanonicalDecl()));
        }
      } else if (FieldDecl *FD = Member->getMember()) {
        if (FD->getParent()->isUnion())
          Info.ActiveUnionMember.insert(std::make_pair(
              FD->getParent()->getCanonicalDecl(), FD->getCanonicalDecl()));
      }
    }
  }

  // Keep track of the direct virtual bases.
  llvm::SmallPtrSet<CXXBaseSpecifier *, 16> DirectVBases;
  for (auto &I : ClassDecl->bases()) {
    if (I.isVirtual())
      DirectVBases.insert(&I);
  }

  // Push virtual bases before others.
  for (auto &VBase : ClassDecl->vbases()) {
    if (CXXCtorInitializer *Value
        = Info.AllBaseFields.lookup(VBase.getType()->getAs<RecordType>())) {
      // [class.base.init]p7, per DR257:
      //   A mem-initializer where the mem-initializer-id names a virtual base
      //   class is ignored during execution of a constructor of any class that
      //   is not the most derived class.
      if (ClassDecl->isAbstract()) {
        // FIXME: Provide a fixit to remove the base specifier. This requires
        // tracking the location of the associated comma for a base specifier.
        Diag(Value->getSourceLocation(), diag::warn_abstract_vbase_init_ignored)
          << VBase.getType() << ClassDecl;
        DiagnoseAbstractType(ClassDecl);
      }

      Info.AllToInit.push_back(Value);
    } else if (!AnyErrors && !ClassDecl->isAbstract()) {
      // [class.base.init]p8, per DR257:
      //   If a given [...] base class is not named by a mem-initializer-id
      //   [...] and the entity is not a virtual base class of an abstract
      //   class, then [...] the entity is default-initialized.
      bool IsInheritedVirtualBase = !DirectVBases.count(&VBase);
      CXXCtorInitializer *CXXBaseInit;
      if (BuildImplicitBaseInitializer(*this, Constructor, Info.IIK,
                                       &VBase, IsInheritedVirtualBase,
                                       CXXBaseInit)) {
        HadError = true;
        continue;
      }

      Info.AllToInit.push_back(CXXBaseInit);
    }
  }

  // Non-virtual bases.
  for (auto &Base : ClassDecl->bases()) {
    // Virtuals are in the virtual base list and already constructed.
    if (Base.isVirtual())
      continue;

    if (CXXCtorInitializer *Value
          = Info.AllBaseFields.lookup(Base.getType()->getAs<RecordType>())) {
      Info.AllToInit.push_back(Value);
    } else if (!AnyErrors) {
      CXXCtorInitializer *CXXBaseInit;
      if (BuildImplicitBaseInitializer(*this, Constructor, Info.IIK,
                                       &Base, /*IsInheritedVirtualBase=*/false,
                                       CXXBaseInit)) {
        HadError = true;
        continue;
      }

      Info.AllToInit.push_back(CXXBaseInit);
    }
  }

  // Fields.
  for (auto *Mem : ClassDecl->decls()) {
    if (auto *F = dyn_cast<FieldDecl>(Mem)) {
      // C++ [class.bit]p2:
      //   A declaration for a bit-field that omits the identifier declares an
      //   unnamed bit-field. Unnamed bit-fields are not members and cannot be
      //   initialized.
      if (F->isUnnamedBitField())
        continue;

      // If we're not generating the implicit copy/move constructor, then we'll
      // handle anonymous struct/union fields based on their individual
      // indirect fields.
      if (F->isAnonymousStructOrUnion() && !Info.isImplicitCopyOrMove())
        continue;

      if (CollectFieldInitializer(*this, Info, F))
        HadError = true;
      continue;
    }

    // Beyond this point, we only consider default initialization.
    if (Info.isImplicitCopyOrMove())
      continue;

    if (auto *F = dyn_cast<IndirectFieldDecl>(Mem)) {
      if (F->getType()->isIncompleteArrayType()) {
        assert(ClassDecl->hasFlexibleArrayMember() &&
               "Incomplete array type is not valid");
        continue;
      }

      // Initialize each field of an anonymous struct individually.
      if (CollectFieldInitializer(*this, Info, F->getAnonField(), F))
        HadError = true;

      continue;
    }
  }

  unsigned NumInitializers = Info.AllToInit.size();
  if (NumInitializers > 0) {
    Constructor->setNumCtorInitializers(NumInitializers);
    CXXCtorInitializer **baseOrMemberInitializers =
      new (Context) CXXCtorInitializer*[NumInitializers];
    memcpy(baseOrMemberInitializers, Info.AllToInit.data(),
           NumInitializers * sizeof(CXXCtorInitializer*));
    Constructor->setCtorInitializers(baseOrMemberInitializers);

    // Constructors implicitly reference the base and member
    // destructors.
    MarkBaseAndMemberDestructorsReferenced(Constructor->getLocation(),
                                           Constructor->getParent());
  }

  return HadError;
}

static void PopulateKeysForFields(FieldDecl *Field, SmallVectorImpl<const void*> &IdealInits) {
  if (const RecordType *RT = Field->getType()->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    if (RD->isAnonymousStructOrUnion()) {
      for (auto *Field : RD->fields())
        PopulateKeysForFields(Field, IdealInits);
      return;
    }
  }
  IdealInits.push_back(Field->getCanonicalDecl());
}

static const void *GetKeyForBase(ASTContext &Context, QualType BaseType) {
  return Context.getCanonicalType(BaseType).getTypePtr();
}

static const void *GetKeyForMember(ASTContext &Context,
                                   CXXCtorInitializer *Member) {
  if (!Member->isAnyMemberInitializer())
    return GetKeyForBase(Context, QualType(Member->getBaseClass(), 0));

  return Member->getAnyMember()->getCanonicalDecl();
}

static void AddInitializerToDiag(const Sema::SemaDiagnosticBuilder &Diag,
                                 const CXXCtorInitializer *Previous,
                                 const CXXCtorInitializer *Current) {
  if (Previous->isAnyMemberInitializer())
    Diag << 0 << Previous->getAnyMember();
  else
    Diag << 1 << Previous->getTypeSourceInfo()->getType();

  if (Current->isAnyMemberInitializer())
    Diag << 0 << Current->getAnyMember();
  else
    Diag << 1 << Current->getTypeSourceInfo()->getType();
}

static void DiagnoseBaseOrMemInitializerOrder(
    Sema &SemaRef, const CXXConstructorDecl *Constructor,
    ArrayRef<CXXCtorInitializer *> Inits) {
  if (Constructor->getDeclContext()->isDependentContext())
    return;

  // Don't check initializers order unless the warning is enabled at the
  // location of at least one initializer.
  bool ShouldCheckOrder = false;
  for (unsigned InitIndex = 0; InitIndex != Inits.size(); ++InitIndex) {
    CXXCtorInitializer *Init = Inits[InitIndex];
    if (!SemaRef.Diags.isIgnored(diag::warn_initializer_out_of_order,
                                 Init->getSourceLocation())) {
      ShouldCheckOrder = true;
      break;
    }
  }
  if (!ShouldCheckOrder)
    return;

  // Build the list of bases and members in the order that they'll
  // actually be initialized.  The explicit initializers should be in
  // this same order but may be missing things.
  SmallVector<const void*, 32> IdealInitKeys;

  const CXXRecordDecl *ClassDecl = Constructor->getParent();

  // 1. Virtual bases.
  for (const auto &VBase : ClassDecl->vbases())
    IdealInitKeys.push_back(GetKeyForBase(SemaRef.Context, VBase.getType()));

  // 2. Non-virtual bases.
  for (const auto &Base : ClassDecl->bases()) {
    if (Base.isVirtual())
      continue;
    IdealInitKeys.push_back(GetKeyForBase(SemaRef.Context, Base.getType()));
  }

  // 3. Direct fields.
  for (auto *Field : ClassDecl->fields()) {
    if (Field->isUnnamedBitField())
      continue;

    PopulateKeysForFields(Field, IdealInitKeys);
  }

  unsigned NumIdealInits = IdealInitKeys.size();
  unsigned IdealIndex = 0;

  // Track initializers that are in an incorrect order for either a warning or
  // note if multiple ones occur.
  SmallVector<unsigned> WarnIndexes;
  // Correlates the index of an initializer in the init-list to the index of
  // the field/base in the class.
  SmallVector<std::pair<unsigned, unsigned>, 32> CorrelatedInitOrder;

  for (unsigned InitIndex = 0; InitIndex != Inits.size(); ++InitIndex) {
    const void *InitKey = GetKeyForMember(SemaRef.Context, Inits[InitIndex]);

    // Scan forward to try to find this initializer in the idealized
    // initializers list.
    for (; IdealIndex != NumIdealInits; ++IdealIndex)
      if (InitKey == IdealInitKeys[IdealIndex])
        break;

    // If we didn't find this initializer, it must be because we
    // scanned past it on a previous iteration.  That can only
    // happen if we're out of order;  emit a warning.
    if (IdealIndex == NumIdealInits && InitIndex) {
      WarnIndexes.push_back(InitIndex);

      // Move back to the initializer's location in the ideal list.
      for (IdealIndex = 0; IdealIndex != NumIdealInits; ++IdealIndex)
        if (InitKey == IdealInitKeys[IdealIndex])
          break;

      assert(IdealIndex < NumIdealInits &&
             "initializer not found in initializer list");
    }
    CorrelatedInitOrder.emplace_back(IdealIndex, InitIndex);
  }

  if (WarnIndexes.empty())
    return;

  // Sort based on the ideal order, first in the pair.
  llvm::sort(CorrelatedInitOrder, llvm::less_first());

  // Introduce a new scope as SemaDiagnosticBuilder needs to be destroyed to
  // emit the diagnostic before we can try adding notes.
  {
    Sema::SemaDiagnosticBuilder D = SemaRef.Diag(
        Inits[WarnIndexes.front() - 1]->getSourceLocation(),
        WarnIndexes.size() == 1 ? diag::warn_initializer_out_of_order
                                : diag::warn_some_initializers_out_of_order);

    for (unsigned I = 0; I < CorrelatedInitOrder.size(); ++I) {
      if (CorrelatedInitOrder[I].second == I)
        continue;
      // Ideally we would be using InsertFromRange here, but clang doesn't
      // appear to handle InsertFromRange correctly when the source range is
      // modified by another fix-it.
      D << FixItHint::CreateReplacement(
          Inits[I]->getSourceRange(),
          Lexer::getSourceText(
              CharSourceRange::getTokenRange(
                  Inits[CorrelatedInitOrder[I].second]->getSourceRange()),
              SemaRef.getSourceManager(), SemaRef.getLangOpts()));
    }

    // If there is only 1 item out of order, the warning expects the name and
    // type of each being added to it.
    if (WarnIndexes.size() == 1) {
      AddInitializerToDiag(D, Inits[WarnIndexes.front() - 1],
                           Inits[WarnIndexes.front()]);
      return;
    }
  }
  // More than 1 item to warn, create notes letting the user know which ones
  // are bad.
  for (unsigned WarnIndex : WarnIndexes) {
    const clang::CXXCtorInitializer *PrevInit = Inits[WarnIndex - 1];
    auto D = SemaRef.Diag(PrevInit->getSourceLocation(),
                          diag::note_initializer_out_of_order);
    AddInitializerToDiag(D, PrevInit, Inits[WarnIndex]);
    D << PrevInit->getSourceRange();
  }
}

namespace {
bool CheckRedundantInit(Sema &S,
                        CXXCtorInitializer *Init,
                        CXXCtorInitializer *&PrevInit) {
  if (!PrevInit) {
    PrevInit = Init;
    return false;
  }

  if (FieldDecl *Field = Init->getAnyMember())
    S.Diag(Init->getSourceLocation(),
           diag::err_multiple_mem_initialization)
      << Field->getDeclName()
      << Init->getSourceRange();
  else {
    const Type *BaseClass = Init->getBaseClass();
    assert(BaseClass && "neither field nor base");
    S.Diag(Init->getSourceLocation(),
           diag::err_multiple_base_initialization)
      << QualType(BaseClass, 0)
      << Init->getSourceRange();
  }
  S.Diag(PrevInit->getSourceLocation(), diag::note_previous_initializer)
    << 0 << PrevInit->getSourceRange();

  return true;
}

typedef std::pair<NamedDecl *, CXXCtorInitializer *> UnionEntry;
typedef llvm::DenseMap<RecordDecl*, UnionEntry> RedundantUnionMap;

bool CheckRedundantUnionInit(Sema &S,
                             CXXCtorInitializer *Init,
                             RedundantUnionMap &Unions) {
  FieldDecl *Field = Init->getAnyMember();
  RecordDecl *Parent = Field->getParent();
  NamedDecl *Child = Field;

  while (Parent->isAnonymousStructOrUnion() || Parent->isUnion()) {
    if (Parent->isUnion()) {
      UnionEntry &En = Unions[Parent];
      if (En.first && En.first != Child) {
        S.Diag(Init->getSourceLocation(),
               diag::err_multiple_mem_union_initialization)
          << Field->getDeclName()
          << Init->getSourceRange();
        S.Diag(En.second->getSourceLocation(), diag::note_previous_initializer)
          << 0 << En.second->getSourceRange();
        return true;
      }
      if (!En.first) {
        En.first = Child;
        En.second = Init;
      }
      if (!Parent->isAnonymousStructOrUnion())
        return false;
    }

    Child = Parent;
    Parent = cast<RecordDecl>(Parent->getDeclContext());
  }

  return false;
}
} // namespace

void Sema::ActOnMemInitializers(Decl *ConstructorDecl,
                                SourceLocation ColonLoc,
                                ArrayRef<CXXCtorInitializer*> MemInits,
                                bool AnyErrors) {
  if (!ConstructorDecl)
    return;

  AdjustDeclIfTemplate(ConstructorDecl);

  CXXConstructorDecl *Constructor
    = dyn_cast<CXXConstructorDecl>(ConstructorDecl);

  if (!Constructor) {
    Diag(ColonLoc, diag::err_only_constructors_take_base_inits);
    return;
  }

  // Mapping for the duplicate initializers check.
  // For member initializers, this is keyed with a FieldDecl*.
  // For base initializers, this is keyed with a Type*.
  llvm::DenseMap<const void *, CXXCtorInitializer *> Members;

  // Mapping for the inconsistent anonymous-union initializers check.
  RedundantUnionMap MemberUnions;

  bool HadError = false;
  for (unsigned i = 0; i < MemInits.size(); i++) {
    CXXCtorInitializer *Init = MemInits[i];

    // Set the source order index.
    Init->setSourceOrder(i);

    if (Init->isAnyMemberInitializer()) {
      const void *Key = GetKeyForMember(Context, Init);
      if (CheckRedundantInit(*this, Init, Members[Key]) ||
          CheckRedundantUnionInit(*this, Init, MemberUnions))
        HadError = true;
    } else if (Init->isBaseInitializer()) {
      const void *Key = GetKeyForMember(Context, Init);
      if (CheckRedundantInit(*this, Init, Members[Key]))
        HadError = true;
    } else {
      assert(Init->isDelegatingInitializer());
      // This must be the only initializer
      if (MemInits.size() != 1) {
        Diag(Init->getSourceLocation(),
             diag::err_delegating_initializer_alone)
          << Init->getSourceRange() << MemInits[i ? 0 : 1]->getSourceRange();
        // We will treat this as being the only initializer.
      }
      SetDelegatingInitializer(Constructor, MemInits[i]);
      // Return immediately as the initializer is set.
      return;
    }
  }

  if (HadError)
    return;

  DiagnoseBaseOrMemInitializerOrder(*this, Constructor, MemInits);

  SetCtorInitializers(Constructor, AnyErrors, MemInits);

  DiagnoseUninitializedFields(*this, Constructor);
}

void
Sema::MarkBaseAndMemberDestructorsReferenced(SourceLocation Location,
                                             CXXRecordDecl *ClassDecl) {
  // Ignore dependent contexts. Also ignore unions, since their members never
  // have destructors implicitly called.
  if (ClassDecl->isDependentContext() || ClassDecl->isUnion())
    return;

  // FIXME: all the access-control diagnostics are positioned on the
  // field/base declaration.  That's probably good; that said, the
  // user might reasonably want to know why the destructor is being
  // emitted, and we currently don't say.

  // Non-static data members.
  for (auto *Field : ClassDecl->fields()) {
    if (Field->isInvalidDecl())
      continue;

    // Don't destroy incomplete or zero-length arrays.
    if (isIncompleteOrZeroLengthArrayType(Context, Field->getType()))
      continue;

    QualType FieldType = Context.getBaseElementType(Field->getType());

    const RecordType* RT = FieldType->getAs<RecordType>();
    if (!RT)
      continue;

    CXXRecordDecl *FieldClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    if (FieldClassDecl->isInvalidDecl())
      continue;
    if (FieldClassDecl->hasIrrelevantDestructor())
      continue;
    // The destructor for an implicit anonymous union member is never invoked.
    if (FieldClassDecl->isUnion() && FieldClassDecl->isAnonymousStructOrUnion())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(FieldClassDecl);
    // Dtor might still be missing, e.g because it's invalid.
    if (!Dtor)
      continue;
    CheckDestructorAccess(Field->getLocation(), Dtor,
                          PDiag(diag::err_access_dtor_field)
                            << Field->getDeclName()
                            << FieldType);

    MarkFunctionReferenced(Location, Dtor);
    DiagnoseUseOfDecl(Dtor, Location);
  }

  // We only potentially invoke the destructors of potentially constructed
  // subobjects.
  bool VisitVirtualBases = !ClassDecl->isAbstract();

  // If the destructor exists and has already been marked used in the MS ABI,
  // then virtual base destructors have already been checked and marked used.
  // Skip checking them again to avoid duplicate diagnostics.
  if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
    CXXDestructorDecl *Dtor = ClassDecl->getDestructor();
    if (Dtor && Dtor->isUsed())
      VisitVirtualBases = false;
  }

  llvm::SmallPtrSet<const RecordType *, 8> DirectVirtualBases;

  // Bases.
  for (const auto &Base : ClassDecl->bases()) {
    const RecordType *RT = Base.getType()->getAs<RecordType>();
    if (!RT)
      continue;

    // Remember direct virtual bases.
    if (Base.isVirtual()) {
      if (!VisitVirtualBases)
        continue;
      DirectVirtualBases.insert(RT);
    }

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    // If our base class is invalid, we probably can't get its dtor anyway.
    if (BaseClassDecl->isInvalidDecl())
      continue;
    if (BaseClassDecl->hasIrrelevantDestructor())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(BaseClassDecl);
    // Dtor might still be missing, e.g because it's invalid.
    if (!Dtor)
      continue;

    // FIXME: caret should be on the start of the class name
    CheckDestructorAccess(Base.getBeginLoc(), Dtor,
                          PDiag(diag::err_access_dtor_base)
                              << Base.getType() << Base.getSourceRange(),
                          Context.getTypeDeclType(ClassDecl));

    MarkFunctionReferenced(Location, Dtor);
    DiagnoseUseOfDecl(Dtor, Location);
  }

  if (VisitVirtualBases)
    MarkVirtualBaseDestructorsReferenced(Location, ClassDecl,
                                         &DirectVirtualBases);
}

void Sema::MarkVirtualBaseDestructorsReferenced(
    SourceLocation Location, CXXRecordDecl *ClassDecl,
    llvm::SmallPtrSetImpl<const RecordType *> *DirectVirtualBases) {
  // Virtual bases.
  for (const auto &VBase : ClassDecl->vbases()) {
    // Bases are always records in a well-formed non-dependent class.
    const RecordType *RT = VBase.getType()->castAs<RecordType>();

    // Ignore already visited direct virtual bases.
    if (DirectVirtualBases && DirectVirtualBases->count(RT))
      continue;

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    // If our base class is invalid, we probably can't get its dtor anyway.
    if (BaseClassDecl->isInvalidDecl())
      continue;
    if (BaseClassDecl->hasIrrelevantDestructor())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(BaseClassDecl);
    // Dtor might still be missing, e.g because it's invalid.
    if (!Dtor)
      continue;
    if (CheckDestructorAccess(
            ClassDecl->getLocation(), Dtor,
            PDiag(diag::err_access_dtor_vbase)
                << Context.getTypeDeclType(ClassDecl) << VBase.getType(),
            Context.getTypeDeclType(ClassDecl)) ==
        AR_accessible) {
      CheckDerivedToBaseConversion(
          Context.getTypeDeclType(ClassDecl), VBase.getType(),
          diag::err_access_dtor_vbase, 0, ClassDecl->getLocation(),
          SourceRange(), DeclarationName(), nullptr);
    }

    MarkFunctionReferenced(Location, Dtor);
    DiagnoseUseOfDecl(Dtor, Location);
  }
}

void Sema::ActOnDefaultCtorInitializers(Decl *CDtorDecl) {
  if (!CDtorDecl)
    return;

  if (CXXConstructorDecl *Constructor
      = dyn_cast<CXXConstructorDecl>(CDtorDecl)) {
    if (CXXRecordDecl *ClassDecl = Constructor->getParent();
        !ClassDecl || ClassDecl->isInvalidDecl()) {
      return;
    }
    SetCtorInitializers(Constructor, /*AnyErrors=*/false);
    DiagnoseUninitializedFields(*this, Constructor);
  }
}

bool Sema::isAbstractType(SourceLocation Loc, QualType T) {
  if (!getLangOpts().CPlusPlus)
    return false;

  const auto *RD = Context.getBaseElementType(T)->getAsCXXRecordDecl();
  if (!RD)
    return false;

  // FIXME: Per [temp.inst]p1, we are supposed to trigger instantiation of a
  // class template specialization here, but doing so breaks a lot of code.

  // We can't answer whether something is abstract until it has a
  // definition. If it's currently being defined, we'll walk back
  // over all the declarations when we have a full definition.
  const CXXRecordDecl *Def = RD->getDefinition();
  if (!Def || Def->isBeingDefined())
    return false;

  return RD->isAbstract();
}

bool Sema::RequireNonAbstractType(SourceLocation Loc, QualType T,
                                  TypeDiagnoser &Diagnoser) {
  if (!isAbstractType(Loc, T))
    return false;

  T = Context.getBaseElementType(T);
  Diagnoser.diagnose(*this, Loc, T);
  DiagnoseAbstractType(T->getAsCXXRecordDecl());
  return true;
}

void Sema::DiagnoseAbstractType(const CXXRecordDecl *RD) {
  // Check if we've already emitted the list of pure virtual functions
  // for this class.
  if (PureVirtualClassDiagSet && PureVirtualClassDiagSet->count(RD))
    return;

  // If the diagnostic is suppressed, don't emit the notes. We're only
  // going to emit them once, so try to attach them to a diagnostic we're
  // actually going to show.
  if (Diags.isLastDiagnosticIgnored())
    return;

  CXXFinalOverriderMap FinalOverriders;
  RD->getFinalOverriders(FinalOverriders);

  // Keep a set of seen pure methods so we won't diagnose the same method
  // more than once.
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> SeenPureMethods;

  for (CXXFinalOverriderMap::iterator M = FinalOverriders.begin(),
                                   MEnd = FinalOverriders.end();
       M != MEnd;
       ++M) {
    for (OverridingMethods::iterator SO = M->second.begin(),
                                  SOEnd = M->second.end();
         SO != SOEnd; ++SO) {
      // C++ [class.abstract]p4:
      //   A class is abstract if it contains or inherits at least one
      //   pure virtual function for which the final overrider is pure
      //   virtual.

      //
      if (SO->second.size() != 1)
        continue;

      if (!SO->second.front().Method->isPureVirtual())
        continue;

      if (!SeenPureMethods.insert(SO->second.front().Method).second)
        continue;

      Diag(SO->second.front().Method->getLocation(),
           diag::note_pure_virtual_function)
        << SO->second.front().Method->getDeclName() << RD->getDeclName();
    }
  }

  if (!PureVirtualClassDiagSet)
    PureVirtualClassDiagSet.reset(new RecordDeclSetTy);
  PureVirtualClassDiagSet->insert(RD);
}

namespace {
struct AbstractUsageInfo {
  Sema &S;
  CXXRecordDecl *Record;
  CanQualType AbstractType;
  bool Invalid;

  AbstractUsageInfo(Sema &S, CXXRecordDecl *Record)
    : S(S), Record(Record),
      AbstractType(S.Context.getCanonicalType(
                   S.Context.getTypeDeclType(Record))),
      Invalid(false) {}

  void DiagnoseAbstractType() {
    if (Invalid) return;
    S.DiagnoseAbstractType(Record);
    Invalid = true;
  }

  void CheckType(const NamedDecl *D, TypeLoc TL, Sema::AbstractDiagSelID Sel);
};

struct CheckAbstractUsage {
  AbstractUsageInfo &Info;
  const NamedDecl *Ctx;

  CheckAbstractUsage(AbstractUsageInfo &Info, const NamedDecl *Ctx)
    : Info(Info), Ctx(Ctx) {}

  void Visit(TypeLoc TL, Sema::AbstractDiagSelID Sel) {
    switch (TL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
    case TypeLoc::CLASS: Check(TL.castAs<CLASS##TypeLoc>(), Sel); break;
#include "clang/AST/TypeLocNodes.def"
    }
  }

  void Check(FunctionProtoTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    Visit(TL.getReturnLoc(), Sema::AbstractReturnType);
    for (unsigned I = 0, E = TL.getNumParams(); I != E; ++I) {
      if (!TL.getParam(I))
        continue;

      TypeSourceInfo *TSI = TL.getParam(I)->getTypeSourceInfo();
      if (TSI) Visit(TSI->getTypeLoc(), Sema::AbstractParamType);
    }
  }

  void Check(ArrayTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    Visit(TL.getElementLoc(), Sema::AbstractArrayType);
  }

  void Check(TemplateSpecializationTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    // Visit the type parameters from a permissive context.
    for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I) {
      TemplateArgumentLoc TAL = TL.getArgLoc(I);
      if (TAL.getArgument().getKind() == TemplateArgument::Type)
        if (TypeSourceInfo *TSI = TAL.getTypeSourceInfo())
          Visit(TSI->getTypeLoc(), Sema::AbstractNone);
      // TODO: other template argument types?
    }
  }

  // Visit pointee types from a permissive context.
#define CheckPolymorphic(Type) \
  void Check(Type TL, Sema::AbstractDiagSelID Sel) { \
    Visit(TL.getNextTypeLoc(), Sema::AbstractNone); \
  }
  CheckPolymorphic(PointerTypeLoc)
  CheckPolymorphic(ReferenceTypeLoc)
  CheckPolymorphic(MemberPointerTypeLoc)
  CheckPolymorphic(BlockPointerTypeLoc)
  CheckPolymorphic(AtomicTypeLoc)

  /// Handle all the types we haven't given a more specific
  /// implementation for above.
  void Check(TypeLoc TL, Sema::AbstractDiagSelID Sel) {
    // Every other kind of type that we haven't called out already
    // that has an inner type is either (1) sugar or (2) contains that
    // inner type in some way as a subobject.
    if (TypeLoc Next = TL.getNextTypeLoc())
      return Visit(Next, Sel);

    // If there's no inner type and we're in a permissive context,
    // don't diagnose.
    if (Sel == Sema::AbstractNone) return;

    // Check whether the type matches the abstract type.
    QualType T = TL.getType();
    if (T->isArrayType()) {
      Sel = Sema::AbstractArrayType;
      T = Info.S.Context.getBaseElementType(T);
    }
    CanQualType CT = T->getCanonicalTypeUnqualified().getUnqualifiedType();
    if (CT != Info.AbstractType) return;

    // It matched; do some magic.
    // FIXME: These should be at most warnings. See P0929R2, CWG1640, CWG1646.
    if (Sel == Sema::AbstractArrayType) {
      Info.S.Diag(Ctx->getLocation(), diag::err_array_of_abstract_type)
        << T << TL.getSourceRange();
    } else {
      Info.S.Diag(Ctx->getLocation(), diag::err_abstract_type_in_decl)
        << Sel << T << TL.getSourceRange();
    }
    Info.DiagnoseAbstractType();
  }
};

void AbstractUsageInfo::CheckType(const NamedDecl *D, TypeLoc TL,
                                  Sema::AbstractDiagSelID Sel) {
  CheckAbstractUsage(*this, D).Visit(TL, Sel);
}

}

/// Check for invalid uses of an abstract type in a function declaration.
static void CheckAbstractClassUsage(AbstractUsageInfo &Info,
                                    FunctionDecl *FD) {
  // Only definitions are required to refer to complete and
  // non-abstract types.
  if (!FD->doesThisDeclarationHaveABody())
    return;

  // For safety's sake, just ignore it if we don't have type source
  // information.  This should never happen for non-implicit methods,
  // but...
  if (TypeSourceInfo *TSI = FD->getTypeSourceInfo())
    Info.CheckType(FD, TSI->getTypeLoc(), Sema::AbstractNone);
}

/// Check for invalid uses of an abstract type in a variable0 declaration.
static void CheckAbstractClassUsage(AbstractUsageInfo &Info,
                                    VarDecl *VD) {
  // No need to do the check on definitions, which require that
  // the type is complete.
  if (VD->isThisDeclarationADefinition())
    return;

  Info.CheckType(VD, VD->getTypeSourceInfo()->getTypeLoc(),
                 Sema::AbstractVariableType);
}

/// Check for invalid uses of an abstract type within a class definition.
static void CheckAbstractClassUsage(AbstractUsageInfo &Info,
                                    CXXRecordDecl *RD) {
  for (auto *D : RD->decls()) {
    if (D->isImplicit()) continue;

    // Step through friends to the befriended declaration.
    if (auto *FD = dyn_cast<FriendDecl>(D)) {
      D = FD->getFriendDecl();
      if (!D) continue;
    }

    // Functions and function templates.
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      CheckAbstractClassUsage(Info, FD);
    } else if (auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
      CheckAbstractClassUsage(Info, FTD->getTemplatedDecl());

    // Fields and static variables.
    } else if (auto *FD = dyn_cast<FieldDecl>(D)) {
      if (TypeSourceInfo *TSI = FD->getTypeSourceInfo())
        Info.CheckType(FD, TSI->getTypeLoc(), Sema::AbstractFieldType);
    } else if (auto *VD = dyn_cast<VarDecl>(D)) {
      CheckAbstractClassUsage(Info, VD);
    } else if (auto *VTD = dyn_cast<VarTemplateDecl>(D)) {
      CheckAbstractClassUsage(Info, VTD->getTemplatedDecl());

    // Nested classes and class templates.
    } else if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
      CheckAbstractClassUsage(Info, RD);
    } else if (auto *CTD = dyn_cast<ClassTemplateDecl>(D)) {
      CheckAbstractClassUsage(Info, CTD->getTemplatedDecl());
    }
  }
}

static void ReferenceDllExportedMembers(Sema &S, CXXRecordDecl *Class) {
  Attr *ClassAttr = getDLLAttr(Class);
  if (!ClassAttr)
    return;

  assert(ClassAttr->getKind() == attr::DLLExport);

  TemplateSpecializationKind TSK = Class->getTemplateSpecializationKind();

  if (TSK == TSK_ExplicitInstantiationDeclaration)
    // Don't go any further if this is just an explicit instantiation
    // declaration.
    return;

  // Add a context note to explain how we got to any diagnostics produced below.
  struct MarkingClassDllexported {
    Sema &S;
    MarkingClassDllexported(Sema &S, CXXRecordDecl *Class,
                            SourceLocation AttrLoc)
        : S(S) {
      Sema::CodeSynthesisContext Ctx;
      Ctx.Kind = Sema::CodeSynthesisContext::MarkingClassDllexported;
      Ctx.PointOfInstantiation = AttrLoc;
      Ctx.Entity = Class;
      S.pushCodeSynthesisContext(Ctx);
    }
    ~MarkingClassDllexported() {
      S.popCodeSynthesisContext();
    }
  } MarkingDllexportedContext(S, Class, ClassAttr->getLocation());

  if (S.Context.getTargetInfo().getTriple().isWindowsGNUEnvironment())
    S.MarkVTableUsed(Class->getLocation(), Class, true);

  for (Decl *Member : Class->decls()) {
    // Skip members that were not marked exported.
    if (!Member->hasAttr<DLLExportAttr>())
      continue;

    // Defined static variables that are members of an exported base
    // class must be marked export too.
    auto *VD = dyn_cast<VarDecl>(Member);
    if (VD && VD->getStorageClass() == SC_Static &&
        TSK == TSK_ImplicitInstantiation)
      S.MarkVariableReferenced(VD->getLocation(), VD);

    auto *MD = dyn_cast<CXXMethodDecl>(Member);
    if (!MD)
      continue;

    if (MD->isUserProvided()) {
      // Instantiate non-default class member functions ...

      // .. except for certain kinds of template specializations.
      if (TSK == TSK_ImplicitInstantiation && !ClassAttr->isInherited())
        continue;

      // If this is an MS ABI dllexport default constructor, instantiate any
      // default arguments.
      if (S.Context.getTargetInfo().getCXXABI().isMicrosoft()) {
        auto *CD = dyn_cast<CXXConstructorDecl>(MD);
        if (CD && CD->isDefaultConstructor() && TSK == TSK_Undeclared) {
          S.InstantiateDefaultCtorDefaultArgs(CD);
        }
      }

      S.MarkFunctionReferenced(Class->getLocation(), MD);

      // The function will be passed to the consumer when its definition is
      // encountered.
    } else if (MD->isExplicitlyDefaulted()) {
      // Synthesize and instantiate explicitly defaulted methods.
      S.MarkFunctionReferenced(Class->getLocation(), MD);

      if (TSK != TSK_ExplicitInstantiationDefinition) {
        // Except for explicit instantiation defs, we will not see the
        // definition again later, so pass it to the consumer now.
        S.Consumer.HandleTopLevelDecl(DeclGroupRef(MD));
      }
    } else if (!MD->isTrivial() ||
               MD->isCopyAssignmentOperator() ||
               MD->isMoveAssignmentOperator()) {
      // Synthesize and instantiate non-trivial implicit methods, and the copy
      // and move assignment operators. The latter are exported even if they
      // are trivial, because the address of an operator can be taken and
      // should compare equal across libraries.
      S.MarkFunctionReferenced(Class->getLocation(), MD);

      // There is no later point when we will see the definition of this
      // function, so pass it to the consumer now.
      S.Consumer.HandleTopLevelDecl(DeclGroupRef(MD));
    }
  }
}

static void checkForMultipleExportedDefaultConstructors(Sema &S,
                                                        CXXRecordDecl *Class) {
  // Only the MS ABI has default constructor closures, so we don't need to do
  // this semantic checking anywhere else.
  if (!S.Context.getTargetInfo().getCXXABI().isMicrosoft())
    return;

  CXXConstructorDecl *LastExportedDefaultCtor = nullptr;
  for (Decl *Member : Class->decls()) {
    // Look for exported default constructors.
    auto *CD = dyn_cast<CXXConstructorDecl>(Member);
    if (!CD || !CD->isDefaultConstructor())
      continue;
    auto *Attr = CD->getAttr<DLLExportAttr>();
    if (!Attr)
      continue;

    // If the class is non-dependent, mark the default arguments as ODR-used so
    // that we can properly codegen the constructor closure.
    if (!Class->isDependentContext()) {
      for (ParmVarDecl *PD : CD->parameters()) {
        (void)S.CheckCXXDefaultArgExpr(Attr->getLocation(), CD, PD);
        S.DiscardCleanupsInEvaluationContext();
      }
    }

    if (LastExportedDefaultCtor) {
      S.Diag(LastExportedDefaultCtor->getLocation(),
             diag::err_attribute_dll_ambiguous_default_ctor)
          << Class;
      S.Diag(CD->getLocation(), diag::note_entity_declared_at)
          << CD->getDeclName();
      return;
    }
    LastExportedDefaultCtor = CD;
  }
}

static void checkCUDADeviceBuiltinSurfaceClassTemplate(Sema &S,
                                                       CXXRecordDecl *Class) {
  bool ErrorReported = false;
  auto reportIllegalClassTemplate = [&ErrorReported](Sema &S,
                                                     ClassTemplateDecl *TD) {
    if (ErrorReported)
      return;
    S.Diag(TD->getLocation(),
           diag::err_cuda_device_builtin_surftex_cls_template)
        << /*surface*/ 0 << TD;
    ErrorReported = true;
  };

  ClassTemplateDecl *TD = Class->getDescribedClassTemplate();
  if (!TD) {
    auto *SD = dyn_cast<ClassTemplateSpecializationDecl>(Class);
    if (!SD) {
      S.Diag(Class->getLocation(),
             diag::err_cuda_device_builtin_surftex_ref_decl)
          << /*surface*/ 0 << Class;
      S.Diag(Class->getLocation(),
             diag::note_cuda_device_builtin_surftex_should_be_template_class)
          << Class;
      return;
    }
    TD = SD->getSpecializedTemplate();
  }

  TemplateParameterList *Params = TD->getTemplateParameters();
  unsigned N = Params->size();

  if (N != 2) {
    reportIllegalClassTemplate(S, TD);
    S.Diag(TD->getLocation(),
           diag::note_cuda_device_builtin_surftex_cls_should_have_n_args)
        << TD << 2;
  }
  if (N > 0 && !isa<TemplateTypeParmDecl>(Params->getParam(0))) {
    reportIllegalClassTemplate(S, TD);
    S.Diag(TD->getLocation(),
           diag::note_cuda_device_builtin_surftex_cls_should_have_match_arg)
        << TD << /*1st*/ 0 << /*type*/ 0;
  }
  if (N > 1) {
    auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Params->getParam(1));
    if (!NTTP || !NTTP->getType()->isIntegralOrEnumerationType()) {
      reportIllegalClassTemplate(S, TD);
      S.Diag(TD->getLocation(),
             diag::note_cuda_device_builtin_surftex_cls_should_have_match_arg)
          << TD << /*2nd*/ 1 << /*integer*/ 1;
    }
  }
}

static void checkCUDADeviceBuiltinTextureClassTemplate(Sema &S,
                                                       CXXRecordDecl *Class) {
  bool ErrorReported = false;
  auto reportIllegalClassTemplate = [&ErrorReported](Sema &S,
                                                     ClassTemplateDecl *TD) {
    if (ErrorReported)
      return;
    S.Diag(TD->getLocation(),
           diag::err_cuda_device_builtin_surftex_cls_template)
        << /*texture*/ 1 << TD;
    ErrorReported = true;
  };

  ClassTemplateDecl *TD = Class->getDescribedClassTemplate();
  if (!TD) {
    auto *SD = dyn_cast<ClassTemplateSpecializationDecl>(Class);
    if (!SD) {
      S.Diag(Class->getLocation(),
             diag::err_cuda_device_builtin_surftex_ref_decl)
          << /*texture*/ 1 << Class;
      S.Diag(Class->getLocation(),
             diag::note_cuda_device_builtin_surftex_should_be_template_class)
          << Class;
      return;
    }
    TD = SD->getSpecializedTemplate();
  }

  TemplateParameterList *Params = TD->getTemplateParameters();
  unsigned N = Params->size();

  if (N != 3) {
    reportIllegalClassTemplate(S, TD);
    S.Diag(TD->getLocation(),
           diag::note_cuda_device_builtin_surftex_cls_should_have_n_args)
        << TD << 3;
  }
  if (N > 0 && !isa<TemplateTypeParmDecl>(Params->getParam(0))) {
    reportIllegalClassTemplate(S, TD);
    S.Diag(TD->getLocation(),
           diag::note_cuda_device_builtin_surftex_cls_should_have_match_arg)
        << TD << /*1st*/ 0 << /*type*/ 0;
  }
  if (N > 1) {
    auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Params->getParam(1));
    if (!NTTP || !NTTP->getType()->isIntegralOrEnumerationType()) {
      reportIllegalClassTemplate(S, TD);
      S.Diag(TD->getLocation(),
             diag::note_cuda_device_builtin_surftex_cls_should_have_match_arg)
          << TD << /*2nd*/ 1 << /*integer*/ 1;
    }
  }
  if (N > 2) {
    auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Params->getParam(2));
    if (!NTTP || !NTTP->getType()->isIntegralOrEnumerationType()) {
      reportIllegalClassTemplate(S, TD);
      S.Diag(TD->getLocation(),
             diag::note_cuda_device_builtin_surftex_cls_should_have_match_arg)
          << TD << /*3rd*/ 2 << /*integer*/ 1;
    }
  }
}

void Sema::checkClassLevelCodeSegAttribute(CXXRecordDecl *Class) {
  // Mark any compiler-generated routines with the implicit code_seg attribute.
  for (auto *Method : Class->methods()) {
    if (Method->isUserProvided())
      continue;
    if (Attr *A = getImplicitCodeSegOrSectionAttrForFunction(Method, /*IsDefinition=*/true))
      Method->addAttr(A);
  }
}

void Sema::checkClassLevelDLLAttribute(CXXRecordDecl *Class) {
  Attr *ClassAttr = getDLLAttr(Class);

  // MSVC inherits DLL attributes to partial class template specializations.
  if (Context.getTargetInfo().shouldDLLImportComdatSymbols() && !ClassAttr) {
    if (auto *Spec = dyn_cast<ClassTemplatePartialSpecializationDecl>(Class)) {
      if (Attr *TemplateAttr =
              getDLLAttr(Spec->getSpecializedTemplate()->getTemplatedDecl())) {
        auto *A = cast<InheritableAttr>(TemplateAttr->clone(getASTContext()));
        A->setInherited(true);
        ClassAttr = A;
      }
    }
  }

  if (!ClassAttr)
    return;

  // MSVC allows imported or exported template classes that have UniqueExternal
  // linkage. This occurs when the template class has been instantiated with
  // a template parameter which itself has internal linkage.
  // We drop the attribute to avoid exporting or importing any members.
  if ((Context.getTargetInfo().getCXXABI().isMicrosoft() ||
       Context.getTargetInfo().getTriple().isPS()) &&
      (!Class->isExternallyVisible() && Class->hasExternalFormalLinkage())) {
    Class->dropAttrs<DLLExportAttr, DLLImportAttr>();
    return;
  }

  if (!Class->isExternallyVisible()) {
    Diag(Class->getLocation(), diag::err_attribute_dll_not_extern)
        << Class << ClassAttr;
    return;
  }

  if (Context.getTargetInfo().shouldDLLImportComdatSymbols() &&
      !ClassAttr->isInherited()) {
    // Diagnose dll attributes on members of class with dll attribute.
    for (Decl *Member : Class->decls()) {
      if (!isa<VarDecl>(Member) && !isa<CXXMethodDecl>(Member))
        continue;
      InheritableAttr *MemberAttr = getDLLAttr(Member);
      if (!MemberAttr || MemberAttr->isInherited() || Member->isInvalidDecl())
        continue;

      Diag(MemberAttr->getLocation(),
             diag::err_attribute_dll_member_of_dll_class)
          << MemberAttr << ClassAttr;
      Diag(ClassAttr->getLocation(), diag::note_previous_attribute);
      Member->setInvalidDecl();
    }
  }

  if (Class->getDescribedClassTemplate())
    // Don't inherit dll attribute until the template is instantiated.
    return;

  // The class is either imported or exported.
  const bool ClassExported = ClassAttr->getKind() == attr::DLLExport;

  // Check if this was a dllimport attribute propagated from a derived class to
  // a base class template specialization. We don't apply these attributes to
  // static data members.
  const bool PropagatedImport =
      !ClassExported &&
      cast<DLLImportAttr>(ClassAttr)->wasPropagatedToBaseTemplate();

  TemplateSpecializationKind TSK = Class->getTemplateSpecializationKind();

  // Ignore explicit dllexport on explicit class template instantiation
  // declarations, except in MinGW mode.
  if (ClassExported && !ClassAttr->isInherited() &&
      TSK == TSK_ExplicitInstantiationDeclaration &&
      !Context.getTargetInfo().getTriple().isWindowsGNUEnvironment()) {
    Class->dropAttr<DLLExportAttr>();
    return;
  }

  // Force declaration of implicit members so they can inherit the attribute.
  ForceDeclarationOfImplicitMembers(Class);

  // FIXME: MSVC's docs say all bases must be exportable, but this doesn't
  // seem to be true in practice?

  for (Decl *Member : Class->decls()) {
    VarDecl *VD = dyn_cast<VarDecl>(Member);
    CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Member);

    // Only methods and static fields inherit the attributes.
    if (!VD && !MD)
      continue;

    if (MD) {
      // Don't process deleted methods.
      if (MD->isDeleted())
        continue;

      if (MD->isInlined()) {
        // MinGW does not import or export inline methods. But do it for
        // template instantiations.
        if (!Context.getTargetInfo().shouldDLLImportComdatSymbols() &&
            TSK != TSK_ExplicitInstantiationDeclaration &&
            TSK != TSK_ExplicitInstantiationDefinition)
          continue;

        // MSVC versions before 2015 don't export the move assignment operators
        // and move constructor, so don't attempt to import/export them if
        // we have a definition.
        auto *Ctor = dyn_cast<CXXConstructorDecl>(MD);
        if ((MD->isMoveAssignmentOperator() ||
             (Ctor && Ctor->isMoveConstructor())) &&
            !getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015))
          continue;

        // MSVC2015 doesn't export trivial defaulted x-tor but copy assign
        // operator is exported anyway.
        if (getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015) &&
            (Ctor || isa<CXXDestructorDecl>(MD)) && MD->isTrivial())
          continue;
      }
    }

    // Don't apply dllimport attributes to static data members of class template
    // instantiations when the attribute is propagated from a derived class.
    if (VD && PropagatedImport)
      continue;

    if (!cast<NamedDecl>(Member)->isExternallyVisible())
      continue;

    if (!getDLLAttr(Member)) {
      InheritableAttr *NewAttr = nullptr;

      // Do not export/import inline function when -fno-dllexport-inlines is
      // passed. But add attribute for later local static var check.
      if (!getLangOpts().DllExportInlines && MD && MD->isInlined() &&
          TSK != TSK_ExplicitInstantiationDeclaration &&
          TSK != TSK_ExplicitInstantiationDefinition) {
        if (ClassExported) {
          NewAttr = ::new (getASTContext())
              DLLExportStaticLocalAttr(getASTContext(), *ClassAttr);
        } else {
          NewAttr = ::new (getASTContext())
              DLLImportStaticLocalAttr(getASTContext(), *ClassAttr);
        }
      } else {
        NewAttr = cast<InheritableAttr>(ClassAttr->clone(getASTContext()));
      }

      NewAttr->setInherited(true);
      Member->addAttr(NewAttr);

      if (MD) {
        // Propagate DLLAttr to friend re-declarations of MD that have already
        // been constructed.
        for (FunctionDecl *FD = MD->getMostRecentDecl(); FD;
             FD = FD->getPreviousDecl()) {
          if (FD->getFriendObjectKind() == Decl::FOK_None)
            continue;
          assert(!getDLLAttr(FD) &&
                 "friend re-decl should not already have a DLLAttr");
          NewAttr = cast<InheritableAttr>(ClassAttr->clone(getASTContext()));
          NewAttr->setInherited(true);
          FD->addAttr(NewAttr);
        }
      }
    }
  }

  if (ClassExported)
    DelayedDllExportClasses.push_back(Class);
}

void Sema::propagateDLLAttrToBaseClassTemplate(
    CXXRecordDecl *Class, Attr *ClassAttr,
    ClassTemplateSpecializationDecl *BaseTemplateSpec, SourceLocation BaseLoc) {
  if (getDLLAttr(
          BaseTemplateSpec->getSpecializedTemplate()->getTemplatedDecl())) {
    // If the base class template has a DLL attribute, don't try to change it.
    return;
  }

  auto TSK = BaseTemplateSpec->getSpecializationKind();
  if (!getDLLAttr(BaseTemplateSpec) &&
      (TSK == TSK_Undeclared || TSK == TSK_ExplicitInstantiationDeclaration ||
       TSK == TSK_ImplicitInstantiation)) {
    // The template hasn't been instantiated yet (or it has, but only as an
    // explicit instantiation declaration or implicit instantiation, which means
    // we haven't codegenned any members yet), so propagate the attribute.
    auto *NewAttr = cast<InheritableAttr>(ClassAttr->clone(getASTContext()));
    NewAttr->setInherited(true);
    BaseTemplateSpec->addAttr(NewAttr);

    // If this was an import, mark that we propagated it from a derived class to
    // a base class template specialization.
    if (auto *ImportAttr = dyn_cast<DLLImportAttr>(NewAttr))
      ImportAttr->setPropagatedToBaseTemplate();

    // If the template is already instantiated, checkDLLAttributeRedeclaration()
    // needs to be run again to work see the new attribute. Otherwise this will
    // get run whenever the template is instantiated.
    if (TSK != TSK_Undeclared)
      checkClassLevelDLLAttribute(BaseTemplateSpec);

    return;
  }

  if (getDLLAttr(BaseTemplateSpec)) {
    // The template has already been specialized or instantiated with an
    // attribute, explicitly or through propagation. We should not try to change
    // it.
    return;
  }

  // The template was previously instantiated or explicitly specialized without
  // a dll attribute, It's too late for us to add an attribute, so warn that
  // this is unsupported.
  Diag(BaseLoc, diag::warn_attribute_dll_instantiated_base_class)
      << BaseTemplateSpec->isExplicitSpecialization();
  Diag(ClassAttr->getLocation(), diag::note_attribute);
  if (BaseTemplateSpec->isExplicitSpecialization()) {
    Diag(BaseTemplateSpec->getLocation(),
           diag::note_template_class_explicit_specialization_was_here)
        << BaseTemplateSpec;
  } else {
    Diag(BaseTemplateSpec->getPointOfInstantiation(),
           diag::note_template_class_instantiation_was_here)
        << BaseTemplateSpec;
  }
}

Sema::DefaultedFunctionKind
Sema::getDefaultedFunctionKind(const FunctionDecl *FD) {
  if (auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
    if (const CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(FD)) {
      if (Ctor->isDefaultConstructor())
        return CXXSpecialMemberKind::DefaultConstructor;

      if (Ctor->isCopyConstructor())
        return CXXSpecialMemberKind::CopyConstructor;

      if (Ctor->isMoveConstructor())
        return CXXSpecialMemberKind::MoveConstructor;
    }

    if (MD->isCopyAssignmentOperator())
      return CXXSpecialMemberKind::CopyAssignment;

    if (MD->isMoveAssignmentOperator())
      return CXXSpecialMemberKind::MoveAssignment;

    if (isa<CXXDestructorDecl>(FD))
      return CXXSpecialMemberKind::Destructor;
  }

  switch (FD->getDeclName().getCXXOverloadedOperator()) {
  case OO_EqualEqual:
    return DefaultedComparisonKind::Equal;

  case OO_ExclaimEqual:
    return DefaultedComparisonKind::NotEqual;

  case OO_Spaceship:
    // No point allowing this if <=> doesn't exist in the current language mode.
    if (!getLangOpts().CPlusPlus20)
      break;
    return DefaultedComparisonKind::ThreeWay;

  case OO_Less:
  case OO_LessEqual:
  case OO_Greater:
  case OO_GreaterEqual:
    // No point allowing this if <=> doesn't exist in the current language mode.
    if (!getLangOpts().CPlusPlus20)
      break;
    return DefaultedComparisonKind::Relational;

  default:
    break;
  }

  // Not defaultable.
  return DefaultedFunctionKind();
}

static void DefineDefaultedFunction(Sema &S, FunctionDecl *FD,
                                    SourceLocation DefaultLoc) {
  Sema::DefaultedFunctionKind DFK = S.getDefaultedFunctionKind(FD);
  if (DFK.isComparison())
    return S.DefineDefaultedComparison(DefaultLoc, FD, DFK.asComparison());

  switch (DFK.asSpecialMember()) {
  case CXXSpecialMemberKind::DefaultConstructor:
    S.DefineImplicitDefaultConstructor(DefaultLoc,
                                       cast<CXXConstructorDecl>(FD));
    break;
  case CXXSpecialMemberKind::CopyConstructor:
    S.DefineImplicitCopyConstructor(DefaultLoc, cast<CXXConstructorDecl>(FD));
    break;
  case CXXSpecialMemberKind::CopyAssignment:
    S.DefineImplicitCopyAssignment(DefaultLoc, cast<CXXMethodDecl>(FD));
    break;
  case CXXSpecialMemberKind::Destructor:
    S.DefineImplicitDestructor(DefaultLoc, cast<CXXDestructorDecl>(FD));
    break;
  case CXXSpecialMemberKind::MoveConstructor:
    S.DefineImplicitMoveConstructor(DefaultLoc, cast<CXXConstructorDecl>(FD));
    break;
  case CXXSpecialMemberKind::MoveAssignment:
    S.DefineImplicitMoveAssignment(DefaultLoc, cast<CXXMethodDecl>(FD));
    break;
  case CXXSpecialMemberKind::Invalid:
    llvm_unreachable("Invalid special member.");
  }
}

/// Determine whether a type is permitted to be passed or returned in
/// registers, per C++ [class.temporary]p3.
static bool canPassInRegisters(Sema &S, CXXRecordDecl *D,
                               TargetInfo::CallingConvKind CCK) {
  if (D->isDependentType() || D->isInvalidDecl())
    return false;

  // Clang <= 4 used the pre-C++11 rule, which ignores move operations.
  // The PS4 platform ABI follows the behavior of Clang 3.2.
  if (CCK == TargetInfo::CCK_ClangABI4OrPS4)
    return !D->hasNonTrivialDestructorForCall() &&
           !D->hasNonTrivialCopyConstructorForCall();

  if (CCK == TargetInfo::CCK_MicrosoftWin64) {
    bool CopyCtorIsTrivial = false, CopyCtorIsTrivialForCall = false;
    bool DtorIsTrivialForCall = false;

    // If a class has at least one eligible, trivial copy constructor, it
    // is passed according to the C ABI. Otherwise, it is passed indirectly.
    //
    // Note: This permits classes with non-trivial copy or move ctors to be
    // passed in registers, so long as they *also* have a trivial copy ctor,
    // which is non-conforming.
    if (D->needsImplicitCopyConstructor()) {
      if (!D->defaultedCopyConstructorIsDeleted()) {
        if (D->hasTrivialCopyConstructor())
          CopyCtorIsTrivial = true;
        if (D->hasTrivialCopyConstructorForCall())
          CopyCtorIsTrivialForCall = true;
      }
    } else {
      for (const CXXConstructorDecl *CD : D->ctors()) {
        if (CD->isCopyConstructor() && !CD->isDeleted() &&
            !CD->isIneligibleOrNotSelected()) {
          if (CD->isTrivial())
            CopyCtorIsTrivial = true;
          if (CD->isTrivialForCall())
            CopyCtorIsTrivialForCall = true;
        }
      }
    }

    if (D->needsImplicitDestructor()) {
      if (!D->defaultedDestructorIsDeleted() &&
          D->hasTrivialDestructorForCall())
        DtorIsTrivialForCall = true;
    } else if (const auto *DD = D->getDestructor()) {
      if (!DD->isDeleted() && DD->isTrivialForCall())
        DtorIsTrivialForCall = true;
    }

    // If the copy ctor and dtor are both trivial-for-calls, pass direct.
    if (CopyCtorIsTrivialForCall && DtorIsTrivialForCall)
      return true;

    // If a class has a destructor, we'd really like to pass it indirectly
    // because it allows us to elide copies.  Unfortunately, MSVC makes that
    // impossible for small types, which it will pass in a single register or
    // stack slot. Most objects with dtors are large-ish, so handle that early.
    // We can't call out all large objects as being indirect because there are
    // multiple x64 calling conventions and the C++ ABI code shouldn't dictate
    // how we pass large POD types.

    // Note: This permits small classes with nontrivial destructors to be
    // passed in registers, which is non-conforming.
    bool isAArch64 = S.Context.getTargetInfo().getTriple().isAArch64();
    uint64_t TypeSize = isAArch64 ? 128 : 64;

    if (CopyCtorIsTrivial &&
        S.getASTContext().getTypeSize(D->getTypeForDecl()) <= TypeSize)
      return true;
    return false;
  }

  // Per C++ [class.temporary]p3, the relevant condition is:
  //   each copy constructor, move constructor, and destructor of X is
  //   either trivial or deleted, and X has at least one non-deleted copy
  //   or move constructor
  bool HasNonDeletedCopyOrMove = false;

  if (D->needsImplicitCopyConstructor() &&
      !D->defaultedCopyConstructorIsDeleted()) {
    if (!D->hasTrivialCopyConstructorForCall())
      return false;
    HasNonDeletedCopyOrMove = true;
  }

  if (S.getLangOpts().CPlusPlus11 && D->needsImplicitMoveConstructor() &&
      !D->defaultedMoveConstructorIsDeleted()) {
    if (!D->hasTrivialMoveConstructorForCall())
      return false;
    HasNonDeletedCopyOrMove = true;
  }

  if (D->needsImplicitDestructor() && !D->defaultedDestructorIsDeleted() &&
      !D->hasTrivialDestructorForCall())
    return false;

  for (const CXXMethodDecl *MD : D->methods()) {
    if (MD->isDeleted() || MD->isIneligibleOrNotSelected())
      continue;

    auto *CD = dyn_cast<CXXConstructorDecl>(MD);
    if (CD && CD->isCopyOrMoveConstructor())
      HasNonDeletedCopyOrMove = true;
    else if (!isa<CXXDestructorDecl>(MD))
      continue;

    if (!MD->isTrivialForCall())
      return false;
  }

  return HasNonDeletedCopyOrMove;
}

/// Report an error regarding overriding, along with any relevant
/// overridden methods.
///
/// \param DiagID the primary error to report.
/// \param MD the overriding method.
static bool
ReportOverrides(Sema &S, unsigned DiagID, const CXXMethodDecl *MD,
                llvm::function_ref<bool(const CXXMethodDecl *)> Report) {
  bool IssuedDiagnostic = false;
  for (const CXXMethodDecl *O : MD->overridden_methods()) {
    if (Report(O)) {
      if (!IssuedDiagnostic) {
        S.Diag(MD->getLocation(), DiagID) << MD->getDeclName();
        IssuedDiagnostic = true;
      }
      S.Diag(O->getLocation(), diag::note_overridden_virtual_function);
    }
  }
  return IssuedDiagnostic;
}

void Sema::CheckCompletedCXXClass(Scope *S, CXXRecordDecl *Record) {
  if (!Record)
    return;

  if (Record->isAbstract() && !Record->isInvalidDecl()) {
    AbstractUsageInfo Info(*this, Record);
    CheckAbstractClassUsage(Info, Record);
  }

  // If this is not an aggregate type and has no user-declared constructor,
  // complain about any non-static data members of reference or const scalar
  // type, since they will never get initializers.
  if (!Record->isInvalidDecl() && !Record->isDependentType() &&
      !Record->isAggregate() && !Record->hasUserDeclaredConstructor() &&
      !Record->isLambda()) {
    bool Complained = false;
    for (const auto *F : Record->fields()) {
      if (F->hasInClassInitializer() || F->isUnnamedBitField())
        continue;

      if (F->getType()->isReferenceType() ||
          (F->getType().isConstQualified() && F->getType()->isScalarType())) {
        if (!Complained) {
          Diag(Record->getLocation(), diag::warn_no_constructor_for_refconst)
              << llvm::to_underlying(Record->getTagKind()) << Record;
          Complained = true;
        }

        Diag(F->getLocation(), diag::note_refconst_member_not_initialized)
          << F->getType()->isReferenceType()
          << F->getDeclName();
      }
    }
  }

  if (Record->getIdentifier()) {
    // C++ [class.mem]p13:
    //   If T is the name of a class, then each of the following shall have a
    //   name different from T:
    //     - every member of every anonymous union that is a member of class T.
    //
    // C++ [class.mem]p14:
    //   In addition, if class T has a user-declared constructor (12.1), every
    //   non-static data member of class T shall have a name different from T.
    DeclContext::lookup_result R = Record->lookup(Record->getDeclName());
    for (DeclContext::lookup_iterator I = R.begin(), E = R.end(); I != E;
         ++I) {
      NamedDecl *D = (*I)->getUnderlyingDecl();
      if (((isa<FieldDecl>(D) || isa<UnresolvedUsingValueDecl>(D)) &&
           Record->hasUserDeclaredConstructor()) ||
          isa<IndirectFieldDecl>(D)) {
        Diag((*I)->getLocation(), diag::err_member_name_of_class)
          << D->getDeclName();
        break;
      }
    }
  }

  // Warn if the class has virtual methods but non-virtual public destructor.
  if (Record->isPolymorphic() && !Record->isDependentType()) {
    CXXDestructorDecl *dtor = Record->getDestructor();
    if ((!dtor || (!dtor->isVirtual() && dtor->getAccess() == AS_public)) &&
        !Record->hasAttr<FinalAttr>())
      Diag(dtor ? dtor->getLocation() : Record->getLocation(),
           diag::warn_non_virtual_dtor) << Context.getRecordType(Record);
  }

  if (Record->isAbstract()) {
    if (FinalAttr *FA = Record->getAttr<FinalAttr>()) {
      Diag(Record->getLocation(), diag::warn_abstract_final_class)
        << FA->isSpelledAsSealed();
      DiagnoseAbstractType(Record);
    }
  }

  // Warn if the class has a final destructor but is not itself marked final.
  if (!Record->hasAttr<FinalAttr>()) {
    if (const CXXDestructorDecl *dtor = Record->getDestructor()) {
      if (const FinalAttr *FA = dtor->getAttr<FinalAttr>()) {
        Diag(FA->getLocation(), diag::warn_final_dtor_non_final_class)
            << FA->isSpelledAsSealed()
            << FixItHint::CreateInsertion(
                   getLocForEndOfToken(Record->getLocation()),
                   (FA->isSpelledAsSealed() ? " sealed" : " final"));
        Diag(Record->getLocation(),
             diag::note_final_dtor_non_final_class_silence)
            << Context.getRecordType(Record) << FA->isSpelledAsSealed();
      }
    }
  }

  // See if trivial_abi has to be dropped.
  if (Record->hasAttr<TrivialABIAttr>())
    checkIllFormedTrivialABIStruct(*Record);

  // Set HasTrivialSpecialMemberForCall if the record has attribute
  // "trivial_abi".
  bool HasTrivialABI = Record->hasAttr<TrivialABIAttr>();

  if (HasTrivialABI)
    Record->setHasTrivialSpecialMemberForCall();

  // Explicitly-defaulted secondary comparison functions (!=, <, <=, >, >=).
  // We check these last because they can depend on the properties of the
  // primary comparison functions (==, <=>).
  llvm::SmallVector<FunctionDecl*, 5> DefaultedSecondaryComparisons;

  // Perform checks that can't be done until we know all the properties of a
  // member function (whether it's defaulted, deleted, virtual, overriding,
  // ...).
  auto CheckCompletedMemberFunction = [&](CXXMethodDecl *MD) {
    // A static function cannot override anything.
    if (MD->getStorageClass() == SC_Static) {
      if (ReportOverrides(*this, diag::err_static_overrides_virtual, MD,
                          [](const CXXMethodDecl *) { return true; }))
        return;
    }

    // A deleted function cannot override a non-deleted function and vice
    // versa.
    if (ReportOverrides(*this,
                        MD->isDeleted() ? diag::err_deleted_override
                                        : diag::err_non_deleted_override,
                        MD, [&](const CXXMethodDecl *V) {
                          return MD->isDeleted() != V->isDeleted();
                        })) {
      if (MD->isDefaulted() && MD->isDeleted())
        // Explain why this defaulted function was deleted.
        DiagnoseDeletedDefaultedFunction(MD);
      return;
    }

    // A consteval function cannot override a non-consteval function and vice
    // versa.
    if (ReportOverrides(*this,
                        MD->isConsteval() ? diag::err_consteval_override
                                          : diag::err_non_consteval_override,
                        MD, [&](const CXXMethodDecl *V) {
                          return MD->isConsteval() != V->isConsteval();
                        })) {
      if (MD->isDefaulted() && MD->isDeleted())
        // Explain why this defaulted function was deleted.
        DiagnoseDeletedDefaultedFunction(MD);
      return;
    }
  };

  auto CheckForDefaultedFunction = [&](FunctionDecl *FD) -> bool {
    if (!FD || FD->isInvalidDecl() || !FD->isExplicitlyDefaulted())
      return false;

    DefaultedFunctionKind DFK = getDefaultedFunctionKind(FD);
    if (DFK.asComparison() == DefaultedComparisonKind::NotEqual ||
        DFK.asComparison() == DefaultedComparisonKind::Relational) {
      DefaultedSecondaryComparisons.push_back(FD);
      return true;
    }

    CheckExplicitlyDefaultedFunction(S, FD);
    return false;
  };

  if (!Record->isInvalidDecl() &&
      Record->hasAttr<VTablePointerAuthenticationAttr>())
    checkIncorrectVTablePointerAuthenticationAttribute(*Record);

  auto CompleteMemberFunction = [&](CXXMethodDecl *M) {
    // Check whether the explicitly-defaulted members are valid.
    bool Incomplete = CheckForDefaultedFunction(M);

    // Skip the rest of the checks for a member of a dependent class.
    if (Record->isDependentType())
      return;

    // For an explicitly defaulted or deleted special member, we defer
    // determining triviality until the class is complete. That time is now!
    CXXSpecialMemberKind CSM = getSpecialMember(M);
    if (!M->isImplicit() && !M->isUserProvided()) {
      if (CSM != CXXSpecialMemberKind::Invalid) {
        M->setTrivial(SpecialMemberIsTrivial(M, CSM));
        // Inform the class that we've finished declaring this member.
        Record->finishedDefaultedOrDeletedMember(M);
        M->setTrivialForCall(
            HasTrivialABI ||
            SpecialMemberIsTrivial(M, CSM, TAH_ConsiderTrivialABI));
        Record->setTrivialForCallFlags(M);
      }
    }

    // Set triviality for the purpose of calls if this is a user-provided
    // copy/move constructor or destructor.
    if ((CSM == CXXSpecialMemberKind::CopyConstructor ||
         CSM == CXXSpecialMemberKind::MoveConstructor ||
         CSM == CXXSpecialMemberKind::Destructor) &&
        M->isUserProvided()) {
      M->setTrivialForCall(HasTrivialABI);
      Record->setTrivialForCallFlags(M);
    }

    if (!M->isInvalidDecl() && M->isExplicitlyDefaulted() &&
        M->hasAttr<DLLExportAttr>()) {
      if (getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015) &&
          M->isTrivial() &&
          (CSM == CXXSpecialMemberKind::DefaultConstructor ||
           CSM == CXXSpecialMemberKind::CopyConstructor ||
           CSM == CXXSpecialMemberKind::Destructor))
        M->dropAttr<DLLExportAttr>();

      if (M->hasAttr<DLLExportAttr>()) {
        // Define after any fields with in-class initializers have been parsed.
        DelayedDllExportMemberFunctions.push_back(M);
      }
    }

    bool EffectivelyConstexprDestructor = true;
    // Avoid triggering vtable instantiation due to a dtor that is not
    // "effectively constexpr" for better compatibility.
    // See https://github.com/llvm/llvm-project/issues/102293 for more info.
    if (isa<CXXDestructorDecl>(M)) {
      auto Check = [](QualType T, auto &&Check) -> bool {
        const CXXRecordDecl *RD =
            T->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
        if (!RD || !RD->isCompleteDefinition())
          return true;

        if (!RD->hasConstexprDestructor())
          return false;

        QualType CanUnqualT = T.getCanonicalType().getUnqualifiedType();
        for (const CXXBaseSpecifier &B : RD->bases())
          if (B.getType().getCanonicalType().getUnqualifiedType() !=
                  CanUnqualT &&
              !Check(B.getType(), Check))
            return false;
        for (const FieldDecl *FD : RD->fields())
          if (FD->getType().getCanonicalType().getUnqualifiedType() !=
                  CanUnqualT &&
              !Check(FD->getType(), Check))
            return false;
        return true;
      };
      EffectivelyConstexprDestructor =
          Check(QualType(Record->getTypeForDecl(), 0), Check);
    }

    // Define defaulted constexpr virtual functions that override a base class
    // function right away.
    // FIXME: We can defer doing this until the vtable is marked as used.
    if (CSM != CXXSpecialMemberKind::Invalid && !M->isDeleted() &&
        M->isDefaulted() && M->isConstexpr() && M->size_overridden_methods() &&
        EffectivelyConstexprDestructor)
      DefineDefaultedFunction(*this, M, M->getLocation());

    if (!Incomplete)
      CheckCompletedMemberFunction(M);
  };

  // Check the destructor before any other member function. We need to
  // determine whether it's trivial in order to determine whether the claas
  // type is a literal type, which is a prerequisite for determining whether
  // other special member functions are valid and whether they're implicitly
  // 'constexpr'.
  if (CXXDestructorDecl *Dtor = Record->getDestructor())
    CompleteMemberFunction(Dtor);

  bool HasMethodWithOverrideControl = false,
       HasOverridingMethodWithoutOverrideControl = false;
  for (auto *D : Record->decls()) {
    if (auto *M = dyn_cast<CXXMethodDecl>(D)) {
      // FIXME: We could do this check for dependent types with non-dependent
      // bases.
      if (!Record->isDependentType()) {
        // See if a method overloads virtual methods in a base
        // class without overriding any.
        if (!M->isStatic())
          DiagnoseHiddenVirtualMethods(M);
        if (M->hasAttr<OverrideAttr>())
          HasMethodWithOverrideControl = true;
        else if (M->size_overridden_methods() > 0)
          HasOverridingMethodWithoutOverrideControl = true;
      }

      if (!isa<CXXDestructorDecl>(M))
        CompleteMemberFunction(M);
    } else if (auto *F = dyn_cast<FriendDecl>(D)) {
      CheckForDefaultedFunction(
          dyn_cast_or_null<FunctionDecl>(F->getFriendDecl()));
    }
  }

  if (HasOverridingMethodWithoutOverrideControl) {
    bool HasInconsistentOverrideControl = HasMethodWithOverrideControl;
    for (auto *M : Record->methods())
      DiagnoseAbsenceOfOverrideControl(M, HasInconsistentOverrideControl);
  }

  // Check the defaulted secondary comparisons after any other member functions.
  for (FunctionDecl *FD : DefaultedSecondaryComparisons) {
    CheckExplicitlyDefaultedFunction(S, FD);

    // If this is a member function, we deferred checking it until now.
    if (auto *MD = dyn_cast<CXXMethodDecl>(FD))
      CheckCompletedMemberFunction(MD);
  }

  // ms_struct is a request to use the same ABI rules as MSVC.  Check
  // whether this class uses any C++ features that are implemented
  // completely differently in MSVC, and if so, emit a diagnostic.
  // That diagnostic defaults to an error, but we allow projects to
  // map it down to a warning (or ignore it).  It's a fairly common
  // practice among users of the ms_struct pragma to mass-annotate
  // headers, sweeping up a bunch of types that the project doesn't
  // really rely on MSVC-compatible layout for.  We must therefore
  // support "ms_struct except for C++ stuff" as a secondary ABI.
  // Don't emit this diagnostic if the feature was enabled as a
  // language option (as opposed to via a pragma or attribute), as
  // the option -mms-bitfields otherwise essentially makes it impossible
  // to build C++ code, unless this diagnostic is turned off.
  if (Record->isMsStruct(Context) && !Context.getLangOpts().MSBitfields &&
      (Record->isPolymorphic() || Record->getNumBases())) {
    Diag(Record->getLocation(), diag::warn_cxx_ms_struct);
  }

  checkClassLevelDLLAttribute(Record);
  checkClassLevelCodeSegAttribute(Record);

  bool ClangABICompat4 =
      Context.getLangOpts().getClangABICompat() <= LangOptions::ClangABI::Ver4;
  TargetInfo::CallingConvKind CCK =
      Context.getTargetInfo().getCallingConvKind(ClangABICompat4);
  bool CanPass = canPassInRegisters(*this, Record, CCK);

  // Do not change ArgPassingRestrictions if it has already been set to
  // RecordArgPassingKind::CanNeverPassInRegs.
  if (Record->getArgPassingRestrictions() !=
      RecordArgPassingKind::CanNeverPassInRegs)
    Record->setArgPassingRestrictions(
        CanPass ? RecordArgPassingKind::CanPassInRegs
                : RecordArgPassingKind::CannotPassInRegs);

  // If canPassInRegisters returns true despite the record having a non-trivial
  // destructor, the record is destructed in the callee. This happens only when
  // the record or one of its subobjects has a field annotated with trivial_abi
  // or a field qualified with ObjC __strong/__weak.
  if (Context.getTargetInfo().getCXXABI().areArgsDestroyedLeftToRightInCallee())
    Record->setParamDestroyedInCallee(true);
  else if (Record->hasNonTrivialDestructor())
    Record->setParamDestroyedInCallee(CanPass);

  if (getLangOpts().ForceEmitVTables) {
    // If we want to emit all the vtables, we need to mark it as used.  This
    // is especially required for cases like vtable assumption loads.
    MarkVTableUsed(Record->getInnerLocStart(), Record);
  }

  if (getLangOpts().CUDA) {
    if (Record->hasAttr<CUDADeviceBuiltinSurfaceTypeAttr>())
      checkCUDADeviceBuiltinSurfaceClassTemplate(*this, Record);
    else if (Record->hasAttr<CUDADeviceBuiltinTextureTypeAttr>())
      checkCUDADeviceBuiltinTextureClassTemplate(*this, Record);
  }
}

/// Look up the special member function that would be called by a special
/// member function for a subobject of class type.
///
/// \param Class The class type of the subobject.
/// \param CSM The kind of special member function.
/// \param FieldQuals If the subobject is a field, its cv-qualifiers.
/// \param ConstRHS True if this is a copy operation with a const object
///        on its RHS, that is, if the argument to the outer special member
///        function is 'const' and this is not a field marked 'mutable'.
static Sema::SpecialMemberOverloadResult
lookupCallFromSpecialMember(Sema &S, CXXRecordDecl *Class,
                            CXXSpecialMemberKind CSM, unsigned FieldQuals,
                            bool ConstRHS) {
  unsigned LHSQuals = 0;
  if (CSM == CXXSpecialMemberKind::CopyAssignment ||
      CSM == CXXSpecialMemberKind::MoveAssignment)
    LHSQuals = FieldQuals;

  unsigned RHSQuals = FieldQuals;
  if (CSM == CXXSpecialMemberKind::DefaultConstructor ||
      CSM == CXXSpecialMemberKind::Destructor)
    RHSQuals = 0;
  else if (ConstRHS)
    RHSQuals |= Qualifiers::Const;

  return S.LookupSpecialMember(Class, CSM,
                               RHSQuals & Qualifiers::Const,
                               RHSQuals & Qualifiers::Volatile,
                               false,
                               LHSQuals & Qualifiers::Const,
                               LHSQuals & Qualifiers::Volatile);
}

class Sema::InheritedConstructorInfo {
  Sema &S;
  SourceLocation UseLoc;

  /// A mapping from the base classes through which the constructor was
  /// inherited to the using shadow declaration in that base class (or a null
  /// pointer if the constructor was declared in that base class).
  llvm::DenseMap<CXXRecordDecl *, ConstructorUsingShadowDecl *>
      InheritedFromBases;

public:
  InheritedConstructorInfo(Sema &S, SourceLocation UseLoc,
                           ConstructorUsingShadowDecl *Shadow)
      : S(S), UseLoc(UseLoc) {
    bool DiagnosedMultipleConstructedBases = false;
    CXXRecordDecl *ConstructedBase = nullptr;
    BaseUsingDecl *ConstructedBaseIntroducer = nullptr;

    // Find the set of such base class subobjects and check that there's a
    // unique constructed subobject.
    for (auto *D : Shadow->redecls()) {
      auto *DShadow = cast<ConstructorUsingShadowDecl>(D);
      auto *DNominatedBase = DShadow->getNominatedBaseClass();
      auto *DConstructedBase = DShadow->getConstructedBaseClass();

      InheritedFromBases.insert(
          std::make_pair(DNominatedBase->getCanonicalDecl(),
                         DShadow->getNominatedBaseClassShadowDecl()));
      if (DShadow->constructsVirtualBase())
        InheritedFromBases.insert(
            std::make_pair(DConstructedBase->getCanonicalDecl(),
                           DShadow->getConstructedBaseClassShadowDecl()));
      else
        assert(DNominatedBase == DConstructedBase);

      // [class.inhctor.init]p2:
      //   If the constructor was inherited from multiple base class subobjects
      //   of type B, the program is ill-formed.
      if (!ConstructedBase) {
        ConstructedBase = DConstructedBase;
        ConstructedBaseIntroducer = D->getIntroducer();
      } else if (ConstructedBase != DConstructedBase &&
                 !Shadow->isInvalidDecl()) {
        if (!DiagnosedMultipleConstructedBases) {
          S.Diag(UseLoc, diag::err_ambiguous_inherited_constructor)
              << Shadow->getTargetDecl();
          S.Diag(ConstructedBaseIntroducer->getLocation(),
                 diag::note_ambiguous_inherited_constructor_using)
              << ConstructedBase;
          DiagnosedMultipleConstructedBases = true;
        }
        S.Diag(D->getIntroducer()->getLocation(),
               diag::note_ambiguous_inherited_constructor_using)
            << DConstructedBase;
      }
    }

    if (DiagnosedMultipleConstructedBases)
      Shadow->setInvalidDecl();
  }

  /// Find the constructor to use for inherited construction of a base class,
  /// and whether that base class constructor inherits the constructor from a
  /// virtual base class (in which case it won't actually invoke it).
  std::pair<CXXConstructorDecl *, bool>
  findConstructorForBase(CXXRecordDecl *Base, CXXConstructorDecl *Ctor) const {
    auto It = InheritedFromBases.find(Base->getCanonicalDecl());
    if (It == InheritedFromBases.end())
      return std::make_pair(nullptr, false);

    // This is an intermediary class.
    if (It->second)
      return std::make_pair(
          S.findInheritingConstructor(UseLoc, Ctor, It->second),
          It->second->constructsVirtualBase());

    // This is the base class from which the constructor was inherited.
    return std::make_pair(Ctor, false);
  }
};

/// Is the special member function which would be selected to perform the
/// specified operation on the specified class type a constexpr constructor?
static bool specialMemberIsConstexpr(
    Sema &S, CXXRecordDecl *ClassDecl, CXXSpecialMemberKind CSM, unsigned Quals,
    bool ConstRHS, CXXConstructorDecl *InheritedCtor = nullptr,
    Sema::InheritedConstructorInfo *Inherited = nullptr) {
  // Suppress duplicate constraint checking here, in case a constraint check
  // caused us to decide to do this.  Any truely recursive checks will get
  // caught during these checks anyway.
  Sema::SatisfactionStackResetRAII SSRAII{S};

  // If we're inheriting a constructor, see if we need to call it for this base
  // class.
  if (InheritedCtor) {
    assert(CSM == CXXSpecialMemberKind::DefaultConstructor);
    auto BaseCtor =
        Inherited->findConstructorForBase(ClassDecl, InheritedCtor).first;
    if (BaseCtor)
      return BaseCtor->isConstexpr();
  }

  if (CSM == CXXSpecialMemberKind::DefaultConstructor)
    return ClassDecl->hasConstexprDefaultConstructor();
  if (CSM == CXXSpecialMemberKind::Destructor)
    return ClassDecl->hasConstexprDestructor();

  Sema::SpecialMemberOverloadResult SMOR =
      lookupCallFromSpecialMember(S, ClassDecl, CSM, Quals, ConstRHS);
  if (!SMOR.getMethod())
    // A constructor we wouldn't select can't be "involved in initializing"
    // anything.
    return true;
  return SMOR.getMethod()->isConstexpr();
}

/// Determine whether the specified special member function would be constexpr
/// if it were implicitly defined.
static bool defaultedSpecialMemberIsConstexpr(
    Sema &S, CXXRecordDecl *ClassDecl, CXXSpecialMemberKind CSM, bool ConstArg,
    CXXConstructorDecl *InheritedCtor = nullptr,
    Sema::InheritedConstructorInfo *Inherited = nullptr) {
  if (!S.getLangOpts().CPlusPlus11)
    return false;

  // C++11 [dcl.constexpr]p4:
  // In the definition of a constexpr constructor [...]
  bool Ctor = true;
  switch (CSM) {
  case CXXSpecialMemberKind::DefaultConstructor:
    if (Inherited)
      break;
    // Since default constructor lookup is essentially trivial (and cannot
    // involve, for instance, template instantiation), we compute whether a
    // defaulted default constructor is constexpr directly within CXXRecordDecl.
    //
    // This is important for performance; we need to know whether the default
    // constructor is constexpr to determine whether the type is a literal type.
    return ClassDecl->defaultedDefaultConstructorIsConstexpr();

  case CXXSpecialMemberKind::CopyConstructor:
  case CXXSpecialMemberKind::MoveConstructor:
    // For copy or move constructors, we need to perform overload resolution.
    break;

  case CXXSpecialMemberKind::CopyAssignment:
  case CXXSpecialMemberKind::MoveAssignment:
    if (!S.getLangOpts().CPlusPlus14)
      return false;
    // In C++1y, we need to perform overload resolution.
    Ctor = false;
    break;

  case CXXSpecialMemberKind::Destructor:
    return ClassDecl->defaultedDestructorIsConstexpr();

  case CXXSpecialMemberKind::Invalid:
    return false;
  }

  //   -- if the class is a non-empty union, or for each non-empty anonymous
  //      union member of a non-union class, exactly one non-static data member
  //      shall be initialized; [DR1359]
  //
  // If we squint, this is guaranteed, since exactly one non-static data member
  // will be initialized (if the constructor isn't deleted), we just don't know
  // which one.
  if (Ctor && ClassDecl->isUnion())
    return CSM == CXXSpecialMemberKind::DefaultConstructor
               ? ClassDecl->hasInClassInitializer() ||
                     !ClassDecl->hasVariantMembers()
               : true;

  //   -- the class shall not have any virtual base classes;
  if (Ctor && ClassDecl->getNumVBases())
    return false;

  // C++1y [class.copy]p26:
  //   -- [the class] is a literal type, and
  if (!Ctor && !ClassDecl->isLiteral() && !S.getLangOpts().CPlusPlus23)
    return false;

  //   -- every constructor involved in initializing [...] base class
  //      sub-objects shall be a constexpr constructor;
  //   -- the assignment operator selected to copy/move each direct base
  //      class is a constexpr function, and
  if (!S.getLangOpts().CPlusPlus23) {
    for (const auto &B : ClassDecl->bases()) {
      const RecordType *BaseType = B.getType()->getAs<RecordType>();
      if (!BaseType)
        continue;
      CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      if (!specialMemberIsConstexpr(S, BaseClassDecl, CSM, 0, ConstArg,
                                    InheritedCtor, Inherited))
        return false;
    }
  }

  //   -- every constructor involved in initializing non-static data members
  //      [...] shall be a constexpr constructor;
  //   -- every non-static data member and base class sub-object shall be
  //      initialized
  //   -- for each non-static data member of X that is of class type (or array
  //      thereof), the assignment operator selected to copy/move that member is
  //      a constexpr function
  if (!S.getLangOpts().CPlusPlus23) {
    for (const auto *F : ClassDecl->fields()) {
      if (F->isInvalidDecl())
        continue;
      if (CSM == CXXSpecialMemberKind::DefaultConstructor &&
          F->hasInClassInitializer())
        continue;
      QualType BaseType = S.Context.getBaseElementType(F->getType());
      if (const RecordType *RecordTy = BaseType->getAs<RecordType>()) {
        CXXRecordDecl *FieldRecDecl = cast<CXXRecordDecl>(RecordTy->getDecl());
        if (!specialMemberIsConstexpr(S, FieldRecDecl, CSM,
                                      BaseType.getCVRQualifiers(),
                                      ConstArg && !F->isMutable()))
          return false;
      } else if (CSM == CXXSpecialMemberKind::DefaultConstructor) {
        return false;
      }
    }
  }

  // All OK, it's constexpr!
  return true;
}

namespace {
/// RAII object to register a defaulted function as having its exception
/// specification computed.
struct ComputingExceptionSpec {
  Sema &S;

  ComputingExceptionSpec(Sema &S, FunctionDecl *FD, SourceLocation Loc)
      : S(S) {
    Sema::CodeSynthesisContext Ctx;
    Ctx.Kind = Sema::CodeSynthesisContext::ExceptionSpecEvaluation;
    Ctx.PointOfInstantiation = Loc;
    Ctx.Entity = FD;
    S.pushCodeSynthesisContext(Ctx);
  }
  ~ComputingExceptionSpec() {
    S.popCodeSynthesisContext();
  }
};
}

static Sema::ImplicitExceptionSpecification
ComputeDefaultedSpecialMemberExceptionSpec(Sema &S, SourceLocation Loc,
                                           CXXMethodDecl *MD,
                                           CXXSpecialMemberKind CSM,
                                           Sema::InheritedConstructorInfo *ICI);

static Sema::ImplicitExceptionSpecification
ComputeDefaultedComparisonExceptionSpec(Sema &S, SourceLocation Loc,
                                        FunctionDecl *FD,
                                        Sema::DefaultedComparisonKind DCK);

static Sema::ImplicitExceptionSpecification
computeImplicitExceptionSpec(Sema &S, SourceLocation Loc, FunctionDecl *FD) {
  auto DFK = S.getDefaultedFunctionKind(FD);
  if (DFK.isSpecialMember())
    return ComputeDefaultedSpecialMemberExceptionSpec(
        S, Loc, cast<CXXMethodDecl>(FD), DFK.asSpecialMember(), nullptr);
  if (DFK.isComparison())
    return ComputeDefaultedComparisonExceptionSpec(S, Loc, FD,
                                                   DFK.asComparison());

  auto *CD = cast<CXXConstructorDecl>(FD);
  assert(CD->getInheritedConstructor() &&
         "only defaulted functions and inherited constructors have implicit "
         "exception specs");
  Sema::InheritedConstructorInfo ICI(
      S, Loc, CD->getInheritedConstructor().getShadowDecl());
  return ComputeDefaultedSpecialMemberExceptionSpec(
      S, Loc, CD, CXXSpecialMemberKind::DefaultConstructor, &ICI);
}

static FunctionProtoType::ExtProtoInfo getImplicitMethodEPI(Sema &S,
                                                            CXXMethodDecl *MD) {
  FunctionProtoType::ExtProtoInfo EPI;

  // Build an exception specification pointing back at this member.
  EPI.ExceptionSpec.Type = EST_Unevaluated;
  EPI.ExceptionSpec.SourceDecl = MD;

  // Set the calling convention to the default for C++ instance methods.
  EPI.ExtInfo = EPI.ExtInfo.withCallingConv(
      S.Context.getDefaultCallingConvention(/*IsVariadic=*/false,
                                            /*IsCXXMethod=*/true));
  return EPI;
}

void Sema::EvaluateImplicitExceptionSpec(SourceLocation Loc, FunctionDecl *FD) {
  const FunctionProtoType *FPT = FD->getType()->castAs<FunctionProtoType>();
  if (FPT->getExceptionSpecType() != EST_Unevaluated)
    return;

  // Evaluate the exception specification.
  auto IES = computeImplicitExceptionSpec(*this, Loc, FD);
  auto ESI = IES.getExceptionSpec();

  // Update the type of the special member to use it.
  UpdateExceptionSpec(FD, ESI);
}

void Sema::CheckExplicitlyDefaultedFunction(Scope *S, FunctionDecl *FD) {
  assert(FD->isExplicitlyDefaulted() && "not explicitly-defaulted");

  DefaultedFunctionKind DefKind = getDefaultedFunctionKind(FD);
  if (!DefKind) {
    assert(FD->getDeclContext()->isDependentContext());
    return;
  }

  if (DefKind.isComparison())
    UnusedPrivateFields.clear();

  if (DefKind.isSpecialMember()
          ? CheckExplicitlyDefaultedSpecialMember(cast<CXXMethodDecl>(FD),
                                                  DefKind.asSpecialMember(),
                                                  FD->getDefaultLoc())
          : CheckExplicitlyDefaultedComparison(S, FD, DefKind.asComparison()))
    FD->setInvalidDecl();
}

bool Sema::CheckExplicitlyDefaultedSpecialMember(CXXMethodDecl *MD,
                                                 CXXSpecialMemberKind CSM,
                                                 SourceLocation DefaultLoc) {
  CXXRecordDecl *RD = MD->getParent();

  assert(MD->isExplicitlyDefaulted() && CSM != CXXSpecialMemberKind::Invalid &&
         "not an explicitly-defaulted special member");

  // Defer all checking for special members of a dependent type.
  if (RD->isDependentType())
    return false;

  // Whether this was the first-declared instance of the constructor.
  // This affects whether we implicitly add an exception spec and constexpr.
  bool First = MD == MD->getCanonicalDecl();

  bool HadError = false;

  // C++11 [dcl.fct.def.default]p1:
  //   A function that is explicitly defaulted shall
  //     -- be a special member function [...] (checked elsewhere),
  //     -- have the same type (except for ref-qualifiers, and except that a
  //        copy operation can take a non-const reference) as an implicit
  //        declaration, and
  //     -- not have default arguments.
  // C++2a changes the second bullet to instead delete the function if it's
  // defaulted on its first declaration, unless it's "an assignment operator,
  // and its return type differs or its parameter type is not a reference".
  bool DeleteOnTypeMismatch = getLangOpts().CPlusPlus20 && First;
  bool ShouldDeleteForTypeMismatch = false;
  unsigned ExpectedParams = 1;
  if (CSM == CXXSpecialMemberKind::DefaultConstructor ||
      CSM == CXXSpecialMemberKind::Destructor)
    ExpectedParams = 0;
  if (MD->getNumExplicitParams() != ExpectedParams) {
    // This checks for default arguments: a copy or move constructor with a
    // default argument is classified as a default constructor, and assignment
    // operations and destructors can't have default arguments.
    Diag(MD->getLocation(), diag::err_defaulted_special_member_params)
        << llvm::to_underlying(CSM) << MD->getSourceRange();
    HadError = true;
  } else if (MD->isVariadic()) {
    if (DeleteOnTypeMismatch)
      ShouldDeleteForTypeMismatch = true;
    else {
      Diag(MD->getLocation(), diag::err_defaulted_special_member_variadic)
          << llvm::to_underlying(CSM) << MD->getSourceRange();
      HadError = true;
    }
  }

  const FunctionProtoType *Type = MD->getType()->castAs<FunctionProtoType>();

  bool CanHaveConstParam = false;
  if (CSM == CXXSpecialMemberKind::CopyConstructor)
    CanHaveConstParam = RD->implicitCopyConstructorHasConstParam();
  else if (CSM == CXXSpecialMemberKind::CopyAssignment)
    CanHaveConstParam = RD->implicitCopyAssignmentHasConstParam();

  QualType ReturnType = Context.VoidTy;
  if (CSM == CXXSpecialMemberKind::CopyAssignment ||
      CSM == CXXSpecialMemberKind::MoveAssignment) {
    // Check for return type matching.
    ReturnType = Type->getReturnType();
    QualType ThisType = MD->getFunctionObjectParameterType();

    QualType DeclType = Context.getTypeDeclType(RD);
    DeclType = Context.getElaboratedType(ElaboratedTypeKeyword::None, nullptr,
                                         DeclType, nullptr);
    DeclType = Context.getAddrSpaceQualType(
        DeclType, ThisType.getQualifiers().getAddressSpace());
    QualType ExpectedReturnType = Context.getLValueReferenceType(DeclType);

    if (!Context.hasSameType(ReturnType, ExpectedReturnType)) {
      Diag(MD->getLocation(), diag::err_defaulted_special_member_return_type)
          << (CSM == CXXSpecialMemberKind::MoveAssignment)
          << ExpectedReturnType;
      HadError = true;
    }

    // A defaulted special member cannot have cv-qualifiers.
    if (ThisType.isConstQualified() || ThisType.isVolatileQualified()) {
      if (DeleteOnTypeMismatch)
        ShouldDeleteForTypeMismatch = true;
      else {
        Diag(MD->getLocation(), diag::err_defaulted_special_member_quals)
            << (CSM == CXXSpecialMemberKind::MoveAssignment)
            << getLangOpts().CPlusPlus14;
        HadError = true;
      }
    }
    // [C++23][dcl.fct.def.default]/p2.2
    // if F2 has an implicit object parameter of type “reference to C”,
    // F1 may be an explicit object member function whose explicit object
    // parameter is of (possibly different) type “reference to C”,
    // in which case the type of F1 would differ from the type of F2
    // in that the type of F1 has an additional parameter;
    if (!Context.hasSameType(
            ThisType.getNonReferenceType().getUnqualifiedType(),
            Context.getRecordType(RD))) {
      if (DeleteOnTypeMismatch)
        ShouldDeleteForTypeMismatch = true;
      else {
        Diag(MD->getLocation(),
             diag::err_defaulted_special_member_explicit_object_mismatch)
            << (CSM == CXXSpecialMemberKind::MoveAssignment) << RD
            << MD->getSourceRange();
        HadError = true;
      }
    }
  }

  // Check for parameter type matching.
  QualType ArgType =
      ExpectedParams
          ? Type->getParamType(MD->isExplicitObjectMemberFunction() ? 1 : 0)
          : QualType();
  bool HasConstParam = false;
  if (ExpectedParams && ArgType->isReferenceType()) {
    // Argument must be reference to possibly-const T.
    QualType ReferentType = ArgType->getPointeeType();
    HasConstParam = ReferentType.isConstQualified();

    if (ReferentType.isVolatileQualified()) {
      if (DeleteOnTypeMismatch)
        ShouldDeleteForTypeMismatch = true;
      else {
        Diag(MD->getLocation(),
             diag::err_defaulted_special_member_volatile_param)
            << llvm::to_underlying(CSM);
        HadError = true;
      }
    }

    if (HasConstParam && !CanHaveConstParam) {
      if (DeleteOnTypeMismatch)
        ShouldDeleteForTypeMismatch = true;
      else if (CSM == CXXSpecialMemberKind::CopyConstructor ||
               CSM == CXXSpecialMemberKind::CopyAssignment) {
        Diag(MD->getLocation(),
             diag::err_defaulted_special_member_copy_const_param)
            << (CSM == CXXSpecialMemberKind::CopyAssignment);
        // FIXME: Explain why this special member can't be const.
        HadError = true;
      } else {
        Diag(MD->getLocation(),
             diag::err_defaulted_special_member_move_const_param)
            << (CSM == CXXSpecialMemberKind::MoveAssignment);
        HadError = true;
      }
    }
  } else if (ExpectedParams) {
    // A copy assignment operator can take its argument by value, but a
    // defaulted one cannot.
    assert(CSM == CXXSpecialMemberKind::CopyAssignment &&
           "unexpected non-ref argument");
    Diag(MD->getLocation(), diag::err_defaulted_copy_assign_not_ref);
    HadError = true;
  }

  // C++11 [dcl.fct.def.default]p2:
  //   An explicitly-defaulted function may be declared constexpr only if it
  //   would have been implicitly declared as constexpr,
  // Do not apply this rule to members of class templates, since core issue 1358
  // makes such functions always instantiate to constexpr functions. For
  // functions which cannot be constexpr (for non-constructors in C++11 and for
  // destructors in C++14 and C++17), this is checked elsewhere.
  //
  // FIXME: This should not apply if the member is deleted.
  bool Constexpr = defaultedSpecialMemberIsConstexpr(*this, RD, CSM,
                                                     HasConstParam);

  // C++14 [dcl.constexpr]p6 (CWG DR647/CWG DR1358):
  //   If the instantiated template specialization of a constexpr function
  //   template or member function of a class template would fail to satisfy
  //   the requirements for a constexpr function or constexpr constructor, that
  //   specialization is still a constexpr function or constexpr constructor,
  //   even though a call to such a function cannot appear in a constant
  //   expression.
  if (MD->isTemplateInstantiation() && MD->isConstexpr())
    Constexpr = true;

  if ((getLangOpts().CPlusPlus20 ||
       (getLangOpts().CPlusPlus14 ? !isa<CXXDestructorDecl>(MD)
                                  : isa<CXXConstructorDecl>(MD))) &&
      MD->isConstexpr() && !Constexpr &&
      MD->getTemplatedKind() == FunctionDecl::TK_NonTemplate) {
        if (!MD->isConsteval() && RD->getNumVBases()) {
          Diag(MD->getBeginLoc(),
               diag::err_incorrect_defaulted_constexpr_with_vb)
              << llvm::to_underlying(CSM);
          for (const auto &I : RD->vbases())
            Diag(I.getBeginLoc(), diag::note_constexpr_virtual_base_here);
        } else {
          Diag(MD->getBeginLoc(), diag::err_incorrect_defaulted_constexpr)
              << llvm::to_underlying(CSM) << MD->isConsteval();
        }
        HadError = true;
        // FIXME: Explain why the special member can't be constexpr.
  }

  if (First) {
    // C++2a [dcl.fct.def.default]p3:
    //   If a function is explicitly defaulted on its first declaration, it is
    //   implicitly considered to be constexpr if the implicit declaration
    //   would be.
    MD->setConstexprKind(Constexpr ? (MD->isConsteval()
                                          ? ConstexprSpecKind::Consteval
                                          : ConstexprSpecKind::Constexpr)
                                   : ConstexprSpecKind::Unspecified);

    if (!Type->hasExceptionSpec()) {
      // C++2a [except.spec]p3:
      //   If a declaration of a function does not have a noexcept-specifier
      //   [and] is defaulted on its first declaration, [...] the exception
      //   specification is as specified below
      FunctionProtoType::ExtProtoInfo EPI = Type->getExtProtoInfo();
      EPI.ExceptionSpec.Type = EST_Unevaluated;
      EPI.ExceptionSpec.SourceDecl = MD;
      MD->setType(
          Context.getFunctionType(ReturnType, Type->getParamTypes(), EPI));
    }
  }

  if (ShouldDeleteForTypeMismatch || ShouldDeleteSpecialMember(MD, CSM)) {
    if (First) {
      SetDeclDeleted(MD, MD->getLocation());
      if (!inTemplateInstantiation() && !HadError) {
        Diag(MD->getLocation(), diag::warn_defaulted_method_deleted)
            << llvm::to_underlying(CSM);
        if (ShouldDeleteForTypeMismatch) {
          Diag(MD->getLocation(), diag::note_deleted_type_mismatch)
              << llvm::to_underlying(CSM);
        } else if (ShouldDeleteSpecialMember(MD, CSM, nullptr,
                                             /*Diagnose*/ true) &&
                   DefaultLoc.isValid()) {
          Diag(DefaultLoc, diag::note_replace_equals_default_to_delete)
              << FixItHint::CreateReplacement(DefaultLoc, "delete");
        }
      }
      if (ShouldDeleteForTypeMismatch && !HadError) {
        Diag(MD->getLocation(),
             diag::warn_cxx17_compat_defaulted_method_type_mismatch)
            << llvm::to_underlying(CSM);
      }
    } else {
      // C++11 [dcl.fct.def.default]p4:
      //   [For a] user-provided explicitly-defaulted function [...] if such a
      //   function is implicitly defined as deleted, the program is ill-formed.
      Diag(MD->getLocation(), diag::err_out_of_line_default_deletes)
          << llvm::to_underlying(CSM);
      assert(!ShouldDeleteForTypeMismatch && "deleted non-first decl");
      ShouldDeleteSpecialMember(MD, CSM, nullptr, /*Diagnose*/true);
      HadError = true;
    }
  }

  return HadError;
}

namespace {
/// Helper class for building and checking a defaulted comparison.
///
/// Defaulted functions are built in two phases:
///
///  * First, the set of operations that the function will perform are
///    identified, and some of them are checked. If any of the checked
///    operations is invalid in certain ways, the comparison function is
///    defined as deleted and no body is built.
///  * Then, if the function is not defined as deleted, the body is built.
///
/// This is accomplished by performing two visitation steps over the eventual
/// body of the function.
template<typename Derived, typename ResultList, typename Result,
         typename Subobject>
class DefaultedComparisonVisitor {
public:
  using DefaultedComparisonKind = Sema::DefaultedComparisonKind;

  DefaultedComparisonVisitor(Sema &S, CXXRecordDecl *RD, FunctionDecl *FD,
                             DefaultedComparisonKind DCK)
      : S(S), RD(RD), FD(FD), DCK(DCK) {
    if (auto *Info = FD->getDefalutedOrDeletedInfo()) {
      // FIXME: Change CreateOverloadedBinOp to take an ArrayRef instead of an
      // UnresolvedSet to avoid this copy.
      Fns.assign(Info->getUnqualifiedLookups().begin(),
                 Info->getUnqualifiedLookups().end());
    }
  }

  ResultList visit() {
    // The type of an lvalue naming a parameter of this function.
    QualType ParamLvalType =
        FD->getParamDecl(0)->getType().getNonReferenceType();

    ResultList Results;

    switch (DCK) {
    case DefaultedComparisonKind::None:
      llvm_unreachable("not a defaulted comparison");

    case DefaultedComparisonKind::Equal:
    case DefaultedComparisonKind::ThreeWay:
      getDerived().visitSubobjects(Results, RD, ParamLvalType.getQualifiers());
      return Results;

    case DefaultedComparisonKind::NotEqual:
    case DefaultedComparisonKind::Relational:
      Results.add(getDerived().visitExpandedSubobject(
          ParamLvalType, getDerived().getCompleteObject()));
      return Results;
    }
    llvm_unreachable("");
  }

protected:
  Derived &getDerived() { return static_cast<Derived&>(*this); }

  /// Visit the expanded list of subobjects of the given type, as specified in
  /// C++2a [class.compare.default].
  ///
  /// \return \c true if the ResultList object said we're done, \c false if not.
  bool visitSubobjects(ResultList &Results, CXXRecordDecl *Record,
                       Qualifiers Quals) {
    // C++2a [class.compare.default]p4:
    //   The direct base class subobjects of C
    for (CXXBaseSpecifier &Base : Record->bases())
      if (Results.add(getDerived().visitSubobject(
              S.Context.getQualifiedType(Base.getType(), Quals),
              getDerived().getBase(&Base))))
        return true;

    //   followed by the non-static data members of C
    for (FieldDecl *Field : Record->fields()) {
      // C++23 [class.bit]p2:
      //   Unnamed bit-fields are not members ...
      if (Field->isUnnamedBitField())
        continue;
      // Recursively expand anonymous structs.
      if (Field->isAnonymousStructOrUnion()) {
        if (visitSubobjects(Results, Field->getType()->getAsCXXRecordDecl(),
                            Quals))
          return true;
        continue;
      }

      // Figure out the type of an lvalue denoting this field.
      Qualifiers FieldQuals = Quals;
      if (Field->isMutable())
        FieldQuals.removeConst();
      QualType FieldType =
          S.Context.getQualifiedType(Field->getType(), FieldQuals);

      if (Results.add(getDerived().visitSubobject(
              FieldType, getDerived().getField(Field))))
        return true;
    }

    //   form a list of subobjects.
    return false;
  }

  Result visitSubobject(QualType Type, Subobject Subobj) {
    //   In that list, any subobject of array type is recursively expanded
    const ArrayType *AT = S.Context.getAsArrayType(Type);
    if (auto *CAT = dyn_cast_or_null<ConstantArrayType>(AT))
      return getDerived().visitSubobjectArray(CAT->getElementType(),
                                              CAT->getSize(), Subobj);
    return getDerived().visitExpandedSubobject(Type, Subobj);
  }

  Result visitSubobjectArray(QualType Type, const llvm::APInt &Size,
                             Subobject Subobj) {
    return getDerived().visitSubobject(Type, Subobj);
  }

protected:
  Sema &S;
  CXXRecordDecl *RD;
  FunctionDecl *FD;
  DefaultedComparisonKind DCK;
  UnresolvedSet<16> Fns;
};

/// Information about a defaulted comparison, as determined by
/// DefaultedComparisonAnalyzer.
struct DefaultedComparisonInfo {
  bool Deleted = false;
  bool Constexpr = true;
  ComparisonCategoryType Category = ComparisonCategoryType::StrongOrdering;

  static DefaultedComparisonInfo deleted() {
    DefaultedComparisonInfo Deleted;
    Deleted.Deleted = true;
    return Deleted;
  }

  bool add(const DefaultedComparisonInfo &R) {
    Deleted |= R.Deleted;
    Constexpr &= R.Constexpr;
    Category = commonComparisonType(Category, R.Category);
    return Deleted;
  }
};

/// An element in the expanded list of subobjects of a defaulted comparison, as
/// specified in C++2a [class.compare.default]p4.
struct DefaultedComparisonSubobject {
  enum { CompleteObject, Member, Base } Kind;
  NamedDecl *Decl;
  SourceLocation Loc;
};

/// A visitor over the notional body of a defaulted comparison that determines
/// whether that body would be deleted or constexpr.
class DefaultedComparisonAnalyzer
    : public DefaultedComparisonVisitor<DefaultedComparisonAnalyzer,
                                        DefaultedComparisonInfo,
                                        DefaultedComparisonInfo,
                                        DefaultedComparisonSubobject> {
public:
  enum DiagnosticKind { NoDiagnostics, ExplainDeleted, ExplainConstexpr };

private:
  DiagnosticKind Diagnose;

public:
  using Base = DefaultedComparisonVisitor;
  using Result = DefaultedComparisonInfo;
  using Subobject = DefaultedComparisonSubobject;

  friend Base;

  DefaultedComparisonAnalyzer(Sema &S, CXXRecordDecl *RD, FunctionDecl *FD,
                              DefaultedComparisonKind DCK,
                              DiagnosticKind Diagnose = NoDiagnostics)
      : Base(S, RD, FD, DCK), Diagnose(Diagnose) {}

  Result visit() {
    if ((DCK == DefaultedComparisonKind::Equal ||
         DCK == DefaultedComparisonKind::ThreeWay) &&
        RD->hasVariantMembers()) {
      // C++2a [class.compare.default]p2 [P2002R0]:
      //   A defaulted comparison operator function for class C is defined as
      //   deleted if [...] C has variant members.
      if (Diagnose == ExplainDeleted) {
        S.Diag(FD->getLocation(), diag::note_defaulted_comparison_union)
          << FD << RD->isUnion() << RD;
      }
      return Result::deleted();
    }

    return Base::visit();
  }

private:
  Subobject getCompleteObject() {
    return Subobject{Subobject::CompleteObject, RD, FD->getLocation()};
  }

  Subobject getBase(CXXBaseSpecifier *Base) {
    return Subobject{Subobject::Base, Base->getType()->getAsCXXRecordDecl(),
                     Base->getBaseTypeLoc()};
  }

  Subobject getField(FieldDecl *Field) {
    return Subobject{Subobject::Member, Field, Field->getLocation()};
  }

  Result visitExpandedSubobject(QualType Type, Subobject Subobj) {
    // C++2a [class.compare.default]p2 [P2002R0]:
    //   A defaulted <=> or == operator function for class C is defined as
    //   deleted if any non-static data member of C is of reference type
    if (Type->isReferenceType()) {
      if (Diagnose == ExplainDeleted) {
        S.Diag(Subobj.Loc, diag::note_defaulted_comparison_reference_member)
            << FD << RD;
      }
      return Result::deleted();
    }

    // [...] Let xi be an lvalue denoting the ith element [...]
    OpaqueValueExpr Xi(FD->getLocation(), Type, VK_LValue);
    Expr *Args[] = {&Xi, &Xi};

    // All operators start by trying to apply that same operator recursively.
    OverloadedOperatorKind OO = FD->getOverloadedOperator();
    assert(OO != OO_None && "not an overloaded operator!");
    return visitBinaryOperator(OO, Args, Subobj);
  }

  Result
  visitBinaryOperator(OverloadedOperatorKind OO, ArrayRef<Expr *> Args,
                      Subobject Subobj,
                      OverloadCandidateSet *SpaceshipCandidates = nullptr) {
    // Note that there is no need to consider rewritten candidates here if
    // we've already found there is no viable 'operator<=>' candidate (and are
    // considering synthesizing a '<=>' from '==' and '<').
    OverloadCandidateSet CandidateSet(
        FD->getLocation(), OverloadCandidateSet::CSK_Operator,
        OverloadCandidateSet::OperatorRewriteInfo(
            OO, FD->getLocation(),
            /*AllowRewrittenCandidates=*/!SpaceshipCandidates));

    /// C++2a [class.compare.default]p1 [P2002R0]:
    ///   [...] the defaulted function itself is never a candidate for overload
    ///   resolution [...]
    CandidateSet.exclude(FD);

    if (Args[0]->getType()->isOverloadableType())
      S.LookupOverloadedBinOp(CandidateSet, OO, Fns, Args);
    else
      // FIXME: We determine whether this is a valid expression by checking to
      // see if there's a viable builtin operator candidate for it. That isn't
      // really what the rules ask us to do, but should give the right results.
      S.AddBuiltinOperatorCandidates(OO, FD->getLocation(), Args, CandidateSet);

    Result R;

    OverloadCandidateSet::iterator Best;
    switch (CandidateSet.BestViableFunction(S, FD->getLocation(), Best)) {
    case OR_Success: {
      // C++2a [class.compare.secondary]p2 [P2002R0]:
      //   The operator function [...] is defined as deleted if [...] the
      //   candidate selected by overload resolution is not a rewritten
      //   candidate.
      if ((DCK == DefaultedComparisonKind::NotEqual ||
           DCK == DefaultedComparisonKind::Relational) &&
          !Best->RewriteKind) {
        if (Diagnose == ExplainDeleted) {
          if (Best->Function) {
            S.Diag(Best->Function->getLocation(),
                   diag::note_defaulted_comparison_not_rewritten_callee)
                << FD;
          } else {
            assert(Best->Conversions.size() == 2 &&
                   Best->Conversions[0].isUserDefined() &&
                   "non-user-defined conversion from class to built-in "
                   "comparison");
            S.Diag(Best->Conversions[0]
                       .UserDefined.FoundConversionFunction.getDecl()
                       ->getLocation(),
                   diag::note_defaulted_comparison_not_rewritten_conversion)
                << FD;
          }
        }
        return Result::deleted();
      }

      // Throughout C++2a [class.compare]: if overload resolution does not
      // result in a usable function, the candidate function is defined as
      // deleted. This requires that we selected an accessible function.
      //
      // Note that this only considers the access of the function when named
      // within the type of the subobject, and not the access path for any
      // derived-to-base conversion.
      CXXRecordDecl *ArgClass = Args[0]->getType()->getAsCXXRecordDecl();
      if (ArgClass && Best->FoundDecl.getDecl() &&
          Best->FoundDecl.getDecl()->isCXXClassMember()) {
        QualType ObjectType = Subobj.Kind == Subobject::Member
                                  ? Args[0]->getType()
                                  : S.Context.getRecordType(RD);
        if (!S.isMemberAccessibleForDeletion(
                ArgClass, Best->FoundDecl, ObjectType, Subobj.Loc,
                Diagnose == ExplainDeleted
                    ? S.PDiag(diag::note_defaulted_comparison_inaccessible)
                          << FD << Subobj.Kind << Subobj.Decl
                    : S.PDiag()))
          return Result::deleted();
      }

      bool NeedsDeducing =
          OO == OO_Spaceship && FD->getReturnType()->isUndeducedAutoType();

      if (FunctionDecl *BestFD = Best->Function) {
        // C++2a [class.compare.default]p3 [P2002R0]:
        //   A defaulted comparison function is constexpr-compatible if
        //   [...] no overlod resolution performed [...] results in a
        //   non-constexpr function.
        assert(!BestFD->isDeleted() && "wrong overload resolution result");
        // If it's not constexpr, explain why not.
        if (Diagnose == ExplainConstexpr && !BestFD->isConstexpr()) {
          if (Subobj.Kind != Subobject::CompleteObject)
            S.Diag(Subobj.Loc, diag::note_defaulted_comparison_not_constexpr)
              << Subobj.Kind << Subobj.Decl;
          S.Diag(BestFD->getLocation(),
                 diag::note_defaulted_comparison_not_constexpr_here);
          // Bail out after explaining; we don't want any more notes.
          return Result::deleted();
        }
        R.Constexpr &= BestFD->isConstexpr();

        if (NeedsDeducing) {
          // If any callee has an undeduced return type, deduce it now.
          // FIXME: It's not clear how a failure here should be handled. For
          // now, we produce an eager diagnostic, because that is forward
          // compatible with most (all?) other reasonable options.
          if (BestFD->getReturnType()->isUndeducedType() &&
              S.DeduceReturnType(BestFD, FD->getLocation(),
                                 /*Diagnose=*/false)) {
            // Don't produce a duplicate error when asked to explain why the
            // comparison is deleted: we diagnosed that when initially checking
            // the defaulted operator.
            if (Diagnose == NoDiagnostics) {
              S.Diag(
                  FD->getLocation(),
                  diag::err_defaulted_comparison_cannot_deduce_undeduced_auto)
                  << Subobj.Kind << Subobj.Decl;
              S.Diag(
                  Subobj.Loc,
                  diag::note_defaulted_comparison_cannot_deduce_undeduced_auto)
                  << Subobj.Kind << Subobj.Decl;
              S.Diag(BestFD->getLocation(),
                     diag::note_defaulted_comparison_cannot_deduce_callee)
                  << Subobj.Kind << Subobj.Decl;
            }
            return Result::deleted();
          }
          auto *Info = S.Context.CompCategories.lookupInfoForType(
              BestFD->getCallResultType());
          if (!Info) {
            if (Diagnose == ExplainDeleted) {
              S.Diag(Subobj.Loc, diag::note_defaulted_comparison_cannot_deduce)
                  << Subobj.Kind << Subobj.Decl
                  << BestFD->getCallResultType().withoutLocalFastQualifiers();
              S.Diag(BestFD->getLocation(),
                     diag::note_defaulted_comparison_cannot_deduce_callee)
                  << Subobj.Kind << Subobj.Decl;
            }
            return Result::deleted();
          }
          R.Category = Info->Kind;
        }
      } else {
        QualType T = Best->BuiltinParamTypes[0];
        assert(T == Best->BuiltinParamTypes[1] &&
               "builtin comparison for different types?");
        assert(Best->BuiltinParamTypes[2].isNull() &&
               "invalid builtin comparison");

        if (NeedsDeducing) {
          std::optional<ComparisonCategoryType> Cat =
              getComparisonCategoryForBuiltinCmp(T);
          assert(Cat && "no category for builtin comparison?");
          R.Category = *Cat;
        }
      }

      // Note that we might be rewriting to a different operator. That call is
      // not considered until we come to actually build the comparison function.
      break;
    }

    case OR_Ambiguous:
      if (Diagnose == ExplainDeleted) {
        unsigned Kind = 0;
        if (FD->getOverloadedOperator() == OO_Spaceship && OO != OO_Spaceship)
          Kind = OO == OO_EqualEqual ? 1 : 2;
        CandidateSet.NoteCandidates(
            PartialDiagnosticAt(
                Subobj.Loc, S.PDiag(diag::note_defaulted_comparison_ambiguous)
                                << FD << Kind << Subobj.Kind << Subobj.Decl),
            S, OCD_AmbiguousCandidates, Args);
      }
      R = Result::deleted();
      break;

    case OR_Deleted:
      if (Diagnose == ExplainDeleted) {
        if ((DCK == DefaultedComparisonKind::NotEqual ||
             DCK == DefaultedComparisonKind::Relational) &&
            !Best->RewriteKind) {
          S.Diag(Best->Function->getLocation(),
                 diag::note_defaulted_comparison_not_rewritten_callee)
              << FD;
        } else {
          S.Diag(Subobj.Loc,
                 diag::note_defaulted_comparison_calls_deleted)
              << FD << Subobj.Kind << Subobj.Decl;
          S.NoteDeletedFunction(Best->Function);
        }
      }
      R = Result::deleted();
      break;

    case OR_No_Viable_Function:
      // If there's no usable candidate, we're done unless we can rewrite a
      // '<=>' in terms of '==' and '<'.
      if (OO == OO_Spaceship &&
          S.Context.CompCategories.lookupInfoForType(FD->getReturnType())) {
        // For any kind of comparison category return type, we need a usable
        // '==' and a usable '<'.
        if (!R.add(visitBinaryOperator(OO_EqualEqual, Args, Subobj,
                                       &CandidateSet)))
          R.add(visitBinaryOperator(OO_Less, Args, Subobj, &CandidateSet));
        break;
      }

      if (Diagnose == ExplainDeleted) {
        S.Diag(Subobj.Loc, diag::note_defaulted_comparison_no_viable_function)
            << FD << (OO == OO_EqualEqual || OO == OO_ExclaimEqual)
            << Subobj.Kind << Subobj.Decl;

        // For a three-way comparison, list both the candidates for the
        // original operator and the candidates for the synthesized operator.
        if (SpaceshipCandidates) {
          SpaceshipCandidates->NoteCandidates(
              S, Args,
              SpaceshipCandidates->CompleteCandidates(S, OCD_AllCandidates,
                                                      Args, FD->getLocation()));
          S.Diag(Subobj.Loc,
                 diag::note_defaulted_comparison_no_viable_function_synthesized)
              << (OO == OO_EqualEqual ? 0 : 1);
        }

        CandidateSet.NoteCandidates(
            S, Args,
            CandidateSet.CompleteCandidates(S, OCD_AllCandidates, Args,
                                            FD->getLocation()));
      }
      R = Result::deleted();
      break;
    }

    return R;
  }
};

/// A list of statements.
struct StmtListResult {
  bool IsInvalid = false;
  llvm::SmallVector<Stmt*, 16> Stmts;

  bool add(const StmtResult &S) {
    IsInvalid |= S.isInvalid();
    if (IsInvalid)
      return true;
    Stmts.push_back(S.get());
    return false;
  }
};

/// A visitor over the notional body of a defaulted comparison that synthesizes
/// the actual body.
class DefaultedComparisonSynthesizer
    : public DefaultedComparisonVisitor<DefaultedComparisonSynthesizer,
                                        StmtListResult, StmtResult,
                                        std::pair<ExprResult, ExprResult>> {
  SourceLocation Loc;
  unsigned ArrayDepth = 0;

public:
  using Base = DefaultedComparisonVisitor;
  using ExprPair = std::pair<ExprResult, ExprResult>;

  friend Base;

  DefaultedComparisonSynthesizer(Sema &S, CXXRecordDecl *RD, FunctionDecl *FD,
                                 DefaultedComparisonKind DCK,
                                 SourceLocation BodyLoc)
      : Base(S, RD, FD, DCK), Loc(BodyLoc) {}

  /// Build a suitable function body for this defaulted comparison operator.
  StmtResult build() {
    Sema::CompoundScopeRAII CompoundScope(S);

    StmtListResult Stmts = visit();
    if (Stmts.IsInvalid)
      return StmtError();

    ExprResult RetVal;
    switch (DCK) {
    case DefaultedComparisonKind::None:
      llvm_unreachable("not a defaulted comparison");

    case DefaultedComparisonKind::Equal: {
      // C++2a [class.eq]p3:
      //   [...] compar[e] the corresponding elements [...] until the first
      //   index i where xi == yi yields [...] false. If no such index exists,
      //   V is true. Otherwise, V is false.
      //
      // Join the comparisons with '&&'s and return the result. Use a right
      // fold (traversing the conditions right-to-left), because that
      // short-circuits more naturally.
      auto OldStmts = std::move(Stmts.Stmts);
      Stmts.Stmts.clear();
      ExprResult CmpSoFar;
      // Finish a particular comparison chain.
      auto FinishCmp = [&] {
        if (Expr *Prior = CmpSoFar.get()) {
          // Convert the last expression to 'return ...;'
          if (RetVal.isUnset() && Stmts.Stmts.empty())
            RetVal = CmpSoFar;
          // Convert any prior comparison to 'if (!(...)) return false;'
          else if (Stmts.add(buildIfNotCondReturnFalse(Prior)))
            return true;
          CmpSoFar = ExprResult();
        }
        return false;
      };
      for (Stmt *EAsStmt : llvm::reverse(OldStmts)) {
        Expr *E = dyn_cast<Expr>(EAsStmt);
        if (!E) {
          // Found an array comparison.
          if (FinishCmp() || Stmts.add(EAsStmt))
            return StmtError();
          continue;
        }

        if (CmpSoFar.isUnset()) {
          CmpSoFar = E;
          continue;
        }
        CmpSoFar = S.CreateBuiltinBinOp(Loc, BO_LAnd, E, CmpSoFar.get());
        if (CmpSoFar.isInvalid())
          return StmtError();
      }
      if (FinishCmp())
        return StmtError();
      std::reverse(Stmts.Stmts.begin(), Stmts.Stmts.end());
      //   If no such index exists, V is true.
      if (RetVal.isUnset())
        RetVal = S.ActOnCXXBoolLiteral(Loc, tok::kw_true);
      break;
    }

    case DefaultedComparisonKind::ThreeWay: {
      // Per C++2a [class.spaceship]p3, as a fallback add:
      // return static_cast<R>(std::strong_ordering::equal);
      QualType StrongOrdering = S.CheckComparisonCategoryType(
          ComparisonCategoryType::StrongOrdering, Loc,
          Sema::ComparisonCategoryUsage::DefaultedOperator);
      if (StrongOrdering.isNull())
        return StmtError();
      VarDecl *EqualVD = S.Context.CompCategories.getInfoForType(StrongOrdering)
                             .getValueInfo(ComparisonCategoryResult::Equal)
                             ->VD;
      RetVal = getDecl(EqualVD);
      if (RetVal.isInvalid())
        return StmtError();
      RetVal = buildStaticCastToR(RetVal.get());
      break;
    }

    case DefaultedComparisonKind::NotEqual:
    case DefaultedComparisonKind::Relational:
      RetVal = cast<Expr>(Stmts.Stmts.pop_back_val());
      break;
    }

    // Build the final return statement.
    if (RetVal.isInvalid())
      return StmtError();
    StmtResult ReturnStmt = S.BuildReturnStmt(Loc, RetVal.get());
    if (ReturnStmt.isInvalid())
      return StmtError();
    Stmts.Stmts.push_back(ReturnStmt.get());

    return S.ActOnCompoundStmt(Loc, Loc, Stmts.Stmts, /*IsStmtExpr=*/false);
  }

private:
  ExprResult getDecl(ValueDecl *VD) {
    return S.BuildDeclarationNameExpr(
        CXXScopeSpec(), DeclarationNameInfo(VD->getDeclName(), Loc), VD);
  }

  ExprResult getParam(unsigned I) {
    ParmVarDecl *PD = FD->getParamDecl(I);
    return getDecl(PD);
  }

  ExprPair getCompleteObject() {
    unsigned Param = 0;
    ExprResult LHS;
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD);
        MD && MD->isImplicitObjectMemberFunction()) {
      // LHS is '*this'.
      LHS = S.ActOnCXXThis(Loc);
      if (!LHS.isInvalid())
        LHS = S.CreateBuiltinUnaryOp(Loc, UO_Deref, LHS.get());
    } else {
      LHS = getParam(Param++);
    }
    ExprResult RHS = getParam(Param++);
    assert(Param == FD->getNumParams());
    return {LHS, RHS};
  }

  ExprPair getBase(CXXBaseSpecifier *Base) {
    ExprPair Obj = getCompleteObject();
    if (Obj.first.isInvalid() || Obj.second.isInvalid())
      return {ExprError(), ExprError()};
    CXXCastPath Path = {Base};
    return {S.ImpCastExprToType(Obj.first.get(), Base->getType(),
                                CK_DerivedToBase, VK_LValue, &Path),
            S.ImpCastExprToType(Obj.second.get(), Base->getType(),
                                CK_DerivedToBase, VK_LValue, &Path)};
  }

  ExprPair getField(FieldDecl *Field) {
    ExprPair Obj = getCompleteObject();
    if (Obj.first.isInvalid() || Obj.second.isInvalid())
      return {ExprError(), ExprError()};

    DeclAccessPair Found = DeclAccessPair::make(Field, Field->getAccess());
    DeclarationNameInfo NameInfo(Field->getDeclName(), Loc);
    return {S.BuildFieldReferenceExpr(Obj.first.get(), /*IsArrow=*/false, Loc,
                                      CXXScopeSpec(), Field, Found, NameInfo),
            S.BuildFieldReferenceExpr(Obj.second.get(), /*IsArrow=*/false, Loc,
                                      CXXScopeSpec(), Field, Found, NameInfo)};
  }

  // FIXME: When expanding a subobject, register a note in the code synthesis
  // stack to say which subobject we're comparing.

  StmtResult buildIfNotCondReturnFalse(ExprResult Cond) {
    if (Cond.isInvalid())
      return StmtError();

    ExprResult NotCond = S.CreateBuiltinUnaryOp(Loc, UO_LNot, Cond.get());
    if (NotCond.isInvalid())
      return StmtError();

    ExprResult False = S.ActOnCXXBoolLiteral(Loc, tok::kw_false);
    assert(!False.isInvalid() && "should never fail");
    StmtResult ReturnFalse = S.BuildReturnStmt(Loc, False.get());
    if (ReturnFalse.isInvalid())
      return StmtError();

    return S.ActOnIfStmt(Loc, IfStatementKind::Ordinary, Loc, nullptr,
                         S.ActOnCondition(nullptr, Loc, NotCond.get(),
                                          Sema::ConditionKind::Boolean),
                         Loc, ReturnFalse.get(), SourceLocation(), nullptr);
  }

  StmtResult visitSubobjectArray(QualType Type, llvm::APInt Size,
                                 ExprPair Subobj) {
    QualType SizeType = S.Context.getSizeType();
    Size = Size.zextOrTrunc(S.Context.getTypeSize(SizeType));

    // Build 'size_t i$n = 0'.
    IdentifierInfo *IterationVarName = nullptr;
    {
      SmallString<8> Str;
      llvm::raw_svector_ostream OS(Str);
      OS << "i" << ArrayDepth;
      IterationVarName = &S.Context.Idents.get(OS.str());
    }
    VarDecl *IterationVar = VarDecl::Create(
        S.Context, S.CurContext, Loc, Loc, IterationVarName, SizeType,
        S.Context.getTrivialTypeSourceInfo(SizeType, Loc), SC_None);
    llvm::APInt Zero(S.Context.getTypeSize(SizeType), 0);
    IterationVar->setInit(
        IntegerLiteral::Create(S.Context, Zero, SizeType, Loc));
    Stmt *Init = new (S.Context) DeclStmt(DeclGroupRef(IterationVar), Loc, Loc);

    auto IterRef = [&] {
      ExprResult Ref = S.BuildDeclarationNameExpr(
          CXXScopeSpec(), DeclarationNameInfo(IterationVarName, Loc),
          IterationVar);
      assert(!Ref.isInvalid() && "can't reference our own variable?");
      return Ref.get();
    };

    // Build 'i$n != Size'.
    ExprResult Cond = S.CreateBuiltinBinOp(
        Loc, BO_NE, IterRef(),
        IntegerLiteral::Create(S.Context, Size, SizeType, Loc));
    assert(!Cond.isInvalid() && "should never fail");

    // Build '++i$n'.
    ExprResult Inc = S.CreateBuiltinUnaryOp(Loc, UO_PreInc, IterRef());
    assert(!Inc.isInvalid() && "should never fail");

    // Build 'a[i$n]' and 'b[i$n]'.
    auto Index = [&](ExprResult E) {
      if (E.isInvalid())
        return ExprError();
      return S.CreateBuiltinArraySubscriptExpr(E.get(), Loc, IterRef(), Loc);
    };
    Subobj.first = Index(Subobj.first);
    Subobj.second = Index(Subobj.second);

    // Compare the array elements.
    ++ArrayDepth;
    StmtResult Substmt = visitSubobject(Type, Subobj);
    --ArrayDepth;

    if (Substmt.isInvalid())
      return StmtError();

    // For the inner level of an 'operator==', build 'if (!cmp) return false;'.
    // For outer levels or for an 'operator<=>' we already have a suitable
    // statement that returns as necessary.
    if (Expr *ElemCmp = dyn_cast<Expr>(Substmt.get())) {
      assert(DCK == DefaultedComparisonKind::Equal &&
             "should have non-expression statement");
      Substmt = buildIfNotCondReturnFalse(ElemCmp);
      if (Substmt.isInvalid())
        return StmtError();
    }

    // Build 'for (...) ...'
    return S.ActOnForStmt(Loc, Loc, Init,
                          S.ActOnCondition(nullptr, Loc, Cond.get(),
                                           Sema::ConditionKind::Boolean),
                          S.MakeFullDiscardedValueExpr(Inc.get()), Loc,
                          Substmt.get());
  }

  StmtResult visitExpandedSubobject(QualType Type, ExprPair Obj) {
    if (Obj.first.isInvalid() || Obj.second.isInvalid())
      return StmtError();

    OverloadedOperatorKind OO = FD->getOverloadedOperator();
    BinaryOperatorKind Opc = BinaryOperator::getOverloadedOpcode(OO);
    ExprResult Op;
    if (Type->isOverloadableType())
      Op = S.CreateOverloadedBinOp(Loc, Opc, Fns, Obj.first.get(),
                                   Obj.second.get(), /*PerformADL=*/true,
                                   /*AllowRewrittenCandidates=*/true, FD);
    else
      Op = S.CreateBuiltinBinOp(Loc, Opc, Obj.first.get(), Obj.second.get());
    if (Op.isInvalid())
      return StmtError();

    switch (DCK) {
    case DefaultedComparisonKind::None:
      llvm_unreachable("not a defaulted comparison");

    case DefaultedComparisonKind::Equal:
      // Per C++2a [class.eq]p2, each comparison is individually contextually
      // converted to bool.
      Op = S.PerformContextuallyConvertToBool(Op.get());
      if (Op.isInvalid())
        return StmtError();
      return Op.get();

    case DefaultedComparisonKind::ThreeWay: {
      // Per C++2a [class.spaceship]p3, form:
      //   if (R cmp = static_cast<R>(op); cmp != 0)
      //     return cmp;
      QualType R = FD->getReturnType();
      Op = buildStaticCastToR(Op.get());
      if (Op.isInvalid())
        return StmtError();

      // R cmp = ...;
      IdentifierInfo *Name = &S.Context.Idents.get("cmp");
      VarDecl *VD =
          VarDecl::Create(S.Context, S.CurContext, Loc, Loc, Name, R,
                          S.Context.getTrivialTypeSourceInfo(R, Loc), SC_None);
      S.AddInitializerToDecl(VD, Op.get(), /*DirectInit=*/false);
      Stmt *InitStmt = new (S.Context) DeclStmt(DeclGroupRef(VD), Loc, Loc);

      // cmp != 0
      ExprResult VDRef = getDecl(VD);
      if (VDRef.isInvalid())
        return StmtError();
      llvm::APInt ZeroVal(S.Context.getIntWidth(S.Context.IntTy), 0);
      Expr *Zero =
          IntegerLiteral::Create(S.Context, ZeroVal, S.Context.IntTy, Loc);
      ExprResult Comp;
      if (VDRef.get()->getType()->isOverloadableType())
        Comp = S.CreateOverloadedBinOp(Loc, BO_NE, Fns, VDRef.get(), Zero, true,
                                       true, FD);
      else
        Comp = S.CreateBuiltinBinOp(Loc, BO_NE, VDRef.get(), Zero);
      if (Comp.isInvalid())
        return StmtError();
      Sema::ConditionResult Cond = S.ActOnCondition(
          nullptr, Loc, Comp.get(), Sema::ConditionKind::Boolean);
      if (Cond.isInvalid())
        return StmtError();

      // return cmp;
      VDRef = getDecl(VD);
      if (VDRef.isInvalid())
        return StmtError();
      StmtResult ReturnStmt = S.BuildReturnStmt(Loc, VDRef.get());
      if (ReturnStmt.isInvalid())
        return StmtError();

      // if (...)
      return S.ActOnIfStmt(Loc, IfStatementKind::Ordinary, Loc, InitStmt, Cond,
                           Loc, ReturnStmt.get(),
                           /*ElseLoc=*/SourceLocation(), /*Else=*/nullptr);
    }

    case DefaultedComparisonKind::NotEqual:
    case DefaultedComparisonKind::Relational:
      // C++2a [class.compare.secondary]p2:
      //   Otherwise, the operator function yields x @ y.
      return Op.get();
    }
    llvm_unreachable("");
  }

  /// Build "static_cast<R>(E)".
  ExprResult buildStaticCastToR(Expr *E) {
    QualType R = FD->getReturnType();
    assert(!R->isUndeducedType() && "type should have been deduced already");

    // Don't bother forming a no-op cast in the common case.
    if (E->isPRValue() && S.Context.hasSameType(E->getType(), R))
      return E;
    return S.BuildCXXNamedCast(Loc, tok::kw_static_cast,
                               S.Context.getTrivialTypeSourceInfo(R, Loc), E,
                               SourceRange(Loc, Loc), SourceRange(Loc, Loc));
  }
};
}

/// Perform the unqualified lookups that might be needed to form a defaulted
/// comparison function for the given operator.
static void lookupOperatorsForDefaultedComparison(Sema &Self, Scope *S,
                                                  UnresolvedSetImpl &Operators,
                                                  OverloadedOperatorKind Op) {
  auto Lookup = [&](OverloadedOperatorKind OO) {
    Self.LookupOverloadedOperatorName(OO, S, Operators);
  };

  // Every defaulted operator looks up itself.
  Lookup(Op);
  // ... and the rewritten form of itself, if any.
  if (OverloadedOperatorKind ExtraOp = getRewrittenOverloadedOperator(Op))
    Lookup(ExtraOp);

  // For 'operator<=>', we also form a 'cmp != 0' expression, and might
  // synthesize a three-way comparison from '<' and '=='. In a dependent
  // context, we also need to look up '==' in case we implicitly declare a
  // defaulted 'operator=='.
  if (Op == OO_Spaceship) {
    Lookup(OO_ExclaimEqual);
    Lookup(OO_Less);
    Lookup(OO_EqualEqual);
  }
}

bool Sema::CheckExplicitlyDefaultedComparison(Scope *S, FunctionDecl *FD,
                                              DefaultedComparisonKind DCK) {
  assert(DCK != DefaultedComparisonKind::None && "not a defaulted comparison");

  // Perform any unqualified lookups we're going to need to default this
  // function.
  if (S) {
    UnresolvedSet<32> Operators;
    lookupOperatorsForDefaultedComparison(*this, S, Operators,
                                          FD->getOverloadedOperator());
    FD->setDefaultedOrDeletedInfo(
        FunctionDecl::DefaultedOrDeletedFunctionInfo::Create(
            Context, Operators.pairs()));
  }

  // C++2a [class.compare.default]p1:
  //   A defaulted comparison operator function for some class C shall be a
  //   non-template function declared in the member-specification of C that is
  //    -- a non-static const non-volatile member of C having one parameter of
  //       type const C& and either no ref-qualifier or the ref-qualifier &, or
  //    -- a friend of C having two parameters of type const C& or two
  //       parameters of type C.

  CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(FD->getLexicalDeclContext());
  bool IsMethod = isa<CXXMethodDecl>(FD);
  if (IsMethod) {
    auto *MD = cast<CXXMethodDecl>(FD);
    assert(!MD->isStatic() && "comparison function cannot be a static member");

    if (MD->getRefQualifier() == RQ_RValue) {
      Diag(MD->getLocation(), diag::err_ref_qualifier_comparison_operator);

      // Remove the ref qualifier to recover.
      const auto *FPT = MD->getType()->castAs<FunctionProtoType>();
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      EPI.RefQualifier = RQ_None;
      MD->setType(Context.getFunctionType(FPT->getReturnType(),
                                          FPT->getParamTypes(), EPI));
    }

    // If we're out-of-class, this is the class we're comparing.
    if (!RD)
      RD = MD->getParent();
    QualType T = MD->getFunctionObjectParameterType();
    if (!T.isConstQualified()) {
      SourceLocation Loc, InsertLoc;
      if (MD->isExplicitObjectMemberFunction()) {
        Loc = MD->getParamDecl(0)->getBeginLoc();
        InsertLoc = getLocForEndOfToken(
            MD->getParamDecl(0)->getExplicitObjectParamThisLoc());
      } else {
        Loc = MD->getLocation();
        if (FunctionTypeLoc Loc = MD->getFunctionTypeLoc())
          InsertLoc = Loc.getRParenLoc();
      }
      // Don't diagnose an implicit 'operator=='; we will have diagnosed the
      // corresponding defaulted 'operator<=>' already.
      if (!MD->isImplicit()) {
        Diag(Loc, diag::err_defaulted_comparison_non_const)
            << (int)DCK << FixItHint::CreateInsertion(InsertLoc, " const");
      }

      // Add the 'const' to the type to recover.
      const auto *FPT = MD->getType()->castAs<FunctionProtoType>();
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      EPI.TypeQuals.addConst();
      MD->setType(Context.getFunctionType(FPT->getReturnType(),
                                          FPT->getParamTypes(), EPI));
    }

    if (MD->isVolatile()) {
      Diag(MD->getLocation(), diag::err_volatile_comparison_operator);

      // Remove the 'volatile' from the type to recover.
      const auto *FPT = MD->getType()->castAs<FunctionProtoType>();
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      EPI.TypeQuals.removeVolatile();
      MD->setType(Context.getFunctionType(FPT->getReturnType(),
                                          FPT->getParamTypes(), EPI));
    }
  }

  if ((FD->getNumParams() -
       (unsigned)FD->hasCXXExplicitFunctionObjectParameter()) !=
      (IsMethod ? 1 : 2)) {
    // Let's not worry about using a variadic template pack here -- who would do
    // such a thing?
    Diag(FD->getLocation(), diag::err_defaulted_comparison_num_args)
        << int(IsMethod) << int(DCK);
    return true;
  }

  const ParmVarDecl *KnownParm = nullptr;
  for (const ParmVarDecl *Param : FD->parameters()) {
    if (Param->isExplicitObjectParameter())
      continue;
    QualType ParmTy = Param->getType();

    if (!KnownParm) {
      auto CTy = ParmTy;
      // Is it `T const &`?
      bool Ok = !IsMethod;
      QualType ExpectedTy;
      if (RD)
        ExpectedTy = Context.getRecordType(RD);
      if (auto *Ref = CTy->getAs<ReferenceType>()) {
        CTy = Ref->getPointeeType();
        if (RD)
          ExpectedTy.addConst();
        Ok = true;
      }

      // Is T a class?
      if (!Ok) {
      } else if (RD) {
        if (!RD->isDependentType() && !Context.hasSameType(CTy, ExpectedTy))
          Ok = false;
      } else if (auto *CRD = CTy->getAsRecordDecl()) {
        RD = cast<CXXRecordDecl>(CRD);
      } else {
        Ok = false;
      }

      if (Ok) {
        KnownParm = Param;
      } else {
        // Don't diagnose an implicit 'operator=='; we will have diagnosed the
        // corresponding defaulted 'operator<=>' already.
        if (!FD->isImplicit()) {
          if (RD) {
            QualType PlainTy = Context.getRecordType(RD);
            QualType RefTy =
                Context.getLValueReferenceType(PlainTy.withConst());
            Diag(FD->getLocation(), diag::err_defaulted_comparison_param)
                << int(DCK) << ParmTy << RefTy << int(!IsMethod) << PlainTy
                << Param->getSourceRange();
          } else {
            assert(!IsMethod && "should know expected type for method");
            Diag(FD->getLocation(),
                 diag::err_defaulted_comparison_param_unknown)
                << int(DCK) << ParmTy << Param->getSourceRange();
          }
        }
        return true;
      }
    } else if (!Context.hasSameType(KnownParm->getType(), ParmTy)) {
      Diag(FD->getLocation(), diag::err_defaulted_comparison_param_mismatch)
          << int(DCK) << KnownParm->getType() << KnownParm->getSourceRange()
          << ParmTy << Param->getSourceRange();
      return true;
    }
  }

  assert(RD && "must have determined class");
  if (IsMethod) {
  } else if (isa<CXXRecordDecl>(FD->getLexicalDeclContext())) {
    // In-class, must be a friend decl.
    assert(FD->getFriendObjectKind() && "expected a friend declaration");
  } else {
    // Out of class, require the defaulted comparison to be a friend (of a
    // complete type).
    if (RequireCompleteType(FD->getLocation(), Context.getRecordType(RD),
                            diag::err_defaulted_comparison_not_friend, int(DCK),
                            int(1)))
      return true;

    if (llvm::none_of(RD->friends(), [&](const FriendDecl *F) {
          return FD->getCanonicalDecl() ==
                 F->getFriendDecl()->getCanonicalDecl();
        })) {
      Diag(FD->getLocation(), diag::err_defaulted_comparison_not_friend)
          << int(DCK) << int(0) << RD;
      Diag(RD->getCanonicalDecl()->getLocation(), diag::note_declared_at);
      return true;
    }
  }

  // C++2a [class.eq]p1, [class.rel]p1:
  //   A [defaulted comparison other than <=>] shall have a declared return
  //   type bool.
  if (DCK != DefaultedComparisonKind::ThreeWay &&
      !FD->getDeclaredReturnType()->isDependentType() &&
      !Context.hasSameType(FD->getDeclaredReturnType(), Context.BoolTy)) {
    Diag(FD->getLocation(), diag::err_defaulted_comparison_return_type_not_bool)
        << (int)DCK << FD->getDeclaredReturnType() << Context.BoolTy
        << FD->getReturnTypeSourceRange();
    return true;
  }
  // C++2a [class.spaceship]p2 [P2002R0]:
  //   Let R be the declared return type [...]. If R is auto, [...]. Otherwise,
  //   R shall not contain a placeholder type.
  if (QualType RT = FD->getDeclaredReturnType();
      DCK == DefaultedComparisonKind::ThreeWay &&
      RT->getContainedDeducedType() &&
      (!Context.hasSameType(RT, Context.getAutoDeductType()) ||
       RT->getContainedAutoType()->isConstrained())) {
    Diag(FD->getLocation(),
         diag::err_defaulted_comparison_deduced_return_type_not_auto)
        << (int)DCK << FD->getDeclaredReturnType() << Context.AutoDeductTy
        << FD->getReturnTypeSourceRange();
    return true;
  }

  // For a defaulted function in a dependent class, defer all remaining checks
  // until instantiation.
  if (RD->isDependentType())
    return false;

  // Determine whether the function should be defined as deleted.
  DefaultedComparisonInfo Info =
      DefaultedComparisonAnalyzer(*this, RD, FD, DCK).visit();

  bool First = FD == FD->getCanonicalDecl();

  if (!First) {
    if (Info.Deleted) {
      // C++11 [dcl.fct.def.default]p4:
      //   [For a] user-provided explicitly-defaulted function [...] if such a
      //   function is implicitly defined as deleted, the program is ill-formed.
      //
      // This is really just a consequence of the general rule that you can
      // only delete a function on its first declaration.
      Diag(FD->getLocation(), diag::err_non_first_default_compare_deletes)
          << FD->isImplicit() << (int)DCK;
      DefaultedComparisonAnalyzer(*this, RD, FD, DCK,
                                  DefaultedComparisonAnalyzer::ExplainDeleted)
          .visit();
      return true;
    }
    if (isa<CXXRecordDecl>(FD->getLexicalDeclContext())) {
      // C++20 [class.compare.default]p1:
      //   [...] A definition of a comparison operator as defaulted that appears
      //   in a class shall be the first declaration of that function.
      Diag(FD->getLocation(), diag::err_non_first_default_compare_in_class)
          << (int)DCK;
      Diag(FD->getCanonicalDecl()->getLocation(),
           diag::note_previous_declaration);
      return true;
    }
  }

  // If we want to delete the function, then do so; there's nothing else to
  // check in that case.
  if (Info.Deleted) {
    SetDeclDeleted(FD, FD->getLocation());
    if (!inTemplateInstantiation() && !FD->isImplicit()) {
      Diag(FD->getLocation(), diag::warn_defaulted_comparison_deleted)
          << (int)DCK;
      DefaultedComparisonAnalyzer(*this, RD, FD, DCK,
                                  DefaultedComparisonAnalyzer::ExplainDeleted)
          .visit();
      if (FD->getDefaultLoc().isValid())
        Diag(FD->getDefaultLoc(), diag::note_replace_equals_default_to_delete)
            << FixItHint::CreateReplacement(FD->getDefaultLoc(), "delete");
    }
    return false;
  }

  // C++2a [class.spaceship]p2:
  //   The return type is deduced as the common comparison type of R0, R1, ...
  if (DCK == DefaultedComparisonKind::ThreeWay &&
      FD->getDeclaredReturnType()->isUndeducedAutoType()) {
    SourceLocation RetLoc = FD->getReturnTypeSourceRange().getBegin();
    if (RetLoc.isInvalid())
      RetLoc = FD->getBeginLoc();
    // FIXME: Should we really care whether we have the complete type and the
    // 'enumerator' constants here? A forward declaration seems sufficient.
    QualType Cat = CheckComparisonCategoryType(
        Info.Category, RetLoc, ComparisonCategoryUsage::DefaultedOperator);
    if (Cat.isNull())
      return true;
    Context.adjustDeducedFunctionResultType(
        FD, SubstAutoType(FD->getDeclaredReturnType(), Cat));
  }

  // C++2a [dcl.fct.def.default]p3 [P2002R0]:
  //   An explicitly-defaulted function that is not defined as deleted may be
  //   declared constexpr or consteval only if it is constexpr-compatible.
  // C++2a [class.compare.default]p3 [P2002R0]:
  //   A defaulted comparison function is constexpr-compatible if it satisfies
  //   the requirements for a constexpr function [...]
  // The only relevant requirements are that the parameter and return types are
  // literal types. The remaining conditions are checked by the analyzer.
  //
  // We support P2448R2 in language modes earlier than C++23 as an extension.
  // The concept of constexpr-compatible was removed.
  // C++23 [dcl.fct.def.default]p3 [P2448R2]
  //  A function explicitly defaulted on its first declaration is implicitly
  //  inline, and is implicitly constexpr if it is constexpr-suitable.
  // C++23 [dcl.constexpr]p3
  //   A function is constexpr-suitable if
  //    - it is not a coroutine, and
  //    - if the function is a constructor or destructor, its class does not
  //      have any virtual base classes.
  if (FD->isConstexpr()) {
    if (!getLangOpts().CPlusPlus23 &&
        CheckConstexprReturnType(*this, FD, CheckConstexprKind::Diagnose) &&
        CheckConstexprParameterTypes(*this, FD, CheckConstexprKind::Diagnose) &&
        !Info.Constexpr) {
      Diag(FD->getBeginLoc(), diag::err_defaulted_comparison_constexpr_mismatch)
          << FD->isImplicit() << (int)DCK << FD->isConsteval();
      DefaultedComparisonAnalyzer(*this, RD, FD, DCK,
                                  DefaultedComparisonAnalyzer::ExplainConstexpr)
          .visit();
    }
  }

  // C++2a [dcl.fct.def.default]p3 [P2002R0]:
  //   If a constexpr-compatible function is explicitly defaulted on its first
  //   declaration, it is implicitly considered to be constexpr.
  // FIXME: Only applying this to the first declaration seems problematic, as
  // simple reorderings can affect the meaning of the program.
  if (First && !FD->isConstexpr() && Info.Constexpr)
    FD->setConstexprKind(ConstexprSpecKind::Constexpr);

  // C++2a [except.spec]p3:
  //   If a declaration of a function does not have a noexcept-specifier
  //   [and] is defaulted on its first declaration, [...] the exception
  //   specification is as specified below
  if (FD->getExceptionSpecType() == EST_None) {
    auto *FPT = FD->getType()->castAs<FunctionProtoType>();
    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
    EPI.ExceptionSpec.Type = EST_Unevaluated;
    EPI.ExceptionSpec.SourceDecl = FD;
    FD->setType(Context.getFunctionType(FPT->getReturnType(),
                                        FPT->getParamTypes(), EPI));
  }

  return false;
}

void Sema::DeclareImplicitEqualityComparison(CXXRecordDecl *RD,
                                             FunctionDecl *Spaceship) {
  Sema::CodeSynthesisContext Ctx;
  Ctx.Kind = Sema::CodeSynthesisContext::DeclaringImplicitEqualityComparison;
  Ctx.PointOfInstantiation = Spaceship->getEndLoc();
  Ctx.Entity = Spaceship;
  pushCodeSynthesisContext(Ctx);

  if (FunctionDecl *EqualEqual = SubstSpaceshipAsEqualEqual(RD, Spaceship))
    EqualEqual->setImplicit();

  popCodeSynthesisContext();
}

void Sema::DefineDefaultedComparison(SourceLocation UseLoc, FunctionDecl *FD,
                                     DefaultedComparisonKind DCK) {
  assert(FD->isDefaulted() && !FD->isDeleted() &&
         !FD->doesThisDeclarationHaveABody());
  if (FD->willHaveBody() || FD->isInvalidDecl())
    return;

  SynthesizedFunctionScope Scope(*this, FD);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(UseLoc);

  {
    // Build and set up the function body.
    // The first parameter has type maybe-ref-to maybe-const T, use that to get
    // the type of the class being compared.
    auto PT = FD->getParamDecl(0)->getType();
    CXXRecordDecl *RD = PT.getNonReferenceType()->getAsCXXRecordDecl();
    SourceLocation BodyLoc =
        FD->getEndLoc().isValid() ? FD->getEndLoc() : FD->getLocation();
    StmtResult Body =
        DefaultedComparisonSynthesizer(*this, RD, FD, DCK, BodyLoc).build();
    if (Body.isInvalid()) {
      FD->setInvalidDecl();
      return;
    }
    FD->setBody(Body.get());
    FD->markUsed(Context);
  }

  // The exception specification is needed because we are defining the
  // function. Note that this will reuse the body we just built.
  ResolveExceptionSpec(UseLoc, FD->getType()->castAs<FunctionProtoType>());

  if (ASTMutationListener *L = getASTMutationListener())
    L->CompletedImplicitDefinition(FD);
}

static Sema::ImplicitExceptionSpecification
ComputeDefaultedComparisonExceptionSpec(Sema &S, SourceLocation Loc,
                                        FunctionDecl *FD,
                                        Sema::DefaultedComparisonKind DCK) {
  ComputingExceptionSpec CES(S, FD, Loc);
  Sema::ImplicitExceptionSpecification ExceptSpec(S);

  if (FD->isInvalidDecl())
    return ExceptSpec;

  // The common case is that we just defined the comparison function. In that
  // case, just look at whether the body can throw.
  if (FD->hasBody()) {
    ExceptSpec.CalledStmt(FD->getBody());
  } else {
    // Otherwise, build a body so we can check it. This should ideally only
    // happen when we're not actually marking the function referenced. (This is
    // only really important for efficiency: we don't want to build and throw
    // away bodies for comparison functions more than we strictly need to.)

    // Pretend to synthesize the function body in an unevaluated context.
    // Note that we can't actually just go ahead and define the function here:
    // we are not permitted to mark its callees as referenced.
    Sema::SynthesizedFunctionScope Scope(S, FD);
    EnterExpressionEvaluationContext Context(
        S, Sema::ExpressionEvaluationContext::Unevaluated);

    CXXRecordDecl *RD =
        cast<CXXRecordDecl>(FD->getFriendObjectKind() == Decl::FOK_None
                                ? FD->getDeclContext()
                                : FD->getLexicalDeclContext());
    SourceLocation BodyLoc =
        FD->getEndLoc().isValid() ? FD->getEndLoc() : FD->getLocation();
    StmtResult Body =
        DefaultedComparisonSynthesizer(S, RD, FD, DCK, BodyLoc).build();
    if (!Body.isInvalid())
      ExceptSpec.CalledStmt(Body.get());

    // FIXME: Can we hold onto this body and just transform it to potentially
    // evaluated when we're asked to define the function rather than rebuilding
    // it? Either that, or we should only build the bits of the body that we
    // need (the expressions, not the statements).
  }

  return ExceptSpec;
}

void Sema::CheckDelayedMemberExceptionSpecs() {
  decltype(DelayedOverridingExceptionSpecChecks) Overriding;
  decltype(DelayedEquivalentExceptionSpecChecks) Equivalent;

  std::swap(Overriding, DelayedOverridingExceptionSpecChecks);
  std::swap(Equivalent, DelayedEquivalentExceptionSpecChecks);

  // Perform any deferred checking of exception specifications for virtual
  // destructors.
  for (auto &Check : Overriding)
    CheckOverridingFunctionExceptionSpec(Check.first, Check.second);

  // Perform any deferred checking of exception specifications for befriended
  // special members.
  for (auto &Check : Equivalent)
    CheckEquivalentExceptionSpec(Check.second, Check.first);
}

namespace {
/// CRTP base class for visiting operations performed by a special member
/// function (or inherited constructor).
template<typename Derived>
struct SpecialMemberVisitor {
  Sema &S;
  CXXMethodDecl *MD;
  CXXSpecialMemberKind CSM;
  Sema::InheritedConstructorInfo *ICI;

  // Properties of the special member, computed for convenience.
  bool IsConstructor = false, IsAssignment = false, ConstArg = false;

  SpecialMemberVisitor(Sema &S, CXXMethodDecl *MD, CXXSpecialMemberKind CSM,
                       Sema::InheritedConstructorInfo *ICI)
      : S(S), MD(MD), CSM(CSM), ICI(ICI) {
    switch (CSM) {
    case CXXSpecialMemberKind::DefaultConstructor:
    case CXXSpecialMemberKind::CopyConstructor:
    case CXXSpecialMemberKind::MoveConstructor:
      IsConstructor = true;
      break;
    case CXXSpecialMemberKind::CopyAssignment:
    case CXXSpecialMemberKind::MoveAssignment:
      IsAssignment = true;
      break;
    case CXXSpecialMemberKind::Destructor:
      break;
    case CXXSpecialMemberKind::Invalid:
      llvm_unreachable("invalid special member kind");
    }

    if (MD->getNumExplicitParams()) {
      if (const ReferenceType *RT =
              MD->getNonObjectParameter(0)->getType()->getAs<ReferenceType>())
        ConstArg = RT->getPointeeType().isConstQualified();
    }
  }

  Derived &getDerived() { return static_cast<Derived&>(*this); }

  /// Is this a "move" special member?
  bool isMove() const {
    return CSM == CXXSpecialMemberKind::MoveConstructor ||
           CSM == CXXSpecialMemberKind::MoveAssignment;
  }

  /// Look up the corresponding special member in the given class.
  Sema::SpecialMemberOverloadResult lookupIn(CXXRecordDecl *Class,
                                             unsigned Quals, bool IsMutable) {
    return lookupCallFromSpecialMember(S, Class, CSM, Quals,
                                       ConstArg && !IsMutable);
  }

  /// Look up the constructor for the specified base class to see if it's
  /// overridden due to this being an inherited constructor.
  Sema::SpecialMemberOverloadResult lookupInheritedCtor(CXXRecordDecl *Class) {
    if (!ICI)
      return {};
    assert(CSM == CXXSpecialMemberKind::DefaultConstructor);
    auto *BaseCtor =
      cast<CXXConstructorDecl>(MD)->getInheritedConstructor().getConstructor();
    if (auto *MD = ICI->findConstructorForBase(Class, BaseCtor).first)
      return MD;
    return {};
  }

  /// A base or member subobject.
  typedef llvm::PointerUnion<CXXBaseSpecifier*, FieldDecl*> Subobject;

  /// Get the location to use for a subobject in diagnostics.
  static SourceLocation getSubobjectLoc(Subobject Subobj) {
    // FIXME: For an indirect virtual base, the direct base leading to
    // the indirect virtual base would be a more useful choice.
    if (auto *B = Subobj.dyn_cast<CXXBaseSpecifier*>())
      return B->getBaseTypeLoc();
    else
      return Subobj.get<FieldDecl*>()->getLocation();
  }

  enum BasesToVisit {
    /// Visit all non-virtual (direct) bases.
    VisitNonVirtualBases,
    /// Visit all direct bases, virtual or not.
    VisitDirectBases,
    /// Visit all non-virtual bases, and all virtual bases if the class
    /// is not abstract.
    VisitPotentiallyConstructedBases,
    /// Visit all direct or virtual bases.
    VisitAllBases
  };

  // Visit the bases and members of the class.
  bool visit(BasesToVisit Bases) {
    CXXRecordDecl *RD = MD->getParent();

    if (Bases == VisitPotentiallyConstructedBases)
      Bases = RD->isAbstract() ? VisitNonVirtualBases : VisitAllBases;

    for (auto &B : RD->bases())
      if ((Bases == VisitDirectBases || !B.isVirtual()) &&
          getDerived().visitBase(&B))
        return true;

    if (Bases == VisitAllBases)
      for (auto &B : RD->vbases())
        if (getDerived().visitBase(&B))
          return true;

    for (auto *F : RD->fields())
      if (!F->isInvalidDecl() && !F->isUnnamedBitField() &&
          getDerived().visitField(F))
        return true;

    return false;
  }
};
}

namespace {
struct SpecialMemberDeletionInfo
    : SpecialMemberVisitor<SpecialMemberDeletionInfo> {
  bool Diagnose;

  SourceLocation Loc;

  bool AllFieldsAreConst;

  SpecialMemberDeletionInfo(Sema &S, CXXMethodDecl *MD,
                            CXXSpecialMemberKind CSM,
                            Sema::InheritedConstructorInfo *ICI, bool Diagnose)
      : SpecialMemberVisitor(S, MD, CSM, ICI), Diagnose(Diagnose),
        Loc(MD->getLocation()), AllFieldsAreConst(true) {}

  bool inUnion() const { return MD->getParent()->isUnion(); }

  CXXSpecialMemberKind getEffectiveCSM() {
    return ICI ? CXXSpecialMemberKind::Invalid : CSM;
  }

  bool shouldDeleteForVariantObjCPtrMember(FieldDecl *FD, QualType FieldType);

  bool visitBase(CXXBaseSpecifier *Base) { return shouldDeleteForBase(Base); }
  bool visitField(FieldDecl *Field) { return shouldDeleteForField(Field); }

  bool shouldDeleteForBase(CXXBaseSpecifier *Base);
  bool shouldDeleteForField(FieldDecl *FD);
  bool shouldDeleteForAllConstMembers();

  bool shouldDeleteForClassSubobject(CXXRecordDecl *Class, Subobject Subobj,
                                     unsigned Quals);
  bool shouldDeleteForSubobjectCall(Subobject Subobj,
                                    Sema::SpecialMemberOverloadResult SMOR,
                                    bool IsDtorCallInCtor);

  bool isAccessible(Subobject Subobj, CXXMethodDecl *D);
};
}

/// Is the given special member inaccessible when used on the given
/// sub-object.
bool SpecialMemberDeletionInfo::isAccessible(Subobject Subobj,
                                             CXXMethodDecl *target) {
  /// If we're operating on a base class, the object type is the
  /// type of this special member.
  QualType objectTy;
  AccessSpecifier access = target->getAccess();
  if (CXXBaseSpecifier *base = Subobj.dyn_cast<CXXBaseSpecifier*>()) {
    objectTy = S.Context.getTypeDeclType(MD->getParent());
    access = CXXRecordDecl::MergeAccess(base->getAccessSpecifier(), access);

  // If we're operating on a field, the object type is the type of the field.
  } else {
    objectTy = S.Context.getTypeDeclType(target->getParent());
  }

  return S.isMemberAccessibleForDeletion(
      target->getParent(), DeclAccessPair::make(target, access), objectTy);
}

/// Check whether we should delete a special member due to the implicit
/// definition containing a call to a special member of a subobject.
bool SpecialMemberDeletionInfo::shouldDeleteForSubobjectCall(
    Subobject Subobj, Sema::SpecialMemberOverloadResult SMOR,
    bool IsDtorCallInCtor) {
  CXXMethodDecl *Decl = SMOR.getMethod();
  FieldDecl *Field = Subobj.dyn_cast<FieldDecl*>();

  int DiagKind = -1;

  if (SMOR.getKind() == Sema::SpecialMemberOverloadResult::NoMemberOrDeleted)
    DiagKind = !Decl ? 0 : 1;
  else if (SMOR.getKind() == Sema::SpecialMemberOverloadResult::Ambiguous)
    DiagKind = 2;
  else if (!isAccessible(Subobj, Decl))
    DiagKind = 3;
  else if (!IsDtorCallInCtor && Field && Field->getParent()->isUnion() &&
           !Decl->isTrivial()) {
    // A member of a union must have a trivial corresponding special member.
    // As a weird special case, a destructor call from a union's constructor
    // must be accessible and non-deleted, but need not be trivial. Such a
    // destructor is never actually called, but is semantically checked as
    // if it were.
    if (CSM == CXXSpecialMemberKind::DefaultConstructor) {
      // [class.default.ctor]p2:
      //   A defaulted default constructor for class X is defined as deleted if
      //   - X is a union that has a variant member with a non-trivial default
      //     constructor and no variant member of X has a default member
      //     initializer
      const auto *RD = cast<CXXRecordDecl>(Field->getParent());
      if (!RD->hasInClassInitializer())
        DiagKind = 4;
    } else {
      DiagKind = 4;
    }
  }

  if (DiagKind == -1)
    return false;

  if (Diagnose) {
    if (Field) {
      S.Diag(Field->getLocation(),
             diag::note_deleted_special_member_class_subobject)
          << llvm::to_underlying(getEffectiveCSM()) << MD->getParent()
          << /*IsField*/ true << Field << DiagKind << IsDtorCallInCtor
          << /*IsObjCPtr*/ false;
    } else {
      CXXBaseSpecifier *Base = Subobj.get<CXXBaseSpecifier*>();
      S.Diag(Base->getBeginLoc(),
             diag::note_deleted_special_member_class_subobject)
          << llvm::to_underlying(getEffectiveCSM()) << MD->getParent()
          << /*IsField*/ false << Base->getType() << DiagKind
          << IsDtorCallInCtor << /*IsObjCPtr*/ false;
    }

    if (DiagKind == 1)
      S.NoteDeletedFunction(Decl);
    // FIXME: Explain inaccessibility if DiagKind == 3.
  }

  return true;
}

/// Check whether we should delete a special member function due to having a
/// direct or virtual base class or non-static data member of class type M.
bool SpecialMemberDeletionInfo::shouldDeleteForClassSubobject(
    CXXRecordDecl *Class, Subobject Subobj, unsigned Quals) {
  FieldDecl *Field = Subobj.dyn_cast<FieldDecl*>();
  bool IsMutable = Field && Field->isMutable();

  // C++11 [class.ctor]p5:
  // -- any direct or virtual base class, or non-static data member with no
  //    brace-or-equal-initializer, has class type M (or array thereof) and
  //    either M has no default constructor or overload resolution as applied
  //    to M's default constructor results in an ambiguity or in a function
  //    that is deleted or inaccessible
  // C++11 [class.copy]p11, C++11 [class.copy]p23:
  // -- a direct or virtual base class B that cannot be copied/moved because
  //    overload resolution, as applied to B's corresponding special member,
  //    results in an ambiguity or a function that is deleted or inaccessible
  //    from the defaulted special member
  // C++11 [class.dtor]p5:
  // -- any direct or virtual base class [...] has a type with a destructor
  //    that is deleted or inaccessible
  if (!(CSM == CXXSpecialMemberKind::DefaultConstructor && Field &&
        Field->hasInClassInitializer()) &&
      shouldDeleteForSubobjectCall(Subobj, lookupIn(Class, Quals, IsMutable),
                                   false))
    return true;

  // C++11 [class.ctor]p5, C++11 [class.copy]p11:
  // -- any direct or virtual base class or non-static data member has a
  //    type with a destructor that is deleted or inaccessible
  if (IsConstructor) {
    Sema::SpecialMemberOverloadResult SMOR =
        S.LookupSpecialMember(Class, CXXSpecialMemberKind::Destructor, false,
                              false, false, false, false);
    if (shouldDeleteForSubobjectCall(Subobj, SMOR, true))
      return true;
  }

  return false;
}

bool SpecialMemberDeletionInfo::shouldDeleteForVariantObjCPtrMember(
    FieldDecl *FD, QualType FieldType) {
  // The defaulted special functions are defined as deleted if this is a variant
  // member with a non-trivial ownership type, e.g., ObjC __strong or __weak
  // type under ARC.
  if (!FieldType.hasNonTrivialObjCLifetime())
    return false;

  // Don't make the defaulted default constructor defined as deleted if the
  // member has an in-class initializer.
  if (CSM == CXXSpecialMemberKind::DefaultConstructor &&
      FD->hasInClassInitializer())
    return false;

  if (Diagnose) {
    auto *ParentClass = cast<CXXRecordDecl>(FD->getParent());
    S.Diag(FD->getLocation(), diag::note_deleted_special_member_class_subobject)
        << llvm::to_underlying(getEffectiveCSM()) << ParentClass
        << /*IsField*/ true << FD << 4 << /*IsDtorCallInCtor*/ false
        << /*IsObjCPtr*/ true;
  }

  return true;
}

/// Check whether we should delete a special member function due to the class
/// having a particular direct or virtual base class.
bool SpecialMemberDeletionInfo::shouldDeleteForBase(CXXBaseSpecifier *Base) {
  CXXRecordDecl *BaseClass = Base->getType()->getAsCXXRecordDecl();
  // If program is correct, BaseClass cannot be null, but if it is, the error
  // must be reported elsewhere.
  if (!BaseClass)
    return false;
  // If we have an inheriting constructor, check whether we're calling an
  // inherited constructor instead of a default constructor.
  Sema::SpecialMemberOverloadResult SMOR = lookupInheritedCtor(BaseClass);
  if (auto *BaseCtor = SMOR.getMethod()) {
    // Note that we do not check access along this path; other than that,
    // this is the same as shouldDeleteForSubobjectCall(Base, BaseCtor, false);
    // FIXME: Check that the base has a usable destructor! Sink this into
    // shouldDeleteForClassSubobject.
    if (BaseCtor->isDeleted() && Diagnose) {
      S.Diag(Base->getBeginLoc(),
             diag::note_deleted_special_member_class_subobject)
          << llvm::to_underlying(getEffectiveCSM()) << MD->getParent()
          << /*IsField*/ false << Base->getType() << /*Deleted*/ 1
          << /*IsDtorCallInCtor*/ false << /*IsObjCPtr*/ false;
      S.NoteDeletedFunction(BaseCtor);
    }
    return BaseCtor->isDeleted();
  }
  return shouldDeleteForClassSubobject(BaseClass, Base, 0);
}

/// Check whether we should delete a special member function due to the class
/// having a particular non-static data member.
bool SpecialMemberDeletionInfo::shouldDeleteForField(FieldDecl *FD) {
  QualType FieldType = S.Context.getBaseElementType(FD->getType());
  CXXRecordDecl *FieldRecord = FieldType->getAsCXXRecordDecl();

  if (inUnion() && shouldDeleteForVariantObjCPtrMember(FD, FieldType))
    return true;

  if (CSM == CXXSpecialMemberKind::DefaultConstructor) {
    // For a default constructor, all references must be initialized in-class
    // and, if a union, it must have a non-const member.
    if (FieldType->isReferenceType() && !FD->hasInClassInitializer()) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_default_ctor_uninit_field)
          << !!ICI << MD->getParent() << FD << FieldType << /*Reference*/0;
      return true;
    }
    // C++11 [class.ctor]p5 (modified by DR2394): any non-variant non-static
    // data member of const-qualified type (or array thereof) with no
    // brace-or-equal-initializer is not const-default-constructible.
    if (!inUnion() && FieldType.isConstQualified() &&
        !FD->hasInClassInitializer() &&
        (!FieldRecord || !FieldRecord->allowConstDefaultInit())) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_default_ctor_uninit_field)
          << !!ICI << MD->getParent() << FD << FD->getType() << /*Const*/1;
      return true;
    }

    if (inUnion() && !FieldType.isConstQualified())
      AllFieldsAreConst = false;
  } else if (CSM == CXXSpecialMemberKind::CopyConstructor) {
    // For a copy constructor, data members must not be of rvalue reference
    // type.
    if (FieldType->isRValueReferenceType()) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_copy_ctor_rvalue_reference)
          << MD->getParent() << FD << FieldType;
      return true;
    }
  } else if (IsAssignment) {
    // For an assignment operator, data members must not be of reference type.
    if (FieldType->isReferenceType()) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_assign_field)
          << isMove() << MD->getParent() << FD << FieldType << /*Reference*/0;
      return true;
    }
    if (!FieldRecord && FieldType.isConstQualified()) {
      // C++11 [class.copy]p23:
      // -- a non-static data member of const non-class type (or array thereof)
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_assign_field)
          << isMove() << MD->getParent() << FD << FD->getType() << /*Const*/1;
      return true;
    }
  }

  if (FieldRecord) {
    // Some additional restrictions exist on the variant members.
    if (!inUnion() && FieldRecord->isUnion() &&
        FieldRecord->isAnonymousStructOrUnion()) {
      bool AllVariantFieldsAreConst = true;

      // FIXME: Handle anonymous unions declared within anonymous unions.
      for (auto *UI : FieldRecord->fields()) {
        QualType UnionFieldType = S.Context.getBaseElementType(UI->getType());

        if (shouldDeleteForVariantObjCPtrMember(&*UI, UnionFieldType))
          return true;

        if (!UnionFieldType.isConstQualified())
          AllVariantFieldsAreConst = false;

        CXXRecordDecl *UnionFieldRecord = UnionFieldType->getAsCXXRecordDecl();
        if (UnionFieldRecord &&
            shouldDeleteForClassSubobject(UnionFieldRecord, UI,
                                          UnionFieldType.getCVRQualifiers()))
          return true;
      }

      // At least one member in each anonymous union must be non-const
      if (CSM == CXXSpecialMemberKind::DefaultConstructor &&
          AllVariantFieldsAreConst && !FieldRecord->field_empty()) {
        if (Diagnose)
          S.Diag(FieldRecord->getLocation(),
                 diag::note_deleted_default_ctor_all_const)
            << !!ICI << MD->getParent() << /*anonymous union*/1;
        return true;
      }

      // Don't check the implicit member of the anonymous union type.
      // This is technically non-conformant but supported, and we have a
      // diagnostic for this elsewhere.
      return false;
    }

    if (shouldDeleteForClassSubobject(FieldRecord, FD,
                                      FieldType.getCVRQualifiers()))
      return true;
  }

  return false;
}

/// C++11 [class.ctor] p5:
///   A defaulted default constructor for a class X is defined as deleted if
/// X is a union and all of its variant members are of const-qualified type.
bool SpecialMemberDeletionInfo::shouldDeleteForAllConstMembers() {
  // This is a silly definition, because it gives an empty union a deleted
  // default constructor. Don't do that.
  if (CSM == CXXSpecialMemberKind::DefaultConstructor && inUnion() &&
      AllFieldsAreConst) {
    bool AnyFields = false;
    for (auto *F : MD->getParent()->fields())
      if ((AnyFields = !F->isUnnamedBitField()))
        break;
    if (!AnyFields)
      return false;
    if (Diagnose)
      S.Diag(MD->getParent()->getLocation(),
             diag::note_deleted_default_ctor_all_const)
        << !!ICI << MD->getParent() << /*not anonymous union*/0;
    return true;
  }
  return false;
}

/// Determine whether a defaulted special member function should be defined as
/// deleted, as specified in C++11 [class.ctor]p5, C++11 [class.copy]p11,
/// C++11 [class.copy]p23, and C++11 [class.dtor]p5.
bool Sema::ShouldDeleteSpecialMember(CXXMethodDecl *MD,
                                     CXXSpecialMemberKind CSM,
                                     InheritedConstructorInfo *ICI,
                                     bool Diagnose) {
  if (MD->isInvalidDecl())
    return false;
  CXXRecordDecl *RD = MD->getParent();
  assert(!RD->isDependentType() && "do deletion after instantiation");
  if (!LangOpts.CPlusPlus || (!LangOpts.CPlusPlus11 && !RD->isLambda()) ||
      RD->isInvalidDecl())
    return false;

  // C++11 [expr.lambda.prim]p19:
  //   The closure type associated with a lambda-expression has a
  //   deleted (8.4.3) default constructor and a deleted copy
  //   assignment operator.
  // C++2a adds back these operators if the lambda has no lambda-capture.
  if (RD->isLambda() && !RD->lambdaIsDefaultConstructibleAndAssignable() &&
      (CSM == CXXSpecialMemberKind::DefaultConstructor ||
       CSM == CXXSpecialMemberKind::CopyAssignment)) {
    if (Diagnose)
      Diag(RD->getLocation(), diag::note_lambda_decl);
    return true;
  }

  // For an anonymous struct or union, the copy and assignment special members
  // will never be used, so skip the check. For an anonymous union declared at
  // namespace scope, the constructor and destructor are used.
  if (CSM != CXXSpecialMemberKind::DefaultConstructor &&
      CSM != CXXSpecialMemberKind::Destructor && RD->isAnonymousStructOrUnion())
    return false;

  // C++11 [class.copy]p7, p18:
  //   If the class definition declares a move constructor or move assignment
  //   operator, an implicitly declared copy constructor or copy assignment
  //   operator is defined as deleted.
  if (MD->isImplicit() && (CSM == CXXSpecialMemberKind::CopyConstructor ||
                           CSM == CXXSpecialMemberKind::CopyAssignment)) {
    CXXMethodDecl *UserDeclaredMove = nullptr;

    // In Microsoft mode up to MSVC 2013, a user-declared move only causes the
    // deletion of the corresponding copy operation, not both copy operations.
    // MSVC 2015 has adopted the standards conforming behavior.
    bool DeletesOnlyMatchingCopy =
        getLangOpts().MSVCCompat &&
        !getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015);

    if (RD->hasUserDeclaredMoveConstructor() &&
        (!DeletesOnlyMatchingCopy ||
         CSM == CXXSpecialMemberKind::CopyConstructor)) {
      if (!Diagnose) return true;

      // Find any user-declared move constructor.
      for (auto *I : RD->ctors()) {
        if (I->isMoveConstructor()) {
          UserDeclaredMove = I;
          break;
        }
      }
      assert(UserDeclaredMove);
    } else if (RD->hasUserDeclaredMoveAssignment() &&
               (!DeletesOnlyMatchingCopy ||
                CSM == CXXSpecialMemberKind::CopyAssignment)) {
      if (!Diagnose) return true;

      // Find any user-declared move assignment operator.
      for (auto *I : RD->methods()) {
        if (I->isMoveAssignmentOperator()) {
          UserDeclaredMove = I;
          break;
        }
      }
      assert(UserDeclaredMove);
    }

    if (UserDeclaredMove) {
      Diag(UserDeclaredMove->getLocation(),
           diag::note_deleted_copy_user_declared_move)
          << (CSM == CXXSpecialMemberKind::CopyAssignment) << RD
          << UserDeclaredMove->isMoveAssignmentOperator();
      return true;
    }
  }

  // Do access control from the special member function
  ContextRAII MethodContext(*this, MD);

  // C++11 [class.dtor]p5:
  // -- for a virtual destructor, lookup of the non-array deallocation function
  //    results in an ambiguity or in a function that is deleted or inaccessible
  if (CSM == CXXSpecialMemberKind::Destructor && MD->isVirtual()) {
    FunctionDecl *OperatorDelete = nullptr;
    DeclarationName Name =
      Context.DeclarationNames.getCXXOperatorName(OO_Delete);
    if (FindDeallocationFunction(MD->getLocation(), MD->getParent(), Name,
                                 OperatorDelete, /*Diagnose*/false)) {
      if (Diagnose)
        Diag(RD->getLocation(), diag::note_deleted_dtor_no_operator_delete);
      return true;
    }
  }

  SpecialMemberDeletionInfo SMI(*this, MD, CSM, ICI, Diagnose);

  // Per DR1611, do not consider virtual bases of constructors of abstract
  // classes, since we are not going to construct them.
  // Per DR1658, do not consider virtual bases of destructors of abstract
  // classes either.
  // Per DR2180, for assignment operators we only assign (and thus only
  // consider) direct bases.
  if (SMI.visit(SMI.IsAssignment ? SMI.VisitDirectBases
                                 : SMI.VisitPotentiallyConstructedBases))
    return true;

  if (SMI.shouldDeleteForAllConstMembers())
    return true;

  if (getLangOpts().CUDA) {
    // We should delete the special member in CUDA mode if target inference
    // failed.
    // For inherited constructors (non-null ICI), CSM may be passed so that MD
    // is treated as certain special member, which may not reflect what special
    // member MD really is. However inferTargetForImplicitSpecialMember
    // expects CSM to match MD, therefore recalculate CSM.
    assert(ICI || CSM == getSpecialMember(MD));
    auto RealCSM = CSM;
    if (ICI)
      RealCSM = getSpecialMember(MD);

    return CUDA().inferTargetForImplicitSpecialMember(RD, RealCSM, MD,
                                                      SMI.ConstArg, Diagnose);
  }

  return false;
}

void Sema::DiagnoseDeletedDefaultedFunction(FunctionDecl *FD) {
  DefaultedFunctionKind DFK = getDefaultedFunctionKind(FD);
  assert(DFK && "not a defaultable function");
  assert(FD->isDefaulted() && FD->isDeleted() && "not defaulted and deleted");

  if (DFK.isSpecialMember()) {
    ShouldDeleteSpecialMember(cast<CXXMethodDecl>(FD), DFK.asSpecialMember(),
                              nullptr, /*Diagnose=*/true);
  } else {
    DefaultedComparisonAnalyzer(
        *this, cast<CXXRecordDecl>(FD->getLexicalDeclContext()), FD,
        DFK.asComparison(), DefaultedComparisonAnalyzer::ExplainDeleted)
        .visit();
  }
}

/// Perform lookup for a special member of the specified kind, and determine
/// whether it is trivial. If the triviality can be determined without the
/// lookup, skip it. This is intended for use when determining whether a
/// special member of a containing object is trivial, and thus does not ever
/// perform overload resolution for default constructors.
///
/// If \p Selected is not \c NULL, \c *Selected will be filled in with the
/// member that was most likely to be intended to be trivial, if any.
///
/// If \p ForCall is true, look at CXXRecord::HasTrivialSpecialMembersForCall to
/// determine whether the special member is trivial.
static bool findTrivialSpecialMember(Sema &S, CXXRecordDecl *RD,
                                     CXXSpecialMemberKind CSM, unsigned Quals,
                                     bool ConstRHS,
                                     Sema::TrivialABIHandling TAH,
                                     CXXMethodDecl **Selected) {
  if (Selected)
    *Selected = nullptr;

  switch (CSM) {
  case CXXSpecialMemberKind::Invalid:
    llvm_unreachable("not a special member");

  case CXXSpecialMemberKind::DefaultConstructor:
    // C++11 [class.ctor]p5:
    //   A default constructor is trivial if:
    //    - all the [direct subobjects] have trivial default constructors
    //
    // Note, no overload resolution is performed in this case.
    if (RD->hasTrivialDefaultConstructor())
      return true;

    if (Selected) {
      // If there's a default constructor which could have been trivial, dig it
      // out. Otherwise, if there's any user-provided default constructor, point
      // to that as an example of why there's not a trivial one.
      CXXConstructorDecl *DefCtor = nullptr;
      if (RD->needsImplicitDefaultConstructor())
        S.DeclareImplicitDefaultConstructor(RD);
      for (auto *CI : RD->ctors()) {
        if (!CI->isDefaultConstructor())
          continue;
        DefCtor = CI;
        if (!DefCtor->isUserProvided())
          break;
      }

      *Selected = DefCtor;
    }

    return false;

  case CXXSpecialMemberKind::Destructor:
    // C++11 [class.dtor]p5:
    //   A destructor is trivial if:
    //    - all the direct [subobjects] have trivial destructors
    if (RD->hasTrivialDestructor() ||
        (TAH == Sema::TAH_ConsiderTrivialABI &&
         RD->hasTrivialDestructorForCall()))
      return true;

    if (Selected) {
      if (RD->needsImplicitDestructor())
        S.DeclareImplicitDestructor(RD);
      *Selected = RD->getDestructor();
    }

    return false;

  case CXXSpecialMemberKind::CopyConstructor:
    // C++11 [class.copy]p12:
    //   A copy constructor is trivial if:
    //    - the constructor selected to copy each direct [subobject] is trivial
    if (RD->hasTrivialCopyConstructor() ||
        (TAH == Sema::TAH_ConsiderTrivialABI &&
         RD->hasTrivialCopyConstructorForCall())) {
      if (Quals == Qualifiers::Const)
        // We must either select the trivial copy constructor or reach an
        // ambiguity; no need to actually perform overload resolution.
        return true;
    } else if (!Selected) {
      return false;
    }
    // In C++98, we are not supposed to perform overload resolution here, but we
    // treat that as a language defect, as suggested on cxx-abi-dev, to treat
    // cases like B as having a non-trivial copy constructor:
    //   struct A { template<typename T> A(T&); };
    //   struct B { mutable A a; };
    goto NeedOverloadResolution;

  case CXXSpecialMemberKind::CopyAssignment:
    // C++11 [class.copy]p25:
    //   A copy assignment operator is trivial if:
    //    - the assignment operator selected to copy each direct [subobject] is
    //      trivial
    if (RD->hasTrivialCopyAssignment()) {
      if (Quals == Qualifiers::Const)
        return true;
    } else if (!Selected) {
      return false;
    }
    // In C++98, we are not supposed to perform overload resolution here, but we
    // treat that as a language defect.
    goto NeedOverloadResolution;

  case CXXSpecialMemberKind::MoveConstructor:
  case CXXSpecialMemberKind::MoveAssignment:
  NeedOverloadResolution:
    Sema::SpecialMemberOverloadResult SMOR =
        lookupCallFromSpecialMember(S, RD, CSM, Quals, ConstRHS);

    // The standard doesn't describe how to behave if the lookup is ambiguous.
    // We treat it as not making the member non-trivial, just like the standard
    // mandates for the default constructor. This should rarely matter, because
    // the member will also be deleted.
    if (SMOR.getKind() == Sema::SpecialMemberOverloadResult::Ambiguous)
      return true;

    if (!SMOR.getMethod()) {
      assert(SMOR.getKind() ==
             Sema::SpecialMemberOverloadResult::NoMemberOrDeleted);
      return false;
    }

    // We deliberately don't check if we found a deleted special member. We're
    // not supposed to!
    if (Selected)
      *Selected = SMOR.getMethod();

    if (TAH == Sema::TAH_ConsiderTrivialABI &&
        (CSM == CXXSpecialMemberKind::CopyConstructor ||
         CSM == CXXSpecialMemberKind::MoveConstructor))
      return SMOR.getMethod()->isTrivialForCall();
    return SMOR.getMethod()->isTrivial();
  }

  llvm_unreachable("unknown special method kind");
}

static CXXConstructorDecl *findUserDeclaredCtor(CXXRecordDecl *RD) {
  for (auto *CI : RD->ctors())
    if (!CI->isImplicit())
      return CI;

  // Look for constructor templates.
  typedef CXXRecordDecl::specific_decl_iterator<FunctionTemplateDecl> tmpl_iter;
  for (tmpl_iter TI(RD->decls_begin()), TE(RD->decls_end()); TI != TE; ++TI) {
    if (CXXConstructorDecl *CD =
          dyn_cast<CXXConstructorDecl>(TI->getTemplatedDecl()))
      return CD;
  }

  return nullptr;
}

/// The kind of subobject we are checking for triviality. The values of this
/// enumeration are used in diagnostics.
enum TrivialSubobjectKind {
  /// The subobject is a base class.
  TSK_BaseClass,
  /// The subobject is a non-static data member.
  TSK_Field,
  /// The object is actually the complete object.
  TSK_CompleteObject
};

/// Check whether the special member selected for a given type would be trivial.
static bool checkTrivialSubobjectCall(Sema &S, SourceLocation SubobjLoc,
                                      QualType SubType, bool ConstRHS,
                                      CXXSpecialMemberKind CSM,
                                      TrivialSubobjectKind Kind,
                                      Sema::TrivialABIHandling TAH,
                                      bool Diagnose) {
  CXXRecordDecl *SubRD = SubType->getAsCXXRecordDecl();
  if (!SubRD)
    return true;

  CXXMethodDecl *Selected;
  if (findTrivialSpecialMember(S, SubRD, CSM, SubType.getCVRQualifiers(),
                               ConstRHS, TAH, Diagnose ? &Selected : nullptr))
    return true;

  if (Diagnose) {
    if (ConstRHS)
      SubType.addConst();

    if (!Selected && CSM == CXXSpecialMemberKind::DefaultConstructor) {
      S.Diag(SubobjLoc, diag::note_nontrivial_no_def_ctor)
        << Kind << SubType.getUnqualifiedType();
      if (CXXConstructorDecl *CD = findUserDeclaredCtor(SubRD))
        S.Diag(CD->getLocation(), diag::note_user_declared_ctor);
    } else if (!Selected)
      S.Diag(SubobjLoc, diag::note_nontrivial_no_copy)
          << Kind << SubType.getUnqualifiedType() << llvm::to_underlying(CSM)
          << SubType;
    else if (Selected->isUserProvided()) {
      if (Kind == TSK_CompleteObject)
        S.Diag(Selected->getLocation(), diag::note_nontrivial_user_provided)
            << Kind << SubType.getUnqualifiedType() << llvm::to_underlying(CSM);
      else {
        S.Diag(SubobjLoc, diag::note_nontrivial_user_provided)
            << Kind << SubType.getUnqualifiedType() << llvm::to_underlying(CSM);
        S.Diag(Selected->getLocation(), diag::note_declared_at);
      }
    } else {
      if (Kind != TSK_CompleteObject)
        S.Diag(SubobjLoc, diag::note_nontrivial_subobject)
            << Kind << SubType.getUnqualifiedType() << llvm::to_underlying(CSM);

      // Explain why the defaulted or deleted special member isn't trivial.
      S.SpecialMemberIsTrivial(Selected, CSM, Sema::TAH_IgnoreTrivialABI,
                               Diagnose);
    }
  }

  return false;
}

/// Check whether the members of a class type allow a special member to be
/// trivial.
static bool checkTrivialClassMembers(Sema &S, CXXRecordDecl *RD,
                                     CXXSpecialMemberKind CSM, bool ConstArg,
                                     Sema::TrivialABIHandling TAH,
                                     bool Diagnose) {
  for (const auto *FI : RD->fields()) {
    if (FI->isInvalidDecl() || FI->isUnnamedBitField())
      continue;

    QualType FieldType = S.Context.getBaseElementType(FI->getType());

    // Pretend anonymous struct or union members are members of this class.
    if (FI->isAnonymousStructOrUnion()) {
      if (!checkTrivialClassMembers(S, FieldType->getAsCXXRecordDecl(),
                                    CSM, ConstArg, TAH, Diagnose))
        return false;
      continue;
    }

    // C++11 [class.ctor]p5:
    //   A default constructor is trivial if [...]
    //    -- no non-static data member of its class has a
    //       brace-or-equal-initializer
    if (CSM == CXXSpecialMemberKind::DefaultConstructor &&
        FI->hasInClassInitializer()) {
      if (Diagnose)
        S.Diag(FI->getLocation(), diag::note_nontrivial_default_member_init)
            << FI;
      return false;
    }

    // Objective C ARC 4.3.5:
    //   [...] nontrivally ownership-qualified types are [...] not trivially
    //   default constructible, copy constructible, move constructible, copy
    //   assignable, move assignable, or destructible [...]
    if (FieldType.hasNonTrivialObjCLifetime()) {
      if (Diagnose)
        S.Diag(FI->getLocation(), diag::note_nontrivial_objc_ownership)
          << RD << FieldType.getObjCLifetime();
      return false;
    }

    bool ConstRHS = ConstArg && !FI->isMutable();
    if (!checkTrivialSubobjectCall(S, FI->getLocation(), FieldType, ConstRHS,
                                   CSM, TSK_Field, TAH, Diagnose))
      return false;
  }

  return true;
}

void Sema::DiagnoseNontrivial(const CXXRecordDecl *RD,
                              CXXSpecialMemberKind CSM) {
  QualType Ty = Context.getRecordType(RD);

  bool ConstArg = (CSM == CXXSpecialMemberKind::CopyConstructor ||
                   CSM == CXXSpecialMemberKind::CopyAssignment);
  checkTrivialSubobjectCall(*this, RD->getLocation(), Ty, ConstArg, CSM,
                            TSK_CompleteObject, TAH_IgnoreTrivialABI,
                            /*Diagnose*/true);
}

bool Sema::SpecialMemberIsTrivial(CXXMethodDecl *MD, CXXSpecialMemberKind CSM,
                                  TrivialABIHandling TAH, bool Diagnose) {
  assert(!MD->isUserProvided() && CSM != CXXSpecialMemberKind::Invalid &&
         "not special enough");

  CXXRecordDecl *RD = MD->getParent();

  bool ConstArg = false;

  // C++11 [class.copy]p12, p25: [DR1593]
  //   A [special member] is trivial if [...] its parameter-type-list is
  //   equivalent to the parameter-type-list of an implicit declaration [...]
  switch (CSM) {
  case CXXSpecialMemberKind::DefaultConstructor:
  case CXXSpecialMemberKind::Destructor:
    // Trivial default constructors and destructors cannot have parameters.
    break;

  case CXXSpecialMemberKind::CopyConstructor:
  case CXXSpecialMemberKind::CopyAssignment: {
    const ParmVarDecl *Param0 = MD->getNonObjectParameter(0);
    const ReferenceType *RT = Param0->getType()->getAs<ReferenceType>();

    // When ClangABICompat14 is true, CXX copy constructors will only be trivial
    // if they are not user-provided and their parameter-type-list is equivalent
    // to the parameter-type-list of an implicit declaration. This maintains the
    // behavior before dr2171 was implemented.
    //
    // Otherwise, if ClangABICompat14 is false, All copy constructors can be
    // trivial, if they are not user-provided, regardless of the qualifiers on
    // the reference type.
    const bool ClangABICompat14 = Context.getLangOpts().getClangABICompat() <=
                                  LangOptions::ClangABI::Ver14;
    if (!RT ||
        ((RT->getPointeeType().getCVRQualifiers() != Qualifiers::Const) &&
         ClangABICompat14)) {
      if (Diagnose)
        Diag(Param0->getLocation(), diag::note_nontrivial_param_type)
          << Param0->getSourceRange() << Param0->getType()
          << Context.getLValueReferenceType(
               Context.getRecordType(RD).withConst());
      return false;
    }

    ConstArg = RT->getPointeeType().isConstQualified();
    break;
  }

  case CXXSpecialMemberKind::MoveConstructor:
  case CXXSpecialMemberKind::MoveAssignment: {
    // Trivial move operations always have non-cv-qualified parameters.
    const ParmVarDecl *Param0 = MD->getNonObjectParameter(0);
    const RValueReferenceType *RT =
      Param0->getType()->getAs<RValueReferenceType>();
    if (!RT || RT->getPointeeType().getCVRQualifiers()) {
      if (Diagnose)
        Diag(Param0->getLocation(), diag::note_nontrivial_param_type)
          << Param0->getSourceRange() << Param0->getType()
          << Context.getRValueReferenceType(Context.getRecordType(RD));
      return false;
    }
    break;
  }

  case CXXSpecialMemberKind::Invalid:
    llvm_unreachable("not a special member");
  }

  if (MD->getMinRequiredArguments() < MD->getNumParams()) {
    if (Diagnose)
      Diag(MD->getParamDecl(MD->getMinRequiredArguments())->getLocation(),
           diag::note_nontrivial_default_arg)
        << MD->getParamDecl(MD->getMinRequiredArguments())->getSourceRange();
    return false;
  }
  if (MD->isVariadic()) {
    if (Diagnose)
      Diag(MD->getLocation(), diag::note_nontrivial_variadic);
    return false;
  }

  // C++11 [class.ctor]p5, C++11 [class.dtor]p5:
  //   A copy/move [constructor or assignment operator] is trivial if
  //    -- the [member] selected to copy/move each direct base class subobject
  //       is trivial
  //
  // C++11 [class.copy]p12, C++11 [class.copy]p25:
  //   A [default constructor or destructor] is trivial if
  //    -- all the direct base classes have trivial [default constructors or
  //       destructors]
  for (const auto &BI : RD->bases())
    if (!checkTrivialSubobjectCall(*this, BI.getBeginLoc(), BI.getType(),
                                   ConstArg, CSM, TSK_BaseClass, TAH, Diagnose))
      return false;

  // C++11 [class.ctor]p5, C++11 [class.dtor]p5:
  //   A copy/move [constructor or assignment operator] for a class X is
  //   trivial if
  //    -- for each non-static data member of X that is of class type (or array
  //       thereof), the constructor selected to copy/move that member is
  //       trivial
  //
  // C++11 [class.copy]p12, C++11 [class.copy]p25:
  //   A [default constructor or destructor] is trivial if
  //    -- for all of the non-static data members of its class that are of class
  //       type (or array thereof), each such class has a trivial [default
  //       constructor or destructor]
  if (!checkTrivialClassMembers(*this, RD, CSM, ConstArg, TAH, Diagnose))
    return false;

  // C++11 [class.dtor]p5:
  //   A destructor is trivial if [...]
  //    -- the destructor is not virtual
  if (CSM == CXXSpecialMemberKind::Destructor && MD->isVirtual()) {
    if (Diagnose)
      Diag(MD->getLocation(), diag::note_nontrivial_virtual_dtor) << RD;
    return false;
  }

  // C++11 [class.ctor]p5, C++11 [class.copy]p12, C++11 [class.copy]p25:
  //   A [special member] for class X is trivial if [...]
  //    -- class X has no virtual functions and no virtual base classes
  if (CSM != CXXSpecialMemberKind::Destructor &&
      MD->getParent()->isDynamicClass()) {
    if (!Diagnose)
      return false;

    if (RD->getNumVBases()) {
      // Check for virtual bases. We already know that the corresponding
      // member in all bases is trivial, so vbases must all be direct.
      CXXBaseSpecifier &BS = *RD->vbases_begin();
      assert(BS.isVirtual());
      Diag(BS.getBeginLoc(), diag::note_nontrivial_has_virtual) << RD << 1;
      return false;
    }

    // Must have a virtual method.
    for (const auto *MI : RD->methods()) {
      if (MI->isVirtual()) {
        SourceLocation MLoc = MI->getBeginLoc();
        Diag(MLoc, diag::note_nontrivial_has_virtual) << RD << 0;
        return false;
      }
    }

    llvm_unreachable("dynamic class with no vbases and no virtual functions");
  }

  // Looks like it's trivial!
  return true;
}

namespace {
struct FindHiddenVirtualMethod {
  Sema *S;
  CXXMethodDecl *Method;
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> OverridenAndUsingBaseMethods;
  SmallVector<CXXMethodDecl *, 8> OverloadedMethods;

private:
  /// Check whether any most overridden method from MD in Methods
  static bool CheckMostOverridenMethods(
      const CXXMethodDecl *MD,
      const llvm::SmallPtrSetImpl<const CXXMethodDecl *> &Methods) {
    if (MD->size_overridden_methods() == 0)
      return Methods.count(MD->getCanonicalDecl());
    for (const CXXMethodDecl *O : MD->overridden_methods())
      if (CheckMostOverridenMethods(O, Methods))
        return true;
    return false;
  }

public:
  /// Member lookup function that determines whether a given C++
  /// method overloads virtual methods in a base class without overriding any,
  /// to be used with CXXRecordDecl::lookupInBases().
  bool operator()(const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
    RecordDecl *BaseRecord =
        Specifier->getType()->castAs<RecordType>()->getDecl();

    DeclarationName Name = Method->getDeclName();
    assert(Name.getNameKind() == DeclarationName::Identifier);

    bool foundSameNameMethod = false;
    SmallVector<CXXMethodDecl *, 8> overloadedMethods;
    for (Path.Decls = BaseRecord->lookup(Name).begin();
         Path.Decls != DeclContext::lookup_iterator(); ++Path.Decls) {
      NamedDecl *D = *Path.Decls;
      if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D)) {
        MD = MD->getCanonicalDecl();
        foundSameNameMethod = true;
        // Interested only in hidden virtual methods.
        if (!MD->isVirtual())
          continue;
        // If the method we are checking overrides a method from its base
        // don't warn about the other overloaded methods. Clang deviates from
        // GCC by only diagnosing overloads of inherited virtual functions that
        // do not override any other virtual functions in the base. GCC's
        // -Woverloaded-virtual diagnoses any derived function hiding a virtual
        // function from a base class. These cases may be better served by a
        // warning (not specific to virtual functions) on call sites when the
        // call would select a different function from the base class, were it
        // visible.
        // See FIXME in test/SemaCXX/warn-overload-virtual.cpp for an example.
        if (!S->IsOverload(Method, MD, false))
          return true;
        // Collect the overload only if its hidden.
        if (!CheckMostOverridenMethods(MD, OverridenAndUsingBaseMethods))
          overloadedMethods.push_back(MD);
      }
    }

    if (foundSameNameMethod)
      OverloadedMethods.append(overloadedMethods.begin(),
                               overloadedMethods.end());
    return foundSameNameMethod;
  }
};
} // end anonymous namespace

/// Add the most overridden methods from MD to Methods
static void AddMostOverridenMethods(const CXXMethodDecl *MD,
                        llvm::SmallPtrSetImpl<const CXXMethodDecl *>& Methods) {
  if (MD->size_overridden_methods() == 0)
    Methods.insert(MD->getCanonicalDecl());
  else
    for (const CXXMethodDecl *O : MD->overridden_methods())
      AddMostOverridenMethods(O, Methods);
}

void Sema::FindHiddenVirtualMethods(CXXMethodDecl *MD,
                          SmallVectorImpl<CXXMethodDecl*> &OverloadedMethods) {
  if (!MD->getDeclName().isIdentifier())
    return;

  CXXBasePaths Paths(/*FindAmbiguities=*/true, // true to look in all bases.
                     /*bool RecordPaths=*/false,
                     /*bool DetectVirtual=*/false);
  FindHiddenVirtualMethod FHVM;
  FHVM.Method = MD;
  FHVM.S = this;

  // Keep the base methods that were overridden or introduced in the subclass
  // by 'using' in a set. A base method not in this set is hidden.
  CXXRecordDecl *DC = MD->getParent();
  DeclContext::lookup_result R = DC->lookup(MD->getDeclName());
  for (DeclContext::lookup_iterator I = R.begin(), E = R.end(); I != E; ++I) {
    NamedDecl *ND = *I;
    if (UsingShadowDecl *shad = dyn_cast<UsingShadowDecl>(*I))
      ND = shad->getTargetDecl();
    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(ND))
      AddMostOverridenMethods(MD, FHVM.OverridenAndUsingBaseMethods);
  }

  if (DC->lookupInBases(FHVM, Paths))
    OverloadedMethods = FHVM.OverloadedMethods;
}

void Sema::NoteHiddenVirtualMethods(CXXMethodDecl *MD,
                          SmallVectorImpl<CXXMethodDecl*> &OverloadedMethods) {
  for (unsigned i = 0, e = OverloadedMethods.size(); i != e; ++i) {
    CXXMethodDecl *overloadedMD = OverloadedMethods[i];
    PartialDiagnostic PD = PDiag(
         diag::note_hidden_overloaded_virtual_declared_here) << overloadedMD;
    HandleFunctionTypeMismatch(PD, MD->getType(), overloadedMD->getType());
    Diag(overloadedMD->getLocation(), PD);
  }
}

void Sema::DiagnoseHiddenVirtualMethods(CXXMethodDecl *MD) {
  if (MD->isInvalidDecl())
    return;

  if (Diags.isIgnored(diag::warn_overloaded_virtual, MD->getLocation()))
    return;

  SmallVector<CXXMethodDecl *, 8> OverloadedMethods;
  FindHiddenVirtualMethods(MD, OverloadedMethods);
  if (!OverloadedMethods.empty()) {
    Diag(MD->getLocation(), diag::warn_overloaded_virtual)
      << MD << (OverloadedMethods.size() > 1);

    NoteHiddenVirtualMethods(MD, OverloadedMethods);
  }
}

void Sema::checkIllFormedTrivialABIStruct(CXXRecordDecl &RD) {
  auto PrintDiagAndRemoveAttr = [&](unsigned N) {
    // No diagnostics if this is a template instantiation.
    if (!isTemplateInstantiation(RD.getTemplateSpecializationKind())) {
      Diag(RD.getAttr<TrivialABIAttr>()->getLocation(),
           diag::ext_cannot_use_trivial_abi) << &RD;
      Diag(RD.getAttr<TrivialABIAttr>()->getLocation(),
           diag::note_cannot_use_trivial_abi_reason) << &RD << N;
    }
    RD.dropAttr<TrivialABIAttr>();
  };

  // Ill-formed if the copy and move constructors are deleted.
  auto HasNonDeletedCopyOrMoveConstructor = [&]() {
    // If the type is dependent, then assume it might have
    // implicit copy or move ctor because we won't know yet at this point.
    if (RD.isDependentType())
      return true;
    if (RD.needsImplicitCopyConstructor() &&
        !RD.defaultedCopyConstructorIsDeleted())
      return true;
    if (RD.needsImplicitMoveConstructor() &&
        !RD.defaultedMoveConstructorIsDeleted())
      return true;
    for (const CXXConstructorDecl *CD : RD.ctors())
      if (CD->isCopyOrMoveConstructor() && !CD->isDeleted())
        return true;
    return false;
  };

  if (!HasNonDeletedCopyOrMoveConstructor()) {
    PrintDiagAndRemoveAttr(0);
    return;
  }

  // Ill-formed if the struct has virtual functions.
  if (RD.isPolymorphic()) {
    PrintDiagAndRemoveAttr(1);
    return;
  }

  for (const auto &B : RD.bases()) {
    // Ill-formed if the base class is non-trivial for the purpose of calls or a
    // virtual base.
    if (!B.getType()->isDependentType() &&
        !B.getType()->getAsCXXRecordDecl()->canPassInRegisters()) {
      PrintDiagAndRemoveAttr(2);
      return;
    }

    if (B.isVirtual()) {
      PrintDiagAndRemoveAttr(3);
      return;
    }
  }

  for (const auto *FD : RD.fields()) {
    // Ill-formed if the field is an ObjectiveC pointer or of a type that is
    // non-trivial for the purpose of calls.
    QualType FT = FD->getType();
    if (FT.getObjCLifetime() == Qualifiers::OCL_Weak) {
      PrintDiagAndRemoveAttr(4);
      return;
    }

    if (const auto *RT = FT->getBaseElementTypeUnsafe()->getAs<RecordType>())
      if (!RT->isDependentType() &&
          !cast<CXXRecordDecl>(RT->getDecl())->canPassInRegisters()) {
        PrintDiagAndRemoveAttr(5);
        return;
      }
  }
}

void Sema::checkIncorrectVTablePointerAuthenticationAttribute(
    CXXRecordDecl &RD) {
  if (RequireCompleteType(RD.getLocation(), Context.getRecordType(&RD),
                          diag::err_incomplete_type_vtable_pointer_auth))
    return;

  const CXXRecordDecl *PrimaryBase = &RD;
  if (PrimaryBase->hasAnyDependentBases())
    return;

  while (1) {
    assert(PrimaryBase);
    const CXXRecordDecl *Base = nullptr;
    for (auto BasePtr : PrimaryBase->bases()) {
      if (!BasePtr.getType()->getAsCXXRecordDecl()->isDynamicClass())
        continue;
      Base = BasePtr.getType()->getAsCXXRecordDecl();
      break;
    }
    if (!Base || Base == PrimaryBase || !Base->isPolymorphic())
      break;
    Diag(RD.getAttr<VTablePointerAuthenticationAttr>()->getLocation(),
         diag::err_non_top_level_vtable_pointer_auth)
        << &RD << Base;
    PrimaryBase = Base;
  }

  if (!RD.isPolymorphic())
    Diag(RD.getAttr<VTablePointerAuthenticationAttr>()->getLocation(),
         diag::err_non_polymorphic_vtable_pointer_auth)
        << &RD;
}

void Sema::ActOnFinishCXXMemberSpecification(
    Scope *S, SourceLocation RLoc, Decl *TagDecl, SourceLocation LBrac,
    SourceLocation RBrac, const ParsedAttributesView &AttrList) {
  if (!TagDecl)
    return;

  AdjustDeclIfTemplate(TagDecl);

  for (const ParsedAttr &AL : AttrList) {
    if (AL.getKind() != ParsedAttr::AT_Visibility)
      continue;
    AL.setInvalid();
    Diag(AL.getLoc(), diag::warn_attribute_after_definition_ignored) << AL;
  }

  ActOnFields(S, RLoc, TagDecl,
              llvm::ArrayRef(
                  // strict aliasing violation!
                  reinterpret_cast<Decl **>(FieldCollector->getCurFields()),
                  FieldCollector->getCurNumFields()),
              LBrac, RBrac, AttrList);

  CheckCompletedCXXClass(S, cast<CXXRecordDecl>(TagDecl));
}

/// Find the equality comparison functions that should be implicitly declared
/// in a given class definition, per C++2a [class.compare.default]p3.
static void findImplicitlyDeclaredEqualityComparisons(
    ASTContext &Ctx, CXXRecordDecl *RD,
    llvm::SmallVectorImpl<FunctionDecl *> &Spaceships) {
  DeclarationName EqEq = Ctx.DeclarationNames.getCXXOperatorName(OO_EqualEqual);
  if (!RD->lookup(EqEq).empty())
    // Member operator== explicitly declared: no implicit operator==s.
    return;

  // Traverse friends looking for an '==' or a '<=>'.
  for (FriendDecl *Friend : RD->friends()) {
    FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(Friend->getFriendDecl());
    if (!FD) continue;

    if (FD->getOverloadedOperator() == OO_EqualEqual) {
      // Friend operator== explicitly declared: no implicit operator==s.
      Spaceships.clear();
      return;
    }

    if (FD->getOverloadedOperator() == OO_Spaceship &&
        FD->isExplicitlyDefaulted())
      Spaceships.push_back(FD);
  }

  // Look for members named 'operator<=>'.
  DeclarationName Cmp = Ctx.DeclarationNames.getCXXOperatorName(OO_Spaceship);
  for (NamedDecl *ND : RD->lookup(Cmp)) {
    // Note that we could find a non-function here (either a function template
    // or a using-declaration). Neither case results in an implicit
    // 'operator=='.
    if (auto *FD = dyn_cast<FunctionDecl>(ND))
      if (FD->isExplicitlyDefaulted())
        Spaceships.push_back(FD);
  }
}

void Sema::AddImplicitlyDeclaredMembersToClass(CXXRecordDecl *ClassDecl) {
  // Don't add implicit special members to templated classes.
  // FIXME: This means unqualified lookups for 'operator=' within a class
  // template don't work properly.
  if (!ClassDecl->isDependentType()) {
    if (ClassDecl->needsImplicitDefaultConstructor()) {
      ++getASTContext().NumImplicitDefaultConstructors;

      if (ClassDecl->hasInheritedConstructor())
        DeclareImplicitDefaultConstructor(ClassDecl);
    }

    if (ClassDecl->needsImplicitCopyConstructor()) {
      ++getASTContext().NumImplicitCopyConstructors;

      // If the properties or semantics of the copy constructor couldn't be
      // determined while the class was being declared, force a declaration
      // of it now.
      if (ClassDecl->needsOverloadResolutionForCopyConstructor() ||
          ClassDecl->hasInheritedConstructor())
        DeclareImplicitCopyConstructor(ClassDecl);
      // For the MS ABI we need to know whether the copy ctor is deleted. A
      // prerequisite for deleting the implicit copy ctor is that the class has
      // a move ctor or move assignment that is either user-declared or whose
      // semantics are inherited from a subobject. FIXME: We should provide a
      // more direct way for CodeGen to ask whether the constructor was deleted.
      else if (Context.getTargetInfo().getCXXABI().isMicrosoft() &&
               (ClassDecl->hasUserDeclaredMoveConstructor() ||
                ClassDecl->needsOverloadResolutionForMoveConstructor() ||
                ClassDecl->hasUserDeclaredMoveAssignment() ||
                ClassDecl->needsOverloadResolutionForMoveAssignment()))
        DeclareImplicitCopyConstructor(ClassDecl);
    }

    if (getLangOpts().CPlusPlus11 &&
        ClassDecl->needsImplicitMoveConstructor()) {
      ++getASTContext().NumImplicitMoveConstructors;

      if (ClassDecl->needsOverloadResolutionForMoveConstructor() ||
          ClassDecl->hasInheritedConstructor())
        DeclareImplicitMoveConstructor(ClassDecl);
    }

    if (ClassDecl->needsImplicitCopyAssignment()) {
      ++getASTContext().NumImplicitCopyAssignmentOperators;

      // If we have a dynamic class, then the copy assignment operator may be
      // virtual, so we have to declare it immediately. This ensures that, e.g.,
      // it shows up in the right place in the vtable and that we diagnose
      // problems with the implicit exception specification.
      if (ClassDecl->isDynamicClass() ||
          ClassDecl->needsOverloadResolutionForCopyAssignment() ||
          ClassDecl->hasInheritedAssignment())
        DeclareImplicitCopyAssignment(ClassDecl);
    }

    if (getLangOpts().CPlusPlus11 && ClassDecl->needsImplicitMoveAssignment()) {
      ++getASTContext().NumImplicitMoveAssignmentOperators;

      // Likewise for the move assignment operator.
      if (ClassDecl->isDynamicClass() ||
          ClassDecl->needsOverloadResolutionForMoveAssignment() ||
          ClassDecl->hasInheritedAssignment())
        DeclareImplicitMoveAssignment(ClassDecl);
    }

    if (ClassDecl->needsImplicitDestructor()) {
      ++getASTContext().NumImplicitDestructors;

      // If we have a dynamic class, then the destructor may be virtual, so we
      // have to declare the destructor immediately. This ensures that, e.g., it
      // shows up in the right place in the vtable and that we diagnose problems
      // with the implicit exception specification.
      if (ClassDecl->isDynamicClass() ||
          ClassDecl->needsOverloadResolutionForDestructor())
        DeclareImplicitDestructor(ClassDecl);
    }
  }

  // C++2a [class.compare.default]p3:
  //   If the member-specification does not explicitly declare any member or
  //   friend named operator==, an == operator function is declared implicitly
  //   for each defaulted three-way comparison operator function defined in
  //   the member-specification
  // FIXME: Consider doing this lazily.
  // We do this during the initial parse for a class template, not during
  // instantiation, so that we can handle unqualified lookups for 'operator=='
  // when parsing the template.
  if (getLangOpts().CPlusPlus20 && !inTemplateInstantiation()) {
    llvm::SmallVector<FunctionDecl *, 4> DefaultedSpaceships;
    findImplicitlyDeclaredEqualityComparisons(Context, ClassDecl,
                                              DefaultedSpaceships);
    for (auto *FD : DefaultedSpaceships)
      DeclareImplicitEqualityComparison(ClassDecl, FD);
  }
}

unsigned
Sema::ActOnReenterTemplateScope(Decl *D,
                                llvm::function_ref<Scope *()> EnterScope) {
  if (!D)
    return 0;
  AdjustDeclIfTemplate(D);

  // In order to get name lookup right, reenter template scopes in order from
  // outermost to innermost.
  SmallVector<TemplateParameterList *, 4> ParameterLists;
  DeclContext *LookupDC = dyn_cast<DeclContext>(D);

  if (DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
    for (unsigned i = 0; i < DD->getNumTemplateParameterLists(); ++i)
      ParameterLists.push_back(DD->getTemplateParameterList(i));

    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      if (FunctionTemplateDecl *FTD = FD->getDescribedFunctionTemplate())
        ParameterLists.push_back(FTD->getTemplateParameters());
    } else if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
      LookupDC = VD->getDeclContext();

      if (VarTemplateDecl *VTD = VD->getDescribedVarTemplate())
        ParameterLists.push_back(VTD->getTemplateParameters());
      else if (auto *PSD = dyn_cast<VarTemplatePartialSpecializationDecl>(D))
        ParameterLists.push_back(PSD->getTemplateParameters());
    }
  } else if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
    for (unsigned i = 0; i < TD->getNumTemplateParameterLists(); ++i)
      ParameterLists.push_back(TD->getTemplateParameterList(i));

    if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(TD)) {
      if (ClassTemplateDecl *CTD = RD->getDescribedClassTemplate())
        ParameterLists.push_back(CTD->getTemplateParameters());
      else if (auto *PSD = dyn_cast<ClassTemplatePartialSpecializationDecl>(D))
        ParameterLists.push_back(PSD->getTemplateParameters());
    }
  }
  // FIXME: Alias declarations and concepts.

  unsigned Count = 0;
  Scope *InnermostTemplateScope = nullptr;
  for (TemplateParameterList *Params : ParameterLists) {
    // Ignore explicit specializations; they don't contribute to the template
    // depth.
    if (Params->size() == 0)
      continue;

    InnermostTemplateScope = EnterScope();
    for (NamedDecl *Param : *Params) {
      if (Param->getDeclName()) {
        InnermostTemplateScope->AddDecl(Param);
        IdResolver.AddDecl(Param);
      }
    }
    ++Count;
  }

  // Associate the new template scopes with the corresponding entities.
  if (InnermostTemplateScope) {
    assert(LookupDC && "no enclosing DeclContext for template lookup");
    EnterTemplatedContext(InnermostTemplateScope, LookupDC);
  }

  return Count;
}

void Sema::ActOnStartDelayedMemberDeclarations(Scope *S, Decl *RecordD) {
  if (!RecordD) return;
  AdjustDeclIfTemplate(RecordD);
  CXXRecordDecl *Record = cast<CXXRecordDecl>(RecordD);
  PushDeclContext(S, Record);
}

void Sema::ActOnFinishDelayedMemberDeclarations(Scope *S, Decl *RecordD) {
  if (!RecordD) return;
  PopDeclContext();
}

void Sema::ActOnReenterCXXMethodParameter(Scope *S, ParmVarDecl *Param) {
  if (!Param)
    return;

  S->AddDecl(Param);
  if (Param->getDeclName())
    IdResolver.AddDecl(Param);
}

void Sema::ActOnStartDelayedCXXMethodDeclaration(Scope *S, Decl *MethodD) {
}

/// ActOnDelayedCXXMethodParameter - We've already started a delayed
/// C++ method declaration. We're (re-)introducing the given
/// function parameter into scope for use in parsing later parts of
/// the method declaration. For example, we could see an
/// ActOnParamDefaultArgument event for this parameter.
void Sema::ActOnDelayedCXXMethodParameter(Scope *S, Decl *ParamD) {
  if (!ParamD)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(ParamD);

  S->AddDecl(Param);
  if (Param->getDeclName())
    IdResolver.AddDecl(Param);
}

void Sema::ActOnFinishDelayedCXXMethodDeclaration(Scope *S, Decl *MethodD) {
  if (!MethodD)
    return;

  AdjustDeclIfTemplate(MethodD);

  FunctionDecl *Method = cast<FunctionDecl>(MethodD);

  // Now that we have our default arguments, check the constructor
  // again. It could produce additional diagnostics or affect whether
  // the class has implicitly-declared destructors, among other
  // things.
  if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(Method))
    CheckConstructor(Constructor);

  // Check the default arguments, which we may have added.
  if (!Method->isInvalidDecl())
    CheckCXXDefaultArguments(Method);
}

// Emit the given diagnostic for each non-address-space qualifier.
// Common part of CheckConstructorDeclarator and CheckDestructorDeclarator.
static void checkMethodTypeQualifiers(Sema &S, Declarator &D, unsigned DiagID) {
  const DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.hasMethodTypeQualifiers() && !D.isInvalidType()) {
    bool DiagOccured = false;
    FTI.MethodQualifiers->forEachQualifier(
        [DiagID, &S, &DiagOccured](DeclSpec::TQ, StringRef QualName,
                                   SourceLocation SL) {
          // This diagnostic should be emitted on any qualifier except an addr
          // space qualifier. However, forEachQualifier currently doesn't visit
          // addr space qualifiers, so there's no way to write this condition
          // right now; we just diagnose on everything.
          S.Diag(SL, DiagID) << QualName << SourceRange(SL);
          DiagOccured = true;
        });
    if (DiagOccured)
      D.setInvalidType();
  }
}

QualType Sema::CheckConstructorDeclarator(Declarator &D, QualType R,
                                          StorageClass &SC) {
  bool isVirtual = D.getDeclSpec().isVirtualSpecified();

  // C++ [class.ctor]p3:
  //   A constructor shall not be virtual (10.3) or static (9.4). A
  //   constructor can be invoked for a const, volatile or const
  //   volatile object. A constructor shall not be declared const,
  //   volatile, or const volatile (9.3.2).
  if (isVirtual) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_constructor_cannot_be)
        << "virtual" << SourceRange(D.getDeclSpec().getVirtualSpecLoc())
        << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  }
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_constructor_cannot_be)
        << "static" << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
    SC = SC_None;
  }

  if (unsigned TypeQuals = D.getDeclSpec().getTypeQualifiers()) {
    diagnoseIgnoredQualifiers(
        diag::err_constructor_return_type, TypeQuals, SourceLocation(),
        D.getDeclSpec().getConstSpecLoc(), D.getDeclSpec().getVolatileSpecLoc(),
        D.getDeclSpec().getRestrictSpecLoc(),
        D.getDeclSpec().getAtomicSpecLoc());
    D.setInvalidType();
  }

  checkMethodTypeQualifiers(*this, D, diag::err_invalid_qualified_constructor);

  // C++0x [class.ctor]p4:
  //   A constructor shall not be declared with a ref-qualifier.
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.hasRefQualifier()) {
    Diag(FTI.getRefQualifierLoc(), diag::err_ref_qualifier_constructor)
      << FTI.RefQualifierIsLValueRef
      << FixItHint::CreateRemoval(FTI.getRefQualifierLoc());
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any type qualifiers (in
  // case any of the errors above fired) and with "void" as the
  // return type, since constructors don't have return types.
  const FunctionProtoType *Proto = R->castAs<FunctionProtoType>();
  if (Proto->getReturnType() == Context.VoidTy && !D.isInvalidType())
    return R;

  FunctionProtoType::ExtProtoInfo EPI = Proto->getExtProtoInfo();
  EPI.TypeQuals = Qualifiers();
  EPI.RefQualifier = RQ_None;

  return Context.getFunctionType(Context.VoidTy, Proto->getParamTypes(), EPI);
}

void Sema::CheckConstructor(CXXConstructorDecl *Constructor) {
  CXXRecordDecl *ClassDecl
    = dyn_cast<CXXRecordDecl>(Constructor->getDeclContext());
  if (!ClassDecl)
    return Constructor->setInvalidDecl();

  // C++ [class.copy]p3:
  //   A declaration of a constructor for a class X is ill-formed if
  //   its first parameter is of type (optionally cv-qualified) X and
  //   either there are no other parameters or else all other
  //   parameters have default arguments.
  if (!Constructor->isInvalidDecl() &&
      Constructor->hasOneParamOrDefaultArgs() &&
      Constructor->getTemplateSpecializationKind() !=
          TSK_ImplicitInstantiation) {
    QualType ParamType = Constructor->getParamDecl(0)->getType();
    QualType ClassTy = Context.getTagDeclType(ClassDecl);
    if (Context.getCanonicalType(ParamType).getUnqualifiedType() == ClassTy) {
      SourceLocation ParamLoc = Constructor->getParamDecl(0)->getLocation();
      const char *ConstRef
        = Constructor->getParamDecl(0)->getIdentifier() ? "const &"
                                                        : " const &";
      Diag(ParamLoc, diag::err_constructor_byvalue_arg)
        << FixItHint::CreateInsertion(ParamLoc, ConstRef);

      // FIXME: Rather that making the constructor invalid, we should endeavor
      // to fix the type.
      Constructor->setInvalidDecl();
    }
  }
}

bool Sema::CheckDestructor(CXXDestructorDecl *Destructor) {
  CXXRecordDecl *RD = Destructor->getParent();

  if (!Destructor->getOperatorDelete() && Destructor->isVirtual()) {
    SourceLocation Loc;

    if (!Destructor->isImplicit())
      Loc = Destructor->getLocation();
    else
      Loc = RD->getLocation();

    // If we have a virtual destructor, look up the deallocation function
    if (FunctionDecl *OperatorDelete =
            FindDeallocationFunctionForDestructor(Loc, RD)) {
      Expr *ThisArg = nullptr;

      // If the notional 'delete this' expression requires a non-trivial
      // conversion from 'this' to the type of a destroying operator delete's
      // first parameter, perform that conversion now.
      if (OperatorDelete->isDestroyingOperatorDelete()) {
        QualType ParamType = OperatorDelete->getParamDecl(0)->getType();
        if (!declaresSameEntity(ParamType->getAsCXXRecordDecl(), RD)) {
          // C++ [class.dtor]p13:
          //   ... as if for the expression 'delete this' appearing in a
          //   non-virtual destructor of the destructor's class.
          ContextRAII SwitchContext(*this, Destructor);
          ExprResult This =
              ActOnCXXThis(OperatorDelete->getParamDecl(0)->getLocation());
          assert(!This.isInvalid() && "couldn't form 'this' expr in dtor?");
          This = PerformImplicitConversion(This.get(), ParamType, AA_Passing);
          if (This.isInvalid()) {
            // FIXME: Register this as a context note so that it comes out
            // in the right order.
            Diag(Loc, diag::note_implicit_delete_this_in_destructor_here);
            return true;
          }
          ThisArg = This.get();
        }
      }

      DiagnoseUseOfDecl(OperatorDelete, Loc);
      MarkFunctionReferenced(Loc, OperatorDelete);
      Destructor->setOperatorDelete(OperatorDelete, ThisArg);
    }
  }

  return false;
}

QualType Sema::CheckDestructorDeclarator(Declarator &D, QualType R,
                                         StorageClass& SC) {
  // C++ [class.dtor]p1:
  //   [...] A typedef-name that names a class is a class-name
  //   (7.1.3); however, a typedef-name that names a class shall not
  //   be used as the identifier in the declarator for a destructor
  //   declaration.
  QualType DeclaratorType = GetTypeFromParser(D.getName().DestructorName);
  if (const TypedefType *TT = DeclaratorType->getAs<TypedefType>())
    Diag(D.getIdentifierLoc(), diag::ext_destructor_typedef_name)
      << DeclaratorType << isa<TypeAliasDecl>(TT->getDecl());
  else if (const TemplateSpecializationType *TST =
             DeclaratorType->getAs<TemplateSpecializationType>())
    if (TST->isTypeAlias())
      Diag(D.getIdentifierLoc(), diag::ext_destructor_typedef_name)
        << DeclaratorType << 1;

  // C++ [class.dtor]p2:
  //   A destructor is used to destroy objects of its class type. A
  //   destructor takes no parameters, and no return type can be
  //   specified for it (not even void). The address of a destructor
  //   shall not be taken. A destructor shall not be static. A
  //   destructor can be invoked for a const, volatile or const
  //   volatile object. A destructor shall not be declared const,
  //   volatile or const volatile (9.3.2).
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_destructor_cannot_be)
        << "static" << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << SourceRange(D.getIdentifierLoc())
        << FixItHint::CreateRemoval(D.getDeclSpec().getStorageClassSpecLoc());

    SC = SC_None;
  }
  if (!D.isInvalidType()) {
    // Destructors don't have return types, but the parser will
    // happily parse something like:
    //
    //   class X {
    //     float ~X();
    //   };
    //
    // The return type will be eliminated later.
    if (D.getDeclSpec().hasTypeSpecifier())
      Diag(D.getIdentifierLoc(), diag::err_destructor_return_type)
        << SourceRange(D.getDeclSpec().getTypeSpecTypeLoc())
        << SourceRange(D.getIdentifierLoc());
    else if (unsigned TypeQuals = D.getDeclSpec().getTypeQualifiers()) {
      diagnoseIgnoredQualifiers(diag::err_destructor_return_type, TypeQuals,
                                SourceLocation(),
                                D.getDeclSpec().getConstSpecLoc(),
                                D.getDeclSpec().getVolatileSpecLoc(),
                                D.getDeclSpec().getRestrictSpecLoc(),
                                D.getDeclSpec().getAtomicSpecLoc());
      D.setInvalidType();
    }
  }

  checkMethodTypeQualifiers(*this, D, diag::err_invalid_qualified_destructor);

  // C++0x [class.dtor]p2:
  //   A destructor shall not be declared with a ref-qualifier.
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.hasRefQualifier()) {
    Diag(FTI.getRefQualifierLoc(), diag::err_ref_qualifier_destructor)
      << FTI.RefQualifierIsLValueRef
      << FixItHint::CreateRemoval(FTI.getRefQualifierLoc());
    D.setInvalidType();
  }

  // Make sure we don't have any parameters.
  if (FTIHasNonVoidParameters(FTI)) {
    Diag(D.getIdentifierLoc(), diag::err_destructor_with_params);

    // Delete the parameters.
    FTI.freeParams();
    D.setInvalidType();
  }

  // Make sure the destructor isn't variadic.
  if (FTI.isVariadic) {
    Diag(D.getIdentifierLoc(), diag::err_destructor_variadic);
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any type qualifiers or
  // parameters (in case any of the errors above fired) and with
  // "void" as the return type, since destructors don't have return
  // types.
  if (!D.isInvalidType())
    return R;

  const FunctionProtoType *Proto = R->castAs<FunctionProtoType>();
  FunctionProtoType::ExtProtoInfo EPI = Proto->getExtProtoInfo();
  EPI.Variadic = false;
  EPI.TypeQuals = Qualifiers();
  EPI.RefQualifier = RQ_None;
  return Context.getFunctionType(Context.VoidTy, std::nullopt, EPI);
}

static void extendLeft(SourceRange &R, SourceRange Before) {
  if (Before.isInvalid())
    return;
  R.setBegin(Before.getBegin());
  if (R.getEnd().isInvalid())
    R.setEnd(Before.getEnd());
}

static void extendRight(SourceRange &R, SourceRange After) {
  if (After.isInvalid())
    return;
  if (R.getBegin().isInvalid())
    R.setBegin(After.getBegin());
  R.setEnd(After.getEnd());
}

void Sema::CheckConversionDeclarator(Declarator &D, QualType &R,
                                     StorageClass& SC) {
  // C++ [class.conv.fct]p1:
  //   Neither parameter types nor return type can be specified. The
  //   type of a conversion function (8.3.5) is "function taking no
  //   parameter returning conversion-type-id."
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_conv_function_not_member)
        << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << D.getName().getSourceRange();
    D.setInvalidType();
    SC = SC_None;
  }

  TypeSourceInfo *ConvTSI = nullptr;
  QualType ConvType =
      GetTypeFromParser(D.getName().ConversionFunctionId, &ConvTSI);

  const DeclSpec &DS = D.getDeclSpec();
  if (DS.hasTypeSpecifier() && !D.isInvalidType()) {
    // Conversion functions don't have return types, but the parser will
    // happily parse something like:
    //
    //   class X {
    //     float operator bool();
    //   };
    //
    // The return type will be changed later anyway.
    Diag(D.getIdentifierLoc(), diag::err_conv_function_return_type)
      << SourceRange(DS.getTypeSpecTypeLoc())
      << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  } else if (DS.getTypeQualifiers() && !D.isInvalidType()) {
    // It's also plausible that the user writes type qualifiers in the wrong
    // place, such as:
    //   struct S { const operator int(); };
    // FIXME: we could provide a fixit to move the qualifiers onto the
    // conversion type.
    Diag(D.getIdentifierLoc(), diag::err_conv_function_with_complex_decl)
        << SourceRange(D.getIdentifierLoc()) << 0;
    D.setInvalidType();
  }
  const auto *Proto = R->castAs<FunctionProtoType>();
  // Make sure we don't have any parameters.
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  unsigned NumParam = Proto->getNumParams();

  // [C++2b]
  // A conversion function shall have no non-object parameters.
  if (NumParam == 1) {
    DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
    if (const auto *First =
            dyn_cast_if_present<ParmVarDecl>(FTI.Params[0].Param);
        First && First->isExplicitObjectParameter())
      NumParam--;
  }

  if (NumParam != 0) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_with_params);
    // Delete the parameters.
    FTI.freeParams();
    D.setInvalidType();
  } else if (Proto->isVariadic()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_variadic);
    D.setInvalidType();
  }

  // Diagnose "&operator bool()" and other such nonsense.  This
  // is actually a gcc extension which we don't support.
  if (Proto->getReturnType() != ConvType) {
    bool NeedsTypedef = false;
    SourceRange Before, After;

    // Walk the chunks and extract information on them for our diagnostic.
    bool PastFunctionChunk = false;
    for (auto &Chunk : D.type_objects()) {
      switch (Chunk.Kind) {
      case DeclaratorChunk::Function:
        if (!PastFunctionChunk) {
          if (Chunk.Fun.HasTrailingReturnType) {
            TypeSourceInfo *TRT = nullptr;
            GetTypeFromParser(Chunk.Fun.getTrailingReturnType(), &TRT);
            if (TRT) extendRight(After, TRT->getTypeLoc().getSourceRange());
          }
          PastFunctionChunk = true;
          break;
        }
        [[fallthrough]];
      case DeclaratorChunk::Array:
        NeedsTypedef = true;
        extendRight(After, Chunk.getSourceRange());
        break;

      case DeclaratorChunk::Pointer:
      case DeclaratorChunk::BlockPointer:
      case DeclaratorChunk::Reference:
      case DeclaratorChunk::MemberPointer:
      case DeclaratorChunk::Pipe:
        extendLeft(Before, Chunk.getSourceRange());
        break;

      case DeclaratorChunk::Paren:
        extendLeft(Before, Chunk.Loc);
        extendRight(After, Chunk.EndLoc);
        break;
      }
    }

    SourceLocation Loc = Before.isValid() ? Before.getBegin() :
                         After.isValid()  ? After.getBegin() :
                                            D.getIdentifierLoc();
    auto &&DB = Diag(Loc, diag::err_conv_function_with_complex_decl);
    DB << Before << After;

    if (!NeedsTypedef) {
      DB << /*don't need a typedef*/0;

      // If we can provide a correct fix-it hint, do so.
      if (After.isInvalid() && ConvTSI) {
        SourceLocation InsertLoc =
            getLocForEndOfToken(ConvTSI->getTypeLoc().getEndLoc());
        DB << FixItHint::CreateInsertion(InsertLoc, " ")
           << FixItHint::CreateInsertionFromRange(
                  InsertLoc, CharSourceRange::getTokenRange(Before))
           << FixItHint::CreateRemoval(Before);
      }
    } else if (!Proto->getReturnType()->isDependentType()) {
      DB << /*typedef*/1 << Proto->getReturnType();
    } else if (getLangOpts().CPlusPlus11) {
      DB << /*alias template*/2 << Proto->getReturnType();
    } else {
      DB << /*might not be fixable*/3;
    }

    // Recover by incorporating the other type chunks into the result type.
    // Note, this does *not* change the name of the function. This is compatible
    // with the GCC extension:
    //   struct S { &operator int(); } s;
    //   int &r = s.operator int(); // ok in GCC
    //   S::operator int&() {} // error in GCC, function name is 'operator int'.
    ConvType = Proto->getReturnType();
  }

  // C++ [class.conv.fct]p4:
  //   The conversion-type-id shall not represent a function type nor
  //   an array type.
  if (ConvType->isArrayType()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_to_array);
    ConvType = Context.getPointerType(ConvType);
    D.setInvalidType();
  } else if (ConvType->isFunctionType()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_to_function);
    ConvType = Context.getPointerType(ConvType);
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any parameters (in case any
  // of the errors above fired) and with the conversion type as the
  // return type.
  if (D.isInvalidType())
    R = Context.getFunctionType(ConvType, std::nullopt,
                                Proto->getExtProtoInfo());

  // C++0x explicit conversion operators.
  if (DS.hasExplicitSpecifier() && !getLangOpts().CPlusPlus20)
    Diag(DS.getExplicitSpecLoc(),
         getLangOpts().CPlusPlus11
             ? diag::warn_cxx98_compat_explicit_conversion_functions
             : diag::ext_explicit_conversion_functions)
        << SourceRange(DS.getExplicitSpecRange());
}

Decl *Sema::ActOnConversionDeclarator(CXXConversionDecl *Conversion) {
  assert(Conversion && "Expected to receive a conversion function declaration");

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(Conversion->getDeclContext());

  // Make sure we aren't redeclaring the conversion function.
  QualType ConvType = Context.getCanonicalType(Conversion->getConversionType());
  // C++ [class.conv.fct]p1:
  //   [...] A conversion function is never used to convert a
  //   (possibly cv-qualified) object to the (possibly cv-qualified)
  //   same object type (or a reference to it), to a (possibly
  //   cv-qualified) base class of that type (or a reference to it),
  //   or to (possibly cv-qualified) void.
  QualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  if (const ReferenceType *ConvTypeRef = ConvType->getAs<ReferenceType>())
    ConvType = ConvTypeRef->getPointeeType();
  if (Conversion->getTemplateSpecializationKind() != TSK_Undeclared &&
      Conversion->getTemplateSpecializationKind() != TSK_ExplicitSpecialization)
    /* Suppress diagnostics for instantiations. */;
  else if (Conversion->size_overridden_methods() != 0)
    /* Suppress diagnostics for overriding virtual function in a base class. */;
  else if (ConvType->isRecordType()) {
    ConvType = Context.getCanonicalType(ConvType).getUnqualifiedType();
    if (ConvType == ClassType)
      Diag(Conversion->getLocation(), diag::warn_conv_to_self_not_used)
        << ClassType;
    else if (IsDerivedFrom(Conversion->getLocation(), ClassType, ConvType))
      Diag(Conversion->getLocation(), diag::warn_conv_to_base_not_used)
        <<  ClassType << ConvType;
  } else if (ConvType->isVoidType()) {
    Diag(Conversion->getLocation(), diag::warn_conv_to_void_not_used)
      << ClassType << ConvType;
  }

  if (FunctionTemplateDecl *ConversionTemplate =
          Conversion->getDescribedFunctionTemplate()) {
    if (const auto *ConvTypePtr = ConvType->getAs<PointerType>()) {
      ConvType = ConvTypePtr->getPointeeType();
    }
    if (ConvType->isUndeducedAutoType()) {
      Diag(Conversion->getTypeSpecStartLoc(), diag::err_auto_not_allowed)
          << getReturnTypeLoc(Conversion).getSourceRange()
          << llvm::to_underlying(ConvType->castAs<AutoType>()->getKeyword())
          << /* in declaration of conversion function template= */ 24;
    }

    return ConversionTemplate;
  }

  return Conversion;
}

void Sema::CheckExplicitObjectMemberFunction(DeclContext *DC, Declarator &D,
                                             DeclarationName Name, QualType R) {
  CheckExplicitObjectMemberFunction(D, Name, R, false, DC);
}

void Sema::CheckExplicitObjectLambda(Declarator &D) {
  CheckExplicitObjectMemberFunction(D, {}, {}, true);
}

void Sema::CheckExplicitObjectMemberFunction(Declarator &D,
                                             DeclarationName Name, QualType R,
                                             bool IsLambda, DeclContext *DC) {
  if (!D.isFunctionDeclarator())
    return;

  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.NumParams == 0)
    return;
  ParmVarDecl *ExplicitObjectParam = nullptr;
  for (unsigned Idx = 0; Idx < FTI.NumParams; Idx++) {
    const auto &ParamInfo = FTI.Params[Idx];
    if (!ParamInfo.Param)
      continue;
    ParmVarDecl *Param = cast<ParmVarDecl>(ParamInfo.Param);
    if (!Param->isExplicitObjectParameter())
      continue;
    if (Idx == 0) {
      ExplicitObjectParam = Param;
      continue;
    } else {
      Diag(Param->getLocation(),
           diag::err_explicit_object_parameter_must_be_first)
          << IsLambda << Param->getSourceRange();
    }
  }
  if (!ExplicitObjectParam)
    return;

  if (ExplicitObjectParam->hasDefaultArg()) {
    Diag(ExplicitObjectParam->getLocation(),
         diag::err_explicit_object_default_arg)
        << ExplicitObjectParam->getSourceRange();
  }

  if (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_static ||
      (D.getContext() == clang::DeclaratorContext::Member &&
       D.isStaticMember())) {
    Diag(ExplicitObjectParam->getBeginLoc(),
         diag::err_explicit_object_parameter_nonmember)
        << D.getSourceRange() << /*static=*/0 << IsLambda;
    D.setInvalidType();
  }

  if (D.getDeclSpec().isVirtualSpecified()) {
    Diag(ExplicitObjectParam->getBeginLoc(),
         diag::err_explicit_object_parameter_nonmember)
        << D.getSourceRange() << /*virtual=*/1 << IsLambda;
    D.setInvalidType();
  }

  // Friend declarations require some care. Consider:
  //
  // namespace N {
  // struct A{};
  // int f(A);
  // }
  //
  // struct S {
  //   struct T {
  //     int f(this T);
  //   };
  //
  //   friend int T::f(this T); // Allow this.
  //   friend int f(this S);    // But disallow this.
  //   friend int N::f(this A); // And disallow this.
  // };
  //
  // Here, it seems to suffice to check whether the scope
  // specifier designates a class type.
  if (D.getDeclSpec().isFriendSpecified() &&
      !isa_and_present<CXXRecordDecl>(
          computeDeclContext(D.getCXXScopeSpec()))) {
    Diag(ExplicitObjectParam->getBeginLoc(),
         diag::err_explicit_object_parameter_nonmember)
        << D.getSourceRange() << /*non-member=*/2 << IsLambda;
    D.setInvalidType();
  }

  if (IsLambda && FTI.hasMutableQualifier()) {
    Diag(ExplicitObjectParam->getBeginLoc(),
         diag::err_explicit_object_parameter_mutable)
        << D.getSourceRange();
  }

  if (IsLambda)
    return;

  if (!DC || !DC->isRecord()) {
    assert(D.isInvalidType() && "Explicit object parameter in non-member "
                                "should have been diagnosed already");
    return;
  }

  // CWG2674: constructors and destructors cannot have explicit parameters.
  if (Name.getNameKind() == DeclarationName::CXXConstructorName ||
      Name.getNameKind() == DeclarationName::CXXDestructorName) {
    Diag(ExplicitObjectParam->getBeginLoc(),
         diag::err_explicit_object_parameter_constructor)
        << (Name.getNameKind() == DeclarationName::CXXDestructorName)
        << D.getSourceRange();
    D.setInvalidType();
  }
}

namespace {
/// Utility class to accumulate and print a diagnostic listing the invalid
/// specifier(s) on a declaration.
struct BadSpecifierDiagnoser {
  BadSpecifierDiagnoser(Sema &S, SourceLocation Loc, unsigned DiagID)
      : S(S), Diagnostic(S.Diag(Loc, DiagID)) {}
  ~BadSpecifierDiagnoser() {
    Diagnostic << Specifiers;
  }

  template<typename T> void check(SourceLocation SpecLoc, T Spec) {
    return check(SpecLoc, DeclSpec::getSpecifierName(Spec));
  }
  void check(SourceLocation SpecLoc, DeclSpec::TST Spec) {
    return check(SpecLoc,
                 DeclSpec::getSpecifierName(Spec, S.getPrintingPolicy()));
  }
  void check(SourceLocation SpecLoc, const char *Spec) {
    if (SpecLoc.isInvalid()) return;
    Diagnostic << SourceRange(SpecLoc, SpecLoc);
    if (!Specifiers.empty()) Specifiers += " ";
    Specifiers += Spec;
  }

  Sema &S;
  Sema::SemaDiagnosticBuilder Diagnostic;
  std::string Specifiers;
};
}

bool Sema::CheckDeductionGuideDeclarator(Declarator &D, QualType &R,
                                         StorageClass &SC) {
  TemplateName GuidedTemplate = D.getName().TemplateName.get().get();
  TemplateDecl *GuidedTemplateDecl = GuidedTemplate.getAsTemplateDecl();
  assert(GuidedTemplateDecl && "missing template decl for deduction guide");

  // C++ [temp.deduct.guide]p3:
  //   A deduction-gide shall be declared in the same scope as the
  //   corresponding class template.
  if (!CurContext->getRedeclContext()->Equals(
          GuidedTemplateDecl->getDeclContext()->getRedeclContext())) {
    Diag(D.getIdentifierLoc(), diag::err_deduction_guide_wrong_scope)
      << GuidedTemplateDecl;
    NoteTemplateLocation(*GuidedTemplateDecl);
  }

  auto &DS = D.getMutableDeclSpec();
  // We leave 'friend' and 'virtual' to be rejected in the normal way.
  if (DS.hasTypeSpecifier() || DS.getTypeQualifiers() ||
      DS.getStorageClassSpecLoc().isValid() || DS.isInlineSpecified() ||
      DS.isNoreturnSpecified() || DS.hasConstexprSpecifier()) {
    BadSpecifierDiagnoser Diagnoser(
        *this, D.getIdentifierLoc(),
        diag::err_deduction_guide_invalid_specifier);

    Diagnoser.check(DS.getStorageClassSpecLoc(), DS.getStorageClassSpec());
    DS.ClearStorageClassSpecs();
    SC = SC_None;

    // 'explicit' is permitted.
    Diagnoser.check(DS.getInlineSpecLoc(), "inline");
    Diagnoser.check(DS.getNoreturnSpecLoc(), "_Noreturn");
    Diagnoser.check(DS.getConstexprSpecLoc(), "constexpr");
    DS.ClearConstexprSpec();

    Diagnoser.check(DS.getConstSpecLoc(), "const");
    Diagnoser.check(DS.getRestrictSpecLoc(), "__restrict");
    Diagnoser.check(DS.getVolatileSpecLoc(), "volatile");
    Diagnoser.check(DS.getAtomicSpecLoc(), "_Atomic");
    Diagnoser.check(DS.getUnalignedSpecLoc(), "__unaligned");
    DS.ClearTypeQualifiers();

    Diagnoser.check(DS.getTypeSpecComplexLoc(), DS.getTypeSpecComplex());
    Diagnoser.check(DS.getTypeSpecSignLoc(), DS.getTypeSpecSign());
    Diagnoser.check(DS.getTypeSpecWidthLoc(), DS.getTypeSpecWidth());
    Diagnoser.check(DS.getTypeSpecTypeLoc(), DS.getTypeSpecType());
    DS.ClearTypeSpecType();
  }

  if (D.isInvalidType())
    return true;

  // Check the declarator is simple enough.
  bool FoundFunction = false;
  for (const DeclaratorChunk &Chunk : llvm::reverse(D.type_objects())) {
    if (Chunk.Kind == DeclaratorChunk::Paren)
      continue;
    if (Chunk.Kind != DeclaratorChunk::Function || FoundFunction) {
      Diag(D.getDeclSpec().getBeginLoc(),
           diag::err_deduction_guide_with_complex_decl)
          << D.getSourceRange();
      break;
    }
    if (!Chunk.Fun.hasTrailingReturnType())
      return Diag(D.getName().getBeginLoc(),
                  diag::err_deduction_guide_no_trailing_return_type);

    // Check that the return type is written as a specialization of
    // the template specified as the deduction-guide's name.
    // The template name may not be qualified. [temp.deduct.guide]
    ParsedType TrailingReturnType = Chunk.Fun.getTrailingReturnType();
    TypeSourceInfo *TSI = nullptr;
    QualType RetTy = GetTypeFromParser(TrailingReturnType, &TSI);
    assert(TSI && "deduction guide has valid type but invalid return type?");
    bool AcceptableReturnType = false;
    bool MightInstantiateToSpecialization = false;
    if (auto RetTST =
            TSI->getTypeLoc().getAsAdjusted<TemplateSpecializationTypeLoc>()) {
      TemplateName SpecifiedName = RetTST.getTypePtr()->getTemplateName();
      bool TemplateMatches =
          Context.hasSameTemplateName(SpecifiedName, GuidedTemplate);

      const QualifiedTemplateName *Qualifiers =
          SpecifiedName.getAsQualifiedTemplateName();
      assert(Qualifiers && "expected QualifiedTemplate");
      bool SimplyWritten = !Qualifiers->hasTemplateKeyword() &&
                           Qualifiers->getQualifier() == nullptr;
      if (SimplyWritten && TemplateMatches)
        AcceptableReturnType = true;
      else {
        // This could still instantiate to the right type, unless we know it
        // names the wrong class template.
        auto *TD = SpecifiedName.getAsTemplateDecl();
        MightInstantiateToSpecialization = !(TD && isa<ClassTemplateDecl>(TD) &&
                                             !TemplateMatches);
      }
    } else if (!RetTy.hasQualifiers() && RetTy->isDependentType()) {
      MightInstantiateToSpecialization = true;
    }

    if (!AcceptableReturnType)
      return Diag(TSI->getTypeLoc().getBeginLoc(),
                  diag::err_deduction_guide_bad_trailing_return_type)
             << GuidedTemplate << TSI->getType()
             << MightInstantiateToSpecialization
             << TSI->getTypeLoc().getSourceRange();

    // Keep going to check that we don't have any inner declarator pieces (we
    // could still have a function returning a pointer to a function).
    FoundFunction = true;
  }

  if (D.isFunctionDefinition())
    // we can still create a valid deduction guide here.
    Diag(D.getIdentifierLoc(), diag::err_deduction_guide_defines_function);
  return false;
}

//===----------------------------------------------------------------------===//
// Namespace Handling
//===----------------------------------------------------------------------===//

/// Diagnose a mismatch in 'inline' qualifiers when a namespace is
/// reopened.
static void DiagnoseNamespaceInlineMismatch(Sema &S, SourceLocation KeywordLoc,
                                            SourceLocation Loc,
                                            IdentifierInfo *II, bool *IsInline,
                                            NamespaceDecl *PrevNS) {
  assert(*IsInline != PrevNS->isInline());

  // 'inline' must appear on the original definition, but not necessarily
  // on all extension definitions, so the note should point to the first
  // definition to avoid confusion.
  PrevNS = PrevNS->getFirstDecl();

  if (PrevNS->isInline())
    // The user probably just forgot the 'inline', so suggest that it
    // be added back.
    S.Diag(Loc, diag::warn_inline_namespace_reopened_noninline)
      << FixItHint::CreateInsertion(KeywordLoc, "inline ");
  else
    S.Diag(Loc, diag::err_inline_namespace_mismatch);

  S.Diag(PrevNS->getLocation(), diag::note_previous_definition);
  *IsInline = PrevNS->isInline();
}

/// ActOnStartNamespaceDef - This is called at the start of a namespace
/// definition.
Decl *Sema::ActOnStartNamespaceDef(Scope *NamespcScope,
                                   SourceLocation InlineLoc,
                                   SourceLocation NamespaceLoc,
                                   SourceLocation IdentLoc, IdentifierInfo *II,
                                   SourceLocation LBrace,
                                   const ParsedAttributesView &AttrList,
                                   UsingDirectiveDecl *&UD, bool IsNested) {
  SourceLocation StartLoc = InlineLoc.isValid() ? InlineLoc : NamespaceLoc;
  // For anonymous namespace, take the location of the left brace.
  SourceLocation Loc = II ? IdentLoc : LBrace;
  bool IsInline = InlineLoc.isValid();
  bool IsInvalid = false;
  bool IsStd = false;
  bool AddToKnown = false;
  Scope *DeclRegionScope = NamespcScope->getParent();

  NamespaceDecl *PrevNS = nullptr;
  if (II) {
    // C++ [namespace.std]p7:
    //   A translation unit shall not declare namespace std to be an inline
    //   namespace (9.8.2).
    //
    // Precondition: the std namespace is in the file scope and is declared to
    // be inline
    auto DiagnoseInlineStdNS = [&]() {
      assert(IsInline && II->isStr("std") &&
             CurContext->getRedeclContext()->isTranslationUnit() &&
             "Precondition of DiagnoseInlineStdNS not met");
      Diag(InlineLoc, diag::err_inline_namespace_std)
          << SourceRange(InlineLoc, InlineLoc.getLocWithOffset(6));
      IsInline = false;
    };
    // C++ [namespace.def]p2:
    //   The identifier in an original-namespace-definition shall not
    //   have been previously defined in the declarative region in
    //   which the original-namespace-definition appears. The
    //   identifier in an original-namespace-definition is the name of
    //   the namespace. Subsequently in that declarative region, it is
    //   treated as an original-namespace-name.
    //
    // Since namespace names are unique in their scope, and we don't
    // look through using directives, just look for any ordinary names
    // as if by qualified name lookup.
    LookupResult R(*this, II, IdentLoc, LookupOrdinaryName,
                   RedeclarationKind::ForExternalRedeclaration);
    LookupQualifiedName(R, CurContext->getRedeclContext());
    NamedDecl *PrevDecl =
        R.isSingleResult() ? R.getRepresentativeDecl() : nullptr;
    PrevNS = dyn_cast_or_null<NamespaceDecl>(PrevDecl);

    if (PrevNS) {
      // This is an extended namespace definition.
      if (IsInline && II->isStr("std") &&
          CurContext->getRedeclContext()->isTranslationUnit())
        DiagnoseInlineStdNS();
      else if (IsInline != PrevNS->isInline())
        DiagnoseNamespaceInlineMismatch(*this, NamespaceLoc, Loc, II,
                                        &IsInline, PrevNS);
    } else if (PrevDecl) {
      // This is an invalid name redefinition.
      Diag(Loc, diag::err_redefinition_different_kind)
        << II;
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      IsInvalid = true;
      // Continue on to push Namespc as current DeclContext and return it.
    } else if (II->isStr("std") &&
               CurContext->getRedeclContext()->isTranslationUnit()) {
      if (IsInline)
        DiagnoseInlineStdNS();
      // This is the first "real" definition of the namespace "std", so update
      // our cache of the "std" namespace to point at this definition.
      PrevNS = getStdNamespace();
      IsStd = true;
      AddToKnown = !IsInline;
    } else {
      // We've seen this namespace for the first time.
      AddToKnown = !IsInline;
    }
  } else {
    // Anonymous namespaces.

    // Determine whether the parent already has an anonymous namespace.
    DeclContext *Parent = CurContext->getRedeclContext();
    if (TranslationUnitDecl *TU = dyn_cast<TranslationUnitDecl>(Parent)) {
      PrevNS = TU->getAnonymousNamespace();
    } else {
      NamespaceDecl *ND = cast<NamespaceDecl>(Parent);
      PrevNS = ND->getAnonymousNamespace();
    }

    if (PrevNS && IsInline != PrevNS->isInline())
      DiagnoseNamespaceInlineMismatch(*this, NamespaceLoc, NamespaceLoc, II,
                                      &IsInline, PrevNS);
  }

  NamespaceDecl *Namespc = NamespaceDecl::Create(
      Context, CurContext, IsInline, StartLoc, Loc, II, PrevNS, IsNested);
  if (IsInvalid)
    Namespc->setInvalidDecl();

  ProcessDeclAttributeList(DeclRegionScope, Namespc, AttrList);
  AddPragmaAttributes(DeclRegionScope, Namespc);
  ProcessAPINotes(Namespc);

  // FIXME: Should we be merging attributes?
  if (const VisibilityAttr *Attr = Namespc->getAttr<VisibilityAttr>())
    PushNamespaceVisibilityAttr(Attr, Loc);

  if (IsStd)
    StdNamespace = Namespc;
  if (AddToKnown)
    KnownNamespaces[Namespc] = false;

  if (II) {
    PushOnScopeChains(Namespc, DeclRegionScope);
  } else {
    // Link the anonymous namespace into its parent.
    DeclContext *Parent = CurContext->getRedeclContext();
    if (TranslationUnitDecl *TU = dyn_cast<TranslationUnitDecl>(Parent)) {
      TU->setAnonymousNamespace(Namespc);
    } else {
      cast<NamespaceDecl>(Parent)->setAnonymousNamespace(Namespc);
    }

    CurContext->addDecl(Namespc);

    // C++ [namespace.unnamed]p1.  An unnamed-namespace-definition
    //   behaves as if it were replaced by
    //     namespace unique { /* empty body */ }
    //     using namespace unique;
    //     namespace unique { namespace-body }
    //   where all occurrences of 'unique' in a translation unit are
    //   replaced by the same identifier and this identifier differs
    //   from all other identifiers in the entire program.

    // We just create the namespace with an empty name and then add an
    // implicit using declaration, just like the standard suggests.
    //
    // CodeGen enforces the "universally unique" aspect by giving all
    // declarations semantically contained within an anonymous
    // namespace internal linkage.

    if (!PrevNS) {
      UD = UsingDirectiveDecl::Create(Context, Parent,
                                      /* 'using' */ LBrace,
                                      /* 'namespace' */ SourceLocation(),
                                      /* qualifier */ NestedNameSpecifierLoc(),
                                      /* identifier */ SourceLocation(),
                                      Namespc,
                                      /* Ancestor */ Parent);
      UD->setImplicit();
      Parent->addDecl(UD);
    }
  }

  ActOnDocumentableDecl(Namespc);

  // Although we could have an invalid decl (i.e. the namespace name is a
  // redefinition), push it as current DeclContext and try to continue parsing.
  // FIXME: We should be able to push Namespc here, so that the each DeclContext
  // for the namespace has the declarations that showed up in that particular
  // namespace definition.
  PushDeclContext(NamespcScope, Namespc);
  return Namespc;
}

/// getNamespaceDecl - Returns the namespace a decl represents. If the decl
/// is a namespace alias, returns the namespace it points to.
static inline NamespaceDecl *getNamespaceDecl(NamedDecl *D) {
  if (NamespaceAliasDecl *AD = dyn_cast_or_null<NamespaceAliasDecl>(D))
    return AD->getNamespace();
  return dyn_cast_or_null<NamespaceDecl>(D);
}

void Sema::ActOnFinishNamespaceDef(Decl *Dcl, SourceLocation RBrace) {
  NamespaceDecl *Namespc = dyn_cast_or_null<NamespaceDecl>(Dcl);
  assert(Namespc && "Invalid parameter, expected NamespaceDecl");
  Namespc->setRBraceLoc(RBrace);
  PopDeclContext();
  if (Namespc->hasAttr<VisibilityAttr>())
    PopPragmaVisibility(true, RBrace);
  // If this namespace contains an export-declaration, export it now.
  if (DeferredExportedNamespaces.erase(Namespc))
    Dcl->setModuleOwnershipKind(Decl::ModuleOwnershipKind::VisibleWhenImported);
}

CXXRecordDecl *Sema::getStdBadAlloc() const {
  return cast_or_null<CXXRecordDecl>(
                                  StdBadAlloc.get(Context.getExternalSource()));
}

EnumDecl *Sema::getStdAlignValT() const {
  return cast_or_null<EnumDecl>(StdAlignValT.get(Context.getExternalSource()));
}

NamespaceDecl *Sema::getStdNamespace() const {
  return cast_or_null<NamespaceDecl>(
                                 StdNamespace.get(Context.getExternalSource()));
}
namespace {

enum UnsupportedSTLSelect {
  USS_InvalidMember,
  USS_MissingMember,
  USS_NonTrivial,
  USS_Other
};

struct InvalidSTLDiagnoser {
  Sema &S;
  SourceLocation Loc;
  QualType TyForDiags;

  QualType operator()(UnsupportedSTLSelect Sel = USS_Other, StringRef Name = "",
                      const VarDecl *VD = nullptr) {
    {
      auto D = S.Diag(Loc, diag::err_std_compare_type_not_supported)
               << TyForDiags << ((int)Sel);
      if (Sel == USS_InvalidMember || Sel == USS_MissingMember) {
        assert(!Name.empty());
        D << Name;
      }
    }
    if (Sel == USS_InvalidMember) {
      S.Diag(VD->getLocation(), diag::note_var_declared_here)
          << VD << VD->getSourceRange();
    }
    return QualType();
  }
};
} // namespace

QualType Sema::CheckComparisonCategoryType(ComparisonCategoryType Kind,
                                           SourceLocation Loc,
                                           ComparisonCategoryUsage Usage) {
  assert(getLangOpts().CPlusPlus &&
         "Looking for comparison category type outside of C++.");

  // Use an elaborated type for diagnostics which has a name containing the
  // prepended 'std' namespace but not any inline namespace names.
  auto TyForDiags = [&](ComparisonCategoryInfo *Info) {
    auto *NNS =
        NestedNameSpecifier::Create(Context, nullptr, getStdNamespace());
    return Context.getElaboratedType(ElaboratedTypeKeyword::None, NNS,
                                     Info->getType());
  };

  // Check if we've already successfully checked the comparison category type
  // before. If so, skip checking it again.
  ComparisonCategoryInfo *Info = Context.CompCategories.lookupInfo(Kind);
  if (Info && FullyCheckedComparisonCategories[static_cast<unsigned>(Kind)]) {
    // The only thing we need to check is that the type has a reachable
    // definition in the current context.
    if (RequireCompleteType(Loc, TyForDiags(Info), diag::err_incomplete_type))
      return QualType();

    return Info->getType();
  }

  // If lookup failed
  if (!Info) {
    std::string NameForDiags = "std::";
    NameForDiags += ComparisonCategories::getCategoryString(Kind);
    Diag(Loc, diag::err_implied_comparison_category_type_not_found)
        << NameForDiags << (int)Usage;
    return QualType();
  }

  assert(Info->Kind == Kind);
  assert(Info->Record);

  // Update the Record decl in case we encountered a forward declaration on our
  // first pass. FIXME: This is a bit of a hack.
  if (Info->Record->hasDefinition())
    Info->Record = Info->Record->getDefinition();

  if (RequireCompleteType(Loc, TyForDiags(Info), diag::err_incomplete_type))
    return QualType();

  InvalidSTLDiagnoser UnsupportedSTLError{*this, Loc, TyForDiags(Info)};

  if (!Info->Record->isTriviallyCopyable())
    return UnsupportedSTLError(USS_NonTrivial);

  for (const CXXBaseSpecifier &BaseSpec : Info->Record->bases()) {
    CXXRecordDecl *Base = BaseSpec.getType()->getAsCXXRecordDecl();
    // Tolerate empty base classes.
    if (Base->isEmpty())
      continue;
    // Reject STL implementations which have at least one non-empty base.
    return UnsupportedSTLError();
  }

  // Check that the STL has implemented the types using a single integer field.
  // This expectation allows better codegen for builtin operators. We require:
  //   (1) The class has exactly one field.
  //   (2) The field is an integral or enumeration type.
  auto FIt = Info->Record->field_begin(), FEnd = Info->Record->field_end();
  if (std::distance(FIt, FEnd) != 1 ||
      !FIt->getType()->isIntegralOrEnumerationType()) {
    return UnsupportedSTLError();
  }

  // Build each of the require values and store them in Info.
  for (ComparisonCategoryResult CCR :
       ComparisonCategories::getPossibleResultsForType(Kind)) {
    StringRef MemName = ComparisonCategories::getResultString(CCR);
    ComparisonCategoryInfo::ValueInfo *ValInfo = Info->lookupValueInfo(CCR);

    if (!ValInfo)
      return UnsupportedSTLError(USS_MissingMember, MemName);

    VarDecl *VD = ValInfo->VD;
    assert(VD && "should not be null!");

    // Attempt to diagnose reasons why the STL definition of this type
    // might be foobar, including it failing to be a constant expression.
    // TODO Handle more ways the lookup or result can be invalid.
    if (!VD->isStaticDataMember() ||
        !VD->isUsableInConstantExpressions(Context))
      return UnsupportedSTLError(USS_InvalidMember, MemName, VD);

    // Attempt to evaluate the var decl as a constant expression and extract
    // the value of its first field as a ICE. If this fails, the STL
    // implementation is not supported.
    if (!ValInfo->hasValidIntValue())
      return UnsupportedSTLError();

    MarkVariableReferenced(Loc, VD);
  }

  // We've successfully built the required types and expressions. Update
  // the cache and return the newly cached value.
  FullyCheckedComparisonCategories[static_cast<unsigned>(Kind)] = true;
  return Info->getType();
}

NamespaceDecl *Sema::getOrCreateStdNamespace() {
  if (!StdNamespace) {
    // The "std" namespace has not yet been defined, so build one implicitly.
    StdNamespace = NamespaceDecl::Create(
        Context, Context.getTranslationUnitDecl(),
        /*Inline=*/false, SourceLocation(), SourceLocation(),
        &PP.getIdentifierTable().get("std"),
        /*PrevDecl=*/nullptr, /*Nested=*/false);
    getStdNamespace()->setImplicit(true);
    // We want the created NamespaceDecl to be available for redeclaration
    // lookups, but not for regular name lookups.
    Context.getTranslationUnitDecl()->addDecl(getStdNamespace());
    getStdNamespace()->clearIdentifierNamespace();
  }

  return getStdNamespace();
}

bool Sema::isStdInitializerList(QualType Ty, QualType *Element) {
  assert(getLangOpts().CPlusPlus &&
         "Looking for std::initializer_list outside of C++.");

  // We're looking for implicit instantiations of
  // template <typename E> class std::initializer_list.

  if (!StdNamespace) // If we haven't seen namespace std yet, this can't be it.
    return false;

  ClassTemplateDecl *Template = nullptr;
  const TemplateArgument *Arguments = nullptr;

  if (const RecordType *RT = Ty->getAs<RecordType>()) {

    ClassTemplateSpecializationDecl *Specialization =
        dyn_cast<ClassTemplateSpecializationDecl>(RT->getDecl());
    if (!Specialization)
      return false;

    Template = Specialization->getSpecializedTemplate();
    Arguments = Specialization->getTemplateArgs().data();
  } else {
    const TemplateSpecializationType *TST = nullptr;
    if (auto *ICN = Ty->getAs<InjectedClassNameType>())
      TST = ICN->getInjectedTST();
    else
      TST = Ty->getAs<TemplateSpecializationType>();
    if (TST) {
      Template = dyn_cast_or_null<ClassTemplateDecl>(
          TST->getTemplateName().getAsTemplateDecl());
      Arguments = TST->template_arguments().begin();
    }
  }
  if (!Template)
    return false;

  if (!StdInitializerList) {
    // Haven't recognized std::initializer_list yet, maybe this is it.
    CXXRecordDecl *TemplateClass = Template->getTemplatedDecl();
    if (TemplateClass->getIdentifier() !=
            &PP.getIdentifierTable().get("initializer_list") ||
        !getStdNamespace()->InEnclosingNamespaceSetOf(
            TemplateClass->getNonTransparentDeclContext()))
      return false;
    // This is a template called std::initializer_list, but is it the right
    // template?
    TemplateParameterList *Params = Template->getTemplateParameters();
    if (Params->getMinRequiredArguments() != 1)
      return false;
    if (!isa<TemplateTypeParmDecl>(Params->getParam(0)))
      return false;

    // It's the right template.
    StdInitializerList = Template;
  }

  if (Template->getCanonicalDecl() != StdInitializerList->getCanonicalDecl())
    return false;

  // This is an instance of std::initializer_list. Find the argument type.
  if (Element)
    *Element = Arguments[0].getAsType();
  return true;
}

static ClassTemplateDecl *LookupStdInitializerList(Sema &S, SourceLocation Loc){
  NamespaceDecl *Std = S.getStdNamespace();
  if (!Std) {
    S.Diag(Loc, diag::err_implied_std_initializer_list_not_found);
    return nullptr;
  }

  LookupResult Result(S, &S.PP.getIdentifierTable().get("initializer_list"),
                      Loc, Sema::LookupOrdinaryName);
  if (!S.LookupQualifiedName(Result, Std)) {
    S.Diag(Loc, diag::err_implied_std_initializer_list_not_found);
    return nullptr;
  }
  ClassTemplateDecl *Template = Result.getAsSingle<ClassTemplateDecl>();
  if (!Template) {
    Result.suppressDiagnostics();
    // We found something weird. Complain about the first thing we found.
    NamedDecl *Found = *Result.begin();
    S.Diag(Found->getLocation(), diag::err_malformed_std_initializer_list);
    return nullptr;
  }

  // We found some template called std::initializer_list. Now verify that it's
  // correct.
  TemplateParameterList *Params = Template->getTemplateParameters();
  if (Params->getMinRequiredArguments() != 1 ||
      !isa<TemplateTypeParmDecl>(Params->getParam(0))) {
    S.Diag(Template->getLocation(), diag::err_malformed_std_initializer_list);
    return nullptr;
  }

  return Template;
}

QualType Sema::BuildStdInitializerList(QualType Element, SourceLocation Loc) {
  if (!StdInitializerList) {
    StdInitializerList = LookupStdInitializerList(*this, Loc);
    if (!StdInitializerList)
      return QualType();
  }

  TemplateArgumentListInfo Args(Loc, Loc);
  Args.addArgument(TemplateArgumentLoc(TemplateArgument(Element),
                                       Context.getTrivialTypeSourceInfo(Element,
                                                                        Loc)));
  return Context.getElaboratedType(
      ElaboratedTypeKeyword::None,
      NestedNameSpecifier::Create(Context, nullptr, getStdNamespace()),
      CheckTemplateIdType(TemplateName(StdInitializerList), Loc, Args));
}

bool Sema::isInitListConstructor(const FunctionDecl *Ctor) {
  // C++ [dcl.init.list]p2:
  //   A constructor is an initializer-list constructor if its first parameter
  //   is of type std::initializer_list<E> or reference to possibly cv-qualified
  //   std::initializer_list<E> for some type E, and either there are no other
  //   parameters or else all other parameters have default arguments.
  if (!Ctor->hasOneParamOrDefaultArgs())
    return false;

  QualType ArgType = Ctor->getParamDecl(0)->getType();
  if (const ReferenceType *RT = ArgType->getAs<ReferenceType>())
    ArgType = RT->getPointeeType().getUnqualifiedType();

  return isStdInitializerList(ArgType, nullptr);
}

/// Determine whether a using statement is in a context where it will be
/// apply in all contexts.
static bool IsUsingDirectiveInToplevelContext(DeclContext *CurContext) {
  switch (CurContext->getDeclKind()) {
    case Decl::TranslationUnit:
      return true;
    case Decl::LinkageSpec:
      return IsUsingDirectiveInToplevelContext(CurContext->getParent());
    default:
      return false;
  }
}

namespace {

// Callback to only accept typo corrections that are namespaces.
class NamespaceValidatorCCC final : public CorrectionCandidateCallback {
public:
  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (NamedDecl *ND = candidate.getCorrectionDecl())
      return isa<NamespaceDecl>(ND) || isa<NamespaceAliasDecl>(ND);
    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<NamespaceValidatorCCC>(*this);
  }
};

}

static void DiagnoseInvisibleNamespace(const TypoCorrection &Corrected,
                                       Sema &S) {
  auto *ND = cast<NamespaceDecl>(Corrected.getFoundDecl());
  Module *M = ND->getOwningModule();
  assert(M && "hidden namespace definition not in a module?");

  if (M->isExplicitGlobalModule())
    S.Diag(Corrected.getCorrectionRange().getBegin(),
           diag::err_module_unimported_use_header)
        << (int)Sema::MissingImportKind::Declaration << Corrected.getFoundDecl()
        << /*Header Name*/ false;
  else
    S.Diag(Corrected.getCorrectionRange().getBegin(),
           diag::err_module_unimported_use)
        << (int)Sema::MissingImportKind::Declaration << Corrected.getFoundDecl()
        << M->getTopLevelModuleName();
}

static bool TryNamespaceTypoCorrection(Sema &S, LookupResult &R, Scope *Sc,
                                       CXXScopeSpec &SS,
                                       SourceLocation IdentLoc,
                                       IdentifierInfo *Ident) {
  R.clear();
  NamespaceValidatorCCC CCC{};
  if (TypoCorrection Corrected =
          S.CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), Sc, &SS, CCC,
                        Sema::CTK_ErrorRecovery)) {
    // Generally we find it is confusing more than helpful to diagnose the
    // invisible namespace.
    // See https://github.com/llvm/llvm-project/issues/73893.
    //
    // However, we should diagnose when the users are trying to using an
    // invisible namespace. So we handle the case specially here.
    if (isa_and_nonnull<NamespaceDecl>(Corrected.getFoundDecl()) &&
        Corrected.requiresImport()) {
      DiagnoseInvisibleNamespace(Corrected, S);
    } else if (DeclContext *DC = S.computeDeclContext(SS, false)) {
      std::string CorrectedStr(Corrected.getAsString(S.getLangOpts()));
      bool DroppedSpecifier =
          Corrected.WillReplaceSpecifier() && Ident->getName() == CorrectedStr;
      S.diagnoseTypo(Corrected,
                     S.PDiag(diag::err_using_directive_member_suggest)
                       << Ident << DC << DroppedSpecifier << SS.getRange(),
                     S.PDiag(diag::note_namespace_defined_here));
    } else {
      S.diagnoseTypo(Corrected,
                     S.PDiag(diag::err_using_directive_suggest) << Ident,
                     S.PDiag(diag::note_namespace_defined_here));
    }
    R.addDecl(Corrected.getFoundDecl());
    return true;
  }
  return false;
}

Decl *Sema::ActOnUsingDirective(Scope *S, SourceLocation UsingLoc,
                                SourceLocation NamespcLoc, CXXScopeSpec &SS,
                                SourceLocation IdentLoc,
                                IdentifierInfo *NamespcName,
                                const ParsedAttributesView &AttrList) {
  assert(!SS.isInvalid() && "Invalid CXXScopeSpec.");
  assert(NamespcName && "Invalid NamespcName.");
  assert(IdentLoc.isValid() && "Invalid NamespceName location.");

  // Get the innermost enclosing declaration scope.
  S = S->getDeclParent();

  UsingDirectiveDecl *UDir = nullptr;
  NestedNameSpecifier *Qualifier = nullptr;
  if (SS.isSet())
    Qualifier = SS.getScopeRep();

  // Lookup namespace name.
  LookupResult R(*this, NamespcName, IdentLoc, LookupNamespaceName);
  LookupParsedName(R, S, &SS, /*ObjectType=*/QualType());
  if (R.isAmbiguous())
    return nullptr;

  if (R.empty()) {
    R.clear();
    // Allow "using namespace std;" or "using namespace ::std;" even if
    // "std" hasn't been defined yet, for GCC compatibility.
    if ((!Qualifier || Qualifier->getKind() == NestedNameSpecifier::Global) &&
        NamespcName->isStr("std")) {
      Diag(IdentLoc, diag::ext_using_undefined_std);
      R.addDecl(getOrCreateStdNamespace());
      R.resolveKind();
    }
    // Otherwise, attempt typo correction.
    else TryNamespaceTypoCorrection(*this, R, S, SS, IdentLoc, NamespcName);
  }

  if (!R.empty()) {
    NamedDecl *Named = R.getRepresentativeDecl();
    NamespaceDecl *NS = R.getAsSingle<NamespaceDecl>();
    assert(NS && "expected namespace decl");

    // The use of a nested name specifier may trigger deprecation warnings.
    DiagnoseUseOfDecl(Named, IdentLoc);

    // C++ [namespace.udir]p1:
    //   A using-directive specifies that the names in the nominated
    //   namespace can be used in the scope in which the
    //   using-directive appears after the using-directive. During
    //   unqualified name lookup (3.4.1), the names appear as if they
    //   were declared in the nearest enclosing namespace which
    //   contains both the using-directive and the nominated
    //   namespace. [Note: in this context, "contains" means "contains
    //   directly or indirectly". ]

    // Find enclosing context containing both using-directive and
    // nominated namespace.
    DeclContext *CommonAncestor = NS;
    while (CommonAncestor && !CommonAncestor->Encloses(CurContext))
      CommonAncestor = CommonAncestor->getParent();

    UDir = UsingDirectiveDecl::Create(Context, CurContext, UsingLoc, NamespcLoc,
                                      SS.getWithLocInContext(Context),
                                      IdentLoc, Named, CommonAncestor);

    if (IsUsingDirectiveInToplevelContext(CurContext) &&
        !SourceMgr.isInMainFile(SourceMgr.getExpansionLoc(IdentLoc))) {
      Diag(IdentLoc, diag::warn_using_directive_in_header);
    }

    PushUsingDirective(S, UDir);
  } else {
    Diag(IdentLoc, diag::err_expected_namespace_name) << SS.getRange();
  }

  if (UDir) {
    ProcessDeclAttributeList(S, UDir, AttrList);
    ProcessAPINotes(UDir);
  }

  return UDir;
}

void Sema::PushUsingDirective(Scope *S, UsingDirectiveDecl *UDir) {
  // If the scope has an associated entity and the using directive is at
  // namespace or translation unit scope, add the UsingDirectiveDecl into
  // its lookup structure so qualified name lookup can find it.
  DeclContext *Ctx = S->getEntity();
  if (Ctx && !Ctx->isFunctionOrMethod())
    Ctx->addDecl(UDir);
  else
    // Otherwise, it is at block scope. The using-directives will affect lookup
    // only to the end of the scope.
    S->PushUsingDirective(UDir);
}

Decl *Sema::ActOnUsingDeclaration(Scope *S, AccessSpecifier AS,
                                  SourceLocation UsingLoc,
                                  SourceLocation TypenameLoc, CXXScopeSpec &SS,
                                  UnqualifiedId &Name,
                                  SourceLocation EllipsisLoc,
                                  const ParsedAttributesView &AttrList) {
  assert(S->getFlags() & Scope::DeclScope && "Invalid Scope.");

  if (SS.isEmpty()) {
    Diag(Name.getBeginLoc(), diag::err_using_requires_qualname);
    return nullptr;
  }

  switch (Name.getKind()) {
  case UnqualifiedIdKind::IK_ImplicitSelfParam:
  case UnqualifiedIdKind::IK_Identifier:
  case UnqualifiedIdKind::IK_OperatorFunctionId:
  case UnqualifiedIdKind::IK_LiteralOperatorId:
  case UnqualifiedIdKind::IK_ConversionFunctionId:
    break;

  case UnqualifiedIdKind::IK_ConstructorName:
  case UnqualifiedIdKind::IK_ConstructorTemplateId:
    // C++11 inheriting constructors.
    Diag(Name.getBeginLoc(),
         getLangOpts().CPlusPlus11
             ? diag::warn_cxx98_compat_using_decl_constructor
             : diag::err_using_decl_constructor)
        << SS.getRange();

    if (getLangOpts().CPlusPlus11) break;

    return nullptr;

  case UnqualifiedIdKind::IK_DestructorName:
    Diag(Name.getBeginLoc(), diag::err_using_decl_destructor) << SS.getRange();
    return nullptr;

  case UnqualifiedIdKind::IK_TemplateId:
    Diag(Name.getBeginLoc(), diag::err_using_decl_template_id)
        << SourceRange(Name.TemplateId->LAngleLoc, Name.TemplateId->RAngleLoc);
    return nullptr;

  case UnqualifiedIdKind::IK_DeductionGuideName:
    llvm_unreachable("cannot parse qualified deduction guide name");
  }

  DeclarationNameInfo TargetNameInfo = GetNameFromUnqualifiedId(Name);
  DeclarationName TargetName = TargetNameInfo.getName();
  if (!TargetName)
    return nullptr;

  // Warn about access declarations.
  if (UsingLoc.isInvalid()) {
    Diag(Name.getBeginLoc(), getLangOpts().CPlusPlus11
                                 ? diag::err_access_decl
                                 : diag::warn_access_decl_deprecated)
        << FixItHint::CreateInsertion(SS.getRange().getBegin(), "using ");
  }

  if (EllipsisLoc.isInvalid()) {
    if (DiagnoseUnexpandedParameterPack(SS, UPPC_UsingDeclaration) ||
        DiagnoseUnexpandedParameterPack(TargetNameInfo, UPPC_UsingDeclaration))
      return nullptr;
  } else {
    if (!SS.getScopeRep()->containsUnexpandedParameterPack() &&
        !TargetNameInfo.containsUnexpandedParameterPack()) {
      Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
        << SourceRange(SS.getBeginLoc(), TargetNameInfo.getEndLoc());
      EllipsisLoc = SourceLocation();
    }
  }

  NamedDecl *UD =
      BuildUsingDeclaration(S, AS, UsingLoc, TypenameLoc.isValid(), TypenameLoc,
                            SS, TargetNameInfo, EllipsisLoc, AttrList,
                            /*IsInstantiation*/ false,
                            AttrList.hasAttribute(ParsedAttr::AT_UsingIfExists));
  if (UD)
    PushOnScopeChains(UD, S, /*AddToContext*/ false);

  return UD;
}

Decl *Sema::ActOnUsingEnumDeclaration(Scope *S, AccessSpecifier AS,
                                      SourceLocation UsingLoc,
                                      SourceLocation EnumLoc, SourceRange TyLoc,
                                      const IdentifierInfo &II, ParsedType Ty,
                                      CXXScopeSpec *SS) {
  assert(!SS->isInvalid() && "ScopeSpec is invalid");
  TypeSourceInfo *TSI = nullptr;
  SourceLocation IdentLoc = TyLoc.getBegin();
  QualType EnumTy = GetTypeFromParser(Ty, &TSI);
  if (EnumTy.isNull()) {
    Diag(IdentLoc, SS && isDependentScopeSpecifier(*SS)
                       ? diag::err_using_enum_is_dependent
                       : diag::err_unknown_typename)
        << II.getName()
        << SourceRange(SS ? SS->getBeginLoc() : IdentLoc, TyLoc.getEnd());
    return nullptr;
  }

  if (EnumTy->isDependentType()) {
    Diag(IdentLoc, diag::err_using_enum_is_dependent);
    return nullptr;
  }

  auto *Enum = dyn_cast_if_present<EnumDecl>(EnumTy->getAsTagDecl());
  if (!Enum) {
    Diag(IdentLoc, diag::err_using_enum_not_enum) << EnumTy;
    return nullptr;
  }

  if (auto *Def = Enum->getDefinition())
    Enum = Def;

  if (TSI == nullptr)
    TSI = Context.getTrivialTypeSourceInfo(EnumTy, IdentLoc);

  auto *UD =
      BuildUsingEnumDeclaration(S, AS, UsingLoc, EnumLoc, IdentLoc, TSI, Enum);

  if (UD)
    PushOnScopeChains(UD, S, /*AddToContext*/ false);

  return UD;
}

/// Determine whether a using declaration considers the given
/// declarations as "equivalent", e.g., if they are redeclarations of
/// the same entity or are both typedefs of the same type.
static bool
IsEquivalentForUsingDecl(ASTContext &Context, NamedDecl *D1, NamedDecl *D2) {
  if (D1->getCanonicalDecl() == D2->getCanonicalDecl())
    return true;

  if (TypedefNameDecl *TD1 = dyn_cast<TypedefNameDecl>(D1))
    if (TypedefNameDecl *TD2 = dyn_cast<TypedefNameDecl>(D2))
      return Context.hasSameType(TD1->getUnderlyingType(),
                                 TD2->getUnderlyingType());

  // Two using_if_exists using-declarations are equivalent if both are
  // unresolved.
  if (isa<UnresolvedUsingIfExistsDecl>(D1) &&
      isa<UnresolvedUsingIfExistsDecl>(D2))
    return true;

  return false;
}

bool Sema::CheckUsingShadowDecl(BaseUsingDecl *BUD, NamedDecl *Orig,
                                const LookupResult &Previous,
                                UsingShadowDecl *&PrevShadow) {
  // Diagnose finding a decl which is not from a base class of the
  // current class.  We do this now because there are cases where this
  // function will silently decide not to build a shadow decl, which
  // will pre-empt further diagnostics.
  //
  // We don't need to do this in C++11 because we do the check once on
  // the qualifier.
  //
  // FIXME: diagnose the following if we care enough:
  //   struct A { int foo; };
  //   struct B : A { using A::foo; };
  //   template <class T> struct C : A {};
  //   template <class T> struct D : C<T> { using B::foo; } // <---
  // This is invalid (during instantiation) in C++03 because B::foo
  // resolves to the using decl in B, which is not a base class of D<T>.
  // We can't diagnose it immediately because C<T> is an unknown
  // specialization. The UsingShadowDecl in D<T> then points directly
  // to A::foo, which will look well-formed when we instantiate.
  // The right solution is to not collapse the shadow-decl chain.
  if (!getLangOpts().CPlusPlus11 && CurContext->isRecord())
    if (auto *Using = dyn_cast<UsingDecl>(BUD)) {
      DeclContext *OrigDC = Orig->getDeclContext();

      // Handle enums and anonymous structs.
      if (isa<EnumDecl>(OrigDC))
        OrigDC = OrigDC->getParent();
      CXXRecordDecl *OrigRec = cast<CXXRecordDecl>(OrigDC);
      while (OrigRec->isAnonymousStructOrUnion())
        OrigRec = cast<CXXRecordDecl>(OrigRec->getDeclContext());

      if (cast<CXXRecordDecl>(CurContext)->isProvablyNotDerivedFrom(OrigRec)) {
        if (OrigDC == CurContext) {
          Diag(Using->getLocation(),
               diag::err_using_decl_nested_name_specifier_is_current_class)
              << Using->getQualifierLoc().getSourceRange();
          Diag(Orig->getLocation(), diag::note_using_decl_target);
          Using->setInvalidDecl();
          return true;
        }

        Diag(Using->getQualifierLoc().getBeginLoc(),
             diag::err_using_decl_nested_name_specifier_is_not_base_class)
            << Using->getQualifier() << cast<CXXRecordDecl>(CurContext)
            << Using->getQualifierLoc().getSourceRange();
        Diag(Orig->getLocation(), diag::note_using_decl_target);
        Using->setInvalidDecl();
        return true;
      }
    }

  if (Previous.empty()) return false;

  NamedDecl *Target = Orig;
  if (isa<UsingShadowDecl>(Target))
    Target = cast<UsingShadowDecl>(Target)->getTargetDecl();

  // If the target happens to be one of the previous declarations, we
  // don't have a conflict.
  //
  // FIXME: but we might be increasing its access, in which case we
  // should redeclare it.
  NamedDecl *NonTag = nullptr, *Tag = nullptr;
  bool FoundEquivalentDecl = false;
  for (LookupResult::iterator I = Previous.begin(), E = Previous.end();
         I != E; ++I) {
    NamedDecl *D = (*I)->getUnderlyingDecl();
    // We can have UsingDecls in our Previous results because we use the same
    // LookupResult for checking whether the UsingDecl itself is a valid
    // redeclaration.
    if (isa<UsingDecl>(D) || isa<UsingPackDecl>(D) || isa<UsingEnumDecl>(D))
      continue;

    if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
      // C++ [class.mem]p19:
      //   If T is the name of a class, then [every named member other than
      //   a non-static data member] shall have a name different from T
      if (RD->isInjectedClassName() && !isa<FieldDecl>(Target) &&
          !isa<IndirectFieldDecl>(Target) &&
          !isa<UnresolvedUsingValueDecl>(Target) &&
          DiagnoseClassNameShadow(
              CurContext,
              DeclarationNameInfo(BUD->getDeclName(), BUD->getLocation())))
        return true;
    }

    if (IsEquivalentForUsingDecl(Context, D, Target)) {
      if (UsingShadowDecl *Shadow = dyn_cast<UsingShadowDecl>(*I))
        PrevShadow = Shadow;
      FoundEquivalentDecl = true;
    } else if (isEquivalentInternalLinkageDeclaration(D, Target)) {
      // We don't conflict with an existing using shadow decl of an equivalent
      // declaration, but we're not a redeclaration of it.
      FoundEquivalentDecl = true;
    }

    if (isVisible(D))
      (isa<TagDecl>(D) ? Tag : NonTag) = D;
  }

  if (FoundEquivalentDecl)
    return false;

  // Always emit a diagnostic for a mismatch between an unresolved
  // using_if_exists and a resolved using declaration in either direction.
  if (isa<UnresolvedUsingIfExistsDecl>(Target) !=
      (isa_and_nonnull<UnresolvedUsingIfExistsDecl>(NonTag))) {
    if (!NonTag && !Tag)
      return false;
    Diag(BUD->getLocation(), diag::err_using_decl_conflict);
    Diag(Target->getLocation(), diag::note_using_decl_target);
    Diag((NonTag ? NonTag : Tag)->getLocation(),
         diag::note_using_decl_conflict);
    BUD->setInvalidDecl();
    return true;
  }

  if (FunctionDecl *FD = Target->getAsFunction()) {
    NamedDecl *OldDecl = nullptr;
    switch (CheckOverload(nullptr, FD, Previous, OldDecl,
                          /*IsForUsingDecl*/ true)) {
    case Ovl_Overload:
      return false;

    case Ovl_NonFunction:
      Diag(BUD->getLocation(), diag::err_using_decl_conflict);
      break;

    // We found a decl with the exact signature.
    case Ovl_Match:
      // If we're in a record, we want to hide the target, so we
      // return true (without a diagnostic) to tell the caller not to
      // build a shadow decl.
      if (CurContext->isRecord())
        return true;

      // If we're not in a record, this is an error.
      Diag(BUD->getLocation(), diag::err_using_decl_conflict);
      break;
    }

    Diag(Target->getLocation(), diag::note_using_decl_target);
    Diag(OldDecl->getLocation(), diag::note_using_decl_conflict);
    BUD->setInvalidDecl();
    return true;
  }

  // Target is not a function.

  if (isa<TagDecl>(Target)) {
    // No conflict between a tag and a non-tag.
    if (!Tag) return false;

    Diag(BUD->getLocation(), diag::err_using_decl_conflict);
    Diag(Target->getLocation(), diag::note_using_decl_target);
    Diag(Tag->getLocation(), diag::note_using_decl_conflict);
    BUD->setInvalidDecl();
    return true;
  }

  // No conflict between a tag and a non-tag.
  if (!NonTag) return false;

  Diag(BUD->getLocation(), diag::err_using_decl_conflict);
  Diag(Target->getLocation(), diag::note_using_decl_target);
  Diag(NonTag->getLocation(), diag::note_using_decl_conflict);
  BUD->setInvalidDecl();
  return true;
}

/// Determine whether a direct base class is a virtual base class.
static bool isVirtualDirectBase(CXXRecordDecl *Derived, CXXRecordDecl *Base) {
  if (!Derived->getNumVBases())
    return false;
  for (auto &B : Derived->bases())
    if (B.getType()->getAsCXXRecordDecl() == Base)
      return B.isVirtual();
  llvm_unreachable("not a direct base class");
}

UsingShadowDecl *Sema::BuildUsingShadowDecl(Scope *S, BaseUsingDecl *BUD,
                                            NamedDecl *Orig,
                                            UsingShadowDecl *PrevDecl) {
  // If we resolved to another shadow declaration, just coalesce them.
  NamedDecl *Target = Orig;
  if (isa<UsingShadowDecl>(Target)) {
    Target = cast<UsingShadowDecl>(Target)->getTargetDecl();
    assert(!isa<UsingShadowDecl>(Target) && "nested shadow declaration");
  }

  NamedDecl *NonTemplateTarget = Target;
  if (auto *TargetTD = dyn_cast<TemplateDecl>(Target))
    NonTemplateTarget = TargetTD->getTemplatedDecl();

  UsingShadowDecl *Shadow;
  if (NonTemplateTarget && isa<CXXConstructorDecl>(NonTemplateTarget)) {
    UsingDecl *Using = cast<UsingDecl>(BUD);
    bool IsVirtualBase =
        isVirtualDirectBase(cast<CXXRecordDecl>(CurContext),
                            Using->getQualifier()->getAsRecordDecl());
    Shadow = ConstructorUsingShadowDecl::Create(
        Context, CurContext, Using->getLocation(), Using, Orig, IsVirtualBase);
  } else {
    Shadow = UsingShadowDecl::Create(Context, CurContext, BUD->getLocation(),
                                     Target->getDeclName(), BUD, Target);
  }
  BUD->addShadowDecl(Shadow);

  Shadow->setAccess(BUD->getAccess());
  if (Orig->isInvalidDecl() || BUD->isInvalidDecl())
    Shadow->setInvalidDecl();

  Shadow->setPreviousDecl(PrevDecl);

  if (S)
    PushOnScopeChains(Shadow, S);
  else
    CurContext->addDecl(Shadow);


  return Shadow;
}

void Sema::HideUsingShadowDecl(Scope *S, UsingShadowDecl *Shadow) {
  if (Shadow->getDeclName().getNameKind() ==
        DeclarationName::CXXConversionFunctionName)
    cast<CXXRecordDecl>(Shadow->getDeclContext())->removeConversion(Shadow);

  // Remove it from the DeclContext...
  Shadow->getDeclContext()->removeDecl(Shadow);

  // ...and the scope, if applicable...
  if (S) {
    S->RemoveDecl(Shadow);
    IdResolver.RemoveDecl(Shadow);
  }

  // ...and the using decl.
  Shadow->getIntroducer()->removeShadowDecl(Shadow);

  // TODO: complain somehow if Shadow was used.  It shouldn't
  // be possible for this to happen, because...?
}

/// Find the base specifier for a base class with the given type.
static CXXBaseSpecifier *findDirectBaseWithType(CXXRecordDecl *Derived,
                                                QualType DesiredBase,
                                                bool &AnyDependentBases) {
  // Check whether the named type is a direct base class.
  CanQualType CanonicalDesiredBase = DesiredBase->getCanonicalTypeUnqualified()
    .getUnqualifiedType();
  for (auto &Base : Derived->bases()) {
    CanQualType BaseType = Base.getType()->getCanonicalTypeUnqualified();
    if (CanonicalDesiredBase == BaseType)
      return &Base;
    if (BaseType->isDependentType())
      AnyDependentBases = true;
  }
  return nullptr;
}

namespace {
class UsingValidatorCCC final : public CorrectionCandidateCallback {
public:
  UsingValidatorCCC(bool HasTypenameKeyword, bool IsInstantiation,
                    NestedNameSpecifier *NNS, CXXRecordDecl *RequireMemberOf)
      : HasTypenameKeyword(HasTypenameKeyword),
        IsInstantiation(IsInstantiation), OldNNS(NNS),
        RequireMemberOf(RequireMemberOf) {}

  bool ValidateCandidate(const TypoCorrection &Candidate) override {
    NamedDecl *ND = Candidate.getCorrectionDecl();

    // Keywords are not valid here.
    if (!ND || isa<NamespaceDecl>(ND))
      return false;

    // Completely unqualified names are invalid for a 'using' declaration.
    if (Candidate.WillReplaceSpecifier() && !Candidate.getCorrectionSpecifier())
      return false;

    // FIXME: Don't correct to a name that CheckUsingDeclRedeclaration would
    // reject.

    if (RequireMemberOf) {
      auto *FoundRecord = dyn_cast<CXXRecordDecl>(ND);
      if (FoundRecord && FoundRecord->isInjectedClassName()) {
        // No-one ever wants a using-declaration to name an injected-class-name
        // of a base class, unless they're declaring an inheriting constructor.
        ASTContext &Ctx = ND->getASTContext();
        if (!Ctx.getLangOpts().CPlusPlus11)
          return false;
        QualType FoundType = Ctx.getRecordType(FoundRecord);

        // Check that the injected-class-name is named as a member of its own
        // type; we don't want to suggest 'using Derived::Base;', since that
        // means something else.
        NestedNameSpecifier *Specifier =
            Candidate.WillReplaceSpecifier()
                ? Candidate.getCorrectionSpecifier()
                : OldNNS;
        if (!Specifier->getAsType() ||
            !Ctx.hasSameType(QualType(Specifier->getAsType(), 0), FoundType))
          return false;

        // Check that this inheriting constructor declaration actually names a
        // direct base class of the current class.
        bool AnyDependentBases = false;
        if (!findDirectBaseWithType(RequireMemberOf,
                                    Ctx.getRecordType(FoundRecord),
                                    AnyDependentBases) &&
            !AnyDependentBases)
          return false;
      } else {
        auto *RD = dyn_cast<CXXRecordDecl>(ND->getDeclContext());
        if (!RD || RequireMemberOf->isProvablyNotDerivedFrom(RD))
          return false;

        // FIXME: Check that the base class member is accessible?
      }
    } else {
      auto *FoundRecord = dyn_cast<CXXRecordDecl>(ND);
      if (FoundRecord && FoundRecord->isInjectedClassName())
        return false;
    }

    if (isa<TypeDecl>(ND))
      return HasTypenameKeyword || !IsInstantiation;

    return !HasTypenameKeyword;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<UsingValidatorCCC>(*this);
  }

private:
  bool HasTypenameKeyword;
  bool IsInstantiation;
  NestedNameSpecifier *OldNNS;
  CXXRecordDecl *RequireMemberOf;
};
} // end anonymous namespace

void Sema::FilterUsingLookup(Scope *S, LookupResult &Previous) {
  // It is really dumb that we have to do this.
  LookupResult::Filter F = Previous.makeFilter();
  while (F.hasNext()) {
    NamedDecl *D = F.next();
    if (!isDeclInScope(D, CurContext, S))
      F.erase();
    // If we found a local extern declaration that's not ordinarily visible,
    // and this declaration is being added to a non-block scope, ignore it.
    // We're only checking for scope conflicts here, not also for violations
    // of the linkage rules.
    else if (!CurContext->isFunctionOrMethod() && D->isLocalExternDecl() &&
             !(D->getIdentifierNamespace() & Decl::IDNS_Ordinary))
      F.erase();
  }
  F.done();
}

NamedDecl *Sema::BuildUsingDeclaration(
    Scope *S, AccessSpecifier AS, SourceLocation UsingLoc,
    bool HasTypenameKeyword, SourceLocation TypenameLoc, CXXScopeSpec &SS,
    DeclarationNameInfo NameInfo, SourceLocation EllipsisLoc,
    const ParsedAttributesView &AttrList, bool IsInstantiation,
    bool IsUsingIfExists) {
  assert(!SS.isInvalid() && "Invalid CXXScopeSpec.");
  SourceLocation IdentLoc = NameInfo.getLoc();
  assert(IdentLoc.isValid() && "Invalid TargetName location.");

  // FIXME: We ignore attributes for now.

  // For an inheriting constructor declaration, the name of the using
  // declaration is the name of a constructor in this class, not in the
  // base class.
  DeclarationNameInfo UsingName = NameInfo;
  if (UsingName.getName().getNameKind() == DeclarationName::CXXConstructorName)
    if (auto *RD = dyn_cast<CXXRecordDecl>(CurContext))
      UsingName.setName(Context.DeclarationNames.getCXXConstructorName(
          Context.getCanonicalType(Context.getRecordType(RD))));

  // Do the redeclaration lookup in the current scope.
  LookupResult Previous(*this, UsingName, LookupUsingDeclName,
                        RedeclarationKind::ForVisibleRedeclaration);
  Previous.setHideTags(false);
  if (S) {
    LookupName(Previous, S);

    FilterUsingLookup(S, Previous);
  } else {
    assert(IsInstantiation && "no scope in non-instantiation");
    if (CurContext->isRecord())
      LookupQualifiedName(Previous, CurContext);
    else {
      // No redeclaration check is needed here; in non-member contexts we
      // diagnosed all possible conflicts with other using-declarations when
      // building the template:
      //
      // For a dependent non-type using declaration, the only valid case is
      // if we instantiate to a single enumerator. We check for conflicts
      // between shadow declarations we introduce, and we check in the template
      // definition for conflicts between a non-type using declaration and any
      // other declaration, which together covers all cases.
      //
      // A dependent typename using declaration will never successfully
      // instantiate, since it will always name a class member, so we reject
      // that in the template definition.
    }
  }

  // Check for invalid redeclarations.
  if (CheckUsingDeclRedeclaration(UsingLoc, HasTypenameKeyword,
                                  SS, IdentLoc, Previous))
    return nullptr;

  // 'using_if_exists' doesn't make sense on an inherited constructor.
  if (IsUsingIfExists && UsingName.getName().getNameKind() ==
                             DeclarationName::CXXConstructorName) {
    Diag(UsingLoc, diag::err_using_if_exists_on_ctor);
    return nullptr;
  }

  DeclContext *LookupContext = computeDeclContext(SS);
  NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
  if (!LookupContext || EllipsisLoc.isValid()) {
    NamedDecl *D;
    // Dependent scope, or an unexpanded pack
    if (!LookupContext && CheckUsingDeclQualifier(UsingLoc, HasTypenameKeyword,
                                                  SS, NameInfo, IdentLoc))
      return nullptr;

    if (HasTypenameKeyword) {
      // FIXME: not all declaration name kinds are legal here
      D = UnresolvedUsingTypenameDecl::Create(Context, CurContext,
                                              UsingLoc, TypenameLoc,
                                              QualifierLoc,
                                              IdentLoc, NameInfo.getName(),
                                              EllipsisLoc);
    } else {
      D = UnresolvedUsingValueDecl::Create(Context, CurContext, UsingLoc,
                                           QualifierLoc, NameInfo, EllipsisLoc);
    }
    D->setAccess(AS);
    CurContext->addDecl(D);
    ProcessDeclAttributeList(S, D, AttrList);
    return D;
  }

  auto Build = [&](bool Invalid) {
    UsingDecl *UD =
        UsingDecl::Create(Context, CurContext, UsingLoc, QualifierLoc,
                          UsingName, HasTypenameKeyword);
    UD->setAccess(AS);
    CurContext->addDecl(UD);
    ProcessDeclAttributeList(S, UD, AttrList);
    UD->setInvalidDecl(Invalid);
    return UD;
  };
  auto BuildInvalid = [&]{ return Build(true); };
  auto BuildValid = [&]{ return Build(false); };

  if (RequireCompleteDeclContext(SS, LookupContext))
    return BuildInvalid();

  // Look up the target name.
  LookupResult R(*this, NameInfo, LookupOrdinaryName);

  // Unlike most lookups, we don't always want to hide tag
  // declarations: tag names are visible through the using declaration
  // even if hidden by ordinary names, *except* in a dependent context
  // where they may be used by two-phase lookup.
  if (!IsInstantiation)
    R.setHideTags(false);

  // For the purposes of this lookup, we have a base object type
  // equal to that of the current context.
  if (CurContext->isRecord()) {
    R.setBaseObjectType(
                   Context.getTypeDeclType(cast<CXXRecordDecl>(CurContext)));
  }

  LookupQualifiedName(R, LookupContext);

  // Validate the context, now we have a lookup
  if (CheckUsingDeclQualifier(UsingLoc, HasTypenameKeyword, SS, NameInfo,
                              IdentLoc, &R))
    return nullptr;

  if (R.empty() && IsUsingIfExists)
    R.addDecl(UnresolvedUsingIfExistsDecl::Create(Context, CurContext, UsingLoc,
                                                  UsingName.getName()),
              AS_public);

  // Try to correct typos if possible. If constructor name lookup finds no
  // results, that means the named class has no explicit constructors, and we
  // suppressed declaring implicit ones (probably because it's dependent or
  // invalid).
  if (R.empty() &&
      NameInfo.getName().getNameKind() != DeclarationName::CXXConstructorName) {
    // HACK 2017-01-08: Work around an issue with libstdc++'s detection of
    // ::gets. Sometimes it believes that glibc provides a ::gets in cases where
    // it does not. The issue was fixed in libstdc++ 6.3 (2016-12-21) and later.
    auto *II = NameInfo.getName().getAsIdentifierInfo();
    if (getLangOpts().CPlusPlus14 && II && II->isStr("gets") &&
        CurContext->isStdNamespace() &&
        isa<TranslationUnitDecl>(LookupContext) &&
        getSourceManager().isInSystemHeader(UsingLoc))
      return nullptr;
    UsingValidatorCCC CCC(HasTypenameKeyword, IsInstantiation, SS.getScopeRep(),
                          dyn_cast<CXXRecordDecl>(CurContext));
    if (TypoCorrection Corrected =
            CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), S, &SS, CCC,
                        CTK_ErrorRecovery)) {
      // We reject candidates where DroppedSpecifier == true, hence the
      // literal '0' below.
      diagnoseTypo(Corrected, PDiag(diag::err_no_member_suggest)
                                << NameInfo.getName() << LookupContext << 0
                                << SS.getRange());

      // If we picked a correction with no attached Decl we can't do anything
      // useful with it, bail out.
      NamedDecl *ND = Corrected.getCorrectionDecl();
      if (!ND)
        return BuildInvalid();

      // If we corrected to an inheriting constructor, handle it as one.
      auto *RD = dyn_cast<CXXRecordDecl>(ND);
      if (RD && RD->isInjectedClassName()) {
        // The parent of the injected class name is the class itself.
        RD = cast<CXXRecordDecl>(RD->getParent());

        // Fix up the information we'll use to build the using declaration.
        if (Corrected.WillReplaceSpecifier()) {
          NestedNameSpecifierLocBuilder Builder;
          Builder.MakeTrivial(Context, Corrected.getCorrectionSpecifier(),
                              QualifierLoc.getSourceRange());
          QualifierLoc = Builder.getWithLocInContext(Context);
        }

        // In this case, the name we introduce is the name of a derived class
        // constructor.
        auto *CurClass = cast<CXXRecordDecl>(CurContext);
        UsingName.setName(Context.DeclarationNames.getCXXConstructorName(
            Context.getCanonicalType(Context.getRecordType(CurClass))));
        UsingName.setNamedTypeInfo(nullptr);
        for (auto *Ctor : LookupConstructors(RD))
          R.addDecl(Ctor);
        R.resolveKind();
      } else {
        // FIXME: Pick up all the declarations if we found an overloaded
        // function.
        UsingName.setName(ND->getDeclName());
        R.addDecl(ND);
      }
    } else {
      Diag(IdentLoc, diag::err_no_member)
        << NameInfo.getName() << LookupContext << SS.getRange();
      return BuildInvalid();
    }
  }

  if (R.isAmbiguous())
    return BuildInvalid();

  if (HasTypenameKeyword) {
    // If we asked for a typename and got a non-type decl, error out.
    if (!R.getAsSingle<TypeDecl>() &&
        !R.getAsSingle<UnresolvedUsingIfExistsDecl>()) {
      Diag(IdentLoc, diag::err_using_typename_non_type);
      for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I)
        Diag((*I)->getUnderlyingDecl()->getLocation(),
             diag::note_using_decl_target);
      return BuildInvalid();
    }
  } else {
    // If we asked for a non-typename and we got a type, error out,
    // but only if this is an instantiation of an unresolved using
    // decl.  Otherwise just silently find the type name.
    if (IsInstantiation && R.getAsSingle<TypeDecl>()) {
      Diag(IdentLoc, diag::err_using_dependent_value_is_type);
      Diag(R.getFoundDecl()->getLocation(), diag::note_using_decl_target);
      return BuildInvalid();
    }
  }

  // C++14 [namespace.udecl]p6:
  // A using-declaration shall not name a namespace.
  if (R.getAsSingle<NamespaceDecl>()) {
    Diag(IdentLoc, diag::err_using_decl_can_not_refer_to_namespace)
        << SS.getRange();
    // Suggest using 'using namespace ...' instead.
    Diag(SS.getBeginLoc(), diag::note_namespace_using_decl)
        << FixItHint::CreateInsertion(SS.getBeginLoc(), "namespace ");
    return BuildInvalid();
  }

  UsingDecl *UD = BuildValid();

  // Some additional rules apply to inheriting constructors.
  if (UsingName.getName().getNameKind() ==
        DeclarationName::CXXConstructorName) {
    // Suppress access diagnostics; the access check is instead performed at the
    // point of use for an inheriting constructor.
    R.suppressDiagnostics();
    if (CheckInheritingConstructorUsingDecl(UD))
      return UD;
  }

  for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
    UsingShadowDecl *PrevDecl = nullptr;
    if (!CheckUsingShadowDecl(UD, *I, Previous, PrevDecl))
      BuildUsingShadowDecl(S, UD, *I, PrevDecl);
  }

  return UD;
}

NamedDecl *Sema::BuildUsingEnumDeclaration(Scope *S, AccessSpecifier AS,
                                           SourceLocation UsingLoc,
                                           SourceLocation EnumLoc,
                                           SourceLocation NameLoc,
                                           TypeSourceInfo *EnumType,
                                           EnumDecl *ED) {
  bool Invalid = false;

  if (CurContext->getRedeclContext()->isRecord()) {
    /// In class scope, check if this is a duplicate, for better a diagnostic.
    DeclarationNameInfo UsingEnumName(ED->getDeclName(), NameLoc);
    LookupResult Previous(*this, UsingEnumName, LookupUsingDeclName,
                          RedeclarationKind::ForVisibleRedeclaration);

    LookupName(Previous, S);

    for (NamedDecl *D : Previous)
      if (UsingEnumDecl *UED = dyn_cast<UsingEnumDecl>(D))
        if (UED->getEnumDecl() == ED) {
          Diag(UsingLoc, diag::err_using_enum_decl_redeclaration)
              << SourceRange(EnumLoc, NameLoc);
          Diag(D->getLocation(), diag::note_using_enum_decl) << 1;
          Invalid = true;
          break;
        }
  }

  if (RequireCompleteEnumDecl(ED, NameLoc))
    Invalid = true;

  UsingEnumDecl *UD = UsingEnumDecl::Create(Context, CurContext, UsingLoc,
                                            EnumLoc, NameLoc, EnumType);
  UD->setAccess(AS);
  CurContext->addDecl(UD);

  if (Invalid) {
    UD->setInvalidDecl();
    return UD;
  }

  // Create the shadow decls for each enumerator
  for (EnumConstantDecl *EC : ED->enumerators()) {
    UsingShadowDecl *PrevDecl = nullptr;
    DeclarationNameInfo DNI(EC->getDeclName(), EC->getLocation());
    LookupResult Previous(*this, DNI, LookupOrdinaryName,
                          RedeclarationKind::ForVisibleRedeclaration);
    LookupName(Previous, S);
    FilterUsingLookup(S, Previous);

    if (!CheckUsingShadowDecl(UD, EC, Previous, PrevDecl))
      BuildUsingShadowDecl(S, UD, EC, PrevDecl);
  }

  return UD;
}

NamedDecl *Sema::BuildUsingPackDecl(NamedDecl *InstantiatedFrom,
                                    ArrayRef<NamedDecl *> Expansions) {
  assert(isa<UnresolvedUsingValueDecl>(InstantiatedFrom) ||
         isa<UnresolvedUsingTypenameDecl>(InstantiatedFrom) ||
         isa<UsingPackDecl>(InstantiatedFrom));

  auto *UPD =
      UsingPackDecl::Create(Context, CurContext, InstantiatedFrom, Expansions);
  UPD->setAccess(InstantiatedFrom->getAccess());
  CurContext->addDecl(UPD);
  return UPD;
}

bool Sema::CheckInheritingConstructorUsingDecl(UsingDecl *UD) {
  assert(!UD->hasTypename() && "expecting a constructor name");

  const Type *SourceType = UD->getQualifier()->getAsType();
  assert(SourceType &&
         "Using decl naming constructor doesn't have type in scope spec.");
  CXXRecordDecl *TargetClass = cast<CXXRecordDecl>(CurContext);

  // Check whether the named type is a direct base class.
  bool AnyDependentBases = false;
  auto *Base = findDirectBaseWithType(TargetClass, QualType(SourceType, 0),
                                      AnyDependentBases);
  if (!Base && !AnyDependentBases) {
    Diag(UD->getUsingLoc(),
         diag::err_using_decl_constructor_not_in_direct_base)
      << UD->getNameInfo().getSourceRange()
      << QualType(SourceType, 0) << TargetClass;
    UD->setInvalidDecl();
    return true;
  }

  if (Base)
    Base->setInheritConstructors();

  return false;
}

bool Sema::CheckUsingDeclRedeclaration(SourceLocation UsingLoc,
                                       bool HasTypenameKeyword,
                                       const CXXScopeSpec &SS,
                                       SourceLocation NameLoc,
                                       const LookupResult &Prev) {
  NestedNameSpecifier *Qual = SS.getScopeRep();

  // C++03 [namespace.udecl]p8:
  // C++0x [namespace.udecl]p10:
  //   A using-declaration is a declaration and can therefore be used
  //   repeatedly where (and only where) multiple declarations are
  //   allowed.
  //
  // That's in non-member contexts.
  if (!CurContext->getRedeclContext()->isRecord()) {
    // A dependent qualifier outside a class can only ever resolve to an
    // enumeration type. Therefore it conflicts with any other non-type
    // declaration in the same scope.
    // FIXME: How should we check for dependent type-type conflicts at block
    // scope?
    if (Qual->isDependent() && !HasTypenameKeyword) {
      for (auto *D : Prev) {
        if (!isa<TypeDecl>(D) && !isa<UsingDecl>(D) && !isa<UsingPackDecl>(D)) {
          bool OldCouldBeEnumerator =
              isa<UnresolvedUsingValueDecl>(D) || isa<EnumConstantDecl>(D);
          Diag(NameLoc,
               OldCouldBeEnumerator ? diag::err_redefinition
                                    : diag::err_redefinition_different_kind)
              << Prev.getLookupName();
          Diag(D->getLocation(), diag::note_previous_definition);
          return true;
        }
      }
    }
    return false;
  }

  const NestedNameSpecifier *CNNS =
      Context.getCanonicalNestedNameSpecifier(Qual);
  for (LookupResult::iterator I = Prev.begin(), E = Prev.end(); I != E; ++I) {
    NamedDecl *D = *I;

    bool DTypename;
    NestedNameSpecifier *DQual;
    if (UsingDecl *UD = dyn_cast<UsingDecl>(D)) {
      DTypename = UD->hasTypename();
      DQual = UD->getQualifier();
    } else if (UnresolvedUsingValueDecl *UD
                 = dyn_cast<UnresolvedUsingValueDecl>(D)) {
      DTypename = false;
      DQual = UD->getQualifier();
    } else if (UnresolvedUsingTypenameDecl *UD
                 = dyn_cast<UnresolvedUsingTypenameDecl>(D)) {
      DTypename = true;
      DQual = UD->getQualifier();
    } else continue;

    // using decls differ if one says 'typename' and the other doesn't.
    // FIXME: non-dependent using decls?
    if (HasTypenameKeyword != DTypename) continue;

    // using decls differ if they name different scopes (but note that
    // template instantiation can cause this check to trigger when it
    // didn't before instantiation).
    if (CNNS != Context.getCanonicalNestedNameSpecifier(DQual))
      continue;

    Diag(NameLoc, diag::err_using_decl_redeclaration) << SS.getRange();
    Diag(D->getLocation(), diag::note_using_decl) << 1;
    return true;
  }

  return false;
}

bool Sema::CheckUsingDeclQualifier(SourceLocation UsingLoc, bool HasTypename,
                                   const CXXScopeSpec &SS,
                                   const DeclarationNameInfo &NameInfo,
                                   SourceLocation NameLoc,
                                   const LookupResult *R, const UsingDecl *UD) {
  DeclContext *NamedContext = computeDeclContext(SS);
  assert(bool(NamedContext) == (R || UD) && !(R && UD) &&
         "resolvable context must have exactly one set of decls");

  // C++ 20 permits using an enumerator that does not have a class-hierarchy
  // relationship.
  bool Cxx20Enumerator = false;
  if (NamedContext) {
    EnumConstantDecl *EC = nullptr;
    if (R)
      EC = R->getAsSingle<EnumConstantDecl>();
    else if (UD && UD->shadow_size() == 1)
      EC = dyn_cast<EnumConstantDecl>(UD->shadow_begin()->getTargetDecl());
    if (EC)
      Cxx20Enumerator = getLangOpts().CPlusPlus20;

    if (auto *ED = dyn_cast<EnumDecl>(NamedContext)) {
      // C++14 [namespace.udecl]p7:
      // A using-declaration shall not name a scoped enumerator.
      // C++20 p1099 permits enumerators.
      if (EC && R && ED->isScoped())
        Diag(SS.getBeginLoc(),
             getLangOpts().CPlusPlus20
                 ? diag::warn_cxx17_compat_using_decl_scoped_enumerator
                 : diag::ext_using_decl_scoped_enumerator)
            << SS.getRange();

      // We want to consider the scope of the enumerator
      NamedContext = ED->getDeclContext();
    }
  }

  if (!CurContext->isRecord()) {
    // C++03 [namespace.udecl]p3:
    // C++0x [namespace.udecl]p8:
    //   A using-declaration for a class member shall be a member-declaration.
    // C++20 [namespace.udecl]p7
    //   ... other than an enumerator ...

    // If we weren't able to compute a valid scope, it might validly be a
    // dependent class or enumeration scope. If we have a 'typename' keyword,
    // the scope must resolve to a class type.
    if (NamedContext ? !NamedContext->getRedeclContext()->isRecord()
                     : !HasTypename)
      return false; // OK

    Diag(NameLoc,
         Cxx20Enumerator
             ? diag::warn_cxx17_compat_using_decl_class_member_enumerator
             : diag::err_using_decl_can_not_refer_to_class_member)
        << SS.getRange();

    if (Cxx20Enumerator)
      return false; // OK

    auto *RD = NamedContext
                   ? cast<CXXRecordDecl>(NamedContext->getRedeclContext())
                   : nullptr;
    if (RD && !RequireCompleteDeclContext(const_cast<CXXScopeSpec &>(SS), RD)) {
      // See if there's a helpful fixit

      if (!R) {
        // We will have already diagnosed the problem on the template
        // definition,  Maybe we should do so again?
      } else if (R->getAsSingle<TypeDecl>()) {
        if (getLangOpts().CPlusPlus11) {
          // Convert 'using X::Y;' to 'using Y = X::Y;'.
          Diag(SS.getBeginLoc(), diag::note_using_decl_class_member_workaround)
            << 0 // alias declaration
            << FixItHint::CreateInsertion(SS.getBeginLoc(),
                                          NameInfo.getName().getAsString() +
                                              " = ");
        } else {
          // Convert 'using X::Y;' to 'typedef X::Y Y;'.
          SourceLocation InsertLoc = getLocForEndOfToken(NameInfo.getEndLoc());
          Diag(InsertLoc, diag::note_using_decl_class_member_workaround)
            << 1 // typedef declaration
            << FixItHint::CreateReplacement(UsingLoc, "typedef")
            << FixItHint::CreateInsertion(
                   InsertLoc, " " + NameInfo.getName().getAsString());
        }
      } else if (R->getAsSingle<VarDecl>()) {
        // Don't provide a fixit outside C++11 mode; we don't want to suggest
        // repeating the type of the static data member here.
        FixItHint FixIt;
        if (getLangOpts().CPlusPlus11) {
          // Convert 'using X::Y;' to 'auto &Y = X::Y;'.
          FixIt = FixItHint::CreateReplacement(
              UsingLoc, "auto &" + NameInfo.getName().getAsString() + " = ");
        }

        Diag(UsingLoc, diag::note_using_decl_class_member_workaround)
          << 2 // reference declaration
          << FixIt;
      } else if (R->getAsSingle<EnumConstantDecl>()) {
        // Don't provide a fixit outside C++11 mode; we don't want to suggest
        // repeating the type of the enumeration here, and we can't do so if
        // the type is anonymous.
        FixItHint FixIt;
        if (getLangOpts().CPlusPlus11) {
          // Convert 'using X::Y;' to 'auto &Y = X::Y;'.
          FixIt = FixItHint::CreateReplacement(
              UsingLoc,
              "constexpr auto " + NameInfo.getName().getAsString() + " = ");
        }

        Diag(UsingLoc, diag::note_using_decl_class_member_workaround)
          << (getLangOpts().CPlusPlus11 ? 4 : 3) // const[expr] variable
          << FixIt;
      }
    }

    return true; // Fail
  }

  // If the named context is dependent, we can't decide much.
  if (!NamedContext) {
    // FIXME: in C++0x, we can diagnose if we can prove that the
    // nested-name-specifier does not refer to a base class, which is
    // still possible in some cases.

    // Otherwise we have to conservatively report that things might be
    // okay.
    return false;
  }

  // The current scope is a record.
  if (!NamedContext->isRecord()) {
    // Ideally this would point at the last name in the specifier,
    // but we don't have that level of source info.
    Diag(SS.getBeginLoc(),
         Cxx20Enumerator
             ? diag::warn_cxx17_compat_using_decl_non_member_enumerator
             : diag::err_using_decl_nested_name_specifier_is_not_class)
        << SS.getScopeRep() << SS.getRange();

    if (Cxx20Enumerator)
      return false; // OK

    return true;
  }

  if (!NamedContext->isDependentContext() &&
      RequireCompleteDeclContext(const_cast<CXXScopeSpec&>(SS), NamedContext))
    return true;

  if (getLangOpts().CPlusPlus11) {
    // C++11 [namespace.udecl]p3:
    //   In a using-declaration used as a member-declaration, the
    //   nested-name-specifier shall name a base class of the class
    //   being defined.

    if (cast<CXXRecordDecl>(CurContext)->isProvablyNotDerivedFrom(
                                 cast<CXXRecordDecl>(NamedContext))) {

      if (Cxx20Enumerator) {
        Diag(NameLoc, diag::warn_cxx17_compat_using_decl_non_member_enumerator)
            << SS.getRange();
        return false;
      }

      if (CurContext == NamedContext) {
        Diag(SS.getBeginLoc(),
             diag::err_using_decl_nested_name_specifier_is_current_class)
            << SS.getRange();
        return !getLangOpts().CPlusPlus20;
      }

      if (!cast<CXXRecordDecl>(NamedContext)->isInvalidDecl()) {
        Diag(SS.getBeginLoc(),
             diag::err_using_decl_nested_name_specifier_is_not_base_class)
            << SS.getScopeRep() << cast<CXXRecordDecl>(CurContext)
            << SS.getRange();
      }
      return true;
    }

    return false;
  }

  // C++03 [namespace.udecl]p4:
  //   A using-declaration used as a member-declaration shall refer
  //   to a member of a base class of the class being defined [etc.].

  // Salient point: SS doesn't have to name a base class as long as
  // lookup only finds members from base classes.  Therefore we can
  // diagnose here only if we can prove that can't happen,
  // i.e. if the class hierarchies provably don't intersect.

  // TODO: it would be nice if "definitely valid" results were cached
  // in the UsingDecl and UsingShadowDecl so that these checks didn't
  // need to be repeated.

  llvm::SmallPtrSet<const CXXRecordDecl *, 4> Bases;
  auto Collect = [&Bases](const CXXRecordDecl *Base) {
    Bases.insert(Base);
    return true;
  };

  // Collect all bases. Return false if we find a dependent base.
  if (!cast<CXXRecordDecl>(CurContext)->forallBases(Collect))
    return false;

  // Returns true if the base is dependent or is one of the accumulated base
  // classes.
  auto IsNotBase = [&Bases](const CXXRecordDecl *Base) {
    return !Bases.count(Base);
  };

  // Return false if the class has a dependent base or if it or one
  // of its bases is present in the base set of the current context.
  if (Bases.count(cast<CXXRecordDecl>(NamedContext)) ||
      !cast<CXXRecordDecl>(NamedContext)->forallBases(IsNotBase))
    return false;

  Diag(SS.getRange().getBegin(),
       diag::err_using_decl_nested_name_specifier_is_not_base_class)
    << SS.getScopeRep()
    << cast<CXXRecordDecl>(CurContext)
    << SS.getRange();

  return true;
}

Decl *Sema::ActOnAliasDeclaration(Scope *S, AccessSpecifier AS,
                                  MultiTemplateParamsArg TemplateParamLists,
                                  SourceLocation UsingLoc, UnqualifiedId &Name,
                                  const ParsedAttributesView &AttrList,
                                  TypeResult Type, Decl *DeclFromDeclSpec) {
  // Get the innermost enclosing declaration scope.
  S = S->getDeclParent();

  if (Type.isInvalid())
    return nullptr;

  bool Invalid = false;
  DeclarationNameInfo NameInfo = GetNameFromUnqualifiedId(Name);
  TypeSourceInfo *TInfo = nullptr;
  GetTypeFromParser(Type.get(), &TInfo);

  if (DiagnoseClassNameShadow(CurContext, NameInfo))
    return nullptr;

  if (DiagnoseUnexpandedParameterPack(Name.StartLocation, TInfo,
                                      UPPC_DeclarationType)) {
    Invalid = true;
    TInfo = Context.getTrivialTypeSourceInfo(Context.IntTy,
                                             TInfo->getTypeLoc().getBeginLoc());
  }

  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        TemplateParamLists.size()
                            ? forRedeclarationInCurContext()
                            : RedeclarationKind::ForVisibleRedeclaration);
  LookupName(Previous, S);

  // Warn about shadowing the name of a template parameter.
  if (Previous.isSingleResult() &&
      Previous.getFoundDecl()->isTemplateParameter()) {
    DiagnoseTemplateParameterShadow(Name.StartLocation,Previous.getFoundDecl());
    Previous.clear();
  }

  assert(Name.getKind() == UnqualifiedIdKind::IK_Identifier &&
         "name in alias declaration must be an identifier");
  TypeAliasDecl *NewTD = TypeAliasDecl::Create(Context, CurContext, UsingLoc,
                                               Name.StartLocation,
                                               Name.Identifier, TInfo);

  NewTD->setAccess(AS);

  if (Invalid)
    NewTD->setInvalidDecl();

  ProcessDeclAttributeList(S, NewTD, AttrList);
  AddPragmaAttributes(S, NewTD);
  ProcessAPINotes(NewTD);

  CheckTypedefForVariablyModifiedType(S, NewTD);
  Invalid |= NewTD->isInvalidDecl();

  bool Redeclaration = false;

  NamedDecl *NewND;
  if (TemplateParamLists.size()) {
    TypeAliasTemplateDecl *OldDecl = nullptr;
    TemplateParameterList *OldTemplateParams = nullptr;

    if (TemplateParamLists.size() != 1) {
      Diag(UsingLoc, diag::err_alias_template_extra_headers)
        << SourceRange(TemplateParamLists[1]->getTemplateLoc(),
         TemplateParamLists[TemplateParamLists.size()-1]->getRAngleLoc());
      Invalid = true;
    }
    TemplateParameterList *TemplateParams = TemplateParamLists[0];

    // Check that we can declare a template here.
    if (CheckTemplateDeclScope(S, TemplateParams))
      return nullptr;

    // Only consider previous declarations in the same scope.
    FilterLookupForScope(Previous, CurContext, S, /*ConsiderLinkage*/false,
                         /*ExplicitInstantiationOrSpecialization*/false);
    if (!Previous.empty()) {
      Redeclaration = true;

      OldDecl = Previous.getAsSingle<TypeAliasTemplateDecl>();
      if (!OldDecl && !Invalid) {
        Diag(UsingLoc, diag::err_redefinition_different_kind)
          << Name.Identifier;

        NamedDecl *OldD = Previous.getRepresentativeDecl();
        if (OldD->getLocation().isValid())
          Diag(OldD->getLocation(), diag::note_previous_definition);

        Invalid = true;
      }

      if (!Invalid && OldDecl && !OldDecl->isInvalidDecl()) {
        if (TemplateParameterListsAreEqual(TemplateParams,
                                           OldDecl->getTemplateParameters(),
                                           /*Complain=*/true,
                                           TPL_TemplateMatch))
          OldTemplateParams =
              OldDecl->getMostRecentDecl()->getTemplateParameters();
        else
          Invalid = true;

        TypeAliasDecl *OldTD = OldDecl->getTemplatedDecl();
        if (!Invalid &&
            !Context.hasSameType(OldTD->getUnderlyingType(),
                                 NewTD->getUnderlyingType())) {
          // FIXME: The C++0x standard does not clearly say this is ill-formed,
          // but we can't reasonably accept it.
          Diag(NewTD->getLocation(), diag::err_redefinition_different_typedef)
            << 2 << NewTD->getUnderlyingType() << OldTD->getUnderlyingType();
          if (OldTD->getLocation().isValid())
            Diag(OldTD->getLocation(), diag::note_previous_definition);
          Invalid = true;
        }
      }
    }

    // Merge any previous default template arguments into our parameters,
    // and check the parameter list.
    if (CheckTemplateParameterList(TemplateParams, OldTemplateParams,
                                   TPC_TypeAliasTemplate))
      return nullptr;

    TypeAliasTemplateDecl *NewDecl =
      TypeAliasTemplateDecl::Create(Context, CurContext, UsingLoc,
                                    Name.Identifier, TemplateParams,
                                    NewTD);
    NewTD->setDescribedAliasTemplate(NewDecl);

    NewDecl->setAccess(AS);

    if (Invalid)
      NewDecl->setInvalidDecl();
    else if (OldDecl) {
      NewDecl->setPreviousDecl(OldDecl);
      CheckRedeclarationInModule(NewDecl, OldDecl);
    }

    NewND = NewDecl;
  } else {
    if (auto *TD = dyn_cast_or_null<TagDecl>(DeclFromDeclSpec)) {
      setTagNameForLinkagePurposes(TD, NewTD);
      handleTagNumbering(TD, S);
    }
    ActOnTypedefNameDecl(S, CurContext, NewTD, Previous, Redeclaration);
    NewND = NewTD;
  }

  PushOnScopeChains(NewND, S);
  ActOnDocumentableDecl(NewND);
  return NewND;
}

Decl *Sema::ActOnNamespaceAliasDef(Scope *S, SourceLocation NamespaceLoc,
                                   SourceLocation AliasLoc,
                                   IdentifierInfo *Alias, CXXScopeSpec &SS,
                                   SourceLocation IdentLoc,
                                   IdentifierInfo *Ident) {

  // Lookup the namespace name.
  LookupResult R(*this, Ident, IdentLoc, LookupNamespaceName);
  LookupParsedName(R, S, &SS, /*ObjectType=*/QualType());

  if (R.isAmbiguous())
    return nullptr;

  if (R.empty()) {
    if (!TryNamespaceTypoCorrection(*this, R, S, SS, IdentLoc, Ident)) {
      Diag(IdentLoc, diag::err_expected_namespace_name) << SS.getRange();
      return nullptr;
    }
  }
  assert(!R.isAmbiguous() && !R.empty());
  NamedDecl *ND = R.getRepresentativeDecl();

  // Check if we have a previous declaration with the same name.
  LookupResult PrevR(*this, Alias, AliasLoc, LookupOrdinaryName,
                     RedeclarationKind::ForVisibleRedeclaration);
  LookupName(PrevR, S);

  // Check we're not shadowing a template parameter.
  if (PrevR.isSingleResult() && PrevR.getFoundDecl()->isTemplateParameter()) {
    DiagnoseTemplateParameterShadow(AliasLoc, PrevR.getFoundDecl());
    PrevR.clear();
  }

  // Filter out any other lookup result from an enclosing scope.
  FilterLookupForScope(PrevR, CurContext, S, /*ConsiderLinkage*/false,
                       /*AllowInlineNamespace*/false);

  // Find the previous declaration and check that we can redeclare it.
  NamespaceAliasDecl *Prev = nullptr;
  if (PrevR.isSingleResult()) {
    NamedDecl *PrevDecl = PrevR.getRepresentativeDecl();
    if (NamespaceAliasDecl *AD = dyn_cast<NamespaceAliasDecl>(PrevDecl)) {
      // We already have an alias with the same name that points to the same
      // namespace; check that it matches.
      if (AD->getNamespace()->Equals(getNamespaceDecl(ND))) {
        Prev = AD;
      } else if (isVisible(PrevDecl)) {
        Diag(AliasLoc, diag::err_redefinition_different_namespace_alias)
          << Alias;
        Diag(AD->getLocation(), diag::note_previous_namespace_alias)
          << AD->getNamespace();
        return nullptr;
      }
    } else if (isVisible(PrevDecl)) {
      unsigned DiagID = isa<NamespaceDecl>(PrevDecl->getUnderlyingDecl())
                            ? diag::err_redefinition
                            : diag::err_redefinition_different_kind;
      Diag(AliasLoc, DiagID) << Alias;
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      return nullptr;
    }
  }

  // The use of a nested name specifier may trigger deprecation warnings.
  DiagnoseUseOfDecl(ND, IdentLoc);

  NamespaceAliasDecl *AliasDecl =
    NamespaceAliasDecl::Create(Context, CurContext, NamespaceLoc, AliasLoc,
                               Alias, SS.getWithLocInContext(Context),
                               IdentLoc, ND);
  if (Prev)
    AliasDecl->setPreviousDecl(Prev);

  PushOnScopeChains(AliasDecl, S);
  return AliasDecl;
}

namespace {
struct SpecialMemberExceptionSpecInfo
    : SpecialMemberVisitor<SpecialMemberExceptionSpecInfo> {
  SourceLocation Loc;
  Sema::ImplicitExceptionSpecification ExceptSpec;

  SpecialMemberExceptionSpecInfo(Sema &S, CXXMethodDecl *MD,
                                 CXXSpecialMemberKind CSM,
                                 Sema::InheritedConstructorInfo *ICI,
                                 SourceLocation Loc)
      : SpecialMemberVisitor(S, MD, CSM, ICI), Loc(Loc), ExceptSpec(S) {}

  bool visitBase(CXXBaseSpecifier *Base);
  bool visitField(FieldDecl *FD);

  void visitClassSubobject(CXXRecordDecl *Class, Subobject Subobj,
                           unsigned Quals);

  void visitSubobjectCall(Subobject Subobj,
                          Sema::SpecialMemberOverloadResult SMOR);
};
}

bool SpecialMemberExceptionSpecInfo::visitBase(CXXBaseSpecifier *Base) {
  auto *RT = Base->getType()->getAs<RecordType>();
  if (!RT)
    return false;

  auto *BaseClass = cast<CXXRecordDecl>(RT->getDecl());
  Sema::SpecialMemberOverloadResult SMOR = lookupInheritedCtor(BaseClass);
  if (auto *BaseCtor = SMOR.getMethod()) {
    visitSubobjectCall(Base, BaseCtor);
    return false;
  }

  visitClassSubobject(BaseClass, Base, 0);
  return false;
}

bool SpecialMemberExceptionSpecInfo::visitField(FieldDecl *FD) {
  if (CSM == CXXSpecialMemberKind::DefaultConstructor &&
      FD->hasInClassInitializer()) {
    Expr *E = FD->getInClassInitializer();
    if (!E)
      // FIXME: It's a little wasteful to build and throw away a
      // CXXDefaultInitExpr here.
      // FIXME: We should have a single context note pointing at Loc, and
      // this location should be MD->getLocation() instead, since that's
      // the location where we actually use the default init expression.
      E = S.BuildCXXDefaultInitExpr(Loc, FD).get();
    if (E)
      ExceptSpec.CalledExpr(E);
  } else if (auto *RT = S.Context.getBaseElementType(FD->getType())
                            ->getAs<RecordType>()) {
    visitClassSubobject(cast<CXXRecordDecl>(RT->getDecl()), FD,
                        FD->getType().getCVRQualifiers());
  }
  return false;
}

void SpecialMemberExceptionSpecInfo::visitClassSubobject(CXXRecordDecl *Class,
                                                         Subobject Subobj,
                                                         unsigned Quals) {
  FieldDecl *Field = Subobj.dyn_cast<FieldDecl*>();
  bool IsMutable = Field && Field->isMutable();
  visitSubobjectCall(Subobj, lookupIn(Class, Quals, IsMutable));
}

void SpecialMemberExceptionSpecInfo::visitSubobjectCall(
    Subobject Subobj, Sema::SpecialMemberOverloadResult SMOR) {
  // Note, if lookup fails, it doesn't matter what exception specification we
  // choose because the special member will be deleted.
  if (CXXMethodDecl *MD = SMOR.getMethod())
    ExceptSpec.CalledDecl(getSubobjectLoc(Subobj), MD);
}

bool Sema::tryResolveExplicitSpecifier(ExplicitSpecifier &ExplicitSpec) {
  llvm::APSInt Result;
  ExprResult Converted = CheckConvertedConstantExpression(
      ExplicitSpec.getExpr(), Context.BoolTy, Result, CCEK_ExplicitBool);
  ExplicitSpec.setExpr(Converted.get());
  if (Converted.isUsable() && !Converted.get()->isValueDependent()) {
    ExplicitSpec.setKind(Result.getBoolValue()
                             ? ExplicitSpecKind::ResolvedTrue
                             : ExplicitSpecKind::ResolvedFalse);
    return true;
  }
  ExplicitSpec.setKind(ExplicitSpecKind::Unresolved);
  return false;
}

ExplicitSpecifier Sema::ActOnExplicitBoolSpecifier(Expr *ExplicitExpr) {
  ExplicitSpecifier ES(ExplicitExpr, ExplicitSpecKind::Unresolved);
  if (!ExplicitExpr->isTypeDependent())
    tryResolveExplicitSpecifier(ES);
  return ES;
}

static Sema::ImplicitExceptionSpecification
ComputeDefaultedSpecialMemberExceptionSpec(
    Sema &S, SourceLocation Loc, CXXMethodDecl *MD, CXXSpecialMemberKind CSM,
    Sema::InheritedConstructorInfo *ICI) {
  ComputingExceptionSpec CES(S, MD, Loc);

  CXXRecordDecl *ClassDecl = MD->getParent();

  // C++ [except.spec]p14:
  //   An implicitly declared special member function (Clause 12) shall have an
  //   exception-specification. [...]
  SpecialMemberExceptionSpecInfo Info(S, MD, CSM, ICI, MD->getLocation());
  if (ClassDecl->isInvalidDecl())
    return Info.ExceptSpec;

  // FIXME: If this diagnostic fires, we're probably missing a check for
  // attempting to resolve an exception specification before it's known
  // at a higher level.
  if (S.RequireCompleteType(MD->getLocation(),
                            S.Context.getRecordType(ClassDecl),
                            diag::err_exception_spec_incomplete_type))
    return Info.ExceptSpec;

  // C++1z [except.spec]p7:
  //   [Look for exceptions thrown by] a constructor selected [...] to
  //   initialize a potentially constructed subobject,
  // C++1z [except.spec]p8:
  //   The exception specification for an implicitly-declared destructor, or a
  //   destructor without a noexcept-specifier, is potentially-throwing if and
  //   only if any of the destructors for any of its potentially constructed
  //   subojects is potentially throwing.
  // FIXME: We respect the first rule but ignore the "potentially constructed"
  // in the second rule to resolve a core issue (no number yet) that would have
  // us reject:
  //   struct A { virtual void f() = 0; virtual ~A() noexcept(false) = 0; };
  //   struct B : A {};
  //   struct C : B { void f(); };
  // ... due to giving B::~B() a non-throwing exception specification.
  Info.visit(Info.IsConstructor ? Info.VisitPotentiallyConstructedBases
                                : Info.VisitAllBases);

  return Info.ExceptSpec;
}

namespace {
/// RAII object to register a special member as being currently declared.
struct DeclaringSpecialMember {
  Sema &S;
  Sema::SpecialMemberDecl D;
  Sema::ContextRAII SavedContext;
  bool WasAlreadyBeingDeclared;

  DeclaringSpecialMember(Sema &S, CXXRecordDecl *RD, CXXSpecialMemberKind CSM)
      : S(S), D(RD, CSM), SavedContext(S, RD) {
    WasAlreadyBeingDeclared = !S.SpecialMembersBeingDeclared.insert(D).second;
    if (WasAlreadyBeingDeclared)
      // This almost never happens, but if it does, ensure that our cache
      // doesn't contain a stale result.
      S.SpecialMemberCache.clear();
    else {
      // Register a note to be produced if we encounter an error while
      // declaring the special member.
      Sema::CodeSynthesisContext Ctx;
      Ctx.Kind = Sema::CodeSynthesisContext::DeclaringSpecialMember;
      // FIXME: We don't have a location to use here. Using the class's
      // location maintains the fiction that we declare all special members
      // with the class, but (1) it's not clear that lying about that helps our
      // users understand what's going on, and (2) there may be outer contexts
      // on the stack (some of which are relevant) and printing them exposes
      // our lies.
      Ctx.PointOfInstantiation = RD->getLocation();
      Ctx.Entity = RD;
      Ctx.SpecialMember = CSM;
      S.pushCodeSynthesisContext(Ctx);
    }
  }
  ~DeclaringSpecialMember() {
    if (!WasAlreadyBeingDeclared) {
      S.SpecialMembersBeingDeclared.erase(D);
      S.popCodeSynthesisContext();
    }
  }

  /// Are we already trying to declare this special member?
  bool isAlreadyBeingDeclared() const {
    return WasAlreadyBeingDeclared;
  }
};
}

void Sema::CheckImplicitSpecialMemberDeclaration(Scope *S, FunctionDecl *FD) {
  // Look up any existing declarations, but don't trigger declaration of all
  // implicit special members with this name.
  DeclarationName Name = FD->getDeclName();
  LookupResult R(*this, Name, SourceLocation(), LookupOrdinaryName,
                 RedeclarationKind::ForExternalRedeclaration);
  for (auto *D : FD->getParent()->lookup(Name))
    if (auto *Acceptable = R.getAcceptableDecl(D))
      R.addDecl(Acceptable);
  R.resolveKind();
  R.suppressDiagnostics();

  CheckFunctionDeclaration(S, FD, R, /*IsMemberSpecialization*/ false,
                           FD->isThisDeclarationADefinition());
}

void Sema::setupImplicitSpecialMemberType(CXXMethodDecl *SpecialMem,
                                          QualType ResultTy,
                                          ArrayRef<QualType> Args) {
  // Build an exception specification pointing back at this constructor.
  FunctionProtoType::ExtProtoInfo EPI = getImplicitMethodEPI(*this, SpecialMem);

  LangAS AS = getDefaultCXXMethodAddrSpace();
  if (AS != LangAS::Default) {
    EPI.TypeQuals.addAddressSpace(AS);
  }

  auto QT = Context.getFunctionType(ResultTy, Args, EPI);
  SpecialMem->setType(QT);

  // During template instantiation of implicit special member functions we need
  // a reliable TypeSourceInfo for the function prototype in order to allow
  // functions to be substituted.
  if (inTemplateInstantiation() &&
      cast<CXXRecordDecl>(SpecialMem->getParent())->isLambda()) {
    TypeSourceInfo *TSI =
        Context.getTrivialTypeSourceInfo(SpecialMem->getType());
    SpecialMem->setTypeSourceInfo(TSI);
  }
}

CXXConstructorDecl *Sema::DeclareImplicitDefaultConstructor(
                                                     CXXRecordDecl *ClassDecl) {
  // C++ [class.ctor]p5:
  //   A default constructor for a class X is a constructor of class X
  //   that can be called without an argument. If there is no
  //   user-declared constructor for class X, a default constructor is
  //   implicitly declared. An implicitly-declared default constructor
  //   is an inline public member of its class.
  assert(ClassDecl->needsImplicitDefaultConstructor() &&
         "Should not build implicit default constructor!");

  DeclaringSpecialMember DSM(*this, ClassDecl,
                             CXXSpecialMemberKind::DefaultConstructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  bool Constexpr = defaultedSpecialMemberIsConstexpr(
      *this, ClassDecl, CXXSpecialMemberKind::DefaultConstructor, false);

  // Create the actual constructor declaration.
  CanQualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(ClassType);
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXConstructorDecl *DefaultCon = CXXConstructorDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, /*Type*/ QualType(),
      /*TInfo=*/nullptr, ExplicitSpecifier(),
      getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true, /*isImplicitlyDeclared=*/true,
      Constexpr ? ConstexprSpecKind::Constexpr
                : ConstexprSpecKind::Unspecified);
  DefaultCon->setAccess(AS_public);
  DefaultCon->setDefaulted();

  setupImplicitSpecialMemberType(DefaultCon, Context.VoidTy, std::nullopt);

  if (getLangOpts().CUDA)
    CUDA().inferTargetForImplicitSpecialMember(
        ClassDecl, CXXSpecialMemberKind::DefaultConstructor, DefaultCon,
        /* ConstRHS */ false,
        /* Diagnose */ false);

  // We don't need to use SpecialMemberIsTrivial here; triviality for default
  // constructors is easy to compute.
  DefaultCon->setTrivial(ClassDecl->hasTrivialDefaultConstructor());

  // Note that we have declared this constructor.
  ++getASTContext().NumImplicitDefaultConstructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, DefaultCon);

  if (ShouldDeleteSpecialMember(DefaultCon,
                                CXXSpecialMemberKind::DefaultConstructor))
    SetDeclDeleted(DefaultCon, ClassLoc);

  if (S)
    PushOnScopeChains(DefaultCon, S, false);
  ClassDecl->addDecl(DefaultCon);

  return DefaultCon;
}

void Sema::DefineImplicitDefaultConstructor(SourceLocation CurrentLocation,
                                            CXXConstructorDecl *Constructor) {
  assert((Constructor->isDefaulted() && Constructor->isDefaultConstructor() &&
          !Constructor->doesThisDeclarationHaveABody() &&
          !Constructor->isDeleted()) &&
    "DefineImplicitDefaultConstructor - call it for implicit default ctor");
  if (Constructor->willHaveBody() || Constructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = Constructor->getParent();
  assert(ClassDecl && "DefineImplicitDefaultConstructor - invalid constructor");
  if (ClassDecl->isInvalidDecl()) {
    return;
  }

  SynthesizedFunctionScope Scope(*this, Constructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       Constructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  if (SetCtorInitializers(Constructor, /*AnyErrors=*/false)) {
    Constructor->setInvalidDecl();
    return;
  }

  SourceLocation Loc = Constructor->getEndLoc().isValid()
                           ? Constructor->getEndLoc()
                           : Constructor->getLocation();
  Constructor->setBody(new (Context) CompoundStmt(Loc));
  Constructor->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Constructor);
  }

  DiagnoseUninitializedFields(*this, Constructor);
}

void Sema::ActOnFinishDelayedMemberInitializers(Decl *D) {
  // Perform any delayed checks on exception specifications.
  CheckDelayedMemberExceptionSpecs();
}

/// Find or create the fake constructor we synthesize to model constructing an
/// object of a derived class via a constructor of a base class.
CXXConstructorDecl *
Sema::findInheritingConstructor(SourceLocation Loc,
                                CXXConstructorDecl *BaseCtor,
                                ConstructorUsingShadowDecl *Shadow) {
  CXXRecordDecl *Derived = Shadow->getParent();
  SourceLocation UsingLoc = Shadow->getLocation();

  // FIXME: Add a new kind of DeclarationName for an inherited constructor.
  // For now we use the name of the base class constructor as a member of the
  // derived class to indicate a (fake) inherited constructor name.
  DeclarationName Name = BaseCtor->getDeclName();

  // Check to see if we already have a fake constructor for this inherited
  // constructor call.
  for (NamedDecl *Ctor : Derived->lookup(Name))
    if (declaresSameEntity(cast<CXXConstructorDecl>(Ctor)
                               ->getInheritedConstructor()
                               .getConstructor(),
                           BaseCtor))
      return cast<CXXConstructorDecl>(Ctor);

  DeclarationNameInfo NameInfo(Name, UsingLoc);
  TypeSourceInfo *TInfo =
      Context.getTrivialTypeSourceInfo(BaseCtor->getType(), UsingLoc);
  FunctionProtoTypeLoc ProtoLoc =
      TInfo->getTypeLoc().IgnoreParens().castAs<FunctionProtoTypeLoc>();

  // Check the inherited constructor is valid and find the list of base classes
  // from which it was inherited.
  InheritedConstructorInfo ICI(*this, Loc, Shadow);

  bool Constexpr = BaseCtor->isConstexpr() &&
                   defaultedSpecialMemberIsConstexpr(
                       *this, Derived, CXXSpecialMemberKind::DefaultConstructor,
                       false, BaseCtor, &ICI);

  CXXConstructorDecl *DerivedCtor = CXXConstructorDecl::Create(
      Context, Derived, UsingLoc, NameInfo, TInfo->getType(), TInfo,
      BaseCtor->getExplicitSpecifier(), getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true,
      /*isImplicitlyDeclared=*/true,
      Constexpr ? BaseCtor->getConstexprKind() : ConstexprSpecKind::Unspecified,
      InheritedConstructor(Shadow, BaseCtor),
      BaseCtor->getTrailingRequiresClause());
  if (Shadow->isInvalidDecl())
    DerivedCtor->setInvalidDecl();

  // Build an unevaluated exception specification for this fake constructor.
  const FunctionProtoType *FPT = TInfo->getType()->castAs<FunctionProtoType>();
  FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
  EPI.ExceptionSpec.Type = EST_Unevaluated;
  EPI.ExceptionSpec.SourceDecl = DerivedCtor;
  DerivedCtor->setType(Context.getFunctionType(FPT->getReturnType(),
                                               FPT->getParamTypes(), EPI));

  // Build the parameter declarations.
  SmallVector<ParmVarDecl *, 16> ParamDecls;
  for (unsigned I = 0, N = FPT->getNumParams(); I != N; ++I) {
    TypeSourceInfo *TInfo =
        Context.getTrivialTypeSourceInfo(FPT->getParamType(I), UsingLoc);
    ParmVarDecl *PD = ParmVarDecl::Create(
        Context, DerivedCtor, UsingLoc, UsingLoc, /*IdentifierInfo=*/nullptr,
        FPT->getParamType(I), TInfo, SC_None, /*DefArg=*/nullptr);
    PD->setScopeInfo(0, I);
    PD->setImplicit();
    // Ensure attributes are propagated onto parameters (this matters for
    // format, pass_object_size, ...).
    mergeDeclAttributes(PD, BaseCtor->getParamDecl(I));
    ParamDecls.push_back(PD);
    ProtoLoc.setParam(I, PD);
  }

  // Set up the new constructor.
  assert(!BaseCtor->isDeleted() && "should not use deleted constructor");
  DerivedCtor->setAccess(BaseCtor->getAccess());
  DerivedCtor->setParams(ParamDecls);
  Derived->addDecl(DerivedCtor);

  if (ShouldDeleteSpecialMember(DerivedCtor,
                                CXXSpecialMemberKind::DefaultConstructor, &ICI))
    SetDeclDeleted(DerivedCtor, UsingLoc);

  return DerivedCtor;
}

void Sema::NoteDeletedInheritingConstructor(CXXConstructorDecl *Ctor) {
  InheritedConstructorInfo ICI(*this, Ctor->getLocation(),
                               Ctor->getInheritedConstructor().getShadowDecl());
  ShouldDeleteSpecialMember(Ctor, CXXSpecialMemberKind::DefaultConstructor,
                            &ICI,
                            /*Diagnose*/ true);
}

void Sema::DefineInheritingConstructor(SourceLocation CurrentLocation,
                                       CXXConstructorDecl *Constructor) {
  CXXRecordDecl *ClassDecl = Constructor->getParent();
  assert(Constructor->getInheritedConstructor() &&
         !Constructor->doesThisDeclarationHaveABody() &&
         !Constructor->isDeleted());
  if (Constructor->willHaveBody() || Constructor->isInvalidDecl())
    return;

  // Initializations are performed "as if by a defaulted default constructor",
  // so enter the appropriate scope.
  SynthesizedFunctionScope Scope(*this, Constructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       Constructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  ConstructorUsingShadowDecl *Shadow =
      Constructor->getInheritedConstructor().getShadowDecl();
  CXXConstructorDecl *InheritedCtor =
      Constructor->getInheritedConstructor().getConstructor();

  // [class.inhctor.init]p1:
  //   initialization proceeds as if a defaulted default constructor is used to
  //   initialize the D object and each base class subobject from which the
  //   constructor was inherited

  InheritedConstructorInfo ICI(*this, CurrentLocation, Shadow);
  CXXRecordDecl *RD = Shadow->getParent();
  SourceLocation InitLoc = Shadow->getLocation();

  // Build explicit initializers for all base classes from which the
  // constructor was inherited.
  SmallVector<CXXCtorInitializer*, 8> Inits;
  for (bool VBase : {false, true}) {
    for (CXXBaseSpecifier &B : VBase ? RD->vbases() : RD->bases()) {
      if (B.isVirtual() != VBase)
        continue;

      auto *BaseRD = B.getType()->getAsCXXRecordDecl();
      if (!BaseRD)
        continue;

      auto BaseCtor = ICI.findConstructorForBase(BaseRD, InheritedCtor);
      if (!BaseCtor.first)
        continue;

      MarkFunctionReferenced(CurrentLocation, BaseCtor.first);
      ExprResult Init = new (Context) CXXInheritedCtorInitExpr(
          InitLoc, B.getType(), BaseCtor.first, VBase, BaseCtor.second);

      auto *TInfo = Context.getTrivialTypeSourceInfo(B.getType(), InitLoc);
      Inits.push_back(new (Context) CXXCtorInitializer(
          Context, TInfo, VBase, InitLoc, Init.get(), InitLoc,
          SourceLocation()));
    }
  }

  // We now proceed as if for a defaulted default constructor, with the relevant
  // initializers replaced.

  if (SetCtorInitializers(Constructor, /*AnyErrors*/false, Inits)) {
    Constructor->setInvalidDecl();
    return;
  }

  Constructor->setBody(new (Context) CompoundStmt(InitLoc));
  Constructor->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Constructor);
  }

  DiagnoseUninitializedFields(*this, Constructor);
}

CXXDestructorDecl *Sema::DeclareImplicitDestructor(CXXRecordDecl *ClassDecl) {
  // C++ [class.dtor]p2:
  //   If a class has no user-declared destructor, a destructor is
  //   declared implicitly. An implicitly-declared destructor is an
  //   inline public member of its class.
  assert(ClassDecl->needsImplicitDestructor());

  DeclaringSpecialMember DSM(*this, ClassDecl,
                             CXXSpecialMemberKind::Destructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  bool Constexpr = defaultedSpecialMemberIsConstexpr(
      *this, ClassDecl, CXXSpecialMemberKind::Destructor, false);

  // Create the actual destructor declaration.
  CanQualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationName Name
    = Context.DeclarationNames.getCXXDestructorName(ClassType);
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXDestructorDecl *Destructor = CXXDestructorDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, QualType(), nullptr,
      getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true,
      /*isImplicitlyDeclared=*/true,
      Constexpr ? ConstexprSpecKind::Constexpr
                : ConstexprSpecKind::Unspecified);
  Destructor->setAccess(AS_public);
  Destructor->setDefaulted();

  setupImplicitSpecialMemberType(Destructor, Context.VoidTy, std::nullopt);

  if (getLangOpts().CUDA)
    CUDA().inferTargetForImplicitSpecialMember(
        ClassDecl, CXXSpecialMemberKind::Destructor, Destructor,
        /* ConstRHS */ false,
        /* Diagnose */ false);

  // We don't need to use SpecialMemberIsTrivial here; triviality for
  // destructors is easy to compute.
  Destructor->setTrivial(ClassDecl->hasTrivialDestructor());
  Destructor->setTrivialForCall(ClassDecl->hasAttr<TrivialABIAttr>() ||
                                ClassDecl->hasTrivialDestructorForCall());

  // Note that we have declared this destructor.
  ++getASTContext().NumImplicitDestructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, Destructor);

  // We can't check whether an implicit destructor is deleted before we complete
  // the definition of the class, because its validity depends on the alignment
  // of the class. We'll check this from ActOnFields once the class is complete.
  if (ClassDecl->isCompleteDefinition() &&
      ShouldDeleteSpecialMember(Destructor, CXXSpecialMemberKind::Destructor))
    SetDeclDeleted(Destructor, ClassLoc);

  // Introduce this destructor into its scope.
  if (S)
    PushOnScopeChains(Destructor, S, false);
  ClassDecl->addDecl(Destructor);

  return Destructor;
}

void Sema::DefineImplicitDestructor(SourceLocation CurrentLocation,
                                    CXXDestructorDecl *Destructor) {
  assert((Destructor->isDefaulted() &&
          !Destructor->doesThisDeclarationHaveABody() &&
          !Destructor->isDeleted()) &&
         "DefineImplicitDestructor - call it for implicit default dtor");
  if (Destructor->willHaveBody() || Destructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = Destructor->getParent();
  assert(ClassDecl && "DefineImplicitDestructor - invalid destructor");

  SynthesizedFunctionScope Scope(*this, Destructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       Destructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  MarkBaseAndMemberDestructorsReferenced(Destructor->getLocation(),
                                         Destructor->getParent());

  if (CheckDestructor(Destructor)) {
    Destructor->setInvalidDecl();
    return;
  }

  SourceLocation Loc = Destructor->getEndLoc().isValid()
                           ? Destructor->getEndLoc()
                           : Destructor->getLocation();
  Destructor->setBody(new (Context) CompoundStmt(Loc));
  Destructor->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Destructor);
  }
}

void Sema::CheckCompleteDestructorVariant(SourceLocation CurrentLocation,
                                          CXXDestructorDecl *Destructor) {
  if (Destructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = Destructor->getParent();
  assert(Context.getTargetInfo().getCXXABI().isMicrosoft() &&
         "implicit complete dtors unneeded outside MS ABI");
  assert(ClassDecl->getNumVBases() > 0 &&
         "complete dtor only exists for classes with vbases");

  SynthesizedFunctionScope Scope(*this, Destructor);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  MarkVirtualBaseDestructorsReferenced(Destructor->getLocation(), ClassDecl);
}

void Sema::ActOnFinishCXXMemberDecls() {
  // If the context is an invalid C++ class, just suppress these checks.
  if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(CurContext)) {
    if (Record->isInvalidDecl()) {
      DelayedOverridingExceptionSpecChecks.clear();
      DelayedEquivalentExceptionSpecChecks.clear();
      return;
    }
    checkForMultipleExportedDefaultConstructors(*this, Record);
  }
}

void Sema::ActOnFinishCXXNonNestedClass() {
  referenceDLLExportedClassMethods();

  if (!DelayedDllExportMemberFunctions.empty()) {
    SmallVector<CXXMethodDecl*, 4> WorkList;
    std::swap(DelayedDllExportMemberFunctions, WorkList);
    for (CXXMethodDecl *M : WorkList) {
      DefineDefaultedFunction(*this, M, M->getLocation());

      // Pass the method to the consumer to get emitted. This is not necessary
      // for explicit instantiation definitions, as they will get emitted
      // anyway.
      if (M->getParent()->getTemplateSpecializationKind() !=
          TSK_ExplicitInstantiationDefinition)
        ActOnFinishInlineFunctionDef(M);
    }
  }
}

void Sema::referenceDLLExportedClassMethods() {
  if (!DelayedDllExportClasses.empty()) {
    // Calling ReferenceDllExportedMembers might cause the current function to
    // be called again, so use a local copy of DelayedDllExportClasses.
    SmallVector<CXXRecordDecl *, 4> WorkList;
    std::swap(DelayedDllExportClasses, WorkList);
    for (CXXRecordDecl *Class : WorkList)
      ReferenceDllExportedMembers(*this, Class);
  }
}

void Sema::AdjustDestructorExceptionSpec(CXXDestructorDecl *Destructor) {
  assert(getLangOpts().CPlusPlus11 &&
         "adjusting dtor exception specs was introduced in c++11");

  if (Destructor->isDependentContext())
    return;

  // C++11 [class.dtor]p3:
  //   A declaration of a destructor that does not have an exception-
  //   specification is implicitly considered to have the same exception-
  //   specification as an implicit declaration.
  const auto *DtorType = Destructor->getType()->castAs<FunctionProtoType>();
  if (DtorType->hasExceptionSpec())
    return;

  // Replace the destructor's type, building off the existing one. Fortunately,
  // the only thing of interest in the destructor type is its extended info.
  // The return and arguments are fixed.
  FunctionProtoType::ExtProtoInfo EPI = DtorType->getExtProtoInfo();
  EPI.ExceptionSpec.Type = EST_Unevaluated;
  EPI.ExceptionSpec.SourceDecl = Destructor;
  Destructor->setType(
      Context.getFunctionType(Context.VoidTy, std::nullopt, EPI));

  // FIXME: If the destructor has a body that could throw, and the newly created
  // spec doesn't allow exceptions, we should emit a warning, because this
  // change in behavior can break conforming C++03 programs at runtime.
  // However, we don't have a body or an exception specification yet, so it
  // needs to be done somewhere else.
}

namespace {
/// An abstract base class for all helper classes used in building the
//  copy/move operators. These classes serve as factory functions and help us
//  avoid using the same Expr* in the AST twice.
class ExprBuilder {
  ExprBuilder(const ExprBuilder&) = delete;
  ExprBuilder &operator=(const ExprBuilder&) = delete;

protected:
  static Expr *assertNotNull(Expr *E) {
    assert(E && "Expression construction must not fail.");
    return E;
  }

public:
  ExprBuilder() {}
  virtual ~ExprBuilder() {}

  virtual Expr *build(Sema &S, SourceLocation Loc) const = 0;
};

class RefBuilder: public ExprBuilder {
  VarDecl *Var;
  QualType VarType;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.BuildDeclRefExpr(Var, VarType, VK_LValue, Loc));
  }

  RefBuilder(VarDecl *Var, QualType VarType)
      : Var(Var), VarType(VarType) {}
};

class ThisBuilder: public ExprBuilder {
public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.ActOnCXXThis(Loc).getAs<Expr>());
  }
};

class CastBuilder: public ExprBuilder {
  const ExprBuilder &Builder;
  QualType Type;
  ExprValueKind Kind;
  const CXXCastPath &Path;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.ImpCastExprToType(Builder.build(S, Loc), Type,
                                             CK_UncheckedDerivedToBase, Kind,
                                             &Path).get());
  }

  CastBuilder(const ExprBuilder &Builder, QualType Type, ExprValueKind Kind,
              const CXXCastPath &Path)
      : Builder(Builder), Type(Type), Kind(Kind), Path(Path) {}
};

class DerefBuilder: public ExprBuilder {
  const ExprBuilder &Builder;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(
        S.CreateBuiltinUnaryOp(Loc, UO_Deref, Builder.build(S, Loc)).get());
  }

  DerefBuilder(const ExprBuilder &Builder) : Builder(Builder) {}
};

class MemberBuilder: public ExprBuilder {
  const ExprBuilder &Builder;
  QualType Type;
  CXXScopeSpec SS;
  bool IsArrow;
  LookupResult &MemberLookup;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.BuildMemberReferenceExpr(
        Builder.build(S, Loc), Type, Loc, IsArrow, SS, SourceLocation(),
        nullptr, MemberLookup, nullptr, nullptr).get());
  }

  MemberBuilder(const ExprBuilder &Builder, QualType Type, bool IsArrow,
                LookupResult &MemberLookup)
      : Builder(Builder), Type(Type), IsArrow(IsArrow),
        MemberLookup(MemberLookup) {}
};

class MoveCastBuilder: public ExprBuilder {
  const ExprBuilder &Builder;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(CastForMoving(S, Builder.build(S, Loc)));
  }

  MoveCastBuilder(const ExprBuilder &Builder) : Builder(Builder) {}
};

class LvalueConvBuilder: public ExprBuilder {
  const ExprBuilder &Builder;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(
        S.DefaultLvalueConversion(Builder.build(S, Loc)).get());
  }

  LvalueConvBuilder(const ExprBuilder &Builder) : Builder(Builder) {}
};

class SubscriptBuilder: public ExprBuilder {
  const ExprBuilder &Base;
  const ExprBuilder &Index;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.CreateBuiltinArraySubscriptExpr(
        Base.build(S, Loc), Loc, Index.build(S, Loc), Loc).get());
  }

  SubscriptBuilder(const ExprBuilder &Base, const ExprBuilder &Index)
      : Base(Base), Index(Index) {}
};

} // end anonymous namespace

/// When generating a defaulted copy or move assignment operator, if a field
/// should be copied with __builtin_memcpy rather than via explicit assignments,
/// do so. This optimization only applies for arrays of scalars, and for arrays
/// of class type where the selected copy/move-assignment operator is trivial.
static StmtResult
buildMemcpyForAssignmentOp(Sema &S, SourceLocation Loc, QualType T,
                           const ExprBuilder &ToB, const ExprBuilder &FromB) {
  // Compute the size of the memory buffer to be copied.
  QualType SizeType = S.Context.getSizeType();
  llvm::APInt Size(S.Context.getTypeSize(SizeType),
                   S.Context.getTypeSizeInChars(T).getQuantity());

  // Take the address of the field references for "from" and "to". We
  // directly construct UnaryOperators here because semantic analysis
  // does not permit us to take the address of an xvalue.
  Expr *From = FromB.build(S, Loc);
  From = UnaryOperator::Create(
      S.Context, From, UO_AddrOf, S.Context.getPointerType(From->getType()),
      VK_PRValue, OK_Ordinary, Loc, false, S.CurFPFeatureOverrides());
  Expr *To = ToB.build(S, Loc);
  To = UnaryOperator::Create(
      S.Context, To, UO_AddrOf, S.Context.getPointerType(To->getType()),
      VK_PRValue, OK_Ordinary, Loc, false, S.CurFPFeatureOverrides());

  const Type *E = T->getBaseElementTypeUnsafe();
  bool NeedsCollectableMemCpy =
      E->isRecordType() &&
      E->castAs<RecordType>()->getDecl()->hasObjectMember();

  // Create a reference to the __builtin_objc_memmove_collectable function
  StringRef MemCpyName = NeedsCollectableMemCpy ?
    "__builtin_objc_memmove_collectable" :
    "__builtin_memcpy";
  LookupResult R(S, &S.Context.Idents.get(MemCpyName), Loc,
                 Sema::LookupOrdinaryName);
  S.LookupName(R, S.TUScope, true);

  FunctionDecl *MemCpy = R.getAsSingle<FunctionDecl>();
  if (!MemCpy)
    // Something went horribly wrong earlier, and we will have complained
    // about it.
    return StmtError();

  ExprResult MemCpyRef = S.BuildDeclRefExpr(MemCpy, S.Context.BuiltinFnTy,
                                            VK_PRValue, Loc, nullptr);
  assert(MemCpyRef.isUsable() && "Builtin reference cannot fail");

  Expr *CallArgs[] = {
    To, From, IntegerLiteral::Create(S.Context, Size, SizeType, Loc)
  };
  ExprResult Call = S.BuildCallExpr(/*Scope=*/nullptr, MemCpyRef.get(),
                                    Loc, CallArgs, Loc);

  assert(!Call.isInvalid() && "Call to __builtin_memcpy cannot fail!");
  return Call.getAs<Stmt>();
}

/// Builds a statement that copies/moves the given entity from \p From to
/// \c To.
///
/// This routine is used to copy/move the members of a class with an
/// implicitly-declared copy/move assignment operator. When the entities being
/// copied are arrays, this routine builds for loops to copy them.
///
/// \param S The Sema object used for type-checking.
///
/// \param Loc The location where the implicit copy/move is being generated.
///
/// \param T The type of the expressions being copied/moved. Both expressions
/// must have this type.
///
/// \param To The expression we are copying/moving to.
///
/// \param From The expression we are copying/moving from.
///
/// \param CopyingBaseSubobject Whether we're copying/moving a base subobject.
/// Otherwise, it's a non-static member subobject.
///
/// \param Copying Whether we're copying or moving.
///
/// \param Depth Internal parameter recording the depth of the recursion.
///
/// \returns A statement or a loop that copies the expressions, or StmtResult(0)
/// if a memcpy should be used instead.
static StmtResult
buildSingleCopyAssignRecursively(Sema &S, SourceLocation Loc, QualType T,
                                 const ExprBuilder &To, const ExprBuilder &From,
                                 bool CopyingBaseSubobject, bool Copying,
                                 unsigned Depth = 0) {
  // C++11 [class.copy]p28:
  //   Each subobject is assigned in the manner appropriate to its type:
  //
  //     - if the subobject is of class type, as if by a call to operator= with
  //       the subobject as the object expression and the corresponding
  //       subobject of x as a single function argument (as if by explicit
  //       qualification; that is, ignoring any possible virtual overriding
  //       functions in more derived classes);
  //
  // C++03 [class.copy]p13:
  //     - if the subobject is of class type, the copy assignment operator for
  //       the class is used (as if by explicit qualification; that is,
  //       ignoring any possible virtual overriding functions in more derived
  //       classes);
  if (const RecordType *RecordTy = T->getAs<RecordType>()) {
    CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(RecordTy->getDecl());

    // Look for operator=.
    DeclarationName Name
      = S.Context.DeclarationNames.getCXXOperatorName(OO_Equal);
    LookupResult OpLookup(S, Name, Loc, Sema::LookupOrdinaryName);
    S.LookupQualifiedName(OpLookup, ClassDecl, false);

    // Prior to C++11, filter out any result that isn't a copy/move-assignment
    // operator.
    if (!S.getLangOpts().CPlusPlus11) {
      LookupResult::Filter F = OpLookup.makeFilter();
      while (F.hasNext()) {
        NamedDecl *D = F.next();
        if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(D))
          if (Method->isCopyAssignmentOperator() ||
              (!Copying && Method->isMoveAssignmentOperator()))
            continue;

        F.erase();
      }
      F.done();
    }

    // Suppress the protected check (C++ [class.protected]) for each of the
    // assignment operators we found. This strange dance is required when
    // we're assigning via a base classes's copy-assignment operator. To
    // ensure that we're getting the right base class subobject (without
    // ambiguities), we need to cast "this" to that subobject type; to
    // ensure that we don't go through the virtual call mechanism, we need
    // to qualify the operator= name with the base class (see below). However,
    // this means that if the base class has a protected copy assignment
    // operator, the protected member access check will fail. So, we
    // rewrite "protected" access to "public" access in this case, since we
    // know by construction that we're calling from a derived class.
    if (CopyingBaseSubobject) {
      for (LookupResult::iterator L = OpLookup.begin(), LEnd = OpLookup.end();
           L != LEnd; ++L) {
        if (L.getAccess() == AS_protected)
          L.setAccess(AS_public);
      }
    }

    // Create the nested-name-specifier that will be used to qualify the
    // reference to operator=; this is required to suppress the virtual
    // call mechanism.
    CXXScopeSpec SS;
    const Type *CanonicalT = S.Context.getCanonicalType(T.getTypePtr());
    SS.MakeTrivial(S.Context,
                   NestedNameSpecifier::Create(S.Context, nullptr, false,
                                               CanonicalT),
                   Loc);

    // Create the reference to operator=.
    ExprResult OpEqualRef
      = S.BuildMemberReferenceExpr(To.build(S, Loc), T, Loc, /*IsArrow=*/false,
                                   SS, /*TemplateKWLoc=*/SourceLocation(),
                                   /*FirstQualifierInScope=*/nullptr,
                                   OpLookup,
                                   /*TemplateArgs=*/nullptr, /*S*/nullptr,
                                   /*SuppressQualifierCheck=*/true);
    if (OpEqualRef.isInvalid())
      return StmtError();

    // Build the call to the assignment operator.

    Expr *FromInst = From.build(S, Loc);
    ExprResult Call = S.BuildCallToMemberFunction(/*Scope=*/nullptr,
                                                  OpEqualRef.getAs<Expr>(),
                                                  Loc, FromInst, Loc);
    if (Call.isInvalid())
      return StmtError();

    // If we built a call to a trivial 'operator=' while copying an array,
    // bail out. We'll replace the whole shebang with a memcpy.
    CXXMemberCallExpr *CE = dyn_cast<CXXMemberCallExpr>(Call.get());
    if (CE && CE->getMethodDecl()->isTrivial() && Depth)
      return StmtResult((Stmt*)nullptr);

    // Convert to an expression-statement, and clean up any produced
    // temporaries.
    return S.ActOnExprStmt(Call);
  }

  //     - if the subobject is of scalar type, the built-in assignment
  //       operator is used.
  const ConstantArrayType *ArrayTy = S.Context.getAsConstantArrayType(T);
  if (!ArrayTy) {
    ExprResult Assignment = S.CreateBuiltinBinOp(
        Loc, BO_Assign, To.build(S, Loc), From.build(S, Loc));
    if (Assignment.isInvalid())
      return StmtError();
    return S.ActOnExprStmt(Assignment);
  }

  //     - if the subobject is an array, each element is assigned, in the
  //       manner appropriate to the element type;

  // Construct a loop over the array bounds, e.g.,
  //
  //   for (__SIZE_TYPE__ i0 = 0; i0 != array-size; ++i0)
  //
  // that will copy each of the array elements.
  QualType SizeType = S.Context.getSizeType();

  // Create the iteration variable.
  IdentifierInfo *IterationVarName = nullptr;
  {
    SmallString<8> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << "__i" << Depth;
    IterationVarName = &S.Context.Idents.get(OS.str());
  }
  VarDecl *IterationVar = VarDecl::Create(S.Context, S.CurContext, Loc, Loc,
                                          IterationVarName, SizeType,
                            S.Context.getTrivialTypeSourceInfo(SizeType, Loc),
                                          SC_None);

  // Initialize the iteration variable to zero.
  llvm::APInt Zero(S.Context.getTypeSize(SizeType), 0);
  IterationVar->setInit(IntegerLiteral::Create(S.Context, Zero, SizeType, Loc));

  // Creates a reference to the iteration variable.
  RefBuilder IterationVarRef(IterationVar, SizeType);
  LvalueConvBuilder IterationVarRefRVal(IterationVarRef);

  // Create the DeclStmt that holds the iteration variable.
  Stmt *InitStmt = new (S.Context) DeclStmt(DeclGroupRef(IterationVar),Loc,Loc);

  // Subscript the "from" and "to" expressions with the iteration variable.
  SubscriptBuilder FromIndexCopy(From, IterationVarRefRVal);
  MoveCastBuilder FromIndexMove(FromIndexCopy);
  const ExprBuilder *FromIndex;
  if (Copying)
    FromIndex = &FromIndexCopy;
  else
    FromIndex = &FromIndexMove;

  SubscriptBuilder ToIndex(To, IterationVarRefRVal);

  // Build the copy/move for an individual element of the array.
  StmtResult Copy =
    buildSingleCopyAssignRecursively(S, Loc, ArrayTy->getElementType(),
                                     ToIndex, *FromIndex, CopyingBaseSubobject,
                                     Copying, Depth + 1);
  // Bail out if copying fails or if we determined that we should use memcpy.
  if (Copy.isInvalid() || !Copy.get())
    return Copy;

  // Create the comparison against the array bound.
  llvm::APInt Upper
    = ArrayTy->getSize().zextOrTrunc(S.Context.getTypeSize(SizeType));
  Expr *Comparison = BinaryOperator::Create(
      S.Context, IterationVarRefRVal.build(S, Loc),
      IntegerLiteral::Create(S.Context, Upper, SizeType, Loc), BO_NE,
      S.Context.BoolTy, VK_PRValue, OK_Ordinary, Loc,
      S.CurFPFeatureOverrides());

  // Create the pre-increment of the iteration variable. We can determine
  // whether the increment will overflow based on the value of the array
  // bound.
  Expr *Increment = UnaryOperator::Create(
      S.Context, IterationVarRef.build(S, Loc), UO_PreInc, SizeType, VK_LValue,
      OK_Ordinary, Loc, Upper.isMaxValue(), S.CurFPFeatureOverrides());

  // Construct the loop that copies all elements of this array.
  return S.ActOnForStmt(
      Loc, Loc, InitStmt,
      S.ActOnCondition(nullptr, Loc, Comparison, Sema::ConditionKind::Boolean),
      S.MakeFullDiscardedValueExpr(Increment), Loc, Copy.get());
}

static StmtResult
buildSingleCopyAssign(Sema &S, SourceLocation Loc, QualType T,
                      const ExprBuilder &To, const ExprBuilder &From,
                      bool CopyingBaseSubobject, bool Copying) {
  // Maybe we should use a memcpy?
  if (T->isArrayType() && !T.isConstQualified() && !T.isVolatileQualified() &&
      T.isTriviallyCopyableType(S.Context))
    return buildMemcpyForAssignmentOp(S, Loc, T, To, From);

  StmtResult Result(buildSingleCopyAssignRecursively(S, Loc, T, To, From,
                                                     CopyingBaseSubobject,
                                                     Copying, 0));

  // If we ended up picking a trivial assignment operator for an array of a
  // non-trivially-copyable class type, just emit a memcpy.
  if (!Result.isInvalid() && !Result.get())
    return buildMemcpyForAssignmentOp(S, Loc, T, To, From);

  return Result;
}

CXXMethodDecl *Sema::DeclareImplicitCopyAssignment(CXXRecordDecl *ClassDecl) {
  // Note: The following rules are largely analoguous to the copy
  // constructor rules. Note that virtual bases are not taken into account
  // for determining the argument type of the operator. Note also that
  // operators taking an object instead of a reference are allowed.
  assert(ClassDecl->needsImplicitCopyAssignment());

  DeclaringSpecialMember DSM(*this, ClassDecl,
                             CXXSpecialMemberKind::CopyAssignment);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  QualType ArgType = Context.getTypeDeclType(ClassDecl);
  ArgType = Context.getElaboratedType(ElaboratedTypeKeyword::None, nullptr,
                                      ArgType, nullptr);
  LangAS AS = getDefaultCXXMethodAddrSpace();
  if (AS != LangAS::Default)
    ArgType = Context.getAddrSpaceQualType(ArgType, AS);
  QualType RetType = Context.getLValueReferenceType(ArgType);
  bool Const = ClassDecl->implicitCopyAssignmentHasConstParam();
  if (Const)
    ArgType = ArgType.withConst();

  ArgType = Context.getLValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(
      *this, ClassDecl, CXXSpecialMemberKind::CopyAssignment, Const);

  //   An implicitly-declared copy assignment operator is an inline public
  //   member of its class.
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXMethodDecl *CopyAssignment = CXXMethodDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, QualType(),
      /*TInfo=*/nullptr, /*StorageClass=*/SC_None,
      getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true,
      Constexpr ? ConstexprSpecKind::Constexpr : ConstexprSpecKind::Unspecified,
      SourceLocation());
  CopyAssignment->setAccess(AS_public);
  CopyAssignment->setDefaulted();
  CopyAssignment->setImplicit();

  setupImplicitSpecialMemberType(CopyAssignment, RetType, ArgType);

  if (getLangOpts().CUDA)
    CUDA().inferTargetForImplicitSpecialMember(
        ClassDecl, CXXSpecialMemberKind::CopyAssignment, CopyAssignment,
        /* ConstRHS */ Const,
        /* Diagnose */ false);

  // Add the parameter to the operator.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, CopyAssignment,
                                               ClassLoc, ClassLoc,
                                               /*Id=*/nullptr, ArgType,
                                               /*TInfo=*/nullptr, SC_None,
                                               nullptr);
  CopyAssignment->setParams(FromParam);

  CopyAssignment->setTrivial(
      ClassDecl->needsOverloadResolutionForCopyAssignment()
          ? SpecialMemberIsTrivial(CopyAssignment,
                                   CXXSpecialMemberKind::CopyAssignment)
          : ClassDecl->hasTrivialCopyAssignment());

  // Note that we have added this copy-assignment operator.
  ++getASTContext().NumImplicitCopyAssignmentOperatorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, CopyAssignment);

  if (ShouldDeleteSpecialMember(CopyAssignment,
                                CXXSpecialMemberKind::CopyAssignment)) {
    ClassDecl->setImplicitCopyAssignmentIsDeleted();
    SetDeclDeleted(CopyAssignment, ClassLoc);
  }

  if (S)
    PushOnScopeChains(CopyAssignment, S, false);
  ClassDecl->addDecl(CopyAssignment);

  return CopyAssignment;
}

/// Diagnose an implicit copy operation for a class which is odr-used, but
/// which is deprecated because the class has a user-declared copy constructor,
/// copy assignment operator, or destructor.
static void diagnoseDeprecatedCopyOperation(Sema &S, CXXMethodDecl *CopyOp) {
  assert(CopyOp->isImplicit());

  CXXRecordDecl *RD = CopyOp->getParent();
  CXXMethodDecl *UserDeclaredOperation = nullptr;

  if (RD->hasUserDeclaredDestructor()) {
    UserDeclaredOperation = RD->getDestructor();
  } else if (!isa<CXXConstructorDecl>(CopyOp) &&
             RD->hasUserDeclaredCopyConstructor()) {
    // Find any user-declared copy constructor.
    for (auto *I : RD->ctors()) {
      if (I->isCopyConstructor()) {
        UserDeclaredOperation = I;
        break;
      }
    }
    assert(UserDeclaredOperation);
  } else if (isa<CXXConstructorDecl>(CopyOp) &&
             RD->hasUserDeclaredCopyAssignment()) {
    // Find any user-declared move assignment operator.
    for (auto *I : RD->methods()) {
      if (I->isCopyAssignmentOperator()) {
        UserDeclaredOperation = I;
        break;
      }
    }
    assert(UserDeclaredOperation);
  }

  if (UserDeclaredOperation) {
    bool UDOIsUserProvided = UserDeclaredOperation->isUserProvided();
    bool UDOIsDestructor = isa<CXXDestructorDecl>(UserDeclaredOperation);
    bool IsCopyAssignment = !isa<CXXConstructorDecl>(CopyOp);
    unsigned DiagID =
        (UDOIsUserProvided && UDOIsDestructor)
            ? diag::warn_deprecated_copy_with_user_provided_dtor
        : (UDOIsUserProvided && !UDOIsDestructor)
            ? diag::warn_deprecated_copy_with_user_provided_copy
        : (!UDOIsUserProvided && UDOIsDestructor)
            ? diag::warn_deprecated_copy_with_dtor
            : diag::warn_deprecated_copy;
    S.Diag(UserDeclaredOperation->getLocation(), DiagID)
        << RD << IsCopyAssignment;
  }
}

void Sema::DefineImplicitCopyAssignment(SourceLocation CurrentLocation,
                                        CXXMethodDecl *CopyAssignOperator) {
  assert((CopyAssignOperator->isDefaulted() &&
          CopyAssignOperator->isOverloadedOperator() &&
          CopyAssignOperator->getOverloadedOperator() == OO_Equal &&
          !CopyAssignOperator->doesThisDeclarationHaveABody() &&
          !CopyAssignOperator->isDeleted()) &&
         "DefineImplicitCopyAssignment called for wrong function");
  if (CopyAssignOperator->willHaveBody() || CopyAssignOperator->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = CopyAssignOperator->getParent();
  if (ClassDecl->isInvalidDecl()) {
    CopyAssignOperator->setInvalidDecl();
    return;
  }

  SynthesizedFunctionScope Scope(*this, CopyAssignOperator);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       CopyAssignOperator->getType()->castAs<FunctionProtoType>());

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  // C++11 [class.copy]p18:
  //   The [definition of an implicitly declared copy assignment operator] is
  //   deprecated if the class has a user-declared copy constructor or a
  //   user-declared destructor.
  if (getLangOpts().CPlusPlus11 && CopyAssignOperator->isImplicit())
    diagnoseDeprecatedCopyOperation(*this, CopyAssignOperator);

  // C++0x [class.copy]p30:
  //   The implicitly-defined or explicitly-defaulted copy assignment operator
  //   for a non-union class X performs memberwise copy assignment of its
  //   subobjects. The direct base classes of X are assigned first, in the
  //   order of their declaration in the base-specifier-list, and then the
  //   immediate non-static data members of X are assigned, in the order in
  //   which they were declared in the class definition.

  // The statements that form the synthesized function body.
  SmallVector<Stmt*, 8> Statements;

  // The parameter for the "other" object, which we are copying from.
  ParmVarDecl *Other = CopyAssignOperator->getNonObjectParameter(0);
  Qualifiers OtherQuals = Other->getType().getQualifiers();
  QualType OtherRefType = Other->getType();
  if (OtherRefType->isLValueReferenceType()) {
    OtherRefType = OtherRefType->getPointeeType();
    OtherQuals = OtherRefType.getQualifiers();
  }

  // Our location for everything implicitly-generated.
  SourceLocation Loc = CopyAssignOperator->getEndLoc().isValid()
                           ? CopyAssignOperator->getEndLoc()
                           : CopyAssignOperator->getLocation();

  // Builds a DeclRefExpr for the "other" object.
  RefBuilder OtherRef(Other, OtherRefType);

  // Builds the function object parameter.
  std::optional<ThisBuilder> This;
  std::optional<DerefBuilder> DerefThis;
  std::optional<RefBuilder> ExplicitObject;
  bool IsArrow = false;
  QualType ObjectType;
  if (CopyAssignOperator->isExplicitObjectMemberFunction()) {
    ObjectType = CopyAssignOperator->getParamDecl(0)->getType();
    if (ObjectType->isReferenceType())
      ObjectType = ObjectType->getPointeeType();
    ExplicitObject.emplace(CopyAssignOperator->getParamDecl(0), ObjectType);
  } else {
    ObjectType = getCurrentThisType();
    This.emplace();
    DerefThis.emplace(*This);
    IsArrow = !LangOpts.HLSL;
  }
  ExprBuilder &ObjectParameter =
      ExplicitObject ? static_cast<ExprBuilder &>(*ExplicitObject)
                     : static_cast<ExprBuilder &>(*This);

  // Assign base classes.
  bool Invalid = false;
  for (auto &Base : ClassDecl->bases()) {
    // Form the assignment:
    //   static_cast<Base*>(this)->Base::operator=(static_cast<Base&>(other));
    QualType BaseType = Base.getType().getUnqualifiedType();
    if (!BaseType->isRecordType()) {
      Invalid = true;
      continue;
    }

    CXXCastPath BasePath;
    BasePath.push_back(&Base);

    // Construct the "from" expression, which is an implicit cast to the
    // appropriately-qualified base type.
    CastBuilder From(OtherRef, Context.getQualifiedType(BaseType, OtherQuals),
                     VK_LValue, BasePath);

    // Dereference "this".
    CastBuilder To(
        ExplicitObject ? static_cast<ExprBuilder &>(*ExplicitObject)
                       : static_cast<ExprBuilder &>(*DerefThis),
        Context.getQualifiedType(BaseType, ObjectType.getQualifiers()),
        VK_LValue, BasePath);

    // Build the copy.
    StmtResult Copy = buildSingleCopyAssign(*this, Loc, BaseType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/true,
                                            /*Copying=*/true);
    if (Copy.isInvalid()) {
      CopyAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the copy.
    Statements.push_back(Copy.getAs<Expr>());
  }

  // Assign non-static members.
  for (auto *Field : ClassDecl->fields()) {
    // FIXME: We should form some kind of AST representation for the implied
    // memcpy in a union copy operation.
    if (Field->isUnnamedBitField() || Field->getParent()->isUnion())
      continue;

    if (Field->isInvalidDecl()) {
      Invalid = true;
      continue;
    }

    // Check for members of reference type; we can't copy those.
    if (Field->getType()->isReferenceType()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 0 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Check for members of const-qualified, non-class type.
    QualType BaseType = Context.getBaseElementType(Field->getType());
    if (!BaseType->getAs<RecordType>() && BaseType.isConstQualified()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 1 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Suppress assigning zero-width bitfields.
    if (Field->isZeroLengthBitField(Context))
      continue;

    QualType FieldType = Field->getType().getNonReferenceType();
    if (FieldType->isIncompleteArrayType()) {
      assert(ClassDecl->hasFlexibleArrayMember() &&
             "Incomplete array type is not valid");
      continue;
    }

    // Build references to the field in the object we're copying from and to.
    CXXScopeSpec SS; // Intentionally empty
    LookupResult MemberLookup(*this, Field->getDeclName(), Loc,
                              LookupMemberName);
    MemberLookup.addDecl(Field);
    MemberLookup.resolveKind();

    MemberBuilder From(OtherRef, OtherRefType, /*IsArrow=*/false, MemberLookup);
    MemberBuilder To(ObjectParameter, ObjectType, IsArrow, MemberLookup);
    // Build the copy of this field.
    StmtResult Copy = buildSingleCopyAssign(*this, Loc, FieldType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/false,
                                            /*Copying=*/true);
    if (Copy.isInvalid()) {
      CopyAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the copy.
    Statements.push_back(Copy.getAs<Stmt>());
  }

  if (!Invalid) {
    // Add a "return *this;"
    Expr *ThisExpr =
        (ExplicitObject  ? static_cast<ExprBuilder &>(*ExplicitObject)
         : LangOpts.HLSL ? static_cast<ExprBuilder &>(*This)
                         : static_cast<ExprBuilder &>(*DerefThis))
            .build(*this, Loc);
    StmtResult Return = BuildReturnStmt(Loc, ThisExpr);
    if (Return.isInvalid())
      Invalid = true;
    else
      Statements.push_back(Return.getAs<Stmt>());
  }

  if (Invalid) {
    CopyAssignOperator->setInvalidDecl();
    return;
  }

  StmtResult Body;
  {
    CompoundScopeRAII CompoundScope(*this);
    Body = ActOnCompoundStmt(Loc, Loc, Statements,
                             /*isStmtExpr=*/false);
    assert(!Body.isInvalid() && "Compound statement creation cannot fail");
  }
  CopyAssignOperator->setBody(Body.getAs<Stmt>());
  CopyAssignOperator->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(CopyAssignOperator);
  }
}

CXXMethodDecl *Sema::DeclareImplicitMoveAssignment(CXXRecordDecl *ClassDecl) {
  assert(ClassDecl->needsImplicitMoveAssignment());

  DeclaringSpecialMember DSM(*this, ClassDecl,
                             CXXSpecialMemberKind::MoveAssignment);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  // Note: The following rules are largely analoguous to the move
  // constructor rules.

  QualType ArgType = Context.getTypeDeclType(ClassDecl);
  ArgType = Context.getElaboratedType(ElaboratedTypeKeyword::None, nullptr,
                                      ArgType, nullptr);
  LangAS AS = getDefaultCXXMethodAddrSpace();
  if (AS != LangAS::Default)
    ArgType = Context.getAddrSpaceQualType(ArgType, AS);
  QualType RetType = Context.getLValueReferenceType(ArgType);
  ArgType = Context.getRValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(
      *this, ClassDecl, CXXSpecialMemberKind::MoveAssignment, false);

  //   An implicitly-declared move assignment operator is an inline public
  //   member of its class.
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXMethodDecl *MoveAssignment = CXXMethodDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, QualType(),
      /*TInfo=*/nullptr, /*StorageClass=*/SC_None,
      getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true,
      Constexpr ? ConstexprSpecKind::Constexpr : ConstexprSpecKind::Unspecified,
      SourceLocation());
  MoveAssignment->setAccess(AS_public);
  MoveAssignment->setDefaulted();
  MoveAssignment->setImplicit();

  setupImplicitSpecialMemberType(MoveAssignment, RetType, ArgType);

  if (getLangOpts().CUDA)
    CUDA().inferTargetForImplicitSpecialMember(
        ClassDecl, CXXSpecialMemberKind::MoveAssignment, MoveAssignment,
        /* ConstRHS */ false,
        /* Diagnose */ false);

  // Add the parameter to the operator.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, MoveAssignment,
                                               ClassLoc, ClassLoc,
                                               /*Id=*/nullptr, ArgType,
                                               /*TInfo=*/nullptr, SC_None,
                                               nullptr);
  MoveAssignment->setParams(FromParam);

  MoveAssignment->setTrivial(
      ClassDecl->needsOverloadResolutionForMoveAssignment()
          ? SpecialMemberIsTrivial(MoveAssignment,
                                   CXXSpecialMemberKind::MoveAssignment)
          : ClassDecl->hasTrivialMoveAssignment());

  // Note that we have added this copy-assignment operator.
  ++getASTContext().NumImplicitMoveAssignmentOperatorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, MoveAssignment);

  if (ShouldDeleteSpecialMember(MoveAssignment,
                                CXXSpecialMemberKind::MoveAssignment)) {
    ClassDecl->setImplicitMoveAssignmentIsDeleted();
    SetDeclDeleted(MoveAssignment, ClassLoc);
  }

  if (S)
    PushOnScopeChains(MoveAssignment, S, false);
  ClassDecl->addDecl(MoveAssignment);

  return MoveAssignment;
}

/// Check if we're implicitly defining a move assignment operator for a class
/// with virtual bases. Such a move assignment might move-assign the virtual
/// base multiple times.
static void checkMoveAssignmentForRepeatedMove(Sema &S, CXXRecordDecl *Class,
                                               SourceLocation CurrentLocation) {
  assert(!Class->isDependentContext() && "should not define dependent move");

  // Only a virtual base could get implicitly move-assigned multiple times.
  // Only a non-trivial move assignment can observe this. We only want to
  // diagnose if we implicitly define an assignment operator that assigns
  // two base classes, both of which move-assign the same virtual base.
  if (Class->getNumVBases() == 0 || Class->hasTrivialMoveAssignment() ||
      Class->getNumBases() < 2)
    return;

  llvm::SmallVector<CXXBaseSpecifier *, 16> Worklist;
  typedef llvm::DenseMap<CXXRecordDecl*, CXXBaseSpecifier*> VBaseMap;
  VBaseMap VBases;

  for (auto &BI : Class->bases()) {
    Worklist.push_back(&BI);
    while (!Worklist.empty()) {
      CXXBaseSpecifier *BaseSpec = Worklist.pop_back_val();
      CXXRecordDecl *Base = BaseSpec->getType()->getAsCXXRecordDecl();

      // If the base has no non-trivial move assignment operators,
      // we don't care about moves from it.
      if (!Base->hasNonTrivialMoveAssignment())
        continue;

      // If there's nothing virtual here, skip it.
      if (!BaseSpec->isVirtual() && !Base->getNumVBases())
        continue;

      // If we're not actually going to call a move assignment for this base,
      // or the selected move assignment is trivial, skip it.
      Sema::SpecialMemberOverloadResult SMOR =
          S.LookupSpecialMember(Base, CXXSpecialMemberKind::MoveAssignment,
                                /*ConstArg*/ false, /*VolatileArg*/ false,
                                /*RValueThis*/ true, /*ConstThis*/ false,
                                /*VolatileThis*/ false);
      if (!SMOR.getMethod() || SMOR.getMethod()->isTrivial() ||
          !SMOR.getMethod()->isMoveAssignmentOperator())
        continue;

      if (BaseSpec->isVirtual()) {
        // We're going to move-assign this virtual base, and its move
        // assignment operator is not trivial. If this can happen for
        // multiple distinct direct bases of Class, diagnose it. (If it
        // only happens in one base, we'll diagnose it when synthesizing
        // that base class's move assignment operator.)
        CXXBaseSpecifier *&Existing =
            VBases.insert(std::make_pair(Base->getCanonicalDecl(), &BI))
                .first->second;
        if (Existing && Existing != &BI) {
          S.Diag(CurrentLocation, diag::warn_vbase_moved_multiple_times)
            << Class << Base;
          S.Diag(Existing->getBeginLoc(), diag::note_vbase_moved_here)
              << (Base->getCanonicalDecl() ==
                  Existing->getType()->getAsCXXRecordDecl()->getCanonicalDecl())
              << Base << Existing->getType() << Existing->getSourceRange();
          S.Diag(BI.getBeginLoc(), diag::note_vbase_moved_here)
              << (Base->getCanonicalDecl() ==
                  BI.getType()->getAsCXXRecordDecl()->getCanonicalDecl())
              << Base << BI.getType() << BaseSpec->getSourceRange();

          // Only diagnose each vbase once.
          Existing = nullptr;
        }
      } else {
        // Only walk over bases that have defaulted move assignment operators.
        // We assume that any user-provided move assignment operator handles
        // the multiple-moves-of-vbase case itself somehow.
        if (!SMOR.getMethod()->isDefaulted())
          continue;

        // We're going to move the base classes of Base. Add them to the list.
        llvm::append_range(Worklist, llvm::make_pointer_range(Base->bases()));
      }
    }
  }
}

void Sema::DefineImplicitMoveAssignment(SourceLocation CurrentLocation,
                                        CXXMethodDecl *MoveAssignOperator) {
  assert((MoveAssignOperator->isDefaulted() &&
          MoveAssignOperator->isOverloadedOperator() &&
          MoveAssignOperator->getOverloadedOperator() == OO_Equal &&
          !MoveAssignOperator->doesThisDeclarationHaveABody() &&
          !MoveAssignOperator->isDeleted()) &&
         "DefineImplicitMoveAssignment called for wrong function");
  if (MoveAssignOperator->willHaveBody() || MoveAssignOperator->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = MoveAssignOperator->getParent();
  if (ClassDecl->isInvalidDecl()) {
    MoveAssignOperator->setInvalidDecl();
    return;
  }

  // C++0x [class.copy]p28:
  //   The implicitly-defined or move assignment operator for a non-union class
  //   X performs memberwise move assignment of its subobjects. The direct base
  //   classes of X are assigned first, in the order of their declaration in the
  //   base-specifier-list, and then the immediate non-static data members of X
  //   are assigned, in the order in which they were declared in the class
  //   definition.

  // Issue a warning if our implicit move assignment operator will move
  // from a virtual base more than once.
  checkMoveAssignmentForRepeatedMove(*this, ClassDecl, CurrentLocation);

  SynthesizedFunctionScope Scope(*this, MoveAssignOperator);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       MoveAssignOperator->getType()->castAs<FunctionProtoType>());

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  // The statements that form the synthesized function body.
  SmallVector<Stmt*, 8> Statements;

  // The parameter for the "other" object, which we are move from.
  ParmVarDecl *Other = MoveAssignOperator->getNonObjectParameter(0);
  QualType OtherRefType =
      Other->getType()->castAs<RValueReferenceType>()->getPointeeType();

  // Our location for everything implicitly-generated.
  SourceLocation Loc = MoveAssignOperator->getEndLoc().isValid()
                           ? MoveAssignOperator->getEndLoc()
                           : MoveAssignOperator->getLocation();

  // Builds a reference to the "other" object.
  RefBuilder OtherRef(Other, OtherRefType);
  // Cast to rvalue.
  MoveCastBuilder MoveOther(OtherRef);

  // Builds the function object parameter.
  std::optional<ThisBuilder> This;
  std::optional<DerefBuilder> DerefThis;
  std::optional<RefBuilder> ExplicitObject;
  QualType ObjectType;
  if (MoveAssignOperator->isExplicitObjectMemberFunction()) {
    ObjectType = MoveAssignOperator->getParamDecl(0)->getType();
    if (ObjectType->isReferenceType())
      ObjectType = ObjectType->getPointeeType();
    ExplicitObject.emplace(MoveAssignOperator->getParamDecl(0), ObjectType);
  } else {
    ObjectType = getCurrentThisType();
    This.emplace();
    DerefThis.emplace(*This);
  }
  ExprBuilder &ObjectParameter =
      ExplicitObject ? *ExplicitObject : static_cast<ExprBuilder &>(*This);

  // Assign base classes.
  bool Invalid = false;
  for (auto &Base : ClassDecl->bases()) {
    // C++11 [class.copy]p28:
    //   It is unspecified whether subobjects representing virtual base classes
    //   are assigned more than once by the implicitly-defined copy assignment
    //   operator.
    // FIXME: Do not assign to a vbase that will be assigned by some other base
    // class. For a move-assignment, this can result in the vbase being moved
    // multiple times.

    // Form the assignment:
    //   static_cast<Base*>(this)->Base::operator=(static_cast<Base&&>(other));
    QualType BaseType = Base.getType().getUnqualifiedType();
    if (!BaseType->isRecordType()) {
      Invalid = true;
      continue;
    }

    CXXCastPath BasePath;
    BasePath.push_back(&Base);

    // Construct the "from" expression, which is an implicit cast to the
    // appropriately-qualified base type.
    CastBuilder From(OtherRef, BaseType, VK_XValue, BasePath);

    // Implicitly cast "this" to the appropriately-qualified base type.
    // Dereference "this".
    CastBuilder To(
        ExplicitObject ? static_cast<ExprBuilder &>(*ExplicitObject)
                       : static_cast<ExprBuilder &>(*DerefThis),
        Context.getQualifiedType(BaseType, ObjectType.getQualifiers()),
        VK_LValue, BasePath);

    // Build the move.
    StmtResult Move = buildSingleCopyAssign(*this, Loc, BaseType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/true,
                                            /*Copying=*/false);
    if (Move.isInvalid()) {
      MoveAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the move.
    Statements.push_back(Move.getAs<Expr>());
  }

  // Assign non-static members.
  for (auto *Field : ClassDecl->fields()) {
    // FIXME: We should form some kind of AST representation for the implied
    // memcpy in a union copy operation.
    if (Field->isUnnamedBitField() || Field->getParent()->isUnion())
      continue;

    if (Field->isInvalidDecl()) {
      Invalid = true;
      continue;
    }

    // Check for members of reference type; we can't move those.
    if (Field->getType()->isReferenceType()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 0 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Check for members of const-qualified, non-class type.
    QualType BaseType = Context.getBaseElementType(Field->getType());
    if (!BaseType->getAs<RecordType>() && BaseType.isConstQualified()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 1 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Suppress assigning zero-width bitfields.
    if (Field->isZeroLengthBitField(Context))
      continue;

    QualType FieldType = Field->getType().getNonReferenceType();
    if (FieldType->isIncompleteArrayType()) {
      assert(ClassDecl->hasFlexibleArrayMember() &&
             "Incomplete array type is not valid");
      continue;
    }

    // Build references to the field in the object we're copying from and to.
    LookupResult MemberLookup(*this, Field->getDeclName(), Loc,
                              LookupMemberName);
    MemberLookup.addDecl(Field);
    MemberLookup.resolveKind();
    MemberBuilder From(MoveOther, OtherRefType,
                       /*IsArrow=*/false, MemberLookup);
    MemberBuilder To(ObjectParameter, ObjectType, /*IsArrow=*/!ExplicitObject,
                     MemberLookup);

    assert(!From.build(*this, Loc)->isLValue() && // could be xvalue or prvalue
        "Member reference with rvalue base must be rvalue except for reference "
        "members, which aren't allowed for move assignment.");

    // Build the move of this field.
    StmtResult Move = buildSingleCopyAssign(*this, Loc, FieldType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/false,
                                            /*Copying=*/false);
    if (Move.isInvalid()) {
      MoveAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the copy.
    Statements.push_back(Move.getAs<Stmt>());
  }

  if (!Invalid) {
    // Add a "return *this;"
    Expr *ThisExpr =
        (ExplicitObject ? static_cast<ExprBuilder &>(*ExplicitObject)
                        : static_cast<ExprBuilder &>(*DerefThis))
            .build(*this, Loc);

    StmtResult Return = BuildReturnStmt(Loc, ThisExpr);
    if (Return.isInvalid())
      Invalid = true;
    else
      Statements.push_back(Return.getAs<Stmt>());
  }

  if (Invalid) {
    MoveAssignOperator->setInvalidDecl();
    return;
  }

  StmtResult Body;
  {
    CompoundScopeRAII CompoundScope(*this);
    Body = ActOnCompoundStmt(Loc, Loc, Statements,
                             /*isStmtExpr=*/false);
    assert(!Body.isInvalid() && "Compound statement creation cannot fail");
  }
  MoveAssignOperator->setBody(Body.getAs<Stmt>());
  MoveAssignOperator->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(MoveAssignOperator);
  }
}

CXXConstructorDecl *Sema::DeclareImplicitCopyConstructor(
                                                    CXXRecordDecl *ClassDecl) {
  // C++ [class.copy]p4:
  //   If the class definition does not explicitly declare a copy
  //   constructor, one is declared implicitly.
  assert(ClassDecl->needsImplicitCopyConstructor());

  DeclaringSpecialMember DSM(*this, ClassDecl,
                             CXXSpecialMemberKind::CopyConstructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  QualType ClassType = Context.getTypeDeclType(ClassDecl);
  QualType ArgType = ClassType;
  ArgType = Context.getElaboratedType(ElaboratedTypeKeyword::None, nullptr,
                                      ArgType, nullptr);
  bool Const = ClassDecl->implicitCopyConstructorHasConstParam();
  if (Const)
    ArgType = ArgType.withConst();

  LangAS AS = getDefaultCXXMethodAddrSpace();
  if (AS != LangAS::Default)
    ArgType = Context.getAddrSpaceQualType(ArgType, AS);

  ArgType = Context.getLValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(
      *this, ClassDecl, CXXSpecialMemberKind::CopyConstructor, Const);

  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(
                                           Context.getCanonicalType(ClassType));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);

  //   An implicitly-declared copy constructor is an inline public
  //   member of its class.
  CXXConstructorDecl *CopyConstructor = CXXConstructorDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, QualType(), /*TInfo=*/nullptr,
      ExplicitSpecifier(), getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true,
      /*isImplicitlyDeclared=*/true,
      Constexpr ? ConstexprSpecKind::Constexpr
                : ConstexprSpecKind::Unspecified);
  CopyConstructor->setAccess(AS_public);
  CopyConstructor->setDefaulted();

  setupImplicitSpecialMemberType(CopyConstructor, Context.VoidTy, ArgType);

  if (getLangOpts().CUDA)
    CUDA().inferTargetForImplicitSpecialMember(
        ClassDecl, CXXSpecialMemberKind::CopyConstructor, CopyConstructor,
        /* ConstRHS */ Const,
        /* Diagnose */ false);

  // During template instantiation of special member functions we need a
  // reliable TypeSourceInfo for the parameter types in order to allow functions
  // to be substituted.
  TypeSourceInfo *TSI = nullptr;
  if (inTemplateInstantiation() && ClassDecl->isLambda())
    TSI = Context.getTrivialTypeSourceInfo(ArgType);

  // Add the parameter to the constructor.
  ParmVarDecl *FromParam =
      ParmVarDecl::Create(Context, CopyConstructor, ClassLoc, ClassLoc,
                          /*IdentifierInfo=*/nullptr, ArgType,
                          /*TInfo=*/TSI, SC_None, nullptr);
  CopyConstructor->setParams(FromParam);

  CopyConstructor->setTrivial(
      ClassDecl->needsOverloadResolutionForCopyConstructor()
          ? SpecialMemberIsTrivial(CopyConstructor,
                                   CXXSpecialMemberKind::CopyConstructor)
          : ClassDecl->hasTrivialCopyConstructor());

  CopyConstructor->setTrivialForCall(
      ClassDecl->hasAttr<TrivialABIAttr>() ||
      (ClassDecl->needsOverloadResolutionForCopyConstructor()
           ? SpecialMemberIsTrivial(CopyConstructor,
                                    CXXSpecialMemberKind::CopyConstructor,
                                    TAH_ConsiderTrivialABI)
           : ClassDecl->hasTrivialCopyConstructorForCall()));

  // Note that we have declared this constructor.
  ++getASTContext().NumImplicitCopyConstructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, CopyConstructor);

  if (ShouldDeleteSpecialMember(CopyConstructor,
                                CXXSpecialMemberKind::CopyConstructor)) {
    ClassDecl->setImplicitCopyConstructorIsDeleted();
    SetDeclDeleted(CopyConstructor, ClassLoc);
  }

  if (S)
    PushOnScopeChains(CopyConstructor, S, false);
  ClassDecl->addDecl(CopyConstructor);

  return CopyConstructor;
}

void Sema::DefineImplicitCopyConstructor(SourceLocation CurrentLocation,
                                         CXXConstructorDecl *CopyConstructor) {
  assert((CopyConstructor->isDefaulted() &&
          CopyConstructor->isCopyConstructor() &&
          !CopyConstructor->doesThisDeclarationHaveABody() &&
          !CopyConstructor->isDeleted()) &&
         "DefineImplicitCopyConstructor - call it for implicit copy ctor");
  if (CopyConstructor->willHaveBody() || CopyConstructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = CopyConstructor->getParent();
  assert(ClassDecl && "DefineImplicitCopyConstructor - invalid constructor");

  SynthesizedFunctionScope Scope(*this, CopyConstructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       CopyConstructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  // C++11 [class.copy]p7:
  //   The [definition of an implicitly declared copy constructor] is
  //   deprecated if the class has a user-declared copy assignment operator
  //   or a user-declared destructor.
  if (getLangOpts().CPlusPlus11 && CopyConstructor->isImplicit())
    diagnoseDeprecatedCopyOperation(*this, CopyConstructor);

  if (SetCtorInitializers(CopyConstructor, /*AnyErrors=*/false)) {
    CopyConstructor->setInvalidDecl();
  }  else {
    SourceLocation Loc = CopyConstructor->getEndLoc().isValid()
                             ? CopyConstructor->getEndLoc()
                             : CopyConstructor->getLocation();
    Sema::CompoundScopeRAII CompoundScope(*this);
    CopyConstructor->setBody(
        ActOnCompoundStmt(Loc, Loc, std::nullopt, /*isStmtExpr=*/false)
            .getAs<Stmt>());
    CopyConstructor->markUsed(Context);
  }

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(CopyConstructor);
  }
}

CXXConstructorDecl *Sema::DeclareImplicitMoveConstructor(
                                                    CXXRecordDecl *ClassDecl) {
  assert(ClassDecl->needsImplicitMoveConstructor());

  DeclaringSpecialMember DSM(*this, ClassDecl,
                             CXXSpecialMemberKind::MoveConstructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  QualType ClassType = Context.getTypeDeclType(ClassDecl);

  QualType ArgType = ClassType;
  ArgType = Context.getElaboratedType(ElaboratedTypeKeyword::None, nullptr,
                                      ArgType, nullptr);
  LangAS AS = getDefaultCXXMethodAddrSpace();
  if (AS != LangAS::Default)
    ArgType = Context.getAddrSpaceQualType(ClassType, AS);
  ArgType = Context.getRValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(
      *this, ClassDecl, CXXSpecialMemberKind::MoveConstructor, false);

  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(
                                           Context.getCanonicalType(ClassType));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);

  // C++11 [class.copy]p11:
  //   An implicitly-declared copy/move constructor is an inline public
  //   member of its class.
  CXXConstructorDecl *MoveConstructor = CXXConstructorDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, QualType(), /*TInfo=*/nullptr,
      ExplicitSpecifier(), getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true,
      /*isImplicitlyDeclared=*/true,
      Constexpr ? ConstexprSpecKind::Constexpr
                : ConstexprSpecKind::Unspecified);
  MoveConstructor->setAccess(AS_public);
  MoveConstructor->setDefaulted();

  setupImplicitSpecialMemberType(MoveConstructor, Context.VoidTy, ArgType);

  if (getLangOpts().CUDA)
    CUDA().inferTargetForImplicitSpecialMember(
        ClassDecl, CXXSpecialMemberKind::MoveConstructor, MoveConstructor,
        /* ConstRHS */ false,
        /* Diagnose */ false);

  // Add the parameter to the constructor.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, MoveConstructor,
                                               ClassLoc, ClassLoc,
                                               /*IdentifierInfo=*/nullptr,
                                               ArgType, /*TInfo=*/nullptr,
                                               SC_None, nullptr);
  MoveConstructor->setParams(FromParam);

  MoveConstructor->setTrivial(
      ClassDecl->needsOverloadResolutionForMoveConstructor()
          ? SpecialMemberIsTrivial(MoveConstructor,
                                   CXXSpecialMemberKind::MoveConstructor)
          : ClassDecl->hasTrivialMoveConstructor());

  MoveConstructor->setTrivialForCall(
      ClassDecl->hasAttr<TrivialABIAttr>() ||
      (ClassDecl->needsOverloadResolutionForMoveConstructor()
           ? SpecialMemberIsTrivial(MoveConstructor,
                                    CXXSpecialMemberKind::MoveConstructor,
                                    TAH_ConsiderTrivialABI)
           : ClassDecl->hasTrivialMoveConstructorForCall()));

  // Note that we have declared this constructor.
  ++getASTContext().NumImplicitMoveConstructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, MoveConstructor);

  if (ShouldDeleteSpecialMember(MoveConstructor,
                                CXXSpecialMemberKind::MoveConstructor)) {
    ClassDecl->setImplicitMoveConstructorIsDeleted();
    SetDeclDeleted(MoveConstructor, ClassLoc);
  }

  if (S)
    PushOnScopeChains(MoveConstructor, S, false);
  ClassDecl->addDecl(MoveConstructor);

  return MoveConstructor;
}

void Sema::DefineImplicitMoveConstructor(SourceLocation CurrentLocation,
                                         CXXConstructorDecl *MoveConstructor) {
  assert((MoveConstructor->isDefaulted() &&
          MoveConstructor->isMoveConstructor() &&
          !MoveConstructor->doesThisDeclarationHaveABody() &&
          !MoveConstructor->isDeleted()) &&
         "DefineImplicitMoveConstructor - call it for implicit move ctor");
  if (MoveConstructor->willHaveBody() || MoveConstructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = MoveConstructor->getParent();
  assert(ClassDecl && "DefineImplicitMoveConstructor - invalid constructor");

  SynthesizedFunctionScope Scope(*this, MoveConstructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       MoveConstructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  if (SetCtorInitializers(MoveConstructor, /*AnyErrors=*/false)) {
    MoveConstructor->setInvalidDecl();
  } else {
    SourceLocation Loc = MoveConstructor->getEndLoc().isValid()
                             ? MoveConstructor->getEndLoc()
                             : MoveConstructor->getLocation();
    Sema::CompoundScopeRAII CompoundScope(*this);
    MoveConstructor->setBody(
        ActOnCompoundStmt(Loc, Loc, std::nullopt, /*isStmtExpr=*/false)
            .getAs<Stmt>());
    MoveConstructor->markUsed(Context);
  }

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(MoveConstructor);
  }
}

bool Sema::isImplicitlyDeleted(FunctionDecl *FD) {
  return FD->isDeleted() && FD->isDefaulted() && isa<CXXMethodDecl>(FD);
}

void Sema::DefineImplicitLambdaToFunctionPointerConversion(
                            SourceLocation CurrentLocation,
                            CXXConversionDecl *Conv) {
  SynthesizedFunctionScope Scope(*this, Conv);
  assert(!Conv->getReturnType()->isUndeducedType());

  QualType ConvRT = Conv->getType()->castAs<FunctionType>()->getReturnType();
  CallingConv CC =
      ConvRT->getPointeeType()->castAs<FunctionType>()->getCallConv();

  CXXRecordDecl *Lambda = Conv->getParent();
  FunctionDecl *CallOp = Lambda->getLambdaCallOperator();
  FunctionDecl *Invoker =
      CallOp->hasCXXExplicitFunctionObjectParameter() || CallOp->isStatic()
          ? CallOp
          : Lambda->getLambdaStaticInvoker(CC);

  if (auto *TemplateArgs = Conv->getTemplateSpecializationArgs()) {
    CallOp = InstantiateFunctionDeclaration(
        CallOp->getDescribedFunctionTemplate(), TemplateArgs, CurrentLocation);
    if (!CallOp)
      return;

    if (CallOp != Invoker) {
      Invoker = InstantiateFunctionDeclaration(
          Invoker->getDescribedFunctionTemplate(), TemplateArgs,
          CurrentLocation);
      if (!Invoker)
        return;
    }
  }

  if (CallOp->isInvalidDecl())
    return;

  // Mark the call operator referenced (and add to pending instantiations
  // if necessary).
  // For both the conversion and static-invoker template specializations
  // we construct their body's in this function, so no need to add them
  // to the PendingInstantiations.
  MarkFunctionReferenced(CurrentLocation, CallOp);

  if (Invoker != CallOp) {
    // Fill in the __invoke function with a dummy implementation. IR generation
    // will fill in the actual details. Update its type in case it contained
    // an 'auto'.
    Invoker->markUsed(Context);
    Invoker->setReferenced();
    Invoker->setType(Conv->getReturnType()->getPointeeType());
    Invoker->setBody(new (Context) CompoundStmt(Conv->getLocation()));
  }

  // Construct the body of the conversion function { return __invoke; }.
  Expr *FunctionRef = BuildDeclRefExpr(Invoker, Invoker->getType(), VK_LValue,
                                       Conv->getLocation());
  assert(FunctionRef && "Can't refer to __invoke function?");
  Stmt *Return = BuildReturnStmt(Conv->getLocation(), FunctionRef).get();
  Conv->setBody(CompoundStmt::Create(Context, Return, FPOptionsOverride(),
                                     Conv->getLocation(), Conv->getLocation()));
  Conv->markUsed(Context);
  Conv->setReferenced();

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Conv);
    if (Invoker != CallOp)
      L->CompletedImplicitDefinition(Invoker);
  }
}

void Sema::DefineImplicitLambdaToBlockPointerConversion(
    SourceLocation CurrentLocation, CXXConversionDecl *Conv) {
  assert(!Conv->getParent()->isGenericLambda());

  SynthesizedFunctionScope Scope(*this, Conv);

  // Copy-initialize the lambda object as needed to capture it.
  Expr *This = ActOnCXXThis(CurrentLocation).get();
  Expr *DerefThis =CreateBuiltinUnaryOp(CurrentLocation, UO_Deref, This).get();

  ExprResult BuildBlock = BuildBlockForLambdaConversion(CurrentLocation,
                                                        Conv->getLocation(),
                                                        Conv, DerefThis);

  // If we're not under ARC, make sure we still get the _Block_copy/autorelease
  // behavior.  Note that only the general conversion function does this
  // (since it's unusable otherwise); in the case where we inline the
  // block literal, it has block literal lifetime semantics.
  if (!BuildBlock.isInvalid() && !getLangOpts().ObjCAutoRefCount)
    BuildBlock = ImplicitCastExpr::Create(
        Context, BuildBlock.get()->getType(), CK_CopyAndAutoreleaseBlockObject,
        BuildBlock.get(), nullptr, VK_PRValue, FPOptionsOverride());

  if (BuildBlock.isInvalid()) {
    Diag(CurrentLocation, diag::note_lambda_to_block_conv);
    Conv->setInvalidDecl();
    return;
  }

  // Create the return statement that returns the block from the conversion
  // function.
  StmtResult Return = BuildReturnStmt(Conv->getLocation(), BuildBlock.get());
  if (Return.isInvalid()) {
    Diag(CurrentLocation, diag::note_lambda_to_block_conv);
    Conv->setInvalidDecl();
    return;
  }

  // Set the body of the conversion function.
  Stmt *ReturnS = Return.get();
  Conv->setBody(CompoundStmt::Create(Context, ReturnS, FPOptionsOverride(),
                                     Conv->getLocation(), Conv->getLocation()));
  Conv->markUsed(Context);

  // We're done; notify the mutation listener, if any.
  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Conv);
  }
}

/// Determine whether the given list arguments contains exactly one
/// "real" (non-default) argument.
static bool hasOneRealArgument(MultiExprArg Args) {
  switch (Args.size()) {
  case 0:
    return false;

  default:
    if (!Args[1]->isDefaultArgument())
      return false;

    [[fallthrough]];
  case 1:
    return !Args[0]->isDefaultArgument();
  }

  return false;
}

ExprResult Sema::BuildCXXConstructExpr(
    SourceLocation ConstructLoc, QualType DeclInitType, NamedDecl *FoundDecl,
    CXXConstructorDecl *Constructor, MultiExprArg ExprArgs,
    bool HadMultipleCandidates, bool IsListInitialization,
    bool IsStdInitListInitialization, bool RequiresZeroInit,
    CXXConstructionKind ConstructKind, SourceRange ParenRange) {
  bool Elidable = false;

  // C++0x [class.copy]p34:
  //   When certain criteria are met, an implementation is allowed to
  //   omit the copy/move construction of a class object, even if the
  //   copy/move constructor and/or destructor for the object have
  //   side effects. [...]
  //     - when a temporary class object that has not been bound to a
  //       reference (12.2) would be copied/moved to a class object
  //       with the same cv-unqualified type, the copy/move operation
  //       can be omitted by constructing the temporary object
  //       directly into the target of the omitted copy/move
  if (ConstructKind == CXXConstructionKind::Complete && Constructor &&
      // FIXME: Converting constructors should also be accepted.
      // But to fix this, the logic that digs down into a CXXConstructExpr
      // to find the source object needs to handle it.
      // Right now it assumes the source object is passed directly as the
      // first argument.
      Constructor->isCopyOrMoveConstructor() && hasOneRealArgument(ExprArgs)) {
    Expr *SubExpr = ExprArgs[0];
    // FIXME: Per above, this is also incorrect if we want to accept
    //        converting constructors, as isTemporaryObject will
    //        reject temporaries with different type from the
    //        CXXRecord itself.
    Elidable = SubExpr->isTemporaryObject(
        Context, cast<CXXRecordDecl>(FoundDecl->getDeclContext()));
  }

  return BuildCXXConstructExpr(ConstructLoc, DeclInitType,
                               FoundDecl, Constructor,
                               Elidable, ExprArgs, HadMultipleCandidates,
                               IsListInitialization,
                               IsStdInitListInitialization, RequiresZeroInit,
                               ConstructKind, ParenRange);
}

ExprResult Sema::BuildCXXConstructExpr(
    SourceLocation ConstructLoc, QualType DeclInitType, NamedDecl *FoundDecl,
    CXXConstructorDecl *Constructor, bool Elidable, MultiExprArg ExprArgs,
    bool HadMultipleCandidates, bool IsListInitialization,
    bool IsStdInitListInitialization, bool RequiresZeroInit,
    CXXConstructionKind ConstructKind, SourceRange ParenRange) {
  if (auto *Shadow = dyn_cast<ConstructorUsingShadowDecl>(FoundDecl)) {
    Constructor = findInheritingConstructor(ConstructLoc, Constructor, Shadow);
    // The only way to get here is if we did overload resolution to find the
    // shadow decl, so we don't need to worry about re-checking the trailing
    // requires clause.
    if (DiagnoseUseOfOverloadedDecl(Constructor, ConstructLoc))
      return ExprError();
  }

  return BuildCXXConstructExpr(
      ConstructLoc, DeclInitType, Constructor, Elidable, ExprArgs,
      HadMultipleCandidates, IsListInitialization, IsStdInitListInitialization,
      RequiresZeroInit, ConstructKind, ParenRange);
}

/// BuildCXXConstructExpr - Creates a complete call to a constructor,
/// including handling of its default argument expressions.
ExprResult Sema::BuildCXXConstructExpr(
    SourceLocation ConstructLoc, QualType DeclInitType,
    CXXConstructorDecl *Constructor, bool Elidable, MultiExprArg ExprArgs,
    bool HadMultipleCandidates, bool IsListInitialization,
    bool IsStdInitListInitialization, bool RequiresZeroInit,
    CXXConstructionKind ConstructKind, SourceRange ParenRange) {
  assert(declaresSameEntity(
             Constructor->getParent(),
             DeclInitType->getBaseElementTypeUnsafe()->getAsCXXRecordDecl()) &&
         "given constructor for wrong type");
  MarkFunctionReferenced(ConstructLoc, Constructor);
  if (getLangOpts().CUDA && !CUDA().CheckCall(ConstructLoc, Constructor))
    return ExprError();

  return CheckForImmediateInvocation(
      CXXConstructExpr::Create(
          Context, DeclInitType, ConstructLoc, Constructor, Elidable, ExprArgs,
          HadMultipleCandidates, IsListInitialization,
          IsStdInitListInitialization, RequiresZeroInit,
          static_cast<CXXConstructionKind>(ConstructKind), ParenRange),
      Constructor);
}

void Sema::FinalizeVarWithDestructor(VarDecl *VD, const RecordType *Record) {
  if (VD->isInvalidDecl()) return;
  // If initializing the variable failed, don't also diagnose problems with
  // the destructor, they're likely related.
  if (VD->getInit() && VD->getInit()->containsErrors())
    return;

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(Record->getDecl());
  if (ClassDecl->isInvalidDecl()) return;
  if (ClassDecl->hasIrrelevantDestructor()) return;
  if (ClassDecl->isDependentContext()) return;

  if (VD->isNoDestroy(getASTContext()))
    return;

  CXXDestructorDecl *Destructor = LookupDestructor(ClassDecl);
  // The result of `LookupDestructor` might be nullptr if the destructor is
  // invalid, in which case it is marked as `IneligibleOrNotSelected` and
  // will not be selected by `CXXRecordDecl::getDestructor()`.
  if (!Destructor)
    return;
  // If this is an array, we'll require the destructor during initialization, so
  // we can skip over this. We still want to emit exit-time destructor warnings
  // though.
  if (!VD->getType()->isArrayType()) {
    MarkFunctionReferenced(VD->getLocation(), Destructor);
    CheckDestructorAccess(VD->getLocation(), Destructor,
                          PDiag(diag::err_access_dtor_var)
                              << VD->getDeclName() << VD->getType());
    DiagnoseUseOfDecl(Destructor, VD->getLocation());
  }

  if (Destructor->isTrivial()) return;

  // If the destructor is constexpr, check whether the variable has constant
  // destruction now.
  if (Destructor->isConstexpr()) {
    bool HasConstantInit = false;
    if (VD->getInit() && !VD->getInit()->isValueDependent())
      HasConstantInit = VD->evaluateValue();
    SmallVector<PartialDiagnosticAt, 8> Notes;
    if (!VD->evaluateDestruction(Notes) && VD->isConstexpr() &&
        HasConstantInit) {
      Diag(VD->getLocation(),
           diag::err_constexpr_var_requires_const_destruction) << VD;
      for (unsigned I = 0, N = Notes.size(); I != N; ++I)
        Diag(Notes[I].first, Notes[I].second);
    }
  }

  if (!VD->hasGlobalStorage() || !VD->needsDestruction(Context))
    return;

  // Emit warning for non-trivial dtor in global scope (a real global,
  // class-static, function-static).
  if (!VD->hasAttr<AlwaysDestroyAttr>())
    Diag(VD->getLocation(), diag::warn_exit_time_destructor);

  // TODO: this should be re-enabled for static locals by !CXAAtExit
  if (!VD->isStaticLocal())
    Diag(VD->getLocation(), diag::warn_global_destructor);
}

bool Sema::CompleteConstructorCall(CXXConstructorDecl *Constructor,
                                   QualType DeclInitType, MultiExprArg ArgsPtr,
                                   SourceLocation Loc,
                                   SmallVectorImpl<Expr *> &ConvertedArgs,
                                   bool AllowExplicit,
                                   bool IsListInitialization) {
  // FIXME: This duplicates a lot of code from Sema::ConvertArgumentsForCall.
  unsigned NumArgs = ArgsPtr.size();
  Expr **Args = ArgsPtr.data();

  const auto *Proto = Constructor->getType()->castAs<FunctionProtoType>();
  unsigned NumParams = Proto->getNumParams();

  // If too few arguments are available, we'll fill in the rest with defaults.
  if (NumArgs < NumParams)
    ConvertedArgs.reserve(NumParams);
  else
    ConvertedArgs.reserve(NumArgs);

  VariadicCallType CallType =
    Proto->isVariadic() ? VariadicConstructor : VariadicDoesNotApply;
  SmallVector<Expr *, 8> AllArgs;
  bool Invalid = GatherArgumentsForCall(
      Loc, Constructor, Proto, 0, llvm::ArrayRef(Args, NumArgs), AllArgs,
      CallType, AllowExplicit, IsListInitialization);
  ConvertedArgs.append(AllArgs.begin(), AllArgs.end());

  DiagnoseSentinelCalls(Constructor, Loc, AllArgs);

  CheckConstructorCall(Constructor, DeclInitType,
                       llvm::ArrayRef(AllArgs.data(), AllArgs.size()), Proto,
                       Loc);

  return Invalid;
}

static inline bool
CheckOperatorNewDeleteDeclarationScope(Sema &SemaRef,
                                       const FunctionDecl *FnDecl) {
  const DeclContext *DC = FnDecl->getDeclContext()->getRedeclContext();
  if (isa<NamespaceDecl>(DC)) {
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_declared_in_namespace)
      << FnDecl->getDeclName();
  }

  if (isa<TranslationUnitDecl>(DC) &&
      FnDecl->getStorageClass() == SC_Static) {
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_declared_static)
      << FnDecl->getDeclName();
  }

  return false;
}

static CanQualType RemoveAddressSpaceFromPtr(Sema &SemaRef,
                                             const PointerType *PtrTy) {
  auto &Ctx = SemaRef.Context;
  Qualifiers PtrQuals = PtrTy->getPointeeType().getQualifiers();
  PtrQuals.removeAddressSpace();
  return Ctx.getPointerType(Ctx.getCanonicalType(Ctx.getQualifiedType(
      PtrTy->getPointeeType().getUnqualifiedType(), PtrQuals)));
}

static inline bool
CheckOperatorNewDeleteTypes(Sema &SemaRef, const FunctionDecl *FnDecl,
                            CanQualType ExpectedResultType,
                            CanQualType ExpectedFirstParamType,
                            unsigned DependentParamTypeDiag,
                            unsigned InvalidParamTypeDiag) {
  QualType ResultType =
      FnDecl->getType()->castAs<FunctionType>()->getReturnType();

  if (SemaRef.getLangOpts().OpenCLCPlusPlus) {
    // The operator is valid on any address space for OpenCL.
    // Drop address space from actual and expected result types.
    if (const auto *PtrTy = ResultType->getAs<PointerType>())
      ResultType = RemoveAddressSpaceFromPtr(SemaRef, PtrTy);

    if (auto ExpectedPtrTy = ExpectedResultType->getAs<PointerType>())
      ExpectedResultType = RemoveAddressSpaceFromPtr(SemaRef, ExpectedPtrTy);
  }

  // Check that the result type is what we expect.
  if (SemaRef.Context.getCanonicalType(ResultType) != ExpectedResultType) {
    // Reject even if the type is dependent; an operator delete function is
    // required to have a non-dependent result type.
    return SemaRef.Diag(
               FnDecl->getLocation(),
               ResultType->isDependentType()
                   ? diag::err_operator_new_delete_dependent_result_type
                   : diag::err_operator_new_delete_invalid_result_type)
           << FnDecl->getDeclName() << ExpectedResultType;
  }

  // A function template must have at least 2 parameters.
  if (FnDecl->getDescribedFunctionTemplate() && FnDecl->getNumParams() < 2)
    return SemaRef.Diag(FnDecl->getLocation(),
                      diag::err_operator_new_delete_template_too_few_parameters)
        << FnDecl->getDeclName();

  // The function decl must have at least 1 parameter.
  if (FnDecl->getNumParams() == 0)
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_too_few_parameters)
      << FnDecl->getDeclName();

  QualType FirstParamType = FnDecl->getParamDecl(0)->getType();
  if (SemaRef.getLangOpts().OpenCLCPlusPlus) {
    // The operator is valid on any address space for OpenCL.
    // Drop address space from actual and expected first parameter types.
    if (const auto *PtrTy =
            FnDecl->getParamDecl(0)->getType()->getAs<PointerType>())
      FirstParamType = RemoveAddressSpaceFromPtr(SemaRef, PtrTy);

    if (auto ExpectedPtrTy = ExpectedFirstParamType->getAs<PointerType>())
      ExpectedFirstParamType =
          RemoveAddressSpaceFromPtr(SemaRef, ExpectedPtrTy);
  }

  // Check that the first parameter type is what we expect.
  if (SemaRef.Context.getCanonicalType(FirstParamType).getUnqualifiedType() !=
      ExpectedFirstParamType) {
    // The first parameter type is not allowed to be dependent. As a tentative
    // DR resolution, we allow a dependent parameter type if it is the right
    // type anyway, to allow destroying operator delete in class templates.
    return SemaRef.Diag(FnDecl->getLocation(), FirstParamType->isDependentType()
                                                   ? DependentParamTypeDiag
                                                   : InvalidParamTypeDiag)
           << FnDecl->getDeclName() << ExpectedFirstParamType;
  }

  return false;
}

static bool
CheckOperatorNewDeclaration(Sema &SemaRef, const FunctionDecl *FnDecl) {
  // C++ [basic.stc.dynamic.allocation]p1:
  //   A program is ill-formed if an allocation function is declared in a
  //   namespace scope other than global scope or declared static in global
  //   scope.
  if (CheckOperatorNewDeleteDeclarationScope(SemaRef, FnDecl))
    return true;

  CanQualType SizeTy =
    SemaRef.Context.getCanonicalType(SemaRef.Context.getSizeType());

  // C++ [basic.stc.dynamic.allocation]p1:
  //  The return type shall be void*. The first parameter shall have type
  //  std::size_t.
  if (CheckOperatorNewDeleteTypes(SemaRef, FnDecl, SemaRef.Context.VoidPtrTy,
                                  SizeTy,
                                  diag::err_operator_new_dependent_param_type,
                                  diag::err_operator_new_param_type))
    return true;

  // C++ [basic.stc.dynamic.allocation]p1:
  //  The first parameter shall not have an associated default argument.
  if (FnDecl->getParamDecl(0)->hasDefaultArg())
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_default_arg)
      << FnDecl->getDeclName() << FnDecl->getParamDecl(0)->getDefaultArgRange();

  return false;
}

static bool
CheckOperatorDeleteDeclaration(Sema &SemaRef, FunctionDecl *FnDecl) {
  // C++ [basic.stc.dynamic.deallocation]p1:
  //   A program is ill-formed if deallocation functions are declared in a
  //   namespace scope other than global scope or declared static in global
  //   scope.
  if (CheckOperatorNewDeleteDeclarationScope(SemaRef, FnDecl))
    return true;

  auto *MD = dyn_cast<CXXMethodDecl>(FnDecl);

  // C++ P0722:
  //   Within a class C, the first parameter of a destroying operator delete
  //   shall be of type C *. The first parameter of any other deallocation
  //   function shall be of type void *.
  CanQualType ExpectedFirstParamType =
      MD && MD->isDestroyingOperatorDelete()
          ? SemaRef.Context.getCanonicalType(SemaRef.Context.getPointerType(
                SemaRef.Context.getRecordType(MD->getParent())))
          : SemaRef.Context.VoidPtrTy;

  // C++ [basic.stc.dynamic.deallocation]p2:
  //   Each deallocation function shall return void
  if (CheckOperatorNewDeleteTypes(
          SemaRef, FnDecl, SemaRef.Context.VoidTy, ExpectedFirstParamType,
          diag::err_operator_delete_dependent_param_type,
          diag::err_operator_delete_param_type))
    return true;

  // C++ P0722:
  //   A destroying operator delete shall be a usual deallocation function.
  if (MD && !MD->getParent()->isDependentContext() &&
      MD->isDestroyingOperatorDelete() &&
      !SemaRef.isUsualDeallocationFunction(MD)) {
    SemaRef.Diag(MD->getLocation(),
                 diag::err_destroying_operator_delete_not_usual);
    return true;
  }

  return false;
}

bool Sema::CheckOverloadedOperatorDeclaration(FunctionDecl *FnDecl) {
  assert(FnDecl && FnDecl->isOverloadedOperator() &&
         "Expected an overloaded operator declaration");

  OverloadedOperatorKind Op = FnDecl->getOverloadedOperator();

  // C++ [over.oper]p5:
  //   The allocation and deallocation functions, operator new,
  //   operator new[], operator delete and operator delete[], are
  //   described completely in 3.7.3. The attributes and restrictions
  //   found in the rest of this subclause do not apply to them unless
  //   explicitly stated in 3.7.3.
  if (Op == OO_Delete || Op == OO_Array_Delete)
    return CheckOperatorDeleteDeclaration(*this, FnDecl);

  if (Op == OO_New || Op == OO_Array_New)
    return CheckOperatorNewDeclaration(*this, FnDecl);

  // C++ [over.oper]p7:
  //   An operator function shall either be a member function or
  //   be a non-member function and have at least one parameter
  //   whose type is a class, a reference to a class, an enumeration,
  //   or a reference to an enumeration.
  // Note: Before C++23, a member function could not be static. The only member
  //       function allowed to be static is the call operator function.
  if (CXXMethodDecl *MethodDecl = dyn_cast<CXXMethodDecl>(FnDecl)) {
    if (MethodDecl->isStatic()) {
      if (Op == OO_Call || Op == OO_Subscript)
        Diag(FnDecl->getLocation(),
             (LangOpts.CPlusPlus23
                  ? diag::warn_cxx20_compat_operator_overload_static
                  : diag::ext_operator_overload_static))
            << FnDecl;
      else
        return Diag(FnDecl->getLocation(), diag::err_operator_overload_static)
               << FnDecl;
    }
  } else {
    bool ClassOrEnumParam = false;
    for (auto *Param : FnDecl->parameters()) {
      QualType ParamType = Param->getType().getNonReferenceType();
      if (ParamType->isDependentType() || ParamType->isRecordType() ||
          ParamType->isEnumeralType()) {
        ClassOrEnumParam = true;
        break;
      }
    }

    if (!ClassOrEnumParam)
      return Diag(FnDecl->getLocation(),
                  diag::err_operator_overload_needs_class_or_enum)
        << FnDecl->getDeclName();
  }

  // C++ [over.oper]p8:
  //   An operator function cannot have default arguments (8.3.6),
  //   except where explicitly stated below.
  //
  // Only the function-call operator (C++ [over.call]p1) and the subscript
  // operator (CWG2507) allow default arguments.
  if (Op != OO_Call) {
    ParmVarDecl *FirstDefaultedParam = nullptr;
    for (auto *Param : FnDecl->parameters()) {
      if (Param->hasDefaultArg()) {
        FirstDefaultedParam = Param;
        break;
      }
    }
    if (FirstDefaultedParam) {
      if (Op == OO_Subscript) {
        Diag(FnDecl->getLocation(), LangOpts.CPlusPlus23
                                        ? diag::ext_subscript_overload
                                        : diag::error_subscript_overload)
            << FnDecl->getDeclName() << 1
            << FirstDefaultedParam->getDefaultArgRange();
      } else {
        return Diag(FirstDefaultedParam->getLocation(),
                    diag::err_operator_overload_default_arg)
               << FnDecl->getDeclName()
               << FirstDefaultedParam->getDefaultArgRange();
      }
    }
  }

  static const bool OperatorUses[NUM_OVERLOADED_OPERATORS][3] = {
    { false, false, false }
#define OVERLOADED_OPERATOR(Name,Spelling,Token,Unary,Binary,MemberOnly) \
    , { Unary, Binary, MemberOnly }
#include "clang/Basic/OperatorKinds.def"
  };

  bool CanBeUnaryOperator = OperatorUses[Op][0];
  bool CanBeBinaryOperator = OperatorUses[Op][1];
  bool MustBeMemberOperator = OperatorUses[Op][2];

  // C++ [over.oper]p8:
  //   [...] Operator functions cannot have more or fewer parameters
  //   than the number required for the corresponding operator, as
  //   described in the rest of this subclause.
  unsigned NumParams = FnDecl->getNumParams() +
                       (isa<CXXMethodDecl>(FnDecl) &&
                                !FnDecl->hasCXXExplicitFunctionObjectParameter()
                            ? 1
                            : 0);
  if (Op != OO_Call && Op != OO_Subscript &&
      ((NumParams == 1 && !CanBeUnaryOperator) ||
       (NumParams == 2 && !CanBeBinaryOperator) || (NumParams < 1) ||
       (NumParams > 2))) {
    // We have the wrong number of parameters.
    unsigned ErrorKind;
    if (CanBeUnaryOperator && CanBeBinaryOperator) {
      ErrorKind = 2;  // 2 -> unary or binary.
    } else if (CanBeUnaryOperator) {
      ErrorKind = 0;  // 0 -> unary
    } else {
      assert(CanBeBinaryOperator &&
             "All non-call overloaded operators are unary or binary!");
      ErrorKind = 1;  // 1 -> binary
    }
    return Diag(FnDecl->getLocation(), diag::err_operator_overload_must_be)
      << FnDecl->getDeclName() << NumParams << ErrorKind;
  }

  if (Op == OO_Subscript && NumParams != 2) {
    Diag(FnDecl->getLocation(), LangOpts.CPlusPlus23
                                    ? diag::ext_subscript_overload
                                    : diag::error_subscript_overload)
        << FnDecl->getDeclName() << (NumParams == 1 ? 0 : 2);
  }

  // Overloaded operators other than operator() and operator[] cannot be
  // variadic.
  if (Op != OO_Call &&
      FnDecl->getType()->castAs<FunctionProtoType>()->isVariadic()) {
    return Diag(FnDecl->getLocation(), diag::err_operator_overload_variadic)
           << FnDecl->getDeclName();
  }

  // Some operators must be member functions.
  if (MustBeMemberOperator && !isa<CXXMethodDecl>(FnDecl)) {
    return Diag(FnDecl->getLocation(),
                diag::err_operator_overload_must_be_member)
      << FnDecl->getDeclName();
  }

  // C++ [over.inc]p1:
  //   The user-defined function called operator++ implements the
  //   prefix and postfix ++ operator. If this function is a member
  //   function with no parameters, or a non-member function with one
  //   parameter of class or enumeration type, it defines the prefix
  //   increment operator ++ for objects of that type. If the function
  //   is a member function with one parameter (which shall be of type
  //   int) or a non-member function with two parameters (the second
  //   of which shall be of type int), it defines the postfix
  //   increment operator ++ for objects of that type.
  if ((Op == OO_PlusPlus || Op == OO_MinusMinus) && NumParams == 2) {
    ParmVarDecl *LastParam = FnDecl->getParamDecl(FnDecl->getNumParams() - 1);
    QualType ParamType = LastParam->getType();

    if (!ParamType->isSpecificBuiltinType(BuiltinType::Int) &&
        !ParamType->isDependentType())
      return Diag(LastParam->getLocation(),
                  diag::err_operator_overload_post_incdec_must_be_int)
        << LastParam->getType() << (Op == OO_MinusMinus);
  }

  return false;
}

static bool
checkLiteralOperatorTemplateParameterList(Sema &SemaRef,
                                          FunctionTemplateDecl *TpDecl) {
  TemplateParameterList *TemplateParams = TpDecl->getTemplateParameters();

  // Must have one or two template parameters.
  if (TemplateParams->size() == 1) {
    NonTypeTemplateParmDecl *PmDecl =
        dyn_cast<NonTypeTemplateParmDecl>(TemplateParams->getParam(0));

    // The template parameter must be a char parameter pack.
    if (PmDecl && PmDecl->isTemplateParameterPack() &&
        SemaRef.Context.hasSameType(PmDecl->getType(), SemaRef.Context.CharTy))
      return false;

    // C++20 [over.literal]p5:
    //   A string literal operator template is a literal operator template
    //   whose template-parameter-list comprises a single non-type
    //   template-parameter of class type.
    //
    // As a DR resolution, we also allow placeholders for deduced class
    // template specializations.
    if (SemaRef.getLangOpts().CPlusPlus20 && PmDecl &&
        !PmDecl->isTemplateParameterPack() &&
        (PmDecl->getType()->isRecordType() ||
         PmDecl->getType()->getAs<DeducedTemplateSpecializationType>()))
      return false;
  } else if (TemplateParams->size() == 2) {
    TemplateTypeParmDecl *PmType =
        dyn_cast<TemplateTypeParmDecl>(TemplateParams->getParam(0));
    NonTypeTemplateParmDecl *PmArgs =
        dyn_cast<NonTypeTemplateParmDecl>(TemplateParams->getParam(1));

    // The second template parameter must be a parameter pack with the
    // first template parameter as its type.
    if (PmType && PmArgs && !PmType->isTemplateParameterPack() &&
        PmArgs->isTemplateParameterPack()) {
      const TemplateTypeParmType *TArgs =
          PmArgs->getType()->getAs<TemplateTypeParmType>();
      if (TArgs && TArgs->getDepth() == PmType->getDepth() &&
          TArgs->getIndex() == PmType->getIndex()) {
        if (!SemaRef.inTemplateInstantiation())
          SemaRef.Diag(TpDecl->getLocation(),
                       diag::ext_string_literal_operator_template);
        return false;
      }
    }
  }

  SemaRef.Diag(TpDecl->getTemplateParameters()->getSourceRange().getBegin(),
               diag::err_literal_operator_template)
      << TpDecl->getTemplateParameters()->getSourceRange();
  return true;
}

bool Sema::CheckLiteralOperatorDeclaration(FunctionDecl *FnDecl) {
  if (isa<CXXMethodDecl>(FnDecl)) {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_outside_namespace)
      << FnDecl->getDeclName();
    return true;
  }

  if (FnDecl->isExternC()) {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_extern_c);
    if (const LinkageSpecDecl *LSD =
            FnDecl->getDeclContext()->getExternCContext())
      Diag(LSD->getExternLoc(), diag::note_extern_c_begins_here);
    return true;
  }

  // This might be the definition of a literal operator template.
  FunctionTemplateDecl *TpDecl = FnDecl->getDescribedFunctionTemplate();

  // This might be a specialization of a literal operator template.
  if (!TpDecl)
    TpDecl = FnDecl->getPrimaryTemplate();

  // template <char...> type operator "" name() and
  // template <class T, T...> type operator "" name() are the only valid
  // template signatures, and the only valid signatures with no parameters.
  //
  // C++20 also allows template <SomeClass T> type operator "" name().
  if (TpDecl) {
    if (FnDecl->param_size() != 0) {
      Diag(FnDecl->getLocation(),
           diag::err_literal_operator_template_with_params);
      return true;
    }

    if (checkLiteralOperatorTemplateParameterList(*this, TpDecl))
      return true;

  } else if (FnDecl->param_size() == 1) {
    const ParmVarDecl *Param = FnDecl->getParamDecl(0);

    QualType ParamType = Param->getType().getUnqualifiedType();

    // Only unsigned long long int, long double, any character type, and const
    // char * are allowed as the only parameters.
    if (ParamType->isSpecificBuiltinType(BuiltinType::ULongLong) ||
        ParamType->isSpecificBuiltinType(BuiltinType::LongDouble) ||
        Context.hasSameType(ParamType, Context.CharTy) ||
        Context.hasSameType(ParamType, Context.WideCharTy) ||
        Context.hasSameType(ParamType, Context.Char8Ty) ||
        Context.hasSameType(ParamType, Context.Char16Ty) ||
        Context.hasSameType(ParamType, Context.Char32Ty)) {
    } else if (const PointerType *Ptr = ParamType->getAs<PointerType>()) {
      QualType InnerType = Ptr->getPointeeType();

      // Pointer parameter must be a const char *.
      if (!(Context.hasSameType(InnerType.getUnqualifiedType(),
                                Context.CharTy) &&
            InnerType.isConstQualified() && !InnerType.isVolatileQualified())) {
        Diag(Param->getSourceRange().getBegin(),
             diag::err_literal_operator_param)
            << ParamType << "'const char *'" << Param->getSourceRange();
        return true;
      }

    } else if (ParamType->isRealFloatingType()) {
      Diag(Param->getSourceRange().getBegin(), diag::err_literal_operator_param)
          << ParamType << Context.LongDoubleTy << Param->getSourceRange();
      return true;

    } else if (ParamType->isIntegerType()) {
      Diag(Param->getSourceRange().getBegin(), diag::err_literal_operator_param)
          << ParamType << Context.UnsignedLongLongTy << Param->getSourceRange();
      return true;

    } else {
      Diag(Param->getSourceRange().getBegin(),
           diag::err_literal_operator_invalid_param)
          << ParamType << Param->getSourceRange();
      return true;
    }

  } else if (FnDecl->param_size() == 2) {
    FunctionDecl::param_iterator Param = FnDecl->param_begin();

    // First, verify that the first parameter is correct.

    QualType FirstParamType = (*Param)->getType().getUnqualifiedType();

    // Two parameter function must have a pointer to const as a
    // first parameter; let's strip those qualifiers.
    const PointerType *PT = FirstParamType->getAs<PointerType>();

    if (!PT) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << FirstParamType << "'const char *'" << (*Param)->getSourceRange();
      return true;
    }

    QualType PointeeType = PT->getPointeeType();
    // First parameter must be const
    if (!PointeeType.isConstQualified() || PointeeType.isVolatileQualified()) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << FirstParamType << "'const char *'" << (*Param)->getSourceRange();
      return true;
    }

    QualType InnerType = PointeeType.getUnqualifiedType();
    // Only const char *, const wchar_t*, const char8_t*, const char16_t*, and
    // const char32_t* are allowed as the first parameter to a two-parameter
    // function
    if (!(Context.hasSameType(InnerType, Context.CharTy) ||
          Context.hasSameType(InnerType, Context.WideCharTy) ||
          Context.hasSameType(InnerType, Context.Char8Ty) ||
          Context.hasSameType(InnerType, Context.Char16Ty) ||
          Context.hasSameType(InnerType, Context.Char32Ty))) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << FirstParamType << "'const char *'" << (*Param)->getSourceRange();
      return true;
    }

    // Move on to the second and final parameter.
    ++Param;

    // The second parameter must be a std::size_t.
    QualType SecondParamType = (*Param)->getType().getUnqualifiedType();
    if (!Context.hasSameType(SecondParamType, Context.getSizeType())) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << SecondParamType << Context.getSizeType()
          << (*Param)->getSourceRange();
      return true;
    }
  } else {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_bad_param_count);
    return true;
  }

  // Parameters are good.

  // A parameter-declaration-clause containing a default argument is not
  // equivalent to any of the permitted forms.
  for (auto *Param : FnDecl->parameters()) {
    if (Param->hasDefaultArg()) {
      Diag(Param->getDefaultArgRange().getBegin(),
           diag::err_literal_operator_default_argument)
        << Param->getDefaultArgRange();
      break;
    }
  }

  const IdentifierInfo *II = FnDecl->getDeclName().getCXXLiteralIdentifier();
  ReservedLiteralSuffixIdStatus Status = II->isReservedLiteralSuffixId();
  if (Status != ReservedLiteralSuffixIdStatus::NotReserved &&
      !getSourceManager().isInSystemHeader(FnDecl->getLocation())) {
    // C++23 [usrlit.suffix]p1:
    //   Literal suffix identifiers that do not start with an underscore are
    //   reserved for future standardization. Literal suffix identifiers that
    //   contain a double underscore __ are reserved for use by C++
    //   implementations.
    Diag(FnDecl->getLocation(), diag::warn_user_literal_reserved)
        << static_cast<int>(Status)
        << StringLiteralParser::isValidUDSuffix(getLangOpts(), II->getName());
  }

  return false;
}

Decl *Sema::ActOnStartLinkageSpecification(Scope *S, SourceLocation ExternLoc,
                                           Expr *LangStr,
                                           SourceLocation LBraceLoc) {
  StringLiteral *Lit = cast<StringLiteral>(LangStr);
  assert(Lit->isUnevaluated() && "Unexpected string literal kind");

  StringRef Lang = Lit->getString();
  LinkageSpecLanguageIDs Language;
  if (Lang == "C")
    Language = LinkageSpecLanguageIDs::C;
  else if (Lang == "C++")
    Language = LinkageSpecLanguageIDs::CXX;
  else {
    Diag(LangStr->getExprLoc(), diag::err_language_linkage_spec_unknown)
      << LangStr->getSourceRange();
    return nullptr;
  }

  // FIXME: Add all the various semantics of linkage specifications

  LinkageSpecDecl *D = LinkageSpecDecl::Create(Context, CurContext, ExternLoc,
                                               LangStr->getExprLoc(), Language,
                                               LBraceLoc.isValid());

  /// C++ [module.unit]p7.2.3
  /// - Otherwise, if the declaration
  ///   - ...
  ///   - ...
  ///   - appears within a linkage-specification,
  ///   it is attached to the global module.
  ///
  /// If the declaration is already in global module fragment, we don't
  /// need to attach it again.
  if (getLangOpts().CPlusPlusModules && isCurrentModulePurview()) {
    Module *GlobalModule = PushImplicitGlobalModuleFragment(ExternLoc);
    D->setLocalOwningModule(GlobalModule);
  }

  CurContext->addDecl(D);
  PushDeclContext(S, D);
  return D;
}

Decl *Sema::ActOnFinishLinkageSpecification(Scope *S,
                                            Decl *LinkageSpec,
                                            SourceLocation RBraceLoc) {
  if (RBraceLoc.isValid()) {
    LinkageSpecDecl* LSDecl = cast<LinkageSpecDecl>(LinkageSpec);
    LSDecl->setRBraceLoc(RBraceLoc);
  }

  // If the current module doesn't has Parent, it implies that the
  // LinkageSpec isn't in the module created by itself. So we don't
  // need to pop it.
  if (getLangOpts().CPlusPlusModules && getCurrentModule() &&
      getCurrentModule()->isImplicitGlobalModule() &&
      getCurrentModule()->Parent)
    PopImplicitGlobalModuleFragment();

  PopDeclContext();
  return LinkageSpec;
}

Decl *Sema::ActOnEmptyDeclaration(Scope *S,
                                  const ParsedAttributesView &AttrList,
                                  SourceLocation SemiLoc) {
  Decl *ED = EmptyDecl::Create(Context, CurContext, SemiLoc);
  // Attribute declarations appertain to empty declaration so we handle
  // them here.
  ProcessDeclAttributeList(S, ED, AttrList);

  CurContext->addDecl(ED);
  return ED;
}

VarDecl *Sema::BuildExceptionDeclaration(Scope *S, TypeSourceInfo *TInfo,
                                         SourceLocation StartLoc,
                                         SourceLocation Loc,
                                         const IdentifierInfo *Name) {
  bool Invalid = false;
  QualType ExDeclType = TInfo->getType();

  // Arrays and functions decay.
  if (ExDeclType->isArrayType())
    ExDeclType = Context.getArrayDecayedType(ExDeclType);
  else if (ExDeclType->isFunctionType())
    ExDeclType = Context.getPointerType(ExDeclType);

  // C++ 15.3p1: The exception-declaration shall not denote an incomplete type.
  // The exception-declaration shall not denote a pointer or reference to an
  // incomplete type, other than [cv] void*.
  // N2844 forbids rvalue references.
  if (!ExDeclType->isDependentType() && ExDeclType->isRValueReferenceType()) {
    Diag(Loc, diag::err_catch_rvalue_ref);
    Invalid = true;
  }

  if (ExDeclType->isVariablyModifiedType()) {
    Diag(Loc, diag::err_catch_variably_modified) << ExDeclType;
    Invalid = true;
  }

  QualType BaseType = ExDeclType;
  int Mode = 0; // 0 for direct type, 1 for pointer, 2 for reference
  unsigned DK = diag::err_catch_incomplete;
  if (const PointerType *Ptr = BaseType->getAs<PointerType>()) {
    BaseType = Ptr->getPointeeType();
    Mode = 1;
    DK = diag::err_catch_incomplete_ptr;
  } else if (const ReferenceType *Ref = BaseType->getAs<ReferenceType>()) {
    // For the purpose of error recovery, we treat rvalue refs like lvalue refs.
    BaseType = Ref->getPointeeType();
    Mode = 2;
    DK = diag::err_catch_incomplete_ref;
  }
  if (!Invalid && (Mode == 0 || !BaseType->isVoidType()) &&
      !BaseType->isDependentType() && RequireCompleteType(Loc, BaseType, DK))
    Invalid = true;

  if (!Invalid && BaseType.isWebAssemblyReferenceType()) {
    Diag(Loc, diag::err_wasm_reftype_tc) << 1;
    Invalid = true;
  }

  if (!Invalid && Mode != 1 && BaseType->isSizelessType()) {
    Diag(Loc, diag::err_catch_sizeless) << (Mode == 2 ? 1 : 0) << BaseType;
    Invalid = true;
  }

  if (!Invalid && !ExDeclType->isDependentType() &&
      RequireNonAbstractType(Loc, ExDeclType,
                             diag::err_abstract_type_in_decl,
                             AbstractVariableType))
    Invalid = true;

  // Only the non-fragile NeXT runtime currently supports C++ catches
  // of ObjC types, and no runtime supports catching ObjC types by value.
  if (!Invalid && getLangOpts().ObjC) {
    QualType T = ExDeclType;
    if (const ReferenceType *RT = T->getAs<ReferenceType>())
      T = RT->getPointeeType();

    if (T->isObjCObjectType()) {
      Diag(Loc, diag::err_objc_object_catch);
      Invalid = true;
    } else if (T->isObjCObjectPointerType()) {
      // FIXME: should this be a test for macosx-fragile specifically?
      if (getLangOpts().ObjCRuntime.isFragile())
        Diag(Loc, diag::warn_objc_pointer_cxx_catch_fragile);
    }
  }

  VarDecl *ExDecl = VarDecl::Create(Context, CurContext, StartLoc, Loc, Name,
                                    ExDeclType, TInfo, SC_None);
  ExDecl->setExceptionVariable(true);

  // In ARC, infer 'retaining' for variables of retainable type.
  if (getLangOpts().ObjCAutoRefCount && ObjC().inferObjCARCLifetime(ExDecl))
    Invalid = true;

  if (!Invalid && !ExDeclType->isDependentType()) {
    if (const RecordType *recordType = ExDeclType->getAs<RecordType>()) {
      // Insulate this from anything else we might currently be parsing.
      EnterExpressionEvaluationContext scope(
          *this, ExpressionEvaluationContext::PotentiallyEvaluated);

      // C++ [except.handle]p16:
      //   The object declared in an exception-declaration or, if the
      //   exception-declaration does not specify a name, a temporary (12.2) is
      //   copy-initialized (8.5) from the exception object. [...]
      //   The object is destroyed when the handler exits, after the destruction
      //   of any automatic objects initialized within the handler.
      //
      // We just pretend to initialize the object with itself, then make sure
      // it can be destroyed later.
      QualType initType = Context.getExceptionObjectType(ExDeclType);

      InitializedEntity entity =
        InitializedEntity::InitializeVariable(ExDecl);
      InitializationKind initKind =
        InitializationKind::CreateCopy(Loc, SourceLocation());

      Expr *opaqueValue =
        new (Context) OpaqueValueExpr(Loc, initType, VK_LValue, OK_Ordinary);
      InitializationSequence sequence(*this, entity, initKind, opaqueValue);
      ExprResult result = sequence.Perform(*this, entity, initKind, opaqueValue);
      if (result.isInvalid())
        Invalid = true;
      else {
        // If the constructor used was non-trivial, set this as the
        // "initializer".
        CXXConstructExpr *construct = result.getAs<CXXConstructExpr>();
        if (!construct->getConstructor()->isTrivial()) {
          Expr *init = MaybeCreateExprWithCleanups(construct);
          ExDecl->setInit(init);
        }

        // And make sure it's destructable.
        FinalizeVarWithDestructor(ExDecl, recordType);
      }
    }
  }

  if (Invalid)
    ExDecl->setInvalidDecl();

  return ExDecl;
}

Decl *Sema::ActOnExceptionDeclarator(Scope *S, Declarator &D) {
  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  bool Invalid = D.isInvalidType();

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                      UPPC_ExceptionType)) {
    TInfo = Context.getTrivialTypeSourceInfo(Context.IntTy,
                                             D.getIdentifierLoc());
    Invalid = true;
  }

  const IdentifierInfo *II = D.getIdentifier();
  if (NamedDecl *PrevDecl =
          LookupSingleName(S, II, D.getIdentifierLoc(), LookupOrdinaryName,
                           RedeclarationKind::ForVisibleRedeclaration)) {
    // The scope should be freshly made just for us. There is just no way
    // it contains any previous declaration, except for function parameters in
    // a function-try-block's catch statement.
    assert(!S->isDeclScope(PrevDecl));
    if (isDeclInScope(PrevDecl, CurContext, S)) {
      Diag(D.getIdentifierLoc(), diag::err_redefinition)
        << D.getIdentifier();
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      Invalid = true;
    } else if (PrevDecl->isTemplateParameter())
      // Maybe we will complain about the shadowed template parameter.
      DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), PrevDecl);
  }

  if (D.getCXXScopeSpec().isSet() && !Invalid) {
    Diag(D.getIdentifierLoc(), diag::err_qualified_catch_declarator)
      << D.getCXXScopeSpec().getRange();
    Invalid = true;
  }

  VarDecl *ExDecl = BuildExceptionDeclaration(
      S, TInfo, D.getBeginLoc(), D.getIdentifierLoc(), D.getIdentifier());
  if (Invalid)
    ExDecl->setInvalidDecl();

  // Add the exception declaration into this scope.
  if (II)
    PushOnScopeChains(ExDecl, S);
  else
    CurContext->addDecl(ExDecl);

  ProcessDeclAttributes(S, ExDecl, D);
  return ExDecl;
}

Decl *Sema::ActOnStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                         Expr *AssertExpr,
                                         Expr *AssertMessageExpr,
                                         SourceLocation RParenLoc) {
  if (DiagnoseUnexpandedParameterPack(AssertExpr, UPPC_StaticAssertExpression))
    return nullptr;

  return BuildStaticAssertDeclaration(StaticAssertLoc, AssertExpr,
                                      AssertMessageExpr, RParenLoc, false);
}

static void WriteCharTypePrefix(BuiltinType::Kind BTK, llvm::raw_ostream &OS) {
  switch (BTK) {
  case BuiltinType::Char_S:
  case BuiltinType::Char_U:
    break;
  case BuiltinType::Char8:
    OS << "u8";
    break;
  case BuiltinType::Char16:
    OS << 'u';
    break;
  case BuiltinType::Char32:
    OS << 'U';
    break;
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    OS << 'L';
    break;
  default:
    llvm_unreachable("Non-character type");
  }
}

/// Convert character's value, interpreted as a code unit, to a string.
/// The value needs to be zero-extended to 32-bits.
/// FIXME: This assumes Unicode literal encodings
static void WriteCharValueForDiagnostic(uint32_t Value, const BuiltinType *BTy,
                                        unsigned TyWidth,
                                        SmallVectorImpl<char> &Str) {
  char Arr[UNI_MAX_UTF8_BYTES_PER_CODE_POINT];
  char *Ptr = Arr;
  BuiltinType::Kind K = BTy->getKind();
  llvm::raw_svector_ostream OS(Str);

  // This should catch Char_S, Char_U, Char8, and use of escaped characters in
  // other types.
  if (K == BuiltinType::Char_S || K == BuiltinType::Char_U ||
      K == BuiltinType::Char8 || Value <= 0x7F) {
    StringRef Escaped = escapeCStyle<EscapeChar::Single>(Value);
    if (!Escaped.empty())
      EscapeStringForDiagnostic(Escaped, Str);
    else
      OS << static_cast<char>(Value);
    return;
  }

  switch (K) {
  case BuiltinType::Char16:
  case BuiltinType::Char32:
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U: {
    if (llvm::ConvertCodePointToUTF8(Value, Ptr))
      EscapeStringForDiagnostic(StringRef(Arr, Ptr - Arr), Str);
    else
      OS << "\\x"
         << llvm::format_hex_no_prefix(Value, TyWidth / 4, /*Upper=*/true);
    break;
  }
  default:
    llvm_unreachable("Non-character type is passed");
  }
}

/// Convert \V to a string we can present to the user in a diagnostic
/// \T is the type of the expression that has been evaluated into \V
static bool ConvertAPValueToString(const APValue &V, QualType T,
                                   SmallVectorImpl<char> &Str,
                                   ASTContext &Context) {
  if (!V.hasValue())
    return false;

  switch (V.getKind()) {
  case APValue::ValueKind::Int:
    if (T->isBooleanType()) {
      // Bools are reduced to ints during evaluation, but for
      // diagnostic purposes we want to print them as
      // true or false.
      int64_t BoolValue = V.getInt().getExtValue();
      assert((BoolValue == 0 || BoolValue == 1) &&
             "Bool type, but value is not 0 or 1");
      llvm::raw_svector_ostream OS(Str);
      OS << (BoolValue ? "true" : "false");
    } else {
      llvm::raw_svector_ostream OS(Str);
      // Same is true for chars.
      // We want to print the character representation for textual types
      const auto *BTy = T->getAs<BuiltinType>();
      if (BTy) {
        switch (BTy->getKind()) {
        case BuiltinType::Char_S:
        case BuiltinType::Char_U:
        case BuiltinType::Char8:
        case BuiltinType::Char16:
        case BuiltinType::Char32:
        case BuiltinType::WChar_S:
        case BuiltinType::WChar_U: {
          unsigned TyWidth = Context.getIntWidth(T);
          assert(8 <= TyWidth && TyWidth <= 32 && "Unexpected integer width");
          uint32_t CodeUnit = static_cast<uint32_t>(V.getInt().getZExtValue());
          WriteCharTypePrefix(BTy->getKind(), OS);
          OS << '\'';
          WriteCharValueForDiagnostic(CodeUnit, BTy, TyWidth, Str);
          OS << "' (0x"
             << llvm::format_hex_no_prefix(CodeUnit, /*Width=*/2,
                                           /*Upper=*/true)
             << ", " << V.getInt() << ')';
          return true;
        }
        default:
          break;
        }
      }
      V.getInt().toString(Str);
    }

    break;

  case APValue::ValueKind::Float:
    V.getFloat().toString(Str);
    break;

  case APValue::ValueKind::LValue:
    if (V.isNullPointer()) {
      llvm::raw_svector_ostream OS(Str);
      OS << "nullptr";
    } else
      return false;
    break;

  case APValue::ValueKind::ComplexFloat: {
    llvm::raw_svector_ostream OS(Str);
    OS << '(';
    V.getComplexFloatReal().toString(Str);
    OS << " + ";
    V.getComplexFloatImag().toString(Str);
    OS << "i)";
  } break;

  case APValue::ValueKind::ComplexInt: {
    llvm::raw_svector_ostream OS(Str);
    OS << '(';
    V.getComplexIntReal().toString(Str);
    OS << " + ";
    V.getComplexIntImag().toString(Str);
    OS << "i)";
  } break;

  default:
    return false;
  }

  return true;
}

/// Some Expression types are not useful to print notes about,
/// e.g. literals and values that have already been expanded
/// before such as int-valued template parameters.
static bool UsefulToPrintExpr(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  // Literals are pretty easy for humans to understand.
  if (isa<IntegerLiteral, FloatingLiteral, CharacterLiteral, CXXBoolLiteralExpr,
          CXXNullPtrLiteralExpr, FixedPointLiteral, ImaginaryLiteral>(E))
    return false;

  // These have been substituted from template parameters
  // and appear as literals in the static assert error.
  if (isa<SubstNonTypeTemplateParmExpr>(E))
    return false;

  // -5 is also simple to understand.
  if (const auto *UnaryOp = dyn_cast<UnaryOperator>(E))
    return UsefulToPrintExpr(UnaryOp->getSubExpr());

  // Only print nested arithmetic operators.
  if (const auto *BO = dyn_cast<BinaryOperator>(E))
    return (BO->isShiftOp() || BO->isAdditiveOp() || BO->isMultiplicativeOp() ||
            BO->isBitwiseOp());

  return true;
}

void Sema::DiagnoseStaticAssertDetails(const Expr *E) {
  if (const auto *Op = dyn_cast<BinaryOperator>(E);
      Op && Op->getOpcode() != BO_LOr) {
    const Expr *LHS = Op->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = Op->getRHS()->IgnoreParenImpCasts();

    // Ignore comparisons of boolean expressions with a boolean literal.
    if ((isa<CXXBoolLiteralExpr>(LHS) && RHS->getType()->isBooleanType()) ||
        (isa<CXXBoolLiteralExpr>(RHS) && LHS->getType()->isBooleanType()))
      return;

    // Don't print obvious expressions.
    if (!UsefulToPrintExpr(LHS) && !UsefulToPrintExpr(RHS))
      return;

    struct {
      const clang::Expr *Cond;
      Expr::EvalResult Result;
      SmallString<12> ValueString;
      bool Print;
    } DiagSide[2] = {{LHS, Expr::EvalResult(), {}, false},
                     {RHS, Expr::EvalResult(), {}, false}};
    for (unsigned I = 0; I < 2; I++) {
      const Expr *Side = DiagSide[I].Cond;

      Side->EvaluateAsRValue(DiagSide[I].Result, Context, true);

      DiagSide[I].Print =
          ConvertAPValueToString(DiagSide[I].Result.Val, Side->getType(),
                                 DiagSide[I].ValueString, Context);
    }
    if (DiagSide[0].Print && DiagSide[1].Print) {
      Diag(Op->getExprLoc(), diag::note_expr_evaluates_to)
          << DiagSide[0].ValueString << Op->getOpcodeStr()
          << DiagSide[1].ValueString << Op->getSourceRange();
    }
  }
}

bool Sema::EvaluateStaticAssertMessageAsString(Expr *Message,
                                               std::string &Result,
                                               ASTContext &Ctx,
                                               bool ErrorOnInvalidMessage) {
  assert(Message);
  assert(!Message->isTypeDependent() && !Message->isValueDependent() &&
         "can't evaluate a dependant static assert message");

  if (const auto *SL = dyn_cast<StringLiteral>(Message)) {
    assert(SL->isUnevaluated() && "expected an unevaluated string");
    Result.assign(SL->getString().begin(), SL->getString().end());
    return true;
  }

  SourceLocation Loc = Message->getBeginLoc();
  QualType T = Message->getType().getNonReferenceType();
  auto *RD = T->getAsCXXRecordDecl();
  if (!RD) {
    Diag(Loc, diag::err_static_assert_invalid_message);
    return false;
  }

  auto FindMember = [&](StringRef Member, bool &Empty,
                        bool Diag = false) -> std::optional<LookupResult> {
    DeclarationName DN = PP.getIdentifierInfo(Member);
    LookupResult MemberLookup(*this, DN, Loc, Sema::LookupMemberName);
    LookupQualifiedName(MemberLookup, RD);
    Empty = MemberLookup.empty();
    OverloadCandidateSet Candidates(MemberLookup.getNameLoc(),
                                    OverloadCandidateSet::CSK_Normal);
    if (MemberLookup.empty())
      return std::nullopt;
    return std::move(MemberLookup);
  };

  bool SizeNotFound, DataNotFound;
  std::optional<LookupResult> SizeMember = FindMember("size", SizeNotFound);
  std::optional<LookupResult> DataMember = FindMember("data", DataNotFound);
  if (SizeNotFound || DataNotFound) {
    Diag(Loc, diag::err_static_assert_missing_member_function)
        << ((SizeNotFound && DataNotFound) ? 2
            : SizeNotFound                 ? 0
                                           : 1);
    return false;
  }

  if (!SizeMember || !DataMember) {
    if (!SizeMember)
      FindMember("size", SizeNotFound, /*Diag=*/true);
    if (!DataMember)
      FindMember("data", DataNotFound, /*Diag=*/true);
    return false;
  }

  auto BuildExpr = [&](LookupResult &LR) {
    ExprResult Res = BuildMemberReferenceExpr(
        Message, Message->getType(), Message->getBeginLoc(), false,
        CXXScopeSpec(), SourceLocation(), nullptr, LR, nullptr, nullptr);
    if (Res.isInvalid())
      return ExprError();
    Res = BuildCallExpr(nullptr, Res.get(), Loc, std::nullopt, Loc, nullptr,
                        false, true);
    if (Res.isInvalid())
      return ExprError();
    if (Res.get()->isTypeDependent() || Res.get()->isValueDependent())
      return ExprError();
    return TemporaryMaterializationConversion(Res.get());
  };

  ExprResult SizeE = BuildExpr(*SizeMember);
  ExprResult DataE = BuildExpr(*DataMember);

  QualType SizeT = Context.getSizeType();
  QualType ConstCharPtr =
      Context.getPointerType(Context.getConstType(Context.CharTy));

  ExprResult EvaluatedSize =
      SizeE.isInvalid() ? ExprError()
                        : BuildConvertedConstantExpression(
                              SizeE.get(), SizeT, CCEK_StaticAssertMessageSize);
  if (EvaluatedSize.isInvalid()) {
    Diag(Loc, diag::err_static_assert_invalid_mem_fn_ret_ty) << /*size*/ 0;
    return false;
  }

  ExprResult EvaluatedData =
      DataE.isInvalid()
          ? ExprError()
          : BuildConvertedConstantExpression(DataE.get(), ConstCharPtr,
                                             CCEK_StaticAssertMessageData);
  if (EvaluatedData.isInvalid()) {
    Diag(Loc, diag::err_static_assert_invalid_mem_fn_ret_ty) << /*data*/ 1;
    return false;
  }

  if (!ErrorOnInvalidMessage &&
      Diags.isIgnored(diag::warn_static_assert_message_constexpr, Loc))
    return true;

  Expr::EvalResult Status;
  SmallVector<PartialDiagnosticAt, 8> Notes;
  Status.Diag = &Notes;
  if (!Message->EvaluateCharRangeAsString(Result, EvaluatedSize.get(),
                                          EvaluatedData.get(), Ctx, Status) ||
      !Notes.empty()) {
    Diag(Message->getBeginLoc(),
         ErrorOnInvalidMessage ? diag::err_static_assert_message_constexpr
                               : diag::warn_static_assert_message_constexpr);
    for (const auto &Note : Notes)
      Diag(Note.first, Note.second);
    return !ErrorOnInvalidMessage;
  }
  return true;
}

Decl *Sema::BuildStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                         Expr *AssertExpr, Expr *AssertMessage,
                                         SourceLocation RParenLoc,
                                         bool Failed) {
  assert(AssertExpr != nullptr && "Expected non-null condition");
  if (!AssertExpr->isTypeDependent() && !AssertExpr->isValueDependent() &&
      (!AssertMessage || (!AssertMessage->isTypeDependent() &&
                          !AssertMessage->isValueDependent())) &&
      !Failed) {
    // In a static_assert-declaration, the constant-expression shall be a
    // constant expression that can be contextually converted to bool.
    ExprResult Converted = PerformContextuallyConvertToBool(AssertExpr);
    if (Converted.isInvalid())
      Failed = true;

    ExprResult FullAssertExpr =
        ActOnFinishFullExpr(Converted.get(), StaticAssertLoc,
                            /*DiscardedValue*/ false,
                            /*IsConstexpr*/ true);
    if (FullAssertExpr.isInvalid())
      Failed = true;
    else
      AssertExpr = FullAssertExpr.get();

    llvm::APSInt Cond;
    Expr *BaseExpr = AssertExpr;
    AllowFoldKind FoldKind = NoFold;

    if (!getLangOpts().CPlusPlus) {
      // In C mode, allow folding as an extension for better compatibility with
      // C++ in terms of expressions like static_assert("test") or
      // static_assert(nullptr).
      FoldKind = AllowFold;
    }

    if (!Failed && VerifyIntegerConstantExpression(
                       BaseExpr, &Cond,
                       diag::err_static_assert_expression_is_not_constant,
                       FoldKind).isInvalid())
      Failed = true;

    // If the static_assert passes, only verify that
    // the message is grammatically valid without evaluating it.
    if (!Failed && AssertMessage && Cond.getBoolValue()) {
      std::string Str;
      EvaluateStaticAssertMessageAsString(AssertMessage, Str, Context,
                                          /*ErrorOnInvalidMessage=*/false);
    }

    // CWG2518
    // [dcl.pre]/p10  If [...] the expression is evaluated in the context of a
    // template definition, the declaration has no effect.
    bool InTemplateDefinition =
        getLangOpts().CPlusPlus && CurContext->isDependentContext();

    if (!Failed && !Cond && !InTemplateDefinition) {
      SmallString<256> MsgBuffer;
      llvm::raw_svector_ostream Msg(MsgBuffer);
      bool HasMessage = AssertMessage;
      if (AssertMessage) {
        std::string Str;
        HasMessage =
            EvaluateStaticAssertMessageAsString(
                AssertMessage, Str, Context, /*ErrorOnInvalidMessage=*/true) ||
            !Str.empty();
        Msg << Str;
      }
      Expr *InnerCond = nullptr;
      std::string InnerCondDescription;
      std::tie(InnerCond, InnerCondDescription) =
        findFailedBooleanCondition(Converted.get());
      if (InnerCond && isa<ConceptSpecializationExpr>(InnerCond)) {
        // Drill down into concept specialization expressions to see why they
        // weren't satisfied.
        Diag(AssertExpr->getBeginLoc(), diag::err_static_assert_failed)
            << !HasMessage << Msg.str() << AssertExpr->getSourceRange();
        ConstraintSatisfaction Satisfaction;
        if (!CheckConstraintSatisfaction(InnerCond, Satisfaction))
          DiagnoseUnsatisfiedConstraint(Satisfaction);
      } else if (InnerCond && !isa<CXXBoolLiteralExpr>(InnerCond)
                           && !isa<IntegerLiteral>(InnerCond)) {
        Diag(InnerCond->getBeginLoc(),
             diag::err_static_assert_requirement_failed)
            << InnerCondDescription << !HasMessage << Msg.str()
            << InnerCond->getSourceRange();
        DiagnoseStaticAssertDetails(InnerCond);
      } else {
        Diag(AssertExpr->getBeginLoc(), diag::err_static_assert_failed)
            << !HasMessage << Msg.str() << AssertExpr->getSourceRange();
        PrintContextStack();
      }
      Failed = true;
    }
  } else {
    ExprResult FullAssertExpr = ActOnFinishFullExpr(AssertExpr, StaticAssertLoc,
                                                    /*DiscardedValue*/false,
                                                    /*IsConstexpr*/true);
    if (FullAssertExpr.isInvalid())
      Failed = true;
    else
      AssertExpr = FullAssertExpr.get();
  }

  Decl *Decl = StaticAssertDecl::Create(Context, CurContext, StaticAssertLoc,
                                        AssertExpr, AssertMessage, RParenLoc,
                                        Failed);

  CurContext->addDecl(Decl);
  return Decl;
}

DeclResult Sema::ActOnTemplatedFriendTag(
    Scope *S, SourceLocation FriendLoc, unsigned TagSpec, SourceLocation TagLoc,
    CXXScopeSpec &SS, IdentifierInfo *Name, SourceLocation NameLoc,
    const ParsedAttributesView &Attr, MultiTemplateParamsArg TempParamLists) {
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);

  bool IsMemberSpecialization = false;
  bool Invalid = false;

  if (TemplateParameterList *TemplateParams =
          MatchTemplateParametersToScopeSpecifier(
              TagLoc, NameLoc, SS, nullptr, TempParamLists, /*friend*/ true,
              IsMemberSpecialization, Invalid)) {
    if (TemplateParams->size() > 0) {
      // This is a declaration of a class template.
      if (Invalid)
        return true;

      return CheckClassTemplate(S, TagSpec, TagUseKind::Friend, TagLoc, SS,
                                Name, NameLoc, Attr, TemplateParams, AS_public,
                                /*ModulePrivateLoc=*/SourceLocation(),
                                FriendLoc, TempParamLists.size() - 1,
                                TempParamLists.data())
          .get();
    } else {
      // The "template<>" header is extraneous.
      Diag(TemplateParams->getTemplateLoc(), diag::err_template_tag_noparams)
        << TypeWithKeyword::getTagTypeKindName(Kind) << Name;
      IsMemberSpecialization = true;
    }
  }

  if (Invalid) return true;

  bool isAllExplicitSpecializations = true;
  for (unsigned I = TempParamLists.size(); I-- > 0; ) {
    if (TempParamLists[I]->size()) {
      isAllExplicitSpecializations = false;
      break;
    }
  }

  // FIXME: don't ignore attributes.

  // If it's explicit specializations all the way down, just forget
  // about the template header and build an appropriate non-templated
  // friend.  TODO: for source fidelity, remember the headers.
  if (isAllExplicitSpecializations) {
    if (SS.isEmpty()) {
      bool Owned = false;
      bool IsDependent = false;
      return ActOnTag(S, TagSpec, TagUseKind::Friend, TagLoc, SS, Name, NameLoc,
                      Attr, AS_public,
                      /*ModulePrivateLoc=*/SourceLocation(),
                      MultiTemplateParamsArg(), Owned, IsDependent,
                      /*ScopedEnumKWLoc=*/SourceLocation(),
                      /*ScopedEnumUsesClassTag=*/false,
                      /*UnderlyingType=*/TypeResult(),
                      /*IsTypeSpecifier=*/false,
                      /*IsTemplateParamOrArg=*/false, /*OOK=*/OOK_Outside);
    }

    NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
    ElaboratedTypeKeyword Keyword
      = TypeWithKeyword::getKeywordForTagTypeKind(Kind);
    QualType T = CheckTypenameType(Keyword, TagLoc, QualifierLoc,
                                   *Name, NameLoc);
    if (T.isNull())
      return true;

    TypeSourceInfo *TSI = Context.CreateTypeSourceInfo(T);
    if (isa<DependentNameType>(T)) {
      DependentNameTypeLoc TL =
          TSI->getTypeLoc().castAs<DependentNameTypeLoc>();
      TL.setElaboratedKeywordLoc(TagLoc);
      TL.setQualifierLoc(QualifierLoc);
      TL.setNameLoc(NameLoc);
    } else {
      ElaboratedTypeLoc TL = TSI->getTypeLoc().castAs<ElaboratedTypeLoc>();
      TL.setElaboratedKeywordLoc(TagLoc);
      TL.setQualifierLoc(QualifierLoc);
      TL.getNamedTypeLoc().castAs<TypeSpecTypeLoc>().setNameLoc(NameLoc);
    }

    FriendDecl *Friend = FriendDecl::Create(Context, CurContext, NameLoc,
                                            TSI, FriendLoc, TempParamLists);
    Friend->setAccess(AS_public);
    CurContext->addDecl(Friend);
    return Friend;
  }

  assert(SS.isNotEmpty() && "valid templated tag with no SS and no direct?");



  // Handle the case of a templated-scope friend class.  e.g.
  //   template <class T> class A<T>::B;
  // FIXME: we don't support these right now.
  Diag(NameLoc, diag::warn_template_qualified_friend_unsupported)
    << SS.getScopeRep() << SS.getRange() << cast<CXXRecordDecl>(CurContext);
  ElaboratedTypeKeyword ETK = TypeWithKeyword::getKeywordForTagTypeKind(Kind);
  QualType T = Context.getDependentNameType(ETK, SS.getScopeRep(), Name);
  TypeSourceInfo *TSI = Context.CreateTypeSourceInfo(T);
  DependentNameTypeLoc TL = TSI->getTypeLoc().castAs<DependentNameTypeLoc>();
  TL.setElaboratedKeywordLoc(TagLoc);
  TL.setQualifierLoc(SS.getWithLocInContext(Context));
  TL.setNameLoc(NameLoc);

  FriendDecl *Friend = FriendDecl::Create(Context, CurContext, NameLoc,
                                          TSI, FriendLoc, TempParamLists);
  Friend->setAccess(AS_public);
  Friend->setUnsupportedFriend(true);
  CurContext->addDecl(Friend);
  return Friend;
}

Decl *Sema::ActOnFriendTypeDecl(Scope *S, const DeclSpec &DS,
                                MultiTemplateParamsArg TempParams) {
  SourceLocation Loc = DS.getBeginLoc();
  SourceLocation FriendLoc = DS.getFriendSpecLoc();

  assert(DS.isFriendSpecified());
  assert(DS.getStorageClassSpec() == DeclSpec::SCS_unspecified);

  // C++ [class.friend]p3:
  // A friend declaration that does not declare a function shall have one of
  // the following forms:
  //     friend elaborated-type-specifier ;
  //     friend simple-type-specifier ;
  //     friend typename-specifier ;
  //
  // If the friend keyword isn't first, or if the declarations has any type
  // qualifiers, then the declaration doesn't have that form.
  if (getLangOpts().CPlusPlus11 && !DS.isFriendSpecifiedFirst())
    Diag(FriendLoc, diag::err_friend_not_first_in_declaration);
  if (DS.getTypeQualifiers()) {
    if (DS.getTypeQualifiers() & DeclSpec::TQ_const)
      Diag(DS.getConstSpecLoc(), diag::err_friend_decl_spec) << "const";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_volatile)
      Diag(DS.getVolatileSpecLoc(), diag::err_friend_decl_spec) << "volatile";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_restrict)
      Diag(DS.getRestrictSpecLoc(), diag::err_friend_decl_spec) << "restrict";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_atomic)
      Diag(DS.getAtomicSpecLoc(), diag::err_friend_decl_spec) << "_Atomic";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_unaligned)
      Diag(DS.getUnalignedSpecLoc(), diag::err_friend_decl_spec) << "__unaligned";
  }

  // Try to convert the decl specifier to a type.  This works for
  // friend templates because ActOnTag never produces a ClassTemplateDecl
  // for a TagUseKind::Friend.
  Declarator TheDeclarator(DS, ParsedAttributesView::none(),
                           DeclaratorContext::Member);
  TypeSourceInfo *TSI = GetTypeForDeclarator(TheDeclarator);
  QualType T = TSI->getType();
  if (TheDeclarator.isInvalidType())
    return nullptr;

  if (DiagnoseUnexpandedParameterPack(Loc, TSI, UPPC_FriendDeclaration))
    return nullptr;

  if (!T->isElaboratedTypeSpecifier()) {
    if (TempParams.size()) {
      // C++23 [dcl.pre]p5:
      //   In a simple-declaration, the optional init-declarator-list can be
      //   omitted only when declaring a class or enumeration, that is, when
      //   the decl-specifier-seq contains either a class-specifier, an
      //   elaborated-type-specifier with a class-key, or an enum-specifier.
      //
      // The declaration of a template-declaration or explicit-specialization
      // is never a member-declaration, so this must be a simple-declaration
      // with no init-declarator-list. Therefore, this is ill-formed.
      Diag(Loc, diag::err_tagless_friend_type_template) << DS.getSourceRange();
      return nullptr;
    } else if (const RecordDecl *RD = T->getAsRecordDecl()) {
      SmallString<16> InsertionText(" ");
      InsertionText += RD->getKindName();

      Diag(Loc, getLangOpts().CPlusPlus11
                    ? diag::warn_cxx98_compat_unelaborated_friend_type
                    : diag::ext_unelaborated_friend_type)
          << (unsigned)RD->getTagKind() << T
          << FixItHint::CreateInsertion(getLocForEndOfToken(FriendLoc),
                                        InsertionText);
    } else {
      Diag(FriendLoc, getLangOpts().CPlusPlus11
                          ? diag::warn_cxx98_compat_nonclass_type_friend
                          : diag::ext_nonclass_type_friend)
          << T << DS.getSourceRange();
    }
  }

  // C++98 [class.friend]p1: A friend of a class is a function
  //   or class that is not a member of the class . . .
  // This is fixed in DR77, which just barely didn't make the C++03
  // deadline.  It's also a very silly restriction that seriously
  // affects inner classes and which nobody else seems to implement;
  // thus we never diagnose it, not even in -pedantic.
  //
  // But note that we could warn about it: it's always useless to
  // friend one of your own members (it's not, however, worthless to
  // friend a member of an arbitrary specialization of your template).

  Decl *D;
  if (!TempParams.empty())
    D = FriendTemplateDecl::Create(Context, CurContext, Loc, TempParams, TSI,
                                   FriendLoc);
  else
    D = FriendDecl::Create(Context, CurContext, TSI->getTypeLoc().getBeginLoc(),
                           TSI, FriendLoc);

  if (!D)
    return nullptr;

  D->setAccess(AS_public);
  CurContext->addDecl(D);

  return D;
}

NamedDecl *Sema::ActOnFriendFunctionDecl(Scope *S, Declarator &D,
                                        MultiTemplateParamsArg TemplateParams) {
  const DeclSpec &DS = D.getDeclSpec();

  assert(DS.isFriendSpecified());
  assert(DS.getStorageClassSpec() == DeclSpec::SCS_unspecified);

  SourceLocation Loc = D.getIdentifierLoc();
  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);

  // C++ [class.friend]p1
  //   A friend of a class is a function or class....
  // Note that this sees through typedefs, which is intended.
  // It *doesn't* see through dependent types, which is correct
  // according to [temp.arg.type]p3:
  //   If a declaration acquires a function type through a
  //   type dependent on a template-parameter and this causes
  //   a declaration that does not use the syntactic form of a
  //   function declarator to have a function type, the program
  //   is ill-formed.
  if (!TInfo->getType()->isFunctionType()) {
    Diag(Loc, diag::err_unexpected_friend);

    // It might be worthwhile to try to recover by creating an
    // appropriate declaration.
    return nullptr;
  }

  // C++ [namespace.memdef]p3
  //  - If a friend declaration in a non-local class first declares a
  //    class or function, the friend class or function is a member
  //    of the innermost enclosing namespace.
  //  - The name of the friend is not found by simple name lookup
  //    until a matching declaration is provided in that namespace
  //    scope (either before or after the class declaration granting
  //    friendship).
  //  - If a friend function is called, its name may be found by the
  //    name lookup that considers functions from namespaces and
  //    classes associated with the types of the function arguments.
  //  - When looking for a prior declaration of a class or a function
  //    declared as a friend, scopes outside the innermost enclosing
  //    namespace scope are not considered.

  CXXScopeSpec &SS = D.getCXXScopeSpec();
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  assert(NameInfo.getName());

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(Loc, TInfo, UPPC_FriendDeclaration) ||
      DiagnoseUnexpandedParameterPack(NameInfo, UPPC_FriendDeclaration) ||
      DiagnoseUnexpandedParameterPack(SS, UPPC_FriendDeclaration))
    return nullptr;

  // The context we found the declaration in, or in which we should
  // create the declaration.
  DeclContext *DC;
  Scope *DCScope = S;
  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        RedeclarationKind::ForExternalRedeclaration);

  bool isTemplateId = D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId;

  // There are five cases here.
  //   - There's no scope specifier and we're in a local class. Only look
  //     for functions declared in the immediately-enclosing block scope.
  // We recover from invalid scope qualifiers as if they just weren't there.
  FunctionDecl *FunctionContainingLocalClass = nullptr;
  if ((SS.isInvalid() || !SS.isSet()) &&
      (FunctionContainingLocalClass =
           cast<CXXRecordDecl>(CurContext)->isLocalClass())) {
    // C++11 [class.friend]p11:
    //   If a friend declaration appears in a local class and the name
    //   specified is an unqualified name, a prior declaration is
    //   looked up without considering scopes that are outside the
    //   innermost enclosing non-class scope. For a friend function
    //   declaration, if there is no prior declaration, the program is
    //   ill-formed.

    // Find the innermost enclosing non-class scope. This is the block
    // scope containing the local class definition (or for a nested class,
    // the outer local class).
    DCScope = S->getFnParent();

    // Look up the function name in the scope.
    Previous.clear(LookupLocalFriendName);
    LookupName(Previous, S, /*AllowBuiltinCreation*/false);

    if (!Previous.empty()) {
      // All possible previous declarations must have the same context:
      // either they were declared at block scope or they are members of
      // one of the enclosing local classes.
      DC = Previous.getRepresentativeDecl()->getDeclContext();
    } else {
      // This is ill-formed, but provide the context that we would have
      // declared the function in, if we were permitted to, for error recovery.
      DC = FunctionContainingLocalClass;
    }
    adjustContextForLocalExternDecl(DC);

  //   - There's no scope specifier, in which case we just go to the
  //     appropriate scope and look for a function or function template
  //     there as appropriate.
  } else if (SS.isInvalid() || !SS.isSet()) {
    // C++11 [namespace.memdef]p3:
    //   If the name in a friend declaration is neither qualified nor
    //   a template-id and the declaration is a function or an
    //   elaborated-type-specifier, the lookup to determine whether
    //   the entity has been previously declared shall not consider
    //   any scopes outside the innermost enclosing namespace.

    // Find the appropriate context according to the above.
    DC = CurContext;

    // Skip class contexts.  If someone can cite chapter and verse
    // for this behavior, that would be nice --- it's what GCC and
    // EDG do, and it seems like a reasonable intent, but the spec
    // really only says that checks for unqualified existing
    // declarations should stop at the nearest enclosing namespace,
    // not that they should only consider the nearest enclosing
    // namespace.
    while (DC->isRecord())
      DC = DC->getParent();

    DeclContext *LookupDC = DC->getNonTransparentContext();
    while (true) {
      LookupQualifiedName(Previous, LookupDC);

      if (!Previous.empty()) {
        DC = LookupDC;
        break;
      }

      if (isTemplateId) {
        if (isa<TranslationUnitDecl>(LookupDC)) break;
      } else {
        if (LookupDC->isFileContext()) break;
      }
      LookupDC = LookupDC->getParent();
    }

    DCScope = getScopeForDeclContext(S, DC);

  //   - There's a non-dependent scope specifier, in which case we
  //     compute it and do a previous lookup there for a function
  //     or function template.
  } else if (!SS.getScopeRep()->isDependent()) {
    DC = computeDeclContext(SS);
    if (!DC) return nullptr;

    if (RequireCompleteDeclContext(SS, DC)) return nullptr;

    LookupQualifiedName(Previous, DC);

    // C++ [class.friend]p1: A friend of a class is a function or
    //   class that is not a member of the class . . .
    if (DC->Equals(CurContext))
      Diag(DS.getFriendSpecLoc(),
           getLangOpts().CPlusPlus11 ?
             diag::warn_cxx98_compat_friend_is_member :
             diag::err_friend_is_member);

  //   - There's a scope specifier that does not match any template
  //     parameter lists, in which case we use some arbitrary context,
  //     create a method or method template, and wait for instantiation.
  //   - There's a scope specifier that does match some template
  //     parameter lists, which we don't handle right now.
  } else {
    DC = CurContext;
    assert(isa<CXXRecordDecl>(DC) && "friend declaration not in class?");
  }

  if (!DC->isRecord()) {
    int DiagArg = -1;
    switch (D.getName().getKind()) {
    case UnqualifiedIdKind::IK_ConstructorTemplateId:
    case UnqualifiedIdKind::IK_ConstructorName:
      DiagArg = 0;
      break;
    case UnqualifiedIdKind::IK_DestructorName:
      DiagArg = 1;
      break;
    case UnqualifiedIdKind::IK_ConversionFunctionId:
      DiagArg = 2;
      break;
    case UnqualifiedIdKind::IK_DeductionGuideName:
      DiagArg = 3;
      break;
    case UnqualifiedIdKind::IK_Identifier:
    case UnqualifiedIdKind::IK_ImplicitSelfParam:
    case UnqualifiedIdKind::IK_LiteralOperatorId:
    case UnqualifiedIdKind::IK_OperatorFunctionId:
    case UnqualifiedIdKind::IK_TemplateId:
      break;
    }
    // This implies that it has to be an operator or function.
    if (DiagArg >= 0) {
      Diag(Loc, diag::err_introducing_special_friend) << DiagArg;
      return nullptr;
    }
  }

  // FIXME: This is an egregious hack to cope with cases where the scope stack
  // does not contain the declaration context, i.e., in an out-of-line
  // definition of a class.
  Scope FakeDCScope(S, Scope::DeclScope, Diags);
  if (!DCScope) {
    FakeDCScope.setEntity(DC);
    DCScope = &FakeDCScope;
  }

  bool AddToScope = true;
  NamedDecl *ND = ActOnFunctionDeclarator(DCScope, D, DC, TInfo, Previous,
                                          TemplateParams, AddToScope);
  if (!ND) return nullptr;

  assert(ND->getLexicalDeclContext() == CurContext);

  // If we performed typo correction, we might have added a scope specifier
  // and changed the decl context.
  DC = ND->getDeclContext();

  // Add the function declaration to the appropriate lookup tables,
  // adjusting the redeclarations list as necessary.  We don't
  // want to do this yet if the friending class is dependent.
  //
  // Also update the scope-based lookup if the target context's
  // lookup context is in lexical scope.
  if (!CurContext->isDependentContext()) {
    DC = DC->getRedeclContext();
    DC->makeDeclVisibleInContext(ND);
    if (Scope *EnclosingScope = getScopeForDeclContext(S, DC))
      PushOnScopeChains(ND, EnclosingScope, /*AddToContext=*/ false);
  }

  FriendDecl *FrD = FriendDecl::Create(Context, CurContext,
                                       D.getIdentifierLoc(), ND,
                                       DS.getFriendSpecLoc());
  FrD->setAccess(AS_public);
  CurContext->addDecl(FrD);

  if (ND->isInvalidDecl()) {
    FrD->setInvalidDecl();
  } else {
    if (DC->isRecord()) CheckFriendAccess(ND);

    FunctionDecl *FD;
    if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(ND))
      FD = FTD->getTemplatedDecl();
    else
      FD = cast<FunctionDecl>(ND);

    // C++ [class.friend]p6:
    //   A function may be defined in a friend declaration of a class if and
    //   only if the class is a non-local class, and the function name is
    //   unqualified.
    if (D.isFunctionDefinition()) {
      // Qualified friend function definition.
      if (SS.isNotEmpty()) {
        // FIXME: We should only do this if the scope specifier names the
        // innermost enclosing namespace; otherwise the fixit changes the
        // meaning of the code.
        SemaDiagnosticBuilder DB =
            Diag(SS.getRange().getBegin(), diag::err_qualified_friend_def);

        DB << SS.getScopeRep();
        if (DC->isFileContext())
          DB << FixItHint::CreateRemoval(SS.getRange());

        // Friend function defined in a local class.
      } else if (FunctionContainingLocalClass) {
        Diag(NameInfo.getBeginLoc(), diag::err_friend_def_in_local_class);

        // Per [basic.pre]p4, a template-id is not a name. Therefore, if we have
        // a template-id, the function name is not unqualified because these is
        // no name. While the wording requires some reading in-between the
        // lines, GCC, MSVC, and EDG all consider a friend function
        // specialization definitions // to be de facto explicit specialization
        // and diagnose them as such.
      } else if (isTemplateId) {
        Diag(NameInfo.getBeginLoc(), diag::err_friend_specialization_def);
      }
    }

    // C++11 [dcl.fct.default]p4: If a friend declaration specifies a
    // default argument expression, that declaration shall be a definition
    // and shall be the only declaration of the function or function
    // template in the translation unit.
    if (functionDeclHasDefaultArgument(FD)) {
      // We can't look at FD->getPreviousDecl() because it may not have been set
      // if we're in a dependent context. If the function is known to be a
      // redeclaration, we will have narrowed Previous down to the right decl.
      if (D.isRedeclaration()) {
        Diag(FD->getLocation(), diag::err_friend_decl_with_def_arg_redeclared);
        Diag(Previous.getRepresentativeDecl()->getLocation(),
             diag::note_previous_declaration);
      } else if (!D.isFunctionDefinition())
        Diag(FD->getLocation(), diag::err_friend_decl_with_def_arg_must_be_def);
    }

    // Mark templated-scope function declarations as unsupported.
    if (FD->getNumTemplateParameterLists() && SS.isValid()) {
      Diag(FD->getLocation(), diag::warn_template_qualified_friend_unsupported)
        << SS.getScopeRep() << SS.getRange()
        << cast<CXXRecordDecl>(CurContext);
      FrD->setUnsupportedFriend(true);
    }
  }

  warnOnReservedIdentifier(ND);

  return ND;
}

void Sema::SetDeclDeleted(Decl *Dcl, SourceLocation DelLoc,
                          StringLiteral *Message) {
  AdjustDeclIfTemplate(Dcl);

  FunctionDecl *Fn = dyn_cast_or_null<FunctionDecl>(Dcl);
  if (!Fn) {
    Diag(DelLoc, diag::err_deleted_non_function);
    return;
  }

  // Deleted function does not have a body.
  Fn->setWillHaveBody(false);

  if (const FunctionDecl *Prev = Fn->getPreviousDecl()) {
    // Don't consider the implicit declaration we generate for explicit
    // specializations. FIXME: Do not generate these implicit declarations.
    if ((Prev->getTemplateSpecializationKind() != TSK_ExplicitSpecialization ||
         Prev->getPreviousDecl()) &&
        !Prev->isDefined()) {
      Diag(DelLoc, diag::err_deleted_decl_not_first);
      Diag(Prev->getLocation().isInvalid() ? DelLoc : Prev->getLocation(),
           Prev->isImplicit() ? diag::note_previous_implicit_declaration
                              : diag::note_previous_declaration);
      // We can't recover from this; the declaration might have already
      // been used.
      Fn->setInvalidDecl();
      return;
    }

    // To maintain the invariant that functions are only deleted on their first
    // declaration, mark the implicitly-instantiated declaration of the
    // explicitly-specialized function as deleted instead of marking the
    // instantiated redeclaration.
    Fn = Fn->getCanonicalDecl();
  }

  // dllimport/dllexport cannot be deleted.
  if (const InheritableAttr *DLLAttr = getDLLAttr(Fn)) {
    Diag(Fn->getLocation(), diag::err_attribute_dll_deleted) << DLLAttr;
    Fn->setInvalidDecl();
  }

  // C++11 [basic.start.main]p3:
  //   A program that defines main as deleted [...] is ill-formed.
  if (Fn->isMain())
    Diag(DelLoc, diag::err_deleted_main);

  // C++11 [dcl.fct.def.delete]p4:
  //  A deleted function is implicitly inline.
  Fn->setImplicitlyInline();
  Fn->setDeletedAsWritten(true, Message);
}

void Sema::SetDeclDefaulted(Decl *Dcl, SourceLocation DefaultLoc) {
  if (!Dcl || Dcl->isInvalidDecl())
    return;

  auto *FD = dyn_cast<FunctionDecl>(Dcl);
  if (!FD) {
    if (auto *FTD = dyn_cast<FunctionTemplateDecl>(Dcl)) {
      if (getDefaultedFunctionKind(FTD->getTemplatedDecl()).isComparison()) {
        Diag(DefaultLoc, diag::err_defaulted_comparison_template);
        return;
      }
    }

    Diag(DefaultLoc, diag::err_default_special_members)
        << getLangOpts().CPlusPlus20;
    return;
  }

  // Reject if this can't possibly be a defaultable function.
  DefaultedFunctionKind DefKind = getDefaultedFunctionKind(FD);
  if (!DefKind &&
      // A dependent function that doesn't locally look defaultable can
      // still instantiate to a defaultable function if it's a constructor
      // or assignment operator.
      (!FD->isDependentContext() ||
       (!isa<CXXConstructorDecl>(FD) &&
        FD->getDeclName().getCXXOverloadedOperator() != OO_Equal))) {
    Diag(DefaultLoc, diag::err_default_special_members)
        << getLangOpts().CPlusPlus20;
    return;
  }

  // Issue compatibility warning. We already warned if the operator is
  // 'operator<=>' when parsing the '<=>' token.
  if (DefKind.isComparison() &&
      DefKind.asComparison() != DefaultedComparisonKind::ThreeWay) {
    Diag(DefaultLoc, getLangOpts().CPlusPlus20
                         ? diag::warn_cxx17_compat_defaulted_comparison
                         : diag::ext_defaulted_comparison);
  }

  FD->setDefaulted();
  FD->setExplicitlyDefaulted();
  FD->setDefaultLoc(DefaultLoc);

  // Defer checking functions that are defaulted in a dependent context.
  if (FD->isDependentContext())
    return;

  // Unset that we will have a body for this function. We might not,
  // if it turns out to be trivial, and we don't need this marking now
  // that we've marked it as defaulted.
  FD->setWillHaveBody(false);

  if (DefKind.isComparison()) {
    // If this comparison's defaulting occurs within the definition of its
    // lexical class context, we have to do the checking when complete.
    if (auto const *RD = dyn_cast<CXXRecordDecl>(FD->getLexicalDeclContext()))
      if (!RD->isCompleteDefinition())
        return;
  }

  // If this member fn was defaulted on its first declaration, we will have
  // already performed the checking in CheckCompletedCXXClass. Such a
  // declaration doesn't trigger an implicit definition.
  if (isa<CXXMethodDecl>(FD)) {
    const FunctionDecl *Primary = FD;
    if (const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern())
      // Ask the template instantiation pattern that actually had the
      // '= default' on it.
      Primary = Pattern;
    if (Primary->getCanonicalDecl()->isDefaulted())
      return;
  }

  if (DefKind.isComparison()) {
    if (CheckExplicitlyDefaultedComparison(nullptr, FD, DefKind.asComparison()))
      FD->setInvalidDecl();
    else
      DefineDefaultedComparison(DefaultLoc, FD, DefKind.asComparison());
  } else {
    auto *MD = cast<CXXMethodDecl>(FD);

    if (CheckExplicitlyDefaultedSpecialMember(MD, DefKind.asSpecialMember(),
                                              DefaultLoc))
      MD->setInvalidDecl();
    else
      DefineDefaultedFunction(*this, MD, DefaultLoc);
  }
}

static void SearchForReturnInStmt(Sema &Self, Stmt *S) {
  for (Stmt *SubStmt : S->children()) {
    if (!SubStmt)
      continue;
    if (isa<ReturnStmt>(SubStmt))
      Self.Diag(SubStmt->getBeginLoc(),
                diag::err_return_in_constructor_handler);
    if (!isa<Expr>(SubStmt))
      SearchForReturnInStmt(Self, SubStmt);
  }
}

void Sema::DiagnoseReturnInConstructorExceptionHandler(CXXTryStmt *TryBlock) {
  for (unsigned I = 0, E = TryBlock->getNumHandlers(); I != E; ++I) {
    CXXCatchStmt *Handler = TryBlock->getHandler(I);
    SearchForReturnInStmt(*this, Handler);
  }
}

void Sema::SetFunctionBodyKind(Decl *D, SourceLocation Loc, FnBodyKind BodyKind,
                               StringLiteral *DeletedMessage) {
  switch (BodyKind) {
  case FnBodyKind::Delete:
    SetDeclDeleted(D, Loc, DeletedMessage);
    break;
  case FnBodyKind::Default:
    SetDeclDefaulted(D, Loc);
    break;
  case FnBodyKind::Other:
    llvm_unreachable(
        "Parsed function body should be '= delete;' or '= default;'");
  }
}

bool Sema::CheckOverridingFunctionAttributes(CXXMethodDecl *New,
                                             const CXXMethodDecl *Old) {
  const auto *NewFT = New->getType()->castAs<FunctionProtoType>();
  const auto *OldFT = Old->getType()->castAs<FunctionProtoType>();

  if (OldFT->hasExtParameterInfos()) {
    for (unsigned I = 0, E = OldFT->getNumParams(); I != E; ++I)
      // A parameter of the overriding method should be annotated with noescape
      // if the corresponding parameter of the overridden method is annotated.
      if (OldFT->getExtParameterInfo(I).isNoEscape() &&
          !NewFT->getExtParameterInfo(I).isNoEscape()) {
        Diag(New->getParamDecl(I)->getLocation(),
             diag::warn_overriding_method_missing_noescape);
        Diag(Old->getParamDecl(I)->getLocation(),
             diag::note_overridden_marked_noescape);
      }
  }

  // SME attributes must match when overriding a function declaration.
  if (IsInvalidSMECallConversion(Old->getType(), New->getType())) {
    Diag(New->getLocation(), diag::err_conflicting_overriding_attributes)
        << New << New->getType() << Old->getType();
    Diag(Old->getLocation(), diag::note_overridden_virtual_function);
    return true;
  }

  // Virtual overrides must have the same code_seg.
  const auto *OldCSA = Old->getAttr<CodeSegAttr>();
  const auto *NewCSA = New->getAttr<CodeSegAttr>();
  if ((NewCSA || OldCSA) &&
      (!OldCSA || !NewCSA || NewCSA->getName() != OldCSA->getName())) {
    Diag(New->getLocation(), diag::err_mismatched_code_seg_override);
    Diag(Old->getLocation(), diag::note_previous_declaration);
    return true;
  }

  // Virtual overrides: check for matching effects.
  if (Context.hasAnyFunctionEffects()) {
    const auto OldFX = Old->getFunctionEffects();
    const auto NewFXOrig = New->getFunctionEffects();

    if (OldFX != NewFXOrig) {
      FunctionEffectSet NewFX(NewFXOrig);
      const auto Diffs = FunctionEffectDifferences(OldFX, NewFX);
      FunctionEffectSet::Conflicts Errs;
      for (const auto &Diff : Diffs) {
        switch (Diff.shouldDiagnoseMethodOverride(*Old, OldFX, *New, NewFX)) {
        case FunctionEffectDiff::OverrideResult::NoAction:
          break;
        case FunctionEffectDiff::OverrideResult::Warn:
          Diag(New->getLocation(), diag::warn_mismatched_func_effect_override)
              << Diff.effectName();
          Diag(Old->getLocation(), diag::note_overridden_virtual_function)
              << Old->getReturnTypeSourceRange();
          break;
        case FunctionEffectDiff::OverrideResult::Merge: {
          NewFX.insert(Diff.Old, Errs);
          const auto *NewFT = New->getType()->castAs<FunctionProtoType>();
          FunctionProtoType::ExtProtoInfo EPI = NewFT->getExtProtoInfo();
          EPI.FunctionEffects = FunctionEffectsRef(NewFX);
          QualType ModQT = Context.getFunctionType(NewFT->getReturnType(),
                                                   NewFT->getParamTypes(), EPI);
          New->setType(ModQT);
          break;
        }
        }
      }
      if (!Errs.empty())
        diagnoseFunctionEffectMergeConflicts(Errs, New->getLocation(),
                                             Old->getLocation());
    }
  }

  CallingConv NewCC = NewFT->getCallConv(), OldCC = OldFT->getCallConv();

  // If the calling conventions match, everything is fine
  if (NewCC == OldCC)
    return false;

  // If the calling conventions mismatch because the new function is static,
  // suppress the calling convention mismatch error; the error about static
  // function override (err_static_overrides_virtual from
  // Sema::CheckFunctionDeclaration) is more clear.
  if (New->getStorageClass() == SC_Static)
    return false;

  Diag(New->getLocation(),
       diag::err_conflicting_overriding_cc_attributes)
    << New->getDeclName() << New->getType() << Old->getType();
  Diag(Old->getLocation(), diag::note_overridden_virtual_function);
  return true;
}

bool Sema::CheckExplicitObjectOverride(CXXMethodDecl *New,
                                       const CXXMethodDecl *Old) {
  // CWG2553
  // A virtual function shall not be an explicit object member function.
  if (!New->isExplicitObjectMemberFunction())
    return true;
  Diag(New->getParamDecl(0)->getBeginLoc(),
       diag::err_explicit_object_parameter_nonmember)
      << New->getSourceRange() << /*virtual*/ 1 << /*IsLambda*/ false;
  Diag(Old->getLocation(), diag::note_overridden_virtual_function);
  New->setInvalidDecl();
  return false;
}

bool Sema::CheckOverridingFunctionReturnType(const CXXMethodDecl *New,
                                             const CXXMethodDecl *Old) {
  QualType NewTy = New->getType()->castAs<FunctionType>()->getReturnType();
  QualType OldTy = Old->getType()->castAs<FunctionType>()->getReturnType();

  if (Context.hasSameType(NewTy, OldTy) ||
      NewTy->isDependentType() || OldTy->isDependentType())
    return false;

  // Check if the return types are covariant
  QualType NewClassTy, OldClassTy;

  /// Both types must be pointers or references to classes.
  if (const PointerType *NewPT = NewTy->getAs<PointerType>()) {
    if (const PointerType *OldPT = OldTy->getAs<PointerType>()) {
      NewClassTy = NewPT->getPointeeType();
      OldClassTy = OldPT->getPointeeType();
    }
  } else if (const ReferenceType *NewRT = NewTy->getAs<ReferenceType>()) {
    if (const ReferenceType *OldRT = OldTy->getAs<ReferenceType>()) {
      if (NewRT->getTypeClass() == OldRT->getTypeClass()) {
        NewClassTy = NewRT->getPointeeType();
        OldClassTy = OldRT->getPointeeType();
      }
    }
  }

  // The return types aren't either both pointers or references to a class type.
  if (NewClassTy.isNull()) {
    Diag(New->getLocation(),
         diag::err_different_return_type_for_overriding_virtual_function)
        << New->getDeclName() << NewTy << OldTy
        << New->getReturnTypeSourceRange();
    Diag(Old->getLocation(), diag::note_overridden_virtual_function)
        << Old->getReturnTypeSourceRange();

    return true;
  }

  if (!Context.hasSameUnqualifiedType(NewClassTy, OldClassTy)) {
    // C++14 [class.virtual]p8:
    //   If the class type in the covariant return type of D::f differs from
    //   that of B::f, the class type in the return type of D::f shall be
    //   complete at the point of declaration of D::f or shall be the class
    //   type D.
    if (const RecordType *RT = NewClassTy->getAs<RecordType>()) {
      if (!RT->isBeingDefined() &&
          RequireCompleteType(New->getLocation(), NewClassTy,
                              diag::err_covariant_return_incomplete,
                              New->getDeclName()))
        return true;
    }

    // Check if the new class derives from the old class.
    if (!IsDerivedFrom(New->getLocation(), NewClassTy, OldClassTy)) {
      Diag(New->getLocation(), diag::err_covariant_return_not_derived)
          << New->getDeclName() << NewTy << OldTy
          << New->getReturnTypeSourceRange();
      Diag(Old->getLocation(), diag::note_overridden_virtual_function)
          << Old->getReturnTypeSourceRange();
      return true;
    }

    // Check if we the conversion from derived to base is valid.
    if (CheckDerivedToBaseConversion(
            NewClassTy, OldClassTy,
            diag::err_covariant_return_inaccessible_base,
            diag::err_covariant_return_ambiguous_derived_to_base_conv,
            New->getLocation(), New->getReturnTypeSourceRange(),
            New->getDeclName(), nullptr)) {
      // FIXME: this note won't trigger for delayed access control
      // diagnostics, and it's impossible to get an undelayed error
      // here from access control during the original parse because
      // the ParsingDeclSpec/ParsingDeclarator are still in scope.
      Diag(Old->getLocation(), diag::note_overridden_virtual_function)
          << Old->getReturnTypeSourceRange();
      return true;
    }
  }

  // The qualifiers of the return types must be the same.
  if (NewTy.getLocalCVRQualifiers() != OldTy.getLocalCVRQualifiers()) {
    Diag(New->getLocation(),
         diag::err_covariant_return_type_different_qualifications)
        << New->getDeclName() << NewTy << OldTy
        << New->getReturnTypeSourceRange();
    Diag(Old->getLocation(), diag::note_overridden_virtual_function)
        << Old->getReturnTypeSourceRange();
    return true;
  }


  // The new class type must have the same or less qualifiers as the old type.
  if (NewClassTy.isMoreQualifiedThan(OldClassTy)) {
    Diag(New->getLocation(),
         diag::err_covariant_return_type_class_type_more_qualified)
        << New->getDeclName() << NewTy << OldTy
        << New->getReturnTypeSourceRange();
    Diag(Old->getLocation(), diag::note_overridden_virtual_function)
        << Old->getReturnTypeSourceRange();
    return true;
  }

  return false;
}

bool Sema::CheckPureMethod(CXXMethodDecl *Method, SourceRange InitRange) {
  SourceLocation EndLoc = InitRange.getEnd();
  if (EndLoc.isValid())
    Method->setRangeEnd(EndLoc);

  if (Method->isVirtual() || Method->getParent()->isDependentContext()) {
    Method->setIsPureVirtual();
    return false;
  }

  if (!Method->isInvalidDecl())
    Diag(Method->getLocation(), diag::err_non_virtual_pure)
      << Method->getDeclName() << InitRange;
  return true;
}

void Sema::ActOnPureSpecifier(Decl *D, SourceLocation ZeroLoc) {
  if (D->getFriendObjectKind())
    Diag(D->getLocation(), diag::err_pure_friend);
  else if (auto *M = dyn_cast<CXXMethodDecl>(D))
    CheckPureMethod(M, ZeroLoc);
  else
    Diag(D->getLocation(), diag::err_illegal_initializer);
}

/// Invoked when we are about to parse an initializer for the declaration
/// 'Dcl'.
///
/// After this method is called, according to [C++ 3.4.1p13], if 'Dcl' is a
/// static data member of class X, names should be looked up in the scope of
/// class X. If the declaration had a scope specifier, a scope will have
/// been created and passed in for this purpose. Otherwise, S will be null.
void Sema::ActOnCXXEnterDeclInitializer(Scope *S, Decl *D) {
  assert(D && !D->isInvalidDecl());

  // We will always have a nested name specifier here, but this declaration
  // might not be out of line if the specifier names the current namespace:
  //   extern int n;
  //   int ::n = 0;
  if (S && D->isOutOfLine())
    EnterDeclaratorContext(S, D->getDeclContext());

  PushExpressionEvaluationContext(
      ExpressionEvaluationContext::PotentiallyEvaluated, D);
}

void Sema::ActOnCXXExitDeclInitializer(Scope *S, Decl *D) {
  assert(D);

  if (S && D->isOutOfLine())
    ExitDeclaratorContext(S);

  if (getLangOpts().CPlusPlus23) {
    // An expression or conversion is 'manifestly constant-evaluated' if it is:
    // [...]
    // - the initializer of a variable that is usable in constant expressions or
    //   has constant initialization.
    if (auto *VD = dyn_cast<VarDecl>(D);
        VD && (VD->isUsableInConstantExpressions(Context) ||
               VD->hasConstantInitialization())) {
      // An expression or conversion is in an 'immediate function context' if it
      // is potentially evaluated and either:
      // [...]
      // - it is a subexpression of a manifestly constant-evaluated expression
      //   or conversion.
      ExprEvalContexts.back().InImmediateFunctionContext = true;
    }
  }

  // Unless the initializer is in an immediate function context (as determined
  // above), this will evaluate all contained immediate function calls as
  // constant expressions. If the initializer IS an immediate function context,
  // the initializer has been determined to be a constant expression, and all
  // such evaluations will be elided (i.e., as if we "knew the whole time" that
  // it was a constant expression).
  PopExpressionEvaluationContext();
}

DeclResult Sema::ActOnCXXConditionDeclaration(Scope *S, Declarator &D) {
  // C++ 6.4p2:
  // The declarator shall not specify a function or an array.
  // The type-specifier-seq shall not contain typedef and shall not declare a
  // new class or enumeration.
  assert(D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class of condition decl.");

  Decl *Dcl = ActOnDeclarator(S, D);
  if (!Dcl)
    return true;

  if (isa<FunctionDecl>(Dcl)) { // The declarator shall not specify a function.
    Diag(Dcl->getLocation(), diag::err_invalid_use_of_function_type)
      << D.getSourceRange();
    return true;
  }

  if (auto *VD = dyn_cast<VarDecl>(Dcl))
    VD->setCXXCondDecl();

  return Dcl;
}

void Sema::LoadExternalVTableUses() {
  if (!ExternalSource)
    return;

  SmallVector<ExternalVTableUse, 4> VTables;
  ExternalSource->ReadUsedVTables(VTables);
  SmallVector<VTableUse, 4> NewUses;
  for (unsigned I = 0, N = VTables.size(); I != N; ++I) {
    llvm::DenseMap<CXXRecordDecl *, bool>::iterator Pos
      = VTablesUsed.find(VTables[I].Record);
    // Even if a definition wasn't required before, it may be required now.
    if (Pos != VTablesUsed.end()) {
      if (!Pos->second && VTables[I].DefinitionRequired)
        Pos->second = true;
      continue;
    }

    VTablesUsed[VTables[I].Record] = VTables[I].DefinitionRequired;
    NewUses.push_back(VTableUse(VTables[I].Record, VTables[I].Location));
  }

  VTableUses.insert(VTableUses.begin(), NewUses.begin(), NewUses.end());
}

void Sema::MarkVTableUsed(SourceLocation Loc, CXXRecordDecl *Class,
                          bool DefinitionRequired) {
  // Ignore any vtable uses in unevaluated operands or for classes that do
  // not have a vtable.
  if (!Class->isDynamicClass() || Class->isDependentContext() ||
      CurContext->isDependentContext() || isUnevaluatedContext())
    return;
  // Do not mark as used if compiling for the device outside of the target
  // region.
  if (TUKind != TU_Prefix && LangOpts.OpenMP && LangOpts.OpenMPIsTargetDevice &&
      !OpenMP().isInOpenMPDeclareTargetContext() &&
      !OpenMP().isInOpenMPTargetExecutionDirective()) {
    if (!DefinitionRequired)
      MarkVirtualMembersReferenced(Loc, Class);
    return;
  }

  // Try to insert this class into the map.
  LoadExternalVTableUses();
  Class = Class->getCanonicalDecl();
  std::pair<llvm::DenseMap<CXXRecordDecl *, bool>::iterator, bool>
    Pos = VTablesUsed.insert(std::make_pair(Class, DefinitionRequired));
  if (!Pos.second) {
    // If we already had an entry, check to see if we are promoting this vtable
    // to require a definition. If so, we need to reappend to the VTableUses
    // list, since we may have already processed the first entry.
    if (DefinitionRequired && !Pos.first->second) {
      Pos.first->second = true;
    } else {
      // Otherwise, we can early exit.
      return;
    }
  } else {
    // The Microsoft ABI requires that we perform the destructor body
    // checks (i.e. operator delete() lookup) when the vtable is marked used, as
    // the deleting destructor is emitted with the vtable, not with the
    // destructor definition as in the Itanium ABI.
    if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
      CXXDestructorDecl *DD = Class->getDestructor();
      if (DD && DD->isVirtual() && !DD->isDeleted()) {
        if (Class->hasUserDeclaredDestructor() && !DD->isDefined()) {
          // If this is an out-of-line declaration, marking it referenced will
          // not do anything. Manually call CheckDestructor to look up operator
          // delete().
          ContextRAII SavedContext(*this, DD);
          CheckDestructor(DD);
        } else {
          MarkFunctionReferenced(Loc, Class->getDestructor());
        }
      }
    }
  }

  // Local classes need to have their virtual members marked
  // immediately. For all other classes, we mark their virtual members
  // at the end of the translation unit.
  if (Class->isLocalClass())
    MarkVirtualMembersReferenced(Loc, Class->getDefinition());
  else
    VTableUses.push_back(std::make_pair(Class, Loc));
}

bool Sema::DefineUsedVTables() {
  LoadExternalVTableUses();
  if (VTableUses.empty())
    return false;

  // Note: The VTableUses vector could grow as a result of marking
  // the members of a class as "used", so we check the size each
  // time through the loop and prefer indices (which are stable) to
  // iterators (which are not).
  bool DefinedAnything = false;
  for (unsigned I = 0; I != VTableUses.size(); ++I) {
    CXXRecordDecl *Class = VTableUses[I].first->getDefinition();
    if (!Class)
      continue;
    TemplateSpecializationKind ClassTSK =
        Class->getTemplateSpecializationKind();

    SourceLocation Loc = VTableUses[I].second;

    bool DefineVTable = true;

    const CXXMethodDecl *KeyFunction = Context.getCurrentKeyFunction(Class);
    // V-tables for non-template classes with an owning module are always
    // uniquely emitted in that module.
    if (Class->isInCurrentModuleUnit()) {
      DefineVTable = true;
    } else if (KeyFunction && !KeyFunction->hasBody()) {
      // If this class has a key function, but that key function is
      // defined in another translation unit, we don't need to emit the
      // vtable even though we're using it.
      // The key function is in another translation unit.
      DefineVTable = false;
      TemplateSpecializationKind TSK =
          KeyFunction->getTemplateSpecializationKind();
      assert(TSK != TSK_ExplicitInstantiationDefinition &&
             TSK != TSK_ImplicitInstantiation &&
             "Instantiations don't have key functions");
      (void)TSK;
    } else if (!KeyFunction) {
      // If we have a class with no key function that is the subject
      // of an explicit instantiation declaration, suppress the
      // vtable; it will live with the explicit instantiation
      // definition.
      bool IsExplicitInstantiationDeclaration =
          ClassTSK == TSK_ExplicitInstantiationDeclaration;
      for (auto *R : Class->redecls()) {
        TemplateSpecializationKind TSK
          = cast<CXXRecordDecl>(R)->getTemplateSpecializationKind();
        if (TSK == TSK_ExplicitInstantiationDeclaration)
          IsExplicitInstantiationDeclaration = true;
        else if (TSK == TSK_ExplicitInstantiationDefinition) {
          IsExplicitInstantiationDeclaration = false;
          break;
        }
      }

      if (IsExplicitInstantiationDeclaration)
        DefineVTable = false;
    }

    // The exception specifications for all virtual members may be needed even
    // if we are not providing an authoritative form of the vtable in this TU.
    // We may choose to emit it available_externally anyway.
    if (!DefineVTable) {
      MarkVirtualMemberExceptionSpecsNeeded(Loc, Class);
      continue;
    }

    // Mark all of the virtual members of this class as referenced, so
    // that we can build a vtable. Then, tell the AST consumer that a
    // vtable for this class is required.
    DefinedAnything = true;
    MarkVirtualMembersReferenced(Loc, Class);
    CXXRecordDecl *Canonical = Class->getCanonicalDecl();
    if (VTablesUsed[Canonical] && !Class->shouldEmitInExternalSource())
      Consumer.HandleVTable(Class);

    // Warn if we're emitting a weak vtable. The vtable will be weak if there is
    // no key function or the key function is inlined. Don't warn in C++ ABIs
    // that lack key functions, since the user won't be able to make one.
    if (Context.getTargetInfo().getCXXABI().hasKeyFunctions() &&
        Class->isExternallyVisible() && ClassTSK != TSK_ImplicitInstantiation &&
        ClassTSK != TSK_ExplicitInstantiationDefinition) {
      const FunctionDecl *KeyFunctionDef = nullptr;
      if (!KeyFunction || (KeyFunction->hasBody(KeyFunctionDef) &&
                           KeyFunctionDef->isInlined()))
        Diag(Class->getLocation(), diag::warn_weak_vtable) << Class;
    }
  }
  VTableUses.clear();

  return DefinedAnything;
}

void Sema::MarkVirtualMemberExceptionSpecsNeeded(SourceLocation Loc,
                                                 const CXXRecordDecl *RD) {
  for (const auto *I : RD->methods())
    if (I->isVirtual() && !I->isPureVirtual())
      ResolveExceptionSpec(Loc, I->getType()->castAs<FunctionProtoType>());
}

void Sema::MarkVirtualMembersReferenced(SourceLocation Loc,
                                        const CXXRecordDecl *RD,
                                        bool ConstexprOnly) {
  // Mark all functions which will appear in RD's vtable as used.
  CXXFinalOverriderMap FinalOverriders;
  RD->getFinalOverriders(FinalOverriders);
  for (CXXFinalOverriderMap::const_iterator I = FinalOverriders.begin(),
                                            E = FinalOverriders.end();
       I != E; ++I) {
    for (OverridingMethods::const_iterator OI = I->second.begin(),
                                           OE = I->second.end();
         OI != OE; ++OI) {
      assert(OI->second.size() > 0 && "no final overrider");
      CXXMethodDecl *Overrider = OI->second.front().Method;

      // C++ [basic.def.odr]p2:
      //   [...] A virtual member function is used if it is not pure. [...]
      if (!Overrider->isPureVirtual() &&
          (!ConstexprOnly || Overrider->isConstexpr()))
        MarkFunctionReferenced(Loc, Overrider);
    }
  }

  // Only classes that have virtual bases need a VTT.
  if (RD->getNumVBases() == 0)
    return;

  for (const auto &I : RD->bases()) {
    const auto *Base =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());
    if (Base->getNumVBases() == 0)
      continue;
    MarkVirtualMembersReferenced(Loc, Base);
  }
}

static
void DelegatingCycleHelper(CXXConstructorDecl* Ctor,
                           llvm::SmallPtrSet<CXXConstructorDecl*, 4> &Valid,
                           llvm::SmallPtrSet<CXXConstructorDecl*, 4> &Invalid,
                           llvm::SmallPtrSet<CXXConstructorDecl*, 4> &Current,
                           Sema &S) {
  if (Ctor->isInvalidDecl())
    return;

  CXXConstructorDecl *Target = Ctor->getTargetConstructor();

  // Target may not be determinable yet, for instance if this is a dependent
  // call in an uninstantiated template.
  if (Target) {
    const FunctionDecl *FNTarget = nullptr;
    (void)Target->hasBody(FNTarget);
    Target = const_cast<CXXConstructorDecl*>(
      cast_or_null<CXXConstructorDecl>(FNTarget));
  }

  CXXConstructorDecl *Canonical = Ctor->getCanonicalDecl(),
                     // Avoid dereferencing a null pointer here.
                     *TCanonical = Target? Target->getCanonicalDecl() : nullptr;

  if (!Current.insert(Canonical).second)
    return;

  // We know that beyond here, we aren't chaining into a cycle.
  if (!Target || !Target->isDelegatingConstructor() ||
      Target->isInvalidDecl() || Valid.count(TCanonical)) {
    Valid.insert(Current.begin(), Current.end());
    Current.clear();
  // We've hit a cycle.
  } else if (TCanonical == Canonical || Invalid.count(TCanonical) ||
             Current.count(TCanonical)) {
    // If we haven't diagnosed this cycle yet, do so now.
    if (!Invalid.count(TCanonical)) {
      S.Diag((*Ctor->init_begin())->getSourceLocation(),
             diag::warn_delegating_ctor_cycle)
        << Ctor;

      // Don't add a note for a function delegating directly to itself.
      if (TCanonical != Canonical)
        S.Diag(Target->getLocation(), diag::note_it_delegates_to);

      CXXConstructorDecl *C = Target;
      while (C->getCanonicalDecl() != Canonical) {
        const FunctionDecl *FNTarget = nullptr;
        (void)C->getTargetConstructor()->hasBody(FNTarget);
        assert(FNTarget && "Ctor cycle through bodiless function");

        C = const_cast<CXXConstructorDecl*>(
          cast<CXXConstructorDecl>(FNTarget));
        S.Diag(C->getLocation(), diag::note_which_delegates_to);
      }
    }

    Invalid.insert(Current.begin(), Current.end());
    Current.clear();
  } else {
    DelegatingCycleHelper(Target, Valid, Invalid, Current, S);
  }
}


void Sema::CheckDelegatingCtorCycles() {
  llvm::SmallPtrSet<CXXConstructorDecl*, 4> Valid, Invalid, Current;

  for (DelegatingCtorDeclsType::iterator
           I = DelegatingCtorDecls.begin(ExternalSource.get()),
           E = DelegatingCtorDecls.end();
       I != E; ++I)
    DelegatingCycleHelper(*I, Valid, Invalid, Current, *this);

  for (auto CI = Invalid.begin(), CE = Invalid.end(); CI != CE; ++CI)
    (*CI)->setInvalidDecl();
}

namespace {
  /// AST visitor that finds references to the 'this' expression.
  class FindCXXThisExpr : public RecursiveASTVisitor<FindCXXThisExpr> {
    Sema &S;

  public:
    explicit FindCXXThisExpr(Sema &S) : S(S) { }

    bool VisitCXXThisExpr(CXXThisExpr *E) {
      S.Diag(E->getLocation(), diag::err_this_static_member_func)
        << E->isImplicit();
      return false;
    }
  };
}

bool Sema::checkThisInStaticMemberFunctionType(CXXMethodDecl *Method) {
  TypeSourceInfo *TSInfo = Method->getTypeSourceInfo();
  if (!TSInfo)
    return false;

  TypeLoc TL = TSInfo->getTypeLoc();
  FunctionProtoTypeLoc ProtoTL = TL.getAs<FunctionProtoTypeLoc>();
  if (!ProtoTL)
    return false;

  // C++11 [expr.prim.general]p3:
  //   [The expression this] shall not appear before the optional
  //   cv-qualifier-seq and it shall not appear within the declaration of a
  //   static member function (although its type and value category are defined
  //   within a static member function as they are within a non-static member
  //   function). [ Note: this is because declaration matching does not occur
  //  until the complete declarator is known. - end note ]
  const FunctionProtoType *Proto = ProtoTL.getTypePtr();
  FindCXXThisExpr Finder(*this);

  // If the return type came after the cv-qualifier-seq, check it now.
  if (Proto->hasTrailingReturn() &&
      !Finder.TraverseTypeLoc(ProtoTL.getReturnLoc()))
    return true;

  // Check the exception specification.
  if (checkThisInStaticMemberFunctionExceptionSpec(Method))
    return true;

  // Check the trailing requires clause
  if (Expr *E = Method->getTrailingRequiresClause())
    if (!Finder.TraverseStmt(E))
      return true;

  return checkThisInStaticMemberFunctionAttributes(Method);
}

bool Sema::checkThisInStaticMemberFunctionExceptionSpec(CXXMethodDecl *Method) {
  TypeSourceInfo *TSInfo = Method->getTypeSourceInfo();
  if (!TSInfo)
    return false;

  TypeLoc TL = TSInfo->getTypeLoc();
  FunctionProtoTypeLoc ProtoTL = TL.getAs<FunctionProtoTypeLoc>();
  if (!ProtoTL)
    return false;

  const FunctionProtoType *Proto = ProtoTL.getTypePtr();
  FindCXXThisExpr Finder(*this);

  switch (Proto->getExceptionSpecType()) {
  case EST_Unparsed:
  case EST_Uninstantiated:
  case EST_Unevaluated:
  case EST_BasicNoexcept:
  case EST_NoThrow:
  case EST_DynamicNone:
  case EST_MSAny:
  case EST_None:
    break;

  case EST_DependentNoexcept:
  case EST_NoexceptFalse:
  case EST_NoexceptTrue:
    if (!Finder.TraverseStmt(Proto->getNoexceptExpr()))
      return true;
    [[fallthrough]];

  case EST_Dynamic:
    for (const auto &E : Proto->exceptions()) {
      if (!Finder.TraverseType(E))
        return true;
    }
    break;
  }

  return false;
}

bool Sema::checkThisInStaticMemberFunctionAttributes(CXXMethodDecl *Method) {
  FindCXXThisExpr Finder(*this);

  // Check attributes.
  for (const auto *A : Method->attrs()) {
    // FIXME: This should be emitted by tblgen.
    Expr *Arg = nullptr;
    ArrayRef<Expr *> Args;
    if (const auto *G = dyn_cast<GuardedByAttr>(A))
      Arg = G->getArg();
    else if (const auto *G = dyn_cast<PtGuardedByAttr>(A))
      Arg = G->getArg();
    else if (const auto *AA = dyn_cast<AcquiredAfterAttr>(A))
      Args = llvm::ArrayRef(AA->args_begin(), AA->args_size());
    else if (const auto *AB = dyn_cast<AcquiredBeforeAttr>(A))
      Args = llvm::ArrayRef(AB->args_begin(), AB->args_size());
    else if (const auto *ETLF = dyn_cast<ExclusiveTrylockFunctionAttr>(A)) {
      Arg = ETLF->getSuccessValue();
      Args = llvm::ArrayRef(ETLF->args_begin(), ETLF->args_size());
    } else if (const auto *STLF = dyn_cast<SharedTrylockFunctionAttr>(A)) {
      Arg = STLF->getSuccessValue();
      Args = llvm::ArrayRef(STLF->args_begin(), STLF->args_size());
    } else if (const auto *LR = dyn_cast<LockReturnedAttr>(A))
      Arg = LR->getArg();
    else if (const auto *LE = dyn_cast<LocksExcludedAttr>(A))
      Args = llvm::ArrayRef(LE->args_begin(), LE->args_size());
    else if (const auto *RC = dyn_cast<RequiresCapabilityAttr>(A))
      Args = llvm::ArrayRef(RC->args_begin(), RC->args_size());
    else if (const auto *AC = dyn_cast<AcquireCapabilityAttr>(A))
      Args = llvm::ArrayRef(AC->args_begin(), AC->args_size());
    else if (const auto *AC = dyn_cast<TryAcquireCapabilityAttr>(A))
      Args = llvm::ArrayRef(AC->args_begin(), AC->args_size());
    else if (const auto *RC = dyn_cast<ReleaseCapabilityAttr>(A))
      Args = llvm::ArrayRef(RC->args_begin(), RC->args_size());

    if (Arg && !Finder.TraverseStmt(Arg))
      return true;

    for (unsigned I = 0, N = Args.size(); I != N; ++I) {
      if (!Finder.TraverseStmt(Args[I]))
        return true;
    }
  }

  return false;
}

void Sema::checkExceptionSpecification(
    bool IsTopLevel, ExceptionSpecificationType EST,
    ArrayRef<ParsedType> DynamicExceptions,
    ArrayRef<SourceRange> DynamicExceptionRanges, Expr *NoexceptExpr,
    SmallVectorImpl<QualType> &Exceptions,
    FunctionProtoType::ExceptionSpecInfo &ESI) {
  Exceptions.clear();
  ESI.Type = EST;
  if (EST == EST_Dynamic) {
    Exceptions.reserve(DynamicExceptions.size());
    for (unsigned ei = 0, ee = DynamicExceptions.size(); ei != ee; ++ei) {
      // FIXME: Preserve type source info.
      QualType ET = GetTypeFromParser(DynamicExceptions[ei]);

      if (IsTopLevel) {
        SmallVector<UnexpandedParameterPack, 2> Unexpanded;
        collectUnexpandedParameterPacks(ET, Unexpanded);
        if (!Unexpanded.empty()) {
          DiagnoseUnexpandedParameterPacks(
              DynamicExceptionRanges[ei].getBegin(), UPPC_ExceptionType,
              Unexpanded);
          continue;
        }
      }

      // Check that the type is valid for an exception spec, and
      // drop it if not.
      if (!CheckSpecifiedExceptionType(ET, DynamicExceptionRanges[ei]))
        Exceptions.push_back(ET);
    }
    ESI.Exceptions = Exceptions;
    return;
  }

  if (isComputedNoexcept(EST)) {
    assert((NoexceptExpr->isTypeDependent() ||
            NoexceptExpr->getType()->getCanonicalTypeUnqualified() ==
            Context.BoolTy) &&
           "Parser should have made sure that the expression is boolean");
    if (IsTopLevel && DiagnoseUnexpandedParameterPack(NoexceptExpr)) {
      ESI.Type = EST_BasicNoexcept;
      return;
    }

    ESI.NoexceptExpr = NoexceptExpr;
    return;
  }
}

void Sema::actOnDelayedExceptionSpecification(
    Decl *D, ExceptionSpecificationType EST, SourceRange SpecificationRange,
    ArrayRef<ParsedType> DynamicExceptions,
    ArrayRef<SourceRange> DynamicExceptionRanges, Expr *NoexceptExpr) {
  if (!D)
    return;

  // Dig out the function we're referring to.
  if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(D))
    D = FTD->getTemplatedDecl();

  FunctionDecl *FD = dyn_cast<FunctionDecl>(D);
  if (!FD)
    return;

  // Check the exception specification.
  llvm::SmallVector<QualType, 4> Exceptions;
  FunctionProtoType::ExceptionSpecInfo ESI;
  checkExceptionSpecification(/*IsTopLevel=*/true, EST, DynamicExceptions,
                              DynamicExceptionRanges, NoexceptExpr, Exceptions,
                              ESI);

  // Update the exception specification on the function type.
  Context.adjustExceptionSpec(FD, ESI, /*AsWritten=*/true);

  if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (MD->isStatic())
      checkThisInStaticMemberFunctionExceptionSpec(MD);

    if (MD->isVirtual()) {
      // Check overrides, which we previously had to delay.
      for (const CXXMethodDecl *O : MD->overridden_methods())
        CheckOverridingFunctionExceptionSpec(MD, O);
    }
  }
}

/// HandleMSProperty - Analyze a __delcspec(property) field of a C++ class.
///
MSPropertyDecl *Sema::HandleMSProperty(Scope *S, RecordDecl *Record,
                                       SourceLocation DeclStart, Declarator &D,
                                       Expr *BitWidth,
                                       InClassInitStyle InitStyle,
                                       AccessSpecifier AS,
                                       const ParsedAttr &MSPropertyAttr) {
  const IdentifierInfo *II = D.getIdentifier();
  if (!II) {
    Diag(DeclStart, diag::err_anonymous_property);
    return nullptr;
  }
  SourceLocation Loc = D.getIdentifierLoc();

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  QualType T = TInfo->getType();
  if (getLangOpts().CPlusPlus) {
    CheckExtraCXXDefaultArguments(D);

    if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                        UPPC_DataMemberType)) {
      D.setInvalidType();
      T = Context.IntTy;
      TInfo = Context.getTrivialTypeSourceInfo(T, Loc);
    }
  }

  DiagnoseFunctionSpecifiers(D.getDeclSpec());

  if (D.getDeclSpec().isInlineSpecified())
    Diag(D.getDeclSpec().getInlineSpecLoc(), diag::err_inline_non_function)
        << getLangOpts().CPlusPlus17;
  if (DeclSpec::TSCS TSCS = D.getDeclSpec().getThreadStorageClassSpec())
    Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
         diag::err_invalid_thread)
      << DeclSpec::getSpecifierName(TSCS);

  // Check to see if this name was declared as a member previously
  NamedDecl *PrevDecl = nullptr;
  LookupResult Previous(*this, II, Loc, LookupMemberName,
                        RedeclarationKind::ForVisibleRedeclaration);
  LookupName(Previous, S);
  switch (Previous.getResultKind()) {
  case LookupResult::Found:
  case LookupResult::FoundUnresolvedValue:
    PrevDecl = Previous.getAsSingle<NamedDecl>();
    break;

  case LookupResult::FoundOverloaded:
    PrevDecl = Previous.getRepresentativeDecl();
    break;

  case LookupResult::NotFound:
  case LookupResult::NotFoundInCurrentInstantiation:
  case LookupResult::Ambiguous:
    break;
  }

  if (PrevDecl && PrevDecl->isTemplateParameter()) {
    // Maybe we will complain about the shadowed template parameter.
    DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), PrevDecl);
    // Just pretend that we didn't see the previous declaration.
    PrevDecl = nullptr;
  }

  if (PrevDecl && !isDeclInScope(PrevDecl, Record, S))
    PrevDecl = nullptr;

  SourceLocation TSSL = D.getBeginLoc();
  MSPropertyDecl *NewPD =
      MSPropertyDecl::Create(Context, Record, Loc, II, T, TInfo, TSSL,
                             MSPropertyAttr.getPropertyDataGetter(),
                             MSPropertyAttr.getPropertyDataSetter());
  ProcessDeclAttributes(TUScope, NewPD, D);
  NewPD->setAccess(AS);

  if (NewPD->isInvalidDecl())
    Record->setInvalidDecl();

  if (D.getDeclSpec().isModulePrivateSpecified())
    NewPD->setModulePrivate();

  if (NewPD->isInvalidDecl() && PrevDecl) {
    // Don't introduce NewFD into scope; there's already something
    // with the same name in the same scope.
  } else if (II) {
    PushOnScopeChains(NewPD, S);
  } else
    Record->addDecl(NewPD);

  return NewPD;
}

void Sema::ActOnStartFunctionDeclarationDeclarator(
    Declarator &Declarator, unsigned TemplateParameterDepth) {
  auto &Info = InventedParameterInfos.emplace_back();
  TemplateParameterList *ExplicitParams = nullptr;
  ArrayRef<TemplateParameterList *> ExplicitLists =
      Declarator.getTemplateParameterLists();
  if (!ExplicitLists.empty()) {
    bool IsMemberSpecialization, IsInvalid;
    ExplicitParams = MatchTemplateParametersToScopeSpecifier(
        Declarator.getBeginLoc(), Declarator.getIdentifierLoc(),
        Declarator.getCXXScopeSpec(), /*TemplateId=*/nullptr,
        ExplicitLists, /*IsFriend=*/false, IsMemberSpecialization, IsInvalid,
        /*SuppressDiagnostic=*/true);
  }
  // C++23 [dcl.fct]p23:
  //   An abbreviated function template can have a template-head. The invented
  //   template-parameters are appended to the template-parameter-list after
  //   the explicitly declared template-parameters.
  //
  // A template-head must have one or more template-parameters (read:
  // 'template<>' is *not* a template-head). Only append the invented
  // template parameters if we matched the nested-name-specifier to a non-empty
  // TemplateParameterList.
  if (ExplicitParams && !ExplicitParams->empty()) {
    Info.AutoTemplateParameterDepth = ExplicitParams->getDepth();
    llvm::append_range(Info.TemplateParams, *ExplicitParams);
    Info.NumExplicitTemplateParams = ExplicitParams->size();
  } else {
    Info.AutoTemplateParameterDepth = TemplateParameterDepth;
    Info.NumExplicitTemplateParams = 0;
  }
}

void Sema::ActOnFinishFunctionDeclarationDeclarator(Declarator &Declarator) {
  auto &FSI = InventedParameterInfos.back();
  if (FSI.TemplateParams.size() > FSI.NumExplicitTemplateParams) {
    if (FSI.NumExplicitTemplateParams != 0) {
      TemplateParameterList *ExplicitParams =
          Declarator.getTemplateParameterLists().back();
      Declarator.setInventedTemplateParameterList(
          TemplateParameterList::Create(
              Context, ExplicitParams->getTemplateLoc(),
              ExplicitParams->getLAngleLoc(), FSI.TemplateParams,
              ExplicitParams->getRAngleLoc(),
              ExplicitParams->getRequiresClause()));
    } else {
      Declarator.setInventedTemplateParameterList(
          TemplateParameterList::Create(
              Context, SourceLocation(), SourceLocation(), FSI.TemplateParams,
              SourceLocation(), /*RequiresClause=*/nullptr));
    }
  }
  InventedParameterInfos.pop_back();
}
