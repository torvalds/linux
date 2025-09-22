//===------ SemaSwift.cpp ------ Swift language-specific routines ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to Swift.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaSwift.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaObjC.h"

namespace clang {
SemaSwift::SemaSwift(Sema &S) : SemaBase(S) {}

SwiftNameAttr *SemaSwift::mergeNameAttr(Decl *D, const SwiftNameAttr &SNA,
                                        StringRef Name) {
  if (const auto *PrevSNA = D->getAttr<SwiftNameAttr>()) {
    if (PrevSNA->getName() != Name && !PrevSNA->isImplicit()) {
      Diag(PrevSNA->getLocation(), diag::err_attributes_are_not_compatible)
          << PrevSNA << &SNA
          << (PrevSNA->isRegularKeywordAttribute() ||
              SNA.isRegularKeywordAttribute());
      Diag(SNA.getLoc(), diag::note_conflicting_attribute);
    }

    D->dropAttr<SwiftNameAttr>();
  }
  return ::new (getASTContext()) SwiftNameAttr(getASTContext(), SNA, Name);
}

/// Pointer-like types in the default address space.
static bool isValidSwiftContextType(QualType Ty) {
  if (!Ty->hasPointerRepresentation())
    return Ty->isDependentType();
  return Ty->getPointeeType().getAddressSpace() == LangAS::Default;
}

/// Pointers and references in the default address space.
static bool isValidSwiftIndirectResultType(QualType Ty) {
  if (const auto *PtrType = Ty->getAs<PointerType>()) {
    Ty = PtrType->getPointeeType();
  } else if (const auto *RefType = Ty->getAs<ReferenceType>()) {
    Ty = RefType->getPointeeType();
  } else {
    return Ty->isDependentType();
  }
  return Ty.getAddressSpace() == LangAS::Default;
}

/// Pointers and references to pointers in the default address space.
static bool isValidSwiftErrorResultType(QualType Ty) {
  if (const auto *PtrType = Ty->getAs<PointerType>()) {
    Ty = PtrType->getPointeeType();
  } else if (const auto *RefType = Ty->getAs<ReferenceType>()) {
    Ty = RefType->getPointeeType();
  } else {
    return Ty->isDependentType();
  }
  if (!Ty.getQualifiers().empty())
    return false;
  return isValidSwiftContextType(Ty);
}

void SemaSwift::handleAttrAttr(Decl *D, const ParsedAttr &AL) {
  // Make sure that there is a string literal as the annotation's single
  // argument.
  StringRef Str;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  D->addAttr(::new (getASTContext()) SwiftAttrAttr(getASTContext(), AL, Str));
}

void SemaSwift::handleBridge(Decl *D, const ParsedAttr &AL) {
  // Make sure that there is a string literal as the annotation's single
  // argument.
  StringRef BT;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, BT))
    return;

  // Warn about duplicate attributes if they have different arguments, but drop
  // any duplicate attributes regardless.
  if (const auto *Other = D->getAttr<SwiftBridgeAttr>()) {
    if (Other->getSwiftType() != BT)
      Diag(AL.getLoc(), diag::warn_duplicate_attribute) << AL;
    return;
  }

  D->addAttr(::new (getASTContext()) SwiftBridgeAttr(getASTContext(), AL, BT));
}

static bool isErrorParameter(Sema &S, QualType QT) {
  const auto *PT = QT->getAs<PointerType>();
  if (!PT)
    return false;

  QualType Pointee = PT->getPointeeType();

  // Check for NSError**.
  if (const auto *OPT = Pointee->getAs<ObjCObjectPointerType>())
    if (const auto *ID = OPT->getInterfaceDecl())
      if (ID->getIdentifier() == S.ObjC().getNSErrorIdent())
        return true;

  // Check for CFError**.
  if (const auto *PT = Pointee->getAs<PointerType>())
    if (const auto *RT = PT->getPointeeType()->getAs<RecordType>())
      if (S.ObjC().isCFError(RT->getDecl()))
        return true;

  return false;
}

void SemaSwift::handleError(Decl *D, const ParsedAttr &AL) {
  auto hasErrorParameter = [](Sema &S, Decl *D, const ParsedAttr &AL) -> bool {
    for (unsigned I = 0, E = getFunctionOrMethodNumParams(D); I != E; ++I) {
      if (isErrorParameter(S, getFunctionOrMethodParamType(D, I)))
        return true;
    }

    S.Diag(AL.getLoc(), diag::err_attr_swift_error_no_error_parameter)
        << AL << isa<ObjCMethodDecl>(D);
    return false;
  };

  auto hasPointerResult = [](Sema &S, Decl *D, const ParsedAttr &AL) -> bool {
    // - C, ObjC, and block pointers are definitely okay.
    // - References are definitely not okay.
    // - nullptr_t is weird, but acceptable.
    QualType RT = getFunctionOrMethodResultType(D);
    if (RT->hasPointerRepresentation() && !RT->isReferenceType())
      return true;

    S.Diag(AL.getLoc(), diag::err_attr_swift_error_return_type)
        << AL << AL.getArgAsIdent(0)->Ident->getName() << isa<ObjCMethodDecl>(D)
        << /*pointer*/ 1;
    return false;
  };

  auto hasIntegerResult = [](Sema &S, Decl *D, const ParsedAttr &AL) -> bool {
    QualType RT = getFunctionOrMethodResultType(D);
    if (RT->isIntegralType(S.Context))
      return true;

    S.Diag(AL.getLoc(), diag::err_attr_swift_error_return_type)
        << AL << AL.getArgAsIdent(0)->Ident->getName() << isa<ObjCMethodDecl>(D)
        << /*integral*/ 0;
    return false;
  };

  if (D->isInvalidDecl())
    return;

  IdentifierLoc *Loc = AL.getArgAsIdent(0);
  SwiftErrorAttr::ConventionKind Convention;
  if (!SwiftErrorAttr::ConvertStrToConventionKind(Loc->Ident->getName(),
                                                  Convention)) {
    Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << Loc->Ident;
    return;
  }

  switch (Convention) {
  case SwiftErrorAttr::None:
    // No additional validation required.
    break;

  case SwiftErrorAttr::NonNullError:
    if (!hasErrorParameter(SemaRef, D, AL))
      return;
    break;

  case SwiftErrorAttr::NullResult:
    if (!hasErrorParameter(SemaRef, D, AL) || !hasPointerResult(SemaRef, D, AL))
      return;
    break;

  case SwiftErrorAttr::NonZeroResult:
  case SwiftErrorAttr::ZeroResult:
    if (!hasErrorParameter(SemaRef, D, AL) || !hasIntegerResult(SemaRef, D, AL))
      return;
    break;
  }

  D->addAttr(::new (getASTContext())
                 SwiftErrorAttr(getASTContext(), AL, Convention));
}

static void checkSwiftAsyncErrorBlock(Sema &S, Decl *D,
                                      const SwiftAsyncErrorAttr *ErrorAttr,
                                      const SwiftAsyncAttr *AsyncAttr) {
  if (AsyncAttr->getKind() == SwiftAsyncAttr::None) {
    if (ErrorAttr->getConvention() != SwiftAsyncErrorAttr::None) {
      S.Diag(AsyncAttr->getLocation(),
             diag::err_swift_async_error_without_swift_async)
          << AsyncAttr << isa<ObjCMethodDecl>(D);
    }
    return;
  }

  const ParmVarDecl *HandlerParam = getFunctionOrMethodParam(
      D, AsyncAttr->getCompletionHandlerIndex().getASTIndex());
  // handleSwiftAsyncAttr already verified the type is correct, so no need to
  // double-check it here.
  const auto *FuncTy = HandlerParam->getType()
                           ->castAs<BlockPointerType>()
                           ->getPointeeType()
                           ->getAs<FunctionProtoType>();
  ArrayRef<QualType> BlockParams;
  if (FuncTy)
    BlockParams = FuncTy->getParamTypes();

  switch (ErrorAttr->getConvention()) {
  case SwiftAsyncErrorAttr::ZeroArgument:
  case SwiftAsyncErrorAttr::NonZeroArgument: {
    uint32_t ParamIdx = ErrorAttr->getHandlerParamIdx();
    if (ParamIdx == 0 || ParamIdx > BlockParams.size()) {
      S.Diag(ErrorAttr->getLocation(),
             diag::err_attribute_argument_out_of_bounds)
          << ErrorAttr << 2;
      return;
    }
    QualType ErrorParam = BlockParams[ParamIdx - 1];
    if (!ErrorParam->isIntegralType(S.Context)) {
      StringRef ConvStr =
          ErrorAttr->getConvention() == SwiftAsyncErrorAttr::ZeroArgument
              ? "zero_argument"
              : "nonzero_argument";
      S.Diag(ErrorAttr->getLocation(), diag::err_swift_async_error_non_integral)
          << ErrorAttr << ConvStr << ParamIdx << ErrorParam;
      return;
    }
    break;
  }
  case SwiftAsyncErrorAttr::NonNullError: {
    bool AnyErrorParams = false;
    for (QualType Param : BlockParams) {
      // Check for NSError *.
      if (const auto *ObjCPtrTy = Param->getAs<ObjCObjectPointerType>()) {
        if (const auto *ID = ObjCPtrTy->getInterfaceDecl()) {
          if (ID->getIdentifier() == S.ObjC().getNSErrorIdent()) {
            AnyErrorParams = true;
            break;
          }
        }
      }
      // Check for CFError *.
      if (const auto *PtrTy = Param->getAs<PointerType>()) {
        if (const auto *RT = PtrTy->getPointeeType()->getAs<RecordType>()) {
          if (S.ObjC().isCFError(RT->getDecl())) {
            AnyErrorParams = true;
            break;
          }
        }
      }
    }

    if (!AnyErrorParams) {
      S.Diag(ErrorAttr->getLocation(),
             diag::err_swift_async_error_no_error_parameter)
          << ErrorAttr << isa<ObjCMethodDecl>(D);
      return;
    }
    break;
  }
  case SwiftAsyncErrorAttr::None:
    break;
  }
}

void SemaSwift::handleAsyncError(Decl *D, const ParsedAttr &AL) {
  IdentifierLoc *IDLoc = AL.getArgAsIdent(0);
  SwiftAsyncErrorAttr::ConventionKind ConvKind;
  if (!SwiftAsyncErrorAttr::ConvertStrToConventionKind(IDLoc->Ident->getName(),
                                                       ConvKind)) {
    Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << IDLoc->Ident;
    return;
  }

  uint32_t ParamIdx = 0;
  switch (ConvKind) {
  case SwiftAsyncErrorAttr::ZeroArgument:
  case SwiftAsyncErrorAttr::NonZeroArgument: {
    if (!AL.checkExactlyNumArgs(SemaRef, 2))
      return;

    Expr *IdxExpr = AL.getArgAsExpr(1);
    if (!SemaRef.checkUInt32Argument(AL, IdxExpr, ParamIdx))
      return;
    break;
  }
  case SwiftAsyncErrorAttr::NonNullError:
  case SwiftAsyncErrorAttr::None: {
    if (!AL.checkExactlyNumArgs(SemaRef, 1))
      return;
    break;
  }
  }

  auto *ErrorAttr = ::new (getASTContext())
      SwiftAsyncErrorAttr(getASTContext(), AL, ConvKind, ParamIdx);
  D->addAttr(ErrorAttr);

  if (auto *AsyncAttr = D->getAttr<SwiftAsyncAttr>())
    checkSwiftAsyncErrorBlock(SemaRef, D, ErrorAttr, AsyncAttr);
}

// For a function, this will validate a compound Swift name, e.g.
// <code>init(foo:bar:baz:)</code> or <code>controllerForName(_:)</code>, and
// the function will output the number of parameter names, and whether this is a
// single-arg initializer.
//
// For a type, enum constant, property, or variable declaration, this will
// validate either a simple identifier, or a qualified
// <code>context.identifier</code> name.
static bool validateSwiftFunctionName(Sema &S, const ParsedAttr &AL,
                                      SourceLocation Loc, StringRef Name,
                                      unsigned &SwiftParamCount,
                                      bool &IsSingleParamInit) {
  SwiftParamCount = 0;
  IsSingleParamInit = false;

  // Check whether this will be mapped to a getter or setter of a property.
  bool IsGetter = false, IsSetter = false;
  if (Name.consume_front("getter:"))
    IsGetter = true;
  else if (Name.consume_front("setter:"))
    IsSetter = true;

  if (Name.back() != ')') {
    S.Diag(Loc, diag::warn_attr_swift_name_function) << AL;
    return false;
  }

  bool IsMember = false;
  StringRef ContextName, BaseName, Parameters;

  std::tie(BaseName, Parameters) = Name.split('(');

  // Split at the first '.', if it exists, which separates the context name
  // from the base name.
  std::tie(ContextName, BaseName) = BaseName.split('.');
  if (BaseName.empty()) {
    BaseName = ContextName;
    ContextName = StringRef();
  } else if (ContextName.empty() || !isValidAsciiIdentifier(ContextName)) {
    S.Diag(Loc, diag::warn_attr_swift_name_invalid_identifier)
        << AL << /*context*/ 1;
    return false;
  } else {
    IsMember = true;
  }

  if (!isValidAsciiIdentifier(BaseName) || BaseName == "_") {
    S.Diag(Loc, diag::warn_attr_swift_name_invalid_identifier)
        << AL << /*basename*/ 0;
    return false;
  }

  bool IsSubscript = BaseName == "subscript";
  // A subscript accessor must be a getter or setter.
  if (IsSubscript && !IsGetter && !IsSetter) {
    S.Diag(Loc, diag::warn_attr_swift_name_subscript_invalid_parameter)
        << AL << /* getter or setter */ 0;
    return false;
  }

  if (Parameters.empty()) {
    S.Diag(Loc, diag::warn_attr_swift_name_missing_parameters) << AL;
    return false;
  }

  assert(Parameters.back() == ')' && "expected ')'");
  Parameters = Parameters.drop_back(); // ')'

  if (Parameters.empty()) {
    // Setters and subscripts must have at least one parameter.
    if (IsSubscript) {
      S.Diag(Loc, diag::warn_attr_swift_name_subscript_invalid_parameter)
          << AL << /* have at least one parameter */ 1;
      return false;
    }

    if (IsSetter) {
      S.Diag(Loc, diag::warn_attr_swift_name_setter_parameters) << AL;
      return false;
    }

    return true;
  }

  if (Parameters.back() != ':') {
    S.Diag(Loc, diag::warn_attr_swift_name_function) << AL;
    return false;
  }

  StringRef CurrentParam;
  std::optional<unsigned> SelfLocation;
  unsigned NewValueCount = 0;
  std::optional<unsigned> NewValueLocation;
  do {
    std::tie(CurrentParam, Parameters) = Parameters.split(':');

    if (!isValidAsciiIdentifier(CurrentParam)) {
      S.Diag(Loc, diag::warn_attr_swift_name_invalid_identifier)
          << AL << /*parameter*/ 2;
      return false;
    }

    if (IsMember && CurrentParam == "self") {
      // "self" indicates the "self" argument for a member.

      // More than one "self"?
      if (SelfLocation) {
        S.Diag(Loc, diag::warn_attr_swift_name_multiple_selfs) << AL;
        return false;
      }

      // The "self" location is the current parameter.
      SelfLocation = SwiftParamCount;
    } else if (CurrentParam == "newValue") {
      // "newValue" indicates the "newValue" argument for a setter.

      // There should only be one 'newValue', but it's only significant for
      // subscript accessors, so don't error right away.
      ++NewValueCount;

      NewValueLocation = SwiftParamCount;
    }

    ++SwiftParamCount;
  } while (!Parameters.empty());

  // Only instance subscripts are currently supported.
  if (IsSubscript && !SelfLocation) {
    S.Diag(Loc, diag::warn_attr_swift_name_subscript_invalid_parameter)
        << AL << /*have a 'self:' parameter*/ 2;
    return false;
  }

  IsSingleParamInit =
      SwiftParamCount == 1 && BaseName == "init" && CurrentParam != "_";

  // Check the number of parameters for a getter/setter.
  if (IsGetter || IsSetter) {
    // Setters have one parameter for the new value.
    unsigned NumExpectedParams = IsGetter ? 0 : 1;
    unsigned ParamDiag = IsGetter
                             ? diag::warn_attr_swift_name_getter_parameters
                             : diag::warn_attr_swift_name_setter_parameters;

    // Instance methods have one parameter for "self".
    if (SelfLocation)
      ++NumExpectedParams;

    // Subscripts may have additional parameters beyond the expected params for
    // the index.
    if (IsSubscript) {
      if (SwiftParamCount < NumExpectedParams) {
        S.Diag(Loc, ParamDiag) << AL;
        return false;
      }

      // A subscript setter must explicitly label its newValue parameter to
      // distinguish it from index parameters.
      if (IsSetter) {
        if (!NewValueLocation) {
          S.Diag(Loc, diag::warn_attr_swift_name_subscript_setter_no_newValue)
              << AL;
          return false;
        }
        if (NewValueCount > 1) {
          S.Diag(Loc,
                 diag::warn_attr_swift_name_subscript_setter_multiple_newValues)
              << AL;
          return false;
        }
      } else {
        // Subscript getters should have no 'newValue:' parameter.
        if (NewValueLocation) {
          S.Diag(Loc, diag::warn_attr_swift_name_subscript_getter_newValue)
              << AL;
          return false;
        }
      }
    } else {
      // Property accessors must have exactly the number of expected params.
      if (SwiftParamCount != NumExpectedParams) {
        S.Diag(Loc, ParamDiag) << AL;
        return false;
      }
    }
  }

  return true;
}

bool SemaSwift::DiagnoseName(Decl *D, StringRef Name, SourceLocation Loc,
                             const ParsedAttr &AL, bool IsAsync) {
  if (isa<ObjCMethodDecl>(D) || isa<FunctionDecl>(D)) {
    ArrayRef<ParmVarDecl *> Params;
    unsigned ParamCount;

    if (const auto *Method = dyn_cast<ObjCMethodDecl>(D)) {
      ParamCount = Method->getSelector().getNumArgs();
      Params = Method->parameters().slice(0, ParamCount);
    } else {
      const auto *F = cast<FunctionDecl>(D);

      ParamCount = F->getNumParams();
      Params = F->parameters();

      if (!F->hasWrittenPrototype()) {
        Diag(Loc, diag::warn_attribute_wrong_decl_type)
            << AL << AL.isRegularKeywordAttribute()
            << ExpectedFunctionWithProtoType;
        return false;
      }
    }

    // The async name drops the last callback parameter.
    if (IsAsync) {
      if (ParamCount == 0) {
        Diag(Loc, diag::warn_attr_swift_name_decl_missing_params)
            << AL << isa<ObjCMethodDecl>(D);
        return false;
      }
      ParamCount -= 1;
    }

    unsigned SwiftParamCount;
    bool IsSingleParamInit;
    if (!validateSwiftFunctionName(SemaRef, AL, Loc, Name, SwiftParamCount,
                                   IsSingleParamInit))
      return false;

    bool ParamCountValid;
    if (SwiftParamCount == ParamCount) {
      ParamCountValid = true;
    } else if (SwiftParamCount > ParamCount) {
      ParamCountValid = IsSingleParamInit && ParamCount == 0;
    } else {
      // We have fewer Swift parameters than Objective-C parameters, but that
      // might be because we've transformed some of them. Check for potential
      // "out" parameters and err on the side of not warning.
      unsigned MaybeOutParamCount =
          llvm::count_if(Params, [](const ParmVarDecl *Param) -> bool {
            QualType ParamTy = Param->getType();
            if (ParamTy->isReferenceType() || ParamTy->isPointerType())
              return !ParamTy->getPointeeType().isConstQualified();
            return false;
          });

      ParamCountValid = SwiftParamCount + MaybeOutParamCount >= ParamCount;
    }

    if (!ParamCountValid) {
      Diag(Loc, diag::warn_attr_swift_name_num_params)
          << (SwiftParamCount > ParamCount) << AL << ParamCount
          << SwiftParamCount;
      return false;
    }
  } else if ((isa<EnumConstantDecl>(D) || isa<ObjCProtocolDecl>(D) ||
              isa<ObjCInterfaceDecl>(D) || isa<ObjCPropertyDecl>(D) ||
              isa<VarDecl>(D) || isa<TypedefNameDecl>(D) || isa<TagDecl>(D) ||
              isa<IndirectFieldDecl>(D) || isa<FieldDecl>(D)) &&
             !IsAsync) {
    StringRef ContextName, BaseName;

    std::tie(ContextName, BaseName) = Name.split('.');
    if (BaseName.empty()) {
      BaseName = ContextName;
      ContextName = StringRef();
    } else if (!isValidAsciiIdentifier(ContextName)) {
      Diag(Loc, diag::warn_attr_swift_name_invalid_identifier)
          << AL << /*context*/ 1;
      return false;
    }

    if (!isValidAsciiIdentifier(BaseName)) {
      Diag(Loc, diag::warn_attr_swift_name_invalid_identifier)
          << AL << /*basename*/ 0;
      return false;
    }
  } else {
    Diag(Loc, diag::warn_attr_swift_name_decl_kind) << AL;
    return false;
  }
  return true;
}

void SemaSwift::handleName(Decl *D, const ParsedAttr &AL) {
  StringRef Name;
  SourceLocation Loc;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Name, &Loc))
    return;

  if (!DiagnoseName(D, Name, Loc, AL, /*IsAsync=*/false))
    return;

  D->addAttr(::new (getASTContext()) SwiftNameAttr(getASTContext(), AL, Name));
}

void SemaSwift::handleAsyncName(Decl *D, const ParsedAttr &AL) {
  StringRef Name;
  SourceLocation Loc;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Name, &Loc))
    return;

  if (!DiagnoseName(D, Name, Loc, AL, /*IsAsync=*/true))
    return;

  D->addAttr(::new (getASTContext())
                 SwiftAsyncNameAttr(getASTContext(), AL, Name));
}

void SemaSwift::handleNewType(Decl *D, const ParsedAttr &AL) {
  // Make sure that there is an identifier as the annotation's single argument.
  if (!AL.checkExactlyNumArgs(SemaRef, 1))
    return;

  if (!AL.isArgIdent(0)) {
    Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  SwiftNewTypeAttr::NewtypeKind Kind;
  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  if (!SwiftNewTypeAttr::ConvertStrToNewtypeKind(II->getName(), Kind)) {
    Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << II;
    return;
  }

  if (!isa<TypedefNameDecl>(D)) {
    Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type_str)
        << AL << AL.isRegularKeywordAttribute() << "typedefs";
    return;
  }

  D->addAttr(::new (getASTContext())
                 SwiftNewTypeAttr(getASTContext(), AL, Kind));
}

void SemaSwift::handleAsyncAttr(Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  SwiftAsyncAttr::Kind Kind;
  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  if (!SwiftAsyncAttr::ConvertStrToKind(II->getName(), Kind)) {
    Diag(AL.getLoc(), diag::err_swift_async_no_access) << AL << II;
    return;
  }

  ParamIdx Idx;
  if (Kind == SwiftAsyncAttr::None) {
    // If this is 'none', then there shouldn't be any additional arguments.
    if (!AL.checkExactlyNumArgs(SemaRef, 1))
      return;
  } else {
    // Non-none swift_async requires a completion handler index argument.
    if (!AL.checkExactlyNumArgs(SemaRef, 2))
      return;

    Expr *HandlerIdx = AL.getArgAsExpr(1);
    if (!SemaRef.checkFunctionOrMethodParameterIndex(D, AL, 2, HandlerIdx, Idx))
      return;

    const ParmVarDecl *CompletionBlock =
        getFunctionOrMethodParam(D, Idx.getASTIndex());
    QualType CompletionBlockType = CompletionBlock->getType();
    if (!CompletionBlockType->isBlockPointerType()) {
      Diag(CompletionBlock->getLocation(), diag::err_swift_async_bad_block_type)
          << CompletionBlock->getType();
      return;
    }
    QualType BlockTy =
        CompletionBlockType->castAs<BlockPointerType>()->getPointeeType();
    if (!BlockTy->castAs<FunctionType>()->getReturnType()->isVoidType()) {
      Diag(CompletionBlock->getLocation(), diag::err_swift_async_bad_block_type)
          << CompletionBlock->getType();
      return;
    }
  }

  auto *AsyncAttr =
      ::new (getASTContext()) SwiftAsyncAttr(getASTContext(), AL, Kind, Idx);
  D->addAttr(AsyncAttr);

  if (auto *ErrorAttr = D->getAttr<SwiftAsyncErrorAttr>())
    checkSwiftAsyncErrorBlock(SemaRef, D, ErrorAttr, AsyncAttr);
}

void SemaSwift::AddParameterABIAttr(Decl *D, const AttributeCommonInfo &CI,
                                    ParameterABI abi) {
  ASTContext &Context = getASTContext();
  QualType type = cast<ParmVarDecl>(D)->getType();

  if (auto existingAttr = D->getAttr<ParameterABIAttr>()) {
    if (existingAttr->getABI() != abi) {
      Diag(CI.getLoc(), diag::err_attributes_are_not_compatible)
          << getParameterABISpelling(abi) << existingAttr
          << (CI.isRegularKeywordAttribute() ||
              existingAttr->isRegularKeywordAttribute());
      Diag(existingAttr->getLocation(), diag::note_conflicting_attribute);
      return;
    }
  }

  switch (abi) {
  case ParameterABI::Ordinary:
    llvm_unreachable("explicit attribute for ordinary parameter ABI?");

  case ParameterABI::SwiftContext:
    if (!isValidSwiftContextType(type)) {
      Diag(CI.getLoc(), diag::err_swift_abi_parameter_wrong_type)
          << getParameterABISpelling(abi) << /*pointer to pointer */ 0 << type;
    }
    D->addAttr(::new (Context) SwiftContextAttr(Context, CI));
    return;

  case ParameterABI::SwiftAsyncContext:
    if (!isValidSwiftContextType(type)) {
      Diag(CI.getLoc(), diag::err_swift_abi_parameter_wrong_type)
          << getParameterABISpelling(abi) << /*pointer to pointer */ 0 << type;
    }
    D->addAttr(::new (Context) SwiftAsyncContextAttr(Context, CI));
    return;

  case ParameterABI::SwiftErrorResult:
    if (!isValidSwiftErrorResultType(type)) {
      Diag(CI.getLoc(), diag::err_swift_abi_parameter_wrong_type)
          << getParameterABISpelling(abi) << /*pointer to pointer */ 1 << type;
    }
    D->addAttr(::new (Context) SwiftErrorResultAttr(Context, CI));
    return;

  case ParameterABI::SwiftIndirectResult:
    if (!isValidSwiftIndirectResultType(type)) {
      Diag(CI.getLoc(), diag::err_swift_abi_parameter_wrong_type)
          << getParameterABISpelling(abi) << /*pointer*/ 0 << type;
    }
    D->addAttr(::new (Context) SwiftIndirectResultAttr(Context, CI));
    return;
  }
  llvm_unreachable("bad parameter ABI attribute");
}

} // namespace clang
