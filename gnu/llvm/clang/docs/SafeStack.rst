=========
SafeStack
=========

.. contents::
   :local:

Introduction
============

SafeStack is an instrumentation pass that protects programs against attacks
based on stack buffer overflows, without introducing any measurable performance
overhead. It works by separating the program stack into two distinct regions:
the safe stack and the unsafe stack. The safe stack stores return addresses,
register spills, and local variables that are always accessed in a safe way,
while the unsafe stack stores everything else. This separation ensures that
buffer overflows on the unsafe stack cannot be used to overwrite anything
on the safe stack.

SafeStack is a part of the `Code-Pointer Integrity (CPI) Project
<https://dslab.epfl.ch/research/cpi/>`_.

Performance
-----------

The performance overhead of the SafeStack instrumentation is less than 0.1% on
average across a variety of benchmarks (see the `Code-Pointer Integrity
<https://dslab.epfl.ch/pubs/cpi.pdf>`__ paper for details). This is mainly
because most small functions do not have any variables that require the unsafe
stack and, hence, do not need unsafe stack frames to be created. The cost of
creating unsafe stack frames for large functions is amortized by the cost of
executing the function.

In some cases, SafeStack actually improves the performance. Objects that end up
being moved to the unsafe stack are usually large arrays or variables that are
used through multiple stack frames. Moving such objects away from the safe
stack increases the locality of frequently accessed values on the stack, such
as register spills, return addresses, and small local variables.

Compatibility
-------------

Most programs, static libraries, or individual files can be compiled
with SafeStack as is. SafeStack requires basic runtime support, which, on most
platforms, is implemented as a compiler-rt library that is automatically linked
in when the program is compiled with SafeStack.

Linking a DSO with SafeStack is not currently supported.

Known compatibility limitations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Certain code that relies on low-level stack manipulations requires adaption to
work with SafeStack. One example is mark-and-sweep garbage collection
implementations for C/C++ (e.g., Oilpan in chromium/blink), which must be
changed to look for the live pointers on both safe and unsafe stacks.

SafeStack supports linking statically modules that are compiled with and
without SafeStack. An executable compiled with SafeStack can load dynamic
libraries that are not compiled with SafeStack. At the moment, compiling
dynamic libraries with SafeStack is not supported.

Signal handlers that use ``sigaltstack()`` must not use the unsafe stack (see
``__attribute__((no_sanitize("safe-stack")))`` below).

Programs that use APIs from ``ucontext.h`` are not supported yet.

Security
--------

SafeStack protects return addresses, spilled registers and local variables that
are always accessed in a safe way by separating them in a dedicated safe stack
region. The safe stack is automatically protected against stack-based buffer
overflows, since it is disjoint from the unsafe stack in memory, and it itself
is always accessed in a safe way. In the current implementation, the safe stack
is protected against arbitrary memory write vulnerabilities though
randomization and information hiding: the safe stack is allocated at a random
address and the instrumentation ensures that no pointers to the safe stack are
ever stored outside of the safe stack itself (see limitations below).

Known security limitations
~~~~~~~~~~~~~~~~~~~~~~~~~~

A complete protection against control-flow hijack attacks requires combining
SafeStack with another mechanism that enforces the integrity of code pointers
that are stored on the heap or the unsafe stack, such as `CPI
<https://dslab.epfl.ch/research/cpi/>`_, or a forward-edge control flow integrity
mechanism that enforces correct calling conventions at indirect call sites,
such as `IFCC <https://research.google.com/pubs/archive/42808.pdf>`_ with arity
checks. Clang has control-flow integrity protection scheme for :doc:`C++ virtual
calls <ControlFlowIntegrity>`, but not non-virtual indirect calls. With
SafeStack alone, an attacker can overwrite a function pointer on the heap or
the unsafe stack and cause a program to call arbitrary location, which in turn
might enable stack pivoting and return-oriented programming.

In its current implementation, SafeStack provides precise protection against
stack-based buffer overflows, but protection against arbitrary memory write
vulnerabilities is probabilistic and relies on randomization and information
hiding. The randomization is currently based on system-enforced ASLR and shares
its known security limitations. The safe stack pointer hiding is not perfect
yet either: system library functions such as ``swapcontext``, exception
handling mechanisms, intrinsics such as ``__builtin_frame_address``, or
low-level bugs in runtime support could leak the safe stack pointer. In the
future, such leaks could be detected by static or dynamic analysis tools and
prevented by adjusting such functions to either encrypt the stack pointer when
storing it in the heap (as already done e.g., by ``setjmp``/``longjmp``
implementation in glibc), or store it in a safe region instead.

The `CPI paper <https://dslab.epfl.ch/pubs/cpi.pdf>`_ describes two alternative,
stronger safe stack protection mechanisms, that rely on software fault
isolation, or hardware segmentation (as available on x86-32 and some x86-64
CPUs).

At the moment, SafeStack assumes that the compiler's implementation is correct.
This has not been verified except through manual code inspection, and could
always regress in the future. It's therefore desirable to have a separate
static or dynamic binary verification tool that would check the correctness of
the SafeStack instrumentation in final binaries.

Usage
=====

To enable SafeStack, just pass ``-fsanitize=safe-stack`` flag to both compile
and link command lines.

Supported Platforms
-------------------

SafeStack was tested on Linux, NetBSD, FreeBSD and macOS.

Low-level API
-------------

``__has_feature(safe_stack)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In some rare cases one may need to execute different code depending on
whether SafeStack is enabled. The macro ``__has_feature(safe_stack)`` can
be used for this purpose.

.. code-block:: c

    #if __has_feature(safe_stack)
    // code that builds only under SafeStack
    #endif

``__attribute__((no_sanitize("safe-stack")))``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``__attribute__((no_sanitize("safe-stack")))`` on a function declaration
to specify that the safe stack instrumentation should not be applied to that
function, even if enabled globally (see ``-fsanitize=safe-stack`` flag). This
attribute may be required for functions that make assumptions about the
exact layout of their stack frames.

All local variables in functions with this attribute will be stored on the safe
stack. The safe stack remains unprotected against memory errors when accessing
these variables, so extra care must be taken to manually ensure that all such
accesses are safe. Furthermore, the addresses of such local variables should
never be stored on the heap, as it would leak the location of the SafeStack.

``__builtin___get_unsafe_stack_ptr()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This builtin function returns current unsafe stack pointer of the current
thread.

``__builtin___get_unsafe_stack_bottom()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This builtin function returns a pointer to the bottom of the unsafe stack of the
current thread.

``__builtin___get_unsafe_stack_top()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This builtin function returns a pointer to the top of the unsafe stack of the
current thread.

``__builtin___get_unsafe_stack_start()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Deprecated: This builtin function is an alias for
``__builtin___get_unsafe_stack_bottom()``.

Design
======

Please refer to the `Code-Pointer Integrity <https://dslab.epfl.ch/research/cpi/>`__
project page for more information about the design of the SafeStack and its
related technologies.

setjmp and exception handling
-----------------------------

The `OSDI'14 paper <https://dslab.epfl.ch/pubs/cpi.pdf>`_ mentions that
on Linux the instrumentation pass finds calls to setjmp or functions that
may throw an exception, and inserts required instrumentation at their call
sites. Specifically, the instrumentation pass saves the shadow stack pointer
on the safe stack before the call site, and restores it either after the
call to setjmp or after an exception has been caught. This is implemented
in the function ``SafeStack::createStackRestorePoints``.

Publications
------------

`Code-Pointer Integrity <https://dslab.epfl.ch/pubs/cpi.pdf>`__.
Volodymyr Kuznetsov, Laszlo Szekeres, Mathias Payer, George Candea, R. Sekar, Dawn Song.
USENIX Symposium on Operating Systems Design and Implementation
(`OSDI <https://www.usenix.org/conference/osdi14>`_), Broomfield, CO, October 2014
