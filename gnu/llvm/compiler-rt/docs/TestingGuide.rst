========================================
Compiler-rt Testing Infrastructure Guide
========================================

.. contents::
   :local:

Overview
========

This document is the reference manual for the compiler-rt modifications to the
testing infrastructure. Documentation for the infrastructure itself can be found at
:ref:`llvm_testing_guide`.

LLVM testing infrastructure organization
========================================

The compiler-rt testing infrastructure contains regression tests which are run
as part of the usual ``make check-all`` and are expected to always pass -- they
should be run before every commit.

Quick start
===========

The regressions tests are in the "compiler-rt" module and are normally checked
out in the directory ``llvm/projects/compiler-rt/test``. Use ``make check-all``
to run the regression tests after building compiler-rt.

REQUIRES, XFAIL, etc.
---------------------

Sometimes it is necessary to restrict a test to a specific target or mark it as
an "expected fail" or XFAIL. This is normally achieved using ``REQUIRES:`` or
``XFAIL:`` and the ``target=<target-triple>`` feature, typically with a regular
expression matching an appropriate substring of the triple. Unfortunately, the
behaviour of this is somewhat quirky in compiler-rt. There are two main
pitfalls to avoid.

The first pitfall is that these regular expressions may inadvertently match
more triples than expected. For example, ``XFAIL: target=mips{{.*}}`` matches
``mips-linux-gnu``, ``mipsel-linux-gnu``, ``mips64-linux-gnu``, and
``mips64el-linux-gnu``. Including a trailing ``-`` such as in 
``XFAIL: target=mips-{{.*}}`` can help to mitigate this quirk but even that has
issues as described below.

The second pitfall is that the default target triple is often inappropriate for
compiler-rt tests since compiler-rt tests may be compiled for multiple targets.
For example, a typical build on an ``x86_64-linux-gnu`` host will often run the
tests for both x86_64 and i386. In this situation ``XFAIL: target=x86_64{{{.*}}``
will mark both the x86_64 and i386 tests as an expected failure while 
``XFAIL: target=i386{{.*}}`` will have no effect at all.

To remedy both pitfalls, compiler-rt tests provide a feature string which can
be used to specify a single target. This string is of the form
``target-is-${arch}`` where ``${arch}}`` is one of the values from the
following lines of the CMake output::

  -- Compiler-RT supported architectures: x86_64;i386
  -- Builtin supported architectures: i386;x86_64

So for example ``XFAIL: target-is-x86_64`` will mark a test as expected to fail
on x86_64 without also affecting the i386 test and ``XFAIL: target-is-i386``
will mark a test as expected to fail on i386 even if the default target triple
is ``x86_64-linux-gnu``. Directives that use these ``target-is-${arch}`` string
require exact matches so ``XFAIL: target-is-mips``,
``XFAIL: target-is-mipsel``, ``XFAIL: target-is-mips64``, and
``XFAIL: target-is-mips64el`` all refer to different MIPS targets.
