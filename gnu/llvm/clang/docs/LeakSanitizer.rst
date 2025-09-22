================
LeakSanitizer
================

.. contents::
   :local:

Introduction
============

LeakSanitizer is a run-time memory leak detector. It can be combined with
:doc:`AddressSanitizer` to get both memory error and leak detection, or
used in a stand-alone mode. LSan adds almost no performance overhead
until the very end of the process, at which point there is an extra leak
detection phase.

Usage
=====

:doc:`AddressSanitizer`: integrates LeakSanitizer and enables it by default on
supported platforms.

.. code-block:: console

    $ cat memory-leak.c
    #include <stdlib.h>
    void *p;
    int main() {
      p = malloc(7);
      p = 0; // The memory is leaked here.
      return 0;
    }
    % clang -fsanitize=address -g memory-leak.c ; ASAN_OPTIONS=detect_leaks=1 ./a.out
    ==23646==ERROR: LeakSanitizer: detected memory leaks
    Direct leak of 7 byte(s) in 1 object(s) allocated from:
        #0 0x4af01b in __interceptor_malloc /projects/compiler-rt/lib/asan/asan_malloc_linux.cc:52:3
        #1 0x4da26a in main memory-leak.c:4:7
        #2 0x7f076fd9cec4 in __libc_start_main libc-start.c:287
    SUMMARY: AddressSanitizer: 7 byte(s) leaked in 1 allocation(s).

To use LeakSanitizer in stand-alone mode, link your program with
``-fsanitize=leak`` flag. Make sure to use ``clang`` (not ``ld``) for the
link step, so that it would link in proper LeakSanitizer run-time library
into the final executable.

Supported Platforms
===================

* Android aarch64/i386/x86_64
* Fuchsia aarch64/x86_64
* Linux arm/aarch64/mips64/ppc64/ppc64le/riscv64/s390x/i386/x86\_64
* macOS aarch64/i386/x86\_64
* NetBSD i386/x86_64

More Information
================

`<https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer>`_
