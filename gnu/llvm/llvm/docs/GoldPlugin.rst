====================
The LLVM gold plugin
====================

Introduction
============

Building with link time optimization requires cooperation from
the system linker. LTO support on Linux systems is available via the
`gold linker`_ which supports LTO via plugins. This is the same mechanism
used by the `GCC LTO`_ project.

The LLVM gold plugin implements the gold plugin interface on top of
:ref:`libLTO`.  The same plugin can also be used by other tools such as
``ar`` and ``nm``.  Note that ld.bfd from binutils version 2.21.51.0.2
and above also supports LTO via plugins.  However, usage of the LLVM
gold plugin with ld.bfd is not tested and therefore not officially
supported or recommended.

As of LLVM 15, the gold plugin will ignore bitcode from the ``.llvmbc``
section inside of ELF object files.  However, LTO with bitcode files
is still supported.

.. _`gold linker`: http://sourceware.org/binutils
.. _`GCC LTO`: http://gcc.gnu.org/wiki/LinkTimeOptimization
.. _`gold plugin interface`: http://gcc.gnu.org/wiki/whopr/driver

.. _lto-how-to-build:

How to build it
===============

You need to have gold with plugin support and build the LLVMgold plugin.
The gold linker is installed as ld.gold. To see whether gold is the default
on your system, run ``/usr/bin/ld -v``. It will report "GNU
gold" or else "GNU ld" if not. If gold is already installed at
``/usr/bin/ld.gold``, one option is to simply make that the default by
backing up your existing ``/usr/bin/ld`` and creating a symbolic link
with ``ln -s /usr/bin/ld.gold /usr/bin/ld``. Alternatively, you can build
with clang's ``-fuse-ld=gold`` or add ``-fuse-ld=gold`` to LDFLAGS, which will
cause the clang driver to invoke ``/usr/bin/ld.gold`` directly.

If you have gold installed, check for plugin support by running
``/usr/bin/ld.gold -plugin``. If it complains "missing argument" then
you have plugin support. If not, and you get an error such as "unknown option",
then you will either need to build gold or install a version with plugin
support.

* Download, configure and build gold with plugin support:

  .. code-block:: bash

     $ git clone --depth 1 git://sourceware.org/git/binutils-gdb.git binutils
     $ mkdir build
     $ cd build
     $ ../binutils/configure --enable-gold --enable-plugins --disable-werror
     $ make all-gold

  That should leave you with ``build/gold/ld-new`` which supports
  the ``-plugin`` option. Running ``make`` will additionally build
  ``build/binutils/ar`` and ``nm-new`` binaries supporting plugins.

  Once you're ready to switch to using gold, backup your existing
  ``/usr/bin/ld`` then replace it with ``ld-new``. Alternatively, install
  in ``/usr/bin/ld.gold`` and use ``-fuse-ld=gold`` as described earlier.

  Optionally, add ``--enable-gold=default`` to the above configure invocation
  to automatically install the newly built gold as the default linker with
  ``make install``.

* Build the LLVMgold plugin. Run CMake with
  ``-DLLVM_BINUTILS_INCDIR=/path/to/binutils/include``.  The correct include
  path will contain the file ``plugin-api.h``.

Usage
=====

You should produce bitcode files from ``clang`` with the option
``-flto``. This flag will also cause ``clang`` to look for the gold plugin in
the ``lib`` directory under its prefix and pass the ``-plugin`` option to
``ld``. It will not look for an alternate linker without ``-fuse-ld=gold``,
which is why you otherwise need gold to be the installed system linker in
your path.

``ar`` and ``nm`` also accept the ``-plugin`` option and it's possible to
to install ``LLVMgold.so`` to ``/usr/lib/bfd-plugins`` for a seamless setup.
If you built your own gold, be sure to install the ``ar`` and ``nm-new`` you
built to ``/usr/bin``.


Example of link time optimization
---------------------------------

The following example shows a worked example of the gold plugin mixing LLVM
bitcode and native code.

.. code-block:: c

   --- a.c ---
   #include <stdio.h>

   extern void foo1(void);
   extern void foo4(void);

   void foo2(void) {
     printf("Foo2\n");
   }

   void foo3(void) {
     foo4();
   }

   int main(void) {
     foo1();
   }

   --- b.c ---
   #include <stdio.h>

   extern void foo2(void);

   void foo1(void) {
     foo2();
   }

   void foo4(void) {
     printf("Foo4");
   }

.. code-block:: bash

   --- command lines ---
   $ clang -flto a.c -c -o a.o      # <-- a.o is LLVM bitcode file
   $ ar q a.a a.o                   # <-- a.a is an archive with LLVM bitcode
   $ clang b.c -c -o b.o            # <-- b.o is native object file
   $ clang -flto a.a b.o -o main    # <-- link with LLVMgold plugin

Gold informs the plugin that foo3 is never referenced outside the IR,
leading LLVM to delete that function. However, unlike in the :ref:`libLTO
example <libLTO-example>` gold does not currently eliminate foo4.

Quickstart for using LTO with autotooled projects
=================================================

Once your system ``ld``, ``ar``, and ``nm`` all support LLVM bitcode,
everything is in place for an easy to use LTO build of autotooled projects:

* Follow the instructions :ref:`on how to build LLVMgold.so
  <lto-how-to-build>`.

* Install the newly built binutils to ``$PREFIX``

* Copy ``Release/lib/LLVMgold.so`` to ``$PREFIX/lib/bfd-plugins/``

* Set environment variables (``$PREFIX`` is where you installed clang and
  binutils):

  .. code-block:: bash

     export CC="$PREFIX/bin/clang -flto"
     export CXX="$PREFIX/bin/clang++ -flto"
     export AR="$PREFIX/bin/ar"
     export NM="$PREFIX/bin/nm"
     export RANLIB=/bin/true #ranlib is not needed, and doesn't support .bc files in .a

* Or you can just set your path:

  .. code-block:: bash

     export PATH="$PREFIX/bin:$PATH"
     export CC="clang -flto"
     export CXX="clang++ -flto"
     export RANLIB=/bin/true
* Configure and build the project as usual:

  .. code-block:: bash

     % ./configure && make && make check

The environment variable settings may work for non-autotooled projects too,
but you may need to set the ``LD`` environment variable as well.

Licensing
=========

Gold is licensed under the GPLv3. LLVMgold uses the interface file
``plugin-api.h`` from gold which means that the resulting ``LLVMgold.so``
binary is also GPLv3. This can still be used to link non-GPLv3 programs
just as much as gold could without the plugin.
