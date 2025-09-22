//===- ASTContext.cpp - Context to hold long-lived AST nodes --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ASTContext interface.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "CXXABI.h"
#include "Interp/Context.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Attr.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Comment.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/MangleNumberingContext.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CommentOptions.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/NoSanitizeList.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/ProfileList.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetCXXABI.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/XRayLists.h"
#include "llvm/ADT/APFixedPoint.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SipHash.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

using namespace clang;

enum FloatingRank {
  BFloat16Rank,
  Float16Rank,
  HalfRank,
  FloatRank,
  DoubleRank,
  LongDoubleRank,
  Float128Rank,
  Ibm128Rank
};

/// \returns The locations that are relevant when searching for Doc comments
/// related to \p D.
static SmallVector<SourceLocation, 2>
getDeclLocsForCommentSearch(const Decl *D, SourceManager &SourceMgr) {
  assert(D);

  // User can not attach documentation to implicit declarations.
  if (D->isImplicit())
    return {};

  // User can not attach documentation to implicit instantiations.
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return {};
  }

  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (VD->isStaticDataMember() &&
        VD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return {};
  }

  if (const auto *CRD = dyn_cast<CXXRecordDecl>(D)) {
    if (CRD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return {};
  }

  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    TemplateSpecializationKind TSK = CTSD->getSpecializationKind();
    if (TSK == TSK_ImplicitInstantiation ||
        TSK == TSK_Undeclared)
      return {};
  }

  if (const auto *ED = dyn_cast<EnumDecl>(D)) {
    if (ED->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return {};
  }
  if (const auto *TD = dyn_cast<TagDecl>(D)) {
    // When tag declaration (but not definition!) is part of the
    // decl-specifier-seq of some other declaration, it doesn't get comment
    if (TD->isEmbeddedInDeclarator() && !TD->isCompleteDefinition())
      return {};
  }
  // TODO: handle comments for function parameters properly.
  if (isa<ParmVarDecl>(D))
    return {};

  // TODO: we could look up template parameter documentation in the template
  // documentation.
  if (isa<TemplateTypeParmDecl>(D) ||
      isa<NonTypeTemplateParmDecl>(D) ||
      isa<TemplateTemplateParmDecl>(D))
    return {};

  SmallVector<SourceLocation, 2> Locations;
  // Find declaration location.
  // For Objective-C declarations we generally don't expect to have multiple
  // declarators, thus use declaration starting location as the "declaration
  // location".
  // For all other declarations multiple declarators are used quite frequently,
  // so we use the location of the identifier as the "declaration location".
  SourceLocation BaseLocation;
  if (isa<ObjCMethodDecl>(D) || isa<ObjCContainerDecl>(D) ||
      isa<ObjCPropertyDecl>(D) || isa<RedeclarableTemplateDecl>(D) ||
      isa<ClassTemplateSpecializationDecl>(D) ||
      // Allow association with Y across {} in `typedef struct X {} Y`.
      isa<TypedefDecl>(D))
    BaseLocation = D->getBeginLoc();
  else
    BaseLocation = D->getLocation();

  if (!D->getLocation().isMacroID()) {
    Locations.emplace_back(BaseLocation);
  } else {
    const auto *DeclCtx = D->getDeclContext();

    // When encountering definitions generated from a macro (that are not
    // contained by another declaration in the macro) we need to try and find
    // the comment at the location of the expansion but if there is no comment
    // there we should retry to see if there is a comment inside the macro as
    // well. To this end we return first BaseLocation to first look at the
    // expansion site, the second value is the spelling location of the
    // beginning of the declaration defined inside the macro.
    if (!(DeclCtx &&
          Decl::castFromDeclContext(DeclCtx)->getLocation().isMacroID())) {
      Locations.emplace_back(SourceMgr.getExpansionLoc(BaseLocation));
    }

    // We use Decl::getBeginLoc() and not just BaseLocation here to ensure that
    // we don't refer to the macro argument location at the expansion site (this
    // can happen if the name's spelling is provided via macro argument), and
    // always to the declaration itself.
    Locations.emplace_back(SourceMgr.getSpellingLoc(D->getBeginLoc()));
  }

  return Locations;
}

RawComment *ASTContext::getRawCommentForDeclNoCacheImpl(
    const Decl *D, const SourceLocation RepresentativeLocForDecl,
    const std::map<unsigned, RawComment *> &CommentsInTheFile) const {
  // If the declaration doesn't map directly to a location in a file, we
  // can't find the comment.
  if (RepresentativeLocForDecl.isInvalid() ||
      !RepresentativeLocForDecl.isFileID())
    return nullptr;

  // If there are no comments anywhere, we won't find anything.
  if (CommentsInTheFile.empty())
    return nullptr;

  // Decompose the location for the declaration and find the beginning of the
  // file buffer.
  const std::pair<FileID, unsigned> DeclLocDecomp =
      SourceMgr.getDecomposedLoc(RepresentativeLocForDecl);

  // Slow path.
  auto OffsetCommentBehindDecl =
      CommentsInTheFile.lower_bound(DeclLocDecomp.second);

  // First check whether we have a trailing comment.
  if (OffsetCommentBehindDecl != CommentsInTheFile.end()) {
    RawComment *CommentBehindDecl = OffsetCommentBehindDecl->second;
    if ((CommentBehindDecl->isDocumentation() ||
         LangOpts.CommentOpts.ParseAllComments) &&
        CommentBehindDecl->isTrailingComment() &&
        (isa<FieldDecl>(D) || isa<EnumConstantDecl>(D) || isa<VarDecl>(D) ||
         isa<ObjCMethodDecl>(D) || isa<ObjCPropertyDecl>(D))) {

      // Check that Doxygen trailing comment comes after the declaration, starts
      // on the same line and in the same file as the declaration.
      if (SourceMgr.getLineNumber(DeclLocDecomp.first, DeclLocDecomp.second) ==
          Comments.getCommentBeginLine(CommentBehindDecl, DeclLocDecomp.first,
                                       OffsetCommentBehindDecl->first)) {
        return CommentBehindDecl;
      }
    }
  }

  // The comment just after the declaration was not a trailing comment.
  // Let's look at the previous comment.
  if (OffsetCommentBehindDecl == CommentsInTheFile.begin())
    return nullptr;

  auto OffsetCommentBeforeDecl = --OffsetCommentBehindDecl;
  RawComment *CommentBeforeDecl = OffsetCommentBeforeDecl->second;

  // Check that we actually have a non-member Doxygen comment.
  if (!(CommentBeforeDecl->isDocumentation() ||
        LangOpts.CommentOpts.ParseAllComments) ||
      CommentBeforeDecl->isTrailingComment())
    return nullptr;

  // Decompose the end of the comment.
  const unsigned CommentEndOffset =
      Comments.getCommentEndOffset(CommentBeforeDecl);

  // Get the corresponding buffer.
  bool Invalid = false;
  const char *Buffer = SourceMgr.getBufferData(DeclLocDecomp.first,
                                               &Invalid).data();
  if (Invalid)
    return nullptr;

  // Extract text between the comment and declaration.
  StringRef Text(Buffer + CommentEndOffset,
                 DeclLocDecomp.second - CommentEndOffset);

  // There should be no other declarations or preprocessor directives between
  // comment and declaration.
  if (Text.find_last_of(";{}#@") != StringRef::npos)
    return nullptr;

  return CommentBeforeDecl;
}

RawComment *ASTContext::getRawCommentForDeclNoCache(const Decl *D) const {
  const auto DeclLocs = getDeclLocsForCommentSearch(D, SourceMgr);

  for (const auto DeclLoc : DeclLocs) {
    // If the declaration doesn't map directly to a location in a file, we
    // can't find the comment.
    if (DeclLoc.isInvalid() || !DeclLoc.isFileID())
      continue;

    if (ExternalSource && !CommentsLoaded) {
      ExternalSource->ReadComments();
      CommentsLoaded = true;
    }

    if (Comments.empty())
      continue;

    const FileID File = SourceMgr.getDecomposedLoc(DeclLoc).first;
    if (!File.isValid())
      continue;

    const auto CommentsInThisFile = Comments.getCommentsInFile(File);
    if (!CommentsInThisFile || CommentsInThisFile->empty())
      continue;

    if (RawComment *Comment =
            getRawCommentForDeclNoCacheImpl(D, DeclLoc, *CommentsInThisFile))
      return Comment;
  }

  return nullptr;
}

void ASTContext::addComment(const RawComment &RC) {
  assert(LangOpts.RetainCommentsFromSystemHeaders ||
         !SourceMgr.isInSystemHeader(RC.getSourceRange().getBegin()));
  Comments.addComment(RC, LangOpts.CommentOpts, BumpAlloc);
}

/// If we have a 'templated' declaration for a template, adjust 'D' to
/// refer to the actual template.
/// If we have an implicit instantiation, adjust 'D' to refer to template.
static const Decl &adjustDeclToTemplate(const Decl &D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(&D)) {
    // Is this function declaration part of a function template?
    if (const FunctionTemplateDecl *FTD = FD->getDescribedFunctionTemplate())
      return *FTD;

    // Nothing to do if function is not an implicit instantiation.
    if (FD->getTemplateSpecializationKind() != TSK_ImplicitInstantiation)
      return D;

    // Function is an implicit instantiation of a function template?
    if (const FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
      return *FTD;

    // Function is instantiated from a member definition of a class template?
    if (const FunctionDecl *MemberDecl =
            FD->getInstantiatedFromMemberFunction())
      return *MemberDecl;

    return D;
  }
  if (const auto *VD = dyn_cast<VarDecl>(&D)) {
    // Static data member is instantiated from a member definition of a class
    // template?
    if (VD->isStaticDataMember())
      if (const VarDecl *MemberDecl = VD->getInstantiatedFromStaticDataMember())
        return *MemberDecl;

    return D;
  }
  if (const auto *CRD = dyn_cast<CXXRecordDecl>(&D)) {
    // Is this class declaration part of a class template?
    if (const ClassTemplateDecl *CTD = CRD->getDescribedClassTemplate())
      return *CTD;

    // Class is an implicit instantiation of a class template or partial
    // specialization?
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(CRD)) {
      if (CTSD->getSpecializationKind() != TSK_ImplicitInstantiation)
        return D;
      llvm::PointerUnion<ClassTemplateDecl *,
                         ClassTemplatePartialSpecializationDecl *>
          PU = CTSD->getSpecializedTemplateOrPartial();
      return PU.is<ClassTemplateDecl *>()
                 ? *static_cast<const Decl *>(PU.get<ClassTemplateDecl *>())
                 : *static_cast<const Decl *>(
                       PU.get<ClassTemplatePartialSpecializationDecl *>());
    }

    // Class is instantiated from a member definition of a class template?
    if (const MemberSpecializationInfo *Info =
            CRD->getMemberSpecializationInfo())
      return *Info->getInstantiatedFrom();

    return D;
  }
  if (const auto *ED = dyn_cast<EnumDecl>(&D)) {
    // Enum is instantiated from a member definition of a class template?
    if (const EnumDecl *MemberDecl = ED->getInstantiatedFromMemberEnum())
      return *MemberDecl;

    return D;
  }
  // FIXME: Adjust alias templates?
  return D;
}

const RawComment *ASTContext::getRawCommentForAnyRedecl(
                                                const Decl *D,
                                                const Decl **OriginalDecl) const {
  if (!D) {
    if (OriginalDecl)
      OriginalDecl = nullptr;
    return nullptr;
  }

  D = &adjustDeclToTemplate(*D);

  // Any comment directly attached to D?
  {
    auto DeclComment = DeclRawComments.find(D);
    if (DeclComment != DeclRawComments.end()) {
      if (OriginalDecl)
        *OriginalDecl = D;
      return DeclComment->second;
    }
  }

  // Any comment attached to any redeclaration of D?
  const Decl *CanonicalD = D->getCanonicalDecl();
  if (!CanonicalD)
    return nullptr;

  {
    auto RedeclComment = RedeclChainComments.find(CanonicalD);
    if (RedeclComment != RedeclChainComments.end()) {
      if (OriginalDecl)
        *OriginalDecl = RedeclComment->second;
      auto CommentAtRedecl = DeclRawComments.find(RedeclComment->second);
      assert(CommentAtRedecl != DeclRawComments.end() &&
             "This decl is supposed to have comment attached.");
      return CommentAtRedecl->second;
    }
  }

  // Any redeclarations of D that we haven't checked for comments yet?
  // We can't use DenseMap::iterator directly since it'd get invalid.
  auto LastCheckedRedecl = [this, CanonicalD]() -> const Decl * {
    return CommentlessRedeclChains.lookup(CanonicalD);
  }();

  for (const auto Redecl : D->redecls()) {
    assert(Redecl);
    // Skip all redeclarations that have been checked previously.
    if (LastCheckedRedecl) {
      if (LastCheckedRedecl == Redecl) {
        LastCheckedRedecl = nullptr;
      }
      continue;
    }
    const RawComment *RedeclComment = getRawCommentForDeclNoCache(Redecl);
    if (RedeclComment) {
      cacheRawCommentForDecl(*Redecl, *RedeclComment);
      if (OriginalDecl)
        *OriginalDecl = Redecl;
      return RedeclComment;
    }
    CommentlessRedeclChains[CanonicalD] = Redecl;
  }

  if (OriginalDecl)
    *OriginalDecl = nullptr;
  return nullptr;
}

void ASTContext::cacheRawCommentForDecl(const Decl &OriginalD,
                                        const RawComment &Comment) const {
  assert(Comment.isDocumentation() || LangOpts.CommentOpts.ParseAllComments);
  DeclRawComments.try_emplace(&OriginalD, &Comment);
  const Decl *const CanonicalDecl = OriginalD.getCanonicalDecl();
  RedeclChainComments.try_emplace(CanonicalDecl, &OriginalD);
  CommentlessRedeclChains.erase(CanonicalDecl);
}

static void addRedeclaredMethods(const ObjCMethodDecl *ObjCMethod,
                   SmallVectorImpl<const NamedDecl *> &Redeclared) {
  const DeclContext *DC = ObjCMethod->getDeclContext();
  if (const auto *IMD = dyn_cast<ObjCImplDecl>(DC)) {
    const ObjCInterfaceDecl *ID = IMD->getClassInterface();
    if (!ID)
      return;
    // Add redeclared method here.
    for (const auto *Ext : ID->known_extensions()) {
      if (ObjCMethodDecl *RedeclaredMethod =
            Ext->getMethod(ObjCMethod->getSelector(),
                                  ObjCMethod->isInstanceMethod()))
        Redeclared.push_back(RedeclaredMethod);
    }
  }
}

void ASTContext::attachCommentsToJustParsedDecls(ArrayRef<Decl *> Decls,
                                                 const Preprocessor *PP) {
  if (Comments.empty() || Decls.empty())
    return;

  FileID File;
  for (const Decl *D : Decls) {
    if (D->isInvalidDecl())
      continue;

    D = &adjustDeclToTemplate(*D);
    SourceLocation Loc = D->getLocation();
    if (Loc.isValid()) {
      // See if there are any new comments that are not attached to a decl.
      // The location doesn't have to be precise - we care only about the file.
      File = SourceMgr.getDecomposedLoc(Loc).first;
      break;
    }
  }

  if (File.isInvalid())
    return;

  auto CommentsInThisFile = Comments.getCommentsInFile(File);
  if (!CommentsInThisFile || CommentsInThisFile->empty() ||
      CommentsInThisFile->rbegin()->second->isAttached())
    return;

  // There is at least one comment not attached to a decl.
  // Maybe it should be attached to one of Decls?
  //
  // Note that this way we pick up not only comments that precede the
  // declaration, but also comments that *follow* the declaration -- thanks to
  // the lookahead in the lexer: we've consumed the semicolon and looked
  // ahead through comments.
  for (const Decl *D : Decls) {
    assert(D);
    if (D->isInvalidDecl())
      continue;

    D = &adjustDeclToTemplate(*D);

    if (DeclRawComments.count(D) > 0)
      continue;

    const auto DeclLocs = getDeclLocsForCommentSearch(D, SourceMgr);

    for (const auto DeclLoc : DeclLocs) {
      if (DeclLoc.isInvalid() || !DeclLoc.isFileID())
        continue;

      if (RawComment *const DocComment = getRawCommentForDeclNoCacheImpl(
              D, DeclLoc, *CommentsInThisFile)) {
        cacheRawCommentForDecl(*D, *DocComment);
        comments::FullComment *FC = DocComment->parse(*this, PP, D);
        ParsedComments[D->getCanonicalDecl()] = FC;
        break;
      }
    }
  }
}

comments::FullComment *ASTContext::cloneFullComment(comments::FullComment *FC,
                                                    const Decl *D) const {
  auto *ThisDeclInfo = new (*this) comments::DeclInfo;
  ThisDeclInfo->CommentDecl = D;
  ThisDeclInfo->IsFilled = false;
  ThisDeclInfo->fill();
  ThisDeclInfo->CommentDecl = FC->getDecl();
  if (!ThisDeclInfo->TemplateParameters)
    ThisDeclInfo->TemplateParameters = FC->getDeclInfo()->TemplateParameters;
  comments::FullComment *CFC =
    new (*this) comments::FullComment(FC->getBlocks(),
                                      ThisDeclInfo);
  return CFC;
}

comments::FullComment *ASTContext::getLocalCommentForDeclUncached(const Decl *D) const {
  const RawComment *RC = getRawCommentForDeclNoCache(D);
  return RC ? RC->parse(*this, nullptr, D) : nullptr;
}

comments::FullComment *ASTContext::getCommentForDecl(
                                              const Decl *D,
                                              const Preprocessor *PP) const {
  if (!D || D->isInvalidDecl())
    return nullptr;
  D = &adjustDeclToTemplate(*D);

  const Decl *Canonical = D->getCanonicalDecl();
  llvm::DenseMap<const Decl *, comments::FullComment *>::iterator Pos =
      ParsedComments.find(Canonical);

  if (Pos != ParsedComments.end()) {
    if (Canonical != D) {
      comments::FullComment *FC = Pos->second;
      comments::FullComment *CFC = cloneFullComment(FC, D);
      return CFC;
    }
    return Pos->second;
  }

  const Decl *OriginalDecl = nullptr;

  const RawComment *RC = getRawCommentForAnyRedecl(D, &OriginalDecl);
  if (!RC) {
    if (isa<ObjCMethodDecl>(D) || isa<FunctionDecl>(D)) {
      SmallVector<const NamedDecl*, 8> Overridden;
      const auto *OMD = dyn_cast<ObjCMethodDecl>(D);
      if (OMD && OMD->isPropertyAccessor())
        if (const ObjCPropertyDecl *PDecl = OMD->findPropertyDecl())
          if (comments::FullComment *FC = getCommentForDecl(PDecl, PP))
            return cloneFullComment(FC, D);
      if (OMD)
        addRedeclaredMethods(OMD, Overridden);
      getOverriddenMethods(dyn_cast<NamedDecl>(D), Overridden);
      for (unsigned i = 0, e = Overridden.size(); i < e; i++)
        if (comments::FullComment *FC = getCommentForDecl(Overridden[i], PP))
          return cloneFullComment(FC, D);
    }
    else if (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
      // Attach any tag type's documentation to its typedef if latter
      // does not have one of its own.
      QualType QT = TD->getUnderlyingType();
      if (const auto *TT = QT->getAs<TagType>())
        if (const Decl *TD = TT->getDecl())
          if (comments::FullComment *FC = getCommentForDecl(TD, PP))
            return cloneFullComment(FC, D);
    }
    else if (const auto *IC = dyn_cast<ObjCInterfaceDecl>(D)) {
      while (IC->getSuperClass()) {
        IC = IC->getSuperClass();
        if (comments::FullComment *FC = getCommentForDecl(IC, PP))
          return cloneFullComment(FC, D);
      }
    }
    else if (const auto *CD = dyn_cast<ObjCCategoryDecl>(D)) {
      if (const ObjCInterfaceDecl *IC = CD->getClassInterface())
        if (comments::FullComment *FC = getCommentForDecl(IC, PP))
          return cloneFullComment(FC, D);
    }
    else if (const auto *RD = dyn_cast<CXXRecordDecl>(D)) {
      if (!(RD = RD->getDefinition()))
        return nullptr;
      // Check non-virtual bases.
      for (const auto &I : RD->bases()) {
        if (I.isVirtual() || (I.getAccessSpecifier() != AS_public))
          continue;
        QualType Ty = I.getType();
        if (Ty.isNull())
          continue;
        if (const CXXRecordDecl *NonVirtualBase = Ty->getAsCXXRecordDecl()) {
          if (!(NonVirtualBase= NonVirtualBase->getDefinition()))
            continue;

          if (comments::FullComment *FC = getCommentForDecl((NonVirtualBase), PP))
            return cloneFullComment(FC, D);
        }
      }
      // Check virtual bases.
      for (const auto &I : RD->vbases()) {
        if (I.getAccessSpecifier() != AS_public)
          continue;
        QualType Ty = I.getType();
        if (Ty.isNull())
          continue;
        if (const CXXRecordDecl *VirtualBase = Ty->getAsCXXRecordDecl()) {
          if (!(VirtualBase= VirtualBase->getDefinition()))
            continue;
          if (comments::FullComment *FC = getCommentForDecl((VirtualBase), PP))
            return cloneFullComment(FC, D);
        }
      }
    }
    return nullptr;
  }

  // If the RawComment was attached to other redeclaration of this Decl, we
  // should parse the comment in context of that other Decl.  This is important
  // because comments can contain references to parameter names which can be
  // different across redeclarations.
  if (D != OriginalDecl && OriginalDecl)
    return getCommentForDecl(OriginalDecl, PP);

  comments::FullComment *FC = RC->parse(*this, PP, D);
  ParsedComments[Canonical] = FC;
  return FC;
}

void
ASTContext::CanonicalTemplateTemplateParm::Profile(llvm::FoldingSetNodeID &ID,
                                                   const ASTContext &C,
                                               TemplateTemplateParmDecl *Parm) {
  ID.AddInteger(Parm->getDepth());
  ID.AddInteger(Parm->getPosition());
  ID.AddBoolean(Parm->isParameterPack());

  TemplateParameterList *Params = Parm->getTemplateParameters();
  ID.AddInteger(Params->size());
  for (TemplateParameterList::const_iterator P = Params->begin(),
                                          PEnd = Params->end();
       P != PEnd; ++P) {
    if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(*P)) {
      ID.AddInteger(0);
      ID.AddBoolean(TTP->isParameterPack());
      if (TTP->isExpandedParameterPack()) {
        ID.AddBoolean(true);
        ID.AddInteger(TTP->getNumExpansionParameters());
      } else
        ID.AddBoolean(false);
      continue;
    }

    if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(*P)) {
      ID.AddInteger(1);
      ID.AddBoolean(NTTP->isParameterPack());
      ID.AddPointer(C.getUnconstrainedType(C.getCanonicalType(NTTP->getType()))
                        .getAsOpaquePtr());
      if (NTTP->isExpandedParameterPack()) {
        ID.AddBoolean(true);
        ID.AddInteger(NTTP->getNumExpansionTypes());
        for (unsigned I = 0, N = NTTP->getNumExpansionTypes(); I != N; ++I) {
          QualType T = NTTP->getExpansionType(I);
          ID.AddPointer(T.getCanonicalType().getAsOpaquePtr());
        }
      } else
        ID.AddBoolean(false);
      continue;
    }

    auto *TTP = cast<TemplateTemplateParmDecl>(*P);
    ID.AddInteger(2);
    Profile(ID, C, TTP);
  }
}

TemplateTemplateParmDecl *
ASTContext::getCanonicalTemplateTemplateParmDecl(
                                          TemplateTemplateParmDecl *TTP) const {
  // Check if we already have a canonical template template parameter.
  llvm::FoldingSetNodeID ID;
  CanonicalTemplateTemplateParm::Profile(ID, *this, TTP);
  void *InsertPos = nullptr;
  CanonicalTemplateTemplateParm *Canonical
    = CanonTemplateTemplateParms.FindNodeOrInsertPos(ID, InsertPos);
  if (Canonical)
    return Canonical->getParam();

  // Build a canonical template parameter list.
  TemplateParameterList *Params = TTP->getTemplateParameters();
  SmallVector<NamedDecl *, 4> CanonParams;
  CanonParams.reserve(Params->size());
  for (TemplateParameterList::const_iterator P = Params->begin(),
                                          PEnd = Params->end();
       P != PEnd; ++P) {
    // Note that, per C++20 [temp.over.link]/6, when determining whether
    // template-parameters are equivalent, constraints are ignored.
    if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(*P)) {
      TemplateTypeParmDecl *NewTTP = TemplateTypeParmDecl::Create(
          *this, getTranslationUnitDecl(), SourceLocation(), SourceLocation(),
          TTP->getDepth(), TTP->getIndex(), nullptr, false,
          TTP->isParameterPack(), /*HasTypeConstraint=*/false,
          TTP->isExpandedParameterPack()
              ? std::optional<unsigned>(TTP->getNumExpansionParameters())
              : std::nullopt);
      CanonParams.push_back(NewTTP);
    } else if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(*P)) {
      QualType T = getUnconstrainedType(getCanonicalType(NTTP->getType()));
      TypeSourceInfo *TInfo = getTrivialTypeSourceInfo(T);
      NonTypeTemplateParmDecl *Param;
      if (NTTP->isExpandedParameterPack()) {
        SmallVector<QualType, 2> ExpandedTypes;
        SmallVector<TypeSourceInfo *, 2> ExpandedTInfos;
        for (unsigned I = 0, N = NTTP->getNumExpansionTypes(); I != N; ++I) {
          ExpandedTypes.push_back(getCanonicalType(NTTP->getExpansionType(I)));
          ExpandedTInfos.push_back(
                                getTrivialTypeSourceInfo(ExpandedTypes.back()));
        }

        Param = NonTypeTemplateParmDecl::Create(*this, getTranslationUnitDecl(),
                                                SourceLocation(),
                                                SourceLocation(),
                                                NTTP->getDepth(),
                                                NTTP->getPosition(), nullptr,
                                                T,
                                                TInfo,
                                                ExpandedTypes,
                                                ExpandedTInfos);
      } else {
        Param = NonTypeTemplateParmDecl::Create(*this, getTranslationUnitDecl(),
                                                SourceLocation(),
                                                SourceLocation(),
                                                NTTP->getDepth(),
                                                NTTP->getPosition(), nullptr,
                                                T,
                                                NTTP->isParameterPack(),
                                                TInfo);
      }
      CanonParams.push_back(Param);
    } else
      CanonParams.push_back(getCanonicalTemplateTemplateParmDecl(
                                           cast<TemplateTemplateParmDecl>(*P)));
  }

  TemplateTemplateParmDecl *CanonTTP = TemplateTemplateParmDecl::Create(
      *this, getTranslationUnitDecl(), SourceLocation(), TTP->getDepth(),
      TTP->getPosition(), TTP->isParameterPack(), nullptr, /*Typename=*/false,
      TemplateParameterList::Create(*this, SourceLocation(), SourceLocation(),
                                    CanonParams, SourceLocation(),
                                    /*RequiresClause=*/nullptr));

  // Get the new insert position for the node we care about.
  Canonical = CanonTemplateTemplateParms.FindNodeOrInsertPos(ID, InsertPos);
  assert(!Canonical && "Shouldn't be in the map!");
  (void)Canonical;

  // Create the canonical template template parameter entry.
  Canonical = new (*this) CanonicalTemplateTemplateParm(CanonTTP);
  CanonTemplateTemplateParms.InsertNode(Canonical, InsertPos);
  return CanonTTP;
}

TargetCXXABI::Kind ASTContext::getCXXABIKind() const {
  auto Kind = getTargetInfo().getCXXABI().getKind();
  return getLangOpts().CXXABI.value_or(Kind);
}

CXXABI *ASTContext::createCXXABI(const TargetInfo &T) {
  if (!LangOpts.CPlusPlus) return nullptr;

  switch (getCXXABIKind()) {
  case TargetCXXABI::AppleARM64:
  case TargetCXXABI::Fuchsia:
  case TargetCXXABI::GenericARM: // Same as Itanium at this level
  case TargetCXXABI::iOS:
  case TargetCXXABI::WatchOS:
  case TargetCXXABI::GenericAArch64:
  case TargetCXXABI::GenericMIPS:
  case TargetCXXABI::GenericItanium:
  case TargetCXXABI::WebAssembly:
  case TargetCXXABI::XL:
    return CreateItaniumCXXABI(*this);
  case TargetCXXABI::Microsoft:
    return CreateMicrosoftCXXABI(*this);
  }
  llvm_unreachable("Invalid CXXABI type!");
}

interp::Context &ASTContext::getInterpContext() {
  if (!InterpContext) {
    InterpContext.reset(new interp::Context(*this));
  }
  return *InterpContext.get();
}

ParentMapContext &ASTContext::getParentMapContext() {
  if (!ParentMapCtx)
    ParentMapCtx.reset(new ParentMapContext(*this));
  return *ParentMapCtx.get();
}

static bool isAddrSpaceMapManglingEnabled(const TargetInfo &TI,
                                          const LangOptions &LangOpts) {
  switch (LangOpts.getAddressSpaceMapMangling()) {
  case LangOptions::ASMM_Target:
    return TI.useAddressSpaceMapMangling();
  case LangOptions::ASMM_On:
    return true;
  case LangOptions::ASMM_Off:
    return false;
  }
  llvm_unreachable("getAddressSpaceMapMangling() doesn't cover anything.");
}

ASTContext::ASTContext(LangOptions &LOpts, SourceManager &SM,
                       IdentifierTable &idents, SelectorTable &sels,
                       Builtin::Context &builtins, TranslationUnitKind TUKind)
    : ConstantArrayTypes(this_(), ConstantArrayTypesLog2InitSize),
      DependentSizedArrayTypes(this_()), DependentSizedExtVectorTypes(this_()),
      DependentAddressSpaceTypes(this_()), DependentVectorTypes(this_()),
      DependentSizedMatrixTypes(this_()),
      FunctionProtoTypes(this_(), FunctionProtoTypesLog2InitSize),
      DependentTypeOfExprTypes(this_()), DependentDecltypeTypes(this_()),
      TemplateSpecializationTypes(this_()),
      DependentTemplateSpecializationTypes(this_()), AutoTypes(this_()),
      DependentBitIntTypes(this_()), SubstTemplateTemplateParmPacks(this_()),
      ArrayParameterTypes(this_()), CanonTemplateTemplateParms(this_()),
      SourceMgr(SM), LangOpts(LOpts),
      NoSanitizeL(new NoSanitizeList(LangOpts.NoSanitizeFiles, SM)),
      XRayFilter(new XRayFunctionFilter(LangOpts.XRayAlwaysInstrumentFiles,
                                        LangOpts.XRayNeverInstrumentFiles,
                                        LangOpts.XRayAttrListFiles, SM)),
      ProfList(new ProfileList(LangOpts.ProfileListFiles, SM)),
      PrintingPolicy(LOpts), Idents(idents), Selectors(sels),
      BuiltinInfo(builtins), TUKind(TUKind), DeclarationNames(*this),
      Comments(SM), CommentCommandTraits(BumpAlloc, LOpts.CommentOpts),
      CompCategories(this_()), LastSDM(nullptr, 0) {
  addTranslationUnitDecl();
}

void ASTContext::cleanup() {
  // Release the DenseMaps associated with DeclContext objects.
  // FIXME: Is this the ideal solution?
  ReleaseDeclContextMaps();

  // Call all of the deallocation functions on all of their targets.
  for (auto &Pair : Deallocations)
    (Pair.first)(Pair.second);
  Deallocations.clear();

  // ASTRecordLayout objects in ASTRecordLayouts must always be destroyed
  // because they can contain DenseMaps.
  for (llvm::DenseMap<const ObjCContainerDecl*,
       const ASTRecordLayout*>::iterator
       I = ObjCLayouts.begin(), E = ObjCLayouts.end(); I != E; )
    // Increment in loop to prevent using deallocated memory.
    if (auto *R = const_cast<ASTRecordLayout *>((I++)->second))
      R->Destroy(*this);
  ObjCLayouts.clear();

  for (llvm::DenseMap<const RecordDecl*, const ASTRecordLayout*>::iterator
       I = ASTRecordLayouts.begin(), E = ASTRecordLayouts.end(); I != E; ) {
    // Increment in loop to prevent using deallocated memory.
    if (auto *R = const_cast<ASTRecordLayout *>((I++)->second))
      R->Destroy(*this);
  }
  ASTRecordLayouts.clear();

  for (llvm::DenseMap<const Decl*, AttrVec*>::iterator A = DeclAttrs.begin(),
                                                    AEnd = DeclAttrs.end();
       A != AEnd; ++A)
    A->second->~AttrVec();
  DeclAttrs.clear();

  for (const auto &Value : ModuleInitializers)
    Value.second->~PerModuleInitializers();
  ModuleInitializers.clear();
}

ASTContext::~ASTContext() { cleanup(); }

void ASTContext::setTraversalScope(const std::vector<Decl *> &TopLevelDecls) {
  TraversalScope = TopLevelDecls;
  getParentMapContext().clear();
}

void ASTContext::AddDeallocation(void (*Callback)(void *), void *Data) const {
  Deallocations.push_back({Callback, Data});
}

void
ASTContext::setExternalSource(IntrusiveRefCntPtr<ExternalASTSource> Source) {
  ExternalSource = std::move(Source);
}

void ASTContext::PrintStats() const {
  llvm::errs() << "\n*** AST Context Stats:\n";
  llvm::errs() << "  " << Types.size() << " types total.\n";

  unsigned counts[] = {
#define TYPE(Name, Parent) 0,
#define ABSTRACT_TYPE(Name, Parent)
#include "clang/AST/TypeNodes.inc"
    0 // Extra
  };

  for (unsigned i = 0, e = Types.size(); i != e; ++i) {
    Type *T = Types[i];
    counts[(unsigned)T->getTypeClass()]++;
  }

  unsigned Idx = 0;
  unsigned TotalBytes = 0;
#define TYPE(Name, Parent)                                              \
  if (counts[Idx])                                                      \
    llvm::errs() << "    " << counts[Idx] << " " << #Name               \
                 << " types, " << sizeof(Name##Type) << " each "        \
                 << "(" << counts[Idx] * sizeof(Name##Type)             \
                 << " bytes)\n";                                        \
  TotalBytes += counts[Idx] * sizeof(Name##Type);                       \
  ++Idx;
#define ABSTRACT_TYPE(Name, Parent)
#include "clang/AST/TypeNodes.inc"

  llvm::errs() << "Total bytes = " << TotalBytes << "\n";

  // Implicit special member functions.
  llvm::errs() << NumImplicitDefaultConstructorsDeclared << "/"
               << NumImplicitDefaultConstructors
               << " implicit default constructors created\n";
  llvm::errs() << NumImplicitCopyConstructorsDeclared << "/"
               << NumImplicitCopyConstructors
               << " implicit copy constructors created\n";
  if (getLangOpts().CPlusPlus)
    llvm::errs() << NumImplicitMoveConstructorsDeclared << "/"
                 << NumImplicitMoveConstructors
                 << " implicit move constructors created\n";
  llvm::errs() << NumImplicitCopyAssignmentOperatorsDeclared << "/"
               << NumImplicitCopyAssignmentOperators
               << " implicit copy assignment operators created\n";
  if (getLangOpts().CPlusPlus)
    llvm::errs() << NumImplicitMoveAssignmentOperatorsDeclared << "/"
                 << NumImplicitMoveAssignmentOperators
                 << " implicit move assignment operators created\n";
  llvm::errs() << NumImplicitDestructorsDeclared << "/"
               << NumImplicitDestructors
               << " implicit destructors created\n";

  if (ExternalSource) {
    llvm::errs() << "\n";
    ExternalSource->PrintStats();
  }

  BumpAlloc.PrintStats();
}

void ASTContext::mergeDefinitionIntoModule(NamedDecl *ND, Module *M,
                                           bool NotifyListeners) {
  if (NotifyListeners)
    if (auto *Listener = getASTMutationListener())
      Listener->RedefinedHiddenDefinition(ND, M);

  MergedDefModules[cast<NamedDecl>(ND->getCanonicalDecl())].push_back(M);
}

void ASTContext::deduplicateMergedDefinitonsFor(NamedDecl *ND) {
  auto It = MergedDefModules.find(cast<NamedDecl>(ND->getCanonicalDecl()));
  if (It == MergedDefModules.end())
    return;

  auto &Merged = It->second;
  llvm::DenseSet<Module*> Found;
  for (Module *&M : Merged)
    if (!Found.insert(M).second)
      M = nullptr;
  llvm::erase(Merged, nullptr);
}

ArrayRef<Module *>
ASTContext::getModulesWithMergedDefinition(const NamedDecl *Def) {
  auto MergedIt =
      MergedDefModules.find(cast<NamedDecl>(Def->getCanonicalDecl()));
  if (MergedIt == MergedDefModules.end())
    return std::nullopt;
  return MergedIt->second;
}

void ASTContext::PerModuleInitializers::resolve(ASTContext &Ctx) {
  if (LazyInitializers.empty())
    return;

  auto *Source = Ctx.getExternalSource();
  assert(Source && "lazy initializers but no external source");

  auto LazyInits = std::move(LazyInitializers);
  LazyInitializers.clear();

  for (auto ID : LazyInits)
    Initializers.push_back(Source->GetExternalDecl(ID));

  assert(LazyInitializers.empty() &&
         "GetExternalDecl for lazy module initializer added more inits");
}

void ASTContext::addModuleInitializer(Module *M, Decl *D) {
  // One special case: if we add a module initializer that imports another
  // module, and that module's only initializer is an ImportDecl, simplify.
  if (const auto *ID = dyn_cast<ImportDecl>(D)) {
    auto It = ModuleInitializers.find(ID->getImportedModule());

    // Maybe the ImportDecl does nothing at all. (Common case.)
    if (It == ModuleInitializers.end())
      return;

    // Maybe the ImportDecl only imports another ImportDecl.
    auto &Imported = *It->second;
    if (Imported.Initializers.size() + Imported.LazyInitializers.size() == 1) {
      Imported.resolve(*this);
      auto *OnlyDecl = Imported.Initializers.front();
      if (isa<ImportDecl>(OnlyDecl))
        D = OnlyDecl;
    }
  }

  auto *&Inits = ModuleInitializers[M];
  if (!Inits)
    Inits = new (*this) PerModuleInitializers;
  Inits->Initializers.push_back(D);
}

void ASTContext::addLazyModuleInitializers(Module *M,
                                           ArrayRef<GlobalDeclID> IDs) {
  auto *&Inits = ModuleInitializers[M];
  if (!Inits)
    Inits = new (*this) PerModuleInitializers;
  Inits->LazyInitializers.insert(Inits->LazyInitializers.end(),
                                 IDs.begin(), IDs.end());
}

ArrayRef<Decl *> ASTContext::getModuleInitializers(Module *M) {
  auto It = ModuleInitializers.find(M);
  if (It == ModuleInitializers.end())
    return std::nullopt;

  auto *Inits = It->second;
  Inits->resolve(*this);
  return Inits->Initializers;
}

void ASTContext::setCurrentNamedModule(Module *M) {
  assert(M->isNamedModule());
  assert(!CurrentCXXNamedModule &&
         "We should set named module for ASTContext for only once");
  CurrentCXXNamedModule = M;
}

bool ASTContext::isInSameModule(const Module *M1, const Module *M2) {
  if (!M1 != !M2)
    return false;

  /// Get the representative module for M. The representative module is the
  /// first module unit for a specific primary module name. So that the module
  /// units have the same representative module belongs to the same module.
  ///
  /// The process is helpful to reduce the expensive string operations.
  auto GetRepresentativeModule = [this](const Module *M) {
    auto Iter = SameModuleLookupSet.find(M);
    if (Iter != SameModuleLookupSet.end())
      return Iter->second;

    const Module *RepresentativeModule =
        PrimaryModuleNameMap.try_emplace(M->getPrimaryModuleInterfaceName(), M)
            .first->second;
    SameModuleLookupSet[M] = RepresentativeModule;
    return RepresentativeModule;
  };

  assert(M1 && "Shouldn't call `isInSameModule` if both M1 and M2 are none.");
  return GetRepresentativeModule(M1) == GetRepresentativeModule(M2);
}

ExternCContextDecl *ASTContext::getExternCContextDecl() const {
  if (!ExternCContext)
    ExternCContext = ExternCContextDecl::Create(*this, getTranslationUnitDecl());

  return ExternCContext;
}

BuiltinTemplateDecl *
ASTContext::buildBuiltinTemplateDecl(BuiltinTemplateKind BTK,
                                     const IdentifierInfo *II) const {
  auto *BuiltinTemplate =
      BuiltinTemplateDecl::Create(*this, getTranslationUnitDecl(), II, BTK);
  BuiltinTemplate->setImplicit();
  getTranslationUnitDecl()->addDecl(BuiltinTemplate);

  return BuiltinTemplate;
}

BuiltinTemplateDecl *
ASTContext::getMakeIntegerSeqDecl() const {
  if (!MakeIntegerSeqDecl)
    MakeIntegerSeqDecl = buildBuiltinTemplateDecl(BTK__make_integer_seq,
                                                  getMakeIntegerSeqName());
  return MakeIntegerSeqDecl;
}

BuiltinTemplateDecl *
ASTContext::getTypePackElementDecl() const {
  if (!TypePackElementDecl)
    TypePackElementDecl = buildBuiltinTemplateDecl(BTK__type_pack_element,
                                                   getTypePackElementName());
  return TypePackElementDecl;
}

RecordDecl *ASTContext::buildImplicitRecord(StringRef Name,
                                            RecordDecl::TagKind TK) const {
  SourceLocation Loc;
  RecordDecl *NewDecl;
  if (getLangOpts().CPlusPlus)
    NewDecl = CXXRecordDecl::Create(*this, TK, getTranslationUnitDecl(), Loc,
                                    Loc, &Idents.get(Name));
  else
    NewDecl = RecordDecl::Create(*this, TK, getTranslationUnitDecl(), Loc, Loc,
                                 &Idents.get(Name));
  NewDecl->setImplicit();
  NewDecl->addAttr(TypeVisibilityAttr::CreateImplicit(
      const_cast<ASTContext &>(*this), TypeVisibilityAttr::Default));
  return NewDecl;
}

TypedefDecl *ASTContext::buildImplicitTypedef(QualType T,
                                              StringRef Name) const {
  TypeSourceInfo *TInfo = getTrivialTypeSourceInfo(T);
  TypedefDecl *NewDecl = TypedefDecl::Create(
      const_cast<ASTContext &>(*this), getTranslationUnitDecl(),
      SourceLocation(), SourceLocation(), &Idents.get(Name), TInfo);
  NewDecl->setImplicit();
  return NewDecl;
}

TypedefDecl *ASTContext::getInt128Decl() const {
  if (!Int128Decl)
    Int128Decl = buildImplicitTypedef(Int128Ty, "__int128_t");
  return Int128Decl;
}

TypedefDecl *ASTContext::getUInt128Decl() const {
  if (!UInt128Decl)
    UInt128Decl = buildImplicitTypedef(UnsignedInt128Ty, "__uint128_t");
  return UInt128Decl;
}

void ASTContext::InitBuiltinType(CanQualType &R, BuiltinType::Kind K) {
  auto *Ty = new (*this, alignof(BuiltinType)) BuiltinType(K);
  R = CanQualType::CreateUnsafe(QualType(Ty, 0));
  Types.push_back(Ty);
}

void ASTContext::InitBuiltinTypes(const TargetInfo &Target,
                                  const TargetInfo *AuxTarget) {
  assert((!this->Target || this->Target == &Target) &&
         "Incorrect target reinitialization");
  assert(VoidTy.isNull() && "Context reinitialized?");

  this->Target = &Target;
  this->AuxTarget = AuxTarget;

  ABI.reset(createCXXABI(Target));
  AddrSpaceMapMangling = isAddrSpaceMapManglingEnabled(Target, LangOpts);

  // C99 6.2.5p19.
  InitBuiltinType(VoidTy,              BuiltinType::Void);

  // C99 6.2.5p2.
  InitBuiltinType(BoolTy,              BuiltinType::Bool);
  // C99 6.2.5p3.
  if (LangOpts.CharIsSigned)
    InitBuiltinType(CharTy,            BuiltinType::Char_S);
  else
    InitBuiltinType(CharTy,            BuiltinType::Char_U);
  // C99 6.2.5p4.
  InitBuiltinType(SignedCharTy,        BuiltinType::SChar);
  InitBuiltinType(ShortTy,             BuiltinType::Short);
  InitBuiltinType(IntTy,               BuiltinType::Int);
  InitBuiltinType(LongTy,              BuiltinType::Long);
  InitBuiltinType(LongLongTy,          BuiltinType::LongLong);

  // C99 6.2.5p6.
  InitBuiltinType(UnsignedCharTy,      BuiltinType::UChar);
  InitBuiltinType(UnsignedShortTy,     BuiltinType::UShort);
  InitBuiltinType(UnsignedIntTy,       BuiltinType::UInt);
  InitBuiltinType(UnsignedLongTy,      BuiltinType::ULong);
  InitBuiltinType(UnsignedLongLongTy,  BuiltinType::ULongLong);

  // C99 6.2.5p10.
  InitBuiltinType(FloatTy,             BuiltinType::Float);
  InitBuiltinType(DoubleTy,            BuiltinType::Double);
  InitBuiltinType(LongDoubleTy,        BuiltinType::LongDouble);

  // GNU extension, __float128 for IEEE quadruple precision
  InitBuiltinType(Float128Ty,          BuiltinType::Float128);

  // __ibm128 for IBM extended precision
  InitBuiltinType(Ibm128Ty, BuiltinType::Ibm128);

  // C11 extension ISO/IEC TS 18661-3
  InitBuiltinType(Float16Ty,           BuiltinType::Float16);

  // ISO/IEC JTC1 SC22 WG14 N1169 Extension
  InitBuiltinType(ShortAccumTy,            BuiltinType::ShortAccum);
  InitBuiltinType(AccumTy,                 BuiltinType::Accum);
  InitBuiltinType(LongAccumTy,             BuiltinType::LongAccum);
  InitBuiltinType(UnsignedShortAccumTy,    BuiltinType::UShortAccum);
  InitBuiltinType(UnsignedAccumTy,         BuiltinType::UAccum);
  InitBuiltinType(UnsignedLongAccumTy,     BuiltinType::ULongAccum);
  InitBuiltinType(ShortFractTy,            BuiltinType::ShortFract);
  InitBuiltinType(FractTy,                 BuiltinType::Fract);
  InitBuiltinType(LongFractTy,             BuiltinType::LongFract);
  InitBuiltinType(UnsignedShortFractTy,    BuiltinType::UShortFract);
  InitBuiltinType(UnsignedFractTy,         BuiltinType::UFract);
  InitBuiltinType(UnsignedLongFractTy,     BuiltinType::ULongFract);
  InitBuiltinType(SatShortAccumTy,         BuiltinType::SatShortAccum);
  InitBuiltinType(SatAccumTy,              BuiltinType::SatAccum);
  InitBuiltinType(SatLongAccumTy,          BuiltinType::SatLongAccum);
  InitBuiltinType(SatUnsignedShortAccumTy, BuiltinType::SatUShortAccum);
  InitBuiltinType(SatUnsignedAccumTy,      BuiltinType::SatUAccum);
  InitBuiltinType(SatUnsignedLongAccumTy,  BuiltinType::SatULongAccum);
  InitBuiltinType(SatShortFractTy,         BuiltinType::SatShortFract);
  InitBuiltinType(SatFractTy,              BuiltinType::SatFract);
  InitBuiltinType(SatLongFractTy,          BuiltinType::SatLongFract);
  InitBuiltinType(SatUnsignedShortFractTy, BuiltinType::SatUShortFract);
  InitBuiltinType(SatUnsignedFractTy,      BuiltinType::SatUFract);
  InitBuiltinType(SatUnsignedLongFractTy,  BuiltinType::SatULongFract);

  // GNU extension, 128-bit integers.
  InitBuiltinType(Int128Ty,            BuiltinType::Int128);
  InitBuiltinType(UnsignedInt128Ty,    BuiltinType::UInt128);

  // C++ 3.9.1p5
  if (TargetInfo::isTypeSigned(Target.getWCharType()))
    InitBuiltinType(WCharTy,           BuiltinType::WChar_S);
  else  // -fshort-wchar makes wchar_t be unsigned.
    InitBuiltinType(WCharTy,           BuiltinType::WChar_U);
  if (LangOpts.CPlusPlus && LangOpts.WChar)
    WideCharTy = WCharTy;
  else {
    // C99 (or C++ using -fno-wchar).
    WideCharTy = getFromTargetType(Target.getWCharType());
  }

  WIntTy = getFromTargetType(Target.getWIntType());

  // C++20 (proposed)
  InitBuiltinType(Char8Ty,              BuiltinType::Char8);

  if (LangOpts.CPlusPlus) // C++0x 3.9.1p5, extension for C++
    InitBuiltinType(Char16Ty,           BuiltinType::Char16);
  else // C99
    Char16Ty = getFromTargetType(Target.getChar16Type());

  if (LangOpts.CPlusPlus) // C++0x 3.9.1p5, extension for C++
    InitBuiltinType(Char32Ty,           BuiltinType::Char32);
  else // C99
    Char32Ty = getFromTargetType(Target.getChar32Type());

  // Placeholder type for type-dependent expressions whose type is
  // completely unknown. No code should ever check a type against
  // DependentTy and users should never see it; however, it is here to
  // help diagnose failures to properly check for type-dependent
  // expressions.
  InitBuiltinType(DependentTy,         BuiltinType::Dependent);

  // Placeholder type for functions.
  InitBuiltinType(OverloadTy,          BuiltinType::Overload);

  // Placeholder type for bound members.
  InitBuiltinType(BoundMemberTy,       BuiltinType::BoundMember);

  // Placeholder type for unresolved templates.
  InitBuiltinType(UnresolvedTemplateTy, BuiltinType::UnresolvedTemplate);

  // Placeholder type for pseudo-objects.
  InitBuiltinType(PseudoObjectTy,      BuiltinType::PseudoObject);

  // "any" type; useful for debugger-like clients.
  InitBuiltinType(UnknownAnyTy,        BuiltinType::UnknownAny);

  // Placeholder type for unbridged ARC casts.
  InitBuiltinType(ARCUnbridgedCastTy,  BuiltinType::ARCUnbridgedCast);

  // Placeholder type for builtin functions.
  InitBuiltinType(BuiltinFnTy,  BuiltinType::BuiltinFn);

  // Placeholder type for OMP array sections.
  if (LangOpts.OpenMP) {
    InitBuiltinType(ArraySectionTy, BuiltinType::ArraySection);
    InitBuiltinType(OMPArrayShapingTy, BuiltinType::OMPArrayShaping);
    InitBuiltinType(OMPIteratorTy, BuiltinType::OMPIterator);
  }
  // Placeholder type for OpenACC array sections, if we are ALSO in OMP mode,
  // don't bother, as we're just using the same type as OMP.
  if (LangOpts.OpenACC && !LangOpts.OpenMP) {
    InitBuiltinType(ArraySectionTy, BuiltinType::ArraySection);
  }
  if (LangOpts.MatrixTypes)
    InitBuiltinType(IncompleteMatrixIdxTy, BuiltinType::IncompleteMatrixIdx);

  // Builtin types for 'id', 'Class', and 'SEL'.
  InitBuiltinType(ObjCBuiltinIdTy, BuiltinType::ObjCId);
  InitBuiltinType(ObjCBuiltinClassTy, BuiltinType::ObjCClass);
  InitBuiltinType(ObjCBuiltinSelTy, BuiltinType::ObjCSel);

  if (LangOpts.OpenCL) {
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
    InitBuiltinType(SingletonId, BuiltinType::Id);
#include "clang/Basic/OpenCLImageTypes.def"

    InitBuiltinType(OCLSamplerTy, BuiltinType::OCLSampler);
    InitBuiltinType(OCLEventTy, BuiltinType::OCLEvent);
    InitBuiltinType(OCLClkEventTy, BuiltinType::OCLClkEvent);
    InitBuiltinType(OCLQueueTy, BuiltinType::OCLQueue);
    InitBuiltinType(OCLReserveIDTy, BuiltinType::OCLReserveID);

#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
    InitBuiltinType(Id##Ty, BuiltinType::Id);
#include "clang/Basic/OpenCLExtensionTypes.def"
  }

  if (Target.hasAArch64SVETypes() ||
      (AuxTarget && AuxTarget->hasAArch64SVETypes())) {
#define SVE_TYPE(Name, Id, SingletonId) \
    InitBuiltinType(SingletonId, BuiltinType::Id);
#include "clang/Basic/AArch64SVEACLETypes.def"
  }

  if (Target.getTriple().isPPC64()) {
#define PPC_VECTOR_MMA_TYPE(Name, Id, Size) \
      InitBuiltinType(Id##Ty, BuiltinType::Id);
#include "clang/Basic/PPCTypes.def"
#define PPC_VECTOR_VSX_TYPE(Name, Id, Size) \
    InitBuiltinType(Id##Ty, BuiltinType::Id);
#include "clang/Basic/PPCTypes.def"
  }

  if (Target.hasRISCVVTypes()) {
#define RVV_TYPE(Name, Id, SingletonId)                                        \
  InitBuiltinType(SingletonId, BuiltinType::Id);
#include "clang/Basic/RISCVVTypes.def"
  }

  if (Target.getTriple().isWasm() && Target.hasFeature("reference-types")) {
#define WASM_TYPE(Name, Id, SingletonId)                                       \
  InitBuiltinType(SingletonId, BuiltinType::Id);
#include "clang/Basic/WebAssemblyReferenceTypes.def"
  }

  if (Target.getTriple().isAMDGPU() ||
      (AuxTarget && AuxTarget->getTriple().isAMDGPU())) {
#define AMDGPU_TYPE(Name, Id, SingletonId)                                     \
  InitBuiltinType(SingletonId, BuiltinType::Id);
#include "clang/Basic/AMDGPUTypes.def"
  }

  // Builtin type for __objc_yes and __objc_no
  ObjCBuiltinBoolTy = (Target.useSignedCharForObjCBool() ?
                       SignedCharTy : BoolTy);

  ObjCConstantStringType = QualType();

  ObjCSuperType = QualType();

  // void * type
  if (LangOpts.OpenCLGenericAddressSpace) {
    auto Q = VoidTy.getQualifiers();
    Q.setAddressSpace(LangAS::opencl_generic);
    VoidPtrTy = getPointerType(getCanonicalType(
        getQualifiedType(VoidTy.getUnqualifiedType(), Q)));
  } else {
    VoidPtrTy = getPointerType(VoidTy);
  }

  // nullptr type (C++0x 2.14.7)
  InitBuiltinType(NullPtrTy,           BuiltinType::NullPtr);

  // half type (OpenCL 6.1.1.1) / ARM NEON __fp16
  InitBuiltinType(HalfTy, BuiltinType::Half);

  InitBuiltinType(BFloat16Ty, BuiltinType::BFloat16);

  // Builtin type used to help define __builtin_va_list.
  VaListTagDecl = nullptr;

  // MSVC predeclares struct _GUID, and we need it to create MSGuidDecls.
  if (LangOpts.MicrosoftExt || LangOpts.Borland) {
    MSGuidTagDecl = buildImplicitRecord("_GUID");
    getTranslationUnitDecl()->addDecl(MSGuidTagDecl);
  }
}

DiagnosticsEngine &ASTContext::getDiagnostics() const {
  return SourceMgr.getDiagnostics();
}

AttrVec& ASTContext::getDeclAttrs(const Decl *D) {
  AttrVec *&Result = DeclAttrs[D];
  if (!Result) {
    void *Mem = Allocate(sizeof(AttrVec));
    Result = new (Mem) AttrVec;
  }

  return *Result;
}

/// Erase the attributes corresponding to the given declaration.
void ASTContext::eraseDeclAttrs(const Decl *D) {
  llvm::DenseMap<const Decl*, AttrVec*>::iterator Pos = DeclAttrs.find(D);
  if (Pos != DeclAttrs.end()) {
    Pos->second->~AttrVec();
    DeclAttrs.erase(Pos);
  }
}

// FIXME: Remove ?
MemberSpecializationInfo *
ASTContext::getInstantiatedFromStaticDataMember(const VarDecl *Var) {
  assert(Var->isStaticDataMember() && "Not a static data member");
  return getTemplateOrSpecializationInfo(Var)
      .dyn_cast<MemberSpecializationInfo *>();
}

ASTContext::TemplateOrSpecializationInfo
ASTContext::getTemplateOrSpecializationInfo(const VarDecl *Var) {
  llvm::DenseMap<const VarDecl *, TemplateOrSpecializationInfo>::iterator Pos =
      TemplateOrInstantiation.find(Var);
  if (Pos == TemplateOrInstantiation.end())
    return {};

  return Pos->second;
}

void
ASTContext::setInstantiatedFromStaticDataMember(VarDecl *Inst, VarDecl *Tmpl,
                                                TemplateSpecializationKind TSK,
                                          SourceLocation PointOfInstantiation) {
  assert(Inst->isStaticDataMember() && "Not a static data member");
  assert(Tmpl->isStaticDataMember() && "Not a static data member");
  setTemplateOrSpecializationInfo(Inst, new (*this) MemberSpecializationInfo(
                                            Tmpl, TSK, PointOfInstantiation));
}

void
ASTContext::setTemplateOrSpecializationInfo(VarDecl *Inst,
                                            TemplateOrSpecializationInfo TSI) {
  assert(!TemplateOrInstantiation[Inst] &&
         "Already noted what the variable was instantiated from");
  TemplateOrInstantiation[Inst] = TSI;
}

NamedDecl *
ASTContext::getInstantiatedFromUsingDecl(NamedDecl *UUD) {
  return InstantiatedFromUsingDecl.lookup(UUD);
}

void
ASTContext::setInstantiatedFromUsingDecl(NamedDecl *Inst, NamedDecl *Pattern) {
  assert((isa<UsingDecl>(Pattern) ||
          isa<UnresolvedUsingValueDecl>(Pattern) ||
          isa<UnresolvedUsingTypenameDecl>(Pattern)) &&
         "pattern decl is not a using decl");
  assert((isa<UsingDecl>(Inst) ||
          isa<UnresolvedUsingValueDecl>(Inst) ||
          isa<UnresolvedUsingTypenameDecl>(Inst)) &&
         "instantiation did not produce a using decl");
  assert(!InstantiatedFromUsingDecl[Inst] && "pattern already exists");
  InstantiatedFromUsingDecl[Inst] = Pattern;
}

UsingEnumDecl *
ASTContext::getInstantiatedFromUsingEnumDecl(UsingEnumDecl *UUD) {
  return InstantiatedFromUsingEnumDecl.lookup(UUD);
}

void ASTContext::setInstantiatedFromUsingEnumDecl(UsingEnumDecl *Inst,
                                                  UsingEnumDecl *Pattern) {
  assert(!InstantiatedFromUsingEnumDecl[Inst] && "pattern already exists");
  InstantiatedFromUsingEnumDecl[Inst] = Pattern;
}

UsingShadowDecl *
ASTContext::getInstantiatedFromUsingShadowDecl(UsingShadowDecl *Inst) {
  return InstantiatedFromUsingShadowDecl.lookup(Inst);
}

void
ASTContext::setInstantiatedFromUsingShadowDecl(UsingShadowDecl *Inst,
                                               UsingShadowDecl *Pattern) {
  assert(!InstantiatedFromUsingShadowDecl[Inst] && "pattern already exists");
  InstantiatedFromUsingShadowDecl[Inst] = Pattern;
}

FieldDecl *ASTContext::getInstantiatedFromUnnamedFieldDecl(FieldDecl *Field) {
  return InstantiatedFromUnnamedFieldDecl.lookup(Field);
}

void ASTContext::setInstantiatedFromUnnamedFieldDecl(FieldDecl *Inst,
                                                     FieldDecl *Tmpl) {
  assert(!Inst->getDeclName() && "Instantiated field decl is not unnamed");
  assert(!Tmpl->getDeclName() && "Template field decl is not unnamed");
  assert(!InstantiatedFromUnnamedFieldDecl[Inst] &&
         "Already noted what unnamed field was instantiated from");

  InstantiatedFromUnnamedFieldDecl[Inst] = Tmpl;
}

ASTContext::overridden_cxx_method_iterator
ASTContext::overridden_methods_begin(const CXXMethodDecl *Method) const {
  return overridden_methods(Method).begin();
}

ASTContext::overridden_cxx_method_iterator
ASTContext::overridden_methods_end(const CXXMethodDecl *Method) const {
  return overridden_methods(Method).end();
}

unsigned
ASTContext::overridden_methods_size(const CXXMethodDecl *Method) const {
  auto Range = overridden_methods(Method);
  return Range.end() - Range.begin();
}

ASTContext::overridden_method_range
ASTContext::overridden_methods(const CXXMethodDecl *Method) const {
  llvm::DenseMap<const CXXMethodDecl *, CXXMethodVector>::const_iterator Pos =
      OverriddenMethods.find(Method->getCanonicalDecl());
  if (Pos == OverriddenMethods.end())
    return overridden_method_range(nullptr, nullptr);
  return overridden_method_range(Pos->second.begin(), Pos->second.end());
}

void ASTContext::addOverriddenMethod(const CXXMethodDecl *Method,
                                     const CXXMethodDecl *Overridden) {
  assert(Method->isCanonicalDecl() && Overridden->isCanonicalDecl());
  OverriddenMethods[Method].push_back(Overridden);
}

void ASTContext::getOverriddenMethods(
                      const NamedDecl *D,
                      SmallVectorImpl<const NamedDecl *> &Overridden) const {
  assert(D);

  if (const auto *CXXMethod = dyn_cast<CXXMethodDecl>(D)) {
    Overridden.append(overridden_methods_begin(CXXMethod),
                      overridden_methods_end(CXXMethod));
    return;
  }

  const auto *Method = dyn_cast<ObjCMethodDecl>(D);
  if (!Method)
    return;

  SmallVector<const ObjCMethodDecl *, 8> OverDecls;
  Method->getOverriddenMethods(OverDecls);
  Overridden.append(OverDecls.begin(), OverDecls.end());
}

void ASTContext::addedLocalImportDecl(ImportDecl *Import) {
  assert(!Import->getNextLocalImport() &&
         "Import declaration already in the chain");
  assert(!Import->isFromASTFile() && "Non-local import declaration");
  if (!FirstLocalImport) {
    FirstLocalImport = Import;
    LastLocalImport = Import;
    return;
  }

  LastLocalImport->setNextLocalImport(Import);
  LastLocalImport = Import;
}

//===----------------------------------------------------------------------===//
//                         Type Sizing and Analysis
//===----------------------------------------------------------------------===//

/// getFloatTypeSemantics - Return the APFloat 'semantics' for the specified
/// scalar floating point type.
const llvm::fltSemantics &ASTContext::getFloatTypeSemantics(QualType T) const {
  switch (T->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Not a floating point type!");
  case BuiltinType::BFloat16:
    return Target->getBFloat16Format();
  case BuiltinType::Float16:
    return Target->getHalfFormat();
  case BuiltinType::Half:
    return Target->getHalfFormat();
  case BuiltinType::Float:      return Target->getFloatFormat();
  case BuiltinType::Double:     return Target->getDoubleFormat();
  case BuiltinType::Ibm128:
    return Target->getIbm128Format();
  case BuiltinType::LongDouble:
    if (getLangOpts().OpenMP && getLangOpts().OpenMPIsTargetDevice)
      return AuxTarget->getLongDoubleFormat();
    return Target->getLongDoubleFormat();
  case BuiltinType::Float128:
    if (getLangOpts().OpenMP && getLangOpts().OpenMPIsTargetDevice)
      return AuxTarget->getFloat128Format();
    return Target->getFloat128Format();
  }
}

CharUnits ASTContext::getDeclAlign(const Decl *D, bool ForAlignof) const {
  unsigned Align = Target->getCharWidth();

  const unsigned AlignFromAttr = D->getMaxAlignment();
  if (AlignFromAttr)
    Align = AlignFromAttr;

  // __attribute__((aligned)) can increase or decrease alignment
  // *except* on a struct or struct member, where it only increases
  // alignment unless 'packed' is also specified.
  //
  // It is an error for alignas to decrease alignment, so we can
  // ignore that possibility;  Sema should diagnose it.
  bool UseAlignAttrOnly;
  if (const FieldDecl *FD = dyn_cast<FieldDecl>(D))
    UseAlignAttrOnly =
        FD->hasAttr<PackedAttr>() || FD->getParent()->hasAttr<PackedAttr>();
  else
    UseAlignAttrOnly = AlignFromAttr != 0;
  // If we're using the align attribute only, just ignore everything
  // else about the declaration and its type.
  if (UseAlignAttrOnly) {
    // do nothing
  } else if (const auto *VD = dyn_cast<ValueDecl>(D)) {
    QualType T = VD->getType();
    if (const auto *RT = T->getAs<ReferenceType>()) {
      if (ForAlignof)
        T = RT->getPointeeType();
      else
        T = getPointerType(RT->getPointeeType());
    }
    QualType BaseT = getBaseElementType(T);
    if (T->isFunctionType())
      Align = getTypeInfoImpl(T.getTypePtr()).Align;
    else if (!BaseT->isIncompleteType()) {
      // Adjust alignments of declarations with array type by the
      // large-array alignment on the target.
      if (const ArrayType *arrayType = getAsArrayType(T)) {
        unsigned MinWidth = Target->getLargeArrayMinWidth();
        if (!ForAlignof && MinWidth) {
          if (isa<VariableArrayType>(arrayType))
            Align = std::max(Align, Target->getLargeArrayAlign());
          else if (isa<ConstantArrayType>(arrayType) &&
                   MinWidth <= getTypeSize(cast<ConstantArrayType>(arrayType)))
            Align = std::max(Align, Target->getLargeArrayAlign());
        }
      }
      Align = std::max(Align, getPreferredTypeAlign(T.getTypePtr()));
      if (BaseT.getQualifiers().hasUnaligned())
        Align = Target->getCharWidth();
    }

    // Ensure miminum alignment for global variables.
    if (const auto *VD = dyn_cast<VarDecl>(D))
      if (VD->hasGlobalStorage() && !ForAlignof) {
        uint64_t TypeSize =
            !BaseT->isIncompleteType() ? getTypeSize(T.getTypePtr()) : 0;
        Align = std::max(Align, getMinGlobalAlignOfVar(TypeSize, VD));
      }

    // Fields can be subject to extra alignment constraints, like if
    // the field is packed, the struct is packed, or the struct has a
    // a max-field-alignment constraint (#pragma pack).  So calculate
    // the actual alignment of the field within the struct, and then
    // (as we're expected to) constrain that by the alignment of the type.
    if (const auto *Field = dyn_cast<FieldDecl>(VD)) {
      const RecordDecl *Parent = Field->getParent();
      // We can only produce a sensible answer if the record is valid.
      if (!Parent->isInvalidDecl()) {
        const ASTRecordLayout &Layout = getASTRecordLayout(Parent);

        // Start with the record's overall alignment.
        unsigned FieldAlign = toBits(Layout.getAlignment());

        // Use the GCD of that and the offset within the record.
        uint64_t Offset = Layout.getFieldOffset(Field->getFieldIndex());
        if (Offset > 0) {
          // Alignment is always a power of 2, so the GCD will be a power of 2,
          // which means we get to do this crazy thing instead of Euclid's.
          uint64_t LowBitOfOffset = Offset & (~Offset + 1);
          if (LowBitOfOffset < FieldAlign)
            FieldAlign = static_cast<unsigned>(LowBitOfOffset);
        }

        Align = std::min(Align, FieldAlign);
      }
    }
  }

  // Some targets have hard limitation on the maximum requestable alignment in
  // aligned attribute for static variables.
  const unsigned MaxAlignedAttr = getTargetInfo().getMaxAlignedAttribute();
  const auto *VD = dyn_cast<VarDecl>(D);
  if (MaxAlignedAttr && VD && VD->getStorageClass() == SC_Static)
    Align = std::min(Align, MaxAlignedAttr);

  return toCharUnitsFromBits(Align);
}

CharUnits ASTContext::getExnObjectAlignment() const {
  return toCharUnitsFromBits(Target->getExnObjectAlignment());
}

// getTypeInfoDataSizeInChars - Return the size of a type, in
// chars. If the type is a record, its data size is returned.  This is
// the size of the memcpy that's performed when assigning this type
// using a trivial copy/move assignment operator.
TypeInfoChars ASTContext::getTypeInfoDataSizeInChars(QualType T) const {
  TypeInfoChars Info = getTypeInfoInChars(T);

  // In C++, objects can sometimes be allocated into the tail padding
  // of a base-class subobject.  We decide whether that's possible
  // during class layout, so here we can just trust the layout results.
  if (getLangOpts().CPlusPlus) {
    if (const auto *RT = T->getAs<RecordType>();
        RT && !RT->getDecl()->isInvalidDecl()) {
      const ASTRecordLayout &layout = getASTRecordLayout(RT->getDecl());
      Info.Width = layout.getDataSize();
    }
  }

  return Info;
}

/// getConstantArrayInfoInChars - Performing the computation in CharUnits
/// instead of in bits prevents overflowing the uint64_t for some large arrays.
TypeInfoChars
static getConstantArrayInfoInChars(const ASTContext &Context,
                                   const ConstantArrayType *CAT) {
  TypeInfoChars EltInfo = Context.getTypeInfoInChars(CAT->getElementType());
  uint64_t Size = CAT->getZExtSize();
  assert((Size == 0 || static_cast<uint64_t>(EltInfo.Width.getQuantity()) <=
              (uint64_t)(-1)/Size) &&
         "Overflow in array type char size evaluation");
  uint64_t Width = EltInfo.Width.getQuantity() * Size;
  unsigned Align = EltInfo.Align.getQuantity();
  if (!Context.getTargetInfo().getCXXABI().isMicrosoft() ||
      Context.getTargetInfo().getPointerWidth(LangAS::Default) == 64)
    Width = llvm::alignTo(Width, Align);
  return TypeInfoChars(CharUnits::fromQuantity(Width),
                       CharUnits::fromQuantity(Align),
                       EltInfo.AlignRequirement);
}

TypeInfoChars ASTContext::getTypeInfoInChars(const Type *T) const {
  if (const auto *CAT = dyn_cast<ConstantArrayType>(T))
    return getConstantArrayInfoInChars(*this, CAT);
  TypeInfo Info = getTypeInfo(T);
  return TypeInfoChars(toCharUnitsFromBits(Info.Width),
                       toCharUnitsFromBits(Info.Align), Info.AlignRequirement);
}

TypeInfoChars ASTContext::getTypeInfoInChars(QualType T) const {
  return getTypeInfoInChars(T.getTypePtr());
}

bool ASTContext::isPromotableIntegerType(QualType T) const {
  // HLSL doesn't promote all small integer types to int, it
  // just uses the rank-based promotion rules for all types.
  if (getLangOpts().HLSL)
    return false;

  if (const auto *BT = T->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Bool:
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::SChar:
    case BuiltinType::UChar:
    case BuiltinType::Short:
    case BuiltinType::UShort:
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
    case BuiltinType::Char8:
    case BuiltinType::Char16:
    case BuiltinType::Char32:
      return true;
    default:
      return false;
    }

  // Enumerated types are promotable to their compatible integer types
  // (C99 6.3.1.1) a.k.a. its underlying type (C++ [conv.prom]p2).
  if (const auto *ET = T->getAs<EnumType>()) {
    if (T->isDependentType() || ET->getDecl()->getPromotionType().isNull() ||
        ET->getDecl()->isScoped())
      return false;

    return true;
  }

  return false;
}

bool ASTContext::isAlignmentRequired(const Type *T) const {
  return getTypeInfo(T).AlignRequirement != AlignRequirementKind::None;
}

bool ASTContext::isAlignmentRequired(QualType T) const {
  return isAlignmentRequired(T.getTypePtr());
}

unsigned ASTContext::getTypeAlignIfKnown(QualType T,
                                         bool NeedsPreferredAlignment) const {
  // An alignment on a typedef overrides anything else.
  if (const auto *TT = T->getAs<TypedefType>())
    if (unsigned Align = TT->getDecl()->getMaxAlignment())
      return Align;

  // If we have an (array of) complete type, we're done.
  T = getBaseElementType(T);
  if (!T->isIncompleteType())
    return NeedsPreferredAlignment ? getPreferredTypeAlign(T) : getTypeAlign(T);

  // If we had an array type, its element type might be a typedef
  // type with an alignment attribute.
  if (const auto *TT = T->getAs<TypedefType>())
    if (unsigned Align = TT->getDecl()->getMaxAlignment())
      return Align;

  // Otherwise, see if the declaration of the type had an attribute.
  if (const auto *TT = T->getAs<TagType>())
    return TT->getDecl()->getMaxAlignment();

  return 0;
}

TypeInfo ASTContext::getTypeInfo(const Type *T) const {
  TypeInfoMap::iterator I = MemoizedTypeInfo.find(T);
  if (I != MemoizedTypeInfo.end())
    return I->second;

  // This call can invalidate MemoizedTypeInfo[T], so we need a second lookup.
  TypeInfo TI = getTypeInfoImpl(T);
  MemoizedTypeInfo[T] = TI;
  return TI;
}

/// getTypeInfoImpl - Return the size of the specified type, in bits.  This
/// method does not work on incomplete types.
///
/// FIXME: Pointers into different addr spaces could have different sizes and
/// alignment requirements: getPointerInfo should take an AddrSpace, this
/// should take a QualType, &c.
TypeInfo ASTContext::getTypeInfoImpl(const Type *T) const {
  uint64_t Width = 0;
  unsigned Align = 8;
  AlignRequirementKind AlignRequirement = AlignRequirementKind::None;
  LangAS AS = LangAS::Default;
  switch (T->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base)                       \
  case Type::Class:                                                            \
  assert(!T->isDependentType() && "should not see dependent types here");      \
  return getTypeInfo(cast<Class##Type>(T)->desugar().getTypePtr());
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("Should not see dependent types");

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    // GCC extension: alignof(function) = 32 bits
    Width = 0;
    Align = 32;
    break;

  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::ConstantArray:
  case Type::ArrayParameter: {
    // Model non-constant sized arrays as size zero, but track the alignment.
    uint64_t Size = 0;
    if (const auto *CAT = dyn_cast<ConstantArrayType>(T))
      Size = CAT->getZExtSize();

    TypeInfo EltInfo = getTypeInfo(cast<ArrayType>(T)->getElementType());
    assert((Size == 0 || EltInfo.Width <= (uint64_t)(-1) / Size) &&
           "Overflow in array type bit size evaluation");
    Width = EltInfo.Width * Size;
    Align = EltInfo.Align;
    AlignRequirement = EltInfo.AlignRequirement;
    if (!getTargetInfo().getCXXABI().isMicrosoft() ||
        getTargetInfo().getPointerWidth(LangAS::Default) == 64)
      Width = llvm::alignTo(Width, Align);
    break;
  }

  case Type::ExtVector:
  case Type::Vector: {
    const auto *VT = cast<VectorType>(T);
    TypeInfo EltInfo = getTypeInfo(VT->getElementType());
    Width = VT->isExtVectorBoolType() ? VT->getNumElements()
                                      : EltInfo.Width * VT->getNumElements();
    // Enforce at least byte size and alignment.
    Width = std::max<unsigned>(8, Width);
    Align = std::max<unsigned>(8, Width);

    // If the alignment is not a power of 2, round up to the next power of 2.
    // This happens for non-power-of-2 length vectors.
    if (Align & (Align-1)) {
      Align = llvm::bit_ceil(Align);
      Width = llvm::alignTo(Width, Align);
    }
    // Adjust the alignment based on the target max.
    uint64_t TargetVectorAlign = Target->getMaxVectorAlign();
    if (TargetVectorAlign && TargetVectorAlign < Align)
      Align = TargetVectorAlign;
    if (VT->getVectorKind() == VectorKind::SveFixedLengthData)
      // Adjust the alignment for fixed-length SVE vectors. This is important
      // for non-power-of-2 vector lengths.
      Align = 128;
    else if (VT->getVectorKind() == VectorKind::SveFixedLengthPredicate)
      // Adjust the alignment for fixed-length SVE predicates.
      Align = 16;
    else if (VT->getVectorKind() == VectorKind::RVVFixedLengthData ||
             VT->getVectorKind() == VectorKind::RVVFixedLengthMask)
      // Adjust the alignment for fixed-length RVV vectors.
      Align = std::min<unsigned>(64, Width);
    break;
  }

  case Type::ConstantMatrix: {
    const auto *MT = cast<ConstantMatrixType>(T);
    TypeInfo ElementInfo = getTypeInfo(MT->getElementType());
    // The internal layout of a matrix value is implementation defined.
    // Initially be ABI compatible with arrays with respect to alignment and
    // size.
    Width = ElementInfo.Width * MT->getNumRows() * MT->getNumColumns();
    Align = ElementInfo.Align;
    break;
  }

  case Type::Builtin:
    switch (cast<BuiltinType>(T)->getKind()) {
    default: llvm_unreachable("Unknown builtin type!");
    case BuiltinType::Void:
      // GCC extension: alignof(void) = 8 bits.
      Width = 0;
      Align = 8;
      break;
    case BuiltinType::Bool:
      Width = Target->getBoolWidth();
      Align = Target->getBoolAlign();
      break;
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
    case BuiltinType::Char8:
      Width = Target->getCharWidth();
      Align = Target->getCharAlign();
      break;
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
      Width = Target->getWCharWidth();
      Align = Target->getWCharAlign();
      break;
    case BuiltinType::Char16:
      Width = Target->getChar16Width();
      Align = Target->getChar16Align();
      break;
    case BuiltinType::Char32:
      Width = Target->getChar32Width();
      Align = Target->getChar32Align();
      break;
    case BuiltinType::UShort:
    case BuiltinType::Short:
      Width = Target->getShortWidth();
      Align = Target->getShortAlign();
      break;
    case BuiltinType::UInt:
    case BuiltinType::Int:
      Width = Target->getIntWidth();
      Align = Target->getIntAlign();
      break;
    case BuiltinType::ULong:
    case BuiltinType::Long:
      Width = Target->getLongWidth();
      Align = Target->getLongAlign();
      break;
    case BuiltinType::ULongLong:
    case BuiltinType::LongLong:
      Width = Target->getLongLongWidth();
      Align = Target->getLongLongAlign();
      break;
    case BuiltinType::Int128:
    case BuiltinType::UInt128:
      Width = 128;
      Align = Target->getInt128Align();
      break;
    case BuiltinType::ShortAccum:
    case BuiltinType::UShortAccum:
    case BuiltinType::SatShortAccum:
    case BuiltinType::SatUShortAccum:
      Width = Target->getShortAccumWidth();
      Align = Target->getShortAccumAlign();
      break;
    case BuiltinType::Accum:
    case BuiltinType::UAccum:
    case BuiltinType::SatAccum:
    case BuiltinType::SatUAccum:
      Width = Target->getAccumWidth();
      Align = Target->getAccumAlign();
      break;
    case BuiltinType::LongAccum:
    case BuiltinType::ULongAccum:
    case BuiltinType::SatLongAccum:
    case BuiltinType::SatULongAccum:
      Width = Target->getLongAccumWidth();
      Align = Target->getLongAccumAlign();
      break;
    case BuiltinType::ShortFract:
    case BuiltinType::UShortFract:
    case BuiltinType::SatShortFract:
    case BuiltinType::SatUShortFract:
      Width = Target->getShortFractWidth();
      Align = Target->getShortFractAlign();
      break;
    case BuiltinType::Fract:
    case BuiltinType::UFract:
    case BuiltinType::SatFract:
    case BuiltinType::SatUFract:
      Width = Target->getFractWidth();
      Align = Target->getFractAlign();
      break;
    case BuiltinType::LongFract:
    case BuiltinType::ULongFract:
    case BuiltinType::SatLongFract:
    case BuiltinType::SatULongFract:
      Width = Target->getLongFractWidth();
      Align = Target->getLongFractAlign();
      break;
    case BuiltinType::BFloat16:
      if (Target->hasBFloat16Type()) {
        Width = Target->getBFloat16Width();
        Align = Target->getBFloat16Align();
      } else if ((getLangOpts().SYCLIsDevice ||
                  (getLangOpts().OpenMP &&
                   getLangOpts().OpenMPIsTargetDevice)) &&
                 AuxTarget->hasBFloat16Type()) {
        Width = AuxTarget->getBFloat16Width();
        Align = AuxTarget->getBFloat16Align();
      }
      break;
    case BuiltinType::Float16:
    case BuiltinType::Half:
      if (Target->hasFloat16Type() || !getLangOpts().OpenMP ||
          !getLangOpts().OpenMPIsTargetDevice) {
        Width = Target->getHalfWidth();
        Align = Target->getHalfAlign();
      } else {
        assert(getLangOpts().OpenMP && getLangOpts().OpenMPIsTargetDevice &&
               "Expected OpenMP device compilation.");
        Width = AuxTarget->getHalfWidth();
        Align = AuxTarget->getHalfAlign();
      }
      break;
    case BuiltinType::Float:
      Width = Target->getFloatWidth();
      Align = Target->getFloatAlign();
      break;
    case BuiltinType::Double:
      Width = Target->getDoubleWidth();
      Align = Target->getDoubleAlign();
      break;
    case BuiltinType::Ibm128:
      Width = Target->getIbm128Width();
      Align = Target->getIbm128Align();
      break;
    case BuiltinType::LongDouble:
      if (getLangOpts().OpenMP && getLangOpts().OpenMPIsTargetDevice &&
          (Target->getLongDoubleWidth() != AuxTarget->getLongDoubleWidth() ||
           Target->getLongDoubleAlign() != AuxTarget->getLongDoubleAlign())) {
        Width = AuxTarget->getLongDoubleWidth();
        Align = AuxTarget->getLongDoubleAlign();
      } else {
        Width = Target->getLongDoubleWidth();
        Align = Target->getLongDoubleAlign();
      }
      break;
    case BuiltinType::Float128:
      if (Target->hasFloat128Type() || !getLangOpts().OpenMP ||
          !getLangOpts().OpenMPIsTargetDevice) {
        Width = Target->getFloat128Width();
        Align = Target->getFloat128Align();
      } else {
        assert(getLangOpts().OpenMP && getLangOpts().OpenMPIsTargetDevice &&
               "Expected OpenMP device compilation.");
        Width = AuxTarget->getFloat128Width();
        Align = AuxTarget->getFloat128Align();
      }
      break;
    case BuiltinType::NullPtr:
      // C++ 3.9.1p11: sizeof(nullptr_t) == sizeof(void*)
      Width = Target->getPointerWidth(LangAS::Default);
      Align = Target->getPointerAlign(LangAS::Default);
      break;
    case BuiltinType::ObjCId:
    case BuiltinType::ObjCClass:
    case BuiltinType::ObjCSel:
      Width = Target->getPointerWidth(LangAS::Default);
      Align = Target->getPointerAlign(LangAS::Default);
      break;
    case BuiltinType::OCLSampler:
    case BuiltinType::OCLEvent:
    case BuiltinType::OCLClkEvent:
    case BuiltinType::OCLQueue:
    case BuiltinType::OCLReserveID:
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
    case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
      AS = Target->getOpenCLTypeAddrSpace(getOpenCLTypeKind(T));
      Width = Target->getPointerWidth(AS);
      Align = Target->getPointerAlign(AS);
      break;
    // The SVE types are effectively target-specific.  The length of an
    // SVE_VECTOR_TYPE is only known at runtime, but it is always a multiple
    // of 128 bits.  There is one predicate bit for each vector byte, so the
    // length of an SVE_PREDICATE_TYPE is always a multiple of 16 bits.
    //
    // Because the length is only known at runtime, we use a dummy value
    // of 0 for the static length.  The alignment values are those defined
    // by the Procedure Call Standard for the Arm Architecture.
#define SVE_VECTOR_TYPE(Name, MangledName, Id, SingletonId, NumEls, ElBits,    \
                        IsSigned, IsFP, IsBF)                                  \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 128;                                                               \
    break;
#define SVE_PREDICATE_TYPE(Name, MangledName, Id, SingletonId, NumEls)         \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 16;                                                                \
    break;
#define SVE_OPAQUE_TYPE(Name, MangledName, Id, SingletonId)                    \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 16;                                                                \
    break;
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size)                                        \
  case BuiltinType::Id:                                                        \
    Width = Size;                                                              \
    Align = Size;                                                              \
    break;
#include "clang/Basic/PPCTypes.def"
#define RVV_VECTOR_TYPE(Name, Id, SingletonId, ElKind, ElBits, NF, IsSigned,   \
                        IsFP, IsBF)                                            \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = ElBits;                                                            \
    break;
#define RVV_PREDICATE_TYPE(Name, Id, SingletonId, ElKind)                      \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 8;                                                                 \
    break;
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId)                                       \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 8;                                                                 \
    break;
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_OPAQUE_PTR_TYPE(NAME, MANGLEDNAME, AS, WIDTH, ALIGN, ID,        \
                               SINGLETONID)                                    \
  case BuiltinType::ID:                                                        \
    Width = WIDTH;                                                             \
    Align = ALIGN;                                                             \
    break;
#include "clang/Basic/AMDGPUTypes.def"
    }
    break;
  case Type::ObjCObjectPointer:
    Width = Target->getPointerWidth(LangAS::Default);
    Align = Target->getPointerAlign(LangAS::Default);
    break;
  case Type::BlockPointer:
    AS = cast<BlockPointerType>(T)->getPointeeType().getAddressSpace();
    Width = Target->getPointerWidth(AS);
    Align = Target->getPointerAlign(AS);
    break;
  case Type::LValueReference:
  case Type::RValueReference:
    // alignof and sizeof should never enter this code path here, so we go
    // the pointer route.
    AS = cast<ReferenceType>(T)->getPointeeType().getAddressSpace();
    Width = Target->getPointerWidth(AS);
    Align = Target->getPointerAlign(AS);
    break;
  case Type::Pointer:
    AS = cast<PointerType>(T)->getPointeeType().getAddressSpace();
    Width = Target->getPointerWidth(AS);
    Align = Target->getPointerAlign(AS);
    break;
  case Type::MemberPointer: {
    const auto *MPT = cast<MemberPointerType>(T);
    CXXABI::MemberPointerInfo MPI = ABI->getMemberPointerInfo(MPT);
    Width = MPI.Width;
    Align = MPI.Align;
    break;
  }
  case Type::Complex: {
    // Complex types have the same alignment as their elements, but twice the
    // size.
    TypeInfo EltInfo = getTypeInfo(cast<ComplexType>(T)->getElementType());
    Width = EltInfo.Width * 2;
    Align = EltInfo.Align;
    break;
  }
  case Type::ObjCObject:
    return getTypeInfo(cast<ObjCObjectType>(T)->getBaseType().getTypePtr());
  case Type::Adjusted:
  case Type::Decayed:
    return getTypeInfo(cast<AdjustedType>(T)->getAdjustedType().getTypePtr());
  case Type::ObjCInterface: {
    const auto *ObjCI = cast<ObjCInterfaceType>(T);
    if (ObjCI->getDecl()->isInvalidDecl()) {
      Width = 8;
      Align = 8;
      break;
    }
    const ASTRecordLayout &Layout = getASTObjCInterfaceLayout(ObjCI->getDecl());
    Width = toBits(Layout.getSize());
    Align = toBits(Layout.getAlignment());
    break;
  }
  case Type::BitInt: {
    const auto *EIT = cast<BitIntType>(T);
    Align = Target->getBitIntAlign(EIT->getNumBits());
    Width = Target->getBitIntWidth(EIT->getNumBits());
    break;
  }
  case Type::Record:
  case Type::Enum: {
    const auto *TT = cast<TagType>(T);

    if (TT->getDecl()->isInvalidDecl()) {
      Width = 8;
      Align = 8;
      break;
    }

    if (const auto *ET = dyn_cast<EnumType>(TT)) {
      const EnumDecl *ED = ET->getDecl();
      TypeInfo Info =
          getTypeInfo(ED->getIntegerType()->getUnqualifiedDesugaredType());
      if (unsigned AttrAlign = ED->getMaxAlignment()) {
        Info.Align = AttrAlign;
        Info.AlignRequirement = AlignRequirementKind::RequiredByEnum;
      }
      return Info;
    }

    const auto *RT = cast<RecordType>(TT);
    const RecordDecl *RD = RT->getDecl();
    const ASTRecordLayout &Layout = getASTRecordLayout(RD);
    Width = toBits(Layout.getSize());
    Align = toBits(Layout.getAlignment());
    AlignRequirement = RD->hasAttr<AlignedAttr>()
                           ? AlignRequirementKind::RequiredByRecord
                           : AlignRequirementKind::None;
    break;
  }

  case Type::SubstTemplateTypeParm:
    return getTypeInfo(cast<SubstTemplateTypeParmType>(T)->
                       getReplacementType().getTypePtr());

  case Type::Auto:
  case Type::DeducedTemplateSpecialization: {
    const auto *A = cast<DeducedType>(T);
    assert(!A->getDeducedType().isNull() &&
           "cannot request the size of an undeduced or dependent auto type");
    return getTypeInfo(A->getDeducedType().getTypePtr());
  }

  case Type::Paren:
    return getTypeInfo(cast<ParenType>(T)->getInnerType().getTypePtr());

  case Type::MacroQualified:
    return getTypeInfo(
        cast<MacroQualifiedType>(T)->getUnderlyingType().getTypePtr());

  case Type::ObjCTypeParam:
    return getTypeInfo(cast<ObjCTypeParamType>(T)->desugar().getTypePtr());

  case Type::Using:
    return getTypeInfo(cast<UsingType>(T)->desugar().getTypePtr());

  case Type::Typedef: {
    const auto *TT = cast<TypedefType>(T);
    TypeInfo Info = getTypeInfo(TT->desugar().getTypePtr());
    // If the typedef has an aligned attribute on it, it overrides any computed
    // alignment we have.  This violates the GCC documentation (which says that
    // attribute(aligned) can only round up) but matches its implementation.
    if (unsigned AttrAlign = TT->getDecl()->getMaxAlignment()) {
      Align = AttrAlign;
      AlignRequirement = AlignRequirementKind::RequiredByTypedef;
    } else {
      Align = Info.Align;
      AlignRequirement = Info.AlignRequirement;
    }
    Width = Info.Width;
    break;
  }

  case Type::Elaborated:
    return getTypeInfo(cast<ElaboratedType>(T)->getNamedType().getTypePtr());

  case Type::Attributed:
    return getTypeInfo(
                  cast<AttributedType>(T)->getEquivalentType().getTypePtr());

  case Type::CountAttributed:
    return getTypeInfo(cast<CountAttributedType>(T)->desugar().getTypePtr());

  case Type::BTFTagAttributed:
    return getTypeInfo(
        cast<BTFTagAttributedType>(T)->getWrappedType().getTypePtr());

  case Type::Atomic: {
    // Start with the base type information.
    TypeInfo Info = getTypeInfo(cast<AtomicType>(T)->getValueType());
    Width = Info.Width;
    Align = Info.Align;

    if (!Width) {
      // An otherwise zero-sized type should still generate an
      // atomic operation.
      Width = Target->getCharWidth();
      assert(Align);
    } else if (Width <= Target->getMaxAtomicPromoteWidth()) {
      // If the size of the type doesn't exceed the platform's max
      // atomic promotion width, make the size and alignment more
      // favorable to atomic operations:

      // Round the size up to a power of 2.
      Width = llvm::bit_ceil(Width);

      // Set the alignment equal to the size.
      Align = static_cast<unsigned>(Width);
    }
  }
  break;

  case Type::Pipe:
    Width = Target->getPointerWidth(LangAS::opencl_global);
    Align = Target->getPointerAlign(LangAS::opencl_global);
    break;
  }

  assert(llvm::isPowerOf2_32(Align) && "Alignment must be power of 2");
  return TypeInfo(Width, Align, AlignRequirement);
}

unsigned ASTContext::getTypeUnadjustedAlign(const Type *T) const {
  UnadjustedAlignMap::iterator I = MemoizedUnadjustedAlign.find(T);
  if (I != MemoizedUnadjustedAlign.end())
    return I->second;

  unsigned UnadjustedAlign;
  if (const auto *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    const ASTRecordLayout &Layout = getASTRecordLayout(RD);
    UnadjustedAlign = toBits(Layout.getUnadjustedAlignment());
  } else if (const auto *ObjCI = T->getAs<ObjCInterfaceType>()) {
    const ASTRecordLayout &Layout = getASTObjCInterfaceLayout(ObjCI->getDecl());
    UnadjustedAlign = toBits(Layout.getUnadjustedAlignment());
  } else {
    UnadjustedAlign = getTypeAlign(T->getUnqualifiedDesugaredType());
  }

  MemoizedUnadjustedAlign[T] = UnadjustedAlign;
  return UnadjustedAlign;
}

unsigned ASTContext::getOpenMPDefaultSimdAlign(QualType T) const {
  unsigned SimdAlign = llvm::OpenMPIRBuilder::getOpenMPDefaultSimdAlign(
      getTargetInfo().getTriple(), Target->getTargetOpts().FeatureMap);
  return SimdAlign;
}

/// toCharUnitsFromBits - Convert a size in bits to a size in characters.
CharUnits ASTContext::toCharUnitsFromBits(int64_t BitSize) const {
  return CharUnits::fromQuantity(BitSize / getCharWidth());
}

/// toBits - Convert a size in characters to a size in characters.
int64_t ASTContext::toBits(CharUnits CharSize) const {
  return CharSize.getQuantity() * getCharWidth();
}

/// getTypeSizeInChars - Return the size of the specified type, in characters.
/// This method does not work on incomplete types.
CharUnits ASTContext::getTypeSizeInChars(QualType T) const {
  return getTypeInfoInChars(T).Width;
}
CharUnits ASTContext::getTypeSizeInChars(const Type *T) const {
  return getTypeInfoInChars(T).Width;
}

/// getTypeAlignInChars - Return the ABI-specified alignment of a type, in
/// characters. This method does not work on incomplete types.
CharUnits ASTContext::getTypeAlignInChars(QualType T) const {
  return toCharUnitsFromBits(getTypeAlign(T));
}
CharUnits ASTContext::getTypeAlignInChars(const Type *T) const {
  return toCharUnitsFromBits(getTypeAlign(T));
}

/// getTypeUnadjustedAlignInChars - Return the ABI-specified alignment of a
/// type, in characters, before alignment adjustments. This method does
/// not work on incomplete types.
CharUnits ASTContext::getTypeUnadjustedAlignInChars(QualType T) const {
  return toCharUnitsFromBits(getTypeUnadjustedAlign(T));
}
CharUnits ASTContext::getTypeUnadjustedAlignInChars(const Type *T) const {
  return toCharUnitsFromBits(getTypeUnadjustedAlign(T));
}

/// getPreferredTypeAlign - Return the "preferred" alignment of the specified
/// type for the current target in bits.  This can be different than the ABI
/// alignment in cases where it is beneficial for performance or backwards
/// compatibility preserving to overalign a data type. (Note: despite the name,
/// the preferred alignment is ABI-impacting, and not an optimization.)
unsigned ASTContext::getPreferredTypeAlign(const Type *T) const {
  TypeInfo TI = getTypeInfo(T);
  unsigned ABIAlign = TI.Align;

  T = T->getBaseElementTypeUnsafe();

  // The preferred alignment of member pointers is that of a pointer.
  if (T->isMemberPointerType())
    return getPreferredTypeAlign(getPointerDiffType().getTypePtr());

  if (!Target->allowsLargerPreferedTypeAlignment())
    return ABIAlign;

  if (const auto *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();

    // When used as part of a typedef, or together with a 'packed' attribute,
    // the 'aligned' attribute can be used to decrease alignment. Note that the
    // 'packed' case is already taken into consideration when computing the
    // alignment, we only need to handle the typedef case here.
    if (TI.AlignRequirement == AlignRequirementKind::RequiredByTypedef ||
        RD->isInvalidDecl())
      return ABIAlign;

    unsigned PreferredAlign = static_cast<unsigned>(
        toBits(getASTRecordLayout(RD).PreferredAlignment));
    assert(PreferredAlign >= ABIAlign &&
           "PreferredAlign should be at least as large as ABIAlign.");
    return PreferredAlign;
  }

  // Double (and, for targets supporting AIX `power` alignment, long double) and
  // long long should be naturally aligned (despite requiring less alignment) if
  // possible.
  if (const auto *CT = T->getAs<ComplexType>())
    T = CT->getElementType().getTypePtr();
  if (const auto *ET = T->getAs<EnumType>())
    T = ET->getDecl()->getIntegerType().getTypePtr();
  if (T->isSpecificBuiltinType(BuiltinType::Double) ||
      T->isSpecificBuiltinType(BuiltinType::LongLong) ||
      T->isSpecificBuiltinType(BuiltinType::ULongLong) ||
      (T->isSpecificBuiltinType(BuiltinType::LongDouble) &&
       Target->defaultsToAIXPowerAlignment()))
    // Don't increase the alignment if an alignment attribute was specified on a
    // typedef declaration.
    if (!TI.isAlignRequired())
      return std::max(ABIAlign, (unsigned)getTypeSize(T));

  return ABIAlign;
}

/// getTargetDefaultAlignForAttributeAligned - Return the default alignment
/// for __attribute__((aligned)) on this target, to be used if no alignment
/// value is specified.
unsigned ASTContext::getTargetDefaultAlignForAttributeAligned() const {
  return getTargetInfo().getDefaultAlignForAttributeAligned();
}

/// getAlignOfGlobalVar - Return the alignment in bits that should be given
/// to a global variable of the specified type.
unsigned ASTContext::getAlignOfGlobalVar(QualType T, const VarDecl *VD) const {
  uint64_t TypeSize = getTypeSize(T.getTypePtr());
  return std::max(getPreferredTypeAlign(T),
                  getMinGlobalAlignOfVar(TypeSize, VD));
}

/// getAlignOfGlobalVarInChars - Return the alignment in characters that
/// should be given to a global variable of the specified type.
CharUnits ASTContext::getAlignOfGlobalVarInChars(QualType T,
                                                 const VarDecl *VD) const {
  return toCharUnitsFromBits(getAlignOfGlobalVar(T, VD));
}

unsigned ASTContext::getMinGlobalAlignOfVar(uint64_t Size,
                                            const VarDecl *VD) const {
  // Make the default handling as that of a non-weak definition in the
  // current translation unit.
  bool HasNonWeakDef = !VD || (VD->hasDefinition() && !VD->isWeak());
  return getTargetInfo().getMinGlobalAlign(Size, HasNonWeakDef);
}

CharUnits ASTContext::getOffsetOfBaseWithVBPtr(const CXXRecordDecl *RD) const {
  CharUnits Offset = CharUnits::Zero();
  const ASTRecordLayout *Layout = &getASTRecordLayout(RD);
  while (const CXXRecordDecl *Base = Layout->getBaseSharingVBPtr()) {
    Offset += Layout->getBaseClassOffset(Base);
    Layout = &getASTRecordLayout(Base);
  }
  return Offset;
}

CharUnits ASTContext::getMemberPointerPathAdjustment(const APValue &MP) const {
  const ValueDecl *MPD = MP.getMemberPointerDecl();
  CharUnits ThisAdjustment = CharUnits::Zero();
  ArrayRef<const CXXRecordDecl*> Path = MP.getMemberPointerPath();
  bool DerivedMember = MP.isMemberPointerToDerivedMember();
  const CXXRecordDecl *RD = cast<CXXRecordDecl>(MPD->getDeclContext());
  for (unsigned I = 0, N = Path.size(); I != N; ++I) {
    const CXXRecordDecl *Base = RD;
    const CXXRecordDecl *Derived = Path[I];
    if (DerivedMember)
      std::swap(Base, Derived);
    ThisAdjustment += getASTRecordLayout(Derived).getBaseClassOffset(Base);
    RD = Path[I];
  }
  if (DerivedMember)
    ThisAdjustment = -ThisAdjustment;
  return ThisAdjustment;
}

/// DeepCollectObjCIvars -
/// This routine first collects all declared, but not synthesized, ivars in
/// super class and then collects all ivars, including those synthesized for
/// current class. This routine is used for implementation of current class
/// when all ivars, declared and synthesized are known.
void ASTContext::DeepCollectObjCIvars(const ObjCInterfaceDecl *OI,
                                      bool leafClass,
                            SmallVectorImpl<const ObjCIvarDecl*> &Ivars) const {
  if (const ObjCInterfaceDecl *SuperClass = OI->getSuperClass())
    DeepCollectObjCIvars(SuperClass, false, Ivars);
  if (!leafClass) {
    llvm::append_range(Ivars, OI->ivars());
  } else {
    auto *IDecl = const_cast<ObjCInterfaceDecl *>(OI);
    for (const ObjCIvarDecl *Iv = IDecl->all_declared_ivar_begin(); Iv;
         Iv= Iv->getNextIvar())
      Ivars.push_back(Iv);
  }
}

/// CollectInheritedProtocols - Collect all protocols in current class and
/// those inherited by it.
void ASTContext::CollectInheritedProtocols(const Decl *CDecl,
                          llvm::SmallPtrSet<ObjCProtocolDecl*, 8> &Protocols) {
  if (const auto *OI = dyn_cast<ObjCInterfaceDecl>(CDecl)) {
    // We can use protocol_iterator here instead of
    // all_referenced_protocol_iterator since we are walking all categories.
    for (auto *Proto : OI->all_referenced_protocols()) {
      CollectInheritedProtocols(Proto, Protocols);
    }

    // Categories of this Interface.
    for (const auto *Cat : OI->visible_categories())
      CollectInheritedProtocols(Cat, Protocols);

    if (ObjCInterfaceDecl *SD = OI->getSuperClass())
      while (SD) {
        CollectInheritedProtocols(SD, Protocols);
        SD = SD->getSuperClass();
      }
  } else if (const auto *OC = dyn_cast<ObjCCategoryDecl>(CDecl)) {
    for (auto *Proto : OC->protocols()) {
      CollectInheritedProtocols(Proto, Protocols);
    }
  } else if (const auto *OP = dyn_cast<ObjCProtocolDecl>(CDecl)) {
    // Insert the protocol.
    if (!Protocols.insert(
          const_cast<ObjCProtocolDecl *>(OP->getCanonicalDecl())).second)
      return;

    for (auto *Proto : OP->protocols())
      CollectInheritedProtocols(Proto, Protocols);
  }
}

static bool unionHasUniqueObjectRepresentations(const ASTContext &Context,
                                                const RecordDecl *RD,
                                                bool CheckIfTriviallyCopyable) {
  assert(RD->isUnion() && "Must be union type");
  CharUnits UnionSize = Context.getTypeSizeInChars(RD->getTypeForDecl());

  for (const auto *Field : RD->fields()) {
    if (!Context.hasUniqueObjectRepresentations(Field->getType(),
                                                CheckIfTriviallyCopyable))
      return false;
    CharUnits FieldSize = Context.getTypeSizeInChars(Field->getType());
    if (FieldSize != UnionSize)
      return false;
  }
  return !RD->field_empty();
}

static int64_t getSubobjectOffset(const FieldDecl *Field,
                                  const ASTContext &Context,
                                  const clang::ASTRecordLayout & /*Layout*/) {
  return Context.getFieldOffset(Field);
}

static int64_t getSubobjectOffset(const CXXRecordDecl *RD,
                                  const ASTContext &Context,
                                  const clang::ASTRecordLayout &Layout) {
  return Context.toBits(Layout.getBaseClassOffset(RD));
}

static std::optional<int64_t>
structHasUniqueObjectRepresentations(const ASTContext &Context,
                                     const RecordDecl *RD,
                                     bool CheckIfTriviallyCopyable);

static std::optional<int64_t>
getSubobjectSizeInBits(const FieldDecl *Field, const ASTContext &Context,
                       bool CheckIfTriviallyCopyable) {
  if (Field->getType()->isRecordType()) {
    const RecordDecl *RD = Field->getType()->getAsRecordDecl();
    if (!RD->isUnion())
      return structHasUniqueObjectRepresentations(Context, RD,
                                                  CheckIfTriviallyCopyable);
  }

  // A _BitInt type may not be unique if it has padding bits
  // but if it is a bitfield the padding bits are not used.
  bool IsBitIntType = Field->getType()->isBitIntType();
  if (!Field->getType()->isReferenceType() && !IsBitIntType &&
      !Context.hasUniqueObjectRepresentations(Field->getType(),
                                              CheckIfTriviallyCopyable))
    return std::nullopt;

  int64_t FieldSizeInBits =
      Context.toBits(Context.getTypeSizeInChars(Field->getType()));
  if (Field->isBitField()) {
    // If we have explicit padding bits, they don't contribute bits
    // to the actual object representation, so return 0.
    if (Field->isUnnamedBitField())
      return 0;

    int64_t BitfieldSize = Field->getBitWidthValue(Context);
    if (IsBitIntType) {
      if ((unsigned)BitfieldSize >
          cast<BitIntType>(Field->getType())->getNumBits())
        return std::nullopt;
    } else if (BitfieldSize > FieldSizeInBits) {
      return std::nullopt;
    }
    FieldSizeInBits = BitfieldSize;
  } else if (IsBitIntType && !Context.hasUniqueObjectRepresentations(
                                 Field->getType(), CheckIfTriviallyCopyable)) {
    return std::nullopt;
  }
  return FieldSizeInBits;
}

static std::optional<int64_t>
getSubobjectSizeInBits(const CXXRecordDecl *RD, const ASTContext &Context,
                       bool CheckIfTriviallyCopyable) {
  return structHasUniqueObjectRepresentations(Context, RD,
                                              CheckIfTriviallyCopyable);
}

template <typename RangeT>
static std::optional<int64_t> structSubobjectsHaveUniqueObjectRepresentations(
    const RangeT &Subobjects, int64_t CurOffsetInBits,
    const ASTContext &Context, const clang::ASTRecordLayout &Layout,
    bool CheckIfTriviallyCopyable) {
  for (const auto *Subobject : Subobjects) {
    std::optional<int64_t> SizeInBits =
        getSubobjectSizeInBits(Subobject, Context, CheckIfTriviallyCopyable);
    if (!SizeInBits)
      return std::nullopt;
    if (*SizeInBits != 0) {
      int64_t Offset = getSubobjectOffset(Subobject, Context, Layout);
      if (Offset != CurOffsetInBits)
        return std::nullopt;
      CurOffsetInBits += *SizeInBits;
    }
  }
  return CurOffsetInBits;
}

static std::optional<int64_t>
structHasUniqueObjectRepresentations(const ASTContext &Context,
                                     const RecordDecl *RD,
                                     bool CheckIfTriviallyCopyable) {
  assert(!RD->isUnion() && "Must be struct/class type");
  const auto &Layout = Context.getASTRecordLayout(RD);

  int64_t CurOffsetInBits = 0;
  if (const auto *ClassDecl = dyn_cast<CXXRecordDecl>(RD)) {
    if (ClassDecl->isDynamicClass())
      return std::nullopt;

    SmallVector<CXXRecordDecl *, 4> Bases;
    for (const auto &Base : ClassDecl->bases()) {
      // Empty types can be inherited from, and non-empty types can potentially
      // have tail padding, so just make sure there isn't an error.
      Bases.emplace_back(Base.getType()->getAsCXXRecordDecl());
    }

    llvm::sort(Bases, [&](const CXXRecordDecl *L, const CXXRecordDecl *R) {
      return Layout.getBaseClassOffset(L) < Layout.getBaseClassOffset(R);
    });

    std::optional<int64_t> OffsetAfterBases =
        structSubobjectsHaveUniqueObjectRepresentations(
            Bases, CurOffsetInBits, Context, Layout, CheckIfTriviallyCopyable);
    if (!OffsetAfterBases)
      return std::nullopt;
    CurOffsetInBits = *OffsetAfterBases;
  }

  std::optional<int64_t> OffsetAfterFields =
      structSubobjectsHaveUniqueObjectRepresentations(
          RD->fields(), CurOffsetInBits, Context, Layout,
          CheckIfTriviallyCopyable);
  if (!OffsetAfterFields)
    return std::nullopt;
  CurOffsetInBits = *OffsetAfterFields;

  return CurOffsetInBits;
}

bool ASTContext::hasUniqueObjectRepresentations(
    QualType Ty, bool CheckIfTriviallyCopyable) const {
  // C++17 [meta.unary.prop]:
  //   The predicate condition for a template specialization
  //   has_unique_object_representations<T> shall be satisfied if and only if:
  //     (9.1) - T is trivially copyable, and
  //     (9.2) - any two objects of type T with the same value have the same
  //     object representation, where:
  //     - two objects of array or non-union class type are considered to have
  //       the same value if their respective sequences of direct subobjects
  //       have the same values, and
  //     - two objects of union type are considered to have the same value if
  //       they have the same active member and the corresponding members have
  //       the same value.
  //   The set of scalar types for which this condition holds is
  //   implementation-defined. [ Note: If a type has padding bits, the condition
  //   does not hold; otherwise, the condition holds true for unsigned integral
  //   types. -- end note ]
  assert(!Ty.isNull() && "Null QualType sent to unique object rep check");

  // Arrays are unique only if their element type is unique.
  if (Ty->isArrayType())
    return hasUniqueObjectRepresentations(getBaseElementType(Ty),
                                          CheckIfTriviallyCopyable);

  assert((Ty->isVoidType() || !Ty->isIncompleteType()) &&
         "hasUniqueObjectRepresentations should not be called with an "
         "incomplete type");

  // (9.1) - T is trivially copyable...
  if (CheckIfTriviallyCopyable && !Ty.isTriviallyCopyableType(*this))
    return false;

  // All integrals and enums are unique.
  if (Ty->isIntegralOrEnumerationType()) {
    // Except _BitInt types that have padding bits.
    if (const auto *BIT = Ty->getAs<BitIntType>())
      return getTypeSize(BIT) == BIT->getNumBits();

    return true;
  }

  // All other pointers are unique.
  if (Ty->isPointerType())
    return true;

  if (const auto *MPT = Ty->getAs<MemberPointerType>())
    return !ABI->getMemberPointerInfo(MPT).HasPadding;

  if (Ty->isRecordType()) {
    const RecordDecl *Record = Ty->castAs<RecordType>()->getDecl();

    if (Record->isInvalidDecl())
      return false;

    if (Record->isUnion())
      return unionHasUniqueObjectRepresentations(*this, Record,
                                                 CheckIfTriviallyCopyable);

    std::optional<int64_t> StructSize = structHasUniqueObjectRepresentations(
        *this, Record, CheckIfTriviallyCopyable);

    return StructSize && *StructSize == static_cast<int64_t>(getTypeSize(Ty));
  }

  // FIXME: More cases to handle here (list by rsmith):
  // vectors (careful about, eg, vector of 3 foo)
  // _Complex int and friends
  // _Atomic T
  // Obj-C block pointers
  // Obj-C object pointers
  // and perhaps OpenCL's various builtin types (pipe, sampler_t, event_t,
  // clk_event_t, queue_t, reserve_id_t)
  // There're also Obj-C class types and the Obj-C selector type, but I think it
  // makes sense for those to return false here.

  return false;
}

unsigned ASTContext::CountNonClassIvars(const ObjCInterfaceDecl *OI) const {
  unsigned count = 0;
  // Count ivars declared in class extension.
  for (const auto *Ext : OI->known_extensions())
    count += Ext->ivar_size();

  // Count ivar defined in this class's implementation.  This
  // includes synthesized ivars.
  if (ObjCImplementationDecl *ImplDecl = OI->getImplementation())
    count += ImplDecl->ivar_size();

  return count;
}

bool ASTContext::isSentinelNullExpr(const Expr *E) {
  if (!E)
    return false;

  // nullptr_t is always treated as null.
  if (E->getType()->isNullPtrType()) return true;

  if (E->getType()->isAnyPointerType() &&
      E->IgnoreParenCasts()->isNullPointerConstant(*this,
                                                Expr::NPC_ValueDependentIsNull))
    return true;

  // Unfortunately, __null has type 'int'.
  if (isa<GNUNullExpr>(E)) return true;

  return false;
}

/// Get the implementation of ObjCInterfaceDecl, or nullptr if none
/// exists.
ObjCImplementationDecl *ASTContext::getObjCImplementation(ObjCInterfaceDecl *D) {
  llvm::DenseMap<ObjCContainerDecl*, ObjCImplDecl*>::iterator
    I = ObjCImpls.find(D);
  if (I != ObjCImpls.end())
    return cast<ObjCImplementationDecl>(I->second);
  return nullptr;
}

/// Get the implementation of ObjCCategoryDecl, or nullptr if none
/// exists.
ObjCCategoryImplDecl *ASTContext::getObjCImplementation(ObjCCategoryDecl *D) {
  llvm::DenseMap<ObjCContainerDecl*, ObjCImplDecl*>::iterator
    I = ObjCImpls.find(D);
  if (I != ObjCImpls.end())
    return cast<ObjCCategoryImplDecl>(I->second);
  return nullptr;
}

/// Set the implementation of ObjCInterfaceDecl.
void ASTContext::setObjCImplementation(ObjCInterfaceDecl *IFaceD,
                           ObjCImplementationDecl *ImplD) {
  assert(IFaceD && ImplD && "Passed null params");
  ObjCImpls[IFaceD] = ImplD;
}

/// Set the implementation of ObjCCategoryDecl.
void ASTContext::setObjCImplementation(ObjCCategoryDecl *CatD,
                           ObjCCategoryImplDecl *ImplD) {
  assert(CatD && ImplD && "Passed null params");
  ObjCImpls[CatD] = ImplD;
}

const ObjCMethodDecl *
ASTContext::getObjCMethodRedeclaration(const ObjCMethodDecl *MD) const {
  return ObjCMethodRedecls.lookup(MD);
}

void ASTContext::setObjCMethodRedeclaration(const ObjCMethodDecl *MD,
                                            const ObjCMethodDecl *Redecl) {
  assert(!getObjCMethodRedeclaration(MD) && "MD already has a redeclaration");
  ObjCMethodRedecls[MD] = Redecl;
}

const ObjCInterfaceDecl *ASTContext::getObjContainingInterface(
                                              const NamedDecl *ND) const {
  if (const auto *ID = dyn_cast<ObjCInterfaceDecl>(ND->getDeclContext()))
    return ID;
  if (const auto *CD = dyn_cast<ObjCCategoryDecl>(ND->getDeclContext()))
    return CD->getClassInterface();
  if (const auto *IMD = dyn_cast<ObjCImplDecl>(ND->getDeclContext()))
    return IMD->getClassInterface();

  return nullptr;
}

/// Get the copy initialization expression of VarDecl, or nullptr if
/// none exists.
BlockVarCopyInit ASTContext::getBlockVarCopyInit(const VarDecl *VD) const {
  assert(VD && "Passed null params");
  assert(VD->hasAttr<BlocksAttr>() &&
         "getBlockVarCopyInits - not __block var");
  auto I = BlockVarCopyInits.find(VD);
  if (I != BlockVarCopyInits.end())
    return I->second;
  return {nullptr, false};
}

/// Set the copy initialization expression of a block var decl.
void ASTContext::setBlockVarCopyInit(const VarDecl*VD, Expr *CopyExpr,
                                     bool CanThrow) {
  assert(VD && CopyExpr && "Passed null params");
  assert(VD->hasAttr<BlocksAttr>() &&
         "setBlockVarCopyInits - not __block var");
  BlockVarCopyInits[VD].setExprAndFlag(CopyExpr, CanThrow);
}

TypeSourceInfo *ASTContext::CreateTypeSourceInfo(QualType T,
                                                 unsigned DataSize) const {
  if (!DataSize)
    DataSize = TypeLoc::getFullDataSizeForType(T);
  else
    assert(DataSize == TypeLoc::getFullDataSizeForType(T) &&
           "incorrect data size provided to CreateTypeSourceInfo!");

  auto *TInfo =
    (TypeSourceInfo*)BumpAlloc.Allocate(sizeof(TypeSourceInfo) + DataSize, 8);
  new (TInfo) TypeSourceInfo(T, DataSize);
  return TInfo;
}

TypeSourceInfo *ASTContext::getTrivialTypeSourceInfo(QualType T,
                                                     SourceLocation L) const {
  TypeSourceInfo *DI = CreateTypeSourceInfo(T);
  DI->getTypeLoc().initialize(const_cast<ASTContext &>(*this), L);
  return DI;
}

const ASTRecordLayout &
ASTContext::getASTObjCInterfaceLayout(const ObjCInterfaceDecl *D) const {
  return getObjCLayout(D, nullptr);
}

const ASTRecordLayout &
ASTContext::getASTObjCImplementationLayout(
                                        const ObjCImplementationDecl *D) const {
  return getObjCLayout(D->getClassInterface(), D);
}

static auto getCanonicalTemplateArguments(const ASTContext &C,
                                          ArrayRef<TemplateArgument> Args,
                                          bool &AnyNonCanonArgs) {
  SmallVector<TemplateArgument, 16> CanonArgs(Args);
  for (auto &Arg : CanonArgs) {
    TemplateArgument OrigArg = Arg;
    Arg = C.getCanonicalTemplateArgument(Arg);
    AnyNonCanonArgs |= !Arg.structurallyEquals(OrigArg);
  }
  return CanonArgs;
}

//===----------------------------------------------------------------------===//
//                   Type creation/memoization methods
//===----------------------------------------------------------------------===//

QualType
ASTContext::getExtQualType(const Type *baseType, Qualifiers quals) const {
  unsigned fastQuals = quals.getFastQualifiers();
  quals.removeFastQualifiers();

  // Check if we've already instantiated this type.
  llvm::FoldingSetNodeID ID;
  ExtQuals::Profile(ID, baseType, quals);
  void *insertPos = nullptr;
  if (ExtQuals *eq = ExtQualNodes.FindNodeOrInsertPos(ID, insertPos)) {
    assert(eq->getQualifiers() == quals);
    return QualType(eq, fastQuals);
  }

  // If the base type is not canonical, make the appropriate canonical type.
  QualType canon;
  if (!baseType->isCanonicalUnqualified()) {
    SplitQualType canonSplit = baseType->getCanonicalTypeInternal().split();
    canonSplit.Quals.addConsistentQualifiers(quals);
    canon = getExtQualType(canonSplit.Ty, canonSplit.Quals);

    // Re-find the insert position.
    (void) ExtQualNodes.FindNodeOrInsertPos(ID, insertPos);
  }

  auto *eq = new (*this, alignof(ExtQuals)) ExtQuals(baseType, canon, quals);
  ExtQualNodes.InsertNode(eq, insertPos);
  return QualType(eq, fastQuals);
}

QualType ASTContext::getAddrSpaceQualType(QualType T,
                                          LangAS AddressSpace) const {
  QualType CanT = getCanonicalType(T);
  if (CanT.getAddressSpace() == AddressSpace)
    return T;

  // If we are composing extended qualifiers together, merge together
  // into one ExtQuals node.
  QualifierCollector Quals;
  const Type *TypeNode = Quals.strip(T);

  // If this type already has an address space specified, it cannot get
  // another one.
  assert(!Quals.hasAddressSpace() &&
         "Type cannot be in multiple addr spaces!");
  Quals.addAddressSpace(AddressSpace);

  return getExtQualType(TypeNode, Quals);
}

QualType ASTContext::removeAddrSpaceQualType(QualType T) const {
  // If the type is not qualified with an address space, just return it
  // immediately.
  if (!T.hasAddressSpace())
    return T;

  QualifierCollector Quals;
  const Type *TypeNode;
  // For arrays, strip the qualifier off the element type, then reconstruct the
  // array type
  if (T.getTypePtr()->isArrayType()) {
    T = getUnqualifiedArrayType(T, Quals);
    TypeNode = T.getTypePtr();
  } else {
    // If we are composing extended qualifiers together, merge together
    // into one ExtQuals node.
    while (T.hasAddressSpace()) {
      TypeNode = Quals.strip(T);

      // If the type no longer has an address space after stripping qualifiers,
      // jump out.
      if (!QualType(TypeNode, 0).hasAddressSpace())
        break;

      // There might be sugar in the way. Strip it and try again.
      T = T.getSingleStepDesugaredType(*this);
    }
  }

  Quals.removeAddressSpace();

  // Removal of the address space can mean there are no longer any
  // non-fast qualifiers, so creating an ExtQualType isn't possible (asserts)
  // or required.
  if (Quals.hasNonFastQualifiers())
    return getExtQualType(TypeNode, Quals);
  else
    return QualType(TypeNode, Quals.getFastQualifiers());
}

uint16_t
ASTContext::getPointerAuthVTablePointerDiscriminator(const CXXRecordDecl *RD) {
  assert(RD->isPolymorphic() &&
         "Attempted to get vtable pointer discriminator on a monomorphic type");
  std::unique_ptr<MangleContext> MC(createMangleContext());
  SmallString<256> Str;
  llvm::raw_svector_ostream Out(Str);
  MC->mangleCXXVTable(RD, Out);
  return llvm::getPointerAuthStableSipHash(Str);
}

/// Encode a function type for use in the discriminator of a function pointer
/// type. We can't use the itanium scheme for this since C has quite permissive
/// rules for type compatibility that we need to be compatible with.
///
/// Formally, this function associates every function pointer type T with an
/// encoded string E(T). Let the equivalence relation T1 ~ T2 be defined as
/// E(T1) == E(T2). E(T) is part of the ABI of values of type T. C type
/// compatibility requires equivalent treatment under the ABI, so
/// CCompatible(T1, T2) must imply E(T1) == E(T2), that is, CCompatible must be
/// a subset of ~. Crucially, however, it must be a proper subset because
/// CCompatible is not an equivalence relation: for example, int[] is compatible
/// with both int[1] and int[2], but the latter are not compatible with each
/// other. Therefore this encoding function must be careful to only distinguish
/// types if there is no third type with which they are both required to be
/// compatible.
static void encodeTypeForFunctionPointerAuth(const ASTContext &Ctx,
                                             raw_ostream &OS, QualType QT) {
  // FIXME: Consider address space qualifiers.
  const Type *T = QT.getCanonicalType().getTypePtr();

  // FIXME: Consider using the C++ type mangling when we encounter a construct
  // that is incompatible with C.

  switch (T->getTypeClass()) {
  case Type::Atomic:
    return encodeTypeForFunctionPointerAuth(
        Ctx, OS, cast<AtomicType>(T)->getValueType());

  case Type::LValueReference:
    OS << "R";
    encodeTypeForFunctionPointerAuth(Ctx, OS,
                                     cast<ReferenceType>(T)->getPointeeType());
    return;
  case Type::RValueReference:
    OS << "O";
    encodeTypeForFunctionPointerAuth(Ctx, OS,
                                     cast<ReferenceType>(T)->getPointeeType());
    return;

  case Type::Pointer:
    // C11 6.7.6.1p2:
    //   For two pointer types to be compatible, both shall be identically
    //   qualified and both shall be pointers to compatible types.
    // FIXME: we should also consider pointee types.
    OS << "P";
    return;

  case Type::ObjCObjectPointer:
  case Type::BlockPointer:
    OS << "P";
    return;

  case Type::Complex:
    OS << "C";
    return encodeTypeForFunctionPointerAuth(
        Ctx, OS, cast<ComplexType>(T)->getElementType());

  case Type::VariableArray:
  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::ArrayParameter:
    // C11 6.7.6.2p6:
    //   For two array types to be compatible, both shall have compatible
    //   element types, and if both size specifiers are present, and are integer
    //   constant expressions, then both size specifiers shall have the same
    //   constant value [...]
    //
    // So since ElemType[N] has to be compatible ElemType[], we can't encode the
    // width of the array.
    OS << "A";
    return encodeTypeForFunctionPointerAuth(
        Ctx, OS, cast<ArrayType>(T)->getElementType());

  case Type::ObjCInterface:
  case Type::ObjCObject:
    OS << "<objc_object>";
    return;

  case Type::Enum: {
    // C11 6.7.2.2p4:
    //   Each enumerated type shall be compatible with char, a signed integer
    //   type, or an unsigned integer type.
    //
    // So we have to treat enum types as integers.
    QualType UnderlyingType = cast<EnumType>(T)->getDecl()->getIntegerType();
    return encodeTypeForFunctionPointerAuth(
        Ctx, OS, UnderlyingType.isNull() ? Ctx.IntTy : UnderlyingType);
  }

  case Type::FunctionNoProto:
  case Type::FunctionProto: {
    // C11 6.7.6.3p15:
    //   For two function types to be compatible, both shall specify compatible
    //   return types. Moreover, the parameter type lists, if both are present,
    //   shall agree in the number of parameters and in the use of the ellipsis
    //   terminator; corresponding parameters shall have compatible types.
    //
    // That paragraph goes on to describe how unprototyped functions are to be
    // handled, which we ignore here. Unprototyped function pointers are hashed
    // as though they were prototyped nullary functions since thats probably
    // what the user meant. This behavior is non-conforming.
    // FIXME: If we add a "custom discriminator" function type attribute we
    // should encode functions as their discriminators.
    OS << "F";
    const auto *FuncType = cast<FunctionType>(T);
    encodeTypeForFunctionPointerAuth(Ctx, OS, FuncType->getReturnType());
    if (const auto *FPT = dyn_cast<FunctionProtoType>(FuncType)) {
      for (QualType Param : FPT->param_types()) {
        Param = Ctx.getSignatureParameterType(Param);
        encodeTypeForFunctionPointerAuth(Ctx, OS, Param);
      }
      if (FPT->isVariadic())
        OS << "z";
    }
    OS << "E";
    return;
  }

  case Type::MemberPointer: {
    OS << "M";
    const auto *MPT = T->getAs<MemberPointerType>();
    encodeTypeForFunctionPointerAuth(Ctx, OS, QualType(MPT->getClass(), 0));
    encodeTypeForFunctionPointerAuth(Ctx, OS, MPT->getPointeeType());
    return;
  }
  case Type::ExtVector:
  case Type::Vector:
    OS << "Dv" << Ctx.getTypeSizeInChars(T).getQuantity();
    break;

  // Don't bother discriminating based on these types.
  case Type::Pipe:
  case Type::BitInt:
  case Type::ConstantMatrix:
    OS << "?";
    return;

  case Type::Builtin: {
    const auto *BTy = T->getAs<BuiltinType>();
    switch (BTy->getKind()) {
#define SIGNED_TYPE(Id, SingletonId)                                           \
  case BuiltinType::Id:                                                        \
    OS << "i";                                                                 \
    return;
#define UNSIGNED_TYPE(Id, SingletonId)                                         \
  case BuiltinType::Id:                                                        \
    OS << "i";                                                                 \
    return;
#define PLACEHOLDER_TYPE(Id, SingletonId) case BuiltinType::Id:
#define BUILTIN_TYPE(Id, SingletonId)
#include "clang/AST/BuiltinTypes.def"
      llvm_unreachable("placeholder types should not appear here.");

    case BuiltinType::Half:
      OS << "Dh";
      return;
    case BuiltinType::Float:
      OS << "f";
      return;
    case BuiltinType::Double:
      OS << "d";
      return;
    case BuiltinType::LongDouble:
      OS << "e";
      return;
    case BuiltinType::Float16:
      OS << "DF16_";
      return;
    case BuiltinType::Float128:
      OS << "g";
      return;

    case BuiltinType::Void:
      OS << "v";
      return;

    case BuiltinType::ObjCId:
    case BuiltinType::ObjCClass:
    case BuiltinType::ObjCSel:
    case BuiltinType::NullPtr:
      OS << "P";
      return;

    // Don't bother discriminating based on OpenCL types.
    case BuiltinType::OCLSampler:
    case BuiltinType::OCLEvent:
    case BuiltinType::OCLClkEvent:
    case BuiltinType::OCLQueue:
    case BuiltinType::OCLReserveID:
    case BuiltinType::BFloat16:
    case BuiltinType::VectorQuad:
    case BuiltinType::VectorPair:
      OS << "?";
      return;

    // Don't bother discriminating based on these seldom-used types.
    case BuiltinType::Ibm128:
      return;
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix)                   \
  case BuiltinType::Id:                                                        \
    return;
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext)                                      \
  case BuiltinType::Id:                                                        \
    return;
#include "clang/Basic/OpenCLExtensionTypes.def"
#define SVE_TYPE(Name, Id, SingletonId)                                        \
  case BuiltinType::Id:                                                        \
    return;
#include "clang/Basic/AArch64SVEACLETypes.def"
    case BuiltinType::Dependent:
      llvm_unreachable("should never get here");
    case BuiltinType::AMDGPUBufferRsrc:
    case BuiltinType::WasmExternRef:
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
      llvm_unreachable("not yet implemented");
    }
  }
  case Type::Record: {
    const RecordDecl *RD = T->getAs<RecordType>()->getDecl();
    const IdentifierInfo *II = RD->getIdentifier();

    // In C++, an immediate typedef of an anonymous struct or union
    // is considered to name it for ODR purposes, but C's specification
    // of type compatibility does not have a similar rule.  Using the typedef
    // name in function type discriminators anyway, as we do here,
    // therefore technically violates the C standard: two function pointer
    // types defined in terms of two typedef'd anonymous structs with
    // different names are formally still compatible, but we are assigning
    // them different discriminators and therefore incompatible ABIs.
    //
    // This is a relatively minor violation that significantly improves
    // discrimination in some cases and has not caused problems in
    // practice.  Regardless, it is now part of the ABI in places where
    // function type discrimination is used, and it can no longer be
    // changed except on new platforms.

    if (!II)
      if (const TypedefNameDecl *Typedef = RD->getTypedefNameForAnonDecl())
        II = Typedef->getDeclName().getAsIdentifierInfo();

    if (!II) {
      OS << "<anonymous_record>";
      return;
    }
    OS << II->getLength() << II->getName();
    return;
  }
  case Type::DeducedTemplateSpecialization:
  case Type::Auto:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base)
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("unexpected non-canonical or dependent type!");
    return;
  }
}

uint16_t ASTContext::getPointerAuthTypeDiscriminator(QualType T) {
  assert(!T->isDependentType() &&
         "cannot compute type discriminator of a dependent type");

  SmallString<256> Str;
  llvm::raw_svector_ostream Out(Str);

  if (T->isFunctionPointerType() || T->isFunctionReferenceType())
    T = T->getPointeeType();

  if (T->isFunctionType()) {
    encodeTypeForFunctionPointerAuth(*this, Out, T);
  } else {
    T = T.getUnqualifiedType();
    std::unique_ptr<MangleContext> MC(createMangleContext());
    MC->mangleCanonicalTypeName(T, Out);
  }

  return llvm::getPointerAuthStableSipHash(Str);
}

QualType ASTContext::getObjCGCQualType(QualType T,
                                       Qualifiers::GC GCAttr) const {
  QualType CanT = getCanonicalType(T);
  if (CanT.getObjCGCAttr() == GCAttr)
    return T;

  if (const auto *ptr = T->getAs<PointerType>()) {
    QualType Pointee = ptr->getPointeeType();
    if (Pointee->isAnyPointerType()) {
      QualType ResultType = getObjCGCQualType(Pointee, GCAttr);
      return getPointerType(ResultType);
    }
  }

  // If we are composing extended qualifiers together, merge together
  // into one ExtQuals node.
  QualifierCollector Quals;
  const Type *TypeNode = Quals.strip(T);

  // If this type already has an ObjCGC specified, it cannot get
  // another one.
  assert(!Quals.hasObjCGCAttr() &&
         "Type cannot have multiple ObjCGCs!");
  Quals.addObjCGCAttr(GCAttr);

  return getExtQualType(TypeNode, Quals);
}

QualType ASTContext::removePtrSizeAddrSpace(QualType T) const {
  if (const PointerType *Ptr = T->getAs<PointerType>()) {
    QualType Pointee = Ptr->getPointeeType();
    if (isPtrSizeAddressSpace(Pointee.getAddressSpace())) {
      return getPointerType(removeAddrSpaceQualType(Pointee));
    }
  }
  return T;
}

QualType ASTContext::getCountAttributedType(
    QualType WrappedTy, Expr *CountExpr, bool CountInBytes, bool OrNull,
    ArrayRef<TypeCoupledDeclRefInfo> DependentDecls) const {
  assert(WrappedTy->isPointerType() || WrappedTy->isArrayType());

  llvm::FoldingSetNodeID ID;
  CountAttributedType::Profile(ID, WrappedTy, CountExpr, CountInBytes, OrNull);

  void *InsertPos = nullptr;
  CountAttributedType *CATy =
      CountAttributedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (CATy)
    return QualType(CATy, 0);

  QualType CanonTy = getCanonicalType(WrappedTy);
  size_t Size = CountAttributedType::totalSizeToAlloc<TypeCoupledDeclRefInfo>(
      DependentDecls.size());
  CATy = (CountAttributedType *)Allocate(Size, TypeAlignment);
  new (CATy) CountAttributedType(WrappedTy, CanonTy, CountExpr, CountInBytes,
                                 OrNull, DependentDecls);
  Types.push_back(CATy);
  CountAttributedTypes.InsertNode(CATy, InsertPos);

  return QualType(CATy, 0);
}

const FunctionType *ASTContext::adjustFunctionType(const FunctionType *T,
                                                   FunctionType::ExtInfo Info) {
  if (T->getExtInfo() == Info)
    return T;

  QualType Result;
  if (const auto *FNPT = dyn_cast<FunctionNoProtoType>(T)) {
    Result = getFunctionNoProtoType(FNPT->getReturnType(), Info);
  } else {
    const auto *FPT = cast<FunctionProtoType>(T);
    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
    EPI.ExtInfo = Info;
    Result = getFunctionType(FPT->getReturnType(), FPT->getParamTypes(), EPI);
  }

  return cast<FunctionType>(Result.getTypePtr());
}

void ASTContext::adjustDeducedFunctionResultType(FunctionDecl *FD,
                                                 QualType ResultType) {
  FD = FD->getMostRecentDecl();
  while (true) {
    const auto *FPT = FD->getType()->castAs<FunctionProtoType>();
    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
    FD->setType(getFunctionType(ResultType, FPT->getParamTypes(), EPI));
    if (FunctionDecl *Next = FD->getPreviousDecl())
      FD = Next;
    else
      break;
  }
  if (ASTMutationListener *L = getASTMutationListener())
    L->DeducedReturnType(FD, ResultType);
}

/// Get a function type and produce the equivalent function type with the
/// specified exception specification. Type sugar that can be present on a
/// declaration of a function with an exception specification is permitted
/// and preserved. Other type sugar (for instance, typedefs) is not.
QualType ASTContext::getFunctionTypeWithExceptionSpec(
    QualType Orig, const FunctionProtoType::ExceptionSpecInfo &ESI) const {
  // Might have some parens.
  if (const auto *PT = dyn_cast<ParenType>(Orig))
    return getParenType(
        getFunctionTypeWithExceptionSpec(PT->getInnerType(), ESI));

  // Might be wrapped in a macro qualified type.
  if (const auto *MQT = dyn_cast<MacroQualifiedType>(Orig))
    return getMacroQualifiedType(
        getFunctionTypeWithExceptionSpec(MQT->getUnderlyingType(), ESI),
        MQT->getMacroIdentifier());

  // Might have a calling-convention attribute.
  if (const auto *AT = dyn_cast<AttributedType>(Orig))
    return getAttributedType(
        AT->getAttrKind(),
        getFunctionTypeWithExceptionSpec(AT->getModifiedType(), ESI),
        getFunctionTypeWithExceptionSpec(AT->getEquivalentType(), ESI));

  // Anything else must be a function type. Rebuild it with the new exception
  // specification.
  const auto *Proto = Orig->castAs<FunctionProtoType>();
  return getFunctionType(
      Proto->getReturnType(), Proto->getParamTypes(),
      Proto->getExtProtoInfo().withExceptionSpec(ESI));
}

bool ASTContext::hasSameFunctionTypeIgnoringExceptionSpec(QualType T,
                                                          QualType U) const {
  return hasSameType(T, U) ||
         (getLangOpts().CPlusPlus17 &&
          hasSameType(getFunctionTypeWithExceptionSpec(T, EST_None),
                      getFunctionTypeWithExceptionSpec(U, EST_None)));
}

QualType ASTContext::getFunctionTypeWithoutPtrSizes(QualType T) {
  if (const auto *Proto = T->getAs<FunctionProtoType>()) {
    QualType RetTy = removePtrSizeAddrSpace(Proto->getReturnType());
    SmallVector<QualType, 16> Args(Proto->param_types().size());
    for (unsigned i = 0, n = Args.size(); i != n; ++i)
      Args[i] = removePtrSizeAddrSpace(Proto->param_types()[i]);
    return getFunctionType(RetTy, Args, Proto->getExtProtoInfo());
  }

  if (const FunctionNoProtoType *Proto = T->getAs<FunctionNoProtoType>()) {
    QualType RetTy = removePtrSizeAddrSpace(Proto->getReturnType());
    return getFunctionNoProtoType(RetTy, Proto->getExtInfo());
  }

  return T;
}

bool ASTContext::hasSameFunctionTypeIgnoringPtrSizes(QualType T, QualType U) {
  return hasSameType(T, U) ||
         hasSameType(getFunctionTypeWithoutPtrSizes(T),
                     getFunctionTypeWithoutPtrSizes(U));
}

void ASTContext::adjustExceptionSpec(
    FunctionDecl *FD, const FunctionProtoType::ExceptionSpecInfo &ESI,
    bool AsWritten) {
  // Update the type.
  QualType Updated =
      getFunctionTypeWithExceptionSpec(FD->getType(), ESI);
  FD->setType(Updated);

  if (!AsWritten)
    return;

  // Update the type in the type source information too.
  if (TypeSourceInfo *TSInfo = FD->getTypeSourceInfo()) {
    // If the type and the type-as-written differ, we may need to update
    // the type-as-written too.
    if (TSInfo->getType() != FD->getType())
      Updated = getFunctionTypeWithExceptionSpec(TSInfo->getType(), ESI);

    // FIXME: When we get proper type location information for exceptions,
    // we'll also have to rebuild the TypeSourceInfo. For now, we just patch
    // up the TypeSourceInfo;
    assert(TypeLoc::getFullDataSizeForType(Updated) ==
               TypeLoc::getFullDataSizeForType(TSInfo->getType()) &&
           "TypeLoc size mismatch from updating exception specification");
    TSInfo->overrideType(Updated);
  }
}

/// getComplexType - Return the uniqued reference to the type for a complex
/// number with the specified element type.
QualType ASTContext::getComplexType(QualType T) const {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  ComplexType::Profile(ID, T);

  void *InsertPos = nullptr;
  if (ComplexType *CT = ComplexTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(CT, 0);

  // If the pointee type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getComplexType(getCanonicalType(T));

    // Get the new insert position for the node we care about.
    ComplexType *NewIP = ComplexTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }
  auto *New = new (*this, alignof(ComplexType)) ComplexType(T, Canonical);
  Types.push_back(New);
  ComplexTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getPointerType - Return the uniqued reference to the type for a pointer to
/// the specified type.
QualType ASTContext::getPointerType(QualType T) const {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  PointerType::Profile(ID, T);

  void *InsertPos = nullptr;
  if (PointerType *PT = PointerTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(PT, 0);

  // If the pointee type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getPointerType(getCanonicalType(T));

    // Get the new insert position for the node we care about.
    PointerType *NewIP = PointerTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }
  auto *New = new (*this, alignof(PointerType)) PointerType(T, Canonical);
  Types.push_back(New);
  PointerTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

QualType ASTContext::getAdjustedType(QualType Orig, QualType New) const {
  llvm::FoldingSetNodeID ID;
  AdjustedType::Profile(ID, Orig, New);
  void *InsertPos = nullptr;
  AdjustedType *AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (AT)
    return QualType(AT, 0);

  QualType Canonical = getCanonicalType(New);

  // Get the new insert position for the node we care about.
  AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  assert(!AT && "Shouldn't be in the map!");

  AT = new (*this, alignof(AdjustedType))
      AdjustedType(Type::Adjusted, Orig, New, Canonical);
  Types.push_back(AT);
  AdjustedTypes.InsertNode(AT, InsertPos);
  return QualType(AT, 0);
}

QualType ASTContext::getDecayedType(QualType Orig, QualType Decayed) const {
  llvm::FoldingSetNodeID ID;
  AdjustedType::Profile(ID, Orig, Decayed);
  void *InsertPos = nullptr;
  AdjustedType *AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (AT)
    return QualType(AT, 0);

  QualType Canonical = getCanonicalType(Decayed);

  // Get the new insert position for the node we care about.
  AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  assert(!AT && "Shouldn't be in the map!");

  AT = new (*this, alignof(DecayedType)) DecayedType(Orig, Decayed, Canonical);
  Types.push_back(AT);
  AdjustedTypes.InsertNode(AT, InsertPos);
  return QualType(AT, 0);
}

QualType ASTContext::getDecayedType(QualType T) const {
  assert((T->isArrayType() || T->isFunctionType()) && "T does not decay");

  QualType Decayed;

  // C99 6.7.5.3p7:
  //   A declaration of a parameter as "array of type" shall be
  //   adjusted to "qualified pointer to type", where the type
  //   qualifiers (if any) are those specified within the [ and ] of
  //   the array type derivation.
  if (T->isArrayType())
    Decayed = getArrayDecayedType(T);

  // C99 6.7.5.3p8:
  //   A declaration of a parameter as "function returning type"
  //   shall be adjusted to "pointer to function returning type", as
  //   in 6.3.2.1.
  if (T->isFunctionType())
    Decayed = getPointerType(T);

  return getDecayedType(T, Decayed);
}

QualType ASTContext::getArrayParameterType(QualType Ty) const {
  if (Ty->isArrayParameterType())
    return Ty;
  assert(Ty->isConstantArrayType() && "Ty must be an array type.");
  const auto *ATy = cast<ConstantArrayType>(Ty);
  llvm::FoldingSetNodeID ID;
  ATy->Profile(ID, *this, ATy->getElementType(), ATy->getZExtSize(),
               ATy->getSizeExpr(), ATy->getSizeModifier(),
               ATy->getIndexTypeQualifiers().getAsOpaqueValue());
  void *InsertPos = nullptr;
  ArrayParameterType *AT =
      ArrayParameterTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (AT)
    return QualType(AT, 0);

  QualType Canonical;
  if (!Ty.isCanonical()) {
    Canonical = getArrayParameterType(getCanonicalType(Ty));

    // Get the new insert position for the node we care about.
    AT = ArrayParameterTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!AT && "Shouldn't be in the map!");
  }

  AT = new (*this, alignof(ArrayParameterType))
      ArrayParameterType(ATy, Canonical);
  Types.push_back(AT);
  ArrayParameterTypes.InsertNode(AT, InsertPos);
  return QualType(AT, 0);
}

/// getBlockPointerType - Return the uniqued reference to the type for
/// a pointer to the specified block.
QualType ASTContext::getBlockPointerType(QualType T) const {
  assert(T->isFunctionType() && "block of function types only");
  // Unique pointers, to guarantee there is only one block of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  BlockPointerType::Profile(ID, T);

  void *InsertPos = nullptr;
  if (BlockPointerType *PT =
        BlockPointerTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(PT, 0);

  // If the block pointee type isn't canonical, this won't be a canonical
  // type either so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getBlockPointerType(getCanonicalType(T));

    // Get the new insert position for the node we care about.
    BlockPointerType *NewIP =
      BlockPointerTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }
  auto *New =
      new (*this, alignof(BlockPointerType)) BlockPointerType(T, Canonical);
  Types.push_back(New);
  BlockPointerTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getLValueReferenceType - Return the uniqued reference to the type for an
/// lvalue reference to the specified type.
QualType
ASTContext::getLValueReferenceType(QualType T, bool SpelledAsLValue) const {
  assert((!T->isPlaceholderType() ||
          T->isSpecificPlaceholderType(BuiltinType::UnknownAny)) &&
         "Unresolved placeholder type");

  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  ReferenceType::Profile(ID, T, SpelledAsLValue);

  void *InsertPos = nullptr;
  if (LValueReferenceType *RT =
        LValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(RT, 0);

  const auto *InnerRef = T->getAs<ReferenceType>();

  // If the referencee type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.
  QualType Canonical;
  if (!SpelledAsLValue || InnerRef || !T.isCanonical()) {
    QualType PointeeType = (InnerRef ? InnerRef->getPointeeType() : T);
    Canonical = getLValueReferenceType(getCanonicalType(PointeeType));

    // Get the new insert position for the node we care about.
    LValueReferenceType *NewIP =
      LValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }

  auto *New = new (*this, alignof(LValueReferenceType))
      LValueReferenceType(T, Canonical, SpelledAsLValue);
  Types.push_back(New);
  LValueReferenceTypes.InsertNode(New, InsertPos);

  return QualType(New, 0);
}

/// getRValueReferenceType - Return the uniqued reference to the type for an
/// rvalue reference to the specified type.
QualType ASTContext::getRValueReferenceType(QualType T) const {
  assert((!T->isPlaceholderType() ||
          T->isSpecificPlaceholderType(BuiltinType::UnknownAny)) &&
         "Unresolved placeholder type");

  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  ReferenceType::Profile(ID, T, false);

  void *InsertPos = nullptr;
  if (RValueReferenceType *RT =
        RValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(RT, 0);

  const auto *InnerRef = T->getAs<ReferenceType>();

  // If the referencee type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.
  QualType Canonical;
  if (InnerRef || !T.isCanonical()) {
    QualType PointeeType = (InnerRef ? InnerRef->getPointeeType() : T);
    Canonical = getRValueReferenceType(getCanonicalType(PointeeType));

    // Get the new insert position for the node we care about.
    RValueReferenceType *NewIP =
      RValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }

  auto *New = new (*this, alignof(RValueReferenceType))
      RValueReferenceType(T, Canonical);
  Types.push_back(New);
  RValueReferenceTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getMemberPointerType - Return the uniqued reference to the type for a
/// member pointer to the specified type, in the specified class.
QualType ASTContext::getMemberPointerType(QualType T, const Type *Cls) const {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  MemberPointerType::Profile(ID, T, Cls);

  void *InsertPos = nullptr;
  if (MemberPointerType *PT =
      MemberPointerTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(PT, 0);

  // If the pointee or class type isn't canonical, this won't be a canonical
  // type either, so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical() || !Cls->isCanonicalUnqualified()) {
    Canonical = getMemberPointerType(getCanonicalType(T),getCanonicalType(Cls));

    // Get the new insert position for the node we care about.
    MemberPointerType *NewIP =
      MemberPointerTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }
  auto *New = new (*this, alignof(MemberPointerType))
      MemberPointerType(T, Cls, Canonical);
  Types.push_back(New);
  MemberPointerTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getConstantArrayType - Return the unique reference to the type for an
/// array of the specified element type.
QualType ASTContext::getConstantArrayType(QualType EltTy,
                                          const llvm::APInt &ArySizeIn,
                                          const Expr *SizeExpr,
                                          ArraySizeModifier ASM,
                                          unsigned IndexTypeQuals) const {
  assert((EltTy->isDependentType() ||
          EltTy->isIncompleteType() || EltTy->isConstantSizeType()) &&
         "Constant array of VLAs is illegal!");

  // We only need the size as part of the type if it's instantiation-dependent.
  if (SizeExpr && !SizeExpr->isInstantiationDependent())
    SizeExpr = nullptr;

  // Convert the array size into a canonical width matching the pointer size for
  // the target.
  llvm::APInt ArySize(ArySizeIn);
  ArySize = ArySize.zextOrTrunc(Target->getMaxPointerWidth());

  llvm::FoldingSetNodeID ID;
  ConstantArrayType::Profile(ID, *this, EltTy, ArySize.getZExtValue(), SizeExpr,
                             ASM, IndexTypeQuals);

  void *InsertPos = nullptr;
  if (ConstantArrayType *ATP =
      ConstantArrayTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(ATP, 0);

  // If the element type isn't canonical or has qualifiers, or the array bound
  // is instantiation-dependent, this won't be a canonical type either, so fill
  // in the canonical type field.
  QualType Canon;
  // FIXME: Check below should look for qualifiers behind sugar.
  if (!EltTy.isCanonical() || EltTy.hasLocalQualifiers() || SizeExpr) {
    SplitQualType canonSplit = getCanonicalType(EltTy).split();
    Canon = getConstantArrayType(QualType(canonSplit.Ty, 0), ArySize, nullptr,
                                 ASM, IndexTypeQuals);
    Canon = getQualifiedType(Canon, canonSplit.Quals);

    // Get the new insert position for the node we care about.
    ConstantArrayType *NewIP =
      ConstantArrayTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }

  auto *New = ConstantArrayType::Create(*this, EltTy, Canon, ArySize, SizeExpr,
                                        ASM, IndexTypeQuals);
  ConstantArrayTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getVariableArrayDecayedType - Turns the given type, which may be
/// variably-modified, into the corresponding type with all the known
/// sizes replaced with [*].
QualType ASTContext::getVariableArrayDecayedType(QualType type) const {
  // Vastly most common case.
  if (!type->isVariablyModifiedType()) return type;

  QualType result;

  SplitQualType split = type.getSplitDesugaredType();
  const Type *ty = split.Ty;
  switch (ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("didn't desugar past all non-canonical types?");

  // These types should never be variably-modified.
  case Type::Builtin:
  case Type::Complex:
  case Type::Vector:
  case Type::DependentVector:
  case Type::ExtVector:
  case Type::DependentSizedExtVector:
  case Type::ConstantMatrix:
  case Type::DependentSizedMatrix:
  case Type::DependentAddressSpace:
  case Type::ObjCObject:
  case Type::ObjCInterface:
  case Type::ObjCObjectPointer:
  case Type::Record:
  case Type::Enum:
  case Type::UnresolvedUsing:
  case Type::TypeOfExpr:
  case Type::TypeOf:
  case Type::Decltype:
  case Type::UnaryTransform:
  case Type::DependentName:
  case Type::InjectedClassName:
  case Type::TemplateSpecialization:
  case Type::DependentTemplateSpecialization:
  case Type::TemplateTypeParm:
  case Type::SubstTemplateTypeParmPack:
  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
  case Type::PackExpansion:
  case Type::PackIndexing:
  case Type::BitInt:
  case Type::DependentBitInt:
  case Type::ArrayParameter:
    llvm_unreachable("type should never be variably-modified");

  // These types can be variably-modified but should never need to
  // further decay.
  case Type::FunctionNoProto:
  case Type::FunctionProto:
  case Type::BlockPointer:
  case Type::MemberPointer:
  case Type::Pipe:
    return type;

  // These types can be variably-modified.  All these modifications
  // preserve structure except as noted by comments.
  // TODO: if we ever care about optimizing VLAs, there are no-op
  // optimizations available here.
  case Type::Pointer:
    result = getPointerType(getVariableArrayDecayedType(
                              cast<PointerType>(ty)->getPointeeType()));
    break;

  case Type::LValueReference: {
    const auto *lv = cast<LValueReferenceType>(ty);
    result = getLValueReferenceType(
                 getVariableArrayDecayedType(lv->getPointeeType()),
                                    lv->isSpelledAsLValue());
    break;
  }

  case Type::RValueReference: {
    const auto *lv = cast<RValueReferenceType>(ty);
    result = getRValueReferenceType(
                 getVariableArrayDecayedType(lv->getPointeeType()));
    break;
  }

  case Type::Atomic: {
    const auto *at = cast<AtomicType>(ty);
    result = getAtomicType(getVariableArrayDecayedType(at->getValueType()));
    break;
  }

  case Type::ConstantArray: {
    const auto *cat = cast<ConstantArrayType>(ty);
    result = getConstantArrayType(
                 getVariableArrayDecayedType(cat->getElementType()),
                                  cat->getSize(),
                                  cat->getSizeExpr(),
                                  cat->getSizeModifier(),
                                  cat->getIndexTypeCVRQualifiers());
    break;
  }

  case Type::DependentSizedArray: {
    const auto *dat = cast<DependentSizedArrayType>(ty);
    result = getDependentSizedArrayType(
                 getVariableArrayDecayedType(dat->getElementType()),
                                        dat->getSizeExpr(),
                                        dat->getSizeModifier(),
                                        dat->getIndexTypeCVRQualifiers(),
                                        dat->getBracketsRange());
    break;
  }

  // Turn incomplete types into [*] types.
  case Type::IncompleteArray: {
    const auto *iat = cast<IncompleteArrayType>(ty);
    result =
        getVariableArrayType(getVariableArrayDecayedType(iat->getElementType()),
                             /*size*/ nullptr, ArraySizeModifier::Normal,
                             iat->getIndexTypeCVRQualifiers(), SourceRange());
    break;
  }

  // Turn VLA types into [*] types.
  case Type::VariableArray: {
    const auto *vat = cast<VariableArrayType>(ty);
    result = getVariableArrayType(
        getVariableArrayDecayedType(vat->getElementType()),
        /*size*/ nullptr, ArraySizeModifier::Star,
        vat->getIndexTypeCVRQualifiers(), vat->getBracketsRange());
    break;
  }
  }

  // Apply the top-level qualifiers from the original.
  return getQualifiedType(result, split.Quals);
}

/// getVariableArrayType - Returns a non-unique reference to the type for a
/// variable array of the specified element type.
QualType ASTContext::getVariableArrayType(QualType EltTy, Expr *NumElts,
                                          ArraySizeModifier ASM,
                                          unsigned IndexTypeQuals,
                                          SourceRange Brackets) const {
  // Since we don't unique expressions, it isn't possible to unique VLA's
  // that have an expression provided for their size.
  QualType Canon;

  // Be sure to pull qualifiers off the element type.
  // FIXME: Check below should look for qualifiers behind sugar.
  if (!EltTy.isCanonical() || EltTy.hasLocalQualifiers()) {
    SplitQualType canonSplit = getCanonicalType(EltTy).split();
    Canon = getVariableArrayType(QualType(canonSplit.Ty, 0), NumElts, ASM,
                                 IndexTypeQuals, Brackets);
    Canon = getQualifiedType(Canon, canonSplit.Quals);
  }

  auto *New = new (*this, alignof(VariableArrayType))
      VariableArrayType(EltTy, Canon, NumElts, ASM, IndexTypeQuals, Brackets);

  VariableArrayTypes.push_back(New);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getDependentSizedArrayType - Returns a non-unique reference to
/// the type for a dependently-sized array of the specified element
/// type.
QualType ASTContext::getDependentSizedArrayType(QualType elementType,
                                                Expr *numElements,
                                                ArraySizeModifier ASM,
                                                unsigned elementTypeQuals,
                                                SourceRange brackets) const {
  assert((!numElements || numElements->isTypeDependent() ||
          numElements->isValueDependent()) &&
         "Size must be type- or value-dependent!");

  SplitQualType canonElementType = getCanonicalType(elementType).split();

  void *insertPos = nullptr;
  llvm::FoldingSetNodeID ID;
  DependentSizedArrayType::Profile(
      ID, *this, numElements ? QualType(canonElementType.Ty, 0) : elementType,
      ASM, elementTypeQuals, numElements);

  // Look for an existing type with these properties.
  DependentSizedArrayType *canonTy =
    DependentSizedArrayTypes.FindNodeOrInsertPos(ID, insertPos);

  // Dependently-sized array types that do not have a specified number
  // of elements will have their sizes deduced from a dependent
  // initializer.
  if (!numElements) {
    if (canonTy)
      return QualType(canonTy, 0);

    auto *newType = new (*this, alignof(DependentSizedArrayType))
        DependentSizedArrayType(elementType, QualType(), numElements, ASM,
                                elementTypeQuals, brackets);
    DependentSizedArrayTypes.InsertNode(newType, insertPos);
    Types.push_back(newType);
    return QualType(newType, 0);
  }

  // If we don't have one, build one.
  if (!canonTy) {
    canonTy = new (*this, alignof(DependentSizedArrayType))
        DependentSizedArrayType(QualType(canonElementType.Ty, 0), QualType(),
                                numElements, ASM, elementTypeQuals, brackets);
    DependentSizedArrayTypes.InsertNode(canonTy, insertPos);
    Types.push_back(canonTy);
  }

  // Apply qualifiers from the element type to the array.
  QualType canon = getQualifiedType(QualType(canonTy,0),
                                    canonElementType.Quals);

  // If we didn't need extra canonicalization for the element type or the size
  // expression, then just use that as our result.
  if (QualType(canonElementType.Ty, 0) == elementType &&
      canonTy->getSizeExpr() == numElements)
    return canon;

  // Otherwise, we need to build a type which follows the spelling
  // of the element type.
  auto *sugaredType = new (*this, alignof(DependentSizedArrayType))
      DependentSizedArrayType(elementType, canon, numElements, ASM,
                              elementTypeQuals, brackets);
  Types.push_back(sugaredType);
  return QualType(sugaredType, 0);
}

QualType ASTContext::getIncompleteArrayType(QualType elementType,
                                            ArraySizeModifier ASM,
                                            unsigned elementTypeQuals) const {
  llvm::FoldingSetNodeID ID;
  IncompleteArrayType::Profile(ID, elementType, ASM, elementTypeQuals);

  void *insertPos = nullptr;
  if (IncompleteArrayType *iat =
       IncompleteArrayTypes.FindNodeOrInsertPos(ID, insertPos))
    return QualType(iat, 0);

  // If the element type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.  We also have to pull
  // qualifiers off the element type.
  QualType canon;

  // FIXME: Check below should look for qualifiers behind sugar.
  if (!elementType.isCanonical() || elementType.hasLocalQualifiers()) {
    SplitQualType canonSplit = getCanonicalType(elementType).split();
    canon = getIncompleteArrayType(QualType(canonSplit.Ty, 0),
                                   ASM, elementTypeQuals);
    canon = getQualifiedType(canon, canonSplit.Quals);

    // Get the new insert position for the node we care about.
    IncompleteArrayType *existing =
      IncompleteArrayTypes.FindNodeOrInsertPos(ID, insertPos);
    assert(!existing && "Shouldn't be in the map!"); (void) existing;
  }

  auto *newType = new (*this, alignof(IncompleteArrayType))
      IncompleteArrayType(elementType, canon, ASM, elementTypeQuals);

  IncompleteArrayTypes.InsertNode(newType, insertPos);
  Types.push_back(newType);
  return QualType(newType, 0);
}

ASTContext::BuiltinVectorTypeInfo
ASTContext::getBuiltinVectorTypeInfo(const BuiltinType *Ty) const {
#define SVE_INT_ELTTY(BITS, ELTS, SIGNED, NUMVECTORS)                          \
  {getIntTypeForBitwidth(BITS, SIGNED), llvm::ElementCount::getScalable(ELTS), \
   NUMVECTORS};

#define SVE_ELTTY(ELTTY, ELTS, NUMVECTORS)                                     \
  {ELTTY, llvm::ElementCount::getScalable(ELTS), NUMVECTORS};

  switch (Ty->getKind()) {
  default:
    llvm_unreachable("Unsupported builtin vector type");
  case BuiltinType::SveInt8:
    return SVE_INT_ELTTY(8, 16, true, 1);
  case BuiltinType::SveUint8:
    return SVE_INT_ELTTY(8, 16, false, 1);
  case BuiltinType::SveInt8x2:
    return SVE_INT_ELTTY(8, 16, true, 2);
  case BuiltinType::SveUint8x2:
    return SVE_INT_ELTTY(8, 16, false, 2);
  case BuiltinType::SveInt8x3:
    return SVE_INT_ELTTY(8, 16, true, 3);
  case BuiltinType::SveUint8x3:
    return SVE_INT_ELTTY(8, 16, false, 3);
  case BuiltinType::SveInt8x4:
    return SVE_INT_ELTTY(8, 16, true, 4);
  case BuiltinType::SveUint8x4:
    return SVE_INT_ELTTY(8, 16, false, 4);
  case BuiltinType::SveInt16:
    return SVE_INT_ELTTY(16, 8, true, 1);
  case BuiltinType::SveUint16:
    return SVE_INT_ELTTY(16, 8, false, 1);
  case BuiltinType::SveInt16x2:
    return SVE_INT_ELTTY(16, 8, true, 2);
  case BuiltinType::SveUint16x2:
    return SVE_INT_ELTTY(16, 8, false, 2);
  case BuiltinType::SveInt16x3:
    return SVE_INT_ELTTY(16, 8, true, 3);
  case BuiltinType::SveUint16x3:
    return SVE_INT_ELTTY(16, 8, false, 3);
  case BuiltinType::SveInt16x4:
    return SVE_INT_ELTTY(16, 8, true, 4);
  case BuiltinType::SveUint16x4:
    return SVE_INT_ELTTY(16, 8, false, 4);
  case BuiltinType::SveInt32:
    return SVE_INT_ELTTY(32, 4, true, 1);
  case BuiltinType::SveUint32:
    return SVE_INT_ELTTY(32, 4, false, 1);
  case BuiltinType::SveInt32x2:
    return SVE_INT_ELTTY(32, 4, true, 2);
  case BuiltinType::SveUint32x2:
    return SVE_INT_ELTTY(32, 4, false, 2);
  case BuiltinType::SveInt32x3:
    return SVE_INT_ELTTY(32, 4, true, 3);
  case BuiltinType::SveUint32x3:
    return SVE_INT_ELTTY(32, 4, false, 3);
  case BuiltinType::SveInt32x4:
    return SVE_INT_ELTTY(32, 4, true, 4);
  case BuiltinType::SveUint32x4:
    return SVE_INT_ELTTY(32, 4, false, 4);
  case BuiltinType::SveInt64:
    return SVE_INT_ELTTY(64, 2, true, 1);
  case BuiltinType::SveUint64:
    return SVE_INT_ELTTY(64, 2, false, 1);
  case BuiltinType::SveInt64x2:
    return SVE_INT_ELTTY(64, 2, true, 2);
  case BuiltinType::SveUint64x2:
    return SVE_INT_ELTTY(64, 2, false, 2);
  case BuiltinType::SveInt64x3:
    return SVE_INT_ELTTY(64, 2, true, 3);
  case BuiltinType::SveUint64x3:
    return SVE_INT_ELTTY(64, 2, false, 3);
  case BuiltinType::SveInt64x4:
    return SVE_INT_ELTTY(64, 2, true, 4);
  case BuiltinType::SveUint64x4:
    return SVE_INT_ELTTY(64, 2, false, 4);
  case BuiltinType::SveBool:
    return SVE_ELTTY(BoolTy, 16, 1);
  case BuiltinType::SveBoolx2:
    return SVE_ELTTY(BoolTy, 16, 2);
  case BuiltinType::SveBoolx4:
    return SVE_ELTTY(BoolTy, 16, 4);
  case BuiltinType::SveFloat16:
    return SVE_ELTTY(HalfTy, 8, 1);
  case BuiltinType::SveFloat16x2:
    return SVE_ELTTY(HalfTy, 8, 2);
  case BuiltinType::SveFloat16x3:
    return SVE_ELTTY(HalfTy, 8, 3);
  case BuiltinType::SveFloat16x4:
    return SVE_ELTTY(HalfTy, 8, 4);
  case BuiltinType::SveFloat32:
    return SVE_ELTTY(FloatTy, 4, 1);
  case BuiltinType::SveFloat32x2:
    return SVE_ELTTY(FloatTy, 4, 2);
  case BuiltinType::SveFloat32x3:
    return SVE_ELTTY(FloatTy, 4, 3);
  case BuiltinType::SveFloat32x4:
    return SVE_ELTTY(FloatTy, 4, 4);
  case BuiltinType::SveFloat64:
    return SVE_ELTTY(DoubleTy, 2, 1);
  case BuiltinType::SveFloat64x2:
    return SVE_ELTTY(DoubleTy, 2, 2);
  case BuiltinType::SveFloat64x3:
    return SVE_ELTTY(DoubleTy, 2, 3);
  case BuiltinType::SveFloat64x4:
    return SVE_ELTTY(DoubleTy, 2, 4);
  case BuiltinType::SveBFloat16:
    return SVE_ELTTY(BFloat16Ty, 8, 1);
  case BuiltinType::SveBFloat16x2:
    return SVE_ELTTY(BFloat16Ty, 8, 2);
  case BuiltinType::SveBFloat16x3:
    return SVE_ELTTY(BFloat16Ty, 8, 3);
  case BuiltinType::SveBFloat16x4:
    return SVE_ELTTY(BFloat16Ty, 8, 4);
#define RVV_VECTOR_TYPE_INT(Name, Id, SingletonId, NumEls, ElBits, NF,         \
                            IsSigned)                                          \
  case BuiltinType::Id:                                                        \
    return {getIntTypeForBitwidth(ElBits, IsSigned),                           \
            llvm::ElementCount::getScalable(NumEls), NF};
#define RVV_VECTOR_TYPE_FLOAT(Name, Id, SingletonId, NumEls, ElBits, NF)       \
  case BuiltinType::Id:                                                        \
    return {ElBits == 16 ? Float16Ty : (ElBits == 32 ? FloatTy : DoubleTy),    \
            llvm::ElementCount::getScalable(NumEls), NF};
#define RVV_VECTOR_TYPE_BFLOAT(Name, Id, SingletonId, NumEls, ElBits, NF)      \
  case BuiltinType::Id:                                                        \
    return {BFloat16Ty, llvm::ElementCount::getScalable(NumEls), NF};
#define RVV_PREDICATE_TYPE(Name, Id, SingletonId, NumEls)                      \
  case BuiltinType::Id:                                                        \
    return {BoolTy, llvm::ElementCount::getScalable(NumEls), 1};
#include "clang/Basic/RISCVVTypes.def"
  }
}

/// getExternrefType - Return a WebAssembly externref type, which represents an
/// opaque reference to a host value.
QualType ASTContext::getWebAssemblyExternrefType() const {
  if (Target->getTriple().isWasm() && Target->hasFeature("reference-types")) {
#define WASM_REF_TYPE(Name, MangledName, Id, SingletonId, AS)                  \
  if (BuiltinType::Id == BuiltinType::WasmExternRef)                           \
    return SingletonId;
#include "clang/Basic/WebAssemblyReferenceTypes.def"
  }
  llvm_unreachable(
      "shouldn't try to generate type externref outside WebAssembly target");
}

/// getScalableVectorType - Return the unique reference to a scalable vector
/// type of the specified element type and size. VectorType must be a built-in
/// type.
QualType ASTContext::getScalableVectorType(QualType EltTy, unsigned NumElts,
                                           unsigned NumFields) const {
  if (Target->hasAArch64SVETypes()) {
    uint64_t EltTySize = getTypeSize(EltTy);
#define SVE_VECTOR_TYPE(Name, MangledName, Id, SingletonId, NumEls, ElBits,    \
                        IsSigned, IsFP, IsBF)                                  \
  if (!EltTy->isBooleanType() &&                                               \
      ((EltTy->hasIntegerRepresentation() &&                                   \
        EltTy->hasSignedIntegerRepresentation() == IsSigned) ||                \
       (EltTy->hasFloatingRepresentation() && !EltTy->isBFloat16Type() &&      \
        IsFP && !IsBF) ||                                                      \
       (EltTy->hasFloatingRepresentation() && EltTy->isBFloat16Type() &&       \
        IsBF && !IsFP)) &&                                                     \
      EltTySize == ElBits && NumElts == NumEls) {                              \
    return SingletonId;                                                        \
  }
#define SVE_PREDICATE_TYPE(Name, MangledName, Id, SingletonId, NumEls)         \
  if (EltTy->isBooleanType() && NumElts == NumEls)                             \
    return SingletonId;
#define SVE_OPAQUE_TYPE(Name, MangledName, Id, SingleTonId)
#include "clang/Basic/AArch64SVEACLETypes.def"
  } else if (Target->hasRISCVVTypes()) {
    uint64_t EltTySize = getTypeSize(EltTy);
#define RVV_VECTOR_TYPE(Name, Id, SingletonId, NumEls, ElBits, NF, IsSigned,   \
                        IsFP, IsBF)                                            \
  if (!EltTy->isBooleanType() &&                                               \
      ((EltTy->hasIntegerRepresentation() &&                                   \
        EltTy->hasSignedIntegerRepresentation() == IsSigned) ||                \
       (EltTy->hasFloatingRepresentation() && !EltTy->isBFloat16Type() &&      \
        IsFP && !IsBF) ||                                                      \
       (EltTy->hasFloatingRepresentation() && EltTy->isBFloat16Type() &&       \
        IsBF && !IsFP)) &&                                                     \
      EltTySize == ElBits && NumElts == NumEls && NumFields == NF)             \
    return SingletonId;
#define RVV_PREDICATE_TYPE(Name, Id, SingletonId, NumEls)                      \
  if (EltTy->isBooleanType() && NumElts == NumEls)                             \
    return SingletonId;
#include "clang/Basic/RISCVVTypes.def"
  }
  return QualType();
}

/// getVectorType - Return the unique reference to a vector type of
/// the specified element type and size. VectorType must be a built-in type.
QualType ASTContext::getVectorType(QualType vecType, unsigned NumElts,
                                   VectorKind VecKind) const {
  assert(vecType->isBuiltinType() ||
         (vecType->isBitIntType() &&
          // Only support _BitInt elements with byte-sized power of 2 NumBits.
          llvm::isPowerOf2_32(vecType->castAs<BitIntType>()->getNumBits()) &&
          vecType->castAs<BitIntType>()->getNumBits() >= 8));

  // Check if we've already instantiated a vector of this type.
  llvm::FoldingSetNodeID ID;
  VectorType::Profile(ID, vecType, NumElts, Type::Vector, VecKind);

  void *InsertPos = nullptr;
  if (VectorType *VTP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(VTP, 0);

  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!vecType.isCanonical()) {
    Canonical = getVectorType(getCanonicalType(vecType), NumElts, VecKind);

    // Get the new insert position for the node we care about.
    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }
  auto *New = new (*this, alignof(VectorType))
      VectorType(vecType, NumElts, Canonical, VecKind);
  VectorTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType ASTContext::getDependentVectorType(QualType VecType, Expr *SizeExpr,
                                            SourceLocation AttrLoc,
                                            VectorKind VecKind) const {
  llvm::FoldingSetNodeID ID;
  DependentVectorType::Profile(ID, *this, getCanonicalType(VecType), SizeExpr,
                               VecKind);
  void *InsertPos = nullptr;
  DependentVectorType *Canon =
      DependentVectorTypes.FindNodeOrInsertPos(ID, InsertPos);
  DependentVectorType *New;

  if (Canon) {
    New = new (*this, alignof(DependentVectorType)) DependentVectorType(
        VecType, QualType(Canon, 0), SizeExpr, AttrLoc, VecKind);
  } else {
    QualType CanonVecTy = getCanonicalType(VecType);
    if (CanonVecTy == VecType) {
      New = new (*this, alignof(DependentVectorType))
          DependentVectorType(VecType, QualType(), SizeExpr, AttrLoc, VecKind);

      DependentVectorType *CanonCheck =
          DependentVectorTypes.FindNodeOrInsertPos(ID, InsertPos);
      assert(!CanonCheck &&
             "Dependent-sized vector_size canonical type broken");
      (void)CanonCheck;
      DependentVectorTypes.InsertNode(New, InsertPos);
    } else {
      QualType CanonTy = getDependentVectorType(CanonVecTy, SizeExpr,
                                                SourceLocation(), VecKind);
      New = new (*this, alignof(DependentVectorType))
          DependentVectorType(VecType, CanonTy, SizeExpr, AttrLoc, VecKind);
    }
  }

  Types.push_back(New);
  return QualType(New, 0);
}

/// getExtVectorType - Return the unique reference to an extended vector type of
/// the specified element type and size. VectorType must be a built-in type.
QualType ASTContext::getExtVectorType(QualType vecType,
                                      unsigned NumElts) const {
  assert(vecType->isBuiltinType() || vecType->isDependentType() ||
         (vecType->isBitIntType() &&
          // Only support _BitInt elements with byte-sized power of 2 NumBits.
          llvm::isPowerOf2_32(vecType->castAs<BitIntType>()->getNumBits()) &&
          vecType->castAs<BitIntType>()->getNumBits() >= 8));

  // Check if we've already instantiated a vector of this type.
  llvm::FoldingSetNodeID ID;
  VectorType::Profile(ID, vecType, NumElts, Type::ExtVector,
                      VectorKind::Generic);
  void *InsertPos = nullptr;
  if (VectorType *VTP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(VTP, 0);

  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!vecType.isCanonical()) {
    Canonical = getExtVectorType(getCanonicalType(vecType), NumElts);

    // Get the new insert position for the node we care about.
    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }
  auto *New = new (*this, alignof(ExtVectorType))
      ExtVectorType(vecType, NumElts, Canonical);
  VectorTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType
ASTContext::getDependentSizedExtVectorType(QualType vecType,
                                           Expr *SizeExpr,
                                           SourceLocation AttrLoc) const {
  llvm::FoldingSetNodeID ID;
  DependentSizedExtVectorType::Profile(ID, *this, getCanonicalType(vecType),
                                       SizeExpr);

  void *InsertPos = nullptr;
  DependentSizedExtVectorType *Canon
    = DependentSizedExtVectorTypes.FindNodeOrInsertPos(ID, InsertPos);
  DependentSizedExtVectorType *New;
  if (Canon) {
    // We already have a canonical version of this array type; use it as
    // the canonical type for a newly-built type.
    New = new (*this, alignof(DependentSizedExtVectorType))
        DependentSizedExtVectorType(vecType, QualType(Canon, 0), SizeExpr,
                                    AttrLoc);
  } else {
    QualType CanonVecTy = getCanonicalType(vecType);
    if (CanonVecTy == vecType) {
      New = new (*this, alignof(DependentSizedExtVectorType))
          DependentSizedExtVectorType(vecType, QualType(), SizeExpr, AttrLoc);

      DependentSizedExtVectorType *CanonCheck
        = DependentSizedExtVectorTypes.FindNodeOrInsertPos(ID, InsertPos);
      assert(!CanonCheck && "Dependent-sized ext_vector canonical type broken");
      (void)CanonCheck;
      DependentSizedExtVectorTypes.InsertNode(New, InsertPos);
    } else {
      QualType CanonExtTy = getDependentSizedExtVectorType(CanonVecTy, SizeExpr,
                                                           SourceLocation());
      New = new (*this, alignof(DependentSizedExtVectorType))
          DependentSizedExtVectorType(vecType, CanonExtTy, SizeExpr, AttrLoc);
    }
  }

  Types.push_back(New);
  return QualType(New, 0);
}

QualType ASTContext::getConstantMatrixType(QualType ElementTy, unsigned NumRows,
                                           unsigned NumColumns) const {
  llvm::FoldingSetNodeID ID;
  ConstantMatrixType::Profile(ID, ElementTy, NumRows, NumColumns,
                              Type::ConstantMatrix);

  assert(MatrixType::isValidElementType(ElementTy) &&
         "need a valid element type");
  assert(ConstantMatrixType::isDimensionValid(NumRows) &&
         ConstantMatrixType::isDimensionValid(NumColumns) &&
         "need valid matrix dimensions");
  void *InsertPos = nullptr;
  if (ConstantMatrixType *MTP = MatrixTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(MTP, 0);

  QualType Canonical;
  if (!ElementTy.isCanonical()) {
    Canonical =
        getConstantMatrixType(getCanonicalType(ElementTy), NumRows, NumColumns);

    ConstantMatrixType *NewIP = MatrixTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Matrix type shouldn't already exist in the map");
    (void)NewIP;
  }

  auto *New = new (*this, alignof(ConstantMatrixType))
      ConstantMatrixType(ElementTy, NumRows, NumColumns, Canonical);
  MatrixTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType ASTContext::getDependentSizedMatrixType(QualType ElementTy,
                                                 Expr *RowExpr,
                                                 Expr *ColumnExpr,
                                                 SourceLocation AttrLoc) const {
  QualType CanonElementTy = getCanonicalType(ElementTy);
  llvm::FoldingSetNodeID ID;
  DependentSizedMatrixType::Profile(ID, *this, CanonElementTy, RowExpr,
                                    ColumnExpr);

  void *InsertPos = nullptr;
  DependentSizedMatrixType *Canon =
      DependentSizedMatrixTypes.FindNodeOrInsertPos(ID, InsertPos);

  if (!Canon) {
    Canon = new (*this, alignof(DependentSizedMatrixType))
        DependentSizedMatrixType(CanonElementTy, QualType(), RowExpr,
                                 ColumnExpr, AttrLoc);
#ifndef NDEBUG
    DependentSizedMatrixType *CanonCheck =
        DependentSizedMatrixTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!CanonCheck && "Dependent-sized matrix canonical type broken");
#endif
    DependentSizedMatrixTypes.InsertNode(Canon, InsertPos);
    Types.push_back(Canon);
  }

  // Already have a canonical version of the matrix type
  //
  // If it exactly matches the requested type, use it directly.
  if (Canon->getElementType() == ElementTy && Canon->getRowExpr() == RowExpr &&
      Canon->getRowExpr() == ColumnExpr)
    return QualType(Canon, 0);

  // Use Canon as the canonical type for newly-built type.
  DependentSizedMatrixType *New = new (*this, alignof(DependentSizedMatrixType))
      DependentSizedMatrixType(ElementTy, QualType(Canon, 0), RowExpr,
                               ColumnExpr, AttrLoc);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType ASTContext::getDependentAddressSpaceType(QualType PointeeType,
                                                  Expr *AddrSpaceExpr,
                                                  SourceLocation AttrLoc) const {
  assert(AddrSpaceExpr->isInstantiationDependent());

  QualType canonPointeeType = getCanonicalType(PointeeType);

  void *insertPos = nullptr;
  llvm::FoldingSetNodeID ID;
  DependentAddressSpaceType::Profile(ID, *this, canonPointeeType,
                                     AddrSpaceExpr);

  DependentAddressSpaceType *canonTy =
    DependentAddressSpaceTypes.FindNodeOrInsertPos(ID, insertPos);

  if (!canonTy) {
    canonTy = new (*this, alignof(DependentAddressSpaceType))
        DependentAddressSpaceType(canonPointeeType, QualType(), AddrSpaceExpr,
                                  AttrLoc);
    DependentAddressSpaceTypes.InsertNode(canonTy, insertPos);
    Types.push_back(canonTy);
  }

  if (canonPointeeType == PointeeType &&
      canonTy->getAddrSpaceExpr() == AddrSpaceExpr)
    return QualType(canonTy, 0);

  auto *sugaredType = new (*this, alignof(DependentAddressSpaceType))
      DependentAddressSpaceType(PointeeType, QualType(canonTy, 0),
                                AddrSpaceExpr, AttrLoc);
  Types.push_back(sugaredType);
  return QualType(sugaredType, 0);
}

/// Determine whether \p T is canonical as the result type of a function.
static bool isCanonicalResultType(QualType T) {
  return T.isCanonical() &&
         (T.getObjCLifetime() == Qualifiers::OCL_None ||
          T.getObjCLifetime() == Qualifiers::OCL_ExplicitNone);
}

/// getFunctionNoProtoType - Return a K&R style C function type like 'int()'.
QualType
ASTContext::getFunctionNoProtoType(QualType ResultTy,
                                   const FunctionType::ExtInfo &Info) const {
  // FIXME: This assertion cannot be enabled (yet) because the ObjC rewriter
  // functionality creates a function without a prototype regardless of
  // language mode (so it makes them even in C++). Once the rewriter has been
  // fixed, this assertion can be enabled again.
  //assert(!LangOpts.requiresStrictPrototypes() &&
  //       "strict prototypes are disabled");

  // Unique functions, to guarantee there is only one function of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  FunctionNoProtoType::Profile(ID, ResultTy, Info);

  void *InsertPos = nullptr;
  if (FunctionNoProtoType *FT =
        FunctionNoProtoTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(FT, 0);

  QualType Canonical;
  if (!isCanonicalResultType(ResultTy)) {
    Canonical =
      getFunctionNoProtoType(getCanonicalFunctionResultType(ResultTy), Info);

    // Get the new insert position for the node we care about.
    FunctionNoProtoType *NewIP =
      FunctionNoProtoTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }

  auto *New = new (*this, alignof(FunctionNoProtoType))
      FunctionNoProtoType(ResultTy, Canonical, Info);
  Types.push_back(New);
  FunctionNoProtoTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

CanQualType
ASTContext::getCanonicalFunctionResultType(QualType ResultType) const {
  CanQualType CanResultType = getCanonicalType(ResultType);

  // Canonical result types do not have ARC lifetime qualifiers.
  if (CanResultType.getQualifiers().hasObjCLifetime()) {
    Qualifiers Qs = CanResultType.getQualifiers();
    Qs.removeObjCLifetime();
    return CanQualType::CreateUnsafe(
             getQualifiedType(CanResultType.getUnqualifiedType(), Qs));
  }

  return CanResultType;
}

static bool isCanonicalExceptionSpecification(
    const FunctionProtoType::ExceptionSpecInfo &ESI, bool NoexceptInType) {
  if (ESI.Type == EST_None)
    return true;
  if (!NoexceptInType)
    return false;

  // C++17 onwards: exception specification is part of the type, as a simple
  // boolean "can this function type throw".
  if (ESI.Type == EST_BasicNoexcept)
    return true;

  // A noexcept(expr) specification is (possibly) canonical if expr is
  // value-dependent.
  if (ESI.Type == EST_DependentNoexcept)
    return true;

  // A dynamic exception specification is canonical if it only contains pack
  // expansions (so we can't tell whether it's non-throwing) and all its
  // contained types are canonical.
  if (ESI.Type == EST_Dynamic) {
    bool AnyPackExpansions = false;
    for (QualType ET : ESI.Exceptions) {
      if (!ET.isCanonical())
        return false;
      if (ET->getAs<PackExpansionType>())
        AnyPackExpansions = true;
    }
    return AnyPackExpansions;
  }

  return false;
}

QualType ASTContext::getFunctionTypeInternal(
    QualType ResultTy, ArrayRef<QualType> ArgArray,
    const FunctionProtoType::ExtProtoInfo &EPI, bool OnlyWantCanonical) const {
  size_t NumArgs = ArgArray.size();

  // Unique functions, to guarantee there is only one function of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  FunctionProtoType::Profile(ID, ResultTy, ArgArray.begin(), NumArgs, EPI,
                             *this, true);

  QualType Canonical;
  bool Unique = false;

  void *InsertPos = nullptr;
  if (FunctionProtoType *FPT =
        FunctionProtoTypes.FindNodeOrInsertPos(ID, InsertPos)) {
    QualType Existing = QualType(FPT, 0);

    // If we find a pre-existing equivalent FunctionProtoType, we can just reuse
    // it so long as our exception specification doesn't contain a dependent
    // noexcept expression, or we're just looking for a canonical type.
    // Otherwise, we're going to need to create a type
    // sugar node to hold the concrete expression.
    if (OnlyWantCanonical || !isComputedNoexcept(EPI.ExceptionSpec.Type) ||
        EPI.ExceptionSpec.NoexceptExpr == FPT->getNoexceptExpr())
      return Existing;

    // We need a new type sugar node for this one, to hold the new noexcept
    // expression. We do no canonicalization here, but that's OK since we don't
    // expect to see the same noexcept expression much more than once.
    Canonical = getCanonicalType(Existing);
    Unique = true;
  }

  bool NoexceptInType = getLangOpts().CPlusPlus17;
  bool IsCanonicalExceptionSpec =
      isCanonicalExceptionSpecification(EPI.ExceptionSpec, NoexceptInType);

  // Determine whether the type being created is already canonical or not.
  bool isCanonical = !Unique && IsCanonicalExceptionSpec &&
                     isCanonicalResultType(ResultTy) && !EPI.HasTrailingReturn;
  for (unsigned i = 0; i != NumArgs && isCanonical; ++i)
    if (!ArgArray[i].isCanonicalAsParam())
      isCanonical = false;

  if (OnlyWantCanonical)
    assert(isCanonical &&
           "given non-canonical parameters constructing canonical type");

  // If this type isn't canonical, get the canonical version of it if we don't
  // already have it. The exception spec is only partially part of the
  // canonical type, and only in C++17 onwards.
  if (!isCanonical && Canonical.isNull()) {
    SmallVector<QualType, 16> CanonicalArgs;
    CanonicalArgs.reserve(NumArgs);
    for (unsigned i = 0; i != NumArgs; ++i)
      CanonicalArgs.push_back(getCanonicalParamType(ArgArray[i]));

    llvm::SmallVector<QualType, 8> ExceptionTypeStorage;
    FunctionProtoType::ExtProtoInfo CanonicalEPI = EPI;
    CanonicalEPI.HasTrailingReturn = false;

    if (IsCanonicalExceptionSpec) {
      // Exception spec is already OK.
    } else if (NoexceptInType) {
      switch (EPI.ExceptionSpec.Type) {
      case EST_Unparsed: case EST_Unevaluated: case EST_Uninstantiated:
        // We don't know yet. It shouldn't matter what we pick here; no-one
        // should ever look at this.
        [[fallthrough]];
      case EST_None: case EST_MSAny: case EST_NoexceptFalse:
        CanonicalEPI.ExceptionSpec.Type = EST_None;
        break;

        // A dynamic exception specification is almost always "not noexcept",
        // with the exception that a pack expansion might expand to no types.
      case EST_Dynamic: {
        bool AnyPacks = false;
        for (QualType ET : EPI.ExceptionSpec.Exceptions) {
          if (ET->getAs<PackExpansionType>())
            AnyPacks = true;
          ExceptionTypeStorage.push_back(getCanonicalType(ET));
        }
        if (!AnyPacks)
          CanonicalEPI.ExceptionSpec.Type = EST_None;
        else {
          CanonicalEPI.ExceptionSpec.Type = EST_Dynamic;
          CanonicalEPI.ExceptionSpec.Exceptions = ExceptionTypeStorage;
        }
        break;
      }

      case EST_DynamicNone:
      case EST_BasicNoexcept:
      case EST_NoexceptTrue:
      case EST_NoThrow:
        CanonicalEPI.ExceptionSpec.Type = EST_BasicNoexcept;
        break;

      case EST_DependentNoexcept:
        llvm_unreachable("dependent noexcept is already canonical");
      }
    } else {
      CanonicalEPI.ExceptionSpec = FunctionProtoType::ExceptionSpecInfo();
    }

    // Adjust the canonical function result type.
    CanQualType CanResultTy = getCanonicalFunctionResultType(ResultTy);
    Canonical =
        getFunctionTypeInternal(CanResultTy, CanonicalArgs, CanonicalEPI, true);

    // Get the new insert position for the node we care about.
    FunctionProtoType *NewIP =
      FunctionProtoTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }

  // Compute the needed size to hold this FunctionProtoType and the
  // various trailing objects.
  auto ESH = FunctionProtoType::getExceptionSpecSize(
      EPI.ExceptionSpec.Type, EPI.ExceptionSpec.Exceptions.size());
  size_t Size = FunctionProtoType::totalSizeToAlloc<
      QualType, SourceLocation, FunctionType::FunctionTypeExtraBitfields,
      FunctionType::FunctionTypeArmAttributes, FunctionType::ExceptionType,
      Expr *, FunctionDecl *, FunctionProtoType::ExtParameterInfo, Qualifiers,
      FunctionEffect, EffectConditionExpr>(
      NumArgs, EPI.Variadic, EPI.requiresFunctionProtoTypeExtraBitfields(),
      EPI.requiresFunctionProtoTypeArmAttributes(), ESH.NumExceptionType,
      ESH.NumExprPtr, ESH.NumFunctionDeclPtr,
      EPI.ExtParameterInfos ? NumArgs : 0,
      EPI.TypeQuals.hasNonFastQualifiers() ? 1 : 0, EPI.FunctionEffects.size(),
      EPI.FunctionEffects.conditions().size());

  auto *FTP = (FunctionProtoType *)Allocate(Size, alignof(FunctionProtoType));
  FunctionProtoType::ExtProtoInfo newEPI = EPI;
  new (FTP) FunctionProtoType(ResultTy, ArgArray, Canonical, newEPI);
  Types.push_back(FTP);
  if (!Unique)
    FunctionProtoTypes.InsertNode(FTP, InsertPos);
  if (!EPI.FunctionEffects.empty())
    AnyFunctionEffects = true;
  return QualType(FTP, 0);
}

QualType ASTContext::getPipeType(QualType T, bool ReadOnly) const {
  llvm::FoldingSetNodeID ID;
  PipeType::Profile(ID, T, ReadOnly);

  void *InsertPos = nullptr;
  if (PipeType *PT = PipeTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(PT, 0);

  // If the pipe element type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getPipeType(getCanonicalType(T), ReadOnly);

    // Get the new insert position for the node we care about.
    PipeType *NewIP = PipeTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }
  auto *New = new (*this, alignof(PipeType)) PipeType(T, Canonical, ReadOnly);
  Types.push_back(New);
  PipeTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

QualType ASTContext::adjustStringLiteralBaseType(QualType Ty) const {
  // OpenCL v1.1 s6.5.3: a string literal is in the constant address space.
  return LangOpts.OpenCL ? getAddrSpaceQualType(Ty, LangAS::opencl_constant)
                         : Ty;
}

QualType ASTContext::getReadPipeType(QualType T) const {
  return getPipeType(T, true);
}

QualType ASTContext::getWritePipeType(QualType T) const {
  return getPipeType(T, false);
}

QualType ASTContext::getBitIntType(bool IsUnsigned, unsigned NumBits) const {
  llvm::FoldingSetNodeID ID;
  BitIntType::Profile(ID, IsUnsigned, NumBits);

  void *InsertPos = nullptr;
  if (BitIntType *EIT = BitIntTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(EIT, 0);

  auto *New = new (*this, alignof(BitIntType)) BitIntType(IsUnsigned, NumBits);
  BitIntTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType ASTContext::getDependentBitIntType(bool IsUnsigned,
                                            Expr *NumBitsExpr) const {
  assert(NumBitsExpr->isInstantiationDependent() && "Only good for dependent");
  llvm::FoldingSetNodeID ID;
  DependentBitIntType::Profile(ID, *this, IsUnsigned, NumBitsExpr);

  void *InsertPos = nullptr;
  if (DependentBitIntType *Existing =
          DependentBitIntTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(Existing, 0);

  auto *New = new (*this, alignof(DependentBitIntType))
      DependentBitIntType(IsUnsigned, NumBitsExpr);
  DependentBitIntTypes.InsertNode(New, InsertPos);

  Types.push_back(New);
  return QualType(New, 0);
}

#ifndef NDEBUG
static bool NeedsInjectedClassNameType(const RecordDecl *D) {
  if (!isa<CXXRecordDecl>(D)) return false;
  const auto *RD = cast<CXXRecordDecl>(D);
  if (isa<ClassTemplatePartialSpecializationDecl>(RD))
    return true;
  if (RD->getDescribedClassTemplate() &&
      !isa<ClassTemplateSpecializationDecl>(RD))
    return true;
  return false;
}
#endif

/// getInjectedClassNameType - Return the unique reference to the
/// injected class name type for the specified templated declaration.
QualType ASTContext::getInjectedClassNameType(CXXRecordDecl *Decl,
                                              QualType TST) const {
  assert(NeedsInjectedClassNameType(Decl));
  if (Decl->TypeForDecl) {
    assert(isa<InjectedClassNameType>(Decl->TypeForDecl));
  } else if (CXXRecordDecl *PrevDecl = Decl->getPreviousDecl()) {
    assert(PrevDecl->TypeForDecl && "previous declaration has no type");
    Decl->TypeForDecl = PrevDecl->TypeForDecl;
    assert(isa<InjectedClassNameType>(Decl->TypeForDecl));
  } else {
    Type *newType = new (*this, alignof(InjectedClassNameType))
        InjectedClassNameType(Decl, TST);
    Decl->TypeForDecl = newType;
    Types.push_back(newType);
  }
  return QualType(Decl->TypeForDecl, 0);
}

/// getTypeDeclType - Return the unique reference to the type for the
/// specified type declaration.
QualType ASTContext::getTypeDeclTypeSlow(const TypeDecl *Decl) const {
  assert(Decl && "Passed null for Decl param");
  assert(!Decl->TypeForDecl && "TypeForDecl present in slow case");

  if (const auto *Typedef = dyn_cast<TypedefNameDecl>(Decl))
    return getTypedefType(Typedef);

  assert(!isa<TemplateTypeParmDecl>(Decl) &&
         "Template type parameter types are always available.");

  if (const auto *Record = dyn_cast<RecordDecl>(Decl)) {
    assert(Record->isFirstDecl() && "struct/union has previous declaration");
    assert(!NeedsInjectedClassNameType(Record));
    return getRecordType(Record);
  } else if (const auto *Enum = dyn_cast<EnumDecl>(Decl)) {
    assert(Enum->isFirstDecl() && "enum has previous declaration");
    return getEnumType(Enum);
  } else if (const auto *Using = dyn_cast<UnresolvedUsingTypenameDecl>(Decl)) {
    return getUnresolvedUsingType(Using);
  } else
    llvm_unreachable("TypeDecl without a type?");

  return QualType(Decl->TypeForDecl, 0);
}

/// getTypedefType - Return the unique reference to the type for the
/// specified typedef name decl.
QualType ASTContext::getTypedefType(const TypedefNameDecl *Decl,
                                    QualType Underlying) const {
  if (!Decl->TypeForDecl) {
    if (Underlying.isNull())
      Underlying = Decl->getUnderlyingType();
    auto *NewType = new (*this, alignof(TypedefType)) TypedefType(
        Type::Typedef, Decl, QualType(), getCanonicalType(Underlying));
    Decl->TypeForDecl = NewType;
    Types.push_back(NewType);
    return QualType(NewType, 0);
  }
  if (Underlying.isNull() || Decl->getUnderlyingType() == Underlying)
    return QualType(Decl->TypeForDecl, 0);
  assert(hasSameType(Decl->getUnderlyingType(), Underlying));

  llvm::FoldingSetNodeID ID;
  TypedefType::Profile(ID, Decl, Underlying);

  void *InsertPos = nullptr;
  if (TypedefType *T = TypedefTypes.FindNodeOrInsertPos(ID, InsertPos)) {
    assert(!T->typeMatchesDecl() &&
           "non-divergent case should be handled with TypeDecl");
    return QualType(T, 0);
  }

  void *Mem = Allocate(TypedefType::totalSizeToAlloc<QualType>(true),
                       alignof(TypedefType));
  auto *NewType = new (Mem) TypedefType(Type::Typedef, Decl, Underlying,
                                        getCanonicalType(Underlying));
  TypedefTypes.InsertNode(NewType, InsertPos);
  Types.push_back(NewType);
  return QualType(NewType, 0);
}

QualType ASTContext::getUsingType(const UsingShadowDecl *Found,
                                  QualType Underlying) const {
  llvm::FoldingSetNodeID ID;
  UsingType::Profile(ID, Found, Underlying);

  void *InsertPos = nullptr;
  if (UsingType *T = UsingTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(T, 0);

  const Type *TypeForDecl =
      cast<TypeDecl>(Found->getTargetDecl())->getTypeForDecl();

  assert(!Underlying.hasLocalQualifiers());
  QualType Canon = Underlying->getCanonicalTypeInternal();
  assert(TypeForDecl->getCanonicalTypeInternal() == Canon);

  if (Underlying.getTypePtr() == TypeForDecl)
    Underlying = QualType();
  void *Mem =
      Allocate(UsingType::totalSizeToAlloc<QualType>(!Underlying.isNull()),
               alignof(UsingType));
  UsingType *NewType = new (Mem) UsingType(Found, Underlying, Canon);
  Types.push_back(NewType);
  UsingTypes.InsertNode(NewType, InsertPos);
  return QualType(NewType, 0);
}

QualType ASTContext::getRecordType(const RecordDecl *Decl) const {
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);

  if (const RecordDecl *PrevDecl = Decl->getPreviousDecl())
    if (PrevDecl->TypeForDecl)
      return QualType(Decl->TypeForDecl = PrevDecl->TypeForDecl, 0);

  auto *newType = new (*this, alignof(RecordType)) RecordType(Decl);
  Decl->TypeForDecl = newType;
  Types.push_back(newType);
  return QualType(newType, 0);
}

QualType ASTContext::getEnumType(const EnumDecl *Decl) const {
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);

  if (const EnumDecl *PrevDecl = Decl->getPreviousDecl())
    if (PrevDecl->TypeForDecl)
      return QualType(Decl->TypeForDecl = PrevDecl->TypeForDecl, 0);

  auto *newType = new (*this, alignof(EnumType)) EnumType(Decl);
  Decl->TypeForDecl = newType;
  Types.push_back(newType);
  return QualType(newType, 0);
}

QualType ASTContext::getUnresolvedUsingType(
    const UnresolvedUsingTypenameDecl *Decl) const {
  if (Decl->TypeForDecl)
    return QualType(Decl->TypeForDecl, 0);

  if (const UnresolvedUsingTypenameDecl *CanonicalDecl =
          Decl->getCanonicalDecl())
    if (CanonicalDecl->TypeForDecl)
      return QualType(Decl->TypeForDecl = CanonicalDecl->TypeForDecl, 0);

  Type *newType =
      new (*this, alignof(UnresolvedUsingType)) UnresolvedUsingType(Decl);
  Decl->TypeForDecl = newType;
  Types.push_back(newType);
  return QualType(newType, 0);
}

QualType ASTContext::getAttributedType(attr::Kind attrKind,
                                       QualType modifiedType,
                                       QualType equivalentType) const {
  llvm::FoldingSetNodeID id;
  AttributedType::Profile(id, attrKind, modifiedType, equivalentType);

  void *insertPos = nullptr;
  AttributedType *type = AttributedTypes.FindNodeOrInsertPos(id, insertPos);
  if (type) return QualType(type, 0);

  QualType canon = getCanonicalType(equivalentType);
  type = new (*this, alignof(AttributedType))
      AttributedType(canon, attrKind, modifiedType, equivalentType);

  Types.push_back(type);
  AttributedTypes.InsertNode(type, insertPos);

  return QualType(type, 0);
}

QualType ASTContext::getBTFTagAttributedType(const BTFTypeTagAttr *BTFAttr,
                                             QualType Wrapped) {
  llvm::FoldingSetNodeID ID;
  BTFTagAttributedType::Profile(ID, Wrapped, BTFAttr);

  void *InsertPos = nullptr;
  BTFTagAttributedType *Ty =
      BTFTagAttributedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (Ty)
    return QualType(Ty, 0);

  QualType Canon = getCanonicalType(Wrapped);
  Ty = new (*this, alignof(BTFTagAttributedType))
      BTFTagAttributedType(Canon, Wrapped, BTFAttr);

  Types.push_back(Ty);
  BTFTagAttributedTypes.InsertNode(Ty, InsertPos);

  return QualType(Ty, 0);
}

/// Retrieve a substitution-result type.
QualType ASTContext::getSubstTemplateTypeParmType(
    QualType Replacement, Decl *AssociatedDecl, unsigned Index,
    std::optional<unsigned> PackIndex) const {
  llvm::FoldingSetNodeID ID;
  SubstTemplateTypeParmType::Profile(ID, Replacement, AssociatedDecl, Index,
                                     PackIndex);
  void *InsertPos = nullptr;
  SubstTemplateTypeParmType *SubstParm =
      SubstTemplateTypeParmTypes.FindNodeOrInsertPos(ID, InsertPos);

  if (!SubstParm) {
    void *Mem = Allocate(SubstTemplateTypeParmType::totalSizeToAlloc<QualType>(
                             !Replacement.isCanonical()),
                         alignof(SubstTemplateTypeParmType));
    SubstParm = new (Mem) SubstTemplateTypeParmType(Replacement, AssociatedDecl,
                                                    Index, PackIndex);
    Types.push_back(SubstParm);
    SubstTemplateTypeParmTypes.InsertNode(SubstParm, InsertPos);
  }

  return QualType(SubstParm, 0);
}

/// Retrieve a
QualType
ASTContext::getSubstTemplateTypeParmPackType(Decl *AssociatedDecl,
                                             unsigned Index, bool Final,
                                             const TemplateArgument &ArgPack) {
#ifndef NDEBUG
  for (const auto &P : ArgPack.pack_elements())
    assert(P.getKind() == TemplateArgument::Type && "Pack contains a non-type");
#endif

  llvm::FoldingSetNodeID ID;
  SubstTemplateTypeParmPackType::Profile(ID, AssociatedDecl, Index, Final,
                                         ArgPack);
  void *InsertPos = nullptr;
  if (SubstTemplateTypeParmPackType *SubstParm =
          SubstTemplateTypeParmPackTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(SubstParm, 0);

  QualType Canon;
  {
    TemplateArgument CanonArgPack = getCanonicalTemplateArgument(ArgPack);
    if (!AssociatedDecl->isCanonicalDecl() ||
        !CanonArgPack.structurallyEquals(ArgPack)) {
      Canon = getSubstTemplateTypeParmPackType(
          AssociatedDecl->getCanonicalDecl(), Index, Final, CanonArgPack);
      [[maybe_unused]] const auto *Nothing =
          SubstTemplateTypeParmPackTypes.FindNodeOrInsertPos(ID, InsertPos);
      assert(!Nothing);
    }
  }

  auto *SubstParm = new (*this, alignof(SubstTemplateTypeParmPackType))
      SubstTemplateTypeParmPackType(Canon, AssociatedDecl, Index, Final,
                                    ArgPack);
  Types.push_back(SubstParm);
  SubstTemplateTypeParmPackTypes.InsertNode(SubstParm, InsertPos);
  return QualType(SubstParm, 0);
}

/// Retrieve the template type parameter type for a template
/// parameter or parameter pack with the given depth, index, and (optionally)
/// name.
QualType ASTContext::getTemplateTypeParmType(unsigned Depth, unsigned Index,
                                             bool ParameterPack,
                                             TemplateTypeParmDecl *TTPDecl) const {
  llvm::FoldingSetNodeID ID;
  TemplateTypeParmType::Profile(ID, Depth, Index, ParameterPack, TTPDecl);
  void *InsertPos = nullptr;
  TemplateTypeParmType *TypeParm
    = TemplateTypeParmTypes.FindNodeOrInsertPos(ID, InsertPos);

  if (TypeParm)
    return QualType(TypeParm, 0);

  if (TTPDecl) {
    QualType Canon = getTemplateTypeParmType(Depth, Index, ParameterPack);
    TypeParm = new (*this, alignof(TemplateTypeParmType))
        TemplateTypeParmType(TTPDecl, Canon);

    TemplateTypeParmType *TypeCheck
      = TemplateTypeParmTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!TypeCheck && "Template type parameter canonical type broken");
    (void)TypeCheck;
  } else
    TypeParm = new (*this, alignof(TemplateTypeParmType))
        TemplateTypeParmType(Depth, Index, ParameterPack);

  Types.push_back(TypeParm);
  TemplateTypeParmTypes.InsertNode(TypeParm, InsertPos);

  return QualType(TypeParm, 0);
}

TypeSourceInfo *
ASTContext::getTemplateSpecializationTypeInfo(TemplateName Name,
                                              SourceLocation NameLoc,
                                        const TemplateArgumentListInfo &Args,
                                              QualType Underlying) const {
  assert(!Name.getAsDependentTemplateName() &&
         "No dependent template names here!");
  QualType TST =
      getTemplateSpecializationType(Name, Args.arguments(), Underlying);

  TypeSourceInfo *DI = CreateTypeSourceInfo(TST);
  TemplateSpecializationTypeLoc TL =
      DI->getTypeLoc().castAs<TemplateSpecializationTypeLoc>();
  TL.setTemplateKeywordLoc(SourceLocation());
  TL.setTemplateNameLoc(NameLoc);
  TL.setLAngleLoc(Args.getLAngleLoc());
  TL.setRAngleLoc(Args.getRAngleLoc());
  for (unsigned i = 0, e = TL.getNumArgs(); i != e; ++i)
    TL.setArgLocInfo(i, Args[i].getLocInfo());
  return DI;
}

QualType
ASTContext::getTemplateSpecializationType(TemplateName Template,
                                          ArrayRef<TemplateArgumentLoc> Args,
                                          QualType Underlying) const {
  assert(!Template.getAsDependentTemplateName() &&
         "No dependent template names here!");

  SmallVector<TemplateArgument, 4> ArgVec;
  ArgVec.reserve(Args.size());
  for (const TemplateArgumentLoc &Arg : Args)
    ArgVec.push_back(Arg.getArgument());

  return getTemplateSpecializationType(Template, ArgVec, Underlying);
}

#ifndef NDEBUG
static bool hasAnyPackExpansions(ArrayRef<TemplateArgument> Args) {
  for (const TemplateArgument &Arg : Args)
    if (Arg.isPackExpansion())
      return true;

  return true;
}
#endif

QualType
ASTContext::getTemplateSpecializationType(TemplateName Template,
                                          ArrayRef<TemplateArgument> Args,
                                          QualType Underlying) const {
  assert(!Template.getAsDependentTemplateName() &&
         "No dependent template names here!");

  const auto *TD = Template.getAsTemplateDecl();
  bool IsTypeAlias = TD && TD->isTypeAlias();
  QualType CanonType;
  if (!Underlying.isNull())
    CanonType = getCanonicalType(Underlying);
  else {
    // We can get here with an alias template when the specialization contains
    // a pack expansion that does not match up with a parameter pack.
    assert((!IsTypeAlias || hasAnyPackExpansions(Args)) &&
           "Caller must compute aliased type");
    IsTypeAlias = false;
    CanonType = getCanonicalTemplateSpecializationType(Template, Args);
  }

  // Allocate the (non-canonical) template specialization type, but don't
  // try to unique it: these types typically have location information that
  // we don't unique and don't want to lose.
  void *Mem = Allocate(sizeof(TemplateSpecializationType) +
                           sizeof(TemplateArgument) * Args.size() +
                           (IsTypeAlias ? sizeof(QualType) : 0),
                       alignof(TemplateSpecializationType));
  auto *Spec
    = new (Mem) TemplateSpecializationType(Template, Args, CanonType,
                                         IsTypeAlias ? Underlying : QualType());

  Types.push_back(Spec);
  return QualType(Spec, 0);
}

QualType ASTContext::getCanonicalTemplateSpecializationType(
    TemplateName Template, ArrayRef<TemplateArgument> Args) const {
  assert(!Template.getAsDependentTemplateName() &&
         "No dependent template names here!");

  // Build the canonical template specialization type.
  TemplateName CanonTemplate = getCanonicalTemplateName(Template);
  bool AnyNonCanonArgs = false;
  auto CanonArgs =
      ::getCanonicalTemplateArguments(*this, Args, AnyNonCanonArgs);

  // Determine whether this canonical template specialization type already
  // exists.
  llvm::FoldingSetNodeID ID;
  TemplateSpecializationType::Profile(ID, CanonTemplate,
                                      CanonArgs, *this);

  void *InsertPos = nullptr;
  TemplateSpecializationType *Spec
    = TemplateSpecializationTypes.FindNodeOrInsertPos(ID, InsertPos);

  if (!Spec) {
    // Allocate a new canonical template specialization type.
    void *Mem = Allocate((sizeof(TemplateSpecializationType) +
                          sizeof(TemplateArgument) * CanonArgs.size()),
                         alignof(TemplateSpecializationType));
    Spec = new (Mem) TemplateSpecializationType(CanonTemplate,
                                                CanonArgs,
                                                QualType(), QualType());
    Types.push_back(Spec);
    TemplateSpecializationTypes.InsertNode(Spec, InsertPos);
  }

  assert(Spec->isDependentType() &&
         "Non-dependent template-id type must have a canonical type");
  return QualType(Spec, 0);
}

QualType ASTContext::getElaboratedType(ElaboratedTypeKeyword Keyword,
                                       NestedNameSpecifier *NNS,
                                       QualType NamedType,
                                       TagDecl *OwnedTagDecl) const {
  llvm::FoldingSetNodeID ID;
  ElaboratedType::Profile(ID, Keyword, NNS, NamedType, OwnedTagDecl);

  void *InsertPos = nullptr;
  ElaboratedType *T = ElaboratedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (T)
    return QualType(T, 0);

  QualType Canon = NamedType;
  if (!Canon.isCanonical()) {
    Canon = getCanonicalType(NamedType);
    ElaboratedType *CheckT = ElaboratedTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!CheckT && "Elaborated canonical type broken");
    (void)CheckT;
  }

  void *Mem =
      Allocate(ElaboratedType::totalSizeToAlloc<TagDecl *>(!!OwnedTagDecl),
               alignof(ElaboratedType));
  T = new (Mem) ElaboratedType(Keyword, NNS, NamedType, Canon, OwnedTagDecl);

  Types.push_back(T);
  ElaboratedTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

QualType
ASTContext::getParenType(QualType InnerType) const {
  llvm::FoldingSetNodeID ID;
  ParenType::Profile(ID, InnerType);

  void *InsertPos = nullptr;
  ParenType *T = ParenTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (T)
    return QualType(T, 0);

  QualType Canon = InnerType;
  if (!Canon.isCanonical()) {
    Canon = getCanonicalType(InnerType);
    ParenType *CheckT = ParenTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!CheckT && "Paren canonical type broken");
    (void)CheckT;
  }

  T = new (*this, alignof(ParenType)) ParenType(InnerType, Canon);
  Types.push_back(T);
  ParenTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

QualType
ASTContext::getMacroQualifiedType(QualType UnderlyingTy,
                                  const IdentifierInfo *MacroII) const {
  QualType Canon = UnderlyingTy;
  if (!Canon.isCanonical())
    Canon = getCanonicalType(UnderlyingTy);

  auto *newType = new (*this, alignof(MacroQualifiedType))
      MacroQualifiedType(UnderlyingTy, Canon, MacroII);
  Types.push_back(newType);
  return QualType(newType, 0);
}

QualType ASTContext::getDependentNameType(ElaboratedTypeKeyword Keyword,
                                          NestedNameSpecifier *NNS,
                                          const IdentifierInfo *Name,
                                          QualType Canon) const {
  if (Canon.isNull()) {
    NestedNameSpecifier *CanonNNS = getCanonicalNestedNameSpecifier(NNS);
    if (CanonNNS != NNS)
      Canon = getDependentNameType(Keyword, CanonNNS, Name);
  }

  llvm::FoldingSetNodeID ID;
  DependentNameType::Profile(ID, Keyword, NNS, Name);

  void *InsertPos = nullptr;
  DependentNameType *T
    = DependentNameTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (T)
    return QualType(T, 0);

  T = new (*this, alignof(DependentNameType))
      DependentNameType(Keyword, NNS, Name, Canon);
  Types.push_back(T);
  DependentNameTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

QualType ASTContext::getDependentTemplateSpecializationType(
    ElaboratedTypeKeyword Keyword, NestedNameSpecifier *NNS,
    const IdentifierInfo *Name, ArrayRef<TemplateArgumentLoc> Args) const {
  // TODO: avoid this copy
  SmallVector<TemplateArgument, 16> ArgCopy;
  for (unsigned I = 0, E = Args.size(); I != E; ++I)
    ArgCopy.push_back(Args[I].getArgument());
  return getDependentTemplateSpecializationType(Keyword, NNS, Name, ArgCopy);
}

QualType
ASTContext::getDependentTemplateSpecializationType(
                                 ElaboratedTypeKeyword Keyword,
                                 NestedNameSpecifier *NNS,
                                 const IdentifierInfo *Name,
                                 ArrayRef<TemplateArgument> Args) const {
  assert((!NNS || NNS->isDependent()) &&
         "nested-name-specifier must be dependent");

  llvm::FoldingSetNodeID ID;
  DependentTemplateSpecializationType::Profile(ID, *this, Keyword, NNS,
                                               Name, Args);

  void *InsertPos = nullptr;
  DependentTemplateSpecializationType *T
    = DependentTemplateSpecializationTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (T)
    return QualType(T, 0);

  NestedNameSpecifier *CanonNNS = getCanonicalNestedNameSpecifier(NNS);

  ElaboratedTypeKeyword CanonKeyword = Keyword;
  if (Keyword == ElaboratedTypeKeyword::None)
    CanonKeyword = ElaboratedTypeKeyword::Typename;

  bool AnyNonCanonArgs = false;
  auto CanonArgs =
      ::getCanonicalTemplateArguments(*this, Args, AnyNonCanonArgs);

  QualType Canon;
  if (AnyNonCanonArgs || CanonNNS != NNS || CanonKeyword != Keyword) {
    Canon = getDependentTemplateSpecializationType(CanonKeyword, CanonNNS,
                                                   Name,
                                                   CanonArgs);

    // Find the insert position again.
    [[maybe_unused]] auto *Nothing =
        DependentTemplateSpecializationTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!Nothing && "canonical type broken");
  }

  void *Mem = Allocate((sizeof(DependentTemplateSpecializationType) +
                        sizeof(TemplateArgument) * Args.size()),
                       alignof(DependentTemplateSpecializationType));
  T = new (Mem) DependentTemplateSpecializationType(Keyword, NNS,
                                                    Name, Args, Canon);
  Types.push_back(T);
  DependentTemplateSpecializationTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

TemplateArgument ASTContext::getInjectedTemplateArg(NamedDecl *Param) {
  TemplateArgument Arg;
  if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {
    QualType ArgType = getTypeDeclType(TTP);
    if (TTP->isParameterPack())
      ArgType = getPackExpansionType(ArgType, std::nullopt);

    Arg = TemplateArgument(ArgType);
  } else if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
    QualType T =
        NTTP->getType().getNonPackExpansionType().getNonLValueExprType(*this);
    // For class NTTPs, ensure we include the 'const' so the type matches that
    // of a real template argument.
    // FIXME: It would be more faithful to model this as something like an
    // lvalue-to-rvalue conversion applied to a const-qualified lvalue.
    if (T->isRecordType())
      T.addConst();
    Expr *E = new (*this) DeclRefExpr(
        *this, NTTP, /*RefersToEnclosingVariableOrCapture*/ false, T,
        Expr::getValueKindForType(NTTP->getType()), NTTP->getLocation());

    if (NTTP->isParameterPack())
      E = new (*this)
          PackExpansionExpr(DependentTy, E, NTTP->getLocation(), std::nullopt);
    Arg = TemplateArgument(E);
  } else {
    auto *TTP = cast<TemplateTemplateParmDecl>(Param);
    TemplateName Name = getQualifiedTemplateName(
        nullptr, /*TemplateKeyword=*/false, TemplateName(TTP));
    if (TTP->isParameterPack())
      Arg = TemplateArgument(Name, std::optional<unsigned>());
    else
      Arg = TemplateArgument(Name);
  }

  if (Param->isTemplateParameterPack())
    Arg = TemplateArgument::CreatePackCopy(*this, Arg);

  return Arg;
}

void
ASTContext::getInjectedTemplateArgs(const TemplateParameterList *Params,
                                    SmallVectorImpl<TemplateArgument> &Args) {
  Args.reserve(Args.size() + Params->size());

  for (NamedDecl *Param : *Params)
    Args.push_back(getInjectedTemplateArg(Param));
}

QualType ASTContext::getPackExpansionType(QualType Pattern,
                                          std::optional<unsigned> NumExpansions,
                                          bool ExpectPackInType) {
  assert((!ExpectPackInType || Pattern->containsUnexpandedParameterPack()) &&
         "Pack expansions must expand one or more parameter packs");

  llvm::FoldingSetNodeID ID;
  PackExpansionType::Profile(ID, Pattern, NumExpansions);

  void *InsertPos = nullptr;
  PackExpansionType *T = PackExpansionTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (T)
    return QualType(T, 0);

  QualType Canon;
  if (!Pattern.isCanonical()) {
    Canon = getPackExpansionType(getCanonicalType(Pattern), NumExpansions,
                                 /*ExpectPackInType=*/false);

    // Find the insert position again, in case we inserted an element into
    // PackExpansionTypes and invalidated our insert position.
    PackExpansionTypes.FindNodeOrInsertPos(ID, InsertPos);
  }

  T = new (*this, alignof(PackExpansionType))
      PackExpansionType(Pattern, Canon, NumExpansions);
  Types.push_back(T);
  PackExpansionTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

/// CmpProtocolNames - Comparison predicate for sorting protocols
/// alphabetically.
static int CmpProtocolNames(ObjCProtocolDecl *const *LHS,
                            ObjCProtocolDecl *const *RHS) {
  return DeclarationName::compare((*LHS)->getDeclName(), (*RHS)->getDeclName());
}

static bool areSortedAndUniqued(ArrayRef<ObjCProtocolDecl *> Protocols) {
  if (Protocols.empty()) return true;

  if (Protocols[0]->getCanonicalDecl() != Protocols[0])
    return false;

  for (unsigned i = 1; i != Protocols.size(); ++i)
    if (CmpProtocolNames(&Protocols[i - 1], &Protocols[i]) >= 0 ||
        Protocols[i]->getCanonicalDecl() != Protocols[i])
      return false;
  return true;
}

static void
SortAndUniqueProtocols(SmallVectorImpl<ObjCProtocolDecl *> &Protocols) {
  // Sort protocols, keyed by name.
  llvm::array_pod_sort(Protocols.begin(), Protocols.end(), CmpProtocolNames);

  // Canonicalize.
  for (ObjCProtocolDecl *&P : Protocols)
    P = P->getCanonicalDecl();

  // Remove duplicates.
  auto ProtocolsEnd = std::unique(Protocols.begin(), Protocols.end());
  Protocols.erase(ProtocolsEnd, Protocols.end());
}

QualType ASTContext::getObjCObjectType(QualType BaseType,
                                       ObjCProtocolDecl * const *Protocols,
                                       unsigned NumProtocols) const {
  return getObjCObjectType(BaseType, {},
                           llvm::ArrayRef(Protocols, NumProtocols),
                           /*isKindOf=*/false);
}

QualType ASTContext::getObjCObjectType(
           QualType baseType,
           ArrayRef<QualType> typeArgs,
           ArrayRef<ObjCProtocolDecl *> protocols,
           bool isKindOf) const {
  // If the base type is an interface and there aren't any protocols or
  // type arguments to add, then the interface type will do just fine.
  if (typeArgs.empty() && protocols.empty() && !isKindOf &&
      isa<ObjCInterfaceType>(baseType))
    return baseType;

  // Look in the folding set for an existing type.
  llvm::FoldingSetNodeID ID;
  ObjCObjectTypeImpl::Profile(ID, baseType, typeArgs, protocols, isKindOf);
  void *InsertPos = nullptr;
  if (ObjCObjectType *QT = ObjCObjectTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(QT, 0);

  // Determine the type arguments to be used for canonicalization,
  // which may be explicitly specified here or written on the base
  // type.
  ArrayRef<QualType> effectiveTypeArgs = typeArgs;
  if (effectiveTypeArgs.empty()) {
    if (const auto *baseObject = baseType->getAs<ObjCObjectType>())
      effectiveTypeArgs = baseObject->getTypeArgs();
  }

  // Build the canonical type, which has the canonical base type and a
  // sorted-and-uniqued list of protocols and the type arguments
  // canonicalized.
  QualType canonical;
  bool typeArgsAreCanonical = llvm::all_of(
      effectiveTypeArgs, [&](QualType type) { return type.isCanonical(); });
  bool protocolsSorted = areSortedAndUniqued(protocols);
  if (!typeArgsAreCanonical || !protocolsSorted || !baseType.isCanonical()) {
    // Determine the canonical type arguments.
    ArrayRef<QualType> canonTypeArgs;
    SmallVector<QualType, 4> canonTypeArgsVec;
    if (!typeArgsAreCanonical) {
      canonTypeArgsVec.reserve(effectiveTypeArgs.size());
      for (auto typeArg : effectiveTypeArgs)
        canonTypeArgsVec.push_back(getCanonicalType(typeArg));
      canonTypeArgs = canonTypeArgsVec;
    } else {
      canonTypeArgs = effectiveTypeArgs;
    }

    ArrayRef<ObjCProtocolDecl *> canonProtocols;
    SmallVector<ObjCProtocolDecl*, 8> canonProtocolsVec;
    if (!protocolsSorted) {
      canonProtocolsVec.append(protocols.begin(), protocols.end());
      SortAndUniqueProtocols(canonProtocolsVec);
      canonProtocols = canonProtocolsVec;
    } else {
      canonProtocols = protocols;
    }

    canonical = getObjCObjectType(getCanonicalType(baseType), canonTypeArgs,
                                  canonProtocols, isKindOf);

    // Regenerate InsertPos.
    ObjCObjectTypes.FindNodeOrInsertPos(ID, InsertPos);
  }

  unsigned size = sizeof(ObjCObjectTypeImpl);
  size += typeArgs.size() * sizeof(QualType);
  size += protocols.size() * sizeof(ObjCProtocolDecl *);
  void *mem = Allocate(size, alignof(ObjCObjectTypeImpl));
  auto *T =
    new (mem) ObjCObjectTypeImpl(canonical, baseType, typeArgs, protocols,
                                 isKindOf);

  Types.push_back(T);
  ObjCObjectTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

/// Apply Objective-C protocol qualifiers to the given type.
/// If this is for the canonical type of a type parameter, we can apply
/// protocol qualifiers on the ObjCObjectPointerType.
QualType
ASTContext::applyObjCProtocolQualifiers(QualType type,
                  ArrayRef<ObjCProtocolDecl *> protocols, bool &hasError,
                  bool allowOnPointerType) const {
  hasError = false;

  if (const auto *objT = dyn_cast<ObjCTypeParamType>(type.getTypePtr())) {
    return getObjCTypeParamType(objT->getDecl(), protocols);
  }

  // Apply protocol qualifiers to ObjCObjectPointerType.
  if (allowOnPointerType) {
    if (const auto *objPtr =
            dyn_cast<ObjCObjectPointerType>(type.getTypePtr())) {
      const ObjCObjectType *objT = objPtr->getObjectType();
      // Merge protocol lists and construct ObjCObjectType.
      SmallVector<ObjCProtocolDecl*, 8> protocolsVec;
      protocolsVec.append(objT->qual_begin(),
                          objT->qual_end());
      protocolsVec.append(protocols.begin(), protocols.end());
      ArrayRef<ObjCProtocolDecl *> protocols = protocolsVec;
      type = getObjCObjectType(
             objT->getBaseType(),
             objT->getTypeArgsAsWritten(),
             protocols,
             objT->isKindOfTypeAsWritten());
      return getObjCObjectPointerType(type);
    }
  }

  // Apply protocol qualifiers to ObjCObjectType.
  if (const auto *objT = dyn_cast<ObjCObjectType>(type.getTypePtr())){
    // FIXME: Check for protocols to which the class type is already
    // known to conform.

    return getObjCObjectType(objT->getBaseType(),
                             objT->getTypeArgsAsWritten(),
                             protocols,
                             objT->isKindOfTypeAsWritten());
  }

  // If the canonical type is ObjCObjectType, ...
  if (type->isObjCObjectType()) {
    // Silently overwrite any existing protocol qualifiers.
    // TODO: determine whether that's the right thing to do.

    // FIXME: Check for protocols to which the class type is already
    // known to conform.
    return getObjCObjectType(type, {}, protocols, false);
  }

  // id<protocol-list>
  if (type->isObjCIdType()) {
    const auto *objPtr = type->castAs<ObjCObjectPointerType>();
    type = getObjCObjectType(ObjCBuiltinIdTy, {}, protocols,
                                 objPtr->isKindOfType());
    return getObjCObjectPointerType(type);
  }

  // Class<protocol-list>
  if (type->isObjCClassType()) {
    const auto *objPtr = type->castAs<ObjCObjectPointerType>();
    type = getObjCObjectType(ObjCBuiltinClassTy, {}, protocols,
                                 objPtr->isKindOfType());
    return getObjCObjectPointerType(type);
  }

  hasError = true;
  return type;
}

QualType
ASTContext::getObjCTypeParamType(const ObjCTypeParamDecl *Decl,
                                 ArrayRef<ObjCProtocolDecl *> protocols) const {
  // Look in the folding set for an existing type.
  llvm::FoldingSetNodeID ID;
  ObjCTypeParamType::Profile(ID, Decl, Decl->getUnderlyingType(), protocols);
  void *InsertPos = nullptr;
  if (ObjCTypeParamType *TypeParam =
      ObjCTypeParamTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(TypeParam, 0);

  // We canonicalize to the underlying type.
  QualType Canonical = getCanonicalType(Decl->getUnderlyingType());
  if (!protocols.empty()) {
    // Apply the protocol qualifers.
    bool hasError;
    Canonical = getCanonicalType(applyObjCProtocolQualifiers(
        Canonical, protocols, hasError, true /*allowOnPointerType*/));
    assert(!hasError && "Error when apply protocol qualifier to bound type");
  }

  unsigned size = sizeof(ObjCTypeParamType);
  size += protocols.size() * sizeof(ObjCProtocolDecl *);
  void *mem = Allocate(size, alignof(ObjCTypeParamType));
  auto *newType = new (mem) ObjCTypeParamType(Decl, Canonical, protocols);

  Types.push_back(newType);
  ObjCTypeParamTypes.InsertNode(newType, InsertPos);
  return QualType(newType, 0);
}

void ASTContext::adjustObjCTypeParamBoundType(const ObjCTypeParamDecl *Orig,
                                              ObjCTypeParamDecl *New) const {
  New->setTypeSourceInfo(getTrivialTypeSourceInfo(Orig->getUnderlyingType()));
  // Update TypeForDecl after updating TypeSourceInfo.
  auto NewTypeParamTy = cast<ObjCTypeParamType>(New->getTypeForDecl());
  SmallVector<ObjCProtocolDecl *, 8> protocols;
  protocols.append(NewTypeParamTy->qual_begin(), NewTypeParamTy->qual_end());
  QualType UpdatedTy = getObjCTypeParamType(New, protocols);
  New->setTypeForDecl(UpdatedTy.getTypePtr());
}

/// ObjCObjectAdoptsQTypeProtocols - Checks that protocols in IC's
/// protocol list adopt all protocols in QT's qualified-id protocol
/// list.
bool ASTContext::ObjCObjectAdoptsQTypeProtocols(QualType QT,
                                                ObjCInterfaceDecl *IC) {
  if (!QT->isObjCQualifiedIdType())
    return false;

  if (const auto *OPT = QT->getAs<ObjCObjectPointerType>()) {
    // If both the right and left sides have qualifiers.
    for (auto *Proto : OPT->quals()) {
      if (!IC->ClassImplementsProtocol(Proto, false))
        return false;
    }
    return true;
  }
  return false;
}

/// QIdProtocolsAdoptObjCObjectProtocols - Checks that protocols in
/// QT's qualified-id protocol list adopt all protocols in IDecl's list
/// of protocols.
bool ASTContext::QIdProtocolsAdoptObjCObjectProtocols(QualType QT,
                                                ObjCInterfaceDecl *IDecl) {
  if (!QT->isObjCQualifiedIdType())
    return false;
  const auto *OPT = QT->getAs<ObjCObjectPointerType>();
  if (!OPT)
    return false;
  if (!IDecl->hasDefinition())
    return false;
  llvm::SmallPtrSet<ObjCProtocolDecl *, 8> InheritedProtocols;
  CollectInheritedProtocols(IDecl, InheritedProtocols);
  if (InheritedProtocols.empty())
    return false;
  // Check that if every protocol in list of id<plist> conforms to a protocol
  // of IDecl's, then bridge casting is ok.
  bool Conforms = false;
  for (auto *Proto : OPT->quals()) {
    Conforms = false;
    for (auto *PI : InheritedProtocols) {
      if (ProtocolCompatibleWithProtocol(Proto, PI)) {
        Conforms = true;
        break;
      }
    }
    if (!Conforms)
      break;
  }
  if (Conforms)
    return true;

  for (auto *PI : InheritedProtocols) {
    // If both the right and left sides have qualifiers.
    bool Adopts = false;
    for (auto *Proto : OPT->quals()) {
      // return 'true' if 'PI' is in the inheritance hierarchy of Proto
      if ((Adopts = ProtocolCompatibleWithProtocol(PI, Proto)))
        break;
    }
    if (!Adopts)
      return false;
  }
  return true;
}

/// getObjCObjectPointerType - Return a ObjCObjectPointerType type for
/// the given object type.
QualType ASTContext::getObjCObjectPointerType(QualType ObjectT) const {
  llvm::FoldingSetNodeID ID;
  ObjCObjectPointerType::Profile(ID, ObjectT);

  void *InsertPos = nullptr;
  if (ObjCObjectPointerType *QT =
              ObjCObjectPointerTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(QT, 0);

  // Find the canonical object type.
  QualType Canonical;
  if (!ObjectT.isCanonical()) {
    Canonical = getObjCObjectPointerType(getCanonicalType(ObjectT));

    // Regenerate InsertPos.
    ObjCObjectPointerTypes.FindNodeOrInsertPos(ID, InsertPos);
  }

  // No match.
  void *Mem =
      Allocate(sizeof(ObjCObjectPointerType), alignof(ObjCObjectPointerType));
  auto *QType =
    new (Mem) ObjCObjectPointerType(Canonical, ObjectT);

  Types.push_back(QType);
  ObjCObjectPointerTypes.InsertNode(QType, InsertPos);
  return QualType(QType, 0);
}

/// getObjCInterfaceType - Return the unique reference to the type for the
/// specified ObjC interface decl. The list of protocols is optional.
QualType ASTContext::getObjCInterfaceType(const ObjCInterfaceDecl *Decl,
                                          ObjCInterfaceDecl *PrevDecl) const {
  if (Decl->TypeForDecl)
    return QualType(Decl->TypeForDecl, 0);

  if (PrevDecl) {
    assert(PrevDecl->TypeForDecl && "previous decl has no TypeForDecl");
    Decl->TypeForDecl = PrevDecl->TypeForDecl;
    return QualType(PrevDecl->TypeForDecl, 0);
  }

  // Prefer the definition, if there is one.
  if (const ObjCInterfaceDecl *Def = Decl->getDefinition())
    Decl = Def;

  void *Mem = Allocate(sizeof(ObjCInterfaceType), alignof(ObjCInterfaceType));
  auto *T = new (Mem) ObjCInterfaceType(Decl);
  Decl->TypeForDecl = T;
  Types.push_back(T);
  return QualType(T, 0);
}

/// getTypeOfExprType - Unlike many "get<Type>" functions, we can't unique
/// TypeOfExprType AST's (since expression's are never shared). For example,
/// multiple declarations that refer to "typeof(x)" all contain different
/// DeclRefExpr's. This doesn't effect the type checker, since it operates
/// on canonical type's (which are always unique).
QualType ASTContext::getTypeOfExprType(Expr *tofExpr, TypeOfKind Kind) const {
  TypeOfExprType *toe;
  if (tofExpr->isTypeDependent()) {
    llvm::FoldingSetNodeID ID;
    DependentTypeOfExprType::Profile(ID, *this, tofExpr,
                                     Kind == TypeOfKind::Unqualified);

    void *InsertPos = nullptr;
    DependentTypeOfExprType *Canon =
        DependentTypeOfExprTypes.FindNodeOrInsertPos(ID, InsertPos);
    if (Canon) {
      // We already have a "canonical" version of an identical, dependent
      // typeof(expr) type. Use that as our canonical type.
      toe = new (*this, alignof(TypeOfExprType)) TypeOfExprType(
          *this, tofExpr, Kind, QualType((TypeOfExprType *)Canon, 0));
    } else {
      // Build a new, canonical typeof(expr) type.
      Canon = new (*this, alignof(DependentTypeOfExprType))
          DependentTypeOfExprType(*this, tofExpr, Kind);
      DependentTypeOfExprTypes.InsertNode(Canon, InsertPos);
      toe = Canon;
    }
  } else {
    QualType Canonical = getCanonicalType(tofExpr->getType());
    toe = new (*this, alignof(TypeOfExprType))
        TypeOfExprType(*this, tofExpr, Kind, Canonical);
  }
  Types.push_back(toe);
  return QualType(toe, 0);
}

/// getTypeOfType -  Unlike many "get<Type>" functions, we don't unique
/// TypeOfType nodes. The only motivation to unique these nodes would be
/// memory savings. Since typeof(t) is fairly uncommon, space shouldn't be
/// an issue. This doesn't affect the type checker, since it operates
/// on canonical types (which are always unique).
QualType ASTContext::getTypeOfType(QualType tofType, TypeOfKind Kind) const {
  QualType Canonical = getCanonicalType(tofType);
  auto *tot = new (*this, alignof(TypeOfType))
      TypeOfType(*this, tofType, Canonical, Kind);
  Types.push_back(tot);
  return QualType(tot, 0);
}

/// getReferenceQualifiedType - Given an expr, will return the type for
/// that expression, as in [dcl.type.simple]p4 but without taking id-expressions
/// and class member access into account.
QualType ASTContext::getReferenceQualifiedType(const Expr *E) const {
  // C++11 [dcl.type.simple]p4:
  //   [...]
  QualType T = E->getType();
  switch (E->getValueKind()) {
  //     - otherwise, if e is an xvalue, decltype(e) is T&&, where T is the
  //       type of e;
  case VK_XValue:
    return getRValueReferenceType(T);
  //     - otherwise, if e is an lvalue, decltype(e) is T&, where T is the
  //       type of e;
  case VK_LValue:
    return getLValueReferenceType(T);
  //  - otherwise, decltype(e) is the type of e.
  case VK_PRValue:
    return T;
  }
  llvm_unreachable("Unknown value kind");
}

/// Unlike many "get<Type>" functions, we don't unique DecltypeType
/// nodes. This would never be helpful, since each such type has its own
/// expression, and would not give a significant memory saving, since there
/// is an Expr tree under each such type.
QualType ASTContext::getDecltypeType(Expr *e, QualType UnderlyingType) const {
  DecltypeType *dt;

  // C++11 [temp.type]p2:
  //   If an expression e involves a template parameter, decltype(e) denotes a
  //   unique dependent type. Two such decltype-specifiers refer to the same
  //   type only if their expressions are equivalent (14.5.6.1).
  if (e->isInstantiationDependent()) {
    llvm::FoldingSetNodeID ID;
    DependentDecltypeType::Profile(ID, *this, e);

    void *InsertPos = nullptr;
    DependentDecltypeType *Canon
      = DependentDecltypeTypes.FindNodeOrInsertPos(ID, InsertPos);
    if (!Canon) {
      // Build a new, canonical decltype(expr) type.
      Canon = new (*this, alignof(DependentDecltypeType))
          DependentDecltypeType(e, DependentTy);
      DependentDecltypeTypes.InsertNode(Canon, InsertPos);
    }
    dt = new (*this, alignof(DecltypeType))
        DecltypeType(e, UnderlyingType, QualType((DecltypeType *)Canon, 0));
  } else {
    dt = new (*this, alignof(DecltypeType))
        DecltypeType(e, UnderlyingType, getCanonicalType(UnderlyingType));
  }
  Types.push_back(dt);
  return QualType(dt, 0);
}

QualType ASTContext::getPackIndexingType(QualType Pattern, Expr *IndexExpr,
                                         bool FullySubstituted,
                                         ArrayRef<QualType> Expansions,
                                         int Index) const {
  QualType Canonical;
  if (FullySubstituted && Index != -1) {
    Canonical = getCanonicalType(Expansions[Index]);
  } else {
    llvm::FoldingSetNodeID ID;
    PackIndexingType::Profile(ID, *this, Pattern, IndexExpr);
    void *InsertPos = nullptr;
    PackIndexingType *Canon =
        DependentPackIndexingTypes.FindNodeOrInsertPos(ID, InsertPos);
    if (!Canon) {
      void *Mem = Allocate(
          PackIndexingType::totalSizeToAlloc<QualType>(Expansions.size()),
          TypeAlignment);
      Canon = new (Mem)
          PackIndexingType(*this, QualType(), Pattern, IndexExpr, Expansions);
      DependentPackIndexingTypes.InsertNode(Canon, InsertPos);
    }
    Canonical = QualType(Canon, 0);
  }

  void *Mem =
      Allocate(PackIndexingType::totalSizeToAlloc<QualType>(Expansions.size()),
               TypeAlignment);
  auto *T = new (Mem)
      PackIndexingType(*this, Canonical, Pattern, IndexExpr, Expansions);
  Types.push_back(T);
  return QualType(T, 0);
}

/// getUnaryTransformationType - We don't unique these, since the memory
/// savings are minimal and these are rare.
QualType ASTContext::getUnaryTransformType(QualType BaseType,
                                           QualType UnderlyingType,
                                           UnaryTransformType::UTTKind Kind)
    const {
  UnaryTransformType *ut = nullptr;

  if (BaseType->isDependentType()) {
    // Look in the folding set for an existing type.
    llvm::FoldingSetNodeID ID;
    DependentUnaryTransformType::Profile(ID, getCanonicalType(BaseType), Kind);

    void *InsertPos = nullptr;
    DependentUnaryTransformType *Canon
      = DependentUnaryTransformTypes.FindNodeOrInsertPos(ID, InsertPos);

    if (!Canon) {
      // Build a new, canonical __underlying_type(type) type.
      Canon = new (*this, alignof(DependentUnaryTransformType))
          DependentUnaryTransformType(*this, getCanonicalType(BaseType), Kind);
      DependentUnaryTransformTypes.InsertNode(Canon, InsertPos);
    }
    ut = new (*this, alignof(UnaryTransformType))
        UnaryTransformType(BaseType, QualType(), Kind, QualType(Canon, 0));
  } else {
    QualType CanonType = getCanonicalType(UnderlyingType);
    ut = new (*this, alignof(UnaryTransformType))
        UnaryTransformType(BaseType, UnderlyingType, Kind, CanonType);
  }
  Types.push_back(ut);
  return QualType(ut, 0);
}

QualType ASTContext::getAutoTypeInternal(
    QualType DeducedType, AutoTypeKeyword Keyword, bool IsDependent,
    bool IsPack, ConceptDecl *TypeConstraintConcept,
    ArrayRef<TemplateArgument> TypeConstraintArgs, bool IsCanon) const {
  if (DeducedType.isNull() && Keyword == AutoTypeKeyword::Auto &&
      !TypeConstraintConcept && !IsDependent)
    return getAutoDeductType();

  // Look in the folding set for an existing type.
  void *InsertPos = nullptr;
  llvm::FoldingSetNodeID ID;
  AutoType::Profile(ID, *this, DeducedType, Keyword, IsDependent,
                    TypeConstraintConcept, TypeConstraintArgs);
  if (AutoType *AT = AutoTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(AT, 0);

  QualType Canon;
  if (!IsCanon) {
    if (!DeducedType.isNull()) {
      Canon = DeducedType.getCanonicalType();
    } else if (TypeConstraintConcept) {
      bool AnyNonCanonArgs = false;
      ConceptDecl *CanonicalConcept = TypeConstraintConcept->getCanonicalDecl();
      auto CanonicalConceptArgs = ::getCanonicalTemplateArguments(
          *this, TypeConstraintArgs, AnyNonCanonArgs);
      if (CanonicalConcept != TypeConstraintConcept || AnyNonCanonArgs) {
        Canon =
            getAutoTypeInternal(QualType(), Keyword, IsDependent, IsPack,
                                CanonicalConcept, CanonicalConceptArgs, true);
        // Find the insert position again.
        [[maybe_unused]] auto *Nothing =
            AutoTypes.FindNodeOrInsertPos(ID, InsertPos);
        assert(!Nothing && "canonical type broken");
      }
    }
  }

  void *Mem = Allocate(sizeof(AutoType) +
                           sizeof(TemplateArgument) * TypeConstraintArgs.size(),
                       alignof(AutoType));
  auto *AT = new (Mem) AutoType(
      DeducedType, Keyword,
      (IsDependent ? TypeDependence::DependentInstantiation
                   : TypeDependence::None) |
          (IsPack ? TypeDependence::UnexpandedPack : TypeDependence::None),
      Canon, TypeConstraintConcept, TypeConstraintArgs);
  Types.push_back(AT);
  AutoTypes.InsertNode(AT, InsertPos);
  return QualType(AT, 0);
}

/// getAutoType - Return the uniqued reference to the 'auto' type which has been
/// deduced to the given type, or to the canonical undeduced 'auto' type, or the
/// canonical deduced-but-dependent 'auto' type.
QualType
ASTContext::getAutoType(QualType DeducedType, AutoTypeKeyword Keyword,
                        bool IsDependent, bool IsPack,
                        ConceptDecl *TypeConstraintConcept,
                        ArrayRef<TemplateArgument> TypeConstraintArgs) const {
  assert((!IsPack || IsDependent) && "only use IsPack for a dependent pack");
  assert((!IsDependent || DeducedType.isNull()) &&
         "A dependent auto should be undeduced");
  return getAutoTypeInternal(DeducedType, Keyword, IsDependent, IsPack,
                             TypeConstraintConcept, TypeConstraintArgs);
}

QualType ASTContext::getUnconstrainedType(QualType T) const {
  QualType CanonT = T.getCanonicalType();

  // Remove a type-constraint from a top-level auto or decltype(auto).
  if (auto *AT = CanonT->getAs<AutoType>()) {
    if (!AT->isConstrained())
      return T;
    return getQualifiedType(getAutoType(QualType(), AT->getKeyword(),
                                        AT->isDependentType(),
                                        AT->containsUnexpandedParameterPack()),
                            T.getQualifiers());
  }

  // FIXME: We only support constrained auto at the top level in the type of a
  // non-type template parameter at the moment. Once we lift that restriction,
  // we'll need to recursively build types containing auto here.
  assert(!CanonT->getContainedAutoType() ||
         !CanonT->getContainedAutoType()->isConstrained());
  return T;
}

/// Return the uniqued reference to the deduced template specialization type
/// which has been deduced to the given type, or to the canonical undeduced
/// such type, or the canonical deduced-but-dependent such type.
QualType ASTContext::getDeducedTemplateSpecializationType(
    TemplateName Template, QualType DeducedType, bool IsDependent) const {
  // Look in the folding set for an existing type.
  void *InsertPos = nullptr;
  llvm::FoldingSetNodeID ID;
  DeducedTemplateSpecializationType::Profile(ID, Template, DeducedType,
                                             IsDependent);
  if (DeducedTemplateSpecializationType *DTST =
          DeducedTemplateSpecializationTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(DTST, 0);

  auto *DTST = new (*this, alignof(DeducedTemplateSpecializationType))
      DeducedTemplateSpecializationType(Template, DeducedType, IsDependent);
  llvm::FoldingSetNodeID TempID;
  DTST->Profile(TempID);
  assert(ID == TempID && "ID does not match");
  Types.push_back(DTST);
  DeducedTemplateSpecializationTypes.InsertNode(DTST, InsertPos);
  return QualType(DTST, 0);
}

/// getAtomicType - Return the uniqued reference to the atomic type for
/// the given value type.
QualType ASTContext::getAtomicType(QualType T) const {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  AtomicType::Profile(ID, T);

  void *InsertPos = nullptr;
  if (AtomicType *AT = AtomicTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(AT, 0);

  // If the atomic value type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getAtomicType(getCanonicalType(T));

    // Get the new insert position for the node we care about.
    AtomicType *NewIP = AtomicTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!"); (void)NewIP;
  }
  auto *New = new (*this, alignof(AtomicType)) AtomicType(T, Canonical);
  Types.push_back(New);
  AtomicTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getAutoDeductType - Get type pattern for deducing against 'auto'.
QualType ASTContext::getAutoDeductType() const {
  if (AutoDeductTy.isNull())
    AutoDeductTy = QualType(new (*this, alignof(AutoType))
                                AutoType(QualType(), AutoTypeKeyword::Auto,
                                         TypeDependence::None, QualType(),
                                         /*concept*/ nullptr, /*args*/ {}),
                            0);
  return AutoDeductTy;
}

/// getAutoRRefDeductType - Get type pattern for deducing against 'auto &&'.
QualType ASTContext::getAutoRRefDeductType() const {
  if (AutoRRefDeductTy.isNull())
    AutoRRefDeductTy = getRValueReferenceType(getAutoDeductType());
  assert(!AutoRRefDeductTy.isNull() && "can't build 'auto &&' pattern");
  return AutoRRefDeductTy;
}

/// getTagDeclType - Return the unique reference to the type for the
/// specified TagDecl (struct/union/class/enum) decl.
QualType ASTContext::getTagDeclType(const TagDecl *Decl) const {
  assert(Decl);
  // FIXME: What is the design on getTagDeclType when it requires casting
  // away const?  mutable?
  return getTypeDeclType(const_cast<TagDecl*>(Decl));
}

/// getSizeType - Return the unique type for "size_t" (C99 7.17), the result
/// of the sizeof operator (C99 6.5.3.4p4). The value is target dependent and
/// needs to agree with the definition in <stddef.h>.
CanQualType ASTContext::getSizeType() const {
  return getFromTargetType(Target->getSizeType());
}

/// Return the unique signed counterpart of the integer type
/// corresponding to size_t.
CanQualType ASTContext::getSignedSizeType() const {
  return getFromTargetType(Target->getSignedSizeType());
}

/// getIntMaxType - Return the unique type for "intmax_t" (C99 7.18.1.5).
CanQualType ASTContext::getIntMaxType() const {
  return getFromTargetType(Target->getIntMaxType());
}

/// getUIntMaxType - Return the unique type for "uintmax_t" (C99 7.18.1.5).
CanQualType ASTContext::getUIntMaxType() const {
  return getFromTargetType(Target->getUIntMaxType());
}

/// getSignedWCharType - Return the type of "signed wchar_t".
/// Used when in C++, as a GCC extension.
QualType ASTContext::getSignedWCharType() const {
  // FIXME: derive from "Target" ?
  return WCharTy;
}

/// getUnsignedWCharType - Return the type of "unsigned wchar_t".
/// Used when in C++, as a GCC extension.
QualType ASTContext::getUnsignedWCharType() const {
  // FIXME: derive from "Target" ?
  return UnsignedIntTy;
}

QualType ASTContext::getIntPtrType() const {
  return getFromTargetType(Target->getIntPtrType());
}

QualType ASTContext::getUIntPtrType() const {
  return getCorrespondingUnsignedType(getIntPtrType());
}

/// getPointerDiffType - Return the unique type for "ptrdiff_t" (C99 7.17)
/// defined in <stddef.h>. Pointer - pointer requires this (C99 6.5.6p9).
QualType ASTContext::getPointerDiffType() const {
  return getFromTargetType(Target->getPtrDiffType(LangAS::Default));
}

/// Return the unique unsigned counterpart of "ptrdiff_t"
/// integer type. The standard (C11 7.21.6.1p7) refers to this type
/// in the definition of %tu format specifier.
QualType ASTContext::getUnsignedPointerDiffType() const {
  return getFromTargetType(Target->getUnsignedPtrDiffType(LangAS::Default));
}

/// Return the unique type for "pid_t" defined in
/// <sys/types.h>. We need this to compute the correct type for vfork().
QualType ASTContext::getProcessIDType() const {
  return getFromTargetType(Target->getProcessIDType());
}

//===----------------------------------------------------------------------===//
//                              Type Operators
//===----------------------------------------------------------------------===//

CanQualType ASTContext::getCanonicalParamType(QualType T) const {
  // Push qualifiers into arrays, and then discard any remaining
  // qualifiers.
  T = getCanonicalType(T);
  T = getVariableArrayDecayedType(T);
  const Type *Ty = T.getTypePtr();
  QualType Result;
  if (getLangOpts().HLSL && isa<ConstantArrayType>(Ty)) {
    Result = getArrayParameterType(QualType(Ty, 0));
  } else if (isa<ArrayType>(Ty)) {
    Result = getArrayDecayedType(QualType(Ty,0));
  } else if (isa<FunctionType>(Ty)) {
    Result = getPointerType(QualType(Ty, 0));
  } else {
    Result = QualType(Ty, 0);
  }

  return CanQualType::CreateUnsafe(Result);
}

QualType ASTContext::getUnqualifiedArrayType(QualType type,
                                             Qualifiers &quals) const {
  SplitQualType splitType = type.getSplitUnqualifiedType();

  // FIXME: getSplitUnqualifiedType() actually walks all the way to
  // the unqualified desugared type and then drops it on the floor.
  // We then have to strip that sugar back off with
  // getUnqualifiedDesugaredType(), which is silly.
  const auto *AT =
      dyn_cast<ArrayType>(splitType.Ty->getUnqualifiedDesugaredType());

  // If we don't have an array, just use the results in splitType.
  if (!AT) {
    quals = splitType.Quals;
    return QualType(splitType.Ty, 0);
  }

  // Otherwise, recurse on the array's element type.
  QualType elementType = AT->getElementType();
  QualType unqualElementType = getUnqualifiedArrayType(elementType, quals);

  // If that didn't change the element type, AT has no qualifiers, so we
  // can just use the results in splitType.
  if (elementType == unqualElementType) {
    assert(quals.empty()); // from the recursive call
    quals = splitType.Quals;
    return QualType(splitType.Ty, 0);
  }

  // Otherwise, add in the qualifiers from the outermost type, then
  // build the type back up.
  quals.addConsistentQualifiers(splitType.Quals);

  if (const auto *CAT = dyn_cast<ConstantArrayType>(AT)) {
    return getConstantArrayType(unqualElementType, CAT->getSize(),
                                CAT->getSizeExpr(), CAT->getSizeModifier(), 0);
  }

  if (const auto *IAT = dyn_cast<IncompleteArrayType>(AT)) {
    return getIncompleteArrayType(unqualElementType, IAT->getSizeModifier(), 0);
  }

  if (const auto *VAT = dyn_cast<VariableArrayType>(AT)) {
    return getVariableArrayType(unqualElementType,
                                VAT->getSizeExpr(),
                                VAT->getSizeModifier(),
                                VAT->getIndexTypeCVRQualifiers(),
                                VAT->getBracketsRange());
  }

  const auto *DSAT = cast<DependentSizedArrayType>(AT);
  return getDependentSizedArrayType(unqualElementType, DSAT->getSizeExpr(),
                                    DSAT->getSizeModifier(), 0,
                                    SourceRange());
}

/// Attempt to unwrap two types that may both be array types with the same bound
/// (or both be array types of unknown bound) for the purpose of comparing the
/// cv-decomposition of two types per C++ [conv.qual].
///
/// \param AllowPiMismatch Allow the Pi1 and Pi2 to differ as described in
///        C++20 [conv.qual], if permitted by the current language mode.
void ASTContext::UnwrapSimilarArrayTypes(QualType &T1, QualType &T2,
                                         bool AllowPiMismatch) {
  while (true) {
    auto *AT1 = getAsArrayType(T1);
    if (!AT1)
      return;

    auto *AT2 = getAsArrayType(T2);
    if (!AT2)
      return;

    // If we don't have two array types with the same constant bound nor two
    // incomplete array types, we've unwrapped everything we can.
    // C++20 also permits one type to be a constant array type and the other
    // to be an incomplete array type.
    // FIXME: Consider also unwrapping array of unknown bound and VLA.
    if (auto *CAT1 = dyn_cast<ConstantArrayType>(AT1)) {
      auto *CAT2 = dyn_cast<ConstantArrayType>(AT2);
      if (!((CAT2 && CAT1->getSize() == CAT2->getSize()) ||
            (AllowPiMismatch && getLangOpts().CPlusPlus20 &&
             isa<IncompleteArrayType>(AT2))))
        return;
    } else if (isa<IncompleteArrayType>(AT1)) {
      if (!(isa<IncompleteArrayType>(AT2) ||
            (AllowPiMismatch && getLangOpts().CPlusPlus20 &&
             isa<ConstantArrayType>(AT2))))
        return;
    } else {
      return;
    }

    T1 = AT1->getElementType();
    T2 = AT2->getElementType();
  }
}

/// Attempt to unwrap two types that may be similar (C++ [conv.qual]).
///
/// If T1 and T2 are both pointer types of the same kind, or both array types
/// with the same bound, unwraps layers from T1 and T2 until a pointer type is
/// unwrapped. Top-level qualifiers on T1 and T2 are ignored.
///
/// This function will typically be called in a loop that successively
/// "unwraps" pointer and pointer-to-member types to compare them at each
/// level.
///
/// \param AllowPiMismatch Allow the Pi1 and Pi2 to differ as described in
///        C++20 [conv.qual], if permitted by the current language mode.
///
/// \return \c true if a pointer type was unwrapped, \c false if we reached a
/// pair of types that can't be unwrapped further.
bool ASTContext::UnwrapSimilarTypes(QualType &T1, QualType &T2,
                                    bool AllowPiMismatch) {
  UnwrapSimilarArrayTypes(T1, T2, AllowPiMismatch);

  const auto *T1PtrType = T1->getAs<PointerType>();
  const auto *T2PtrType = T2->getAs<PointerType>();
  if (T1PtrType && T2PtrType) {
    T1 = T1PtrType->getPointeeType();
    T2 = T2PtrType->getPointeeType();
    return true;
  }

  const auto *T1MPType = T1->getAs<MemberPointerType>();
  const auto *T2MPType = T2->getAs<MemberPointerType>();
  if (T1MPType && T2MPType &&
      hasSameUnqualifiedType(QualType(T1MPType->getClass(), 0),
                             QualType(T2MPType->getClass(), 0))) {
    T1 = T1MPType->getPointeeType();
    T2 = T2MPType->getPointeeType();
    return true;
  }

  if (getLangOpts().ObjC) {
    const auto *T1OPType = T1->getAs<ObjCObjectPointerType>();
    const auto *T2OPType = T2->getAs<ObjCObjectPointerType>();
    if (T1OPType && T2OPType) {
      T1 = T1OPType->getPointeeType();
      T2 = T2OPType->getPointeeType();
      return true;
    }
  }

  // FIXME: Block pointers, too?

  return false;
}

bool ASTContext::hasSimilarType(QualType T1, QualType T2) {
  while (true) {
    Qualifiers Quals;
    T1 = getUnqualifiedArrayType(T1, Quals);
    T2 = getUnqualifiedArrayType(T2, Quals);
    if (hasSameType(T1, T2))
      return true;
    if (!UnwrapSimilarTypes(T1, T2))
      return false;
  }
}

bool ASTContext::hasCvrSimilarType(QualType T1, QualType T2) {
  while (true) {
    Qualifiers Quals1, Quals2;
    T1 = getUnqualifiedArrayType(T1, Quals1);
    T2 = getUnqualifiedArrayType(T2, Quals2);

    Quals1.removeCVRQualifiers();
    Quals2.removeCVRQualifiers();
    if (Quals1 != Quals2)
      return false;

    if (hasSameType(T1, T2))
      return true;

    if (!UnwrapSimilarTypes(T1, T2, /*AllowPiMismatch*/ false))
      return false;
  }
}

DeclarationNameInfo
ASTContext::getNameForTemplate(TemplateName Name,
                               SourceLocation NameLoc) const {
  switch (Name.getKind()) {
  case TemplateName::QualifiedTemplate:
  case TemplateName::Template:
    // DNInfo work in progress: CHECKME: what about DNLoc?
    return DeclarationNameInfo(Name.getAsTemplateDecl()->getDeclName(),
                               NameLoc);

  case TemplateName::OverloadedTemplate: {
    OverloadedTemplateStorage *Storage = Name.getAsOverloadedTemplate();
    // DNInfo work in progress: CHECKME: what about DNLoc?
    return DeclarationNameInfo((*Storage->begin())->getDeclName(), NameLoc);
  }

  case TemplateName::AssumedTemplate: {
    AssumedTemplateStorage *Storage = Name.getAsAssumedTemplateName();
    return DeclarationNameInfo(Storage->getDeclName(), NameLoc);
  }

  case TemplateName::DependentTemplate: {
    DependentTemplateName *DTN = Name.getAsDependentTemplateName();
    DeclarationName DName;
    if (DTN->isIdentifier()) {
      DName = DeclarationNames.getIdentifier(DTN->getIdentifier());
      return DeclarationNameInfo(DName, NameLoc);
    } else {
      DName = DeclarationNames.getCXXOperatorName(DTN->getOperator());
      // DNInfo work in progress: FIXME: source locations?
      DeclarationNameLoc DNLoc =
          DeclarationNameLoc::makeCXXOperatorNameLoc(SourceRange());
      return DeclarationNameInfo(DName, NameLoc, DNLoc);
    }
  }

  case TemplateName::SubstTemplateTemplateParm: {
    SubstTemplateTemplateParmStorage *subst
      = Name.getAsSubstTemplateTemplateParm();
    return DeclarationNameInfo(subst->getParameter()->getDeclName(),
                               NameLoc);
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    SubstTemplateTemplateParmPackStorage *subst
      = Name.getAsSubstTemplateTemplateParmPack();
    return DeclarationNameInfo(subst->getParameterPack()->getDeclName(),
                               NameLoc);
  }
  case TemplateName::UsingTemplate:
    return DeclarationNameInfo(Name.getAsUsingShadowDecl()->getDeclName(),
                               NameLoc);
  }

  llvm_unreachable("bad template name kind!");
}

TemplateName
ASTContext::getCanonicalTemplateName(const TemplateName &Name) const {
  switch (Name.getKind()) {
  case TemplateName::UsingTemplate:
  case TemplateName::QualifiedTemplate:
  case TemplateName::Template: {
    TemplateDecl *Template = Name.getAsTemplateDecl();
    if (auto *TTP  = dyn_cast<TemplateTemplateParmDecl>(Template))
      Template = getCanonicalTemplateTemplateParmDecl(TTP);

    // The canonical template name is the canonical template declaration.
    return TemplateName(cast<TemplateDecl>(Template->getCanonicalDecl()));
  }

  case TemplateName::OverloadedTemplate:
  case TemplateName::AssumedTemplate:
    llvm_unreachable("cannot canonicalize unresolved template");

  case TemplateName::DependentTemplate: {
    DependentTemplateName *DTN = Name.getAsDependentTemplateName();
    assert(DTN && "Non-dependent template names must refer to template decls.");
    return DTN->CanonicalTemplateName;
  }

  case TemplateName::SubstTemplateTemplateParm: {
    SubstTemplateTemplateParmStorage *subst
      = Name.getAsSubstTemplateTemplateParm();
    return getCanonicalTemplateName(subst->getReplacement());
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    SubstTemplateTemplateParmPackStorage *subst =
        Name.getAsSubstTemplateTemplateParmPack();
    TemplateArgument canonArgPack =
        getCanonicalTemplateArgument(subst->getArgumentPack());
    return getSubstTemplateTemplateParmPack(
        canonArgPack, subst->getAssociatedDecl()->getCanonicalDecl(),
        subst->getFinal(), subst->getIndex());
  }
  }

  llvm_unreachable("bad template name!");
}

bool ASTContext::hasSameTemplateName(const TemplateName &X,
                                     const TemplateName &Y) const {
  return getCanonicalTemplateName(X).getAsVoidPointer() ==
         getCanonicalTemplateName(Y).getAsVoidPointer();
}

bool ASTContext::isSameConstraintExpr(const Expr *XCE, const Expr *YCE) const {
  if (!XCE != !YCE)
    return false;

  if (!XCE)
    return true;

  llvm::FoldingSetNodeID XCEID, YCEID;
  XCE->Profile(XCEID, *this, /*Canonical=*/true, /*ProfileLambdaExpr=*/true);
  YCE->Profile(YCEID, *this, /*Canonical=*/true, /*ProfileLambdaExpr=*/true);
  return XCEID == YCEID;
}

bool ASTContext::isSameTypeConstraint(const TypeConstraint *XTC,
                                      const TypeConstraint *YTC) const {
  if (!XTC != !YTC)
    return false;

  if (!XTC)
    return true;

  auto *NCX = XTC->getNamedConcept();
  auto *NCY = YTC->getNamedConcept();
  if (!NCX || !NCY || !isSameEntity(NCX, NCY))
    return false;
  if (XTC->getConceptReference()->hasExplicitTemplateArgs() !=
      YTC->getConceptReference()->hasExplicitTemplateArgs())
    return false;
  if (XTC->getConceptReference()->hasExplicitTemplateArgs())
    if (XTC->getConceptReference()
            ->getTemplateArgsAsWritten()
            ->NumTemplateArgs !=
        YTC->getConceptReference()->getTemplateArgsAsWritten()->NumTemplateArgs)
      return false;

  // Compare slowly by profiling.
  //
  // We couldn't compare the profiling result for the template
  // args here. Consider the following example in different modules:
  //
  // template <__integer_like _Tp, C<_Tp> Sentinel>
  // constexpr _Tp operator()(_Tp &&__t, Sentinel &&last) const {
  //   return __t;
  // }
  //
  // When we compare the profiling result for `C<_Tp>` in different
  // modules, it will compare the type of `_Tp` in different modules.
  // However, the type of `_Tp` in different modules refer to different
  // types here naturally. So we couldn't compare the profiling result
  // for the template args directly.
  return isSameConstraintExpr(XTC->getImmediatelyDeclaredConstraint(),
                              YTC->getImmediatelyDeclaredConstraint());
}

bool ASTContext::isSameTemplateParameter(const NamedDecl *X,
                                         const NamedDecl *Y) const {
  if (X->getKind() != Y->getKind())
    return false;

  if (auto *TX = dyn_cast<TemplateTypeParmDecl>(X)) {
    auto *TY = cast<TemplateTypeParmDecl>(Y);
    if (TX->isParameterPack() != TY->isParameterPack())
      return false;
    if (TX->hasTypeConstraint() != TY->hasTypeConstraint())
      return false;
    return isSameTypeConstraint(TX->getTypeConstraint(),
                                TY->getTypeConstraint());
  }

  if (auto *TX = dyn_cast<NonTypeTemplateParmDecl>(X)) {
    auto *TY = cast<NonTypeTemplateParmDecl>(Y);
    return TX->isParameterPack() == TY->isParameterPack() &&
           TX->getASTContext().hasSameType(TX->getType(), TY->getType()) &&
           isSameConstraintExpr(TX->getPlaceholderTypeConstraint(),
                                TY->getPlaceholderTypeConstraint());
  }

  auto *TX = cast<TemplateTemplateParmDecl>(X);
  auto *TY = cast<TemplateTemplateParmDecl>(Y);
  return TX->isParameterPack() == TY->isParameterPack() &&
         isSameTemplateParameterList(TX->getTemplateParameters(),
                                     TY->getTemplateParameters());
}

bool ASTContext::isSameTemplateParameterList(
    const TemplateParameterList *X, const TemplateParameterList *Y) const {
  if (X->size() != Y->size())
    return false;

  for (unsigned I = 0, N = X->size(); I != N; ++I)
    if (!isSameTemplateParameter(X->getParam(I), Y->getParam(I)))
      return false;

  return isSameConstraintExpr(X->getRequiresClause(), Y->getRequiresClause());
}

bool ASTContext::isSameDefaultTemplateArgument(const NamedDecl *X,
                                               const NamedDecl *Y) const {
  // If the type parameter isn't the same already, we don't need to check the
  // default argument further.
  if (!isSameTemplateParameter(X, Y))
    return false;

  if (auto *TTPX = dyn_cast<TemplateTypeParmDecl>(X)) {
    auto *TTPY = cast<TemplateTypeParmDecl>(Y);
    if (!TTPX->hasDefaultArgument() || !TTPY->hasDefaultArgument())
      return false;

    return hasSameType(TTPX->getDefaultArgument().getArgument().getAsType(),
                       TTPY->getDefaultArgument().getArgument().getAsType());
  }

  if (auto *NTTPX = dyn_cast<NonTypeTemplateParmDecl>(X)) {
    auto *NTTPY = cast<NonTypeTemplateParmDecl>(Y);
    if (!NTTPX->hasDefaultArgument() || !NTTPY->hasDefaultArgument())
      return false;

    Expr *DefaultArgumentX =
        NTTPX->getDefaultArgument().getArgument().getAsExpr()->IgnoreImpCasts();
    Expr *DefaultArgumentY =
        NTTPY->getDefaultArgument().getArgument().getAsExpr()->IgnoreImpCasts();
    llvm::FoldingSetNodeID XID, YID;
    DefaultArgumentX->Profile(XID, *this, /*Canonical=*/true);
    DefaultArgumentY->Profile(YID, *this, /*Canonical=*/true);
    return XID == YID;
  }

  auto *TTPX = cast<TemplateTemplateParmDecl>(X);
  auto *TTPY = cast<TemplateTemplateParmDecl>(Y);

  if (!TTPX->hasDefaultArgument() || !TTPY->hasDefaultArgument())
    return false;

  const TemplateArgument &TAX = TTPX->getDefaultArgument().getArgument();
  const TemplateArgument &TAY = TTPY->getDefaultArgument().getArgument();
  return hasSameTemplateName(TAX.getAsTemplate(), TAY.getAsTemplate());
}

static NamespaceDecl *getNamespace(const NestedNameSpecifier *X) {
  if (auto *NS = X->getAsNamespace())
    return NS;
  if (auto *NAS = X->getAsNamespaceAlias())
    return NAS->getNamespace();
  return nullptr;
}

static bool isSameQualifier(const NestedNameSpecifier *X,
                            const NestedNameSpecifier *Y) {
  if (auto *NSX = getNamespace(X)) {
    auto *NSY = getNamespace(Y);
    if (!NSY || NSX->getCanonicalDecl() != NSY->getCanonicalDecl())
      return false;
  } else if (X->getKind() != Y->getKind())
    return false;

  // FIXME: For namespaces and types, we're permitted to check that the entity
  // is named via the same tokens. We should probably do so.
  switch (X->getKind()) {
  case NestedNameSpecifier::Identifier:
    if (X->getAsIdentifier() != Y->getAsIdentifier())
      return false;
    break;
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
    // We've already checked that we named the same namespace.
    break;
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    if (X->getAsType()->getCanonicalTypeInternal() !=
        Y->getAsType()->getCanonicalTypeInternal())
      return false;
    break;
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    return true;
  }

  // Recurse into earlier portion of NNS, if any.
  auto *PX = X->getPrefix();
  auto *PY = Y->getPrefix();
  if (PX && PY)
    return isSameQualifier(PX, PY);
  return !PX && !PY;
}

/// Determine whether the attributes we can overload on are identical for A and
/// B. Will ignore any overloadable attrs represented in the type of A and B.
static bool hasSameOverloadableAttrs(const FunctionDecl *A,
                                     const FunctionDecl *B) {
  // Note that pass_object_size attributes are represented in the function's
  // ExtParameterInfo, so we don't need to check them here.

  llvm::FoldingSetNodeID Cand1ID, Cand2ID;
  auto AEnableIfAttrs = A->specific_attrs<EnableIfAttr>();
  auto BEnableIfAttrs = B->specific_attrs<EnableIfAttr>();

  for (auto Pair : zip_longest(AEnableIfAttrs, BEnableIfAttrs)) {
    std::optional<EnableIfAttr *> Cand1A = std::get<0>(Pair);
    std::optional<EnableIfAttr *> Cand2A = std::get<1>(Pair);

    // Return false if the number of enable_if attributes is different.
    if (!Cand1A || !Cand2A)
      return false;

    Cand1ID.clear();
    Cand2ID.clear();

    (*Cand1A)->getCond()->Profile(Cand1ID, A->getASTContext(), true);
    (*Cand2A)->getCond()->Profile(Cand2ID, B->getASTContext(), true);

    // Return false if any of the enable_if expressions of A and B are
    // different.
    if (Cand1ID != Cand2ID)
      return false;
  }
  return true;
}

bool ASTContext::isSameEntity(const NamedDecl *X, const NamedDecl *Y) const {
  // Caution: this function is called by the AST reader during deserialization,
  // so it cannot rely on AST invariants being met. Non-trivial accessors
  // should be avoided, along with any traversal of redeclaration chains.

  if (X == Y)
    return true;

  if (X->getDeclName() != Y->getDeclName())
    return false;

  // Must be in the same context.
  //
  // Note that we can't use DeclContext::Equals here, because the DeclContexts
  // could be two different declarations of the same function. (We will fix the
  // semantic DC to refer to the primary definition after merging.)
  if (!declaresSameEntity(cast<Decl>(X->getDeclContext()->getRedeclContext()),
                          cast<Decl>(Y->getDeclContext()->getRedeclContext())))
    return false;

  // Two typedefs refer to the same entity if they have the same underlying
  // type.
  if (const auto *TypedefX = dyn_cast<TypedefNameDecl>(X))
    if (const auto *TypedefY = dyn_cast<TypedefNameDecl>(Y))
      return hasSameType(TypedefX->getUnderlyingType(),
                         TypedefY->getUnderlyingType());

  // Must have the same kind.
  if (X->getKind() != Y->getKind())
    return false;

  // Objective-C classes and protocols with the same name always match.
  if (isa<ObjCInterfaceDecl>(X) || isa<ObjCProtocolDecl>(X))
    return true;

  if (isa<ClassTemplateSpecializationDecl>(X)) {
    // No need to handle these here: we merge them when adding them to the
    // template.
    return false;
  }

  // Compatible tags match.
  if (const auto *TagX = dyn_cast<TagDecl>(X)) {
    const auto *TagY = cast<TagDecl>(Y);
    return (TagX->getTagKind() == TagY->getTagKind()) ||
           ((TagX->getTagKind() == TagTypeKind::Struct ||
             TagX->getTagKind() == TagTypeKind::Class ||
             TagX->getTagKind() == TagTypeKind::Interface) &&
            (TagY->getTagKind() == TagTypeKind::Struct ||
             TagY->getTagKind() == TagTypeKind::Class ||
             TagY->getTagKind() == TagTypeKind::Interface));
  }

  // Functions with the same type and linkage match.
  // FIXME: This needs to cope with merging of prototyped/non-prototyped
  // functions, etc.
  if (const auto *FuncX = dyn_cast<FunctionDecl>(X)) {
    const auto *FuncY = cast<FunctionDecl>(Y);
    if (const auto *CtorX = dyn_cast<CXXConstructorDecl>(X)) {
      const auto *CtorY = cast<CXXConstructorDecl>(Y);
      if (CtorX->getInheritedConstructor() &&
          !isSameEntity(CtorX->getInheritedConstructor().getConstructor(),
                        CtorY->getInheritedConstructor().getConstructor()))
        return false;
    }

    if (FuncX->isMultiVersion() != FuncY->isMultiVersion())
      return false;

    // Multiversioned functions with different feature strings are represented
    // as separate declarations.
    if (FuncX->isMultiVersion()) {
      const auto *TAX = FuncX->getAttr<TargetAttr>();
      const auto *TAY = FuncY->getAttr<TargetAttr>();
      assert(TAX && TAY && "Multiversion Function without target attribute");

      if (TAX->getFeaturesStr() != TAY->getFeaturesStr())
        return false;
    }

    // Per C++20 [temp.over.link]/4, friends in different classes are sometimes
    // not the same entity if they are constrained.
    if ((FuncX->isMemberLikeConstrainedFriend() ||
         FuncY->isMemberLikeConstrainedFriend()) &&
        !FuncX->getLexicalDeclContext()->Equals(
            FuncY->getLexicalDeclContext())) {
      return false;
    }

    if (!isSameConstraintExpr(FuncX->getTrailingRequiresClause(),
                              FuncY->getTrailingRequiresClause()))
      return false;

    auto GetTypeAsWritten = [](const FunctionDecl *FD) {
      // Map to the first declaration that we've already merged into this one.
      // The TSI of redeclarations might not match (due to calling conventions
      // being inherited onto the type but not the TSI), but the TSI type of
      // the first declaration of the function should match across modules.
      FD = FD->getCanonicalDecl();
      return FD->getTypeSourceInfo() ? FD->getTypeSourceInfo()->getType()
                                     : FD->getType();
    };
    QualType XT = GetTypeAsWritten(FuncX), YT = GetTypeAsWritten(FuncY);
    if (!hasSameType(XT, YT)) {
      // We can get functions with different types on the redecl chain in C++17
      // if they have differing exception specifications and at least one of
      // the excpetion specs is unresolved.
      auto *XFPT = XT->getAs<FunctionProtoType>();
      auto *YFPT = YT->getAs<FunctionProtoType>();
      if (getLangOpts().CPlusPlus17 && XFPT && YFPT &&
          (isUnresolvedExceptionSpec(XFPT->getExceptionSpecType()) ||
           isUnresolvedExceptionSpec(YFPT->getExceptionSpecType())) &&
          hasSameFunctionTypeIgnoringExceptionSpec(XT, YT))
        return true;
      return false;
    }

    return FuncX->getLinkageInternal() == FuncY->getLinkageInternal() &&
           hasSameOverloadableAttrs(FuncX, FuncY);
  }

  // Variables with the same type and linkage match.
  if (const auto *VarX = dyn_cast<VarDecl>(X)) {
    const auto *VarY = cast<VarDecl>(Y);
    if (VarX->getLinkageInternal() == VarY->getLinkageInternal()) {
      // During deserialization, we might compare variables before we load
      // their types. Assume the types will end up being the same.
      if (VarX->getType().isNull() || VarY->getType().isNull())
        return true;

      if (hasSameType(VarX->getType(), VarY->getType()))
        return true;

      // We can get decls with different types on the redecl chain. Eg.
      // template <typename T> struct S { static T Var[]; }; // #1
      // template <typename T> T S<T>::Var[sizeof(T)]; // #2
      // Only? happens when completing an incomplete array type. In this case
      // when comparing #1 and #2 we should go through their element type.
      const ArrayType *VarXTy = getAsArrayType(VarX->getType());
      const ArrayType *VarYTy = getAsArrayType(VarY->getType());
      if (!VarXTy || !VarYTy)
        return false;
      if (VarXTy->isIncompleteArrayType() || VarYTy->isIncompleteArrayType())
        return hasSameType(VarXTy->getElementType(), VarYTy->getElementType());
    }
    return false;
  }

  // Namespaces with the same name and inlinedness match.
  if (const auto *NamespaceX = dyn_cast<NamespaceDecl>(X)) {
    const auto *NamespaceY = cast<NamespaceDecl>(Y);
    return NamespaceX->isInline() == NamespaceY->isInline();
  }

  // Identical template names and kinds match if their template parameter lists
  // and patterns match.
  if (const auto *TemplateX = dyn_cast<TemplateDecl>(X)) {
    const auto *TemplateY = cast<TemplateDecl>(Y);

    // ConceptDecl wouldn't be the same if their constraint expression differs.
    if (const auto *ConceptX = dyn_cast<ConceptDecl>(X)) {
      const auto *ConceptY = cast<ConceptDecl>(Y);
      if (!isSameConstraintExpr(ConceptX->getConstraintExpr(),
                                ConceptY->getConstraintExpr()))
        return false;
    }

    return isSameEntity(TemplateX->getTemplatedDecl(),
                        TemplateY->getTemplatedDecl()) &&
           isSameTemplateParameterList(TemplateX->getTemplateParameters(),
                                       TemplateY->getTemplateParameters());
  }

  // Fields with the same name and the same type match.
  if (const auto *FDX = dyn_cast<FieldDecl>(X)) {
    const auto *FDY = cast<FieldDecl>(Y);
    // FIXME: Also check the bitwidth is odr-equivalent, if any.
    return hasSameType(FDX->getType(), FDY->getType());
  }

  // Indirect fields with the same target field match.
  if (const auto *IFDX = dyn_cast<IndirectFieldDecl>(X)) {
    const auto *IFDY = cast<IndirectFieldDecl>(Y);
    return IFDX->getAnonField()->getCanonicalDecl() ==
           IFDY->getAnonField()->getCanonicalDecl();
  }

  // Enumerators with the same name match.
  if (isa<EnumConstantDecl>(X))
    // FIXME: Also check the value is odr-equivalent.
    return true;

  // Using shadow declarations with the same target match.
  if (const auto *USX = dyn_cast<UsingShadowDecl>(X)) {
    const auto *USY = cast<UsingShadowDecl>(Y);
    return declaresSameEntity(USX->getTargetDecl(), USY->getTargetDecl());
  }

  // Using declarations with the same qualifier match. (We already know that
  // the name matches.)
  if (const auto *UX = dyn_cast<UsingDecl>(X)) {
    const auto *UY = cast<UsingDecl>(Y);
    return isSameQualifier(UX->getQualifier(), UY->getQualifier()) &&
           UX->hasTypename() == UY->hasTypename() &&
           UX->isAccessDeclaration() == UY->isAccessDeclaration();
  }
  if (const auto *UX = dyn_cast<UnresolvedUsingValueDecl>(X)) {
    const auto *UY = cast<UnresolvedUsingValueDecl>(Y);
    return isSameQualifier(UX->getQualifier(), UY->getQualifier()) &&
           UX->isAccessDeclaration() == UY->isAccessDeclaration();
  }
  if (const auto *UX = dyn_cast<UnresolvedUsingTypenameDecl>(X)) {
    return isSameQualifier(
        UX->getQualifier(),
        cast<UnresolvedUsingTypenameDecl>(Y)->getQualifier());
  }

  // Using-pack declarations are only created by instantiation, and match if
  // they're instantiated from matching UnresolvedUsing...Decls.
  if (const auto *UX = dyn_cast<UsingPackDecl>(X)) {
    return declaresSameEntity(
        UX->getInstantiatedFromUsingDecl(),
        cast<UsingPackDecl>(Y)->getInstantiatedFromUsingDecl());
  }

  // Namespace alias definitions with the same target match.
  if (const auto *NAX = dyn_cast<NamespaceAliasDecl>(X)) {
    const auto *NAY = cast<NamespaceAliasDecl>(Y);
    return NAX->getNamespace()->Equals(NAY->getNamespace());
  }

  return false;
}

TemplateArgument
ASTContext::getCanonicalTemplateArgument(const TemplateArgument &Arg) const {
  switch (Arg.getKind()) {
    case TemplateArgument::Null:
      return Arg;

    case TemplateArgument::Expression:
      return Arg;

    case TemplateArgument::Declaration: {
      auto *D = cast<ValueDecl>(Arg.getAsDecl()->getCanonicalDecl());
      return TemplateArgument(D, getCanonicalType(Arg.getParamTypeForDecl()),
                              Arg.getIsDefaulted());
    }

    case TemplateArgument::NullPtr:
      return TemplateArgument(getCanonicalType(Arg.getNullPtrType()),
                              /*isNullPtr*/ true, Arg.getIsDefaulted());

    case TemplateArgument::Template:
      return TemplateArgument(getCanonicalTemplateName(Arg.getAsTemplate()),
                              Arg.getIsDefaulted());

    case TemplateArgument::TemplateExpansion:
      return TemplateArgument(
          getCanonicalTemplateName(Arg.getAsTemplateOrTemplatePattern()),
          Arg.getNumTemplateExpansions(), Arg.getIsDefaulted());

    case TemplateArgument::Integral:
      return TemplateArgument(Arg, getCanonicalType(Arg.getIntegralType()));

    case TemplateArgument::StructuralValue:
      return TemplateArgument(*this,
                              getCanonicalType(Arg.getStructuralValueType()),
                              Arg.getAsStructuralValue());

    case TemplateArgument::Type:
      return TemplateArgument(getCanonicalType(Arg.getAsType()),
                              /*isNullPtr*/ false, Arg.getIsDefaulted());

    case TemplateArgument::Pack: {
      bool AnyNonCanonArgs = false;
      auto CanonArgs = ::getCanonicalTemplateArguments(
          *this, Arg.pack_elements(), AnyNonCanonArgs);
      if (!AnyNonCanonArgs)
        return Arg;
      return TemplateArgument::CreatePackCopy(const_cast<ASTContext &>(*this),
                                              CanonArgs);
    }
  }

  // Silence GCC warning
  llvm_unreachable("Unhandled template argument kind");
}

NestedNameSpecifier *
ASTContext::getCanonicalNestedNameSpecifier(NestedNameSpecifier *NNS) const {
  if (!NNS)
    return nullptr;

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Identifier:
    // Canonicalize the prefix but keep the identifier the same.
    return NestedNameSpecifier::Create(*this,
                         getCanonicalNestedNameSpecifier(NNS->getPrefix()),
                                       NNS->getAsIdentifier());

  case NestedNameSpecifier::Namespace:
    // A namespace is canonical; build a nested-name-specifier with
    // this namespace and no prefix.
    return NestedNameSpecifier::Create(*this, nullptr,
                                       NNS->getAsNamespace()->getFirstDecl());

  case NestedNameSpecifier::NamespaceAlias:
    // A namespace is canonical; build a nested-name-specifier with
    // this namespace and no prefix.
    return NestedNameSpecifier::Create(
        *this, nullptr,
        NNS->getAsNamespaceAlias()->getNamespace()->getFirstDecl());

  // The difference between TypeSpec and TypeSpecWithTemplate is that the
  // latter will have the 'template' keyword when printed.
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate: {
    const Type *T = getCanonicalType(NNS->getAsType());

    // If we have some kind of dependent-named type (e.g., "typename T::type"),
    // break it apart into its prefix and identifier, then reconsititute those
    // as the canonical nested-name-specifier. This is required to canonicalize
    // a dependent nested-name-specifier involving typedefs of dependent-name
    // types, e.g.,
    //   typedef typename T::type T1;
    //   typedef typename T1::type T2;
    if (const auto *DNT = T->getAs<DependentNameType>())
      return NestedNameSpecifier::Create(*this, DNT->getQualifier(),
                                         DNT->getIdentifier());
    if (const auto *DTST = T->getAs<DependentTemplateSpecializationType>())
      return NestedNameSpecifier::Create(*this, DTST->getQualifier(), true, T);

    // TODO: Set 'Template' parameter to true for other template types.
    return NestedNameSpecifier::Create(*this, nullptr, false, T);
  }

  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    // The global specifier and __super specifer are canonical and unique.
    return NNS;
  }

  llvm_unreachable("Invalid NestedNameSpecifier::Kind!");
}

const ArrayType *ASTContext::getAsArrayType(QualType T) const {
  // Handle the non-qualified case efficiently.
  if (!T.hasLocalQualifiers()) {
    // Handle the common positive case fast.
    if (const auto *AT = dyn_cast<ArrayType>(T))
      return AT;
  }

  // Handle the common negative case fast.
  if (!isa<ArrayType>(T.getCanonicalType()))
    return nullptr;

  // Apply any qualifiers from the array type to the element type.  This
  // implements C99 6.7.3p8: "If the specification of an array type includes
  // any type qualifiers, the element type is so qualified, not the array type."

  // If we get here, we either have type qualifiers on the type, or we have
  // sugar such as a typedef in the way.  If we have type qualifiers on the type
  // we must propagate them down into the element type.

  SplitQualType split = T.getSplitDesugaredType();
  Qualifiers qs = split.Quals;

  // If we have a simple case, just return now.
  const auto *ATy = dyn_cast<ArrayType>(split.Ty);
  if (!ATy || qs.empty())
    return ATy;

  // Otherwise, we have an array and we have qualifiers on it.  Push the
  // qualifiers into the array element type and return a new array type.
  QualType NewEltTy = getQualifiedType(ATy->getElementType(), qs);

  if (const auto *CAT = dyn_cast<ConstantArrayType>(ATy))
    return cast<ArrayType>(getConstantArrayType(NewEltTy, CAT->getSize(),
                                                CAT->getSizeExpr(),
                                                CAT->getSizeModifier(),
                                           CAT->getIndexTypeCVRQualifiers()));
  if (const auto *IAT = dyn_cast<IncompleteArrayType>(ATy))
    return cast<ArrayType>(getIncompleteArrayType(NewEltTy,
                                                  IAT->getSizeModifier(),
                                           IAT->getIndexTypeCVRQualifiers()));

  if (const auto *DSAT = dyn_cast<DependentSizedArrayType>(ATy))
    return cast<ArrayType>(
                     getDependentSizedArrayType(NewEltTy,
                                                DSAT->getSizeExpr(),
                                                DSAT->getSizeModifier(),
                                              DSAT->getIndexTypeCVRQualifiers(),
                                                DSAT->getBracketsRange()));

  const auto *VAT = cast<VariableArrayType>(ATy);
  return cast<ArrayType>(getVariableArrayType(NewEltTy,
                                              VAT->getSizeExpr(),
                                              VAT->getSizeModifier(),
                                              VAT->getIndexTypeCVRQualifiers(),
                                              VAT->getBracketsRange()));
}

QualType ASTContext::getAdjustedParameterType(QualType T) const {
  if (getLangOpts().HLSL && T->isConstantArrayType())
    return getArrayParameterType(T);
  if (T->isArrayType() || T->isFunctionType())
    return getDecayedType(T);
  return T;
}

QualType ASTContext::getSignatureParameterType(QualType T) const {
  T = getVariableArrayDecayedType(T);
  T = getAdjustedParameterType(T);
  return T.getUnqualifiedType();
}

QualType ASTContext::getExceptionObjectType(QualType T) const {
  // C++ [except.throw]p3:
  //   A throw-expression initializes a temporary object, called the exception
  //   object, the type of which is determined by removing any top-level
  //   cv-qualifiers from the static type of the operand of throw and adjusting
  //   the type from "array of T" or "function returning T" to "pointer to T"
  //   or "pointer to function returning T", [...]
  T = getVariableArrayDecayedType(T);
  if (T->isArrayType() || T->isFunctionType())
    T = getDecayedType(T);
  return T.getUnqualifiedType();
}

/// getArrayDecayedType - Return the properly qualified result of decaying the
/// specified array type to a pointer.  This operation is non-trivial when
/// handling typedefs etc.  The canonical type of "T" must be an array type,
/// this returns a pointer to a properly qualified element of the array.
///
/// See C99 6.7.5.3p7 and C99 6.3.2.1p3.
QualType ASTContext::getArrayDecayedType(QualType Ty) const {
  // Get the element type with 'getAsArrayType' so that we don't lose any
  // typedefs in the element type of the array.  This also handles propagation
  // of type qualifiers from the array type into the element type if present
  // (C99 6.7.3p8).
  const ArrayType *PrettyArrayType = getAsArrayType(Ty);
  assert(PrettyArrayType && "Not an array type!");

  QualType PtrTy = getPointerType(PrettyArrayType->getElementType());

  // int x[restrict 4] ->  int *restrict
  QualType Result = getQualifiedType(PtrTy,
                                     PrettyArrayType->getIndexTypeQualifiers());

  // int x[_Nullable] -> int * _Nullable
  if (auto Nullability = Ty->getNullability()) {
    Result = const_cast<ASTContext *>(this)->getAttributedType(
        AttributedType::getNullabilityAttrKind(*Nullability), Result, Result);
  }
  return Result;
}

QualType ASTContext::getBaseElementType(const ArrayType *array) const {
  return getBaseElementType(array->getElementType());
}

QualType ASTContext::getBaseElementType(QualType type) const {
  Qualifiers qs;
  while (true) {
    SplitQualType split = type.getSplitDesugaredType();
    const ArrayType *array = split.Ty->getAsArrayTypeUnsafe();
    if (!array) break;

    type = array->getElementType();
    qs.addConsistentQualifiers(split.Quals);
  }

  return getQualifiedType(type, qs);
}

/// getConstantArrayElementCount - Returns number of constant array elements.
uint64_t
ASTContext::getConstantArrayElementCount(const ConstantArrayType *CA)  const {
  uint64_t ElementCount = 1;
  do {
    ElementCount *= CA->getZExtSize();
    CA = dyn_cast_or_null<ConstantArrayType>(
      CA->getElementType()->getAsArrayTypeUnsafe());
  } while (CA);
  return ElementCount;
}

uint64_t ASTContext::getArrayInitLoopExprElementCount(
    const ArrayInitLoopExpr *AILE) const {
  if (!AILE)
    return 0;

  uint64_t ElementCount = 1;

  do {
    ElementCount *= AILE->getArraySize().getZExtValue();
    AILE = dyn_cast<ArrayInitLoopExpr>(AILE->getSubExpr());
  } while (AILE);

  return ElementCount;
}

/// getFloatingRank - Return a relative rank for floating point types.
/// This routine will assert if passed a built-in type that isn't a float.
static FloatingRank getFloatingRank(QualType T) {
  if (const auto *CT = T->getAs<ComplexType>())
    return getFloatingRank(CT->getElementType());

  switch (T->castAs<BuiltinType>()->getKind()) {
  default: llvm_unreachable("getFloatingRank(): not a floating type");
  case BuiltinType::Float16:    return Float16Rank;
  case BuiltinType::Half:       return HalfRank;
  case BuiltinType::Float:      return FloatRank;
  case BuiltinType::Double:     return DoubleRank;
  case BuiltinType::LongDouble: return LongDoubleRank;
  case BuiltinType::Float128:   return Float128Rank;
  case BuiltinType::BFloat16:   return BFloat16Rank;
  case BuiltinType::Ibm128:     return Ibm128Rank;
  }
}

/// getFloatingTypeOrder - Compare the rank of the two specified floating
/// point types, ignoring the domain of the type (i.e. 'double' ==
/// '_Complex double').  If LHS > RHS, return 1.  If LHS == RHS, return 0. If
/// LHS < RHS, return -1.
int ASTContext::getFloatingTypeOrder(QualType LHS, QualType RHS) const {
  FloatingRank LHSR = getFloatingRank(LHS);
  FloatingRank RHSR = getFloatingRank(RHS);

  if (LHSR == RHSR)
    return 0;
  if (LHSR > RHSR)
    return 1;
  return -1;
}

int ASTContext::getFloatingTypeSemanticOrder(QualType LHS, QualType RHS) const {
  if (&getFloatTypeSemantics(LHS) == &getFloatTypeSemantics(RHS))
    return 0;
  return getFloatingTypeOrder(LHS, RHS);
}

/// getIntegerRank - Return an integer conversion rank (C99 6.3.1.1p1). This
/// routine will assert if passed a built-in type that isn't an integer or enum,
/// or if it is not canonicalized.
unsigned ASTContext::getIntegerRank(const Type *T) const {
  assert(T->isCanonicalUnqualified() && "T should be canonicalized");

  // Results in this 'losing' to any type of the same size, but winning if
  // larger.
  if (const auto *EIT = dyn_cast<BitIntType>(T))
    return 0 + (EIT->getNumBits() << 3);

  switch (cast<BuiltinType>(T)->getKind()) {
  default: llvm_unreachable("getIntegerRank(): not a built-in integer");
  case BuiltinType::Bool:
    return 1 + (getIntWidth(BoolTy) << 3);
  case BuiltinType::Char_S:
  case BuiltinType::Char_U:
  case BuiltinType::SChar:
  case BuiltinType::UChar:
    return 2 + (getIntWidth(CharTy) << 3);
  case BuiltinType::Short:
  case BuiltinType::UShort:
    return 3 + (getIntWidth(ShortTy) << 3);
  case BuiltinType::Int:
  case BuiltinType::UInt:
    return 4 + (getIntWidth(IntTy) << 3);
  case BuiltinType::Long:
  case BuiltinType::ULong:
    return 5 + (getIntWidth(LongTy) << 3);
  case BuiltinType::LongLong:
  case BuiltinType::ULongLong:
    return 6 + (getIntWidth(LongLongTy) << 3);
  case BuiltinType::Int128:
  case BuiltinType::UInt128:
    return 7 + (getIntWidth(Int128Ty) << 3);

  // "The ranks of char8_t, char16_t, char32_t, and wchar_t equal the ranks of
  // their underlying types" [c++20 conv.rank]
  case BuiltinType::Char8:
    return getIntegerRank(UnsignedCharTy.getTypePtr());
  case BuiltinType::Char16:
    return getIntegerRank(
        getFromTargetType(Target->getChar16Type()).getTypePtr());
  case BuiltinType::Char32:
    return getIntegerRank(
        getFromTargetType(Target->getChar32Type()).getTypePtr());
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    return getIntegerRank(
        getFromTargetType(Target->getWCharType()).getTypePtr());
  }
}

/// Whether this is a promotable bitfield reference according
/// to C99 6.3.1.1p2, bullet 2 (and GCC extensions).
///
/// \returns the type this bit-field will promote to, or NULL if no
/// promotion occurs.
QualType ASTContext::isPromotableBitField(Expr *E) const {
  if (E->isTypeDependent() || E->isValueDependent())
    return {};

  // C++ [conv.prom]p5:
  //    If the bit-field has an enumerated type, it is treated as any other
  //    value of that type for promotion purposes.
  if (getLangOpts().CPlusPlus && E->getType()->isEnumeralType())
    return {};

  // FIXME: We should not do this unless E->refersToBitField() is true. This
  // matters in C where getSourceBitField() will find bit-fields for various
  // cases where the source expression is not a bit-field designator.

  FieldDecl *Field = E->getSourceBitField(); // FIXME: conditional bit-fields?
  if (!Field)
    return {};

  QualType FT = Field->getType();

  uint64_t BitWidth = Field->getBitWidthValue(*this);
  uint64_t IntSize = getTypeSize(IntTy);
  // C++ [conv.prom]p5:
  //   A prvalue for an integral bit-field can be converted to a prvalue of type
  //   int if int can represent all the values of the bit-field; otherwise, it
  //   can be converted to unsigned int if unsigned int can represent all the
  //   values of the bit-field. If the bit-field is larger yet, no integral
  //   promotion applies to it.
  // C11 6.3.1.1/2:
  //   [For a bit-field of type _Bool, int, signed int, or unsigned int:]
  //   If an int can represent all values of the original type (as restricted by
  //   the width, for a bit-field), the value is converted to an int; otherwise,
  //   it is converted to an unsigned int.
  //
  // FIXME: C does not permit promotion of a 'long : 3' bitfield to int.
  //        We perform that promotion here to match GCC and C++.
  // FIXME: C does not permit promotion of an enum bit-field whose rank is
  //        greater than that of 'int'. We perform that promotion to match GCC.
  //
  // C23 6.3.1.1p2:
  //   The value from a bit-field of a bit-precise integer type is converted to
  //   the corresponding bit-precise integer type. (The rest is the same as in
  //   C11.)
  if (QualType QT = Field->getType(); QT->isBitIntType())
    return QT;

  if (BitWidth < IntSize)
    return IntTy;

  if (BitWidth == IntSize)
    return FT->isSignedIntegerType() ? IntTy : UnsignedIntTy;

  // Bit-fields wider than int are not subject to promotions, and therefore act
  // like the base type. GCC has some weird bugs in this area that we
  // deliberately do not follow (GCC follows a pre-standard resolution to
  // C's DR315 which treats bit-width as being part of the type, and this leaks
  // into their semantics in some cases).
  return {};
}

/// getPromotedIntegerType - Returns the type that Promotable will
/// promote to: C99 6.3.1.1p2, assuming that Promotable is a promotable
/// integer type.
QualType ASTContext::getPromotedIntegerType(QualType Promotable) const {
  assert(!Promotable.isNull());
  assert(isPromotableIntegerType(Promotable));
  if (const auto *ET = Promotable->getAs<EnumType>())
    return ET->getDecl()->getPromotionType();

  if (const auto *BT = Promotable->getAs<BuiltinType>()) {
    // C++ [conv.prom]: A prvalue of type char16_t, char32_t, or wchar_t
    // (3.9.1) can be converted to a prvalue of the first of the following
    // types that can represent all the values of its underlying type:
    // int, unsigned int, long int, unsigned long int, long long int, or
    // unsigned long long int [...]
    // FIXME: Is there some better way to compute this?
    if (BT->getKind() == BuiltinType::WChar_S ||
        BT->getKind() == BuiltinType::WChar_U ||
        BT->getKind() == BuiltinType::Char8 ||
        BT->getKind() == BuiltinType::Char16 ||
        BT->getKind() == BuiltinType::Char32) {
      bool FromIsSigned = BT->getKind() == BuiltinType::WChar_S;
      uint64_t FromSize = getTypeSize(BT);
      QualType PromoteTypes[] = { IntTy, UnsignedIntTy, LongTy, UnsignedLongTy,
                                  LongLongTy, UnsignedLongLongTy };
      for (const auto &PT : PromoteTypes) {
        uint64_t ToSize = getTypeSize(PT);
        if (FromSize < ToSize ||
            (FromSize == ToSize && FromIsSigned == PT->isSignedIntegerType()))
          return PT;
      }
      llvm_unreachable("char type should fit into long long");
    }
  }

  // At this point, we should have a signed or unsigned integer type.
  if (Promotable->isSignedIntegerType())
    return IntTy;
  uint64_t PromotableSize = getIntWidth(Promotable);
  uint64_t IntSize = getIntWidth(IntTy);
  assert(Promotable->isUnsignedIntegerType() && PromotableSize <= IntSize);
  return (PromotableSize != IntSize) ? IntTy : UnsignedIntTy;
}

/// Recurses in pointer/array types until it finds an objc retainable
/// type and returns its ownership.
Qualifiers::ObjCLifetime ASTContext::getInnerObjCOwnership(QualType T) const {
  while (!T.isNull()) {
    if (T.getObjCLifetime() != Qualifiers::OCL_None)
      return T.getObjCLifetime();
    if (T->isArrayType())
      T = getBaseElementType(T);
    else if (const auto *PT = T->getAs<PointerType>())
      T = PT->getPointeeType();
    else if (const auto *RT = T->getAs<ReferenceType>())
      T = RT->getPointeeType();
    else
      break;
  }

  return Qualifiers::OCL_None;
}

static const Type *getIntegerTypeForEnum(const EnumType *ET) {
  // Incomplete enum types are not treated as integer types.
  // FIXME: In C++, enum types are never integer types.
  if (ET->getDecl()->isComplete() && !ET->getDecl()->isScoped())
    return ET->getDecl()->getIntegerType().getTypePtr();
  return nullptr;
}

/// getIntegerTypeOrder - Returns the highest ranked integer type:
/// C99 6.3.1.8p1.  If LHS > RHS, return 1.  If LHS == RHS, return 0. If
/// LHS < RHS, return -1.
int ASTContext::getIntegerTypeOrder(QualType LHS, QualType RHS) const {
  const Type *LHSC = getCanonicalType(LHS).getTypePtr();
  const Type *RHSC = getCanonicalType(RHS).getTypePtr();

  // Unwrap enums to their underlying type.
  if (const auto *ET = dyn_cast<EnumType>(LHSC))
    LHSC = getIntegerTypeForEnum(ET);
  if (const auto *ET = dyn_cast<EnumType>(RHSC))
    RHSC = getIntegerTypeForEnum(ET);

  if (LHSC == RHSC) return 0;

  bool LHSUnsigned = LHSC->isUnsignedIntegerType();
  bool RHSUnsigned = RHSC->isUnsignedIntegerType();

  unsigned LHSRank = getIntegerRank(LHSC);
  unsigned RHSRank = getIntegerRank(RHSC);

  if (LHSUnsigned == RHSUnsigned) {  // Both signed or both unsigned.
    if (LHSRank == RHSRank) return 0;
    return LHSRank > RHSRank ? 1 : -1;
  }

  // Otherwise, the LHS is signed and the RHS is unsigned or visa versa.
  if (LHSUnsigned) {
    // If the unsigned [LHS] type is larger, return it.
    if (LHSRank >= RHSRank)
      return 1;

    // If the signed type can represent all values of the unsigned type, it
    // wins.  Because we are dealing with 2's complement and types that are
    // powers of two larger than each other, this is always safe.
    return -1;
  }

  // If the unsigned [RHS] type is larger, return it.
  if (RHSRank >= LHSRank)
    return -1;

  // If the signed type can represent all values of the unsigned type, it
  // wins.  Because we are dealing with 2's complement and types that are
  // powers of two larger than each other, this is always safe.
  return 1;
}

TypedefDecl *ASTContext::getCFConstantStringDecl() const {
  if (CFConstantStringTypeDecl)
    return CFConstantStringTypeDecl;

  assert(!CFConstantStringTagDecl &&
         "tag and typedef should be initialized together");
  CFConstantStringTagDecl = buildImplicitRecord("__NSConstantString_tag");
  CFConstantStringTagDecl->startDefinition();

  struct {
    QualType Type;
    const char *Name;
  } Fields[5];
  unsigned Count = 0;

  /// Objective-C ABI
  ///
  ///    typedef struct __NSConstantString_tag {
  ///      const int *isa;
  ///      int flags;
  ///      const char *str;
  ///      long length;
  ///    } __NSConstantString;
  ///
  /// Swift ABI (4.1, 4.2)
  ///
  ///    typedef struct __NSConstantString_tag {
  ///      uintptr_t _cfisa;
  ///      uintptr_t _swift_rc;
  ///      _Atomic(uint64_t) _cfinfoa;
  ///      const char *_ptr;
  ///      uint32_t _length;
  ///    } __NSConstantString;
  ///
  /// Swift ABI (5.0)
  ///
  ///    typedef struct __NSConstantString_tag {
  ///      uintptr_t _cfisa;
  ///      uintptr_t _swift_rc;
  ///      _Atomic(uint64_t) _cfinfoa;
  ///      const char *_ptr;
  ///      uintptr_t _length;
  ///    } __NSConstantString;

  const auto CFRuntime = getLangOpts().CFRuntime;
  if (static_cast<unsigned>(CFRuntime) <
      static_cast<unsigned>(LangOptions::CoreFoundationABI::Swift)) {
    Fields[Count++] = { getPointerType(IntTy.withConst()), "isa" };
    Fields[Count++] = { IntTy, "flags" };
    Fields[Count++] = { getPointerType(CharTy.withConst()), "str" };
    Fields[Count++] = { LongTy, "length" };
  } else {
    Fields[Count++] = { getUIntPtrType(), "_cfisa" };
    Fields[Count++] = { getUIntPtrType(), "_swift_rc" };
    Fields[Count++] = { getFromTargetType(Target->getUInt64Type()), "_swift_rc" };
    Fields[Count++] = { getPointerType(CharTy.withConst()), "_ptr" };
    if (CFRuntime == LangOptions::CoreFoundationABI::Swift4_1 ||
        CFRuntime == LangOptions::CoreFoundationABI::Swift4_2)
      Fields[Count++] = { IntTy, "_ptr" };
    else
      Fields[Count++] = { getUIntPtrType(), "_ptr" };
  }

  // Create fields
  for (unsigned i = 0; i < Count; ++i) {
    FieldDecl *Field =
        FieldDecl::Create(*this, CFConstantStringTagDecl, SourceLocation(),
                          SourceLocation(), &Idents.get(Fields[i].Name),
                          Fields[i].Type, /*TInfo=*/nullptr,
                          /*BitWidth=*/nullptr, /*Mutable=*/false, ICIS_NoInit);
    Field->setAccess(AS_public);
    CFConstantStringTagDecl->addDecl(Field);
  }

  CFConstantStringTagDecl->completeDefinition();
  // This type is designed to be compatible with NSConstantString, but cannot
  // use the same name, since NSConstantString is an interface.
  auto tagType = getTagDeclType(CFConstantStringTagDecl);
  CFConstantStringTypeDecl =
      buildImplicitTypedef(tagType, "__NSConstantString");

  return CFConstantStringTypeDecl;
}

RecordDecl *ASTContext::getCFConstantStringTagDecl() const {
  if (!CFConstantStringTagDecl)
    getCFConstantStringDecl(); // Build the tag and the typedef.
  return CFConstantStringTagDecl;
}

// getCFConstantStringType - Return the type used for constant CFStrings.
QualType ASTContext::getCFConstantStringType() const {
  return getTypedefType(getCFConstantStringDecl());
}

QualType ASTContext::getObjCSuperType() const {
  if (ObjCSuperType.isNull()) {
    RecordDecl *ObjCSuperTypeDecl = buildImplicitRecord("objc_super");
    getTranslationUnitDecl()->addDecl(ObjCSuperTypeDecl);
    ObjCSuperType = getTagDeclType(ObjCSuperTypeDecl);
  }
  return ObjCSuperType;
}

void ASTContext::setCFConstantStringType(QualType T) {
  const auto *TD = T->castAs<TypedefType>();
  CFConstantStringTypeDecl = cast<TypedefDecl>(TD->getDecl());
  const auto *TagType =
      CFConstantStringTypeDecl->getUnderlyingType()->castAs<RecordType>();
  CFConstantStringTagDecl = TagType->getDecl();
}

QualType ASTContext::getBlockDescriptorType() const {
  if (BlockDescriptorType)
    return getTagDeclType(BlockDescriptorType);

  RecordDecl *RD;
  // FIXME: Needs the FlagAppleBlock bit.
  RD = buildImplicitRecord("__block_descriptor");
  RD->startDefinition();

  QualType FieldTypes[] = {
    UnsignedLongTy,
    UnsignedLongTy,
  };

  static const char *const FieldNames[] = {
    "reserved",
    "Size"
  };

  for (size_t i = 0; i < 2; ++i) {
    FieldDecl *Field = FieldDecl::Create(
        *this, RD, SourceLocation(), SourceLocation(),
        &Idents.get(FieldNames[i]), FieldTypes[i], /*TInfo=*/nullptr,
        /*BitWidth=*/nullptr, /*Mutable=*/false, ICIS_NoInit);
    Field->setAccess(AS_public);
    RD->addDecl(Field);
  }

  RD->completeDefinition();

  BlockDescriptorType = RD;

  return getTagDeclType(BlockDescriptorType);
}

QualType ASTContext::getBlockDescriptorExtendedType() const {
  if (BlockDescriptorExtendedType)
    return getTagDeclType(BlockDescriptorExtendedType);

  RecordDecl *RD;
  // FIXME: Needs the FlagAppleBlock bit.
  RD = buildImplicitRecord("__block_descriptor_withcopydispose");
  RD->startDefinition();

  QualType FieldTypes[] = {
    UnsignedLongTy,
    UnsignedLongTy,
    getPointerType(VoidPtrTy),
    getPointerType(VoidPtrTy)
  };

  static const char *const FieldNames[] = {
    "reserved",
    "Size",
    "CopyFuncPtr",
    "DestroyFuncPtr"
  };

  for (size_t i = 0; i < 4; ++i) {
    FieldDecl *Field = FieldDecl::Create(
        *this, RD, SourceLocation(), SourceLocation(),
        &Idents.get(FieldNames[i]), FieldTypes[i], /*TInfo=*/nullptr,
        /*BitWidth=*/nullptr,
        /*Mutable=*/false, ICIS_NoInit);
    Field->setAccess(AS_public);
    RD->addDecl(Field);
  }

  RD->completeDefinition();

  BlockDescriptorExtendedType = RD;
  return getTagDeclType(BlockDescriptorExtendedType);
}

OpenCLTypeKind ASTContext::getOpenCLTypeKind(const Type *T) const {
  const auto *BT = dyn_cast<BuiltinType>(T);

  if (!BT) {
    if (isa<PipeType>(T))
      return OCLTK_Pipe;

    return OCLTK_Default;
  }

  switch (BT->getKind()) {
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix)                   \
  case BuiltinType::Id:                                                        \
    return OCLTK_Image;
#include "clang/Basic/OpenCLImageTypes.def"

  case BuiltinType::OCLClkEvent:
    return OCLTK_ClkEvent;

  case BuiltinType::OCLEvent:
    return OCLTK_Event;

  case BuiltinType::OCLQueue:
    return OCLTK_Queue;

  case BuiltinType::OCLReserveID:
    return OCLTK_ReserveID;

  case BuiltinType::OCLSampler:
    return OCLTK_Sampler;

  default:
    return OCLTK_Default;
  }
}

LangAS ASTContext::getOpenCLTypeAddrSpace(const Type *T) const {
  return Target->getOpenCLTypeAddrSpace(getOpenCLTypeKind(T));
}

/// BlockRequiresCopying - Returns true if byref variable "D" of type "Ty"
/// requires copy/dispose. Note that this must match the logic
/// in buildByrefHelpers.
bool ASTContext::BlockRequiresCopying(QualType Ty,
                                      const VarDecl *D) {
  if (const CXXRecordDecl *record = Ty->getAsCXXRecordDecl()) {
    const Expr *copyExpr = getBlockVarCopyInit(D).getCopyExpr();
    if (!copyExpr && record->hasTrivialDestructor()) return false;

    return true;
  }

  // The block needs copy/destroy helpers if Ty is non-trivial to destructively
  // move or destroy.
  if (Ty.isNonTrivialToPrimitiveDestructiveMove() || Ty.isDestructedType())
    return true;

  if (!Ty->isObjCRetainableType()) return false;

  Qualifiers qs = Ty.getQualifiers();

  // If we have lifetime, that dominates.
  if (Qualifiers::ObjCLifetime lifetime = qs.getObjCLifetime()) {
    switch (lifetime) {
      case Qualifiers::OCL_None: llvm_unreachable("impossible");

      // These are just bits as far as the runtime is concerned.
      case Qualifiers::OCL_ExplicitNone:
      case Qualifiers::OCL_Autoreleasing:
        return false;

      // These cases should have been taken care of when checking the type's
      // non-triviality.
      case Qualifiers::OCL_Weak:
      case Qualifiers::OCL_Strong:
        llvm_unreachable("impossible");
    }
    llvm_unreachable("fell out of lifetime switch!");
  }
  return (Ty->isBlockPointerType() || isObjCNSObjectType(Ty) ||
          Ty->isObjCObjectPointerType());
}

bool ASTContext::getByrefLifetime(QualType Ty,
                              Qualifiers::ObjCLifetime &LifeTime,
                              bool &HasByrefExtendedLayout) const {
  if (!getLangOpts().ObjC ||
      getLangOpts().getGC() != LangOptions::NonGC)
    return false;

  HasByrefExtendedLayout = false;
  if (Ty->isRecordType()) {
    HasByrefExtendedLayout = true;
    LifeTime = Qualifiers::OCL_None;
  } else if ((LifeTime = Ty.getObjCLifetime())) {
    // Honor the ARC qualifiers.
  } else if (Ty->isObjCObjectPointerType() || Ty->isBlockPointerType()) {
    // The MRR rule.
    LifeTime = Qualifiers::OCL_ExplicitNone;
  } else {
    LifeTime = Qualifiers::OCL_None;
  }
  return true;
}

CanQualType ASTContext::getNSUIntegerType() const {
  assert(Target && "Expected target to be initialized");
  const llvm::Triple &T = Target->getTriple();
  // Windows is LLP64 rather than LP64
  if (T.isOSWindows() && T.isArch64Bit())
    return UnsignedLongLongTy;
  return UnsignedLongTy;
}

CanQualType ASTContext::getNSIntegerType() const {
  assert(Target && "Expected target to be initialized");
  const llvm::Triple &T = Target->getTriple();
  // Windows is LLP64 rather than LP64
  if (T.isOSWindows() && T.isArch64Bit())
    return LongLongTy;
  return LongTy;
}

TypedefDecl *ASTContext::getObjCInstanceTypeDecl() {
  if (!ObjCInstanceTypeDecl)
    ObjCInstanceTypeDecl =
        buildImplicitTypedef(getObjCIdType(), "instancetype");
  return ObjCInstanceTypeDecl;
}

// This returns true if a type has been typedefed to BOOL:
// typedef <type> BOOL;
static bool isTypeTypedefedAsBOOL(QualType T) {
  if (const auto *TT = dyn_cast<TypedefType>(T))
    if (IdentifierInfo *II = TT->getDecl()->getIdentifier())
      return II->isStr("BOOL");

  return false;
}

/// getObjCEncodingTypeSize returns size of type for objective-c encoding
/// purpose.
CharUnits ASTContext::getObjCEncodingTypeSize(QualType type) const {
  if (!type->isIncompleteArrayType() && type->isIncompleteType())
    return CharUnits::Zero();

  CharUnits sz = getTypeSizeInChars(type);

  // Make all integer and enum types at least as large as an int
  if (sz.isPositive() && type->isIntegralOrEnumerationType())
    sz = std::max(sz, getTypeSizeInChars(IntTy));
  // Treat arrays as pointers, since that's how they're passed in.
  else if (type->isArrayType())
    sz = getTypeSizeInChars(VoidPtrTy);
  return sz;
}

bool ASTContext::isMSStaticDataMemberInlineDefinition(const VarDecl *VD) const {
  return getTargetInfo().getCXXABI().isMicrosoft() &&
         VD->isStaticDataMember() &&
         VD->getType()->isIntegralOrEnumerationType() &&
         !VD->getFirstDecl()->isOutOfLine() && VD->getFirstDecl()->hasInit();
}

ASTContext::InlineVariableDefinitionKind
ASTContext::getInlineVariableDefinitionKind(const VarDecl *VD) const {
  if (!VD->isInline())
    return InlineVariableDefinitionKind::None;

  // In almost all cases, it's a weak definition.
  auto *First = VD->getFirstDecl();
  if (First->isInlineSpecified() || !First->isStaticDataMember())
    return InlineVariableDefinitionKind::Weak;

  // If there's a file-context declaration in this translation unit, it's a
  // non-discardable definition.
  for (auto *D : VD->redecls())
    if (D->getLexicalDeclContext()->isFileContext() &&
        !D->isInlineSpecified() && (D->isConstexpr() || First->isConstexpr()))
      return InlineVariableDefinitionKind::Strong;

  // If we've not seen one yet, we don't know.
  return InlineVariableDefinitionKind::WeakUnknown;
}

static std::string charUnitsToString(const CharUnits &CU) {
  return llvm::itostr(CU.getQuantity());
}

/// getObjCEncodingForBlock - Return the encoded type for this block
/// declaration.
std::string ASTContext::getObjCEncodingForBlock(const BlockExpr *Expr) const {
  std::string S;

  const BlockDecl *Decl = Expr->getBlockDecl();
  QualType BlockTy =
      Expr->getType()->castAs<BlockPointerType>()->getPointeeType();
  QualType BlockReturnTy = BlockTy->castAs<FunctionType>()->getReturnType();
  // Encode result type.
  if (getLangOpts().EncodeExtendedBlockSig)
    getObjCEncodingForMethodParameter(Decl::OBJC_TQ_None, BlockReturnTy, S,
                                      true /*Extended*/);
  else
    getObjCEncodingForType(BlockReturnTy, S);
  // Compute size of all parameters.
  // Start with computing size of a pointer in number of bytes.
  // FIXME: There might(should) be a better way of doing this computation!
  CharUnits PtrSize = getTypeSizeInChars(VoidPtrTy);
  CharUnits ParmOffset = PtrSize;
  for (auto *PI : Decl->parameters()) {
    QualType PType = PI->getType();
    CharUnits sz = getObjCEncodingTypeSize(PType);
    if (sz.isZero())
      continue;
    assert(sz.isPositive() && "BlockExpr - Incomplete param type");
    ParmOffset += sz;
  }
  // Size of the argument frame
  S += charUnitsToString(ParmOffset);
  // Block pointer and offset.
  S += "@?0";

  // Argument types.
  ParmOffset = PtrSize;
  for (auto *PVDecl : Decl->parameters()) {
    QualType PType = PVDecl->getOriginalType();
    if (const auto *AT =
            dyn_cast<ArrayType>(PType->getCanonicalTypeInternal())) {
      // Use array's original type only if it has known number of
      // elements.
      if (!isa<ConstantArrayType>(AT))
        PType = PVDecl->getType();
    } else if (PType->isFunctionType())
      PType = PVDecl->getType();
    if (getLangOpts().EncodeExtendedBlockSig)
      getObjCEncodingForMethodParameter(Decl::OBJC_TQ_None, PType,
                                      S, true /*Extended*/);
    else
      getObjCEncodingForType(PType, S);
    S += charUnitsToString(ParmOffset);
    ParmOffset += getObjCEncodingTypeSize(PType);
  }

  return S;
}

std::string
ASTContext::getObjCEncodingForFunctionDecl(const FunctionDecl *Decl) const {
  std::string S;
  // Encode result type.
  getObjCEncodingForType(Decl->getReturnType(), S);
  CharUnits ParmOffset;
  // Compute size of all parameters.
  for (auto *PI : Decl->parameters()) {
    QualType PType = PI->getType();
    CharUnits sz = getObjCEncodingTypeSize(PType);
    if (sz.isZero())
      continue;

    assert(sz.isPositive() &&
           "getObjCEncodingForFunctionDecl - Incomplete param type");
    ParmOffset += sz;
  }
  S += charUnitsToString(ParmOffset);
  ParmOffset = CharUnits::Zero();

  // Argument types.
  for (auto *PVDecl : Decl->parameters()) {
    QualType PType = PVDecl->getOriginalType();
    if (const auto *AT =
            dyn_cast<ArrayType>(PType->getCanonicalTypeInternal())) {
      // Use array's original type only if it has known number of
      // elements.
      if (!isa<ConstantArrayType>(AT))
        PType = PVDecl->getType();
    } else if (PType->isFunctionType())
      PType = PVDecl->getType();
    getObjCEncodingForType(PType, S);
    S += charUnitsToString(ParmOffset);
    ParmOffset += getObjCEncodingTypeSize(PType);
  }

  return S;
}

/// getObjCEncodingForMethodParameter - Return the encoded type for a single
/// method parameter or return type. If Extended, include class names and
/// block object types.
void ASTContext::getObjCEncodingForMethodParameter(Decl::ObjCDeclQualifier QT,
                                                   QualType T, std::string& S,
                                                   bool Extended) const {
  // Encode type qualifier, 'in', 'inout', etc. for the parameter.
  getObjCEncodingForTypeQualifier(QT, S);
  // Encode parameter type.
  ObjCEncOptions Options = ObjCEncOptions()
                               .setExpandPointedToStructures()
                               .setExpandStructures()
                               .setIsOutermostType();
  if (Extended)
    Options.setEncodeBlockParameters().setEncodeClassNames();
  getObjCEncodingForTypeImpl(T, S, Options, /*Field=*/nullptr);
}

/// getObjCEncodingForMethodDecl - Return the encoded type for this method
/// declaration.
std::string ASTContext::getObjCEncodingForMethodDecl(const ObjCMethodDecl *Decl,
                                                     bool Extended) const {
  // FIXME: This is not very efficient.
  // Encode return type.
  std::string S;
  getObjCEncodingForMethodParameter(Decl->getObjCDeclQualifier(),
                                    Decl->getReturnType(), S, Extended);
  // Compute size of all parameters.
  // Start with computing size of a pointer in number of bytes.
  // FIXME: There might(should) be a better way of doing this computation!
  CharUnits PtrSize = getTypeSizeInChars(VoidPtrTy);
  // The first two arguments (self and _cmd) are pointers; account for
  // their size.
  CharUnits ParmOffset = 2 * PtrSize;
  for (ObjCMethodDecl::param_const_iterator PI = Decl->param_begin(),
       E = Decl->sel_param_end(); PI != E; ++PI) {
    QualType PType = (*PI)->getType();
    CharUnits sz = getObjCEncodingTypeSize(PType);
    if (sz.isZero())
      continue;

    assert(sz.isPositive() &&
           "getObjCEncodingForMethodDecl - Incomplete param type");
    ParmOffset += sz;
  }
  S += charUnitsToString(ParmOffset);
  S += "@0:";
  S += charUnitsToString(PtrSize);

  // Argument types.
  ParmOffset = 2 * PtrSize;
  for (ObjCMethodDecl::param_const_iterator PI = Decl->param_begin(),
       E = Decl->sel_param_end(); PI != E; ++PI) {
    const ParmVarDecl *PVDecl = *PI;
    QualType PType = PVDecl->getOriginalType();
    if (const auto *AT =
            dyn_cast<ArrayType>(PType->getCanonicalTypeInternal())) {
      // Use array's original type only if it has known number of
      // elements.
      if (!isa<ConstantArrayType>(AT))
        PType = PVDecl->getType();
    } else if (PType->isFunctionType())
      PType = PVDecl->getType();
    getObjCEncodingForMethodParameter(PVDecl->getObjCDeclQualifier(),
                                      PType, S, Extended);
    S += charUnitsToString(ParmOffset);
    ParmOffset += getObjCEncodingTypeSize(PType);
  }

  return S;
}

ObjCPropertyImplDecl *
ASTContext::getObjCPropertyImplDeclForPropertyDecl(
                                      const ObjCPropertyDecl *PD,
                                      const Decl *Container) const {
  if (!Container)
    return nullptr;
  if (const auto *CID = dyn_cast<ObjCCategoryImplDecl>(Container)) {
    for (auto *PID : CID->property_impls())
      if (PID->getPropertyDecl() == PD)
        return PID;
  } else {
    const auto *OID = cast<ObjCImplementationDecl>(Container);
    for (auto *PID : OID->property_impls())
      if (PID->getPropertyDecl() == PD)
        return PID;
  }
  return nullptr;
}

/// getObjCEncodingForPropertyDecl - Return the encoded type for this
/// property declaration. If non-NULL, Container must be either an
/// ObjCCategoryImplDecl or ObjCImplementationDecl; it should only be
/// NULL when getting encodings for protocol properties.
/// Property attributes are stored as a comma-delimited C string. The simple
/// attributes readonly and bycopy are encoded as single characters. The
/// parametrized attributes, getter=name, setter=name, and ivar=name, are
/// encoded as single characters, followed by an identifier. Property types
/// are also encoded as a parametrized attribute. The characters used to encode
/// these attributes are defined by the following enumeration:
/// @code
/// enum PropertyAttributes {
/// kPropertyReadOnly = 'R',   // property is read-only.
/// kPropertyBycopy = 'C',     // property is a copy of the value last assigned
/// kPropertyByref = '&',  // property is a reference to the value last assigned
/// kPropertyDynamic = 'D',    // property is dynamic
/// kPropertyGetter = 'G',     // followed by getter selector name
/// kPropertySetter = 'S',     // followed by setter selector name
/// kPropertyInstanceVariable = 'V'  // followed by instance variable  name
/// kPropertyType = 'T'              // followed by old-style type encoding.
/// kPropertyWeak = 'W'              // 'weak' property
/// kPropertyStrong = 'P'            // property GC'able
/// kPropertyNonAtomic = 'N'         // property non-atomic
/// kPropertyOptional = '?'          // property optional
/// };
/// @endcode
std::string
ASTContext::getObjCEncodingForPropertyDecl(const ObjCPropertyDecl *PD,
                                           const Decl *Container) const {
  // Collect information from the property implementation decl(s).
  bool Dynamic = false;
  ObjCPropertyImplDecl *SynthesizePID = nullptr;

  if (ObjCPropertyImplDecl *PropertyImpDecl =
      getObjCPropertyImplDeclForPropertyDecl(PD, Container)) {
    if (PropertyImpDecl->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic)
      Dynamic = true;
    else
      SynthesizePID = PropertyImpDecl;
  }

  // FIXME: This is not very efficient.
  std::string S = "T";

  // Encode result type.
  // GCC has some special rules regarding encoding of properties which
  // closely resembles encoding of ivars.
  getObjCEncodingForPropertyType(PD->getType(), S);

  if (PD->isOptional())
    S += ",?";

  if (PD->isReadOnly()) {
    S += ",R";
    if (PD->getPropertyAttributes() & ObjCPropertyAttribute::kind_copy)
      S += ",C";
    if (PD->getPropertyAttributes() & ObjCPropertyAttribute::kind_retain)
      S += ",&";
    if (PD->getPropertyAttributes() & ObjCPropertyAttribute::kind_weak)
      S += ",W";
  } else {
    switch (PD->getSetterKind()) {
    case ObjCPropertyDecl::Assign: break;
    case ObjCPropertyDecl::Copy:   S += ",C"; break;
    case ObjCPropertyDecl::Retain: S += ",&"; break;
    case ObjCPropertyDecl::Weak:   S += ",W"; break;
    }
  }

  // It really isn't clear at all what this means, since properties
  // are "dynamic by default".
  if (Dynamic)
    S += ",D";

  if (PD->getPropertyAttributes() & ObjCPropertyAttribute::kind_nonatomic)
    S += ",N";

  if (PD->getPropertyAttributes() & ObjCPropertyAttribute::kind_getter) {
    S += ",G";
    S += PD->getGetterName().getAsString();
  }

  if (PD->getPropertyAttributes() & ObjCPropertyAttribute::kind_setter) {
    S += ",S";
    S += PD->getSetterName().getAsString();
  }

  if (SynthesizePID) {
    const ObjCIvarDecl *OID = SynthesizePID->getPropertyIvarDecl();
    S += ",V";
    S += OID->getNameAsString();
  }

  // FIXME: OBJCGC: weak & strong
  return S;
}

/// getLegacyIntegralTypeEncoding -
/// Another legacy compatibility encoding: 32-bit longs are encoded as
/// 'l' or 'L' , but not always.  For typedefs, we need to use
/// 'i' or 'I' instead if encoding a struct field, or a pointer!
void ASTContext::getLegacyIntegralTypeEncoding (QualType &PointeeTy) const {
  if (PointeeTy->getAs<TypedefType>()) {
    if (const auto *BT = PointeeTy->getAs<BuiltinType>()) {
      if (BT->getKind() == BuiltinType::ULong && getIntWidth(PointeeTy) == 32)
        PointeeTy = UnsignedIntTy;
      else
        if (BT->getKind() == BuiltinType::Long && getIntWidth(PointeeTy) == 32)
          PointeeTy = IntTy;
    }
  }
}

void ASTContext::getObjCEncodingForType(QualType T, std::string& S,
                                        const FieldDecl *Field,
                                        QualType *NotEncodedT) const {
  // We follow the behavior of gcc, expanding structures which are
  // directly pointed to, and expanding embedded structures. Note that
  // these rules are sufficient to prevent recursive encoding of the
  // same type.
  getObjCEncodingForTypeImpl(T, S,
                             ObjCEncOptions()
                                 .setExpandPointedToStructures()
                                 .setExpandStructures()
                                 .setIsOutermostType(),
                             Field, NotEncodedT);
}

void ASTContext::getObjCEncodingForPropertyType(QualType T,
                                                std::string& S) const {
  // Encode result type.
  // GCC has some special rules regarding encoding of properties which
  // closely resembles encoding of ivars.
  getObjCEncodingForTypeImpl(T, S,
                             ObjCEncOptions()
                                 .setExpandPointedToStructures()
                                 .setExpandStructures()
                                 .setIsOutermostType()
                                 .setEncodingProperty(),
                             /*Field=*/nullptr);
}

static char getObjCEncodingForPrimitiveType(const ASTContext *C,
                                            const BuiltinType *BT) {
    BuiltinType::Kind kind = BT->getKind();
    switch (kind) {
    case BuiltinType::Void:       return 'v';
    case BuiltinType::Bool:       return 'B';
    case BuiltinType::Char8:
    case BuiltinType::Char_U:
    case BuiltinType::UChar:      return 'C';
    case BuiltinType::Char16:
    case BuiltinType::UShort:     return 'S';
    case BuiltinType::Char32:
    case BuiltinType::UInt:       return 'I';
    case BuiltinType::ULong:
        return C->getTargetInfo().getLongWidth() == 32 ? 'L' : 'Q';
    case BuiltinType::UInt128:    return 'T';
    case BuiltinType::ULongLong:  return 'Q';
    case BuiltinType::Char_S:
    case BuiltinType::SChar:      return 'c';
    case BuiltinType::Short:      return 's';
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
    case BuiltinType::Int:        return 'i';
    case BuiltinType::Long:
      return C->getTargetInfo().getLongWidth() == 32 ? 'l' : 'q';
    case BuiltinType::LongLong:   return 'q';
    case BuiltinType::Int128:     return 't';
    case BuiltinType::Float:      return 'f';
    case BuiltinType::Double:     return 'd';
    case BuiltinType::LongDouble: return 'D';
    case BuiltinType::NullPtr:    return '*'; // like char*

    case BuiltinType::BFloat16:
    case BuiltinType::Float16:
    case BuiltinType::Float128:
    case BuiltinType::Ibm128:
    case BuiltinType::Half:
    case BuiltinType::ShortAccum:
    case BuiltinType::Accum:
    case BuiltinType::LongAccum:
    case BuiltinType::UShortAccum:
    case BuiltinType::UAccum:
    case BuiltinType::ULongAccum:
    case BuiltinType::ShortFract:
    case BuiltinType::Fract:
    case BuiltinType::LongFract:
    case BuiltinType::UShortFract:
    case BuiltinType::UFract:
    case BuiltinType::ULongFract:
    case BuiltinType::SatShortAccum:
    case BuiltinType::SatAccum:
    case BuiltinType::SatLongAccum:
    case BuiltinType::SatUShortAccum:
    case BuiltinType::SatUAccum:
    case BuiltinType::SatULongAccum:
    case BuiltinType::SatShortFract:
    case BuiltinType::SatFract:
    case BuiltinType::SatLongFract:
    case BuiltinType::SatUShortFract:
    case BuiltinType::SatUFract:
    case BuiltinType::SatULongFract:
      // FIXME: potentially need @encodes for these!
      return ' ';

#define SVE_TYPE(Name, Id, SingletonId) \
    case BuiltinType::Id:
#include "clang/Basic/AArch64SVEACLETypes.def"
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
      {
        DiagnosticsEngine &Diags = C->getDiagnostics();
        unsigned DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                                "cannot yet @encode type %0");
        Diags.Report(DiagID) << BT->getName(C->getPrintingPolicy());
        return ' ';
      }

    case BuiltinType::ObjCId:
    case BuiltinType::ObjCClass:
    case BuiltinType::ObjCSel:
      llvm_unreachable("@encoding ObjC primitive type");

    // OpenCL and placeholder types don't need @encodings.
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
    case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
    case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
    case BuiltinType::OCLEvent:
    case BuiltinType::OCLClkEvent:
    case BuiltinType::OCLQueue:
    case BuiltinType::OCLReserveID:
    case BuiltinType::OCLSampler:
    case BuiltinType::Dependent:
#define PPC_VECTOR_TYPE(Name, Id, Size) \
    case BuiltinType::Id:
#include "clang/Basic/PPCTypes.def"
#define BUILTIN_TYPE(KIND, ID)
#define PLACEHOLDER_TYPE(KIND, ID) \
    case BuiltinType::KIND:
#include "clang/AST/BuiltinTypes.def"
      llvm_unreachable("invalid builtin type for @encode");
    }
    llvm_unreachable("invalid BuiltinType::Kind value");
}

static char ObjCEncodingForEnumType(const ASTContext *C, const EnumType *ET) {
  EnumDecl *Enum = ET->getDecl();

  // The encoding of an non-fixed enum type is always 'i', regardless of size.
  if (!Enum->isFixed())
    return 'i';

  // The encoding of a fixed enum type matches its fixed underlying type.
  const auto *BT = Enum->getIntegerType()->castAs<BuiltinType>();
  return getObjCEncodingForPrimitiveType(C, BT);
}

static void EncodeBitField(const ASTContext *Ctx, std::string& S,
                           QualType T, const FieldDecl *FD) {
  assert(FD->isBitField() && "not a bitfield - getObjCEncodingForTypeImpl");
  S += 'b';
  // The NeXT runtime encodes bit fields as b followed by the number of bits.
  // The GNU runtime requires more information; bitfields are encoded as b,
  // then the offset (in bits) of the first element, then the type of the
  // bitfield, then the size in bits.  For example, in this structure:
  //
  // struct
  // {
  //    int integer;
  //    int flags:2;
  // };
  // On a 32-bit system, the encoding for flags would be b2 for the NeXT
  // runtime, but b32i2 for the GNU runtime.  The reason for this extra
  // information is not especially sensible, but we're stuck with it for
  // compatibility with GCC, although providing it breaks anything that
  // actually uses runtime introspection and wants to work on both runtimes...
  if (Ctx->getLangOpts().ObjCRuntime.isGNUFamily()) {
    uint64_t Offset;

    if (const auto *IVD = dyn_cast<ObjCIvarDecl>(FD)) {
      Offset = Ctx->lookupFieldBitOffset(IVD->getContainingInterface(), nullptr,
                                         IVD);
    } else {
      const RecordDecl *RD = FD->getParent();
      const ASTRecordLayout &RL = Ctx->getASTRecordLayout(RD);
      Offset = RL.getFieldOffset(FD->getFieldIndex());
    }

    S += llvm::utostr(Offset);

    if (const auto *ET = T->getAs<EnumType>())
      S += ObjCEncodingForEnumType(Ctx, ET);
    else {
      const auto *BT = T->castAs<BuiltinType>();
      S += getObjCEncodingForPrimitiveType(Ctx, BT);
    }
  }
  S += llvm::utostr(FD->getBitWidthValue(*Ctx));
}

// Helper function for determining whether the encoded type string would include
// a template specialization type.
static bool hasTemplateSpecializationInEncodedString(const Type *T,
                                                     bool VisitBasesAndFields) {
  T = T->getBaseElementTypeUnsafe();

  if (auto *PT = T->getAs<PointerType>())
    return hasTemplateSpecializationInEncodedString(
        PT->getPointeeType().getTypePtr(), false);

  auto *CXXRD = T->getAsCXXRecordDecl();

  if (!CXXRD)
    return false;

  if (isa<ClassTemplateSpecializationDecl>(CXXRD))
    return true;

  if (!CXXRD->hasDefinition() || !VisitBasesAndFields)
    return false;

  for (const auto &B : CXXRD->bases())
    if (hasTemplateSpecializationInEncodedString(B.getType().getTypePtr(),
                                                 true))
      return true;

  for (auto *FD : CXXRD->fields())
    if (hasTemplateSpecializationInEncodedString(FD->getType().getTypePtr(),
                                                 true))
      return true;

  return false;
}

// FIXME: Use SmallString for accumulating string.
void ASTContext::getObjCEncodingForTypeImpl(QualType T, std::string &S,
                                            const ObjCEncOptions Options,
                                            const FieldDecl *FD,
                                            QualType *NotEncodedT) const {
  CanQualType CT = getCanonicalType(T);
  switch (CT->getTypeClass()) {
  case Type::Builtin:
  case Type::Enum:
    if (FD && FD->isBitField())
      return EncodeBitField(this, S, T, FD);
    if (const auto *BT = dyn_cast<BuiltinType>(CT))
      S += getObjCEncodingForPrimitiveType(this, BT);
    else
      S += ObjCEncodingForEnumType(this, cast<EnumType>(CT));
    return;

  case Type::Complex:
    S += 'j';
    getObjCEncodingForTypeImpl(T->castAs<ComplexType>()->getElementType(), S,
                               ObjCEncOptions(),
                               /*Field=*/nullptr);
    return;

  case Type::Atomic:
    S += 'A';
    getObjCEncodingForTypeImpl(T->castAs<AtomicType>()->getValueType(), S,
                               ObjCEncOptions(),
                               /*Field=*/nullptr);
    return;

  // encoding for pointer or reference types.
  case Type::Pointer:
  case Type::LValueReference:
  case Type::RValueReference: {
    QualType PointeeTy;
    if (isa<PointerType>(CT)) {
      const auto *PT = T->castAs<PointerType>();
      if (PT->isObjCSelType()) {
        S += ':';
        return;
      }
      PointeeTy = PT->getPointeeType();
    } else {
      PointeeTy = T->castAs<ReferenceType>()->getPointeeType();
    }

    bool isReadOnly = false;
    // For historical/compatibility reasons, the read-only qualifier of the
    // pointee gets emitted _before_ the '^'.  The read-only qualifier of
    // the pointer itself gets ignored, _unless_ we are looking at a typedef!
    // Also, do not emit the 'r' for anything but the outermost type!
    if (T->getAs<TypedefType>()) {
      if (Options.IsOutermostType() && T.isConstQualified()) {
        isReadOnly = true;
        S += 'r';
      }
    } else if (Options.IsOutermostType()) {
      QualType P = PointeeTy;
      while (auto PT = P->getAs<PointerType>())
        P = PT->getPointeeType();
      if (P.isConstQualified()) {
        isReadOnly = true;
        S += 'r';
      }
    }
    if (isReadOnly) {
      // Another legacy compatibility encoding. Some ObjC qualifier and type
      // combinations need to be rearranged.
      // Rewrite "in const" from "nr" to "rn"
      if (StringRef(S).ends_with("nr"))
        S.replace(S.end()-2, S.end(), "rn");
    }

    if (PointeeTy->isCharType()) {
      // char pointer types should be encoded as '*' unless it is a
      // type that has been typedef'd to 'BOOL'.
      if (!isTypeTypedefedAsBOOL(PointeeTy)) {
        S += '*';
        return;
      }
    } else if (const auto *RTy = PointeeTy->getAs<RecordType>()) {
      // GCC binary compat: Need to convert "struct objc_class *" to "#".
      if (RTy->getDecl()->getIdentifier() == &Idents.get("objc_class")) {
        S += '#';
        return;
      }
      // GCC binary compat: Need to convert "struct objc_object *" to "@".
      if (RTy->getDecl()->getIdentifier() == &Idents.get("objc_object")) {
        S += '@';
        return;
      }
      // If the encoded string for the class includes template names, just emit
      // "^v" for pointers to the class.
      if (getLangOpts().CPlusPlus &&
          (!getLangOpts().EncodeCXXClassTemplateSpec &&
           hasTemplateSpecializationInEncodedString(
               RTy, Options.ExpandPointedToStructures()))) {
        S += "^v";
        return;
      }
      // fall through...
    }
    S += '^';
    getLegacyIntegralTypeEncoding(PointeeTy);

    ObjCEncOptions NewOptions;
    if (Options.ExpandPointedToStructures())
      NewOptions.setExpandStructures();
    getObjCEncodingForTypeImpl(PointeeTy, S, NewOptions,
                               /*Field=*/nullptr, NotEncodedT);
    return;
  }

  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray: {
    const auto *AT = cast<ArrayType>(CT);

    if (isa<IncompleteArrayType>(AT) && !Options.IsStructField()) {
      // Incomplete arrays are encoded as a pointer to the array element.
      S += '^';

      getObjCEncodingForTypeImpl(
          AT->getElementType(), S,
          Options.keepingOnly(ObjCEncOptions().setExpandStructures()), FD);
    } else {
      S += '[';

      if (const auto *CAT = dyn_cast<ConstantArrayType>(AT))
        S += llvm::utostr(CAT->getZExtSize());
      else {
        //Variable length arrays are encoded as a regular array with 0 elements.
        assert((isa<VariableArrayType>(AT) || isa<IncompleteArrayType>(AT)) &&
               "Unknown array type!");
        S += '0';
      }

      getObjCEncodingForTypeImpl(
          AT->getElementType(), S,
          Options.keepingOnly(ObjCEncOptions().setExpandStructures()), FD,
          NotEncodedT);
      S += ']';
    }
    return;
  }

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    S += '?';
    return;

  case Type::Record: {
    RecordDecl *RDecl = cast<RecordType>(CT)->getDecl();
    S += RDecl->isUnion() ? '(' : '{';
    // Anonymous structures print as '?'
    if (const IdentifierInfo *II = RDecl->getIdentifier()) {
      S += II->getName();
      if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(RDecl)) {
        const TemplateArgumentList &TemplateArgs = Spec->getTemplateArgs();
        llvm::raw_string_ostream OS(S);
        printTemplateArgumentList(OS, TemplateArgs.asArray(),
                                  getPrintingPolicy());
      }
    } else {
      S += '?';
    }
    if (Options.ExpandStructures()) {
      S += '=';
      if (!RDecl->isUnion()) {
        getObjCEncodingForStructureImpl(RDecl, S, FD, true, NotEncodedT);
      } else {
        for (const auto *Field : RDecl->fields()) {
          if (FD) {
            S += '"';
            S += Field->getNameAsString();
            S += '"';
          }

          // Special case bit-fields.
          if (Field->isBitField()) {
            getObjCEncodingForTypeImpl(Field->getType(), S,
                                       ObjCEncOptions().setExpandStructures(),
                                       Field);
          } else {
            QualType qt = Field->getType();
            getLegacyIntegralTypeEncoding(qt);
            getObjCEncodingForTypeImpl(
                qt, S,
                ObjCEncOptions().setExpandStructures().setIsStructField(), FD,
                NotEncodedT);
          }
        }
      }
    }
    S += RDecl->isUnion() ? ')' : '}';
    return;
  }

  case Type::BlockPointer: {
    const auto *BT = T->castAs<BlockPointerType>();
    S += "@?"; // Unlike a pointer-to-function, which is "^?".
    if (Options.EncodeBlockParameters()) {
      const auto *FT = BT->getPointeeType()->castAs<FunctionType>();

      S += '<';
      // Block return type
      getObjCEncodingForTypeImpl(FT->getReturnType(), S,
                                 Options.forComponentType(), FD, NotEncodedT);
      // Block self
      S += "@?";
      // Block parameters
      if (const auto *FPT = dyn_cast<FunctionProtoType>(FT)) {
        for (const auto &I : FPT->param_types())
          getObjCEncodingForTypeImpl(I, S, Options.forComponentType(), FD,
                                     NotEncodedT);
      }
      S += '>';
    }
    return;
  }

  case Type::ObjCObject: {
    // hack to match legacy encoding of *id and *Class
    QualType Ty = getObjCObjectPointerType(CT);
    if (Ty->isObjCIdType()) {
      S += "{objc_object=}";
      return;
    }
    else if (Ty->isObjCClassType()) {
      S += "{objc_class=}";
      return;
    }
    // TODO: Double check to make sure this intentionally falls through.
    [[fallthrough]];
  }

  case Type::ObjCInterface: {
    // Ignore protocol qualifiers when mangling at this level.
    // @encode(class_name)
    ObjCInterfaceDecl *OI = T->castAs<ObjCObjectType>()->getInterface();
    S += '{';
    S += OI->getObjCRuntimeNameAsString();
    if (Options.ExpandStructures()) {
      S += '=';
      SmallVector<const ObjCIvarDecl*, 32> Ivars;
      DeepCollectObjCIvars(OI, true, Ivars);
      for (unsigned i = 0, e = Ivars.size(); i != e; ++i) {
        const FieldDecl *Field = Ivars[i];
        if (Field->isBitField())
          getObjCEncodingForTypeImpl(Field->getType(), S,
                                     ObjCEncOptions().setExpandStructures(),
                                     Field);
        else
          getObjCEncodingForTypeImpl(Field->getType(), S,
                                     ObjCEncOptions().setExpandStructures(), FD,
                                     NotEncodedT);
      }
    }
    S += '}';
    return;
  }

  case Type::ObjCObjectPointer: {
    const auto *OPT = T->castAs<ObjCObjectPointerType>();
    if (OPT->isObjCIdType()) {
      S += '@';
      return;
    }

    if (OPT->isObjCClassType() || OPT->isObjCQualifiedClassType()) {
      // FIXME: Consider if we need to output qualifiers for 'Class<p>'.
      // Since this is a binary compatibility issue, need to consult with
      // runtime folks. Fortunately, this is a *very* obscure construct.
      S += '#';
      return;
    }

    if (OPT->isObjCQualifiedIdType()) {
      getObjCEncodingForTypeImpl(
          getObjCIdType(), S,
          Options.keepingOnly(ObjCEncOptions()
                                  .setExpandPointedToStructures()
                                  .setExpandStructures()),
          FD);
      if (FD || Options.EncodingProperty() || Options.EncodeClassNames()) {
        // Note that we do extended encoding of protocol qualifier list
        // Only when doing ivar or property encoding.
        S += '"';
        for (const auto *I : OPT->quals()) {
          S += '<';
          S += I->getObjCRuntimeNameAsString();
          S += '>';
        }
        S += '"';
      }
      return;
    }

    S += '@';
    if (OPT->getInterfaceDecl() &&
        (FD || Options.EncodingProperty() || Options.EncodeClassNames())) {
      S += '"';
      S += OPT->getInterfaceDecl()->getObjCRuntimeNameAsString();
      for (const auto *I : OPT->quals()) {
        S += '<';
        S += I->getObjCRuntimeNameAsString();
        S += '>';
      }
      S += '"';
    }
    return;
  }

  // gcc just blithely ignores member pointers.
  // FIXME: we should do better than that.  'M' is available.
  case Type::MemberPointer:
  // This matches gcc's encoding, even though technically it is insufficient.
  //FIXME. We should do a better job than gcc.
  case Type::Vector:
  case Type::ExtVector:
  // Until we have a coherent encoding of these three types, issue warning.
    if (NotEncodedT)
      *NotEncodedT = T;
    return;

  case Type::ConstantMatrix:
    if (NotEncodedT)
      *NotEncodedT = T;
    return;

  case Type::BitInt:
    if (NotEncodedT)
      *NotEncodedT = T;
    return;

  // We could see an undeduced auto type here during error recovery.
  // Just ignore it.
  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
    return;

  case Type::ArrayParameter:
  case Type::Pipe:
#define ABSTRACT_TYPE(KIND, BASE)
#define TYPE(KIND, BASE)
#define DEPENDENT_TYPE(KIND, BASE) \
  case Type::KIND:
#define NON_CANONICAL_TYPE(KIND, BASE) \
  case Type::KIND:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(KIND, BASE) \
  case Type::KIND:
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("@encode for dependent type!");
  }
  llvm_unreachable("bad type kind!");
}

void ASTContext::getObjCEncodingForStructureImpl(RecordDecl *RDecl,
                                                 std::string &S,
                                                 const FieldDecl *FD,
                                                 bool includeVBases,
                                                 QualType *NotEncodedT) const {
  assert(RDecl && "Expected non-null RecordDecl");
  assert(!RDecl->isUnion() && "Should not be called for unions");
  if (!RDecl->getDefinition() || RDecl->getDefinition()->isInvalidDecl())
    return;

  const auto *CXXRec = dyn_cast<CXXRecordDecl>(RDecl);
  std::multimap<uint64_t, NamedDecl *> FieldOrBaseOffsets;
  const ASTRecordLayout &layout = getASTRecordLayout(RDecl);

  if (CXXRec) {
    for (const auto &BI : CXXRec->bases()) {
      if (!BI.isVirtual()) {
        CXXRecordDecl *base = BI.getType()->getAsCXXRecordDecl();
        if (base->isEmpty())
          continue;
        uint64_t offs = toBits(layout.getBaseClassOffset(base));
        FieldOrBaseOffsets.insert(FieldOrBaseOffsets.upper_bound(offs),
                                  std::make_pair(offs, base));
      }
    }
  }

  for (FieldDecl *Field : RDecl->fields()) {
    if (!Field->isZeroLengthBitField(*this) && Field->isZeroSize(*this))
      continue;
    uint64_t offs = layout.getFieldOffset(Field->getFieldIndex());
    FieldOrBaseOffsets.insert(FieldOrBaseOffsets.upper_bound(offs),
                              std::make_pair(offs, Field));
  }

  if (CXXRec && includeVBases) {
    for (const auto &BI : CXXRec->vbases()) {
      CXXRecordDecl *base = BI.getType()->getAsCXXRecordDecl();
      if (base->isEmpty())
        continue;
      uint64_t offs = toBits(layout.getVBaseClassOffset(base));
      if (offs >= uint64_t(toBits(layout.getNonVirtualSize())) &&
          FieldOrBaseOffsets.find(offs) == FieldOrBaseOffsets.end())
        FieldOrBaseOffsets.insert(FieldOrBaseOffsets.end(),
                                  std::make_pair(offs, base));
    }
  }

  CharUnits size;
  if (CXXRec) {
    size = includeVBases ? layout.getSize() : layout.getNonVirtualSize();
  } else {
    size = layout.getSize();
  }

#ifndef NDEBUG
  uint64_t CurOffs = 0;
#endif
  std::multimap<uint64_t, NamedDecl *>::iterator
    CurLayObj = FieldOrBaseOffsets.begin();

  if (CXXRec && CXXRec->isDynamicClass() &&
      (CurLayObj == FieldOrBaseOffsets.end() || CurLayObj->first != 0)) {
    if (FD) {
      S += "\"_vptr$";
      std::string recname = CXXRec->getNameAsString();
      if (recname.empty()) recname = "?";
      S += recname;
      S += '"';
    }
    S += "^^?";
#ifndef NDEBUG
    CurOffs += getTypeSize(VoidPtrTy);
#endif
  }

  if (!RDecl->hasFlexibleArrayMember()) {
    // Mark the end of the structure.
    uint64_t offs = toBits(size);
    FieldOrBaseOffsets.insert(FieldOrBaseOffsets.upper_bound(offs),
                              std::make_pair(offs, nullptr));
  }

  for (; CurLayObj != FieldOrBaseOffsets.end(); ++CurLayObj) {
#ifndef NDEBUG
    assert(CurOffs <= CurLayObj->first);
    if (CurOffs < CurLayObj->first) {
      uint64_t padding = CurLayObj->first - CurOffs;
      // FIXME: There doesn't seem to be a way to indicate in the encoding that
      // packing/alignment of members is different that normal, in which case
      // the encoding will be out-of-sync with the real layout.
      // If the runtime switches to just consider the size of types without
      // taking into account alignment, we could make padding explicit in the
      // encoding (e.g. using arrays of chars). The encoding strings would be
      // longer then though.
      CurOffs += padding;
    }
#endif

    NamedDecl *dcl = CurLayObj->second;
    if (!dcl)
      break; // reached end of structure.

    if (auto *base = dyn_cast<CXXRecordDecl>(dcl)) {
      // We expand the bases without their virtual bases since those are going
      // in the initial structure. Note that this differs from gcc which
      // expands virtual bases each time one is encountered in the hierarchy,
      // making the encoding type bigger than it really is.
      getObjCEncodingForStructureImpl(base, S, FD, /*includeVBases*/false,
                                      NotEncodedT);
      assert(!base->isEmpty());
#ifndef NDEBUG
      CurOffs += toBits(getASTRecordLayout(base).getNonVirtualSize());
#endif
    } else {
      const auto *field = cast<FieldDecl>(dcl);
      if (FD) {
        S += '"';
        S += field->getNameAsString();
        S += '"';
      }

      if (field->isBitField()) {
        EncodeBitField(this, S, field->getType(), field);
#ifndef NDEBUG
        CurOffs += field->getBitWidthValue(*this);
#endif
      } else {
        QualType qt = field->getType();
        getLegacyIntegralTypeEncoding(qt);
        getObjCEncodingForTypeImpl(
            qt, S, ObjCEncOptions().setExpandStructures().setIsStructField(),
            FD, NotEncodedT);
#ifndef NDEBUG
        CurOffs += getTypeSize(field->getType());
#endif
      }
    }
  }
}

void ASTContext::getObjCEncodingForTypeQualifier(Decl::ObjCDeclQualifier QT,
                                                 std::string& S) const {
  if (QT & Decl::OBJC_TQ_In)
    S += 'n';
  if (QT & Decl::OBJC_TQ_Inout)
    S += 'N';
  if (QT & Decl::OBJC_TQ_Out)
    S += 'o';
  if (QT & Decl::OBJC_TQ_Bycopy)
    S += 'O';
  if (QT & Decl::OBJC_TQ_Byref)
    S += 'R';
  if (QT & Decl::OBJC_TQ_Oneway)
    S += 'V';
}

TypedefDecl *ASTContext::getObjCIdDecl() const {
  if (!ObjCIdDecl) {
    QualType T = getObjCObjectType(ObjCBuiltinIdTy, {}, {});
    T = getObjCObjectPointerType(T);
    ObjCIdDecl = buildImplicitTypedef(T, "id");
  }
  return ObjCIdDecl;
}

TypedefDecl *ASTContext::getObjCSelDecl() const {
  if (!ObjCSelDecl) {
    QualType T = getPointerType(ObjCBuiltinSelTy);
    ObjCSelDecl = buildImplicitTypedef(T, "SEL");
  }
  return ObjCSelDecl;
}

TypedefDecl *ASTContext::getObjCClassDecl() const {
  if (!ObjCClassDecl) {
    QualType T = getObjCObjectType(ObjCBuiltinClassTy, {}, {});
    T = getObjCObjectPointerType(T);
    ObjCClassDecl = buildImplicitTypedef(T, "Class");
  }
  return ObjCClassDecl;
}

ObjCInterfaceDecl *ASTContext::getObjCProtocolDecl() const {
  if (!ObjCProtocolClassDecl) {
    ObjCProtocolClassDecl
      = ObjCInterfaceDecl::Create(*this, getTranslationUnitDecl(),
                                  SourceLocation(),
                                  &Idents.get("Protocol"),
                                  /*typeParamList=*/nullptr,
                                  /*PrevDecl=*/nullptr,
                                  SourceLocation(), true);
  }

  return ObjCProtocolClassDecl;
}

//===----------------------------------------------------------------------===//
// __builtin_va_list Construction Functions
//===----------------------------------------------------------------------===//

static TypedefDecl *CreateCharPtrNamedVaListDecl(const ASTContext *Context,
                                                 StringRef Name) {
  // typedef char* __builtin[_ms]_va_list;
  QualType T = Context->getPointerType(Context->CharTy);
  return Context->buildImplicitTypedef(T, Name);
}

static TypedefDecl *CreateMSVaListDecl(const ASTContext *Context) {
  return CreateCharPtrNamedVaListDecl(Context, "__builtin_ms_va_list");
}

static TypedefDecl *CreateCharPtrBuiltinVaListDecl(const ASTContext *Context) {
  return CreateCharPtrNamedVaListDecl(Context, "__builtin_va_list");
}

static TypedefDecl *CreateVoidPtrBuiltinVaListDecl(const ASTContext *Context) {
  // typedef void* __builtin_va_list;
  QualType T = Context->getPointerType(Context->VoidTy);
  return Context->buildImplicitTypedef(T, "__builtin_va_list");
}

static TypedefDecl *
CreateAArch64ABIBuiltinVaListDecl(const ASTContext *Context) {
  // struct __va_list
  RecordDecl *VaListTagDecl = Context->buildImplicitRecord("__va_list");
  if (Context->getLangOpts().CPlusPlus) {
    // namespace std { struct __va_list {
    auto *NS = NamespaceDecl::Create(
        const_cast<ASTContext &>(*Context), Context->getTranslationUnitDecl(),
        /*Inline=*/false, SourceLocation(), SourceLocation(),
        &Context->Idents.get("std"),
        /*PrevDecl=*/nullptr, /*Nested=*/false);
    NS->setImplicit();
    VaListTagDecl->setDeclContext(NS);
  }

  VaListTagDecl->startDefinition();

  const size_t NumFields = 5;
  QualType FieldTypes[NumFields];
  const char *FieldNames[NumFields];

  // void *__stack;
  FieldTypes[0] = Context->getPointerType(Context->VoidTy);
  FieldNames[0] = "__stack";

  // void *__gr_top;
  FieldTypes[1] = Context->getPointerType(Context->VoidTy);
  FieldNames[1] = "__gr_top";

  // void *__vr_top;
  FieldTypes[2] = Context->getPointerType(Context->VoidTy);
  FieldNames[2] = "__vr_top";

  // int __gr_offs;
  FieldTypes[3] = Context->IntTy;
  FieldNames[3] = "__gr_offs";

  // int __vr_offs;
  FieldTypes[4] = Context->IntTy;
  FieldNames[4] = "__vr_offs";

  // Create fields
  for (unsigned i = 0; i < NumFields; ++i) {
    FieldDecl *Field = FieldDecl::Create(const_cast<ASTContext &>(*Context),
                                         VaListTagDecl,
                                         SourceLocation(),
                                         SourceLocation(),
                                         &Context->Idents.get(FieldNames[i]),
                                         FieldTypes[i], /*TInfo=*/nullptr,
                                         /*BitWidth=*/nullptr,
                                         /*Mutable=*/false,
                                         ICIS_NoInit);
    Field->setAccess(AS_public);
    VaListTagDecl->addDecl(Field);
  }
  VaListTagDecl->completeDefinition();
  Context->VaListTagDecl = VaListTagDecl;
  QualType VaListTagType = Context->getRecordType(VaListTagDecl);

  // } __builtin_va_list;
  return Context->buildImplicitTypedef(VaListTagType, "__builtin_va_list");
}

static TypedefDecl *CreatePowerABIBuiltinVaListDecl(const ASTContext *Context) {
  // typedef struct __va_list_tag {
  RecordDecl *VaListTagDecl;

  VaListTagDecl = Context->buildImplicitRecord("__va_list_tag");
  VaListTagDecl->startDefinition();

  const size_t NumFields = 5;
  QualType FieldTypes[NumFields];
  const char *FieldNames[NumFields];

  //   unsigned char gpr;
  FieldTypes[0] = Context->UnsignedCharTy;
  FieldNames[0] = "gpr";

  //   unsigned char fpr;
  FieldTypes[1] = Context->UnsignedCharTy;
  FieldNames[1] = "fpr";

  //   unsigned short reserved;
  FieldTypes[2] = Context->UnsignedShortTy;
  FieldNames[2] = "reserved";

  //   void* overflow_arg_area;
  FieldTypes[3] = Context->getPointerType(Context->VoidTy);
  FieldNames[3] = "overflow_arg_area";

  //   void* reg_save_area;
  FieldTypes[4] = Context->getPointerType(Context->VoidTy);
  FieldNames[4] = "reg_save_area";

  // Create fields
  for (unsigned i = 0; i < NumFields; ++i) {
    FieldDecl *Field = FieldDecl::Create(*Context, VaListTagDecl,
                                         SourceLocation(),
                                         SourceLocation(),
                                         &Context->Idents.get(FieldNames[i]),
                                         FieldTypes[i], /*TInfo=*/nullptr,
                                         /*BitWidth=*/nullptr,
                                         /*Mutable=*/false,
                                         ICIS_NoInit);
    Field->setAccess(AS_public);
    VaListTagDecl->addDecl(Field);
  }
  VaListTagDecl->completeDefinition();
  Context->VaListTagDecl = VaListTagDecl;
  QualType VaListTagType = Context->getRecordType(VaListTagDecl);

  // } __va_list_tag;
  TypedefDecl *VaListTagTypedefDecl =
      Context->buildImplicitTypedef(VaListTagType, "__va_list_tag");

  QualType VaListTagTypedefType =
    Context->getTypedefType(VaListTagTypedefDecl);

  // typedef __va_list_tag __builtin_va_list[1];
  llvm::APInt Size(Context->getTypeSize(Context->getSizeType()), 1);
  QualType VaListTagArrayType = Context->getConstantArrayType(
      VaListTagTypedefType, Size, nullptr, ArraySizeModifier::Normal, 0);
  return Context->buildImplicitTypedef(VaListTagArrayType, "__builtin_va_list");
}

static TypedefDecl *
CreateX86_64ABIBuiltinVaListDecl(const ASTContext *Context) {
  // struct __va_list_tag {
  RecordDecl *VaListTagDecl;
  VaListTagDecl = Context->buildImplicitRecord("__va_list_tag");
  VaListTagDecl->startDefinition();

  const size_t NumFields = 4;
  QualType FieldTypes[NumFields];
  const char *FieldNames[NumFields];

  //   unsigned gp_offset;
  FieldTypes[0] = Context->UnsignedIntTy;
  FieldNames[0] = "gp_offset";

  //   unsigned fp_offset;
  FieldTypes[1] = Context->UnsignedIntTy;
  FieldNames[1] = "fp_offset";

  //   void* overflow_arg_area;
  FieldTypes[2] = Context->getPointerType(Context->VoidTy);
  FieldNames[2] = "overflow_arg_area";

  //   void* reg_save_area;
  FieldTypes[3] = Context->getPointerType(Context->VoidTy);
  FieldNames[3] = "reg_save_area";

  // Create fields
  for (unsigned i = 0; i < NumFields; ++i) {
    FieldDecl *Field = FieldDecl::Create(const_cast<ASTContext &>(*Context),
                                         VaListTagDecl,
                                         SourceLocation(),
                                         SourceLocation(),
                                         &Context->Idents.get(FieldNames[i]),
                                         FieldTypes[i], /*TInfo=*/nullptr,
                                         /*BitWidth=*/nullptr,
                                         /*Mutable=*/false,
                                         ICIS_NoInit);
    Field->setAccess(AS_public);
    VaListTagDecl->addDecl(Field);
  }
  VaListTagDecl->completeDefinition();
  Context->VaListTagDecl = VaListTagDecl;
  QualType VaListTagType = Context->getRecordType(VaListTagDecl);

  // };

  // typedef struct __va_list_tag __builtin_va_list[1];
  llvm::APInt Size(Context->getTypeSize(Context->getSizeType()), 1);
  QualType VaListTagArrayType = Context->getConstantArrayType(
      VaListTagType, Size, nullptr, ArraySizeModifier::Normal, 0);
  return Context->buildImplicitTypedef(VaListTagArrayType, "__builtin_va_list");
}

static TypedefDecl *CreatePNaClABIBuiltinVaListDecl(const ASTContext *Context) {
  // typedef int __builtin_va_list[4];
  llvm::APInt Size(Context->getTypeSize(Context->getSizeType()), 4);
  QualType IntArrayType = Context->getConstantArrayType(
      Context->IntTy, Size, nullptr, ArraySizeModifier::Normal, 0);
  return Context->buildImplicitTypedef(IntArrayType, "__builtin_va_list");
}

static TypedefDecl *
CreateAAPCSABIBuiltinVaListDecl(const ASTContext *Context) {
  // struct __va_list
  RecordDecl *VaListDecl = Context->buildImplicitRecord("__va_list");
  if (Context->getLangOpts().CPlusPlus) {
    // namespace std { struct __va_list {
    NamespaceDecl *NS;
    NS = NamespaceDecl::Create(const_cast<ASTContext &>(*Context),
                               Context->getTranslationUnitDecl(),
                               /*Inline=*/false, SourceLocation(),
                               SourceLocation(), &Context->Idents.get("std"),
                               /*PrevDecl=*/nullptr, /*Nested=*/false);
    NS->setImplicit();
    VaListDecl->setDeclContext(NS);
  }

  VaListDecl->startDefinition();

  // void * __ap;
  FieldDecl *Field = FieldDecl::Create(const_cast<ASTContext &>(*Context),
                                       VaListDecl,
                                       SourceLocation(),
                                       SourceLocation(),
                                       &Context->Idents.get("__ap"),
                                       Context->getPointerType(Context->VoidTy),
                                       /*TInfo=*/nullptr,
                                       /*BitWidth=*/nullptr,
                                       /*Mutable=*/false,
                                       ICIS_NoInit);
  Field->setAccess(AS_public);
  VaListDecl->addDecl(Field);

  // };
  VaListDecl->completeDefinition();
  Context->VaListTagDecl = VaListDecl;

  // typedef struct __va_list __builtin_va_list;
  QualType T = Context->getRecordType(VaListDecl);
  return Context->buildImplicitTypedef(T, "__builtin_va_list");
}

static TypedefDecl *
CreateSystemZBuiltinVaListDecl(const ASTContext *Context) {
  // struct __va_list_tag {
  RecordDecl *VaListTagDecl;
  VaListTagDecl = Context->buildImplicitRecord("__va_list_tag");
  VaListTagDecl->startDefinition();

  const size_t NumFields = 4;
  QualType FieldTypes[NumFields];
  const char *FieldNames[NumFields];

  //   long __gpr;
  FieldTypes[0] = Context->LongTy;
  FieldNames[0] = "__gpr";

  //   long __fpr;
  FieldTypes[1] = Context->LongTy;
  FieldNames[1] = "__fpr";

  //   void *__overflow_arg_area;
  FieldTypes[2] = Context->getPointerType(Context->VoidTy);
  FieldNames[2] = "__overflow_arg_area";

  //   void *__reg_save_area;
  FieldTypes[3] = Context->getPointerType(Context->VoidTy);
  FieldNames[3] = "__reg_save_area";

  // Create fields
  for (unsigned i = 0; i < NumFields; ++i) {
    FieldDecl *Field = FieldDecl::Create(const_cast<ASTContext &>(*Context),
                                         VaListTagDecl,
                                         SourceLocation(),
                                         SourceLocation(),
                                         &Context->Idents.get(FieldNames[i]),
                                         FieldTypes[i], /*TInfo=*/nullptr,
                                         /*BitWidth=*/nullptr,
                                         /*Mutable=*/false,
                                         ICIS_NoInit);
    Field->setAccess(AS_public);
    VaListTagDecl->addDecl(Field);
  }
  VaListTagDecl->completeDefinition();
  Context->VaListTagDecl = VaListTagDecl;
  QualType VaListTagType = Context->getRecordType(VaListTagDecl);

  // };

  // typedef __va_list_tag __builtin_va_list[1];
  llvm::APInt Size(Context->getTypeSize(Context->getSizeType()), 1);
  QualType VaListTagArrayType = Context->getConstantArrayType(
      VaListTagType, Size, nullptr, ArraySizeModifier::Normal, 0);

  return Context->buildImplicitTypedef(VaListTagArrayType, "__builtin_va_list");
}

static TypedefDecl *CreateHexagonBuiltinVaListDecl(const ASTContext *Context) {
  // typedef struct __va_list_tag {
  RecordDecl *VaListTagDecl;
  VaListTagDecl = Context->buildImplicitRecord("__va_list_tag");
  VaListTagDecl->startDefinition();

  const size_t NumFields = 3;
  QualType FieldTypes[NumFields];
  const char *FieldNames[NumFields];

  //   void *CurrentSavedRegisterArea;
  FieldTypes[0] = Context->getPointerType(Context->VoidTy);
  FieldNames[0] = "__current_saved_reg_area_pointer";

  //   void *SavedRegAreaEnd;
  FieldTypes[1] = Context->getPointerType(Context->VoidTy);
  FieldNames[1] = "__saved_reg_area_end_pointer";

  //   void *OverflowArea;
  FieldTypes[2] = Context->getPointerType(Context->VoidTy);
  FieldNames[2] = "__overflow_area_pointer";

  // Create fields
  for (unsigned i = 0; i < NumFields; ++i) {
    FieldDecl *Field = FieldDecl::Create(
        const_cast<ASTContext &>(*Context), VaListTagDecl, SourceLocation(),
        SourceLocation(), &Context->Idents.get(FieldNames[i]), FieldTypes[i],
        /*TInfo=*/nullptr,
        /*BitWidth=*/nullptr,
        /*Mutable=*/false, ICIS_NoInit);
    Field->setAccess(AS_public);
    VaListTagDecl->addDecl(Field);
  }
  VaListTagDecl->completeDefinition();
  Context->VaListTagDecl = VaListTagDecl;
  QualType VaListTagType = Context->getRecordType(VaListTagDecl);

  // } __va_list_tag;
  TypedefDecl *VaListTagTypedefDecl =
      Context->buildImplicitTypedef(VaListTagType, "__va_list_tag");

  QualType VaListTagTypedefType = Context->getTypedefType(VaListTagTypedefDecl);

  // typedef __va_list_tag __builtin_va_list[1];
  llvm::APInt Size(Context->getTypeSize(Context->getSizeType()), 1);
  QualType VaListTagArrayType = Context->getConstantArrayType(
      VaListTagTypedefType, Size, nullptr, ArraySizeModifier::Normal, 0);

  return Context->buildImplicitTypedef(VaListTagArrayType, "__builtin_va_list");
}

static TypedefDecl *CreateVaListDecl(const ASTContext *Context,
                                     TargetInfo::BuiltinVaListKind Kind) {
  switch (Kind) {
  case TargetInfo::CharPtrBuiltinVaList:
    return CreateCharPtrBuiltinVaListDecl(Context);
  case TargetInfo::VoidPtrBuiltinVaList:
    return CreateVoidPtrBuiltinVaListDecl(Context);
  case TargetInfo::AArch64ABIBuiltinVaList:
    return CreateAArch64ABIBuiltinVaListDecl(Context);
  case TargetInfo::PowerABIBuiltinVaList:
    return CreatePowerABIBuiltinVaListDecl(Context);
  case TargetInfo::X86_64ABIBuiltinVaList:
    return CreateX86_64ABIBuiltinVaListDecl(Context);
  case TargetInfo::PNaClABIBuiltinVaList:
    return CreatePNaClABIBuiltinVaListDecl(Context);
  case TargetInfo::AAPCSABIBuiltinVaList:
    return CreateAAPCSABIBuiltinVaListDecl(Context);
  case TargetInfo::SystemZBuiltinVaList:
    return CreateSystemZBuiltinVaListDecl(Context);
  case TargetInfo::HexagonBuiltinVaList:
    return CreateHexagonBuiltinVaListDecl(Context);
  }

  llvm_unreachable("Unhandled __builtin_va_list type kind");
}

TypedefDecl *ASTContext::getBuiltinVaListDecl() const {
  if (!BuiltinVaListDecl) {
    BuiltinVaListDecl = CreateVaListDecl(this, Target->getBuiltinVaListKind());
    assert(BuiltinVaListDecl->isImplicit());
  }

  return BuiltinVaListDecl;
}

Decl *ASTContext::getVaListTagDecl() const {
  // Force the creation of VaListTagDecl by building the __builtin_va_list
  // declaration.
  if (!VaListTagDecl)
    (void)getBuiltinVaListDecl();

  return VaListTagDecl;
}

TypedefDecl *ASTContext::getBuiltinMSVaListDecl() const {
  if (!BuiltinMSVaListDecl)
    BuiltinMSVaListDecl = CreateMSVaListDecl(this);

  return BuiltinMSVaListDecl;
}

bool ASTContext::canBuiltinBeRedeclared(const FunctionDecl *FD) const {
  // Allow redecl custom type checking builtin for HLSL.
  if (LangOpts.HLSL && FD->getBuiltinID() != Builtin::NotBuiltin &&
      BuiltinInfo.hasCustomTypechecking(FD->getBuiltinID()))
    return true;
  return BuiltinInfo.canBeRedeclared(FD->getBuiltinID());
}

void ASTContext::setObjCConstantStringInterface(ObjCInterfaceDecl *Decl) {
  assert(ObjCConstantStringType.isNull() &&
         "'NSConstantString' type already set!");

  ObjCConstantStringType = getObjCInterfaceType(Decl);
}

/// Retrieve the template name that corresponds to a non-empty
/// lookup.
TemplateName
ASTContext::getOverloadedTemplateName(UnresolvedSetIterator Begin,
                                      UnresolvedSetIterator End) const {
  unsigned size = End - Begin;
  assert(size > 1 && "set is not overloaded!");

  void *memory = Allocate(sizeof(OverloadedTemplateStorage) +
                          size * sizeof(FunctionTemplateDecl*));
  auto *OT = new (memory) OverloadedTemplateStorage(size);

  NamedDecl **Storage = OT->getStorage();
  for (UnresolvedSetIterator I = Begin; I != End; ++I) {
    NamedDecl *D = *I;
    assert(isa<FunctionTemplateDecl>(D) ||
           isa<UnresolvedUsingValueDecl>(D) ||
           (isa<UsingShadowDecl>(D) &&
            isa<FunctionTemplateDecl>(D->getUnderlyingDecl())));
    *Storage++ = D;
  }

  return TemplateName(OT);
}

/// Retrieve a template name representing an unqualified-id that has been
/// assumed to name a template for ADL purposes.
TemplateName ASTContext::getAssumedTemplateName(DeclarationName Name) const {
  auto *OT = new (*this) AssumedTemplateStorage(Name);
  return TemplateName(OT);
}

/// Retrieve the template name that represents a qualified
/// template name such as \c std::vector.
TemplateName ASTContext::getQualifiedTemplateName(NestedNameSpecifier *NNS,
                                                  bool TemplateKeyword,
                                                  TemplateName Template) const {
  assert(Template.getKind() == TemplateName::Template ||
         Template.getKind() == TemplateName::UsingTemplate);

  // FIXME: Canonicalization?
  llvm::FoldingSetNodeID ID;
  QualifiedTemplateName::Profile(ID, NNS, TemplateKeyword, Template);

  void *InsertPos = nullptr;
  QualifiedTemplateName *QTN =
    QualifiedTemplateNames.FindNodeOrInsertPos(ID, InsertPos);
  if (!QTN) {
    QTN = new (*this, alignof(QualifiedTemplateName))
        QualifiedTemplateName(NNS, TemplateKeyword, Template);
    QualifiedTemplateNames.InsertNode(QTN, InsertPos);
  }

  return TemplateName(QTN);
}

/// Retrieve the template name that represents a dependent
/// template name such as \c MetaFun::template apply.
TemplateName
ASTContext::getDependentTemplateName(NestedNameSpecifier *NNS,
                                     const IdentifierInfo *Name) const {
  assert((!NNS || NNS->isDependent()) &&
         "Nested name specifier must be dependent");

  llvm::FoldingSetNodeID ID;
  DependentTemplateName::Profile(ID, NNS, Name);

  void *InsertPos = nullptr;
  DependentTemplateName *QTN =
    DependentTemplateNames.FindNodeOrInsertPos(ID, InsertPos);

  if (QTN)
    return TemplateName(QTN);

  NestedNameSpecifier *CanonNNS = getCanonicalNestedNameSpecifier(NNS);
  if (CanonNNS == NNS) {
    QTN = new (*this, alignof(DependentTemplateName))
        DependentTemplateName(NNS, Name);
  } else {
    TemplateName Canon = getDependentTemplateName(CanonNNS, Name);
    QTN = new (*this, alignof(DependentTemplateName))
        DependentTemplateName(NNS, Name, Canon);
    DependentTemplateName *CheckQTN =
      DependentTemplateNames.FindNodeOrInsertPos(ID, InsertPos);
    assert(!CheckQTN && "Dependent type name canonicalization broken");
    (void)CheckQTN;
  }

  DependentTemplateNames.InsertNode(QTN, InsertPos);
  return TemplateName(QTN);
}

/// Retrieve the template name that represents a dependent
/// template name such as \c MetaFun::template operator+.
TemplateName
ASTContext::getDependentTemplateName(NestedNameSpecifier *NNS,
                                     OverloadedOperatorKind Operator) const {
  assert((!NNS || NNS->isDependent()) &&
         "Nested name specifier must be dependent");

  llvm::FoldingSetNodeID ID;
  DependentTemplateName::Profile(ID, NNS, Operator);

  void *InsertPos = nullptr;
  DependentTemplateName *QTN
    = DependentTemplateNames.FindNodeOrInsertPos(ID, InsertPos);

  if (QTN)
    return TemplateName(QTN);

  NestedNameSpecifier *CanonNNS = getCanonicalNestedNameSpecifier(NNS);
  if (CanonNNS == NNS) {
    QTN = new (*this, alignof(DependentTemplateName))
        DependentTemplateName(NNS, Operator);
  } else {
    TemplateName Canon = getDependentTemplateName(CanonNNS, Operator);
    QTN = new (*this, alignof(DependentTemplateName))
        DependentTemplateName(NNS, Operator, Canon);

    DependentTemplateName *CheckQTN
      = DependentTemplateNames.FindNodeOrInsertPos(ID, InsertPos);
    assert(!CheckQTN && "Dependent template name canonicalization broken");
    (void)CheckQTN;
  }

  DependentTemplateNames.InsertNode(QTN, InsertPos);
  return TemplateName(QTN);
}

TemplateName ASTContext::getSubstTemplateTemplateParm(
    TemplateName Replacement, Decl *AssociatedDecl, unsigned Index,
    std::optional<unsigned> PackIndex) const {
  llvm::FoldingSetNodeID ID;
  SubstTemplateTemplateParmStorage::Profile(ID, Replacement, AssociatedDecl,
                                            Index, PackIndex);

  void *insertPos = nullptr;
  SubstTemplateTemplateParmStorage *subst
    = SubstTemplateTemplateParms.FindNodeOrInsertPos(ID, insertPos);

  if (!subst) {
    subst = new (*this) SubstTemplateTemplateParmStorage(
        Replacement, AssociatedDecl, Index, PackIndex);
    SubstTemplateTemplateParms.InsertNode(subst, insertPos);
  }

  return TemplateName(subst);
}

TemplateName
ASTContext::getSubstTemplateTemplateParmPack(const TemplateArgument &ArgPack,
                                             Decl *AssociatedDecl,
                                             unsigned Index, bool Final) const {
  auto &Self = const_cast<ASTContext &>(*this);
  llvm::FoldingSetNodeID ID;
  SubstTemplateTemplateParmPackStorage::Profile(ID, Self, ArgPack,
                                                AssociatedDecl, Index, Final);

  void *InsertPos = nullptr;
  SubstTemplateTemplateParmPackStorage *Subst
    = SubstTemplateTemplateParmPacks.FindNodeOrInsertPos(ID, InsertPos);

  if (!Subst) {
    Subst = new (*this) SubstTemplateTemplateParmPackStorage(
        ArgPack.pack_elements(), AssociatedDecl, Index, Final);
    SubstTemplateTemplateParmPacks.InsertNode(Subst, InsertPos);
  }

  return TemplateName(Subst);
}

/// getFromTargetType - Given one of the integer types provided by
/// TargetInfo, produce the corresponding type. The unsigned @p Type
/// is actually a value of type @c TargetInfo::IntType.
CanQualType ASTContext::getFromTargetType(unsigned Type) const {
  switch (Type) {
  case TargetInfo::NoInt: return {};
  case TargetInfo::SignedChar: return SignedCharTy;
  case TargetInfo::UnsignedChar: return UnsignedCharTy;
  case TargetInfo::SignedShort: return ShortTy;
  case TargetInfo::UnsignedShort: return UnsignedShortTy;
  case TargetInfo::SignedInt: return IntTy;
  case TargetInfo::UnsignedInt: return UnsignedIntTy;
  case TargetInfo::SignedLong: return LongTy;
  case TargetInfo::UnsignedLong: return UnsignedLongTy;
  case TargetInfo::SignedLongLong: return LongLongTy;
  case TargetInfo::UnsignedLongLong: return UnsignedLongLongTy;
  }

  llvm_unreachable("Unhandled TargetInfo::IntType value");
}

//===----------------------------------------------------------------------===//
//                        Type Predicates.
//===----------------------------------------------------------------------===//

/// getObjCGCAttr - Returns one of GCNone, Weak or Strong objc's
/// garbage collection attribute.
///
Qualifiers::GC ASTContext::getObjCGCAttrKind(QualType Ty) const {
  if (getLangOpts().getGC() == LangOptions::NonGC)
    return Qualifiers::GCNone;

  assert(getLangOpts().ObjC);
  Qualifiers::GC GCAttrs = Ty.getObjCGCAttr();

  // Default behaviour under objective-C's gc is for ObjC pointers
  // (or pointers to them) be treated as though they were declared
  // as __strong.
  if (GCAttrs == Qualifiers::GCNone) {
    if (Ty->isObjCObjectPointerType() || Ty->isBlockPointerType())
      return Qualifiers::Strong;
    else if (Ty->isPointerType())
      return getObjCGCAttrKind(Ty->castAs<PointerType>()->getPointeeType());
  } else {
    // It's not valid to set GC attributes on anything that isn't a
    // pointer.
#ifndef NDEBUG
    QualType CT = Ty->getCanonicalTypeInternal();
    while (const auto *AT = dyn_cast<ArrayType>(CT))
      CT = AT->getElementType();
    assert(CT->isAnyPointerType() || CT->isBlockPointerType());
#endif
  }
  return GCAttrs;
}

//===----------------------------------------------------------------------===//
//                        Type Compatibility Testing
//===----------------------------------------------------------------------===//

/// areCompatVectorTypes - Return true if the two specified vector types are
/// compatible.
static bool areCompatVectorTypes(const VectorType *LHS,
                                 const VectorType *RHS) {
  assert(LHS->isCanonicalUnqualified() && RHS->isCanonicalUnqualified());
  return LHS->getElementType() == RHS->getElementType() &&
         LHS->getNumElements() == RHS->getNumElements();
}

/// areCompatMatrixTypes - Return true if the two specified matrix types are
/// compatible.
static bool areCompatMatrixTypes(const ConstantMatrixType *LHS,
                                 const ConstantMatrixType *RHS) {
  assert(LHS->isCanonicalUnqualified() && RHS->isCanonicalUnqualified());
  return LHS->getElementType() == RHS->getElementType() &&
         LHS->getNumRows() == RHS->getNumRows() &&
         LHS->getNumColumns() == RHS->getNumColumns();
}

bool ASTContext::areCompatibleVectorTypes(QualType FirstVec,
                                          QualType SecondVec) {
  assert(FirstVec->isVectorType() && "FirstVec should be a vector type");
  assert(SecondVec->isVectorType() && "SecondVec should be a vector type");

  if (hasSameUnqualifiedType(FirstVec, SecondVec))
    return true;

  // Treat Neon vector types and most AltiVec vector types as if they are the
  // equivalent GCC vector types.
  const auto *First = FirstVec->castAs<VectorType>();
  const auto *Second = SecondVec->castAs<VectorType>();
  if (First->getNumElements() == Second->getNumElements() &&
      hasSameType(First->getElementType(), Second->getElementType()) &&
      First->getVectorKind() != VectorKind::AltiVecPixel &&
      First->getVectorKind() != VectorKind::AltiVecBool &&
      Second->getVectorKind() != VectorKind::AltiVecPixel &&
      Second->getVectorKind() != VectorKind::AltiVecBool &&
      First->getVectorKind() != VectorKind::SveFixedLengthData &&
      First->getVectorKind() != VectorKind::SveFixedLengthPredicate &&
      Second->getVectorKind() != VectorKind::SveFixedLengthData &&
      Second->getVectorKind() != VectorKind::SveFixedLengthPredicate &&
      First->getVectorKind() != VectorKind::RVVFixedLengthData &&
      Second->getVectorKind() != VectorKind::RVVFixedLengthData &&
      First->getVectorKind() != VectorKind::RVVFixedLengthMask &&
      Second->getVectorKind() != VectorKind::RVVFixedLengthMask)
    return true;

  return false;
}

/// getSVETypeSize - Return SVE vector or predicate register size.
static uint64_t getSVETypeSize(ASTContext &Context, const BuiltinType *Ty) {
  assert(Ty->isSveVLSBuiltinType() && "Invalid SVE Type");
  if (Ty->getKind() == BuiltinType::SveBool ||
      Ty->getKind() == BuiltinType::SveCount)
    return (Context.getLangOpts().VScaleMin * 128) / Context.getCharWidth();
  return Context.getLangOpts().VScaleMin * 128;
}

bool ASTContext::areCompatibleSveTypes(QualType FirstType,
                                       QualType SecondType) {
  auto IsValidCast = [this](QualType FirstType, QualType SecondType) {
    if (const auto *BT = FirstType->getAs<BuiltinType>()) {
      if (const auto *VT = SecondType->getAs<VectorType>()) {
        // Predicates have the same representation as uint8 so we also have to
        // check the kind to make these types incompatible.
        if (VT->getVectorKind() == VectorKind::SveFixedLengthPredicate)
          return BT->getKind() == BuiltinType::SveBool;
        else if (VT->getVectorKind() == VectorKind::SveFixedLengthData)
          return VT->getElementType().getCanonicalType() ==
                 FirstType->getSveEltType(*this);
        else if (VT->getVectorKind() == VectorKind::Generic)
          return getTypeSize(SecondType) == getSVETypeSize(*this, BT) &&
                 hasSameType(VT->getElementType(),
                             getBuiltinVectorTypeInfo(BT).ElementType);
      }
    }
    return false;
  };

  return IsValidCast(FirstType, SecondType) ||
         IsValidCast(SecondType, FirstType);
}

bool ASTContext::areLaxCompatibleSveTypes(QualType FirstType,
                                          QualType SecondType) {
  auto IsLaxCompatible = [this](QualType FirstType, QualType SecondType) {
    const auto *BT = FirstType->getAs<BuiltinType>();
    if (!BT)
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    if (VecTy && (VecTy->getVectorKind() == VectorKind::SveFixedLengthData ||
                  VecTy->getVectorKind() == VectorKind::Generic)) {
      const LangOptions::LaxVectorConversionKind LVCKind =
          getLangOpts().getLaxVectorConversions();

      // Can not convert between sve predicates and sve vectors because of
      // different size.
      if (BT->getKind() == BuiltinType::SveBool &&
          VecTy->getVectorKind() == VectorKind::SveFixedLengthData)
        return false;

      // If __ARM_FEATURE_SVE_BITS != N do not allow GNU vector lax conversion.
      // "Whenever __ARM_FEATURE_SVE_BITS==N, GNUT implicitly
      // converts to VLAT and VLAT implicitly converts to GNUT."
      // ACLE Spec Version 00bet6, 3.7.3.2. Behavior common to vectors and
      // predicates.
      if (VecTy->getVectorKind() == VectorKind::Generic &&
          getTypeSize(SecondType) != getSVETypeSize(*this, BT))
        return false;

      // If -flax-vector-conversions=all is specified, the types are
      // certainly compatible.
      if (LVCKind == LangOptions::LaxVectorConversionKind::All)
        return true;

      // If -flax-vector-conversions=integer is specified, the types are
      // compatible if the elements are integer types.
      if (LVCKind == LangOptions::LaxVectorConversionKind::Integer)
        return VecTy->getElementType().getCanonicalType()->isIntegerType() &&
               FirstType->getSveEltType(*this)->isIntegerType();
    }

    return false;
  };

  return IsLaxCompatible(FirstType, SecondType) ||
         IsLaxCompatible(SecondType, FirstType);
}

/// getRVVTypeSize - Return RVV vector register size.
static uint64_t getRVVTypeSize(ASTContext &Context, const BuiltinType *Ty) {
  assert(Ty->isRVVVLSBuiltinType() && "Invalid RVV Type");
  auto VScale = Context.getTargetInfo().getVScaleRange(Context.getLangOpts());
  if (!VScale)
    return 0;

  ASTContext::BuiltinVectorTypeInfo Info = Context.getBuiltinVectorTypeInfo(Ty);

  uint64_t EltSize = Context.getTypeSize(Info.ElementType);
  if (Info.ElementType == Context.BoolTy)
    EltSize = 1;

  uint64_t MinElts = Info.EC.getKnownMinValue();
  return VScale->first * MinElts * EltSize;
}

bool ASTContext::areCompatibleRVVTypes(QualType FirstType,
                                       QualType SecondType) {
  assert(
      ((FirstType->isRVVSizelessBuiltinType() && SecondType->isVectorType()) ||
       (FirstType->isVectorType() && SecondType->isRVVSizelessBuiltinType())) &&
      "Expected RVV builtin type and vector type!");

  auto IsValidCast = [this](QualType FirstType, QualType SecondType) {
    if (const auto *BT = FirstType->getAs<BuiltinType>()) {
      if (const auto *VT = SecondType->getAs<VectorType>()) {
        if (VT->getVectorKind() == VectorKind::RVVFixedLengthMask) {
          BuiltinVectorTypeInfo Info = getBuiltinVectorTypeInfo(BT);
          return FirstType->isRVVVLSBuiltinType() &&
                 Info.ElementType == BoolTy &&
                 getTypeSize(SecondType) == getRVVTypeSize(*this, BT);
        }
        if (VT->getVectorKind() == VectorKind::RVVFixedLengthData ||
            VT->getVectorKind() == VectorKind::Generic)
          return FirstType->isRVVVLSBuiltinType() &&
                 getTypeSize(SecondType) == getRVVTypeSize(*this, BT) &&
                 hasSameType(VT->getElementType(),
                             getBuiltinVectorTypeInfo(BT).ElementType);
      }
    }
    return false;
  };

  return IsValidCast(FirstType, SecondType) ||
         IsValidCast(SecondType, FirstType);
}

bool ASTContext::areLaxCompatibleRVVTypes(QualType FirstType,
                                          QualType SecondType) {
  assert(
      ((FirstType->isRVVSizelessBuiltinType() && SecondType->isVectorType()) ||
       (FirstType->isVectorType() && SecondType->isRVVSizelessBuiltinType())) &&
      "Expected RVV builtin type and vector type!");

  auto IsLaxCompatible = [this](QualType FirstType, QualType SecondType) {
    const auto *BT = FirstType->getAs<BuiltinType>();
    if (!BT)
      return false;

    if (!BT->isRVVVLSBuiltinType())
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    if (VecTy && VecTy->getVectorKind() == VectorKind::Generic) {
      const LangOptions::LaxVectorConversionKind LVCKind =
          getLangOpts().getLaxVectorConversions();

      // If __riscv_v_fixed_vlen != N do not allow vector lax conversion.
      if (getTypeSize(SecondType) != getRVVTypeSize(*this, BT))
        return false;

      // If -flax-vector-conversions=all is specified, the types are
      // certainly compatible.
      if (LVCKind == LangOptions::LaxVectorConversionKind::All)
        return true;

      // If -flax-vector-conversions=integer is specified, the types are
      // compatible if the elements are integer types.
      if (LVCKind == LangOptions::LaxVectorConversionKind::Integer)
        return VecTy->getElementType().getCanonicalType()->isIntegerType() &&
               FirstType->getRVVEltType(*this)->isIntegerType();
    }

    return false;
  };

  return IsLaxCompatible(FirstType, SecondType) ||
         IsLaxCompatible(SecondType, FirstType);
}

bool ASTContext::hasDirectOwnershipQualifier(QualType Ty) const {
  while (true) {
    // __strong id
    if (const AttributedType *Attr = dyn_cast<AttributedType>(Ty)) {
      if (Attr->getAttrKind() == attr::ObjCOwnership)
        return true;

      Ty = Attr->getModifiedType();

    // X *__strong (...)
    } else if (const ParenType *Paren = dyn_cast<ParenType>(Ty)) {
      Ty = Paren->getInnerType();

    // We do not want to look through typedefs, typeof(expr),
    // typeof(type), or any other way that the type is somehow
    // abstracted.
    } else {
      return false;
    }
  }
}

//===----------------------------------------------------------------------===//
// ObjCQualifiedIdTypesAreCompatible - Compatibility testing for qualified id's.
//===----------------------------------------------------------------------===//

/// ProtocolCompatibleWithProtocol - return 'true' if 'lProto' is in the
/// inheritance hierarchy of 'rProto'.
bool
ASTContext::ProtocolCompatibleWithProtocol(ObjCProtocolDecl *lProto,
                                           ObjCProtocolDecl *rProto) const {
  if (declaresSameEntity(lProto, rProto))
    return true;
  for (auto *PI : rProto->protocols())
    if (ProtocolCompatibleWithProtocol(lProto, PI))
      return true;
  return false;
}

/// ObjCQualifiedClassTypesAreCompatible - compare  Class<pr,...> and
/// Class<pr1, ...>.
bool ASTContext::ObjCQualifiedClassTypesAreCompatible(
    const ObjCObjectPointerType *lhs, const ObjCObjectPointerType *rhs) {
  for (auto *lhsProto : lhs->quals()) {
    bool match = false;
    for (auto *rhsProto : rhs->quals()) {
      if (ProtocolCompatibleWithProtocol(lhsProto, rhsProto)) {
        match = true;
        break;
      }
    }
    if (!match)
      return false;
  }
  return true;
}

/// ObjCQualifiedIdTypesAreCompatible - We know that one of lhs/rhs is an
/// ObjCQualifiedIDType.
bool ASTContext::ObjCQualifiedIdTypesAreCompatible(
    const ObjCObjectPointerType *lhs, const ObjCObjectPointerType *rhs,
    bool compare) {
  // Allow id<P..> and an 'id' in all cases.
  if (lhs->isObjCIdType() || rhs->isObjCIdType())
    return true;

  // Don't allow id<P..> to convert to Class or Class<P..> in either direction.
  if (lhs->isObjCClassType() || lhs->isObjCQualifiedClassType() ||
      rhs->isObjCClassType() || rhs->isObjCQualifiedClassType())
    return false;

  if (lhs->isObjCQualifiedIdType()) {
    if (rhs->qual_empty()) {
      // If the RHS is a unqualified interface pointer "NSString*",
      // make sure we check the class hierarchy.
      if (ObjCInterfaceDecl *rhsID = rhs->getInterfaceDecl()) {
        for (auto *I : lhs->quals()) {
          // when comparing an id<P> on lhs with a static type on rhs,
          // see if static class implements all of id's protocols, directly or
          // through its super class and categories.
          if (!rhsID->ClassImplementsProtocol(I, true))
            return false;
        }
      }
      // If there are no qualifiers and no interface, we have an 'id'.
      return true;
    }
    // Both the right and left sides have qualifiers.
    for (auto *lhsProto : lhs->quals()) {
      bool match = false;

      // when comparing an id<P> on lhs with a static type on rhs,
      // see if static class implements all of id's protocols, directly or
      // through its super class and categories.
      for (auto *rhsProto : rhs->quals()) {
        if (ProtocolCompatibleWithProtocol(lhsProto, rhsProto) ||
            (compare && ProtocolCompatibleWithProtocol(rhsProto, lhsProto))) {
          match = true;
          break;
        }
      }
      // If the RHS is a qualified interface pointer "NSString<P>*",
      // make sure we check the class hierarchy.
      if (ObjCInterfaceDecl *rhsID = rhs->getInterfaceDecl()) {
        for (auto *I : lhs->quals()) {
          // when comparing an id<P> on lhs with a static type on rhs,
          // see if static class implements all of id's protocols, directly or
          // through its super class and categories.
          if (rhsID->ClassImplementsProtocol(I, true)) {
            match = true;
            break;
          }
        }
      }
      if (!match)
        return false;
    }

    return true;
  }

  assert(rhs->isObjCQualifiedIdType() && "One of the LHS/RHS should be id<x>");

  if (lhs->getInterfaceType()) {
    // If both the right and left sides have qualifiers.
    for (auto *lhsProto : lhs->quals()) {
      bool match = false;

      // when comparing an id<P> on rhs with a static type on lhs,
      // see if static class implements all of id's protocols, directly or
      // through its super class and categories.
      // First, lhs protocols in the qualifier list must be found, direct
      // or indirect in rhs's qualifier list or it is a mismatch.
      for (auto *rhsProto : rhs->quals()) {
        if (ProtocolCompatibleWithProtocol(lhsProto, rhsProto) ||
            (compare && ProtocolCompatibleWithProtocol(rhsProto, lhsProto))) {
          match = true;
          break;
        }
      }
      if (!match)
        return false;
    }

    // Static class's protocols, or its super class or category protocols
    // must be found, direct or indirect in rhs's qualifier list or it is a mismatch.
    if (ObjCInterfaceDecl *lhsID = lhs->getInterfaceDecl()) {
      llvm::SmallPtrSet<ObjCProtocolDecl *, 8> LHSInheritedProtocols;
      CollectInheritedProtocols(lhsID, LHSInheritedProtocols);
      // This is rather dubious but matches gcc's behavior. If lhs has
      // no type qualifier and its class has no static protocol(s)
      // assume that it is mismatch.
      if (LHSInheritedProtocols.empty() && lhs->qual_empty())
        return false;
      for (auto *lhsProto : LHSInheritedProtocols) {
        bool match = false;
        for (auto *rhsProto : rhs->quals()) {
          if (ProtocolCompatibleWithProtocol(lhsProto, rhsProto) ||
              (compare && ProtocolCompatibleWithProtocol(rhsProto, lhsProto))) {
            match = true;
            break;
          }
        }
        if (!match)
          return false;
      }
    }
    return true;
  }
  return false;
}

/// canAssignObjCInterfaces - Return true if the two interface types are
/// compatible for assignment from RHS to LHS.  This handles validation of any
/// protocol qualifiers on the LHS or RHS.
bool ASTContext::canAssignObjCInterfaces(const ObjCObjectPointerType *LHSOPT,
                                         const ObjCObjectPointerType *RHSOPT) {
  const ObjCObjectType* LHS = LHSOPT->getObjectType();
  const ObjCObjectType* RHS = RHSOPT->getObjectType();

  // If either type represents the built-in 'id' type, return true.
  if (LHS->isObjCUnqualifiedId() || RHS->isObjCUnqualifiedId())
    return true;

  // Function object that propagates a successful result or handles
  // __kindof types.
  auto finish = [&](bool succeeded) -> bool {
    if (succeeded)
      return true;

    if (!RHS->isKindOfType())
      return false;

    // Strip off __kindof and protocol qualifiers, then check whether
    // we can assign the other way.
    return canAssignObjCInterfaces(RHSOPT->stripObjCKindOfTypeAndQuals(*this),
                                   LHSOPT->stripObjCKindOfTypeAndQuals(*this));
  };

  // Casts from or to id<P> are allowed when the other side has compatible
  // protocols.
  if (LHS->isObjCQualifiedId() || RHS->isObjCQualifiedId()) {
    return finish(ObjCQualifiedIdTypesAreCompatible(LHSOPT, RHSOPT, false));
  }

  // Verify protocol compatibility for casts from Class<P1> to Class<P2>.
  if (LHS->isObjCQualifiedClass() && RHS->isObjCQualifiedClass()) {
    return finish(ObjCQualifiedClassTypesAreCompatible(LHSOPT, RHSOPT));
  }

  // Casts from Class to Class<Foo>, or vice-versa, are allowed.
  if (LHS->isObjCClass() && RHS->isObjCClass()) {
    return true;
  }

  // If we have 2 user-defined types, fall into that path.
  if (LHS->getInterface() && RHS->getInterface()) {
    return finish(canAssignObjCInterfaces(LHS, RHS));
  }

  return false;
}

/// canAssignObjCInterfacesInBlockPointer - This routine is specifically written
/// for providing type-safety for objective-c pointers used to pass/return
/// arguments in block literals. When passed as arguments, passing 'A*' where
/// 'id' is expected is not OK. Passing 'Sub *" where 'Super *" is expected is
/// not OK. For the return type, the opposite is not OK.
bool ASTContext::canAssignObjCInterfacesInBlockPointer(
                                         const ObjCObjectPointerType *LHSOPT,
                                         const ObjCObjectPointerType *RHSOPT,
                                         bool BlockReturnType) {

  // Function object that propagates a successful result or handles
  // __kindof types.
  auto finish = [&](bool succeeded) -> bool {
    if (succeeded)
      return true;

    const ObjCObjectPointerType *Expected = BlockReturnType ? RHSOPT : LHSOPT;
    if (!Expected->isKindOfType())
      return false;

    // Strip off __kindof and protocol qualifiers, then check whether
    // we can assign the other way.
    return canAssignObjCInterfacesInBlockPointer(
             RHSOPT->stripObjCKindOfTypeAndQuals(*this),
             LHSOPT->stripObjCKindOfTypeAndQuals(*this),
             BlockReturnType);
  };

  if (RHSOPT->isObjCBuiltinType() || LHSOPT->isObjCIdType())
    return true;

  if (LHSOPT->isObjCBuiltinType()) {
    return finish(RHSOPT->isObjCBuiltinType() ||
                  RHSOPT->isObjCQualifiedIdType());
  }

  if (LHSOPT->isObjCQualifiedIdType() || RHSOPT->isObjCQualifiedIdType()) {
    if (getLangOpts().CompatibilityQualifiedIdBlockParamTypeChecking)
      // Use for block parameters previous type checking for compatibility.
      return finish(ObjCQualifiedIdTypesAreCompatible(LHSOPT, RHSOPT, false) ||
                    // Or corrected type checking as in non-compat mode.
                    (!BlockReturnType &&
                     ObjCQualifiedIdTypesAreCompatible(RHSOPT, LHSOPT, false)));
    else
      return finish(ObjCQualifiedIdTypesAreCompatible(
          (BlockReturnType ? LHSOPT : RHSOPT),
          (BlockReturnType ? RHSOPT : LHSOPT), false));
  }

  const ObjCInterfaceType* LHS = LHSOPT->getInterfaceType();
  const ObjCInterfaceType* RHS = RHSOPT->getInterfaceType();
  if (LHS && RHS)  { // We have 2 user-defined types.
    if (LHS != RHS) {
      if (LHS->getDecl()->isSuperClassOf(RHS->getDecl()))
        return finish(BlockReturnType);
      if (RHS->getDecl()->isSuperClassOf(LHS->getDecl()))
        return finish(!BlockReturnType);
    }
    else
      return true;
  }
  return false;
}

/// Comparison routine for Objective-C protocols to be used with
/// llvm::array_pod_sort.
static int compareObjCProtocolsByName(ObjCProtocolDecl * const *lhs,
                                      ObjCProtocolDecl * const *rhs) {
  return (*lhs)->getName().compare((*rhs)->getName());
}

/// getIntersectionOfProtocols - This routine finds the intersection of set
/// of protocols inherited from two distinct objective-c pointer objects with
/// the given common base.
/// It is used to build composite qualifier list of the composite type of
/// the conditional expression involving two objective-c pointer objects.
static
void getIntersectionOfProtocols(ASTContext &Context,
                                const ObjCInterfaceDecl *CommonBase,
                                const ObjCObjectPointerType *LHSOPT,
                                const ObjCObjectPointerType *RHSOPT,
      SmallVectorImpl<ObjCProtocolDecl *> &IntersectionSet) {

  const ObjCObjectType* LHS = LHSOPT->getObjectType();
  const ObjCObjectType* RHS = RHSOPT->getObjectType();
  assert(LHS->getInterface() && "LHS must have an interface base");
  assert(RHS->getInterface() && "RHS must have an interface base");

  // Add all of the protocols for the LHS.
  llvm::SmallPtrSet<ObjCProtocolDecl *, 8> LHSProtocolSet;

  // Start with the protocol qualifiers.
  for (auto *proto : LHS->quals()) {
    Context.CollectInheritedProtocols(proto, LHSProtocolSet);
  }

  // Also add the protocols associated with the LHS interface.
  Context.CollectInheritedProtocols(LHS->getInterface(), LHSProtocolSet);

  // Add all of the protocols for the RHS.
  llvm::SmallPtrSet<ObjCProtocolDecl *, 8> RHSProtocolSet;

  // Start with the protocol qualifiers.
  for (auto *proto : RHS->quals()) {
    Context.CollectInheritedProtocols(proto, RHSProtocolSet);
  }

  // Also add the protocols associated with the RHS interface.
  Context.CollectInheritedProtocols(RHS->getInterface(), RHSProtocolSet);

  // Compute the intersection of the collected protocol sets.
  for (auto *proto : LHSProtocolSet) {
    if (RHSProtocolSet.count(proto))
      IntersectionSet.push_back(proto);
  }

  // Compute the set of protocols that is implied by either the common type or
  // the protocols within the intersection.
  llvm::SmallPtrSet<ObjCProtocolDecl *, 8> ImpliedProtocols;
  Context.CollectInheritedProtocols(CommonBase, ImpliedProtocols);

  // Remove any implied protocols from the list of inherited protocols.
  if (!ImpliedProtocols.empty()) {
    llvm::erase_if(IntersectionSet, [&](ObjCProtocolDecl *proto) -> bool {
      return ImpliedProtocols.contains(proto);
    });
  }

  // Sort the remaining protocols by name.
  llvm::array_pod_sort(IntersectionSet.begin(), IntersectionSet.end(),
                       compareObjCProtocolsByName);
}

/// Determine whether the first type is a subtype of the second.
static bool canAssignObjCObjectTypes(ASTContext &ctx, QualType lhs,
                                     QualType rhs) {
  // Common case: two object pointers.
  const auto *lhsOPT = lhs->getAs<ObjCObjectPointerType>();
  const auto *rhsOPT = rhs->getAs<ObjCObjectPointerType>();
  if (lhsOPT && rhsOPT)
    return ctx.canAssignObjCInterfaces(lhsOPT, rhsOPT);

  // Two block pointers.
  const auto *lhsBlock = lhs->getAs<BlockPointerType>();
  const auto *rhsBlock = rhs->getAs<BlockPointerType>();
  if (lhsBlock && rhsBlock)
    return ctx.typesAreBlockPointerCompatible(lhs, rhs);

  // If either is an unqualified 'id' and the other is a block, it's
  // acceptable.
  if ((lhsOPT && lhsOPT->isObjCIdType() && rhsBlock) ||
      (rhsOPT && rhsOPT->isObjCIdType() && lhsBlock))
    return true;

  return false;
}

// Check that the given Objective-C type argument lists are equivalent.
static bool sameObjCTypeArgs(ASTContext &ctx,
                             const ObjCInterfaceDecl *iface,
                             ArrayRef<QualType> lhsArgs,
                             ArrayRef<QualType> rhsArgs,
                             bool stripKindOf) {
  if (lhsArgs.size() != rhsArgs.size())
    return false;

  ObjCTypeParamList *typeParams = iface->getTypeParamList();
  if (!typeParams)
    return false;

  for (unsigned i = 0, n = lhsArgs.size(); i != n; ++i) {
    if (ctx.hasSameType(lhsArgs[i], rhsArgs[i]))
      continue;

    switch (typeParams->begin()[i]->getVariance()) {
    case ObjCTypeParamVariance::Invariant:
      if (!stripKindOf ||
          !ctx.hasSameType(lhsArgs[i].stripObjCKindOfType(ctx),
                           rhsArgs[i].stripObjCKindOfType(ctx))) {
        return false;
      }
      break;

    case ObjCTypeParamVariance::Covariant:
      if (!canAssignObjCObjectTypes(ctx, lhsArgs[i], rhsArgs[i]))
        return false;
      break;

    case ObjCTypeParamVariance::Contravariant:
      if (!canAssignObjCObjectTypes(ctx, rhsArgs[i], lhsArgs[i]))
        return false;
      break;
    }
  }

  return true;
}

QualType ASTContext::areCommonBaseCompatible(
           const ObjCObjectPointerType *Lptr,
           const ObjCObjectPointerType *Rptr) {
  const ObjCObjectType *LHS = Lptr->getObjectType();
  const ObjCObjectType *RHS = Rptr->getObjectType();
  const ObjCInterfaceDecl* LDecl = LHS->getInterface();
  const ObjCInterfaceDecl* RDecl = RHS->getInterface();

  if (!LDecl || !RDecl)
    return {};

  // When either LHS or RHS is a kindof type, we should return a kindof type.
  // For example, for common base of kindof(ASub1) and kindof(ASub2), we return
  // kindof(A).
  bool anyKindOf = LHS->isKindOfType() || RHS->isKindOfType();

  // Follow the left-hand side up the class hierarchy until we either hit a
  // root or find the RHS. Record the ancestors in case we don't find it.
  llvm::SmallDenseMap<const ObjCInterfaceDecl *, const ObjCObjectType *, 4>
    LHSAncestors;
  while (true) {
    // Record this ancestor. We'll need this if the common type isn't in the
    // path from the LHS to the root.
    LHSAncestors[LHS->getInterface()->getCanonicalDecl()] = LHS;

    if (declaresSameEntity(LHS->getInterface(), RDecl)) {
      // Get the type arguments.
      ArrayRef<QualType> LHSTypeArgs = LHS->getTypeArgsAsWritten();
      bool anyChanges = false;
      if (LHS->isSpecialized() && RHS->isSpecialized()) {
        // Both have type arguments, compare them.
        if (!sameObjCTypeArgs(*this, LHS->getInterface(),
                              LHS->getTypeArgs(), RHS->getTypeArgs(),
                              /*stripKindOf=*/true))
          return {};
      } else if (LHS->isSpecialized() != RHS->isSpecialized()) {
        // If only one has type arguments, the result will not have type
        // arguments.
        LHSTypeArgs = {};
        anyChanges = true;
      }

      // Compute the intersection of protocols.
      SmallVector<ObjCProtocolDecl *, 8> Protocols;
      getIntersectionOfProtocols(*this, LHS->getInterface(), Lptr, Rptr,
                                 Protocols);
      if (!Protocols.empty())
        anyChanges = true;

      // If anything in the LHS will have changed, build a new result type.
      // If we need to return a kindof type but LHS is not a kindof type, we
      // build a new result type.
      if (anyChanges || LHS->isKindOfType() != anyKindOf) {
        QualType Result = getObjCInterfaceType(LHS->getInterface());
        Result = getObjCObjectType(Result, LHSTypeArgs, Protocols,
                                   anyKindOf || LHS->isKindOfType());
        return getObjCObjectPointerType(Result);
      }

      return getObjCObjectPointerType(QualType(LHS, 0));
    }

    // Find the superclass.
    QualType LHSSuperType = LHS->getSuperClassType();
    if (LHSSuperType.isNull())
      break;

    LHS = LHSSuperType->castAs<ObjCObjectType>();
  }

  // We didn't find anything by following the LHS to its root; now check
  // the RHS against the cached set of ancestors.
  while (true) {
    auto KnownLHS = LHSAncestors.find(RHS->getInterface()->getCanonicalDecl());
    if (KnownLHS != LHSAncestors.end()) {
      LHS = KnownLHS->second;

      // Get the type arguments.
      ArrayRef<QualType> RHSTypeArgs = RHS->getTypeArgsAsWritten();
      bool anyChanges = false;
      if (LHS->isSpecialized() && RHS->isSpecialized()) {
        // Both have type arguments, compare them.
        if (!sameObjCTypeArgs(*this, LHS->getInterface(),
                              LHS->getTypeArgs(), RHS->getTypeArgs(),
                              /*stripKindOf=*/true))
          return {};
      } else if (LHS->isSpecialized() != RHS->isSpecialized()) {
        // If only one has type arguments, the result will not have type
        // arguments.
        RHSTypeArgs = {};
        anyChanges = true;
      }

      // Compute the intersection of protocols.
      SmallVector<ObjCProtocolDecl *, 8> Protocols;
      getIntersectionOfProtocols(*this, RHS->getInterface(), Lptr, Rptr,
                                 Protocols);
      if (!Protocols.empty())
        anyChanges = true;

      // If we need to return a kindof type but RHS is not a kindof type, we
      // build a new result type.
      if (anyChanges || RHS->isKindOfType() != anyKindOf) {
        QualType Result = getObjCInterfaceType(RHS->getInterface());
        Result = getObjCObjectType(Result, RHSTypeArgs, Protocols,
                                   anyKindOf || RHS->isKindOfType());
        return getObjCObjectPointerType(Result);
      }

      return getObjCObjectPointerType(QualType(RHS, 0));
    }

    // Find the superclass of the RHS.
    QualType RHSSuperType = RHS->getSuperClassType();
    if (RHSSuperType.isNull())
      break;

    RHS = RHSSuperType->castAs<ObjCObjectType>();
  }

  return {};
}

bool ASTContext::canAssignObjCInterfaces(const ObjCObjectType *LHS,
                                         const ObjCObjectType *RHS) {
  assert(LHS->getInterface() && "LHS is not an interface type");
  assert(RHS->getInterface() && "RHS is not an interface type");

  // Verify that the base decls are compatible: the RHS must be a subclass of
  // the LHS.
  ObjCInterfaceDecl *LHSInterface = LHS->getInterface();
  bool IsSuperClass = LHSInterface->isSuperClassOf(RHS->getInterface());
  if (!IsSuperClass)
    return false;

  // If the LHS has protocol qualifiers, determine whether all of them are
  // satisfied by the RHS (i.e., the RHS has a superset of the protocols in the
  // LHS).
  if (LHS->getNumProtocols() > 0) {
    // OK if conversion of LHS to SuperClass results in narrowing of types
    // ; i.e., SuperClass may implement at least one of the protocols
    // in LHS's protocol list. Example, SuperObj<P1> = lhs<P1,P2> is ok.
    // But not SuperObj<P1,P2,P3> = lhs<P1,P2>.
    llvm::SmallPtrSet<ObjCProtocolDecl *, 8> SuperClassInheritedProtocols;
    CollectInheritedProtocols(RHS->getInterface(), SuperClassInheritedProtocols);
    // Also, if RHS has explicit quelifiers, include them for comparing with LHS's
    // qualifiers.
    for (auto *RHSPI : RHS->quals())
      CollectInheritedProtocols(RHSPI, SuperClassInheritedProtocols);
    // If there is no protocols associated with RHS, it is not a match.
    if (SuperClassInheritedProtocols.empty())
      return false;

    for (const auto *LHSProto : LHS->quals()) {
      bool SuperImplementsProtocol = false;
      for (auto *SuperClassProto : SuperClassInheritedProtocols)
        if (SuperClassProto->lookupProtocolNamed(LHSProto->getIdentifier())) {
          SuperImplementsProtocol = true;
          break;
        }
      if (!SuperImplementsProtocol)
        return false;
    }
  }

  // If the LHS is specialized, we may need to check type arguments.
  if (LHS->isSpecialized()) {
    // Follow the superclass chain until we've matched the LHS class in the
    // hierarchy. This substitutes type arguments through.
    const ObjCObjectType *RHSSuper = RHS;
    while (!declaresSameEntity(RHSSuper->getInterface(), LHSInterface))
      RHSSuper = RHSSuper->getSuperClassType()->castAs<ObjCObjectType>();

    // If the RHS is specializd, compare type arguments.
    if (RHSSuper->isSpecialized() &&
        !sameObjCTypeArgs(*this, LHS->getInterface(),
                          LHS->getTypeArgs(), RHSSuper->getTypeArgs(),
                          /*stripKindOf=*/true)) {
      return false;
    }
  }

  return true;
}

bool ASTContext::areComparableObjCPointerTypes(QualType LHS, QualType RHS) {
  // get the "pointed to" types
  const auto *LHSOPT = LHS->getAs<ObjCObjectPointerType>();
  const auto *RHSOPT = RHS->getAs<ObjCObjectPointerType>();

  if (!LHSOPT || !RHSOPT)
    return false;

  return canAssignObjCInterfaces(LHSOPT, RHSOPT) ||
         canAssignObjCInterfaces(RHSOPT, LHSOPT);
}

bool ASTContext::canBindObjCObjectType(QualType To, QualType From) {
  return canAssignObjCInterfaces(
      getObjCObjectPointerType(To)->castAs<ObjCObjectPointerType>(),
      getObjCObjectPointerType(From)->castAs<ObjCObjectPointerType>());
}

/// typesAreCompatible - C99 6.7.3p9: For two qualified types to be compatible,
/// both shall have the identically qualified version of a compatible type.
/// C99 6.2.7p1: Two types have compatible types if their types are the
/// same. See 6.7.[2,3,5] for additional rules.
bool ASTContext::typesAreCompatible(QualType LHS, QualType RHS,
                                    bool CompareUnqualified) {
  if (getLangOpts().CPlusPlus)
    return hasSameType(LHS, RHS);

  return !mergeTypes(LHS, RHS, false, CompareUnqualified).isNull();
}

bool ASTContext::propertyTypesAreCompatible(QualType LHS, QualType RHS) {
  return typesAreCompatible(LHS, RHS);
}

bool ASTContext::typesAreBlockPointerCompatible(QualType LHS, QualType RHS) {
  return !mergeTypes(LHS, RHS, true).isNull();
}

/// mergeTransparentUnionType - if T is a transparent union type and a member
/// of T is compatible with SubType, return the merged type, else return
/// QualType()
QualType ASTContext::mergeTransparentUnionType(QualType T, QualType SubType,
                                               bool OfBlockPointer,
                                               bool Unqualified) {
  if (const RecordType *UT = T->getAsUnionType()) {
    RecordDecl *UD = UT->getDecl();
    if (UD->hasAttr<TransparentUnionAttr>()) {
      for (const auto *I : UD->fields()) {
        QualType ET = I->getType().getUnqualifiedType();
        QualType MT = mergeTypes(ET, SubType, OfBlockPointer, Unqualified);
        if (!MT.isNull())
          return MT;
      }
    }
  }

  return {};
}

/// mergeFunctionParameterTypes - merge two types which appear as function
/// parameter types
QualType ASTContext::mergeFunctionParameterTypes(QualType lhs, QualType rhs,
                                                 bool OfBlockPointer,
                                                 bool Unqualified) {
  // GNU extension: two types are compatible if they appear as a function
  // argument, one of the types is a transparent union type and the other
  // type is compatible with a union member
  QualType lmerge = mergeTransparentUnionType(lhs, rhs, OfBlockPointer,
                                              Unqualified);
  if (!lmerge.isNull())
    return lmerge;

  QualType rmerge = mergeTransparentUnionType(rhs, lhs, OfBlockPointer,
                                              Unqualified);
  if (!rmerge.isNull())
    return rmerge;

  return mergeTypes(lhs, rhs, OfBlockPointer, Unqualified);
}

QualType ASTContext::mergeFunctionTypes(QualType lhs, QualType rhs,
                                        bool OfBlockPointer, bool Unqualified,
                                        bool AllowCXX,
                                        bool IsConditionalOperator) {
  const auto *lbase = lhs->castAs<FunctionType>();
  const auto *rbase = rhs->castAs<FunctionType>();
  const auto *lproto = dyn_cast<FunctionProtoType>(lbase);
  const auto *rproto = dyn_cast<FunctionProtoType>(rbase);
  bool allLTypes = true;
  bool allRTypes = true;

  // Check return type
  QualType retType;
  if (OfBlockPointer) {
    QualType RHS = rbase->getReturnType();
    QualType LHS = lbase->getReturnType();
    bool UnqualifiedResult = Unqualified;
    if (!UnqualifiedResult)
      UnqualifiedResult = (!RHS.hasQualifiers() && LHS.hasQualifiers());
    retType = mergeTypes(LHS, RHS, true, UnqualifiedResult, true);
  }
  else
    retType = mergeTypes(lbase->getReturnType(), rbase->getReturnType(), false,
                         Unqualified);
  if (retType.isNull())
    return {};

  if (Unqualified)
    retType = retType.getUnqualifiedType();

  CanQualType LRetType = getCanonicalType(lbase->getReturnType());
  CanQualType RRetType = getCanonicalType(rbase->getReturnType());
  if (Unqualified) {
    LRetType = LRetType.getUnqualifiedType();
    RRetType = RRetType.getUnqualifiedType();
  }

  if (getCanonicalType(retType) != LRetType)
    allLTypes = false;
  if (getCanonicalType(retType) != RRetType)
    allRTypes = false;

  // FIXME: double check this
  // FIXME: should we error if lbase->getRegParmAttr() != 0 &&
  //                           rbase->getRegParmAttr() != 0 &&
  //                           lbase->getRegParmAttr() != rbase->getRegParmAttr()?
  FunctionType::ExtInfo lbaseInfo = lbase->getExtInfo();
  FunctionType::ExtInfo rbaseInfo = rbase->getExtInfo();

  // Compatible functions must have compatible calling conventions
  if (lbaseInfo.getCC() != rbaseInfo.getCC())
    return {};

  // Regparm is part of the calling convention.
  if (lbaseInfo.getHasRegParm() != rbaseInfo.getHasRegParm())
    return {};
  if (lbaseInfo.getRegParm() != rbaseInfo.getRegParm())
    return {};

  if (lbaseInfo.getProducesResult() != rbaseInfo.getProducesResult())
    return {};
  if (lbaseInfo.getNoCallerSavedRegs() != rbaseInfo.getNoCallerSavedRegs())
    return {};
  if (lbaseInfo.getNoCfCheck() != rbaseInfo.getNoCfCheck())
    return {};

  // When merging declarations, it's common for supplemental information like
  // attributes to only be present in one of the declarations, and we generally
  // want type merging to preserve the union of information.  So a merged
  // function type should be noreturn if it was noreturn in *either* operand
  // type.
  //
  // But for the conditional operator, this is backwards.  The result of the
  // operator could be either operand, and its type should conservatively
  // reflect that.  So a function type in a composite type is noreturn only
  // if it's noreturn in *both* operand types.
  //
  // Arguably, noreturn is a kind of subtype, and the conditional operator
  // ought to produce the most specific common supertype of its operand types.
  // That would differ from this rule in contravariant positions.  However,
  // neither C nor C++ generally uses this kind of subtype reasoning.  Also,
  // as a practical matter, it would only affect C code that does abstraction of
  // higher-order functions (taking noreturn callbacks!), which is uncommon to
  // say the least.  So we use the simpler rule.
  bool NoReturn = IsConditionalOperator
                      ? lbaseInfo.getNoReturn() && rbaseInfo.getNoReturn()
                      : lbaseInfo.getNoReturn() || rbaseInfo.getNoReturn();
  if (lbaseInfo.getNoReturn() != NoReturn)
    allLTypes = false;
  if (rbaseInfo.getNoReturn() != NoReturn)
    allRTypes = false;

  FunctionType::ExtInfo einfo = lbaseInfo.withNoReturn(NoReturn);

  std::optional<FunctionEffectSet> MergedFX;

  if (lproto && rproto) { // two C99 style function prototypes
    assert((AllowCXX ||
            (!lproto->hasExceptionSpec() && !rproto->hasExceptionSpec())) &&
           "C++ shouldn't be here");
    // Compatible functions must have the same number of parameters
    if (lproto->getNumParams() != rproto->getNumParams())
      return {};

    // Variadic and non-variadic functions aren't compatible
    if (lproto->isVariadic() != rproto->isVariadic())
      return {};

    if (lproto->getMethodQuals() != rproto->getMethodQuals())
      return {};

    // Function effects are handled similarly to noreturn, see above.
    FunctionEffectsRef LHSFX = lproto->getFunctionEffects();
    FunctionEffectsRef RHSFX = rproto->getFunctionEffects();
    if (LHSFX != RHSFX) {
      if (IsConditionalOperator)
        MergedFX = FunctionEffectSet::getIntersection(LHSFX, RHSFX);
      else {
        FunctionEffectSet::Conflicts Errs;
        MergedFX = FunctionEffectSet::getUnion(LHSFX, RHSFX, Errs);
        // Here we're discarding a possible error due to conflicts in the effect
        // sets. But we're not in a context where we can report it. The
        // operation does however guarantee maintenance of invariants.
      }
      if (*MergedFX != LHSFX)
        allLTypes = false;
      if (*MergedFX != RHSFX)
        allRTypes = false;
    }

    SmallVector<FunctionProtoType::ExtParameterInfo, 4> newParamInfos;
    bool canUseLeft, canUseRight;
    if (!mergeExtParameterInfo(lproto, rproto, canUseLeft, canUseRight,
                               newParamInfos))
      return {};

    if (!canUseLeft)
      allLTypes = false;
    if (!canUseRight)
      allRTypes = false;

    // Check parameter type compatibility
    SmallVector<QualType, 10> types;
    for (unsigned i = 0, n = lproto->getNumParams(); i < n; i++) {
      QualType lParamType = lproto->getParamType(i).getUnqualifiedType();
      QualType rParamType = rproto->getParamType(i).getUnqualifiedType();
      QualType paramType = mergeFunctionParameterTypes(
          lParamType, rParamType, OfBlockPointer, Unqualified);
      if (paramType.isNull())
        return {};

      if (Unqualified)
        paramType = paramType.getUnqualifiedType();

      types.push_back(paramType);
      if (Unqualified) {
        lParamType = lParamType.getUnqualifiedType();
        rParamType = rParamType.getUnqualifiedType();
      }

      if (getCanonicalType(paramType) != getCanonicalType(lParamType))
        allLTypes = false;
      if (getCanonicalType(paramType) != getCanonicalType(rParamType))
        allRTypes = false;
    }

    if (allLTypes) return lhs;
    if (allRTypes) return rhs;

    FunctionProtoType::ExtProtoInfo EPI = lproto->getExtProtoInfo();
    EPI.ExtInfo = einfo;
    EPI.ExtParameterInfos =
        newParamInfos.empty() ? nullptr : newParamInfos.data();
    if (MergedFX)
      EPI.FunctionEffects = *MergedFX;
    return getFunctionType(retType, types, EPI);
  }

  if (lproto) allRTypes = false;
  if (rproto) allLTypes = false;

  const FunctionProtoType *proto = lproto ? lproto : rproto;
  if (proto) {
    assert((AllowCXX || !proto->hasExceptionSpec()) && "C++ shouldn't be here");
    if (proto->isVariadic())
      return {};
    // Check that the types are compatible with the types that
    // would result from default argument promotions (C99 6.7.5.3p15).
    // The only types actually affected are promotable integer
    // types and floats, which would be passed as a different
    // type depending on whether the prototype is visible.
    for (unsigned i = 0, n = proto->getNumParams(); i < n; ++i) {
      QualType paramTy = proto->getParamType(i);

      // Look at the converted type of enum types, since that is the type used
      // to pass enum values.
      if (const auto *Enum = paramTy->getAs<EnumType>()) {
        paramTy = Enum->getDecl()->getIntegerType();
        if (paramTy.isNull())
          return {};
      }

      if (isPromotableIntegerType(paramTy) ||
          getCanonicalType(paramTy).getUnqualifiedType() == FloatTy)
        return {};
    }

    if (allLTypes) return lhs;
    if (allRTypes) return rhs;

    FunctionProtoType::ExtProtoInfo EPI = proto->getExtProtoInfo();
    EPI.ExtInfo = einfo;
    if (MergedFX)
      EPI.FunctionEffects = *MergedFX;
    return getFunctionType(retType, proto->getParamTypes(), EPI);
  }

  if (allLTypes) return lhs;
  if (allRTypes) return rhs;
  return getFunctionNoProtoType(retType, einfo);
}

/// Given that we have an enum type and a non-enum type, try to merge them.
static QualType mergeEnumWithInteger(ASTContext &Context, const EnumType *ET,
                                     QualType other, bool isBlockReturnType) {
  // C99 6.7.2.2p4: Each enumerated type shall be compatible with char,
  // a signed integer type, or an unsigned integer type.
  // Compatibility is based on the underlying type, not the promotion
  // type.
  QualType underlyingType = ET->getDecl()->getIntegerType();
  if (underlyingType.isNull())
    return {};
  if (Context.hasSameType(underlyingType, other))
    return other;

  // In block return types, we're more permissive and accept any
  // integral type of the same size.
  if (isBlockReturnType && other->isIntegerType() &&
      Context.getTypeSize(underlyingType) == Context.getTypeSize(other))
    return other;

  return {};
}

QualType ASTContext::mergeTypes(QualType LHS, QualType RHS, bool OfBlockPointer,
                                bool Unqualified, bool BlockReturnType,
                                bool IsConditionalOperator) {
  // For C++ we will not reach this code with reference types (see below),
  // for OpenMP variant call overloading we might.
  //
  // C++ [expr]: If an expression initially has the type "reference to T", the
  // type is adjusted to "T" prior to any further analysis, the expression
  // designates the object or function denoted by the reference, and the
  // expression is an lvalue unless the reference is an rvalue reference and
  // the expression is a function call (possibly inside parentheses).
  auto *LHSRefTy = LHS->getAs<ReferenceType>();
  auto *RHSRefTy = RHS->getAs<ReferenceType>();
  if (LangOpts.OpenMP && LHSRefTy && RHSRefTy &&
      LHS->getTypeClass() == RHS->getTypeClass())
    return mergeTypes(LHSRefTy->getPointeeType(), RHSRefTy->getPointeeType(),
                      OfBlockPointer, Unqualified, BlockReturnType);
  if (LHSRefTy || RHSRefTy)
    return {};

  if (Unqualified) {
    LHS = LHS.getUnqualifiedType();
    RHS = RHS.getUnqualifiedType();
  }

  QualType LHSCan = getCanonicalType(LHS),
           RHSCan = getCanonicalType(RHS);

  // If two types are identical, they are compatible.
  if (LHSCan == RHSCan)
    return LHS;

  // If the qualifiers are different, the types aren't compatible... mostly.
  Qualifiers LQuals = LHSCan.getLocalQualifiers();
  Qualifiers RQuals = RHSCan.getLocalQualifiers();
  if (LQuals != RQuals) {
    // If any of these qualifiers are different, we have a type
    // mismatch.
    if (LQuals.getCVRQualifiers() != RQuals.getCVRQualifiers() ||
        LQuals.getAddressSpace() != RQuals.getAddressSpace() ||
        LQuals.getObjCLifetime() != RQuals.getObjCLifetime() ||
        LQuals.hasUnaligned() != RQuals.hasUnaligned())
      return {};

    // Exactly one GC qualifier difference is allowed: __strong is
    // okay if the other type has no GC qualifier but is an Objective
    // C object pointer (i.e. implicitly strong by default).  We fix
    // this by pretending that the unqualified type was actually
    // qualified __strong.
    Qualifiers::GC GC_L = LQuals.getObjCGCAttr();
    Qualifiers::GC GC_R = RQuals.getObjCGCAttr();
    assert((GC_L != GC_R) && "unequal qualifier sets had only equal elements");

    if (GC_L == Qualifiers::Weak || GC_R == Qualifiers::Weak)
      return {};

    if (GC_L == Qualifiers::Strong && RHSCan->isObjCObjectPointerType()) {
      return mergeTypes(LHS, getObjCGCQualType(RHS, Qualifiers::Strong));
    }
    if (GC_R == Qualifiers::Strong && LHSCan->isObjCObjectPointerType()) {
      return mergeTypes(getObjCGCQualType(LHS, Qualifiers::Strong), RHS);
    }
    return {};
  }

  // Okay, qualifiers are equal.

  Type::TypeClass LHSClass = LHSCan->getTypeClass();
  Type::TypeClass RHSClass = RHSCan->getTypeClass();

  // We want to consider the two function types to be the same for these
  // comparisons, just force one to the other.
  if (LHSClass == Type::FunctionProto) LHSClass = Type::FunctionNoProto;
  if (RHSClass == Type::FunctionProto) RHSClass = Type::FunctionNoProto;

  // Same as above for arrays
  if (LHSClass == Type::VariableArray || LHSClass == Type::IncompleteArray)
    LHSClass = Type::ConstantArray;
  if (RHSClass == Type::VariableArray || RHSClass == Type::IncompleteArray)
    RHSClass = Type::ConstantArray;

  // ObjCInterfaces are just specialized ObjCObjects.
  if (LHSClass == Type::ObjCInterface) LHSClass = Type::ObjCObject;
  if (RHSClass == Type::ObjCInterface) RHSClass = Type::ObjCObject;

  // Canonicalize ExtVector -> Vector.
  if (LHSClass == Type::ExtVector) LHSClass = Type::Vector;
  if (RHSClass == Type::ExtVector) RHSClass = Type::Vector;

  // If the canonical type classes don't match.
  if (LHSClass != RHSClass) {
    // Note that we only have special rules for turning block enum
    // returns into block int returns, not vice-versa.
    if (const auto *ETy = LHS->getAs<EnumType>()) {
      return mergeEnumWithInteger(*this, ETy, RHS, false);
    }
    if (const EnumType* ETy = RHS->getAs<EnumType>()) {
      return mergeEnumWithInteger(*this, ETy, LHS, BlockReturnType);
    }
    // allow block pointer type to match an 'id' type.
    if (OfBlockPointer && !BlockReturnType) {
       if (LHS->isObjCIdType() && RHS->isBlockPointerType())
         return LHS;
      if (RHS->isObjCIdType() && LHS->isBlockPointerType())
        return RHS;
    }
    // Allow __auto_type to match anything; it merges to the type with more
    // information.
    if (const auto *AT = LHS->getAs<AutoType>()) {
      if (!AT->isDeduced() && AT->isGNUAutoType())
        return RHS;
    }
    if (const auto *AT = RHS->getAs<AutoType>()) {
      if (!AT->isDeduced() && AT->isGNUAutoType())
        return LHS;
    }
    return {};
  }

  // The canonical type classes match.
  switch (LHSClass) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("Non-canonical and dependent types shouldn't get here");

  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
  case Type::LValueReference:
  case Type::RValueReference:
  case Type::MemberPointer:
    llvm_unreachable("C++ should never be in mergeTypes");

  case Type::ObjCInterface:
  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::FunctionProto:
  case Type::ExtVector:
    llvm_unreachable("Types are eliminated above");

  case Type::Pointer:
  {
    // Merge two pointer types, while trying to preserve typedef info
    QualType LHSPointee = LHS->castAs<PointerType>()->getPointeeType();
    QualType RHSPointee = RHS->castAs<PointerType>()->getPointeeType();
    if (Unqualified) {
      LHSPointee = LHSPointee.getUnqualifiedType();
      RHSPointee = RHSPointee.getUnqualifiedType();
    }
    QualType ResultType = mergeTypes(LHSPointee, RHSPointee, false,
                                     Unqualified);
    if (ResultType.isNull())
      return {};
    if (getCanonicalType(LHSPointee) == getCanonicalType(ResultType))
      return LHS;
    if (getCanonicalType(RHSPointee) == getCanonicalType(ResultType))
      return RHS;
    return getPointerType(ResultType);
  }
  case Type::BlockPointer:
  {
    // Merge two block pointer types, while trying to preserve typedef info
    QualType LHSPointee = LHS->castAs<BlockPointerType>()->getPointeeType();
    QualType RHSPointee = RHS->castAs<BlockPointerType>()->getPointeeType();
    if (Unqualified) {
      LHSPointee = LHSPointee.getUnqualifiedType();
      RHSPointee = RHSPointee.getUnqualifiedType();
    }
    if (getLangOpts().OpenCL) {
      Qualifiers LHSPteeQual = LHSPointee.getQualifiers();
      Qualifiers RHSPteeQual = RHSPointee.getQualifiers();
      // Blocks can't be an expression in a ternary operator (OpenCL v2.0
      // 6.12.5) thus the following check is asymmetric.
      if (!LHSPteeQual.isAddressSpaceSupersetOf(RHSPteeQual))
        return {};
      LHSPteeQual.removeAddressSpace();
      RHSPteeQual.removeAddressSpace();
      LHSPointee =
          QualType(LHSPointee.getTypePtr(), LHSPteeQual.getAsOpaqueValue());
      RHSPointee =
          QualType(RHSPointee.getTypePtr(), RHSPteeQual.getAsOpaqueValue());
    }
    QualType ResultType = mergeTypes(LHSPointee, RHSPointee, OfBlockPointer,
                                     Unqualified);
    if (ResultType.isNull())
      return {};
    if (getCanonicalType(LHSPointee) == getCanonicalType(ResultType))
      return LHS;
    if (getCanonicalType(RHSPointee) == getCanonicalType(ResultType))
      return RHS;
    return getBlockPointerType(ResultType);
  }
  case Type::Atomic:
  {
    // Merge two pointer types, while trying to preserve typedef info
    QualType LHSValue = LHS->castAs<AtomicType>()->getValueType();
    QualType RHSValue = RHS->castAs<AtomicType>()->getValueType();
    if (Unqualified) {
      LHSValue = LHSValue.getUnqualifiedType();
      RHSValue = RHSValue.getUnqualifiedType();
    }
    QualType ResultType = mergeTypes(LHSValue, RHSValue, false,
                                     Unqualified);
    if (ResultType.isNull())
      return {};
    if (getCanonicalType(LHSValue) == getCanonicalType(ResultType))
      return LHS;
    if (getCanonicalType(RHSValue) == getCanonicalType(ResultType))
      return RHS;
    return getAtomicType(ResultType);
  }
  case Type::ConstantArray:
  {
    const ConstantArrayType* LCAT = getAsConstantArrayType(LHS);
    const ConstantArrayType* RCAT = getAsConstantArrayType(RHS);
    if (LCAT && RCAT && RCAT->getZExtSize() != LCAT->getZExtSize())
      return {};

    QualType LHSElem = getAsArrayType(LHS)->getElementType();
    QualType RHSElem = getAsArrayType(RHS)->getElementType();
    if (Unqualified) {
      LHSElem = LHSElem.getUnqualifiedType();
      RHSElem = RHSElem.getUnqualifiedType();
    }

    QualType ResultType = mergeTypes(LHSElem, RHSElem, false, Unqualified);
    if (ResultType.isNull())
      return {};

    const VariableArrayType* LVAT = getAsVariableArrayType(LHS);
    const VariableArrayType* RVAT = getAsVariableArrayType(RHS);

    // If either side is a variable array, and both are complete, check whether
    // the current dimension is definite.
    if (LVAT || RVAT) {
      auto SizeFetch = [this](const VariableArrayType* VAT,
          const ConstantArrayType* CAT)
          -> std::pair<bool,llvm::APInt> {
        if (VAT) {
          std::optional<llvm::APSInt> TheInt;
          Expr *E = VAT->getSizeExpr();
          if (E && (TheInt = E->getIntegerConstantExpr(*this)))
            return std::make_pair(true, *TheInt);
          return std::make_pair(false, llvm::APSInt());
        }
        if (CAT)
          return std::make_pair(true, CAT->getSize());
        return std::make_pair(false, llvm::APInt());
      };

      bool HaveLSize, HaveRSize;
      llvm::APInt LSize, RSize;
      std::tie(HaveLSize, LSize) = SizeFetch(LVAT, LCAT);
      std::tie(HaveRSize, RSize) = SizeFetch(RVAT, RCAT);
      if (HaveLSize && HaveRSize && !llvm::APInt::isSameValue(LSize, RSize))
        return {}; // Definite, but unequal, array dimension
    }

    if (LCAT && getCanonicalType(LHSElem) == getCanonicalType(ResultType))
      return LHS;
    if (RCAT && getCanonicalType(RHSElem) == getCanonicalType(ResultType))
      return RHS;
    if (LCAT)
      return getConstantArrayType(ResultType, LCAT->getSize(),
                                  LCAT->getSizeExpr(), ArraySizeModifier(), 0);
    if (RCAT)
      return getConstantArrayType(ResultType, RCAT->getSize(),
                                  RCAT->getSizeExpr(), ArraySizeModifier(), 0);
    if (LVAT && getCanonicalType(LHSElem) == getCanonicalType(ResultType))
      return LHS;
    if (RVAT && getCanonicalType(RHSElem) == getCanonicalType(ResultType))
      return RHS;
    if (LVAT) {
      // FIXME: This isn't correct! But tricky to implement because
      // the array's size has to be the size of LHS, but the type
      // has to be different.
      return LHS;
    }
    if (RVAT) {
      // FIXME: This isn't correct! But tricky to implement because
      // the array's size has to be the size of RHS, but the type
      // has to be different.
      return RHS;
    }
    if (getCanonicalType(LHSElem) == getCanonicalType(ResultType)) return LHS;
    if (getCanonicalType(RHSElem) == getCanonicalType(ResultType)) return RHS;
    return getIncompleteArrayType(ResultType, ArraySizeModifier(), 0);
  }
  case Type::FunctionNoProto:
    return mergeFunctionTypes(LHS, RHS, OfBlockPointer, Unqualified,
                              /*AllowCXX=*/false, IsConditionalOperator);
  case Type::Record:
  case Type::Enum:
    return {};
  case Type::Builtin:
    // Only exactly equal builtin types are compatible, which is tested above.
    return {};
  case Type::Complex:
    // Distinct complex types are incompatible.
    return {};
  case Type::Vector:
    // FIXME: The merged type should be an ExtVector!
    if (areCompatVectorTypes(LHSCan->castAs<VectorType>(),
                             RHSCan->castAs<VectorType>()))
      return LHS;
    return {};
  case Type::ConstantMatrix:
    if (areCompatMatrixTypes(LHSCan->castAs<ConstantMatrixType>(),
                             RHSCan->castAs<ConstantMatrixType>()))
      return LHS;
    return {};
  case Type::ObjCObject: {
    // Check if the types are assignment compatible.
    // FIXME: This should be type compatibility, e.g. whether
    // "LHS x; RHS x;" at global scope is legal.
    if (canAssignObjCInterfaces(LHS->castAs<ObjCObjectType>(),
                                RHS->castAs<ObjCObjectType>()))
      return LHS;
    return {};
  }
  case Type::ObjCObjectPointer:
    if (OfBlockPointer) {
      if (canAssignObjCInterfacesInBlockPointer(
              LHS->castAs<ObjCObjectPointerType>(),
              RHS->castAs<ObjCObjectPointerType>(), BlockReturnType))
        return LHS;
      return {};
    }
    if (canAssignObjCInterfaces(LHS->castAs<ObjCObjectPointerType>(),
                                RHS->castAs<ObjCObjectPointerType>()))
      return LHS;
    return {};
  case Type::Pipe:
    assert(LHS != RHS &&
           "Equivalent pipe types should have already been handled!");
    return {};
  case Type::ArrayParameter:
    assert(LHS != RHS &&
           "Equivalent ArrayParameter types should have already been handled!");
    return {};
  case Type::BitInt: {
    // Merge two bit-precise int types, while trying to preserve typedef info.
    bool LHSUnsigned = LHS->castAs<BitIntType>()->isUnsigned();
    bool RHSUnsigned = RHS->castAs<BitIntType>()->isUnsigned();
    unsigned LHSBits = LHS->castAs<BitIntType>()->getNumBits();
    unsigned RHSBits = RHS->castAs<BitIntType>()->getNumBits();

    // Like unsigned/int, shouldn't have a type if they don't match.
    if (LHSUnsigned != RHSUnsigned)
      return {};

    if (LHSBits != RHSBits)
      return {};
    return LHS;
  }
  }

  llvm_unreachable("Invalid Type::Class!");
}

bool ASTContext::mergeExtParameterInfo(
    const FunctionProtoType *FirstFnType, const FunctionProtoType *SecondFnType,
    bool &CanUseFirst, bool &CanUseSecond,
    SmallVectorImpl<FunctionProtoType::ExtParameterInfo> &NewParamInfos) {
  assert(NewParamInfos.empty() && "param info list not empty");
  CanUseFirst = CanUseSecond = true;
  bool FirstHasInfo = FirstFnType->hasExtParameterInfos();
  bool SecondHasInfo = SecondFnType->hasExtParameterInfos();

  // Fast path: if the first type doesn't have ext parameter infos,
  // we match if and only if the second type also doesn't have them.
  if (!FirstHasInfo && !SecondHasInfo)
    return true;

  bool NeedParamInfo = false;
  size_t E = FirstHasInfo ? FirstFnType->getExtParameterInfos().size()
                          : SecondFnType->getExtParameterInfos().size();

  for (size_t I = 0; I < E; ++I) {
    FunctionProtoType::ExtParameterInfo FirstParam, SecondParam;
    if (FirstHasInfo)
      FirstParam = FirstFnType->getExtParameterInfo(I);
    if (SecondHasInfo)
      SecondParam = SecondFnType->getExtParameterInfo(I);

    // Cannot merge unless everything except the noescape flag matches.
    if (FirstParam.withIsNoEscape(false) != SecondParam.withIsNoEscape(false))
      return false;

    bool FirstNoEscape = FirstParam.isNoEscape();
    bool SecondNoEscape = SecondParam.isNoEscape();
    bool IsNoEscape = FirstNoEscape && SecondNoEscape;
    NewParamInfos.push_back(FirstParam.withIsNoEscape(IsNoEscape));
    if (NewParamInfos.back().getOpaqueValue())
      NeedParamInfo = true;
    if (FirstNoEscape != IsNoEscape)
      CanUseFirst = false;
    if (SecondNoEscape != IsNoEscape)
      CanUseSecond = false;
  }

  if (!NeedParamInfo)
    NewParamInfos.clear();

  return true;
}

void ASTContext::ResetObjCLayout(const ObjCContainerDecl *CD) {
  ObjCLayouts[CD] = nullptr;
}

/// mergeObjCGCQualifiers - This routine merges ObjC's GC attribute of 'LHS' and
/// 'RHS' attributes and returns the merged version; including for function
/// return types.
QualType ASTContext::mergeObjCGCQualifiers(QualType LHS, QualType RHS) {
  QualType LHSCan = getCanonicalType(LHS),
  RHSCan = getCanonicalType(RHS);
  // If two types are identical, they are compatible.
  if (LHSCan == RHSCan)
    return LHS;
  if (RHSCan->isFunctionType()) {
    if (!LHSCan->isFunctionType())
      return {};
    QualType OldReturnType =
        cast<FunctionType>(RHSCan.getTypePtr())->getReturnType();
    QualType NewReturnType =
        cast<FunctionType>(LHSCan.getTypePtr())->getReturnType();
    QualType ResReturnType =
      mergeObjCGCQualifiers(NewReturnType, OldReturnType);
    if (ResReturnType.isNull())
      return {};
    if (ResReturnType == NewReturnType || ResReturnType == OldReturnType) {
      // id foo(); ... __strong id foo(); or: __strong id foo(); ... id foo();
      // In either case, use OldReturnType to build the new function type.
      const auto *F = LHS->castAs<FunctionType>();
      if (const auto *FPT = cast<FunctionProtoType>(F)) {
        FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
        EPI.ExtInfo = getFunctionExtInfo(LHS);
        QualType ResultType =
            getFunctionType(OldReturnType, FPT->getParamTypes(), EPI);
        return ResultType;
      }
    }
    return {};
  }

  // If the qualifiers are different, the types can still be merged.
  Qualifiers LQuals = LHSCan.getLocalQualifiers();
  Qualifiers RQuals = RHSCan.getLocalQualifiers();
  if (LQuals != RQuals) {
    // If any of these qualifiers are different, we have a type mismatch.
    if (LQuals.getCVRQualifiers() != RQuals.getCVRQualifiers() ||
        LQuals.getAddressSpace() != RQuals.getAddressSpace())
      return {};

    // Exactly one GC qualifier difference is allowed: __strong is
    // okay if the other type has no GC qualifier but is an Objective
    // C object pointer (i.e. implicitly strong by default).  We fix
    // this by pretending that the unqualified type was actually
    // qualified __strong.
    Qualifiers::GC GC_L = LQuals.getObjCGCAttr();
    Qualifiers::GC GC_R = RQuals.getObjCGCAttr();
    assert((GC_L != GC_R) && "unequal qualifier sets had only equal elements");

    if (GC_L == Qualifiers::Weak || GC_R == Qualifiers::Weak)
      return {};

    if (GC_L == Qualifiers::Strong)
      return LHS;
    if (GC_R == Qualifiers::Strong)
      return RHS;
    return {};
  }

  if (LHSCan->isObjCObjectPointerType() && RHSCan->isObjCObjectPointerType()) {
    QualType LHSBaseQT = LHS->castAs<ObjCObjectPointerType>()->getPointeeType();
    QualType RHSBaseQT = RHS->castAs<ObjCObjectPointerType>()->getPointeeType();
    QualType ResQT = mergeObjCGCQualifiers(LHSBaseQT, RHSBaseQT);
    if (ResQT == LHSBaseQT)
      return LHS;
    if (ResQT == RHSBaseQT)
      return RHS;
  }
  return {};
}

//===----------------------------------------------------------------------===//
//                         Integer Predicates
//===----------------------------------------------------------------------===//

unsigned ASTContext::getIntWidth(QualType T) const {
  if (const auto *ET = T->getAs<EnumType>())
    T = ET->getDecl()->getIntegerType();
  if (T->isBooleanType())
    return 1;
  if (const auto *EIT = T->getAs<BitIntType>())
    return EIT->getNumBits();
  // For builtin types, just use the standard type sizing method
  return (unsigned)getTypeSize(T);
}

QualType ASTContext::getCorrespondingUnsignedType(QualType T) const {
  assert((T->hasIntegerRepresentation() || T->isEnumeralType() ||
          T->isFixedPointType()) &&
         "Unexpected type");

  // Turn <4 x signed int> -> <4 x unsigned int>
  if (const auto *VTy = T->getAs<VectorType>())
    return getVectorType(getCorrespondingUnsignedType(VTy->getElementType()),
                         VTy->getNumElements(), VTy->getVectorKind());

  // For _BitInt, return an unsigned _BitInt with same width.
  if (const auto *EITy = T->getAs<BitIntType>())
    return getBitIntType(/*Unsigned=*/true, EITy->getNumBits());

  // For enums, get the underlying integer type of the enum, and let the general
  // integer type signchanging code handle it.
  if (const auto *ETy = T->getAs<EnumType>())
    T = ETy->getDecl()->getIntegerType();

  switch (T->castAs<BuiltinType>()->getKind()) {
  case BuiltinType::Char_U:
    // Plain `char` is mapped to `unsigned char` even if it's already unsigned
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
  case BuiltinType::Char8:
    return UnsignedCharTy;
  case BuiltinType::Short:
    return UnsignedShortTy;
  case BuiltinType::Int:
    return UnsignedIntTy;
  case BuiltinType::Long:
    return UnsignedLongTy;
  case BuiltinType::LongLong:
    return UnsignedLongLongTy;
  case BuiltinType::Int128:
    return UnsignedInt128Ty;
  // wchar_t is special. It is either signed or not, but when it's signed,
  // there's no matching "unsigned wchar_t". Therefore we return the unsigned
  // version of its underlying type instead.
  case BuiltinType::WChar_S:
    return getUnsignedWCharType();

  case BuiltinType::ShortAccum:
    return UnsignedShortAccumTy;
  case BuiltinType::Accum:
    return UnsignedAccumTy;
  case BuiltinType::LongAccum:
    return UnsignedLongAccumTy;
  case BuiltinType::SatShortAccum:
    return SatUnsignedShortAccumTy;
  case BuiltinType::SatAccum:
    return SatUnsignedAccumTy;
  case BuiltinType::SatLongAccum:
    return SatUnsignedLongAccumTy;
  case BuiltinType::ShortFract:
    return UnsignedShortFractTy;
  case BuiltinType::Fract:
    return UnsignedFractTy;
  case BuiltinType::LongFract:
    return UnsignedLongFractTy;
  case BuiltinType::SatShortFract:
    return SatUnsignedShortFractTy;
  case BuiltinType::SatFract:
    return SatUnsignedFractTy;
  case BuiltinType::SatLongFract:
    return SatUnsignedLongFractTy;
  default:
    assert((T->hasUnsignedIntegerRepresentation() ||
            T->isUnsignedFixedPointType()) &&
           "Unexpected signed integer or fixed point type");
    return T;
  }
}

QualType ASTContext::getCorrespondingSignedType(QualType T) const {
  assert((T->hasIntegerRepresentation() || T->isEnumeralType() ||
          T->isFixedPointType()) &&
         "Unexpected type");

  // Turn <4 x unsigned int> -> <4 x signed int>
  if (const auto *VTy = T->getAs<VectorType>())
    return getVectorType(getCorrespondingSignedType(VTy->getElementType()),
                         VTy->getNumElements(), VTy->getVectorKind());

  // For _BitInt, return a signed _BitInt with same width.
  if (const auto *EITy = T->getAs<BitIntType>())
    return getBitIntType(/*Unsigned=*/false, EITy->getNumBits());

  // For enums, get the underlying integer type of the enum, and let the general
  // integer type signchanging code handle it.
  if (const auto *ETy = T->getAs<EnumType>())
    T = ETy->getDecl()->getIntegerType();

  switch (T->castAs<BuiltinType>()->getKind()) {
  case BuiltinType::Char_S:
    // Plain `char` is mapped to `signed char` even if it's already signed
  case BuiltinType::Char_U:
  case BuiltinType::UChar:
  case BuiltinType::Char8:
    return SignedCharTy;
  case BuiltinType::UShort:
    return ShortTy;
  case BuiltinType::UInt:
    return IntTy;
  case BuiltinType::ULong:
    return LongTy;
  case BuiltinType::ULongLong:
    return LongLongTy;
  case BuiltinType::UInt128:
    return Int128Ty;
  // wchar_t is special. It is either unsigned or not, but when it's unsigned,
  // there's no matching "signed wchar_t". Therefore we return the signed
  // version of its underlying type instead.
  case BuiltinType::WChar_U:
    return getSignedWCharType();

  case BuiltinType::UShortAccum:
    return ShortAccumTy;
  case BuiltinType::UAccum:
    return AccumTy;
  case BuiltinType::ULongAccum:
    return LongAccumTy;
  case BuiltinType::SatUShortAccum:
    return SatShortAccumTy;
  case BuiltinType::SatUAccum:
    return SatAccumTy;
  case BuiltinType::SatULongAccum:
    return SatLongAccumTy;
  case BuiltinType::UShortFract:
    return ShortFractTy;
  case BuiltinType::UFract:
    return FractTy;
  case BuiltinType::ULongFract:
    return LongFractTy;
  case BuiltinType::SatUShortFract:
    return SatShortFractTy;
  case BuiltinType::SatUFract:
    return SatFractTy;
  case BuiltinType::SatULongFract:
    return SatLongFractTy;
  default:
    assert(
        (T->hasSignedIntegerRepresentation() || T->isSignedFixedPointType()) &&
        "Unexpected signed integer or fixed point type");
    return T;
  }
}

ASTMutationListener::~ASTMutationListener() = default;

void ASTMutationListener::DeducedReturnType(const FunctionDecl *FD,
                                            QualType ReturnType) {}

//===----------------------------------------------------------------------===//
//                          Builtin Type Computation
//===----------------------------------------------------------------------===//

/// DecodeTypeFromStr - This decodes one type descriptor from Str, advancing the
/// pointer over the consumed characters.  This returns the resultant type.  If
/// AllowTypeModifiers is false then modifier like * are not parsed, just basic
/// types.  This allows "v2i*" to be parsed as a pointer to a v2i instead of
/// a vector of "i*".
///
/// RequiresICE is filled in on return to indicate whether the value is required
/// to be an Integer Constant Expression.
static QualType DecodeTypeFromStr(const char *&Str, const ASTContext &Context,
                                  ASTContext::GetBuiltinTypeError &Error,
                                  bool &RequiresICE,
                                  bool AllowTypeModifiers) {
  // Modifiers.
  int HowLong = 0;
  bool Signed = false, Unsigned = false;
  RequiresICE = false;

  // Read the prefixed modifiers first.
  bool Done = false;
  #ifndef NDEBUG
  bool IsSpecial = false;
  #endif
  while (!Done) {
    switch (*Str++) {
    default: Done = true; --Str; break;
    case 'I':
      RequiresICE = true;
      break;
    case 'S':
      assert(!Unsigned && "Can't use both 'S' and 'U' modifiers!");
      assert(!Signed && "Can't use 'S' modifier multiple times!");
      Signed = true;
      break;
    case 'U':
      assert(!Signed && "Can't use both 'S' and 'U' modifiers!");
      assert(!Unsigned && "Can't use 'U' modifier multiple times!");
      Unsigned = true;
      break;
    case 'L':
      assert(!IsSpecial && "Can't use 'L' with 'W', 'N', 'Z' or 'O' modifiers");
      assert(HowLong <= 2 && "Can't have LLLL modifier");
      ++HowLong;
      break;
    case 'N':
      // 'N' behaves like 'L' for all non LP64 targets and 'int' otherwise.
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'N' modifiers!");
      #ifndef NDEBUG
      IsSpecial = true;
      #endif
      if (Context.getTargetInfo().getLongWidth() == 32)
        ++HowLong;
      break;
    case 'W':
      // This modifier represents int64 type.
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'W' modifiers!");
      #ifndef NDEBUG
      IsSpecial = true;
      #endif
      switch (Context.getTargetInfo().getInt64Type()) {
      default:
        llvm_unreachable("Unexpected integer type");
      case TargetInfo::SignedLong:
        HowLong = 1;
        break;
      case TargetInfo::SignedLongLong:
        HowLong = 2;
        break;
      }
      break;
    case 'Z':
      // This modifier represents int32 type.
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'Z' modifiers!");
      #ifndef NDEBUG
      IsSpecial = true;
      #endif
      switch (Context.getTargetInfo().getIntTypeByWidth(32, true)) {
      default:
        llvm_unreachable("Unexpected integer type");
      case TargetInfo::SignedInt:
        HowLong = 0;
        break;
      case TargetInfo::SignedLong:
        HowLong = 1;
        break;
      case TargetInfo::SignedLongLong:
        HowLong = 2;
        break;
      }
      break;
    case 'O':
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'O' modifiers!");
      #ifndef NDEBUG
      IsSpecial = true;
      #endif
      if (Context.getLangOpts().OpenCL)
        HowLong = 1;
      else
        HowLong = 2;
      break;
    }
  }

  QualType Type;

  // Read the base type.
  switch (*Str++) {
  default: llvm_unreachable("Unknown builtin type letter!");
  case 'x':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'x'!");
    Type = Context.Float16Ty;
    break;
  case 'y':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'y'!");
    Type = Context.BFloat16Ty;
    break;
  case 'v':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'v'!");
    Type = Context.VoidTy;
    break;
  case 'h':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'h'!");
    Type = Context.HalfTy;
    break;
  case 'f':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'f'!");
    Type = Context.FloatTy;
    break;
  case 'd':
    assert(HowLong < 3 && !Signed && !Unsigned &&
           "Bad modifiers used with 'd'!");
    if (HowLong == 1)
      Type = Context.LongDoubleTy;
    else if (HowLong == 2)
      Type = Context.Float128Ty;
    else
      Type = Context.DoubleTy;
    break;
  case 's':
    assert(HowLong == 0 && "Bad modifiers used with 's'!");
    if (Unsigned)
      Type = Context.UnsignedShortTy;
    else
      Type = Context.ShortTy;
    break;
  case 'i':
    if (HowLong == 3)
      Type = Unsigned ? Context.UnsignedInt128Ty : Context.Int128Ty;
    else if (HowLong == 2)
      Type = Unsigned ? Context.UnsignedLongLongTy : Context.LongLongTy;
    else if (HowLong == 1)
      Type = Unsigned ? Context.UnsignedLongTy : Context.LongTy;
    else
      Type = Unsigned ? Context.UnsignedIntTy : Context.IntTy;
    break;
  case 'c':
    assert(HowLong == 0 && "Bad modifiers used with 'c'!");
    if (Signed)
      Type = Context.SignedCharTy;
    else if (Unsigned)
      Type = Context.UnsignedCharTy;
    else
      Type = Context.CharTy;
    break;
  case 'b': // boolean
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'b'!");
    Type = Context.BoolTy;
    break;
  case 'z':  // size_t.
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'z'!");
    Type = Context.getSizeType();
    break;
  case 'w':  // wchar_t.
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'w'!");
    Type = Context.getWideCharType();
    break;
  case 'F':
    Type = Context.getCFConstantStringType();
    break;
  case 'G':
    Type = Context.getObjCIdType();
    break;
  case 'H':
    Type = Context.getObjCSelType();
    break;
  case 'M':
    Type = Context.getObjCSuperType();
    break;
  case 'a':
    Type = Context.getBuiltinVaListType();
    assert(!Type.isNull() && "builtin va list type not initialized!");
    break;
  case 'A':
    // This is a "reference" to a va_list; however, what exactly
    // this means depends on how va_list is defined. There are two
    // different kinds of va_list: ones passed by value, and ones
    // passed by reference.  An example of a by-value va_list is
    // x86, where va_list is a char*. An example of by-ref va_list
    // is x86-64, where va_list is a __va_list_tag[1]. For x86,
    // we want this argument to be a char*&; for x86-64, we want
    // it to be a __va_list_tag*.
    Type = Context.getBuiltinVaListType();
    assert(!Type.isNull() && "builtin va list type not initialized!");
    if (Type->isArrayType())
      Type = Context.getArrayDecayedType(Type);
    else
      Type = Context.getLValueReferenceType(Type);
    break;
  case 'q': {
    char *End;
    unsigned NumElements = strtoul(Str, &End, 10);
    assert(End != Str && "Missing vector size");
    Str = End;

    QualType ElementType = DecodeTypeFromStr(Str, Context, Error,
                                             RequiresICE, false);
    assert(!RequiresICE && "Can't require vector ICE");

    Type = Context.getScalableVectorType(ElementType, NumElements);
    break;
  }
  case 'Q': {
    switch (*Str++) {
    case 'a': {
      Type = Context.SveCountTy;
      break;
    }
    case 'b': {
      Type = Context.AMDGPUBufferRsrcTy;
      break;
    }
    default:
      llvm_unreachable("Unexpected target builtin type");
    }
    break;
  }
  case 'V': {
    char *End;
    unsigned NumElements = strtoul(Str, &End, 10);
    assert(End != Str && "Missing vector size");
    Str = End;

    QualType ElementType = DecodeTypeFromStr(Str, Context, Error,
                                             RequiresICE, false);
    assert(!RequiresICE && "Can't require vector ICE");

    // TODO: No way to make AltiVec vectors in builtins yet.
    Type = Context.getVectorType(ElementType, NumElements, VectorKind::Generic);
    break;
  }
  case 'E': {
    char *End;

    unsigned NumElements = strtoul(Str, &End, 10);
    assert(End != Str && "Missing vector size");

    Str = End;

    QualType ElementType = DecodeTypeFromStr(Str, Context, Error, RequiresICE,
                                             false);
    Type = Context.getExtVectorType(ElementType, NumElements);
    break;
  }
  case 'X': {
    QualType ElementType = DecodeTypeFromStr(Str, Context, Error, RequiresICE,
                                             false);
    assert(!RequiresICE && "Can't require complex ICE");
    Type = Context.getComplexType(ElementType);
    break;
  }
  case 'Y':
    Type = Context.getPointerDiffType();
    break;
  case 'P':
    Type = Context.getFILEType();
    if (Type.isNull()) {
      Error = ASTContext::GE_Missing_stdio;
      return {};
    }
    break;
  case 'J':
    if (Signed)
      Type = Context.getsigjmp_bufType();
    else
      Type = Context.getjmp_bufType();

    if (Type.isNull()) {
      Error = ASTContext::GE_Missing_setjmp;
      return {};
    }
    break;
  case 'K':
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'K'!");
    Type = Context.getucontext_tType();

    if (Type.isNull()) {
      Error = ASTContext::GE_Missing_ucontext;
      return {};
    }
    break;
  case 'p':
    Type = Context.getProcessIDType();
    break;
  }

  // If there are modifiers and if we're allowed to parse them, go for it.
  Done = !AllowTypeModifiers;
  while (!Done) {
    switch (char c = *Str++) {
    default: Done = true; --Str; break;
    case '*':
    case '&': {
      // Both pointers and references can have their pointee types
      // qualified with an address space.
      char *End;
      unsigned AddrSpace = strtoul(Str, &End, 10);
      if (End != Str) {
        // Note AddrSpace == 0 is not the same as an unspecified address space.
        Type = Context.getAddrSpaceQualType(
          Type,
          Context.getLangASForBuiltinAddressSpace(AddrSpace));
        Str = End;
      }
      if (c == '*')
        Type = Context.getPointerType(Type);
      else
        Type = Context.getLValueReferenceType(Type);
      break;
    }
    // FIXME: There's no way to have a built-in with an rvalue ref arg.
    case 'C':
      Type = Type.withConst();
      break;
    case 'D':
      Type = Context.getVolatileType(Type);
      break;
    case 'R':
      Type = Type.withRestrict();
      break;
    }
  }

  assert((!RequiresICE || Type->isIntegralOrEnumerationType()) &&
         "Integer constant 'I' type must be an integer");

  return Type;
}

// On some targets such as PowerPC, some of the builtins are defined with custom
// type descriptors for target-dependent types. These descriptors are decoded in
// other functions, but it may be useful to be able to fall back to default
// descriptor decoding to define builtins mixing target-dependent and target-
// independent types. This function allows decoding one type descriptor with
// default decoding.
QualType ASTContext::DecodeTypeStr(const char *&Str, const ASTContext &Context,
                                   GetBuiltinTypeError &Error, bool &RequireICE,
                                   bool AllowTypeModifiers) const {
  return DecodeTypeFromStr(Str, Context, Error, RequireICE, AllowTypeModifiers);
}

/// GetBuiltinType - Return the type for the specified builtin.
QualType ASTContext::GetBuiltinType(unsigned Id,
                                    GetBuiltinTypeError &Error,
                                    unsigned *IntegerConstantArgs) const {
  const char *TypeStr = BuiltinInfo.getTypeString(Id);
  if (TypeStr[0] == '\0') {
    Error = GE_Missing_type;
    return {};
  }

  SmallVector<QualType, 8> ArgTypes;

  bool RequiresICE = false;
  Error = GE_None;
  QualType ResType = DecodeTypeFromStr(TypeStr, *this, Error,
                                       RequiresICE, true);
  if (Error != GE_None)
    return {};

  assert(!RequiresICE && "Result of intrinsic cannot be required to be an ICE");

  while (TypeStr[0] && TypeStr[0] != '.') {
    QualType Ty = DecodeTypeFromStr(TypeStr, *this, Error, RequiresICE, true);
    if (Error != GE_None)
      return {};

    // If this argument is required to be an IntegerConstantExpression and the
    // caller cares, fill in the bitmask we return.
    if (RequiresICE && IntegerConstantArgs)
      *IntegerConstantArgs |= 1 << ArgTypes.size();

    // Do array -> pointer decay.  The builtin should use the decayed type.
    if (Ty->isArrayType())
      Ty = getArrayDecayedType(Ty);

    ArgTypes.push_back(Ty);
  }

  if (Id == Builtin::BI__GetExceptionInfo)
    return {};

  assert((TypeStr[0] != '.' || TypeStr[1] == 0) &&
         "'.' should only occur at end of builtin type list!");

  bool Variadic = (TypeStr[0] == '.');

  FunctionType::ExtInfo EI(getDefaultCallingConvention(
      Variadic, /*IsCXXMethod=*/false, /*IsBuiltin=*/true));
  if (BuiltinInfo.isNoReturn(Id)) EI = EI.withNoReturn(true);


  // We really shouldn't be making a no-proto type here.
  if (ArgTypes.empty() && Variadic && !getLangOpts().requiresStrictPrototypes())
    return getFunctionNoProtoType(ResType, EI);

  FunctionProtoType::ExtProtoInfo EPI;
  EPI.ExtInfo = EI;
  EPI.Variadic = Variadic;
  if (getLangOpts().CPlusPlus && BuiltinInfo.isNoThrow(Id))
    EPI.ExceptionSpec.Type =
        getLangOpts().CPlusPlus11 ? EST_BasicNoexcept : EST_DynamicNone;

  return getFunctionType(ResType, ArgTypes, EPI);
}

static GVALinkage basicGVALinkageForFunction(const ASTContext &Context,
                                             const FunctionDecl *FD) {
  if (!FD->isExternallyVisible())
    return GVA_Internal;

  // Non-user-provided functions get emitted as weak definitions with every
  // use, no matter whether they've been explicitly instantiated etc.
  if (!FD->isUserProvided())
    return GVA_DiscardableODR;

  GVALinkage External;
  switch (FD->getTemplateSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ExplicitSpecialization:
    External = GVA_StrongExternal;
    break;

  case TSK_ExplicitInstantiationDefinition:
    return GVA_StrongODR;

  // C++11 [temp.explicit]p10:
  //   [ Note: The intent is that an inline function that is the subject of
  //   an explicit instantiation declaration will still be implicitly
  //   instantiated when used so that the body can be considered for
  //   inlining, but that no out-of-line copy of the inline function would be
  //   generated in the translation unit. -- end note ]
  case TSK_ExplicitInstantiationDeclaration:
    return GVA_AvailableExternally;

  case TSK_ImplicitInstantiation:
    External = GVA_DiscardableODR;
    break;
  }

  if (!FD->isInlined())
    return External;

  if ((!Context.getLangOpts().CPlusPlus &&
       !Context.getTargetInfo().getCXXABI().isMicrosoft() &&
       !FD->hasAttr<DLLExportAttr>()) ||
      FD->hasAttr<GNUInlineAttr>()) {
    // FIXME: This doesn't match gcc's behavior for dllexport inline functions.

    // GNU or C99 inline semantics. Determine whether this symbol should be
    // externally visible.
    if (FD->isInlineDefinitionExternallyVisible())
      return External;

    // C99 inline semantics, where the symbol is not externally visible.
    return GVA_AvailableExternally;
  }

  // Functions specified with extern and inline in -fms-compatibility mode
  // forcibly get emitted.  While the body of the function cannot be later
  // replaced, the function definition cannot be discarded.
  if (FD->isMSExternInline())
    return GVA_StrongODR;

  if (Context.getTargetInfo().getCXXABI().isMicrosoft() &&
      isa<CXXConstructorDecl>(FD) &&
      cast<CXXConstructorDecl>(FD)->isInheritingConstructor())
    // Our approach to inheriting constructors is fundamentally different from
    // that used by the MS ABI, so keep our inheriting constructor thunks
    // internal rather than trying to pick an unambiguous mangling for them.
    return GVA_Internal;

  return GVA_DiscardableODR;
}

static GVALinkage adjustGVALinkageForAttributes(const ASTContext &Context,
                                                const Decl *D, GVALinkage L) {
  // See http://msdn.microsoft.com/en-us/library/xa0d9ste.aspx
  // dllexport/dllimport on inline functions.
  if (D->hasAttr<DLLImportAttr>()) {
    if (L == GVA_DiscardableODR || L == GVA_StrongODR)
      return GVA_AvailableExternally;
  } else if (D->hasAttr<DLLExportAttr>()) {
    if (L == GVA_DiscardableODR)
      return GVA_StrongODR;
  } else if (Context.getLangOpts().CUDA && Context.getLangOpts().CUDAIsDevice) {
    // Device-side functions with __global__ attribute must always be
    // visible externally so they can be launched from host.
    if (D->hasAttr<CUDAGlobalAttr>() &&
        (L == GVA_DiscardableODR || L == GVA_Internal))
      return GVA_StrongODR;
    // Single source offloading languages like CUDA/HIP need to be able to
    // access static device variables from host code of the same compilation
    // unit. This is done by externalizing the static variable with a shared
    // name between the host and device compilation which is the same for the
    // same compilation unit whereas different among different compilation
    // units.
    if (Context.shouldExternalize(D))
      return GVA_StrongExternal;
  }
  return L;
}

/// Adjust the GVALinkage for a declaration based on what an external AST source
/// knows about whether there can be other definitions of this declaration.
static GVALinkage
adjustGVALinkageForExternalDefinitionKind(const ASTContext &Ctx, const Decl *D,
                                          GVALinkage L) {
  ExternalASTSource *Source = Ctx.getExternalSource();
  if (!Source)
    return L;

  switch (Source->hasExternalDefinitions(D)) {
  case ExternalASTSource::EK_Never:
    // Other translation units rely on us to provide the definition.
    if (L == GVA_DiscardableODR)
      return GVA_StrongODR;
    break;

  case ExternalASTSource::EK_Always:
    return GVA_AvailableExternally;

  case ExternalASTSource::EK_ReplyHazy:
    break;
  }
  return L;
}

GVALinkage ASTContext::GetGVALinkageForFunction(const FunctionDecl *FD) const {
  return adjustGVALinkageForExternalDefinitionKind(*this, FD,
           adjustGVALinkageForAttributes(*this, FD,
             basicGVALinkageForFunction(*this, FD)));
}

static GVALinkage basicGVALinkageForVariable(const ASTContext &Context,
                                             const VarDecl *VD) {
  // As an extension for interactive REPLs, make sure constant variables are
  // only emitted once instead of LinkageComputer::getLVForNamespaceScopeDecl
  // marking them as internal.
  if (Context.getLangOpts().CPlusPlus &&
      Context.getLangOpts().IncrementalExtensions &&
      VD->getType().isConstQualified() &&
      !VD->getType().isVolatileQualified() && !VD->isInline() &&
      !isa<VarTemplateSpecializationDecl>(VD) && !VD->getDescribedVarTemplate())
    return GVA_DiscardableODR;

  if (!VD->isExternallyVisible())
    return GVA_Internal;

  if (VD->isStaticLocal()) {
    const DeclContext *LexicalContext = VD->getParentFunctionOrMethod();
    while (LexicalContext && !isa<FunctionDecl>(LexicalContext))
      LexicalContext = LexicalContext->getLexicalParent();

    // ObjC Blocks can create local variables that don't have a FunctionDecl
    // LexicalContext.
    if (!LexicalContext)
      return GVA_DiscardableODR;

    // Otherwise, let the static local variable inherit its linkage from the
    // nearest enclosing function.
    auto StaticLocalLinkage =
        Context.GetGVALinkageForFunction(cast<FunctionDecl>(LexicalContext));

    // Itanium ABI 5.2.2: "Each COMDAT group [for a static local variable] must
    // be emitted in any object with references to the symbol for the object it
    // contains, whether inline or out-of-line."
    // Similar behavior is observed with MSVC. An alternative ABI could use
    // StrongODR/AvailableExternally to match the function, but none are
    // known/supported currently.
    if (StaticLocalLinkage == GVA_StrongODR ||
        StaticLocalLinkage == GVA_AvailableExternally)
      return GVA_DiscardableODR;
    return StaticLocalLinkage;
  }

  // MSVC treats in-class initialized static data members as definitions.
  // By giving them non-strong linkage, out-of-line definitions won't
  // cause link errors.
  if (Context.isMSStaticDataMemberInlineDefinition(VD))
    return GVA_DiscardableODR;

  // Most non-template variables have strong linkage; inline variables are
  // linkonce_odr or (occasionally, for compatibility) weak_odr.
  GVALinkage StrongLinkage;
  switch (Context.getInlineVariableDefinitionKind(VD)) {
  case ASTContext::InlineVariableDefinitionKind::None:
    StrongLinkage = GVA_StrongExternal;
    break;
  case ASTContext::InlineVariableDefinitionKind::Weak:
  case ASTContext::InlineVariableDefinitionKind::WeakUnknown:
    StrongLinkage = GVA_DiscardableODR;
    break;
  case ASTContext::InlineVariableDefinitionKind::Strong:
    StrongLinkage = GVA_StrongODR;
    break;
  }

  switch (VD->getTemplateSpecializationKind()) {
  case TSK_Undeclared:
    return StrongLinkage;

  case TSK_ExplicitSpecialization:
    return Context.getTargetInfo().getCXXABI().isMicrosoft() &&
                   VD->isStaticDataMember()
               ? GVA_StrongODR
               : StrongLinkage;

  case TSK_ExplicitInstantiationDefinition:
    return GVA_StrongODR;

  case TSK_ExplicitInstantiationDeclaration:
    return GVA_AvailableExternally;

  case TSK_ImplicitInstantiation:
    return GVA_DiscardableODR;
  }

  llvm_unreachable("Invalid Linkage!");
}

GVALinkage ASTContext::GetGVALinkageForVariable(const VarDecl *VD) const {
  return adjustGVALinkageForExternalDefinitionKind(*this, VD,
           adjustGVALinkageForAttributes(*this, VD,
             basicGVALinkageForVariable(*this, VD)));
}

bool ASTContext::DeclMustBeEmitted(const Decl *D) {
  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (!VD->isFileVarDecl())
      return false;
    // Global named register variables (GNU extension) are never emitted.
    if (VD->getStorageClass() == SC_Register)
      return false;
    if (VD->getDescribedVarTemplate() ||
        isa<VarTemplatePartialSpecializationDecl>(VD))
      return false;
  } else if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    // We never need to emit an uninstantiated function template.
    if (FD->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
      return false;
  } else if (isa<PragmaCommentDecl>(D))
    return true;
  else if (isa<PragmaDetectMismatchDecl>(D))
    return true;
  else if (isa<OMPRequiresDecl>(D))
    return true;
  else if (isa<OMPThreadPrivateDecl>(D))
    return !D->getDeclContext()->isDependentContext();
  else if (isa<OMPAllocateDecl>(D))
    return !D->getDeclContext()->isDependentContext();
  else if (isa<OMPDeclareReductionDecl>(D) || isa<OMPDeclareMapperDecl>(D))
    return !D->getDeclContext()->isDependentContext();
  else if (isa<ImportDecl>(D))
    return true;
  else
    return false;

  // If this is a member of a class template, we do not need to emit it.
  if (D->getDeclContext()->isDependentContext())
    return false;

  // Weak references don't produce any output by themselves.
  if (D->hasAttr<WeakRefAttr>())
    return false;

  // Aliases and used decls are required.
  if (D->hasAttr<AliasAttr>() || D->hasAttr<UsedAttr>())
    return true;

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    // Forward declarations aren't required.
    if (!FD->doesThisDeclarationHaveABody())
      return FD->doesDeclarationForceExternallyVisibleDefinition();

    // Constructors and destructors are required.
    if (FD->hasAttr<ConstructorAttr>() || FD->hasAttr<DestructorAttr>())
      return true;

    // The key function for a class is required.  This rule only comes
    // into play when inline functions can be key functions, though.
    if (getTargetInfo().getCXXABI().canKeyFunctionBeInline()) {
      if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
        const CXXRecordDecl *RD = MD->getParent();
        if (MD->isOutOfLine() && RD->isDynamicClass()) {
          const CXXMethodDecl *KeyFunc = getCurrentKeyFunction(RD);
          if (KeyFunc && KeyFunc->getCanonicalDecl() == MD->getCanonicalDecl())
            return true;
        }
      }
    }

    GVALinkage Linkage = GetGVALinkageForFunction(FD);

    // static, static inline, always_inline, and extern inline functions can
    // always be deferred.  Normal inline functions can be deferred in C99/C++.
    // Implicit template instantiations can also be deferred in C++.
    return !isDiscardableGVALinkage(Linkage);
  }

  const auto *VD = cast<VarDecl>(D);
  assert(VD->isFileVarDecl() && "Expected file scoped var");

  // If the decl is marked as `declare target to`, it should be emitted for the
  // host and for the device.
  if (LangOpts.OpenMP &&
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD))
    return true;

  if (VD->isThisDeclarationADefinition() == VarDecl::DeclarationOnly &&
      !isMSStaticDataMemberInlineDefinition(VD))
    return false;

  if (VD->shouldEmitInExternalSource())
    return false;

  // Variables that can be needed in other TUs are required.
  auto Linkage = GetGVALinkageForVariable(VD);
  if (!isDiscardableGVALinkage(Linkage))
    return true;

  // We never need to emit a variable that is available in another TU.
  if (Linkage == GVA_AvailableExternally)
    return false;

  // Variables that have destruction with side-effects are required.
  if (VD->needsDestruction(*this))
    return true;

  // Variables that have initialization with side-effects are required.
  if (VD->getInit() && VD->getInit()->HasSideEffects(*this) &&
      // We can get a value-dependent initializer during error recovery.
      (VD->getInit()->isValueDependent() || !VD->evaluateValue()))
    return true;

  // Likewise, variables with tuple-like bindings are required if their
  // bindings have side-effects.
  if (const auto *DD = dyn_cast<DecompositionDecl>(VD))
    for (const auto *BD : DD->bindings())
      if (const auto *BindingVD = BD->getHoldingVar())
        if (DeclMustBeEmitted(BindingVD))
          return true;

  return false;
}

void ASTContext::forEachMultiversionedFunctionVersion(
    const FunctionDecl *FD,
    llvm::function_ref<void(FunctionDecl *)> Pred) const {
  assert(FD->isMultiVersion() && "Only valid for multiversioned functions");
  llvm::SmallDenseSet<const FunctionDecl*, 4> SeenDecls;
  FD = FD->getMostRecentDecl();
  // FIXME: The order of traversal here matters and depends on the order of
  // lookup results, which happens to be (mostly) oldest-to-newest, but we
  // shouldn't rely on that.
  for (auto *CurDecl :
       FD->getDeclContext()->getRedeclContext()->lookup(FD->getDeclName())) {
    FunctionDecl *CurFD = CurDecl->getAsFunction()->getMostRecentDecl();
    if (CurFD && hasSameType(CurFD->getType(), FD->getType()) &&
        !SeenDecls.contains(CurFD)) {
      SeenDecls.insert(CurFD);
      Pred(CurFD);
    }
  }
}

CallingConv ASTContext::getDefaultCallingConvention(bool IsVariadic,
                                                    bool IsCXXMethod,
                                                    bool IsBuiltin) const {
  // Pass through to the C++ ABI object
  if (IsCXXMethod)
    return ABI->getDefaultMethodCallConv(IsVariadic);

  // Builtins ignore user-specified default calling convention and remain the
  // Target's default calling convention.
  if (!IsBuiltin) {
    switch (LangOpts.getDefaultCallingConv()) {
    case LangOptions::DCC_None:
      break;
    case LangOptions::DCC_CDecl:
      return CC_C;
    case LangOptions::DCC_FastCall:
      if (getTargetInfo().hasFeature("sse2") && !IsVariadic)
        return CC_X86FastCall;
      break;
    case LangOptions::DCC_StdCall:
      if (!IsVariadic)
        return CC_X86StdCall;
      break;
    case LangOptions::DCC_VectorCall:
      // __vectorcall cannot be applied to variadic functions.
      if (!IsVariadic)
        return CC_X86VectorCall;
      break;
    case LangOptions::DCC_RegCall:
      // __regcall cannot be applied to variadic functions.
      if (!IsVariadic)
        return CC_X86RegCall;
      break;
    case LangOptions::DCC_RtdCall:
      if (!IsVariadic)
        return CC_M68kRTD;
      break;
    }
  }
  return Target->getDefaultCallingConv();
}

bool ASTContext::isNearlyEmpty(const CXXRecordDecl *RD) const {
  // Pass through to the C++ ABI object
  return ABI->isNearlyEmpty(RD);
}

VTableContextBase *ASTContext::getVTableContext() {
  if (!VTContext.get()) {
    auto ABI = Target->getCXXABI();
    if (ABI.isMicrosoft())
      VTContext.reset(new MicrosoftVTableContext(*this));
    else {
      auto ComponentLayout = getLangOpts().RelativeCXXABIVTables
                                 ? ItaniumVTableContext::Relative
                                 : ItaniumVTableContext::Pointer;
      VTContext.reset(new ItaniumVTableContext(*this, ComponentLayout));
    }
  }
  return VTContext.get();
}

MangleContext *ASTContext::createMangleContext(const TargetInfo *T) {
  if (!T)
    T = Target;
  switch (T->getCXXABI().getKind()) {
  case TargetCXXABI::AppleARM64:
  case TargetCXXABI::Fuchsia:
  case TargetCXXABI::GenericAArch64:
  case TargetCXXABI::GenericItanium:
  case TargetCXXABI::GenericARM:
  case TargetCXXABI::GenericMIPS:
  case TargetCXXABI::iOS:
  case TargetCXXABI::WebAssembly:
  case TargetCXXABI::WatchOS:
  case TargetCXXABI::XL:
    return ItaniumMangleContext::create(*this, getDiagnostics());
  case TargetCXXABI::Microsoft:
    return MicrosoftMangleContext::create(*this, getDiagnostics());
  }
  llvm_unreachable("Unsupported ABI");
}

MangleContext *ASTContext::createDeviceMangleContext(const TargetInfo &T) {
  assert(T.getCXXABI().getKind() != TargetCXXABI::Microsoft &&
         "Device mangle context does not support Microsoft mangling.");
  switch (T.getCXXABI().getKind()) {
  case TargetCXXABI::AppleARM64:
  case TargetCXXABI::Fuchsia:
  case TargetCXXABI::GenericAArch64:
  case TargetCXXABI::GenericItanium:
  case TargetCXXABI::GenericARM:
  case TargetCXXABI::GenericMIPS:
  case TargetCXXABI::iOS:
  case TargetCXXABI::WebAssembly:
  case TargetCXXABI::WatchOS:
  case TargetCXXABI::XL:
    return ItaniumMangleContext::create(
        *this, getDiagnostics(),
        [](ASTContext &, const NamedDecl *ND) -> std::optional<unsigned> {
          if (const auto *RD = dyn_cast<CXXRecordDecl>(ND))
            return RD->getDeviceLambdaManglingNumber();
          return std::nullopt;
        },
        /*IsAux=*/true);
  case TargetCXXABI::Microsoft:
    return MicrosoftMangleContext::create(*this, getDiagnostics(),
                                          /*IsAux=*/true);
  }
  llvm_unreachable("Unsupported ABI");
}

CXXABI::~CXXABI() = default;

size_t ASTContext::getSideTableAllocatedMemory() const {
  return ASTRecordLayouts.getMemorySize() +
         llvm::capacity_in_bytes(ObjCLayouts) +
         llvm::capacity_in_bytes(KeyFunctions) +
         llvm::capacity_in_bytes(ObjCImpls) +
         llvm::capacity_in_bytes(BlockVarCopyInits) +
         llvm::capacity_in_bytes(DeclAttrs) +
         llvm::capacity_in_bytes(TemplateOrInstantiation) +
         llvm::capacity_in_bytes(InstantiatedFromUsingDecl) +
         llvm::capacity_in_bytes(InstantiatedFromUsingShadowDecl) +
         llvm::capacity_in_bytes(InstantiatedFromUnnamedFieldDecl) +
         llvm::capacity_in_bytes(OverriddenMethods) +
         llvm::capacity_in_bytes(Types) +
         llvm::capacity_in_bytes(VariableArrayTypes);
}

/// getIntTypeForBitwidth -
/// sets integer QualTy according to specified details:
/// bitwidth, signed/unsigned.
/// Returns empty type if there is no appropriate target types.
QualType ASTContext::getIntTypeForBitwidth(unsigned DestWidth,
                                           unsigned Signed) const {
  TargetInfo::IntType Ty = getTargetInfo().getIntTypeByWidth(DestWidth, Signed);
  CanQualType QualTy = getFromTargetType(Ty);
  if (!QualTy && DestWidth == 128)
    return Signed ? Int128Ty : UnsignedInt128Ty;
  return QualTy;
}

/// getRealTypeForBitwidth -
/// sets floating point QualTy according to specified bitwidth.
/// Returns empty type if there is no appropriate target types.
QualType ASTContext::getRealTypeForBitwidth(unsigned DestWidth,
                                            FloatModeKind ExplicitType) const {
  FloatModeKind Ty =
      getTargetInfo().getRealTypeByWidth(DestWidth, ExplicitType);
  switch (Ty) {
  case FloatModeKind::Half:
    return HalfTy;
  case FloatModeKind::Float:
    return FloatTy;
  case FloatModeKind::Double:
    return DoubleTy;
  case FloatModeKind::LongDouble:
    return LongDoubleTy;
  case FloatModeKind::Float128:
    return Float128Ty;
  case FloatModeKind::Ibm128:
    return Ibm128Ty;
  case FloatModeKind::NoFloat:
    return {};
  }

  llvm_unreachable("Unhandled TargetInfo::RealType value");
}

void ASTContext::setManglingNumber(const NamedDecl *ND, unsigned Number) {
  if (Number <= 1)
    return;

  MangleNumbers[ND] = Number;

  if (Listener)
    Listener->AddedManglingNumber(ND, Number);
}

unsigned ASTContext::getManglingNumber(const NamedDecl *ND,
                                       bool ForAuxTarget) const {
  auto I = MangleNumbers.find(ND);
  unsigned Res = I != MangleNumbers.end() ? I->second : 1;
  // CUDA/HIP host compilation encodes host and device mangling numbers
  // as lower and upper half of 32 bit integer.
  if (LangOpts.CUDA && !LangOpts.CUDAIsDevice) {
    Res = ForAuxTarget ? Res >> 16 : Res & 0xFFFF;
  } else {
    assert(!ForAuxTarget && "Only CUDA/HIP host compilation supports mangling "
                            "number for aux target");
  }
  return Res > 1 ? Res : 1;
}

void ASTContext::setStaticLocalNumber(const VarDecl *VD, unsigned Number) {
  if (Number <= 1)
    return;

  StaticLocalNumbers[VD] = Number;

  if (Listener)
    Listener->AddedStaticLocalNumbers(VD, Number);
}

unsigned ASTContext::getStaticLocalNumber(const VarDecl *VD) const {
  auto I = StaticLocalNumbers.find(VD);
  return I != StaticLocalNumbers.end() ? I->second : 1;
}

MangleNumberingContext &
ASTContext::getManglingNumberContext(const DeclContext *DC) {
  assert(LangOpts.CPlusPlus);  // We don't need mangling numbers for plain C.
  std::unique_ptr<MangleNumberingContext> &MCtx = MangleNumberingContexts[DC];
  if (!MCtx)
    MCtx = createMangleNumberingContext();
  return *MCtx;
}

MangleNumberingContext &
ASTContext::getManglingNumberContext(NeedExtraManglingDecl_t, const Decl *D) {
  assert(LangOpts.CPlusPlus); // We don't need mangling numbers for plain C.
  std::unique_ptr<MangleNumberingContext> &MCtx =
      ExtraMangleNumberingContexts[D];
  if (!MCtx)
    MCtx = createMangleNumberingContext();
  return *MCtx;
}

std::unique_ptr<MangleNumberingContext>
ASTContext::createMangleNumberingContext() const {
  return ABI->createMangleNumberingContext();
}

const CXXConstructorDecl *
ASTContext::getCopyConstructorForExceptionObject(CXXRecordDecl *RD) {
  return ABI->getCopyConstructorForExceptionObject(
      cast<CXXRecordDecl>(RD->getFirstDecl()));
}

void ASTContext::addCopyConstructorForExceptionObject(CXXRecordDecl *RD,
                                                      CXXConstructorDecl *CD) {
  return ABI->addCopyConstructorForExceptionObject(
      cast<CXXRecordDecl>(RD->getFirstDecl()),
      cast<CXXConstructorDecl>(CD->getFirstDecl()));
}

void ASTContext::addTypedefNameForUnnamedTagDecl(TagDecl *TD,
                                                 TypedefNameDecl *DD) {
  return ABI->addTypedefNameForUnnamedTagDecl(TD, DD);
}

TypedefNameDecl *
ASTContext::getTypedefNameForUnnamedTagDecl(const TagDecl *TD) {
  return ABI->getTypedefNameForUnnamedTagDecl(TD);
}

void ASTContext::addDeclaratorForUnnamedTagDecl(TagDecl *TD,
                                                DeclaratorDecl *DD) {
  return ABI->addDeclaratorForUnnamedTagDecl(TD, DD);
}

DeclaratorDecl *ASTContext::getDeclaratorForUnnamedTagDecl(const TagDecl *TD) {
  return ABI->getDeclaratorForUnnamedTagDecl(TD);
}

void ASTContext::setParameterIndex(const ParmVarDecl *D, unsigned int index) {
  ParamIndices[D] = index;
}

unsigned ASTContext::getParameterIndex(const ParmVarDecl *D) const {
  ParameterIndexTable::const_iterator I = ParamIndices.find(D);
  assert(I != ParamIndices.end() &&
         "ParmIndices lacks entry set by ParmVarDecl");
  return I->second;
}

QualType ASTContext::getStringLiteralArrayType(QualType EltTy,
                                               unsigned Length) const {
  // A C++ string literal has a const-qualified element type (C++ 2.13.4p1).
  if (getLangOpts().CPlusPlus || getLangOpts().ConstStrings)
    EltTy = EltTy.withConst();

  EltTy = adjustStringLiteralBaseType(EltTy);

  // Get an array type for the string, according to C99 6.4.5. This includes
  // the null terminator character.
  return getConstantArrayType(EltTy, llvm::APInt(32, Length + 1), nullptr,
                              ArraySizeModifier::Normal, /*IndexTypeQuals*/ 0);
}

StringLiteral *
ASTContext::getPredefinedStringLiteralFromCache(StringRef Key) const {
  StringLiteral *&Result = StringLiteralCache[Key];
  if (!Result)
    Result = StringLiteral::Create(
        *this, Key, StringLiteralKind::Ordinary,
        /*Pascal*/ false, getStringLiteralArrayType(CharTy, Key.size()),
        SourceLocation());
  return Result;
}

MSGuidDecl *
ASTContext::getMSGuidDecl(MSGuidDecl::Parts Parts) const {
  assert(MSGuidTagDecl && "building MS GUID without MS extensions?");

  llvm::FoldingSetNodeID ID;
  MSGuidDecl::Profile(ID, Parts);

  void *InsertPos;
  if (MSGuidDecl *Existing = MSGuidDecls.FindNodeOrInsertPos(ID, InsertPos))
    return Existing;

  QualType GUIDType = getMSGuidType().withConst();
  MSGuidDecl *New = MSGuidDecl::Create(*this, GUIDType, Parts);
  MSGuidDecls.InsertNode(New, InsertPos);
  return New;
}

UnnamedGlobalConstantDecl *
ASTContext::getUnnamedGlobalConstantDecl(QualType Ty,
                                         const APValue &APVal) const {
  llvm::FoldingSetNodeID ID;
  UnnamedGlobalConstantDecl::Profile(ID, Ty, APVal);

  void *InsertPos;
  if (UnnamedGlobalConstantDecl *Existing =
          UnnamedGlobalConstantDecls.FindNodeOrInsertPos(ID, InsertPos))
    return Existing;

  UnnamedGlobalConstantDecl *New =
      UnnamedGlobalConstantDecl::Create(*this, Ty, APVal);
  UnnamedGlobalConstantDecls.InsertNode(New, InsertPos);
  return New;
}

TemplateParamObjectDecl *
ASTContext::getTemplateParamObjectDecl(QualType T, const APValue &V) const {
  assert(T->isRecordType() && "template param object of unexpected type");

  // C++ [temp.param]p8:
  //   [...] a static storage duration object of type 'const T' [...]
  T.addConst();

  llvm::FoldingSetNodeID ID;
  TemplateParamObjectDecl::Profile(ID, T, V);

  void *InsertPos;
  if (TemplateParamObjectDecl *Existing =
          TemplateParamObjectDecls.FindNodeOrInsertPos(ID, InsertPos))
    return Existing;

  TemplateParamObjectDecl *New = TemplateParamObjectDecl::Create(*this, T, V);
  TemplateParamObjectDecls.InsertNode(New, InsertPos);
  return New;
}

bool ASTContext::AtomicUsesUnsupportedLibcall(const AtomicExpr *E) const {
  const llvm::Triple &T = getTargetInfo().getTriple();
  if (!T.isOSDarwin())
    return false;

  if (!(T.isiOS() && T.isOSVersionLT(7)) &&
      !(T.isMacOSX() && T.isOSVersionLT(10, 9)))
    return false;

  QualType AtomicTy = E->getPtr()->getType()->getPointeeType();
  CharUnits sizeChars = getTypeSizeInChars(AtomicTy);
  uint64_t Size = sizeChars.getQuantity();
  CharUnits alignChars = getTypeAlignInChars(AtomicTy);
  unsigned Align = alignChars.getQuantity();
  unsigned MaxInlineWidthInBits = getTargetInfo().getMaxAtomicInlineWidth();
  return (Size != Align || toBits(sizeChars) > MaxInlineWidthInBits);
}

bool
ASTContext::ObjCMethodsAreEqual(const ObjCMethodDecl *MethodDecl,
                                const ObjCMethodDecl *MethodImpl) {
  // No point trying to match an unavailable/deprecated mothod.
  if (MethodDecl->hasAttr<UnavailableAttr>()
      || MethodDecl->hasAttr<DeprecatedAttr>())
    return false;
  if (MethodDecl->getObjCDeclQualifier() !=
      MethodImpl->getObjCDeclQualifier())
    return false;
  if (!hasSameType(MethodDecl->getReturnType(), MethodImpl->getReturnType()))
    return false;

  if (MethodDecl->param_size() != MethodImpl->param_size())
    return false;

  for (ObjCMethodDecl::param_const_iterator IM = MethodImpl->param_begin(),
       IF = MethodDecl->param_begin(), EM = MethodImpl->param_end(),
       EF = MethodDecl->param_end();
       IM != EM && IF != EF; ++IM, ++IF) {
    const ParmVarDecl *DeclVar = (*IF);
    const ParmVarDecl *ImplVar = (*IM);
    if (ImplVar->getObjCDeclQualifier() != DeclVar->getObjCDeclQualifier())
      return false;
    if (!hasSameType(DeclVar->getType(), ImplVar->getType()))
      return false;
  }

  return (MethodDecl->isVariadic() == MethodImpl->isVariadic());
}

uint64_t ASTContext::getTargetNullPointerValue(QualType QT) const {
  LangAS AS;
  if (QT->getUnqualifiedDesugaredType()->isNullPtrType())
    AS = LangAS::Default;
  else
    AS = QT->getPointeeType().getAddressSpace();

  return getTargetInfo().getNullPointerValue(AS);
}

unsigned ASTContext::getTargetAddressSpace(LangAS AS) const {
  return getTargetInfo().getTargetAddressSpace(AS);
}

bool ASTContext::hasSameExpr(const Expr *X, const Expr *Y) const {
  if (X == Y)
    return true;
  if (!X || !Y)
    return false;
  llvm::FoldingSetNodeID IDX, IDY;
  X->Profile(IDX, *this, /*Canonical=*/true);
  Y->Profile(IDY, *this, /*Canonical=*/true);
  return IDX == IDY;
}

// The getCommon* helpers return, for given 'same' X and Y entities given as
// inputs, another entity which is also the 'same' as the inputs, but which
// is closer to the canonical form of the inputs, each according to a given
// criteria.
// The getCommon*Checked variants are 'null inputs not-allowed' equivalents of
// the regular ones.

static Decl *getCommonDecl(Decl *X, Decl *Y) {
  if (!declaresSameEntity(X, Y))
    return nullptr;
  for (const Decl *DX : X->redecls()) {
    // If we reach Y before reaching the first decl, that means X is older.
    if (DX == Y)
      return X;
    // If we reach the first decl, then Y is older.
    if (DX->isFirstDecl())
      return Y;
  }
  llvm_unreachable("Corrupt redecls chain");
}

template <class T, std::enable_if_t<std::is_base_of_v<Decl, T>, bool> = true>
static T *getCommonDecl(T *X, T *Y) {
  return cast_or_null<T>(
      getCommonDecl(const_cast<Decl *>(cast_or_null<Decl>(X)),
                    const_cast<Decl *>(cast_or_null<Decl>(Y))));
}

template <class T, std::enable_if_t<std::is_base_of_v<Decl, T>, bool> = true>
static T *getCommonDeclChecked(T *X, T *Y) {
  return cast<T>(getCommonDecl(const_cast<Decl *>(cast<Decl>(X)),
                               const_cast<Decl *>(cast<Decl>(Y))));
}

static TemplateName getCommonTemplateName(ASTContext &Ctx, TemplateName X,
                                          TemplateName Y) {
  if (X.getAsVoidPointer() == Y.getAsVoidPointer())
    return X;
  // FIXME: There are cases here where we could find a common template name
  //        with more sugar. For example one could be a SubstTemplateTemplate*
  //        replacing the other.
  TemplateName CX = Ctx.getCanonicalTemplateName(X);
  if (CX.getAsVoidPointer() !=
      Ctx.getCanonicalTemplateName(Y).getAsVoidPointer())
    return TemplateName();
  return CX;
}

static TemplateName
getCommonTemplateNameChecked(ASTContext &Ctx, TemplateName X, TemplateName Y) {
  TemplateName R = getCommonTemplateName(Ctx, X, Y);
  assert(R.getAsVoidPointer() != nullptr);
  return R;
}

static auto getCommonTypes(ASTContext &Ctx, ArrayRef<QualType> Xs,
                           ArrayRef<QualType> Ys, bool Unqualified = false) {
  assert(Xs.size() == Ys.size());
  SmallVector<QualType, 8> Rs(Xs.size());
  for (size_t I = 0; I < Rs.size(); ++I)
    Rs[I] = Ctx.getCommonSugaredType(Xs[I], Ys[I], Unqualified);
  return Rs;
}

template <class T>
static SourceLocation getCommonAttrLoc(const T *X, const T *Y) {
  return X->getAttributeLoc() == Y->getAttributeLoc() ? X->getAttributeLoc()
                                                      : SourceLocation();
}

static TemplateArgument getCommonTemplateArgument(ASTContext &Ctx,
                                                  const TemplateArgument &X,
                                                  const TemplateArgument &Y) {
  if (X.getKind() != Y.getKind())
    return TemplateArgument();

  switch (X.getKind()) {
  case TemplateArgument::ArgKind::Type:
    if (!Ctx.hasSameType(X.getAsType(), Y.getAsType()))
      return TemplateArgument();
    return TemplateArgument(
        Ctx.getCommonSugaredType(X.getAsType(), Y.getAsType()));
  case TemplateArgument::ArgKind::NullPtr:
    if (!Ctx.hasSameType(X.getNullPtrType(), Y.getNullPtrType()))
      return TemplateArgument();
    return TemplateArgument(
        Ctx.getCommonSugaredType(X.getNullPtrType(), Y.getNullPtrType()),
        /*Unqualified=*/true);
  case TemplateArgument::ArgKind::Expression:
    if (!Ctx.hasSameType(X.getAsExpr()->getType(), Y.getAsExpr()->getType()))
      return TemplateArgument();
    // FIXME: Try to keep the common sugar.
    return X;
  case TemplateArgument::ArgKind::Template: {
    TemplateName TX = X.getAsTemplate(), TY = Y.getAsTemplate();
    TemplateName CTN = ::getCommonTemplateName(Ctx, TX, TY);
    if (!CTN.getAsVoidPointer())
      return TemplateArgument();
    return TemplateArgument(CTN);
  }
  case TemplateArgument::ArgKind::TemplateExpansion: {
    TemplateName TX = X.getAsTemplateOrTemplatePattern(),
                 TY = Y.getAsTemplateOrTemplatePattern();
    TemplateName CTN = ::getCommonTemplateName(Ctx, TX, TY);
    if (!CTN.getAsVoidPointer())
      return TemplateName();
    auto NExpX = X.getNumTemplateExpansions();
    assert(NExpX == Y.getNumTemplateExpansions());
    return TemplateArgument(CTN, NExpX);
  }
  default:
    // FIXME: Handle the other argument kinds.
    return X;
  }
}

static bool getCommonTemplateArguments(ASTContext &Ctx,
                                       SmallVectorImpl<TemplateArgument> &R,
                                       ArrayRef<TemplateArgument> Xs,
                                       ArrayRef<TemplateArgument> Ys) {
  if (Xs.size() != Ys.size())
    return true;
  R.resize(Xs.size());
  for (size_t I = 0; I < R.size(); ++I) {
    R[I] = getCommonTemplateArgument(Ctx, Xs[I], Ys[I]);
    if (R[I].isNull())
      return true;
  }
  return false;
}

static auto getCommonTemplateArguments(ASTContext &Ctx,
                                       ArrayRef<TemplateArgument> Xs,
                                       ArrayRef<TemplateArgument> Ys) {
  SmallVector<TemplateArgument, 8> R;
  bool Different = getCommonTemplateArguments(Ctx, R, Xs, Ys);
  assert(!Different);
  (void)Different;
  return R;
}

template <class T>
static ElaboratedTypeKeyword getCommonTypeKeyword(const T *X, const T *Y) {
  return X->getKeyword() == Y->getKeyword() ? X->getKeyword()
                                            : ElaboratedTypeKeyword::None;
}

template <class T>
static NestedNameSpecifier *getCommonNNS(ASTContext &Ctx, const T *X,
                                         const T *Y) {
  // FIXME: Try to keep the common NNS sugar.
  return X->getQualifier() == Y->getQualifier()
             ? X->getQualifier()
             : Ctx.getCanonicalNestedNameSpecifier(X->getQualifier());
}

template <class T>
static QualType getCommonElementType(ASTContext &Ctx, const T *X, const T *Y) {
  return Ctx.getCommonSugaredType(X->getElementType(), Y->getElementType());
}

template <class T>
static QualType getCommonArrayElementType(ASTContext &Ctx, const T *X,
                                          Qualifiers &QX, const T *Y,
                                          Qualifiers &QY) {
  QualType EX = X->getElementType(), EY = Y->getElementType();
  QualType R = Ctx.getCommonSugaredType(EX, EY,
                                        /*Unqualified=*/true);
  Qualifiers RQ = R.getQualifiers();
  QX += EX.getQualifiers() - RQ;
  QY += EY.getQualifiers() - RQ;
  return R;
}

template <class T>
static QualType getCommonPointeeType(ASTContext &Ctx, const T *X, const T *Y) {
  return Ctx.getCommonSugaredType(X->getPointeeType(), Y->getPointeeType());
}

template <class T> static auto *getCommonSizeExpr(ASTContext &Ctx, T *X, T *Y) {
  assert(Ctx.hasSameExpr(X->getSizeExpr(), Y->getSizeExpr()));
  return X->getSizeExpr();
}

static auto getCommonSizeModifier(const ArrayType *X, const ArrayType *Y) {
  assert(X->getSizeModifier() == Y->getSizeModifier());
  return X->getSizeModifier();
}

static auto getCommonIndexTypeCVRQualifiers(const ArrayType *X,
                                            const ArrayType *Y) {
  assert(X->getIndexTypeCVRQualifiers() == Y->getIndexTypeCVRQualifiers());
  return X->getIndexTypeCVRQualifiers();
}

// Merges two type lists such that the resulting vector will contain
// each type (in a canonical sense) only once, in the order they appear
// from X to Y. If they occur in both X and Y, the result will contain
// the common sugared type between them.
static void mergeTypeLists(ASTContext &Ctx, SmallVectorImpl<QualType> &Out,
                           ArrayRef<QualType> X, ArrayRef<QualType> Y) {
  llvm::DenseMap<QualType, unsigned> Found;
  for (auto Ts : {X, Y}) {
    for (QualType T : Ts) {
      auto Res = Found.try_emplace(Ctx.getCanonicalType(T), Out.size());
      if (!Res.second) {
        QualType &U = Out[Res.first->second];
        U = Ctx.getCommonSugaredType(U, T);
      } else {
        Out.emplace_back(T);
      }
    }
  }
}

FunctionProtoType::ExceptionSpecInfo
ASTContext::mergeExceptionSpecs(FunctionProtoType::ExceptionSpecInfo ESI1,
                                FunctionProtoType::ExceptionSpecInfo ESI2,
                                SmallVectorImpl<QualType> &ExceptionTypeStorage,
                                bool AcceptDependent) {
  ExceptionSpecificationType EST1 = ESI1.Type, EST2 = ESI2.Type;

  // If either of them can throw anything, that is the result.
  for (auto I : {EST_None, EST_MSAny, EST_NoexceptFalse}) {
    if (EST1 == I)
      return ESI1;
    if (EST2 == I)
      return ESI2;
  }

  // If either of them is non-throwing, the result is the other.
  for (auto I :
       {EST_NoThrow, EST_DynamicNone, EST_BasicNoexcept, EST_NoexceptTrue}) {
    if (EST1 == I)
      return ESI2;
    if (EST2 == I)
      return ESI1;
  }

  // If we're left with value-dependent computed noexcept expressions, we're
  // stuck. Before C++17, we can just drop the exception specification entirely,
  // since it's not actually part of the canonical type. And this should never
  // happen in C++17, because it would mean we were computing the composite
  // pointer type of dependent types, which should never happen.
  if (EST1 == EST_DependentNoexcept || EST2 == EST_DependentNoexcept) {
    assert(AcceptDependent &&
           "computing composite pointer type of dependent types");
    return FunctionProtoType::ExceptionSpecInfo();
  }

  // Switch over the possibilities so that people adding new values know to
  // update this function.
  switch (EST1) {
  case EST_None:
  case EST_DynamicNone:
  case EST_MSAny:
  case EST_BasicNoexcept:
  case EST_DependentNoexcept:
  case EST_NoexceptFalse:
  case EST_NoexceptTrue:
  case EST_NoThrow:
    llvm_unreachable("These ESTs should be handled above");

  case EST_Dynamic: {
    // This is the fun case: both exception specifications are dynamic. Form
    // the union of the two lists.
    assert(EST2 == EST_Dynamic && "other cases should already be handled");
    mergeTypeLists(*this, ExceptionTypeStorage, ESI1.Exceptions,
                   ESI2.Exceptions);
    FunctionProtoType::ExceptionSpecInfo Result(EST_Dynamic);
    Result.Exceptions = ExceptionTypeStorage;
    return Result;
  }

  case EST_Unevaluated:
  case EST_Uninstantiated:
  case EST_Unparsed:
    llvm_unreachable("shouldn't see unresolved exception specifications here");
  }

  llvm_unreachable("invalid ExceptionSpecificationType");
}

static QualType getCommonNonSugarTypeNode(ASTContext &Ctx, const Type *X,
                                          Qualifiers &QX, const Type *Y,
                                          Qualifiers &QY) {
  Type::TypeClass TC = X->getTypeClass();
  assert(TC == Y->getTypeClass());
  switch (TC) {
#define UNEXPECTED_TYPE(Class, Kind)                                           \
  case Type::Class:                                                            \
    llvm_unreachable("Unexpected " Kind ": " #Class);

#define NON_CANONICAL_TYPE(Class, Base) UNEXPECTED_TYPE(Class, "non-canonical")
#define TYPE(Class, Base)
#include "clang/AST/TypeNodes.inc"

#define SUGAR_FREE_TYPE(Class) UNEXPECTED_TYPE(Class, "sugar-free")
    SUGAR_FREE_TYPE(Builtin)
    SUGAR_FREE_TYPE(DeducedTemplateSpecialization)
    SUGAR_FREE_TYPE(DependentBitInt)
    SUGAR_FREE_TYPE(Enum)
    SUGAR_FREE_TYPE(BitInt)
    SUGAR_FREE_TYPE(ObjCInterface)
    SUGAR_FREE_TYPE(Record)
    SUGAR_FREE_TYPE(SubstTemplateTypeParmPack)
    SUGAR_FREE_TYPE(UnresolvedUsing)
#undef SUGAR_FREE_TYPE
#define NON_UNIQUE_TYPE(Class) UNEXPECTED_TYPE(Class, "non-unique")
    NON_UNIQUE_TYPE(TypeOfExpr)
    NON_UNIQUE_TYPE(VariableArray)
#undef NON_UNIQUE_TYPE

    UNEXPECTED_TYPE(TypeOf, "sugar")

#undef UNEXPECTED_TYPE

  case Type::Auto: {
    const auto *AX = cast<AutoType>(X), *AY = cast<AutoType>(Y);
    assert(AX->getDeducedType().isNull());
    assert(AY->getDeducedType().isNull());
    assert(AX->getKeyword() == AY->getKeyword());
    assert(AX->isInstantiationDependentType() ==
           AY->isInstantiationDependentType());
    auto As = getCommonTemplateArguments(Ctx, AX->getTypeConstraintArguments(),
                                         AY->getTypeConstraintArguments());
    return Ctx.getAutoType(QualType(), AX->getKeyword(),
                           AX->isInstantiationDependentType(),
                           AX->containsUnexpandedParameterPack(),
                           getCommonDeclChecked(AX->getTypeConstraintConcept(),
                                                AY->getTypeConstraintConcept()),
                           As);
  }
  case Type::IncompleteArray: {
    const auto *AX = cast<IncompleteArrayType>(X),
               *AY = cast<IncompleteArrayType>(Y);
    return Ctx.getIncompleteArrayType(
        getCommonArrayElementType(Ctx, AX, QX, AY, QY),
        getCommonSizeModifier(AX, AY), getCommonIndexTypeCVRQualifiers(AX, AY));
  }
  case Type::DependentSizedArray: {
    const auto *AX = cast<DependentSizedArrayType>(X),
               *AY = cast<DependentSizedArrayType>(Y);
    return Ctx.getDependentSizedArrayType(
        getCommonArrayElementType(Ctx, AX, QX, AY, QY),
        getCommonSizeExpr(Ctx, AX, AY), getCommonSizeModifier(AX, AY),
        getCommonIndexTypeCVRQualifiers(AX, AY),
        AX->getBracketsRange() == AY->getBracketsRange()
            ? AX->getBracketsRange()
            : SourceRange());
  }
  case Type::ConstantArray: {
    const auto *AX = cast<ConstantArrayType>(X),
               *AY = cast<ConstantArrayType>(Y);
    assert(AX->getSize() == AY->getSize());
    const Expr *SizeExpr = Ctx.hasSameExpr(AX->getSizeExpr(), AY->getSizeExpr())
                               ? AX->getSizeExpr()
                               : nullptr;
    return Ctx.getConstantArrayType(
        getCommonArrayElementType(Ctx, AX, QX, AY, QY), AX->getSize(), SizeExpr,
        getCommonSizeModifier(AX, AY), getCommonIndexTypeCVRQualifiers(AX, AY));
  }
  case Type::ArrayParameter: {
    const auto *AX = cast<ArrayParameterType>(X),
               *AY = cast<ArrayParameterType>(Y);
    assert(AX->getSize() == AY->getSize());
    const Expr *SizeExpr = Ctx.hasSameExpr(AX->getSizeExpr(), AY->getSizeExpr())
                               ? AX->getSizeExpr()
                               : nullptr;
    auto ArrayTy = Ctx.getConstantArrayType(
        getCommonArrayElementType(Ctx, AX, QX, AY, QY), AX->getSize(), SizeExpr,
        getCommonSizeModifier(AX, AY), getCommonIndexTypeCVRQualifiers(AX, AY));
    return Ctx.getArrayParameterType(ArrayTy);
  }
  case Type::Atomic: {
    const auto *AX = cast<AtomicType>(X), *AY = cast<AtomicType>(Y);
    return Ctx.getAtomicType(
        Ctx.getCommonSugaredType(AX->getValueType(), AY->getValueType()));
  }
  case Type::Complex: {
    const auto *CX = cast<ComplexType>(X), *CY = cast<ComplexType>(Y);
    return Ctx.getComplexType(getCommonArrayElementType(Ctx, CX, QX, CY, QY));
  }
  case Type::Pointer: {
    const auto *PX = cast<PointerType>(X), *PY = cast<PointerType>(Y);
    return Ctx.getPointerType(getCommonPointeeType(Ctx, PX, PY));
  }
  case Type::BlockPointer: {
    const auto *PX = cast<BlockPointerType>(X), *PY = cast<BlockPointerType>(Y);
    return Ctx.getBlockPointerType(getCommonPointeeType(Ctx, PX, PY));
  }
  case Type::ObjCObjectPointer: {
    const auto *PX = cast<ObjCObjectPointerType>(X),
               *PY = cast<ObjCObjectPointerType>(Y);
    return Ctx.getObjCObjectPointerType(getCommonPointeeType(Ctx, PX, PY));
  }
  case Type::MemberPointer: {
    const auto *PX = cast<MemberPointerType>(X),
               *PY = cast<MemberPointerType>(Y);
    return Ctx.getMemberPointerType(
        getCommonPointeeType(Ctx, PX, PY),
        Ctx.getCommonSugaredType(QualType(PX->getClass(), 0),
                                 QualType(PY->getClass(), 0))
            .getTypePtr());
  }
  case Type::LValueReference: {
    const auto *PX = cast<LValueReferenceType>(X),
               *PY = cast<LValueReferenceType>(Y);
    // FIXME: Preserve PointeeTypeAsWritten.
    return Ctx.getLValueReferenceType(getCommonPointeeType(Ctx, PX, PY),
                                      PX->isSpelledAsLValue() ||
                                          PY->isSpelledAsLValue());
  }
  case Type::RValueReference: {
    const auto *PX = cast<RValueReferenceType>(X),
               *PY = cast<RValueReferenceType>(Y);
    // FIXME: Preserve PointeeTypeAsWritten.
    return Ctx.getRValueReferenceType(getCommonPointeeType(Ctx, PX, PY));
  }
  case Type::DependentAddressSpace: {
    const auto *PX = cast<DependentAddressSpaceType>(X),
               *PY = cast<DependentAddressSpaceType>(Y);
    assert(Ctx.hasSameExpr(PX->getAddrSpaceExpr(), PY->getAddrSpaceExpr()));
    return Ctx.getDependentAddressSpaceType(getCommonPointeeType(Ctx, PX, PY),
                                            PX->getAddrSpaceExpr(),
                                            getCommonAttrLoc(PX, PY));
  }
  case Type::FunctionNoProto: {
    const auto *FX = cast<FunctionNoProtoType>(X),
               *FY = cast<FunctionNoProtoType>(Y);
    assert(FX->getExtInfo() == FY->getExtInfo());
    return Ctx.getFunctionNoProtoType(
        Ctx.getCommonSugaredType(FX->getReturnType(), FY->getReturnType()),
        FX->getExtInfo());
  }
  case Type::FunctionProto: {
    const auto *FX = cast<FunctionProtoType>(X),
               *FY = cast<FunctionProtoType>(Y);
    FunctionProtoType::ExtProtoInfo EPIX = FX->getExtProtoInfo(),
                                    EPIY = FY->getExtProtoInfo();
    assert(EPIX.ExtInfo == EPIY.ExtInfo);
    assert(EPIX.ExtParameterInfos == EPIY.ExtParameterInfos);
    assert(EPIX.RefQualifier == EPIY.RefQualifier);
    assert(EPIX.TypeQuals == EPIY.TypeQuals);
    assert(EPIX.Variadic == EPIY.Variadic);

    // FIXME: Can we handle an empty EllipsisLoc?
    //        Use emtpy EllipsisLoc if X and Y differ.

    EPIX.HasTrailingReturn = EPIX.HasTrailingReturn && EPIY.HasTrailingReturn;

    QualType R =
        Ctx.getCommonSugaredType(FX->getReturnType(), FY->getReturnType());
    auto P = getCommonTypes(Ctx, FX->param_types(), FY->param_types(),
                            /*Unqualified=*/true);

    SmallVector<QualType, 8> Exceptions;
    EPIX.ExceptionSpec = Ctx.mergeExceptionSpecs(
        EPIX.ExceptionSpec, EPIY.ExceptionSpec, Exceptions, true);
    return Ctx.getFunctionType(R, P, EPIX);
  }
  case Type::ObjCObject: {
    const auto *OX = cast<ObjCObjectType>(X), *OY = cast<ObjCObjectType>(Y);
    assert(
        std::equal(OX->getProtocols().begin(), OX->getProtocols().end(),
                   OY->getProtocols().begin(), OY->getProtocols().end(),
                   [](const ObjCProtocolDecl *P0, const ObjCProtocolDecl *P1) {
                     return P0->getCanonicalDecl() == P1->getCanonicalDecl();
                   }) &&
        "protocol lists must be the same");
    auto TAs = getCommonTypes(Ctx, OX->getTypeArgsAsWritten(),
                              OY->getTypeArgsAsWritten());
    return Ctx.getObjCObjectType(
        Ctx.getCommonSugaredType(OX->getBaseType(), OY->getBaseType()), TAs,
        OX->getProtocols(),
        OX->isKindOfTypeAsWritten() && OY->isKindOfTypeAsWritten());
  }
  case Type::ConstantMatrix: {
    const auto *MX = cast<ConstantMatrixType>(X),
               *MY = cast<ConstantMatrixType>(Y);
    assert(MX->getNumRows() == MY->getNumRows());
    assert(MX->getNumColumns() == MY->getNumColumns());
    return Ctx.getConstantMatrixType(getCommonElementType(Ctx, MX, MY),
                                     MX->getNumRows(), MX->getNumColumns());
  }
  case Type::DependentSizedMatrix: {
    const auto *MX = cast<DependentSizedMatrixType>(X),
               *MY = cast<DependentSizedMatrixType>(Y);
    assert(Ctx.hasSameExpr(MX->getRowExpr(), MY->getRowExpr()));
    assert(Ctx.hasSameExpr(MX->getColumnExpr(), MY->getColumnExpr()));
    return Ctx.getDependentSizedMatrixType(
        getCommonElementType(Ctx, MX, MY), MX->getRowExpr(),
        MX->getColumnExpr(), getCommonAttrLoc(MX, MY));
  }
  case Type::Vector: {
    const auto *VX = cast<VectorType>(X), *VY = cast<VectorType>(Y);
    assert(VX->getNumElements() == VY->getNumElements());
    assert(VX->getVectorKind() == VY->getVectorKind());
    return Ctx.getVectorType(getCommonElementType(Ctx, VX, VY),
                             VX->getNumElements(), VX->getVectorKind());
  }
  case Type::ExtVector: {
    const auto *VX = cast<ExtVectorType>(X), *VY = cast<ExtVectorType>(Y);
    assert(VX->getNumElements() == VY->getNumElements());
    return Ctx.getExtVectorType(getCommonElementType(Ctx, VX, VY),
                                VX->getNumElements());
  }
  case Type::DependentSizedExtVector: {
    const auto *VX = cast<DependentSizedExtVectorType>(X),
               *VY = cast<DependentSizedExtVectorType>(Y);
    return Ctx.getDependentSizedExtVectorType(getCommonElementType(Ctx, VX, VY),
                                              getCommonSizeExpr(Ctx, VX, VY),
                                              getCommonAttrLoc(VX, VY));
  }
  case Type::DependentVector: {
    const auto *VX = cast<DependentVectorType>(X),
               *VY = cast<DependentVectorType>(Y);
    assert(VX->getVectorKind() == VY->getVectorKind());
    return Ctx.getDependentVectorType(
        getCommonElementType(Ctx, VX, VY), getCommonSizeExpr(Ctx, VX, VY),
        getCommonAttrLoc(VX, VY), VX->getVectorKind());
  }
  case Type::InjectedClassName: {
    const auto *IX = cast<InjectedClassNameType>(X),
               *IY = cast<InjectedClassNameType>(Y);
    return Ctx.getInjectedClassNameType(
        getCommonDeclChecked(IX->getDecl(), IY->getDecl()),
        Ctx.getCommonSugaredType(IX->getInjectedSpecializationType(),
                                 IY->getInjectedSpecializationType()));
  }
  case Type::TemplateSpecialization: {
    const auto *TX = cast<TemplateSpecializationType>(X),
               *TY = cast<TemplateSpecializationType>(Y);
    auto As = getCommonTemplateArguments(Ctx, TX->template_arguments(),
                                         TY->template_arguments());
    return Ctx.getTemplateSpecializationType(
        ::getCommonTemplateNameChecked(Ctx, TX->getTemplateName(),
                                       TY->getTemplateName()),
        As, X->getCanonicalTypeInternal());
  }
  case Type::Decltype: {
    const auto *DX = cast<DecltypeType>(X);
    [[maybe_unused]] const auto *DY = cast<DecltypeType>(Y);
    assert(DX->isDependentType());
    assert(DY->isDependentType());
    assert(Ctx.hasSameExpr(DX->getUnderlyingExpr(), DY->getUnderlyingExpr()));
    // As Decltype is not uniqued, building a common type would be wasteful.
    return QualType(DX, 0);
  }
  case Type::PackIndexing: {
    const auto *DX = cast<PackIndexingType>(X);
    [[maybe_unused]] const auto *DY = cast<PackIndexingType>(Y);
    assert(DX->isDependentType());
    assert(DY->isDependentType());
    assert(Ctx.hasSameExpr(DX->getIndexExpr(), DY->getIndexExpr()));
    return QualType(DX, 0);
  }
  case Type::DependentName: {
    const auto *NX = cast<DependentNameType>(X),
               *NY = cast<DependentNameType>(Y);
    assert(NX->getIdentifier() == NY->getIdentifier());
    return Ctx.getDependentNameType(
        getCommonTypeKeyword(NX, NY), getCommonNNS(Ctx, NX, NY),
        NX->getIdentifier(), NX->getCanonicalTypeInternal());
  }
  case Type::DependentTemplateSpecialization: {
    const auto *TX = cast<DependentTemplateSpecializationType>(X),
               *TY = cast<DependentTemplateSpecializationType>(Y);
    assert(TX->getIdentifier() == TY->getIdentifier());
    auto As = getCommonTemplateArguments(Ctx, TX->template_arguments(),
                                         TY->template_arguments());
    return Ctx.getDependentTemplateSpecializationType(
        getCommonTypeKeyword(TX, TY), getCommonNNS(Ctx, TX, TY),
        TX->getIdentifier(), As);
  }
  case Type::UnaryTransform: {
    const auto *TX = cast<UnaryTransformType>(X),
               *TY = cast<UnaryTransformType>(Y);
    assert(TX->getUTTKind() == TY->getUTTKind());
    return Ctx.getUnaryTransformType(
        Ctx.getCommonSugaredType(TX->getBaseType(), TY->getBaseType()),
        Ctx.getCommonSugaredType(TX->getUnderlyingType(),
                                 TY->getUnderlyingType()),
        TX->getUTTKind());
  }
  case Type::PackExpansion: {
    const auto *PX = cast<PackExpansionType>(X),
               *PY = cast<PackExpansionType>(Y);
    assert(PX->getNumExpansions() == PY->getNumExpansions());
    return Ctx.getPackExpansionType(
        Ctx.getCommonSugaredType(PX->getPattern(), PY->getPattern()),
        PX->getNumExpansions(), false);
  }
  case Type::Pipe: {
    const auto *PX = cast<PipeType>(X), *PY = cast<PipeType>(Y);
    assert(PX->isReadOnly() == PY->isReadOnly());
    auto MP = PX->isReadOnly() ? &ASTContext::getReadPipeType
                               : &ASTContext::getWritePipeType;
    return (Ctx.*MP)(getCommonElementType(Ctx, PX, PY));
  }
  case Type::TemplateTypeParm: {
    const auto *TX = cast<TemplateTypeParmType>(X),
               *TY = cast<TemplateTypeParmType>(Y);
    assert(TX->getDepth() == TY->getDepth());
    assert(TX->getIndex() == TY->getIndex());
    assert(TX->isParameterPack() == TY->isParameterPack());
    return Ctx.getTemplateTypeParmType(
        TX->getDepth(), TX->getIndex(), TX->isParameterPack(),
        getCommonDecl(TX->getDecl(), TY->getDecl()));
  }
  }
  llvm_unreachable("Unknown Type Class");
}

static QualType getCommonSugarTypeNode(ASTContext &Ctx, const Type *X,
                                       const Type *Y,
                                       SplitQualType Underlying) {
  Type::TypeClass TC = X->getTypeClass();
  if (TC != Y->getTypeClass())
    return QualType();
  switch (TC) {
#define UNEXPECTED_TYPE(Class, Kind)                                           \
  case Type::Class:                                                            \
    llvm_unreachable("Unexpected " Kind ": " #Class);
#define TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) UNEXPECTED_TYPE(Class, "dependent")
#include "clang/AST/TypeNodes.inc"

#define CANONICAL_TYPE(Class) UNEXPECTED_TYPE(Class, "canonical")
    CANONICAL_TYPE(Atomic)
    CANONICAL_TYPE(BitInt)
    CANONICAL_TYPE(BlockPointer)
    CANONICAL_TYPE(Builtin)
    CANONICAL_TYPE(Complex)
    CANONICAL_TYPE(ConstantArray)
    CANONICAL_TYPE(ArrayParameter)
    CANONICAL_TYPE(ConstantMatrix)
    CANONICAL_TYPE(Enum)
    CANONICAL_TYPE(ExtVector)
    CANONICAL_TYPE(FunctionNoProto)
    CANONICAL_TYPE(FunctionProto)
    CANONICAL_TYPE(IncompleteArray)
    CANONICAL_TYPE(LValueReference)
    CANONICAL_TYPE(MemberPointer)
    CANONICAL_TYPE(ObjCInterface)
    CANONICAL_TYPE(ObjCObject)
    CANONICAL_TYPE(ObjCObjectPointer)
    CANONICAL_TYPE(Pipe)
    CANONICAL_TYPE(Pointer)
    CANONICAL_TYPE(Record)
    CANONICAL_TYPE(RValueReference)
    CANONICAL_TYPE(VariableArray)
    CANONICAL_TYPE(Vector)
#undef CANONICAL_TYPE

#undef UNEXPECTED_TYPE

  case Type::Adjusted: {
    const auto *AX = cast<AdjustedType>(X), *AY = cast<AdjustedType>(Y);
    QualType OX = AX->getOriginalType(), OY = AY->getOriginalType();
    if (!Ctx.hasSameType(OX, OY))
      return QualType();
    // FIXME: It's inefficient to have to unify the original types.
    return Ctx.getAdjustedType(Ctx.getCommonSugaredType(OX, OY),
                               Ctx.getQualifiedType(Underlying));
  }
  case Type::Decayed: {
    const auto *DX = cast<DecayedType>(X), *DY = cast<DecayedType>(Y);
    QualType OX = DX->getOriginalType(), OY = DY->getOriginalType();
    if (!Ctx.hasSameType(OX, OY))
      return QualType();
    // FIXME: It's inefficient to have to unify the original types.
    return Ctx.getDecayedType(Ctx.getCommonSugaredType(OX, OY),
                              Ctx.getQualifiedType(Underlying));
  }
  case Type::Attributed: {
    const auto *AX = cast<AttributedType>(X), *AY = cast<AttributedType>(Y);
    AttributedType::Kind Kind = AX->getAttrKind();
    if (Kind != AY->getAttrKind())
      return QualType();
    QualType MX = AX->getModifiedType(), MY = AY->getModifiedType();
    if (!Ctx.hasSameType(MX, MY))
      return QualType();
    // FIXME: It's inefficient to have to unify the modified types.
    return Ctx.getAttributedType(Kind, Ctx.getCommonSugaredType(MX, MY),
                                 Ctx.getQualifiedType(Underlying));
  }
  case Type::BTFTagAttributed: {
    const auto *BX = cast<BTFTagAttributedType>(X);
    const BTFTypeTagAttr *AX = BX->getAttr();
    // The attribute is not uniqued, so just compare the tag.
    if (AX->getBTFTypeTag() !=
        cast<BTFTagAttributedType>(Y)->getAttr()->getBTFTypeTag())
      return QualType();
    return Ctx.getBTFTagAttributedType(AX, Ctx.getQualifiedType(Underlying));
  }
  case Type::Auto: {
    const auto *AX = cast<AutoType>(X), *AY = cast<AutoType>(Y);

    AutoTypeKeyword KW = AX->getKeyword();
    if (KW != AY->getKeyword())
      return QualType();

    ConceptDecl *CD = ::getCommonDecl(AX->getTypeConstraintConcept(),
                                      AY->getTypeConstraintConcept());
    SmallVector<TemplateArgument, 8> As;
    if (CD &&
        getCommonTemplateArguments(Ctx, As, AX->getTypeConstraintArguments(),
                                   AY->getTypeConstraintArguments())) {
      CD = nullptr; // The arguments differ, so make it unconstrained.
      As.clear();
    }

    // Both auto types can't be dependent, otherwise they wouldn't have been
    // sugar. This implies they can't contain unexpanded packs either.
    return Ctx.getAutoType(Ctx.getQualifiedType(Underlying), AX->getKeyword(),
                           /*IsDependent=*/false, /*IsPack=*/false, CD, As);
  }
  case Type::PackIndexing:
  case Type::Decltype:
    return QualType();
  case Type::DeducedTemplateSpecialization:
    // FIXME: Try to merge these.
    return QualType();

  case Type::Elaborated: {
    const auto *EX = cast<ElaboratedType>(X), *EY = cast<ElaboratedType>(Y);
    return Ctx.getElaboratedType(
        ::getCommonTypeKeyword(EX, EY), ::getCommonNNS(Ctx, EX, EY),
        Ctx.getQualifiedType(Underlying),
        ::getCommonDecl(EX->getOwnedTagDecl(), EY->getOwnedTagDecl()));
  }
  case Type::MacroQualified: {
    const auto *MX = cast<MacroQualifiedType>(X),
               *MY = cast<MacroQualifiedType>(Y);
    const IdentifierInfo *IX = MX->getMacroIdentifier();
    if (IX != MY->getMacroIdentifier())
      return QualType();
    return Ctx.getMacroQualifiedType(Ctx.getQualifiedType(Underlying), IX);
  }
  case Type::SubstTemplateTypeParm: {
    const auto *SX = cast<SubstTemplateTypeParmType>(X),
               *SY = cast<SubstTemplateTypeParmType>(Y);
    Decl *CD =
        ::getCommonDecl(SX->getAssociatedDecl(), SY->getAssociatedDecl());
    if (!CD)
      return QualType();
    unsigned Index = SX->getIndex();
    if (Index != SY->getIndex())
      return QualType();
    auto PackIndex = SX->getPackIndex();
    if (PackIndex != SY->getPackIndex())
      return QualType();
    return Ctx.getSubstTemplateTypeParmType(Ctx.getQualifiedType(Underlying),
                                            CD, Index, PackIndex);
  }
  case Type::ObjCTypeParam:
    // FIXME: Try to merge these.
    return QualType();
  case Type::Paren:
    return Ctx.getParenType(Ctx.getQualifiedType(Underlying));

  case Type::TemplateSpecialization: {
    const auto *TX = cast<TemplateSpecializationType>(X),
               *TY = cast<TemplateSpecializationType>(Y);
    TemplateName CTN = ::getCommonTemplateName(Ctx, TX->getTemplateName(),
                                               TY->getTemplateName());
    if (!CTN.getAsVoidPointer())
      return QualType();
    SmallVector<TemplateArgument, 8> Args;
    if (getCommonTemplateArguments(Ctx, Args, TX->template_arguments(),
                                   TY->template_arguments()))
      return QualType();
    return Ctx.getTemplateSpecializationType(CTN, Args,
                                             Ctx.getQualifiedType(Underlying));
  }
  case Type::Typedef: {
    const auto *TX = cast<TypedefType>(X), *TY = cast<TypedefType>(Y);
    const TypedefNameDecl *CD = ::getCommonDecl(TX->getDecl(), TY->getDecl());
    if (!CD)
      return QualType();
    return Ctx.getTypedefType(CD, Ctx.getQualifiedType(Underlying));
  }
  case Type::TypeOf: {
    // The common sugar between two typeof expressions, where one is
    // potentially a typeof_unqual and the other is not, we unify to the
    // qualified type as that retains the most information along with the type.
    // We only return a typeof_unqual type when both types are unqual types.
    TypeOfKind Kind = TypeOfKind::Qualified;
    if (cast<TypeOfType>(X)->getKind() == cast<TypeOfType>(Y)->getKind() &&
        cast<TypeOfType>(X)->getKind() == TypeOfKind::Unqualified)
      Kind = TypeOfKind::Unqualified;
    return Ctx.getTypeOfType(Ctx.getQualifiedType(Underlying), Kind);
  }
  case Type::TypeOfExpr:
    return QualType();

  case Type::UnaryTransform: {
    const auto *UX = cast<UnaryTransformType>(X),
               *UY = cast<UnaryTransformType>(Y);
    UnaryTransformType::UTTKind KX = UX->getUTTKind();
    if (KX != UY->getUTTKind())
      return QualType();
    QualType BX = UX->getBaseType(), BY = UY->getBaseType();
    if (!Ctx.hasSameType(BX, BY))
      return QualType();
    // FIXME: It's inefficient to have to unify the base types.
    return Ctx.getUnaryTransformType(Ctx.getCommonSugaredType(BX, BY),
                                     Ctx.getQualifiedType(Underlying), KX);
  }
  case Type::Using: {
    const auto *UX = cast<UsingType>(X), *UY = cast<UsingType>(Y);
    const UsingShadowDecl *CD =
        ::getCommonDecl(UX->getFoundDecl(), UY->getFoundDecl());
    if (!CD)
      return QualType();
    return Ctx.getUsingType(CD, Ctx.getQualifiedType(Underlying));
  }
  case Type::CountAttributed: {
    const auto *DX = cast<CountAttributedType>(X),
               *DY = cast<CountAttributedType>(Y);
    if (DX->isCountInBytes() != DY->isCountInBytes())
      return QualType();
    if (DX->isOrNull() != DY->isOrNull())
      return QualType();
    Expr *CEX = DX->getCountExpr();
    Expr *CEY = DY->getCountExpr();
    llvm::ArrayRef<clang::TypeCoupledDeclRefInfo> CDX = DX->getCoupledDecls();
    if (Ctx.hasSameExpr(CEX, CEY))
      return Ctx.getCountAttributedType(Ctx.getQualifiedType(Underlying), CEX,
                                        DX->isCountInBytes(), DX->isOrNull(),
                                        CDX);
    if (!CEX->isIntegerConstantExpr(Ctx) || !CEY->isIntegerConstantExpr(Ctx))
      return QualType();
    // Two declarations with the same integer constant may still differ in their
    // expression pointers, so we need to evaluate them.
    llvm::APSInt VX = *CEX->getIntegerConstantExpr(Ctx);
    llvm::APSInt VY = *CEY->getIntegerConstantExpr(Ctx);
    if (VX != VY)
      return QualType();
    return Ctx.getCountAttributedType(Ctx.getQualifiedType(Underlying), CEX,
                                      DX->isCountInBytes(), DX->isOrNull(),
                                      CDX);
  }
  }
  llvm_unreachable("Unhandled Type Class");
}

static auto unwrapSugar(SplitQualType &T, Qualifiers &QTotal) {
  SmallVector<SplitQualType, 8> R;
  while (true) {
    QTotal.addConsistentQualifiers(T.Quals);
    QualType NT = T.Ty->getLocallyUnqualifiedSingleStepDesugaredType();
    if (NT == QualType(T.Ty, 0))
      break;
    R.push_back(T);
    T = NT.split();
  }
  return R;
}

QualType ASTContext::getCommonSugaredType(QualType X, QualType Y,
                                          bool Unqualified) {
  assert(Unqualified ? hasSameUnqualifiedType(X, Y) : hasSameType(X, Y));
  if (X == Y)
    return X;
  if (!Unqualified) {
    if (X.isCanonical())
      return X;
    if (Y.isCanonical())
      return Y;
  }

  SplitQualType SX = X.split(), SY = Y.split();
  Qualifiers QX, QY;
  // Desugar SX and SY, setting the sugar and qualifiers aside into Xs and Ys,
  // until we reach their underlying "canonical nodes". Note these are not
  // necessarily canonical types, as they may still have sugared properties.
  // QX and QY will store the sum of all qualifiers in Xs and Ys respectively.
  auto Xs = ::unwrapSugar(SX, QX), Ys = ::unwrapSugar(SY, QY);
  if (SX.Ty != SY.Ty) {
    // The canonical nodes differ. Build a common canonical node out of the two,
    // unifying their sugar. This may recurse back here.
    SX.Ty =
        ::getCommonNonSugarTypeNode(*this, SX.Ty, QX, SY.Ty, QY).getTypePtr();
  } else {
    // The canonical nodes were identical: We may have desugared too much.
    // Add any common sugar back in.
    while (!Xs.empty() && !Ys.empty() && Xs.back().Ty == Ys.back().Ty) {
      QX -= SX.Quals;
      QY -= SY.Quals;
      SX = Xs.pop_back_val();
      SY = Ys.pop_back_val();
    }
  }
  if (Unqualified)
    QX = Qualifiers::removeCommonQualifiers(QX, QY);
  else
    assert(QX == QY);

  // Even though the remaining sugar nodes in Xs and Ys differ, some may be
  // related. Walk up these nodes, unifying them and adding the result.
  while (!Xs.empty() && !Ys.empty()) {
    auto Underlying = SplitQualType(
        SX.Ty, Qualifiers::removeCommonQualifiers(SX.Quals, SY.Quals));
    SX = Xs.pop_back_val();
    SY = Ys.pop_back_val();
    SX.Ty = ::getCommonSugarTypeNode(*this, SX.Ty, SY.Ty, Underlying)
                .getTypePtrOrNull();
    // Stop at the first pair which is unrelated.
    if (!SX.Ty) {
      SX.Ty = Underlying.Ty;
      break;
    }
    QX -= Underlying.Quals;
  };

  // Add back the missing accumulated qualifiers, which were stripped off
  // with the sugar nodes we could not unify.
  QualType R = getQualifiedType(SX.Ty, QX);
  assert(Unqualified ? hasSameUnqualifiedType(R, X) : hasSameType(R, X));
  return R;
}

QualType ASTContext::getCorrespondingUnsaturatedType(QualType Ty) const {
  assert(Ty->isFixedPointType());

  if (Ty->isUnsaturatedFixedPointType())
    return Ty;

  switch (Ty->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Not a saturated fixed point type!");
  case BuiltinType::SatShortAccum:
    return ShortAccumTy;
  case BuiltinType::SatAccum:
    return AccumTy;
  case BuiltinType::SatLongAccum:
    return LongAccumTy;
  case BuiltinType::SatUShortAccum:
    return UnsignedShortAccumTy;
  case BuiltinType::SatUAccum:
    return UnsignedAccumTy;
  case BuiltinType::SatULongAccum:
    return UnsignedLongAccumTy;
  case BuiltinType::SatShortFract:
    return ShortFractTy;
  case BuiltinType::SatFract:
    return FractTy;
  case BuiltinType::SatLongFract:
    return LongFractTy;
  case BuiltinType::SatUShortFract:
    return UnsignedShortFractTy;
  case BuiltinType::SatUFract:
    return UnsignedFractTy;
  case BuiltinType::SatULongFract:
    return UnsignedLongFractTy;
  }
}

QualType ASTContext::getCorrespondingSaturatedType(QualType Ty) const {
  assert(Ty->isFixedPointType());

  if (Ty->isSaturatedFixedPointType()) return Ty;

  switch (Ty->castAs<BuiltinType>()->getKind()) {
    default:
      llvm_unreachable("Not a fixed point type!");
    case BuiltinType::ShortAccum:
      return SatShortAccumTy;
    case BuiltinType::Accum:
      return SatAccumTy;
    case BuiltinType::LongAccum:
      return SatLongAccumTy;
    case BuiltinType::UShortAccum:
      return SatUnsignedShortAccumTy;
    case BuiltinType::UAccum:
      return SatUnsignedAccumTy;
    case BuiltinType::ULongAccum:
      return SatUnsignedLongAccumTy;
    case BuiltinType::ShortFract:
      return SatShortFractTy;
    case BuiltinType::Fract:
      return SatFractTy;
    case BuiltinType::LongFract:
      return SatLongFractTy;
    case BuiltinType::UShortFract:
      return SatUnsignedShortFractTy;
    case BuiltinType::UFract:
      return SatUnsignedFractTy;
    case BuiltinType::ULongFract:
      return SatUnsignedLongFractTy;
  }
}

LangAS ASTContext::getLangASForBuiltinAddressSpace(unsigned AS) const {
  if (LangOpts.OpenCL)
    return getTargetInfo().getOpenCLBuiltinAddressSpace(AS);

  if (LangOpts.CUDA)
    return getTargetInfo().getCUDABuiltinAddressSpace(AS);

  return getLangASFromTargetAS(AS);
}

// Explicitly instantiate this in case a Redeclarable<T> is used from a TU that
// doesn't include ASTContext.h
template
clang::LazyGenerationalUpdatePtr<
    const Decl *, Decl *, &ExternalASTSource::CompleteRedeclChain>::ValueType
clang::LazyGenerationalUpdatePtr<
    const Decl *, Decl *, &ExternalASTSource::CompleteRedeclChain>::makeValue(
        const clang::ASTContext &Ctx, Decl *Value);

unsigned char ASTContext::getFixedPointScale(QualType Ty) const {
  assert(Ty->isFixedPointType());

  const TargetInfo &Target = getTargetInfo();
  switch (Ty->castAs<BuiltinType>()->getKind()) {
    default:
      llvm_unreachable("Not a fixed point type!");
    case BuiltinType::ShortAccum:
    case BuiltinType::SatShortAccum:
      return Target.getShortAccumScale();
    case BuiltinType::Accum:
    case BuiltinType::SatAccum:
      return Target.getAccumScale();
    case BuiltinType::LongAccum:
    case BuiltinType::SatLongAccum:
      return Target.getLongAccumScale();
    case BuiltinType::UShortAccum:
    case BuiltinType::SatUShortAccum:
      return Target.getUnsignedShortAccumScale();
    case BuiltinType::UAccum:
    case BuiltinType::SatUAccum:
      return Target.getUnsignedAccumScale();
    case BuiltinType::ULongAccum:
    case BuiltinType::SatULongAccum:
      return Target.getUnsignedLongAccumScale();
    case BuiltinType::ShortFract:
    case BuiltinType::SatShortFract:
      return Target.getShortFractScale();
    case BuiltinType::Fract:
    case BuiltinType::SatFract:
      return Target.getFractScale();
    case BuiltinType::LongFract:
    case BuiltinType::SatLongFract:
      return Target.getLongFractScale();
    case BuiltinType::UShortFract:
    case BuiltinType::SatUShortFract:
      return Target.getUnsignedShortFractScale();
    case BuiltinType::UFract:
    case BuiltinType::SatUFract:
      return Target.getUnsignedFractScale();
    case BuiltinType::ULongFract:
    case BuiltinType::SatULongFract:
      return Target.getUnsignedLongFractScale();
  }
}

unsigned char ASTContext::getFixedPointIBits(QualType Ty) const {
  assert(Ty->isFixedPointType());

  const TargetInfo &Target = getTargetInfo();
  switch (Ty->castAs<BuiltinType>()->getKind()) {
    default:
      llvm_unreachable("Not a fixed point type!");
    case BuiltinType::ShortAccum:
    case BuiltinType::SatShortAccum:
      return Target.getShortAccumIBits();
    case BuiltinType::Accum:
    case BuiltinType::SatAccum:
      return Target.getAccumIBits();
    case BuiltinType::LongAccum:
    case BuiltinType::SatLongAccum:
      return Target.getLongAccumIBits();
    case BuiltinType::UShortAccum:
    case BuiltinType::SatUShortAccum:
      return Target.getUnsignedShortAccumIBits();
    case BuiltinType::UAccum:
    case BuiltinType::SatUAccum:
      return Target.getUnsignedAccumIBits();
    case BuiltinType::ULongAccum:
    case BuiltinType::SatULongAccum:
      return Target.getUnsignedLongAccumIBits();
    case BuiltinType::ShortFract:
    case BuiltinType::SatShortFract:
    case BuiltinType::Fract:
    case BuiltinType::SatFract:
    case BuiltinType::LongFract:
    case BuiltinType::SatLongFract:
    case BuiltinType::UShortFract:
    case BuiltinType::SatUShortFract:
    case BuiltinType::UFract:
    case BuiltinType::SatUFract:
    case BuiltinType::ULongFract:
    case BuiltinType::SatULongFract:
      return 0;
  }
}

llvm::FixedPointSemantics
ASTContext::getFixedPointSemantics(QualType Ty) const {
  assert((Ty->isFixedPointType() || Ty->isIntegerType()) &&
         "Can only get the fixed point semantics for a "
         "fixed point or integer type.");
  if (Ty->isIntegerType())
    return llvm::FixedPointSemantics::GetIntegerSemantics(
        getIntWidth(Ty), Ty->isSignedIntegerType());

  bool isSigned = Ty->isSignedFixedPointType();
  return llvm::FixedPointSemantics(
      static_cast<unsigned>(getTypeSize(Ty)), getFixedPointScale(Ty), isSigned,
      Ty->isSaturatedFixedPointType(),
      !isSigned && getTargetInfo().doUnsignedFixedPointTypesHavePadding());
}

llvm::APFixedPoint ASTContext::getFixedPointMax(QualType Ty) const {
  assert(Ty->isFixedPointType());
  return llvm::APFixedPoint::getMax(getFixedPointSemantics(Ty));
}

llvm::APFixedPoint ASTContext::getFixedPointMin(QualType Ty) const {
  assert(Ty->isFixedPointType());
  return llvm::APFixedPoint::getMin(getFixedPointSemantics(Ty));
}

QualType ASTContext::getCorrespondingSignedFixedPointType(QualType Ty) const {
  assert(Ty->isUnsignedFixedPointType() &&
         "Expected unsigned fixed point type");

  switch (Ty->castAs<BuiltinType>()->getKind()) {
  case BuiltinType::UShortAccum:
    return ShortAccumTy;
  case BuiltinType::UAccum:
    return AccumTy;
  case BuiltinType::ULongAccum:
    return LongAccumTy;
  case BuiltinType::SatUShortAccum:
    return SatShortAccumTy;
  case BuiltinType::SatUAccum:
    return SatAccumTy;
  case BuiltinType::SatULongAccum:
    return SatLongAccumTy;
  case BuiltinType::UShortFract:
    return ShortFractTy;
  case BuiltinType::UFract:
    return FractTy;
  case BuiltinType::ULongFract:
    return LongFractTy;
  case BuiltinType::SatUShortFract:
    return SatShortFractTy;
  case BuiltinType::SatUFract:
    return SatFractTy;
  case BuiltinType::SatULongFract:
    return SatLongFractTy;
  default:
    llvm_unreachable("Unexpected unsigned fixed point type");
  }
}

// Given a list of FMV features, return a concatenated list of the
// corresponding backend features (which may contain duplicates).
static std::vector<std::string> getFMVBackendFeaturesFor(
    const llvm::SmallVectorImpl<StringRef> &FMVFeatStrings) {
  std::vector<std::string> BackendFeats;
  for (StringRef F : FMVFeatStrings)
    if (auto FMVExt = llvm::AArch64::parseFMVExtension(F))
      for (StringRef F : FMVExt->getImpliedFeatures())
        BackendFeats.push_back(F.str());
  return BackendFeats;
}

ParsedTargetAttr
ASTContext::filterFunctionTargetAttrs(const TargetAttr *TD) const {
  assert(TD != nullptr);
  ParsedTargetAttr ParsedAttr = Target->parseTargetAttr(TD->getFeaturesStr());

  llvm::erase_if(ParsedAttr.Features, [&](const std::string &Feat) {
    return !Target->isValidFeatureName(StringRef{Feat}.substr(1));
  });
  return ParsedAttr;
}

void ASTContext::getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                                       const FunctionDecl *FD) const {
  if (FD)
    getFunctionFeatureMap(FeatureMap, GlobalDecl().getWithDecl(FD));
  else
    Target->initFeatureMap(FeatureMap, getDiagnostics(),
                           Target->getTargetOpts().CPU,
                           Target->getTargetOpts().Features);
}

// Fills in the supplied string map with the set of target features for the
// passed in function.
void ASTContext::getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                                       GlobalDecl GD) const {
  StringRef TargetCPU = Target->getTargetOpts().CPU;
  const FunctionDecl *FD = GD.getDecl()->getAsFunction();
  if (const auto *TD = FD->getAttr<TargetAttr>()) {
    ParsedTargetAttr ParsedAttr = filterFunctionTargetAttrs(TD);

    // Make a copy of the features as passed on the command line into the
    // beginning of the additional features from the function to override.
    // AArch64 handles command line option features in parseTargetAttr().
    if (!Target->getTriple().isAArch64())
      ParsedAttr.Features.insert(
          ParsedAttr.Features.begin(),
          Target->getTargetOpts().FeaturesAsWritten.begin(),
          Target->getTargetOpts().FeaturesAsWritten.end());

    if (ParsedAttr.CPU != "" && Target->isValidCPUName(ParsedAttr.CPU))
      TargetCPU = ParsedAttr.CPU;

    // Now populate the feature map, first with the TargetCPU which is either
    // the default or a new one from the target attribute string. Then we'll use
    // the passed in features (FeaturesAsWritten) along with the new ones from
    // the attribute.
    Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU,
                           ParsedAttr.Features);
  } else if (const auto *SD = FD->getAttr<CPUSpecificAttr>()) {
    llvm::SmallVector<StringRef, 32> FeaturesTmp;
    Target->getCPUSpecificCPUDispatchFeatures(
        SD->getCPUName(GD.getMultiVersionIndex())->getName(), FeaturesTmp);
    std::vector<std::string> Features(FeaturesTmp.begin(), FeaturesTmp.end());
    Features.insert(Features.begin(),
                    Target->getTargetOpts().FeaturesAsWritten.begin(),
                    Target->getTargetOpts().FeaturesAsWritten.end());
    Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU, Features);
  } else if (const auto *TC = FD->getAttr<TargetClonesAttr>()) {
    if (Target->getTriple().isAArch64()) {
      llvm::SmallVector<StringRef, 8> Feats;
      TC->getFeatures(Feats, GD.getMultiVersionIndex());
      std::vector<std::string> Features = getFMVBackendFeaturesFor(Feats);
      Features.insert(Features.begin(),
                      Target->getTargetOpts().FeaturesAsWritten.begin(),
                      Target->getTargetOpts().FeaturesAsWritten.end());
      Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU, Features);
    } else {
      std::vector<std::string> Features;
      StringRef VersionStr = TC->getFeatureStr(GD.getMultiVersionIndex());
      if (VersionStr.starts_with("arch="))
        TargetCPU = VersionStr.drop_front(sizeof("arch=") - 1);
      else if (VersionStr != "default")
        Features.push_back((StringRef{"+"} + VersionStr).str());
      Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU, Features);
    }
  } else if (const auto *TV = FD->getAttr<TargetVersionAttr>()) {
    llvm::SmallVector<StringRef, 8> Feats;
    TV->getFeatures(Feats);
    std::vector<std::string> Features = getFMVBackendFeaturesFor(Feats);
    Features.insert(Features.begin(),
                    Target->getTargetOpts().FeaturesAsWritten.begin(),
                    Target->getTargetOpts().FeaturesAsWritten.end());
    Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU, Features);
  } else {
    FeatureMap = Target->getTargetOpts().FeatureMap;
  }
}

OMPTraitInfo &ASTContext::getNewOMPTraitInfo() {
  OMPTraitInfoVector.emplace_back(new OMPTraitInfo());
  return *OMPTraitInfoVector.back();
}

const StreamingDiagnostic &clang::
operator<<(const StreamingDiagnostic &DB,
           const ASTContext::SectionInfo &Section) {
  if (Section.Decl)
    return DB << Section.Decl;
  return DB << "a prior #pragma section";
}

bool ASTContext::mayExternalize(const Decl *D) const {
  bool IsInternalVar =
      isa<VarDecl>(D) &&
      basicGVALinkageForVariable(*this, cast<VarDecl>(D)) == GVA_Internal;
  bool IsExplicitDeviceVar = (D->hasAttr<CUDADeviceAttr>() &&
                              !D->getAttr<CUDADeviceAttr>()->isImplicit()) ||
                             (D->hasAttr<CUDAConstantAttr>() &&
                              !D->getAttr<CUDAConstantAttr>()->isImplicit());
  // CUDA/HIP: managed variables need to be externalized since it is
  // a declaration in IR, therefore cannot have internal linkage. Kernels in
  // anonymous name space needs to be externalized to avoid duplicate symbols.
  return (IsInternalVar &&
          (D->hasAttr<HIPManagedAttr>() || IsExplicitDeviceVar)) ||
         (D->hasAttr<CUDAGlobalAttr>() &&
          basicGVALinkageForFunction(*this, cast<FunctionDecl>(D)) ==
              GVA_Internal);
}

bool ASTContext::shouldExternalize(const Decl *D) const {
  return mayExternalize(D) &&
         (D->hasAttr<HIPManagedAttr>() || D->hasAttr<CUDAGlobalAttr>() ||
          CUDADeviceVarODRUsedByHost.count(cast<VarDecl>(D)));
}

StringRef ASTContext::getCUIDHash() const {
  if (!CUIDHash.empty())
    return CUIDHash;
  if (LangOpts.CUID.empty())
    return StringRef();
  CUIDHash = llvm::utohexstr(llvm::MD5Hash(LangOpts.CUID), /*LowerCase=*/true);
  return CUIDHash;
}

const CXXRecordDecl *
ASTContext::baseForVTableAuthentication(const CXXRecordDecl *ThisClass) {
  assert(ThisClass);
  assert(ThisClass->isPolymorphic());
  const CXXRecordDecl *PrimaryBase = ThisClass;
  while (1) {
    assert(PrimaryBase);
    assert(PrimaryBase->isPolymorphic());
    auto &Layout = getASTRecordLayout(PrimaryBase);
    auto Base = Layout.getPrimaryBase();
    if (!Base || Base == PrimaryBase || !Base->isPolymorphic())
      break;
    PrimaryBase = Base;
  }
  return PrimaryBase;
}

bool ASTContext::useAbbreviatedThunkName(GlobalDecl VirtualMethodDecl,
                                         StringRef MangledName) {
  auto *Method = cast<CXXMethodDecl>(VirtualMethodDecl.getDecl());
  assert(Method->isVirtual());
  bool DefaultIncludesPointerAuth =
      LangOpts.PointerAuthCalls || LangOpts.PointerAuthIntrinsics;

  if (!DefaultIncludesPointerAuth)
    return true;

  auto Existing = ThunksToBeAbbreviated.find(VirtualMethodDecl);
  if (Existing != ThunksToBeAbbreviated.end())
    return Existing->second.contains(MangledName.str());

  std::unique_ptr<MangleContext> Mangler(createMangleContext());
  llvm::StringMap<llvm::SmallVector<std::string, 2>> Thunks;
  auto VtableContext = getVTableContext();
  if (const auto *ThunkInfos = VtableContext->getThunkInfo(VirtualMethodDecl)) {
    auto *Destructor = dyn_cast<CXXDestructorDecl>(Method);
    for (const auto &Thunk : *ThunkInfos) {
      SmallString<256> ElidedName;
      llvm::raw_svector_ostream ElidedNameStream(ElidedName);
      if (Destructor)
        Mangler->mangleCXXDtorThunk(Destructor, VirtualMethodDecl.getDtorType(),
                                    Thunk, /* elideOverrideInfo */ true,
                                    ElidedNameStream);
      else
        Mangler->mangleThunk(Method, Thunk, /* elideOverrideInfo */ true,
                             ElidedNameStream);
      SmallString<256> MangledName;
      llvm::raw_svector_ostream mangledNameStream(MangledName);
      if (Destructor)
        Mangler->mangleCXXDtorThunk(Destructor, VirtualMethodDecl.getDtorType(),
                                    Thunk, /* elideOverrideInfo */ false,
                                    mangledNameStream);
      else
        Mangler->mangleThunk(Method, Thunk, /* elideOverrideInfo */ false,
                             mangledNameStream);

      if (Thunks.find(ElidedName) == Thunks.end())
        Thunks[ElidedName] = {};
      Thunks[ElidedName].push_back(std::string(MangledName));
    }
  }
  llvm::StringSet<> SimplifiedThunkNames;
  for (auto &ThunkList : Thunks) {
    llvm::sort(ThunkList.second);
    SimplifiedThunkNames.insert(ThunkList.second[0]);
  }
  bool Result = SimplifiedThunkNames.contains(MangledName);
  ThunksToBeAbbreviated[VirtualMethodDecl] = std::move(SimplifiedThunkNames);
  return Result;
}
