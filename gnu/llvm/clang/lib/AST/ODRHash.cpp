//===-- ODRHash.cpp - Hashing to diagnose ODR failures ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the ODRHash class, which calculates a hash based
/// on AST nodes, which is stable across different runs.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ODRHash.h"

#include "clang/AST/DeclVisitor.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeVisitor.h"

using namespace clang;

void ODRHash::AddStmt(const Stmt *S) {
  assert(S && "Expecting non-null pointer.");
  S->ProcessODRHash(ID, *this);
}

void ODRHash::AddIdentifierInfo(const IdentifierInfo *II) {
  assert(II && "Expecting non-null pointer.");
  ID.AddString(II->getName());
}

void ODRHash::AddDeclarationName(DeclarationName Name, bool TreatAsDecl) {
  if (TreatAsDecl)
    // Matches the NamedDecl check in AddDecl
    AddBoolean(true);

  AddDeclarationNameImpl(Name);

  if (TreatAsDecl)
    // Matches the ClassTemplateSpecializationDecl check in AddDecl
    AddBoolean(false);
}

void ODRHash::AddDeclarationNameImpl(DeclarationName Name) {
  // Index all DeclarationName and use index numbers to refer to them.
  auto Result = DeclNameMap.insert(std::make_pair(Name, DeclNameMap.size()));
  ID.AddInteger(Result.first->second);
  if (!Result.second) {
    // If found in map, the DeclarationName has previously been processed.
    return;
  }

  // First time processing each DeclarationName, also process its details.
  AddBoolean(Name.isEmpty());
  if (Name.isEmpty())
    return;

  auto Kind = Name.getNameKind();
  ID.AddInteger(Kind);
  switch (Kind) {
  case DeclarationName::Identifier:
    AddIdentifierInfo(Name.getAsIdentifierInfo());
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector: {
    Selector S = Name.getObjCSelector();
    AddBoolean(S.isNull());
    AddBoolean(S.isKeywordSelector());
    AddBoolean(S.isUnarySelector());
    unsigned NumArgs = S.getNumArgs();
    ID.AddInteger(NumArgs);
    // Compare all selector slots. For selectors with arguments it means all arg
    // slots. And if there are no arguments, compare the first-and-only slot.
    unsigned SlotsToCheck = NumArgs > 0 ? NumArgs : 1;
    for (unsigned i = 0; i < SlotsToCheck; ++i) {
      const IdentifierInfo *II = S.getIdentifierInfoForSlot(i);
      AddBoolean(II);
      if (II) {
        AddIdentifierInfo(II);
      }
    }
    break;
  }
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
    AddQualType(Name.getCXXNameType());
    break;
  case DeclarationName::CXXOperatorName:
    ID.AddInteger(Name.getCXXOverloadedOperator());
    break;
  case DeclarationName::CXXLiteralOperatorName:
    AddIdentifierInfo(Name.getCXXLiteralIdentifier());
    break;
  case DeclarationName::CXXConversionFunctionName:
    AddQualType(Name.getCXXNameType());
    break;
  case DeclarationName::CXXUsingDirective:
    break;
  case DeclarationName::CXXDeductionGuideName: {
    auto *Template = Name.getCXXDeductionGuideTemplate();
    AddBoolean(Template);
    if (Template) {
      AddDecl(Template);
    }
  }
  }
}

void ODRHash::AddNestedNameSpecifier(const NestedNameSpecifier *NNS) {
  assert(NNS && "Expecting non-null pointer.");
  const auto *Prefix = NNS->getPrefix();
  AddBoolean(Prefix);
  if (Prefix) {
    AddNestedNameSpecifier(Prefix);
  }
  auto Kind = NNS->getKind();
  ID.AddInteger(Kind);
  switch (Kind) {
  case NestedNameSpecifier::Identifier:
    AddIdentifierInfo(NNS->getAsIdentifier());
    break;
  case NestedNameSpecifier::Namespace:
    AddDecl(NNS->getAsNamespace());
    break;
  case NestedNameSpecifier::NamespaceAlias:
    AddDecl(NNS->getAsNamespaceAlias());
    break;
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    AddType(NNS->getAsType());
    break;
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    break;
  }
}

void ODRHash::AddTemplateName(TemplateName Name) {
  auto Kind = Name.getKind();
  ID.AddInteger(Kind);

  switch (Kind) {
  case TemplateName::Template:
    AddDecl(Name.getAsTemplateDecl());
    break;
  case TemplateName::QualifiedTemplate: {
    QualifiedTemplateName *QTN = Name.getAsQualifiedTemplateName();
    if (NestedNameSpecifier *NNS = QTN->getQualifier())
      AddNestedNameSpecifier(NNS);
    AddBoolean(QTN->hasTemplateKeyword());
    AddTemplateName(QTN->getUnderlyingTemplate());
    break;
  }
  // TODO: Support these cases.
  case TemplateName::OverloadedTemplate:
  case TemplateName::AssumedTemplate:
  case TemplateName::DependentTemplate:
  case TemplateName::SubstTemplateTemplateParm:
  case TemplateName::SubstTemplateTemplateParmPack:
  case TemplateName::UsingTemplate:
    break;
  }
}

void ODRHash::AddTemplateArgument(TemplateArgument TA) {
  const auto Kind = TA.getKind();
  ID.AddInteger(Kind);

  switch (Kind) {
    case TemplateArgument::Null:
      llvm_unreachable("Expected valid TemplateArgument");
    case TemplateArgument::Type:
      AddQualType(TA.getAsType());
      break;
    case TemplateArgument::Declaration:
      AddDecl(TA.getAsDecl());
      break;
    case TemplateArgument::NullPtr:
      ID.AddPointer(nullptr);
      break;
    case TemplateArgument::Integral: {
      // There are integrals (e.g.: _BitInt(128)) that cannot be represented as
      // any builtin integral type, so we use the hash of APSInt instead.
      TA.getAsIntegral().Profile(ID);
      break;
    }
    case TemplateArgument::StructuralValue:
      AddQualType(TA.getStructuralValueType());
      AddStructuralValue(TA.getAsStructuralValue());
      break;
    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      AddTemplateName(TA.getAsTemplateOrTemplatePattern());
      break;
    case TemplateArgument::Expression:
      AddStmt(TA.getAsExpr());
      break;
    case TemplateArgument::Pack:
      ID.AddInteger(TA.pack_size());
      for (auto SubTA : TA.pack_elements()) {
        AddTemplateArgument(SubTA);
      }
      break;
  }
}

void ODRHash::AddTemplateParameterList(const TemplateParameterList *TPL) {
  assert(TPL && "Expecting non-null pointer.");

  ID.AddInteger(TPL->size());
  for (auto *ND : TPL->asArray()) {
    AddSubDecl(ND);
  }
}

void ODRHash::clear() {
  DeclNameMap.clear();
  Bools.clear();
  ID.clear();
}

unsigned ODRHash::CalculateHash() {
  // Append the bools to the end of the data segment backwards.  This allows
  // for the bools data to be compressed 32 times smaller compared to using
  // ID.AddBoolean
  const unsigned unsigned_bits = sizeof(unsigned) * CHAR_BIT;
  const unsigned size = Bools.size();
  const unsigned remainder = size % unsigned_bits;
  const unsigned loops = size / unsigned_bits;
  auto I = Bools.rbegin();
  unsigned value = 0;
  for (unsigned i = 0; i < remainder; ++i) {
    value <<= 1;
    value |= *I;
    ++I;
  }
  ID.AddInteger(value);

  for (unsigned i = 0; i < loops; ++i) {
    value = 0;
    for (unsigned j = 0; j < unsigned_bits; ++j) {
      value <<= 1;
      value |= *I;
      ++I;
    }
    ID.AddInteger(value);
  }

  assert(I == Bools.rend());
  Bools.clear();
  return ID.computeStableHash();
}

namespace {
// Process a Decl pointer.  Add* methods call back into ODRHash while Visit*
// methods process the relevant parts of the Decl.
class ODRDeclVisitor : public ConstDeclVisitor<ODRDeclVisitor> {
  typedef ConstDeclVisitor<ODRDeclVisitor> Inherited;
  llvm::FoldingSetNodeID &ID;
  ODRHash &Hash;

public:
  ODRDeclVisitor(llvm::FoldingSetNodeID &ID, ODRHash &Hash)
      : ID(ID), Hash(Hash) {}

  void AddStmt(const Stmt *S) {
    Hash.AddBoolean(S);
    if (S) {
      Hash.AddStmt(S);
    }
  }

  void AddIdentifierInfo(const IdentifierInfo *II) {
    Hash.AddBoolean(II);
    if (II) {
      Hash.AddIdentifierInfo(II);
    }
  }

  void AddQualType(QualType T) {
    Hash.AddQualType(T);
  }

  void AddDecl(const Decl *D) {
    Hash.AddBoolean(D);
    if (D) {
      Hash.AddDecl(D);
    }
  }

  void AddTemplateArgument(TemplateArgument TA) {
    Hash.AddTemplateArgument(TA);
  }

  void Visit(const Decl *D) {
    ID.AddInteger(D->getKind());
    Inherited::Visit(D);
  }

  void VisitNamedDecl(const NamedDecl *D) {
    Hash.AddDeclarationName(D->getDeclName());
    Inherited::VisitNamedDecl(D);
  }

  void VisitValueDecl(const ValueDecl *D) {
    if (auto *DD = dyn_cast<DeclaratorDecl>(D); DD && DD->getTypeSourceInfo())
      AddQualType(DD->getTypeSourceInfo()->getType());

    Inherited::VisitValueDecl(D);
  }

  void VisitVarDecl(const VarDecl *D) {
    Hash.AddBoolean(D->isStaticLocal());
    Hash.AddBoolean(D->isConstexpr());
    const bool HasInit = D->hasInit();
    Hash.AddBoolean(HasInit);
    if (HasInit) {
      AddStmt(D->getInit());
    }
    Inherited::VisitVarDecl(D);
  }

  void VisitParmVarDecl(const ParmVarDecl *D) {
    // TODO: Handle default arguments.
    Inherited::VisitParmVarDecl(D);
  }

  void VisitAccessSpecDecl(const AccessSpecDecl *D) {
    ID.AddInteger(D->getAccess());
    Inherited::VisitAccessSpecDecl(D);
  }

  void VisitStaticAssertDecl(const StaticAssertDecl *D) {
    AddStmt(D->getAssertExpr());
    AddStmt(D->getMessage());

    Inherited::VisitStaticAssertDecl(D);
  }

  void VisitFieldDecl(const FieldDecl *D) {
    const bool IsBitfield = D->isBitField();
    Hash.AddBoolean(IsBitfield);

    if (IsBitfield) {
      AddStmt(D->getBitWidth());
    }

    Hash.AddBoolean(D->isMutable());
    AddStmt(D->getInClassInitializer());

    Inherited::VisitFieldDecl(D);
  }

  void VisitObjCIvarDecl(const ObjCIvarDecl *D) {
    ID.AddInteger(D->getCanonicalAccessControl());
    Inherited::VisitObjCIvarDecl(D);
  }

  void VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
    ID.AddInteger(D->getPropertyAttributes());
    ID.AddInteger(D->getPropertyImplementation());
    AddQualType(D->getTypeSourceInfo()->getType());
    AddDecl(D);

    Inherited::VisitObjCPropertyDecl(D);
  }

  void VisitFunctionDecl(const FunctionDecl *D) {
    // Handled by the ODRHash for FunctionDecl
    ID.AddInteger(D->getODRHash());

    Inherited::VisitFunctionDecl(D);
  }

  void VisitCXXMethodDecl(const CXXMethodDecl *D) {
    // Handled by the ODRHash for FunctionDecl

    Inherited::VisitCXXMethodDecl(D);
  }

  void VisitObjCMethodDecl(const ObjCMethodDecl *Method) {
    ID.AddInteger(Method->getDeclKind());
    Hash.AddBoolean(Method->isInstanceMethod()); // false if class method
    Hash.AddBoolean(Method->isVariadic());
    Hash.AddBoolean(Method->isSynthesizedAccessorStub());
    Hash.AddBoolean(Method->isDefined());
    Hash.AddBoolean(Method->isDirectMethod());
    Hash.AddBoolean(Method->isThisDeclarationADesignatedInitializer());
    Hash.AddBoolean(Method->hasSkippedBody());

    ID.AddInteger(llvm::to_underlying(Method->getImplementationControl()));
    ID.AddInteger(Method->getMethodFamily());
    ImplicitParamDecl *Cmd = Method->getCmdDecl();
    Hash.AddBoolean(Cmd);
    if (Cmd)
      ID.AddInteger(llvm::to_underlying(Cmd->getParameterKind()));

    ImplicitParamDecl *Self = Method->getSelfDecl();
    Hash.AddBoolean(Self);
    if (Self)
      ID.AddInteger(llvm::to_underlying(Self->getParameterKind()));

    AddDecl(Method);

    if (Method->getReturnTypeSourceInfo())
      AddQualType(Method->getReturnTypeSourceInfo()->getType());

    ID.AddInteger(Method->param_size());
    for (auto Param : Method->parameters())
      Hash.AddSubDecl(Param);

    if (Method->hasBody()) {
      const bool IsDefinition = Method->isThisDeclarationADefinition();
      Hash.AddBoolean(IsDefinition);
      if (IsDefinition) {
        Stmt *Body = Method->getBody();
        Hash.AddBoolean(Body);
        if (Body)
          AddStmt(Body);

        // Filter out sub-Decls which will not be processed in order to get an
        // accurate count of Decl's.
        llvm::SmallVector<const Decl *, 16> Decls;
        for (Decl *SubDecl : Method->decls())
          if (ODRHash::isSubDeclToBeProcessed(SubDecl, Method))
            Decls.push_back(SubDecl);

        ID.AddInteger(Decls.size());
        for (auto SubDecl : Decls)
          Hash.AddSubDecl(SubDecl);
      }
    } else {
      Hash.AddBoolean(false);
    }

    Inherited::VisitObjCMethodDecl(Method);
  }

  void VisitTypedefNameDecl(const TypedefNameDecl *D) {
    AddQualType(D->getUnderlyingType());

    Inherited::VisitTypedefNameDecl(D);
  }

  void VisitTypedefDecl(const TypedefDecl *D) {
    Inherited::VisitTypedefDecl(D);
  }

  void VisitTypeAliasDecl(const TypeAliasDecl *D) {
    Inherited::VisitTypeAliasDecl(D);
  }

  void VisitFriendDecl(const FriendDecl *D) {
    TypeSourceInfo *TSI = D->getFriendType();
    Hash.AddBoolean(TSI);
    if (TSI) {
      AddQualType(TSI->getType());
    } else {
      AddDecl(D->getFriendDecl());
    }
  }

  void VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D) {
    // Only care about default arguments as part of the definition.
    const bool hasDefaultArgument =
        D->hasDefaultArgument() && !D->defaultArgumentWasInherited();
    Hash.AddBoolean(hasDefaultArgument);
    if (hasDefaultArgument) {
      AddTemplateArgument(D->getDefaultArgument().getArgument());
    }
    Hash.AddBoolean(D->isParameterPack());

    const TypeConstraint *TC = D->getTypeConstraint();
    Hash.AddBoolean(TC != nullptr);
    if (TC)
      AddStmt(TC->getImmediatelyDeclaredConstraint());

    Inherited::VisitTemplateTypeParmDecl(D);
  }

  void VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D) {
    // Only care about default arguments as part of the definition.
    const bool hasDefaultArgument =
        D->hasDefaultArgument() && !D->defaultArgumentWasInherited();
    Hash.AddBoolean(hasDefaultArgument);
    if (hasDefaultArgument) {
      AddTemplateArgument(D->getDefaultArgument().getArgument());
    }
    Hash.AddBoolean(D->isParameterPack());

    Inherited::VisitNonTypeTemplateParmDecl(D);
  }

  void VisitTemplateTemplateParmDecl(const TemplateTemplateParmDecl *D) {
    // Only care about default arguments as part of the definition.
    const bool hasDefaultArgument =
        D->hasDefaultArgument() && !D->defaultArgumentWasInherited();
    Hash.AddBoolean(hasDefaultArgument);
    if (hasDefaultArgument) {
      AddTemplateArgument(D->getDefaultArgument().getArgument());
    }
    Hash.AddBoolean(D->isParameterPack());

    Inherited::VisitTemplateTemplateParmDecl(D);
  }

  void VisitTemplateDecl(const TemplateDecl *D) {
    Hash.AddTemplateParameterList(D->getTemplateParameters());

    Inherited::VisitTemplateDecl(D);
  }

  void VisitRedeclarableTemplateDecl(const RedeclarableTemplateDecl *D) {
    Hash.AddBoolean(D->isMemberSpecialization());
    Inherited::VisitRedeclarableTemplateDecl(D);
  }

  void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
    AddDecl(D->getTemplatedDecl());
    ID.AddInteger(D->getTemplatedDecl()->getODRHash());
    Inherited::VisitFunctionTemplateDecl(D);
  }

  void VisitEnumConstantDecl(const EnumConstantDecl *D) {
    AddStmt(D->getInitExpr());
    Inherited::VisitEnumConstantDecl(D);
  }
};
} // namespace

// Only allow a small portion of Decl's to be processed.  Remove this once
// all Decl's can be handled.
bool ODRHash::isSubDeclToBeProcessed(const Decl *D, const DeclContext *Parent) {
  if (D->isImplicit()) return false;
  if (D->getDeclContext() != Parent) return false;

  switch (D->getKind()) {
    default:
      return false;
    case Decl::AccessSpec:
    case Decl::CXXConstructor:
    case Decl::CXXDestructor:
    case Decl::CXXMethod:
    case Decl::EnumConstant: // Only found in EnumDecl's.
    case Decl::Field:
    case Decl::Friend:
    case Decl::FunctionTemplate:
    case Decl::StaticAssert:
    case Decl::TypeAlias:
    case Decl::Typedef:
    case Decl::Var:
    case Decl::ObjCMethod:
    case Decl::ObjCIvar:
    case Decl::ObjCProperty:
      return true;
  }
}

void ODRHash::AddSubDecl(const Decl *D) {
  assert(D && "Expecting non-null pointer.");

  ODRDeclVisitor(ID, *this).Visit(D);
}

void ODRHash::AddCXXRecordDecl(const CXXRecordDecl *Record) {
  assert(Record && Record->hasDefinition() &&
         "Expected non-null record to be a definition.");

  const DeclContext *DC = Record;
  while (DC) {
    if (isa<ClassTemplateSpecializationDecl>(DC)) {
      return;
    }
    DC = DC->getParent();
  }

  AddDecl(Record);

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : Record->decls()) {
    if (isSubDeclToBeProcessed(SubDecl, Record)) {
      Decls.push_back(SubDecl);
      if (auto *Function = dyn_cast<FunctionDecl>(SubDecl)) {
        // Compute/Preload ODRHash into FunctionDecl.
        Function->getODRHash();
      }
    }
  }

  ID.AddInteger(Decls.size());
  for (auto SubDecl : Decls) {
    AddSubDecl(SubDecl);
  }

  const ClassTemplateDecl *TD = Record->getDescribedClassTemplate();
  AddBoolean(TD);
  if (TD) {
    AddTemplateParameterList(TD->getTemplateParameters());
  }

  ID.AddInteger(Record->getNumBases());
  auto Bases = Record->bases();
  for (const auto &Base : Bases) {
    AddQualType(Base.getTypeSourceInfo()->getType());
    ID.AddInteger(Base.isVirtual());
    ID.AddInteger(Base.getAccessSpecifierAsWritten());
  }
}

void ODRHash::AddRecordDecl(const RecordDecl *Record) {
  assert(!isa<CXXRecordDecl>(Record) &&
         "For CXXRecordDecl should call AddCXXRecordDecl.");
  AddDecl(Record);

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : Record->decls()) {
    if (isSubDeclToBeProcessed(SubDecl, Record))
      Decls.push_back(SubDecl);
  }

  ID.AddInteger(Decls.size());
  for (const Decl *SubDecl : Decls)
    AddSubDecl(SubDecl);
}

void ODRHash::AddObjCInterfaceDecl(const ObjCInterfaceDecl *IF) {
  AddDecl(IF);

  auto *SuperClass = IF->getSuperClass();
  AddBoolean(SuperClass);
  if (SuperClass)
    ID.AddInteger(SuperClass->getODRHash());

  // Hash referenced protocols.
  ID.AddInteger(IF->getReferencedProtocols().size());
  for (const ObjCProtocolDecl *RefP : IF->protocols()) {
    // Hash the name only as a referenced protocol can be a forward declaration.
    AddDeclarationName(RefP->getDeclName());
  }

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : IF->decls())
    if (isSubDeclToBeProcessed(SubDecl, IF))
      Decls.push_back(SubDecl);

  ID.AddInteger(Decls.size());
  for (auto *SubDecl : Decls)
    AddSubDecl(SubDecl);
}

void ODRHash::AddFunctionDecl(const FunctionDecl *Function,
                              bool SkipBody) {
  assert(Function && "Expecting non-null pointer.");

  // Skip functions that are specializations or in specialization context.
  const DeclContext *DC = Function;
  while (DC) {
    if (isa<ClassTemplateSpecializationDecl>(DC)) return;
    if (auto *F = dyn_cast<FunctionDecl>(DC)) {
      if (F->isFunctionTemplateSpecialization()) {
        if (!isa<CXXMethodDecl>(DC)) return;
        if (DC->getLexicalParent()->isFileContext()) return;
        // Skip class scope explicit function template specializations,
        // as they have not yet been instantiated.
        if (F->getDependentSpecializationInfo())
          return;
        // Inline method specializations are the only supported
        // specialization for now.
      }
    }
    DC = DC->getParent();
  }

  ID.AddInteger(Function->getDeclKind());

  const auto *SpecializationArgs = Function->getTemplateSpecializationArgs();
  AddBoolean(SpecializationArgs);
  if (SpecializationArgs) {
    ID.AddInteger(SpecializationArgs->size());
    for (const TemplateArgument &TA : SpecializationArgs->asArray()) {
      AddTemplateArgument(TA);
    }
  }

  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function)) {
    AddBoolean(Method->isConst());
    AddBoolean(Method->isVolatile());
  }

  ID.AddInteger(Function->getStorageClass());
  AddBoolean(Function->isInlineSpecified());
  AddBoolean(Function->isVirtualAsWritten());
  AddBoolean(Function->isPureVirtual());
  AddBoolean(Function->isDeletedAsWritten());
  AddBoolean(Function->isExplicitlyDefaulted());

  StringLiteral *DeletedMessage = Function->getDeletedMessage();
  AddBoolean(DeletedMessage);

  if (DeletedMessage)
    ID.AddString(DeletedMessage->getBytes());

  AddDecl(Function);

  AddQualType(Function->getReturnType());

  ID.AddInteger(Function->param_size());
  for (auto *Param : Function->parameters())
    AddSubDecl(Param);

  if (SkipBody) {
    AddBoolean(false);
    return;
  }

  const bool HasBody = Function->isThisDeclarationADefinition() &&
                       !Function->isDefaulted() && !Function->isDeleted() &&
                       !Function->isLateTemplateParsed();
  AddBoolean(HasBody);
  if (!HasBody) {
    return;
  }

  auto *Body = Function->getBody();
  AddBoolean(Body);
  if (Body)
    AddStmt(Body);

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : Function->decls()) {
    if (isSubDeclToBeProcessed(SubDecl, Function)) {
      Decls.push_back(SubDecl);
    }
  }

  ID.AddInteger(Decls.size());
  for (auto SubDecl : Decls) {
    AddSubDecl(SubDecl);
  }
}

void ODRHash::AddEnumDecl(const EnumDecl *Enum) {
  assert(Enum);
  AddDeclarationName(Enum->getDeclName());

  AddBoolean(Enum->isScoped());
  if (Enum->isScoped())
    AddBoolean(Enum->isScopedUsingClassTag());

  if (Enum->getIntegerTypeSourceInfo())
    AddQualType(Enum->getIntegerType().getCanonicalType());

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : Enum->decls()) {
    if (isSubDeclToBeProcessed(SubDecl, Enum)) {
      assert(isa<EnumConstantDecl>(SubDecl) && "Unexpected Decl");
      Decls.push_back(SubDecl);
    }
  }

  ID.AddInteger(Decls.size());
  for (auto SubDecl : Decls) {
    AddSubDecl(SubDecl);
  }

}

void ODRHash::AddObjCProtocolDecl(const ObjCProtocolDecl *P) {
  AddDecl(P);

  // Hash referenced protocols.
  ID.AddInteger(P->getReferencedProtocols().size());
  for (const ObjCProtocolDecl *RefP : P->protocols()) {
    // Hash the name only as a referenced protocol can be a forward declaration.
    AddDeclarationName(RefP->getDeclName());
  }

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : P->decls()) {
    if (isSubDeclToBeProcessed(SubDecl, P)) {
      Decls.push_back(SubDecl);
    }
  }

  ID.AddInteger(Decls.size());
  for (auto *SubDecl : Decls) {
    AddSubDecl(SubDecl);
  }
}

void ODRHash::AddDecl(const Decl *D) {
  assert(D && "Expecting non-null pointer.");
  D = D->getCanonicalDecl();

  const NamedDecl *ND = dyn_cast<NamedDecl>(D);
  AddBoolean(ND);
  if (!ND) {
    ID.AddInteger(D->getKind());
    return;
  }

  AddDeclarationName(ND->getDeclName());

  const auto *Specialization =
            dyn_cast<ClassTemplateSpecializationDecl>(D);
  AddBoolean(Specialization);
  if (Specialization) {
    const TemplateArgumentList &List = Specialization->getTemplateArgs();
    ID.AddInteger(List.size());
    for (const TemplateArgument &TA : List.asArray())
      AddTemplateArgument(TA);
  }
}

namespace {
// Process a Type pointer.  Add* methods call back into ODRHash while Visit*
// methods process the relevant parts of the Type.
class ODRTypeVisitor : public TypeVisitor<ODRTypeVisitor> {
  typedef TypeVisitor<ODRTypeVisitor> Inherited;
  llvm::FoldingSetNodeID &ID;
  ODRHash &Hash;

public:
  ODRTypeVisitor(llvm::FoldingSetNodeID &ID, ODRHash &Hash)
      : ID(ID), Hash(Hash) {}

  void AddStmt(Stmt *S) {
    Hash.AddBoolean(S);
    if (S) {
      Hash.AddStmt(S);
    }
  }

  void AddDecl(const Decl *D) {
    Hash.AddBoolean(D);
    if (D) {
      Hash.AddDecl(D);
    }
  }

  void AddQualType(QualType T) {
    Hash.AddQualType(T);
  }

  void AddType(const Type *T) {
    Hash.AddBoolean(T);
    if (T) {
      Hash.AddType(T);
    }
  }

  void AddNestedNameSpecifier(const NestedNameSpecifier *NNS) {
    Hash.AddBoolean(NNS);
    if (NNS) {
      Hash.AddNestedNameSpecifier(NNS);
    }
  }

  void AddIdentifierInfo(const IdentifierInfo *II) {
    Hash.AddBoolean(II);
    if (II) {
      Hash.AddIdentifierInfo(II);
    }
  }

  void VisitQualifiers(Qualifiers Quals) {
    ID.AddInteger(Quals.getAsOpaqueValue());
  }

  // Return the RecordType if the typedef only strips away a keyword.
  // Otherwise, return the original type.
  static const Type *RemoveTypedef(const Type *T) {
    const auto *TypedefT = dyn_cast<TypedefType>(T);
    if (!TypedefT) {
      return T;
    }

    const TypedefNameDecl *D = TypedefT->getDecl();
    QualType UnderlyingType = D->getUnderlyingType();

    if (UnderlyingType.hasLocalQualifiers()) {
      return T;
    }

    const auto *ElaboratedT = dyn_cast<ElaboratedType>(UnderlyingType);
    if (!ElaboratedT) {
      return T;
    }

    if (ElaboratedT->getQualifier() != nullptr) {
      return T;
    }

    QualType NamedType = ElaboratedT->getNamedType();
    if (NamedType.hasLocalQualifiers()) {
      return T;
    }

    const auto *RecordT = dyn_cast<RecordType>(NamedType);
    if (!RecordT) {
      return T;
    }

    const IdentifierInfo *TypedefII = TypedefT->getDecl()->getIdentifier();
    const IdentifierInfo *RecordII = RecordT->getDecl()->getIdentifier();
    if (!TypedefII || !RecordII ||
        TypedefII->getName() != RecordII->getName()) {
      return T;
    }

    return RecordT;
  }

  void Visit(const Type *T) {
    T = RemoveTypedef(T);
    ID.AddInteger(T->getTypeClass());
    Inherited::Visit(T);
  }

  void VisitType(const Type *T) {}

  void VisitAdjustedType(const AdjustedType *T) {
    AddQualType(T->getOriginalType());

    VisitType(T);
  }

  void VisitDecayedType(const DecayedType *T) {
    // getDecayedType and getPointeeType are derived from getAdjustedType
    // and don't need to be separately processed.
    VisitAdjustedType(T);
  }

  void VisitArrayType(const ArrayType *T) {
    AddQualType(T->getElementType());
    ID.AddInteger(llvm::to_underlying(T->getSizeModifier()));
    VisitQualifiers(T->getIndexTypeQualifiers());
    VisitType(T);
  }
  void VisitConstantArrayType(const ConstantArrayType *T) {
    T->getSize().Profile(ID);
    VisitArrayType(T);
  }

  void VisitArrayParameterType(const ArrayParameterType *T) {
    VisitConstantArrayType(T);
  }

  void VisitDependentSizedArrayType(const DependentSizedArrayType *T) {
    AddStmt(T->getSizeExpr());
    VisitArrayType(T);
  }

  void VisitIncompleteArrayType(const IncompleteArrayType *T) {
    VisitArrayType(T);
  }

  void VisitVariableArrayType(const VariableArrayType *T) {
    AddStmt(T->getSizeExpr());
    VisitArrayType(T);
  }

  void VisitAttributedType(const AttributedType *T) {
    ID.AddInteger(T->getAttrKind());
    AddQualType(T->getModifiedType());

    VisitType(T);
  }

  void VisitBlockPointerType(const BlockPointerType *T) {
    AddQualType(T->getPointeeType());
    VisitType(T);
  }

  void VisitBuiltinType(const BuiltinType *T) {
    ID.AddInteger(T->getKind());
    VisitType(T);
  }

  void VisitComplexType(const ComplexType *T) {
    AddQualType(T->getElementType());
    VisitType(T);
  }

  void VisitDecltypeType(const DecltypeType *T) {
    AddStmt(T->getUnderlyingExpr());
    VisitType(T);
  }

  void VisitDependentDecltypeType(const DependentDecltypeType *T) {
    VisitDecltypeType(T);
  }

  void VisitDeducedType(const DeducedType *T) {
    AddQualType(T->getDeducedType());
    VisitType(T);
  }

  void VisitAutoType(const AutoType *T) {
    ID.AddInteger((unsigned)T->getKeyword());
    ID.AddInteger(T->isConstrained());
    if (T->isConstrained()) {
      AddDecl(T->getTypeConstraintConcept());
      ID.AddInteger(T->getTypeConstraintArguments().size());
      for (const auto &TA : T->getTypeConstraintArguments())
        Hash.AddTemplateArgument(TA);
    }
    VisitDeducedType(T);
  }

  void VisitDeducedTemplateSpecializationType(
      const DeducedTemplateSpecializationType *T) {
    Hash.AddTemplateName(T->getTemplateName());
    VisitDeducedType(T);
  }

  void VisitDependentAddressSpaceType(const DependentAddressSpaceType *T) {
    AddQualType(T->getPointeeType());
    AddStmt(T->getAddrSpaceExpr());
    VisitType(T);
  }

  void VisitDependentSizedExtVectorType(const DependentSizedExtVectorType *T) {
    AddQualType(T->getElementType());
    AddStmt(T->getSizeExpr());
    VisitType(T);
  }

  void VisitFunctionType(const FunctionType *T) {
    AddQualType(T->getReturnType());
    T->getExtInfo().Profile(ID);
    Hash.AddBoolean(T->isConst());
    Hash.AddBoolean(T->isVolatile());
    Hash.AddBoolean(T->isRestrict());
    VisitType(T);
  }

  void VisitFunctionNoProtoType(const FunctionNoProtoType *T) {
    VisitFunctionType(T);
  }

  void VisitFunctionProtoType(const FunctionProtoType *T) {
    ID.AddInteger(T->getNumParams());
    for (auto ParamType : T->getParamTypes())
      AddQualType(ParamType);

    VisitFunctionType(T);
  }

  void VisitInjectedClassNameType(const InjectedClassNameType *T) {
    AddDecl(T->getDecl());
    VisitType(T);
  }

  void VisitMemberPointerType(const MemberPointerType *T) {
    AddQualType(T->getPointeeType());
    AddType(T->getClass());
    VisitType(T);
  }

  void VisitObjCObjectPointerType(const ObjCObjectPointerType *T) {
    AddQualType(T->getPointeeType());
    VisitType(T);
  }

  void VisitObjCObjectType(const ObjCObjectType *T) {
    AddDecl(T->getInterface());

    auto TypeArgs = T->getTypeArgsAsWritten();
    ID.AddInteger(TypeArgs.size());
    for (auto Arg : TypeArgs) {
      AddQualType(Arg);
    }

    auto Protocols = T->getProtocols();
    ID.AddInteger(Protocols.size());
    for (auto *Protocol : Protocols) {
      AddDecl(Protocol);
    }

    Hash.AddBoolean(T->isKindOfType());

    VisitType(T);
  }

  void VisitObjCInterfaceType(const ObjCInterfaceType *T) {
    // This type is handled by the parent type ObjCObjectType.
    VisitObjCObjectType(T);
  }

  void VisitObjCTypeParamType(const ObjCTypeParamType *T) {
    AddDecl(T->getDecl());
    auto Protocols = T->getProtocols();
    ID.AddInteger(Protocols.size());
    for (auto *Protocol : Protocols) {
      AddDecl(Protocol);
    }

    VisitType(T);
  }

  void VisitPackExpansionType(const PackExpansionType *T) {
    AddQualType(T->getPattern());
    VisitType(T);
  }

  void VisitParenType(const ParenType *T) {
    AddQualType(T->getInnerType());
    VisitType(T);
  }

  void VisitPipeType(const PipeType *T) {
    AddQualType(T->getElementType());
    Hash.AddBoolean(T->isReadOnly());
    VisitType(T);
  }

  void VisitPointerType(const PointerType *T) {
    AddQualType(T->getPointeeType());
    VisitType(T);
  }

  void VisitReferenceType(const ReferenceType *T) {
    AddQualType(T->getPointeeTypeAsWritten());
    VisitType(T);
  }

  void VisitLValueReferenceType(const LValueReferenceType *T) {
    VisitReferenceType(T);
  }

  void VisitRValueReferenceType(const RValueReferenceType *T) {
    VisitReferenceType(T);
  }

  void
  VisitSubstTemplateTypeParmPackType(const SubstTemplateTypeParmPackType *T) {
    AddDecl(T->getAssociatedDecl());
    Hash.AddTemplateArgument(T->getArgumentPack());
    VisitType(T);
  }

  void VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *T) {
    AddDecl(T->getAssociatedDecl());
    AddQualType(T->getReplacementType());
    VisitType(T);
  }

  void VisitTagType(const TagType *T) {
    AddDecl(T->getDecl());
    VisitType(T);
  }

  void VisitRecordType(const RecordType *T) { VisitTagType(T); }
  void VisitEnumType(const EnumType *T) { VisitTagType(T); }

  void VisitTemplateSpecializationType(const TemplateSpecializationType *T) {
    ID.AddInteger(T->template_arguments().size());
    for (const auto &TA : T->template_arguments()) {
      Hash.AddTemplateArgument(TA);
    }
    Hash.AddTemplateName(T->getTemplateName());
    VisitType(T);
  }

  void VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
    ID.AddInteger(T->getDepth());
    ID.AddInteger(T->getIndex());
    Hash.AddBoolean(T->isParameterPack());
    AddDecl(T->getDecl());
  }

  void VisitTypedefType(const TypedefType *T) {
    AddDecl(T->getDecl());
    VisitType(T);
  }

  void VisitTypeOfExprType(const TypeOfExprType *T) {
    AddStmt(T->getUnderlyingExpr());
    Hash.AddBoolean(T->isSugared());

    VisitType(T);
  }
  void VisitTypeOfType(const TypeOfType *T) {
    AddQualType(T->getUnmodifiedType());
    VisitType(T);
  }

  void VisitTypeWithKeyword(const TypeWithKeyword *T) {
    ID.AddInteger(llvm::to_underlying(T->getKeyword()));
    VisitType(T);
  };

  void VisitDependentNameType(const DependentNameType *T) {
    AddNestedNameSpecifier(T->getQualifier());
    AddIdentifierInfo(T->getIdentifier());
    VisitTypeWithKeyword(T);
  }

  void VisitDependentTemplateSpecializationType(
      const DependentTemplateSpecializationType *T) {
    AddIdentifierInfo(T->getIdentifier());
    AddNestedNameSpecifier(T->getQualifier());
    ID.AddInteger(T->template_arguments().size());
    for (const auto &TA : T->template_arguments()) {
      Hash.AddTemplateArgument(TA);
    }
    VisitTypeWithKeyword(T);
  }

  void VisitElaboratedType(const ElaboratedType *T) {
    AddNestedNameSpecifier(T->getQualifier());
    AddQualType(T->getNamedType());
    VisitTypeWithKeyword(T);
  }

  void VisitUnaryTransformType(const UnaryTransformType *T) {
    AddQualType(T->getUnderlyingType());
    AddQualType(T->getBaseType());
    VisitType(T);
  }

  void VisitUnresolvedUsingType(const UnresolvedUsingType *T) {
    AddDecl(T->getDecl());
    VisitType(T);
  }

  void VisitVectorType(const VectorType *T) {
    AddQualType(T->getElementType());
    ID.AddInteger(T->getNumElements());
    ID.AddInteger(llvm::to_underlying(T->getVectorKind()));
    VisitType(T);
  }

  void VisitExtVectorType(const ExtVectorType * T) {
    VisitVectorType(T);
  }
};
} // namespace

void ODRHash::AddType(const Type *T) {
  assert(T && "Expecting non-null pointer.");
  ODRTypeVisitor(ID, *this).Visit(T);
}

void ODRHash::AddQualType(QualType T) {
  AddBoolean(T.isNull());
  if (T.isNull())
    return;
  SplitQualType split = T.split();
  ID.AddInteger(split.Quals.getAsOpaqueValue());
  AddType(split.Ty);
}

void ODRHash::AddBoolean(bool Value) {
  Bools.push_back(Value);
}

void ODRHash::AddStructuralValue(const APValue &Value) {
  ID.AddInteger(Value.getKind());

  // 'APValue::Profile' uses pointer values to make hash for LValue and
  // MemberPointer, but they differ from one compiler invocation to another.
  // So, handle them explicitly here.

  switch (Value.getKind()) {
  case APValue::LValue: {
    const APValue::LValueBase &Base = Value.getLValueBase();
    if (!Base) {
      ID.AddInteger(Value.getLValueOffset().getQuantity());
      break;
    }

    assert(Base.is<const ValueDecl *>());
    AddDecl(Base.get<const ValueDecl *>());
    ID.AddInteger(Value.getLValueOffset().getQuantity());

    bool OnePastTheEnd = Value.isLValueOnePastTheEnd();
    if (Value.hasLValuePath()) {
      QualType TypeSoFar = Base.getType();
      for (APValue::LValuePathEntry E : Value.getLValuePath()) {
        if (const auto *AT = TypeSoFar->getAsArrayTypeUnsafe()) {
          if (const auto *CAT = dyn_cast<ConstantArrayType>(AT))
            OnePastTheEnd |= CAT->getSize() == E.getAsArrayIndex();
          TypeSoFar = AT->getElementType();
        } else {
          const Decl *D = E.getAsBaseOrMember().getPointer();
          if (const auto *FD = dyn_cast<FieldDecl>(D)) {
            if (FD->getParent()->isUnion())
              ID.AddInteger(FD->getFieldIndex());
            TypeSoFar = FD->getType();
          } else {
            TypeSoFar =
                D->getASTContext().getRecordType(cast<CXXRecordDecl>(D));
          }
        }
      }
    }
    unsigned Val = 0;
    if (Value.isNullPointer())
      Val |= 1 << 0;
    if (OnePastTheEnd)
      Val |= 1 << 1;
    if (Value.hasLValuePath())
      Val |= 1 << 2;
    ID.AddInteger(Val);
    break;
  }
  case APValue::MemberPointer: {
    const ValueDecl *D = Value.getMemberPointerDecl();
    assert(D);
    AddDecl(D);
    ID.AddInteger(
        D->getASTContext().getMemberPointerPathAdjustment(Value).getQuantity());
    break;
  }
  default:
    Value.Profile(ID);
  }
}
