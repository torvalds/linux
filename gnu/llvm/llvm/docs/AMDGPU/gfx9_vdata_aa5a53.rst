..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx9_vdata_aa5a53:

vdata
=====

Input data for an atomic instruction.

Optionally, this operand may be used to store output data:

* If :ref:`glc<amdgpu_synid_glc>` is specified, gets the memory value before the operation.

*Size:* depends on :ref:`dmask<amdgpu_synid_dmask>`:

* :ref:`dmask<amdgpu_synid_dmask>` may specify 1 data element for 32-bit-per-pixel surfaces or 2 data elements for 64-bit-per-pixel surfaces. Each data element occupies 1 dword.


  Note: the surface data format is indicated in the image resource constant, but not in the instruction.

*Operands:* :ref:`v<amdgpu_synid_v>`
