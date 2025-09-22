=================================================
Choosing the Right Interface for Your Application
=================================================

Clang provides infrastructure to write tools that need syntactic and semantic
information about a program.  This document will give a short introduction of
the different ways to write clang tools, and their pros and cons.

LibClang
--------

`LibClang <https://clang.llvm.org/doxygen/group__CINDEX.html>`_ is a stable high
level C interface to clang.  When in doubt LibClang is probably the interface
you want to use.  Consider the other interfaces only when you have a good
reason not to use LibClang.

Canonical examples of when to use LibClang:

* Xcode
* Clang Python Bindings

Use LibClang when you...:

* want to interface with clang from other languages than C++
* need a stable interface that takes care to be backwards compatible
* want powerful high-level abstractions, like iterating through an AST with a
  cursor, and don't want to learn all the nitty gritty details of Clang's AST.

Do not use LibClang when you...:

* want full control over the Clang AST

Clang Plugins
-------------

:doc:`Clang Plugins <ClangPlugins>` allow you to run additional actions on the
AST as part of a compilation.  Plugins are dynamic libraries that are loaded at
runtime by the compiler, and they're easy to integrate into your build
environment.

Canonical examples of when to use Clang Plugins:

* special lint-style warnings or errors for your project
* creating additional build artifacts from a single compile step

Use Clang Plugins when you...:

* need your tool to rerun if any of the dependencies change
* want your tool to make or break a build
* need full control over the Clang AST

Do not use Clang Plugins when you...:

* want to run tools outside of your build environment
* want full control on how Clang is set up, including mapping of in-memory
  virtual files
* need to run over a specific subset of files in your project which is not
  necessarily related to any changes which would trigger rebuilds

LibTooling
----------

:doc:`LibTooling <LibTooling>` is a C++ interface aimed at writing standalone
tools, as well as integrating into services that run clang tools.  Canonical
examples of when to use LibTooling:

* a simple syntax checker
* refactoring tools

Use LibTooling when you...:

* want to run tools over a single file, or a specific subset of files,
  independently of the build system
* want full control over the Clang AST
* want to share code with Clang Plugins

Do not use LibTooling when you...:

* want to run as part of the build triggered by dependency changes
* want a stable interface so you don't need to change your code when the AST API
  changes
* want high level abstractions like cursors and code completion out of the box
* do not want to write your tools in C++

:doc:`Clang tools <ClangTools>` are a collection of specific developer tools
built on top of the LibTooling infrastructure as part of the Clang project.
They are targeted at automating and improving core development activities of
C/C++ developers.

Examples of tools we are building or planning as part of the Clang project:

* Syntax checking (:program:`clang-check`)
* Automatic fixing of compile errors (:program:`clang-fixit`)
* Automatic code formatting (:program:`clang-format`)
* Migration tools for new features in new language standards
* Core refactoring tools

