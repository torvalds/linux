====================================
LLVM bugpoint tool: design and usage
====================================

.. contents::
   :local:

Description
===========

``bugpoint`` narrows down the source of problems in LLVM tools and passes.  It
can be used to debug three types of failures: optimizer crashes, miscompilations
by optimizers, or bad native code generation (including problems in the static
and JIT compilers).  It aims to reduce large test cases to small, useful ones.
For example, if ``opt`` crashes while optimizing a file, it will identify the
optimization (or combination of optimizations) that causes the crash, and reduce
the file down to a small example which triggers the crash.

For detailed case scenarios, such as debugging ``opt``, or one of the LLVM code
generators, see :doc:`HowToSubmitABug`.

Design Philosophy
=================

``bugpoint`` is designed to be a useful tool without requiring any hooks into
the LLVM infrastructure at all.  It works with any and all LLVM passes and code
generators, and does not need to "know" how they work.  Because of this, it may
appear to do stupid things or miss obvious simplifications.  ``bugpoint`` is
also designed to trade off programmer time for computer time in the
compiler-debugging process; consequently, it may take a long period of
(unattended) time to reduce a test case, but we feel it is still worth it. Note
that ``bugpoint`` is generally very quick unless debugging a miscompilation
where each test of the program (which requires executing it) takes a long time.

Automatic Debugger Selection
----------------------------

``bugpoint`` reads each ``.bc`` or ``.ll`` file specified on the command line
and links them together into a single module, called the test program.  If any
LLVM passes are specified on the command line, it runs these passes on the test
program.  If any of the passes crash, or if they produce malformed output (which
causes the verifier to abort), ``bugpoint`` starts the `crash debugger`_.

Otherwise, if the ``-output`` option was not specified, ``bugpoint`` runs the
test program with the "safe" backend (which is assumed to generate good code) to
generate a reference output.  Once ``bugpoint`` has a reference output for the
test program, it tries executing it with the selected code generator.  If the
selected code generator crashes, ``bugpoint`` starts the `crash debugger`_ on
the code generator.  Otherwise, if the resulting output differs from the
reference output, it assumes the difference resulted from a code generator
failure, and starts the `code generator debugger`_.

Finally, if the output of the selected code generator matches the reference
output, ``bugpoint`` runs the test program after all of the LLVM passes have
been applied to it.  If its output differs from the reference output, it assumes
the difference resulted from a failure in one of the LLVM passes, and enters the
`miscompilation debugger`_.  Otherwise, there is no problem ``bugpoint`` can
debug.

.. _crash debugger:

Crash debugger
--------------

If an optimizer or code generator crashes, ``bugpoint`` will try as hard as it
can to reduce the list of passes (for optimizer crashes) and the size of the
test program.  First, ``bugpoint`` figures out which combination of optimizer
passes triggers the bug. This is useful when debugging a problem exposed by
``opt``, for example, because it runs over 38 passes.

Next, ``bugpoint`` tries removing functions from the test program, to reduce its
size.  Usually it is able to reduce a test program to a single function, when
debugging intraprocedural optimizations.  Once the number of functions has been
reduced, it attempts to delete various edges in the control flow graph, to
reduce the size of the function as much as possible.  Finally, ``bugpoint``
deletes any individual LLVM instructions whose absence does not eliminate the
failure.  At the end, ``bugpoint`` should tell you what passes crash, give you a
bitcode file, and give you instructions on how to reproduce the failure with
``opt`` or ``llc``.

.. _code generator debugger:

Code generator debugger
-----------------------

The code generator debugger attempts to narrow down the amount of code that is
being miscompiled by the selected code generator.  To do this, it takes the test
program and partitions it into two pieces: one piece which it compiles with the
"safe" backend (into a shared object), and one piece which it runs with either
the JIT or the static LLC compiler.  It uses several techniques to reduce the
amount of code pushed through the LLVM code generator, to reduce the potential
scope of the problem.  After it is finished, it emits two bitcode files (called
"test" [to be compiled with the code generator] and "safe" [to be compiled with
the "safe" backend], respectively), and instructions for reproducing the
problem.  The code generator debugger assumes that the "safe" backend produces
good code.

.. _miscompilation debugger:

Miscompilation debugger
-----------------------

The miscompilation debugger works similarly to the code generator debugger.  It
works by splitting the test program into two pieces, running the optimizations
specified on one piece, linking the two pieces back together, and then executing
the result.  It attempts to narrow down the list of passes to the one (or few)
which are causing the miscompilation, then reduce the portion of the test
program which is being miscompiled.  The miscompilation debugger assumes that
the selected code generator is working properly.

Advice for using bugpoint
=========================

``bugpoint`` can be a remarkably useful tool, but it sometimes works in
non-obvious ways.  Here are some hints and tips:

* In the code generator and miscompilation debuggers, ``bugpoint`` only works
  with programs that have deterministic output.  Thus, if the program outputs
  ``argv[0]``, the date, time, or any other "random" data, ``bugpoint`` may
  misinterpret differences in these data, when output, as the result of a
  miscompilation.  Programs should be temporarily modified to disable outputs
  that are likely to vary from run to run.

* In the `crash debugger`_, ``bugpoint`` does not distinguish different crashes
  during reduction. Thus, if new crash or miscompilation happens, ``bugpoint``
  will continue with the new crash instead. If you would like to stick to
  particular crash, you should write check scripts to validate the error
  message, see ``-compile-command`` in :doc:`CommandGuide/bugpoint`.

* In the code generator and miscompilation debuggers, debugging will go faster
  if you manually modify the program or its inputs to reduce the runtime, but
  still exhibit the problem.

* ``bugpoint`` is extremely useful when working on a new optimization: it helps
  track down regressions quickly.  To avoid having to relink ``bugpoint`` every
  time you change your optimization however, have ``bugpoint`` dynamically load
  your optimization with the ``-load`` option.

* ``bugpoint`` can generate a lot of output and run for a long period of time.
  It is often useful to capture the output of the program to file.  For example,
  in the C shell, you can run:

  .. code-block:: console

    $ bugpoint  ... |& tee bugpoint.log

  to get a copy of ``bugpoint``'s output in the file ``bugpoint.log``, as well
  as on your terminal.

* ``bugpoint`` cannot debug problems with the LLVM linker. If ``bugpoint``
  crashes before you see its "All input ok" message, you might try ``llvm-link
  -v`` on the same set of input files. If that also crashes, you may be
  experiencing a linker bug.

* ``bugpoint`` is useful for proactively finding bugs in LLVM.  Invoking
  ``bugpoint`` with the ``-find-bugs`` option will cause the list of specified
  optimizations to be randomized and applied to the program. This process will
  repeat until a bug is found or the user kills ``bugpoint``.

* ``bugpoint`` can produce IR which contains long names. Run ``opt
  -passes=metarenamer`` over the IR to rename everything using easy-to-read,
  metasyntactic names. Alternatively, run ``opt -passes=strip,instnamer`` to
  rename everything with very short (often purely numeric) names.

What to do when bugpoint isn't enough
=====================================
	
Sometimes, ``bugpoint`` is not enough. In particular, InstCombine and
TargetLowering both have visitor structured code with lots of potential
transformations.  If the process of using bugpoint has left you with still too
much code to figure out and the problem seems to be in instcombine, the
following steps may help.  These same techniques are useful with TargetLowering
as well.

Turn on ``-debug-only=instcombine`` and see which transformations within
instcombine are firing by selecting out lines with "``IC``" in them.

At this point, you have a decision to make.  Is the number of transformations
small enough to step through them using a debugger?  If so, then try that.

If there are too many transformations, then a source modification approach may
be helpful.  In this approach, you can modify the source code of instcombine to
disable just those transformations that are being performed on your test input
and perform a binary search over the set of transformations.  One set of places
to modify are the "``visit*``" methods of ``InstCombiner`` (*e.g.*
``visitICmpInst``) by adding a "``return false``" as the first line of the
method.

If that still doesn't remove enough, then change the caller of
``InstCombiner::DoOneIteration``, ``InstCombiner::runOnFunction`` to limit the
number of iterations.

You may also find it useful to use "``-stats``" now to see what parts of
instcombine are firing.  This can guide where to put additional reporting code.

At this point, if the amount of transformations is still too large, then
inserting code to limit whether or not to execute the body of the code in the
visit function can be helpful.  Add a static counter which is incremented on
every invocation of the function.  Then add code which simply returns false on
desired ranges.  For example:

.. code-block:: c++


  static int calledCount = 0;
  calledCount++;
  LLVM_DEBUG(if (calledCount < 212) return false);
  LLVM_DEBUG(if (calledCount > 217) return false);
  LLVM_DEBUG(if (calledCount == 213) return false);
  LLVM_DEBUG(if (calledCount == 214) return false);
  LLVM_DEBUG(if (calledCount == 215) return false);
  LLVM_DEBUG(if (calledCount == 216) return false);
  LLVM_DEBUG(dbgs() << "visitXOR calledCount: " << calledCount << "\n");
  LLVM_DEBUG(dbgs() << "I: "; I->dump());

could be added to ``visitXOR`` to limit ``visitXor`` to being applied only to
calls 212 and 217. This is from an actual test case and raises an important
point---a simple binary search may not be sufficient, as transformations that
interact may require isolating more than one call.  In TargetLowering, use
``return SDNode();`` instead of ``return false;``.

Now that the number of transformations is down to a manageable number, try
examining the output to see if you can figure out which transformations are
being done.  If that can be figured out, then do the usual debugging.  If which
code corresponds to the transformation being performed isn't obvious, set a
breakpoint after the call count based disabling and step through the code.
Alternatively, you can use "``printf``" style debugging to report waypoints.
