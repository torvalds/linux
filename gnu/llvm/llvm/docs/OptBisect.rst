====================================================
Using -opt-bisect-limit to debug optimization errors
====================================================
.. contents::
   :local:
   :depth: 1

Introduction
============

The -opt-bisect-limit option provides a way to disable all optimization passes
above a specified limit without modifying the way in which the Pass Managers
are populated.  The intention of this option is to assist in tracking down
problems where incorrect transformations during optimization result in incorrect
run-time behavior.

This feature is implemented on an opt-in basis.  Passes which can be safely
skipped while still allowing correct code generation call a function to
check the opt-bisect limit before performing optimizations.  Passes which
either must be run or do not modify the IR do not perform this check and are
therefore never skipped.  Generally, this means analysis passes, passes
that are run at CodeGenOptLevel::None and passes which are required for register
allocation.

The -opt-bisect-limit option can be used with any tool, including front ends
such as clang, that uses the core LLVM library for optimization and code
generation.  The exact syntax for invoking the option is discussed below.

This feature is not intended to replace other debugging tools such as bugpoint.
Rather it provides an alternate course of action when reproducing the problem
requires a complex build infrastructure that would make using bugpoint
impractical or when reproducing the failure requires a sequence of
transformations that is difficult to replicate with tools like opt and llc.


Getting Started
===============

The -opt-bisect-limit command line option can be passed directly to tools such
as opt, llc and lli.  The syntax is as follows:

::

  <tool name> [other options] -opt-bisect-limit=<limit>

If a value of -1 is used the tool will perform all optimizations but a message
will be printed to stderr for each optimization that could be skipped
indicating the index value that is associated with that optimization.  To skip
optimizations, pass the value of the last optimization to be performed as the
opt-bisect-limit.  All optimizations with a higher index value will be skipped.

In order to use the -opt-bisect-limit option with a driver that provides a
wrapper around the LLVM core library, an additional prefix option may be
required, as defined by the driver.  For example, to use this option with
clang, the "-mllvm" prefix must be used.  A typical clang invocation would look
like this:

::

  clang -O2 -mllvm -opt-bisect-limit=256 my_file.c

The -opt-bisect-limit option may also be applied to link-time optimizations by
using a prefix to indicate that this is a plug-in option for the linker. The
following syntax will set a bisect limit for LTO transformations:

::

  # When using lld, or ld64 (macOS)
  clang -flto -Wl,-mllvm,-opt-bisect-limit=256 my_file.o my_other_file.o
  # When using Gold
  clang -flto -Wl,-plugin-opt,-opt-bisect-limit=256 my_file.o my_other_file.o

LTO passes are run by a library instance invoked by the linker. Therefore any
passes run in the primary driver compilation phase are not affected by options
passed via '-Wl,-plugin-opt' and LTO passes are not affected by options
passed to the driver-invoked LLVM invocation via '-mllvm'.

Passing ``-opt-bisect-print-ir-path=path/foo.ll`` will dump the IR to
``path/foo.ll`` when -opt-bisect-limit starts skipping passes.

Bisection Index Values
======================

The granularity of the optimizations associated with a single index value is
variable.  Depending on how the optimization pass has been instrumented the
value may be associated with as much as all transformations that would have
been performed by an optimization pass on an IR unit for which it is invoked
(for instance, during a single call of runOnFunction for a FunctionPass) or as
little as a single transformation. The index values may also be nested so that
if an invocation of the pass is not skipped individual transformations within
that invocation may still be skipped.

The order of the values assigned is guaranteed to remain stable and consistent
from one run to the next up to and including the value specified as the limit.
Above the limit value skipping of optimizations can cause a change in the
numbering, but because all optimizations above the limit are skipped this
is not a problem.

When an opt-bisect index value refers to an entire invocation of the run
function for a pass, the pass will query whether or not it should be skipped
each time it is invoked and each invocation will be assigned a unique value.
For example, if a FunctionPass is used with a module containing three functions
a different index value will be assigned to the pass for each of the functions
as the pass is run. The pass may be run on two functions but skipped for the
third.

If the pass internally performs operations on a smaller IR unit the pass must be
specifically instrumented to enable bisection at this finer level of granularity
(see below for details).


Example Usage
=============

.. code-block:: console

  $ opt -O2 -o test-opt.bc -opt-bisect-limit=16 test.ll

  BISECT: running pass (1) Simplify the CFG on function (g)
  BISECT: running pass (2) SROA on function (g)
  BISECT: running pass (3) Early CSE on function (g)
  BISECT: running pass (4) Infer set function attributes on module (test.ll)
  BISECT: running pass (5) Interprocedural Sparse Conditional Constant Propagation on module (test.ll)
  BISECT: running pass (6) Global Variable Optimizer on module (test.ll)
  BISECT: running pass (7) Promote Memory to Register on function (g)
  BISECT: running pass (8) Dead Argument Elimination on module (test.ll)
  BISECT: running pass (9) Combine redundant instructions on function (g)
  BISECT: running pass (10) Simplify the CFG on function (g)
  BISECT: running pass (11) Remove unused exception handling info on SCC (<<null function>>)
  BISECT: running pass (12) Function Integration/Inlining on SCC (<<null function>>)
  BISECT: running pass (13) Deduce function attributes on SCC (<<null function>>)
  BISECT: running pass (14) Remove unused exception handling info on SCC (f)
  BISECT: running pass (15) Function Integration/Inlining on SCC (f)
  BISECT: running pass (16) Deduce function attributes on SCC (f)
  BISECT: NOT running pass (17) Remove unused exception handling info on SCC (g)
  BISECT: NOT running pass (18) Function Integration/Inlining on SCC (g)
  BISECT: NOT running pass (19) Deduce function attributes on SCC (g)
  BISECT: NOT running pass (20) SROA on function (g)
  BISECT: NOT running pass (21) Early CSE on function (g)
  BISECT: NOT running pass (22) Speculatively execute instructions if target has divergent branches on function (g)
  ... etc. ...


Pass Skipping Implementation
============================

The -opt-bisect-limit implementation depends on individual passes opting in to
the opt-bisect process.  The OptBisect object that manages the process is
entirely passive and has no knowledge of how any pass is implemented.  When a
pass is run if the pass may be skipped, it should call the OptBisect object to
see if it should be skipped.

The OptBisect object is intended to be accessed through LLVMContext and each
Pass base class contains a helper function that abstracts the details in order
to make this check uniform across all passes.  These helper functions are:

.. code-block:: c++

  bool ModulePass::skipModule(Module &M);
  bool CallGraphSCCPass::skipSCC(CallGraphSCC &SCC);
  bool FunctionPass::skipFunction(const Function &F);
  bool LoopPass::skipLoop(const Loop *L);

A MachineFunctionPass should use FunctionPass::skipFunction() as such:

.. code-block:: c++

  bool MyMachineFunctionPass::runOnMachineFunction(Function &MF) {
    if (skipFunction(*MF.getFunction())
      return false;
    // Otherwise, run the pass normally.
  }

In addition to checking with the OptBisect class to see if the pass should be
skipped, the skipFunction(), skipLoop() and skipBasicBlock() helper functions
also look for the presence of the "optnone" function attribute.  The calling
pass will be unable to determine whether it is being skipped because the
"optnone" attribute is present or because the opt-bisect-limit has been
reached.  This is desirable because the behavior should be the same in either
case.

The majority of LLVM passes which can be skipped have already been instrumented
in the manner described above.  If you are adding a new pass or believe you
have found a pass which is not being included in the opt-bisect process but
should be, you can add it as described above.


Adding Finer Granularity
========================

Once the pass in which an incorrect transformation is performed has been
determined, it may be useful to perform further analysis in order to determine
which specific transformation is causing the problem.  Debug counters
can be used for this purpose.
