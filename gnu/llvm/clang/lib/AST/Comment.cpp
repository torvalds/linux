//===--- Comment.cpp - Comment AST node implementation --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Comment.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/CharInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <type_traits>

namespace clang {
namespace comments {

// Check that no comment class has a non-trival destructor. They are allocated
// with a BumpPtrAllocator and therefore their destructor is not executed.
#define ABSTRACT_COMMENT(COMMENT)
#define COMMENT(CLASS, PARENT)                                                 \
  static_assert(std::is_trivially_destructible<CLASS>::value,                  \
                #CLASS " should be trivially destructible!");
#include "clang/AST/CommentNodes.inc"
#undef COMMENT
#undef ABSTRACT_COMMENT

// DeclInfo is also allocated with a BumpPtrAllocator.
static_assert(std::is_trivially_destructible_v<DeclInfo>,
              "DeclInfo should be trivially destructible!");

const char *Comment::getCommentKindName() const {
  switch (getCommentKind()) {
  case CommentKind::None:
    return "None";
#define ABSTRACT_COMMENT(COMMENT)
#define COMMENT(CLASS, PARENT)                                                 \
  case CommentKind::CLASS:                                                     \
    return #CLASS;
#include "clang/AST/CommentNodes.inc"
#undef COMMENT
#undef ABSTRACT_COMMENT
  }
  llvm_unreachable("Unknown comment kind!");
}

namespace {
struct good {};
struct bad {};

template <typename T>
good implements_child_begin_end(Comment::child_iterator (T::*)() const) {
  return good();
}

LLVM_ATTRIBUTE_UNUSED
static inline bad implements_child_begin_end(
                      Comment::child_iterator (Comment::*)() const) {
  return bad();
}

#define ASSERT_IMPLEMENTS_child_begin(function) \
  (void) good(implements_child_begin_end(function))

LLVM_ATTRIBUTE_UNUSED
static inline void CheckCommentASTNodes() {
#define ABSTRACT_COMMENT(COMMENT)
#define COMMENT(CLASS, PARENT) \
  ASSERT_IMPLEMENTS_child_begin(&CLASS::child_begin); \
  ASSERT_IMPLEMENTS_child_begin(&CLASS::child_end);
#include "clang/AST/CommentNodes.inc"
#undef COMMENT
#undef ABSTRACT_COMMENT
}

#undef ASSERT_IMPLEMENTS_child_begin

} // end unnamed namespace

Comment::child_iterator Comment::child_begin() const {
  switch (getCommentKind()) {
  case CommentKind::None:
    llvm_unreachable("comment without a kind");
#define ABSTRACT_COMMENT(COMMENT)
#define COMMENT(CLASS, PARENT)                                                 \
  case CommentKind::CLASS:                                                     \
    return static_cast<const CLASS *>(this)->child_begin();
#include "clang/AST/CommentNodes.inc"
#undef COMMENT
#undef ABSTRACT_COMMENT
  }
  llvm_unreachable("Unknown comment kind!");
}

Comment::child_iterator Comment::child_end() const {
  switch (getCommentKind()) {
  case CommentKind::None:
    llvm_unreachable("comment without a kind");
#define ABSTRACT_COMMENT(COMMENT)
#define COMMENT(CLASS, PARENT)                                                 \
  case CommentKind::CLASS:                                                     \
    return static_cast<const CLASS *>(this)->child_end();
#include "clang/AST/CommentNodes.inc"
#undef COMMENT
#undef ABSTRACT_COMMENT
  }
  llvm_unreachable("Unknown comment kind!");
}

bool TextComment::isWhitespaceNoCache() const {
  return llvm::all_of(Text, clang::isWhitespace);
}

bool ParagraphComment::isWhitespaceNoCache() const {
  for (child_iterator I = child_begin(), E = child_end(); I != E; ++I) {
    if (const TextComment *TC = dyn_cast<TextComment>(*I)) {
      if (!TC->isWhitespace())
        return false;
    } else
      return false;
  }
  return true;
}

static TypeLoc lookThroughTypedefOrTypeAliasLocs(TypeLoc &SrcTL) {
  TypeLoc TL = SrcTL.IgnoreParens();

  // Look through attribute types.
  if (AttributedTypeLoc AttributeTL = TL.getAs<AttributedTypeLoc>())
    return AttributeTL.getModifiedLoc();
  // Look through qualified types.
  if (QualifiedTypeLoc QualifiedTL = TL.getAs<QualifiedTypeLoc>())
    return QualifiedTL.getUnqualifiedLoc();
  // Look through pointer types.
  if (PointerTypeLoc PointerTL = TL.getAs<PointerTypeLoc>())
    return PointerTL.getPointeeLoc().getUnqualifiedLoc();
  // Look through reference types.
  if (ReferenceTypeLoc ReferenceTL = TL.getAs<ReferenceTypeLoc>())
    return ReferenceTL.getPointeeLoc().getUnqualifiedLoc();
  // Look through adjusted types.
  if (AdjustedTypeLoc ATL = TL.getAs<AdjustedTypeLoc>())
    return ATL.getOriginalLoc();
  if (BlockPointerTypeLoc BlockPointerTL = TL.getAs<BlockPointerTypeLoc>())
    return BlockPointerTL.getPointeeLoc().getUnqualifiedLoc();
  if (MemberPointerTypeLoc MemberPointerTL = TL.getAs<MemberPointerTypeLoc>())
    return MemberPointerTL.getPointeeLoc().getUnqualifiedLoc();
  if (ElaboratedTypeLoc ETL = TL.getAs<ElaboratedTypeLoc>())
    return ETL.getNamedTypeLoc();

  return TL;
}

static bool getFunctionTypeLoc(TypeLoc TL, FunctionTypeLoc &ResFTL) {
  TypeLoc PrevTL;
  while (PrevTL != TL) {
    PrevTL = TL;
    TL = lookThroughTypedefOrTypeAliasLocs(TL);
  }

  if (FunctionTypeLoc FTL = TL.getAs<FunctionTypeLoc>()) {
    ResFTL = FTL;
    return true;
  }

  if (TemplateSpecializationTypeLoc STL =
          TL.getAs<TemplateSpecializationTypeLoc>()) {
    // If we have a typedef to a template specialization with exactly one
    // template argument of a function type, this looks like std::function,
    // boost::function, or other function wrapper.  Treat these typedefs as
    // functions.
    if (STL.getNumArgs() != 1)
      return false;
    TemplateArgumentLoc MaybeFunction = STL.getArgLoc(0);
    if (MaybeFunction.getArgument().getKind() != TemplateArgument::Type)
      return false;
    TypeSourceInfo *MaybeFunctionTSI = MaybeFunction.getTypeSourceInfo();
    TypeLoc TL = MaybeFunctionTSI->getTypeLoc().getUnqualifiedLoc();
    if (FunctionTypeLoc FTL = TL.getAs<FunctionTypeLoc>()) {
      ResFTL = FTL;
      return true;
    }
  }

  return false;
}

const char *
ParamCommandComment::getDirectionAsString(ParamCommandPassDirection D) {
  switch (D) {
  case ParamCommandPassDirection::In:
    return "[in]";
  case ParamCommandPassDirection::Out:
    return "[out]";
  case ParamCommandPassDirection::InOut:
    return "[in,out]";
  }
  llvm_unreachable("unknown PassDirection");
}

void DeclInfo::fill() {
  assert(!IsFilled);

  // Set defaults.
  Kind = OtherKind;
  TemplateKind = NotTemplate;
  IsObjCMethod = false;
  IsInstanceMethod = false;
  IsClassMethod = false;
  IsVariadic = false;
  ParamVars = std::nullopt;
  TemplateParameters = nullptr;

  if (!CommentDecl) {
    // If there is no declaration, the defaults is our only guess.
    IsFilled = true;
    return;
  }
  CurrentDecl = CommentDecl;

  Decl::Kind K = CommentDecl->getKind();
  const TypeSourceInfo *TSI = nullptr;
  switch (K) {
  default:
    // Defaults are should be good for declarations we don't handle explicitly.
    break;
  case Decl::Function:
  case Decl::CXXMethod:
  case Decl::CXXConstructor:
  case Decl::CXXDestructor:
  case Decl::CXXConversion: {
    const FunctionDecl *FD = cast<FunctionDecl>(CommentDecl);
    Kind = FunctionKind;
    ParamVars = FD->parameters();
    ReturnType = FD->getReturnType();
    unsigned NumLists = FD->getNumTemplateParameterLists();
    if (NumLists != 0) {
      TemplateKind = TemplateSpecialization;
      TemplateParameters =
          FD->getTemplateParameterList(NumLists - 1);
    }

    if (K == Decl::CXXMethod || K == Decl::CXXConstructor ||
        K == Decl::CXXDestructor || K == Decl::CXXConversion) {
      const CXXMethodDecl *MD = cast<CXXMethodDecl>(CommentDecl);
      IsInstanceMethod = MD->isInstance();
      IsClassMethod = !IsInstanceMethod;
    }
    IsVariadic = FD->isVariadic();
    assert(involvesFunctionType());
    break;
  }
  case Decl::ObjCMethod: {
    const ObjCMethodDecl *MD = cast<ObjCMethodDecl>(CommentDecl);
    Kind = FunctionKind;
    ParamVars = MD->parameters();
    ReturnType = MD->getReturnType();
    IsObjCMethod = true;
    IsInstanceMethod = MD->isInstanceMethod();
    IsClassMethod = !IsInstanceMethod;
    IsVariadic = MD->isVariadic();
    assert(involvesFunctionType());
    break;
  }
  case Decl::FunctionTemplate: {
    const FunctionTemplateDecl *FTD = cast<FunctionTemplateDecl>(CommentDecl);
    Kind = FunctionKind;
    TemplateKind = Template;
    const FunctionDecl *FD = FTD->getTemplatedDecl();
    ParamVars = FD->parameters();
    ReturnType = FD->getReturnType();
    TemplateParameters = FTD->getTemplateParameters();
    IsVariadic = FD->isVariadic();
    assert(involvesFunctionType());
    break;
  }
  case Decl::ClassTemplate: {
    const ClassTemplateDecl *CTD = cast<ClassTemplateDecl>(CommentDecl);
    Kind = ClassKind;
    TemplateKind = Template;
    TemplateParameters = CTD->getTemplateParameters();
    break;
  }
  case Decl::ClassTemplatePartialSpecialization: {
    const ClassTemplatePartialSpecializationDecl *CTPSD =
        cast<ClassTemplatePartialSpecializationDecl>(CommentDecl);
    Kind = ClassKind;
    TemplateKind = TemplatePartialSpecialization;
    TemplateParameters = CTPSD->getTemplateParameters();
    break;
  }
  case Decl::ClassTemplateSpecialization:
    Kind = ClassKind;
    TemplateKind = TemplateSpecialization;
    break;
  case Decl::Record:
  case Decl::CXXRecord:
    Kind = ClassKind;
    break;
  case Decl::Var:
    if (const VarTemplateDecl *VTD =
            cast<VarDecl>(CommentDecl)->getDescribedVarTemplate()) {
      TemplateKind = TemplateSpecialization;
      TemplateParameters = VTD->getTemplateParameters();
    }
    [[fallthrough]];
  case Decl::Field:
  case Decl::EnumConstant:
  case Decl::ObjCIvar:
  case Decl::ObjCAtDefsField:
  case Decl::ObjCProperty:
    if (const auto *VD = dyn_cast<DeclaratorDecl>(CommentDecl))
      TSI = VD->getTypeSourceInfo();
    else if (const auto *PD = dyn_cast<ObjCPropertyDecl>(CommentDecl))
      TSI = PD->getTypeSourceInfo();
    Kind = VariableKind;
    break;
  case Decl::VarTemplate: {
    const VarTemplateDecl *VTD = cast<VarTemplateDecl>(CommentDecl);
    Kind = VariableKind;
    TemplateKind = Template;
    TemplateParameters = VTD->getTemplateParameters();
    if (const VarDecl *VD = VTD->getTemplatedDecl())
      TSI = VD->getTypeSourceInfo();
    break;
  }
  case Decl::Namespace:
    Kind = NamespaceKind;
    break;
  case Decl::TypeAlias:
  case Decl::Typedef:
    Kind = TypedefKind;
    TSI = cast<TypedefNameDecl>(CommentDecl)->getTypeSourceInfo();
    break;
  case Decl::TypeAliasTemplate: {
    const TypeAliasTemplateDecl *TAT = cast<TypeAliasTemplateDecl>(CommentDecl);
    Kind = TypedefKind;
    TemplateKind = Template;
    TemplateParameters = TAT->getTemplateParameters();
    if (TypeAliasDecl *TAD = TAT->getTemplatedDecl())
      TSI = TAD->getTypeSourceInfo();
    break;
  }
  case Decl::Enum:
    Kind = EnumKind;
    break;
  }

  // If the type is a typedef / using to something we consider a function,
  // extract arguments and return type.
  if (TSI) {
    TypeLoc TL = TSI->getTypeLoc().getUnqualifiedLoc();
    FunctionTypeLoc FTL;
    if (getFunctionTypeLoc(TL, FTL)) {
      ParamVars = FTL.getParams();
      ReturnType = FTL.getReturnLoc().getType();
      if (const auto *FPT = dyn_cast<FunctionProtoType>(FTL.getTypePtr()))
        IsVariadic = FPT->isVariadic();
      assert(involvesFunctionType());
    }
  }

  IsFilled = true;
}

StringRef ParamCommandComment::getParamName(const FullComment *FC) const {
  assert(isParamIndexValid());
  if (isVarArgParam())
    return "...";
  return FC->getDeclInfo()->ParamVars[getParamIndex()]->getName();
}

StringRef TParamCommandComment::getParamName(const FullComment *FC) const {
  assert(isPositionValid());
  const TemplateParameterList *TPL = FC->getDeclInfo()->TemplateParameters;
  for (unsigned i = 0, e = getDepth(); i != e; ++i) {
    assert(TPL && "Unknown TemplateParameterList");
    if (i == e - 1)
      return TPL->getParam(getIndex(i))->getName();
    const NamedDecl *Param = TPL->getParam(getIndex(i));
    if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param))
      TPL = TTP->getTemplateParameters();
  }
  return "";
}

} // end namespace comments
} // end namespace clang

