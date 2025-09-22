=========================
 RISC-V Vector Extension
=========================

.. contents::
   :local:

The RISC-V target supports the 1.0 version of the `RISC-V Vector Extension (RVV) <https://github.com/riscv/riscv-v-spec/blob/v1.0/v-spec.adoc>`_.
This guide gives an overview of how it's modelled in LLVM IR and how the backend generates code for it.

Mapping to LLVM IR types
========================

RVV adds 32 VLEN sized registers, where VLEN is an unknown constant to the compiler. To be able to represent VLEN sized values, the RISC-V backend takes the same approach as AArch64's SVE and uses `scalable vector types <https://llvm.org/docs/LangRef.html#t-vector>`_.

Scalable vector types are of the form ``<vscale x n x ty>``, which indicates a vector with a multiple of ``n`` elements of type ``ty``.
On RISC-V ``n`` and ``ty`` control LMUL and SEW respectively.

LLVM only supports ELEN=32 or ELEN=64, so ``vscale`` is defined as VLEN/64 (see ``RISCV::RVVBitsPerBlock``).
Note this means that VLEN must be at least 64, so VLEN=32 isn't currently supported.

+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+
|                   | LMUL=⅛        | LMUL=¼         | LMUL=½           | LMUL=1            | LMUL=2            | LMUL=4            | LMUL=8            |
+===================+===============+================+==================+===================+===================+===================+===================+
| i64 (ELEN=64)     | N/A           | N/A            | N/A              | <v x 1 x i64>     | <v x 2 x i64>     | <v x 4 x i64>     | <v x 8 x i64>     |
+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+
| i32               | N/A           | N/A            | <v x 1 x i32>    | <v x 2 x i32>     | <v x 4 x i32>     | <v x 8 x i32>     | <v x 16 x i32>    |
+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+
| i16               | N/A           | <v x 1 x i16>  | <v x 2 x i16>    | <v x 4 x i16>     | <v x 8 x i16>     | <v x 16 x i16>    | <v x 32 x i16>    |
+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+
| i8                | <v x 1 x i8>  | <v x 2 x i8>   | <v x 4 x i8>     | <v x 8 x i8>      | <v x 16 x i8>     | <v x 32 x i8>     | <v x 64 x i8>     |
+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+
| double (ELEN=64)  | N/A           | N/A            | N/A              | <v x 1 x double>  | <v x 2 x double>  | <v x 4 x double>  | <v x 8 x double>  |
+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+
| float             | N/A           | N/A            | <v x 1 x float>  | <v x 2 x float>   | <v x 4 x float>   | <v x 8 x float>   | <v x 16 x float>  |
+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+
| half              | N/A           | <v x 1 x half> | <v x 2 x half>   | <v x 4 x half>    | <v x 8 x half>    | <v x 16 x half>   | <v x 32 x half>   |
+-------------------+---------------+----------------+------------------+-------------------+-------------------+-------------------+-------------------+

(Read ``<v x k x ty>`` as ``<vscale x k x ty>``)


Mask vector types
-----------------

Mask vectors are physically represented using a layout of densely packed bits in a vector register.
They are mapped to the following LLVM IR types:

- ``<vscale x 1 x i1>``
- ``<vscale x 2 x i1>``
- ``<vscale x 4 x i1>``
- ``<vscale x 8 x i1>``
- ``<vscale x 16 x i1>``
- ``<vscale x 32 x i1>``
- ``<vscale x 64 x i1>``

Two types with the same SEW/LMUL ratio will have the same related mask type.
For instance, two different comparisons one under SEW=64, LMUL=2 and the other under SEW=32, LMUL=1 will both generate a mask ``<vscale x 2 x i1>``.

Representation in LLVM IR
=========================

Vector instructions can be represented in three main ways in LLVM IR:

1. Regular instructions on both scalable and fixed-length vector types

   .. code-block:: llvm

       %c = add <vscale x 4 x i32> %a, %b
       %f = add <4 x i32> %d, %e

2. RISC-V vector intrinsics, which mirror the `C intrinsics specification <https://github.com/riscv-non-isa/rvv-intrinsic-doc>`_

   These come in unmasked variants:

   .. code-block:: llvm

       %c = call @llvm.riscv.vadd.nxv4i32.nxv4i32(
              <vscale x 4 x i32> %passthru,
	      <vscale x 4 x i32> %a,
	      <vscale x 4 x i32> %b,
	      i64 %avl
	    )

   As well as masked variants:

   .. code-block:: llvm

       %c = call @llvm.riscv.vadd.mask.nxv4i32.nxv4i32(
              <vscale x 4 x i32> %passthru,
	      <vscale x 4 x i32> %a,
	      <vscale x 4 x i32> %b,
	      <vscale x 4 x i1> %mask,
	      i64 %avl,
	      i64 0 ; policy (must be an immediate)
	    )

   Both allow setting the AVL as well as controlling the inactive/tail elements via the passthru operand, but the masked variant also provides operands for the mask and ``vta``/``vma`` policy bits.

   The only valid types are scalable vector types.

3. :ref:`Vector predication (VP) intrinsics <int_vp>`

   .. code-block:: llvm

       %c = call @llvm.vp.add.nxv4i32(
	      <vscale x 4 x i32> %a,
	      <vscale x 4 x i32> %b,
	      <vscale x 4 x i1> %m
	      i32 %evl
	    )

   Unlike RISC-V intrinsics, VP intrinsics are target agnostic so they can be emitted from other optimisation passes in the middle-end (like the loop vectorizer). They also support fixed-length vector types.

   VP intrinsics also don't have passthru operands, but tail/mask undisturbed behaviour can be emulated by using the output in a ``@llvm.vp.merge``.
   It will get lowered as a ``vmerge``, but will be merged back into the underlying instruction's mask via ``RISCVDAGToDAGISel::performCombineVMergeAndVOps``.


The different properties of the above representations are summarized below:

+----------------------+--------------+-----------------+----------+------------------+----------------------+-----------------+
|                      | AVL          | Masking         | Passthru | Scalable vectors | Fixed-length vectors | Target agnostic |
+======================+==============+=================+==========+==================+======================+=================+
| LLVM IR instructions | Always VLMAX | No              | None     | Yes              | Yes                  | Yes             |
+----------------------+--------------+-----------------+----------+------------------+----------------------+-----------------+
| RVV intrinsics       | Yes          | Yes             | Yes      | Yes              | No                   | No              |
+----------------------+--------------+-----------------+----------+------------------+----------------------+-----------------+
| VP intrinsics        | Yes (EVL)    | Yes             | No       | Yes              | Yes                  | Yes             |
+----------------------+--------------+-----------------+----------+------------------+----------------------+-----------------+

SelectionDAG lowering
=====================

For most regular **scalable** vector LLVM IR instructions, their corresponding SelectionDAG nodes are legal on RISC-V and don't require any custom lowering.

.. code-block::

   t5: nxv4i32 = add t2, t4

RISC-V vector intrinsics also don't require any custom lowering.

.. code-block::

   t12: nxv4i32 = llvm.riscv.vadd TargetConstant:i64<10056>, undef:nxv4i32, t2, t4, t6

Fixed-length vectors
--------------------

Because there are no fixed-length vector patterns, fixed-length vectors need to be custom lowered and performed in a scalable "container" type:

1. The fixed-length vector operands are inserted into scalable containers with ``insert_subvector`` nodes. The container type is chosen such that its minimum size will fit the fixed-length vector (see ``getContainerForFixedLengthVector``).
2. The operation is then performed on the container type via a **VL (vector length) node**. These are custom nodes defined in ``RISCVInstrInfoVVLPatterns.td`` that mirror target agnostic SelectionDAG nodes, as well as some RVV instructions. They contain an AVL operand, which is set to the number of elements in the fixed-length vector.
   Some nodes also have a passthru or mask operand, which will usually be set to ``undef`` and all ones when lowering fixed-length vectors.
3. The result is put back into a fixed-length vector via ``extract_subvector``.

.. code-block::

       t2: nxv2i32,ch = CopyFromReg t0, Register:nxv2i32 %0
       t6: nxv2i32,ch = CopyFromReg t0, Register:nxv2i32 %1
     t4: v4i32 = extract_subvector t2, Constant:i64<0>
     t7: v4i32 = extract_subvector t6, Constant:i64<0>
   t8: v4i32 = add t4, t7

   // is custom lowered to:

       t2: nxv2i32,ch = CopyFromReg t0, Register:nxv2i32 %0
       t6: nxv2i32,ch = CopyFromReg t0, Register:nxv2i32 %1
       t15: nxv2i1 = RISCVISD::VMSET_VL Constant:i64<4>
     t16: nxv2i32 = RISCVISD::ADD_VL t2, t6, undef:nxv2i32, t15, Constant:i64<4>
   t17: v4i32 = extract_subvector t16, Constant:i64<0>

VL nodes often have a passthru or mask operand, which are usually set to ``undef`` and all ones for fixed-length vectors.

The ``insert_subvector`` and ``extract_subvector`` nodes responsible for wrapping and unwrapping will get combined away, and eventually we will lower all fixed-length vector types to scalable. Note that fixed-length vectors at the interface of a function are passed in a scalable vector container.

.. note::

   The only ``insert_subvector`` and ``extract_subvector`` nodes that make it through lowering are those that can be performed as an exact subregister insert or extract. This means that any fixed-length vector ``insert_subvector`` and ``extract_subvector`` nodes that aren't legalized must lie on a register group boundary, so the exact VLEN must be known at compile time (i.e., compiled with ``-mrvv-vector-bits=zvl`` or ``-mllvm -riscv-v-vector-bits-max=VLEN``, or have an exact ``vscale_range`` attribute).

Vector predication intrinsics
-----------------------------

VP intrinsics also get custom lowered via VL nodes.

.. code-block::

   t12: nxv2i32 = vp_add t2, t4, t6, Constant:i64<8>

   // is custom lowered to:

   t18: nxv2i32 = RISCVISD::ADD_VL t2, t4, undef:nxv2i32, t6, Constant:i64<8>

The VP EVL and mask are used for the VL node's AVL and mask respectively, whilst the passthru is set to ``undef``.

Instruction selection
=====================

``vl`` and ``vtype`` need to be configured correctly, so we can't just directly select the underlying vector ``MachineInstr``. Instead pseudo instructions are selected, which carry the extra information needed to emit the necessary ``vsetvli``\s later.

.. code-block::

   %c:vrm2 = PseudoVADD_VV_M2 %passthru:vrm2(tied-def 0), %a:vrm2, %b:vrm2, %vl:gpr, 5 /*sew*/, 3 /*policy*/

Each vector instruction has multiple pseudo instructions defined in ``RISCVInstrInfoVPseudos.td``.
There is a variant of each pseudo for each possible LMUL, as well as a masked variant. So a typical instruction like ``vadd.vv`` would have the following pseudos:

.. code-block::

   %rd:vr = PseudoVADD_VV_MF8 %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, %avl:gpr, sew:imm, policy:imm
   %rd:vr = PseudoVADD_VV_MF4 %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, %avl:gpr, sew:imm, policy:imm
   %rd:vr = PseudoVADD_VV_MF2 %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, %avl:gpr, sew:imm, policy:imm
   %rd:vr = PseudoVADD_VV_M1 %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, %avl:gpr, sew:imm, policy:imm
   %rd:vrm2 = PseudoVADD_VV_M2 %passthru:vrm2(tied-def 0), %rs2:vrm2, %rs1:vrm2, %avl:gpr, sew:imm, policy:imm
   %rd:vrm4 = PseudoVADD_VV_M4 %passthru:vrm4(tied-def 0), %rs2:vrm4, %rs1:vrm4, %avl:gpr, sew:imm, policy:imm
   %rd:vrm8 = PseudoVADD_VV_M8 %passthru:vrm8(tied-def 0), %rs2:vrm8, %rs1:vrm8, %avl:gpr, sew:imm, policy:imm
   %rd:vr = PseudoVADD_VV_MF8_MASK %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, mask:$v0, %avl:gpr, sew:imm, policy:imm
   %rd:vr = PseudoVADD_VV_MF4_MASK %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, mask:$v0, %avl:gpr, sew:imm, policy:imm
   %rd:vr = PseudoVADD_VV_MF2_MASK %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, mask:$v0, %avl:gpr, sew:imm, policy:imm
   %rd:vr = PseudoVADD_VV_M1_MASK %passthru:vr(tied-def 0), %rs2:vr, %rs1:vr, mask:$v0, %avl:gpr, sew:imm, policy:imm
   %rd:vrm2 = PseudoVADD_VV_M2_MASK %passthru:vrm2(tied-def 0), %rs2:vrm2, %%rs1:vrm2, mask:$v0, %avl:gpr, sew:imm, policy:imm
   %rd:vrm4 = PseudoVADD_VV_M4_MASK %passthru:vrm4(tied-def 0), %rs2:vrm4, %rs1:vrm4, mask:$v0, %avl:gpr, sew:imm, policy:imm
   %rd:vrm8 = PseudoVADD_VV_M8_MASK %passthru:vrm8(tied-def 0), %rs2:vrm8, %rs1:vrm8, mask:$v0, %avl:gpr, sew:imm, policy:imm

.. note::

   Whilst the SEW can be encoded in an operand, we need to use separate pseudos for each LMUL since different register groups will require different register classes: see :ref:`rvv_register_allocation`.


Pseudos have operands for the AVL and SEW (encoded as a power of 2), as well as potentially the mask, policy or rounding mode if applicable.
The passthru operand is tied to the destination register which will determine the inactive/tail elements.

For scalable vectors that should use VLMAX, the AVL is set to a sentinel value of ``-1``.

There are patterns for target agnostic SelectionDAG nodes in ``RISCVInstrInfoVSDPatterns.td``, VL nodes in ``RISCVInstrInfoVVLPatterns.td`` and RVV intrinsics in ``RISCVInstrInfoVPseudos.td``.

Mask patterns
-------------

For masked pseudos the mask operand is copied to the physical ``$v0`` register during instruction selection with a glued ``CopyToReg`` node:

.. code-block::

     t23: ch,glue = CopyToReg t0, Register:nxv4i1 $v0, t6
   t25: nxv4i32 = PseudoVADD_VV_M2_MASK Register:nxv4i32 $noreg, t2, t4, Register:nxv4i1 $v0, TargetConstant:i64<8>, TargetConstant:i64<5>, TargetConstant:i64<1>, t23:1

The patterns in ``RISCVInstrInfoVVLPatterns.td`` only match masked pseudos to reduce the size of the match table, even if the node's mask is all ones and could be an unmasked pseudo.
``RISCVFoldMasks::convertToUnmasked`` will detect if the mask is all ones and convert it into its unmasked form.

.. code-block::

   $v0 = PseudoVMSET_M_B16 -1, 32
   %rd:vrm2 = PseudoVADD_VV_M2_MASK %passthru:vrm2(tied-def 0), %rs2:vrm2, %rs1:vrm2, $v0, %avl:gpr, sew:imm, policy:imm

   // gets optimized to:

   %rd:vrm2 = PseudoVADD_VV_M2 %passthru:vrm2(tied-def 0), %rs2:vrm2, %rs1:vrm2, %avl:gpr, sew:imm, policy:imm

.. note::

   Any ``vmset.m`` can be treated as an all ones mask since the tail elements past AVL are ``undef`` and can be replaced with ones.

.. _rvv_register_allocation:

Register allocation
===================

Register allocation is split between vector and scalar registers, with vector allocation running first:

.. code-block::

  $v8m2 = PseudoVADD_VV_M2 $v8m2(tied-def 0), $v8m2, $v10m2, %vl:gpr, 5, 3

.. note::

   Register allocation is split so that :ref:`RISCVInsertVSETVLI` can run after vector register allocation, but before scalar register allocation. It needs to be run before scalar register allocation as it may need to create a new virtual register to set the AVL to VLMAX.

   Performing ``RISCVInsertVSETVLI`` after vector register allocation imposes fewer constraints on the machine scheduler since it cannot schedule instructions past ``vsetvli``\s, and it allows us to emit further vector pseudos during spilling or constant rematerialization.

There are four register classes for vectors:

- ``VR`` for vector registers (``v0``, ``v1,``, ..., ``v32``). Used when :math:`\text{LMUL} \leq 1` and mask registers.
- ``VRM2`` for vector groups of length 2 i.e., :math:`\text{LMUL}=2` (``v0m2``, ``v2m2``, ..., ``v30m2``)
- ``VRM4`` for vector groups of length 4 i.e., :math:`\text{LMUL}=4` (``v0m4``, ``v4m4``, ..., ``v28m4``)
- ``VRM8`` for vector groups of length 8 i.e., :math:`\text{LMUL}=8` (``v0m8``, ``v8m8``, ..., ``v24m8``)

:math:`\text{LMUL} \lt 1` types and mask types do not benefit from having a dedicated class, so ``VR`` is used in their case.

Some instructions have a constraint that a register operand cannot be ``V0`` or overlap with ``V0``, so for these cases we also have ``VRNoV0`` variants.

.. _RISCVInsertVSETVLI:

RISCVInsertVSETVLI
==================

After vector registers are allocated, the ``RISCVInsertVSETVLI`` pass will insert the necessary ``vsetvli``\s for the pseudos.

.. code-block::

  dead $x0 = PseudoVSETVLI %vl:gpr, 209, implicit-def $vl, implicit-def $vtype
  $v8m2 = PseudoVADD_VV_M2 $v8m2(tied-def 0), $v8m2, $v10m2, $noreg, 5, implicit $vl, implicit $vtype

The physical ``$vl`` and ``$vtype`` registers are implicitly defined by the ``PseudoVSETVLI``, and are implicitly used by the ``PseudoVADD``.
The ``vtype`` operand (``209`` in this example) is encoded as per the specification via ``RISCVVType::encodeVTYPE``.

``RISCVInsertVSETVLI`` performs dataflow analysis to emit as few ``vsetvli``\s as possible. It will also try to minimize the number of ``vsetvli``\s that set VL, i.e., it will emit ``vsetvli x0, x0`` if only ``vtype`` needs changed but ``vl`` doesn't.

Pseudo expansion and printing
=============================

After scalar register allocation, the ``RISCVExpandPseudoInsts.cpp`` pass expands the ``PseudoVSETVLI`` instructions.

.. code-block::

   dead $x0 = VSETVLI $x1, 209, implicit-def $vtype, implicit-def $vl
   renamable $v8m2 = PseudoVADD_VV_M2 $v8m2(tied-def 0), $v8m2, $v10m2, $noreg, 5, implicit $vl, implicit $vtype

Note that the vector pseudo remains as it's needed to encode the register class for the LMUL. Its AVL and SEW operands are no longer used.

``RISCVAsmPrinter`` will then lower the pseudo instructions into real ``MCInst``\s.

.. code-block:: nasm

   vsetvli a0, zero, e32, m2, ta, ma
   vadd.vv v8, v8, v10



See also
========

- `[llvm-dev] [RFC] Code generation for RISC-V V-extension <https://lists.llvm.org/pipermail/llvm-dev/2020-October/145850.html>`_
- `2023 LLVM Dev Mtg - Vector codegen in the RISC-V backend <https://youtu.be/-ox8iJmbp0c?feature=shared>`_
- `2023 LLVM Dev Mtg - How to add an C intrinsic and code-gen it, using the RISC-V vector C intrinsics <https://youtu.be/t17O_bU1jks?feature=shared>`_
- `2021 LLVM Dev Mtg “Optimizing code for scalable vector architectures” <https://youtu.be/daWLCyhwrZ8?feature=shared>`_
