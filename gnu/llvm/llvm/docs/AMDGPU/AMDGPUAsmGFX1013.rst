..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

====================================================================================
Syntax of gfx1013 Instructions
====================================================================================

.. contents::
  :local:

Introduction
============

This document describes the syntax of *instructions specific to gfx1013*.

For a description of other gfx1013 instructions, see :doc:`Syntax of GFX10 RDNA1 Instructions<AMDGPUAsmGFX10>`.

Notation
========

Notation used in this document is explained :ref:`here<amdgpu_syn_instruction_notation>`.

Overview
========

An overview of generic syntax and other features of AMDGPU instructions may be found :ref:`in this document<amdgpu_syn_instructions>`.

Instructions
============


MIMG
----

.. parsed-literal::

    **INSTRUCTION**                   **DST**      **SRC0**     **SRC1**       **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    image_bvh64_intersect_ray     :ref:`vdst<amdgpu_synid_gfx1013_vdst_9041ac>`,    :ref:`vaddr<amdgpu_synid_gfx1013_vaddr_c5ab43>`,   :ref:`srsrc<amdgpu_synid_gfx1013_srsrc_5dafbc>`      :ref:`a16<amdgpu_synid_a16>`
    image_bvh_intersect_ray       :ref:`vdst<amdgpu_synid_gfx1013_vdst_9041ac>`,    :ref:`vaddr<amdgpu_synid_gfx1013_vaddr_c5ab43>`,   :ref:`srsrc<amdgpu_synid_gfx1013_srsrc_5dafbc>`      :ref:`a16<amdgpu_synid_a16>`
    image_msaa_load               :ref:`vdst<amdgpu_synid_gfx1013_vdst_eae4c8>`,    :ref:`vaddr<amdgpu_synid_gfx1013_vaddr_a5639c>`,   :ref:`srsrc<amdgpu_synid_gfx1013_srsrc_cf7132>`      :ref:`dmask<amdgpu_synid_dmask>` :ref:`dim<amdgpu_synid_dim>` :ref:`unorm<amdgpu_synid_unorm>` :ref:`glc<amdgpu_synid_glc>` :ref:`slc<amdgpu_synid_slc>` :ref:`dlc<amdgpu_synid_dlc>` :ref:`a16<amdgpu_synid_a16>` :ref:`tfe<amdgpu_synid_tfe>` :ref:`lwe<amdgpu_synid_lwe>` :ref:`d16<amdgpu_synid_d16>`

.. |---| unicode:: U+02014 .. em dash

.. toctree::
    :hidden:

    gfx1013_srsrc_5dafbc
    gfx1013_srsrc_cf7132
    gfx1013_vaddr_a5639c
    gfx1013_vaddr_c5ab43
    gfx1013_vdst_9041ac
    gfx1013_vdst_eae4c8
