#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/CodeGen/ObjectFilePCHContainerOperations.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>
#include <string>

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory InstrCategory("LLDB Instrumentation Generator");

class SBVisitor : public RecursiveASTVisitor<SBVisitor> {
public:
  SBVisitor(Rewriter &R, ASTContext &Context)
      : MyRewriter(R), Context(Context) {}

  bool VisitCXXMethodDecl(CXXMethodDecl *Decl) {
    // Not all decls should be registered. Please refer to that method's
    // comment for details.
    if (ShouldSkip(Decl))
      return false;

    // Print 'bool' instead of '_Bool'.
    PrintingPolicy Policy(Context.getLangOpts());
    Policy.Bool = true;

    // Collect the functions parameter types and names.
    std::vector<std::string> ParamNames;
    if (!Decl->isStatic())
      ParamNames.push_back("this");
    for (auto *P : Decl->parameters())
      ParamNames.push_back(P->getNameAsString());

    // Construct the macros.
    std::string Buffer;
    llvm::raw_string_ostream Macro(Buffer);
    if (ParamNames.empty()) {
      Macro << "LLDB_INSTRUMENT()";
    } else {
      Macro << "LLDB_INSTRUMENT_VA(" << llvm::join(ParamNames, ", ") << ")";
    }

    Stmt *Body = Decl->getBody();
    for (auto &C : Body->children()) {
      if (C->getBeginLoc().isMacroID()) {
        CharSourceRange Range =
            MyRewriter.getSourceMgr().getExpansionRange(C->getSourceRange());
        MyRewriter.ReplaceText(Range, Macro.str());
      } else {
        Macro << ";";
        SourceLocation InsertLoc = Lexer::getLocForEndOfToken(
            Body->getBeginLoc(), 0, MyRewriter.getSourceMgr(),
            MyRewriter.getLangOpts());
        MyRewriter.InsertTextAfter(InsertLoc, Macro.str());
      }
      break;
    }

    return true;
  }

private:
  /// Determine whether we need to consider the given CXXMethodDecl.
  ///
  /// Currently we skip the following cases:
  ///  1. Decls outside the main source file,
  ///  2. Decls that are only present in the source file,
  ///  3. Decls that are not definitions,
  ///  4. Non-public methods,
  ///  5. Variadic methods.
  ///  6. Destructors.
  bool ShouldSkip(CXXMethodDecl *Decl) {
    // Skip anything outside the main file.
    if (!MyRewriter.getSourceMgr().isInMainFile(Decl->getBeginLoc()))
      return true;

    // Skip if the canonical decl in the current decl. It means that the method
    // is declared in the implementation and is therefore not exposed as part
    // of the API.
    if (Decl == Decl->getCanonicalDecl())
      return true;

    // Skip decls that have no body, i.e. are just declarations.
    Stmt *Body = Decl->getBody();
    if (!Body)
      return true;

    // Skip non-public methods.
    AccessSpecifier AS = Decl->getAccess();
    if (AS != AccessSpecifier::AS_public)
      return true;

    // Skip variadic methods.
    if (Decl->isVariadic())
      return true;

    // Skip destructors.
    if (isa<CXXDestructorDecl>(Decl))
      return true;

    return false;
  }

  Rewriter &MyRewriter;
  ASTContext &Context;
};

class SBConsumer : public ASTConsumer {
public:
  SBConsumer(Rewriter &R, ASTContext &Context) : Visitor(R, Context) {}

  // Override the method that gets called for each parsed top-level
  // declaration.
  bool HandleTopLevelDecl(DeclGroupRef DR) override {
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      Visitor.TraverseDecl(*b);
    }
    return true;
  }

private:
  SBVisitor Visitor;
};

class SBAction : public ASTFrontendAction {
public:
  SBAction() = default;

  bool BeginSourceFileAction(CompilerInstance &CI) override { return true; }

  void EndSourceFileAction() override { MyRewriter.overwriteChangedFiles(); }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef File) override {
    MyRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<SBConsumer>(MyRewriter, CI.getASTContext());
  }

private:
  Rewriter MyRewriter;
};

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(
      argc, argv, InstrCategory, llvm::cl::OneOrMore,
      "Utility for generating the macros for LLDB's "
      "instrumentation framework.");
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OP = ExpectedParser.get();

  auto PCHOpts = std::make_shared<PCHContainerOperations>();
  PCHOpts->registerWriter(std::make_unique<ObjectFilePCHContainerWriter>());
  PCHOpts->registerReader(std::make_unique<ObjectFilePCHContainerReader>());

  ClangTool T(OP.getCompilations(), OP.getSourcePathList(), PCHOpts);
  return T.run(newFrontendActionFactory<SBAction>().get());
}
