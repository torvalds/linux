..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

====================================================================================
Syntax of gfx906 Instructions
====================================================================================

.. contents::
  :local:

Introduction
============

This document describes the syntax of *instructions specific to gfx906*.

For a description of other gfx906 instructions see :doc:`Syntax of Core GFX9 Instructions<AMDGPUAsmGFX9>`.

Notation
========

Notation used in this document is explained :ref:`here<amdgpu_syn_instruction_notation>`.

Overview
========

An overview of generic syntax and other features of AMDGPU instructions may be found :ref:`in this document<amdgpu_syn_instructions>`.

Instructions
============


VOP2
-----------------------

.. parsed-literal::

    **INSTRUCTION**                    **DST**       **SRC0**      **SRC1**       **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    v_fmac_f32                     :ref:`vdst<amdgpu_synid_gfx906_vdst>`,     :ref:`src0<amdgpu_synid_gfx906_src>`,     :ref:`vsrc1<amdgpu_synid_gfx906_vsrc>`
    v_fmac_f32_dpp                 :ref:`vdst<amdgpu_synid_gfx906_vdst>`,     :ref:`vsrc0<amdgpu_synid_gfx906_vsrc>`::ref:`m<amdgpu_synid_gfx906_m>`,  :ref:`vsrc1<amdgpu_synid_gfx906_vsrc>`::ref:`m<amdgpu_synid_gfx906_m>`    :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_xnor_b32                     :ref:`vdst<amdgpu_synid_gfx906_vdst>`,     :ref:`src0<amdgpu_synid_gfx906_src>`,     :ref:`vsrc1<amdgpu_synid_gfx906_vsrc>`
    v_xnor_b32_dpp                 :ref:`vdst<amdgpu_synid_gfx906_vdst>`,     :ref:`vsrc0<amdgpu_synid_gfx906_vsrc>`,    :ref:`vsrc1<amdgpu_synid_gfx906_vsrc>`      :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_xnor_b32_sdwa                :ref:`vdst<amdgpu_synid_gfx906_vdst>`,     :ref:`src0<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m_1>`,   :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m_1>`     :ref:`dst_sel<amdgpu_synid_dst_sel>` :ref:`dst_unused<amdgpu_synid_dst_unused>` :ref:`src0_sel<amdgpu_synid_src0_sel>` :ref:`src1_sel<amdgpu_synid_src1_sel>`

VOP3
-----------------------

.. parsed-literal::

    **INSTRUCTION**                    **DST**       **SRC0**      **SRC1**           **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    v_fmac_f32_e64                 :ref:`vdst<amdgpu_synid_gfx906_vdst>`,     :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`m<amdgpu_synid_gfx906_m>`,   :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m>`         :ref:`clamp<amdgpu_synid_clamp>` :ref:`omod<amdgpu_synid_omod>`
    v_xnor_b32_e64                 :ref:`vdst<amdgpu_synid_gfx906_vdst>`,     :ref:`src0<amdgpu_synid_gfx906_src_2>`,     :ref:`src1<amdgpu_synid_gfx906_src_1>`

VOP3P
-----------------------

.. parsed-literal::

    **INSTRUCTION**            **DST**      **SRC0**        **SRC1**        **SRC2**           **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    v_dot2_f32_f16         :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`f16x2<amdgpu_synid_gfx906_type_deviation>`, :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`f16x2<amdgpu_synid_gfx906_type_deviation>`, :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`f32<amdgpu_synid_gfx906_type_deviation>`       :ref:`neg_lo<amdgpu_synid_neg_lo>` :ref:`neg_hi<amdgpu_synid_neg_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_dot2_i32_i16         :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_3>`::ref:`i16x2<amdgpu_synid_gfx906_type_deviation>`, :ref:`src1<amdgpu_synid_gfx906_src_4>`::ref:`i16x2<amdgpu_synid_gfx906_type_deviation>`, :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`i32<amdgpu_synid_gfx906_type_deviation>`       :ref:`clamp<amdgpu_synid_clamp>`
    v_dot2_u32_u16         :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_3>`::ref:`u16x2<amdgpu_synid_gfx906_type_deviation>`, :ref:`src1<amdgpu_synid_gfx906_src_4>`::ref:`u16x2<amdgpu_synid_gfx906_type_deviation>`, :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`u32<amdgpu_synid_gfx906_type_deviation>`       :ref:`clamp<amdgpu_synid_clamp>`
    v_dot4_i32_i8          :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`i8x4<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`i8x4<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`i32<amdgpu_synid_gfx906_type_deviation>`       :ref:`clamp<amdgpu_synid_clamp>`
    v_dot4_u32_u8          :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`u8x4<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`u8x4<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`u32<amdgpu_synid_gfx906_type_deviation>`       :ref:`clamp<amdgpu_synid_clamp>`
    v_dot8_i32_i4          :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`i4x8<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`i4x8<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`i32<amdgpu_synid_gfx906_type_deviation>`       :ref:`clamp<amdgpu_synid_clamp>`
    v_dot8_u32_u4          :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`u4x8<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`u4x8<amdgpu_synid_gfx906_type_deviation>`,  :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`u32<amdgpu_synid_gfx906_type_deviation>`       :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mix_f32          :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`,  :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`,  :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`      :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mixhi_f16        :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`,  :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`,  :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`      :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mixlo_f16        :ref:`vdst<amdgpu_synid_gfx906_vdst>`,    :ref:`src0<amdgpu_synid_gfx906_src_2>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`,  :ref:`src1<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`,  :ref:`src2<amdgpu_synid_gfx906_src_1>`::ref:`m<amdgpu_synid_gfx906_m>`::ref:`fx<amdgpu_synid_gfx906_fx_operand>`      :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`

.. |---| unicode:: U+02014 .. em dash

.. toctree::
    :hidden:

    gfx906_fx_operand
    gfx906_m
    gfx906_m_1
    gfx906_src
    gfx906_src_1
    gfx906_src_2
    gfx906_src_3
    gfx906_src_4
    gfx906_type_deviation
    gfx906_vdst
    gfx906_vsrc
