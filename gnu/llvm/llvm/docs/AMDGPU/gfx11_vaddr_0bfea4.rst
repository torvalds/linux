..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx11_vaddr_0bfea4:

vaddr
=====

Image address which includes from one to four dimensional coordinates and other data used to locate a position in the image.

This operand may be specified using either :ref:`standard VGPR syntax<amdgpu_synid_v>` or special :ref:`NSA VGPR syntax<amdgpu_synid_nsa>`.

*Size:* 8-12 dwords. Actual size depends on opcode and :ref:`a16<amdgpu_synid_a16>`.

 This instruction expects NSA address to be partitioned into 5 groups; registers within each group must be contiguous.

  Examples:

  .. parsed-literal::

    image_bvh_intersect_ray   v[4:7], v[9:16], s[4:7]
    image_bvh64_intersect_ray v[5:8], v[1:12], s[8:11]
    image_bvh_intersect_ray   v[39:42], [v50, v46, v[20:22], v[40:42], v[47:49]], s[12:15]
    image_bvh64_intersect_ray v[39:42], [v[50:51], v46, v[20:22], v[40:42], v[47:49]], s[12:15]

*Operands:* :ref:`v<amdgpu_synid_v>`
