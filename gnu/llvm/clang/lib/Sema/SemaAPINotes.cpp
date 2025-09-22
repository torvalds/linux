//===--- SemaAPINotes.cpp - API Notes Handling ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the mapping from API notes to declaration attributes.
//
//===----------------------------------------------------------------------===//

#include "clang/APINotes/APINotesReader.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaSwift.h"
#include <stack>

using namespace clang;

namespace {
enum class IsActive_t : bool { Inactive, Active };
enum class IsSubstitution_t : bool { Original, Replacement };

struct VersionedInfoMetadata {
  /// An empty version refers to unversioned metadata.
  VersionTuple Version;
  unsigned IsActive : 1;
  unsigned IsReplacement : 1;

  VersionedInfoMetadata(VersionTuple Version, IsActive_t Active,
                        IsSubstitution_t Replacement)
      : Version(Version), IsActive(Active == IsActive_t::Active),
        IsReplacement(Replacement == IsSubstitution_t::Replacement) {}
};
} // end anonymous namespace

/// Determine whether this is a multi-level pointer type.
static bool isIndirectPointerType(QualType Type) {
  QualType Pointee = Type->getPointeeType();
  if (Pointee.isNull())
    return false;

  return Pointee->isAnyPointerType() || Pointee->isObjCObjectPointerType() ||
         Pointee->isMemberPointerType();
}

/// Apply nullability to the given declaration.
static void applyNullability(Sema &S, Decl *D, NullabilityKind Nullability,
                             VersionedInfoMetadata Metadata) {
  if (!Metadata.IsActive)
    return;

  auto GetModified =
      [&](Decl *D, QualType QT,
          NullabilityKind Nullability) -> std::optional<QualType> {
    QualType Original = QT;
    S.CheckImplicitNullabilityTypeSpecifier(QT, Nullability, D->getLocation(),
                                            isa<ParmVarDecl>(D),
                                            /*OverrideExisting=*/true);
    return (QT.getTypePtr() != Original.getTypePtr()) ? std::optional(QT)
                                                      : std::nullopt;
  };

  if (auto Function = dyn_cast<FunctionDecl>(D)) {
    if (auto Modified =
            GetModified(D, Function->getReturnType(), Nullability)) {
      const FunctionType *FnType = Function->getType()->castAs<FunctionType>();
      if (const FunctionProtoType *proto = dyn_cast<FunctionProtoType>(FnType))
        Function->setType(S.Context.getFunctionType(
            *Modified, proto->getParamTypes(), proto->getExtProtoInfo()));
      else
        Function->setType(
            S.Context.getFunctionNoProtoType(*Modified, FnType->getExtInfo()));
    }
  } else if (auto Method = dyn_cast<ObjCMethodDecl>(D)) {
    if (auto Modified = GetModified(D, Method->getReturnType(), Nullability)) {
      Method->setReturnType(*Modified);

      // Make it a context-sensitive keyword if we can.
      if (!isIndirectPointerType(*Modified))
        Method->setObjCDeclQualifier(Decl::ObjCDeclQualifier(
            Method->getObjCDeclQualifier() | Decl::OBJC_TQ_CSNullability));
    }
  } else if (auto Value = dyn_cast<ValueDecl>(D)) {
    if (auto Modified = GetModified(D, Value->getType(), Nullability)) {
      Value->setType(*Modified);

      // Make it a context-sensitive keyword if we can.
      if (auto Parm = dyn_cast<ParmVarDecl>(D)) {
        if (Parm->isObjCMethodParameter() && !isIndirectPointerType(*Modified))
          Parm->setObjCDeclQualifier(Decl::ObjCDeclQualifier(
              Parm->getObjCDeclQualifier() | Decl::OBJC_TQ_CSNullability));
      }
    }
  } else if (auto Property = dyn_cast<ObjCPropertyDecl>(D)) {
    if (auto Modified = GetModified(D, Property->getType(), Nullability)) {
      Property->setType(*Modified, Property->getTypeSourceInfo());

      // Make it a property attribute if we can.
      if (!isIndirectPointerType(*Modified))
        Property->setPropertyAttributes(
            ObjCPropertyAttribute::kind_null_resettable);
    }
  }
}

/// Copy a string into ASTContext-allocated memory.
static StringRef ASTAllocateString(ASTContext &Ctx, StringRef String) {
  void *mem = Ctx.Allocate(String.size(), alignof(char *));
  memcpy(mem, String.data(), String.size());
  return StringRef(static_cast<char *>(mem), String.size());
}

static AttributeCommonInfo getPlaceholderAttrInfo() {
  return AttributeCommonInfo(SourceRange(),
                             AttributeCommonInfo::UnknownAttribute,
                             {AttributeCommonInfo::AS_GNU,
                              /*Spelling*/ 0, /*IsAlignas*/ false,
                              /*IsRegularKeywordAttribute*/ false});
}

namespace {
template <typename A> struct AttrKindFor {};

#define ATTR(X)                                                                \
  template <> struct AttrKindFor<X##Attr> {                                    \
    static const attr::Kind value = attr::X;                                   \
  };
#include "clang/Basic/AttrList.inc"

/// Handle an attribute introduced by API notes.
///
/// \param IsAddition Whether we should add a new attribute
/// (otherwise, we might remove an existing attribute).
/// \param CreateAttr Create the new attribute to be added.
template <typename A>
void handleAPINotedAttribute(
    Sema &S, Decl *D, bool IsAddition, VersionedInfoMetadata Metadata,
    llvm::function_ref<A *()> CreateAttr,
    llvm::function_ref<Decl::attr_iterator(const Decl *)> GetExistingAttr) {
  if (Metadata.IsActive) {
    auto Existing = GetExistingAttr(D);
    if (Existing != D->attr_end()) {
      // Remove the existing attribute, and treat it as a superseded
      // non-versioned attribute.
      auto *Versioned = SwiftVersionedAdditionAttr::CreateImplicit(
          S.Context, Metadata.Version, *Existing, /*IsReplacedByActive*/ true);

      D->getAttrs().erase(Existing);
      D->addAttr(Versioned);
    }

    // If we're supposed to add a new attribute, do so.
    if (IsAddition) {
      if (auto Attr = CreateAttr())
        D->addAttr(Attr);
    }

    return;
  }
  if (IsAddition) {
    if (auto Attr = CreateAttr()) {
      auto *Versioned = SwiftVersionedAdditionAttr::CreateImplicit(
          S.Context, Metadata.Version, Attr,
          /*IsReplacedByActive*/ Metadata.IsReplacement);
      D->addAttr(Versioned);
    }
  } else {
    // FIXME: This isn't preserving enough information for things like
    // availability, where we're trying to remove a /specific/ kind of
    // attribute.
    auto *Versioned = SwiftVersionedRemovalAttr::CreateImplicit(
        S.Context, Metadata.Version, AttrKindFor<A>::value,
        /*IsReplacedByActive*/ Metadata.IsReplacement);
    D->addAttr(Versioned);
  }
}

template <typename A>
void handleAPINotedAttribute(Sema &S, Decl *D, bool ShouldAddAttribute,
                             VersionedInfoMetadata Metadata,
                             llvm::function_ref<A *()> CreateAttr) {
  handleAPINotedAttribute<A>(
      S, D, ShouldAddAttribute, Metadata, CreateAttr, [](const Decl *D) {
        return llvm::find_if(D->attrs(),
                             [](const Attr *Next) { return isa<A>(Next); });
      });
}
} // namespace

template <typename A>
static void handleAPINotedRetainCountAttribute(Sema &S, Decl *D,
                                               bool ShouldAddAttribute,
                                               VersionedInfoMetadata Metadata) {
  // The template argument has a default to make the "removal" case more
  // concise; it doesn't matter /which/ attribute is being removed.
  handleAPINotedAttribute<A>(
      S, D, ShouldAddAttribute, Metadata,
      [&] { return new (S.Context) A(S.Context, getPlaceholderAttrInfo()); },
      [](const Decl *D) -> Decl::attr_iterator {
        return llvm::find_if(D->attrs(), [](const Attr *Next) -> bool {
          return isa<CFReturnsRetainedAttr>(Next) ||
                 isa<CFReturnsNotRetainedAttr>(Next) ||
                 isa<NSReturnsRetainedAttr>(Next) ||
                 isa<NSReturnsNotRetainedAttr>(Next) ||
                 isa<CFAuditedTransferAttr>(Next);
        });
      });
}

static void handleAPINotedRetainCountConvention(
    Sema &S, Decl *D, VersionedInfoMetadata Metadata,
    std::optional<api_notes::RetainCountConventionKind> Convention) {
  if (!Convention)
    return;
  switch (*Convention) {
  case api_notes::RetainCountConventionKind::None:
    if (isa<FunctionDecl>(D)) {
      handleAPINotedRetainCountAttribute<CFUnknownTransferAttr>(
          S, D, /*shouldAddAttribute*/ true, Metadata);
    } else {
      handleAPINotedRetainCountAttribute<CFReturnsRetainedAttr>(
          S, D, /*shouldAddAttribute*/ false, Metadata);
    }
    break;
  case api_notes::RetainCountConventionKind::CFReturnsRetained:
    handleAPINotedRetainCountAttribute<CFReturnsRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  case api_notes::RetainCountConventionKind::CFReturnsNotRetained:
    handleAPINotedRetainCountAttribute<CFReturnsNotRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  case api_notes::RetainCountConventionKind::NSReturnsRetained:
    handleAPINotedRetainCountAttribute<NSReturnsRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  case api_notes::RetainCountConventionKind::NSReturnsNotRetained:
    handleAPINotedRetainCountAttribute<NSReturnsNotRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  }
}

static void ProcessAPINotes(Sema &S, Decl *D,
                            const api_notes::CommonEntityInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Availability
  if (Info.Unavailable) {
    handleAPINotedAttribute<UnavailableAttr>(S, D, true, Metadata, [&] {
      return new (S.Context)
          UnavailableAttr(S.Context, getPlaceholderAttrInfo(),
                          ASTAllocateString(S.Context, Info.UnavailableMsg));
    });
  }

  if (Info.UnavailableInSwift) {
    handleAPINotedAttribute<AvailabilityAttr>(
        S, D, true, Metadata,
        [&] {
          return new (S.Context) AvailabilityAttr(
              S.Context, getPlaceholderAttrInfo(),
              &S.Context.Idents.get("swift"), VersionTuple(), VersionTuple(),
              VersionTuple(),
              /*Unavailable=*/true,
              ASTAllocateString(S.Context, Info.UnavailableMsg),
              /*Strict=*/false,
              /*Replacement=*/StringRef(),
              /*Priority=*/Sema::AP_Explicit,
              /*Environment=*/nullptr);
        },
        [](const Decl *D) {
          return llvm::find_if(D->attrs(), [](const Attr *next) -> bool {
            if (const auto *AA = dyn_cast<AvailabilityAttr>(next))
              if (const auto *II = AA->getPlatform())
                return II->isStr("swift");
            return false;
          });
        });
  }

  // swift_private
  if (auto SwiftPrivate = Info.isSwiftPrivate()) {
    handleAPINotedAttribute<SwiftPrivateAttr>(
        S, D, *SwiftPrivate, Metadata, [&] {
          return new (S.Context)
              SwiftPrivateAttr(S.Context, getPlaceholderAttrInfo());
        });
  }

  // swift_name
  if (!Info.SwiftName.empty()) {
    handleAPINotedAttribute<SwiftNameAttr>(
        S, D, true, Metadata, [&]() -> SwiftNameAttr * {
          AttributeFactory AF{};
          AttributePool AP{AF};
          auto &C = S.getASTContext();
          ParsedAttr *SNA =
              AP.create(&C.Idents.get("swift_name"), SourceRange(), nullptr,
                        SourceLocation(), nullptr, nullptr, nullptr,
                        ParsedAttr::Form::GNU());

          if (!S.Swift().DiagnoseName(D, Info.SwiftName, D->getLocation(), *SNA,
                                      /*IsAsync=*/false))
            return nullptr;

          return new (S.Context)
              SwiftNameAttr(S.Context, getPlaceholderAttrInfo(),
                            ASTAllocateString(S.Context, Info.SwiftName));
        });
  }
}

static void ProcessAPINotes(Sema &S, Decl *D,
                            const api_notes::CommonTypeInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // swift_bridge
  if (auto SwiftBridge = Info.getSwiftBridge()) {
    handleAPINotedAttribute<SwiftBridgeAttr>(
        S, D, !SwiftBridge->empty(), Metadata, [&] {
          return new (S.Context)
              SwiftBridgeAttr(S.Context, getPlaceholderAttrInfo(),
                              ASTAllocateString(S.Context, *SwiftBridge));
        });
  }

  // ns_error_domain
  if (auto NSErrorDomain = Info.getNSErrorDomain()) {
    handleAPINotedAttribute<NSErrorDomainAttr>(
        S, D, !NSErrorDomain->empty(), Metadata, [&] {
          return new (S.Context)
              NSErrorDomainAttr(S.Context, getPlaceholderAttrInfo(),
                                &S.Context.Idents.get(*NSErrorDomain));
        });
  }

  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Check that the replacement type provided by API notes is reasonable.
///
/// This is a very weak form of ABI check.
static bool checkAPINotesReplacementType(Sema &S, SourceLocation Loc,
                                         QualType OrigType,
                                         QualType ReplacementType) {
  if (S.Context.getTypeSize(OrigType) !=
      S.Context.getTypeSize(ReplacementType)) {
    S.Diag(Loc, diag::err_incompatible_replacement_type)
        << ReplacementType << OrigType;
    return true;
  }

  return false;
}

/// Process API notes for a variable or property.
static void ProcessAPINotes(Sema &S, Decl *D,
                            const api_notes::VariableInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Type override.
  if (Metadata.IsActive && !Info.getType().empty() &&
      S.ParseTypeFromStringCallback) {
    auto ParsedType = S.ParseTypeFromStringCallback(
        Info.getType(), "<API Notes>", D->getLocation());
    if (ParsedType.isUsable()) {
      QualType Type = Sema::GetTypeFromParser(ParsedType.get());
      auto TypeInfo =
          S.Context.getTrivialTypeSourceInfo(Type, D->getLocation());

      if (auto Var = dyn_cast<VarDecl>(D)) {
        // Make adjustments to parameter types.
        if (isa<ParmVarDecl>(Var)) {
          Type = S.ObjC().AdjustParameterTypeForObjCAutoRefCount(
              Type, D->getLocation(), TypeInfo);
          Type = S.Context.getAdjustedParameterType(Type);
        }

        if (!checkAPINotesReplacementType(S, Var->getLocation(), Var->getType(),
                                          Type)) {
          Var->setType(Type);
          Var->setTypeSourceInfo(TypeInfo);
        }
      } else if (auto Property = dyn_cast<ObjCPropertyDecl>(D)) {
        if (!checkAPINotesReplacementType(S, Property->getLocation(),
                                          Property->getType(), Type))
          Property->setType(Type, TypeInfo);

      } else
        llvm_unreachable("API notes allowed a type on an unknown declaration");
    }
  }

  // Nullability.
  if (auto Nullability = Info.getNullability())
    applyNullability(S, D, *Nullability, Metadata);

  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Process API notes for a parameter.
static void ProcessAPINotes(Sema &S, ParmVarDecl *D,
                            const api_notes::ParamInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // noescape
  if (auto NoEscape = Info.isNoEscape())
    handleAPINotedAttribute<NoEscapeAttr>(S, D, *NoEscape, Metadata, [&] {
      return new (S.Context) NoEscapeAttr(S.Context, getPlaceholderAttrInfo());
    });

  // Retain count convention
  handleAPINotedRetainCountConvention(S, D, Metadata,
                                      Info.getRetainCountConvention());

  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::VariableInfo &>(Info),
                  Metadata);
}

/// Process API notes for a global variable.
static void ProcessAPINotes(Sema &S, VarDecl *D,
                            const api_notes::GlobalVariableInfo &Info,
                            VersionedInfoMetadata metadata) {
  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::VariableInfo &>(Info),
                  metadata);
}

/// Process API notes for an Objective-C property.
static void ProcessAPINotes(Sema &S, ObjCPropertyDecl *D,
                            const api_notes::ObjCPropertyInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::VariableInfo &>(Info),
                  Metadata);

  if (auto AsAccessors = Info.getSwiftImportAsAccessors()) {
    handleAPINotedAttribute<SwiftImportPropertyAsAccessorsAttr>(
        S, D, *AsAccessors, Metadata, [&] {
          return new (S.Context) SwiftImportPropertyAsAccessorsAttr(
              S.Context, getPlaceholderAttrInfo());
        });
  }
}

namespace {
typedef llvm::PointerUnion<FunctionDecl *, ObjCMethodDecl *> FunctionOrMethod;
}

/// Process API notes for a function or method.
static void ProcessAPINotes(Sema &S, FunctionOrMethod AnyFunc,
                            const api_notes::FunctionInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Find the declaration itself.
  FunctionDecl *FD = AnyFunc.dyn_cast<FunctionDecl *>();
  Decl *D = FD;
  ObjCMethodDecl *MD = nullptr;
  if (!D) {
    MD = AnyFunc.get<ObjCMethodDecl *>();
    D = MD;
  }

  assert((FD || MD) && "Expecting Function or ObjCMethod");

  // Nullability of return type.
  if (Info.NullabilityAudited)
    applyNullability(S, D, Info.getReturnTypeInfo(), Metadata);

  // Parameters.
  unsigned NumParams = FD ? FD->getNumParams() : MD->param_size();

  bool AnyTypeChanged = false;
  for (unsigned I = 0; I != NumParams; ++I) {
    ParmVarDecl *Param = FD ? FD->getParamDecl(I) : MD->param_begin()[I];
    QualType ParamTypeBefore = Param->getType();

    if (I < Info.Params.size())
      ProcessAPINotes(S, Param, Info.Params[I], Metadata);

    // Nullability.
    if (Info.NullabilityAudited)
      applyNullability(S, Param, Info.getParamTypeInfo(I), Metadata);

    if (ParamTypeBefore.getAsOpaquePtr() != Param->getType().getAsOpaquePtr())
      AnyTypeChanged = true;
  }

  // Result type override.
  QualType OverriddenResultType;
  if (Metadata.IsActive && !Info.ResultType.empty() &&
      S.ParseTypeFromStringCallback) {
    auto ParsedType = S.ParseTypeFromStringCallback(
        Info.ResultType, "<API Notes>", D->getLocation());
    if (ParsedType.isUsable()) {
      QualType ResultType = Sema::GetTypeFromParser(ParsedType.get());

      if (MD) {
        if (!checkAPINotesReplacementType(S, D->getLocation(),
                                          MD->getReturnType(), ResultType)) {
          auto ResultTypeInfo =
              S.Context.getTrivialTypeSourceInfo(ResultType, D->getLocation());
          MD->setReturnType(ResultType);
          MD->setReturnTypeSourceInfo(ResultTypeInfo);
        }
      } else if (!checkAPINotesReplacementType(
                     S, FD->getLocation(), FD->getReturnType(), ResultType)) {
        OverriddenResultType = ResultType;
        AnyTypeChanged = true;
      }
    }
  }

  // If the result type or any of the parameter types changed for a function
  // declaration, we have to rebuild the type.
  if (FD && AnyTypeChanged) {
    if (const auto *fnProtoType = FD->getType()->getAs<FunctionProtoType>()) {
      if (OverriddenResultType.isNull())
        OverriddenResultType = fnProtoType->getReturnType();

      SmallVector<QualType, 4> ParamTypes;
      for (auto Param : FD->parameters())
        ParamTypes.push_back(Param->getType());

      FD->setType(S.Context.getFunctionType(OverriddenResultType, ParamTypes,
                                            fnProtoType->getExtProtoInfo()));
    } else if (!OverriddenResultType.isNull()) {
      const auto *FnNoProtoType = FD->getType()->castAs<FunctionNoProtoType>();
      FD->setType(S.Context.getFunctionNoProtoType(
          OverriddenResultType, FnNoProtoType->getExtInfo()));
    }
  }

  // Retain count convention
  handleAPINotedRetainCountConvention(S, D, Metadata,
                                      Info.getRetainCountConvention());

  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Process API notes for a C++ method.
static void ProcessAPINotes(Sema &S, CXXMethodDecl *Method,
                            const api_notes::CXXMethodInfo &Info,
                            VersionedInfoMetadata Metadata) {
  ProcessAPINotes(S, (FunctionOrMethod)Method, Info, Metadata);
}

/// Process API notes for a global function.
static void ProcessAPINotes(Sema &S, FunctionDecl *D,
                            const api_notes::GlobalFunctionInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common function information.
  ProcessAPINotes(S, FunctionOrMethod(D),
                  static_cast<const api_notes::FunctionInfo &>(Info), Metadata);
}

/// Process API notes for an enumerator.
static void ProcessAPINotes(Sema &S, EnumConstantDecl *D,
                            const api_notes::EnumConstantInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Process API notes for an Objective-C method.
static void ProcessAPINotes(Sema &S, ObjCMethodDecl *D,
                            const api_notes::ObjCMethodInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Designated initializers.
  if (Info.DesignatedInit) {
    handleAPINotedAttribute<ObjCDesignatedInitializerAttr>(
        S, D, true, Metadata, [&] {
          if (ObjCInterfaceDecl *IFace = D->getClassInterface())
            IFace->setHasDesignatedInitializers();

          return new (S.Context) ObjCDesignatedInitializerAttr(
              S.Context, getPlaceholderAttrInfo());
        });
  }

  // Handle common function information.
  ProcessAPINotes(S, FunctionOrMethod(D),
                  static_cast<const api_notes::FunctionInfo &>(Info), Metadata);
}

/// Process API notes for a tag.
static void ProcessAPINotes(Sema &S, TagDecl *D, const api_notes::TagInfo &Info,
                            VersionedInfoMetadata Metadata) {
  if (auto ImportAs = Info.SwiftImportAs)
    D->addAttr(SwiftAttrAttr::Create(S.Context, "import_" + ImportAs.value()));

  if (auto RetainOp = Info.SwiftRetainOp)
    D->addAttr(SwiftAttrAttr::Create(S.Context, "retain:" + RetainOp.value()));

  if (auto ReleaseOp = Info.SwiftReleaseOp)
    D->addAttr(
        SwiftAttrAttr::Create(S.Context, "release:" + ReleaseOp.value()));

  if (auto Copyable = Info.isSwiftCopyable()) {
    if (!*Copyable)
      D->addAttr(SwiftAttrAttr::Create(S.Context, "~Copyable"));
  }

  if (auto Extensibility = Info.EnumExtensibility) {
    using api_notes::EnumExtensibilityKind;
    bool ShouldAddAttribute = (*Extensibility != EnumExtensibilityKind::None);
    handleAPINotedAttribute<EnumExtensibilityAttr>(
        S, D, ShouldAddAttribute, Metadata, [&] {
          EnumExtensibilityAttr::Kind kind;
          switch (*Extensibility) {
          case EnumExtensibilityKind::None:
            llvm_unreachable("remove only");
          case EnumExtensibilityKind::Open:
            kind = EnumExtensibilityAttr::Open;
            break;
          case EnumExtensibilityKind::Closed:
            kind = EnumExtensibilityAttr::Closed;
            break;
          }
          return new (S.Context)
              EnumExtensibilityAttr(S.Context, getPlaceholderAttrInfo(), kind);
        });
  }

  if (auto FlagEnum = Info.isFlagEnum()) {
    handleAPINotedAttribute<FlagEnumAttr>(S, D, *FlagEnum, Metadata, [&] {
      return new (S.Context) FlagEnumAttr(S.Context, getPlaceholderAttrInfo());
    });
  }

  // Handle common type information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonTypeInfo &>(Info),
                  Metadata);
}

/// Process API notes for a typedef.
static void ProcessAPINotes(Sema &S, TypedefNameDecl *D,
                            const api_notes::TypedefInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // swift_wrapper
  using SwiftWrapperKind = api_notes::SwiftNewTypeKind;

  if (auto SwiftWrapper = Info.SwiftWrapper) {
    handleAPINotedAttribute<SwiftNewTypeAttr>(
        S, D, *SwiftWrapper != SwiftWrapperKind::None, Metadata, [&] {
          SwiftNewTypeAttr::NewtypeKind Kind;
          switch (*SwiftWrapper) {
          case SwiftWrapperKind::None:
            llvm_unreachable("Shouldn't build an attribute");

          case SwiftWrapperKind::Struct:
            Kind = SwiftNewTypeAttr::NK_Struct;
            break;

          case SwiftWrapperKind::Enum:
            Kind = SwiftNewTypeAttr::NK_Enum;
            break;
          }
          AttributeCommonInfo SyntaxInfo{
              SourceRange(),
              AttributeCommonInfo::AT_SwiftNewType,
              {AttributeCommonInfo::AS_GNU, SwiftNewTypeAttr::GNU_swift_wrapper,
               /*IsAlignas*/ false, /*IsRegularKeywordAttribute*/ false}};
          return new (S.Context) SwiftNewTypeAttr(S.Context, SyntaxInfo, Kind);
        });
  }

  // Handle common type information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonTypeInfo &>(Info),
                  Metadata);
}

/// Process API notes for an Objective-C class or protocol.
static void ProcessAPINotes(Sema &S, ObjCContainerDecl *D,
                            const api_notes::ContextInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common type information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonTypeInfo &>(Info),
                  Metadata);
}

/// Process API notes for an Objective-C class.
static void ProcessAPINotes(Sema &S, ObjCInterfaceDecl *D,
                            const api_notes::ContextInfo &Info,
                            VersionedInfoMetadata Metadata) {
  if (auto AsNonGeneric = Info.getSwiftImportAsNonGeneric()) {
    handleAPINotedAttribute<SwiftImportAsNonGenericAttr>(
        S, D, *AsNonGeneric, Metadata, [&] {
          return new (S.Context)
              SwiftImportAsNonGenericAttr(S.Context, getPlaceholderAttrInfo());
        });
  }

  if (auto ObjcMembers = Info.getSwiftObjCMembers()) {
    handleAPINotedAttribute<SwiftObjCMembersAttr>(
        S, D, *ObjcMembers, Metadata, [&] {
          return new (S.Context)
              SwiftObjCMembersAttr(S.Context, getPlaceholderAttrInfo());
        });
  }

  // Handle information common to Objective-C classes and protocols.
  ProcessAPINotes(S, static_cast<clang::ObjCContainerDecl *>(D), Info,
                  Metadata);
}

/// If we're applying API notes with an active, non-default version, and the
/// versioned API notes have a SwiftName but the declaration normally wouldn't
/// have one, add a removal attribute to make it clear that the new SwiftName
/// attribute only applies to the active version of \p D, not to all versions.
///
/// This must be run \em before processing API notes for \p D, because otherwise
/// any existing SwiftName attribute will have been packaged up in a
/// SwiftVersionedAdditionAttr.
template <typename SpecificInfo>
static void maybeAttachUnversionedSwiftName(
    Sema &S, Decl *D,
    const api_notes::APINotesReader::VersionedInfo<SpecificInfo> Info) {
  if (D->hasAttr<SwiftNameAttr>())
    return;
  if (!Info.getSelected())
    return;

  // Is the active slice versioned, and does it set a Swift name?
  VersionTuple SelectedVersion;
  SpecificInfo SelectedInfoSlice;
  std::tie(SelectedVersion, SelectedInfoSlice) = Info[*Info.getSelected()];
  if (SelectedVersion.empty())
    return;
  if (SelectedInfoSlice.SwiftName.empty())
    return;

  // Does the unversioned slice /not/ set a Swift name?
  for (const auto &VersionAndInfoSlice : Info) {
    if (!VersionAndInfoSlice.first.empty())
      continue;
    if (!VersionAndInfoSlice.second.SwiftName.empty())
      return;
  }

  // Then explicitly call that out with a removal attribute.
  VersionedInfoMetadata DummyFutureMetadata(
      SelectedVersion, IsActive_t::Inactive, IsSubstitution_t::Replacement);
  handleAPINotedAttribute<SwiftNameAttr>(
      S, D, /*add*/ false, DummyFutureMetadata, []() -> SwiftNameAttr * {
        llvm_unreachable("should not try to add an attribute here");
      });
}

/// Processes all versions of versioned API notes.
///
/// Just dispatches to the various ProcessAPINotes functions in this file.
template <typename SpecificDecl, typename SpecificInfo>
static void ProcessVersionedAPINotes(
    Sema &S, SpecificDecl *D,
    const api_notes::APINotesReader::VersionedInfo<SpecificInfo> Info) {

  maybeAttachUnversionedSwiftName(S, D, Info);

  unsigned Selected = Info.getSelected().value_or(Info.size());

  VersionTuple Version;
  SpecificInfo InfoSlice;
  for (unsigned i = 0, e = Info.size(); i != e; ++i) {
    std::tie(Version, InfoSlice) = Info[i];
    auto Active = (i == Selected) ? IsActive_t::Active : IsActive_t::Inactive;
    auto Replacement = IsSubstitution_t::Original;
    if (Active == IsActive_t::Inactive && Version.empty()) {
      Replacement = IsSubstitution_t::Replacement;
      Version = Info[Selected].first;
    }
    ProcessAPINotes(S, D, InfoSlice,
                    VersionedInfoMetadata(Version, Active, Replacement));
  }
}

/// Process API notes that are associated with this declaration, mapping them
/// to attributes as appropriate.
void Sema::ProcessAPINotes(Decl *D) {
  if (!D)
    return;

  auto GetNamespaceContext =
      [&](DeclContext *DC) -> std::optional<api_notes::Context> {
    if (auto NamespaceContext = dyn_cast<NamespaceDecl>(DC)) {
      for (auto Reader :
           APINotes.findAPINotes(NamespaceContext->getLocation())) {
        // Retrieve the context ID for the parent namespace of the decl.
        std::stack<NamespaceDecl *> NamespaceStack;
        {
          for (auto CurrentNamespace = NamespaceContext; CurrentNamespace;
               CurrentNamespace =
                   dyn_cast<NamespaceDecl>(CurrentNamespace->getParent())) {
            if (!CurrentNamespace->isInlineNamespace())
              NamespaceStack.push(CurrentNamespace);
          }
        }
        std::optional<api_notes::ContextID> NamespaceID;
        while (!NamespaceStack.empty()) {
          auto CurrentNamespace = NamespaceStack.top();
          NamespaceStack.pop();
          NamespaceID = Reader->lookupNamespaceID(CurrentNamespace->getName(),
                                                  NamespaceID);
          if (!NamespaceID)
            break;
        }
        if (NamespaceID)
          return api_notes::Context(*NamespaceID,
                                    api_notes::ContextKind::Namespace);
      }
    }
    return std::nullopt;
  };

  // Globals.
  if (D->getDeclContext()->isFileContext() ||
      D->getDeclContext()->isNamespace() ||
      D->getDeclContext()->isExternCContext() ||
      D->getDeclContext()->isExternCXXContext()) {
    std::optional<api_notes::Context> APINotesContext =
        GetNamespaceContext(D->getDeclContext());
    // Global variables.
    if (auto VD = dyn_cast<VarDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        auto Info =
            Reader->lookupGlobalVariable(VD->getName(), APINotesContext);
        ProcessVersionedAPINotes(*this, VD, Info);
      }

      return;
    }

    // Global functions.
    if (auto FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->getDeclName().isIdentifier()) {
        for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
          auto Info =
              Reader->lookupGlobalFunction(FD->getName(), APINotesContext);
          ProcessVersionedAPINotes(*this, FD, Info);
        }
      }

      return;
    }

    // Objective-C classes.
    if (auto Class = dyn_cast<ObjCInterfaceDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        auto Info = Reader->lookupObjCClassInfo(Class->getName());
        ProcessVersionedAPINotes(*this, Class, Info);
      }

      return;
    }

    // Objective-C protocols.
    if (auto Protocol = dyn_cast<ObjCProtocolDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        auto Info = Reader->lookupObjCProtocolInfo(Protocol->getName());
        ProcessVersionedAPINotes(*this, Protocol, Info);
      }

      return;
    }

    // Tags
    if (auto Tag = dyn_cast<TagDecl>(D)) {
      std::string LookupName = Tag->getName().str();

      // Use the source location to discern if this Tag is an OPTIONS macro.
      // For now we would like to limit this trick of looking up the APINote tag
      // using the EnumDecl's QualType in the case where the enum is anonymous.
      // This is only being used to support APINotes lookup for C++
      // NS/CF_OPTIONS when C++-Interop is enabled.
      std::string MacroName =
          LookupName.empty() && Tag->getOuterLocStart().isMacroID()
              ? clang::Lexer::getImmediateMacroName(
                    Tag->getOuterLocStart(),
                    Tag->getASTContext().getSourceManager(), LangOpts)
                    .str()
              : "";

      if (LookupName.empty() && isa<clang::EnumDecl>(Tag) &&
          (MacroName == "CF_OPTIONS" || MacroName == "NS_OPTIONS" ||
           MacroName == "OBJC_OPTIONS" || MacroName == "SWIFT_OPTIONS")) {

        clang::QualType T = llvm::cast<clang::EnumDecl>(Tag)->getIntegerType();
        LookupName = clang::QualType::getAsString(
            T.split(), getASTContext().getPrintingPolicy());
      }

      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        auto Info = Reader->lookupTag(LookupName, APINotesContext);
        ProcessVersionedAPINotes(*this, Tag, Info);
      }

      return;
    }

    // Typedefs
    if (auto Typedef = dyn_cast<TypedefNameDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        auto Info = Reader->lookupTypedef(Typedef->getName(), APINotesContext);
        ProcessVersionedAPINotes(*this, Typedef, Info);
      }

      return;
    }
  }

  // Enumerators.
  if (D->getDeclContext()->getRedeclContext()->isFileContext() ||
      D->getDeclContext()->getRedeclContext()->isExternCContext()) {
    if (auto EnumConstant = dyn_cast<EnumConstantDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        auto Info = Reader->lookupEnumConstant(EnumConstant->getName());
        ProcessVersionedAPINotes(*this, EnumConstant, Info);
      }

      return;
    }
  }

  if (auto ObjCContainer = dyn_cast<ObjCContainerDecl>(D->getDeclContext())) {
    // Location function that looks up an Objective-C context.
    auto GetContext = [&](api_notes::APINotesReader *Reader)
        -> std::optional<api_notes::ContextID> {
      if (auto Protocol = dyn_cast<ObjCProtocolDecl>(ObjCContainer)) {
        if (auto Found = Reader->lookupObjCProtocolID(Protocol->getName()))
          return *Found;

        return std::nullopt;
      }

      if (auto Impl = dyn_cast<ObjCCategoryImplDecl>(ObjCContainer)) {
        if (auto Cat = Impl->getCategoryDecl())
          ObjCContainer = Cat->getClassInterface();
        else
          return std::nullopt;
      }

      if (auto Category = dyn_cast<ObjCCategoryDecl>(ObjCContainer)) {
        if (Category->getClassInterface())
          ObjCContainer = Category->getClassInterface();
        else
          return std::nullopt;
      }

      if (auto Impl = dyn_cast<ObjCImplDecl>(ObjCContainer)) {
        if (Impl->getClassInterface())
          ObjCContainer = Impl->getClassInterface();
        else
          return std::nullopt;
      }

      if (auto Class = dyn_cast<ObjCInterfaceDecl>(ObjCContainer)) {
        if (auto Found = Reader->lookupObjCClassID(Class->getName()))
          return *Found;

        return std::nullopt;
      }

      return std::nullopt;
    };

    // Objective-C methods.
    if (auto Method = dyn_cast<ObjCMethodDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        if (auto Context = GetContext(Reader)) {
          // Map the selector.
          Selector Sel = Method->getSelector();
          SmallVector<StringRef, 2> SelPieces;
          if (Sel.isUnarySelector()) {
            SelPieces.push_back(Sel.getNameForSlot(0));
          } else {
            for (unsigned i = 0, n = Sel.getNumArgs(); i != n; ++i)
              SelPieces.push_back(Sel.getNameForSlot(i));
          }

          api_notes::ObjCSelectorRef SelectorRef;
          SelectorRef.NumArgs = Sel.getNumArgs();
          SelectorRef.Identifiers = SelPieces;

          auto Info = Reader->lookupObjCMethod(*Context, SelectorRef,
                                               Method->isInstanceMethod());
          ProcessVersionedAPINotes(*this, Method, Info);
        }
      }
    }

    // Objective-C properties.
    if (auto Property = dyn_cast<ObjCPropertyDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        if (auto Context = GetContext(Reader)) {
          bool isInstanceProperty =
              (Property->getPropertyAttributesAsWritten() &
               ObjCPropertyAttribute::kind_class) == 0;
          auto Info = Reader->lookupObjCProperty(*Context, Property->getName(),
                                                 isInstanceProperty);
          ProcessVersionedAPINotes(*this, Property, Info);
        }
      }

      return;
    }
  }

  if (auto CXXRecord = dyn_cast<CXXRecordDecl>(D->getDeclContext())) {
    auto GetRecordContext = [&](api_notes::APINotesReader *Reader)
        -> std::optional<api_notes::ContextID> {
      auto ParentContext = GetNamespaceContext(CXXRecord->getDeclContext());
      if (auto Found = Reader->lookupTagID(CXXRecord->getName(), ParentContext))
        return *Found;

      return std::nullopt;
    };

    if (auto CXXMethod = dyn_cast<CXXMethodDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        if (auto Context = GetRecordContext(Reader)) {
          auto Info = Reader->lookupCXXMethod(*Context, CXXMethod->getName());
          ProcessVersionedAPINotes(*this, CXXMethod, Info);
        }
      }
    }
  }
}
