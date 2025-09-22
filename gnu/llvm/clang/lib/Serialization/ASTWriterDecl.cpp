//===--- ASTWriterDecl.cpp - Declaration Serialization --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements serialization for Declarations.
//
//===----------------------------------------------------------------------===//

#include "ASTCommon.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTRecordWriter.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>
using namespace clang;
using namespace serialization;

//===----------------------------------------------------------------------===//
// Declaration serialization
//===----------------------------------------------------------------------===//

namespace clang {
  class ASTDeclWriter : public DeclVisitor<ASTDeclWriter, void> {
    ASTWriter &Writer;
    ASTContext &Context;
    ASTRecordWriter Record;

    serialization::DeclCode Code;
    unsigned AbbrevToUse;

    bool GeneratingReducedBMI = false;

  public:
    ASTDeclWriter(ASTWriter &Writer, ASTContext &Context,
                  ASTWriter::RecordDataImpl &Record, bool GeneratingReducedBMI)
        : Writer(Writer), Context(Context), Record(Writer, Record),
          Code((serialization::DeclCode)0), AbbrevToUse(0),
          GeneratingReducedBMI(GeneratingReducedBMI) {}

    uint64_t Emit(Decl *D) {
      if (!Code)
        llvm::report_fatal_error(StringRef("unexpected declaration kind '") +
            D->getDeclKindName() + "'");
      return Record.Emit(Code, AbbrevToUse);
    }

    void Visit(Decl *D);

    void VisitDecl(Decl *D);
    void VisitPragmaCommentDecl(PragmaCommentDecl *D);
    void VisitPragmaDetectMismatchDecl(PragmaDetectMismatchDecl *D);
    void VisitTranslationUnitDecl(TranslationUnitDecl *D);
    void VisitNamedDecl(NamedDecl *D);
    void VisitLabelDecl(LabelDecl *LD);
    void VisitNamespaceDecl(NamespaceDecl *D);
    void VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
    void VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
    void VisitTypeDecl(TypeDecl *D);
    void VisitTypedefNameDecl(TypedefNameDecl *D);
    void VisitTypedefDecl(TypedefDecl *D);
    void VisitTypeAliasDecl(TypeAliasDecl *D);
    void VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
    void VisitUnresolvedUsingIfExistsDecl(UnresolvedUsingIfExistsDecl *D);
    void VisitTagDecl(TagDecl *D);
    void VisitEnumDecl(EnumDecl *D);
    void VisitRecordDecl(RecordDecl *D);
    void VisitCXXRecordDecl(CXXRecordDecl *D);
    void VisitClassTemplateSpecializationDecl(
                                            ClassTemplateSpecializationDecl *D);
    void VisitClassTemplatePartialSpecializationDecl(
                                     ClassTemplatePartialSpecializationDecl *D);
    void VisitVarTemplateSpecializationDecl(VarTemplateSpecializationDecl *D);
    void VisitVarTemplatePartialSpecializationDecl(
        VarTemplatePartialSpecializationDecl *D);
    void VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D);
    void VisitValueDecl(ValueDecl *D);
    void VisitEnumConstantDecl(EnumConstantDecl *D);
    void VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
    void VisitDeclaratorDecl(DeclaratorDecl *D);
    void VisitFunctionDecl(FunctionDecl *D);
    void VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D);
    void VisitCXXMethodDecl(CXXMethodDecl *D);
    void VisitCXXConstructorDecl(CXXConstructorDecl *D);
    void VisitCXXDestructorDecl(CXXDestructorDecl *D);
    void VisitCXXConversionDecl(CXXConversionDecl *D);
    void VisitFieldDecl(FieldDecl *D);
    void VisitMSPropertyDecl(MSPropertyDecl *D);
    void VisitMSGuidDecl(MSGuidDecl *D);
    void VisitUnnamedGlobalConstantDecl(UnnamedGlobalConstantDecl *D);
    void VisitTemplateParamObjectDecl(TemplateParamObjectDecl *D);
    void VisitIndirectFieldDecl(IndirectFieldDecl *D);
    void VisitVarDecl(VarDecl *D);
    void VisitImplicitParamDecl(ImplicitParamDecl *D);
    void VisitParmVarDecl(ParmVarDecl *D);
    void VisitDecompositionDecl(DecompositionDecl *D);
    void VisitBindingDecl(BindingDecl *D);
    void VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D);
    void VisitTemplateDecl(TemplateDecl *D);
    void VisitConceptDecl(ConceptDecl *D);
    void VisitImplicitConceptSpecializationDecl(
        ImplicitConceptSpecializationDecl *D);
    void VisitRequiresExprBodyDecl(RequiresExprBodyDecl *D);
    void VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D);
    void VisitClassTemplateDecl(ClassTemplateDecl *D);
    void VisitVarTemplateDecl(VarTemplateDecl *D);
    void VisitFunctionTemplateDecl(FunctionTemplateDecl *D);
    void VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D);
    void VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D);
    void VisitUsingDecl(UsingDecl *D);
    void VisitUsingEnumDecl(UsingEnumDecl *D);
    void VisitUsingPackDecl(UsingPackDecl *D);
    void VisitUsingShadowDecl(UsingShadowDecl *D);
    void VisitConstructorUsingShadowDecl(ConstructorUsingShadowDecl *D);
    void VisitLinkageSpecDecl(LinkageSpecDecl *D);
    void VisitExportDecl(ExportDecl *D);
    void VisitFileScopeAsmDecl(FileScopeAsmDecl *D);
    void VisitTopLevelStmtDecl(TopLevelStmtDecl *D);
    void VisitImportDecl(ImportDecl *D);
    void VisitAccessSpecDecl(AccessSpecDecl *D);
    void VisitFriendDecl(FriendDecl *D);
    void VisitFriendTemplateDecl(FriendTemplateDecl *D);
    void VisitStaticAssertDecl(StaticAssertDecl *D);
    void VisitBlockDecl(BlockDecl *D);
    void VisitCapturedDecl(CapturedDecl *D);
    void VisitEmptyDecl(EmptyDecl *D);
    void VisitLifetimeExtendedTemporaryDecl(LifetimeExtendedTemporaryDecl *D);
    void VisitDeclContext(DeclContext *DC);
    template <typename T> void VisitRedeclarable(Redeclarable<T> *D);
    void VisitHLSLBufferDecl(HLSLBufferDecl *D);

    // FIXME: Put in the same order is DeclNodes.td?
    void VisitObjCMethodDecl(ObjCMethodDecl *D);
    void VisitObjCTypeParamDecl(ObjCTypeParamDecl *D);
    void VisitObjCContainerDecl(ObjCContainerDecl *D);
    void VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
    void VisitObjCIvarDecl(ObjCIvarDecl *D);
    void VisitObjCProtocolDecl(ObjCProtocolDecl *D);
    void VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *D);
    void VisitObjCCategoryDecl(ObjCCategoryDecl *D);
    void VisitObjCImplDecl(ObjCImplDecl *D);
    void VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
    void VisitObjCImplementationDecl(ObjCImplementationDecl *D);
    void VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D);
    void VisitObjCPropertyDecl(ObjCPropertyDecl *D);
    void VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
    void VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D);
    void VisitOMPAllocateDecl(OMPAllocateDecl *D);
    void VisitOMPRequiresDecl(OMPRequiresDecl *D);
    void VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D);
    void VisitOMPDeclareMapperDecl(OMPDeclareMapperDecl *D);
    void VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D);

    /// Add an Objective-C type parameter list to the given record.
    void AddObjCTypeParamList(ObjCTypeParamList *typeParams) {
      // Empty type parameter list.
      if (!typeParams) {
        Record.push_back(0);
        return;
      }

      Record.push_back(typeParams->size());
      for (auto *typeParam : *typeParams) {
        Record.AddDeclRef(typeParam);
      }
      Record.AddSourceLocation(typeParams->getLAngleLoc());
      Record.AddSourceLocation(typeParams->getRAngleLoc());
    }

    /// Add to the record the first declaration from each module file that
    /// provides a declaration of D. The intent is to provide a sufficient
    /// set such that reloading this set will load all current redeclarations.
    void AddFirstDeclFromEachModule(const Decl *D, bool IncludeLocal) {
      llvm::MapVector<ModuleFile*, const Decl*> Firsts;
      // FIXME: We can skip entries that we know are implied by others.
      for (const Decl *R = D->getMostRecentDecl(); R; R = R->getPreviousDecl()) {
        if (R->isFromASTFile())
          Firsts[Writer.Chain->getOwningModuleFile(R)] = R;
        else if (IncludeLocal)
          Firsts[nullptr] = R;
      }
      for (const auto &F : Firsts)
        Record.AddDeclRef(F.second);
    }

    /// Get the specialization decl from an entry in the specialization list.
    template <typename EntryType>
    typename RedeclarableTemplateDecl::SpecEntryTraits<EntryType>::DeclType *
    getSpecializationDecl(EntryType &T) {
      return RedeclarableTemplateDecl::SpecEntryTraits<EntryType>::getDecl(&T);
    }

    /// Get the list of partial specializations from a template's common ptr.
    template<typename T>
    decltype(T::PartialSpecializations) &getPartialSpecializations(T *Common) {
      return Common->PartialSpecializations;
    }
    ArrayRef<Decl> getPartialSpecializations(FunctionTemplateDecl::Common *) {
      return std::nullopt;
    }

    template<typename DeclTy>
    void AddTemplateSpecializations(DeclTy *D) {
      auto *Common = D->getCommonPtr();

      // If we have any lazy specializations, and the external AST source is
      // our chained AST reader, we can just write out the DeclIDs. Otherwise,
      // we need to resolve them to actual declarations.
      if (Writer.Chain != Writer.Context->getExternalSource() &&
          Common->LazySpecializations) {
        D->LoadLazySpecializations();
        assert(!Common->LazySpecializations);
      }

      ArrayRef<GlobalDeclID> LazySpecializations;
      if (auto *LS = Common->LazySpecializations)
        LazySpecializations = llvm::ArrayRef(LS + 1, LS[0].getRawValue());

      // Add a slot to the record for the number of specializations.
      unsigned I = Record.size();
      Record.push_back(0);

      // AddFirstDeclFromEachModule might trigger deserialization, invalidating
      // *Specializations iterators.
      llvm::SmallVector<const Decl*, 16> Specs;
      for (auto &Entry : Common->Specializations)
        Specs.push_back(getSpecializationDecl(Entry));
      for (auto &Entry : getPartialSpecializations(Common))
        Specs.push_back(getSpecializationDecl(Entry));

      for (auto *D : Specs) {
        assert(D->isCanonicalDecl() && "non-canonical decl in set");
        AddFirstDeclFromEachModule(D, /*IncludeLocal*/true);
      }
      Record.append(
          DeclIDIterator<GlobalDeclID, DeclID>(LazySpecializations.begin()),
          DeclIDIterator<GlobalDeclID, DeclID>(LazySpecializations.end()));

      // Update the size entry we added earlier.
      Record[I] = Record.size() - I - 1;
    }

    /// Ensure that this template specialization is associated with the specified
    /// template on reload.
    void RegisterTemplateSpecialization(const Decl *Template,
                                        const Decl *Specialization) {
      Template = Template->getCanonicalDecl();

      // If the canonical template is local, we'll write out this specialization
      // when we emit it.
      // FIXME: We can do the same thing if there is any local declaration of
      // the template, to avoid emitting an update record.
      if (!Template->isFromASTFile())
        return;

      // We only need to associate the first local declaration of the
      // specialization. The other declarations will get pulled in by it.
      if (Writer.getFirstLocalDecl(Specialization) != Specialization)
        return;

      Writer.DeclUpdates[Template].push_back(ASTWriter::DeclUpdate(
          UPD_CXX_ADDED_TEMPLATE_SPECIALIZATION, Specialization));
    }
  };
}

bool clang::CanElideDeclDef(const Decl *D) {
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isInlined() || FD->isConstexpr())
      return false;

    if (FD->isDependentContext())
      return false;

    if (FD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return false;
  }

  if (auto *VD = dyn_cast<VarDecl>(D)) {
    if (!VD->getDeclContext()->getRedeclContext()->isFileContext() ||
        VD->isInline() || VD->isConstexpr() || isa<ParmVarDecl>(VD) ||
        // Constant initialized variable may not affect the ABI, but they
        // may be used in constant evaluation in the frontend, so we have
        // to remain them.
        VD->hasConstantInitialization())
      return false;

    if (VD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return false;
  }

  return true;
}

void ASTDeclWriter::Visit(Decl *D) {
  DeclVisitor<ASTDeclWriter>::Visit(D);

  // Source locations require array (variable-length) abbreviations.  The
  // abbreviation infrastructure requires that arrays are encoded last, so
  // we handle it here in the case of those classes derived from DeclaratorDecl
  if (auto *DD = dyn_cast<DeclaratorDecl>(D)) {
    if (auto *TInfo = DD->getTypeSourceInfo())
      Record.AddTypeLoc(TInfo->getTypeLoc());
  }

  // Handle FunctionDecl's body here and write it after all other Stmts/Exprs
  // have been written. We want it last because we will not read it back when
  // retrieving it from the AST, we'll just lazily set the offset.
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (!GeneratingReducedBMI || !CanElideDeclDef(FD)) {
      Record.push_back(FD->doesThisDeclarationHaveABody());
      if (FD->doesThisDeclarationHaveABody())
        Record.AddFunctionDefinition(FD);
    } else
      Record.push_back(0);
  }

  // Similar to FunctionDecls, handle VarDecl's initializer here and write it
  // after all other Stmts/Exprs. We will not read the initializer until after
  // we have finished recursive deserialization, because it can recursively
  // refer back to the variable.
  if (auto *VD = dyn_cast<VarDecl>(D)) {
    if (!GeneratingReducedBMI || !CanElideDeclDef(VD))
      Record.AddVarDeclInit(VD);
    else
      Record.push_back(0);
  }

  // And similarly for FieldDecls. We already serialized whether there is a
  // default member initializer.
  if (auto *FD = dyn_cast<FieldDecl>(D)) {
    if (FD->hasInClassInitializer()) {
      if (Expr *Init = FD->getInClassInitializer()) {
        Record.push_back(1);
        Record.AddStmt(Init);
      } else {
        Record.push_back(0);
        // Initializer has not been instantiated yet.
      }
    }
  }

  // If this declaration is also a DeclContext, write blocks for the
  // declarations that lexically stored inside its context and those
  // declarations that are visible from its context.
  if (auto *DC = dyn_cast<DeclContext>(D))
    VisitDeclContext(DC);
}

void ASTDeclWriter::VisitDecl(Decl *D) {
  BitsPacker DeclBits;

  // The order matters here. It will be better to put the bit with higher
  // probability to be 0 in the end of the bits.
  //
  // Since we're using VBR6 format to store it.
  // It will be pretty effient if all the higher bits are 0.
  // For example, if we need to pack 8 bits into a value and the stored value
  // is 0xf0, the actual stored value will be 0b000111'110000, which takes 12
  // bits actually. However, if we changed the order to be 0x0f, then we can
  // store it as 0b001111, which takes 6 bits only now.
  DeclBits.addBits((uint64_t)D->getModuleOwnershipKind(), /*BitWidth=*/3);
  DeclBits.addBit(D->isReferenced());
  DeclBits.addBit(D->isUsed(false));
  DeclBits.addBits(D->getAccess(), /*BitWidth=*/2);
  DeclBits.addBit(D->isImplicit());
  DeclBits.addBit(D->getDeclContext() != D->getLexicalDeclContext());
  DeclBits.addBit(D->hasAttrs());
  DeclBits.addBit(D->isTopLevelDeclInObjCContainer());
  DeclBits.addBit(D->isInvalidDecl());
  Record.push_back(DeclBits);

  Record.AddDeclRef(cast_or_null<Decl>(D->getDeclContext()));
  if (D->getDeclContext() != D->getLexicalDeclContext())
    Record.AddDeclRef(cast_or_null<Decl>(D->getLexicalDeclContext()));

  if (D->hasAttrs())
    Record.AddAttributes(D->getAttrs());

  Record.push_back(Writer.getSubmoduleID(D->getOwningModule()));

  // If this declaration injected a name into a context different from its
  // lexical context, and that context is an imported namespace, we need to
  // update its visible declarations to include this name.
  //
  // This happens when we instantiate a class with a friend declaration or a
  // function with a local extern declaration, for instance.
  //
  // FIXME: Can we handle this in AddedVisibleDecl instead?
  if (D->isOutOfLine()) {
    auto *DC = D->getDeclContext();
    while (auto *NS = dyn_cast<NamespaceDecl>(DC->getRedeclContext())) {
      if (!NS->isFromASTFile())
        break;
      Writer.UpdatedDeclContexts.insert(NS->getPrimaryContext());
      if (!NS->isInlineNamespace())
        break;
      DC = NS->getParent();
    }
  }
}

void ASTDeclWriter::VisitPragmaCommentDecl(PragmaCommentDecl *D) {
  StringRef Arg = D->getArg();
  Record.push_back(Arg.size());
  VisitDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.push_back(D->getCommentKind());
  Record.AddString(Arg);
  Code = serialization::DECL_PRAGMA_COMMENT;
}

void ASTDeclWriter::VisitPragmaDetectMismatchDecl(
    PragmaDetectMismatchDecl *D) {
  StringRef Name = D->getName();
  StringRef Value = D->getValue();
  Record.push_back(Name.size() + 1 + Value.size());
  VisitDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddString(Name);
  Record.AddString(Value);
  Code = serialization::DECL_PRAGMA_DETECT_MISMATCH;
}

void ASTDeclWriter::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  llvm_unreachable("Translation units aren't directly serialized");
}

void ASTDeclWriter::VisitNamedDecl(NamedDecl *D) {
  VisitDecl(D);
  Record.AddDeclarationName(D->getDeclName());
  Record.push_back(needsAnonymousDeclarationNumber(D)
                       ? Writer.getAnonymousDeclarationNumber(D)
                       : 0);
}

void ASTDeclWriter::VisitTypeDecl(TypeDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddTypeRef(QualType(D->getTypeForDecl(), 0));
}

void ASTDeclWriter::VisitTypedefNameDecl(TypedefNameDecl *D) {
  VisitRedeclarable(D);
  VisitTypeDecl(D);
  Record.AddTypeSourceInfo(D->getTypeSourceInfo());
  Record.push_back(D->isModed());
  if (D->isModed())
    Record.AddTypeRef(D->getUnderlyingType());
  Record.AddDeclRef(D->getAnonDeclWithTypedefName(false));
}

void ASTDeclWriter::VisitTypedefDecl(TypedefDecl *D) {
  VisitTypedefNameDecl(D);
  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      !D->isInvalidDecl() &&
      !D->isTopLevelDeclInObjCContainer() &&
      !D->isModulePrivate() &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclTypedefAbbrev();

  Code = serialization::DECL_TYPEDEF;
}

void ASTDeclWriter::VisitTypeAliasDecl(TypeAliasDecl *D) {
  VisitTypedefNameDecl(D);
  Record.AddDeclRef(D->getDescribedAliasTemplate());
  Code = serialization::DECL_TYPEALIAS;
}

void ASTDeclWriter::VisitTagDecl(TagDecl *D) {
  static_assert(DeclContext::NumTagDeclBits == 23,
                "You need to update the serializer after you change the "
                "TagDeclBits");

  VisitRedeclarable(D);
  VisitTypeDecl(D);
  Record.push_back(D->getIdentifierNamespace());

  BitsPacker TagDeclBits;
  TagDeclBits.addBits(llvm::to_underlying(D->getTagKind()), /*BitWidth=*/3);
  TagDeclBits.addBit(!isa<CXXRecordDecl>(D) ? D->isCompleteDefinition() : 0);
  TagDeclBits.addBit(D->isEmbeddedInDeclarator());
  TagDeclBits.addBit(D->isFreeStanding());
  TagDeclBits.addBit(D->isCompleteDefinitionRequired());
  TagDeclBits.addBits(
      D->hasExtInfo() ? 1 : (D->getTypedefNameForAnonDecl() ? 2 : 0),
      /*BitWidth=*/2);
  Record.push_back(TagDeclBits);

  Record.AddSourceRange(D->getBraceRange());

  if (D->hasExtInfo()) {
    Record.AddQualifierInfo(*D->getExtInfo());
  } else if (auto *TD = D->getTypedefNameForAnonDecl()) {
    Record.AddDeclRef(TD);
    Record.AddIdentifierRef(TD->getDeclName().getAsIdentifierInfo());
  }
}

void ASTDeclWriter::VisitEnumDecl(EnumDecl *D) {
  static_assert(DeclContext::NumEnumDeclBits == 43,
                "You need to update the serializer after you change the "
                "EnumDeclBits");

  VisitTagDecl(D);
  Record.AddTypeSourceInfo(D->getIntegerTypeSourceInfo());
  if (!D->getIntegerTypeSourceInfo())
    Record.AddTypeRef(D->getIntegerType());
  Record.AddTypeRef(D->getPromotionType());

  BitsPacker EnumDeclBits;
  EnumDeclBits.addBits(D->getNumPositiveBits(), /*BitWidth=*/8);
  EnumDeclBits.addBits(D->getNumNegativeBits(), /*BitWidth=*/8);
  EnumDeclBits.addBit(D->isScoped());
  EnumDeclBits.addBit(D->isScopedUsingClassTag());
  EnumDeclBits.addBit(D->isFixed());
  Record.push_back(EnumDeclBits);

  Record.push_back(D->getODRHash());

  if (MemberSpecializationInfo *MemberInfo = D->getMemberSpecializationInfo()) {
    Record.AddDeclRef(MemberInfo->getInstantiatedFrom());
    Record.push_back(MemberInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(MemberInfo->getPointOfInstantiation());
  } else {
    Record.AddDeclRef(nullptr);
  }

  if (D->getDeclContext() == D->getLexicalDeclContext() && !D->hasAttrs() &&
      !D->isInvalidDecl() && !D->isImplicit() && !D->hasExtInfo() &&
      !D->getTypedefNameForAnonDecl() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      !D->isTopLevelDeclInObjCContainer() &&
      !CXXRecordDecl::classofKind(D->getKind()) &&
      !D->getIntegerTypeSourceInfo() && !D->getMemberSpecializationInfo() &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclEnumAbbrev();

  Code = serialization::DECL_ENUM;
}

void ASTDeclWriter::VisitRecordDecl(RecordDecl *D) {
  static_assert(DeclContext::NumRecordDeclBits == 64,
                "You need to update the serializer after you change the "
                "RecordDeclBits");

  VisitTagDecl(D);

  BitsPacker RecordDeclBits;
  RecordDeclBits.addBit(D->hasFlexibleArrayMember());
  RecordDeclBits.addBit(D->isAnonymousStructOrUnion());
  RecordDeclBits.addBit(D->hasObjectMember());
  RecordDeclBits.addBit(D->hasVolatileMember());
  RecordDeclBits.addBit(D->isNonTrivialToPrimitiveDefaultInitialize());
  RecordDeclBits.addBit(D->isNonTrivialToPrimitiveCopy());
  RecordDeclBits.addBit(D->isNonTrivialToPrimitiveDestroy());
  RecordDeclBits.addBit(D->hasNonTrivialToPrimitiveDefaultInitializeCUnion());
  RecordDeclBits.addBit(D->hasNonTrivialToPrimitiveDestructCUnion());
  RecordDeclBits.addBit(D->hasNonTrivialToPrimitiveCopyCUnion());
  RecordDeclBits.addBit(D->isParamDestroyedInCallee());
  RecordDeclBits.addBits(llvm::to_underlying(D->getArgPassingRestrictions()), 2);
  Record.push_back(RecordDeclBits);

  // Only compute this for C/Objective-C, in C++ this is computed as part
  // of CXXRecordDecl.
  if (!isa<CXXRecordDecl>(D))
    Record.push_back(D->getODRHash());

  if (D->getDeclContext() == D->getLexicalDeclContext() && !D->hasAttrs() &&
      !D->isImplicit() && !D->isInvalidDecl() && !D->hasExtInfo() &&
      !D->getTypedefNameForAnonDecl() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      !D->isTopLevelDeclInObjCContainer() &&
      !CXXRecordDecl::classofKind(D->getKind()) &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclRecordAbbrev();

  Code = serialization::DECL_RECORD;
}

void ASTDeclWriter::VisitValueDecl(ValueDecl *D) {
  VisitNamedDecl(D);
  Record.AddTypeRef(D->getType());
}

void ASTDeclWriter::VisitEnumConstantDecl(EnumConstantDecl *D) {
  VisitValueDecl(D);
  Record.push_back(D->getInitExpr()? 1 : 0);
  if (D->getInitExpr())
    Record.AddStmt(D->getInitExpr());
  Record.AddAPSInt(D->getInitVal());

  Code = serialization::DECL_ENUM_CONSTANT;
}

void ASTDeclWriter::VisitDeclaratorDecl(DeclaratorDecl *D) {
  VisitValueDecl(D);
  Record.AddSourceLocation(D->getInnerLocStart());
  Record.push_back(D->hasExtInfo());
  if (D->hasExtInfo()) {
    DeclaratorDecl::ExtInfo *Info = D->getExtInfo();
    Record.AddQualifierInfo(*Info);
    Record.AddStmt(Info->TrailingRequiresClause);
  }
  // The location information is deferred until the end of the record.
  Record.AddTypeRef(D->getTypeSourceInfo() ? D->getTypeSourceInfo()->getType()
                                           : QualType());
}

void ASTDeclWriter::VisitFunctionDecl(FunctionDecl *D) {
  static_assert(DeclContext::NumFunctionDeclBits == 44,
                "You need to update the serializer after you change the "
                "FunctionDeclBits");

  VisitRedeclarable(D);

  Record.push_back(D->getTemplatedKind());
  switch (D->getTemplatedKind()) {
  case FunctionDecl::TK_NonTemplate:
    break;
  case FunctionDecl::TK_DependentNonTemplate:
    Record.AddDeclRef(D->getInstantiatedFromDecl());
    break;
  case FunctionDecl::TK_FunctionTemplate:
    Record.AddDeclRef(D->getDescribedFunctionTemplate());
    break;
  case FunctionDecl::TK_MemberSpecialization: {
    MemberSpecializationInfo *MemberInfo = D->getMemberSpecializationInfo();
    Record.AddDeclRef(MemberInfo->getInstantiatedFrom());
    Record.push_back(MemberInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(MemberInfo->getPointOfInstantiation());
    break;
  }
  case FunctionDecl::TK_FunctionTemplateSpecialization: {
    FunctionTemplateSpecializationInfo *
      FTSInfo = D->getTemplateSpecializationInfo();

    RegisterTemplateSpecialization(FTSInfo->getTemplate(), D);

    Record.AddDeclRef(FTSInfo->getTemplate());
    Record.push_back(FTSInfo->getTemplateSpecializationKind());

    // Template arguments.
    Record.AddTemplateArgumentList(FTSInfo->TemplateArguments);

    // Template args as written.
    Record.push_back(FTSInfo->TemplateArgumentsAsWritten != nullptr);
    if (FTSInfo->TemplateArgumentsAsWritten)
      Record.AddASTTemplateArgumentListInfo(
          FTSInfo->TemplateArgumentsAsWritten);

    Record.AddSourceLocation(FTSInfo->getPointOfInstantiation());

    if (MemberSpecializationInfo *MemberInfo =
        FTSInfo->getMemberSpecializationInfo()) {
      Record.push_back(1);
      Record.AddDeclRef(MemberInfo->getInstantiatedFrom());
      Record.push_back(MemberInfo->getTemplateSpecializationKind());
      Record.AddSourceLocation(MemberInfo->getPointOfInstantiation());
    } else {
      Record.push_back(0);
    }

    if (D->isCanonicalDecl()) {
      // Write the template that contains the specializations set. We will
      // add a FunctionTemplateSpecializationInfo to it when reading.
      Record.AddDeclRef(FTSInfo->getTemplate()->getCanonicalDecl());
    }
    break;
  }
  case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
    DependentFunctionTemplateSpecializationInfo *
      DFTSInfo = D->getDependentSpecializationInfo();

    // Candidates.
    Record.push_back(DFTSInfo->getCandidates().size());
    for (FunctionTemplateDecl *FTD : DFTSInfo->getCandidates())
      Record.AddDeclRef(FTD);

    // Templates args.
    Record.push_back(DFTSInfo->TemplateArgumentsAsWritten != nullptr);
    if (DFTSInfo->TemplateArgumentsAsWritten)
      Record.AddASTTemplateArgumentListInfo(
          DFTSInfo->TemplateArgumentsAsWritten);
    break;
  }
  }

  VisitDeclaratorDecl(D);
  Record.AddDeclarationNameLoc(D->DNLoc, D->getDeclName());
  Record.push_back(D->getIdentifierNamespace());

  // The order matters here. It will be better to put the bit with higher
  // probability to be 0 in the end of the bits. See the comments in VisitDecl
  // for details.
  BitsPacker FunctionDeclBits;
  // FIXME: stable encoding
  FunctionDeclBits.addBits(llvm::to_underlying(D->getLinkageInternal()), 3);
  FunctionDeclBits.addBits((uint32_t)D->getStorageClass(), /*BitWidth=*/3);
  FunctionDeclBits.addBit(D->isInlineSpecified());
  FunctionDeclBits.addBit(D->isInlined());
  FunctionDeclBits.addBit(D->hasSkippedBody());
  FunctionDeclBits.addBit(D->isVirtualAsWritten());
  FunctionDeclBits.addBit(D->isPureVirtual());
  FunctionDeclBits.addBit(D->hasInheritedPrototype());
  FunctionDeclBits.addBit(D->hasWrittenPrototype());
  FunctionDeclBits.addBit(D->isDeletedBit());
  FunctionDeclBits.addBit(D->isTrivial());
  FunctionDeclBits.addBit(D->isTrivialForCall());
  FunctionDeclBits.addBit(D->isDefaulted());
  FunctionDeclBits.addBit(D->isExplicitlyDefaulted());
  FunctionDeclBits.addBit(D->isIneligibleOrNotSelected());
  FunctionDeclBits.addBits((uint64_t)(D->getConstexprKind()), /*BitWidth=*/2);
  FunctionDeclBits.addBit(D->hasImplicitReturnZero());
  FunctionDeclBits.addBit(D->isMultiVersion());
  FunctionDeclBits.addBit(D->isLateTemplateParsed());
  FunctionDeclBits.addBit(D->FriendConstraintRefersToEnclosingTemplate());
  FunctionDeclBits.addBit(D->usesSEHTry());
  Record.push_back(FunctionDeclBits);

  Record.AddSourceLocation(D->getEndLoc());
  if (D->isExplicitlyDefaulted())
    Record.AddSourceLocation(D->getDefaultLoc());

  Record.push_back(D->getODRHash());

  if (D->isDefaulted() || D->isDeletedAsWritten()) {
    if (auto *FDI = D->getDefalutedOrDeletedInfo()) {
      // Store both that there is an DefaultedOrDeletedInfo and whether it
      // contains a DeletedMessage.
      StringLiteral *DeletedMessage = FDI->getDeletedMessage();
      Record.push_back(1 | (DeletedMessage ? 2 : 0));
      if (DeletedMessage)
        Record.AddStmt(DeletedMessage);

      Record.push_back(FDI->getUnqualifiedLookups().size());
      for (DeclAccessPair P : FDI->getUnqualifiedLookups()) {
        Record.AddDeclRef(P.getDecl());
        Record.push_back(P.getAccess());
      }
    } else {
      Record.push_back(0);
    }
  }

  Record.push_back(D->param_size());
  for (auto *P : D->parameters())
    Record.AddDeclRef(P);
  Code = serialization::DECL_FUNCTION;
}

static void addExplicitSpecifier(ExplicitSpecifier ES,
                                 ASTRecordWriter &Record) {
  uint64_t Kind = static_cast<uint64_t>(ES.getKind());
  Kind = Kind << 1 | static_cast<bool>(ES.getExpr());
  Record.push_back(Kind);
  if (ES.getExpr()) {
    Record.AddStmt(ES.getExpr());
  }
}

void ASTDeclWriter::VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D) {
  addExplicitSpecifier(D->getExplicitSpecifier(), Record);
  Record.AddDeclRef(D->Ctor);
  VisitFunctionDecl(D);
  Record.push_back(static_cast<unsigned char>(D->getDeductionCandidateKind()));
  Code = serialization::DECL_CXX_DEDUCTION_GUIDE;
}

void ASTDeclWriter::VisitObjCMethodDecl(ObjCMethodDecl *D) {
  static_assert(DeclContext::NumObjCMethodDeclBits == 37,
                "You need to update the serializer after you change the "
                "ObjCMethodDeclBits");

  VisitNamedDecl(D);
  // FIXME: convert to LazyStmtPtr?
  // Unlike C/C++, method bodies will never be in header files.
  bool HasBodyStuff = D->getBody() != nullptr;
  Record.push_back(HasBodyStuff);
  if (HasBodyStuff) {
    Record.AddStmt(D->getBody());
  }
  Record.AddDeclRef(D->getSelfDecl());
  Record.AddDeclRef(D->getCmdDecl());
  Record.push_back(D->isInstanceMethod());
  Record.push_back(D->isVariadic());
  Record.push_back(D->isPropertyAccessor());
  Record.push_back(D->isSynthesizedAccessorStub());
  Record.push_back(D->isDefined());
  Record.push_back(D->isOverriding());
  Record.push_back(D->hasSkippedBody());

  Record.push_back(D->isRedeclaration());
  Record.push_back(D->hasRedeclaration());
  if (D->hasRedeclaration()) {
    assert(Context.getObjCMethodRedeclaration(D));
    Record.AddDeclRef(Context.getObjCMethodRedeclaration(D));
  }

  // FIXME: stable encoding for @required/@optional
  Record.push_back(llvm::to_underlying(D->getImplementationControl()));
  // FIXME: stable encoding for in/out/inout/bycopy/byref/oneway/nullability
  Record.push_back(D->getObjCDeclQualifier());
  Record.push_back(D->hasRelatedResultType());
  Record.AddTypeRef(D->getReturnType());
  Record.AddTypeSourceInfo(D->getReturnTypeSourceInfo());
  Record.AddSourceLocation(D->getEndLoc());
  Record.push_back(D->param_size());
  for (const auto *P : D->parameters())
    Record.AddDeclRef(P);

  Record.push_back(D->getSelLocsKind());
  unsigned NumStoredSelLocs = D->getNumStoredSelLocs();
  SourceLocation *SelLocs = D->getStoredSelLocs();
  Record.push_back(NumStoredSelLocs);
  for (unsigned i = 0; i != NumStoredSelLocs; ++i)
    Record.AddSourceLocation(SelLocs[i]);

  Code = serialization::DECL_OBJC_METHOD;
}

void ASTDeclWriter::VisitObjCTypeParamDecl(ObjCTypeParamDecl *D) {
  VisitTypedefNameDecl(D);
  Record.push_back(D->Variance);
  Record.push_back(D->Index);
  Record.AddSourceLocation(D->VarianceLoc);
  Record.AddSourceLocation(D->ColonLoc);

  Code = serialization::DECL_OBJC_TYPE_PARAM;
}

void ASTDeclWriter::VisitObjCContainerDecl(ObjCContainerDecl *D) {
  static_assert(DeclContext::NumObjCContainerDeclBits == 64,
                "You need to update the serializer after you change the "
                "ObjCContainerDeclBits");

  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getAtStartLoc());
  Record.AddSourceRange(D->getAtEndRange());
  // Abstract class (no need to define a stable serialization::DECL code).
}

void ASTDeclWriter::VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
  VisitRedeclarable(D);
  VisitObjCContainerDecl(D);
  Record.AddTypeRef(QualType(D->getTypeForDecl(), 0));
  AddObjCTypeParamList(D->TypeParamList);

  Record.push_back(D->isThisDeclarationADefinition());
  if (D->isThisDeclarationADefinition()) {
    // Write the DefinitionData
    ObjCInterfaceDecl::DefinitionData &Data = D->data();

    Record.AddTypeSourceInfo(D->getSuperClassTInfo());
    Record.AddSourceLocation(D->getEndOfDefinitionLoc());
    Record.push_back(Data.HasDesignatedInitializers);
    Record.push_back(D->getODRHash());

    // Write out the protocols that are directly referenced by the @interface.
    Record.push_back(Data.ReferencedProtocols.size());
    for (const auto *P : D->protocols())
      Record.AddDeclRef(P);
    for (const auto &PL : D->protocol_locs())
      Record.AddSourceLocation(PL);

    // Write out the protocols that are transitively referenced.
    Record.push_back(Data.AllReferencedProtocols.size());
    for (ObjCList<ObjCProtocolDecl>::iterator
              P = Data.AllReferencedProtocols.begin(),
           PEnd = Data.AllReferencedProtocols.end();
         P != PEnd; ++P)
      Record.AddDeclRef(*P);


    if (ObjCCategoryDecl *Cat = D->getCategoryListRaw()) {
      // Ensure that we write out the set of categories for this class.
      Writer.ObjCClassesWithCategories.insert(D);

      // Make sure that the categories get serialized.
      for (; Cat; Cat = Cat->getNextClassCategoryRaw())
        (void)Writer.GetDeclRef(Cat);
    }
  }

  Code = serialization::DECL_OBJC_INTERFACE;
}

void ASTDeclWriter::VisitObjCIvarDecl(ObjCIvarDecl *D) {
  VisitFieldDecl(D);
  // FIXME: stable encoding for @public/@private/@protected/@package
  Record.push_back(D->getAccessControl());
  Record.push_back(D->getSynthesize());

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      !D->isModulePrivate() &&
      !D->getBitWidth() &&
      !D->hasExtInfo() &&
      D->getDeclName())
    AbbrevToUse = Writer.getDeclObjCIvarAbbrev();

  Code = serialization::DECL_OBJC_IVAR;
}

void ASTDeclWriter::VisitObjCProtocolDecl(ObjCProtocolDecl *D) {
  VisitRedeclarable(D);
  VisitObjCContainerDecl(D);

  Record.push_back(D->isThisDeclarationADefinition());
  if (D->isThisDeclarationADefinition()) {
    Record.push_back(D->protocol_size());
    for (const auto *I : D->protocols())
      Record.AddDeclRef(I);
    for (const auto &PL : D->protocol_locs())
      Record.AddSourceLocation(PL);
    Record.push_back(D->getODRHash());
  }

  Code = serialization::DECL_OBJC_PROTOCOL;
}

void ASTDeclWriter::VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *D) {
  VisitFieldDecl(D);
  Code = serialization::DECL_OBJC_AT_DEFS_FIELD;
}

void ASTDeclWriter::VisitObjCCategoryDecl(ObjCCategoryDecl *D) {
  VisitObjCContainerDecl(D);
  Record.AddSourceLocation(D->getCategoryNameLoc());
  Record.AddSourceLocation(D->getIvarLBraceLoc());
  Record.AddSourceLocation(D->getIvarRBraceLoc());
  Record.AddDeclRef(D->getClassInterface());
  AddObjCTypeParamList(D->TypeParamList);
  Record.push_back(D->protocol_size());
  for (const auto *I : D->protocols())
    Record.AddDeclRef(I);
  for (const auto &PL : D->protocol_locs())
    Record.AddSourceLocation(PL);
  Code = serialization::DECL_OBJC_CATEGORY;
}

void ASTDeclWriter::VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D) {
  VisitNamedDecl(D);
  Record.AddDeclRef(D->getClassInterface());
  Code = serialization::DECL_OBJC_COMPATIBLE_ALIAS;
}

void ASTDeclWriter::VisitObjCPropertyDecl(ObjCPropertyDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getAtLoc());
  Record.AddSourceLocation(D->getLParenLoc());
  Record.AddTypeRef(D->getType());
  Record.AddTypeSourceInfo(D->getTypeSourceInfo());
  // FIXME: stable encoding
  Record.push_back((unsigned)D->getPropertyAttributes());
  Record.push_back((unsigned)D->getPropertyAttributesAsWritten());
  // FIXME: stable encoding
  Record.push_back((unsigned)D->getPropertyImplementation());
  Record.AddDeclarationName(D->getGetterName());
  Record.AddSourceLocation(D->getGetterNameLoc());
  Record.AddDeclarationName(D->getSetterName());
  Record.AddSourceLocation(D->getSetterNameLoc());
  Record.AddDeclRef(D->getGetterMethodDecl());
  Record.AddDeclRef(D->getSetterMethodDecl());
  Record.AddDeclRef(D->getPropertyIvarDecl());
  Code = serialization::DECL_OBJC_PROPERTY;
}

void ASTDeclWriter::VisitObjCImplDecl(ObjCImplDecl *D) {
  VisitObjCContainerDecl(D);
  Record.AddDeclRef(D->getClassInterface());
  // Abstract class (no need to define a stable serialization::DECL code).
}

void ASTDeclWriter::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D) {
  VisitObjCImplDecl(D);
  Record.AddSourceLocation(D->getCategoryNameLoc());
  Code = serialization::DECL_OBJC_CATEGORY_IMPL;
}

void ASTDeclWriter::VisitObjCImplementationDecl(ObjCImplementationDecl *D) {
  VisitObjCImplDecl(D);
  Record.AddDeclRef(D->getSuperClass());
  Record.AddSourceLocation(D->getSuperClassLoc());
  Record.AddSourceLocation(D->getIvarLBraceLoc());
  Record.AddSourceLocation(D->getIvarRBraceLoc());
  Record.push_back(D->hasNonZeroConstructors());
  Record.push_back(D->hasDestructors());
  Record.push_back(D->NumIvarInitializers);
  if (D->NumIvarInitializers)
    Record.AddCXXCtorInitializers(
        llvm::ArrayRef(D->init_begin(), D->init_end()));
  Code = serialization::DECL_OBJC_IMPLEMENTATION;
}

void ASTDeclWriter::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
  VisitDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddDeclRef(D->getPropertyDecl());
  Record.AddDeclRef(D->getPropertyIvarDecl());
  Record.AddSourceLocation(D->getPropertyIvarDeclLoc());
  Record.AddDeclRef(D->getGetterMethodDecl());
  Record.AddDeclRef(D->getSetterMethodDecl());
  Record.AddStmt(D->getGetterCXXConstructor());
  Record.AddStmt(D->getSetterCXXAssignment());
  Code = serialization::DECL_OBJC_PROPERTY_IMPL;
}

void ASTDeclWriter::VisitFieldDecl(FieldDecl *D) {
  VisitDeclaratorDecl(D);
  Record.push_back(D->isMutable());

  Record.push_back((D->StorageKind << 1) | D->BitField);
  if (D->StorageKind == FieldDecl::ISK_CapturedVLAType)
    Record.AddTypeRef(QualType(D->getCapturedVLAType(), 0));
  else if (D->BitField)
    Record.AddStmt(D->getBitWidth());

  if (!D->getDeclName())
    Record.AddDeclRef(Context.getInstantiatedFromUnnamedFieldDecl(D));

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      !D->isTopLevelDeclInObjCContainer() &&
      !D->isModulePrivate() &&
      !D->getBitWidth() &&
      !D->hasInClassInitializer() &&
      !D->hasCapturedVLAType() &&
      !D->hasExtInfo() &&
      !ObjCIvarDecl::classofKind(D->getKind()) &&
      !ObjCAtDefsFieldDecl::classofKind(D->getKind()) &&
      D->getDeclName())
    AbbrevToUse = Writer.getDeclFieldAbbrev();

  Code = serialization::DECL_FIELD;
}

void ASTDeclWriter::VisitMSPropertyDecl(MSPropertyDecl *D) {
  VisitDeclaratorDecl(D);
  Record.AddIdentifierRef(D->getGetterId());
  Record.AddIdentifierRef(D->getSetterId());
  Code = serialization::DECL_MS_PROPERTY;
}

void ASTDeclWriter::VisitMSGuidDecl(MSGuidDecl *D) {
  VisitValueDecl(D);
  MSGuidDecl::Parts Parts = D->getParts();
  Record.push_back(Parts.Part1);
  Record.push_back(Parts.Part2);
  Record.push_back(Parts.Part3);
  Record.append(std::begin(Parts.Part4And5), std::end(Parts.Part4And5));
  Code = serialization::DECL_MS_GUID;
}

void ASTDeclWriter::VisitUnnamedGlobalConstantDecl(
    UnnamedGlobalConstantDecl *D) {
  VisitValueDecl(D);
  Record.AddAPValue(D->getValue());
  Code = serialization::DECL_UNNAMED_GLOBAL_CONSTANT;
}

void ASTDeclWriter::VisitTemplateParamObjectDecl(TemplateParamObjectDecl *D) {
  VisitValueDecl(D);
  Record.AddAPValue(D->getValue());
  Code = serialization::DECL_TEMPLATE_PARAM_OBJECT;
}

void ASTDeclWriter::VisitIndirectFieldDecl(IndirectFieldDecl *D) {
  VisitValueDecl(D);
  Record.push_back(D->getChainingSize());

  for (const auto *P : D->chain())
    Record.AddDeclRef(P);
  Code = serialization::DECL_INDIRECTFIELD;
}

void ASTDeclWriter::VisitVarDecl(VarDecl *D) {
  VisitRedeclarable(D);
  VisitDeclaratorDecl(D);

  // The order matters here. It will be better to put the bit with higher
  // probability to be 0 in the end of the bits. See the comments in VisitDecl
  // for details.
  BitsPacker VarDeclBits;
  VarDeclBits.addBits(llvm::to_underlying(D->getLinkageInternal()),
                      /*BitWidth=*/3);

  bool ModulesCodegen = false;
  if (Writer.WritingModule && D->getStorageDuration() == SD_Static &&
      !D->getDescribedVarTemplate()) {
    // When building a C++20 module interface unit or a partition unit, a
    // strong definition in the module interface is provided by the
    // compilation of that unit, not by its users. (Inline variables are still
    // emitted in module users.)
    ModulesCodegen =
        (Writer.WritingModule->isInterfaceOrPartition() ||
         (D->hasAttr<DLLExportAttr>() &&
          Writer.Context->getLangOpts().BuildingPCHWithObjectFile)) &&
        Writer.Context->GetGVALinkageForVariable(D) >= GVA_StrongExternal;
  }
  VarDeclBits.addBit(ModulesCodegen);

  VarDeclBits.addBits(D->getStorageClass(), /*BitWidth=*/3);
  VarDeclBits.addBits(D->getTSCSpec(), /*BitWidth=*/2);
  VarDeclBits.addBits(D->getInitStyle(), /*BitWidth=*/2);
  VarDeclBits.addBit(D->isARCPseudoStrong());

  bool HasDeducedType = false;
  if (!isa<ParmVarDecl>(D)) {
    VarDeclBits.addBit(D->isThisDeclarationADemotedDefinition());
    VarDeclBits.addBit(D->isExceptionVariable());
    VarDeclBits.addBit(D->isNRVOVariable());
    VarDeclBits.addBit(D->isCXXForRangeDecl());

    VarDeclBits.addBit(D->isInline());
    VarDeclBits.addBit(D->isInlineSpecified());
    VarDeclBits.addBit(D->isConstexpr());
    VarDeclBits.addBit(D->isInitCapture());
    VarDeclBits.addBit(D->isPreviousDeclInSameBlockScope());

    VarDeclBits.addBit(D->isEscapingByref());
    HasDeducedType = D->getType()->getContainedDeducedType();
    VarDeclBits.addBit(HasDeducedType);

    if (const auto *IPD = dyn_cast<ImplicitParamDecl>(D))
      VarDeclBits.addBits(llvm::to_underlying(IPD->getParameterKind()),
                          /*Width=*/3);
    else
      VarDeclBits.addBits(0, /*Width=*/3);

    VarDeclBits.addBit(D->isObjCForDecl());
  }

  Record.push_back(VarDeclBits);

  if (ModulesCodegen)
    Writer.AddDeclRef(D, Writer.ModularCodegenDecls);

  if (D->hasAttr<BlocksAttr>()) {
    BlockVarCopyInit Init = Writer.Context->getBlockVarCopyInit(D);
    Record.AddStmt(Init.getCopyExpr());
    if (Init.getCopyExpr())
      Record.push_back(Init.canThrow());
  }

  enum {
    VarNotTemplate = 0, VarTemplate, StaticDataMemberSpecialization
  };
  if (VarTemplateDecl *TemplD = D->getDescribedVarTemplate()) {
    Record.push_back(VarTemplate);
    Record.AddDeclRef(TemplD);
  } else if (MemberSpecializationInfo *SpecInfo
               = D->getMemberSpecializationInfo()) {
    Record.push_back(StaticDataMemberSpecialization);
    Record.AddDeclRef(SpecInfo->getInstantiatedFrom());
    Record.push_back(SpecInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(SpecInfo->getPointOfInstantiation());
  } else {
    Record.push_back(VarNotTemplate);
  }

  if (D->getDeclContext() == D->getLexicalDeclContext() && !D->hasAttrs() &&
      !D->isTopLevelDeclInObjCContainer() &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier &&
      !D->hasExtInfo() && D->getFirstDecl() == D->getMostRecentDecl() &&
      D->getKind() == Decl::Var && !D->isInline() && !D->isConstexpr() &&
      !D->isInitCapture() && !D->isPreviousDeclInSameBlockScope() &&
      !D->isEscapingByref() && !HasDeducedType &&
      D->getStorageDuration() != SD_Static && !D->getDescribedVarTemplate() &&
      !D->getMemberSpecializationInfo() && !D->isObjCForDecl() &&
      !isa<ImplicitParamDecl>(D) && !D->isEscapingByref())
    AbbrevToUse = Writer.getDeclVarAbbrev();

  Code = serialization::DECL_VAR;
}

void ASTDeclWriter::VisitImplicitParamDecl(ImplicitParamDecl *D) {
  VisitVarDecl(D);
  Code = serialization::DECL_IMPLICIT_PARAM;
}

void ASTDeclWriter::VisitParmVarDecl(ParmVarDecl *D) {
  VisitVarDecl(D);

  // See the implementation of `ParmVarDecl::getParameterIndex()`, which may
  // exceed the size of the normal bitfield. So it may be better to not pack
  // these bits.
  Record.push_back(D->getFunctionScopeIndex());

  BitsPacker ParmVarDeclBits;
  ParmVarDeclBits.addBit(D->isObjCMethodParameter());
  ParmVarDeclBits.addBits(D->getFunctionScopeDepth(), /*BitsWidth=*/7);
  // FIXME: stable encoding
  ParmVarDeclBits.addBits(D->getObjCDeclQualifier(), /*BitsWidth=*/7);
  ParmVarDeclBits.addBit(D->isKNRPromoted());
  ParmVarDeclBits.addBit(D->hasInheritedDefaultArg());
  ParmVarDeclBits.addBit(D->hasUninstantiatedDefaultArg());
  ParmVarDeclBits.addBit(D->getExplicitObjectParamThisLoc().isValid());
  Record.push_back(ParmVarDeclBits);

  if (D->hasUninstantiatedDefaultArg())
    Record.AddStmt(D->getUninstantiatedDefaultArg());
  if (D->getExplicitObjectParamThisLoc().isValid())
    Record.AddSourceLocation(D->getExplicitObjectParamThisLoc());
  Code = serialization::DECL_PARM_VAR;

  // If the assumptions about the DECL_PARM_VAR abbrev are true, use it.  Here
  // we dynamically check for the properties that we optimize for, but don't
  // know are true of all PARM_VAR_DECLs.
  if (D->getDeclContext() == D->getLexicalDeclContext() && !D->hasAttrs() &&
      !D->hasExtInfo() && D->getStorageClass() == 0 && !D->isInvalidDecl() &&
      !D->isTopLevelDeclInObjCContainer() &&
      D->getInitStyle() == VarDecl::CInit && // Can params have anything else?
      D->getInit() == nullptr)               // No default expr.
    AbbrevToUse = Writer.getDeclParmVarAbbrev();

  // Check things we know are true of *every* PARM_VAR_DECL, which is more than
  // just us assuming it.
  assert(!D->getTSCSpec() && "PARM_VAR_DECL can't use TLS");
  assert(!D->isThisDeclarationADemotedDefinition()
         && "PARM_VAR_DECL can't be demoted definition.");
  assert(D->getAccess() == AS_none && "PARM_VAR_DECL can't be public/private");
  assert(!D->isExceptionVariable() && "PARM_VAR_DECL can't be exception var");
  assert(D->getPreviousDecl() == nullptr && "PARM_VAR_DECL can't be redecl");
  assert(!D->isStaticDataMember() &&
         "PARM_VAR_DECL can't be static data member");
}

void ASTDeclWriter::VisitDecompositionDecl(DecompositionDecl *D) {
  // Record the number of bindings first to simplify deserialization.
  Record.push_back(D->bindings().size());

  VisitVarDecl(D);
  for (auto *B : D->bindings())
    Record.AddDeclRef(B);
  Code = serialization::DECL_DECOMPOSITION;
}

void ASTDeclWriter::VisitBindingDecl(BindingDecl *D) {
  VisitValueDecl(D);
  Record.AddStmt(D->getBinding());
  Code = serialization::DECL_BINDING;
}

void ASTDeclWriter::VisitFileScopeAsmDecl(FileScopeAsmDecl *D) {
  VisitDecl(D);
  Record.AddStmt(D->getAsmString());
  Record.AddSourceLocation(D->getRParenLoc());
  Code = serialization::DECL_FILE_SCOPE_ASM;
}

void ASTDeclWriter::VisitTopLevelStmtDecl(TopLevelStmtDecl *D) {
  VisitDecl(D);
  Record.AddStmt(D->getStmt());
  Code = serialization::DECL_TOP_LEVEL_STMT_DECL;
}

void ASTDeclWriter::VisitEmptyDecl(EmptyDecl *D) {
  VisitDecl(D);
  Code = serialization::DECL_EMPTY;
}

void ASTDeclWriter::VisitLifetimeExtendedTemporaryDecl(
    LifetimeExtendedTemporaryDecl *D) {
  VisitDecl(D);
  Record.AddDeclRef(D->getExtendingDecl());
  Record.AddStmt(D->getTemporaryExpr());
  Record.push_back(static_cast<bool>(D->getValue()));
  if (D->getValue())
    Record.AddAPValue(*D->getValue());
  Record.push_back(D->getManglingNumber());
  Code = serialization::DECL_LIFETIME_EXTENDED_TEMPORARY;
}
void ASTDeclWriter::VisitBlockDecl(BlockDecl *D) {
  VisitDecl(D);
  Record.AddStmt(D->getBody());
  Record.AddTypeSourceInfo(D->getSignatureAsWritten());
  Record.push_back(D->param_size());
  for (ParmVarDecl *P : D->parameters())
    Record.AddDeclRef(P);
  Record.push_back(D->isVariadic());
  Record.push_back(D->blockMissingReturnType());
  Record.push_back(D->isConversionFromLambda());
  Record.push_back(D->doesNotEscape());
  Record.push_back(D->canAvoidCopyToHeap());
  Record.push_back(D->capturesCXXThis());
  Record.push_back(D->getNumCaptures());
  for (const auto &capture : D->captures()) {
    Record.AddDeclRef(capture.getVariable());

    unsigned flags = 0;
    if (capture.isByRef()) flags |= 1;
    if (capture.isNested()) flags |= 2;
    if (capture.hasCopyExpr()) flags |= 4;
    Record.push_back(flags);

    if (capture.hasCopyExpr()) Record.AddStmt(capture.getCopyExpr());
  }

  Code = serialization::DECL_BLOCK;
}

void ASTDeclWriter::VisitCapturedDecl(CapturedDecl *CD) {
  Record.push_back(CD->getNumParams());
  VisitDecl(CD);
  Record.push_back(CD->getContextParamPosition());
  Record.push_back(CD->isNothrow() ? 1 : 0);
  // Body is stored by VisitCapturedStmt.
  for (unsigned I = 0; I < CD->getNumParams(); ++I)
    Record.AddDeclRef(CD->getParam(I));
  Code = serialization::DECL_CAPTURED;
}

void ASTDeclWriter::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  static_assert(DeclContext::NumLinkageSpecDeclBits == 17,
                "You need to update the serializer after you change the"
                "LinkageSpecDeclBits");

  VisitDecl(D);
  Record.push_back(llvm::to_underlying(D->getLanguage()));
  Record.AddSourceLocation(D->getExternLoc());
  Record.AddSourceLocation(D->getRBraceLoc());
  Code = serialization::DECL_LINKAGE_SPEC;
}

void ASTDeclWriter::VisitExportDecl(ExportDecl *D) {
  VisitDecl(D);
  Record.AddSourceLocation(D->getRBraceLoc());
  Code = serialization::DECL_EXPORT;
}

void ASTDeclWriter::VisitLabelDecl(LabelDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Code = serialization::DECL_LABEL;
}


void ASTDeclWriter::VisitNamespaceDecl(NamespaceDecl *D) {
  VisitRedeclarable(D);
  VisitNamedDecl(D);

  BitsPacker NamespaceDeclBits;
  NamespaceDeclBits.addBit(D->isInline());
  NamespaceDeclBits.addBit(D->isNested());
  Record.push_back(NamespaceDeclBits);

  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddSourceLocation(D->getRBraceLoc());

  if (D->isFirstDecl())
    Record.AddDeclRef(D->getAnonymousNamespace());
  Code = serialization::DECL_NAMESPACE;

  if (Writer.hasChain() && D->isAnonymousNamespace() &&
      D == D->getMostRecentDecl()) {
    // This is a most recent reopening of the anonymous namespace. If its parent
    // is in a previous PCH (or is the TU), mark that parent for update, because
    // the original namespace always points to the latest re-opening of its
    // anonymous namespace.
    Decl *Parent = cast<Decl>(
        D->getParent()->getRedeclContext()->getPrimaryContext());
    if (Parent->isFromASTFile() || isa<TranslationUnitDecl>(Parent)) {
      Writer.DeclUpdates[Parent].push_back(
          ASTWriter::DeclUpdate(UPD_CXX_ADDED_ANONYMOUS_NAMESPACE, D));
    }
  }
}

void ASTDeclWriter::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  VisitRedeclarable(D);
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getNamespaceLoc());
  Record.AddSourceLocation(D->getTargetNameLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclRef(D->getNamespace());
  Code = serialization::DECL_NAMESPACE_ALIAS;
}

void ASTDeclWriter::VisitUsingDecl(UsingDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getUsingLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclarationNameLoc(D->DNLoc, D->getDeclName());
  Record.AddDeclRef(D->FirstUsingShadow.getPointer());
  Record.push_back(D->hasTypename());
  Record.AddDeclRef(Context.getInstantiatedFromUsingDecl(D));
  Code = serialization::DECL_USING;
}

void ASTDeclWriter::VisitUsingEnumDecl(UsingEnumDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getUsingLoc());
  Record.AddSourceLocation(D->getEnumLoc());
  Record.AddTypeSourceInfo(D->getEnumType());
  Record.AddDeclRef(D->FirstUsingShadow.getPointer());
  Record.AddDeclRef(Context.getInstantiatedFromUsingEnumDecl(D));
  Code = serialization::DECL_USING_ENUM;
}

void ASTDeclWriter::VisitUsingPackDecl(UsingPackDecl *D) {
  Record.push_back(D->NumExpansions);
  VisitNamedDecl(D);
  Record.AddDeclRef(D->getInstantiatedFromUsingDecl());
  for (auto *E : D->expansions())
    Record.AddDeclRef(E);
  Code = serialization::DECL_USING_PACK;
}

void ASTDeclWriter::VisitUsingShadowDecl(UsingShadowDecl *D) {
  VisitRedeclarable(D);
  VisitNamedDecl(D);
  Record.AddDeclRef(D->getTargetDecl());
  Record.push_back(D->getIdentifierNamespace());
  Record.AddDeclRef(D->UsingOrNextShadow);
  Record.AddDeclRef(Context.getInstantiatedFromUsingShadowDecl(D));

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      D->getFirstDecl() == D->getMostRecentDecl() && !D->hasAttrs() &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclUsingShadowAbbrev();

  Code = serialization::DECL_USING_SHADOW;
}

void ASTDeclWriter::VisitConstructorUsingShadowDecl(
    ConstructorUsingShadowDecl *D) {
  VisitUsingShadowDecl(D);
  Record.AddDeclRef(D->NominatedBaseClassShadowDecl);
  Record.AddDeclRef(D->ConstructedBaseClassShadowDecl);
  Record.push_back(D->IsVirtual);
  Code = serialization::DECL_CONSTRUCTOR_USING_SHADOW;
}

void ASTDeclWriter::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getUsingLoc());
  Record.AddSourceLocation(D->getNamespaceKeyLocation());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclRef(D->getNominatedNamespace());
  Record.AddDeclRef(dyn_cast<Decl>(D->getCommonAncestor()));
  Code = serialization::DECL_USING_DIRECTIVE;
}

void ASTDeclWriter::VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
  VisitValueDecl(D);
  Record.AddSourceLocation(D->getUsingLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclarationNameLoc(D->DNLoc, D->getDeclName());
  Record.AddSourceLocation(D->getEllipsisLoc());
  Code = serialization::DECL_UNRESOLVED_USING_VALUE;
}

void ASTDeclWriter::VisitUnresolvedUsingTypenameDecl(
                                               UnresolvedUsingTypenameDecl *D) {
  VisitTypeDecl(D);
  Record.AddSourceLocation(D->getTypenameLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddSourceLocation(D->getEllipsisLoc());
  Code = serialization::DECL_UNRESOLVED_USING_TYPENAME;
}

void ASTDeclWriter::VisitUnresolvedUsingIfExistsDecl(
    UnresolvedUsingIfExistsDecl *D) {
  VisitNamedDecl(D);
  Code = serialization::DECL_UNRESOLVED_USING_IF_EXISTS;
}

void ASTDeclWriter::VisitCXXRecordDecl(CXXRecordDecl *D) {
  VisitRecordDecl(D);

  enum {
    CXXRecNotTemplate = 0,
    CXXRecTemplate,
    CXXRecMemberSpecialization,
    CXXLambda
  };
  if (ClassTemplateDecl *TemplD = D->getDescribedClassTemplate()) {
    Record.push_back(CXXRecTemplate);
    Record.AddDeclRef(TemplD);
  } else if (MemberSpecializationInfo *MSInfo
               = D->getMemberSpecializationInfo()) {
    Record.push_back(CXXRecMemberSpecialization);
    Record.AddDeclRef(MSInfo->getInstantiatedFrom());
    Record.push_back(MSInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(MSInfo->getPointOfInstantiation());
  } else if (D->isLambda()) {
    // For a lambda, we need some information early for merging.
    Record.push_back(CXXLambda);
    if (auto *Context = D->getLambdaContextDecl()) {
      Record.AddDeclRef(Context);
      Record.push_back(D->getLambdaIndexInContext());
    } else {
      Record.push_back(0);
    }
  } else {
    Record.push_back(CXXRecNotTemplate);
  }

  Record.push_back(D->isThisDeclarationADefinition());
  if (D->isThisDeclarationADefinition())
    Record.AddCXXDefinitionData(D);

  if (D->isCompleteDefinition() && D->isInNamedModule())
    Writer.AddDeclRef(D, Writer.ModularCodegenDecls);

  // Store (what we currently believe to be) the key function to avoid
  // deserializing every method so we can compute it.
  //
  // FIXME: Avoid adding the key function if the class is defined in
  // module purview since in that case the key function is meaningless.
  if (D->isCompleteDefinition())
    Record.AddDeclRef(Context.getCurrentKeyFunction(D));

  Code = serialization::DECL_CXX_RECORD;
}

void ASTDeclWriter::VisitCXXMethodDecl(CXXMethodDecl *D) {
  VisitFunctionDecl(D);
  if (D->isCanonicalDecl()) {
    Record.push_back(D->size_overridden_methods());
    for (const CXXMethodDecl *MD : D->overridden_methods())
      Record.AddDeclRef(MD);
  } else {
    // We only need to record overridden methods once for the canonical decl.
    Record.push_back(0);
  }

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      D->getFirstDecl() == D->getMostRecentDecl() && !D->isInvalidDecl() &&
      !D->hasAttrs() && !D->isTopLevelDeclInObjCContainer() &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier &&
      !D->hasExtInfo() && !D->isExplicitlyDefaulted()) {
    if (D->getTemplatedKind() == FunctionDecl::TK_NonTemplate ||
        D->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate ||
        D->getTemplatedKind() == FunctionDecl::TK_MemberSpecialization ||
        D->getTemplatedKind() == FunctionDecl::TK_DependentNonTemplate)
      AbbrevToUse = Writer.getDeclCXXMethodAbbrev(D->getTemplatedKind());
    else if (D->getTemplatedKind() ==
             FunctionDecl::TK_FunctionTemplateSpecialization) {
      FunctionTemplateSpecializationInfo *FTSInfo =
          D->getTemplateSpecializationInfo();

      if (FTSInfo->TemplateArguments->size() == 1) {
        const TemplateArgument &TA = FTSInfo->TemplateArguments->get(0);
        if (TA.getKind() == TemplateArgument::Type &&
            !FTSInfo->TemplateArgumentsAsWritten &&
            !FTSInfo->getMemberSpecializationInfo())
          AbbrevToUse = Writer.getDeclCXXMethodAbbrev(D->getTemplatedKind());
      }
    } else if (D->getTemplatedKind() ==
               FunctionDecl::TK_DependentFunctionTemplateSpecialization) {
      DependentFunctionTemplateSpecializationInfo *DFTSInfo =
          D->getDependentSpecializationInfo();
      if (!DFTSInfo->TemplateArgumentsAsWritten)
        AbbrevToUse = Writer.getDeclCXXMethodAbbrev(D->getTemplatedKind());
    }
  }

  Code = serialization::DECL_CXX_METHOD;
}

void ASTDeclWriter::VisitCXXConstructorDecl(CXXConstructorDecl *D) {
  static_assert(DeclContext::NumCXXConstructorDeclBits == 64,
                "You need to update the serializer after you change the "
                "CXXConstructorDeclBits");

  Record.push_back(D->getTrailingAllocKind());
  addExplicitSpecifier(D->getExplicitSpecifier(), Record);
  if (auto Inherited = D->getInheritedConstructor()) {
    Record.AddDeclRef(Inherited.getShadowDecl());
    Record.AddDeclRef(Inherited.getConstructor());
  }

  VisitCXXMethodDecl(D);
  Code = serialization::DECL_CXX_CONSTRUCTOR;
}

void ASTDeclWriter::VisitCXXDestructorDecl(CXXDestructorDecl *D) {
  VisitCXXMethodDecl(D);

  Record.AddDeclRef(D->getOperatorDelete());
  if (D->getOperatorDelete())
    Record.AddStmt(D->getOperatorDeleteThisArg());

  Code = serialization::DECL_CXX_DESTRUCTOR;
}

void ASTDeclWriter::VisitCXXConversionDecl(CXXConversionDecl *D) {
  addExplicitSpecifier(D->getExplicitSpecifier(), Record);
  VisitCXXMethodDecl(D);
  Code = serialization::DECL_CXX_CONVERSION;
}

void ASTDeclWriter::VisitImportDecl(ImportDecl *D) {
  VisitDecl(D);
  Record.push_back(Writer.getSubmoduleID(D->getImportedModule()));
  ArrayRef<SourceLocation> IdentifierLocs = D->getIdentifierLocs();
  Record.push_back(!IdentifierLocs.empty());
  if (IdentifierLocs.empty()) {
    Record.AddSourceLocation(D->getEndLoc());
    Record.push_back(1);
  } else {
    for (unsigned I = 0, N = IdentifierLocs.size(); I != N; ++I)
      Record.AddSourceLocation(IdentifierLocs[I]);
    Record.push_back(IdentifierLocs.size());
  }
  // Note: the number of source locations must always be the last element in
  // the record.
  Code = serialization::DECL_IMPORT;
}

void ASTDeclWriter::VisitAccessSpecDecl(AccessSpecDecl *D) {
  VisitDecl(D);
  Record.AddSourceLocation(D->getColonLoc());
  Code = serialization::DECL_ACCESS_SPEC;
}

void ASTDeclWriter::VisitFriendDecl(FriendDecl *D) {
  // Record the number of friend type template parameter lists here
  // so as to simplify memory allocation during deserialization.
  Record.push_back(D->NumTPLists);
  VisitDecl(D);
  bool hasFriendDecl = D->Friend.is<NamedDecl*>();
  Record.push_back(hasFriendDecl);
  if (hasFriendDecl)
    Record.AddDeclRef(D->getFriendDecl());
  else
    Record.AddTypeSourceInfo(D->getFriendType());
  for (unsigned i = 0; i < D->NumTPLists; ++i)
    Record.AddTemplateParameterList(D->getFriendTypeTemplateParameterList(i));
  Record.AddDeclRef(D->getNextFriend());
  Record.push_back(D->UnsupportedFriend);
  Record.AddSourceLocation(D->FriendLoc);
  Code = serialization::DECL_FRIEND;
}

void ASTDeclWriter::VisitFriendTemplateDecl(FriendTemplateDecl *D) {
  VisitDecl(D);
  Record.push_back(D->getNumTemplateParameters());
  for (unsigned i = 0, e = D->getNumTemplateParameters(); i != e; ++i)
    Record.AddTemplateParameterList(D->getTemplateParameterList(i));
  Record.push_back(D->getFriendDecl() != nullptr);
  if (D->getFriendDecl())
    Record.AddDeclRef(D->getFriendDecl());
  else
    Record.AddTypeSourceInfo(D->getFriendType());
  Record.AddSourceLocation(D->getFriendLoc());
  Code = serialization::DECL_FRIEND_TEMPLATE;
}

void ASTDeclWriter::VisitTemplateDecl(TemplateDecl *D) {
  VisitNamedDecl(D);

  Record.AddTemplateParameterList(D->getTemplateParameters());
  Record.AddDeclRef(D->getTemplatedDecl());
}

void ASTDeclWriter::VisitConceptDecl(ConceptDecl *D) {
  VisitTemplateDecl(D);
  Record.AddStmt(D->getConstraintExpr());
  Code = serialization::DECL_CONCEPT;
}

void ASTDeclWriter::VisitImplicitConceptSpecializationDecl(
    ImplicitConceptSpecializationDecl *D) {
  Record.push_back(D->getTemplateArguments().size());
  VisitDecl(D);
  for (const TemplateArgument &Arg : D->getTemplateArguments())
    Record.AddTemplateArgument(Arg);
  Code = serialization::DECL_IMPLICIT_CONCEPT_SPECIALIZATION;
}

void ASTDeclWriter::VisitRequiresExprBodyDecl(RequiresExprBodyDecl *D) {
  Code = serialization::DECL_REQUIRES_EXPR_BODY;
}

void ASTDeclWriter::VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D) {
  VisitRedeclarable(D);

  // Emit data to initialize CommonOrPrev before VisitTemplateDecl so that
  // getCommonPtr() can be used while this is still initializing.
  if (D->isFirstDecl()) {
    // This declaration owns the 'common' pointer, so serialize that data now.
    Record.AddDeclRef(D->getInstantiatedFromMemberTemplate());
    if (D->getInstantiatedFromMemberTemplate())
      Record.push_back(D->isMemberSpecialization());
  }

  VisitTemplateDecl(D);
  Record.push_back(D->getIdentifierNamespace());
}

void ASTDeclWriter::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->isFirstDecl())
    AddTemplateSpecializations(D);

  // Force emitting the corresponding deduction guide in reduced BMI mode.
  // Otherwise, the deduction guide may be optimized out incorrectly.
  if (Writer.isGeneratingReducedBMI()) {
    auto Name = Context.DeclarationNames.getCXXDeductionGuideName(D);
    for (auto *DG : D->getDeclContext()->noload_lookup(Name))
      Writer.GetDeclRef(DG->getCanonicalDecl());
  }

  Code = serialization::DECL_CLASS_TEMPLATE;
}

void ASTDeclWriter::VisitClassTemplateSpecializationDecl(
                                           ClassTemplateSpecializationDecl *D) {
  RegisterTemplateSpecialization(D->getSpecializedTemplate(), D);

  VisitCXXRecordDecl(D);

  llvm::PointerUnion<ClassTemplateDecl *,
                     ClassTemplatePartialSpecializationDecl *> InstFrom
    = D->getSpecializedTemplateOrPartial();
  if (Decl *InstFromD = InstFrom.dyn_cast<ClassTemplateDecl *>()) {
    Record.AddDeclRef(InstFromD);
  } else {
    Record.AddDeclRef(InstFrom.get<ClassTemplatePartialSpecializationDecl *>());
    Record.AddTemplateArgumentList(&D->getTemplateInstantiationArgs());
  }

  Record.AddTemplateArgumentList(&D->getTemplateArgs());
  Record.AddSourceLocation(D->getPointOfInstantiation());
  Record.push_back(D->getSpecializationKind());
  Record.push_back(D->isCanonicalDecl());

  if (D->isCanonicalDecl()) {
    // When reading, we'll add it to the folding set of the following template.
    Record.AddDeclRef(D->getSpecializedTemplate()->getCanonicalDecl());
  }

  bool ExplicitInstantiation =
      D->getTemplateSpecializationKind() ==
          TSK_ExplicitInstantiationDeclaration ||
      D->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDefinition;
  Record.push_back(ExplicitInstantiation);
  if (ExplicitInstantiation) {
    Record.AddSourceLocation(D->getExternKeywordLoc());
    Record.AddSourceLocation(D->getTemplateKeywordLoc());
  }

  const ASTTemplateArgumentListInfo *ArgsWritten =
      D->getTemplateArgsAsWritten();
  Record.push_back(!!ArgsWritten);
  if (ArgsWritten)
    Record.AddASTTemplateArgumentListInfo(ArgsWritten);

  Code = serialization::DECL_CLASS_TEMPLATE_SPECIALIZATION;
}

void ASTDeclWriter::VisitClassTemplatePartialSpecializationDecl(
                                    ClassTemplatePartialSpecializationDecl *D) {
  Record.AddTemplateParameterList(D->getTemplateParameters());

  VisitClassTemplateSpecializationDecl(D);

  // These are read/set from/to the first declaration.
  if (D->getPreviousDecl() == nullptr) {
    Record.AddDeclRef(D->getInstantiatedFromMember());
    Record.push_back(D->isMemberSpecialization());
  }

  Code = serialization::DECL_CLASS_TEMPLATE_PARTIAL_SPECIALIZATION;
}

void ASTDeclWriter::VisitVarTemplateDecl(VarTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->isFirstDecl())
    AddTemplateSpecializations(D);
  Code = serialization::DECL_VAR_TEMPLATE;
}

void ASTDeclWriter::VisitVarTemplateSpecializationDecl(
    VarTemplateSpecializationDecl *D) {
  RegisterTemplateSpecialization(D->getSpecializedTemplate(), D);

  llvm::PointerUnion<VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>
  InstFrom = D->getSpecializedTemplateOrPartial();
  if (Decl *InstFromD = InstFrom.dyn_cast<VarTemplateDecl *>()) {
    Record.AddDeclRef(InstFromD);
  } else {
    Record.AddDeclRef(InstFrom.get<VarTemplatePartialSpecializationDecl *>());
    Record.AddTemplateArgumentList(&D->getTemplateInstantiationArgs());
  }

  bool ExplicitInstantiation =
      D->getTemplateSpecializationKind() ==
          TSK_ExplicitInstantiationDeclaration ||
      D->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDefinition;
  Record.push_back(ExplicitInstantiation);
  if (ExplicitInstantiation) {
    Record.AddSourceLocation(D->getExternKeywordLoc());
    Record.AddSourceLocation(D->getTemplateKeywordLoc());
  }

  const ASTTemplateArgumentListInfo *ArgsWritten =
      D->getTemplateArgsAsWritten();
  Record.push_back(!!ArgsWritten);
  if (ArgsWritten)
    Record.AddASTTemplateArgumentListInfo(ArgsWritten);

  Record.AddTemplateArgumentList(&D->getTemplateArgs());
  Record.AddSourceLocation(D->getPointOfInstantiation());
  Record.push_back(D->getSpecializationKind());
  Record.push_back(D->IsCompleteDefinition);

  VisitVarDecl(D);

  Record.push_back(D->isCanonicalDecl());

  if (D->isCanonicalDecl()) {
    // When reading, we'll add it to the folding set of the following template.
    Record.AddDeclRef(D->getSpecializedTemplate()->getCanonicalDecl());
  }

  Code = serialization::DECL_VAR_TEMPLATE_SPECIALIZATION;
}

void ASTDeclWriter::VisitVarTemplatePartialSpecializationDecl(
    VarTemplatePartialSpecializationDecl *D) {
  Record.AddTemplateParameterList(D->getTemplateParameters());

  VisitVarTemplateSpecializationDecl(D);

  // These are read/set from/to the first declaration.
  if (D->getPreviousDecl() == nullptr) {
    Record.AddDeclRef(D->getInstantiatedFromMember());
    Record.push_back(D->isMemberSpecialization());
  }

  Code = serialization::DECL_VAR_TEMPLATE_PARTIAL_SPECIALIZATION;
}

void ASTDeclWriter::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->isFirstDecl())
    AddTemplateSpecializations(D);
  Code = serialization::DECL_FUNCTION_TEMPLATE;
}

void ASTDeclWriter::VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D) {
  Record.push_back(D->hasTypeConstraint());
  VisitTypeDecl(D);

  Record.push_back(D->wasDeclaredWithTypename());

  const TypeConstraint *TC = D->getTypeConstraint();
  Record.push_back(/*TypeConstraintInitialized=*/TC != nullptr);
  if (TC) {
    auto *CR = TC->getConceptReference();
    Record.push_back(CR != nullptr);
    if (CR)
      Record.AddConceptReference(CR);
    Record.AddStmt(TC->getImmediatelyDeclaredConstraint());
    Record.push_back(D->isExpandedParameterPack());
    if (D->isExpandedParameterPack())
      Record.push_back(D->getNumExpansionParameters());
  }

  bool OwnsDefaultArg = D->hasDefaultArgument() &&
                        !D->defaultArgumentWasInherited();
  Record.push_back(OwnsDefaultArg);
  if (OwnsDefaultArg)
    Record.AddTemplateArgumentLoc(D->getDefaultArgument());

  if (!D->hasTypeConstraint() && !OwnsDefaultArg &&
      D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->isInvalidDecl() && !D->hasAttrs() &&
      !D->isTopLevelDeclInObjCContainer() && !D->isImplicit() &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclTemplateTypeParmAbbrev();

  Code = serialization::DECL_TEMPLATE_TYPE_PARM;
}

void ASTDeclWriter::VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D) {
  // For an expanded parameter pack, record the number of expansion types here
  // so that it's easier for deserialization to allocate the right amount of
  // memory.
  Expr *TypeConstraint = D->getPlaceholderTypeConstraint();
  Record.push_back(!!TypeConstraint);
  if (D->isExpandedParameterPack())
    Record.push_back(D->getNumExpansionTypes());

  VisitDeclaratorDecl(D);
  // TemplateParmPosition.
  Record.push_back(D->getDepth());
  Record.push_back(D->getPosition());
  if (TypeConstraint)
    Record.AddStmt(TypeConstraint);

  if (D->isExpandedParameterPack()) {
    for (unsigned I = 0, N = D->getNumExpansionTypes(); I != N; ++I) {
      Record.AddTypeRef(D->getExpansionType(I));
      Record.AddTypeSourceInfo(D->getExpansionTypeSourceInfo(I));
    }

    Code = serialization::DECL_EXPANDED_NON_TYPE_TEMPLATE_PARM_PACK;
  } else {
    // Rest of NonTypeTemplateParmDecl.
    Record.push_back(D->isParameterPack());
    bool OwnsDefaultArg = D->hasDefaultArgument() &&
                          !D->defaultArgumentWasInherited();
    Record.push_back(OwnsDefaultArg);
    if (OwnsDefaultArg)
      Record.AddTemplateArgumentLoc(D->getDefaultArgument());
    Code = serialization::DECL_NON_TYPE_TEMPLATE_PARM;
  }
}

void ASTDeclWriter::VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D) {
  // For an expanded parameter pack, record the number of expansion types here
  // so that it's easier for deserialization to allocate the right amount of
  // memory.
  if (D->isExpandedParameterPack())
    Record.push_back(D->getNumExpansionTemplateParameters());

  VisitTemplateDecl(D);
  Record.push_back(D->wasDeclaredWithTypename());
  // TemplateParmPosition.
  Record.push_back(D->getDepth());
  Record.push_back(D->getPosition());

  if (D->isExpandedParameterPack()) {
    for (unsigned I = 0, N = D->getNumExpansionTemplateParameters();
         I != N; ++I)
      Record.AddTemplateParameterList(D->getExpansionTemplateParameters(I));
    Code = serialization::DECL_EXPANDED_TEMPLATE_TEMPLATE_PARM_PACK;
  } else {
    // Rest of TemplateTemplateParmDecl.
    Record.push_back(D->isParameterPack());
    bool OwnsDefaultArg = D->hasDefaultArgument() &&
                          !D->defaultArgumentWasInherited();
    Record.push_back(OwnsDefaultArg);
    if (OwnsDefaultArg)
      Record.AddTemplateArgumentLoc(D->getDefaultArgument());
    Code = serialization::DECL_TEMPLATE_TEMPLATE_PARM;
  }
}

void ASTDeclWriter::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);
  Code = serialization::DECL_TYPE_ALIAS_TEMPLATE;
}

void ASTDeclWriter::VisitStaticAssertDecl(StaticAssertDecl *D) {
  VisitDecl(D);
  Record.AddStmt(D->getAssertExpr());
  Record.push_back(D->isFailed());
  Record.AddStmt(D->getMessage());
  Record.AddSourceLocation(D->getRParenLoc());
  Code = serialization::DECL_STATIC_ASSERT;
}

/// Emit the DeclContext part of a declaration context decl.
void ASTDeclWriter::VisitDeclContext(DeclContext *DC) {
  static_assert(DeclContext::NumDeclContextBits == 13,
                "You need to update the serializer after you change the "
                "DeclContextBits");

  uint64_t LexicalOffset = 0;
  uint64_t VisibleOffset = 0;

  if (Writer.isGeneratingReducedBMI() && isa<NamespaceDecl>(DC) &&
      cast<NamespaceDecl>(DC)->isFromExplicitGlobalModule()) {
    // In reduced BMI, delay writing lexical and visible block for namespace
    // in the global module fragment. See the comments of DelayedNamespace for
    // details.
    Writer.DelayedNamespace.push_back(cast<NamespaceDecl>(DC));
  } else {
    LexicalOffset = Writer.WriteDeclContextLexicalBlock(Context, DC);
    VisibleOffset = Writer.WriteDeclContextVisibleBlock(Context, DC);
  }

  Record.AddOffset(LexicalOffset);
  Record.AddOffset(VisibleOffset);
}

const Decl *ASTWriter::getFirstLocalDecl(const Decl *D) {
  assert(IsLocalDecl(D) && "expected a local declaration");

  const Decl *Canon = D->getCanonicalDecl();
  if (IsLocalDecl(Canon))
    return Canon;

  const Decl *&CacheEntry = FirstLocalDeclCache[Canon];
  if (CacheEntry)
    return CacheEntry;

  for (const Decl *Redecl = D; Redecl; Redecl = Redecl->getPreviousDecl())
    if (IsLocalDecl(Redecl))
      D = Redecl;
  return CacheEntry = D;
}

template <typename T>
void ASTDeclWriter::VisitRedeclarable(Redeclarable<T> *D) {
  T *First = D->getFirstDecl();
  T *MostRecent = First->getMostRecentDecl();
  T *DAsT = static_cast<T *>(D);
  if (MostRecent != First) {
    assert(isRedeclarableDeclKind(DAsT->getKind()) &&
           "Not considered redeclarable?");

    Record.AddDeclRef(First);

    // Write out a list of local redeclarations of this declaration if it's the
    // first local declaration in the chain.
    const Decl *FirstLocal = Writer.getFirstLocalDecl(DAsT);
    if (DAsT == FirstLocal) {
      // Emit a list of all imported first declarations so that we can be sure
      // that all redeclarations visible to this module are before D in the
      // redecl chain.
      unsigned I = Record.size();
      Record.push_back(0);
      if (Writer.Chain)
        AddFirstDeclFromEachModule(DAsT, /*IncludeLocal*/false);
      // This is the number of imported first declarations + 1.
      Record[I] = Record.size() - I;

      // Collect the set of local redeclarations of this declaration, from
      // newest to oldest.
      ASTWriter::RecordData LocalRedecls;
      ASTRecordWriter LocalRedeclWriter(Record, LocalRedecls);
      for (const Decl *Prev = FirstLocal->getMostRecentDecl();
           Prev != FirstLocal; Prev = Prev->getPreviousDecl())
        if (!Prev->isFromASTFile())
          LocalRedeclWriter.AddDeclRef(Prev);

      // If we have any redecls, write them now as a separate record preceding
      // the declaration itself.
      if (LocalRedecls.empty())
        Record.push_back(0);
      else
        Record.AddOffset(LocalRedeclWriter.Emit(LOCAL_REDECLARATIONS));
    } else {
      Record.push_back(0);
      Record.AddDeclRef(FirstLocal);
    }

    // Make sure that we serialize both the previous and the most-recent
    // declarations, which (transitively) ensures that all declarations in the
    // chain get serialized.
    //
    // FIXME: This is not correct; when we reach an imported declaration we
    // won't emit its previous declaration.
    (void)Writer.GetDeclRef(D->getPreviousDecl());
    (void)Writer.GetDeclRef(MostRecent);
  } else {
    // We use the sentinel value 0 to indicate an only declaration.
    Record.push_back(0);
  }
}

void ASTDeclWriter::VisitHLSLBufferDecl(HLSLBufferDecl *D) {
  VisitNamedDecl(D);
  VisitDeclContext(D);
  Record.push_back(D->isCBuffer());
  Record.AddSourceLocation(D->getLocStart());
  Record.AddSourceLocation(D->getLBraceLoc());
  Record.AddSourceLocation(D->getRBraceLoc());

  Code = serialization::DECL_HLSL_BUFFER;
}

void ASTDeclWriter::VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D) {
  Record.writeOMPChildren(D->Data);
  VisitDecl(D);
  Code = serialization::DECL_OMP_THREADPRIVATE;
}

void ASTDeclWriter::VisitOMPAllocateDecl(OMPAllocateDecl *D) {
  Record.writeOMPChildren(D->Data);
  VisitDecl(D);
  Code = serialization::DECL_OMP_ALLOCATE;
}

void ASTDeclWriter::VisitOMPRequiresDecl(OMPRequiresDecl *D) {
  Record.writeOMPChildren(D->Data);
  VisitDecl(D);
  Code = serialization::DECL_OMP_REQUIRES;
}

void ASTDeclWriter::VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D) {
  static_assert(DeclContext::NumOMPDeclareReductionDeclBits == 15,
                "You need to update the serializer after you change the "
                "NumOMPDeclareReductionDeclBits");

  VisitValueDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddStmt(D->getCombinerIn());
  Record.AddStmt(D->getCombinerOut());
  Record.AddStmt(D->getCombiner());
  Record.AddStmt(D->getInitOrig());
  Record.AddStmt(D->getInitPriv());
  Record.AddStmt(D->getInitializer());
  Record.push_back(llvm::to_underlying(D->getInitializerKind()));
  Record.AddDeclRef(D->getPrevDeclInScope());
  Code = serialization::DECL_OMP_DECLARE_REDUCTION;
}

void ASTDeclWriter::VisitOMPDeclareMapperDecl(OMPDeclareMapperDecl *D) {
  Record.writeOMPChildren(D->Data);
  VisitValueDecl(D);
  Record.AddDeclarationName(D->getVarName());
  Record.AddDeclRef(D->getPrevDeclInScope());
  Code = serialization::DECL_OMP_DECLARE_MAPPER;
}

void ASTDeclWriter::VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D) {
  VisitVarDecl(D);
  Code = serialization::DECL_OMP_CAPTUREDEXPR;
}

//===----------------------------------------------------------------------===//
// ASTWriter Implementation
//===----------------------------------------------------------------------===//

namespace {
template <FunctionDecl::TemplatedKind Kind>
std::shared_ptr<llvm::BitCodeAbbrev>
getFunctionDeclAbbrev(serialization::DeclCode Code) {
  using namespace llvm;

  auto Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(Code));
  // RedeclarableDecl
  Abv->Add(BitCodeAbbrevOp(0)); // CanonicalDecl
  Abv->Add(BitCodeAbbrevOp(Kind));
  if constexpr (Kind == FunctionDecl::TK_NonTemplate) {

  } else if constexpr (Kind == FunctionDecl::TK_FunctionTemplate) {
    // DescribedFunctionTemplate
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  } else if constexpr (Kind == FunctionDecl::TK_DependentNonTemplate) {
    // Instantiated From Decl
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  } else if constexpr (Kind == FunctionDecl::TK_MemberSpecialization) {
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InstantiatedFrom
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                             3)); // TemplateSpecializationKind
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Specialized Location
  } else if constexpr (Kind ==
                       FunctionDecl::TK_FunctionTemplateSpecialization) {
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Template
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                             3)); // TemplateSpecializationKind
    Abv->Add(BitCodeAbbrevOp(1)); // Template Argument Size
    Abv->Add(BitCodeAbbrevOp(TemplateArgument::Type)); // Template Argument Kind
    Abv->Add(
        BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Template Argument Type
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Is Defaulted
    Abv->Add(BitCodeAbbrevOp(0)); // TemplateArgumentsAsWritten
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SourceLocation
    Abv->Add(BitCodeAbbrevOp(0));
    Abv->Add(
        BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Canonical Decl of template
  } else if constexpr (Kind == FunctionDecl::
                                   TK_DependentFunctionTemplateSpecialization) {
    // Candidates of specialization
    Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abv->Add(BitCodeAbbrevOp(0)); // TemplateArgumentsAsWritten
  } else {
    llvm_unreachable("Unknown templated kind?");
  }
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           8)); // Packed DeclBits: ModuleOwnershipKind,
                                // isUsed, isReferenced,  AccessSpecifier,
                                // isImplicit
                                //
                                // The following bits should be 0:
                                // HasStandaloneLexicalDC, HasAttrs,
                                // TopLevelDeclInObjCContainer,
                                // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(DeclarationName::Identifier)); // NameKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));     // Identifier
  Abv->Add(BitCodeAbbrevOp(0));                           // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerLocStart
  Abv->Add(BitCodeAbbrevOp(0));                       // HasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // FunctionDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 11)); // IDNS
  Abv->Add(BitCodeAbbrevOp(
      BitCodeAbbrevOp::Fixed,
      28)); // Packed Function Bits: StorageClass, Inline, InlineSpecified,
            // VirtualAsWritten, Pure, HasInheritedProto, HasWrittenProto,
            // Deleted, Trivial, TrivialForCall, Defaulted, ExplicitlyDefaulted,
            // IsIneligibleOrNotSelected, ImplicitReturnZero, Constexpr,
            // UsesSEHTry, SkippedBody, MultiVersion, LateParsed,
            // FriendConstraintRefersToEnclosingTemplate, Linkage,
            // ShouldSkipCheckingODR
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));    // LocEnd
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // ODRHash
  // This Array slurps the rest of the record. Fortunately we want to encode
  // (nearly) all the remaining (variable number of) fields in the same way.
  //
  // This is:
  //         NumParams and Params[] from FunctionDecl, and
  //         NumOverriddenMethods, OverriddenMethods[] from CXXMethodDecl.
  //
  //  Add an AbbrevOp for 'size then elements' and use it here.
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  return Abv;
}

template <FunctionDecl::TemplatedKind Kind>
std::shared_ptr<llvm::BitCodeAbbrev> getCXXMethodAbbrev() {
  return getFunctionDeclAbbrev<Kind>(serialization::DECL_CXX_METHOD);
}
} // namespace

void ASTWriter::WriteDeclAbbrevs() {
  using namespace llvm;

  std::shared_ptr<BitCodeAbbrev> Abv;

  // Abbreviation for DECL_FIELD
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_FIELD));
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           7)); // Packed DeclBits: ModuleOwnershipKind,
                                // isUsed, isReferenced,  AccessSpecifier,
                                //
                                // The following bits should be 0:
                                // isImplicit, HasStandaloneLexicalDC, HasAttrs,
                                // TopLevelDeclInObjCContainer,
                                // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // FieldDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isMutable
  Abv->Add(BitCodeAbbrevOp(0));                       // StorageKind
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclFieldAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_OBJC_IVAR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_OBJC_IVAR));
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           12)); // Packed DeclBits: HasStandaloneLexicalDC,
                                 // isInvalidDecl, HasAttrs, isImplicit, isUsed,
                                 // isReferenced, TopLevelDeclInObjCContainer,
                                 // AccessSpecifier, ModuleOwnershipKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // FieldDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isMutable
  Abv->Add(BitCodeAbbrevOp(0));                       // InitStyle
  // ObjC Ivar
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // getAccessControl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // getSynthesize
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclObjCIvarAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_ENUM
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_ENUM));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           7)); // Packed DeclBits: ModuleOwnershipKind,
                                // isUsed, isReferenced,  AccessSpecifier,
                                //
                                // The following bits should be 0:
                                // isImplicit, HasStandaloneLexicalDC, HasAttrs,
                                // TopLevelDeclInObjCContainer,
                                // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // TypeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type Ref
  // TagDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // IdentifierNamespace
  Abv->Add(BitCodeAbbrevOp(
      BitCodeAbbrevOp::Fixed,
      9)); // Packed Tag Decl Bits: getTagKind, isCompleteDefinition,
           // EmbeddedInDeclarator, IsFreeStanding,
           // isCompleteDefinitionRequired, ExtInfoKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  // EnumDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // AddTypeRef
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // IntegerType
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // getPromotionType
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 20)); // Enum Decl Bits
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));// ODRHash
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // InstantiatedMembEnum
  // DC
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // LexicalOffset
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // VisibleOffset
  DeclEnumAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_RECORD
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_RECORD));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           7)); // Packed DeclBits: ModuleOwnershipKind,
                                // isUsed, isReferenced,  AccessSpecifier,
                                //
                                // The following bits should be 0:
                                // isImplicit, HasStandaloneLexicalDC, HasAttrs,
                                // TopLevelDeclInObjCContainer,
                                // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // TypeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type Ref
  // TagDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // IdentifierNamespace
  Abv->Add(BitCodeAbbrevOp(
      BitCodeAbbrevOp::Fixed,
      9)); // Packed Tag Decl Bits: getTagKind, isCompleteDefinition,
           // EmbeddedInDeclarator, IsFreeStanding,
           // isCompleteDefinitionRequired, ExtInfoKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  // RecordDecl
  Abv->Add(BitCodeAbbrevOp(
      BitCodeAbbrevOp::Fixed,
      13)); // Packed Record Decl Bits: FlexibleArrayMember,
            // AnonymousStructUnion, hasObjectMember, hasVolatileMember,
            // isNonTrivialToPrimitiveDefaultInitialize,
            // isNonTrivialToPrimitiveCopy, isNonTrivialToPrimitiveDestroy,
            // hasNonTrivialToPrimitiveDefaultInitializeCUnion,
            // hasNonTrivialToPrimitiveDestructCUnion,
            // hasNonTrivialToPrimitiveCopyCUnion, isParamDestroyedInCallee,
            // getArgPassingRestrictions
  // ODRHash
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 26));

  // DC
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // LexicalOffset
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // VisibleOffset
  DeclRecordAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_PARM_VAR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_PARM_VAR));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           8)); // Packed DeclBits: ModuleOwnershipKind, isUsed,
                                // isReferenced, AccessSpecifier,
                                // HasStandaloneLexicalDC, HasAttrs, isImplicit,
                                // TopLevelDeclInObjCContainer,
                                // isInvalidDecl,
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // VarDecl
  Abv->Add(
      BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                      12)); // Packed Var Decl bits: SClass, TSCSpec, InitStyle,
                            // isARCPseudoStrong, Linkage, ModulesCodegen
  Abv->Add(BitCodeAbbrevOp(0));                          // VarKind (local enum)
  // ParmVarDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ScopeIndex
  Abv->Add(BitCodeAbbrevOp(
      BitCodeAbbrevOp::Fixed,
      19)); // Packed Parm Var Decl bits: IsObjCMethodParameter, ScopeDepth,
            // ObjCDeclQualifier, KNRPromoted,
            // HasInheritedDefaultArg, HasUninstantiatedDefaultArg
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclParmVarAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_TYPEDEF
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_TYPEDEF));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           7)); // Packed DeclBits: ModuleOwnershipKind,
                                // isReferenced, isUsed, AccessSpecifier. Other
                                // higher bits should be 0: isImplicit,
                                // HasStandaloneLexicalDC, HasAttrs,
                                // TopLevelDeclInObjCContainer, isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // TypeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type Ref
  // TypedefDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclTypedefAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_VAR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_VAR));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           12)); // Packed DeclBits: HasStandaloneLexicalDC,
                                 // isInvalidDecl, HasAttrs, isImplicit, isUsed,
                                 // isReferenced, TopLevelDeclInObjCContainer,
                                 // AccessSpecifier, ModuleOwnershipKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // VarDecl
  Abv->Add(BitCodeAbbrevOp(
      BitCodeAbbrevOp::Fixed,
      21)); // Packed Var Decl bits:  Linkage, ModulesCodegen,
            // SClass, TSCSpec, InitStyle,
            // isARCPseudoStrong, IsThisDeclarationADemotedDefinition,
            // isExceptionVariable, isNRVOVariable, isCXXForRangeDecl,
            // isInline, isInlineSpecified, isConstexpr,
            // isInitCapture, isPrevDeclInSameScope,
            // EscapingByref, HasDeducedType, ImplicitParamKind, isObjCForDecl
  Abv->Add(BitCodeAbbrevOp(0));                         // VarKind (local enum)
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclVarAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_CXX_METHOD
  DeclCXXMethodAbbrev =
      Stream.EmitAbbrev(getCXXMethodAbbrev<FunctionDecl::TK_NonTemplate>());
  DeclTemplateCXXMethodAbbrev = Stream.EmitAbbrev(
      getCXXMethodAbbrev<FunctionDecl::TK_FunctionTemplate>());
  DeclDependentNonTemplateCXXMethodAbbrev = Stream.EmitAbbrev(
      getCXXMethodAbbrev<FunctionDecl::TK_DependentNonTemplate>());
  DeclMemberSpecializedCXXMethodAbbrev = Stream.EmitAbbrev(
      getCXXMethodAbbrev<FunctionDecl::TK_MemberSpecialization>());
  DeclTemplateSpecializedCXXMethodAbbrev = Stream.EmitAbbrev(
      getCXXMethodAbbrev<FunctionDecl::TK_FunctionTemplateSpecialization>());
  DeclDependentSpecializationCXXMethodAbbrev = Stream.EmitAbbrev(
      getCXXMethodAbbrev<
          FunctionDecl::TK_DependentFunctionTemplateSpecialization>());

  // Abbreviation for DECL_TEMPLATE_TYPE_PARM
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_TEMPLATE_TYPE_PARM));
  Abv->Add(BitCodeAbbrevOp(0)); // hasTypeConstraint
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           7)); // Packed DeclBits: ModuleOwnershipKind,
                                // isReferenced, isUsed, AccessSpecifier. Other
                                // higher bits should be 0: isImplicit,
                                // HasStandaloneLexicalDC, HasAttrs,
                                // TopLevelDeclInObjCContainer, isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));
  // TypeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type Ref
  // TemplateTypeParmDecl
  Abv->Add(
      BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // wasDeclaredWithTypename
  Abv->Add(BitCodeAbbrevOp(0));                    // TypeConstraintInitialized
  Abv->Add(BitCodeAbbrevOp(0));                    // OwnsDefaultArg
  DeclTemplateTypeParmAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_USING_SHADOW
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_USING_SHADOW));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0)); // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           12)); // Packed DeclBits: HasStandaloneLexicalDC,
                                 // isInvalidDecl, HasAttrs, isImplicit, isUsed,
                                 // isReferenced, TopLevelDeclInObjCContainer,
                                 // AccessSpecifier, ModuleOwnershipKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));
  // UsingShadowDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));    // TargetDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 11)); // IDNS
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));    // UsingOrNextShadow
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR,
                           6)); // InstantiatedFromUsingShadowDecl
  DeclUsingShadowAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_DECL_REF
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_DECL_REF));
  // Stmt
  //  Expr
  //  PackingBits: DependenceKind, ValueKind. ObjectKind should be 0.
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclRefExpr
  // Packing Bits: , HadMultipleCandidates, RefersToEnclosingVariableOrCapture,
  // IsImmediateEscalating, NonOdrUseReason.
  // GetDeclFound, HasQualifier and ExplicitTemplateArgs should be 0.
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 5));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclRef
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Location
  DeclRefExprAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_INTEGER_LITERAL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_INTEGER_LITERAL));
  //Stmt
  // Expr
  // DependenceKind, ValueKind, ObjectKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 10));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // Integer Literal
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Location
  Abv->Add(BitCodeAbbrevOp(32));                      // Bit Width
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Value
  IntegerLiteralAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_CHARACTER_LITERAL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_CHARACTER_LITERAL));
  //Stmt
  // Expr
  // DependenceKind, ValueKind, ObjectKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 10));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // Character Literal
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // getValue
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // getKind
  CharacterLiteralAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_IMPLICIT_CAST
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_IMPLICIT_CAST));
  // Stmt
  // Expr
  // Packing Bits: DependenceKind, ValueKind, ObjectKind,
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 10));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // CastExpr
  Abv->Add(BitCodeAbbrevOp(0)); // PathSize
  // Packing Bits: CastKind, StoredFPFeatures, isPartOfExplicitCast
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 9));
  // ImplicitCastExpr
  ExprImplicitCastAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_BINARY_OPERATOR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_BINARY_OPERATOR));
  // Stmt
  // Expr
  // Packing Bits: DependenceKind. ValueKind and ObjectKind should
  // be 0 in this case.
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 5));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // BinaryOperator
  Abv->Add(
      BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // OpCode and HasFPFeatures
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  BinaryOperatorAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_COMPOUND_ASSIGN_OPERATOR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_COMPOUND_ASSIGN_OPERATOR));
  // Stmt
  // Expr
  // Packing Bits: DependenceKind. ValueKind and ObjectKind should
  // be 0 in this case.
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 5));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // BinaryOperator
  // Packing Bits: OpCode. The HasFPFeatures bit should be 0
  Abv->Add(
      BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // OpCode and HasFPFeatures
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  // CompoundAssignOperator
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // LHSType
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Result Type
  CompoundAssignOperatorAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_CALL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_CALL));
  // Stmt
  // Expr
  // Packing Bits: DependenceKind, ValueKind, ObjectKind,
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 10));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // CallExpr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // NumArgs
  Abv->Add(BitCodeAbbrevOp(0));                       // ADLCallKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  CallExprAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_CXX_OPERATOR_CALL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_CXX_OPERATOR_CALL));
  // Stmt
  // Expr
  // Packing Bits: DependenceKind, ValueKind, ObjectKind,
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 10));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // CallExpr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // NumArgs
  Abv->Add(BitCodeAbbrevOp(0));                       // ADLCallKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  // CXXOperatorCallExpr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Operator Kind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  CXXOperatorCallExprAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_CXX_MEMBER_CALL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_CXX_MEMBER_CALL));
  // Stmt
  // Expr
  // Packing Bits: DependenceKind, ValueKind, ObjectKind,
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 10));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // CallExpr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // NumArgs
  Abv->Add(BitCodeAbbrevOp(0));                       // ADLCallKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  // CXXMemberCallExpr
  CXXMemberCallExprAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for STMT_COMPOUND
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::STMT_COMPOUND));
  // Stmt
  // CompoundStmt
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Num Stmts
  Abv->Add(BitCodeAbbrevOp(0));                       // hasStoredFPFeatures
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  CompoundStmtAbbrev = Stream.EmitAbbrev(std::move(Abv));

  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_CONTEXT_LEXICAL));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  DeclContextLexicalAbbrev = Stream.EmitAbbrev(std::move(Abv));

  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_CONTEXT_VISIBLE));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  DeclContextVisibleLookupAbbrev = Stream.EmitAbbrev(std::move(Abv));
}

/// isRequiredDecl - Check if this is a "required" Decl, which must be seen by
/// consumers of the AST.
///
/// Such decls will always be deserialized from the AST file, so we would like
/// this to be as restrictive as possible. Currently the predicate is driven by
/// code generation requirements, if other clients have a different notion of
/// what is "required" then we may have to consider an alternate scheme where
/// clients can iterate over the top-level decls and get information on them,
/// without necessary deserializing them. We could explicitly require such
/// clients to use a separate API call to "realize" the decl. This should be
/// relatively painless since they would presumably only do it for top-level
/// decls.
static bool isRequiredDecl(const Decl *D, ASTContext &Context,
                           Module *WritingModule) {
  // Named modules have different semantics than header modules. Every named
  // module units owns a translation unit. So the importer of named modules
  // doesn't need to deserilize everything ahead of time.
  if (WritingModule && WritingModule->isNamedModule()) {
    // The PragmaCommentDecl and PragmaDetectMismatchDecl are MSVC's extension.
    // And the behavior of MSVC for such cases will leak this to the module
    // users. Given pragma is not a standard thing, the compiler has the space
    // to do their own decision. Let's follow MSVC here.
    if (isa<PragmaCommentDecl, PragmaDetectMismatchDecl>(D))
      return true;
    return false;
  }

  // An ObjCMethodDecl is never considered as "required" because its
  // implementation container always is.

  // File scoped assembly or obj-c or OMP declare target implementation must be
  // seen.
  if (isa<FileScopeAsmDecl, TopLevelStmtDecl, ObjCImplDecl>(D))
    return true;

  if (WritingModule && isPartOfPerModuleInitializer(D)) {
    // These declarations are part of the module initializer, and are emitted
    // if and when the module is imported, rather than being emitted eagerly.
    return false;
  }

  return Context.DeclMustBeEmitted(D);
}

void ASTWriter::WriteDecl(ASTContext &Context, Decl *D) {
  PrettyDeclStackTraceEntry CrashInfo(Context, D, SourceLocation(),
                                      "serializing");

  // Determine the ID for this declaration.
  LocalDeclID ID;
  assert(!D->isFromASTFile() && "should not be emitting imported decl");
  LocalDeclID &IDR = DeclIDs[D];
  if (IDR.isInvalid())
    IDR = NextDeclID++;

  ID = IDR;

  assert(ID >= FirstDeclID && "invalid decl ID");

  RecordData Record;
  ASTDeclWriter W(*this, Context, Record, GeneratingReducedBMI);

  // Build a record for this declaration
  W.Visit(D);

  // Emit this declaration to the bitstream.
  uint64_t Offset = W.Emit(D);

  // Record the offset for this declaration
  SourceLocation Loc = D->getLocation();
  SourceLocationEncoding::RawLocEncoding RawLoc =
      getRawSourceLocationEncoding(getAdjustedLocation(Loc));

  unsigned Index = ID.getRawValue() - FirstDeclID.getRawValue();
  if (DeclOffsets.size() == Index)
    DeclOffsets.emplace_back(RawLoc, Offset, DeclTypesBlockStartOffset);
  else if (DeclOffsets.size() < Index) {
    // FIXME: Can/should this happen?
    DeclOffsets.resize(Index+1);
    DeclOffsets[Index].setRawLoc(RawLoc);
    DeclOffsets[Index].setBitOffset(Offset, DeclTypesBlockStartOffset);
  } else {
    llvm_unreachable("declarations should be emitted in ID order");
  }

  SourceManager &SM = Context.getSourceManager();
  if (Loc.isValid() && SM.isLocalSourceLocation(Loc))
    associateDeclWithFile(D, ID);

  // Note declarations that should be deserialized eagerly so that we can add
  // them to a record in the AST file later.
  if (isRequiredDecl(D, Context, WritingModule))
    AddDeclRef(D, EagerlyDeserializedDecls);
}

void ASTRecordWriter::AddFunctionDefinition(const FunctionDecl *FD) {
  // Switch case IDs are per function body.
  Writer->ClearSwitchCaseIDs();

  assert(FD->doesThisDeclarationHaveABody());
  bool ModulesCodegen = false;
  if (!FD->isDependentContext()) {
    std::optional<GVALinkage> Linkage;
    if (Writer->WritingModule &&
        Writer->WritingModule->isInterfaceOrPartition()) {
      // When building a C++20 module interface unit or a partition unit, a
      // strong definition in the module interface is provided by the
      // compilation of that unit, not by its users. (Inline functions are still
      // emitted in module users.)
      Linkage = Writer->Context->GetGVALinkageForFunction(FD);
      ModulesCodegen = *Linkage >= GVA_StrongExternal;
    }
    if (Writer->Context->getLangOpts().ModulesCodegen ||
        (FD->hasAttr<DLLExportAttr>() &&
         Writer->Context->getLangOpts().BuildingPCHWithObjectFile)) {

      // Under -fmodules-codegen, codegen is performed for all non-internal,
      // non-always_inline functions, unless they are available elsewhere.
      if (!FD->hasAttr<AlwaysInlineAttr>()) {
        if (!Linkage)
          Linkage = Writer->Context->GetGVALinkageForFunction(FD);
        ModulesCodegen =
            *Linkage != GVA_Internal && *Linkage != GVA_AvailableExternally;
      }
    }
  }
  Record->push_back(ModulesCodegen);
  if (ModulesCodegen)
    Writer->AddDeclRef(FD, Writer->ModularCodegenDecls);
  if (auto *CD = dyn_cast<CXXConstructorDecl>(FD)) {
    Record->push_back(CD->getNumCtorInitializers());
    if (CD->getNumCtorInitializers())
      AddCXXCtorInitializers(llvm::ArrayRef(CD->init_begin(), CD->init_end()));
  }
  AddStmt(FD->getBody());
}
