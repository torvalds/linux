..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx940_vaddr_b73dc0:

vaddr
=====

This is an optional operand which may specify offset and/or index.

*Size:* 0, 1 or 2 dwords. Size is controlled by modifiers :ref:`offen<amdgpu_synid_offen>` and :ref:`idxen<amdgpu_synid_idxen>`:

* If only :ref:`idxen<amdgpu_synid_idxen>` is specified, this operand supplies an index. Size is 1 dword.
* If only :ref:`offen<amdgpu_synid_offen>` is specified, this operand supplies an offset. Size is 1 dword.
* If both modifiers are specified, index is in the first register and offset is in the second. Size is 2 dwords.
* If none of these modifiers are specified, this operand must be set to :ref:`off<amdgpu_synid_off>`.

*Operands:* :ref:`v<amdgpu_synid_v>`, :ref:`off<amdgpu_synid_off>`
