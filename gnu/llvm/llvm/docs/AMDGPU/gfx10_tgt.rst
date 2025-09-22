..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx10_tgt:

tgt
===

An export target:

    ================== ===================================
    Syntax             Description
    ================== ===================================
    pos{0..4}          Copy vertex position 0..4.
    param{0..31}       Copy vertex parameter 0..31.
    mrt{0..7}          Copy pixel color to the MRTs 0..7.
    mrtz               Copy pixel depth (Z) data.
    prim               Copy primitive (connectivity) data.
    null               Copy nothing.
    ================== ===================================

Examples:

.. parsed-literal::

  exp pos3 v1, v2, v3, v4
  exp mrt0 v1, v2, v3, v4
