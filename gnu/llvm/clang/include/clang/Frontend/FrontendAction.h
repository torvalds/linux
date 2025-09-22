//===-- FrontendAction.h - Generic Frontend Action Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::FrontendAction interface and various convenience
/// abstract classes (clang::ASTFrontendAction, clang::PluginASTAction,
/// clang::PreprocessorFrontendAction, and clang::WrapperFrontendAction)
/// derived from it.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_FRONTENDACTION_H
#define LLVM_CLANG_FRONTEND_FRONTENDACTION_H

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>
#include <vector>

namespace clang {
class ASTMergeAction;
class CompilerInstance;

/// Abstract base class for actions which can be performed by the frontend.
class FrontendAction {
  FrontendInputFile CurrentInput;
  std::unique_ptr<ASTUnit> CurrentASTUnit;
  CompilerInstance *Instance;
  friend class ASTMergeAction;
  friend class WrapperFrontendAction;

private:
  std::unique_ptr<ASTConsumer> CreateWrappedASTConsumer(CompilerInstance &CI,
                                                        StringRef InFile);

protected:
  /// @name Implementation Action Interface
  /// @{

  /// Prepare to execute the action on the given CompilerInstance.
  ///
  /// This is called before executing the action on any inputs, and can modify
  /// the configuration as needed (including adjusting the input list).
  virtual bool PrepareToExecuteAction(CompilerInstance &CI) { return true; }

  /// Create the AST consumer object for this action, if supported.
  ///
  /// This routine is called as part of BeginSourceFile(), which will
  /// fail if the AST consumer cannot be created. This will not be called if the
  /// action has indicated that it only uses the preprocessor.
  ///
  /// \param CI - The current compiler instance, provided as a convenience, see
  /// getCompilerInstance().
  ///
  /// \param InFile - The current input file, provided as a convenience, see
  /// getCurrentFile().
  ///
  /// \return The new AST consumer, or null on failure.
  virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                         StringRef InFile) = 0;

  /// Callback before starting processing a single input, giving the
  /// opportunity to modify the CompilerInvocation or do some other action
  /// before BeginSourceFileAction is called.
  ///
  /// \return True on success; on failure BeginSourceFileAction(),
  /// ExecuteAction() and EndSourceFileAction() will not be called.
  virtual bool BeginInvocation(CompilerInstance &CI) { return true; }

  /// Callback at the start of processing a single input.
  ///
  /// \return True on success; on failure ExecutionAction() and
  /// EndSourceFileAction() will not be called.
  virtual bool BeginSourceFileAction(CompilerInstance &CI) {
    return true;
  }

  /// Callback to run the program action, using the initialized
  /// compiler instance.
  ///
  /// This is guaranteed to only be called between BeginSourceFileAction()
  /// and EndSourceFileAction().
  virtual void ExecuteAction() = 0;

  /// Callback at the end of processing a single input.
  ///
  /// This is guaranteed to only be called following a successful call to
  /// BeginSourceFileAction (and BeginSourceFile).
  virtual void EndSourceFileAction() {}

  /// Callback at the end of processing a single input, to determine
  /// if the output files should be erased or not.
  ///
  /// By default it returns true if a compiler error occurred.
  /// This is guaranteed to only be called following a successful call to
  /// BeginSourceFileAction (and BeginSourceFile).
  virtual bool shouldEraseOutputFiles();

  /// @}

public:
  FrontendAction();
  virtual ~FrontendAction();

  /// @name Compiler Instance Access
  /// @{

  CompilerInstance &getCompilerInstance() const {
    assert(Instance && "Compiler instance not registered!");
    return *Instance;
  }

  void setCompilerInstance(CompilerInstance *Value) { Instance = Value; }

  /// @}
  /// @name Current File Information
  /// @{

  bool isCurrentFileAST() const {
    assert(!CurrentInput.isEmpty() && "No current file!");
    return (bool)CurrentASTUnit;
  }

  const FrontendInputFile &getCurrentInput() const {
    return CurrentInput;
  }

  StringRef getCurrentFile() const {
    assert(!CurrentInput.isEmpty() && "No current file!");
    return CurrentInput.getFile();
  }

  StringRef getCurrentFileOrBufferName() const {
    assert(!CurrentInput.isEmpty() && "No current file!");
    return CurrentInput.isFile()
               ? CurrentInput.getFile()
               : CurrentInput.getBuffer().getBufferIdentifier();
  }

  InputKind getCurrentFileKind() const {
    assert(!CurrentInput.isEmpty() && "No current file!");
    return CurrentInput.getKind();
  }

  ASTUnit &getCurrentASTUnit() const {
    assert(CurrentASTUnit && "No current AST unit!");
    return *CurrentASTUnit;
  }

  Module *getCurrentModule() const;

  std::unique_ptr<ASTUnit> takeCurrentASTUnit() {
    return std::move(CurrentASTUnit);
  }

  void setCurrentInput(const FrontendInputFile &CurrentInput,
                       std::unique_ptr<ASTUnit> AST = nullptr);

  /// @}
  /// @name Supported Modes
  /// @{

  /// Is this action invoked on a model file?
  ///
  /// Model files are incomplete translation units that relies on type
  /// information from another translation unit. Check ParseModelFileAction for
  /// details.
  virtual bool isModelParsingAction() const { return false; }

  /// Does this action only use the preprocessor?
  ///
  /// If so no AST context will be created and this action will be invalid
  /// with AST file inputs.
  virtual bool usesPreprocessorOnly() const = 0;

  /// For AST-based actions, the kind of translation unit we're handling.
  virtual TranslationUnitKind getTranslationUnitKind() { return TU_Complete; }

  /// Does this action support use with PCH?
  virtual bool hasPCHSupport() const { return true; }

  /// Does this action support use with AST files?
  virtual bool hasASTFileSupport() const { return true; }

  /// Does this action support use with IR files?
  virtual bool hasIRSupport() const { return false; }

  /// Does this action support use with code completion?
  virtual bool hasCodeCompletionSupport() const { return false; }

  /// @}
  /// @name Public Action Interface
  /// @{

  /// Prepare the action to execute on the given compiler instance.
  bool PrepareToExecute(CompilerInstance &CI) {
    return PrepareToExecuteAction(CI);
  }

  /// Prepare the action for processing the input file \p Input.
  ///
  /// This is run after the options and frontend have been initialized,
  /// but prior to executing any per-file processing.
  ///
  /// \param CI - The compiler instance this action is being run from. The
  /// action may store and use this object up until the matching EndSourceFile
  /// action.
  ///
  /// \param Input - The input filename and kind. Some input kinds are handled
  /// specially, for example AST inputs, since the AST file itself contains
  /// several objects which would normally be owned by the
  /// CompilerInstance. When processing AST input files, these objects should
  /// generally not be initialized in the CompilerInstance -- they will
  /// automatically be shared with the AST file in between
  /// BeginSourceFile() and EndSourceFile().
  ///
  /// \return True on success; on failure the compilation of this file should
  /// be aborted and neither Execute() nor EndSourceFile() should be called.
  bool BeginSourceFile(CompilerInstance &CI, const FrontendInputFile &Input);

  /// Set the source manager's main input file, and run the action.
  llvm::Error Execute();

  /// Perform any per-file post processing, deallocate per-file
  /// objects, and run statistics and output file cleanup code.
  virtual void EndSourceFile();

  /// @}
};

/// Abstract base class to use for AST consumer-based frontend actions.
class ASTFrontendAction : public FrontendAction {
protected:
  /// Implement the ExecuteAction interface by running Sema on
  /// the already-initialized AST consumer.
  ///
  /// This will also take care of instantiating a code completion consumer if
  /// the user requested it and the action supports it.
  void ExecuteAction() override;

public:
  ASTFrontendAction() {}
  bool usesPreprocessorOnly() const override { return false; }
};

class PluginASTAction : public ASTFrontendAction {
  virtual void anchor();
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override = 0;

  /// Parse the given plugin command line arguments.
  ///
  /// \param CI - The compiler instance, for use in reporting diagnostics.
  /// \return True if the parsing succeeded; otherwise the plugin will be
  /// destroyed and no action run. The plugin is responsible for using the
  /// CompilerInstance's Diagnostic object to report errors.
  virtual bool ParseArgs(const CompilerInstance &CI,
                         const std::vector<std::string> &arg) = 0;

  enum ActionType {
    CmdlineBeforeMainAction, ///< Execute the action before the main action if
                             ///< on the command line
    CmdlineAfterMainAction,  ///< Execute the action after the main action if on
                             ///< the command line
    ReplaceAction,           ///< Replace the main action
    AddBeforeMainAction,     ///< Execute the action before the main action
    AddAfterMainAction       ///< Execute the action after the main action
  };
  /// Get the action type for this plugin
  ///
  /// \return The action type. By default we use CmdlineAfterMainAction.
  virtual ActionType getActionType() { return CmdlineAfterMainAction; }
};

/// Abstract base class to use for preprocessor-based frontend actions.
class PreprocessorFrontendAction : public FrontendAction {
protected:
  /// Provide a default implementation which returns aborts;
  /// this method should never be called by FrontendAction clients.
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

public:
  bool usesPreprocessorOnly() const override { return true; }
};

/// A frontend action which simply wraps some other runtime-specified
/// frontend action.
///
/// Deriving from this class allows an action to inject custom logic around
/// some existing action's behavior. It implements every virtual method in
/// the FrontendAction interface by forwarding to the wrapped action.
class WrapperFrontendAction : public FrontendAction {
protected:
  std::unique_ptr<FrontendAction> WrappedAction;

  bool PrepareToExecuteAction(CompilerInstance &CI) override;
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
  bool BeginInvocation(CompilerInstance &CI) override;
  bool BeginSourceFileAction(CompilerInstance &CI) override;
  void ExecuteAction() override;
  void EndSourceFile() override;
  void EndSourceFileAction() override;
  bool shouldEraseOutputFiles() override;

public:
  /// Construct a WrapperFrontendAction from an existing action, taking
  /// ownership of it.
  WrapperFrontendAction(std::unique_ptr<FrontendAction> WrappedAction);

  bool usesPreprocessorOnly() const override;
  TranslationUnitKind getTranslationUnitKind() override;
  bool hasPCHSupport() const override;
  bool hasASTFileSupport() const override;
  bool hasIRSupport() const override;
  bool hasCodeCompletionSupport() const override;
};

}  // end namespace clang

#endif
