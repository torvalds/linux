//===------- SemaTemplateInstantiate.cpp - C++ Template Instantiation ------===/
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===/
//
//  This file implements C++ template instantiation.
//
//===----------------------------------------------------------------------===/

#include "TreeTransform.h"
#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeVisitor.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Stack.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConcept.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateDeduction.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include <optional>

using namespace clang;
using namespace sema;

//===----------------------------------------------------------------------===/
// Template Instantiation Support
//===----------------------------------------------------------------------===/

namespace {
namespace TemplateInstArgsHelpers {
struct Response {
  const Decl *NextDecl = nullptr;
  bool IsDone = false;
  bool ClearRelativeToPrimary = true;
  static Response Done() {
    Response R;
    R.IsDone = true;
    return R;
  }
  static Response ChangeDecl(const Decl *ND) {
    Response R;
    R.NextDecl = ND;
    return R;
  }
  static Response ChangeDecl(const DeclContext *Ctx) {
    Response R;
    R.NextDecl = Decl::castFromDeclContext(Ctx);
    return R;
  }

  static Response UseNextDecl(const Decl *CurDecl) {
    return ChangeDecl(CurDecl->getDeclContext());
  }

  static Response DontClearRelativeToPrimaryNextDecl(const Decl *CurDecl) {
    Response R = Response::UseNextDecl(CurDecl);
    R.ClearRelativeToPrimary = false;
    return R;
  }
};

// Retrieve the primary template for a lambda call operator. It's
// unfortunate that we only have the mappings of call operators rather
// than lambda classes.
const FunctionDecl *
getPrimaryTemplateOfGenericLambda(const FunctionDecl *LambdaCallOperator) {
  if (!isLambdaCallOperator(LambdaCallOperator))
    return LambdaCallOperator;
  while (true) {
    if (auto *FTD = dyn_cast_if_present<FunctionTemplateDecl>(
            LambdaCallOperator->getDescribedTemplate());
        FTD && FTD->getInstantiatedFromMemberTemplate()) {
      LambdaCallOperator =
          FTD->getInstantiatedFromMemberTemplate()->getTemplatedDecl();
    } else if (LambdaCallOperator->getPrimaryTemplate()) {
      // Cases where the lambda operator is instantiated in
      // TemplateDeclInstantiator::VisitCXXMethodDecl.
      LambdaCallOperator =
          LambdaCallOperator->getPrimaryTemplate()->getTemplatedDecl();
    } else if (auto *Prev = cast<CXXMethodDecl>(LambdaCallOperator)
                                ->getInstantiatedFromMemberFunction())
      LambdaCallOperator = Prev;
    else
      break;
  }
  return LambdaCallOperator;
}

struct EnclosingTypeAliasTemplateDetails {
  TypeAliasTemplateDecl *Template = nullptr;
  TypeAliasTemplateDecl *PrimaryTypeAliasDecl = nullptr;
  ArrayRef<TemplateArgument> AssociatedTemplateArguments;

  explicit operator bool() noexcept { return Template; }
};

// Find the enclosing type alias template Decl from CodeSynthesisContexts, as
// well as its primary template and instantiating template arguments.
EnclosingTypeAliasTemplateDetails
getEnclosingTypeAliasTemplateDecl(Sema &SemaRef) {
  for (auto &CSC : llvm::reverse(SemaRef.CodeSynthesisContexts)) {
    if (CSC.Kind != Sema::CodeSynthesisContext::SynthesisKind::
                        TypeAliasTemplateInstantiation)
      continue;
    EnclosingTypeAliasTemplateDetails Result;
    auto *TATD = cast<TypeAliasTemplateDecl>(CSC.Entity),
         *Next = TATD->getInstantiatedFromMemberTemplate();
    Result = {
        /*Template=*/TATD,
        /*PrimaryTypeAliasDecl=*/TATD,
        /*AssociatedTemplateArguments=*/CSC.template_arguments(),
    };
    while (Next) {
      Result.PrimaryTypeAliasDecl = Next;
      Next = Next->getInstantiatedFromMemberTemplate();
    }
    return Result;
  }
  return {};
}

// Check if we are currently inside of a lambda expression that is
// surrounded by a using alias declaration. e.g.
//   template <class> using type = decltype([](auto) { ^ }());
// We have to do so since a TypeAliasTemplateDecl (or a TypeAliasDecl) is never
// a DeclContext, nor does it have an associated specialization Decl from which
// we could collect these template arguments.
bool isLambdaEnclosedByTypeAliasDecl(
    const FunctionDecl *LambdaCallOperator,
    const TypeAliasTemplateDecl *PrimaryTypeAliasDecl) {
  struct Visitor : RecursiveASTVisitor<Visitor> {
    Visitor(const FunctionDecl *CallOperator) : CallOperator(CallOperator) {}
    bool VisitLambdaExpr(const LambdaExpr *LE) {
      // Return true to bail out of the traversal, implying the Decl contains
      // the lambda.
      return getPrimaryTemplateOfGenericLambda(LE->getCallOperator()) !=
             CallOperator;
    }
    const FunctionDecl *CallOperator;
  };

  QualType Underlying =
      PrimaryTypeAliasDecl->getTemplatedDecl()->getUnderlyingType();

  return !Visitor(getPrimaryTemplateOfGenericLambda(LambdaCallOperator))
              .TraverseType(Underlying);
}

// Add template arguments from a variable template instantiation.
Response
HandleVarTemplateSpec(const VarTemplateSpecializationDecl *VarTemplSpec,
                      MultiLevelTemplateArgumentList &Result,
                      bool SkipForSpecialization) {
  // For a class-scope explicit specialization, there are no template arguments
  // at this level, but there may be enclosing template arguments.
  if (VarTemplSpec->isClassScopeExplicitSpecialization())
    return Response::DontClearRelativeToPrimaryNextDecl(VarTemplSpec);

  // We're done when we hit an explicit specialization.
  if (VarTemplSpec->getSpecializationKind() == TSK_ExplicitSpecialization &&
      !isa<VarTemplatePartialSpecializationDecl>(VarTemplSpec))
    return Response::Done();

  // If this variable template specialization was instantiated from a
  // specialized member that is a variable template, we're done.
  assert(VarTemplSpec->getSpecializedTemplate() && "No variable template?");
  llvm::PointerUnion<VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>
      Specialized = VarTemplSpec->getSpecializedTemplateOrPartial();
  if (VarTemplatePartialSpecializationDecl *Partial =
          Specialized.dyn_cast<VarTemplatePartialSpecializationDecl *>()) {
    if (!SkipForSpecialization)
      Result.addOuterTemplateArguments(
          Partial, VarTemplSpec->getTemplateInstantiationArgs().asArray(),
          /*Final=*/false);
    if (Partial->isMemberSpecialization())
      return Response::Done();
  } else {
    VarTemplateDecl *Tmpl = Specialized.get<VarTemplateDecl *>();
    if (!SkipForSpecialization)
      Result.addOuterTemplateArguments(
          Tmpl, VarTemplSpec->getTemplateInstantiationArgs().asArray(),
          /*Final=*/false);
    if (Tmpl->isMemberSpecialization())
      return Response::Done();
  }
  return Response::DontClearRelativeToPrimaryNextDecl(VarTemplSpec);
}

// If we have a template template parameter with translation unit context,
// then we're performing substitution into a default template argument of
// this template template parameter before we've constructed the template
// that will own this template template parameter. In this case, we
// use empty template parameter lists for all of the outer templates
// to avoid performing any substitutions.
Response
HandleDefaultTempArgIntoTempTempParam(const TemplateTemplateParmDecl *TTP,
                                      MultiLevelTemplateArgumentList &Result) {
  for (unsigned I = 0, N = TTP->getDepth() + 1; I != N; ++I)
    Result.addOuterTemplateArguments(std::nullopt);
  return Response::Done();
}

Response HandlePartialClassTemplateSpec(
    const ClassTemplatePartialSpecializationDecl *PartialClassTemplSpec,
    MultiLevelTemplateArgumentList &Result, bool SkipForSpecialization) {
  if (!SkipForSpecialization)
      Result.addOuterRetainedLevels(PartialClassTemplSpec->getTemplateDepth());
  return Response::Done();
}

// Add template arguments from a class template instantiation.
Response
HandleClassTemplateSpec(const ClassTemplateSpecializationDecl *ClassTemplSpec,
                        MultiLevelTemplateArgumentList &Result,
                        bool SkipForSpecialization) {
  if (!ClassTemplSpec->isClassScopeExplicitSpecialization()) {
    // We're done when we hit an explicit specialization.
    if (ClassTemplSpec->getSpecializationKind() == TSK_ExplicitSpecialization &&
        !isa<ClassTemplatePartialSpecializationDecl>(ClassTemplSpec))
      return Response::Done();

    if (!SkipForSpecialization)
      Result.addOuterTemplateArguments(
          const_cast<ClassTemplateSpecializationDecl *>(ClassTemplSpec),
          ClassTemplSpec->getTemplateInstantiationArgs().asArray(),
          /*Final=*/false);

    // If this class template specialization was instantiated from a
    // specialized member that is a class template, we're done.
    assert(ClassTemplSpec->getSpecializedTemplate() && "No class template?");
    if (ClassTemplSpec->getSpecializedTemplate()->isMemberSpecialization())
      return Response::Done();

    // If this was instantiated from a partial template specialization, we need
    // to get the next level of declaration context from the partial
    // specialization, as the ClassTemplateSpecializationDecl's
    // DeclContext/LexicalDeclContext will be for the primary template.
    if (auto *InstFromPartialTempl = ClassTemplSpec->getSpecializedTemplateOrPartial()
                      .dyn_cast<ClassTemplatePartialSpecializationDecl *>())
      return Response::ChangeDecl(InstFromPartialTempl->getLexicalDeclContext());
  }
  return Response::UseNextDecl(ClassTemplSpec);
}

Response HandleFunction(Sema &SemaRef, const FunctionDecl *Function,
                        MultiLevelTemplateArgumentList &Result,
                        const FunctionDecl *Pattern, bool RelativeToPrimary,
                        bool ForConstraintInstantiation) {
  // Add template arguments from a function template specialization.
  if (!RelativeToPrimary &&
      Function->getTemplateSpecializationKindForInstantiation() ==
          TSK_ExplicitSpecialization)
    return Response::Done();

  if (!RelativeToPrimary &&
      Function->getTemplateSpecializationKind() == TSK_ExplicitSpecialization) {
    // This is an implicit instantiation of an explicit specialization. We
    // don't get any template arguments from this function but might get
    // some from an enclosing template.
    return Response::UseNextDecl(Function);
  } else if (const TemplateArgumentList *TemplateArgs =
                 Function->getTemplateSpecializationArgs()) {
    // Add the template arguments for this specialization.
    Result.addOuterTemplateArguments(const_cast<FunctionDecl *>(Function),
                                     TemplateArgs->asArray(),
                                     /*Final=*/false);

    if (RelativeToPrimary &&
        (Function->getTemplateSpecializationKind() ==
             TSK_ExplicitSpecialization ||
         (Function->getFriendObjectKind() &&
          !Function->getPrimaryTemplate()->getFriendObjectKind())))
      return Response::UseNextDecl(Function);

    // If this function was instantiated from a specialized member that is
    // a function template, we're done.
    assert(Function->getPrimaryTemplate() && "No function template?");
    if (Function->getPrimaryTemplate()->isMemberSpecialization())
      return Response::Done();

    // If this function is a generic lambda specialization, we are done.
    if (!ForConstraintInstantiation &&
        isGenericLambdaCallOperatorOrStaticInvokerSpecialization(Function))
      return Response::Done();

  } else if (Function->getDescribedFunctionTemplate()) {
    assert(
        (ForConstraintInstantiation || Result.getNumSubstitutedLevels() == 0) &&
        "Outer template not instantiated?");
  }
  // If this is a friend or local declaration and it declares an entity at
  // namespace scope, take arguments from its lexical parent
  // instead of its semantic parent, unless of course the pattern we're
  // instantiating actually comes from the file's context!
  if ((Function->getFriendObjectKind() || Function->isLocalExternDecl()) &&
      Function->getNonTransparentDeclContext()->isFileContext() &&
      (!Pattern || !Pattern->getLexicalDeclContext()->isFileContext())) {
    return Response::ChangeDecl(Function->getLexicalDeclContext());
  }

  if (ForConstraintInstantiation && Function->getFriendObjectKind())
    return Response::ChangeDecl(Function->getLexicalDeclContext());
  return Response::UseNextDecl(Function);
}

Response HandleFunctionTemplateDecl(const FunctionTemplateDecl *FTD,
                                    MultiLevelTemplateArgumentList &Result) {
  if (!isa<ClassTemplateSpecializationDecl>(FTD->getDeclContext())) {
    Result.addOuterTemplateArguments(
        const_cast<FunctionTemplateDecl *>(FTD),
        const_cast<FunctionTemplateDecl *>(FTD)->getInjectedTemplateArgs(),
        /*Final=*/false);

    NestedNameSpecifier *NNS = FTD->getTemplatedDecl()->getQualifier();

    while (const Type *Ty = NNS ? NNS->getAsType() : nullptr) {
      if (NNS->isInstantiationDependent()) {
        if (const auto *TSTy = Ty->getAs<TemplateSpecializationType>()) {
          ArrayRef<TemplateArgument> Arguments = TSTy->template_arguments();
          // Prefer template arguments from the injected-class-type if possible.
          // For example,
          // ```cpp
          // template <class... Pack> struct S {
          //   template <class T> void foo();
          // };
          // template <class... Pack> template <class T>
          //           ^^^^^^^^^^^^^ InjectedTemplateArgs
          //           They're of kind TemplateArgument::Pack, not of
          //           TemplateArgument::Type.
          // void S<Pack...>::foo() {}
          //        ^^^^^^^
          //        TSTy->template_arguments() (which are of PackExpansionType)
          // ```
          // This meets the contract in
          // TreeTransform::TryExpandParameterPacks that the template arguments
          // for unexpanded parameters should be of a Pack kind.
          if (TSTy->isCurrentInstantiation()) {
            auto *RD = TSTy->getCanonicalTypeInternal()->getAsCXXRecordDecl();
            if (ClassTemplateDecl *CTD = RD->getDescribedClassTemplate())
              Arguments = CTD->getInjectedTemplateArgs();
            else if (auto *Specialization =
                         dyn_cast<ClassTemplateSpecializationDecl>(RD))
              Arguments =
                  Specialization->getTemplateInstantiationArgs().asArray();
          }
          Result.addOuterTemplateArguments(
              const_cast<FunctionTemplateDecl *>(FTD), Arguments,
              /*Final=*/false);
        }
      }

      NNS = NNS->getPrefix();
    }
  }

  return Response::ChangeDecl(FTD->getLexicalDeclContext());
}

Response HandleRecordDecl(Sema &SemaRef, const CXXRecordDecl *Rec,
                          MultiLevelTemplateArgumentList &Result,
                          ASTContext &Context,
                          bool ForConstraintInstantiation) {
  if (ClassTemplateDecl *ClassTemplate = Rec->getDescribedClassTemplate()) {
    assert(
        (ForConstraintInstantiation || Result.getNumSubstitutedLevels() == 0) &&
        "Outer template not instantiated?");
    if (ClassTemplate->isMemberSpecialization())
      return Response::Done();
    if (ForConstraintInstantiation)
      Result.addOuterTemplateArguments(const_cast<CXXRecordDecl *>(Rec),
                                       ClassTemplate->getInjectedTemplateArgs(),
                                       /*Final=*/false);
  }

  if (const MemberSpecializationInfo *MSInfo =
          Rec->getMemberSpecializationInfo())
    if (MSInfo->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
      return Response::Done();

  bool IsFriend = Rec->getFriendObjectKind() ||
                  (Rec->getDescribedClassTemplate() &&
                   Rec->getDescribedClassTemplate()->getFriendObjectKind());
  if (ForConstraintInstantiation && IsFriend &&
      Rec->getNonTransparentDeclContext()->isFileContext()) {
    return Response::ChangeDecl(Rec->getLexicalDeclContext());
  }

  // This is to make sure we pick up the VarTemplateSpecializationDecl or the
  // TypeAliasTemplateDecl that this lambda is defined inside of.
  if (Rec->isLambda()) {
    if (const Decl *LCD = Rec->getLambdaContextDecl())
      return Response::ChangeDecl(LCD);
    // Retrieve the template arguments for a using alias declaration.
    // This is necessary for constraint checking, since we always keep
    // constraints relative to the primary template.
    if (auto TypeAlias = getEnclosingTypeAliasTemplateDecl(SemaRef);
        ForConstraintInstantiation && TypeAlias) {
      if (isLambdaEnclosedByTypeAliasDecl(Rec->getLambdaCallOperator(),
                                          TypeAlias.PrimaryTypeAliasDecl)) {
        Result.addOuterTemplateArguments(TypeAlias.Template,
                                         TypeAlias.AssociatedTemplateArguments,
                                         /*Final=*/false);
        // Visit the parent of the current type alias declaration rather than
        // the lambda thereof.
        // E.g., in the following example:
        // struct S {
        //  template <class> using T = decltype([]<Concept> {} ());
        // };
        // void foo() {
        //   S::T var;
        // }
        // The instantiated lambda expression (which we're visiting at 'var')
        // has a function DeclContext 'foo' rather than the Record DeclContext
        // S. This seems to be an oversight to me that we may want to set a
        // Sema Context from the CXXScopeSpec before substituting into T.
        return Response::ChangeDecl(TypeAlias.Template->getDeclContext());
      }
    }
  }

  return Response::UseNextDecl(Rec);
}

Response HandleImplicitConceptSpecializationDecl(
    const ImplicitConceptSpecializationDecl *CSD,
    MultiLevelTemplateArgumentList &Result) {
  Result.addOuterTemplateArguments(
      const_cast<ImplicitConceptSpecializationDecl *>(CSD),
      CSD->getTemplateArguments(),
      /*Final=*/false);
  return Response::UseNextDecl(CSD);
}

Response HandleGenericDeclContext(const Decl *CurDecl) {
  return Response::UseNextDecl(CurDecl);
}
} // namespace TemplateInstArgsHelpers
} // namespace

MultiLevelTemplateArgumentList Sema::getTemplateInstantiationArgs(
    const NamedDecl *ND, const DeclContext *DC, bool Final,
    std::optional<ArrayRef<TemplateArgument>> Innermost, bool RelativeToPrimary,
    const FunctionDecl *Pattern, bool ForConstraintInstantiation,
    bool SkipForSpecialization) {
  assert((ND || DC) && "Can't find arguments for a decl if one isn't provided");
  // Accumulate the set of template argument lists in this structure.
  MultiLevelTemplateArgumentList Result;

  using namespace TemplateInstArgsHelpers;
  const Decl *CurDecl = ND;

  if (!CurDecl)
    CurDecl = Decl::castFromDeclContext(DC);

  if (Innermost) {
    Result.addOuterTemplateArguments(const_cast<NamedDecl *>(ND), *Innermost,
                                     Final);
    // Populate placeholder template arguments for TemplateTemplateParmDecls.
    // This is essential for the case e.g.
    //
    // template <class> concept Concept = false;
    // template <template <Concept C> class T> void foo(T<int>)
    //
    // where parameter C has a depth of 1 but the substituting argument `int`
    // has a depth of 0.
    if (const auto *TTP = dyn_cast<TemplateTemplateParmDecl>(CurDecl))
      HandleDefaultTempArgIntoTempTempParam(TTP, Result);
    CurDecl = Response::UseNextDecl(CurDecl).NextDecl;
  }

  while (!CurDecl->isFileContextDecl()) {
    Response R;
    if (const auto *VarTemplSpec =
            dyn_cast<VarTemplateSpecializationDecl>(CurDecl)) {
      R = HandleVarTemplateSpec(VarTemplSpec, Result, SkipForSpecialization);
    } else if (const auto *PartialClassTemplSpec =
                   dyn_cast<ClassTemplatePartialSpecializationDecl>(CurDecl)) {
      R = HandlePartialClassTemplateSpec(PartialClassTemplSpec, Result,
                                         SkipForSpecialization);
    } else if (const auto *ClassTemplSpec =
                   dyn_cast<ClassTemplateSpecializationDecl>(CurDecl)) {
      R = HandleClassTemplateSpec(ClassTemplSpec, Result,
                                  SkipForSpecialization);
    } else if (const auto *Function = dyn_cast<FunctionDecl>(CurDecl)) {
      R = HandleFunction(*this, Function, Result, Pattern, RelativeToPrimary,
                         ForConstraintInstantiation);
    } else if (const auto *Rec = dyn_cast<CXXRecordDecl>(CurDecl)) {
      R = HandleRecordDecl(*this, Rec, Result, Context,
                           ForConstraintInstantiation);
    } else if (const auto *CSD =
                   dyn_cast<ImplicitConceptSpecializationDecl>(CurDecl)) {
      R = HandleImplicitConceptSpecializationDecl(CSD, Result);
    } else if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(CurDecl)) {
      R = HandleFunctionTemplateDecl(FTD, Result);
    } else if (const auto *CTD = dyn_cast<ClassTemplateDecl>(CurDecl)) {
      R = Response::ChangeDecl(CTD->getLexicalDeclContext());
    } else if (!isa<DeclContext>(CurDecl)) {
      R = Response::DontClearRelativeToPrimaryNextDecl(CurDecl);
      if (const auto *TTP = dyn_cast<TemplateTemplateParmDecl>(CurDecl)) {
        R = HandleDefaultTempArgIntoTempTempParam(TTP, Result);
      }
    } else {
      R = HandleGenericDeclContext(CurDecl);
    }

    if (R.IsDone)
      return Result;
    if (R.ClearRelativeToPrimary)
      RelativeToPrimary = false;
    assert(R.NextDecl);
    CurDecl = R.NextDecl;
  }

  return Result;
}

bool Sema::CodeSynthesisContext::isInstantiationRecord() const {
  switch (Kind) {
  case TemplateInstantiation:
  case ExceptionSpecInstantiation:
  case DefaultTemplateArgumentInstantiation:
  case DefaultFunctionArgumentInstantiation:
  case ExplicitTemplateArgumentSubstitution:
  case DeducedTemplateArgumentSubstitution:
  case PriorTemplateArgumentSubstitution:
  case ConstraintsCheck:
  case NestedRequirementConstraintsCheck:
    return true;

  case RequirementInstantiation:
  case RequirementParameterInstantiation:
  case DefaultTemplateArgumentChecking:
  case DeclaringSpecialMember:
  case DeclaringImplicitEqualityComparison:
  case DefiningSynthesizedFunction:
  case ExceptionSpecEvaluation:
  case ConstraintSubstitution:
  case ParameterMappingSubstitution:
  case ConstraintNormalization:
  case RewritingOperatorAsSpaceship:
  case InitializingStructuredBinding:
  case MarkingClassDllexported:
  case BuildingBuiltinDumpStructCall:
  case LambdaExpressionSubstitution:
  case BuildingDeductionGuides:
  case TypeAliasTemplateInstantiation:
    return false;

  // This function should never be called when Kind's value is Memoization.
  case Memoization:
    break;
  }

  llvm_unreachable("Invalid SynthesisKind!");
}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, CodeSynthesisContext::SynthesisKind Kind,
    SourceLocation PointOfInstantiation, SourceRange InstantiationRange,
    Decl *Entity, NamedDecl *Template, ArrayRef<TemplateArgument> TemplateArgs,
    sema::TemplateDeductionInfo *DeductionInfo)
    : SemaRef(SemaRef) {
  // Don't allow further instantiation if a fatal error and an uncompilable
  // error have occurred. Any diagnostics we might have raised will not be
  // visible, and we do not need to construct a correct AST.
  if (SemaRef.Diags.hasFatalErrorOccurred() &&
      SemaRef.hasUncompilableErrorOccurred()) {
    Invalid = true;
    return;
  }
  Invalid = CheckInstantiationDepth(PointOfInstantiation, InstantiationRange);
  if (!Invalid) {
    CodeSynthesisContext Inst;
    Inst.Kind = Kind;
    Inst.PointOfInstantiation = PointOfInstantiation;
    Inst.Entity = Entity;
    Inst.Template = Template;
    Inst.TemplateArgs = TemplateArgs.data();
    Inst.NumTemplateArgs = TemplateArgs.size();
    Inst.DeductionInfo = DeductionInfo;
    Inst.InstantiationRange = InstantiationRange;
    SemaRef.pushCodeSynthesisContext(Inst);

    AlreadyInstantiating = !Inst.Entity ? false :
        !SemaRef.InstantiatingSpecializations
             .insert({Inst.Entity->getCanonicalDecl(), Inst.Kind})
             .second;
    atTemplateBegin(SemaRef.TemplateInstCallbacks, SemaRef, Inst);
  }
}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, Decl *Entity,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(SemaRef,
                            CodeSynthesisContext::TemplateInstantiation,
                            PointOfInstantiation, InstantiationRange, Entity) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, FunctionDecl *Entity,
    ExceptionSpecification, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::ExceptionSpecInstantiation,
          PointOfInstantiation, InstantiationRange, Entity) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, TemplateParameter Param,
    TemplateDecl *Template, ArrayRef<TemplateArgument> TemplateArgs,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef,
          CodeSynthesisContext::DefaultTemplateArgumentInstantiation,
          PointOfInstantiation, InstantiationRange, getAsNamedDecl(Param),
          Template, TemplateArgs) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    FunctionTemplateDecl *FunctionTemplate,
    ArrayRef<TemplateArgument> TemplateArgs,
    CodeSynthesisContext::SynthesisKind Kind,
    sema::TemplateDeductionInfo &DeductionInfo, SourceRange InstantiationRange)
    : InstantiatingTemplate(SemaRef, Kind, PointOfInstantiation,
                            InstantiationRange, FunctionTemplate, nullptr,
                            TemplateArgs, &DeductionInfo) {
  assert(Kind == CodeSynthesisContext::ExplicitTemplateArgumentSubstitution ||
         Kind == CodeSynthesisContext::DeducedTemplateArgumentSubstitution ||
         Kind == CodeSynthesisContext::BuildingDeductionGuides);
}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    TemplateDecl *Template,
    ArrayRef<TemplateArgument> TemplateArgs,
    sema::TemplateDeductionInfo &DeductionInfo, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef,
          CodeSynthesisContext::DeducedTemplateArgumentSubstitution,
          PointOfInstantiation, InstantiationRange, Template, nullptr,
          TemplateArgs, &DeductionInfo) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    ClassTemplatePartialSpecializationDecl *PartialSpec,
    ArrayRef<TemplateArgument> TemplateArgs,
    sema::TemplateDeductionInfo &DeductionInfo, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef,
          CodeSynthesisContext::DeducedTemplateArgumentSubstitution,
          PointOfInstantiation, InstantiationRange, PartialSpec, nullptr,
          TemplateArgs, &DeductionInfo) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    VarTemplatePartialSpecializationDecl *PartialSpec,
    ArrayRef<TemplateArgument> TemplateArgs,
    sema::TemplateDeductionInfo &DeductionInfo, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef,
          CodeSynthesisContext::DeducedTemplateArgumentSubstitution,
          PointOfInstantiation, InstantiationRange, PartialSpec, nullptr,
          TemplateArgs, &DeductionInfo) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, ParmVarDecl *Param,
    ArrayRef<TemplateArgument> TemplateArgs, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef,
          CodeSynthesisContext::DefaultFunctionArgumentInstantiation,
          PointOfInstantiation, InstantiationRange, Param, nullptr,
          TemplateArgs) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, NamedDecl *Template,
    NonTypeTemplateParmDecl *Param, ArrayRef<TemplateArgument> TemplateArgs,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef,
          CodeSynthesisContext::PriorTemplateArgumentSubstitution,
          PointOfInstantiation, InstantiationRange, Param, Template,
          TemplateArgs) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, NamedDecl *Template,
    TemplateTemplateParmDecl *Param, ArrayRef<TemplateArgument> TemplateArgs,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef,
          CodeSynthesisContext::PriorTemplateArgumentSubstitution,
          PointOfInstantiation, InstantiationRange, Param, Template,
          TemplateArgs) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    TypeAliasTemplateDecl *Entity, ArrayRef<TemplateArgument> TemplateArgs,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::TypeAliasTemplateInstantiation,
          PointOfInstantiation, InstantiationRange, /*Entity=*/Entity,
          /*Template=*/nullptr, TemplateArgs) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, TemplateDecl *Template,
    NamedDecl *Param, ArrayRef<TemplateArgument> TemplateArgs,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::DefaultTemplateArgumentChecking,
          PointOfInstantiation, InstantiationRange, Param, Template,
          TemplateArgs) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    concepts::Requirement *Req, sema::TemplateDeductionInfo &DeductionInfo,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::RequirementInstantiation,
          PointOfInstantiation, InstantiationRange, /*Entity=*/nullptr,
          /*Template=*/nullptr, /*TemplateArgs=*/std::nullopt, &DeductionInfo) {
}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    concepts::NestedRequirement *Req, ConstraintsCheck,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::NestedRequirementConstraintsCheck,
          PointOfInstantiation, InstantiationRange, /*Entity=*/nullptr,
          /*Template=*/nullptr, /*TemplateArgs=*/std::nullopt) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, const RequiresExpr *RE,
    sema::TemplateDeductionInfo &DeductionInfo, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::RequirementParameterInstantiation,
          PointOfInstantiation, InstantiationRange, /*Entity=*/nullptr,
          /*Template=*/nullptr, /*TemplateArgs=*/std::nullopt, &DeductionInfo) {
}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    ConstraintsCheck, NamedDecl *Template,
    ArrayRef<TemplateArgument> TemplateArgs, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::ConstraintsCheck,
          PointOfInstantiation, InstantiationRange, Template, nullptr,
          TemplateArgs) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    ConstraintSubstitution, NamedDecl *Template,
    sema::TemplateDeductionInfo &DeductionInfo, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::ConstraintSubstitution,
          PointOfInstantiation, InstantiationRange, Template, nullptr,
          {}, &DeductionInfo) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    ConstraintNormalization, NamedDecl *Template,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::ConstraintNormalization,
          PointOfInstantiation, InstantiationRange, Template) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation,
    ParameterMappingSubstitution, NamedDecl *Template,
    SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::ParameterMappingSubstitution,
          PointOfInstantiation, InstantiationRange, Template) {}

Sema::InstantiatingTemplate::InstantiatingTemplate(
    Sema &SemaRef, SourceLocation PointOfInstantiation, TemplateDecl *Entity,
    BuildingDeductionGuidesTag, SourceRange InstantiationRange)
    : InstantiatingTemplate(
          SemaRef, CodeSynthesisContext::BuildingDeductionGuides,
          PointOfInstantiation, InstantiationRange, Entity) {}


void Sema::pushCodeSynthesisContext(CodeSynthesisContext Ctx) {
  Ctx.SavedInNonInstantiationSFINAEContext = InNonInstantiationSFINAEContext;
  InNonInstantiationSFINAEContext = false;

  CodeSynthesisContexts.push_back(Ctx);

  if (!Ctx.isInstantiationRecord())
    ++NonInstantiationEntries;

  // Check to see if we're low on stack space. We can't do anything about this
  // from here, but we can at least warn the user.
  if (isStackNearlyExhausted())
    warnStackExhausted(Ctx.PointOfInstantiation);
}

void Sema::popCodeSynthesisContext() {
  auto &Active = CodeSynthesisContexts.back();
  if (!Active.isInstantiationRecord()) {
    assert(NonInstantiationEntries > 0);
    --NonInstantiationEntries;
  }

  InNonInstantiationSFINAEContext = Active.SavedInNonInstantiationSFINAEContext;

  // Name lookup no longer looks in this template's defining module.
  assert(CodeSynthesisContexts.size() >=
             CodeSynthesisContextLookupModules.size() &&
         "forgot to remove a lookup module for a template instantiation");
  if (CodeSynthesisContexts.size() ==
      CodeSynthesisContextLookupModules.size()) {
    if (Module *M = CodeSynthesisContextLookupModules.back())
      LookupModulesCache.erase(M);
    CodeSynthesisContextLookupModules.pop_back();
  }

  // If we've left the code synthesis context for the current context stack,
  // stop remembering that we've emitted that stack.
  if (CodeSynthesisContexts.size() ==
      LastEmittedCodeSynthesisContextDepth)
    LastEmittedCodeSynthesisContextDepth = 0;

  CodeSynthesisContexts.pop_back();
}

void Sema::InstantiatingTemplate::Clear() {
  if (!Invalid) {
    if (!AlreadyInstantiating) {
      auto &Active = SemaRef.CodeSynthesisContexts.back();
      if (Active.Entity)
        SemaRef.InstantiatingSpecializations.erase(
            {Active.Entity->getCanonicalDecl(), Active.Kind});
    }

    atTemplateEnd(SemaRef.TemplateInstCallbacks, SemaRef,
                  SemaRef.CodeSynthesisContexts.back());

    SemaRef.popCodeSynthesisContext();
    Invalid = true;
  }
}

static std::string convertCallArgsToString(Sema &S,
                                           llvm::ArrayRef<const Expr *> Args) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  llvm::ListSeparator Comma;
  for (const Expr *Arg : Args) {
    OS << Comma;
    Arg->IgnoreParens()->printPretty(OS, nullptr,
                                     S.Context.getPrintingPolicy());
  }
  return Result;
}

bool Sema::InstantiatingTemplate::CheckInstantiationDepth(
                                        SourceLocation PointOfInstantiation,
                                           SourceRange InstantiationRange) {
  assert(SemaRef.NonInstantiationEntries <=
         SemaRef.CodeSynthesisContexts.size());
  if ((SemaRef.CodeSynthesisContexts.size() -
          SemaRef.NonInstantiationEntries)
        <= SemaRef.getLangOpts().InstantiationDepth)
    return false;

  SemaRef.Diag(PointOfInstantiation,
               diag::err_template_recursion_depth_exceeded)
    << SemaRef.getLangOpts().InstantiationDepth
    << InstantiationRange;
  SemaRef.Diag(PointOfInstantiation, diag::note_template_recursion_depth)
    << SemaRef.getLangOpts().InstantiationDepth;
  return true;
}

void Sema::PrintInstantiationStack() {
  // Determine which template instantiations to skip, if any.
  unsigned SkipStart = CodeSynthesisContexts.size(), SkipEnd = SkipStart;
  unsigned Limit = Diags.getTemplateBacktraceLimit();
  if (Limit && Limit < CodeSynthesisContexts.size()) {
    SkipStart = Limit / 2 + Limit % 2;
    SkipEnd = CodeSynthesisContexts.size() - Limit / 2;
  }

  // FIXME: In all of these cases, we need to show the template arguments
  unsigned InstantiationIdx = 0;
  for (SmallVectorImpl<CodeSynthesisContext>::reverse_iterator
         Active = CodeSynthesisContexts.rbegin(),
         ActiveEnd = CodeSynthesisContexts.rend();
       Active != ActiveEnd;
       ++Active, ++InstantiationIdx) {
    // Skip this instantiation?
    if (InstantiationIdx >= SkipStart && InstantiationIdx < SkipEnd) {
      if (InstantiationIdx == SkipStart) {
        // Note that we're skipping instantiations.
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_instantiation_contexts_suppressed)
          << unsigned(CodeSynthesisContexts.size() - Limit);
      }
      continue;
    }

    switch (Active->Kind) {
    case CodeSynthesisContext::TemplateInstantiation: {
      Decl *D = Active->Entity;
      if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(D)) {
        unsigned DiagID = diag::note_template_member_class_here;
        if (isa<ClassTemplateSpecializationDecl>(Record))
          DiagID = diag::note_template_class_instantiation_here;
        Diags.Report(Active->PointOfInstantiation, DiagID)
          << Record << Active->InstantiationRange;
      } else if (FunctionDecl *Function = dyn_cast<FunctionDecl>(D)) {
        unsigned DiagID;
        if (Function->getPrimaryTemplate())
          DiagID = diag::note_function_template_spec_here;
        else
          DiagID = diag::note_template_member_function_here;
        Diags.Report(Active->PointOfInstantiation, DiagID)
          << Function
          << Active->InstantiationRange;
      } else if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
        Diags.Report(Active->PointOfInstantiation,
                     VD->isStaticDataMember()?
                       diag::note_template_static_data_member_def_here
                     : diag::note_template_variable_def_here)
          << VD
          << Active->InstantiationRange;
      } else if (EnumDecl *ED = dyn_cast<EnumDecl>(D)) {
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_template_enum_def_here)
          << ED
          << Active->InstantiationRange;
      } else if (FieldDecl *FD = dyn_cast<FieldDecl>(D)) {
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_template_nsdmi_here)
            << FD << Active->InstantiationRange;
      } else if (ClassTemplateDecl *CTD = dyn_cast<ClassTemplateDecl>(D)) {
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_template_class_instantiation_here)
            << CTD << Active->InstantiationRange;
      }
      break;
    }

    case CodeSynthesisContext::DefaultTemplateArgumentInstantiation: {
      TemplateDecl *Template = cast<TemplateDecl>(Active->Template);
      SmallString<128> TemplateArgsStr;
      llvm::raw_svector_ostream OS(TemplateArgsStr);
      Template->printName(OS, getPrintingPolicy());
      printTemplateArgumentList(OS, Active->template_arguments(),
                                getPrintingPolicy());
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_default_arg_instantiation_here)
        << OS.str()
        << Active->InstantiationRange;
      break;
    }

    case CodeSynthesisContext::ExplicitTemplateArgumentSubstitution: {
      FunctionTemplateDecl *FnTmpl = cast<FunctionTemplateDecl>(Active->Entity);
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_explicit_template_arg_substitution_here)
        << FnTmpl
        << getTemplateArgumentBindingsText(FnTmpl->getTemplateParameters(),
                                           Active->TemplateArgs,
                                           Active->NumTemplateArgs)
        << Active->InstantiationRange;
      break;
    }

    case CodeSynthesisContext::DeducedTemplateArgumentSubstitution: {
      if (FunctionTemplateDecl *FnTmpl =
              dyn_cast<FunctionTemplateDecl>(Active->Entity)) {
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_function_template_deduction_instantiation_here)
          << FnTmpl
          << getTemplateArgumentBindingsText(FnTmpl->getTemplateParameters(),
                                             Active->TemplateArgs,
                                             Active->NumTemplateArgs)
          << Active->InstantiationRange;
      } else {
        bool IsVar = isa<VarTemplateDecl>(Active->Entity) ||
                     isa<VarTemplateSpecializationDecl>(Active->Entity);
        bool IsTemplate = false;
        TemplateParameterList *Params;
        if (auto *D = dyn_cast<TemplateDecl>(Active->Entity)) {
          IsTemplate = true;
          Params = D->getTemplateParameters();
        } else if (auto *D = dyn_cast<ClassTemplatePartialSpecializationDecl>(
                       Active->Entity)) {
          Params = D->getTemplateParameters();
        } else if (auto *D = dyn_cast<VarTemplatePartialSpecializationDecl>(
                       Active->Entity)) {
          Params = D->getTemplateParameters();
        } else {
          llvm_unreachable("unexpected template kind");
        }

        Diags.Report(Active->PointOfInstantiation,
                     diag::note_deduced_template_arg_substitution_here)
          << IsVar << IsTemplate << cast<NamedDecl>(Active->Entity)
          << getTemplateArgumentBindingsText(Params, Active->TemplateArgs,
                                             Active->NumTemplateArgs)
          << Active->InstantiationRange;
      }
      break;
    }

    case CodeSynthesisContext::DefaultFunctionArgumentInstantiation: {
      ParmVarDecl *Param = cast<ParmVarDecl>(Active->Entity);
      FunctionDecl *FD = cast<FunctionDecl>(Param->getDeclContext());

      SmallString<128> TemplateArgsStr;
      llvm::raw_svector_ostream OS(TemplateArgsStr);
      FD->printName(OS, getPrintingPolicy());
      printTemplateArgumentList(OS, Active->template_arguments(),
                                getPrintingPolicy());
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_default_function_arg_instantiation_here)
        << OS.str()
        << Active->InstantiationRange;
      break;
    }

    case CodeSynthesisContext::PriorTemplateArgumentSubstitution: {
      NamedDecl *Parm = cast<NamedDecl>(Active->Entity);
      std::string Name;
      if (!Parm->getName().empty())
        Name = std::string(" '") + Parm->getName().str() + "'";

      TemplateParameterList *TemplateParams = nullptr;
      if (TemplateDecl *Template = dyn_cast<TemplateDecl>(Active->Template))
        TemplateParams = Template->getTemplateParameters();
      else
        TemplateParams =
          cast<ClassTemplatePartialSpecializationDecl>(Active->Template)
                                                      ->getTemplateParameters();
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_prior_template_arg_substitution)
        << isa<TemplateTemplateParmDecl>(Parm)
        << Name
        << getTemplateArgumentBindingsText(TemplateParams,
                                           Active->TemplateArgs,
                                           Active->NumTemplateArgs)
        << Active->InstantiationRange;
      break;
    }

    case CodeSynthesisContext::DefaultTemplateArgumentChecking: {
      TemplateParameterList *TemplateParams = nullptr;
      if (TemplateDecl *Template = dyn_cast<TemplateDecl>(Active->Template))
        TemplateParams = Template->getTemplateParameters();
      else
        TemplateParams =
          cast<ClassTemplatePartialSpecializationDecl>(Active->Template)
                                                      ->getTemplateParameters();

      Diags.Report(Active->PointOfInstantiation,
                   diag::note_template_default_arg_checking)
        << getTemplateArgumentBindingsText(TemplateParams,
                                           Active->TemplateArgs,
                                           Active->NumTemplateArgs)
        << Active->InstantiationRange;
      break;
    }

    case CodeSynthesisContext::ExceptionSpecEvaluation:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_evaluating_exception_spec_here)
          << cast<FunctionDecl>(Active->Entity);
      break;

    case CodeSynthesisContext::ExceptionSpecInstantiation:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_template_exception_spec_instantiation_here)
        << cast<FunctionDecl>(Active->Entity)
        << Active->InstantiationRange;
      break;

    case CodeSynthesisContext::RequirementInstantiation:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_template_requirement_instantiation_here)
        << Active->InstantiationRange;
      break;
    case CodeSynthesisContext::RequirementParameterInstantiation:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_template_requirement_params_instantiation_here)
          << Active->InstantiationRange;
      break;

    case CodeSynthesisContext::NestedRequirementConstraintsCheck:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_nested_requirement_here)
        << Active->InstantiationRange;
      break;

    case CodeSynthesisContext::DeclaringSpecialMember:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_in_declaration_of_implicit_special_member)
          << cast<CXXRecordDecl>(Active->Entity)
          << llvm::to_underlying(Active->SpecialMember);
      break;

    case CodeSynthesisContext::DeclaringImplicitEqualityComparison:
      Diags.Report(Active->Entity->getLocation(),
                   diag::note_in_declaration_of_implicit_equality_comparison);
      break;

    case CodeSynthesisContext::DefiningSynthesizedFunction: {
      // FIXME: For synthesized functions that are not defaulted,
      // produce a note.
      auto *FD = dyn_cast<FunctionDecl>(Active->Entity);
      DefaultedFunctionKind DFK =
          FD ? getDefaultedFunctionKind(FD) : DefaultedFunctionKind();
      if (DFK.isSpecialMember()) {
        auto *MD = cast<CXXMethodDecl>(FD);
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_member_synthesized_at)
            << MD->isExplicitlyDefaulted()
            << llvm::to_underlying(DFK.asSpecialMember())
            << Context.getTagDeclType(MD->getParent());
      } else if (DFK.isComparison()) {
        QualType RecordType = FD->getParamDecl(0)
                                  ->getType()
                                  .getNonReferenceType()
                                  .getUnqualifiedType();
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_comparison_synthesized_at)
            << (int)DFK.asComparison() << RecordType;
      }
      break;
    }

    case CodeSynthesisContext::RewritingOperatorAsSpaceship:
      Diags.Report(Active->Entity->getLocation(),
                   diag::note_rewriting_operator_as_spaceship);
      break;

    case CodeSynthesisContext::InitializingStructuredBinding:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_in_binding_decl_init)
          << cast<BindingDecl>(Active->Entity);
      break;

    case CodeSynthesisContext::MarkingClassDllexported:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_due_to_dllexported_class)
          << cast<CXXRecordDecl>(Active->Entity) << !getLangOpts().CPlusPlus11;
      break;

    case CodeSynthesisContext::BuildingBuiltinDumpStructCall:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_building_builtin_dump_struct_call)
          << convertCallArgsToString(
                 *this, llvm::ArrayRef(Active->CallArgs, Active->NumCallArgs));
      break;

    case CodeSynthesisContext::Memoization:
      break;

    case CodeSynthesisContext::LambdaExpressionSubstitution:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_lambda_substitution_here);
      break;
    case CodeSynthesisContext::ConstraintsCheck: {
      unsigned DiagID = 0;
      if (!Active->Entity) {
        Diags.Report(Active->PointOfInstantiation,
                     diag::note_nested_requirement_here)
          << Active->InstantiationRange;
        break;
      }
      if (isa<ConceptDecl>(Active->Entity))
        DiagID = diag::note_concept_specialization_here;
      else if (isa<TemplateDecl>(Active->Entity))
        DiagID = diag::note_checking_constraints_for_template_id_here;
      else if (isa<VarTemplatePartialSpecializationDecl>(Active->Entity))
        DiagID = diag::note_checking_constraints_for_var_spec_id_here;
      else if (isa<ClassTemplatePartialSpecializationDecl>(Active->Entity))
        DiagID = diag::note_checking_constraints_for_class_spec_id_here;
      else {
        assert(isa<FunctionDecl>(Active->Entity));
        DiagID = diag::note_checking_constraints_for_function_here;
      }
      SmallString<128> TemplateArgsStr;
      llvm::raw_svector_ostream OS(TemplateArgsStr);
      cast<NamedDecl>(Active->Entity)->printName(OS, getPrintingPolicy());
      if (!isa<FunctionDecl>(Active->Entity)) {
        printTemplateArgumentList(OS, Active->template_arguments(),
                                  getPrintingPolicy());
      }
      Diags.Report(Active->PointOfInstantiation, DiagID) << OS.str()
        << Active->InstantiationRange;
      break;
    }
    case CodeSynthesisContext::ConstraintSubstitution:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_constraint_substitution_here)
          << Active->InstantiationRange;
      break;
    case CodeSynthesisContext::ConstraintNormalization:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_constraint_normalization_here)
          << cast<NamedDecl>(Active->Entity)->getName()
          << Active->InstantiationRange;
      break;
    case CodeSynthesisContext::ParameterMappingSubstitution:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_parameter_mapping_substitution_here)
          << Active->InstantiationRange;
      break;
    case CodeSynthesisContext::BuildingDeductionGuides:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_building_deduction_guide_here);
      break;
    case CodeSynthesisContext::TypeAliasTemplateInstantiation:
      Diags.Report(Active->PointOfInstantiation,
                   diag::note_template_type_alias_instantiation_here)
          << cast<TypeAliasTemplateDecl>(Active->Entity)
          << Active->InstantiationRange;
      break;
    }
  }
}

std::optional<TemplateDeductionInfo *> Sema::isSFINAEContext() const {
  if (InNonInstantiationSFINAEContext)
    return std::optional<TemplateDeductionInfo *>(nullptr);

  for (SmallVectorImpl<CodeSynthesisContext>::const_reverse_iterator
         Active = CodeSynthesisContexts.rbegin(),
         ActiveEnd = CodeSynthesisContexts.rend();
       Active != ActiveEnd;
       ++Active)
  {
    switch (Active->Kind) {
    case CodeSynthesisContext::TypeAliasTemplateInstantiation:
      // An instantiation of an alias template may or may not be a SFINAE
      // context, depending on what else is on the stack.
      if (isa<TypeAliasTemplateDecl>(Active->Entity))
        break;
      [[fallthrough]];
    case CodeSynthesisContext::TemplateInstantiation:
    case CodeSynthesisContext::DefaultFunctionArgumentInstantiation:
    case CodeSynthesisContext::ExceptionSpecInstantiation:
    case CodeSynthesisContext::ConstraintsCheck:
    case CodeSynthesisContext::ParameterMappingSubstitution:
    case CodeSynthesisContext::ConstraintNormalization:
    case CodeSynthesisContext::NestedRequirementConstraintsCheck:
      // This is a template instantiation, so there is no SFINAE.
      return std::nullopt;
    case CodeSynthesisContext::LambdaExpressionSubstitution:
      // [temp.deduct]p9
      // A lambda-expression appearing in a function type or a template
      // parameter is not considered part of the immediate context for the
      // purposes of template argument deduction.
      // CWG2672: A lambda-expression body is never in the immediate context.
      return std::nullopt;

    case CodeSynthesisContext::DefaultTemplateArgumentInstantiation:
    case CodeSynthesisContext::PriorTemplateArgumentSubstitution:
    case CodeSynthesisContext::DefaultTemplateArgumentChecking:
    case CodeSynthesisContext::RewritingOperatorAsSpaceship:
      // A default template argument instantiation and substitution into
      // template parameters with arguments for prior parameters may or may
      // not be a SFINAE context; look further up the stack.
      break;

    case CodeSynthesisContext::ExplicitTemplateArgumentSubstitution:
    case CodeSynthesisContext::DeducedTemplateArgumentSubstitution:
      // We're either substituting explicitly-specified template arguments,
      // deduced template arguments. SFINAE applies unless we are in a lambda
      // body, see [temp.deduct]p9.
    case CodeSynthesisContext::ConstraintSubstitution:
    case CodeSynthesisContext::RequirementInstantiation:
    case CodeSynthesisContext::RequirementParameterInstantiation:
      // SFINAE always applies in a constraint expression or a requirement
      // in a requires expression.
      assert(Active->DeductionInfo && "Missing deduction info pointer");
      return Active->DeductionInfo;

    case CodeSynthesisContext::DeclaringSpecialMember:
    case CodeSynthesisContext::DeclaringImplicitEqualityComparison:
    case CodeSynthesisContext::DefiningSynthesizedFunction:
    case CodeSynthesisContext::InitializingStructuredBinding:
    case CodeSynthesisContext::MarkingClassDllexported:
    case CodeSynthesisContext::BuildingBuiltinDumpStructCall:
    case CodeSynthesisContext::BuildingDeductionGuides:
      // This happens in a context unrelated to template instantiation, so
      // there is no SFINAE.
      return std::nullopt;

    case CodeSynthesisContext::ExceptionSpecEvaluation:
      // FIXME: This should not be treated as a SFINAE context, because
      // we will cache an incorrect exception specification. However, clang
      // bootstrap relies this! See PR31692.
      break;

    case CodeSynthesisContext::Memoization:
      break;
    }

    // The inner context was transparent for SFINAE. If it occurred within a
    // non-instantiation SFINAE context, then SFINAE applies.
    if (Active->SavedInNonInstantiationSFINAEContext)
      return std::optional<TemplateDeductionInfo *>(nullptr);
  }

  return std::nullopt;
}

//===----------------------------------------------------------------------===/
// Template Instantiation for Types
//===----------------------------------------------------------------------===/
namespace {
  class TemplateInstantiator : public TreeTransform<TemplateInstantiator> {
    const MultiLevelTemplateArgumentList &TemplateArgs;
    SourceLocation Loc;
    DeclarationName Entity;
    // Whether to evaluate the C++20 constraints or simply substitute into them.
    bool EvaluateConstraints = true;

  public:
    typedef TreeTransform<TemplateInstantiator> inherited;

    TemplateInstantiator(Sema &SemaRef,
                         const MultiLevelTemplateArgumentList &TemplateArgs,
                         SourceLocation Loc, DeclarationName Entity)
        : inherited(SemaRef), TemplateArgs(TemplateArgs), Loc(Loc),
          Entity(Entity) {}

    void setEvaluateConstraints(bool B) {
      EvaluateConstraints = B;
    }
    bool getEvaluateConstraints() {
      return EvaluateConstraints;
    }

    /// Determine whether the given type \p T has already been
    /// transformed.
    ///
    /// For the purposes of template instantiation, a type has already been
    /// transformed if it is NULL or if it is not dependent.
    bool AlreadyTransformed(QualType T);

    /// Returns the location of the entity being instantiated, if known.
    SourceLocation getBaseLocation() { return Loc; }

    /// Returns the name of the entity being instantiated, if any.
    DeclarationName getBaseEntity() { return Entity; }

    /// Sets the "base" location and entity when that
    /// information is known based on another transformation.
    void setBase(SourceLocation Loc, DeclarationName Entity) {
      this->Loc = Loc;
      this->Entity = Entity;
    }

    unsigned TransformTemplateDepth(unsigned Depth) {
      return TemplateArgs.getNewDepth(Depth);
    }

    std::optional<unsigned> getPackIndex(TemplateArgument Pack) {
      int Index = getSema().ArgumentPackSubstitutionIndex;
      if (Index == -1)
        return std::nullopt;
      return Pack.pack_size() - 1 - Index;
    }

    bool TryExpandParameterPacks(SourceLocation EllipsisLoc,
                                 SourceRange PatternRange,
                                 ArrayRef<UnexpandedParameterPack> Unexpanded,
                                 bool &ShouldExpand, bool &RetainExpansion,
                                 std::optional<unsigned> &NumExpansions) {
      return getSema().CheckParameterPacksForExpansion(EllipsisLoc,
                                                       PatternRange, Unexpanded,
                                                       TemplateArgs,
                                                       ShouldExpand,
                                                       RetainExpansion,
                                                       NumExpansions);
    }

    void ExpandingFunctionParameterPack(ParmVarDecl *Pack) {
      SemaRef.CurrentInstantiationScope->MakeInstantiatedLocalArgPack(Pack);
    }

    TemplateArgument ForgetPartiallySubstitutedPack() {
      TemplateArgument Result;
      if (NamedDecl *PartialPack
            = SemaRef.CurrentInstantiationScope->getPartiallySubstitutedPack()){
        MultiLevelTemplateArgumentList &TemplateArgs
          = const_cast<MultiLevelTemplateArgumentList &>(this->TemplateArgs);
        unsigned Depth, Index;
        std::tie(Depth, Index) = getDepthAndIndex(PartialPack);
        if (TemplateArgs.hasTemplateArgument(Depth, Index)) {
          Result = TemplateArgs(Depth, Index);
          TemplateArgs.setArgument(Depth, Index, TemplateArgument());
        }
      }

      return Result;
    }

    void RememberPartiallySubstitutedPack(TemplateArgument Arg) {
      if (Arg.isNull())
        return;

      if (NamedDecl *PartialPack
            = SemaRef.CurrentInstantiationScope->getPartiallySubstitutedPack()){
        MultiLevelTemplateArgumentList &TemplateArgs
        = const_cast<MultiLevelTemplateArgumentList &>(this->TemplateArgs);
        unsigned Depth, Index;
        std::tie(Depth, Index) = getDepthAndIndex(PartialPack);
        TemplateArgs.setArgument(Depth, Index, Arg);
      }
    }

    /// Transform the given declaration by instantiating a reference to
    /// this declaration.
    Decl *TransformDecl(SourceLocation Loc, Decl *D);

    void transformAttrs(Decl *Old, Decl *New) {
      SemaRef.InstantiateAttrs(TemplateArgs, Old, New);
    }

    void transformedLocalDecl(Decl *Old, ArrayRef<Decl *> NewDecls) {
      if (Old->isParameterPack()) {
        SemaRef.CurrentInstantiationScope->MakeInstantiatedLocalArgPack(Old);
        for (auto *New : NewDecls)
          SemaRef.CurrentInstantiationScope->InstantiatedLocalPackArg(
              Old, cast<VarDecl>(New));
        return;
      }

      assert(NewDecls.size() == 1 &&
             "should only have multiple expansions for a pack");
      Decl *New = NewDecls.front();

      // If we've instantiated the call operator of a lambda or the call
      // operator template of a generic lambda, update the "instantiation of"
      // information.
      auto *NewMD = dyn_cast<CXXMethodDecl>(New);
      if (NewMD && isLambdaCallOperator(NewMD)) {
        auto *OldMD = dyn_cast<CXXMethodDecl>(Old);
        if (auto *NewTD = NewMD->getDescribedFunctionTemplate())
          NewTD->setInstantiatedFromMemberTemplate(
              OldMD->getDescribedFunctionTemplate());
        else
          NewMD->setInstantiationOfMemberFunction(OldMD,
                                                  TSK_ImplicitInstantiation);
      }

      SemaRef.CurrentInstantiationScope->InstantiatedLocal(Old, New);

      // We recreated a local declaration, but not by instantiating it. There
      // may be pending dependent diagnostics to produce.
      if (auto *DC = dyn_cast<DeclContext>(Old);
          DC && DC->isDependentContext() && DC->isFunctionOrMethod())
        SemaRef.PerformDependentDiagnostics(DC, TemplateArgs);
    }

    /// Transform the definition of the given declaration by
    /// instantiating it.
    Decl *TransformDefinition(SourceLocation Loc, Decl *D);

    /// Transform the first qualifier within a scope by instantiating the
    /// declaration.
    NamedDecl *TransformFirstQualifierInScope(NamedDecl *D, SourceLocation Loc);

    bool TransformExceptionSpec(SourceLocation Loc,
                                FunctionProtoType::ExceptionSpecInfo &ESI,
                                SmallVectorImpl<QualType> &Exceptions,
                                bool &Changed);

    /// Rebuild the exception declaration and register the declaration
    /// as an instantiated local.
    VarDecl *RebuildExceptionDecl(VarDecl *ExceptionDecl,
                                  TypeSourceInfo *Declarator,
                                  SourceLocation StartLoc,
                                  SourceLocation NameLoc,
                                  IdentifierInfo *Name);

    /// Rebuild the Objective-C exception declaration and register the
    /// declaration as an instantiated local.
    VarDecl *RebuildObjCExceptionDecl(VarDecl *ExceptionDecl,
                                      TypeSourceInfo *TSInfo, QualType T);

    /// Check for tag mismatches when instantiating an
    /// elaborated type.
    QualType RebuildElaboratedType(SourceLocation KeywordLoc,
                                   ElaboratedTypeKeyword Keyword,
                                   NestedNameSpecifierLoc QualifierLoc,
                                   QualType T);

    TemplateName
    TransformTemplateName(CXXScopeSpec &SS, TemplateName Name,
                          SourceLocation NameLoc,
                          QualType ObjectType = QualType(),
                          NamedDecl *FirstQualifierInScope = nullptr,
                          bool AllowInjectedClassName = false);

    const CXXAssumeAttr *TransformCXXAssumeAttr(const CXXAssumeAttr *AA);
    const LoopHintAttr *TransformLoopHintAttr(const LoopHintAttr *LH);
    const NoInlineAttr *TransformStmtNoInlineAttr(const Stmt *OrigS,
                                                  const Stmt *InstS,
                                                  const NoInlineAttr *A);
    const AlwaysInlineAttr *
    TransformStmtAlwaysInlineAttr(const Stmt *OrigS, const Stmt *InstS,
                                  const AlwaysInlineAttr *A);
    const CodeAlignAttr *TransformCodeAlignAttr(const CodeAlignAttr *CA);
    ExprResult TransformPredefinedExpr(PredefinedExpr *E);
    ExprResult TransformDeclRefExpr(DeclRefExpr *E);
    ExprResult TransformCXXDefaultArgExpr(CXXDefaultArgExpr *E);

    ExprResult TransformTemplateParmRefExpr(DeclRefExpr *E,
                                            NonTypeTemplateParmDecl *D);
    ExprResult TransformSubstNonTypeTemplateParmPackExpr(
                                           SubstNonTypeTemplateParmPackExpr *E);
    ExprResult TransformSubstNonTypeTemplateParmExpr(
                                           SubstNonTypeTemplateParmExpr *E);

    /// Rebuild a DeclRefExpr for a VarDecl reference.
    ExprResult RebuildVarDeclRefExpr(VarDecl *PD, SourceLocation Loc);

    /// Transform a reference to a function or init-capture parameter pack.
    ExprResult TransformFunctionParmPackRefExpr(DeclRefExpr *E, VarDecl *PD);

    /// Transform a FunctionParmPackExpr which was built when we couldn't
    /// expand a function parameter pack reference which refers to an expanded
    /// pack.
    ExprResult TransformFunctionParmPackExpr(FunctionParmPackExpr *E);

    QualType TransformFunctionProtoType(TypeLocBuilder &TLB,
                                        FunctionProtoTypeLoc TL) {
      // Call the base version; it will forward to our overridden version below.
      return inherited::TransformFunctionProtoType(TLB, TL);
    }

    QualType TransformInjectedClassNameType(TypeLocBuilder &TLB,
                                            InjectedClassNameTypeLoc TL) {
      auto Type = inherited::TransformInjectedClassNameType(TLB, TL);
      // Special case for transforming a deduction guide, we return a
      // transformed TemplateSpecializationType.
      if (Type.isNull() &&
          SemaRef.CodeSynthesisContexts.back().Kind ==
              Sema::CodeSynthesisContext::BuildingDeductionGuides) {
        // Return a TemplateSpecializationType for transforming a deduction
        // guide.
        if (auto *ICT = TL.getType()->getAs<InjectedClassNameType>()) {
          auto Type =
              inherited::TransformType(ICT->getInjectedSpecializationType());
          TLB.pushTrivial(SemaRef.Context, Type, TL.getNameLoc());
          return Type;
        }
      }
      return Type;
    }
    // Override the default version to handle a rewrite-template-arg-pack case
    // for building a deduction guide.
    bool TransformTemplateArgument(const TemplateArgumentLoc &Input,
                                   TemplateArgumentLoc &Output,
                                   bool Uneval = false) {
      const TemplateArgument &Arg = Input.getArgument();
      std::vector<TemplateArgument> TArgs;
      switch (Arg.getKind()) {
      case TemplateArgument::Pack:
        // Literally rewrite the template argument pack, instead of unpacking
        // it.
        for (auto &pack : Arg.getPackAsArray()) {
          TemplateArgumentLoc Input = SemaRef.getTrivialTemplateArgumentLoc(
              pack, QualType(), SourceLocation{});
          TemplateArgumentLoc Output;
          if (SemaRef.SubstTemplateArgument(Input, TemplateArgs, Output))
            return true; // fails
          TArgs.push_back(Output.getArgument());
        }
        Output = SemaRef.getTrivialTemplateArgumentLoc(
            TemplateArgument(llvm::ArrayRef(TArgs).copy(SemaRef.Context)),
            QualType(), SourceLocation{});
        return false;
      default:
        break;
      }
      return inherited::TransformTemplateArgument(Input, Output, Uneval);
    }

    template<typename Fn>
    QualType TransformFunctionProtoType(TypeLocBuilder &TLB,
                                        FunctionProtoTypeLoc TL,
                                        CXXRecordDecl *ThisContext,
                                        Qualifiers ThisTypeQuals,
                                        Fn TransformExceptionSpec);

    ParmVarDecl *
    TransformFunctionTypeParam(ParmVarDecl *OldParm, int indexAdjustment,
                               std::optional<unsigned> NumExpansions,
                               bool ExpectParameterPack);

    using inherited::TransformTemplateTypeParmType;
    /// Transforms a template type parameter type by performing
    /// substitution of the corresponding template type argument.
    QualType TransformTemplateTypeParmType(TypeLocBuilder &TLB,
                                           TemplateTypeParmTypeLoc TL,
                                           bool SuppressObjCLifetime);

    QualType BuildSubstTemplateTypeParmType(
        TypeLocBuilder &TLB, bool SuppressObjCLifetime, bool Final,
        Decl *AssociatedDecl, unsigned Index, std::optional<unsigned> PackIndex,
        TemplateArgument Arg, SourceLocation NameLoc);

    /// Transforms an already-substituted template type parameter pack
    /// into either itself (if we aren't substituting into its pack expansion)
    /// or the appropriate substituted argument.
    using inherited::TransformSubstTemplateTypeParmPackType;
    QualType
    TransformSubstTemplateTypeParmPackType(TypeLocBuilder &TLB,
                                           SubstTemplateTypeParmPackTypeLoc TL,
                                           bool SuppressObjCLifetime);

    CXXRecordDecl::LambdaDependencyKind
    ComputeLambdaDependency(LambdaScopeInfo *LSI) {
      if (auto TypeAlias =
              TemplateInstArgsHelpers::getEnclosingTypeAliasTemplateDecl(
                  getSema());
          TypeAlias && TemplateInstArgsHelpers::isLambdaEnclosedByTypeAliasDecl(
                           LSI->CallOperator, TypeAlias.PrimaryTypeAliasDecl)) {
        unsigned TypeAliasDeclDepth = TypeAlias.Template->getTemplateDepth();
        if (TypeAliasDeclDepth >= TemplateArgs.getNumSubstitutedLevels())
          return CXXRecordDecl::LambdaDependencyKind::LDK_AlwaysDependent;
        for (const TemplateArgument &TA : TypeAlias.AssociatedTemplateArguments)
          if (TA.isDependent())
            return CXXRecordDecl::LambdaDependencyKind::LDK_AlwaysDependent;
      }
      return inherited::ComputeLambdaDependency(LSI);
    }

    ExprResult TransformLambdaExpr(LambdaExpr *E) {
      LocalInstantiationScope Scope(SemaRef, /*CombineWithOuterScope=*/true);
      Sema::ConstraintEvalRAII<TemplateInstantiator> RAII(*this);

      ExprResult Result = inherited::TransformLambdaExpr(E);
      if (Result.isInvalid())
        return Result;

      CXXMethodDecl *MD = Result.getAs<LambdaExpr>()->getCallOperator();
      for (ParmVarDecl *PVD : MD->parameters()) {
        assert(PVD && "null in a parameter list");
        if (!PVD->hasDefaultArg())
          continue;
        Expr *UninstExpr = PVD->getUninstantiatedDefaultArg();
        // FIXME: Obtain the source location for the '=' token.
        SourceLocation EqualLoc = UninstExpr->getBeginLoc();
        if (SemaRef.SubstDefaultArgument(EqualLoc, PVD, TemplateArgs)) {
          // If substitution fails, the default argument is set to a
          // RecoveryExpr that wraps the uninstantiated default argument so
          // that downstream diagnostics are omitted.
          ExprResult ErrorResult = SemaRef.CreateRecoveryExpr(
              UninstExpr->getBeginLoc(), UninstExpr->getEndLoc(),
              { UninstExpr }, UninstExpr->getType());
          if (ErrorResult.isUsable())
            PVD->setDefaultArg(ErrorResult.get());
        }
      }

      return Result;
    }

    StmtResult TransformLambdaBody(LambdaExpr *E, Stmt *Body) {
      // Currently, we instantiate the body when instantiating the lambda
      // expression. However, `EvaluateConstraints` is disabled during the
      // instantiation of the lambda expression, causing the instantiation
      // failure of the return type requirement in the body. If p0588r1 is fully
      // implemented, the body will be lazily instantiated, and this problem
      // will not occur. Here, `EvaluateConstraints` is temporarily set to
      // `true` to temporarily fix this issue.
      // FIXME: This temporary fix can be removed after fully implementing
      // p0588r1.
      bool Prev = EvaluateConstraints;
      EvaluateConstraints = true;
      StmtResult Stmt = inherited::TransformLambdaBody(E, Body);
      EvaluateConstraints = Prev;
      return Stmt;
    }

    ExprResult TransformRequiresExpr(RequiresExpr *E) {
      LocalInstantiationScope Scope(SemaRef, /*CombineWithOuterScope=*/true);
      ExprResult TransReq = inherited::TransformRequiresExpr(E);
      if (TransReq.isInvalid())
        return TransReq;
      assert(TransReq.get() != E &&
             "Do not change value of isSatisfied for the existing expression. "
             "Create a new expression instead.");
      if (E->getBody()->isDependentContext()) {
        Sema::SFINAETrap Trap(SemaRef);
        // We recreate the RequiresExpr body, but not by instantiating it.
        // Produce pending diagnostics for dependent access check.
        SemaRef.PerformDependentDiagnostics(E->getBody(), TemplateArgs);
        // FIXME: Store SFINAE diagnostics in RequiresExpr for diagnosis.
        if (Trap.hasErrorOccurred())
          TransReq.getAs<RequiresExpr>()->setSatisfied(false);
      }
      return TransReq;
    }

    bool TransformRequiresExprRequirements(
        ArrayRef<concepts::Requirement *> Reqs,
        SmallVectorImpl<concepts::Requirement *> &Transformed) {
      bool SatisfactionDetermined = false;
      for (concepts::Requirement *Req : Reqs) {
        concepts::Requirement *TransReq = nullptr;
        if (!SatisfactionDetermined) {
          if (auto *TypeReq = dyn_cast<concepts::TypeRequirement>(Req))
            TransReq = TransformTypeRequirement(TypeReq);
          else if (auto *ExprReq = dyn_cast<concepts::ExprRequirement>(Req))
            TransReq = TransformExprRequirement(ExprReq);
          else
            TransReq = TransformNestedRequirement(
                cast<concepts::NestedRequirement>(Req));
          if (!TransReq)
            return true;
          if (!TransReq->isDependent() && !TransReq->isSatisfied())
            // [expr.prim.req]p6
            //   [...]  The substitution and semantic constraint checking
            //   proceeds in lexical order and stops when a condition that
            //   determines the result of the requires-expression is
            //   encountered. [..]
            SatisfactionDetermined = true;
        } else
          TransReq = Req;
        Transformed.push_back(TransReq);
      }
      return false;
    }

    TemplateParameterList *TransformTemplateParameterList(
                              TemplateParameterList *OrigTPL)  {
      if (!OrigTPL || !OrigTPL->size()) return OrigTPL;

      DeclContext *Owner = OrigTPL->getParam(0)->getDeclContext();
      TemplateDeclInstantiator  DeclInstantiator(getSema(),
                        /* DeclContext *Owner */ Owner, TemplateArgs);
      DeclInstantiator.setEvaluateConstraints(EvaluateConstraints);
      return DeclInstantiator.SubstTemplateParams(OrigTPL);
    }

    concepts::TypeRequirement *
    TransformTypeRequirement(concepts::TypeRequirement *Req);
    concepts::ExprRequirement *
    TransformExprRequirement(concepts::ExprRequirement *Req);
    concepts::NestedRequirement *
    TransformNestedRequirement(concepts::NestedRequirement *Req);
    ExprResult TransformRequiresTypeParams(
        SourceLocation KWLoc, SourceLocation RBraceLoc, const RequiresExpr *RE,
        RequiresExprBodyDecl *Body, ArrayRef<ParmVarDecl *> Params,
        SmallVectorImpl<QualType> &PTypes,
        SmallVectorImpl<ParmVarDecl *> &TransParams,
        Sema::ExtParameterInfoBuilder &PInfos);

  private:
    ExprResult
    transformNonTypeTemplateParmRef(Decl *AssociatedDecl,
                                    const NonTypeTemplateParmDecl *parm,
                                    SourceLocation loc, TemplateArgument arg,
                                    std::optional<unsigned> PackIndex);
  };
}

bool TemplateInstantiator::AlreadyTransformed(QualType T) {
  if (T.isNull())
    return true;

  if (T->isInstantiationDependentType() || T->isVariablyModifiedType())
    return false;

  getSema().MarkDeclarationsReferencedInType(Loc, T);
  return true;
}

static TemplateArgument
getPackSubstitutedTemplateArgument(Sema &S, TemplateArgument Arg) {
  assert(S.ArgumentPackSubstitutionIndex >= 0);
  assert(S.ArgumentPackSubstitutionIndex < (int)Arg.pack_size());
  Arg = Arg.pack_begin()[S.ArgumentPackSubstitutionIndex];
  if (Arg.isPackExpansion())
    Arg = Arg.getPackExpansionPattern();
  return Arg;
}

Decl *TemplateInstantiator::TransformDecl(SourceLocation Loc, Decl *D) {
  if (!D)
    return nullptr;

  if (TemplateTemplateParmDecl *TTP = dyn_cast<TemplateTemplateParmDecl>(D)) {
    if (TTP->getDepth() < TemplateArgs.getNumLevels()) {
      // If the corresponding template argument is NULL or non-existent, it's
      // because we are performing instantiation from explicitly-specified
      // template arguments in a function template, but there were some
      // arguments left unspecified.
      if (!TemplateArgs.hasTemplateArgument(TTP->getDepth(),
                                            TTP->getPosition()))
        return D;

      TemplateArgument Arg = TemplateArgs(TTP->getDepth(), TTP->getPosition());

      if (TTP->isParameterPack()) {
        assert(Arg.getKind() == TemplateArgument::Pack &&
               "Missing argument pack");
        Arg = getPackSubstitutedTemplateArgument(getSema(), Arg);
      }

      TemplateName Template = Arg.getAsTemplate();
      assert(!Template.isNull() && Template.getAsTemplateDecl() &&
             "Wrong kind of template template argument");
      return Template.getAsTemplateDecl();
    }

    // Fall through to find the instantiated declaration for this template
    // template parameter.
  }

  return SemaRef.FindInstantiatedDecl(Loc, cast<NamedDecl>(D), TemplateArgs);
}

Decl *TemplateInstantiator::TransformDefinition(SourceLocation Loc, Decl *D) {
  Decl *Inst = getSema().SubstDecl(D, getSema().CurContext, TemplateArgs);
  if (!Inst)
    return nullptr;

  getSema().CurrentInstantiationScope->InstantiatedLocal(D, Inst);
  return Inst;
}

bool TemplateInstantiator::TransformExceptionSpec(
    SourceLocation Loc, FunctionProtoType::ExceptionSpecInfo &ESI,
    SmallVectorImpl<QualType> &Exceptions, bool &Changed) {
  if (ESI.Type == EST_Uninstantiated) {
    ESI.instantiate();
    Changed = true;
  }
  return inherited::TransformExceptionSpec(Loc, ESI, Exceptions, Changed);
}

NamedDecl *
TemplateInstantiator::TransformFirstQualifierInScope(NamedDecl *D,
                                                     SourceLocation Loc) {
  // If the first part of the nested-name-specifier was a template type
  // parameter, instantiate that type parameter down to a tag type.
  if (TemplateTypeParmDecl *TTPD = dyn_cast_or_null<TemplateTypeParmDecl>(D)) {
    const TemplateTypeParmType *TTP
      = cast<TemplateTypeParmType>(getSema().Context.getTypeDeclType(TTPD));

    if (TTP->getDepth() < TemplateArgs.getNumLevels()) {
      // FIXME: This needs testing w/ member access expressions.
      TemplateArgument Arg = TemplateArgs(TTP->getDepth(), TTP->getIndex());

      if (TTP->isParameterPack()) {
        assert(Arg.getKind() == TemplateArgument::Pack &&
               "Missing argument pack");

        if (getSema().ArgumentPackSubstitutionIndex == -1)
          return nullptr;

        Arg = getPackSubstitutedTemplateArgument(getSema(), Arg);
      }

      QualType T = Arg.getAsType();
      if (T.isNull())
        return cast_or_null<NamedDecl>(TransformDecl(Loc, D));

      if (const TagType *Tag = T->getAs<TagType>())
        return Tag->getDecl();

      // The resulting type is not a tag; complain.
      getSema().Diag(Loc, diag::err_nested_name_spec_non_tag) << T;
      return nullptr;
    }
  }

  return cast_or_null<NamedDecl>(TransformDecl(Loc, D));
}

VarDecl *
TemplateInstantiator::RebuildExceptionDecl(VarDecl *ExceptionDecl,
                                           TypeSourceInfo *Declarator,
                                           SourceLocation StartLoc,
                                           SourceLocation NameLoc,
                                           IdentifierInfo *Name) {
  VarDecl *Var = inherited::RebuildExceptionDecl(ExceptionDecl, Declarator,
                                                 StartLoc, NameLoc, Name);
  if (Var)
    getSema().CurrentInstantiationScope->InstantiatedLocal(ExceptionDecl, Var);
  return Var;
}

VarDecl *TemplateInstantiator::RebuildObjCExceptionDecl(VarDecl *ExceptionDecl,
                                                        TypeSourceInfo *TSInfo,
                                                        QualType T) {
  VarDecl *Var = inherited::RebuildObjCExceptionDecl(ExceptionDecl, TSInfo, T);
  if (Var)
    getSema().CurrentInstantiationScope->InstantiatedLocal(ExceptionDecl, Var);
  return Var;
}

QualType
TemplateInstantiator::RebuildElaboratedType(SourceLocation KeywordLoc,
                                            ElaboratedTypeKeyword Keyword,
                                            NestedNameSpecifierLoc QualifierLoc,
                                            QualType T) {
  if (const TagType *TT = T->getAs<TagType>()) {
    TagDecl* TD = TT->getDecl();

    SourceLocation TagLocation = KeywordLoc;

    IdentifierInfo *Id = TD->getIdentifier();

    // TODO: should we even warn on struct/class mismatches for this?  Seems
    // like it's likely to produce a lot of spurious errors.
    if (Id && Keyword != ElaboratedTypeKeyword::None &&
        Keyword != ElaboratedTypeKeyword::Typename) {
      TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForKeyword(Keyword);
      if (!SemaRef.isAcceptableTagRedeclaration(TD, Kind, /*isDefinition*/false,
                                                TagLocation, Id)) {
        SemaRef.Diag(TagLocation, diag::err_use_with_wrong_tag)
          << Id
          << FixItHint::CreateReplacement(SourceRange(TagLocation),
                                          TD->getKindName());
        SemaRef.Diag(TD->getLocation(), diag::note_previous_use);
      }
    }
  }

  return inherited::RebuildElaboratedType(KeywordLoc, Keyword, QualifierLoc, T);
}

TemplateName TemplateInstantiator::TransformTemplateName(
    CXXScopeSpec &SS, TemplateName Name, SourceLocation NameLoc,
    QualType ObjectType, NamedDecl *FirstQualifierInScope,
    bool AllowInjectedClassName) {
  if (TemplateTemplateParmDecl *TTP
       = dyn_cast_or_null<TemplateTemplateParmDecl>(Name.getAsTemplateDecl())) {
    if (TTP->getDepth() < TemplateArgs.getNumLevels()) {
      // If the corresponding template argument is NULL or non-existent, it's
      // because we are performing instantiation from explicitly-specified
      // template arguments in a function template, but there were some
      // arguments left unspecified.
      if (!TemplateArgs.hasTemplateArgument(TTP->getDepth(),
                                            TTP->getPosition()))
        return Name;

      TemplateArgument Arg = TemplateArgs(TTP->getDepth(), TTP->getPosition());

      if (TemplateArgs.isRewrite()) {
        // We're rewriting the template parameter as a reference to another
        // template parameter.
        if (Arg.getKind() == TemplateArgument::Pack) {
          assert(Arg.pack_size() == 1 && Arg.pack_begin()->isPackExpansion() &&
                 "unexpected pack arguments in template rewrite");
          Arg = Arg.pack_begin()->getPackExpansionPattern();
        }
        assert(Arg.getKind() == TemplateArgument::Template &&
               "unexpected nontype template argument kind in template rewrite");
        return Arg.getAsTemplate();
      }

      auto [AssociatedDecl, Final] =
          TemplateArgs.getAssociatedDecl(TTP->getDepth());
      std::optional<unsigned> PackIndex;
      if (TTP->isParameterPack()) {
        assert(Arg.getKind() == TemplateArgument::Pack &&
               "Missing argument pack");

        if (getSema().ArgumentPackSubstitutionIndex == -1) {
          // We have the template argument pack to substitute, but we're not
          // actually expanding the enclosing pack expansion yet. So, just
          // keep the entire argument pack.
          return getSema().Context.getSubstTemplateTemplateParmPack(
              Arg, AssociatedDecl, TTP->getIndex(), Final);
        }

        PackIndex = getPackIndex(Arg);
        Arg = getPackSubstitutedTemplateArgument(getSema(), Arg);
      }

      TemplateName Template = Arg.getAsTemplate();
      assert(!Template.isNull() && "Null template template argument");

      if (Final)
        return Template;
      return getSema().Context.getSubstTemplateTemplateParm(
          Template, AssociatedDecl, TTP->getIndex(), PackIndex);
    }
  }

  if (SubstTemplateTemplateParmPackStorage *SubstPack
      = Name.getAsSubstTemplateTemplateParmPack()) {
    if (getSema().ArgumentPackSubstitutionIndex == -1)
      return Name;

    TemplateArgument Pack = SubstPack->getArgumentPack();
    TemplateName Template =
        getPackSubstitutedTemplateArgument(getSema(), Pack).getAsTemplate();
    if (SubstPack->getFinal())
      return Template;
    return getSema().Context.getSubstTemplateTemplateParm(
        Template, SubstPack->getAssociatedDecl(), SubstPack->getIndex(),
        getPackIndex(Pack));
  }

  return inherited::TransformTemplateName(SS, Name, NameLoc, ObjectType,
                                          FirstQualifierInScope,
                                          AllowInjectedClassName);
}

ExprResult
TemplateInstantiator::TransformPredefinedExpr(PredefinedExpr *E) {
  if (!E->isTypeDependent())
    return E;

  return getSema().BuildPredefinedExpr(E->getLocation(), E->getIdentKind());
}

ExprResult
TemplateInstantiator::TransformTemplateParmRefExpr(DeclRefExpr *E,
                                               NonTypeTemplateParmDecl *NTTP) {
  // If the corresponding template argument is NULL or non-existent, it's
  // because we are performing instantiation from explicitly-specified
  // template arguments in a function template, but there were some
  // arguments left unspecified.
  if (!TemplateArgs.hasTemplateArgument(NTTP->getDepth(),
                                        NTTP->getPosition()))
    return E;

  TemplateArgument Arg = TemplateArgs(NTTP->getDepth(), NTTP->getPosition());

  if (TemplateArgs.isRewrite()) {
    // We're rewriting the template parameter as a reference to another
    // template parameter.
    if (Arg.getKind() == TemplateArgument::Pack) {
      assert(Arg.pack_size() == 1 && Arg.pack_begin()->isPackExpansion() &&
             "unexpected pack arguments in template rewrite");
      Arg = Arg.pack_begin()->getPackExpansionPattern();
    }
    assert(Arg.getKind() == TemplateArgument::Expression &&
           "unexpected nontype template argument kind in template rewrite");
    // FIXME: This can lead to the same subexpression appearing multiple times
    // in a complete expression.
    return Arg.getAsExpr();
  }

  auto [AssociatedDecl, _] = TemplateArgs.getAssociatedDecl(NTTP->getDepth());
  std::optional<unsigned> PackIndex;
  if (NTTP->isParameterPack()) {
    assert(Arg.getKind() == TemplateArgument::Pack &&
           "Missing argument pack");

    if (getSema().ArgumentPackSubstitutionIndex == -1) {
      // We have an argument pack, but we can't select a particular argument
      // out of it yet. Therefore, we'll build an expression to hold on to that
      // argument pack.
      QualType TargetType = SemaRef.SubstType(NTTP->getType(), TemplateArgs,
                                              E->getLocation(),
                                              NTTP->getDeclName());
      if (TargetType.isNull())
        return ExprError();

      QualType ExprType = TargetType.getNonLValueExprType(SemaRef.Context);
      if (TargetType->isRecordType())
        ExprType.addConst();
      // FIXME: Pass in Final.
      return new (SemaRef.Context) SubstNonTypeTemplateParmPackExpr(
          ExprType, TargetType->isReferenceType() ? VK_LValue : VK_PRValue,
          E->getLocation(), Arg, AssociatedDecl, NTTP->getPosition());
    }
    PackIndex = getPackIndex(Arg);
    Arg = getPackSubstitutedTemplateArgument(getSema(), Arg);
  }
  // FIXME: Don't put subst node on Final replacement.
  return transformNonTypeTemplateParmRef(AssociatedDecl, NTTP, E->getLocation(),
                                         Arg, PackIndex);
}

const CXXAssumeAttr *
TemplateInstantiator::TransformCXXAssumeAttr(const CXXAssumeAttr *AA) {
  ExprResult Res = getDerived().TransformExpr(AA->getAssumption());
  if (!Res.isUsable())
    return AA;

  Res = getSema().BuildCXXAssumeExpr(Res.get(), AA->getAttrName(),
                                     AA->getRange());
  if (!Res.isUsable())
    return AA;

  return CXXAssumeAttr::CreateImplicit(getSema().Context, Res.get(),
                                       AA->getRange());
}

const LoopHintAttr *
TemplateInstantiator::TransformLoopHintAttr(const LoopHintAttr *LH) {
  Expr *TransformedExpr = getDerived().TransformExpr(LH->getValue()).get();

  if (TransformedExpr == LH->getValue())
    return LH;

  // Generate error if there is a problem with the value.
  if (getSema().CheckLoopHintExpr(TransformedExpr, LH->getLocation(),
                                  LH->getSemanticSpelling() ==
                                      LoopHintAttr::Pragma_unroll))
    return LH;

  LoopHintAttr::OptionType Option = LH->getOption();
  LoopHintAttr::LoopHintState State = LH->getState();

  llvm::APSInt ValueAPS =
      TransformedExpr->EvaluateKnownConstInt(getSema().getASTContext());
  // The values of 0 and 1 block any unrolling of the loop.
  if (ValueAPS.isZero() || ValueAPS.isOne()) {
    Option = LoopHintAttr::Unroll;
    State = LoopHintAttr::Disable;
  }

  // Create new LoopHintValueAttr with integral expression in place of the
  // non-type template parameter.
  return LoopHintAttr::CreateImplicit(getSema().Context, Option, State,
                                      TransformedExpr, *LH);
}
const NoInlineAttr *TemplateInstantiator::TransformStmtNoInlineAttr(
    const Stmt *OrigS, const Stmt *InstS, const NoInlineAttr *A) {
  if (!A || getSema().CheckNoInlineAttr(OrigS, InstS, *A))
    return nullptr;

  return A;
}
const AlwaysInlineAttr *TemplateInstantiator::TransformStmtAlwaysInlineAttr(
    const Stmt *OrigS, const Stmt *InstS, const AlwaysInlineAttr *A) {
  if (!A || getSema().CheckAlwaysInlineAttr(OrigS, InstS, *A))
    return nullptr;

  return A;
}

const CodeAlignAttr *
TemplateInstantiator::TransformCodeAlignAttr(const CodeAlignAttr *CA) {
  Expr *TransformedExpr = getDerived().TransformExpr(CA->getAlignment()).get();
  return getSema().BuildCodeAlignAttr(*CA, TransformedExpr);
}

ExprResult TemplateInstantiator::transformNonTypeTemplateParmRef(
    Decl *AssociatedDecl, const NonTypeTemplateParmDecl *parm,
    SourceLocation loc, TemplateArgument arg,
    std::optional<unsigned> PackIndex) {
  ExprResult result;

  // Determine the substituted parameter type. We can usually infer this from
  // the template argument, but not always.
  auto SubstParamType = [&] {
    QualType T;
    if (parm->isExpandedParameterPack())
      T = parm->getExpansionType(SemaRef.ArgumentPackSubstitutionIndex);
    else
      T = parm->getType();
    if (parm->isParameterPack() && isa<PackExpansionType>(T))
      T = cast<PackExpansionType>(T)->getPattern();
    return SemaRef.SubstType(T, TemplateArgs, loc, parm->getDeclName());
  };

  bool refParam = false;

  // The template argument itself might be an expression, in which case we just
  // return that expression. This happens when substituting into an alias
  // template.
  if (arg.getKind() == TemplateArgument::Expression) {
    Expr *argExpr = arg.getAsExpr();
    result = argExpr;
    if (argExpr->isLValue()) {
      if (argExpr->getType()->isRecordType()) {
        // Check whether the parameter was actually a reference.
        QualType paramType = SubstParamType();
        if (paramType.isNull())
          return ExprError();
        refParam = paramType->isReferenceType();
      } else {
        refParam = true;
      }
    }
  } else if (arg.getKind() == TemplateArgument::Declaration ||
             arg.getKind() == TemplateArgument::NullPtr) {
    if (arg.getKind() == TemplateArgument::Declaration) {
      ValueDecl *VD = arg.getAsDecl();

      // Find the instantiation of the template argument.  This is
      // required for nested templates.
      VD = cast_or_null<ValueDecl>(
             getSema().FindInstantiatedDecl(loc, VD, TemplateArgs));
      if (!VD)
        return ExprError();
    }

    QualType paramType = arg.getNonTypeTemplateArgumentType();
    assert(!paramType.isNull() && "type substitution failed for param type");
    assert(!paramType->isDependentType() && "param type still dependent");
    result = SemaRef.BuildExpressionFromDeclTemplateArgument(arg, paramType, loc);
    refParam = paramType->isReferenceType();
  } else {
    QualType paramType = arg.getNonTypeTemplateArgumentType();
    result = SemaRef.BuildExpressionFromNonTypeTemplateArgument(arg, loc);
    refParam = paramType->isReferenceType();
    assert(result.isInvalid() ||
           SemaRef.Context.hasSameType(result.get()->getType(),
                                       paramType.getNonReferenceType()));
  }

  if (result.isInvalid())
    return ExprError();

  Expr *resultExpr = result.get();
  // FIXME: Don't put subst node on final replacement.
  return new (SemaRef.Context) SubstNonTypeTemplateParmExpr(
      resultExpr->getType(), resultExpr->getValueKind(), loc, resultExpr,
      AssociatedDecl, parm->getIndex(), PackIndex, refParam);
}

ExprResult
TemplateInstantiator::TransformSubstNonTypeTemplateParmPackExpr(
                                          SubstNonTypeTemplateParmPackExpr *E) {
  if (getSema().ArgumentPackSubstitutionIndex == -1) {
    // We aren't expanding the parameter pack, so just return ourselves.
    return E;
  }

  TemplateArgument Pack = E->getArgumentPack();
  TemplateArgument Arg = getPackSubstitutedTemplateArgument(getSema(), Pack);
  // FIXME: Don't put subst node on final replacement.
  return transformNonTypeTemplateParmRef(
      E->getAssociatedDecl(), E->getParameterPack(),
      E->getParameterPackLocation(), Arg, getPackIndex(Pack));
}

ExprResult
TemplateInstantiator::TransformSubstNonTypeTemplateParmExpr(
                                          SubstNonTypeTemplateParmExpr *E) {
  ExprResult SubstReplacement = E->getReplacement();
  if (!isa<ConstantExpr>(SubstReplacement.get()))
    SubstReplacement = TransformExpr(E->getReplacement());
  if (SubstReplacement.isInvalid())
    return true;
  QualType SubstType = TransformType(E->getParameterType(getSema().Context));
  if (SubstType.isNull())
    return true;
  // The type may have been previously dependent and not now, which means we
  // might have to implicit cast the argument to the new type, for example:
  // template<auto T, decltype(T) U>
  // concept C = sizeof(U) == 4;
  // void foo() requires C<2, 'a'> { }
  // When normalizing foo(), we first form the normalized constraints of C:
  // AtomicExpr(sizeof(U) == 4,
  //            U=SubstNonTypeTemplateParmExpr(Param=U,
  //                                           Expr=DeclRef(U),
  //                                           Type=decltype(T)))
  // Then we substitute T = 2, U = 'a' into the parameter mapping, and need to
  // produce:
  // AtomicExpr(sizeof(U) == 4,
  //            U=SubstNonTypeTemplateParmExpr(Param=U,
  //                                           Expr=ImpCast(
  //                                               decltype(2),
  //                                               SubstNTTPE(Param=U, Expr='a',
  //                                                          Type=char)),
  //                                           Type=decltype(2)))
  // The call to CheckTemplateArgument here produces the ImpCast.
  TemplateArgument SugaredConverted, CanonicalConverted;
  if (SemaRef
          .CheckTemplateArgument(E->getParameter(), SubstType,
                                 SubstReplacement.get(), SugaredConverted,
                                 CanonicalConverted, Sema::CTAK_Specified)
          .isInvalid())
    return true;
  return transformNonTypeTemplateParmRef(E->getAssociatedDecl(),
                                         E->getParameter(), E->getExprLoc(),
                                         SugaredConverted, E->getPackIndex());
}

ExprResult TemplateInstantiator::RebuildVarDeclRefExpr(VarDecl *PD,
                                                       SourceLocation Loc) {
  DeclarationNameInfo NameInfo(PD->getDeclName(), Loc);
  return getSema().BuildDeclarationNameExpr(CXXScopeSpec(), NameInfo, PD);
}

ExprResult
TemplateInstantiator::TransformFunctionParmPackExpr(FunctionParmPackExpr *E) {
  if (getSema().ArgumentPackSubstitutionIndex != -1) {
    // We can expand this parameter pack now.
    VarDecl *D = E->getExpansion(getSema().ArgumentPackSubstitutionIndex);
    VarDecl *VD = cast_or_null<VarDecl>(TransformDecl(E->getExprLoc(), D));
    if (!VD)
      return ExprError();
    return RebuildVarDeclRefExpr(VD, E->getExprLoc());
  }

  QualType T = TransformType(E->getType());
  if (T.isNull())
    return ExprError();

  // Transform each of the parameter expansions into the corresponding
  // parameters in the instantiation of the function decl.
  SmallVector<VarDecl *, 8> Vars;
  Vars.reserve(E->getNumExpansions());
  for (FunctionParmPackExpr::iterator I = E->begin(), End = E->end();
       I != End; ++I) {
    VarDecl *D = cast_or_null<VarDecl>(TransformDecl(E->getExprLoc(), *I));
    if (!D)
      return ExprError();
    Vars.push_back(D);
  }

  auto *PackExpr =
      FunctionParmPackExpr::Create(getSema().Context, T, E->getParameterPack(),
                                   E->getParameterPackLocation(), Vars);
  getSema().MarkFunctionParmPackReferenced(PackExpr);
  return PackExpr;
}

ExprResult
TemplateInstantiator::TransformFunctionParmPackRefExpr(DeclRefExpr *E,
                                                       VarDecl *PD) {
  typedef LocalInstantiationScope::DeclArgumentPack DeclArgumentPack;
  llvm::PointerUnion<Decl *, DeclArgumentPack *> *Found
    = getSema().CurrentInstantiationScope->findInstantiationOf(PD);
  assert(Found && "no instantiation for parameter pack");

  Decl *TransformedDecl;
  if (DeclArgumentPack *Pack = Found->dyn_cast<DeclArgumentPack *>()) {
    // If this is a reference to a function parameter pack which we can
    // substitute but can't yet expand, build a FunctionParmPackExpr for it.
    if (getSema().ArgumentPackSubstitutionIndex == -1) {
      QualType T = TransformType(E->getType());
      if (T.isNull())
        return ExprError();
      auto *PackExpr = FunctionParmPackExpr::Create(getSema().Context, T, PD,
                                                    E->getExprLoc(), *Pack);
      getSema().MarkFunctionParmPackReferenced(PackExpr);
      return PackExpr;
    }

    TransformedDecl = (*Pack)[getSema().ArgumentPackSubstitutionIndex];
  } else {
    TransformedDecl = Found->get<Decl*>();
  }

  // We have either an unexpanded pack or a specific expansion.
  return RebuildVarDeclRefExpr(cast<VarDecl>(TransformedDecl), E->getExprLoc());
}

ExprResult
TemplateInstantiator::TransformDeclRefExpr(DeclRefExpr *E) {
  NamedDecl *D = E->getDecl();

  // Handle references to non-type template parameters and non-type template
  // parameter packs.
  if (NonTypeTemplateParmDecl *NTTP = dyn_cast<NonTypeTemplateParmDecl>(D)) {
    if (NTTP->getDepth() < TemplateArgs.getNumLevels())
      return TransformTemplateParmRefExpr(E, NTTP);

    // We have a non-type template parameter that isn't fully substituted;
    // FindInstantiatedDecl will find it in the local instantiation scope.
  }

  // Handle references to function parameter packs.
  if (VarDecl *PD = dyn_cast<VarDecl>(D))
    if (PD->isParameterPack())
      return TransformFunctionParmPackRefExpr(E, PD);

  return inherited::TransformDeclRefExpr(E);
}

ExprResult TemplateInstantiator::TransformCXXDefaultArgExpr(
    CXXDefaultArgExpr *E) {
  assert(!cast<FunctionDecl>(E->getParam()->getDeclContext())->
             getDescribedFunctionTemplate() &&
         "Default arg expressions are never formed in dependent cases.");
  return SemaRef.BuildCXXDefaultArgExpr(
      E->getUsedLocation(), cast<FunctionDecl>(E->getParam()->getDeclContext()),
      E->getParam());
}

template<typename Fn>
QualType TemplateInstantiator::TransformFunctionProtoType(TypeLocBuilder &TLB,
                                 FunctionProtoTypeLoc TL,
                                 CXXRecordDecl *ThisContext,
                                 Qualifiers ThisTypeQuals,
                                 Fn TransformExceptionSpec) {
  // We need a local instantiation scope for this function prototype.
  LocalInstantiationScope Scope(SemaRef, /*CombineWithOuterScope=*/true);
  return inherited::TransformFunctionProtoType(
      TLB, TL, ThisContext, ThisTypeQuals, TransformExceptionSpec);
}

ParmVarDecl *TemplateInstantiator::TransformFunctionTypeParam(
    ParmVarDecl *OldParm, int indexAdjustment,
    std::optional<unsigned> NumExpansions, bool ExpectParameterPack) {
  auto NewParm = SemaRef.SubstParmVarDecl(
      OldParm, TemplateArgs, indexAdjustment, NumExpansions,
      ExpectParameterPack, EvaluateConstraints);
  if (NewParm && SemaRef.getLangOpts().OpenCL)
    SemaRef.deduceOpenCLAddressSpace(NewParm);
  return NewParm;
}

QualType TemplateInstantiator::BuildSubstTemplateTypeParmType(
    TypeLocBuilder &TLB, bool SuppressObjCLifetime, bool Final,
    Decl *AssociatedDecl, unsigned Index, std::optional<unsigned> PackIndex,
    TemplateArgument Arg, SourceLocation NameLoc) {
  QualType Replacement = Arg.getAsType();

  // If the template parameter had ObjC lifetime qualifiers,
  // then any such qualifiers on the replacement type are ignored.
  if (SuppressObjCLifetime) {
    Qualifiers RQs;
    RQs = Replacement.getQualifiers();
    RQs.removeObjCLifetime();
    Replacement =
        SemaRef.Context.getQualifiedType(Replacement.getUnqualifiedType(), RQs);
  }

  if (Final) {
    TLB.pushTrivial(SemaRef.Context, Replacement, NameLoc);
    return Replacement;
  }
  // TODO: only do this uniquing once, at the start of instantiation.
  QualType Result = getSema().Context.getSubstTemplateTypeParmType(
      Replacement, AssociatedDecl, Index, PackIndex);
  SubstTemplateTypeParmTypeLoc NewTL =
      TLB.push<SubstTemplateTypeParmTypeLoc>(Result);
  NewTL.setNameLoc(NameLoc);
  return Result;
}

QualType
TemplateInstantiator::TransformTemplateTypeParmType(TypeLocBuilder &TLB,
                                                    TemplateTypeParmTypeLoc TL,
                                                    bool SuppressObjCLifetime) {
  const TemplateTypeParmType *T = TL.getTypePtr();
  if (T->getDepth() < TemplateArgs.getNumLevels()) {
    // Replace the template type parameter with its corresponding
    // template argument.

    // If the corresponding template argument is NULL or doesn't exist, it's
    // because we are performing instantiation from explicitly-specified
    // template arguments in a function template class, but there were some
    // arguments left unspecified.
    if (!TemplateArgs.hasTemplateArgument(T->getDepth(), T->getIndex())) {
      TemplateTypeParmTypeLoc NewTL
        = TLB.push<TemplateTypeParmTypeLoc>(TL.getType());
      NewTL.setNameLoc(TL.getNameLoc());
      return TL.getType();
    }

    TemplateArgument Arg = TemplateArgs(T->getDepth(), T->getIndex());

    if (TemplateArgs.isRewrite()) {
      // We're rewriting the template parameter as a reference to another
      // template parameter.
      if (Arg.getKind() == TemplateArgument::Pack) {
        assert(Arg.pack_size() == 1 && Arg.pack_begin()->isPackExpansion() &&
               "unexpected pack arguments in template rewrite");
        Arg = Arg.pack_begin()->getPackExpansionPattern();
      }
      assert(Arg.getKind() == TemplateArgument::Type &&
             "unexpected nontype template argument kind in template rewrite");
      QualType NewT = Arg.getAsType();
      TLB.pushTrivial(SemaRef.Context, NewT, TL.getNameLoc());
      return NewT;
    }

    auto [AssociatedDecl, Final] =
        TemplateArgs.getAssociatedDecl(T->getDepth());
    std::optional<unsigned> PackIndex;
    if (T->isParameterPack()) {
      assert(Arg.getKind() == TemplateArgument::Pack &&
             "Missing argument pack");

      if (getSema().ArgumentPackSubstitutionIndex == -1) {
        // We have the template argument pack, but we're not expanding the
        // enclosing pack expansion yet. Just save the template argument
        // pack for later substitution.
        QualType Result = getSema().Context.getSubstTemplateTypeParmPackType(
            AssociatedDecl, T->getIndex(), Final, Arg);
        SubstTemplateTypeParmPackTypeLoc NewTL
          = TLB.push<SubstTemplateTypeParmPackTypeLoc>(Result);
        NewTL.setNameLoc(TL.getNameLoc());
        return Result;
      }

      // PackIndex starts from last element.
      PackIndex = getPackIndex(Arg);
      Arg = getPackSubstitutedTemplateArgument(getSema(), Arg);
    }

    assert(Arg.getKind() == TemplateArgument::Type &&
           "Template argument kind mismatch");

    return BuildSubstTemplateTypeParmType(TLB, SuppressObjCLifetime, Final,
                                          AssociatedDecl, T->getIndex(),
                                          PackIndex, Arg, TL.getNameLoc());
  }

  // The template type parameter comes from an inner template (e.g.,
  // the template parameter list of a member template inside the
  // template we are instantiating). Create a new template type
  // parameter with the template "level" reduced by one.
  TemplateTypeParmDecl *NewTTPDecl = nullptr;
  if (TemplateTypeParmDecl *OldTTPDecl = T->getDecl())
    NewTTPDecl = cast_or_null<TemplateTypeParmDecl>(
        TransformDecl(TL.getNameLoc(), OldTTPDecl));
  QualType Result = getSema().Context.getTemplateTypeParmType(
      T->getDepth() - TemplateArgs.getNumSubstitutedLevels(), T->getIndex(),
      T->isParameterPack(), NewTTPDecl);
  TemplateTypeParmTypeLoc NewTL = TLB.push<TemplateTypeParmTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());
  return Result;
}

QualType TemplateInstantiator::TransformSubstTemplateTypeParmPackType(
    TypeLocBuilder &TLB, SubstTemplateTypeParmPackTypeLoc TL,
    bool SuppressObjCLifetime) {
  const SubstTemplateTypeParmPackType *T = TL.getTypePtr();

  Decl *NewReplaced = TransformDecl(TL.getNameLoc(), T->getAssociatedDecl());

  if (getSema().ArgumentPackSubstitutionIndex == -1) {
    // We aren't expanding the parameter pack, so just return ourselves.
    QualType Result = TL.getType();
    if (NewReplaced != T->getAssociatedDecl())
      Result = getSema().Context.getSubstTemplateTypeParmPackType(
          NewReplaced, T->getIndex(), T->getFinal(), T->getArgumentPack());
    SubstTemplateTypeParmPackTypeLoc NewTL =
        TLB.push<SubstTemplateTypeParmPackTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
    return Result;
  }

  TemplateArgument Pack = T->getArgumentPack();
  TemplateArgument Arg = getPackSubstitutedTemplateArgument(getSema(), Pack);
  return BuildSubstTemplateTypeParmType(
      TLB, SuppressObjCLifetime, T->getFinal(), NewReplaced, T->getIndex(),
      getPackIndex(Pack), Arg, TL.getNameLoc());
}

static concepts::Requirement::SubstitutionDiagnostic *
createSubstDiag(Sema &S, TemplateDeductionInfo &Info,
                concepts::EntityPrinter Printer) {
  SmallString<128> Message;
  SourceLocation ErrorLoc;
  if (Info.hasSFINAEDiagnostic()) {
    PartialDiagnosticAt PDA(SourceLocation(),
                            PartialDiagnostic::NullDiagnostic{});
    Info.takeSFINAEDiagnostic(PDA);
    PDA.second.EmitToString(S.getDiagnostics(), Message);
    ErrorLoc = PDA.first;
  } else {
    ErrorLoc = Info.getLocation();
  }
  SmallString<128> Entity;
  llvm::raw_svector_ostream OS(Entity);
  Printer(OS);
  const ASTContext &C = S.Context;
  return new (C) concepts::Requirement::SubstitutionDiagnostic{
      C.backupStr(Entity), ErrorLoc, C.backupStr(Message)};
}

concepts::Requirement::SubstitutionDiagnostic *
concepts::createSubstDiagAt(Sema &S, SourceLocation Location,
                            EntityPrinter Printer) {
  SmallString<128> Entity;
  llvm::raw_svector_ostream OS(Entity);
  Printer(OS);
  const ASTContext &C = S.Context;
  return new (C) concepts::Requirement::SubstitutionDiagnostic{
      /*SubstitutedEntity=*/C.backupStr(Entity),
      /*DiagLoc=*/Location, /*DiagMessage=*/StringRef()};
}

ExprResult TemplateInstantiator::TransformRequiresTypeParams(
    SourceLocation KWLoc, SourceLocation RBraceLoc, const RequiresExpr *RE,
    RequiresExprBodyDecl *Body, ArrayRef<ParmVarDecl *> Params,
    SmallVectorImpl<QualType> &PTypes,
    SmallVectorImpl<ParmVarDecl *> &TransParams,
    Sema::ExtParameterInfoBuilder &PInfos) {

  TemplateDeductionInfo Info(KWLoc);
  Sema::InstantiatingTemplate TypeInst(SemaRef, KWLoc,
                                       RE, Info,
                                       SourceRange{KWLoc, RBraceLoc});
  Sema::SFINAETrap Trap(SemaRef);

  unsigned ErrorIdx;
  if (getDerived().TransformFunctionTypeParams(
          KWLoc, Params, /*ParamTypes=*/nullptr, /*ParamInfos=*/nullptr, PTypes,
          &TransParams, PInfos, &ErrorIdx) ||
      Trap.hasErrorOccurred()) {
    SmallVector<concepts::Requirement *, 4> TransReqs;
    ParmVarDecl *FailedDecl = Params[ErrorIdx];
    // Add a 'failed' Requirement to contain the error that caused the failure
    // here.
    TransReqs.push_back(RebuildTypeRequirement(createSubstDiag(
        SemaRef, Info, [&](llvm::raw_ostream &OS) { OS << *FailedDecl; })));
    return getDerived().RebuildRequiresExpr(KWLoc, Body, RE->getLParenLoc(),
                                            TransParams, RE->getRParenLoc(),
                                            TransReqs, RBraceLoc);
  }

  return ExprResult{};
}

concepts::TypeRequirement *
TemplateInstantiator::TransformTypeRequirement(concepts::TypeRequirement *Req) {
  if (!Req->isDependent() && !AlwaysRebuild())
    return Req;
  if (Req->isSubstitutionFailure()) {
    if (AlwaysRebuild())
      return RebuildTypeRequirement(
              Req->getSubstitutionDiagnostic());
    return Req;
  }

  Sema::SFINAETrap Trap(SemaRef);
  TemplateDeductionInfo Info(Req->getType()->getTypeLoc().getBeginLoc());
  Sema::InstantiatingTemplate TypeInst(SemaRef,
      Req->getType()->getTypeLoc().getBeginLoc(), Req, Info,
      Req->getType()->getTypeLoc().getSourceRange());
  if (TypeInst.isInvalid())
    return nullptr;
  TypeSourceInfo *TransType = TransformType(Req->getType());
  if (!TransType || Trap.hasErrorOccurred())
    return RebuildTypeRequirement(createSubstDiag(SemaRef, Info,
        [&] (llvm::raw_ostream& OS) {
            Req->getType()->getType().print(OS, SemaRef.getPrintingPolicy());
        }));
  return RebuildTypeRequirement(TransType);
}

concepts::ExprRequirement *
TemplateInstantiator::TransformExprRequirement(concepts::ExprRequirement *Req) {
  if (!Req->isDependent() && !AlwaysRebuild())
    return Req;

  Sema::SFINAETrap Trap(SemaRef);

  llvm::PointerUnion<Expr *, concepts::Requirement::SubstitutionDiagnostic *>
      TransExpr;
  if (Req->isExprSubstitutionFailure())
    TransExpr = Req->getExprSubstitutionDiagnostic();
  else {
    Expr *E = Req->getExpr();
    TemplateDeductionInfo Info(E->getBeginLoc());
    Sema::InstantiatingTemplate ExprInst(SemaRef, E->getBeginLoc(), Req, Info,
                                         E->getSourceRange());
    if (ExprInst.isInvalid())
      return nullptr;
    ExprResult TransExprRes = TransformExpr(E);
    if (!TransExprRes.isInvalid() && !Trap.hasErrorOccurred() &&
        TransExprRes.get()->hasPlaceholderType())
      TransExprRes = SemaRef.CheckPlaceholderExpr(TransExprRes.get());
    if (TransExprRes.isInvalid() || Trap.hasErrorOccurred())
      TransExpr = createSubstDiag(SemaRef, Info, [&](llvm::raw_ostream &OS) {
        E->printPretty(OS, nullptr, SemaRef.getPrintingPolicy());
      });
    else
      TransExpr = TransExprRes.get();
  }

  std::optional<concepts::ExprRequirement::ReturnTypeRequirement> TransRetReq;
  const auto &RetReq = Req->getReturnTypeRequirement();
  if (RetReq.isEmpty())
    TransRetReq.emplace();
  else if (RetReq.isSubstitutionFailure())
    TransRetReq.emplace(RetReq.getSubstitutionDiagnostic());
  else if (RetReq.isTypeConstraint()) {
    TemplateParameterList *OrigTPL =
        RetReq.getTypeConstraintTemplateParameterList();
    TemplateDeductionInfo Info(OrigTPL->getTemplateLoc());
    Sema::InstantiatingTemplate TPLInst(SemaRef, OrigTPL->getTemplateLoc(),
                                        Req, Info, OrigTPL->getSourceRange());
    if (TPLInst.isInvalid())
      return nullptr;
    TemplateParameterList *TPL = TransformTemplateParameterList(OrigTPL);
    if (!TPL || Trap.hasErrorOccurred())
      TransRetReq.emplace(createSubstDiag(SemaRef, Info,
          [&] (llvm::raw_ostream& OS) {
              RetReq.getTypeConstraint()->getImmediatelyDeclaredConstraint()
                  ->printPretty(OS, nullptr, SemaRef.getPrintingPolicy());
          }));
    else {
      TPLInst.Clear();
      TransRetReq.emplace(TPL);
    }
  }
  assert(TransRetReq && "All code paths leading here must set TransRetReq");
  if (Expr *E = TransExpr.dyn_cast<Expr *>())
    return RebuildExprRequirement(E, Req->isSimple(), Req->getNoexceptLoc(),
                                  std::move(*TransRetReq));
  return RebuildExprRequirement(
      TransExpr.get<concepts::Requirement::SubstitutionDiagnostic *>(),
      Req->isSimple(), Req->getNoexceptLoc(), std::move(*TransRetReq));
}

concepts::NestedRequirement *
TemplateInstantiator::TransformNestedRequirement(
    concepts::NestedRequirement *Req) {
  if (!Req->isDependent() && !AlwaysRebuild())
    return Req;
  if (Req->hasInvalidConstraint()) {
    if (AlwaysRebuild())
      return RebuildNestedRequirement(Req->getInvalidConstraintEntity(),
                                      Req->getConstraintSatisfaction());
    return Req;
  }
  Sema::InstantiatingTemplate ReqInst(SemaRef,
      Req->getConstraintExpr()->getBeginLoc(), Req,
      Sema::InstantiatingTemplate::ConstraintsCheck{},
      Req->getConstraintExpr()->getSourceRange());
  if (!getEvaluateConstraints()) {
    ExprResult TransConstraint = TransformExpr(Req->getConstraintExpr());
    if (TransConstraint.isInvalid() || !TransConstraint.get())
      return nullptr;
    if (TransConstraint.get()->isInstantiationDependent())
      return new (SemaRef.Context)
          concepts::NestedRequirement(TransConstraint.get());
    ConstraintSatisfaction Satisfaction;
    return new (SemaRef.Context) concepts::NestedRequirement(
        SemaRef.Context, TransConstraint.get(), Satisfaction);
  }

  ExprResult TransConstraint;
  ConstraintSatisfaction Satisfaction;
  TemplateDeductionInfo Info(Req->getConstraintExpr()->getBeginLoc());
  {
    EnterExpressionEvaluationContext ContextRAII(
        SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    Sema::SFINAETrap Trap(SemaRef);
    Sema::InstantiatingTemplate ConstrInst(SemaRef,
        Req->getConstraintExpr()->getBeginLoc(), Req, Info,
        Req->getConstraintExpr()->getSourceRange());
    if (ConstrInst.isInvalid())
      return nullptr;
    llvm::SmallVector<Expr *> Result;
    if (!SemaRef.CheckConstraintSatisfaction(
            nullptr, {Req->getConstraintExpr()}, Result, TemplateArgs,
            Req->getConstraintExpr()->getSourceRange(), Satisfaction) &&
        !Result.empty())
      TransConstraint = Result[0];
    assert(!Trap.hasErrorOccurred() && "Substitution failures must be handled "
                                       "by CheckConstraintSatisfaction.");
  }
  ASTContext &C = SemaRef.Context;
  if (TransConstraint.isUsable() &&
      TransConstraint.get()->isInstantiationDependent())
    return new (C) concepts::NestedRequirement(TransConstraint.get());
  if (TransConstraint.isInvalid() || !TransConstraint.get() ||
      Satisfaction.HasSubstitutionFailure()) {
    SmallString<128> Entity;
    llvm::raw_svector_ostream OS(Entity);
    Req->getConstraintExpr()->printPretty(OS, nullptr,
                                          SemaRef.getPrintingPolicy());
    return new (C) concepts::NestedRequirement(
        SemaRef.Context, C.backupStr(Entity), Satisfaction);
  }
  return new (C)
      concepts::NestedRequirement(C, TransConstraint.get(), Satisfaction);
}

TypeSourceInfo *Sema::SubstType(TypeSourceInfo *T,
                                const MultiLevelTemplateArgumentList &Args,
                                SourceLocation Loc,
                                DeclarationName Entity,
                                bool AllowDeducedTST) {
  assert(!CodeSynthesisContexts.empty() &&
         "Cannot perform an instantiation without some context on the "
         "instantiation stack");

  if (!T->getType()->isInstantiationDependentType() &&
      !T->getType()->isVariablyModifiedType())
    return T;

  TemplateInstantiator Instantiator(*this, Args, Loc, Entity);
  return AllowDeducedTST ? Instantiator.TransformTypeWithDeducedTST(T)
                         : Instantiator.TransformType(T);
}

TypeSourceInfo *Sema::SubstType(TypeLoc TL,
                                const MultiLevelTemplateArgumentList &Args,
                                SourceLocation Loc,
                                DeclarationName Entity) {
  assert(!CodeSynthesisContexts.empty() &&
         "Cannot perform an instantiation without some context on the "
         "instantiation stack");

  if (TL.getType().isNull())
    return nullptr;

  if (!TL.getType()->isInstantiationDependentType() &&
      !TL.getType()->isVariablyModifiedType()) {
    // FIXME: Make a copy of the TypeLoc data here, so that we can
    // return a new TypeSourceInfo. Inefficient!
    TypeLocBuilder TLB;
    TLB.pushFullCopy(TL);
    return TLB.getTypeSourceInfo(Context, TL.getType());
  }

  TemplateInstantiator Instantiator(*this, Args, Loc, Entity);
  TypeLocBuilder TLB;
  TLB.reserve(TL.getFullDataSize());
  QualType Result = Instantiator.TransformType(TLB, TL);
  if (Result.isNull())
    return nullptr;

  return TLB.getTypeSourceInfo(Context, Result);
}

/// Deprecated form of the above.
QualType Sema::SubstType(QualType T,
                         const MultiLevelTemplateArgumentList &TemplateArgs,
                         SourceLocation Loc, DeclarationName Entity) {
  assert(!CodeSynthesisContexts.empty() &&
         "Cannot perform an instantiation without some context on the "
         "instantiation stack");

  // If T is not a dependent type or a variably-modified type, there
  // is nothing to do.
  if (!T->isInstantiationDependentType() && !T->isVariablyModifiedType())
    return T;

  TemplateInstantiator Instantiator(*this, TemplateArgs, Loc, Entity);
  return Instantiator.TransformType(T);
}

static bool NeedsInstantiationAsFunctionType(TypeSourceInfo *T) {
  if (T->getType()->isInstantiationDependentType() ||
      T->getType()->isVariablyModifiedType())
    return true;

  TypeLoc TL = T->getTypeLoc().IgnoreParens();
  if (!TL.getAs<FunctionProtoTypeLoc>())
    return false;

  FunctionProtoTypeLoc FP = TL.castAs<FunctionProtoTypeLoc>();
  for (ParmVarDecl *P : FP.getParams()) {
    // This must be synthesized from a typedef.
    if (!P) continue;

    // If there are any parameters, a new TypeSourceInfo that refers to the
    // instantiated parameters must be built.
    return true;
  }

  return false;
}

TypeSourceInfo *Sema::SubstFunctionDeclType(TypeSourceInfo *T,
                                const MultiLevelTemplateArgumentList &Args,
                                SourceLocation Loc,
                                DeclarationName Entity,
                                CXXRecordDecl *ThisContext,
                                Qualifiers ThisTypeQuals,
                                bool EvaluateConstraints) {
  assert(!CodeSynthesisContexts.empty() &&
         "Cannot perform an instantiation without some context on the "
         "instantiation stack");

  if (!NeedsInstantiationAsFunctionType(T))
    return T;

  TemplateInstantiator Instantiator(*this, Args, Loc, Entity);
  Instantiator.setEvaluateConstraints(EvaluateConstraints);

  TypeLocBuilder TLB;

  TypeLoc TL = T->getTypeLoc();
  TLB.reserve(TL.getFullDataSize());

  QualType Result;

  if (FunctionProtoTypeLoc Proto =
          TL.IgnoreParens().getAs<FunctionProtoTypeLoc>()) {
    // Instantiate the type, other than its exception specification. The
    // exception specification is instantiated in InitFunctionInstantiation
    // once we've built the FunctionDecl.
    // FIXME: Set the exception specification to EST_Uninstantiated here,
    // instead of rebuilding the function type again later.
    Result = Instantiator.TransformFunctionProtoType(
        TLB, Proto, ThisContext, ThisTypeQuals,
        [](FunctionProtoType::ExceptionSpecInfo &ESI,
           bool &Changed) { return false; });
  } else {
    Result = Instantiator.TransformType(TLB, TL);
  }
  // When there are errors resolving types, clang may use IntTy as a fallback,
  // breaking our assumption that function declarations have function types.
  if (Result.isNull() || !Result->isFunctionType())
    return nullptr;

  return TLB.getTypeSourceInfo(Context, Result);
}

bool Sema::SubstExceptionSpec(SourceLocation Loc,
                              FunctionProtoType::ExceptionSpecInfo &ESI,
                              SmallVectorImpl<QualType> &ExceptionStorage,
                              const MultiLevelTemplateArgumentList &Args) {
  bool Changed = false;
  TemplateInstantiator Instantiator(*this, Args, Loc, DeclarationName());
  return Instantiator.TransformExceptionSpec(Loc, ESI, ExceptionStorage,
                                             Changed);
}

void Sema::SubstExceptionSpec(FunctionDecl *New, const FunctionProtoType *Proto,
                              const MultiLevelTemplateArgumentList &Args) {
  FunctionProtoType::ExceptionSpecInfo ESI =
      Proto->getExtProtoInfo().ExceptionSpec;

  SmallVector<QualType, 4> ExceptionStorage;
  if (SubstExceptionSpec(New->getTypeSourceInfo()->getTypeLoc().getEndLoc(),
                         ESI, ExceptionStorage, Args))
    // On error, recover by dropping the exception specification.
    ESI.Type = EST_None;

  UpdateExceptionSpec(New, ESI);
}

namespace {

  struct GetContainedInventedTypeParmVisitor :
    public TypeVisitor<GetContainedInventedTypeParmVisitor,
                       TemplateTypeParmDecl *> {
    using TypeVisitor<GetContainedInventedTypeParmVisitor,
                      TemplateTypeParmDecl *>::Visit;

    TemplateTypeParmDecl *Visit(QualType T) {
      if (T.isNull())
        return nullptr;
      return Visit(T.getTypePtr());
    }
    // The deduced type itself.
    TemplateTypeParmDecl *VisitTemplateTypeParmType(
        const TemplateTypeParmType *T) {
      if (!T->getDecl() || !T->getDecl()->isImplicit())
        return nullptr;
      return T->getDecl();
    }

    // Only these types can contain 'auto' types, and subsequently be replaced
    // by references to invented parameters.

    TemplateTypeParmDecl *VisitElaboratedType(const ElaboratedType *T) {
      return Visit(T->getNamedType());
    }

    TemplateTypeParmDecl *VisitPointerType(const PointerType *T) {
      return Visit(T->getPointeeType());
    }

    TemplateTypeParmDecl *VisitBlockPointerType(const BlockPointerType *T) {
      return Visit(T->getPointeeType());
    }

    TemplateTypeParmDecl *VisitReferenceType(const ReferenceType *T) {
      return Visit(T->getPointeeTypeAsWritten());
    }

    TemplateTypeParmDecl *VisitMemberPointerType(const MemberPointerType *T) {
      return Visit(T->getPointeeType());
    }

    TemplateTypeParmDecl *VisitArrayType(const ArrayType *T) {
      return Visit(T->getElementType());
    }

    TemplateTypeParmDecl *VisitDependentSizedExtVectorType(
      const DependentSizedExtVectorType *T) {
      return Visit(T->getElementType());
    }

    TemplateTypeParmDecl *VisitVectorType(const VectorType *T) {
      return Visit(T->getElementType());
    }

    TemplateTypeParmDecl *VisitFunctionProtoType(const FunctionProtoType *T) {
      return VisitFunctionType(T);
    }

    TemplateTypeParmDecl *VisitFunctionType(const FunctionType *T) {
      return Visit(T->getReturnType());
    }

    TemplateTypeParmDecl *VisitParenType(const ParenType *T) {
      return Visit(T->getInnerType());
    }

    TemplateTypeParmDecl *VisitAttributedType(const AttributedType *T) {
      return Visit(T->getModifiedType());
    }

    TemplateTypeParmDecl *VisitMacroQualifiedType(const MacroQualifiedType *T) {
      return Visit(T->getUnderlyingType());
    }

    TemplateTypeParmDecl *VisitAdjustedType(const AdjustedType *T) {
      return Visit(T->getOriginalType());
    }

    TemplateTypeParmDecl *VisitPackExpansionType(const PackExpansionType *T) {
      return Visit(T->getPattern());
    }
  };

} // namespace

bool Sema::SubstTypeConstraint(
    TemplateTypeParmDecl *Inst, const TypeConstraint *TC,
    const MultiLevelTemplateArgumentList &TemplateArgs,
    bool EvaluateConstraints) {
  const ASTTemplateArgumentListInfo *TemplArgInfo =
      TC->getTemplateArgsAsWritten();

  if (!EvaluateConstraints) {
      Inst->setTypeConstraint(TC->getConceptReference(),
                              TC->getImmediatelyDeclaredConstraint());
      return false;
  }

  TemplateArgumentListInfo InstArgs;

  if (TemplArgInfo) {
    InstArgs.setLAngleLoc(TemplArgInfo->LAngleLoc);
    InstArgs.setRAngleLoc(TemplArgInfo->RAngleLoc);
    if (SubstTemplateArguments(TemplArgInfo->arguments(), TemplateArgs,
                               InstArgs))
      return true;
  }
  return AttachTypeConstraint(
      TC->getNestedNameSpecifierLoc(), TC->getConceptNameInfo(),
      TC->getNamedConcept(),
      /*FoundDecl=*/TC->getConceptReference()->getFoundDecl(), &InstArgs, Inst,
      Inst->isParameterPack()
          ? cast<CXXFoldExpr>(TC->getImmediatelyDeclaredConstraint())
                ->getEllipsisLoc()
          : SourceLocation());
}

ParmVarDecl *Sema::SubstParmVarDecl(
    ParmVarDecl *OldParm, const MultiLevelTemplateArgumentList &TemplateArgs,
    int indexAdjustment, std::optional<unsigned> NumExpansions,
    bool ExpectParameterPack, bool EvaluateConstraint) {
  TypeSourceInfo *OldDI = OldParm->getTypeSourceInfo();
  TypeSourceInfo *NewDI = nullptr;

  TypeLoc OldTL = OldDI->getTypeLoc();
  if (PackExpansionTypeLoc ExpansionTL = OldTL.getAs<PackExpansionTypeLoc>()) {

    // We have a function parameter pack. Substitute into the pattern of the
    // expansion.
    NewDI = SubstType(ExpansionTL.getPatternLoc(), TemplateArgs,
                      OldParm->getLocation(), OldParm->getDeclName());
    if (!NewDI)
      return nullptr;

    if (NewDI->getType()->containsUnexpandedParameterPack()) {
      // We still have unexpanded parameter packs, which means that
      // our function parameter is still a function parameter pack.
      // Therefore, make its type a pack expansion type.
      NewDI = CheckPackExpansion(NewDI, ExpansionTL.getEllipsisLoc(),
                                 NumExpansions);
    } else if (ExpectParameterPack) {
      // We expected to get a parameter pack but didn't (because the type
      // itself is not a pack expansion type), so complain. This can occur when
      // the substitution goes through an alias template that "loses" the
      // pack expansion.
      Diag(OldParm->getLocation(),
           diag::err_function_parameter_pack_without_parameter_packs)
        << NewDI->getType();
      return nullptr;
    }
  } else {
    NewDI = SubstType(OldDI, TemplateArgs, OldParm->getLocation(),
                      OldParm->getDeclName());
  }

  if (!NewDI)
    return nullptr;

  if (NewDI->getType()->isVoidType()) {
    Diag(OldParm->getLocation(), diag::err_param_with_void_type);
    return nullptr;
  }

  // In abbreviated templates, TemplateTypeParmDecls with possible
  // TypeConstraints are created when the parameter list is originally parsed.
  // The TypeConstraints can therefore reference other functions parameters in
  // the abbreviated function template, which is why we must instantiate them
  // here, when the instantiated versions of those referenced parameters are in
  // scope.
  if (TemplateTypeParmDecl *TTP =
          GetContainedInventedTypeParmVisitor().Visit(OldDI->getType())) {
    if (const TypeConstraint *TC = TTP->getTypeConstraint()) {
      auto *Inst = cast_or_null<TemplateTypeParmDecl>(
          FindInstantiatedDecl(TTP->getLocation(), TTP, TemplateArgs));
      // We will first get here when instantiating the abbreviated function
      // template's described function, but we might also get here later.
      // Make sure we do not instantiate the TypeConstraint more than once.
      if (Inst && !Inst->getTypeConstraint()) {
        if (SubstTypeConstraint(Inst, TC, TemplateArgs, EvaluateConstraint))
          return nullptr;
      }
    }
  }

  ParmVarDecl *NewParm = CheckParameter(Context.getTranslationUnitDecl(),
                                        OldParm->getInnerLocStart(),
                                        OldParm->getLocation(),
                                        OldParm->getIdentifier(),
                                        NewDI->getType(), NewDI,
                                        OldParm->getStorageClass());
  if (!NewParm)
    return nullptr;

  // Mark the (new) default argument as uninstantiated (if any).
  if (OldParm->hasUninstantiatedDefaultArg()) {
    Expr *Arg = OldParm->getUninstantiatedDefaultArg();
    NewParm->setUninstantiatedDefaultArg(Arg);
  } else if (OldParm->hasUnparsedDefaultArg()) {
    NewParm->setUnparsedDefaultArg();
    UnparsedDefaultArgInstantiations[OldParm].push_back(NewParm);
  } else if (Expr *Arg = OldParm->getDefaultArg()) {
    // Default arguments cannot be substituted until the declaration context
    // for the associated function or lambda capture class is available.
    // This is necessary for cases like the following where construction of
    // the lambda capture class for the outer lambda is dependent on the
    // parameter types but where the default argument is dependent on the
    // outer lambda's declaration context.
    //   template <typename T>
    //   auto f() {
    //     return [](T = []{ return T{}; }()) { return 0; };
    //   }
    NewParm->setUninstantiatedDefaultArg(Arg);
  }

  NewParm->setExplicitObjectParameterLoc(
      OldParm->getExplicitObjectParamThisLoc());
  NewParm->setHasInheritedDefaultArg(OldParm->hasInheritedDefaultArg());

  if (OldParm->isParameterPack() && !NewParm->isParameterPack()) {
    // Add the new parameter to the instantiated parameter pack.
    CurrentInstantiationScope->InstantiatedLocalPackArg(OldParm, NewParm);
  } else {
    // Introduce an Old -> New mapping
    CurrentInstantiationScope->InstantiatedLocal(OldParm, NewParm);
  }

  // FIXME: OldParm may come from a FunctionProtoType, in which case CurContext
  // can be anything, is this right ?
  NewParm->setDeclContext(CurContext);

  NewParm->setScopeInfo(OldParm->getFunctionScopeDepth(),
                        OldParm->getFunctionScopeIndex() + indexAdjustment);

  InstantiateAttrs(TemplateArgs, OldParm, NewParm);

  return NewParm;
}

bool Sema::SubstParmTypes(
    SourceLocation Loc, ArrayRef<ParmVarDecl *> Params,
    const FunctionProtoType::ExtParameterInfo *ExtParamInfos,
    const MultiLevelTemplateArgumentList &TemplateArgs,
    SmallVectorImpl<QualType> &ParamTypes,
    SmallVectorImpl<ParmVarDecl *> *OutParams,
    ExtParameterInfoBuilder &ParamInfos) {
  assert(!CodeSynthesisContexts.empty() &&
         "Cannot perform an instantiation without some context on the "
         "instantiation stack");

  TemplateInstantiator Instantiator(*this, TemplateArgs, Loc,
                                    DeclarationName());
  return Instantiator.TransformFunctionTypeParams(
      Loc, Params, nullptr, ExtParamInfos, ParamTypes, OutParams, ParamInfos);
}

bool Sema::SubstDefaultArgument(
    SourceLocation Loc,
    ParmVarDecl *Param,
    const MultiLevelTemplateArgumentList &TemplateArgs,
    bool ForCallExpr) {
  FunctionDecl *FD = cast<FunctionDecl>(Param->getDeclContext());
  Expr *PatternExpr = Param->getUninstantiatedDefaultArg();

  EnterExpressionEvaluationContext EvalContext(
      *this, ExpressionEvaluationContext::PotentiallyEvaluated, Param);

  InstantiatingTemplate Inst(*this, Loc, Param, TemplateArgs.getInnermost());
  if (Inst.isInvalid())
    return true;
  if (Inst.isAlreadyInstantiating()) {
    Diag(Param->getBeginLoc(), diag::err_recursive_default_argument) << FD;
    Param->setInvalidDecl();
    return true;
  }

  ExprResult Result;
  {
    // C++ [dcl.fct.default]p5:
    //   The names in the [default argument] expression are bound, and
    //   the semantic constraints are checked, at the point where the
    //   default argument expression appears.
    ContextRAII SavedContext(*this, FD);
    std::unique_ptr<LocalInstantiationScope> LIS;
    MultiLevelTemplateArgumentList NewTemplateArgs = TemplateArgs;

    if (ForCallExpr) {
      // When instantiating a default argument due to use in a call expression,
      // an instantiation scope that includes the parameters of the callee is
      // required to satisfy references from the default argument. For example:
      //   template<typename T> void f(T a, int = decltype(a)());
      //   void g() { f(0); }
      LIS = std::make_unique<LocalInstantiationScope>(*this);
      FunctionDecl *PatternFD = FD->getTemplateInstantiationPattern(
          /*ForDefinition*/ false);
      if (addInstantiatedParametersToScope(FD, PatternFD, *LIS, TemplateArgs))
        return true;
      const FunctionTemplateDecl *PrimaryTemplate = FD->getPrimaryTemplate();
      if (PrimaryTemplate && PrimaryTemplate->isOutOfLine()) {
        TemplateArgumentList *CurrentTemplateArgumentList =
            TemplateArgumentList::CreateCopy(getASTContext(),
                                             TemplateArgs.getInnermost());
        NewTemplateArgs = getTemplateInstantiationArgs(
            FD, FD->getDeclContext(), /*Final=*/false,
            CurrentTemplateArgumentList->asArray(), /*RelativeToPrimary=*/true);
      }
    }

    runWithSufficientStackSpace(Loc, [&] {
      Result = SubstInitializer(PatternExpr, NewTemplateArgs,
                                /*DirectInit*/ false);
    });
  }
  if (Result.isInvalid())
    return true;

  if (ForCallExpr) {
    // Check the expression as an initializer for the parameter.
    InitializedEntity Entity
      = InitializedEntity::InitializeParameter(Context, Param);
    InitializationKind Kind = InitializationKind::CreateCopy(
        Param->getLocation(),
        /*FIXME:EqualLoc*/ PatternExpr->getBeginLoc());
    Expr *ResultE = Result.getAs<Expr>();

    InitializationSequence InitSeq(*this, Entity, Kind, ResultE);
    Result = InitSeq.Perform(*this, Entity, Kind, ResultE);
    if (Result.isInvalid())
      return true;

    Result =
        ActOnFinishFullExpr(Result.getAs<Expr>(), Param->getOuterLocStart(),
                            /*DiscardedValue*/ false);
  } else {
    // FIXME: Obtain the source location for the '=' token.
    SourceLocation EqualLoc = PatternExpr->getBeginLoc();
    Result = ConvertParamDefaultArgument(Param, Result.getAs<Expr>(), EqualLoc);
  }
  if (Result.isInvalid())
      return true;

  // Remember the instantiated default argument.
  Param->setDefaultArg(Result.getAs<Expr>());

  return false;
}

bool
Sema::SubstBaseSpecifiers(CXXRecordDecl *Instantiation,
                          CXXRecordDecl *Pattern,
                          const MultiLevelTemplateArgumentList &TemplateArgs) {
  bool Invalid = false;
  SmallVector<CXXBaseSpecifier*, 4> InstantiatedBases;
  for (const auto &Base : Pattern->bases()) {
    if (!Base.getType()->isDependentType()) {
      if (const CXXRecordDecl *RD = Base.getType()->getAsCXXRecordDecl()) {
        if (RD->isInvalidDecl())
          Instantiation->setInvalidDecl();
      }
      InstantiatedBases.push_back(new (Context) CXXBaseSpecifier(Base));
      continue;
    }

    SourceLocation EllipsisLoc;
    TypeSourceInfo *BaseTypeLoc;
    if (Base.isPackExpansion()) {
      // This is a pack expansion. See whether we should expand it now, or
      // wait until later.
      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      collectUnexpandedParameterPacks(Base.getTypeSourceInfo()->getTypeLoc(),
                                      Unexpanded);
      bool ShouldExpand = false;
      bool RetainExpansion = false;
      std::optional<unsigned> NumExpansions;
      if (CheckParameterPacksForExpansion(Base.getEllipsisLoc(),
                                          Base.getSourceRange(),
                                          Unexpanded,
                                          TemplateArgs, ShouldExpand,
                                          RetainExpansion,
                                          NumExpansions)) {
        Invalid = true;
        continue;
      }

      // If we should expand this pack expansion now, do so.
      if (ShouldExpand) {
        for (unsigned I = 0; I != *NumExpansions; ++I) {
            Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this, I);

          TypeSourceInfo *BaseTypeLoc = SubstType(Base.getTypeSourceInfo(),
                                                  TemplateArgs,
                                              Base.getSourceRange().getBegin(),
                                                  DeclarationName());
          if (!BaseTypeLoc) {
            Invalid = true;
            continue;
          }

          if (CXXBaseSpecifier *InstantiatedBase
                = CheckBaseSpecifier(Instantiation,
                                     Base.getSourceRange(),
                                     Base.isVirtual(),
                                     Base.getAccessSpecifierAsWritten(),
                                     BaseTypeLoc,
                                     SourceLocation()))
            InstantiatedBases.push_back(InstantiatedBase);
          else
            Invalid = true;
        }

        continue;
      }

      // The resulting base specifier will (still) be a pack expansion.
      EllipsisLoc = Base.getEllipsisLoc();
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this, -1);
      BaseTypeLoc = SubstType(Base.getTypeSourceInfo(),
                              TemplateArgs,
                              Base.getSourceRange().getBegin(),
                              DeclarationName());
    } else {
      BaseTypeLoc = SubstType(Base.getTypeSourceInfo(),
                              TemplateArgs,
                              Base.getSourceRange().getBegin(),
                              DeclarationName());
    }

    if (!BaseTypeLoc) {
      Invalid = true;
      continue;
    }

    if (CXXBaseSpecifier *InstantiatedBase
          = CheckBaseSpecifier(Instantiation,
                               Base.getSourceRange(),
                               Base.isVirtual(),
                               Base.getAccessSpecifierAsWritten(),
                               BaseTypeLoc,
                               EllipsisLoc))
      InstantiatedBases.push_back(InstantiatedBase);
    else
      Invalid = true;
  }

  if (!Invalid && AttachBaseSpecifiers(Instantiation, InstantiatedBases))
    Invalid = true;

  return Invalid;
}

// Defined via #include from SemaTemplateInstantiateDecl.cpp
namespace clang {
  namespace sema {
    Attr *instantiateTemplateAttribute(const Attr *At, ASTContext &C, Sema &S,
                            const MultiLevelTemplateArgumentList &TemplateArgs);
    Attr *instantiateTemplateAttributeForDecl(
        const Attr *At, ASTContext &C, Sema &S,
        const MultiLevelTemplateArgumentList &TemplateArgs);
  }
}

bool
Sema::InstantiateClass(SourceLocation PointOfInstantiation,
                       CXXRecordDecl *Instantiation, CXXRecordDecl *Pattern,
                       const MultiLevelTemplateArgumentList &TemplateArgs,
                       TemplateSpecializationKind TSK,
                       bool Complain) {
  CXXRecordDecl *PatternDef
    = cast_or_null<CXXRecordDecl>(Pattern->getDefinition());
  if (DiagnoseUninstantiableTemplate(PointOfInstantiation, Instantiation,
                                Instantiation->getInstantiatedFromMemberClass(),
                                     Pattern, PatternDef, TSK, Complain))
    return true;

  llvm::TimeTraceScope TimeScope("InstantiateClass", [&]() {
    llvm::TimeTraceMetadata M;
    llvm::raw_string_ostream OS(M.Detail);
    Instantiation->getNameForDiagnostic(OS, getPrintingPolicy(),
                                        /*Qualified=*/true);
    if (llvm::isTimeTraceVerbose()) {
      auto Loc = SourceMgr.getExpansionLoc(Instantiation->getLocation());
      M.File = SourceMgr.getFilename(Loc);
      M.Line = SourceMgr.getExpansionLineNumber(Loc);
    }
    return M;
  });

  Pattern = PatternDef;

  // Record the point of instantiation.
  if (MemberSpecializationInfo *MSInfo
        = Instantiation->getMemberSpecializationInfo()) {
    MSInfo->setTemplateSpecializationKind(TSK);
    MSInfo->setPointOfInstantiation(PointOfInstantiation);
  } else if (ClassTemplateSpecializationDecl *Spec
        = dyn_cast<ClassTemplateSpecializationDecl>(Instantiation)) {
    Spec->setTemplateSpecializationKind(TSK);
    Spec->setPointOfInstantiation(PointOfInstantiation);
  }

  InstantiatingTemplate Inst(*this, PointOfInstantiation, Instantiation);
  if (Inst.isInvalid())
    return true;
  assert(!Inst.isAlreadyInstantiating() && "should have been caught by caller");
  PrettyDeclStackTraceEntry CrashInfo(Context, Instantiation, SourceLocation(),
                                      "instantiating class definition");

  // Enter the scope of this instantiation. We don't use
  // PushDeclContext because we don't have a scope.
  ContextRAII SavedContext(*this, Instantiation);
  EnterExpressionEvaluationContext EvalContext(
      *this, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

  // If this is an instantiation of a local class, merge this local
  // instantiation scope with the enclosing scope. Otherwise, every
  // instantiation of a class has its own local instantiation scope.
  bool MergeWithParentScope = !Instantiation->isDefinedOutsideFunctionOrMethod();
  LocalInstantiationScope Scope(*this, MergeWithParentScope);

  // Some class state isn't processed immediately but delayed till class
  // instantiation completes. We may not be ready to handle any delayed state
  // already on the stack as it might correspond to a different class, so save
  // it now and put it back later.
  SavePendingParsedClassStateRAII SavedPendingParsedClassState(*this);

  // Pull attributes from the pattern onto the instantiation.
  InstantiateAttrs(TemplateArgs, Pattern, Instantiation);

  // Start the definition of this instantiation.
  Instantiation->startDefinition();

  // The instantiation is visible here, even if it was first declared in an
  // unimported module.
  Instantiation->setVisibleDespiteOwningModule();

  // FIXME: This loses the as-written tag kind for an explicit instantiation.
  Instantiation->setTagKind(Pattern->getTagKind());

  // Do substitution on the base class specifiers.
  if (SubstBaseSpecifiers(Instantiation, Pattern, TemplateArgs))
    Instantiation->setInvalidDecl();

  TemplateDeclInstantiator Instantiator(*this, Instantiation, TemplateArgs);
  Instantiator.setEvaluateConstraints(false);
  SmallVector<Decl*, 4> Fields;
  // Delay instantiation of late parsed attributes.
  LateInstantiatedAttrVec LateAttrs;
  Instantiator.enableLateAttributeInstantiation(&LateAttrs);

  bool MightHaveConstexprVirtualFunctions = false;
  for (auto *Member : Pattern->decls()) {
    // Don't instantiate members not belonging in this semantic context.
    // e.g. for:
    // @code
    //    template <int i> class A {
    //      class B *g;
    //    };
    // @endcode
    // 'class B' has the template as lexical context but semantically it is
    // introduced in namespace scope.
    if (Member->getDeclContext() != Pattern)
      continue;

    // BlockDecls can appear in a default-member-initializer. They must be the
    // child of a BlockExpr, so we only know how to instantiate them from there.
    // Similarly, lambda closure types are recreated when instantiating the
    // corresponding LambdaExpr.
    if (isa<BlockDecl>(Member) ||
        (isa<CXXRecordDecl>(Member) && cast<CXXRecordDecl>(Member)->isLambda()))
      continue;

    if (Member->isInvalidDecl()) {
      Instantiation->setInvalidDecl();
      continue;
    }

    Decl *NewMember = Instantiator.Visit(Member);
    if (NewMember) {
      if (FieldDecl *Field = dyn_cast<FieldDecl>(NewMember)) {
        Fields.push_back(Field);
      } else if (EnumDecl *Enum = dyn_cast<EnumDecl>(NewMember)) {
        // C++11 [temp.inst]p1: The implicit instantiation of a class template
        // specialization causes the implicit instantiation of the definitions
        // of unscoped member enumerations.
        // Record a point of instantiation for this implicit instantiation.
        if (TSK == TSK_ImplicitInstantiation && !Enum->isScoped() &&
            Enum->isCompleteDefinition()) {
          MemberSpecializationInfo *MSInfo =Enum->getMemberSpecializationInfo();
          assert(MSInfo && "no spec info for member enum specialization");
          MSInfo->setTemplateSpecializationKind(TSK_ImplicitInstantiation);
          MSInfo->setPointOfInstantiation(PointOfInstantiation);
        }
      } else if (StaticAssertDecl *SA = dyn_cast<StaticAssertDecl>(NewMember)) {
        if (SA->isFailed()) {
          // A static_assert failed. Bail out; instantiating this
          // class is probably not meaningful.
          Instantiation->setInvalidDecl();
          break;
        }
      } else if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(NewMember)) {
        if (MD->isConstexpr() && !MD->getFriendObjectKind() &&
            (MD->isVirtualAsWritten() || Instantiation->getNumBases()))
          MightHaveConstexprVirtualFunctions = true;
      }

      if (NewMember->isInvalidDecl())
        Instantiation->setInvalidDecl();
    } else {
      // FIXME: Eventually, a NULL return will mean that one of the
      // instantiations was a semantic disaster, and we'll want to mark the
      // declaration invalid.
      // For now, we expect to skip some members that we can't yet handle.
    }
  }

  // Finish checking fields.
  ActOnFields(nullptr, Instantiation->getLocation(), Instantiation, Fields,
              SourceLocation(), SourceLocation(), ParsedAttributesView());
  CheckCompletedCXXClass(nullptr, Instantiation);

  // Default arguments are parsed, if not instantiated. We can go instantiate
  // default arg exprs for default constructors if necessary now. Unless we're
  // parsing a class, in which case wait until that's finished.
  if (ParsingClassDepth == 0)
    ActOnFinishCXXNonNestedClass();

  // Instantiate late parsed attributes, and attach them to their decls.
  // See Sema::InstantiateAttrs
  for (LateInstantiatedAttrVec::iterator I = LateAttrs.begin(),
       E = LateAttrs.end(); I != E; ++I) {
    assert(CurrentInstantiationScope == Instantiator.getStartingScope());
    CurrentInstantiationScope = I->Scope;

    // Allow 'this' within late-parsed attributes.
    auto *ND = cast<NamedDecl>(I->NewDecl);
    auto *ThisContext = dyn_cast_or_null<CXXRecordDecl>(ND->getDeclContext());
    CXXThisScopeRAII ThisScope(*this, ThisContext, Qualifiers(),
                               ND->isCXXInstanceMember());

    Attr *NewAttr =
      instantiateTemplateAttribute(I->TmplAttr, Context, *this, TemplateArgs);
    if (NewAttr)
      I->NewDecl->addAttr(NewAttr);
    LocalInstantiationScope::deleteScopes(I->Scope,
                                          Instantiator.getStartingScope());
  }
  Instantiator.disableLateAttributeInstantiation();
  LateAttrs.clear();

  ActOnFinishDelayedMemberInitializers(Instantiation);

  // FIXME: We should do something similar for explicit instantiations so they
  // end up in the right module.
  if (TSK == TSK_ImplicitInstantiation) {
    Instantiation->setLocation(Pattern->getLocation());
    Instantiation->setLocStart(Pattern->getInnerLocStart());
    Instantiation->setBraceRange(Pattern->getBraceRange());
  }

  if (!Instantiation->isInvalidDecl()) {
    // Perform any dependent diagnostics from the pattern.
    if (Pattern->isDependentContext())
      PerformDependentDiagnostics(Pattern, TemplateArgs);

    // Instantiate any out-of-line class template partial
    // specializations now.
    for (TemplateDeclInstantiator::delayed_partial_spec_iterator
              P = Instantiator.delayed_partial_spec_begin(),
           PEnd = Instantiator.delayed_partial_spec_end();
         P != PEnd; ++P) {
      if (!Instantiator.InstantiateClassTemplatePartialSpecialization(
              P->first, P->second)) {
        Instantiation->setInvalidDecl();
        break;
      }
    }

    // Instantiate any out-of-line variable template partial
    // specializations now.
    for (TemplateDeclInstantiator::delayed_var_partial_spec_iterator
              P = Instantiator.delayed_var_partial_spec_begin(),
           PEnd = Instantiator.delayed_var_partial_spec_end();
         P != PEnd; ++P) {
      if (!Instantiator.InstantiateVarTemplatePartialSpecialization(
              P->first, P->second)) {
        Instantiation->setInvalidDecl();
        break;
      }
    }
  }

  // Exit the scope of this instantiation.
  SavedContext.pop();

  if (!Instantiation->isInvalidDecl()) {
    // Always emit the vtable for an explicit instantiation definition
    // of a polymorphic class template specialization. Otherwise, eagerly
    // instantiate only constexpr virtual functions in preparation for their use
    // in constant evaluation.
    if (TSK == TSK_ExplicitInstantiationDefinition)
      MarkVTableUsed(PointOfInstantiation, Instantiation, true);
    else if (MightHaveConstexprVirtualFunctions)
      MarkVirtualMembersReferenced(PointOfInstantiation, Instantiation,
                                   /*ConstexprOnly*/ true);
  }

  Consumer.HandleTagDeclDefinition(Instantiation);

  return Instantiation->isInvalidDecl();
}

bool Sema::InstantiateEnum(SourceLocation PointOfInstantiation,
                           EnumDecl *Instantiation, EnumDecl *Pattern,
                           const MultiLevelTemplateArgumentList &TemplateArgs,
                           TemplateSpecializationKind TSK) {
  EnumDecl *PatternDef = Pattern->getDefinition();
  if (DiagnoseUninstantiableTemplate(PointOfInstantiation, Instantiation,
                                 Instantiation->getInstantiatedFromMemberEnum(),
                                     Pattern, PatternDef, TSK,/*Complain*/true))
    return true;
  Pattern = PatternDef;

  // Record the point of instantiation.
  if (MemberSpecializationInfo *MSInfo
        = Instantiation->getMemberSpecializationInfo()) {
    MSInfo->setTemplateSpecializationKind(TSK);
    MSInfo->setPointOfInstantiation(PointOfInstantiation);
  }

  InstantiatingTemplate Inst(*this, PointOfInstantiation, Instantiation);
  if (Inst.isInvalid())
    return true;
  if (Inst.isAlreadyInstantiating())
    return false;
  PrettyDeclStackTraceEntry CrashInfo(Context, Instantiation, SourceLocation(),
                                      "instantiating enum definition");

  // The instantiation is visible here, even if it was first declared in an
  // unimported module.
  Instantiation->setVisibleDespiteOwningModule();

  // Enter the scope of this instantiation. We don't use
  // PushDeclContext because we don't have a scope.
  ContextRAII SavedContext(*this, Instantiation);
  EnterExpressionEvaluationContext EvalContext(
      *this, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

  LocalInstantiationScope Scope(*this, /*MergeWithParentScope*/true);

  // Pull attributes from the pattern onto the instantiation.
  InstantiateAttrs(TemplateArgs, Pattern, Instantiation);

  TemplateDeclInstantiator Instantiator(*this, Instantiation, TemplateArgs);
  Instantiator.InstantiateEnumDefinition(Instantiation, Pattern);

  // Exit the scope of this instantiation.
  SavedContext.pop();

  return Instantiation->isInvalidDecl();
}

bool Sema::InstantiateInClassInitializer(
    SourceLocation PointOfInstantiation, FieldDecl *Instantiation,
    FieldDecl *Pattern, const MultiLevelTemplateArgumentList &TemplateArgs) {
  // If there is no initializer, we don't need to do anything.
  if (!Pattern->hasInClassInitializer())
    return false;

  assert(Instantiation->getInClassInitStyle() ==
             Pattern->getInClassInitStyle() &&
         "pattern and instantiation disagree about init style");

  // Error out if we haven't parsed the initializer of the pattern yet because
  // we are waiting for the closing brace of the outer class.
  Expr *OldInit = Pattern->getInClassInitializer();
  if (!OldInit) {
    RecordDecl *PatternRD = Pattern->getParent();
    RecordDecl *OutermostClass = PatternRD->getOuterLexicalRecordContext();
    Diag(PointOfInstantiation,
         diag::err_default_member_initializer_not_yet_parsed)
        << OutermostClass << Pattern;
    Diag(Pattern->getEndLoc(),
         diag::note_default_member_initializer_not_yet_parsed);
    Instantiation->setInvalidDecl();
    return true;
  }

  InstantiatingTemplate Inst(*this, PointOfInstantiation, Instantiation);
  if (Inst.isInvalid())
    return true;
  if (Inst.isAlreadyInstantiating()) {
    // Error out if we hit an instantiation cycle for this initializer.
    Diag(PointOfInstantiation, diag::err_default_member_initializer_cycle)
      << Instantiation;
    return true;
  }
  PrettyDeclStackTraceEntry CrashInfo(Context, Instantiation, SourceLocation(),
                                      "instantiating default member init");

  // Enter the scope of this instantiation. We don't use PushDeclContext because
  // we don't have a scope.
  ContextRAII SavedContext(*this, Instantiation->getParent());
  EnterExpressionEvaluationContext EvalContext(
      *this, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
  ExprEvalContexts.back().DelayedDefaultInitializationContext = {
      PointOfInstantiation, Instantiation, CurContext};

  LocalInstantiationScope Scope(*this, true);

  // Instantiate the initializer.
  ActOnStartCXXInClassMemberInitializer();
  CXXThisScopeRAII ThisScope(*this, Instantiation->getParent(), Qualifiers());

  ExprResult NewInit = SubstInitializer(OldInit, TemplateArgs,
                                        /*CXXDirectInit=*/false);
  Expr *Init = NewInit.get();
  assert((!Init || !isa<ParenListExpr>(Init)) && "call-style init in class");
  ActOnFinishCXXInClassMemberInitializer(
      Instantiation, Init ? Init->getBeginLoc() : SourceLocation(), Init);

  if (auto *L = getASTMutationListener())
    L->DefaultMemberInitializerInstantiated(Instantiation);

  // Return true if the in-class initializer is still missing.
  return !Instantiation->getInClassInitializer();
}

namespace {
  /// A partial specialization whose template arguments have matched
  /// a given template-id.
  struct PartialSpecMatchResult {
    ClassTemplatePartialSpecializationDecl *Partial;
    TemplateArgumentList *Args;
  };
}

bool Sema::usesPartialOrExplicitSpecialization(
    SourceLocation Loc, ClassTemplateSpecializationDecl *ClassTemplateSpec) {
  if (ClassTemplateSpec->getTemplateSpecializationKind() ==
      TSK_ExplicitSpecialization)
    return true;

  SmallVector<ClassTemplatePartialSpecializationDecl *, 4> PartialSpecs;
  ClassTemplateSpec->getSpecializedTemplate()
                   ->getPartialSpecializations(PartialSpecs);
  for (unsigned I = 0, N = PartialSpecs.size(); I != N; ++I) {
    TemplateDeductionInfo Info(Loc);
    if (DeduceTemplateArguments(PartialSpecs[I],
                                ClassTemplateSpec->getTemplateArgs().asArray(),
                                Info) == TemplateDeductionResult::Success)
      return true;
  }

  return false;
}

/// Get the instantiation pattern to use to instantiate the definition of a
/// given ClassTemplateSpecializationDecl (either the pattern of the primary
/// template or of a partial specialization).
static ActionResult<CXXRecordDecl *>
getPatternForClassTemplateSpecialization(
    Sema &S, SourceLocation PointOfInstantiation,
    ClassTemplateSpecializationDecl *ClassTemplateSpec,
    TemplateSpecializationKind TSK) {
  Sema::InstantiatingTemplate Inst(S, PointOfInstantiation, ClassTemplateSpec);
  if (Inst.isInvalid())
    return {/*Invalid=*/true};
  if (Inst.isAlreadyInstantiating())
    return {/*Invalid=*/false};

  llvm::PointerUnion<ClassTemplateDecl *,
                     ClassTemplatePartialSpecializationDecl *>
      Specialized = ClassTemplateSpec->getSpecializedTemplateOrPartial();
  if (!Specialized.is<ClassTemplatePartialSpecializationDecl *>()) {
    // Find best matching specialization.
    ClassTemplateDecl *Template = ClassTemplateSpec->getSpecializedTemplate();

    // C++ [temp.class.spec.match]p1:
    //   When a class template is used in a context that requires an
    //   instantiation of the class, it is necessary to determine
    //   whether the instantiation is to be generated using the primary
    //   template or one of the partial specializations. This is done by
    //   matching the template arguments of the class template
    //   specialization with the template argument lists of the partial
    //   specializations.
    typedef PartialSpecMatchResult MatchResult;
    SmallVector<MatchResult, 4> Matched;
    SmallVector<ClassTemplatePartialSpecializationDecl *, 4> PartialSpecs;
    Template->getPartialSpecializations(PartialSpecs);
    TemplateSpecCandidateSet FailedCandidates(PointOfInstantiation);
    for (unsigned I = 0, N = PartialSpecs.size(); I != N; ++I) {
      ClassTemplatePartialSpecializationDecl *Partial = PartialSpecs[I];
      TemplateDeductionInfo Info(FailedCandidates.getLocation());
      if (TemplateDeductionResult Result = S.DeduceTemplateArguments(
              Partial, ClassTemplateSpec->getTemplateArgs().asArray(), Info);
          Result != TemplateDeductionResult::Success) {
        // Store the failed-deduction information for use in diagnostics, later.
        // TODO: Actually use the failed-deduction info?
        FailedCandidates.addCandidate().set(
            DeclAccessPair::make(Template, AS_public), Partial,
            MakeDeductionFailureInfo(S.Context, Result, Info));
        (void)Result;
      } else {
        Matched.push_back(PartialSpecMatchResult());
        Matched.back().Partial = Partial;
        Matched.back().Args = Info.takeCanonical();
      }
    }

    // If we're dealing with a member template where the template parameters
    // have been instantiated, this provides the original template parameters
    // from which the member template's parameters were instantiated.

    if (Matched.size() >= 1) {
      SmallVectorImpl<MatchResult>::iterator Best = Matched.begin();
      if (Matched.size() == 1) {
        //   -- If exactly one matching specialization is found, the
        //      instantiation is generated from that specialization.
        // We don't need to do anything for this.
      } else {
        //   -- If more than one matching specialization is found, the
        //      partial order rules (14.5.4.2) are used to determine
        //      whether one of the specializations is more specialized
        //      than the others. If none of the specializations is more
        //      specialized than all of the other matching
        //      specializations, then the use of the class template is
        //      ambiguous and the program is ill-formed.
        for (SmallVectorImpl<MatchResult>::iterator P = Best + 1,
                                                 PEnd = Matched.end();
             P != PEnd; ++P) {
          if (S.getMoreSpecializedPartialSpecialization(
                  P->Partial, Best->Partial, PointOfInstantiation) ==
              P->Partial)
            Best = P;
        }

        // Determine if the best partial specialization is more specialized than
        // the others.
        bool Ambiguous = false;
        for (SmallVectorImpl<MatchResult>::iterator P = Matched.begin(),
                                                 PEnd = Matched.end();
             P != PEnd; ++P) {
          if (P != Best && S.getMoreSpecializedPartialSpecialization(
                               P->Partial, Best->Partial,
                               PointOfInstantiation) != Best->Partial) {
            Ambiguous = true;
            break;
          }
        }

        if (Ambiguous) {
          // Partial ordering did not produce a clear winner. Complain.
          Inst.Clear();
          ClassTemplateSpec->setInvalidDecl();
          S.Diag(PointOfInstantiation,
                 diag::err_partial_spec_ordering_ambiguous)
              << ClassTemplateSpec;

          // Print the matching partial specializations.
          for (SmallVectorImpl<MatchResult>::iterator P = Matched.begin(),
                                                   PEnd = Matched.end();
               P != PEnd; ++P)
            S.Diag(P->Partial->getLocation(), diag::note_partial_spec_match)
                << S.getTemplateArgumentBindingsText(
                       P->Partial->getTemplateParameters(), *P->Args);

          return {/*Invalid=*/true};
        }
      }

      ClassTemplateSpec->setInstantiationOf(Best->Partial, Best->Args);
    } else {
      //   -- If no matches are found, the instantiation is generated
      //      from the primary template.
    }
  }

  CXXRecordDecl *Pattern = nullptr;
  Specialized = ClassTemplateSpec->getSpecializedTemplateOrPartial();
  if (auto *PartialSpec =
          Specialized.dyn_cast<ClassTemplatePartialSpecializationDecl *>()) {
    // Instantiate using the best class template partial specialization.
    while (PartialSpec->getInstantiatedFromMember()) {
      // If we've found an explicit specialization of this class template,
      // stop here and use that as the pattern.
      if (PartialSpec->isMemberSpecialization())
        break;

      PartialSpec = PartialSpec->getInstantiatedFromMember();
    }
    Pattern = PartialSpec;
  } else {
    ClassTemplateDecl *Template = ClassTemplateSpec->getSpecializedTemplate();
    while (Template->getInstantiatedFromMemberTemplate()) {
      // If we've found an explicit specialization of this class template,
      // stop here and use that as the pattern.
      if (Template->isMemberSpecialization())
        break;

      Template = Template->getInstantiatedFromMemberTemplate();
    }
    Pattern = Template->getTemplatedDecl();
  }

  return Pattern;
}

bool Sema::InstantiateClassTemplateSpecialization(
    SourceLocation PointOfInstantiation,
    ClassTemplateSpecializationDecl *ClassTemplateSpec,
    TemplateSpecializationKind TSK, bool Complain) {
  // Perform the actual instantiation on the canonical declaration.
  ClassTemplateSpec = cast<ClassTemplateSpecializationDecl>(
      ClassTemplateSpec->getCanonicalDecl());
  if (ClassTemplateSpec->isInvalidDecl())
    return true;

  ActionResult<CXXRecordDecl *> Pattern =
      getPatternForClassTemplateSpecialization(*this, PointOfInstantiation,
                                               ClassTemplateSpec, TSK);
  if (!Pattern.isUsable())
    return Pattern.isInvalid();

  return InstantiateClass(
      PointOfInstantiation, ClassTemplateSpec, Pattern.get(),
      getTemplateInstantiationArgs(ClassTemplateSpec), TSK, Complain);
}

void
Sema::InstantiateClassMembers(SourceLocation PointOfInstantiation,
                              CXXRecordDecl *Instantiation,
                        const MultiLevelTemplateArgumentList &TemplateArgs,
                              TemplateSpecializationKind TSK) {
  // FIXME: We need to notify the ASTMutationListener that we did all of these
  // things, in case we have an explicit instantiation definition in a PCM, a
  // module, or preamble, and the declaration is in an imported AST.
  assert(
      (TSK == TSK_ExplicitInstantiationDefinition ||
       TSK == TSK_ExplicitInstantiationDeclaration ||
       (TSK == TSK_ImplicitInstantiation && Instantiation->isLocalClass())) &&
      "Unexpected template specialization kind!");
  for (auto *D : Instantiation->decls()) {
    bool SuppressNew = false;
    if (auto *Function = dyn_cast<FunctionDecl>(D)) {
      if (FunctionDecl *Pattern =
              Function->getInstantiatedFromMemberFunction()) {

        if (Function->isIneligibleOrNotSelected())
          continue;

        if (Function->getTrailingRequiresClause()) {
          ConstraintSatisfaction Satisfaction;
          if (CheckFunctionConstraints(Function, Satisfaction) ||
              !Satisfaction.IsSatisfied) {
            continue;
          }
        }

        if (Function->hasAttr<ExcludeFromExplicitInstantiationAttr>())
          continue;

        MemberSpecializationInfo *MSInfo =
            Function->getMemberSpecializationInfo();
        assert(MSInfo && "No member specialization information?");
        if (MSInfo->getTemplateSpecializationKind()
                                                 == TSK_ExplicitSpecialization)
          continue;

        if (CheckSpecializationInstantiationRedecl(PointOfInstantiation, TSK,
                                                   Function,
                                        MSInfo->getTemplateSpecializationKind(),
                                              MSInfo->getPointOfInstantiation(),
                                                   SuppressNew) ||
            SuppressNew)
          continue;

        // C++11 [temp.explicit]p8:
        //   An explicit instantiation definition that names a class template
        //   specialization explicitly instantiates the class template
        //   specialization and is only an explicit instantiation definition
        //   of members whose definition is visible at the point of
        //   instantiation.
        if (TSK == TSK_ExplicitInstantiationDefinition && !Pattern->isDefined())
          continue;

        Function->setTemplateSpecializationKind(TSK, PointOfInstantiation);

        if (Function->isDefined()) {
          // Let the ASTConsumer know that this function has been explicitly
          // instantiated now, and its linkage might have changed.
          Consumer.HandleTopLevelDecl(DeclGroupRef(Function));
        } else if (TSK == TSK_ExplicitInstantiationDefinition) {
          InstantiateFunctionDefinition(PointOfInstantiation, Function);
        } else if (TSK == TSK_ImplicitInstantiation) {
          PendingLocalImplicitInstantiations.push_back(
              std::make_pair(Function, PointOfInstantiation));
        }
      }
    } else if (auto *Var = dyn_cast<VarDecl>(D)) {
      if (isa<VarTemplateSpecializationDecl>(Var))
        continue;

      if (Var->isStaticDataMember()) {
        if (Var->hasAttr<ExcludeFromExplicitInstantiationAttr>())
          continue;

        MemberSpecializationInfo *MSInfo = Var->getMemberSpecializationInfo();
        assert(MSInfo && "No member specialization information?");
        if (MSInfo->getTemplateSpecializationKind()
                                                 == TSK_ExplicitSpecialization)
          continue;

        if (CheckSpecializationInstantiationRedecl(PointOfInstantiation, TSK,
                                                   Var,
                                        MSInfo->getTemplateSpecializationKind(),
                                              MSInfo->getPointOfInstantiation(),
                                                   SuppressNew) ||
            SuppressNew)
          continue;

        if (TSK == TSK_ExplicitInstantiationDefinition) {
          // C++0x [temp.explicit]p8:
          //   An explicit instantiation definition that names a class template
          //   specialization explicitly instantiates the class template
          //   specialization and is only an explicit instantiation definition
          //   of members whose definition is visible at the point of
          //   instantiation.
          if (!Var->getInstantiatedFromStaticDataMember()->getDefinition())
            continue;

          Var->setTemplateSpecializationKind(TSK, PointOfInstantiation);
          InstantiateVariableDefinition(PointOfInstantiation, Var);
        } else {
          Var->setTemplateSpecializationKind(TSK, PointOfInstantiation);
        }
      }
    } else if (auto *Record = dyn_cast<CXXRecordDecl>(D)) {
      if (Record->hasAttr<ExcludeFromExplicitInstantiationAttr>())
        continue;

      // Always skip the injected-class-name, along with any
      // redeclarations of nested classes, since both would cause us
      // to try to instantiate the members of a class twice.
      // Skip closure types; they'll get instantiated when we instantiate
      // the corresponding lambda-expression.
      if (Record->isInjectedClassName() || Record->getPreviousDecl() ||
          Record->isLambda())
        continue;

      MemberSpecializationInfo *MSInfo = Record->getMemberSpecializationInfo();
      assert(MSInfo && "No member specialization information?");

      if (MSInfo->getTemplateSpecializationKind()
                                                == TSK_ExplicitSpecialization)
        continue;

      if (Context.getTargetInfo().getTriple().isOSWindows() &&
          TSK == TSK_ExplicitInstantiationDeclaration) {
        // On Windows, explicit instantiation decl of the outer class doesn't
        // affect the inner class. Typically extern template declarations are
        // used in combination with dll import/export annotations, but those
        // are not propagated from the outer class templates to inner classes.
        // Therefore, do not instantiate inner classes on this platform, so
        // that users don't end up with undefined symbols during linking.
        continue;
      }

      if (CheckSpecializationInstantiationRedecl(PointOfInstantiation, TSK,
                                                 Record,
                                        MSInfo->getTemplateSpecializationKind(),
                                              MSInfo->getPointOfInstantiation(),
                                                 SuppressNew) ||
          SuppressNew)
        continue;

      CXXRecordDecl *Pattern = Record->getInstantiatedFromMemberClass();
      assert(Pattern && "Missing instantiated-from-template information");

      if (!Record->getDefinition()) {
        if (!Pattern->getDefinition()) {
          // C++0x [temp.explicit]p8:
          //   An explicit instantiation definition that names a class template
          //   specialization explicitly instantiates the class template
          //   specialization and is only an explicit instantiation definition
          //   of members whose definition is visible at the point of
          //   instantiation.
          if (TSK == TSK_ExplicitInstantiationDeclaration) {
            MSInfo->setTemplateSpecializationKind(TSK);
            MSInfo->setPointOfInstantiation(PointOfInstantiation);
          }

          continue;
        }

        InstantiateClass(PointOfInstantiation, Record, Pattern,
                         TemplateArgs,
                         TSK);
      } else {
        if (TSK == TSK_ExplicitInstantiationDefinition &&
            Record->getTemplateSpecializationKind() ==
                TSK_ExplicitInstantiationDeclaration) {
          Record->setTemplateSpecializationKind(TSK);
          MarkVTableUsed(PointOfInstantiation, Record, true);
        }
      }

      Pattern = cast_or_null<CXXRecordDecl>(Record->getDefinition());
      if (Pattern)
        InstantiateClassMembers(PointOfInstantiation, Pattern, TemplateArgs,
                                TSK);
    } else if (auto *Enum = dyn_cast<EnumDecl>(D)) {
      MemberSpecializationInfo *MSInfo = Enum->getMemberSpecializationInfo();
      assert(MSInfo && "No member specialization information?");

      if (MSInfo->getTemplateSpecializationKind()
            == TSK_ExplicitSpecialization)
        continue;

      if (CheckSpecializationInstantiationRedecl(
            PointOfInstantiation, TSK, Enum,
            MSInfo->getTemplateSpecializationKind(),
            MSInfo->getPointOfInstantiation(), SuppressNew) ||
          SuppressNew)
        continue;

      if (Enum->getDefinition())
        continue;

      EnumDecl *Pattern = Enum->getTemplateInstantiationPattern();
      assert(Pattern && "Missing instantiated-from-template information");

      if (TSK == TSK_ExplicitInstantiationDefinition) {
        if (!Pattern->getDefinition())
          continue;

        InstantiateEnum(PointOfInstantiation, Enum, Pattern, TemplateArgs, TSK);
      } else {
        MSInfo->setTemplateSpecializationKind(TSK);
        MSInfo->setPointOfInstantiation(PointOfInstantiation);
      }
    } else if (auto *Field = dyn_cast<FieldDecl>(D)) {
      // No need to instantiate in-class initializers during explicit
      // instantiation.
      if (Field->hasInClassInitializer() && TSK == TSK_ImplicitInstantiation) {
        CXXRecordDecl *ClassPattern =
            Instantiation->getTemplateInstantiationPattern();
        DeclContext::lookup_result Lookup =
            ClassPattern->lookup(Field->getDeclName());
        FieldDecl *Pattern = Lookup.find_first<FieldDecl>();
        assert(Pattern);
        InstantiateInClassInitializer(PointOfInstantiation, Field, Pattern,
                                      TemplateArgs);
      }
    }
  }
}

void
Sema::InstantiateClassTemplateSpecializationMembers(
                                           SourceLocation PointOfInstantiation,
                            ClassTemplateSpecializationDecl *ClassTemplateSpec,
                                               TemplateSpecializationKind TSK) {
  // C++0x [temp.explicit]p7:
  //   An explicit instantiation that names a class template
  //   specialization is an explicit instantion of the same kind
  //   (declaration or definition) of each of its members (not
  //   including members inherited from base classes) that has not
  //   been previously explicitly specialized in the translation unit
  //   containing the explicit instantiation, except as described
  //   below.
  InstantiateClassMembers(PointOfInstantiation, ClassTemplateSpec,
                          getTemplateInstantiationArgs(ClassTemplateSpec),
                          TSK);
}

StmtResult
Sema::SubstStmt(Stmt *S, const MultiLevelTemplateArgumentList &TemplateArgs) {
  if (!S)
    return S;

  TemplateInstantiator Instantiator(*this, TemplateArgs,
                                    SourceLocation(),
                                    DeclarationName());
  return Instantiator.TransformStmt(S);
}

bool Sema::SubstTemplateArgument(
    const TemplateArgumentLoc &Input,
    const MultiLevelTemplateArgumentList &TemplateArgs,
    TemplateArgumentLoc &Output, SourceLocation Loc,
    const DeclarationName &Entity) {
  TemplateInstantiator Instantiator(*this, TemplateArgs, Loc, Entity);
  return Instantiator.TransformTemplateArgument(Input, Output);
}

bool Sema::SubstTemplateArguments(
    ArrayRef<TemplateArgumentLoc> Args,
    const MultiLevelTemplateArgumentList &TemplateArgs,
    TemplateArgumentListInfo &Out) {
  TemplateInstantiator Instantiator(*this, TemplateArgs, SourceLocation(),
                                    DeclarationName());
  return Instantiator.TransformTemplateArguments(Args.begin(), Args.end(), Out);
}

ExprResult
Sema::SubstExpr(Expr *E, const MultiLevelTemplateArgumentList &TemplateArgs) {
  if (!E)
    return E;

  TemplateInstantiator Instantiator(*this, TemplateArgs,
                                    SourceLocation(),
                                    DeclarationName());
  return Instantiator.TransformExpr(E);
}

ExprResult
Sema::SubstConstraintExpr(Expr *E,
                          const MultiLevelTemplateArgumentList &TemplateArgs) {
  // FIXME: should call SubstExpr directly if this function is equivalent or
  //        should it be different?
  return SubstExpr(E, TemplateArgs);
}

ExprResult Sema::SubstConstraintExprWithoutSatisfaction(
    Expr *E, const MultiLevelTemplateArgumentList &TemplateArgs) {
  if (!E)
    return E;

  TemplateInstantiator Instantiator(*this, TemplateArgs, SourceLocation(),
                                    DeclarationName());
  Instantiator.setEvaluateConstraints(false);
  return Instantiator.TransformExpr(E);
}

ExprResult Sema::SubstInitializer(Expr *Init,
                          const MultiLevelTemplateArgumentList &TemplateArgs,
                          bool CXXDirectInit) {
  TemplateInstantiator Instantiator(*this, TemplateArgs, SourceLocation(),
                                    DeclarationName());
  return Instantiator.TransformInitializer(Init, CXXDirectInit);
}

bool Sema::SubstExprs(ArrayRef<Expr *> Exprs, bool IsCall,
                      const MultiLevelTemplateArgumentList &TemplateArgs,
                      SmallVectorImpl<Expr *> &Outputs) {
  if (Exprs.empty())
    return false;

  TemplateInstantiator Instantiator(*this, TemplateArgs,
                                    SourceLocation(),
                                    DeclarationName());
  return Instantiator.TransformExprs(Exprs.data(), Exprs.size(),
                                     IsCall, Outputs);
}

NestedNameSpecifierLoc
Sema::SubstNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS,
                        const MultiLevelTemplateArgumentList &TemplateArgs) {
  if (!NNS)
    return NestedNameSpecifierLoc();

  TemplateInstantiator Instantiator(*this, TemplateArgs, NNS.getBeginLoc(),
                                    DeclarationName());
  return Instantiator.TransformNestedNameSpecifierLoc(NNS);
}

DeclarationNameInfo
Sema::SubstDeclarationNameInfo(const DeclarationNameInfo &NameInfo,
                         const MultiLevelTemplateArgumentList &TemplateArgs) {
  TemplateInstantiator Instantiator(*this, TemplateArgs, NameInfo.getLoc(),
                                    NameInfo.getName());
  return Instantiator.TransformDeclarationNameInfo(NameInfo);
}

TemplateName
Sema::SubstTemplateName(NestedNameSpecifierLoc QualifierLoc,
                        TemplateName Name, SourceLocation Loc,
                        const MultiLevelTemplateArgumentList &TemplateArgs) {
  TemplateInstantiator Instantiator(*this, TemplateArgs, Loc,
                                    DeclarationName());
  CXXScopeSpec SS;
  SS.Adopt(QualifierLoc);
  return Instantiator.TransformTemplateName(SS, Name, Loc);
}

static const Decl *getCanonicalParmVarDecl(const Decl *D) {
  // When storing ParmVarDecls in the local instantiation scope, we always
  // want to use the ParmVarDecl from the canonical function declaration,
  // since the map is then valid for any redeclaration or definition of that
  // function.
  if (const ParmVarDecl *PV = dyn_cast<ParmVarDecl>(D)) {
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(PV->getDeclContext())) {
      unsigned i = PV->getFunctionScopeIndex();
      // This parameter might be from a freestanding function type within the
      // function and isn't necessarily referring to one of FD's parameters.
      if (i < FD->getNumParams() && FD->getParamDecl(i) == PV)
        return FD->getCanonicalDecl()->getParamDecl(i);
    }
  }
  return D;
}


llvm::PointerUnion<Decl *, LocalInstantiationScope::DeclArgumentPack *> *
LocalInstantiationScope::findInstantiationOf(const Decl *D) {
  D = getCanonicalParmVarDecl(D);
  for (LocalInstantiationScope *Current = this; Current;
       Current = Current->Outer) {

    // Check if we found something within this scope.
    const Decl *CheckD = D;
    do {
      LocalDeclsMap::iterator Found = Current->LocalDecls.find(CheckD);
      if (Found != Current->LocalDecls.end())
        return &Found->second;

      // If this is a tag declaration, it's possible that we need to look for
      // a previous declaration.
      if (const TagDecl *Tag = dyn_cast<TagDecl>(CheckD))
        CheckD = Tag->getPreviousDecl();
      else
        CheckD = nullptr;
    } while (CheckD);

    // If we aren't combined with our outer scope, we're done.
    if (!Current->CombineWithOuterScope)
      break;
  }

  // If we're performing a partial substitution during template argument
  // deduction, we may not have values for template parameters yet.
  if (isa<NonTypeTemplateParmDecl>(D) || isa<TemplateTypeParmDecl>(D) ||
      isa<TemplateTemplateParmDecl>(D))
    return nullptr;

  // Local types referenced prior to definition may require instantiation.
  if (const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D))
    if (RD->isLocalClass())
      return nullptr;

  // Enumeration types referenced prior to definition may appear as a result of
  // error recovery.
  if (isa<EnumDecl>(D))
    return nullptr;

  // Materialized typedefs/type alias for implicit deduction guides may require
  // instantiation.
  if (isa<TypedefNameDecl>(D) &&
      isa<CXXDeductionGuideDecl>(D->getDeclContext()))
    return nullptr;

  // If we didn't find the decl, then we either have a sema bug, or we have a
  // forward reference to a label declaration.  Return null to indicate that
  // we have an uninstantiated label.
  assert(isa<LabelDecl>(D) && "declaration not instantiated in this scope");
  return nullptr;
}

void LocalInstantiationScope::InstantiatedLocal(const Decl *D, Decl *Inst) {
  D = getCanonicalParmVarDecl(D);
  llvm::PointerUnion<Decl *, DeclArgumentPack *> &Stored = LocalDecls[D];
  if (Stored.isNull()) {
#ifndef NDEBUG
    // It should not be present in any surrounding scope either.
    LocalInstantiationScope *Current = this;
    while (Current->CombineWithOuterScope && Current->Outer) {
      Current = Current->Outer;
      assert(!Current->LocalDecls.contains(D) &&
             "Instantiated local in inner and outer scopes");
    }
#endif
    Stored = Inst;
  } else if (DeclArgumentPack *Pack = Stored.dyn_cast<DeclArgumentPack *>()) {
    Pack->push_back(cast<VarDecl>(Inst));
  } else {
    assert(Stored.get<Decl *>() == Inst && "Already instantiated this local");
  }
}

void LocalInstantiationScope::InstantiatedLocalPackArg(const Decl *D,
                                                       VarDecl *Inst) {
  D = getCanonicalParmVarDecl(D);
  DeclArgumentPack *Pack = LocalDecls[D].get<DeclArgumentPack *>();
  Pack->push_back(Inst);
}

void LocalInstantiationScope::MakeInstantiatedLocalArgPack(const Decl *D) {
#ifndef NDEBUG
  // This should be the first time we've been told about this decl.
  for (LocalInstantiationScope *Current = this;
       Current && Current->CombineWithOuterScope; Current = Current->Outer)
    assert(!Current->LocalDecls.contains(D) &&
           "Creating local pack after instantiation of local");
#endif

  D = getCanonicalParmVarDecl(D);
  llvm::PointerUnion<Decl *, DeclArgumentPack *> &Stored = LocalDecls[D];
  DeclArgumentPack *Pack = new DeclArgumentPack;
  Stored = Pack;
  ArgumentPacks.push_back(Pack);
}

bool LocalInstantiationScope::isLocalPackExpansion(const Decl *D) {
  for (DeclArgumentPack *Pack : ArgumentPacks)
    if (llvm::is_contained(*Pack, D))
      return true;
  return false;
}

void LocalInstantiationScope::SetPartiallySubstitutedPack(NamedDecl *Pack,
                                          const TemplateArgument *ExplicitArgs,
                                                    unsigned NumExplicitArgs) {
  assert((!PartiallySubstitutedPack || PartiallySubstitutedPack == Pack) &&
         "Already have a partially-substituted pack");
  assert((!PartiallySubstitutedPack
          || NumArgsInPartiallySubstitutedPack == NumExplicitArgs) &&
         "Wrong number of arguments in partially-substituted pack");
  PartiallySubstitutedPack = Pack;
  ArgsInPartiallySubstitutedPack = ExplicitArgs;
  NumArgsInPartiallySubstitutedPack = NumExplicitArgs;
}

NamedDecl *LocalInstantiationScope::getPartiallySubstitutedPack(
                                         const TemplateArgument **ExplicitArgs,
                                              unsigned *NumExplicitArgs) const {
  if (ExplicitArgs)
    *ExplicitArgs = nullptr;
  if (NumExplicitArgs)
    *NumExplicitArgs = 0;

  for (const LocalInstantiationScope *Current = this; Current;
       Current = Current->Outer) {
    if (Current->PartiallySubstitutedPack) {
      if (ExplicitArgs)
        *ExplicitArgs = Current->ArgsInPartiallySubstitutedPack;
      if (NumExplicitArgs)
        *NumExplicitArgs = Current->NumArgsInPartiallySubstitutedPack;

      return Current->PartiallySubstitutedPack;
    }

    if (!Current->CombineWithOuterScope)
      break;
  }

  return nullptr;
}
