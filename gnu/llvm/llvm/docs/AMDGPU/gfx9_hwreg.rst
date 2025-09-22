..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx9_hwreg:

hwreg
=====

Bits of a hardware register being accessed.

The bits of this operand have the following meaning:

    ======= ===================== ============
    Bits    Description           Value Range
    ======= ===================== ============
    5:0     Register *id*.        0..63
    10:6    First bit *offset*.   0..31
    15:11   *Size* in bits.       1..32
    ======= ===================== ============

This operand may be specified as one of the following:

* An :ref:`integer_number<amdgpu_synid_integer_number>` or an :ref:`absolute_expression<amdgpu_synid_absolute_expression>`. The value must be in the range from 0 to 0xFFFF.
* An *hwreg* value which is described below.

    ==================================== ===============================================================================
    Hwreg Value Syntax                   Description
    ==================================== ===============================================================================
    hwreg({0..63})                       All bits of a register indicated by the register *id*.
    hwreg(<*name*>)                      All bits of a register indicated by the register *name*.
    hwreg({0..63}, {0..31}, {1..32})     Register bits indicated by the register *id*, first bit *offset* and *size*.
    hwreg(<*name*>, {0..31}, {1..32})    Register bits indicated by the register *name*, first bit *offset* and *size*.
    ==================================== ===============================================================================

Numeric values may be specified as positive :ref:`integer numbers<amdgpu_synid_integer_number>`
or :ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Predefined register *names* include:

    ============================== ==========================================
    Name                           Description
    ============================== ==========================================
    HW_REG_MODE                    Shader writable mode bits.
    HW_REG_STATUS                  Shader read-only status.
    HW_REG_TRAPSTS                 Trap status.
    HW_REG_HW_ID                   Id of wave, simd, compute unit, etc.
    HW_REG_GPR_ALLOC               Per-wave SGPR and VGPR allocation.
    HW_REG_LDS_ALLOC               Per-wave LDS allocation.
    HW_REG_IB_STS                  Counters of outstanding instructions.
    HW_REG_SH_MEM_BASES            Memory aperture.
    HW_REG_TBA_LO                  tba_lo register.
    HW_REG_TBA_HI                  tba_hi register.
    HW_REG_TMA_LO                  tma_lo register.
    HW_REG_TMA_HI                  tma_hi register.
    ============================== ==========================================

Examples:

.. parsed-literal::

    reg = 1
    offset = 2
    size = 4
    hwreg_enc = reg | (offset << 6) | ((size - 1) << 11)

    s_getreg_b32 s2, 0x1881
    s_getreg_b32 s2, hwreg_enc                     // the same as above
    s_getreg_b32 s2, hwreg(1, 2, 4)                // the same as above
    s_getreg_b32 s2, hwreg(reg, offset, size)      // the same as above

    s_getreg_b32 s2, hwreg(15)
    s_getreg_b32 s2, hwreg(51, 1, 31)
    s_getreg_b32 s2, hwreg(HW_REG_LDS_ALLOC, 0, 1)
