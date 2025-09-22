======================================================
LLVM Link Time Optimization: Design and Implementation
======================================================

.. contents::
   :local:

Description
===========

LLVM features powerful intermodular optimizations which can be used at link
time. Link Time Optimization (LTO) is another name for intermodular
optimization when performed during the link stage. This document describes the
interface and design between the LTO optimizer and the linker.

Design Philosophy
=================

The LLVM Link Time Optimizer provides complete transparency, while doing
intermodular optimization, in the compiler tool chain. Its main goal is to let
the developer take advantage of intermodular optimizations without making any
significant changes to the developer's makefiles or build system. This is
achieved through tight integration with the linker. In this model, the linker
treats LLVM bitcode files like native object files and allows mixing and
matching among them. The linker uses `libLTO`_, a shared object, to handle LLVM
bitcode files. This tight integration between the linker and LLVM optimizer
helps to do optimizations that are not possible in other models. The linker
input allows the optimizer to avoid relying on conservative escape analysis.

.. _libLTO-example:

Example of link time optimization
---------------------------------

The following example illustrates the advantages of LTO's integrated approach
and clean interface. This example requires a system linker which supports LTO
through the interface described in this document. Here, clang transparently
invokes system linker.

* Input source file ``a.c`` is compiled into LLVM bitcode form.
* Input source file ``main.c`` is compiled into native object code.

.. code-block:: c++

  --- a.h ---
  extern int foo1(void);
  extern void foo2(void);
  extern void foo4(void);

  --- a.c ---
  #include "a.h"

  static signed int i = 0;

  void foo2(void) {
    i = -1;
  }

  static int foo3() {
    foo4();
    return 10;
  }

  int foo1(void) {
    int data = 0;

    if (i < 0)
      data = foo3();

    data = data + 42;
    return data;
  }

  --- main.c ---
  #include <stdio.h>
  #include "a.h"

  void foo4(void) {
    printf("Hi\n");
  }

  int main() {
    return foo1();
  }

To compile, run:

.. code-block:: console

  % clang -flto -c a.c -o a.o        # <-- a.o is LLVM bitcode file
  % clang -c main.c -o main.o        # <-- main.o is native object file
  % clang -flto a.o main.o -o main   # <-- standard link command with -flto

* In this example, the linker recognizes that ``foo2()`` is an externally
  visible symbol defined in LLVM bitcode file. The linker completes its usual
  symbol resolution pass and finds that ``foo2()`` is not used
  anywhere. This information is used by the LLVM optimizer and it
  removes ``foo2()``.

* As soon as ``foo2()`` is removed, the optimizer recognizes that condition ``i
  < 0`` is always false, which means ``foo3()`` is never used. Hence, the
  optimizer also removes ``foo3()``.

* And this in turn, enables linker to remove ``foo4()``.

This example illustrates the advantage of tight integration with the
linker. Here, the optimizer can not remove ``foo3()`` without the linker's
input.

Alternative Approaches
----------------------

**Compiler driver invokes link time optimizer separately.**
    In this model the link time optimizer is not able to take advantage of
    information collected during the linker's normal symbol resolution phase.
    In the above example, the optimizer can not remove ``foo2()`` without the
    linker's input because it is externally visible. This in turn prohibits the
    optimizer from removing ``foo3()``.

**Use separate tool to collect symbol information from all object files.**
    In this model, a new, separate, tool or library replicates the linker's
    capability to collect information for link time optimization. Not only is
    this code duplication difficult to justify, but it also has several other
    disadvantages. For example, the linking semantics and the features provided
    by the linker on various platform are not unique. This means, this new tool
    needs to support all such features and platforms in one super tool or a
    separate tool per platform is required. This increases maintenance cost for
    link time optimizer significantly, which is not necessary. This approach
    also requires staying synchronized with linker developments on various
    platforms, which is not the main focus of the link time optimizer. Finally,
    this approach increases end user's build time due to the duplication of work
    done by this separate tool and the linker itself.

Multi-phase communication between ``libLTO`` and linker
=======================================================

The linker collects information about symbol definitions and uses in various
link objects which is more accurate than any information collected by other
tools during typical build cycles. The linker collects this information by
looking at the definitions and uses of symbols in native .o files and using
symbol visibility information. The linker also uses user-supplied information,
such as a list of exported symbols. LLVM optimizer collects control flow
information, data flow information and knows much more about program structure
from the optimizer's point of view. Our goal is to take advantage of tight
integration between the linker and the optimizer by sharing this information
during various linking phases.

Phase 1 : Read LLVM Bitcode Files
---------------------------------

The linker first reads all object files in natural order and collects symbol
information. This includes native object files as well as LLVM bitcode files.
To minimize the cost to the linker in the case that all .o files are native
object files, the linker only calls ``lto_module_create()`` when a supplied
object file is found to not be a native object file. If ``lto_module_create()``
returns that the file is an LLVM bitcode file, the linker then iterates over the
module using ``lto_module_get_symbol_name()`` and
``lto_module_get_symbol_attribute()`` to get all symbols defined and referenced.
This information is added to the linker's global symbol table.


The lto* functions are all implemented in a shared object libLTO. This allows
the LLVM LTO code to be updated independently of the linker tool. On platforms
that support it, the shared object is lazily loaded.

Phase 2 : Symbol Resolution
---------------------------

In this stage, the linker resolves symbols using global symbol table. It may
report undefined symbol errors, read archive members, replace weak symbols, etc.
The linker is able to do this seamlessly even though it does not know the exact
content of input LLVM bitcode files. If dead code stripping is enabled then the
linker collects the list of live symbols.

Phase 3 : Optimize Bitcode Files
--------------------------------

After symbol resolution, the linker tells the LTO shared object which symbols
are needed by native object files. In the example above, the linker reports
that only ``foo1()`` is used by native object files using
``lto_codegen_add_must_preserve_symbol()``. Next the linker invokes the LLVM
optimizer and code generators using ``lto_codegen_compile()`` which returns a
native object file creating by merging the LLVM bitcode files and applying
various optimization passes.

Phase 4 : Symbol Resolution after optimization
----------------------------------------------

In this phase, the linker reads optimized a native object file and updates the
internal global symbol table to reflect any changes. The linker also collects
information about any changes in use of external symbols by LLVM bitcode
files. In the example above, the linker notes that ``foo4()`` is not used any
more. If dead code stripping is enabled then the linker refreshes the live
symbol information appropriately and performs dead code stripping.

After this phase, the linker continues linking as if it never saw LLVM bitcode
files.

.. _libLTO:

``libLTO``
==========

``libLTO`` is a shared object that is part of the LLVM tools, and is intended
for use by a linker. ``libLTO`` provides an abstract C interface to use the LLVM
interprocedural optimizer without exposing details of LLVM's internals. The
intention is to keep the interface as stable as possible even when the LLVM
optimizer continues to evolve. It should even be possible for a completely
different compilation technology to provide a different libLTO that works with
their object files and the standard linker tool.

``lto_module_t``
----------------

A non-native object file is handled via an ``lto_module_t``. The following
functions allow the linker to check if a file (on disk or in a memory buffer) is
a file which libLTO can process:

.. code-block:: c

  lto_module_is_object_file(const char*)
  lto_module_is_object_file_for_target(const char*, const char*)
  lto_module_is_object_file_in_memory(const void*, size_t)
  lto_module_is_object_file_in_memory_for_target(const void*, size_t, const char*)

If the object file can be processed by ``libLTO``, the linker creates a
``lto_module_t`` by using one of:

.. code-block:: c

  lto_module_create(const char*)
  lto_module_create_from_memory(const void*, size_t)

and when done, the handle is released via

.. code-block:: c

  lto_module_dispose(lto_module_t)


The linker can introspect the non-native object file by getting the number of
symbols and getting the name and attributes of each symbol via:

.. code-block:: c

  lto_module_get_num_symbols(lto_module_t)
  lto_module_get_symbol_name(lto_module_t, unsigned int)
  lto_module_get_symbol_attribute(lto_module_t, unsigned int)

The attributes of a symbol include the alignment, visibility, and kind.

Tools working with object files on Darwin (e.g. lipo) may need to know properties like the CPU type:

.. code-block:: c

  lto_module_get_macho_cputype(lto_module_t mod, unsigned int *out_cputype, unsigned int *out_cpusubtype)

``lto_code_gen_t``
------------------

Once the linker has loaded each non-native object files into an
``lto_module_t``, it can request ``libLTO`` to process them all and generate a
native object file. This is done in a couple of steps. First, a code generator
is created with:

.. code-block:: c

  lto_codegen_create()

Then, each non-native object file is added to the code generator with:

.. code-block:: c

  lto_codegen_add_module(lto_code_gen_t, lto_module_t)

The linker then has the option of setting some codegen options. Whether or not
to generate DWARF debug info is set with:

.. code-block:: c

  lto_codegen_set_debug_model(lto_code_gen_t)

which kind of position independence is set with:

.. code-block:: c

  lto_codegen_set_pic_model(lto_code_gen_t)

And each symbol that is referenced by a native object file or otherwise must not
be optimized away is set with:

.. code-block:: c

  lto_codegen_add_must_preserve_symbol(lto_code_gen_t, const char*)

After all these settings are done, the linker requests that a native object file
be created from the modules with the settings using:

.. code-block:: c

  lto_codegen_compile(lto_code_gen_t, size*)

which returns a pointer to a buffer containing the generated native object file.
The linker then parses that and links it with the rest of the native object
files.
