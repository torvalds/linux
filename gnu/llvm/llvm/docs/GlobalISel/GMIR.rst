.. _gmir:

Generic Machine IR
==================

.. contents::
   :local:

Generic MIR (gMIR) is an intermediate representation that shares the same data
structures as :doc:`MachineIR (MIR) <../MIRLangRef>` but has more relaxed
constraints. As the compilation pipeline proceeds, these constraints are
gradually tightened until gMIR has become MIR.

The rest of this document will assume that you are familiar with the concepts
in :doc:`MachineIR (MIR) <../MIRLangRef>` and will highlight the differences
between MIR and gMIR.

.. _gmir-instructions:

Generic Machine Instructions
----------------------------

.. note::

  This section expands on :ref:`mir-instructions` from the MIR Language
  Reference.

Whereas MIR deals largely in Target Instructions and only has a small set of
target independent opcodes such as ``COPY``, ``PHI``, and ``REG_SEQUENCE``,
gMIR defines a rich collection of ``Generic Opcodes`` which are target
independent and describe operations which are typically supported by targets.
One example is ``G_ADD`` which is the generic opcode for an integer addition.
More information on each of the generic opcodes can be found at
:doc:`GenericOpcode`.

The ``MachineIRBuilder`` class wraps the ``MachineInstrBuilder`` and provides
a convenient way to create these generic instructions.

.. _gmir-gvregs:

Generic Virtual Registers
-------------------------

.. note::

  This section expands on :ref:`mir-registers` from the MIR Language
  Reference.

Generic virtual registers are like virtual registers but they are not assigned a
Register Class constraint. Instead, generic virtual registers have less strict
constraints starting with a :ref:`gmir-llt` and then further constrained to a
:ref:`gmir-regbank`. Eventually they will be constrained to a register class
at which point they become normal virtual registers.

Generic virtual registers can be used with all the virtual register API's
provided by ``MachineRegisterInfo``. In particular, the def-use chain API's can
be used without needing to distinguish them from non-generic virtual registers.

For simplicity, most generic instructions only accept virtual registers (both
generic and non-generic). There are some exceptions to this but in general:

* instead of immediates, they use a generic virtual register defined by an
  instruction that materializes the immediate value (see
  :ref:`irtranslator-constants`). Typically this is a G_CONSTANT or a
  G_FCONSTANT. One example of an exception to this rule is G_SEXT_INREG where
  having an immediate is mandatory.
* instead of physical register, they use a generic virtual register that is
  either defined by a ``COPY`` from the physical register or used by a ``COPY``
  that defines the physical register.

.. admonition:: Historical Note

  We started with an alternative representation, where MRI tracks a size for
  each generic virtual register, and instructions have lists of types.
  That had two flaws: the type and size are redundant, and there was no generic
  way of getting a given operand's type (as there was no 1:1 mapping between
  instruction types and operands).
  We considered putting the type in some variant of MCInstrDesc instead:
  See `PR26576 <https://llvm.org/PR26576>`_: [GlobalISel] Generic MachineInstrs
  need a type but this increases the memory footprint of the related objects

.. _gmir-regbank:

Register Bank
-------------

A Register Bank is a set of register classes defined by the target. This
definition is rather loose so let's talk about what they can achieve.

Suppose we have a processor that has two register files, A and B. These are
equal in every way and support the same instructions for the same cost. They're
just physically stored apart and each instruction can only access registers from
A or B but never a mix of the two. If we want to perform an operation on data
that's in split between the two register files, we must first copy all the data
into a single register file.

Given a processor like this, we would benefit from clustering related data
together into one register file so that we minimize the cost of copying data
back and forth to satisfy the (possibly conflicting) requirements of all the
instructions. Register Banks are a means to constrain the register allocator to
use a particular register file for a virtual register.

In practice, register files A and B are rarely equal. They can typically store
the same data but there's usually some restrictions on what operations you can
do on each register file. A fairly common pattern is for one of them to be
accessible to integer operations and the other accessible to floating point
operations. To accommodate this, let's rename A and B to GPR (general purpose
registers) and FPR (floating point registers).

We now have some additional constraints that limit us. An operation like G_FMUL
has to happen in FPR and G_ADD has to happen in GPR. However, even though this
prescribes a lot of the assignments we still have some freedom. A G_LOAD can
happen in both GPR and FPR, and which we want depends on who is going to consume
the loaded data. Similarly, G_FNEG can happen in both GPR and FPR. If we assign
it to FPR, then we'll use floating point negation. However, if we assign it to
GPR then we can equivalently G_XOR the sign bit with 1 to invert it.

In summary, Register Banks are a means of disambiguating between seemingly
equivalent choices based on some analysis of the differences when each choice
is applied in a given context.

To give some concrete examples:

AArch64

  AArch64 has three main banks. GPR for integer operations, FPR for floating
  point and also for the NEON vector instruction set. The third is CCR and
  describes the condition code register used for predication.

MIPS

  MIPS has five main banks of which many programs only really use one or two.
  GPR is the general purpose bank for integer operations. FGR or CP1 is for
  the floating point operations as well as the MSA vector instructions and a
  few other application specific extensions. CP0 is for system registers and
  few programs will use it. CP2 and CP3 are for any application specific
  coprocessors that may be present in the chip. Arguably, there is also a sixth
  for the LO and HI registers but these are only used for the result of a few
  operations and it's of questionable value to model distinctly from GPR.

X86

  X86 can be seen as having 3 main banks: general-purpose, x87, and
  vector (which could be further split into a bank per domain for single vs
  double precision instructions). It also looks like there's arguably a few
  more potential banks such as one for the AVX512 Mask Registers.

Register banks are described by a target-provided API,
:ref:`RegisterBankInfo <api-registerbankinfo>`.

.. _gmir-llt:

Low Level Type
--------------

Additionally, every generic virtual register has a type, represented by an
instance of the ``LLT`` class.

Like ``EVT``/``MVT``/``Type``, it has no distinction between unsigned and signed
integer types.  Furthermore, it also has no distinction between integer and
floating-point types: it mainly conveys absolutely necessary information, such
as size and number of vector lanes:

* ``sN`` for scalars
* ``pN`` for pointers
* ``<N x sM>`` for vectors

``LLT`` is intended to replace the usage of ``EVT`` in SelectionDAG.

Here are some LLT examples and their ``EVT`` and ``Type`` equivalents:

   =============  =========  ======================================
   LLT            EVT        IR Type
   =============  =========  ======================================
   ``s1``         ``i1``     ``i1``
   ``s8``         ``i8``     ``i8``
   ``s32``        ``i32``    ``i32``
   ``s32``        ``f32``    ``float``
   ``s17``        ``i17``    ``i17``
   ``s16``        N/A        ``{i8, i8}`` [#abi-dependent]_
   ``s32``        N/A        ``[4 x i8]`` [#abi-dependent]_
   ``p0``         ``iPTR``   ``i8*``, ``i32*``, ``%opaque*``
   ``p2``         ``iPTR``   ``i8 addrspace(2)*``
   ``<4 x s32>``  ``v4f32``  ``<4 x float>``
   ``s64``        ``v1f64``  ``<1 x double>``
   ``<3 x s32>``  ``v3i32``  ``<3 x i32>``
   =============  =========  ======================================


Rationale: instructions already encode a specific interpretation of types
(e.g., ``add`` vs. ``fadd``, or ``sdiv`` vs. ``udiv``).  Also encoding that
information in the type system requires introducing bitcast with no real
advantage for the selector.

Pointer types are distinguished by address space.  This matches IR, as opposed
to SelectionDAG where address space is an attribute on operations.
This representation better supports pointers having different sizes depending
on their addressspace.

.. note::

  .. caution::

    Is this still true? I thought we'd removed the 1-element vector concept.
    Hypothetically, it could be distinct from a scalar but I think we failed to
    find a real occurrence.

  Currently, LLT requires at least 2 elements in vectors, but some targets have
  the concept of a '1-element vector'.  Representing them as their underlying
  scalar type is a nice simplification.

.. rubric:: Footnotes

.. [#abi-dependent] This mapping is ABI dependent. Here we've assumed no additional padding is required.

Generic Opcode Reference
------------------------

The Generic Opcodes that are available are described at :doc:`GenericOpcode`.
