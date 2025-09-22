..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx90a_probe:

probe
=====

A bit mask which indicates request permissions.

This operand must be specified as an :ref:`integer_number<amdgpu_synid_integer_number>` or an :ref:`absolute_expression<amdgpu_synid_absolute_expression>`.
The value is truncated to 7 bits, but only 3 low bits are significant.

    ============ ==============================
    Bit Number   Description
    ============ ==============================
    0            Request *read* permission.
    1            Request *write* permission.
    2            Request *execute* permission.
    ============ ==============================
