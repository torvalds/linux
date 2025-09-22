llvm-dis - LLVM disassembler
============================

.. program:: llvm-dis

SYNOPSIS
--------

**llvm-dis** [*options*] [*filename*]

DESCRIPTION
-----------

The **llvm-dis** command is the LLVM disassembler.  It takes an LLVM
bitcode file and converts it into human-readable LLVM assembly language.

If filename is omitted or specified as ``-``, **llvm-dis** reads its
input from standard input.

If the input is being read from standard input, then **llvm-dis**
will send its output to standard output by default.  Otherwise, the
output will be written to a file named after the input file, with
a ``.ll`` suffix added (any existing ``.bc`` suffix will first be
removed).  You can override the choice of output file using the
**-o** option.

OPTIONS
-------

**-f**

 Enable binary output on terminals.  Normally, **llvm-dis** will refuse to
 write raw bitcode output if the output stream is a terminal. With this option,
 **llvm-dis** will write raw bitcode regardless of the output device.

**-help**

 Print a summary of command line options.

**-o** *filename*

 Specify the output file name.  If *filename* is -, then the output is sent
 to standard output.

EXIT STATUS
-----------

If **llvm-dis** succeeds, it will exit with 0.  Otherwise, if an error
occurs, it will exit with a non-zero value.

SEE ALSO
--------

:manpage:`llvm-as(1)`
