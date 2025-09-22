.. _porting:

Porting GlobalISel to A New Target
==================================

There are four major classes to implement by the target:

* :ref:`CallLowering <translator-call-lower>` --- lower calls, returns, and
  arguments according to the ABI.
* :ref:`RegisterBankInfo <api-registerbankinfo>` --- describe
  :ref:`gmir-regbank` coverage, cross-bank copy cost, and the mapping of
  operands onto banks for each instruction.
* :ref:`LegalizerInfo <api-legalizerinfo>` --- describe what is legal, and how
  to legalize what isn't.
* :ref:`InstructionSelector <api-instructionselector>` --- select generic MIR
  to target-specific MIR.

Additionally:

* ``TargetPassConfig`` --- create the passes constituting the pipeline,
  including additional passes not included in the :ref:`pipeline`.

Tutorials
=========

We'd recommend watching `this tutorial
<https://www.llvm.org/devmtg/2017-10/#tutorial2>`_ from the 2017 LLVM DevMeeting
which gave an overview of how to bring up a new backend in GlobalISel.
