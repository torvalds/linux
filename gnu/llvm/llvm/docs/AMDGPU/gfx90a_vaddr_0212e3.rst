..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx90a_vaddr_0212e3:

vaddr
=====

A 64-bit flat global address or a 32-bit offset depending on addressing mode:

* Address = :ref:`vaddr<amdgpu_synid_gfx90a_vaddr_0212e3>` + :ref:`offset13s<amdgpu_synid_flat_offset13s>`. :ref:`vaddr<amdgpu_synid_gfx90a_vaddr_0212e3>` is a 64-bit address. This mode is indicated by :ref:`saddr<amdgpu_synid_gfx90a_saddr_a37373>` set to :ref:`off<amdgpu_synid_off>`.
* Address = :ref:`saddr<amdgpu_synid_gfx90a_saddr_a37373>` + :ref:`vaddr<amdgpu_synid_gfx90a_vaddr_0212e3>` + :ref:`offset13s<amdgpu_synid_flat_offset13s>`. :ref:`vaddr<amdgpu_synid_gfx90a_vaddr_0212e3>` is a 32-bit offset. This mode is used when :ref:`saddr<amdgpu_synid_gfx90a_saddr_a37373>` is not :ref:`off<amdgpu_synid_off>`.

*Size:* 1 or 2 dwords.

*Operands:* :ref:`v<amdgpu_synid_v>`
