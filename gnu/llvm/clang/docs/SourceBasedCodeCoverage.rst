==========================
Source-based Code Coverage
==========================

.. contents::
   :local:

Introduction
============

This document explains how to use clang's source-based code coverage feature.
It's called "source-based" because it operates on AST and preprocessor
information directly. This allows it to generate very precise coverage data.

Clang ships two other code coverage implementations:

* :doc:`SanitizerCoverage` - A low-overhead tool meant for use alongside the
  various sanitizers. It can provide up to edge-level coverage.

* gcov - A GCC-compatible coverage implementation which operates on DebugInfo.
  This is enabled by ``-ftest-coverage`` or ``--coverage``.

From this point onwards "code coverage" will refer to the source-based kind.

The code coverage workflow
==========================

The code coverage workflow consists of three main steps:

* Compiling with coverage enabled.

* Running the instrumented program.

* Creating coverage reports.

The next few sections work through a complete, copy-'n-paste friendly example
based on this program:

.. code-block:: cpp

    % cat <<EOF > foo.cc
    #define BAR(x) ((x) || (x))
    template <typename T> void foo(T x) {
      for (unsigned I = 0; I < 10; ++I) { BAR(I); }
    }
    int main() {
      foo<int>(0);
      foo<float>(0);
      return 0;
    }
    EOF

Compiling with coverage enabled
===============================

To compile code with coverage enabled, pass ``-fprofile-instr-generate
-fcoverage-mapping`` to the compiler:

.. code-block:: console

    # Step 1: Compile with coverage enabled.
    % clang++ -fprofile-instr-generate -fcoverage-mapping foo.cc -o foo

Note that linking together code with and without coverage instrumentation is
supported. Uninstrumented code simply won't be accounted for in reports.

To compile code with Modified Condition/Decision Coverage (MC/DC) enabled,
pass ``-fcoverage-mcdc`` in addition to the clang options specified above.
MC/DC is an advanced form of code coverage most applicable in the embedded
space.

Running the instrumented program
================================

The next step is to run the instrumented program. When the program exits it
will write a **raw profile** to the path specified by the ``LLVM_PROFILE_FILE``
environment variable. If that variable does not exist, the profile is written
to ``default.profraw`` in the current directory of the program. If
``LLVM_PROFILE_FILE`` contains a path to a non-existent directory, the missing
directory structure will be created.  Additionally, the following special
**pattern strings** are rewritten:

* "%p" expands out to the process ID.

* "%h" expands out to the hostname of the machine running the program.

* "%t" expands out to the value of the ``TMPDIR`` environment variable. On
  Darwin, this is typically set to a temporary scratch directory.

* "%Nm" expands out to the instrumented binary's signature. When this pattern
  is specified, the runtime creates a pool of N raw profiles which are used for
  on-line profile merging. The runtime takes care of selecting a raw profile
  from the pool, locking it, and updating it before the program exits.  If N is
  not specified (i.e the pattern is "%m"), it's assumed that ``N = 1``. The
  merge pool specifier can only occur once per filename pattern.

* "%c" expands out to nothing, but enables a mode in which profile counter
  updates are continuously synced to a file. This means that if the
  instrumented program crashes, or is killed by a signal, perfect coverage
  information can still be recovered. Continuous mode does not support value
  profiling for PGO, and is only supported on Darwin at the moment. Support for
  Linux may be mostly complete but requires testing, and support for Windows
  may require more extensive changes: please get involved if you are interested
  in porting this feature.

.. code-block:: console

    # Step 2: Run the program.
    % LLVM_PROFILE_FILE="foo.profraw" ./foo

Note that continuous mode is also used on Fuchsia where it's the only supported
mode, but the implementation is different. The Darwin and Linux implementation
relies on padding and the ability to map a file over the existing memory
mapping which is generally only available on POSIX systems and isn't suitable
for other platforms.

On Fuchsia, we rely on the ability to relocate counters at runtime using a
level of indirection. On every counter access, we add a bias to the counter
address. This bias is stored in ``__llvm_profile_counter_bias`` symbol that's
provided by the profile runtime and is initially set to zero, meaning no
relocation. The runtime can map the profile into memory at arbitrary locations,
and set bias to the offset between the original and the new counter location,
at which point every subsequent counter access will be to the new location,
which allows updating profile directly akin to the continuous mode.

The advantage of this approach is that doesn't require any special OS support.
The disadvantage is the extra overhead due to additional instructions required
for each counter access (overhead both in terms of binary size and performance)
plus duplication of counters (i.e. one copy in the binary itself and another
copy that's mapped into memory). This implementation can be also enabled for
other platforms by passing the ``-runtime-counter-relocation`` option to the
backend during compilation.

For a program such as the `Lit <https://llvm.org/docs/CommandGuide/lit.html>`_
testing tool which invokes other programs, it may be necessary to set
``LLVM_PROFILE_FILE`` for each invocation. The pattern strings "%p" or "%Nm"
may help to avoid corruption due to concurrency. Note that "%p" is also a Lit
token and needs to be escaped as "%%p".

.. code-block:: console

    % clang++ -fprofile-instr-generate -fcoverage-mapping -mllvm -runtime-counter-relocation foo.cc -o foo

Creating coverage reports
=========================

Raw profiles have to be **indexed** before they can be used to generate
coverage reports. This is done using the "merge" tool in ``llvm-profdata``
(which can combine multiple raw profiles and index them at the same time):

.. code-block:: console

    # Step 3(a): Index the raw profile.
    % llvm-profdata merge -sparse foo.profraw -o foo.profdata

For an example of merging multiple profiles created by testing,
see the LLVM `coverage build script <https://github.com/llvm/llvm-zorg/blob/main/zorg/jenkins/jobs/jobs/llvm-coverage>`_.

There are multiple different ways to render coverage reports. The simplest
option is to generate a line-oriented report:

.. code-block:: console

    # Step 3(b): Create a line-oriented coverage report.
    % llvm-cov show ./foo -instr-profile=foo.profdata

This report includes a summary view as well as dedicated sub-views for
templated functions and their instantiations. For our example program, we get
distinct views for ``foo<int>(...)`` and ``foo<float>(...)``.  If
``-show-line-counts-or-regions`` is enabled, ``llvm-cov`` displays sub-line
region counts (even in macro expansions):

.. code-block:: none

        1|   20|#define BAR(x) ((x) || (x))
                               ^20     ^2
        2|    2|template <typename T> void foo(T x) {
        3|   22|  for (unsigned I = 0; I < 10; ++I) { BAR(I); }
                                       ^22     ^20  ^20^20
        4|    2|}
    ------------------
    | void foo<int>(int):
    |      2|    1|template <typename T> void foo(T x) {
    |      3|   11|  for (unsigned I = 0; I < 10; ++I) { BAR(I); }
    |                                     ^11     ^10  ^10^10
    |      4|    1|}
    ------------------
    | void foo<float>(int):
    |      2|    1|template <typename T> void foo(T x) {
    |      3|   11|  for (unsigned I = 0; I < 10; ++I) { BAR(I); }
    |                                     ^11     ^10  ^10^10
    |      4|    1|}
    ------------------

If ``--show-branches=count`` and ``--show-expansions`` are also enabled, the
sub-views will show detailed branch coverage information in addition to the
region counts:

.. code-block:: none

    ------------------
    | void foo<float>(int):
    |      2|    1|template <typename T> void foo(T x) {
    |      3|   11|  for (unsigned I = 0; I < 10; ++I) { BAR(I); }
    |                                     ^11     ^10  ^10^10
    |  ------------------
    |  |  |    1|     10|#define BAR(x) ((x) || (x))
    |  |  |                             ^10     ^1
    |  |  |  ------------------
    |  |  |  |  Branch (1:17): [True: 9, False: 1]
    |  |  |  |  Branch (1:24): [True: 0, False: 1]
    |  |  |  ------------------
    |  ------------------
    |  |  Branch (3:23): [True: 10, False: 1]
    |  ------------------
    |      4|    1|}
    ------------------

If the application was instrumented for Modified Condition/Decision Coverage
(MC/DC) using the clang option ``-fcoverage-mcdc``, an MC/DC subview can be
enabled using ``--show-mcdc`` that will show detailed MC/DC information for
each complex condition boolean expression containing at most six conditions.

To generate a file-level summary of coverage statistics instead of a
line-oriented report, try:

.. code-block:: console

    # Step 3(c): Create a coverage summary.
    % llvm-cov report ./foo -instr-profile=foo.profdata
    Filename           Regions    Missed Regions     Cover   Functions  Missed Functions  Executed       Lines      Missed Lines     Cover     Branches    Missed Branches     Cover
    --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /tmp/foo.cc             13                 0   100.00%           3                 0   100.00%          13                 0   100.00%           12                  2    83.33%
    --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    TOTAL                   13                 0   100.00%           3                 0   100.00%          13                 0   100.00%           12                  2    83.33%

The ``llvm-cov`` tool supports specifying a custom demangler, writing out
reports in a directory structure, and generating html reports. For the full
list of options, please refer to the `command guide
<https://llvm.org/docs/CommandGuide/llvm-cov.html>`_.

A few final notes:

* The ``-sparse`` flag is optional but can result in dramatically smaller
  indexed profiles. This option should not be used if the indexed profile will
  be reused for PGO.

* Raw profiles can be discarded after they are indexed. Advanced use of the
  profile runtime library allows an instrumented program to merge profiling
  information directly into an existing raw profile on disk. The details are
  out of scope.

* The ``llvm-profdata`` tool can be used to merge together multiple raw or
  indexed profiles. To combine profiling data from multiple runs of a program,
  try e.g:

  .. code-block:: console

      % llvm-profdata merge -sparse foo1.profraw foo2.profdata -o foo3.profdata

Exporting coverage data
=======================

Coverage data can be exported into JSON using the ``llvm-cov export``
sub-command. There is a comprehensive reference which defines the structure of
the exported data at a high level in the llvm-cov source code.

Interpreting reports
====================

There are six statistics tracked in a coverage summary:

* Function coverage is the percentage of functions which have been executed at
  least once. A function is considered to be executed if any of its
  instantiations are executed.

* Instantiation coverage is the percentage of function instantiations which
  have been executed at least once. Template functions and static inline
  functions from headers are two kinds of functions which may have multiple
  instantiations. This statistic is hidden by default in reports, but can be
  enabled via the ``-show-instantiation-summary`` option.

* Line coverage is the percentage of code lines which have been executed at
  least once. Only executable lines within function bodies are considered to be
  code lines.

* Region coverage is the percentage of code regions which have been executed at
  least once. A code region may span multiple lines (e.g in a large function
  body with no control flow). However, it's also possible for a single line to
  contain multiple code regions (e.g in "return x || y && z").

* Branch coverage is the percentage of "true" and "false" branches that have
  been taken at least once. Each branch is tied to individual conditions in the
  source code that may each evaluate to either "true" or "false".  These
  conditions may comprise larger boolean expressions linked by boolean logical
  operators. For example, "x = (y == 2) || (z < 10)" is a boolean expression
  that is comprised of two individual conditions, each of which evaluates to
  either true or false, producing four total branch outcomes.

* Modified Condition/Decision Coverage (MC/DC) is the percentage of individual
  branch conditions that have been shown to independently affect the decision
  outcome of the boolean expression they comprise. This is accomplished using
  the analysis of executed control flow through the expression (i.e. test
  vectors) to show that as a condition's outcome is varied between "true" and
  false", the decision's outcome also varies between "true" and false", while
  the outcome of all other conditions is held fixed (or they are masked out as
  unevaluatable, as happens in languages whose logical operators have
  short-circuit semantics).  MC/DC builds on top of branch coverage and
  requires that all code blocks and all execution paths have been tested.  This
  statistic is hidden by default in reports, but it can be enabled via the
  ``-show-mcdc-summary`` option as long as code was also compiled using the
  clang option ``-fcoverage-mcdc``.

  * Boolean expressions that are only comprised of one condition (and therefore
    have no logical operators) are not included in MC/DC analysis and are
    trivially deducible using branch coverage.

Of these six statistics, function coverage is usually the least granular while
branch coverage (with MC/DC) is the most granular. 100% branch coverage for a
function implies 100% region coverage for a function. The project-wide totals
for each statistic are listed in the summary.

Format compatibility guarantees
===============================

* There are no backwards or forwards compatibility guarantees for the raw
  profile format. Raw profiles may be dependent on the specific compiler
  revision used to generate them. It's inadvisable to store raw profiles for
  long periods of time.

* Tools must retain **backwards** compatibility with indexed profile formats.
  These formats are not forwards-compatible: i.e, a tool which uses format
  version X will not be able to understand format version (X+k).

* Tools must also retain **backwards** compatibility with the format of the
  coverage mappings emitted into instrumented binaries. These formats are not
  forwards-compatible.

* The JSON coverage export format has a (major, minor, patch) version triple.
  Only a major version increment indicates a backwards-incompatible change. A
  minor version increment is for added functionality, and patch version
  increments are for bugfixes.

Impact of llvm optimizations on coverage reports
================================================

llvm optimizations (such as inlining or CFG simplification) should have no
impact on coverage report quality. This is due to the fact that the mapping
from source regions to profile counters is immutable, and is generated before
the llvm optimizer kicks in. The optimizer can't prove that profile counter
instrumentation is safe to delete (because it's not: it affects the profile the
program emits), and so leaves it alone.

Note that this coverage feature does not rely on information that can degrade
during the course of optimization, such as debug info line tables.

Using the profiling runtime without static initializers
=======================================================

By default the compiler runtime uses a static initializer to determine the
profile output path and to register a writer function. To collect profiles
without using static initializers, do this manually:

* Export a ``int __llvm_profile_runtime`` symbol from each instrumented shared
  library and executable. When the linker finds a definition of this symbol, it
  knows to skip loading the object which contains the profiling runtime's
  static initializer.

* Forward-declare ``void __llvm_profile_initialize_file(void)`` and call it
  once from each instrumented executable. This function parses
  ``LLVM_PROFILE_FILE``, sets the output path, and truncates any existing files
  at that path. To get the same behavior without truncating existing files,
  pass a filename pattern string to ``void __llvm_profile_set_filename(char
  *)``.  These calls can be placed anywhere so long as they precede all calls
  to ``__llvm_profile_write_file``.

* Forward-declare ``int __llvm_profile_write_file(void)`` and call it to write
  out a profile. This function returns 0 when it succeeds, and a non-zero value
  otherwise. Calling this function multiple times appends profile data to an
  existing on-disk raw profile.

In C++ files, declare these as ``extern "C"``.

Using the profiling runtime without a filesystem
------------------------------------------------

The profiling runtime also supports freestanding environments that lack a
filesystem. The runtime ships as a static archive that's structured to make
dependencies on a hosted environment optional, depending on what features
the client application uses.

The first step is to export ``__llvm_profile_runtime``, as above, to disable
the default static initializers. Instead of calling the ``*_file()`` APIs
described above, use the following to save the profile directly to a buffer
under your control:

* Forward-declare ``uint64_t __llvm_profile_get_size_for_buffer(void)`` and
  call it to determine the size of the profile. You'll need to allocate a
  buffer of this size.

* Forward-declare ``int __llvm_profile_write_buffer(char *Buffer)`` and call it
  to copy the current counters to ``Buffer``, which is expected to already be
  allocated and big enough for the profile.

* Optionally, forward-declare ``void __llvm_profile_reset_counters(void)`` and
  call it to reset the counters before entering a specific section to be
  profiled. This is only useful if there is some setup that should be excluded
  from the profile.

In C++ files, declare these as ``extern "C"``.

Collecting coverage reports for the llvm project
================================================

To prepare a coverage report for llvm (and any of its sub-projects), add
``-DLLVM_BUILD_INSTRUMENTED_COVERAGE=On`` to the cmake configuration. Raw
profiles will be written to ``$BUILD_DIR/profiles/``. To prepare an html
report, run ``llvm/utils/prepare-code-coverage-artifact.py``.

To specify an alternate directory for raw profiles, use
``-DLLVM_PROFILE_DATA_DIR``. To change the size of the profile merge pool, use
``-DLLVM_PROFILE_MERGE_POOL_SIZE``.

Drawbacks and limitations
=========================

* Prior to version 2.26, the GNU binutils BFD linker is not able link programs
  compiled with ``-fcoverage-mapping`` in its ``--gc-sections`` mode.  Possible
  workarounds include disabling ``--gc-sections``, upgrading to a newer version
  of BFD, or using the Gold linker.

* Code coverage does not handle unpredictable changes in control flow or stack
  unwinding in the presence of exceptions precisely. Consider the following
  function:

  .. code-block:: cpp

      int f() {
        may_throw();
        return 0;
      }

  If the call to ``may_throw()`` propagates an exception into ``f``, the code
  coverage tool may mark the ``return`` statement as executed even though it is
  not. A call to ``longjmp()`` can have similar effects.

Clang implementation details
============================

This section may be of interest to those wishing to understand or improve
the clang code coverage implementation.

Gap regions
-----------

Gap regions are source regions with counts. A reporting tool cannot set a line
execution count to the count from a gap region unless that region is the only
one on a line.

Gap regions are used to eliminate unnatural artifacts in coverage reports, such
as red "unexecuted" highlights present at the end of an otherwise covered line,
or blue "executed" highlights present at the start of a line that is otherwise
not executed.

Branch regions
--------------
When viewing branch coverage details in source-based file-level sub-views using
``--show-branches``, it is recommended that users show all macro expansions
(using option ``--show-expansions``) since macros may contain hidden branch
conditions.  The coverage summary report will always include these macro-based
boolean expressions in the overall branch coverage count for a function or
source file.

Branch coverage is not tracked for constant folded branch conditions since
branches are not generated for these cases.  In the source-based file-level
sub-view, these branches will simply be shown as ``[Folded - Ignored]`` so that
users are informed about what happened.

Branch coverage is tied directly to branch-generating conditions in the source
code.  Users should not see hidden branches that aren't actually tied to the
source code.

MC/DC Instrumentation
---------------------

When instrumenting for Modified Condition/Decision Coverage (MC/DC) using the
clang option ``-fcoverage-mcdc``, there are two hard limits.

The maximum number of terms is limited to 32767, which is practical for
handwritten expressions. To be more restrictive in order to enforce coding rules,
use ``-Xclang -fmcdc-max-conditions=n``. Expressions with exceeded condition
counts ``n`` will generate warnings and will be excluded in the MC/DC coverage.

The number of test vectors (the maximum number of possible combinations of
expressions) is limited to 2,147,483,646. In this case, approximately
256MiB (==2GiB/8) is used to record test vectors.

To reduce memory usage, users can limit the maximum number of test vectors per
expression with ``-Xclang -fmcdc-max-test-vectors=m``.
If the number of test vectors resulting from the analysis of an expression
exceeds ``m``, a warning will be issued and the expression will be excluded
from the MC/DC coverage.

The number of test vectors ``m``, for ``n`` terms in an expression, can be
``m <= 2^n`` in the theoretical worst case, but is usually much smaller.
In simple cases, such as expressions consisting of a sequence of single
operators, ``m == n+1``. For example, ``(a && b && c && d && e && f && g)``
requires 8 test vectors.

Expressions such as ``((a0 && b0) || (a1 && b1) || ...)`` can cause the
number of test vectors to increase exponentially.

Also, if a boolean expression is embedded in the nest of another boolean
expression but separated by a non-logical operator, this is also not supported.
For example, in ``x = (a && b && c && func(d && f))``, the ``d && f`` case
starts a new boolean expression that is separated from the other conditions by
the operator ``func()``.  When this is encountered, a warning will be generated
and the boolean expression will not be instrumented.

Switch statements
-----------------

The region mapping for a switch body consists of a gap region that covers the
entire body (starting from the '{' in 'switch (...) {', and terminating where the
last case ends). This gap region has a zero count: this causes "gap" areas in
between case statements, which contain no executable code, to appear uncovered.

When a switch case is visited, the parent region is extended: if the parent
region has no start location, its start location becomes the start of the case.
This is used to support switch statements without a ``CompoundStmt`` body, in
which the switch body and the single case share a count.

For switches with ``CompoundStmt`` bodies, a new region is created at the start
of each switch case.

Branch regions are also generated for each switch case, including the default
case. If there is no explicitly defined default case in the source code, a
branch region is generated to correspond to the implicit default case that is
generated by the compiler.  The implicit branch region is tied to the line and
column number of the switch statement condition since no source code for the
implicit case exists.
