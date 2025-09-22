=================
SanitizerCoverage
=================

.. contents::
   :local:

Introduction
============

LLVM has a simple code coverage instrumentation built in (SanitizerCoverage).
It inserts calls to user-defined functions on function-, basic-block-, and edge- levels.
Default implementations of those callbacks are provided and implement
simple coverage reporting and visualization,
however if you need *just* coverage visualization you may want to use
:doc:`SourceBasedCodeCoverage <SourceBasedCodeCoverage>` instead.

Tracing PCs with guards
=======================

With ``-fsanitize-coverage=trace-pc-guard`` the compiler will insert the following code
on every edge:

.. code-block:: none

   __sanitizer_cov_trace_pc_guard(&guard_variable)

Every edge will have its own `guard_variable` (uint32_t).

The compiler will also insert calls to a module constructor:

.. code-block:: c++

   // The guards are [start, stop).
   // This function will be called at least once per DSO and may be called
   // more than once with the same values of start/stop.
   __sanitizer_cov_trace_pc_guard_init(uint32_t *start, uint32_t *stop);

With an additional ``...=trace-pc,indirect-calls`` flag
``__sanitizer_cov_trace_pc_indirect(void *callee)`` will be inserted on every indirect call.

The functions `__sanitizer_cov_trace_pc_*` should be defined by the user.

Example:

.. code-block:: c++

  // trace-pc-guard-cb.cc
  #include <stdint.h>
  #include <stdio.h>
  #include <sanitizer/coverage_interface.h>

  // This callback is inserted by the compiler as a module constructor
  // into every DSO. 'start' and 'stop' correspond to the
  // beginning and end of the section with the guards for the entire
  // binary (executable or DSO). The callback will be called at least
  // once per DSO and may be called multiple times with the same parameters.
  extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t *start,
                                                      uint32_t *stop) {
    static uint64_t N;  // Counter for the guards.
    if (start == stop || *start) return;  // Initialize only once.
    printf("INIT: %p %p\n", start, stop);
    for (uint32_t *x = start; x < stop; x++)
      *x = ++N;  // Guards should start from 1.
  }

  // This callback is inserted by the compiler on every edge in the
  // control flow (some optimizations apply).
  // Typically, the compiler will emit the code like this:
  //    if(*guard)
  //      __sanitizer_cov_trace_pc_guard(guard);
  // But for large functions it will emit a simple call:
  //    __sanitizer_cov_trace_pc_guard(guard);
  extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
    if (!*guard) return;  // Duplicate the guard check.
    // If you set *guard to 0 this code will not be called again for this edge.
    // Now you can get the PC and do whatever you want:
    //   store it somewhere or symbolize it and print right away.
    // The values of `*guard` are as you set them in
    // __sanitizer_cov_trace_pc_guard_init and so you can make them consecutive
    // and use them to dereference an array or a bit vector.
    void *PC = __builtin_return_address(0);
    char PcDescr[1024];
    // This function is a part of the sanitizer run-time.
    // To use it, link with AddressSanitizer or other sanitizer.
    __sanitizer_symbolize_pc(PC, "%p %F %L", PcDescr, sizeof(PcDescr));
    printf("guard: %p %x PC %s\n", guard, *guard, PcDescr);
  }

.. code-block:: c++

  // trace-pc-guard-example.cc
  void foo() { }
  int main(int argc, char **argv) {
    if (argc > 1) foo();
  }

.. code-block:: console

  clang++ -g  -fsanitize-coverage=trace-pc-guard trace-pc-guard-example.cc -c
  clang++ trace-pc-guard-cb.cc trace-pc-guard-example.o -fsanitize=address
  ASAN_OPTIONS=strip_path_prefix=`pwd`/ ./a.out

.. code-block:: console

  INIT: 0x71bcd0 0x71bce0
  guard: 0x71bcd4 2 PC 0x4ecd5b in main trace-pc-guard-example.cc:2
  guard: 0x71bcd8 3 PC 0x4ecd9e in main trace-pc-guard-example.cc:3:7

.. code-block:: console

  ASAN_OPTIONS=strip_path_prefix=`pwd`/ ./a.out with-foo


.. code-block:: console

  INIT: 0x71bcd0 0x71bce0
  guard: 0x71bcd4 2 PC 0x4ecd5b in main trace-pc-guard-example.cc:3
  guard: 0x71bcdc 4 PC 0x4ecdc7 in main trace-pc-guard-example.cc:4:17
  guard: 0x71bcd0 1 PC 0x4ecd20 in foo() trace-pc-guard-example.cc:2:14

Inline 8bit-counters
====================

**Experimental, may change or disappear in future**

With ``-fsanitize-coverage=inline-8bit-counters`` the compiler will insert
inline counter increments on every edge.
This is similar to ``-fsanitize-coverage=trace-pc-guard`` but instead of a
callback the instrumentation simply increments a counter.

Users need to implement a single function to capture the counters at startup.

.. code-block:: c++

  extern "C"
  void __sanitizer_cov_8bit_counters_init(char *start, char *end) {
    // [start,end) is the array of 8-bit counters created for the current DSO.
    // Capture this array in order to read/modify the counters.
  }


Inline bool-flag
================

**Experimental, may change or disappear in future**

With ``-fsanitize-coverage=inline-bool-flag`` the compiler will insert
setting an inline boolean to true on every edge.
This is similar to ``-fsanitize-coverage=inline-8bit-counter`` but instead of
an increment of a counter, it just sets a boolean to true.

Users need to implement a single function to capture the flags at startup.

.. code-block:: c++

  extern "C"
  void __sanitizer_cov_bool_flag_init(bool *start, bool *end) {
    // [start,end) is the array of boolean flags created for the current DSO.
    // Capture this array in order to read/modify the flags.
  }


PC-Table
========

**Experimental, may change or disappear in future**

**Note:** this instrumentation might be incompatible with dead code stripping
(``-Wl,-gc-sections``) for linkers other than LLD, thus resulting in a
significant binary size overhead. For more information, see
`Bug 34636 <https://bugs.llvm.org/show_bug.cgi?id=34636>`_.

With ``-fsanitize-coverage=pc-table`` the compiler will create a table of
instrumented PCs. Requires either ``-fsanitize-coverage=inline-8bit-counters``,
or ``-fsanitize-coverage=inline-bool-flag``, or ``-fsanitize-coverage=trace-pc-guard``.

Users need to implement a single function to capture the PC table at startup:

.. code-block:: c++

  extern "C"
  void __sanitizer_cov_pcs_init(const uintptr_t *pcs_beg,
                                const uintptr_t *pcs_end) {
    // [pcs_beg,pcs_end) is the array of ptr-sized integers representing
    // pairs [PC,PCFlags] for every instrumented block in the current DSO.
    // Capture this array in order to read the PCs and their Flags.
    // The number of PCs and PCFlags for a given DSO is the same as the number
    // of 8-bit counters (-fsanitize-coverage=inline-8bit-counters), or
    // boolean flags (-fsanitize-coverage=inline=bool-flags), or trace_pc_guard
    // callbacks (-fsanitize-coverage=trace-pc-guard).
    // A PCFlags describes the basic block:
    //  * bit0: 1 if the block is the function entry block, 0 otherwise.
  }


Tracing PCs
===========

With ``-fsanitize-coverage=trace-pc`` the compiler will insert
``__sanitizer_cov_trace_pc()`` on every edge.
With an additional ``...=trace-pc,indirect-calls`` flag
``__sanitizer_cov_trace_pc_indirect(void *callee)`` will be inserted on every indirect call.
These callbacks are not implemented in the Sanitizer run-time and should be defined
by the user.
This mechanism is used for fuzzing the Linux kernel
(https://github.com/google/syzkaller).

Instrumentation points
======================
Sanitizer Coverage offers different levels of instrumentation.

* ``edge`` (default): edges are instrumented (see below).
* ``bb``: basic blocks are instrumented.
* ``func``: only the entry block of every function will be instrumented.

Use these flags together with ``trace-pc-guard`` or ``trace-pc``,
like this: ``-fsanitize-coverage=func,trace-pc-guard``.

When ``edge`` or ``bb`` is used, some of the edges/blocks may still be left
uninstrumented (pruned) if such instrumentation is considered redundant.
Use ``no-prune`` (e.g. ``-fsanitize-coverage=bb,no-prune,trace-pc-guard``)
to disable pruning. This could be useful for better coverage visualization.


Edge coverage
-------------

Consider this code:

.. code-block:: c++

    void foo(int *a) {
      if (a)
        *a = 0;
    }

It contains 3 basic blocks, let's name them A, B, C:

.. code-block:: none

    A
    |\
    | \
    |  B
    | /
    |/
    C

If blocks A, B, and C are all covered we know for certain that the edges A=>B
and B=>C were executed, but we still don't know if the edge A=>C was executed.
Such edges of control flow graph are called
`critical <https://en.wikipedia.org/wiki/Control_flow_graph#Special_edges>`_.
The edge-level coverage simply splits all critical edges by introducing new
dummy blocks and then instruments those blocks:

.. code-block:: none

    A
    |\
    | \
    D  B
    | /
    |/
    C

Tracing data flow
=================

Support for data-flow-guided fuzzing.
With ``-fsanitize-coverage=trace-cmp`` the compiler will insert extra instrumentation
around comparison instructions and switch statements.
Similarly, with ``-fsanitize-coverage=trace-div`` the compiler will instrument
integer division instructions (to capture the right argument of division)
and with  ``-fsanitize-coverage=trace-gep`` --
the `LLVM GEP instructions <https://llvm.org/docs/GetElementPtr.html>`_
(to capture array indices).
Similarly, with ``-fsanitize-coverage=trace-loads`` and ``-fsanitize-coverage=trace-stores``
the compiler will instrument loads and stores, respectively.

Currently, these flags do not work by themselves - they require one
of ``-fsanitize-coverage={trace-pc,inline-8bit-counters,inline-bool}``
flags to work.

Unless ``no-prune`` option is provided, some of the comparison instructions
will not be instrumented.

.. code-block:: c++

  // Called before a comparison instruction.
  // Arg1 and Arg2 are arguments of the comparison.
  void __sanitizer_cov_trace_cmp1(uint8_t Arg1, uint8_t Arg2);
  void __sanitizer_cov_trace_cmp2(uint16_t Arg1, uint16_t Arg2);
  void __sanitizer_cov_trace_cmp4(uint32_t Arg1, uint32_t Arg2);
  void __sanitizer_cov_trace_cmp8(uint64_t Arg1, uint64_t Arg2);

  // Called before a comparison instruction if exactly one of the arguments is constant.
  // Arg1 and Arg2 are arguments of the comparison, Arg1 is a compile-time constant.
  // These callbacks are emitted by -fsanitize-coverage=trace-cmp since 2017-08-11
  void __sanitizer_cov_trace_const_cmp1(uint8_t Arg1, uint8_t Arg2);
  void __sanitizer_cov_trace_const_cmp2(uint16_t Arg1, uint16_t Arg2);
  void __sanitizer_cov_trace_const_cmp4(uint32_t Arg1, uint32_t Arg2);
  void __sanitizer_cov_trace_const_cmp8(uint64_t Arg1, uint64_t Arg2);

  // Called before a switch statement.
  // Val is the switch operand.
  // Cases[0] is the number of case constants.
  // Cases[1] is the size of Val in bits.
  // Cases[2:] are the case constants.
  void __sanitizer_cov_trace_switch(uint64_t Val, uint64_t *Cases);

  // Called before a division statement.
  // Val is the second argument of division.
  void __sanitizer_cov_trace_div4(uint32_t Val);
  void __sanitizer_cov_trace_div8(uint64_t Val);

  // Called before a GetElemementPtr (GEP) instruction
  // for every non-constant array index.
  void __sanitizer_cov_trace_gep(uintptr_t Idx);

  // Called before a load of appropriate size. Addr is the address of the load.
  void __sanitizer_cov_load1(uint8_t *addr);
  void __sanitizer_cov_load2(uint16_t *addr);
  void __sanitizer_cov_load4(uint32_t *addr);
  void __sanitizer_cov_load8(uint64_t *addr);
  void __sanitizer_cov_load16(__int128 *addr);
  // Called before a store of appropriate size. Addr is the address of the store.
  void __sanitizer_cov_store1(uint8_t *addr);
  void __sanitizer_cov_store2(uint16_t *addr);
  void __sanitizer_cov_store4(uint32_t *addr);
  void __sanitizer_cov_store8(uint64_t *addr);
  void __sanitizer_cov_store16(__int128 *addr);


Tracing control flow
====================

With ``-fsanitize-coverage=control-flow`` the compiler will create a table to collect
control flow for each function. More specifically, for each basic block in the function,
two lists are populated. One list for successors of the basic block and another list for
non-intrinsic called functions.

**TODO:** in the current implementation, indirect calls are not tracked
and are only marked with special value (-1) in the list.

Each table row consists of the basic block address
followed by ``null``-ended lists of successors and callees.
The table is encoded in a special section named ``sancov_cfs``

Example:

.. code-block:: c++

  int foo (int x) {
    if (x > 0)
      bar(x);
    else
      x = 0;
    return x;
  }

The code above contains 4 basic blocks, let's name them A, B, C, D:

.. code-block:: none

    A
    |\
    | \
    B  C
    | /
    |/
    D

The collected control flow table is as follows:
``A, B, C, null, null, B, D, null, @bar, null, C, D, null, null, D, null, null.``

Users need to implement a single function to capture the CF table at startup:

.. code-block:: c++

  extern "C"
  void __sanitizer_cov_cfs_init(const uintptr_t *cfs_beg,
                                const uintptr_t *cfs_end) {
    // [cfs_beg,cfs_end) is the array of ptr-sized integers representing
    // the collected control flow.
  }


Disabling instrumentation with ``__attribute__((no_sanitize("coverage")))``
===========================================================================

It is possible to disable coverage instrumentation for select functions via the
function attribute ``__attribute__((no_sanitize("coverage")))``. Because this
attribute may not be supported by other compilers, it is recommended to use it
together with ``__has_feature(coverage_sanitizer)``.

Disabling instrumentation without source modification
=====================================================

It is sometimes useful to tell SanitizerCoverage to instrument only a subset of the
functions in your target without modifying source files.
With ``-fsanitize-coverage-allowlist=allowlist.txt``
and ``-fsanitize-coverage-ignorelist=blocklist.txt``,
you can specify such a subset through the combination of an allowlist and a blocklist.

SanitizerCoverage will only instrument functions that satisfy two conditions.
First, the function should belong to a source file with a path that is both allowlisted
and not blocklisted.
Second, the function should have a mangled name that is both allowlisted and not blocklisted.

The allowlist and blocklist format is similar to that of the sanitizer blocklist format.
The default allowlist will match every source file and every function.
The default blocklist will match no source file and no function.

A common use case is to have the allowlist list folders or source files for which you want
instrumentation and allow all function names, while the blocklist will opt out some specific
files or functions that the allowlist loosely allowed.

Here is an example allowlist:

.. code-block:: none

  # Enable instrumentation for a whole folder
  src:bar/*
  # Enable instrumentation for a specific source file
  src:foo/a.cpp
  # Enable instrumentation for all functions in those files
  fun:*

And an example blocklist:

.. code-block:: none

  # Disable instrumentation for a specific source file that the allowlist allowed
  src:bar/b.cpp
  # Disable instrumentation for a specific function that the allowlist allowed
  fun:*myFunc*

The use of ``*`` wildcards above is required because function names are matched after mangling.
Without the wildcards, one would have to write the whole mangled name.

Be careful that the paths of source files are matched exactly as they are provided on the clang
command line.
For example, the allowlist above would include file ``bar/b.cpp`` if the path was provided
exactly like this, but would it would fail to include it with other ways to refer to the same
file such as ``./bar/b.cpp``, or ``bar\b.cpp`` on Windows.
So, please make sure to always double check that your lists are correctly applied.

Default implementation
======================

The sanitizer run-time (AddressSanitizer, MemorySanitizer, etc) provide a
default implementations of some of the coverage callbacks.
You may use this implementation to dump the coverage on disk at the process
exit.

Example:

.. code-block:: console

    % cat -n cov.cc
         1  #include <stdio.h>
         2  __attribute__((noinline))
         3  void foo() { printf("foo\n"); }
         4
         5  int main(int argc, char **argv) {
         6    if (argc == 2)
         7      foo();
         8    printf("main\n");
         9  }
    % clang++ -g cov.cc -fsanitize=address -fsanitize-coverage=trace-pc-guard
    % ASAN_OPTIONS=coverage=1 ./a.out; wc -c *.sancov
    main
    SanitizerCoverage: ./a.out.7312.sancov 2 PCs written
    24 a.out.7312.sancov
    % ASAN_OPTIONS=coverage=1 ./a.out foo ; wc -c *.sancov
    foo
    main
    SanitizerCoverage: ./a.out.7316.sancov 3 PCs written
    24 a.out.7312.sancov
    32 a.out.7316.sancov

Every time you run an executable instrumented with SanitizerCoverage
one ``*.sancov`` file is created during the process shutdown.
If the executable is dynamically linked against instrumented DSOs,
one ``*.sancov`` file will be also created for every DSO.

Sancov data format
------------------

The format of ``*.sancov`` files is very simple: the first 8 bytes is the magic,
one of ``0xC0BFFFFFFFFFFF64`` and ``0xC0BFFFFFFFFFFF32``. The last byte of the
magic defines the size of the following offsets. The rest of the data is the
offsets in the corresponding binary/DSO that were executed during the run.

Sancov Tool
-----------

A simple ``sancov`` tool is provided to process coverage files.
The tool is part of LLVM project and is currently supported only on Linux.
It can handle symbolization tasks autonomously without any extra support
from the environment. You need to pass .sancov files (named
``<module_name>.<pid>.sancov`` and paths to all corresponding binary elf files.
Sancov matches these files using module names and binaries file names.

.. code-block:: console

    USAGE: sancov [options] <action> (<binary file>|<.sancov file>)...

    Action (required)
      -print                    - Print coverage addresses
      -covered-functions        - Print all covered functions.
      -not-covered-functions    - Print all not covered functions.
      -symbolize                - Symbolizes the report.

    Options
      -blocklist=<string>         - Blocklist file (sanitizer blocklist format).
      -demangle                   - Print demangled function name.
      -strip_path_prefix=<string> - Strip this prefix from file paths in reports


Coverage Reports
----------------

**Experimental**

``.sancov`` files do not contain enough information to generate a source-level
coverage report. The missing information is contained
in debug info of the binary. Thus the ``.sancov`` has to be symbolized
to produce a ``.symcov`` file first:

.. code-block:: console

    sancov -symbolize my_program.123.sancov my_program > my_program.123.symcov

The ``.symcov`` file can be browsed overlaid over the source code by
running ``tools/sancov/coverage-report-server.py`` script that will start
an HTTP server.

Output directory
----------------

By default, .sancov files are created in the current working directory.
This can be changed with ``ASAN_OPTIONS=coverage_dir=/path``:

.. code-block:: console

    % ASAN_OPTIONS="coverage=1:coverage_dir=/tmp/cov" ./a.out foo
    % ls -l /tmp/cov/*sancov
    -rw-r----- 1 kcc eng 4 Nov 27 12:21 a.out.22673.sancov
    -rw-r----- 1 kcc eng 8 Nov 27 12:21 a.out.22679.sancov
