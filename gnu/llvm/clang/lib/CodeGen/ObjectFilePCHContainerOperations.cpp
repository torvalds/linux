//===--- ObjectFilePCHContainerOperations.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/ObjectFilePCHContainerOperations.h"
#include "CGDebugInfo.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Path.h"
#include <memory>
#include <utility>

using namespace clang;

#define DEBUG_TYPE "pchcontainer"

namespace {
class PCHContainerGenerator : public ASTConsumer {
  DiagnosticsEngine &Diags;
  const std::string MainFileName;
  const std::string OutputFileName;
  ASTContext *Ctx;
  ModuleMap &MMap;
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
  const HeaderSearchOptions &HeaderSearchOpts;
  const PreprocessorOptions &PreprocessorOpts;
  CodeGenOptions CodeGenOpts;
  const TargetOptions TargetOpts;
  LangOptions LangOpts;
  std::unique_ptr<llvm::LLVMContext> VMContext;
  std::unique_ptr<llvm::Module> M;
  std::unique_ptr<CodeGen::CodeGenModule> Builder;
  std::unique_ptr<raw_pwrite_stream> OS;
  std::shared_ptr<PCHBuffer> Buffer;

  /// Visit every type and emit debug info for it.
  struct DebugTypeVisitor : public RecursiveASTVisitor<DebugTypeVisitor> {
    clang::CodeGen::CGDebugInfo &DI;
    ASTContext &Ctx;
    DebugTypeVisitor(clang::CodeGen::CGDebugInfo &DI, ASTContext &Ctx)
        : DI(DI), Ctx(Ctx) {}

    /// Determine whether this type can be represented in DWARF.
    static bool CanRepresent(const Type *Ty) {
      return !Ty->isDependentType() && !Ty->isUndeducedType();
    }

    bool VisitImportDecl(ImportDecl *D) {
      if (!D->getImportedOwningModule())
        DI.EmitImportDecl(*D);
      return true;
    }

    bool VisitTypeDecl(TypeDecl *D) {
      // TagDecls may be deferred until after all decls have been merged and we
      // know the complete type. Pure forward declarations will be skipped, but
      // they don't need to be emitted into the module anyway.
      if (auto *TD = dyn_cast<TagDecl>(D))
        if (!TD->isCompleteDefinition())
          return true;

      QualType QualTy = Ctx.getTypeDeclType(D);
      if (!QualTy.isNull() && CanRepresent(QualTy.getTypePtr()))
        DI.getOrCreateStandaloneType(QualTy, D->getLocation());
      return true;
    }

    bool VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
      QualType QualTy(D->getTypeForDecl(), 0);
      if (!QualTy.isNull() && CanRepresent(QualTy.getTypePtr()))
        DI.getOrCreateStandaloneType(QualTy, D->getLocation());
      return true;
    }

    bool VisitFunctionDecl(FunctionDecl *D) {
      // Skip deduction guides.
      if (isa<CXXDeductionGuideDecl>(D))
        return true;

      if (isa<CXXMethodDecl>(D))
        // This is not yet supported. Constructing the `this' argument
        // mandates a CodeGenFunction.
        return true;

      SmallVector<QualType, 16> ArgTypes;
      for (auto *i : D->parameters())
        ArgTypes.push_back(i->getType());
      QualType RetTy = D->getReturnType();
      QualType FnTy = Ctx.getFunctionType(RetTy, ArgTypes,
                                          FunctionProtoType::ExtProtoInfo());
      if (CanRepresent(FnTy.getTypePtr()))
        DI.EmitFunctionDecl(D, D->getLocation(), FnTy);
      return true;
    }

    bool VisitObjCMethodDecl(ObjCMethodDecl *D) {
      if (!D->getClassInterface())
        return true;

      bool selfIsPseudoStrong, selfIsConsumed;
      SmallVector<QualType, 16> ArgTypes;
      ArgTypes.push_back(D->getSelfType(Ctx, D->getClassInterface(),
                                        selfIsPseudoStrong, selfIsConsumed));
      ArgTypes.push_back(Ctx.getObjCSelType());
      for (auto *i : D->parameters())
        ArgTypes.push_back(i->getType());
      QualType RetTy = D->getReturnType();
      QualType FnTy = Ctx.getFunctionType(RetTy, ArgTypes,
                                          FunctionProtoType::ExtProtoInfo());
      if (CanRepresent(FnTy.getTypePtr()))
        DI.EmitFunctionDecl(D, D->getLocation(), FnTy);
      return true;
    }
  };

public:
  PCHContainerGenerator(CompilerInstance &CI, const std::string &MainFileName,
                        const std::string &OutputFileName,
                        std::unique_ptr<raw_pwrite_stream> OS,
                        std::shared_ptr<PCHBuffer> Buffer)
      : Diags(CI.getDiagnostics()), MainFileName(MainFileName),
        OutputFileName(OutputFileName), Ctx(nullptr),
        MMap(CI.getPreprocessor().getHeaderSearchInfo().getModuleMap()),
        FS(&CI.getVirtualFileSystem()),
        HeaderSearchOpts(CI.getHeaderSearchOpts()),
        PreprocessorOpts(CI.getPreprocessorOpts()),
        TargetOpts(CI.getTargetOpts()), LangOpts(CI.getLangOpts()),
        OS(std::move(OS)), Buffer(std::move(Buffer)) {
    // The debug info output isn't affected by CodeModel and
    // ThreadModel, but the backend expects them to be nonempty.
    CodeGenOpts.CodeModel = "default";
    LangOpts.setThreadModel(LangOptions::ThreadModelKind::Single);
    CodeGenOpts.DebugTypeExtRefs = true;
    // When building a module MainFileName is the name of the modulemap file.
    CodeGenOpts.MainFileName =
        LangOpts.CurrentModule.empty() ? MainFileName : LangOpts.CurrentModule;
    CodeGenOpts.setDebugInfo(llvm::codegenoptions::FullDebugInfo);
    CodeGenOpts.setDebuggerTuning(CI.getCodeGenOpts().getDebuggerTuning());
    CodeGenOpts.DwarfVersion = CI.getCodeGenOpts().DwarfVersion;
    CodeGenOpts.DebugCompilationDir =
        CI.getInvocation().getCodeGenOpts().DebugCompilationDir;
    CodeGenOpts.DebugPrefixMap =
        CI.getInvocation().getCodeGenOpts().DebugPrefixMap;
    CodeGenOpts.DebugStrictDwarf = CI.getCodeGenOpts().DebugStrictDwarf;
  }

  ~PCHContainerGenerator() override = default;

  void Initialize(ASTContext &Context) override {
    assert(!Ctx && "initialized multiple times");

    Ctx = &Context;
    VMContext.reset(new llvm::LLVMContext());
    M.reset(new llvm::Module(MainFileName, *VMContext));
    M->setDataLayout(Ctx->getTargetInfo().getDataLayoutString());
    Builder.reset(new CodeGen::CodeGenModule(
        *Ctx, FS, HeaderSearchOpts, PreprocessorOpts, CodeGenOpts, *M, Diags));

    // Prepare CGDebugInfo to emit debug info for a clang module.
    auto *DI = Builder->getModuleDebugInfo();
    StringRef ModuleName = llvm::sys::path::filename(MainFileName);
    DI->setPCHDescriptor(
        {ModuleName, "", OutputFileName, ASTFileSignature::createDISentinel()});
    DI->setModuleMap(MMap);
  }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    if (Diags.hasErrorOccurred())
      return true;

    // Collect debug info for all decls in this group.
    for (auto *I : D)
      if (!I->isFromASTFile()) {
        DebugTypeVisitor DTV(*Builder->getModuleDebugInfo(), *Ctx);
        DTV.TraverseDecl(I);
      }
    return true;
  }

  void HandleTopLevelDeclInObjCContainer(DeclGroupRef D) override {
    HandleTopLevelDecl(D);
  }

  void HandleTagDeclDefinition(TagDecl *D) override {
    if (Diags.hasErrorOccurred())
      return;

    if (D->isFromASTFile())
      return;

    // Anonymous tag decls are deferred until we are building their declcontext.
    if (D->getName().empty())
      return;

    // Defer tag decls until their declcontext is complete.
    auto *DeclCtx = D->getDeclContext();
    while (DeclCtx) {
      if (auto *D = dyn_cast<TagDecl>(DeclCtx))
        if (!D->isCompleteDefinition())
          return;
      DeclCtx = DeclCtx->getParent();
    }

    DebugTypeVisitor DTV(*Builder->getModuleDebugInfo(), *Ctx);
    DTV.TraverseDecl(D);
    Builder->UpdateCompletedType(D);
  }

  void HandleTagDeclRequiredDefinition(const TagDecl *D) override {
    if (Diags.hasErrorOccurred())
      return;

    if (const RecordDecl *RD = dyn_cast<RecordDecl>(D))
      Builder->getModuleDebugInfo()->completeRequiredType(RD);
  }

  void HandleImplicitImportDecl(ImportDecl *D) override {
    if (!D->getImportedOwningModule())
      Builder->getModuleDebugInfo()->EmitImportDecl(*D);
  }

  /// Emit a container holding the serialized AST.
  void HandleTranslationUnit(ASTContext &Ctx) override {
    assert(M && VMContext && Builder);
    // Delete these on function exit.
    std::unique_ptr<llvm::LLVMContext> VMContext = std::move(this->VMContext);
    std::unique_ptr<llvm::Module> M = std::move(this->M);
    std::unique_ptr<CodeGen::CodeGenModule> Builder = std::move(this->Builder);

    if (Diags.hasErrorOccurred())
      return;

    M->setTargetTriple(Ctx.getTargetInfo().getTriple().getTriple());
    M->setDataLayout(Ctx.getTargetInfo().getDataLayoutString());

    // PCH files don't have a signature field in the control block,
    // but LLVM detects DWO CUs by looking for a non-zero DWO id.
    // We use the lower 64 bits for debug info.

    uint64_t Signature =
        Buffer->Signature ? Buffer->Signature.truncatedValue() : ~1ULL;

    Builder->getModuleDebugInfo()->setDwoId(Signature);

    // Finalize the Builder.
    if (Builder)
      Builder->Release();

    // Ensure the target exists.
    std::string Error;
    auto Triple = Ctx.getTargetInfo().getTriple();
    if (!llvm::TargetRegistry::lookupTarget(Triple.getTriple(), Error))
      llvm::report_fatal_error(llvm::Twine(Error));

    // Emit the serialized Clang AST into its own section.
    assert(Buffer->IsComplete && "serialization did not complete");
    auto &SerializedAST = Buffer->Data;
    auto Size = SerializedAST.size();

    if (Triple.isOSBinFormatWasm()) {
      // Emit __clangast in custom section instead of named data segment
      // to find it while iterating sections.
      // This could be avoided if all data segements (the wasm sense) were
      // represented as their own sections (in the llvm sense).
      // TODO: https://github.com/WebAssembly/tool-conventions/issues/138
      llvm::NamedMDNode *MD =
          M->getOrInsertNamedMetadata("wasm.custom_sections");
      llvm::Metadata *Ops[2] = {
          llvm::MDString::get(*VMContext, "__clangast"),
          llvm::MDString::get(*VMContext,
                              StringRef(SerializedAST.data(), Size))};
      auto *NameAndContent = llvm::MDTuple::get(*VMContext, Ops);
      MD->addOperand(NameAndContent);
    } else {
      auto Int8Ty = llvm::Type::getInt8Ty(*VMContext);
      auto *Ty = llvm::ArrayType::get(Int8Ty, Size);
      auto *Data = llvm::ConstantDataArray::getString(
          *VMContext, StringRef(SerializedAST.data(), Size),
          /*AddNull=*/false);
      auto *ASTSym = new llvm::GlobalVariable(
          *M, Ty, /*constant*/ true, llvm::GlobalVariable::InternalLinkage,
          Data, "__clang_ast");
      // The on-disk hashtable needs to be aligned.
      ASTSym->setAlignment(llvm::Align(8));

      // Mach-O also needs a segment name.
      if (Triple.isOSBinFormatMachO())
        ASTSym->setSection("__CLANG,__clangast");
      // COFF has an eight character length limit.
      else if (Triple.isOSBinFormatCOFF())
        ASTSym->setSection("clangast");
      else
        ASTSym->setSection("__clangast");
    }

    LLVM_DEBUG({
      // Print the IR for the PCH container to the debug output.
      llvm::SmallString<0> Buffer;
      clang::EmitBackendOutput(
          Diags, HeaderSearchOpts, CodeGenOpts, TargetOpts, LangOpts,
          Ctx.getTargetInfo().getDataLayoutString(), M.get(),
          BackendAction::Backend_EmitLL, FS,
          std::make_unique<llvm::raw_svector_ostream>(Buffer));
      llvm::dbgs() << Buffer;
    });

    // Use the LLVM backend to emit the pch container.
    clang::EmitBackendOutput(Diags, HeaderSearchOpts, CodeGenOpts, TargetOpts,
                             LangOpts,
                             Ctx.getTargetInfo().getDataLayoutString(), M.get(),
                             BackendAction::Backend_EmitObj, FS, std::move(OS));

    // Free the memory for the temporary buffer.
    llvm::SmallVector<char, 0> Empty;
    SerializedAST = std::move(Empty);
  }
};

} // anonymous namespace

std::unique_ptr<ASTConsumer>
ObjectFilePCHContainerWriter::CreatePCHContainerGenerator(
    CompilerInstance &CI, const std::string &MainFileName,
    const std::string &OutputFileName,
    std::unique_ptr<llvm::raw_pwrite_stream> OS,
    std::shared_ptr<PCHBuffer> Buffer) const {
  return std::make_unique<PCHContainerGenerator>(
      CI, MainFileName, OutputFileName, std::move(OS), Buffer);
}

ArrayRef<StringRef> ObjectFilePCHContainerReader::getFormats() const {
  static StringRef Formats[] = {"obj", "raw"};
  return Formats;
}

StringRef
ObjectFilePCHContainerReader::ExtractPCH(llvm::MemoryBufferRef Buffer) const {
  StringRef PCH;
  auto OFOrErr = llvm::object::ObjectFile::createObjectFile(Buffer);
  if (OFOrErr) {
    auto &OF = OFOrErr.get();
    bool IsCOFF = isa<llvm::object::COFFObjectFile>(*OF);
    // Find the clang AST section in the container.
    for (auto &Section : OF->sections()) {
      StringRef Name;
      if (Expected<StringRef> NameOrErr = Section.getName())
        Name = *NameOrErr;
      else
        consumeError(NameOrErr.takeError());

      if ((!IsCOFF && Name == "__clangast") || (IsCOFF && Name == "clangast")) {
        if (Expected<StringRef> E = Section.getContents())
          return *E;
        else {
          handleAllErrors(E.takeError(), [&](const llvm::ErrorInfoBase &EIB) {
            EIB.log(llvm::errs());
          });
          return "";
        }
      }
    }
  }
  handleAllErrors(OFOrErr.takeError(), [&](const llvm::ErrorInfoBase &EIB) {
    if (EIB.convertToErrorCode() ==
        llvm::object::object_error::invalid_file_type)
      // As a fallback, treat the buffer as a raw AST.
      PCH = Buffer.getBuffer();
    else
      EIB.log(llvm::errs());
  });
  return PCH;
}
