..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx11_tgt:

tgt
===

An export target:

    ================== ===================================
    Syntax             Description
    ================== ===================================
    pos{0..4}          Copy vertex position 0..4.
    mrt{0..7}          Copy pixel color to the MRTs 0..7.
    mrtz               Copy pixel depth (Z) data.
    prim               Copy primitive (connectivity) data.
    dual_src_blend0    Copy dual source blend left.
    dual_src_blend1    Copy dual source blend right.
    ================== ===================================

Examples:

.. parsed-literal::

  exp pos3 v1, v2, v3, v4
  exp mrt0 v1, v2, v3, v4
