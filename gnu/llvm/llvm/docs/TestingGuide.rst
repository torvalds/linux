=================================
LLVM Testing Infrastructure Guide
=================================

.. contents::
   :local:

.. toctree::
   :hidden:

   TestSuiteGuide
   TestSuiteMakefileGuide

Overview
========

This document is the reference manual for the LLVM testing
infrastructure. It documents the structure of the LLVM testing
infrastructure, the tools needed to use it, and how to add and run
tests.

Requirements
============

In order to use the LLVM testing infrastructure, you will need all of the
software required to build LLVM, as well as `Python <http://python.org>`_ 3.8 or
later.

LLVM Testing Infrastructure Organization
========================================

The LLVM testing infrastructure contains three major categories of tests:
unit tests, regression tests and whole programs. The unit tests and regression
tests are contained inside the LLVM repository itself under ``llvm/unittests``
and ``llvm/test`` respectively and are expected to always pass -- they should be
run before every commit.

The whole programs tests are referred to as the "LLVM test suite" (or
"test-suite") and are in the ``test-suite``
`repository on GitHub <https://github.com/llvm/llvm-test-suite.git>`_.
For historical reasons, these tests are also referred to as the "nightly
tests" in places, which is less ambiguous than "test-suite" and remains
in use although we run them much more often than nightly.

Unit tests
----------

Unit tests are written using `Google Test <https://github.com/google/googletest/blob/master/docs/primer.md>`_
and `Google Mock <https://github.com/google/googletest/blob/master/docs/gmock_for_dummies.md>`_
and are located in the ``llvm/unittests`` directory.
In general unit tests are reserved for targeting the support library and other
generic data structure, we prefer relying on regression tests for testing
transformations and analysis on the IR.

Regression tests
----------------

The regression tests are small pieces of code that test a specific
feature of LLVM or trigger a specific bug in LLVM. The language they are
written in depends on the part of LLVM being tested. These tests are driven by
the :doc:`Lit <CommandGuide/lit>` testing tool (which is part of LLVM), and
are located in the ``llvm/test`` directory.

Typically when a bug is found in LLVM, a regression test containing just
enough code to reproduce the problem should be written and placed
somewhere underneath this directory. For example, it can be a small
piece of LLVM IR distilled from an actual application or benchmark.

Testing Analysis
----------------

An analysis is a pass that infer properties on some part of the IR and not
transforming it. They are tested in general using the same infrastructure as the
regression tests, by creating a separate "Printer" pass to consume the analysis
result and print it on the standard output in a textual format suitable for
FileCheck.
See `llvm/test/Analysis/BranchProbabilityInfo/loop.ll <https://github.com/llvm/llvm-project/blob/main/llvm/test/Analysis/BranchProbabilityInfo/loop.ll>`_
for an example of such test.

``test-suite``
--------------

The test suite contains whole programs, which are pieces of code which
can be compiled and linked into a stand-alone program that can be
executed. These programs are generally written in high level languages
such as C or C++.

These programs are compiled using a user specified compiler and set of
flags, and then executed to capture the program output and timing
information. The output of these programs is compared to a reference
output to ensure that the program is being compiled correctly.

In addition to compiling and executing programs, whole program tests
serve as a way of benchmarking LLVM performance, both in terms of the
efficiency of the programs generated as well as the speed with which
LLVM compiles, optimizes, and generates code.

The test-suite is located in the ``test-suite``
`repository on GitHub <https://github.com/llvm/llvm-test-suite.git>`_.

See the :doc:`TestSuiteGuide` for details.

Debugging Information tests
---------------------------

The test suite contains tests to check quality of debugging information.
The test are written in C based languages or in LLVM assembly language.

These tests are compiled and run under a debugger. The debugger output
is checked to validate of debugging information. See README.txt in the
test suite for more information. This test suite is located in the
``cross-project-tests/debuginfo-tests`` directory.

Quick start
===========

The tests are located in two separate repositories. The unit and
regression tests are in the main "llvm"/ directory under the directories
``llvm/unittests`` and ``llvm/test`` (so you get these tests for free with the
main LLVM tree). Use ``make check-all`` to run the unit and regression tests
after building LLVM.

The ``test-suite`` module contains more comprehensive tests including whole C
and C++ programs. See the :doc:`TestSuiteGuide` for details.

Unit and Regression tests
-------------------------

To run all of the LLVM unit tests use the check-llvm-unit target:

.. code-block:: bash

    % make check-llvm-unit

To run all of the LLVM regression tests use the check-llvm target:

.. code-block:: bash

    % make check-llvm

In order to get reasonable testing performance, build LLVM and subprojects
in release mode, i.e.

.. code-block:: bash

    % cmake -DCMAKE_BUILD_TYPE="Release" -DLLVM_ENABLE_ASSERTIONS=On

If you have `Clang <https://clang.llvm.org/>`_ checked out and built, you
can run the LLVM and Clang tests simultaneously using:

.. code-block:: bash

    % make check-all

To run the tests with Valgrind (Memcheck by default), use the ``LIT_ARGS`` make
variable to pass the required options to lit. For example, you can use:

.. code-block:: bash

    % make check LIT_ARGS="-v --vg --vg-leak"

to enable testing with valgrind and with leak checking enabled.

To run individual tests or subsets of tests, you can use the ``llvm-lit``
script which is built as part of LLVM. For example, to run the
``Integer/BitPacked.ll`` test by itself you can run:

.. code-block:: bash

    % llvm-lit ~/llvm/test/Integer/BitPacked.ll

or to run all of the ARM CodeGen tests:

.. code-block:: bash

    % llvm-lit ~/llvm/test/CodeGen/ARM

The regression tests will use the Python psutil module only if installed in a
**non-user** location. Under Linux, install with sudo or within a virtual
environment. Under Windows, install Python for all users and then run
``pip install psutil`` in an elevated command prompt.

For more information on using the :program:`lit` tool, see ``llvm-lit --help``
or the :doc:`lit man page <CommandGuide/lit>`.

Debugging Information tests
---------------------------

To run debugging information tests simply add the ``cross-project-tests``
project to your ``LLVM_ENABLE_PROJECTS`` define on the cmake
command-line.

Regression test structure
=========================

The LLVM regression tests are driven by :program:`lit` and are located in the
``llvm/test`` directory.

This directory contains a large array of small tests that exercise
various features of LLVM and to ensure that regressions do not occur.
The directory is broken into several sub-directories, each focused on a
particular area of LLVM.

Writing new regression tests
----------------------------

The regression test structure is very simple, but does require some
information to be set. This information is gathered via ``cmake``
and is written to a file, ``test/lit.site.cfg.py`` in the build directory.
The ``llvm/test`` Makefile does this work for you.

In order for the regression tests to work, each directory of tests must
have a ``lit.local.cfg`` file. :program:`lit` looks for this file to determine
how to run the tests. This file is just Python code and thus is very
flexible, but we've standardized it for the LLVM regression tests. If
you're adding a directory of tests, just copy ``lit.local.cfg`` from
another directory to get running. The standard ``lit.local.cfg`` simply
specifies which files to look in for tests. Any directory that contains
only directories does not need the ``lit.local.cfg`` file. Read the :doc:`Lit
documentation <CommandGuide/lit>` for more information.

Each test file must contain lines starting with "RUN:" that tell :program:`lit`
how to run it. If there are no RUN lines, :program:`lit` will issue an error
while running a test.

RUN lines are specified in the comments of the test program using the
keyword ``RUN`` followed by a colon, and lastly the command (pipeline)
to execute. Together, these lines form the "script" that :program:`lit`
executes to run the test case. The syntax of the RUN lines is similar to a
shell's syntax for pipelines including I/O redirection and variable
substitution. However, even though these lines may *look* like a shell
script, they are not. RUN lines are interpreted by :program:`lit`.
Consequently, the syntax differs from shell in a few ways. You can specify
as many RUN lines as needed.

:program:`lit` performs substitution on each RUN line to replace LLVM tool names
with the full paths to the executable built for each tool (in
``$(LLVM_OBJ_ROOT)/bin``). This ensures that :program:`lit` does
not invoke any stray LLVM tools in the user's path during testing.

Each RUN line is executed on its own, distinct from other lines unless
its last character is ``\``. This continuation character causes the RUN
line to be concatenated with the next one. In this way you can build up
long pipelines of commands without making huge line lengths. The lines
ending in ``\`` are concatenated until a RUN line that doesn't end in
``\`` is found. This concatenated set of RUN lines then constitutes one
execution. :program:`lit` will substitute variables and arrange for the pipeline
to be executed. If any process in the pipeline fails, the entire line (and
test case) fails too.

Below is an example of legal RUN lines in a ``.ll`` file:

.. code-block:: llvm

    ; RUN: llvm-as < %s | llvm-dis > %t1
    ; RUN: llvm-dis < %s.bc-13 > %t2
    ; RUN: diff %t1 %t2

As with a Unix shell, the RUN lines permit pipelines and I/O
redirection to be used.

There are some quoting rules that you must pay attention to when writing
your RUN lines. In general nothing needs to be quoted. :program:`lit` won't
strip off any quote characters so they will get passed to the invoked program.
To avoid this use curly braces to tell :program:`lit` that it should treat
everything enclosed as one value.

In general, you should strive to keep your RUN lines as simple as possible,
using them only to run tools that generate textual output you can then examine.
The recommended way to examine output to figure out if the test passes is using
the :doc:`FileCheck tool <CommandGuide/FileCheck>`. *[The usage of grep in RUN
lines is deprecated - please do not send or commit patches that use it.]*

Put related tests into a single file rather than having a separate file per
test. Check if there are files already covering your feature and consider
adding your code there instead of creating a new file.

Generating assertions in regression tests
-----------------------------------------

Some regression test cases are very large and complex to write/update by hand.
In that case to reduce the human work we can use the scripts available in
llvm/utils/ to generate the assertions.

For example to generate assertions in an :program:`llc`-based test, after
adding one or more RUN lines use:

 .. code-block:: bash

     % llvm/utils/update_llc_test_checks.py --llc-binary build/bin/llc test.ll

This will generate FileCheck assertions, and insert a ``NOTE:`` line at the
top to indicate that assertions were automatically generated.

If you want to update assertions in an existing test case, pass the `-u` option
which first checks the ``NOTE:`` line exists and matches the script name.

Sometimes a test absolutely depends on hand-written assertions and should not
have assertions automatically generated. In that case, add the text ``NOTE: Do
not autogenerate`` to the first line, and the scripts will skip that test. It
is a good idea to explain why generated assertions will not work for the test
so future developers will understand what is going on.

These are the most common scripts and their purposes/applications in generating
assertions:

.. code-block:: none

  update_analyze_test_checks.py
  opt -passes='print<cost-model>'

  update_cc_test_checks.py
  C/C++, or clang/clang++ (IR checks)

  update_llc_test_checks.py
  llc (assembly checks)

  update_mca_test_checks.py
  llvm-mca

  update_mir_test_checks.py
  llc (MIR checks)

  update_test_checks.py
  opt

Precommit workflow for tests
----------------------------

If the test does not crash, assert, or infinite loop, commit the test with
baseline check-lines first. That is, the test will show a miscompile or
missing optimization. Add a "TODO" or "FIXME" comment to indicate that
something is expected to change in a test.

A follow-up patch with code changes to the compiler will then show check-line
differences to the tests, so it is easier to see the effect of the patch.
Remove TODO/FIXME comments added in the previous step if a problem is solved.

Baseline tests (no-functional-change or NFC patch) may be pushed to main
without pre-commit review if you have commit access.

Best practices for regression tests
-----------------------------------

- Use auto-generated check lines (produced by the scripts mentioned above)
  whenever feasible.
- Include comments about what is tested/expected in a particular test. If there
  are relevant issues in the bug tracker, add references to those bug reports
  (for example, "See PR999 for more details").
- Avoid undefined behavior and poison/undef values unless necessary. For
  example, do not use patterns like ``br i1 undef``, which are likely to break
  as a result of future optimizations.
- Minimize tests by removing unnecessary instructions, metadata, attributes,
  etc. Tools like ``llvm-reduce`` can help automate this.
- Outside PhaseOrdering tests, only run a minimal set of passes. For example,
  prefer ``opt -S -passes=instcombine`` over ``opt -S -O3``.
- Avoid unnamed instructions/blocks (such as ``%0`` or ``1:``), because they may
  require renumbering on future test modifications. These can be removed by
  running the test through ``opt -S -passes=instnamer``.
- Try to give values (including variables, blocks and functions) meaningful
  names, and avoid retaining complex names generated by the optimization
  pipeline (such as ``%foo.0.0.0.0.0.0``).

Extra files
-----------

If your test requires extra files besides the file containing the ``RUN:`` lines
and the extra files are small, consider specifying them in the same file and
using ``split-file`` to extract them. For example,

.. code-block:: llvm

  ; RUN: split-file %s %t
  ; RUN: llvm-link -S %t/a.ll %t/b.ll | FileCheck %s

  ; CHECK: ...

  ;--- a.ll
  ...
  ;--- b.ll
  ...

The parts are separated by the regex ``^(.|//)--- <part>``.

If you want to test relative line numbers like ``[[#@LINE+1]]``, specify
``--leading-lines`` to add leading empty lines to preserve line numbers.

If the extra files are large, the idiomatic place to put them is in a subdirectory ``Inputs``.
You can then refer to the extra files as ``%S/Inputs/foo.bar``.

For example, consider ``test/Linker/ident.ll``. The directory structure is
as follows::

  test/
    Linker/
      ident.ll
      Inputs/
        ident.a.ll
        ident.b.ll

For convenience, these are the contents:

.. code-block:: llvm

  ;;;;; ident.ll:

  ; RUN: llvm-link %S/Inputs/ident.a.ll %S/Inputs/ident.b.ll -S | FileCheck %s

  ; Verify that multiple input llvm.ident metadata are linked together.

  ; CHECK-DAG: !llvm.ident = !{!0, !1, !2}
  ; CHECK-DAG: "Compiler V1"
  ; CHECK-DAG: "Compiler V2"
  ; CHECK-DAG: "Compiler V3"

  ;;;;; Inputs/ident.a.ll:

  !llvm.ident = !{!0, !1}
  !0 = metadata !{metadata !"Compiler V1"}
  !1 = metadata !{metadata !"Compiler V2"}

  ;;;;; Inputs/ident.b.ll:

  !llvm.ident = !{!0}
  !0 = metadata !{metadata !"Compiler V3"}

For symmetry reasons, ``ident.ll`` is just a dummy file that doesn't
actually participate in the test besides holding the ``RUN:`` lines.

.. note::

  Some existing tests use ``RUN: true`` in extra files instead of just
  putting the extra files in an ``Inputs/`` directory. This pattern is
  deprecated.

Elaborated tests
----------------

Generally, IR and assembly test files benefit from being cleaned to remove
unnecessary details. However, for tests requiring elaborate IR or assembly
files where cleanup is less practical (e.g., large amount of debug information
output from Clang), you can include generation instructions within
``split-file`` part called ``gen``. Then, run
``llvm/utils/update_test_body.py`` on the test file to generate the needed
content.

.. code-block:: none

    ; RUN: rm -rf %t && split-file %s %t && cd %t
    ; RUN: opt -S a.ll ... | FileCheck %s

    ; CHECK: hello

    ;--- a.cc
    int va;
    ;--- gen
    clang --target=x86_64-linux -S -emit-llvm -g a.cc -o -

    ;--- a.ll
    # content generated by the script 'gen'

.. code-block:: bash

   PATH=/path/to/clang_build/bin:$PATH llvm/utils/update_test_body.py path/to/test.ll

The script will prepare extra files with ``split-file``, invoke ``gen``, and
then rewrite the part after ``gen`` with its stdout.

For convenience, if the test needs one single assembly file, you can also wrap
``gen`` and its required files with ``.ifdef`` and ``.endif``. Then you can
skip ``split-file`` in RUN lines.

.. code-block:: none

    # RUN: llvm-mc -filetype=obj -triple=x86_64 %s -o a.o
    # RUN: ... | FileCheck %s

    # CHECK: hello

    .ifdef GEN
    #--- a.cc
    int va;
    #--- gen
    clang --target=x86_64-linux -S -g a.cc -o -
    .endif
    # content generated by the script 'gen'

.. note::

  Consider specifying an explicit target triple to avoid differences when
  regeneration is needed on another machine.

  ``gen`` is invoked with ``PWD`` set to ``/proc/self/cwd``. Clang commands
  don't need ``-fdebug-compilation-dir=`` since its default value is ``PWD``.

  Check prefixes should be placed before ``.endif`` since the part after
  ``.endif`` is replaced.

If the test body contains multiple files, you can print ``---`` separators and
utilize ``split-file`` in ``RUN`` lines.

.. code-block:: none

    # RUN: rm -rf %t && split-file %s %t && cd %t
    ...

    #--- a.cc
    int va;
    #--- b.cc
    int vb;
    #--- gen
    clang --target=x86_64-linux -S -O1 -g a.cc -o -
    echo '#--- b.s'
    clang --target=x86_64-linux -S -O1 -g b.cc -o -
    #--- a.s

Fragile tests
-------------

It is easy to write a fragile test that would fail spuriously if the tool being
tested outputs a full path to the input file.  For example, :program:`opt` by
default outputs a ``ModuleID``:

.. code-block:: console

  $ cat example.ll
  define i32 @main() nounwind {
      ret i32 0
  }

  $ opt -S /path/to/example.ll
  ; ModuleID = '/path/to/example.ll'

  define i32 @main() nounwind {
      ret i32 0
  }

``ModuleID`` can unexpectedly match against ``CHECK`` lines.  For example:

.. code-block:: llvm

  ; RUN: opt -S %s | FileCheck

  define i32 @main() nounwind {
      ; CHECK-NOT: load
      ret i32 0
  }

This test will fail if placed into a ``download`` directory.

To make your tests robust, always use ``opt ... < %s`` in the RUN line.
:program:`opt` does not output a ``ModuleID`` when input comes from stdin.

Platform-Specific Tests
-----------------------

Whenever adding tests that require the knowledge of a specific platform,
either related to code generated, specific output or back-end features,
you must make sure to isolate the features, so that buildbots that
run on different architectures (and don't even compile all back-ends),
don't fail.

The first problem is to check for target-specific output, for example sizes
of structures, paths and architecture names, for example:

* Tests containing Windows paths will fail on Linux and vice-versa.
* Tests that check for ``x86_64`` somewhere in the text will fail anywhere else.
* Tests where the debug information calculates the size of types and structures.

Also, if the test rely on any behaviour that is coded in any back-end, it must
go in its own directory. So, for instance, code generator tests for ARM go
into ``test/CodeGen/ARM`` and so on. Those directories contain a special
``lit`` configuration file that ensure all tests in that directory will
only run if a specific back-end is compiled and available.

For instance, on ``test/CodeGen/ARM``, the ``lit.local.cfg`` is:

.. code-block:: python

  config.suffixes = ['.ll', '.c', '.cpp', '.test']
  if not 'ARM' in config.root.targets:
    config.unsupported = True

Other platform-specific tests are those that depend on a specific feature
of a specific sub-architecture, for example only to Intel chips that support ``AVX2``.

For instance, ``test/CodeGen/X86/psubus.ll`` tests three sub-architecture
variants:

.. code-block:: llvm

  ; RUN: llc -mcpu=core2 < %s | FileCheck %s -check-prefix=SSE2
  ; RUN: llc -mcpu=corei7-avx < %s | FileCheck %s -check-prefix=AVX1
  ; RUN: llc -mcpu=core-avx2 < %s | FileCheck %s -check-prefix=AVX2

And the checks are different:

.. code-block:: llvm

  ; SSE2: @test1
  ; SSE2: psubusw LCPI0_0(%rip), %xmm0
  ; AVX1: @test1
  ; AVX1: vpsubusw LCPI0_0(%rip), %xmm0, %xmm0
  ; AVX2: @test1
  ; AVX2: vpsubusw LCPI0_0(%rip), %xmm0, %xmm0

So, if you're testing for a behaviour that you know is platform-specific or
depends on special features of sub-architectures, you must add the specific
triple, test with the specific FileCheck and put it into the specific
directory that will filter out all other architectures.


Constraining test execution
---------------------------

Some tests can be run only in specific configurations, such as
with debug builds or on particular platforms. Use ``REQUIRES``
and ``UNSUPPORTED`` to control when the test is enabled.

Some tests are expected to fail. For example, there may be a known bug
that the test detect. Use ``XFAIL`` to mark a test as an expected failure.
An ``XFAIL`` test will be successful if its execution fails, and
will be a failure if its execution succeeds.

.. code-block:: llvm

    ; This test will be only enabled in the build with asserts.
    ; REQUIRES: asserts
    ; This test is disabled when running on Linux.
    ; UNSUPPORTED: system-linux
    ; This test is expected to fail when targeting PowerPC.
    ; XFAIL: target=powerpc{{.*}}

``REQUIRES`` and ``UNSUPPORTED`` and ``XFAIL`` all accept a comma-separated
list of boolean expressions. The values in each expression may be:

- Features added to ``config.available_features`` by configuration files such as ``lit.cfg``.
  String comparison of features is case-sensitive. Furthermore, a boolean expression can
  contain any Python regular expression enclosed in ``{{ }}``, in which case the boolean
  expression is satisfied if any feature matches the regular expression. Regular
  expressions can appear inside an identifier, so for example ``he{{l+}}o`` would match
  ``helo``, ``hello``, ``helllo``, and so on.
- The default target triple, preceded by the string ``target=`` (for example,
  ``target=x86_64-pc-windows-msvc``). Typically regular expressions are used
  to match parts of the triple (for example, ``target={{.*}}-windows{{.*}}``
  to match any Windows target triple).

| ``REQUIRES`` enables the test if all expressions are true.
| ``UNSUPPORTED`` disables the test if any expression is true.
| ``XFAIL`` expects the test to fail if any expression is true.

Use, ``XFAIL: *`` if the test is expected to fail everywhere. Similarly, use
``UNSUPPORTED: target={{.*}}`` to disable the test everywhere.

.. code-block:: llvm

    ; This test is disabled when running on Windows,
    ; and is disabled when targeting Linux, except for Android Linux.
    ; UNSUPPORTED: system-windows, target={{.*linux.*}} && !target={{.*android.*}}
    ; This test is expected to fail when targeting PowerPC or running on Darwin.
    ; XFAIL: target=powerpc{{.*}}, system-darwin


Tips for writing constraints
----------------------------

**``REQUIRES`` and ``UNSUPPORTED``**

These are logical inverses. In principle, ``UNSUPPORTED`` isn't absolutely
necessary (the logical negation could be used with ``REQUIRES`` to get
exactly the same effect), but it can make these clauses easier to read and
understand. Generally, people use ``REQUIRES`` to state things that the test
depends on to operate correctly, and ``UNSUPPORTED`` to exclude cases where
the test is expected never to work.

**``UNSUPPORTED`` and ``XFAIL``**

Both of these indicate that the test isn't expected to work; however, they
have different effects. ``UNSUPPORTED`` causes the test to be skipped;
this saves execution time, but then you'll never know whether the test
actually would start working. Conversely, ``XFAIL`` actually runs the test
but expects a failure output, taking extra execution time but alerting you
if/when the test begins to behave correctly (an XPASS test result). You
need to decide which is more appropriate in each case.

**Using ``target=...``**

Checking the target triple can be tricky; it's easy to mis-specify. For
example, ``target=mips{{.*}}`` will match not only mips, but also mipsel,
mips64, and mips64el. ``target={{.*}}-linux-gnu`` will match
x86_64-unknown-linux-gnu, but not armv8l-unknown-linux-gnueabihf.
Prefer to use hyphens to delimit triple components (``target=mips-{{.*}}``)
and it's generally a good idea to use a trailing wildcard to allow for
unexpected suffixes.

Also, it's generally better to write regular expressions that use entire
triple components, than to do something clever to shorten them. For
example, to match both freebsd and netbsd in an expression, you could write
``target={{.*(free|net)bsd.*}}`` and that would work. However, it would
prevent a ``grep freebsd`` from finding this test. Better to use:
``target={{.+-freebsd.*}} || target={{.+-netbsd.*}}``


Substitutions
-------------

Besides replacing LLVM tool names the following substitutions are performed in
RUN lines:

``%%``
   Replaced by a single ``%``. This allows escaping other substitutions.

``%s``
   File path to the test case's source. This is suitable for passing on the
   command line as the input to an LLVM tool.

   Example: ``/home/user/llvm/test/MC/ELF/foo_test.s``

``%S``
   Directory path to the test case's source.

   Example: ``/home/user/llvm/test/MC/ELF``

``%t``
   File path to a temporary file name that could be used for this test case.
   The file name won't conflict with other test cases. You can append to it
   if you need multiple temporaries. This is useful as the destination of
   some redirected output.

   Example: ``/home/user/llvm.build/test/MC/ELF/Output/foo_test.s.tmp``

``%T``
   Directory of ``%t``. Deprecated. Shouldn't be used, because it can be easily
   misused and cause race conditions between tests.

   Use ``rm -rf %t && mkdir %t`` instead if a temporary directory is necessary.

   Example: ``/home/user/llvm.build/test/MC/ELF/Output``

``%{pathsep}``

   Expands to the path separator, i.e. ``:`` (or ``;`` on Windows).

``%{fs-src-root}``
   Expands to the root component of file system paths for the source directory,
   i.e. ``/`` on Unix systems or ``C:\`` (or another drive) on Windows.

``%{fs-tmp-root}``
   Expands to the root component of file system paths for the test's temporary
   directory, i.e. ``/`` on Unix systems or ``C:\`` (or another drive) on
   Windows.

``%{fs-sep}``
   Expands to the file system separator, i.e. ``/`` or ``\`` on Windows.

``%/s, %/S, %/t, %/T``

  Act like the corresponding substitution above but replace any ``\``
  character with a ``/``. This is useful to normalize path separators.

   Example: ``%s:  C:\Desktop Files/foo_test.s.tmp``

   Example: ``%/s: C:/Desktop Files/foo_test.s.tmp``

``%{s:real}, %{S:real}, %{t:real}, %{T:real}``
``%{/s:real}, %{/S:real}, %{/t:real}, %{/T:real}``

  Act like the corresponding substitution, including with ``/``, but use
  the real path by expanding all symbolic links and substitute drives.

   Example: ``%s:  S:\foo_test.s.tmp``

   Example: ``%{/s:real}: C:/SDrive/foo_test.s.tmp``

``%:s, %:S, %:t, %:T``

  Act like the corresponding substitution above but remove colons at
  the beginning of Windows paths. This is useful to allow concatenation
  of absolute paths on Windows to produce a legal path.

   Example: ``%s:  C:\Desktop Files\foo_test.s.tmp``

   Example: ``%:s: C\Desktop Files\foo_test.s.tmp``

``%errc_<ERRCODE>``

 Some error messages may be substituted to allow different spellings
 based on the host platform.

   The following error codes are currently supported:
   ENOENT, EISDIR, EINVAL, EACCES.

   Example: ``Linux %errc_ENOENT: No such file or directory``

   Example: ``Windows %errc_ENOENT: no such file or directory``

``%if feature %{<if branch>%} %else %{<else branch>%}``

 Conditional substitution: if ``feature`` is available it expands to
 ``<if branch>``, otherwise it expands to ``<else branch>``.
 ``%else %{<else branch>%}`` is optional and treated like ``%else %{%}``
 if not present.

``%(line)``, ``%(line+<number>)``, ``%(line-<number>)``

  The number of the line where this substitution is used, with an
  optional integer offset.  These expand only if they appear
  immediately in ``RUN:``, ``DEFINE:``, and ``REDEFINE:`` directives.
  Occurrences in substitutions defined elsewhere are never expanded.
  For example, this can be used in tests with multiple RUN lines,
  which reference the test file's line numbers.

**LLVM-specific substitutions:**

``%shlibext``
   The suffix for the host platforms shared library files. This includes the
   period as the first character.

   Example: ``.so`` (Linux), ``.dylib`` (macOS), ``.dll`` (Windows)

``%exeext``
   The suffix for the host platforms executable files. This includes the
   period as the first character.

   Example: ``.exe`` (Windows), empty on Linux.

**Clang-specific substitutions:**

``%clang``
   Invokes the Clang driver.

``%clang_cpp``
   Invokes the Clang driver for C++.

``%clang_cl``
   Invokes the CL-compatible Clang driver.

``%clangxx``
   Invokes the G++-compatible Clang driver.

``%clang_cc1``
   Invokes the Clang frontend.

``%itanium_abi_triple``, ``%ms_abi_triple``
   These substitutions can be used to get the current target triple adjusted to
   the desired ABI. For example, if the test suite is running with the
   ``i686-pc-win32`` target, ``%itanium_abi_triple`` will expand to
   ``i686-pc-mingw32``. This allows a test to run with a specific ABI without
   constraining it to a specific triple.

**FileCheck-specific substitutions:**

``%ProtectFileCheckOutput``
   This should precede a ``FileCheck`` call if and only if the call's textual
   output affects test results.  It's usually easy to tell: just look for
   redirection or piping of the ``FileCheck`` call's stdout or stderr.

.. _Test-specific substitutions:

**Test-specific substitutions:**

Additional substitutions can be defined as follows:

- Lit configuration files (e.g., ``lit.cfg`` or ``lit.local.cfg``) can define
  substitutions for all tests in a test directory.  They do so by extending the
  substitution list, ``config.substitutions``.  Each item in the list is a tuple
  consisting of a pattern and its replacement, which lit applies using python's
  ``re.sub`` function.
- To define substitutions within a single test file, lit supports the
  ``DEFINE:`` and ``REDEFINE:`` directives, described in detail below.  So that
  they have no effect on other test files, these directives modify a copy of the
  substitution list that is produced by lit configuration files.

For example, the following directives can be inserted into a test file to define
``%{cflags}`` and ``%{fcflags}`` substitutions with empty initial values, which
serve as the parameters of another newly defined ``%{check}`` substitution:

.. code-block:: llvm

    ; DEFINE: %{cflags} =
    ; DEFINE: %{fcflags} =

    ; DEFINE: %{check} =                                                  \
    ; DEFINE:   %clang_cc1 -verify -fopenmp -fopenmp-version=51 %{cflags} \
    ; DEFINE:              -emit-llvm -o - %s |                           \
    ; DEFINE:     FileCheck %{fcflags} %s

Alternatively, the above substitutions can be defined in a lit configuration
file to be shared with other test files.  Either way, the test file can then
specify directives like the following to redefine the parameter substitutions as
desired before each use of ``%{check}`` in a ``RUN:`` line:

.. code-block:: llvm

    ; REDEFINE: %{cflags} = -triple x86_64-apple-darwin10.6.0 -fopenmp-simd
    ; REDEFINE: %{fcflags} = -check-prefix=SIMD
    ; RUN: %{check}

    ; REDEFINE: %{cflags} = -triple x86_64-unknown-linux-gnu -fopenmp-simd
    ; REDEFINE: %{fcflags} = -check-prefix=SIMD
    ; RUN: %{check}

    ; REDEFINE: %{cflags} = -triple x86_64-apple-darwin10.6.0
    ; REDEFINE: %{fcflags} = -check-prefix=NO-SIMD
    ; RUN: %{check}

    ; REDEFINE: %{cflags} = -triple x86_64-unknown-linux-gnu
    ; REDEFINE: %{fcflags} = -check-prefix=NO-SIMD
    ; RUN: %{check}

Besides providing initial values, the initial ``DEFINE:`` directives for the
parameter substitutions in the above example serve a second purpose: they
establish the substitution order so that both ``%{check}`` and its parameters
expand as desired.  There's a simple way to remember the required definition
order in a test file: define a substitution before any substitution that might
refer to it.

In general, substitution expansion behaves as follows:

- Upon arriving at each ``RUN:`` line, lit expands all substitutions in that
  ``RUN:`` line using their current values from the substitution list.  No
  substitution expansion is performed immediately at ``DEFINE:`` and
  ``REDEFINE:`` directives except ``%(line)``, ``%(line+<number>)``, and
  ``%(line-<number>)``.
- When expanding substitutions in a ``RUN:`` line, lit makes only one pass
  through the substitution list by default.  In this case, a substitution must
  have been inserted earlier in the substitution list than any substitution
  appearing in its value in order for the latter to expand.  (For greater
  flexibility, you can enable multiple passes through the substitution list by
  setting `recursiveExpansionLimit`_ in a lit configuration file.)
- While lit configuration files can insert anywhere in the substitution list,
  the insertion behavior of the ``DEFINE:`` and ``REDEFINE:`` directives is
  specified below and is designed specifically for the use case presented in the
  example above.
- Defining a substitution in terms of itself, whether directly or via other
  substitutions, should be avoided.  It usually produces an infinitely recursive
  definition that cannot be fully expanded.  It does *not* define the
  substitution in terms of its previous value, even when using ``REDEFINE:``.

The relationship between the ``DEFINE:`` and ``REDEFINE:`` directive is
analogous to the relationship between a variable declaration and variable
assignment in many programming languages:

- ``DEFINE: %{name} = value``

   This directive assigns the specified value to a new substitution whose
   pattern is ``%{name}``, or it reports an error if there is already a
   substitution whose pattern contains ``%{name}`` because that could produce
   confusing expansions (e.g., a lit configuration file might define a
   substitution with the pattern ``%{name}\[0\]``).  The new substitution is
   inserted at the start of the substitution list so that it will expand first.
   Thus, its value can contain any substitution previously defined, whether in
   the same test file or in a lit configuration file, and both will expand.

- ``REDEFINE: %{name} = value``

   This directive assigns the specified value to an existing substitution whose
   pattern is ``%{name}``, or it reports an error if there are no substitutions
   with that pattern or if there are multiple substitutions whose patterns
   contain ``%{name}``.  The substitution's current position in the substitution
   list does not change so that expansion order relative to other existing
   substitutions is preserved.

The following properties apply to both the ``DEFINE:`` and ``REDEFINE:``
directives:

- **Substitution name**: In the directive, whitespace immediately before or
  after ``%{name}`` is optional and discarded.  ``%{name}`` must start with
  ``%{``, it must end with ``}``, and the rest must start with a letter or
  underscore and contain only alphanumeric characters, hyphens, underscores, and
  colons.  This syntax has a few advantages:

    - It is impossible for ``%{name}`` to contain sequences that are special in
      python's ``re.sub`` patterns.  Otherwise, attempting to specify
      ``%{name}`` as a substitution pattern in a lit configuration file could
      produce confusing expansions.
    - The braces help avoid the possibility that another substitution's pattern
      will match part of ``%{name}`` or vice-versa, producing confusing
      expansions.  However, the patterns of substitutions defined by lit
      configuration files and by lit itself are not restricted to this form, so
      overlaps are still theoretically possible.

- **Substitution value**: The value includes all text from the first
  non-whitespace character after ``=`` to the last non-whitespace character.  If
  there is no non-whitespace character after ``=``, the value is the empty
  string.  Escape sequences that can appear in python ``re.sub`` replacement
  strings are treated as plain text in the value.
- **Line continuations**: If the last non-whitespace character on the line after
  ``:`` is ``\``, then the next directive must use the same directive keyword
  (e.g., ``DEFINE:``) , and it is an error if there is no additional directive.
  That directive serves as a continuation.  That is, before following the rules
  above to parse the text after ``:`` in either directive, lit joins that text
  together to form a single directive, replaces the ``\`` with a single space,
  and removes any other whitespace that is now adjacent to that space.  A
  continuation can be continued in the same manner.  A continuation containing
  only whitespace after its ``:`` is an error.

.. _recursiveExpansionLimit:

**recursiveExpansionLimit:**

As described in the previous section, when expanding substitutions in a ``RUN:``
line, lit makes only one pass through the substitution list by default.  Thus,
if substitutions are not defined in the proper order, some will remain in the
``RUN:`` line unexpanded.  For example, the following directives refer to
``%{inner}`` within ``%{outer}`` but do not define ``%{inner}`` until after
``%{outer}``:

.. code-block:: llvm

    ; By default, this definition order does not enable full expansion.

    ; DEFINE: %{outer} = %{inner}
    ; DEFINE: %{inner} = expanded

    ; RUN: echo '%{outer}'

``DEFINE:`` inserts substitutions at the start of the substitution list, so
``%{inner}`` expands first but has no effect because the original ``RUN:`` line
does not contain ``%{inner}``.  Next, ``%{outer}`` expands, and the output of
the ``echo`` command becomes:

.. code-block:: shell

    %{inner}

Of course, one way to fix this simple case is to reverse the definitions of
``%{outer}`` and ``%{inner}``.  However, if a test has a complex set of
substitutions that can all reference each other, there might not exist a
sufficient substitution order.

To address such use cases, lit configuration files support
``config.recursiveExpansionLimit``, which can be set to a non-negative integer
to specify the maximum number of passes through the substitution list.  Thus, in
the above example, setting the limit to 2 would cause lit to make a second pass
that expands ``%{inner}`` in the ``RUN:`` line, and the output from the ``echo``
command when then be:

.. code-block:: shell

    expanded

To improve performance, lit will stop making passes when it notices the ``RUN:``
line has stopped changing.  In the above example, setting the limit higher than
2 is thus harmless.

To facilitate debugging, after reaching the limit, lit will make one extra pass
and report an error if the ``RUN:`` line changes again.  In the above example,
setting the limit to 1 will thus cause lit to report an error instead of
producing incorrect output.

Options
-------

The llvm lit configuration allows to customize some things with user options:

``llc``, ``opt``, ...
    Substitute the respective llvm tool name with a custom command line. This
    allows to specify custom paths and default arguments for these tools.
    Example:

    % llvm-lit "-Dllc=llc -verify-machineinstrs"

``run_long_tests``
    Enable the execution of long running tests.

``llvm_site_config``
    Load the specified lit configuration instead of the default one.


Other Features
--------------

To make RUN line writing easier, there are several helper programs. These
helpers are in the PATH when running tests, so you can just call them using
their name. For example:

``not``
   This program runs its arguments and then inverts the result code from it.
   Zero result codes become 1. Non-zero result codes become 0.

To make the output more useful, :program:`lit` will scan
the lines of the test case for ones that contain a pattern that matches
``PR[0-9]+``. This is the syntax for specifying a PR (Problem Report) number
that is related to the test case. The number after "PR" specifies the
LLVM Bugzilla number. When a PR number is specified, it will be used in
the pass/fail reporting. This is useful to quickly get some context when
a test fails.

Finally, any line that contains "END." will cause the special
interpretation of lines to terminate. This is generally done right after
the last RUN: line. This has two side effects:

(a) it prevents special interpretation of lines that are part of the test
    program, not the instructions to the test case, and

(b) it speeds things up for really big test cases by avoiding
    interpretation of the remainder of the file.
