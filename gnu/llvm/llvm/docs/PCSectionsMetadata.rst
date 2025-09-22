=========================
LLVM PC Sections Metadata
=========================

.. contents::
   :local:

Introduction
============

PC Sections Metadata can be attached to instructions and functions, for which
addresses, viz. program counters (PCs), are to be emitted in specially encoded
binary sections. Metadata is assigned as an ``MDNode`` of the ``MD_pcsections``
(``!pcsections``) kind; the following section describes the metadata format.

Metadata Format
===============

An arbitrary number of interleaved ``MDString`` and constant operators can be
added, where a new ``MDString`` always denotes a section name, followed by an
arbitrary number of auxiliary constant data encoded along the PC of the
instruction or function. The first operator must be a ``MDString`` denoting the
first section.

.. code-block:: none

  !0 = !{
    !"<section#1>"
    [ , !1 ... ]
    [ !"<section#2">
      [ , !2 ... ]
      ... ]
  }
  !1 = !{ iXX <aux-consts#1>, ... }
  !2 = !{ iXX <aux-consts#2>, ... }
  ...

The occurrence of ``section#1``, ``section#2``, ..., ``section#N`` in the
metadata causes the backend to emit the PC for the associated instruction or
function to all named sections. For each emitted PC in a section #N, the
constants ``aux-consts#N`` in the tuple ``!N`` will be emitted after the PC.
Multiple tuples with constant data may be provided after a section name string
(e.g. ``!0 = !{"s1", !1, !2}``), and a single constant tuple may be reused for
different sections (e.g. ``!0 = !{"s1", !1, "s2", !1}``).

Binary Encoding
===============

*Instructions* result in emitting a single PC, and *functions* result in
emission of the start of the function and a 32-bit size. This is followed by
the auxiliary constants that followed the respective section name in the
``MD_pcsections`` metadata.

To avoid relocations in the final binary, each PC address stored at ``entry``
is a relative relocation, computed as ``pc - entry``. To decode, a user has to
compute ``entry + *entry``.

The size of each entry depends on the code model. With large and medium sized
code models, the entry size matches pointer size. For any smaller code model
the entry size is just 32 bits.

Encoding Options
----------------

Optional encoding options can be passed in the first ``MDString`` operator:
``<section>!<options>``. The following options are available:

    * ``C`` -- Compress constant integers of size 2-8 bytes as ULEB128; this
      includes the function size (but excludes the PC entry).

For example, ``foo!C`` will emit into section ``foo`` with all constants
encoded as ULEB128.

Guarantees on Code Generation
=============================

Attaching ``!pcsections`` metadata to LLVM IR instructions *shall not* affect
optimizations or code generation outside the requested PC sections.

While relying on LLVM IR metadata to request PC sections makes the above
guarantee relatively trivial, propagation of metadata through the optimization
and code generation pipeline has the following guarantees.

Metadata Propagation
--------------------

In general, LLVM *does not make any guarantees* about preserving IR metadata
(attached to an ``Instruction``) through IR transformations. When using PC
sections metadata, this guarantee is unchanged, and ``!pcsections`` metadata is
remains *optional* until lowering to machine IR (MIR).

Note for Code Generation
------------------------

As with other LLVM IR metadata, there are no requirements for LLVM IR
transformation passes to preserve ``!pcsections`` metadata, with the following
exceptions:

    * The ``AtomicExpandPass`` shall preserve ``!pcsections`` metadata
      according to the below rules 1-4.

When translating LLVM IR to MIR, the ``!pcsections`` metadata shall be copied
from the source ``Instruction`` to the target ``MachineInstr`` (set with
``MachineInstr::setPCSections()``). The instruction selectors and MIR
optimization passes shall preserve PC sections metadata as follows:

    1. Replacements will preserve PC sections metadata of the replaced
       instruction.

    2. Duplications will preserve PC sections metadata of the copied
       instruction.

    3. Merging will preserve PC sections metadata of one of the two
       instructions (no guarantee on which instruction's metadata is used).

    4. Deletions will loose PC sections metadata.

This is similar to debug info, and the ``BuildMI()`` helper provides a
convenient way to propagate debug info and ``!pcsections`` metadata in the
``MIMetadata`` bundle.

Note for Metadata Users
-----------------------

Use cases for ``!pcsections`` metadata should either be fully tolerant to
missing metadata, or the passes inserting ``!pcsections`` metadata should run
*after* all LLVM IR optimization passes to preserve the metadata until being
translated to MIR.
