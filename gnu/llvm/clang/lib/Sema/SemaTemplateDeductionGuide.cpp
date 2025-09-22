//===- SemaTemplateDeductionGude.cpp - Template Argument Deduction---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements deduction guides for C++ class template argument
// deduction.
//
//===----------------------------------------------------------------------===//

#include "TreeTransform.h"
#include "TypeLocBuilder.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <optional>
#include <utility>

using namespace clang;
using namespace sema;

namespace {
/// Tree transform to "extract" a transformed type from a class template's
/// constructor to a deduction guide.
class ExtractTypeForDeductionGuide
    : public TreeTransform<ExtractTypeForDeductionGuide> {
  llvm::SmallVectorImpl<TypedefNameDecl *> &MaterializedTypedefs;
  ClassTemplateDecl *NestedPattern;
  const MultiLevelTemplateArgumentList *OuterInstantiationArgs;
  std::optional<TemplateDeclInstantiator> TypedefNameInstantiator;

public:
  typedef TreeTransform<ExtractTypeForDeductionGuide> Base;
  ExtractTypeForDeductionGuide(
      Sema &SemaRef,
      llvm::SmallVectorImpl<TypedefNameDecl *> &MaterializedTypedefs,
      ClassTemplateDecl *NestedPattern = nullptr,
      const MultiLevelTemplateArgumentList *OuterInstantiationArgs = nullptr)
      : Base(SemaRef), MaterializedTypedefs(MaterializedTypedefs),
        NestedPattern(NestedPattern),
        OuterInstantiationArgs(OuterInstantiationArgs) {
    if (OuterInstantiationArgs)
      TypedefNameInstantiator.emplace(
          SemaRef, SemaRef.getASTContext().getTranslationUnitDecl(),
          *OuterInstantiationArgs);
  }

  TypeSourceInfo *transform(TypeSourceInfo *TSI) { return TransformType(TSI); }

  /// Returns true if it's safe to substitute \p Typedef with
  /// \p OuterInstantiationArgs.
  bool mightReferToOuterTemplateParameters(TypedefNameDecl *Typedef) {
    if (!NestedPattern)
      return false;

    static auto WalkUp = [](DeclContext *DC, DeclContext *TargetDC) {
      if (DC->Equals(TargetDC))
        return true;
      while (DC->isRecord()) {
        if (DC->Equals(TargetDC))
          return true;
        DC = DC->getParent();
      }
      return false;
    };

    if (WalkUp(Typedef->getDeclContext(), NestedPattern->getTemplatedDecl()))
      return true;
    if (WalkUp(NestedPattern->getTemplatedDecl(), Typedef->getDeclContext()))
      return true;
    return false;
  }

  QualType
  RebuildTemplateSpecializationType(TemplateName Template,
                                    SourceLocation TemplateNameLoc,
                                    TemplateArgumentListInfo &TemplateArgs) {
    if (!OuterInstantiationArgs ||
        !isa_and_present<TypeAliasTemplateDecl>(Template.getAsTemplateDecl()))
      return Base::RebuildTemplateSpecializationType(Template, TemplateNameLoc,
                                                     TemplateArgs);

    auto *TATD = cast<TypeAliasTemplateDecl>(Template.getAsTemplateDecl());
    auto *Pattern = TATD;
    while (Pattern->getInstantiatedFromMemberTemplate())
      Pattern = Pattern->getInstantiatedFromMemberTemplate();
    if (!mightReferToOuterTemplateParameters(Pattern->getTemplatedDecl()))
      return Base::RebuildTemplateSpecializationType(Template, TemplateNameLoc,
                                                     TemplateArgs);

    Decl *NewD =
        TypedefNameInstantiator->InstantiateTypeAliasTemplateDecl(TATD);
    if (!NewD)
      return QualType();

    auto *NewTATD = cast<TypeAliasTemplateDecl>(NewD);
    MaterializedTypedefs.push_back(NewTATD->getTemplatedDecl());

    return Base::RebuildTemplateSpecializationType(
        TemplateName(NewTATD), TemplateNameLoc, TemplateArgs);
  }

  QualType TransformTypedefType(TypeLocBuilder &TLB, TypedefTypeLoc TL) {
    ASTContext &Context = SemaRef.getASTContext();
    TypedefNameDecl *OrigDecl = TL.getTypedefNameDecl();
    TypedefNameDecl *Decl = OrigDecl;
    // Transform the underlying type of the typedef and clone the Decl only if
    // the typedef has a dependent context.
    bool InDependentContext = OrigDecl->getDeclContext()->isDependentContext();

    // A typedef/alias Decl within the NestedPattern may reference the outer
    // template parameters. They're substituted with corresponding instantiation
    // arguments here and in RebuildTemplateSpecializationType() above.
    // Otherwise, we would have a CTAD guide with "dangling" template
    // parameters.
    // For example,
    //   template <class T> struct Outer {
    //     using Alias = S<T>;
    //     template <class U> struct Inner {
    //       Inner(Alias);
    //     };
    //   };
    if (OuterInstantiationArgs && InDependentContext &&
        TL.getTypePtr()->isInstantiationDependentType()) {
      Decl = cast_if_present<TypedefNameDecl>(
          TypedefNameInstantiator->InstantiateTypedefNameDecl(
              OrigDecl, /*IsTypeAlias=*/isa<TypeAliasDecl>(OrigDecl)));
      if (!Decl)
        return QualType();
      MaterializedTypedefs.push_back(Decl);
    } else if (InDependentContext) {
      TypeLocBuilder InnerTLB;
      QualType Transformed =
          TransformType(InnerTLB, OrigDecl->getTypeSourceInfo()->getTypeLoc());
      TypeSourceInfo *TSI = InnerTLB.getTypeSourceInfo(Context, Transformed);
      if (isa<TypeAliasDecl>(OrigDecl))
        Decl = TypeAliasDecl::Create(
            Context, Context.getTranslationUnitDecl(), OrigDecl->getBeginLoc(),
            OrigDecl->getLocation(), OrigDecl->getIdentifier(), TSI);
      else {
        assert(isa<TypedefDecl>(OrigDecl) && "Not a Type alias or typedef");
        Decl = TypedefDecl::Create(
            Context, Context.getTranslationUnitDecl(), OrigDecl->getBeginLoc(),
            OrigDecl->getLocation(), OrigDecl->getIdentifier(), TSI);
      }
      MaterializedTypedefs.push_back(Decl);
    }

    QualType TDTy = Context.getTypedefType(Decl);
    TypedefTypeLoc TypedefTL = TLB.push<TypedefTypeLoc>(TDTy);
    TypedefTL.setNameLoc(TL.getNameLoc());

    return TDTy;
  }
};

// Build a deduction guide using the provided information.
//
// A deduction guide can be either a template or a non-template function
// declaration. If \p TemplateParams is null, a non-template function
// declaration will be created.
NamedDecl *buildDeductionGuide(
    Sema &SemaRef, TemplateDecl *OriginalTemplate,
    TemplateParameterList *TemplateParams, CXXConstructorDecl *Ctor,
    ExplicitSpecifier ES, TypeSourceInfo *TInfo, SourceLocation LocStart,
    SourceLocation Loc, SourceLocation LocEnd, bool IsImplicit,
    llvm::ArrayRef<TypedefNameDecl *> MaterializedTypedefs = {}) {
  DeclContext *DC = OriginalTemplate->getDeclContext();
  auto DeductionGuideName =
      SemaRef.Context.DeclarationNames.getCXXDeductionGuideName(
          OriginalTemplate);

  DeclarationNameInfo Name(DeductionGuideName, Loc);
  ArrayRef<ParmVarDecl *> Params =
      TInfo->getTypeLoc().castAs<FunctionProtoTypeLoc>().getParams();

  // Build the implicit deduction guide template.
  auto *Guide =
      CXXDeductionGuideDecl::Create(SemaRef.Context, DC, LocStart, ES, Name,
                                    TInfo->getType(), TInfo, LocEnd, Ctor);
  Guide->setImplicit(IsImplicit);
  Guide->setParams(Params);

  for (auto *Param : Params)
    Param->setDeclContext(Guide);
  for (auto *TD : MaterializedTypedefs)
    TD->setDeclContext(Guide);
  if (isa<CXXRecordDecl>(DC))
    Guide->setAccess(AS_public);

  if (!TemplateParams) {
    DC->addDecl(Guide);
    return Guide;
  }

  auto *GuideTemplate = FunctionTemplateDecl::Create(
      SemaRef.Context, DC, Loc, DeductionGuideName, TemplateParams, Guide);
  GuideTemplate->setImplicit(IsImplicit);
  Guide->setDescribedFunctionTemplate(GuideTemplate);

  if (isa<CXXRecordDecl>(DC))
    GuideTemplate->setAccess(AS_public);

  DC->addDecl(GuideTemplate);
  return GuideTemplate;
}

// Transform a given template type parameter `TTP`.
TemplateTypeParmDecl *
transformTemplateTypeParam(Sema &SemaRef, DeclContext *DC,
                           TemplateTypeParmDecl *TTP,
                           MultiLevelTemplateArgumentList &Args,
                           unsigned NewDepth, unsigned NewIndex) {
  // TemplateTypeParmDecl's index cannot be changed after creation, so
  // substitute it directly.
  auto *NewTTP = TemplateTypeParmDecl::Create(
      SemaRef.Context, DC, TTP->getBeginLoc(), TTP->getLocation(), NewDepth,
      NewIndex, TTP->getIdentifier(), TTP->wasDeclaredWithTypename(),
      TTP->isParameterPack(), TTP->hasTypeConstraint(),
      TTP->isExpandedParameterPack()
          ? std::optional<unsigned>(TTP->getNumExpansionParameters())
          : std::nullopt);
  if (const auto *TC = TTP->getTypeConstraint())
    SemaRef.SubstTypeConstraint(NewTTP, TC, Args,
                                /*EvaluateConstraint=*/true);
  if (TTP->hasDefaultArgument()) {
    TemplateArgumentLoc InstantiatedDefaultArg;
    if (!SemaRef.SubstTemplateArgument(
            TTP->getDefaultArgument(), Args, InstantiatedDefaultArg,
            TTP->getDefaultArgumentLoc(), TTP->getDeclName()))
      NewTTP->setDefaultArgument(SemaRef.Context, InstantiatedDefaultArg);
  }
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(TTP, NewTTP);
  return NewTTP;
}
// Similar to above, but for non-type template or template template parameters.
template <typename NonTypeTemplateOrTemplateTemplateParmDecl>
NonTypeTemplateOrTemplateTemplateParmDecl *
transformTemplateParam(Sema &SemaRef, DeclContext *DC,
                       NonTypeTemplateOrTemplateTemplateParmDecl *OldParam,
                       MultiLevelTemplateArgumentList &Args, unsigned NewIndex,
                       unsigned NewDepth) {
  // Ask the template instantiator to do the heavy lifting for us, then adjust
  // the index of the parameter once it's done.
  auto *NewParam = cast<NonTypeTemplateOrTemplateTemplateParmDecl>(
      SemaRef.SubstDecl(OldParam, DC, Args));
  NewParam->setPosition(NewIndex);
  NewParam->setDepth(NewDepth);
  return NewParam;
}

/// Transform to convert portions of a constructor declaration into the
/// corresponding deduction guide, per C++1z [over.match.class.deduct]p1.
struct ConvertConstructorToDeductionGuideTransform {
  ConvertConstructorToDeductionGuideTransform(Sema &S,
                                              ClassTemplateDecl *Template)
      : SemaRef(S), Template(Template) {
    // If the template is nested, then we need to use the original
    // pattern to iterate over the constructors.
    ClassTemplateDecl *Pattern = Template;
    while (Pattern->getInstantiatedFromMemberTemplate()) {
      if (Pattern->isMemberSpecialization())
        break;
      Pattern = Pattern->getInstantiatedFromMemberTemplate();
      NestedPattern = Pattern;
    }

    if (NestedPattern)
      OuterInstantiationArgs = SemaRef.getTemplateInstantiationArgs(Template);
  }

  Sema &SemaRef;
  ClassTemplateDecl *Template;
  ClassTemplateDecl *NestedPattern = nullptr;

  DeclContext *DC = Template->getDeclContext();
  CXXRecordDecl *Primary = Template->getTemplatedDecl();
  DeclarationName DeductionGuideName =
      SemaRef.Context.DeclarationNames.getCXXDeductionGuideName(Template);

  QualType DeducedType = SemaRef.Context.getTypeDeclType(Primary);

  // Index adjustment to apply to convert depth-1 template parameters into
  // depth-0 template parameters.
  unsigned Depth1IndexAdjustment = Template->getTemplateParameters()->size();

  // Instantiation arguments for the outermost depth-1 templates
  // when the template is nested
  MultiLevelTemplateArgumentList OuterInstantiationArgs;

  /// Transform a constructor declaration into a deduction guide.
  NamedDecl *transformConstructor(FunctionTemplateDecl *FTD,
                                  CXXConstructorDecl *CD) {
    SmallVector<TemplateArgument, 16> SubstArgs;

    LocalInstantiationScope Scope(SemaRef);

    // C++ [over.match.class.deduct]p1:
    // -- For each constructor of the class template designated by the
    //    template-name, a function template with the following properties:

    //    -- The template parameters are the template parameters of the class
    //       template followed by the template parameters (including default
    //       template arguments) of the constructor, if any.
    TemplateParameterList *TemplateParams =
        SemaRef.GetTemplateParameterList(Template);
    if (FTD) {
      TemplateParameterList *InnerParams = FTD->getTemplateParameters();
      SmallVector<NamedDecl *, 16> AllParams;
      SmallVector<TemplateArgument, 16> Depth1Args;
      AllParams.reserve(TemplateParams->size() + InnerParams->size());
      AllParams.insert(AllParams.begin(), TemplateParams->begin(),
                       TemplateParams->end());
      SubstArgs.reserve(InnerParams->size());
      Depth1Args.reserve(InnerParams->size());

      // Later template parameters could refer to earlier ones, so build up
      // a list of substituted template arguments as we go.
      for (NamedDecl *Param : *InnerParams) {
        MultiLevelTemplateArgumentList Args;
        Args.setKind(TemplateSubstitutionKind::Rewrite);
        Args.addOuterTemplateArguments(Depth1Args);
        Args.addOuterRetainedLevel();
        if (NestedPattern)
          Args.addOuterRetainedLevels(NestedPattern->getTemplateDepth());
        NamedDecl *NewParam = transformTemplateParameter(Param, Args);
        if (!NewParam)
          return nullptr;
        // Constraints require that we substitute depth-1 arguments
        // to match depths when substituted for evaluation later
        Depth1Args.push_back(SemaRef.Context.getInjectedTemplateArg(NewParam));

        if (NestedPattern) {
          TemplateDeclInstantiator Instantiator(SemaRef, DC,
                                                OuterInstantiationArgs);
          Instantiator.setEvaluateConstraints(false);
          SemaRef.runWithSufficientStackSpace(NewParam->getLocation(), [&] {
            NewParam = cast<NamedDecl>(Instantiator.Visit(NewParam));
          });
        }

        assert(NewParam->getTemplateDepth() == 0 &&
               "Unexpected template parameter depth");

        AllParams.push_back(NewParam);
        SubstArgs.push_back(SemaRef.Context.getInjectedTemplateArg(NewParam));
      }

      // Substitute new template parameters into requires-clause if present.
      Expr *RequiresClause = nullptr;
      if (Expr *InnerRC = InnerParams->getRequiresClause()) {
        MultiLevelTemplateArgumentList Args;
        Args.setKind(TemplateSubstitutionKind::Rewrite);
        Args.addOuterTemplateArguments(Depth1Args);
        Args.addOuterRetainedLevel();
        if (NestedPattern)
          Args.addOuterRetainedLevels(NestedPattern->getTemplateDepth());
        ExprResult E = SemaRef.SubstExpr(InnerRC, Args);
        if (E.isInvalid())
          return nullptr;
        RequiresClause = E.getAs<Expr>();
      }

      TemplateParams = TemplateParameterList::Create(
          SemaRef.Context, InnerParams->getTemplateLoc(),
          InnerParams->getLAngleLoc(), AllParams, InnerParams->getRAngleLoc(),
          RequiresClause);
    }

    // If we built a new template-parameter-list, track that we need to
    // substitute references to the old parameters into references to the
    // new ones.
    MultiLevelTemplateArgumentList Args;
    Args.setKind(TemplateSubstitutionKind::Rewrite);
    if (FTD) {
      Args.addOuterTemplateArguments(SubstArgs);
      Args.addOuterRetainedLevel();
    }

    FunctionProtoTypeLoc FPTL = CD->getTypeSourceInfo()
                                    ->getTypeLoc()
                                    .getAsAdjusted<FunctionProtoTypeLoc>();
    assert(FPTL && "no prototype for constructor declaration");

    // Transform the type of the function, adjusting the return type and
    // replacing references to the old parameters with references to the
    // new ones.
    TypeLocBuilder TLB;
    SmallVector<ParmVarDecl *, 8> Params;
    SmallVector<TypedefNameDecl *, 4> MaterializedTypedefs;
    QualType NewType = transformFunctionProtoType(TLB, FPTL, Params, Args,
                                                  MaterializedTypedefs);
    if (NewType.isNull())
      return nullptr;
    TypeSourceInfo *NewTInfo = TLB.getTypeSourceInfo(SemaRef.Context, NewType);

    return buildDeductionGuide(
        SemaRef, Template, TemplateParams, CD, CD->getExplicitSpecifier(),
        NewTInfo, CD->getBeginLoc(), CD->getLocation(), CD->getEndLoc(),
        /*IsImplicit=*/true, MaterializedTypedefs);
  }

  /// Build a deduction guide with the specified parameter types.
  NamedDecl *buildSimpleDeductionGuide(MutableArrayRef<QualType> ParamTypes) {
    SourceLocation Loc = Template->getLocation();

    // Build the requested type.
    FunctionProtoType::ExtProtoInfo EPI;
    EPI.HasTrailingReturn = true;
    QualType Result = SemaRef.BuildFunctionType(DeducedType, ParamTypes, Loc,
                                                DeductionGuideName, EPI);
    TypeSourceInfo *TSI = SemaRef.Context.getTrivialTypeSourceInfo(Result, Loc);
    if (NestedPattern)
      TSI = SemaRef.SubstType(TSI, OuterInstantiationArgs, Loc,
                              DeductionGuideName);

    if (!TSI)
      return nullptr;

    FunctionProtoTypeLoc FPTL =
        TSI->getTypeLoc().castAs<FunctionProtoTypeLoc>();

    // Build the parameters, needed during deduction / substitution.
    SmallVector<ParmVarDecl *, 4> Params;
    for (auto T : ParamTypes) {
      auto *TSI = SemaRef.Context.getTrivialTypeSourceInfo(T, Loc);
      if (NestedPattern)
        TSI = SemaRef.SubstType(TSI, OuterInstantiationArgs, Loc,
                                DeclarationName());
      if (!TSI)
        return nullptr;

      ParmVarDecl *NewParam =
          ParmVarDecl::Create(SemaRef.Context, DC, Loc, Loc, nullptr,
                              TSI->getType(), TSI, SC_None, nullptr);
      NewParam->setScopeInfo(0, Params.size());
      FPTL.setParam(Params.size(), NewParam);
      Params.push_back(NewParam);
    }

    return buildDeductionGuide(
        SemaRef, Template, SemaRef.GetTemplateParameterList(Template), nullptr,
        ExplicitSpecifier(), TSI, Loc, Loc, Loc, /*IsImplicit=*/true);
  }

private:
  /// Transform a constructor template parameter into a deduction guide template
  /// parameter, rebuilding any internal references to earlier parameters and
  /// renumbering as we go.
  NamedDecl *transformTemplateParameter(NamedDecl *TemplateParam,
                                        MultiLevelTemplateArgumentList &Args) {
    if (auto *TTP = dyn_cast<TemplateTypeParmDecl>(TemplateParam))
      return transformTemplateTypeParam(
          SemaRef, DC, TTP, Args, TTP->getDepth() - 1,
          Depth1IndexAdjustment + TTP->getIndex());
    if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(TemplateParam))
      return transformTemplateParam(SemaRef, DC, TTP, Args,
                                    Depth1IndexAdjustment + TTP->getIndex(),
                                    TTP->getDepth() - 1);
    auto *NTTP = cast<NonTypeTemplateParmDecl>(TemplateParam);
    return transformTemplateParam(SemaRef, DC, NTTP, Args,
                                  Depth1IndexAdjustment + NTTP->getIndex(),
                                  NTTP->getDepth() - 1);
  }

  QualType transformFunctionProtoType(
      TypeLocBuilder &TLB, FunctionProtoTypeLoc TL,
      SmallVectorImpl<ParmVarDecl *> &Params,
      MultiLevelTemplateArgumentList &Args,
      SmallVectorImpl<TypedefNameDecl *> &MaterializedTypedefs) {
    SmallVector<QualType, 4> ParamTypes;
    const FunctionProtoType *T = TL.getTypePtr();

    //    -- The types of the function parameters are those of the constructor.
    for (auto *OldParam : TL.getParams()) {
      ParmVarDecl *NewParam = OldParam;
      // Given
      //   template <class T> struct C {
      //     template <class U> struct D {
      //       template <class V> D(U, V);
      //     };
      //   };
      // First, transform all the references to template parameters that are
      // defined outside of the surrounding class template. That is T in the
      // above example.
      if (NestedPattern) {
        NewParam = transformFunctionTypeParam(
            NewParam, OuterInstantiationArgs, MaterializedTypedefs,
            /*TransformingOuterPatterns=*/true);
        if (!NewParam)
          return QualType();
      }
      // Then, transform all the references to template parameters that are
      // defined at the class template and the constructor. In this example,
      // they're U and V, respectively.
      NewParam =
          transformFunctionTypeParam(NewParam, Args, MaterializedTypedefs,
                                     /*TransformingOuterPatterns=*/false);
      if (!NewParam)
        return QualType();
      ParamTypes.push_back(NewParam->getType());
      Params.push_back(NewParam);
    }

    //    -- The return type is the class template specialization designated by
    //       the template-name and template arguments corresponding to the
    //       template parameters obtained from the class template.
    //
    // We use the injected-class-name type of the primary template instead.
    // This has the convenient property that it is different from any type that
    // the user can write in a deduction-guide (because they cannot enter the
    // context of the template), so implicit deduction guides can never collide
    // with explicit ones.
    QualType ReturnType = DeducedType;
    TLB.pushTypeSpec(ReturnType).setNameLoc(Primary->getLocation());

    // Resolving a wording defect, we also inherit the variadicness of the
    // constructor.
    FunctionProtoType::ExtProtoInfo EPI;
    EPI.Variadic = T->isVariadic();
    EPI.HasTrailingReturn = true;

    QualType Result = SemaRef.BuildFunctionType(
        ReturnType, ParamTypes, TL.getBeginLoc(), DeductionGuideName, EPI);
    if (Result.isNull())
      return QualType();

    FunctionProtoTypeLoc NewTL = TLB.push<FunctionProtoTypeLoc>(Result);
    NewTL.setLocalRangeBegin(TL.getLocalRangeBegin());
    NewTL.setLParenLoc(TL.getLParenLoc());
    NewTL.setRParenLoc(TL.getRParenLoc());
    NewTL.setExceptionSpecRange(SourceRange());
    NewTL.setLocalRangeEnd(TL.getLocalRangeEnd());
    for (unsigned I = 0, E = NewTL.getNumParams(); I != E; ++I)
      NewTL.setParam(I, Params[I]);

    return Result;
  }

  ParmVarDecl *transformFunctionTypeParam(
      ParmVarDecl *OldParam, MultiLevelTemplateArgumentList &Args,
      llvm::SmallVectorImpl<TypedefNameDecl *> &MaterializedTypedefs,
      bool TransformingOuterPatterns) {
    TypeSourceInfo *OldDI = OldParam->getTypeSourceInfo();
    TypeSourceInfo *NewDI;
    if (auto PackTL = OldDI->getTypeLoc().getAs<PackExpansionTypeLoc>()) {
      // Expand out the one and only element in each inner pack.
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, 0);
      NewDI =
          SemaRef.SubstType(PackTL.getPatternLoc(), Args,
                            OldParam->getLocation(), OldParam->getDeclName());
      if (!NewDI)
        return nullptr;
      NewDI =
          SemaRef.CheckPackExpansion(NewDI, PackTL.getEllipsisLoc(),
                                     PackTL.getTypePtr()->getNumExpansions());
    } else
      NewDI = SemaRef.SubstType(OldDI, Args, OldParam->getLocation(),
                                OldParam->getDeclName());
    if (!NewDI)
      return nullptr;

    // Extract the type. This (for instance) replaces references to typedef
    // members of the current instantiations with the definitions of those
    // typedefs, avoiding triggering instantiation of the deduced type during
    // deduction.
    NewDI = ExtractTypeForDeductionGuide(
                SemaRef, MaterializedTypedefs, NestedPattern,
                TransformingOuterPatterns ? &Args : nullptr)
                .transform(NewDI);

    // Resolving a wording defect, we also inherit default arguments from the
    // constructor.
    ExprResult NewDefArg;
    if (OldParam->hasDefaultArg()) {
      // We don't care what the value is (we won't use it); just create a
      // placeholder to indicate there is a default argument.
      QualType ParamTy = NewDI->getType();
      NewDefArg = new (SemaRef.Context)
          OpaqueValueExpr(OldParam->getDefaultArgRange().getBegin(),
                          ParamTy.getNonLValueExprType(SemaRef.Context),
                          ParamTy->isLValueReferenceType()   ? VK_LValue
                          : ParamTy->isRValueReferenceType() ? VK_XValue
                                                             : VK_PRValue);
    }
    // Handle arrays and functions decay.
    auto NewType = NewDI->getType();
    if (NewType->isArrayType() || NewType->isFunctionType())
      NewType = SemaRef.Context.getDecayedType(NewType);

    ParmVarDecl *NewParam = ParmVarDecl::Create(
        SemaRef.Context, DC, OldParam->getInnerLocStart(),
        OldParam->getLocation(), OldParam->getIdentifier(), NewType, NewDI,
        OldParam->getStorageClass(), NewDefArg.get());
    NewParam->setScopeInfo(OldParam->getFunctionScopeDepth(),
                           OldParam->getFunctionScopeIndex());
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(OldParam, NewParam);
    return NewParam;
  }
};

unsigned getTemplateParameterDepth(NamedDecl *TemplateParam) {
  if (auto *TTP = dyn_cast<TemplateTypeParmDecl>(TemplateParam))
    return TTP->getDepth();
  if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(TemplateParam))
    return TTP->getDepth();
  if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(TemplateParam))
    return NTTP->getDepth();
  llvm_unreachable("Unhandled template parameter types");
}

unsigned getTemplateParameterIndex(NamedDecl *TemplateParam) {
  if (auto *TTP = dyn_cast<TemplateTypeParmDecl>(TemplateParam))
    return TTP->getIndex();
  if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(TemplateParam))
    return TTP->getIndex();
  if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(TemplateParam))
    return NTTP->getIndex();
  llvm_unreachable("Unhandled template parameter types");
}

// Find all template parameters that appear in the given DeducedArgs.
// Return the indices of the template parameters in the TemplateParams.
SmallVector<unsigned> TemplateParamsReferencedInTemplateArgumentList(
    const TemplateParameterList *TemplateParamsList,
    ArrayRef<TemplateArgument> DeducedArgs) {
  struct TemplateParamsReferencedFinder
      : public RecursiveASTVisitor<TemplateParamsReferencedFinder> {
    const TemplateParameterList *TemplateParamList;
    llvm::BitVector ReferencedTemplateParams;

    TemplateParamsReferencedFinder(
        const TemplateParameterList *TemplateParamList)
        : TemplateParamList(TemplateParamList),
          ReferencedTemplateParams(TemplateParamList->size()) {}

    bool VisitTemplateTypeParmType(TemplateTypeParmType *TTP) {
      // We use the index and depth to retrieve the corresponding template
      // parameter from the parameter list, which is more robost.
      Mark(TTP->getDepth(), TTP->getIndex());
      return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr *DRE) {
      MarkAppeared(DRE->getFoundDecl());
      return true;
    }

    bool TraverseTemplateName(TemplateName Template) {
      if (auto *TD = Template.getAsTemplateDecl())
        MarkAppeared(TD);
      return RecursiveASTVisitor::TraverseTemplateName(Template);
    }

    void MarkAppeared(NamedDecl *ND) {
      if (llvm::isa<NonTypeTemplateParmDecl, TemplateTypeParmDecl,
                    TemplateTemplateParmDecl>(ND))
        Mark(getTemplateParameterDepth(ND), getTemplateParameterIndex(ND));
    }
    void Mark(unsigned Depth, unsigned Index) {
      if (Index < TemplateParamList->size() &&
          TemplateParamList->getParam(Index)->getTemplateDepth() == Depth)
        ReferencedTemplateParams.set(Index);
    }
  };
  TemplateParamsReferencedFinder Finder(TemplateParamsList);
  Finder.TraverseTemplateArguments(DeducedArgs);

  SmallVector<unsigned> Results;
  for (unsigned Index = 0; Index < TemplateParamsList->size(); ++Index) {
    if (Finder.ReferencedTemplateParams[Index])
      Results.push_back(Index);
  }
  return Results;
}

bool hasDeclaredDeductionGuides(DeclarationName Name, DeclContext *DC) {
  // Check whether we've already declared deduction guides for this template.
  // FIXME: Consider storing a flag on the template to indicate this.
  assert(Name.getNameKind() ==
             DeclarationName::NameKind::CXXDeductionGuideName &&
         "name must be a deduction guide name");
  auto Existing = DC->lookup(Name);
  for (auto *D : Existing)
    if (D->isImplicit())
      return true;
  return false;
}

NamedDecl *transformTemplateParameter(Sema &SemaRef, DeclContext *DC,
                                      NamedDecl *TemplateParam,
                                      MultiLevelTemplateArgumentList &Args,
                                      unsigned NewIndex, unsigned NewDepth) {
  if (auto *TTP = dyn_cast<TemplateTypeParmDecl>(TemplateParam))
    return transformTemplateTypeParam(SemaRef, DC, TTP, Args, NewDepth,
                                      NewIndex);
  if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(TemplateParam))
    return transformTemplateParam(SemaRef, DC, TTP, Args, NewIndex, NewDepth);
  if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(TemplateParam))
    return transformTemplateParam(SemaRef, DC, NTTP, Args, NewIndex, NewDepth);
  llvm_unreachable("Unhandled template parameter types");
}

// Build the associated constraints for the alias deduction guides.
// C++ [over.match.class.deduct]p3.3:
//   The associated constraints ([temp.constr.decl]) are the conjunction of the
//   associated constraints of g and a constraint that is satisfied if and only
//   if the arguments of A are deducible (see below) from the return type.
//
// The return result is expected to be the require-clause for the synthesized
// alias deduction guide.
Expr *
buildAssociatedConstraints(Sema &SemaRef, FunctionTemplateDecl *F,
                           TypeAliasTemplateDecl *AliasTemplate,
                           ArrayRef<DeducedTemplateArgument> DeduceResults,
                           unsigned FirstUndeducedParamIdx, Expr *IsDeducible) {
  Expr *RC = F->getTemplateParameters()->getRequiresClause();
  if (!RC)
    return IsDeducible;

  ASTContext &Context = SemaRef.Context;
  LocalInstantiationScope Scope(SemaRef);

  // In the clang AST, constraint nodes are deliberately not instantiated unless
  // they are actively being evaluated. Consequently, occurrences of template
  // parameters in the require-clause expression have a subtle "depth"
  // difference compared to normal occurrences in places, such as function
  // parameters. When transforming the require-clause, we must take this
  // distinction into account:
  //
  //   1) In the transformed require-clause, occurrences of template parameters
  //   must use the "uninstantiated" depth;
  //   2) When substituting on the require-clause expr of the underlying
  //   deduction guide, we must use the entire set of template argument lists;
  //
  // It's important to note that we're performing this transformation on an
  // *instantiated* AliasTemplate.

  // For 1), if the alias template is nested within a class template, we
  // calcualte the 'uninstantiated' depth by adding the substitution level back.
  unsigned AdjustDepth = 0;
  if (auto *PrimaryTemplate =
          AliasTemplate->getInstantiatedFromMemberTemplate())
    AdjustDepth = PrimaryTemplate->getTemplateDepth();

  // We rebuild all template parameters with the uninstantiated depth, and
  // build template arguments refer to them.
  SmallVector<TemplateArgument> AdjustedAliasTemplateArgs;

  for (auto *TP : *AliasTemplate->getTemplateParameters()) {
    // Rebuild any internal references to earlier parameters and reindex
    // as we go.
    MultiLevelTemplateArgumentList Args;
    Args.setKind(TemplateSubstitutionKind::Rewrite);
    Args.addOuterTemplateArguments(AdjustedAliasTemplateArgs);
    NamedDecl *NewParam = transformTemplateParameter(
        SemaRef, AliasTemplate->getDeclContext(), TP, Args,
        /*NewIndex=*/AdjustedAliasTemplateArgs.size(),
        getTemplateParameterDepth(TP) + AdjustDepth);

    TemplateArgument NewTemplateArgument =
        Context.getInjectedTemplateArg(NewParam);
    AdjustedAliasTemplateArgs.push_back(NewTemplateArgument);
  }
  // Template arguments used to transform the template arguments in
  // DeducedResults.
  SmallVector<TemplateArgument> TemplateArgsForBuildingRC(
      F->getTemplateParameters()->size());
  // Transform the transformed template args
  MultiLevelTemplateArgumentList Args;
  Args.setKind(TemplateSubstitutionKind::Rewrite);
  Args.addOuterTemplateArguments(AdjustedAliasTemplateArgs);

  for (unsigned Index = 0; Index < DeduceResults.size(); ++Index) {
    const auto &D = DeduceResults[Index];
    if (D.isNull()) { // non-deduced template parameters of f
      NamedDecl *TP = F->getTemplateParameters()->getParam(Index);
      MultiLevelTemplateArgumentList Args;
      Args.setKind(TemplateSubstitutionKind::Rewrite);
      Args.addOuterTemplateArguments(TemplateArgsForBuildingRC);
      // Rebuild the template parameter with updated depth and index.
      NamedDecl *NewParam = transformTemplateParameter(
          SemaRef, F->getDeclContext(), TP, Args,
          /*NewIndex=*/FirstUndeducedParamIdx,
          getTemplateParameterDepth(TP) + AdjustDepth);
      FirstUndeducedParamIdx += 1;
      assert(TemplateArgsForBuildingRC[Index].isNull());
      TemplateArgsForBuildingRC[Index] =
          Context.getInjectedTemplateArg(NewParam);
      continue;
    }
    TemplateArgumentLoc Input =
        SemaRef.getTrivialTemplateArgumentLoc(D, QualType(), SourceLocation{});
    TemplateArgumentLoc Output;
    if (!SemaRef.SubstTemplateArgument(Input, Args, Output)) {
      assert(TemplateArgsForBuildingRC[Index].isNull() &&
             "InstantiatedArgs must be null before setting");
      TemplateArgsForBuildingRC[Index] = Output.getArgument();
    }
  }

  // A list of template arguments for transforming the require-clause of F.
  // It must contain the entire set of template argument lists.
  MultiLevelTemplateArgumentList ArgsForBuildingRC;
  ArgsForBuildingRC.setKind(clang::TemplateSubstitutionKind::Rewrite);
  ArgsForBuildingRC.addOuterTemplateArguments(TemplateArgsForBuildingRC);
  // For 2), if the underlying deduction guide F is nested in a class template,
  // we need the entire template argument list, as the constraint AST in the
  // require-clause of F remains completely uninstantiated.
  //
  // For example:
  //   template <typename T> // depth 0
  //   struct Outer {
  //      template <typename U>
  //      struct Foo { Foo(U); };
  //
  //      template <typename U> // depth 1
  //      requires C<U>
  //      Foo(U) -> Foo<int>;
  //   };
  //   template <typename U>
  //   using AFoo = Outer<int>::Foo<U>;
  //
  // In this scenario, the deduction guide for `Foo` inside `Outer<int>`:
  //   - The occurrence of U in the require-expression is [depth:1, index:0]
  //   - The occurrence of U in the function parameter is [depth:0, index:0]
  //   - The template parameter of U is [depth:0, index:0]
  //
  // We add the outer template arguments which is [int] to the multi-level arg
  // list to ensure that the occurrence U in `C<U>` will be replaced with int
  // during the substitution.
  //
  // NOTE: The underlying deduction guide F is instantiated -- either from an
  // explicitly-written deduction guide member, or from a constructor.
  // getInstantiatedFromMemberTemplate() can only handle the former case, so we
  // check the DeclContext kind.
  if (F->getLexicalDeclContext()->getDeclKind() ==
      clang::Decl::ClassTemplateSpecialization) {
    auto OuterLevelArgs = SemaRef.getTemplateInstantiationArgs(
        F, F->getLexicalDeclContext(),
        /*Final=*/false, /*Innermost=*/std::nullopt,
        /*RelativeToPrimary=*/true,
        /*Pattern=*/nullptr,
        /*ForConstraintInstantiation=*/true);
    for (auto It : OuterLevelArgs)
      ArgsForBuildingRC.addOuterTemplateArguments(It.Args);
  }

  ExprResult E = SemaRef.SubstExpr(RC, ArgsForBuildingRC);
  if (E.isInvalid())
    return nullptr;

  auto Conjunction =
      SemaRef.BuildBinOp(SemaRef.getCurScope(), SourceLocation{},
                         BinaryOperatorKind::BO_LAnd, E.get(), IsDeducible);
  if (Conjunction.isInvalid())
    return nullptr;
  return Conjunction.getAs<Expr>();
}
// Build the is_deducible constraint for the alias deduction guides.
// [over.match.class.deduct]p3.3:
//    ... and a constraint that is satisfied if and only if the arguments
//    of A are deducible (see below) from the return type.
Expr *buildIsDeducibleConstraint(Sema &SemaRef,
                                 TypeAliasTemplateDecl *AliasTemplate,
                                 QualType ReturnType,
                                 SmallVector<NamedDecl *> TemplateParams) {
  ASTContext &Context = SemaRef.Context;
  // Constraint AST nodes must use uninstantiated depth.
  if (auto *PrimaryTemplate =
          AliasTemplate->getInstantiatedFromMemberTemplate();
      PrimaryTemplate && TemplateParams.size() > 0) {
    LocalInstantiationScope Scope(SemaRef);

    // Adjust the depth for TemplateParams.
    unsigned AdjustDepth = PrimaryTemplate->getTemplateDepth();
    SmallVector<TemplateArgument> TransformedTemplateArgs;
    for (auto *TP : TemplateParams) {
      // Rebuild any internal references to earlier parameters and reindex
      // as we go.
      MultiLevelTemplateArgumentList Args;
      Args.setKind(TemplateSubstitutionKind::Rewrite);
      Args.addOuterTemplateArguments(TransformedTemplateArgs);
      NamedDecl *NewParam = transformTemplateParameter(
          SemaRef, AliasTemplate->getDeclContext(), TP, Args,
          /*NewIndex=*/TransformedTemplateArgs.size(),
          getTemplateParameterDepth(TP) + AdjustDepth);

      TemplateArgument NewTemplateArgument =
          Context.getInjectedTemplateArg(NewParam);
      TransformedTemplateArgs.push_back(NewTemplateArgument);
    }
    // Transformed the ReturnType to restore the uninstantiated depth.
    MultiLevelTemplateArgumentList Args;
    Args.setKind(TemplateSubstitutionKind::Rewrite);
    Args.addOuterTemplateArguments(TransformedTemplateArgs);
    ReturnType = SemaRef.SubstType(
        ReturnType, Args, AliasTemplate->getLocation(),
        Context.DeclarationNames.getCXXDeductionGuideName(AliasTemplate));
  };

  SmallVector<TypeSourceInfo *> IsDeducibleTypeTraitArgs = {
      Context.getTrivialTypeSourceInfo(
          Context.getDeducedTemplateSpecializationType(
              TemplateName(AliasTemplate), /*DeducedType=*/QualType(),
              /*IsDependent=*/true)), // template specialization type whose
                                      // arguments will be deduced.
      Context.getTrivialTypeSourceInfo(
          ReturnType), // type from which template arguments are deduced.
  };
  return TypeTraitExpr::Create(
      Context, Context.getLogicalOperationType(), AliasTemplate->getLocation(),
      TypeTrait::BTT_IsDeducible, IsDeducibleTypeTraitArgs,
      AliasTemplate->getLocation(), /*Value*/ false);
}

std::pair<TemplateDecl *, llvm::ArrayRef<TemplateArgument>>
getRHSTemplateDeclAndArgs(Sema &SemaRef, TypeAliasTemplateDecl *AliasTemplate) {
  // Unwrap the sugared ElaboratedType.
  auto RhsType = AliasTemplate->getTemplatedDecl()
                     ->getUnderlyingType()
                     .getSingleStepDesugaredType(SemaRef.Context);
  TemplateDecl *Template = nullptr;
  llvm::ArrayRef<TemplateArgument> AliasRhsTemplateArgs;
  if (const auto *TST = RhsType->getAs<TemplateSpecializationType>()) {
    // Cases where the RHS of the alias is dependent. e.g.
    //   template<typename T>
    //   using AliasFoo1 = Foo<T>; // a class/type alias template specialization
    Template = TST->getTemplateName().getAsTemplateDecl();
    AliasRhsTemplateArgs = TST->template_arguments();
  } else if (const auto *RT = RhsType->getAs<RecordType>()) {
    // Cases where template arguments in the RHS of the alias are not
    // dependent. e.g.
    //   using AliasFoo = Foo<bool>;
    if (const auto *CTSD = llvm::dyn_cast<ClassTemplateSpecializationDecl>(
            RT->getAsCXXRecordDecl())) {
      Template = CTSD->getSpecializedTemplate();
      AliasRhsTemplateArgs = CTSD->getTemplateArgs().asArray();
    }
  } else {
    assert(false && "unhandled RHS type of the alias");
  }
  return {Template, AliasRhsTemplateArgs};
}

// Build deduction guides for a type alias template from the given underlying
// deduction guide F.
FunctionTemplateDecl *
BuildDeductionGuideForTypeAlias(Sema &SemaRef,
                                TypeAliasTemplateDecl *AliasTemplate,
                                FunctionTemplateDecl *F, SourceLocation Loc) {
  LocalInstantiationScope Scope(SemaRef);
  Sema::InstantiatingTemplate BuildingDeductionGuides(
      SemaRef, AliasTemplate->getLocation(), F,
      Sema::InstantiatingTemplate::BuildingDeductionGuidesTag{});
  if (BuildingDeductionGuides.isInvalid())
    return nullptr;

  auto &Context = SemaRef.Context;
  auto [Template, AliasRhsTemplateArgs] =
      getRHSTemplateDeclAndArgs(SemaRef, AliasTemplate);

  auto RType = F->getTemplatedDecl()->getReturnType();
  // The (trailing) return type of the deduction guide.
  const TemplateSpecializationType *FReturnType =
      RType->getAs<TemplateSpecializationType>();
  if (const auto *InjectedCNT = RType->getAs<InjectedClassNameType>())
    // implicitly-generated deduction guide.
    FReturnType = InjectedCNT->getInjectedTST();
  else if (const auto *ET = RType->getAs<ElaboratedType>())
    // explicit deduction guide.
    FReturnType = ET->getNamedType()->getAs<TemplateSpecializationType>();
  assert(FReturnType && "expected to see a return type");
  // Deduce template arguments of the deduction guide f from the RHS of
  // the alias.
  //
  // C++ [over.match.class.deduct]p3: ...For each function or function
  // template f in the guides of the template named by the
  // simple-template-id of the defining-type-id, the template arguments
  // of the return type of f are deduced from the defining-type-id of A
  // according to the process in [temp.deduct.type] with the exception
  // that deduction does not fail if not all template arguments are
  // deduced.
  //
  //
  //  template<typename X, typename Y>
  //  f(X, Y) -> f<Y, X>;
  //
  //  template<typename U>
  //  using alias = f<int, U>;
  //
  // The RHS of alias is f<int, U>, we deduced the template arguments of
  // the return type of the deduction guide from it: Y->int, X->U
  sema::TemplateDeductionInfo TDeduceInfo(Loc);
  // Must initialize n elements, this is required by DeduceTemplateArguments.
  SmallVector<DeducedTemplateArgument> DeduceResults(
      F->getTemplateParameters()->size());

  // FIXME: DeduceTemplateArguments stops immediately at the first
  // non-deducible template argument. However, this doesn't seem to casue
  // issues for practice cases, we probably need to extend it to continue
  // performing deduction for rest of arguments to align with the C++
  // standard.
  SemaRef.DeduceTemplateArguments(
      F->getTemplateParameters(), FReturnType->template_arguments(),
      AliasRhsTemplateArgs, TDeduceInfo, DeduceResults,
      /*NumberOfArgumentsMustMatch=*/false);

  SmallVector<TemplateArgument> DeducedArgs;
  SmallVector<unsigned> NonDeducedTemplateParamsInFIndex;
  // !!NOTE: DeduceResults respects the sequence of template parameters of
  // the deduction guide f.
  for (unsigned Index = 0; Index < DeduceResults.size(); ++Index) {
    if (const auto &D = DeduceResults[Index]; !D.isNull()) // Deduced
      DeducedArgs.push_back(D);
    else
      NonDeducedTemplateParamsInFIndex.push_back(Index);
  }
  auto DeducedAliasTemplateParams =
      TemplateParamsReferencedInTemplateArgumentList(
          AliasTemplate->getTemplateParameters(), DeducedArgs);
  // All template arguments null by default.
  SmallVector<TemplateArgument> TemplateArgsForBuildingFPrime(
      F->getTemplateParameters()->size());

  // Create a template parameter list for the synthesized deduction guide f'.
  //
  // C++ [over.match.class.deduct]p3.2:
  //   If f is a function template, f' is a function template whose template
  //   parameter list consists of all the template parameters of A
  //   (including their default template arguments) that appear in the above
  //   deductions or (recursively) in their default template arguments
  SmallVector<NamedDecl *> FPrimeTemplateParams;
  // Store template arguments that refer to the newly-created template
  // parameters, used for building `TemplateArgsForBuildingFPrime`.
  SmallVector<TemplateArgument, 16> TransformedDeducedAliasArgs(
      AliasTemplate->getTemplateParameters()->size());

  for (unsigned AliasTemplateParamIdx : DeducedAliasTemplateParams) {
    auto *TP =
        AliasTemplate->getTemplateParameters()->getParam(AliasTemplateParamIdx);
    // Rebuild any internal references to earlier parameters and reindex as
    // we go.
    MultiLevelTemplateArgumentList Args;
    Args.setKind(TemplateSubstitutionKind::Rewrite);
    Args.addOuterTemplateArguments(TransformedDeducedAliasArgs);
    NamedDecl *NewParam = transformTemplateParameter(
        SemaRef, AliasTemplate->getDeclContext(), TP, Args,
        /*NewIndex=*/FPrimeTemplateParams.size(),
        getTemplateParameterDepth(TP));
    FPrimeTemplateParams.push_back(NewParam);

    TemplateArgument NewTemplateArgument =
        Context.getInjectedTemplateArg(NewParam);
    TransformedDeducedAliasArgs[AliasTemplateParamIdx] = NewTemplateArgument;
  }
  unsigned FirstUndeducedParamIdx = FPrimeTemplateParams.size();
  //   ...followed by the template parameters of f that were not deduced
  //   (including their default template arguments)
  for (unsigned FTemplateParamIdx : NonDeducedTemplateParamsInFIndex) {
    auto *TP = F->getTemplateParameters()->getParam(FTemplateParamIdx);
    MultiLevelTemplateArgumentList Args;
    Args.setKind(TemplateSubstitutionKind::Rewrite);
    // We take a shortcut here, it is ok to reuse the
    // TemplateArgsForBuildingFPrime.
    Args.addOuterTemplateArguments(TemplateArgsForBuildingFPrime);
    NamedDecl *NewParam = transformTemplateParameter(
        SemaRef, F->getDeclContext(), TP, Args, FPrimeTemplateParams.size(),
        getTemplateParameterDepth(TP));
    FPrimeTemplateParams.push_back(NewParam);

    assert(TemplateArgsForBuildingFPrime[FTemplateParamIdx].isNull() &&
           "The argument must be null before setting");
    TemplateArgsForBuildingFPrime[FTemplateParamIdx] =
        Context.getInjectedTemplateArg(NewParam);
  }

  // To form a deduction guide f' from f, we leverage clang's instantiation
  // mechanism, we construct a template argument list where the template
  // arguments refer to the newly-created template parameters of f', and
  // then apply instantiation on this template argument list to instantiate
  // f, this ensures all template parameter occurrences are updated
  // correctly.
  //
  // The template argument list is formed from the `DeducedArgs`, two parts:
  //  1) appeared template parameters of alias: transfrom the deduced
  //  template argument;
  //  2) non-deduced template parameters of f: rebuild a
  //  template argument;
  //
  // 2) has been built already (when rebuilding the new template
  // parameters), we now perform 1).
  MultiLevelTemplateArgumentList Args;
  Args.setKind(TemplateSubstitutionKind::Rewrite);
  Args.addOuterTemplateArguments(TransformedDeducedAliasArgs);
  for (unsigned Index = 0; Index < DeduceResults.size(); ++Index) {
    const auto &D = DeduceResults[Index];
    if (D.isNull()) {
      // 2): Non-deduced template parameter has been built already.
      assert(!TemplateArgsForBuildingFPrime[Index].isNull() &&
             "template arguments for non-deduced template parameters should "
             "be been set!");
      continue;
    }
    TemplateArgumentLoc Input =
        SemaRef.getTrivialTemplateArgumentLoc(D, QualType(), SourceLocation{});
    TemplateArgumentLoc Output;
    if (!SemaRef.SubstTemplateArgument(Input, Args, Output)) {
      assert(TemplateArgsForBuildingFPrime[Index].isNull() &&
             "InstantiatedArgs must be null before setting");
      TemplateArgsForBuildingFPrime[Index] = Output.getArgument();
    }
  }

  auto *TemplateArgListForBuildingFPrime =
      TemplateArgumentList::CreateCopy(Context, TemplateArgsForBuildingFPrime);
  // Form the f' by substituting the template arguments into f.
  if (auto *FPrime = SemaRef.InstantiateFunctionDeclaration(
          F, TemplateArgListForBuildingFPrime, AliasTemplate->getLocation(),
          Sema::CodeSynthesisContext::BuildingDeductionGuides)) {
    auto *GG = cast<CXXDeductionGuideDecl>(FPrime);

    Expr *IsDeducible = buildIsDeducibleConstraint(
        SemaRef, AliasTemplate, FPrime->getReturnType(), FPrimeTemplateParams);
    Expr *RequiresClause =
        buildAssociatedConstraints(SemaRef, F, AliasTemplate, DeduceResults,
                                   FirstUndeducedParamIdx, IsDeducible);

    auto *FPrimeTemplateParamList = TemplateParameterList::Create(
        Context, AliasTemplate->getTemplateParameters()->getTemplateLoc(),
        AliasTemplate->getTemplateParameters()->getLAngleLoc(),
        FPrimeTemplateParams,
        AliasTemplate->getTemplateParameters()->getRAngleLoc(),
        /*RequiresClause=*/RequiresClause);
    auto *Result = cast<FunctionTemplateDecl>(buildDeductionGuide(
        SemaRef, AliasTemplate, FPrimeTemplateParamList,
        GG->getCorrespondingConstructor(), GG->getExplicitSpecifier(),
        GG->getTypeSourceInfo(), AliasTemplate->getBeginLoc(),
        AliasTemplate->getLocation(), AliasTemplate->getEndLoc(),
        F->isImplicit()));
    cast<CXXDeductionGuideDecl>(Result->getTemplatedDecl())
        ->setDeductionCandidateKind(GG->getDeductionCandidateKind());
    return Result;
  }
  return nullptr;
}

void DeclareImplicitDeductionGuidesForTypeAlias(
    Sema &SemaRef, TypeAliasTemplateDecl *AliasTemplate, SourceLocation Loc) {
  if (AliasTemplate->isInvalidDecl())
    return;
  auto &Context = SemaRef.Context;
  // FIXME: if there is an explicit deduction guide after the first use of the
  // type alias usage, we will not cover this explicit deduction guide. fix this
  // case.
  if (hasDeclaredDeductionGuides(
          Context.DeclarationNames.getCXXDeductionGuideName(AliasTemplate),
          AliasTemplate->getDeclContext()))
    return;
  auto [Template, AliasRhsTemplateArgs] =
      getRHSTemplateDeclAndArgs(SemaRef, AliasTemplate);
  if (!Template)
    return;
  DeclarationNameInfo NameInfo(
      Context.DeclarationNames.getCXXDeductionGuideName(Template), Loc);
  LookupResult Guides(SemaRef, NameInfo, clang::Sema::LookupOrdinaryName);
  SemaRef.LookupQualifiedName(Guides, Template->getDeclContext());
  Guides.suppressDiagnostics();

  for (auto *G : Guides) {
    if (auto *DG = dyn_cast<CXXDeductionGuideDecl>(G)) {
      // The deduction guide is a non-template function decl, we just clone it.
      auto *FunctionType =
          SemaRef.Context.getTrivialTypeSourceInfo(DG->getType());
      FunctionProtoTypeLoc FPTL =
          FunctionType->getTypeLoc().castAs<FunctionProtoTypeLoc>();

      // Clone the parameters.
      for (unsigned I = 0, N = DG->getNumParams(); I != N; ++I) {
        const auto *P = DG->getParamDecl(I);
        auto *TSI = SemaRef.Context.getTrivialTypeSourceInfo(P->getType());
        ParmVarDecl *NewParam = ParmVarDecl::Create(
            SemaRef.Context, G->getDeclContext(),
            DG->getParamDecl(I)->getBeginLoc(), P->getLocation(), nullptr,
            TSI->getType(), TSI, SC_None, nullptr);
        NewParam->setScopeInfo(0, I);
        FPTL.setParam(I, NewParam);
      }
      auto *Transformed = cast<FunctionDecl>(buildDeductionGuide(
          SemaRef, AliasTemplate, /*TemplateParams=*/nullptr,
          /*Constructor=*/nullptr, DG->getExplicitSpecifier(), FunctionType,
          AliasTemplate->getBeginLoc(), AliasTemplate->getLocation(),
          AliasTemplate->getEndLoc(), DG->isImplicit()));

      // FIXME: Here the synthesized deduction guide is not a templated
      // function. Per [dcl.decl]p4, the requires-clause shall be present only
      // if the declarator declares a templated function, a bug in standard?
      auto *Constraint = buildIsDeducibleConstraint(
          SemaRef, AliasTemplate, Transformed->getReturnType(), {});
      if (auto *RC = DG->getTrailingRequiresClause()) {
        auto Conjunction =
            SemaRef.BuildBinOp(SemaRef.getCurScope(), SourceLocation{},
                               BinaryOperatorKind::BO_LAnd, RC, Constraint);
        if (!Conjunction.isInvalid())
          Constraint = Conjunction.getAs<Expr>();
      }
      Transformed->setTrailingRequiresClause(Constraint);
    }
    FunctionTemplateDecl *F = dyn_cast<FunctionTemplateDecl>(G);
    if (!F)
      continue;
    // The **aggregate** deduction guides are handled in a different code path
    // (DeclareAggregateDeductionGuideFromInitList), which involves the tricky
    // cache.
    if (cast<CXXDeductionGuideDecl>(F->getTemplatedDecl())
            ->getDeductionCandidateKind() == DeductionCandidate::Aggregate)
      continue;

    BuildDeductionGuideForTypeAlias(SemaRef, AliasTemplate, F, Loc);
  }
}

// Build an aggregate deduction guide for a type alias template.
FunctionTemplateDecl *DeclareAggregateDeductionGuideForTypeAlias(
    Sema &SemaRef, TypeAliasTemplateDecl *AliasTemplate,
    MutableArrayRef<QualType> ParamTypes, SourceLocation Loc) {
  TemplateDecl *RHSTemplate =
      getRHSTemplateDeclAndArgs(SemaRef, AliasTemplate).first;
  if (!RHSTemplate)
    return nullptr;

  llvm::SmallVector<TypedefNameDecl *> TypedefDecls;
  llvm::SmallVector<QualType> NewParamTypes;
  ExtractTypeForDeductionGuide TypeAliasTransformer(SemaRef, TypedefDecls);
  for (QualType P : ParamTypes) {
    QualType Type = TypeAliasTransformer.TransformType(P);
    if (Type.isNull())
      return nullptr;
    NewParamTypes.push_back(Type);
  }

  auto *RHSDeductionGuide = SemaRef.DeclareAggregateDeductionGuideFromInitList(
      RHSTemplate, NewParamTypes, Loc);
  if (!RHSDeductionGuide)
    return nullptr;

  for (TypedefNameDecl *TD : TypedefDecls)
    TD->setDeclContext(RHSDeductionGuide->getTemplatedDecl());

  return BuildDeductionGuideForTypeAlias(SemaRef, AliasTemplate,
                                         RHSDeductionGuide, Loc);
}

} // namespace

FunctionTemplateDecl *Sema::DeclareAggregateDeductionGuideFromInitList(
    TemplateDecl *Template, MutableArrayRef<QualType> ParamTypes,
    SourceLocation Loc) {
  llvm::FoldingSetNodeID ID;
  ID.AddPointer(Template);
  for (auto &T : ParamTypes)
    T.getCanonicalType().Profile(ID);
  unsigned Hash = ID.ComputeHash();

  auto Found = AggregateDeductionCandidates.find(Hash);
  if (Found != AggregateDeductionCandidates.end()) {
    CXXDeductionGuideDecl *GD = Found->getSecond();
    return GD->getDescribedFunctionTemplate();
  }

  if (auto *AliasTemplate = llvm::dyn_cast<TypeAliasTemplateDecl>(Template)) {
    if (auto *FTD = DeclareAggregateDeductionGuideForTypeAlias(
            *this, AliasTemplate, ParamTypes, Loc)) {
      auto *GD = cast<CXXDeductionGuideDecl>(FTD->getTemplatedDecl());
      GD->setDeductionCandidateKind(DeductionCandidate::Aggregate);
      AggregateDeductionCandidates[Hash] = GD;
      return FTD;
    }
  }

  if (CXXRecordDecl *DefRecord =
          cast<CXXRecordDecl>(Template->getTemplatedDecl())->getDefinition()) {
    if (TemplateDecl *DescribedTemplate =
            DefRecord->getDescribedClassTemplate())
      Template = DescribedTemplate;
  }

  DeclContext *DC = Template->getDeclContext();
  if (DC->isDependentContext())
    return nullptr;

  ConvertConstructorToDeductionGuideTransform Transform(
      *this, cast<ClassTemplateDecl>(Template));
  if (!isCompleteType(Loc, Transform.DeducedType))
    return nullptr;

  // In case we were expanding a pack when we attempted to declare deduction
  // guides, turn off pack expansion for everything we're about to do.
  ArgumentPackSubstitutionIndexRAII SubstIndex(*this,
                                               /*NewSubstitutionIndex=*/-1);
  // Create a template instantiation record to track the "instantiation" of
  // constructors into deduction guides.
  InstantiatingTemplate BuildingDeductionGuides(
      *this, Loc, Template,
      Sema::InstantiatingTemplate::BuildingDeductionGuidesTag{});
  if (BuildingDeductionGuides.isInvalid())
    return nullptr;

  ClassTemplateDecl *Pattern =
      Transform.NestedPattern ? Transform.NestedPattern : Transform.Template;
  ContextRAII SavedContext(*this, Pattern->getTemplatedDecl());

  auto *FTD = cast<FunctionTemplateDecl>(
      Transform.buildSimpleDeductionGuide(ParamTypes));
  SavedContext.pop();
  auto *GD = cast<CXXDeductionGuideDecl>(FTD->getTemplatedDecl());
  GD->setDeductionCandidateKind(DeductionCandidate::Aggregate);
  AggregateDeductionCandidates[Hash] = GD;
  return FTD;
}

void Sema::DeclareImplicitDeductionGuides(TemplateDecl *Template,
                                          SourceLocation Loc) {
  if (auto *AliasTemplate = llvm::dyn_cast<TypeAliasTemplateDecl>(Template)) {
    DeclareImplicitDeductionGuidesForTypeAlias(*this, AliasTemplate, Loc);
    return;
  }
  if (CXXRecordDecl *DefRecord =
          cast<CXXRecordDecl>(Template->getTemplatedDecl())->getDefinition()) {
    if (TemplateDecl *DescribedTemplate =
            DefRecord->getDescribedClassTemplate())
      Template = DescribedTemplate;
  }

  DeclContext *DC = Template->getDeclContext();
  if (DC->isDependentContext())
    return;

  ConvertConstructorToDeductionGuideTransform Transform(
      *this, cast<ClassTemplateDecl>(Template));
  if (!isCompleteType(Loc, Transform.DeducedType))
    return;

  if (hasDeclaredDeductionGuides(Transform.DeductionGuideName, DC))
    return;

  // In case we were expanding a pack when we attempted to declare deduction
  // guides, turn off pack expansion for everything we're about to do.
  ArgumentPackSubstitutionIndexRAII SubstIndex(*this, -1);
  // Create a template instantiation record to track the "instantiation" of
  // constructors into deduction guides.
  InstantiatingTemplate BuildingDeductionGuides(
      *this, Loc, Template,
      Sema::InstantiatingTemplate::BuildingDeductionGuidesTag{});
  if (BuildingDeductionGuides.isInvalid())
    return;

  // Convert declared constructors into deduction guide templates.
  // FIXME: Skip constructors for which deduction must necessarily fail (those
  // for which some class template parameter without a default argument never
  // appears in a deduced context).
  ClassTemplateDecl *Pattern =
      Transform.NestedPattern ? Transform.NestedPattern : Transform.Template;
  ContextRAII SavedContext(*this, Pattern->getTemplatedDecl());
  llvm::SmallPtrSet<NamedDecl *, 8> ProcessedCtors;
  bool AddedAny = false;
  for (NamedDecl *D : LookupConstructors(Pattern->getTemplatedDecl())) {
    D = D->getUnderlyingDecl();
    if (D->isInvalidDecl() || D->isImplicit())
      continue;

    D = cast<NamedDecl>(D->getCanonicalDecl());

    // Within C++20 modules, we may have multiple same constructors in
    // multiple same RecordDecls. And it doesn't make sense to create
    // duplicated deduction guides for the duplicated constructors.
    if (ProcessedCtors.count(D))
      continue;

    auto *FTD = dyn_cast<FunctionTemplateDecl>(D);
    auto *CD =
        dyn_cast_or_null<CXXConstructorDecl>(FTD ? FTD->getTemplatedDecl() : D);
    // Class-scope explicit specializations (MS extension) do not result in
    // deduction guides.
    if (!CD || (!FTD && CD->isFunctionTemplateSpecialization()))
      continue;

    // Cannot make a deduction guide when unparsed arguments are present.
    if (llvm::any_of(CD->parameters(), [](ParmVarDecl *P) {
          return !P || P->hasUnparsedDefaultArg();
        }))
      continue;

    ProcessedCtors.insert(D);
    Transform.transformConstructor(FTD, CD);
    AddedAny = true;
  }

  // C++17 [over.match.class.deduct]
  //    --  If C is not defined or does not declare any constructors, an
  //    additional function template derived as above from a hypothetical
  //    constructor C().
  if (!AddedAny)
    Transform.buildSimpleDeductionGuide(std::nullopt);

  //    -- An additional function template derived as above from a hypothetical
  //    constructor C(C), called the copy deduction candidate.
  cast<CXXDeductionGuideDecl>(
      cast<FunctionTemplateDecl>(
          Transform.buildSimpleDeductionGuide(Transform.DeducedType))
          ->getTemplatedDecl())
      ->setDeductionCandidateKind(DeductionCandidate::Copy);

  SavedContext.pop();
}
