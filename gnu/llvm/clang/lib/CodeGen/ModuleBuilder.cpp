//===--- ModuleBuilder.cpp - Emit LLVM Code from ASTs ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This builds an AST and converts it to LLVM Code.
//
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/ModuleBuilder.h"
#include "CGDebugInfo.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>

using namespace clang;
using namespace CodeGen;

namespace {
  class CodeGeneratorImpl : public CodeGenerator {
    DiagnosticsEngine &Diags;
    ASTContext *Ctx;
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS; // Only used for debug info.
    const HeaderSearchOptions &HeaderSearchOpts; // Only used for debug info.
    const PreprocessorOptions &PreprocessorOpts; // Only used for debug info.
    const CodeGenOptions &CodeGenOpts;

    unsigned HandlingTopLevelDecls;

    /// Use this when emitting decls to block re-entrant decl emission. It will
    /// emit all deferred decls on scope exit. Set EmitDeferred to false if decl
    /// emission must be deferred longer, like at the end of a tag definition.
    struct HandlingTopLevelDeclRAII {
      CodeGeneratorImpl &Self;
      bool EmitDeferred;
      HandlingTopLevelDeclRAII(CodeGeneratorImpl &Self,
                               bool EmitDeferred = true)
          : Self(Self), EmitDeferred(EmitDeferred) {
        ++Self.HandlingTopLevelDecls;
      }
      ~HandlingTopLevelDeclRAII() {
        unsigned Level = --Self.HandlingTopLevelDecls;
        if (Level == 0 && EmitDeferred)
          Self.EmitDeferredDecls();
      }
    };

    CoverageSourceInfo *CoverageInfo;

  protected:
    std::unique_ptr<llvm::Module> M;
    std::unique_ptr<CodeGen::CodeGenModule> Builder;

  private:
    SmallVector<FunctionDecl *, 8> DeferredInlineMemberFuncDefs;

    static llvm::StringRef ExpandModuleName(llvm::StringRef ModuleName,
                                            const CodeGenOptions &CGO) {
      if (ModuleName == "-" && !CGO.MainFileName.empty())
        return CGO.MainFileName;
      return ModuleName;
    }

  public:
    CodeGeneratorImpl(DiagnosticsEngine &diags, llvm::StringRef ModuleName,
                      IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                      const HeaderSearchOptions &HSO,
                      const PreprocessorOptions &PPO, const CodeGenOptions &CGO,
                      llvm::LLVMContext &C,
                      CoverageSourceInfo *CoverageInfo = nullptr)
        : Diags(diags), Ctx(nullptr), FS(std::move(FS)), HeaderSearchOpts(HSO),
          PreprocessorOpts(PPO), CodeGenOpts(CGO), HandlingTopLevelDecls(0),
          CoverageInfo(CoverageInfo),
          M(new llvm::Module(ExpandModuleName(ModuleName, CGO), C)) {
      C.setDiscardValueNames(CGO.DiscardValueNames);
    }

    ~CodeGeneratorImpl() override {
      // There should normally not be any leftover inline method definitions.
      assert(DeferredInlineMemberFuncDefs.empty() ||
             Diags.hasErrorOccurred());
    }

    CodeGenModule &CGM() {
      return *Builder;
    }

    llvm::Module *GetModule() {
      return M.get();
    }

    CGDebugInfo *getCGDebugInfo() {
      return Builder->getModuleDebugInfo();
    }

    llvm::Module *ReleaseModule() {
      return M.release();
    }

    const Decl *GetDeclForMangledName(StringRef MangledName) {
      GlobalDecl Result;
      if (!Builder->lookupRepresentativeDecl(MangledName, Result))
        return nullptr;
      const Decl *D = Result.getCanonicalDecl().getDecl();
      if (auto FD = dyn_cast<FunctionDecl>(D)) {
        if (FD->hasBody(FD))
          return FD;
      } else if (auto TD = dyn_cast<TagDecl>(D)) {
        if (auto Def = TD->getDefinition())
          return Def;
      }
      return D;
    }

    llvm::StringRef GetMangledName(GlobalDecl GD) {
      return Builder->getMangledName(GD);
    }

    llvm::Constant *GetAddrOfGlobal(GlobalDecl global, bool isForDefinition) {
      return Builder->GetAddrOfGlobal(global, ForDefinition_t(isForDefinition));
    }

    llvm::Module *StartModule(llvm::StringRef ModuleName,
                              llvm::LLVMContext &C) {
      assert(!M && "Replacing existing Module?");
      M.reset(new llvm::Module(ExpandModuleName(ModuleName, CodeGenOpts), C));

      std::unique_ptr<CodeGenModule> OldBuilder = std::move(Builder);

      Initialize(*Ctx);

      if (OldBuilder)
        OldBuilder->moveLazyEmissionStates(Builder.get());

      return M.get();
    }

    void Initialize(ASTContext &Context) override {
      Ctx = &Context;

      M->setTargetTriple(Ctx->getTargetInfo().getTriple().getTriple());
      M->setDataLayout(Ctx->getTargetInfo().getDataLayoutString());
      const auto &SDKVersion = Ctx->getTargetInfo().getSDKVersion();
      if (!SDKVersion.empty())
        M->setSDKVersion(SDKVersion);
      if (const auto *TVT = Ctx->getTargetInfo().getDarwinTargetVariantTriple())
        M->setDarwinTargetVariantTriple(TVT->getTriple());
      if (auto TVSDKVersion =
              Ctx->getTargetInfo().getDarwinTargetVariantSDKVersion())
        M->setDarwinTargetVariantSDKVersion(*TVSDKVersion);
      Builder.reset(new CodeGen::CodeGenModule(Context, FS, HeaderSearchOpts,
                                               PreprocessorOpts, CodeGenOpts,
                                               *M, Diags, CoverageInfo));

      for (auto &&Lib : CodeGenOpts.DependentLibraries)
        Builder->AddDependentLib(Lib);
      for (auto &&Opt : CodeGenOpts.LinkerOptions)
        Builder->AppendLinkerOptions(Opt);
    }

    void HandleCXXStaticMemberVarInstantiation(VarDecl *VD) override {
      if (Diags.hasErrorOccurred())
        return;

      Builder->HandleCXXStaticMemberVarInstantiation(VD);
    }

    bool HandleTopLevelDecl(DeclGroupRef DG) override {
      // FIXME: Why not return false and abort parsing?
      if (Diags.hasUnrecoverableErrorOccurred())
        return true;

      HandlingTopLevelDeclRAII HandlingDecl(*this);

      // Make sure to emit all elements of a Decl.
      for (DeclGroupRef::iterator I = DG.begin(), E = DG.end(); I != E; ++I)
        Builder->EmitTopLevelDecl(*I);

      return true;
    }

    void EmitDeferredDecls() {
      if (DeferredInlineMemberFuncDefs.empty())
        return;

      // Emit any deferred inline method definitions. Note that more deferred
      // methods may be added during this loop, since ASTConsumer callbacks
      // can be invoked if AST inspection results in declarations being added.
      HandlingTopLevelDeclRAII HandlingDecl(*this);
      for (unsigned I = 0; I != DeferredInlineMemberFuncDefs.size(); ++I)
        Builder->EmitTopLevelDecl(DeferredInlineMemberFuncDefs[I]);
      DeferredInlineMemberFuncDefs.clear();
    }

    void HandleInlineFunctionDefinition(FunctionDecl *D) override {
      if (Diags.hasUnrecoverableErrorOccurred())
        return;

      assert(D->doesThisDeclarationHaveABody());

      // We may want to emit this definition. However, that decision might be
      // based on computing the linkage, and we have to defer that in case we
      // are inside of something that will change the method's final linkage,
      // e.g.
      //   typedef struct {
      //     void bar();
      //     void foo() { bar(); }
      //   } A;
      DeferredInlineMemberFuncDefs.push_back(D);

      // Provide some coverage mapping even for methods that aren't emitted.
      // Don't do this for templated classes though, as they may not be
      // instantiable.
      if (!D->getLexicalDeclContext()->isDependentContext())
        Builder->AddDeferredUnusedCoverageMapping(D);
    }

    /// HandleTagDeclDefinition - This callback is invoked each time a TagDecl
    /// to (e.g. struct, union, enum, class) is completed. This allows the
    /// client hack on the type, which can occur at any point in the file
    /// (because these can be defined in declspecs).
    void HandleTagDeclDefinition(TagDecl *D) override {
      if (Diags.hasUnrecoverableErrorOccurred())
        return;

      // Don't allow re-entrant calls to CodeGen triggered by PCH
      // deserialization to emit deferred decls.
      HandlingTopLevelDeclRAII HandlingDecl(*this, /*EmitDeferred=*/false);

      Builder->UpdateCompletedType(D);

      // For MSVC compatibility, treat declarations of static data members with
      // inline initializers as definitions.
      if (Ctx->getTargetInfo().getCXXABI().isMicrosoft()) {
        for (Decl *Member : D->decls()) {
          if (VarDecl *VD = dyn_cast<VarDecl>(Member)) {
            if (Ctx->isMSStaticDataMemberInlineDefinition(VD) &&
                Ctx->DeclMustBeEmitted(VD)) {
              Builder->EmitGlobal(VD);
            }
          }
        }
      }
      // For OpenMP emit declare reduction functions, if required.
      if (Ctx->getLangOpts().OpenMP) {
        for (Decl *Member : D->decls()) {
          if (auto *DRD = dyn_cast<OMPDeclareReductionDecl>(Member)) {
            if (Ctx->DeclMustBeEmitted(DRD))
              Builder->EmitGlobal(DRD);
          } else if (auto *DMD = dyn_cast<OMPDeclareMapperDecl>(Member)) {
            if (Ctx->DeclMustBeEmitted(DMD))
              Builder->EmitGlobal(DMD);
          }
        }
      }
    }

    void HandleTagDeclRequiredDefinition(const TagDecl *D) override {
      if (Diags.hasUnrecoverableErrorOccurred())
        return;

      // Don't allow re-entrant calls to CodeGen triggered by PCH
      // deserialization to emit deferred decls.
      HandlingTopLevelDeclRAII HandlingDecl(*this, /*EmitDeferred=*/false);

      if (CodeGen::CGDebugInfo *DI = Builder->getModuleDebugInfo())
        if (const RecordDecl *RD = dyn_cast<RecordDecl>(D))
          DI->completeRequiredType(RD);
    }

    void HandleTranslationUnit(ASTContext &Ctx) override {
      // Release the Builder when there is no error.
      if (!Diags.hasUnrecoverableErrorOccurred() && Builder)
        Builder->Release();

      // If there are errors before or when releasing the Builder, reset
      // the module to stop here before invoking the backend.
      if (Diags.hasErrorOccurred()) {
        if (Builder)
          Builder->clear();
        M.reset();
        return;
      }
    }

    void AssignInheritanceModel(CXXRecordDecl *RD) override {
      if (Diags.hasUnrecoverableErrorOccurred())
        return;

      Builder->RefreshTypeCacheForClass(RD);
    }

    void CompleteTentativeDefinition(VarDecl *D) override {
      if (Diags.hasUnrecoverableErrorOccurred())
        return;

      Builder->EmitTentativeDefinition(D);
    }

    void CompleteExternalDeclaration(DeclaratorDecl *D) override {
      Builder->EmitExternalDeclaration(D);
    }

    void HandleVTable(CXXRecordDecl *RD) override {
      if (Diags.hasUnrecoverableErrorOccurred())
        return;

      Builder->EmitVTable(RD);
    }
  };
}

void CodeGenerator::anchor() { }

CodeGenModule &CodeGenerator::CGM() {
  return static_cast<CodeGeneratorImpl*>(this)->CGM();
}

llvm::Module *CodeGenerator::GetModule() {
  return static_cast<CodeGeneratorImpl*>(this)->GetModule();
}

llvm::Module *CodeGenerator::ReleaseModule() {
  return static_cast<CodeGeneratorImpl*>(this)->ReleaseModule();
}

CGDebugInfo *CodeGenerator::getCGDebugInfo() {
  return static_cast<CodeGeneratorImpl*>(this)->getCGDebugInfo();
}

const Decl *CodeGenerator::GetDeclForMangledName(llvm::StringRef name) {
  return static_cast<CodeGeneratorImpl*>(this)->GetDeclForMangledName(name);
}

llvm::StringRef CodeGenerator::GetMangledName(GlobalDecl GD) {
  return static_cast<CodeGeneratorImpl *>(this)->GetMangledName(GD);
}

llvm::Constant *CodeGenerator::GetAddrOfGlobal(GlobalDecl global,
                                               bool isForDefinition) {
  return static_cast<CodeGeneratorImpl*>(this)
           ->GetAddrOfGlobal(global, isForDefinition);
}

llvm::Module *CodeGenerator::StartModule(llvm::StringRef ModuleName,
                                         llvm::LLVMContext &C) {
  return static_cast<CodeGeneratorImpl*>(this)->StartModule(ModuleName, C);
}

CodeGenerator *
clang::CreateLLVMCodeGen(DiagnosticsEngine &Diags, llvm::StringRef ModuleName,
                         IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                         const HeaderSearchOptions &HeaderSearchOpts,
                         const PreprocessorOptions &PreprocessorOpts,
                         const CodeGenOptions &CGO, llvm::LLVMContext &C,
                         CoverageSourceInfo *CoverageInfo) {
  return new CodeGeneratorImpl(Diags, ModuleName, std::move(FS),
                               HeaderSearchOpts, PreprocessorOpts, CGO, C,
                               CoverageInfo);
}
