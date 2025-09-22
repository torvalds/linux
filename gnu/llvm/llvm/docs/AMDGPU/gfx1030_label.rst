..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx1030_label:

label
=====

A branch target, which is a 16-bit signed integer treated as a PC-relative dword offset.

This operand may be specified as one of the following:

* An :ref:`integer_number<amdgpu_synid_integer_number>` or an :ref:`absolute_expression<amdgpu_synid_absolute_expression>`. The value must be in the range from -32768 to 65535.
* A :ref:`symbol<amdgpu_synid_symbol>` (for example, a label) representing a relocatable address in the same compilation unit where it is referred from. The value is handled as a 16-bit PC-relative dword offset to be resolved by a linker.

Examples:

.. parsed-literal::

  offset = 30
  label_1:
  label_2 = . + 4

  s_branch 32
  s_branch offset + 2
  s_branch label_1
  s_branch label_2
  s_branch label_3
  s_branch label_4

  label_3 = label_2 + 4
  label_4:
