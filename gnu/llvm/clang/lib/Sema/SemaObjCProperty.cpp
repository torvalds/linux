//===--- SemaObjCProperty.cpp - Semantic Analysis for ObjC @property ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for Objective C @property and
//  @synthesize declarations.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// Grammar actions.
//===----------------------------------------------------------------------===//

/// getImpliedARCOwnership - Given a set of property attributes and a
/// type, infer an expected lifetime.  The type's ownership qualification
/// is not considered.
///
/// Returns OCL_None if the attributes as stated do not imply an ownership.
/// Never returns OCL_Autoreleasing.
static Qualifiers::ObjCLifetime
getImpliedARCOwnership(ObjCPropertyAttribute::Kind attrs, QualType type) {
  // retain, strong, copy, weak, and unsafe_unretained are only legal
  // on properties of retainable pointer type.
  if (attrs &
      (ObjCPropertyAttribute::kind_retain | ObjCPropertyAttribute::kind_strong |
       ObjCPropertyAttribute::kind_copy)) {
    return Qualifiers::OCL_Strong;
  } else if (attrs & ObjCPropertyAttribute::kind_weak) {
    return Qualifiers::OCL_Weak;
  } else if (attrs & ObjCPropertyAttribute::kind_unsafe_unretained) {
    return Qualifiers::OCL_ExplicitNone;
  }

  // assign can appear on other types, so we have to check the
  // property type.
  if (attrs & ObjCPropertyAttribute::kind_assign &&
      type->isObjCRetainableType()) {
    return Qualifiers::OCL_ExplicitNone;
  }

  return Qualifiers::OCL_None;
}

/// Check the internal consistency of a property declaration with
/// an explicit ownership qualifier.
static void checkPropertyDeclWithOwnership(Sema &S,
                                           ObjCPropertyDecl *property) {
  if (property->isInvalidDecl()) return;

  ObjCPropertyAttribute::Kind propertyKind = property->getPropertyAttributes();
  Qualifiers::ObjCLifetime propertyLifetime
    = property->getType().getObjCLifetime();

  assert(propertyLifetime != Qualifiers::OCL_None);

  Qualifiers::ObjCLifetime expectedLifetime
    = getImpliedARCOwnership(propertyKind, property->getType());
  if (!expectedLifetime) {
    // We have a lifetime qualifier but no dominating property
    // attribute.  That's okay, but restore reasonable invariants by
    // setting the property attribute according to the lifetime
    // qualifier.
    ObjCPropertyAttribute::Kind attr;
    if (propertyLifetime == Qualifiers::OCL_Strong) {
      attr = ObjCPropertyAttribute::kind_strong;
    } else if (propertyLifetime == Qualifiers::OCL_Weak) {
      attr = ObjCPropertyAttribute::kind_weak;
    } else {
      assert(propertyLifetime == Qualifiers::OCL_ExplicitNone);
      attr = ObjCPropertyAttribute::kind_unsafe_unretained;
    }
    property->setPropertyAttributes(attr);
    return;
  }

  if (propertyLifetime == expectedLifetime) return;

  property->setInvalidDecl();
  S.Diag(property->getLocation(),
         diag::err_arc_inconsistent_property_ownership)
    << property->getDeclName()
    << expectedLifetime
    << propertyLifetime;
}

/// Check this Objective-C property against a property declared in the
/// given protocol.
static void
CheckPropertyAgainstProtocol(Sema &S, ObjCPropertyDecl *Prop,
                             ObjCProtocolDecl *Proto,
                             llvm::SmallPtrSetImpl<ObjCProtocolDecl *> &Known) {
  // Have we seen this protocol before?
  if (!Known.insert(Proto).second)
    return;

  // Look for a property with the same name.
  if (ObjCPropertyDecl *ProtoProp = Proto->getProperty(
          Prop->getIdentifier(), Prop->isInstanceProperty())) {
    S.ObjC().DiagnosePropertyMismatch(Prop, ProtoProp, Proto->getIdentifier(),
                                      true);
    return;
  }

  // Check this property against any protocols we inherit.
  for (auto *P : Proto->protocols())
    CheckPropertyAgainstProtocol(S, Prop, P, Known);
}

static unsigned deducePropertyOwnershipFromType(Sema &S, QualType T) {
  // In GC mode, just look for the __weak qualifier.
  if (S.getLangOpts().getGC() != LangOptions::NonGC) {
    if (T.isObjCGCWeak())
      return ObjCPropertyAttribute::kind_weak;

    // In ARC/MRC, look for an explicit ownership qualifier.
    // For some reason, this only applies to __weak.
  } else if (auto ownership = T.getObjCLifetime()) {
    switch (ownership) {
    case Qualifiers::OCL_Weak:
      return ObjCPropertyAttribute::kind_weak;
    case Qualifiers::OCL_Strong:
      return ObjCPropertyAttribute::kind_strong;
    case Qualifiers::OCL_ExplicitNone:
      return ObjCPropertyAttribute::kind_unsafe_unretained;
    case Qualifiers::OCL_Autoreleasing:
    case Qualifiers::OCL_None:
      return 0;
    }
    llvm_unreachable("bad qualifier");
  }

  return 0;
}

static const unsigned OwnershipMask =
    (ObjCPropertyAttribute::kind_assign | ObjCPropertyAttribute::kind_retain |
     ObjCPropertyAttribute::kind_copy | ObjCPropertyAttribute::kind_weak |
     ObjCPropertyAttribute::kind_strong |
     ObjCPropertyAttribute::kind_unsafe_unretained);

static unsigned getOwnershipRule(unsigned attr) {
  unsigned result = attr & OwnershipMask;

  // From an ownership perspective, assign and unsafe_unretained are
  // identical; make sure one also implies the other.
  if (result & (ObjCPropertyAttribute::kind_assign |
                ObjCPropertyAttribute::kind_unsafe_unretained)) {
    result |= ObjCPropertyAttribute::kind_assign |
              ObjCPropertyAttribute::kind_unsafe_unretained;
  }

  return result;
}

Decl *SemaObjC::ActOnProperty(Scope *S, SourceLocation AtLoc,
                              SourceLocation LParenLoc, FieldDeclarator &FD,
                              ObjCDeclSpec &ODS, Selector GetterSel,
                              Selector SetterSel,
                              tok::ObjCKeywordKind MethodImplKind,
                              DeclContext *lexicalDC) {
  unsigned Attributes = ODS.getPropertyAttributes();
  FD.D.setObjCWeakProperty((Attributes & ObjCPropertyAttribute::kind_weak) !=
                           0);
  TypeSourceInfo *TSI = SemaRef.GetTypeForDeclarator(FD.D);
  QualType T = TSI->getType();
  if (!getOwnershipRule(Attributes)) {
    Attributes |= deducePropertyOwnershipFromType(SemaRef, T);
  }
  bool isReadWrite = ((Attributes & ObjCPropertyAttribute::kind_readwrite) ||
                      // default is readwrite!
                      !(Attributes & ObjCPropertyAttribute::kind_readonly));

  // Proceed with constructing the ObjCPropertyDecls.
  ObjCContainerDecl *ClassDecl = cast<ObjCContainerDecl>(SemaRef.CurContext);
  ObjCPropertyDecl *Res = nullptr;
  if (ObjCCategoryDecl *CDecl = dyn_cast<ObjCCategoryDecl>(ClassDecl)) {
    if (CDecl->IsClassExtension()) {
      Res = HandlePropertyInClassExtension(S, AtLoc, LParenLoc,
                                           FD,
                                           GetterSel, ODS.getGetterNameLoc(),
                                           SetterSel, ODS.getSetterNameLoc(),
                                           isReadWrite, Attributes,
                                           ODS.getPropertyAttributes(),
                                           T, TSI, MethodImplKind);
      if (!Res)
        return nullptr;
    }
  }

  if (!Res) {
    Res = CreatePropertyDecl(S, ClassDecl, AtLoc, LParenLoc, FD,
                             GetterSel, ODS.getGetterNameLoc(), SetterSel,
                             ODS.getSetterNameLoc(), isReadWrite, Attributes,
                             ODS.getPropertyAttributes(), T, TSI,
                             MethodImplKind);
    if (lexicalDC)
      Res->setLexicalDeclContext(lexicalDC);
  }

  // Validate the attributes on the @property.
  CheckObjCPropertyAttributes(Res, AtLoc, Attributes,
                              (isa<ObjCInterfaceDecl>(ClassDecl) ||
                               isa<ObjCProtocolDecl>(ClassDecl)));

  // Check consistency if the type has explicit ownership qualification.
  if (Res->getType().getObjCLifetime())
    checkPropertyDeclWithOwnership(SemaRef, Res);

  llvm::SmallPtrSet<ObjCProtocolDecl *, 16> KnownProtos;
  if (ObjCInterfaceDecl *IFace = dyn_cast<ObjCInterfaceDecl>(ClassDecl)) {
    // For a class, compare the property against a property in our superclass.
    bool FoundInSuper = false;
    ObjCInterfaceDecl *CurrentInterfaceDecl = IFace;
    while (ObjCInterfaceDecl *Super = CurrentInterfaceDecl->getSuperClass()) {
      if (ObjCPropertyDecl *SuperProp = Super->getProperty(
              Res->getIdentifier(), Res->isInstanceProperty())) {
        DiagnosePropertyMismatch(Res, SuperProp, Super->getIdentifier(), false);
        FoundInSuper = true;
        break;
      }
      CurrentInterfaceDecl = Super;
    }

    if (FoundInSuper) {
      // Also compare the property against a property in our protocols.
      for (auto *P : CurrentInterfaceDecl->protocols()) {
        CheckPropertyAgainstProtocol(SemaRef, Res, P, KnownProtos);
      }
    } else {
      // Slower path: look in all protocols we referenced.
      for (auto *P : IFace->all_referenced_protocols()) {
        CheckPropertyAgainstProtocol(SemaRef, Res, P, KnownProtos);
      }
    }
  } else if (ObjCCategoryDecl *Cat = dyn_cast<ObjCCategoryDecl>(ClassDecl)) {
    // We don't check if class extension. Because properties in class extension
    // are meant to override some of the attributes and checking has already done
    // when property in class extension is constructed.
    if (!Cat->IsClassExtension())
      for (auto *P : Cat->protocols())
        CheckPropertyAgainstProtocol(SemaRef, Res, P, KnownProtos);
  } else {
    ObjCProtocolDecl *Proto = cast<ObjCProtocolDecl>(ClassDecl);
    for (auto *P : Proto->protocols())
      CheckPropertyAgainstProtocol(SemaRef, Res, P, KnownProtos);
  }

  SemaRef.ActOnDocumentableDecl(Res);
  return Res;
}

static ObjCPropertyAttribute::Kind
makePropertyAttributesAsWritten(unsigned Attributes) {
  unsigned attributesAsWritten = 0;
  if (Attributes & ObjCPropertyAttribute::kind_readonly)
    attributesAsWritten |= ObjCPropertyAttribute::kind_readonly;
  if (Attributes & ObjCPropertyAttribute::kind_readwrite)
    attributesAsWritten |= ObjCPropertyAttribute::kind_readwrite;
  if (Attributes & ObjCPropertyAttribute::kind_getter)
    attributesAsWritten |= ObjCPropertyAttribute::kind_getter;
  if (Attributes & ObjCPropertyAttribute::kind_setter)
    attributesAsWritten |= ObjCPropertyAttribute::kind_setter;
  if (Attributes & ObjCPropertyAttribute::kind_assign)
    attributesAsWritten |= ObjCPropertyAttribute::kind_assign;
  if (Attributes & ObjCPropertyAttribute::kind_retain)
    attributesAsWritten |= ObjCPropertyAttribute::kind_retain;
  if (Attributes & ObjCPropertyAttribute::kind_strong)
    attributesAsWritten |= ObjCPropertyAttribute::kind_strong;
  if (Attributes & ObjCPropertyAttribute::kind_weak)
    attributesAsWritten |= ObjCPropertyAttribute::kind_weak;
  if (Attributes & ObjCPropertyAttribute::kind_copy)
    attributesAsWritten |= ObjCPropertyAttribute::kind_copy;
  if (Attributes & ObjCPropertyAttribute::kind_unsafe_unretained)
    attributesAsWritten |= ObjCPropertyAttribute::kind_unsafe_unretained;
  if (Attributes & ObjCPropertyAttribute::kind_nonatomic)
    attributesAsWritten |= ObjCPropertyAttribute::kind_nonatomic;
  if (Attributes & ObjCPropertyAttribute::kind_atomic)
    attributesAsWritten |= ObjCPropertyAttribute::kind_atomic;
  if (Attributes & ObjCPropertyAttribute::kind_class)
    attributesAsWritten |= ObjCPropertyAttribute::kind_class;
  if (Attributes & ObjCPropertyAttribute::kind_direct)
    attributesAsWritten |= ObjCPropertyAttribute::kind_direct;

  return (ObjCPropertyAttribute::Kind)attributesAsWritten;
}

static bool LocPropertyAttribute( ASTContext &Context, const char *attrName,
                                 SourceLocation LParenLoc, SourceLocation &Loc) {
  if (LParenLoc.isMacroID())
    return false;

  SourceManager &SM = Context.getSourceManager();
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(LParenLoc);
  // Try to load the file buffer.
  bool invalidTemp = false;
  StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp)
    return false;
  const char *tokenBegin = file.data() + locInfo.second;

  // Lex from the start of the given location.
  Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
              Context.getLangOpts(),
              file.begin(), tokenBegin, file.end());
  Token Tok;
  do {
    lexer.LexFromRawLexer(Tok);
    if (Tok.is(tok::raw_identifier) && Tok.getRawIdentifier() == attrName) {
      Loc = Tok.getLocation();
      return true;
    }
  } while (Tok.isNot(tok::r_paren));
  return false;
}

/// Check for a mismatch in the atomicity of the given properties.
static void checkAtomicPropertyMismatch(Sema &S,
                                        ObjCPropertyDecl *OldProperty,
                                        ObjCPropertyDecl *NewProperty,
                                        bool PropagateAtomicity) {
  // If the atomicity of both matches, we're done.
  bool OldIsAtomic = (OldProperty->getPropertyAttributes() &
                      ObjCPropertyAttribute::kind_nonatomic) == 0;
  bool NewIsAtomic = (NewProperty->getPropertyAttributes() &
                      ObjCPropertyAttribute::kind_nonatomic) == 0;
  if (OldIsAtomic == NewIsAtomic) return;

  // Determine whether the given property is readonly and implicitly
  // atomic.
  auto isImplicitlyReadonlyAtomic = [](ObjCPropertyDecl *Property) -> bool {
    // Is it readonly?
    auto Attrs = Property->getPropertyAttributes();
    if ((Attrs & ObjCPropertyAttribute::kind_readonly) == 0)
      return false;

    // Is it nonatomic?
    if (Attrs & ObjCPropertyAttribute::kind_nonatomic)
      return false;

    // Was 'atomic' specified directly?
    if (Property->getPropertyAttributesAsWritten() &
        ObjCPropertyAttribute::kind_atomic)
      return false;

    return true;
  };

  // If we're allowed to propagate atomicity, and the new property did
  // not specify atomicity at all, propagate.
  const unsigned AtomicityMask = (ObjCPropertyAttribute::kind_atomic |
                                  ObjCPropertyAttribute::kind_nonatomic);
  if (PropagateAtomicity &&
      ((NewProperty->getPropertyAttributesAsWritten() & AtomicityMask) == 0)) {
    unsigned Attrs = NewProperty->getPropertyAttributes();
    Attrs = Attrs & ~AtomicityMask;
    if (OldIsAtomic)
      Attrs |= ObjCPropertyAttribute::kind_atomic;
    else
      Attrs |= ObjCPropertyAttribute::kind_nonatomic;

    NewProperty->overwritePropertyAttributes(Attrs);
    return;
  }

  // One of the properties is atomic; if it's a readonly property, and
  // 'atomic' wasn't explicitly specified, we're okay.
  if ((OldIsAtomic && isImplicitlyReadonlyAtomic(OldProperty)) ||
      (NewIsAtomic && isImplicitlyReadonlyAtomic(NewProperty)))
    return;

  // Diagnose the conflict.
  const IdentifierInfo *OldContextName;
  auto *OldDC = OldProperty->getDeclContext();
  if (auto Category = dyn_cast<ObjCCategoryDecl>(OldDC))
    OldContextName = Category->getClassInterface()->getIdentifier();
  else
    OldContextName = cast<ObjCContainerDecl>(OldDC)->getIdentifier();

  S.Diag(NewProperty->getLocation(), diag::warn_property_attribute)
    << NewProperty->getDeclName() << "atomic"
    << OldContextName;
  S.Diag(OldProperty->getLocation(), diag::note_property_declare);
}

ObjCPropertyDecl *SemaObjC::HandlePropertyInClassExtension(
    Scope *S, SourceLocation AtLoc, SourceLocation LParenLoc,
    FieldDeclarator &FD, Selector GetterSel, SourceLocation GetterNameLoc,
    Selector SetterSel, SourceLocation SetterNameLoc, const bool isReadWrite,
    unsigned &Attributes, const unsigned AttributesAsWritten, QualType T,
    TypeSourceInfo *TSI, tok::ObjCKeywordKind MethodImplKind) {
  ObjCCategoryDecl *CDecl = cast<ObjCCategoryDecl>(SemaRef.CurContext);
  // Diagnose if this property is already in continuation class.
  DeclContext *DC = SemaRef.CurContext;
  const IdentifierInfo *PropertyId = FD.D.getIdentifier();
  ObjCInterfaceDecl *CCPrimary = CDecl->getClassInterface();

  // We need to look in the @interface to see if the @property was
  // already declared.
  if (!CCPrimary) {
    Diag(CDecl->getLocation(), diag::err_continuation_class);
    return nullptr;
  }

  bool isClassProperty =
      (AttributesAsWritten & ObjCPropertyAttribute::kind_class) ||
      (Attributes & ObjCPropertyAttribute::kind_class);

  // Find the property in the extended class's primary class or
  // extensions.
  ObjCPropertyDecl *PIDecl = CCPrimary->FindPropertyVisibleInPrimaryClass(
      PropertyId, ObjCPropertyDecl::getQueryKind(isClassProperty));

  // If we found a property in an extension, complain.
  if (PIDecl && isa<ObjCCategoryDecl>(PIDecl->getDeclContext())) {
    Diag(AtLoc, diag::err_duplicate_property);
    Diag(PIDecl->getLocation(), diag::note_property_declare);
    return nullptr;
  }

  // Check for consistency with the previous declaration, if there is one.
  if (PIDecl) {
    // A readonly property declared in the primary class can be refined
    // by adding a readwrite property within an extension.
    // Anything else is an error.
    if (!(PIDecl->isReadOnly() && isReadWrite)) {
      // Tailor the diagnostics for the common case where a readwrite
      // property is declared both in the @interface and the continuation.
      // This is a common error where the user often intended the original
      // declaration to be readonly.
      unsigned diag =
          (Attributes & ObjCPropertyAttribute::kind_readwrite) &&
                  (PIDecl->getPropertyAttributesAsWritten() &
                   ObjCPropertyAttribute::kind_readwrite)
              ? diag::err_use_continuation_class_redeclaration_readwrite
              : diag::err_use_continuation_class;
      Diag(AtLoc, diag)
        << CCPrimary->getDeclName();
      Diag(PIDecl->getLocation(), diag::note_property_declare);
      return nullptr;
    }

    // Check for consistency of getters.
    if (PIDecl->getGetterName() != GetterSel) {
     // If the getter was written explicitly, complain.
     if (AttributesAsWritten & ObjCPropertyAttribute::kind_getter) {
       Diag(AtLoc, diag::warn_property_redecl_getter_mismatch)
           << PIDecl->getGetterName() << GetterSel;
       Diag(PIDecl->getLocation(), diag::note_property_declare);
     }

      // Always adopt the getter from the original declaration.
      GetterSel = PIDecl->getGetterName();
      Attributes |= ObjCPropertyAttribute::kind_getter;
    }

    // Check consistency of ownership.
    unsigned ExistingOwnership
      = getOwnershipRule(PIDecl->getPropertyAttributes());
    unsigned NewOwnership = getOwnershipRule(Attributes);
    if (ExistingOwnership && NewOwnership != ExistingOwnership) {
      // If the ownership was written explicitly, complain.
      if (getOwnershipRule(AttributesAsWritten)) {
        Diag(AtLoc, diag::warn_property_attr_mismatch);
        Diag(PIDecl->getLocation(), diag::note_property_declare);
      }

      // Take the ownership from the original property.
      Attributes = (Attributes & ~OwnershipMask) | ExistingOwnership;
    }

    // If the redeclaration is 'weak' but the original property is not,
    if ((Attributes & ObjCPropertyAttribute::kind_weak) &&
        !(PIDecl->getPropertyAttributesAsWritten() &
          ObjCPropertyAttribute::kind_weak) &&
        PIDecl->getType()->getAs<ObjCObjectPointerType>() &&
        PIDecl->getType().getObjCLifetime() == Qualifiers::OCL_None) {
      Diag(AtLoc, diag::warn_property_implicitly_mismatched);
      Diag(PIDecl->getLocation(), diag::note_property_declare);
    }
  }

  // Create a new ObjCPropertyDecl with the DeclContext being
  // the class extension.
  ObjCPropertyDecl *PDecl = CreatePropertyDecl(S, CDecl, AtLoc, LParenLoc,
                                               FD, GetterSel, GetterNameLoc,
                                               SetterSel, SetterNameLoc,
                                               isReadWrite,
                                               Attributes, AttributesAsWritten,
                                               T, TSI, MethodImplKind, DC);
  ASTContext &Context = getASTContext();
  // If there was no declaration of a property with the same name in
  // the primary class, we're done.
  if (!PIDecl) {
    ProcessPropertyDecl(PDecl);
    return PDecl;
  }

  if (!Context.hasSameType(PIDecl->getType(), PDecl->getType())) {
    bool IncompatibleObjC = false;
    QualType ConvertedType;
    // Relax the strict type matching for property type in continuation class.
    // Allow property object type of continuation class to be different as long
    // as it narrows the object type in its primary class property. Note that
    // this conversion is safe only because the wider type is for a 'readonly'
    // property in primary class and 'narrowed' type for a 'readwrite' property
    // in continuation class.
    QualType PrimaryClassPropertyT = Context.getCanonicalType(PIDecl->getType());
    QualType ClassExtPropertyT = Context.getCanonicalType(PDecl->getType());
    if (!isa<ObjCObjectPointerType>(PrimaryClassPropertyT) ||
        !isa<ObjCObjectPointerType>(ClassExtPropertyT) ||
        (!SemaRef.isObjCPointerConversion(ClassExtPropertyT,
                                          PrimaryClassPropertyT, ConvertedType,
                                          IncompatibleObjC)) ||
        IncompatibleObjC) {
      Diag(AtLoc,
          diag::err_type_mismatch_continuation_class) << PDecl->getType();
      Diag(PIDecl->getLocation(), diag::note_property_declare);
      return nullptr;
    }
  }

  // Check that atomicity of property in class extension matches the previous
  // declaration.
  checkAtomicPropertyMismatch(SemaRef, PIDecl, PDecl, true);

  // Make sure getter/setter are appropriately synthesized.
  ProcessPropertyDecl(PDecl);
  return PDecl;
}

ObjCPropertyDecl *SemaObjC::CreatePropertyDecl(
    Scope *S, ObjCContainerDecl *CDecl, SourceLocation AtLoc,
    SourceLocation LParenLoc, FieldDeclarator &FD, Selector GetterSel,
    SourceLocation GetterNameLoc, Selector SetterSel,
    SourceLocation SetterNameLoc, const bool isReadWrite,
    const unsigned Attributes, const unsigned AttributesAsWritten, QualType T,
    TypeSourceInfo *TInfo, tok::ObjCKeywordKind MethodImplKind,
    DeclContext *lexicalDC) {
  ASTContext &Context = getASTContext();
  const IdentifierInfo *PropertyId = FD.D.getIdentifier();

  // Property defaults to 'assign' if it is readwrite, unless this is ARC
  // and the type is retainable.
  bool isAssign;
  if (Attributes & (ObjCPropertyAttribute::kind_assign |
                    ObjCPropertyAttribute::kind_unsafe_unretained)) {
    isAssign = true;
  } else if (getOwnershipRule(Attributes) || !isReadWrite) {
    isAssign = false;
  } else {
    isAssign = (!getLangOpts().ObjCAutoRefCount ||
                !T->isObjCRetainableType());
  }

  // Issue a warning if property is 'assign' as default and its
  // object, which is gc'able conforms to NSCopying protocol
  if (getLangOpts().getGC() != LangOptions::NonGC && isAssign &&
      !(Attributes & ObjCPropertyAttribute::kind_assign)) {
    if (const ObjCObjectPointerType *ObjPtrTy =
          T->getAs<ObjCObjectPointerType>()) {
      ObjCInterfaceDecl *IDecl = ObjPtrTy->getObjectType()->getInterface();
      if (IDecl)
        if (ObjCProtocolDecl* PNSCopying =
            LookupProtocol(&Context.Idents.get("NSCopying"), AtLoc))
          if (IDecl->ClassImplementsProtocol(PNSCopying, true))
            Diag(AtLoc, diag::warn_implements_nscopying) << PropertyId;
    }
  }

  if (T->isObjCObjectType()) {
    SourceLocation StarLoc = TInfo->getTypeLoc().getEndLoc();
    StarLoc = SemaRef.getLocForEndOfToken(StarLoc);
    Diag(FD.D.getIdentifierLoc(), diag::err_statically_allocated_object)
      << FixItHint::CreateInsertion(StarLoc, "*");
    T = Context.getObjCObjectPointerType(T);
    SourceLocation TLoc = TInfo->getTypeLoc().getBeginLoc();
    TInfo = Context.getTrivialTypeSourceInfo(T, TLoc);
  }

  DeclContext *DC = CDecl;
  ObjCPropertyDecl *PDecl = ObjCPropertyDecl::Create(Context, DC,
                                                     FD.D.getIdentifierLoc(),
                                                     PropertyId, AtLoc,
                                                     LParenLoc, T, TInfo);

  bool isClassProperty =
      (AttributesAsWritten & ObjCPropertyAttribute::kind_class) ||
      (Attributes & ObjCPropertyAttribute::kind_class);
  // Class property and instance property can have the same name.
  if (ObjCPropertyDecl *prevDecl = ObjCPropertyDecl::findPropertyDecl(
          DC, PropertyId, ObjCPropertyDecl::getQueryKind(isClassProperty))) {
    Diag(PDecl->getLocation(), diag::err_duplicate_property);
    Diag(prevDecl->getLocation(), diag::note_property_declare);
    PDecl->setInvalidDecl();
  }
  else {
    DC->addDecl(PDecl);
    if (lexicalDC)
      PDecl->setLexicalDeclContext(lexicalDC);
  }

  if (T->isArrayType() || T->isFunctionType()) {
    Diag(AtLoc, diag::err_property_type) << T;
    PDecl->setInvalidDecl();
  }

  // Regardless of setter/getter attribute, we save the default getter/setter
  // selector names in anticipation of declaration of setter/getter methods.
  PDecl->setGetterName(GetterSel, GetterNameLoc);
  PDecl->setSetterName(SetterSel, SetterNameLoc);
  PDecl->setPropertyAttributesAsWritten(
                          makePropertyAttributesAsWritten(AttributesAsWritten));

  SemaRef.ProcessDeclAttributes(S, PDecl, FD.D);

  if (Attributes & ObjCPropertyAttribute::kind_readonly)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_readonly);

  if (Attributes & ObjCPropertyAttribute::kind_getter)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_getter);

  if (Attributes & ObjCPropertyAttribute::kind_setter)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_setter);

  if (isReadWrite)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_readwrite);

  if (Attributes & ObjCPropertyAttribute::kind_retain)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_retain);

  if (Attributes & ObjCPropertyAttribute::kind_strong)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_strong);

  if (Attributes & ObjCPropertyAttribute::kind_weak)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_weak);

  if (Attributes & ObjCPropertyAttribute::kind_copy)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_copy);

  if (Attributes & ObjCPropertyAttribute::kind_unsafe_unretained)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_unsafe_unretained);

  if (isAssign)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_assign);

  // In the semantic attributes, one of nonatomic or atomic is always set.
  if (Attributes & ObjCPropertyAttribute::kind_nonatomic)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_nonatomic);
  else
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_atomic);

  // 'unsafe_unretained' is alias for 'assign'.
  if (Attributes & ObjCPropertyAttribute::kind_unsafe_unretained)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_assign);
  if (isAssign)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_unsafe_unretained);

  if (MethodImplKind == tok::objc_required)
    PDecl->setPropertyImplementation(ObjCPropertyDecl::Required);
  else if (MethodImplKind == tok::objc_optional)
    PDecl->setPropertyImplementation(ObjCPropertyDecl::Optional);

  if (Attributes & ObjCPropertyAttribute::kind_nullability)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_nullability);

  if (Attributes & ObjCPropertyAttribute::kind_null_resettable)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_null_resettable);

  if (Attributes & ObjCPropertyAttribute::kind_class)
    PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_class);

  if ((Attributes & ObjCPropertyAttribute::kind_direct) ||
      CDecl->hasAttr<ObjCDirectMembersAttr>()) {
    if (isa<ObjCProtocolDecl>(CDecl)) {
      Diag(PDecl->getLocation(), diag::err_objc_direct_on_protocol) << true;
    } else if (getLangOpts().ObjCRuntime.allowsDirectDispatch()) {
      PDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_direct);
    } else {
      Diag(PDecl->getLocation(), diag::warn_objc_direct_property_ignored)
          << PDecl->getDeclName();
    }
  }

  return PDecl;
}

static void checkARCPropertyImpl(Sema &S, SourceLocation propertyImplLoc,
                                 ObjCPropertyDecl *property,
                                 ObjCIvarDecl *ivar) {
  if (property->isInvalidDecl() || ivar->isInvalidDecl()) return;

  QualType ivarType = ivar->getType();
  Qualifiers::ObjCLifetime ivarLifetime = ivarType.getObjCLifetime();

  // The lifetime implied by the property's attributes.
  Qualifiers::ObjCLifetime propertyLifetime =
    getImpliedARCOwnership(property->getPropertyAttributes(),
                           property->getType());

  // We're fine if they match.
  if (propertyLifetime == ivarLifetime) return;

  // None isn't a valid lifetime for an object ivar in ARC, and
  // __autoreleasing is never valid; don't diagnose twice.
  if ((ivarLifetime == Qualifiers::OCL_None &&
       S.getLangOpts().ObjCAutoRefCount) ||
      ivarLifetime == Qualifiers::OCL_Autoreleasing)
    return;

  // If the ivar is private, and it's implicitly __unsafe_unretained
  // because of its type, then pretend it was actually implicitly
  // __strong.  This is only sound because we're processing the
  // property implementation before parsing any method bodies.
  if (ivarLifetime == Qualifiers::OCL_ExplicitNone &&
      propertyLifetime == Qualifiers::OCL_Strong &&
      ivar->getAccessControl() == ObjCIvarDecl::Private) {
    SplitQualType split = ivarType.split();
    if (split.Quals.hasObjCLifetime()) {
      assert(ivarType->isObjCARCImplicitlyUnretainedType());
      split.Quals.setObjCLifetime(Qualifiers::OCL_Strong);
      ivarType = S.Context.getQualifiedType(split);
      ivar->setType(ivarType);
      return;
    }
  }

  switch (propertyLifetime) {
  case Qualifiers::OCL_Strong:
    S.Diag(ivar->getLocation(), diag::err_arc_strong_property_ownership)
      << property->getDeclName()
      << ivar->getDeclName()
      << ivarLifetime;
    break;

  case Qualifiers::OCL_Weak:
    S.Diag(ivar->getLocation(), diag::err_weak_property)
      << property->getDeclName()
      << ivar->getDeclName();
    break;

  case Qualifiers::OCL_ExplicitNone:
    S.Diag(ivar->getLocation(), diag::err_arc_assign_property_ownership)
        << property->getDeclName() << ivar->getDeclName()
        << ((property->getPropertyAttributesAsWritten() &
             ObjCPropertyAttribute::kind_assign) != 0);
    break;

  case Qualifiers::OCL_Autoreleasing:
    llvm_unreachable("properties cannot be autoreleasing");

  case Qualifiers::OCL_None:
    // Any other property should be ignored.
    return;
  }

  S.Diag(property->getLocation(), diag::note_property_declare);
  if (propertyImplLoc.isValid())
    S.Diag(propertyImplLoc, diag::note_property_synthesize);
}

/// setImpliedPropertyAttributeForReadOnlyProperty -
/// This routine evaludates life-time attributes for a 'readonly'
/// property with no known lifetime of its own, using backing
/// 'ivar's attribute, if any. If no backing 'ivar', property's
/// life-time is assumed 'strong'.
static void setImpliedPropertyAttributeForReadOnlyProperty(
              ObjCPropertyDecl *property, ObjCIvarDecl *ivar) {
  Qualifiers::ObjCLifetime propertyLifetime =
    getImpliedARCOwnership(property->getPropertyAttributes(),
                           property->getType());
  if (propertyLifetime != Qualifiers::OCL_None)
    return;

  if (!ivar) {
    // if no backing ivar, make property 'strong'.
    property->setPropertyAttributes(ObjCPropertyAttribute::kind_strong);
    return;
  }
  // property assumes owenership of backing ivar.
  QualType ivarType = ivar->getType();
  Qualifiers::ObjCLifetime ivarLifetime = ivarType.getObjCLifetime();
  if (ivarLifetime == Qualifiers::OCL_Strong)
    property->setPropertyAttributes(ObjCPropertyAttribute::kind_strong);
  else if (ivarLifetime == Qualifiers::OCL_Weak)
    property->setPropertyAttributes(ObjCPropertyAttribute::kind_weak);
}

static bool isIncompatiblePropertyAttribute(unsigned Attr1, unsigned Attr2,
                                            ObjCPropertyAttribute::Kind Kind) {
  return (Attr1 & Kind) != (Attr2 & Kind);
}

static bool areIncompatiblePropertyAttributes(unsigned Attr1, unsigned Attr2,
                                              unsigned Kinds) {
  return ((Attr1 & Kinds) != 0) != ((Attr2 & Kinds) != 0);
}

/// SelectPropertyForSynthesisFromProtocols - Finds the most appropriate
/// property declaration that should be synthesised in all of the inherited
/// protocols. It also diagnoses properties declared in inherited protocols with
/// mismatched types or attributes, since any of them can be candidate for
/// synthesis.
static ObjCPropertyDecl *
SelectPropertyForSynthesisFromProtocols(Sema &S, SourceLocation AtLoc,
                                        ObjCInterfaceDecl *ClassDecl,
                                        ObjCPropertyDecl *Property) {
  assert(isa<ObjCProtocolDecl>(Property->getDeclContext()) &&
         "Expected a property from a protocol");
  ObjCInterfaceDecl::ProtocolPropertySet ProtocolSet;
  ObjCInterfaceDecl::PropertyDeclOrder Properties;
  for (const auto *PI : ClassDecl->all_referenced_protocols()) {
    if (const ObjCProtocolDecl *PDecl = PI->getDefinition())
      PDecl->collectInheritedProtocolProperties(Property, ProtocolSet,
                                                Properties);
  }
  if (ObjCInterfaceDecl *SDecl = ClassDecl->getSuperClass()) {
    while (SDecl) {
      for (const auto *PI : SDecl->all_referenced_protocols()) {
        if (const ObjCProtocolDecl *PDecl = PI->getDefinition())
          PDecl->collectInheritedProtocolProperties(Property, ProtocolSet,
                                                    Properties);
      }
      SDecl = SDecl->getSuperClass();
    }
  }

  if (Properties.empty())
    return Property;

  ObjCPropertyDecl *OriginalProperty = Property;
  size_t SelectedIndex = 0;
  for (const auto &Prop : llvm::enumerate(Properties)) {
    // Select the 'readwrite' property if such property exists.
    if (Property->isReadOnly() && !Prop.value()->isReadOnly()) {
      Property = Prop.value();
      SelectedIndex = Prop.index();
    }
  }
  if (Property != OriginalProperty) {
    // Check that the old property is compatible with the new one.
    Properties[SelectedIndex] = OriginalProperty;
  }

  QualType RHSType = S.Context.getCanonicalType(Property->getType());
  unsigned OriginalAttributes = Property->getPropertyAttributesAsWritten();
  enum MismatchKind {
    IncompatibleType = 0,
    HasNoExpectedAttribute,
    HasUnexpectedAttribute,
    DifferentGetter,
    DifferentSetter
  };
  // Represents a property from another protocol that conflicts with the
  // selected declaration.
  struct MismatchingProperty {
    const ObjCPropertyDecl *Prop;
    MismatchKind Kind;
    StringRef AttributeName;
  };
  SmallVector<MismatchingProperty, 4> Mismatches;
  for (ObjCPropertyDecl *Prop : Properties) {
    // Verify the property attributes.
    unsigned Attr = Prop->getPropertyAttributesAsWritten();
    if (Attr != OriginalAttributes) {
      auto Diag = [&](bool OriginalHasAttribute, StringRef AttributeName) {
        MismatchKind Kind = OriginalHasAttribute ? HasNoExpectedAttribute
                                                 : HasUnexpectedAttribute;
        Mismatches.push_back({Prop, Kind, AttributeName});
      };
      // The ownership might be incompatible unless the property has no explicit
      // ownership.
      bool HasOwnership =
          (Attr & (ObjCPropertyAttribute::kind_retain |
                   ObjCPropertyAttribute::kind_strong |
                   ObjCPropertyAttribute::kind_copy |
                   ObjCPropertyAttribute::kind_assign |
                   ObjCPropertyAttribute::kind_unsafe_unretained |
                   ObjCPropertyAttribute::kind_weak)) != 0;
      if (HasOwnership &&
          isIncompatiblePropertyAttribute(OriginalAttributes, Attr,
                                          ObjCPropertyAttribute::kind_copy)) {
        Diag(OriginalAttributes & ObjCPropertyAttribute::kind_copy, "copy");
        continue;
      }
      if (HasOwnership && areIncompatiblePropertyAttributes(
                              OriginalAttributes, Attr,
                              ObjCPropertyAttribute::kind_retain |
                                  ObjCPropertyAttribute::kind_strong)) {
        Diag(OriginalAttributes & (ObjCPropertyAttribute::kind_retain |
                                   ObjCPropertyAttribute::kind_strong),
             "retain (or strong)");
        continue;
      }
      if (isIncompatiblePropertyAttribute(OriginalAttributes, Attr,
                                          ObjCPropertyAttribute::kind_atomic)) {
        Diag(OriginalAttributes & ObjCPropertyAttribute::kind_atomic, "atomic");
        continue;
      }
    }
    if (Property->getGetterName() != Prop->getGetterName()) {
      Mismatches.push_back({Prop, DifferentGetter, ""});
      continue;
    }
    if (!Property->isReadOnly() && !Prop->isReadOnly() &&
        Property->getSetterName() != Prop->getSetterName()) {
      Mismatches.push_back({Prop, DifferentSetter, ""});
      continue;
    }
    QualType LHSType = S.Context.getCanonicalType(Prop->getType());
    if (!S.Context.propertyTypesAreCompatible(LHSType, RHSType)) {
      bool IncompatibleObjC = false;
      QualType ConvertedType;
      if (!S.isObjCPointerConversion(RHSType, LHSType, ConvertedType, IncompatibleObjC)
          || IncompatibleObjC) {
        Mismatches.push_back({Prop, IncompatibleType, ""});
        continue;
      }
    }
  }

  if (Mismatches.empty())
    return Property;

  // Diagnose incompability.
  {
    bool HasIncompatibleAttributes = false;
    for (const auto &Note : Mismatches)
      HasIncompatibleAttributes =
          Note.Kind != IncompatibleType ? true : HasIncompatibleAttributes;
    // Promote the warning to an error if there are incompatible attributes or
    // incompatible types together with readwrite/readonly incompatibility.
    auto Diag = S.Diag(Property->getLocation(),
                       Property != OriginalProperty || HasIncompatibleAttributes
                           ? diag::err_protocol_property_mismatch
                           : diag::warn_protocol_property_mismatch);
    Diag << Mismatches[0].Kind;
    switch (Mismatches[0].Kind) {
    case IncompatibleType:
      Diag << Property->getType();
      break;
    case HasNoExpectedAttribute:
    case HasUnexpectedAttribute:
      Diag << Mismatches[0].AttributeName;
      break;
    case DifferentGetter:
      Diag << Property->getGetterName();
      break;
    case DifferentSetter:
      Diag << Property->getSetterName();
      break;
    }
  }
  for (const auto &Note : Mismatches) {
    auto Diag =
        S.Diag(Note.Prop->getLocation(), diag::note_protocol_property_declare)
        << Note.Kind;
    switch (Note.Kind) {
    case IncompatibleType:
      Diag << Note.Prop->getType();
      break;
    case HasNoExpectedAttribute:
    case HasUnexpectedAttribute:
      Diag << Note.AttributeName;
      break;
    case DifferentGetter:
      Diag << Note.Prop->getGetterName();
      break;
    case DifferentSetter:
      Diag << Note.Prop->getSetterName();
      break;
    }
  }
  if (AtLoc.isValid())
    S.Diag(AtLoc, diag::note_property_synthesize);

  return Property;
}

/// Determine whether any storage attributes were written on the property.
static bool hasWrittenStorageAttribute(ObjCPropertyDecl *Prop,
                                       ObjCPropertyQueryKind QueryKind) {
  if (Prop->getPropertyAttributesAsWritten() & OwnershipMask) return true;

  // If this is a readwrite property in a class extension that refines
  // a readonly property in the original class definition, check it as
  // well.

  // If it's a readonly property, we're not interested.
  if (Prop->isReadOnly()) return false;

  // Is it declared in an extension?
  auto Category = dyn_cast<ObjCCategoryDecl>(Prop->getDeclContext());
  if (!Category || !Category->IsClassExtension()) return false;

  // Find the corresponding property in the primary class definition.
  auto OrigClass = Category->getClassInterface();
  for (auto *Found : OrigClass->lookup(Prop->getDeclName())) {
    if (ObjCPropertyDecl *OrigProp = dyn_cast<ObjCPropertyDecl>(Found))
      return OrigProp->getPropertyAttributesAsWritten() & OwnershipMask;
  }

  // Look through all of the protocols.
  for (const auto *Proto : OrigClass->all_referenced_protocols()) {
    if (ObjCPropertyDecl *OrigProp = Proto->FindPropertyDeclaration(
            Prop->getIdentifier(), QueryKind))
      return OrigProp->getPropertyAttributesAsWritten() & OwnershipMask;
  }

  return false;
}

/// Create a synthesized property accessor stub inside the \@implementation.
static ObjCMethodDecl *
RedeclarePropertyAccessor(ASTContext &Context, ObjCImplementationDecl *Impl,
                          ObjCMethodDecl *AccessorDecl, SourceLocation AtLoc,
                          SourceLocation PropertyLoc) {
  ObjCMethodDecl *Decl = AccessorDecl;
  ObjCMethodDecl *ImplDecl = ObjCMethodDecl::Create(
      Context, AtLoc.isValid() ? AtLoc : Decl->getBeginLoc(),
      PropertyLoc.isValid() ? PropertyLoc : Decl->getEndLoc(),
      Decl->getSelector(), Decl->getReturnType(),
      Decl->getReturnTypeSourceInfo(), Impl, Decl->isInstanceMethod(),
      Decl->isVariadic(), Decl->isPropertyAccessor(),
      /* isSynthesized*/ true, Decl->isImplicit(), Decl->isDefined(),
      Decl->getImplementationControl(), Decl->hasRelatedResultType());
  ImplDecl->getMethodFamily();
  if (Decl->hasAttrs())
    ImplDecl->setAttrs(Decl->getAttrs());
  ImplDecl->setSelfDecl(Decl->getSelfDecl());
  ImplDecl->setCmdDecl(Decl->getCmdDecl());
  SmallVector<SourceLocation, 1> SelLocs;
  Decl->getSelectorLocs(SelLocs);
  ImplDecl->setMethodParams(Context, Decl->parameters(), SelLocs);
  ImplDecl->setLexicalDeclContext(Impl);
  ImplDecl->setDefined(false);
  return ImplDecl;
}

/// ActOnPropertyImplDecl - This routine performs semantic checks and
/// builds the AST node for a property implementation declaration; declared
/// as \@synthesize or \@dynamic.
///
Decl *SemaObjC::ActOnPropertyImplDecl(
    Scope *S, SourceLocation AtLoc, SourceLocation PropertyLoc, bool Synthesize,
    IdentifierInfo *PropertyId, IdentifierInfo *PropertyIvar,
    SourceLocation PropertyIvarLoc, ObjCPropertyQueryKind QueryKind) {
  ASTContext &Context = getASTContext();
  ObjCContainerDecl *ClassImpDecl =
      dyn_cast<ObjCContainerDecl>(SemaRef.CurContext);
  // Make sure we have a context for the property implementation declaration.
  if (!ClassImpDecl) {
    Diag(AtLoc, diag::err_missing_property_context);
    return nullptr;
  }
  if (PropertyIvarLoc.isInvalid())
    PropertyIvarLoc = PropertyLoc;
  SourceLocation PropertyDiagLoc = PropertyLoc;
  if (PropertyDiagLoc.isInvalid())
    PropertyDiagLoc = ClassImpDecl->getBeginLoc();
  ObjCPropertyDecl *property = nullptr;
  ObjCInterfaceDecl *IDecl = nullptr;
  // Find the class or category class where this property must have
  // a declaration.
  ObjCImplementationDecl *IC = nullptr;
  ObjCCategoryImplDecl *CatImplClass = nullptr;
  if ((IC = dyn_cast<ObjCImplementationDecl>(ClassImpDecl))) {
    IDecl = IC->getClassInterface();
    // We always synthesize an interface for an implementation
    // without an interface decl. So, IDecl is always non-zero.
    assert(IDecl &&
           "ActOnPropertyImplDecl - @implementation without @interface");

    // Look for this property declaration in the @implementation's @interface
    property = IDecl->FindPropertyDeclaration(PropertyId, QueryKind);
    if (!property) {
      Diag(PropertyLoc, diag::err_bad_property_decl) << IDecl->getDeclName();
      return nullptr;
    }
    if (property->isClassProperty() && Synthesize) {
      Diag(PropertyLoc, diag::err_synthesize_on_class_property) << PropertyId;
      return nullptr;
    }
    unsigned PIkind = property->getPropertyAttributesAsWritten();
    if ((PIkind & (ObjCPropertyAttribute::kind_atomic |
                   ObjCPropertyAttribute::kind_nonatomic)) == 0) {
      if (AtLoc.isValid())
        Diag(AtLoc, diag::warn_implicit_atomic_property);
      else
        Diag(IC->getLocation(), diag::warn_auto_implicit_atomic_property);
      Diag(property->getLocation(), diag::note_property_declare);
    }

    if (const ObjCCategoryDecl *CD =
        dyn_cast<ObjCCategoryDecl>(property->getDeclContext())) {
      if (!CD->IsClassExtension()) {
        Diag(PropertyLoc, diag::err_category_property) << CD->getDeclName();
        Diag(property->getLocation(), diag::note_property_declare);
        return nullptr;
      }
    }
    if (Synthesize && (PIkind & ObjCPropertyAttribute::kind_readonly) &&
        property->hasAttr<IBOutletAttr>() && !AtLoc.isValid()) {
      bool ReadWriteProperty = false;
      // Search into the class extensions and see if 'readonly property is
      // redeclared 'readwrite', then no warning is to be issued.
      for (auto *Ext : IDecl->known_extensions()) {
        DeclContext::lookup_result R = Ext->lookup(property->getDeclName());
        if (auto *ExtProp = R.find_first<ObjCPropertyDecl>()) {
          PIkind = ExtProp->getPropertyAttributesAsWritten();
          if (PIkind & ObjCPropertyAttribute::kind_readwrite) {
            ReadWriteProperty = true;
            break;
          }
        }
      }

      if (!ReadWriteProperty) {
        Diag(property->getLocation(), diag::warn_auto_readonly_iboutlet_property)
            << property;
        SourceLocation readonlyLoc;
        if (LocPropertyAttribute(Context, "readonly",
                                 property->getLParenLoc(), readonlyLoc)) {
          SourceLocation endLoc =
            readonlyLoc.getLocWithOffset(strlen("readonly")-1);
          SourceRange ReadonlySourceRange(readonlyLoc, endLoc);
          Diag(property->getLocation(),
               diag::note_auto_readonly_iboutlet_fixup_suggest) <<
          FixItHint::CreateReplacement(ReadonlySourceRange, "readwrite");
        }
      }
    }
    if (Synthesize && isa<ObjCProtocolDecl>(property->getDeclContext()))
      property = SelectPropertyForSynthesisFromProtocols(SemaRef, AtLoc, IDecl,
                                                         property);

  } else if ((CatImplClass = dyn_cast<ObjCCategoryImplDecl>(ClassImpDecl))) {
    if (Synthesize) {
      Diag(AtLoc, diag::err_synthesize_category_decl);
      return nullptr;
    }
    IDecl = CatImplClass->getClassInterface();
    if (!IDecl) {
      Diag(AtLoc, diag::err_missing_property_interface);
      return nullptr;
    }
    ObjCCategoryDecl *Category =
    IDecl->FindCategoryDeclaration(CatImplClass->getIdentifier());

    // If category for this implementation not found, it is an error which
    // has already been reported eralier.
    if (!Category)
      return nullptr;
    // Look for this property declaration in @implementation's category
    property = Category->FindPropertyDeclaration(PropertyId, QueryKind);
    if (!property) {
      Diag(PropertyLoc, diag::err_bad_category_property_decl)
      << Category->getDeclName();
      return nullptr;
    }
  } else {
    Diag(AtLoc, diag::err_bad_property_context);
    return nullptr;
  }
  ObjCIvarDecl *Ivar = nullptr;
  bool CompleteTypeErr = false;
  bool compat = true;
  // Check that we have a valid, previously declared ivar for @synthesize
  if (Synthesize) {
    // @synthesize
    if (!PropertyIvar)
      PropertyIvar = PropertyId;
    // Check that this is a previously declared 'ivar' in 'IDecl' interface
    ObjCInterfaceDecl *ClassDeclared;
    Ivar = IDecl->lookupInstanceVariable(PropertyIvar, ClassDeclared);
    QualType PropType = property->getType();
    QualType PropertyIvarType = PropType.getNonReferenceType();

    if (SemaRef.RequireCompleteType(PropertyDiagLoc, PropertyIvarType,
                                    diag::err_incomplete_synthesized_property,
                                    property->getDeclName())) {
      Diag(property->getLocation(), diag::note_property_declare);
      CompleteTypeErr = true;
    }

    if (getLangOpts().ObjCAutoRefCount &&
        (property->getPropertyAttributesAsWritten() &
         ObjCPropertyAttribute::kind_readonly) &&
        PropertyIvarType->isObjCRetainableType()) {
      setImpliedPropertyAttributeForReadOnlyProperty(property, Ivar);
    }

    ObjCPropertyAttribute::Kind kind = property->getPropertyAttributes();

    bool isARCWeak = false;
    if (kind & ObjCPropertyAttribute::kind_weak) {
      // Add GC __weak to the ivar type if the property is weak.
      if (getLangOpts().getGC() != LangOptions::NonGC) {
        assert(!getLangOpts().ObjCAutoRefCount);
        if (PropertyIvarType.isObjCGCStrong()) {
          Diag(PropertyDiagLoc, diag::err_gc_weak_property_strong_type);
          Diag(property->getLocation(), diag::note_property_declare);
        } else {
          PropertyIvarType =
            Context.getObjCGCQualType(PropertyIvarType, Qualifiers::Weak);
        }

      // Otherwise, check whether ARC __weak is enabled and works with
      // the property type.
      } else {
        if (!getLangOpts().ObjCWeak) {
          // Only complain here when synthesizing an ivar.
          if (!Ivar) {
            Diag(PropertyDiagLoc,
                 getLangOpts().ObjCWeakRuntime
                   ? diag::err_synthesizing_arc_weak_property_disabled
                   : diag::err_synthesizing_arc_weak_property_no_runtime);
            Diag(property->getLocation(), diag::note_property_declare);
          }
          CompleteTypeErr = true; // suppress later diagnostics about the ivar
        } else {
          isARCWeak = true;
          if (const ObjCObjectPointerType *ObjT =
                PropertyIvarType->getAs<ObjCObjectPointerType>()) {
            const ObjCInterfaceDecl *ObjI = ObjT->getInterfaceDecl();
            if (ObjI && ObjI->isArcWeakrefUnavailable()) {
              Diag(property->getLocation(),
                   diag::err_arc_weak_unavailable_property)
                << PropertyIvarType;
              Diag(ClassImpDecl->getLocation(), diag::note_implemented_by_class)
                << ClassImpDecl->getName();
            }
          }
        }
      }
    }

    if (AtLoc.isInvalid()) {
      // Check when default synthesizing a property that there is
      // an ivar matching property name and issue warning; since this
      // is the most common case of not using an ivar used for backing
      // property in non-default synthesis case.
      ObjCInterfaceDecl *ClassDeclared=nullptr;
      ObjCIvarDecl *originalIvar =
      IDecl->lookupInstanceVariable(property->getIdentifier(),
                                    ClassDeclared);
      if (originalIvar) {
        Diag(PropertyDiagLoc,
             diag::warn_autosynthesis_property_ivar_match)
        << PropertyId << (Ivar == nullptr) << PropertyIvar
        << originalIvar->getIdentifier();
        Diag(property->getLocation(), diag::note_property_declare);
        Diag(originalIvar->getLocation(), diag::note_ivar_decl);
      }
    }

    if (!Ivar) {
      // In ARC, give the ivar a lifetime qualifier based on the
      // property attributes.
      if ((getLangOpts().ObjCAutoRefCount || isARCWeak) &&
          !PropertyIvarType.getObjCLifetime() &&
          PropertyIvarType->isObjCRetainableType()) {

        // It's an error if we have to do this and the user didn't
        // explicitly write an ownership attribute on the property.
        if (!hasWrittenStorageAttribute(property, QueryKind) &&
            !(kind & ObjCPropertyAttribute::kind_strong)) {
          Diag(PropertyDiagLoc,
               diag::err_arc_objc_property_default_assign_on_object);
          Diag(property->getLocation(), diag::note_property_declare);
        } else {
          Qualifiers::ObjCLifetime lifetime =
            getImpliedARCOwnership(kind, PropertyIvarType);
          assert(lifetime && "no lifetime for property?");

          Qualifiers qs;
          qs.addObjCLifetime(lifetime);
          PropertyIvarType = Context.getQualifiedType(PropertyIvarType, qs);
        }
      }

      Ivar = ObjCIvarDecl::Create(Context, ClassImpDecl,
                                  PropertyIvarLoc,PropertyIvarLoc, PropertyIvar,
                                  PropertyIvarType, /*TInfo=*/nullptr,
                                  ObjCIvarDecl::Private,
                                  (Expr *)nullptr, true);
      if (SemaRef.RequireNonAbstractType(PropertyIvarLoc, PropertyIvarType,
                                         diag::err_abstract_type_in_decl,
                                         Sema::AbstractSynthesizedIvarType)) {
        Diag(property->getLocation(), diag::note_property_declare);
        // An abstract type is as bad as an incomplete type.
        CompleteTypeErr = true;
      }
      if (!CompleteTypeErr) {
        const RecordType *RecordTy = PropertyIvarType->getAs<RecordType>();
        if (RecordTy && RecordTy->getDecl()->hasFlexibleArrayMember()) {
          Diag(PropertyIvarLoc, diag::err_synthesize_variable_sized_ivar)
            << PropertyIvarType;
          CompleteTypeErr = true; // suppress later diagnostics about the ivar
        }
      }
      if (CompleteTypeErr)
        Ivar->setInvalidDecl();
      ClassImpDecl->addDecl(Ivar);
      IDecl->makeDeclVisibleInContext(Ivar);

      if (getLangOpts().ObjCRuntime.isFragile())
        Diag(PropertyDiagLoc, diag::err_missing_property_ivar_decl)
            << PropertyId;
      // Note! I deliberately want it to fall thru so, we have a
      // a property implementation and to avoid future warnings.
    } else if (getLangOpts().ObjCRuntime.isNonFragile() &&
               !declaresSameEntity(ClassDeclared, IDecl)) {
      Diag(PropertyDiagLoc, diag::err_ivar_in_superclass_use)
      << property->getDeclName() << Ivar->getDeclName()
      << ClassDeclared->getDeclName();
      Diag(Ivar->getLocation(), diag::note_previous_access_declaration)
      << Ivar << Ivar->getName();
      // Note! I deliberately want it to fall thru so more errors are caught.
    }
    property->setPropertyIvarDecl(Ivar);

    QualType IvarType = Context.getCanonicalType(Ivar->getType());

    // Check that type of property and its ivar are type compatible.
    if (!Context.hasSameType(PropertyIvarType, IvarType)) {
      if (isa<ObjCObjectPointerType>(PropertyIvarType)
          && isa<ObjCObjectPointerType>(IvarType))
        compat = Context.canAssignObjCInterfaces(
            PropertyIvarType->castAs<ObjCObjectPointerType>(),
            IvarType->castAs<ObjCObjectPointerType>());
      else {
        compat = (SemaRef.CheckAssignmentConstraints(
                      PropertyIvarLoc, PropertyIvarType, IvarType) ==
                  Sema::Compatible);
      }
      if (!compat) {
        Diag(PropertyDiagLoc, diag::err_property_ivar_type)
          << property->getDeclName() << PropType
          << Ivar->getDeclName() << IvarType;
        Diag(Ivar->getLocation(), diag::note_ivar_decl);
        // Note! I deliberately want it to fall thru so, we have a
        // a property implementation and to avoid future warnings.
      }
      else {
        // FIXME! Rules for properties are somewhat different that those
        // for assignments. Use a new routine to consolidate all cases;
        // specifically for property redeclarations as well as for ivars.
        QualType lhsType =Context.getCanonicalType(PropertyIvarType).getUnqualifiedType();
        QualType rhsType =Context.getCanonicalType(IvarType).getUnqualifiedType();
        if (lhsType != rhsType &&
            lhsType->isArithmeticType()) {
          Diag(PropertyDiagLoc, diag::err_property_ivar_type)
            << property->getDeclName() << PropType
            << Ivar->getDeclName() << IvarType;
          Diag(Ivar->getLocation(), diag::note_ivar_decl);
          // Fall thru - see previous comment
        }
      }
      // __weak is explicit. So it works on Canonical type.
      if ((PropType.isObjCGCWeak() && !IvarType.isObjCGCWeak() &&
           getLangOpts().getGC() != LangOptions::NonGC)) {
        Diag(PropertyDiagLoc, diag::err_weak_property)
        << property->getDeclName() << Ivar->getDeclName();
        Diag(Ivar->getLocation(), diag::note_ivar_decl);
        // Fall thru - see previous comment
      }
      // Fall thru - see previous comment
      if ((property->getType()->isObjCObjectPointerType() ||
           PropType.isObjCGCStrong()) && IvarType.isObjCGCWeak() &&
          getLangOpts().getGC() != LangOptions::NonGC) {
        Diag(PropertyDiagLoc, diag::err_strong_property)
        << property->getDeclName() << Ivar->getDeclName();
        // Fall thru - see previous comment
      }
    }
    if (getLangOpts().ObjCAutoRefCount || isARCWeak ||
        Ivar->getType().getObjCLifetime())
      checkARCPropertyImpl(SemaRef, PropertyLoc, property, Ivar);
  } else if (PropertyIvar)
    // @dynamic
    Diag(PropertyDiagLoc, diag::err_dynamic_property_ivar_decl);

  assert (property && "ActOnPropertyImplDecl - property declaration missing");
  ObjCPropertyImplDecl *PIDecl = ObjCPropertyImplDecl::Create(
      Context, SemaRef.CurContext, AtLoc, PropertyLoc, property,
      (Synthesize ? ObjCPropertyImplDecl::Synthesize
                  : ObjCPropertyImplDecl::Dynamic),
      Ivar, PropertyIvarLoc);

  if (CompleteTypeErr || !compat)
    PIDecl->setInvalidDecl();

  if (ObjCMethodDecl *getterMethod = property->getGetterMethodDecl()) {
    getterMethod->createImplicitParams(Context, IDecl);

    // Redeclare the getter within the implementation as DeclContext.
    if (Synthesize) {
      // If the method hasn't been overridden, create a synthesized implementation.
      ObjCMethodDecl *OMD = ClassImpDecl->getMethod(
          getterMethod->getSelector(), getterMethod->isInstanceMethod());
      if (!OMD)
        OMD = RedeclarePropertyAccessor(Context, IC, getterMethod, AtLoc,
                                        PropertyLoc);
      PIDecl->setGetterMethodDecl(OMD);
    }

    if (getLangOpts().CPlusPlus && Synthesize && !CompleteTypeErr &&
        Ivar->getType()->isRecordType()) {
      // For Objective-C++, need to synthesize the AST for the IVAR object to be
      // returned by the getter as it must conform to C++'s copy-return rules.
      // FIXME. Eventually we want to do this for Objective-C as well.
      Sema::SynthesizedFunctionScope Scope(SemaRef, getterMethod);
      ImplicitParamDecl *SelfDecl = getterMethod->getSelfDecl();
      DeclRefExpr *SelfExpr = new (Context)
          DeclRefExpr(Context, SelfDecl, false, SelfDecl->getType(), VK_LValue,
                      PropertyDiagLoc);
      SemaRef.MarkDeclRefReferenced(SelfExpr);
      Expr *LoadSelfExpr = ImplicitCastExpr::Create(
          Context, SelfDecl->getType(), CK_LValueToRValue, SelfExpr, nullptr,
          VK_PRValue, FPOptionsOverride());
      Expr *IvarRefExpr =
        new (Context) ObjCIvarRefExpr(Ivar,
                                      Ivar->getUsageType(SelfDecl->getType()),
                                      PropertyDiagLoc,
                                      Ivar->getLocation(),
                                      LoadSelfExpr, true, true);
      ExprResult Res = SemaRef.PerformCopyInitialization(
          InitializedEntity::InitializeResult(PropertyDiagLoc,
                                              getterMethod->getReturnType()),
          PropertyDiagLoc, IvarRefExpr);
      if (!Res.isInvalid()) {
        Expr *ResExpr = Res.getAs<Expr>();
        if (ResExpr)
          ResExpr = SemaRef.MaybeCreateExprWithCleanups(ResExpr);
        PIDecl->setGetterCXXConstructor(ResExpr);
      }
    }
    if (property->hasAttr<NSReturnsNotRetainedAttr>() &&
        !getterMethod->hasAttr<NSReturnsNotRetainedAttr>()) {
      Diag(getterMethod->getLocation(),
           diag::warn_property_getter_owning_mismatch);
      Diag(property->getLocation(), diag::note_property_declare);
    }
    if (getLangOpts().ObjCAutoRefCount && Synthesize)
      switch (getterMethod->getMethodFamily()) {
        case OMF_retain:
        case OMF_retainCount:
        case OMF_release:
        case OMF_autorelease:
          Diag(getterMethod->getLocation(), diag::err_arc_illegal_method_def)
            << 1 << getterMethod->getSelector();
          break;
        default:
          break;
      }
  }

  if (ObjCMethodDecl *setterMethod = property->getSetterMethodDecl()) {
    setterMethod->createImplicitParams(Context, IDecl);

    // Redeclare the setter within the implementation as DeclContext.
    if (Synthesize) {
      ObjCMethodDecl *OMD = ClassImpDecl->getMethod(
          setterMethod->getSelector(), setterMethod->isInstanceMethod());
      if (!OMD)
        OMD = RedeclarePropertyAccessor(Context, IC, setterMethod,
                                        AtLoc, PropertyLoc);
      PIDecl->setSetterMethodDecl(OMD);
    }

    if (getLangOpts().CPlusPlus && Synthesize && !CompleteTypeErr &&
        Ivar->getType()->isRecordType()) {
      // FIXME. Eventually we want to do this for Objective-C as well.
      Sema::SynthesizedFunctionScope Scope(SemaRef, setterMethod);
      ImplicitParamDecl *SelfDecl = setterMethod->getSelfDecl();
      DeclRefExpr *SelfExpr = new (Context)
          DeclRefExpr(Context, SelfDecl, false, SelfDecl->getType(), VK_LValue,
                      PropertyDiagLoc);
      SemaRef.MarkDeclRefReferenced(SelfExpr);
      Expr *LoadSelfExpr = ImplicitCastExpr::Create(
          Context, SelfDecl->getType(), CK_LValueToRValue, SelfExpr, nullptr,
          VK_PRValue, FPOptionsOverride());
      Expr *lhs =
        new (Context) ObjCIvarRefExpr(Ivar,
                                      Ivar->getUsageType(SelfDecl->getType()),
                                      PropertyDiagLoc,
                                      Ivar->getLocation(),
                                      LoadSelfExpr, true, true);
      ObjCMethodDecl::param_iterator P = setterMethod->param_begin();
      ParmVarDecl *Param = (*P);
      QualType T = Param->getType().getNonReferenceType();
      DeclRefExpr *rhs = new (Context)
          DeclRefExpr(Context, Param, false, T, VK_LValue, PropertyDiagLoc);
      SemaRef.MarkDeclRefReferenced(rhs);
      ExprResult Res =
          SemaRef.BuildBinOp(S, PropertyDiagLoc, BO_Assign, lhs, rhs);
      if (property->getPropertyAttributes() &
          ObjCPropertyAttribute::kind_atomic) {
        Expr *callExpr = Res.getAs<Expr>();
        if (const CXXOperatorCallExpr *CXXCE =
              dyn_cast_or_null<CXXOperatorCallExpr>(callExpr))
          if (const FunctionDecl *FuncDecl = CXXCE->getDirectCallee())
            if (!FuncDecl->isTrivial())
              if (property->getType()->isReferenceType()) {
                Diag(PropertyDiagLoc,
                     diag::err_atomic_property_nontrivial_assign_op)
                    << property->getType();
                Diag(FuncDecl->getBeginLoc(), diag::note_callee_decl)
                    << FuncDecl;
              }
      }
      PIDecl->setSetterCXXAssignment(Res.getAs<Expr>());
    }
  }

  if (IC) {
    if (Synthesize)
      if (ObjCPropertyImplDecl *PPIDecl =
          IC->FindPropertyImplIvarDecl(PropertyIvar)) {
        Diag(PropertyLoc, diag::err_duplicate_ivar_use)
        << PropertyId << PPIDecl->getPropertyDecl()->getIdentifier()
        << PropertyIvar;
        Diag(PPIDecl->getLocation(), diag::note_previous_use);
      }

    if (ObjCPropertyImplDecl *PPIDecl
        = IC->FindPropertyImplDecl(PropertyId, QueryKind)) {
      Diag(PropertyLoc, diag::err_property_implemented) << PropertyId;
      Diag(PPIDecl->getLocation(), diag::note_previous_declaration);
      return nullptr;
    }
    IC->addPropertyImplementation(PIDecl);
    if (getLangOpts().ObjCDefaultSynthProperties &&
        getLangOpts().ObjCRuntime.isNonFragile() &&
        !IDecl->isObjCRequiresPropertyDefs()) {
      // Diagnose if an ivar was lazily synthesdized due to a previous
      // use and if 1) property is @dynamic or 2) property is synthesized
      // but it requires an ivar of different name.
      ObjCInterfaceDecl *ClassDeclared=nullptr;
      ObjCIvarDecl *Ivar = nullptr;
      if (!Synthesize)
        Ivar = IDecl->lookupInstanceVariable(PropertyId, ClassDeclared);
      else {
        if (PropertyIvar && PropertyIvar != PropertyId)
          Ivar = IDecl->lookupInstanceVariable(PropertyId, ClassDeclared);
      }
      // Issue diagnostics only if Ivar belongs to current class.
      if (Ivar && Ivar->getSynthesize() &&
          declaresSameEntity(IC->getClassInterface(), ClassDeclared)) {
        Diag(Ivar->getLocation(), diag::err_undeclared_var_use)
        << PropertyId;
        Ivar->setInvalidDecl();
      }
    }
  } else {
    if (Synthesize)
      if (ObjCPropertyImplDecl *PPIDecl =
          CatImplClass->FindPropertyImplIvarDecl(PropertyIvar)) {
        Diag(PropertyDiagLoc, diag::err_duplicate_ivar_use)
        << PropertyId << PPIDecl->getPropertyDecl()->getIdentifier()
        << PropertyIvar;
        Diag(PPIDecl->getLocation(), diag::note_previous_use);
      }

    if (ObjCPropertyImplDecl *PPIDecl =
        CatImplClass->FindPropertyImplDecl(PropertyId, QueryKind)) {
      Diag(PropertyDiagLoc, diag::err_property_implemented) << PropertyId;
      Diag(PPIDecl->getLocation(), diag::note_previous_declaration);
      return nullptr;
    }
    CatImplClass->addPropertyImplementation(PIDecl);
  }

  if (PIDecl->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic &&
      PIDecl->getPropertyDecl() &&
      PIDecl->getPropertyDecl()->isDirectProperty()) {
    Diag(PropertyLoc, diag::err_objc_direct_dynamic_property);
    Diag(PIDecl->getPropertyDecl()->getLocation(),
         diag::note_previous_declaration);
    return nullptr;
  }

  return PIDecl;
}

//===----------------------------------------------------------------------===//
// Helper methods.
//===----------------------------------------------------------------------===//

/// DiagnosePropertyMismatch - Compares two properties for their
/// attributes and types and warns on a variety of inconsistencies.
///
void SemaObjC::DiagnosePropertyMismatch(ObjCPropertyDecl *Property,
                                        ObjCPropertyDecl *SuperProperty,
                                        const IdentifierInfo *inheritedName,
                                        bool OverridingProtocolProperty) {
  ASTContext &Context = getASTContext();
  ObjCPropertyAttribute::Kind CAttr = Property->getPropertyAttributes();
  ObjCPropertyAttribute::Kind SAttr = SuperProperty->getPropertyAttributes();

  // We allow readonly properties without an explicit ownership
  // (assign/unsafe_unretained/weak/retain/strong/copy) in super class
  // to be overridden by a property with any explicit ownership in the subclass.
  if (!OverridingProtocolProperty &&
      !getOwnershipRule(SAttr) && getOwnershipRule(CAttr))
    ;
  else {
    if ((CAttr & ObjCPropertyAttribute::kind_readonly) &&
        (SAttr & ObjCPropertyAttribute::kind_readwrite))
      Diag(Property->getLocation(), diag::warn_readonly_property)
        << Property->getDeclName() << inheritedName;
    if ((CAttr & ObjCPropertyAttribute::kind_copy) !=
        (SAttr & ObjCPropertyAttribute::kind_copy))
      Diag(Property->getLocation(), diag::warn_property_attribute)
        << Property->getDeclName() << "copy" << inheritedName;
    else if (!(SAttr & ObjCPropertyAttribute::kind_readonly)) {
      unsigned CAttrRetain = (CAttr & (ObjCPropertyAttribute::kind_retain |
                                       ObjCPropertyAttribute::kind_strong));
      unsigned SAttrRetain = (SAttr & (ObjCPropertyAttribute::kind_retain |
                                       ObjCPropertyAttribute::kind_strong));
      bool CStrong = (CAttrRetain != 0);
      bool SStrong = (SAttrRetain != 0);
      if (CStrong != SStrong)
        Diag(Property->getLocation(), diag::warn_property_attribute)
          << Property->getDeclName() << "retain (or strong)" << inheritedName;
    }
  }

  // Check for nonatomic; note that nonatomic is effectively
  // meaningless for readonly properties, so don't diagnose if the
  // atomic property is 'readonly'.
  checkAtomicPropertyMismatch(SemaRef, SuperProperty, Property, false);
  // Readonly properties from protocols can be implemented as "readwrite"
  // with a custom setter name.
  if (Property->getSetterName() != SuperProperty->getSetterName() &&
      !(SuperProperty->isReadOnly() &&
        isa<ObjCProtocolDecl>(SuperProperty->getDeclContext()))) {
    Diag(Property->getLocation(), diag::warn_property_attribute)
      << Property->getDeclName() << "setter" << inheritedName;
    Diag(SuperProperty->getLocation(), diag::note_property_declare);
  }
  if (Property->getGetterName() != SuperProperty->getGetterName()) {
    Diag(Property->getLocation(), diag::warn_property_attribute)
      << Property->getDeclName() << "getter" << inheritedName;
    Diag(SuperProperty->getLocation(), diag::note_property_declare);
  }

  QualType LHSType =
    Context.getCanonicalType(SuperProperty->getType());
  QualType RHSType =
    Context.getCanonicalType(Property->getType());

  if (!Context.propertyTypesAreCompatible(LHSType, RHSType)) {
    // Do cases not handled in above.
    // FIXME. For future support of covariant property types, revisit this.
    bool IncompatibleObjC = false;
    QualType ConvertedType;
    if (!SemaRef.isObjCPointerConversion(RHSType, LHSType, ConvertedType,
                                         IncompatibleObjC) ||
        IncompatibleObjC) {
      Diag(Property->getLocation(), diag::warn_property_types_are_incompatible)
          << Property->getType() << SuperProperty->getType() << inheritedName;
      Diag(SuperProperty->getLocation(), diag::note_property_declare);
    }
  }
}

bool SemaObjC::DiagnosePropertyAccessorMismatch(ObjCPropertyDecl *property,
                                                ObjCMethodDecl *GetterMethod,
                                                SourceLocation Loc) {
  ASTContext &Context = getASTContext();
  if (!GetterMethod)
    return false;
  QualType GetterType = GetterMethod->getReturnType().getNonReferenceType();
  QualType PropertyRValueType =
      property->getType().getNonReferenceType().getAtomicUnqualifiedType();
  bool compat = Context.hasSameType(PropertyRValueType, GetterType);
  if (!compat) {
    const ObjCObjectPointerType *propertyObjCPtr = nullptr;
    const ObjCObjectPointerType *getterObjCPtr = nullptr;
    if ((propertyObjCPtr =
             PropertyRValueType->getAs<ObjCObjectPointerType>()) &&
        (getterObjCPtr = GetterType->getAs<ObjCObjectPointerType>()))
      compat = Context.canAssignObjCInterfaces(getterObjCPtr, propertyObjCPtr);
    else if (SemaRef.CheckAssignmentConstraints(
                 Loc, GetterType, PropertyRValueType) != Sema::Compatible) {
      Diag(Loc, diag::err_property_accessor_type)
          << property->getDeclName() << PropertyRValueType
          << GetterMethod->getSelector() << GetterType;
      Diag(GetterMethod->getLocation(), diag::note_declared_at);
      return true;
    } else {
      compat = true;
      QualType lhsType = Context.getCanonicalType(PropertyRValueType);
      QualType rhsType =Context.getCanonicalType(GetterType).getUnqualifiedType();
      if (lhsType != rhsType && lhsType->isArithmeticType())
        compat = false;
    }
  }

  if (!compat) {
    Diag(Loc, diag::warn_accessor_property_type_mismatch)
    << property->getDeclName()
    << GetterMethod->getSelector();
    Diag(GetterMethod->getLocation(), diag::note_declared_at);
    return true;
  }

  return false;
}

/// CollectImmediateProperties - This routine collects all properties in
/// the class and its conforming protocols; but not those in its super class.
static void
CollectImmediateProperties(ObjCContainerDecl *CDecl,
                           ObjCContainerDecl::PropertyMap &PropMap,
                           ObjCContainerDecl::PropertyMap &SuperPropMap,
                           bool CollectClassPropsOnly = false,
                           bool IncludeProtocols = true) {
  if (ObjCInterfaceDecl *IDecl = dyn_cast<ObjCInterfaceDecl>(CDecl)) {
    for (auto *Prop : IDecl->properties()) {
      if (CollectClassPropsOnly && !Prop->isClassProperty())
        continue;
      PropMap[std::make_pair(Prop->getIdentifier(), Prop->isClassProperty())] =
          Prop;
    }

    // Collect the properties from visible extensions.
    for (auto *Ext : IDecl->visible_extensions())
      CollectImmediateProperties(Ext, PropMap, SuperPropMap,
                                 CollectClassPropsOnly, IncludeProtocols);

    if (IncludeProtocols) {
      // Scan through class's protocols.
      for (auto *PI : IDecl->all_referenced_protocols())
        CollectImmediateProperties(PI, PropMap, SuperPropMap,
                                   CollectClassPropsOnly);
    }
  }
  if (ObjCCategoryDecl *CATDecl = dyn_cast<ObjCCategoryDecl>(CDecl)) {
    for (auto *Prop : CATDecl->properties()) {
      if (CollectClassPropsOnly && !Prop->isClassProperty())
        continue;
      PropMap[std::make_pair(Prop->getIdentifier(), Prop->isClassProperty())] =
          Prop;
    }
    if (IncludeProtocols) {
      // Scan through class's protocols.
      for (auto *PI : CATDecl->protocols())
        CollectImmediateProperties(PI, PropMap, SuperPropMap,
                                   CollectClassPropsOnly);
    }
  }
  else if (ObjCProtocolDecl *PDecl = dyn_cast<ObjCProtocolDecl>(CDecl)) {
    for (auto *Prop : PDecl->properties()) {
      if (CollectClassPropsOnly && !Prop->isClassProperty())
        continue;
      ObjCPropertyDecl *PropertyFromSuper =
          SuperPropMap[std::make_pair(Prop->getIdentifier(),
                                      Prop->isClassProperty())];
      // Exclude property for protocols which conform to class's super-class,
      // as super-class has to implement the property.
      if (!PropertyFromSuper ||
          PropertyFromSuper->getIdentifier() != Prop->getIdentifier()) {
        ObjCPropertyDecl *&PropEntry =
            PropMap[std::make_pair(Prop->getIdentifier(),
                                   Prop->isClassProperty())];
        if (!PropEntry)
          PropEntry = Prop;
      }
    }
    // Scan through protocol's protocols.
    for (auto *PI : PDecl->protocols())
      CollectImmediateProperties(PI, PropMap, SuperPropMap,
                                 CollectClassPropsOnly);
  }
}

/// CollectSuperClassPropertyImplementations - This routine collects list of
/// properties to be implemented in super class(s) and also coming from their
/// conforming protocols.
static void CollectSuperClassPropertyImplementations(ObjCInterfaceDecl *CDecl,
                                    ObjCInterfaceDecl::PropertyMap &PropMap) {
  if (ObjCInterfaceDecl *SDecl = CDecl->getSuperClass()) {
    while (SDecl) {
      SDecl->collectPropertiesToImplement(PropMap);
      SDecl = SDecl->getSuperClass();
    }
  }
}

/// IvarBacksCurrentMethodAccessor - This routine returns 'true' if 'IV' is
/// an ivar synthesized for 'Method' and 'Method' is a property accessor
/// declared in class 'IFace'.
bool SemaObjC::IvarBacksCurrentMethodAccessor(ObjCInterfaceDecl *IFace,
                                              ObjCMethodDecl *Method,
                                              ObjCIvarDecl *IV) {
  if (!IV->getSynthesize())
    return false;
  ObjCMethodDecl *IMD = IFace->lookupMethod(Method->getSelector(),
                                            Method->isInstanceMethod());
  if (!IMD || !IMD->isPropertyAccessor())
    return false;

  // look up a property declaration whose one of its accessors is implemented
  // by this method.
  for (const auto *Property : IFace->instance_properties()) {
    if ((Property->getGetterName() == IMD->getSelector() ||
         Property->getSetterName() == IMD->getSelector()) &&
        (Property->getPropertyIvarDecl() == IV))
      return true;
  }
  // Also look up property declaration in class extension whose one of its
  // accessors is implemented by this method.
  for (const auto *Ext : IFace->known_extensions())
    for (const auto *Property : Ext->instance_properties())
      if ((Property->getGetterName() == IMD->getSelector() ||
           Property->getSetterName() == IMD->getSelector()) &&
          (Property->getPropertyIvarDecl() == IV))
        return true;
  return false;
}

static bool SuperClassImplementsProperty(ObjCInterfaceDecl *IDecl,
                                         ObjCPropertyDecl *Prop) {
  bool SuperClassImplementsGetter = false;
  bool SuperClassImplementsSetter = false;
  if (Prop->getPropertyAttributes() & ObjCPropertyAttribute::kind_readonly)
    SuperClassImplementsSetter = true;

  while (IDecl->getSuperClass()) {
    ObjCInterfaceDecl *SDecl = IDecl->getSuperClass();
    if (!SuperClassImplementsGetter && SDecl->getInstanceMethod(Prop->getGetterName()))
      SuperClassImplementsGetter = true;

    if (!SuperClassImplementsSetter && SDecl->getInstanceMethod(Prop->getSetterName()))
      SuperClassImplementsSetter = true;
    if (SuperClassImplementsGetter && SuperClassImplementsSetter)
      return true;
    IDecl = IDecl->getSuperClass();
  }
  return false;
}

/// Default synthesizes all properties which must be synthesized
/// in class's \@implementation.
void SemaObjC::DefaultSynthesizeProperties(Scope *S, ObjCImplDecl *IMPDecl,
                                           ObjCInterfaceDecl *IDecl,
                                           SourceLocation AtEnd) {
  ASTContext &Context = getASTContext();
  ObjCInterfaceDecl::PropertyMap PropMap;
  IDecl->collectPropertiesToImplement(PropMap);
  if (PropMap.empty())
    return;
  ObjCInterfaceDecl::PropertyMap SuperPropMap;
  CollectSuperClassPropertyImplementations(IDecl, SuperPropMap);

  for (const auto &PropEntry : PropMap) {
    ObjCPropertyDecl *Prop = PropEntry.second;
    // Is there a matching property synthesize/dynamic?
    if (Prop->isInvalidDecl() ||
        Prop->isClassProperty() ||
        Prop->getPropertyImplementation() == ObjCPropertyDecl::Optional)
      continue;
    // Property may have been synthesized by user.
    if (IMPDecl->FindPropertyImplDecl(
            Prop->getIdentifier(), Prop->getQueryKind()))
      continue;
    ObjCMethodDecl *ImpMethod = IMPDecl->getInstanceMethod(Prop->getGetterName());
    if (ImpMethod && !ImpMethod->getBody()) {
      if (Prop->getPropertyAttributes() & ObjCPropertyAttribute::kind_readonly)
        continue;
      ImpMethod = IMPDecl->getInstanceMethod(Prop->getSetterName());
      if (ImpMethod && !ImpMethod->getBody())
        continue;
    }
    if (ObjCPropertyImplDecl *PID =
        IMPDecl->FindPropertyImplIvarDecl(Prop->getIdentifier())) {
      Diag(Prop->getLocation(), diag::warn_no_autosynthesis_shared_ivar_property)
        << Prop->getIdentifier();
      if (PID->getLocation().isValid())
        Diag(PID->getLocation(), diag::note_property_synthesize);
      continue;
    }
    ObjCPropertyDecl *PropInSuperClass =
        SuperPropMap[std::make_pair(Prop->getIdentifier(),
                                    Prop->isClassProperty())];
    if (ObjCProtocolDecl *Proto =
          dyn_cast<ObjCProtocolDecl>(Prop->getDeclContext())) {
      // We won't auto-synthesize properties declared in protocols.
      // Suppress the warning if class's superclass implements property's
      // getter and implements property's setter (if readwrite property).
      // Or, if property is going to be implemented in its super class.
      if (!SuperClassImplementsProperty(IDecl, Prop) && !PropInSuperClass) {
        Diag(IMPDecl->getLocation(),
             diag::warn_auto_synthesizing_protocol_property)
          << Prop << Proto;
        Diag(Prop->getLocation(), diag::note_property_declare);
        std::string FixIt =
            (Twine("@synthesize ") + Prop->getName() + ";\n\n").str();
        Diag(AtEnd, diag::note_add_synthesize_directive)
            << FixItHint::CreateInsertion(AtEnd, FixIt);
      }
      continue;
    }
    // If property to be implemented in the super class, ignore.
    if (PropInSuperClass) {
      if ((Prop->getPropertyAttributes() &
           ObjCPropertyAttribute::kind_readwrite) &&
          (PropInSuperClass->getPropertyAttributes() &
           ObjCPropertyAttribute::kind_readonly) &&
          !IMPDecl->getInstanceMethod(Prop->getSetterName()) &&
          !IDecl->HasUserDeclaredSetterMethod(Prop)) {
        Diag(Prop->getLocation(), diag::warn_no_autosynthesis_property)
        << Prop->getIdentifier();
        Diag(PropInSuperClass->getLocation(), diag::note_property_declare);
      } else {
        Diag(Prop->getLocation(), diag::warn_autosynthesis_property_in_superclass)
        << Prop->getIdentifier();
        Diag(PropInSuperClass->getLocation(), diag::note_property_declare);
        Diag(IMPDecl->getLocation(), diag::note_while_in_implementation);
      }
      continue;
    }
    // We use invalid SourceLocations for the synthesized ivars since they
    // aren't really synthesized at a particular location; they just exist.
    // Saying that they are located at the @implementation isn't really going
    // to help users.
    ObjCPropertyImplDecl *PIDecl = dyn_cast_or_null<ObjCPropertyImplDecl>(
      ActOnPropertyImplDecl(S, SourceLocation(), SourceLocation(),
                            true,
                            /* property = */ Prop->getIdentifier(),
                            /* ivar = */ Prop->getDefaultSynthIvarName(Context),
                            Prop->getLocation(), Prop->getQueryKind()));
    if (PIDecl && !Prop->isUnavailable()) {
      Diag(Prop->getLocation(), diag::warn_missing_explicit_synthesis);
      Diag(IMPDecl->getLocation(), diag::note_while_in_implementation);
    }
  }
}

void SemaObjC::DefaultSynthesizeProperties(Scope *S, Decl *D,
                                           SourceLocation AtEnd) {
  if (!getLangOpts().ObjCDefaultSynthProperties ||
      getLangOpts().ObjCRuntime.isFragile())
    return;
  ObjCImplementationDecl *IC=dyn_cast_or_null<ObjCImplementationDecl>(D);
  if (!IC)
    return;
  if (ObjCInterfaceDecl* IDecl = IC->getClassInterface())
    if (!IDecl->isObjCRequiresPropertyDefs())
      DefaultSynthesizeProperties(S, IC, IDecl, AtEnd);
}

static void DiagnoseUnimplementedAccessor(
    Sema &S, ObjCInterfaceDecl *PrimaryClass, Selector Method,
    ObjCImplDecl *IMPDecl, ObjCContainerDecl *CDecl, ObjCCategoryDecl *C,
    ObjCPropertyDecl *Prop,
    llvm::SmallPtrSet<const ObjCMethodDecl *, 8> &SMap) {
  // Check to see if we have a corresponding selector in SMap and with the
  // right method type.
  auto I = llvm::find_if(SMap, [&](const ObjCMethodDecl *x) {
    return x->getSelector() == Method &&
           x->isClassMethod() == Prop->isClassProperty();
  });
  // When reporting on missing property setter/getter implementation in
  // categories, do not report when they are declared in primary class,
  // class's protocol, or one of it super classes. This is because,
  // the class is going to implement them.
  if (I == SMap.end() &&
      (PrimaryClass == nullptr ||
       !PrimaryClass->lookupPropertyAccessor(Method, C,
                                             Prop->isClassProperty()))) {
    unsigned diag =
        isa<ObjCCategoryDecl>(CDecl)
            ? (Prop->isClassProperty()
                   ? diag::warn_impl_required_in_category_for_class_property
                   : diag::warn_setter_getter_impl_required_in_category)
            : (Prop->isClassProperty()
                   ? diag::warn_impl_required_for_class_property
                   : diag::warn_setter_getter_impl_required);
    S.Diag(IMPDecl->getLocation(), diag) << Prop->getDeclName() << Method;
    S.Diag(Prop->getLocation(), diag::note_property_declare);
    if (S.LangOpts.ObjCDefaultSynthProperties &&
        S.LangOpts.ObjCRuntime.isNonFragile())
      if (ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(CDecl))
        if (const ObjCInterfaceDecl *RID = ID->isObjCRequiresPropertyDefs())
          S.Diag(RID->getLocation(), diag::note_suppressed_class_declare);
  }
}

void SemaObjC::DiagnoseUnimplementedProperties(Scope *S, ObjCImplDecl *IMPDecl,
                                               ObjCContainerDecl *CDecl,
                                               bool SynthesizeProperties) {
  ObjCContainerDecl::PropertyMap PropMap;
  ObjCInterfaceDecl *IDecl = dyn_cast<ObjCInterfaceDecl>(CDecl);

  // Since we don't synthesize class properties, we should emit diagnose even
  // if SynthesizeProperties is true.
  ObjCContainerDecl::PropertyMap NoNeedToImplPropMap;
  // Gather properties which need not be implemented in this class
  // or category.
  if (!IDecl)
    if (ObjCCategoryDecl *C = dyn_cast<ObjCCategoryDecl>(CDecl)) {
      // For categories, no need to implement properties declared in
      // its primary class (and its super classes) if property is
      // declared in one of those containers.
      if ((IDecl = C->getClassInterface())) {
        IDecl->collectPropertiesToImplement(NoNeedToImplPropMap);
      }
    }
  if (IDecl)
    CollectSuperClassPropertyImplementations(IDecl, NoNeedToImplPropMap);

  // When SynthesizeProperties is true, we only check class properties.
  CollectImmediateProperties(CDecl, PropMap, NoNeedToImplPropMap,
                             SynthesizeProperties/*CollectClassPropsOnly*/);

  // Scan the @interface to see if any of the protocols it adopts
  // require an explicit implementation, via attribute
  // 'objc_protocol_requires_explicit_implementation'.
  if (IDecl) {
    std::unique_ptr<ObjCContainerDecl::PropertyMap> LazyMap;

    for (auto *PDecl : IDecl->all_referenced_protocols()) {
      if (!PDecl->hasAttr<ObjCExplicitProtocolImplAttr>())
        continue;
      // Lazily construct a set of all the properties in the @interface
      // of the class, without looking at the superclass.  We cannot
      // use the call to CollectImmediateProperties() above as that
      // utilizes information from the super class's properties as well
      // as scans the adopted protocols.  This work only triggers for protocols
      // with the attribute, which is very rare, and only occurs when
      // analyzing the @implementation.
      if (!LazyMap) {
        ObjCContainerDecl::PropertyMap NoNeedToImplPropMap;
        LazyMap.reset(new ObjCContainerDecl::PropertyMap());
        CollectImmediateProperties(CDecl, *LazyMap, NoNeedToImplPropMap,
                                   /* CollectClassPropsOnly */ false,
                                   /* IncludeProtocols */ false);
      }
      // Add the properties of 'PDecl' to the list of properties that
      // need to be implemented.
      for (auto *PropDecl : PDecl->properties()) {
        if ((*LazyMap)[std::make_pair(PropDecl->getIdentifier(),
                                      PropDecl->isClassProperty())])
          continue;
        PropMap[std::make_pair(PropDecl->getIdentifier(),
                               PropDecl->isClassProperty())] = PropDecl;
      }
    }
  }

  if (PropMap.empty())
    return;

  llvm::DenseSet<ObjCPropertyDecl *> PropImplMap;
  for (const auto *I : IMPDecl->property_impls())
    PropImplMap.insert(I->getPropertyDecl());

  llvm::SmallPtrSet<const ObjCMethodDecl *, 8> InsMap;
  // Collect property accessors implemented in current implementation.
  for (const auto *I : IMPDecl->methods())
    InsMap.insert(I);

  ObjCCategoryDecl *C = dyn_cast<ObjCCategoryDecl>(CDecl);
  ObjCInterfaceDecl *PrimaryClass = nullptr;
  if (C && !C->IsClassExtension())
    if ((PrimaryClass = C->getClassInterface()))
      // Report unimplemented properties in the category as well.
      if (ObjCImplDecl *IMP = PrimaryClass->getImplementation()) {
        // When reporting on missing setter/getters, do not report when
        // setter/getter is implemented in category's primary class
        // implementation.
        for (const auto *I : IMP->methods())
          InsMap.insert(I);
      }

  for (ObjCContainerDecl::PropertyMap::iterator
       P = PropMap.begin(), E = PropMap.end(); P != E; ++P) {
    ObjCPropertyDecl *Prop = P->second;
    // Is there a matching property synthesize/dynamic?
    if (Prop->isInvalidDecl() ||
        Prop->getPropertyImplementation() == ObjCPropertyDecl::Optional ||
        PropImplMap.count(Prop) ||
        Prop->getAvailability() == AR_Unavailable)
      continue;

    // Diagnose unimplemented getters and setters.
    DiagnoseUnimplementedAccessor(SemaRef, PrimaryClass, Prop->getGetterName(),
                                  IMPDecl, CDecl, C, Prop, InsMap);
    if (!Prop->isReadOnly())
      DiagnoseUnimplementedAccessor(SemaRef, PrimaryClass,
                                    Prop->getSetterName(), IMPDecl, CDecl, C,
                                    Prop, InsMap);
  }
}

void SemaObjC::diagnoseNullResettableSynthesizedSetters(
    const ObjCImplDecl *impDecl) {
  for (const auto *propertyImpl : impDecl->property_impls()) {
    const auto *property = propertyImpl->getPropertyDecl();
    // Warn about null_resettable properties with synthesized setters,
    // because the setter won't properly handle nil.
    if (propertyImpl->getPropertyImplementation() ==
            ObjCPropertyImplDecl::Synthesize &&
        (property->getPropertyAttributes() &
         ObjCPropertyAttribute::kind_null_resettable) &&
        property->getGetterMethodDecl() && property->getSetterMethodDecl()) {
      auto *getterImpl = propertyImpl->getGetterMethodDecl();
      auto *setterImpl = propertyImpl->getSetterMethodDecl();
      if ((!getterImpl || getterImpl->isSynthesizedAccessorStub()) &&
          (!setterImpl || setterImpl->isSynthesizedAccessorStub())) {
        SourceLocation loc = propertyImpl->getLocation();
        if (loc.isInvalid())
          loc = impDecl->getBeginLoc();

        Diag(loc, diag::warn_null_resettable_setter)
          << setterImpl->getSelector() << property->getDeclName();
      }
    }
  }
}

void SemaObjC::AtomicPropertySetterGetterRules(ObjCImplDecl *IMPDecl,
                                               ObjCInterfaceDecl *IDecl) {
  // Rules apply in non-GC mode only
  if (getLangOpts().getGC() != LangOptions::NonGC)
    return;
  ObjCContainerDecl::PropertyMap PM;
  for (auto *Prop : IDecl->properties())
    PM[std::make_pair(Prop->getIdentifier(), Prop->isClassProperty())] = Prop;
  for (const auto *Ext : IDecl->known_extensions())
    for (auto *Prop : Ext->properties())
      PM[std::make_pair(Prop->getIdentifier(), Prop->isClassProperty())] = Prop;

  for (ObjCContainerDecl::PropertyMap::iterator I = PM.begin(), E = PM.end();
       I != E; ++I) {
    const ObjCPropertyDecl *Property = I->second;
    ObjCMethodDecl *GetterMethod = nullptr;
    ObjCMethodDecl *SetterMethod = nullptr;

    unsigned Attributes = Property->getPropertyAttributes();
    unsigned AttributesAsWritten = Property->getPropertyAttributesAsWritten();

    if (!(AttributesAsWritten & ObjCPropertyAttribute::kind_atomic) &&
        !(AttributesAsWritten & ObjCPropertyAttribute::kind_nonatomic)) {
      GetterMethod = Property->isClassProperty() ?
                     IMPDecl->getClassMethod(Property->getGetterName()) :
                     IMPDecl->getInstanceMethod(Property->getGetterName());
      SetterMethod = Property->isClassProperty() ?
                     IMPDecl->getClassMethod(Property->getSetterName()) :
                     IMPDecl->getInstanceMethod(Property->getSetterName());
      if (GetterMethod && GetterMethod->isSynthesizedAccessorStub())
        GetterMethod = nullptr;
      if (SetterMethod && SetterMethod->isSynthesizedAccessorStub())
        SetterMethod = nullptr;
      if (GetterMethod) {
        Diag(GetterMethod->getLocation(),
             diag::warn_default_atomic_custom_getter_setter)
          << Property->getIdentifier() << 0;
        Diag(Property->getLocation(), diag::note_property_declare);
      }
      if (SetterMethod) {
        Diag(SetterMethod->getLocation(),
             diag::warn_default_atomic_custom_getter_setter)
          << Property->getIdentifier() << 1;
        Diag(Property->getLocation(), diag::note_property_declare);
      }
    }

    // We only care about readwrite atomic property.
    if ((Attributes & ObjCPropertyAttribute::kind_nonatomic) ||
        !(Attributes & ObjCPropertyAttribute::kind_readwrite))
      continue;
    if (const ObjCPropertyImplDecl *PIDecl = IMPDecl->FindPropertyImplDecl(
            Property->getIdentifier(), Property->getQueryKind())) {
      if (PIDecl->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic)
        continue;
      GetterMethod = PIDecl->getGetterMethodDecl();
      SetterMethod = PIDecl->getSetterMethodDecl();
      if (GetterMethod && GetterMethod->isSynthesizedAccessorStub())
        GetterMethod = nullptr;
      if (SetterMethod && SetterMethod->isSynthesizedAccessorStub())
        SetterMethod = nullptr;
      if ((bool)GetterMethod ^ (bool)SetterMethod) {
        SourceLocation MethodLoc =
          (GetterMethod ? GetterMethod->getLocation()
                        : SetterMethod->getLocation());
        Diag(MethodLoc, diag::warn_atomic_property_rule)
          << Property->getIdentifier() << (GetterMethod != nullptr)
          << (SetterMethod != nullptr);
        // fixit stuff.
        if (Property->getLParenLoc().isValid() &&
            !(AttributesAsWritten & ObjCPropertyAttribute::kind_atomic)) {
          // @property () ... case.
          SourceLocation AfterLParen =
              SemaRef.getLocForEndOfToken(Property->getLParenLoc());
          StringRef NonatomicStr = AttributesAsWritten? "nonatomic, "
                                                      : "nonatomic";
          Diag(Property->getLocation(),
               diag::note_atomic_property_fixup_suggest)
            << FixItHint::CreateInsertion(AfterLParen, NonatomicStr);
        } else if (Property->getLParenLoc().isInvalid()) {
          //@property id etc.
          SourceLocation startLoc =
            Property->getTypeSourceInfo()->getTypeLoc().getBeginLoc();
          Diag(Property->getLocation(),
               diag::note_atomic_property_fixup_suggest)
            << FixItHint::CreateInsertion(startLoc, "(nonatomic) ");
        } else
          Diag(MethodLoc, diag::note_atomic_property_fixup_suggest);
        Diag(Property->getLocation(), diag::note_property_declare);
      }
    }
  }
}

void SemaObjC::DiagnoseOwningPropertyGetterSynthesis(
    const ObjCImplementationDecl *D) {
  if (getLangOpts().getGC() == LangOptions::GCOnly)
    return;

  for (const auto *PID : D->property_impls()) {
    const ObjCPropertyDecl *PD = PID->getPropertyDecl();
    if (PD && !PD->hasAttr<NSReturnsNotRetainedAttr>() &&
        !PD->isClassProperty()) {
      ObjCMethodDecl *IM = PID->getGetterMethodDecl();
      if (IM && !IM->isSynthesizedAccessorStub())
        continue;
      ObjCMethodDecl *method = PD->getGetterMethodDecl();
      if (!method)
        continue;
      ObjCMethodFamily family = method->getMethodFamily();
      if (family == OMF_alloc || family == OMF_copy ||
          family == OMF_mutableCopy || family == OMF_new) {
        if (getLangOpts().ObjCAutoRefCount)
          Diag(PD->getLocation(), diag::err_cocoa_naming_owned_rule);
        else
          Diag(PD->getLocation(), diag::warn_cocoa_naming_owned_rule);

        // Look for a getter explicitly declared alongside the property.
        // If we find one, use its location for the note.
        SourceLocation noteLoc = PD->getLocation();
        SourceLocation fixItLoc;
        for (auto *getterRedecl : method->redecls()) {
          if (getterRedecl->isImplicit())
            continue;
          if (getterRedecl->getDeclContext() != PD->getDeclContext())
            continue;
          noteLoc = getterRedecl->getLocation();
          fixItLoc = getterRedecl->getEndLoc();
        }

        Preprocessor &PP = SemaRef.getPreprocessor();
        TokenValue tokens[] = {
          tok::kw___attribute, tok::l_paren, tok::l_paren,
          PP.getIdentifierInfo("objc_method_family"), tok::l_paren,
          PP.getIdentifierInfo("none"), tok::r_paren,
          tok::r_paren, tok::r_paren
        };
        StringRef spelling = "__attribute__((objc_method_family(none)))";
        StringRef macroName = PP.getLastMacroWithSpelling(noteLoc, tokens);
        if (!macroName.empty())
          spelling = macroName;

        auto noteDiag = Diag(noteLoc, diag::note_cocoa_naming_declare_family)
            << method->getDeclName() << spelling;
        if (fixItLoc.isValid()) {
          SmallString<64> fixItText(" ");
          fixItText += spelling;
          noteDiag << FixItHint::CreateInsertion(fixItLoc, fixItText);
        }
      }
    }
  }
}

void SemaObjC::DiagnoseMissingDesignatedInitOverrides(
    const ObjCImplementationDecl *ImplD, const ObjCInterfaceDecl *IFD) {
  assert(IFD->hasDesignatedInitializers());
  const ObjCInterfaceDecl *SuperD = IFD->getSuperClass();
  if (!SuperD)
    return;

  SelectorSet InitSelSet;
  for (const auto *I : ImplD->instance_methods())
    if (I->getMethodFamily() == OMF_init)
      InitSelSet.insert(I->getSelector());

  SmallVector<const ObjCMethodDecl *, 8> DesignatedInits;
  SuperD->getDesignatedInitializers(DesignatedInits);
  for (SmallVector<const ObjCMethodDecl *, 8>::iterator
         I = DesignatedInits.begin(), E = DesignatedInits.end(); I != E; ++I) {
    const ObjCMethodDecl *MD = *I;
    if (!InitSelSet.count(MD->getSelector())) {
      // Don't emit a diagnostic if the overriding method in the subclass is
      // marked as unavailable.
      bool Ignore = false;
      if (auto *IMD = IFD->getInstanceMethod(MD->getSelector())) {
        Ignore = IMD->isUnavailable();
      } else {
        // Check the methods declared in the class extensions too.
        for (auto *Ext : IFD->visible_extensions())
          if (auto *IMD = Ext->getInstanceMethod(MD->getSelector())) {
            Ignore = IMD->isUnavailable();
            break;
          }
      }
      if (!Ignore) {
        Diag(ImplD->getLocation(),
             diag::warn_objc_implementation_missing_designated_init_override)
          << MD->getSelector();
        Diag(MD->getLocation(), diag::note_objc_designated_init_marked_here);
      }
    }
  }
}

/// AddPropertyAttrs - Propagates attributes from a property to the
/// implicitly-declared getter or setter for that property.
static void AddPropertyAttrs(Sema &S, ObjCMethodDecl *PropertyMethod,
                             ObjCPropertyDecl *Property) {
  // Should we just clone all attributes over?
  for (const auto *A : Property->attrs()) {
    if (isa<DeprecatedAttr>(A) ||
        isa<UnavailableAttr>(A) ||
        isa<AvailabilityAttr>(A))
      PropertyMethod->addAttr(A->clone(S.Context));
  }
}

/// ProcessPropertyDecl - Make sure that any user-defined setter/getter methods
/// have the property type and issue diagnostics if they don't.
/// Also synthesize a getter/setter method if none exist (and update the
/// appropriate lookup tables.
void SemaObjC::ProcessPropertyDecl(ObjCPropertyDecl *property) {
  ASTContext &Context = getASTContext();
  ObjCMethodDecl *GetterMethod, *SetterMethod;
  ObjCContainerDecl *CD = cast<ObjCContainerDecl>(property->getDeclContext());
  if (CD->isInvalidDecl())
    return;

  bool IsClassProperty = property->isClassProperty();
  GetterMethod = IsClassProperty ?
    CD->getClassMethod(property->getGetterName()) :
    CD->getInstanceMethod(property->getGetterName());

  // if setter or getter is not found in class extension, it might be
  // in the primary class.
  if (!GetterMethod)
    if (const ObjCCategoryDecl *CatDecl = dyn_cast<ObjCCategoryDecl>(CD))
      if (CatDecl->IsClassExtension())
        GetterMethod = IsClassProperty ? CatDecl->getClassInterface()->
                         getClassMethod(property->getGetterName()) :
                       CatDecl->getClassInterface()->
                         getInstanceMethod(property->getGetterName());

  SetterMethod = IsClassProperty ?
                 CD->getClassMethod(property->getSetterName()) :
                 CD->getInstanceMethod(property->getSetterName());
  if (!SetterMethod)
    if (const ObjCCategoryDecl *CatDecl = dyn_cast<ObjCCategoryDecl>(CD))
      if (CatDecl->IsClassExtension())
        SetterMethod = IsClassProperty ? CatDecl->getClassInterface()->
                          getClassMethod(property->getSetterName()) :
                       CatDecl->getClassInterface()->
                          getInstanceMethod(property->getSetterName());
  DiagnosePropertyAccessorMismatch(property, GetterMethod,
                                   property->getLocation());

  // synthesizing accessors must not result in a direct method that is not
  // monomorphic
  if (!GetterMethod) {
    if (const ObjCCategoryDecl *CatDecl = dyn_cast<ObjCCategoryDecl>(CD)) {
      auto *ExistingGetter = CatDecl->getClassInterface()->lookupMethod(
          property->getGetterName(), !IsClassProperty, true, false, CatDecl);
      if (ExistingGetter) {
        if (ExistingGetter->isDirectMethod() || property->isDirectProperty()) {
          Diag(property->getLocation(), diag::err_objc_direct_duplicate_decl)
              << property->isDirectProperty() << 1 /* property */
              << ExistingGetter->isDirectMethod()
              << ExistingGetter->getDeclName();
          Diag(ExistingGetter->getLocation(), diag::note_previous_declaration);
        }
      }
    }
  }

  if (!property->isReadOnly() && !SetterMethod) {
    if (const ObjCCategoryDecl *CatDecl = dyn_cast<ObjCCategoryDecl>(CD)) {
      auto *ExistingSetter = CatDecl->getClassInterface()->lookupMethod(
          property->getSetterName(), !IsClassProperty, true, false, CatDecl);
      if (ExistingSetter) {
        if (ExistingSetter->isDirectMethod() || property->isDirectProperty()) {
          Diag(property->getLocation(), diag::err_objc_direct_duplicate_decl)
              << property->isDirectProperty() << 1 /* property */
              << ExistingSetter->isDirectMethod()
              << ExistingSetter->getDeclName();
          Diag(ExistingSetter->getLocation(), diag::note_previous_declaration);
        }
      }
    }
  }

  if (!property->isReadOnly() && SetterMethod) {
    if (Context.getCanonicalType(SetterMethod->getReturnType()) !=
        Context.VoidTy)
      Diag(SetterMethod->getLocation(), diag::err_setter_type_void);
    if (SetterMethod->param_size() != 1 ||
        !Context.hasSameUnqualifiedType(
          (*SetterMethod->param_begin())->getType().getNonReferenceType(),
          property->getType().getNonReferenceType())) {
      Diag(property->getLocation(),
           diag::warn_accessor_property_type_mismatch)
        << property->getDeclName()
        << SetterMethod->getSelector();
      Diag(SetterMethod->getLocation(), diag::note_declared_at);
    }
  }

  // Synthesize getter/setter methods if none exist.
  // Find the default getter and if one not found, add one.
  // FIXME: The synthesized property we set here is misleading. We almost always
  // synthesize these methods unless the user explicitly provided prototypes
  // (which is odd, but allowed). Sema should be typechecking that the
  // declarations jive in that situation (which it is not currently).
  if (!GetterMethod) {
    // No instance/class method of same name as property getter name was found.
    // Declare a getter method and add it to the list of methods
    // for this class.
    SourceLocation Loc = property->getLocation();

    // The getter returns the declared property type with all qualifiers
    // removed.
    QualType resultTy = property->getType().getAtomicUnqualifiedType();

    // If the property is null_resettable, the getter returns nonnull.
    if (property->getPropertyAttributes() &
        ObjCPropertyAttribute::kind_null_resettable) {
      QualType modifiedTy = resultTy;
      if (auto nullability = AttributedType::stripOuterNullability(modifiedTy)) {
        if (*nullability == NullabilityKind::Unspecified)
          resultTy = Context.getAttributedType(attr::TypeNonNull,
                                               modifiedTy, modifiedTy);
      }
    }

    GetterMethod = ObjCMethodDecl::Create(
        Context, Loc, Loc, property->getGetterName(), resultTy, nullptr, CD,
        !IsClassProperty, /*isVariadic=*/false,
        /*isPropertyAccessor=*/true, /*isSynthesizedAccessorStub=*/false,
        /*isImplicitlyDeclared=*/true, /*isDefined=*/false,
        (property->getPropertyImplementation() == ObjCPropertyDecl::Optional)
            ? ObjCImplementationControl::Optional
            : ObjCImplementationControl::Required);
    CD->addDecl(GetterMethod);

    AddPropertyAttrs(SemaRef, GetterMethod, property);

    if (property->isDirectProperty())
      GetterMethod->addAttr(ObjCDirectAttr::CreateImplicit(Context, Loc));

    if (property->hasAttr<NSReturnsNotRetainedAttr>())
      GetterMethod->addAttr(NSReturnsNotRetainedAttr::CreateImplicit(Context,
                                                                     Loc));

    if (property->hasAttr<ObjCReturnsInnerPointerAttr>())
      GetterMethod->addAttr(
        ObjCReturnsInnerPointerAttr::CreateImplicit(Context, Loc));

    if (const SectionAttr *SA = property->getAttr<SectionAttr>())
      GetterMethod->addAttr(SectionAttr::CreateImplicit(
          Context, SA->getName(), Loc, SectionAttr::GNU_section));

    SemaRef.ProcessAPINotes(GetterMethod);

    if (getLangOpts().ObjCAutoRefCount)
      CheckARCMethodDecl(GetterMethod);
  } else
    // A user declared getter will be synthesize when @synthesize of
    // the property with the same name is seen in the @implementation
    GetterMethod->setPropertyAccessor(true);

  GetterMethod->createImplicitParams(Context,
                                     GetterMethod->getClassInterface());
  property->setGetterMethodDecl(GetterMethod);

  // Skip setter if property is read-only.
  if (!property->isReadOnly()) {
    // Find the default setter and if one not found, add one.
    if (!SetterMethod) {
      // No instance/class method of same name as property setter name was
      // found.
      // Declare a setter method and add it to the list of methods
      // for this class.
      SourceLocation Loc = property->getLocation();

      SetterMethod = ObjCMethodDecl::Create(
          Context, Loc, Loc, property->getSetterName(), Context.VoidTy, nullptr,
          CD, !IsClassProperty,
          /*isVariadic=*/false,
          /*isPropertyAccessor=*/true,
          /*isSynthesizedAccessorStub=*/false,
          /*isImplicitlyDeclared=*/true,
          /*isDefined=*/false,
          (property->getPropertyImplementation() == ObjCPropertyDecl::Optional)
              ? ObjCImplementationControl::Optional
              : ObjCImplementationControl::Required);

      // Remove all qualifiers from the setter's parameter type.
      QualType paramTy =
          property->getType().getUnqualifiedType().getAtomicUnqualifiedType();

      // If the property is null_resettable, the setter accepts a
      // nullable value.
      if (property->getPropertyAttributes() &
          ObjCPropertyAttribute::kind_null_resettable) {
        QualType modifiedTy = paramTy;
        if (auto nullability = AttributedType::stripOuterNullability(modifiedTy)){
          if (*nullability == NullabilityKind::Unspecified)
            paramTy = Context.getAttributedType(attr::TypeNullable,
                                                modifiedTy, modifiedTy);
        }
      }

      // Invent the arguments for the setter. We don't bother making a
      // nice name for the argument.
      ParmVarDecl *Argument = ParmVarDecl::Create(Context, SetterMethod,
                                                  Loc, Loc,
                                                  property->getIdentifier(),
                                                  paramTy,
                                                  /*TInfo=*/nullptr,
                                                  SC_None,
                                                  nullptr);
      SetterMethod->setMethodParams(Context, Argument, std::nullopt);

      AddPropertyAttrs(SemaRef, SetterMethod, property);

      if (property->isDirectProperty())
        SetterMethod->addAttr(ObjCDirectAttr::CreateImplicit(Context, Loc));

      CD->addDecl(SetterMethod);
      if (const SectionAttr *SA = property->getAttr<SectionAttr>())
        SetterMethod->addAttr(SectionAttr::CreateImplicit(
            Context, SA->getName(), Loc, SectionAttr::GNU_section));

      SemaRef.ProcessAPINotes(SetterMethod);

      // It's possible for the user to have set a very odd custom
      // setter selector that causes it to have a method family.
      if (getLangOpts().ObjCAutoRefCount)
        CheckARCMethodDecl(SetterMethod);
    } else
      // A user declared setter will be synthesize when @synthesize of
      // the property with the same name is seen in the @implementation
      SetterMethod->setPropertyAccessor(true);

    SetterMethod->createImplicitParams(Context,
                                       SetterMethod->getClassInterface());
    property->setSetterMethodDecl(SetterMethod);
  }
  // Add any synthesized methods to the global pool. This allows us to
  // handle the following, which is supported by GCC (and part of the design).
  //
  // @interface Foo
  // @property double bar;
  // @end
  //
  // void thisIsUnfortunate() {
  //   id foo;
  //   double bar = [foo bar];
  // }
  //
  if (!IsClassProperty) {
    if (GetterMethod)
      AddInstanceMethodToGlobalPool(GetterMethod);
    if (SetterMethod)
      AddInstanceMethodToGlobalPool(SetterMethod);
  } else {
    if (GetterMethod)
      AddFactoryMethodToGlobalPool(GetterMethod);
    if (SetterMethod)
      AddFactoryMethodToGlobalPool(SetterMethod);
  }

  ObjCInterfaceDecl *CurrentClass = dyn_cast<ObjCInterfaceDecl>(CD);
  if (!CurrentClass) {
    if (ObjCCategoryDecl *Cat = dyn_cast<ObjCCategoryDecl>(CD))
      CurrentClass = Cat->getClassInterface();
    else if (ObjCImplDecl *Impl = dyn_cast<ObjCImplDecl>(CD))
      CurrentClass = Impl->getClassInterface();
  }
  if (GetterMethod)
    CheckObjCMethodOverrides(GetterMethod, CurrentClass, SemaObjC::RTC_Unknown);
  if (SetterMethod)
    CheckObjCMethodOverrides(SetterMethod, CurrentClass, SemaObjC::RTC_Unknown);
}

void SemaObjC::CheckObjCPropertyAttributes(Decl *PDecl, SourceLocation Loc,
                                           unsigned &Attributes,
                                           bool propertyInPrimaryClass) {
  // FIXME: Improve the reported location.
  if (!PDecl || PDecl->isInvalidDecl())
    return;

  if ((Attributes & ObjCPropertyAttribute::kind_readonly) &&
      (Attributes & ObjCPropertyAttribute::kind_readwrite))
    Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
    << "readonly" << "readwrite";

  ObjCPropertyDecl *PropertyDecl = cast<ObjCPropertyDecl>(PDecl);
  QualType PropertyTy = PropertyDecl->getType();

  // Check for copy or retain on non-object types.
  if ((Attributes &
       (ObjCPropertyAttribute::kind_weak | ObjCPropertyAttribute::kind_copy |
        ObjCPropertyAttribute::kind_retain |
        ObjCPropertyAttribute::kind_strong)) &&
      !PropertyTy->isObjCRetainableType() &&
      !PropertyDecl->hasAttr<ObjCNSObjectAttr>()) {
    Diag(Loc, diag::err_objc_property_requires_object)
        << (Attributes & ObjCPropertyAttribute::kind_weak
                ? "weak"
                : Attributes & ObjCPropertyAttribute::kind_copy
                      ? "copy"
                      : "retain (or strong)");
    Attributes &=
        ~(ObjCPropertyAttribute::kind_weak | ObjCPropertyAttribute::kind_copy |
          ObjCPropertyAttribute::kind_retain |
          ObjCPropertyAttribute::kind_strong);
    PropertyDecl->setInvalidDecl();
  }

  // Check for assign on object types.
  if ((Attributes & ObjCPropertyAttribute::kind_assign) &&
      !(Attributes & ObjCPropertyAttribute::kind_unsafe_unretained) &&
      PropertyTy->isObjCRetainableType() &&
      !PropertyTy->isObjCARCImplicitlyUnretainedType()) {
    Diag(Loc, diag::warn_objc_property_assign_on_object);
  }

  // Check for more than one of { assign, copy, retain }.
  if (Attributes & ObjCPropertyAttribute::kind_assign) {
    if (Attributes & ObjCPropertyAttribute::kind_copy) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "assign" << "copy";
      Attributes &= ~ObjCPropertyAttribute::kind_copy;
    }
    if (Attributes & ObjCPropertyAttribute::kind_retain) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "assign" << "retain";
      Attributes &= ~ObjCPropertyAttribute::kind_retain;
    }
    if (Attributes & ObjCPropertyAttribute::kind_strong) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "assign" << "strong";
      Attributes &= ~ObjCPropertyAttribute::kind_strong;
    }
    if (getLangOpts().ObjCAutoRefCount &&
        (Attributes & ObjCPropertyAttribute::kind_weak)) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "assign" << "weak";
      Attributes &= ~ObjCPropertyAttribute::kind_weak;
    }
    if (PropertyDecl->hasAttr<IBOutletCollectionAttr>())
      Diag(Loc, diag::warn_iboutletcollection_property_assign);
  } else if (Attributes & ObjCPropertyAttribute::kind_unsafe_unretained) {
    if (Attributes & ObjCPropertyAttribute::kind_copy) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "unsafe_unretained" << "copy";
      Attributes &= ~ObjCPropertyAttribute::kind_copy;
    }
    if (Attributes & ObjCPropertyAttribute::kind_retain) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "unsafe_unretained" << "retain";
      Attributes &= ~ObjCPropertyAttribute::kind_retain;
    }
    if (Attributes & ObjCPropertyAttribute::kind_strong) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "unsafe_unretained" << "strong";
      Attributes &= ~ObjCPropertyAttribute::kind_strong;
    }
    if (getLangOpts().ObjCAutoRefCount &&
        (Attributes & ObjCPropertyAttribute::kind_weak)) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "unsafe_unretained" << "weak";
      Attributes &= ~ObjCPropertyAttribute::kind_weak;
    }
  } else if (Attributes & ObjCPropertyAttribute::kind_copy) {
    if (Attributes & ObjCPropertyAttribute::kind_retain) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "copy" << "retain";
      Attributes &= ~ObjCPropertyAttribute::kind_retain;
    }
    if (Attributes & ObjCPropertyAttribute::kind_strong) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "copy" << "strong";
      Attributes &= ~ObjCPropertyAttribute::kind_strong;
    }
    if (Attributes & ObjCPropertyAttribute::kind_weak) {
      Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
        << "copy" << "weak";
      Attributes &= ~ObjCPropertyAttribute::kind_weak;
    }
  } else if ((Attributes & ObjCPropertyAttribute::kind_retain) &&
             (Attributes & ObjCPropertyAttribute::kind_weak)) {
    Diag(Loc, diag::err_objc_property_attr_mutually_exclusive) << "retain"
                                                               << "weak";
    Attributes &= ~ObjCPropertyAttribute::kind_retain;
  } else if ((Attributes & ObjCPropertyAttribute::kind_strong) &&
             (Attributes & ObjCPropertyAttribute::kind_weak)) {
    Diag(Loc, diag::err_objc_property_attr_mutually_exclusive) << "strong"
                                                               << "weak";
    Attributes &= ~ObjCPropertyAttribute::kind_weak;
  }

  if (Attributes & ObjCPropertyAttribute::kind_weak) {
    // 'weak' and 'nonnull' are mutually exclusive.
    if (auto nullability = PropertyTy->getNullability()) {
      if (*nullability == NullabilityKind::NonNull)
        Diag(Loc, diag::err_objc_property_attr_mutually_exclusive)
          << "nonnull" << "weak";
    }
  }

  if ((Attributes & ObjCPropertyAttribute::kind_atomic) &&
      (Attributes & ObjCPropertyAttribute::kind_nonatomic)) {
    Diag(Loc, diag::err_objc_property_attr_mutually_exclusive) << "atomic"
                                                               << "nonatomic";
    Attributes &= ~ObjCPropertyAttribute::kind_atomic;
  }

  // Warn if user supplied no assignment attribute, property is
  // readwrite, and this is an object type.
  if (!getOwnershipRule(Attributes) && PropertyTy->isObjCRetainableType()) {
    if (Attributes & ObjCPropertyAttribute::kind_readonly) {
      // do nothing
    } else if (getLangOpts().ObjCAutoRefCount) {
      // With arc, @property definitions should default to strong when
      // not specified.
      PropertyDecl->setPropertyAttributes(ObjCPropertyAttribute::kind_strong);
    } else if (PropertyTy->isObjCObjectPointerType()) {
      bool isAnyClassTy = (PropertyTy->isObjCClassType() ||
                           PropertyTy->isObjCQualifiedClassType());
      // In non-gc, non-arc mode, 'Class' is treated as a 'void *' no need to
      // issue any warning.
      if (isAnyClassTy && getLangOpts().getGC() == LangOptions::NonGC)
        ;
      else if (propertyInPrimaryClass) {
        // Don't issue warning on property with no life time in class
        // extension as it is inherited from property in primary class.
        // Skip this warning in gc-only mode.
        if (getLangOpts().getGC() != LangOptions::GCOnly)
          Diag(Loc, diag::warn_objc_property_no_assignment_attribute);

        // If non-gc code warn that this is likely inappropriate.
        if (getLangOpts().getGC() == LangOptions::NonGC)
          Diag(Loc, diag::warn_objc_property_default_assign_on_object);
      }
    }

    // FIXME: Implement warning dependent on NSCopying being
    // implemented.
  }

  if (!(Attributes & ObjCPropertyAttribute::kind_copy) &&
      !(Attributes & ObjCPropertyAttribute::kind_readonly) &&
      getLangOpts().getGC() == LangOptions::GCOnly &&
      PropertyTy->isBlockPointerType())
    Diag(Loc, diag::warn_objc_property_copy_missing_on_block);
  else if ((Attributes & ObjCPropertyAttribute::kind_retain) &&
           !(Attributes & ObjCPropertyAttribute::kind_readonly) &&
           !(Attributes & ObjCPropertyAttribute::kind_strong) &&
           PropertyTy->isBlockPointerType())
    Diag(Loc, diag::warn_objc_property_retain_of_block);

  if ((Attributes & ObjCPropertyAttribute::kind_readonly) &&
      (Attributes & ObjCPropertyAttribute::kind_setter))
    Diag(Loc, diag::warn_objc_readonly_property_has_setter);
}
