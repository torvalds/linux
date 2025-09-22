==========================================
How to build Windows Itanium applications.
==========================================

Introduction
============

This document contains information describing how to create a Windows Itanium toolchain.

Windows Itanium allows you to deploy Itanium C++ ABI applications on top of the MS VS CRT.
This environment can use the Windows SDK headers directly and does not required additional
headers or additional runtime machinery (such as is used by mingw).

Windows Itanium Stack:

* Uses the Itanium C++ abi.
* libc++.
* libc++-abi.
* libunwind.
* The MS VS CRT.
* Is compatible with MS Windows SDK include headers.
* COFF/PE file format.
* LLD

Note: compiler-rt is not used. This functionality is supplied by the MS VCRT.

Prerequisites
=============

* The MS SDK is installed as part of MS Visual Studio.
* Clang with support for the windows-itanium triple.
* COFF LLD with support for the -autoimport switch.

Known issues:
=============

SJLJ exceptions, "-fsjlj-exceptions", are the only currently supported model.

link.exe (the MS linker) is unsuitable as it doesn't support auto-importing which
is currently required to link correctly. However, if that limitation is removed
then there are no other known issues with using link.exe.

Currently, there is a lack of a usable Windows compiler driver for Windows Itanium.
A reasonable work-around is to build clang with a windows-msvc default target and
then override the triple with e.g. "-Xclang -triple -Xclang x86_64-unknown-windows-itanium".
The linker can be specified with: "-fuse-ld=lld".

In the Itanium C++ ABI the first member of an object is a pointer to the vtable
for its class. The vtable is often emitted into the object file with the key function
and must be imported for classes marked dllimport. The pointers must be globally
unique. Unfortunately, the COFF/PE file format does not provide a mechanism to
store a runtime address from another DLL into this pointer (although runtime
addresses are patched into the IAT). Therefore, the compiler must emit some code,
that runs after IAT patching but before anything that might use the vtable pointers,
and sets the vtable pointer to the address from the IAT. For the special case of
the references to vtables for __cxxabiv1::__class_type_info from typeinto objects
there is no declaration available to the compiler so this can't be done. To allow
programs to link we currently rely on the -auto-import switch in LLD to auto-import
references to __cxxabiv1::__class_type_info pointers (see: https://reviews.llvm.org/D43184
for a related discussion). This allows for linking; but, code that actually uses
such fields will not work as they these will not be fixed up at runtime. See
_pei386_runtime_relocator which handles the runtime component of the autoimporting
scheme used for mingw and comments in https://reviews.llvm.org/D43184 and
https://reviews.llvm.org/D89518 for more.

Assembling a Toolchain:
=======================

The procedure is:

# Build an LLVM toolchain with support for Windows Itanium.
# Use the toolchain from step 1. to build libc++, libc++abi, and libunwind.

It is also possible to cross-compile from Linux.

One method of building the libraries in step 2. is to build them "stand-alone".
A stand-alone build doesn't involve the rest of the LLVM tree. The steps are:

* ``cd build-dir``
* ``cmake -DLLVM_PATH=<path to llvm checkout e.g. /llvm-project/> -DCMAKE_INSTALL_PREFIX=<install path> <other options> <path to project e.g. /llvm-project/libcxxabi>``
* ``<make program e.g. ninja>``
* ``<make program> install``

More information on standalone builds can be found in the build documentation for
the respective libraries. The next section discuss the salient options and modifications
required for building and installing the libraries using standalone builds. This assumes
that we are building libunwind and ibc++ as DLLs and statically linking libc++abi into
libc++. Other build configurations are possible, but they are not discussed here.

Common CMake configuration options:
-----------------------------------

* ``-D_LIBCPP_ABI_FORCE_ITANIUM'``

Tell the libc++ headers that the Itanium C++ ABI is being used.

* ``-DCMAKE_C_FLAGS="-lmsvcrt -llegacy_stdio_definitions -D_NO_CRT_STDIO_INLINE"``

Supply CRT definitions including stdio definitions that have been removed from the MS VS CRT.
We don't want the stdio functions declared inline as they will cause multiple definition
errors when the same symbols are pulled in from legacy_stdio_definitions.ib.

* ``-DCMAKE_INSTALL_PREFIX=<install path>``

Where to install the library and headers.

Building libunwind:
-------------------

* ``-DLIBUNWIND_ENABLE_SHARED=ON``
* ``-DLIBUNWIND_ENABLE_STATIC=OFF``

libunwind can be built as a DLL. It is not dependent on other projects.

* ``-DLIBUNWIND_USE_COMPILER_RT=OFF``

We use the MS runtime.

The CMake files will need to be edited to prevent them adding GNU specific libraries to the link line.

Building libc++abi:
-------------------

* ``-DLIBCXXABI_ENABLE_SHARED=OFF``
* ``-DLIBCXXABI_ENABLE_STATIC=ON``
* ``-DLIBCXX_ENABLE_SHARED=ON'``
* ``-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON``

To break the symbol dependency between libc++abi and libc++ we
build libc++abi as a static library and then statically link it
into the libc++ DLL. This necessitates setting the CMake file
to ensure that the visibility macros (which expand to dllexport/import)
are expanded as they will be needed when creating the final libc++
DLL later, see: https://reviews.llvm.org/D90021.

* ``-DLIBCXXABI_LIBCXX_INCLUDES=<path to libcxx>/include``

Where to find the libc++ headers

Building libc++:
----------------

* ``-DLIBCXX_ENABLE_SHARED=ON``
* ``-DLIBCXX_ENABLE_STATIC=OFF``

We build libc++ as a DLL and statically link libc++abi into it.

* ``-DLIBCXX_INSTALL_HEADERS=ON``

Install the headers.

* ``-DLIBCXX_USE_COMPILER_RT=OFF``

We use the MS runtime.

* ``-DLIBCXX_HAS_WIN32_THREAD_API=ON``

Windows Itanium does not offer a POSIX-like layer over WIN32.

* ``-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON``
* ``-DLIBCXX_CXX_ABI=libcxxabi``
* ``-DLIBCXX_CXX_ABI_INCLUDE_PATHS=<libcxxabi src path>/include``
* ``-DLIBCXX_CXX_ABI_LIBRARY_PATH=<libcxxabi build path>/lib``

Use the static libc++abi library built earlier.

* ``-DLIBCXX_NO_VCRUNTIME=ON``

Remove any dependency on the VC runtime - we need libc++abi to supply the C++ runtime.

* ``-DCMAKE_C_FLAGS=<path to installed unwind.lib>``

As we are statically linking against libcxxabi we need to link
against the unwind import library to resolve unwind references
from the libcxxabi objects.

* ``-DCMAKE_C_FLAGS+=' -UCLOCK_REALTIME'``

Prevent the inclusion of sys/time that MS doesn't provide.

Notes:
------

An example build recipe is available here: https://reviews.llvm.org/D88124
