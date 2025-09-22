..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx11_waitcnt_depctr:

waitcnt_depctr
==============

Dependency counters to wait for.

This operand may be specified as one of the following:

* An :ref:`integer_number<amdgpu_synid_integer_number>` or an :ref:`absolute_expression<amdgpu_synid_absolute_expression>`. The value must be in the range from -32768 to 65535.
* A combination of *symbolic values* described below.

    ======================== ======================== ================ =================
    Syntax                   Description              Valid *N* Values Default *N* Value
    ======================== ======================== ================ =================
    depctr_hold_cnt(<*N*>)   Wait for HOLD_CNT <= N      0..1                1
    depctr_sa_sdst(<*N*>)    Wait for SA_SDST <= N       0..1                1
    depctr_va_vdst(<*N*>)    Wait for VA_VDST <= N       0..15              15
    depctr_va_sdst(<*N*>)    Wait for VA_SDST <= N       0..7                7
    depctr_va_ssrc(<*N*>)    Wait for VA_SSRC <= N       0..1                1
    depctr_va_vcc(<*N*>)     Wait for VA_VCC <= N        0..1                1
    depctr_vm_vsrc(<*N*>)    Wait for VM_VSRC <= N       0..7                7
    ======================== ======================== ================ =================

    These values may be specified in any order. Spaces, ampersands, and commas may be used as optional separators.

Examples:

.. parsed-literal::

    s_waitcnt_depctr depctr_sa_sdst(0) depctr_va_vdst(0)
    s_waitcnt_depctr depctr_sa_sdst(1) & depctr_va_vdst(1)
    s_waitcnt_depctr depctr_va_vdst(3), depctr_va_sdst(5)
