==================================
Benchmarking tips
==================================


Introduction
============

For benchmarking a patch we want to reduce all possible sources of
noise as much as possible. How to do that is very OS dependent.

Note that low noise is required, but not sufficient. It does not
exclude measurement bias.
See `"Producing Wrong Data Without Doing Anything Obviously Wrong!" by Mytkowicz, Diwan, Hauswith and Sweeney (ASPLOS 2009) <https://users.cs.northwestern.edu/~robby/courses/322-2013-spring/mytkowicz-wrong-data.pdf>`_
for example.

General
================================

* Use a high resolution timer, e.g. perf under linux.

* Run the benchmark multiple times to be able to recognize noise.

* Disable as many processes or services as possible on the target system.

* Disable frequency scaling, turbo boost and address space
  randomization (see OS specific section).

* Static link if the OS supports it. That avoids any variation that
  might be introduced by loading dynamic libraries. This can be done
  by passing ``-DLLVM_BUILD_STATIC=ON`` to cmake.

* Try to avoid storage. On some systems you can use tmpfs. Putting the
  program, inputs and outputs on tmpfs avoids touching a real storage
  system, which can have a pretty big variability.

  To mount it (on linux and freebsd at least)::

    mount -t tmpfs -o size=<XX>g none dir_to_mount

Linux
=====

* Disable address space randomization::

    echo 0 > /proc/sys/kernel/randomize_va_space

* Set scaling_governor to performance::

   for i in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
   do
     echo performance > $i
   done

* Use https://github.com/lpechacek/cpuset to reserve cpus for just the
  program you are benchmarking. If using perf, leave at least 2 cores
  so that perf runs in one and your program in another::

    cset shield -c N1,N2 -k on

  This will move all threads out of N1 and N2. The ``-k on`` means
  that even kernel threads are moved out.

* Disable the SMT pair of the cpus you will use for the benchmark. The
  pair of cpu N can be found in
  ``/sys/devices/system/cpu/cpuN/topology/thread_siblings_list`` and
  disabled with::

    echo 0 > /sys/devices/system/cpu/cpuX/online


* Run the program with::

    cset shield --exec -- perf stat -r 10 <cmd>

  This will run the command after ``--`` in the isolated cpus. The
  particular perf command runs the ``<cmd>`` 10 times and reports
  statistics.

With these in place you can expect perf variations of less than 0.1%.

Linux Intel
-----------

* Disable turbo mode::

    echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
