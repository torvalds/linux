//===- CXIndexDataConsumer.h - Index data consumer for libclang--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CXINDEXDATACONSUMER_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CXINDEXDATACONSUMER_H

#include "CXCursor.h"
#include "Index_Internal.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "llvm/ADT/DenseSet.h"

namespace clang {
  class FileEntry;
  class MSPropertyDecl;
  class ObjCPropertyDecl;
  class ClassTemplateDecl;
  class FunctionTemplateDecl;
  class TypeAliasTemplateDecl;
  class ClassTemplateSpecializationDecl;

namespace cxindex {
  class CXIndexDataConsumer;
  class AttrListInfo;

class ScratchAlloc {
  CXIndexDataConsumer &IdxCtx;

public:
  explicit ScratchAlloc(CXIndexDataConsumer &indexCtx);
  ScratchAlloc(const ScratchAlloc &SA);

  ~ScratchAlloc();

  const char *toCStr(StringRef Str);
  const char *copyCStr(StringRef Str);

  template <typename T>
  T *allocate();
};

struct EntityInfo : public CXIdxEntityInfo {
  const NamedDecl *Dcl;
  CXIndexDataConsumer *IndexCtx;
  IntrusiveRefCntPtr<AttrListInfo> AttrList;

  EntityInfo() {
    name = USR = nullptr;
    attributes = nullptr;
    numAttributes = 0;
  }
};

struct ContainerInfo : public CXIdxContainerInfo {
  const DeclContext *DC;
  CXIndexDataConsumer *IndexCtx;
};
  
struct DeclInfo : public CXIdxDeclInfo {
  enum DInfoKind {
    Info_Decl,

    Info_ObjCContainer,
      Info_ObjCInterface,
      Info_ObjCProtocol,
      Info_ObjCCategory,

    Info_ObjCProperty,

    Info_CXXClass
  };
  
  DInfoKind Kind;

  EntityInfo EntInfo;
  ContainerInfo SemanticContainer;
  ContainerInfo LexicalContainer;
  ContainerInfo DeclAsContainer;

  DeclInfo(bool isRedeclaration, bool isDefinition, bool isContainer)
    : Kind(Info_Decl) {
    this->isRedeclaration = isRedeclaration;
    this->isDefinition = isDefinition;
    this->isContainer = isContainer;
    attributes = nullptr;
    numAttributes = 0;
    declAsContainer = semanticContainer = lexicalContainer = nullptr;
    flags = 0;
  }
  DeclInfo(DInfoKind K,
           bool isRedeclaration, bool isDefinition, bool isContainer)
    : Kind(K) {
    this->isRedeclaration = isRedeclaration;
    this->isDefinition = isDefinition;
    this->isContainer = isContainer;
    attributes = nullptr;
    numAttributes = 0;
    declAsContainer = semanticContainer = lexicalContainer = nullptr;
    flags = 0;
  }
};

struct ObjCContainerDeclInfo : public DeclInfo {
  CXIdxObjCContainerDeclInfo ObjCContDeclInfo;

  ObjCContainerDeclInfo(bool isForwardRef,
                        bool isRedeclaration,
                        bool isImplementation)
    : DeclInfo(Info_ObjCContainer, isRedeclaration,
               /*isDefinition=*/!isForwardRef, /*isContainer=*/!isForwardRef) {
    init(isForwardRef, isImplementation);
  }
  ObjCContainerDeclInfo(DInfoKind K,
                        bool isForwardRef,
                        bool isRedeclaration,
                        bool isImplementation)
    : DeclInfo(K, isRedeclaration, /*isDefinition=*/!isForwardRef,
               /*isContainer=*/!isForwardRef) {
    init(isForwardRef, isImplementation);
  }

  static bool classof(const DeclInfo *D) {
    return Info_ObjCContainer <= D->Kind && D->Kind <= Info_ObjCCategory;
  }

private:
  void init(bool isForwardRef, bool isImplementation) {
    if (isForwardRef)
      ObjCContDeclInfo.kind = CXIdxObjCContainer_ForwardRef;
    else if (isImplementation)
      ObjCContDeclInfo.kind = CXIdxObjCContainer_Implementation;
    else
      ObjCContDeclInfo.kind = CXIdxObjCContainer_Interface;
  }
};

struct ObjCInterfaceDeclInfo : public ObjCContainerDeclInfo {
  CXIdxObjCInterfaceDeclInfo ObjCInterDeclInfo;
  CXIdxObjCProtocolRefListInfo ObjCProtoListInfo;

  ObjCInterfaceDeclInfo(const ObjCInterfaceDecl *D)
    : ObjCContainerDeclInfo(Info_ObjCInterface,
                            /*isForwardRef=*/false,
                            /*isRedeclaration=*/D->getPreviousDecl() != nullptr,
                            /*isImplementation=*/false) { }

  static bool classof(const DeclInfo *D) {
    return D->Kind == Info_ObjCInterface;
  }
};

struct ObjCProtocolDeclInfo : public ObjCContainerDeclInfo {
  CXIdxObjCProtocolRefListInfo ObjCProtoRefListInfo;

  ObjCProtocolDeclInfo(const ObjCProtocolDecl *D)
    : ObjCContainerDeclInfo(Info_ObjCProtocol,
                            /*isForwardRef=*/false,
                            /*isRedeclaration=*/D->getPreviousDecl(),
                            /*isImplementation=*/false) { }

  static bool classof(const DeclInfo *D) {
    return D->Kind == Info_ObjCProtocol;
  }
};

struct ObjCCategoryDeclInfo : public ObjCContainerDeclInfo {
  CXIdxObjCCategoryDeclInfo ObjCCatDeclInfo;
  CXIdxObjCProtocolRefListInfo ObjCProtoListInfo;

  explicit ObjCCategoryDeclInfo(bool isImplementation)
    : ObjCContainerDeclInfo(Info_ObjCCategory,
                            /*isForwardRef=*/false,
                            /*isRedeclaration=*/isImplementation,
                            /*isImplementation=*/isImplementation) { }

  static bool classof(const DeclInfo *D) {
    return D->Kind == Info_ObjCCategory;
  }
};

struct ObjCPropertyDeclInfo : public DeclInfo {
  CXIdxObjCPropertyDeclInfo ObjCPropDeclInfo;

  ObjCPropertyDeclInfo()
    : DeclInfo(Info_ObjCProperty,
               /*isRedeclaration=*/false, /*isDefinition=*/false,
               /*isContainer=*/false) { }

  static bool classof(const DeclInfo *D) {
    return D->Kind == Info_ObjCProperty;
  }
};

struct CXXClassDeclInfo : public DeclInfo {
  CXIdxCXXClassDeclInfo CXXClassInfo;

  CXXClassDeclInfo(bool isRedeclaration, bool isDefinition)
    : DeclInfo(Info_CXXClass, isRedeclaration, isDefinition, isDefinition) { }

  static bool classof(const DeclInfo *D) {
    return D->Kind == Info_CXXClass;
  }
};

struct AttrInfo : public CXIdxAttrInfo {
  const Attr *A;

  AttrInfo(CXIdxAttrKind Kind, CXCursor C, CXIdxLoc Loc, const Attr *A) {
    kind = Kind;
    cursor = C;
    loc = Loc;
    this->A = A;
  }
};

struct IBOutletCollectionInfo : public AttrInfo {
  EntityInfo ClassInfo;
  CXIdxIBOutletCollectionAttrInfo IBCollInfo;

  IBOutletCollectionInfo(CXCursor C, CXIdxLoc Loc, const Attr *A) :
    AttrInfo(CXIdxAttr_IBOutletCollection, C, Loc, A) {
    assert(C.kind == CXCursor_IBOutletCollectionAttr);
    IBCollInfo.objcClass = nullptr;
  }

  IBOutletCollectionInfo(const IBOutletCollectionInfo &other);

  static bool classof(const AttrInfo *A) {
    return A->kind == CXIdxAttr_IBOutletCollection;
  }
};

class AttrListInfo {
  ScratchAlloc SA;

  SmallVector<AttrInfo, 2> Attrs;
  SmallVector<IBOutletCollectionInfo, 2> IBCollAttrs;
  SmallVector<CXIdxAttrInfo *, 2> CXAttrs;
  unsigned ref_cnt;

  AttrListInfo(const AttrListInfo &) = delete;
  void operator=(const AttrListInfo &) = delete;
public:
  AttrListInfo(const Decl *D, CXIndexDataConsumer &IdxCtx);

  static IntrusiveRefCntPtr<AttrListInfo> create(const Decl *D,
                                                 CXIndexDataConsumer &IdxCtx);

  const CXIdxAttrInfo *const *getAttrs() const {
    if (CXAttrs.empty())
      return nullptr;
    return CXAttrs.data();
  }
  unsigned getNumAttrs() const { return (unsigned)CXAttrs.size(); }

  /// Retain/Release only useful when we allocate a AttrListInfo from the
  /// BumpPtrAllocator, and not from the stack; so that we keep a pointer
  // in the EntityInfo
  void Retain() { ++ref_cnt; }
  void Release() {
    assert (ref_cnt > 0 && "Reference count is already zero.");
    if (--ref_cnt == 0) {
      // Memory is allocated from a BumpPtrAllocator, no need to delete it.
      this->~AttrListInfo();
    }
  }
};

class CXIndexDataConsumer : public index::IndexDataConsumer {
  ASTContext *Ctx;
  CXClientData ClientData;
  IndexerCallbacks &CB;
  unsigned IndexOptions;
  CXTranslationUnit CXTU;
  
  typedef llvm::DenseMap<const FileEntry *, CXIdxClientFile> FileMapTy;
  typedef llvm::DenseMap<const DeclContext *, CXIdxClientContainer>
    ContainerMapTy;
  typedef llvm::DenseMap<const Decl *, CXIdxClientEntity> EntityMapTy;

  FileMapTy FileMap;
  ContainerMapTy ContainerMap;
  EntityMapTy EntityMap;

  typedef std::pair<const FileEntry *, const Decl *> RefFileOccurrence;
  llvm::DenseSet<RefFileOccurrence> RefFileOccurrences;

  llvm::BumpPtrAllocator StrScratch;
  unsigned StrAdapterCount;
  friend class ScratchAlloc;

  struct ObjCProtocolListInfo {
    SmallVector<CXIdxObjCProtocolRefInfo, 4> ProtInfos;
    SmallVector<EntityInfo, 4> ProtEntities;
    SmallVector<CXIdxObjCProtocolRefInfo *, 4> Prots;

    CXIdxObjCProtocolRefListInfo getListInfo() const {
      CXIdxObjCProtocolRefListInfo Info = { Prots.data(),
                                            (unsigned)Prots.size() };
      return Info;
    }

    ObjCProtocolListInfo(const ObjCProtocolList &ProtList,
                         CXIndexDataConsumer &IdxCtx,
                         ScratchAlloc &SA);
  };

  struct CXXBasesListInfo {
    SmallVector<CXIdxBaseClassInfo, 4> BaseInfos;
    SmallVector<EntityInfo, 4> BaseEntities;
    SmallVector<CXIdxBaseClassInfo *, 4> CXBases;

    const CXIdxBaseClassInfo *const *getBases() const {
      return CXBases.data();
    }
    unsigned getNumBases() const { return (unsigned)CXBases.size(); }

    CXXBasesListInfo(const CXXRecordDecl *D,
                     CXIndexDataConsumer &IdxCtx, ScratchAlloc &SA);

  private:
    SourceLocation getBaseLoc(const CXXBaseSpecifier &Base) const;
  };

  friend class AttrListInfo;

public:
  CXIndexDataConsumer(CXClientData clientData, IndexerCallbacks &indexCallbacks,
                      unsigned indexOptions, CXTranslationUnit cxTU)
      : Ctx(nullptr), ClientData(clientData), CB(indexCallbacks),
        IndexOptions(indexOptions), CXTU(cxTU), StrAdapterCount(0) {}

  ASTContext &getASTContext() const { return *Ctx; }
  CXTranslationUnit getCXTU() const { return CXTU; }

  void setASTContext(ASTContext &ctx);
  void setPreprocessor(std::shared_ptr<Preprocessor> PP) override;

  bool shouldSuppressRefs() const {
    return IndexOptions & CXIndexOpt_SuppressRedundantRefs;
  }

  bool shouldIndexFunctionLocalSymbols() const {
    return IndexOptions & CXIndexOpt_IndexFunctionLocalSymbols;
  }

  bool shouldIndexImplicitTemplateInsts() const {
    return IndexOptions & CXIndexOpt_IndexImplicitTemplateInstantiations;
  }

  static bool isFunctionLocalDecl(const Decl *D);

  bool shouldAbort();

  bool hasDiagnosticCallback() const { return CB.diagnostic; }

  void enteredMainFile(OptionalFileEntryRef File);

  void ppIncludedFile(SourceLocation hashLoc, StringRef filename,
                      OptionalFileEntryRef File, bool isImport, bool isAngled,
                      bool isModuleImport);

  void importedModule(const ImportDecl *ImportD);
  void importedPCH(FileEntryRef File);

  void startedTranslationUnit();

  void indexDiagnostics();

  void handleDiagnosticSet(CXDiagnosticSet CXDiagSet);

  bool handleFunction(const FunctionDecl *FD);

  bool handleVar(const VarDecl *D);

  bool handleField(const FieldDecl *D);

  bool handleEnumerator(const EnumConstantDecl *D);

  bool handleTagDecl(const TagDecl *D);
  
  bool handleTypedefName(const TypedefNameDecl *D);

  bool handleObjCInterface(const ObjCInterfaceDecl *D);
  bool handleObjCImplementation(const ObjCImplementationDecl *D);

  bool handleObjCProtocol(const ObjCProtocolDecl *D);

  bool handleObjCCategory(const ObjCCategoryDecl *D);
  bool handleObjCCategoryImpl(const ObjCCategoryImplDecl *D);

  bool handleObjCMethod(const ObjCMethodDecl *D, SourceLocation Loc);

  bool handleSynthesizedObjCProperty(const ObjCPropertyImplDecl *D);
  bool handleSynthesizedObjCMethod(const ObjCMethodDecl *D, SourceLocation Loc,
                                   const DeclContext *LexicalDC);

  bool handleObjCProperty(const ObjCPropertyDecl *D);

  bool handleNamespace(const NamespaceDecl *D);

  bool handleClassTemplate(const ClassTemplateDecl *D);
  bool handleFunctionTemplate(const FunctionTemplateDecl *D);
  bool handleTypeAliasTemplate(const TypeAliasTemplateDecl *D);

  bool handleConcept(const ConceptDecl *D);

  bool handleReference(const NamedDecl *D, SourceLocation Loc, CXCursor Cursor,
                       const NamedDecl *Parent,
                       const DeclContext *DC,
                       const Expr *E = nullptr,
                       CXIdxEntityRefKind Kind = CXIdxEntityRef_Direct,
                       CXSymbolRole Role = CXSymbolRole_None);

  bool isNotFromSourceFile(SourceLocation Loc) const;

  void translateLoc(SourceLocation Loc, CXIdxClientFile *indexFile, CXFile *file,
                    unsigned *line, unsigned *column, unsigned *offset);

  CXIdxClientContainer getClientContainerForDC(const DeclContext *DC) const;
  void addContainerInMap(const DeclContext *DC, CXIdxClientContainer container);

  CXIdxClientEntity getClientEntity(const Decl *D) const;
  void setClientEntity(const Decl *D, CXIdxClientEntity client);

  static bool isTemplateImplicitInstantiation(const Decl *D);

private:
  bool handleDeclOccurrence(const Decl *D, index::SymbolRoleSet Roles,
                            ArrayRef<index::SymbolRelation> Relations,
                            SourceLocation Loc, ASTNodeInfo ASTNode) override;

  bool handleModuleOccurrence(const ImportDecl *ImportD, const Module *Mod,
                              index::SymbolRoleSet Roles,
                              SourceLocation Loc) override;

  void finish() override;

  bool handleDecl(const NamedDecl *D,
                  SourceLocation Loc, CXCursor Cursor,
                  DeclInfo &DInfo,
                  const DeclContext *LexicalDC = nullptr,
                  const DeclContext *SemaDC = nullptr);

  bool handleObjCContainer(const ObjCContainerDecl *D,
                           SourceLocation Loc, CXCursor Cursor,
                           ObjCContainerDeclInfo &ContDInfo);

  bool handleCXXRecordDecl(const CXXRecordDecl *RD, const NamedDecl *OrigD);

  bool markEntityOccurrenceInFile(const NamedDecl *D, SourceLocation Loc);

  const NamedDecl *getEntityDecl(const NamedDecl *D) const;

  const DeclContext *getEntityContainer(const Decl *D) const;

  CXIdxClientFile getIndexFile(OptionalFileEntryRef File);

  CXIdxLoc getIndexLoc(SourceLocation Loc) const;

  void getEntityInfo(const NamedDecl *D,
                     EntityInfo &EntityInfo,
                     ScratchAlloc &SA);

  void getContainerInfo(const DeclContext *DC, ContainerInfo &ContInfo);

  CXCursor getCursor(const Decl *D) {
    return cxcursor::MakeCXCursor(D, CXTU);
  }

  CXCursor getRefCursor(const NamedDecl *D, SourceLocation Loc);

  static bool shouldIgnoreIfImplicit(const Decl *D);
};

inline ScratchAlloc::ScratchAlloc(CXIndexDataConsumer &idxCtx) : IdxCtx(idxCtx) {
  ++IdxCtx.StrAdapterCount;
}
inline ScratchAlloc::ScratchAlloc(const ScratchAlloc &SA) : IdxCtx(SA.IdxCtx) {
  ++IdxCtx.StrAdapterCount;
}

inline ScratchAlloc::~ScratchAlloc() {
  --IdxCtx.StrAdapterCount;
  if (IdxCtx.StrAdapterCount == 0)
    IdxCtx.StrScratch.Reset();
}

template <typename T>
inline T *ScratchAlloc::allocate() {
  return IdxCtx.StrScratch.Allocate<T>();
}

}} // end clang::cxindex

#endif
