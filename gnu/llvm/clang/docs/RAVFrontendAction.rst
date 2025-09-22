==========================================================
How to write RecursiveASTVisitor based ASTFrontendActions.
==========================================================

Introduction
============

In this tutorial you will learn how to create a FrontendAction that uses
a RecursiveASTVisitor to find CXXRecordDecl AST nodes with a specified
name.

Creating a FrontendAction
=========================

When writing a clang based tool like a Clang Plugin or a standalone tool
based on LibTooling, the common entry point is the FrontendAction.
FrontendAction is an interface that allows execution of user specific
actions as part of the compilation. To run tools over the AST clang
provides the convenience interface ASTFrontendAction, which takes care
of executing the action. The only part left is to implement the
CreateASTConsumer method that returns an ASTConsumer per translation
unit.

::

      class FindNamedClassAction : public clang::ASTFrontendAction {
      public:
        virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
          clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
          return std::make_unique<FindNamedClassConsumer>();
        }
      };

Creating an ASTConsumer
=======================

ASTConsumer is an interface used to write generic actions on an AST,
regardless of how the AST was produced. ASTConsumer provides many
different entry points, but for our use case the only one needed is
HandleTranslationUnit, which is called with the ASTContext for the
translation unit.

::

      class FindNamedClassConsumer : public clang::ASTConsumer {
      public:
        virtual void HandleTranslationUnit(clang::ASTContext &Context) {
          // Traversing the translation unit decl via a RecursiveASTVisitor
          // will visit all nodes in the AST.
          Visitor.TraverseDecl(Context.getTranslationUnitDecl());
        }
      private:
        // A RecursiveASTVisitor implementation.
        FindNamedClassVisitor Visitor;
      };

Using the RecursiveASTVisitor
=============================

Now that everything is hooked up, the next step is to implement a
RecursiveASTVisitor to extract the relevant information from the AST.

The RecursiveASTVisitor provides hooks of the form bool
VisitNodeType(NodeType \*) for most AST nodes; the exception are TypeLoc
nodes, which are passed by-value. We only need to implement the methods
for the relevant node types.

Let's start by writing a RecursiveASTVisitor that visits all
CXXRecordDecl's.

::

      class FindNamedClassVisitor
        : public RecursiveASTVisitor<FindNamedClassVisitor> {
      public:
        bool VisitCXXRecordDecl(CXXRecordDecl *Declaration) {
          // For debugging, dumping the AST nodes will show which nodes are already
          // being visited.
          Declaration->dump();

          // The return value indicates whether we want the visitation to proceed.
          // Return false to stop the traversal of the AST.
          return true;
        }
      };

In the methods of our RecursiveASTVisitor we can now use the full power
of the Clang AST to drill through to the parts that are interesting for
us. For example, to find all class declaration with a certain name, we
can check for a specific qualified name:

::

      bool VisitCXXRecordDecl(CXXRecordDecl *Declaration) {
        if (Declaration->getQualifiedNameAsString() == "n::m::C")
          Declaration->dump();
        return true;
      }

Accessing the SourceManager and ASTContext
==========================================

Some of the information about the AST, like source locations and global
identifier information, are not stored in the AST nodes themselves, but
in the ASTContext and its associated source manager. To retrieve them we
need to hand the ASTContext into our RecursiveASTVisitor implementation.

The ASTContext is available from the CompilerInstance during the call to
CreateASTConsumer. We can thus extract it there and hand it into our
freshly created FindNamedClassConsumer:

::

      virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
        return std::make_unique<FindNamedClassConsumer>(&Compiler.getASTContext());
      }

Now that the ASTContext is available in the RecursiveASTVisitor, we can
do more interesting things with AST nodes, like looking up their source
locations:

::

      bool VisitCXXRecordDecl(CXXRecordDecl *Declaration) {
        if (Declaration->getQualifiedNameAsString() == "n::m::C") {
          // getFullLoc uses the ASTContext's SourceManager to resolve the source
          // location and break it up into its line and column parts.
          FullSourceLoc FullLocation = Context->getFullLoc(Declaration->getBeginLoc());
          if (FullLocation.isValid())
            llvm::outs() << "Found declaration at "
                         << FullLocation.getSpellingLineNumber() << ":"
                         << FullLocation.getSpellingColumnNumber() << "\n";
        }
        return true;
      }

Putting it all together
=======================

Now we can combine all of the above into a small example program:

::

      #include "clang/AST/ASTConsumer.h"
      #include "clang/AST/RecursiveASTVisitor.h"
      #include "clang/Frontend/CompilerInstance.h"
      #include "clang/Frontend/FrontendAction.h"
      #include "clang/Tooling/Tooling.h"

      using namespace clang;

      class FindNamedClassVisitor
        : public RecursiveASTVisitor<FindNamedClassVisitor> {
      public:
        explicit FindNamedClassVisitor(ASTContext *Context)
          : Context(Context) {}

        bool VisitCXXRecordDecl(CXXRecordDecl *Declaration) {
          if (Declaration->getQualifiedNameAsString() == "n::m::C") {
            FullSourceLoc FullLocation = Context->getFullLoc(Declaration->getBeginLoc());
            if (FullLocation.isValid())
              llvm::outs() << "Found declaration at "
                           << FullLocation.getSpellingLineNumber() << ":"
                           << FullLocation.getSpellingColumnNumber() << "\n";
          }
          return true;
        }

      private:
        ASTContext *Context;
      };

      class FindNamedClassConsumer : public clang::ASTConsumer {
      public:
        explicit FindNamedClassConsumer(ASTContext *Context)
          : Visitor(Context) {}

        virtual void HandleTranslationUnit(clang::ASTContext &Context) {
          Visitor.TraverseDecl(Context.getTranslationUnitDecl());
        }
      private:
        FindNamedClassVisitor Visitor;
      };

      class FindNamedClassAction : public clang::ASTFrontendAction {
      public:
        virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
          clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
          return std::make_unique<FindNamedClassConsumer>(&Compiler.getASTContext());
        }
      };

      int main(int argc, char **argv) {
        if (argc > 1) {
          clang::tooling::runToolOnCode(std::make_unique<FindNamedClassAction>(), argv[1]);
        }
      }

We store this into a file called FindClassDecls.cpp and create the
following CMakeLists.txt to link it:

::

    set(LLVM_LINK_COMPONENTS
      Support
      )

    add_clang_executable(find-class-decls FindClassDecls.cpp)

    target_link_libraries(find-class-decls
      PRIVATE
      clangAST
      clangBasic
      clangFrontend
      clangSerialization
      clangTooling
      )

When running this tool over a small code snippet it will output all
declarations of a class n::m::C it found:

::

      $ ./bin/find-class-decls "namespace n { namespace m { class C {}; } }"
      Found declaration at 1:29
