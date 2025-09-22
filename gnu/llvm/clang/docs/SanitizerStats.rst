==============
SanitizerStats
==============

.. contents::
   :local:

Introduction
============

The sanitizers support a simple mechanism for gathering profiling statistics
to help understand the overhead associated with sanitizers.

How to build and run
====================

SanitizerStats can currently only be used with :doc:`ControlFlowIntegrity`.
In addition to ``-fsanitize=cfi*``, pass the ``-fsanitize-stats`` flag.
This will cause the program to count the number of times that each control
flow integrity check in the program fires.

At run time, set the ``SANITIZER_STATS_PATH`` environment variable to direct
statistics output to a file. The file will be written on process exit.
The following substitutions will be applied to the environment variable:

  - ``%b`` -- The executable basename.
  - ``%p`` -- The process ID.

You can also send the ``SIGUSR2`` signal to a process to make it write
sanitizer statistics immediately.

The ``sanstats`` program can be used to dump statistics. It takes as a
command line argument the path to a statistics file produced by a program
compiled with ``-fsanitize-stats``.

The output of ``sanstats`` is in four columns, separated by spaces. The first
column is the file and line number of the call site. The second column is
the function name. The third column is the type of statistic gathered (in
this case, the type of control flow integrity check). The fourth column is
the call count.

Example:

.. code-block:: console

    $ cat -n vcall.cc
         1 struct A {
         2   virtual void f() {}
         3 };
         4
         5 __attribute__((noinline)) void g(A *a) {
         6   a->f();
         7 }
         8
         9 int main() {
        10   A a;
        11   g(&a);
        12 }
    $ clang++ -fsanitize=cfi -fvisibility=hidden -flto -fuse-ld=gold vcall.cc -fsanitize-stats -g
    $ SANITIZER_STATS_PATH=a.stats ./a.out
    $ sanstats a.stats
    vcall.cc:6 _Z1gP1A cfi-vcall 1
