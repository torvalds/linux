..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx1013_vaddr_c5ab43:

vaddr
=====

Image address which includes from one to four dimensional coordinates and other data used to locate a position in the image.

This operand may be specified using either :ref:`standard VGPR syntax<amdgpu_synid_v>` or special :ref:`NSA VGPR syntax<amdgpu_synid_nsa>`.

*Size:* 8-12 dwords. Actual size depends on opcode and :ref:`a16<amdgpu_synid_a16>`.



  Examples:

  .. parsed-literal::

    image_bvh_intersect_ray   v[4:7], v[9:16], s[4:7]
    image_bvh64_intersect_ray v[5:8], v[1:12], s[8:11]
    image_bvh_intersect_ray   v[39:42], [v5, v4, v2, v1, v7, v3, v0, v6], s[12:15] a16
    image_bvh64_intersect_ray v[39:42], [v50, v46, v23, v17, v16, v15, v21, v20, v19, v37, v40, v42], s[12:15]

*Operands:* :ref:`v<amdgpu_synid_v>`
