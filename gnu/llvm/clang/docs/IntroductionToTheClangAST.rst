=============================
Introduction to the Clang AST
=============================

This document gives a gentle introduction to the mysteries of the Clang
AST. It is targeted at developers who either want to contribute to
Clang, or use tools that work based on Clang's AST, like the AST
matchers.

.. raw:: html

  <center><iframe width="560" height="315" src="https://www.youtube.com/embed/VqCkCDFLSsc?vq=hd720" frameborder="0" allowfullscreen></iframe></center>

`Slides <https://llvm.org/devmtg/2013-04/klimek-slides.pdf>`_

Introduction
============

Clang's AST is different from ASTs produced by some other compilers in
that it closely resembles both the written C++ code and the C++
standard. For example, parenthesis expressions and compile time
constants are available in an unreduced form in the AST. This makes
Clang's AST a good fit for refactoring tools.

Documentation for all Clang AST nodes is available via the generated
`Doxygen <https://clang.llvm.org/doxygen>`_. The doxygen online
documentation is also indexed by your favorite search engine, which will
make a search for clang and the AST node's class name usually turn up
the doxygen of the class you're looking for (for example, search for:
clang ParenExpr).

Examining the AST
=================

A good way to familiarize yourself with the Clang AST is to actually look
at it on some simple example code. Clang has a builtin AST-dump mode,
which can be enabled with the flag ``-ast-dump``.

Let's look at a simple example AST:

::

    $ cat test.cc
    int f(int x) {
      int result = (x / 42);
      return result;
    }

    # Clang by default is a frontend for many tools; -Xclang is used to pass
    # options directly to the C++ frontend.
    $ clang -Xclang -ast-dump -fsyntax-only test.cc
    TranslationUnitDecl 0x5aea0d0 <<invalid sloc>>
    ... cutting out internal declarations of clang ...
    `-FunctionDecl 0x5aeab50 <test.cc:1:1, line:4:1> f 'int (int)'
      |-ParmVarDecl 0x5aeaa90 <line:1:7, col:11> x 'int'
      `-CompoundStmt 0x5aead88 <col:14, line:4:1>
        |-DeclStmt 0x5aead10 <line:2:3, col:24>
        | `-VarDecl 0x5aeac10 <col:3, col:23> result 'int'
        |   `-ParenExpr 0x5aeacf0 <col:16, col:23> 'int'
        |     `-BinaryOperator 0x5aeacc8 <col:17, col:21> 'int' '/'
        |       |-ImplicitCastExpr 0x5aeacb0 <col:17> 'int' <LValueToRValue>
        |       | `-DeclRefExpr 0x5aeac68 <col:17> 'int' lvalue ParmVar 0x5aeaa90 'x' 'int'
        |       `-IntegerLiteral 0x5aeac90 <col:21> 'int' 42
        `-ReturnStmt 0x5aead68 <line:3:3, col:10>
          `-ImplicitCastExpr 0x5aead50 <col:10> 'int' <LValueToRValue>
            `-DeclRefExpr 0x5aead28 <col:10> 'int' lvalue Var 0x5aeac10 'result' 'int'

The toplevel declaration in
a translation unit is always the `translation unit
declaration <https://clang.llvm.org/doxygen/classclang_1_1TranslationUnitDecl.html>`_.
In this example, our first user written declaration is the `function
declaration <https://clang.llvm.org/doxygen/classclang_1_1FunctionDecl.html>`_
of "``f``". The body of "``f``" is a `compound
statement <https://clang.llvm.org/doxygen/classclang_1_1CompoundStmt.html>`_,
whose child nodes are a `declaration
statement <https://clang.llvm.org/doxygen/classclang_1_1DeclStmt.html>`_
that declares our result variable, and the `return
statement <https://clang.llvm.org/doxygen/classclang_1_1ReturnStmt.html>`_.

AST Context
===========

All information about the AST for a translation unit is bundled up in
the class
`ASTContext <https://clang.llvm.org/doxygen/classclang_1_1ASTContext.html>`_.
It allows traversal of the whole translation unit starting from
`getTranslationUnitDecl <https://clang.llvm.org/doxygen/classclang_1_1ASTContext.html#abd909fb01ef10cfd0244832a67b1dd64>`_,
or to access Clang's `table of
identifiers <https://clang.llvm.org/doxygen/classclang_1_1ASTContext.html#a4f95adb9958e22fbe55212ae6482feb4>`_
for the parsed translation unit.

AST Nodes
=========

Clang's AST nodes are modeled on a class hierarchy that does not have a
common ancestor. Instead, there are multiple larger hierarchies for
basic node types like
`Decl <https://clang.llvm.org/doxygen/classclang_1_1Decl.html>`_ and
`Stmt <https://clang.llvm.org/doxygen/classclang_1_1Stmt.html>`_. Many
important AST nodes derive from
`Type <https://clang.llvm.org/doxygen/classclang_1_1Type.html>`_,
`Decl <https://clang.llvm.org/doxygen/classclang_1_1Decl.html>`_,
`DeclContext <https://clang.llvm.org/doxygen/classclang_1_1DeclContext.html>`_
or `Stmt <https://clang.llvm.org/doxygen/classclang_1_1Stmt.html>`_, with
some classes deriving from both Decl and DeclContext.

There are also a multitude of nodes in the AST that are not part of a
larger hierarchy, and are only reachable from specific other nodes, like
`CXXBaseSpecifier <https://clang.llvm.org/doxygen/classclang_1_1CXXBaseSpecifier.html>`_.

Thus, to traverse the full AST, one starts from the
`TranslationUnitDecl <https://clang.llvm.org/doxygen/classclang_1_1TranslationUnitDecl.html>`_
and then recursively traverses everything that can be reached from that
node - this information has to be encoded for each specific node type.
This algorithm is encoded in the
`RecursiveASTVisitor <https://clang.llvm.org/doxygen/classclang_1_1RecursiveASTVisitor.html>`_.
See the `RecursiveASTVisitor
tutorial <https://clang.llvm.org/docs/RAVFrontendAction.html>`_.

The two most basic nodes in the Clang AST are statements
(`Stmt <https://clang.llvm.org/doxygen/classclang_1_1Stmt.html>`_) and
declarations
(`Decl <https://clang.llvm.org/doxygen/classclang_1_1Decl.html>`_). Note
that expressions
(`Expr <https://clang.llvm.org/doxygen/classclang_1_1Expr.html>`_) are
also statements in Clang's AST.
