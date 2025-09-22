..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx9_attr:

attr
====

Interpolation attribute and channel:

    ============== ===================================
    Syntax         Description
    ============== ===================================
    attr{0..32}.x  Attribute 0..32 with *x* channel.
    attr{0..32}.y  Attribute 0..32 with *y* channel.
    attr{0..32}.z  Attribute 0..32 with *z* channel.
    attr{0..32}.w  Attribute 0..32 with *w* channel.
    ============== ===================================

Examples:

.. parsed-literal::

    v_interp_p1_f32 v1, v0, attr0.x
    v_interp_p1_f32 v1, v0, attr32.w
