============================
AMDGPU Instructions Notation
============================

.. contents::
   :local:

.. _amdgpu_syn_instruction_notation:

Introduction
============

This is an overview of notation used to describe the syntax of AMDGPU assembler instructions.

This notation looks a lot like the :ref:`syntax of assembler instructions<amdgpu_syn_instructions>`,
except that instead of real operands and modifiers, it uses references to their descriptions.

Instructions
============

Notation
~~~~~~~~

This is the notation used to describe AMDGPU instructions:

  | ``<``\ :ref:`opcode description<amdgpu_syn_opcode_notation>`\ ``>
      <``\ :ref:`operands description<amdgpu_syn_instruction_operands_notation>`\ ``>
      <``\ :ref:`modifiers description<amdgpu_syn_instruction_modifiers_notation>`\ ``>``

.. _amdgpu_syn_opcode_notation:

Opcode
======

Notation
~~~~~~~~

TBD

.. _amdgpu_syn_instruction_operands_notation:

Operands
========

An instruction may have zero or more *operands*. They are comma-separated in the description:

  | ``<``\ :ref:`description of operand 0<amdgpu_syn_instruction_operand_notation>`\ ``>,
      <``\ :ref:`description of operand 1<amdgpu_syn_instruction_operand_notation>`\ ``>, ...``

The order of *operands* is fixed. *Operands* cannot be omitted
except for special cases described below.

.. _amdgpu_syn_instruction_operand_notation:

Notation
~~~~~~~~

An operand is described using the following notation:

    *<kind><name><tag0><tag1>...*

Where:

* *kind* is an optional prefix describing operand :ref:`kind<amdgpu_syn_instruction_operand_kinds>`.
* *name* is a link to a description of the operand.
* *tags* are optional. They are used to indicate
  :ref:`special operand properties<amdgpu_syn_instruction_operand_tags>`.

.. _amdgpu_syn_instruction_operand_kinds:

Operand Kinds
^^^^^^^^^^^^^

Operand kind indicates which values are accepted by the operand.

* Operands which only accept *vector* registers are labelled with 'v' prefix.
* Operands which only accept *scalar* registers and values are labelled with 's' prefix.
* Operands which accept any registers and values have no prefix.

Examples:

.. parsed-literal::

    vdata          // operand only accepts vector registers
    sdst           // operand only accepts scalar registers
    src1           // operand accepts vector registers, scalar registers, and scalar values

.. _amdgpu_syn_instruction_operand_tags:

Operand Tags
^^^^^^^^^^^^

Operand tags indicate special operand properties.

    ============== =================================================================================
    Operand tag    Meaning
    ============== =================================================================================
    :opt           An optional operand.
    :m             An operand which may be used with operand modifiers
                   :ref:`abs<amdgpu_synid_abs>`, :ref:`neg<amdgpu_synid_neg>` or
                   :ref:`sext<amdgpu_synid_sext>`.
    :dst           An input operand which is also used as a destination
                   if :ref:`glc<amdgpu_synid_glc>` modifier is specified.
    :fx            This is a *f32* or *f16* operand, depending on
                   :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` modifier.
    :<type>        The operand *type* differs from the *type*
                   :ref:`implied by the opcode name<amdgpu_syn_instruction_type>`.
                   This tag specifies the actual operand *type*.
    ============== =================================================================================

Examples:

.. parsed-literal::

    src1:m             // src1 operand may be used with operand modifiers
    vdata:dst          // vdata operand may be used as both source and destination
    vdst:u32           // vdst operand has u32 type

.. _amdgpu_syn_instruction_modifiers_notation:

Modifiers
=========

An instruction may have zero or more optional *modifiers*. They are space-separated in the description:

  | ``<``\ :ref:`description of modifier 0<amdgpu_syn_instruction_modifier_notation>`\ ``>
      <``\ :ref:`description of modifier 1<amdgpu_syn_instruction_modifier_notation>`\ ``> ...``

The order of *modifiers* is fixed.

.. _amdgpu_syn_instruction_modifier_notation:

Notation
~~~~~~~~

A *modifier* is described using the following notation:

    *<name>*

Where the *name* is a link to a description of the *modifier*.
