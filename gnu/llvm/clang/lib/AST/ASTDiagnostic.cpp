//===--- ASTDiagnostic.cpp - Diagnostic Printing Hooks for AST Nodes ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a diagnostic formatting hook for AST elements.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

// Returns a desugared version of the QualType, and marks ShouldAKA as true
// whenever we remove significant sugar from the type. Make sure ShouldAKA
// is initialized before passing it in.
QualType clang::desugarForDiagnostic(ASTContext &Context, QualType QT,
                                     bool &ShouldAKA) {
  QualifierCollector QC;

  while (true) {
    const Type *Ty = QC.strip(QT);

    // Don't aka just because we saw an elaborated type...
    if (const ElaboratedType *ET = dyn_cast<ElaboratedType>(Ty)) {
      QT = ET->desugar();
      continue;
    }
    // ... or a using type ...
    if (const UsingType *UT = dyn_cast<UsingType>(Ty)) {
      QT = UT->desugar();
      continue;
    }
    // ... or a paren type ...
    if (const ParenType *PT = dyn_cast<ParenType>(Ty)) {
      QT = PT->desugar();
      continue;
    }
    // ... or a macro defined type ...
    if (const MacroQualifiedType *MDT = dyn_cast<MacroQualifiedType>(Ty)) {
      QT = MDT->desugar();
      continue;
    }
    // ...or a substituted template type parameter ...
    if (const SubstTemplateTypeParmType *ST =
          dyn_cast<SubstTemplateTypeParmType>(Ty)) {
      QT = ST->desugar();
      continue;
    }
    // ...or an attributed type...
    if (const AttributedType *AT = dyn_cast<AttributedType>(Ty)) {
      QT = AT->desugar();
      continue;
    }
    // ...or an adjusted type...
    if (const AdjustedType *AT = dyn_cast<AdjustedType>(Ty)) {
      QT = AT->desugar();
      continue;
    }
    // ... or an auto type.
    if (const AutoType *AT = dyn_cast<AutoType>(Ty)) {
      if (!AT->isSugared())
        break;
      QT = AT->desugar();
      continue;
    }

    // Desugar FunctionType if return type or any parameter type should be
    // desugared. Preserve nullability attribute on desugared types.
    if (const FunctionType *FT = dyn_cast<FunctionType>(Ty)) {
      bool DesugarReturn = false;
      QualType SugarRT = FT->getReturnType();
      QualType RT = desugarForDiagnostic(Context, SugarRT, DesugarReturn);
      if (auto nullability = AttributedType::stripOuterNullability(SugarRT)) {
        RT = Context.getAttributedType(
            AttributedType::getNullabilityAttrKind(*nullability), RT, RT);
      }

      bool DesugarArgument = false;
      SmallVector<QualType, 4> Args;
      const FunctionProtoType *FPT = dyn_cast<FunctionProtoType>(FT);
      if (FPT) {
        for (QualType SugarPT : FPT->param_types()) {
          QualType PT = desugarForDiagnostic(Context, SugarPT, DesugarArgument);
          if (auto nullability =
                  AttributedType::stripOuterNullability(SugarPT)) {
            PT = Context.getAttributedType(
                AttributedType::getNullabilityAttrKind(*nullability), PT, PT);
          }
          Args.push_back(PT);
        }
      }

      if (DesugarReturn || DesugarArgument) {
        ShouldAKA = true;
        QT = FPT ? Context.getFunctionType(RT, Args, FPT->getExtProtoInfo())
                 : Context.getFunctionNoProtoType(RT, FT->getExtInfo());
        break;
      }
    }

    // Desugar template specializations if any template argument should be
    // desugared.
    if (const TemplateSpecializationType *TST =
            dyn_cast<TemplateSpecializationType>(Ty)) {
      if (!TST->isTypeAlias()) {
        bool DesugarArgument = false;
        SmallVector<TemplateArgument, 4> Args;
        for (const TemplateArgument &Arg : TST->template_arguments()) {
          if (Arg.getKind() == TemplateArgument::Type)
            Args.push_back(desugarForDiagnostic(Context, Arg.getAsType(),
                                                DesugarArgument));
          else
            Args.push_back(Arg);
        }

        if (DesugarArgument) {
          ShouldAKA = true;
          QT = Context.getTemplateSpecializationType(
              TST->getTemplateName(), Args, QT);
        }
        break;
      }
    }

    if (const auto *AT = dyn_cast<ArrayType>(Ty)) {
      QualType ElementTy =
          desugarForDiagnostic(Context, AT->getElementType(), ShouldAKA);
      if (const auto *CAT = dyn_cast<ConstantArrayType>(AT))
        QT = Context.getConstantArrayType(
            ElementTy, CAT->getSize(), CAT->getSizeExpr(),
            CAT->getSizeModifier(), CAT->getIndexTypeCVRQualifiers());
      else if (const auto *VAT = dyn_cast<VariableArrayType>(AT))
        QT = Context.getVariableArrayType(
            ElementTy, VAT->getSizeExpr(), VAT->getSizeModifier(),
            VAT->getIndexTypeCVRQualifiers(), VAT->getBracketsRange());
      else if (const auto *DSAT = dyn_cast<DependentSizedArrayType>(AT))
        QT = Context.getDependentSizedArrayType(
            ElementTy, DSAT->getSizeExpr(), DSAT->getSizeModifier(),
            DSAT->getIndexTypeCVRQualifiers(), DSAT->getBracketsRange());
      else if (const auto *IAT = dyn_cast<IncompleteArrayType>(AT))
        QT = Context.getIncompleteArrayType(ElementTy, IAT->getSizeModifier(),
                                            IAT->getIndexTypeCVRQualifiers());
      else
        llvm_unreachable("Unhandled array type");
      break;
    }

    // Don't desugar magic Objective-C types.
    if (QualType(Ty,0) == Context.getObjCIdType() ||
        QualType(Ty,0) == Context.getObjCClassType() ||
        QualType(Ty,0) == Context.getObjCSelType() ||
        QualType(Ty,0) == Context.getObjCProtoType())
      break;

    // Don't desugar va_list.
    if (QualType(Ty, 0) == Context.getBuiltinVaListType() ||
        QualType(Ty, 0) == Context.getBuiltinMSVaListType())
      break;

    // Otherwise, do a single-step desugar.
    QualType Underlying;
    bool IsSugar = false;
    switch (Ty->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base) \
case Type::Class: { \
const Class##Type *CTy = cast<Class##Type>(Ty); \
if (CTy->isSugared()) { \
IsSugar = true; \
Underlying = CTy->desugar(); \
} \
break; \
}
#include "clang/AST/TypeNodes.inc"
    }

    // If it wasn't sugared, we're done.
    if (!IsSugar)
      break;

    // If the desugared type is a vector type, we don't want to expand
    // it, it will turn into an attribute mess. People want their "vec4".
    if (isa<VectorType>(Underlying))
      break;

    // Don't desugar through the primary typedef of an anonymous type.
    if (const TagType *UTT = Underlying->getAs<TagType>())
      if (const TypedefType *QTT = dyn_cast<TypedefType>(QT))
        if (UTT->getDecl()->getTypedefNameForAnonDecl() == QTT->getDecl())
          break;

    // Record that we actually looked through an opaque type here.
    ShouldAKA = true;
    QT = Underlying;
  }

  // If we have a pointer-like type, desugar the pointee as well.
  // FIXME: Handle other pointer-like types.
  if (const PointerType *Ty = QT->getAs<PointerType>()) {
    QT = Context.getPointerType(
        desugarForDiagnostic(Context, Ty->getPointeeType(), ShouldAKA));
  } else if (const auto *Ty = QT->getAs<ObjCObjectPointerType>()) {
    QT = Context.getObjCObjectPointerType(
        desugarForDiagnostic(Context, Ty->getPointeeType(), ShouldAKA));
  } else if (const LValueReferenceType *Ty = QT->getAs<LValueReferenceType>()) {
    QT = Context.getLValueReferenceType(
        desugarForDiagnostic(Context, Ty->getPointeeType(), ShouldAKA));
  } else if (const RValueReferenceType *Ty = QT->getAs<RValueReferenceType>()) {
    QT = Context.getRValueReferenceType(
        desugarForDiagnostic(Context, Ty->getPointeeType(), ShouldAKA));
  } else if (const auto *Ty = QT->getAs<ObjCObjectType>()) {
    if (Ty->getBaseType().getTypePtr() != Ty && !ShouldAKA) {
      QualType BaseType =
          desugarForDiagnostic(Context, Ty->getBaseType(), ShouldAKA);
      QT = Context.getObjCObjectType(
          BaseType, Ty->getTypeArgsAsWritten(),
          llvm::ArrayRef(Ty->qual_begin(), Ty->getNumProtocols()),
          Ty->isKindOfTypeAsWritten());
    }
  }

  return QC.apply(Context, QT);
}

/// Convert the given type to a string suitable for printing as part of
/// a diagnostic.
///
/// There are four main criteria when determining whether we should have an
/// a.k.a. clause when pretty-printing a type:
///
/// 1) Some types provide very minimal sugar that doesn't impede the
///    user's understanding --- for example, elaborated type
///    specifiers.  If this is all the sugar we see, we don't want an
///    a.k.a. clause.
/// 2) Some types are technically sugared but are much more familiar
///    when seen in their sugared form --- for example, va_list,
///    vector types, and the magic Objective C types.  We don't
///    want to desugar these, even if we do produce an a.k.a. clause.
/// 3) Some types may have already been desugared previously in this diagnostic.
///    if this is the case, doing another "aka" would just be clutter.
/// 4) Two different types within the same diagnostic have the same output
///    string.  In this case, force an a.k.a with the desugared type when
///    doing so will provide additional information.
///
/// \param Context the context in which the type was allocated
/// \param Ty the type to print
/// \param QualTypeVals pointer values to QualTypes which are used in the
/// diagnostic message
static std::string
ConvertTypeToDiagnosticString(ASTContext &Context, QualType Ty,
                            ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
                            ArrayRef<intptr_t> QualTypeVals) {
  // FIXME: Playing with std::string is really slow.
  bool ForceAKA = false;
  QualType CanTy = Ty.getCanonicalType();
  std::string S = Ty.getAsString(Context.getPrintingPolicy());
  std::string CanS = CanTy.getAsString(Context.getPrintingPolicy());

  for (const intptr_t &QualTypeVal : QualTypeVals) {
    QualType CompareTy =
        QualType::getFromOpaquePtr(reinterpret_cast<void *>(QualTypeVal));
    if (CompareTy.isNull())
      continue;
    if (CompareTy == Ty)
      continue;  // Same types
    QualType CompareCanTy = CompareTy.getCanonicalType();
    if (CompareCanTy == CanTy)
      continue;  // Same canonical types
    std::string CompareS = CompareTy.getAsString(Context.getPrintingPolicy());
    bool ShouldAKA = false;
    QualType CompareDesugar =
        desugarForDiagnostic(Context, CompareTy, ShouldAKA);
    std::string CompareDesugarStr =
        CompareDesugar.getAsString(Context.getPrintingPolicy());
    if (CompareS != S && CompareDesugarStr != S)
      continue;  // The type string is different than the comparison string
                 // and the desugared comparison string.
    std::string CompareCanS =
        CompareCanTy.getAsString(Context.getPrintingPolicy());

    if (CompareCanS == CanS)
      continue;  // No new info from canonical type

    ForceAKA = true;
    break;
  }

  // Check to see if we already desugared this type in this
  // diagnostic.  If so, don't do it again.
  bool Repeated = false;
  for (const auto &PrevArg : PrevArgs) {
    // TODO: Handle ak_declcontext case.
    if (PrevArg.first == DiagnosticsEngine::ak_qualtype) {
      QualType PrevTy(
          QualType::getFromOpaquePtr(reinterpret_cast<void *>(PrevArg.second)));
      if (PrevTy == Ty) {
        Repeated = true;
        break;
      }
    }
  }

  // Consider producing an a.k.a. clause if removing all the direct
  // sugar gives us something "significantly different".
  if (!Repeated) {
    bool ShouldAKA = false;
    QualType DesugaredTy = desugarForDiagnostic(Context, Ty, ShouldAKA);
    if (ShouldAKA || ForceAKA) {
      if (DesugaredTy == Ty) {
        DesugaredTy = Ty.getCanonicalType();
      }
      std::string akaStr = DesugaredTy.getAsString(Context.getPrintingPolicy());
      if (akaStr != S) {
        S = "'" + S + "' (aka '" + akaStr + "')";
        return S;
      }
    }

    // Give some additional info on vector types. These are either not desugared
    // or displaying complex __attribute__ expressions so add details of the
    // type and element count.
    if (const auto *VTy = Ty->getAs<VectorType>()) {
      std::string DecoratedString;
      llvm::raw_string_ostream OS(DecoratedString);
      const char *Values = VTy->getNumElements() > 1 ? "values" : "value";
      OS << "'" << S << "' (vector of " << VTy->getNumElements() << " '"
         << VTy->getElementType().getAsString(Context.getPrintingPolicy())
         << "' " << Values << ")";
      return DecoratedString;
    }
  }

  S = "'" + S + "'";
  return S;
}

static bool FormatTemplateTypeDiff(ASTContext &Context, QualType FromType,
                                   QualType ToType, bool PrintTree,
                                   bool PrintFromType, bool ElideType,
                                   bool ShowColors, raw_ostream &OS);

void clang::FormatASTNodeDiagnosticArgument(
    DiagnosticsEngine::ArgumentKind Kind,
    intptr_t Val,
    StringRef Modifier,
    StringRef Argument,
    ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
    SmallVectorImpl<char> &Output,
    void *Cookie,
    ArrayRef<intptr_t> QualTypeVals) {
  ASTContext &Context = *static_cast<ASTContext*>(Cookie);

  size_t OldEnd = Output.size();
  llvm::raw_svector_ostream OS(Output);
  bool NeedQuotes = true;

  switch (Kind) {
    default: llvm_unreachable("unknown ArgumentKind");
    case DiagnosticsEngine::ak_addrspace: {
      assert(Modifier.empty() && Argument.empty() &&
             "Invalid modifier for Qualifiers argument");

      auto S = Qualifiers::getAddrSpaceAsString(static_cast<LangAS>(Val));
      if (S.empty()) {
        OS << (Context.getLangOpts().OpenCL ? "default" : "generic");
        OS << " address space";
      } else {
        OS << "address space";
        OS << " '" << S << "'";
      }
      NeedQuotes = false;
      break;
    }
    case DiagnosticsEngine::ak_qual: {
      assert(Modifier.empty() && Argument.empty() &&
             "Invalid modifier for Qualifiers argument");

      Qualifiers Q(Qualifiers::fromOpaqueValue(Val));
      auto S = Q.getAsString();
      if (S.empty()) {
        OS << "unqualified";
        NeedQuotes = false;
      } else {
        OS << S;
      }
      break;
    }
    case DiagnosticsEngine::ak_qualtype_pair: {
      TemplateDiffTypes &TDT = *reinterpret_cast<TemplateDiffTypes*>(Val);
      QualType FromType =
          QualType::getFromOpaquePtr(reinterpret_cast<void*>(TDT.FromType));
      QualType ToType =
          QualType::getFromOpaquePtr(reinterpret_cast<void*>(TDT.ToType));

      if (FormatTemplateTypeDiff(Context, FromType, ToType, TDT.PrintTree,
                                 TDT.PrintFromType, TDT.ElideType,
                                 TDT.ShowColors, OS)) {
        NeedQuotes = !TDT.PrintTree;
        TDT.TemplateDiffUsed = true;
        break;
      }

      // Don't fall-back during tree printing.  The caller will handle
      // this case.
      if (TDT.PrintTree)
        return;

      // Attempting to do a template diff on non-templates.  Set the variables
      // and continue with regular type printing of the appropriate type.
      Val = TDT.PrintFromType ? TDT.FromType : TDT.ToType;
      Modifier = StringRef();
      Argument = StringRef();
      // Fall through
      [[fallthrough]];
    }
    case DiagnosticsEngine::ak_qualtype: {
      assert(Modifier.empty() && Argument.empty() &&
             "Invalid modifier for QualType argument");

      QualType Ty(QualType::getFromOpaquePtr(reinterpret_cast<void*>(Val)));
      OS << ConvertTypeToDiagnosticString(Context, Ty, PrevArgs, QualTypeVals);
      NeedQuotes = false;
      break;
    }
    case DiagnosticsEngine::ak_declarationname: {
      if (Modifier == "objcclass" && Argument.empty())
        OS << '+';
      else if (Modifier == "objcinstance" && Argument.empty())
        OS << '-';
      else
        assert(Modifier.empty() && Argument.empty() &&
               "Invalid modifier for DeclarationName argument");

      OS << DeclarationName::getFromOpaqueInteger(Val);
      break;
    }
    case DiagnosticsEngine::ak_nameddecl: {
      bool Qualified;
      if (Modifier == "q" && Argument.empty())
        Qualified = true;
      else {
        assert(Modifier.empty() && Argument.empty() &&
               "Invalid modifier for NamedDecl* argument");
        Qualified = false;
      }
      const NamedDecl *ND = reinterpret_cast<const NamedDecl*>(Val);
      ND->getNameForDiagnostic(OS, Context.getPrintingPolicy(), Qualified);
      break;
    }
    case DiagnosticsEngine::ak_nestednamespec: {
      NestedNameSpecifier *NNS = reinterpret_cast<NestedNameSpecifier*>(Val);
      NNS->print(OS, Context.getPrintingPolicy());
      NeedQuotes = false;
      break;
    }
    case DiagnosticsEngine::ak_declcontext: {
      DeclContext *DC = reinterpret_cast<DeclContext *> (Val);
      assert(DC && "Should never have a null declaration context");
      NeedQuotes = false;

      // FIXME: Get the strings for DeclContext from some localized place
      if (DC->isTranslationUnit()) {
        if (Context.getLangOpts().CPlusPlus)
          OS << "the global namespace";
        else
          OS << "the global scope";
      } else if (DC->isClosure()) {
        OS << "block literal";
      } else if (isLambdaCallOperator(DC)) {
        OS << "lambda expression";
      } else if (TypeDecl *Type = dyn_cast<TypeDecl>(DC)) {
        OS << ConvertTypeToDiagnosticString(Context,
                                            Context.getTypeDeclType(Type),
                                            PrevArgs, QualTypeVals);
      } else {
        assert(isa<NamedDecl>(DC) && "Expected a NamedDecl");
        NamedDecl *ND = cast<NamedDecl>(DC);
        if (isa<NamespaceDecl>(ND))
          OS << "namespace ";
        else if (isa<ObjCMethodDecl>(ND))
          OS << "method ";
        else if (isa<FunctionDecl>(ND))
          OS << "function ";

        OS << '\'';
        ND->getNameForDiagnostic(OS, Context.getPrintingPolicy(), true);
        OS << '\'';
      }
      break;
    }
    case DiagnosticsEngine::ak_attr: {
      const Attr *At = reinterpret_cast<Attr *>(Val);
      assert(At && "Received null Attr object!");
      OS << '\'' << At->getSpelling() << '\'';
      NeedQuotes = false;
      break;
    }
  }

  if (NeedQuotes) {
    Output.insert(Output.begin()+OldEnd, '\'');
    Output.push_back('\'');
  }
}

/// TemplateDiff - A class that constructs a pretty string for a pair of
/// QualTypes.  For the pair of types, a diff tree will be created containing
/// all the information about the templates and template arguments.  Afterwards,
/// the tree is transformed to a string according to the options passed in.
namespace {
class TemplateDiff {
  /// Context - The ASTContext which is used for comparing template arguments.
  ASTContext &Context;

  /// Policy - Used during expression printing.
  PrintingPolicy Policy;

  /// ElideType - Option to elide identical types.
  bool ElideType;

  /// PrintTree - Format output string as a tree.
  bool PrintTree;

  /// ShowColor - Diagnostics support color, so bolding will be used.
  bool ShowColor;

  /// FromTemplateType - When single type printing is selected, this is the
  /// type to be printed.  When tree printing is selected, this type will
  /// show up first in the tree.
  QualType FromTemplateType;

  /// ToTemplateType - The type that FromType is compared to.  Only in tree
  /// printing will this type be outputed.
  QualType ToTemplateType;

  /// OS - The stream used to construct the output strings.
  raw_ostream &OS;

  /// IsBold - Keeps track of the bold formatting for the output string.
  bool IsBold;

  /// DiffTree - A tree representation the differences between two types.
  class DiffTree {
  public:
    /// DiffKind - The difference in a DiffNode.  Fields of
    /// TemplateArgumentInfo needed by each difference can be found in the
    /// Set* and Get* functions.
    enum DiffKind {
      /// Incomplete or invalid node.
      Invalid,
      /// Another level of templates
      Template,
      /// Type difference, all type differences except those falling under
      /// the Template difference.
      Type,
      /// Expression difference, this is only when both arguments are
      /// expressions.  If one argument is an expression and the other is
      /// Integer or Declaration, then use that diff type instead.
      Expression,
      /// Template argument difference
      TemplateTemplate,
      /// Integer difference
      Integer,
      /// Declaration difference, nullptr arguments are included here
      Declaration,
      /// One argument being integer and the other being declaration
      FromIntegerAndToDeclaration,
      FromDeclarationAndToInteger
    };

  private:
    /// TemplateArgumentInfo - All the information needed to pretty print
    /// a template argument.  See the Set* and Get* functions to see which
    /// fields are used for each DiffKind.
    struct TemplateArgumentInfo {
      QualType ArgType;
      Qualifiers Qual;
      llvm::APSInt Val;
      bool IsValidInt = false;
      Expr *ArgExpr = nullptr;
      TemplateDecl *TD = nullptr;
      ValueDecl *VD = nullptr;
      bool NeedAddressOf = false;
      bool IsNullPtr = false;
      bool IsDefault = false;
    };

    /// DiffNode - The root node stores the original type.  Each child node
    /// stores template arguments of their parents.  For templated types, the
    /// template decl is also stored.
    struct DiffNode {
      DiffKind Kind = Invalid;

      /// NextNode - The index of the next sibling node or 0.
      unsigned NextNode = 0;

      /// ChildNode - The index of the first child node or 0.
      unsigned ChildNode = 0;

      /// ParentNode - The index of the parent node.
      unsigned ParentNode = 0;

      TemplateArgumentInfo FromArgInfo, ToArgInfo;

      /// Same - Whether the two arguments evaluate to the same value.
      bool Same = false;

      DiffNode(unsigned ParentNode = 0) : ParentNode(ParentNode) {}
    };

    /// FlatTree - A flattened tree used to store the DiffNodes.
    SmallVector<DiffNode, 16> FlatTree;

    /// CurrentNode - The index of the current node being used.
    unsigned CurrentNode;

    /// NextFreeNode - The index of the next unused node.  Used when creating
    /// child nodes.
    unsigned NextFreeNode;

    /// ReadNode - The index of the current node being read.
    unsigned ReadNode;

  public:
    DiffTree() : CurrentNode(0), NextFreeNode(1), ReadNode(0) {
      FlatTree.push_back(DiffNode());
    }

    // Node writing functions, one for each valid DiffKind element.
    void SetTemplateDiff(TemplateDecl *FromTD, TemplateDecl *ToTD,
                         Qualifiers FromQual, Qualifiers ToQual,
                         bool FromDefault, bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = Template;
      FlatTree[CurrentNode].FromArgInfo.TD = FromTD;
      FlatTree[CurrentNode].ToArgInfo.TD = ToTD;
      FlatTree[CurrentNode].FromArgInfo.Qual = FromQual;
      FlatTree[CurrentNode].ToArgInfo.Qual = ToQual;
      SetDefault(FromDefault, ToDefault);
    }

    void SetTypeDiff(QualType FromType, QualType ToType, bool FromDefault,
                     bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = Type;
      FlatTree[CurrentNode].FromArgInfo.ArgType = FromType;
      FlatTree[CurrentNode].ToArgInfo.ArgType = ToType;
      SetDefault(FromDefault, ToDefault);
    }

    void SetExpressionDiff(Expr *FromExpr, Expr *ToExpr, bool FromDefault,
                           bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = Expression;
      FlatTree[CurrentNode].FromArgInfo.ArgExpr = FromExpr;
      FlatTree[CurrentNode].ToArgInfo.ArgExpr = ToExpr;
      SetDefault(FromDefault, ToDefault);
    }

    void SetTemplateTemplateDiff(TemplateDecl *FromTD, TemplateDecl *ToTD,
                                 bool FromDefault, bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = TemplateTemplate;
      FlatTree[CurrentNode].FromArgInfo.TD = FromTD;
      FlatTree[CurrentNode].ToArgInfo.TD = ToTD;
      SetDefault(FromDefault, ToDefault);
    }

    void SetIntegerDiff(const llvm::APSInt &FromInt, const llvm::APSInt &ToInt,
                        bool IsValidFromInt, bool IsValidToInt,
                        QualType FromIntType, QualType ToIntType,
                        Expr *FromExpr, Expr *ToExpr, bool FromDefault,
                        bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = Integer;
      FlatTree[CurrentNode].FromArgInfo.Val = FromInt;
      FlatTree[CurrentNode].ToArgInfo.Val = ToInt;
      FlatTree[CurrentNode].FromArgInfo.IsValidInt = IsValidFromInt;
      FlatTree[CurrentNode].ToArgInfo.IsValidInt = IsValidToInt;
      FlatTree[CurrentNode].FromArgInfo.ArgType = FromIntType;
      FlatTree[CurrentNode].ToArgInfo.ArgType = ToIntType;
      FlatTree[CurrentNode].FromArgInfo.ArgExpr = FromExpr;
      FlatTree[CurrentNode].ToArgInfo.ArgExpr = ToExpr;
      SetDefault(FromDefault, ToDefault);
    }

    void SetDeclarationDiff(ValueDecl *FromValueDecl, ValueDecl *ToValueDecl,
                            bool FromAddressOf, bool ToAddressOf,
                            bool FromNullPtr, bool ToNullPtr, Expr *FromExpr,
                            Expr *ToExpr, bool FromDefault, bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = Declaration;
      FlatTree[CurrentNode].FromArgInfo.VD = FromValueDecl;
      FlatTree[CurrentNode].ToArgInfo.VD = ToValueDecl;
      FlatTree[CurrentNode].FromArgInfo.NeedAddressOf = FromAddressOf;
      FlatTree[CurrentNode].ToArgInfo.NeedAddressOf = ToAddressOf;
      FlatTree[CurrentNode].FromArgInfo.IsNullPtr = FromNullPtr;
      FlatTree[CurrentNode].ToArgInfo.IsNullPtr = ToNullPtr;
      FlatTree[CurrentNode].FromArgInfo.ArgExpr = FromExpr;
      FlatTree[CurrentNode].ToArgInfo.ArgExpr = ToExpr;
      SetDefault(FromDefault, ToDefault);
    }

    void SetFromDeclarationAndToIntegerDiff(
        ValueDecl *FromValueDecl, bool FromAddressOf, bool FromNullPtr,
        Expr *FromExpr, const llvm::APSInt &ToInt, bool IsValidToInt,
        QualType ToIntType, Expr *ToExpr, bool FromDefault, bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = FromDeclarationAndToInteger;
      FlatTree[CurrentNode].FromArgInfo.VD = FromValueDecl;
      FlatTree[CurrentNode].FromArgInfo.NeedAddressOf = FromAddressOf;
      FlatTree[CurrentNode].FromArgInfo.IsNullPtr = FromNullPtr;
      FlatTree[CurrentNode].FromArgInfo.ArgExpr = FromExpr;
      FlatTree[CurrentNode].ToArgInfo.Val = ToInt;
      FlatTree[CurrentNode].ToArgInfo.IsValidInt = IsValidToInt;
      FlatTree[CurrentNode].ToArgInfo.ArgType = ToIntType;
      FlatTree[CurrentNode].ToArgInfo.ArgExpr = ToExpr;
      SetDefault(FromDefault, ToDefault);
    }

    void SetFromIntegerAndToDeclarationDiff(
        const llvm::APSInt &FromInt, bool IsValidFromInt, QualType FromIntType,
        Expr *FromExpr, ValueDecl *ToValueDecl, bool ToAddressOf,
        bool ToNullPtr, Expr *ToExpr, bool FromDefault, bool ToDefault) {
      assert(FlatTree[CurrentNode].Kind == Invalid && "Node is not empty.");
      FlatTree[CurrentNode].Kind = FromIntegerAndToDeclaration;
      FlatTree[CurrentNode].FromArgInfo.Val = FromInt;
      FlatTree[CurrentNode].FromArgInfo.IsValidInt = IsValidFromInt;
      FlatTree[CurrentNode].FromArgInfo.ArgType = FromIntType;
      FlatTree[CurrentNode].FromArgInfo.ArgExpr = FromExpr;
      FlatTree[CurrentNode].ToArgInfo.VD = ToValueDecl;
      FlatTree[CurrentNode].ToArgInfo.NeedAddressOf = ToAddressOf;
      FlatTree[CurrentNode].ToArgInfo.IsNullPtr = ToNullPtr;
      FlatTree[CurrentNode].ToArgInfo.ArgExpr = ToExpr;
      SetDefault(FromDefault, ToDefault);
    }

    /// SetDefault - Sets FromDefault and ToDefault flags of the current node.
    void SetDefault(bool FromDefault, bool ToDefault) {
      assert((!FromDefault || !ToDefault) && "Both arguments cannot be default.");
      FlatTree[CurrentNode].FromArgInfo.IsDefault = FromDefault;
      FlatTree[CurrentNode].ToArgInfo.IsDefault = ToDefault;
    }

    /// SetSame - Sets the same flag of the current node.
    void SetSame(bool Same) {
      FlatTree[CurrentNode].Same = Same;
    }

    /// SetKind - Sets the current node's type.
    void SetKind(DiffKind Kind) {
      FlatTree[CurrentNode].Kind = Kind;
    }

    /// Up - Changes the node to the parent of the current node.
    void Up() {
      assert(FlatTree[CurrentNode].Kind != Invalid &&
             "Cannot exit node before setting node information.");
      CurrentNode = FlatTree[CurrentNode].ParentNode;
    }

    /// AddNode - Adds a child node to the current node, then sets that node
    /// node as the current node.
    void AddNode() {
      assert(FlatTree[CurrentNode].Kind == Template &&
             "Only Template nodes can have children nodes.");
      FlatTree.push_back(DiffNode(CurrentNode));
      DiffNode &Node = FlatTree[CurrentNode];
      if (Node.ChildNode == 0) {
        // If a child node doesn't exist, add one.
        Node.ChildNode = NextFreeNode;
      } else {
        // If a child node exists, find the last child node and add a
        // next node to it.
        unsigned i;
        for (i = Node.ChildNode; FlatTree[i].NextNode != 0;
             i = FlatTree[i].NextNode) {
        }
        FlatTree[i].NextNode = NextFreeNode;
      }
      CurrentNode = NextFreeNode;
      ++NextFreeNode;
    }

    // Node reading functions.
    /// StartTraverse - Prepares the tree for recursive traversal.
    void StartTraverse() {
      ReadNode = 0;
      CurrentNode = NextFreeNode;
      NextFreeNode = 0;
    }

    /// Parent - Move the current read node to its parent.
    void Parent() {
      ReadNode = FlatTree[ReadNode].ParentNode;
    }

    void GetTemplateDiff(TemplateDecl *&FromTD, TemplateDecl *&ToTD,
                         Qualifiers &FromQual, Qualifiers &ToQual) {
      assert(FlatTree[ReadNode].Kind == Template && "Unexpected kind.");
      FromTD = FlatTree[ReadNode].FromArgInfo.TD;
      ToTD = FlatTree[ReadNode].ToArgInfo.TD;
      FromQual = FlatTree[ReadNode].FromArgInfo.Qual;
      ToQual = FlatTree[ReadNode].ToArgInfo.Qual;
    }

    void GetTypeDiff(QualType &FromType, QualType &ToType) {
      assert(FlatTree[ReadNode].Kind == Type && "Unexpected kind");
      FromType = FlatTree[ReadNode].FromArgInfo.ArgType;
      ToType = FlatTree[ReadNode].ToArgInfo.ArgType;
    }

    void GetExpressionDiff(Expr *&FromExpr, Expr *&ToExpr) {
      assert(FlatTree[ReadNode].Kind == Expression && "Unexpected kind");
      FromExpr = FlatTree[ReadNode].FromArgInfo.ArgExpr;
      ToExpr = FlatTree[ReadNode].ToArgInfo.ArgExpr;
    }

    void GetTemplateTemplateDiff(TemplateDecl *&FromTD, TemplateDecl *&ToTD) {
      assert(FlatTree[ReadNode].Kind == TemplateTemplate && "Unexpected kind.");
      FromTD = FlatTree[ReadNode].FromArgInfo.TD;
      ToTD = FlatTree[ReadNode].ToArgInfo.TD;
    }

    void GetIntegerDiff(llvm::APSInt &FromInt, llvm::APSInt &ToInt,
                        bool &IsValidFromInt, bool &IsValidToInt,
                        QualType &FromIntType, QualType &ToIntType,
                        Expr *&FromExpr, Expr *&ToExpr) {
      assert(FlatTree[ReadNode].Kind == Integer && "Unexpected kind.");
      FromInt = FlatTree[ReadNode].FromArgInfo.Val;
      ToInt = FlatTree[ReadNode].ToArgInfo.Val;
      IsValidFromInt = FlatTree[ReadNode].FromArgInfo.IsValidInt;
      IsValidToInt = FlatTree[ReadNode].ToArgInfo.IsValidInt;
      FromIntType = FlatTree[ReadNode].FromArgInfo.ArgType;
      ToIntType = FlatTree[ReadNode].ToArgInfo.ArgType;
      FromExpr = FlatTree[ReadNode].FromArgInfo.ArgExpr;
      ToExpr = FlatTree[ReadNode].ToArgInfo.ArgExpr;
    }

    void GetDeclarationDiff(ValueDecl *&FromValueDecl, ValueDecl *&ToValueDecl,
                            bool &FromAddressOf, bool &ToAddressOf,
                            bool &FromNullPtr, bool &ToNullPtr, Expr *&FromExpr,
                            Expr *&ToExpr) {
      assert(FlatTree[ReadNode].Kind == Declaration && "Unexpected kind.");
      FromValueDecl = FlatTree[ReadNode].FromArgInfo.VD;
      ToValueDecl = FlatTree[ReadNode].ToArgInfo.VD;
      FromAddressOf = FlatTree[ReadNode].FromArgInfo.NeedAddressOf;
      ToAddressOf = FlatTree[ReadNode].ToArgInfo.NeedAddressOf;
      FromNullPtr = FlatTree[ReadNode].FromArgInfo.IsNullPtr;
      ToNullPtr = FlatTree[ReadNode].ToArgInfo.IsNullPtr;
      FromExpr = FlatTree[ReadNode].FromArgInfo.ArgExpr;
      ToExpr = FlatTree[ReadNode].ToArgInfo.ArgExpr;
    }

    void GetFromDeclarationAndToIntegerDiff(
        ValueDecl *&FromValueDecl, bool &FromAddressOf, bool &FromNullPtr,
        Expr *&FromExpr, llvm::APSInt &ToInt, bool &IsValidToInt,
        QualType &ToIntType, Expr *&ToExpr) {
      assert(FlatTree[ReadNode].Kind == FromDeclarationAndToInteger &&
             "Unexpected kind.");
      FromValueDecl = FlatTree[ReadNode].FromArgInfo.VD;
      FromAddressOf = FlatTree[ReadNode].FromArgInfo.NeedAddressOf;
      FromNullPtr = FlatTree[ReadNode].FromArgInfo.IsNullPtr;
      FromExpr = FlatTree[ReadNode].FromArgInfo.ArgExpr;
      ToInt = FlatTree[ReadNode].ToArgInfo.Val;
      IsValidToInt = FlatTree[ReadNode].ToArgInfo.IsValidInt;
      ToIntType = FlatTree[ReadNode].ToArgInfo.ArgType;
      ToExpr = FlatTree[ReadNode].ToArgInfo.ArgExpr;
    }

    void GetFromIntegerAndToDeclarationDiff(
        llvm::APSInt &FromInt, bool &IsValidFromInt, QualType &FromIntType,
        Expr *&FromExpr, ValueDecl *&ToValueDecl, bool &ToAddressOf,
        bool &ToNullPtr, Expr *&ToExpr) {
      assert(FlatTree[ReadNode].Kind == FromIntegerAndToDeclaration &&
             "Unexpected kind.");
      FromInt = FlatTree[ReadNode].FromArgInfo.Val;
      IsValidFromInt = FlatTree[ReadNode].FromArgInfo.IsValidInt;
      FromIntType = FlatTree[ReadNode].FromArgInfo.ArgType;
      FromExpr = FlatTree[ReadNode].FromArgInfo.ArgExpr;
      ToValueDecl = FlatTree[ReadNode].ToArgInfo.VD;
      ToAddressOf = FlatTree[ReadNode].ToArgInfo.NeedAddressOf;
      ToNullPtr = FlatTree[ReadNode].ToArgInfo.IsNullPtr;
      ToExpr = FlatTree[ReadNode].ToArgInfo.ArgExpr;
    }

    /// FromDefault - Return true if the from argument is the default.
    bool FromDefault() {
      return FlatTree[ReadNode].FromArgInfo.IsDefault;
    }

    /// ToDefault - Return true if the to argument is the default.
    bool ToDefault() {
      return FlatTree[ReadNode].ToArgInfo.IsDefault;
    }

    /// NodeIsSame - Returns true the arguments are the same.
    bool NodeIsSame() {
      return FlatTree[ReadNode].Same;
    }

    /// HasChildrend - Returns true if the node has children.
    bool HasChildren() {
      return FlatTree[ReadNode].ChildNode != 0;
    }

    /// MoveToChild - Moves from the current node to its child.
    void MoveToChild() {
      ReadNode = FlatTree[ReadNode].ChildNode;
    }

    /// AdvanceSibling - If there is a next sibling, advance to it and return
    /// true.  Otherwise, return false.
    bool AdvanceSibling() {
      if (FlatTree[ReadNode].NextNode == 0)
        return false;

      ReadNode = FlatTree[ReadNode].NextNode;
      return true;
    }

    /// HasNextSibling - Return true if the node has a next sibling.
    bool HasNextSibling() {
      return FlatTree[ReadNode].NextNode != 0;
    }

    /// Empty - Returns true if the tree has no information.
    bool Empty() {
      return GetKind() == Invalid;
    }

    /// GetKind - Returns the current node's type.
    DiffKind GetKind() {
      return FlatTree[ReadNode].Kind;
    }
  };

  DiffTree Tree;

  /// TSTiterator - a pair of iterators that walks the
  /// TemplateSpecializationType and the desugared TemplateSpecializationType.
  /// The deseguared TemplateArgument should provide the canonical argument
  /// for comparisons.
  class TSTiterator {
    typedef const TemplateArgument& reference;
    typedef const TemplateArgument* pointer;

    /// InternalIterator - an iterator that is used to enter a
    /// TemplateSpecializationType and read TemplateArguments inside template
    /// parameter packs in order with the rest of the TemplateArguments.
    struct InternalIterator {
      /// TST - the template specialization whose arguments this iterator
      /// traverse over.
      const TemplateSpecializationType *TST;

      /// Index - the index of the template argument in TST.
      unsigned Index;

      /// CurrentTA - if CurrentTA is not the same as EndTA, then CurrentTA
      /// points to a TemplateArgument within a parameter pack.
      TemplateArgument::pack_iterator CurrentTA;

      /// EndTA - the end iterator of a parameter pack
      TemplateArgument::pack_iterator EndTA;

      /// InternalIterator - Constructs an iterator and sets it to the first
      /// template argument.
      InternalIterator(const TemplateSpecializationType *TST)
          : TST(TST), Index(0), CurrentTA(nullptr), EndTA(nullptr) {
        if (!TST) return;

        if (isEnd()) return;

        // Set to first template argument.  If not a parameter pack, done.
        TemplateArgument TA = TST->template_arguments()[0];
        if (TA.getKind() != TemplateArgument::Pack) return;

        // Start looking into the parameter pack.
        CurrentTA = TA.pack_begin();
        EndTA = TA.pack_end();

        // Found a valid template argument.
        if (CurrentTA != EndTA) return;

        // Parameter pack is empty, use the increment to get to a valid
        // template argument.
        ++(*this);
      }

      /// Return true if the iterator is non-singular.
      bool isValid() const { return TST; }

      /// isEnd - Returns true if the iterator is one past the end.
      bool isEnd() const {
        assert(TST && "InternalIterator is invalid with a null TST.");
        return Index >= TST->template_arguments().size();
      }

      /// &operator++ - Increment the iterator to the next template argument.
      InternalIterator &operator++() {
        assert(TST && "InternalIterator is invalid with a null TST.");
        if (isEnd()) {
          return *this;
        }

        // If in a parameter pack, advance in the parameter pack.
        if (CurrentTA != EndTA) {
          ++CurrentTA;
          if (CurrentTA != EndTA)
            return *this;
        }

        // Loop until a template argument is found, or the end is reached.
        while (true) {
          // Advance to the next template argument.  Break if reached the end.
          if (++Index == TST->template_arguments().size())
            break;

          // If the TemplateArgument is not a parameter pack, done.
          TemplateArgument TA = TST->template_arguments()[Index];
          if (TA.getKind() != TemplateArgument::Pack)
            break;

          // Handle parameter packs.
          CurrentTA = TA.pack_begin();
          EndTA = TA.pack_end();

          // If the parameter pack is empty, try to advance again.
          if (CurrentTA != EndTA)
            break;
        }
        return *this;
      }

      /// operator* - Returns the appropriate TemplateArgument.
      reference operator*() const {
        assert(TST && "InternalIterator is invalid with a null TST.");
        assert(!isEnd() && "Index exceeds number of arguments.");
        if (CurrentTA == EndTA)
          return TST->template_arguments()[Index];
        else
          return *CurrentTA;
      }

      /// operator-> - Allow access to the underlying TemplateArgument.
      pointer operator->() const {
        assert(TST && "InternalIterator is invalid with a null TST.");
        return &operator*();
      }
    };

    InternalIterator SugaredIterator;
    InternalIterator DesugaredIterator;

  public:
    TSTiterator(ASTContext &Context, const TemplateSpecializationType *TST)
        : SugaredIterator(TST),
          DesugaredIterator(
              (TST->isSugared() && !TST->isTypeAlias())
                  ? GetTemplateSpecializationType(Context, TST->desugar())
                  : nullptr) {}

    /// &operator++ - Increment the iterator to the next template argument.
    TSTiterator &operator++() {
      ++SugaredIterator;
      if (DesugaredIterator.isValid())
        ++DesugaredIterator;
      return *this;
    }

    /// operator* - Returns the appropriate TemplateArgument.
    reference operator*() const {
      return *SugaredIterator;
    }

    /// operator-> - Allow access to the underlying TemplateArgument.
    pointer operator->() const {
      return &operator*();
    }

    /// isEnd - Returns true if no more TemplateArguments are available.
    bool isEnd() const {
      return SugaredIterator.isEnd();
    }

    /// hasDesugaredTA - Returns true if there is another TemplateArgument
    /// available.
    bool hasDesugaredTA() const {
      return DesugaredIterator.isValid() && !DesugaredIterator.isEnd();
    }

    /// getDesugaredTA - Returns the desugared TemplateArgument.
    reference getDesugaredTA() const {
      assert(DesugaredIterator.isValid() &&
             "Desugared TemplateArgument should not be used.");
      return *DesugaredIterator;
    }
  };

  // These functions build up the template diff tree, including functions to
  // retrieve and compare template arguments.

  static const TemplateSpecializationType *GetTemplateSpecializationType(
      ASTContext &Context, QualType Ty) {
    if (const TemplateSpecializationType *TST =
            Ty->getAs<TemplateSpecializationType>())
      return TST;

    if (const auto* SubstType = Ty->getAs<SubstTemplateTypeParmType>())
      Ty = SubstType->getReplacementType();

    const RecordType *RT = Ty->getAs<RecordType>();

    if (!RT)
      return nullptr;

    const ClassTemplateSpecializationDecl *CTSD =
        dyn_cast<ClassTemplateSpecializationDecl>(RT->getDecl());

    if (!CTSD)
      return nullptr;

    Ty = Context.getTemplateSpecializationType(
             TemplateName(CTSD->getSpecializedTemplate()),
             CTSD->getTemplateArgs().asArray(),
             Ty.getLocalUnqualifiedType().getCanonicalType());

    return Ty->getAs<TemplateSpecializationType>();
  }

  /// Returns true if the DiffType is Type and false for Template.
  static bool OnlyPerformTypeDiff(ASTContext &Context, QualType FromType,
                                  QualType ToType,
                                  const TemplateSpecializationType *&FromArgTST,
                                  const TemplateSpecializationType *&ToArgTST) {
    if (FromType.isNull() || ToType.isNull())
      return true;

    if (Context.hasSameType(FromType, ToType))
      return true;

    FromArgTST = GetTemplateSpecializationType(Context, FromType);
    ToArgTST = GetTemplateSpecializationType(Context, ToType);

    if (!FromArgTST || !ToArgTST)
      return true;

    if (!hasSameTemplate(FromArgTST, ToArgTST))
      return true;

    return false;
  }

  /// DiffTypes - Fills a DiffNode with information about a type difference.
  void DiffTypes(const TSTiterator &FromIter, const TSTiterator &ToIter) {
    QualType FromType = GetType(FromIter);
    QualType ToType = GetType(ToIter);

    bool FromDefault = FromIter.isEnd() && !FromType.isNull();
    bool ToDefault = ToIter.isEnd() && !ToType.isNull();

    const TemplateSpecializationType *FromArgTST = nullptr;
    const TemplateSpecializationType *ToArgTST = nullptr;
    if (OnlyPerformTypeDiff(Context, FromType, ToType, FromArgTST, ToArgTST)) {
      Tree.SetTypeDiff(FromType, ToType, FromDefault, ToDefault);
      Tree.SetSame(!FromType.isNull() && !ToType.isNull() &&
                   Context.hasSameType(FromType, ToType));
    } else {
      assert(FromArgTST && ToArgTST &&
             "Both template specializations need to be valid.");
      Qualifiers FromQual = FromType.getQualifiers(),
                 ToQual = ToType.getQualifiers();
      FromQual -= QualType(FromArgTST, 0).getQualifiers();
      ToQual -= QualType(ToArgTST, 0).getQualifiers();
      Tree.SetTemplateDiff(FromArgTST->getTemplateName().getAsTemplateDecl(),
                           ToArgTST->getTemplateName().getAsTemplateDecl(),
                           FromQual, ToQual, FromDefault, ToDefault);
      DiffTemplate(FromArgTST, ToArgTST);
    }
  }

  /// DiffTemplateTemplates - Fills a DiffNode with information about a
  /// template template difference.
  void DiffTemplateTemplates(const TSTiterator &FromIter,
                             const TSTiterator &ToIter) {
    TemplateDecl *FromDecl = GetTemplateDecl(FromIter);
    TemplateDecl *ToDecl = GetTemplateDecl(ToIter);
    Tree.SetTemplateTemplateDiff(FromDecl, ToDecl, FromIter.isEnd() && FromDecl,
                                 ToIter.isEnd() && ToDecl);
    Tree.SetSame(FromDecl && ToDecl &&
                 FromDecl->getCanonicalDecl() == ToDecl->getCanonicalDecl());
  }

  /// InitializeNonTypeDiffVariables - Helper function for DiffNonTypes
  static void InitializeNonTypeDiffVariables(ASTContext &Context,
                                             const TSTiterator &Iter,
                                             NonTypeTemplateParmDecl *Default,
                                             llvm::APSInt &Value, bool &HasInt,
                                             QualType &IntType, bool &IsNullPtr,
                                             Expr *&E, ValueDecl *&VD,
                                             bool &NeedAddressOf) {
    if (!Iter.isEnd()) {
      switch (Iter->getKind()) {
      case TemplateArgument::StructuralValue:
        // FIXME: Diffing of structural values is not implemented.
        // There is no possible fallback in this case, this will show up
        // as '(no argument)'.
        return;
      case TemplateArgument::Integral:
        Value = Iter->getAsIntegral();
        HasInt = true;
        IntType = Iter->getIntegralType();
        return;
      case TemplateArgument::Declaration: {
        VD = Iter->getAsDecl();
        QualType ArgType = Iter->getParamTypeForDecl();
        QualType VDType = VD->getType();
        if (ArgType->isPointerType() &&
            Context.hasSameType(ArgType->getPointeeType(), VDType))
          NeedAddressOf = true;
        return;
      }
      case TemplateArgument::NullPtr:
        IsNullPtr = true;
        return;
      case TemplateArgument::Expression:
        E = Iter->getAsExpr();
        break;
      case TemplateArgument::Null:
      case TemplateArgument::Type:
      case TemplateArgument::Template:
      case TemplateArgument::TemplateExpansion:
        llvm_unreachable("TemplateArgument kind is not expected for NTTP");
      case TemplateArgument::Pack:
        llvm_unreachable("TemplateArgument kind should be handled elsewhere");
      }
    } else if (!Default->isParameterPack()) {
      E = Default->getDefaultArgument().getArgument().getAsExpr();
    }

    if (!Iter.hasDesugaredTA())
      return;

    const TemplateArgument &TA = Iter.getDesugaredTA();
    switch (TA.getKind()) {
    case TemplateArgument::StructuralValue:
      // FIXME: Diffing of structural values is not implemented.
      //        Just fall back to the expression.
      return;
    case TemplateArgument::Integral:
      Value = TA.getAsIntegral();
      HasInt = true;
      IntType = TA.getIntegralType();
      return;
    case TemplateArgument::Declaration: {
      VD = TA.getAsDecl();
      QualType ArgType = TA.getParamTypeForDecl();
      QualType VDType = VD->getType();
      if (ArgType->isPointerType() &&
          Context.hasSameType(ArgType->getPointeeType(), VDType))
        NeedAddressOf = true;
      return;
    }
    case TemplateArgument::NullPtr:
      IsNullPtr = true;
      return;
    case TemplateArgument::Expression:
      // TODO: Sometimes, the desugared template argument Expr differs from
      // the sugared template argument Expr.  It may be useful in the future
      // but for now, it is just discarded.
      if (!E)
        E = TA.getAsExpr();
      return;
    case TemplateArgument::Null:
    case TemplateArgument::Type:
    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      llvm_unreachable("TemplateArgument kind is not expected for NTTP");
    case TemplateArgument::Pack:
      llvm_unreachable("TemplateArgument kind should be handled elsewhere");
    }
    llvm_unreachable("Unexpected TemplateArgument kind");
  }

  /// DiffNonTypes - Handles any template parameters not handled by DiffTypes
  /// of DiffTemplatesTemplates, such as integer and declaration parameters.
  void DiffNonTypes(const TSTiterator &FromIter, const TSTiterator &ToIter,
                    NonTypeTemplateParmDecl *FromDefaultNonTypeDecl,
                    NonTypeTemplateParmDecl *ToDefaultNonTypeDecl) {
    Expr *FromExpr = nullptr, *ToExpr = nullptr;
    llvm::APSInt FromInt, ToInt;
    QualType FromIntType, ToIntType;
    ValueDecl *FromValueDecl = nullptr, *ToValueDecl = nullptr;
    bool HasFromInt = false, HasToInt = false, FromNullPtr = false,
         ToNullPtr = false, NeedFromAddressOf = false, NeedToAddressOf = false;
    InitializeNonTypeDiffVariables(
        Context, FromIter, FromDefaultNonTypeDecl, FromInt, HasFromInt,
        FromIntType, FromNullPtr, FromExpr, FromValueDecl, NeedFromAddressOf);
    InitializeNonTypeDiffVariables(Context, ToIter, ToDefaultNonTypeDecl, ToInt,
                                   HasToInt, ToIntType, ToNullPtr, ToExpr,
                                   ToValueDecl, NeedToAddressOf);

    bool FromDefault = FromIter.isEnd() &&
                       (FromExpr || FromValueDecl || HasFromInt || FromNullPtr);
    bool ToDefault = ToIter.isEnd() &&
                     (ToExpr || ToValueDecl || HasToInt || ToNullPtr);

    bool FromDeclaration = FromValueDecl || FromNullPtr;
    bool ToDeclaration = ToValueDecl || ToNullPtr;

    if (FromDeclaration && HasToInt) {
      Tree.SetFromDeclarationAndToIntegerDiff(
          FromValueDecl, NeedFromAddressOf, FromNullPtr, FromExpr, ToInt,
          HasToInt, ToIntType, ToExpr, FromDefault, ToDefault);
      Tree.SetSame(false);
      return;

    }

    if (HasFromInt && ToDeclaration) {
      Tree.SetFromIntegerAndToDeclarationDiff(
          FromInt, HasFromInt, FromIntType, FromExpr, ToValueDecl,
          NeedToAddressOf, ToNullPtr, ToExpr, FromDefault, ToDefault);
      Tree.SetSame(false);
      return;
    }

    if (HasFromInt || HasToInt) {
      Tree.SetIntegerDiff(FromInt, ToInt, HasFromInt, HasToInt, FromIntType,
                          ToIntType, FromExpr, ToExpr, FromDefault, ToDefault);
      if (HasFromInt && HasToInt) {
        Tree.SetSame(Context.hasSameType(FromIntType, ToIntType) &&
                     FromInt == ToInt);
      }
      return;
    }

    if (FromDeclaration || ToDeclaration) {
      Tree.SetDeclarationDiff(FromValueDecl, ToValueDecl, NeedFromAddressOf,
                              NeedToAddressOf, FromNullPtr, ToNullPtr, FromExpr,
                              ToExpr, FromDefault, ToDefault);
      bool BothNull = FromNullPtr && ToNullPtr;
      bool SameValueDecl =
          FromValueDecl && ToValueDecl &&
          NeedFromAddressOf == NeedToAddressOf &&
          FromValueDecl->getCanonicalDecl() == ToValueDecl->getCanonicalDecl();
      Tree.SetSame(BothNull || SameValueDecl);
      return;
    }

    assert((FromExpr || ToExpr) && "Both template arguments cannot be empty.");
    Tree.SetExpressionDiff(FromExpr, ToExpr, FromDefault, ToDefault);
    Tree.SetSame(IsEqualExpr(Context, FromExpr, ToExpr));
  }

  /// DiffTemplate - recursively visits template arguments and stores the
  /// argument info into a tree.
  void DiffTemplate(const TemplateSpecializationType *FromTST,
                    const TemplateSpecializationType *ToTST) {
    // Begin descent into diffing template tree.
    TemplateParameterList *ParamsFrom =
        FromTST->getTemplateName().getAsTemplateDecl()->getTemplateParameters();
    TemplateParameterList *ParamsTo =
        ToTST->getTemplateName().getAsTemplateDecl()->getTemplateParameters();
    unsigned TotalArgs = 0;
    for (TSTiterator FromIter(Context, FromTST), ToIter(Context, ToTST);
         !FromIter.isEnd() || !ToIter.isEnd(); ++TotalArgs) {
      Tree.AddNode();

      // Get the parameter at index TotalArgs.  If index is larger
      // than the total number of parameters, then there is an
      // argument pack, so re-use the last parameter.
      unsigned FromParamIndex = std::min(TotalArgs, ParamsFrom->size() - 1);
      unsigned ToParamIndex = std::min(TotalArgs, ParamsTo->size() - 1);
      NamedDecl *FromParamND = ParamsFrom->getParam(FromParamIndex);
      NamedDecl *ToParamND = ParamsTo->getParam(ToParamIndex);

      assert(FromParamND->getKind() == ToParamND->getKind() &&
             "Parameter Decl are not the same kind.");

      if (isa<TemplateTypeParmDecl>(FromParamND)) {
        DiffTypes(FromIter, ToIter);
      } else if (isa<TemplateTemplateParmDecl>(FromParamND)) {
        DiffTemplateTemplates(FromIter, ToIter);
      } else if (isa<NonTypeTemplateParmDecl>(FromParamND)) {
        NonTypeTemplateParmDecl *FromDefaultNonTypeDecl =
            cast<NonTypeTemplateParmDecl>(FromParamND);
        NonTypeTemplateParmDecl *ToDefaultNonTypeDecl =
            cast<NonTypeTemplateParmDecl>(ToParamND);
        DiffNonTypes(FromIter, ToIter, FromDefaultNonTypeDecl,
                     ToDefaultNonTypeDecl);
      } else {
        llvm_unreachable("Unexpected Decl type.");
      }

      ++FromIter;
      ++ToIter;
      Tree.Up();
    }
  }

  /// makeTemplateList - Dump every template alias into the vector.
  static void makeTemplateList(
      SmallVectorImpl<const TemplateSpecializationType *> &TemplateList,
      const TemplateSpecializationType *TST) {
    while (TST) {
      TemplateList.push_back(TST);
      if (!TST->isTypeAlias())
        return;
      TST = TST->getAliasedType()->getAs<TemplateSpecializationType>();
    }
  }

  /// hasSameBaseTemplate - Returns true when the base templates are the same,
  /// even if the template arguments are not.
  static bool hasSameBaseTemplate(const TemplateSpecializationType *FromTST,
                                  const TemplateSpecializationType *ToTST) {
    return FromTST->getTemplateName().getAsTemplateDecl()->getCanonicalDecl() ==
           ToTST->getTemplateName().getAsTemplateDecl()->getCanonicalDecl();
  }

  /// hasSameTemplate - Returns true if both types are specialized from the
  /// same template declaration.  If they come from different template aliases,
  /// do a parallel ascension search to determine the highest template alias in
  /// common and set the arguments to them.
  static bool hasSameTemplate(const TemplateSpecializationType *&FromTST,
                              const TemplateSpecializationType *&ToTST) {
    // Check the top templates if they are the same.
    if (hasSameBaseTemplate(FromTST, ToTST))
      return true;

    // Create vectors of template aliases.
    SmallVector<const TemplateSpecializationType*, 1> FromTemplateList,
                                                      ToTemplateList;

    makeTemplateList(FromTemplateList, FromTST);
    makeTemplateList(ToTemplateList, ToTST);

    SmallVectorImpl<const TemplateSpecializationType *>::reverse_iterator
        FromIter = FromTemplateList.rbegin(), FromEnd = FromTemplateList.rend(),
        ToIter = ToTemplateList.rbegin(), ToEnd = ToTemplateList.rend();

    // Check if the lowest template types are the same.  If not, return.
    if (!hasSameBaseTemplate(*FromIter, *ToIter))
      return false;

    // Begin searching up the template aliases.  The bottom most template
    // matches so move up until one pair does not match.  Use the template
    // right before that one.
    for (; FromIter != FromEnd && ToIter != ToEnd; ++FromIter, ++ToIter) {
      if (!hasSameBaseTemplate(*FromIter, *ToIter))
        break;
    }

    FromTST = FromIter[-1];
    ToTST = ToIter[-1];

    return true;
  }

  /// GetType - Retrieves the template type arguments, including default
  /// arguments.
  static QualType GetType(const TSTiterator &Iter) {
    if (!Iter.isEnd())
      return Iter->getAsType();
    if (Iter.hasDesugaredTA())
      return Iter.getDesugaredTA().getAsType();
    return QualType();
  }

  /// GetTemplateDecl - Retrieves the template template arguments, including
  /// default arguments.
  static TemplateDecl *GetTemplateDecl(const TSTiterator &Iter) {
    if (!Iter.isEnd())
      return Iter->getAsTemplate().getAsTemplateDecl();
    if (Iter.hasDesugaredTA())
      return Iter.getDesugaredTA().getAsTemplate().getAsTemplateDecl();
    return nullptr;
  }

  /// IsEqualExpr - Returns true if the expressions are the same in regards to
  /// template arguments.  These expressions are dependent, so profile them
  /// instead of trying to evaluate them.
  static bool IsEqualExpr(ASTContext &Context, Expr *FromExpr, Expr *ToExpr) {
    if (FromExpr == ToExpr)
      return true;

    if (!FromExpr || !ToExpr)
      return false;

    llvm::FoldingSetNodeID FromID, ToID;
    FromExpr->Profile(FromID, Context, true);
    ToExpr->Profile(ToID, Context, true);
    return FromID == ToID;
  }

  // These functions converts the tree representation of the template
  // differences into the internal character vector.

  /// TreeToString - Converts the Tree object into a character stream which
  /// will later be turned into the output string.
  void TreeToString(int Indent = 1) {
    if (PrintTree) {
      OS << '\n';
      OS.indent(2 * Indent);
      ++Indent;
    }

    // Handle cases where the difference is not templates with different
    // arguments.
    switch (Tree.GetKind()) {
      case DiffTree::Invalid:
        llvm_unreachable("Template diffing failed with bad DiffNode");
      case DiffTree::Type: {
        QualType FromType, ToType;
        Tree.GetTypeDiff(FromType, ToType);
        PrintTypeNames(FromType, ToType, Tree.FromDefault(), Tree.ToDefault(),
                       Tree.NodeIsSame());
        return;
      }
      case DiffTree::Expression: {
        Expr *FromExpr, *ToExpr;
        Tree.GetExpressionDiff(FromExpr, ToExpr);
        PrintExpr(FromExpr, ToExpr, Tree.FromDefault(), Tree.ToDefault(),
                  Tree.NodeIsSame());
        return;
      }
      case DiffTree::TemplateTemplate: {
        TemplateDecl *FromTD, *ToTD;
        Tree.GetTemplateTemplateDiff(FromTD, ToTD);
        PrintTemplateTemplate(FromTD, ToTD, Tree.FromDefault(),
                              Tree.ToDefault(), Tree.NodeIsSame());
        return;
      }
      case DiffTree::Integer: {
        llvm::APSInt FromInt, ToInt;
        Expr *FromExpr, *ToExpr;
        bool IsValidFromInt, IsValidToInt;
        QualType FromIntType, ToIntType;
        Tree.GetIntegerDiff(FromInt, ToInt, IsValidFromInt, IsValidToInt,
                            FromIntType, ToIntType, FromExpr, ToExpr);
        PrintAPSInt(FromInt, ToInt, IsValidFromInt, IsValidToInt, FromIntType,
                    ToIntType, FromExpr, ToExpr, Tree.FromDefault(),
                    Tree.ToDefault(), Tree.NodeIsSame());
        return;
      }
      case DiffTree::Declaration: {
        ValueDecl *FromValueDecl, *ToValueDecl;
        bool FromAddressOf, ToAddressOf;
        bool FromNullPtr, ToNullPtr;
        Expr *FromExpr, *ToExpr;
        Tree.GetDeclarationDiff(FromValueDecl, ToValueDecl, FromAddressOf,
                                ToAddressOf, FromNullPtr, ToNullPtr, FromExpr,
                                ToExpr);
        PrintValueDecl(FromValueDecl, ToValueDecl, FromAddressOf, ToAddressOf,
                       FromNullPtr, ToNullPtr, FromExpr, ToExpr,
                       Tree.FromDefault(), Tree.ToDefault(), Tree.NodeIsSame());
        return;
      }
      case DiffTree::FromDeclarationAndToInteger: {
        ValueDecl *FromValueDecl;
        bool FromAddressOf;
        bool FromNullPtr;
        Expr *FromExpr;
        llvm::APSInt ToInt;
        bool IsValidToInt;
        QualType ToIntType;
        Expr *ToExpr;
        Tree.GetFromDeclarationAndToIntegerDiff(
            FromValueDecl, FromAddressOf, FromNullPtr, FromExpr, ToInt,
            IsValidToInt, ToIntType, ToExpr);
        assert((FromValueDecl || FromNullPtr) && IsValidToInt);
        PrintValueDeclAndInteger(FromValueDecl, FromAddressOf, FromNullPtr,
                                 FromExpr, Tree.FromDefault(), ToInt, ToIntType,
                                 ToExpr, Tree.ToDefault());
        return;
      }
      case DiffTree::FromIntegerAndToDeclaration: {
        llvm::APSInt FromInt;
        bool IsValidFromInt;
        QualType FromIntType;
        Expr *FromExpr;
        ValueDecl *ToValueDecl;
        bool ToAddressOf;
        bool ToNullPtr;
        Expr *ToExpr;
        Tree.GetFromIntegerAndToDeclarationDiff(
            FromInt, IsValidFromInt, FromIntType, FromExpr, ToValueDecl,
            ToAddressOf, ToNullPtr, ToExpr);
        assert(IsValidFromInt && (ToValueDecl || ToNullPtr));
        PrintIntegerAndValueDecl(FromInt, FromIntType, FromExpr,
                                 Tree.FromDefault(), ToValueDecl, ToAddressOf,
                                 ToNullPtr, ToExpr, Tree.ToDefault());
        return;
      }
      case DiffTree::Template: {
        // Node is root of template.  Recurse on children.
        TemplateDecl *FromTD, *ToTD;
        Qualifiers FromQual, ToQual;
        Tree.GetTemplateDiff(FromTD, ToTD, FromQual, ToQual);

        PrintQualifiers(FromQual, ToQual);

        if (!Tree.HasChildren()) {
          // If we're dealing with a template specialization with zero
          // arguments, there are no children; special-case this.
          OS << FromTD->getDeclName() << "<>";
          return;
        }

        OS << FromTD->getDeclName() << '<';
        Tree.MoveToChild();
        unsigned NumElideArgs = 0;
        bool AllArgsElided = true;
        do {
          if (ElideType) {
            if (Tree.NodeIsSame()) {
              ++NumElideArgs;
              continue;
            }
            AllArgsElided = false;
            if (NumElideArgs > 0) {
              PrintElideArgs(NumElideArgs, Indent);
              NumElideArgs = 0;
              OS << ", ";
            }
          }
          TreeToString(Indent);
          if (Tree.HasNextSibling())
            OS << ", ";
        } while (Tree.AdvanceSibling());
        if (NumElideArgs > 0) {
          if (AllArgsElided)
            OS << "...";
          else
            PrintElideArgs(NumElideArgs, Indent);
        }

        Tree.Parent();
        OS << ">";
        return;
      }
    }
  }

  // To signal to the text printer that a certain text needs to be bolded,
  // a special character is injected into the character stream which the
  // text printer will later strip out.

  /// Bold - Start bolding text.
  void Bold() {
    assert(!IsBold && "Attempting to bold text that is already bold.");
    IsBold = true;
    if (ShowColor)
      OS << ToggleHighlight;
  }

  /// Unbold - Stop bolding text.
  void Unbold() {
    assert(IsBold && "Attempting to remove bold from unbold text.");
    IsBold = false;
    if (ShowColor)
      OS << ToggleHighlight;
  }

  // Functions to print out the arguments and highlighting the difference.

  /// PrintTypeNames - prints the typenames, bolding differences.  Will detect
  /// typenames that are the same and attempt to disambiguate them by using
  /// canonical typenames.
  void PrintTypeNames(QualType FromType, QualType ToType,
                      bool FromDefault, bool ToDefault, bool Same) {
    assert((!FromType.isNull() || !ToType.isNull()) &&
           "Only one template argument may be missing.");

    if (Same) {
      OS << FromType.getAsString(Policy);
      return;
    }

    if (!FromType.isNull() && !ToType.isNull() &&
        FromType.getLocalUnqualifiedType() ==
        ToType.getLocalUnqualifiedType()) {
      Qualifiers FromQual = FromType.getLocalQualifiers(),
                 ToQual = ToType.getLocalQualifiers();
      PrintQualifiers(FromQual, ToQual);
      FromType.getLocalUnqualifiedType().print(OS, Policy);
      return;
    }

    std::string FromTypeStr = FromType.isNull() ? "(no argument)"
                                                : FromType.getAsString(Policy);
    std::string ToTypeStr = ToType.isNull() ? "(no argument)"
                                            : ToType.getAsString(Policy);
    // Print without ElaboratedType sugar if it is better.
    // TODO: merge this with other aka printing above.
    if (FromTypeStr == ToTypeStr) {
      const auto *FromElTy = dyn_cast<ElaboratedType>(FromType),
                 *ToElTy = dyn_cast<ElaboratedType>(ToType);
      if (FromElTy || ToElTy) {
        std::string FromNamedTypeStr =
            FromElTy ? FromElTy->getNamedType().getAsString(Policy)
                     : FromTypeStr;
        std::string ToNamedTypeStr =
            ToElTy ? ToElTy->getNamedType().getAsString(Policy) : ToTypeStr;
        if (FromNamedTypeStr != ToNamedTypeStr) {
          FromTypeStr = FromNamedTypeStr;
          ToTypeStr = ToNamedTypeStr;
          goto PrintTypes;
        }
      }
      // Switch to canonical typename if it is better.
      std::string FromCanTypeStr =
          FromType.getCanonicalType().getAsString(Policy);
      std::string ToCanTypeStr = ToType.getCanonicalType().getAsString(Policy);
      if (FromCanTypeStr != ToCanTypeStr) {
        FromTypeStr = FromCanTypeStr;
        ToTypeStr = ToCanTypeStr;
      }
    }

  PrintTypes:
    if (PrintTree) OS << '[';
    OS << (FromDefault ? "(default) " : "");
    Bold();
    OS << FromTypeStr;
    Unbold();
    if (PrintTree) {
      OS << " != " << (ToDefault ? "(default) " : "");
      Bold();
      OS << ToTypeStr;
      Unbold();
      OS << "]";
    }
  }

  /// PrintExpr - Prints out the expr template arguments, highlighting argument
  /// differences.
  void PrintExpr(const Expr *FromExpr, const Expr *ToExpr, bool FromDefault,
                 bool ToDefault, bool Same) {
    assert((FromExpr || ToExpr) &&
            "Only one template argument may be missing.");
    if (Same) {
      PrintExpr(FromExpr);
    } else if (!PrintTree) {
      OS << (FromDefault ? "(default) " : "");
      Bold();
      PrintExpr(FromExpr);
      Unbold();
    } else {
      OS << (FromDefault ? "[(default) " : "[");
      Bold();
      PrintExpr(FromExpr);
      Unbold();
      OS << " != " << (ToDefault ? "(default) " : "");
      Bold();
      PrintExpr(ToExpr);
      Unbold();
      OS << ']';
    }
  }

  /// PrintExpr - Actual formatting and printing of expressions.
  void PrintExpr(const Expr *E) {
    if (E) {
      E->printPretty(OS, nullptr, Policy);
      return;
    }
    OS << "(no argument)";
  }

  /// PrintTemplateTemplate - Handles printing of template template arguments,
  /// highlighting argument differences.
  void PrintTemplateTemplate(TemplateDecl *FromTD, TemplateDecl *ToTD,
                             bool FromDefault, bool ToDefault, bool Same) {
    assert((FromTD || ToTD) && "Only one template argument may be missing.");

    std::string FromName =
        std::string(FromTD ? FromTD->getName() : "(no argument)");
    std::string ToName = std::string(ToTD ? ToTD->getName() : "(no argument)");
    if (FromTD && ToTD && FromName == ToName) {
      FromName = FromTD->getQualifiedNameAsString();
      ToName = ToTD->getQualifiedNameAsString();
    }

    if (Same) {
      OS << "template " << FromTD->getDeclName();
    } else if (!PrintTree) {
      OS << (FromDefault ? "(default) template " : "template ");
      Bold();
      OS << FromName;
      Unbold();
    } else {
      OS << (FromDefault ? "[(default) template " : "[template ");
      Bold();
      OS << FromName;
      Unbold();
      OS << " != " << (ToDefault ? "(default) template " : "template ");
      Bold();
      OS << ToName;
      Unbold();
      OS << ']';
    }
  }

  /// PrintAPSInt - Handles printing of integral arguments, highlighting
  /// argument differences.
  void PrintAPSInt(const llvm::APSInt &FromInt, const llvm::APSInt &ToInt,
                   bool IsValidFromInt, bool IsValidToInt, QualType FromIntType,
                   QualType ToIntType, Expr *FromExpr, Expr *ToExpr,
                   bool FromDefault, bool ToDefault, bool Same) {
    assert((IsValidFromInt || IsValidToInt) &&
           "Only one integral argument may be missing.");

    if (Same) {
      if (FromIntType->isBooleanType()) {
        OS << ((FromInt == 0) ? "false" : "true");
      } else {
        OS << toString(FromInt, 10);
      }
      return;
    }

    bool PrintType = IsValidFromInt && IsValidToInt &&
                     !Context.hasSameType(FromIntType, ToIntType);

    if (!PrintTree) {
      OS << (FromDefault ? "(default) " : "");
      PrintAPSInt(FromInt, FromExpr, IsValidFromInt, FromIntType, PrintType);
    } else {
      OS << (FromDefault ? "[(default) " : "[");
      PrintAPSInt(FromInt, FromExpr, IsValidFromInt, FromIntType, PrintType);
      OS << " != " << (ToDefault ? "(default) " : "");
      PrintAPSInt(ToInt, ToExpr, IsValidToInt, ToIntType, PrintType);
      OS << ']';
    }
  }

  /// PrintAPSInt - If valid, print the APSInt.  If the expression is
  /// gives more information, print it too.
  void PrintAPSInt(const llvm::APSInt &Val, Expr *E, bool Valid,
                   QualType IntType, bool PrintType) {
    Bold();
    if (Valid) {
      if (HasExtraInfo(E)) {
        PrintExpr(E);
        Unbold();
        OS << " aka ";
        Bold();
      }
      if (PrintType) {
        Unbold();
        OS << "(";
        Bold();
        IntType.print(OS, Context.getPrintingPolicy());
        Unbold();
        OS << ") ";
        Bold();
      }
      if (IntType->isBooleanType()) {
        OS << ((Val == 0) ? "false" : "true");
      } else {
        OS << toString(Val, 10);
      }
    } else if (E) {
      PrintExpr(E);
    } else {
      OS << "(no argument)";
    }
    Unbold();
  }

  /// HasExtraInfo - Returns true if E is not an integer literal, the
  /// negation of an integer literal, or a boolean literal.
  bool HasExtraInfo(Expr *E) {
    if (!E) return false;

    E = E->IgnoreImpCasts();

    if (isa<IntegerLiteral>(E)) return false;

    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E))
      if (UO->getOpcode() == UO_Minus)
        if (isa<IntegerLiteral>(UO->getSubExpr()))
          return false;

    if (isa<CXXBoolLiteralExpr>(E))
      return false;

    return true;
  }

  void PrintValueDecl(ValueDecl *VD, bool AddressOf, Expr *E, bool NullPtr) {
    if (VD) {
      if (AddressOf)
        OS << "&";
      else if (auto *TPO = dyn_cast<TemplateParamObjectDecl>(VD)) {
        // FIXME: Diffing the APValue would be neat.
        // FIXME: Suppress this and use the full name of the declaration if the
        // parameter is a pointer or reference.
        TPO->getType().getUnqualifiedType().print(OS, Policy);
        TPO->printAsInit(OS, Policy);
        return;
      }
      VD->printName(OS, Policy);
      return;
    }

    if (NullPtr) {
      if (E && !isa<CXXNullPtrLiteralExpr>(E)) {
        PrintExpr(E);
        if (IsBold) {
          Unbold();
          OS << " aka ";
          Bold();
        } else {
          OS << " aka ";
        }
      }

      OS << "nullptr";
      return;
    }

    if (E) {
      PrintExpr(E);
      return;
    }

    OS << "(no argument)";
  }

  /// PrintDecl - Handles printing of Decl arguments, highlighting
  /// argument differences.
  void PrintValueDecl(ValueDecl *FromValueDecl, ValueDecl *ToValueDecl,
                      bool FromAddressOf, bool ToAddressOf, bool FromNullPtr,
                      bool ToNullPtr, Expr *FromExpr, Expr *ToExpr,
                      bool FromDefault, bool ToDefault, bool Same) {
    assert((FromValueDecl || FromNullPtr || ToValueDecl || ToNullPtr) &&
           "Only one Decl argument may be NULL");

    if (Same) {
      PrintValueDecl(FromValueDecl, FromAddressOf, FromExpr, FromNullPtr);
    } else if (!PrintTree) {
      OS << (FromDefault ? "(default) " : "");
      Bold();
      PrintValueDecl(FromValueDecl, FromAddressOf, FromExpr, FromNullPtr);
      Unbold();
    } else {
      OS << (FromDefault ? "[(default) " : "[");
      Bold();
      PrintValueDecl(FromValueDecl, FromAddressOf, FromExpr, FromNullPtr);
      Unbold();
      OS << " != " << (ToDefault ? "(default) " : "");
      Bold();
      PrintValueDecl(ToValueDecl, ToAddressOf, ToExpr, ToNullPtr);
      Unbold();
      OS << ']';
    }
  }

  /// PrintValueDeclAndInteger - Uses the print functions for ValueDecl and
  /// APSInt to print a mixed difference.
  void PrintValueDeclAndInteger(ValueDecl *VD, bool NeedAddressOf,
                                bool IsNullPtr, Expr *VDExpr, bool DefaultDecl,
                                const llvm::APSInt &Val, QualType IntType,
                                Expr *IntExpr, bool DefaultInt) {
    if (!PrintTree) {
      OS << (DefaultDecl ? "(default) " : "");
      Bold();
      PrintValueDecl(VD, NeedAddressOf, VDExpr, IsNullPtr);
      Unbold();
    } else {
      OS << (DefaultDecl ? "[(default) " : "[");
      Bold();
      PrintValueDecl(VD, NeedAddressOf, VDExpr, IsNullPtr);
      Unbold();
      OS << " != " << (DefaultInt ? "(default) " : "");
      PrintAPSInt(Val, IntExpr, true /*Valid*/, IntType, false /*PrintType*/);
      OS << ']';
    }
  }

  /// PrintIntegerAndValueDecl - Uses the print functions for APSInt and
  /// ValueDecl to print a mixed difference.
  void PrintIntegerAndValueDecl(const llvm::APSInt &Val, QualType IntType,
                                Expr *IntExpr, bool DefaultInt, ValueDecl *VD,
                                bool NeedAddressOf, bool IsNullPtr,
                                Expr *VDExpr, bool DefaultDecl) {
    if (!PrintTree) {
      OS << (DefaultInt ? "(default) " : "");
      PrintAPSInt(Val, IntExpr, true /*Valid*/, IntType, false /*PrintType*/);
    } else {
      OS << (DefaultInt ? "[(default) " : "[");
      PrintAPSInt(Val, IntExpr, true /*Valid*/, IntType, false /*PrintType*/);
      OS << " != " << (DefaultDecl ? "(default) " : "");
      Bold();
      PrintValueDecl(VD, NeedAddressOf, VDExpr, IsNullPtr);
      Unbold();
      OS << ']';
    }
  }

  // Prints the appropriate placeholder for elided template arguments.
  void PrintElideArgs(unsigned NumElideArgs, unsigned Indent) {
    if (PrintTree) {
      OS << '\n';
      for (unsigned i = 0; i < Indent; ++i)
        OS << "  ";
    }
    if (NumElideArgs == 0) return;
    if (NumElideArgs == 1)
      OS << "[...]";
    else
      OS << "[" << NumElideArgs << " * ...]";
  }

  // Prints and highlights differences in Qualifiers.
  void PrintQualifiers(Qualifiers FromQual, Qualifiers ToQual) {
    // Both types have no qualifiers
    if (FromQual.empty() && ToQual.empty())
      return;

    // Both types have same qualifiers
    if (FromQual == ToQual) {
      PrintQualifier(FromQual, /*ApplyBold*/false);
      return;
    }

    // Find common qualifiers and strip them from FromQual and ToQual.
    Qualifiers CommonQual = Qualifiers::removeCommonQualifiers(FromQual,
                                                               ToQual);

    // The qualifiers are printed before the template name.
    // Inline printing:
    // The common qualifiers are printed.  Then, qualifiers only in this type
    // are printed and highlighted.  Finally, qualifiers only in the other
    // type are printed and highlighted inside parentheses after "missing".
    // Tree printing:
    // Qualifiers are printed next to each other, inside brackets, and
    // separated by "!=".  The printing order is:
    // common qualifiers, highlighted from qualifiers, "!=",
    // common qualifiers, highlighted to qualifiers
    if (PrintTree) {
      OS << "[";
      if (CommonQual.empty() && FromQual.empty()) {
        Bold();
        OS << "(no qualifiers) ";
        Unbold();
      } else {
        PrintQualifier(CommonQual, /*ApplyBold*/false);
        PrintQualifier(FromQual, /*ApplyBold*/true);
      }
      OS << "!= ";
      if (CommonQual.empty() && ToQual.empty()) {
        Bold();
        OS << "(no qualifiers)";
        Unbold();
      } else {
        PrintQualifier(CommonQual, /*ApplyBold*/false,
                       /*appendSpaceIfNonEmpty*/!ToQual.empty());
        PrintQualifier(ToQual, /*ApplyBold*/true,
                       /*appendSpaceIfNonEmpty*/false);
      }
      OS << "] ";
    } else {
      PrintQualifier(CommonQual, /*ApplyBold*/false);
      PrintQualifier(FromQual, /*ApplyBold*/true);
    }
  }

  void PrintQualifier(Qualifiers Q, bool ApplyBold,
                      bool AppendSpaceIfNonEmpty = true) {
    if (Q.empty()) return;
    if (ApplyBold) Bold();
    Q.print(OS, Policy, AppendSpaceIfNonEmpty);
    if (ApplyBold) Unbold();
  }

public:

  TemplateDiff(raw_ostream &OS, ASTContext &Context, QualType FromType,
               QualType ToType, bool PrintTree, bool PrintFromType,
               bool ElideType, bool ShowColor)
    : Context(Context),
      Policy(Context.getLangOpts()),
      ElideType(ElideType),
      PrintTree(PrintTree),
      ShowColor(ShowColor),
      // When printing a single type, the FromType is the one printed.
      FromTemplateType(PrintFromType ? FromType : ToType),
      ToTemplateType(PrintFromType ? ToType : FromType),
      OS(OS),
      IsBold(false) {
  }

  /// DiffTemplate - Start the template type diffing.
  void DiffTemplate() {
    Qualifiers FromQual = FromTemplateType.getQualifiers(),
               ToQual = ToTemplateType.getQualifiers();

    const TemplateSpecializationType *FromOrigTST =
        GetTemplateSpecializationType(Context, FromTemplateType);
    const TemplateSpecializationType *ToOrigTST =
        GetTemplateSpecializationType(Context, ToTemplateType);

    // Only checking templates.
    if (!FromOrigTST || !ToOrigTST)
      return;

    // Different base templates.
    if (!hasSameTemplate(FromOrigTST, ToOrigTST)) {
      return;
    }

    FromQual -= QualType(FromOrigTST, 0).getQualifiers();
    ToQual -= QualType(ToOrigTST, 0).getQualifiers();

    // Same base template, but different arguments.
    Tree.SetTemplateDiff(FromOrigTST->getTemplateName().getAsTemplateDecl(),
                         ToOrigTST->getTemplateName().getAsTemplateDecl(),
                         FromQual, ToQual, false /*FromDefault*/,
                         false /*ToDefault*/);

    DiffTemplate(FromOrigTST, ToOrigTST);
  }

  /// Emit - When the two types given are templated types with the same
  /// base template, a string representation of the type difference will be
  /// emitted to the stream and return true.  Otherwise, return false.
  bool Emit() {
    Tree.StartTraverse();
    if (Tree.Empty())
      return false;

    TreeToString();
    assert(!IsBold && "Bold is applied to end of string.");
    return true;
  }
}; // end class TemplateDiff
}  // end anonymous namespace

/// FormatTemplateTypeDiff - A helper static function to start the template
/// diff and return the properly formatted string.  Returns true if the diff
/// is successful.
static bool FormatTemplateTypeDiff(ASTContext &Context, QualType FromType,
                                   QualType ToType, bool PrintTree,
                                   bool PrintFromType, bool ElideType,
                                   bool ShowColors, raw_ostream &OS) {
  if (PrintTree)
    PrintFromType = true;
  TemplateDiff TD(OS, Context, FromType, ToType, PrintTree, PrintFromType,
                  ElideType, ShowColors);
  TD.DiffTemplate();
  return TD.Emit();
}
