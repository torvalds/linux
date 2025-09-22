.. title:: LLDB Homepage

The LLDB Debugger
=================

Welcome to the LLDB documentation!

LLDB is a next generation, high-performance debugger. It is built as a set of
reusable components which highly leverage existing libraries in the larger
`LLVM Project <https://llvm.org>`_, such as the Clang expression parser and
LLVM disassembler.

LLDB is the default debugger in Xcode on macOS and supports debugging C,
Objective-C and C++ on the desktop and iOS devices and simulator.

All of the code in the LLDB project is available under the
`"Apache 2.0 License with LLVM exceptions"`_.

.. _"Apache 2.0 License with LLVM exceptions": https://llvm.org/docs/DeveloperPolicy.html#new-llvm-project-license-framework

Using LLDB
----------

For an introduction into the LLDB command language, head over to the `LLDB
Tutorial <https://lldb.llvm.org/use/tutorial.html>`_. For users already familiar
with GDB there is a cheat sheet listing common tasks and their LLDB equivalent
in the `GDB to LLDB command map <https://lldb.llvm.org/use/map.html>`_.

There are also multiple resources on how to script LLDB using Python: the
:doc:`use/python-reference` is a great starting point for that.

Compiler Integration Benefits
-----------------------------

LLDB converts debug information into Clang types so that it can
leverage the Clang compiler infrastructure. This allows LLDB to support the
latest C, C++, Objective-C and Objective-C++ language features and runtimes in
expressions without having to reimplement any of this functionality. It also
leverages the compiler to take care of all ABI details when making functions
calls for expressions, when disassembling instructions and extracting
instruction details, and much more.

The major benefits include:

- Up to date language support for C, C++, Objective-C
- Multi-line expressions that can declare local variables and types
- Utilize the JIT for expressions when supported
- Evaluate expression Intermediate Representation (IR) when JIT can't be used

Reusability
-----------

The LLDB debugger APIs are exposed as a C++ object oriented interface in a
shared library. The lldb command line tool links to, and uses this public API.
On macOS the shared library is exposed as a framework named LLDB.framework,
and Unix systems expose it as lldb.so. The entire API is also then exposed
through Python script bindings which allow the API to be used within the LLDB
embedded script interpreter, and also in any python script that loads the
lldb.py module in standard python script files. See the Python Reference page
for more details on how and where Python can be used with the LLDB API.

Sharing the LLDB API allows LLDB to not only be used for debugging, but also
for symbolication, disassembly, object and symbol file introspection, and much
more.

Platform Support
----------------

LLDB is known to work on the following platforms, but ports to new platforms
are welcome:

* macOS debugging for i386, x86_64 and AArch64
* iOS, tvOS, and watchOS simulator debugging on i386, x86_64 and AArch64
* iOS, tvOS, and watchOS device debugging on ARM and AArch64
* Linux user-space debugging for i386, x86_64, ARM, AArch64, PPC64le, s390x
* FreeBSD user-space debugging for i386, x86_64, ARM, AArch64, MIPS64, PPC
* NetBSD user-space debugging for i386 and x86_64
* Windows user-space debugging for i386, x86_64, ARM and AArch64 (*)

(*) Support for Windows is under active development. Basic functionality is
expected to work, with functionality improving rapidly. ARM and AArch64 support
is more experimental, with more known issues than the others.

Get Involved
------------

Check out the LLVM source-tree with git and find the sources in the `lldb`
subdirectory:

::

  $ git clone https://github.com/llvm/llvm-project.git

Note that LLDB generally builds from top-of-trunk using CMake and Ninja.
Additionally it builds:

* on macOS with a :ref:`generated Xcode project <CMakeGeneratedXcodeProject>`
* on Linux and FreeBSD with Clang and libstdc++/libc++
* on NetBSD with GCC/Clang and libstdc++/libc++
* on Windows with a generated project for VS 2017 or higher

See the :doc:`LLDB Build Page <resources/build>` for build instructions.

Discussions about LLDB should go to the `LLDB forum
<https://discourse.llvm.org/c/subprojects/lldb>`__ or the ``lldb`` channel on
the `LLVM Discord server <https://discord.com/invite/xS7Z362>`__.

For contributions follow the
`LLVM contribution process <https://llvm.org/docs/Contributing.html>`__. Commit
messages are automatically sent to the `lldb-commits
<http://lists.llvm.org/mailman/listinfo/lldb-commits>`__ mailing list.

See the :doc:`Projects page <resources/projects>` if you are looking for some
interesting areas to contribute to lldb.

.. toctree::
   :hidden:
   :maxdepth: 1
   :caption: Using LLDB

   use/tutorial
   use/map
   use/formatting
   use/variable
   use/symbolication
   use/symbols
   use/remote
   use/intel_pt
   use/ondemand
   use/aarch64-linux
   use/troubleshooting
   use/links
   Man Page <man/lldb>

.. toctree::
   :hidden:
   :maxdepth: 1
   :caption: Scripting LLDB

   use/python
   use/python-reference
   Python API <python_api>
   Python Extensions <python_extensions>


.. toctree::
   :hidden:
   :maxdepth: 1
   :caption: Developing LLDB

   resources/overview
   resources/contributing
   resources/build
   resources/test
   resources/qemu-testing
   resources/debugging
   resources/fuzzing
   resources/sbapi
   resources/dataformatters
   resources/extensions
   resources/lldbgdbremote
   resources/lldbplatformpackets
   resources/caveats
   resources/projects
   resources/lldbdap
   Public C++ API <https://lldb.llvm.org/cpp_reference/namespacelldb.html>
   Private C++ API <https://lldb.llvm.org/cpp_reference/index.html>

.. toctree::
   :hidden:
   :maxdepth: 1
   :caption: External Links

   Source Code <https://github.com/llvm/llvm-project>
   Releases <https://github.com/llvm/llvm-project/releases>
   Discussion Forums <https://discourse.llvm.org/c/subprojects/lldb/8>
   Developer Policy <https://llvm.org/docs/DeveloperPolicy.html>
   Bug Reports <https://github.com/llvm/llvm-project/issues?q=is%3Aissue+label%3Alldb+is%3Aopen>
   Code Reviews <https://github.com/llvm/llvm-project/pulls?q=is%3Apr+label%3Alldb+is%3Aopen>
