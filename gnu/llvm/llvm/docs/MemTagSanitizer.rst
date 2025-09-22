================
MemTagSanitizer
================

.. contents::
   :local:

Introduction
============

**Note:** this page describes a tool under development. Part of this
functionality is planned but not implemented.  Hardware capable of
running MemTagSanitizer does not exist as of Oct 2019.

MemTagSanitizer is a fast memory error detector and **a code hardening
tool** based on the Armv8.5-A `Memory Tagging Extension`_. It
detects a similar class of errors as `AddressSanitizer`_ or `HardwareAssistedAddressSanitizer`_, but with
**much** lower overhead.

MemTagSanitizer overhead is expected to be in low single digits, both
CPU and memory. There are plans for a debug mode with slightly higher
memory overhead and better diagnostics. The primary use case of
MemTagSanitizer is code hardening in production binaries, where it is
expected to be a strong mitigation for both stack and heap-based
memory bugs.


Usage
=====

Compile and link your program with ``-fsanitize=memtag`` flag. This
will only work when targeting AArch64 with MemTag extension. One
possible way to achieve that is to add ``-target
aarch64-linux -march=armv8+memtag`` to compilation flags.

Implementation
==============

See `HardwareAssistedAddressSanitizer`_ for a general overview of a
tag-based approach to memory safety.  MemTagSanitizer follows a
similar implementation strategy, but with the tag storage (shadow)
provided by the hardware.

A quick overview of MTE hardware capabilities:

* Every 16 aligned bytes of memory can be assigned a 4-bit Allocation Tag.
* Every pointer can have a 4-bit Address Tag that is in its most significant byte.
* Most memory access instructions generate an exception if Address Tag != Allocation Tag.
* Special instructions are provided for fast tag manipulation.

Stack instrumentation
=====================

Stack-based memory errors are detected by updating Allocation Tag for
each local variable to a random value at the start of its lifetime,
and resetting it to the stack pointer Address Tag at the end of
it. Unallocated stack space is expected to match the Address Tag of
SP; this allows to skip tagging of any variable when memory safety can
be statically proven.

Allocating a truly random tag for each stack variable in a large
function may incur significant code size overhead, because it means
that each variable's address is an independent, non-rematerializable
value; thus a function with N local variables will have extra N live
values to keep through most of its life time.

For this reason MemTagSanitizer generates at most one random tag per
function, called a "base tag". Other stack variables, if there are
any, are assigned tags at a fixed offset from the base.

Please refer to `this document
<https://github.com/google/sanitizers/wiki/Stack-instrumentation-with-ARM-Memory-Tagging-Extension-(MTE)>`_
for more details about stack instrumentation.

Heap tagging
============

**Note:** this part is not implemented as of Oct 2019.

MemTagSanitizer will use :doc:`ScudoHardenedAllocator`
with additional code to update memory tags when

* New memory is obtained from the system.
* An allocation is freed.

There is no need to change Allocation Tags for the bulk of the
allocated memory in malloc(), as long as a pointer with the matching
Address Tag is returned.

More information
================

* `LLVM Developer Meeting 2018 talk on Memory Tagging <https://llvm.org/devmtg/2018-10/slides/Serebryany-Stepanov-Tsyrklevich-Memory-Tagging-Slides-LLVM-2018.pdf>`_
* `Memory Tagging Whitepaper <https://arxiv.org/pdf/1802.09517.pdf>`_

.. _Memory Tagging Extension: https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/arm-a-profile-architecture-2018-developments-armv85a
.. _AddressSanitizer: https://clang.llvm.org/docs/AddressSanitizer.html
.. _HardwareAssistedAddressSanitizer: https://clang.llvm.org/docs/HardwareAssistedAddressSanitizerDesign.html
