===================
FatLTO
===================
.. contents::
   :local:
   :depth: 2

.. toctree::
   :maxdepth: 1

Introduction
============

FatLTO objects are a special type of `fat object file
<https://en.wikipedia.org/wiki/Fat_binary>`_ that contain LTO compatible IR in
addition to generated object code, instead of containing object code for
multiple target architectures. This allows users to defer the choice of whether
to use LTO or not to link-time, and has been a feature available in other
compilers, like `GCC
<https://gcc.gnu.org/onlinedocs/gccint/LTO-Overview.html>`_, for some time.

Under FatLTO the compiler can emit standard object files which contain both the
machine code in the ``.text`` section and LLVM bitcode in the ``.llvm.lto``
section.

Overview
========

Within LLVM, FatLTO is supported by choosing the ``FatLTODefaultPipeline``.
This pipeline will:

#) Run the pre-link (Thin)LTO pipeline on the current module.
#) Embed the pre-link bitcode in a special ``.llvm.lto`` section.
#) Finish optimizing the module using the ModuleOptimization pipeline.
#) Emit the object file, including the new ``.llvm.lto`` section.

.. NOTE

   Previously, we conservatively ran independent pipelines on separate copies
   of the LLVM module to generate the bitcode section and the object code,
   which happen to be identical to those used outside of FatLTO. While that
   resulted in  compiled artifacts that were identical to those produced by the
   default and (Thin)LTO pipelines, module cloning led to some cases of
   miscompilation, and we have moved away from trying to keep bitcode
   generation and optimization completely disjoint.

   Bit-for-bit compatibility is not (and never was) a guarantee, and we reserve
   the right to change this at any time. Explicitly, users should not rely on
   the produced bitcode or object code to match their non-LTO counterparts
   precisely. They will exhibit similar performance characteristics, but may
   not be bit-for-bit the same.

Internally, the ``.llvm.lto`` section is created by running the
``EmbedBitcodePass`` after the ``ThinLTOPreLinkDefaultPipeline``. This pass is
responsible for emitting the ``.llvm.lto`` section. Afterwards, the
``ThinLTODefaultPipeline`` runs and the compiler can emit the fat object file.

Limitations
===========

Linkers
-------

Currently, using LTO with LLVM fat lto objects is supported by LLD and by the
GNU linkers via :doc:`GoldPlugin`. This may change in the future, but
extending support to other linkers isn't planned for now.

.. NOTE
   For standard linking the fat object files should be usable by any
   linker capable of using ELF objects, since the ``.llvm.lto`` section is
   marked ``SHF_EXCLUDE``.

Supported File Formats
----------------------

The current implementation only supports ELF files. At time of writing, it is
unclear if it will be useful to support other object file formats like ``COFF``
or ``Mach-O``.

Usage
=====

Clang users can specify ``-ffat-lto-objects`` with ``-flto`` or ``-flto=thin``.
Without the ``-flto`` option, ``-ffat-lto-objects`` has no effect.

Compile an object file using FatLTO:

.. code-block:: console

   $ clang -flto -ffat-lto-objects example.c -c -o example.o

Link using the object code from the fat object without LTO. This turns
``-ffat-lto-objects`` into a no-op, when ``-fno-lto`` is specified:

.. code-block:: console

   $ clang -fno-lto -ffat-lto-objects -fuse-ld=lld example.o

Alternatively, you can omit any references to LTO with fat objects and retain standard linker behavior:

.. code-block:: console

   $ clang -fuse-ld=lld example.o

Link using the LLVM bitcode from the fat object with Full LTO:

.. code-block:: console

   $ clang -flto -ffat-lto-objects -fuse-ld=lld example.o  # clang will pass --lto=full --fat-lto-objects to ld.lld

Link using the LLVM bitcode from the fat object with Thin LTO:

.. code-block:: console

   $ clang -flto=thin -ffat-lto-objects -fuse-ld=lld example.o  # clang will pass --lto=thin --fat-lto-objects to ld.lld
