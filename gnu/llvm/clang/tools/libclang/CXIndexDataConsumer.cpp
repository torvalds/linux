//===- CXIndexDataConsumer.cpp - Index data consumer for libclang----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CXIndexDataConsumer.h"
#include "CIndexDiagnostic.h"
#include "CXFile.h"
#include "CXTranslationUnit.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/Frontend/ASTUnit.h"

using namespace clang;
using namespace clang::index;
using namespace cxindex;
using namespace cxcursor;

namespace {
class IndexingDeclVisitor : public ConstDeclVisitor<IndexingDeclVisitor, bool> {
  CXIndexDataConsumer &DataConsumer;
  SourceLocation DeclLoc;
  const DeclContext *LexicalDC;

public:
  IndexingDeclVisitor(CXIndexDataConsumer &dataConsumer, SourceLocation Loc,
                      const DeclContext *lexicalDC)
    : DataConsumer(dataConsumer), DeclLoc(Loc), LexicalDC(lexicalDC) { }

  bool VisitFunctionDecl(const FunctionDecl *D) {
    DataConsumer.handleFunction(D);
    return true;
  }

  bool VisitVarDecl(const VarDecl *D) {
    DataConsumer.handleVar(D);
    return true;
  }

  bool VisitFieldDecl(const FieldDecl *D) {
    DataConsumer.handleField(D);
    return true;
  }

  bool VisitMSPropertyDecl(const MSPropertyDecl *D) {
    return true;
  }

  bool VisitEnumConstantDecl(const EnumConstantDecl *D) {
    DataConsumer.handleEnumerator(D);
    return true;
  }

  bool VisitTypedefNameDecl(const TypedefNameDecl *D) {
    DataConsumer.handleTypedefName(D);
    return true;
  }

  bool VisitTagDecl(const TagDecl *D) {
    DataConsumer.handleTagDecl(D);
    return true;
  }

  bool VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
    DataConsumer.handleObjCInterface(D);
    return true;
  }

  bool VisitObjCProtocolDecl(const ObjCProtocolDecl *D) {
    DataConsumer.handleObjCProtocol(D);
    return true;
  }

  bool VisitObjCImplementationDecl(const ObjCImplementationDecl *D) {
    DataConsumer.handleObjCImplementation(D);
    return true;
  }

  bool VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
    DataConsumer.handleObjCCategory(D);
    return true;
  }

  bool VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D) {
    DataConsumer.handleObjCCategoryImpl(D);
    return true;
  }

  bool VisitObjCMethodDecl(const ObjCMethodDecl *D) {
    if (isa<ObjCImplDecl>(LexicalDC) && !D->isThisDeclarationADefinition())
      DataConsumer.handleSynthesizedObjCMethod(D, DeclLoc, LexicalDC);
    else
      DataConsumer.handleObjCMethod(D, DeclLoc);
    return true;
  }

  bool VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
    DataConsumer.handleObjCProperty(D);
    return true;
  }

  bool VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D) {
    DataConsumer.handleSynthesizedObjCProperty(D);
    return true;
  }

  bool VisitNamespaceDecl(const NamespaceDecl *D) {
    DataConsumer.handleNamespace(D);
    return true;
  }

  bool VisitUsingDecl(const UsingDecl *D) {
    return true;
  }

  bool VisitUsingDirectiveDecl(const UsingDirectiveDecl *D) {
    return true;
  }

  bool VisitClassTemplateDecl(const ClassTemplateDecl *D) {
    DataConsumer.handleClassTemplate(D);
    return true;
  }

  bool VisitClassTemplateSpecializationDecl(const
                                           ClassTemplateSpecializationDecl *D) {
    DataConsumer.handleTagDecl(D);
    return true;
  }

  bool VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
    DataConsumer.handleFunctionTemplate(D);
    return true;
  }

  bool VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D) {
    DataConsumer.handleTypeAliasTemplate(D);
    return true;
  }

  bool VisitImportDecl(const ImportDecl *D) {
    DataConsumer.importedModule(D);
    return true;
  }

  bool VisitConceptDecl(const ConceptDecl *D) {
    DataConsumer.handleConcept(D);
    return true;
  }
};

CXSymbolRole getSymbolRole(SymbolRoleSet Role) {
  // CXSymbolRole mirrors low 9 bits of clang::index::SymbolRole.
  return CXSymbolRole(static_cast<uint32_t>(Role) & ((1 << 9) - 1));
}
}

bool CXIndexDataConsumer::handleDeclOccurrence(
    const Decl *D, SymbolRoleSet Roles, ArrayRef<SymbolRelation> Relations,
    SourceLocation Loc, ASTNodeInfo ASTNode) {
  Loc = getASTContext().getSourceManager().getFileLoc(Loc);

  if (Roles & (unsigned)SymbolRole::Reference) {
    const NamedDecl *ND = dyn_cast<NamedDecl>(D);
    if (!ND)
      return true;

    if (auto *ObjCID = dyn_cast_or_null<ObjCInterfaceDecl>(ASTNode.OrigD)) {
      if (!ObjCID->isThisDeclarationADefinition() &&
          ObjCID->getLocation() == Loc) {
        // The libclang API treats this as ObjCClassRef declaration.
        IndexingDeclVisitor(*this, Loc, nullptr).Visit(ObjCID);
        return true;
      }
    }
    if (auto *ObjCPD = dyn_cast_or_null<ObjCProtocolDecl>(ASTNode.OrigD)) {
      if (!ObjCPD->isThisDeclarationADefinition() &&
          ObjCPD->getLocation() == Loc) {
        // The libclang API treats this as ObjCProtocolRef declaration.
        IndexingDeclVisitor(*this, Loc, nullptr).Visit(ObjCPD);
        return true;
      }
    }

    CXIdxEntityRefKind Kind = CXIdxEntityRef_Direct;
    if (Roles & (unsigned)SymbolRole::Implicit) {
      Kind = CXIdxEntityRef_Implicit;
    }
    CXSymbolRole CXRole = getSymbolRole(Roles);

    CXCursor Cursor;
    if (ASTNode.OrigE) {
      Cursor = cxcursor::MakeCXCursor(ASTNode.OrigE,
                                      cast<Decl>(ASTNode.ContainerDC),
                                      getCXTU());
    } else {
      if (ASTNode.OrigD) {
        if (auto *OrigND = dyn_cast<NamedDecl>(ASTNode.OrigD))
          Cursor = getRefCursor(OrigND, Loc);
        else
          Cursor = MakeCXCursor(ASTNode.OrigD, CXTU);
      } else {
        Cursor = getRefCursor(ND, Loc);
      }
    }
    handleReference(ND, Loc, Cursor,
                    dyn_cast_or_null<NamedDecl>(ASTNode.Parent),
                    ASTNode.ContainerDC, ASTNode.OrigE, Kind, CXRole);

  } else {
    const DeclContext *LexicalDC = ASTNode.ContainerDC;
    if (!LexicalDC) {
      for (const auto &SymRel : Relations) {
        if (SymRel.Roles & (unsigned)SymbolRole::RelationChildOf)
          LexicalDC = dyn_cast<DeclContext>(SymRel.RelatedSymbol);
      }
    }
    IndexingDeclVisitor(*this, Loc, LexicalDC).Visit(ASTNode.OrigD);
  }

  return !shouldAbort();
}

bool CXIndexDataConsumer::handleModuleOccurrence(const ImportDecl *ImportD,
                                                 const Module *Mod,
                                                 SymbolRoleSet Roles,
                                                 SourceLocation Loc) {
  if (Roles & (SymbolRoleSet)SymbolRole::Declaration)
    IndexingDeclVisitor(*this, SourceLocation(), nullptr).Visit(ImportD);
  return !shouldAbort();
}

void CXIndexDataConsumer::finish() {
  indexDiagnostics();
}


CXIndexDataConsumer::ObjCProtocolListInfo::ObjCProtocolListInfo(
                                    const ObjCProtocolList &ProtList,
                                    CXIndexDataConsumer &IdxCtx,
                                    ScratchAlloc &SA) {
  ObjCInterfaceDecl::protocol_loc_iterator LI = ProtList.loc_begin();
  for (ObjCInterfaceDecl::protocol_iterator
         I = ProtList.begin(), E = ProtList.end(); I != E; ++I, ++LI) {
    SourceLocation Loc = *LI;
    ObjCProtocolDecl *PD = *I;
    ProtEntities.push_back(EntityInfo());
    IdxCtx.getEntityInfo(PD, ProtEntities.back(), SA);
    CXIdxObjCProtocolRefInfo ProtInfo = { nullptr,
                                MakeCursorObjCProtocolRef(PD, Loc, IdxCtx.CXTU),
                                IdxCtx.getIndexLoc(Loc) };
    ProtInfos.push_back(ProtInfo);

    if (IdxCtx.shouldSuppressRefs())
      IdxCtx.markEntityOccurrenceInFile(PD, Loc);
  }

  for (unsigned i = 0, e = ProtInfos.size(); i != e; ++i)
    ProtInfos[i].protocol = &ProtEntities[i];

  for (unsigned i = 0, e = ProtInfos.size(); i != e; ++i)
    Prots.push_back(&ProtInfos[i]);
}


IBOutletCollectionInfo::IBOutletCollectionInfo(
                                          const IBOutletCollectionInfo &other)
  : AttrInfo(CXIdxAttr_IBOutletCollection, other.cursor, other.loc, other.A) {

  IBCollInfo.attrInfo = this;
  IBCollInfo.classCursor = other.IBCollInfo.classCursor;
  IBCollInfo.classLoc = other.IBCollInfo.classLoc;
  if (other.IBCollInfo.objcClass) {
    ClassInfo = other.ClassInfo;
    IBCollInfo.objcClass = &ClassInfo;
  } else
    IBCollInfo.objcClass = nullptr;
}

AttrListInfo::AttrListInfo(const Decl *D, CXIndexDataConsumer &IdxCtx)
  : SA(IdxCtx), ref_cnt(0) {

  if (!D->hasAttrs())
    return;

  for (const auto *A : D->attrs()) {
    CXCursor C = MakeCXCursor(A, D, IdxCtx.CXTU);
    CXIdxLoc Loc =  IdxCtx.getIndexLoc(A->getLocation());
    switch (C.kind) {
    default:
      Attrs.push_back(AttrInfo(CXIdxAttr_Unexposed, C, Loc, A));
      break;
    case CXCursor_IBActionAttr:
      Attrs.push_back(AttrInfo(CXIdxAttr_IBAction, C, Loc, A));
      break;
    case CXCursor_IBOutletAttr:
      Attrs.push_back(AttrInfo(CXIdxAttr_IBOutlet, C, Loc, A));
      break;
    case CXCursor_IBOutletCollectionAttr:
      IBCollAttrs.push_back(IBOutletCollectionInfo(C, Loc, A));
      break;
    }
  }

  for (unsigned i = 0, e = IBCollAttrs.size(); i != e; ++i) {
    IBOutletCollectionInfo &IBInfo = IBCollAttrs[i];
    CXAttrs.push_back(&IBInfo);

    const IBOutletCollectionAttr *
      IBAttr = cast<IBOutletCollectionAttr>(IBInfo.A);
    SourceLocation InterfaceLocStart =
        IBAttr->getInterfaceLoc()->getTypeLoc().getBeginLoc();
    IBInfo.IBCollInfo.attrInfo = &IBInfo;
    IBInfo.IBCollInfo.classLoc = IdxCtx.getIndexLoc(InterfaceLocStart);
    IBInfo.IBCollInfo.objcClass = nullptr;
    IBInfo.IBCollInfo.classCursor = clang_getNullCursor();
    QualType Ty = IBAttr->getInterface();
    if (const ObjCObjectType *ObjectTy = Ty->getAs<ObjCObjectType>()) {
      if (const ObjCInterfaceDecl *InterD = ObjectTy->getInterface()) {
        IdxCtx.getEntityInfo(InterD, IBInfo.ClassInfo, SA);
        IBInfo.IBCollInfo.objcClass = &IBInfo.ClassInfo;
        IBInfo.IBCollInfo.classCursor =
            MakeCursorObjCClassRef(InterD, InterfaceLocStart, IdxCtx.CXTU);
      }
    }
  }

  for (unsigned i = 0, e = Attrs.size(); i != e; ++i)
    CXAttrs.push_back(&Attrs[i]);
}

IntrusiveRefCntPtr<AttrListInfo>
AttrListInfo::create(const Decl *D, CXIndexDataConsumer &IdxCtx) {
  ScratchAlloc SA(IdxCtx);
  AttrListInfo *attrs = SA.allocate<AttrListInfo>();
  return new (attrs) AttrListInfo(D, IdxCtx);
}

CXIndexDataConsumer::CXXBasesListInfo::CXXBasesListInfo(const CXXRecordDecl *D,
                                   CXIndexDataConsumer &IdxCtx,
                                   ScratchAlloc &SA) {
  for (const auto &Base : D->bases()) {
    BaseEntities.push_back(EntityInfo());
    const NamedDecl *BaseD = nullptr;
    QualType T = Base.getType();
    SourceLocation Loc = getBaseLoc(Base);

    if (const TypedefType *TDT = T->getAs<TypedefType>()) {
      BaseD = TDT->getDecl();
    } else if (const TemplateSpecializationType *
          TST = T->getAs<TemplateSpecializationType>()) {
      BaseD = TST->getTemplateName().getAsTemplateDecl();
    } else if (const RecordType *RT = T->getAs<RecordType>()) {
      BaseD = RT->getDecl();
    }

    if (BaseD)
      IdxCtx.getEntityInfo(BaseD, BaseEntities.back(), SA);
    CXIdxBaseClassInfo BaseInfo = { nullptr,
                         MakeCursorCXXBaseSpecifier(&Base, IdxCtx.CXTU),
                         IdxCtx.getIndexLoc(Loc) };
    BaseInfos.push_back(BaseInfo);
  }

  for (unsigned i = 0, e = BaseInfos.size(); i != e; ++i) {
    if (BaseEntities[i].name && BaseEntities[i].USR)
      BaseInfos[i].base = &BaseEntities[i];
  }

  for (unsigned i = 0, e = BaseInfos.size(); i != e; ++i)
    CXBases.push_back(&BaseInfos[i]);
}

SourceLocation CXIndexDataConsumer::CXXBasesListInfo::getBaseLoc(
                                           const CXXBaseSpecifier &Base) const {
  SourceLocation Loc = Base.getSourceRange().getBegin();
  TypeLoc TL;
  if (Base.getTypeSourceInfo())
    TL = Base.getTypeSourceInfo()->getTypeLoc();
  if (TL.isNull())
    return Loc;

  if (QualifiedTypeLoc QL = TL.getAs<QualifiedTypeLoc>())
    TL = QL.getUnqualifiedLoc();

  if (ElaboratedTypeLoc EL = TL.getAs<ElaboratedTypeLoc>())
    return EL.getNamedTypeLoc().getBeginLoc();
  if (DependentNameTypeLoc DL = TL.getAs<DependentNameTypeLoc>())
    return DL.getNameLoc();
  if (DependentTemplateSpecializationTypeLoc DTL =
          TL.getAs<DependentTemplateSpecializationTypeLoc>())
    return DTL.getTemplateNameLoc();

  return Loc;
}

const char *ScratchAlloc::toCStr(StringRef Str) {
  if (Str.empty())
    return "";
  if (Str.data()[Str.size()] == '\0')
    return Str.data();
  return copyCStr(Str);
}

const char *ScratchAlloc::copyCStr(StringRef Str) {
  char *buf = IdxCtx.StrScratch.Allocate<char>(Str.size() + 1);
  std::uninitialized_copy(Str.begin(), Str.end(), buf);
  buf[Str.size()] = '\0';
  return buf;
}

void CXIndexDataConsumer::setASTContext(ASTContext &ctx) {
  Ctx = &ctx;
  cxtu::getASTUnit(CXTU)->setASTContext(&ctx);
}

void CXIndexDataConsumer::setPreprocessor(std::shared_ptr<Preprocessor> PP) {
  cxtu::getASTUnit(CXTU)->setPreprocessor(std::move(PP));
}

bool CXIndexDataConsumer::isFunctionLocalDecl(const Decl *D) {
  assert(D);

  if (!D->getParentFunctionOrMethod())
    return false;

  if (const NamedDecl *ND = dyn_cast<NamedDecl>(D)) {
    switch (ND->getFormalLinkage()) {
    case Linkage::Invalid:
      llvm_unreachable("Linkage hasn't been computed!");
    case Linkage::None:
    case Linkage::Internal:
      return true;
    case Linkage::VisibleNone:
    case Linkage::UniqueExternal:
      llvm_unreachable("Not a sema linkage");
    case Linkage::Module:
    case Linkage::External:
      return false;
    }
  }

  return true;
}

bool CXIndexDataConsumer::shouldAbort() {
  if (!CB.abortQuery)
    return false;
  return CB.abortQuery(ClientData, nullptr);
}

void CXIndexDataConsumer::enteredMainFile(OptionalFileEntryRef File) {
  if (File && CB.enteredMainFile) {
    CXIdxClientFile idxFile =
        CB.enteredMainFile(ClientData, cxfile::makeCXFile(*File), nullptr);
    FileMap[*File] = idxFile;
  }
}

void CXIndexDataConsumer::ppIncludedFile(SourceLocation hashLoc,
                                         StringRef filename,
                                         OptionalFileEntryRef File,
                                         bool isImport, bool isAngled,
                                         bool isModuleImport) {
  if (!CB.ppIncludedFile)
    return;

  const FileEntry *FE = File ? &File->getFileEntry() : nullptr;

  ScratchAlloc SA(*this);
  CXIdxIncludedFileInfo Info = { getIndexLoc(hashLoc),
                                 SA.toCStr(filename),
                                 cxfile::makeCXFile(File),
                                 isImport, isAngled, isModuleImport };
  CXIdxClientFile idxFile = CB.ppIncludedFile(ClientData, &Info);
  FileMap[FE] = idxFile;
}

void CXIndexDataConsumer::importedModule(const ImportDecl *ImportD) {
  if (!CB.importedASTFile)
    return;

  Module *Mod = ImportD->getImportedModule();
  if (!Mod)
    return;

  // If the imported module is part of the top-level module that we're
  // indexing, it doesn't correspond to an imported AST file.
  // FIXME: This assumes that AST files and top-level modules directly
  // correspond, which is unlikely to remain true forever.
  if (Module *SrcMod = ImportD->getImportedOwningModule())
    if (SrcMod->getTopLevelModule() == Mod->getTopLevelModule())
      return;

  OptionalFileEntryRef FE = Mod->getASTFile();
  CXIdxImportedASTFileInfo Info = {cxfile::makeCXFile(FE), Mod,
                                   getIndexLoc(ImportD->getLocation()),
                                   ImportD->isImplicit()};
  CXIdxClientASTFile astFile = CB.importedASTFile(ClientData, &Info);
  (void)astFile;
}

void CXIndexDataConsumer::importedPCH(FileEntryRef File) {
  if (!CB.importedASTFile)
    return;

  CXIdxImportedASTFileInfo Info = {
                                    cxfile::makeCXFile(File),
                                    /*module=*/nullptr,
                                    getIndexLoc(SourceLocation()),
                                    /*isImplicit=*/false
                                  };
  CXIdxClientASTFile astFile = CB.importedASTFile(ClientData, &Info);
  (void)astFile;
}

void CXIndexDataConsumer::startedTranslationUnit() {
  CXIdxClientContainer idxCont = nullptr;
  if (CB.startedTranslationUnit)
    idxCont = CB.startedTranslationUnit(ClientData, nullptr);
  addContainerInMap(Ctx->getTranslationUnitDecl(), idxCont);
}

void CXIndexDataConsumer::indexDiagnostics() {
  if (!hasDiagnosticCallback())
    return;

  CXDiagnosticSetImpl *DiagSet = cxdiag::lazyCreateDiags(getCXTU());
  handleDiagnosticSet(DiagSet);
}

void CXIndexDataConsumer::handleDiagnosticSet(CXDiagnostic CXDiagSet) {
  if (!CB.diagnostic)
    return;

  CB.diagnostic(ClientData, CXDiagSet, nullptr);
}

bool CXIndexDataConsumer::handleDecl(const NamedDecl *D,
                                 SourceLocation Loc, CXCursor Cursor,
                                 DeclInfo &DInfo,
                                 const DeclContext *LexicalDC,
                                 const DeclContext *SemaDC) {
  if (!CB.indexDeclaration || !D)
    return false;
  if (D->isImplicit() && shouldIgnoreIfImplicit(D))
    return false;

  ScratchAlloc SA(*this);
  getEntityInfo(D, DInfo.EntInfo, SA);
  if ((!shouldIndexFunctionLocalSymbols() && !DInfo.EntInfo.USR)
      || Loc.isInvalid())
    return false;

  if (!LexicalDC)
    LexicalDC = D->getLexicalDeclContext();

  if (shouldSuppressRefs())
    markEntityOccurrenceInFile(D, Loc);
  
  DInfo.entityInfo = &DInfo.EntInfo;
  DInfo.cursor = Cursor;
  DInfo.loc = getIndexLoc(Loc);
  DInfo.isImplicit = D->isImplicit();

  DInfo.attributes = DInfo.EntInfo.attributes;
  DInfo.numAttributes = DInfo.EntInfo.numAttributes;

  if (!SemaDC)
    SemaDC = D->getDeclContext();
  getContainerInfo(SemaDC, DInfo.SemanticContainer);
  DInfo.semanticContainer = &DInfo.SemanticContainer;

  if (LexicalDC == SemaDC) {
    DInfo.lexicalContainer = &DInfo.SemanticContainer;
  } else if (isTemplateImplicitInstantiation(D)) {
    // Implicit instantiations have the lexical context of where they were
    // instantiated first. We choose instead the semantic context because:
    // 1) at the time that we see the instantiation we have not seen the
    //   function where it occurred yet.
    // 2) the lexical context of the first instantiation is not useful
    //   information anyway.
    DInfo.lexicalContainer = &DInfo.SemanticContainer;
  } else {
    getContainerInfo(LexicalDC, DInfo.LexicalContainer);
    DInfo.lexicalContainer = &DInfo.LexicalContainer;
  }

  if (DInfo.isContainer) {
    getContainerInfo(getEntityContainer(D), DInfo.DeclAsContainer);
    DInfo.declAsContainer = &DInfo.DeclAsContainer;
  }

  CB.indexDeclaration(ClientData, &DInfo);
  return true;
}

bool CXIndexDataConsumer::handleObjCContainer(const ObjCContainerDecl *D,
                                          SourceLocation Loc, CXCursor Cursor,
                                          ObjCContainerDeclInfo &ContDInfo) {
  ContDInfo.ObjCContDeclInfo.declInfo = &ContDInfo;
  return handleDecl(D, Loc, Cursor, ContDInfo);
}

bool CXIndexDataConsumer::handleFunction(const FunctionDecl *D) {
  bool isDef = D->isThisDeclarationADefinition();
  bool isContainer = isDef;
  bool isSkipped = false;
  if (D->hasSkippedBody()) {
    isSkipped = true;
    isDef = true;
    isContainer = false;
  }

  DeclInfo DInfo(!D->isFirstDecl(), isDef, isContainer);
  if (isSkipped)
    DInfo.flags |= CXIdxDeclFlag_Skipped;
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleVar(const VarDecl *D) {
  DeclInfo DInfo(!D->isFirstDecl(), D->isThisDeclarationADefinition(),
                 /*isContainer=*/false);
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleField(const FieldDecl *D) {
  DeclInfo DInfo(/*isRedeclaration=*/false, /*isDefinition=*/true,
                 /*isContainer=*/false);
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleEnumerator(const EnumConstantDecl *D) {
  DeclInfo DInfo(/*isRedeclaration=*/false, /*isDefinition=*/true,
                 /*isContainer=*/false);
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleTagDecl(const TagDecl *D) {
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(D))
    return handleCXXRecordDecl(CXXRD, D);

  DeclInfo DInfo(!D->isFirstDecl(), D->isThisDeclarationADefinition(),
                 D->isThisDeclarationADefinition());
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleTypedefName(const TypedefNameDecl *D) {
  DeclInfo DInfo(!D->isFirstDecl(), /*isDefinition=*/true,
                 /*isContainer=*/false);
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleObjCInterface(const ObjCInterfaceDecl *D) {
  // For @class forward declarations, suppress them the same way as references.
  if (!D->isThisDeclarationADefinition()) {
    if (shouldSuppressRefs() && markEntityOccurrenceInFile(D, D->getLocation()))
      return false; // already occurred.

    // FIXME: This seems like the wrong definition for redeclaration.
    bool isRedeclaration = D->hasDefinition() || D->getPreviousDecl();
    ObjCContainerDeclInfo ContDInfo(/*isForwardRef=*/true, isRedeclaration,
                                    /*isImplementation=*/false);
    return handleObjCContainer(D, D->getLocation(),
                               MakeCursorObjCClassRef(D, D->getLocation(),
                                                      CXTU), 
                               ContDInfo);
  }

  ScratchAlloc SA(*this);

  CXIdxBaseClassInfo BaseClass;
  EntityInfo BaseEntity;
  BaseClass.cursor = clang_getNullCursor();
  if (ObjCInterfaceDecl *SuperD = D->getSuperClass()) {
    getEntityInfo(SuperD, BaseEntity, SA);
    SourceLocation SuperLoc = D->getSuperClassLoc();
    BaseClass.base = &BaseEntity;
    BaseClass.cursor = MakeCursorObjCSuperClassRef(SuperD, SuperLoc, CXTU);
    BaseClass.loc = getIndexLoc(SuperLoc);

    if (shouldSuppressRefs())
      markEntityOccurrenceInFile(SuperD, SuperLoc);
  }
  
  ObjCProtocolList EmptyProtoList;
  ObjCProtocolListInfo ProtInfo(D->isThisDeclarationADefinition() 
                                  ? D->getReferencedProtocols()
                                  : EmptyProtoList, 
                                *this, SA);
  
  ObjCInterfaceDeclInfo InterInfo(D);
  InterInfo.ObjCProtoListInfo = ProtInfo.getListInfo();
  InterInfo.ObjCInterDeclInfo.containerInfo = &InterInfo.ObjCContDeclInfo;
  InterInfo.ObjCInterDeclInfo.superInfo = D->getSuperClass() ? &BaseClass
                                                             : nullptr;
  InterInfo.ObjCInterDeclInfo.protocols = &InterInfo.ObjCProtoListInfo;

  return handleObjCContainer(D, D->getLocation(), getCursor(D), InterInfo);
}

bool CXIndexDataConsumer::handleObjCImplementation(
                                              const ObjCImplementationDecl *D) {
  ObjCContainerDeclInfo ContDInfo(/*isForwardRef=*/false,
                      /*isRedeclaration=*/true,
                      /*isImplementation=*/true);
  return handleObjCContainer(D, D->getLocation(), getCursor(D), ContDInfo);
}

bool CXIndexDataConsumer::handleObjCProtocol(const ObjCProtocolDecl *D) {
  if (!D->isThisDeclarationADefinition()) {
    if (shouldSuppressRefs() && markEntityOccurrenceInFile(D, D->getLocation()))
      return false; // already occurred.
    
    // FIXME: This seems like the wrong definition for redeclaration.
    bool isRedeclaration = D->hasDefinition() || D->getPreviousDecl();
    ObjCContainerDeclInfo ContDInfo(/*isForwardRef=*/true,
                                    isRedeclaration,
                                    /*isImplementation=*/false);
    return handleObjCContainer(D, D->getLocation(), 
                               MakeCursorObjCProtocolRef(D, D->getLocation(),
                                                         CXTU),
                               ContDInfo);    
  }
  
  ScratchAlloc SA(*this);
  ObjCProtocolList EmptyProtoList;
  ObjCProtocolListInfo ProtListInfo(D->isThisDeclarationADefinition()
                                      ? D->getReferencedProtocols()
                                      : EmptyProtoList,
                                    *this, SA);
  
  ObjCProtocolDeclInfo ProtInfo(D);
  ProtInfo.ObjCProtoRefListInfo = ProtListInfo.getListInfo();

  return handleObjCContainer(D, D->getLocation(), getCursor(D), ProtInfo);
}

bool CXIndexDataConsumer::handleObjCCategory(const ObjCCategoryDecl *D) {
  ScratchAlloc SA(*this);

  ObjCCategoryDeclInfo CatDInfo(/*isImplementation=*/false);
  EntityInfo ClassEntity;
  const ObjCInterfaceDecl *IFaceD = D->getClassInterface();
  SourceLocation ClassLoc = D->getLocation();
  SourceLocation CategoryLoc = D->IsClassExtension() ? ClassLoc
                                                     : D->getCategoryNameLoc();
  getEntityInfo(IFaceD, ClassEntity, SA);

  if (shouldSuppressRefs())
    markEntityOccurrenceInFile(IFaceD, ClassLoc);

  ObjCProtocolListInfo ProtInfo(D->getReferencedProtocols(), *this, SA);
  
  CatDInfo.ObjCCatDeclInfo.containerInfo = &CatDInfo.ObjCContDeclInfo;
  if (IFaceD) {
    CatDInfo.ObjCCatDeclInfo.objcClass = &ClassEntity;
    CatDInfo.ObjCCatDeclInfo.classCursor =
        MakeCursorObjCClassRef(IFaceD, ClassLoc, CXTU);
  } else {
    CatDInfo.ObjCCatDeclInfo.objcClass = nullptr;
    CatDInfo.ObjCCatDeclInfo.classCursor = clang_getNullCursor();
  }
  CatDInfo.ObjCCatDeclInfo.classLoc = getIndexLoc(ClassLoc);
  CatDInfo.ObjCProtoListInfo = ProtInfo.getListInfo();
  CatDInfo.ObjCCatDeclInfo.protocols = &CatDInfo.ObjCProtoListInfo;

  return handleObjCContainer(D, CategoryLoc, getCursor(D), CatDInfo);
}

bool CXIndexDataConsumer::handleObjCCategoryImpl(const ObjCCategoryImplDecl *D) {
  ScratchAlloc SA(*this);

  const ObjCCategoryDecl *CatD = D->getCategoryDecl();
  ObjCCategoryDeclInfo CatDInfo(/*isImplementation=*/true);
  EntityInfo ClassEntity;
  const ObjCInterfaceDecl *IFaceD = CatD->getClassInterface();
  SourceLocation ClassLoc = D->getLocation();
  SourceLocation CategoryLoc = D->getCategoryNameLoc();
  getEntityInfo(IFaceD, ClassEntity, SA);

  if (shouldSuppressRefs())
    markEntityOccurrenceInFile(IFaceD, ClassLoc);

  CatDInfo.ObjCCatDeclInfo.containerInfo = &CatDInfo.ObjCContDeclInfo;
  if (IFaceD) {
    CatDInfo.ObjCCatDeclInfo.objcClass = &ClassEntity;
    CatDInfo.ObjCCatDeclInfo.classCursor =
        MakeCursorObjCClassRef(IFaceD, ClassLoc, CXTU);
  } else {
    CatDInfo.ObjCCatDeclInfo.objcClass = nullptr;
    CatDInfo.ObjCCatDeclInfo.classCursor = clang_getNullCursor();
  }
  CatDInfo.ObjCCatDeclInfo.classLoc = getIndexLoc(ClassLoc);
  CatDInfo.ObjCCatDeclInfo.protocols = nullptr;

  return handleObjCContainer(D, CategoryLoc, getCursor(D), CatDInfo);
}

bool CXIndexDataConsumer::handleObjCMethod(const ObjCMethodDecl *D,
                                           SourceLocation Loc) {
  bool isDef = D->isThisDeclarationADefinition();
  bool isContainer = isDef;
  bool isSkipped = false;
  if (D->hasSkippedBody()) {
    isSkipped = true;
    isDef = true;
    isContainer = false;
  }

  DeclInfo DInfo(!D->isCanonicalDecl(), isDef, isContainer);
  if (isSkipped)
    DInfo.flags |= CXIdxDeclFlag_Skipped;
  return handleDecl(D, Loc, getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleSynthesizedObjCProperty(
                                                const ObjCPropertyImplDecl *D) {
  ObjCPropertyDecl *PD = D->getPropertyDecl();
  auto *DC = D->getDeclContext();
  return handleReference(PD, D->getLocation(), getCursor(D),
                         dyn_cast<NamedDecl>(DC), DC);
}

bool CXIndexDataConsumer::handleSynthesizedObjCMethod(const ObjCMethodDecl *D,
                                                  SourceLocation Loc,
                                                 const DeclContext *LexicalDC) {
  DeclInfo DInfo(/*isRedeclaration=*/true, /*isDefinition=*/true,
                 /*isContainer=*/false);
  return handleDecl(D, Loc, getCursor(D), DInfo, LexicalDC, D->getDeclContext());
}

bool CXIndexDataConsumer::handleObjCProperty(const ObjCPropertyDecl *D) {
  ScratchAlloc SA(*this);

  ObjCPropertyDeclInfo DInfo;
  EntityInfo GetterEntity;
  EntityInfo SetterEntity;

  DInfo.ObjCPropDeclInfo.declInfo = &DInfo;

  if (ObjCMethodDecl *Getter = D->getGetterMethodDecl()) {
    getEntityInfo(Getter, GetterEntity, SA);
    DInfo.ObjCPropDeclInfo.getter = &GetterEntity;
  } else {
    DInfo.ObjCPropDeclInfo.getter = nullptr;
  }
  if (ObjCMethodDecl *Setter = D->getSetterMethodDecl()) {
    getEntityInfo(Setter, SetterEntity, SA);
    DInfo.ObjCPropDeclInfo.setter = &SetterEntity;
  } else {
    DInfo.ObjCPropDeclInfo.setter = nullptr;
  }

  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleNamespace(const NamespaceDecl *D) {
  DeclInfo DInfo(/*isRedeclaration=*/!D->isFirstDecl(),
                 /*isDefinition=*/true,
                 /*isContainer=*/true);
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleClassTemplate(const ClassTemplateDecl *D) {
  return handleCXXRecordDecl(D->getTemplatedDecl(), D);
}

bool CXIndexDataConsumer::handleFunctionTemplate(const FunctionTemplateDecl *D) {
  DeclInfo DInfo(/*isRedeclaration=*/!D->isCanonicalDecl(),
                 /*isDefinition=*/D->isThisDeclarationADefinition(),
                 /*isContainer=*/D->isThisDeclarationADefinition());
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleTypeAliasTemplate(const TypeAliasTemplateDecl *D) {
  DeclInfo DInfo(/*isRedeclaration=*/!D->isCanonicalDecl(),
                 /*isDefinition=*/true, /*isContainer=*/false);
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleConcept(const ConceptDecl *D) {
  DeclInfo DInfo(/*isRedeclaration=*/!D->isCanonicalDecl(),
                 /*isDefinition=*/true, /*isContainer=*/false);
  return handleDecl(D, D->getLocation(), getCursor(D), DInfo);
}

bool CXIndexDataConsumer::handleReference(const NamedDecl *D, SourceLocation Loc,
                                      CXCursor Cursor,
                                      const NamedDecl *Parent,
                                      const DeclContext *DC,
                                      const Expr *E,
                                      CXIdxEntityRefKind Kind,
                                      CXSymbolRole Role) {
  if (!CB.indexEntityReference)
    return false;

  if (!D || !DC)
    return false;
  if (Loc.isInvalid())
    return false;
  if (!shouldIndexFunctionLocalSymbols() && isFunctionLocalDecl(D))
    return false;
  if (isNotFromSourceFile(D->getLocation()))
    return false;
  if (D->isImplicit() && shouldIgnoreIfImplicit(D))
    return false;

  if (shouldSuppressRefs()) {
    if (markEntityOccurrenceInFile(D, Loc))
      return false; // already occurred.
  }

  ScratchAlloc SA(*this);
  EntityInfo RefEntity, ParentEntity;
  getEntityInfo(D, RefEntity, SA);
  if (!RefEntity.USR)
    return false;

  getEntityInfo(Parent, ParentEntity, SA);

  ContainerInfo Container;
  getContainerInfo(DC, Container);

  CXIdxEntityRefInfo Info = { Kind,
                              Cursor,
                              getIndexLoc(Loc),
                              &RefEntity,
                              Parent ? &ParentEntity : nullptr,
                              &Container,
                              Role };
  CB.indexEntityReference(ClientData, &Info);
  return true;
}

bool CXIndexDataConsumer::isNotFromSourceFile(SourceLocation Loc) const {
  if (Loc.isInvalid())
    return true;
  SourceManager &SM = Ctx->getSourceManager();
  SourceLocation FileLoc = SM.getFileLoc(Loc);
  FileID FID = SM.getFileID(FileLoc);
  return SM.getFileEntryForID(FID) == nullptr;
}

void CXIndexDataConsumer::addContainerInMap(const DeclContext *DC,
                                        CXIdxClientContainer container) {
  if (!DC)
    return;

  ContainerMapTy::iterator I = ContainerMap.find(DC);
  if (I == ContainerMap.end()) {
    if (container)
      ContainerMap[DC] = container;
    return;
  }
  // Allow changing the container of a previously seen DeclContext so we
  // can handle invalid user code, like a function re-definition.
  if (container)
    I->second = container;
  else
    ContainerMap.erase(I);
}

CXIdxClientEntity CXIndexDataConsumer::getClientEntity(const Decl *D) const {
  return D ? EntityMap.lookup(D) : nullptr;
}

void CXIndexDataConsumer::setClientEntity(const Decl *D, CXIdxClientEntity client) {
  if (!D)
    return;
  EntityMap[D] = client;
}

bool CXIndexDataConsumer::handleCXXRecordDecl(const CXXRecordDecl *RD,
                                          const NamedDecl *OrigD) {
  if (RD->isThisDeclarationADefinition()) {
    ScratchAlloc SA(*this);
    CXXClassDeclInfo CXXDInfo(/*isRedeclaration=*/!OrigD->isCanonicalDecl(),
                           /*isDefinition=*/RD->isThisDeclarationADefinition());
    CXXBasesListInfo BaseList(RD, *this, SA);
    CXXDInfo.CXXClassInfo.declInfo = &CXXDInfo;
    CXXDInfo.CXXClassInfo.bases = BaseList.getBases();
    CXXDInfo.CXXClassInfo.numBases = BaseList.getNumBases();

    if (shouldSuppressRefs()) {
      // Go through bases and mark them as referenced.
      for (unsigned i = 0, e = BaseList.getNumBases(); i != e; ++i) {
        const CXIdxBaseClassInfo *baseInfo = BaseList.getBases()[i];
        if (baseInfo->base) {
          const NamedDecl *BaseD = BaseList.BaseEntities[i].Dcl;
          SourceLocation
            Loc = SourceLocation::getFromRawEncoding(baseInfo->loc.int_data);
          markEntityOccurrenceInFile(BaseD, Loc);
        }
      }
    }

    return handleDecl(OrigD, OrigD->getLocation(), getCursor(OrigD), CXXDInfo);
  }

  DeclInfo DInfo(/*isRedeclaration=*/!OrigD->isCanonicalDecl(),
                 /*isDefinition=*/RD->isThisDeclarationADefinition(),
                 /*isContainer=*/RD->isThisDeclarationADefinition());
  return handleDecl(OrigD, OrigD->getLocation(), getCursor(OrigD), DInfo);
}

bool CXIndexDataConsumer::markEntityOccurrenceInFile(const NamedDecl *D,
                                                 SourceLocation Loc) {
  if (!D || Loc.isInvalid())
    return true;

  SourceManager &SM = Ctx->getSourceManager();
  D = getEntityDecl(D);
  
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(SM.getFileLoc(Loc));
  FileID FID = LocInfo.first;
  if (FID.isInvalid())
    return true;
  
  const FileEntry *FE = SM.getFileEntryForID(FID);
  if (!FE)
    return true;
  RefFileOccurrence RefOccur(FE, D);
  std::pair<llvm::DenseSet<RefFileOccurrence>::iterator, bool>
  res = RefFileOccurrences.insert(RefOccur);
  return !res.second; // already in map
}

const NamedDecl *CXIndexDataConsumer::getEntityDecl(const NamedDecl *D) const {
  assert(D);
  D = cast<NamedDecl>(D->getCanonicalDecl());

  if (const ObjCImplementationDecl *
               ImplD = dyn_cast<ObjCImplementationDecl>(D)) {
    return getEntityDecl(ImplD->getClassInterface());

  } else if (const ObjCCategoryImplDecl *
               CatImplD = dyn_cast<ObjCCategoryImplDecl>(D)) {
    return getEntityDecl(CatImplD->getCategoryDecl());
  } else if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FunctionTemplateDecl *TemplD = FD->getDescribedFunctionTemplate())
      return getEntityDecl(TemplD);
  } else if (const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D)) {
    if (ClassTemplateDecl *TemplD = RD->getDescribedClassTemplate())
      return getEntityDecl(TemplD);
  }

  return D;
}

const DeclContext *
CXIndexDataConsumer::getEntityContainer(const Decl *D) const {
  const DeclContext *DC = dyn_cast<DeclContext>(D);
  if (DC)
    return DC;

  if (const ClassTemplateDecl *ClassTempl = dyn_cast<ClassTemplateDecl>(D)) {
    DC = ClassTempl->getTemplatedDecl();
  } else if (const FunctionTemplateDecl *
          FuncTempl = dyn_cast<FunctionTemplateDecl>(D)) {
    DC = FuncTempl->getTemplatedDecl();
  }

  return DC;
}

CXIdxClientContainer
CXIndexDataConsumer::getClientContainerForDC(const DeclContext *DC) const {
  return DC ? ContainerMap.lookup(DC) : nullptr;
}

CXIdxClientFile CXIndexDataConsumer::getIndexFile(OptionalFileEntryRef File) {
  return File ? FileMap.lookup(*File) : nullptr;
}

CXIdxLoc CXIndexDataConsumer::getIndexLoc(SourceLocation Loc) const {
  CXIdxLoc idxLoc =  { {nullptr, nullptr}, 0 };
  if (Loc.isInvalid())
    return idxLoc;

  idxLoc.ptr_data[0] = const_cast<CXIndexDataConsumer *>(this);
  idxLoc.int_data = Loc.getRawEncoding();
  return idxLoc;
}

void CXIndexDataConsumer::translateLoc(SourceLocation Loc,
                                   CXIdxClientFile *indexFile, CXFile *file,
                                   unsigned *line, unsigned *column,
                                   unsigned *offset) {
  if (Loc.isInvalid())
    return;

  SourceManager &SM = Ctx->getSourceManager();
  Loc = SM.getFileLoc(Loc);

  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  FileID FID = LocInfo.first;
  unsigned FileOffset = LocInfo.second;

  if (FID.isInvalid())
    return;

  OptionalFileEntryRef FE = SM.getFileEntryRefForID(FID);
  if (indexFile)
    *indexFile = getIndexFile(FE);
  if (file)
    *file = cxfile::makeCXFile(FE);
  if (line)
    *line = SM.getLineNumber(FID, FileOffset);
  if (column)
    *column = SM.getColumnNumber(FID, FileOffset);
  if (offset)
    *offset = FileOffset;
}

static CXIdxEntityKind getEntityKindFromSymbolKind(SymbolKind K, SymbolLanguage L);
static CXIdxEntityCXXTemplateKind
getEntityKindFromSymbolProperties(SymbolPropertySet K);
static CXIdxEntityLanguage getEntityLangFromSymbolLang(SymbolLanguage L);

void CXIndexDataConsumer::getEntityInfo(const NamedDecl *D,
                                    EntityInfo &EntityInfo,
                                    ScratchAlloc &SA) {
  if (!D)
    return;

  D = getEntityDecl(D);
  EntityInfo.cursor = getCursor(D);
  EntityInfo.Dcl = D;
  EntityInfo.IndexCtx = this;

  SymbolInfo SymInfo = getSymbolInfo(D);
  EntityInfo.kind = getEntityKindFromSymbolKind(SymInfo.Kind, SymInfo.Lang);
  EntityInfo.templateKind = getEntityKindFromSymbolProperties(SymInfo.Properties);
  EntityInfo.lang = getEntityLangFromSymbolLang(SymInfo.Lang);

  if (D->hasAttrs()) {
    EntityInfo.AttrList = AttrListInfo::create(D, *this);
    EntityInfo.attributes = EntityInfo.AttrList->getAttrs();
    EntityInfo.numAttributes = EntityInfo.AttrList->getNumAttrs();
  }

  if (EntityInfo.kind == CXIdxEntity_Unexposed)
    return;

  if (IdentifierInfo *II = D->getIdentifier()) {
    EntityInfo.name = SA.toCStr(II->getName());

  } else if (isa<TagDecl>(D) || isa<FieldDecl>(D) || isa<NamespaceDecl>(D)) {
    EntityInfo.name = nullptr; // anonymous tag/field/namespace.

  } else {
    SmallString<256> StrBuf;
    {
      llvm::raw_svector_ostream OS(StrBuf);
      D->printName(OS);
    }
    EntityInfo.name = SA.copyCStr(StrBuf.str());
  }

  {
    SmallString<512> StrBuf;
    bool Ignore = getDeclCursorUSR(D, StrBuf);
    if (Ignore) {
      EntityInfo.USR = nullptr;
    } else {
      EntityInfo.USR = SA.copyCStr(StrBuf.str());
    }
  }
}

void CXIndexDataConsumer::getContainerInfo(const DeclContext *DC,
                                       ContainerInfo &ContInfo) {
  ContInfo.cursor = getCursor(cast<Decl>(DC));
  ContInfo.DC = DC;
  ContInfo.IndexCtx = this;
}

CXCursor CXIndexDataConsumer::getRefCursor(const NamedDecl *D, SourceLocation Loc) {
  if (const TypeDecl *TD = dyn_cast<TypeDecl>(D))
    return MakeCursorTypeRef(TD, Loc, CXTU);
  if (const ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(D))
    return MakeCursorObjCClassRef(ID, Loc, CXTU);
  if (const ObjCProtocolDecl *PD = dyn_cast<ObjCProtocolDecl>(D))
    return MakeCursorObjCProtocolRef(PD, Loc, CXTU);
  if (const TemplateDecl *Template = dyn_cast<TemplateDecl>(D))
    return MakeCursorTemplateRef(Template, Loc, CXTU);
  if (const NamespaceDecl *Namespace = dyn_cast<NamespaceDecl>(D))
    return MakeCursorNamespaceRef(Namespace, Loc, CXTU);
  if (const NamespaceAliasDecl *Namespace = dyn_cast<NamespaceAliasDecl>(D))
    return MakeCursorNamespaceRef(Namespace, Loc, CXTU);
  if (const FieldDecl *Field = dyn_cast<FieldDecl>(D))
    return MakeCursorMemberRef(Field, Loc, CXTU);
  if (const VarDecl *Var = dyn_cast<VarDecl>(D))
    return MakeCursorVariableRef(Var, Loc, CXTU);
  
  return clang_getNullCursor();
}

bool CXIndexDataConsumer::shouldIgnoreIfImplicit(const Decl *D) {
  if (isa<ObjCInterfaceDecl>(D))
    return false;
  if (isa<ObjCCategoryDecl>(D))
    return false;
  if (isa<ObjCIvarDecl>(D))
    return false;
  if (isa<ObjCMethodDecl>(D))
    return false;
  if (isa<ImportDecl>(D))
    return false;
  return true;
}

bool CXIndexDataConsumer::isTemplateImplicitInstantiation(const Decl *D) {
  if (const ClassTemplateSpecializationDecl *
        SD = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    return SD->getSpecializationKind() == TSK_ImplicitInstantiation;
  }
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    return FD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation;
  }
  return false;
}

static CXIdxEntityKind getEntityKindFromSymbolKind(SymbolKind K, SymbolLanguage Lang) {
  switch (K) {
  case SymbolKind::Unknown:
  case SymbolKind::Module:
  case SymbolKind::Macro:
  case SymbolKind::ClassProperty:
  case SymbolKind::Using:
  case SymbolKind::TemplateTypeParm:
  case SymbolKind::TemplateTemplateParm:
  case SymbolKind::NonTypeTemplateParm:
    return CXIdxEntity_Unexposed;

  case SymbolKind::Enum: return CXIdxEntity_Enum;
  case SymbolKind::Struct: return CXIdxEntity_Struct;
  case SymbolKind::Union: return CXIdxEntity_Union;
  case SymbolKind::TypeAlias:
    if (Lang == SymbolLanguage::CXX)
      return CXIdxEntity_CXXTypeAlias;
    return CXIdxEntity_Typedef;
  case SymbolKind::Function: return CXIdxEntity_Function;
  case SymbolKind::Variable: return CXIdxEntity_Variable;
  case SymbolKind::Field:
    if (Lang == SymbolLanguage::ObjC)
      return CXIdxEntity_ObjCIvar;
    return CXIdxEntity_Field;
  case SymbolKind::EnumConstant: return CXIdxEntity_EnumConstant;
  case SymbolKind::Class:
    if (Lang == SymbolLanguage::ObjC)
      return CXIdxEntity_ObjCClass;
    return CXIdxEntity_CXXClass;
  case SymbolKind::Protocol:
    if (Lang == SymbolLanguage::ObjC)
      return CXIdxEntity_ObjCProtocol;
    return CXIdxEntity_CXXInterface;
  case SymbolKind::Extension: return CXIdxEntity_ObjCCategory;
  case SymbolKind::InstanceMethod:
    if (Lang == SymbolLanguage::ObjC)
      return CXIdxEntity_ObjCInstanceMethod;
    return CXIdxEntity_CXXInstanceMethod;
  case SymbolKind::ClassMethod: return CXIdxEntity_ObjCClassMethod;
  case SymbolKind::StaticMethod: return CXIdxEntity_CXXStaticMethod;
  case SymbolKind::InstanceProperty: return CXIdxEntity_ObjCProperty;
  case SymbolKind::StaticProperty: return CXIdxEntity_CXXStaticVariable;
  case SymbolKind::Namespace: return CXIdxEntity_CXXNamespace;
  case SymbolKind::NamespaceAlias: return CXIdxEntity_CXXNamespaceAlias;
  case SymbolKind::Constructor: return CXIdxEntity_CXXConstructor;
  case SymbolKind::Destructor: return CXIdxEntity_CXXDestructor;
  case SymbolKind::ConversionFunction: return CXIdxEntity_CXXConversionFunction;
  case SymbolKind::Parameter: return CXIdxEntity_Variable;
  case SymbolKind::Concept:
    return CXIdxEntity_CXXConcept;
  }
  llvm_unreachable("invalid symbol kind");
}

static CXIdxEntityCXXTemplateKind
getEntityKindFromSymbolProperties(SymbolPropertySet K) {
  if (K & (SymbolPropertySet)SymbolProperty::TemplatePartialSpecialization)
    return CXIdxEntity_TemplatePartialSpecialization;
  if (K & (SymbolPropertySet)SymbolProperty::TemplateSpecialization)
    return CXIdxEntity_TemplateSpecialization;
  if (K & (SymbolPropertySet)SymbolProperty::Generic)
    return CXIdxEntity_Template;
  return CXIdxEntity_NonTemplate;
}

static CXIdxEntityLanguage getEntityLangFromSymbolLang(SymbolLanguage L) {
  switch (L) {
  case SymbolLanguage::C: return CXIdxEntityLang_C;
  case SymbolLanguage::ObjC: return CXIdxEntityLang_ObjC;
  case SymbolLanguage::CXX: return CXIdxEntityLang_CXX;
  case SymbolLanguage::Swift: return CXIdxEntityLang_Swift;
  }
  llvm_unreachable("invalid symbol language");
}
