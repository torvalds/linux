//===- ASTReaderDecl.cpp - Decl Deserialization ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ASTReader::readDeclRecord method, which is the
// entrypoint for loading a decl.
//
//===----------------------------------------------------------------------===//

#include "ASTCommon.h"
#include "ASTReaderInternals.h"
#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/Attr.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Redeclarable.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/PragmaKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/Stack.h"
#include "clang/Sema/IdentifierResolver.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTRecordReader.h"
#include "clang/Serialization/ContinuousRangeMap.h"
#include "clang/Serialization/ModuleFile.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SaveAndRestore.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

using namespace clang;
using namespace serialization;

//===----------------------------------------------------------------------===//
// Declaration deserialization
//===----------------------------------------------------------------------===//

namespace clang {

  class ASTDeclReader : public DeclVisitor<ASTDeclReader, void> {
    ASTReader &Reader;
    ASTRecordReader &Record;
    ASTReader::RecordLocation Loc;
    const GlobalDeclID ThisDeclID;
    const SourceLocation ThisDeclLoc;

    using RecordData = ASTReader::RecordData;

    TypeID DeferredTypeID = 0;
    unsigned AnonymousDeclNumber = 0;
    GlobalDeclID NamedDeclForTagDecl = GlobalDeclID();
    IdentifierInfo *TypedefNameForLinkage = nullptr;

    ///A flag to carry the information for a decl from the entity is
    /// used. We use it to delay the marking of the canonical decl as used until
    /// the entire declaration is deserialized and merged.
    bool IsDeclMarkedUsed = false;

    uint64_t GetCurrentCursorOffset();

    uint64_t ReadLocalOffset() {
      uint64_t LocalOffset = Record.readInt();
      assert(LocalOffset < Loc.Offset && "offset point after current record");
      return LocalOffset ? Loc.Offset - LocalOffset : 0;
    }

    uint64_t ReadGlobalOffset() {
      uint64_t Local = ReadLocalOffset();
      return Local ? Record.getGlobalBitOffset(Local) : 0;
    }

    SourceLocation readSourceLocation() {
      return Record.readSourceLocation();
    }

    SourceRange readSourceRange() {
      return Record.readSourceRange();
    }

    TypeSourceInfo *readTypeSourceInfo() {
      return Record.readTypeSourceInfo();
    }

    GlobalDeclID readDeclID() { return Record.readDeclID(); }

    std::string readString() {
      return Record.readString();
    }

    void readDeclIDList(SmallVectorImpl<GlobalDeclID> &IDs) {
      for (unsigned I = 0, Size = Record.readInt(); I != Size; ++I)
        IDs.push_back(readDeclID());
    }

    Decl *readDecl() {
      return Record.readDecl();
    }

    template<typename T>
    T *readDeclAs() {
      return Record.readDeclAs<T>();
    }

    serialization::SubmoduleID readSubmoduleID() {
      if (Record.getIdx() == Record.size())
        return 0;

      return Record.getGlobalSubmoduleID(Record.readInt());
    }

    Module *readModule() {
      return Record.getSubmodule(readSubmoduleID());
    }

    void ReadCXXRecordDefinition(CXXRecordDecl *D, bool Update,
                                 Decl *LambdaContext = nullptr,
                                 unsigned IndexInLambdaContext = 0);
    void ReadCXXDefinitionData(struct CXXRecordDecl::DefinitionData &Data,
                               const CXXRecordDecl *D, Decl *LambdaContext,
                               unsigned IndexInLambdaContext);
    void MergeDefinitionData(CXXRecordDecl *D,
                             struct CXXRecordDecl::DefinitionData &&NewDD);
    void ReadObjCDefinitionData(struct ObjCInterfaceDecl::DefinitionData &Data);
    void MergeDefinitionData(ObjCInterfaceDecl *D,
                             struct ObjCInterfaceDecl::DefinitionData &&NewDD);
    void ReadObjCDefinitionData(struct ObjCProtocolDecl::DefinitionData &Data);
    void MergeDefinitionData(ObjCProtocolDecl *D,
                             struct ObjCProtocolDecl::DefinitionData &&NewDD);

    static DeclContext *getPrimaryDCForAnonymousDecl(DeclContext *LexicalDC);

    static NamedDecl *getAnonymousDeclForMerging(ASTReader &Reader,
                                                 DeclContext *DC,
                                                 unsigned Index);
    static void setAnonymousDeclForMerging(ASTReader &Reader, DeclContext *DC,
                                           unsigned Index, NamedDecl *D);

    /// Commit to a primary definition of the class RD, which is known to be
    /// a definition of the class. We might not have read the definition data
    /// for it yet. If we haven't then allocate placeholder definition data
    /// now too.
    static CXXRecordDecl *getOrFakePrimaryClassDefinition(ASTReader &Reader,
                                                          CXXRecordDecl *RD);

    /// Results from loading a RedeclarableDecl.
    class RedeclarableResult {
      Decl *MergeWith;
      GlobalDeclID FirstID;
      bool IsKeyDecl;

    public:
      RedeclarableResult(Decl *MergeWith, GlobalDeclID FirstID, bool IsKeyDecl)
          : MergeWith(MergeWith), FirstID(FirstID), IsKeyDecl(IsKeyDecl) {}

      /// Retrieve the first ID.
      GlobalDeclID getFirstID() const { return FirstID; }

      /// Is this declaration a key declaration?
      bool isKeyDecl() const { return IsKeyDecl; }

      /// Get a known declaration that this should be merged with, if
      /// any.
      Decl *getKnownMergeTarget() const { return MergeWith; }
    };

    /// Class used to capture the result of searching for an existing
    /// declaration of a specific kind and name, along with the ability
    /// to update the place where this result was found (the declaration
    /// chain hanging off an identifier or the DeclContext we searched in)
    /// if requested.
    class FindExistingResult {
      ASTReader &Reader;
      NamedDecl *New = nullptr;
      NamedDecl *Existing = nullptr;
      bool AddResult = false;
      unsigned AnonymousDeclNumber = 0;
      IdentifierInfo *TypedefNameForLinkage = nullptr;

    public:
      FindExistingResult(ASTReader &Reader) : Reader(Reader) {}

      FindExistingResult(ASTReader &Reader, NamedDecl *New, NamedDecl *Existing,
                         unsigned AnonymousDeclNumber,
                         IdentifierInfo *TypedefNameForLinkage)
          : Reader(Reader), New(New), Existing(Existing), AddResult(true),
            AnonymousDeclNumber(AnonymousDeclNumber),
            TypedefNameForLinkage(TypedefNameForLinkage) {}

      FindExistingResult(FindExistingResult &&Other)
          : Reader(Other.Reader), New(Other.New), Existing(Other.Existing),
            AddResult(Other.AddResult),
            AnonymousDeclNumber(Other.AnonymousDeclNumber),
            TypedefNameForLinkage(Other.TypedefNameForLinkage) {
        Other.AddResult = false;
      }

      FindExistingResult &operator=(FindExistingResult &&) = delete;
      ~FindExistingResult();

      /// Suppress the addition of this result into the known set of
      /// names.
      void suppress() { AddResult = false; }

      operator NamedDecl*() const { return Existing; }

      template<typename T>
      operator T*() const { return dyn_cast_or_null<T>(Existing); }
    };

    static DeclContext *getPrimaryContextForMerging(ASTReader &Reader,
                                                    DeclContext *DC);
    FindExistingResult findExisting(NamedDecl *D);

  public:
    ASTDeclReader(ASTReader &Reader, ASTRecordReader &Record,
                  ASTReader::RecordLocation Loc, GlobalDeclID thisDeclID,
                  SourceLocation ThisDeclLoc)
        : Reader(Reader), Record(Record), Loc(Loc), ThisDeclID(thisDeclID),
          ThisDeclLoc(ThisDeclLoc) {}

    template <typename T>
    static void AddLazySpecializations(T *D,
                                       SmallVectorImpl<GlobalDeclID> &IDs) {
      if (IDs.empty())
        return;

      // FIXME: We should avoid this pattern of getting the ASTContext.
      ASTContext &C = D->getASTContext();

      auto *&LazySpecializations = D->getCommonPtr()->LazySpecializations;

      if (auto &Old = LazySpecializations) {
        IDs.insert(IDs.end(), Old + 1, Old + 1 + Old[0].getRawValue());
        llvm::sort(IDs);
        IDs.erase(std::unique(IDs.begin(), IDs.end()), IDs.end());
      }

      auto *Result = new (C) GlobalDeclID[1 + IDs.size()];
      *Result = GlobalDeclID(IDs.size());

      std::copy(IDs.begin(), IDs.end(), Result + 1);

      LazySpecializations = Result;
    }

    template <typename DeclT>
    static Decl *getMostRecentDeclImpl(Redeclarable<DeclT> *D);
    static Decl *getMostRecentDeclImpl(...);
    static Decl *getMostRecentDecl(Decl *D);

    static void mergeInheritableAttributes(ASTReader &Reader, Decl *D,
                                           Decl *Previous);

    template <typename DeclT>
    static void attachPreviousDeclImpl(ASTReader &Reader,
                                       Redeclarable<DeclT> *D, Decl *Previous,
                                       Decl *Canon);
    static void attachPreviousDeclImpl(ASTReader &Reader, ...);
    static void attachPreviousDecl(ASTReader &Reader, Decl *D, Decl *Previous,
                                   Decl *Canon);

    template <typename DeclT>
    static void attachLatestDeclImpl(Redeclarable<DeclT> *D, Decl *Latest);
    static void attachLatestDeclImpl(...);
    static void attachLatestDecl(Decl *D, Decl *latest);

    template <typename DeclT>
    static void markIncompleteDeclChainImpl(Redeclarable<DeclT> *D);
    static void markIncompleteDeclChainImpl(...);

    void ReadFunctionDefinition(FunctionDecl *FD);
    void Visit(Decl *D);

    void UpdateDecl(Decl *D, SmallVectorImpl<GlobalDeclID> &);

    static void setNextObjCCategory(ObjCCategoryDecl *Cat,
                                    ObjCCategoryDecl *Next) {
      Cat->NextClassCategory = Next;
    }

    void VisitDecl(Decl *D);
    void VisitPragmaCommentDecl(PragmaCommentDecl *D);
    void VisitPragmaDetectMismatchDecl(PragmaDetectMismatchDecl *D);
    void VisitTranslationUnitDecl(TranslationUnitDecl *TU);
    void VisitNamedDecl(NamedDecl *ND);
    void VisitLabelDecl(LabelDecl *LD);
    void VisitNamespaceDecl(NamespaceDecl *D);
    void VisitHLSLBufferDecl(HLSLBufferDecl *D);
    void VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
    void VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
    void VisitTypeDecl(TypeDecl *TD);
    RedeclarableResult VisitTypedefNameDecl(TypedefNameDecl *TD);
    void VisitTypedefDecl(TypedefDecl *TD);
    void VisitTypeAliasDecl(TypeAliasDecl *TD);
    void VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
    void VisitUnresolvedUsingIfExistsDecl(UnresolvedUsingIfExistsDecl *D);
    RedeclarableResult VisitTagDecl(TagDecl *TD);
    void VisitEnumDecl(EnumDecl *ED);
    RedeclarableResult VisitRecordDeclImpl(RecordDecl *RD);
    void VisitRecordDecl(RecordDecl *RD);
    RedeclarableResult VisitCXXRecordDeclImpl(CXXRecordDecl *D);
    void VisitCXXRecordDecl(CXXRecordDecl *D) { VisitCXXRecordDeclImpl(D); }
    RedeclarableResult VisitClassTemplateSpecializationDeclImpl(
                                            ClassTemplateSpecializationDecl *D);

    void VisitClassTemplateSpecializationDecl(
        ClassTemplateSpecializationDecl *D) {
      VisitClassTemplateSpecializationDeclImpl(D);
    }

    void VisitClassTemplatePartialSpecializationDecl(
        ClassTemplatePartialSpecializationDecl *D);
    RedeclarableResult
    VisitVarTemplateSpecializationDeclImpl(VarTemplateSpecializationDecl *D);

    void VisitVarTemplateSpecializationDecl(VarTemplateSpecializationDecl *D) {
      VisitVarTemplateSpecializationDeclImpl(D);
    }

    void VisitVarTemplatePartialSpecializationDecl(
        VarTemplatePartialSpecializationDecl *D);
    void VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D);
    void VisitValueDecl(ValueDecl *VD);
    void VisitEnumConstantDecl(EnumConstantDecl *ECD);
    void VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
    void VisitDeclaratorDecl(DeclaratorDecl *DD);
    void VisitFunctionDecl(FunctionDecl *FD);
    void VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *GD);
    void VisitCXXMethodDecl(CXXMethodDecl *D);
    void VisitCXXConstructorDecl(CXXConstructorDecl *D);
    void VisitCXXDestructorDecl(CXXDestructorDecl *D);
    void VisitCXXConversionDecl(CXXConversionDecl *D);
    void VisitFieldDecl(FieldDecl *FD);
    void VisitMSPropertyDecl(MSPropertyDecl *FD);
    void VisitMSGuidDecl(MSGuidDecl *D);
    void VisitUnnamedGlobalConstantDecl(UnnamedGlobalConstantDecl *D);
    void VisitTemplateParamObjectDecl(TemplateParamObjectDecl *D);
    void VisitIndirectFieldDecl(IndirectFieldDecl *FD);
    RedeclarableResult VisitVarDeclImpl(VarDecl *D);
    void ReadVarDeclInit(VarDecl *VD);
    void VisitVarDecl(VarDecl *VD) { VisitVarDeclImpl(VD); }
    void VisitImplicitParamDecl(ImplicitParamDecl *PD);
    void VisitParmVarDecl(ParmVarDecl *PD);
    void VisitDecompositionDecl(DecompositionDecl *DD);
    void VisitBindingDecl(BindingDecl *BD);
    void VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D);
    void VisitTemplateDecl(TemplateDecl *D);
    void VisitConceptDecl(ConceptDecl *D);
    void VisitImplicitConceptSpecializationDecl(
        ImplicitConceptSpecializationDecl *D);
    void VisitRequiresExprBodyDecl(RequiresExprBodyDecl *D);
    RedeclarableResult VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D);
    void VisitClassTemplateDecl(ClassTemplateDecl *D);
    void VisitBuiltinTemplateDecl(BuiltinTemplateDecl *D);
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
    void VisitFileScopeAsmDecl(FileScopeAsmDecl *AD);
    void VisitTopLevelStmtDecl(TopLevelStmtDecl *D);
    void VisitImportDecl(ImportDecl *D);
    void VisitAccessSpecDecl(AccessSpecDecl *D);
    void VisitFriendDecl(FriendDecl *D);
    void VisitFriendTemplateDecl(FriendTemplateDecl *D);
    void VisitStaticAssertDecl(StaticAssertDecl *D);
    void VisitBlockDecl(BlockDecl *BD);
    void VisitCapturedDecl(CapturedDecl *CD);
    void VisitEmptyDecl(EmptyDecl *D);
    void VisitLifetimeExtendedTemporaryDecl(LifetimeExtendedTemporaryDecl *D);

    std::pair<uint64_t, uint64_t> VisitDeclContext(DeclContext *DC);

    template<typename T>
    RedeclarableResult VisitRedeclarable(Redeclarable<T> *D);

    template <typename T>
    void mergeRedeclarable(Redeclarable<T> *D, RedeclarableResult &Redecl);

    void mergeLambda(CXXRecordDecl *D, RedeclarableResult &Redecl,
                     Decl *Context, unsigned Number);

    void mergeRedeclarableTemplate(RedeclarableTemplateDecl *D,
                                   RedeclarableResult &Redecl);

    template <typename T>
    void mergeRedeclarable(Redeclarable<T> *D, T *Existing,
                           RedeclarableResult &Redecl);

    template<typename T>
    void mergeMergeable(Mergeable<T> *D);

    void mergeMergeable(LifetimeExtendedTemporaryDecl *D);

    void mergeTemplatePattern(RedeclarableTemplateDecl *D,
                              RedeclarableTemplateDecl *Existing,
                              bool IsKeyDecl);

    ObjCTypeParamList *ReadObjCTypeParamList();

    // FIXME: Reorder according to DeclNodes.td?
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
    void VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D);
    void VisitOMPDeclareMapperDecl(OMPDeclareMapperDecl *D);
    void VisitOMPRequiresDecl(OMPRequiresDecl *D);
    void VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D);
  };

} // namespace clang

namespace {

/// Iterator over the redeclarations of a declaration that have already
/// been merged into the same redeclaration chain.
template <typename DeclT> class MergedRedeclIterator {
  DeclT *Start = nullptr;
  DeclT *Canonical = nullptr;
  DeclT *Current = nullptr;

public:
  MergedRedeclIterator() = default;
  MergedRedeclIterator(DeclT *Start) : Start(Start), Current(Start) {}

  DeclT *operator*() { return Current; }

  MergedRedeclIterator &operator++() {
    if (Current->isFirstDecl()) {
      Canonical = Current;
      Current = Current->getMostRecentDecl();
    } else
      Current = Current->getPreviousDecl();

    // If we started in the merged portion, we'll reach our start position
    // eventually. Otherwise, we'll never reach it, but the second declaration
    // we reached was the canonical declaration, so stop when we see that one
    // again.
    if (Current == Start || Current == Canonical)
      Current = nullptr;
    return *this;
  }

  friend bool operator!=(const MergedRedeclIterator &A,
                         const MergedRedeclIterator &B) {
    return A.Current != B.Current;
  }
};

} // namespace

template <typename DeclT>
static llvm::iterator_range<MergedRedeclIterator<DeclT>>
merged_redecls(DeclT *D) {
  return llvm::make_range(MergedRedeclIterator<DeclT>(D),
                          MergedRedeclIterator<DeclT>());
}

uint64_t ASTDeclReader::GetCurrentCursorOffset() {
  return Loc.F->DeclsCursor.GetCurrentBitNo() + Loc.F->GlobalBitOffset;
}

void ASTDeclReader::ReadFunctionDefinition(FunctionDecl *FD) {
  if (Record.readInt()) {
    Reader.DefinitionSource[FD] =
        Loc.F->Kind == ModuleKind::MK_MainFile ||
        Reader.getContext().getLangOpts().BuildingPCHWithObjectFile;
  }
  if (auto *CD = dyn_cast<CXXConstructorDecl>(FD)) {
    CD->setNumCtorInitializers(Record.readInt());
    if (CD->getNumCtorInitializers())
      CD->CtorInitializers = ReadGlobalOffset();
  }
  // Store the offset of the body so we can lazily load it later.
  Reader.PendingBodies[FD] = GetCurrentCursorOffset();
}

void ASTDeclReader::Visit(Decl *D) {
  DeclVisitor<ASTDeclReader, void>::Visit(D);

  // At this point we have deserialized and merged the decl and it is safe to
  // update its canonical decl to signal that the entire entity is used.
  D->getCanonicalDecl()->Used |= IsDeclMarkedUsed;
  IsDeclMarkedUsed = false;

  if (auto *DD = dyn_cast<DeclaratorDecl>(D)) {
    if (auto *TInfo = DD->getTypeSourceInfo())
      Record.readTypeLoc(TInfo->getTypeLoc());
  }

  if (auto *TD = dyn_cast<TypeDecl>(D)) {
    // We have a fully initialized TypeDecl. Read its type now.
    TD->setTypeForDecl(Reader.GetType(DeferredTypeID).getTypePtrOrNull());

    // If this is a tag declaration with a typedef name for linkage, it's safe
    // to load that typedef now.
    if (NamedDeclForTagDecl.isValid())
      cast<TagDecl>(D)->TypedefNameDeclOrQualifier =
          cast<TypedefNameDecl>(Reader.GetDecl(NamedDeclForTagDecl));
  } else if (auto *ID = dyn_cast<ObjCInterfaceDecl>(D)) {
    // if we have a fully initialized TypeDecl, we can safely read its type now.
    ID->TypeForDecl = Reader.GetType(DeferredTypeID).getTypePtrOrNull();
  } else if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    // FunctionDecl's body was written last after all other Stmts/Exprs.
    if (Record.readInt())
      ReadFunctionDefinition(FD);
  } else if (auto *VD = dyn_cast<VarDecl>(D)) {
    ReadVarDeclInit(VD);
  } else if (auto *FD = dyn_cast<FieldDecl>(D)) {
    if (FD->hasInClassInitializer() && Record.readInt()) {
      FD->setLazyInClassInitializer(LazyDeclStmtPtr(GetCurrentCursorOffset()));
    }
  }
}

void ASTDeclReader::VisitDecl(Decl *D) {
  BitsUnpacker DeclBits(Record.readInt());
  auto ModuleOwnership =
      (Decl::ModuleOwnershipKind)DeclBits.getNextBits(/*Width=*/3);
  D->setReferenced(DeclBits.getNextBit());
  D->Used = DeclBits.getNextBit();
  IsDeclMarkedUsed |= D->Used;
  D->setAccess((AccessSpecifier)DeclBits.getNextBits(/*Width=*/2));
  D->setImplicit(DeclBits.getNextBit());
  bool HasStandaloneLexicalDC = DeclBits.getNextBit();
  bool HasAttrs = DeclBits.getNextBit();
  D->setTopLevelDeclInObjCContainer(DeclBits.getNextBit());
  D->InvalidDecl = DeclBits.getNextBit();
  D->FromASTFile = true;

  if (D->isTemplateParameter() || D->isTemplateParameterPack() ||
      isa<ParmVarDecl, ObjCTypeParamDecl>(D)) {
    // We don't want to deserialize the DeclContext of a template
    // parameter or of a parameter of a function template immediately.   These
    // entities might be used in the formulation of its DeclContext (for
    // example, a function parameter can be used in decltype() in trailing
    // return type of the function).  Use the translation unit DeclContext as a
    // placeholder.
    GlobalDeclID SemaDCIDForTemplateParmDecl = readDeclID();
    GlobalDeclID LexicalDCIDForTemplateParmDecl =
        HasStandaloneLexicalDC ? readDeclID() : GlobalDeclID();
    if (LexicalDCIDForTemplateParmDecl.isInvalid())
      LexicalDCIDForTemplateParmDecl = SemaDCIDForTemplateParmDecl;
    Reader.addPendingDeclContextInfo(D,
                                     SemaDCIDForTemplateParmDecl,
                                     LexicalDCIDForTemplateParmDecl);
    D->setDeclContext(Reader.getContext().getTranslationUnitDecl());
  } else {
    auto *SemaDC = readDeclAs<DeclContext>();
    auto *LexicalDC =
        HasStandaloneLexicalDC ? readDeclAs<DeclContext>() : nullptr;
    if (!LexicalDC)
      LexicalDC = SemaDC;
    // If the context is a class, we might not have actually merged it yet, in
    // the case where the definition comes from an update record.
    DeclContext *MergedSemaDC;
    if (auto *RD = dyn_cast<CXXRecordDecl>(SemaDC))
      MergedSemaDC = getOrFakePrimaryClassDefinition(Reader, RD);
    else
      MergedSemaDC = Reader.MergedDeclContexts.lookup(SemaDC);
    // Avoid calling setLexicalDeclContext() directly because it uses
    // Decl::getASTContext() internally which is unsafe during derialization.
    D->setDeclContextsImpl(MergedSemaDC ? MergedSemaDC : SemaDC, LexicalDC,
                           Reader.getContext());
  }
  D->setLocation(ThisDeclLoc);

  if (HasAttrs) {
    AttrVec Attrs;
    Record.readAttributes(Attrs);
    // Avoid calling setAttrs() directly because it uses Decl::getASTContext()
    // internally which is unsafe during derialization.
    D->setAttrsImpl(Attrs, Reader.getContext());
  }

  // Determine whether this declaration is part of a (sub)module. If so, it
  // may not yet be visible.
  bool ModulePrivate =
      (ModuleOwnership == Decl::ModuleOwnershipKind::ModulePrivate);
  if (unsigned SubmoduleID = readSubmoduleID()) {
    switch (ModuleOwnership) {
    case Decl::ModuleOwnershipKind::Visible:
      ModuleOwnership = Decl::ModuleOwnershipKind::VisibleWhenImported;
      break;
    case Decl::ModuleOwnershipKind::Unowned:
    case Decl::ModuleOwnershipKind::VisibleWhenImported:
    case Decl::ModuleOwnershipKind::ReachableWhenImported:
    case Decl::ModuleOwnershipKind::ModulePrivate:
      break;
    }

    D->setModuleOwnershipKind(ModuleOwnership);
    // Store the owning submodule ID in the declaration.
    D->setOwningModuleID(SubmoduleID);

    if (ModulePrivate) {
      // Module-private declarations are never visible, so there is no work to
      // do.
    } else if (Reader.getContext().getLangOpts().ModulesLocalVisibility) {
      // If local visibility is being tracked, this declaration will become
      // hidden and visible as the owning module does.
    } else if (Module *Owner = Reader.getSubmodule(SubmoduleID)) {
      // Mark the declaration as visible when its owning module becomes visible.
      if (Owner->NameVisibility == Module::AllVisible)
        D->setVisibleDespiteOwningModule();
      else
        Reader.HiddenNamesMap[Owner].push_back(D);
    }
  } else if (ModulePrivate) {
    D->setModuleOwnershipKind(Decl::ModuleOwnershipKind::ModulePrivate);
  }
}

void ASTDeclReader::VisitPragmaCommentDecl(PragmaCommentDecl *D) {
  VisitDecl(D);
  D->setLocation(readSourceLocation());
  D->CommentKind = (PragmaMSCommentKind)Record.readInt();
  std::string Arg = readString();
  memcpy(D->getTrailingObjects<char>(), Arg.data(), Arg.size());
  D->getTrailingObjects<char>()[Arg.size()] = '\0';
}

void ASTDeclReader::VisitPragmaDetectMismatchDecl(PragmaDetectMismatchDecl *D) {
  VisitDecl(D);
  D->setLocation(readSourceLocation());
  std::string Name = readString();
  memcpy(D->getTrailingObjects<char>(), Name.data(), Name.size());
  D->getTrailingObjects<char>()[Name.size()] = '\0';

  D->ValueStart = Name.size() + 1;
  std::string Value = readString();
  memcpy(D->getTrailingObjects<char>() + D->ValueStart, Value.data(),
         Value.size());
  D->getTrailingObjects<char>()[D->ValueStart + Value.size()] = '\0';
}

void ASTDeclReader::VisitTranslationUnitDecl(TranslationUnitDecl *TU) {
  llvm_unreachable("Translation units are not serialized");
}

void ASTDeclReader::VisitNamedDecl(NamedDecl *ND) {
  VisitDecl(ND);
  ND->setDeclName(Record.readDeclarationName());
  AnonymousDeclNumber = Record.readInt();
}

void ASTDeclReader::VisitTypeDecl(TypeDecl *TD) {
  VisitNamedDecl(TD);
  TD->setLocStart(readSourceLocation());
  // Delay type reading until after we have fully initialized the decl.
  DeferredTypeID = Record.getGlobalTypeID(Record.readInt());
}

ASTDeclReader::RedeclarableResult
ASTDeclReader::VisitTypedefNameDecl(TypedefNameDecl *TD) {
  RedeclarableResult Redecl = VisitRedeclarable(TD);
  VisitTypeDecl(TD);
  TypeSourceInfo *TInfo = readTypeSourceInfo();
  if (Record.readInt()) { // isModed
    QualType modedT = Record.readType();
    TD->setModedTypeSourceInfo(TInfo, modedT);
  } else
    TD->setTypeSourceInfo(TInfo);
  // Read and discard the declaration for which this is a typedef name for
  // linkage, if it exists. We cannot rely on our type to pull in this decl,
  // because it might have been merged with a type from another module and
  // thus might not refer to our version of the declaration.
  readDecl();
  return Redecl;
}

void ASTDeclReader::VisitTypedefDecl(TypedefDecl *TD) {
  RedeclarableResult Redecl = VisitTypedefNameDecl(TD);
  mergeRedeclarable(TD, Redecl);
}

void ASTDeclReader::VisitTypeAliasDecl(TypeAliasDecl *TD) {
  RedeclarableResult Redecl = VisitTypedefNameDecl(TD);
  if (auto *Template = readDeclAs<TypeAliasTemplateDecl>())
    // Merged when we merge the template.
    TD->setDescribedAliasTemplate(Template);
  else
    mergeRedeclarable(TD, Redecl);
}

ASTDeclReader::RedeclarableResult ASTDeclReader::VisitTagDecl(TagDecl *TD) {
  RedeclarableResult Redecl = VisitRedeclarable(TD);
  VisitTypeDecl(TD);

  TD->IdentifierNamespace = Record.readInt();

  BitsUnpacker TagDeclBits(Record.readInt());
  TD->setTagKind(
      static_cast<TagTypeKind>(TagDeclBits.getNextBits(/*Width=*/3)));
  TD->setCompleteDefinition(TagDeclBits.getNextBit());
  TD->setEmbeddedInDeclarator(TagDeclBits.getNextBit());
  TD->setFreeStanding(TagDeclBits.getNextBit());
  TD->setCompleteDefinitionRequired(TagDeclBits.getNextBit());
  TD->setBraceRange(readSourceRange());

  switch (TagDeclBits.getNextBits(/*Width=*/2)) {
  case 0:
    break;
  case 1: { // ExtInfo
    auto *Info = new (Reader.getContext()) TagDecl::ExtInfo();
    Record.readQualifierInfo(*Info);
    TD->TypedefNameDeclOrQualifier = Info;
    break;
  }
  case 2: // TypedefNameForAnonDecl
    NamedDeclForTagDecl = readDeclID();
    TypedefNameForLinkage = Record.readIdentifier();
    break;
  default:
    llvm_unreachable("unexpected tag info kind");
  }

  if (!isa<CXXRecordDecl>(TD))
    mergeRedeclarable(TD, Redecl);
  return Redecl;
}

void ASTDeclReader::VisitEnumDecl(EnumDecl *ED) {
  VisitTagDecl(ED);
  if (TypeSourceInfo *TI = readTypeSourceInfo())
    ED->setIntegerTypeSourceInfo(TI);
  else
    ED->setIntegerType(Record.readType());
  ED->setPromotionType(Record.readType());

  BitsUnpacker EnumDeclBits(Record.readInt());
  ED->setNumPositiveBits(EnumDeclBits.getNextBits(/*Width=*/8));
  ED->setNumNegativeBits(EnumDeclBits.getNextBits(/*Width=*/8));
  ED->setScoped(EnumDeclBits.getNextBit());
  ED->setScopedUsingClassTag(EnumDeclBits.getNextBit());
  ED->setFixed(EnumDeclBits.getNextBit());

  ED->setHasODRHash(true);
  ED->ODRHash = Record.readInt();

  // If this is a definition subject to the ODR, and we already have a
  // definition, merge this one into it.
  if (ED->isCompleteDefinition() && Reader.getContext().getLangOpts().Modules) {
    EnumDecl *&OldDef = Reader.EnumDefinitions[ED->getCanonicalDecl()];
    if (!OldDef) {
      // This is the first time we've seen an imported definition. Look for a
      // local definition before deciding that we are the first definition.
      for (auto *D : merged_redecls(ED->getCanonicalDecl())) {
        if (!D->isFromASTFile() && D->isCompleteDefinition()) {
          OldDef = D;
          break;
        }
      }
    }
    if (OldDef) {
      Reader.MergedDeclContexts.insert(std::make_pair(ED, OldDef));
      ED->demoteThisDefinitionToDeclaration();
      Reader.mergeDefinitionVisibility(OldDef, ED);
      // We don't want to check the ODR hash value for declarations from global
      // module fragment.
      if (!shouldSkipCheckingODR(ED) && !shouldSkipCheckingODR(OldDef) &&
          OldDef->getODRHash() != ED->getODRHash())
        Reader.PendingEnumOdrMergeFailures[OldDef].push_back(ED);
    } else {
      OldDef = ED;
    }
  }

  if (auto *InstED = readDeclAs<EnumDecl>()) {
    auto TSK = (TemplateSpecializationKind)Record.readInt();
    SourceLocation POI = readSourceLocation();
    ED->setInstantiationOfMemberEnum(Reader.getContext(), InstED, TSK);
    ED->getMemberSpecializationInfo()->setPointOfInstantiation(POI);
  }
}

ASTDeclReader::RedeclarableResult
ASTDeclReader::VisitRecordDeclImpl(RecordDecl *RD) {
  RedeclarableResult Redecl = VisitTagDecl(RD);

  BitsUnpacker RecordDeclBits(Record.readInt());
  RD->setHasFlexibleArrayMember(RecordDeclBits.getNextBit());
  RD->setAnonymousStructOrUnion(RecordDeclBits.getNextBit());
  RD->setHasObjectMember(RecordDeclBits.getNextBit());
  RD->setHasVolatileMember(RecordDeclBits.getNextBit());
  RD->setNonTrivialToPrimitiveDefaultInitialize(RecordDeclBits.getNextBit());
  RD->setNonTrivialToPrimitiveCopy(RecordDeclBits.getNextBit());
  RD->setNonTrivialToPrimitiveDestroy(RecordDeclBits.getNextBit());
  RD->setHasNonTrivialToPrimitiveDefaultInitializeCUnion(
      RecordDeclBits.getNextBit());
  RD->setHasNonTrivialToPrimitiveDestructCUnion(RecordDeclBits.getNextBit());
  RD->setHasNonTrivialToPrimitiveCopyCUnion(RecordDeclBits.getNextBit());
  RD->setParamDestroyedInCallee(RecordDeclBits.getNextBit());
  RD->setArgPassingRestrictions(
      (RecordArgPassingKind)RecordDeclBits.getNextBits(/*Width=*/2));
  return Redecl;
}

void ASTDeclReader::VisitRecordDecl(RecordDecl *RD) {
  VisitRecordDeclImpl(RD);
  RD->setODRHash(Record.readInt());

  // Maintain the invariant of a redeclaration chain containing only
  // a single definition.
  if (RD->isCompleteDefinition()) {
    RecordDecl *Canon = static_cast<RecordDecl *>(RD->getCanonicalDecl());
    RecordDecl *&OldDef = Reader.RecordDefinitions[Canon];
    if (!OldDef) {
      // This is the first time we've seen an imported definition. Look for a
      // local definition before deciding that we are the first definition.
      for (auto *D : merged_redecls(Canon)) {
        if (!D->isFromASTFile() && D->isCompleteDefinition()) {
          OldDef = D;
          break;
        }
      }
    }
    if (OldDef) {
      Reader.MergedDeclContexts.insert(std::make_pair(RD, OldDef));
      RD->demoteThisDefinitionToDeclaration();
      Reader.mergeDefinitionVisibility(OldDef, RD);
      if (OldDef->getODRHash() != RD->getODRHash())
        Reader.PendingRecordOdrMergeFailures[OldDef].push_back(RD);
    } else {
      OldDef = RD;
    }
  }
}

void ASTDeclReader::VisitValueDecl(ValueDecl *VD) {
  VisitNamedDecl(VD);
  // For function or variable declarations, defer reading the type in case the
  // declaration has a deduced type that references an entity declared within
  // the function definition or variable initializer.
  if (isa<FunctionDecl, VarDecl>(VD))
    DeferredTypeID = Record.getGlobalTypeID(Record.readInt());
  else
    VD->setType(Record.readType());
}

void ASTDeclReader::VisitEnumConstantDecl(EnumConstantDecl *ECD) {
  VisitValueDecl(ECD);
  if (Record.readInt())
    ECD->setInitExpr(Record.readExpr());
  ECD->setInitVal(Reader.getContext(), Record.readAPSInt());
  mergeMergeable(ECD);
}

void ASTDeclReader::VisitDeclaratorDecl(DeclaratorDecl *DD) {
  VisitValueDecl(DD);
  DD->setInnerLocStart(readSourceLocation());
  if (Record.readInt()) { // hasExtInfo
    auto *Info = new (Reader.getContext()) DeclaratorDecl::ExtInfo();
    Record.readQualifierInfo(*Info);
    Info->TrailingRequiresClause = Record.readExpr();
    DD->DeclInfo = Info;
  }
  QualType TSIType = Record.readType();
  DD->setTypeSourceInfo(
      TSIType.isNull() ? nullptr
                       : Reader.getContext().CreateTypeSourceInfo(TSIType));
}

void ASTDeclReader::VisitFunctionDecl(FunctionDecl *FD) {
  RedeclarableResult Redecl = VisitRedeclarable(FD);

  FunctionDecl *Existing = nullptr;

  switch ((FunctionDecl::TemplatedKind)Record.readInt()) {
  case FunctionDecl::TK_NonTemplate:
    break;
  case FunctionDecl::TK_DependentNonTemplate:
    FD->setInstantiatedFromDecl(readDeclAs<FunctionDecl>());
    break;
  case FunctionDecl::TK_FunctionTemplate: {
    auto *Template = readDeclAs<FunctionTemplateDecl>();
    Template->init(FD);
    FD->setDescribedFunctionTemplate(Template);
    break;
  }
  case FunctionDecl::TK_MemberSpecialization: {
    auto *InstFD = readDeclAs<FunctionDecl>();
    auto TSK = (TemplateSpecializationKind)Record.readInt();
    SourceLocation POI = readSourceLocation();
    FD->setInstantiationOfMemberFunction(Reader.getContext(), InstFD, TSK);
    FD->getMemberSpecializationInfo()->setPointOfInstantiation(POI);
    break;
  }
  case FunctionDecl::TK_FunctionTemplateSpecialization: {
    auto *Template = readDeclAs<FunctionTemplateDecl>();
    auto TSK = (TemplateSpecializationKind)Record.readInt();

    // Template arguments.
    SmallVector<TemplateArgument, 8> TemplArgs;
    Record.readTemplateArgumentList(TemplArgs, /*Canonicalize*/ true);

    // Template args as written.
    TemplateArgumentListInfo TemplArgsWritten;
    bool HasTemplateArgumentsAsWritten = Record.readBool();
    if (HasTemplateArgumentsAsWritten)
      Record.readTemplateArgumentListInfo(TemplArgsWritten);

    SourceLocation POI = readSourceLocation();

    ASTContext &C = Reader.getContext();
    TemplateArgumentList *TemplArgList =
        TemplateArgumentList::CreateCopy(C, TemplArgs);

    MemberSpecializationInfo *MSInfo = nullptr;
    if (Record.readInt()) {
      auto *FD = readDeclAs<FunctionDecl>();
      auto TSK = (TemplateSpecializationKind)Record.readInt();
      SourceLocation POI = readSourceLocation();

      MSInfo = new (C) MemberSpecializationInfo(FD, TSK);
      MSInfo->setPointOfInstantiation(POI);
    }

    FunctionTemplateSpecializationInfo *FTInfo =
        FunctionTemplateSpecializationInfo::Create(
            C, FD, Template, TSK, TemplArgList,
            HasTemplateArgumentsAsWritten ? &TemplArgsWritten : nullptr, POI,
            MSInfo);
    FD->TemplateOrSpecialization = FTInfo;

    if (FD->isCanonicalDecl()) { // if canonical add to template's set.
      // The template that contains the specializations set. It's not safe to
      // use getCanonicalDecl on Template since it may still be initializing.
      auto *CanonTemplate = readDeclAs<FunctionTemplateDecl>();
      // Get the InsertPos by FindNodeOrInsertPos() instead of calling
      // InsertNode(FTInfo) directly to avoid the getASTContext() call in
      // FunctionTemplateSpecializationInfo's Profile().
      // We avoid getASTContext because a decl in the parent hierarchy may
      // be initializing.
      llvm::FoldingSetNodeID ID;
      FunctionTemplateSpecializationInfo::Profile(ID, TemplArgs, C);
      void *InsertPos = nullptr;
      FunctionTemplateDecl::Common *CommonPtr = CanonTemplate->getCommonPtr();
      FunctionTemplateSpecializationInfo *ExistingInfo =
          CommonPtr->Specializations.FindNodeOrInsertPos(ID, InsertPos);
      if (InsertPos)
        CommonPtr->Specializations.InsertNode(FTInfo, InsertPos);
      else {
        assert(Reader.getContext().getLangOpts().Modules &&
               "already deserialized this template specialization");
        Existing = ExistingInfo->getFunction();
      }
    }
    break;
  }
  case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
    // Templates.
    UnresolvedSet<8> Candidates;
    unsigned NumCandidates = Record.readInt();
    while (NumCandidates--)
      Candidates.addDecl(readDeclAs<NamedDecl>());

    // Templates args.
    TemplateArgumentListInfo TemplArgsWritten;
    bool HasTemplateArgumentsAsWritten = Record.readBool();
    if (HasTemplateArgumentsAsWritten)
      Record.readTemplateArgumentListInfo(TemplArgsWritten);

    FD->setDependentTemplateSpecialization(
        Reader.getContext(), Candidates,
        HasTemplateArgumentsAsWritten ? &TemplArgsWritten : nullptr);
    // These are not merged; we don't need to merge redeclarations of dependent
    // template friends.
    break;
  }
  }

  VisitDeclaratorDecl(FD);

  // Attach a type to this function. Use the real type if possible, but fall
  // back to the type as written if it involves a deduced return type.
  if (FD->getTypeSourceInfo() && FD->getTypeSourceInfo()
                                     ->getType()
                                     ->castAs<FunctionType>()
                                     ->getReturnType()
                                     ->getContainedAutoType()) {
    // We'll set up the real type in Visit, once we've finished loading the
    // function.
    FD->setType(FD->getTypeSourceInfo()->getType());
    Reader.PendingDeducedFunctionTypes.push_back({FD, DeferredTypeID});
  } else {
    FD->setType(Reader.GetType(DeferredTypeID));
  }
  DeferredTypeID = 0;

  FD->DNLoc = Record.readDeclarationNameLoc(FD->getDeclName());
  FD->IdentifierNamespace = Record.readInt();

  // FunctionDecl's body is handled last at ASTDeclReader::Visit,
  // after everything else is read.
  BitsUnpacker FunctionDeclBits(Record.readInt());

  FD->setCachedLinkage((Linkage)FunctionDeclBits.getNextBits(/*Width=*/3));
  FD->setStorageClass((StorageClass)FunctionDeclBits.getNextBits(/*Width=*/3));
  FD->setInlineSpecified(FunctionDeclBits.getNextBit());
  FD->setImplicitlyInline(FunctionDeclBits.getNextBit());
  FD->setHasSkippedBody(FunctionDeclBits.getNextBit());
  FD->setVirtualAsWritten(FunctionDeclBits.getNextBit());
  // We defer calling `FunctionDecl::setPure()` here as for methods of
  // `CXXTemplateSpecializationDecl`s, we may not have connected up the
  // definition (which is required for `setPure`).
  const bool Pure = FunctionDeclBits.getNextBit();
  FD->setHasInheritedPrototype(FunctionDeclBits.getNextBit());
  FD->setHasWrittenPrototype(FunctionDeclBits.getNextBit());
  FD->setDeletedAsWritten(FunctionDeclBits.getNextBit());
  FD->setTrivial(FunctionDeclBits.getNextBit());
  FD->setTrivialForCall(FunctionDeclBits.getNextBit());
  FD->setDefaulted(FunctionDeclBits.getNextBit());
  FD->setExplicitlyDefaulted(FunctionDeclBits.getNextBit());
  FD->setIneligibleOrNotSelected(FunctionDeclBits.getNextBit());
  FD->setConstexprKind(
      (ConstexprSpecKind)FunctionDeclBits.getNextBits(/*Width=*/2));
  FD->setHasImplicitReturnZero(FunctionDeclBits.getNextBit());
  FD->setIsMultiVersion(FunctionDeclBits.getNextBit());
  FD->setLateTemplateParsed(FunctionDeclBits.getNextBit());
  FD->setFriendConstraintRefersToEnclosingTemplate(
      FunctionDeclBits.getNextBit());
  FD->setUsesSEHTry(FunctionDeclBits.getNextBit());

  FD->EndRangeLoc = readSourceLocation();
  if (FD->isExplicitlyDefaulted())
    FD->setDefaultLoc(readSourceLocation());

  FD->ODRHash = Record.readInt();
  FD->setHasODRHash(true);

  if (FD->isDefaulted() || FD->isDeletedAsWritten()) {
    // If 'Info' is nonzero, we need to read an DefaultedOrDeletedInfo; if,
    // additionally, the second bit is also set, we also need to read
    // a DeletedMessage for the DefaultedOrDeletedInfo.
    if (auto Info = Record.readInt()) {
      bool HasMessage = Info & 2;
      StringLiteral *DeletedMessage =
          HasMessage ? cast<StringLiteral>(Record.readExpr()) : nullptr;

      unsigned NumLookups = Record.readInt();
      SmallVector<DeclAccessPair, 8> Lookups;
      for (unsigned I = 0; I != NumLookups; ++I) {
        NamedDecl *ND = Record.readDeclAs<NamedDecl>();
        AccessSpecifier AS = (AccessSpecifier)Record.readInt();
        Lookups.push_back(DeclAccessPair::make(ND, AS));
      }

      FD->setDefaultedOrDeletedInfo(
          FunctionDecl::DefaultedOrDeletedFunctionInfo::Create(
              Reader.getContext(), Lookups, DeletedMessage));
    }
  }

  if (Existing)
    mergeRedeclarable(FD, Existing, Redecl);
  else if (auto Kind = FD->getTemplatedKind();
           Kind == FunctionDecl::TK_FunctionTemplate ||
           Kind == FunctionDecl::TK_FunctionTemplateSpecialization) {
    // Function Templates have their FunctionTemplateDecls merged instead of
    // their FunctionDecls.
    auto merge = [this, &Redecl, FD](auto &&F) {
      auto *Existing = cast_or_null<FunctionDecl>(Redecl.getKnownMergeTarget());
      RedeclarableResult NewRedecl(Existing ? F(Existing) : nullptr,
                                   Redecl.getFirstID(), Redecl.isKeyDecl());
      mergeRedeclarableTemplate(F(FD), NewRedecl);
    };
    if (Kind == FunctionDecl::TK_FunctionTemplate)
      merge(
          [](FunctionDecl *FD) { return FD->getDescribedFunctionTemplate(); });
    else
      merge([](FunctionDecl *FD) {
        return FD->getTemplateSpecializationInfo()->getTemplate();
      });
  } else
    mergeRedeclarable(FD, Redecl);

  // Defer calling `setPure` until merging above has guaranteed we've set
  // `DefinitionData` (as this will need to access it).
  FD->setIsPureVirtual(Pure);

  // Read in the parameters.
  unsigned NumParams = Record.readInt();
  SmallVector<ParmVarDecl *, 16> Params;
  Params.reserve(NumParams);
  for (unsigned I = 0; I != NumParams; ++I)
    Params.push_back(readDeclAs<ParmVarDecl>());
  FD->setParams(Reader.getContext(), Params);
}

void ASTDeclReader::VisitObjCMethodDecl(ObjCMethodDecl *MD) {
  VisitNamedDecl(MD);
  if (Record.readInt()) {
    // Load the body on-demand. Most clients won't care, because method
    // definitions rarely show up in headers.
    Reader.PendingBodies[MD] = GetCurrentCursorOffset();
  }
  MD->setSelfDecl(readDeclAs<ImplicitParamDecl>());
  MD->setCmdDecl(readDeclAs<ImplicitParamDecl>());
  MD->setInstanceMethod(Record.readInt());
  MD->setVariadic(Record.readInt());
  MD->setPropertyAccessor(Record.readInt());
  MD->setSynthesizedAccessorStub(Record.readInt());
  MD->setDefined(Record.readInt());
  MD->setOverriding(Record.readInt());
  MD->setHasSkippedBody(Record.readInt());

  MD->setIsRedeclaration(Record.readInt());
  MD->setHasRedeclaration(Record.readInt());
  if (MD->hasRedeclaration())
    Reader.getContext().setObjCMethodRedeclaration(MD,
                                       readDeclAs<ObjCMethodDecl>());

  MD->setDeclImplementation(
      static_cast<ObjCImplementationControl>(Record.readInt()));
  MD->setObjCDeclQualifier((Decl::ObjCDeclQualifier)Record.readInt());
  MD->setRelatedResultType(Record.readInt());
  MD->setReturnType(Record.readType());
  MD->setReturnTypeSourceInfo(readTypeSourceInfo());
  MD->DeclEndLoc = readSourceLocation();
  unsigned NumParams = Record.readInt();
  SmallVector<ParmVarDecl *, 16> Params;
  Params.reserve(NumParams);
  for (unsigned I = 0; I != NumParams; ++I)
    Params.push_back(readDeclAs<ParmVarDecl>());

  MD->setSelLocsKind((SelectorLocationsKind)Record.readInt());
  unsigned NumStoredSelLocs = Record.readInt();
  SmallVector<SourceLocation, 16> SelLocs;
  SelLocs.reserve(NumStoredSelLocs);
  for (unsigned i = 0; i != NumStoredSelLocs; ++i)
    SelLocs.push_back(readSourceLocation());

  MD->setParamsAndSelLocs(Reader.getContext(), Params, SelLocs);
}

void ASTDeclReader::VisitObjCTypeParamDecl(ObjCTypeParamDecl *D) {
  VisitTypedefNameDecl(D);

  D->Variance = Record.readInt();
  D->Index = Record.readInt();
  D->VarianceLoc = readSourceLocation();
  D->ColonLoc = readSourceLocation();
}

void ASTDeclReader::VisitObjCContainerDecl(ObjCContainerDecl *CD) {
  VisitNamedDecl(CD);
  CD->setAtStartLoc(readSourceLocation());
  CD->setAtEndRange(readSourceRange());
}

ObjCTypeParamList *ASTDeclReader::ReadObjCTypeParamList() {
  unsigned numParams = Record.readInt();
  if (numParams == 0)
    return nullptr;

  SmallVector<ObjCTypeParamDecl *, 4> typeParams;
  typeParams.reserve(numParams);
  for (unsigned i = 0; i != numParams; ++i) {
    auto *typeParam = readDeclAs<ObjCTypeParamDecl>();
    if (!typeParam)
      return nullptr;

    typeParams.push_back(typeParam);
  }

  SourceLocation lAngleLoc = readSourceLocation();
  SourceLocation rAngleLoc = readSourceLocation();

  return ObjCTypeParamList::create(Reader.getContext(), lAngleLoc,
                                   typeParams, rAngleLoc);
}

void ASTDeclReader::ReadObjCDefinitionData(
         struct ObjCInterfaceDecl::DefinitionData &Data) {
  // Read the superclass.
  Data.SuperClassTInfo = readTypeSourceInfo();

  Data.EndLoc = readSourceLocation();
  Data.HasDesignatedInitializers = Record.readInt();
  Data.ODRHash = Record.readInt();
  Data.HasODRHash = true;

  // Read the directly referenced protocols and their SourceLocations.
  unsigned NumProtocols = Record.readInt();
  SmallVector<ObjCProtocolDecl *, 16> Protocols;
  Protocols.reserve(NumProtocols);
  for (unsigned I = 0; I != NumProtocols; ++I)
    Protocols.push_back(readDeclAs<ObjCProtocolDecl>());
  SmallVector<SourceLocation, 16> ProtoLocs;
  ProtoLocs.reserve(NumProtocols);
  for (unsigned I = 0; I != NumProtocols; ++I)
    ProtoLocs.push_back(readSourceLocation());
  Data.ReferencedProtocols.set(Protocols.data(), NumProtocols, ProtoLocs.data(),
                               Reader.getContext());

  // Read the transitive closure of protocols referenced by this class.
  NumProtocols = Record.readInt();
  Protocols.clear();
  Protocols.reserve(NumProtocols);
  for (unsigned I = 0; I != NumProtocols; ++I)
    Protocols.push_back(readDeclAs<ObjCProtocolDecl>());
  Data.AllReferencedProtocols.set(Protocols.data(), NumProtocols,
                                  Reader.getContext());
}

void ASTDeclReader::MergeDefinitionData(ObjCInterfaceDecl *D,
         struct ObjCInterfaceDecl::DefinitionData &&NewDD) {
  struct ObjCInterfaceDecl::DefinitionData &DD = D->data();
  if (DD.Definition == NewDD.Definition)
    return;

  Reader.MergedDeclContexts.insert(
      std::make_pair(NewDD.Definition, DD.Definition));
  Reader.mergeDefinitionVisibility(DD.Definition, NewDD.Definition);

  if (D->getODRHash() != NewDD.ODRHash)
    Reader.PendingObjCInterfaceOdrMergeFailures[DD.Definition].push_back(
        {NewDD.Definition, &NewDD});
}

void ASTDeclReader::VisitObjCInterfaceDecl(ObjCInterfaceDecl *ID) {
  RedeclarableResult Redecl = VisitRedeclarable(ID);
  VisitObjCContainerDecl(ID);
  DeferredTypeID = Record.getGlobalTypeID(Record.readInt());
  mergeRedeclarable(ID, Redecl);

  ID->TypeParamList = ReadObjCTypeParamList();
  if (Record.readInt()) {
    // Read the definition.
    ID->allocateDefinitionData();

    ReadObjCDefinitionData(ID->data());
    ObjCInterfaceDecl *Canon = ID->getCanonicalDecl();
    if (Canon->Data.getPointer()) {
      // If we already have a definition, keep the definition invariant and
      // merge the data.
      MergeDefinitionData(Canon, std::move(ID->data()));
      ID->Data = Canon->Data;
    } else {
      // Set the definition data of the canonical declaration, so other
      // redeclarations will see it.
      ID->getCanonicalDecl()->Data = ID->Data;

      // We will rebuild this list lazily.
      ID->setIvarList(nullptr);
    }

    // Note that we have deserialized a definition.
    Reader.PendingDefinitions.insert(ID);

    // Note that we've loaded this Objective-C class.
    Reader.ObjCClassesLoaded.push_back(ID);
  } else {
    ID->Data = ID->getCanonicalDecl()->Data;
  }
}

void ASTDeclReader::VisitObjCIvarDecl(ObjCIvarDecl *IVD) {
  VisitFieldDecl(IVD);
  IVD->setAccessControl((ObjCIvarDecl::AccessControl)Record.readInt());
  // This field will be built lazily.
  IVD->setNextIvar(nullptr);
  bool synth = Record.readInt();
  IVD->setSynthesize(synth);

  // Check ivar redeclaration.
  if (IVD->isInvalidDecl())
    return;
  // Don't check ObjCInterfaceDecl as interfaces are named and mismatches can be
  // detected in VisitObjCInterfaceDecl. Here we are looking for redeclarations
  // in extensions.
  if (isa<ObjCInterfaceDecl>(IVD->getDeclContext()))
    return;
  ObjCInterfaceDecl *CanonIntf =
      IVD->getContainingInterface()->getCanonicalDecl();
  IdentifierInfo *II = IVD->getIdentifier();
  ObjCIvarDecl *PrevIvar = CanonIntf->lookupInstanceVariable(II);
  if (PrevIvar && PrevIvar != IVD) {
    auto *ParentExt = dyn_cast<ObjCCategoryDecl>(IVD->getDeclContext());
    auto *PrevParentExt =
        dyn_cast<ObjCCategoryDecl>(PrevIvar->getDeclContext());
    if (ParentExt && PrevParentExt) {
      // Postpone diagnostic as we should merge identical extensions from
      // different modules.
      Reader
          .PendingObjCExtensionIvarRedeclarations[std::make_pair(ParentExt,
                                                                 PrevParentExt)]
          .push_back(std::make_pair(IVD, PrevIvar));
    } else if (ParentExt || PrevParentExt) {
      // Duplicate ivars in extension + implementation are never compatible.
      // Compatibility of implementation + implementation should be handled in
      // VisitObjCImplementationDecl.
      Reader.Diag(IVD->getLocation(), diag::err_duplicate_ivar_declaration)
          << II;
      Reader.Diag(PrevIvar->getLocation(), diag::note_previous_definition);
    }
  }
}

void ASTDeclReader::ReadObjCDefinitionData(
         struct ObjCProtocolDecl::DefinitionData &Data) {
    unsigned NumProtoRefs = Record.readInt();
    SmallVector<ObjCProtocolDecl *, 16> ProtoRefs;
    ProtoRefs.reserve(NumProtoRefs);
    for (unsigned I = 0; I != NumProtoRefs; ++I)
      ProtoRefs.push_back(readDeclAs<ObjCProtocolDecl>());
    SmallVector<SourceLocation, 16> ProtoLocs;
    ProtoLocs.reserve(NumProtoRefs);
    for (unsigned I = 0; I != NumProtoRefs; ++I)
      ProtoLocs.push_back(readSourceLocation());
    Data.ReferencedProtocols.set(ProtoRefs.data(), NumProtoRefs,
                                 ProtoLocs.data(), Reader.getContext());
    Data.ODRHash = Record.readInt();
    Data.HasODRHash = true;
}

void ASTDeclReader::MergeDefinitionData(
    ObjCProtocolDecl *D, struct ObjCProtocolDecl::DefinitionData &&NewDD) {
  struct ObjCProtocolDecl::DefinitionData &DD = D->data();
  if (DD.Definition == NewDD.Definition)
    return;

  Reader.MergedDeclContexts.insert(
      std::make_pair(NewDD.Definition, DD.Definition));
  Reader.mergeDefinitionVisibility(DD.Definition, NewDD.Definition);

  if (D->getODRHash() != NewDD.ODRHash)
    Reader.PendingObjCProtocolOdrMergeFailures[DD.Definition].push_back(
        {NewDD.Definition, &NewDD});
}

void ASTDeclReader::VisitObjCProtocolDecl(ObjCProtocolDecl *PD) {
  RedeclarableResult Redecl = VisitRedeclarable(PD);
  VisitObjCContainerDecl(PD);
  mergeRedeclarable(PD, Redecl);

  if (Record.readInt()) {
    // Read the definition.
    PD->allocateDefinitionData();

    ReadObjCDefinitionData(PD->data());

    ObjCProtocolDecl *Canon = PD->getCanonicalDecl();
    if (Canon->Data.getPointer()) {
      // If we already have a definition, keep the definition invariant and
      // merge the data.
      MergeDefinitionData(Canon, std::move(PD->data()));
      PD->Data = Canon->Data;
    } else {
      // Set the definition data of the canonical declaration, so other
      // redeclarations will see it.
      PD->getCanonicalDecl()->Data = PD->Data;
    }
    // Note that we have deserialized a definition.
    Reader.PendingDefinitions.insert(PD);
  } else {
    PD->Data = PD->getCanonicalDecl()->Data;
  }
}

void ASTDeclReader::VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *FD) {
  VisitFieldDecl(FD);
}

void ASTDeclReader::VisitObjCCategoryDecl(ObjCCategoryDecl *CD) {
  VisitObjCContainerDecl(CD);
  CD->setCategoryNameLoc(readSourceLocation());
  CD->setIvarLBraceLoc(readSourceLocation());
  CD->setIvarRBraceLoc(readSourceLocation());

  // Note that this category has been deserialized. We do this before
  // deserializing the interface declaration, so that it will consider this
  /// category.
  Reader.CategoriesDeserialized.insert(CD);

  CD->ClassInterface = readDeclAs<ObjCInterfaceDecl>();
  CD->TypeParamList = ReadObjCTypeParamList();
  unsigned NumProtoRefs = Record.readInt();
  SmallVector<ObjCProtocolDecl *, 16> ProtoRefs;
  ProtoRefs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoRefs.push_back(readDeclAs<ObjCProtocolDecl>());
  SmallVector<SourceLocation, 16> ProtoLocs;
  ProtoLocs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoLocs.push_back(readSourceLocation());
  CD->setProtocolList(ProtoRefs.data(), NumProtoRefs, ProtoLocs.data(),
                      Reader.getContext());

  // Protocols in the class extension belong to the class.
  if (NumProtoRefs > 0 && CD->ClassInterface && CD->IsClassExtension())
    CD->ClassInterface->mergeClassExtensionProtocolList(
        (ObjCProtocolDecl *const *)ProtoRefs.data(), NumProtoRefs,
        Reader.getContext());
}

void ASTDeclReader::VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *CAD) {
  VisitNamedDecl(CAD);
  CAD->setClassInterface(readDeclAs<ObjCInterfaceDecl>());
}

void ASTDeclReader::VisitObjCPropertyDecl(ObjCPropertyDecl *D) {
  VisitNamedDecl(D);
  D->setAtLoc(readSourceLocation());
  D->setLParenLoc(readSourceLocation());
  QualType T = Record.readType();
  TypeSourceInfo *TSI = readTypeSourceInfo();
  D->setType(T, TSI);
  D->setPropertyAttributes((ObjCPropertyAttribute::Kind)Record.readInt());
  D->setPropertyAttributesAsWritten(
      (ObjCPropertyAttribute::Kind)Record.readInt());
  D->setPropertyImplementation(
      (ObjCPropertyDecl::PropertyControl)Record.readInt());
  DeclarationName GetterName = Record.readDeclarationName();
  SourceLocation GetterLoc = readSourceLocation();
  D->setGetterName(GetterName.getObjCSelector(), GetterLoc);
  DeclarationName SetterName = Record.readDeclarationName();
  SourceLocation SetterLoc = readSourceLocation();
  D->setSetterName(SetterName.getObjCSelector(), SetterLoc);
  D->setGetterMethodDecl(readDeclAs<ObjCMethodDecl>());
  D->setSetterMethodDecl(readDeclAs<ObjCMethodDecl>());
  D->setPropertyIvarDecl(readDeclAs<ObjCIvarDecl>());
}

void ASTDeclReader::VisitObjCImplDecl(ObjCImplDecl *D) {
  VisitObjCContainerDecl(D);
  D->setClassInterface(readDeclAs<ObjCInterfaceDecl>());
}

void ASTDeclReader::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D) {
  VisitObjCImplDecl(D);
  D->CategoryNameLoc = readSourceLocation();
}

void ASTDeclReader::VisitObjCImplementationDecl(ObjCImplementationDecl *D) {
  VisitObjCImplDecl(D);
  D->setSuperClass(readDeclAs<ObjCInterfaceDecl>());
  D->SuperLoc = readSourceLocation();
  D->setIvarLBraceLoc(readSourceLocation());
  D->setIvarRBraceLoc(readSourceLocation());
  D->setHasNonZeroConstructors(Record.readInt());
  D->setHasDestructors(Record.readInt());
  D->NumIvarInitializers = Record.readInt();
  if (D->NumIvarInitializers)
    D->IvarInitializers = ReadGlobalOffset();
}

void ASTDeclReader::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
  VisitDecl(D);
  D->setAtLoc(readSourceLocation());
  D->setPropertyDecl(readDeclAs<ObjCPropertyDecl>());
  D->PropertyIvarDecl = readDeclAs<ObjCIvarDecl>();
  D->IvarLoc = readSourceLocation();
  D->setGetterMethodDecl(readDeclAs<ObjCMethodDecl>());
  D->setSetterMethodDecl(readDeclAs<ObjCMethodDecl>());
  D->setGetterCXXConstructor(Record.readExpr());
  D->setSetterCXXAssignment(Record.readExpr());
}

void ASTDeclReader::VisitFieldDecl(FieldDecl *FD) {
  VisitDeclaratorDecl(FD);
  FD->Mutable = Record.readInt();

  unsigned Bits = Record.readInt();
  FD->StorageKind = Bits >> 1;
  if (FD->StorageKind == FieldDecl::ISK_CapturedVLAType)
    FD->CapturedVLAType =
        cast<VariableArrayType>(Record.readType().getTypePtr());
  else if (Bits & 1)
    FD->setBitWidth(Record.readExpr());

  if (!FD->getDeclName()) {
    if (auto *Tmpl = readDeclAs<FieldDecl>())
      Reader.getContext().setInstantiatedFromUnnamedFieldDecl(FD, Tmpl);
  }
  mergeMergeable(FD);
}

void ASTDeclReader::VisitMSPropertyDecl(MSPropertyDecl *PD) {
  VisitDeclaratorDecl(PD);
  PD->GetterId = Record.readIdentifier();
  PD->SetterId = Record.readIdentifier();
}

void ASTDeclReader::VisitMSGuidDecl(MSGuidDecl *D) {
  VisitValueDecl(D);
  D->PartVal.Part1 = Record.readInt();
  D->PartVal.Part2 = Record.readInt();
  D->PartVal.Part3 = Record.readInt();
  for (auto &C : D->PartVal.Part4And5)
    C = Record.readInt();

  // Add this GUID to the AST context's lookup structure, and merge if needed.
  if (MSGuidDecl *Existing = Reader.getContext().MSGuidDecls.GetOrInsertNode(D))
    Reader.getContext().setPrimaryMergedDecl(D, Existing->getCanonicalDecl());
}

void ASTDeclReader::VisitUnnamedGlobalConstantDecl(
    UnnamedGlobalConstantDecl *D) {
  VisitValueDecl(D);
  D->Value = Record.readAPValue();

  // Add this to the AST context's lookup structure, and merge if needed.
  if (UnnamedGlobalConstantDecl *Existing =
          Reader.getContext().UnnamedGlobalConstantDecls.GetOrInsertNode(D))
    Reader.getContext().setPrimaryMergedDecl(D, Existing->getCanonicalDecl());
}

void ASTDeclReader::VisitTemplateParamObjectDecl(TemplateParamObjectDecl *D) {
  VisitValueDecl(D);
  D->Value = Record.readAPValue();

  // Add this template parameter object to the AST context's lookup structure,
  // and merge if needed.
  if (TemplateParamObjectDecl *Existing =
          Reader.getContext().TemplateParamObjectDecls.GetOrInsertNode(D))
    Reader.getContext().setPrimaryMergedDecl(D, Existing->getCanonicalDecl());
}

void ASTDeclReader::VisitIndirectFieldDecl(IndirectFieldDecl *FD) {
  VisitValueDecl(FD);

  FD->ChainingSize = Record.readInt();
  assert(FD->ChainingSize >= 2 && "Anonymous chaining must be >= 2");
  FD->Chaining = new (Reader.getContext())NamedDecl*[FD->ChainingSize];

  for (unsigned I = 0; I != FD->ChainingSize; ++I)
    FD->Chaining[I] = readDeclAs<NamedDecl>();

  mergeMergeable(FD);
}

ASTDeclReader::RedeclarableResult ASTDeclReader::VisitVarDeclImpl(VarDecl *VD) {
  RedeclarableResult Redecl = VisitRedeclarable(VD);
  VisitDeclaratorDecl(VD);

  BitsUnpacker VarDeclBits(Record.readInt());
  auto VarLinkage = Linkage(VarDeclBits.getNextBits(/*Width=*/3));
  bool DefGeneratedInModule = VarDeclBits.getNextBit();
  VD->VarDeclBits.SClass = (StorageClass)VarDeclBits.getNextBits(/*Width=*/3);
  VD->VarDeclBits.TSCSpec = VarDeclBits.getNextBits(/*Width=*/2);
  VD->VarDeclBits.InitStyle = VarDeclBits.getNextBits(/*Width=*/2);
  VD->VarDeclBits.ARCPseudoStrong = VarDeclBits.getNextBit();
  bool HasDeducedType = false;
  if (!isa<ParmVarDecl>(VD)) {
    VD->NonParmVarDeclBits.IsThisDeclarationADemotedDefinition =
        VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.ExceptionVar = VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.NRVOVariable = VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.CXXForRangeDecl = VarDeclBits.getNextBit();

    VD->NonParmVarDeclBits.IsInline = VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.IsInlineSpecified = VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.IsConstexpr = VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.IsInitCapture = VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.PreviousDeclInSameBlockScope =
        VarDeclBits.getNextBit();

    VD->NonParmVarDeclBits.EscapingByref = VarDeclBits.getNextBit();
    HasDeducedType = VarDeclBits.getNextBit();
    VD->NonParmVarDeclBits.ImplicitParamKind =
        VarDeclBits.getNextBits(/*Width*/ 3);

    VD->NonParmVarDeclBits.ObjCForDecl = VarDeclBits.getNextBit();
  }

  // If this variable has a deduced type, defer reading that type until we are
  // done deserializing this variable, because the type might refer back to the
  // variable.
  if (HasDeducedType)
    Reader.PendingDeducedVarTypes.push_back({VD, DeferredTypeID});
  else
    VD->setType(Reader.GetType(DeferredTypeID));
  DeferredTypeID = 0;

  VD->setCachedLinkage(VarLinkage);

  // Reconstruct the one piece of the IdentifierNamespace that we need.
  if (VD->getStorageClass() == SC_Extern && VarLinkage != Linkage::None &&
      VD->getLexicalDeclContext()->isFunctionOrMethod())
    VD->setLocalExternDecl();

  if (DefGeneratedInModule) {
    Reader.DefinitionSource[VD] =
        Loc.F->Kind == ModuleKind::MK_MainFile ||
        Reader.getContext().getLangOpts().BuildingPCHWithObjectFile;
  }

  if (VD->hasAttr<BlocksAttr>()) {
    Expr *CopyExpr = Record.readExpr();
    if (CopyExpr)
      Reader.getContext().setBlockVarCopyInit(VD, CopyExpr, Record.readInt());
  }

  enum VarKind {
    VarNotTemplate = 0, VarTemplate, StaticDataMemberSpecialization
  };
  switch ((VarKind)Record.readInt()) {
  case VarNotTemplate:
    // Only true variables (not parameters or implicit parameters) can be
    // merged; the other kinds are not really redeclarable at all.
    if (!isa<ParmVarDecl>(VD) && !isa<ImplicitParamDecl>(VD) &&
        !isa<VarTemplateSpecializationDecl>(VD))
      mergeRedeclarable(VD, Redecl);
    break;
  case VarTemplate:
    // Merged when we merge the template.
    VD->setDescribedVarTemplate(readDeclAs<VarTemplateDecl>());
    break;
  case StaticDataMemberSpecialization: { // HasMemberSpecializationInfo.
    auto *Tmpl = readDeclAs<VarDecl>();
    auto TSK = (TemplateSpecializationKind)Record.readInt();
    SourceLocation POI = readSourceLocation();
    Reader.getContext().setInstantiatedFromStaticDataMember(VD, Tmpl, TSK,POI);
    mergeRedeclarable(VD, Redecl);
    break;
  }
  }

  return Redecl;
}

void ASTDeclReader::ReadVarDeclInit(VarDecl *VD) {
  if (uint64_t Val = Record.readInt()) {
    EvaluatedStmt *Eval = VD->ensureEvaluatedStmt();
    Eval->HasConstantInitialization = (Val & 2) != 0;
    Eval->HasConstantDestruction = (Val & 4) != 0;
    Eval->WasEvaluated = (Val & 8) != 0;
    if (Eval->WasEvaluated) {
      Eval->Evaluated = Record.readAPValue();
      if (Eval->Evaluated.needsCleanup())
        Reader.getContext().addDestruction(&Eval->Evaluated);
    }

    // Store the offset of the initializer. Don't deserialize it yet: it might
    // not be needed, and might refer back to the variable, for example if it
    // contains a lambda.
    Eval->Value = GetCurrentCursorOffset();
  }
}

void ASTDeclReader::VisitImplicitParamDecl(ImplicitParamDecl *PD) {
  VisitVarDecl(PD);
}

void ASTDeclReader::VisitParmVarDecl(ParmVarDecl *PD) {
  VisitVarDecl(PD);

  unsigned scopeIndex = Record.readInt();
  BitsUnpacker ParmVarDeclBits(Record.readInt());
  unsigned isObjCMethodParam = ParmVarDeclBits.getNextBit();
  unsigned scopeDepth = ParmVarDeclBits.getNextBits(/*Width=*/7);
  unsigned declQualifier = ParmVarDeclBits.getNextBits(/*Width=*/7);
  if (isObjCMethodParam) {
    assert(scopeDepth == 0);
    PD->setObjCMethodScopeInfo(scopeIndex);
    PD->ParmVarDeclBits.ScopeDepthOrObjCQuals = declQualifier;
  } else {
    PD->setScopeInfo(scopeDepth, scopeIndex);
  }
  PD->ParmVarDeclBits.IsKNRPromoted = ParmVarDeclBits.getNextBit();

  PD->ParmVarDeclBits.HasInheritedDefaultArg = ParmVarDeclBits.getNextBit();
  if (ParmVarDeclBits.getNextBit()) // hasUninstantiatedDefaultArg.
    PD->setUninstantiatedDefaultArg(Record.readExpr());

  if (ParmVarDeclBits.getNextBit()) // Valid explicit object parameter
    PD->ExplicitObjectParameterIntroducerLoc = Record.readSourceLocation();

  // FIXME: If this is a redeclaration of a function from another module, handle
  // inheritance of default arguments.
}

void ASTDeclReader::VisitDecompositionDecl(DecompositionDecl *DD) {
  VisitVarDecl(DD);
  auto **BDs = DD->getTrailingObjects<BindingDecl *>();
  for (unsigned I = 0; I != DD->NumBindings; ++I) {
    BDs[I] = readDeclAs<BindingDecl>();
    BDs[I]->setDecomposedDecl(DD);
  }
}

void ASTDeclReader::VisitBindingDecl(BindingDecl *BD) {
  VisitValueDecl(BD);
  BD->Binding = Record.readExpr();
}

void ASTDeclReader::VisitFileScopeAsmDecl(FileScopeAsmDecl *AD) {
  VisitDecl(AD);
  AD->setAsmString(cast<StringLiteral>(Record.readExpr()));
  AD->setRParenLoc(readSourceLocation());
}

void ASTDeclReader::VisitTopLevelStmtDecl(TopLevelStmtDecl *D) {
  VisitDecl(D);
  D->Statement = Record.readStmt();
}

void ASTDeclReader::VisitBlockDecl(BlockDecl *BD) {
  VisitDecl(BD);
  BD->setBody(cast_or_null<CompoundStmt>(Record.readStmt()));
  BD->setSignatureAsWritten(readTypeSourceInfo());
  unsigned NumParams = Record.readInt();
  SmallVector<ParmVarDecl *, 16> Params;
  Params.reserve(NumParams);
  for (unsigned I = 0; I != NumParams; ++I)
    Params.push_back(readDeclAs<ParmVarDecl>());
  BD->setParams(Params);

  BD->setIsVariadic(Record.readInt());
  BD->setBlockMissingReturnType(Record.readInt());
  BD->setIsConversionFromLambda(Record.readInt());
  BD->setDoesNotEscape(Record.readInt());
  BD->setCanAvoidCopyToHeap(Record.readInt());

  bool capturesCXXThis = Record.readInt();
  unsigned numCaptures = Record.readInt();
  SmallVector<BlockDecl::Capture, 16> captures;
  captures.reserve(numCaptures);
  for (unsigned i = 0; i != numCaptures; ++i) {
    auto *decl = readDeclAs<VarDecl>();
    unsigned flags = Record.readInt();
    bool byRef = (flags & 1);
    bool nested = (flags & 2);
    Expr *copyExpr = ((flags & 4) ? Record.readExpr() : nullptr);

    captures.push_back(BlockDecl::Capture(decl, byRef, nested, copyExpr));
  }
  BD->setCaptures(Reader.getContext(), captures, capturesCXXThis);
}

void ASTDeclReader::VisitCapturedDecl(CapturedDecl *CD) {
  VisitDecl(CD);
  unsigned ContextParamPos = Record.readInt();
  CD->setNothrow(Record.readInt() != 0);
  // Body is set by VisitCapturedStmt.
  for (unsigned I = 0; I < CD->NumParams; ++I) {
    if (I != ContextParamPos)
      CD->setParam(I, readDeclAs<ImplicitParamDecl>());
    else
      CD->setContextParam(I, readDeclAs<ImplicitParamDecl>());
  }
}

void ASTDeclReader::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  VisitDecl(D);
  D->setLanguage(static_cast<LinkageSpecLanguageIDs>(Record.readInt()));
  D->setExternLoc(readSourceLocation());
  D->setRBraceLoc(readSourceLocation());
}

void ASTDeclReader::VisitExportDecl(ExportDecl *D) {
  VisitDecl(D);
  D->RBraceLoc = readSourceLocation();
}

void ASTDeclReader::VisitLabelDecl(LabelDecl *D) {
  VisitNamedDecl(D);
  D->setLocStart(readSourceLocation());
}

void ASTDeclReader::VisitNamespaceDecl(NamespaceDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarable(D);
  VisitNamedDecl(D);

  BitsUnpacker NamespaceDeclBits(Record.readInt());
  D->setInline(NamespaceDeclBits.getNextBit());
  D->setNested(NamespaceDeclBits.getNextBit());
  D->LocStart = readSourceLocation();
  D->RBraceLoc = readSourceLocation();

  // Defer loading the anonymous namespace until we've finished merging
  // this namespace; loading it might load a later declaration of the
  // same namespace, and we have an invariant that older declarations
  // get merged before newer ones try to merge.
  GlobalDeclID AnonNamespace;
  if (Redecl.getFirstID() == ThisDeclID)
    AnonNamespace = readDeclID();

  mergeRedeclarable(D, Redecl);

  if (AnonNamespace.isValid()) {
    // Each module has its own anonymous namespace, which is disjoint from
    // any other module's anonymous namespaces, so don't attach the anonymous
    // namespace at all.
    auto *Anon = cast<NamespaceDecl>(Reader.GetDecl(AnonNamespace));
    if (!Record.isModule())
      D->setAnonymousNamespace(Anon);
  }
}

void ASTDeclReader::VisitHLSLBufferDecl(HLSLBufferDecl *D) {
  VisitNamedDecl(D);
  VisitDeclContext(D);
  D->IsCBuffer = Record.readBool();
  D->KwLoc = readSourceLocation();
  D->LBraceLoc = readSourceLocation();
  D->RBraceLoc = readSourceLocation();
}

void ASTDeclReader::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarable(D);
  VisitNamedDecl(D);
  D->NamespaceLoc = readSourceLocation();
  D->IdentLoc = readSourceLocation();
  D->QualifierLoc = Record.readNestedNameSpecifierLoc();
  D->Namespace = readDeclAs<NamedDecl>();
  mergeRedeclarable(D, Redecl);
}

void ASTDeclReader::VisitUsingDecl(UsingDecl *D) {
  VisitNamedDecl(D);
  D->setUsingLoc(readSourceLocation());
  D->QualifierLoc = Record.readNestedNameSpecifierLoc();
  D->DNLoc = Record.readDeclarationNameLoc(D->getDeclName());
  D->FirstUsingShadow.setPointer(readDeclAs<UsingShadowDecl>());
  D->setTypename(Record.readInt());
  if (auto *Pattern = readDeclAs<NamedDecl>())
    Reader.getContext().setInstantiatedFromUsingDecl(D, Pattern);
  mergeMergeable(D);
}

void ASTDeclReader::VisitUsingEnumDecl(UsingEnumDecl *D) {
  VisitNamedDecl(D);
  D->setUsingLoc(readSourceLocation());
  D->setEnumLoc(readSourceLocation());
  D->setEnumType(Record.readTypeSourceInfo());
  D->FirstUsingShadow.setPointer(readDeclAs<UsingShadowDecl>());
  if (auto *Pattern = readDeclAs<UsingEnumDecl>())
    Reader.getContext().setInstantiatedFromUsingEnumDecl(D, Pattern);
  mergeMergeable(D);
}

void ASTDeclReader::VisitUsingPackDecl(UsingPackDecl *D) {
  VisitNamedDecl(D);
  D->InstantiatedFrom = readDeclAs<NamedDecl>();
  auto **Expansions = D->getTrailingObjects<NamedDecl *>();
  for (unsigned I = 0; I != D->NumExpansions; ++I)
    Expansions[I] = readDeclAs<NamedDecl>();
  mergeMergeable(D);
}

void ASTDeclReader::VisitUsingShadowDecl(UsingShadowDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarable(D);
  VisitNamedDecl(D);
  D->Underlying = readDeclAs<NamedDecl>();
  D->IdentifierNamespace = Record.readInt();
  D->UsingOrNextShadow = readDeclAs<NamedDecl>();
  auto *Pattern = readDeclAs<UsingShadowDecl>();
  if (Pattern)
    Reader.getContext().setInstantiatedFromUsingShadowDecl(D, Pattern);
  mergeRedeclarable(D, Redecl);
}

void ASTDeclReader::VisitConstructorUsingShadowDecl(
    ConstructorUsingShadowDecl *D) {
  VisitUsingShadowDecl(D);
  D->NominatedBaseClassShadowDecl = readDeclAs<ConstructorUsingShadowDecl>();
  D->ConstructedBaseClassShadowDecl = readDeclAs<ConstructorUsingShadowDecl>();
  D->IsVirtual = Record.readInt();
}

void ASTDeclReader::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  VisitNamedDecl(D);
  D->UsingLoc = readSourceLocation();
  D->NamespaceLoc = readSourceLocation();
  D->QualifierLoc = Record.readNestedNameSpecifierLoc();
  D->NominatedNamespace = readDeclAs<NamedDecl>();
  D->CommonAncestor = readDeclAs<DeclContext>();
}

void ASTDeclReader::VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
  VisitValueDecl(D);
  D->setUsingLoc(readSourceLocation());
  D->QualifierLoc = Record.readNestedNameSpecifierLoc();
  D->DNLoc = Record.readDeclarationNameLoc(D->getDeclName());
  D->EllipsisLoc = readSourceLocation();
  mergeMergeable(D);
}

void ASTDeclReader::VisitUnresolvedUsingTypenameDecl(
                                               UnresolvedUsingTypenameDecl *D) {
  VisitTypeDecl(D);
  D->TypenameLocation = readSourceLocation();
  D->QualifierLoc = Record.readNestedNameSpecifierLoc();
  D->EllipsisLoc = readSourceLocation();
  mergeMergeable(D);
}

void ASTDeclReader::VisitUnresolvedUsingIfExistsDecl(
    UnresolvedUsingIfExistsDecl *D) {
  VisitNamedDecl(D);
}

void ASTDeclReader::ReadCXXDefinitionData(
    struct CXXRecordDecl::DefinitionData &Data, const CXXRecordDecl *D,
    Decl *LambdaContext, unsigned IndexInLambdaContext) {

  BitsUnpacker CXXRecordDeclBits = Record.readInt();

#define FIELD(Name, Width, Merge)                                              \
  if (!CXXRecordDeclBits.canGetNextNBits(Width))                         \
    CXXRecordDeclBits.updateValue(Record.readInt());                           \
  Data.Name = CXXRecordDeclBits.getNextBits(Width);

#include "clang/AST/CXXRecordDeclDefinitionBits.def"
#undef FIELD

  // Note: the caller has deserialized the IsLambda bit already.
  Data.ODRHash = Record.readInt();
  Data.HasODRHash = true;

  if (Record.readInt()) {
    Reader.DefinitionSource[D] =
        Loc.F->Kind == ModuleKind::MK_MainFile ||
        Reader.getContext().getLangOpts().BuildingPCHWithObjectFile;
  }

  Record.readUnresolvedSet(Data.Conversions);
  Data.ComputedVisibleConversions = Record.readInt();
  if (Data.ComputedVisibleConversions)
    Record.readUnresolvedSet(Data.VisibleConversions);
  assert(Data.Definition && "Data.Definition should be already set!");

  if (!Data.IsLambda) {
    assert(!LambdaContext && !IndexInLambdaContext &&
           "given lambda context for non-lambda");

    Data.NumBases = Record.readInt();
    if (Data.NumBases)
      Data.Bases = ReadGlobalOffset();

    Data.NumVBases = Record.readInt();
    if (Data.NumVBases)
      Data.VBases = ReadGlobalOffset();

    Data.FirstFriend = readDeclID().getRawValue();
  } else {
    using Capture = LambdaCapture;

    auto &Lambda = static_cast<CXXRecordDecl::LambdaDefinitionData &>(Data);

    BitsUnpacker LambdaBits(Record.readInt());
    Lambda.DependencyKind = LambdaBits.getNextBits(/*Width=*/2);
    Lambda.IsGenericLambda = LambdaBits.getNextBit();
    Lambda.CaptureDefault = LambdaBits.getNextBits(/*Width=*/2);
    Lambda.NumCaptures = LambdaBits.getNextBits(/*Width=*/15);
    Lambda.HasKnownInternalLinkage = LambdaBits.getNextBit();

    Lambda.NumExplicitCaptures = Record.readInt();
    Lambda.ManglingNumber = Record.readInt();
    if (unsigned DeviceManglingNumber = Record.readInt())
      Reader.getContext().DeviceLambdaManglingNumbers[D] = DeviceManglingNumber;
    Lambda.IndexInContext = IndexInLambdaContext;
    Lambda.ContextDecl = LambdaContext;
    Capture *ToCapture = nullptr;
    if (Lambda.NumCaptures) {
      ToCapture = (Capture *)Reader.getContext().Allocate(sizeof(Capture) *
                                                          Lambda.NumCaptures);
      Lambda.AddCaptureList(Reader.getContext(), ToCapture);
    }
    Lambda.MethodTyInfo = readTypeSourceInfo();
    for (unsigned I = 0, N = Lambda.NumCaptures; I != N; ++I) {
      SourceLocation Loc = readSourceLocation();
      BitsUnpacker CaptureBits(Record.readInt());
      bool IsImplicit = CaptureBits.getNextBit();
      auto Kind =
          static_cast<LambdaCaptureKind>(CaptureBits.getNextBits(/*Width=*/3));
      switch (Kind) {
      case LCK_StarThis:
      case LCK_This:
      case LCK_VLAType:
        new (ToCapture)
            Capture(Loc, IsImplicit, Kind, nullptr, SourceLocation());
        ToCapture++;
        break;
      case LCK_ByCopy:
      case LCK_ByRef:
        auto *Var = readDeclAs<ValueDecl>();
        SourceLocation EllipsisLoc = readSourceLocation();
        new (ToCapture) Capture(Loc, IsImplicit, Kind, Var, EllipsisLoc);
        ToCapture++;
        break;
      }
    }
  }
}

void ASTDeclReader::MergeDefinitionData(
    CXXRecordDecl *D, struct CXXRecordDecl::DefinitionData &&MergeDD) {
  assert(D->DefinitionData &&
         "merging class definition into non-definition");
  auto &DD = *D->DefinitionData;

  if (DD.Definition != MergeDD.Definition) {
    // Track that we merged the definitions.
    Reader.MergedDeclContexts.insert(std::make_pair(MergeDD.Definition,
                                                    DD.Definition));
    Reader.PendingDefinitions.erase(MergeDD.Definition);
    MergeDD.Definition->setCompleteDefinition(false);
    Reader.mergeDefinitionVisibility(DD.Definition, MergeDD.Definition);
    assert(!Reader.Lookups.contains(MergeDD.Definition) &&
           "already loaded pending lookups for merged definition");
  }

  auto PFDI = Reader.PendingFakeDefinitionData.find(&DD);
  if (PFDI != Reader.PendingFakeDefinitionData.end() &&
      PFDI->second == ASTReader::PendingFakeDefinitionKind::Fake) {
    // We faked up this definition data because we found a class for which we'd
    // not yet loaded the definition. Replace it with the real thing now.
    assert(!DD.IsLambda && !MergeDD.IsLambda && "faked up lambda definition?");
    PFDI->second = ASTReader::PendingFakeDefinitionKind::FakeLoaded;

    // Don't change which declaration is the definition; that is required
    // to be invariant once we select it.
    auto *Def = DD.Definition;
    DD = std::move(MergeDD);
    DD.Definition = Def;
    return;
  }

  bool DetectedOdrViolation = false;

  #define FIELD(Name, Width, Merge) Merge(Name)
  #define MERGE_OR(Field) DD.Field |= MergeDD.Field;
  #define NO_MERGE(Field) \
    DetectedOdrViolation |= DD.Field != MergeDD.Field; \
    MERGE_OR(Field)
  #include "clang/AST/CXXRecordDeclDefinitionBits.def"
  NO_MERGE(IsLambda)
  #undef NO_MERGE
  #undef MERGE_OR

  if (DD.NumBases != MergeDD.NumBases || DD.NumVBases != MergeDD.NumVBases)
    DetectedOdrViolation = true;
  // FIXME: Issue a diagnostic if the base classes don't match when we come
  // to lazily load them.

  // FIXME: Issue a diagnostic if the list of conversion functions doesn't
  // match when we come to lazily load them.
  if (MergeDD.ComputedVisibleConversions && !DD.ComputedVisibleConversions) {
    DD.VisibleConversions = std::move(MergeDD.VisibleConversions);
    DD.ComputedVisibleConversions = true;
  }

  // FIXME: Issue a diagnostic if FirstFriend doesn't match when we come to
  // lazily load it.

  if (DD.IsLambda) {
    auto &Lambda1 = static_cast<CXXRecordDecl::LambdaDefinitionData &>(DD);
    auto &Lambda2 = static_cast<CXXRecordDecl::LambdaDefinitionData &>(MergeDD);
    DetectedOdrViolation |= Lambda1.DependencyKind != Lambda2.DependencyKind;
    DetectedOdrViolation |= Lambda1.IsGenericLambda != Lambda2.IsGenericLambda;
    DetectedOdrViolation |= Lambda1.CaptureDefault != Lambda2.CaptureDefault;
    DetectedOdrViolation |= Lambda1.NumCaptures != Lambda2.NumCaptures;
    DetectedOdrViolation |=
        Lambda1.NumExplicitCaptures != Lambda2.NumExplicitCaptures;
    DetectedOdrViolation |=
        Lambda1.HasKnownInternalLinkage != Lambda2.HasKnownInternalLinkage;
    DetectedOdrViolation |= Lambda1.ManglingNumber != Lambda2.ManglingNumber;

    if (Lambda1.NumCaptures && Lambda1.NumCaptures == Lambda2.NumCaptures) {
      for (unsigned I = 0, N = Lambda1.NumCaptures; I != N; ++I) {
        LambdaCapture &Cap1 = Lambda1.Captures.front()[I];
        LambdaCapture &Cap2 = Lambda2.Captures.front()[I];
        DetectedOdrViolation |= Cap1.getCaptureKind() != Cap2.getCaptureKind();
      }
      Lambda1.AddCaptureList(Reader.getContext(), Lambda2.Captures.front());
    }
  }

  // We don't want to check ODR for decls in the global module fragment.
  if (shouldSkipCheckingODR(MergeDD.Definition) || shouldSkipCheckingODR(D))
    return;

  if (D->getODRHash() != MergeDD.ODRHash) {
    DetectedOdrViolation = true;
  }

  if (DetectedOdrViolation)
    Reader.PendingOdrMergeFailures[DD.Definition].push_back(
        {MergeDD.Definition, &MergeDD});
}

void ASTDeclReader::ReadCXXRecordDefinition(CXXRecordDecl *D, bool Update,
                                            Decl *LambdaContext,
                                            unsigned IndexInLambdaContext) {
  struct CXXRecordDecl::DefinitionData *DD;
  ASTContext &C = Reader.getContext();

  // Determine whether this is a lambda closure type, so that we can
  // allocate the appropriate DefinitionData structure.
  bool IsLambda = Record.readInt();
  assert(!(IsLambda && Update) &&
         "lambda definition should not be added by update record");
  if (IsLambda)
    DD = new (C) CXXRecordDecl::LambdaDefinitionData(
        D, nullptr, CXXRecordDecl::LDK_Unknown, false, LCD_None);
  else
    DD = new (C) struct CXXRecordDecl::DefinitionData(D);

  CXXRecordDecl *Canon = D->getCanonicalDecl();
  // Set decl definition data before reading it, so that during deserialization
  // when we read CXXRecordDecl, it already has definition data and we don't
  // set fake one.
  if (!Canon->DefinitionData)
    Canon->DefinitionData = DD;
  D->DefinitionData = Canon->DefinitionData;
  ReadCXXDefinitionData(*DD, D, LambdaContext, IndexInLambdaContext);

  // We might already have a different definition for this record. This can
  // happen either because we're reading an update record, or because we've
  // already done some merging. Either way, just merge into it.
  if (Canon->DefinitionData != DD) {
    MergeDefinitionData(Canon, std::move(*DD));
    return;
  }

  // Mark this declaration as being a definition.
  D->setCompleteDefinition(true);

  // If this is not the first declaration or is an update record, we can have
  // other redeclarations already. Make a note that we need to propagate the
  // DefinitionData pointer onto them.
  if (Update || Canon != D)
    Reader.PendingDefinitions.insert(D);
}

ASTDeclReader::RedeclarableResult
ASTDeclReader::VisitCXXRecordDeclImpl(CXXRecordDecl *D) {
  RedeclarableResult Redecl = VisitRecordDeclImpl(D);

  ASTContext &C = Reader.getContext();

  enum CXXRecKind {
    CXXRecNotTemplate = 0,
    CXXRecTemplate,
    CXXRecMemberSpecialization,
    CXXLambda
  };

  Decl *LambdaContext = nullptr;
  unsigned IndexInLambdaContext = 0;

  switch ((CXXRecKind)Record.readInt()) {
  case CXXRecNotTemplate:
    // Merged when we merge the folding set entry in the primary template.
    if (!isa<ClassTemplateSpecializationDecl>(D))
      mergeRedeclarable(D, Redecl);
    break;
  case CXXRecTemplate: {
    // Merged when we merge the template.
    auto *Template = readDeclAs<ClassTemplateDecl>();
    D->TemplateOrInstantiation = Template;
    if (!Template->getTemplatedDecl()) {
      // We've not actually loaded the ClassTemplateDecl yet, because we're
      // currently being loaded as its pattern. Rely on it to set up our
      // TypeForDecl (see VisitClassTemplateDecl).
      //
      // Beware: we do not yet know our canonical declaration, and may still
      // get merged once the surrounding class template has got off the ground.
      DeferredTypeID = 0;
    }
    break;
  }
  case CXXRecMemberSpecialization: {
    auto *RD = readDeclAs<CXXRecordDecl>();
    auto TSK = (TemplateSpecializationKind)Record.readInt();
    SourceLocation POI = readSourceLocation();
    MemberSpecializationInfo *MSI = new (C) MemberSpecializationInfo(RD, TSK);
    MSI->setPointOfInstantiation(POI);
    D->TemplateOrInstantiation = MSI;
    mergeRedeclarable(D, Redecl);
    break;
  }
  case CXXLambda: {
    LambdaContext = readDecl();
    if (LambdaContext)
      IndexInLambdaContext = Record.readInt();
    mergeLambda(D, Redecl, LambdaContext, IndexInLambdaContext);
    break;
  }
  }

  bool WasDefinition = Record.readInt();
  if (WasDefinition)
    ReadCXXRecordDefinition(D, /*Update=*/false, LambdaContext,
                            IndexInLambdaContext);
  else
    // Propagate DefinitionData pointer from the canonical declaration.
    D->DefinitionData = D->getCanonicalDecl()->DefinitionData;

  // Lazily load the key function to avoid deserializing every method so we can
  // compute it.
  if (WasDefinition) {
    GlobalDeclID KeyFn = readDeclID();
    if (KeyFn.isValid() && D->isCompleteDefinition())
      // FIXME: This is wrong for the ARM ABI, where some other module may have
      // made this function no longer be a key function. We need an update
      // record or similar for that case.
      C.KeyFunctions[D] = KeyFn.getRawValue();
  }

  return Redecl;
}

void ASTDeclReader::VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D) {
  D->setExplicitSpecifier(Record.readExplicitSpec());
  D->Ctor = readDeclAs<CXXConstructorDecl>();
  VisitFunctionDecl(D);
  D->setDeductionCandidateKind(
      static_cast<DeductionCandidate>(Record.readInt()));
}

void ASTDeclReader::VisitCXXMethodDecl(CXXMethodDecl *D) {
  VisitFunctionDecl(D);

  unsigned NumOverridenMethods = Record.readInt();
  if (D->isCanonicalDecl()) {
    while (NumOverridenMethods--) {
      // Avoid invariant checking of CXXMethodDecl::addOverriddenMethod,
      // MD may be initializing.
      if (auto *MD = readDeclAs<CXXMethodDecl>())
        Reader.getContext().addOverriddenMethod(D, MD->getCanonicalDecl());
    }
  } else {
    // We don't care about which declarations this used to override; we get
    // the relevant information from the canonical declaration.
    Record.skipInts(NumOverridenMethods);
  }
}

void ASTDeclReader::VisitCXXConstructorDecl(CXXConstructorDecl *D) {
  // We need the inherited constructor information to merge the declaration,
  // so we have to read it before we call VisitCXXMethodDecl.
  D->setExplicitSpecifier(Record.readExplicitSpec());
  if (D->isInheritingConstructor()) {
    auto *Shadow = readDeclAs<ConstructorUsingShadowDecl>();
    auto *Ctor = readDeclAs<CXXConstructorDecl>();
    *D->getTrailingObjects<InheritedConstructor>() =
        InheritedConstructor(Shadow, Ctor);
  }

  VisitCXXMethodDecl(D);
}

void ASTDeclReader::VisitCXXDestructorDecl(CXXDestructorDecl *D) {
  VisitCXXMethodDecl(D);

  if (auto *OperatorDelete = readDeclAs<FunctionDecl>()) {
    CXXDestructorDecl *Canon = D->getCanonicalDecl();
    auto *ThisArg = Record.readExpr();
    // FIXME: Check consistency if we have an old and new operator delete.
    if (!Canon->OperatorDelete) {
      Canon->OperatorDelete = OperatorDelete;
      Canon->OperatorDeleteThisArg = ThisArg;
    }
  }
}

void ASTDeclReader::VisitCXXConversionDecl(CXXConversionDecl *D) {
  D->setExplicitSpecifier(Record.readExplicitSpec());
  VisitCXXMethodDecl(D);
}

void ASTDeclReader::VisitImportDecl(ImportDecl *D) {
  VisitDecl(D);
  D->ImportedModule = readModule();
  D->setImportComplete(Record.readInt());
  auto *StoredLocs = D->getTrailingObjects<SourceLocation>();
  for (unsigned I = 0, N = Record.back(); I != N; ++I)
    StoredLocs[I] = readSourceLocation();
  Record.skipInts(1); // The number of stored source locations.
}

void ASTDeclReader::VisitAccessSpecDecl(AccessSpecDecl *D) {
  VisitDecl(D);
  D->setColonLoc(readSourceLocation());
}

void ASTDeclReader::VisitFriendDecl(FriendDecl *D) {
  VisitDecl(D);
  if (Record.readInt()) // hasFriendDecl
    D->Friend = readDeclAs<NamedDecl>();
  else
    D->Friend = readTypeSourceInfo();
  for (unsigned i = 0; i != D->NumTPLists; ++i)
    D->getTrailingObjects<TemplateParameterList *>()[i] =
        Record.readTemplateParameterList();
  D->NextFriend = readDeclID().getRawValue();
  D->UnsupportedFriend = (Record.readInt() != 0);
  D->FriendLoc = readSourceLocation();
}

void ASTDeclReader::VisitFriendTemplateDecl(FriendTemplateDecl *D) {
  VisitDecl(D);
  unsigned NumParams = Record.readInt();
  D->NumParams = NumParams;
  D->Params = new (Reader.getContext()) TemplateParameterList *[NumParams];
  for (unsigned i = 0; i != NumParams; ++i)
    D->Params[i] = Record.readTemplateParameterList();
  if (Record.readInt()) // HasFriendDecl
    D->Friend = readDeclAs<NamedDecl>();
  else
    D->Friend = readTypeSourceInfo();
  D->FriendLoc = readSourceLocation();
}

void ASTDeclReader::VisitTemplateDecl(TemplateDecl *D) {
  VisitNamedDecl(D);

  assert(!D->TemplateParams && "TemplateParams already set!");
  D->TemplateParams = Record.readTemplateParameterList();
  D->init(readDeclAs<NamedDecl>());
}

void ASTDeclReader::VisitConceptDecl(ConceptDecl *D) {
  VisitTemplateDecl(D);
  D->ConstraintExpr = Record.readExpr();
  mergeMergeable(D);
}

void ASTDeclReader::VisitImplicitConceptSpecializationDecl(
    ImplicitConceptSpecializationDecl *D) {
  // The size of the template list was read during creation of the Decl, so we
  // don't have to re-read it here.
  VisitDecl(D);
  llvm::SmallVector<TemplateArgument, 4> Args;
  for (unsigned I = 0; I < D->NumTemplateArgs; ++I)
    Args.push_back(Record.readTemplateArgument(/*Canonicalize=*/true));
  D->setTemplateArguments(Args);
}

void ASTDeclReader::VisitRequiresExprBodyDecl(RequiresExprBodyDecl *D) {
}

ASTDeclReader::RedeclarableResult
ASTDeclReader::VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarable(D);

  // Make sure we've allocated the Common pointer first. We do this before
  // VisitTemplateDecl so that getCommonPtr() can be used during initialization.
  RedeclarableTemplateDecl *CanonD = D->getCanonicalDecl();
  if (!CanonD->Common) {
    CanonD->Common = CanonD->newCommon(Reader.getContext());
    Reader.PendingDefinitions.insert(CanonD);
  }
  D->Common = CanonD->Common;

  // If this is the first declaration of the template, fill in the information
  // for the 'common' pointer.
  if (ThisDeclID == Redecl.getFirstID()) {
    if (auto *RTD = readDeclAs<RedeclarableTemplateDecl>()) {
      assert(RTD->getKind() == D->getKind() &&
             "InstantiatedFromMemberTemplate kind mismatch");
      D->setInstantiatedFromMemberTemplate(RTD);
      if (Record.readInt())
        D->setMemberSpecialization();
    }
  }

  VisitTemplateDecl(D);
  D->IdentifierNamespace = Record.readInt();

  return Redecl;
}

void ASTDeclReader::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarableTemplateDecl(D);
  mergeRedeclarableTemplate(D, Redecl);

  if (ThisDeclID == Redecl.getFirstID()) {
    // This ClassTemplateDecl owns a CommonPtr; read it to keep track of all of
    // the specializations.
    SmallVector<GlobalDeclID, 32> SpecIDs;
    readDeclIDList(SpecIDs);
    ASTDeclReader::AddLazySpecializations(D, SpecIDs);
  }

  if (D->getTemplatedDecl()->TemplateOrInstantiation) {
    // We were loaded before our templated declaration was. We've not set up
    // its corresponding type yet (see VisitCXXRecordDeclImpl), so reconstruct
    // it now.
    Reader.getContext().getInjectedClassNameType(
        D->getTemplatedDecl(), D->getInjectedClassNameSpecialization());
  }
}

void ASTDeclReader::VisitBuiltinTemplateDecl(BuiltinTemplateDecl *D) {
  llvm_unreachable("BuiltinTemplates are not serialized");
}

/// TODO: Unify with ClassTemplateDecl version?
///       May require unifying ClassTemplateDecl and
///        VarTemplateDecl beyond TemplateDecl...
void ASTDeclReader::VisitVarTemplateDecl(VarTemplateDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarableTemplateDecl(D);
  mergeRedeclarableTemplate(D, Redecl);

  if (ThisDeclID == Redecl.getFirstID()) {
    // This VarTemplateDecl owns a CommonPtr; read it to keep track of all of
    // the specializations.
    SmallVector<GlobalDeclID, 32> SpecIDs;
    readDeclIDList(SpecIDs);
    ASTDeclReader::AddLazySpecializations(D, SpecIDs);
  }
}

ASTDeclReader::RedeclarableResult
ASTDeclReader::VisitClassTemplateSpecializationDeclImpl(
    ClassTemplateSpecializationDecl *D) {
  RedeclarableResult Redecl = VisitCXXRecordDeclImpl(D);

  ASTContext &C = Reader.getContext();
  if (Decl *InstD = readDecl()) {
    if (auto *CTD = dyn_cast<ClassTemplateDecl>(InstD)) {
      D->SpecializedTemplate = CTD;
    } else {
      SmallVector<TemplateArgument, 8> TemplArgs;
      Record.readTemplateArgumentList(TemplArgs);
      TemplateArgumentList *ArgList
        = TemplateArgumentList::CreateCopy(C, TemplArgs);
      auto *PS =
          new (C) ClassTemplateSpecializationDecl::
                                             SpecializedPartialSpecialization();
      PS->PartialSpecialization
          = cast<ClassTemplatePartialSpecializationDecl>(InstD);
      PS->TemplateArgs = ArgList;
      D->SpecializedTemplate = PS;
    }
  }

  SmallVector<TemplateArgument, 8> TemplArgs;
  Record.readTemplateArgumentList(TemplArgs, /*Canonicalize*/ true);
  D->TemplateArgs = TemplateArgumentList::CreateCopy(C, TemplArgs);
  D->PointOfInstantiation = readSourceLocation();
  D->SpecializationKind = (TemplateSpecializationKind)Record.readInt();

  bool writtenAsCanonicalDecl = Record.readInt();
  if (writtenAsCanonicalDecl) {
    auto *CanonPattern = readDeclAs<ClassTemplateDecl>();
    if (D->isCanonicalDecl()) { // It's kept in the folding set.
      // Set this as, or find, the canonical declaration for this specialization
      ClassTemplateSpecializationDecl *CanonSpec;
      if (auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(D)) {
        CanonSpec = CanonPattern->getCommonPtr()->PartialSpecializations
            .GetOrInsertNode(Partial);
      } else {
        CanonSpec =
            CanonPattern->getCommonPtr()->Specializations.GetOrInsertNode(D);
      }
      // If there was already a canonical specialization, merge into it.
      if (CanonSpec != D) {
        mergeRedeclarable<TagDecl>(D, CanonSpec, Redecl);

        // This declaration might be a definition. Merge with any existing
        // definition.
        if (auto *DDD = D->DefinitionData) {
          if (CanonSpec->DefinitionData)
            MergeDefinitionData(CanonSpec, std::move(*DDD));
          else
            CanonSpec->DefinitionData = D->DefinitionData;
        }
        D->DefinitionData = CanonSpec->DefinitionData;
      }
    }
  }

  // extern/template keyword locations for explicit instantiations
  if (Record.readBool()) {
    auto *ExplicitInfo = new (C) ExplicitInstantiationInfo;
    ExplicitInfo->ExternKeywordLoc = readSourceLocation();
    ExplicitInfo->TemplateKeywordLoc = readSourceLocation();
    D->ExplicitInfo = ExplicitInfo;
  }

  if (Record.readBool())
    D->setTemplateArgsAsWritten(Record.readASTTemplateArgumentListInfo());

  return Redecl;
}

void ASTDeclReader::VisitClassTemplatePartialSpecializationDecl(
                                    ClassTemplatePartialSpecializationDecl *D) {
  // We need to read the template params first because redeclarable is going to
  // need them for profiling
  TemplateParameterList *Params = Record.readTemplateParameterList();
  D->TemplateParams = Params;

  RedeclarableResult Redecl = VisitClassTemplateSpecializationDeclImpl(D);

  // These are read/set from/to the first declaration.
  if (ThisDeclID == Redecl.getFirstID()) {
    D->InstantiatedFromMember.setPointer(
      readDeclAs<ClassTemplatePartialSpecializationDecl>());
    D->InstantiatedFromMember.setInt(Record.readInt());
  }
}

void ASTDeclReader::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarableTemplateDecl(D);

  if (ThisDeclID == Redecl.getFirstID()) {
    // This FunctionTemplateDecl owns a CommonPtr; read it.
    SmallVector<GlobalDeclID, 32> SpecIDs;
    readDeclIDList(SpecIDs);
    ASTDeclReader::AddLazySpecializations(D, SpecIDs);
  }
}

/// TODO: Unify with ClassTemplateSpecializationDecl version?
///       May require unifying ClassTemplate(Partial)SpecializationDecl and
///        VarTemplate(Partial)SpecializationDecl with a new data
///        structure Template(Partial)SpecializationDecl, and
///        using Template(Partial)SpecializationDecl as input type.
ASTDeclReader::RedeclarableResult
ASTDeclReader::VisitVarTemplateSpecializationDeclImpl(
    VarTemplateSpecializationDecl *D) {
  ASTContext &C = Reader.getContext();
  if (Decl *InstD = readDecl()) {
    if (auto *VTD = dyn_cast<VarTemplateDecl>(InstD)) {
      D->SpecializedTemplate = VTD;
    } else {
      SmallVector<TemplateArgument, 8> TemplArgs;
      Record.readTemplateArgumentList(TemplArgs);
      TemplateArgumentList *ArgList = TemplateArgumentList::CreateCopy(
          C, TemplArgs);
      auto *PS =
          new (C)
          VarTemplateSpecializationDecl::SpecializedPartialSpecialization();
      PS->PartialSpecialization =
          cast<VarTemplatePartialSpecializationDecl>(InstD);
      PS->TemplateArgs = ArgList;
      D->SpecializedTemplate = PS;
    }
  }

  // extern/template keyword locations for explicit instantiations
  if (Record.readBool()) {
    auto *ExplicitInfo = new (C) ExplicitInstantiationInfo;
    ExplicitInfo->ExternKeywordLoc = readSourceLocation();
    ExplicitInfo->TemplateKeywordLoc = readSourceLocation();
    D->ExplicitInfo = ExplicitInfo;
  }

  if (Record.readBool())
    D->setTemplateArgsAsWritten(Record.readASTTemplateArgumentListInfo());

  SmallVector<TemplateArgument, 8> TemplArgs;
  Record.readTemplateArgumentList(TemplArgs, /*Canonicalize*/ true);
  D->TemplateArgs = TemplateArgumentList::CreateCopy(C, TemplArgs);
  D->PointOfInstantiation = readSourceLocation();
  D->SpecializationKind = (TemplateSpecializationKind)Record.readInt();
  D->IsCompleteDefinition = Record.readInt();

  RedeclarableResult Redecl = VisitVarDeclImpl(D);

  bool writtenAsCanonicalDecl = Record.readInt();
  if (writtenAsCanonicalDecl) {
    auto *CanonPattern = readDeclAs<VarTemplateDecl>();
    if (D->isCanonicalDecl()) { // It's kept in the folding set.
      VarTemplateSpecializationDecl *CanonSpec;
      if (auto *Partial = dyn_cast<VarTemplatePartialSpecializationDecl>(D)) {
        CanonSpec = CanonPattern->getCommonPtr()
                        ->PartialSpecializations.GetOrInsertNode(Partial);
      } else {
        CanonSpec =
            CanonPattern->getCommonPtr()->Specializations.GetOrInsertNode(D);
      }
      // If we already have a matching specialization, merge it.
      if (CanonSpec != D)
        mergeRedeclarable<VarDecl>(D, CanonSpec, Redecl);
    }
  }

  return Redecl;
}

/// TODO: Unify with ClassTemplatePartialSpecializationDecl version?
///       May require unifying ClassTemplate(Partial)SpecializationDecl and
///        VarTemplate(Partial)SpecializationDecl with a new data
///        structure Template(Partial)SpecializationDecl, and
///        using Template(Partial)SpecializationDecl as input type.
void ASTDeclReader::VisitVarTemplatePartialSpecializationDecl(
    VarTemplatePartialSpecializationDecl *D) {
  TemplateParameterList *Params = Record.readTemplateParameterList();
  D->TemplateParams = Params;

  RedeclarableResult Redecl = VisitVarTemplateSpecializationDeclImpl(D);

  // These are read/set from/to the first declaration.
  if (ThisDeclID == Redecl.getFirstID()) {
    D->InstantiatedFromMember.setPointer(
        readDeclAs<VarTemplatePartialSpecializationDecl>());
    D->InstantiatedFromMember.setInt(Record.readInt());
  }
}

void ASTDeclReader::VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D) {
  VisitTypeDecl(D);

  D->setDeclaredWithTypename(Record.readInt());

  const bool TypeConstraintInitialized = Record.readBool();
  if (TypeConstraintInitialized && D->hasTypeConstraint()) {
    ConceptReference *CR = nullptr;
    if (Record.readBool())
      CR = Record.readConceptReference();
    Expr *ImmediatelyDeclaredConstraint = Record.readExpr();

    D->setTypeConstraint(CR, ImmediatelyDeclaredConstraint);
    if ((D->ExpandedParameterPack = Record.readInt()))
      D->NumExpanded = Record.readInt();
  }

  if (Record.readInt())
    D->setDefaultArgument(Reader.getContext(),
                          Record.readTemplateArgumentLoc());
}

void ASTDeclReader::VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D) {
  VisitDeclaratorDecl(D);
  // TemplateParmPosition.
  D->setDepth(Record.readInt());
  D->setPosition(Record.readInt());
  if (D->hasPlaceholderTypeConstraint())
    D->setPlaceholderTypeConstraint(Record.readExpr());
  if (D->isExpandedParameterPack()) {
    auto TypesAndInfos =
        D->getTrailingObjects<std::pair<QualType, TypeSourceInfo *>>();
    for (unsigned I = 0, N = D->getNumExpansionTypes(); I != N; ++I) {
      new (&TypesAndInfos[I].first) QualType(Record.readType());
      TypesAndInfos[I].second = readTypeSourceInfo();
    }
  } else {
    // Rest of NonTypeTemplateParmDecl.
    D->ParameterPack = Record.readInt();
    if (Record.readInt())
      D->setDefaultArgument(Reader.getContext(),
                            Record.readTemplateArgumentLoc());
  }
}

void ASTDeclReader::VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D) {
  VisitTemplateDecl(D);
  D->setDeclaredWithTypename(Record.readBool());
  // TemplateParmPosition.
  D->setDepth(Record.readInt());
  D->setPosition(Record.readInt());
  if (D->isExpandedParameterPack()) {
    auto **Data = D->getTrailingObjects<TemplateParameterList *>();
    for (unsigned I = 0, N = D->getNumExpansionTemplateParameters();
         I != N; ++I)
      Data[I] = Record.readTemplateParameterList();
  } else {
    // Rest of TemplateTemplateParmDecl.
    D->ParameterPack = Record.readInt();
    if (Record.readInt())
      D->setDefaultArgument(Reader.getContext(),
                            Record.readTemplateArgumentLoc());
  }
}

void ASTDeclReader::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
  RedeclarableResult Redecl = VisitRedeclarableTemplateDecl(D);
  mergeRedeclarableTemplate(D, Redecl);
}

void ASTDeclReader::VisitStaticAssertDecl(StaticAssertDecl *D) {
  VisitDecl(D);
  D->AssertExprAndFailed.setPointer(Record.readExpr());
  D->AssertExprAndFailed.setInt(Record.readInt());
  D->Message = cast_or_null<StringLiteral>(Record.readExpr());
  D->RParenLoc = readSourceLocation();
}

void ASTDeclReader::VisitEmptyDecl(EmptyDecl *D) {
  VisitDecl(D);
}

void ASTDeclReader::VisitLifetimeExtendedTemporaryDecl(
    LifetimeExtendedTemporaryDecl *D) {
  VisitDecl(D);
  D->ExtendingDecl = readDeclAs<ValueDecl>();
  D->ExprWithTemporary = Record.readStmt();
  if (Record.readInt()) {
    D->Value = new (D->getASTContext()) APValue(Record.readAPValue());
    D->getASTContext().addDestruction(D->Value);
  }
  D->ManglingNumber = Record.readInt();
  mergeMergeable(D);
}

std::pair<uint64_t, uint64_t>
ASTDeclReader::VisitDeclContext(DeclContext *DC) {
  uint64_t LexicalOffset = ReadLocalOffset();
  uint64_t VisibleOffset = ReadLocalOffset();
  return std::make_pair(LexicalOffset, VisibleOffset);
}

template <typename T>
ASTDeclReader::RedeclarableResult
ASTDeclReader::VisitRedeclarable(Redeclarable<T> *D) {
  GlobalDeclID FirstDeclID = readDeclID();
  Decl *MergeWith = nullptr;

  bool IsKeyDecl = ThisDeclID == FirstDeclID;
  bool IsFirstLocalDecl = false;

  uint64_t RedeclOffset = 0;

  // invalid FirstDeclID  indicates that this declaration was the only
  // declaration of its entity, and is used for space optimization.
  if (FirstDeclID.isInvalid()) {
    FirstDeclID = ThisDeclID;
    IsKeyDecl = true;
    IsFirstLocalDecl = true;
  } else if (unsigned N = Record.readInt()) {
    // This declaration was the first local declaration, but may have imported
    // other declarations.
    IsKeyDecl = N == 1;
    IsFirstLocalDecl = true;

    // We have some declarations that must be before us in our redeclaration
    // chain. Read them now, and remember that we ought to merge with one of
    // them.
    // FIXME: Provide a known merge target to the second and subsequent such
    // declaration.
    for (unsigned I = 0; I != N - 1; ++I)
      MergeWith = readDecl();

    RedeclOffset = ReadLocalOffset();
  } else {
    // This declaration was not the first local declaration. Read the first
    // local declaration now, to trigger the import of other redeclarations.
    (void)readDecl();
  }

  auto *FirstDecl = cast_or_null<T>(Reader.GetDecl(FirstDeclID));
  if (FirstDecl != D) {
    // We delay loading of the redeclaration chain to avoid deeply nested calls.
    // We temporarily set the first (canonical) declaration as the previous one
    // which is the one that matters and mark the real previous DeclID to be
    // loaded & attached later on.
    D->RedeclLink = Redeclarable<T>::PreviousDeclLink(FirstDecl);
    D->First = FirstDecl->getCanonicalDecl();
  }

  auto *DAsT = static_cast<T *>(D);

  // Note that we need to load local redeclarations of this decl and build a
  // decl chain for them. This must happen *after* we perform the preloading
  // above; this ensures that the redeclaration chain is built in the correct
  // order.
  if (IsFirstLocalDecl)
    Reader.PendingDeclChains.push_back(std::make_pair(DAsT, RedeclOffset));

  return RedeclarableResult(MergeWith, FirstDeclID, IsKeyDecl);
}

/// Attempts to merge the given declaration (D) with another declaration
/// of the same entity.
template <typename T>
void ASTDeclReader::mergeRedeclarable(Redeclarable<T> *DBase,
                                      RedeclarableResult &Redecl) {
  // If modules are not available, there is no reason to perform this merge.
  if (!Reader.getContext().getLangOpts().Modules)
    return;

  // If we're not the canonical declaration, we don't need to merge.
  if (!DBase->isFirstDecl())
    return;

  auto *D = static_cast<T *>(DBase);

  if (auto *Existing = Redecl.getKnownMergeTarget())
    // We already know of an existing declaration we should merge with.
    mergeRedeclarable(D, cast<T>(Existing), Redecl);
  else if (FindExistingResult ExistingRes = findExisting(D))
    if (T *Existing = ExistingRes)
      mergeRedeclarable(D, Existing, Redecl);
}

/// Attempt to merge D with a previous declaration of the same lambda, which is
/// found by its index within its context declaration, if it has one.
///
/// We can't look up lambdas in their enclosing lexical or semantic context in
/// general, because for lambdas in variables, both of those might be a
/// namespace or the translation unit.
void ASTDeclReader::mergeLambda(CXXRecordDecl *D, RedeclarableResult &Redecl,
                                Decl *Context, unsigned IndexInContext) {
  // If we don't have a mangling context, treat this like any other
  // declaration.
  if (!Context)
    return mergeRedeclarable(D, Redecl);

  // If modules are not available, there is no reason to perform this merge.
  if (!Reader.getContext().getLangOpts().Modules)
    return;

  // If we're not the canonical declaration, we don't need to merge.
  if (!D->isFirstDecl())
    return;

  if (auto *Existing = Redecl.getKnownMergeTarget())
    // We already know of an existing declaration we should merge with.
    mergeRedeclarable(D, cast<TagDecl>(Existing), Redecl);

  // Look up this lambda to see if we've seen it before. If so, merge with the
  // one we already loaded.
  NamedDecl *&Slot = Reader.LambdaDeclarationsForMerging[{
      Context->getCanonicalDecl(), IndexInContext}];
  if (Slot)
    mergeRedeclarable(D, cast<TagDecl>(Slot), Redecl);
  else
    Slot = D;
}

void ASTDeclReader::mergeRedeclarableTemplate(RedeclarableTemplateDecl *D,
                                              RedeclarableResult &Redecl) {
  mergeRedeclarable(D, Redecl);
  // If we merged the template with a prior declaration chain, merge the
  // common pointer.
  // FIXME: Actually merge here, don't just overwrite.
  D->Common = D->getCanonicalDecl()->Common;
}

/// "Cast" to type T, asserting if we don't have an implicit conversion.
/// We use this to put code in a template that will only be valid for certain
/// instantiations.
template<typename T> static T assert_cast(T t) { return t; }
template<typename T> static T assert_cast(...) {
  llvm_unreachable("bad assert_cast");
}

/// Merge together the pattern declarations from two template
/// declarations.
void ASTDeclReader::mergeTemplatePattern(RedeclarableTemplateDecl *D,
                                         RedeclarableTemplateDecl *Existing,
                                         bool IsKeyDecl) {
  auto *DPattern = D->getTemplatedDecl();
  auto *ExistingPattern = Existing->getTemplatedDecl();
  RedeclarableResult Result(
      /*MergeWith*/ ExistingPattern,
      DPattern->getCanonicalDecl()->getGlobalID(), IsKeyDecl);

  if (auto *DClass = dyn_cast<CXXRecordDecl>(DPattern)) {
    // Merge with any existing definition.
    // FIXME: This is duplicated in several places. Refactor.
    auto *ExistingClass =
        cast<CXXRecordDecl>(ExistingPattern)->getCanonicalDecl();
    if (auto *DDD = DClass->DefinitionData) {
      if (ExistingClass->DefinitionData) {
        MergeDefinitionData(ExistingClass, std::move(*DDD));
      } else {
        ExistingClass->DefinitionData = DClass->DefinitionData;
        // We may have skipped this before because we thought that DClass
        // was the canonical declaration.
        Reader.PendingDefinitions.insert(DClass);
      }
    }
    DClass->DefinitionData = ExistingClass->DefinitionData;

    return mergeRedeclarable(DClass, cast<TagDecl>(ExistingPattern),
                             Result);
  }
  if (auto *DFunction = dyn_cast<FunctionDecl>(DPattern))
    return mergeRedeclarable(DFunction, cast<FunctionDecl>(ExistingPattern),
                             Result);
  if (auto *DVar = dyn_cast<VarDecl>(DPattern))
    return mergeRedeclarable(DVar, cast<VarDecl>(ExistingPattern), Result);
  if (auto *DAlias = dyn_cast<TypeAliasDecl>(DPattern))
    return mergeRedeclarable(DAlias, cast<TypedefNameDecl>(ExistingPattern),
                             Result);
  llvm_unreachable("merged an unknown kind of redeclarable template");
}

/// Attempts to merge the given declaration (D) with another declaration
/// of the same entity.
template <typename T>
void ASTDeclReader::mergeRedeclarable(Redeclarable<T> *DBase, T *Existing,
                                      RedeclarableResult &Redecl) {
  auto *D = static_cast<T *>(DBase);
  T *ExistingCanon = Existing->getCanonicalDecl();
  T *DCanon = D->getCanonicalDecl();
  if (ExistingCanon != DCanon) {
    // Have our redeclaration link point back at the canonical declaration
    // of the existing declaration, so that this declaration has the
    // appropriate canonical declaration.
    D->RedeclLink = Redeclarable<T>::PreviousDeclLink(ExistingCanon);
    D->First = ExistingCanon;
    ExistingCanon->Used |= D->Used;
    D->Used = false;

    // When we merge a template, merge its pattern.
    if (auto *DTemplate = dyn_cast<RedeclarableTemplateDecl>(D))
      mergeTemplatePattern(
          DTemplate, assert_cast<RedeclarableTemplateDecl *>(ExistingCanon),
          Redecl.isKeyDecl());

    // If this declaration is a key declaration, make a note of that.
    if (Redecl.isKeyDecl())
      Reader.KeyDecls[ExistingCanon].push_back(Redecl.getFirstID());
  }
}

/// ODR-like semantics for C/ObjC allow us to merge tag types and a structural
/// check in Sema guarantees the types can be merged (see C11 6.2.7/1 or C89
/// 6.1.2.6/1). Although most merging is done in Sema, we need to guarantee
/// that some types are mergeable during deserialization, otherwise name
/// lookup fails. This is the case for EnumConstantDecl.
static bool allowODRLikeMergeInC(NamedDecl *ND) {
  if (!ND)
    return false;
  // TODO: implement merge for other necessary decls.
  if (isa<EnumConstantDecl, FieldDecl, IndirectFieldDecl>(ND))
    return true;
  return false;
}

/// Attempts to merge LifetimeExtendedTemporaryDecl with
/// identical class definitions from two different modules.
void ASTDeclReader::mergeMergeable(LifetimeExtendedTemporaryDecl *D) {
  // If modules are not available, there is no reason to perform this merge.
  if (!Reader.getContext().getLangOpts().Modules)
    return;

  LifetimeExtendedTemporaryDecl *LETDecl = D;

  LifetimeExtendedTemporaryDecl *&LookupResult =
      Reader.LETemporaryForMerging[std::make_pair(
          LETDecl->getExtendingDecl(), LETDecl->getManglingNumber())];
  if (LookupResult)
    Reader.getContext().setPrimaryMergedDecl(LETDecl,
                                             LookupResult->getCanonicalDecl());
  else
    LookupResult = LETDecl;
}

/// Attempts to merge the given declaration (D) with another declaration
/// of the same entity, for the case where the entity is not actually
/// redeclarable. This happens, for instance, when merging the fields of
/// identical class definitions from two different modules.
template<typename T>
void ASTDeclReader::mergeMergeable(Mergeable<T> *D) {
  // If modules are not available, there is no reason to perform this merge.
  if (!Reader.getContext().getLangOpts().Modules)
    return;

  // ODR-based merging is performed in C++ and in some cases (tag types) in C.
  // Note that C identically-named things in different translation units are
  // not redeclarations, but may still have compatible types, where ODR-like
  // semantics may apply.
  if (!Reader.getContext().getLangOpts().CPlusPlus &&
      !allowODRLikeMergeInC(dyn_cast<NamedDecl>(static_cast<T*>(D))))
    return;

  if (FindExistingResult ExistingRes = findExisting(static_cast<T*>(D)))
    if (T *Existing = ExistingRes)
      Reader.getContext().setPrimaryMergedDecl(static_cast<T *>(D),
                                               Existing->getCanonicalDecl());
}

void ASTDeclReader::VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D) {
  Record.readOMPChildren(D->Data);
  VisitDecl(D);
}

void ASTDeclReader::VisitOMPAllocateDecl(OMPAllocateDecl *D) {
  Record.readOMPChildren(D->Data);
  VisitDecl(D);
}

void ASTDeclReader::VisitOMPRequiresDecl(OMPRequiresDecl * D) {
  Record.readOMPChildren(D->Data);
  VisitDecl(D);
}

void ASTDeclReader::VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D) {
  VisitValueDecl(D);
  D->setLocation(readSourceLocation());
  Expr *In = Record.readExpr();
  Expr *Out = Record.readExpr();
  D->setCombinerData(In, Out);
  Expr *Combiner = Record.readExpr();
  D->setCombiner(Combiner);
  Expr *Orig = Record.readExpr();
  Expr *Priv = Record.readExpr();
  D->setInitializerData(Orig, Priv);
  Expr *Init = Record.readExpr();
  auto IK = static_cast<OMPDeclareReductionInitKind>(Record.readInt());
  D->setInitializer(Init, IK);
  D->PrevDeclInScope = readDeclID().getRawValue();
}

void ASTDeclReader::VisitOMPDeclareMapperDecl(OMPDeclareMapperDecl *D) {
  Record.readOMPChildren(D->Data);
  VisitValueDecl(D);
  D->VarName = Record.readDeclarationName();
  D->PrevDeclInScope = readDeclID().getRawValue();
}

void ASTDeclReader::VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D) {
  VisitVarDecl(D);
}

//===----------------------------------------------------------------------===//
// Attribute Reading
//===----------------------------------------------------------------------===//

namespace {
class AttrReader {
  ASTRecordReader &Reader;

public:
  AttrReader(ASTRecordReader &Reader) : Reader(Reader) {}

  uint64_t readInt() {
    return Reader.readInt();
  }

  bool readBool() { return Reader.readBool(); }

  SourceRange readSourceRange() {
    return Reader.readSourceRange();
  }

  SourceLocation readSourceLocation() {
    return Reader.readSourceLocation();
  }

  Expr *readExpr() { return Reader.readExpr(); }

  Attr *readAttr() { return Reader.readAttr(); }

  std::string readString() {
    return Reader.readString();
  }

  TypeSourceInfo *readTypeSourceInfo() {
    return Reader.readTypeSourceInfo();
  }

  IdentifierInfo *readIdentifier() {
    return Reader.readIdentifier();
  }

  VersionTuple readVersionTuple() {
    return Reader.readVersionTuple();
  }

  OMPTraitInfo *readOMPTraitInfo() { return Reader.readOMPTraitInfo(); }

  template <typename T> T *readDeclAs() { return Reader.readDeclAs<T>(); }
};
}

Attr *ASTRecordReader::readAttr() {
  AttrReader Record(*this);
  auto V = Record.readInt();
  if (!V)
    return nullptr;

  Attr *New = nullptr;
  // Kind is stored as a 1-based integer because 0 is used to indicate a null
  // Attr pointer.
  auto Kind = static_cast<attr::Kind>(V - 1);
  ASTContext &Context = getContext();

  IdentifierInfo *AttrName = Record.readIdentifier();
  IdentifierInfo *ScopeName = Record.readIdentifier();
  SourceRange AttrRange = Record.readSourceRange();
  SourceLocation ScopeLoc = Record.readSourceLocation();
  unsigned ParsedKind = Record.readInt();
  unsigned Syntax = Record.readInt();
  unsigned SpellingIndex = Record.readInt();
  bool IsAlignas = (ParsedKind == AttributeCommonInfo::AT_Aligned &&
                    Syntax == AttributeCommonInfo::AS_Keyword &&
                    SpellingIndex == AlignedAttr::Keyword_alignas);
  bool IsRegularKeywordAttribute = Record.readBool();

  AttributeCommonInfo Info(AttrName, ScopeName, AttrRange, ScopeLoc,
                           AttributeCommonInfo::Kind(ParsedKind),
                           {AttributeCommonInfo::Syntax(Syntax), SpellingIndex,
                            IsAlignas, IsRegularKeywordAttribute});

#include "clang/Serialization/AttrPCHRead.inc"

  assert(New && "Unable to decode attribute?");
  return New;
}

/// Reads attributes from the current stream position.
void ASTRecordReader::readAttributes(AttrVec &Attrs) {
  for (unsigned I = 0, E = readInt(); I != E; ++I)
    if (auto *A = readAttr())
      Attrs.push_back(A);
}

//===----------------------------------------------------------------------===//
// ASTReader Implementation
//===----------------------------------------------------------------------===//

/// Note that we have loaded the declaration with the given
/// Index.
///
/// This routine notes that this declaration has already been loaded,
/// so that future GetDecl calls will return this declaration rather
/// than trying to load a new declaration.
inline void ASTReader::LoadedDecl(unsigned Index, Decl *D) {
  assert(!DeclsLoaded[Index] && "Decl loaded twice?");
  DeclsLoaded[Index] = D;
}

/// Determine whether the consumer will be interested in seeing
/// this declaration (via HandleTopLevelDecl).
///
/// This routine should return true for anything that might affect
/// code generation, e.g., inline function definitions, Objective-C
/// declarations with metadata, etc.
bool ASTReader::isConsumerInterestedIn(Decl *D) {
  // An ObjCMethodDecl is never considered as "interesting" because its
  // implementation container always is.

  // An ImportDecl or VarDecl imported from a module map module will get
  // emitted when we import the relevant module.
  if (isPartOfPerModuleInitializer(D)) {
    auto *M = D->getImportedOwningModule();
    if (M && M->Kind == Module::ModuleMapModule &&
        getContext().DeclMustBeEmitted(D))
      return false;
  }

  if (isa<FileScopeAsmDecl, TopLevelStmtDecl, ObjCProtocolDecl, ObjCImplDecl,
          ImportDecl, PragmaCommentDecl, PragmaDetectMismatchDecl>(D))
    return true;
  if (isa<OMPThreadPrivateDecl, OMPDeclareReductionDecl, OMPDeclareMapperDecl,
          OMPAllocateDecl, OMPRequiresDecl>(D))
    return !D->getDeclContext()->isFunctionOrMethod();
  if (const auto *Var = dyn_cast<VarDecl>(D))
    return Var->isFileVarDecl() &&
           (Var->isThisDeclarationADefinition() == VarDecl::Definition ||
            OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(Var));
  if (const auto *Func = dyn_cast<FunctionDecl>(D))
    return Func->doesThisDeclarationHaveABody() || PendingBodies.count(D);

  if (auto *ES = D->getASTContext().getExternalSource())
    if (ES->hasExternalDefinitions(D) == ExternalASTSource::EK_Never)
      return true;

  return false;
}

/// Get the correct cursor and offset for loading a declaration.
ASTReader::RecordLocation ASTReader::DeclCursorForID(GlobalDeclID ID,
                                                     SourceLocation &Loc) {
  ModuleFile *M = getOwningModuleFile(ID);
  assert(M);
  unsigned LocalDeclIndex = ID.getLocalDeclIndex();
  const DeclOffset &DOffs = M->DeclOffsets[LocalDeclIndex];
  Loc = ReadSourceLocation(*M, DOffs.getRawLoc());
  return RecordLocation(M, DOffs.getBitOffset(M->DeclsBlockStartOffset));
}

ASTReader::RecordLocation ASTReader::getLocalBitOffset(uint64_t GlobalOffset) {
  auto I = GlobalBitOffsetsMap.find(GlobalOffset);

  assert(I != GlobalBitOffsetsMap.end() && "Corrupted global bit offsets map");
  return RecordLocation(I->second, GlobalOffset - I->second->GlobalBitOffset);
}

uint64_t ASTReader::getGlobalBitOffset(ModuleFile &M, uint64_t LocalOffset) {
  return LocalOffset + M.GlobalBitOffset;
}

CXXRecordDecl *
ASTDeclReader::getOrFakePrimaryClassDefinition(ASTReader &Reader,
                                               CXXRecordDecl *RD) {
  // Try to dig out the definition.
  auto *DD = RD->DefinitionData;
  if (!DD)
    DD = RD->getCanonicalDecl()->DefinitionData;

  // If there's no definition yet, then DC's definition is added by an update
  // record, but we've not yet loaded that update record. In this case, we
  // commit to DC being the canonical definition now, and will fix this when
  // we load the update record.
  if (!DD) {
    DD = new (Reader.getContext()) struct CXXRecordDecl::DefinitionData(RD);
    RD->setCompleteDefinition(true);
    RD->DefinitionData = DD;
    RD->getCanonicalDecl()->DefinitionData = DD;

    // Track that we did this horrible thing so that we can fix it later.
    Reader.PendingFakeDefinitionData.insert(
        std::make_pair(DD, ASTReader::PendingFakeDefinitionKind::Fake));
  }

  return DD->Definition;
}

/// Find the context in which we should search for previous declarations when
/// looking for declarations to merge.
DeclContext *ASTDeclReader::getPrimaryContextForMerging(ASTReader &Reader,
                                                        DeclContext *DC) {
  if (auto *ND = dyn_cast<NamespaceDecl>(DC))
    return ND->getFirstDecl();

  if (auto *RD = dyn_cast<CXXRecordDecl>(DC))
    return getOrFakePrimaryClassDefinition(Reader, RD);

  if (auto *RD = dyn_cast<RecordDecl>(DC))
    return RD->getDefinition();

  if (auto *ED = dyn_cast<EnumDecl>(DC))
    return ED->getDefinition();

  if (auto *OID = dyn_cast<ObjCInterfaceDecl>(DC))
    return OID->getDefinition();

  // We can see the TU here only if we have no Sema object. It is possible
  // we're in clang-repl so we still need to get the primary context.
  if (auto *TU = dyn_cast<TranslationUnitDecl>(DC))
    return TU->getPrimaryContext();

  return nullptr;
}

ASTDeclReader::FindExistingResult::~FindExistingResult() {
  // Record that we had a typedef name for linkage whether or not we merge
  // with that declaration.
  if (TypedefNameForLinkage) {
    DeclContext *DC = New->getDeclContext()->getRedeclContext();
    Reader.ImportedTypedefNamesForLinkage.insert(
        std::make_pair(std::make_pair(DC, TypedefNameForLinkage), New));
    return;
  }

  if (!AddResult || Existing)
    return;

  DeclarationName Name = New->getDeclName();
  DeclContext *DC = New->getDeclContext()->getRedeclContext();
  if (needsAnonymousDeclarationNumber(New)) {
    setAnonymousDeclForMerging(Reader, New->getLexicalDeclContext(),
                               AnonymousDeclNumber, New);
  } else if (DC->isTranslationUnit() &&
             !Reader.getContext().getLangOpts().CPlusPlus) {
    if (Reader.getIdResolver().tryAddTopLevelDecl(New, Name))
      Reader.PendingFakeLookupResults[Name.getAsIdentifierInfo()]
            .push_back(New);
  } else if (DeclContext *MergeDC = getPrimaryContextForMerging(Reader, DC)) {
    // Add the declaration to its redeclaration context so later merging
    // lookups will find it.
    MergeDC->makeDeclVisibleInContextImpl(New, /*Internal*/true);
  }
}

/// Find the declaration that should be merged into, given the declaration found
/// by name lookup. If we're merging an anonymous declaration within a typedef,
/// we need a matching typedef, and we merge with the type inside it.
static NamedDecl *getDeclForMerging(NamedDecl *Found,
                                    bool IsTypedefNameForLinkage) {
  if (!IsTypedefNameForLinkage)
    return Found;

  // If we found a typedef declaration that gives a name to some other
  // declaration, then we want that inner declaration. Declarations from
  // AST files are handled via ImportedTypedefNamesForLinkage.
  if (Found->isFromASTFile())
    return nullptr;

  if (auto *TND = dyn_cast<TypedefNameDecl>(Found))
    return TND->getAnonDeclWithTypedefName(/*AnyRedecl*/true);

  return nullptr;
}

/// Find the declaration to use to populate the anonymous declaration table
/// for the given lexical DeclContext. We only care about finding local
/// definitions of the context; we'll merge imported ones as we go.
DeclContext *
ASTDeclReader::getPrimaryDCForAnonymousDecl(DeclContext *LexicalDC) {
  // For classes, we track the definition as we merge.
  if (auto *RD = dyn_cast<CXXRecordDecl>(LexicalDC)) {
    auto *DD = RD->getCanonicalDecl()->DefinitionData;
    return DD ? DD->Definition : nullptr;
  } else if (auto *OID = dyn_cast<ObjCInterfaceDecl>(LexicalDC)) {
    return OID->getCanonicalDecl()->getDefinition();
  }

  // For anything else, walk its merged redeclarations looking for a definition.
  // Note that we can't just call getDefinition here because the redeclaration
  // chain isn't wired up.
  for (auto *D : merged_redecls(cast<Decl>(LexicalDC))) {
    if (auto *FD = dyn_cast<FunctionDecl>(D))
      if (FD->isThisDeclarationADefinition())
        return FD;
    if (auto *MD = dyn_cast<ObjCMethodDecl>(D))
      if (MD->isThisDeclarationADefinition())
        return MD;
    if (auto *RD = dyn_cast<RecordDecl>(D))
      if (RD->isThisDeclarationADefinition())
        return RD;
  }

  // No merged definition yet.
  return nullptr;
}

NamedDecl *ASTDeclReader::getAnonymousDeclForMerging(ASTReader &Reader,
                                                     DeclContext *DC,
                                                     unsigned Index) {
  // If the lexical context has been merged, look into the now-canonical
  // definition.
  auto *CanonDC = cast<Decl>(DC)->getCanonicalDecl();

  // If we've seen this before, return the canonical declaration.
  auto &Previous = Reader.AnonymousDeclarationsForMerging[CanonDC];
  if (Index < Previous.size() && Previous[Index])
    return Previous[Index];

  // If this is the first time, but we have parsed a declaration of the context,
  // build the anonymous declaration list from the parsed declaration.
  auto *PrimaryDC = getPrimaryDCForAnonymousDecl(DC);
  if (PrimaryDC && !cast<Decl>(PrimaryDC)->isFromASTFile()) {
    numberAnonymousDeclsWithin(PrimaryDC, [&](NamedDecl *ND, unsigned Number) {
      if (Previous.size() == Number)
        Previous.push_back(cast<NamedDecl>(ND->getCanonicalDecl()));
      else
        Previous[Number] = cast<NamedDecl>(ND->getCanonicalDecl());
    });
  }

  return Index < Previous.size() ? Previous[Index] : nullptr;
}

void ASTDeclReader::setAnonymousDeclForMerging(ASTReader &Reader,
                                               DeclContext *DC, unsigned Index,
                                               NamedDecl *D) {
  auto *CanonDC = cast<Decl>(DC)->getCanonicalDecl();

  auto &Previous = Reader.AnonymousDeclarationsForMerging[CanonDC];
  if (Index >= Previous.size())
    Previous.resize(Index + 1);
  if (!Previous[Index])
    Previous[Index] = D;
}

ASTDeclReader::FindExistingResult ASTDeclReader::findExisting(NamedDecl *D) {
  DeclarationName Name = TypedefNameForLinkage ? TypedefNameForLinkage
                                               : D->getDeclName();

  if (!Name && !needsAnonymousDeclarationNumber(D)) {
    // Don't bother trying to find unnamed declarations that are in
    // unmergeable contexts.
    FindExistingResult Result(Reader, D, /*Existing=*/nullptr,
                              AnonymousDeclNumber, TypedefNameForLinkage);
    Result.suppress();
    return Result;
  }

  ASTContext &C = Reader.getContext();
  DeclContext *DC = D->getDeclContext()->getRedeclContext();
  if (TypedefNameForLinkage) {
    auto It = Reader.ImportedTypedefNamesForLinkage.find(
        std::make_pair(DC, TypedefNameForLinkage));
    if (It != Reader.ImportedTypedefNamesForLinkage.end())
      if (C.isSameEntity(It->second, D))
        return FindExistingResult(Reader, D, It->second, AnonymousDeclNumber,
                                  TypedefNameForLinkage);
    // Go on to check in other places in case an existing typedef name
    // was not imported.
  }

  if (needsAnonymousDeclarationNumber(D)) {
    // This is an anonymous declaration that we may need to merge. Look it up
    // in its context by number.
    if (auto *Existing = getAnonymousDeclForMerging(
            Reader, D->getLexicalDeclContext(), AnonymousDeclNumber))
      if (C.isSameEntity(Existing, D))
        return FindExistingResult(Reader, D, Existing, AnonymousDeclNumber,
                                  TypedefNameForLinkage);
  } else if (DC->isTranslationUnit() &&
             !Reader.getContext().getLangOpts().CPlusPlus) {
    IdentifierResolver &IdResolver = Reader.getIdResolver();

    // Temporarily consider the identifier to be up-to-date. We don't want to
    // cause additional lookups here.
    class UpToDateIdentifierRAII {
      IdentifierInfo *II;
      bool WasOutToDate = false;

    public:
      explicit UpToDateIdentifierRAII(IdentifierInfo *II) : II(II) {
        if (II) {
          WasOutToDate = II->isOutOfDate();
          if (WasOutToDate)
            II->setOutOfDate(false);
        }
      }

      ~UpToDateIdentifierRAII() {
        if (WasOutToDate)
          II->setOutOfDate(true);
      }
    } UpToDate(Name.getAsIdentifierInfo());

    for (IdentifierResolver::iterator I = IdResolver.begin(Name),
                                   IEnd = IdResolver.end();
         I != IEnd; ++I) {
      if (NamedDecl *Existing = getDeclForMerging(*I, TypedefNameForLinkage))
        if (C.isSameEntity(Existing, D))
          return FindExistingResult(Reader, D, Existing, AnonymousDeclNumber,
                                    TypedefNameForLinkage);
    }
  } else if (DeclContext *MergeDC = getPrimaryContextForMerging(Reader, DC)) {
    DeclContext::lookup_result R = MergeDC->noload_lookup(Name);
    for (DeclContext::lookup_iterator I = R.begin(), E = R.end(); I != E; ++I) {
      if (NamedDecl *Existing = getDeclForMerging(*I, TypedefNameForLinkage))
        if (C.isSameEntity(Existing, D))
          return FindExistingResult(Reader, D, Existing, AnonymousDeclNumber,
                                    TypedefNameForLinkage);
    }
  } else {
    // Not in a mergeable context.
    return FindExistingResult(Reader);
  }

  // If this declaration is from a merged context, make a note that we need to
  // check that the canonical definition of that context contains the decl.
  //
  // Note that we don't perform ODR checks for decls from the global module
  // fragment.
  //
  // FIXME: We should do something similar if we merge two definitions of the
  // same template specialization into the same CXXRecordDecl.
  auto MergedDCIt = Reader.MergedDeclContexts.find(D->getLexicalDeclContext());
  if (MergedDCIt != Reader.MergedDeclContexts.end() &&
      !shouldSkipCheckingODR(D) && MergedDCIt->second == D->getDeclContext())
    Reader.PendingOdrMergeChecks.push_back(D);

  return FindExistingResult(Reader, D, /*Existing=*/nullptr,
                            AnonymousDeclNumber, TypedefNameForLinkage);
}

template<typename DeclT>
Decl *ASTDeclReader::getMostRecentDeclImpl(Redeclarable<DeclT> *D) {
  return D->RedeclLink.getLatestNotUpdated();
}

Decl *ASTDeclReader::getMostRecentDeclImpl(...) {
  llvm_unreachable("getMostRecentDecl on non-redeclarable declaration");
}

Decl *ASTDeclReader::getMostRecentDecl(Decl *D) {
  assert(D);

  switch (D->getKind()) {
#define ABSTRACT_DECL(TYPE)
#define DECL(TYPE, BASE)                               \
  case Decl::TYPE:                                     \
    return getMostRecentDeclImpl(cast<TYPE##Decl>(D));
#include "clang/AST/DeclNodes.inc"
  }
  llvm_unreachable("unknown decl kind");
}

Decl *ASTReader::getMostRecentExistingDecl(Decl *D) {
  return ASTDeclReader::getMostRecentDecl(D->getCanonicalDecl());
}

void ASTDeclReader::mergeInheritableAttributes(ASTReader &Reader, Decl *D,
                                               Decl *Previous) {
  InheritableAttr *NewAttr = nullptr;
  ASTContext &Context = Reader.getContext();
  const auto *IA = Previous->getAttr<MSInheritanceAttr>();

  if (IA && !D->hasAttr<MSInheritanceAttr>()) {
    NewAttr = cast<InheritableAttr>(IA->clone(Context));
    NewAttr->setInherited(true);
    D->addAttr(NewAttr);
  }

  const auto *AA = Previous->getAttr<AvailabilityAttr>();
  if (AA && !D->hasAttr<AvailabilityAttr>()) {
    NewAttr = AA->clone(Context);
    NewAttr->setInherited(true);
    D->addAttr(NewAttr);
  }
}

template<typename DeclT>
void ASTDeclReader::attachPreviousDeclImpl(ASTReader &Reader,
                                           Redeclarable<DeclT> *D,
                                           Decl *Previous, Decl *Canon) {
  D->RedeclLink.setPrevious(cast<DeclT>(Previous));
  D->First = cast<DeclT>(Previous)->First;
}

namespace clang {

template<>
void ASTDeclReader::attachPreviousDeclImpl(ASTReader &Reader,
                                           Redeclarable<VarDecl> *D,
                                           Decl *Previous, Decl *Canon) {
  auto *VD = static_cast<VarDecl *>(D);
  auto *PrevVD = cast<VarDecl>(Previous);
  D->RedeclLink.setPrevious(PrevVD);
  D->First = PrevVD->First;

  // We should keep at most one definition on the chain.
  // FIXME: Cache the definition once we've found it. Building a chain with
  // N definitions currently takes O(N^2) time here.
  if (VD->isThisDeclarationADefinition() == VarDecl::Definition) {
    for (VarDecl *CurD = PrevVD; CurD; CurD = CurD->getPreviousDecl()) {
      if (CurD->isThisDeclarationADefinition() == VarDecl::Definition) {
        Reader.mergeDefinitionVisibility(CurD, VD);
        VD->demoteThisDefinitionToDeclaration();
        break;
      }
    }
  }
}

static bool isUndeducedReturnType(QualType T) {
  auto *DT = T->getContainedDeducedType();
  return DT && !DT->isDeduced();
}

template<>
void ASTDeclReader::attachPreviousDeclImpl(ASTReader &Reader,
                                           Redeclarable<FunctionDecl> *D,
                                           Decl *Previous, Decl *Canon) {
  auto *FD = static_cast<FunctionDecl *>(D);
  auto *PrevFD = cast<FunctionDecl>(Previous);

  FD->RedeclLink.setPrevious(PrevFD);
  FD->First = PrevFD->First;

  // If the previous declaration is an inline function declaration, then this
  // declaration is too.
  if (PrevFD->isInlined() != FD->isInlined()) {
    // FIXME: [dcl.fct.spec]p4:
    //   If a function with external linkage is declared inline in one
    //   translation unit, it shall be declared inline in all translation
    //   units in which it appears.
    //
    // Be careful of this case:
    //
    // module A:
    //   template<typename T> struct X { void f(); };
    //   template<typename T> inline void X<T>::f() {}
    //
    // module B instantiates the declaration of X<int>::f
    // module C instantiates the definition of X<int>::f
    //
    // If module B and C are merged, we do not have a violation of this rule.
    FD->setImplicitlyInline(true);
  }

  auto *FPT = FD->getType()->getAs<FunctionProtoType>();
  auto *PrevFPT = PrevFD->getType()->getAs<FunctionProtoType>();
  if (FPT && PrevFPT) {
    // If we need to propagate an exception specification along the redecl
    // chain, make a note of that so that we can do so later.
    bool IsUnresolved = isUnresolvedExceptionSpec(FPT->getExceptionSpecType());
    bool WasUnresolved =
        isUnresolvedExceptionSpec(PrevFPT->getExceptionSpecType());
    if (IsUnresolved != WasUnresolved)
      Reader.PendingExceptionSpecUpdates.insert(
          {Canon, IsUnresolved ? PrevFD : FD});

    // If we need to propagate a deduced return type along the redecl chain,
    // make a note of that so that we can do it later.
    bool IsUndeduced = isUndeducedReturnType(FPT->getReturnType());
    bool WasUndeduced = isUndeducedReturnType(PrevFPT->getReturnType());
    if (IsUndeduced != WasUndeduced)
      Reader.PendingDeducedTypeUpdates.insert(
          {cast<FunctionDecl>(Canon),
           (IsUndeduced ? PrevFPT : FPT)->getReturnType()});
  }
}

} // namespace clang

void ASTDeclReader::attachPreviousDeclImpl(ASTReader &Reader, ...) {
  llvm_unreachable("attachPreviousDecl on non-redeclarable declaration");
}

/// Inherit the default template argument from \p From to \p To. Returns
/// \c false if there is no default template for \p From.
template <typename ParmDecl>
static bool inheritDefaultTemplateArgument(ASTContext &Context, ParmDecl *From,
                                           Decl *ToD) {
  auto *To = cast<ParmDecl>(ToD);
  if (!From->hasDefaultArgument())
    return false;
  To->setInheritedDefaultArgument(Context, From);
  return true;
}

static void inheritDefaultTemplateArguments(ASTContext &Context,
                                            TemplateDecl *From,
                                            TemplateDecl *To) {
  auto *FromTP = From->getTemplateParameters();
  auto *ToTP = To->getTemplateParameters();
  assert(FromTP->size() == ToTP->size() && "merged mismatched templates?");

  for (unsigned I = 0, N = FromTP->size(); I != N; ++I) {
    NamedDecl *FromParam = FromTP->getParam(I);
    NamedDecl *ToParam = ToTP->getParam(I);

    if (auto *FTTP = dyn_cast<TemplateTypeParmDecl>(FromParam))
      inheritDefaultTemplateArgument(Context, FTTP, ToParam);
    else if (auto *FNTTP = dyn_cast<NonTypeTemplateParmDecl>(FromParam))
      inheritDefaultTemplateArgument(Context, FNTTP, ToParam);
    else
      inheritDefaultTemplateArgument(
              Context, cast<TemplateTemplateParmDecl>(FromParam), ToParam);
  }
}

// [basic.link]/p10:
//    If two declarations of an entity are attached to different modules,
//    the program is ill-formed;
static void checkMultipleDefinitionInNamedModules(ASTReader &Reader, Decl *D,
                                                  Decl *Previous) {
  Module *M = Previous->getOwningModule();

  // We only care about the case in named modules.
  if (!M || !M->isNamedModule())
    return;

  // If it is previous implcitly introduced, it is not meaningful to
  // diagnose it.
  if (Previous->isImplicit())
    return;

  // FIXME: Get rid of the enumeration of decl types once we have an appropriate
  // abstract for decls of an entity. e.g., the namespace decl and using decl
  // doesn't introduce an entity.
  if (!isa<VarDecl, FunctionDecl, TagDecl, RedeclarableTemplateDecl>(Previous))
    return;

  // Skip implicit instantiations since it may give false positive diagnostic
  // messages.
  // FIXME: Maybe this shows the implicit instantiations may have incorrect
  // module owner ships. But given we've finished the compilation of a module,
  // how can we add new entities to that module?
  if (auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(Previous);
      VTSD && !VTSD->isExplicitSpecialization())
    return;
  if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(Previous);
      CTSD && !CTSD->isExplicitSpecialization())
    return;
  if (auto *Func = dyn_cast<FunctionDecl>(Previous))
    if (auto *FTSI = Func->getTemplateSpecializationInfo();
        FTSI && !FTSI->isExplicitSpecialization())
      return;

  // It is fine if they are in the same module.
  if (Reader.getContext().isInSameModule(M, D->getOwningModule()))
    return;

  Reader.Diag(Previous->getLocation(),
              diag::err_multiple_decl_in_different_modules)
      << cast<NamedDecl>(Previous) << M->Name;
  Reader.Diag(D->getLocation(), diag::note_also_found);
}

void ASTDeclReader::attachPreviousDecl(ASTReader &Reader, Decl *D,
                                       Decl *Previous, Decl *Canon) {
  assert(D && Previous);

  switch (D->getKind()) {
#define ABSTRACT_DECL(TYPE)
#define DECL(TYPE, BASE)                                                  \
  case Decl::TYPE:                                                        \
    attachPreviousDeclImpl(Reader, cast<TYPE##Decl>(D), Previous, Canon); \
    break;
#include "clang/AST/DeclNodes.inc"
  }

  checkMultipleDefinitionInNamedModules(Reader, D, Previous);

  // If the declaration was visible in one module, a redeclaration of it in
  // another module remains visible even if it wouldn't be visible by itself.
  //
  // FIXME: In this case, the declaration should only be visible if a module
  //        that makes it visible has been imported.
  D->IdentifierNamespace |=
      Previous->IdentifierNamespace &
      (Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Type);

  // If the declaration declares a template, it may inherit default arguments
  // from the previous declaration.
  if (auto *TD = dyn_cast<TemplateDecl>(D))
    inheritDefaultTemplateArguments(Reader.getContext(),
                                    cast<TemplateDecl>(Previous), TD);

  // If any of the declaration in the chain contains an Inheritable attribute,
  // it needs to be added to all the declarations in the redeclarable chain.
  // FIXME: Only the logic of merging MSInheritableAttr is present, it should
  // be extended for all inheritable attributes.
  mergeInheritableAttributes(Reader, D, Previous);
}

template<typename DeclT>
void ASTDeclReader::attachLatestDeclImpl(Redeclarable<DeclT> *D, Decl *Latest) {
  D->RedeclLink.setLatest(cast<DeclT>(Latest));
}

void ASTDeclReader::attachLatestDeclImpl(...) {
  llvm_unreachable("attachLatestDecl on non-redeclarable declaration");
}

void ASTDeclReader::attachLatestDecl(Decl *D, Decl *Latest) {
  assert(D && Latest);

  switch (D->getKind()) {
#define ABSTRACT_DECL(TYPE)
#define DECL(TYPE, BASE)                                  \
  case Decl::TYPE:                                        \
    attachLatestDeclImpl(cast<TYPE##Decl>(D), Latest); \
    break;
#include "clang/AST/DeclNodes.inc"
  }
}

template<typename DeclT>
void ASTDeclReader::markIncompleteDeclChainImpl(Redeclarable<DeclT> *D) {
  D->RedeclLink.markIncomplete();
}

void ASTDeclReader::markIncompleteDeclChainImpl(...) {
  llvm_unreachable("markIncompleteDeclChain on non-redeclarable declaration");
}

void ASTReader::markIncompleteDeclChain(Decl *D) {
  switch (D->getKind()) {
#define ABSTRACT_DECL(TYPE)
#define DECL(TYPE, BASE)                                             \
  case Decl::TYPE:                                                   \
    ASTDeclReader::markIncompleteDeclChainImpl(cast<TYPE##Decl>(D)); \
    break;
#include "clang/AST/DeclNodes.inc"
  }
}

/// Read the declaration at the given offset from the AST file.
Decl *ASTReader::ReadDeclRecord(GlobalDeclID ID) {
  SourceLocation DeclLoc;
  RecordLocation Loc = DeclCursorForID(ID, DeclLoc);
  llvm::BitstreamCursor &DeclsCursor = Loc.F->DeclsCursor;
  // Keep track of where we are in the stream, then jump back there
  // after reading this declaration.
  SavedStreamPosition SavedPosition(DeclsCursor);

  ReadingKindTracker ReadingKind(Read_Decl, *this);

  // Note that we are loading a declaration record.
  Deserializing ADecl(this);

  auto Fail = [](const char *what, llvm::Error &&Err) {
    llvm::report_fatal_error(Twine("ASTReader::readDeclRecord failed ") + what +
                             ": " + toString(std::move(Err)));
  };

  if (llvm::Error JumpFailed = DeclsCursor.JumpToBit(Loc.Offset))
    Fail("jumping", std::move(JumpFailed));
  ASTRecordReader Record(*this, *Loc.F);
  ASTDeclReader Reader(*this, Record, Loc, ID, DeclLoc);
  Expected<unsigned> MaybeCode = DeclsCursor.ReadCode();
  if (!MaybeCode)
    Fail("reading code", MaybeCode.takeError());
  unsigned Code = MaybeCode.get();

  ASTContext &Context = getContext();
  Decl *D = nullptr;
  Expected<unsigned> MaybeDeclCode = Record.readRecord(DeclsCursor, Code);
  if (!MaybeDeclCode)
    llvm::report_fatal_error(
        Twine("ASTReader::readDeclRecord failed reading decl code: ") +
        toString(MaybeDeclCode.takeError()));

  switch ((DeclCode)MaybeDeclCode.get()) {
  case DECL_CONTEXT_LEXICAL:
  case DECL_CONTEXT_VISIBLE:
    llvm_unreachable("Record cannot be de-serialized with readDeclRecord");
  case DECL_TYPEDEF:
    D = TypedefDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_TYPEALIAS:
    D = TypeAliasDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_ENUM:
    D = EnumDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_RECORD:
    D = RecordDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_ENUM_CONSTANT:
    D = EnumConstantDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_FUNCTION:
    D = FunctionDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_LINKAGE_SPEC:
    D = LinkageSpecDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_EXPORT:
    D = ExportDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_LABEL:
    D = LabelDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_NAMESPACE:
    D = NamespaceDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_NAMESPACE_ALIAS:
    D = NamespaceAliasDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_USING:
    D = UsingDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_USING_PACK:
    D = UsingPackDecl::CreateDeserialized(Context, ID, Record.readInt());
    break;
  case DECL_USING_SHADOW:
    D = UsingShadowDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_USING_ENUM:
    D = UsingEnumDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CONSTRUCTOR_USING_SHADOW:
    D = ConstructorUsingShadowDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_USING_DIRECTIVE:
    D = UsingDirectiveDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_UNRESOLVED_USING_VALUE:
    D = UnresolvedUsingValueDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_UNRESOLVED_USING_TYPENAME:
    D = UnresolvedUsingTypenameDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_UNRESOLVED_USING_IF_EXISTS:
    D = UnresolvedUsingIfExistsDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CXX_RECORD:
    D = CXXRecordDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CXX_DEDUCTION_GUIDE:
    D = CXXDeductionGuideDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CXX_METHOD:
    D = CXXMethodDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CXX_CONSTRUCTOR:
    D = CXXConstructorDecl::CreateDeserialized(Context, ID, Record.readInt());
    break;
  case DECL_CXX_DESTRUCTOR:
    D = CXXDestructorDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CXX_CONVERSION:
    D = CXXConversionDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_ACCESS_SPEC:
    D = AccessSpecDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_FRIEND:
    D = FriendDecl::CreateDeserialized(Context, ID, Record.readInt());
    break;
  case DECL_FRIEND_TEMPLATE:
    D = FriendTemplateDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CLASS_TEMPLATE:
    D = ClassTemplateDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CLASS_TEMPLATE_SPECIALIZATION:
    D = ClassTemplateSpecializationDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CLASS_TEMPLATE_PARTIAL_SPECIALIZATION:
    D = ClassTemplatePartialSpecializationDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_VAR_TEMPLATE:
    D = VarTemplateDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_VAR_TEMPLATE_SPECIALIZATION:
    D = VarTemplateSpecializationDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_VAR_TEMPLATE_PARTIAL_SPECIALIZATION:
    D = VarTemplatePartialSpecializationDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_FUNCTION_TEMPLATE:
    D = FunctionTemplateDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_TEMPLATE_TYPE_PARM: {
    bool HasTypeConstraint = Record.readInt();
    D = TemplateTypeParmDecl::CreateDeserialized(Context, ID,
                                                 HasTypeConstraint);
    break;
  }
  case DECL_NON_TYPE_TEMPLATE_PARM: {
    bool HasTypeConstraint = Record.readInt();
    D = NonTypeTemplateParmDecl::CreateDeserialized(Context, ID,
                                                    HasTypeConstraint);
    break;
  }
  case DECL_EXPANDED_NON_TYPE_TEMPLATE_PARM_PACK: {
    bool HasTypeConstraint = Record.readInt();
    D = NonTypeTemplateParmDecl::CreateDeserialized(
        Context, ID, Record.readInt(), HasTypeConstraint);
    break;
  }
  case DECL_TEMPLATE_TEMPLATE_PARM:
    D = TemplateTemplateParmDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_EXPANDED_TEMPLATE_TEMPLATE_PARM_PACK:
    D = TemplateTemplateParmDecl::CreateDeserialized(Context, ID,
                                                     Record.readInt());
    break;
  case DECL_TYPE_ALIAS_TEMPLATE:
    D = TypeAliasTemplateDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CONCEPT:
    D = ConceptDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_REQUIRES_EXPR_BODY:
    D = RequiresExprBodyDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_STATIC_ASSERT:
    D = StaticAssertDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_METHOD:
    D = ObjCMethodDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_INTERFACE:
    D = ObjCInterfaceDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_IVAR:
    D = ObjCIvarDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_PROTOCOL:
    D = ObjCProtocolDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_AT_DEFS_FIELD:
    D = ObjCAtDefsFieldDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_CATEGORY:
    D = ObjCCategoryDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_CATEGORY_IMPL:
    D = ObjCCategoryImplDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_IMPLEMENTATION:
    D = ObjCImplementationDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_COMPATIBLE_ALIAS:
    D = ObjCCompatibleAliasDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_PROPERTY:
    D = ObjCPropertyDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_PROPERTY_IMPL:
    D = ObjCPropertyImplDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_FIELD:
    D = FieldDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_INDIRECTFIELD:
    D = IndirectFieldDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_VAR:
    D = VarDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_IMPLICIT_PARAM:
    D = ImplicitParamDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_PARM_VAR:
    D = ParmVarDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_DECOMPOSITION:
    D = DecompositionDecl::CreateDeserialized(Context, ID, Record.readInt());
    break;
  case DECL_BINDING:
    D = BindingDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_FILE_SCOPE_ASM:
    D = FileScopeAsmDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_TOP_LEVEL_STMT_DECL:
    D = TopLevelStmtDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_BLOCK:
    D = BlockDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_MS_PROPERTY:
    D = MSPropertyDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_MS_GUID:
    D = MSGuidDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_UNNAMED_GLOBAL_CONSTANT:
    D = UnnamedGlobalConstantDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_TEMPLATE_PARAM_OBJECT:
    D = TemplateParamObjectDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_CAPTURED:
    D = CapturedDecl::CreateDeserialized(Context, ID, Record.readInt());
    break;
  case DECL_CXX_BASE_SPECIFIERS:
    Error("attempt to read a C++ base-specifier record as a declaration");
    return nullptr;
  case DECL_CXX_CTOR_INITIALIZERS:
    Error("attempt to read a C++ ctor initializer record as a declaration");
    return nullptr;
  case DECL_IMPORT:
    // Note: last entry of the ImportDecl record is the number of stored source
    // locations.
    D = ImportDecl::CreateDeserialized(Context, ID, Record.back());
    break;
  case DECL_OMP_THREADPRIVATE: {
    Record.skipInts(1);
    unsigned NumChildren = Record.readInt();
    Record.skipInts(1);
    D = OMPThreadPrivateDecl::CreateDeserialized(Context, ID, NumChildren);
    break;
  }
  case DECL_OMP_ALLOCATE: {
    unsigned NumClauses = Record.readInt();
    unsigned NumVars = Record.readInt();
    Record.skipInts(1);
    D = OMPAllocateDecl::CreateDeserialized(Context, ID, NumVars, NumClauses);
    break;
  }
  case DECL_OMP_REQUIRES: {
    unsigned NumClauses = Record.readInt();
    Record.skipInts(2);
    D = OMPRequiresDecl::CreateDeserialized(Context, ID, NumClauses);
    break;
  }
  case DECL_OMP_DECLARE_REDUCTION:
    D = OMPDeclareReductionDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OMP_DECLARE_MAPPER: {
    unsigned NumClauses = Record.readInt();
    Record.skipInts(2);
    D = OMPDeclareMapperDecl::CreateDeserialized(Context, ID, NumClauses);
    break;
  }
  case DECL_OMP_CAPTUREDEXPR:
    D = OMPCapturedExprDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_PRAGMA_COMMENT:
    D = PragmaCommentDecl::CreateDeserialized(Context, ID, Record.readInt());
    break;
  case DECL_PRAGMA_DETECT_MISMATCH:
    D = PragmaDetectMismatchDecl::CreateDeserialized(Context, ID,
                                                     Record.readInt());
    break;
  case DECL_EMPTY:
    D = EmptyDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_LIFETIME_EXTENDED_TEMPORARY:
    D = LifetimeExtendedTemporaryDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_OBJC_TYPE_PARAM:
    D = ObjCTypeParamDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_HLSL_BUFFER:
    D = HLSLBufferDecl::CreateDeserialized(Context, ID);
    break;
  case DECL_IMPLICIT_CONCEPT_SPECIALIZATION:
    D = ImplicitConceptSpecializationDecl::CreateDeserialized(Context, ID,
                                                              Record.readInt());
    break;
  }

  assert(D && "Unknown declaration reading AST file");
  LoadedDecl(translateGlobalDeclIDToIndex(ID), D);
  // Set the DeclContext before doing any deserialization, to make sure internal
  // calls to Decl::getASTContext() by Decl's methods will find the
  // TranslationUnitDecl without crashing.
  D->setDeclContext(Context.getTranslationUnitDecl());

  // Reading some declarations can result in deep recursion.
  clang::runWithSufficientStackSpace([&] { warnStackExhausted(DeclLoc); },
                                     [&] { Reader.Visit(D); });

  // If this declaration is also a declaration context, get the
  // offsets for its tables of lexical and visible declarations.
  if (auto *DC = dyn_cast<DeclContext>(D)) {
    std::pair<uint64_t, uint64_t> Offsets = Reader.VisitDeclContext(DC);

    // Get the lexical and visible block for the delayed namespace.
    // It is sufficient to judge if ID is in DelayedNamespaceOffsetMap.
    // But it may be more efficient to filter the other cases.
    if (!Offsets.first && !Offsets.second && isa<NamespaceDecl>(D))
      if (auto Iter = DelayedNamespaceOffsetMap.find(ID);
          Iter != DelayedNamespaceOffsetMap.end())
        Offsets = Iter->second;

    if (Offsets.first &&
        ReadLexicalDeclContextStorage(*Loc.F, DeclsCursor, Offsets.first, DC))
      return nullptr;
    if (Offsets.second &&
        ReadVisibleDeclContextStorage(*Loc.F, DeclsCursor, Offsets.second, ID))
      return nullptr;
  }
  assert(Record.getIdx() == Record.size());

  // Load any relevant update records.
  PendingUpdateRecords.push_back(
      PendingUpdateRecord(ID, D, /*JustLoaded=*/true));

  // Load the categories after recursive loading is finished.
  if (auto *Class = dyn_cast<ObjCInterfaceDecl>(D))
    // If we already have a definition when deserializing the ObjCInterfaceDecl,
    // we put the Decl in PendingDefinitions so we can pull the categories here.
    if (Class->isThisDeclarationADefinition() ||
        PendingDefinitions.count(Class))
      loadObjCCategories(ID, Class);

  // If we have deserialized a declaration that has a definition the
  // AST consumer might need to know about, queue it.
  // We don't pass it to the consumer immediately because we may be in recursive
  // loading, and some declarations may still be initializing.
  PotentiallyInterestingDecls.push_back(D);

  return D;
}

void ASTReader::PassInterestingDeclsToConsumer() {
  assert(Consumer);

  if (PassingDeclsToConsumer)
    return;

  // Guard variable to avoid recursively redoing the process of passing
  // decls to consumer.
  SaveAndRestore GuardPassingDeclsToConsumer(PassingDeclsToConsumer, true);

  // Ensure that we've loaded all potentially-interesting declarations
  // that need to be eagerly loaded.
  for (auto ID : EagerlyDeserializedDecls)
    GetDecl(ID);
  EagerlyDeserializedDecls.clear();

  auto ConsumingPotentialInterestingDecls = [this]() {
    while (!PotentiallyInterestingDecls.empty()) {
      Decl *D = PotentiallyInterestingDecls.front();
      PotentiallyInterestingDecls.pop_front();
      if (isConsumerInterestedIn(D))
        PassInterestingDeclToConsumer(D);
    }
  };
  std::deque<Decl *> MaybeInterestingDecls =
      std::move(PotentiallyInterestingDecls);
  PotentiallyInterestingDecls.clear();
  assert(PotentiallyInterestingDecls.empty());
  while (!MaybeInterestingDecls.empty()) {
    Decl *D = MaybeInterestingDecls.front();
    MaybeInterestingDecls.pop_front();
    // Since we load the variable's initializers lazily, it'd be problematic
    // if the initializers dependent on each other. So here we try to load the
    // initializers of static variables to make sure they are passed to code
    // generator by order. If we read anything interesting, we would consume
    // that before emitting the current declaration.
    if (auto *VD = dyn_cast<VarDecl>(D);
        VD && VD->isFileVarDecl() && !VD->isExternallyVisible())
      VD->getInit();
    ConsumingPotentialInterestingDecls();
    if (isConsumerInterestedIn(D))
      PassInterestingDeclToConsumer(D);
  }

  // If we add any new potential interesting decl in the last call, consume it.
  ConsumingPotentialInterestingDecls();

  for (GlobalDeclID ID : VTablesToEmit) {
    auto *RD = cast<CXXRecordDecl>(GetDecl(ID));
    assert(!RD->shouldEmitInExternalSource());
    PassVTableToConsumer(RD);
  }
  VTablesToEmit.clear();
}

void ASTReader::loadDeclUpdateRecords(PendingUpdateRecord &Record) {
  // The declaration may have been modified by files later in the chain.
  // If this is the case, read the record containing the updates from each file
  // and pass it to ASTDeclReader to make the modifications.
  GlobalDeclID ID = Record.ID;
  Decl *D = Record.D;
  ProcessingUpdatesRAIIObj ProcessingUpdates(*this);
  DeclUpdateOffsetsMap::iterator UpdI = DeclUpdateOffsets.find(ID);

  SmallVector<GlobalDeclID, 8> PendingLazySpecializationIDs;

  if (UpdI != DeclUpdateOffsets.end()) {
    auto UpdateOffsets = std::move(UpdI->second);
    DeclUpdateOffsets.erase(UpdI);

    // Check if this decl was interesting to the consumer. If we just loaded
    // the declaration, then we know it was interesting and we skip the call
    // to isConsumerInterestedIn because it is unsafe to call in the
    // current ASTReader state.
    bool WasInteresting = Record.JustLoaded || isConsumerInterestedIn(D);
    for (auto &FileAndOffset : UpdateOffsets) {
      ModuleFile *F = FileAndOffset.first;
      uint64_t Offset = FileAndOffset.second;
      llvm::BitstreamCursor &Cursor = F->DeclsCursor;
      SavedStreamPosition SavedPosition(Cursor);
      if (llvm::Error JumpFailed = Cursor.JumpToBit(Offset))
        // FIXME don't do a fatal error.
        llvm::report_fatal_error(
            Twine("ASTReader::loadDeclUpdateRecords failed jumping: ") +
            toString(std::move(JumpFailed)));
      Expected<unsigned> MaybeCode = Cursor.ReadCode();
      if (!MaybeCode)
        llvm::report_fatal_error(
            Twine("ASTReader::loadDeclUpdateRecords failed reading code: ") +
            toString(MaybeCode.takeError()));
      unsigned Code = MaybeCode.get();
      ASTRecordReader Record(*this, *F);
      if (Expected<unsigned> MaybeRecCode = Record.readRecord(Cursor, Code))
        assert(MaybeRecCode.get() == DECL_UPDATES &&
               "Expected DECL_UPDATES record!");
      else
        llvm::report_fatal_error(
            Twine("ASTReader::loadDeclUpdateRecords failed reading rec code: ") +
            toString(MaybeCode.takeError()));

      ASTDeclReader Reader(*this, Record, RecordLocation(F, Offset), ID,
                           SourceLocation());
      Reader.UpdateDecl(D, PendingLazySpecializationIDs);

      // We might have made this declaration interesting. If so, remember that
      // we need to hand it off to the consumer.
      if (!WasInteresting && isConsumerInterestedIn(D)) {
        PotentiallyInterestingDecls.push_back(D);
        WasInteresting = true;
      }
    }
  }
  // Add the lazy specializations to the template.
  assert((PendingLazySpecializationIDs.empty() || isa<ClassTemplateDecl>(D) ||
          isa<FunctionTemplateDecl, VarTemplateDecl>(D)) &&
         "Must not have pending specializations");
  if (auto *CTD = dyn_cast<ClassTemplateDecl>(D))
    ASTDeclReader::AddLazySpecializations(CTD, PendingLazySpecializationIDs);
  else if (auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
    ASTDeclReader::AddLazySpecializations(FTD, PendingLazySpecializationIDs);
  else if (auto *VTD = dyn_cast<VarTemplateDecl>(D))
    ASTDeclReader::AddLazySpecializations(VTD, PendingLazySpecializationIDs);
  PendingLazySpecializationIDs.clear();

  // Load the pending visible updates for this decl context, if it has any.
  auto I = PendingVisibleUpdates.find(ID);
  if (I != PendingVisibleUpdates.end()) {
    auto VisibleUpdates = std::move(I->second);
    PendingVisibleUpdates.erase(I);

    auto *DC = cast<DeclContext>(D)->getPrimaryContext();
    for (const auto &Update : VisibleUpdates)
      Lookups[DC].Table.add(
          Update.Mod, Update.Data,
          reader::ASTDeclContextNameLookupTrait(*this, *Update.Mod));
    DC->setHasExternalVisibleStorage(true);
  }
}

void ASTReader::loadPendingDeclChain(Decl *FirstLocal, uint64_t LocalOffset) {
  // Attach FirstLocal to the end of the decl chain.
  Decl *CanonDecl = FirstLocal->getCanonicalDecl();
  if (FirstLocal != CanonDecl) {
    Decl *PrevMostRecent = ASTDeclReader::getMostRecentDecl(CanonDecl);
    ASTDeclReader::attachPreviousDecl(
        *this, FirstLocal, PrevMostRecent ? PrevMostRecent : CanonDecl,
        CanonDecl);
  }

  if (!LocalOffset) {
    ASTDeclReader::attachLatestDecl(CanonDecl, FirstLocal);
    return;
  }

  // Load the list of other redeclarations from this module file.
  ModuleFile *M = getOwningModuleFile(FirstLocal);
  assert(M && "imported decl from no module file");

  llvm::BitstreamCursor &Cursor = M->DeclsCursor;
  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error JumpFailed = Cursor.JumpToBit(LocalOffset))
    llvm::report_fatal_error(
        Twine("ASTReader::loadPendingDeclChain failed jumping: ") +
        toString(std::move(JumpFailed)));

  RecordData Record;
  Expected<unsigned> MaybeCode = Cursor.ReadCode();
  if (!MaybeCode)
    llvm::report_fatal_error(
        Twine("ASTReader::loadPendingDeclChain failed reading code: ") +
        toString(MaybeCode.takeError()));
  unsigned Code = MaybeCode.get();
  if (Expected<unsigned> MaybeRecCode = Cursor.readRecord(Code, Record))
    assert(MaybeRecCode.get() == LOCAL_REDECLARATIONS &&
           "expected LOCAL_REDECLARATIONS record!");
  else
    llvm::report_fatal_error(
        Twine("ASTReader::loadPendingDeclChain failed reading rec code: ") +
        toString(MaybeCode.takeError()));

  // FIXME: We have several different dispatches on decl kind here; maybe
  // we should instead generate one loop per kind and dispatch up-front?
  Decl *MostRecent = FirstLocal;
  for (unsigned I = 0, N = Record.size(); I != N; ++I) {
    unsigned Idx = N - I - 1;
    auto *D = ReadDecl(*M, Record, Idx);
    ASTDeclReader::attachPreviousDecl(*this, D, MostRecent, CanonDecl);
    MostRecent = D;
  }
  ASTDeclReader::attachLatestDecl(CanonDecl, MostRecent);
}

namespace {

  /// Given an ObjC interface, goes through the modules and links to the
  /// interface all the categories for it.
  class ObjCCategoriesVisitor {
    ASTReader &Reader;
    ObjCInterfaceDecl *Interface;
    llvm::SmallPtrSetImpl<ObjCCategoryDecl *> &Deserialized;
    ObjCCategoryDecl *Tail = nullptr;
    llvm::DenseMap<DeclarationName, ObjCCategoryDecl *> NameCategoryMap;
    GlobalDeclID InterfaceID;
    unsigned PreviousGeneration;

    void add(ObjCCategoryDecl *Cat) {
      // Only process each category once.
      if (!Deserialized.erase(Cat))
        return;

      // Check for duplicate categories.
      if (Cat->getDeclName()) {
        ObjCCategoryDecl *&Existing = NameCategoryMap[Cat->getDeclName()];
        if (Existing && Reader.getOwningModuleFile(Existing) !=
                            Reader.getOwningModuleFile(Cat)) {
          llvm::DenseSet<std::pair<Decl *, Decl *>> NonEquivalentDecls;
          StructuralEquivalenceContext Ctx(
              Cat->getASTContext(), Existing->getASTContext(),
              NonEquivalentDecls, StructuralEquivalenceKind::Default,
              /*StrictTypeSpelling =*/false,
              /*Complain =*/false,
              /*ErrorOnTagTypeMismatch =*/true);
          if (!Ctx.IsEquivalent(Cat, Existing)) {
            // Warn only if the categories with the same name are different.
            Reader.Diag(Cat->getLocation(), diag::warn_dup_category_def)
                << Interface->getDeclName() << Cat->getDeclName();
            Reader.Diag(Existing->getLocation(),
                        diag::note_previous_definition);
          }
        } else if (!Existing) {
          // Record this category.
          Existing = Cat;
        }
      }

      // Add this category to the end of the chain.
      if (Tail)
        ASTDeclReader::setNextObjCCategory(Tail, Cat);
      else
        Interface->setCategoryListRaw(Cat);
      Tail = Cat;
    }

  public:
    ObjCCategoriesVisitor(
        ASTReader &Reader, ObjCInterfaceDecl *Interface,
        llvm::SmallPtrSetImpl<ObjCCategoryDecl *> &Deserialized,
        GlobalDeclID InterfaceID, unsigned PreviousGeneration)
        : Reader(Reader), Interface(Interface), Deserialized(Deserialized),
          InterfaceID(InterfaceID), PreviousGeneration(PreviousGeneration) {
      // Populate the name -> category map with the set of known categories.
      for (auto *Cat : Interface->known_categories()) {
        if (Cat->getDeclName())
          NameCategoryMap[Cat->getDeclName()] = Cat;

        // Keep track of the tail of the category list.
        Tail = Cat;
      }
    }

    bool operator()(ModuleFile &M) {
      // If we've loaded all of the category information we care about from
      // this module file, we're done.
      if (M.Generation <= PreviousGeneration)
        return true;

      // Map global ID of the definition down to the local ID used in this
      // module file. If there is no such mapping, we'll find nothing here
      // (or in any module it imports).
      LocalDeclID LocalID =
          Reader.mapGlobalIDToModuleFileGlobalID(M, InterfaceID);
      if (LocalID.isInvalid())
        return true;

      // Perform a binary search to find the local redeclarations for this
      // declaration (if any).
      const ObjCCategoriesInfo Compare = { LocalID, 0 };
      const ObjCCategoriesInfo *Result
        = std::lower_bound(M.ObjCCategoriesMap,
                           M.ObjCCategoriesMap + M.LocalNumObjCCategoriesInMap,
                           Compare);
      if (Result == M.ObjCCategoriesMap + M.LocalNumObjCCategoriesInMap ||
          LocalID != Result->getDefinitionID()) {
        // We didn't find anything. If the class definition is in this module
        // file, then the module files it depends on cannot have any categories,
        // so suppress further lookup.
        return Reader.isDeclIDFromModule(InterfaceID, M);
      }

      // We found something. Dig out all of the categories.
      unsigned Offset = Result->Offset;
      unsigned N = M.ObjCCategories[Offset];
      M.ObjCCategories[Offset++] = 0; // Don't try to deserialize again
      for (unsigned I = 0; I != N; ++I)
        add(Reader.ReadDeclAs<ObjCCategoryDecl>(M, M.ObjCCategories, Offset));
      return true;
    }
  };

} // namespace

void ASTReader::loadObjCCategories(GlobalDeclID ID, ObjCInterfaceDecl *D,
                                   unsigned PreviousGeneration) {
  ObjCCategoriesVisitor Visitor(*this, D, CategoriesDeserialized, ID,
                                PreviousGeneration);
  ModuleMgr.visit(Visitor);
}

template<typename DeclT, typename Fn>
static void forAllLaterRedecls(DeclT *D, Fn F) {
  F(D);

  // Check whether we've already merged D into its redeclaration chain.
  // MostRecent may or may not be nullptr if D has not been merged. If
  // not, walk the merged redecl chain and see if it's there.
  auto *MostRecent = D->getMostRecentDecl();
  bool Found = false;
  for (auto *Redecl = MostRecent; Redecl && !Found;
       Redecl = Redecl->getPreviousDecl())
    Found = (Redecl == D);

  // If this declaration is merged, apply the functor to all later decls.
  if (Found) {
    for (auto *Redecl = MostRecent; Redecl != D;
         Redecl = Redecl->getPreviousDecl())
      F(Redecl);
  }
}

void ASTDeclReader::UpdateDecl(
    Decl *D,
    llvm::SmallVectorImpl<GlobalDeclID> &PendingLazySpecializationIDs) {
  while (Record.getIdx() < Record.size()) {
    switch ((DeclUpdateKind)Record.readInt()) {
    case UPD_CXX_ADDED_IMPLICIT_MEMBER: {
      auto *RD = cast<CXXRecordDecl>(D);
      Decl *MD = Record.readDecl();
      assert(MD && "couldn't read decl from update record");
      Reader.PendingAddedClassMembers.push_back({RD, MD});
      break;
    }

    case UPD_CXX_ADDED_TEMPLATE_SPECIALIZATION:
      // It will be added to the template's lazy specialization set.
      PendingLazySpecializationIDs.push_back(readDeclID());
      break;

    case UPD_CXX_ADDED_ANONYMOUS_NAMESPACE: {
      auto *Anon = readDeclAs<NamespaceDecl>();

      // Each module has its own anonymous namespace, which is disjoint from
      // any other module's anonymous namespaces, so don't attach the anonymous
      // namespace at all.
      if (!Record.isModule()) {
        if (auto *TU = dyn_cast<TranslationUnitDecl>(D))
          TU->setAnonymousNamespace(Anon);
        else
          cast<NamespaceDecl>(D)->setAnonymousNamespace(Anon);
      }
      break;
    }

    case UPD_CXX_ADDED_VAR_DEFINITION: {
      auto *VD = cast<VarDecl>(D);
      VD->NonParmVarDeclBits.IsInline = Record.readInt();
      VD->NonParmVarDeclBits.IsInlineSpecified = Record.readInt();
      ReadVarDeclInit(VD);
      break;
    }

    case UPD_CXX_POINT_OF_INSTANTIATION: {
      SourceLocation POI = Record.readSourceLocation();
      if (auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(D)) {
        VTSD->setPointOfInstantiation(POI);
      } else if (auto *VD = dyn_cast<VarDecl>(D)) {
        MemberSpecializationInfo *MSInfo = VD->getMemberSpecializationInfo();
        assert(MSInfo && "No member specialization information");
        MSInfo->setPointOfInstantiation(POI);
      } else {
        auto *FD = cast<FunctionDecl>(D);
        if (auto *FTSInfo = FD->TemplateOrSpecialization
                    .dyn_cast<FunctionTemplateSpecializationInfo *>())
          FTSInfo->setPointOfInstantiation(POI);
        else
          FD->TemplateOrSpecialization.get<MemberSpecializationInfo *>()
              ->setPointOfInstantiation(POI);
      }
      break;
    }

    case UPD_CXX_INSTANTIATED_DEFAULT_ARGUMENT: {
      auto *Param = cast<ParmVarDecl>(D);

      // We have to read the default argument regardless of whether we use it
      // so that hypothetical further update records aren't messed up.
      // TODO: Add a function to skip over the next expr record.
      auto *DefaultArg = Record.readExpr();

      // Only apply the update if the parameter still has an uninstantiated
      // default argument.
      if (Param->hasUninstantiatedDefaultArg())
        Param->setDefaultArg(DefaultArg);
      break;
    }

    case UPD_CXX_INSTANTIATED_DEFAULT_MEMBER_INITIALIZER: {
      auto *FD = cast<FieldDecl>(D);
      auto *DefaultInit = Record.readExpr();

      // Only apply the update if the field still has an uninstantiated
      // default member initializer.
      if (FD->hasInClassInitializer() && !FD->hasNonNullInClassInitializer()) {
        if (DefaultInit)
          FD->setInClassInitializer(DefaultInit);
        else
          // Instantiation failed. We can get here if we serialized an AST for
          // an invalid program.
          FD->removeInClassInitializer();
      }
      break;
    }

    case UPD_CXX_ADDED_FUNCTION_DEFINITION: {
      auto *FD = cast<FunctionDecl>(D);
      if (Reader.PendingBodies[FD]) {
        // FIXME: Maybe check for ODR violations.
        // It's safe to stop now because this update record is always last.
        return;
      }

      if (Record.readInt()) {
        // Maintain AST consistency: any later redeclarations of this function
        // are inline if this one is. (We might have merged another declaration
        // into this one.)
        forAllLaterRedecls(FD, [](FunctionDecl *FD) {
          FD->setImplicitlyInline();
        });
      }
      FD->setInnerLocStart(readSourceLocation());
      ReadFunctionDefinition(FD);
      assert(Record.getIdx() == Record.size() && "lazy body must be last");
      break;
    }

    case UPD_CXX_INSTANTIATED_CLASS_DEFINITION: {
      auto *RD = cast<CXXRecordDecl>(D);
      auto *OldDD = RD->getCanonicalDecl()->DefinitionData;
      bool HadRealDefinition =
          OldDD && (OldDD->Definition != RD ||
                    !Reader.PendingFakeDefinitionData.count(OldDD));
      RD->setParamDestroyedInCallee(Record.readInt());
      RD->setArgPassingRestrictions(
          static_cast<RecordArgPassingKind>(Record.readInt()));
      ReadCXXRecordDefinition(RD, /*Update*/true);

      // Visible update is handled separately.
      uint64_t LexicalOffset = ReadLocalOffset();
      if (!HadRealDefinition && LexicalOffset) {
        Record.readLexicalDeclContextStorage(LexicalOffset, RD);
        Reader.PendingFakeDefinitionData.erase(OldDD);
      }

      auto TSK = (TemplateSpecializationKind)Record.readInt();
      SourceLocation POI = readSourceLocation();
      if (MemberSpecializationInfo *MSInfo =
              RD->getMemberSpecializationInfo()) {
        MSInfo->setTemplateSpecializationKind(TSK);
        MSInfo->setPointOfInstantiation(POI);
      } else {
        auto *Spec = cast<ClassTemplateSpecializationDecl>(RD);
        Spec->setTemplateSpecializationKind(TSK);
        Spec->setPointOfInstantiation(POI);

        if (Record.readInt()) {
          auto *PartialSpec =
              readDeclAs<ClassTemplatePartialSpecializationDecl>();
          SmallVector<TemplateArgument, 8> TemplArgs;
          Record.readTemplateArgumentList(TemplArgs);
          auto *TemplArgList = TemplateArgumentList::CreateCopy(
              Reader.getContext(), TemplArgs);

          // FIXME: If we already have a partial specialization set,
          // check that it matches.
          if (!Spec->getSpecializedTemplateOrPartial()
                   .is<ClassTemplatePartialSpecializationDecl *>())
            Spec->setInstantiationOf(PartialSpec, TemplArgList);
        }
      }

      RD->setTagKind(static_cast<TagTypeKind>(Record.readInt()));
      RD->setLocation(readSourceLocation());
      RD->setLocStart(readSourceLocation());
      RD->setBraceRange(readSourceRange());

      if (Record.readInt()) {
        AttrVec Attrs;
        Record.readAttributes(Attrs);
        // If the declaration already has attributes, we assume that some other
        // AST file already loaded them.
        if (!D->hasAttrs())
          D->setAttrsImpl(Attrs, Reader.getContext());
      }
      break;
    }

    case UPD_CXX_RESOLVED_DTOR_DELETE: {
      // Set the 'operator delete' directly to avoid emitting another update
      // record.
      auto *Del = readDeclAs<FunctionDecl>();
      auto *First = cast<CXXDestructorDecl>(D->getCanonicalDecl());
      auto *ThisArg = Record.readExpr();
      // FIXME: Check consistency if we have an old and new operator delete.
      if (!First->OperatorDelete) {
        First->OperatorDelete = Del;
        First->OperatorDeleteThisArg = ThisArg;
      }
      break;
    }

    case UPD_CXX_RESOLVED_EXCEPTION_SPEC: {
      SmallVector<QualType, 8> ExceptionStorage;
      auto ESI = Record.readExceptionSpecInfo(ExceptionStorage);

      // Update this declaration's exception specification, if needed.
      auto *FD = cast<FunctionDecl>(D);
      auto *FPT = FD->getType()->castAs<FunctionProtoType>();
      // FIXME: If the exception specification is already present, check that it
      // matches.
      if (isUnresolvedExceptionSpec(FPT->getExceptionSpecType())) {
        FD->setType(Reader.getContext().getFunctionType(
            FPT->getReturnType(), FPT->getParamTypes(),
            FPT->getExtProtoInfo().withExceptionSpec(ESI)));

        // When we get to the end of deserializing, see if there are other decls
        // that we need to propagate this exception specification onto.
        Reader.PendingExceptionSpecUpdates.insert(
            std::make_pair(FD->getCanonicalDecl(), FD));
      }
      break;
    }

    case UPD_CXX_DEDUCED_RETURN_TYPE: {
      auto *FD = cast<FunctionDecl>(D);
      QualType DeducedResultType = Record.readType();
      Reader.PendingDeducedTypeUpdates.insert(
          {FD->getCanonicalDecl(), DeducedResultType});
      break;
    }

    case UPD_DECL_MARKED_USED:
      // Maintain AST consistency: any later redeclarations are used too.
      D->markUsed(Reader.getContext());
      break;

    case UPD_MANGLING_NUMBER:
      Reader.getContext().setManglingNumber(cast<NamedDecl>(D),
                                            Record.readInt());
      break;

    case UPD_STATIC_LOCAL_NUMBER:
      Reader.getContext().setStaticLocalNumber(cast<VarDecl>(D),
                                               Record.readInt());
      break;

    case UPD_DECL_MARKED_OPENMP_THREADPRIVATE:
      D->addAttr(OMPThreadPrivateDeclAttr::CreateImplicit(Reader.getContext(),
                                                          readSourceRange()));
      break;

    case UPD_DECL_MARKED_OPENMP_ALLOCATE: {
      auto AllocatorKind =
          static_cast<OMPAllocateDeclAttr::AllocatorTypeTy>(Record.readInt());
      Expr *Allocator = Record.readExpr();
      Expr *Alignment = Record.readExpr();
      SourceRange SR = readSourceRange();
      D->addAttr(OMPAllocateDeclAttr::CreateImplicit(
          Reader.getContext(), AllocatorKind, Allocator, Alignment, SR));
      break;
    }

    case UPD_DECL_EXPORTED: {
      unsigned SubmoduleID = readSubmoduleID();
      auto *Exported = cast<NamedDecl>(D);
      Module *Owner = SubmoduleID ? Reader.getSubmodule(SubmoduleID) : nullptr;
      Reader.getContext().mergeDefinitionIntoModule(Exported, Owner);
      Reader.PendingMergedDefinitionsToDeduplicate.insert(Exported);
      break;
    }

    case UPD_DECL_MARKED_OPENMP_DECLARETARGET: {
      auto MapType = Record.readEnum<OMPDeclareTargetDeclAttr::MapTypeTy>();
      auto DevType = Record.readEnum<OMPDeclareTargetDeclAttr::DevTypeTy>();
      Expr *IndirectE = Record.readExpr();
      bool Indirect = Record.readBool();
      unsigned Level = Record.readInt();
      D->addAttr(OMPDeclareTargetDeclAttr::CreateImplicit(
          Reader.getContext(), MapType, DevType, IndirectE, Indirect, Level,
          readSourceRange()));
      break;
    }

    case UPD_ADDED_ATTR_TO_RECORD:
      AttrVec Attrs;
      Record.readAttributes(Attrs);
      assert(Attrs.size() == 1);
      D->addAttr(Attrs[0]);
      break;
    }
  }
}
