lit - LLVM Integrated Tester
============================

.. program:: lit

SYNOPSIS
--------

:program:`lit` [*options*] [*tests*]

DESCRIPTION
-----------

:program:`lit` is a portable tool for executing LLVM and Clang style test
suites, summarizing their results, and providing indication of failures.
:program:`lit` is designed to be a lightweight testing tool with as simple a
user interface as possible.

:program:`lit` should be run with one or more *tests* to run specified on the
command line.  Tests can be either individual test files or directories to
search for tests (see :ref:`test-discovery`).

Each specified test will be executed (potentially concurrently) and once all
tests have been run :program:`lit` will print summary information on the number
of tests which passed or failed (see :ref:`test-status-results`).  The
:program:`lit` program will execute with a non-zero exit code if any tests
fail.

By default :program:`lit` will use a succinct progress display and will only
print summary information for test failures.  See :ref:`output-options` for
options controlling the :program:`lit` progress display and output.

:program:`lit` also includes a number of options for controlling how tests are
executed (specific features may depend on the particular test format).  See
:ref:`execution-options` for more information.

Finally, :program:`lit` also supports additional options for only running a
subset of the options specified on the command line, see
:ref:`selection-options` for more information.

:program:`lit` parses options from the environment variable ``LIT_OPTS`` after
parsing options from the command line.  ``LIT_OPTS`` is primarily useful for
supplementing or overriding the command-line options supplied to :program:`lit`
by ``check`` targets defined by a project's build system.

:program:`lit` can also read options from response files which are specified as
inputs using the ``@path/to/file.rsp`` syntax. Arguments read from a file must
be one per line and are treated as if they were in the same place as the
original file referencing argument on the command line. A response file can
reference other response files.

Users interested in the :program:`lit` architecture or designing a
:program:`lit` testing implementation should see :ref:`lit-infrastructure`.

GENERAL OPTIONS
---------------

.. option:: -h, --help

 Show the :program:`lit` help message.

.. option:: -j N, --workers=N

 Run ``N`` tests in parallel.  By default, this is automatically chosen to
 match the number of detected available CPUs.

.. option:: --config-prefix=NAME

 Search for :file:`{NAME}.cfg` and :file:`{NAME}.site.cfg` when searching for
 test suites, instead of :file:`lit.cfg` and :file:`lit.site.cfg`.

.. option:: -D NAME[=VALUE], --param NAME[=VALUE]

 Add a user defined parameter ``NAME`` with the given ``VALUE`` (or the empty
 string if not given).  The meaning and use of these parameters is test suite
 dependent.

.. _output-options:

OUTPUT OPTIONS
--------------

.. option:: -q, --quiet

 Suppress any output except for test failures.

.. option:: -s, --succinct

 Show less output, for example don't show information on tests that pass.
 Also show a progress bar, unless ``--no-progress-bar`` is specified.

.. option:: -v, --verbose

 Show more information on test failures, for example the entire test output
 instead of just the test result.

 Each command is printed before it is executed. This can be valuable for
 debugging test failures, as the last printed command is the one that failed.
 Moreover, :program:`lit` inserts ``'RUN: at line N'`` before each
 command pipeline in the output to help you locate the source line of
 the failed command.

.. option:: -vv, --echo-all-commands

 Deprecated alias for -v.

.. option:: -a, --show-all

 Enable -v, but for all tests not just failed tests.

.. option:: --no-progress-bar

 Do not use curses based progress bar.

.. option:: --show-unsupported

 Show the names of unsupported tests.

.. option:: --show-xfail

 Show the names of tests that were expected to fail.

.. _execution-options:

EXECUTION OPTIONS
-----------------

.. option:: --path=PATH

 Specify an additional ``PATH`` to use when searching for executables in tests.

.. option:: --vg

 Run individual tests under valgrind (using the memcheck tool).  The
 ``--error-exitcode`` argument for valgrind is used so that valgrind failures
 will cause the program to exit with a non-zero status.

 When this option is enabled, :program:`lit` will also automatically provide a
 "``valgrind``" feature that can be used to conditionally disable (or expect
 failure in) certain tests.

.. option:: --vg-arg=ARG

 When :option:`--vg` is used, specify an additional argument to pass to
 :program:`valgrind` itself.

.. option:: --vg-leak

 When :option:`--vg` is used, enable memory leak checks.  When this option is
 enabled, :program:`lit` will also automatically provide a "``vg_leak``"
 feature that can be used to conditionally disable (or expect failure in)
 certain tests.

.. option:: --skip-test-time-recording

 Disable tracking the wall time individual tests take to execute.

.. option:: --time-tests

 Track the wall time individual tests take to execute and includes the results
 in the summary output.  This is useful for determining which tests in a test
 suite take the most time to execute.

.. option:: --ignore-fail

 Exit with status zero even if some tests fail.

.. _selection-options:

SELECTION OPTIONS
-----------------

By default, `lit` will run failing tests first, then run tests in descending
execution time order to optimize concurrency.  The execution order can be
changed using the :option:`--order` option.

The timing data is stored in the `test_exec_root` in a file named
`.lit_test_times.txt`. If this file does not exist, then `lit` checks the
`test_source_root` for the file to optionally accelerate clean builds.

.. option:: --shuffle

 Run the tests in a random order, not failing/slowest first. Deprecated,
 use :option:`--order` instead.

.. option:: --per-test-coverage

 Emit the necessary test coverage data, divided per test case (involves
 setting a unique value to LLVM_PROFILE_FILE for each RUN). The coverage
 data files will be emitted in the directory specified by `config.test_exec_root`.

.. option:: --max-failures N

 Stop execution after the given number ``N`` of failures.
 An integer argument should be passed on the command line
 prior to execution.

.. option:: --max-tests=N

 Run at most ``N`` tests and then terminate.

.. option:: --max-time=N

 Spend at most ``N`` seconds (approximately) running tests and then terminate.
 Note that this is not an alias for :option:`--timeout`; the two are
 different kinds of maximums.

.. option:: --num-shards=M

 Divide the set of selected tests into ``M`` equal-sized subsets or
 "shards", and run only one of them.  Must be used with the
 ``--run-shard=N`` option, which selects the shard to run. The environment
 variable ``LIT_NUM_SHARDS`` can also be used in place of this
 option. These two options provide a coarse mechanism for partitioning large
 testsuites, for parallel execution on separate machines (say in a large
 testing farm).

.. option:: --order={lexical,random,smart}

 Define the order in which tests are run. The supported values are:

 - lexical - tests will be run in lexical order according to the test file
   path. This option is useful when predictable test order is desired.

 - random - tests will be run in random order.

 - smart - tests that failed previously will be run first, then the remaining
   tests, all in descending execution time order. This is the default as it
   optimizes concurrency.

.. option:: --run-shard=N

 Select which shard to run, assuming the ``--num-shards=M`` option was
 provided. The two options must be used together, and the value of ``N``
 must be in the range ``1..M``. The environment variable
 ``LIT_RUN_SHARD`` can also be used in place of this option.

.. option:: --timeout=N

 Spend at most ``N`` seconds (approximately) running each individual test.
 ``0`` means no time limit, and ``0`` is the default. Note that this is not an
 alias for :option:`--max-time`; the two are different kinds of maximums.

.. option:: --filter=REGEXP

  Run only those tests whose name matches the regular expression specified in
  ``REGEXP``. The environment variable ``LIT_FILTER`` can be also used in place
  of this option, which is especially useful in environments where the call
  to ``lit`` is issued indirectly.

.. option:: --filter-out=REGEXP

  Filter out those tests whose name matches the regular expression specified in
  ``REGEXP``. The environment variable ``LIT_FILTER_OUT`` can be also used in
  place of this option, which is especially useful in environments where the
  call to ``lit`` is issued indirectly.

.. option:: --xfail=LIST

  Treat those tests whose name is in the semicolon separated list ``LIST`` as
  ``XFAIL``. This can be helpful when one does not want to modify the test
  suite. The environment variable ``LIT_XFAIL`` can be also used in place of
  this option, which is especially useful in environments where the call to
  ``lit`` is issued indirectly.

  A test name can specified as a file name relative to the test suite directory.
  For example:

  .. code-block:: none

    LIT_XFAIL="affinity/kmp-hw-subset.c;offloading/memory_manager.cpp"

  In this case, all of the following tests are treated as ``XFAIL``:

  .. code-block:: none

    libomp :: affinity/kmp-hw-subset.c
    libomptarget :: nvptx64-nvidia-cuda :: offloading/memory_manager.cpp
    libomptarget :: x86_64-pc-linux-gnu :: offloading/memory_manager.cpp

  Alternatively, a test name can be specified as the full test name
  reported in LIT output.  For example, we can adjust the previous
  example not to treat the ``nvptx64-nvidia-cuda`` version of
  ``offloading/memory_manager.cpp`` as XFAIL:

  .. code-block:: none

    LIT_XFAIL="affinity/kmp-hw-subset.c;libomptarget :: x86_64-pc-linux-gnu :: offloading/memory_manager.cpp"

.. option:: --xfail-not=LIST

  Do not treat the specified tests as ``XFAIL``.  The environment variable
  ``LIT_XFAIL_NOT`` can also be used in place of this option.  The syntax is the
  same as for :option:`--xfail` and ``LIT_XFAIL``.  :option:`--xfail-not` and
  ``LIT_XFAIL_NOT`` always override all other ``XFAIL`` specifications,
  including an :option:`--xfail` appearing later on the command line.  The
  primary purpose is to suppress an ``XPASS`` result without modifying a test
  case that uses the ``XFAIL`` directive.

ADDITIONAL OPTIONS
------------------

.. option:: --debug

 Run :program:`lit` in debug mode, for debugging configuration issues and
 :program:`lit` itself.

.. option:: --show-suites

 List the discovered test suites and exit.

.. option:: --show-tests

 List all of the discovered tests and exit.

EXIT STATUS
-----------

:program:`lit` will exit with an exit code of 1 if there are any FAIL or XPASS
results.  Otherwise, it will exit with the status 0.  Other exit codes are used
for non-test related failures (for example a user error or an internal program
error).

.. _test-discovery:

TEST DISCOVERY
--------------

The inputs passed to :program:`lit` can be either individual tests, or entire
directories or hierarchies of tests to run.  When :program:`lit` starts up, the
first thing it does is convert the inputs into a complete list of tests to run
as part of *test discovery*.

In the :program:`lit` model, every test must exist inside some *test suite*.
:program:`lit` resolves the inputs specified on the command line to test suites
by searching upwards from the input path until it finds a :file:`lit.cfg` or
:file:`lit.site.cfg` file.  These files serve as both a marker of test suites
and as configuration files which :program:`lit` loads in order to understand
how to find and run the tests inside the test suite.

Once :program:`lit` has mapped the inputs into test suites it traverses the
list of inputs adding tests for individual files and recursively searching for
tests in directories.

This behavior makes it easy to specify a subset of tests to run, while still
allowing the test suite configuration to control exactly how tests are
interpreted.  In addition, :program:`lit` always identifies tests by the test
suite they are in, and their relative path inside the test suite.  For
appropriately configured projects, this allows :program:`lit` to provide
convenient and flexible support for out-of-tree builds.

.. _test-status-results:

TEST STATUS RESULTS
-------------------

Each test ultimately produces one of the following eight results:

**PASS**

 The test succeeded.

**FLAKYPASS**

 The test succeeded after being re-run more than once. This only applies to
 tests containing an ``ALLOW_RETRIES:`` annotation.

**XFAIL**

 The test failed, but that is expected.  This is used for test formats which allow
 specifying that a test does not currently work, but wish to leave it in the test
 suite.

**XPASS**

 The test succeeded, but it was expected to fail.  This is used for tests which
 were specified as expected to fail, but are now succeeding (generally because
 the feature they test was broken and has been fixed).

**FAIL**

 The test failed.

**UNRESOLVED**

 The test result could not be determined.  For example, this occurs when the test
 could not be run, the test itself is invalid, or the test was interrupted.

**UNSUPPORTED**

 The test is not supported in this environment.  This is used by test formats
 which can report unsupported tests.

**TIMEOUT**

 The test was run, but it timed out before it was able to complete. This is
 considered a failure.

Depending on the test format tests may produce additional information about
their status (generally only for failures).  See the :ref:`output-options`
section for more information.

.. _lit-infrastructure:

LIT INFRASTRUCTURE
------------------

This section describes the :program:`lit` testing architecture for users interested in
creating a new :program:`lit` testing implementation, or extending an existing one.

:program:`lit` proper is primarily an infrastructure for discovering and running
arbitrary tests, and to expose a single convenient interface to these
tests. :program:`lit` itself doesn't know how to run tests, rather this logic is
defined by *test suites*.

TEST SUITES
~~~~~~~~~~~

As described in :ref:`test-discovery`, tests are always located inside a *test
suite*.  Test suites serve to define the format of the tests they contain, the
logic for finding those tests, and any additional information to run the tests.

:program:`lit` identifies test suites as directories containing ``lit.cfg`` or
``lit.site.cfg`` files (see also :option:`--config-prefix`).  Test suites are
initially discovered by recursively searching up the directory hierarchy for
all the input files passed on the command line.  You can use
:option:`--show-suites` to display the discovered test suites at startup.

Once a test suite is discovered, its config file is loaded.  Config files
themselves are Python modules which will be executed.  When the config file is
executed, two important global variables are predefined:

**lit_config**

 The global **lit** configuration object (a *LitConfig* instance), which defines
 the builtin test formats, global configuration parameters, and other helper
 routines for implementing test configurations.

**config**

 This is the config object (a *TestingConfig* instance) for the test suite,
 which the config file is expected to populate.  The following variables are also
 available on the *config* object, some of which must be set by the config and
 others are optional or predefined:

 **name** *[required]* The name of the test suite, for use in reports and
 diagnostics.

 **test_format** *[required]* The test format object which will be used to
 discover and run tests in the test suite.  Generally this will be a builtin test
 format available from the *lit.formats* module.

 **test_source_root** The filesystem path to the test suite root.  For out-of-dir
 builds this is the directory that will be scanned for tests.

 **test_exec_root** For out-of-dir builds, the path to the test suite root inside
 the object directory.  This is where tests will be run and temporary output files
 placed.

 **environment** A dictionary representing the environment to use when executing
 tests in the suite.

 **standalone_tests** When true, mark a directory with tests expected to be run
 standalone. Test discovery is disabled for that directory. *lit.suffixes* and
 *lit.excludes* must be empty when this variable is true.

 **suffixes** For **lit** test formats which scan directories for tests, this
 variable is a list of suffixes to identify test files.  Used by: *ShTest*.

 **substitutions** For **lit** test formats which substitute variables into a test
 script, the list of substitutions to perform.  Used by: *ShTest*.

 **unsupported** Mark an unsupported directory, all tests within it will be
 reported as unsupported.  Used by: *ShTest*.

 **parent** The parent configuration, this is the config object for the directory
 containing the test suite, or None.

 **root** The root configuration.  This is the top-most :program:`lit` configuration in
 the project.

 **pipefail** Normally a test using a shell pipe fails if any of the commands
 on the pipe fail. If this is not desired, setting this variable to false
 makes the test fail only if the last command in the pipe fails.

 **available_features** A set of features that can be used in `XFAIL`,
 `REQUIRES`, and `UNSUPPORTED` directives.

TEST DISCOVERY
~~~~~~~~~~~~~~

Once test suites are located, :program:`lit` recursively traverses the source
directory (following *test_source_root*) looking for tests.  When :program:`lit`
enters a sub-directory, it first checks to see if a nested test suite is
defined in that directory.  If so, it loads that test suite recursively,
otherwise it instantiates a local test config for the directory (see
:ref:`local-configuration-files`).

Tests are identified by the test suite they are contained within, and the
relative path inside that suite.  Note that the relative path may not refer to
an actual file on disk; some test formats (such as *GoogleTest*) define
"virtual tests" which have a path that contains both the path to the actual
test file and a subpath to identify the virtual test.

.. _local-configuration-files:

LOCAL CONFIGURATION FILES
~~~~~~~~~~~~~~~~~~~~~~~~~

When :program:`lit` loads a subdirectory in a test suite, it instantiates a
local test configuration by cloning the configuration for the parent directory
--- the root of this configuration chain will always be a test suite.  Once the
test configuration is cloned :program:`lit` checks for a *lit.local.cfg* file
in the subdirectory.  If present, this file will be loaded and can be used to
specialize the configuration for each individual directory.  This facility can
be used to define subdirectories of optional tests, or to change other
configuration parameters --- for example, to change the test format, or the
suffixes which identify test files.

SUBSTITUTIONS
~~~~~~~~~~~~~

:program:`lit` allows patterns to be substituted inside RUN commands. It also
provides the following base set of substitutions, which are defined in
TestRunner.py:

 ======================= ==============
  Macro                   Substitution
 ======================= ==============
 %s                      source path (path to the file currently being run)
 %S                      source dir (directory of the file currently being run)
 %p                      same as %S
 %{pathsep}              path separator
 %{fs-src-root}          root component of file system paths pointing to the LLVM checkout
 %{fs-tmp-root}          root component of file system paths pointing to the test's temporary directory
 %{fs-sep}               file system path separator
 %t                      temporary file name unique to the test
 %basename_t             The last path component of %t but without the ``.tmp`` extension
 %T                      parent directory of %t (not unique, deprecated, do not use)
 %%                      %
 %/s                     %s but ``\`` is replaced by ``/``
 %/S                     %S but ``\`` is replaced by ``/``
 %/p                     %p but ``\`` is replaced by ``/``
 %/t                     %t but ``\`` is replaced by ``/``
 %/T                     %T but ``\`` is replaced by ``/``
 %{s:real}               %s after expanding all symbolic links and substitute drives
 %{S:real}               %S after expanding all symbolic links and substitute drives
 %{p:real}               %p after expanding all symbolic links and substitute drives
 %{t:real}               %t after expanding all symbolic links and substitute drives
 %{T:real}               %T after expanding all symbolic links and substitute drives
 %{/s:real}              %/s after expanding all symbolic links and substitute drives
 %{/S:real}              %/S after expanding all symbolic links and substitute drives
 %{/p:real}              %/p after expanding all symbolic links and substitute drives
 %{/t:real}              %/t after expanding all symbolic links and substitute drives
 %{/T:real}              %/T after expanding all symbolic links and substitute drives
 %{/s:regex_replacement} %/s but escaped for use in the replacement of a ``s@@@`` command in sed
 %{/S:regex_replacement} %/S but escaped for use in the replacement of a ``s@@@`` command in sed
 %{/p:regex_replacement} %/p but escaped for use in the replacement of a ``s@@@`` command in sed
 %{/t:regex_replacement} %/t but escaped for use in the replacement of a ``s@@@`` command in sed
 %{/T:regex_replacement} %/T but escaped for use in the replacement of a ``s@@@`` command in sed
 %:s                     On Windows, %/s but a ``:`` is removed if its the second character.
                         Otherwise, %s but with a single leading ``/`` removed.
 %:S                     On Windows, %/S but a ``:`` is removed if its the second character.
                         Otherwise, %S but with a single leading ``/`` removed.
 %:p                     On Windows, %/p but a ``:`` is removed if its the second character.
                         Otherwise, %p but with a single leading ``/`` removed.
 %:t                     On Windows, %/t but a ``:`` is removed if its the second character.
                         Otherwise, %t but with a single leading ``/`` removed.
 %:T                     On Windows, %/T but a ``:`` is removed if its the second character.
                         Otherwise, %T but with a single leading ``/`` removed.
 ======================= ==============

Other substitutions are provided that are variations on this base set and
further substitution patterns can be defined by each test module. See the
modules :ref:`local-configuration-files`.

More detailed information on substitutions can be found in the
:doc:`../TestingGuide`.

TEST RUN OUTPUT FORMAT
~~~~~~~~~~~~~~~~~~~~~~

The :program:`lit` output for a test run conforms to the following schema, in
both short and verbose modes (although in short mode no PASS lines will be
shown).  This schema has been chosen to be relatively easy to reliably parse by
a machine (for example in buildbot log scraping), and for other tools to
generate.

Each test result is expected to appear on a line that matches:

.. code-block:: none

  <result code>: <test name> (<progress info>)

where ``<result-code>`` is a standard test result such as PASS, FAIL, XFAIL,
XPASS, UNRESOLVED, or UNSUPPORTED.  The performance result codes of IMPROVED and
REGRESSED are also allowed.

The ``<test name>`` field can consist of an arbitrary string containing no
newline.

The ``<progress info>`` field can be used to report progress information such
as (1/300) or can be empty, but even when empty the parentheses are required.

Each test result may include additional (multiline) log information in the
following format:

.. code-block:: none

  <log delineator> TEST '(<test name>)' <trailing delineator>
  ... log message ...
  <log delineator>

where ``<test name>`` should be the name of a preceding reported test, ``<log
delineator>`` is a string of "*" characters *at least* four characters long
(the recommended length is 20), and ``<trailing delineator>`` is an arbitrary
(unparsed) string.

The following is an example of a test run output which consists of four tests A,
B, C, and D, and a log message for the failing test C:

.. code-block:: none

  PASS: A (1 of 4)
  PASS: B (2 of 4)
  FAIL: C (3 of 4)
  ******************** TEST 'C' FAILED ********************
  Test 'C' failed as a result of exit code 1.
  ********************
  PASS: D (4 of 4)

DEFAULT FEATURES
~~~~~~~~~~~~~~~~~

For convenience :program:`lit` automatically adds **available_features** for
some common use cases.

:program:`lit` adds a feature based on the operating system being built on, for
example: `system-darwin`, `system-linux`, etc. :program:`lit` also
automatically adds a feature based on the current architecture, for example
`target-x86_64`, `target-aarch64`, etc.

When building with sanitizers enabled, :program:`lit` automatically adds the
short name of the sanitizer, for example: `asan`, `tsan`, etc.

To see the full list of features that can be added, see
*llvm/utils/lit/lit/llvm/config.py*.

LIT EXAMPLE TESTS
~~~~~~~~~~~~~~~~~

The :program:`lit` distribution contains several example implementations of
test suites in the *ExampleTests* directory.

SEE ALSO
--------

valgrind(1)
