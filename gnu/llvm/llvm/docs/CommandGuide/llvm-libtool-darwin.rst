llvm-libtool-darwin - LLVM tool for creating libraries for Darwin
=================================================================

.. program:: llvm-libtool-darwin

SYNOPSIS
--------

:program:`llvm-libtool-darwin` [*options*] *<input files>*

DESCRIPTION
-----------

:program:`llvm-libtool-darwin` is a tool for creating static and dynamic
libraries for Darwin.

For most scenarios, it works as a drop-in replacement for cctools'
:program:`libtool`.

OPTIONS
--------
:program:`llvm-libtool-darwin` supports the following options:

.. option:: -arch_only <architecture>

  Build a static library only for the specified `<architecture>` and ignore all
  other architectures in the files.

.. option:: -D

  Use zero for timestamps and UIDs/GIDs. This is set by default.

.. option:: -filelist <listfile[,dirname]>

  Read input file names from `<listfile>`. File names are specified in `<listfile>`
  one per line, separated only by newlines. Whitespace on a line is assumed
  to be part of the filename. If the directory name, `dirname`, is also
  specified then it is prepended to each file name in the `<listfile>`.

.. option:: -h, -help

  Show help and usage for this command.

.. option:: -l <x>

  Searches for the library libx.a in the library search path. If the string `<x>`
  ends with '.o', then the library 'x' is searched for without prepending 'lib'
  or appending '.a'. If the library is found, it is added to the list of input
  files. Otherwise, an error is raised.

.. option:: -L <dir>

  Adds `<dir>` to the list of directories in which to search for libraries. The
  directories are searched in the order in which they are specified with
  :option:`-L` and before the default search path. The default search path
  includes directories `/lib`, `/usr/lib` and `/usr/local/lib`.

.. option:: -no_warning_for_no_symbols

   Do not warn about files that have no symbols.

.. option:: -warnings_as_errors

  Produce a non-zero exit status if any warnings are emitted.

.. option:: -o <filename>

  Specify the output file name. Must be specified exactly once.

.. option:: -static

  Produces a static library from the input files.

.. option:: -U

  Use actual timestamps and UIDs/GIDs.

.. option:: -V

  Display the version of this program and perform any operation specified.

.. option:: -version

  Display the version of this program and exit immediately.

EXIT STATUS
-----------

:program:`llvm-libtool-darwin` exits with a non-zero exit code if there is an error.
Otherwise, it exits with code 0.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/issues/>.

SEE ALSO
--------

:manpage:`llvm-ar(1)`
