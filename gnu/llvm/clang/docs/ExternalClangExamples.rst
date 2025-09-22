=======================
External Clang Examples
=======================

Introduction
============

This page provides some examples of the kinds of things that people have
done with Clang that might serve as useful guides (or starting points) from
which to develop your own tools. They may be helpful even for something as
banal (but necessary) as how to set up your build to integrate Clang.

Clang's library-based design is deliberately aimed at facilitating use by
external projects, and we are always interested in improving Clang to
better serve our external users. Some typical categories of applications
where Clang is used are:

- Static analysis.
- Documentation/cross-reference generation.

If you know of (or wrote!) a tool or project using Clang, please post on
`the Discourse forums (Clang Frontend category)
<https://discourse.llvm.org/c/clang/6>`_ to have it added.
(or if you are already a Clang contributor, feel free to directly commit
additions). Since the primary purpose of this page is to provide examples
that can help developers, generally they must have code available.

List of projects and tools
==========================

`<https://github.com/Andersbakken/rtags/>`_
   "RTags is a client/server application that indexes c/c++ code and keeps
   a persistent in-memory database of references, symbolnames, completions
   etc."

`<https://rprichard.github.io/CxxCodeBrowser/>`_
   "A C/C++ source code indexer and navigator"

`<https://github.com/etaoins/qconnectlint>`_
   "qconnectlint is a Clang tool for statically verifying the consistency
   of signal and slot connections made with Qt's ``QObject::connect``."

`<https://github.com/woboq/woboq_codebrowser>`_
   "The Woboq Code Browser is a web-based code browser for C/C++ projects.
   Check out `<https://code.woboq.org/>`_ for an example!"

`<https://github.com/mozilla/dxr>`_
    "DXR is a source code cross-reference tool that uses static analysis
    data collected by instrumented compilers."

`<https://github.com/eschulte/clang-mutate>`_
    "This tool performs a number of operations on C-language source files."

`<https://github.com/gmarpons/Crisp>`_
    "A coding rule validation add-on for LLVM/clang. Crisp rules are written
    in Prolog. A high-level declarative DSL to easily write new rules is under
    development. It will be called CRISP, an acronym for *Coding Rules in
    Sugared Prolog*."

`<https://github.com/drothlis/clang-ctags>`_
    "Generate tag file for C++ source code."

`<https://github.com/exclipy/clang_indexer>`_
    "This is an indexer for C and C++ based on the libclang library."

`<https://github.com/holtgrewe/linty>`_
    "Linty - C/C++ Style Checking with Python & libclang."

`<https://github.com/axw/cmonster>`_
    "cmonster is a Python wrapper for the Clang C++ parser."

`<https://github.com/rizsotto/Constantine>`_
    "Constantine is a toy project to learn how to write clang plugin.
    Implements pseudo const analysis. Generates warnings about variables,
    which were declared without const qualifier."

`<https://github.com/jessevdk/cldoc>`_
    "cldoc is a Clang based documentation generator for C and C++.
    cldoc tries to solve the issue of writing C/C++ software documentation
    with a modern, non-intrusive and robust approach."

`<https://github.com/AlexDenisov/ToyClangPlugin>`_
    "The simplest Clang plugin implementing a semantic check for Objective-C.
    This example shows how to use the ``DiagnosticsEngine`` (emit warnings,
    errors, fixit hints).  See also `<http://l.rw.rw/clang_plugin>`_ for
    step-by-step instructions."

`<https://phabricator.kde.org/source/clazy>`_
   "clazy is a compiler plugin which allows clang to understand Qt semantics.
   You get more than 50 Qt related compiler warnings, ranging from unneeded
   memory allocations to misusage of API, including fix-its for automatic
   refactoring."

`<https://gerrit.libreoffice.org/gitweb?p=core.git;a=blob_plain;f=compilerplugins/README;hb=HEAD>`_
   "LibreOffice uses a Clang plugin infrastructure to check during the build
   various things, some more, some less specific to the LibreOffice source code.
   There are currently around 50 such checkers, from flagging C-style casts and
   uses of reserved identifiers to ensuring that code adheres to lifecycle
   protocols for certain LibreOffice-specific classes.  They may serve as
   examples for writing RecursiveASTVisitor-based plugins."
