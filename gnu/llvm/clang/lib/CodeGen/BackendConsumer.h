//===--- BackendConsumer.h - LLVM BackendConsumer Header File -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_BACKENDCONSUMER_H
#define LLVM_CLANG_LIB_CODEGEN_BACKENDCONSUMER_H

#include "clang/CodeGen/BackendUtil.h"
#include "clang/CodeGen/CodeGenAction.h"

#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/Timer.h"

namespace llvm {
  class DiagnosticInfoDontCall;
}

namespace clang {
class ASTContext;
class CodeGenAction;
class CoverageSourceInfo;

class BackendConsumer : public ASTConsumer {
  using LinkModule = CodeGenAction::LinkModule;

  virtual void anchor();
  DiagnosticsEngine &Diags;
  BackendAction Action;
  const HeaderSearchOptions &HeaderSearchOpts;
  const CodeGenOptions &CodeGenOpts;
  const TargetOptions &TargetOpts;
  const LangOptions &LangOpts;
  std::unique_ptr<raw_pwrite_stream> AsmOutStream;
  ASTContext *Context;
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;

  llvm::Timer LLVMIRGeneration;
  unsigned LLVMIRGenerationRefCount;

  /// True if we've finished generating IR. This prevents us from generating
  /// additional LLVM IR after emitting output in HandleTranslationUnit. This
  /// can happen when Clang plugins trigger additional AST deserialization.
  bool IRGenFinished = false;

  bool TimerIsEnabled = false;

  std::unique_ptr<CodeGenerator> Gen;

  SmallVector<LinkModule, 4> LinkModules;

  // A map from mangled names to their function's source location, used for
  // backend diagnostics as the Clang AST may be unavailable. We actually use
  // the mangled name's hash as the key because mangled names can be very
  // long and take up lots of space. Using a hash can cause name collision,
  // but that is rare and the consequences are pointing to a wrong source
  // location which is not severe. This is a vector instead of an actual map
  // because we optimize for time building this map rather than time
  // retrieving an entry, as backend diagnostics are uncommon.
  std::vector<std::pair<llvm::hash_code, FullSourceLoc>>
    ManglingFullSourceLocs;


  // This is here so that the diagnostic printer knows the module a diagnostic
  // refers to.
  llvm::Module *CurLinkModule = nullptr;

public:
  BackendConsumer(BackendAction Action, DiagnosticsEngine &Diags,
                  IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                  const HeaderSearchOptions &HeaderSearchOpts,
                  const PreprocessorOptions &PPOpts,
                  const CodeGenOptions &CodeGenOpts,
                  const TargetOptions &TargetOpts, const LangOptions &LangOpts,
                  const std::string &InFile,
                  SmallVector<LinkModule, 4> LinkModules,
                  std::unique_ptr<raw_pwrite_stream> OS, llvm::LLVMContext &C,
                  CoverageSourceInfo *CoverageInfo = nullptr);

  // This constructor is used in installing an empty BackendConsumer
  // to use the clang diagnostic handler for IR input files. It avoids
  // initializing the OS field.
  BackendConsumer(BackendAction Action, DiagnosticsEngine &Diags,
                  IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                  const HeaderSearchOptions &HeaderSearchOpts,
                  const PreprocessorOptions &PPOpts,
                  const CodeGenOptions &CodeGenOpts,
                  const TargetOptions &TargetOpts, const LangOptions &LangOpts,
                  llvm::Module *Module, SmallVector<LinkModule, 4> LinkModules,
                  llvm::LLVMContext &C,
                  CoverageSourceInfo *CoverageInfo = nullptr);

  llvm::Module *getModule() const;
  std::unique_ptr<llvm::Module> takeModule();

  CodeGenerator *getCodeGenerator();

  void HandleCXXStaticMemberVarInstantiation(VarDecl *VD) override;
  void Initialize(ASTContext &Ctx) override;
  bool HandleTopLevelDecl(DeclGroupRef D) override;
  void HandleInlineFunctionDefinition(FunctionDecl *D) override;
  void HandleInterestingDecl(DeclGroupRef D) override;
  void HandleTranslationUnit(ASTContext &C) override;
  void HandleTagDeclDefinition(TagDecl *D) override;
  void HandleTagDeclRequiredDefinition(const TagDecl *D) override;
  void CompleteTentativeDefinition(VarDecl *D) override;
  void CompleteExternalDeclaration(DeclaratorDecl *D) override;
  void AssignInheritanceModel(CXXRecordDecl *RD) override;
  void HandleVTable(CXXRecordDecl *RD) override;

  // Links each entry in LinkModules into our module.  Returns true on error.
  bool LinkInModules(llvm::Module *M);

  /// Get the best possible source location to represent a diagnostic that
  /// may have associated debug info.
  const FullSourceLoc getBestLocationFromDebugLoc(
    const llvm::DiagnosticInfoWithLocationBase &D,
    bool &BadDebugInfo, StringRef &Filename,
    unsigned &Line, unsigned &Column) const;

  std::optional<FullSourceLoc> getFunctionSourceLocation(
    const llvm::Function &F) const;

  void DiagnosticHandlerImpl(const llvm::DiagnosticInfo &DI);
  /// Specialized handler for InlineAsm diagnostic.
  /// \return True if the diagnostic has been successfully reported, false
  /// otherwise.
  bool InlineAsmDiagHandler(const llvm::DiagnosticInfoInlineAsm &D);
  /// Specialized handler for diagnostics reported using SMDiagnostic.
  void SrcMgrDiagHandler(const llvm::DiagnosticInfoSrcMgr &D);
  /// Specialized handler for StackSize diagnostic.
  /// \return True if the diagnostic has been successfully reported, false
  /// otherwise.
  bool StackSizeDiagHandler(const llvm::DiagnosticInfoStackSize &D);
  /// Specialized handler for ResourceLimit diagnostic.
  /// \return True if the diagnostic has been successfully reported, false
  /// otherwise.
  bool ResourceLimitDiagHandler(const llvm::DiagnosticInfoResourceLimit &D);

  /// Specialized handler for unsupported backend feature diagnostic.
  void UnsupportedDiagHandler(const llvm::DiagnosticInfoUnsupported &D);
  /// Specialized handlers for optimization remarks.
  /// Note that these handlers only accept remarks and they always handle
  /// them.
  void EmitOptimizationMessage(const llvm::DiagnosticInfoOptimizationBase &D,
                               unsigned DiagID);
  void
    OptimizationRemarkHandler(const llvm::DiagnosticInfoOptimizationBase &D);
  void OptimizationRemarkHandler(
    const llvm::OptimizationRemarkAnalysisFPCommute &D);
  void OptimizationRemarkHandler(
    const llvm::OptimizationRemarkAnalysisAliasing &D);
  void OptimizationFailureHandler(
    const llvm::DiagnosticInfoOptimizationFailure &D);
  void DontCallDiagHandler(const llvm::DiagnosticInfoDontCall &D);
  /// Specialized handler for misexpect warnings.
  /// Note that misexpect remarks are emitted through ORE
  void MisExpectDiagHandler(const llvm::DiagnosticInfoMisExpect &D);
};

} // namespace clang
#endif
