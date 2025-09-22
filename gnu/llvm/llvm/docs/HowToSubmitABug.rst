================================
How to submit an LLVM bug report
================================

Introduction - Got bugs?
========================


If you're working with LLVM and run into a bug, we definitely want to know
about it.  This document describes what you can do to increase the odds of
getting it fixed quickly.

🔒 If you believe that the bug is security related, please follow :ref:`report-security-issue`. 🔒

Basically you have to do two things at a minimum. First, decide whether the
bug `crashes the compiler`_ or if the compiler is `miscompiling`_ the program
(i.e., the compiler successfully produces an executable, but it doesn't run
right). Based on what type of bug it is, follow the instructions in the
linked section to narrow down the bug so that the person who fixes it will be
able to find the problem more easily.

Once you have a reduced test-case, go to `the LLVM Bug Tracking System
<https://github.com/llvm/llvm-project/issues>`_ and fill out the form with the
necessary details (note that you don't need to pick a label, just use if you're
not sure).  The bug description should contain the following information:

* All information necessary to reproduce the problem.
* The reduced test-case that triggers the bug.
* The location where you obtained LLVM (if not from our Git
  repository).

Thanks for helping us make LLVM better!

.. _crashes the compiler:

Crashing Bugs
=============

More often than not, bugs in the compiler cause it to crash---often due to
an assertion failure of some sort. The most important piece of the puzzle
is to figure out if it is crashing in the Clang front-end or if it is one of
the LLVM libraries (e.g. the optimizer or code generator) that has
problems.

To figure out which component is crashing (the front-end, middle-end
optimizer, or backend code generator), run the ``clang`` command line as you
were when the crash occurred, but with the following extra command line
options:

* ``-emit-llvm -Xclang -disable-llvm-passes``: If ``clang`` still crashes when
  passed these options (which disable the optimizer and code generator), then
  the crash is in the front-end. Jump ahead to :ref:`front-end bugs
  <frontend-crash>`.

* ``-emit-llvm``: If ``clang`` crashes with this option (which disables
  the code generator), you found a middle-end optimizer bug. Jump ahead to
  :ref:`middle-end bugs <middleend-crash>`.

* Otherwise, you have a backend code generator crash. Jump ahead to :ref:`code
  generator bugs <backend-crash>`.

.. _frontend-crash:

Front-end bugs
--------------

On a ``clang`` crash, the compiler will dump a preprocessed file and a script
to replay the ``clang`` command. For example, you should see something like

.. code-block:: text

   PLEASE ATTACH THE FOLLOWING FILES TO THE BUG REPORT:
   Preprocessed source(s) and associated run script(s) are located at:
   clang: note: diagnostic msg: /tmp/foo-xxxxxx.c
   clang: note: diagnostic msg: /tmp/foo-xxxxxx.sh

The `creduce <https://github.com/csmith-project/creduce>`_ tool helps to
reduce the preprocessed file down to the smallest amount of code that still
replicates the problem. You're encouraged to use creduce to reduce the code
to make the developers' lives easier. The
``clang/utils/creduce-clang-crash.py`` script can be used on the files
that clang dumps to help with automating creating a test to check for the
compiler crash.

`cvise <https://github.com/marxin/cvise>`_ is an alternative to ``creduce``.

.. _middleend-crash:

Middle-end optimization bugs
----------------------------

If you find that a bug crashes in the optimizer, compile your test-case to a
``.bc`` file by passing "``-emit-llvm -O1 -Xclang -disable-llvm-passes -c -o
foo.bc``". The ``-O1`` is important because ``-O0`` adds the ``optnone``
function attribute to all functions and many passes don't run on ``optnone``
functions. Then run:

.. code-block:: bash

   opt -O3 foo.bc -disable-output

If this doesn't crash, please follow the instructions for a :ref:`front-end
bug <frontend-crash>`.

If this does crash, then you should be able to debug this with the following
:doc:`bugpoint <Bugpoint>` command:

.. code-block:: bash

   bugpoint foo.bc -O3

Run this, then file a bug with the instructions and reduced .bc
files that bugpoint emits.

If bugpoint doesn't reproduce the crash, ``llvm-reduce`` is an alternative
way to reduce LLVM IR. Create a script that repros the crash and run:

.. code-block:: bash

   llvm-reduce --test=path/to/script foo.bc

which should produce reduced IR that reproduces the crash. Be warned the
``llvm-reduce`` is still fairly immature and may crash.

If none of the above work, you can get the IR before a crash by running the
``opt`` command with the ``--print-before-all --print-module-scope`` flags to
dump the IR before every pass. Be warned that this is very verbose.

.. _backend-crash:

Backend code generator bugs
---------------------------

If you find a bug that crashes clang in the code generator, compile your
source file to a .bc file by passing "``-emit-llvm -c -o foo.bc``" to
clang (in addition to the options you already pass).  Once your have
foo.bc, one of the following commands should fail:

#. ``llc foo.bc``
#. ``llc foo.bc -relocation-model=pic``
#. ``llc foo.bc -relocation-model=static``

If none of these crash, please follow the instructions for a :ref:`front-end
bug<frontend-crash>`. If one of these do crash, you should be able to reduce
this with one of the following :doc:`bugpoint <Bugpoint>` command lines (use
the one corresponding to the command above that failed):

#. ``bugpoint -run-llc foo.bc``
#. ``bugpoint -run-llc foo.bc --tool-args -relocation-model=pic``
#. ``bugpoint -run-llc foo.bc --tool-args -relocation-model=static``

Please run this, then file a bug with the instructions and reduced .bc file
that bugpoint emits.  If something goes wrong with bugpoint, please submit
the "foo.bc" file and the option that llc crashes with.

LTO bugs
---------------------------

If you encounter a bug that leads to crashes in the LLVM LTO phase when using
the ``-flto`` option, follow these steps to diagnose and report the issue:

Compile your source file to a ``.bc`` (Bitcode) file with the following options,
in addition to your existing compilation options:

.. code-block:: bash

   export CFLAGS="-flto -fuse-ld=lld" CXXFLAGS="-flto -fuse-ld=lld" LDFLAGS="-Wl,-plugin-opt=save-temps"

These options enable LTO and save temporary files generated during compilation
for later analysis.

On Windows, you should be using lld-link as the linker. Adjust your compilation 
flags as follows:
* Add ``/lldsavetemps`` to the linker flags.
* When linking from the compiler driver, add ``/link /lldsavetemps`` in order to forward that flag to the linker.

Using the specified flags will generate four intermediate bytecode files:

#. a.out.0.0.preopt.bc (Before any link-time optimizations (LTO) are applied)
#. a.out.0.2.internalize.bc (After initial optimizations are applied)
#. a.out.0.4.opt.bc (After an extensive set of optimizations)
#. a.out.0.5.precodegen.bc (After LTO but before translating into machine code)

Execute one of the following commands to identify the source of the problem:

#. ``opt "-passes=lto<O3>" a.out.0.2.internalize.bc``
#. ``llc a.out.0.5.precodegen.bc``

If one of these do crash, you should be able to reduce
this with :program:`llvm-reduce`
command line (use the bc file corresponding to the command above that failed):

.. code-block:: bash

   llvm-reduce --test reduce.sh a.out.0.2.internalize.bc

Example of reduce.sh script

.. code-block:: bash

   $ cat reduce.sh
   #!/bin/bash -e

   path/to/not --crash path/to/opt "-passes=lto<O3>" $1 -o temp.bc  2> err.log
   grep -q "It->second == &Insn" err.log

Here we have grepped the failed assert message.

Please run this, then file a bug with the instructions and reduced .bc file
that llvm-reduce emits.

.. _miscompiling:

Miscompilations
===============

If clang successfully produces an executable, but that executable doesn't run
right, this is either a bug in the code or a bug in the compiler. The first
thing to check is to make sure it is not using undefined behavior (e.g.
reading a variable before it is defined). In particular, check to see if the
program is clean under various `sanitizers
<https://github.com/google/sanitizers>`_ (e.g. ``clang
-fsanitize=undefined,address``) and `valgrind <http://valgrind.org/>`_. Many
"LLVM bugs" that we have chased down ended up being bugs in the program being
compiled, not LLVM.

Once you determine that the program itself is not buggy, you should choose
which code generator you wish to compile the program with (e.g. LLC or the JIT)
and optionally a series of LLVM passes to run.  For example:

.. code-block:: bash

   bugpoint -run-llc [... optzn passes ...] file-to-test.bc --args -- [program arguments]

bugpoint will try to narrow down your list of passes to the one pass that
causes an error, and simplify the bitcode file as much as it can to assist
you. It will print a message letting you know how to reproduce the
resulting error.

The :doc:`OptBisect <OptBisect>` page shows an alternative method for finding
incorrect optimization passes.

Incorrect code generation
=========================

Similarly to debugging incorrect compilation by mis-behaving passes, you
can debug incorrect code generation by either LLC or the JIT, using
``bugpoint``. The process ``bugpoint`` follows in this case is to try to
narrow the code down to a function that is miscompiled by one or the other
method, but since for correctness, the entire program must be run,
``bugpoint`` will compile the code it deems to not be affected with the C
Backend, and then link in the shared object it generates.

To debug the JIT:

.. code-block:: bash

   bugpoint -run-jit -output=[correct output file] [bitcode file]  \
            --tool-args -- [arguments to pass to lli]              \
            --args -- [program arguments]

Similarly, to debug the LLC, one would run:

.. code-block:: bash

   bugpoint -run-llc -output=[correct output file] [bitcode file]  \
            --tool-args -- [arguments to pass to llc]              \
            --args -- [program arguments]

**Special note:** if you are debugging MultiSource or SPEC tests that
already exist in the ``llvm/test`` hierarchy, there is an easier way to
debug the JIT, LLC, and CBE, using the pre-written Makefile targets, which
will pass the program options specified in the Makefiles:

.. code-block:: bash

   cd llvm/test/../../program
   make bugpoint-jit

At the end of a successful ``bugpoint`` run, you will be presented
with two bitcode files: a *safe* file which can be compiled with the C
backend and the *test* file which either LLC or the JIT
mis-codegenerates, and thus causes the error.

To reproduce the error that ``bugpoint`` found, it is sufficient to do
the following:

#. Regenerate the shared object from the safe bitcode file:

   .. code-block:: bash

      llc -march=c safe.bc -o safe.c
      gcc -shared safe.c -o safe.so

#. If debugging LLC, compile test bitcode native and link with the shared
   object:

   .. code-block:: bash

      llc test.bc -o test.s
      gcc test.s safe.so -o test.llc
      ./test.llc [program options]

#. If debugging the JIT, load the shared object and supply the test
   bitcode:

   .. code-block:: bash

      lli -load=safe.so test.bc [program options]
