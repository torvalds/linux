=======
ThinLTO
=======

.. contents::
   :local:

Introduction
============

*ThinLTO* compilation is a new type of LTO that is both scalable and
incremental. *LTO* (Link Time Optimization) achieves better
runtime performance through whole-program analysis and cross-module
optimization. However, monolithic LTO implements this by merging all
input into a single module, which is not scalable
in time or memory, and also prevents fast incremental compiles.

In ThinLTO mode, as with regular LTO, clang emits LLVM bitcode after the
compile phase. The ThinLTO bitcode is augmented with a compact summary
of the module. During the link step, only the summaries are read and
merged into a combined summary index, which includes an index of function
locations for later cross-module function importing. Fast and efficient
whole-program analysis is then performed on the combined summary index.

However, all transformations, including function importing, occur
later when the modules are optimized in fully parallel backends.
By default, linkers_ that support ThinLTO are set up to launch
the ThinLTO backends in threads. So the usage model is not affected
as the distinction between the fast serial thin link step and the backends
is transparent to the user.

For more information on the ThinLTO design and current performance,
see the LLVM blog post `ThinLTO: Scalable and Incremental LTO
<http://blog.llvm.org/2016/06/thinlto-scalable-and-incremental-lto.html>`_.
While tuning is still in progress, results in the blog post show that
ThinLTO already performs well compared to LTO, in many cases matching
the performance improvement.

Current Status
==============

Clang/LLVM
----------
.. _compiler:

The 3.9 release of clang includes ThinLTO support. However, ThinLTO
is under active development, and new features, improvements and bugfixes
are being added for the next release. For the latest ThinLTO support,
`build a recent version of clang and LLVM
<https://llvm.org/docs/CMake.html>`_.

Linkers
-------
.. _linkers:
.. _linker:

ThinLTO is currently supported for the following linkers:

- **gold (via the gold-plugin)**:
  Similar to monolithic LTO, this requires using
  a `gold linker configured with plugins enabled
  <https://llvm.org/docs/GoldPlugin.html>`_.
- **ld64**:
  Starting with `Xcode 8 <https://developer.apple.com/xcode/>`_.
- **lld**:
  Starting with r284050 for ELF, r298942 for COFF.

Usage
=====

Basic
-----

To utilize ThinLTO, simply add the -flto=thin option to compile and link. E.g.

.. code-block:: console

  % clang -flto=thin -O2 file1.c file2.c -c
  % clang -flto=thin -O2 file1.o file2.o -o a.out

When using lld-link, the -flto option need only be added to the compile step:

.. code-block:: console

  % clang-cl -flto=thin -O2 -c file1.c file2.c
  % lld-link /out:a.exe file1.obj file2.obj

As mentioned earlier, by default the linkers will launch the ThinLTO backend
threads in parallel, passing the resulting native object files back to the
linker for the final native link.  As such, the usage model is the same as
non-LTO.

With gold, if you see an error during the link of the form:

.. code-block:: console

  /usr/bin/ld: error: /path/to/clang/bin/../lib/LLVMgold.so: could not load plugin library: /path/to/clang/bin/../lib/LLVMgold.so: cannot open shared object file: No such file or directory

Then either gold was not configured with plugins enabled, or clang
was not built with ``-DLLVM_BINUTILS_INCDIR`` set properly. See
the instructions for the
`LLVM gold plugin <https://llvm.org/docs/GoldPlugin.html#how-to-build-it>`_.

Controlling Backend Parallelism
-------------------------------
.. _parallelism:

By default, the ThinLTO link step will launch as many
threads in parallel as there are cores. If the number of
cores can't be computed for the architecture, then it will launch
``std::thread::hardware_concurrency`` number of threads in parallel.
For machines with hyper-threading, this is the total number of
virtual cores. For some applications and machine configurations this
may be too aggressive, in which case the amount of parallelism can
be reduced to ``N`` via:

- gold:
  ``-Wl,-plugin-opt,jobs=N``
- ld64:
  ``-Wl,-mllvm,-threads=N``
- ld.lld, ld64.lld:
  ``-Wl,--thinlto-jobs=N``
- lld-link:
  ``/opt:lldltojobs=N``

Other possible values for ``N`` are:

- 0:
  Use one thread per physical core (default)
- 1:
  Use a single thread only (disable multi-threading)
- all:
  Use one thread per logical core (uses all hyper-threads)

Incremental
-----------
.. _incremental:

ThinLTO supports fast incremental builds through the use of a cache,
which currently must be enabled through a linker option.

- gold (as of LLVM 4.0):
  ``-Wl,-plugin-opt,cache-dir=/path/to/cache``
- ld64 (supported since clang 3.9 and Xcode 8) and Mach-O ld64.lld (as of LLVM
  15.0):
  ``-Wl,-cache_path_lto,/path/to/cache``
- ELF ld.lld (as of LLVM 5.0):
  ``-Wl,--thinlto-cache-dir=/path/to/cache``
- COFF lld-link (as of LLVM 6.0):
  ``/lldltocache:/path/to/cache``

Cache Pruning
-------------

To help keep the size of the cache under control, ThinLTO supports cache
pruning. Cache pruning is supported with gold, ld64, and lld, but currently only
gold and lld allow you to control the policy with a policy string. The cache
policy must be specified with a linker option.

- gold (as of LLVM 6.0):
  ``-Wl,-plugin-opt,cache-policy=POLICY``
- ELF ld.lld (as of LLVM 5.0), Mach-O ld64.lld (as of LLVM 15.0):
  ``-Wl,--thinlto-cache-policy=POLICY``
- COFF lld-link (as of LLVM 6.0):
  ``/lldltocachepolicy:POLICY``

A policy string is a series of key-value pairs separated by ``:`` characters.
Possible key-value pairs are:

- ``cache_size=X%``: The maximum size for the cache directory is ``X`` percent
  of the available space on the disk. Set to 100 to indicate no limit,
  50 to indicate that the cache size will not be left over half the available
  disk space. A value over 100 is invalid. A value of 0 disables the percentage
  size-based pruning. The default is 75%.

- ``cache_size_bytes=X``, ``cache_size_bytes=Xk``, ``cache_size_bytes=Xm``,
  ``cache_size_bytes=Xg``:
  Sets the maximum size for the cache directory to ``X`` bytes (or KB, MB,
  GB respectively). A value over the amount of available space on the disk
  will be reduced to the amount of available space. A value of 0 disables
  the byte size-based pruning. The default is no byte size-based pruning.

  Note that ThinLTO will apply both size-based pruning policies simultaneously,
  and changing one does not affect the other. For example, a policy of
  ``cache_size_bytes=1g`` on its own will cause both the 1GB and default 75%
  policies to be applied unless the default ``cache_size`` is overridden.

- ``cache_size_files=X``:
  Set the maximum number of files in the cache directory. Set to 0 to indicate
  no limit. The default is 1000000 files.

- ``prune_after=Xs``, ``prune_after=Xm``, ``prune_after=Xh``: Sets the
  expiration time for cache files to ``X`` seconds (or minutes, hours
  respectively).  When a file hasn't been accessed for ``prune_after`` seconds,
  it is removed from the cache. A value of 0 disables the expiration-based
  pruning. The default is 1 week.

- ``prune_interval=Xs``, ``prune_interval=Xm``, ``prune_interval=Xh``:
  Sets the pruning interval to ``X`` seconds (or minutes, hours
  respectively). This is intended to be used to avoid scanning the directory
  too often. It does not impact the decision of which files to prune. A
  value of 0 forces the scan to occur. The default is every 20 minutes.

Clang Bootstrap
---------------

To `bootstrap clang/LLVM <https://llvm.org/docs/AdvancedBuilds.html#bootstrap-builds>`_
with ThinLTO, follow these steps:

1. The host compiler_ must be a version of clang that supports ThinLTO.
#. The host linker_ must support ThinLTO (and in the case of gold, must be
   `configured with plugins enabled <https://llvm.org/docs/GoldPlugin.html>`_).
#. Use the following additional `CMake variables
   <https://llvm.org/docs/CMake.html#options-and-variables>`_
   when configuring the bootstrap compiler build:

  * ``-DLLVM_ENABLE_LTO=Thin``
  * ``-DCMAKE_C_COMPILER=/path/to/host/clang``
  * ``-DCMAKE_CXX_COMPILER=/path/to/host/clang++``
  * ``-DCMAKE_RANLIB=/path/to/host/llvm-ranlib``
  * ``-DCMAKE_AR=/path/to/host/llvm-ar``

  Or, on Windows:

  * ``-DLLVM_ENABLE_LTO=Thin``
  * ``-DCMAKE_C_COMPILER=/path/to/host/clang-cl.exe``
  * ``-DCMAKE_CXX_COMPILER=/path/to/host/clang-cl.exe``
  * ``-DCMAKE_LINKER=/path/to/host/lld-link.exe``
  * ``-DCMAKE_RANLIB=/path/to/host/llvm-ranlib.exe``
  * ``-DCMAKE_AR=/path/to/host/llvm-ar.exe``

#. To use additional linker arguments for controlling the backend
   parallelism_ or enabling incremental_ builds of the bootstrap compiler,
   after configuring the build, modify the resulting CMakeCache.txt file in the
   build directory. Specify any additional linker options after
   ``CMAKE_EXE_LINKER_FLAGS:STRING=``. Note the configure may fail if
   linker plugin options are instead specified directly in the previous step.

The ``BOOTSTRAP_LLVM_ENABLE_LTO=Thin`` will enable ThinLTO for stage 2 and
stage 3 in case the compiler used for stage 1 does not support the ThinLTO
option.

More Information
================

* From LLVM project blog:
  `ThinLTO: Scalable and Incremental LTO
  <http://blog.llvm.org/2016/06/thinlto-scalable-and-incremental-lto.html>`_
