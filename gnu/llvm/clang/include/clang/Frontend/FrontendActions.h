//===-- FrontendActions.h - Useful Frontend Actions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_FRONTENDACTIONS_H
#define LLVM_CLANG_FRONTEND_FRONTENDACTIONS_H

#include "clang/Frontend/FrontendAction.h"
#include <memory>
#include <string>
#include <vector>

namespace clang {

//===----------------------------------------------------------------------===//
// Custom Consumer Actions
//===----------------------------------------------------------------------===//

class InitOnlyAction : public FrontendAction {
  void ExecuteAction() override;

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

public:
  // Don't claim to only use the preprocessor, we want to follow the AST path,
  // but do nothing.
  bool usesPreprocessorOnly() const override { return false; }
};

/// Preprocessor-based frontend action that also loads PCH files.
class ReadPCHAndPreprocessAction : public FrontendAction {
  void ExecuteAction() override;

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

public:
  bool usesPreprocessorOnly() const override { return false; }
};

class DumpCompilerOptionsAction : public FrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    return nullptr;
  }

  void ExecuteAction() override;

public:
  bool usesPreprocessorOnly() const override { return true; }
};

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

class ASTPrintAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class ASTDumpAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class ASTDeclListAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class ASTViewAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class GeneratePCHAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  TranslationUnitKind getTranslationUnitKind() override {
    return TU_Prefix;
  }

  bool hasASTFileSupport() const override { return false; }

  bool shouldEraseOutputFiles() override;

public:
  /// Compute the AST consumer arguments that will be used to
  /// create the PCHGenerator instance returned by CreateASTConsumer.
  ///
  /// \returns false if an error occurred, true otherwise.
  static bool ComputeASTConsumerArguments(CompilerInstance &CI,
                                          std::string &Sysroot);

  /// Creates file to write the PCH into and returns a stream to write it
  /// into. On error, returns null.
  static std::unique_ptr<llvm::raw_pwrite_stream>
  CreateOutputFile(CompilerInstance &CI, StringRef InFile,
                   std::string &OutputFile);

  bool BeginSourceFileAction(CompilerInstance &CI) override;
};

class GenerateModuleAction : public ASTFrontendAction {
  virtual std::unique_ptr<raw_pwrite_stream>
  CreateOutputFile(CompilerInstance &CI, StringRef InFile) = 0;

protected:
  std::vector<std::unique_ptr<ASTConsumer>>
  CreateMultiplexConsumer(CompilerInstance &CI, StringRef InFile);

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  TranslationUnitKind getTranslationUnitKind() override {
    return TU_ClangModule;
  }

  bool hasASTFileSupport() const override { return false; }

  bool shouldEraseOutputFiles() override;
};

class GenerateInterfaceStubsAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  TranslationUnitKind getTranslationUnitKind() override {
    return TU_ClangModule;
  }
  bool hasASTFileSupport() const override { return false; }
};

class GenerateModuleFromModuleMapAction : public GenerateModuleAction {
private:
  bool BeginSourceFileAction(CompilerInstance &CI) override;

  std::unique_ptr<raw_pwrite_stream>
  CreateOutputFile(CompilerInstance &CI, StringRef InFile) override;
};

/// Generates full BMI (which contains full information to generate the object
/// files) for C++20 Named Modules.
class GenerateModuleInterfaceAction : public GenerateModuleAction {
protected:
  bool BeginSourceFileAction(CompilerInstance &CI) override;

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  TranslationUnitKind getTranslationUnitKind() override { return TU_Complete; }

  std::unique_ptr<raw_pwrite_stream>
  CreateOutputFile(CompilerInstance &CI, StringRef InFile) override;
};

/// Only generates the reduced BMI. This action is mainly used by tests.
class GenerateReducedModuleInterfaceAction
    : public GenerateModuleInterfaceAction {
private:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class GenerateHeaderUnitAction : public GenerateModuleAction {

private:
  bool BeginSourceFileAction(CompilerInstance &CI) override;

  std::unique_ptr<raw_pwrite_stream>
  CreateOutputFile(CompilerInstance &CI, StringRef InFile) override;
};

class SyntaxOnlyAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

public:
  ~SyntaxOnlyAction() override;
  bool hasCodeCompletionSupport() const override { return true; }
};

/// Dump information about the given module file, to be used for
/// basic debugging and discovery.
class DumpModuleInfoAction : public ASTFrontendAction {
  // Allow other tools (ex lldb) to direct output for their use.
  std::shared_ptr<llvm::raw_ostream> OutputStream;

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
  bool BeginInvocation(CompilerInstance &CI) override;
  void ExecuteAction() override;

public:
  DumpModuleInfoAction() = default;
  explicit DumpModuleInfoAction(std::shared_ptr<llvm::raw_ostream> Out)
      : OutputStream(Out) {}
  bool hasPCHSupport() const override { return false; }
  bool hasASTFileSupport() const override { return true; }
  bool hasIRSupport() const override { return false; }
  bool hasCodeCompletionSupport() const override { return false; }
};

class VerifyPCHAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  void ExecuteAction() override;

public:
  bool hasCodeCompletionSupport() const override { return false; }
};

class TemplightDumpAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  void ExecuteAction() override;
};

/**
 * Frontend action adaptor that merges ASTs together.
 *
 * This action takes an existing AST file and "merges" it into the AST
 * context, producing a merged context. This action is an action
 * adaptor, which forwards most of its calls to another action that
 * will consume the merged context.
 */
class ASTMergeAction : public FrontendAction {
  /// The action that the merge action adapts.
  std::unique_ptr<FrontendAction> AdaptedAction;

  /// The set of AST files to merge.
  std::vector<std::string> ASTFiles;

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  bool BeginSourceFileAction(CompilerInstance &CI) override;

  void ExecuteAction() override;
  void EndSourceFileAction() override;

public:
  ASTMergeAction(std::unique_ptr<FrontendAction> AdaptedAction,
                 ArrayRef<std::string> ASTFiles);
  ~ASTMergeAction() override;

  bool usesPreprocessorOnly() const override;
  TranslationUnitKind getTranslationUnitKind() override;
  bool hasPCHSupport() const override;
  bool hasASTFileSupport() const override;
  bool hasCodeCompletionSupport() const override;
};

class PrintPreambleAction : public FrontendAction {
protected:
  void ExecuteAction() override;
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                 StringRef) override {
    return nullptr;
  }

  bool usesPreprocessorOnly() const override { return true; }
};

class PrintDependencyDirectivesSourceMinimizerAction : public FrontendAction {
protected:
  void ExecuteAction() override;
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                 StringRef) override {
    return nullptr;
  }

  bool usesPreprocessorOnly() const override { return true; }
};

//===----------------------------------------------------------------------===//
// Preprocessor Actions
//===----------------------------------------------------------------------===//

class DumpRawTokensAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

class DumpTokensAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

class PreprocessOnlyAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

class PrintPreprocessedAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;

  bool hasPCHSupport() const override { return true; }
};

class GetDependenciesByModuleNameAction : public PreprocessOnlyAction {
  StringRef ModuleName;
  void ExecuteAction() override;

public:
  GetDependenciesByModuleNameAction(StringRef ModuleName)
      : ModuleName(ModuleName) {}
};

}  // end namespace clang

#endif
