Testing
=======

Test Suite Structure
--------------------

The LLDB test suite consists of three different kinds of test:

* **Unit tests**: written in C++ using the googletest unit testing library.
* **Shell tests**: Integration tests that test the debugger through the command
  line. These tests interact with the debugger either through the command line
  driver or through ``lldb-test`` which is a tool that exposes the internal
  data structures in an easy-to-parse way for testing. Most people will know
  these as *lit tests* in LLVM, although lit is the test driver and ShellTest
  is the test format that uses ``RUN:`` lines. `FileCheck
  <https://llvm.org/docs/CommandGuide/FileCheck.html>`_ is used to verify
  the output.
* **API tests**: Integration tests that interact with the debugger through the
  SB API. These are written in Python and use LLDB's ``dotest.py`` testing
  framework on top of Python's `unittest
  <https://docs.python.org/3/library/unittest.html>`_.

All three test suites use ``lit`` (`LLVM Integrated Tester
<https://llvm.org/docs/CommandGuide/lit.html>`_ ) as the test driver. The test
suites can be run as a whole or separately.


Unit Tests
``````````

Unit tests are located under ``lldb/unittests``. If it's possible to test
something in isolation or as a single unit, you should make it a unit test.

Often you need instances of the core objects such as a debugger, target or
process, in order to test something meaningful. We already have a handful of
tests that have the necessary boiler plate, but this is something we could
abstract away and make it more user friendly.

Shell Tests
```````````

Shell tests are located under ``lldb/test/Shell``. These tests are generally
built around checking the output of ``lldb`` (the command line driver) or
``lldb-test`` using ``FileCheck``. Shell tests are generally small and fast to
write because they require little boilerplate.

``lldb-test`` is a relatively new addition to the test suite. It was the first
tool that was added that is designed for testing. Since then it has been
continuously extended with new subcommands, improving our test coverage. Among
other things you can use it to query lldb for symbol files, for object files
and breakpoints.

Obviously shell tests are great for testing the command line driver itself or
the subcomponents already exposed by lldb-test. But when it comes to LLDB's
vast functionality, most things can be tested both through the driver as well
as the Python API. For example, to test setting a breakpoint, you could do it
from the command line driver with ``b main`` or you could use the SB API and do
something like ``target.BreakpointCreateByName`` [#]_.

A good rule of thumb is to prefer shell tests when what is being tested is
relatively simple. Expressivity is limited compared to the API tests, which
means that you have to have a well-defined test scenario that you can easily
match with ``FileCheck``.

Another thing to consider are the binaries being debugged, which we call
inferiors. For shell tests, they have to be relatively simple. The
``dotest.py`` test framework has extensive support for complex build scenarios
and different variants, which is described in more detail below, while shell
tests are limited to single lines of shell commands with compiler and linker
invocations.

On the same topic, another interesting aspect of the shell tests is that there
you can often get away with a broken or incomplete binary, whereas the API
tests almost always require a fully functional executable. This enables testing
of (some) aspects of handling of binaries with non-native architectures or
operating systems.

Finally, the shell tests always run in batch mode. You start with some input
and the test verifies the output. The debugger can be sensitive to its
environment, such as the platform it runs on. It can be hard to express
that the same test might behave slightly differently on macOS and Linux.
Additionally, the debugger is an interactive tool, and the shell test provide
no good way of testing those interactive aspects, such as tab completion for
example.

API Tests
`````````

API tests are located under ``lldb/test/API``. They are run with the
``dotest.py``. Tests are written in Python and test binaries (inferiors) are
compiled with Make. The majority of API tests are end-to-end tests that compile
programs from source, run them, and debug the processes.

As mentioned before, ``dotest.py`` is LLDB's testing framework. The
implementation is located under ``lldb/packages/Python/lldbsuite``. We have
several extensions and custom test primitives on top of what's offered by
`unittest <https://docs.python.org/3/library/unittest.html>`_. Those can be
found  in
`lldbtest.py <https://github.com/llvm/llvm-project/blob/main/lldb/packages/Python/lldbsuite/test/lldbtest.py>`_.

Below is the directory layout of the `example API test
<https://github.com/llvm/llvm-project/tree/main/lldb/test/API/sample_test>`_.
The test directory will always contain a python file, starting with ``Test``.
Most of the tests are structured as a binary being debugged, so there will be
one or more source files and a ``Makefile``.

::

  sample_test
  ├── Makefile
  ├── TestSampleTest.py
  └── main.c

Let's start with the Python test file. Every test is its own class and can have
one or more test methods, that start with ``test_``.  Many tests define
multiple test methods and share a bunch of common code. For example, for a
fictive test that makes sure we can set breakpoints we might have one test
method that ensures we can set a breakpoint by address, on that sets a
breakpoint by name and another that sets the same breakpoint by file and line
number. The setup, teardown and everything else other than setting the
breakpoint could be shared.

Our testing framework also has a bunch of utilities that abstract common
operations, such as creating targets, setting breakpoints etc. When code is
shared across tests, we extract it into a utility in ``lldbutil``. It's always
worth taking a look at  `lldbutil
<https://github.com/llvm/llvm-project/blob/main/lldb/packages/Python/lldbsuite/test/lldbutil.py>`_
to see if there's a utility to simplify some of the testing boiler plate.
Because we can't always audit every existing test, this is doubly true when
looking at an existing test for inspiration.

It's possible to skip or `XFAIL
<https://ftp.gnu.org/old-gnu/Manuals/dejagnu-1.3/html_node/dejagnu_6.html>`_
tests using decorators. You'll see them a lot. The debugger can be sensitive to
things like the architecture, the host and target platform, the compiler
version etc. LLDB comes with a range of predefined decorators for these
configurations.

::

  @expectedFailureAll(archs=["aarch64"], oslist=["linux"]

Another great thing about these decorators is that they're very easy to extend,
it's even possible to define a function in a test case that determines whether
the test should be run or not.

::

  @skipTestIfFn(checking_function_name)

In addition to providing a lot more flexibility when it comes to writing the
test, the API test also allow for much more complex scenarios when it comes to
building inferiors. Every test has its own ``Makefile``, most of them only a
few lines long. A shared ``Makefile`` (``Makefile.rules``) with about a
thousand lines of rules takes care of most if not all of the boiler plate,
while individual make files can be used to build more advanced tests.

Here's an example of a simple ``Makefile`` used by the example test.

::

  C_SOURCES := main.c
  CFLAGS_EXTRAS := -std=c99

  include Makefile.rules

Finding the right variables to set can be tricky. You can always take a look at
`Makefile.rules <https://github.com/llvm/llvm-project/blob/main/lldb/packages/Python/lldbsuite/test/make/Makefile.rules>`_
but often it's easier to find an existing ``Makefile`` that does something
similar to what you want to do.

Another thing this enables is having different variants for the same test
case. By default, we run every test for two debug info formats, once with
DWARF from the object files and another with a dSYM on macOS or split
DWARF (DWO) on Linux. But there are many more things we can test
that are orthogonal to the test itself. On GreenDragon we have a matrix bot
that runs the test suite under different configurations, with older host
compilers and different DWARF versions.

As you can imagine, this quickly lead to combinatorial explosion in the number
of variants. It's very tempting to add more variants because it's an easy way
to increase test coverage. It doesn't scale. It's easy to set up, but increases
the runtime of the tests and has a large ongoing cost.

The test variants are most useful when developing a larger feature (e.g. support
for a new DWARF version). The test suite contains a large number of fairly
generic tests, so running the test suite with the feature enabled is a good way
to gain confidence that you haven't missed an important aspect. However, this
genericness makes them poor regression tests. Because it's not clear what a
specific test covers, a random modification to the test case can make it start
(or stop) testing a completely different part of your feature. And since these
tests tend to look very similar, it's easy for a simple bug to cause hundreds of
tests to fail in the same way.

For this reason, we recommend using test variants only while developing a new
feature. This can often be done by running the test suite with different
arguments -- without any modifications to the code. You can create a focused
test for any bug found that way. Often, there will be many tests failing, but a
lot of then will have the same root cause.  These tests will be easier to debug
and will not put undue burden on all other bots and developers.

In conclusion, you'll want to opt for an API test to test the API itself or
when you need the expressivity, either for the test case itself or for the
program being debugged. The fact that the API tests work with different
variants mean that more general tests should be API tests, so that they can be
run against the different variants.

Guidelines for API tests
^^^^^^^^^^^^^^^^^^^^^^^^

API tests are expected to be fast, reliable and maintainable. To achieve this
goal, API tests should conform to the following guidelines in addition to normal
good testing practices.

**Don't unnecessarily launch the test executable.**
    Launching a process and running to a breakpoint can often be the most
    expensive part of a test and should be avoided if possible. A large part
    of LLDB's functionality is available directly after creating an `SBTarget`
    of the test executable.

    The part of the SB API that can be tested with just a target includes
    everything that represents information about the executable and its
    debug information (e.g., `SBTarget`, `SBModule`, `SBSymbolContext`,
    `SBFunction`, `SBInstruction`, `SBCompileUnit`, etc.). For test executables
    written in languages with a type system that is mostly defined at compile
    time (e.g., C and C++) there is also usually no process necessary to test
    the `SBType`-related parts of the API. With those languages it's also
    possible to test `SBValue` by running expressions with
    `SBTarget.EvaluateExpression` or the ``expect_expr`` testing utility.

    Functionality that always requires a running process is everything that
    tests the `SBProcess`, `SBThread`, and `SBFrame` classes. The same is true
    for tests that exercise breakpoints, watchpoints and sanitizers.
    Languages such as Objective-C that have a dependency on a runtime
    environment also always require a running process.

**Don't unnecessarily include system headers in test sources.**
    Including external headers slows down the compilation of the test executable
    and it makes reproducing test failures on other operating systems or
    configurations harder.

**Avoid specifying test-specific compiler flags when including system headers.**
    If a test requires including a system header (e.g., a test for a libc++
    formatter includes a libc++ header), try to avoid specifying custom compiler
    flags if possible. Certain debug information formats such as ``gmodules``
    use a cache that is shared between all API tests and that contains
    precompiled system headers. If you add or remove a specific compiler flag
    in your test (e.g., adding ``-DFOO`` to the ``Makefile`` or ``self.build``
    arguments), then the test will not use the shared precompiled header cache
    and expensively recompile all system headers from scratch. If you depend on
    a specific compiler flag for the test, you can avoid this issue by either
    removing all system header includes or decorating the test function with
    ``@no_debug_info_test`` (which will avoid running all debug information
    variants including ``gmodules``).

**Test programs should be kept simple.**
    Test executables should do the minimum amount of work to bring the process
    into the state that is required for the test. Simulating a 'real' program
    that actually tries to do some useful task rarely helps with catching bugs
    and makes the test much harder to debug and maintain. The test programs
    should always be deterministic (i.e., do not generate and check against
    random test values).

**Identifiers in tests should be simple and descriptive.**
    Often test programs need to declare functions and classes which require
    choosing some form of identifier for them. These identifiers should always
    either be kept simple for small tests (e.g., ``A``, ``B``, ...) or have some
    descriptive name (e.g., ``ClassWithTailPadding``, ``inlined_func``, ...).
    Never choose identifiers that are already used anywhere else in LLVM or
    other programs (e.g., don't name a class  ``VirtualFileSystem``, a function
    ``llvm_unreachable``, or a namespace ``rapidxml``) as this will mislead
    people ``grep``'ing the LLVM repository for those strings.

**Prefer LLDB testing utilities over directly working with the SB API.**
    The ``lldbutil`` module and the ``TestBase`` class come with a large amount
    of utility functions that can do common test setup tasks (e.g., starting a
    test executable and running the process to a breakpoint). Using these
    functions not only keeps the test shorter and free of duplicated code, but
    they also follow best test suite practices and usually give much clearer
    error messages if something goes wrong. The test utilities also contain
    custom asserts and checks that should be preferably used (e.g.
    ``self.assertSuccess``).

**Prefer calling the SB API over checking command output.**
    Avoid writing your tests on top of ``self.expect(...)`` calls that check
    the output of LLDB commands and instead try calling into the SB API. Relying
    on LLDB commands makes changing (and improving) the output/syntax of
    commands harder and the resulting tests are often prone to accepting
    incorrect test results. Especially improved error messages that contain
    more information might cause these ``self.expect`` calls to unintentionally
    find the required ``substrs``. For example, the following ``self.expect``
    check will unexpectedly pass if it's ran as the first expression in a test:

::

    self.expect("expr 2 + 2", substrs=["0"])

When running the same command in LLDB the reason for the unexpected success
is that '0' is found in the name of the implicitly created result variable:

::

    (lldb) expr 2 + 2
    (int) $0 = 4
           ^ The '0' substring is found here.

A better way to write the test above would be using LLDB's testing function
``expect_expr`` will only pass if the expression produces a value of 0:

::

    self.expect_expr("2 + 2", result_value="0")

**Prefer using specific asserts over the generic assertTrue/assertFalse.**.
    The ``self.assertTrue``/``self.assertFalse`` functions should always be your
    last option as they give non-descriptive error messages. The test class has
    several expressive asserts such as ``self.assertIn`` that automatically
    generate an explanation how the received values differ from the expected
    ones. Check the documentation of Python's ``unittest`` module to see what
    asserts are available. LLDB also has a few custom asserts that are tailored
    to our own data types.

+-----------------------------------------------+-----------------------------------------------------------------+
| **Assert**                                    | **Description**                                                 |
+-----------------------------------------------+-----------------------------------------------------------------+
| ``assertSuccess``                             | Assert that an ``lldb.SBError`` is in the "success" state.      |
+-----------------------------------------------+-----------------------------------------------------------------+
| ``assertState``                               | Assert that two states (``lldb.eState*``) are equal.            |
+-----------------------------------------------+-----------------------------------------------------------------+
| ``assertStopReason``                          | Assert that two stop reasons (``lldb.eStopReason*``) are equal. |
+-----------------------------------------------+-----------------------------------------------------------------+

    If you can't find a specific assert that fits your needs and you fall back
    to a generic assert, make sure you put useful information into the assert's
    ``msg`` argument that helps explain the failure.

::

    # Bad. Will print a generic error such as 'False is not True'.
    self.assertTrue(expected_string in list_of_results)
    # Good. Will print expected_string and the contents of list_of_results.
    self.assertIn(expected_string, list_of_results)

**Do not use hard-coded line numbers in your test case.**

Instead, try to tag the line with some distinguishing pattern, and use the function line_number() defined in lldbtest.py which takes
filename and string_to_match as arguments and returns the line number.

As an example, take a look at test/API/functionalities/breakpoint/breakpoint_conditions/main.c which has these
two lines:

.. code-block:: c

        return c(val); // Find the line number of c's parent call here.

and

.. code-block:: c

    return val + 3; // Find the line number of function "c" here.

The Python test case TestBreakpointConditions.py uses the comment strings to find the line numbers during setUp(self) and use them
later on to verify that the correct breakpoint is being stopped on and that its parent frame also has the correct line number as
intended through the breakpoint condition.

**Take advantage of the unittest framework's decorator features.**

These features can be use to properly mark your test class or method for platform-specific tests, compiler specific, version specific.

As an example, take a look at test/API/lang/c/forward/TestForwardDeclaration.py which has these lines:

.. code-block:: python

    @no_debug_info_test
    @skipIfDarwin
    @skipIf(compiler=no_match("clang"))
    @skipIf(compiler_version=["<", "8.0"])
    @expectedFailureAll(oslist=["windows"])
    def test_debug_names(self):
        """Test that we are able to find complete types when using DWARF v5
        accelerator tables"""
        self.do_test(dict(CFLAGS_EXTRAS="-gdwarf-5 -gpubnames"))

This tells the test harness that unless we are running "linux" and clang version equal & above 8.0, the test should be skipped.

**Class-wise cleanup after yourself.**

TestBase.tearDownClass(cls) provides a mechanism to invoke the platform-specific cleanup after finishing with a test class. A test
class can have more than one test methods, so the tearDownClass(cls) method gets run after all the test methods have been executed by
the test harness.

The default cleanup action performed by the packages/Python/lldbsuite/test/lldbtest.py module invokes the "make clean" os command.

If this default cleanup is not enough, individual class can provide an extra cleanup hook with a class method named classCleanup ,
for example, in test/API/terminal/TestSTTYBeforeAndAfter.py:

.. code-block:: python

    @classmethod
    def classCleanup(cls):
        """Cleanup the test byproducts."""
        cls.RemoveTempFile("child_send1.txt")


The 'child_send1.txt' file gets generated during the test run, so it makes sense to explicitly spell out the action in the same
TestSTTYBeforeAndAfter.py file to do the cleanup instead of artificially adding it as part of the default cleanup action which serves to
cleanup those intermediate and a.out files.

CI
--

LLVM Buildbot is the place where volunteers provide machines for building and
testing. Everyone can `add a buildbot for LLDB <https://llvm.org/docs/HowToAddABuilder.html>`_.

An overview of all LLDB builders can be found here:

`https://lab.llvm.org/buildbot/#/builders?tags=lldb <https://lab.llvm.org/buildbot/#/builders?tags=lldb>`_

Building and testing for macOS uses a different platform called GreenDragon. It
has a dedicated tab for LLDB: `https://green.lab.llvm.org/green/view/LLDB/
<https://green.lab.llvm.org/green/view/LLDB/>`_


Running The Tests
-----------------

.. note::

   On Windows any invocations of python should be replaced with python_d, the
   debug interpreter, when running the test suite against a debug version of
   LLDB.

.. note::

   On NetBSD you must export ``LD_LIBRARY_PATH=$PWD/lib`` in your environment.
   This is due to lack of the ``$ORIGIN`` linker feature.

Running the Full Test Suite
```````````````````````````

The easiest way to run the LLDB test suite is to use the ``check-lldb`` build
target.

::

   $ ninja check-lldb

Changing Test Suite Options
```````````````````````````

By default, the ``check-lldb`` target builds the test programs with the same
compiler that was used to build LLDB. To build the tests with a different
compiler, you can set the ``LLDB_TEST_COMPILER`` CMake variable.

You can also add to the test runner options by setting the
``LLDB_TEST_USER_ARGS`` CMake variable. This variable uses ``;`` to separate
items which must be separate parts of the runner's command line.

It is possible to customize the architecture of the test binaries and compiler
used by appending ``-A`` and ``-C`` options respectively. For example, to test
LLDB against 32-bit binaries built with a custom version of clang, do:

::

   $ cmake -DLLDB_TEST_USER_ARGS="-A;i386;-C;/path/to/custom/clang" -G Ninja
   $ ninja check-lldb

Note that multiple ``-A`` and ``-C`` flags can be specified to
``LLDB_TEST_USER_ARGS``.

If you want to change the LLDB settings that tests run with then you can set
the ``--setting`` option of the test runner via this same variable. For example
``--setting;target.disable-aslr=true``.

For a full list of test runner options, see
``<build-dir>/bin/lldb-dotest --help``.

Running a Single Test Suite
```````````````````````````

Each test suite can be run separately, similar to running the whole test suite
with ``check-lldb``.

* Use ``check-lldb-unit`` to run just the unit tests.
* Use ``check-lldb-api`` to run just the SB API tests.
* Use ``check-lldb-shell`` to run just the shell tests.

You can run specific subdirectories by appending the directory name to the
target. For example, to run all the tests in ``ObjectFile``, you can use the
target ``check-lldb-shell-objectfile``. However, because the unit tests and API
tests don't actually live under ``lldb/test``, this convenience is only
available for the shell tests.

Running a Single Test
`````````````````````

The recommended way to run a single test is by invoking the lit driver with a
filter. This ensures that the test is run with the same configuration as when
run as part of a test suite.

::

   $ ./bin/llvm-lit -sv <llvm-project-root>/lldb/test --filter <test>


Because lit automatically scans a directory for tests, it's also possible to
pass a subdirectory to run a specific subset of the tests.

::

   $ ./bin/llvm-lit -sv <llvm-project-root>/lldb/test/Shell/Commands/CommandScriptImmediateOutput


For the SB API tests it is possible to forward arguments to ``dotest.py`` by
passing ``--param`` to lit and setting a value for ``dotest-args``.

::

   $ ./bin/llvm-lit -sv <llvm-project-root>/lldb/test --param dotest-args='-C gcc'


Below is an overview of running individual test in the unit and API test suites
without going through the lit driver.

Running a Specific Test or Set of Tests: API Tests
``````````````````````````````````````````````````

In addition to running all the LLDB test suites with the ``check-lldb`` CMake
target above, it is possible to run individual LLDB tests. If you have a CMake
build you can use the ``lldb-dotest`` binary, which is a wrapper around
``dotest.py`` that passes all the arguments configured by CMake.

Alternatively, you can use ``dotest.py`` directly, if you want to run a test
one-off with a different configuration.

For example, to run the test cases defined in TestInferiorCrashing.py, run:

::

   $ ./bin/lldb-dotest -p TestInferiorCrashing.py

::

   $ cd $lldb/test
   $ python dotest.py --executable <path-to-lldb> -p TestInferiorCrashing.py ../packages/Python/lldbsuite/test

If the test is not specified by name (e.g. if you leave the ``-p`` argument
off),  all tests in that directory will be executed:


::

   $ ./bin/lldb-dotest functionalities/data-formatter

::

   $ python dotest.py --executable <path-to-lldb> functionalities/data-formatter

Many more options that are available. To see a list of all of them, run:

::

   $ python dotest.py -h


Running a Specific Test or Set of Tests: Unit Tests
```````````````````````````````````````````````````

The unit tests are simple executables, located in the build directory under ``tools/lldb/unittests``.

To run them, just run the test binary, for example, to run all the Host tests:

::

   $ ./tools/lldb/unittests/Host/HostTests


To run a specific test, pass a filter, for example:

::

   $ ./tools/lldb/unittests/Host/HostTests --gtest_filter=SocketTest.DomainListenConnectAccept


Running the Test Suite Remotely
```````````````````````````````

Running the test-suite remotely is similar to the process of running a local
test suite, but there are two things to have in mind:

1. You must have the lldb-server running on the remote system, ready to accept
   multiple connections. For more information on how to setup remote debugging
   see the Remote debugging page.
2. You must tell the test-suite how to connect to the remote system. This is
   achieved using the ``--platform-name``, ``--platform-url`` and
   ``--platform-working-dir`` parameters to ``dotest.py``. These parameters
   correspond to the platform select and platform connect LLDB commands. You
   will usually also need to specify the compiler and architecture for the
   remote system.

Currently, running the remote test suite is supported only with ``dotest.py`` (or
dosep.py with a single thread), but we expect this issue to be addressed in the
near future.

Running tests in QEMU System Emulation Environment
``````````````````````````````````````````````````

QEMU can be used to test LLDB in an emulation environment in the absence of
actual hardware. :doc:`/use/qemu-testing` describes how to setup an
emulation environment using QEMU helper scripts found in
``llvm-project/lldb/scripts/lldb-test-qemu``. These scripts currently
work with Arm or AArch64, but support for other architectures can be added easily.

Debugging Test Failures
-----------------------

On non-Windows platforms, you can use the ``-d`` option to ``dotest.py`` which
will cause the script to print out the pid of the test and wait for a while
until a debugger is attached. Then run ``lldb -p <pid>`` to attach.

To instead debug a test's python source, edit the test and insert ``import pdb; pdb.set_trace()`` or ``breakpoint()`` (Python 3 only) at the point you want to start debugging. The ``breakpoint()`` command can be used for any LLDB Python script, not just for API tests.

In addition to pdb's debugging facilities, lldb commands can be executed with the
help of a pdb alias. For example ``lldb bt`` and ``lldb v some_var``. Add this
line to your ``~/.pdbrc``:

::

   alias lldb self.dbg.HandleCommand("%*")

Debugging Test Failures on Windows
``````````````````````````````````

On Windows, it is strongly recommended to use Python Tools for Visual Studio
for debugging test failures. It can seamlessly step between native and managed
code, which is very helpful when you need to step through the test itself, and
then into the LLDB code that backs the operations the test is performing.

A quick guide to getting started with PTVS is as follows:

#. Install PTVS
#. Create a Visual Studio Project for the Python code.
    #. Go to File -> New -> Project -> Python -> From Existing Python Code.
    #. Choose llvm/tools/lldb as the directory containing the Python code.
    #. When asked where to save the .pyproj file, choose the folder ``llvm/tools/lldb/pyproj``. This is a special folder that is ignored by the ``.gitignore`` file, since it is not checked in.
#. Set test/dotest.py as the startup file
#. Make sure there is a Python Environment installed for your distribution. For example, if you installed Python to ``C:\Python35``, PTVS needs to know that this is the interpreter you want to use for running the test suite.
    #. Go to Tools -> Options -> Python Tools -> Environment Options
    #. Click Add Environment, and enter Python 3.5 Debug for the name. Fill out the values correctly.
#. Configure the project to use this debug interpreter.
    #. Right click the Project node in Solution Explorer.
    #. In the General tab, Make sure Python 3.5 Debug is the selected Interpreter.
    #. In Debug/Search Paths, enter the path to your ninja/lib/site-packages directory.
    #. In Debug/Environment Variables, enter ``VCINSTALLDIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\``.
    #. If you want to enabled mixed mode debugging, check Enable native code debugging (this slows down debugging, so enable it only on an as-needed basis.)
#. Set the command line for the test suite to run.
    #. Right click the project in solution explorer and choose the Debug tab.
    #. Enter the arguments to dotest.py.
    #. Example command options:

::

   --arch=i686
   # Path to debug lldb.exe
   --executable D:/src/llvmbuild/ninja/bin/lldb.exe
   # Directory to store log files
   -s D:/src/llvmbuild/ninja/lldb-test-traces
   -u CXXFLAGS -u CFLAGS
   # If a test crashes, show JIT debugging dialog.
   --enable-crash-dialog
   # Path to release clang.exe
   -C d:\src\llvmbuild\ninja_release\bin\clang.exe
   # Path to the particular test you want to debug.
   -p TestPaths.py
   # Root of test tree
   D:\src\llvm\tools\lldb\packages\Python\lldbsuite\test

::

   --arch=i686 --executable D:/src/llvmbuild/ninja/bin/lldb.exe -s D:/src/llvmbuild/ninja/lldb-test-traces -u CXXFLAGS -u CFLAGS --enable-crash-dialog -C d:\src\llvmbuild\ninja_release\bin\clang.exe -p TestPaths.py D:\src\llvm\tools\lldb\packages\Python\lldbsuite\test --no-multiprocess

.. [#] `https://lldb.llvm.org/python_reference/lldb.SBTarget-class.html#BreakpointCreateByName <https://lldb.llvm.org/python_reference/lldb.SBTarget-class.html#BreakpointCreateByName>`_
