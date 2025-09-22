//===- CommonOptionsParser.h - common options for clang tools -*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the CommonOptionsParser class used to parse common
//  command-line options for clang tools, so that they can be run as separate
//  command-line applications with a consistent common interface for handling
//  compilation database and input files.
//
//  It provides a common subset of command-line options, common algorithm
//  for locating a compilation database and source files, and help messages
//  for the basic command-line interface.
//
//  It creates a CompilationDatabase and reads common command-line options.
//
//  This class uses the Clang Tooling infrastructure, see
//    http://clang.llvm.org/docs/HowToSetupToolingForLLVM.html
//  for details on setting it up with LLVM source tree.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_COMMONOPTIONSPARSER_H
#define LLVM_CLANG_TOOLING_COMMONOPTIONSPARSER_H

#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

namespace clang {
namespace tooling {
/// A parser for options common to all command-line Clang tools.
///
/// Parses a common subset of command-line arguments, locates and loads a
/// compilation commands database and runs a tool with user-specified action. It
/// also contains a help message for the common command-line options.
///
/// An example of usage:
/// \code
/// #include "clang/Frontend/FrontendActions.h"
/// #include "clang/Tooling/CommonOptionsParser.h"
/// #include "clang/Tooling/Tooling.h"
/// #include "llvm/Support/CommandLine.h"
///
/// using namespace clang::tooling;
/// using namespace llvm;
///
/// static cl::OptionCategory MyToolCategory("my-tool options");
/// static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
/// static cl::extrahelp MoreHelp("\nMore help text...\n");
///
/// int main(int argc, const char **argv) {
///   auto ExpectedParser =
///       CommonOptionsParser::create(argc, argv, MyToolCategory);
///   if (!ExpectedParser) {
///     llvm::errs() << ExpectedParser.takeError();
///     return 1;
///   }
///   CommonOptionsParser& OptionsParser = ExpectedParser.get();
///   ClangTool Tool(OptionsParser.getCompilations(),
///                  OptionsParser.getSourcePathList());
///   return Tool.run(
///       newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
/// }
/// \endcode
class CommonOptionsParser {

protected:
  /// Parses command-line, initializes a compilation database.
  ///
  /// This constructor can change argc and argv contents, e.g. consume
  /// command-line options used for creating FixedCompilationDatabase.
  ///
  /// All options not belonging to \p Category become hidden.
  ///
  /// It also allows calls to set the required number of positional parameters.
  CommonOptionsParser(
      int &argc, const char **argv, llvm::cl::OptionCategory &Category,
      llvm::cl::NumOccurrencesFlag OccurrencesFlag = llvm::cl::OneOrMore,
      const char *Overview = nullptr);

public:
  /// A factory method that is similar to the above constructor, except
  /// this returns an error instead exiting the program on error.
  static llvm::Expected<CommonOptionsParser>
  create(int &argc, const char **argv, llvm::cl::OptionCategory &Category,
         llvm::cl::NumOccurrencesFlag OccurrencesFlag = llvm::cl::OneOrMore,
         const char *Overview = nullptr);

  /// Returns a reference to the loaded compilations database.
  CompilationDatabase &getCompilations() {
    return *Compilations;
  }

  /// Returns a list of source file paths to process.
  const std::vector<std::string> &getSourcePathList() const {
    return SourcePathList;
  }

  /// Returns the argument adjuster calculated from "--extra-arg" and
  //"--extra-arg-before" options.
  ArgumentsAdjuster getArgumentsAdjuster() { return Adjuster; }

  static const char *const HelpMessage;

private:
  CommonOptionsParser() = default;

  llvm::Error init(int &argc, const char **argv,
                   llvm::cl::OptionCategory &Category,
                   llvm::cl::NumOccurrencesFlag OccurrencesFlag,
                   const char *Overview);

  std::unique_ptr<CompilationDatabase> Compilations;
  std::vector<std::string> SourcePathList;
  ArgumentsAdjuster Adjuster;
};

class ArgumentsAdjustingCompilations : public CompilationDatabase {
public:
  ArgumentsAdjustingCompilations(
      std::unique_ptr<CompilationDatabase> Compilations)
      : Compilations(std::move(Compilations)) {}

  void appendArgumentsAdjuster(ArgumentsAdjuster Adjuster);

  std::vector<CompileCommand>
  getCompileCommands(StringRef FilePath) const override;

  std::vector<std::string> getAllFiles() const override;

  std::vector<CompileCommand> getAllCompileCommands() const override;

private:
  std::unique_ptr<CompilationDatabase> Compilations;
  std::vector<ArgumentsAdjuster> Adjusters;

  std::vector<CompileCommand>
  adjustCommands(std::vector<CompileCommand> Commands) const;
};

}  // namespace tooling
}  // namespace clang

#endif // LLVM_CLANG_TOOLING_COMMONOPTIONSPARSER_H
