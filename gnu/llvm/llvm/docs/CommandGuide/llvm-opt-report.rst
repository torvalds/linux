llvm-opt-report - generate optimization report from YAML
========================================================

.. program:: llvm-opt-report

SYNOPSIS
--------

:program:`llvm-opt-report` [*options*] [input]

DESCRIPTION
-----------

:program:`llvm-opt-report` is a tool to generate an optimization report from YAML optimization record files.

You need to create an input YAML optimization record file before running :program:`llvm-opt-report`.

It provides information on the execution time, memory usage, and other details of each optimization pass.


.. code-block:: console

 $ clang -c foo.c -o foo.o -O3 -fsave-optimization-record

Then, you create a report using the :program:`llvm-opt-report` command with the YAML optimization record file :file:`foo.opt.yaml` as input.

.. code-block:: console

 $ llvm-opt-report foo.opt.yaml -o foo.lst

foo.lst is the generated optimization report.

.. code-block::

 < foo.c
  1          | void bar();
  2          | void foo() { bar(); }
  3          |
  4          | void Test(int *res, int *c, int *d, int *p, int n) {
  5          |   int i;
  6          |
  7          | #pragma clang loop vectorize(assume_safety)
  8     V4,1 |   for (i = 0; i < 1600; i++) {
  9          |     res[i] = (p[i] == 0) ? res[i] : res[i] + d[i];
 10          |   }
 11          |
 12  U16     |   for (i = 0; i < 16; i++) {
 13          |     res[i] = (p[i] == 0) ? res[i] : res[i] + d[i];
 14          |   }
 15          |
 16 I        |   foo();
 17          |
 18          |   foo(); bar(); foo();
    I        |   ^
    I        |                 ^
 19          | }
 20          |

Symbols printed on the left side of the program indicate what kind of optimization was performed.
The meanings of the symbols are as follows:

- I: The function is inlined.
- U: The loop is unrolled. The following number indicates the unroll factor.
- V: The loop is vectorized. The following numbers indicate the vector length and the interleave factor.

.. note:: 

    If a specific line of code is output twice, it means that the same optimization pass was applied to that 
    line of code twice, and the pass was able to further optimize the code on the second iteration.


OPTIONS
-------

If ``input`` is "``-``" or omitted, :program:`llvm-opt-report` reads from standard
input. Otherwise, it will read from the specified filename.

If the :option:`-o` option is omitted, then :program:`llvm-opt-report` will send its output
to standard output.  If the :option:`-o` option specifies "``-``", then the output will also
be sent to standard output.


.. option:: --help

 Display available options.

.. option:: --version

 Display the version of this program.

.. option:: --format=<string>

 The format of the optimization record file.
 The Argument is one of the following:

 - yaml
 - yaml-strtab
 - bitstream

.. option:: --no-demangle

 Do not demangle function names.

.. option:: -o=<string>

 Output file.

.. option:: -r=<string>

 Root for relative input paths.

.. option:: -s

 Do not include vectorization factors, etc.

EXIT STATUS
-----------

:program:`llvm-opt-report` returns 0 on success. Otherwise, an error message is printed
to standard error, and the tool returns 1.

