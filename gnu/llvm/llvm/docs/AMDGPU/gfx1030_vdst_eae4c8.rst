..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx1030_vdst_eae4c8:

vdst
====

Image data to be loaded by an image instruction.

*Size:* depends on :ref:`dmask<amdgpu_synid_dmask>`, :ref:`tfe<amdgpu_synid_tfe>` and :ref:`d16<amdgpu_synid_d16>`:

* :ref:`dmask<amdgpu_synid_dmask>` may specify from 1 to 4 data elements. Each data element occupies either 32 bits or 16 bits, depending on :ref:`d16<amdgpu_synid_d16>`.
* :ref:`d16<amdgpu_synid_d16>` specifies that data elements in registers are packed; each value occupies 16 bits.
* :ref:`tfe<amdgpu_synid_tfe>` adds 1 dword if specified.

*Operands:* :ref:`v<amdgpu_synid_v>`
