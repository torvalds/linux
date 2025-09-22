..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx11_saddr_844ded:

saddr
=====

A 64-bit flat global address. Must be specified as :ref:`off<amdgpu_synid_off>` if not used.

The final memory address is computed as follows:

* Address = [:ref:`saddr<amdgpu_synid_gfx11_saddr_844ded>`] + :ref:`offset13s<amdgpu_synid_flat_offset13s>` + ThreadID * 4.

*Size:* 2 dwords.

*Operands:* :ref:`s<amdgpu_synid_s>`, :ref:`vcc<amdgpu_synid_vcc>`, :ref:`ttmp<amdgpu_synid_ttmp>`, :ref:`null<amdgpu_synid_null>`, :ref:`off<amdgpu_synid_off>`
