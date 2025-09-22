===========================
Sanitizer special case list
===========================

.. contents::
   :local:

Introduction
============

This document describes the way to disable or alter the behavior of
sanitizer tools for certain source-level entities by providing a special
file at compile-time.

Goal and usage
==============

Users of sanitizer tools, such as :doc:`AddressSanitizer`, :doc:`ThreadSanitizer`
or :doc:`MemorySanitizer` may want to disable or alter some checks for
certain source-level entities to:

* speedup hot function, which is known to be correct;
* ignore a function that does some low-level magic (e.g. walks through the
  thread stack, bypassing the frame boundaries);
* ignore a known problem.

To achieve this, user may create a file listing the entities they want to
ignore, and pass it to clang at compile-time using
``-fsanitize-ignorelist`` flag. See :doc:`UsersManual` for details.

Example
=======

.. code-block:: bash

  $ cat foo.c
  #include <stdlib.h>
  void bad_foo() {
    int *a = (int*)malloc(40);
    a[10] = 1;
  }
  int main() { bad_foo(); }
  $ cat ignorelist.txt
  # Ignore reports from bad_foo function.
  fun:bad_foo
  $ clang -fsanitize=address foo.c ; ./a.out
  # AddressSanitizer prints an error report.
  $ clang -fsanitize=address -fsanitize-ignorelist=ignorelist.txt foo.c ; ./a.out
  # No error report here.

Format
======

Ignorelists consist of entries, optionally grouped into sections. Empty lines
and lines starting with "#" are ignored.

.. note::

  Prior to Clang 18, section names and entries described below use a variant of
  regex where ``*`` is translated to ``.*``. Clang 18 (`D154014
  <https://reviews.llvm.org/D154014>`) switches to glob and plans to remove
  regex support in Clang 19.

  For Clang 18, regex is supported if ``#!special-case-list-v1`` is the first
  line of the file.

  Many special case lists use ``.`` to indicate the literal character and do
  not use regex metacharacters such as ``(``, ``)``. They are unaffected by the
  regex to glob transition. For more details, see `this discourse post
  <https://discourse.llvm.org/t/use-glob-instead-of-regex-for-specialcaselists/71666>`_.

Section names are globs written in square brackets that denote
which sanitizer the following entries apply to. For example, ``[address]``
specifies AddressSanitizer while ``[{cfi-vcall,cfi-icall}]`` specifies Control
Flow Integrity virtual and indirect call checking. Entries without a section
will be placed under the ``[*]`` section applying to all enabled sanitizers.

Entries contain an entity type, followed by a colon and a glob,
specifying the names of the entities, optionally followed by an equals sign and
a tool-specific category, e.g. ``fun:*ExampleFunc=example_category``.
Two generic entity types are ``src`` and
``fun``, which allow users to specify source files and functions, respectively.
Some sanitizer tools may introduce custom entity types and categories - refer to
tool-specific docs.

.. code-block:: bash

    # The line above is explained in the note above
    # Lines starting with # are ignored.
    # Turn off checks for the source file
    # Entries without sections are placed into [*] and apply to all sanitizers
    src:path/to/source/file.c
    src:*/source/file.c
    # Turn off checks for this main file, including files included by it.
    # Useful when the main file instead of an included file should be ignored.
    mainfile:file.c
    # Turn off checks for a particular functions (use mangled names):
    fun:_Z8MyFooBarv
    # Glob brace expansions and character ranges are supported
    fun:bad_{foo,bar}
    src:bad_source[1-9].c
    # "*" matches zero or more characters
    src:bad/sources/*
    fun:*BadFunction*
    # Specific sanitizer tools may introduce categories.
    src:/special/path/*=special_sources
    # Sections can be used to limit ignorelist entries to specific sanitizers
    [address]
    fun:*BadASanFunc*
    # Section names are globs
    [{cfi-vcall,cfi-icall}]
    fun:*BadCfiCall

``mainfile`` is similar to applying ``-fno-sanitize=`` to a set of files but
does not need plumbing into the build system. This works well for internal
linkage functions but has a caveat for C++ vague linkage functions.

C++ vague linkage functions (e.g. inline functions, template instantiations) are
deduplicated at link time. A function (in an included file) ignored by a
specific ``mainfile`` pattern may not be the prevailing copy picked by the
linker. Therefore, using ``mainfile`` requires caution. It may still be useful,
e.g. when patterns are picked in a way to ensure the prevailing one is ignored.
(There is action-at-a-distance risk.)

``mainfile`` can be useful enabling a ubsan check for a large code base when
finding the direct stack frame triggering the failure for every failure is
difficult.
