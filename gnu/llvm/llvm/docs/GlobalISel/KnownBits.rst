Known Bits Analysis
===================

The Known Bits Analysis pass makes information about the known values of bits
available to other passes to enable transformations like those in the examples
below. The information is lazily computed so you should only pay for what you
use.

Examples
--------

A simple example is that transforming::

  a + 1

into::

  a | 1

is only valid when the addition doesn't carry. In other words it's only valid
if ``a & 1`` is zero.

Another example is:

.. code-block:: none

  %1:(s32) = G_CONSTANT i32 0xFF0
  %2:(s32) = G_AND %0, %1
  %3:(s32) = G_CONSTANT i32 0x0FF
  %4:(s32) = G_AND %2, %3

We can use the constants and the definition of ``G_AND`` to determine the known
bits:

.. code-block:: none

                                   ; %0 = 0x????????
  %1:(s32) = G_CONSTANT i32 0xFF0  ; %1 = 0x00000FF0
  %2:(s32) = G_AND %0, %1          ; %2 = 0x00000??0
  %3:(s32) = G_CONSTANT i32 0x0FF  ; %3 = 0x000000FF
  %4:(s32) = G_AND %2, %3          ; %4 = 0x000000?0

and then use this to simplify the expression:

.. code-block:: none

                                   ; %0 = 0x????????
  %5:(s32) = G_CONSTANT i32 0x0F0  ; %5 = 0x00000FF0
  %4:(s32) = G_AND %0, %5          ; %4 = 0x000000?0

Note that ``%4`` still has the same known bits as before the transformation.
Many transformations share this property. The main exception being when the
transform causes undefined bits to become defined to either zero, one, or
defined but unknown.

Usage
-----

To use Known Bits Analysis in a pass, first include the header and register the
dependency with ``INITIALIZE_PASS_DEPENDENCY``.

.. code-block:: c++

  #include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"

  ...

  INITIALIZE_PASS_BEGIN(...)
  INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
  INITIALIZE_PASS_END(...)

and require the pass in ``getAnalysisUsage``.

.. code-block:: c++

  void MyPass::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<GISelKnownBitsAnalysis>();
    // Optional: If your pass preserves known bits analysis (many do) then
    //           indicate that it's preserved for re-use by another pass here.
    AU.addPreserved<GISelKnownBitsAnalysis>();
  }

Then it's just a matter of fetching the analysis and using it:

.. code-block:: c++

  bool MyPass::runOnMachineFunction(MachineFunction &MF) {
    ...
    GISelKnownBits &KB = getAnalysis<GISelKnownBitsAnalysis>().get(MF);
    ...
    MachineInstr *MI = ...;
    KnownBits Known = KB->getKnownBits(MI->getOperand(0).getReg());
    if (Known.Zeros & 1) {
      // Bit 0 is known to be zero
    }
    ...
  }

There are many more API's beyond ``getKnownBits()``. See the `API reference
<https://llvm.org/doxygen>`_ for more information
