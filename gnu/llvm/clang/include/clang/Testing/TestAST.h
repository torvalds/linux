//===--- TestAST.h - Build clang ASTs for testing -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In normal operation of Clang, the FrontendAction's lifecycle both creates
// and destroys the AST, and code should operate on it during callbacks in
// between (e.g. via ASTConsumer).
//
// For tests it is often more convenient to parse an AST from code, and keep it
// alive as a normal local object, with assertions as straight-line code.
// TestAST provides such an interface.
// (ASTUnit can be used for this purpose, but is a production library with
// broad scope and complicated API).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TESTING_TESTAST_H
#define LLVM_CLANG_TESTING_TESTAST_H

#include "clang/Basic/LLVM.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Testing/CommandLineArgs.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace clang {

/// Specifies a virtual source file to be parsed as part of a test.
struct TestInputs {
  TestInputs() = default;
  TestInputs(StringRef Code) : Code(Code) {}

  /// The source code of the input file to be parsed.
  std::string Code;

  /// The language to parse as.
  /// This affects the -x and -std flags used, and the filename.
  TestLanguage Language = TestLanguage::Lang_OBJCXX;

  /// Extra argv to pass to clang -cc1.
  std::vector<std::string> ExtraArgs = {};

  /// Extra virtual files that are available to be #included.
  /// Keys are plain filenames ("foo.h"), values are file content.
  llvm::StringMap<std::string> ExtraFiles = {};

  /// Root of execution, all relative paths in Args/Files are resolved against
  /// this.
  std::string WorkingDir;

  /// Filename to use for translation unit. A default will be used when empty.
  std::string FileName;

  /// By default, error diagnostics during parsing are reported as gtest errors.
  /// To suppress this, set ErrorOK or include "error-ok" in a comment in Code.
  /// In either case, all diagnostics appear in TestAST::diagnostics().
  bool ErrorOK = false;

  /// The action used to parse the code.
  /// By default, a SyntaxOnlyAction is used.
  std::function<std::unique_ptr<FrontendAction>()> MakeAction;
};

/// The result of parsing a file specified by TestInputs.
///
/// The ASTContext, Sema etc are valid as long as this object is alive.
class TestAST {
public:
  /// Constructing a TestAST parses the virtual file.
  ///
  /// To keep tests terse, critical errors (e.g. invalid flags) are reported as
  /// unit test failures with ADD_FAILURE() and produce an empty ASTContext,
  /// Sema etc. This frees the test code from handling these explicitly.
  TestAST(const TestInputs &);
  TestAST(StringRef Code) : TestAST(TestInputs(Code)) {}
  TestAST(TestAST &&M);
  TestAST &operator=(TestAST &&);
  ~TestAST();

  /// Provides access to the AST context and other parts of Clang.

  ASTContext &context() { return Clang->getASTContext(); }
  Sema &sema() { return Clang->getSema(); }
  SourceManager &sourceManager() { return Clang->getSourceManager(); }
  FileManager &fileManager() { return Clang->getFileManager(); }
  Preprocessor &preprocessor() { return Clang->getPreprocessor(); }
  FrontendAction &action() { return *Action; }

  /// Returns diagnostics emitted during parsing.
  /// (By default, errors cause test failures, see TestInputs::ErrorOK).
  llvm::ArrayRef<StoredDiagnostic> diagnostics() { return Diagnostics; }

private:
  void clear();
  std::unique_ptr<FrontendAction> Action;
  std::unique_ptr<CompilerInstance> Clang;
  std::vector<StoredDiagnostic> Diagnostics;
};

} // end namespace clang

#endif
