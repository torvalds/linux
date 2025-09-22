======================================
Syntax of AMDGPU Instruction Modifiers
======================================

.. contents::
   :local:

Conventions
===========

The following notation is used throughout this document:

    =================== =============================================================
    Notation            Description
    =================== =============================================================
    {0..N}              Any integer value in the range from 0 to N (inclusive).
    <x>                 Syntax and meaning of *x* are explained elsewhere.
    =================== =============================================================

.. _amdgpu_syn_modifiers:

Modifiers
=========

DS Modifiers
------------

.. _amdgpu_synid_ds_offset80:

offset0
~~~~~~~

Specifies the first 8-bit offset, in bytes. The default value is 0.

Used with DS instructions that expect two addresses.

    =================== ====================================================================
    Syntax              Description
    =================== ====================================================================
    offset0:{0..0xFF}   Specifies an unsigned 8-bit offset as a positive
                        :ref:`integer number <amdgpu_synid_integer_number>`
                        or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    =================== ====================================================================

Examples:

.. parsed-literal::

  offset0:0xff
  offset0:2-x
  offset0:-x-y

.. _amdgpu_synid_ds_offset81:

offset1
~~~~~~~

Specifies the second 8-bit offset, in bytes. The default value is 0.

Used with DS instructions that expect two addresses.

    =================== ====================================================================
    Syntax              Description
    =================== ====================================================================
    offset1:{0..0xFF}   Specifies an unsigned 8-bit offset as a positive
                        :ref:`integer number <amdgpu_synid_integer_number>`
                        or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    =================== ====================================================================

Examples:

.. parsed-literal::

  offset1:0xff
  offset1:2-x
  offset1:-x-y

.. _amdgpu_synid_ds_offset16:

offset
~~~~~~

Specifies a 16-bit offset, in bytes. The default value is 0.

Used with DS instructions that expect a single address.

    ==================== ====================================================================
    Syntax               Description
    ==================== ====================================================================
    offset:{0..0xFFFF}   Specifies an unsigned 16-bit offset as a positive
                         :ref:`integer number <amdgpu_synid_integer_number>`
                         or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ==================== ====================================================================

Examples:

.. parsed-literal::

  offset:65535
  offset:0xffff
  offset:-x-y

.. _amdgpu_synid_sw_offset16:

swizzle pattern
~~~~~~~~~~~~~~~

This is a special modifier that may be used with *ds_swizzle_b32* instruction only.
It specifies a swizzle pattern in numeric or symbolic form. The default value is 0.

    ======================================================= ===========================================================
    Syntax                                                  Description
    ======================================================= ===========================================================
    offset:{0..0xFFFF}                                      Specifies a 16-bit swizzle pattern.
    offset:swizzle(QUAD_PERM,{0..3},{0..3},{0..3},{0..3})   Specifies a quad permute mode pattern

                                                            Each number is a lane *id*.
    offset:swizzle(BITMASK_PERM, "<mask>")                  Specifies a bitmask permute mode pattern.

                                                            The pattern converts a 5-bit lane *id* to another
                                                            lane *id* with which the lane interacts.

                                                            The *mask* is a 5-character sequence which
                                                            specifies how to transform the bits of the
                                                            lane *id*.

                                                            The following characters are allowed:

                                                            * "0" - set bit to 0.

                                                            * "1" - set bit to 1.

                                                            * "p" - preserve bit.

                                                            * "i" - inverse bit.

    offset:swizzle(BROADCAST,{2..32},{0..N})                Specifies a broadcast mode.

                                                            Broadcasts the value of any particular lane to
                                                            all lanes in its group.

                                                            The first numeric parameter is a group
                                                            size and must be equal to 2, 4, 8, 16 or 32.

                                                            The second numeric parameter is an index of the
                                                            lane being broadcast.

                                                            The index must not exceed group size.
    offset:swizzle(SWAP,{1..16})                            Specifies a swap mode.

                                                            Swaps the neighboring groups of
                                                            1, 2, 4, 8 or 16 lanes.
    offset:swizzle(REVERSE,{2..32})                         Specifies a reverse mode.

                                                            Reverses the lanes for groups of 2, 4, 8, 16 or 32 lanes.
    ======================================================= ===========================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  offset:255
  offset:0xffff
  offset:swizzle(QUAD_PERM, 0, 1, 2, 3)
  offset:swizzle(BITMASK_PERM, "01pi0")
  offset:swizzle(BROADCAST, 2, 0)
  offset:swizzle(SWAP, 8)
  offset:swizzle(REVERSE, 30 + 2)

.. _amdgpu_synid_gds:

gds
~~~

Specifies whether to use GDS or LDS memory (LDS is the default).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    gds                                      Use GDS memory.
    ======================================== ================================================


EXP Modifiers
-------------

.. _amdgpu_synid_done:

done
~~~~

Specifies if this is the last export from the shader to the target. By default,
an *export* instruction does not finish an export sequence.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    done                                     Indicates the last export operation.
    ======================================== ================================================

.. _amdgpu_synid_compr:

compr
~~~~~

Indicates if the data is compressed (data is not compressed by default).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    compr                                    Data is compressed.
    ======================================== ================================================

.. _amdgpu_synid_vm:

vm
~~

Specifies if the :ref:`exec<amdgpu_synid_exec>` mask is valid for this *export* instruction
(the mask is not valid by default).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    vm                                       Set the flag indicating a valid
                                             :ref:`exec<amdgpu_synid_exec>` mask.
    ======================================== ================================================

.. _amdgpu_synid_row_en:

row_en
~~~~~~

Specifies whether to export one row or multiple rows of data.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    row_en                                   Export multiple rows using row index from M0.
    ======================================== ================================================

FLAT Modifiers
--------------

.. _amdgpu_synid_flat_offset12:

offset12
~~~~~~~~

Specifies an immediate unsigned 12-bit offset, in bytes. The default value is 0.

    ================= ====================================================================
    Syntax            Description
    ================= ====================================================================
    offset:{0..4095}  Specifies a 12-bit unsigned offset as a positive
                      :ref:`integer number <amdgpu_synid_integer_number>`
                      or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ================= ====================================================================

Examples:

.. parsed-literal::

  offset:4095
  offset:x-0xff

.. _amdgpu_synid_flat_offset13s:

offset13s
~~~~~~~~~

Specifies an immediate signed 13-bit offset, in bytes. The default value is 0.

    ===================== ====================================================================
    Syntax                Description
    ===================== ====================================================================
    offset:{-4096..4095}  Specifies a 13-bit signed offset as an
                          :ref:`integer number <amdgpu_synid_integer_number>`
                          or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ===================== ====================================================================

Examples:

.. parsed-literal::

  offset:-4000
  offset:0x10
  offset:-x

.. _amdgpu_synid_flat_offset12s:

offset12s
~~~~~~~~~

Specifies an immediate signed 12-bit offset, in bytes. The default value is 0.

    ===================== ====================================================================
    Syntax                Description
    ===================== ====================================================================
    offset:{-2048..2047}  Specifies a 12-bit signed offset as an
                          :ref:`integer number <amdgpu_synid_integer_number>`
                          or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ===================== ====================================================================

Examples:

.. parsed-literal::

  offset:-2000
  offset:0x10
  offset:-x+y

.. _amdgpu_synid_flat_offset11:

offset11
~~~~~~~~

Specifies an immediate unsigned 11-bit offset, in bytes. The default value is 0.

    ================= ====================================================================
    Syntax            Description
    ================= ====================================================================
    offset:{0..2047}  Specifies an 11-bit unsigned offset as a positive
                      :ref:`integer number <amdgpu_synid_integer_number>`
                      or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ================= ====================================================================

Examples:

.. parsed-literal::

  offset:2047
  offset:x+0xff

dlc
~~~

See a description :ref:`here<amdgpu_synid_dlc>`.

glc
~~~

See a description :ref:`here<amdgpu_synid_glc>`.

lds
~~~

See a description :ref:`here<amdgpu_synid_lds>`.

slc
~~~

See a description :ref:`here<amdgpu_synid_slc>`.

tfe
~~~

See a description :ref:`here<amdgpu_synid_tfe>`.

nv
~~

See a description :ref:`here<amdgpu_synid_nv>`.

sc0
~~~

See a description :ref:`here<amdgpu_synid_sc0>`.

sc1
~~~

See a description :ref:`here<amdgpu_synid_sc1>`.

nt
~~

See a description :ref:`here<amdgpu_synid_nt>`.

MIMG Modifiers
--------------

.. _amdgpu_synid_dmask:

dmask
~~~~~

Specifies which channels (image components) are used by the operation.
By default, no channels are used.

    =============== ====================================================================
    Syntax          Description
    =============== ====================================================================
    dmask:{0..15}   Specifies image channels as a positive
                    :ref:`integer number <amdgpu_synid_integer_number>`
                    or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

                    Each bit corresponds to one of 4 image components (RGBA).

                    If the specified bit value is 0, the image component is not used,
                    while value 1 means that the component is used.
    =============== ====================================================================

This modifier has some limitations depending on the instruction kind:

    =================================================== ========================
    Instruction Kind                                    Valid dmask Values
    =================================================== ========================
    32-bit atomic *cmpswap*                             0x3
    32-bit atomic instructions except for *cmpswap*     0x1
    64-bit atomic *cmpswap*                             0xF
    64-bit atomic instructions except for *cmpswap*     0x3
    *gather4*                                           0x1, 0x2, 0x4, 0x8
    GFX11+ *msaa_load*                                  0x1, 0x2, 0x4, 0x8
    Other instructions                                  any value
    =================================================== ========================

Examples:

.. parsed-literal::

  dmask:0xf
  dmask:0b1111
  dmask:x|y|z

.. _amdgpu_synid_unorm:

unorm
~~~~~

Specifies whether the address is normalized or not (the address is normalized by default).

    ======================== ========================================
    Syntax                   Description
    ======================== ========================================
    unorm                    Force the address to be not normalized.
    ======================== ========================================

glc
~~~

See a description :ref:`here<amdgpu_synid_glc>`.

slc
~~~

See a description :ref:`here<amdgpu_synid_slc>`.

.. _amdgpu_synid_r128:

r128
~~~~

Specifies texture resource size. The default size is 256 bits.

    =================== ================================================
    Syntax              Description
    =================== ================================================
    r128                Specifies 128 bits texture resource size.
    =================== ================================================

.. WARNING:: Using this modifier shall decrease *rsrc* operand size from 8 to 4 dwords, \
             but assembler does not currently support this feature.

tfe
~~~

See a description :ref:`here<amdgpu_synid_tfe>`.

.. _amdgpu_synid_lwe:

lwe
~~~

Specifies LOD warning status (LOD warning is disabled by default).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    lwe                                      Enables LOD warning.
    ======================================== ================================================

.. _amdgpu_synid_da:

da
~~

Specifies if an array index must be sent to TA. By default, the array index is not sent.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    da                                       Send an array index to TA.
    ======================================== ================================================

.. _amdgpu_synid_d16:

d16
~~~

Specifies data size: 16 or 32 bits (32 bits by default).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    d16                                      Enables 16-bits data mode.

                                             On loads, convert data in memory to 16-bit
                                             format before storing it in VGPRs.

                                             For stores, convert 16-bit data in VGPRs to
                                             32 bits before writing the values to memory.

                                             Note that GFX8.0 does not support data packing.
                                             Each 16-bit data element occupies 1 VGPR.

                                             GFX8.1 and GFX9+ support data packing.
                                             Each pair of 16-bit data elements
                                             occupies 1 VGPR.
    ======================================== ================================================

.. _amdgpu_synid_a16:

a16
~~~

Specifies the size of image address components: 16 or 32 bits (32 bits by default).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    a16                                      Enables 16-bits image address components.
    ======================================== ================================================

.. _amdgpu_synid_dim:

dim
~~~

Specifies surface dimension. This is a mandatory modifier. There is no default value.

    =============================== =========================================================
    Syntax                          Description
    =============================== =========================================================
    dim:1D                          One-dimensional image.
    dim:2D                          Two-dimensional image.
    dim:3D                          Three-dimensional image.
    dim:CUBE                        Cubemap array.
    dim:1D_ARRAY                    One-dimensional image array.
    dim:2D_ARRAY                    Two-dimensional image array.
    dim:2D_MSAA                     Two-dimensional multi-sample auto-aliasing image.
    dim:2D_MSAA_ARRAY               Two-dimensional multi-sample auto-aliasing image array.
    =============================== =========================================================

The following table defines an alternative syntax which is supported
for compatibility with SP3 assembler:

    =============================== =========================================================
    Syntax                          Description
    =============================== =========================================================
    dim:SQ_RSRC_IMG_1D              One-dimensional image.
    dim:SQ_RSRC_IMG_2D              Two-dimensional image.
    dim:SQ_RSRC_IMG_3D              Three-dimensional image.
    dim:SQ_RSRC_IMG_CUBE            Cubemap array.
    dim:SQ_RSRC_IMG_1D_ARRAY        One-dimensional image array.
    dim:SQ_RSRC_IMG_2D_ARRAY        Two-dimensional image array.
    dim:SQ_RSRC_IMG_2D_MSAA         Two-dimensional multi-sample auto-aliasing image.
    dim:SQ_RSRC_IMG_2D_MSAA_ARRAY   Two-dimensional multi-sample auto-aliasing image array.
    =============================== =========================================================

dlc
~~~

See a description :ref:`here<amdgpu_synid_dlc>`.

Miscellaneous Modifiers
-----------------------

.. _amdgpu_synid_dlc:

dlc
~~~

Controls device level cache policy for memory operations. Used for synchronization.
When specified, forces operation to bypass device level cache, making the operation device
level coherent. By default, instructions use device level cache.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    dlc                                      Bypass device level cache.
    ======================================== ================================================

.. _amdgpu_synid_glc:

glc
~~~

For atomic opcodes, this modifier indicates that the instruction returns the value from memory
before the operation. For other opcodes, it is used together with :ref:`slc<amdgpu_synid_slc>`
to specify cache policy.

The default value is off (0).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    glc                                      Set glc bit to 1.
    ======================================== ================================================

.. _amdgpu_synid_lds:

lds
~~~

Specifies where to store the result: VGPRs or LDS (VGPRs by default).

    ======================================== ===========================
    Syntax                                   Description
    ======================================== ===========================
    lds                                      Store the result in LDS.
    ======================================== ===========================

.. _amdgpu_synid_nv:

nv
~~

Specifies if the instruction is operating on non-volatile memory.
By default, memory is volatile.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    nv                                       Indicates that the instruction operates on
                                             non-volatile memory.
    ======================================== ================================================

.. _amdgpu_synid_slc:

slc
~~~

Controls behavior of L2 cache. The default value is off (0).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    slc                                      Set slc bit to 1.
    ======================================== ================================================

.. _amdgpu_synid_tfe:

tfe
~~~

Controls access to partially resident textures. The default value is off (0).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    tfe                                      Set tfe bit to 1.
    ======================================== ================================================

.. _amdgpu_synid_sc0:

sc0
~~~

For atomic opcodes, this modifier indicates that the instruction returns the value from memory
before the operation. For other opcodes, it is used together with :ref:`sc1<amdgpu_synid_sc1>`
to specify cache policy.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    sc0                                      Set sc0 bit to 1.
    ======================================== ================================================

.. _amdgpu_synid_sc1:

sc1
~~~

This modifier is used together with :ref:`sc0<amdgpu_synid_sc0>` to specify cache
policy.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    sc1                                      Set sc1 bit to 1.
    ======================================== ================================================

.. _amdgpu_synid_nt:

nt
~~

Indicates an operation with non-temporal data.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    nt                                       Set nt bit to 1.
    ======================================== ================================================

MUBUF/MTBUF Modifiers
---------------------

.. _amdgpu_synid_idxen:

idxen
~~~~~

Specifies whether address components include an index. By default, the index is not used.

May be used together with :ref:`offen<amdgpu_synid_offen>`.

Cannot be used with :ref:`addr64<amdgpu_synid_addr64>`.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    idxen                                    Address components include an index.
    ======================================== ================================================

.. _amdgpu_synid_offen:

offen
~~~~~

Specifies whether address components include an offset. By default, the offset is not used.

May be used together with :ref:`idxen<amdgpu_synid_idxen>`.

Cannot be used with :ref:`addr64<amdgpu_synid_addr64>`.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    offen                                    Address components include an offset.
    ======================================== ================================================

.. _amdgpu_synid_addr64:

addr64
~~~~~~

Specifies whether a 64-bit address is used. By default, no address is used.

Cannot be used with :ref:`offen<amdgpu_synid_offen>` and
:ref:`idxen<amdgpu_synid_idxen>` modifiers.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    addr64                                   A 64-bit address is used.
    ======================================== ================================================

.. _amdgpu_synid_buf_offset12:

offset12
~~~~~~~~

Specifies an immediate unsigned 12-bit offset, in bytes. The default value is 0.

    ================== ====================================================================
    Syntax             Description
    ================== ====================================================================
    offset:{0..0xFFF}  Specifies a 12-bit unsigned offset as a positive
                       :ref:`integer number <amdgpu_synid_integer_number>`
                       or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ================== ====================================================================

Examples:

.. parsed-literal::

  offset:x+y
  offset:0x10

glc
~~~

See a description :ref:`here<amdgpu_synid_glc>`.

slc
~~~

See a description :ref:`here<amdgpu_synid_slc>`.

lds
~~~

See a description :ref:`here<amdgpu_synid_lds>`.

dlc
~~~

See a description :ref:`here<amdgpu_synid_dlc>`.

tfe
~~~

See a description :ref:`here<amdgpu_synid_tfe>`.

.. _amdgpu_synid_fmt:

fmt
~~~

Specifies data and numeric formats used by the operation.
The default numeric format is BUF_NUM_FORMAT_UNORM.
The default data format is BUF_DATA_FORMAT_8.

    ========================================= ===============================================================
    Syntax                                    Description
    ========================================= ===============================================================
    format:{0..127}                           Use a format specified as either an
                                              :ref:`integer number<amdgpu_synid_integer_number>` or an
                                              :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    format:[<data format>]                    Use the specified data format and
                                              default numeric format.
    format:[<numeric format>]                 Use the specified numeric format and
                                              default data format.
    format:[<data format>,<numeric format>]   Use the specified data and numeric formats.
    format:[<numeric format>,<data format>]   Use the specified data and numeric formats.
    ========================================= ===============================================================

.. _amdgpu_synid_format_data:

Supported data formats are defined in the following table:

    ========================================= ===============================
    Syntax                                    Note
    ========================================= ===============================
    BUF_DATA_FORMAT_INVALID
    BUF_DATA_FORMAT_8                         The default value.
    BUF_DATA_FORMAT_16
    BUF_DATA_FORMAT_8_8
    BUF_DATA_FORMAT_32
    BUF_DATA_FORMAT_16_16
    BUF_DATA_FORMAT_10_11_11
    BUF_DATA_FORMAT_11_11_10
    BUF_DATA_FORMAT_10_10_10_2
    BUF_DATA_FORMAT_2_10_10_10
    BUF_DATA_FORMAT_8_8_8_8
    BUF_DATA_FORMAT_32_32
    BUF_DATA_FORMAT_16_16_16_16
    BUF_DATA_FORMAT_32_32_32
    BUF_DATA_FORMAT_32_32_32_32
    BUF_DATA_FORMAT_RESERVED_15
    ========================================= ===============================

.. _amdgpu_synid_format_num:

Supported numeric formats are defined below:

    ========================================= ===============================
    Syntax                                    Note
    ========================================= ===============================
    BUF_NUM_FORMAT_UNORM                      The default value.
    BUF_NUM_FORMAT_SNORM
    BUF_NUM_FORMAT_USCALED
    BUF_NUM_FORMAT_SSCALED
    BUF_NUM_FORMAT_UINT
    BUF_NUM_FORMAT_SINT
    BUF_NUM_FORMAT_SNORM_OGL                  GFX7 only.
    BUF_NUM_FORMAT_RESERVED_6                 GFX8 and GFX9 only.
    BUF_NUM_FORMAT_FLOAT
    ========================================= ===============================

Examples:

.. parsed-literal::

  format:0
  format:127
  format:[BUF_DATA_FORMAT_16]
  format:[BUF_DATA_FORMAT_16,BUF_NUM_FORMAT_SSCALED]
  format:[BUF_NUM_FORMAT_FLOAT]

.. _amdgpu_synid_ufmt:

ufmt
~~~~

Specifies a unified format used by the operation.
The default format is BUF_FMT_8_UNORM.

    ========================================= ===============================================================
    Syntax                                    Description
    ========================================= ===============================================================
    format:{0..127}                           Use a unified format specified as either an
                                              :ref:`integer number<amdgpu_synid_integer_number>` or an
                                              :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
                                              Note that unified format numbers are incompatible with
                                              format numbers used for pre-GFX10 ISA.
    format:[<unified format>]                 Use the specified unified format.
    ========================================= ===============================================================

Unified format is a replacement for :ref:`data<amdgpu_synid_format_data>`
and :ref:`numeric<amdgpu_synid_format_num>` formats. For compatibility with older ISA,
:ref:`the syntax with data and numeric formats<amdgpu_synid_fmt>` is still accepted
provided that the combination of formats can be mapped to a unified format.

Supported unified formats and equivalent combinations of data and numeric formats
are defined below:

    ============================== ============================== ============================= ============
    Unified Format Syntax          Equivalent Data Format         Equivalent Numeric Format     Note
    ============================== ============================== ============================= ============
    BUF_FMT_INVALID                BUF_DATA_FORMAT_INVALID        BUF_NUM_FORMAT_UNORM

    BUF_FMT_8_UNORM                BUF_DATA_FORMAT_8              BUF_NUM_FORMAT_UNORM
    BUF_FMT_8_SNORM                BUF_DATA_FORMAT_8              BUF_NUM_FORMAT_SNORM
    BUF_FMT_8_USCALED              BUF_DATA_FORMAT_8              BUF_NUM_FORMAT_USCALED
    BUF_FMT_8_SSCALED              BUF_DATA_FORMAT_8              BUF_NUM_FORMAT_SSCALED
    BUF_FMT_8_UINT                 BUF_DATA_FORMAT_8              BUF_NUM_FORMAT_UINT
    BUF_FMT_8_SINT                 BUF_DATA_FORMAT_8              BUF_NUM_FORMAT_SINT

    BUF_FMT_16_UNORM               BUF_DATA_FORMAT_16             BUF_NUM_FORMAT_UNORM
    BUF_FMT_16_SNORM               BUF_DATA_FORMAT_16             BUF_NUM_FORMAT_SNORM
    BUF_FMT_16_USCALED             BUF_DATA_FORMAT_16             BUF_NUM_FORMAT_USCALED
    BUF_FMT_16_SSCALED             BUF_DATA_FORMAT_16             BUF_NUM_FORMAT_SSCALED
    BUF_FMT_16_UINT                BUF_DATA_FORMAT_16             BUF_NUM_FORMAT_UINT
    BUF_FMT_16_SINT                BUF_DATA_FORMAT_16             BUF_NUM_FORMAT_SINT
    BUF_FMT_16_FLOAT               BUF_DATA_FORMAT_16             BUF_NUM_FORMAT_FLOAT

    BUF_FMT_8_8_UNORM              BUF_DATA_FORMAT_8_8            BUF_NUM_FORMAT_UNORM
    BUF_FMT_8_8_SNORM              BUF_DATA_FORMAT_8_8            BUF_NUM_FORMAT_SNORM
    BUF_FMT_8_8_USCALED            BUF_DATA_FORMAT_8_8            BUF_NUM_FORMAT_USCALED
    BUF_FMT_8_8_SSCALED            BUF_DATA_FORMAT_8_8            BUF_NUM_FORMAT_SSCALED
    BUF_FMT_8_8_UINT               BUF_DATA_FORMAT_8_8            BUF_NUM_FORMAT_UINT
    BUF_FMT_8_8_SINT               BUF_DATA_FORMAT_8_8            BUF_NUM_FORMAT_SINT

    BUF_FMT_32_UINT                BUF_DATA_FORMAT_32             BUF_NUM_FORMAT_UINT
    BUF_FMT_32_SINT                BUF_DATA_FORMAT_32             BUF_NUM_FORMAT_SINT
    BUF_FMT_32_FLOAT               BUF_DATA_FORMAT_32             BUF_NUM_FORMAT_FLOAT

    BUF_FMT_16_16_UNORM            BUF_DATA_FORMAT_16_16          BUF_NUM_FORMAT_UNORM
    BUF_FMT_16_16_SNORM            BUF_DATA_FORMAT_16_16          BUF_NUM_FORMAT_SNORM
    BUF_FMT_16_16_USCALED          BUF_DATA_FORMAT_16_16          BUF_NUM_FORMAT_USCALED
    BUF_FMT_16_16_SSCALED          BUF_DATA_FORMAT_16_16          BUF_NUM_FORMAT_SSCALED
    BUF_FMT_16_16_UINT             BUF_DATA_FORMAT_16_16          BUF_NUM_FORMAT_UINT
    BUF_FMT_16_16_SINT             BUF_DATA_FORMAT_16_16          BUF_NUM_FORMAT_SINT
    BUF_FMT_16_16_FLOAT            BUF_DATA_FORMAT_16_16          BUF_NUM_FORMAT_FLOAT

    BUF_FMT_10_11_11_UNORM         BUF_DATA_FORMAT_10_11_11       BUF_NUM_FORMAT_UNORM          GFX10 only
    BUF_FMT_10_11_11_SNORM         BUF_DATA_FORMAT_10_11_11       BUF_NUM_FORMAT_SNORM          GFX10 only
    BUF_FMT_10_11_11_USCALED       BUF_DATA_FORMAT_10_11_11       BUF_NUM_FORMAT_USCALED        GFX10 only
    BUF_FMT_10_11_11_SSCALED       BUF_DATA_FORMAT_10_11_11       BUF_NUM_FORMAT_SSCALED        GFX10 only
    BUF_FMT_10_11_11_UINT          BUF_DATA_FORMAT_10_11_11       BUF_NUM_FORMAT_UINT           GFX10 only
    BUF_FMT_10_11_11_SINT          BUF_DATA_FORMAT_10_11_11       BUF_NUM_FORMAT_SINT           GFX10 only
    BUF_FMT_10_11_11_FLOAT         BUF_DATA_FORMAT_10_11_11       BUF_NUM_FORMAT_FLOAT

    BUF_FMT_11_11_10_UNORM         BUF_DATA_FORMAT_11_11_10       BUF_NUM_FORMAT_UNORM          GFX10 only
    BUF_FMT_11_11_10_SNORM         BUF_DATA_FORMAT_11_11_10       BUF_NUM_FORMAT_SNORM          GFX10 only
    BUF_FMT_11_11_10_USCALED       BUF_DATA_FORMAT_11_11_10       BUF_NUM_FORMAT_USCALED        GFX10 only
    BUF_FMT_11_11_10_SSCALED       BUF_DATA_FORMAT_11_11_10       BUF_NUM_FORMAT_SSCALED        GFX10 only
    BUF_FMT_11_11_10_UINT          BUF_DATA_FORMAT_11_11_10       BUF_NUM_FORMAT_UINT           GFX10 only
    BUF_FMT_11_11_10_SINT          BUF_DATA_FORMAT_11_11_10       BUF_NUM_FORMAT_SINT           GFX10 only
    BUF_FMT_11_11_10_FLOAT         BUF_DATA_FORMAT_11_11_10       BUF_NUM_FORMAT_FLOAT

    BUF_FMT_10_10_10_2_UNORM       BUF_DATA_FORMAT_10_10_10_2     BUF_NUM_FORMAT_UNORM
    BUF_FMT_10_10_10_2_SNORM       BUF_DATA_FORMAT_10_10_10_2     BUF_NUM_FORMAT_SNORM
    BUF_FMT_10_10_10_2_USCALED     BUF_DATA_FORMAT_10_10_10_2     BUF_NUM_FORMAT_USCALED        GFX10 only
    BUF_FMT_10_10_10_2_SSCALED     BUF_DATA_FORMAT_10_10_10_2     BUF_NUM_FORMAT_SSCALED        GFX10 only
    BUF_FMT_10_10_10_2_UINT        BUF_DATA_FORMAT_10_10_10_2     BUF_NUM_FORMAT_UINT
    BUF_FMT_10_10_10_2_SINT        BUF_DATA_FORMAT_10_10_10_2     BUF_NUM_FORMAT_SINT

    BUF_FMT_2_10_10_10_UNORM       BUF_DATA_FORMAT_2_10_10_10     BUF_NUM_FORMAT_UNORM
    BUF_FMT_2_10_10_10_SNORM       BUF_DATA_FORMAT_2_10_10_10     BUF_NUM_FORMAT_SNORM
    BUF_FMT_2_10_10_10_USCALED     BUF_DATA_FORMAT_2_10_10_10     BUF_NUM_FORMAT_USCALED
    BUF_FMT_2_10_10_10_SSCALED     BUF_DATA_FORMAT_2_10_10_10     BUF_NUM_FORMAT_SSCALED
    BUF_FMT_2_10_10_10_UINT        BUF_DATA_FORMAT_2_10_10_10     BUF_NUM_FORMAT_UINT
    BUF_FMT_2_10_10_10_SINT        BUF_DATA_FORMAT_2_10_10_10     BUF_NUM_FORMAT_SINT

    BUF_FMT_8_8_8_8_UNORM          BUF_DATA_FORMAT_8_8_8_8        BUF_NUM_FORMAT_UNORM
    BUF_FMT_8_8_8_8_SNORM          BUF_DATA_FORMAT_8_8_8_8        BUF_NUM_FORMAT_SNORM
    BUF_FMT_8_8_8_8_USCALED        BUF_DATA_FORMAT_8_8_8_8        BUF_NUM_FORMAT_USCALED
    BUF_FMT_8_8_8_8_SSCALED        BUF_DATA_FORMAT_8_8_8_8        BUF_NUM_FORMAT_SSCALED
    BUF_FMT_8_8_8_8_UINT           BUF_DATA_FORMAT_8_8_8_8        BUF_NUM_FORMAT_UINT
    BUF_FMT_8_8_8_8_SINT           BUF_DATA_FORMAT_8_8_8_8        BUF_NUM_FORMAT_SINT

    BUF_FMT_32_32_UINT             BUF_DATA_FORMAT_32_32          BUF_NUM_FORMAT_UINT
    BUF_FMT_32_32_SINT             BUF_DATA_FORMAT_32_32          BUF_NUM_FORMAT_SINT
    BUF_FMT_32_32_FLOAT            BUF_DATA_FORMAT_32_32          BUF_NUM_FORMAT_FLOAT

    BUF_FMT_16_16_16_16_UNORM      BUF_DATA_FORMAT_16_16_16_16    BUF_NUM_FORMAT_UNORM
    BUF_FMT_16_16_16_16_SNORM      BUF_DATA_FORMAT_16_16_16_16    BUF_NUM_FORMAT_SNORM
    BUF_FMT_16_16_16_16_USCALED    BUF_DATA_FORMAT_16_16_16_16    BUF_NUM_FORMAT_USCALED
    BUF_FMT_16_16_16_16_SSCALED    BUF_DATA_FORMAT_16_16_16_16    BUF_NUM_FORMAT_SSCALED
    BUF_FMT_16_16_16_16_UINT       BUF_DATA_FORMAT_16_16_16_16    BUF_NUM_FORMAT_UINT
    BUF_FMT_16_16_16_16_SINT       BUF_DATA_FORMAT_16_16_16_16    BUF_NUM_FORMAT_SINT
    BUF_FMT_16_16_16_16_FLOAT      BUF_DATA_FORMAT_16_16_16_16    BUF_NUM_FORMAT_FLOAT

    BUF_FMT_32_32_32_UINT          BUF_DATA_FORMAT_32_32_32       BUF_NUM_FORMAT_UINT
    BUF_FMT_32_32_32_SINT          BUF_DATA_FORMAT_32_32_32       BUF_NUM_FORMAT_SINT
    BUF_FMT_32_32_32_FLOAT         BUF_DATA_FORMAT_32_32_32       BUF_NUM_FORMAT_FLOAT
    BUF_FMT_32_32_32_32_UINT       BUF_DATA_FORMAT_32_32_32_32    BUF_NUM_FORMAT_UINT
    BUF_FMT_32_32_32_32_SINT       BUF_DATA_FORMAT_32_32_32_32    BUF_NUM_FORMAT_SINT
    BUF_FMT_32_32_32_32_FLOAT      BUF_DATA_FORMAT_32_32_32_32    BUF_NUM_FORMAT_FLOAT
    ============================== ============================== ============================= ============

Examples:

.. parsed-literal::

  format:0
  format:[BUF_FMT_32_UINT]

SMRD/SMEM Modifiers
-------------------

glc
~~~

See a description :ref:`here<amdgpu_synid_glc>`.

nv
~~

See a description :ref:`here<amdgpu_synid_nv>`.

dlc
~~~

See a description :ref:`here<amdgpu_synid_dlc>`.

.. _amdgpu_synid_smem_offset20u:

offset20u
~~~~~~~~~

Specifies an unsigned 20-bit offset, in bytes. The default value is 0.

    ==================== ====================================================================
    Syntax               Description
    ==================== ====================================================================
    offset:{0..0xFFFFF}  Specifies an offset as a positive
                         :ref:`integer number <amdgpu_synid_integer_number>`
                         or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ==================== ====================================================================

Examples:

.. parsed-literal::

  offset:1
  offset:0xfffff
  offset:x-y

.. _amdgpu_synid_smem_offset21s:

offset21s
~~~~~~~~~

Specifies a signed 21-bit offset, in bytes. The default value is 0.

    ============================= ====================================================================
    Syntax                        Description
    ============================= ====================================================================
    offset:{-0x100000..0xFFFFF}   Specifies an offset as an
                                  :ref:`integer number <amdgpu_synid_integer_number>`
                                  or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    ============================= ====================================================================

Examples:

.. parsed-literal::

  offset:-1
  offset:0xfffff
  offset:-x

VINTRP/VINTERP/LDSDIR Modifiers
-------------------------------

.. _amdgpu_synid_high:

high
~~~~

Specifies which half of the LDS word to use. Low half of LDS word is used by default.

    ======================================== ================================
    Syntax                                   Description
    ======================================== ================================
    high                                     Use the high half of LDS word.
    ======================================== ================================

neg
~~~

See a description :ref:`here<amdgpu_synid_neg>`.

.. _amdgpu_synid_wait_exp:

wait_exp
~~~~~~~~

Specifies a wait on the EXP counter before issuing the current instruction.
The counter must be less than or equal to this value before the instruction is issued.
If set to 7, no wait is performed.

The default value is zero. This is a safe value, but it may be suboptimal.

    ================ ======================================================
    Syntax           Description
    ================ ======================================================
    wait_exp:{0..7}  An additional wait on the EXP counter before
                     issuing this instruction.
    ================ ======================================================

.. _amdgpu_synid_wait_vdst:

wait_vdst
~~~~~~~~~

Specifies a wait on the VA_VDST counter before issuing the current instruction.
The counter must be less than or equal to this value before the instruction is issued.
If set to 15, no wait is performed.

The default value is zero. This is a safe value, but it may be suboptimal.

    ================== ======================================================
    Syntax             Description
    ================== ======================================================
    wait_vdst:{0..15}  An additional wait on the VA_VDST counter before
                       issuing this instruction.
    ================== ======================================================

DPP8 Modifiers
--------------

.. _amdgpu_synid_dpp8_sel:

dpp8_sel
~~~~~~~~

Selects which lanes to pull data from, within a group of 8 lanes. This is a mandatory modifier.
There is no default value.

The *dpp8_sel* modifier must specify exactly 8 values.
The first value selects which lane to read from to supply data into lane 0.
The second value controls lane 1 and so on.

Each value may be specified as either
an :ref:`integer number<amdgpu_synid_integer_number>` or
an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

    =============================================================== ===========================
    Syntax                                                          Description
    =============================================================== ===========================
    dpp8:[{0..7},{0..7},{0..7},{0..7},{0..7},{0..7},{0..7},{0..7}]  Select lanes to read from.
    =============================================================== ===========================

Examples:

.. parsed-literal::

  dpp8:[7,6,5,4,3,2,1,0]
  dpp8:[0,1,0,1,0,1,0,1]

.. _amdgpu_synid_fi8:

fi
~~

Controls interaction with inactive lanes for *dpp8* instructions. The default value is zero.

Note: *inactive* lanes are those whose :ref:`exec<amdgpu_synid_exec>` mask bit is zero.

    ==================================== =====================================================
    Syntax                               Description
    ==================================== =====================================================
    fi:0                                 Fetch zero when accessing data from inactive lanes.
    fi:1                                 Fetch pre-existing values from inactive lanes.
    ==================================== =====================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

DPP Modifiers
-------------

.. _amdgpu_synid_dpp_ctrl:

dpp_ctrl
~~~~~~~~

Specifies how data is shared between threads. This is a mandatory modifier.
There is no default value.

Note: the lanes of a wavefront are organized in four *rows* and four *banks*.

    ======================================== ========================================================
    Syntax                                   Description
    ======================================== ========================================================
    quad_perm:[{0..3},{0..3},{0..3},{0..3}]  Full permute of 4 threads.
    row_mirror                               Mirror threads within row.
    row_half_mirror                          Mirror threads within 1/2 row (8 threads).
    row_bcast:15                             Broadcast the 15th thread of each row to the next row.
    row_bcast:31                             Broadcast thread 31 to rows 2 and 3.
    wave_shl:1                               Wavefront left shift by 1 thread.
    wave_rol:1                               Wavefront left rotate by 1 thread.
    wave_shr:1                               Wavefront right shift by 1 thread.
    wave_ror:1                               Wavefront right rotate by 1 thread.
    row_shl:{1..15}                          Row shift left by 1-15 threads.
    row_shr:{1..15}                          Row shift right by 1-15 threads.
    row_ror:{1..15}                          Row rotate right by 1-15 threads.
    ======================================== ========================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  quad_perm:[0, 1, 2, 3]
  row_shl:3

.. _amdgpu_synid_dpp16_ctrl:

dpp16_ctrl
~~~~~~~~~~

Specifies how data is shared between threads. This is a mandatory modifier.
There is no default value.

Note: the lanes of a wavefront are organized in four *rows* and four *banks*.
(There are only two rows in *wave32* mode.)

    ======================================== =======================================================
    Syntax                                   Description
    ======================================== =======================================================
    quad_perm:[{0..3},{0..3},{0..3},{0..3}]  Full permute of 4 threads.
    row_mirror                               Mirror threads within row.
    row_half_mirror                          Mirror threads within 1/2 row (8 threads).
    row_share:{0..15}                        Share the value from the specified lane with other
                                             lanes in the row.
    row_xmask:{0..15}                        Fetch from XOR(<current lane id>,<specified lane id>).
    row_shl:{1..15}                          Row shift left by 1-15 threads.
    row_shr:{1..15}                          Row shift right by 1-15 threads.
    row_ror:{1..15}                          Row rotate right by 1-15 threads.
    ======================================== =======================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  quad_perm:[0, 1, 2, 3]
  row_shl:3

.. _amdgpu_synid_dpp32_ctrl:

dpp32_ctrl
~~~~~~~~~~

Specifies how data is shared between threads. This is a mandatory modifier.
There is no default value.

Note: the lanes of a wavefront are organized in four *rows* and four *banks*.

    ======================================== =========================================================
    Syntax                                   Description
    ======================================== =========================================================
    quad_perm:[{0..3},{0..3},{0..3},{0..3}]  Full permute of 4 threads.
    row_mirror                               Mirror threads within row.
    row_half_mirror                          Mirror threads within 1/2 row (8 threads).
    row_bcast:15                             Broadcast the 15th thread of each row to the next row.
    row_bcast:31                             Broadcast thread 31 to rows 2 and 3.
    wave_shl:1                               Wavefront left shift by 1 thread.
    wave_rol:1                               Wavefront left rotate by 1 thread.
    wave_shr:1                               Wavefront right shift by 1 thread.
    wave_ror:1                               Wavefront right rotate by 1 thread.
    row_shl:{1..15}                          Row shift left by 1-15 threads.
    row_shr:{1..15}                          Row shift right by 1-15 threads.
    row_ror:{1..15}                          Row rotate right by 1-15 threads.
    row_newbcast:{1..15}                     Broadcast a thread within a row to the whole row.
    ======================================== =========================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  quad_perm:[0, 1, 2, 3]
  row_shl:3


.. _amdgpu_synid_dpp64_ctrl:

dpp64_ctrl
~~~~~~~~~~

Specifies how data is shared between threads. This is a mandatory modifier.
There is no default value.

Note: the lanes of a wavefront are organized in four *rows* and four *banks*.

    ======================================== ==================================================
    Syntax                                   Description
    ======================================== ==================================================
    row_newbcast:{1..15}                     Broadcast a thread within a row to the whole row.
    ======================================== ==================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  row_newbcast:3


.. _amdgpu_synid_row_mask:

row_mask
~~~~~~~~

Controls which rows are enabled for data sharing. By default, all rows are enabled.

Note: the lanes of a wavefront are organized in four *rows* and four *banks*.
(There are only two rows in *wave32* mode.)

    ================= ====================================================================
    Syntax            Description
    ================= ====================================================================
    row_mask:{0..15}  Specifies a *row mask* as a positive
                      :ref:`integer number <amdgpu_synid_integer_number>`
                      or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

                      Each of the 4 bits in the mask controls one row
                      (0 - disabled, 1 - enabled).

                      In *wave32* mode, the values shall be limited to {0..7}.
    ================= ====================================================================

Examples:

.. parsed-literal::

  row_mask:0xf
  row_mask:0b1010
  row_mask:x|y

.. _amdgpu_synid_bank_mask:

bank_mask
~~~~~~~~~

Controls which banks are enabled for data sharing. By default, all banks are enabled.

Note: the lanes of a wavefront are organized in four *rows* and four *banks*.
(There are only two rows in *wave32* mode.)

    ================== ====================================================================
    Syntax             Description
    ================== ====================================================================
    bank_mask:{0..15}  Specifies a *bank mask* as a positive
                       :ref:`integer number <amdgpu_synid_integer_number>`
                       or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

                       Each of the 4 bits in the mask controls one bank
                       (0 - disabled, 1 - enabled).
    ================== ====================================================================

Examples:

.. parsed-literal::

  bank_mask:0x3
  bank_mask:0b0011
  bank_mask:x&y

.. _amdgpu_synid_bound_ctrl:

bound_ctrl
~~~~~~~~~~

Controls data sharing when accessing an invalid lane. By default, data sharing with
invalid lanes is disabled.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    bound_ctrl:1                             Enables data sharing with invalid lanes.

                                             Accessing data from an invalid lane will
                                             return zero.

    bound_ctrl:0 (GFX11+)                    Disables data sharing with invalid lanes.
    ======================================== ================================================

.. WARNING:: For historical reasons, *bound_ctrl:0* has the same meaning as *bound_ctrl:1* for older architectures.

.. _amdgpu_synid_fi16:

fi
~~

Controls interaction with *inactive* lanes for *dpp16* instructions. The default value is zero.

Note: *inactive* lanes are those whose :ref:`exec<amdgpu_synid_exec>` mask bit is zero.

    ======================================== ==================================================
    Syntax                                   Description
    ======================================== ==================================================
    fi:0                                     Interaction with inactive lanes is controlled by
                                             :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`.

    fi:1                                     Fetch pre-existing values from inactive lanes.
    ======================================== ==================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

SDWA Modifiers
--------------

clamp
~~~~~

See a description :ref:`here<amdgpu_synid_clamp>`.

omod
~~~~

See a description :ref:`here<amdgpu_synid_omod>`.

.. _amdgpu_synid_dst_sel:

dst_sel
~~~~~~~

Selects which bits in the destination are affected. By default, all bits are affected.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    dst_sel:DWORD                            Use bits 31:0.
    dst_sel:BYTE_0                           Use bits 7:0.
    dst_sel:BYTE_1                           Use bits 15:8.
    dst_sel:BYTE_2                           Use bits 23:16.
    dst_sel:BYTE_3                           Use bits 31:24.
    dst_sel:WORD_0                           Use bits 15:0.
    dst_sel:WORD_1                           Use bits 31:16.
    ======================================== ================================================

.. _amdgpu_synid_dst_unused:

dst_unused
~~~~~~~~~~

Controls what to do with the bits in the destination which are not selected
by :ref:`dst_sel<amdgpu_synid_dst_sel>`.
By default, unused bits are preserved.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    dst_unused:UNUSED_PAD                    Pad with zeros.
    dst_unused:UNUSED_SEXT                   Sign-extend upper bits, zero lower bits.
    dst_unused:UNUSED_PRESERVE               Preserve bits.
    ======================================== ================================================

.. _amdgpu_synid_src0_sel:

src0_sel
~~~~~~~~

Controls which bits in the src0 are used. By default, all bits are used.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    src0_sel:DWORD                           Use bits 31:0.
    src0_sel:BYTE_0                          Use bits 7:0.
    src0_sel:BYTE_1                          Use bits 15:8.
    src0_sel:BYTE_2                          Use bits 23:16.
    src0_sel:BYTE_3                          Use bits 31:24.
    src0_sel:WORD_0                          Use bits 15:0.
    src0_sel:WORD_1                          Use bits 31:16.
    ======================================== ================================================

.. _amdgpu_synid_src1_sel:

src1_sel
~~~~~~~~

Controls which bits in the src1 are used. By default, all bits are used.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    src1_sel:DWORD                           Use bits 31:0.
    src1_sel:BYTE_0                          Use bits 7:0.
    src1_sel:BYTE_1                          Use bits 15:8.
    src1_sel:BYTE_2                          Use bits 23:16.
    src1_sel:BYTE_3                          Use bits 31:24.
    src1_sel:WORD_0                          Use bits 15:0.
    src1_sel:WORD_1                          Use bits 31:16.
    ======================================== ================================================

.. _amdgpu_synid_sdwa_operand_modifiers:

SDWA Operand Modifiers
----------------------

Operand modifiers are not used separately. They are applied to source operands.

abs
~~~

See a description :ref:`here<amdgpu_synid_abs>`.

neg
~~~

See a description :ref:`here<amdgpu_synid_neg>`.

.. _amdgpu_synid_sext:

sext
~~~~

Sign-extends the value of a (sub-dword) integer operand to fill all 32 bits.

Valid for integer operands only.

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    sext(<operand>)                          Sign-extend operand value.
    ======================================== ================================================

Examples:

.. parsed-literal::

  sext(v4)
  sext(v255)

VOP3 Modifiers
--------------

.. _amdgpu_synid_vop3_op_sel:

op_sel
~~~~~~

Selects the low [15:0] or high [31:16] operand bits for source and destination operands.
By default, low bits are used for all operands.

The number of values specified with the op_sel modifier must match the number of instruction
operands (both source and destination). The first value controls src0, the second value controls src1
and so on, except that the last value controls destination.
The value 0 selects the low bits, while 1 selects the high bits.

Note: op_sel modifier affects 16-bit operands only. For 32-bit operands, the value specified
by op_sel must be 0.

    ======================================== ============================================================
    Syntax                                   Description
    ======================================== ============================================================
    op_sel:[{0..1},{0..1}]                   Select operand bits for instructions with 1 source operand.
    op_sel:[{0..1},{0..1},{0..1}]            Select operand bits for instructions with 2 source operands.
    op_sel:[{0..1},{0..1},{0..1},{0..1}]     Select operand bits for instructions with 3 source operands.
    ======================================== ============================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  op_sel:[0,0]
  op_sel:[0,1]

.. _amdgpu_synid_dpp_op_sel:

dpp_op_sel
~~~~~~~~~~

This is a special version of *op_sel* used for *permlane* opcodes to specify
dpp-like mode bits - :ref:`fi<amdgpu_synid_fi16>` and
:ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`.

    ======================================== =================================================================
    Syntax                                   Description
    ======================================== =================================================================
    op_sel:[{0..1},{0..1}]                   The first bit specifies :ref:`fi<amdgpu_synid_fi16>`, the second
                                             bit specifies :ref:`bound_ctrl<amdgpu_synid_bound_ctrl>`.
    ======================================== =================================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  op_sel:[0,0]

.. _amdgpu_synid_clamp:

clamp
~~~~~

Clamp meaning depends on instruction.

For *v_cmp* instructions, clamp modifier indicates that the compare signals
if a floating-point exception occurs. By default, signaling is disabled.

For integer operations, clamp modifier indicates that the result must be clamped
to the largest and smallest representable value. By default, there is no clamping.

For floating-point operations, clamp modifier indicates that the result must be clamped
to the range [0.0, 1.0]. By default, there is no clamping.

Note: clamp modifier is applied after :ref:`output modifiers<amdgpu_synid_omod>` (if any).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    clamp                                    Enables clamping (or signaling).
    ======================================== ================================================

.. _amdgpu_synid_omod:

omod
~~~~

Specifies if an output modifier must be applied to the result.
It is assumed that the result is a floating-point number.

By default, no output modifiers are applied.

Note: output modifiers are applied before :ref:`clamping<amdgpu_synid_clamp>` (if any).

    ======================================== ================================================
    Syntax                                   Description
    ======================================== ================================================
    mul:2                                    Multiply the result by 2.
    mul:4                                    Multiply the result by 4.
    div:2                                    Multiply the result by 0.5.
    ======================================== ================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  mul:2
  mul:x      // x must be equal to 2 or 4

.. _amdgpu_synid_vop3_operand_modifiers:

VOP3 Operand Modifiers
----------------------

Operand modifiers are not used separately. They are applied to source operands.

.. _amdgpu_synid_abs:

abs
~~~

Computes the absolute value of its operand. Must be applied before :ref:`neg<amdgpu_synid_neg>`
(if any). Valid for floating-point operands only.

    ======================================== ====================================================
    Syntax                                   Description
    ======================================== ====================================================
    abs(<operand>)                           Get the absolute value of a floating-point operand.
    \|<operand>|                             The same as above (an SP3 syntax).
    ======================================== ====================================================

Note: avoid using SP3 syntax with operands specified as expressions because the trailing '|'
may be misinterpreted. Such operands should be enclosed into additional parentheses, as shown
in examples below.

Examples:

.. parsed-literal::

  abs(v36)
  \|v36|
  abs(x|y)     // ok
  \|(x|y)|      // additional parentheses are required

.. _amdgpu_synid_neg:

neg
~~~

Computes the negative value of its operand. Must be applied after :ref:`abs<amdgpu_synid_abs>`
(if any). Valid for floating-point operands only.

    ================== ====================================================
    Syntax             Description
    ================== ====================================================
    neg(<operand>)     Get the negative value of a floating-point operand.
                       An optional :ref:`abs<amdgpu_synid_abs>` modifier
                       may be applied to the operand before negation.
    -<operand>         The same as above (an SP3 syntax).
    ================== ====================================================

Note: SP3 syntax is supported with limitations because of a potential ambiguity.
Currently, it is allowed in the following cases:

* Before a register.
* Before an :ref:`abs<amdgpu_synid_abs>` modifier.
* Before an SP3 :ref:`abs<amdgpu_synid_abs>` modifier.

In all other cases, "-" is handled as a part of an expression that follows the sign.

Examples:

.. parsed-literal::

  // Operands with negate modifiers
  neg(v[0])
  neg(1.0)
  neg(abs(v0))
  -v5
  -abs(v5)
  -\|v5|

  // Expressions where "-" has a different meaning
  -1
  -x+y

VOP3P Modifiers
---------------

This section describes modifiers of *regular* VOP3P instructions.

*v_mad_mix\** and *v_fma_mix\**
instructions use these modifiers :ref:`in a special manner<amdgpu_synid_mad_mix>`.

.. _amdgpu_synid_op_sel:

op_sel
~~~~~~

Selects the low [15:0] or high [31:16] operand bits as input to the operation,
which results in the lower-half of the destination.
By default, low 16 bits are used for all operands.

The number of values specified by the *op_sel* modifier must match the number of source
operands. The first value controls src0, the second value controls src1 and so on.

The value 0 selects the low bits, while 1 selects the high bits.

    ================================= =============================================================
    Syntax                            Description
    ================================= =============================================================
    op_sel:[{0..1}]                   Select operand bits for instructions with 1 source operand.
    op_sel:[{0..1},{0..1}]            Select operand bits for instructions with 2 source operands.
    op_sel:[{0..1},{0..1},{0..1}]     Select operand bits for instructions with 3 source operands.
    ================================= =============================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  op_sel:[0,0]
  op_sel:[0,1,0]

.. _amdgpu_synid_op_sel_hi:

op_sel_hi
~~~~~~~~~

Selects the low [15:0] or high [31:16] operand bits as input to the operation,
which results in the upper-half of the destination.
By default, high 16 bits are used for all operands.

The number of values specified by the *op_sel_hi* modifier must match the number of source
operands. The first value controls src0, the second value controls src1 and so on.

The value 0 selects the low bits, while 1 selects the high bits.

    =================================== =============================================================
    Syntax                              Description
    =================================== =============================================================
    op_sel_hi:[{0..1}]                  Select operand bits for instructions with 1 source operand.
    op_sel_hi:[{0..1},{0..1}]           Select operand bits for instructions with 2 source operands.
    op_sel_hi:[{0..1},{0..1},{0..1}]    Select operand bits for instructions with 3 source operands.
    =================================== =============================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  op_sel_hi:[0,0]
  op_sel_hi:[0,0,1]

.. _amdgpu_synid_neg_lo:

neg_lo
~~~~~~

Specifies whether to change the sign of operand values selected by
:ref:`op_sel<amdgpu_synid_op_sel>`. These values are then used
as input to the operation, which results in the upper-half of the destination.

The number of values specified by this modifier must match the number of source
operands. The first value controls src0, the second value controls src1 and so on.

The value 0 indicates that the corresponding operand value is used unmodified,
the value 1 indicates that the negative value of the operand must be used.

By default, operand values are used unmodified.

This modifier is valid for floating-point operands only.

    ================================ ==================================================================
    Syntax                           Description
    ================================ ==================================================================
    neg_lo:[{0..1}]                  Select affected operands for instructions with 1 source operand.
    neg_lo:[{0..1},{0..1}]           Select affected operands for instructions with 2 source operands.
    neg_lo:[{0..1},{0..1},{0..1}]    Select affected operands for instructions with 3 source operands.
    ================================ ==================================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  neg_lo:[0]
  neg_lo:[0,1]

.. _amdgpu_synid_neg_hi:

neg_hi
~~~~~~

Specifies whether to change sign of operand values selected by
:ref:`op_sel_hi<amdgpu_synid_op_sel_hi>`. These values are then used
as input to the operation, which results in the upper-half of the destination.

The number of values specified by this modifier must match the number of source
operands. The first value controls src0, the second value controls src1 and so on.

The value 0 indicates that the corresponding operand value is used unmodified,
the value 1 indicates that the negative value of the operand must be used.

By default, operand values are used unmodified.

This modifier is valid for floating-point operands only.

    =============================== ==================================================================
    Syntax                          Description
    =============================== ==================================================================
    neg_hi:[{0..1}]                 Select affected operands for instructions with 1 source operand.
    neg_hi:[{0..1},{0..1}]          Select affected operands for instructions with 2 source operands.
    neg_hi:[{0..1},{0..1},{0..1}]   Select affected operands for instructions with 3 source operands.
    =============================== ==================================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  neg_hi:[1,0]
  neg_hi:[0,1,1]

clamp
~~~~~

See a description :ref:`here<amdgpu_synid_clamp>`.

.. _amdgpu_synid_mad_mix:

VOP3P MAD_MIX/FMA_MIX Modifiers
-------------------------------

*v_mad_mix\** and *v_fma_mix\**
instructions use *op_sel* and *op_sel_hi* modifiers
in a manner different from *regular* VOP3P instructions.

See a description below.

.. _amdgpu_synid_mad_mix_op_sel:

m_op_sel
~~~~~~~~

This operand has meaning only for 16-bit source operands, as indicated by
:ref:`m_op_sel_hi<amdgpu_synid_mad_mix_op_sel_hi>`.
It specifies to select either the low [15:0] or high [31:16] operand bits
as input to the operation.

The number of values specified by the *op_sel* modifier must match the number of source
operands. The first value controls src0, the second value controls src1 and so on.

The value 0 indicates the low bits, the value 1 indicates the high 16 bits.

By default, low bits are used for all operands.

    =============================== ===================================================
    Syntax                          Description
    =============================== ===================================================
    op_sel:[{0..1},{0..1},{0..1}]   Select the location of each 16-bit source operand.
    =============================== ===================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  op_sel:[0,1]

.. _amdgpu_synid_mad_mix_op_sel_hi:

m_op_sel_hi
~~~~~~~~~~~

Selects the size of source operands: either 32 bits or 16 bits.
By default, 32 bits are used for all source operands.

The number of values specified by the *op_sel_hi* modifier must match the number of source
operands. The first value controls src0, the second value controls src1 and so on.

The value 0 indicates 32 bits, the value 1 indicates 16 bits.

The location of 16 bits in the operand may be specified by
:ref:`m_op_sel<amdgpu_synid_mad_mix_op_sel>`.

    ======================================== ========================================
    Syntax                                   Description
    ======================================== ========================================
    op_sel_hi:[{0..1},{0..1},{0..1}]         Select the size of each source operand.
    ======================================== ========================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  op_sel_hi:[1,1,1]

abs
~~~

See a description :ref:`here<amdgpu_synid_abs>`.

neg
~~~

See a description :ref:`here<amdgpu_synid_neg>`.

clamp
~~~~~

See a description :ref:`here<amdgpu_synid_clamp>`.

VOP3P MFMA Modifiers
--------------------

.. _amdgpu_synid_cbsz:

cbsz
~~~~

Specifies a broadcast mode.

    =============================== ==================================================================
    Syntax                          Description
    =============================== ==================================================================
    cbsz:[{0..7}]                   A broadcast mode.
    =============================== ==================================================================

Note: numeric value may be specified as either
an :ref:`integer number<amdgpu_synid_integer_number>` or
an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

.. _amdgpu_synid_abid:

abid
~~~~

Specifies matrix A group select.

    =============================== ==================================================================
    Syntax                          Description
    =============================== ==================================================================
    abid:[{0..15}]                  Matrix A group select id.
    =============================== ==================================================================

Note: numeric value may be specified as either
an :ref:`integer number<amdgpu_synid_integer_number>` or
an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

.. _amdgpu_synid_blgp:

blgp
~~~~

Specifies matrix B lane group pattern.

    =============================== ==================================================================
    Syntax                          Description
    =============================== ==================================================================
    blgp:[{0..7}]                   Matrix B lane group pattern.
    =============================== ==================================================================

Note: numeric value may be specified as either
an :ref:`integer number<amdgpu_synid_integer_number>` or
an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

.. _amdgpu_synid_mfma_neg:

neg
~~~

Indicates operands that must be negated before the operation.
The number of values specified by this modifier must match the number of source
operands. The first value controls src0, the second value controls src1 and so on.

The value 0 indicates that the corresponding operand value is used unmodified,
the value 1 indicates that the operand value must be negated before the operation.

By default, operand values are used unmodified.

    =============================== ==================================================================
    Syntax                          Description
    =============================== ==================================================================
    neg:[{0..1},{0..1},{0..1}]      Select operands which must be negated before the operation.
    =============================== ==================================================================

Note: numeric values may be specified as either
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Examples:

.. parsed-literal::

  neg:[0,1,1]
