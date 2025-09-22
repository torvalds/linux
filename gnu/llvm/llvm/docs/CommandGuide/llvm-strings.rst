llvm-strings - print strings
============================

.. program:: llvm-strings

SYNOPSIS
--------

:program:`llvm-strings` [*options*] [*input...*]

DESCRIPTION
-----------

:program:`llvm-strings` is a tool intended as a drop-in replacement for GNU's
:program:`strings`, which looks for printable strings in files and writes them
to the standard output stream. A printable string is any sequence of four (by
default) or more printable ASCII characters. The end of the file, or any other
byte, terminates the current sequence.

:program:`llvm-strings` looks for strings in each ``input`` file specified.
Unlike GNU :program:`strings` it looks in the entire input file, regardless of
file format, rather than restricting the search to certain sections of object
files. If "``-``" is specified as an ``input``, or no ``input`` is specified,
the program reads from the standard input stream.

EXAMPLE
-------

.. code-block:: console

 $ cat input.txt
 bars
 foo
 wibble blob
 $ llvm-strings input.txt
 bars
 wibble blob

OPTIONS
-------

.. option:: --all, -a

 Silently ignored. Present for GNU :program:`strings` compatibility.

.. option:: --bytes=<length>, -n

 Set the minimum number of printable ASCII characters required for a sequence of
 bytes to be considered a string. The default value is 4.

.. option:: --help, -h

 Display a summary of command line options.

.. option:: --print-file-name, -f

 Display the name of the containing file before each string.

 Example:

 .. code-block:: console

  $ llvm-strings --print-file-name test.o test.elf
  test.o: _Z5hellov
  test.o: some_bss
  test.o: test.cpp
  test.o: main
  test.elf: test.cpp
  test.elf: test2.cpp
  test.elf: _Z5hellov
  test.elf: main
  test.elf: some_bss

.. option:: --radix=<radix>, -t

 Display the offset within the file of each string, before the string and using
 the specified radix. Valid ``<radix>`` values are ``o``, ``d`` and ``x`` for
 octal, decimal and hexadecimal respectively.

 Example:

 .. code-block:: console

  $ llvm-strings --radix=o test.o
      1054 _Z5hellov
      1066 .rela.text
      1101 .comment
      1112 some_bss
      1123 .bss
      1130 test.cpp
      1141 main
  $ llvm-strings --radix=d test.o
      556 _Z5hellov
      566 .rela.text
      577 .comment
      586 some_bss
      595 .bss
      600 test.cpp
      609 main
  $ llvm-strings -t x test.o
      22c _Z5hellov
      236 .rela.text
      241 .comment
      24a some_bss
      253 .bss
      258 test.cpp
      261 main

.. option:: --version

 Display the version of the :program:`llvm-strings` executable.

.. option:: @<FILE>

 Read command-line options from response file ``<FILE>``.

EXIT STATUS
-----------

:program:`llvm-strings` exits with a non-zero exit code if there is an error.
Otherwise, it exits with code 0.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/labels/tools:llvm-strings/>.
