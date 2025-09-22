..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

====================================================================================
Syntax of gfx904 Instructions
====================================================================================

.. contents::
  :local:

Introduction
============

This document describes the syntax of *instructions specific to gfx904*.

For a description of other gfx904 instructions see :doc:`Syntax of Core GFX9 Instructions<AMDGPUAsmGFX9>`.

Notation
========

Notation used in this document is explained :ref:`here<amdgpu_syn_instruction_notation>`.

Overview
========

An overview of generic syntax and other features of AMDGPU instructions may be found :ref:`in this document<amdgpu_syn_instructions>`.

Instructions
============


VOP3P
-----------------------

.. parsed-literal::

    **INSTRUCTION**                    **DST**       **SRC0**       **SRC1**       **SRC2**       **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    v_fma_mix_f32                  :ref:`vdst<amdgpu_synid_gfx904_vdst>`,     :ref:`src0<amdgpu_synid_gfx904_src>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`, :ref:`src1<amdgpu_synid_gfx904_src_1>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`, :ref:`src2<amdgpu_synid_gfx904_src_1>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`  :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mixhi_f16                :ref:`vdst<amdgpu_synid_gfx904_vdst>`,     :ref:`src0<amdgpu_synid_gfx904_src>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`, :ref:`src1<amdgpu_synid_gfx904_src_1>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`, :ref:`src2<amdgpu_synid_gfx904_src_1>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`  :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mixlo_f16                :ref:`vdst<amdgpu_synid_gfx904_vdst>`,     :ref:`src0<amdgpu_synid_gfx904_src>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`, :ref:`src1<amdgpu_synid_gfx904_src_1>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`, :ref:`src2<amdgpu_synid_gfx904_src_1>`::ref:`m<amdgpu_synid_gfx904_m>`::ref:`fx<amdgpu_synid_gfx904_fx_operand>`  :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`

.. |---| unicode:: U+02014 .. em dash

.. toctree::
    :hidden:

    gfx904_fx_operand
    gfx904_m
    gfx904_src
    gfx904_src_1
    gfx904_vdst
