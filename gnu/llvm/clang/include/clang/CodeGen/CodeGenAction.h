//===--- CodeGenAction.h - LLVM Code Generation Frontend Action -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_CODEGENACTION_H
#define LLVM_CLANG_CODEGEN_CODEGENACTION_H

#include "clang/Frontend/FrontendAction.h"
#include <memory>

namespace llvm {
  class LLVMContext;
  class Module;
}

namespace clang {
class BackendConsumer;
class CodeGenerator;

class CodeGenAction : public ASTFrontendAction {
private:
  // Let BackendConsumer access LinkModule.
  friend class BackendConsumer;

  /// Info about module to link into a module we're generating.
  struct LinkModule {
    /// The module to link in.
    std::unique_ptr<llvm::Module> Module;

    /// If true, we set attributes on Module's functions according to our
    /// CodeGenOptions and LangOptions, as though we were generating the
    /// function ourselves.
    bool PropagateAttrs;

    /// If true, we use LLVM module internalizer.
    bool Internalize;

    /// Bitwise combination of llvm::LinkerFlags used when we link the module.
    unsigned LinkFlags;
  };

  unsigned Act;
  std::unique_ptr<llvm::Module> TheModule;

  /// Bitcode modules to link in to our module.
  SmallVector<LinkModule, 4> LinkModules;
  llvm::LLVMContext *VMContext;
  bool OwnsVMContext;

  std::unique_ptr<llvm::Module> loadModule(llvm::MemoryBufferRef MBRef);

  /// Load bitcode modules to link into our module from the options.
  bool loadLinkModules(CompilerInstance &CI);

protected:
  bool BeginSourceFileAction(CompilerInstance &CI) override;

  /// Create a new code generation action.  If the optional \p _VMContext
  /// parameter is supplied, the action uses it without taking ownership,
  /// otherwise it creates a fresh LLVM context and takes ownership.
  CodeGenAction(unsigned _Act, llvm::LLVMContext *_VMContext = nullptr);

  bool hasIRSupport() const override;

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  void ExecuteAction() override;

  void EndSourceFileAction() override;

public:
  ~CodeGenAction() override;

  /// Take the generated LLVM module, for use after the action has been run.
  /// The result may be null on failure.
  std::unique_ptr<llvm::Module> takeModule();

  /// Take the LLVM context used by this action.
  llvm::LLVMContext *takeLLVMContext();

  CodeGenerator *getCodeGenerator() const;

  BackendConsumer *BEConsumer = nullptr;
};

class EmitAssemblyAction : public CodeGenAction {
  virtual void anchor();
public:
  EmitAssemblyAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitBCAction : public CodeGenAction {
  virtual void anchor();
public:
  EmitBCAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitLLVMAction : public CodeGenAction {
  virtual void anchor();
public:
  EmitLLVMAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitLLVMOnlyAction : public CodeGenAction {
  virtual void anchor();
public:
  EmitLLVMOnlyAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitCodeGenOnlyAction : public CodeGenAction {
  virtual void anchor();
public:
  EmitCodeGenOnlyAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitObjAction : public CodeGenAction {
  virtual void anchor();
public:
  EmitObjAction(llvm::LLVMContext *_VMContext = nullptr);
};

}

#endif
