llvm-link - LLVM bitcode linker
===============================

.. program:: llvm-link

SYNOPSIS
--------

:program:`llvm-link` [*options*] *filename ...*

DESCRIPTION
-----------

:program:`llvm-link` takes several LLVM bitcode files and links them together
into a single LLVM bitcode file.  It writes the output file to standard output,
unless the :option:`-o` option is used to specify a filename.

OPTIONS
-------

.. option:: -f

 Enable binary output on terminals.  Normally, :program:`llvm-link` will refuse
 to write raw bitcode output if the output stream is a terminal. With this
 option, :program:`llvm-link` will write raw bitcode regardless of the output
 device.

.. option:: -o filename

 Specify the output file name.  If ``filename`` is "``-``", then
 :program:`llvm-link` will write its output to standard output.

.. option:: -S

 Write output in LLVM intermediate language (instead of bitcode).

.. option:: -d

 If specified, :program:`llvm-link` prints a human-readable version of the
 output bitcode file to standard error.

.. option:: --help

 Print a summary of command line options.

.. option:: -v

 Verbose mode.  Print information about what :program:`llvm-link` is doing.
 This typically includes a message for each bitcode file linked in and for each
 library found.

.. option:: --override <filename>

  Adds the passed-in file to the link and overrides symbols that have already
  been declared with the definitions in the file that is passed in. This flag
  can be specified multiple times to have multiple files act as overrides. If
  a symbol is declared more than twice, the definition from the file declared
  last takes precedence.

.. option:: --import <function:filename>

  Specify a function that should be imported from the specified file for
  linking with ThinLTO. This option can be specified multiple times to import
  multiple functions.

.. option:: --summary-index <filename>

  Specify the path to a file containing the module summary index with the
  results of an earlier ThinLTO link. This option is required when 
  `--import` is used.

.. option:: --internalize

  Internalize the linked symbols.

.. option:: --disable-debug-info-type-map

  Disables the use of a uniquing type map for debug info.

.. option:: --only-needed

  Link only needed symbols.

.. option:: --disable-lazy-loading

  Disable lazy module loading.

.. option:: --suppress-warnings

  Suppress all linker warnings.

.. option:: --preserve-bc-uselistorder
  
  Preserve the use-list order when writing LLVM bitcode.

.. option:: --preserve-ll-uselistorder

  Preserve the use-list order when writing LLVM assembly.

.. option:: --ignore-non-bitcode

  Do not error out when a non-bitcode file is encountered while processing
  an archive.

EXIT STATUS
-----------

If :program:`llvm-link` succeeds, it will exit with 0.  Otherwise, if an error
occurs, it will exit with a non-zero value.
