//===--- RewriteObjC.cpp - Playground for the code rewriter ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hacks and fun related to the code rewriter.
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Frontend/ASTConsumers.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMap.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Config/config.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

#if CLANG_ENABLE_OBJC_REWRITER

using namespace clang;
using llvm::utostr;

namespace {
  class RewriteObjC : public ASTConsumer {
  protected:
    enum {
      BLOCK_FIELD_IS_OBJECT   =  3,  /* id, NSObject, __attribute__((NSObject)),
                                        block, ... */
      BLOCK_FIELD_IS_BLOCK    =  7,  /* a block variable */
      BLOCK_FIELD_IS_BYREF    =  8,  /* the on stack structure holding the
                                        __block variable */
      BLOCK_FIELD_IS_WEAK     = 16,  /* declared __weak, only used in byref copy
                                        helpers */
      BLOCK_BYREF_CALLER      = 128, /* called from __block (byref) copy/dispose
                                        support routines */
      BLOCK_BYREF_CURRENT_MAX = 256
    };

    enum {
      BLOCK_NEEDS_FREE =        (1 << 24),
      BLOCK_HAS_COPY_DISPOSE =  (1 << 25),
      BLOCK_HAS_CXX_OBJ =       (1 << 26),
      BLOCK_IS_GC =             (1 << 27),
      BLOCK_IS_GLOBAL =         (1 << 28),
      BLOCK_HAS_DESCRIPTOR =    (1 << 29)
    };
    static const int OBJC_ABI_VERSION = 7;

    Rewriter Rewrite;
    DiagnosticsEngine &Diags;
    const LangOptions &LangOpts;
    ASTContext *Context;
    SourceManager *SM;
    TranslationUnitDecl *TUDecl;
    FileID MainFileID;
    const char *MainFileStart, *MainFileEnd;
    Stmt *CurrentBody;
    ParentMap *PropParentMap; // created lazily.
    std::string InFileName;
    std::unique_ptr<raw_ostream> OutFile;
    std::string Preamble;

    TypeDecl *ProtocolTypeDecl;
    VarDecl *GlobalVarDecl;
    unsigned RewriteFailedDiag;
    // ObjC string constant support.
    unsigned NumObjCStringLiterals;
    VarDecl *ConstantStringClassReference;
    RecordDecl *NSStringRecord;

    // ObjC foreach break/continue generation support.
    int BcLabelCount;

    unsigned TryFinallyContainsReturnDiag;
    // Needed for super.
    ObjCMethodDecl *CurMethodDef;
    RecordDecl *SuperStructDecl;
    RecordDecl *ConstantStringDecl;

    FunctionDecl *MsgSendFunctionDecl;
    FunctionDecl *MsgSendSuperFunctionDecl;
    FunctionDecl *MsgSendStretFunctionDecl;
    FunctionDecl *MsgSendSuperStretFunctionDecl;
    FunctionDecl *MsgSendFpretFunctionDecl;
    FunctionDecl *GetClassFunctionDecl;
    FunctionDecl *GetMetaClassFunctionDecl;
    FunctionDecl *GetSuperClassFunctionDecl;
    FunctionDecl *SelGetUidFunctionDecl;
    FunctionDecl *CFStringFunctionDecl;
    FunctionDecl *SuperConstructorFunctionDecl;
    FunctionDecl *CurFunctionDef;
    FunctionDecl *CurFunctionDeclToDeclareForBlock;

    /* Misc. containers needed for meta-data rewrite. */
    SmallVector<ObjCImplementationDecl *, 8> ClassImplementation;
    SmallVector<ObjCCategoryImplDecl *, 8> CategoryImplementation;
    llvm::SmallPtrSet<ObjCInterfaceDecl*, 8> ObjCSynthesizedStructs;
    llvm::SmallPtrSet<ObjCProtocolDecl*, 8> ObjCSynthesizedProtocols;
    llvm::SmallPtrSet<ObjCInterfaceDecl*, 8> ObjCForwardDecls;
    llvm::DenseMap<ObjCMethodDecl*, std::string> MethodInternalNames;
    SmallVector<Stmt *, 32> Stmts;
    SmallVector<int, 8> ObjCBcLabelNo;
    // Remember all the @protocol(<expr>) expressions.
    llvm::SmallPtrSet<ObjCProtocolDecl *, 32> ProtocolExprDecls;

    llvm::DenseSet<uint64_t> CopyDestroyCache;

    // Block expressions.
    SmallVector<BlockExpr *, 32> Blocks;
    SmallVector<int, 32> InnerDeclRefsCount;
    SmallVector<DeclRefExpr *, 32> InnerDeclRefs;

    SmallVector<DeclRefExpr *, 32> BlockDeclRefs;

    // Block related declarations.
    SmallVector<ValueDecl *, 8> BlockByCopyDecls;
    llvm::SmallPtrSet<ValueDecl *, 8> BlockByCopyDeclsPtrSet;
    SmallVector<ValueDecl *, 8> BlockByRefDecls;
    llvm::SmallPtrSet<ValueDecl *, 8> BlockByRefDeclsPtrSet;
    llvm::DenseMap<ValueDecl *, unsigned> BlockByRefDeclNo;
    llvm::SmallPtrSet<ValueDecl *, 8> ImportedBlockDecls;
    llvm::SmallPtrSet<VarDecl *, 8> ImportedLocalExternalDecls;

    llvm::DenseMap<BlockExpr *, std::string> RewrittenBlockExprs;

    // This maps an original source AST to it's rewritten form. This allows
    // us to avoid rewriting the same node twice (which is very uncommon).
    // This is needed to support some of the exotic property rewriting.
    llvm::DenseMap<Stmt *, Stmt *> ReplacedNodes;

    // Needed for header files being rewritten
    bool IsHeader;
    bool SilenceRewriteMacroWarning;
    bool objc_impl_method;

    bool DisableReplaceStmt;
    class DisableReplaceStmtScope {
      RewriteObjC &R;
      bool SavedValue;

    public:
      DisableReplaceStmtScope(RewriteObjC &R)
        : R(R), SavedValue(R.DisableReplaceStmt) {
        R.DisableReplaceStmt = true;
      }

      ~DisableReplaceStmtScope() {
        R.DisableReplaceStmt = SavedValue;
      }
    };

    void InitializeCommon(ASTContext &context);

  public:
    // Top Level Driver code.
    bool HandleTopLevelDecl(DeclGroupRef D) override {
      for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I) {
        if (ObjCInterfaceDecl *Class = dyn_cast<ObjCInterfaceDecl>(*I)) {
          if (!Class->isThisDeclarationADefinition()) {
            RewriteForwardClassDecl(D);
            break;
          }
        }

        if (ObjCProtocolDecl *Proto = dyn_cast<ObjCProtocolDecl>(*I)) {
          if (!Proto->isThisDeclarationADefinition()) {
            RewriteForwardProtocolDecl(D);
            break;
          }
        }

        HandleTopLevelSingleDecl(*I);
      }
      return true;
    }

    void HandleTopLevelSingleDecl(Decl *D);
    void HandleDeclInMainFile(Decl *D);
    RewriteObjC(std::string inFile, std::unique_ptr<raw_ostream> OS,
                DiagnosticsEngine &D, const LangOptions &LOpts,
                bool silenceMacroWarn);

    ~RewriteObjC() override {}

    void HandleTranslationUnit(ASTContext &C) override;

    void ReplaceStmt(Stmt *Old, Stmt *New) {
      ReplaceStmtWithRange(Old, New, Old->getSourceRange());
    }

    void ReplaceStmtWithRange(Stmt *Old, Stmt *New, SourceRange SrcRange) {
      assert(Old != nullptr && New != nullptr && "Expected non-null Stmt's");

      Stmt *ReplacingStmt = ReplacedNodes[Old];
      if (ReplacingStmt)
        return; // We can't rewrite the same node twice.

      if (DisableReplaceStmt)
        return;

      // Measure the old text.
      int Size = Rewrite.getRangeSize(SrcRange);
      if (Size == -1) {
        Diags.Report(Context->getFullLoc(Old->getBeginLoc()), RewriteFailedDiag)
            << Old->getSourceRange();
        return;
      }
      // Get the new text.
      std::string SStr;
      llvm::raw_string_ostream S(SStr);
      New->printPretty(S, nullptr, PrintingPolicy(LangOpts));
      const std::string &Str = S.str();

      // If replacement succeeded or warning disabled return with no warning.
      if (!Rewrite.ReplaceText(SrcRange.getBegin(), Size, Str)) {
        ReplacedNodes[Old] = New;
        return;
      }
      if (SilenceRewriteMacroWarning)
        return;
      Diags.Report(Context->getFullLoc(Old->getBeginLoc()), RewriteFailedDiag)
          << Old->getSourceRange();
    }

    void InsertText(SourceLocation Loc, StringRef Str,
                    bool InsertAfter = true) {
      // If insertion succeeded or warning disabled return with no warning.
      if (!Rewrite.InsertText(Loc, Str, InsertAfter) ||
          SilenceRewriteMacroWarning)
        return;

      Diags.Report(Context->getFullLoc(Loc), RewriteFailedDiag);
    }

    void ReplaceText(SourceLocation Start, unsigned OrigLength,
                     StringRef Str) {
      // If removal succeeded or warning disabled return with no warning.
      if (!Rewrite.ReplaceText(Start, OrigLength, Str) ||
          SilenceRewriteMacroWarning)
        return;

      Diags.Report(Context->getFullLoc(Start), RewriteFailedDiag);
    }

    // Syntactic Rewriting.
    void RewriteRecordBody(RecordDecl *RD);
    void RewriteInclude();
    void RewriteForwardClassDecl(DeclGroupRef D);
    void RewriteForwardClassDecl(const SmallVectorImpl<Decl *> &DG);
    void RewriteForwardClassEpilogue(ObjCInterfaceDecl *ClassDecl,
                                     const std::string &typedefString);
    void RewriteImplementations();
    void RewritePropertyImplDecl(ObjCPropertyImplDecl *PID,
                                 ObjCImplementationDecl *IMD,
                                 ObjCCategoryImplDecl *CID);
    void RewriteInterfaceDecl(ObjCInterfaceDecl *Dcl);
    void RewriteImplementationDecl(Decl *Dcl);
    void RewriteObjCMethodDecl(const ObjCInterfaceDecl *IDecl,
                               ObjCMethodDecl *MDecl, std::string &ResultStr);
    void RewriteTypeIntoString(QualType T, std::string &ResultStr,
                               const FunctionType *&FPRetType);
    void RewriteByRefString(std::string &ResultStr, const std::string &Name,
                            ValueDecl *VD, bool def=false);
    void RewriteCategoryDecl(ObjCCategoryDecl *Dcl);
    void RewriteProtocolDecl(ObjCProtocolDecl *Dcl);
    void RewriteForwardProtocolDecl(DeclGroupRef D);
    void RewriteForwardProtocolDecl(const SmallVectorImpl<Decl *> &DG);
    void RewriteMethodDeclaration(ObjCMethodDecl *Method);
    void RewriteProperty(ObjCPropertyDecl *prop);
    void RewriteFunctionDecl(FunctionDecl *FD);
    void RewriteBlockPointerType(std::string& Str, QualType Type);
    void RewriteBlockPointerTypeVariable(std::string& Str, ValueDecl *VD);
    void RewriteBlockLiteralFunctionDecl(FunctionDecl *FD);
    void RewriteObjCQualifiedInterfaceTypes(Decl *Dcl);
    void RewriteTypeOfDecl(VarDecl *VD);
    void RewriteObjCQualifiedInterfaceTypes(Expr *E);

    // Expression Rewriting.
    Stmt *RewriteFunctionBodyOrGlobalInitializer(Stmt *S);
    Stmt *RewriteAtEncode(ObjCEncodeExpr *Exp);
    Stmt *RewritePropertyOrImplicitGetter(PseudoObjectExpr *Pseudo);
    Stmt *RewritePropertyOrImplicitSetter(PseudoObjectExpr *Pseudo);
    Stmt *RewriteAtSelector(ObjCSelectorExpr *Exp);
    Stmt *RewriteMessageExpr(ObjCMessageExpr *Exp);
    Stmt *RewriteObjCStringLiteral(ObjCStringLiteral *Exp);
    Stmt *RewriteObjCProtocolExpr(ObjCProtocolExpr *Exp);
    void RewriteTryReturnStmts(Stmt *S);
    void RewriteSyncReturnStmts(Stmt *S, std::string buf);
    Stmt *RewriteObjCTryStmt(ObjCAtTryStmt *S);
    Stmt *RewriteObjCSynchronizedStmt(ObjCAtSynchronizedStmt *S);
    Stmt *RewriteObjCThrowStmt(ObjCAtThrowStmt *S);
    Stmt *RewriteObjCForCollectionStmt(ObjCForCollectionStmt *S,
                                       SourceLocation OrigEnd);
    Stmt *RewriteBreakStmt(BreakStmt *S);
    Stmt *RewriteContinueStmt(ContinueStmt *S);
    void RewriteCastExpr(CStyleCastExpr *CE);

    // Block rewriting.
    void RewriteBlocksInFunctionProtoType(QualType funcType, NamedDecl *D);

    // Block specific rewrite rules.
    void RewriteBlockPointerDecl(NamedDecl *VD);
    void RewriteByRefVar(VarDecl *VD);
    Stmt *RewriteBlockDeclRefExpr(DeclRefExpr *VD);
    Stmt *RewriteLocalVariableExternalStorage(DeclRefExpr *DRE);
    void RewriteBlockPointerFunctionArgs(FunctionDecl *FD);

    void RewriteObjCInternalStruct(ObjCInterfaceDecl *CDecl,
                                      std::string &Result);

    void Initialize(ASTContext &context) override = 0;

    // Metadata Rewriting.
    virtual void RewriteMetaDataIntoBuffer(std::string &Result) = 0;
    virtual void RewriteObjCProtocolListMetaData(const ObjCList<ObjCProtocolDecl> &Prots,
                                                 StringRef prefix,
                                                 StringRef ClassName,
                                                 std::string &Result) = 0;
    virtual void RewriteObjCCategoryImplDecl(ObjCCategoryImplDecl *CDecl,
                                             std::string &Result) = 0;
    virtual void RewriteObjCProtocolMetaData(ObjCProtocolDecl *Protocol,
                                     StringRef prefix,
                                     StringRef ClassName,
                                     std::string &Result) = 0;
    virtual void RewriteObjCClassMetaData(ObjCImplementationDecl *IDecl,
                                          std::string &Result) = 0;

    // Rewriting ivar access
    virtual Stmt *RewriteObjCIvarRefExpr(ObjCIvarRefExpr *IV) = 0;
    virtual void RewriteIvarOffsetComputation(ObjCIvarDecl *ivar,
                                         std::string &Result) = 0;

    // Misc. AST transformation routines. Sometimes they end up calling
    // rewriting routines on the new ASTs.
    CallExpr *SynthesizeCallToFunctionDecl(FunctionDecl *FD,
                                           ArrayRef<Expr *> Args,
                                           SourceLocation StartLoc=SourceLocation(),
                                           SourceLocation EndLoc=SourceLocation());
    CallExpr *SynthMsgSendStretCallExpr(FunctionDecl *MsgSendStretFlavor,
                                        QualType msgSendType,
                                        QualType returnType,
                                        SmallVectorImpl<QualType> &ArgTypes,
                                        SmallVectorImpl<Expr*> &MsgExprs,
                                        ObjCMethodDecl *Method);
    Stmt *SynthMessageExpr(ObjCMessageExpr *Exp,
                           SourceLocation StartLoc=SourceLocation(),
                           SourceLocation EndLoc=SourceLocation());

    void SynthCountByEnumWithState(std::string &buf);
    void SynthMsgSendFunctionDecl();
    void SynthMsgSendSuperFunctionDecl();
    void SynthMsgSendStretFunctionDecl();
    void SynthMsgSendFpretFunctionDecl();
    void SynthMsgSendSuperStretFunctionDecl();
    void SynthGetClassFunctionDecl();
    void SynthGetMetaClassFunctionDecl();
    void SynthGetSuperClassFunctionDecl();
    void SynthSelGetUidFunctionDecl();
    void SynthSuperConstructorFunctionDecl();

    std::string SynthesizeByrefCopyDestroyHelper(VarDecl *VD, int flag);
    std::string SynthesizeBlockHelperFuncs(BlockExpr *CE, int i,
                                      StringRef funcName, std::string Tag);
    std::string SynthesizeBlockFunc(BlockExpr *CE, int i,
                                      StringRef funcName, std::string Tag);
    std::string SynthesizeBlockImpl(BlockExpr *CE,
                                    std::string Tag, std::string Desc);
    std::string SynthesizeBlockDescriptor(std::string DescTag,
                                          std::string ImplTag,
                                          int i, StringRef funcName,
                                          unsigned hasCopy);
    Stmt *SynthesizeBlockCall(CallExpr *Exp, const Expr* BlockExp);
    void SynthesizeBlockLiterals(SourceLocation FunLocStart,
                                 StringRef FunName);
    FunctionDecl *SynthBlockInitFunctionDecl(StringRef name);
    Stmt *SynthBlockInitExpr(BlockExpr *Exp,
            const SmallVectorImpl<DeclRefExpr *> &InnerBlockDeclRefs);

    // Misc. helper routines.
    QualType getProtocolType();
    void WarnAboutReturnGotoStmts(Stmt *S);
    void HasReturnStmts(Stmt *S, bool &hasReturns);
    void CheckFunctionPointerDecl(QualType dType, NamedDecl *ND);
    void InsertBlockLiteralsWithinFunction(FunctionDecl *FD);
    void InsertBlockLiteralsWithinMethod(ObjCMethodDecl *MD);

    bool IsDeclStmtInForeachHeader(DeclStmt *DS);
    void CollectBlockDeclRefInfo(BlockExpr *Exp);
    void GetBlockDeclRefExprs(Stmt *S);
    void GetInnerBlockDeclRefExprs(Stmt *S,
                SmallVectorImpl<DeclRefExpr *> &InnerBlockDeclRefs,
                llvm::SmallPtrSetImpl<const DeclContext *> &InnerContexts);

    // We avoid calling Type::isBlockPointerType(), since it operates on the
    // canonical type. We only care if the top-level type is a closure pointer.
    bool isTopLevelBlockPointerType(QualType T) {
      return isa<BlockPointerType>(T);
    }

    /// convertBlockPointerToFunctionPointer - Converts a block-pointer type
    /// to a function pointer type and upon success, returns true; false
    /// otherwise.
    bool convertBlockPointerToFunctionPointer(QualType &T) {
      if (isTopLevelBlockPointerType(T)) {
        const auto *BPT = T->castAs<BlockPointerType>();
        T = Context->getPointerType(BPT->getPointeeType());
        return true;
      }
      return false;
    }

    bool needToScanForQualifiers(QualType T);
    QualType getSuperStructType();
    QualType getConstantStringStructType();
    QualType convertFunctionTypeOfBlocks(const FunctionType *FT);
    bool BufferContainsPPDirectives(const char *startBuf, const char *endBuf);

    void convertToUnqualifiedObjCType(QualType &T) {
      if (T->isObjCQualifiedIdType())
        T = Context->getObjCIdType();
      else if (T->isObjCQualifiedClassType())
        T = Context->getObjCClassType();
      else if (T->isObjCObjectPointerType() &&
               T->getPointeeType()->isObjCQualifiedInterfaceType()) {
        if (const ObjCObjectPointerType * OBJPT =
              T->getAsObjCInterfacePointerType()) {
          const ObjCInterfaceType *IFaceT = OBJPT->getInterfaceType();
          T = QualType(IFaceT, 0);
          T = Context->getPointerType(T);
        }
     }
    }

    // FIXME: This predicate seems like it would be useful to add to ASTContext.
    bool isObjCType(QualType T) {
      if (!LangOpts.ObjC)
        return false;

      QualType OCT = Context->getCanonicalType(T).getUnqualifiedType();

      if (OCT == Context->getCanonicalType(Context->getObjCIdType()) ||
          OCT == Context->getCanonicalType(Context->getObjCClassType()))
        return true;

      if (const PointerType *PT = OCT->getAs<PointerType>()) {
        if (isa<ObjCInterfaceType>(PT->getPointeeType()) ||
            PT->getPointeeType()->isObjCQualifiedIdType())
          return true;
      }
      return false;
    }
    bool PointerTypeTakesAnyBlockArguments(QualType QT);
    bool PointerTypeTakesAnyObjCQualifiedType(QualType QT);
    void GetExtentOfArgList(const char *Name, const char *&LParen,
                            const char *&RParen);

    void QuoteDoublequotes(std::string &From, std::string &To) {
      for (unsigned i = 0; i < From.length(); i++) {
        if (From[i] == '"')
          To += "\\\"";
        else
          To += From[i];
      }
    }

    QualType getSimpleFunctionType(QualType result,
                                   ArrayRef<QualType> args,
                                   bool variadic = false) {
      if (result == Context->getObjCInstanceType())
        result =  Context->getObjCIdType();
      FunctionProtoType::ExtProtoInfo fpi;
      fpi.Variadic = variadic;
      return Context->getFunctionType(result, args, fpi);
    }

    // Helper function: create a CStyleCastExpr with trivial type source info.
    CStyleCastExpr* NoTypeInfoCStyleCastExpr(ASTContext *Ctx, QualType Ty,
                                             CastKind Kind, Expr *E) {
      TypeSourceInfo *TInfo = Ctx->getTrivialTypeSourceInfo(Ty, SourceLocation());
      return CStyleCastExpr::Create(*Ctx, Ty, VK_PRValue, Kind, E, nullptr,
                                    FPOptionsOverride(), TInfo,
                                    SourceLocation(), SourceLocation());
    }

    StringLiteral *getStringLiteral(StringRef Str) {
      QualType StrType = Context->getConstantArrayType(
          Context->CharTy, llvm::APInt(32, Str.size() + 1), nullptr,
          ArraySizeModifier::Normal, 0);
      return StringLiteral::Create(*Context, Str, StringLiteralKind::Ordinary,
                                   /*Pascal=*/false, StrType, SourceLocation());
    }
  };

  class RewriteObjCFragileABI : public RewriteObjC {
  public:
    RewriteObjCFragileABI(std::string inFile, std::unique_ptr<raw_ostream> OS,
                          DiagnosticsEngine &D, const LangOptions &LOpts,
                          bool silenceMacroWarn)
        : RewriteObjC(inFile, std::move(OS), D, LOpts, silenceMacroWarn) {}

    ~RewriteObjCFragileABI() override {}
    void Initialize(ASTContext &context) override;

    // Rewriting metadata
    template<typename MethodIterator>
    void RewriteObjCMethodsMetaData(MethodIterator MethodBegin,
                                    MethodIterator MethodEnd,
                                    bool IsInstanceMethod,
                                    StringRef prefix,
                                    StringRef ClassName,
                                    std::string &Result);
    void RewriteObjCProtocolMetaData(ObjCProtocolDecl *Protocol,
                                     StringRef prefix, StringRef ClassName,
                                     std::string &Result) override;
    void RewriteObjCProtocolListMetaData(
          const ObjCList<ObjCProtocolDecl> &Prots,
          StringRef prefix, StringRef ClassName, std::string &Result) override;
    void RewriteObjCClassMetaData(ObjCImplementationDecl *IDecl,
                                  std::string &Result) override;
    void RewriteMetaDataIntoBuffer(std::string &Result) override;
    void RewriteObjCCategoryImplDecl(ObjCCategoryImplDecl *CDecl,
                                     std::string &Result) override;

    // Rewriting ivar
    void RewriteIvarOffsetComputation(ObjCIvarDecl *ivar,
                                      std::string &Result) override;
    Stmt *RewriteObjCIvarRefExpr(ObjCIvarRefExpr *IV) override;
  };
} // end anonymous namespace

void RewriteObjC::RewriteBlocksInFunctionProtoType(QualType funcType,
                                                   NamedDecl *D) {
  if (const FunctionProtoType *fproto
      = dyn_cast<FunctionProtoType>(funcType.IgnoreParens())) {
    for (const auto &I : fproto->param_types())
      if (isTopLevelBlockPointerType(I)) {
        // All the args are checked/rewritten. Don't call twice!
        RewriteBlockPointerDecl(D);
        break;
      }
  }
}

void RewriteObjC::CheckFunctionPointerDecl(QualType funcType, NamedDecl *ND) {
  const PointerType *PT = funcType->getAs<PointerType>();
  if (PT && PointerTypeTakesAnyBlockArguments(funcType))
    RewriteBlocksInFunctionProtoType(PT->getPointeeType(), ND);
}

static bool IsHeaderFile(const std::string &Filename) {
  std::string::size_type DotPos = Filename.rfind('.');

  if (DotPos == std::string::npos) {
    // no file extension
    return false;
  }

  std::string Ext = Filename.substr(DotPos + 1);
  // C header: .h
  // C++ header: .hh or .H;
  return Ext == "h" || Ext == "hh" || Ext == "H";
}

RewriteObjC::RewriteObjC(std::string inFile, std::unique_ptr<raw_ostream> OS,
                         DiagnosticsEngine &D, const LangOptions &LOpts,
                         bool silenceMacroWarn)
    : Diags(D), LangOpts(LOpts), InFileName(inFile), OutFile(std::move(OS)),
      SilenceRewriteMacroWarning(silenceMacroWarn) {
  IsHeader = IsHeaderFile(inFile);
  RewriteFailedDiag = Diags.getCustomDiagID(DiagnosticsEngine::Warning,
               "rewriting sub-expression within a macro (may not be correct)");
  TryFinallyContainsReturnDiag = Diags.getCustomDiagID(
               DiagnosticsEngine::Warning,
               "rewriter doesn't support user-specified control flow semantics "
               "for @try/@finally (code may not execute properly)");
}

std::unique_ptr<ASTConsumer>
clang::CreateObjCRewriter(const std::string &InFile,
                          std::unique_ptr<raw_ostream> OS,
                          DiagnosticsEngine &Diags, const LangOptions &LOpts,
                          bool SilenceRewriteMacroWarning) {
  return std::make_unique<RewriteObjCFragileABI>(
      InFile, std::move(OS), Diags, LOpts, SilenceRewriteMacroWarning);
}

void RewriteObjC::InitializeCommon(ASTContext &context) {
  Context = &context;
  SM = &Context->getSourceManager();
  TUDecl = Context->getTranslationUnitDecl();
  MsgSendFunctionDecl = nullptr;
  MsgSendSuperFunctionDecl = nullptr;
  MsgSendStretFunctionDecl = nullptr;
  MsgSendSuperStretFunctionDecl = nullptr;
  MsgSendFpretFunctionDecl = nullptr;
  GetClassFunctionDecl = nullptr;
  GetMetaClassFunctionDecl = nullptr;
  GetSuperClassFunctionDecl = nullptr;
  SelGetUidFunctionDecl = nullptr;
  CFStringFunctionDecl = nullptr;
  ConstantStringClassReference = nullptr;
  NSStringRecord = nullptr;
  CurMethodDef = nullptr;
  CurFunctionDef = nullptr;
  CurFunctionDeclToDeclareForBlock = nullptr;
  GlobalVarDecl = nullptr;
  SuperStructDecl = nullptr;
  ProtocolTypeDecl = nullptr;
  ConstantStringDecl = nullptr;
  BcLabelCount = 0;
  SuperConstructorFunctionDecl = nullptr;
  NumObjCStringLiterals = 0;
  PropParentMap = nullptr;
  CurrentBody = nullptr;
  DisableReplaceStmt = false;
  objc_impl_method = false;

  // Get the ID and start/end of the main file.
  MainFileID = SM->getMainFileID();
  llvm::MemoryBufferRef MainBuf = SM->getBufferOrFake(MainFileID);
  MainFileStart = MainBuf.getBufferStart();
  MainFileEnd = MainBuf.getBufferEnd();

  Rewrite.setSourceMgr(Context->getSourceManager(), Context->getLangOpts());
}

//===----------------------------------------------------------------------===//
// Top Level Driver Code
//===----------------------------------------------------------------------===//

void RewriteObjC::HandleTopLevelSingleDecl(Decl *D) {
  if (Diags.hasErrorOccurred())
    return;

  // Two cases: either the decl could be in the main file, or it could be in a
  // #included file.  If the former, rewrite it now.  If the later, check to see
  // if we rewrote the #include/#import.
  SourceLocation Loc = D->getLocation();
  Loc = SM->getExpansionLoc(Loc);

  // If this is for a builtin, ignore it.
  if (Loc.isInvalid()) return;

  // Look for built-in declarations that we need to refer during the rewrite.
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    RewriteFunctionDecl(FD);
  } else if (VarDecl *FVD = dyn_cast<VarDecl>(D)) {
    // declared in <Foundation/NSString.h>
    if (FVD->getName() == "_NSConstantStringClassReference") {
      ConstantStringClassReference = FVD;
      return;
    }
  } else if (ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(D)) {
    if (ID->isThisDeclarationADefinition())
      RewriteInterfaceDecl(ID);
  } else if (ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(D)) {
    RewriteCategoryDecl(CD);
  } else if (ObjCProtocolDecl *PD = dyn_cast<ObjCProtocolDecl>(D)) {
    if (PD->isThisDeclarationADefinition())
      RewriteProtocolDecl(PD);
  } else if (LinkageSpecDecl *LSD = dyn_cast<LinkageSpecDecl>(D)) {
    // Recurse into linkage specifications
    for (DeclContext::decl_iterator DI = LSD->decls_begin(),
                                 DIEnd = LSD->decls_end();
         DI != DIEnd; ) {
      if (ObjCInterfaceDecl *IFace = dyn_cast<ObjCInterfaceDecl>((*DI))) {
        if (!IFace->isThisDeclarationADefinition()) {
          SmallVector<Decl *, 8> DG;
          SourceLocation StartLoc = IFace->getBeginLoc();
          do {
            if (isa<ObjCInterfaceDecl>(*DI) &&
                !cast<ObjCInterfaceDecl>(*DI)->isThisDeclarationADefinition() &&
                StartLoc == (*DI)->getBeginLoc())
              DG.push_back(*DI);
            else
              break;

            ++DI;
          } while (DI != DIEnd);
          RewriteForwardClassDecl(DG);
          continue;
        }
      }

      if (ObjCProtocolDecl *Proto = dyn_cast<ObjCProtocolDecl>((*DI))) {
        if (!Proto->isThisDeclarationADefinition()) {
          SmallVector<Decl *, 8> DG;
          SourceLocation StartLoc = Proto->getBeginLoc();
          do {
            if (isa<ObjCProtocolDecl>(*DI) &&
                !cast<ObjCProtocolDecl>(*DI)->isThisDeclarationADefinition() &&
                StartLoc == (*DI)->getBeginLoc())
              DG.push_back(*DI);
            else
              break;

            ++DI;
          } while (DI != DIEnd);
          RewriteForwardProtocolDecl(DG);
          continue;
        }
      }

      HandleTopLevelSingleDecl(*DI);
      ++DI;
    }
  }
  // If we have a decl in the main file, see if we should rewrite it.
  if (SM->isWrittenInMainFile(Loc))
    return HandleDeclInMainFile(D);
}

//===----------------------------------------------------------------------===//
// Syntactic (non-AST) Rewriting Code
//===----------------------------------------------------------------------===//

void RewriteObjC::RewriteInclude() {
  SourceLocation LocStart = SM->getLocForStartOfFile(MainFileID);
  StringRef MainBuf = SM->getBufferData(MainFileID);
  const char *MainBufStart = MainBuf.begin();
  const char *MainBufEnd = MainBuf.end();
  size_t ImportLen = strlen("import");

  // Loop over the whole file, looking for includes.
  for (const char *BufPtr = MainBufStart; BufPtr < MainBufEnd; ++BufPtr) {
    if (*BufPtr == '#') {
      if (++BufPtr == MainBufEnd)
        return;
      while (*BufPtr == ' ' || *BufPtr == '\t')
        if (++BufPtr == MainBufEnd)
          return;
      if (!strncmp(BufPtr, "import", ImportLen)) {
        // replace import with include
        SourceLocation ImportLoc =
          LocStart.getLocWithOffset(BufPtr-MainBufStart);
        ReplaceText(ImportLoc, ImportLen, "include");
        BufPtr += ImportLen;
      }
    }
  }
}

static std::string getIvarAccessString(ObjCIvarDecl *OID) {
  const ObjCInterfaceDecl *ClassDecl = OID->getContainingInterface();
  std::string S;
  S = "((struct ";
  S += ClassDecl->getIdentifier()->getName();
  S += "_IMPL *)self)->";
  S += OID->getName();
  return S;
}

void RewriteObjC::RewritePropertyImplDecl(ObjCPropertyImplDecl *PID,
                                          ObjCImplementationDecl *IMD,
                                          ObjCCategoryImplDecl *CID) {
  static bool objcGetPropertyDefined = false;
  static bool objcSetPropertyDefined = false;
  SourceLocation startLoc = PID->getBeginLoc();
  InsertText(startLoc, "// ");
  const char *startBuf = SM->getCharacterData(startLoc);
  assert((*startBuf == '@') && "bogus @synthesize location");
  const char *semiBuf = strchr(startBuf, ';');
  assert((*semiBuf == ';') && "@synthesize: can't find ';'");
  SourceLocation onePastSemiLoc =
    startLoc.getLocWithOffset(semiBuf-startBuf+1);

  if (PID->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic)
    return; // FIXME: is this correct?

  // Generate the 'getter' function.
  ObjCPropertyDecl *PD = PID->getPropertyDecl();
  ObjCIvarDecl *OID = PID->getPropertyIvarDecl();

  if (!OID)
    return;

  unsigned Attributes = PD->getPropertyAttributes();
  if (PID->getGetterMethodDecl() && !PID->getGetterMethodDecl()->isDefined()) {
    bool GenGetProperty =
        !(Attributes & ObjCPropertyAttribute::kind_nonatomic) &&
        (Attributes & (ObjCPropertyAttribute::kind_retain |
                       ObjCPropertyAttribute::kind_copy));
    std::string Getr;
    if (GenGetProperty && !objcGetPropertyDefined) {
      objcGetPropertyDefined = true;
      // FIXME. Is this attribute correct in all cases?
      Getr = "\nextern \"C\" __declspec(dllimport) "
            "id objc_getProperty(id, SEL, long, bool);\n";
    }
    RewriteObjCMethodDecl(OID->getContainingInterface(),
                          PID->getGetterMethodDecl(), Getr);
    Getr += "{ ";
    // Synthesize an explicit cast to gain access to the ivar.
    // See objc-act.c:objc_synthesize_new_getter() for details.
    if (GenGetProperty) {
      // return objc_getProperty(self, _cmd, offsetof(ClassDecl, OID), 1)
      Getr += "typedef ";
      const FunctionType *FPRetType = nullptr;
      RewriteTypeIntoString(PID->getGetterMethodDecl()->getReturnType(), Getr,
                            FPRetType);
      Getr += " _TYPE";
      if (FPRetType) {
        Getr += ")"; // close the precedence "scope" for "*".

        // Now, emit the argument types (if any).
        if (const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(FPRetType)){
          Getr += "(";
          for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
            if (i) Getr += ", ";
            std::string ParamStr =
                FT->getParamType(i).getAsString(Context->getPrintingPolicy());
            Getr += ParamStr;
          }
          if (FT->isVariadic()) {
            if (FT->getNumParams())
              Getr += ", ";
            Getr += "...";
          }
          Getr += ")";
        } else
          Getr += "()";
      }
      Getr += ";\n";
      Getr += "return (_TYPE)";
      Getr += "objc_getProperty(self, _cmd, ";
      RewriteIvarOffsetComputation(OID, Getr);
      Getr += ", 1)";
    }
    else
      Getr += "return " + getIvarAccessString(OID);
    Getr += "; }";
    InsertText(onePastSemiLoc, Getr);
  }

  if (PD->isReadOnly() || !PID->getSetterMethodDecl() ||
      PID->getSetterMethodDecl()->isDefined())
    return;

  // Generate the 'setter' function.
  std::string Setr;
  bool GenSetProperty = Attributes & (ObjCPropertyAttribute::kind_retain |
                                      ObjCPropertyAttribute::kind_copy);
  if (GenSetProperty && !objcSetPropertyDefined) {
    objcSetPropertyDefined = true;
    // FIXME. Is this attribute correct in all cases?
    Setr = "\nextern \"C\" __declspec(dllimport) "
    "void objc_setProperty (id, SEL, long, id, bool, bool);\n";
  }

  RewriteObjCMethodDecl(OID->getContainingInterface(),
                        PID->getSetterMethodDecl(), Setr);
  Setr += "{ ";
  // Synthesize an explicit cast to initialize the ivar.
  // See objc-act.c:objc_synthesize_new_setter() for details.
  if (GenSetProperty) {
    Setr += "objc_setProperty (self, _cmd, ";
    RewriteIvarOffsetComputation(OID, Setr);
    Setr += ", (id)";
    Setr += PD->getName();
    Setr += ", ";
    if (Attributes & ObjCPropertyAttribute::kind_nonatomic)
      Setr += "0, ";
    else
      Setr += "1, ";
    if (Attributes & ObjCPropertyAttribute::kind_copy)
      Setr += "1)";
    else
      Setr += "0)";
  }
  else {
    Setr += getIvarAccessString(OID) + " = ";
    Setr += PD->getName();
  }
  Setr += "; }";
  InsertText(onePastSemiLoc, Setr);
}

static void RewriteOneForwardClassDecl(ObjCInterfaceDecl *ForwardDecl,
                                       std::string &typedefString) {
  typedefString += "#ifndef _REWRITER_typedef_";
  typedefString += ForwardDecl->getNameAsString();
  typedefString += "\n";
  typedefString += "#define _REWRITER_typedef_";
  typedefString += ForwardDecl->getNameAsString();
  typedefString += "\n";
  typedefString += "typedef struct objc_object ";
  typedefString += ForwardDecl->getNameAsString();
  typedefString += ";\n#endif\n";
}

void RewriteObjC::RewriteForwardClassEpilogue(ObjCInterfaceDecl *ClassDecl,
                                              const std::string &typedefString) {
  SourceLocation startLoc = ClassDecl->getBeginLoc();
  const char *startBuf = SM->getCharacterData(startLoc);
  const char *semiPtr = strchr(startBuf, ';');
  // Replace the @class with typedefs corresponding to the classes.
  ReplaceText(startLoc, semiPtr - startBuf + 1, typedefString);
}

void RewriteObjC::RewriteForwardClassDecl(DeclGroupRef D) {
  std::string typedefString;
  for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I) {
    ObjCInterfaceDecl *ForwardDecl = cast<ObjCInterfaceDecl>(*I);
    if (I == D.begin()) {
      // Translate to typedef's that forward reference structs with the same name
      // as the class. As a convenience, we include the original declaration
      // as a comment.
      typedefString += "// @class ";
      typedefString += ForwardDecl->getNameAsString();
      typedefString += ";\n";
    }
    RewriteOneForwardClassDecl(ForwardDecl, typedefString);
  }
  DeclGroupRef::iterator I = D.begin();
  RewriteForwardClassEpilogue(cast<ObjCInterfaceDecl>(*I), typedefString);
}

void RewriteObjC::RewriteForwardClassDecl(const SmallVectorImpl<Decl *> &D) {
  std::string typedefString;
  for (unsigned i = 0; i < D.size(); i++) {
    ObjCInterfaceDecl *ForwardDecl = cast<ObjCInterfaceDecl>(D[i]);
    if (i == 0) {
      typedefString += "// @class ";
      typedefString += ForwardDecl->getNameAsString();
      typedefString += ";\n";
    }
    RewriteOneForwardClassDecl(ForwardDecl, typedefString);
  }
  RewriteForwardClassEpilogue(cast<ObjCInterfaceDecl>(D[0]), typedefString);
}

void RewriteObjC::RewriteMethodDeclaration(ObjCMethodDecl *Method) {
  // When method is a synthesized one, such as a getter/setter there is
  // nothing to rewrite.
  if (Method->isImplicit())
    return;
  SourceLocation LocStart = Method->getBeginLoc();
  SourceLocation LocEnd = Method->getEndLoc();

  if (SM->getExpansionLineNumber(LocEnd) >
      SM->getExpansionLineNumber(LocStart)) {
    InsertText(LocStart, "#if 0\n");
    ReplaceText(LocEnd, 1, ";\n#endif\n");
  } else {
    InsertText(LocStart, "// ");
  }
}

void RewriteObjC::RewriteProperty(ObjCPropertyDecl *prop) {
  SourceLocation Loc = prop->getAtLoc();

  ReplaceText(Loc, 0, "// ");
  // FIXME: handle properties that are declared across multiple lines.
}

void RewriteObjC::RewriteCategoryDecl(ObjCCategoryDecl *CatDecl) {
  SourceLocation LocStart = CatDecl->getBeginLoc();

  // FIXME: handle category headers that are declared across multiple lines.
  ReplaceText(LocStart, 0, "// ");

  for (auto *I : CatDecl->instance_properties())
    RewriteProperty(I);
  for (auto *I : CatDecl->instance_methods())
    RewriteMethodDeclaration(I);
  for (auto *I : CatDecl->class_methods())
    RewriteMethodDeclaration(I);

  // Lastly, comment out the @end.
  ReplaceText(CatDecl->getAtEndRange().getBegin(),
              strlen("@end"), "/* @end */");
}

void RewriteObjC::RewriteProtocolDecl(ObjCProtocolDecl *PDecl) {
  SourceLocation LocStart = PDecl->getBeginLoc();
  assert(PDecl->isThisDeclarationADefinition());

  // FIXME: handle protocol headers that are declared across multiple lines.
  ReplaceText(LocStart, 0, "// ");

  for (auto *I : PDecl->instance_methods())
    RewriteMethodDeclaration(I);
  for (auto *I : PDecl->class_methods())
    RewriteMethodDeclaration(I);
  for (auto *I : PDecl->instance_properties())
    RewriteProperty(I);

  // Lastly, comment out the @end.
  SourceLocation LocEnd = PDecl->getAtEndRange().getBegin();
  ReplaceText(LocEnd, strlen("@end"), "/* @end */");

  // Must comment out @optional/@required
  const char *startBuf = SM->getCharacterData(LocStart);
  const char *endBuf = SM->getCharacterData(LocEnd);
  for (const char *p = startBuf; p < endBuf; p++) {
    if (*p == '@' && !strncmp(p+1, "optional", strlen("optional"))) {
      SourceLocation OptionalLoc = LocStart.getLocWithOffset(p-startBuf);
      ReplaceText(OptionalLoc, strlen("@optional"), "/* @optional */");

    }
    else if (*p == '@' && !strncmp(p+1, "required", strlen("required"))) {
      SourceLocation OptionalLoc = LocStart.getLocWithOffset(p-startBuf);
      ReplaceText(OptionalLoc, strlen("@required"), "/* @required */");

    }
  }
}

void RewriteObjC::RewriteForwardProtocolDecl(DeclGroupRef D) {
  SourceLocation LocStart = (*D.begin())->getBeginLoc();
  if (LocStart.isInvalid())
    llvm_unreachable("Invalid SourceLocation");
  // FIXME: handle forward protocol that are declared across multiple lines.
  ReplaceText(LocStart, 0, "// ");
}

void
RewriteObjC::RewriteForwardProtocolDecl(const SmallVectorImpl<Decl *> &DG) {
  SourceLocation LocStart = DG[0]->getBeginLoc();
  if (LocStart.isInvalid())
    llvm_unreachable("Invalid SourceLocation");
  // FIXME: handle forward protocol that are declared across multiple lines.
  ReplaceText(LocStart, 0, "// ");
}

void RewriteObjC::RewriteTypeIntoString(QualType T, std::string &ResultStr,
                                        const FunctionType *&FPRetType) {
  if (T->isObjCQualifiedIdType())
    ResultStr += "id";
  else if (T->isFunctionPointerType() ||
           T->isBlockPointerType()) {
    // needs special handling, since pointer-to-functions have special
    // syntax (where a decaration models use).
    QualType retType = T;
    QualType PointeeTy;
    if (const PointerType* PT = retType->getAs<PointerType>())
      PointeeTy = PT->getPointeeType();
    else if (const BlockPointerType *BPT = retType->getAs<BlockPointerType>())
      PointeeTy = BPT->getPointeeType();
    if ((FPRetType = PointeeTy->getAs<FunctionType>())) {
      ResultStr +=
          FPRetType->getReturnType().getAsString(Context->getPrintingPolicy());
      ResultStr += "(*";
    }
  } else
    ResultStr += T.getAsString(Context->getPrintingPolicy());
}

void RewriteObjC::RewriteObjCMethodDecl(const ObjCInterfaceDecl *IDecl,
                                        ObjCMethodDecl *OMD,
                                        std::string &ResultStr) {
  //fprintf(stderr,"In RewriteObjCMethodDecl\n");
  const FunctionType *FPRetType = nullptr;
  ResultStr += "\nstatic ";
  RewriteTypeIntoString(OMD->getReturnType(), ResultStr, FPRetType);
  ResultStr += " ";

  // Unique method name
  std::string NameStr;

  if (OMD->isInstanceMethod())
    NameStr += "_I_";
  else
    NameStr += "_C_";

  NameStr += IDecl->getNameAsString();
  NameStr += "_";

  if (ObjCCategoryImplDecl *CID =
      dyn_cast<ObjCCategoryImplDecl>(OMD->getDeclContext())) {
    NameStr += CID->getNameAsString();
    NameStr += "_";
  }
  // Append selector names, replacing ':' with '_'
  {
    std::string selString = OMD->getSelector().getAsString();
    int len = selString.size();
    for (int i = 0; i < len; i++)
      if (selString[i] == ':')
        selString[i] = '_';
    NameStr += selString;
  }
  // Remember this name for metadata emission
  MethodInternalNames[OMD] = NameStr;
  ResultStr += NameStr;

  // Rewrite arguments
  ResultStr += "(";

  // invisible arguments
  if (OMD->isInstanceMethod()) {
    QualType selfTy = Context->getObjCInterfaceType(IDecl);
    selfTy = Context->getPointerType(selfTy);
    if (!LangOpts.MicrosoftExt) {
      if (ObjCSynthesizedStructs.count(const_cast<ObjCInterfaceDecl*>(IDecl)))
        ResultStr += "struct ";
    }
    // When rewriting for Microsoft, explicitly omit the structure name.
    ResultStr += IDecl->getNameAsString();
    ResultStr += " *";
  }
  else
    ResultStr += Context->getObjCClassType().getAsString(
      Context->getPrintingPolicy());

  ResultStr += " self, ";
  ResultStr += Context->getObjCSelType().getAsString(Context->getPrintingPolicy());
  ResultStr += " _cmd";

  // Method arguments.
  for (const auto *PDecl : OMD->parameters()) {
    ResultStr += ", ";
    if (PDecl->getType()->isObjCQualifiedIdType()) {
      ResultStr += "id ";
      ResultStr += PDecl->getNameAsString();
    } else {
      std::string Name = PDecl->getNameAsString();
      QualType QT = PDecl->getType();
      // Make sure we convert "t (^)(...)" to "t (*)(...)".
      (void)convertBlockPointerToFunctionPointer(QT);
      QT.getAsStringInternal(Name, Context->getPrintingPolicy());
      ResultStr += Name;
    }
  }
  if (OMD->isVariadic())
    ResultStr += ", ...";
  ResultStr += ") ";

  if (FPRetType) {
    ResultStr += ")"; // close the precedence "scope" for "*".

    // Now, emit the argument types (if any).
    if (const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(FPRetType)) {
      ResultStr += "(";
      for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
        if (i) ResultStr += ", ";
        std::string ParamStr =
            FT->getParamType(i).getAsString(Context->getPrintingPolicy());
        ResultStr += ParamStr;
      }
      if (FT->isVariadic()) {
        if (FT->getNumParams())
          ResultStr += ", ";
        ResultStr += "...";
      }
      ResultStr += ")";
    } else {
      ResultStr += "()";
    }
  }
}

void RewriteObjC::RewriteImplementationDecl(Decl *OID) {
  ObjCImplementationDecl *IMD = dyn_cast<ObjCImplementationDecl>(OID);
  ObjCCategoryImplDecl *CID = dyn_cast<ObjCCategoryImplDecl>(OID);
  assert((IMD || CID) && "Unknown ImplementationDecl");

  InsertText(IMD ? IMD->getBeginLoc() : CID->getBeginLoc(), "// ");

  for (auto *OMD : IMD ? IMD->instance_methods() : CID->instance_methods()) {
    if (!OMD->getBody())
      continue;
    std::string ResultStr;
    RewriteObjCMethodDecl(OMD->getClassInterface(), OMD, ResultStr);
    SourceLocation LocStart = OMD->getBeginLoc();
    SourceLocation LocEnd = OMD->getCompoundBody()->getBeginLoc();

    const char *startBuf = SM->getCharacterData(LocStart);
    const char *endBuf = SM->getCharacterData(LocEnd);
    ReplaceText(LocStart, endBuf-startBuf, ResultStr);
  }

  for (auto *OMD : IMD ? IMD->class_methods() : CID->class_methods()) {
    if (!OMD->getBody())
      continue;
    std::string ResultStr;
    RewriteObjCMethodDecl(OMD->getClassInterface(), OMD, ResultStr);
    SourceLocation LocStart = OMD->getBeginLoc();
    SourceLocation LocEnd = OMD->getCompoundBody()->getBeginLoc();

    const char *startBuf = SM->getCharacterData(LocStart);
    const char *endBuf = SM->getCharacterData(LocEnd);
    ReplaceText(LocStart, endBuf-startBuf, ResultStr);
  }
  for (auto *I : IMD ? IMD->property_impls() : CID->property_impls())
    RewritePropertyImplDecl(I, IMD, CID);

  InsertText(IMD ? IMD->getEndLoc() : CID->getEndLoc(), "// ");
}

void RewriteObjC::RewriteInterfaceDecl(ObjCInterfaceDecl *ClassDecl) {
  std::string ResultStr;
  if (!ObjCForwardDecls.count(ClassDecl->getCanonicalDecl())) {
    // we haven't seen a forward decl - generate a typedef.
    ResultStr = "#ifndef _REWRITER_typedef_";
    ResultStr += ClassDecl->getNameAsString();
    ResultStr += "\n";
    ResultStr += "#define _REWRITER_typedef_";
    ResultStr += ClassDecl->getNameAsString();
    ResultStr += "\n";
    ResultStr += "typedef struct objc_object ";
    ResultStr += ClassDecl->getNameAsString();
    ResultStr += ";\n#endif\n";
    // Mark this typedef as having been generated.
    ObjCForwardDecls.insert(ClassDecl->getCanonicalDecl());
  }
  RewriteObjCInternalStruct(ClassDecl, ResultStr);

  for (auto *I : ClassDecl->instance_properties())
    RewriteProperty(I);
  for (auto *I : ClassDecl->instance_methods())
    RewriteMethodDeclaration(I);
  for (auto *I : ClassDecl->class_methods())
    RewriteMethodDeclaration(I);

  // Lastly, comment out the @end.
  ReplaceText(ClassDecl->getAtEndRange().getBegin(), strlen("@end"),
              "/* @end */");
}

Stmt *RewriteObjC::RewritePropertyOrImplicitSetter(PseudoObjectExpr *PseudoOp) {
  SourceRange OldRange = PseudoOp->getSourceRange();

  // We just magically know some things about the structure of this
  // expression.
  ObjCMessageExpr *OldMsg =
    cast<ObjCMessageExpr>(PseudoOp->getSemanticExpr(
                            PseudoOp->getNumSemanticExprs() - 1));

  // Because the rewriter doesn't allow us to rewrite rewritten code,
  // we need to suppress rewriting the sub-statements.
  Expr *Base, *RHS;
  {
    DisableReplaceStmtScope S(*this);

    // Rebuild the base expression if we have one.
    Base = nullptr;
    if (OldMsg->getReceiverKind() == ObjCMessageExpr::Instance) {
      Base = OldMsg->getInstanceReceiver();
      Base = cast<OpaqueValueExpr>(Base)->getSourceExpr();
      Base = cast<Expr>(RewriteFunctionBodyOrGlobalInitializer(Base));
    }

    // Rebuild the RHS.
    RHS = cast<BinaryOperator>(PseudoOp->getSyntacticForm())->getRHS();
    RHS = cast<OpaqueValueExpr>(RHS)->getSourceExpr();
    RHS = cast<Expr>(RewriteFunctionBodyOrGlobalInitializer(RHS));
  }

  // TODO: avoid this copy.
  SmallVector<SourceLocation, 1> SelLocs;
  OldMsg->getSelectorLocs(SelLocs);

  ObjCMessageExpr *NewMsg = nullptr;
  switch (OldMsg->getReceiverKind()) {
  case ObjCMessageExpr::Class:
    NewMsg = ObjCMessageExpr::Create(*Context, OldMsg->getType(),
                                     OldMsg->getValueKind(),
                                     OldMsg->getLeftLoc(),
                                     OldMsg->getClassReceiverTypeInfo(),
                                     OldMsg->getSelector(),
                                     SelLocs,
                                     OldMsg->getMethodDecl(),
                                     RHS,
                                     OldMsg->getRightLoc(),
                                     OldMsg->isImplicit());
    break;

  case ObjCMessageExpr::Instance:
    NewMsg = ObjCMessageExpr::Create(*Context, OldMsg->getType(),
                                     OldMsg->getValueKind(),
                                     OldMsg->getLeftLoc(),
                                     Base,
                                     OldMsg->getSelector(),
                                     SelLocs,
                                     OldMsg->getMethodDecl(),
                                     RHS,
                                     OldMsg->getRightLoc(),
                                     OldMsg->isImplicit());
    break;

  case ObjCMessageExpr::SuperClass:
  case ObjCMessageExpr::SuperInstance:
    NewMsg = ObjCMessageExpr::Create(*Context, OldMsg->getType(),
                                     OldMsg->getValueKind(),
                                     OldMsg->getLeftLoc(),
                                     OldMsg->getSuperLoc(),
                 OldMsg->getReceiverKind() == ObjCMessageExpr::SuperInstance,
                                     OldMsg->getSuperType(),
                                     OldMsg->getSelector(),
                                     SelLocs,
                                     OldMsg->getMethodDecl(),
                                     RHS,
                                     OldMsg->getRightLoc(),
                                     OldMsg->isImplicit());
    break;
  }

  Stmt *Replacement = SynthMessageExpr(NewMsg);
  ReplaceStmtWithRange(PseudoOp, Replacement, OldRange);
  return Replacement;
}

Stmt *RewriteObjC::RewritePropertyOrImplicitGetter(PseudoObjectExpr *PseudoOp) {
  SourceRange OldRange = PseudoOp->getSourceRange();

  // We just magically know some things about the structure of this
  // expression.
  ObjCMessageExpr *OldMsg =
    cast<ObjCMessageExpr>(PseudoOp->getResultExpr()->IgnoreImplicit());

  // Because the rewriter doesn't allow us to rewrite rewritten code,
  // we need to suppress rewriting the sub-statements.
  Expr *Base = nullptr;
  {
    DisableReplaceStmtScope S(*this);

    // Rebuild the base expression if we have one.
    if (OldMsg->getReceiverKind() == ObjCMessageExpr::Instance) {
      Base = OldMsg->getInstanceReceiver();
      Base = cast<OpaqueValueExpr>(Base)->getSourceExpr();
      Base = cast<Expr>(RewriteFunctionBodyOrGlobalInitializer(Base));
    }
  }

  // Intentionally empty.
  SmallVector<SourceLocation, 1> SelLocs;
  SmallVector<Expr*, 1> Args;

  ObjCMessageExpr *NewMsg = nullptr;
  switch (OldMsg->getReceiverKind()) {
  case ObjCMessageExpr::Class:
    NewMsg = ObjCMessageExpr::Create(*Context, OldMsg->getType(),
                                     OldMsg->getValueKind(),
                                     OldMsg->getLeftLoc(),
                                     OldMsg->getClassReceiverTypeInfo(),
                                     OldMsg->getSelector(),
                                     SelLocs,
                                     OldMsg->getMethodDecl(),
                                     Args,
                                     OldMsg->getRightLoc(),
                                     OldMsg->isImplicit());
    break;

  case ObjCMessageExpr::Instance:
    NewMsg = ObjCMessageExpr::Create(*Context, OldMsg->getType(),
                                     OldMsg->getValueKind(),
                                     OldMsg->getLeftLoc(),
                                     Base,
                                     OldMsg->getSelector(),
                                     SelLocs,
                                     OldMsg->getMethodDecl(),
                                     Args,
                                     OldMsg->getRightLoc(),
                                     OldMsg->isImplicit());
    break;

  case ObjCMessageExpr::SuperClass:
  case ObjCMessageExpr::SuperInstance:
    NewMsg = ObjCMessageExpr::Create(*Context, OldMsg->getType(),
                                     OldMsg->getValueKind(),
                                     OldMsg->getLeftLoc(),
                                     OldMsg->getSuperLoc(),
                 OldMsg->getReceiverKind() == ObjCMessageExpr::SuperInstance,
                                     OldMsg->getSuperType(),
                                     OldMsg->getSelector(),
                                     SelLocs,
                                     OldMsg->getMethodDecl(),
                                     Args,
                                     OldMsg->getRightLoc(),
                                     OldMsg->isImplicit());
    break;
  }

  Stmt *Replacement = SynthMessageExpr(NewMsg);
  ReplaceStmtWithRange(PseudoOp, Replacement, OldRange);
  return Replacement;
}

/// SynthCountByEnumWithState - To print:
/// ((unsigned int (*)
///  (id, SEL, struct __objcFastEnumerationState *, id *, unsigned int))
///  (void *)objc_msgSend)((id)l_collection,
///                        sel_registerName(
///                          "countByEnumeratingWithState:objects:count:"),
///                        &enumState,
///                        (id *)__rw_items, (unsigned int)16)
///
void RewriteObjC::SynthCountByEnumWithState(std::string &buf) {
  buf += "((unsigned int (*) (id, SEL, struct __objcFastEnumerationState *, "
  "id *, unsigned int))(void *)objc_msgSend)";
  buf += "\n\t\t";
  buf += "((id)l_collection,\n\t\t";
  buf += "sel_registerName(\"countByEnumeratingWithState:objects:count:\"),";
  buf += "\n\t\t";
  buf += "&enumState, "
         "(id *)__rw_items, (unsigned int)16)";
}

/// RewriteBreakStmt - Rewrite for a break-stmt inside an ObjC2's foreach
/// statement to exit to its outer synthesized loop.
///
Stmt *RewriteObjC::RewriteBreakStmt(BreakStmt *S) {
  if (Stmts.empty() || !isa<ObjCForCollectionStmt>(Stmts.back()))
    return S;
  // replace break with goto __break_label
  std::string buf;

  SourceLocation startLoc = S->getBeginLoc();
  buf = "goto __break_label_";
  buf += utostr(ObjCBcLabelNo.back());
  ReplaceText(startLoc, strlen("break"), buf);

  return nullptr;
}

/// RewriteContinueStmt - Rewrite for a continue-stmt inside an ObjC2's foreach
/// statement to continue with its inner synthesized loop.
///
Stmt *RewriteObjC::RewriteContinueStmt(ContinueStmt *S) {
  if (Stmts.empty() || !isa<ObjCForCollectionStmt>(Stmts.back()))
    return S;
  // replace continue with goto __continue_label
  std::string buf;

  SourceLocation startLoc = S->getBeginLoc();
  buf = "goto __continue_label_";
  buf += utostr(ObjCBcLabelNo.back());
  ReplaceText(startLoc, strlen("continue"), buf);

  return nullptr;
}

/// RewriteObjCForCollectionStmt - Rewriter for ObjC2's foreach statement.
///  It rewrites:
/// for ( type elem in collection) { stmts; }

/// Into:
/// {
///   type elem;
///   struct __objcFastEnumerationState enumState = { 0 };
///   id __rw_items[16];
///   id l_collection = (id)collection;
///   unsigned long limit = [l_collection countByEnumeratingWithState:&enumState
///                                       objects:__rw_items count:16];
/// if (limit) {
///   unsigned long startMutations = *enumState.mutationsPtr;
///   do {
///        unsigned long counter = 0;
///        do {
///             if (startMutations != *enumState.mutationsPtr)
///               objc_enumerationMutation(l_collection);
///             elem = (type)enumState.itemsPtr[counter++];
///             stmts;
///             __continue_label: ;
///        } while (counter < limit);
///   } while (limit = [l_collection countByEnumeratingWithState:&enumState
///                                  objects:__rw_items count:16]);
///   elem = nil;
///   __break_label: ;
///  }
///  else
///       elem = nil;
///  }
///
Stmt *RewriteObjC::RewriteObjCForCollectionStmt(ObjCForCollectionStmt *S,
                                                SourceLocation OrigEnd) {
  assert(!Stmts.empty() && "ObjCForCollectionStmt - Statement stack empty");
  assert(isa<ObjCForCollectionStmt>(Stmts.back()) &&
         "ObjCForCollectionStmt Statement stack mismatch");
  assert(!ObjCBcLabelNo.empty() &&
         "ObjCForCollectionStmt - Label No stack empty");

  SourceLocation startLoc = S->getBeginLoc();
  const char *startBuf = SM->getCharacterData(startLoc);
  StringRef elementName;
  std::string elementTypeAsString;
  std::string buf;
  buf = "\n{\n\t";
  if (DeclStmt *DS = dyn_cast<DeclStmt>(S->getElement())) {
    // type elem;
    NamedDecl* D = cast<NamedDecl>(DS->getSingleDecl());
    QualType ElementType = cast<ValueDecl>(D)->getType();
    if (ElementType->isObjCQualifiedIdType() ||
        ElementType->isObjCQualifiedInterfaceType())
      // Simply use 'id' for all qualified types.
      elementTypeAsString = "id";
    else
      elementTypeAsString = ElementType.getAsString(Context->getPrintingPolicy());
    buf += elementTypeAsString;
    buf += " ";
    elementName = D->getName();
    buf += elementName;
    buf += ";\n\t";
  }
  else {
    DeclRefExpr *DR = cast<DeclRefExpr>(S->getElement());
    elementName = DR->getDecl()->getName();
    ValueDecl *VD = DR->getDecl();
    if (VD->getType()->isObjCQualifiedIdType() ||
        VD->getType()->isObjCQualifiedInterfaceType())
      // Simply use 'id' for all qualified types.
      elementTypeAsString = "id";
    else
      elementTypeAsString = VD->getType().getAsString(Context->getPrintingPolicy());
  }

  // struct __objcFastEnumerationState enumState = { 0 };
  buf += "struct __objcFastEnumerationState enumState = { 0 };\n\t";
  // id __rw_items[16];
  buf += "id __rw_items[16];\n\t";
  // id l_collection = (id)
  buf += "id l_collection = (id)";
  // Find start location of 'collection' the hard way!
  const char *startCollectionBuf = startBuf;
  startCollectionBuf += 3;  // skip 'for'
  startCollectionBuf = strchr(startCollectionBuf, '(');
  startCollectionBuf++; // skip '('
  // find 'in' and skip it.
  while (*startCollectionBuf != ' ' ||
         *(startCollectionBuf+1) != 'i' || *(startCollectionBuf+2) != 'n' ||
         (*(startCollectionBuf+3) != ' ' &&
          *(startCollectionBuf+3) != '[' && *(startCollectionBuf+3) != '('))
    startCollectionBuf++;
  startCollectionBuf += 3;

  // Replace: "for (type element in" with string constructed thus far.
  ReplaceText(startLoc, startCollectionBuf - startBuf, buf);
  // Replace ')' in for '(' type elem in collection ')' with ';'
  SourceLocation rightParenLoc = S->getRParenLoc();
  const char *rparenBuf = SM->getCharacterData(rightParenLoc);
  SourceLocation lparenLoc = startLoc.getLocWithOffset(rparenBuf-startBuf);
  buf = ";\n\t";

  // unsigned long limit = [l_collection countByEnumeratingWithState:&enumState
  //                                   objects:__rw_items count:16];
  // which is synthesized into:
  // unsigned int limit =
  // ((unsigned int (*)
  //  (id, SEL, struct __objcFastEnumerationState *, id *, unsigned int))
  //  (void *)objc_msgSend)((id)l_collection,
  //                        sel_registerName(
  //                          "countByEnumeratingWithState:objects:count:"),
  //                        (struct __objcFastEnumerationState *)&state,
  //                        (id *)__rw_items, (unsigned int)16);
  buf += "unsigned long limit =\n\t\t";
  SynthCountByEnumWithState(buf);
  buf += ";\n\t";
  /// if (limit) {
  ///   unsigned long startMutations = *enumState.mutationsPtr;
  ///   do {
  ///        unsigned long counter = 0;
  ///        do {
  ///             if (startMutations != *enumState.mutationsPtr)
  ///               objc_enumerationMutation(l_collection);
  ///             elem = (type)enumState.itemsPtr[counter++];
  buf += "if (limit) {\n\t";
  buf += "unsigned long startMutations = *enumState.mutationsPtr;\n\t";
  buf += "do {\n\t\t";
  buf += "unsigned long counter = 0;\n\t\t";
  buf += "do {\n\t\t\t";
  buf += "if (startMutations != *enumState.mutationsPtr)\n\t\t\t\t";
  buf += "objc_enumerationMutation(l_collection);\n\t\t\t";
  buf += elementName;
  buf += " = (";
  buf += elementTypeAsString;
  buf += ")enumState.itemsPtr[counter++];";
  // Replace ')' in for '(' type elem in collection ')' with all of these.
  ReplaceText(lparenLoc, 1, buf);

  ///            __continue_label: ;
  ///        } while (counter < limit);
  ///   } while (limit = [l_collection countByEnumeratingWithState:&enumState
  ///                                  objects:__rw_items count:16]);
  ///   elem = nil;
  ///   __break_label: ;
  ///  }
  ///  else
  ///       elem = nil;
  ///  }
  ///
  buf = ";\n\t";
  buf += "__continue_label_";
  buf += utostr(ObjCBcLabelNo.back());
  buf += ": ;";
  buf += "\n\t\t";
  buf += "} while (counter < limit);\n\t";
  buf += "} while (limit = ";
  SynthCountByEnumWithState(buf);
  buf += ");\n\t";
  buf += elementName;
  buf += " = ((";
  buf += elementTypeAsString;
  buf += ")0);\n\t";
  buf += "__break_label_";
  buf += utostr(ObjCBcLabelNo.back());
  buf += ": ;\n\t";
  buf += "}\n\t";
  buf += "else\n\t\t";
  buf += elementName;
  buf += " = ((";
  buf += elementTypeAsString;
  buf += ")0);\n\t";
  buf += "}\n";

  // Insert all these *after* the statement body.
  // FIXME: If this should support Obj-C++, support CXXTryStmt
  if (isa<CompoundStmt>(S->getBody())) {
    SourceLocation endBodyLoc = OrigEnd.getLocWithOffset(1);
    InsertText(endBodyLoc, buf);
  } else {
    /* Need to treat single statements specially. For example:
     *
     *     for (A *a in b) if (stuff()) break;
     *     for (A *a in b) xxxyy;
     *
     * The following code simply scans ahead to the semi to find the actual end.
     */
    const char *stmtBuf = SM->getCharacterData(OrigEnd);
    const char *semiBuf = strchr(stmtBuf, ';');
    assert(semiBuf && "Can't find ';'");
    SourceLocation endBodyLoc = OrigEnd.getLocWithOffset(semiBuf-stmtBuf+1);
    InsertText(endBodyLoc, buf);
  }
  Stmts.pop_back();
  ObjCBcLabelNo.pop_back();
  return nullptr;
}

/// RewriteObjCSynchronizedStmt -
/// This routine rewrites @synchronized(expr) stmt;
/// into:
/// objc_sync_enter(expr);
/// @try stmt @finally { objc_sync_exit(expr); }
///
Stmt *RewriteObjC::RewriteObjCSynchronizedStmt(ObjCAtSynchronizedStmt *S) {
  // Get the start location and compute the semi location.
  SourceLocation startLoc = S->getBeginLoc();
  const char *startBuf = SM->getCharacterData(startLoc);

  assert((*startBuf == '@') && "bogus @synchronized location");

  std::string buf;
  buf = "objc_sync_enter((id)";
  const char *lparenBuf = startBuf;
  while (*lparenBuf != '(') lparenBuf++;
  ReplaceText(startLoc, lparenBuf-startBuf+1, buf);
  // We can't use S->getSynchExpr()->getEndLoc() to find the end location, since
  // the sync expression is typically a message expression that's already
  // been rewritten! (which implies the SourceLocation's are invalid).
  SourceLocation endLoc = S->getSynchBody()->getBeginLoc();
  const char *endBuf = SM->getCharacterData(endLoc);
  while (*endBuf != ')') endBuf--;
  SourceLocation rparenLoc = startLoc.getLocWithOffset(endBuf-startBuf);
  buf = ");\n";
  // declare a new scope with two variables, _stack and _rethrow.
  buf += "/* @try scope begin */ \n{ struct _objc_exception_data {\n";
  buf += "int buf[18/*32-bit i386*/];\n";
  buf += "char *pointers[4];} _stack;\n";
  buf += "id volatile _rethrow = 0;\n";
  buf += "objc_exception_try_enter(&_stack);\n";
  buf += "if (!_setjmp(_stack.buf)) /* @try block continue */\n";
  ReplaceText(rparenLoc, 1, buf);
  startLoc = S->getSynchBody()->getEndLoc();
  startBuf = SM->getCharacterData(startLoc);

  assert((*startBuf == '}') && "bogus @synchronized block");
  SourceLocation lastCurlyLoc = startLoc;
  buf = "}\nelse {\n";
  buf += "  _rethrow = objc_exception_extract(&_stack);\n";
  buf += "}\n";
  buf += "{ /* implicit finally clause */\n";
  buf += "  if (!_rethrow) objc_exception_try_exit(&_stack);\n";

  std::string syncBuf;
  syncBuf += " objc_sync_exit(";

  Expr *syncExpr = S->getSynchExpr();
  CastKind CK = syncExpr->getType()->isObjCObjectPointerType()
                  ? CK_BitCast :
                syncExpr->getType()->isBlockPointerType()
                  ? CK_BlockPointerToObjCPointerCast
                  : CK_CPointerToObjCPointerCast;
  syncExpr = NoTypeInfoCStyleCastExpr(Context, Context->getObjCIdType(),
                                      CK, syncExpr);
  std::string syncExprBufS;
  llvm::raw_string_ostream syncExprBuf(syncExprBufS);
  assert(syncExpr != nullptr && "Expected non-null Expr");
  syncExpr->printPretty(syncExprBuf, nullptr, PrintingPolicy(LangOpts));
  syncBuf += syncExprBuf.str();
  syncBuf += ");";

  buf += syncBuf;
  buf += "\n  if (_rethrow) objc_exception_throw(_rethrow);\n";
  buf += "}\n";
  buf += "}";

  ReplaceText(lastCurlyLoc, 1, buf);

  bool hasReturns = false;
  HasReturnStmts(S->getSynchBody(), hasReturns);
  if (hasReturns)
    RewriteSyncReturnStmts(S->getSynchBody(), syncBuf);

  return nullptr;
}

void RewriteObjC::WarnAboutReturnGotoStmts(Stmt *S)
{
  // Perform a bottom up traversal of all children.
  for (Stmt *SubStmt : S->children())
    if (SubStmt)
      WarnAboutReturnGotoStmts(SubStmt);

  if (isa<ReturnStmt>(S) || isa<GotoStmt>(S)) {
    Diags.Report(Context->getFullLoc(S->getBeginLoc()),
                 TryFinallyContainsReturnDiag);
  }
}

void RewriteObjC::HasReturnStmts(Stmt *S, bool &hasReturns)
{
  // Perform a bottom up traversal of all children.
  for (Stmt *SubStmt : S->children())
    if (SubStmt)
      HasReturnStmts(SubStmt, hasReturns);

  if (isa<ReturnStmt>(S))
    hasReturns = true;
}

void RewriteObjC::RewriteTryReturnStmts(Stmt *S) {
  // Perform a bottom up traversal of all children.
  for (Stmt *SubStmt : S->children())
    if (SubStmt) {
      RewriteTryReturnStmts(SubStmt);
    }
  if (isa<ReturnStmt>(S)) {
    SourceLocation startLoc = S->getBeginLoc();
    const char *startBuf = SM->getCharacterData(startLoc);
    const char *semiBuf = strchr(startBuf, ';');
    assert((*semiBuf == ';') && "RewriteTryReturnStmts: can't find ';'");
    SourceLocation onePastSemiLoc = startLoc.getLocWithOffset(semiBuf-startBuf+1);

    std::string buf;
    buf = "{ objc_exception_try_exit(&_stack); return";

    ReplaceText(startLoc, 6, buf);
    InsertText(onePastSemiLoc, "}");
  }
}

void RewriteObjC::RewriteSyncReturnStmts(Stmt *S, std::string syncExitBuf) {
  // Perform a bottom up traversal of all children.
  for (Stmt *SubStmt : S->children())
    if (SubStmt) {
      RewriteSyncReturnStmts(SubStmt, syncExitBuf);
    }
  if (isa<ReturnStmt>(S)) {
    SourceLocation startLoc = S->getBeginLoc();
    const char *startBuf = SM->getCharacterData(startLoc);

    const char *semiBuf = strchr(startBuf, ';');
    assert((*semiBuf == ';') && "RewriteSyncReturnStmts: can't find ';'");
    SourceLocation onePastSemiLoc = startLoc.getLocWithOffset(semiBuf-startBuf+1);

    std::string buf;
    buf = "{ objc_exception_try_exit(&_stack);";
    buf += syncExitBuf;
    buf += " return";

    ReplaceText(startLoc, 6, buf);
    InsertText(onePastSemiLoc, "}");
  }
}

Stmt *RewriteObjC::RewriteObjCTryStmt(ObjCAtTryStmt *S) {
  // Get the start location and compute the semi location.
  SourceLocation startLoc = S->getBeginLoc();
  const char *startBuf = SM->getCharacterData(startLoc);

  assert((*startBuf == '@') && "bogus @try location");

  std::string buf;
  // declare a new scope with two variables, _stack and _rethrow.
  buf = "/* @try scope begin */ { struct _objc_exception_data {\n";
  buf += "int buf[18/*32-bit i386*/];\n";
  buf += "char *pointers[4];} _stack;\n";
  buf += "id volatile _rethrow = 0;\n";
  buf += "objc_exception_try_enter(&_stack);\n";
  buf += "if (!_setjmp(_stack.buf)) /* @try block continue */\n";

  ReplaceText(startLoc, 4, buf);

  startLoc = S->getTryBody()->getEndLoc();
  startBuf = SM->getCharacterData(startLoc);

  assert((*startBuf == '}') && "bogus @try block");

  SourceLocation lastCurlyLoc = startLoc;
  if (S->getNumCatchStmts()) {
    startLoc = startLoc.getLocWithOffset(1);
    buf = " /* @catch begin */ else {\n";
    buf += " id _caught = objc_exception_extract(&_stack);\n";
    buf += " objc_exception_try_enter (&_stack);\n";
    buf += " if (_setjmp(_stack.buf))\n";
    buf += "   _rethrow = objc_exception_extract(&_stack);\n";
    buf += " else { /* @catch continue */";

    InsertText(startLoc, buf);
  } else { /* no catch list */
    buf = "}\nelse {\n";
    buf += "  _rethrow = objc_exception_extract(&_stack);\n";
    buf += "}";
    ReplaceText(lastCurlyLoc, 1, buf);
  }
  Stmt *lastCatchBody = nullptr;
  for (unsigned I = 0, N = S->getNumCatchStmts(); I != N; ++I) {
    ObjCAtCatchStmt *Catch = S->getCatchStmt(I);
    VarDecl *catchDecl = Catch->getCatchParamDecl();

    if (I == 0)
      buf = "if ("; // we are generating code for the first catch clause
    else
      buf = "else if (";
    startLoc = Catch->getBeginLoc();
    startBuf = SM->getCharacterData(startLoc);

    assert((*startBuf == '@') && "bogus @catch location");

    const char *lParenLoc = strchr(startBuf, '(');

    if (Catch->hasEllipsis()) {
      // Now rewrite the body...
      lastCatchBody = Catch->getCatchBody();
      SourceLocation bodyLoc = lastCatchBody->getBeginLoc();
      const char *bodyBuf = SM->getCharacterData(bodyLoc);
      assert(*SM->getCharacterData(Catch->getRParenLoc()) == ')' &&
             "bogus @catch paren location");
      assert((*bodyBuf == '{') && "bogus @catch body location");

      buf += "1) { id _tmp = _caught;";
      Rewrite.ReplaceText(startLoc, bodyBuf-startBuf+1, buf);
    } else if (catchDecl) {
      QualType t = catchDecl->getType();
      if (t == Context->getObjCIdType()) {
        buf += "1) { ";
        ReplaceText(startLoc, lParenLoc-startBuf+1, buf);
      } else if (const ObjCObjectPointerType *Ptr =
                   t->getAs<ObjCObjectPointerType>()) {
        // Should be a pointer to a class.
        ObjCInterfaceDecl *IDecl = Ptr->getObjectType()->getInterface();
        if (IDecl) {
          buf += "objc_exception_match((struct objc_class *)objc_getClass(\"";
          buf += IDecl->getNameAsString();
          buf += "\"), (struct objc_object *)_caught)) { ";
          ReplaceText(startLoc, lParenLoc-startBuf+1, buf);
        }
      }
      // Now rewrite the body...
      lastCatchBody = Catch->getCatchBody();
      SourceLocation rParenLoc = Catch->getRParenLoc();
      SourceLocation bodyLoc = lastCatchBody->getBeginLoc();
      const char *bodyBuf = SM->getCharacterData(bodyLoc);
      const char *rParenBuf = SM->getCharacterData(rParenLoc);
      assert((*rParenBuf == ')') && "bogus @catch paren location");
      assert((*bodyBuf == '{') && "bogus @catch body location");

      // Here we replace ") {" with "= _caught;" (which initializes and
      // declares the @catch parameter).
      ReplaceText(rParenLoc, bodyBuf-rParenBuf+1, " = _caught;");
    } else {
      llvm_unreachable("@catch rewrite bug");
    }
  }
  // Complete the catch list...
  if (lastCatchBody) {
    SourceLocation bodyLoc = lastCatchBody->getEndLoc();
    assert(*SM->getCharacterData(bodyLoc) == '}' &&
           "bogus @catch body location");

    // Insert the last (implicit) else clause *before* the right curly brace.
    bodyLoc = bodyLoc.getLocWithOffset(-1);
    buf = "} /* last catch end */\n";
    buf += "else {\n";
    buf += " _rethrow = _caught;\n";
    buf += " objc_exception_try_exit(&_stack);\n";
    buf += "} } /* @catch end */\n";
    if (!S->getFinallyStmt())
      buf += "}\n";
    InsertText(bodyLoc, buf);

    // Set lastCurlyLoc
    lastCurlyLoc = lastCatchBody->getEndLoc();
  }
  if (ObjCAtFinallyStmt *finalStmt = S->getFinallyStmt()) {
    startLoc = finalStmt->getBeginLoc();
    startBuf = SM->getCharacterData(startLoc);
    assert((*startBuf == '@') && "bogus @finally start");

    ReplaceText(startLoc, 8, "/* @finally */");

    Stmt *body = finalStmt->getFinallyBody();
    SourceLocation startLoc = body->getBeginLoc();
    SourceLocation endLoc = body->getEndLoc();
    assert(*SM->getCharacterData(startLoc) == '{' &&
           "bogus @finally body location");
    assert(*SM->getCharacterData(endLoc) == '}' &&
           "bogus @finally body location");

    startLoc = startLoc.getLocWithOffset(1);
    InsertText(startLoc, " if (!_rethrow) objc_exception_try_exit(&_stack);\n");
    endLoc = endLoc.getLocWithOffset(-1);
    InsertText(endLoc, " if (_rethrow) objc_exception_throw(_rethrow);\n");

    // Set lastCurlyLoc
    lastCurlyLoc = body->getEndLoc();

    // Now check for any return/continue/go statements within the @try.
    WarnAboutReturnGotoStmts(S->getTryBody());
  } else { /* no finally clause - make sure we synthesize an implicit one */
    buf = "{ /* implicit finally clause */\n";
    buf += " if (!_rethrow) objc_exception_try_exit(&_stack);\n";
    buf += " if (_rethrow) objc_exception_throw(_rethrow);\n";
    buf += "}";
    ReplaceText(lastCurlyLoc, 1, buf);

    // Now check for any return/continue/go statements within the @try.
    // The implicit finally clause won't called if the @try contains any
    // jump statements.
    bool hasReturns = false;
    HasReturnStmts(S->getTryBody(), hasReturns);
    if (hasReturns)
      RewriteTryReturnStmts(S->getTryBody());
  }
  // Now emit the final closing curly brace...
  lastCurlyLoc = lastCurlyLoc.getLocWithOffset(1);
  InsertText(lastCurlyLoc, " } /* @try scope end */\n");
  return nullptr;
}

// This can't be done with ReplaceStmt(S, ThrowExpr), since
// the throw expression is typically a message expression that's already
// been rewritten! (which implies the SourceLocation's are invalid).
Stmt *RewriteObjC::RewriteObjCThrowStmt(ObjCAtThrowStmt *S) {
  // Get the start location and compute the semi location.
  SourceLocation startLoc = S->getBeginLoc();
  const char *startBuf = SM->getCharacterData(startLoc);

  assert((*startBuf == '@') && "bogus @throw location");

  std::string buf;
  /* void objc_exception_throw(id) __attribute__((noreturn)); */
  if (S->getThrowExpr())
    buf = "objc_exception_throw(";
  else // add an implicit argument
    buf = "objc_exception_throw(_caught";

  // handle "@  throw" correctly.
  const char *wBuf = strchr(startBuf, 'w');
  assert((*wBuf == 'w') && "@throw: can't find 'w'");
  ReplaceText(startLoc, wBuf-startBuf+1, buf);

  const char *semiBuf = strchr(startBuf, ';');
  assert((*semiBuf == ';') && "@throw: can't find ';'");
  SourceLocation semiLoc = startLoc.getLocWithOffset(semiBuf-startBuf);
  ReplaceText(semiLoc, 1, ");");
  return nullptr;
}

Stmt *RewriteObjC::RewriteAtEncode(ObjCEncodeExpr *Exp) {
  // Create a new string expression.
  std::string StrEncoding;
  Context->getObjCEncodingForType(Exp->getEncodedType(), StrEncoding);
  Expr *Replacement = getStringLiteral(StrEncoding);
  ReplaceStmt(Exp, Replacement);

  // Replace this subexpr in the parent.
  // delete Exp; leak for now, see RewritePropertyOrImplicitSetter() usage for more info.
  return Replacement;
}

Stmt *RewriteObjC::RewriteAtSelector(ObjCSelectorExpr *Exp) {
  if (!SelGetUidFunctionDecl)
    SynthSelGetUidFunctionDecl();
  assert(SelGetUidFunctionDecl && "Can't find sel_registerName() decl");
  // Create a call to sel_registerName("selName").
  SmallVector<Expr*, 8> SelExprs;
  SelExprs.push_back(getStringLiteral(Exp->getSelector().getAsString()));
  CallExpr *SelExp = SynthesizeCallToFunctionDecl(SelGetUidFunctionDecl,
                                                  SelExprs);
  ReplaceStmt(Exp, SelExp);
  // delete Exp; leak for now, see RewritePropertyOrImplicitSetter() usage for more info.
  return SelExp;
}

CallExpr *
RewriteObjC::SynthesizeCallToFunctionDecl(FunctionDecl *FD,
                                          ArrayRef<Expr *> Args,
                                          SourceLocation StartLoc,
                                          SourceLocation EndLoc) {
  // Get the type, we will need to reference it in a couple spots.
  QualType msgSendType = FD->getType();

  // Create a reference to the objc_msgSend() declaration.
  DeclRefExpr *DRE = new (Context) DeclRefExpr(*Context, FD, false, msgSendType,
                                               VK_LValue, SourceLocation());

  // Now, we cast the reference to a pointer to the objc_msgSend type.
  QualType pToFunc = Context->getPointerType(msgSendType);
  ImplicitCastExpr *ICE =
      ImplicitCastExpr::Create(*Context, pToFunc, CK_FunctionToPointerDecay,
                               DRE, nullptr, VK_PRValue, FPOptionsOverride());

  const auto *FT = msgSendType->castAs<FunctionType>();

  CallExpr *Exp =
      CallExpr::Create(*Context, ICE, Args, FT->getCallResultType(*Context),
                       VK_PRValue, EndLoc, FPOptionsOverride());
  return Exp;
}

static bool scanForProtocolRefs(const char *startBuf, const char *endBuf,
                                const char *&startRef, const char *&endRef) {
  while (startBuf < endBuf) {
    if (*startBuf == '<')
      startRef = startBuf; // mark the start.
    if (*startBuf == '>') {
      if (startRef && *startRef == '<') {
        endRef = startBuf; // mark the end.
        return true;
      }
      return false;
    }
    startBuf++;
  }
  return false;
}

static void scanToNextArgument(const char *&argRef) {
  int angle = 0;
  while (*argRef != ')' && (*argRef != ',' || angle > 0)) {
    if (*argRef == '<')
      angle++;
    else if (*argRef == '>')
      angle--;
    argRef++;
  }
  assert(angle == 0 && "scanToNextArgument - bad protocol type syntax");
}

bool RewriteObjC::needToScanForQualifiers(QualType T) {
  if (T->isObjCQualifiedIdType())
    return true;
  if (const PointerType *PT = T->getAs<PointerType>()) {
    if (PT->getPointeeType()->isObjCQualifiedIdType())
      return true;
  }
  if (T->isObjCObjectPointerType()) {
    T = T->getPointeeType();
    return T->isObjCQualifiedInterfaceType();
  }
  if (T->isArrayType()) {
    QualType ElemTy = Context->getBaseElementType(T);
    return needToScanForQualifiers(ElemTy);
  }
  return false;
}

void RewriteObjC::RewriteObjCQualifiedInterfaceTypes(Expr *E) {
  QualType Type = E->getType();
  if (needToScanForQualifiers(Type)) {
    SourceLocation Loc, EndLoc;

    if (const CStyleCastExpr *ECE = dyn_cast<CStyleCastExpr>(E)) {
      Loc = ECE->getLParenLoc();
      EndLoc = ECE->getRParenLoc();
    } else {
      Loc = E->getBeginLoc();
      EndLoc = E->getEndLoc();
    }
    // This will defend against trying to rewrite synthesized expressions.
    if (Loc.isInvalid() || EndLoc.isInvalid())
      return;

    const char *startBuf = SM->getCharacterData(Loc);
    const char *endBuf = SM->getCharacterData(EndLoc);
    const char *startRef = nullptr, *endRef = nullptr;
    if (scanForProtocolRefs(startBuf, endBuf, startRef, endRef)) {
      // Get the locations of the startRef, endRef.
      SourceLocation LessLoc = Loc.getLocWithOffset(startRef-startBuf);
      SourceLocation GreaterLoc = Loc.getLocWithOffset(endRef-startBuf+1);
      // Comment out the protocol references.
      InsertText(LessLoc, "/*");
      InsertText(GreaterLoc, "*/");
    }
  }
}

void RewriteObjC::RewriteObjCQualifiedInterfaceTypes(Decl *Dcl) {
  SourceLocation Loc;
  QualType Type;
  const FunctionProtoType *proto = nullptr;
  if (VarDecl *VD = dyn_cast<VarDecl>(Dcl)) {
    Loc = VD->getLocation();
    Type = VD->getType();
  }
  else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(Dcl)) {
    Loc = FD->getLocation();
    // Check for ObjC 'id' and class types that have been adorned with protocol
    // information (id<p>, C<p>*). The protocol references need to be rewritten!
    const FunctionType *funcType = FD->getType()->getAs<FunctionType>();
    assert(funcType && "missing function type");
    proto = dyn_cast<FunctionProtoType>(funcType);
    if (!proto)
      return;
    Type = proto->getReturnType();
  }
  else if (FieldDecl *FD = dyn_cast<FieldDecl>(Dcl)) {
    Loc = FD->getLocation();
    Type = FD->getType();
  }
  else
    return;

  if (needToScanForQualifiers(Type)) {
    // Since types are unique, we need to scan the buffer.

    const char *endBuf = SM->getCharacterData(Loc);
    const char *startBuf = endBuf;
    while (*startBuf != ';' && *startBuf != '<' && startBuf != MainFileStart)
      startBuf--; // scan backward (from the decl location) for return type.
    const char *startRef = nullptr, *endRef = nullptr;
    if (scanForProtocolRefs(startBuf, endBuf, startRef, endRef)) {
      // Get the locations of the startRef, endRef.
      SourceLocation LessLoc = Loc.getLocWithOffset(startRef-endBuf);
      SourceLocation GreaterLoc = Loc.getLocWithOffset(endRef-endBuf+1);
      // Comment out the protocol references.
      InsertText(LessLoc, "/*");
      InsertText(GreaterLoc, "*/");
    }
  }
  if (!proto)
      return; // most likely, was a variable
  // Now check arguments.
  const char *startBuf = SM->getCharacterData(Loc);
  const char *startFuncBuf = startBuf;
  for (unsigned i = 0; i < proto->getNumParams(); i++) {
    if (needToScanForQualifiers(proto->getParamType(i))) {
      // Since types are unique, we need to scan the buffer.

      const char *endBuf = startBuf;
      // scan forward (from the decl location) for argument types.
      scanToNextArgument(endBuf);
      const char *startRef = nullptr, *endRef = nullptr;
      if (scanForProtocolRefs(startBuf, endBuf, startRef, endRef)) {
        // Get the locations of the startRef, endRef.
        SourceLocation LessLoc =
          Loc.getLocWithOffset(startRef-startFuncBuf);
        SourceLocation GreaterLoc =
          Loc.getLocWithOffset(endRef-startFuncBuf+1);
        // Comment out the protocol references.
        InsertText(LessLoc, "/*");
        InsertText(GreaterLoc, "*/");
      }
      startBuf = ++endBuf;
    }
    else {
      // If the function name is derived from a macro expansion, then the
      // argument buffer will not follow the name. Need to speak with Chris.
      while (*startBuf && *startBuf != ')' && *startBuf != ',')
        startBuf++; // scan forward (from the decl location) for argument types.
      startBuf++;
    }
  }
}

void RewriteObjC::RewriteTypeOfDecl(VarDecl *ND) {
  QualType QT = ND->getType();
  const Type* TypePtr = QT->getAs<Type>();
  if (!isa<TypeOfExprType>(TypePtr))
    return;
  while (isa<TypeOfExprType>(TypePtr)) {
    const TypeOfExprType *TypeOfExprTypePtr = cast<TypeOfExprType>(TypePtr);
    QT = TypeOfExprTypePtr->getUnderlyingExpr()->getType();
    TypePtr = QT->getAs<Type>();
  }
  // FIXME. This will not work for multiple declarators; as in:
  // __typeof__(a) b,c,d;
  std::string TypeAsString(QT.getAsString(Context->getPrintingPolicy()));
  SourceLocation DeclLoc = ND->getTypeSpecStartLoc();
  const char *startBuf = SM->getCharacterData(DeclLoc);
  if (ND->getInit()) {
    std::string Name(ND->getNameAsString());
    TypeAsString += " " + Name + " = ";
    Expr *E = ND->getInit();
    SourceLocation startLoc;
    if (const CStyleCastExpr *ECE = dyn_cast<CStyleCastExpr>(E))
      startLoc = ECE->getLParenLoc();
    else
      startLoc = E->getBeginLoc();
    startLoc = SM->getExpansionLoc(startLoc);
    const char *endBuf = SM->getCharacterData(startLoc);
    ReplaceText(DeclLoc, endBuf-startBuf-1, TypeAsString);
  }
  else {
    SourceLocation X = ND->getEndLoc();
    X = SM->getExpansionLoc(X);
    const char *endBuf = SM->getCharacterData(X);
    ReplaceText(DeclLoc, endBuf-startBuf-1, TypeAsString);
  }
}

// SynthSelGetUidFunctionDecl - SEL sel_registerName(const char *str);
void RewriteObjC::SynthSelGetUidFunctionDecl() {
  IdentifierInfo *SelGetUidIdent = &Context->Idents.get("sel_registerName");
  SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(Context->CharTy.withConst()));
  QualType getFuncType =
    getSimpleFunctionType(Context->getObjCSelType(), ArgTys);
  SelGetUidFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                               SourceLocation(),
                                               SourceLocation(),
                                               SelGetUidIdent, getFuncType,
                                               nullptr, SC_Extern);
}

void RewriteObjC::RewriteFunctionDecl(FunctionDecl *FD) {
  // declared in <objc/objc.h>
  if (FD->getIdentifier() &&
      FD->getName() == "sel_registerName") {
    SelGetUidFunctionDecl = FD;
    return;
  }
  RewriteObjCQualifiedInterfaceTypes(FD);
}

void RewriteObjC::RewriteBlockPointerType(std::string& Str, QualType Type) {
  std::string TypeString(Type.getAsString(Context->getPrintingPolicy()));
  const char *argPtr = TypeString.c_str();
  if (!strchr(argPtr, '^')) {
    Str += TypeString;
    return;
  }
  while (*argPtr) {
    Str += (*argPtr == '^' ? '*' : *argPtr);
    argPtr++;
  }
}

// FIXME. Consolidate this routine with RewriteBlockPointerType.
void RewriteObjC::RewriteBlockPointerTypeVariable(std::string& Str,
                                                  ValueDecl *VD) {
  QualType Type = VD->getType();
  std::string TypeString(Type.getAsString(Context->getPrintingPolicy()));
  const char *argPtr = TypeString.c_str();
  int paren = 0;
  while (*argPtr) {
    switch (*argPtr) {
      case '(':
        Str += *argPtr;
        paren++;
        break;
      case ')':
        Str += *argPtr;
        paren--;
        break;
      case '^':
        Str += '*';
        if (paren == 1)
          Str += VD->getNameAsString();
        break;
      default:
        Str += *argPtr;
        break;
    }
    argPtr++;
  }
}

void RewriteObjC::RewriteBlockLiteralFunctionDecl(FunctionDecl *FD) {
  SourceLocation FunLocStart = FD->getTypeSpecStartLoc();
  const FunctionType *funcType = FD->getType()->getAs<FunctionType>();
  const FunctionProtoType *proto = dyn_cast_or_null<FunctionProtoType>(funcType);
  if (!proto)
    return;
  QualType Type = proto->getReturnType();
  std::string FdStr = Type.getAsString(Context->getPrintingPolicy());
  FdStr += " ";
  FdStr += FD->getName();
  FdStr +=  "(";
  unsigned numArgs = proto->getNumParams();
  for (unsigned i = 0; i < numArgs; i++) {
    QualType ArgType = proto->getParamType(i);
    RewriteBlockPointerType(FdStr, ArgType);
    if (i+1 < numArgs)
      FdStr += ", ";
  }
  FdStr +=  ");\n";
  InsertText(FunLocStart, FdStr);
  CurFunctionDeclToDeclareForBlock = nullptr;
}

// SynthSuperConstructorFunctionDecl - id objc_super(id obj, id super);
void RewriteObjC::SynthSuperConstructorFunctionDecl() {
  if (SuperConstructorFunctionDecl)
    return;
  IdentifierInfo *msgSendIdent = &Context->Idents.get("__rw_objc_super");
  SmallVector<QualType, 16> ArgTys;
  QualType argT = Context->getObjCIdType();
  assert(!argT.isNull() && "Can't find 'id' type");
  ArgTys.push_back(argT);
  ArgTys.push_back(argT);
  QualType msgSendType = getSimpleFunctionType(Context->getObjCIdType(),
                                               ArgTys);
  SuperConstructorFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                                     SourceLocation(),
                                                     SourceLocation(),
                                                     msgSendIdent, msgSendType,
                                                     nullptr, SC_Extern);
}

// SynthMsgSendFunctionDecl - id objc_msgSend(id self, SEL op, ...);
void RewriteObjC::SynthMsgSendFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSend");
  SmallVector<QualType, 16> ArgTys;
  QualType argT = Context->getObjCIdType();
  assert(!argT.isNull() && "Can't find 'id' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = getSimpleFunctionType(Context->getObjCIdType(),
                                               ArgTys, /*variadic=*/true);
  MsgSendFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                             SourceLocation(),
                                             SourceLocation(),
                                             msgSendIdent, msgSendType,
                                             nullptr, SC_Extern);
}

// SynthMsgSendSuperFunctionDecl - id objc_msgSendSuper(struct objc_super *, SEL op, ...);
void RewriteObjC::SynthMsgSendSuperFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSendSuper");
  SmallVector<QualType, 16> ArgTys;
  RecordDecl *RD = RecordDecl::Create(*Context, TagTypeKind::Struct, TUDecl,
                                      SourceLocation(), SourceLocation(),
                                      &Context->Idents.get("objc_super"));
  QualType argT = Context->getPointerType(Context->getTagDeclType(RD));
  assert(!argT.isNull() && "Can't build 'struct objc_super *' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = getSimpleFunctionType(Context->getObjCIdType(),
                                               ArgTys, /*variadic=*/true);
  MsgSendSuperFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                                  SourceLocation(),
                                                  SourceLocation(),
                                                  msgSendIdent, msgSendType,
                                                  nullptr, SC_Extern);
}

// SynthMsgSendStretFunctionDecl - id objc_msgSend_stret(id self, SEL op, ...);
void RewriteObjC::SynthMsgSendStretFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSend_stret");
  SmallVector<QualType, 16> ArgTys;
  QualType argT = Context->getObjCIdType();
  assert(!argT.isNull() && "Can't find 'id' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = getSimpleFunctionType(Context->getObjCIdType(),
                                               ArgTys, /*variadic=*/true);
  MsgSendStretFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                                  SourceLocation(),
                                                  SourceLocation(),
                                                  msgSendIdent, msgSendType,
                                                  nullptr, SC_Extern);
}

// SynthMsgSendSuperStretFunctionDecl -
// id objc_msgSendSuper_stret(struct objc_super *, SEL op, ...);
void RewriteObjC::SynthMsgSendSuperStretFunctionDecl() {
  IdentifierInfo *msgSendIdent =
    &Context->Idents.get("objc_msgSendSuper_stret");
  SmallVector<QualType, 16> ArgTys;
  RecordDecl *RD = RecordDecl::Create(*Context, TagTypeKind::Struct, TUDecl,
                                      SourceLocation(), SourceLocation(),
                                      &Context->Idents.get("objc_super"));
  QualType argT = Context->getPointerType(Context->getTagDeclType(RD));
  assert(!argT.isNull() && "Can't build 'struct objc_super *' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = getSimpleFunctionType(Context->getObjCIdType(),
                                               ArgTys, /*variadic=*/true);
  MsgSendSuperStretFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                                       SourceLocation(),
                                                       SourceLocation(),
                                                       msgSendIdent,
                                                       msgSendType, nullptr,
                                                       SC_Extern);
}

// SynthMsgSendFpretFunctionDecl - double objc_msgSend_fpret(id self, SEL op, ...);
void RewriteObjC::SynthMsgSendFpretFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSend_fpret");
  SmallVector<QualType, 16> ArgTys;
  QualType argT = Context->getObjCIdType();
  assert(!argT.isNull() && "Can't find 'id' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = getSimpleFunctionType(Context->DoubleTy,
                                               ArgTys, /*variadic=*/true);
  MsgSendFpretFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                                  SourceLocation(),
                                                  SourceLocation(),
                                                  msgSendIdent, msgSendType,
                                                  nullptr, SC_Extern);
}

// SynthGetClassFunctionDecl - id objc_getClass(const char *name);
void RewriteObjC::SynthGetClassFunctionDecl() {
  IdentifierInfo *getClassIdent = &Context->Idents.get("objc_getClass");
  SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(Context->CharTy.withConst()));
  QualType getClassType = getSimpleFunctionType(Context->getObjCIdType(),
                                                ArgTys);
  GetClassFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                              SourceLocation(),
                                              SourceLocation(),
                                              getClassIdent, getClassType,
                                              nullptr, SC_Extern);
}

// SynthGetSuperClassFunctionDecl - Class class_getSuperclass(Class cls);
void RewriteObjC::SynthGetSuperClassFunctionDecl() {
  IdentifierInfo *getSuperClassIdent =
    &Context->Idents.get("class_getSuperclass");
  SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getObjCClassType());
  QualType getClassType = getSimpleFunctionType(Context->getObjCClassType(),
                                                ArgTys);
  GetSuperClassFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                                   SourceLocation(),
                                                   SourceLocation(),
                                                   getSuperClassIdent,
                                                   getClassType, nullptr,
                                                   SC_Extern);
}

// SynthGetMetaClassFunctionDecl - id objc_getMetaClass(const char *name);
void RewriteObjC::SynthGetMetaClassFunctionDecl() {
  IdentifierInfo *getClassIdent = &Context->Idents.get("objc_getMetaClass");
  SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(Context->CharTy.withConst()));
  QualType getClassType = getSimpleFunctionType(Context->getObjCIdType(),
                                                ArgTys);
  GetMetaClassFunctionDecl = FunctionDecl::Create(*Context, TUDecl,
                                                  SourceLocation(),
                                                  SourceLocation(),
                                                  getClassIdent, getClassType,
                                                  nullptr, SC_Extern);
}

Stmt *RewriteObjC::RewriteObjCStringLiteral(ObjCStringLiteral *Exp) {
  assert(Exp != nullptr && "Expected non-null ObjCStringLiteral");
  QualType strType = getConstantStringStructType();

  std::string S = "__NSConstantStringImpl_";

  std::string tmpName = InFileName;
  unsigned i;
  for (i=0; i < tmpName.length(); i++) {
    char c = tmpName.at(i);
    // replace any non-alphanumeric characters with '_'.
    if (!isAlphanumeric(c))
      tmpName[i] = '_';
  }
  S += tmpName;
  S += "_";
  S += utostr(NumObjCStringLiterals++);

  Preamble += "static __NSConstantStringImpl " + S;
  Preamble += " __attribute__ ((section (\"__DATA, __cfstring\"))) = {__CFConstantStringClassReference,";
  Preamble += "0x000007c8,"; // utf8_str
  // The pretty printer for StringLiteral handles escape characters properly.
  std::string prettyBufS;
  llvm::raw_string_ostream prettyBuf(prettyBufS);
  Exp->getString()->printPretty(prettyBuf, nullptr, PrintingPolicy(LangOpts));
  Preamble += prettyBuf.str();
  Preamble += ",";
  Preamble += utostr(Exp->getString()->getByteLength()) + "};\n";

  VarDecl *NewVD = VarDecl::Create(*Context, TUDecl, SourceLocation(),
                                   SourceLocation(), &Context->Idents.get(S),
                                   strType, nullptr, SC_Static);
  DeclRefExpr *DRE = new (Context)
      DeclRefExpr(*Context, NewVD, false, strType, VK_LValue, SourceLocation());
  Expr *Unop = UnaryOperator::Create(
      const_cast<ASTContext &>(*Context), DRE, UO_AddrOf,
      Context->getPointerType(DRE->getType()), VK_PRValue, OK_Ordinary,
      SourceLocation(), false, FPOptionsOverride());
  // cast to NSConstantString *
  CastExpr *cast = NoTypeInfoCStyleCastExpr(Context, Exp->getType(),
                                            CK_CPointerToObjCPointerCast, Unop);
  ReplaceStmt(Exp, cast);
  // delete Exp; leak for now, see RewritePropertyOrImplicitSetter() usage for more info.
  return cast;
}

// struct objc_super { struct objc_object *receiver; struct objc_class *super; };
QualType RewriteObjC::getSuperStructType() {
  if (!SuperStructDecl) {
    SuperStructDecl = RecordDecl::Create(*Context, TagTypeKind::Struct, TUDecl,
                                         SourceLocation(), SourceLocation(),
                                         &Context->Idents.get("objc_super"));
    QualType FieldTypes[2];

    // struct objc_object *receiver;
    FieldTypes[0] = Context->getObjCIdType();
    // struct objc_class *super;
    FieldTypes[1] = Context->getObjCClassType();

    // Create fields
    for (unsigned i = 0; i < 2; ++i) {
      SuperStructDecl->addDecl(FieldDecl::Create(*Context, SuperStructDecl,
                                                 SourceLocation(),
                                                 SourceLocation(), nullptr,
                                                 FieldTypes[i], nullptr,
                                                 /*BitWidth=*/nullptr,
                                                 /*Mutable=*/false,
                                                 ICIS_NoInit));
    }

    SuperStructDecl->completeDefinition();
  }
  return Context->getTagDeclType(SuperStructDecl);
}

QualType RewriteObjC::getConstantStringStructType() {
  if (!ConstantStringDecl) {
    ConstantStringDecl = RecordDecl::Create(
        *Context, TagTypeKind::Struct, TUDecl, SourceLocation(),
        SourceLocation(), &Context->Idents.get("__NSConstantStringImpl"));
    QualType FieldTypes[4];

    // struct objc_object *receiver;
    FieldTypes[0] = Context->getObjCIdType();
    // int flags;
    FieldTypes[1] = Context->IntTy;
    // char *str;
    FieldTypes[2] = Context->getPointerType(Context->CharTy);
    // long length;
    FieldTypes[3] = Context->LongTy;

    // Create fields
    for (unsigned i = 0; i < 4; ++i) {
      ConstantStringDecl->addDecl(FieldDecl::Create(*Context,
                                                    ConstantStringDecl,
                                                    SourceLocation(),
                                                    SourceLocation(), nullptr,
                                                    FieldTypes[i], nullptr,
                                                    /*BitWidth=*/nullptr,
                                                    /*Mutable=*/true,
                                                    ICIS_NoInit));
    }

    ConstantStringDecl->completeDefinition();
  }
  return Context->getTagDeclType(ConstantStringDecl);
}

CallExpr *RewriteObjC::SynthMsgSendStretCallExpr(FunctionDecl *MsgSendStretFlavor,
                                                QualType msgSendType,
                                                QualType returnType,
                                                SmallVectorImpl<QualType> &ArgTypes,
                                                SmallVectorImpl<Expr*> &MsgExprs,
                                                ObjCMethodDecl *Method) {
  // Create a reference to the objc_msgSend_stret() declaration.
  DeclRefExpr *STDRE =
      new (Context) DeclRefExpr(*Context, MsgSendStretFlavor, false,
                                msgSendType, VK_LValue, SourceLocation());
  // Need to cast objc_msgSend_stret to "void *" (see above comment).
  CastExpr *cast = NoTypeInfoCStyleCastExpr(Context,
                                  Context->getPointerType(Context->VoidTy),
                                  CK_BitCast, STDRE);
  // Now do the "normal" pointer to function cast.
  QualType castType = getSimpleFunctionType(returnType, ArgTypes,
                                            Method ? Method->isVariadic()
                                                   : false);
  castType = Context->getPointerType(castType);
  cast = NoTypeInfoCStyleCastExpr(Context, castType, CK_BitCast,
                                            cast);

  // Don't forget the parens to enforce the proper binding.
  ParenExpr *PE = new (Context) ParenExpr(SourceLocation(), SourceLocation(), cast);

  const auto *FT = msgSendType->castAs<FunctionType>();
  CallExpr *STCE =
      CallExpr::Create(*Context, PE, MsgExprs, FT->getReturnType(), VK_PRValue,
                       SourceLocation(), FPOptionsOverride());
  return STCE;
}

Stmt *RewriteObjC::SynthMessageExpr(ObjCMessageExpr *Exp,
                                    SourceLocation StartLoc,
                                    SourceLocation EndLoc) {
  if (!SelGetUidFunctionDecl)
    SynthSelGetUidFunctionDecl();
  if (!MsgSendFunctionDecl)
    SynthMsgSendFunctionDecl();
  if (!MsgSendSuperFunctionDecl)
    SynthMsgSendSuperFunctionDecl();
  if (!MsgSendStretFunctionDecl)
    SynthMsgSendStretFunctionDecl();
  if (!MsgSendSuperStretFunctionDecl)
    SynthMsgSendSuperStretFunctionDecl();
  if (!MsgSendFpretFunctionDecl)
    SynthMsgSendFpretFunctionDecl();
  if (!GetClassFunctionDecl)
    SynthGetClassFunctionDecl();
  if (!GetSuperClassFunctionDecl)
    SynthGetSuperClassFunctionDecl();
  if (!GetMetaClassFunctionDecl)
    SynthGetMetaClassFunctionDecl();

  // default to objc_msgSend().
  FunctionDecl *MsgSendFlavor = MsgSendFunctionDecl;
  // May need to use objc_msgSend_stret() as well.
  FunctionDecl *MsgSendStretFlavor = nullptr;
  if (ObjCMethodDecl *mDecl = Exp->getMethodDecl()) {
    QualType resultType = mDecl->getReturnType();
    if (resultType->isRecordType())
      MsgSendStretFlavor = MsgSendStretFunctionDecl;
    else if (resultType->isRealFloatingType())
      MsgSendFlavor = MsgSendFpretFunctionDecl;
  }

  // Synthesize a call to objc_msgSend().
  SmallVector<Expr*, 8> MsgExprs;
  switch (Exp->getReceiverKind()) {
  case ObjCMessageExpr::SuperClass: {
    MsgSendFlavor = MsgSendSuperFunctionDecl;
    if (MsgSendStretFlavor)
      MsgSendStretFlavor = MsgSendSuperStretFunctionDecl;
    assert(MsgSendFlavor && "MsgSendFlavor is NULL!");

    ObjCInterfaceDecl *ClassDecl = CurMethodDef->getClassInterface();

    SmallVector<Expr*, 4> InitExprs;

    // set the receiver to self, the first argument to all methods.
    InitExprs.push_back(NoTypeInfoCStyleCastExpr(
        Context, Context->getObjCIdType(), CK_BitCast,
        new (Context) DeclRefExpr(*Context, CurMethodDef->getSelfDecl(), false,
                                  Context->getObjCIdType(), VK_PRValue,
                                  SourceLocation()))); // set the 'receiver'.

    // (id)class_getSuperclass((Class)objc_getClass("CurrentClass"))
    SmallVector<Expr*, 8> ClsExprs;
    ClsExprs.push_back(getStringLiteral(ClassDecl->getIdentifier()->getName()));
    CallExpr *Cls = SynthesizeCallToFunctionDecl(GetMetaClassFunctionDecl,
                                                 ClsExprs, StartLoc, EndLoc);
    // (Class)objc_getClass("CurrentClass")
    CastExpr *ArgExpr = NoTypeInfoCStyleCastExpr(Context,
                                             Context->getObjCClassType(),
                                             CK_BitCast, Cls);
    ClsExprs.clear();
    ClsExprs.push_back(ArgExpr);
    Cls = SynthesizeCallToFunctionDecl(GetSuperClassFunctionDecl, ClsExprs,
                                       StartLoc, EndLoc);
    // (id)class_getSuperclass((Class)objc_getClass("CurrentClass"))
    // To turn off a warning, type-cast to 'id'
    InitExprs.push_back( // set 'super class', using class_getSuperclass().
                        NoTypeInfoCStyleCastExpr(Context,
                                                 Context->getObjCIdType(),
                                                 CK_BitCast, Cls));
    // struct objc_super
    QualType superType = getSuperStructType();
    Expr *SuperRep;

    if (LangOpts.MicrosoftExt) {
      SynthSuperConstructorFunctionDecl();
      // Simulate a constructor call...
      DeclRefExpr *DRE = new (Context)
          DeclRefExpr(*Context, SuperConstructorFunctionDecl, false, superType,
                      VK_LValue, SourceLocation());
      SuperRep =
          CallExpr::Create(*Context, DRE, InitExprs, superType, VK_LValue,
                           SourceLocation(), FPOptionsOverride());
      // The code for super is a little tricky to prevent collision with
      // the structure definition in the header. The rewriter has it's own
      // internal definition (__rw_objc_super) that is uses. This is why
      // we need the cast below. For example:
      // (struct objc_super *)&__rw_objc_super((id)self, (id)objc_getClass("SUPER"))
      //
      SuperRep = UnaryOperator::Create(
          const_cast<ASTContext &>(*Context), SuperRep, UO_AddrOf,
          Context->getPointerType(SuperRep->getType()), VK_PRValue, OK_Ordinary,
          SourceLocation(), false, FPOptionsOverride());
      SuperRep = NoTypeInfoCStyleCastExpr(Context,
                                          Context->getPointerType(superType),
                                          CK_BitCast, SuperRep);
    } else {
      // (struct objc_super) { <exprs from above> }
      InitListExpr *ILE =
        new (Context) InitListExpr(*Context, SourceLocation(), InitExprs,
                                   SourceLocation());
      TypeSourceInfo *superTInfo
        = Context->getTrivialTypeSourceInfo(superType);
      SuperRep = new (Context) CompoundLiteralExpr(SourceLocation(), superTInfo,
                                                   superType, VK_LValue,
                                                   ILE, false);
      // struct objc_super *
      SuperRep = UnaryOperator::Create(
          const_cast<ASTContext &>(*Context), SuperRep, UO_AddrOf,
          Context->getPointerType(SuperRep->getType()), VK_PRValue, OK_Ordinary,
          SourceLocation(), false, FPOptionsOverride());
    }
    MsgExprs.push_back(SuperRep);
    break;
  }

  case ObjCMessageExpr::Class: {
    SmallVector<Expr*, 8> ClsExprs;
    auto *Class =
        Exp->getClassReceiver()->castAs<ObjCObjectType>()->getInterface();
    IdentifierInfo *clsName = Class->getIdentifier();
    ClsExprs.push_back(getStringLiteral(clsName->getName()));
    CallExpr *Cls = SynthesizeCallToFunctionDecl(GetClassFunctionDecl, ClsExprs,
                                                 StartLoc, EndLoc);
    MsgExprs.push_back(Cls);
    break;
  }

  case ObjCMessageExpr::SuperInstance:{
    MsgSendFlavor = MsgSendSuperFunctionDecl;
    if (MsgSendStretFlavor)
      MsgSendStretFlavor = MsgSendSuperStretFunctionDecl;
    assert(MsgSendFlavor && "MsgSendFlavor is NULL!");
    ObjCInterfaceDecl *ClassDecl = CurMethodDef->getClassInterface();
    SmallVector<Expr*, 4> InitExprs;

    InitExprs.push_back(NoTypeInfoCStyleCastExpr(
        Context, Context->getObjCIdType(), CK_BitCast,
        new (Context) DeclRefExpr(*Context, CurMethodDef->getSelfDecl(), false,
                                  Context->getObjCIdType(), VK_PRValue,
                                  SourceLocation()))); // set the 'receiver'.

    // (id)class_getSuperclass((Class)objc_getClass("CurrentClass"))
    SmallVector<Expr*, 8> ClsExprs;
    ClsExprs.push_back(getStringLiteral(ClassDecl->getIdentifier()->getName()));
    CallExpr *Cls = SynthesizeCallToFunctionDecl(GetClassFunctionDecl, ClsExprs,
                                                 StartLoc, EndLoc);
    // (Class)objc_getClass("CurrentClass")
    CastExpr *ArgExpr = NoTypeInfoCStyleCastExpr(Context,
                                                 Context->getObjCClassType(),
                                                 CK_BitCast, Cls);
    ClsExprs.clear();
    ClsExprs.push_back(ArgExpr);
    Cls = SynthesizeCallToFunctionDecl(GetSuperClassFunctionDecl, ClsExprs,
                                       StartLoc, EndLoc);

    // (id)class_getSuperclass((Class)objc_getClass("CurrentClass"))
    // To turn off a warning, type-cast to 'id'
    InitExprs.push_back(
      // set 'super class', using class_getSuperclass().
      NoTypeInfoCStyleCastExpr(Context, Context->getObjCIdType(),
                               CK_BitCast, Cls));
    // struct objc_super
    QualType superType = getSuperStructType();
    Expr *SuperRep;

    if (LangOpts.MicrosoftExt) {
      SynthSuperConstructorFunctionDecl();
      // Simulate a constructor call...
      DeclRefExpr *DRE = new (Context)
          DeclRefExpr(*Context, SuperConstructorFunctionDecl, false, superType,
                      VK_LValue, SourceLocation());
      SuperRep =
          CallExpr::Create(*Context, DRE, InitExprs, superType, VK_LValue,
                           SourceLocation(), FPOptionsOverride());
      // The code for super is a little tricky to prevent collision with
      // the structure definition in the header. The rewriter has it's own
      // internal definition (__rw_objc_super) that is uses. This is why
      // we need the cast below. For example:
      // (struct objc_super *)&__rw_objc_super((id)self, (id)objc_getClass("SUPER"))
      //
      SuperRep = UnaryOperator::Create(
          const_cast<ASTContext &>(*Context), SuperRep, UO_AddrOf,
          Context->getPointerType(SuperRep->getType()), VK_PRValue, OK_Ordinary,
          SourceLocation(), false, FPOptionsOverride());
      SuperRep = NoTypeInfoCStyleCastExpr(Context,
                               Context->getPointerType(superType),
                               CK_BitCast, SuperRep);
    } else {
      // (struct objc_super) { <exprs from above> }
      InitListExpr *ILE =
        new (Context) InitListExpr(*Context, SourceLocation(), InitExprs,
                                   SourceLocation());
      TypeSourceInfo *superTInfo
        = Context->getTrivialTypeSourceInfo(superType);
      SuperRep = new (Context) CompoundLiteralExpr(
          SourceLocation(), superTInfo, superType, VK_PRValue, ILE, false);
    }
    MsgExprs.push_back(SuperRep);
    break;
  }

  case ObjCMessageExpr::Instance: {
    // Remove all type-casts because it may contain objc-style types; e.g.
    // Foo<Proto> *.
    Expr *recExpr = Exp->getInstanceReceiver();
    while (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(recExpr))
      recExpr = CE->getSubExpr();
    CastKind CK = recExpr->getType()->isObjCObjectPointerType()
                    ? CK_BitCast : recExpr->getType()->isBlockPointerType()
                                     ? CK_BlockPointerToObjCPointerCast
                                     : CK_CPointerToObjCPointerCast;

    recExpr = NoTypeInfoCStyleCastExpr(Context, Context->getObjCIdType(),
                                       CK, recExpr);
    MsgExprs.push_back(recExpr);
    break;
  }
  }

  // Create a call to sel_registerName("selName"), it will be the 2nd argument.
  SmallVector<Expr*, 8> SelExprs;
  SelExprs.push_back(getStringLiteral(Exp->getSelector().getAsString()));
  CallExpr *SelExp = SynthesizeCallToFunctionDecl(SelGetUidFunctionDecl,
                                                  SelExprs, StartLoc, EndLoc);
  MsgExprs.push_back(SelExp);

  // Now push any user supplied arguments.
  for (unsigned i = 0; i < Exp->getNumArgs(); i++) {
    Expr *userExpr = Exp->getArg(i);
    // Make all implicit casts explicit...ICE comes in handy:-)
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(userExpr)) {
      // Reuse the ICE type, it is exactly what the doctor ordered.
      QualType type = ICE->getType();
      if (needToScanForQualifiers(type))
        type = Context->getObjCIdType();
      // Make sure we convert "type (^)(...)" to "type (*)(...)".
      (void)convertBlockPointerToFunctionPointer(type);
      const Expr *SubExpr = ICE->IgnoreParenImpCasts();
      CastKind CK;
      if (SubExpr->getType()->isIntegralType(*Context) &&
          type->isBooleanType()) {
        CK = CK_IntegralToBoolean;
      } else if (type->isObjCObjectPointerType()) {
        if (SubExpr->getType()->isBlockPointerType()) {
          CK = CK_BlockPointerToObjCPointerCast;
        } else if (SubExpr->getType()->isPointerType()) {
          CK = CK_CPointerToObjCPointerCast;
        } else {
          CK = CK_BitCast;
        }
      } else {
        CK = CK_BitCast;
      }

      userExpr = NoTypeInfoCStyleCastExpr(Context, type, CK, userExpr);
    }
    // Make id<P...> cast into an 'id' cast.
    else if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(userExpr)) {
      if (CE->getType()->isObjCQualifiedIdType()) {
        while ((CE = dyn_cast<CStyleCastExpr>(userExpr)))
          userExpr = CE->getSubExpr();
        CastKind CK;
        if (userExpr->getType()->isIntegralType(*Context)) {
          CK = CK_IntegralToPointer;
        } else if (userExpr->getType()->isBlockPointerType()) {
          CK = CK_BlockPointerToObjCPointerCast;
        } else if (userExpr->getType()->isPointerType()) {
          CK = CK_CPointerToObjCPointerCast;
        } else {
          CK = CK_BitCast;
        }
        userExpr = NoTypeInfoCStyleCastExpr(Context, Context->getObjCIdType(),
                                            CK, userExpr);
      }
    }
    MsgExprs.push_back(userExpr);
    // We've transferred the ownership to MsgExprs. For now, we *don't* null
    // out the argument in the original expression (since we aren't deleting
    // the ObjCMessageExpr). See RewritePropertyOrImplicitSetter() usage for more info.
    //Exp->setArg(i, 0);
  }
  // Generate the funky cast.
  CastExpr *cast;
  SmallVector<QualType, 8> ArgTypes;
  QualType returnType;

  // Push 'id' and 'SEL', the 2 implicit arguments.
  if (MsgSendFlavor == MsgSendSuperFunctionDecl)
    ArgTypes.push_back(Context->getPointerType(getSuperStructType()));
  else
    ArgTypes.push_back(Context->getObjCIdType());
  ArgTypes.push_back(Context->getObjCSelType());
  if (ObjCMethodDecl *OMD = Exp->getMethodDecl()) {
    // Push any user argument types.
    for (const auto *PI : OMD->parameters()) {
      QualType t = PI->getType()->isObjCQualifiedIdType()
                     ? Context->getObjCIdType()
                     : PI->getType();
      // Make sure we convert "t (^)(...)" to "t (*)(...)".
      (void)convertBlockPointerToFunctionPointer(t);
      ArgTypes.push_back(t);
    }
    returnType = Exp->getType();
    convertToUnqualifiedObjCType(returnType);
    (void)convertBlockPointerToFunctionPointer(returnType);
  } else {
    returnType = Context->getObjCIdType();
  }
  // Get the type, we will need to reference it in a couple spots.
  QualType msgSendType = MsgSendFlavor->getType();

  // Create a reference to the objc_msgSend() declaration.
  DeclRefExpr *DRE = new (Context) DeclRefExpr(
      *Context, MsgSendFlavor, false, msgSendType, VK_LValue, SourceLocation());

  // Need to cast objc_msgSend to "void *" (to workaround a GCC bandaid).
  // If we don't do this cast, we get the following bizarre warning/note:
  // xx.m:13: warning: function called through a non-compatible type
  // xx.m:13: note: if this code is reached, the program will abort
  cast = NoTypeInfoCStyleCastExpr(Context,
                                  Context->getPointerType(Context->VoidTy),
                                  CK_BitCast, DRE);

  // Now do the "normal" pointer to function cast.
  // If we don't have a method decl, force a variadic cast.
  const ObjCMethodDecl *MD = Exp->getMethodDecl();
  QualType castType =
    getSimpleFunctionType(returnType, ArgTypes, MD ? MD->isVariadic() : true);
  castType = Context->getPointerType(castType);
  cast = NoTypeInfoCStyleCastExpr(Context, castType, CK_BitCast,
                                  cast);

  // Don't forget the parens to enforce the proper binding.
  ParenExpr *PE = new (Context) ParenExpr(StartLoc, EndLoc, cast);

  const auto *FT = msgSendType->castAs<FunctionType>();
  CallExpr *CE = CallExpr::Create(*Context, PE, MsgExprs, FT->getReturnType(),
                                  VK_PRValue, EndLoc, FPOptionsOverride());
  Stmt *ReplacingStmt = CE;
  if (MsgSendStretFlavor) {
    // We have the method which returns a struct/union. Must also generate
    // call to objc_msgSend_stret and hang both varieties on a conditional
    // expression which dictate which one to envoke depending on size of
    // method's return type.

    CallExpr *STCE = SynthMsgSendStretCallExpr(MsgSendStretFlavor,
                                               msgSendType, returnType,
                                               ArgTypes, MsgExprs,
                                               Exp->getMethodDecl());

    // Build sizeof(returnType)
    UnaryExprOrTypeTraitExpr *sizeofExpr =
       new (Context) UnaryExprOrTypeTraitExpr(UETT_SizeOf,
                                 Context->getTrivialTypeSourceInfo(returnType),
                                 Context->getSizeType(), SourceLocation(),
                                 SourceLocation());
    // (sizeof(returnType) <= 8 ? objc_msgSend(...) : objc_msgSend_stret(...))
    // FIXME: Value of 8 is base on ppc32/x86 ABI for the most common cases.
    // For X86 it is more complicated and some kind of target specific routine
    // is needed to decide what to do.
    unsigned IntSize =
      static_cast<unsigned>(Context->getTypeSize(Context->IntTy));
    IntegerLiteral *limit = IntegerLiteral::Create(*Context,
                                                   llvm::APInt(IntSize, 8),
                                                   Context->IntTy,
                                                   SourceLocation());
    BinaryOperator *lessThanExpr = BinaryOperator::Create(
        *Context, sizeofExpr, limit, BO_LE, Context->IntTy, VK_PRValue,
        OK_Ordinary, SourceLocation(), FPOptionsOverride());
    // (sizeof(returnType) <= 8 ? objc_msgSend(...) : objc_msgSend_stret(...))
    ConditionalOperator *CondExpr = new (Context) ConditionalOperator(
        lessThanExpr, SourceLocation(), CE, SourceLocation(), STCE, returnType,
        VK_PRValue, OK_Ordinary);
    ReplacingStmt = new (Context) ParenExpr(SourceLocation(), SourceLocation(),
                                            CondExpr);
  }
  // delete Exp; leak for now, see RewritePropertyOrImplicitSetter() usage for more info.
  return ReplacingStmt;
}

Stmt *RewriteObjC::RewriteMessageExpr(ObjCMessageExpr *Exp) {
  Stmt *ReplacingStmt =
      SynthMessageExpr(Exp, Exp->getBeginLoc(), Exp->getEndLoc());

  // Now do the actual rewrite.
  ReplaceStmt(Exp, ReplacingStmt);

  // delete Exp; leak for now, see RewritePropertyOrImplicitSetter() usage for more info.
  return ReplacingStmt;
}

// typedef struct objc_object Protocol;
QualType RewriteObjC::getProtocolType() {
  if (!ProtocolTypeDecl) {
    TypeSourceInfo *TInfo
      = Context->getTrivialTypeSourceInfo(Context->getObjCIdType());
    ProtocolTypeDecl = TypedefDecl::Create(*Context, TUDecl,
                                           SourceLocation(), SourceLocation(),
                                           &Context->Idents.get("Protocol"),
                                           TInfo);
  }
  return Context->getTypeDeclType(ProtocolTypeDecl);
}

/// RewriteObjCProtocolExpr - Rewrite a protocol expression into
/// a synthesized/forward data reference (to the protocol's metadata).
/// The forward references (and metadata) are generated in
/// RewriteObjC::HandleTranslationUnit().
Stmt *RewriteObjC::RewriteObjCProtocolExpr(ObjCProtocolExpr *Exp) {
  std::string Name = "_OBJC_PROTOCOL_" + Exp->getProtocol()->getNameAsString();
  IdentifierInfo *ID = &Context->Idents.get(Name);
  VarDecl *VD = VarDecl::Create(*Context, TUDecl, SourceLocation(),
                                SourceLocation(), ID, getProtocolType(),
                                nullptr, SC_Extern);
  DeclRefExpr *DRE = new (Context) DeclRefExpr(
      *Context, VD, false, getProtocolType(), VK_LValue, SourceLocation());
  Expr *DerefExpr = UnaryOperator::Create(
      const_cast<ASTContext &>(*Context), DRE, UO_AddrOf,
      Context->getPointerType(DRE->getType()), VK_PRValue, OK_Ordinary,
      SourceLocation(), false, FPOptionsOverride());
  CastExpr *castExpr = NoTypeInfoCStyleCastExpr(Context, DerefExpr->getType(),
                                                CK_BitCast,
                                                DerefExpr);
  ReplaceStmt(Exp, castExpr);
  ProtocolExprDecls.insert(Exp->getProtocol()->getCanonicalDecl());
  // delete Exp; leak for now, see RewritePropertyOrImplicitSetter() usage for more info.
  return castExpr;
}

bool RewriteObjC::BufferContainsPPDirectives(const char *startBuf,
                                             const char *endBuf) {
  while (startBuf < endBuf) {
    if (*startBuf == '#') {
      // Skip whitespace.
      for (++startBuf; startBuf[0] == ' ' || startBuf[0] == '\t'; ++startBuf)
        ;
      if (!strncmp(startBuf, "if", strlen("if")) ||
          !strncmp(startBuf, "ifdef", strlen("ifdef")) ||
          !strncmp(startBuf, "ifndef", strlen("ifndef")) ||
          !strncmp(startBuf, "define", strlen("define")) ||
          !strncmp(startBuf, "undef", strlen("undef")) ||
          !strncmp(startBuf, "else", strlen("else")) ||
          !strncmp(startBuf, "elif", strlen("elif")) ||
          !strncmp(startBuf, "endif", strlen("endif")) ||
          !strncmp(startBuf, "pragma", strlen("pragma")) ||
          !strncmp(startBuf, "include", strlen("include")) ||
          !strncmp(startBuf, "import", strlen("import")) ||
          !strncmp(startBuf, "include_next", strlen("include_next")))
        return true;
    }
    startBuf++;
  }
  return false;
}

/// RewriteObjCInternalStruct - Rewrite one internal struct corresponding to
/// an objective-c class with ivars.
void RewriteObjC::RewriteObjCInternalStruct(ObjCInterfaceDecl *CDecl,
                                               std::string &Result) {
  assert(CDecl && "Class missing in SynthesizeObjCInternalStruct");
  assert(CDecl->getName() != "" &&
         "Name missing in SynthesizeObjCInternalStruct");
  // Do not synthesize more than once.
  if (ObjCSynthesizedStructs.count(CDecl))
    return;
  ObjCInterfaceDecl *RCDecl = CDecl->getSuperClass();
  int NumIvars = CDecl->ivar_size();
  SourceLocation LocStart = CDecl->getBeginLoc();
  SourceLocation LocEnd = CDecl->getEndOfDefinitionLoc();

  const char *startBuf = SM->getCharacterData(LocStart);
  const char *endBuf = SM->getCharacterData(LocEnd);

  // If no ivars and no root or if its root, directly or indirectly,
  // have no ivars (thus not synthesized) then no need to synthesize this class.
  if ((!CDecl->isThisDeclarationADefinition() || NumIvars == 0) &&
      (!RCDecl || !ObjCSynthesizedStructs.count(RCDecl))) {
    endBuf += Lexer::MeasureTokenLength(LocEnd, *SM, LangOpts);
    ReplaceText(LocStart, endBuf-startBuf, Result);
    return;
  }

  // FIXME: This has potential of causing problem. If
  // SynthesizeObjCInternalStruct is ever called recursively.
  Result += "\nstruct ";
  Result += CDecl->getNameAsString();
  if (LangOpts.MicrosoftExt)
    Result += "_IMPL";

  if (NumIvars > 0) {
    const char *cursor = strchr(startBuf, '{');
    assert((cursor && endBuf)
           && "SynthesizeObjCInternalStruct - malformed @interface");
    // If the buffer contains preprocessor directives, we do more fine-grained
    // rewrites. This is intended to fix code that looks like (which occurs in
    // NSURL.h, for example):
    //
    // #ifdef XYZ
    // @interface Foo : NSObject
    // #else
    // @interface FooBar : NSObject
    // #endif
    // {
    //    int i;
    // }
    // @end
    //
    // This clause is segregated to avoid breaking the common case.
    if (BufferContainsPPDirectives(startBuf, cursor)) {
      SourceLocation L = RCDecl ? CDecl->getSuperClassLoc() :
                                  CDecl->getAtStartLoc();
      const char *endHeader = SM->getCharacterData(L);
      endHeader += Lexer::MeasureTokenLength(L, *SM, LangOpts);

      if (CDecl->protocol_begin() != CDecl->protocol_end()) {
        // advance to the end of the referenced protocols.
        while (endHeader < cursor && *endHeader != '>') endHeader++;
        endHeader++;
      }
      // rewrite the original header
      ReplaceText(LocStart, endHeader-startBuf, Result);
    } else {
      // rewrite the original header *without* disturbing the '{'
      ReplaceText(LocStart, cursor-startBuf, Result);
    }
    if (RCDecl && ObjCSynthesizedStructs.count(RCDecl)) {
      Result = "\n    struct ";
      Result += RCDecl->getNameAsString();
      Result += "_IMPL ";
      Result += RCDecl->getNameAsString();
      Result += "_IVARS;\n";

      // insert the super class structure definition.
      SourceLocation OnePastCurly =
        LocStart.getLocWithOffset(cursor-startBuf+1);
      InsertText(OnePastCurly, Result);
    }
    cursor++; // past '{'

    // Now comment out any visibility specifiers.
    while (cursor < endBuf) {
      if (*cursor == '@') {
        SourceLocation atLoc = LocStart.getLocWithOffset(cursor-startBuf);
        // Skip whitespace.
        for (++cursor; cursor[0] == ' ' || cursor[0] == '\t'; ++cursor)
          /*scan*/;

        // FIXME: presence of @public, etc. inside comment results in
        // this transformation as well, which is still correct c-code.
        if (!strncmp(cursor, "public", strlen("public")) ||
            !strncmp(cursor, "private", strlen("private")) ||
            !strncmp(cursor, "package", strlen("package")) ||
            !strncmp(cursor, "protected", strlen("protected")))
          InsertText(atLoc, "// ");
      }
      // FIXME: If there are cases where '<' is used in ivar declaration part
      // of user code, then scan the ivar list and use needToScanForQualifiers
      // for type checking.
      else if (*cursor == '<') {
        SourceLocation atLoc = LocStart.getLocWithOffset(cursor-startBuf);
        InsertText(atLoc, "/* ");
        cursor = strchr(cursor, '>');
        cursor++;
        atLoc = LocStart.getLocWithOffset(cursor-startBuf);
        InsertText(atLoc, " */");
      } else if (*cursor == '^') { // rewrite block specifier.
        SourceLocation caretLoc = LocStart.getLocWithOffset(cursor-startBuf);
        ReplaceText(caretLoc, 1, "*");
      }
      cursor++;
    }
    // Don't forget to add a ';'!!
    InsertText(LocEnd.getLocWithOffset(1), ";");
  } else { // we don't have any instance variables - insert super struct.
    endBuf += Lexer::MeasureTokenLength(LocEnd, *SM, LangOpts);
    Result += " {\n    struct ";
    Result += RCDecl->getNameAsString();
    Result += "_IMPL ";
    Result += RCDecl->getNameAsString();
    Result += "_IVARS;\n};\n";
    ReplaceText(LocStart, endBuf-startBuf, Result);
  }
  // Mark this struct as having been generated.
  if (!ObjCSynthesizedStructs.insert(CDecl).second)
    llvm_unreachable("struct already synthesize- SynthesizeObjCInternalStruct");
}

//===----------------------------------------------------------------------===//
// Meta Data Emission
//===----------------------------------------------------------------------===//

/// RewriteImplementations - This routine rewrites all method implementations
/// and emits meta-data.

void RewriteObjC::RewriteImplementations() {
  int ClsDefCount = ClassImplementation.size();
  int CatDefCount = CategoryImplementation.size();

  // Rewrite implemented methods
  for (int i = 0; i < ClsDefCount; i++)
    RewriteImplementationDecl(ClassImplementation[i]);

  for (int i = 0; i < CatDefCount; i++)
    RewriteImplementationDecl(CategoryImplementation[i]);
}

void RewriteObjC::RewriteByRefString(std::string &ResultStr,
                                     const std::string &Name,
                                     ValueDecl *VD, bool def) {
  assert(BlockByRefDeclNo.count(VD) &&
         "RewriteByRefString: ByRef decl missing");
  if (def)
    ResultStr += "struct ";
  ResultStr += "__Block_byref_" + Name +
    "_" + utostr(BlockByRefDeclNo[VD]) ;
}

static bool HasLocalVariableExternalStorage(ValueDecl *VD) {
  if (VarDecl *Var = dyn_cast<VarDecl>(VD))
    return (Var->isFunctionOrMethodVarDecl() && !Var->hasLocalStorage());
  return false;
}

std::string RewriteObjC::SynthesizeBlockFunc(BlockExpr *CE, int i,
                                                   StringRef funcName,
                                                   std::string Tag) {
  const FunctionType *AFT = CE->getFunctionType();
  QualType RT = AFT->getReturnType();
  std::string StructRef = "struct " + Tag;
  std::string S = "static " + RT.getAsString(Context->getPrintingPolicy()) + " __" +
                  funcName.str() + "_" + "block_func_" + utostr(i);

  BlockDecl *BD = CE->getBlockDecl();

  if (isa<FunctionNoProtoType>(AFT)) {
    // No user-supplied arguments. Still need to pass in a pointer to the
    // block (to reference imported block decl refs).
    S += "(" + StructRef + " *__cself)";
  } else if (BD->param_empty()) {
    S += "(" + StructRef + " *__cself)";
  } else {
    const FunctionProtoType *FT = cast<FunctionProtoType>(AFT);
    assert(FT && "SynthesizeBlockFunc: No function proto");
    S += '(';
    // first add the implicit argument.
    S += StructRef + " *__cself, ";
    std::string ParamStr;
    for (BlockDecl::param_iterator AI = BD->param_begin(),
         E = BD->param_end(); AI != E; ++AI) {
      if (AI != BD->param_begin()) S += ", ";
      ParamStr = (*AI)->getNameAsString();
      QualType QT = (*AI)->getType();
      (void)convertBlockPointerToFunctionPointer(QT);
      QT.getAsStringInternal(ParamStr, Context->getPrintingPolicy());
      S += ParamStr;
    }
    if (FT->isVariadic()) {
      if (!BD->param_empty()) S += ", ";
      S += "...";
    }
    S += ')';
  }
  S += " {\n";

  // Create local declarations to avoid rewriting all closure decl ref exprs.
  // First, emit a declaration for all "by ref" decls.
  for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByRefDecls.begin(),
       E = BlockByRefDecls.end(); I != E; ++I) {
    S += "  ";
    std::string Name = (*I)->getNameAsString();
    std::string TypeString;
    RewriteByRefString(TypeString, Name, (*I));
    TypeString += " *";
    Name = TypeString + Name;
    S += Name + " = __cself->" + (*I)->getNameAsString() + "; // bound by ref\n";
  }
  // Next, emit a declaration for all "by copy" declarations.
  for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByCopyDecls.begin(),
       E = BlockByCopyDecls.end(); I != E; ++I) {
    S += "  ";
    // Handle nested closure invocation. For example:
    //
    //   void (^myImportedClosure)(void);
    //   myImportedClosure  = ^(void) { setGlobalInt(x + y); };
    //
    //   void (^anotherClosure)(void);
    //   anotherClosure = ^(void) {
    //     myImportedClosure(); // import and invoke the closure
    //   };
    //
    if (isTopLevelBlockPointerType((*I)->getType())) {
      RewriteBlockPointerTypeVariable(S, (*I));
      S += " = (";
      RewriteBlockPointerType(S, (*I)->getType());
      S += ")";
      S += "__cself->" + (*I)->getNameAsString() + "; // bound by copy\n";
    }
    else {
      std::string Name = (*I)->getNameAsString();
      QualType QT = (*I)->getType();
      if (HasLocalVariableExternalStorage(*I))
        QT = Context->getPointerType(QT);
      QT.getAsStringInternal(Name, Context->getPrintingPolicy());
      S += Name + " = __cself->" +
                              (*I)->getNameAsString() + "; // bound by copy\n";
    }
  }
  std::string RewrittenStr = RewrittenBlockExprs[CE];
  const char *cstr = RewrittenStr.c_str();
  while (*cstr++ != '{') ;
  S += cstr;
  S += "\n";
  return S;
}

std::string RewriteObjC::SynthesizeBlockHelperFuncs(BlockExpr *CE, int i,
                                                   StringRef funcName,
                                                   std::string Tag) {
  std::string StructRef = "struct " + Tag;
  std::string S = "static void __";

  S += funcName;
  S += "_block_copy_" + utostr(i);
  S += "(" + StructRef;
  S += "*dst, " + StructRef;
  S += "*src) {";
  for (ValueDecl *VD : ImportedBlockDecls) {
    S += "_Block_object_assign((void*)&dst->";
    S += VD->getNameAsString();
    S += ", (void*)src->";
    S += VD->getNameAsString();
    if (BlockByRefDeclsPtrSet.count(VD))
      S += ", " + utostr(BLOCK_FIELD_IS_BYREF) + "/*BLOCK_FIELD_IS_BYREF*/);";
    else if (VD->getType()->isBlockPointerType())
      S += ", " + utostr(BLOCK_FIELD_IS_BLOCK) + "/*BLOCK_FIELD_IS_BLOCK*/);";
    else
      S += ", " + utostr(BLOCK_FIELD_IS_OBJECT) + "/*BLOCK_FIELD_IS_OBJECT*/);";
  }
  S += "}\n";

  S += "\nstatic void __";
  S += funcName;
  S += "_block_dispose_" + utostr(i);
  S += "(" + StructRef;
  S += "*src) {";
  for (ValueDecl *VD : ImportedBlockDecls) {
    S += "_Block_object_dispose((void*)src->";
    S += VD->getNameAsString();
    if (BlockByRefDeclsPtrSet.count(VD))
      S += ", " + utostr(BLOCK_FIELD_IS_BYREF) + "/*BLOCK_FIELD_IS_BYREF*/);";
    else if (VD->getType()->isBlockPointerType())
      S += ", " + utostr(BLOCK_FIELD_IS_BLOCK) + "/*BLOCK_FIELD_IS_BLOCK*/);";
    else
      S += ", " + utostr(BLOCK_FIELD_IS_OBJECT) + "/*BLOCK_FIELD_IS_OBJECT*/);";
  }
  S += "}\n";
  return S;
}

std::string RewriteObjC::SynthesizeBlockImpl(BlockExpr *CE, std::string Tag,
                                             std::string Desc) {
  std::string S = "\nstruct " + Tag;
  std::string Constructor = "  " + Tag;

  S += " {\n  struct __block_impl impl;\n";
  S += "  struct " + Desc;
  S += "* Desc;\n";

  Constructor += "(void *fp, "; // Invoke function pointer.
  Constructor += "struct " + Desc; // Descriptor pointer.
  Constructor += " *desc";

  if (BlockDeclRefs.size()) {
    // Output all "by copy" declarations.
    for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByCopyDecls.begin(),
         E = BlockByCopyDecls.end(); I != E; ++I) {
      S += "  ";
      std::string FieldName = (*I)->getNameAsString();
      std::string ArgName = "_" + FieldName;
      // Handle nested closure invocation. For example:
      //
      //   void (^myImportedBlock)(void);
      //   myImportedBlock  = ^(void) { setGlobalInt(x + y); };
      //
      //   void (^anotherBlock)(void);
      //   anotherBlock = ^(void) {
      //     myImportedBlock(); // import and invoke the closure
      //   };
      //
      if (isTopLevelBlockPointerType((*I)->getType())) {
        S += "struct __block_impl *";
        Constructor += ", void *" + ArgName;
      } else {
        QualType QT = (*I)->getType();
        if (HasLocalVariableExternalStorage(*I))
          QT = Context->getPointerType(QT);
        QT.getAsStringInternal(FieldName, Context->getPrintingPolicy());
        QT.getAsStringInternal(ArgName, Context->getPrintingPolicy());
        Constructor += ", " + ArgName;
      }
      S += FieldName + ";\n";
    }
    // Output all "by ref" declarations.
    for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByRefDecls.begin(),
         E = BlockByRefDecls.end(); I != E; ++I) {
      S += "  ";
      std::string FieldName = (*I)->getNameAsString();
      std::string ArgName = "_" + FieldName;
      {
        std::string TypeString;
        RewriteByRefString(TypeString, FieldName, (*I));
        TypeString += " *";
        FieldName = TypeString + FieldName;
        ArgName = TypeString + ArgName;
        Constructor += ", " + ArgName;
      }
      S += FieldName + "; // by ref\n";
    }
    // Finish writing the constructor.
    Constructor += ", int flags=0)";
    // Initialize all "by copy" arguments.
    bool firsTime = true;
    for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByCopyDecls.begin(),
         E = BlockByCopyDecls.end(); I != E; ++I) {
      std::string Name = (*I)->getNameAsString();
        if (firsTime) {
          Constructor += " : ";
          firsTime = false;
        }
        else
          Constructor += ", ";
        if (isTopLevelBlockPointerType((*I)->getType()))
          Constructor += Name + "((struct __block_impl *)_" + Name + ")";
        else
          Constructor += Name + "(_" + Name + ")";
    }
    // Initialize all "by ref" arguments.
    for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByRefDecls.begin(),
         E = BlockByRefDecls.end(); I != E; ++I) {
      std::string Name = (*I)->getNameAsString();
      if (firsTime) {
        Constructor += " : ";
        firsTime = false;
      }
      else
        Constructor += ", ";
      Constructor += Name + "(_" + Name + "->__forwarding)";
    }

    Constructor += " {\n";
    if (GlobalVarDecl)
      Constructor += "    impl.isa = &_NSConcreteGlobalBlock;\n";
    else
      Constructor += "    impl.isa = &_NSConcreteStackBlock;\n";
    Constructor += "    impl.Flags = flags;\n    impl.FuncPtr = fp;\n";

    Constructor += "    Desc = desc;\n";
  } else {
    // Finish writing the constructor.
    Constructor += ", int flags=0) {\n";
    if (GlobalVarDecl)
      Constructor += "    impl.isa = &_NSConcreteGlobalBlock;\n";
    else
      Constructor += "    impl.isa = &_NSConcreteStackBlock;\n";
    Constructor += "    impl.Flags = flags;\n    impl.FuncPtr = fp;\n";
    Constructor += "    Desc = desc;\n";
  }
  Constructor += "  ";
  Constructor += "}\n";
  S += Constructor;
  S += "};\n";
  return S;
}

std::string RewriteObjC::SynthesizeBlockDescriptor(std::string DescTag,
                                                   std::string ImplTag, int i,
                                                   StringRef FunName,
                                                   unsigned hasCopy) {
  std::string S = "\nstatic struct " + DescTag;

  S += " {\n  unsigned long reserved;\n";
  S += "  unsigned long Block_size;\n";
  if (hasCopy) {
    S += "  void (*copy)(struct ";
    S += ImplTag; S += "*, struct ";
    S += ImplTag; S += "*);\n";

    S += "  void (*dispose)(struct ";
    S += ImplTag; S += "*);\n";
  }
  S += "} ";

  S += DescTag + "_DATA = { 0, sizeof(struct ";
  S += ImplTag + ")";
  if (hasCopy) {
    S += ", __" + FunName.str() + "_block_copy_" + utostr(i);
    S += ", __" + FunName.str() + "_block_dispose_" + utostr(i);
  }
  S += "};\n";
  return S;
}

void RewriteObjC::SynthesizeBlockLiterals(SourceLocation FunLocStart,
                                          StringRef FunName) {
  // Insert declaration for the function in which block literal is used.
  if (CurFunctionDeclToDeclareForBlock && !Blocks.empty())
    RewriteBlockLiteralFunctionDecl(CurFunctionDeclToDeclareForBlock);
  bool RewriteSC = (GlobalVarDecl &&
                    !Blocks.empty() &&
                    GlobalVarDecl->getStorageClass() == SC_Static &&
                    GlobalVarDecl->getType().getCVRQualifiers());
  if (RewriteSC) {
    std::string SC(" void __");
    SC += GlobalVarDecl->getNameAsString();
    SC += "() {}";
    InsertText(FunLocStart, SC);
  }

  // Insert closures that were part of the function.
  for (unsigned i = 0, count=0; i < Blocks.size(); i++) {
    CollectBlockDeclRefInfo(Blocks[i]);
    // Need to copy-in the inner copied-in variables not actually used in this
    // block.
    for (int j = 0; j < InnerDeclRefsCount[i]; j++) {
      DeclRefExpr *Exp = InnerDeclRefs[count++];
      ValueDecl *VD = Exp->getDecl();
      BlockDeclRefs.push_back(Exp);
      if (!VD->hasAttr<BlocksAttr>() && !BlockByCopyDeclsPtrSet.count(VD)) {
        BlockByCopyDeclsPtrSet.insert(VD);
        BlockByCopyDecls.push_back(VD);
      }
      if (VD->hasAttr<BlocksAttr>() && !BlockByRefDeclsPtrSet.count(VD)) {
        BlockByRefDeclsPtrSet.insert(VD);
        BlockByRefDecls.push_back(VD);
      }
      // imported objects in the inner blocks not used in the outer
      // blocks must be copied/disposed in the outer block as well.
      if (VD->hasAttr<BlocksAttr>() ||
          VD->getType()->isObjCObjectPointerType() ||
          VD->getType()->isBlockPointerType())
        ImportedBlockDecls.insert(VD);
    }

    std::string ImplTag = "__" + FunName.str() + "_block_impl_" + utostr(i);
    std::string DescTag = "__" + FunName.str() + "_block_desc_" + utostr(i);

    std::string CI = SynthesizeBlockImpl(Blocks[i], ImplTag, DescTag);

    InsertText(FunLocStart, CI);

    std::string CF = SynthesizeBlockFunc(Blocks[i], i, FunName, ImplTag);

    InsertText(FunLocStart, CF);

    if (ImportedBlockDecls.size()) {
      std::string HF = SynthesizeBlockHelperFuncs(Blocks[i], i, FunName, ImplTag);
      InsertText(FunLocStart, HF);
    }
    std::string BD = SynthesizeBlockDescriptor(DescTag, ImplTag, i, FunName,
                                               ImportedBlockDecls.size() > 0);
    InsertText(FunLocStart, BD);

    BlockDeclRefs.clear();
    BlockByRefDecls.clear();
    BlockByRefDeclsPtrSet.clear();
    BlockByCopyDecls.clear();
    BlockByCopyDeclsPtrSet.clear();
    ImportedBlockDecls.clear();
  }
  if (RewriteSC) {
    // Must insert any 'const/volatile/static here. Since it has been
    // removed as result of rewriting of block literals.
    std::string SC;
    if (GlobalVarDecl->getStorageClass() == SC_Static)
      SC = "static ";
    if (GlobalVarDecl->getType().isConstQualified())
      SC += "const ";
    if (GlobalVarDecl->getType().isVolatileQualified())
      SC += "volatile ";
    if (GlobalVarDecl->getType().isRestrictQualified())
      SC += "restrict ";
    InsertText(FunLocStart, SC);
  }

  Blocks.clear();
  InnerDeclRefsCount.clear();
  InnerDeclRefs.clear();
  RewrittenBlockExprs.clear();
}

void RewriteObjC::InsertBlockLiteralsWithinFunction(FunctionDecl *FD) {
  SourceLocation FunLocStart = FD->getTypeSpecStartLoc();
  StringRef FuncName = FD->getName();

  SynthesizeBlockLiterals(FunLocStart, FuncName);
}

static void BuildUniqueMethodName(std::string &Name,
                                  ObjCMethodDecl *MD) {
  ObjCInterfaceDecl *IFace = MD->getClassInterface();
  Name = std::string(IFace->getName());
  Name += "__" + MD->getSelector().getAsString();
  // Convert colons to underscores.
  std::string::size_type loc = 0;
  while ((loc = Name.find(':', loc)) != std::string::npos)
    Name.replace(loc, 1, "_");
}

void RewriteObjC::InsertBlockLiteralsWithinMethod(ObjCMethodDecl *MD) {
  // fprintf(stderr,"In InsertBlockLiteralsWitinMethod\n");
  // SourceLocation FunLocStart = MD->getBeginLoc();
  SourceLocation FunLocStart = MD->getBeginLoc();
  std::string FuncName;
  BuildUniqueMethodName(FuncName, MD);
  SynthesizeBlockLiterals(FunLocStart, FuncName);
}

void RewriteObjC::GetBlockDeclRefExprs(Stmt *S) {
  for (Stmt *SubStmt : S->children())
    if (SubStmt) {
      if (BlockExpr *CBE = dyn_cast<BlockExpr>(SubStmt))
        GetBlockDeclRefExprs(CBE->getBody());
      else
        GetBlockDeclRefExprs(SubStmt);
    }
  // Handle specific things.
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S))
    if (DRE->refersToEnclosingVariableOrCapture() ||
        HasLocalVariableExternalStorage(DRE->getDecl()))
      // FIXME: Handle enums.
      BlockDeclRefs.push_back(DRE);
}

void RewriteObjC::GetInnerBlockDeclRefExprs(Stmt *S,
                SmallVectorImpl<DeclRefExpr *> &InnerBlockDeclRefs,
                llvm::SmallPtrSetImpl<const DeclContext *> &InnerContexts) {
  for (Stmt *SubStmt : S->children())
    if (SubStmt) {
      if (BlockExpr *CBE = dyn_cast<BlockExpr>(SubStmt)) {
        InnerContexts.insert(cast<DeclContext>(CBE->getBlockDecl()));
        GetInnerBlockDeclRefExprs(CBE->getBody(),
                                  InnerBlockDeclRefs,
                                  InnerContexts);
      }
      else
        GetInnerBlockDeclRefExprs(SubStmt, InnerBlockDeclRefs, InnerContexts);
    }
  // Handle specific things.
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S)) {
    if (DRE->refersToEnclosingVariableOrCapture() ||
        HasLocalVariableExternalStorage(DRE->getDecl())) {
      if (!InnerContexts.count(DRE->getDecl()->getDeclContext()))
        InnerBlockDeclRefs.push_back(DRE);
      if (VarDecl *Var = cast<VarDecl>(DRE->getDecl()))
        if (Var->isFunctionOrMethodVarDecl())
          ImportedLocalExternalDecls.insert(Var);
    }
  }
}

/// convertFunctionTypeOfBlocks - This routine converts a function type
/// whose result type may be a block pointer or whose argument type(s)
/// might be block pointers to an equivalent function type replacing
/// all block pointers to function pointers.
QualType RewriteObjC::convertFunctionTypeOfBlocks(const FunctionType *FT) {
  const FunctionProtoType *FTP = dyn_cast<FunctionProtoType>(FT);
  // FTP will be null for closures that don't take arguments.
  // Generate a funky cast.
  SmallVector<QualType, 8> ArgTypes;
  QualType Res = FT->getReturnType();
  bool HasBlockType = convertBlockPointerToFunctionPointer(Res);

  if (FTP) {
    for (auto &I : FTP->param_types()) {
      QualType t = I;
      // Make sure we convert "t (^)(...)" to "t (*)(...)".
      if (convertBlockPointerToFunctionPointer(t))
        HasBlockType = true;
      ArgTypes.push_back(t);
    }
  }
  QualType FuncType;
  // FIXME. Does this work if block takes no argument but has a return type
  // which is of block type?
  if (HasBlockType)
    FuncType = getSimpleFunctionType(Res, ArgTypes);
  else FuncType = QualType(FT, 0);
  return FuncType;
}

Stmt *RewriteObjC::SynthesizeBlockCall(CallExpr *Exp, const Expr *BlockExp) {
  // Navigate to relevant type information.
  const BlockPointerType *CPT = nullptr;

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(BlockExp)) {
    CPT = DRE->getType()->getAs<BlockPointerType>();
  } else if (const MemberExpr *MExpr = dyn_cast<MemberExpr>(BlockExp)) {
    CPT = MExpr->getType()->getAs<BlockPointerType>();
  }
  else if (const ParenExpr *PRE = dyn_cast<ParenExpr>(BlockExp)) {
    return SynthesizeBlockCall(Exp, PRE->getSubExpr());
  }
  else if (const ImplicitCastExpr *IEXPR = dyn_cast<ImplicitCastExpr>(BlockExp))
    CPT = IEXPR->getType()->getAs<BlockPointerType>();
  else if (const ConditionalOperator *CEXPR =
            dyn_cast<ConditionalOperator>(BlockExp)) {
    Expr *LHSExp = CEXPR->getLHS();
    Stmt *LHSStmt = SynthesizeBlockCall(Exp, LHSExp);
    Expr *RHSExp = CEXPR->getRHS();
    Stmt *RHSStmt = SynthesizeBlockCall(Exp, RHSExp);
    Expr *CONDExp = CEXPR->getCond();
    ConditionalOperator *CondExpr = new (Context) ConditionalOperator(
        CONDExp, SourceLocation(), cast<Expr>(LHSStmt), SourceLocation(),
        cast<Expr>(RHSStmt), Exp->getType(), VK_PRValue, OK_Ordinary);
    return CondExpr;
  } else if (const ObjCIvarRefExpr *IRE = dyn_cast<ObjCIvarRefExpr>(BlockExp)) {
    CPT = IRE->getType()->getAs<BlockPointerType>();
  } else if (const PseudoObjectExpr *POE
               = dyn_cast<PseudoObjectExpr>(BlockExp)) {
    CPT = POE->getType()->castAs<BlockPointerType>();
  } else {
    assert(false && "RewriteBlockClass: Bad type");
  }
  assert(CPT && "RewriteBlockClass: Bad type");
  const FunctionType *FT = CPT->getPointeeType()->getAs<FunctionType>();
  assert(FT && "RewriteBlockClass: Bad type");
  const FunctionProtoType *FTP = dyn_cast<FunctionProtoType>(FT);
  // FTP will be null for closures that don't take arguments.

  RecordDecl *RD = RecordDecl::Create(*Context, TagTypeKind::Struct, TUDecl,
                                      SourceLocation(), SourceLocation(),
                                      &Context->Idents.get("__block_impl"));
  QualType PtrBlock = Context->getPointerType(Context->getTagDeclType(RD));

  // Generate a funky cast.
  SmallVector<QualType, 8> ArgTypes;

  // Push the block argument type.
  ArgTypes.push_back(PtrBlock);
  if (FTP) {
    for (auto &I : FTP->param_types()) {
      QualType t = I;
      // Make sure we convert "t (^)(...)" to "t (*)(...)".
      if (!convertBlockPointerToFunctionPointer(t))
        convertToUnqualifiedObjCType(t);
      ArgTypes.push_back(t);
    }
  }
  // Now do the pointer to function cast.
  QualType PtrToFuncCastType = getSimpleFunctionType(Exp->getType(), ArgTypes);

  PtrToFuncCastType = Context->getPointerType(PtrToFuncCastType);

  CastExpr *BlkCast = NoTypeInfoCStyleCastExpr(Context, PtrBlock,
                                               CK_BitCast,
                                               const_cast<Expr*>(BlockExp));
  // Don't forget the parens to enforce the proper binding.
  ParenExpr *PE = new (Context) ParenExpr(SourceLocation(), SourceLocation(),
                                          BlkCast);
  //PE->dump();

  FieldDecl *FD = FieldDecl::Create(*Context, nullptr, SourceLocation(),
                                    SourceLocation(),
                                    &Context->Idents.get("FuncPtr"),
                                    Context->VoidPtrTy, nullptr,
                                    /*BitWidth=*/nullptr, /*Mutable=*/true,
                                    ICIS_NoInit);
  MemberExpr *ME = MemberExpr::CreateImplicit(
      *Context, PE, true, FD, FD->getType(), VK_LValue, OK_Ordinary);

  CastExpr *FunkCast = NoTypeInfoCStyleCastExpr(Context, PtrToFuncCastType,
                                                CK_BitCast, ME);
  PE = new (Context) ParenExpr(SourceLocation(), SourceLocation(), FunkCast);

  SmallVector<Expr*, 8> BlkExprs;
  // Add the implicit argument.
  BlkExprs.push_back(BlkCast);
  // Add the user arguments.
  for (CallExpr::arg_iterator I = Exp->arg_begin(),
       E = Exp->arg_end(); I != E; ++I) {
    BlkExprs.push_back(*I);
  }
  CallExpr *CE =
      CallExpr::Create(*Context, PE, BlkExprs, Exp->getType(), VK_PRValue,
                       SourceLocation(), FPOptionsOverride());
  return CE;
}

// We need to return the rewritten expression to handle cases where the
// BlockDeclRefExpr is embedded in another expression being rewritten.
// For example:
//
// int main() {
//    __block Foo *f;
//    __block int i;
//
//    void (^myblock)() = ^() {
//        [f test]; // f is a BlockDeclRefExpr embedded in a message (which is being rewritten).
//        i = 77;
//    };
//}
Stmt *RewriteObjC::RewriteBlockDeclRefExpr(DeclRefExpr *DeclRefExp) {
  // Rewrite the byref variable into BYREFVAR->__forwarding->BYREFVAR
  // for each DeclRefExp where BYREFVAR is name of the variable.
  ValueDecl *VD = DeclRefExp->getDecl();
  bool isArrow = DeclRefExp->refersToEnclosingVariableOrCapture() ||
                 HasLocalVariableExternalStorage(DeclRefExp->getDecl());

  FieldDecl *FD = FieldDecl::Create(*Context, nullptr, SourceLocation(),
                                    SourceLocation(),
                                    &Context->Idents.get("__forwarding"),
                                    Context->VoidPtrTy, nullptr,
                                    /*BitWidth=*/nullptr, /*Mutable=*/true,
                                    ICIS_NoInit);
  MemberExpr *ME =
      MemberExpr::CreateImplicit(*Context, DeclRefExp, isArrow, FD,
                                 FD->getType(), VK_LValue, OK_Ordinary);

  StringRef Name = VD->getName();
  FD = FieldDecl::Create(*Context, nullptr, SourceLocation(), SourceLocation(),
                         &Context->Idents.get(Name),
                         Context->VoidPtrTy, nullptr,
                         /*BitWidth=*/nullptr, /*Mutable=*/true,
                         ICIS_NoInit);
  ME = MemberExpr::CreateImplicit(*Context, ME, true, FD, DeclRefExp->getType(),
                                  VK_LValue, OK_Ordinary);

  // Need parens to enforce precedence.
  ParenExpr *PE = new (Context) ParenExpr(DeclRefExp->getExprLoc(),
                                          DeclRefExp->getExprLoc(),
                                          ME);
  ReplaceStmt(DeclRefExp, PE);
  return PE;
}

// Rewrites the imported local variable V with external storage
// (static, extern, etc.) as *V
//
Stmt *RewriteObjC::RewriteLocalVariableExternalStorage(DeclRefExpr *DRE) {
  ValueDecl *VD = DRE->getDecl();
  if (VarDecl *Var = dyn_cast<VarDecl>(VD))
    if (!ImportedLocalExternalDecls.count(Var))
      return DRE;
  Expr *Exp = UnaryOperator::Create(
      const_cast<ASTContext &>(*Context), DRE, UO_Deref, DRE->getType(),
      VK_LValue, OK_Ordinary, DRE->getLocation(), false, FPOptionsOverride());
  // Need parens to enforce precedence.
  ParenExpr *PE = new (Context) ParenExpr(SourceLocation(), SourceLocation(),
                                          Exp);
  ReplaceStmt(DRE, PE);
  return PE;
}

void RewriteObjC::RewriteCastExpr(CStyleCastExpr *CE) {
  SourceLocation LocStart = CE->getLParenLoc();
  SourceLocation LocEnd = CE->getRParenLoc();

  // Need to avoid trying to rewrite synthesized casts.
  if (LocStart.isInvalid())
    return;
  // Need to avoid trying to rewrite casts contained in macros.
  if (!Rewriter::isRewritable(LocStart) || !Rewriter::isRewritable(LocEnd))
    return;

  const char *startBuf = SM->getCharacterData(LocStart);
  const char *endBuf = SM->getCharacterData(LocEnd);
  QualType QT = CE->getType();
  const Type* TypePtr = QT->getAs<Type>();
  if (isa<TypeOfExprType>(TypePtr)) {
    const TypeOfExprType *TypeOfExprTypePtr = cast<TypeOfExprType>(TypePtr);
    QT = TypeOfExprTypePtr->getUnderlyingExpr()->getType();
    std::string TypeAsString = "(";
    RewriteBlockPointerType(TypeAsString, QT);
    TypeAsString += ")";
    ReplaceText(LocStart, endBuf-startBuf+1, TypeAsString);
    return;
  }
  // advance the location to startArgList.
  const char *argPtr = startBuf;

  while (*argPtr++ && (argPtr < endBuf)) {
    switch (*argPtr) {
    case '^':
      // Replace the '^' with '*'.
      LocStart = LocStart.getLocWithOffset(argPtr-startBuf);
      ReplaceText(LocStart, 1, "*");
      break;
    }
  }
}

void RewriteObjC::RewriteBlockPointerFunctionArgs(FunctionDecl *FD) {
  SourceLocation DeclLoc = FD->getLocation();
  unsigned parenCount = 0;

  // We have 1 or more arguments that have closure pointers.
  const char *startBuf = SM->getCharacterData(DeclLoc);
  const char *startArgList = strchr(startBuf, '(');

  assert((*startArgList == '(') && "Rewriter fuzzy parser confused");

  parenCount++;
  // advance the location to startArgList.
  DeclLoc = DeclLoc.getLocWithOffset(startArgList-startBuf);
  assert((DeclLoc.isValid()) && "Invalid DeclLoc");

  const char *argPtr = startArgList;

  while (*argPtr++ && parenCount) {
    switch (*argPtr) {
    case '^':
      // Replace the '^' with '*'.
      DeclLoc = DeclLoc.getLocWithOffset(argPtr-startArgList);
      ReplaceText(DeclLoc, 1, "*");
      break;
    case '(':
      parenCount++;
      break;
    case ')':
      parenCount--;
      break;
    }
  }
}

bool RewriteObjC::PointerTypeTakesAnyBlockArguments(QualType QT) {
  const FunctionProtoType *FTP;
  const PointerType *PT = QT->getAs<PointerType>();
  if (PT) {
    FTP = PT->getPointeeType()->getAs<FunctionProtoType>();
  } else {
    const BlockPointerType *BPT = QT->getAs<BlockPointerType>();
    assert(BPT && "BlockPointerTypeTakeAnyBlockArguments(): not a block pointer type");
    FTP = BPT->getPointeeType()->getAs<FunctionProtoType>();
  }
  if (FTP) {
    for (const auto &I : FTP->param_types())
      if (isTopLevelBlockPointerType(I))
        return true;
  }
  return false;
}

bool RewriteObjC::PointerTypeTakesAnyObjCQualifiedType(QualType QT) {
  const FunctionProtoType *FTP;
  const PointerType *PT = QT->getAs<PointerType>();
  if (PT) {
    FTP = PT->getPointeeType()->getAs<FunctionProtoType>();
  } else {
    const BlockPointerType *BPT = QT->getAs<BlockPointerType>();
    assert(BPT && "BlockPointerTypeTakeAnyBlockArguments(): not a block pointer type");
    FTP = BPT->getPointeeType()->getAs<FunctionProtoType>();
  }
  if (FTP) {
    for (const auto &I : FTP->param_types()) {
      if (I->isObjCQualifiedIdType())
        return true;
      if (I->isObjCObjectPointerType() &&
          I->getPointeeType()->isObjCQualifiedInterfaceType())
        return true;
    }

  }
  return false;
}

void RewriteObjC::GetExtentOfArgList(const char *Name, const char *&LParen,
                                     const char *&RParen) {
  const char *argPtr = strchr(Name, '(');
  assert((*argPtr == '(') && "Rewriter fuzzy parser confused");

  LParen = argPtr; // output the start.
  argPtr++; // skip past the left paren.
  unsigned parenCount = 1;

  while (*argPtr && parenCount) {
    switch (*argPtr) {
    case '(': parenCount++; break;
    case ')': parenCount--; break;
    default: break;
    }
    if (parenCount) argPtr++;
  }
  assert((*argPtr == ')') && "Rewriter fuzzy parser confused");
  RParen = argPtr; // output the end
}

void RewriteObjC::RewriteBlockPointerDecl(NamedDecl *ND) {
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(ND)) {
    RewriteBlockPointerFunctionArgs(FD);
    return;
  }
  // Handle Variables and Typedefs.
  SourceLocation DeclLoc = ND->getLocation();
  QualType DeclT;
  if (VarDecl *VD = dyn_cast<VarDecl>(ND))
    DeclT = VD->getType();
  else if (TypedefNameDecl *TDD = dyn_cast<TypedefNameDecl>(ND))
    DeclT = TDD->getUnderlyingType();
  else if (FieldDecl *FD = dyn_cast<FieldDecl>(ND))
    DeclT = FD->getType();
  else
    llvm_unreachable("RewriteBlockPointerDecl(): Decl type not yet handled");

  const char *startBuf = SM->getCharacterData(DeclLoc);
  const char *endBuf = startBuf;
  // scan backward (from the decl location) for the end of the previous decl.
  while (*startBuf != '^' && *startBuf != ';' && startBuf != MainFileStart)
    startBuf--;
  SourceLocation Start = DeclLoc.getLocWithOffset(startBuf-endBuf);
  std::string buf;
  unsigned OrigLength=0;
  // *startBuf != '^' if we are dealing with a pointer to function that
  // may take block argument types (which will be handled below).
  if (*startBuf == '^') {
    // Replace the '^' with '*', computing a negative offset.
    buf = '*';
    startBuf++;
    OrigLength++;
  }
  while (*startBuf != ')') {
    buf += *startBuf;
    startBuf++;
    OrigLength++;
  }
  buf += ')';
  OrigLength++;

  if (PointerTypeTakesAnyBlockArguments(DeclT) ||
      PointerTypeTakesAnyObjCQualifiedType(DeclT)) {
    // Replace the '^' with '*' for arguments.
    // Replace id<P> with id/*<>*/
    DeclLoc = ND->getLocation();
    startBuf = SM->getCharacterData(DeclLoc);
    const char *argListBegin, *argListEnd;
    GetExtentOfArgList(startBuf, argListBegin, argListEnd);
    while (argListBegin < argListEnd) {
      if (*argListBegin == '^')
        buf += '*';
      else if (*argListBegin ==  '<') {
        buf += "/*";
        buf += *argListBegin++;
        OrigLength++;
        while (*argListBegin != '>') {
          buf += *argListBegin++;
          OrigLength++;
        }
        buf += *argListBegin;
        buf += "*/";
      }
      else
        buf += *argListBegin;
      argListBegin++;
      OrigLength++;
    }
    buf += ')';
    OrigLength++;
  }
  ReplaceText(Start, OrigLength, buf);
}

/// SynthesizeByrefCopyDestroyHelper - This routine synthesizes:
/// void __Block_byref_id_object_copy(struct Block_byref_id_object *dst,
///                    struct Block_byref_id_object *src) {
///  _Block_object_assign (&_dest->object, _src->object,
///                        BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT
///                        [|BLOCK_FIELD_IS_WEAK]) // object
///  _Block_object_assign(&_dest->object, _src->object,
///                       BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK
///                       [|BLOCK_FIELD_IS_WEAK]) // block
/// }
/// And:
/// void __Block_byref_id_object_dispose(struct Block_byref_id_object *_src) {
///  _Block_object_dispose(_src->object,
///                        BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT
///                        [|BLOCK_FIELD_IS_WEAK]) // object
///  _Block_object_dispose(_src->object,
///                         BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK
///                         [|BLOCK_FIELD_IS_WEAK]) // block
/// }

std::string RewriteObjC::SynthesizeByrefCopyDestroyHelper(VarDecl *VD,
                                                          int flag) {
  std::string S;
  if (CopyDestroyCache.count(flag))
    return S;
  CopyDestroyCache.insert(flag);
  S = "static void __Block_byref_id_object_copy_";
  S += utostr(flag);
  S += "(void *dst, void *src) {\n";

  // offset into the object pointer is computed as:
  // void * + void* + int + int + void* + void *
  unsigned IntSize =
  static_cast<unsigned>(Context->getTypeSize(Context->IntTy));
  unsigned VoidPtrSize =
  static_cast<unsigned>(Context->getTypeSize(Context->VoidPtrTy));

  unsigned offset = (VoidPtrSize*4 + IntSize + IntSize)/Context->getCharWidth();
  S += " _Block_object_assign((char*)dst + ";
  S += utostr(offset);
  S += ", *(void * *) ((char*)src + ";
  S += utostr(offset);
  S += "), ";
  S += utostr(flag);
  S += ");\n}\n";

  S += "static void __Block_byref_id_object_dispose_";
  S += utostr(flag);
  S += "(void *src) {\n";
  S += " _Block_object_dispose(*(void * *) ((char*)src + ";
  S += utostr(offset);
  S += "), ";
  S += utostr(flag);
  S += ");\n}\n";
  return S;
}

/// RewriteByRefVar - For each __block typex ND variable this routine transforms
/// the declaration into:
/// struct __Block_byref_ND {
/// void *__isa;                  // NULL for everything except __weak pointers
/// struct __Block_byref_ND *__forwarding;
/// int32_t __flags;
/// int32_t __size;
/// void *__Block_byref_id_object_copy; // If variable is __block ObjC object
/// void *__Block_byref_id_object_dispose; // If variable is __block ObjC object
/// typex ND;
/// };
///
/// It then replaces declaration of ND variable with:
/// struct __Block_byref_ND ND = {__isa=0B, __forwarding=&ND, __flags=some_flag,
///                               __size=sizeof(struct __Block_byref_ND),
///                               ND=initializer-if-any};
///
///
void RewriteObjC::RewriteByRefVar(VarDecl *ND) {
  // Insert declaration for the function in which block literal is
  // used.
  if (CurFunctionDeclToDeclareForBlock)
    RewriteBlockLiteralFunctionDecl(CurFunctionDeclToDeclareForBlock);
  int flag = 0;
  int isa = 0;
  SourceLocation DeclLoc = ND->getTypeSpecStartLoc();
  if (DeclLoc.isInvalid())
    // If type location is missing, it is because of missing type (a warning).
    // Use variable's location which is good for this case.
    DeclLoc = ND->getLocation();
  const char *startBuf = SM->getCharacterData(DeclLoc);
  SourceLocation X = ND->getEndLoc();
  X = SM->getExpansionLoc(X);
  const char *endBuf = SM->getCharacterData(X);
  std::string Name(ND->getNameAsString());
  std::string ByrefType;
  RewriteByRefString(ByrefType, Name, ND, true);
  ByrefType += " {\n";
  ByrefType += "  void *__isa;\n";
  RewriteByRefString(ByrefType, Name, ND);
  ByrefType += " *__forwarding;\n";
  ByrefType += " int __flags;\n";
  ByrefType += " int __size;\n";
  // Add void *__Block_byref_id_object_copy;
  // void *__Block_byref_id_object_dispose; if needed.
  QualType Ty = ND->getType();
  bool HasCopyAndDispose = Context->BlockRequiresCopying(Ty, ND);
  if (HasCopyAndDispose) {
    ByrefType += " void (*__Block_byref_id_object_copy)(void*, void*);\n";
    ByrefType += " void (*__Block_byref_id_object_dispose)(void*);\n";
  }

  QualType T = Ty;
  (void)convertBlockPointerToFunctionPointer(T);
  T.getAsStringInternal(Name, Context->getPrintingPolicy());

  ByrefType += " " + Name + ";\n";
  ByrefType += "};\n";
  // Insert this type in global scope. It is needed by helper function.
  SourceLocation FunLocStart;
  if (CurFunctionDef)
     FunLocStart = CurFunctionDef->getTypeSpecStartLoc();
  else {
    assert(CurMethodDef && "RewriteByRefVar - CurMethodDef is null");
    FunLocStart = CurMethodDef->getBeginLoc();
  }
  InsertText(FunLocStart, ByrefType);
  if (Ty.isObjCGCWeak()) {
    flag |= BLOCK_FIELD_IS_WEAK;
    isa = 1;
  }

  if (HasCopyAndDispose) {
    flag = BLOCK_BYREF_CALLER;
    QualType Ty = ND->getType();
    // FIXME. Handle __weak variable (BLOCK_FIELD_IS_WEAK) as well.
    if (Ty->isBlockPointerType())
      flag |= BLOCK_FIELD_IS_BLOCK;
    else
      flag |= BLOCK_FIELD_IS_OBJECT;
    std::string HF = SynthesizeByrefCopyDestroyHelper(ND, flag);
    if (!HF.empty())
      InsertText(FunLocStart, HF);
  }

  // struct __Block_byref_ND ND =
  // {0, &ND, some_flag, __size=sizeof(struct __Block_byref_ND),
  //  initializer-if-any};
  bool hasInit = (ND->getInit() != nullptr);
  unsigned flags = 0;
  if (HasCopyAndDispose)
    flags |= BLOCK_HAS_COPY_DISPOSE;
  Name = ND->getNameAsString();
  ByrefType.clear();
  RewriteByRefString(ByrefType, Name, ND);
  std::string ForwardingCastType("(");
  ForwardingCastType += ByrefType + " *)";
  if (!hasInit) {
    ByrefType += " " + Name + " = {(void*)";
    ByrefType += utostr(isa);
    ByrefType += "," +  ForwardingCastType + "&" + Name + ", ";
    ByrefType += utostr(flags);
    ByrefType += ", ";
    ByrefType += "sizeof(";
    RewriteByRefString(ByrefType, Name, ND);
    ByrefType += ")";
    if (HasCopyAndDispose) {
      ByrefType += ", __Block_byref_id_object_copy_";
      ByrefType += utostr(flag);
      ByrefType += ", __Block_byref_id_object_dispose_";
      ByrefType += utostr(flag);
    }
    ByrefType += "};\n";
    unsigned nameSize = Name.size();
    // for block or function pointer declaration. Name is already
    // part of the declaration.
    if (Ty->isBlockPointerType() || Ty->isFunctionPointerType())
      nameSize = 1;
    ReplaceText(DeclLoc, endBuf-startBuf+nameSize, ByrefType);
  }
  else {
    SourceLocation startLoc;
    Expr *E = ND->getInit();
    if (const CStyleCastExpr *ECE = dyn_cast<CStyleCastExpr>(E))
      startLoc = ECE->getLParenLoc();
    else
      startLoc = E->getBeginLoc();
    startLoc = SM->getExpansionLoc(startLoc);
    endBuf = SM->getCharacterData(startLoc);
    ByrefType += " " + Name;
    ByrefType += " = {(void*)";
    ByrefType += utostr(isa);
    ByrefType += "," +  ForwardingCastType + "&" + Name + ", ";
    ByrefType += utostr(flags);
    ByrefType += ", ";
    ByrefType += "sizeof(";
    RewriteByRefString(ByrefType, Name, ND);
    ByrefType += "), ";
    if (HasCopyAndDispose) {
      ByrefType += "__Block_byref_id_object_copy_";
      ByrefType += utostr(flag);
      ByrefType += ", __Block_byref_id_object_dispose_";
      ByrefType += utostr(flag);
      ByrefType += ", ";
    }
    ReplaceText(DeclLoc, endBuf-startBuf, ByrefType);

    // Complete the newly synthesized compound expression by inserting a right
    // curly brace before the end of the declaration.
    // FIXME: This approach avoids rewriting the initializer expression. It
    // also assumes there is only one declarator. For example, the following
    // isn't currently supported by this routine (in general):
    //
    // double __block BYREFVAR = 1.34, BYREFVAR2 = 1.37;
    //
    const char *startInitializerBuf = SM->getCharacterData(startLoc);
    const char *semiBuf = strchr(startInitializerBuf, ';');
    assert((*semiBuf == ';') && "RewriteByRefVar: can't find ';'");
    SourceLocation semiLoc =
      startLoc.getLocWithOffset(semiBuf-startInitializerBuf);

    InsertText(semiLoc, "}");
  }
}

void RewriteObjC::CollectBlockDeclRefInfo(BlockExpr *Exp) {
  // Add initializers for any closure decl refs.
  GetBlockDeclRefExprs(Exp->getBody());
  if (BlockDeclRefs.size()) {
    // Unique all "by copy" declarations.
    for (unsigned i = 0; i < BlockDeclRefs.size(); i++)
      if (!BlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>()) {
        if (!BlockByCopyDeclsPtrSet.count(BlockDeclRefs[i]->getDecl())) {
          BlockByCopyDeclsPtrSet.insert(BlockDeclRefs[i]->getDecl());
          BlockByCopyDecls.push_back(BlockDeclRefs[i]->getDecl());
        }
      }
    // Unique all "by ref" declarations.
    for (unsigned i = 0; i < BlockDeclRefs.size(); i++)
      if (BlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>()) {
        if (!BlockByRefDeclsPtrSet.count(BlockDeclRefs[i]->getDecl())) {
          BlockByRefDeclsPtrSet.insert(BlockDeclRefs[i]->getDecl());
          BlockByRefDecls.push_back(BlockDeclRefs[i]->getDecl());
        }
      }
    // Find any imported blocks...they will need special attention.
    for (unsigned i = 0; i < BlockDeclRefs.size(); i++)
      if (BlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>() ||
          BlockDeclRefs[i]->getType()->isObjCObjectPointerType() ||
          BlockDeclRefs[i]->getType()->isBlockPointerType())
        ImportedBlockDecls.insert(BlockDeclRefs[i]->getDecl());
  }
}

FunctionDecl *RewriteObjC::SynthBlockInitFunctionDecl(StringRef name) {
  IdentifierInfo *ID = &Context->Idents.get(name);
  QualType FType = Context->getFunctionNoProtoType(Context->VoidPtrTy);
  return FunctionDecl::Create(*Context, TUDecl, SourceLocation(),
                              SourceLocation(), ID, FType, nullptr, SC_Extern,
                              false, false);
}

Stmt *RewriteObjC::SynthBlockInitExpr(BlockExpr *Exp,
                     const SmallVectorImpl<DeclRefExpr *> &InnerBlockDeclRefs) {
  const BlockDecl *block = Exp->getBlockDecl();
  Blocks.push_back(Exp);

  CollectBlockDeclRefInfo(Exp);

  // Add inner imported variables now used in current block.
 int countOfInnerDecls = 0;
  if (!InnerBlockDeclRefs.empty()) {
    for (unsigned i = 0; i < InnerBlockDeclRefs.size(); i++) {
      DeclRefExpr *Exp = InnerBlockDeclRefs[i];
      ValueDecl *VD = Exp->getDecl();
      if (!VD->hasAttr<BlocksAttr>() && !BlockByCopyDeclsPtrSet.count(VD)) {
      // We need to save the copied-in variables in nested
      // blocks because it is needed at the end for some of the API generations.
      // See SynthesizeBlockLiterals routine.
        InnerDeclRefs.push_back(Exp); countOfInnerDecls++;
        BlockDeclRefs.push_back(Exp);
        BlockByCopyDeclsPtrSet.insert(VD);
        BlockByCopyDecls.push_back(VD);
      }
      if (VD->hasAttr<BlocksAttr>() && !BlockByRefDeclsPtrSet.count(VD)) {
        InnerDeclRefs.push_back(Exp); countOfInnerDecls++;
        BlockDeclRefs.push_back(Exp);
        BlockByRefDeclsPtrSet.insert(VD);
        BlockByRefDecls.push_back(VD);
      }
    }
    // Find any imported blocks...they will need special attention.
    for (unsigned i = 0; i < InnerBlockDeclRefs.size(); i++)
      if (InnerBlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>() ||
          InnerBlockDeclRefs[i]->getType()->isObjCObjectPointerType() ||
          InnerBlockDeclRefs[i]->getType()->isBlockPointerType())
        ImportedBlockDecls.insert(InnerBlockDeclRefs[i]->getDecl());
  }
  InnerDeclRefsCount.push_back(countOfInnerDecls);

  std::string FuncName;

  if (CurFunctionDef)
    FuncName = CurFunctionDef->getNameAsString();
  else if (CurMethodDef)
    BuildUniqueMethodName(FuncName, CurMethodDef);
  else if (GlobalVarDecl)
    FuncName = std::string(GlobalVarDecl->getNameAsString());

  std::string BlockNumber = utostr(Blocks.size()-1);

  std::string Tag = "__" + FuncName + "_block_impl_" + BlockNumber;
  std::string Func = "__" + FuncName + "_block_func_" + BlockNumber;

  // Get a pointer to the function type so we can cast appropriately.
  QualType BFT = convertFunctionTypeOfBlocks(Exp->getFunctionType());
  QualType FType = Context->getPointerType(BFT);

  FunctionDecl *FD;
  Expr *NewRep;

  // Simulate a constructor call...
  FD = SynthBlockInitFunctionDecl(Tag);
  DeclRefExpr *DRE = new (Context)
      DeclRefExpr(*Context, FD, false, FType, VK_PRValue, SourceLocation());

  SmallVector<Expr*, 4> InitExprs;

  // Initialize the block function.
  FD = SynthBlockInitFunctionDecl(Func);
  DeclRefExpr *Arg = new (Context) DeclRefExpr(
      *Context, FD, false, FD->getType(), VK_LValue, SourceLocation());
  CastExpr *castExpr =
      NoTypeInfoCStyleCastExpr(Context, Context->VoidPtrTy, CK_BitCast, Arg);
  InitExprs.push_back(castExpr);

  // Initialize the block descriptor.
  std::string DescData = "__" + FuncName + "_block_desc_" + BlockNumber + "_DATA";

  VarDecl *NewVD = VarDecl::Create(
      *Context, TUDecl, SourceLocation(), SourceLocation(),
      &Context->Idents.get(DescData), Context->VoidPtrTy, nullptr, SC_Static);
  UnaryOperator *DescRefExpr = UnaryOperator::Create(
      const_cast<ASTContext &>(*Context),
      new (Context) DeclRefExpr(*Context, NewVD, false, Context->VoidPtrTy,
                                VK_LValue, SourceLocation()),
      UO_AddrOf, Context->getPointerType(Context->VoidPtrTy), VK_PRValue,
      OK_Ordinary, SourceLocation(), false, FPOptionsOverride());
  InitExprs.push_back(DescRefExpr);

  // Add initializers for any closure decl refs.
  if (BlockDeclRefs.size()) {
    Expr *Exp;
    // Output all "by copy" declarations.
    for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByCopyDecls.begin(),
         E = BlockByCopyDecls.end(); I != E; ++I) {
      if (isObjCType((*I)->getType())) {
        // FIXME: Conform to ABI ([[obj retain] autorelease]).
        FD = SynthBlockInitFunctionDecl((*I)->getName());
        Exp = new (Context) DeclRefExpr(*Context, FD, false, FD->getType(),
                                        VK_LValue, SourceLocation());
        if (HasLocalVariableExternalStorage(*I)) {
          QualType QT = (*I)->getType();
          QT = Context->getPointerType(QT);
          Exp = UnaryOperator::Create(const_cast<ASTContext &>(*Context), Exp,
                                      UO_AddrOf, QT, VK_PRValue, OK_Ordinary,
                                      SourceLocation(), false,
                                      FPOptionsOverride());
        }
      } else if (isTopLevelBlockPointerType((*I)->getType())) {
        FD = SynthBlockInitFunctionDecl((*I)->getName());
        Arg = new (Context) DeclRefExpr(*Context, FD, false, FD->getType(),
                                        VK_LValue, SourceLocation());
        Exp = NoTypeInfoCStyleCastExpr(Context, Context->VoidPtrTy, CK_BitCast,
                                       Arg);
      } else {
        FD = SynthBlockInitFunctionDecl((*I)->getName());
        Exp = new (Context) DeclRefExpr(*Context, FD, false, FD->getType(),
                                        VK_LValue, SourceLocation());
        if (HasLocalVariableExternalStorage(*I)) {
          QualType QT = (*I)->getType();
          QT = Context->getPointerType(QT);
          Exp = UnaryOperator::Create(const_cast<ASTContext &>(*Context), Exp,
                                      UO_AddrOf, QT, VK_PRValue, OK_Ordinary,
                                      SourceLocation(), false,
                                      FPOptionsOverride());
        }
      }
      InitExprs.push_back(Exp);
    }
    // Output all "by ref" declarations.
    for (SmallVectorImpl<ValueDecl *>::iterator I = BlockByRefDecls.begin(),
         E = BlockByRefDecls.end(); I != E; ++I) {
      ValueDecl *ND = (*I);
      std::string Name(ND->getNameAsString());
      std::string RecName;
      RewriteByRefString(RecName, Name, ND, true);
      IdentifierInfo *II = &Context->Idents.get(RecName.c_str()
                                                + sizeof("struct"));
      RecordDecl *RD =
          RecordDecl::Create(*Context, TagTypeKind::Struct, TUDecl,
                             SourceLocation(), SourceLocation(), II);
      assert(RD && "SynthBlockInitExpr(): Can't find RecordDecl");
      QualType castT = Context->getPointerType(Context->getTagDeclType(RD));

      FD = SynthBlockInitFunctionDecl((*I)->getName());
      Exp = new (Context) DeclRefExpr(*Context, FD, false, FD->getType(),
                                      VK_LValue, SourceLocation());
      bool isNestedCapturedVar = false;
      if (block)
        for (const auto &CI : block->captures()) {
          const VarDecl *variable = CI.getVariable();
          if (variable == ND && CI.isNested()) {
            assert (CI.isByRef() &&
                    "SynthBlockInitExpr - captured block variable is not byref");
            isNestedCapturedVar = true;
            break;
          }
        }
      // captured nested byref variable has its address passed. Do not take
      // its address again.
      if (!isNestedCapturedVar)
        Exp = UnaryOperator::Create(
            const_cast<ASTContext &>(*Context), Exp, UO_AddrOf,
            Context->getPointerType(Exp->getType()), VK_PRValue, OK_Ordinary,
            SourceLocation(), false, FPOptionsOverride());
      Exp = NoTypeInfoCStyleCastExpr(Context, castT, CK_BitCast, Exp);
      InitExprs.push_back(Exp);
    }
  }
  if (ImportedBlockDecls.size()) {
    // generate BLOCK_HAS_COPY_DISPOSE(have helper funcs) | BLOCK_HAS_DESCRIPTOR
    int flag = (BLOCK_HAS_COPY_DISPOSE | BLOCK_HAS_DESCRIPTOR);
    unsigned IntSize =
      static_cast<unsigned>(Context->getTypeSize(Context->IntTy));
    Expr *FlagExp = IntegerLiteral::Create(*Context, llvm::APInt(IntSize, flag),
                                           Context->IntTy, SourceLocation());
    InitExprs.push_back(FlagExp);
  }
  NewRep = CallExpr::Create(*Context, DRE, InitExprs, FType, VK_LValue,
                            SourceLocation(), FPOptionsOverride());
  NewRep = UnaryOperator::Create(
      const_cast<ASTContext &>(*Context), NewRep, UO_AddrOf,
      Context->getPointerType(NewRep->getType()), VK_PRValue, OK_Ordinary,
      SourceLocation(), false, FPOptionsOverride());
  NewRep = NoTypeInfoCStyleCastExpr(Context, FType, CK_BitCast,
                                    NewRep);
  BlockDeclRefs.clear();
  BlockByRefDecls.clear();
  BlockByRefDeclsPtrSet.clear();
  BlockByCopyDecls.clear();
  BlockByCopyDeclsPtrSet.clear();
  ImportedBlockDecls.clear();
  return NewRep;
}

bool RewriteObjC::IsDeclStmtInForeachHeader(DeclStmt *DS) {
  if (const ObjCForCollectionStmt * CS =
      dyn_cast<ObjCForCollectionStmt>(Stmts.back()))
        return CS->getElement() == DS;
  return false;
}

//===----------------------------------------------------------------------===//
// Function Body / Expression rewriting
//===----------------------------------------------------------------------===//

Stmt *RewriteObjC::RewriteFunctionBodyOrGlobalInitializer(Stmt *S) {
  if (isa<SwitchStmt>(S) || isa<WhileStmt>(S) ||
      isa<DoStmt>(S) || isa<ForStmt>(S))
    Stmts.push_back(S);
  else if (isa<ObjCForCollectionStmt>(S)) {
    Stmts.push_back(S);
    ObjCBcLabelNo.push_back(++BcLabelCount);
  }

  // Pseudo-object operations and ivar references need special
  // treatment because we're going to recursively rewrite them.
  if (PseudoObjectExpr *PseudoOp = dyn_cast<PseudoObjectExpr>(S)) {
    if (isa<BinaryOperator>(PseudoOp->getSyntacticForm())) {
      return RewritePropertyOrImplicitSetter(PseudoOp);
    } else {
      return RewritePropertyOrImplicitGetter(PseudoOp);
    }
  } else if (ObjCIvarRefExpr *IvarRefExpr = dyn_cast<ObjCIvarRefExpr>(S)) {
    return RewriteObjCIvarRefExpr(IvarRefExpr);
  }

  SourceRange OrigStmtRange = S->getSourceRange();

  // Perform a bottom up rewrite of all children.
  for (Stmt *&childStmt : S->children())
    if (childStmt) {
      Stmt *newStmt = RewriteFunctionBodyOrGlobalInitializer(childStmt);
      if (newStmt) {
        childStmt = newStmt;
      }
    }

  if (BlockExpr *BE = dyn_cast<BlockExpr>(S)) {
    SmallVector<DeclRefExpr *, 8> InnerBlockDeclRefs;
    llvm::SmallPtrSet<const DeclContext *, 8> InnerContexts;
    InnerContexts.insert(BE->getBlockDecl());
    ImportedLocalExternalDecls.clear();
    GetInnerBlockDeclRefExprs(BE->getBody(),
                              InnerBlockDeclRefs, InnerContexts);
    // Rewrite the block body in place.
    Stmt *SaveCurrentBody = CurrentBody;
    CurrentBody = BE->getBody();
    PropParentMap = nullptr;
    // block literal on rhs of a property-dot-sytax assignment
    // must be replaced by its synthesize ast so getRewrittenText
    // works as expected. In this case, what actually ends up on RHS
    // is the blockTranscribed which is the helper function for the
    // block literal; as in: self.c = ^() {[ace ARR];};
    bool saveDisableReplaceStmt = DisableReplaceStmt;
    DisableReplaceStmt = false;
    RewriteFunctionBodyOrGlobalInitializer(BE->getBody());
    DisableReplaceStmt = saveDisableReplaceStmt;
    CurrentBody = SaveCurrentBody;
    PropParentMap = nullptr;
    ImportedLocalExternalDecls.clear();
    // Now we snarf the rewritten text and stash it away for later use.
    std::string Str = Rewrite.getRewrittenText(BE->getSourceRange());
    RewrittenBlockExprs[BE] = Str;

    Stmt *blockTranscribed = SynthBlockInitExpr(BE, InnerBlockDeclRefs);

    //blockTranscribed->dump();
    ReplaceStmt(S, blockTranscribed);
    return blockTranscribed;
  }
  // Handle specific things.
  if (ObjCEncodeExpr *AtEncode = dyn_cast<ObjCEncodeExpr>(S))
    return RewriteAtEncode(AtEncode);

  if (ObjCSelectorExpr *AtSelector = dyn_cast<ObjCSelectorExpr>(S))
    return RewriteAtSelector(AtSelector);

  if (ObjCStringLiteral *AtString = dyn_cast<ObjCStringLiteral>(S))
    return RewriteObjCStringLiteral(AtString);

  if (ObjCMessageExpr *MessExpr = dyn_cast<ObjCMessageExpr>(S)) {
#if 0
    // Before we rewrite it, put the original message expression in a comment.
    SourceLocation startLoc = MessExpr->getBeginLoc();
    SourceLocation endLoc = MessExpr->getEndLoc();

    const char *startBuf = SM->getCharacterData(startLoc);
    const char *endBuf = SM->getCharacterData(endLoc);

    std::string messString;
    messString += "// ";
    messString.append(startBuf, endBuf-startBuf+1);
    messString += "\n";

    // FIXME: Missing definition of
    // InsertText(clang::SourceLocation, char const*, unsigned int).
    // InsertText(startLoc, messString);
    // Tried this, but it didn't work either...
    // ReplaceText(startLoc, 0, messString.c_str(), messString.size());
#endif
    return RewriteMessageExpr(MessExpr);
  }

  if (ObjCAtTryStmt *StmtTry = dyn_cast<ObjCAtTryStmt>(S))
    return RewriteObjCTryStmt(StmtTry);

  if (ObjCAtSynchronizedStmt *StmtTry = dyn_cast<ObjCAtSynchronizedStmt>(S))
    return RewriteObjCSynchronizedStmt(StmtTry);

  if (ObjCAtThrowStmt *StmtThrow = dyn_cast<ObjCAtThrowStmt>(S))
    return RewriteObjCThrowStmt(StmtThrow);

  if (ObjCProtocolExpr *ProtocolExp = dyn_cast<ObjCProtocolExpr>(S))
    return RewriteObjCProtocolExpr(ProtocolExp);

  if (ObjCForCollectionStmt *StmtForCollection =
        dyn_cast<ObjCForCollectionStmt>(S))
    return RewriteObjCForCollectionStmt(StmtForCollection,
                                        OrigStmtRange.getEnd());
  if (BreakStmt *StmtBreakStmt =
      dyn_cast<BreakStmt>(S))
    return RewriteBreakStmt(StmtBreakStmt);
  if (ContinueStmt *StmtContinueStmt =
      dyn_cast<ContinueStmt>(S))
    return RewriteContinueStmt(StmtContinueStmt);

  // Need to check for protocol refs (id <P>, Foo <P> *) in variable decls
  // and cast exprs.
  if (DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    // FIXME: What we're doing here is modifying the type-specifier that
    // precedes the first Decl.  In the future the DeclGroup should have
    // a separate type-specifier that we can rewrite.
    // NOTE: We need to avoid rewriting the DeclStmt if it is within
    // the context of an ObjCForCollectionStmt. For example:
    //   NSArray *someArray;
    //   for (id <FooProtocol> index in someArray) ;
    // This is because RewriteObjCForCollectionStmt() does textual rewriting
    // and it depends on the original text locations/positions.
    if (Stmts.empty() || !IsDeclStmtInForeachHeader(DS))
      RewriteObjCQualifiedInterfaceTypes(*DS->decl_begin());

    // Blocks rewrite rules.
    for (auto *SD : DS->decls()) {
      if (ValueDecl *ND = dyn_cast<ValueDecl>(SD)) {
        if (isTopLevelBlockPointerType(ND->getType()))
          RewriteBlockPointerDecl(ND);
        else if (ND->getType()->isFunctionPointerType())
          CheckFunctionPointerDecl(ND->getType(), ND);
        if (VarDecl *VD = dyn_cast<VarDecl>(SD)) {
          if (VD->hasAttr<BlocksAttr>()) {
            static unsigned uniqueByrefDeclCount = 0;
            assert(!BlockByRefDeclNo.count(ND) &&
              "RewriteFunctionBodyOrGlobalInitializer: Duplicate byref decl");
            BlockByRefDeclNo[ND] = uniqueByrefDeclCount++;
            RewriteByRefVar(VD);
          }
          else
            RewriteTypeOfDecl(VD);
        }
      }
      if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(SD)) {
        if (isTopLevelBlockPointerType(TD->getUnderlyingType()))
          RewriteBlockPointerDecl(TD);
        else if (TD->getUnderlyingType()->isFunctionPointerType())
          CheckFunctionPointerDecl(TD->getUnderlyingType(), TD);
      }
    }
  }

  if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(S))
    RewriteObjCQualifiedInterfaceTypes(CE);

  if (isa<SwitchStmt>(S) || isa<WhileStmt>(S) ||
      isa<DoStmt>(S) || isa<ForStmt>(S)) {
    assert(!Stmts.empty() && "Statement stack is empty");
    assert ((isa<SwitchStmt>(Stmts.back()) || isa<WhileStmt>(Stmts.back()) ||
             isa<DoStmt>(Stmts.back()) || isa<ForStmt>(Stmts.back()))
            && "Statement stack mismatch");
    Stmts.pop_back();
  }
  // Handle blocks rewriting.
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S)) {
    ValueDecl *VD = DRE->getDecl();
    if (VD->hasAttr<BlocksAttr>())
      return RewriteBlockDeclRefExpr(DRE);
    if (HasLocalVariableExternalStorage(VD))
      return RewriteLocalVariableExternalStorage(DRE);
  }

  if (CallExpr *CE = dyn_cast<CallExpr>(S)) {
    if (CE->getCallee()->getType()->isBlockPointerType()) {
      Stmt *BlockCall = SynthesizeBlockCall(CE, CE->getCallee());
      ReplaceStmt(S, BlockCall);
      return BlockCall;
    }
  }
  if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(S)) {
    RewriteCastExpr(CE);
  }
#if 0
  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(S)) {
    CastExpr *Replacement = new (Context) CastExpr(ICE->getType(),
                                                   ICE->getSubExpr(),
                                                   SourceLocation());
    // Get the new text.
    std::string SStr;
    llvm::raw_string_ostream Buf(SStr);
    Replacement->printPretty(Buf);
    const std::string &Str = Buf.str();

    printf("CAST = %s\n", &Str[0]);
    InsertText(ICE->getSubExpr()->getBeginLoc(), Str);
    delete S;
    return Replacement;
  }
#endif
  // Return this stmt unmodified.
  return S;
}

void RewriteObjC::RewriteRecordBody(RecordDecl *RD) {
  for (auto *FD : RD->fields()) {
    if (isTopLevelBlockPointerType(FD->getType()))
      RewriteBlockPointerDecl(FD);
    if (FD->getType()->isObjCQualifiedIdType() ||
        FD->getType()->isObjCQualifiedInterfaceType())
      RewriteObjCQualifiedInterfaceTypes(FD);
  }
}

/// HandleDeclInMainFile - This is called for each top-level decl defined in the
/// main file of the input.
void RewriteObjC::HandleDeclInMainFile(Decl *D) {
  switch (D->getKind()) {
    case Decl::Function: {
      FunctionDecl *FD = cast<FunctionDecl>(D);
      if (FD->isOverloadedOperator())
        return;

      // Since function prototypes don't have ParmDecl's, we check the function
      // prototype. This enables us to rewrite function declarations and
      // definitions using the same code.
      RewriteBlocksInFunctionProtoType(FD->getType(), FD);

      if (!FD->isThisDeclarationADefinition())
        break;

      // FIXME: If this should support Obj-C++, support CXXTryStmt
      if (CompoundStmt *Body = dyn_cast_or_null<CompoundStmt>(FD->getBody())) {
        CurFunctionDef = FD;
        CurFunctionDeclToDeclareForBlock = FD;
        CurrentBody = Body;
        Body =
        cast_or_null<CompoundStmt>(RewriteFunctionBodyOrGlobalInitializer(Body));
        FD->setBody(Body);
        CurrentBody = nullptr;
        if (PropParentMap) {
          delete PropParentMap;
          PropParentMap = nullptr;
        }
        // This synthesizes and inserts the block "impl" struct, invoke function,
        // and any copy/dispose helper functions.
        InsertBlockLiteralsWithinFunction(FD);
        CurFunctionDef = nullptr;
        CurFunctionDeclToDeclareForBlock = nullptr;
      }
      break;
    }
    case Decl::ObjCMethod: {
      ObjCMethodDecl *MD = cast<ObjCMethodDecl>(D);
      if (CompoundStmt *Body = MD->getCompoundBody()) {
        CurMethodDef = MD;
        CurrentBody = Body;
        Body =
          cast_or_null<CompoundStmt>(RewriteFunctionBodyOrGlobalInitializer(Body));
        MD->setBody(Body);
        CurrentBody = nullptr;
        if (PropParentMap) {
          delete PropParentMap;
          PropParentMap = nullptr;
        }
        InsertBlockLiteralsWithinMethod(MD);
        CurMethodDef = nullptr;
      }
      break;
    }
    case Decl::ObjCImplementation: {
      ObjCImplementationDecl *CI = cast<ObjCImplementationDecl>(D);
      ClassImplementation.push_back(CI);
      break;
    }
    case Decl::ObjCCategoryImpl: {
      ObjCCategoryImplDecl *CI = cast<ObjCCategoryImplDecl>(D);
      CategoryImplementation.push_back(CI);
      break;
    }
    case Decl::Var: {
      VarDecl *VD = cast<VarDecl>(D);
      RewriteObjCQualifiedInterfaceTypes(VD);
      if (isTopLevelBlockPointerType(VD->getType()))
        RewriteBlockPointerDecl(VD);
      else if (VD->getType()->isFunctionPointerType()) {
        CheckFunctionPointerDecl(VD->getType(), VD);
        if (VD->getInit()) {
          if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(VD->getInit())) {
            RewriteCastExpr(CE);
          }
        }
      } else if (VD->getType()->isRecordType()) {
        RecordDecl *RD = VD->getType()->castAs<RecordType>()->getDecl();
        if (RD->isCompleteDefinition())
          RewriteRecordBody(RD);
      }
      if (VD->getInit()) {
        GlobalVarDecl = VD;
        CurrentBody = VD->getInit();
        RewriteFunctionBodyOrGlobalInitializer(VD->getInit());
        CurrentBody = nullptr;
        if (PropParentMap) {
          delete PropParentMap;
          PropParentMap = nullptr;
        }
        SynthesizeBlockLiterals(VD->getTypeSpecStartLoc(), VD->getName());
        GlobalVarDecl = nullptr;

        // This is needed for blocks.
        if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(VD->getInit())) {
            RewriteCastExpr(CE);
        }
      }
      break;
    }
    case Decl::TypeAlias:
    case Decl::Typedef: {
      if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(D)) {
        if (isTopLevelBlockPointerType(TD->getUnderlyingType()))
          RewriteBlockPointerDecl(TD);
        else if (TD->getUnderlyingType()->isFunctionPointerType())
          CheckFunctionPointerDecl(TD->getUnderlyingType(), TD);
      }
      break;
    }
    case Decl::CXXRecord:
    case Decl::Record: {
      RecordDecl *RD = cast<RecordDecl>(D);
      if (RD->isCompleteDefinition())
        RewriteRecordBody(RD);
      break;
    }
    default:
      break;
  }
  // Nothing yet.
}

void RewriteObjC::HandleTranslationUnit(ASTContext &C) {
  if (Diags.hasErrorOccurred())
    return;

  RewriteInclude();

  // Here's a great place to add any extra declarations that may be needed.
  // Write out meta data for each @protocol(<expr>).
  for (ObjCProtocolDecl *ProtDecl : ProtocolExprDecls)
    RewriteObjCProtocolMetaData(ProtDecl, "", "", Preamble);

  InsertText(SM->getLocForStartOfFile(MainFileID), Preamble, false);
  if (ClassImplementation.size() || CategoryImplementation.size())
    RewriteImplementations();

  // Get the buffer corresponding to MainFileID.  If we haven't changed it, then
  // we are done.
  if (const RewriteBuffer *RewriteBuf =
      Rewrite.getRewriteBufferFor(MainFileID)) {
    //printf("Changed:\n");
    *OutFile << std::string(RewriteBuf->begin(), RewriteBuf->end());
  } else {
    llvm::errs() << "No changes\n";
  }

  if (ClassImplementation.size() || CategoryImplementation.size() ||
      ProtocolExprDecls.size()) {
    // Rewrite Objective-c meta data*
    std::string ResultStr;
    RewriteMetaDataIntoBuffer(ResultStr);
    // Emit metadata.
    *OutFile << ResultStr;
  }
  OutFile->flush();
}

void RewriteObjCFragileABI::Initialize(ASTContext &context) {
  InitializeCommon(context);

  // declaring objc_selector outside the parameter list removes a silly
  // scope related warning...
  if (IsHeader)
    Preamble = "#pragma once\n";
  Preamble += "struct objc_selector; struct objc_class;\n";
  Preamble += "struct __rw_objc_super { struct objc_object *object; ";
  Preamble += "struct objc_object *superClass; ";
  if (LangOpts.MicrosoftExt) {
    // Add a constructor for creating temporary objects.
    Preamble += "__rw_objc_super(struct objc_object *o, struct objc_object *s) "
    ": ";
    Preamble += "object(o), superClass(s) {} ";
  }
  Preamble += "};\n";
  Preamble += "#ifndef _REWRITER_typedef_Protocol\n";
  Preamble += "typedef struct objc_object Protocol;\n";
  Preamble += "#define _REWRITER_typedef_Protocol\n";
  Preamble += "#endif\n";
  if (LangOpts.MicrosoftExt) {
    Preamble += "#define __OBJC_RW_DLLIMPORT extern \"C\" __declspec(dllimport)\n";
    Preamble += "#define __OBJC_RW_STATICIMPORT extern \"C\"\n";
  } else
    Preamble += "#define __OBJC_RW_DLLIMPORT extern\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_object *objc_msgSend";
  Preamble += "(struct objc_object *, struct objc_selector *, ...);\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_object *objc_msgSendSuper";
  Preamble += "(struct objc_super *, struct objc_selector *, ...);\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_object* objc_msgSend_stret";
  Preamble += "(struct objc_object *, struct objc_selector *, ...);\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_object* objc_msgSendSuper_stret";
  Preamble += "(struct objc_super *, struct objc_selector *, ...);\n";
  Preamble += "__OBJC_RW_DLLIMPORT double objc_msgSend_fpret";
  Preamble += "(struct objc_object *, struct objc_selector *, ...);\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_object *objc_getClass";
  Preamble += "(const char *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_class *class_getSuperclass";
  Preamble += "(struct objc_class *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_object *objc_getMetaClass";
  Preamble += "(const char *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT void objc_exception_throw(struct objc_object *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT void objc_exception_try_enter(void *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT void objc_exception_try_exit(void *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT struct objc_object *objc_exception_extract(void *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT int objc_exception_match";
  Preamble += "(struct objc_class *, struct objc_object *);\n";
  // @synchronized hooks.
  Preamble += "__OBJC_RW_DLLIMPORT int objc_sync_enter(struct objc_object *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT int objc_sync_exit(struct objc_object *);\n";
  Preamble += "__OBJC_RW_DLLIMPORT Protocol *objc_getProtocol(const char *);\n";
  Preamble += "#ifndef __FASTENUMERATIONSTATE\n";
  Preamble += "struct __objcFastEnumerationState {\n\t";
  Preamble += "unsigned long state;\n\t";
  Preamble += "void **itemsPtr;\n\t";
  Preamble += "unsigned long *mutationsPtr;\n\t";
  Preamble += "unsigned long extra[5];\n};\n";
  Preamble += "__OBJC_RW_DLLIMPORT void objc_enumerationMutation(struct objc_object *);\n";
  Preamble += "#define __FASTENUMERATIONSTATE\n";
  Preamble += "#endif\n";
  Preamble += "#ifndef __NSCONSTANTSTRINGIMPL\n";
  Preamble += "struct __NSConstantStringImpl {\n";
  Preamble += "  int *isa;\n";
  Preamble += "  int flags;\n";
  Preamble += "  char *str;\n";
  Preamble += "  long length;\n";
  Preamble += "};\n";
  Preamble += "#ifdef CF_EXPORT_CONSTANT_STRING\n";
  Preamble += "extern \"C\" __declspec(dllexport) int __CFConstantStringClassReference[];\n";
  Preamble += "#else\n";
  Preamble += "__OBJC_RW_DLLIMPORT int __CFConstantStringClassReference[];\n";
  Preamble += "#endif\n";
  Preamble += "#define __NSCONSTANTSTRINGIMPL\n";
  Preamble += "#endif\n";
  // Blocks preamble.
  Preamble += "#ifndef BLOCK_IMPL\n";
  Preamble += "#define BLOCK_IMPL\n";
  Preamble += "struct __block_impl {\n";
  Preamble += "  void *isa;\n";
  Preamble += "  int Flags;\n";
  Preamble += "  int Reserved;\n";
  Preamble += "  void *FuncPtr;\n";
  Preamble += "};\n";
  Preamble += "// Runtime copy/destroy helper functions (from Block_private.h)\n";
  Preamble += "#ifdef __OBJC_EXPORT_BLOCKS\n";
  Preamble += "extern \"C\" __declspec(dllexport) "
  "void _Block_object_assign(void *, const void *, const int);\n";
  Preamble += "extern \"C\" __declspec(dllexport) void _Block_object_dispose(const void *, const int);\n";
  Preamble += "extern \"C\" __declspec(dllexport) void *_NSConcreteGlobalBlock[32];\n";
  Preamble += "extern \"C\" __declspec(dllexport) void *_NSConcreteStackBlock[32];\n";
  Preamble += "#else\n";
  Preamble += "__OBJC_RW_DLLIMPORT void _Block_object_assign(void *, const void *, const int);\n";
  Preamble += "__OBJC_RW_DLLIMPORT void _Block_object_dispose(const void *, const int);\n";
  Preamble += "__OBJC_RW_DLLIMPORT void *_NSConcreteGlobalBlock[32];\n";
  Preamble += "__OBJC_RW_DLLIMPORT void *_NSConcreteStackBlock[32];\n";
  Preamble += "#endif\n";
  Preamble += "#endif\n";
  if (LangOpts.MicrosoftExt) {
    Preamble += "#undef __OBJC_RW_DLLIMPORT\n";
    Preamble += "#undef __OBJC_RW_STATICIMPORT\n";
    Preamble += "#ifndef KEEP_ATTRIBUTES\n";  // We use this for clang tests.
    Preamble += "#define __attribute__(X)\n";
    Preamble += "#endif\n";
    Preamble += "#define __weak\n";
  }
  else {
    Preamble += "#define __block\n";
    Preamble += "#define __weak\n";
  }
  // NOTE! Windows uses LLP64 for 64bit mode. So, cast pointer to long long
  // as this avoids warning in any 64bit/32bit compilation model.
  Preamble += "\n#define __OFFSETOFIVAR__(TYPE, MEMBER) ((long long) &((TYPE *)0)->MEMBER)\n";
}

/// RewriteIvarOffsetComputation - This routine synthesizes computation of
/// ivar offset.
void RewriteObjCFragileABI::RewriteIvarOffsetComputation(ObjCIvarDecl *ivar,
                                                         std::string &Result) {
  if (ivar->isBitField()) {
    // FIXME: The hack below doesn't work for bitfields. For now, we simply
    // place all bitfields at offset 0.
    Result += "0";
  } else {
    Result += "__OFFSETOFIVAR__(struct ";
    Result += ivar->getContainingInterface()->getNameAsString();
    if (LangOpts.MicrosoftExt)
      Result += "_IMPL";
    Result += ", ";
    Result += ivar->getNameAsString();
    Result += ")";
  }
}

/// RewriteObjCProtocolMetaData - Rewrite protocols meta-data.
void RewriteObjCFragileABI::RewriteObjCProtocolMetaData(
                            ObjCProtocolDecl *PDecl, StringRef prefix,
                            StringRef ClassName, std::string &Result) {
  static bool objc_protocol_methods = false;

  // Output struct protocol_methods holder of method selector and type.
  if (!objc_protocol_methods && PDecl->hasDefinition()) {
    /* struct protocol_methods {
     SEL _cmd;
     char *method_types;
     }
     */
    Result += "\nstruct _protocol_methods {\n";
    Result += "\tstruct objc_selector *_cmd;\n";
    Result += "\tchar *method_types;\n";
    Result += "};\n";

    objc_protocol_methods = true;
  }
  // Do not synthesize the protocol more than once.
  if (ObjCSynthesizedProtocols.count(PDecl->getCanonicalDecl()))
    return;

  if (ObjCProtocolDecl *Def = PDecl->getDefinition())
    PDecl = Def;

  if (PDecl->instmeth_begin() != PDecl->instmeth_end()) {
    unsigned NumMethods = std::distance(PDecl->instmeth_begin(),
                                        PDecl->instmeth_end());
    /* struct _objc_protocol_method_list {
     int protocol_method_count;
     struct protocol_methods protocols[];
     }
     */
    Result += "\nstatic struct {\n";
    Result += "\tint protocol_method_count;\n";
    Result += "\tstruct _protocol_methods protocol_methods[";
    Result += utostr(NumMethods);
    Result += "];\n} _OBJC_PROTOCOL_INSTANCE_METHODS_";
    Result += PDecl->getNameAsString();
    Result += " __attribute__ ((used, section (\"__OBJC, __cat_inst_meth\")))= "
    "{\n\t" + utostr(NumMethods) + "\n";

    // Output instance methods declared in this protocol.
    for (ObjCProtocolDecl::instmeth_iterator
         I = PDecl->instmeth_begin(), E = PDecl->instmeth_end();
         I != E; ++I) {
      if (I == PDecl->instmeth_begin())
        Result += "\t  ,{{(struct objc_selector *)\"";
      else
        Result += "\t  ,{(struct objc_selector *)\"";
      Result += (*I)->getSelector().getAsString();
      std::string MethodTypeString = Context->getObjCEncodingForMethodDecl(*I);
      Result += "\", \"";
      Result += MethodTypeString;
      Result += "\"}\n";
    }
    Result += "\t }\n};\n";
  }

  // Output class methods declared in this protocol.
  unsigned NumMethods = std::distance(PDecl->classmeth_begin(),
                                      PDecl->classmeth_end());
  if (NumMethods > 0) {
    /* struct _objc_protocol_method_list {
     int protocol_method_count;
     struct protocol_methods protocols[];
     }
     */
    Result += "\nstatic struct {\n";
    Result += "\tint protocol_method_count;\n";
    Result += "\tstruct _protocol_methods protocol_methods[";
    Result += utostr(NumMethods);
    Result += "];\n} _OBJC_PROTOCOL_CLASS_METHODS_";
    Result += PDecl->getNameAsString();
    Result += " __attribute__ ((used, section (\"__OBJC, __cat_cls_meth\")))= "
    "{\n\t";
    Result += utostr(NumMethods);
    Result += "\n";

    // Output instance methods declared in this protocol.
    for (ObjCProtocolDecl::classmeth_iterator
         I = PDecl->classmeth_begin(), E = PDecl->classmeth_end();
         I != E; ++I) {
      if (I == PDecl->classmeth_begin())
        Result += "\t  ,{{(struct objc_selector *)\"";
      else
        Result += "\t  ,{(struct objc_selector *)\"";
      Result += (*I)->getSelector().getAsString();
      std::string MethodTypeString = Context->getObjCEncodingForMethodDecl(*I);
      Result += "\", \"";
      Result += MethodTypeString;
      Result += "\"}\n";
    }
    Result += "\t }\n};\n";
  }

  // Output:
  /* struct _objc_protocol {
   // Objective-C 1.0 extensions
   struct _objc_protocol_extension *isa;
   char *protocol_name;
   struct _objc_protocol **protocol_list;
   struct _objc_protocol_method_list *instance_methods;
   struct _objc_protocol_method_list *class_methods;
   };
   */
  static bool objc_protocol = false;
  if (!objc_protocol) {
    Result += "\nstruct _objc_protocol {\n";
    Result += "\tstruct _objc_protocol_extension *isa;\n";
    Result += "\tchar *protocol_name;\n";
    Result += "\tstruct _objc_protocol **protocol_list;\n";
    Result += "\tstruct _objc_protocol_method_list *instance_methods;\n";
    Result += "\tstruct _objc_protocol_method_list *class_methods;\n";
    Result += "};\n";

    objc_protocol = true;
  }

  Result += "\nstatic struct _objc_protocol _OBJC_PROTOCOL_";
  Result += PDecl->getNameAsString();
  Result += " __attribute__ ((used, section (\"__OBJC, __protocol\")))= "
  "{\n\t0, \"";
  Result += PDecl->getNameAsString();
  Result += "\", 0, ";
  if (PDecl->instmeth_begin() != PDecl->instmeth_end()) {
    Result += "(struct _objc_protocol_method_list *)&_OBJC_PROTOCOL_INSTANCE_METHODS_";
    Result += PDecl->getNameAsString();
    Result += ", ";
  }
  else
    Result += "0, ";
  if (PDecl->classmeth_begin() != PDecl->classmeth_end()) {
    Result += "(struct _objc_protocol_method_list *)&_OBJC_PROTOCOL_CLASS_METHODS_";
    Result += PDecl->getNameAsString();
    Result += "\n";
  }
  else
    Result += "0\n";
  Result += "};\n";

  // Mark this protocol as having been generated.
  if (!ObjCSynthesizedProtocols.insert(PDecl->getCanonicalDecl()).second)
    llvm_unreachable("protocol already synthesized");
}

void RewriteObjCFragileABI::RewriteObjCProtocolListMetaData(
                                const ObjCList<ObjCProtocolDecl> &Protocols,
                                StringRef prefix, StringRef ClassName,
                                std::string &Result) {
  if (Protocols.empty()) return;

  for (unsigned i = 0; i != Protocols.size(); i++)
    RewriteObjCProtocolMetaData(Protocols[i], prefix, ClassName, Result);

  // Output the top lovel protocol meta-data for the class.
  /* struct _objc_protocol_list {
   struct _objc_protocol_list *next;
   int    protocol_count;
   struct _objc_protocol *class_protocols[];
   }
   */
  Result += "\nstatic struct {\n";
  Result += "\tstruct _objc_protocol_list *next;\n";
  Result += "\tint    protocol_count;\n";
  Result += "\tstruct _objc_protocol *class_protocols[";
  Result += utostr(Protocols.size());
  Result += "];\n} _OBJC_";
  Result += prefix;
  Result += "_PROTOCOLS_";
  Result += ClassName;
  Result += " __attribute__ ((used, section (\"__OBJC, __cat_cls_meth\")))= "
  "{\n\t0, ";
  Result += utostr(Protocols.size());
  Result += "\n";

  Result += "\t,{&_OBJC_PROTOCOL_";
  Result += Protocols[0]->getNameAsString();
  Result += " \n";

  for (unsigned i = 1; i != Protocols.size(); i++) {
    Result += "\t ,&_OBJC_PROTOCOL_";
    Result += Protocols[i]->getNameAsString();
    Result += "\n";
  }
  Result += "\t }\n};\n";
}

void RewriteObjCFragileABI::RewriteObjCClassMetaData(ObjCImplementationDecl *IDecl,
                                           std::string &Result) {
  ObjCInterfaceDecl *CDecl = IDecl->getClassInterface();

  // Explicitly declared @interface's are already synthesized.
  if (CDecl->isImplicitInterfaceDecl()) {
    // FIXME: Implementation of a class with no @interface (legacy) does not
    // produce correct synthesis as yet.
    RewriteObjCInternalStruct(CDecl, Result);
  }

  // Build _objc_ivar_list metadata for classes ivars if needed
  unsigned NumIvars =
      !IDecl->ivar_empty() ? IDecl->ivar_size() : CDecl->ivar_size();
  if (NumIvars > 0) {
    static bool objc_ivar = false;
    if (!objc_ivar) {
      /* struct _objc_ivar {
       char *ivar_name;
       char *ivar_type;
       int ivar_offset;
       };
       */
      Result += "\nstruct _objc_ivar {\n";
      Result += "\tchar *ivar_name;\n";
      Result += "\tchar *ivar_type;\n";
      Result += "\tint ivar_offset;\n";
      Result += "};\n";

      objc_ivar = true;
    }

    /* struct {
     int ivar_count;
     struct _objc_ivar ivar_list[nIvars];
     };
     */
    Result += "\nstatic struct {\n";
    Result += "\tint ivar_count;\n";
    Result += "\tstruct _objc_ivar ivar_list[";
    Result += utostr(NumIvars);
    Result += "];\n} _OBJC_INSTANCE_VARIABLES_";
    Result += IDecl->getNameAsString();
    Result += " __attribute__ ((used, section (\"__OBJC, __instance_vars\")))= "
    "{\n\t";
    Result += utostr(NumIvars);
    Result += "\n";

    ObjCInterfaceDecl::ivar_iterator IVI, IVE;
    SmallVector<ObjCIvarDecl *, 8> IVars;
    if (!IDecl->ivar_empty()) {
      for (auto *IV : IDecl->ivars())
        IVars.push_back(IV);
      IVI = IDecl->ivar_begin();
      IVE = IDecl->ivar_end();
    } else {
      IVI = CDecl->ivar_begin();
      IVE = CDecl->ivar_end();
    }
    Result += "\t,{{\"";
    Result += IVI->getNameAsString();
    Result += "\", \"";
    std::string TmpString, StrEncoding;
    Context->getObjCEncodingForType(IVI->getType(), TmpString, *IVI);
    QuoteDoublequotes(TmpString, StrEncoding);
    Result += StrEncoding;
    Result += "\", ";
    RewriteIvarOffsetComputation(*IVI, Result);
    Result += "}\n";
    for (++IVI; IVI != IVE; ++IVI) {
      Result += "\t  ,{\"";
      Result += IVI->getNameAsString();
      Result += "\", \"";
      std::string TmpString, StrEncoding;
      Context->getObjCEncodingForType(IVI->getType(), TmpString, *IVI);
      QuoteDoublequotes(TmpString, StrEncoding);
      Result += StrEncoding;
      Result += "\", ";
      RewriteIvarOffsetComputation(*IVI, Result);
      Result += "}\n";
    }

    Result += "\t }\n};\n";
  }

  // Build _objc_method_list for class's instance methods if needed
  SmallVector<ObjCMethodDecl *, 32> InstanceMethods(IDecl->instance_methods());

  // If any of our property implementations have associated getters or
  // setters, produce metadata for them as well.
  for (const auto *Prop : IDecl->property_impls()) {
    if (Prop->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic)
      continue;
    if (!Prop->getPropertyIvarDecl())
      continue;
    ObjCPropertyDecl *PD = Prop->getPropertyDecl();
    if (!PD)
      continue;
    if (ObjCMethodDecl *Getter = Prop->getGetterMethodDecl())
      if (!Getter->isDefined())
        InstanceMethods.push_back(Getter);
    if (PD->isReadOnly())
      continue;
    if (ObjCMethodDecl *Setter = Prop->getSetterMethodDecl())
      if (!Setter->isDefined())
        InstanceMethods.push_back(Setter);
  }
  RewriteObjCMethodsMetaData(InstanceMethods.begin(), InstanceMethods.end(),
                             true, "", IDecl->getName(), Result);

  // Build _objc_method_list for class's class methods if needed
  RewriteObjCMethodsMetaData(IDecl->classmeth_begin(), IDecl->classmeth_end(),
                             false, "", IDecl->getName(), Result);

  // Protocols referenced in class declaration?
  RewriteObjCProtocolListMetaData(CDecl->getReferencedProtocols(),
                                  "CLASS", CDecl->getName(), Result);

  // Declaration of class/meta-class metadata
  /* struct _objc_class {
   struct _objc_class *isa; // or const char *root_class_name when metadata
   const char *super_class_name;
   char *name;
   long version;
   long info;
   long instance_size;
   struct _objc_ivar_list *ivars;
   struct _objc_method_list *methods;
   struct objc_cache *cache;
   struct objc_protocol_list *protocols;
   const char *ivar_layout;
   struct _objc_class_ext  *ext;
   };
   */
  static bool objc_class = false;
  if (!objc_class) {
    Result += "\nstruct _objc_class {\n";
    Result += "\tstruct _objc_class *isa;\n";
    Result += "\tconst char *super_class_name;\n";
    Result += "\tchar *name;\n";
    Result += "\tlong version;\n";
    Result += "\tlong info;\n";
    Result += "\tlong instance_size;\n";
    Result += "\tstruct _objc_ivar_list *ivars;\n";
    Result += "\tstruct _objc_method_list *methods;\n";
    Result += "\tstruct objc_cache *cache;\n";
    Result += "\tstruct _objc_protocol_list *protocols;\n";
    Result += "\tconst char *ivar_layout;\n";
    Result += "\tstruct _objc_class_ext  *ext;\n";
    Result += "};\n";
    objc_class = true;
  }

  // Meta-class metadata generation.
  ObjCInterfaceDecl *RootClass = nullptr;
  ObjCInterfaceDecl *SuperClass = CDecl->getSuperClass();
  while (SuperClass) {
    RootClass = SuperClass;
    SuperClass = SuperClass->getSuperClass();
  }
  SuperClass = CDecl->getSuperClass();

  Result += "\nstatic struct _objc_class _OBJC_METACLASS_";
  Result += CDecl->getNameAsString();
  Result += " __attribute__ ((used, section (\"__OBJC, __meta_class\")))= "
  "{\n\t(struct _objc_class *)\"";
  Result += (RootClass ? RootClass->getNameAsString() : CDecl->getNameAsString());
  Result += "\"";

  if (SuperClass) {
    Result += ", \"";
    Result += SuperClass->getNameAsString();
    Result += "\", \"";
    Result += CDecl->getNameAsString();
    Result += "\"";
  }
  else {
    Result += ", 0, \"";
    Result += CDecl->getNameAsString();
    Result += "\"";
  }
  // Set 'ivars' field for root class to 0. ObjC1 runtime does not use it.
  // 'info' field is initialized to CLS_META(2) for metaclass
  Result += ", 0,2, sizeof(struct _objc_class), 0";
  if (IDecl->classmeth_begin() != IDecl->classmeth_end()) {
    Result += "\n\t, (struct _objc_method_list *)&_OBJC_CLASS_METHODS_";
    Result += IDecl->getNameAsString();
    Result += "\n";
  }
  else
    Result += ", 0\n";
  if (CDecl->protocol_begin() != CDecl->protocol_end()) {
    Result += "\t,0, (struct _objc_protocol_list *)&_OBJC_CLASS_PROTOCOLS_";
    Result += CDecl->getNameAsString();
    Result += ",0,0\n";
  }
  else
    Result += "\t,0,0,0,0\n";
  Result += "};\n";

  // class metadata generation.
  Result += "\nstatic struct _objc_class _OBJC_CLASS_";
  Result += CDecl->getNameAsString();
  Result += " __attribute__ ((used, section (\"__OBJC, __class\")))= "
  "{\n\t&_OBJC_METACLASS_";
  Result += CDecl->getNameAsString();
  if (SuperClass) {
    Result += ", \"";
    Result += SuperClass->getNameAsString();
    Result += "\", \"";
    Result += CDecl->getNameAsString();
    Result += "\"";
  }
  else {
    Result += ", 0, \"";
    Result += CDecl->getNameAsString();
    Result += "\"";
  }
  // 'info' field is initialized to CLS_CLASS(1) for class
  Result += ", 0,1";
  if (!ObjCSynthesizedStructs.count(CDecl))
    Result += ",0";
  else {
    // class has size. Must synthesize its size.
    Result += ",sizeof(struct ";
    Result += CDecl->getNameAsString();
    if (LangOpts.MicrosoftExt)
      Result += "_IMPL";
    Result += ")";
  }
  if (NumIvars > 0) {
    Result += ", (struct _objc_ivar_list *)&_OBJC_INSTANCE_VARIABLES_";
    Result += CDecl->getNameAsString();
    Result += "\n\t";
  }
  else
    Result += ",0";
  if (IDecl->instmeth_begin() != IDecl->instmeth_end()) {
    Result += ", (struct _objc_method_list *)&_OBJC_INSTANCE_METHODS_";
    Result += CDecl->getNameAsString();
    Result += ", 0\n\t";
  }
  else
    Result += ",0,0";
  if (CDecl->protocol_begin() != CDecl->protocol_end()) {
    Result += ", (struct _objc_protocol_list*)&_OBJC_CLASS_PROTOCOLS_";
    Result += CDecl->getNameAsString();
    Result += ", 0,0\n";
  }
  else
    Result += ",0,0,0\n";
  Result += "};\n";
}

void RewriteObjCFragileABI::RewriteMetaDataIntoBuffer(std::string &Result) {
  int ClsDefCount = ClassImplementation.size();
  int CatDefCount = CategoryImplementation.size();

  // For each implemented class, write out all its meta data.
  for (int i = 0; i < ClsDefCount; i++)
    RewriteObjCClassMetaData(ClassImplementation[i], Result);

  // For each implemented category, write out all its meta data.
  for (int i = 0; i < CatDefCount; i++)
    RewriteObjCCategoryImplDecl(CategoryImplementation[i], Result);

  // Write objc_symtab metadata
  /*
   struct _objc_symtab
   {
   long sel_ref_cnt;
   SEL *refs;
   short cls_def_cnt;
   short cat_def_cnt;
   void *defs[cls_def_cnt + cat_def_cnt];
   };
   */

  Result += "\nstruct _objc_symtab {\n";
  Result += "\tlong sel_ref_cnt;\n";
  Result += "\tSEL *refs;\n";
  Result += "\tshort cls_def_cnt;\n";
  Result += "\tshort cat_def_cnt;\n";
  Result += "\tvoid *defs[" + utostr(ClsDefCount + CatDefCount)+ "];\n";
  Result += "};\n\n";

  Result += "static struct _objc_symtab "
  "_OBJC_SYMBOLS __attribute__((used, section (\"__OBJC, __symbols\")))= {\n";
  Result += "\t0, 0, " + utostr(ClsDefCount)
  + ", " + utostr(CatDefCount) + "\n";
  for (int i = 0; i < ClsDefCount; i++) {
    Result += "\t,&_OBJC_CLASS_";
    Result += ClassImplementation[i]->getNameAsString();
    Result += "\n";
  }

  for (int i = 0; i < CatDefCount; i++) {
    Result += "\t,&_OBJC_CATEGORY_";
    Result += CategoryImplementation[i]->getClassInterface()->getNameAsString();
    Result += "_";
    Result += CategoryImplementation[i]->getNameAsString();
    Result += "\n";
  }

  Result += "};\n\n";

  // Write objc_module metadata

  /*
   struct _objc_module {
   long version;
   long size;
   const char *name;
   struct _objc_symtab *symtab;
   }
   */

  Result += "\nstruct _objc_module {\n";
  Result += "\tlong version;\n";
  Result += "\tlong size;\n";
  Result += "\tconst char *name;\n";
  Result += "\tstruct _objc_symtab *symtab;\n";
  Result += "};\n\n";
  Result += "static struct _objc_module "
  "_OBJC_MODULES __attribute__ ((used, section (\"__OBJC, __module_info\")))= {\n";
  Result += "\t" + utostr(OBJC_ABI_VERSION) +
  ", sizeof(struct _objc_module), \"\", &_OBJC_SYMBOLS\n";
  Result += "};\n\n";

  if (LangOpts.MicrosoftExt) {
    if (ProtocolExprDecls.size()) {
      Result += "#pragma section(\".objc_protocol$B\",long,read,write)\n";
      Result += "#pragma data_seg(push, \".objc_protocol$B\")\n";
      for (ObjCProtocolDecl *ProtDecl : ProtocolExprDecls) {
        Result += "static struct _objc_protocol *_POINTER_OBJC_PROTOCOL_";
        Result += ProtDecl->getNameAsString();
        Result += " = &_OBJC_PROTOCOL_";
        Result += ProtDecl->getNameAsString();
        Result += ";\n";
      }
      Result += "#pragma data_seg(pop)\n\n";
    }
    Result += "#pragma section(\".objc_module_info$B\",long,read,write)\n";
    Result += "#pragma data_seg(push, \".objc_module_info$B\")\n";
    Result += "static struct _objc_module *_POINTER_OBJC_MODULES = ";
    Result += "&_OBJC_MODULES;\n";
    Result += "#pragma data_seg(pop)\n\n";
  }
}

/// RewriteObjCCategoryImplDecl - Rewrite metadata for each category
/// implementation.
void RewriteObjCFragileABI::RewriteObjCCategoryImplDecl(ObjCCategoryImplDecl *IDecl,
                                              std::string &Result) {
  ObjCInterfaceDecl *ClassDecl = IDecl->getClassInterface();
  // Find category declaration for this implementation.
  ObjCCategoryDecl *CDecl
    = ClassDecl->FindCategoryDeclaration(IDecl->getIdentifier());

  std::string FullCategoryName = ClassDecl->getNameAsString();
  FullCategoryName += '_';
  FullCategoryName += IDecl->getNameAsString();

  // Build _objc_method_list for class's instance methods if needed
  SmallVector<ObjCMethodDecl *, 32> InstanceMethods(IDecl->instance_methods());

  // If any of our property implementations have associated getters or
  // setters, produce metadata for them as well.
  for (const auto *Prop : IDecl->property_impls()) {
    if (Prop->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic)
      continue;
    if (!Prop->getPropertyIvarDecl())
      continue;
    ObjCPropertyDecl *PD = Prop->getPropertyDecl();
    if (!PD)
      continue;
    if (ObjCMethodDecl *Getter = Prop->getGetterMethodDecl())
      InstanceMethods.push_back(Getter);
    if (PD->isReadOnly())
      continue;
    if (ObjCMethodDecl *Setter = Prop->getSetterMethodDecl())
      InstanceMethods.push_back(Setter);
  }
  RewriteObjCMethodsMetaData(InstanceMethods.begin(), InstanceMethods.end(),
                             true, "CATEGORY_", FullCategoryName, Result);

  // Build _objc_method_list for class's class methods if needed
  RewriteObjCMethodsMetaData(IDecl->classmeth_begin(), IDecl->classmeth_end(),
                             false, "CATEGORY_", FullCategoryName, Result);

  // Protocols referenced in class declaration?
  // Null CDecl is case of a category implementation with no category interface
  if (CDecl)
    RewriteObjCProtocolListMetaData(CDecl->getReferencedProtocols(), "CATEGORY",
                                    FullCategoryName, Result);
  /* struct _objc_category {
   char *category_name;
   char *class_name;
   struct _objc_method_list *instance_methods;
   struct _objc_method_list *class_methods;
   struct _objc_protocol_list *protocols;
   // Objective-C 1.0 extensions
   uint32_t size;     // sizeof (struct _objc_category)
   struct _objc_property_list *instance_properties;  // category's own
   // @property decl.
   };
   */

  static bool objc_category = false;
  if (!objc_category) {
    Result += "\nstruct _objc_category {\n";
    Result += "\tchar *category_name;\n";
    Result += "\tchar *class_name;\n";
    Result += "\tstruct _objc_method_list *instance_methods;\n";
    Result += "\tstruct _objc_method_list *class_methods;\n";
    Result += "\tstruct _objc_protocol_list *protocols;\n";
    Result += "\tunsigned int size;\n";
    Result += "\tstruct _objc_property_list *instance_properties;\n";
    Result += "};\n";
    objc_category = true;
  }
  Result += "\nstatic struct _objc_category _OBJC_CATEGORY_";
  Result += FullCategoryName;
  Result += " __attribute__ ((used, section (\"__OBJC, __category\")))= {\n\t\"";
  Result += IDecl->getNameAsString();
  Result += "\"\n\t, \"";
  Result += ClassDecl->getNameAsString();
  Result += "\"\n";

  if (IDecl->instmeth_begin() != IDecl->instmeth_end()) {
    Result += "\t, (struct _objc_method_list *)"
    "&_OBJC_CATEGORY_INSTANCE_METHODS_";
    Result += FullCategoryName;
    Result += "\n";
  }
  else
    Result += "\t, 0\n";
  if (IDecl->classmeth_begin() != IDecl->classmeth_end()) {
    Result += "\t, (struct _objc_method_list *)"
    "&_OBJC_CATEGORY_CLASS_METHODS_";
    Result += FullCategoryName;
    Result += "\n";
  }
  else
    Result += "\t, 0\n";

  if (CDecl && CDecl->protocol_begin() != CDecl->protocol_end()) {
    Result += "\t, (struct _objc_protocol_list *)&_OBJC_CATEGORY_PROTOCOLS_";
    Result += FullCategoryName;
    Result += "\n";
  }
  else
    Result += "\t, 0\n";
  Result += "\t, sizeof(struct _objc_category), 0\n};\n";
}

// RewriteObjCMethodsMetaData - Rewrite methods metadata for instance or
/// class methods.
template<typename MethodIterator>
void RewriteObjCFragileABI::RewriteObjCMethodsMetaData(MethodIterator MethodBegin,
                                             MethodIterator MethodEnd,
                                             bool IsInstanceMethod,
                                             StringRef prefix,
                                             StringRef ClassName,
                                             std::string &Result) {
  if (MethodBegin == MethodEnd) return;

  if (!objc_impl_method) {
    /* struct _objc_method {
     SEL _cmd;
     char *method_types;
     void *_imp;
     }
     */
    Result += "\nstruct _objc_method {\n";
    Result += "\tSEL _cmd;\n";
    Result += "\tchar *method_types;\n";
    Result += "\tvoid *_imp;\n";
    Result += "};\n";

    objc_impl_method = true;
  }

  // Build _objc_method_list for class's methods if needed

  /* struct  {
   struct _objc_method_list *next_method;
   int method_count;
   struct _objc_method method_list[];
   }
   */
  unsigned NumMethods = std::distance(MethodBegin, MethodEnd);
  Result += "\nstatic struct {\n";
  Result += "\tstruct _objc_method_list *next_method;\n";
  Result += "\tint method_count;\n";
  Result += "\tstruct _objc_method method_list[";
  Result += utostr(NumMethods);
  Result += "];\n} _OBJC_";
  Result += prefix;
  Result += IsInstanceMethod ? "INSTANCE" : "CLASS";
  Result += "_METHODS_";
  Result += ClassName;
  Result += " __attribute__ ((used, section (\"__OBJC, __";
  Result += IsInstanceMethod ? "inst" : "cls";
  Result += "_meth\")))= ";
  Result += "{\n\t0, " + utostr(NumMethods) + "\n";

  Result += "\t,{{(SEL)\"";
  Result += (*MethodBegin)->getSelector().getAsString();
  std::string MethodTypeString =
    Context->getObjCEncodingForMethodDecl(*MethodBegin);
  Result += "\", \"";
  Result += MethodTypeString;
  Result += "\", (void *)";
  Result += MethodInternalNames[*MethodBegin];
  Result += "}\n";
  for (++MethodBegin; MethodBegin != MethodEnd; ++MethodBegin) {
    Result += "\t  ,{(SEL)\"";
    Result += (*MethodBegin)->getSelector().getAsString();
    std::string MethodTypeString =
      Context->getObjCEncodingForMethodDecl(*MethodBegin);
    Result += "\", \"";
    Result += MethodTypeString;
    Result += "\", (void *)";
    Result += MethodInternalNames[*MethodBegin];
    Result += "}\n";
  }
  Result += "\t }\n};\n";
}

Stmt *RewriteObjCFragileABI::RewriteObjCIvarRefExpr(ObjCIvarRefExpr *IV) {
  SourceRange OldRange = IV->getSourceRange();
  Expr *BaseExpr = IV->getBase();

  // Rewrite the base, but without actually doing replaces.
  {
    DisableReplaceStmtScope S(*this);
    BaseExpr = cast<Expr>(RewriteFunctionBodyOrGlobalInitializer(BaseExpr));
    IV->setBase(BaseExpr);
  }

  ObjCIvarDecl *D = IV->getDecl();

  Expr *Replacement = IV;
  if (CurMethodDef) {
    if (BaseExpr->getType()->isObjCObjectPointerType()) {
      const ObjCInterfaceType *iFaceDecl =
      dyn_cast<ObjCInterfaceType>(BaseExpr->getType()->getPointeeType());
      assert(iFaceDecl && "RewriteObjCIvarRefExpr - iFaceDecl is null");
      // lookup which class implements the instance variable.
      ObjCInterfaceDecl *clsDeclared = nullptr;
      iFaceDecl->getDecl()->lookupInstanceVariable(D->getIdentifier(),
                                                   clsDeclared);
      assert(clsDeclared && "RewriteObjCIvarRefExpr(): Can't find class");

      // Synthesize an explicit cast to gain access to the ivar.
      std::string RecName =
          std::string(clsDeclared->getIdentifier()->getName());
      RecName += "_IMPL";
      IdentifierInfo *II = &Context->Idents.get(RecName);
      RecordDecl *RD =
          RecordDecl::Create(*Context, TagTypeKind::Struct, TUDecl,
                             SourceLocation(), SourceLocation(), II);
      assert(RD && "RewriteObjCIvarRefExpr(): Can't find RecordDecl");
      QualType castT = Context->getPointerType(Context->getTagDeclType(RD));
      CastExpr *castExpr = NoTypeInfoCStyleCastExpr(Context, castT,
                                                    CK_BitCast,
                                                    IV->getBase());
      // Don't forget the parens to enforce the proper binding.
      ParenExpr *PE = new (Context) ParenExpr(OldRange.getBegin(),
                                              OldRange.getEnd(),
                                              castExpr);
      if (IV->isFreeIvar() &&
          declaresSameEntity(CurMethodDef->getClassInterface(),
                             iFaceDecl->getDecl())) {
        MemberExpr *ME = MemberExpr::CreateImplicit(
            *Context, PE, true, D, D->getType(), VK_LValue, OK_Ordinary);
        Replacement = ME;
      } else {
        IV->setBase(PE);
      }
    }
  } else { // we are outside a method.
    assert(!IV->isFreeIvar() && "Cannot have a free standing ivar outside a method");

    // Explicit ivar refs need to have a cast inserted.
    // FIXME: consider sharing some of this code with the code above.
    if (BaseExpr->getType()->isObjCObjectPointerType()) {
      const ObjCInterfaceType *iFaceDecl =
      dyn_cast<ObjCInterfaceType>(BaseExpr->getType()->getPointeeType());
      // lookup which class implements the instance variable.
      ObjCInterfaceDecl *clsDeclared = nullptr;
      iFaceDecl->getDecl()->lookupInstanceVariable(D->getIdentifier(),
                                                   clsDeclared);
      assert(clsDeclared && "RewriteObjCIvarRefExpr(): Can't find class");

      // Synthesize an explicit cast to gain access to the ivar.
      std::string RecName =
          std::string(clsDeclared->getIdentifier()->getName());
      RecName += "_IMPL";
      IdentifierInfo *II = &Context->Idents.get(RecName);
      RecordDecl *RD =
          RecordDecl::Create(*Context, TagTypeKind::Struct, TUDecl,
                             SourceLocation(), SourceLocation(), II);
      assert(RD && "RewriteObjCIvarRefExpr(): Can't find RecordDecl");
      QualType castT = Context->getPointerType(Context->getTagDeclType(RD));
      CastExpr *castExpr = NoTypeInfoCStyleCastExpr(Context, castT,
                                                    CK_BitCast,
                                                    IV->getBase());
      // Don't forget the parens to enforce the proper binding.
      ParenExpr *PE = new (Context) ParenExpr(
          IV->getBase()->getBeginLoc(), IV->getBase()->getEndLoc(), castExpr);
      // Cannot delete IV->getBase(), since PE points to it.
      // Replace the old base with the cast. This is important when doing
      // embedded rewrites. For example, [newInv->_container addObject:0].
      IV->setBase(PE);
    }
  }

  ReplaceStmtWithRange(IV, Replacement, OldRange);
  return Replacement;
}

#endif // CLANG_ENABLE_OBJC_REWRITER
