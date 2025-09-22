===============================
Assembling a Complete Toolchain
===============================

.. contents::
   :local:
   :depth: 2

Introduction
============

Clang is only one component in a complete tool chain for C family
programming languages. In order to assemble a complete toolchain,
additional tools and runtime libraries are required. Clang is designed
to interoperate with existing tools and libraries for its target
platforms, and the LLVM project provides alternatives for a number
of these components.

This document describes the required and optional components in a
complete toolchain, where to find them, and the supported versions
and limitations of each option.

.. warning::

  This document currently describes Clang configurations on POSIX-like
  operating systems with the GCC-compatible ``clang`` driver. When
  targeting Windows with the MSVC-compatible ``clang-cl`` driver, some
  of the details are different.

Tools
=====

.. FIXME: Describe DWARF-related tools

A complete compilation of C family programming languages typically
involves the following pipeline of tools, some of which are omitted
in some compilations:

* **Preprocessor**: This performs the actions of the C preprocessor:
  expanding #includes and #defines.
  The ``-E`` flag instructs Clang to stop after this step.

* **Parsing**: This parses and semantically analyzes the source language and
  builds a source-level intermediate representation ("AST"), producing a
  :ref:`precompiled header (PCH) <usersmanual-precompiled-headers>`,
  preamble, or
  :doc:`precompiled module file (PCM) <Modules>`,
  depending on the input.
  The ``-precompile`` flag instructs Clang to stop after this step. This is
  the default when the input is a header file.

* **IR generation**: This converts the source-level intermediate representation
  into an optimizer-specific intermediate representation (IR); for Clang, this
  is LLVM IR.
  The ``-emit-llvm`` flag instructs Clang to stop after this step. If combined
  with ``-S``, Clang will produce textual LLVM IR; otherwise, it will produce
  LLVM IR bitcode.

* **Compiler backend**: This converts the intermediate representation
  into target-specific assembly code.
  The ``-S`` flag instructs Clang to stop after this step.

* **Assembler**: This converts target-specific assembly code into
  target-specific machine code object files.
  The ``-c`` flag instructs Clang to stop after this step.

* **Linker**: This combines multiple object files into a single image
  (either a shared object or an executable).

Clang provides all of these pieces other than the linker. When multiple
steps are performed by the same tool, it is common for the steps to be
fused together to avoid creating intermediate files.

When given an output of one of the above steps as an input, earlier steps
are skipped (for instance, a ``.s`` file input will be assembled and linked).

The Clang driver can be invoked with the ``-###`` flag (this argument will need
to be escaped under most shells) to see which commands it would run for the
above steps, without running them. The ``-v`` (verbose) flag will print the
commands in addition to running them.

Clang frontend
--------------

The Clang frontend (``clang -cc1``) is used to compile C family languages. The
command-line interface of the frontend is considered to be an implementation
detail, intentionally has no external documentation, and is subject to change
without notice.

Language frontends for other languages
--------------------------------------

Clang can be provided with inputs written in non-C-family languages. In such
cases, an external tool will be used to compile the input. The
currently-supported languages are:

* Ada (``-x ada``, ``.ad[bs]``)
* Fortran (``-x f95``, ``.f``, ``.f9[05]``, ``.for``, ``.fpp``, case-insensitive)
* Java (``-x java``)

In each case, GCC will be invoked to compile the input.

Assembler
---------

Clang can either use LLVM's integrated assembler or an external system-specific
tool (for instance, the GNU Assembler on GNU OSes) to produce machine code from
assembly.
By default, Clang uses LLVM's integrated assembler on all targets where it is
supported. If you wish to use the system assembler instead, use the
``-fno-integrated-as`` option.

Linker
------

Clang can be configured to use one of several different linkers:

* GNU ld
* GNU gold
* LLVM's `lld <https://lld.llvm.org>`_
* MSVC's link.exe

Link-time optimization is natively supported by lld, and supported via
a `linker plugin <https://llvm.org/docs/GoldPlugin.html>`_ when using gold.

The default linker varies between targets, and can be overridden via the
``-fuse-ld=<linker name>`` flag.

Runtime libraries
=================

A number of different runtime libraries are required to provide different
layers of support for C family programs. Clang will implicitly link an
appropriate implementation of each runtime library, selected based on
target defaults or explicitly selected by the ``--rtlib=`` and ``--stdlib=``
flags.

The set of implicitly-linked libraries depend on the language mode. As a
consequence, you should use ``clang++`` when linking C++ programs in order
to ensure the C++ runtimes are provided.

.. note::

  There may exist other implementations for these components not described
  below. Please let us know how well those other implementations work with
  Clang so they can be added to this list!

.. FIXME: Describe Objective-C runtime libraries
.. FIXME: Describe profiling runtime library
.. FIXME: Describe cuda/openmp/opencl/... runtime libraries

Compiler runtime
----------------

The compiler runtime library provides definitions of functions implicitly
invoked by the compiler to support operations not natively supported by
the underlying hardware (for instance, 128-bit integer multiplications),
and where inline expansion of the operation is deemed unsuitable.

The default runtime library is target-specific. For targets where GCC is
the dominant compiler, Clang currently defaults to using libgcc_s. On most
other targets, compiler-rt is used by default.

compiler-rt (LLVM)
^^^^^^^^^^^^^^^^^^

`LLVM's compiler runtime library <https://compiler-rt.llvm.org/>`_ provides a
complete set of runtime library functions containing all functions that
Clang will implicitly call, in ``libclang_rt.builtins.<arch>.a``.

You can instruct Clang to use compiler-rt with the ``--rtlib=compiler-rt`` flag.
This is not supported on every platform.

If using libc++ and/or libc++abi, you may need to configure them to use
compiler-rt rather than libgcc_s by passing ``-DLIBCXX_USE_COMPILER_RT=YES``
and/or ``-DLIBCXXABI_USE_COMPILER_RT=YES`` to ``cmake``. Otherwise, you
may end up with both runtime libraries linked into your program (this is
typically harmless, but wasteful).

libgcc_s (GNU)
^^^^^^^^^^^^^^

`GCC's runtime library <https://gcc.gnu.org/onlinedocs/gccint/Libgcc.html>`_
can be used in place of compiler-rt. However, it lacks several functions
that LLVM may emit references to, particularly when using Clang's
``__builtin_*_overflow`` family of intrinsics.

You can instruct Clang to use libgcc_s with the ``--rtlib=libgcc`` flag.
This is not supported on every platform.

Atomics library
---------------

If your program makes use of atomic operations and the compiler is not able
to lower them all directly to machine instructions (because there either is
no known suitable machine instruction or the operand is not known to be
suitably aligned), a call to a runtime library ``__atomic_*`` function
will be generated. A runtime library containing these atomics functions is
necessary for such programs.

compiler-rt (LLVM)
^^^^^^^^^^^^^^^^^^

compiler-rt contains an implementation of an atomics library.

libatomic (GNU)
^^^^^^^^^^^^^^^

libgcc_s does not provide an implementation of an atomics library. Instead,
`GCC's libatomic library <https://gcc.gnu.org/wiki/Atomic/GCCMM>`_ can be
used to supply these when using libgcc_s.

.. note::

  Clang does not currently automatically link against libatomic when using
  libgcc_s. You may need to manually add ``-latomic`` to support this
  configuration when using non-native atomic operations (if you see link errors
  referring to ``__atomic_*`` functions).

Unwind library
--------------

The unwind library provides a family of ``_Unwind_*`` functions implementing
the language-neutral stack unwinding portion of the Itanium C++ ABI
(`Level I <https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html#base-abi>`_).
It is a dependency of the C++ ABI library, and sometimes is a dependency
of other runtimes.

libunwind (LLVM)
^^^^^^^^^^^^^^^^

LLVM's unwinder library is part of the llvm-project git repository. To
build it, pass ``-DLLVM_ENABLE_RUNTIMES=libunwind`` to the cmake invocation.

If using libc++abi, you may need to configure it to use libunwind
rather than libgcc_s by passing ``-DLIBCXXABI_USE_LLVM_UNWINDER=YES``
to ``cmake``. If libc++abi is configured to use some version of
libunwind, that library will be implicitly linked into binaries that
link to libc++abi.

libgcc_s (GNU)
^^^^^^^^^^^^^^

libgcc_s has an integrated unwinder, and does not need an external unwind
library to be provided.

libunwind (nongnu.org)
^^^^^^^^^^^^^^^^^^^^^^

This is another implementation of the libunwind specification.
See `libunwind (nongnu.org) <https://www.nongnu.org/libunwind>`_.

libunwind (PathScale)
^^^^^^^^^^^^^^^^^^^^^

This is another implementation of the libunwind specification.
See `libunwind (pathscale) <https://github.com/pathscale/libunwind>`_.

Sanitizer runtime
-----------------

The instrumentation added by Clang's sanitizers (``-fsanitize=...``) implicitly
makes calls to a runtime library, in order to maintain side state about the
execution of the program and to issue diagnostic messages when a problem is
detected.

The only supported implementation of these runtimes is provided by LLVM's
compiler-rt, and the relevant portion of that library
(``libclang_rt.<sanitizer>.<arch>.a``)
will be implicitly linked when linking with a ``-fsanitize=...`` flag.

C standard library
------------------

Clang supports a wide variety of
`C standard library <https://en.cppreference.com/w/c>`_
implementations.

C++ ABI library
---------------

The C++ ABI library provides an implementation of the library portion of
the Itanium C++ ABI, covering both the
`support functionality in the main Itanium C++ ABI document
<https://itanium-cxx-abi.github.io/cxx-abi/abi.html>`_ and
`Level II of the exception handling support
<https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html#cxx-abi>`_.
References to the functions and objects in this library are implicitly
generated by Clang when compiling C++ code.

While it is possible to link C++ code using libstdc++ and code using libc++
together into the same program (so long as you do not attempt to pass C++
standard library objects across the boundary), it is not generally possible
to have more than one C++ ABI library in a program.

The version of the C++ ABI library used by Clang will be the one that the
chosen C++ standard library was linked against. Several implementations are
available:

libc++abi (LLVM)
^^^^^^^^^^^^^^^^

`libc++abi <https://libcxxabi.llvm.org/>`_ is LLVM's implementation of this
specification.

libsupc++ (GNU)
^^^^^^^^^^^^^^^

libsupc++ is GCC's implementation of this specification. However, this
library is only used when libstdc++ is linked statically. The dynamic
library version of libstdc++ contains a copy of libsupc++.

.. note::

  Clang does not currently automatically link against libsupc++ when statically
  linking libstdc++. You may need to manually add ``-lsupc++`` to support this
  configuration when using ``-static`` or ``-static-libstdc++``.

libcxxrt (PathScale)
^^^^^^^^^^^^^^^^^^^^

This is another implementation of the Itanium C++ ABI specification.
See `libcxxrt <https://github.com/pathscale/libcxxrt>`_.

C++ standard library
--------------------

Clang supports use of either LLVM's libc++ or GCC's libstdc++ implementation
of the `C++ standard library <https://en.cppreference.com/w/cpp>`_.

libc++ (LLVM)
^^^^^^^^^^^^^

`libc++ <https://libcxx.llvm.org/>`_ is LLVM's implementation of the C++
standard library, aimed at being a complete implementation of the C++
standards from C++11 onwards.

You can instruct Clang to use libc++ with the ``-stdlib=libc++`` flag.

libstdc++ (GNU)
^^^^^^^^^^^^^^^

`libstdc++ <https://gcc.gnu.org/onlinedocs/libstdc++/>`_ is GCC's
implementation of the C++ standard library. Clang supports libstdc++
4.8.3 (released 2014-05-22) and later. Historically Clang implemented
workarounds for issues discovered in libstdc++, and these are removed
as fixed libstdc++ becomes sufficiently old.

You can instruct Clang to use libstdc++ with the ``-stdlib=libstdc++`` flag.
