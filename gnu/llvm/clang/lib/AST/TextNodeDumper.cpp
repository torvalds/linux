//===--- TextNodeDumper.cpp - Printing of AST nodes -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements AST dumping of components of individual AST nodes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TextNodeDumper.h"
#include "clang/AST/APValue.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/LocInfoType.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TypeTraits.h"
#include "llvm/ADT/StringExtras.h"

#include <algorithm>
#include <utility>

using namespace clang;

static void dumpPreviousDeclImpl(raw_ostream &OS, ...) {}

template <typename T>
static void dumpPreviousDeclImpl(raw_ostream &OS, const Mergeable<T> *D) {
  const T *First = D->getFirstDecl();
  if (First != D)
    OS << " first " << First;
}

template <typename T>
static void dumpPreviousDeclImpl(raw_ostream &OS, const Redeclarable<T> *D) {
  const T *Prev = D->getPreviousDecl();
  if (Prev)
    OS << " prev " << Prev;
}

/// Dump the previous declaration in the redeclaration chain for a declaration,
/// if any.
static void dumpPreviousDecl(raw_ostream &OS, const Decl *D) {
  switch (D->getKind()) {
#define DECL(DERIVED, BASE)                                                    \
  case Decl::DERIVED:                                                          \
    return dumpPreviousDeclImpl(OS, cast<DERIVED##Decl>(D));
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
  llvm_unreachable("Decl that isn't part of DeclNodes.inc!");
}

TextNodeDumper::TextNodeDumper(raw_ostream &OS, const ASTContext &Context,
                               bool ShowColors)
    : TextTreeStructure(OS, ShowColors), OS(OS), ShowColors(ShowColors),
      Context(&Context), SM(&Context.getSourceManager()),
      PrintPolicy(Context.getPrintingPolicy()),
      Traits(&Context.getCommentCommandTraits()) {}

TextNodeDumper::TextNodeDumper(raw_ostream &OS, bool ShowColors)
    : TextTreeStructure(OS, ShowColors), OS(OS), ShowColors(ShowColors) {}

void TextNodeDumper::Visit(const comments::Comment *C,
                           const comments::FullComment *FC) {
  if (!C) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, CommentColor);
    OS << C->getCommentKindName();
  }
  dumpPointer(C);
  dumpSourceRange(C->getSourceRange());

  ConstCommentVisitor<TextNodeDumper, void,
                      const comments::FullComment *>::visit(C, FC);
}

void TextNodeDumper::Visit(const Attr *A) {
  {
    ColorScope Color(OS, ShowColors, AttrColor);

    switch (A->getKind()) {
#define ATTR(X)                                                                \
  case attr::X:                                                                \
    OS << #X;                                                                  \
    break;
#include "clang/Basic/AttrList.inc"
    }
    OS << "Attr";
  }
  dumpPointer(A);
  dumpSourceRange(A->getRange());
  if (A->isInherited())
    OS << " Inherited";
  if (A->isImplicit())
    OS << " Implicit";

  ConstAttrVisitor<TextNodeDumper>::Visit(A);
}

void TextNodeDumper::Visit(const TemplateArgument &TA, SourceRange R,
                           const Decl *From, StringRef Label) {
  OS << "TemplateArgument";
  if (R.isValid())
    dumpSourceRange(R);

  if (From)
    dumpDeclRef(From, Label);

  ConstTemplateArgumentVisitor<TextNodeDumper>::Visit(TA);
}

void TextNodeDumper::Visit(const Stmt *Node) {
  if (!Node) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }
  {
    ColorScope Color(OS, ShowColors, StmtColor);
    OS << Node->getStmtClassName();
  }
  dumpPointer(Node);
  dumpSourceRange(Node->getSourceRange());

  if (const auto *E = dyn_cast<Expr>(Node)) {
    dumpType(E->getType());

    if (E->containsErrors()) {
      ColorScope Color(OS, ShowColors, ErrorsColor);
      OS << " contains-errors";
    }

    {
      ColorScope Color(OS, ShowColors, ValueKindColor);
      switch (E->getValueKind()) {
      case VK_PRValue:
        break;
      case VK_LValue:
        OS << " lvalue";
        break;
      case VK_XValue:
        OS << " xvalue";
        break;
      }
    }

    {
      ColorScope Color(OS, ShowColors, ObjectKindColor);
      switch (E->getObjectKind()) {
      case OK_Ordinary:
        break;
      case OK_BitField:
        OS << " bitfield";
        break;
      case OK_ObjCProperty:
        OS << " objcproperty";
        break;
      case OK_ObjCSubscript:
        OS << " objcsubscript";
        break;
      case OK_VectorComponent:
        OS << " vectorcomponent";
        break;
      case OK_MatrixComponent:
        OS << " matrixcomponent";
        break;
      }
    }
  }

  ConstStmtVisitor<TextNodeDumper>::Visit(Node);
}

void TextNodeDumper::Visit(const Type *T) {
  if (!T) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }
  if (isa<LocInfoType>(T)) {
    {
      ColorScope Color(OS, ShowColors, TypeColor);
      OS << "LocInfo Type";
    }
    dumpPointer(T);
    return;
  }

  {
    ColorScope Color(OS, ShowColors, TypeColor);
    OS << T->getTypeClassName() << "Type";
  }
  dumpPointer(T);
  OS << " ";
  dumpBareType(QualType(T, 0), false);

  QualType SingleStepDesugar =
      T->getLocallyUnqualifiedSingleStepDesugaredType();
  if (SingleStepDesugar != QualType(T, 0))
    OS << " sugar";

  if (T->containsErrors()) {
    ColorScope Color(OS, ShowColors, ErrorsColor);
    OS << " contains-errors";
  }

  if (T->isDependentType())
    OS << " dependent";
  else if (T->isInstantiationDependentType())
    OS << " instantiation_dependent";

  if (T->isVariablyModifiedType())
    OS << " variably_modified";
  if (T->containsUnexpandedParameterPack())
    OS << " contains_unexpanded_pack";
  if (T->isFromAST())
    OS << " imported";

  TypeVisitor<TextNodeDumper>::Visit(T);
}

void TextNodeDumper::Visit(QualType T) {
  OS << "QualType";
  dumpPointer(T.getAsOpaquePtr());
  OS << " ";
  dumpBareType(T, false);
  OS << " " << T.split().Quals.getAsString();
}

void TextNodeDumper::Visit(TypeLoc TL) {
  if (!TL) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, TypeColor);
    OS << (TL.getTypeLocClass() == TypeLoc::Qualified
               ? "Qualified"
               : TL.getType()->getTypeClassName())
       << "TypeLoc";
  }
  dumpSourceRange(TL.getSourceRange());
  OS << ' ';
  dumpBareType(TL.getType(), /*Desugar=*/false);

  TypeLocVisitor<TextNodeDumper>::Visit(TL);
}

void TextNodeDumper::Visit(const Decl *D) {
  if (!D) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, DeclKindNameColor);
    OS << D->getDeclKindName() << "Decl";
  }
  dumpPointer(D);
  if (D->getLexicalDeclContext() != D->getDeclContext())
    OS << " parent " << cast<Decl>(D->getDeclContext());
  dumpPreviousDecl(OS, D);
  dumpSourceRange(D->getSourceRange());
  OS << ' ';
  dumpLocation(D->getLocation());
  if (D->isFromASTFile())
    OS << " imported";
  if (Module *M = D->getOwningModule())
    OS << " in " << M->getFullModuleName();
  if (auto *ND = dyn_cast<NamedDecl>(D))
    for (Module *M : D->getASTContext().getModulesWithMergedDefinition(
             const_cast<NamedDecl *>(ND)))
      AddChild([=] { OS << "also in " << M->getFullModuleName(); });
  if (const NamedDecl *ND = dyn_cast<NamedDecl>(D))
    if (!ND->isUnconditionallyVisible())
      OS << " hidden";
  if (D->isImplicit())
    OS << " implicit";

  if (D->isUsed())
    OS << " used";
  else if (D->isThisDeclarationReferenced())
    OS << " referenced";

  if (D->isInvalidDecl())
    OS << " invalid";
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isConstexprSpecified())
      OS << " constexpr";
    if (FD->isConsteval())
      OS << " consteval";
    else if (FD->isImmediateFunction())
      OS << " immediate";
    if (FD->isMultiVersion())
      OS << " multiversion";
  }

  if (!isa<FunctionDecl>(*D)) {
    const auto *MD = dyn_cast<ObjCMethodDecl>(D);
    if (!MD || !MD->isThisDeclarationADefinition()) {
      const auto *DC = dyn_cast<DeclContext>(D);
      if (DC && DC->hasExternalLexicalStorage()) {
        ColorScope Color(OS, ShowColors, UndeserializedColor);
        OS << " <undeserialized declarations>";
      }
    }
  }

  switch (D->getFriendObjectKind()) {
  case Decl::FOK_None:
    break;
  case Decl::FOK_Declared:
    OS << " friend";
    break;
  case Decl::FOK_Undeclared:
    OS << " friend_undeclared";
    break;
  }

  ConstDeclVisitor<TextNodeDumper>::Visit(D);
}

void TextNodeDumper::Visit(const CXXCtorInitializer *Init) {
  OS << "CXXCtorInitializer";
  if (Init->isAnyMemberInitializer()) {
    OS << ' ';
    dumpBareDeclRef(Init->getAnyMember());
  } else if (Init->isBaseInitializer()) {
    dumpType(QualType(Init->getBaseClass(), 0));
  } else if (Init->isDelegatingInitializer()) {
    dumpType(Init->getTypeSourceInfo()->getType());
  } else {
    llvm_unreachable("Unknown initializer type");
  }
}

void TextNodeDumper::Visit(const BlockDecl::Capture &C) {
  OS << "capture";
  if (C.isByRef())
    OS << " byref";
  if (C.isNested())
    OS << " nested";
  if (C.getVariable()) {
    OS << ' ';
    dumpBareDeclRef(C.getVariable());
  }
}

void TextNodeDumper::Visit(const OMPClause *C) {
  if (!C) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>> OMPClause";
    return;
  }
  {
    ColorScope Color(OS, ShowColors, AttrColor);
    StringRef ClauseName(llvm::omp::getOpenMPClauseName(C->getClauseKind()));
    OS << "OMP" << ClauseName.substr(/*Start=*/0, /*N=*/1).upper()
       << ClauseName.drop_front() << "Clause";
  }
  dumpPointer(C);
  dumpSourceRange(SourceRange(C->getBeginLoc(), C->getEndLoc()));
  if (C->isImplicit())
    OS << " <implicit>";
}

void TextNodeDumper::Visit(const OpenACCClause *C) {
  if (!C) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>> OpenACCClause";
    return;
  }
  {
    ColorScope Color(OS, ShowColors, AttrColor);
    OS << C->getClauseKind();

    // Handle clauses with parens for types that have no children, likely
    // because there is no sub expression.
    switch (C->getClauseKind()) {
    case OpenACCClauseKind::Default:
      OS << '(' << cast<OpenACCDefaultClause>(C)->getDefaultClauseKind() << ')';
      break;
    case OpenACCClauseKind::Async:
    case OpenACCClauseKind::Auto:
    case OpenACCClauseKind::Attach:
    case OpenACCClauseKind::Copy:
    case OpenACCClauseKind::PCopy:
    case OpenACCClauseKind::PresentOrCopy:
    case OpenACCClauseKind::If:
    case OpenACCClauseKind::Independent:
    case OpenACCClauseKind::DevicePtr:
    case OpenACCClauseKind::FirstPrivate:
    case OpenACCClauseKind::NoCreate:
    case OpenACCClauseKind::NumGangs:
    case OpenACCClauseKind::NumWorkers:
    case OpenACCClauseKind::Present:
    case OpenACCClauseKind::Private:
    case OpenACCClauseKind::Self:
    case OpenACCClauseKind::Seq:
    case OpenACCClauseKind::VectorLength:
      // The condition expression will be printed as a part of the 'children',
      // but print 'clause' here so it is clear what is happening from the dump.
      OS << " clause";
      break;
    case OpenACCClauseKind::CopyIn:
    case OpenACCClauseKind::PCopyIn:
    case OpenACCClauseKind::PresentOrCopyIn:
      OS << " clause";
      if (cast<OpenACCCopyInClause>(C)->isReadOnly())
        OS << " : readonly";
      break;
    case OpenACCClauseKind::CopyOut:
    case OpenACCClauseKind::PCopyOut:
    case OpenACCClauseKind::PresentOrCopyOut:
      OS << " clause";
      if (cast<OpenACCCopyOutClause>(C)->isZero())
        OS << " : zero";
      break;
    case OpenACCClauseKind::Create:
    case OpenACCClauseKind::PCreate:
    case OpenACCClauseKind::PresentOrCreate:
      OS << " clause";
      if (cast<OpenACCCreateClause>(C)->isZero())
        OS << " : zero";
      break;
    case OpenACCClauseKind::Wait:
      OS << " clause";
      if (cast<OpenACCWaitClause>(C)->hasDevNumExpr())
        OS << " has devnum";
      if (cast<OpenACCWaitClause>(C)->hasQueuesTag())
        OS << " has queues tag";
      break;
    case OpenACCClauseKind::DeviceType:
    case OpenACCClauseKind::DType:
      OS << "(";
      llvm::interleaveComma(
          cast<OpenACCDeviceTypeClause>(C)->getArchitectures(), OS,
          [&](const DeviceTypeArgument &Arch) {
            if (Arch.first == nullptr)
              OS << "*";
            else
              OS << Arch.first->getName();
          });
      OS << ")";
      break;
    case OpenACCClauseKind::Reduction:
      OS << " clause Operator: "
         << cast<OpenACCReductionClause>(C)->getReductionOp();
      break;
    default:
      // Nothing to do here.
      break;
    }
  }
  dumpPointer(C);
  dumpSourceRange(SourceRange(C->getBeginLoc(), C->getEndLoc()));
}

void TextNodeDumper::Visit(const GenericSelectionExpr::ConstAssociation &A) {
  const TypeSourceInfo *TSI = A.getTypeSourceInfo();
  if (TSI) {
    OS << "case ";
    dumpType(TSI->getType());
  } else {
    OS << "default";
  }

  if (A.isSelected())
    OS << " selected";
}

void TextNodeDumper::Visit(const ConceptReference *R) {
  if (!R) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>> ConceptReference";
    return;
  }

  OS << "ConceptReference";
  dumpPointer(R);
  dumpSourceRange(R->getSourceRange());
  OS << ' ';
  dumpBareDeclRef(R->getNamedConcept());
}

void TextNodeDumper::Visit(const concepts::Requirement *R) {
  if (!R) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>> Requirement";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, StmtColor);
    switch (R->getKind()) {
    case concepts::Requirement::RK_Type:
      OS << "TypeRequirement";
      break;
    case concepts::Requirement::RK_Simple:
      OS << "SimpleRequirement";
      break;
    case concepts::Requirement::RK_Compound:
      OS << "CompoundRequirement";
      break;
    case concepts::Requirement::RK_Nested:
      OS << "NestedRequirement";
      break;
    }
  }

  dumpPointer(R);

  if (auto *ER = dyn_cast<concepts::ExprRequirement>(R)) {
    if (ER->hasNoexceptRequirement())
      OS << " noexcept";
  }

  if (R->isDependent())
    OS << " dependent";
  else
    OS << (R->isSatisfied() ? " satisfied" : " unsatisfied");
  if (R->containsUnexpandedParameterPack())
    OS << " contains_unexpanded_pack";
}

static double GetApproxValue(const llvm::APFloat &F) {
  llvm::APFloat V = F;
  bool ignored;
  V.convert(llvm::APFloat::IEEEdouble(), llvm::APFloat::rmNearestTiesToEven,
            &ignored);
  return V.convertToDouble();
}

/// True if the \p APValue \p Value can be folded onto the current line.
static bool isSimpleAPValue(const APValue &Value) {
  switch (Value.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
  case APValue::Int:
  case APValue::Float:
  case APValue::FixedPoint:
  case APValue::ComplexInt:
  case APValue::ComplexFloat:
  case APValue::LValue:
  case APValue::MemberPointer:
  case APValue::AddrLabelDiff:
    return true;
  case APValue::Vector:
  case APValue::Array:
  case APValue::Struct:
    return false;
  case APValue::Union:
    return isSimpleAPValue(Value.getUnionValue());
  }
  llvm_unreachable("unexpected APValue kind!");
}

/// Dump the children of the \p APValue \p Value.
///
/// \param[in] Value          The \p APValue to visit
/// \param[in] Ty             The \p QualType passed to \p Visit
///
/// \param[in] IdxToChildFun  A function mapping an \p APValue and an index
///                           to one of the child of the \p APValue
///
/// \param[in] NumChildren    \p IdxToChildFun will be called on \p Value with
///                           the indices in the range \p [0,NumChildren(
///
/// \param[in] LabelSingular  The label to use on a line with a single child
/// \param[in] LabelPlurial   The label to use on a line with multiple children
void TextNodeDumper::dumpAPValueChildren(
    const APValue &Value, QualType Ty,
    const APValue &(*IdxToChildFun)(const APValue &, unsigned),
    unsigned NumChildren, StringRef LabelSingular, StringRef LabelPlurial) {
  // To save some vertical space we print up to MaxChildrenPerLine APValues
  // considered to be simple (by isSimpleAPValue) on a single line.
  constexpr unsigned MaxChildrenPerLine = 4;
  unsigned I = 0;
  while (I < NumChildren) {
    unsigned J = I;
    while (J < NumChildren) {
      if (isSimpleAPValue(IdxToChildFun(Value, J)) &&
          (J - I < MaxChildrenPerLine)) {
        ++J;
        continue;
      }
      break;
    }

    J = std::max(I + 1, J);

    // Print [I,J) on a single line.
    AddChild(J - I > 1 ? LabelPlurial : LabelSingular, [=]() {
      for (unsigned X = I; X < J; ++X) {
        Visit(IdxToChildFun(Value, X), Ty);
        if (X + 1 != J)
          OS << ", ";
      }
    });
    I = J;
  }
}

void TextNodeDumper::Visit(const APValue &Value, QualType Ty) {
  ColorScope Color(OS, ShowColors, ValueKindColor);
  switch (Value.getKind()) {
  case APValue::None:
    OS << "None";
    return;
  case APValue::Indeterminate:
    OS << "Indeterminate";
    return;
  case APValue::Int:
    OS << "Int ";
    {
      ColorScope Color(OS, ShowColors, ValueColor);
      OS << Value.getInt();
    }
    return;
  case APValue::Float:
    OS << "Float ";
    {
      ColorScope Color(OS, ShowColors, ValueColor);
      OS << GetApproxValue(Value.getFloat());
    }
    return;
  case APValue::FixedPoint:
    OS << "FixedPoint ";
    {
      ColorScope Color(OS, ShowColors, ValueColor);
      OS << Value.getFixedPoint();
    }
    return;
  case APValue::Vector: {
    unsigned VectorLength = Value.getVectorLength();
    OS << "Vector length=" << VectorLength;

    dumpAPValueChildren(
        Value, Ty,
        [](const APValue &Value, unsigned Index) -> const APValue & {
          return Value.getVectorElt(Index);
        },
        VectorLength, "element", "elements");
    return;
  }
  case APValue::ComplexInt:
    OS << "ComplexInt ";
    {
      ColorScope Color(OS, ShowColors, ValueColor);
      OS << Value.getComplexIntReal() << " + " << Value.getComplexIntImag()
         << 'i';
    }
    return;
  case APValue::ComplexFloat:
    OS << "ComplexFloat ";
    {
      ColorScope Color(OS, ShowColors, ValueColor);
      OS << GetApproxValue(Value.getComplexFloatReal()) << " + "
         << GetApproxValue(Value.getComplexFloatImag()) << 'i';
    }
    return;
  case APValue::LValue:
    (void)Context;
    OS << "LValue <todo>";
    return;
  case APValue::Array: {
    unsigned ArraySize = Value.getArraySize();
    unsigned NumInitializedElements = Value.getArrayInitializedElts();
    OS << "Array size=" << ArraySize;

    dumpAPValueChildren(
        Value, Ty,
        [](const APValue &Value, unsigned Index) -> const APValue & {
          return Value.getArrayInitializedElt(Index);
        },
        NumInitializedElements, "element", "elements");

    if (Value.hasArrayFiller()) {
      AddChild("filler", [=] {
        {
          ColorScope Color(OS, ShowColors, ValueColor);
          OS << ArraySize - NumInitializedElements << " x ";
        }
        Visit(Value.getArrayFiller(), Ty);
      });
    }

    return;
  }
  case APValue::Struct: {
    OS << "Struct";

    dumpAPValueChildren(
        Value, Ty,
        [](const APValue &Value, unsigned Index) -> const APValue & {
          return Value.getStructBase(Index);
        },
        Value.getStructNumBases(), "base", "bases");

    dumpAPValueChildren(
        Value, Ty,
        [](const APValue &Value, unsigned Index) -> const APValue & {
          return Value.getStructField(Index);
        },
        Value.getStructNumFields(), "field", "fields");

    return;
  }
  case APValue::Union: {
    OS << "Union";
    {
      ColorScope Color(OS, ShowColors, ValueColor);
      if (const FieldDecl *FD = Value.getUnionField())
        OS << " ." << *cast<NamedDecl>(FD);
    }
    // If the union value is considered to be simple, fold it into the
    // current line to save some vertical space.
    const APValue &UnionValue = Value.getUnionValue();
    if (isSimpleAPValue(UnionValue)) {
      OS << ' ';
      Visit(UnionValue, Ty);
    } else {
      AddChild([=] { Visit(UnionValue, Ty); });
    }

    return;
  }
  case APValue::MemberPointer:
    OS << "MemberPointer <todo>";
    return;
  case APValue::AddrLabelDiff:
    OS << "AddrLabelDiff <todo>";
    return;
  }
  llvm_unreachable("Unknown APValue kind!");
}

void TextNodeDumper::dumpPointer(const void *Ptr) {
  ColorScope Color(OS, ShowColors, AddressColor);
  OS << ' ' << Ptr;
}

void TextNodeDumper::dumpLocation(SourceLocation Loc) {
  if (!SM)
    return;

  ColorScope Color(OS, ShowColors, LocationColor);
  SourceLocation SpellingLoc = SM->getSpellingLoc(Loc);

  // The general format we print out is filename:line:col, but we drop pieces
  // that haven't changed since the last loc printed.
  PresumedLoc PLoc = SM->getPresumedLoc(SpellingLoc);

  if (PLoc.isInvalid()) {
    OS << "<invalid sloc>";
    return;
  }

  if (strcmp(PLoc.getFilename(), LastLocFilename) != 0) {
    OS << PLoc.getFilename() << ':' << PLoc.getLine() << ':'
       << PLoc.getColumn();
    LastLocFilename = PLoc.getFilename();
    LastLocLine = PLoc.getLine();
  } else if (PLoc.getLine() != LastLocLine) {
    OS << "line" << ':' << PLoc.getLine() << ':' << PLoc.getColumn();
    LastLocLine = PLoc.getLine();
  } else {
    OS << "col" << ':' << PLoc.getColumn();
  }
}

void TextNodeDumper::dumpSourceRange(SourceRange R) {
  // Can't translate locations if a SourceManager isn't available.
  if (!SM)
    return;

  OS << " <";
  dumpLocation(R.getBegin());
  if (R.getBegin() != R.getEnd()) {
    OS << ", ";
    dumpLocation(R.getEnd());
  }
  OS << ">";

  // <t2.c:123:421[blah], t2.c:412:321>
}

void TextNodeDumper::dumpBareType(QualType T, bool Desugar) {
  ColorScope Color(OS, ShowColors, TypeColor);

  SplitQualType T_split = T.split();
  std::string T_str = QualType::getAsString(T_split, PrintPolicy);
  OS << "'" << T_str << "'";

  if (Desugar && !T.isNull()) {
    // If the type is sugared, also dump a (shallow) desugared type when
    // it is visibly different.
    SplitQualType D_split = T.getSplitDesugaredType();
    if (T_split != D_split) {
      std::string D_str = QualType::getAsString(D_split, PrintPolicy);
      if (T_str != D_str)
        OS << ":'" << QualType::getAsString(D_split, PrintPolicy) << "'";
    }
  }
}

void TextNodeDumper::dumpType(QualType T) {
  OS << ' ';
  dumpBareType(T);
}

void TextNodeDumper::dumpBareDeclRef(const Decl *D) {
  if (!D) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, DeclKindNameColor);
    OS << D->getDeclKindName();
  }
  dumpPointer(D);

  if (const NamedDecl *ND = dyn_cast<NamedDecl>(D)) {
    ColorScope Color(OS, ShowColors, DeclNameColor);
    OS << " '" << ND->getDeclName() << '\'';
  }

  if (const ValueDecl *VD = dyn_cast<ValueDecl>(D))
    dumpType(VD->getType());
}

void TextNodeDumper::dumpName(const NamedDecl *ND) {
  if (ND->getDeclName()) {
    ColorScope Color(OS, ShowColors, DeclNameColor);
    OS << ' ' << ND->getDeclName();
  }
}

void TextNodeDumper::dumpAccessSpecifier(AccessSpecifier AS) {
  const auto AccessSpelling = getAccessSpelling(AS);
  if (AccessSpelling.empty())
    return;
  OS << AccessSpelling;
}

void TextNodeDumper::dumpCleanupObject(
    const ExprWithCleanups::CleanupObject &C) {
  if (auto *BD = C.dyn_cast<BlockDecl *>())
    dumpDeclRef(BD, "cleanup");
  else if (auto *CLE = C.dyn_cast<CompoundLiteralExpr *>())
    AddChild([=] {
      OS << "cleanup ";
      {
        ColorScope Color(OS, ShowColors, StmtColor);
        OS << CLE->getStmtClassName();
      }
      dumpPointer(CLE);
    });
  else
    llvm_unreachable("unexpected cleanup type");
}

void clang::TextNodeDumper::dumpTemplateSpecializationKind(
    TemplateSpecializationKind TSK) {
  switch (TSK) {
  case TSK_Undeclared:
    break;
  case TSK_ImplicitInstantiation:
    OS << " implicit_instantiation";
    break;
  case TSK_ExplicitSpecialization:
    OS << " explicit_specialization";
    break;
  case TSK_ExplicitInstantiationDeclaration:
    OS << " explicit_instantiation_declaration";
    break;
  case TSK_ExplicitInstantiationDefinition:
    OS << " explicit_instantiation_definition";
    break;
  }
}

void clang::TextNodeDumper::dumpNestedNameSpecifier(const NestedNameSpecifier *NNS) {
  if (!NNS)
    return;

  AddChild([=] {
    OS << "NestedNameSpecifier";

    switch (NNS->getKind()) {
    case NestedNameSpecifier::Identifier:
      OS << " Identifier";
      OS << " '" << NNS->getAsIdentifier()->getName() << "'";
      break;
    case NestedNameSpecifier::Namespace:
      OS << " "; // "Namespace" is printed as the decl kind.
      dumpBareDeclRef(NNS->getAsNamespace());
      break;
    case NestedNameSpecifier::NamespaceAlias:
      OS << " "; // "NamespaceAlias" is printed as the decl kind.
      dumpBareDeclRef(NNS->getAsNamespaceAlias());
      break;
    case NestedNameSpecifier::TypeSpec:
      OS << " TypeSpec";
      dumpType(QualType(NNS->getAsType(), 0));
      break;
    case NestedNameSpecifier::TypeSpecWithTemplate:
      OS << " TypeSpecWithTemplate";
      dumpType(QualType(NNS->getAsType(), 0));
      break;
    case NestedNameSpecifier::Global:
      OS << " Global";
      break;
    case NestedNameSpecifier::Super:
      OS << " Super";
      break;
    }

    dumpNestedNameSpecifier(NNS->getPrefix());
  });
}

void TextNodeDumper::dumpDeclRef(const Decl *D, StringRef Label) {
  if (!D)
    return;

  AddChild([=] {
    if (!Label.empty())
      OS << Label << ' ';
    dumpBareDeclRef(D);
  });
}

void TextNodeDumper::dumpTemplateArgument(const TemplateArgument &TA) {
  llvm::SmallString<128> Str;
  {
    llvm::raw_svector_ostream SS(Str);
    TA.print(PrintPolicy, SS, /*IncludeType=*/true);
  }
  OS << " '" << Str << "'";

  if (!Context)
    return;

  if (TemplateArgument CanonTA = Context->getCanonicalTemplateArgument(TA);
      !CanonTA.structurallyEquals(TA)) {
    llvm::SmallString<128> CanonStr;
    {
      llvm::raw_svector_ostream SS(CanonStr);
      CanonTA.print(PrintPolicy, SS, /*IncludeType=*/true);
    }
    if (CanonStr != Str)
      OS << ":'" << CanonStr << "'";
  }
}

const char *TextNodeDumper::getCommandName(unsigned CommandID) {
  if (Traits)
    return Traits->getCommandInfo(CommandID)->Name;
  const comments::CommandInfo *Info =
      comments::CommandTraits::getBuiltinCommandInfo(CommandID);
  if (Info)
    return Info->Name;
  return "<not a builtin command>";
}

void TextNodeDumper::printFPOptions(FPOptionsOverride FPO) {
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  if (FPO.has##NAME##Override())                                               \
    OS << " " #NAME "=" << FPO.get##NAME##Override();
#include "clang/Basic/FPOptions.def"
}

void TextNodeDumper::visitTextComment(const comments::TextComment *C,
                                      const comments::FullComment *) {
  OS << " Text=\"" << C->getText() << "\"";
}

void TextNodeDumper::visitInlineCommandComment(
    const comments::InlineCommandComment *C, const comments::FullComment *) {
  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\"";
  switch (C->getRenderKind()) {
  case comments::InlineCommandRenderKind::Normal:
    OS << " RenderNormal";
    break;
  case comments::InlineCommandRenderKind::Bold:
    OS << " RenderBold";
    break;
  case comments::InlineCommandRenderKind::Monospaced:
    OS << " RenderMonospaced";
    break;
  case comments::InlineCommandRenderKind::Emphasized:
    OS << " RenderEmphasized";
    break;
  case comments::InlineCommandRenderKind::Anchor:
    OS << " RenderAnchor";
    break;
  }

  for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i)
    OS << " Arg[" << i << "]=\"" << C->getArgText(i) << "\"";
}

void TextNodeDumper::visitHTMLStartTagComment(
    const comments::HTMLStartTagComment *C, const comments::FullComment *) {
  OS << " Name=\"" << C->getTagName() << "\"";
  if (C->getNumAttrs() != 0) {
    OS << " Attrs: ";
    for (unsigned i = 0, e = C->getNumAttrs(); i != e; ++i) {
      const comments::HTMLStartTagComment::Attribute &Attr = C->getAttr(i);
      OS << " \"" << Attr.Name << "=\"" << Attr.Value << "\"";
    }
  }
  if (C->isSelfClosing())
    OS << " SelfClosing";
}

void TextNodeDumper::visitHTMLEndTagComment(
    const comments::HTMLEndTagComment *C, const comments::FullComment *) {
  OS << " Name=\"" << C->getTagName() << "\"";
}

void TextNodeDumper::visitBlockCommandComment(
    const comments::BlockCommandComment *C, const comments::FullComment *) {
  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\"";
  for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i)
    OS << " Arg[" << i << "]=\"" << C->getArgText(i) << "\"";
}

void TextNodeDumper::visitParamCommandComment(
    const comments::ParamCommandComment *C, const comments::FullComment *FC) {
  OS << " "
     << comments::ParamCommandComment::getDirectionAsString(C->getDirection());

  if (C->isDirectionExplicit())
    OS << " explicitly";
  else
    OS << " implicitly";

  if (C->hasParamName()) {
    if (C->isParamIndexValid())
      OS << " Param=\"" << C->getParamName(FC) << "\"";
    else
      OS << " Param=\"" << C->getParamNameAsWritten() << "\"";
  }

  if (C->isParamIndexValid() && !C->isVarArgParam())
    OS << " ParamIndex=" << C->getParamIndex();
}

void TextNodeDumper::visitTParamCommandComment(
    const comments::TParamCommandComment *C, const comments::FullComment *FC) {
  if (C->hasParamName()) {
    if (C->isPositionValid())
      OS << " Param=\"" << C->getParamName(FC) << "\"";
    else
      OS << " Param=\"" << C->getParamNameAsWritten() << "\"";
  }

  if (C->isPositionValid()) {
    OS << " Position=<";
    for (unsigned i = 0, e = C->getDepth(); i != e; ++i) {
      OS << C->getIndex(i);
      if (i != e - 1)
        OS << ", ";
    }
    OS << ">";
  }
}

void TextNodeDumper::visitVerbatimBlockComment(
    const comments::VerbatimBlockComment *C, const comments::FullComment *) {
  OS << " Name=\"" << getCommandName(C->getCommandID())
     << "\""
        " CloseName=\""
     << C->getCloseName() << "\"";
}

void TextNodeDumper::visitVerbatimBlockLineComment(
    const comments::VerbatimBlockLineComment *C,
    const comments::FullComment *) {
  OS << " Text=\"" << C->getText() << "\"";
}

void TextNodeDumper::visitVerbatimLineComment(
    const comments::VerbatimLineComment *C, const comments::FullComment *) {
  OS << " Text=\"" << C->getText() << "\"";
}

void TextNodeDumper::VisitNullTemplateArgument(const TemplateArgument &) {
  OS << " null";
}

void TextNodeDumper::VisitTypeTemplateArgument(const TemplateArgument &TA) {
  OS << " type";
  dumpTemplateArgument(TA);
}

void TextNodeDumper::VisitDeclarationTemplateArgument(
    const TemplateArgument &TA) {
  OS << " decl";
  dumpTemplateArgument(TA);
  dumpDeclRef(TA.getAsDecl());
}

void TextNodeDumper::VisitNullPtrTemplateArgument(const TemplateArgument &TA) {
  OS << " nullptr";
  dumpTemplateArgument(TA);
}

void TextNodeDumper::VisitIntegralTemplateArgument(const TemplateArgument &TA) {
  OS << " integral";
  dumpTemplateArgument(TA);
}

void TextNodeDumper::dumpTemplateName(TemplateName TN, StringRef Label) {
  AddChild(Label, [=] {
    {
      llvm::SmallString<128> Str;
      {
        llvm::raw_svector_ostream SS(Str);
        TN.print(SS, PrintPolicy);
      }
      OS << "'" << Str << "'";

      if (Context) {
        if (TemplateName CanonTN = Context->getCanonicalTemplateName(TN);
            CanonTN != TN) {
          llvm::SmallString<128> CanonStr;
          {
            llvm::raw_svector_ostream SS(CanonStr);
            CanonTN.print(SS, PrintPolicy);
          }
          if (CanonStr != Str)
            OS << ":'" << CanonStr << "'";
        }
      }
    }
    dumpBareTemplateName(TN);
  });
}

void TextNodeDumper::dumpBareTemplateName(TemplateName TN) {
  switch (TN.getKind()) {
  case TemplateName::Template:
    AddChild([=] { Visit(TN.getAsTemplateDecl()); });
    return;
  case TemplateName::UsingTemplate: {
    const UsingShadowDecl *USD = TN.getAsUsingShadowDecl();
    AddChild([=] { Visit(USD); });
    AddChild("target", [=] { Visit(USD->getTargetDecl()); });
    return;
  }
  case TemplateName::QualifiedTemplate: {
    OS << " qualified";
    const QualifiedTemplateName *QTN = TN.getAsQualifiedTemplateName();
    if (QTN->hasTemplateKeyword())
      OS << " keyword";
    dumpNestedNameSpecifier(QTN->getQualifier());
    dumpBareTemplateName(QTN->getUnderlyingTemplate());
    return;
  }
  case TemplateName::DependentTemplate: {
    OS << " dependent";
    const DependentTemplateName *DTN = TN.getAsDependentTemplateName();
    dumpNestedNameSpecifier(DTN->getQualifier());
    return;
  }
  case TemplateName::SubstTemplateTemplateParm: {
    OS << " subst";
    const SubstTemplateTemplateParmStorage *STS =
        TN.getAsSubstTemplateTemplateParm();
    OS << " index " << STS->getIndex();
    if (std::optional<unsigned int> PackIndex = STS->getPackIndex())
      OS << " pack_index " << *PackIndex;
    if (const TemplateTemplateParmDecl *P = STS->getParameter())
      AddChild("parameter", [=] { Visit(P); });
    dumpDeclRef(STS->getAssociatedDecl(), "associated");
    dumpTemplateName(STS->getReplacement(), "replacement");
    return;
  }
  // FIXME: Implement these.
  case TemplateName::OverloadedTemplate:
    OS << " overloaded";
    return;
  case TemplateName::AssumedTemplate:
    OS << " assumed";
    return;
  case TemplateName::SubstTemplateTemplateParmPack:
    OS << " subst_pack";
    return;
  }
  llvm_unreachable("Unexpected TemplateName Kind");
}

void TextNodeDumper::VisitTemplateTemplateArgument(const TemplateArgument &TA) {
  OS << " template";
  dumpTemplateArgument(TA);
  dumpBareTemplateName(TA.getAsTemplate());
}

void TextNodeDumper::VisitTemplateExpansionTemplateArgument(
    const TemplateArgument &TA) {
  OS << " template expansion";
  dumpTemplateArgument(TA);
  dumpBareTemplateName(TA.getAsTemplateOrTemplatePattern());
}

void TextNodeDumper::VisitExpressionTemplateArgument(
    const TemplateArgument &TA) {
  OS << " expr";
  dumpTemplateArgument(TA);
}

void TextNodeDumper::VisitPackTemplateArgument(const TemplateArgument &TA) {
  OS << " pack";
  dumpTemplateArgument(TA);
}

static void dumpBasePath(raw_ostream &OS, const CastExpr *Node) {
  if (Node->path_empty())
    return;

  OS << " (";
  bool First = true;
  for (CastExpr::path_const_iterator I = Node->path_begin(),
                                     E = Node->path_end();
       I != E; ++I) {
    const CXXBaseSpecifier *Base = *I;
    if (!First)
      OS << " -> ";

    const auto *RD =
        cast<CXXRecordDecl>(Base->getType()->castAs<RecordType>()->getDecl());

    if (Base->isVirtual())
      OS << "virtual ";
    OS << RD->getName();
    First = false;
  }

  OS << ')';
}

void TextNodeDumper::VisitIfStmt(const IfStmt *Node) {
  if (Node->hasInitStorage())
    OS << " has_init";
  if (Node->hasVarStorage())
    OS << " has_var";
  if (Node->hasElseStorage())
    OS << " has_else";
  if (Node->isConstexpr())
    OS << " constexpr";
  if (Node->isConsteval()) {
    OS << " ";
    if (Node->isNegatedConsteval())
      OS << "!";
    OS << "consteval";
  }
}

void TextNodeDumper::VisitSwitchStmt(const SwitchStmt *Node) {
  if (Node->hasInitStorage())
    OS << " has_init";
  if (Node->hasVarStorage())
    OS << " has_var";
}

void TextNodeDumper::VisitWhileStmt(const WhileStmt *Node) {
  if (Node->hasVarStorage())
    OS << " has_var";
}

void TextNodeDumper::VisitLabelStmt(const LabelStmt *Node) {
  OS << " '" << Node->getName() << "'";
  if (Node->isSideEntry())
    OS << " side_entry";
}

void TextNodeDumper::VisitGotoStmt(const GotoStmt *Node) {
  OS << " '" << Node->getLabel()->getName() << "'";
  dumpPointer(Node->getLabel());
}

void TextNodeDumper::VisitCaseStmt(const CaseStmt *Node) {
  if (Node->caseStmtIsGNURange())
    OS << " gnu_range";
}

void clang::TextNodeDumper::VisitReturnStmt(const ReturnStmt *Node) {
  if (const VarDecl *Cand = Node->getNRVOCandidate()) {
    OS << " nrvo_candidate(";
    dumpBareDeclRef(Cand);
    OS << ")";
  }
}

void clang::TextNodeDumper::VisitCoawaitExpr(const CoawaitExpr *Node) {
  if (Node->isImplicit())
    OS << " implicit";
}

void clang::TextNodeDumper::VisitCoreturnStmt(const CoreturnStmt *Node) {
  if (Node->isImplicit())
    OS << " implicit";
}

void TextNodeDumper::VisitConstantExpr(const ConstantExpr *Node) {
  if (Node->hasAPValueResult())
    AddChild("value",
             [=] { Visit(Node->getAPValueResult(), Node->getType()); });
}

void TextNodeDumper::VisitCallExpr(const CallExpr *Node) {
  if (Node->usesADL())
    OS << " adl";
  if (Node->hasStoredFPFeatures())
    printFPOptions(Node->getFPFeatures());
}

void TextNodeDumper::VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *Node) {
  const char *OperatorSpelling = clang::getOperatorSpelling(Node->getOperator());
  if (OperatorSpelling)
    OS << " '" << OperatorSpelling << "'";

  VisitCallExpr(Node);
}

void TextNodeDumper::VisitCastExpr(const CastExpr *Node) {
  OS << " <";
  {
    ColorScope Color(OS, ShowColors, CastColor);
    OS << Node->getCastKindName();
  }
  dumpBasePath(OS, Node);
  OS << ">";
  if (Node->hasStoredFPFeatures())
    printFPOptions(Node->getFPFeatures());
}

void TextNodeDumper::VisitImplicitCastExpr(const ImplicitCastExpr *Node) {
  VisitCastExpr(Node);
  if (Node->isPartOfExplicitCast())
    OS << " part_of_explicit_cast";
}

void TextNodeDumper::VisitDeclRefExpr(const DeclRefExpr *Node) {
  OS << " ";
  dumpBareDeclRef(Node->getDecl());
  dumpNestedNameSpecifier(Node->getQualifier());
  if (Node->getDecl() != Node->getFoundDecl()) {
    OS << " (";
    dumpBareDeclRef(Node->getFoundDecl());
    OS << ")";
  }
  switch (Node->isNonOdrUse()) {
  case NOUR_None: break;
  case NOUR_Unevaluated: OS << " non_odr_use_unevaluated"; break;
  case NOUR_Constant: OS << " non_odr_use_constant"; break;
  case NOUR_Discarded: OS << " non_odr_use_discarded"; break;
  }
  if (Node->isCapturedByCopyInLambdaWithExplicitObjectParameter())
    OS << " dependent_capture";
  else if (Node->refersToEnclosingVariableOrCapture())
    OS << " refers_to_enclosing_variable_or_capture";

  if (Node->isImmediateEscalating())
    OS << " immediate-escalating";
}

void clang::TextNodeDumper::VisitDependentScopeDeclRefExpr(
    const DependentScopeDeclRefExpr *Node) {

  dumpNestedNameSpecifier(Node->getQualifier());
}

void TextNodeDumper::VisitUnresolvedLookupExpr(
    const UnresolvedLookupExpr *Node) {
  OS << " (";
  if (!Node->requiresADL())
    OS << "no ";
  OS << "ADL) = '" << Node->getName() << '\'';

  UnresolvedLookupExpr::decls_iterator I = Node->decls_begin(),
                                       E = Node->decls_end();
  if (I == E)
    OS << " empty";
  for (; I != E; ++I)
    dumpPointer(*I);
}

void TextNodeDumper::VisitObjCIvarRefExpr(const ObjCIvarRefExpr *Node) {
  {
    ColorScope Color(OS, ShowColors, DeclKindNameColor);
    OS << " " << Node->getDecl()->getDeclKindName() << "Decl";
  }
  OS << "='" << *Node->getDecl() << "'";
  dumpPointer(Node->getDecl());
  if (Node->isFreeIvar())
    OS << " isFreeIvar";
}

void TextNodeDumper::VisitSYCLUniqueStableNameExpr(
    const SYCLUniqueStableNameExpr *Node) {
  dumpType(Node->getTypeSourceInfo()->getType());
}

void TextNodeDumper::VisitPredefinedExpr(const PredefinedExpr *Node) {
  OS << " " << PredefinedExpr::getIdentKindName(Node->getIdentKind());
}

void TextNodeDumper::VisitCharacterLiteral(const CharacterLiteral *Node) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << Node->getValue();
}

void TextNodeDumper::VisitIntegerLiteral(const IntegerLiteral *Node) {
  bool isSigned = Node->getType()->isSignedIntegerType();
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << toString(Node->getValue(), 10, isSigned);
}

void TextNodeDumper::VisitFixedPointLiteral(const FixedPointLiteral *Node) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << Node->getValueAsString(/*Radix=*/10);
}

void TextNodeDumper::VisitFloatingLiteral(const FloatingLiteral *Node) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << Node->getValueAsApproximateDouble();
}

void TextNodeDumper::VisitStringLiteral(const StringLiteral *Str) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " ";
  Str->outputString(OS);
}

void TextNodeDumper::VisitInitListExpr(const InitListExpr *ILE) {
  if (auto *Field = ILE->getInitializedFieldInUnion()) {
    OS << " field ";
    dumpBareDeclRef(Field);
  }
}

void TextNodeDumper::VisitGenericSelectionExpr(const GenericSelectionExpr *E) {
  if (E->isResultDependent())
    OS << " result_dependent";
}

void TextNodeDumper::VisitUnaryOperator(const UnaryOperator *Node) {
  OS << " " << (Node->isPostfix() ? "postfix" : "prefix") << " '"
     << UnaryOperator::getOpcodeStr(Node->getOpcode()) << "'";
  if (!Node->canOverflow())
    OS << " cannot overflow";
  if (Node->hasStoredFPFeatures())
    printFPOptions(Node->getStoredFPFeatures());
}

void TextNodeDumper::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *Node) {
  OS << " " << getTraitSpelling(Node->getKind());

  if (Node->isArgumentType())
    dumpType(Node->getArgumentType());
}

void TextNodeDumper::VisitMemberExpr(const MemberExpr *Node) {
  OS << " " << (Node->isArrow() ? "->" : ".") << *Node->getMemberDecl();
  dumpPointer(Node->getMemberDecl());
  dumpNestedNameSpecifier(Node->getQualifier());
  switch (Node->isNonOdrUse()) {
  case NOUR_None: break;
  case NOUR_Unevaluated: OS << " non_odr_use_unevaluated"; break;
  case NOUR_Constant: OS << " non_odr_use_constant"; break;
  case NOUR_Discarded: OS << " non_odr_use_discarded"; break;
  }
}

void TextNodeDumper::VisitExtVectorElementExpr(
    const ExtVectorElementExpr *Node) {
  OS << " " << Node->getAccessor().getNameStart();
}

void TextNodeDumper::VisitBinaryOperator(const BinaryOperator *Node) {
  OS << " '" << BinaryOperator::getOpcodeStr(Node->getOpcode()) << "'";
  if (Node->hasStoredFPFeatures())
    printFPOptions(Node->getStoredFPFeatures());
}

void TextNodeDumper::VisitCompoundAssignOperator(
    const CompoundAssignOperator *Node) {
  OS << " '" << BinaryOperator::getOpcodeStr(Node->getOpcode())
     << "' ComputeLHSTy=";
  dumpBareType(Node->getComputationLHSType());
  OS << " ComputeResultTy=";
  dumpBareType(Node->getComputationResultType());
  if (Node->hasStoredFPFeatures())
    printFPOptions(Node->getStoredFPFeatures());
}

void TextNodeDumper::VisitAddrLabelExpr(const AddrLabelExpr *Node) {
  OS << " " << Node->getLabel()->getName();
  dumpPointer(Node->getLabel());
}

void TextNodeDumper::VisitCXXNamedCastExpr(const CXXNamedCastExpr *Node) {
  OS << " " << Node->getCastName() << "<"
     << Node->getTypeAsWritten().getAsString() << ">"
     << " <" << Node->getCastKindName();
  dumpBasePath(OS, Node);
  OS << ">";
}

void TextNodeDumper::VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *Node) {
  OS << " " << (Node->getValue() ? "true" : "false");
}

void TextNodeDumper::VisitCXXThisExpr(const CXXThisExpr *Node) {
  if (Node->isImplicit())
    OS << " implicit";
  if (Node->isCapturedByCopyInLambdaWithExplicitObjectParameter())
    OS << " dependent_capture";
  OS << " this";
}

void TextNodeDumper::VisitCXXFunctionalCastExpr(
    const CXXFunctionalCastExpr *Node) {
  OS << " functional cast to " << Node->getTypeAsWritten().getAsString() << " <"
     << Node->getCastKindName() << ">";
  if (Node->hasStoredFPFeatures())
    printFPOptions(Node->getFPFeatures());
}

void TextNodeDumper::VisitCXXStaticCastExpr(const CXXStaticCastExpr *Node) {
  VisitCXXNamedCastExpr(Node);
  if (Node->hasStoredFPFeatures())
    printFPOptions(Node->getFPFeatures());
}

void TextNodeDumper::VisitCXXUnresolvedConstructExpr(
    const CXXUnresolvedConstructExpr *Node) {
  dumpType(Node->getTypeAsWritten());
  if (Node->isListInitialization())
    OS << " list";
}

void TextNodeDumper::VisitCXXConstructExpr(const CXXConstructExpr *Node) {
  CXXConstructorDecl *Ctor = Node->getConstructor();
  dumpType(Ctor->getType());
  if (Node->isElidable())
    OS << " elidable";
  if (Node->isListInitialization())
    OS << " list";
  if (Node->isStdInitListInitialization())
    OS << " std::initializer_list";
  if (Node->requiresZeroInitialization())
    OS << " zeroing";
  if (Node->isImmediateEscalating())
    OS << " immediate-escalating";
}

void TextNodeDumper::VisitCXXBindTemporaryExpr(
    const CXXBindTemporaryExpr *Node) {
  OS << " (CXXTemporary";
  dumpPointer(Node);
  OS << ")";
}

void TextNodeDumper::VisitCXXNewExpr(const CXXNewExpr *Node) {
  if (Node->isGlobalNew())
    OS << " global";
  if (Node->isArray())
    OS << " array";
  if (Node->getOperatorNew()) {
    OS << ' ';
    dumpBareDeclRef(Node->getOperatorNew());
  }
  // We could dump the deallocation function used in case of error, but it's
  // usually not that interesting.
}

void TextNodeDumper::VisitCXXDeleteExpr(const CXXDeleteExpr *Node) {
  if (Node->isGlobalDelete())
    OS << " global";
  if (Node->isArrayForm())
    OS << " array";
  if (Node->getOperatorDelete()) {
    OS << ' ';
    dumpBareDeclRef(Node->getOperatorDelete());
  }
}

void TextNodeDumper::VisitTypeTraitExpr(const TypeTraitExpr *Node) {
  OS << " " << getTraitSpelling(Node->getTrait());
}

void TextNodeDumper::VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *Node) {
  OS << " " << getTraitSpelling(Node->getTrait());
}

void TextNodeDumper::VisitExpressionTraitExpr(const ExpressionTraitExpr *Node) {
  OS << " " << getTraitSpelling(Node->getTrait());
}

void TextNodeDumper::VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *Node) {
  if (Node->hasRewrittenInit())
    OS << " has rewritten init";
}

void TextNodeDumper::VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *Node) {
  if (Node->hasRewrittenInit())
    OS << " has rewritten init";
}

void TextNodeDumper::VisitMaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *Node) {
  if (const ValueDecl *VD = Node->getExtendingDecl()) {
    OS << " extended by ";
    dumpBareDeclRef(VD);
  }
}

void TextNodeDumper::VisitExprWithCleanups(const ExprWithCleanups *Node) {
  for (unsigned i = 0, e = Node->getNumObjects(); i != e; ++i)
    dumpCleanupObject(Node->getObject(i));
}

void TextNodeDumper::VisitSizeOfPackExpr(const SizeOfPackExpr *Node) {
  dumpPointer(Node->getPack());
  dumpName(Node->getPack());
}

void TextNodeDumper::VisitCXXDependentScopeMemberExpr(
    const CXXDependentScopeMemberExpr *Node) {
  OS << " " << (Node->isArrow() ? "->" : ".") << Node->getMember();
}

void TextNodeDumper::VisitObjCMessageExpr(const ObjCMessageExpr *Node) {
  OS << " selector=";
  Node->getSelector().print(OS);
  switch (Node->getReceiverKind()) {
  case ObjCMessageExpr::Instance:
    break;

  case ObjCMessageExpr::Class:
    OS << " class=";
    dumpBareType(Node->getClassReceiver());
    break;

  case ObjCMessageExpr::SuperInstance:
    OS << " super (instance)";
    break;

  case ObjCMessageExpr::SuperClass:
    OS << " super (class)";
    break;
  }
}

void TextNodeDumper::VisitObjCBoxedExpr(const ObjCBoxedExpr *Node) {
  if (auto *BoxingMethod = Node->getBoxingMethod()) {
    OS << " selector=";
    BoxingMethod->getSelector().print(OS);
  }
}

void TextNodeDumper::VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node) {
  if (!Node->getCatchParamDecl())
    OS << " catch all";
}

void TextNodeDumper::VisitObjCEncodeExpr(const ObjCEncodeExpr *Node) {
  dumpType(Node->getEncodedType());
}

void TextNodeDumper::VisitObjCSelectorExpr(const ObjCSelectorExpr *Node) {
  OS << " ";
  Node->getSelector().print(OS);
}

void TextNodeDumper::VisitObjCProtocolExpr(const ObjCProtocolExpr *Node) {
  OS << ' ' << *Node->getProtocol();
}

void TextNodeDumper::VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *Node) {
  if (Node->isImplicitProperty()) {
    OS << " Kind=MethodRef Getter=\"";
    if (Node->getImplicitPropertyGetter())
      Node->getImplicitPropertyGetter()->getSelector().print(OS);
    else
      OS << "(null)";

    OS << "\" Setter=\"";
    if (ObjCMethodDecl *Setter = Node->getImplicitPropertySetter())
      Setter->getSelector().print(OS);
    else
      OS << "(null)";
    OS << "\"";
  } else {
    OS << " Kind=PropertyRef Property=\"" << *Node->getExplicitProperty()
       << '"';
  }

  if (Node->isSuperReceiver())
    OS << " super";

  OS << " Messaging=";
  if (Node->isMessagingGetter() && Node->isMessagingSetter())
    OS << "Getter&Setter";
  else if (Node->isMessagingGetter())
    OS << "Getter";
  else if (Node->isMessagingSetter())
    OS << "Setter";
}

void TextNodeDumper::VisitObjCSubscriptRefExpr(
    const ObjCSubscriptRefExpr *Node) {
  if (Node->isArraySubscriptRefExpr())
    OS << " Kind=ArraySubscript GetterForArray=\"";
  else
    OS << " Kind=DictionarySubscript GetterForDictionary=\"";
  if (Node->getAtIndexMethodDecl())
    Node->getAtIndexMethodDecl()->getSelector().print(OS);
  else
    OS << "(null)";

  if (Node->isArraySubscriptRefExpr())
    OS << "\" SetterForArray=\"";
  else
    OS << "\" SetterForDictionary=\"";
  if (Node->setAtIndexMethodDecl())
    Node->setAtIndexMethodDecl()->getSelector().print(OS);
  else
    OS << "(null)";
}

void TextNodeDumper::VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *Node) {
  OS << " " << (Node->getValue() ? "__objc_yes" : "__objc_no");
}

void TextNodeDumper::VisitOMPIteratorExpr(const OMPIteratorExpr *Node) {
  OS << " ";
  for (unsigned I = 0, E = Node->numOfIterators(); I < E; ++I) {
    Visit(Node->getIteratorDecl(I));
    OS << " = ";
    const OMPIteratorExpr::IteratorRange Range = Node->getIteratorRange(I);
    OS << " begin ";
    Visit(Range.Begin);
    OS << " end ";
    Visit(Range.End);
    if (Range.Step) {
      OS << " step ";
      Visit(Range.Step);
    }
  }
}

void TextNodeDumper::VisitConceptSpecializationExpr(
    const ConceptSpecializationExpr *Node) {
  OS << " ";
  dumpBareDeclRef(Node->getFoundDecl());
}

void TextNodeDumper::VisitRequiresExpr(
    const RequiresExpr *Node) {
  if (!Node->isValueDependent())
    OS << (Node->isSatisfied() ? " satisfied" : " unsatisfied");
}

void TextNodeDumper::VisitRValueReferenceType(const ReferenceType *T) {
  if (T->isSpelledAsLValue())
    OS << " written as lvalue reference";
}

void TextNodeDumper::VisitArrayType(const ArrayType *T) {
  switch (T->getSizeModifier()) {
  case ArraySizeModifier::Normal:
    break;
  case ArraySizeModifier::Static:
    OS << " static";
    break;
  case ArraySizeModifier::Star:
    OS << " *";
    break;
  }
  OS << " " << T->getIndexTypeQualifiers().getAsString();
}

void TextNodeDumper::VisitConstantArrayType(const ConstantArrayType *T) {
  OS << " " << T->getSize();
  VisitArrayType(T);
}

void TextNodeDumper::VisitVariableArrayType(const VariableArrayType *T) {
  OS << " ";
  dumpSourceRange(T->getBracketsRange());
  VisitArrayType(T);
}

void TextNodeDumper::VisitDependentSizedArrayType(
    const DependentSizedArrayType *T) {
  VisitArrayType(T);
  OS << " ";
  dumpSourceRange(T->getBracketsRange());
}

void TextNodeDumper::VisitDependentSizedExtVectorType(
    const DependentSizedExtVectorType *T) {
  OS << " ";
  dumpLocation(T->getAttributeLoc());
}

void TextNodeDumper::VisitVectorType(const VectorType *T) {
  switch (T->getVectorKind()) {
  case VectorKind::Generic:
    break;
  case VectorKind::AltiVecVector:
    OS << " altivec";
    break;
  case VectorKind::AltiVecPixel:
    OS << " altivec pixel";
    break;
  case VectorKind::AltiVecBool:
    OS << " altivec bool";
    break;
  case VectorKind::Neon:
    OS << " neon";
    break;
  case VectorKind::NeonPoly:
    OS << " neon poly";
    break;
  case VectorKind::SveFixedLengthData:
    OS << " fixed-length sve data vector";
    break;
  case VectorKind::SveFixedLengthPredicate:
    OS << " fixed-length sve predicate vector";
    break;
  case VectorKind::RVVFixedLengthData:
    OS << " fixed-length rvv data vector";
    break;
  case VectorKind::RVVFixedLengthMask:
    OS << " fixed-length rvv mask vector";
    break;
  }
  OS << " " << T->getNumElements();
}

void TextNodeDumper::VisitFunctionType(const FunctionType *T) {
  auto EI = T->getExtInfo();
  if (EI.getNoReturn())
    OS << " noreturn";
  if (EI.getProducesResult())
    OS << " produces_result";
  if (EI.getHasRegParm())
    OS << " regparm " << EI.getRegParm();
  OS << " " << FunctionType::getNameForCallConv(EI.getCC());
}

void TextNodeDumper::VisitFunctionProtoType(const FunctionProtoType *T) {
  auto EPI = T->getExtProtoInfo();
  if (EPI.HasTrailingReturn)
    OS << " trailing_return";
  if (T->isConst())
    OS << " const";
  if (T->isVolatile())
    OS << " volatile";
  if (T->isRestrict())
    OS << " restrict";
  if (T->getExtProtoInfo().Variadic)
    OS << " variadic";
  switch (EPI.RefQualifier) {
  case RQ_None:
    break;
  case RQ_LValue:
    OS << " &";
    break;
  case RQ_RValue:
    OS << " &&";
    break;
  }

  switch (EPI.ExceptionSpec.Type) {
  case EST_None:
    break;
  case EST_DynamicNone:
    OS << " exceptionspec_dynamic_none";
    break;
  case EST_Dynamic:
    OS << " exceptionspec_dynamic";
    break;
  case EST_MSAny:
    OS << " exceptionspec_ms_any";
    break;
  case EST_NoThrow:
    OS << " exceptionspec_nothrow";
    break;
  case EST_BasicNoexcept:
    OS << " exceptionspec_basic_noexcept";
    break;
  case EST_DependentNoexcept:
    OS << " exceptionspec_dependent_noexcept";
    break;
  case EST_NoexceptFalse:
    OS << " exceptionspec_noexcept_false";
    break;
  case EST_NoexceptTrue:
    OS << " exceptionspec_noexcept_true";
    break;
  case EST_Unevaluated:
    OS << " exceptionspec_unevaluated";
    break;
  case EST_Uninstantiated:
    OS << " exceptionspec_uninstantiated";
    break;
  case EST_Unparsed:
    OS << " exceptionspec_unparsed";
    break;
  }
  if (!EPI.ExceptionSpec.Exceptions.empty()) {
    AddChild([=] {
      OS << "Exceptions:";
      for (unsigned I = 0, N = EPI.ExceptionSpec.Exceptions.size(); I != N;
           ++I) {
        if (I)
          OS << ",";
        dumpType(EPI.ExceptionSpec.Exceptions[I]);
      }
    });
  }
  if (EPI.ExceptionSpec.NoexceptExpr) {
    AddChild([=] {
      OS << "NoexceptExpr: ";
      Visit(EPI.ExceptionSpec.NoexceptExpr);
    });
  }
  dumpDeclRef(EPI.ExceptionSpec.SourceDecl, "ExceptionSourceDecl");
  dumpDeclRef(EPI.ExceptionSpec.SourceTemplate, "ExceptionSourceTemplate");

  // FIXME: Consumed parameters.
  VisitFunctionType(T);
}

void TextNodeDumper::VisitUnresolvedUsingType(const UnresolvedUsingType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitUsingType(const UsingType *T) {
  dumpDeclRef(T->getFoundDecl());
  if (!T->typeMatchesDecl())
    OS << " divergent";
}

void TextNodeDumper::VisitTypedefType(const TypedefType *T) {
  dumpDeclRef(T->getDecl());
  if (!T->typeMatchesDecl())
    OS << " divergent";
}

void TextNodeDumper::VisitUnaryTransformType(const UnaryTransformType *T) {
  switch (T->getUTTKind()) {
#define TRANSFORM_TYPE_TRAIT_DEF(Enum, Trait)                                  \
  case UnaryTransformType::Enum:                                               \
    OS << " " #Trait;                                                          \
    break;
#include "clang/Basic/TransformTypeTraits.def"
  }
}

void TextNodeDumper::VisitTagType(const TagType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
  OS << " depth " << T->getDepth() << " index " << T->getIndex();
  if (T->isParameterPack())
    OS << " pack";
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitSubstTemplateTypeParmType(
    const SubstTemplateTypeParmType *T) {
  dumpDeclRef(T->getAssociatedDecl());
  VisitTemplateTypeParmDecl(T->getReplacedParameter());
  if (auto PackIndex = T->getPackIndex())
    OS << " pack_index " << *PackIndex;
}

void TextNodeDumper::VisitSubstTemplateTypeParmPackType(
    const SubstTemplateTypeParmPackType *T) {
  dumpDeclRef(T->getAssociatedDecl());
  VisitTemplateTypeParmDecl(T->getReplacedParameter());
}

void TextNodeDumper::VisitAutoType(const AutoType *T) {
  if (T->isDecltypeAuto())
    OS << " decltype(auto)";
  if (!T->isDeduced())
    OS << " undeduced";
  if (T->isConstrained())
    dumpDeclRef(T->getTypeConstraintConcept());
}

void TextNodeDumper::VisitDeducedTemplateSpecializationType(
    const DeducedTemplateSpecializationType *T) {
  dumpTemplateName(T->getTemplateName(), "name");
}

void TextNodeDumper::VisitTemplateSpecializationType(
    const TemplateSpecializationType *T) {
  if (T->isTypeAlias())
    OS << " alias";
  dumpTemplateName(T->getTemplateName(), "name");
}

void TextNodeDumper::VisitInjectedClassNameType(
    const InjectedClassNameType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitObjCInterfaceType(const ObjCInterfaceType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitPackExpansionType(const PackExpansionType *T) {
  if (auto N = T->getNumExpansions())
    OS << " expansions " << *N;
}

void TextNodeDumper::VisitTypeLoc(TypeLoc TL) {
  // By default, add extra Type details with no extra loc info.
  TypeVisitor<TextNodeDumper>::Visit(TL.getTypePtr());
}
// FIXME: override behavior for TypeLocs that have interesting location
// information, such as the qualifier in ElaboratedTypeLoc.

void TextNodeDumper::VisitLabelDecl(const LabelDecl *D) { dumpName(D); }

void TextNodeDumper::VisitTypedefDecl(const TypedefDecl *D) {
  dumpName(D);
  dumpType(D->getUnderlyingType());
  if (D->isModulePrivate())
    OS << " __module_private__";
}

void TextNodeDumper::VisitEnumDecl(const EnumDecl *D) {
  if (D->isScoped()) {
    if (D->isScopedUsingClassTag())
      OS << " class";
    else
      OS << " struct";
  }
  dumpName(D);
  if (D->isModulePrivate())
    OS << " __module_private__";
  if (D->isFixed())
    dumpType(D->getIntegerType());
}

void TextNodeDumper::VisitRecordDecl(const RecordDecl *D) {
  OS << ' ' << D->getKindName();
  dumpName(D);
  if (D->isModulePrivate())
    OS << " __module_private__";
  if (D->isCompleteDefinition())
    OS << " definition";
}

void TextNodeDumper::VisitEnumConstantDecl(const EnumConstantDecl *D) {
  dumpName(D);
  dumpType(D->getType());
}

void TextNodeDumper::VisitIndirectFieldDecl(const IndirectFieldDecl *D) {
  dumpName(D);
  dumpType(D->getType());

  for (const auto *Child : D->chain())
    dumpDeclRef(Child);
}

void TextNodeDumper::VisitFunctionDecl(const FunctionDecl *D) {
  dumpName(D);
  dumpType(D->getType());
  dumpTemplateSpecializationKind(D->getTemplateSpecializationKind());

  StorageClass SC = D->getStorageClass();
  if (SC != SC_None)
    OS << ' ' << VarDecl::getStorageClassSpecifierString(SC);
  if (D->isInlineSpecified())
    OS << " inline";
  if (D->isVirtualAsWritten())
    OS << " virtual";
  if (D->isModulePrivate())
    OS << " __module_private__";

  if (D->isPureVirtual())
    OS << " pure";
  if (D->isDefaulted()) {
    OS << " default";
    if (D->isDeleted())
      OS << "_delete";
  }
  if (D->isDeletedAsWritten())
    OS << " delete";
  if (D->isTrivial())
    OS << " trivial";

  if (const StringLiteral *M = D->getDeletedMessage())
    AddChild("delete message", [=] { Visit(M); });

  if (D->isIneligibleOrNotSelected())
    OS << (isa<CXXDestructorDecl>(D) ? " not_selected" : " ineligible");

  if (const auto *FPT = D->getType()->getAs<FunctionProtoType>()) {
    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
    switch (EPI.ExceptionSpec.Type) {
    default:
      break;
    case EST_Unevaluated:
      OS << " noexcept-unevaluated " << EPI.ExceptionSpec.SourceDecl;
      break;
    case EST_Uninstantiated:
      OS << " noexcept-uninstantiated " << EPI.ExceptionSpec.SourceTemplate;
      break;
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (MD->size_overridden_methods() != 0) {
      auto dumpOverride = [=](const CXXMethodDecl *D) {
        SplitQualType T_split = D->getType().split();
        OS << D << " " << D->getParent()->getName() << "::" << D->getDeclName()
           << " '" << QualType::getAsString(T_split, PrintPolicy) << "'";
      };

      AddChild([=] {
        auto Overrides = MD->overridden_methods();
        OS << "Overrides: [ ";
        dumpOverride(*Overrides.begin());
        for (const auto *Override : llvm::drop_begin(Overrides)) {
          OS << ", ";
          dumpOverride(Override);
        }
        OS << " ]";
      });
    }
  }

  if (!D->isInlineSpecified() && D->isInlined()) {
    OS << " implicit-inline";
  }
  // Since NumParams comes from the FunctionProtoType of the FunctionDecl and
  // the Params are set later, it is possible for a dump during debugging to
  // encounter a FunctionDecl that has been created but hasn't been assigned
  // ParmVarDecls yet.
  if (!D->param_empty() && !D->param_begin())
    OS << " <<<NULL params x " << D->getNumParams() << ">>>";

  if (const auto *Instance = D->getInstantiatedFromMemberFunction()) {
    OS << " instantiated_from";
    dumpPointer(Instance);
  }
}

void TextNodeDumper::VisitCXXDeductionGuideDecl(
    const CXXDeductionGuideDecl *D) {
  VisitFunctionDecl(D);
  switch (D->getDeductionCandidateKind()) {
  case DeductionCandidate::Normal:
  case DeductionCandidate::Copy:
    return;
  case DeductionCandidate::Aggregate:
    OS << " aggregate ";
    break;
  }
}

void TextNodeDumper::VisitLifetimeExtendedTemporaryDecl(
    const LifetimeExtendedTemporaryDecl *D) {
  OS << " extended by ";
  dumpBareDeclRef(D->getExtendingDecl());
  OS << " mangling ";
  {
    ColorScope Color(OS, ShowColors, ValueColor);
    OS << D->getManglingNumber();
  }
}

void TextNodeDumper::VisitFieldDecl(const FieldDecl *D) {
  dumpName(D);
  dumpType(D->getType());
  if (D->isMutable())
    OS << " mutable";
  if (D->isModulePrivate())
    OS << " __module_private__";
}

void TextNodeDumper::VisitVarDecl(const VarDecl *D) {
  dumpNestedNameSpecifier(D->getQualifier());
  dumpName(D);
  if (const auto *P = dyn_cast<ParmVarDecl>(D);
      P && P->isExplicitObjectParameter())
    OS << " this";

  dumpType(D->getType());
  dumpTemplateSpecializationKind(D->getTemplateSpecializationKind());
  StorageClass SC = D->getStorageClass();
  if (SC != SC_None)
    OS << ' ' << VarDecl::getStorageClassSpecifierString(SC);
  switch (D->getTLSKind()) {
  case VarDecl::TLS_None:
    break;
  case VarDecl::TLS_Static:
    OS << " tls";
    break;
  case VarDecl::TLS_Dynamic:
    OS << " tls_dynamic";
    break;
  }
  if (D->isModulePrivate())
    OS << " __module_private__";
  if (D->isNRVOVariable())
    OS << " nrvo";
  if (D->isInline())
    OS << " inline";
  if (D->isConstexpr())
    OS << " constexpr";
  if (D->hasInit()) {
    switch (D->getInitStyle()) {
    case VarDecl::CInit:
      OS << " cinit";
      break;
    case VarDecl::CallInit:
      OS << " callinit";
      break;
    case VarDecl::ListInit:
      OS << " listinit";
      break;
    case VarDecl::ParenListInit:
      OS << " parenlistinit";
    }
  }
  if (D->needsDestruction(D->getASTContext()))
    OS << " destroyed";
  if (D->isParameterPack())
    OS << " pack";

  if (D->hasInit()) {
    const Expr *E = D->getInit();
    // Only dump the value of constexpr VarDecls for now.
    if (E && !E->isValueDependent() && D->isConstexpr() &&
        !D->getType()->isDependentType()) {
      const APValue *Value = D->evaluateValue();
      if (Value)
        AddChild("value", [=] { Visit(*Value, E->getType()); });
    }
  }
}

void TextNodeDumper::VisitBindingDecl(const BindingDecl *D) {
  dumpName(D);
  dumpType(D->getType());
}

void TextNodeDumper::VisitCapturedDecl(const CapturedDecl *D) {
  if (D->isNothrow())
    OS << " nothrow";
}

void TextNodeDumper::VisitImportDecl(const ImportDecl *D) {
  OS << ' ' << D->getImportedModule()->getFullModuleName();

  for (Decl *InitD :
       D->getASTContext().getModuleInitializers(D->getImportedModule()))
    dumpDeclRef(InitD, "initializer");
}

void TextNodeDumper::VisitPragmaCommentDecl(const PragmaCommentDecl *D) {
  OS << ' ';
  switch (D->getCommentKind()) {
  case PCK_Unknown:
    llvm_unreachable("unexpected pragma comment kind");
  case PCK_Compiler:
    OS << "compiler";
    break;
  case PCK_ExeStr:
    OS << "exestr";
    break;
  case PCK_Lib:
    OS << "lib";
    break;
  case PCK_Linker:
    OS << "linker";
    break;
  case PCK_User:
    OS << "user";
    break;
  }
  StringRef Arg = D->getArg();
  if (!Arg.empty())
    OS << " \"" << Arg << "\"";
}

void TextNodeDumper::VisitPragmaDetectMismatchDecl(
    const PragmaDetectMismatchDecl *D) {
  OS << " \"" << D->getName() << "\" \"" << D->getValue() << "\"";
}

void TextNodeDumper::VisitOMPExecutableDirective(
    const OMPExecutableDirective *D) {
  if (D->isStandaloneDirective())
    OS << " openmp_standalone_directive";
}

void TextNodeDumper::VisitOMPDeclareReductionDecl(
    const OMPDeclareReductionDecl *D) {
  dumpName(D);
  dumpType(D->getType());
  OS << " combiner";
  dumpPointer(D->getCombiner());
  if (const auto *Initializer = D->getInitializer()) {
    OS << " initializer";
    dumpPointer(Initializer);
    switch (D->getInitializerKind()) {
    case OMPDeclareReductionInitKind::Direct:
      OS << " omp_priv = ";
      break;
    case OMPDeclareReductionInitKind::Copy:
      OS << " omp_priv ()";
      break;
    case OMPDeclareReductionInitKind::Call:
      break;
    }
  }
}

void TextNodeDumper::VisitOMPRequiresDecl(const OMPRequiresDecl *D) {
  for (const auto *C : D->clauselists()) {
    AddChild([=] {
      if (!C) {
        ColorScope Color(OS, ShowColors, NullColor);
        OS << "<<<NULL>>> OMPClause";
        return;
      }
      {
        ColorScope Color(OS, ShowColors, AttrColor);
        StringRef ClauseName(
            llvm::omp::getOpenMPClauseName(C->getClauseKind()));
        OS << "OMP" << ClauseName.substr(/*Start=*/0, /*N=*/1).upper()
           << ClauseName.drop_front() << "Clause";
      }
      dumpPointer(C);
      dumpSourceRange(SourceRange(C->getBeginLoc(), C->getEndLoc()));
    });
  }
}

void TextNodeDumper::VisitOMPCapturedExprDecl(const OMPCapturedExprDecl *D) {
  dumpName(D);
  dumpType(D->getType());
}

void TextNodeDumper::VisitNamespaceDecl(const NamespaceDecl *D) {
  dumpName(D);
  if (D->isInline())
    OS << " inline";
  if (D->isNested())
    OS << " nested";
  if (!D->isFirstDecl())
    dumpDeclRef(D->getFirstDecl(), "original");
}

void TextNodeDumper::VisitUsingDirectiveDecl(const UsingDirectiveDecl *D) {
  OS << ' ';
  dumpBareDeclRef(D->getNominatedNamespace());
}

void TextNodeDumper::VisitNamespaceAliasDecl(const NamespaceAliasDecl *D) {
  dumpName(D);
  dumpDeclRef(D->getAliasedNamespace());
}

void TextNodeDumper::VisitTypeAliasDecl(const TypeAliasDecl *D) {
  dumpName(D);
  dumpType(D->getUnderlyingType());
}

void TextNodeDumper::VisitTypeAliasTemplateDecl(
    const TypeAliasTemplateDecl *D) {
  dumpName(D);
}

void TextNodeDumper::VisitCXXRecordDecl(const CXXRecordDecl *D) {
  VisitRecordDecl(D);
  if (const auto *Instance = D->getInstantiatedFromMemberClass()) {
    OS << " instantiated_from";
    dumpPointer(Instance);
  }
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
    dumpTemplateSpecializationKind(CTSD->getSpecializationKind());

  dumpNestedNameSpecifier(D->getQualifier());

  if (!D->isCompleteDefinition())
    return;

  AddChild([=] {
    {
      ColorScope Color(OS, ShowColors, DeclKindNameColor);
      OS << "DefinitionData";
    }
#define FLAG(fn, name)                                                         \
  if (D->fn())                                                                 \
    OS << " " #name;
    FLAG(isParsingBaseSpecifiers, parsing_base_specifiers);

    FLAG(isGenericLambda, generic);
    FLAG(isLambda, lambda);

    FLAG(isAnonymousStructOrUnion, is_anonymous);
    FLAG(canPassInRegisters, pass_in_registers);
    FLAG(isEmpty, empty);
    FLAG(isAggregate, aggregate);
    FLAG(isStandardLayout, standard_layout);
    FLAG(isTriviallyCopyable, trivially_copyable);
    FLAG(isPOD, pod);
    FLAG(isTrivial, trivial);
    FLAG(isPolymorphic, polymorphic);
    FLAG(isAbstract, abstract);
    FLAG(isLiteral, literal);

    FLAG(hasUserDeclaredConstructor, has_user_declared_ctor);
    FLAG(hasConstexprNonCopyMoveConstructor, has_constexpr_non_copy_move_ctor);
    FLAG(hasMutableFields, has_mutable_fields);
    FLAG(hasVariantMembers, has_variant_members);
    FLAG(allowConstDefaultInit, can_const_default_init);

    AddChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "DefaultConstructor";
      }
      FLAG(hasDefaultConstructor, exists);
      FLAG(hasTrivialDefaultConstructor, trivial);
      FLAG(hasNonTrivialDefaultConstructor, non_trivial);
      FLAG(hasUserProvidedDefaultConstructor, user_provided);
      FLAG(hasConstexprDefaultConstructor, constexpr);
      FLAG(needsImplicitDefaultConstructor, needs_implicit);
      FLAG(defaultedDefaultConstructorIsConstexpr, defaulted_is_constexpr);
    });

    AddChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "CopyConstructor";
      }
      FLAG(hasSimpleCopyConstructor, simple);
      FLAG(hasTrivialCopyConstructor, trivial);
      FLAG(hasNonTrivialCopyConstructor, non_trivial);
      FLAG(hasUserDeclaredCopyConstructor, user_declared);
      FLAG(hasCopyConstructorWithConstParam, has_const_param);
      FLAG(needsImplicitCopyConstructor, needs_implicit);
      FLAG(needsOverloadResolutionForCopyConstructor,
           needs_overload_resolution);
      if (!D->needsOverloadResolutionForCopyConstructor())
        FLAG(defaultedCopyConstructorIsDeleted, defaulted_is_deleted);
      FLAG(implicitCopyConstructorHasConstParam, implicit_has_const_param);
    });

    AddChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "MoveConstructor";
      }
      FLAG(hasMoveConstructor, exists);
      FLAG(hasSimpleMoveConstructor, simple);
      FLAG(hasTrivialMoveConstructor, trivial);
      FLAG(hasNonTrivialMoveConstructor, non_trivial);
      FLAG(hasUserDeclaredMoveConstructor, user_declared);
      FLAG(needsImplicitMoveConstructor, needs_implicit);
      FLAG(needsOverloadResolutionForMoveConstructor,
           needs_overload_resolution);
      if (!D->needsOverloadResolutionForMoveConstructor())
        FLAG(defaultedMoveConstructorIsDeleted, defaulted_is_deleted);
    });

    AddChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "CopyAssignment";
      }
      FLAG(hasSimpleCopyAssignment, simple);
      FLAG(hasTrivialCopyAssignment, trivial);
      FLAG(hasNonTrivialCopyAssignment, non_trivial);
      FLAG(hasCopyAssignmentWithConstParam, has_const_param);
      FLAG(hasUserDeclaredCopyAssignment, user_declared);
      FLAG(needsImplicitCopyAssignment, needs_implicit);
      FLAG(needsOverloadResolutionForCopyAssignment, needs_overload_resolution);
      FLAG(implicitCopyAssignmentHasConstParam, implicit_has_const_param);
    });

    AddChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "MoveAssignment";
      }
      FLAG(hasMoveAssignment, exists);
      FLAG(hasSimpleMoveAssignment, simple);
      FLAG(hasTrivialMoveAssignment, trivial);
      FLAG(hasNonTrivialMoveAssignment, non_trivial);
      FLAG(hasUserDeclaredMoveAssignment, user_declared);
      FLAG(needsImplicitMoveAssignment, needs_implicit);
      FLAG(needsOverloadResolutionForMoveAssignment, needs_overload_resolution);
    });

    AddChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "Destructor";
      }
      FLAG(hasSimpleDestructor, simple);
      FLAG(hasIrrelevantDestructor, irrelevant);
      FLAG(hasTrivialDestructor, trivial);
      FLAG(hasNonTrivialDestructor, non_trivial);
      FLAG(hasUserDeclaredDestructor, user_declared);
      FLAG(hasConstexprDestructor, constexpr);
      FLAG(needsImplicitDestructor, needs_implicit);
      FLAG(needsOverloadResolutionForDestructor, needs_overload_resolution);
      if (!D->needsOverloadResolutionForDestructor())
        FLAG(defaultedDestructorIsDeleted, defaulted_is_deleted);
    });
  });

  for (const auto &I : D->bases()) {
    AddChild([=] {
      if (I.isVirtual())
        OS << "virtual ";
      dumpAccessSpecifier(I.getAccessSpecifier());
      dumpType(I.getType());
      if (I.isPackExpansion())
        OS << "...";
    });
  }
}

void TextNodeDumper::VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
  dumpName(D);
}

void TextNodeDumper::VisitClassTemplateDecl(const ClassTemplateDecl *D) {
  dumpName(D);
}

void TextNodeDumper::VisitVarTemplateDecl(const VarTemplateDecl *D) {
  dumpName(D);
}

void TextNodeDumper::VisitBuiltinTemplateDecl(const BuiltinTemplateDecl *D) {
  dumpName(D);
}

void TextNodeDumper::VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D) {
  if (const auto *TC = D->getTypeConstraint()) {
    OS << " ";
    dumpBareDeclRef(TC->getNamedConcept());
    if (TC->getNamedConcept() != TC->getFoundDecl()) {
      OS << " (";
      dumpBareDeclRef(TC->getFoundDecl());
      OS << ")";
    }
  } else if (D->wasDeclaredWithTypename())
    OS << " typename";
  else
    OS << " class";
  OS << " depth " << D->getDepth() << " index " << D->getIndex();
  if (D->isParameterPack())
    OS << " ...";
  dumpName(D);
}

void TextNodeDumper::VisitNonTypeTemplateParmDecl(
    const NonTypeTemplateParmDecl *D) {
  dumpType(D->getType());
  OS << " depth " << D->getDepth() << " index " << D->getIndex();
  if (D->isParameterPack())
    OS << " ...";
  dumpName(D);
}

void TextNodeDumper::VisitTemplateTemplateParmDecl(
    const TemplateTemplateParmDecl *D) {
  OS << " depth " << D->getDepth() << " index " << D->getIndex();
  if (D->isParameterPack())
    OS << " ...";
  dumpName(D);
}

void TextNodeDumper::VisitUsingDecl(const UsingDecl *D) {
  OS << ' ';
  if (D->getQualifier())
    D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
  OS << D->getDeclName();
  dumpNestedNameSpecifier(D->getQualifier());
}

void TextNodeDumper::VisitUsingEnumDecl(const UsingEnumDecl *D) {
  OS << ' ';
  dumpBareDeclRef(D->getEnumDecl());
}

void TextNodeDumper::VisitUnresolvedUsingTypenameDecl(
    const UnresolvedUsingTypenameDecl *D) {
  OS << ' ';
  if (D->getQualifier())
    D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
  OS << D->getDeclName();
}

void TextNodeDumper::VisitUnresolvedUsingValueDecl(
    const UnresolvedUsingValueDecl *D) {
  OS << ' ';
  if (D->getQualifier())
    D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
  OS << D->getDeclName();
  dumpType(D->getType());
}

void TextNodeDumper::VisitUsingShadowDecl(const UsingShadowDecl *D) {
  OS << ' ';
  dumpBareDeclRef(D->getTargetDecl());
}

void TextNodeDumper::VisitConstructorUsingShadowDecl(
    const ConstructorUsingShadowDecl *D) {
  if (D->constructsVirtualBase())
    OS << " virtual";

  AddChild([=] {
    OS << "target ";
    dumpBareDeclRef(D->getTargetDecl());
  });

  AddChild([=] {
    OS << "nominated ";
    dumpBareDeclRef(D->getNominatedBaseClass());
    OS << ' ';
    dumpBareDeclRef(D->getNominatedBaseClassShadowDecl());
  });

  AddChild([=] {
    OS << "constructed ";
    dumpBareDeclRef(D->getConstructedBaseClass());
    OS << ' ';
    dumpBareDeclRef(D->getConstructedBaseClassShadowDecl());
  });
}

void TextNodeDumper::VisitLinkageSpecDecl(const LinkageSpecDecl *D) {
  switch (D->getLanguage()) {
  case LinkageSpecLanguageIDs::C:
    OS << " C";
    break;
  case LinkageSpecLanguageIDs::CXX:
    OS << " C++";
    break;
  }
}

void TextNodeDumper::VisitAccessSpecDecl(const AccessSpecDecl *D) {
  OS << ' ';
  dumpAccessSpecifier(D->getAccess());
}

void TextNodeDumper::VisitFriendDecl(const FriendDecl *D) {
  if (TypeSourceInfo *T = D->getFriendType())
    dumpType(T->getType());
}

void TextNodeDumper::VisitObjCIvarDecl(const ObjCIvarDecl *D) {
  dumpName(D);
  dumpType(D->getType());
  if (D->getSynthesize())
    OS << " synthesize";

  switch (D->getAccessControl()) {
  case ObjCIvarDecl::None:
    OS << " none";
    break;
  case ObjCIvarDecl::Private:
    OS << " private";
    break;
  case ObjCIvarDecl::Protected:
    OS << " protected";
    break;
  case ObjCIvarDecl::Public:
    OS << " public";
    break;
  case ObjCIvarDecl::Package:
    OS << " package";
    break;
  }
}

void TextNodeDumper::VisitObjCMethodDecl(const ObjCMethodDecl *D) {
  if (D->isInstanceMethod())
    OS << " -";
  else
    OS << " +";
  dumpName(D);
  dumpType(D->getReturnType());

  if (D->isVariadic())
    OS << " variadic";
}

void TextNodeDumper::VisitObjCTypeParamDecl(const ObjCTypeParamDecl *D) {
  dumpName(D);
  switch (D->getVariance()) {
  case ObjCTypeParamVariance::Invariant:
    break;

  case ObjCTypeParamVariance::Covariant:
    OS << " covariant";
    break;

  case ObjCTypeParamVariance::Contravariant:
    OS << " contravariant";
    break;
  }

  if (D->hasExplicitBound())
    OS << " bounded";
  dumpType(D->getUnderlyingType());
}

void TextNodeDumper::VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
  dumpName(D);
  dumpDeclRef(D->getClassInterface());
  dumpDeclRef(D->getImplementation());
  for (const auto *P : D->protocols())
    dumpDeclRef(P);
}

void TextNodeDumper::VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D) {
  dumpName(D);
  dumpDeclRef(D->getClassInterface());
  dumpDeclRef(D->getCategoryDecl());
}

void TextNodeDumper::VisitObjCProtocolDecl(const ObjCProtocolDecl *D) {
  dumpName(D);

  for (const auto *Child : D->protocols())
    dumpDeclRef(Child);
}

void TextNodeDumper::VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
  dumpName(D);
  dumpDeclRef(D->getSuperClass(), "super");

  dumpDeclRef(D->getImplementation());
  for (const auto *Child : D->protocols())
    dumpDeclRef(Child);
}

void TextNodeDumper::VisitObjCImplementationDecl(
    const ObjCImplementationDecl *D) {
  dumpName(D);
  dumpDeclRef(D->getSuperClass(), "super");
  dumpDeclRef(D->getClassInterface());
}

void TextNodeDumper::VisitObjCCompatibleAliasDecl(
    const ObjCCompatibleAliasDecl *D) {
  dumpName(D);
  dumpDeclRef(D->getClassInterface());
}

void TextNodeDumper::VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
  dumpName(D);
  dumpType(D->getType());

  if (D->getPropertyImplementation() == ObjCPropertyDecl::Required)
    OS << " required";
  else if (D->getPropertyImplementation() == ObjCPropertyDecl::Optional)
    OS << " optional";

  ObjCPropertyAttribute::Kind Attrs = D->getPropertyAttributes();
  if (Attrs != ObjCPropertyAttribute::kind_noattr) {
    if (Attrs & ObjCPropertyAttribute::kind_readonly)
      OS << " readonly";
    if (Attrs & ObjCPropertyAttribute::kind_assign)
      OS << " assign";
    if (Attrs & ObjCPropertyAttribute::kind_readwrite)
      OS << " readwrite";
    if (Attrs & ObjCPropertyAttribute::kind_retain)
      OS << " retain";
    if (Attrs & ObjCPropertyAttribute::kind_copy)
      OS << " copy";
    if (Attrs & ObjCPropertyAttribute::kind_nonatomic)
      OS << " nonatomic";
    if (Attrs & ObjCPropertyAttribute::kind_atomic)
      OS << " atomic";
    if (Attrs & ObjCPropertyAttribute::kind_weak)
      OS << " weak";
    if (Attrs & ObjCPropertyAttribute::kind_strong)
      OS << " strong";
    if (Attrs & ObjCPropertyAttribute::kind_unsafe_unretained)
      OS << " unsafe_unretained";
    if (Attrs & ObjCPropertyAttribute::kind_class)
      OS << " class";
    if (Attrs & ObjCPropertyAttribute::kind_direct)
      OS << " direct";
    if (Attrs & ObjCPropertyAttribute::kind_getter)
      dumpDeclRef(D->getGetterMethodDecl(), "getter");
    if (Attrs & ObjCPropertyAttribute::kind_setter)
      dumpDeclRef(D->getSetterMethodDecl(), "setter");
  }
}

void TextNodeDumper::VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D) {
  dumpName(D->getPropertyDecl());
  if (D->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize)
    OS << " synthesize";
  else
    OS << " dynamic";
  dumpDeclRef(D->getPropertyDecl());
  dumpDeclRef(D->getPropertyIvarDecl());
}

void TextNodeDumper::VisitBlockDecl(const BlockDecl *D) {
  if (D->isVariadic())
    OS << " variadic";

  if (D->capturesCXXThis())
    OS << " captures_this";
}

void TextNodeDumper::VisitConceptDecl(const ConceptDecl *D) {
  dumpName(D);
}

void TextNodeDumper::VisitCompoundStmt(const CompoundStmt *S) {
  VisitStmt(S);
  if (S->hasStoredFPFeatures())
    printFPOptions(S->getStoredFPFeatures());
}

void TextNodeDumper::VisitHLSLBufferDecl(const HLSLBufferDecl *D) {
  if (D->isCBuffer())
    OS << " cbuffer";
  else
    OS << " tbuffer";
  dumpName(D);
}

void TextNodeDumper::VisitOpenACCConstructStmt(const OpenACCConstructStmt *S) {
  OS << " " << S->getDirectiveKind();
}
void TextNodeDumper::VisitOpenACCLoopConstruct(const OpenACCLoopConstruct *S) {

  if (S->isOrphanedLoopConstruct())
    OS << " <orphan>";
  else
    OS << " parent: " << S->getParentComputeConstruct();
}

void TextNodeDumper::VisitEmbedExpr(const EmbedExpr *S) {
  AddChild("begin", [=] { OS << S->getStartingElementPos(); });
  AddChild("number of elements", [=] { OS << S->getDataElementCount(); });
}
