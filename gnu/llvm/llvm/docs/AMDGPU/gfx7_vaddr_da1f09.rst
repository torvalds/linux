..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx7_vaddr_da1f09:

vaddr
=====

This is an optional operand which may specify a 64-bit address, offset and/or index.

*Size:* 0, 1 or 2 dwords. Size is controlled by modifiers :ref:`addr64<amdgpu_synid_addr64>`, :ref:`offen<amdgpu_synid_offen>` and :ref:`idxen<amdgpu_synid_idxen>`:

* If only :ref:`addr64<amdgpu_synid_addr64>` is specified, this operand supplies a 64-bit address. Size is 2 dwords.
* If only :ref:`idxen<amdgpu_synid_idxen>` is specified, this operand supplies an index. Size is 1 dword.
* If only :ref:`offen<amdgpu_synid_offen>` is specified, this operand supplies an offset. Size is 1 dword.
* If both :ref:`idxen<amdgpu_synid_idxen>` and :ref:`offen<amdgpu_synid_offen>` are specified, index is in the first register and offset is in the second. Size is 2 dwords.
* If none of these modifiers are specified, this operand must be set to :ref:`off<amdgpu_synid_off>`.
* All other combinations of these modifiers are illegal.

*Operands:* :ref:`v<amdgpu_synid_v>`, :ref:`off<amdgpu_synid_off>`
