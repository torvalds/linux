..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx11_delay:

delay
=====

A delay between dependent SALU/VALU instructions.
This operand may specify a delay for 2 instructions:
the one after the current *s_delay_alu* instruction
and for the second instruction indicated by *SKIP*.

The bits of this operand have the following meaning:

    ===== ========================================================== ============
    Bits  Description                                                Value Range
    ===== ========================================================== ============
    3:0   ID0: indicates a delay for the first instruction.          0..11
    6:4   SKIP: indicates the position of the second instruction.    0..5
    10:7  ID1: indicates a delay for the second instruction.         0..11
    ===== ========================================================== ============

This operand may be specified as one of the following:

* An :ref:`integer_number<amdgpu_synid_integer_number>` or an :ref:`absolute_expression<amdgpu_synid_absolute_expression>`. The value must be in the range from 0 to 0xFFFF.
* A combination of *instid0*, *instskip*, *instid1* values which are described below.

    ======================== =========================== ===============
    Syntax                   Description                 Default Value
    ======================== =========================== ===============
    instid0(<*ID name*>)     A symbolic *ID0* value.     instid0(NO_DEP)
    instskip(<*SKIP name*>)  A symbolic *SKIP* value.    instskip(SAME)
    instid1(<*ID name*>)     A symbolic *ID1* value.     instid1(NO_DEP)
    ======================== =========================== ===============

These values may be specified in any order.
When more than one value is specified, the values must be separated from each other by a '|'.

Valid *ID names* are defined below.

    =================== ===================================================================
    Name                Description
    =================== ===================================================================
    NO_DEP              No dependency on any prior instruction. This is the default value.
    VALU_DEP_1          Dependency on a previous VALU instruction, 1 opcode back.
    VALU_DEP_2          Dependency on a previous VALU instruction, 2 opcodes back.
    VALU_DEP_3          Dependency on a previous VALU instruction, 3 opcodes back.
    VALU_DEP_4          Dependency on a previous VALU instruction, 4 opcodes back.
    TRANS32_DEP_1       Dependency on a previous TRANS32 instruction, 1 opcode back.
    TRANS32_DEP_2       Dependency on a previous TRANS32 instruction, 2 opcodes back.
    TRANS32_DEP_3       Dependency on a previous TRANS32 instruction, 3 opcodes back.
    FMA_ACCUM_CYCLE_1   Single cycle penalty for FMA accumulation.
    SALU_CYCLE_1        1 cycle penalty for a prior SALU instruction.
    SALU_CYCLE_2        2 cycle penalty for a prior SALU instruction.
    SALU_CYCLE_3        3 cycle penalty for a prior SALU instruction.
    =================== ===================================================================

Legal *SKIP names* are described in the following table.

    ======== ============================================================================
    Name     Description
    ======== ============================================================================
    SAME     Apply second dependency to the same instruction. This is the default value.
    NEXT     Apply second dependency to the next instruction.
    SKIP_1   Skip 1 instruction then apply dependency.
    SKIP_2   Skip 2 instructions then apply dependency.
    SKIP_3   Skip 3 instructions then apply dependency.
    SKIP_4   Skip 4 instructions then apply dependency.
    ======== ============================================================================

Examples:

.. parsed-literal::

    s_delay_alu instid0(VALU_DEP_1)
    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
