..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx940_waitcnt:

waitcnt
=======

Counts of outstanding instructions to wait for.

The bits of this operand have the following meaning:

    ========== ========= ================================================ ============
    High Bits  Low Bits  Description                                      Value Range
    ========== ========= ================================================ ============
    15:14      3:0       VM_CNT: vector memory operations count.          0..63
    \-         6:4       EXP_CNT: export count.                           0..7
    \-         11:8      LGKM_CNT: LDS, GDS, Constant and Message count.  0..15
    ========== ========= ================================================ ============

This operand may be specified as one of the following:

* An :ref:`integer_number<amdgpu_synid_integer_number>` or an :ref:`absolute_expression<amdgpu_synid_absolute_expression>`. The value must be in the range from 0 to 0xFFFF.
* A combination of *vmcnt*, *expcnt*, *lgkmcnt* and other values described below.

    ====================== ======================================================================
    Syntax                 Description
    ====================== ======================================================================
    vmcnt(<*N*>)           A VM_CNT value. *N* must not exceed the largest VM_CNT value.
    expcnt(<*N*>)          An EXP_CNT value. *N* must not exceed the largest EXP_CNT value.
    lgkmcnt(<*N*>)         An LGKM_CNT value. *N* must not exceed the largest LGKM_CNT value.
    vmcnt_sat(<*N*>)       A VM_CNT value computed as min(*N*, the largest VM_CNT value).
    expcnt_sat(<*N*>)      An EXP_CNT value computed as min(*N*, the largest EXP_CNT value).
    lgkmcnt_sat(<*N*>)     An LGKM_CNT value computed as min(*N*, the largest LGKM_CNT value).
    ====================== ======================================================================

These values may be specified in any order. Spaces, ampersands, and commas may be used as optional separators.
If some values are omitted, the corresponding fields will default to their maximum value.

*N* is either an
:ref:`integer number<amdgpu_synid_integer_number>` or an
:ref:`absolute expression<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

    vm_cnt = 1
    exp_cnt = 2
    lgkm_cnt = 3
    cnt = vm_cnt | (exp_cnt << 4) | (lgkm_cnt << 8)

    s_waitcnt cnt
    s_waitcnt 1 | (2 << 4) | (3 << 8)                          // the same as above
    s_waitcnt vmcnt(1) expcnt(2) lgkmcnt(3)                    // the same as above
    s_waitcnt vmcnt(vm_cnt) expcnt(exp_cnt) lgkmcnt(lgkm_cnt)  // the same as above

    s_waitcnt vmcnt(1)
    s_waitcnt expcnt(2) lgkmcnt(3)
    s_waitcnt vmcnt(1), expcnt(2), lgkmcnt(3)
    s_waitcnt vmcnt(1) & lgkmcnt_sat(100) & expcnt(2)
