llvm-tli-checker - TargetLibraryInfo vs library checker
=======================================================

.. program:: llvm-tli-checker

SYNOPSIS
--------

:program:`llvm-tli-checker` [*options*] [*library-file...*]

DESCRIPTION
-----------

:program:`llvm-tli-checker` compares TargetLibraryInfo's opinion of the
availability of library functions against the set of functions exported
by the specified library files, reporting any disagreements between TLI's
opinion and whether the function is actually present. This is primarily
useful for vendors to ensure the TLI for their target is correct, and
the compiler will not "optimize" some code sequence into a library call
that is not actually available.

EXAMPLE
-------

.. code-block:: console

  $ llvm-tli-checker --triple x86_64-scei-ps4 example.so
  TLI knows 466 symbols, 235 available for 'x86_64-scei-ps4'

  Looking for symbols in 'example.so'
  Found 235 global function symbols in 'example.so'
  Found a grand total of 235 library symbols
  << TLI yes SDK no:  '_ZdaPv' aka operator delete[](void*)
  >> TLI no  SDK yes: '_ZdaPvj' aka operator delete[](void*, unsigned int)
  << Total TLI yes SDK no:  1
  >> Total TLI no  SDK yes: 1
  == Total TLI yes SDK yes: 234
  FAIL: LLVM TLI doesn't match SDK libraries.

OPTIONS
-------

.. option:: --dump-tli

  Print "available"/"not available" for each library function, according to
  TargetLibraryInfo's information for the specified triple, and exit. This
  option does not read any input files.

.. option:: --help, -h

  Print a summary of command line options and exit.

.. option:: --libdir=<directory>

  A base directory to prepend to each library file path. This is handy
  when there are a number of library files all in the same directory, or
  a list of input filenames are kept in a response file.

.. option:: --report=<level>

  The amount of information to report.  <level> can be summary, discrepancy,
  or full. A summary report gives only the count of matching and mis-matching
  symbols; discrepancy lists the mis-matching symbols; and full lists all
  symbols known to TLI, matching or mis-matching. The default is discrepancy.

.. option:: --separate

  Read and report a summary for each library file separately.  This can be
  useful to identify library files that don't contribute anything that TLI
  knows about. Implies --report=summary (can be overridden).

.. option:: --triple=<triple>

  The triple to use for initializing TargetLibraryInfo.

.. option:: @<FILE>

 Read command-line options and/or library names from response file `<FILE>`.

EXIT STATUS
-----------

:program:`llvm-tli-checker` returns 0 even if there are mismatches. It returns a
non-zero exit code if there is an unrecognized option, or no input files are
provided.
