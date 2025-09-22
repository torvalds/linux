=======================================================
libFuzzer – a library for coverage-guided fuzz testing.
=======================================================
.. contents::
   :local:
   :depth: 1

Introduction
============

LibFuzzer is an in-process, coverage-guided, evolutionary fuzzing engine.

LibFuzzer is linked with the library under test, and feeds fuzzed inputs to the
library via a specific fuzzing entrypoint (aka "target function"); the fuzzer
then tracks which areas of the code are reached, and generates mutations on the
corpus of input data in order to maximize the code coverage.
The code coverage
information for libFuzzer is provided by LLVM's SanitizerCoverage_
instrumentation.

Contact: libfuzzer(#)googlegroups.com

Status
======

The original authors of libFuzzer have stopped active work on it and switched
to working on another fuzzing engine, Centipede_. LibFuzzer is still fully
supported in that important bugs will get fixed. However, please do not expect
major new features or code reviews, other than for bug fixes.

Versions
========

LibFuzzer requires a matching version of Clang.


Getting Started
===============

.. contents::
   :local:
   :depth: 1

Fuzz Target
-----------

The first step in using libFuzzer on a library is to implement a
*fuzz target* -- a function that accepts an array of bytes and
does something interesting with these bytes using the API under test.
Like this:

.. code-block:: c++

  // fuzz_target.cc
  extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    DoSomethingInterestingWithMyAPI(Data, Size);
    return 0;  // Values other than 0 and -1 are reserved for future use.
  }

Note that this fuzz target does not depend on libFuzzer in any way
and so it is possible and even desirable to use it with other fuzzing engines
e.g. AFL_ and/or Radamsa_.

Some important things to remember about fuzz targets:

* The fuzzing engine will execute the fuzz target many times with different inputs in the same process.
* It must tolerate any kind of input (empty, huge, malformed, etc).
* It must not `exit()` on any input.
* It may use threads but ideally all threads should be joined at the end of the function.
* It must be as deterministic as possible. Non-determinism (e.g. random decisions not based on the input bytes) will make fuzzing inefficient.
* It must be fast. Try avoiding cubic or greater complexity, logging, or excessive memory consumption.
* Ideally, it should not modify any global state (although that's not strict).
* Usually, the narrower the target the better. E.g. if your target can parse several data formats, split it into several targets, one per format.


Fuzzer Usage
------------

Recent versions of Clang (starting from 6.0) include libFuzzer, and no extra installation is necessary.

In order to build your fuzzer binary, use the `-fsanitize=fuzzer` flag during the
compilation and linking. In most cases you may want to combine libFuzzer with
AddressSanitizer_ (ASAN), UndefinedBehaviorSanitizer_ (UBSAN), or both.  You can
also build with MemorySanitizer_ (MSAN), but support is experimental::

   clang -g -O1 -fsanitize=fuzzer                         mytarget.c # Builds the fuzz target w/o sanitizers
   clang -g -O1 -fsanitize=fuzzer,address                 mytarget.c # Builds the fuzz target with ASAN
   clang -g -O1 -fsanitize=fuzzer,signed-integer-overflow mytarget.c # Builds the fuzz target with a part of UBSAN
   clang -g -O1 -fsanitize=fuzzer,memory                  mytarget.c # Builds the fuzz target with MSAN

This will perform the necessary instrumentation, as well as linking with the libFuzzer library.
Note that ``-fsanitize=fuzzer`` links in the libFuzzer's ``main()`` symbol.

If modifying ``CFLAGS`` of a large project, which also compiles executables
requiring their own ``main`` symbol, it may be desirable to request just the
instrumentation without linking::

   clang -fsanitize=fuzzer-no-link mytarget.c

Then libFuzzer can be linked to the desired driver by passing in
``-fsanitize=fuzzer`` during the linking stage.

.. _libfuzzer-corpus:

Corpus
------

Coverage-guided fuzzers like libFuzzer rely on a corpus of sample inputs for the
code under test.  This corpus should ideally be seeded with a varied collection
of valid and invalid inputs for the code under test; for example, for a graphics
library the initial corpus might hold a variety of different small PNG/JPG/GIF
files.  The fuzzer generates random mutations based around the sample inputs in
the current corpus.  If a mutation triggers execution of a previously-uncovered
path in the code under test, then that mutation is saved to the corpus for
future variations.

LibFuzzer will work without any initial seeds, but will be less
efficient if the library under test accepts complex,
structured inputs.

The corpus can also act as a sanity/regression check, to confirm that the
fuzzing entrypoint still works and that all of the sample inputs run through
the code under test without problems.

If you have a large corpus (either generated by fuzzing or acquired by other means)
you may want to minimize it while still preserving the full coverage. One way to do that
is to use the `-merge=1` flag:

.. code-block:: console

  mkdir NEW_CORPUS_DIR  # Store minimized corpus here.
  ./my_fuzzer -merge=1 NEW_CORPUS_DIR FULL_CORPUS_DIR

You may use the same flag to add more interesting items to an existing corpus.
Only the inputs that trigger new coverage will be added to the first corpus.

.. code-block:: console

  ./my_fuzzer -merge=1 CURRENT_CORPUS_DIR NEW_POTENTIALLY_INTERESTING_INPUTS_DIR

Running
-------

To run the fuzzer, first create a Corpus_ directory that holds the
initial "seed" sample inputs:

.. code-block:: console

  mkdir CORPUS_DIR
  cp /some/input/samples/* CORPUS_DIR

Then run the fuzzer on the corpus directory:

.. code-block:: console

  ./my_fuzzer CORPUS_DIR  # -max_len=1000 -jobs=20 ...

As the fuzzer discovers new interesting test cases (i.e. test cases that
trigger coverage of new paths through the code under test), those test cases
will be added to the corpus directory.

By default, the fuzzing process will continue indefinitely – at least until
a bug is found.  Any crashes or sanitizer failures will be reported as usual,
stopping the fuzzing process, and the particular input that triggered the bug
will be written to disk (typically as ``crash-<sha1>``, ``leak-<sha1>``,
or ``timeout-<sha1>``).


Parallel Fuzzing
----------------

Each libFuzzer process is single-threaded, unless the library under test starts
its own threads.  However, it is possible to run multiple libFuzzer processes in
parallel with a shared corpus directory; this has the advantage that any new
inputs found by one fuzzer process will be available to the other fuzzer
processes (unless you disable this with the ``-reload=0`` option).

This is primarily controlled by the ``-jobs=N`` option, which indicates that
that `N` fuzzing jobs should be run to completion (i.e. until a bug is found or
time/iteration limits are reached).  These jobs will be run across a set of
worker processes, by default using half of the available CPU cores; the count of
worker processes can be overridden by the ``-workers=N`` option.  For example,
running with ``-jobs=30`` on a 12-core machine would run 6 workers by default,
with each worker averaging 5 bugs by completion of the entire process.

Fork mode
---------

**Experimental** mode ``-fork=N`` (where ``N`` is the number of parallel jobs)
enables oom-, timeout-, and crash-resistant
fuzzing with separate processes (using ``fork-exec``, not just ``fork``).

The top libFuzzer process will not do any fuzzing itself, but will
spawn up to ``N`` concurrent child processes providing them
small random subsets of the corpus. After a child exits, the top process
merges the corpus generated by the child back to the main corpus.

Related flags:

``-ignore_ooms``
  True by default. If an OOM happens during fuzzing in one of the child processes,
  the reproducer is saved on disk, and fuzzing continues.
``-ignore_timeouts``
  True by default, same as ``-ignore_ooms``, but for timeouts.
``-ignore_crashes``
  False by default, same as ``-ignore_ooms``, but for all other crashes.

The plan is to eventually replace ``-jobs=N`` and ``-workers=N`` with ``-fork=N``.

Resuming merge
--------------

Merging large corpora may be time consuming, and it is often desirable to do it
on preemptable VMs, where the process may be killed at any time.
In order to seamlessly resume the merge, use the ``-merge_control_file`` flag
and use ``killall -SIGUSR1 /path/to/fuzzer/binary`` to stop the merge gracefully. Example:

.. code-block:: console

  % rm -f SomeLocalPath
  % ./my_fuzzer CORPUS1 CORPUS2 -merge=1 -merge_control_file=SomeLocalPath
  ...
  MERGE-INNER: using the control file 'SomeLocalPath'
  ...
  # While this is running, do `killall -SIGUSR1 my_fuzzer` in another console
  ==9015== INFO: libFuzzer: exiting as requested

  # This will leave the file SomeLocalPath with the partial state of the merge.
  # Now, you can continue the merge by executing the same command. The merge
  # will continue from where it has been interrupted.
  % ./my_fuzzer CORPUS1 CORPUS2 -merge=1 -merge_control_file=SomeLocalPath
  ...
  MERGE-OUTER: non-empty control file provided: 'SomeLocalPath'
  MERGE-OUTER: control file ok, 32 files total, first not processed file 20
  ...

Options
=======

To run the fuzzer, pass zero or more corpus directories as command line
arguments.  The fuzzer will read test inputs from each of these corpus
directories, and any new test inputs that are generated will be written
back to the first corpus directory:

.. code-block:: console

  ./fuzzer [-flag1=val1 [-flag2=val2 ...] ] [dir1 [dir2 ...] ]

If a list of files (rather than directories) are passed to the fuzzer program,
then it will re-run those files as test inputs but will not perform any fuzzing.
In this mode the fuzzer binary can be used as a regression test (e.g. on a
continuous integration system) to check the target function and saved inputs
still work.

The most important command line options are:

``-help``
  Print help message (``-help=1``).
``-seed``
  Random seed. If 0 (the default), the seed is generated.
``-runs``
  Number of individual test runs, -1 (the default) to run indefinitely.
``-max_len``
  Maximum length of a test input. If 0 (the default), libFuzzer tries to guess
  a good value based on the corpus (and reports it).
``-len_control``
  Try generating small inputs first, then try larger inputs over time.
  Specifies the rate at which the length limit is increased (smaller == faster).
  Default is 100. If 0, immediately try inputs with size up to max_len.
``-timeout``
  Timeout in seconds, default 1200. If an input takes longer than this timeout,
  the process is treated as a failure case.
``-rss_limit_mb``
  Memory usage limit in Mb, default 2048. Use 0 to disable the limit.
  If an input requires more than this amount of RSS memory to execute,
  the process is treated as a failure case.
  The limit is checked in a separate thread every second.
  If running w/o ASAN/MSAN, you may use 'ulimit -v' instead.
``-malloc_limit_mb``
  If non-zero, the fuzzer will exit if the target tries to allocate this
  number of Mb with one malloc call.
  If zero (default) same limit as rss_limit_mb is applied.
``-timeout_exitcode``
  Exit code (default 77) used if libFuzzer reports a timeout.
``-error_exitcode``
  Exit code (default 77) used if libFuzzer itself (not a sanitizer) reports a bug (leak, OOM, etc).
``-max_total_time``
  If positive, indicates the maximum total time in seconds to run the fuzzer.
  If 0 (the default), run indefinitely.
``-merge``
  If set to 1, any corpus inputs from the 2nd, 3rd etc. corpus directories
  that trigger new code coverage will be merged into the first corpus
  directory.  Defaults to 0. This flag can be used to minimize a corpus.
``-merge_control_file``
  Specify a control file used for the merge process.
  If a merge process gets killed it tries to leave this file in a state
  suitable for resuming the merge. By default a temporary file will be used.
``-minimize_crash``
  If 1, minimizes the provided crash input.
  Use with -runs=N or -max_total_time=N to limit the number of attempts.
``-reload``
  If set to 1 (the default), the corpus directory is re-read periodically to
  check for new inputs; this allows detection of new inputs that were discovered
  by other fuzzing processes.
``-jobs``
  Number of fuzzing jobs to run to completion. Default value is 0, which runs a
  single fuzzing process until completion.  If the value is >= 1, then this
  number of jobs performing fuzzing are run, in a collection of parallel
  separate worker processes; each such worker process has its
  ``stdout``/``stderr`` redirected to ``fuzz-<JOB>.log``.
``-workers``
  Number of simultaneous worker processes to run the fuzzing jobs to completion
  in. If 0 (the default), ``min(jobs, NumberOfCpuCores()/2)`` is used.
``-dict``
  Provide a dictionary of input keywords; see Dictionaries_.
``-use_counters``
  Use `coverage counters`_ to generate approximate counts of how often code
  blocks are hit; defaults to 1.
``-reduce_inputs``
  Try to reduce the size of inputs while preserving their full feature sets;
  defaults to 1.
``-use_value_profile``
  Use `value profile`_ to guide corpus expansion; defaults to 0.
``-only_ascii``
  If 1, generate only ASCII (``isprint``+``isspace``) inputs. Defaults to 0.
``-artifact_prefix``
  Provide a prefix to use when saving fuzzing artifacts (crash, timeout, or
  slow inputs) as ``$(artifact_prefix)file``.  Defaults to empty.
``-exact_artifact_path``
  Ignored if empty (the default).  If non-empty, write the single artifact on
  failure (crash, timeout) as ``$(exact_artifact_path)``. This overrides
  ``-artifact_prefix`` and will not use checksum in the file name. Do not use
  the same path for several parallel processes.
``-print_pcs``
  If 1, print out newly covered PCs. Defaults to 0.
``-print_final_stats``
  If 1, print statistics at exit.  Defaults to 0.
``-detect_leaks``
  If 1 (default) and if LeakSanitizer is enabled
  try to detect memory leaks during fuzzing (i.e. not only at shut down).
``-close_fd_mask``
  Indicate output streams to close at startup. Be careful, this will
  remove diagnostic output from target code (e.g. messages on assert failure).

   - 0 (default): close neither ``stdout`` nor ``stderr``
   - 1 : close ``stdout``
   - 2 : close ``stderr``
   - 3 : close both ``stdout`` and ``stderr``.

For the full list of flags run the fuzzer binary with ``-help=1``.

Output
======

During operation the fuzzer prints information to ``stderr``, for example::

  INFO: Seed: 1523017872
  INFO: Loaded 1 modules (16 guards): [0x744e60, 0x744ea0),
  INFO: -max_len is not provided, using 64
  INFO: A corpus is not provided, starting from an empty corpus
  #0	READ units: 1
  #1	INITED cov: 3 ft: 2 corp: 1/1b exec/s: 0 rss: 24Mb
  #3811	NEW    cov: 4 ft: 3 corp: 2/2b exec/s: 0 rss: 25Mb L: 1 MS: 5 ChangeBit-ChangeByte-ChangeBit-ShuffleBytes-ChangeByte-
  #3827	NEW    cov: 5 ft: 4 corp: 3/4b exec/s: 0 rss: 25Mb L: 2 MS: 1 CopyPart-
  #3963	NEW    cov: 6 ft: 5 corp: 4/6b exec/s: 0 rss: 25Mb L: 2 MS: 2 ShuffleBytes-ChangeBit-
  #4167	NEW    cov: 7 ft: 6 corp: 5/9b exec/s: 0 rss: 25Mb L: 3 MS: 1 InsertByte-
  ...

The early parts of the output include information about the fuzzer options and
configuration, including the current random seed (in the ``Seed:`` line; this
can be overridden with the ``-seed=N`` flag).

Further output lines have the form of an event code and statistics.  The
possible event codes are:

``READ``
  The fuzzer has read in all of the provided input samples from the corpus
  directories.
``INITED``
  The fuzzer has completed initialization, which includes running each of
  the initial input samples through the code under test.
``NEW``
  The fuzzer has created a test input that covers new areas of the code
  under test.  This input will be saved to the primary corpus directory.
``REDUCE``
  The fuzzer has found a better (smaller) input that triggers previously
  discovered features (set ``-reduce_inputs=0`` to disable).
``pulse``
  The fuzzer has generated 2\ :sup:`n` inputs (generated periodically to reassure
  the user that the fuzzer is still working).
``DONE``
  The fuzzer has completed operation because it has reached the specified
  iteration limit (``-runs``) or time limit (``-max_total_time``).
``RELOAD``
  The fuzzer is performing a periodic reload of inputs from the corpus
  directory; this allows it to discover any inputs discovered by other
  fuzzer processes (see `Parallel Fuzzing`_).

Each output line also reports the following statistics (when non-zero):

``cov:``
  Total number of code blocks or edges covered by executing the current corpus.
``ft:``
  libFuzzer uses different signals to evaluate the code coverage:
  edge coverage, edge counters, value profiles, indirect caller/callee pairs, etc.
  These signals combined are called *features* (`ft:`).
``corp:``
  Number of entries in the current in-memory test corpus and its size in bytes.
``lim:``
  Current limit on the length of new entries in the corpus.  Increases over time
  until the max length (``-max_len``) is reached.
``exec/s:``
  Number of fuzzer iterations per second.
``rss:``
  Current memory consumption.

For ``NEW`` and ``REDUCE`` events, the output line also includes information
about the mutation operation that produced the new input:

``L:``
  Size of the new input in bytes.
``MS: <n> <operations>``
  Count and list of the mutation operations used to generate the input.


Examples
========
.. contents::
   :local:
   :depth: 1

Toy example
-----------

A simple function that does something interesting if it receives the input
"HI!"::

  cat << EOF > test_fuzzer.cc
  #include <stdint.h>
  #include <stddef.h>
  extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 0 && data[0] == 'H')
      if (size > 1 && data[1] == 'I')
         if (size > 2 && data[2] == '!')
         __builtin_trap();
    return 0;
  }
  EOF
  # Build test_fuzzer.cc with asan and link against libFuzzer.
  clang++ -fsanitize=address,fuzzer test_fuzzer.cc
  # Run the fuzzer with no corpus.
  ./a.out

You should get an error pretty quickly::

  INFO: Seed: 1523017872
  INFO: Loaded 1 modules (16 guards): [0x744e60, 0x744ea0),
  INFO: -max_len is not provided, using 64
  INFO: A corpus is not provided, starting from an empty corpus
  #0	READ units: 1
  #1	INITED cov: 3 ft: 2 corp: 1/1b exec/s: 0 rss: 24Mb
  #3811	NEW    cov: 4 ft: 3 corp: 2/2b exec/s: 0 rss: 25Mb L: 1 MS: 5 ChangeBit-ChangeByte-ChangeBit-ShuffleBytes-ChangeByte-
  #3827	NEW    cov: 5 ft: 4 corp: 3/4b exec/s: 0 rss: 25Mb L: 2 MS: 1 CopyPart-
  #3963	NEW    cov: 6 ft: 5 corp: 4/6b exec/s: 0 rss: 25Mb L: 2 MS: 2 ShuffleBytes-ChangeBit-
  #4167	NEW    cov: 7 ft: 6 corp: 5/9b exec/s: 0 rss: 25Mb L: 3 MS: 1 InsertByte-
  ==31511== ERROR: libFuzzer: deadly signal
  ...
  artifact_prefix='./'; Test unit written to ./crash-b13e8756b13a00cf168300179061fb4b91fefbed


More examples
-------------

Examples of real-life fuzz targets and the bugs they find can be found
at http://tutorial.libfuzzer.info. Among other things you can learn how
to detect Heartbleed_ in one second.


Advanced features
=================
.. contents::
   :local:
   :depth: 1

Dictionaries
------------
LibFuzzer supports user-supplied dictionaries with input language keywords
or other interesting byte sequences (e.g. multi-byte magic values).
Use ``-dict=DICTIONARY_FILE``. For some input languages using a dictionary
may significantly improve the search speed.
The dictionary syntax is similar to that used by AFL_ for its ``-x`` option::

  # Lines starting with '#' and empty lines are ignored.

  # Adds "blah" (w/o quotes) to the dictionary.
  kw1="blah"
  # Use \\ for backslash and \" for quotes.
  kw2="\"ac\\dc\""
  # Use \xAB for hex values
  kw3="\xF7\xF8"
  # the name of the keyword followed by '=' may be omitted:
  "foo\x0Abar"



Tracing CMP instructions
------------------------

With an additional compiler flag ``-fsanitize-coverage=trace-cmp``
(on by default as part of ``-fsanitize=fuzzer``, see SanitizerCoverageTraceDataFlow_)
libFuzzer will intercept CMP instructions and guide mutations based
on the arguments of intercepted CMP instructions. This may slow down
the fuzzing but is very likely to improve the results.

Value Profile
-------------

With  ``-fsanitize-coverage=trace-cmp`` (default with ``-fsanitize=fuzzer``)
and extra run-time flag ``-use_value_profile=1`` the fuzzer will
collect value profiles for the parameters of compare instructions
and treat some new values as new coverage.

The current implementation does roughly the following:

* The compiler instruments all CMP instructions with a callback that receives both CMP arguments.
* The callback computes `(caller_pc&4095) | (popcnt(Arg1 ^ Arg2) << 12)` and uses this value to set a bit in a bitset.
* Every new observed bit in the bitset is treated as new coverage.


This feature has a potential to discover many interesting inputs,
but there are two downsides.
First, the extra instrumentation may bring up to 2x additional slowdown.
Second, the corpus may grow by several times.

Fuzzer-friendly build mode
---------------------------
Sometimes the code under test is not fuzzing-friendly. Examples:

  - The target code uses a PRNG seeded e.g. by system time and
    thus two consequent invocations may potentially execute different code paths
    even if the end result will be the same. This will cause a fuzzer to treat
    two similar inputs as significantly different and it will blow up the test corpus.
    E.g. libxml uses ``rand()`` inside its hash table.
  - The target code uses checksums to protect from invalid inputs.
    E.g. png checks CRC for every chunk.

In many cases it makes sense to build a special fuzzing-friendly build
with certain fuzzing-unfriendly features disabled. We propose to use a common build macro
for all such cases for consistency: ``FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION``.

.. code-block:: c++

  void MyInitPRNG() {
  #ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    // In fuzzing mode the behavior of the code should be deterministic.
    srand(0);
  #else
    srand(time(0));
  #endif
  }



AFL compatibility
-----------------
LibFuzzer can be used together with AFL_ on the same test corpus.
Both fuzzers expect the test corpus to reside in a directory, one file per input.
You can run both fuzzers on the same corpus, one after another:

.. code-block:: console

  ./afl-fuzz -i testcase_dir -o findings_dir /path/to/program @@
  ./llvm-fuzz testcase_dir findings_dir  # Will write new tests to testcase_dir

Periodically restart both fuzzers so that they can use each other's findings.
Currently, there is no simple way to run both fuzzing engines in parallel while sharing the same corpus dir.

You may also use AFL on your target function ``LLVMFuzzerTestOneInput``:
see an example `here <https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/fuzzer/afl>`__.

How good is my fuzzer?
----------------------

Once you implement your target function ``LLVMFuzzerTestOneInput`` and fuzz it to death,
you will want to know whether the function or the corpus can be improved further.
One easy to use metric is, of course, code coverage.

We recommend to use
`Clang Coverage <https://clang.llvm.org/docs/SourceBasedCodeCoverage.html>`_,
to visualize and study your code coverage
(`example <https://github.com/google/fuzzer-test-suite/blob/master/tutorial/libFuzzerTutorial.md#visualizing-coverage>`_).


User-supplied mutators
----------------------

LibFuzzer allows to use custom (user-supplied) mutators, see
`Structure-Aware Fuzzing <https://github.com/google/fuzzing/blob/master/docs/structure-aware-fuzzing.md>`_
for more details.

Startup initialization
----------------------
If the library being tested needs to be initialized, there are several options.

The simplest way is to have a statically initialized global object inside
`LLVMFuzzerTestOneInput` (or in global scope if that works for you):

.. code-block:: c++

  extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    static bool Initialized = DoInitialization();
    ...

Alternatively, you may define an optional init function and it will receive
the program arguments that you can read and modify. Do this **only** if you
really need to access ``argv``/``argc``.

.. code-block:: c++

   extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    ReadAndMaybeModify(argc, argv);
    return 0;
   }

Using libFuzzer as a library
----------------------------
If the code being fuzzed must provide its own `main`, it's possible to
invoke libFuzzer as a library. Be sure to pass ``-fsanitize=fuzzer-no-link``
during compilation, and link your binary against the no-main version of
libFuzzer. On Linux installations, this is typically located at:

.. code-block:: bash

  /usr/lib/<llvm-version>/lib/clang/<clang-version>/lib/linux/libclang_rt.fuzzer_no_main-<architecture>.a

If building libFuzzer from source, this is located at the following path
in the build output directory:

.. code-block:: bash

  lib/linux/libclang_rt.fuzzer_no_main-<architecture>.a

From here, the code can do whatever setup it requires, and when it's ready
to start fuzzing, it can call `LLVMFuzzerRunDriver`, passing in the program
arguments and a callback. This callback is invoked just like
`LLVMFuzzerTestOneInput`, and has the same signature.

.. code-block:: c++

  extern "C" int LLVMFuzzerRunDriver(int *argc, char ***argv,
                    int (*UserCb)(const uint8_t *Data, size_t Size));


Rejecting unwanted inputs
-------------------------

It may be desirable to reject some inputs, i.e. to not add them to the corpus.

For example, when fuzzing an API consisting of parsing and other logic,
one may want to allow only those inputs into the corpus that parse successfully.

If the fuzz target returns -1 on a given input,
libFuzzer will not add that input top the corpus, regardless of what coverage
it triggers.


.. code-block:: c++

  extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (auto *Obj = ParseMe(Data, Size)) {
      Obj->DoSomethingInteresting();
      return 0;  // Accept. The input may be added to the corpus.
    }
    return -1;  // Reject; The input will not be added to the corpus.
  }

Leaks
-----

Binaries built with AddressSanitizer_ or LeakSanitizer_ will try to detect
memory leaks at the process shutdown.
For in-process fuzzing this is inconvenient
since the fuzzer needs to report a leak with a reproducer as soon as the leaky
mutation is found. However, running full leak detection after every mutation
is expensive.

By default (``-detect_leaks=1``) libFuzzer will count the number of
``malloc`` and ``free`` calls when executing every mutation.
If the numbers don't match (which by itself doesn't mean there is a leak)
libFuzzer will invoke the more expensive LeakSanitizer_
pass and if the actual leak is found, it will be reported with the reproducer
and the process will exit.

If your target has massive leaks and the leak detection is disabled
you will eventually run out of RAM (see the ``-rss_limit_mb`` flag).


Developing libFuzzer
====================

LibFuzzer is built as a part of LLVM project by default on macos and Linux.
Users of other operating systems can explicitly request compilation using
``-DCOMPILER_RT_BUILD_LIBFUZZER=ON`` flag.
Tests are run using ``check-fuzzer`` target from the build directory
which was configured with ``-DCOMPILER_RT_INCLUDE_TESTS=ON`` flag.

.. code-block:: console

    ninja check-fuzzer


FAQ
=========================

Q. Why doesn't libFuzzer use any of the LLVM support?
-----------------------------------------------------

There are two reasons.

First, we want this library to be used outside of the LLVM without users having to
build the rest of LLVM. This may sound unconvincing for many LLVM folks,
but in practice the need for building the whole LLVM frightens many potential
users -- and we want more users to use this code.

Second, there is a subtle technical reason not to rely on the rest of LLVM, or
any other large body of code (maybe not even STL). When coverage instrumentation
is enabled, it will also instrument the LLVM support code which will blow up the
coverage set of the process (since the fuzzer is in-process). In other words, by
using more external dependencies we will slow down the fuzzer while the main
reason for it to exist is extreme speed.

Q. Does libFuzzer Support Windows?
------------------------------------------------------------------------------------

Yes, libFuzzer now supports Windows. Initial support was added in r341082.
Any build of Clang 9 supports it. You can download a build of Clang for Windows
that has libFuzzer from
`LLVM Snapshot Builds <https://llvm.org/builds/>`_.

Using libFuzzer on Windows without ASAN is unsupported. Building fuzzers with the
``/MD`` (dynamic runtime library) compile option is unsupported. Support for these
may be added in the future. Linking fuzzers with the ``/INCREMENTAL`` link option
(or the ``/DEBUG`` option which implies it) is also unsupported.

Send any questions or comments to the mailing list: libfuzzer(#)googlegroups.com

Q. When libFuzzer is not a good solution for a problem?
---------------------------------------------------------

* If the test inputs are validated by the target library and the validator
  asserts/crashes on invalid inputs, in-process fuzzing is not applicable.
* Bugs in the target library may accumulate without being detected. E.g. a memory
  corruption that goes undetected at first and then leads to a crash while
  testing another input. This is why it is highly recommended to run this
  in-process fuzzer with all sanitizers to detect most bugs on the spot.
* It is harder to protect the in-process fuzzer from excessive memory
  consumption and infinite loops in the target library (still possible).
* The target library should not have significant global state that is not
  reset between the runs.
* Many interesting target libraries are not designed in a way that supports
  the in-process fuzzer interface (e.g. require a file path instead of a
  byte array).
* If a single test run takes a considerable fraction of a second (or
  more) the speed benefit from the in-process fuzzer is negligible.
* If the target library runs persistent threads (that outlive
  execution of one test) the fuzzing results will be unreliable.

Q. So, what exactly this Fuzzer is good for?
--------------------------------------------

This Fuzzer might be a good choice for testing libraries that have relatively
small inputs, each input takes < 10ms to run, and the library code is not expected
to crash on invalid inputs.
Examples: regular expression matchers, text or binary format parsers, compression,
network, crypto.

Q. LibFuzzer crashes on my complicated fuzz target (but works fine for me on smaller targets).
----------------------------------------------------------------------------------------------

Check if your fuzz target uses ``dlclose``.
Currently, libFuzzer doesn't support targets that call ``dlclose``,
this may be fixed in future.


Trophies
========
* Thousands of bugs found on OSS-Fuzz:  https://opensource.googleblog.com/2017/05/oss-fuzz-five-months-later-and.html

* GLIBC: https://sourceware.org/glibc/wiki/FuzzingLibc

* MUSL LIBC: `[1] <http://git.musl-libc.org/cgit/musl/commit/?id=39dfd58417ef642307d90306e1c7e50aaec5a35c>`__ `[2] <http://www.openwall.com/lists/oss-security/2015/03/30/3>`__

* `pugixml <https://github.com/zeux/pugixml/issues/39>`_

* PCRE: Search for "LLVM fuzzer" in http://vcs.pcre.org/pcre2/code/trunk/ChangeLog?view=markup;
  also in `bugzilla <https://bugs.exim.org/buglist.cgi?bug_status=__all__&content=libfuzzer&no_redirect=1&order=Importance&product=PCRE&query_format=specific>`_

* `ICU <http://bugs.icu-project.org/trac/ticket/11838>`_

* `Freetype <https://savannah.nongnu.org/search/?words=LibFuzzer&type_of_search=bugs&Search=Search&exact=1#options>`_

* `Harfbuzz <https://github.com/behdad/harfbuzz/issues/139>`_

* `SQLite <http://www3.sqlite.org/cgi/src/info/088009efdd56160b>`_

* `Python <http://bugs.python.org/issue25388>`_

* OpenSSL/BoringSSL: `[1] <https://boringssl.googlesource.com/boringssl/+/cb852981cd61733a7a1ae4fd8755b7ff950e857d>`_ `[2] <https://openssl.org/news/secadv/20160301.txt>`_ `[3] <https://boringssl.googlesource.com/boringssl/+/2b07fa4b22198ac02e0cee8f37f3337c3dba91bc>`_ `[4] <https://boringssl.googlesource.com/boringssl/+/6b6e0b20893e2be0e68af605a60ffa2cbb0ffa64>`_  `[5] <https://github.com/openssl/openssl/pull/931/commits/dd5ac557f052cc2b7f718ac44a8cb7ac6f77dca8>`_ `[6] <https://github.com/openssl/openssl/pull/931/commits/19b5b9194071d1d84e38ac9a952e715afbc85a81>`_

* `Libxml2
  <https://bugzilla.gnome.org/buglist.cgi?bug_status=__all__&content=libFuzzer&list_id=68957&order=Importance&product=libxml2&query_format=specific>`_ and `[HT206167] <https://support.apple.com/en-gb/HT206167>`_ (CVE-2015-5312, CVE-2015-7500, CVE-2015-7942)

* `Linux Kernel's BPF verifier <https://github.com/iovisor/bpf-fuzzer>`_

* `Linux Kernel's Crypto code <https://www.spinics.net/lists/stable/msg199712.html>`_

* Capstone: `[1] <https://github.com/aquynh/capstone/issues/600>`__ `[2] <https://github.com/aquynh/capstone/commit/6b88d1d51eadf7175a8f8a11b690684443b11359>`__

* file:`[1] <http://bugs.gw.com/view.php?id=550>`__  `[2] <http://bugs.gw.com/view.php?id=551>`__  `[3] <http://bugs.gw.com/view.php?id=553>`__  `[4] <http://bugs.gw.com/view.php?id=554>`__

* Radare2: `[1] <https://github.com/revskills?tab=contributions&from=2016-04-09>`__

* gRPC: `[1] <https://github.com/grpc/grpc/pull/6071/commits/df04c1f7f6aec6e95722ec0b023a6b29b6ea871c>`__ `[2] <https://github.com/grpc/grpc/pull/6071/commits/22a3dfd95468daa0db7245a4e8e6679a52847579>`__ `[3] <https://github.com/grpc/grpc/pull/6071/commits/9cac2a12d9e181d130841092e9d40fa3309d7aa7>`__ `[4] <https://github.com/grpc/grpc/pull/6012/commits/82a91c91d01ce9b999c8821ed13515883468e203>`__ `[5] <https://github.com/grpc/grpc/pull/6202/commits/2e3e0039b30edaf89fb93bfb2c1d0909098519fa>`__ `[6] <https://github.com/grpc/grpc/pull/6106/files>`__

* WOFF2: `[1] <https://github.com/google/woff2/commit/a15a8ab>`__

* LLVM: `Clang <https://bugs.llvm.org/show_bug.cgi?id=23057>`_, `Clang-format <https://bugs.llvm.org/show_bug.cgi?id=23052>`_, `libc++ <https://bugs.llvm.org/show_bug.cgi?id=24411>`_, `llvm-as <https://bugs.llvm.org/show_bug.cgi?id=24639>`_, `Demangler <https://bugs.chromium.org/p/chromium/issues/detail?id=606626>`_, Disassembler: http://reviews.llvm.org/rL247405, http://reviews.llvm.org/rL247414, http://reviews.llvm.org/rL247416, http://reviews.llvm.org/rL247417, http://reviews.llvm.org/rL247420, http://reviews.llvm.org/rL247422.

* Tensorflow: `[1] <https://da-data.blogspot.com/2017/01/finding-bugs-in-tensorflow-with.html>`__

* Ffmpeg: `[1] <https://github.com/FFmpeg/FFmpeg/commit/c92f55847a3d9cd12db60bfcd0831ff7f089c37c>`__  `[2] <https://github.com/FFmpeg/FFmpeg/commit/25ab1a65f3acb5ec67b53fb7a2463a7368f1ad16>`__  `[3] <https://github.com/FFmpeg/FFmpeg/commit/85d23e5cbc9ad6835eef870a5b4247de78febe56>`__ `[4] <https://github.com/FFmpeg/FFmpeg/commit/04bd1b38ee6b8df410d0ab8d4949546b6c4af26a>`__

* `Wireshark <https://bugs.wireshark.org/bugzilla/buglist.cgi?bug_status=UNCONFIRMED&bug_status=CONFIRMED&bug_status=IN_PROGRESS&bug_status=INCOMPLETE&bug_status=RESOLVED&bug_status=VERIFIED&f0=OP&f1=OP&f2=product&f3=component&f4=alias&f5=short_desc&f7=content&f8=CP&f9=CP&j1=OR&o2=substring&o3=substring&o4=substring&o5=substring&o6=substring&o7=matches&order=bug_id%20DESC&query_format=advanced&v2=libfuzzer&v3=libfuzzer&v4=libfuzzer&v5=libfuzzer&v6=libfuzzer&v7=%22libfuzzer%22>`_

* `QEMU <https://researchcenter.paloaltonetworks.com/2017/09/unit42-palo-alto-networks-discovers-new-qemu-vulnerability/>`_

.. _pcre2: http://www.pcre.org/
.. _AFL: http://lcamtuf.coredump.cx/afl/
.. _Radamsa: https://github.com/aoh/radamsa
.. _SanitizerCoverage: https://clang.llvm.org/docs/SanitizerCoverage.html
.. _SanitizerCoverageTraceDataFlow: https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-data-flow
.. _AddressSanitizer: https://clang.llvm.org/docs/AddressSanitizer.html
.. _LeakSanitizer: https://clang.llvm.org/docs/LeakSanitizer.html
.. _Heartbleed: http://en.wikipedia.org/wiki/Heartbleed
.. _FuzzerInterface.h: https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/fuzzer/FuzzerInterface.h
.. _3.7.0: https://llvm.org/releases/3.7.0/docs/LibFuzzer.html
.. _building Clang from trunk: https://clang.llvm.org/get_started.html
.. _MemorySanitizer: https://clang.llvm.org/docs/MemorySanitizer.html
.. _UndefinedBehaviorSanitizer: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
.. _`coverage counters`: https://clang.llvm.org/docs/SanitizerCoverage.html#coverage-counters
.. _`value profile`: #value-profile
.. _`caller-callee pairs`: https://clang.llvm.org/docs/SanitizerCoverage.html#caller-callee-coverage
.. _BoringSSL: https://boringssl.googlesource.com/boringssl/
.. _Centipede: https://github.com/google/centipede

