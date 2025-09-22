llvm-reduce - LLVM automatic testcase reducer.
==============================================

.. program:: llvm-reduce

SYNOPSIS
--------

:program:`llvm-reduce` [*options*] [*input...*]

DESCRIPTION
-----------

The :program:`llvm-reduce` tool project that can be used for reducing the size of LLVM test cases.
It works by removing redundant or unnecessary code from LLVM test cases while still preserving 
their ability to detect bugs.

If ``input`` is "``-``", :program:`llvm-reduce` reads from standard
input. Otherwise, it will read from the specified ``filenames``.

LLVM-Reduce is a useful tool for reducing the size and 
complexity of LLVM test cases, making it easier to identify and debug issues in 
the LLVM compiler infrastructure.

GENERIC OPTIONS
---------------


.. option:: --help

 Display available options (--help-hidden for more).

.. option:: --abort-on-invalid-reduction

 Abort if any reduction results in invalid IR

.. option::--delta-passes=<string>  

 Delta passes to run, separated by commas. By default, run all delta passes.


.. option:: --in-place     

 WARNING: This option will replace your input file with the reduced version!

.. option:: --ir-passes=<string> 

 A textual description of the pass pipeline, same as what's passed to `opt -passes`.

.. option:: -j <uint>  

 Maximum number of threads to use to process chunks. Set to 1 to disable parallelism.

.. option::  --max-pass-iterations=<int>

  Maximum number of times to run the full set of delta passes (default=5).

.. option:: --mtriple=<string> 

 Set the target triple.

.. option:: --preserve-debug-environment

 Don't disable features used for crash debugging (crash reports, llvm-symbolizer and core dumps)

.. option:: --print-delta-passes  

 Print list of delta passes, passable to --delta-passes as a comma separated liste.

.. option:: --skip-delta-passes=<string>     

 Delta passes to not run, separated by commas. By default, run all delta passes.

.. option:: --starting-granularity-level=<uint>

  Number of times to divide chunks prior to first test.

  Note : Granularity refers to the level of detail at which the reduction process operates.
  A lower granularity means that the reduction process operates at a more coarse-grained level,
  while a higher granularity means that it operates at a more fine-grained level.

.. option::  --test=<string> 

 Name of the interesting-ness test to be run.

.. option:: --test-arg=<string> 

 Arguments passed onto the interesting-ness test.

.. option:: --verbose    

 Print extra debugging information.
 
.. option::  --write-tmp-files-as-bitcode  

 Always write temporary files as bitcode instead of textual IR.

.. option:: -x={ir|mir}

 Input language as ir or mir.

EXIT STATUS
------------

:program:`llvm-reduce` returns 0 under normal operation. It returns a non-zero
exit code if there were any errors.

EXAMPLE
-------

:program:`llvm-reduce` can be used to simplify a test that causes a
compiler crash.

For example, let's assume that `opt` is crashing on the IR file
`test.ll` with error message `Assertion failed at line 1234 of
WhateverFile.cpp`, when running at `-O2`.

The test case of `test.ll` can be reduced by invoking the following
command:

.. code-block:: bash

   $(LLVM_BUILD_FOLDER)/bin/llvm-reduce --test=script.sh <path to>/test.ll

The shell script passed to the option `test` consists of the
following:

.. code-block:: bash

   $(LLVM_BUILD_FOLDER)/bin/opt -O2 -disable-output $1 \
     |& grep "Assertion failed at line 1234 of WhateverFile.cpp"

(In this script, `grep` exits with 0 if it finds the string and that
becomes the whole script's status.)

This example can be generalized to other tools that process IR files,
for example `llc`.
