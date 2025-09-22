llvm-as - LLVM assembler
========================

.. program:: llvm-as

SYNOPSIS
--------

**llvm-as** [*options*] [*filename*]

DESCRIPTION
-----------

**llvm-as** is the LLVM assembler.  It reads a file containing human-readable
LLVM assembly language, translates it to LLVM bitcode, and writes the result
into a file or to standard output.

If *filename* is omitted or is ``-``, then **llvm-as** reads its input from
standard input.

If an output file is not specified with the **-o** option, then
**llvm-as** sends its output to a file or standard output by following
these rules:

* If the input is standard input, then the output is standard output.

* If the input is a file that ends with ``.ll``, then the output file is of the
  same name, except that the suffix is changed to ``.bc``.

* If the input is a file that does not end with the ``.ll`` suffix, then the
  output file has the same name as the input file, except that the ``.bc``
  suffix is appended.

OPTIONS
-------

**-f**
 Enable binary output on terminals.  Normally, **llvm-as** will refuse to
 write raw bitcode output if the output stream is a terminal. With this option,
 **llvm-as** will write raw bitcode regardless of the output device.

**-help**
 Print a summary of command line options.

**-o** *filename*
 Specify the output file name.  If *filename* is ``-``, then **llvm-as**
 sends its output to standard output.

EXIT STATUS
-----------

If **llvm-as** succeeds, it will exit with 0.  Otherwise, if an error occurs, it
will exit with a non-zero value.

SEE ALSO
--------

:manpage:`llvm-dis(1)`, as(1)
