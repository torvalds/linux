ThreadSanitizer
===============

Introduction
------------

ThreadSanitizer is a tool that detects data races.  It consists of a compiler
instrumentation module and a run-time library.  Typical slowdown introduced by
ThreadSanitizer is about **5x-15x**.  Typical memory overhead introduced by
ThreadSanitizer is about **5x-10x**.

How to build
------------

Build LLVM/Clang with `CMake <https://llvm.org/docs/CMake.html>`_.

Supported Platforms
-------------------

ThreadSanitizer is supported on the following OS:

* Android aarch64, x86_64
* Darwin arm64, x86_64
* FreeBSD
* Linux aarch64, x86_64, powerpc64, powerpc64le
* NetBSD

Support for other 64-bit architectures is possible, contributions are welcome.
Support for 32-bit platforms is problematic and is not planned.

Usage
-----

Simply compile and link your program with ``-fsanitize=thread``.  To get a
reasonable performance add ``-O1`` or higher.  Use ``-g`` to get file names
and line numbers in the warning messages.

Example:

.. code-block:: console

  % cat projects/compiler-rt/lib/tsan/lit_tests/tiny_race.c
  #include <pthread.h>
  int Global;
  void *Thread1(void *x) {
    Global = 42;
    return x;
  }
  int main() {
    pthread_t t;
    pthread_create(&t, NULL, Thread1, NULL);
    Global = 43;
    pthread_join(t, NULL);
    return Global;
  }

  $ clang -fsanitize=thread -g -O1 tiny_race.c

If a bug is detected, the program will print an error message to stderr.
Currently, ThreadSanitizer symbolizes its output using an external
``addr2line`` process (this will be fixed in future).

.. code-block:: bash

  % ./a.out
  WARNING: ThreadSanitizer: data race (pid=19219)
    Write of size 4 at 0x7fcf47b21bc0 by thread T1:
      #0 Thread1 tiny_race.c:4 (exe+0x00000000a360)

    Previous write of size 4 at 0x7fcf47b21bc0 by main thread:
      #0 main tiny_race.c:10 (exe+0x00000000a3b4)

    Thread T1 (running) created at:
      #0 pthread_create tsan_interceptors.cc:705 (exe+0x00000000c790)
      #1 main tiny_race.c:9 (exe+0x00000000a3a4)

``__has_feature(thread_sanitizer)``
------------------------------------

In some cases one may need to execute different code depending on whether
ThreadSanitizer is enabled.
:ref:`\_\_has\_feature <langext-__has_feature-__has_extension>` can be used for
this purpose.

.. code-block:: c

    #if defined(__has_feature)
    #  if __has_feature(thread_sanitizer)
    // code that builds only under ThreadSanitizer
    #  endif
    #endif

``__attribute__((no_sanitize("thread")))``
-----------------------------------------------

Some code should not be instrumented by ThreadSanitizer.  One may use the
function attribute ``no_sanitize("thread")`` to disable instrumentation of plain
(non-atomic) loads/stores in a particular function.  ThreadSanitizer still
instruments such functions to avoid false positives and provide meaningful stack
traces.  This attribute may not be supported by other compilers, so we suggest
to use it together with ``__has_feature(thread_sanitizer)``.

``__attribute__((disable_sanitizer_instrumentation))``
--------------------------------------------------------

The ``disable_sanitizer_instrumentation`` attribute can be applied to functions
to prevent all kinds of instrumentation. As a result, it may introduce false
positives and incorrect stack traces. Therefore, it should be used with care,
and only if absolutely required; for example for certain code that cannot
tolerate any instrumentation and resulting side-effects. This attribute
overrides ``no_sanitize("thread")``.

Ignorelist
----------

ThreadSanitizer supports ``src`` and ``fun`` entity types in
:doc:`SanitizerSpecialCaseList`, that can be used to suppress data race reports
in the specified source files or functions. Unlike functions marked with
``no_sanitize("thread")`` attribute, ignored functions are not instrumented
at all. This can lead to false positives due to missed synchronization via
atomic operations and missed stack frames in reports.

Limitations
-----------

* ThreadSanitizer uses more real memory than a native run. At the default
  settings the memory overhead is 5x plus 1Mb per each thread. Settings with 3x
  (less accurate analysis) and 9x (more accurate analysis) overhead are also
  available.
* ThreadSanitizer maps (but does not reserve) a lot of virtual address space.
  This means that tools like ``ulimit`` may not work as usually expected.
* Libc/libstdc++ static linking is not supported.
* Non-position-independent executables are not supported.  Therefore, the
  ``fsanitize=thread`` flag will cause Clang to act as though the ``-fPIE``
  flag had been supplied if compiling without ``-fPIC``, and as though the
  ``-pie`` flag had been supplied if linking an executable.

Current Status
--------------

ThreadSanitizer is in beta stage.  It is known to work on large C++ programs
using pthreads, but we do not promise anything (yet).  C++11 threading is
supported with llvm libc++.  The test suite is integrated into CMake build
and can be run with ``make check-tsan`` command.

We are actively working on enhancing the tool --- stay tuned.  Any help,
especially in the form of minimized standalone tests is more than welcome.

More Information
----------------
`<https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual>`_
