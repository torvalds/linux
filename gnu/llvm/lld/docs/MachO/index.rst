Mach-O LLD Port
===============

LLD is a linker from the LLVM project that is a drop-in replacement
for system linkers and runs much faster than them. It also provides
features that are useful for toolchain developers. This document
will describe the Mach-O port.

Features
--------

- LLD is a drop-in replacement for Apple's Mach-O linker, ld64, that accepts the
  same command line arguments.

- LLD is very fast. When you link a large program on a multicore
  machine, you can expect that LLD runs more than twice as fast as the ld64
  linker.

Download
--------

LLD is available as a pre-built binary by going to the `latest release <https://github.com/llvm/llvm-project/releases>`_,
downloading the appropriate bundle (``clang+llvm-<version>-<your architecture>-<your platform>.tar.xz``),
decompressing it, and locating the binary at ``bin/ld64.lld``. Note
that if ``ld64.lld`` is moved out of ``bin``, it must still be accompanied
by its sibling file ``lld``, as ``ld64.lld`` is technically a symlink to ``lld``.

Build
-----

The easiest way to build LLD is to
check out the entire LLVM projects/sub-projects from a git mirror and
build that tree. You need ``cmake`` and of course a C++ compiler.

.. code-block:: console

  $ git clone https://github.com/llvm/llvm-project llvm-project
  $ mkdir build
  $ cd build
  $ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS='lld' ../llvm-project/llvm
  $ ninja check-lld-macho

Then you can find output binary at ``build/bin/ld64.lld``. Note
that if ``ld64.lld`` is moved out of ``bin``, it must still be accompanied
by its sibling file ``lld``, as ``ld64.lld`` is technically a symlink to ``lld``.

Using LLD
---------

LLD can be used by adding ``-fuse-ld=/path/to/ld64.lld`` to the linker flags.
For Xcode, this can be done by adding it to "Other linker flags" in the build
settings. For Bazel, this can be done with ``--linkopt`` or with
`rules_apple_linker <https://github.com/keith/rules_apple_linker>`_.

.. seealso::

  :doc:`ld64-vs-lld` has more info on the differences between the two linkers.

.. toctree::
   :hidden:

   ld64-vs-lld
