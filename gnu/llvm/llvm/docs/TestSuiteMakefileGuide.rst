======================================
test-suite Makefile Guide (deprecated)
======================================

.. contents::
    :local:

Overview
========

First, all tests are executed within the LLVM object directory tree.
They *are not* executed inside of the LLVM source tree. This is because
the test suite creates temporary files during execution.

To run the test suite, you need to use the following steps:

#. Check out the ``test-suite`` module with:

   .. code-block:: bash

       % git clone https://github.com/llvm/llvm-test-suite.git test-suite

#. FIXME: these directions are outdated and won't work. Figure out
   what the correct thing to do is, and write it down here.

#. Configure and build ``llvm``.

#. Configure and build ``llvm-gcc``.

#. Install ``llvm-gcc`` somewhere.

#. *Re-configure* ``llvm`` from the top level of each build tree (LLVM
   object directory tree) in which you want to run the test suite, just
   as you do before building LLVM.

   During the *re-configuration*, you must either: (1) have ``llvm-gcc``
   you just built in your path, or (2) specify the directory where your
   just-built ``llvm-gcc`` is installed using
   ``--with-llvmgccdir=$LLVM_GCC_DIR``.

   You must also tell the configure machinery that the test suite is
   available so it can be configured for your build tree:

   .. code-block:: bash

       % cd $LLVM_OBJ_ROOT ; $LLVM_SRC_ROOT/configure [--with-llvmgccdir=$LLVM_GCC_DIR]

   [Remember that ``$LLVM_GCC_DIR`` is the directory where you
   *installed* llvm-gcc, not its src or obj directory.]

#. You can now run the test suite from your build tree as follows:

   .. code-block:: bash

       % cd $LLVM_OBJ_ROOT/projects/test-suite
       % make

Note that the second and third steps only need to be done once. After
you have the suite checked out and configured, you don't need to do it
again (unless the test code or configure script changes).

Configuring External Tests
==========================

In order to run the External tests in the ``test-suite`` module, you
must specify *--with-externals*. This must be done during the
*re-configuration* step (see above), and the ``llvm`` re-configuration
must recognize the previously-built ``llvm-gcc``. If any of these is
missing or neglected, the External tests won't work.

* *--with-externals*

* *--with-externals=<directory>*

This tells LLVM where to find any external tests. They are expected to
be in specifically named subdirectories of <``directory``>. If
``directory`` is left unspecified, ``configure`` uses the default value
``/home/vadve/shared/benchmarks/speccpu2000/benchspec``. Subdirectory
names known to LLVM include:

* spec95

* speccpu2000

* speccpu2006

* povray31

Others are added from time to time, and can be determined from
``configure``.

Running Different Tests
=======================

In addition to the regular "whole program" tests, the ``test-suite``
module also provides a mechanism for compiling the programs in different
ways. If the variable TEST is defined on the ``gmake`` command line, the
test system will include a Makefile named
``TEST.<value of TEST variable>.Makefile``. This Makefile can modify
build rules to yield different results.

For example, the LLVM nightly tester uses ``TEST.nightly.Makefile`` to
create the nightly test reports. To run the nightly tests, run
``gmake TEST=nightly``.

There are several TEST Makefiles available in the tree. Some of them are
designed for internal LLVM research and will not work outside of the
LLVM research group. They may still be valuable, however, as a guide to
writing your own TEST Makefile for any optimization or analysis passes
that you develop with LLVM.

Generating Test Output
======================

There are a number of ways to run the tests and generate output. The
most simple one is simply running ``gmake`` with no arguments. This will
compile and run all programs in the tree using a number of different
methods and compare results. Any failures are reported in the output,
but are likely drowned in the other output. Passes are not reported
explicitly.

Somewhat better is running ``gmake TEST=sometest test``, which runs the
specified test and usually adds per-program summaries to the output
(depending on which sometest you use). For example, the ``nightly`` test
explicitly outputs TEST-PASS or TEST-FAIL for every test after each
program. Though these lines are still drowned in the output, it's easy
to grep the output logs in the Output directories.

Even better are the ``report`` and ``report.format`` targets (where
``format`` is one of ``html``, ``csv``, ``text`` or ``graphs``). The
exact contents of the report are dependent on which ``TEST`` you are
running, but the text results are always shown at the end of the run and
the results are always stored in the ``report.<type>.format`` file (when
running with ``TEST=<type>``). The ``report`` also generate a file
called ``report.<type>.raw.out`` containing the output of the entire
test run.

Writing Custom Tests for the test-suite
=======================================

Assuming you can run the test suite, (e.g.
"``gmake TEST=nightly report``" should work), it is really easy to run
optimizations or code generator components against every program in the
tree, collecting statistics or running custom checks for correctness. At
base, this is how the nightly tester works, it's just one example of a
general framework.

Lets say that you have an LLVM optimization pass, and you want to see
how many times it triggers. First thing you should do is add an LLVM
`statistic <ProgrammersManual.html#Statistic>`_ to your pass, which will
tally counts of things you care about.

Following this, you can set up a test and a report that collects these
and formats them for easy viewing. This consists of two files, a
"``test-suite/TEST.XXX.Makefile``" fragment (where XXX is the name of
your test) and a "``test-suite/TEST.XXX.report``" file that indicates
how to format the output into a table. There are many example reports of
various levels of sophistication included with the test suite, and the
framework is very general.

If you are interested in testing an optimization pass, check out the
"libcalls" test as an example. It can be run like this:

.. code-block:: bash

    % cd llvm/projects/test-suite/MultiSource/Benchmarks  # or some other level
    % make TEST=libcalls report

This will do a bunch of stuff, then eventually print a table like this:

::

    Name                                  | total | #exit |
    ...
    FreeBench/analyzer/analyzer           | 51    | 6     |
    FreeBench/fourinarow/fourinarow       | 1     | 1     |
    FreeBench/neural/neural               | 19    | 9     |
    FreeBench/pifft/pifft                 | 5     | 3     |
    MallocBench/cfrac/cfrac               | 1     | *     |
    MallocBench/espresso/espresso         | 52    | 12    |
    MallocBench/gs/gs                     | 4     | *     |
    Prolangs-C/TimberWolfMC/timberwolfmc  | 302   | *     |
    Prolangs-C/agrep/agrep                | 33    | 12    |
    Prolangs-C/allroots/allroots          | *     | *     |
    Prolangs-C/assembler/assembler        | 47    | *     |
    Prolangs-C/bison/mybison              | 74    | *     |
    ...

This basically is grepping the -stats output and displaying it in a
table. You can also use the "TEST=libcalls report.html" target to get
the table in HTML form, similarly for report.csv and report.tex.

The source for this is in ``test-suite/TEST.libcalls.*``. The format is
pretty simple: the Makefile indicates how to run the test (in this case,
"``opt -simplify-libcalls -stats``"), and the report contains one line
for each column of the output. The first value is the header for the
column and the second is the regex to grep the output of the command
for. There are lots of example reports that can do fancy stuff.
