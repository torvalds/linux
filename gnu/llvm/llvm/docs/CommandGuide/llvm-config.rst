llvm-config - Print LLVM compilation options
============================================

.. program:: llvm-config

SYNOPSIS
--------

**llvm-config** *option* [*components*...]

DESCRIPTION
-----------

**llvm-config** makes it easier to build applications that use LLVM.  It can
print the compiler flags, linker flags and object libraries needed to link
against LLVM.

EXAMPLES
--------

To link against the JIT:

.. code-block:: sh

   g++ `llvm-config --cxxflags` -o HowToUseJIT.o -c HowToUseJIT.cpp
   g++ `llvm-config --ldflags` -o HowToUseJIT HowToUseJIT.o \
       `llvm-config --libs engine bcreader scalaropts`

OPTIONS
-------

**--assertion-mode**

 Print the assertion mode used when LLVM was built (ON or OFF).

**--bindir**

 Print the installation directory for LLVM binaries.

**--build-mode**

 Print the build mode used when LLVM was built (e.g. Debug or Release).

**--build-system**

 Print the build system used to build LLVM (e.g. `cmake` or `gn`).

**--cflags**

 Print the C compiler flags needed to use LLVM headers.

**--cmakedir**

 Print the installation directory for LLVM CMake modules.

**--components**

 Print all valid component names.

**--cppflags**

 Print the C preprocessor flags needed to use LLVM headers.

**--cxxflags**

 Print the C++ compiler flags needed to use LLVM headers.

**--has-rtti**

 Print whether or not LLVM was built with rtti (YES or NO).

**--help**

 Print a summary of **llvm-config** arguments.

**--host-target**

 Print the target triple used to configure LLVM.

**--ignore-libllvm**

 Ignore libLLVM and link component libraries instead.

**--includedir**

 Print the installation directory for LLVM headers.

**--ldflags**

 Print the flags needed to link against LLVM libraries.

**--libdir**

 Print the installation directory for LLVM libraries.

**--libfiles**

 Similar to **--libs**, but print the full path to each library file.  This is
 useful when creating makefile dependencies, to ensure that a tool is relinked if
 any library it uses changes.

**--libnames**

 Similar to **--libs**, but prints the bare filenames of the libraries
 without **-l** or pathnames.  Useful for linking against a not-yet-installed
 copy of LLVM.

**--libs**

 Print all the libraries needed to link against the specified LLVM
 *components*, including any dependencies.

**--link-shared**

 Link the components as shared libraries.

**--link-static**

 Link the component libraries statically.

**--obj-root**

 Print the object root used to build LLVM.

**--prefix**

 Print the installation prefix for LLVM.

**--shared-mode**

 Print how the provided components can be collectively linked (`shared` or `static`).

**--system-libs**

 Print all the system libraries needed to link against the specified LLVM
 *components*, including any dependencies.

**--targets-built**

 Print the component names for all targets supported by this copy of LLVM.

**--version**

 Print the version number of LLVM.


COMPONENTS
----------

To print a list of all available components, run **llvm-config
--components**.  In most cases, components correspond directly to LLVM
libraries.  Useful "virtual" components include:

**all**

 Includes all LLVM libraries.  The default if no components are specified.

**backend**

 Includes either a native backend or the C backend.

**engine**

 Includes either a native JIT or the bitcode interpreter.


EXIT STATUS
-----------

If **llvm-config** succeeds, it will exit with 0.  Otherwise, if an error
occurs, it will exit with a non-zero value.
