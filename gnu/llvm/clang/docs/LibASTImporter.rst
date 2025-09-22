===============================
ASTImporter: Merging Clang ASTs
===============================

The ``ASTImporter`` class is part of Clang's core library, the AST library.
It imports nodes of an ``ASTContext`` into another ``ASTContext``.

In this document, we assume basic knowledge about the Clang AST.  See the :doc:`Introduction
to the Clang AST <IntroductionToTheClangAST>` if you want to learn more
about how the AST is structured.
Knowledge about :doc:`matching the Clang AST <LibASTMatchers>` and the `reference for the matchers <https://clang.llvm.org/docs/LibASTMatchersReference.html>`_ are also useful.

.. contents::
   :local:

Introduction
------------

``ASTContext`` holds long-lived AST nodes (such as types and decls) that can be referred to throughout the semantic analysis of a file.
In some cases it is preferable to work with more than one ``ASTContext``.
For example, we'd like to parse multiple different files inside the same Clang tool.
It may be convenient if we could view the set of the resulting ASTs as if they were one AST resulting from the parsing of each file together.
``ASTImporter`` provides the way to copy types or declarations from one ``ASTContext`` to another.
We refer to the context from which we import as the **"from" context** or *source context*; and the context into which we import as the **"to" context** or *destination context*.

Existing clients of the ``ASTImporter`` library are Cross Translation Unit (CTU) static analysis and the LLDB expression parser.
CTU static analysis imports a definition of a function if its definition is found in another translation unit (TU).
This way the analysis can breach out from the single TU limitation.
LLDB's ``expr`` command parses a user-defined expression, creates an ``ASTContext`` for that and then imports the missing definitions from the AST what we got from the debug information (DWARF, etc).

Algorithm of the import
-----------------------

Importing one AST node copies that node into the destination ``ASTContext``.
Why do we have to copy the node?
Isn't enough to insert the pointer to that node into the destination context?
One reason is that the "from" context may outlive the "to" context.
Also, the Clang AST consider nodes (or certain properties of nodes) equivalent if they have the same address!

The import algorithm has to ensure that the structurally equivalent nodes in the different translation units are not getting duplicated in the merged AST.
E.g. if we include the definition of the vector template (``#include <vector>``) in two translation units, then their merged AST should have only one node which represents the template.
Also, we have to discover *one definition rule* (ODR) violations.
For instance, if there is a class definition with the same name in both translation units, but one of the definition contains a different number of fields.
So, we look up existing definitions, and then we check the structural equivalency on those nodes.
The following pseudo-code demonstrates the basics of the import mechanism:

.. code-block:: cpp

  // Pseudo-code(!) of import:
  ErrorOrDecl Import(Decl *FromD) {
    Decl *ToDecl = nullptr;
    FoundDeclsList = Look up all Decls in the "to" Ctx with the same name of FromD;
    for (auto FoundDecl : FoundDeclsList) {
      if (StructurallyEquivalentDecls(FoundDecl, FromD)) {
        ToDecl = FoundDecl;
        Mark FromD as imported;
        break;
      } else {
        Report ODR violation;
        return error;
      }
    }
    if (FoundDeclsList is empty) {
      Import dependent declarations and types of ToDecl;
      ToDecl = create a new AST node in "to" Ctx;
      Mark FromD as imported;
    }
    return ToDecl;
  }

Two AST nodes are *structurally equivalent* if they are

- builtin types and refer to the same type, e.g. ``int`` and ``int`` are structurally equivalent,
- function types and all their parameters have structurally equivalent types,
- record types and all their fields in order of their definition have the same identifier names and structurally equivalent types,
- variable or function declarations and they have the same identifier name and their types are structurally equivalent.

We could extend the definition of structural equivalency to templates similarly.

If A and B are AST nodes and *A depends on B*, then we say that A is a **dependant** of B and B is a **dependency** of A.
The words "dependant" and "dependency" are nouns in British English.
Unfortunately, in American English, the adjective "dependent" is used for both meanings.
In this document, with the "dependent" adjective we always address the dependencies, the B node in the example.

API
---

Let's create a tool which uses the ASTImporter class!
First, we build two ASTs from virtual files; the content of the virtual files are synthesized from string literals:

.. code-block:: cpp

  std::unique_ptr<ASTUnit> ToUnit = buildASTFromCode(
      "", "to.cc"); // empty file
  std::unique_ptr<ASTUnit> FromUnit = buildASTFromCode(
      R"(
      class MyClass {
        int m1;
        int m2;
      };
      )",
      "from.cc");

The first AST corresponds to the destination ("to") context - which is empty - and the second for the source ("from") context.
Next, we define a matcher to match ``MyClass`` in the "from" context:

.. code-block:: cpp

  auto Matcher = cxxRecordDecl(hasName("MyClass"));
  auto *From = getFirstDecl<CXXRecordDecl>(Matcher, FromUnit);

Now we create the Importer and do the import:

.. code-block:: cpp

  ASTImporter Importer(ToUnit->getASTContext(), ToUnit->getFileManager(),
                       FromUnit->getASTContext(), FromUnit->getFileManager(),
                       /*MinimalImport=*/true);
  llvm::Expected<Decl *> ImportedOrErr = Importer.Import(From);

The ``Import`` call returns with ``llvm::Expected``, so, we must check for any error.
Please refer to the `error handling <https://llvm.org/docs/ProgrammersManual.html#recoverable-errors>`_ documentation for details.

.. code-block:: cpp

  if (!ImportedOrErr) {
    llvm::Error Err = ImportedOrErr.takeError();
    llvm::errs() << "ERROR: " << Err << "\n";
    consumeError(std::move(Err));
    return 1;
  }

If there's no error then we can get the underlying value.
In this example we will print the AST of the "to" context.

.. code-block:: cpp

  Decl *Imported = *ImportedOrErr;
  Imported->getTranslationUnitDecl()->dump();

Since we set **minimal import** in the constructor of the importer, the AST will not contain the declaration of the members (once we run the test tool).

.. code-block:: bash

  TranslationUnitDecl 0x68b9a8 <<invalid sloc>> <invalid sloc>
  `-CXXRecordDecl 0x6c7e30 <line:2:7, col:13> col:13 class MyClass definition
    `-DefinitionData pass_in_registers standard_layout trivially_copyable trivial literal
      |-DefaultConstructor exists trivial needs_implicit
      |-CopyConstructor simple trivial has_const_param needs_implicit implicit_has_const_param
      |-MoveConstructor exists simple trivial needs_implicit
      |-CopyAssignment trivial has_const_param needs_implicit implicit_has_const_param
      |-MoveAssignment exists simple trivial needs_implicit
      `-Destructor simple irrelevant trivial needs_implicit

We'd like to get the members too, so, we use ``ImportDefinition`` to copy the whole definition of ``MyClass`` into the "to" context.
Then we dump the AST again.

.. code-block:: cpp

  if (llvm::Error Err = Importer.ImportDefinition(From)) {
    llvm::errs() << "ERROR: " << Err << "\n";
    consumeError(std::move(Err));
    return 1;
  }
  llvm::errs() << "Imported definition.\n";
  Imported->getTranslationUnitDecl()->dump();

This time the AST is going to contain the members too.

.. code-block:: bash

  TranslationUnitDecl 0x68b9a8 <<invalid sloc>> <invalid sloc>
  `-CXXRecordDecl 0x6c7e30 <line:2:7, col:13> col:13 class MyClass definition
    |-DefinitionData pass_in_registers standard_layout trivially_copyable trivial literal
    | |-DefaultConstructor exists trivial needs_implicit
    | |-CopyConstructor simple trivial has_const_param needs_implicit implicit_has_const_param
    | |-MoveConstructor exists simple trivial needs_implicit
    | |-CopyAssignment trivial has_const_param needs_implicit implicit_has_const_param
    | |-MoveAssignment exists simple trivial needs_implicit
    | `-Destructor simple irrelevant trivial needs_implicit
    |-CXXRecordDecl 0x6c7f48 <col:7, col:13> col:13 implicit class MyClass
    |-FieldDecl 0x6c7ff0 <line:3:9, col:13> col:13 m1 'int'
    `-FieldDecl 0x6c8058 <line:4:9, col:13> col:13 m2 'int'

We can spare the call for ``ImportDefinition`` if we set up the importer to do a "normal" (not minimal) import.

.. code-block:: cpp

  ASTImporter Importer( ....  /*MinimalImport=*/false);

With **normal import**, all dependent declarations are imported normally.
However, with minimal import, the dependent Decls are imported without definition, and we have to import their definition for each if we later need that.

Putting this all together here is how the source of the tool looks like:

.. code-block:: cpp

  #include "clang/AST/ASTImporter.h"
  #include "clang/ASTMatchers/ASTMatchFinder.h"
  #include "clang/ASTMatchers/ASTMatchers.h"
  #include "clang/Tooling/Tooling.h"

  using namespace clang;
  using namespace tooling;
  using namespace ast_matchers;

  template <typename Node, typename Matcher>
  Node *getFirstDecl(Matcher M, const std::unique_ptr<ASTUnit> &Unit) {
    auto MB = M.bind("bindStr"); // Bind the to-be-matched node to a string key.
    auto MatchRes = match(MB, Unit->getASTContext());
    // We should have at least one match.
    assert(MatchRes.size() >= 1);
    // Get the first matched and bound node.
    Node *Result =
        const_cast<Node *>(MatchRes[0].template getNodeAs<Node>("bindStr"));
    assert(Result);
    return Result;
  }

  int main() {
    std::unique_ptr<ASTUnit> ToUnit = buildASTFromCode(
        "", "to.cc");
    std::unique_ptr<ASTUnit> FromUnit = buildASTFromCode(
        R"(
        class MyClass {
          int m1;
          int m2;
        };
        )",
        "from.cc");
    auto Matcher = cxxRecordDecl(hasName("MyClass"));
    auto *From = getFirstDecl<CXXRecordDecl>(Matcher, FromUnit);

    ASTImporter Importer(ToUnit->getASTContext(), ToUnit->getFileManager(),
                         FromUnit->getASTContext(), FromUnit->getFileManager(),
                         /*MinimalImport=*/true);
    llvm::Expected<Decl *> ImportedOrErr = Importer.Import(From);
    if (!ImportedOrErr) {
      llvm::Error Err = ImportedOrErr.takeError();
      llvm::errs() << "ERROR: " << Err << "\n";
      consumeError(std::move(Err));
      return 1;
    }
    Decl *Imported = *ImportedOrErr;
    Imported->getTranslationUnitDecl()->dump();

    if (llvm::Error Err = Importer.ImportDefinition(From)) {
      llvm::errs() << "ERROR: " << Err << "\n";
      consumeError(std::move(Err));
      return 1;
    }
    llvm::errs() << "Imported definition.\n";
    Imported->getTranslationUnitDecl()->dump();

    return 0;
  };

We may extend the ``CMakeLists.txt`` under let's say ``clang/tools`` with the build and link instructions:

.. code-block:: bash

  add_clang_executable(astimporter-demo ASTImporterDemo.cpp)
  clang_target_link_libraries(astimporter-demo
    PRIVATE
    LLVMSupport
    clangAST
    clangASTMatchers
    clangBasic
    clangFrontend
    clangSerialization
    clangTooling
    )

Then we can build and execute the new tool.

.. code-block:: bash

  $ ninja astimporter-demo && ./bin/astimporter-demo

Errors during the import process
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Normally, either the source or the destination context contains the definition of a declaration.
However, there may be cases when both of the contexts have a definition for a given symbol.
If these definitions differ, then we have a name conflict, in C++ it is known as ODR (one definition rule) violation.
Let's modify the previous tool we had written and try to import a ``ClassTemplateSpecializationDecl`` with a conflicting definition:

.. code-block:: cpp

  int main() {
    std::unique_ptr<ASTUnit> ToUnit = buildASTFromCode(
        R"(
        // primary template
        template <typename T>
        struct X {};
        // explicit specialization
        template<>
        struct X<int> { int i; };
        )",
        "to.cc");
    ToUnit->enableSourceFileDiagnostics();
    std::unique_ptr<ASTUnit> FromUnit = buildASTFromCode(
        R"(
        // primary template
        template <typename T>
        struct X {};
        // explicit specialization
        template<>
        struct X<int> { int i2; };
        // field mismatch:  ^^
        )",
        "from.cc");
    FromUnit->enableSourceFileDiagnostics();
    auto Matcher = classTemplateSpecializationDecl(hasName("X"));
    auto *From = getFirstDecl<ClassTemplateSpecializationDecl>(Matcher, FromUnit);
    auto *To = getFirstDecl<ClassTemplateSpecializationDecl>(Matcher, ToUnit);

    ASTImporter Importer(ToUnit->getASTContext(), ToUnit->getFileManager(),
                         FromUnit->getASTContext(), FromUnit->getFileManager(),
                         /*MinimalImport=*/false);
    llvm::Expected<Decl *> ImportedOrErr = Importer.Import(From);
    if (!ImportedOrErr) {
      llvm::Error Err = ImportedOrErr.takeError();
      llvm::errs() << "ERROR: " << Err << "\n";
      consumeError(std::move(Err));
      To->getTranslationUnitDecl()->dump();
      return 1;
    }
    return 0;
  };

When we run the tool we have the following warning:

.. code-block:: bash

  to.cc:7:14: warning: type 'X<int>' has incompatible definitions in different translation units [-Wodr]
        struct X<int> { int i; };
               ^
  to.cc:7:27: note: field has name 'i' here
        struct X<int> { int i; };
                            ^
  from.cc:7:27: note: field has name 'i2' here
        struct X<int> { int i2; };
                          ^

Note, because of these diagnostics we had to call ``enableSourceFileDiagnostics`` on the ``ASTUnit`` objects.

Since we could not import the specified declaration (``From``), we get an error in the return value.
The AST does not contain the conflicting definition, so we are left with the original AST.

.. code-block:: bash

  ERROR: NameConflict
  TranslationUnitDecl 0xe54a48 <<invalid sloc>> <invalid sloc>
  |-ClassTemplateDecl 0xe91020 <to.cc:3:7, line:4:17> col:14 X
  | |-TemplateTypeParmDecl 0xe90ed0 <line:3:17, col:26> col:26 typename depth 0 index 0 T
  | |-CXXRecordDecl 0xe90f90 <line:4:7, col:17> col:14 struct X definition
  | | |-DefinitionData empty aggregate standard_layout trivially_copyable pod trivial literal has_constexpr_non_copy_move_ctor can_const_default_init
  | | | |-DefaultConstructor exists trivial constexpr needs_implicit defaulted_is_constexpr
  | | | |-CopyConstructor simple trivial has_const_param needs_implicit implicit_has_const_param
  | | | |-MoveConstructor exists simple trivial needs_implicit
  | | | |-CopyAssignment trivial has_const_param needs_implicit implicit_has_const_param
  | | | |-MoveAssignment exists simple trivial needs_implicit
  | | | `-Destructor simple irrelevant trivial needs_implicit
  | | `-CXXRecordDecl 0xe91270 <col:7, col:14> col:14 implicit struct X
  | `-ClassTemplateSpecialization 0xe91340 'X'
  `-ClassTemplateSpecializationDecl 0xe91340 <line:6:7, line:7:30> col:14 struct X definition
    |-DefinitionData pass_in_registers aggregate standard_layout trivially_copyable pod trivial literal
    | |-DefaultConstructor exists trivial needs_implicit
    | |-CopyConstructor simple trivial has_const_param needs_implicit implicit_has_const_param
    | |-MoveConstructor exists simple trivial needs_implicit
    | |-CopyAssignment trivial has_const_param needs_implicit implicit_has_const_param
    | |-MoveAssignment exists simple trivial needs_implicit
    | `-Destructor simple irrelevant trivial needs_implicit
    |-TemplateArgument type 'int'
    |-CXXRecordDecl 0xe91558 <col:7, col:14> col:14 implicit struct X
    `-FieldDecl 0xe91600 <col:23, col:27> col:27 i 'int'

Error propagation
"""""""""""""""""

If there is a dependent node we have to import before we could import a given node then the import error associated to the dependency propagates to the dependant node.
Let's modify the previous example and import a ``FieldDecl`` instead of the ``ClassTemplateSpecializationDecl``.

.. code-block:: cpp

  auto Matcher = fieldDecl(hasName("i2"));
  auto *From = getFirstDecl<FieldDecl>(Matcher, FromUnit);

In this case we can see that an error is associated (``getImportDeclErrorIfAny``) to the specialization also, not just to the field:

.. code-block:: cpp

  llvm::Expected<Decl *> ImportedOrErr = Importer.Import(From);
  if (!ImportedOrErr) {
    llvm::Error Err = ImportedOrErr.takeError();
    consumeError(std::move(Err));

    // check that the ClassTemplateSpecializationDecl is also marked as
    // erroneous.
    auto *FromSpec = getFirstDecl<ClassTemplateSpecializationDecl>(
        classTemplateSpecializationDecl(hasName("X")), FromUnit);
    assert(Importer.getImportDeclErrorIfAny(FromSpec));
    // Btw, the error is also set for the FieldDecl.
    assert(Importer.getImportDeclErrorIfAny(From));
    return 1;
  }

Polluted AST
""""""""""""

We may recognize an error during the import of a dependent node. However, by that time, we had already created the dependant.
In these cases we do not remove the existing erroneous node from the "to" context, rather we associate an error to that node.
Let's extend the previous example with another class ``Y``.
This class has a forward definition in the "to" context, but its definition is in the "from" context.
We'd like to import the definition, but it contains a member whose type conflicts with the type in the "to" context:

.. code-block:: cpp

  std::unique_ptr<ASTUnit> ToUnit = buildASTFromCode(
      R"(
      // primary template
      template <typename T>
      struct X {};
      // explicit specialization
      template<>
      struct X<int> { int i; };

      class Y;
      )",
      "to.cc");
  ToUnit->enableSourceFileDiagnostics();
  std::unique_ptr<ASTUnit> FromUnit = buildASTFromCode(
      R"(
      // primary template
      template <typename T>
      struct X {};
      // explicit specialization
      template<>
      struct X<int> { int i2; };
      // field mismatch:  ^^

      class Y { void f() { X<int> xi; } };
      )",
      "from.cc");
  FromUnit->enableSourceFileDiagnostics();
  auto Matcher = cxxRecordDecl(hasName("Y"));
  auto *From = getFirstDecl<CXXRecordDecl>(Matcher, FromUnit);
  auto *To = getFirstDecl<CXXRecordDecl>(Matcher, ToUnit);

This time we create a shared_ptr for ``ASTImporterSharedState`` which owns the associated errors for the "to" context.
Note, there may be several different ASTImporter objects which import into the same "to" context but from different "from" contexts; they should share the same ``ASTImporterSharedState``.
(Also note, we have to include the corresponding ``ASTImporterSharedState.h`` header file.)

.. code-block:: cpp

  auto ImporterState = std::make_shared<ASTImporterSharedState>();
  ASTImporter Importer(ToUnit->getASTContext(), ToUnit->getFileManager(),
                       FromUnit->getASTContext(), FromUnit->getFileManager(),
                       /*MinimalImport=*/false, ImporterState);
  llvm::Expected<Decl *> ImportedOrErr = Importer.Import(From);
  if (!ImportedOrErr) {
    llvm::Error Err = ImportedOrErr.takeError();
    consumeError(std::move(Err));

    // ... but the node had been created.
    auto *ToYDef = getFirstDecl<CXXRecordDecl>(
        cxxRecordDecl(hasName("Y"), isDefinition()), ToUnit);
    ToYDef->dump();
    // An error is set for "ToYDef" in the shared state.
    Optional<ASTImportError> OptErr =
        ImporterState->getImportDeclErrorIfAny(ToYDef);
    assert(OptErr);

    return 1;
  }

If we take a look at the AST, then we can see that the Decl with the definition is created, but the field is missing.

.. code-block:: bash

  |-CXXRecordDecl 0xf66678 <line:9:7, col:13> col:13 class Y
  `-CXXRecordDecl 0xf66730 prev 0xf66678 <:10:7, col:13> col:13 class Y definition
    |-DefinitionData pass_in_registers empty aggregate standard_layout trivially_copyable pod trivial literal has_constexpr_non_copy_move_ctor can_const_default_init
    | |-DefaultConstructor exists trivial constexpr needs_implicit defaulted_is_constexpr
    | |-CopyConstructor simple trivial has_const_param needs_implicit implicit_has_const_param
    | |-MoveConstructor exists simple trivial needs_implicit
    | |-CopyAssignment trivial has_const_param needs_implicit implicit_has_const_param
    | |-MoveAssignment exists simple trivial needs_implicit
    | `-Destructor simple irrelevant trivial needs_implicit
    `-CXXRecordDecl 0xf66828 <col:7, col:13> col:13 implicit class Y

We do not remove the erroneous nodes because by the time when we recognize the error it is too late to remove the node, there may be additional references to that already in the AST.
This is aligned with the overall `design principle of the Clang AST <InternalsManual.html#immutability>`_: Clang AST nodes (types, declarations, statements, expressions, and so on) are generally designed to be **immutable once created**.
Thus, clients of the ASTImporter library should always check if there is any associated error for the node which they inspect in the destination context.
We recommend skipping the processing of those nodes which have an error associated with them.

Using the ``-ast-merge`` Clang front-end action
-----------------------------------------------

The ``-ast-merge <pch-file>`` command-line switch can be used to merge from the given serialized AST file.
This file represents the source context.
When this switch is present then each top-level AST node of the source context is being merged into the destination context.
If the merge was successful then ``ASTConsumer::HandleTopLevelDecl`` is called for the Decl.
This results that we can execute the original front-end action on the extended AST.

Example for C
^^^^^^^^^^^^^

Let's consider the following three files:

.. code-block:: c

  // bar.h
  #ifndef BAR_H
  #define BAR_H
  int bar();
  #endif /* BAR_H */

  // bar.c
  #include "bar.h"
  int bar() {
    return 41;
  }

  // main.c
  #include "bar.h"
  int main() {
      return bar();
  }

Let's generate the AST files for the two source files:

.. code-block:: bash

  $ clang -cc1 -emit-pch -o bar.ast bar.c
  $ clang -cc1 -emit-pch -o main.ast main.c

Then, let's check how the merged AST would look like if we consider only the ``bar()`` function:

.. code-block:: bash

  $ clang -cc1 -ast-merge bar.ast -ast-merge main.ast /dev/null -ast-dump
  TranslationUnitDecl 0x12b0738 <<invalid sloc>> <invalid sloc>
  |-FunctionDecl 0x12b1470 </path/bar.h:4:1, col:9> col:5 used bar 'int ()'
  |-FunctionDecl 0x12b1538 prev 0x12b1470 </path/bar.c:3:1, line:5:1> line:3:5 used bar 'int ()'
  | `-CompoundStmt 0x12b1608 <col:11, line:5:1>
  |   `-ReturnStmt 0x12b15f8 <line:4:3, col:10>
  |     `-IntegerLiteral 0x12b15d8 <col:10> 'int' 41
  |-FunctionDecl 0x12b1648 prev 0x12b1538 </path/bar.h:4:1, col:9> col:5 used bar 'int ()'

We can inspect that the prototype of the function and the definition of it is merged into the same redeclaration chain.
What's more there is a third prototype declaration merged to the chain.
The functions are merged in a way that prototypes are added to the redecl chain if they refer to the same type, but we can have only one definition.
The first two declarations are from ``bar.ast``, the third is from ``main.ast``.

Now, let's create an object file from the merged AST:

.. code-block:: bash

  $ clang -cc1 -ast-merge bar.ast -ast-merge main.ast /dev/null -emit-obj -o main.o

Next, we may call the linker and execute the created binary file.

.. code-block:: bash

  $ clang -o a.out main.o
  $ ./a.out
  $ echo $?
  41
  $

Example for C++
^^^^^^^^^^^^^^^

In the case of C++, the generation of the AST files and the way how we invoke the front-end is a bit different.
Assuming we have these three files:

.. code-block:: cpp

  // foo.h
  #ifndef FOO_H
  #define FOO_H
  struct foo {
      virtual int fun();
  };
  #endif /* FOO_H */

  // foo.cpp
  #include "foo.h"
  int foo::fun() {
    return 42;
  }

  // main.cpp
  #include "foo.h"
  int main() {
      return foo().fun();
  }

We shall generate the AST files, merge them, create the executable and then run it:

.. code-block:: bash

  $ clang++ -x c++-header -o foo.ast foo.cpp
  $ clang++ -x c++-header -o main.ast main.cpp
  $ clang++ -cc1 -x c++ -ast-merge foo.ast -ast-merge main.ast /dev/null -ast-dump
  $ clang++ -cc1 -x c++ -ast-merge foo.ast -ast-merge main.ast /dev/null -emit-obj -o main.o
  $ clang++ -o a.out main.o
  $ ./a.out
  $ echo $?
  42
  $
