[![Run Linux CI tasks](https://github.com/libexpat/libexpat/actions/workflows/linux.yml/badge.svg)](https://github.com/libexpat/libexpat/actions/workflows/linux.yml)
[![Packaging status](https://repology.org/badge/tiny-repos/expat.svg)](https://repology.org/metapackage/expat/versions)
[![Downloads SourceForge](https://img.shields.io/sourceforge/dt/expat?label=Downloads%20SourceForge)](https://sourceforge.net/projects/expat/files/)
[![Downloads GitHub](https://img.shields.io/github/downloads/libexpat/libexpat/total?label=Downloads%20GitHub)](https://github.com/libexpat/libexpat/releases)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/10205/badge)](https://www.bestpractices.dev/projects/10205)

> [!CAUTION]
>
> Expat is **understaffed** and without funding.
> There is a [call for help with details](https://github.com/libexpat/libexpat/blob/master/expat/Changes)
> at the top of the `Changes` file.


# Expat, Release 2.7.2

This is Expat, a C99 library for parsing
[XML 1.0 Fourth Edition](https://www.w3.org/TR/2006/REC-xml-20060816/), started by
[James Clark](https://en.wikipedia.org/wiki/James_Clark_%28programmer%29) in 1997.
Expat is a stream-oriented XML parser.  This means that you register
handlers with the parser before starting the parse.  These handlers
are called when the parser discovers the associated structures in the
document being parsed.  A start tag is an example of the kind of
structures for which you may register handlers.

Expat supports the following C99 compilers:

- GNU GCC >=4.5 (for use from C) or GNU GCC >=4.8.1 (for use from C++)
- LLVM Clang >=3.5
- Microsoft Visual Studio >=17.0/2022
  (the oldest version supported by the [official GitHub Actions Windows images](https://github.com/actions/runner-images))

Windows users can use the
[`expat-win32bin-*.*.*.{exe,zip}` download](https://github.com/libexpat/libexpat/releases),
which includes both pre-compiled libraries and executables, and source code for
developers.

Expat is [free software](https://www.gnu.org/philosophy/free-sw.en.html).
You may copy, distribute, and modify it under the terms of the License
contained in the file
[`COPYING`](https://github.com/libexpat/libexpat/blob/master/expat/COPYING)
distributed with this package.
This license is the same as the MIT/X Consortium license.


## Using libexpat in your CMake-Based Project

There are three documented ways of using libexpat with CMake:

### a) `find_package` with Module Mode

This approach leverages CMake's own [module `FindEXPAT`](https://cmake.org/cmake/help/latest/module/FindEXPAT.html).

Notice the *uppercase* `EXPAT` in the following example:

```cmake
cmake_minimum_required(VERSION 3.10)

project(hello VERSION 1.0.0)

find_package(EXPAT 2.2.8 MODULE REQUIRED)

add_executable(hello
    hello.c
)

target_link_libraries(hello PUBLIC EXPAT::EXPAT)
```

### b) `find_package` with Config Mode

This approach requires files from…

- libexpat >=2.2.8 where packaging uses the CMake build system
or
- libexpat >=2.3.0 where packaging uses the GNU Autotools build system
  on Linux
or
- libexpat >=2.4.0 where packaging uses the GNU Autotools build system
  on macOS or MinGW.

Notice the *lowercase* `expat` in the following example:

```cmake
cmake_minimum_required(VERSION 3.10)

project(hello VERSION 1.0.0)

find_package(expat 2.2.8 CONFIG REQUIRED char dtd ns)

add_executable(hello
    hello.c
)

target_link_libraries(hello PUBLIC expat::expat)
```

### c) The `FetchContent` module

This approach — as demonstrated below — requires CMake >=3.18 for both the
[`FetchContent` module](https://cmake.org/cmake/help/latest/module/FetchContent.html)
and its support for the `SOURCE_SUBDIR` option to be available.

Please note that:
- Use of the `FetchContent` module with *non-release* SHA1s or `master`
  of libexpat is neither advised nor considered officially supported.
- Pinning to a specific commit is great for robust CI.
- Pinning to a specific commit needs updating every time there is a new
  release of libexpat — either manually or through automation —,
  to not miss out on libexpat security updates.

For an example that pulls in libexpat via Git:

```cmake
cmake_minimum_required(VERSION 3.18)

include(FetchContent)

project(hello VERSION 1.0.0)

FetchContent_Declare(
    expat
    GIT_REPOSITORY https://github.com/libexpat/libexpat/
    GIT_TAG        000000000_GIT_COMMIT_SHA1_HERE_000000000  # i.e. Git tag R_0_Y_Z
    SOURCE_SUBDIR  expat/
)

FetchContent_MakeAvailable(expat)

add_executable(hello
    hello.c
)

target_link_libraries(hello PUBLIC expat)
```


## Building from a Git Clone

If you are building Expat from a check-out from the
[Git repository](https://github.com/libexpat/libexpat/),
you need to run a script that generates the configure script using the
GNU autoconf and libtool tools.  To do this, you need to have
autoconf 2.58 or newer. Run the script like this:

```console
./buildconf.sh
```

Once this has been done, follow the same instructions as for building
from a source distribution.


## Building from a Source Distribution

### a) Building with the configure script (i.e. GNU Autotools)

To build Expat from a source distribution, you first run the
configuration shell script in the top level distribution directory:

```console
./configure
```

There are many options which you may provide to configure (which you
can discover by running configure with the `--help` option).  But the
one of most interest is the one that sets the installation directory.
By default, the configure script will set things up to install
libexpat into `/usr/local/lib`, `expat.h` into `/usr/local/include`, and
`xmlwf` into `/usr/local/bin`.  If, for example, you'd prefer to install
into `/home/me/mystuff/lib`, `/home/me/mystuff/include`, and
`/home/me/mystuff/bin`, you can tell `configure` about that with:

```console
./configure --prefix=/home/me/mystuff
```

Another interesting option is to enable 64-bit integer support for
line and column numbers and the over-all byte index:

```console
./configure CPPFLAGS=-DXML_LARGE_SIZE
```

However, such a modification would be a breaking change to the ABI
and is therefore not recommended for general use &mdash; e.g. as part of
a Linux distribution &mdash; but rather for builds with special requirements.

After running the configure script, the `make` command will build
things and `make install` will install things into their proper
location.  Have a look at the `Makefile` to learn about additional
`make` options.  Note that you need to have write permission into
the directories into which things will be installed.

If you are interested in building Expat to provide document
information in UTF-16 encoding rather than the default UTF-8, follow
these instructions (after having run `make distclean`).
Please note that we configure with `--without-xmlwf` as xmlwf does not
support this mode of compilation (yet):

1. Mass-patch `Makefile.am` files to use `libexpatw.la` for a library name:
   <br/>
   `find . -name Makefile.am -exec sed
       -e 's,libexpat\.la,libexpatw.la,'
       -e 's,libexpat_la,libexpatw_la,'
       -i.bak {} +`

1. Run `automake` to re-write `Makefile.in` files:<br/>
   `automake`

1. For UTF-16 output as unsigned short (and version/error strings as char),
   run:<br/>
   `./configure CPPFLAGS=-DXML_UNICODE --without-xmlwf`<br/>
   For UTF-16 output as `wchar_t` (incl. version/error strings), run:<br/>
   `./configure CFLAGS="-g -O2 -fshort-wchar" CPPFLAGS=-DXML_UNICODE_WCHAR_T
       --without-xmlwf`
   <br/>Note: The latter requires libc compiled with `-fshort-wchar`, as well.

1. Run `make` (which excludes xmlwf).

1. Run `make install` (again, excludes xmlwf).

Using `DESTDIR` is supported.  It works as follows:

```console
make install DESTDIR=/path/to/image
```

overrides the in-makefile set `DESTDIR`, because variable-setting priority is

1. commandline
1. in-makefile
1. environment

Note: This only applies to the Expat library itself, building UTF-16 versions
of xmlwf and the tests is currently not supported.

When using Expat with a project using autoconf for configuration, you
can use the probing macro in `conftools/expat.m4` to determine how to
include Expat.  See the comments at the top of that file for more
information.

A reference manual is available in the file `doc/reference.html` in this
distribution.


### b) Building with CMake

The CMake build system is still *experimental* and may replace the primary
build system based on GNU Autotools at some point when it is ready.


#### Available Options

For an idea of the available (non-advanced) options for building with CMake:

```console
# rm -f CMakeCache.txt ; cmake -D_EXPAT_HELP=ON -LH . | grep -B1 ':.*=' | sed 's,^--$,,'
// Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel ...
CMAKE_BUILD_TYPE:STRING=

// Install path prefix, prepended onto install directories.
CMAKE_INSTALL_PREFIX:PATH=/usr/local

// Path to a program.
DOCBOOK_TO_MAN:FILEPATH=/usr/bin/docbook2x-man

// Build man page for xmlwf
EXPAT_BUILD_DOCS:BOOL=ON

// Build the examples for expat library
EXPAT_BUILD_EXAMPLES:BOOL=ON

// Build fuzzers for the expat library
EXPAT_BUILD_FUZZERS:BOOL=OFF

// Build pkg-config file
EXPAT_BUILD_PKGCONFIG:BOOL=ON

// Build the tests for expat library
EXPAT_BUILD_TESTS:BOOL=ON

// Build the xmlwf tool for expat library
EXPAT_BUILD_TOOLS:BOOL=ON

// Character type to use (char|ushort|wchar_t) [default=char]
EXPAT_CHAR_TYPE:STRING=char

// Install expat files in cmake install target
EXPAT_ENABLE_INSTALL:BOOL=ON

// Use /MT flag (static CRT) when compiling in MSVC
EXPAT_MSVC_STATIC_CRT:BOOL=OFF

// Build fuzzers via OSS-Fuzz for the expat library
EXPAT_OSSFUZZ_BUILD:BOOL=OFF

// Build a shared expat library
EXPAT_SHARED_LIBS:BOOL=ON

// Treat all compiler warnings as errors
EXPAT_WARNINGS_AS_ERRORS:BOOL=OFF

// Make use of getrandom function (ON|OFF|AUTO) [default=AUTO]
EXPAT_WITH_GETRANDOM:STRING=AUTO

// Utilize libbsd (for arc4random_buf)
EXPAT_WITH_LIBBSD:BOOL=OFF

// Make use of syscall SYS_getrandom (ON|OFF|AUTO) [default=AUTO]
EXPAT_WITH_SYS_GETRANDOM:STRING=AUTO
```
