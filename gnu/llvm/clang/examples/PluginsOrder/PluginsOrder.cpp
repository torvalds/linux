//===- PluginsOrder.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
using namespace clang;

namespace {

class AlwaysBeforeConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &) override {
    llvm::errs() << "always-before\n";
  }
};

class AlwaysBeforeAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<AlwaysBeforeConsumer>();
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    return true;
  }

  PluginASTAction::ActionType getActionType() override {
    return AddBeforeMainAction;
  }
};

class AlwaysAfterConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &) override {
    llvm::errs() << "always-after\n";
  }
};

class AlwaysAfterAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<AlwaysAfterConsumer>();
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    return true;
  }

  PluginASTAction::ActionType getActionType() override {
    return AddAfterMainAction;
  }
};

class CmdAfterConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &) override {
    llvm::errs() << "cmd-after\n";
  }
};

class CmdAfterAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<CmdAfterConsumer>();
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    return true;
  }

  PluginASTAction::ActionType getActionType() override {
    return CmdlineAfterMainAction;
  }
};

class CmdBeforeConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &) override {
    llvm::errs() << "cmd-before\n";
  }
};

class CmdBeforeAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<CmdBeforeConsumer>();
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    return true;
  }

  PluginASTAction::ActionType getActionType() override {
    return CmdlineBeforeMainAction;
  }
};

} // namespace

static FrontendPluginRegistry::Add<CmdBeforeAction> X1("cmd-before", "");
static FrontendPluginRegistry::Add<CmdAfterAction> X2("cmd-after", "");
static FrontendPluginRegistry::Add<AlwaysBeforeAction> X3("always-before", "");
static FrontendPluginRegistry::Add<AlwaysAfterAction> X4("always-after", "");
