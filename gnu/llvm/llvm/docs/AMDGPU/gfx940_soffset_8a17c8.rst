..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx940_soffset_8a17c8:

soffset
=======

An offset from the base address.

* If offset is specified as a register, it supplies an unsigned byte offset.
* If offset is specified as a 21-bit immediate, it supplies a signed byte offset.

Note that an *immediate* offset may be specified using either :ref:`simm21<amdgpu_synid_simm21>` operand or :ref:`offset21s<amdgpu_synid_smem_offset21s>` modifier, but not both.

*Size:* 1 dword.

*Operands:* :ref:`s<amdgpu_synid_s>`, :ref:`flat_scratch<amdgpu_synid_flat_scratch>`, :ref:`xnack_mask<amdgpu_synid_xnack_mask>`, :ref:`vcc<amdgpu_synid_vcc>`, :ref:`ttmp<amdgpu_synid_ttmp>`, :ref:`m0<amdgpu_synid_m0>`, :ref:`simm21<amdgpu_synid_simm21>`
