llvm-size - print size information
==================================

.. program:: llvm-size

SYNOPSIS
--------

:program:`llvm-size` [*options*] [*input...*]

DESCRIPTION
-----------

:program:`llvm-size` is a tool that prints size information for binary files.
It is intended to be a drop-in replacement for GNU's :program:`size`.

The tool prints size information for each ``input`` specified. If no input is
specified, the program prints size information for ``a.out``. If "``-``" is
specified as an input file, :program:`llvm-size` reads a file from the standard
input stream. If an input is an archive, size information will be displayed for
all its members.

OPTIONS
-------

.. option:: -A

 Equivalent to :option:`--format` with a value of ``sysv``.

.. option:: --arch=<arch>

 Architecture(s) from Mach-O universal binaries to display information for.

.. option:: -B

 Equivalent to :option:`--format` with a value of ``berkeley``.

.. option:: --common

 Include ELF common symbol sizes in bss size for ``berkeley`` output format, or
 as a separate section entry for ``sysv`` output. If not specified, these
 symbols are ignored.

.. option:: -d

 Equivalent to :option:`--radix` with a value of ``10``.

.. option:: -l

 Display verbose address and offset information for segments and sections in
 Mach-O files in ``darwin`` format.

.. option:: --format=<format>

 Set the output format to the ``<format>`` specified. Available ``<format>``
 options are ``berkeley`` (the default), ``sysv`` and ``darwin``.

 Berkeley output summarises text, data and bss sizes in each file, as shown
 below for a typical pair of ELF files:

 .. code-block:: console

  $ llvm-size --format=berkeley test.o test2.o
     text    data     bss     dec     hex filename
      182      16       5     203      cb test.elf
       82       8       1      91      5b test2.o

 For Mach-O files, the output format is slightly different:

 .. code-block:: console

  $ llvm-size --format=berkeley macho.obj macho2.obj
  __TEXT  __DATA  __OBJC  others  dec     hex
  4       8       0       0       12      c       macho.obj
  16      32      0       0       48      30      macho2.obj

 Sysv output displays size and address information for most sections, with each
 file being listed separately:

 .. code-block:: console

  $ llvm-size --format=sysv test.elf test2.o
     test.elf  :
     section       size      addr
     .eh_frame       92   2097496
     .text           90   2101248
     .data           16   2105344
     .bss             5   2105360
     .comment       209         0
     Total          412

     test2.o  :
     section             size   addr
     .text                 26      0
     .data                  8      0
     .bss                   1      0
     .comment             106      0
     .note.GNU-stack        0      0
     .eh_frame             56      0
     .llvm_addrsig          2      0
     Total                199

 ``darwin`` format only affects Mach-O input files. If an input of a different
 file format is specified, :program:`llvm-size` falls back to ``berkeley``
 format. When producing ``darwin`` format, the tool displays information about
 segments and sections:

 .. code-block:: console

  $ llvm-size --format=darwin macho.obj macho2.obj
     macho.obj:
     Segment : 12
             Section (__TEXT, __text): 4
             Section (__DATA, __data): 8
             total 12
     total 12
     macho2.obj:
     Segment : 48
             Section (__TEXT, __text): 16
             Section (__DATA, __data): 32
             total 48
     total 48

.. option:: --help, -h

 Display a summary of command line options.

.. option:: -m

 Equivalent to :option:`--format` with a value of ``darwin``.

.. option:: -o

 Equivalent to :option:`--radix` with a value of ``8``.

.. option:: --radix=<value>

 Display size information in the specified radix. Permitted values are ``8``,
 ``10`` (the default) and ``16`` for octal, decimal and hexadecimal output
 respectively.

 Example:

 .. code-block:: console

  $ llvm-size --radix=8 test.o
     text    data     bss     oct     hex filename
     0152      04      04     162      72 test.o

  $ llvm-size --radix=10 test.o
     text    data     bss     dec     hex filename
      106       4       4     114      72 test.o

  $ llvm-size --radix=16 test.o
     text    data     bss     dec     hex filename
     0x6a     0x4     0x4     114      72 test.o

.. option:: --totals, -t

 Applies only to ``berkeley`` output format. Display the totals for all listed
 fields, in addition to the individual file listings.

 Example:

 .. code-block:: console

  $ llvm-size --totals test.elf test2.o
     text    data     bss     dec     hex filename
      182      16       5     203      cb test.elf
       82       8       1      91      5b test2.o
      264      24       6     294     126 (TOTALS)

.. option:: --version

 Display the version of the :program:`llvm-size` executable.

.. option:: -x

 Equivalent to :option:`--radix` with a value of ``16``.

.. option:: @<FILE>

 Read command-line options from response file ``<FILE>``.

EXIT STATUS
-----------

:program:`llvm-size` exits with a non-zero exit code if there is an error.
Otherwise, it exits with code 0.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/labels/tools:llvm-size/>.
