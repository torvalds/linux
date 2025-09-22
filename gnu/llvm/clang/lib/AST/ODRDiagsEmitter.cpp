//===-- ODRDiagsEmitter.cpp - Diagnostics for ODR mismatches ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ODRDiagsEmitter.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ODRHash.h"
#include "clang/Basic/DiagnosticAST.h"
#include "clang/Basic/Module.h"

using namespace clang;

static unsigned computeODRHash(QualType Ty) {
  ODRHash Hasher;
  Hasher.AddQualType(Ty);
  return Hasher.CalculateHash();
}

static unsigned computeODRHash(const Stmt *S) {
  ODRHash Hasher;
  Hasher.AddStmt(S);
  return Hasher.CalculateHash();
}

static unsigned computeODRHash(const Decl *D) {
  assert(D);
  ODRHash Hasher;
  Hasher.AddSubDecl(D);
  return Hasher.CalculateHash();
}

static unsigned computeODRHash(const TemplateArgument &TA) {
  ODRHash Hasher;
  Hasher.AddTemplateArgument(TA);
  return Hasher.CalculateHash();
}

std::string ODRDiagsEmitter::getOwningModuleNameForDiagnostic(const Decl *D) {
  // If we know the owning module, use it.
  if (Module *M = D->getImportedOwningModule())
    return M->getFullModuleName();

  // Not from a module.
  return {};
}

template <typename MethodT>
static bool diagnoseSubMismatchMethodParameters(DiagnosticsEngine &Diags,
                                                const NamedDecl *FirstContainer,
                                                StringRef FirstModule,
                                                StringRef SecondModule,
                                                const MethodT *FirstMethod,
                                                const MethodT *SecondMethod) {
  enum DiagMethodType {
    DiagMethod,
    DiagConstructor,
    DiagDestructor,
  };
  auto GetDiagMethodType = [](const NamedDecl *D) {
    if (isa<CXXConstructorDecl>(D))
      return DiagConstructor;
    if (isa<CXXDestructorDecl>(D))
      return DiagDestructor;
    return DiagMethod;
  };

  enum ODRMethodParametersDifference {
    NumberParameters,
    ParameterType,
    ParameterName,
  };
  auto DiagError = [&Diags, &GetDiagMethodType, FirstContainer, FirstModule,
                    FirstMethod](ODRMethodParametersDifference DiffType) {
    DeclarationName FirstName = FirstMethod->getDeclName();
    DiagMethodType FirstMethodType = GetDiagMethodType(FirstMethod);
    return Diags.Report(FirstMethod->getLocation(),
                        diag::err_module_odr_violation_method_params)
           << FirstContainer << FirstModule.empty() << FirstModule
           << FirstMethod->getSourceRange() << DiffType << FirstMethodType
           << FirstName;
  };
  auto DiagNote = [&Diags, &GetDiagMethodType, SecondModule,
                   SecondMethod](ODRMethodParametersDifference DiffType) {
    DeclarationName SecondName = SecondMethod->getDeclName();
    DiagMethodType SecondMethodType = GetDiagMethodType(SecondMethod);
    return Diags.Report(SecondMethod->getLocation(),
                        diag::note_module_odr_violation_method_params)
           << SecondModule.empty() << SecondModule
           << SecondMethod->getSourceRange() << DiffType << SecondMethodType
           << SecondName;
  };

  const unsigned FirstNumParameters = FirstMethod->param_size();
  const unsigned SecondNumParameters = SecondMethod->param_size();
  if (FirstNumParameters != SecondNumParameters) {
    DiagError(NumberParameters) << FirstNumParameters;
    DiagNote(NumberParameters) << SecondNumParameters;
    return true;
  }

  for (unsigned I = 0; I < FirstNumParameters; ++I) {
    const ParmVarDecl *FirstParam = FirstMethod->getParamDecl(I);
    const ParmVarDecl *SecondParam = SecondMethod->getParamDecl(I);

    QualType FirstParamType = FirstParam->getType();
    QualType SecondParamType = SecondParam->getType();
    if (FirstParamType != SecondParamType &&
        computeODRHash(FirstParamType) != computeODRHash(SecondParamType)) {
      if (const DecayedType *ParamDecayedType =
              FirstParamType->getAs<DecayedType>()) {
        DiagError(ParameterType) << (I + 1) << FirstParamType << true
                                 << ParamDecayedType->getOriginalType();
      } else {
        DiagError(ParameterType) << (I + 1) << FirstParamType << false;
      }

      if (const DecayedType *ParamDecayedType =
              SecondParamType->getAs<DecayedType>()) {
        DiagNote(ParameterType) << (I + 1) << SecondParamType << true
                                << ParamDecayedType->getOriginalType();
      } else {
        DiagNote(ParameterType) << (I + 1) << SecondParamType << false;
      }
      return true;
    }

    DeclarationName FirstParamName = FirstParam->getDeclName();
    DeclarationName SecondParamName = SecondParam->getDeclName();
    if (FirstParamName != SecondParamName) {
      DiagError(ParameterName) << (I + 1) << FirstParamName;
      DiagNote(ParameterName) << (I + 1) << SecondParamName;
      return true;
    }
  }

  return false;
}

bool ODRDiagsEmitter::diagnoseSubMismatchField(
    const NamedDecl *FirstRecord, StringRef FirstModule, StringRef SecondModule,
    const FieldDecl *FirstField, const FieldDecl *SecondField) const {
  enum ODRFieldDifference {
    FieldName,
    FieldTypeName,
    FieldSingleBitField,
    FieldDifferentWidthBitField,
    FieldSingleMutable,
    FieldSingleInitializer,
    FieldDifferentInitializers,
  };

  auto DiagError = [FirstRecord, FirstField, FirstModule,
                    this](ODRFieldDifference DiffType) {
    return Diag(FirstField->getLocation(), diag::err_module_odr_violation_field)
           << FirstRecord << FirstModule.empty() << FirstModule
           << FirstField->getSourceRange() << DiffType;
  };
  auto DiagNote = [SecondField, SecondModule,
                   this](ODRFieldDifference DiffType) {
    return Diag(SecondField->getLocation(),
                diag::note_module_odr_violation_field)
           << SecondModule.empty() << SecondModule << SecondField->getSourceRange() << DiffType;
  };

  IdentifierInfo *FirstII = FirstField->getIdentifier();
  IdentifierInfo *SecondII = SecondField->getIdentifier();
  if (FirstII->getName() != SecondII->getName()) {
    DiagError(FieldName) << FirstII;
    DiagNote(FieldName) << SecondII;
    return true;
  }

  QualType FirstType = FirstField->getType();
  QualType SecondType = SecondField->getType();
  if (computeODRHash(FirstType) != computeODRHash(SecondType)) {
    DiagError(FieldTypeName) << FirstII << FirstType;
    DiagNote(FieldTypeName) << SecondII << SecondType;
    return true;
  }

  assert(Context.hasSameType(FirstField->getType(), SecondField->getType()));
  (void)Context;

  const bool IsFirstBitField = FirstField->isBitField();
  const bool IsSecondBitField = SecondField->isBitField();
  if (IsFirstBitField != IsSecondBitField) {
    DiagError(FieldSingleBitField) << FirstII << IsFirstBitField;
    DiagNote(FieldSingleBitField) << SecondII << IsSecondBitField;
    return true;
  }

  if (IsFirstBitField && IsSecondBitField) {
    unsigned FirstBitWidthHash = computeODRHash(FirstField->getBitWidth());
    unsigned SecondBitWidthHash = computeODRHash(SecondField->getBitWidth());
    if (FirstBitWidthHash != SecondBitWidthHash) {
      DiagError(FieldDifferentWidthBitField)
          << FirstII << FirstField->getBitWidth()->getSourceRange();
      DiagNote(FieldDifferentWidthBitField)
          << SecondII << SecondField->getBitWidth()->getSourceRange();
      return true;
    }
  }

  if (!LangOpts.CPlusPlus)
    return false;

  const bool IsFirstMutable = FirstField->isMutable();
  const bool IsSecondMutable = SecondField->isMutable();
  if (IsFirstMutable != IsSecondMutable) {
    DiagError(FieldSingleMutable) << FirstII << IsFirstMutable;
    DiagNote(FieldSingleMutable) << SecondII << IsSecondMutable;
    return true;
  }

  const Expr *FirstInitializer = FirstField->getInClassInitializer();
  const Expr *SecondInitializer = SecondField->getInClassInitializer();
  if ((!FirstInitializer && SecondInitializer) ||
      (FirstInitializer && !SecondInitializer)) {
    DiagError(FieldSingleInitializer)
        << FirstII << (FirstInitializer != nullptr);
    DiagNote(FieldSingleInitializer)
        << SecondII << (SecondInitializer != nullptr);
    return true;
  }

  if (FirstInitializer && SecondInitializer) {
    unsigned FirstInitHash = computeODRHash(FirstInitializer);
    unsigned SecondInitHash = computeODRHash(SecondInitializer);
    if (FirstInitHash != SecondInitHash) {
      DiagError(FieldDifferentInitializers)
          << FirstII << FirstInitializer->getSourceRange();
      DiagNote(FieldDifferentInitializers)
          << SecondII << SecondInitializer->getSourceRange();
      return true;
    }
  }

  return false;
}

bool ODRDiagsEmitter::diagnoseSubMismatchTypedef(
    const NamedDecl *FirstRecord, StringRef FirstModule, StringRef SecondModule,
    const TypedefNameDecl *FirstTD, const TypedefNameDecl *SecondTD,
    bool IsTypeAlias) const {
  enum ODRTypedefDifference {
    TypedefName,
    TypedefType,
  };

  auto DiagError = [FirstRecord, FirstTD, FirstModule,
                    this](ODRTypedefDifference DiffType) {
    return Diag(FirstTD->getLocation(), diag::err_module_odr_violation_typedef)
           << FirstRecord << FirstModule.empty() << FirstModule
           << FirstTD->getSourceRange() << DiffType;
  };
  auto DiagNote = [SecondTD, SecondModule,
                   this](ODRTypedefDifference DiffType) {
    return Diag(SecondTD->getLocation(),
                diag::note_module_odr_violation_typedef)
           << SecondModule << SecondTD->getSourceRange() << DiffType;
  };

  DeclarationName FirstName = FirstTD->getDeclName();
  DeclarationName SecondName = SecondTD->getDeclName();
  if (FirstName != SecondName) {
    DiagError(TypedefName) << IsTypeAlias << FirstName;
    DiagNote(TypedefName) << IsTypeAlias << SecondName;
    return true;
  }

  QualType FirstType = FirstTD->getUnderlyingType();
  QualType SecondType = SecondTD->getUnderlyingType();
  if (computeODRHash(FirstType) != computeODRHash(SecondType)) {
    DiagError(TypedefType) << IsTypeAlias << FirstName << FirstType;
    DiagNote(TypedefType) << IsTypeAlias << SecondName << SecondType;
    return true;
  }
  return false;
}

bool ODRDiagsEmitter::diagnoseSubMismatchVar(const NamedDecl *FirstRecord,
                                             StringRef FirstModule,
                                             StringRef SecondModule,
                                             const VarDecl *FirstVD,
                                             const VarDecl *SecondVD) const {
  enum ODRVarDifference {
    VarName,
    VarType,
    VarSingleInitializer,
    VarDifferentInitializer,
    VarConstexpr,
  };

  auto DiagError = [FirstRecord, FirstVD, FirstModule,
                    this](ODRVarDifference DiffType) {
    return Diag(FirstVD->getLocation(), diag::err_module_odr_violation_variable)
           << FirstRecord << FirstModule.empty() << FirstModule
           << FirstVD->getSourceRange() << DiffType;
  };
  auto DiagNote = [SecondVD, SecondModule, this](ODRVarDifference DiffType) {
    return Diag(SecondVD->getLocation(),
                diag::note_module_odr_violation_variable)
           << SecondModule << SecondVD->getSourceRange() << DiffType;
  };

  DeclarationName FirstName = FirstVD->getDeclName();
  DeclarationName SecondName = SecondVD->getDeclName();
  if (FirstName != SecondName) {
    DiagError(VarName) << FirstName;
    DiagNote(VarName) << SecondName;
    return true;
  }

  QualType FirstType = FirstVD->getType();
  QualType SecondType = SecondVD->getType();
  if (computeODRHash(FirstType) != computeODRHash(SecondType)) {
    DiagError(VarType) << FirstName << FirstType;
    DiagNote(VarType) << SecondName << SecondType;
    return true;
  }

  if (!LangOpts.CPlusPlus)
    return false;

  const Expr *FirstInit = FirstVD->getInit();
  const Expr *SecondInit = SecondVD->getInit();
  if ((FirstInit == nullptr) != (SecondInit == nullptr)) {
    DiagError(VarSingleInitializer)
        << FirstName << (FirstInit == nullptr)
        << (FirstInit ? FirstInit->getSourceRange() : SourceRange());
    DiagNote(VarSingleInitializer)
        << SecondName << (SecondInit == nullptr)
        << (SecondInit ? SecondInit->getSourceRange() : SourceRange());
    return true;
  }

  if (FirstInit && SecondInit &&
      computeODRHash(FirstInit) != computeODRHash(SecondInit)) {
    DiagError(VarDifferentInitializer)
        << FirstName << FirstInit->getSourceRange();
    DiagNote(VarDifferentInitializer)
        << SecondName << SecondInit->getSourceRange();
    return true;
  }

  const bool FirstIsConstexpr = FirstVD->isConstexpr();
  const bool SecondIsConstexpr = SecondVD->isConstexpr();
  if (FirstIsConstexpr != SecondIsConstexpr) {
    DiagError(VarConstexpr) << FirstName << FirstIsConstexpr;
    DiagNote(VarConstexpr) << SecondName << SecondIsConstexpr;
    return true;
  }
  return false;
}

bool ODRDiagsEmitter::diagnoseSubMismatchProtocols(
    const ObjCProtocolList &FirstProtocols,
    const ObjCContainerDecl *FirstContainer, StringRef FirstModule,
    const ObjCProtocolList &SecondProtocols,
    const ObjCContainerDecl *SecondContainer, StringRef SecondModule) const {
  // Keep in sync with err_module_odr_violation_referenced_protocols.
  enum ODRReferencedProtocolDifference {
    NumProtocols,
    ProtocolType,
  };
  auto DiagRefProtocolError = [FirstContainer, FirstModule,
                               this](SourceLocation Loc, SourceRange Range,
                                     ODRReferencedProtocolDifference DiffType) {
    return Diag(Loc, diag::err_module_odr_violation_referenced_protocols)
           << FirstContainer << FirstModule.empty() << FirstModule << Range
           << DiffType;
  };
  auto DiagRefProtocolNote = [SecondModule,
                              this](SourceLocation Loc, SourceRange Range,
                                    ODRReferencedProtocolDifference DiffType) {
    return Diag(Loc, diag::note_module_odr_violation_referenced_protocols)
           << SecondModule.empty() << SecondModule << Range << DiffType;
  };
  auto GetProtoListSourceRange = [](const ObjCProtocolList &PL) {
    if (PL.empty())
      return SourceRange();
    return SourceRange(*PL.loc_begin(), *std::prev(PL.loc_end()));
  };

  if (FirstProtocols.size() != SecondProtocols.size()) {
    DiagRefProtocolError(FirstContainer->getLocation(),
                         GetProtoListSourceRange(FirstProtocols), NumProtocols)
        << FirstProtocols.size();
    DiagRefProtocolNote(SecondContainer->getLocation(),
                        GetProtoListSourceRange(SecondProtocols), NumProtocols)
        << SecondProtocols.size();
    return true;
  }

  for (unsigned I = 0, E = FirstProtocols.size(); I != E; ++I) {
    const ObjCProtocolDecl *FirstProtocol = FirstProtocols[I];
    const ObjCProtocolDecl *SecondProtocol = SecondProtocols[I];
    DeclarationName FirstProtocolName = FirstProtocol->getDeclName();
    DeclarationName SecondProtocolName = SecondProtocol->getDeclName();
    if (FirstProtocolName != SecondProtocolName) {
      SourceLocation FirstLoc = *(FirstProtocols.loc_begin() + I);
      SourceLocation SecondLoc = *(SecondProtocols.loc_begin() + I);
      SourceRange EmptyRange;
      DiagRefProtocolError(FirstLoc, EmptyRange, ProtocolType)
          << (I + 1) << FirstProtocolName;
      DiagRefProtocolNote(SecondLoc, EmptyRange, ProtocolType)
          << (I + 1) << SecondProtocolName;
      return true;
    }
  }

  return false;
}

bool ODRDiagsEmitter::diagnoseSubMismatchObjCMethod(
    const NamedDecl *FirstObjCContainer, StringRef FirstModule,
    StringRef SecondModule, const ObjCMethodDecl *FirstMethod,
    const ObjCMethodDecl *SecondMethod) const {
  enum ODRMethodDifference {
    ReturnType,
    InstanceOrClass,
    ControlLevel, // optional/required
    DesignatedInitializer,
    Directness,
    Name,
  };

  auto DiagError = [FirstObjCContainer, FirstModule, FirstMethod,
                    this](ODRMethodDifference DiffType) {
    return Diag(FirstMethod->getLocation(),
                diag::err_module_odr_violation_objc_method)
           << FirstObjCContainer << FirstModule.empty() << FirstModule
           << FirstMethod->getSourceRange() << DiffType;
  };
  auto DiagNote = [SecondModule, SecondMethod,
                   this](ODRMethodDifference DiffType) {
    return Diag(SecondMethod->getLocation(),
                diag::note_module_odr_violation_objc_method)
           << SecondModule.empty() << SecondModule
           << SecondMethod->getSourceRange() << DiffType;
  };

  if (computeODRHash(FirstMethod->getReturnType()) !=
      computeODRHash(SecondMethod->getReturnType())) {
    DiagError(ReturnType) << FirstMethod << FirstMethod->getReturnType();
    DiagNote(ReturnType) << SecondMethod << SecondMethod->getReturnType();
    return true;
  }

  if (FirstMethod->isInstanceMethod() != SecondMethod->isInstanceMethod()) {
    DiagError(InstanceOrClass)
        << FirstMethod << FirstMethod->isInstanceMethod();
    DiagNote(InstanceOrClass)
        << SecondMethod << SecondMethod->isInstanceMethod();
    return true;
  }
  if (FirstMethod->getImplementationControl() !=
      SecondMethod->getImplementationControl()) {
    DiagError(ControlLevel)
        << llvm::to_underlying(FirstMethod->getImplementationControl());
    DiagNote(ControlLevel) << llvm::to_underlying(
        SecondMethod->getImplementationControl());
    return true;
  }
  if (FirstMethod->isThisDeclarationADesignatedInitializer() !=
      SecondMethod->isThisDeclarationADesignatedInitializer()) {
    DiagError(DesignatedInitializer)
        << FirstMethod
        << FirstMethod->isThisDeclarationADesignatedInitializer();
    DiagNote(DesignatedInitializer)
        << SecondMethod
        << SecondMethod->isThisDeclarationADesignatedInitializer();
    return true;
  }
  if (FirstMethod->isDirectMethod() != SecondMethod->isDirectMethod()) {
    DiagError(Directness) << FirstMethod << FirstMethod->isDirectMethod();
    DiagNote(Directness) << SecondMethod << SecondMethod->isDirectMethod();
    return true;
  }
  if (diagnoseSubMismatchMethodParameters(Diags, FirstObjCContainer,
                                          FirstModule, SecondModule,
                                          FirstMethod, SecondMethod))
    return true;

  // Check method name *after* looking at the parameters otherwise we get a
  // less ideal diagnostics: a ObjCMethodName mismatch given that selectors
  // for different parameters are likely to be different.
  DeclarationName FirstName = FirstMethod->getDeclName();
  DeclarationName SecondName = SecondMethod->getDeclName();
  if (FirstName != SecondName) {
    DiagError(Name) << FirstName;
    DiagNote(Name) << SecondName;
    return true;
  }

  return false;
}

bool ODRDiagsEmitter::diagnoseSubMismatchObjCProperty(
    const NamedDecl *FirstObjCContainer, StringRef FirstModule,
    StringRef SecondModule, const ObjCPropertyDecl *FirstProp,
    const ObjCPropertyDecl *SecondProp) const {
  enum ODRPropertyDifference {
    Name,
    Type,
    ControlLevel, // optional/required
    Attribute,
  };

  auto DiagError = [FirstObjCContainer, FirstModule, FirstProp,
                    this](SourceLocation Loc, ODRPropertyDifference DiffType) {
    return Diag(Loc, diag::err_module_odr_violation_objc_property)
           << FirstObjCContainer << FirstModule.empty() << FirstModule
           << FirstProp->getSourceRange() << DiffType;
  };
  auto DiagNote = [SecondModule, SecondProp,
                   this](SourceLocation Loc, ODRPropertyDifference DiffType) {
    return Diag(Loc, diag::note_module_odr_violation_objc_property)
           << SecondModule.empty() << SecondModule
           << SecondProp->getSourceRange() << DiffType;
  };

  IdentifierInfo *FirstII = FirstProp->getIdentifier();
  IdentifierInfo *SecondII = SecondProp->getIdentifier();
  if (FirstII->getName() != SecondII->getName()) {
    DiagError(FirstProp->getLocation(), Name) << FirstII;
    DiagNote(SecondProp->getLocation(), Name) << SecondII;
    return true;
  }
  if (computeODRHash(FirstProp->getType()) !=
      computeODRHash(SecondProp->getType())) {
    DiagError(FirstProp->getLocation(), Type)
        << FirstII << FirstProp->getType();
    DiagNote(SecondProp->getLocation(), Type)
        << SecondII << SecondProp->getType();
    return true;
  }
  if (FirstProp->getPropertyImplementation() !=
      SecondProp->getPropertyImplementation()) {
    DiagError(FirstProp->getLocation(), ControlLevel)
        << FirstProp->getPropertyImplementation();
    DiagNote(SecondProp->getLocation(), ControlLevel)
        << SecondProp->getPropertyImplementation();
    return true;
  }

  // Go over the property attributes and stop at the first mismatch.
  unsigned FirstAttrs = (unsigned)FirstProp->getPropertyAttributes();
  unsigned SecondAttrs = (unsigned)SecondProp->getPropertyAttributes();
  if (FirstAttrs != SecondAttrs) {
    for (unsigned I = 0; I < NumObjCPropertyAttrsBits; ++I) {
      unsigned CheckedAttr = (1 << I);
      if ((FirstAttrs & CheckedAttr) == (SecondAttrs & CheckedAttr))
        continue;

      bool IsFirstWritten =
          (unsigned)FirstProp->getPropertyAttributesAsWritten() & CheckedAttr;
      bool IsSecondWritten =
          (unsigned)SecondProp->getPropertyAttributesAsWritten() & CheckedAttr;
      DiagError(IsFirstWritten ? FirstProp->getLParenLoc()
                               : FirstProp->getLocation(),
                Attribute)
          << FirstII << (I + 1) << IsFirstWritten;
      DiagNote(IsSecondWritten ? SecondProp->getLParenLoc()
                               : SecondProp->getLocation(),
               Attribute)
          << SecondII << (I + 1);
      return true;
    }
  }

  return false;
}

ODRDiagsEmitter::DiffResult
ODRDiagsEmitter::FindTypeDiffs(DeclHashes &FirstHashes,
                               DeclHashes &SecondHashes) {
  auto DifferenceSelector = [](const Decl *D) {
    assert(D && "valid Decl required");
    switch (D->getKind()) {
    default:
      return Other;
    case Decl::AccessSpec:
      switch (D->getAccess()) {
      case AS_public:
        return PublicSpecifer;
      case AS_private:
        return PrivateSpecifer;
      case AS_protected:
        return ProtectedSpecifer;
      case AS_none:
        break;
      }
      llvm_unreachable("Invalid access specifier");
    case Decl::StaticAssert:
      return StaticAssert;
    case Decl::Field:
      return Field;
    case Decl::CXXMethod:
    case Decl::CXXConstructor:
    case Decl::CXXDestructor:
      return CXXMethod;
    case Decl::TypeAlias:
      return TypeAlias;
    case Decl::Typedef:
      return TypeDef;
    case Decl::Var:
      return Var;
    case Decl::Friend:
      return Friend;
    case Decl::FunctionTemplate:
      return FunctionTemplate;
    case Decl::ObjCMethod:
      return ObjCMethod;
    case Decl::ObjCIvar:
      return ObjCIvar;
    case Decl::ObjCProperty:
      return ObjCProperty;
    }
  };

  DiffResult DR;
  auto FirstIt = FirstHashes.begin();
  auto SecondIt = SecondHashes.begin();
  while (FirstIt != FirstHashes.end() || SecondIt != SecondHashes.end()) {
    if (FirstIt != FirstHashes.end() && SecondIt != SecondHashes.end() &&
        FirstIt->second == SecondIt->second) {
      ++FirstIt;
      ++SecondIt;
      continue;
    }

    DR.FirstDecl = FirstIt == FirstHashes.end() ? nullptr : FirstIt->first;
    DR.SecondDecl = SecondIt == SecondHashes.end() ? nullptr : SecondIt->first;

    DR.FirstDiffType =
        DR.FirstDecl ? DifferenceSelector(DR.FirstDecl) : EndOfClass;
    DR.SecondDiffType =
        DR.SecondDecl ? DifferenceSelector(DR.SecondDecl) : EndOfClass;
    return DR;
  }
  return DR;
}

void ODRDiagsEmitter::diagnoseSubMismatchUnexpected(
    DiffResult &DR, const NamedDecl *FirstRecord, StringRef FirstModule,
    const NamedDecl *SecondRecord, StringRef SecondModule) const {
  Diag(FirstRecord->getLocation(),
       diag::err_module_odr_violation_different_definitions)
      << FirstRecord << FirstModule.empty() << FirstModule;

  if (DR.FirstDecl) {
    Diag(DR.FirstDecl->getLocation(), diag::note_first_module_difference)
        << FirstRecord << DR.FirstDecl->getSourceRange();
  }

  Diag(SecondRecord->getLocation(),
       diag::note_module_odr_violation_different_definitions)
      << SecondModule;

  if (DR.SecondDecl) {
    Diag(DR.SecondDecl->getLocation(), diag::note_second_module_difference)
        << DR.SecondDecl->getSourceRange();
  }
}

void ODRDiagsEmitter::diagnoseSubMismatchDifferentDeclKinds(
    DiffResult &DR, const NamedDecl *FirstRecord, StringRef FirstModule,
    const NamedDecl *SecondRecord, StringRef SecondModule) const {
  auto GetMismatchedDeclLoc = [](const NamedDecl *Container,
                                 ODRMismatchDecl DiffType, const Decl *D) {
    SourceLocation Loc;
    SourceRange Range;
    if (DiffType == EndOfClass) {
      if (auto *Tag = dyn_cast<TagDecl>(Container))
        Loc = Tag->getBraceRange().getEnd();
      else if (auto *IF = dyn_cast<ObjCInterfaceDecl>(Container))
        Loc = IF->getAtEndRange().getBegin();
      else
        Loc = Container->getEndLoc();
    } else {
      Loc = D->getLocation();
      Range = D->getSourceRange();
    }
    return std::make_pair(Loc, Range);
  };

  auto FirstDiagInfo =
      GetMismatchedDeclLoc(FirstRecord, DR.FirstDiffType, DR.FirstDecl);
  Diag(FirstDiagInfo.first, diag::err_module_odr_violation_mismatch_decl)
      << FirstRecord << FirstModule.empty() << FirstModule
      << FirstDiagInfo.second << DR.FirstDiffType;

  auto SecondDiagInfo =
      GetMismatchedDeclLoc(SecondRecord, DR.SecondDiffType, DR.SecondDecl);
  Diag(SecondDiagInfo.first, diag::note_module_odr_violation_mismatch_decl)
      << SecondModule.empty() << SecondModule << SecondDiagInfo.second
      << DR.SecondDiffType;
}

bool ODRDiagsEmitter::diagnoseMismatch(
    const CXXRecordDecl *FirstRecord, const CXXRecordDecl *SecondRecord,
    const struct CXXRecordDecl::DefinitionData *SecondDD) const {
  // Multiple different declarations got merged together; tell the user
  // where they came from.
  if (FirstRecord == SecondRecord)
    return false;

  std::string FirstModule = getOwningModuleNameForDiagnostic(FirstRecord);
  std::string SecondModule = getOwningModuleNameForDiagnostic(SecondRecord);

  const struct CXXRecordDecl::DefinitionData *FirstDD =
      FirstRecord->DefinitionData;
  assert(FirstDD && SecondDD && "Definitions without DefinitionData");

  // Diagnostics from DefinitionData are emitted here.
  if (FirstDD != SecondDD) {
    // Keep in sync with err_module_odr_violation_definition_data.
    enum ODRDefinitionDataDifference {
      NumBases,
      NumVBases,
      BaseType,
      BaseVirtual,
      BaseAccess,
    };
    auto DiagBaseError = [FirstRecord, &FirstModule,
                          this](SourceLocation Loc, SourceRange Range,
                                ODRDefinitionDataDifference DiffType) {
      return Diag(Loc, diag::err_module_odr_violation_definition_data)
             << FirstRecord << FirstModule.empty() << FirstModule << Range
             << DiffType;
    };
    auto DiagBaseNote = [&SecondModule,
                         this](SourceLocation Loc, SourceRange Range,
                               ODRDefinitionDataDifference DiffType) {
      return Diag(Loc, diag::note_module_odr_violation_definition_data)
             << SecondModule << Range << DiffType;
    };
    auto GetSourceRange = [](const struct CXXRecordDecl::DefinitionData *DD) {
      unsigned NumBases = DD->NumBases;
      if (NumBases == 0)
        return SourceRange();
      ArrayRef<CXXBaseSpecifier> bases = DD->bases();
      return SourceRange(bases[0].getBeginLoc(),
                         bases[NumBases - 1].getEndLoc());
    };

    unsigned FirstNumBases = FirstDD->NumBases;
    unsigned FirstNumVBases = FirstDD->NumVBases;
    unsigned SecondNumBases = SecondDD->NumBases;
    unsigned SecondNumVBases = SecondDD->NumVBases;
    if (FirstNumBases != SecondNumBases) {
      DiagBaseError(FirstRecord->getLocation(), GetSourceRange(FirstDD),
                    NumBases)
          << FirstNumBases;
      DiagBaseNote(SecondRecord->getLocation(), GetSourceRange(SecondDD),
                   NumBases)
          << SecondNumBases;
      return true;
    }

    if (FirstNumVBases != SecondNumVBases) {
      DiagBaseError(FirstRecord->getLocation(), GetSourceRange(FirstDD),
                    NumVBases)
          << FirstNumVBases;
      DiagBaseNote(SecondRecord->getLocation(), GetSourceRange(SecondDD),
                   NumVBases)
          << SecondNumVBases;
      return true;
    }

    ArrayRef<CXXBaseSpecifier> FirstBases = FirstDD->bases();
    ArrayRef<CXXBaseSpecifier> SecondBases = SecondDD->bases();
    for (unsigned I = 0; I < FirstNumBases; ++I) {
      const CXXBaseSpecifier FirstBase = FirstBases[I];
      const CXXBaseSpecifier SecondBase = SecondBases[I];
      if (computeODRHash(FirstBase.getType()) !=
          computeODRHash(SecondBase.getType())) {
        DiagBaseError(FirstRecord->getLocation(), FirstBase.getSourceRange(),
                      BaseType)
            << (I + 1) << FirstBase.getType();
        DiagBaseNote(SecondRecord->getLocation(), SecondBase.getSourceRange(),
                     BaseType)
            << (I + 1) << SecondBase.getType();
        return true;
      }

      if (FirstBase.isVirtual() != SecondBase.isVirtual()) {
        DiagBaseError(FirstRecord->getLocation(), FirstBase.getSourceRange(),
                      BaseVirtual)
            << (I + 1) << FirstBase.isVirtual() << FirstBase.getType();
        DiagBaseNote(SecondRecord->getLocation(), SecondBase.getSourceRange(),
                     BaseVirtual)
            << (I + 1) << SecondBase.isVirtual() << SecondBase.getType();
        return true;
      }

      if (FirstBase.getAccessSpecifierAsWritten() !=
          SecondBase.getAccessSpecifierAsWritten()) {
        DiagBaseError(FirstRecord->getLocation(), FirstBase.getSourceRange(),
                      BaseAccess)
            << (I + 1) << FirstBase.getType()
            << (int)FirstBase.getAccessSpecifierAsWritten();
        DiagBaseNote(SecondRecord->getLocation(), SecondBase.getSourceRange(),
                     BaseAccess)
            << (I + 1) << SecondBase.getType()
            << (int)SecondBase.getAccessSpecifierAsWritten();
        return true;
      }
    }
  }

  const ClassTemplateDecl *FirstTemplate =
      FirstRecord->getDescribedClassTemplate();
  const ClassTemplateDecl *SecondTemplate =
      SecondRecord->getDescribedClassTemplate();

  assert(!FirstTemplate == !SecondTemplate &&
         "Both pointers should be null or non-null");

  if (FirstTemplate && SecondTemplate) {
    ArrayRef<const NamedDecl *> FirstTemplateParams =
        FirstTemplate->getTemplateParameters()->asArray();
    ArrayRef<const NamedDecl *> SecondTemplateParams =
        SecondTemplate->getTemplateParameters()->asArray();
    assert(FirstTemplateParams.size() == SecondTemplateParams.size() &&
           "Number of template parameters should be equal.");
    for (auto Pair : llvm::zip(FirstTemplateParams, SecondTemplateParams)) {
      const NamedDecl *FirstDecl = std::get<0>(Pair);
      const NamedDecl *SecondDecl = std::get<1>(Pair);
      if (computeODRHash(FirstDecl) == computeODRHash(SecondDecl))
        continue;

      assert(FirstDecl->getKind() == SecondDecl->getKind() &&
             "Parameter Decl's should be the same kind.");

      enum ODRTemplateDifference {
        ParamEmptyName,
        ParamName,
        ParamSingleDefaultArgument,
        ParamDifferentDefaultArgument,
      };

      auto hasDefaultArg = [](const NamedDecl *D) {
        if (auto *TTP = dyn_cast<TemplateTypeParmDecl>(D))
          return TTP->hasDefaultArgument() &&
                 !TTP->defaultArgumentWasInherited();
        if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(D))
          return NTTP->hasDefaultArgument() &&
                 !NTTP->defaultArgumentWasInherited();
        auto *TTP = cast<TemplateTemplateParmDecl>(D);
        return TTP->hasDefaultArgument() && !TTP->defaultArgumentWasInherited();
      };
      bool hasFirstArg = hasDefaultArg(FirstDecl);
      bool hasSecondArg = hasDefaultArg(SecondDecl);

      ODRTemplateDifference ErrDiffType;
      ODRTemplateDifference NoteDiffType;

      DeclarationName FirstName = FirstDecl->getDeclName();
      DeclarationName SecondName = SecondDecl->getDeclName();

      if (FirstName != SecondName) {
        bool FirstNameEmpty =
            FirstName.isIdentifier() && !FirstName.getAsIdentifierInfo();
        bool SecondNameEmpty =
            SecondName.isIdentifier() && !SecondName.getAsIdentifierInfo();
        ErrDiffType = FirstNameEmpty ? ParamEmptyName : ParamName;
        NoteDiffType = SecondNameEmpty ? ParamEmptyName : ParamName;
      } else if (hasFirstArg == hasSecondArg)
        ErrDiffType = NoteDiffType = ParamDifferentDefaultArgument;
      else
        ErrDiffType = NoteDiffType = ParamSingleDefaultArgument;

      Diag(FirstDecl->getLocation(),
           diag::err_module_odr_violation_template_parameter)
          << FirstRecord << FirstModule.empty() << FirstModule
          << FirstDecl->getSourceRange() << ErrDiffType << hasFirstArg
          << FirstName;
      Diag(SecondDecl->getLocation(),
           diag::note_module_odr_violation_template_parameter)
          << SecondModule << SecondDecl->getSourceRange() << NoteDiffType
          << hasSecondArg << SecondName;
      return true;
    }
  }

  auto PopulateHashes = [](DeclHashes &Hashes, const RecordDecl *Record,
                           const DeclContext *DC) {
    for (const Decl *D : Record->decls()) {
      if (!ODRHash::isSubDeclToBeProcessed(D, DC))
        continue;
      Hashes.emplace_back(D, computeODRHash(D));
    }
  };

  DeclHashes FirstHashes;
  DeclHashes SecondHashes;
  const DeclContext *DC = FirstRecord;
  PopulateHashes(FirstHashes, FirstRecord, DC);
  PopulateHashes(SecondHashes, SecondRecord, DC);

  DiffResult DR = FindTypeDiffs(FirstHashes, SecondHashes);
  ODRMismatchDecl FirstDiffType = DR.FirstDiffType;
  ODRMismatchDecl SecondDiffType = DR.SecondDiffType;
  const Decl *FirstDecl = DR.FirstDecl;
  const Decl *SecondDecl = DR.SecondDecl;

  if (FirstDiffType == Other || SecondDiffType == Other) {
    diagnoseSubMismatchUnexpected(DR, FirstRecord, FirstModule, SecondRecord,
                                  SecondModule);
    return true;
  }

  if (FirstDiffType != SecondDiffType) {
    diagnoseSubMismatchDifferentDeclKinds(DR, FirstRecord, FirstModule,
                                          SecondRecord, SecondModule);
    return true;
  }

  // Used with err_module_odr_violation_record and
  // note_module_odr_violation_record
  enum ODRCXXRecordDifference {
    StaticAssertCondition,
    StaticAssertMessage,
    StaticAssertOnlyMessage,
    MethodName,
    MethodDeleted,
    MethodDefaulted,
    MethodVirtual,
    MethodStatic,
    MethodVolatile,
    MethodConst,
    MethodInline,
    MethodParameterSingleDefaultArgument,
    MethodParameterDifferentDefaultArgument,
    MethodNoTemplateArguments,
    MethodDifferentNumberTemplateArguments,
    MethodDifferentTemplateArgument,
    MethodSingleBody,
    MethodDifferentBody,
    FriendTypeFunction,
    FriendType,
    FriendFunction,
    FunctionTemplateDifferentNumberParameters,
    FunctionTemplateParameterDifferentKind,
    FunctionTemplateParameterName,
    FunctionTemplateParameterSingleDefaultArgument,
    FunctionTemplateParameterDifferentDefaultArgument,
    FunctionTemplateParameterDifferentType,
    FunctionTemplatePackParameter,
  };
  auto DiagError = [FirstRecord, &FirstModule,
                    this](SourceLocation Loc, SourceRange Range,
                          ODRCXXRecordDifference DiffType) {
    return Diag(Loc, diag::err_module_odr_violation_record)
           << FirstRecord << FirstModule.empty() << FirstModule << Range
           << DiffType;
  };
  auto DiagNote = [&SecondModule, this](SourceLocation Loc, SourceRange Range,
                                        ODRCXXRecordDifference DiffType) {
    return Diag(Loc, diag::note_module_odr_violation_record)
           << SecondModule << Range << DiffType;
  };

  assert(FirstDiffType == SecondDiffType);
  switch (FirstDiffType) {
  case Other:
  case EndOfClass:
  case PublicSpecifer:
  case PrivateSpecifer:
  case ProtectedSpecifer:
  case ObjCMethod:
  case ObjCIvar:
  case ObjCProperty:
    llvm_unreachable("Invalid diff type");

  case StaticAssert: {
    const StaticAssertDecl *FirstSA = cast<StaticAssertDecl>(FirstDecl);
    const StaticAssertDecl *SecondSA = cast<StaticAssertDecl>(SecondDecl);

    const Expr *FirstExpr = FirstSA->getAssertExpr();
    const Expr *SecondExpr = SecondSA->getAssertExpr();
    unsigned FirstODRHash = computeODRHash(FirstExpr);
    unsigned SecondODRHash = computeODRHash(SecondExpr);
    if (FirstODRHash != SecondODRHash) {
      DiagError(FirstExpr->getBeginLoc(), FirstExpr->getSourceRange(),
                StaticAssertCondition);
      DiagNote(SecondExpr->getBeginLoc(), SecondExpr->getSourceRange(),
               StaticAssertCondition);
      return true;
    }

    const Expr *FirstMessage = FirstSA->getMessage();
    const Expr *SecondMessage = SecondSA->getMessage();
    assert((FirstMessage || SecondMessage) && "Both messages cannot be empty");
    if ((FirstMessage && !SecondMessage) || (!FirstMessage && SecondMessage)) {
      SourceLocation FirstLoc, SecondLoc;
      SourceRange FirstRange, SecondRange;
      if (FirstMessage) {
        FirstLoc = FirstMessage->getBeginLoc();
        FirstRange = FirstMessage->getSourceRange();
      } else {
        FirstLoc = FirstSA->getBeginLoc();
        FirstRange = FirstSA->getSourceRange();
      }
      if (SecondMessage) {
        SecondLoc = SecondMessage->getBeginLoc();
        SecondRange = SecondMessage->getSourceRange();
      } else {
        SecondLoc = SecondSA->getBeginLoc();
        SecondRange = SecondSA->getSourceRange();
      }
      DiagError(FirstLoc, FirstRange, StaticAssertOnlyMessage)
          << (FirstMessage == nullptr);
      DiagNote(SecondLoc, SecondRange, StaticAssertOnlyMessage)
          << (SecondMessage == nullptr);
      return true;
    }

    if (FirstMessage && SecondMessage) {
      unsigned FirstMessageODRHash = computeODRHash(FirstMessage);
      unsigned SecondMessageODRHash = computeODRHash(SecondMessage);
      if (FirstMessageODRHash != SecondMessageODRHash) {
        DiagError(FirstMessage->getBeginLoc(), FirstMessage->getSourceRange(),
                  StaticAssertMessage);
        DiagNote(SecondMessage->getBeginLoc(), SecondMessage->getSourceRange(),
                 StaticAssertMessage);
        return true;
      }
    }
    break;
  }

  case Field: {
    if (diagnoseSubMismatchField(FirstRecord, FirstModule, SecondModule,
                                 cast<FieldDecl>(FirstDecl),
                                 cast<FieldDecl>(SecondDecl)))
      return true;
    break;
  }

  case CXXMethod: {
    enum {
      DiagMethod,
      DiagConstructor,
      DiagDestructor,
    } FirstMethodType,
        SecondMethodType;
    auto GetMethodTypeForDiagnostics = [](const CXXMethodDecl *D) {
      if (isa<CXXConstructorDecl>(D))
        return DiagConstructor;
      if (isa<CXXDestructorDecl>(D))
        return DiagDestructor;
      return DiagMethod;
    };
    const CXXMethodDecl *FirstMethod = cast<CXXMethodDecl>(FirstDecl);
    const CXXMethodDecl *SecondMethod = cast<CXXMethodDecl>(SecondDecl);
    FirstMethodType = GetMethodTypeForDiagnostics(FirstMethod);
    SecondMethodType = GetMethodTypeForDiagnostics(SecondMethod);
    DeclarationName FirstName = FirstMethod->getDeclName();
    DeclarationName SecondName = SecondMethod->getDeclName();
    auto DiagMethodError = [&DiagError, FirstMethod, FirstMethodType,
                            FirstName](ODRCXXRecordDifference DiffType) {
      return DiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), DiffType)
             << FirstMethodType << FirstName;
    };
    auto DiagMethodNote = [&DiagNote, SecondMethod, SecondMethodType,
                           SecondName](ODRCXXRecordDifference DiffType) {
      return DiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), DiffType)
             << SecondMethodType << SecondName;
    };

    if (FirstMethodType != SecondMethodType || FirstName != SecondName) {
      DiagMethodError(MethodName);
      DiagMethodNote(MethodName);
      return true;
    }

    const bool FirstDeleted = FirstMethod->isDeletedAsWritten();
    const bool SecondDeleted = SecondMethod->isDeletedAsWritten();
    if (FirstDeleted != SecondDeleted) {
      DiagMethodError(MethodDeleted) << FirstDeleted;
      DiagMethodNote(MethodDeleted) << SecondDeleted;
      return true;
    }

    const bool FirstDefaulted = FirstMethod->isExplicitlyDefaulted();
    const bool SecondDefaulted = SecondMethod->isExplicitlyDefaulted();
    if (FirstDefaulted != SecondDefaulted) {
      DiagMethodError(MethodDefaulted) << FirstDefaulted;
      DiagMethodNote(MethodDefaulted) << SecondDefaulted;
      return true;
    }

    const bool FirstVirtual = FirstMethod->isVirtualAsWritten();
    const bool SecondVirtual = SecondMethod->isVirtualAsWritten();
    const bool FirstPure = FirstMethod->isPureVirtual();
    const bool SecondPure = SecondMethod->isPureVirtual();
    if ((FirstVirtual || SecondVirtual) &&
        (FirstVirtual != SecondVirtual || FirstPure != SecondPure)) {
      DiagMethodError(MethodVirtual) << FirstPure << FirstVirtual;
      DiagMethodNote(MethodVirtual) << SecondPure << SecondVirtual;
      return true;
    }

    // CXXMethodDecl::isStatic uses the canonical Decl.  With Decl merging,
    // FirstDecl is the canonical Decl of SecondDecl, so the storage
    // class needs to be checked instead.
    StorageClass FirstStorage = FirstMethod->getStorageClass();
    StorageClass SecondStorage = SecondMethod->getStorageClass();
    const bool FirstStatic = FirstStorage == SC_Static;
    const bool SecondStatic = SecondStorage == SC_Static;
    if (FirstStatic != SecondStatic) {
      DiagMethodError(MethodStatic) << FirstStatic;
      DiagMethodNote(MethodStatic) << SecondStatic;
      return true;
    }

    const bool FirstVolatile = FirstMethod->isVolatile();
    const bool SecondVolatile = SecondMethod->isVolatile();
    if (FirstVolatile != SecondVolatile) {
      DiagMethodError(MethodVolatile) << FirstVolatile;
      DiagMethodNote(MethodVolatile) << SecondVolatile;
      return true;
    }

    const bool FirstConst = FirstMethod->isConst();
    const bool SecondConst = SecondMethod->isConst();
    if (FirstConst != SecondConst) {
      DiagMethodError(MethodConst) << FirstConst;
      DiagMethodNote(MethodConst) << SecondConst;
      return true;
    }

    const bool FirstInline = FirstMethod->isInlineSpecified();
    const bool SecondInline = SecondMethod->isInlineSpecified();
    if (FirstInline != SecondInline) {
      DiagMethodError(MethodInline) << FirstInline;
      DiagMethodNote(MethodInline) << SecondInline;
      return true;
    }

    if (diagnoseSubMismatchMethodParameters(Diags, FirstRecord,
                                            FirstModule, SecondModule,
                                            FirstMethod, SecondMethod))
      return true;

    for (unsigned I = 0, N = FirstMethod->param_size(); I < N; ++I) {
      const ParmVarDecl *FirstParam = FirstMethod->getParamDecl(I);
      const ParmVarDecl *SecondParam = SecondMethod->getParamDecl(I);

      const Expr *FirstInit = FirstParam->getInit();
      const Expr *SecondInit = SecondParam->getInit();
      if ((FirstInit == nullptr) != (SecondInit == nullptr)) {
        DiagMethodError(MethodParameterSingleDefaultArgument)
            << (I + 1) << (FirstInit == nullptr)
            << (FirstInit ? FirstInit->getSourceRange() : SourceRange());
        DiagMethodNote(MethodParameterSingleDefaultArgument)
            << (I + 1) << (SecondInit == nullptr)
            << (SecondInit ? SecondInit->getSourceRange() : SourceRange());
        return true;
      }

      if (FirstInit && SecondInit &&
          computeODRHash(FirstInit) != computeODRHash(SecondInit)) {
        DiagMethodError(MethodParameterDifferentDefaultArgument)
            << (I + 1) << FirstInit->getSourceRange();
        DiagMethodNote(MethodParameterDifferentDefaultArgument)
            << (I + 1) << SecondInit->getSourceRange();
        return true;
      }
    }

    const TemplateArgumentList *FirstTemplateArgs =
        FirstMethod->getTemplateSpecializationArgs();
    const TemplateArgumentList *SecondTemplateArgs =
        SecondMethod->getTemplateSpecializationArgs();

    if ((FirstTemplateArgs && !SecondTemplateArgs) ||
        (!FirstTemplateArgs && SecondTemplateArgs)) {
      DiagMethodError(MethodNoTemplateArguments)
          << (FirstTemplateArgs != nullptr);
      DiagMethodNote(MethodNoTemplateArguments)
          << (SecondTemplateArgs != nullptr);
      return true;
    }

    if (FirstTemplateArgs && SecondTemplateArgs) {
      // Remove pack expansions from argument list.
      auto ExpandTemplateArgumentList = [](const TemplateArgumentList *TAL) {
        llvm::SmallVector<const TemplateArgument *, 8> ExpandedList;
        for (const TemplateArgument &TA : TAL->asArray()) {
          if (TA.getKind() != TemplateArgument::Pack) {
            ExpandedList.push_back(&TA);
            continue;
          }
          llvm::append_range(ExpandedList,
                             llvm::make_pointer_range(TA.getPackAsArray()));
        }
        return ExpandedList;
      };
      llvm::SmallVector<const TemplateArgument *, 8> FirstExpandedList =
          ExpandTemplateArgumentList(FirstTemplateArgs);
      llvm::SmallVector<const TemplateArgument *, 8> SecondExpandedList =
          ExpandTemplateArgumentList(SecondTemplateArgs);

      if (FirstExpandedList.size() != SecondExpandedList.size()) {
        DiagMethodError(MethodDifferentNumberTemplateArguments)
            << (unsigned)FirstExpandedList.size();
        DiagMethodNote(MethodDifferentNumberTemplateArguments)
            << (unsigned)SecondExpandedList.size();
        return true;
      }

      for (unsigned i = 0, e = FirstExpandedList.size(); i != e; ++i) {
        const TemplateArgument &FirstTA = *FirstExpandedList[i],
                               &SecondTA = *SecondExpandedList[i];
        if (computeODRHash(FirstTA) == computeODRHash(SecondTA))
          continue;

        DiagMethodError(MethodDifferentTemplateArgument) << FirstTA << i + 1;
        DiagMethodNote(MethodDifferentTemplateArgument) << SecondTA << i + 1;
        return true;
      }
    }

    // Compute the hash of the method as if it has no body.
    auto ComputeCXXMethodODRHash = [](const CXXMethodDecl *D) {
      ODRHash Hasher;
      Hasher.AddFunctionDecl(D, true /*SkipBody*/);
      return Hasher.CalculateHash();
    };

    // Compare the hash generated to the hash stored.  A difference means
    // that a body was present in the original source.  Due to merging,
    // the standard way of detecting a body will not work.
    const bool HasFirstBody =
        ComputeCXXMethodODRHash(FirstMethod) != FirstMethod->getODRHash();
    const bool HasSecondBody =
        ComputeCXXMethodODRHash(SecondMethod) != SecondMethod->getODRHash();

    if (HasFirstBody != HasSecondBody) {
      DiagMethodError(MethodSingleBody) << HasFirstBody;
      DiagMethodNote(MethodSingleBody) << HasSecondBody;
      return true;
    }

    if (HasFirstBody && HasSecondBody) {
      DiagMethodError(MethodDifferentBody);
      DiagMethodNote(MethodDifferentBody);
      return true;
    }

    break;
  }

  case TypeAlias:
  case TypeDef: {
    if (diagnoseSubMismatchTypedef(FirstRecord, FirstModule, SecondModule,
                                   cast<TypedefNameDecl>(FirstDecl),
                                   cast<TypedefNameDecl>(SecondDecl),
                                   FirstDiffType == TypeAlias))
      return true;
    break;
  }
  case Var: {
    if (diagnoseSubMismatchVar(FirstRecord, FirstModule, SecondModule,
                               cast<VarDecl>(FirstDecl),
                               cast<VarDecl>(SecondDecl)))
      return true;
    break;
  }
  case Friend: {
    const FriendDecl *FirstFriend = cast<FriendDecl>(FirstDecl);
    const FriendDecl *SecondFriend = cast<FriendDecl>(SecondDecl);

    const NamedDecl *FirstND = FirstFriend->getFriendDecl();
    const NamedDecl *SecondND = SecondFriend->getFriendDecl();

    TypeSourceInfo *FirstTSI = FirstFriend->getFriendType();
    TypeSourceInfo *SecondTSI = SecondFriend->getFriendType();

    if (FirstND && SecondND) {
      DiagError(FirstFriend->getFriendLoc(), FirstFriend->getSourceRange(),
                FriendFunction)
          << FirstND;
      DiagNote(SecondFriend->getFriendLoc(), SecondFriend->getSourceRange(),
               FriendFunction)
          << SecondND;
      return true;
    }

    if (FirstTSI && SecondTSI) {
      QualType FirstFriendType = FirstTSI->getType();
      QualType SecondFriendType = SecondTSI->getType();
      assert(computeODRHash(FirstFriendType) !=
             computeODRHash(SecondFriendType));
      DiagError(FirstFriend->getFriendLoc(), FirstFriend->getSourceRange(),
                FriendType)
          << FirstFriendType;
      DiagNote(SecondFriend->getFriendLoc(), SecondFriend->getSourceRange(),
               FriendType)
          << SecondFriendType;
      return true;
    }

    DiagError(FirstFriend->getFriendLoc(), FirstFriend->getSourceRange(),
              FriendTypeFunction)
        << (FirstTSI == nullptr);
    DiagNote(SecondFriend->getFriendLoc(), SecondFriend->getSourceRange(),
             FriendTypeFunction)
        << (SecondTSI == nullptr);
    return true;
  }
  case FunctionTemplate: {
    const FunctionTemplateDecl *FirstTemplate =
        cast<FunctionTemplateDecl>(FirstDecl);
    const FunctionTemplateDecl *SecondTemplate =
        cast<FunctionTemplateDecl>(SecondDecl);

    TemplateParameterList *FirstTPL = FirstTemplate->getTemplateParameters();
    TemplateParameterList *SecondTPL = SecondTemplate->getTemplateParameters();

    auto DiagTemplateError = [&DiagError,
                              FirstTemplate](ODRCXXRecordDifference DiffType) {
      return DiagError(FirstTemplate->getLocation(),
                       FirstTemplate->getSourceRange(), DiffType)
             << FirstTemplate;
    };
    auto DiagTemplateNote = [&DiagNote,
                             SecondTemplate](ODRCXXRecordDifference DiffType) {
      return DiagNote(SecondTemplate->getLocation(),
                      SecondTemplate->getSourceRange(), DiffType)
             << SecondTemplate;
    };

    if (FirstTPL->size() != SecondTPL->size()) {
      DiagTemplateError(FunctionTemplateDifferentNumberParameters)
          << FirstTPL->size();
      DiagTemplateNote(FunctionTemplateDifferentNumberParameters)
          << SecondTPL->size();
      return true;
    }

    for (unsigned i = 0, e = FirstTPL->size(); i != e; ++i) {
      NamedDecl *FirstParam = FirstTPL->getParam(i);
      NamedDecl *SecondParam = SecondTPL->getParam(i);

      if (FirstParam->getKind() != SecondParam->getKind()) {
        enum {
          TemplateTypeParameter,
          NonTypeTemplateParameter,
          TemplateTemplateParameter,
        };
        auto GetParamType = [](NamedDecl *D) {
          switch (D->getKind()) {
          default:
            llvm_unreachable("Unexpected template parameter type");
          case Decl::TemplateTypeParm:
            return TemplateTypeParameter;
          case Decl::NonTypeTemplateParm:
            return NonTypeTemplateParameter;
          case Decl::TemplateTemplateParm:
            return TemplateTemplateParameter;
          }
        };

        DiagTemplateError(FunctionTemplateParameterDifferentKind)
            << (i + 1) << GetParamType(FirstParam);
        DiagTemplateNote(FunctionTemplateParameterDifferentKind)
            << (i + 1) << GetParamType(SecondParam);
        return true;
      }

      if (FirstParam->getName() != SecondParam->getName()) {
        DiagTemplateError(FunctionTemplateParameterName)
            << (i + 1) << (bool)FirstParam->getIdentifier() << FirstParam;
        DiagTemplateNote(FunctionTemplateParameterName)
            << (i + 1) << (bool)SecondParam->getIdentifier() << SecondParam;
        return true;
      }

      if (isa<TemplateTypeParmDecl>(FirstParam) &&
          isa<TemplateTypeParmDecl>(SecondParam)) {
        TemplateTypeParmDecl *FirstTTPD =
            cast<TemplateTypeParmDecl>(FirstParam);
        TemplateTypeParmDecl *SecondTTPD =
            cast<TemplateTypeParmDecl>(SecondParam);
        bool HasFirstDefaultArgument =
            FirstTTPD->hasDefaultArgument() &&
            !FirstTTPD->defaultArgumentWasInherited();
        bool HasSecondDefaultArgument =
            SecondTTPD->hasDefaultArgument() &&
            !SecondTTPD->defaultArgumentWasInherited();
        if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
          DiagTemplateError(FunctionTemplateParameterSingleDefaultArgument)
              << (i + 1) << HasFirstDefaultArgument;
          DiagTemplateNote(FunctionTemplateParameterSingleDefaultArgument)
              << (i + 1) << HasSecondDefaultArgument;
          return true;
        }

        if (HasFirstDefaultArgument && HasSecondDefaultArgument) {
          TemplateArgument FirstTA =
              FirstTTPD->getDefaultArgument().getArgument();
          TemplateArgument SecondTA =
              SecondTTPD->getDefaultArgument().getArgument();
          if (computeODRHash(FirstTA) != computeODRHash(SecondTA)) {
            DiagTemplateError(FunctionTemplateParameterDifferentDefaultArgument)
                << (i + 1) << FirstTA;
            DiagTemplateNote(FunctionTemplateParameterDifferentDefaultArgument)
                << (i + 1) << SecondTA;
            return true;
          }
        }

        if (FirstTTPD->isParameterPack() != SecondTTPD->isParameterPack()) {
          DiagTemplateError(FunctionTemplatePackParameter)
              << (i + 1) << FirstTTPD->isParameterPack();
          DiagTemplateNote(FunctionTemplatePackParameter)
              << (i + 1) << SecondTTPD->isParameterPack();
          return true;
        }
      }

      if (isa<TemplateTemplateParmDecl>(FirstParam) &&
          isa<TemplateTemplateParmDecl>(SecondParam)) {
        TemplateTemplateParmDecl *FirstTTPD =
            cast<TemplateTemplateParmDecl>(FirstParam);
        TemplateTemplateParmDecl *SecondTTPD =
            cast<TemplateTemplateParmDecl>(SecondParam);

        TemplateParameterList *FirstTPL = FirstTTPD->getTemplateParameters();
        TemplateParameterList *SecondTPL = SecondTTPD->getTemplateParameters();

        auto ComputeTemplateParameterListODRHash =
            [](const TemplateParameterList *TPL) {
              assert(TPL);
              ODRHash Hasher;
              Hasher.AddTemplateParameterList(TPL);
              return Hasher.CalculateHash();
            };

        if (ComputeTemplateParameterListODRHash(FirstTPL) !=
            ComputeTemplateParameterListODRHash(SecondTPL)) {
          DiagTemplateError(FunctionTemplateParameterDifferentType) << (i + 1);
          DiagTemplateNote(FunctionTemplateParameterDifferentType) << (i + 1);
          return true;
        }

        bool HasFirstDefaultArgument =
            FirstTTPD->hasDefaultArgument() &&
            !FirstTTPD->defaultArgumentWasInherited();
        bool HasSecondDefaultArgument =
            SecondTTPD->hasDefaultArgument() &&
            !SecondTTPD->defaultArgumentWasInherited();
        if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
          DiagTemplateError(FunctionTemplateParameterSingleDefaultArgument)
              << (i + 1) << HasFirstDefaultArgument;
          DiagTemplateNote(FunctionTemplateParameterSingleDefaultArgument)
              << (i + 1) << HasSecondDefaultArgument;
          return true;
        }

        if (HasFirstDefaultArgument && HasSecondDefaultArgument) {
          TemplateArgument FirstTA =
              FirstTTPD->getDefaultArgument().getArgument();
          TemplateArgument SecondTA =
              SecondTTPD->getDefaultArgument().getArgument();
          if (computeODRHash(FirstTA) != computeODRHash(SecondTA)) {
            DiagTemplateError(FunctionTemplateParameterDifferentDefaultArgument)
                << (i + 1) << FirstTA;
            DiagTemplateNote(FunctionTemplateParameterDifferentDefaultArgument)
                << (i + 1) << SecondTA;
            return true;
          }
        }

        if (FirstTTPD->isParameterPack() != SecondTTPD->isParameterPack()) {
          DiagTemplateError(FunctionTemplatePackParameter)
              << (i + 1) << FirstTTPD->isParameterPack();
          DiagTemplateNote(FunctionTemplatePackParameter)
              << (i + 1) << SecondTTPD->isParameterPack();
          return true;
        }
      }

      if (isa<NonTypeTemplateParmDecl>(FirstParam) &&
          isa<NonTypeTemplateParmDecl>(SecondParam)) {
        NonTypeTemplateParmDecl *FirstNTTPD =
            cast<NonTypeTemplateParmDecl>(FirstParam);
        NonTypeTemplateParmDecl *SecondNTTPD =
            cast<NonTypeTemplateParmDecl>(SecondParam);

        QualType FirstType = FirstNTTPD->getType();
        QualType SecondType = SecondNTTPD->getType();
        if (computeODRHash(FirstType) != computeODRHash(SecondType)) {
          DiagTemplateError(FunctionTemplateParameterDifferentType) << (i + 1);
          DiagTemplateNote(FunctionTemplateParameterDifferentType) << (i + 1);
          return true;
        }

        bool HasFirstDefaultArgument =
            FirstNTTPD->hasDefaultArgument() &&
            !FirstNTTPD->defaultArgumentWasInherited();
        bool HasSecondDefaultArgument =
            SecondNTTPD->hasDefaultArgument() &&
            !SecondNTTPD->defaultArgumentWasInherited();
        if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
          DiagTemplateError(FunctionTemplateParameterSingleDefaultArgument)
              << (i + 1) << HasFirstDefaultArgument;
          DiagTemplateNote(FunctionTemplateParameterSingleDefaultArgument)
              << (i + 1) << HasSecondDefaultArgument;
          return true;
        }

        if (HasFirstDefaultArgument && HasSecondDefaultArgument) {
          TemplateArgument FirstDefaultArgument =
              FirstNTTPD->getDefaultArgument().getArgument();
          TemplateArgument SecondDefaultArgument =
              SecondNTTPD->getDefaultArgument().getArgument();

          if (computeODRHash(FirstDefaultArgument) !=
              computeODRHash(SecondDefaultArgument)) {
            DiagTemplateError(FunctionTemplateParameterDifferentDefaultArgument)
                << (i + 1) << FirstDefaultArgument;
            DiagTemplateNote(FunctionTemplateParameterDifferentDefaultArgument)
                << (i + 1) << SecondDefaultArgument;
            return true;
          }
        }

        if (FirstNTTPD->isParameterPack() != SecondNTTPD->isParameterPack()) {
          DiagTemplateError(FunctionTemplatePackParameter)
              << (i + 1) << FirstNTTPD->isParameterPack();
          DiagTemplateNote(FunctionTemplatePackParameter)
              << (i + 1) << SecondNTTPD->isParameterPack();
          return true;
        }
      }
    }
    break;
  }
  }

  Diag(FirstDecl->getLocation(),
       diag::err_module_odr_violation_mismatch_decl_unknown)
      << FirstRecord << FirstModule.empty() << FirstModule << FirstDiffType
      << FirstDecl->getSourceRange();
  Diag(SecondDecl->getLocation(),
       diag::note_module_odr_violation_mismatch_decl_unknown)
      << SecondModule.empty() << SecondModule << FirstDiffType
      << SecondDecl->getSourceRange();
  return true;
}

bool ODRDiagsEmitter::diagnoseMismatch(const RecordDecl *FirstRecord,
                                       const RecordDecl *SecondRecord) const {
  if (FirstRecord == SecondRecord)
    return false;

  std::string FirstModule = getOwningModuleNameForDiagnostic(FirstRecord);
  std::string SecondModule = getOwningModuleNameForDiagnostic(SecondRecord);

  auto PopulateHashes = [](DeclHashes &Hashes, const RecordDecl *Record,
                           const DeclContext *DC) {
    for (const Decl *D : Record->decls()) {
      if (!ODRHash::isSubDeclToBeProcessed(D, DC))
        continue;
      Hashes.emplace_back(D, computeODRHash(D));
    }
  };

  DeclHashes FirstHashes;
  DeclHashes SecondHashes;
  const DeclContext *DC = FirstRecord;
  PopulateHashes(FirstHashes, FirstRecord, DC);
  PopulateHashes(SecondHashes, SecondRecord, DC);

  DiffResult DR = FindTypeDiffs(FirstHashes, SecondHashes);
  ODRMismatchDecl FirstDiffType = DR.FirstDiffType;
  ODRMismatchDecl SecondDiffType = DR.SecondDiffType;
  const Decl *FirstDecl = DR.FirstDecl;
  const Decl *SecondDecl = DR.SecondDecl;

  if (FirstDiffType == Other || SecondDiffType == Other) {
    diagnoseSubMismatchUnexpected(DR, FirstRecord, FirstModule, SecondRecord,
                                  SecondModule);
    return true;
  }

  if (FirstDiffType != SecondDiffType) {
    diagnoseSubMismatchDifferentDeclKinds(DR, FirstRecord, FirstModule,
                                          SecondRecord, SecondModule);
    return true;
  }

  assert(FirstDiffType == SecondDiffType);
  switch (FirstDiffType) {
  // Already handled.
  case EndOfClass:
  case Other:
  // C++ only, invalid in this context.
  case PublicSpecifer:
  case PrivateSpecifer:
  case ProtectedSpecifer:
  case StaticAssert:
  case CXXMethod:
  case TypeAlias:
  case Friend:
  case FunctionTemplate:
  // Cannot be contained by RecordDecl, invalid in this context.
  case ObjCMethod:
  case ObjCIvar:
  case ObjCProperty:
    llvm_unreachable("Invalid diff type");

  case Field: {
    if (diagnoseSubMismatchField(FirstRecord, FirstModule, SecondModule,
                                 cast<FieldDecl>(FirstDecl),
                                 cast<FieldDecl>(SecondDecl)))
      return true;
    break;
  }
  case TypeDef: {
    if (diagnoseSubMismatchTypedef(FirstRecord, FirstModule, SecondModule,
                                   cast<TypedefNameDecl>(FirstDecl),
                                   cast<TypedefNameDecl>(SecondDecl),
                                   /*IsTypeAlias=*/false))
      return true;
    break;
  }
  case Var: {
    if (diagnoseSubMismatchVar(FirstRecord, FirstModule, SecondModule,
                               cast<VarDecl>(FirstDecl),
                               cast<VarDecl>(SecondDecl)))
      return true;
    break;
  }
  }

  Diag(FirstDecl->getLocation(),
       diag::err_module_odr_violation_mismatch_decl_unknown)
      << FirstRecord << FirstModule.empty() << FirstModule << FirstDiffType
      << FirstDecl->getSourceRange();
  Diag(SecondDecl->getLocation(),
       diag::note_module_odr_violation_mismatch_decl_unknown)
      << SecondModule.empty() << SecondModule << FirstDiffType
      << SecondDecl->getSourceRange();
  return true;
}

bool ODRDiagsEmitter::diagnoseMismatch(
    const FunctionDecl *FirstFunction,
    const FunctionDecl *SecondFunction) const {
  if (FirstFunction == SecondFunction)
    return false;

  // Keep in sync with select options in err_module_odr_violation_function.
  enum ODRFunctionDifference {
    ReturnType,
    ParameterName,
    ParameterType,
    ParameterSingleDefaultArgument,
    ParameterDifferentDefaultArgument,
    FunctionBody,
  };

  std::string FirstModule = getOwningModuleNameForDiagnostic(FirstFunction);
  std::string SecondModule = getOwningModuleNameForDiagnostic(SecondFunction);

  auto DiagError = [FirstFunction, &FirstModule,
                    this](SourceLocation Loc, SourceRange Range,
                          ODRFunctionDifference DiffType) {
    return Diag(Loc, diag::err_module_odr_violation_function)
           << FirstFunction << FirstModule.empty() << FirstModule << Range
           << DiffType;
  };
  auto DiagNote = [&SecondModule, this](SourceLocation Loc, SourceRange Range,
                                        ODRFunctionDifference DiffType) {
    return Diag(Loc, diag::note_module_odr_violation_function)
           << SecondModule << Range << DiffType;
  };

  if (computeODRHash(FirstFunction->getReturnType()) !=
      computeODRHash(SecondFunction->getReturnType())) {
    DiagError(FirstFunction->getReturnTypeSourceRange().getBegin(),
              FirstFunction->getReturnTypeSourceRange(), ReturnType)
        << FirstFunction->getReturnType();
    DiagNote(SecondFunction->getReturnTypeSourceRange().getBegin(),
             SecondFunction->getReturnTypeSourceRange(), ReturnType)
        << SecondFunction->getReturnType();
    return true;
  }

  assert(FirstFunction->param_size() == SecondFunction->param_size() &&
         "Merged functions with different number of parameters");

  size_t ParamSize = FirstFunction->param_size();
  for (unsigned I = 0; I < ParamSize; ++I) {
    const ParmVarDecl *FirstParam = FirstFunction->getParamDecl(I);
    const ParmVarDecl *SecondParam = SecondFunction->getParamDecl(I);

    assert(Context.hasSameType(FirstParam->getType(), SecondParam->getType()) &&
           "Merged function has different parameter types.");

    if (FirstParam->getDeclName() != SecondParam->getDeclName()) {
      DiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                ParameterName)
          << I + 1 << FirstParam->getDeclName();
      DiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
               ParameterName)
          << I + 1 << SecondParam->getDeclName();
      return true;
    };

    QualType FirstParamType = FirstParam->getType();
    QualType SecondParamType = SecondParam->getType();
    if (FirstParamType != SecondParamType &&
        computeODRHash(FirstParamType) != computeODRHash(SecondParamType)) {
      if (const DecayedType *ParamDecayedType =
              FirstParamType->getAs<DecayedType>()) {
        DiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                  ParameterType)
            << (I + 1) << FirstParamType << true
            << ParamDecayedType->getOriginalType();
      } else {
        DiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                  ParameterType)
            << (I + 1) << FirstParamType << false;
      }

      if (const DecayedType *ParamDecayedType =
              SecondParamType->getAs<DecayedType>()) {
        DiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
                 ParameterType)
            << (I + 1) << SecondParamType << true
            << ParamDecayedType->getOriginalType();
      } else {
        DiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
                 ParameterType)
            << (I + 1) << SecondParamType << false;
      }
      return true;
    }

    // Note, these calls can trigger deserialization.
    const Expr *FirstInit = FirstParam->getInit();
    const Expr *SecondInit = SecondParam->getInit();
    if ((FirstInit == nullptr) != (SecondInit == nullptr)) {
      DiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                ParameterSingleDefaultArgument)
          << (I + 1) << (FirstInit == nullptr)
          << (FirstInit ? FirstInit->getSourceRange() : SourceRange());
      DiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
               ParameterSingleDefaultArgument)
          << (I + 1) << (SecondInit == nullptr)
          << (SecondInit ? SecondInit->getSourceRange() : SourceRange());
      return true;
    }

    if (FirstInit && SecondInit &&
        computeODRHash(FirstInit) != computeODRHash(SecondInit)) {
      DiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                ParameterDifferentDefaultArgument)
          << (I + 1) << FirstInit->getSourceRange();
      DiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
               ParameterDifferentDefaultArgument)
          << (I + 1) << SecondInit->getSourceRange();
      return true;
    }

    assert(computeODRHash(FirstParam) == computeODRHash(SecondParam) &&
           "Undiagnosed parameter difference.");
  }

  // If no error has been generated before now, assume the problem is in
  // the body and generate a message.
  DiagError(FirstFunction->getLocation(), FirstFunction->getSourceRange(),
            FunctionBody);
  DiagNote(SecondFunction->getLocation(), SecondFunction->getSourceRange(),
           FunctionBody);
  return true;
}

bool ODRDiagsEmitter::diagnoseMismatch(const EnumDecl *FirstEnum,
                                       const EnumDecl *SecondEnum) const {
  if (FirstEnum == SecondEnum)
    return false;

  // Keep in sync with select options in err_module_odr_violation_enum.
  enum ODREnumDifference {
    SingleScopedEnum,
    EnumTagKeywordMismatch,
    SingleSpecifiedType,
    DifferentSpecifiedTypes,
    DifferentNumberEnumConstants,
    EnumConstantName,
    EnumConstantSingleInitializer,
    EnumConstantDifferentInitializer,
  };

  std::string FirstModule = getOwningModuleNameForDiagnostic(FirstEnum);
  std::string SecondModule = getOwningModuleNameForDiagnostic(SecondEnum);

  auto DiagError = [FirstEnum, &FirstModule, this](const auto *DiagAnchor,
                                                   ODREnumDifference DiffType) {
    return Diag(DiagAnchor->getLocation(), diag::err_module_odr_violation_enum)
           << FirstEnum << FirstModule.empty() << FirstModule
           << DiagAnchor->getSourceRange() << DiffType;
  };
  auto DiagNote = [&SecondModule, this](const auto *DiagAnchor,
                                        ODREnumDifference DiffType) {
    return Diag(DiagAnchor->getLocation(), diag::note_module_odr_violation_enum)
           << SecondModule << DiagAnchor->getSourceRange() << DiffType;
  };

  if (FirstEnum->isScoped() != SecondEnum->isScoped()) {
    DiagError(FirstEnum, SingleScopedEnum) << FirstEnum->isScoped();
    DiagNote(SecondEnum, SingleScopedEnum) << SecondEnum->isScoped();
    return true;
  }

  if (FirstEnum->isScoped() && SecondEnum->isScoped()) {
    if (FirstEnum->isScopedUsingClassTag() !=
        SecondEnum->isScopedUsingClassTag()) {
      DiagError(FirstEnum, EnumTagKeywordMismatch)
          << FirstEnum->isScopedUsingClassTag();
      DiagNote(SecondEnum, EnumTagKeywordMismatch)
          << SecondEnum->isScopedUsingClassTag();
      return true;
    }
  }

  QualType FirstUnderlyingType =
      FirstEnum->getIntegerTypeSourceInfo()
          ? FirstEnum->getIntegerTypeSourceInfo()->getType()
          : QualType();
  QualType SecondUnderlyingType =
      SecondEnum->getIntegerTypeSourceInfo()
          ? SecondEnum->getIntegerTypeSourceInfo()->getType()
          : QualType();
  if (FirstUnderlyingType.isNull() != SecondUnderlyingType.isNull()) {
    DiagError(FirstEnum, SingleSpecifiedType) << !FirstUnderlyingType.isNull();
    DiagNote(SecondEnum, SingleSpecifiedType) << !SecondUnderlyingType.isNull();
    return true;
  }

  if (!FirstUnderlyingType.isNull() && !SecondUnderlyingType.isNull()) {
    if (computeODRHash(FirstUnderlyingType) !=
        computeODRHash(SecondUnderlyingType)) {
      DiagError(FirstEnum, DifferentSpecifiedTypes) << FirstUnderlyingType;
      DiagNote(SecondEnum, DifferentSpecifiedTypes) << SecondUnderlyingType;
      return true;
    }
  }

  // Compare enum constants.
  using DeclHashes =
      llvm::SmallVector<std::pair<const EnumConstantDecl *, unsigned>, 4>;
  auto PopulateHashes = [FirstEnum](DeclHashes &Hashes, const EnumDecl *Enum) {
    for (const Decl *D : Enum->decls()) {
      // Due to decl merging, the first EnumDecl is the parent of
      // Decls in both records.
      if (!ODRHash::isSubDeclToBeProcessed(D, FirstEnum))
        continue;
      assert(isa<EnumConstantDecl>(D) && "Unexpected Decl kind");
      Hashes.emplace_back(cast<EnumConstantDecl>(D), computeODRHash(D));
    }
  };
  DeclHashes FirstHashes;
  PopulateHashes(FirstHashes, FirstEnum);
  DeclHashes SecondHashes;
  PopulateHashes(SecondHashes, SecondEnum);

  if (FirstHashes.size() != SecondHashes.size()) {
    DiagError(FirstEnum, DifferentNumberEnumConstants)
        << (int)FirstHashes.size();
    DiagNote(SecondEnum, DifferentNumberEnumConstants)
        << (int)SecondHashes.size();
    return true;
  }

  for (unsigned I = 0, N = FirstHashes.size(); I < N; ++I) {
    if (FirstHashes[I].second == SecondHashes[I].second)
      continue;
    const EnumConstantDecl *FirstConstant = FirstHashes[I].first;
    const EnumConstantDecl *SecondConstant = SecondHashes[I].first;

    if (FirstConstant->getDeclName() != SecondConstant->getDeclName()) {
      DiagError(FirstConstant, EnumConstantName) << I + 1 << FirstConstant;
      DiagNote(SecondConstant, EnumConstantName) << I + 1 << SecondConstant;
      return true;
    }

    const Expr *FirstInit = FirstConstant->getInitExpr();
    const Expr *SecondInit = SecondConstant->getInitExpr();
    if (!FirstInit && !SecondInit)
      continue;

    if (!FirstInit || !SecondInit) {
      DiagError(FirstConstant, EnumConstantSingleInitializer)
          << I + 1 << FirstConstant << (FirstInit != nullptr);
      DiagNote(SecondConstant, EnumConstantSingleInitializer)
          << I + 1 << SecondConstant << (SecondInit != nullptr);
      return true;
    }

    if (computeODRHash(FirstInit) != computeODRHash(SecondInit)) {
      DiagError(FirstConstant, EnumConstantDifferentInitializer)
          << I + 1 << FirstConstant;
      DiagNote(SecondConstant, EnumConstantDifferentInitializer)
          << I + 1 << SecondConstant;
      return true;
    }
  }
  return false;
}

bool ODRDiagsEmitter::diagnoseMismatch(
    const ObjCInterfaceDecl *FirstID, const ObjCInterfaceDecl *SecondID,
    const struct ObjCInterfaceDecl::DefinitionData *SecondDD) const {
  // Multiple different declarations got merged together; tell the user
  // where they came from.
  if (FirstID == SecondID)
    return false;

  std::string FirstModule = getOwningModuleNameForDiagnostic(FirstID);
  std::string SecondModule = getOwningModuleNameForDiagnostic(SecondID);

  // Keep in sync with err_module_odr_violation_objc_interface.
  enum ODRInterfaceDifference {
    SuperClassType,
    IVarAccess,
  };

  auto DiagError = [FirstID, &FirstModule,
                    this](SourceLocation Loc, SourceRange Range,
                          ODRInterfaceDifference DiffType) {
    return Diag(Loc, diag::err_module_odr_violation_objc_interface)
           << FirstID << FirstModule.empty() << FirstModule << Range
           << DiffType;
  };
  auto DiagNote = [&SecondModule, this](SourceLocation Loc, SourceRange Range,
                                        ODRInterfaceDifference DiffType) {
    return Diag(Loc, diag::note_module_odr_violation_objc_interface)
           << SecondModule.empty() << SecondModule << Range << DiffType;
  };

  const struct ObjCInterfaceDecl::DefinitionData *FirstDD = &FirstID->data();
  assert(FirstDD && SecondDD && "Definitions without DefinitionData");
  if (FirstDD != SecondDD) {
    // Check for matching super class.
    auto GetSuperClassSourceRange = [](const TypeSourceInfo *SuperInfo,
                                       const ObjCInterfaceDecl *ID) {
      if (!SuperInfo)
        return ID->getSourceRange();
      TypeLoc Loc = SuperInfo->getTypeLoc();
      return SourceRange(Loc.getBeginLoc(), Loc.getEndLoc());
    };

    ObjCInterfaceDecl *FirstSuperClass = FirstID->getSuperClass();
    ObjCInterfaceDecl *SecondSuperClass = nullptr;
    const TypeSourceInfo *FirstSuperInfo = FirstID->getSuperClassTInfo();
    const TypeSourceInfo *SecondSuperInfo = SecondDD->SuperClassTInfo;
    if (SecondSuperInfo)
      SecondSuperClass =
          SecondSuperInfo->getType()->castAs<ObjCObjectType>()->getInterface();

    if ((FirstSuperClass && SecondSuperClass &&
         FirstSuperClass->getODRHash() != SecondSuperClass->getODRHash()) ||
        (FirstSuperClass && !SecondSuperClass) ||
        (!FirstSuperClass && SecondSuperClass)) {
      QualType FirstType;
      if (FirstSuperInfo)
        FirstType = FirstSuperInfo->getType();

      DiagError(FirstID->getLocation(),
                GetSuperClassSourceRange(FirstSuperInfo, FirstID),
                SuperClassType)
          << (bool)FirstSuperInfo << FirstType;

      QualType SecondType;
      if (SecondSuperInfo)
        SecondType = SecondSuperInfo->getType();

      DiagNote(SecondID->getLocation(),
               GetSuperClassSourceRange(SecondSuperInfo, SecondID),
               SuperClassType)
          << (bool)SecondSuperInfo << SecondType;
      return true;
    }

    // Check both interfaces reference the same protocols.
    auto &FirstProtos = FirstID->getReferencedProtocols();
    auto &SecondProtos = SecondDD->ReferencedProtocols;
    if (diagnoseSubMismatchProtocols(FirstProtos, FirstID, FirstModule,
                                     SecondProtos, SecondID, SecondModule))
      return true;
  }

  auto PopulateHashes = [](DeclHashes &Hashes, const ObjCInterfaceDecl *ID,
                           const DeclContext *DC) {
    for (auto *D : ID->decls()) {
      if (!ODRHash::isSubDeclToBeProcessed(D, DC))
        continue;
      Hashes.emplace_back(D, computeODRHash(D));
    }
  };

  DeclHashes FirstHashes;
  DeclHashes SecondHashes;
  // Use definition as DeclContext because definitions are merged when
  // DeclContexts are merged and separate when DeclContexts are separate.
  PopulateHashes(FirstHashes, FirstID, FirstID->getDefinition());
  PopulateHashes(SecondHashes, SecondID, SecondID->getDefinition());

  DiffResult DR = FindTypeDiffs(FirstHashes, SecondHashes);
  ODRMismatchDecl FirstDiffType = DR.FirstDiffType;
  ODRMismatchDecl SecondDiffType = DR.SecondDiffType;
  const Decl *FirstDecl = DR.FirstDecl;
  const Decl *SecondDecl = DR.SecondDecl;

  if (FirstDiffType == Other || SecondDiffType == Other) {
    diagnoseSubMismatchUnexpected(DR, FirstID, FirstModule, SecondID,
                                  SecondModule);
    return true;
  }

  if (FirstDiffType != SecondDiffType) {
    diagnoseSubMismatchDifferentDeclKinds(DR, FirstID, FirstModule, SecondID,
                                          SecondModule);
    return true;
  }

  assert(FirstDiffType == SecondDiffType);
  switch (FirstDiffType) {
  // Already handled.
  case EndOfClass:
  case Other:
  // Cannot be contained by ObjCInterfaceDecl, invalid in this context.
  case Field:
  case TypeDef:
  case Var:
  // C++ only, invalid in this context.
  case PublicSpecifer:
  case PrivateSpecifer:
  case ProtectedSpecifer:
  case StaticAssert:
  case CXXMethod:
  case TypeAlias:
  case Friend:
  case FunctionTemplate:
    llvm_unreachable("Invalid diff type");

  case ObjCMethod: {
    if (diagnoseSubMismatchObjCMethod(FirstID, FirstModule, SecondModule,
                                      cast<ObjCMethodDecl>(FirstDecl),
                                      cast<ObjCMethodDecl>(SecondDecl)))
      return true;
    break;
  }
  case ObjCIvar: {
    if (diagnoseSubMismatchField(FirstID, FirstModule, SecondModule,
                                 cast<FieldDecl>(FirstDecl),
                                 cast<FieldDecl>(SecondDecl)))
      return true;

    // Check if the access match.
    const ObjCIvarDecl *FirstIvar = cast<ObjCIvarDecl>(FirstDecl);
    const ObjCIvarDecl *SecondIvar = cast<ObjCIvarDecl>(SecondDecl);
    if (FirstIvar->getCanonicalAccessControl() !=
        SecondIvar->getCanonicalAccessControl()) {
      DiagError(FirstIvar->getLocation(), FirstIvar->getSourceRange(),
                IVarAccess)
          << FirstIvar->getName()
          << (int)FirstIvar->getCanonicalAccessControl();
      DiagNote(SecondIvar->getLocation(), SecondIvar->getSourceRange(),
               IVarAccess)
          << SecondIvar->getName()
          << (int)SecondIvar->getCanonicalAccessControl();
      return true;
    }
    break;
  }
  case ObjCProperty: {
    if (diagnoseSubMismatchObjCProperty(FirstID, FirstModule, SecondModule,
                                        cast<ObjCPropertyDecl>(FirstDecl),
                                        cast<ObjCPropertyDecl>(SecondDecl)))
      return true;
    break;
  }
  }

  Diag(FirstDecl->getLocation(),
       diag::err_module_odr_violation_mismatch_decl_unknown)
      << FirstID << FirstModule.empty() << FirstModule << FirstDiffType
      << FirstDecl->getSourceRange();
  Diag(SecondDecl->getLocation(),
       diag::note_module_odr_violation_mismatch_decl_unknown)
      << SecondModule.empty() << SecondModule << FirstDiffType
      << SecondDecl->getSourceRange();
  return true;
}

bool ODRDiagsEmitter::diagnoseMismatch(
    const ObjCProtocolDecl *FirstProtocol,
    const ObjCProtocolDecl *SecondProtocol,
    const struct ObjCProtocolDecl::DefinitionData *SecondDD) const {
  if (FirstProtocol == SecondProtocol)
    return false;

  std::string FirstModule = getOwningModuleNameForDiagnostic(FirstProtocol);
  std::string SecondModule = getOwningModuleNameForDiagnostic(SecondProtocol);

  const ObjCProtocolDecl::DefinitionData *FirstDD = &FirstProtocol->data();
  assert(FirstDD && SecondDD && "Definitions without DefinitionData");
  // Diagnostics from ObjCProtocol DefinitionData are emitted here.
  if (FirstDD != SecondDD) {
    // Check both protocols reference the same protocols.
    const ObjCProtocolList &FirstProtocols =
        FirstProtocol->getReferencedProtocols();
    const ObjCProtocolList &SecondProtocols = SecondDD->ReferencedProtocols;
    if (diagnoseSubMismatchProtocols(FirstProtocols, FirstProtocol, FirstModule,
                                     SecondProtocols, SecondProtocol,
                                     SecondModule))
      return true;
  }

  auto PopulateHashes = [](DeclHashes &Hashes, const ObjCProtocolDecl *ID,
                           const DeclContext *DC) {
    for (const Decl *D : ID->decls()) {
      if (!ODRHash::isSubDeclToBeProcessed(D, DC))
        continue;
      Hashes.emplace_back(D, computeODRHash(D));
    }
  };

  DeclHashes FirstHashes;
  DeclHashes SecondHashes;
  // Use definition as DeclContext because definitions are merged when
  // DeclContexts are merged and separate when DeclContexts are separate.
  PopulateHashes(FirstHashes, FirstProtocol, FirstProtocol->getDefinition());
  PopulateHashes(SecondHashes, SecondProtocol, SecondProtocol->getDefinition());

  DiffResult DR = FindTypeDiffs(FirstHashes, SecondHashes);
  ODRMismatchDecl FirstDiffType = DR.FirstDiffType;
  ODRMismatchDecl SecondDiffType = DR.SecondDiffType;
  const Decl *FirstDecl = DR.FirstDecl;
  const Decl *SecondDecl = DR.SecondDecl;

  if (FirstDiffType == Other || SecondDiffType == Other) {
    diagnoseSubMismatchUnexpected(DR, FirstProtocol, FirstModule,
                                  SecondProtocol, SecondModule);
    return true;
  }

  if (FirstDiffType != SecondDiffType) {
    diagnoseSubMismatchDifferentDeclKinds(DR, FirstProtocol, FirstModule,
                                          SecondProtocol, SecondModule);
    return true;
  }

  assert(FirstDiffType == SecondDiffType);
  switch (FirstDiffType) {
  // Already handled.
  case EndOfClass:
  case Other:
  // Cannot be contained by ObjCProtocolDecl, invalid in this context.
  case Field:
  case TypeDef:
  case Var:
  case ObjCIvar:
  // C++ only, invalid in this context.
  case PublicSpecifer:
  case PrivateSpecifer:
  case ProtectedSpecifer:
  case StaticAssert:
  case CXXMethod:
  case TypeAlias:
  case Friend:
  case FunctionTemplate:
    llvm_unreachable("Invalid diff type");
  case ObjCMethod: {
    if (diagnoseSubMismatchObjCMethod(FirstProtocol, FirstModule, SecondModule,
                                      cast<ObjCMethodDecl>(FirstDecl),
                                      cast<ObjCMethodDecl>(SecondDecl)))
      return true;
    break;
  }
  case ObjCProperty: {
    if (diagnoseSubMismatchObjCProperty(FirstProtocol, FirstModule,
                                        SecondModule,
                                        cast<ObjCPropertyDecl>(FirstDecl),
                                        cast<ObjCPropertyDecl>(SecondDecl)))
      return true;
    break;
  }
  }

  Diag(FirstDecl->getLocation(),
       diag::err_module_odr_violation_mismatch_decl_unknown)
      << FirstProtocol << FirstModule.empty() << FirstModule << FirstDiffType
      << FirstDecl->getSourceRange();
  Diag(SecondDecl->getLocation(),
       diag::note_module_odr_violation_mismatch_decl_unknown)
      << SecondModule.empty() << SecondModule << FirstDiffType
      << SecondDecl->getSourceRange();
  return true;
}
