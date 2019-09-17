.. SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

libperf tutorial
================

Compile and install libperf from kernel sources
===============================================
.. code-block:: bash

  git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
  cd linux/tools/perf/lib
  make
  sudo make install prefix=/usr

Libperf object
==============
The libperf library provides several high level objects:

struct perf_cpu_map
  Provides a cpu list abstraction.

struct perf_thread_map
  Provides a thread list abstraction.

struct perf_evsel
  Provides an abstraction for single a perf event.

struct perf_evlist
  Gathers several struct perf_evsel object and performs functions on all of them.

The exported API binds these objects together,
for full reference see the libperf.7 man page.

Examples
========
Examples aim to explain libperf functionality on simple use cases.
They are based in on a checked out linux kernel git tree:

.. code-block:: bash

  $ cd tools/perf/lib/Documentation/tutorial/
  $ ls -d  ex-*
  ex-1-compile  ex-2-evsel-stat  ex-3-evlist-stat

ex-1-compile example
====================
This example shows the basic usage of *struct perf_cpu_map*,
how to create it and display its cpus:

.. code-block:: bash

  $ cd ex-1-compile/
  $ make
  gcc -o test test.c -lperf
  $ ./test
  0 1 2 3 4 5 6 7


The full code listing is here:

.. code-block:: c

   1 #include <perf/cpumap.h>
   2
   3 int main(int argc, char **Argv)
   4 {
   5         struct perf_cpu_map *cpus;
   6         int cpu, tmp;
   7
   8         cpus = perf_cpu_map__new(NULL);
   9
  10         perf_cpu_map__for_each_cpu(cpu, tmp, cpus)
  11                 fprintf(stdout, "%d ", cpu);
  12
  13         fprintf(stdout, "\n");
  14
  15         perf_cpu_map__put(cpus);
  16         return 0;
  17 }


First you need to include the proper header to have *struct perf_cpumap*
declaration and functions:

.. code-block:: c

   1 #include <perf/cpumap.h>


The *struct perf_cpumap* object is created by *perf_cpu_map__new* call.
The *NULL* argument asks it to populate the object with the current online CPUs list:

.. code-block:: c

   8         cpus = perf_cpu_map__new(NULL);

This is paired with a *perf_cpu_map__put*, that drops its reference at the end, possibly deleting it.

.. code-block:: c

  15         perf_cpu_map__put(cpus);

The iteration through the *struct perf_cpumap* CPUs is done using the *perf_cpu_map__for_each_cpu*
macro which requires 3 arguments:

- cpu  - the cpu numer
- tmp  - iteration helper variable
- cpus - the *struct perf_cpumap* object

.. code-block:: c

  10         perf_cpu_map__for_each_cpu(cpu, tmp, cpus)
  11                 fprintf(stdout, "%d ", cpu);

ex-2-evsel-stat example
=======================

TBD

ex-3-evlist-stat example
========================

TBD
