DWARF Extensions
================

LLDB supports some DWARF extensions produced by Clang.

Clang ``-gmodules`` debug info
------------------------------

On Darwin platforms, including Apple macOS and iOS, Clang can emit
DWARF debug info for types found in `Clang
modules <https://clang.llvm.org/docs/Modules.html>`_ more efficiently.

From an on-disk storage perspective, Clang modules are precompiled
header files that contain serialized Clang ASTs of all the
declarations found in a Clang module. In traditional DWARF debug info,
two object files that were built from sources that imported the same
header file will both contain DWARF debug type info for types in that
header file. This can lead to a lot of redundant `debug
info <https://llvm.org/devmtg/2015-10/#talk19>`_.

When Clang compiles a Clang module or precompiled header with the
``-gmodules`` option, the precompiled header (``.pch``) or module
(``.pcm``) files become object file containers (on Darwin: Mach-O)
that hold a ``__clang_ast`` section with the serialized Clang AST and
various DWARF sections containing debug info for the type declarations
found in the header or module.

This allows Clang to omit these type definitions from the object
(``.o``) files and replace them with forward declarations to save
space. Type declarations in a Clang module are nested inside one
``DW_TAG_module``, or -- in the case of submodules -- multiple levels
of ``DW_TAG_module``. If a DWARF DIE needs to reference a type DIE
from another module, Clang emits a forward declaration of the
referenced DIE into a ``DW_TAG_module`` inside the same compile unit.

When a consumer sees a forward declaration that is nested inside a
``DW_TAG_module``, it knows that it can find the full type declaration
in an external ``.pcm`` or ``.pch`` file. To facilitate locating these
external dependencies, Clang emits skeleton CUs into each object file
that references external modules. Clang uses the same mechanism that
is used to locate external ``.dwo`` files on ELF-based platforms. The
``DW_AT_GNU_dwo_name`` contains the absolute path to the ``.pcm``
file, and the ``DW_AT_GNU_dwo_id`` is a hash of the contents that is
repeated in the ``DW_TAG_compile_unit`` of the ``.pcm`` file.

For example:

M.h

::

   struct A {
     int x;
   };


M.pcm

::

   DW_TAG_compile_unit
     DW_AT_GNU_dwo_id  (0xabcdef)
     DW_TAG_module
       DW_AT_name "M"
       DW_TAG_structure
         DW_AT_name "A"
         DW_TAG_member
           DW_AT_name "x"

A.c

::

   A a;

A.o

::

   DW_TAG_compile_unit
     DW_TAG_module
       DW_AT_name "M"
       DW_TAG_structure
         DW_AT_name "A"
         DW_AT_declaration (true)
     DW_TAG_variable
       DW_AT_name "a"
       DW_AT_type (local ref to fwd decl "A")

   DW_TAG_compile_unit
     DW_AT_GNU_dwo_id  (0xabcdef)
     DW_AT_GNU_dwo_name    ("M.pcm")

The debug info inside a ``.pcm`` file may recursively reference
further external types that are defined in other ``.pcm`` files. Clang
generates external references (and debug info inside the modules) for
the following types:

C:

- ``struct``
- ``union``
- ``enum``
- ``typedef``

Objective-C:

- all the C types listed above
- ``@interface``

C++:

- all the C types listed above
- ``namespace``
- any explicit ``extern template`` specializations

LLDB supports this DWARF extension only when debugging from ``.o``
files. The ``dsymutil`` debug info linker also understands this format
and will resolve all module type references to point straight to the
underlying defining declaration. Because of this a ``.dSYM`` bundle
will never contain any ``-gmodules``-style references.

Apple SDK information
---------------------

Clang and the Swift compiler emit information about the Xcode SDK that
was used to build a translation unit into the ``DW_TAG_compile_unit``.
The ``DW_AT_LLVM_sysroot`` attribute points to the SDK root
(equivalent to Clang's ``-isysroot`` option). The ``DW_AT_APPLE_sdk``
attribute contains the name of the SDK, for example ``MacOSX.sdk``.

Objective-C runtime
-------------------

Clang emits the Objective-C runtime version into the
``DW_TAG_compile_unit`` using the
``DW_AT_APPLE_major_runtime_version`` attribute. The value 2 stands
for Objective-C 2.0.
