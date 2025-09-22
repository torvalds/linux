..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

====================================================================================
Syntax of gfx908 Instructions
====================================================================================

.. contents::
  :local:

Introduction
============

This document describes the syntax of *instructions specific to gfx908*.

For a description of other gfx908 instructions, see :doc:`Syntax of Core GFX9 Instructions<AMDGPUAsmGFX9>`.

Notation
========

Notation used in this document is explained :ref:`here<amdgpu_syn_instruction_notation>`.

Overview
========

An overview of generic syntax and other features of AMDGPU instructions may be found :ref:`in this document<amdgpu_syn_instructions>`.

Instructions
============


FLAT
----

.. parsed-literal::

    **INSTRUCTION**                    **SRC0**      **SRC1**      **SRC2**           **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    global_atomic_add_f32          :ref:`vaddr<amdgpu_synid_gfx908_vaddr_0212e3>`,    :ref:`vdata<amdgpu_synid_gfx908_vdata_6802ce>`,    :ref:`saddr<amdgpu_synid_gfx908_saddr>`          :ref:`offset13s<amdgpu_synid_flat_offset13s>` :ref:`slc<amdgpu_synid_slc>`
    global_atomic_pk_add_f16       :ref:`vaddr<amdgpu_synid_gfx908_vaddr_0212e3>`,    :ref:`vdata<amdgpu_synid_gfx908_vdata_6802ce>`,    :ref:`saddr<amdgpu_synid_gfx908_saddr>`          :ref:`offset13s<amdgpu_synid_flat_offset13s>` :ref:`slc<amdgpu_synid_slc>`

MUBUF
-----

.. parsed-literal::

    **INSTRUCTION**                    **SRC0**      **SRC1**      **SRC2**      **SRC3**           **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    buffer_atomic_add_f32          :ref:`vdata<amdgpu_synid_gfx908_vdata_fe1edf>`,    :ref:`vaddr<amdgpu_synid_gfx908_vaddr_b73dc0>`,    :ref:`srsrc<amdgpu_synid_gfx908_srsrc>`,    :ref:`soffset<amdgpu_synid_gfx908_soffset>`        :ref:`idxen<amdgpu_synid_idxen>` :ref:`offen<amdgpu_synid_offen>` :ref:`offset12<amdgpu_synid_buf_offset12>` :ref:`slc<amdgpu_synid_slc>`
    buffer_atomic_pk_add_f16       :ref:`vdata<amdgpu_synid_gfx908_vdata_fe1edf>`,    :ref:`vaddr<amdgpu_synid_gfx908_vaddr_b73dc0>`,    :ref:`srsrc<amdgpu_synid_gfx908_srsrc>`,    :ref:`soffset<amdgpu_synid_gfx908_soffset>`        :ref:`idxen<amdgpu_synid_idxen>` :ref:`offen<amdgpu_synid_offen>` :ref:`offset12<amdgpu_synid_buf_offset12>` :ref:`slc<amdgpu_synid_slc>`

VOP2
----

.. parsed-literal::

    **INSTRUCTION**            **DST**      **SRC0**           **SRC1**           **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    v_dot2c_f32_f16        :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_73ab34>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`
    v_dot2c_f32_f16_dpp    :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`  :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_dot2c_i32_i16        :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_73ab34>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`
    v_dot2c_i32_i16_dpp    :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`    :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_dot4c_i32_i8         :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_73ab34>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,     :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`
    v_dot4c_i32_i8_dpp     :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`     :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_dot8c_i32_i4         :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_73ab34>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`,     :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`
    v_dot8c_i32_i4_dpp     :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`     :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_fmac_f32             :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_73ab34>`,          :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`
    v_fmac_f32_dpp         :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`,       :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`        :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_pk_fmac_f16          :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_73ab34>`,          :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`
    v_xnor_b32             :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_73ab34>`,          :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`
    v_xnor_b32_dpp         :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_6802ce>`,         :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_6802ce>`          :ref:`dpp_ctrl<amdgpu_synid_dpp_ctrl>` :ref:`row_mask<amdgpu_synid_row_mask>` :ref:`bank_mask<amdgpu_synid_bank_mask>` :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`
    v_xnor_b32_sdwa        :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,    :ref:`src0<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_28b494>`,        :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_28b494>`         :ref:`dst_sel<amdgpu_synid_dst_sel>` :ref:`dst_unused<amdgpu_synid_dst_unused>` :ref:`src0_sel<amdgpu_synid_src0_sel>` :ref:`src1_sel<amdgpu_synid_src1_sel>`

VOP3
----

.. parsed-literal::

    **INSTRUCTION**                    **DST**       **SRC0**          **SRC1**              **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    v_dot2c_f32_f16_e64            :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,     :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`      :ref:`clamp<amdgpu_synid_clamp>` :ref:`omod<amdgpu_synid_omod>`
    v_dot2c_i32_i16_e64            :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,     :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`        :ref:`clamp<amdgpu_synid_clamp>`
    v_dot4c_i32_i8_e64             :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,     :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`         :ref:`clamp<amdgpu_synid_clamp>`
    v_dot8c_i32_i4_e64             :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,     :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`         :ref:`clamp<amdgpu_synid_clamp>`
    v_fmac_f32_e64                 :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,     :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`,       :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`            :ref:`clamp<amdgpu_synid_clamp>` :ref:`omod<amdgpu_synid_omod>`
    v_xnor_b32_e64                 :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,     :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`,         :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`

VOP3P
-----

.. parsed-literal::

    **INSTRUCTION**             **DST**          **SRC0**          **SRC1**          **SRC2**          **MODIFIERS**
    \ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|\ |---|
    v_accvgpr_read_b32      :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`vsrc<amdgpu_synid_gfx908_vsrc_1027ca>`
    v_accvgpr_write_b32     :ref:`vdst<amdgpu_synid_gfx908_vdst_78dd0a>`,        :ref:`src<amdgpu_synid_gfx908_src_7c8695>`
    v_dot2_f32_f16          :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`f16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`      :ref:`neg_lo<amdgpu_synid_neg_lo>` :ref:`neg_hi<amdgpu_synid_neg_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_dot2_i32_i16          :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_955b45>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`src1<amdgpu_synid_gfx908_src_d95796>`::ref:`i16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`i32<amdgpu_synid_gfx908_type_deviation>`      :ref:`clamp<amdgpu_synid_clamp>`
    v_dot2_u32_u16          :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_955b45>`::ref:`u16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`src1<amdgpu_synid_gfx908_src_d95796>`::ref:`u16x2<amdgpu_synid_gfx908_type_deviation>`,   :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`u32<amdgpu_synid_gfx908_type_deviation>`      :ref:`clamp<amdgpu_synid_clamp>`
    v_dot4_i32_i8           :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`i32<amdgpu_synid_gfx908_type_deviation>`      :ref:`clamp<amdgpu_synid_clamp>`
    v_dot4_u32_u8           :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`u8x4<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`u8x4<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`u32<amdgpu_synid_gfx908_type_deviation>`      :ref:`clamp<amdgpu_synid_clamp>`
    v_dot8_i32_i4           :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`i4x8<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`i32<amdgpu_synid_gfx908_type_deviation>`      :ref:`clamp<amdgpu_synid_clamp>`
    v_dot8_u32_u4           :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`u4x8<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`u4x8<amdgpu_synid_gfx908_type_deviation>`,    :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`u32<amdgpu_synid_gfx908_type_deviation>`      :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mix_f32           :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`,    :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`     :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mixhi_f16         :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`,    :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`     :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_fma_mixlo_f16         :ref:`vdst<amdgpu_synid_gfx908_vdst_89680f>`,        :ref:`src0<amdgpu_synid_gfx908_src_4e78e6>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`,    :ref:`src1<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`,    :ref:`src2<amdgpu_synid_gfx908_src_d578c4>`::ref:`m<amdgpu_synid_gfx908_m_c141fc>`::ref:`fx<amdgpu_synid_gfx908_fx_operand>`     :ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>` :ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>` :ref:`clamp<amdgpu_synid_clamp>`
    v_mfma_f32_16x16x16f16  :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_16x16x1f32   :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_16x16x2bf16  :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_16x16x4f16   :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_16x16x4f32   :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_16x16x8bf16  :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_32x32x1f32   :ref:`vdst<amdgpu_synid_gfx908_vdst_2c8d1e>`::ref:`f32x32<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_036abe>`::ref:`f32x32<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_32x32x2bf16  :ref:`vdst<amdgpu_synid_gfx908_vdst_2c8d1e>`::ref:`f32x32<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_036abe>`::ref:`f32x32<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_32x32x2f32   :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_32x32x4bf16  :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_32x32x4f16   :ref:`vdst<amdgpu_synid_gfx908_vdst_2c8d1e>`::ref:`f32x32<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_036abe>`::ref:`f32x32<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_32x32x8f16   :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`f32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_4x4x1f32     :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`f32<amdgpu_synid_gfx908_type_deviation>`,    :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_4x4x2bf16    :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`bf16x2<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_f32_4x4x4f16     :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_9ad749>`::ref:`f16x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`f32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_i32_16x16x16i8   :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`i32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`i32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_i32_16x16x4i8    :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`i32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`i32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_i32_32x32x4i8    :ref:`vdst<amdgpu_synid_gfx908_vdst_2c8d1e>`::ref:`i32x32<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_036abe>`::ref:`i32x32<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_i32_32x32x8i8    :ref:`vdst<amdgpu_synid_gfx908_vdst_0c4ef8>`::ref:`i32x16<amdgpu_synid_gfx908_type_deviation>`, :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_2d4632>`::ref:`i32x16<amdgpu_synid_gfx908_type_deviation>`  :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`
    v_mfma_i32_4x4x4i8      :ref:`vdst<amdgpu_synid_gfx908_vdst_bcee7a>`::ref:`i32x4<amdgpu_synid_gfx908_type_deviation>`,  :ref:`vsrc0<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc1<amdgpu_synid_gfx908_vsrc_be4895>`::ref:`i8x4<amdgpu_synid_gfx908_type_deviation>`,   :ref:`vsrc2<amdgpu_synid_gfx908_vsrc_f3d248>`::ref:`i32x4<amdgpu_synid_gfx908_type_deviation>`   :ref:`cbsz<amdgpu_synid_cbsz>` :ref:`abid<amdgpu_synid_abid>` :ref:`blgp<amdgpu_synid_blgp>`

.. |---| unicode:: U+02014 .. em dash

.. toctree::
    :hidden:

    gfx908_fx_operand
    gfx908_m_28b494
    gfx908_m_c141fc
    gfx908_saddr
    gfx908_soffset
    gfx908_src_4e78e6
    gfx908_src_73ab34
    gfx908_src_7c8695
    gfx908_src_955b45
    gfx908_src_d578c4
    gfx908_src_d95796
    gfx908_srsrc
    gfx908_type_deviation
    gfx908_vaddr_0212e3
    gfx908_vaddr_b73dc0
    gfx908_vdata_6802ce
    gfx908_vdata_fe1edf
    gfx908_vdst_0c4ef8
    gfx908_vdst_2c8d1e
    gfx908_vdst_78dd0a
    gfx908_vdst_89680f
    gfx908_vdst_bcee7a
    gfx908_vsrc_036abe
    gfx908_vsrc_1027ca
    gfx908_vsrc_2d4632
    gfx908_vsrc_6802ce
    gfx908_vsrc_9ad749
    gfx908_vsrc_be4895
    gfx908_vsrc_f3d248
