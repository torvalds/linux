llvm-cxxfilt - LLVM symbol name demangler
=========================================

.. program:: llvm-cxxfilt

SYNOPSIS
--------

:program:`llvm-cxxfilt` [*options*] [*mangled names...*]

DESCRIPTION
-----------

:program:`llvm-cxxfilt` is a symbol demangler that can be used as a replacement
for the GNU :program:`c++filt` tool. It takes a series of symbol names and
prints their demangled form on the standard output stream. If a name cannot be
demangled, it is simply printed as is.

If no names are specified on the command-line, names are read interactively from
the standard input stream. When reading names from standard input, each input
line is split on characters that are not part of valid Itanium name manglings,
i.e. characters that are not alphanumeric, '.', '$', or '_'. Separators between
names are copied to the output as is.

EXAMPLE
-------

.. code-block:: console

  $ llvm-cxxfilt _Z3foov _Z3bari not_mangled
  foo()
  bar(int)
  not_mangled
  $ cat input.txt
  | _Z3foov *** _Z3bari *** not_mangled |
  $ llvm-cxxfilt < input.txt
  | foo() *** bar(int) *** not_mangled |

OPTIONS
-------

.. option:: --format=<value>, -s

  Mangling scheme to assume. Valid values are ``auto`` (default, auto-detect the
  style) and ``gnu`` (assume GNU/Itanium style).

.. option:: --help, -h

  Print a summary of command line options.

.. option:: --no-params, -p

  Do not demangle function parameters or return types.

.. option:: --no-strip-underscore, -n

  Do not strip a leading underscore. This is the default for all platforms
  except Mach-O based hosts.

.. option:: --strip-underscore, -_

  Strip a single leading underscore, if present, from each input name before
  demangling. On by default on Mach-O based platforms.

.. option:: --types, -t

  Attempt to demangle names as type names as well as function names.

.. option:: --version

  Display the version of the :program:`llvm-cxxfilt` executable.

.. option:: @<FILE>

 Read command-line options from response file `<FILE>`.

EXIT STATUS
-----------

:program:`llvm-cxxfilt` returns 0 unless it encounters a usage error, in which
case a non-zero exit code is returned.

SEE ALSO
--------

:manpage:`llvm-nm(1)`
