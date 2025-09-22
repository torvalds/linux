=====================================
Syntax of AMDGPU Instruction Operands
=====================================

.. contents::
   :local:

Conventions
===========

The following notation is used throughout this document:

    =================== =============================================================================
    Notation            Description
    =================== =============================================================================
    {0..N}              Any integer value in the range from 0 to N (inclusive).
    <x>                 Syntax and meaning of *x* are explained elsewhere.
    =================== =============================================================================

.. _amdgpu_syn_operands:

Operands
========

.. _amdgpu_synid_v:

v (32-bit)
----------

Vector registers. There are 256 32-bit vector registers.

A sequence of *vector* registers may be used to operate with more than 32 bits of data.

Assembler currently supports tuples with 1 to 12, 16 and 32 *vector* registers.

    =================================================== ====================================================================
    Syntax                                              Description
    =================================================== ====================================================================
    **v**\<N>                                           A single 32-bit *vector* register.

                                                        *N* must be a decimal
                                                        :ref:`integer number<amdgpu_synid_integer_number>`.
    **v[**\ <N>\ **]**                                  A single 32-bit *vector* register.

                                                        *N* may be specified as an
                                                        :ref:`integer number<amdgpu_synid_integer_number>`
                                                        or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    **v[**\ <N>:<K>\ **]**                              A sequence of (\ *K-N+1*\ ) *vector* registers.

                                                        *N* and *K* may be specified as
                                                        :ref:`integer numbers<amdgpu_synid_integer_number>`
                                                        or :ref:`absolute expressions<amdgpu_synid_absolute_expression>`.
    **[v**\ <N>, \ **v**\ <N+1>, ... **v**\ <K>\ **]**  A sequence of (\ *K-N+1*\ ) *vector* registers.

                                                        Register indices must be specified as decimal
                                                        :ref:`integer numbers<amdgpu_synid_integer_number>`.
    =================================================== ====================================================================

Note: *N* and *K* must satisfy the following conditions:

* *N* <= *K*.
* 0 <= *N* <= 255.
* 0 <= *K* <= 255.
* *K-N+1* must be in the range from 1 to 12 or equal to 16 or 32.

GFX90A and GFX940 have an additional alignment requirement:
pairs of *vector* registers must be even-aligned
(first register must be even).

Examples:

.. parsed-literal::

  v255
  v[0]
  v[0:1]
  v[1:1]
  v[0:3]
  v[2*2]
  v[1-1:2-1]
  [v252]
  [v252,v253,v254,v255]

.. _amdgpu_synid_nsa:

**Non-Sequential Address (NSA) Syntax**

GFX10+ *image* instructions may use special *NSA* (Non-Sequential Address)
syntax for *image addresses*:

    ===================================== =================================================
    Syntax                                Description
    ===================================== =================================================
    **[Vm**, \ **Vn**, ... **Vk**\ **]**  A sequence of 32-bit *vector* registers.
                                          Each register may be specified using the syntax
                                          defined :ref:`above<amdgpu_synid_v>`.

                                          In contrast with the standard syntax, registers
                                          in *NSA* sequence are not required to have
                                          consecutive indices. Moreover, the same register
                                          may appear in the sequence more than once.

                                          GFX11+ has an additional limitation: if address
                                          size occupies more than 5 dwords, registers
                                          starting from the 5th element must be contiguous.
    ===================================== =================================================

Examples:

.. parsed-literal::

  [v32,v1,v[2]]
  [v[32],v[1:1],[v2]]
  [v4,v4,v4,v4]

.. _amdgpu_synid_v16:

v (16-bit)
----------

16-bit vector registers. Each :ref:`32-bit vector register<amdgpu_synid_v>` is divided into two 16-bit low and high registers, so there are 512 16-bit vector registers.

Only VOP3, VOP3P and VINTERP instructions may access all 512 registers (using :ref:`op_sel<amdgpu_synid_op_sel>` modifier).
VOP1, VOP2 and VOPC instructions may currently access only 128 low 16-bit registers using the syntax described below.

.. WARNING:: This section is incomplete. The support of 16-bit registers in the assembler is still WIP.

\
    =================================================== ====================================================================
    Syntax                                              Description
    =================================================== ====================================================================
    **v**\<N>                                           A single 16-bit *vector* register (low half).
    =================================================== ====================================================================

Note: *N* must satisfy the following conditions:

* 0 <= *N* <= 127.

Examples:

.. parsed-literal::

  v127

.. _amdgpu_synid_a:

a
-

Accumulator registers. There are 256 32-bit accumulator registers.

A sequence of *accumulator* registers may be used to operate with more than 32 bits of data.

Assembler currently supports tuples with 1 to 12, 16 and 32 *accumulator* registers.

    =================================================== ========================================================= ====================================================================
    Syntax                                              Alternative Syntax (SP3)                                  Description
    =================================================== ========================================================= ====================================================================
    **a**\<N>                                           **acc**\<N>                                               A single 32-bit *accumulator* register.

                                                                                                                  *N* must be a decimal
                                                                                                                  :ref:`integer number<amdgpu_synid_integer_number>`.
    **a[**\ <N>\ **]**                                  **acc[**\ <N>\ **]**                                      A single 32-bit *accumulator* register.

                                                                                                                  *N* may be specified as an
                                                                                                                  :ref:`integer number<amdgpu_synid_integer_number>`
                                                                                                                  or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    **a[**\ <N>:<K>\ **]**                              **acc[**\ <N>:<K>\ **]**                                  A sequence of (\ *K-N+1*\ ) *accumulator* registers.

                                                                                                                  *N* and *K* may be specified as
                                                                                                                  :ref:`integer numbers<amdgpu_synid_integer_number>`
                                                                                                                  or :ref:`absolute expressions<amdgpu_synid_absolute_expression>`.
    **[a**\ <N>, \ **a**\ <N+1>, ... **a**\ <K>\ **]**  **[acc**\ <N>, \ **acc**\ <N+1>, ... **acc**\ <K>\ **]**  A sequence of (\ *K-N+1*\ ) *accumulator* registers.

                                                                                                                  Register indices must be specified as decimal
                                                                                                                  :ref:`integer numbers<amdgpu_synid_integer_number>`.
    =================================================== ========================================================= ====================================================================

Note: *N* and *K* must satisfy the following conditions:

* *N* <= *K*.
* 0 <= *N* <= 255.
* 0 <= *K* <= 255.
* *K-N+1* must be in the range from 1 to 12 or equal to 16 or 32.

GFX90A and GFX940 have an additional alignment requirement:
pairs of *accumulator* registers must be even-aligned
(first register must be even).

Examples:

.. parsed-literal::

  a255
  a[0]
  a[0:1]
  a[1:1]
  a[0:3]
  a[2*2]
  a[1-1:2-1]
  [a252]
  [a252,a253,a254,a255]

  acc0
  acc[1]
  [acc250]
  [acc2,acc3]

.. _amdgpu_synid_s:

s
-

Scalar 32-bit registers. The number of available *scalar* registers depends on the GPU:

    ======= ============================
    GPU     Number of *scalar* registers
    ======= ============================
    GFX7    104
    GFX8    102
    GFX9    102
    GFX10+  106
    ======= ============================

A sequence of *scalar* registers may be used to operate with more than 32 bits of data.
Assembler currently supports tuples with 1 to 12, 16 and 32 *scalar* registers.

Pairs of *scalar* registers must be even-aligned (first register must be even).
Sequences of 4 and more *scalar* registers must be quad-aligned.

    ======================================================== ====================================================================
    Syntax                                                   Description
    ======================================================== ====================================================================
    **s**\ <N>                                               A single 32-bit *scalar* register.

                                                             *N* must be a decimal
                                                             :ref:`integer number<amdgpu_synid_integer_number>`.

    **s[**\ <N>\ **]**                                       A single 32-bit *scalar* register.

                                                             *N* may be specified as an
                                                             :ref:`integer number<amdgpu_synid_integer_number>`
                                                             or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    **s[**\ <N>:<K>\ **]**                                   A sequence of (\ *K-N+1*\ ) *scalar* registers.

                                                             *N* and *K* may be specified as
                                                             :ref:`integer numbers<amdgpu_synid_integer_number>`
                                                             or :ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

    **[s**\ <N>, \ **s**\ <N+1>, ... **s**\ <K>\ **]**       A sequence of (\ *K-N+1*\ ) *scalar* registers.

                                                             Register indices must be specified as decimal
                                                             :ref:`integer numbers<amdgpu_synid_integer_number>`.
    ======================================================== ====================================================================

Note: *N* and *K* must satisfy the following conditions:

* *N* must be properly aligned based on the sequence size.
* *N* <= *K*.
* 0 <= *N* < *SMAX*\ , where *SMAX* is the number of available *scalar* registers.
* 0 <= *K* < *SMAX*\ , where *SMAX* is the number of available *scalar* registers.
* *K-N+1* must be in the range from 1 to 12 or equal to 16 or 32.

Examples:

.. parsed-literal::

  s0
  s[0]
  s[0:1]
  s[1:1]
  s[0:3]
  s[2*2]
  s[1-1:2-1]
  [s4]
  [s4,s5,s6,s7]

Examples of *scalar* registers with an invalid alignment:

.. parsed-literal::

  s[1:2]
  s[2:5]

.. _amdgpu_synid_trap:

trap
----

A set of trap handler registers:

* :ref:`ttmp<amdgpu_synid_ttmp>`
* :ref:`tba<amdgpu_synid_tba>`
* :ref:`tma<amdgpu_synid_tma>`

.. _amdgpu_synid_ttmp:

ttmp
----

Trap handler temporary scalar registers, 32-bits wide.
The number of available *ttmp* registers depends on the GPU:

    ======= ===========================
    GPU     Number of *ttmp* registers
    ======= ===========================
    GFX7    12
    GFX8    12
    GFX9    16
    GFX10+  16
    ======= ===========================

A sequence of *ttmp* registers may be used to operate with more than 32 bits of data.
Assembler currently supports tuples with 1 to 12 and 16 *ttmp* registers.

Pairs of *ttmp* registers must be even-aligned (first register must be even).
Sequences of 4 and more *ttmp* registers must be quad-aligned.

    ============================================================= ====================================================================
    Syntax                                                        Description
    ============================================================= ====================================================================
    **ttmp**\ <N>                                                 A single 32-bit *ttmp* register.

                                                                  *N* must be a decimal
                                                                  :ref:`integer number<amdgpu_synid_integer_number>`.
    **ttmp[**\ <N>\ **]**                                         A single 32-bit *ttmp* register.

                                                                  *N* may be specified as an
                                                                  :ref:`integer number<amdgpu_synid_integer_number>`
                                                                  or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
    **ttmp[**\ <N>:<K>\ **]**                                     A sequence of (\ *K-N+1*\ ) *ttmp* registers.

                                                                  *N* and *K* may be specified as
                                                                  :ref:`integer numbers<amdgpu_synid_integer_number>`
                                                                  or :ref:`absolute expressions<amdgpu_synid_absolute_expression>`.
    **[ttmp**\ <N>, \ **ttmp**\ <N+1>, ... **ttmp**\ <K>\ **]**   A sequence of (\ *K-N+1*\ ) *ttmp* registers.

                                                                  Register indices must be specified as decimal
                                                                  :ref:`integer numbers<amdgpu_synid_integer_number>`.
    ============================================================= ====================================================================

Note: *N* and *K* must satisfy the following conditions:

* *N* must be properly aligned based on the sequence size.
* *N* <= *K*.
* 0 <= *N* < *TMAX*, where *TMAX* is the number of available *ttmp* registers.
* 0 <= *K* < *TMAX*, where *TMAX* is the number of available *ttmp* registers.
* *K-N+1* must be in the range from 1 to 12 or equal to 16.

Examples:

.. parsed-literal::

  ttmp0
  ttmp[0]
  ttmp[0:1]
  ttmp[1:1]
  ttmp[0:3]
  ttmp[2*2]
  ttmp[1-1:2-1]
  [ttmp4]
  [ttmp4,ttmp5,ttmp6,ttmp7]

Examples of *ttmp* registers with an invalid alignment:

.. parsed-literal::

  ttmp[1:2]
  ttmp[2:5]

.. _amdgpu_synid_tba:

tba
---

Trap base address, 64-bits wide. Holds the pointer to the current
trap handler program.

    ================== ======================================================================= =============
    Syntax             Description                                                             Availability
    ================== ======================================================================= =============
    tba                64-bit *trap base address* register.                                    GFX7, GFX8
    [tba]              64-bit *trap base address* register (an SP3 syntax).                    GFX7, GFX8
    [tba_lo,tba_hi]    64-bit *trap base address* register (an SP3 syntax).                    GFX7, GFX8
    ================== ======================================================================= =============

High and low 32 bits of *trap base address* may be accessed as separate registers:

    ================== ======================================================================= =============
    Syntax             Description                                                             Availability
    ================== ======================================================================= =============
    tba_lo             Low 32 bits of *trap base address* register.                            GFX7, GFX8
    tba_hi             High 32 bits of *trap base address* register.                           GFX7, GFX8
    [tba_lo]           Low 32 bits of *trap base address* register (an SP3 syntax).            GFX7, GFX8
    [tba_hi]           High 32 bits of *trap base address* register (an SP3 syntax).           GFX7, GFX8
    ================== ======================================================================= =============

.. _amdgpu_synid_tma:

tma
---

Trap memory address, 64-bits wide.

    ================= ======================================================================= ==================
    Syntax            Description                                                             Availability
    ================= ======================================================================= ==================
    tma               64-bit *trap memory address* register.                                  GFX7, GFX8
    [tma]             64-bit *trap memory address* register (an SP3 syntax).                  GFX7, GFX8
    [tma_lo,tma_hi]   64-bit *trap memory address* register (an SP3 syntax).                  GFX7, GFX8
    ================= ======================================================================= ==================

High and low 32 bits of *trap memory address* may be accessed as separate registers:

    ================= ======================================================================= ==================
    Syntax            Description                                                             Availability
    ================= ======================================================================= ==================
    tma_lo            Low 32 bits of *trap memory address* register.                          GFX7, GFX8
    tma_hi            High 32 bits of *trap memory address* register.                         GFX7, GFX8
    [tma_lo]          Low 32 bits of *trap memory address* register (an SP3 syntax).          GFX7, GFX8
    [tma_hi]          High 32 bits of *trap memory address* register (an SP3 syntax).         GFX7, GFX8
    ================= ======================================================================= ==================

.. _amdgpu_synid_flat_scratch:

flat_scratch
------------

Flat scratch address, 64-bits wide. Holds the base address of scratch memory.

    ================================== ================================================================
    Syntax                             Description
    ================================== ================================================================
    flat_scratch                       64-bit *flat scratch* address register.
    [flat_scratch]                     64-bit *flat scratch* address register (an SP3 syntax).
    [flat_scratch_lo,flat_scratch_hi]  64-bit *flat scratch* address register (an SP3 syntax).
    ================================== ================================================================

High and low 32 bits of *flat scratch* address may be accessed as separate registers:

    ========================= =========================================================================
    Syntax                    Description
    ========================= =========================================================================
    flat_scratch_lo           Low 32 bits of *flat scratch* address register.
    flat_scratch_hi           High 32 bits of *flat scratch* address register.
    [flat_scratch_lo]         Low 32 bits of *flat scratch* address register (an SP3 syntax).
    [flat_scratch_hi]         High 32 bits of *flat scratch* address register (an SP3 syntax).
    ========================= =========================================================================

.. _amdgpu_synid_xnack:
.. _amdgpu_synid_xnack_mask:

xnack_mask
----------

Xnack mask, 64-bits wide. Holds a 64-bit mask of which threads
received an *XNACK* due to a vector memory operation.

For availability of *xnack* feature, refer to :ref:`this table<amdgpu-processors>`.

    ============================== =====================================================
    Syntax                         Description
    ============================== =====================================================
    xnack_mask                     64-bit *xnack mask* register.
    [xnack_mask]                   64-bit *xnack mask* register (an SP3 syntax).
    [xnack_mask_lo,xnack_mask_hi]  64-bit *xnack mask* register (an SP3 syntax).
    ============================== =====================================================

High and low 32 bits of *xnack mask* may be accessed as separate registers:

    ===================== ==============================================================
    Syntax                Description
    ===================== ==============================================================
    xnack_mask_lo         Low 32 bits of *xnack mask* register.
    xnack_mask_hi         High 32 bits of *xnack mask* register.
    [xnack_mask_lo]       Low 32 bits of *xnack mask* register (an SP3 syntax).
    [xnack_mask_hi]       High 32 bits of *xnack mask* register (an SP3 syntax).
    ===================== ==============================================================

.. _amdgpu_synid_vcc:
.. _amdgpu_synid_vcc_lo:

vcc
---

Vector condition code, 64-bits wide. A bit mask with one bit per thread;
it holds the result of a vector compare operation.

Note that GFX10+ H/W does not use high 32 bits of *vcc* in *wave32* mode.

    ================ =========================================================================
    Syntax           Description
    ================ =========================================================================
    vcc              64-bit *vector condition code* register.
    [vcc]            64-bit *vector condition code* register (an SP3 syntax).
    [vcc_lo,vcc_hi]  64-bit *vector condition code* register (an SP3 syntax).
    ================ =========================================================================

High and low 32 bits of *vector condition code* may be accessed as separate registers:

    ================ =========================================================================
    Syntax           Description
    ================ =========================================================================
    vcc_lo           Low 32 bits of *vector condition code* register.
    vcc_hi           High 32 bits of *vector condition code* register.
    [vcc_lo]         Low 32 bits of *vector condition code* register (an SP3 syntax).
    [vcc_hi]         High 32 bits of *vector condition code* register (an SP3 syntax).
    ================ =========================================================================

.. _amdgpu_synid_m0:

m0
--

A 32-bit memory register. It has various uses,
including register indexing and bounds checking.

    =========== ===================================================
    Syntax      Description
    =========== ===================================================
    m0          A 32-bit *memory* register.
    [m0]        A 32-bit *memory* register (an SP3 syntax).
    =========== ===================================================

.. _amdgpu_synid_exec:

exec
----

Execute mask, 64-bits wide. A bit mask with one bit per thread,
which is applied to vector instructions and controls which threads execute
and which ignore the instruction.

Note that GFX10+ H/W does not use high 32 bits of *exec* in *wave32* mode.

    ===================== =================================================================
    Syntax                Description
    ===================== =================================================================
    exec                  64-bit *execute mask* register.
    [exec]                64-bit *execute mask* register (an SP3 syntax).
    [exec_lo,exec_hi]     64-bit *execute mask* register (an SP3 syntax).
    ===================== =================================================================

High and low 32 bits of *execute mask* may be accessed as separate registers:

    ===================== =================================================================
    Syntax                Description
    ===================== =================================================================
    exec_lo               Low 32 bits of *execute mask* register.
    exec_hi               High 32 bits of *execute mask* register.
    [exec_lo]             Low 32 bits of *execute mask* register (an SP3 syntax).
    [exec_hi]             High 32 bits of *execute mask* register (an SP3 syntax).
    ===================== =================================================================

.. _amdgpu_synid_vccz:

vccz
----

A single bit flag indicating that the :ref:`vcc<amdgpu_synid_vcc>`
is all zeros.

Note: when GFX10+ operates in *wave32* mode, this register reflects
the state of :ref:`vcc_lo<amdgpu_synid_vcc_lo>`.

.. _amdgpu_synid_execz:

execz
-----

A single bit flag indicating that the :ref:`exec<amdgpu_synid_exec>`
is all zeros.

Note: when GFX10+ operates in *wave32* mode, this register reflects
the state of :ref:`exec_lo<amdgpu_synid_exec>`.

.. _amdgpu_synid_scc:

scc
---

A single bit flag indicating the result of a scalar compare operation.

.. _amdgpu_synid_lds_direct:

lds_direct
----------

A special operand which supplies a 32-bit value
fetched from *LDS* memory using :ref:`m0<amdgpu_synid_m0>` as an address.

.. _amdgpu_synid_null:

null
----

This is a special operand that may be used as a source or a destination.

When used as a destination, the result of the operation is discarded.

When used as a source, it supplies zero value.

.. _amdgpu_synid_constant:

inline constant
---------------

An *inline constant* is an integer or a floating-point value
encoded as a part of an instruction. Compare *inline constants*
with :ref:`literals<amdgpu_synid_literal>`.

Inline constants include:

* :ref:`Integer inline constants<amdgpu_synid_iconst>`;
* :ref:`Floating-point inline constants<amdgpu_synid_fconst>`;
* :ref:`Inline values<amdgpu_synid_ival>`.

If a number may be encoded as either
a :ref:`literal<amdgpu_synid_literal>` or
a :ref:`constant<amdgpu_synid_constant>`,
the assembler selects the latter encoding as more efficient.

.. _amdgpu_synid_iconst:

iconst
~~~~~~

An :ref:`integer number<amdgpu_synid_integer_number>` or
an :ref:`absolute expression<amdgpu_synid_absolute_expression>`
encoded as an *inline constant*.

Only a small fraction of integer numbers may be encoded as *inline constants*.
They are enumerated in the table below.
Other integer numbers are encoded as :ref:`literals<amdgpu_synid_literal>`.

    ================================== ====================================
    Value                              Note
    ================================== ====================================
    {0..64}                            Positive integer inline constants.
    {-16..-1}                          Negative integer inline constants.
    ================================== ====================================

.. _amdgpu_synid_fconst:

fconst
~~~~~~

A :ref:`floating-point number<amdgpu_synid_floating-point_number>`
encoded as an *inline constant*.

Only a small fraction of floating-point numbers may be encoded
as *inline constants*. They are enumerated in the table below.
Other floating-point numbers are encoded as
:ref:`literals<amdgpu_synid_literal>`.

    ===================== ===================================================== ==================
    Value                 Note                                                  Availability
    ===================== ===================================================== ==================
    0.0                   The same as integer constant 0.                       All GPUs
    0.5                   Floating-point constant 0.5                           All GPUs
    1.0                   Floating-point constant 1.0                           All GPUs
    2.0                   Floating-point constant 2.0                           All GPUs
    4.0                   Floating-point constant 4.0                           All GPUs
    -0.5                  Floating-point constant -0.5                          All GPUs
    -1.0                  Floating-point constant -1.0                          All GPUs
    -2.0                  Floating-point constant -2.0                          All GPUs
    -4.0                  Floating-point constant -4.0                          All GPUs
    0.1592                1.0/(2.0*pi). Use only for 16-bit operands.           GFX8+
    0.15915494            1.0/(2.0*pi). Use only for 16- and 32-bit operands.   GFX8+
    0.15915494309189532   1.0/(2.0*pi).                                         GFX8+
    ===================== ===================================================== ==================

.. WARNING:: Floating-point inline constants cannot be used with *16-bit integer* operands. \
             Assembler encodes these values as literals.

.. _amdgpu_synid_ival:

ival
~~~~

A symbolic operand encoded as an *inline constant*.
These operands provide read-only access to H/W registers.

    ===================== ========================= ================================================ =============
    Syntax                Alternative Syntax (SP3)  Note                                             Availability
    ===================== ========================= ================================================ =============
    shared_base           src_shared_base           Base address of shared memory region.            GFX9+
    shared_limit          src_shared_limit          Address of the end of shared memory region.      GFX9+
    private_base          src_private_base          Base address of private memory region.           GFX9+
    private_limit         src_private_limit         Address of the end of private memory region.     GFX9+
    pops_exiting_wave_id  src_pops_exiting_wave_id  A dedicated counter for POPS.                    GFX9, GFX10
    ===================== ========================= ================================================ =============

.. _amdgpu_synid_literal:

literal
-------

A *literal* is a 64-bit value encoded as a separate
32-bit dword in the instruction stream. Compare *literals*
with :ref:`inline constants<amdgpu_synid_constant>`.

If a number may be encoded as either
a :ref:`literal<amdgpu_synid_literal>` or
an :ref:`inline constant<amdgpu_synid_constant>`,
assembler selects the latter encoding as more efficient.

Literals may be specified as
:ref:`integer numbers<amdgpu_synid_integer_number>`,
:ref:`floating-point numbers<amdgpu_synid_floating-point_number>`,
:ref:`absolute expressions<amdgpu_synid_absolute_expression>` or
:ref:`relocatable expressions<amdgpu_synid_relocatable_expression>`.

An instruction may use only one literal,
but several operands may refer to the same literal.

.. _amdgpu_synid_uimm8:

uimm8
-----

An 8-bit :ref:`integer number<amdgpu_synid_integer_number>`
or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
The value must be in the range 0..0xFF.

.. _amdgpu_synid_uimm32:

uimm32
------

A 32-bit :ref:`integer number<amdgpu_synid_integer_number>`
or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.
The value must be in the range 0..0xFFFFFFFF.

.. _amdgpu_synid_uimm20:

uimm20
------

A 20-bit :ref:`integer number<amdgpu_synid_integer_number>`
or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

The value must be in the range 0..0xFFFFF.

.. _amdgpu_synid_simm21:

simm21
------

A 21-bit :ref:`integer number<amdgpu_synid_integer_number>`
or an :ref:`absolute expression<amdgpu_synid_absolute_expression>`.

The value must be in the range -0x100000..0x0FFFFF.

.. _amdgpu_synid_off:

off
---

A special entity which indicates that the value of this operand is not used.

    ================================== ===================================================
    Syntax                             Description
    ================================== ===================================================
    off                                Indicates an unused operand.
    ================================== ===================================================


.. _amdgpu_synid_number:

Numbers
=======

.. _amdgpu_synid_integer_number:

Integer Numbers
---------------

Integer numbers are 64 bits wide.
They are converted to :ref:`expected operand type<amdgpu_syn_instruction_type>`
as described :ref:`here<amdgpu_synid_int_conv>`.

Integer numbers may be specified in binary, octal,
hexadecimal and decimal formats:

    ============ =============================== ========
    Format       Syntax                          Example
    ============ =============================== ========
    Decimal      [-]?[1-9][0-9]*                 -1234
    Binary       [-]?0b[01]+                     0b1010
    Octal        [-]?0[0-7]+                     010
    Hexadecimal  [-]?0x[0-9a-fA-F]+              0xff
    \            [-]?[0x]?[0-9][0-9a-fA-F]*[hH]  0ffh
    ============ =============================== ========

.. _amdgpu_synid_floating-point_number:

Floating-Point Numbers
----------------------

All floating-point numbers are handled as double (64 bits wide).
They are converted to
:ref:`expected operand type<amdgpu_syn_instruction_type>`
as described :ref:`here<amdgpu_synid_fp_conv>`.

Floating-point numbers may be specified in hexadecimal and decimal formats:

    ============ ======================================================== ====================== ====================
    Format       Syntax                                                   Examples               Note
    ============ ======================================================== ====================== ====================
    Decimal      [-]?[0-9]*[.][0-9]*([eE][+-]?[0-9]*)?                    -1.234, 234e2          Must include either
                                                                                                 a decimal separator
                                                                                                 or an exponent.
    Hexadecimal  [-]0x[0-9a-fA-F]*(.[0-9a-fA-F]*)?[pP][+-]?[0-9a-fA-F]+   -0x1afp-10, 0x.1afp10
    ============ ======================================================== ====================== ====================

.. _amdgpu_synid_expression:

Expressions
===========

An expression is evaluated to a 64-bit integer.
Note that floating-point expressions are not supported.

There are two kinds of expressions:

* :ref:`Absolute<amdgpu_synid_absolute_expression>`.
* :ref:`Relocatable<amdgpu_synid_relocatable_expression>`.

.. _amdgpu_synid_absolute_expression:

Absolute Expressions
--------------------

The value of an absolute expression does not change after program relocation.
Absolute expressions must not include unassigned and relocatable values
such as labels.

Absolute expressions are evaluated to 64-bit integer values and converted to
:ref:`expected operand type<amdgpu_syn_instruction_type>`
as described :ref:`here<amdgpu_synid_int_conv>`.

Examples:

.. parsed-literal::

    x = -1
    y = x + 10

.. _amdgpu_synid_relocatable_expression:

Relocatable Expressions
-----------------------

The value of a relocatable expression depends on program relocation.

Note that use of relocatable expressions is limited to branch targets
and 32-bit integer operands.

A relocatable expression is evaluated to a 64-bit integer value,
which depends on operand kind and
:ref:`relocation type<amdgpu-relocation-records>` of symbol(s)
used in the expression. For example, if an instruction refers to a label,
this reference is evaluated to an offset from the address after
the instruction to the label address:

.. parsed-literal::

    label:
    v_add_co_u32_e32 v0, vcc, label, v1  // 'label' operand is evaluated to -4

Note that values of relocatable expressions are usually unknown
at assembly time; they are resolved later by a linker and converted to
:ref:`expected operand type<amdgpu_syn_instruction_type>`
as described :ref:`here<amdgpu_synid_rl_conv>`.

Operands and Operations
-----------------------

Expressions are composed of 64-bit integer operands and operations.
Operands include :ref:`integer numbers<amdgpu_synid_integer_number>`
and :ref:`symbols<amdgpu_synid_symbol>`.

Expressions may also use "." which is a reference
to the current PC (program counter).

:ref:`Unary<amdgpu_synid_expression_un_op>` and
:ref:`binary<amdgpu_synid_expression_bin_op>`
operations produce 64-bit integer results.

Syntax of Expressions
---------------------

Syntax of expressions is shown below::

    expr ::= expr binop expr | primaryexpr ;

    primaryexpr ::= '(' expr ')' | symbol | number | '.' | unop primaryexpr ;

    binop ::= '&&'
            | '||'
            | '|'
            | '^'
            | '&'
            | '!'
            | '=='
            | '!='
            | '<>'
            | '<'
            | '<='
            | '>'
            | '>='
            | '<<'
            | '>>'
            | '+'
            | '-'
            | '*'
            | '/'
            | '%' ;

    unop ::= '~'
           | '+'
           | '-'
           | '!' ;

.. _amdgpu_synid_expression_bin_op:

Binary Operators
----------------

Binary operators are described in the following table.
They operate on and produce 64-bit integers.
Operators with higher priority are performed first.

    ========== ========= ===============================================
    Operator   Priority  Meaning
    ========== ========= ===============================================
       \*         5      Integer multiplication.
       /          5      Integer division.
       %          5      Integer signed remainder.
       \+         4      Integer addition.
       \-         4      Integer subtraction.
       <<         3      Integer shift left.
       >>         3      Logical shift right.
       ==         2      Equality comparison.
       !=         2      Inequality comparison.
       <>         2      Inequality comparison.
       <          2      Signed less than comparison.
       <=         2      Signed less than or equal comparison.
       >          2      Signed greater than comparison.
       >=         2      Signed greater than or equal comparison.
      \|          1      Bitwise or.
       ^          1      Bitwise xor.
       &          1      Bitwise and.
       &&         0      Logical and.
       ||         0      Logical or.
    ========== ========= ===============================================

.. _amdgpu_synid_expression_un_op:

Unary Operators
---------------

Unary operators are described in the following table.
They operate on and produce 64-bit integers.

    ========== ===============================================
    Operator   Meaning
    ========== ===============================================
       !       Logical negation.
       ~       Bitwise negation.
       \+      Integer unary plus.
       \-      Integer unary minus.
    ========== ===============================================

.. _amdgpu_synid_symbol:

Symbols
-------

A symbol is a named 64-bit integer value, representing a relocatable
address or an absolute (non-relocatable) number.

Symbol names have the following syntax:
    ``[a-zA-Z_.][a-zA-Z0-9_$.@]*``

The table below provides several examples of syntax used for symbol definition.

    ================ ==========================================================
    Syntax           Meaning
    ================ ==========================================================
    .globl <S>       Declares a global symbol S without assigning it a value.
    .set <S>, <E>    Assigns the value of an expression E to a symbol S.
    <S> = <E>        Assigns the value of an expression E to a symbol S.
    <S>:             Declares a label S and assigns it the current PC value.
    ================ ==========================================================

A symbol may be used before it is declared or assigned;
unassigned symbols are assumed to be PC-relative.

Additional information about symbols may be found :ref:`here<amdgpu-symbols>`.

.. _amdgpu_synid_conv:

Type and Size Conversion
========================

This section describes what happens when a 64-bit
:ref:`integer number<amdgpu_synid_integer_number>`, a
:ref:`floating-point number<amdgpu_synid_floating-point_number>` or an
:ref:`expression<amdgpu_synid_expression>`
is used for an operand which has a different type or size.

.. _amdgpu_synid_int_conv:

Conversion of Integer Values
----------------------------

Instruction operands may be specified as 64-bit
:ref:`integer numbers<amdgpu_synid_integer_number>` or
:ref:`absolute expressions<amdgpu_synid_absolute_expression>`.
These values are converted to the
:ref:`expected operand type<amdgpu_syn_instruction_type>`
using the following steps:

1. *Validation*. Assembler checks if the input value may be truncated
without loss to the required *truncation width* (see the table below).
There are two cases when this operation is enabled:

    * The truncated bits are all 0.
    * The truncated bits are all 1 and the value after truncation has its MSB bit set.

In all other cases, the assembler triggers an error.

2. *Conversion*. The input value is converted to the expected type
as described in the table below. Depending on operand kind, this conversion
is performed by either assembler or AMDGPU H/W (or both).

    ============== ================= =============== ====================================================================
    Expected type  Truncation Width  Conversion      Description
    ============== ================= =============== ====================================================================
    i16, u16, b16  16                num.u16         Truncate to 16 bits.
    i32, u32, b32  32                num.u32         Truncate to 32 bits.
    i64            32                {-1,num.i32}    Truncate to 32 bits and then sign-extend the result to 64 bits.
    u64, b64       32                {0,num.u32}     Truncate to 32 bits and then zero-extend the result to 64 bits.
    f16            16                num.u16         Use low 16 bits as an f16 value.
    f32            32                num.u32         Use low 32 bits as an f32 value.
    f64            32                {num.u32,0}     Use low 32 bits of the number as high 32 bits
                                                     of the result; low 32 bits of the result are zeroed.
    ============== ================= =============== ====================================================================

Examples of enabled conversions:

.. parsed-literal::

    // GFX9

    v_add_u16 v0, -1, 0                   // src0 = 0xFFFF
    v_add_f16 v0, -1, 0                   // src0 = 0xFFFF (NaN)
                                          //
    v_add_u32 v0, -1, 0                   // src0 = 0xFFFFFFFF
    v_add_f32 v0, -1, 0                   // src0 = 0xFFFFFFFF (NaN)
                                          //
    v_add_u16 v0, 0xff00, v0              // src0 = 0xff00
    v_add_u16 v0, 0xffffffffffffff00, v0  // src0 = 0xff00
    v_add_u16 v0, -256, v0                // src0 = 0xff00
                                          //
    s_bfe_i64 s[0:1], 0xffefffff, s3      // src0 = 0xffffffffffefffff
    s_bfe_u64 s[0:1], 0xffefffff, s3      // src0 = 0x00000000ffefffff
    v_ceil_f64_e32 v[0:1], 0xffefffff     // src0 = 0xffefffff00000000 (-1.7976922776554302e308)
                                          //
    x = 0xffefffff                        //
    s_bfe_i64 s[0:1], x, s3               // src0 = 0xffffffffffefffff
    s_bfe_u64 s[0:1], x, s3               // src0 = 0x00000000ffefffff
    v_ceil_f64_e32 v[0:1], x              // src0 = 0xffefffff00000000 (-1.7976922776554302e308)

Examples of disabled conversions:

.. parsed-literal::

    // GFX9

    v_add_u16 v0, 0x1ff00, v0               // truncated bits are not all 0 or 1
    v_add_u16 v0, 0xffffffffffff00ff, v0    // truncated bits do not match MSB of the result

.. _amdgpu_synid_fp_conv:

Conversion of Floating-Point Values
-----------------------------------

Instruction operands may be specified as 64-bit
:ref:`floating-point numbers<amdgpu_synid_floating-point_number>`.
These values are converted to the
:ref:`expected operand type<amdgpu_syn_instruction_type>`
using the following steps:

1. *Validation*. Assembler checks if the input f64 number can be converted
to the *required floating-point type* (see the table below) without overflow
or underflow. Precision lost is allowed. If this conversion is not possible,
the assembler triggers an error.

2. *Conversion*. The input value is converted to the expected type
as described in the table below. Depending on operand kind, this is
performed by either assembler or AMDGPU H/W (or both).

    ============== ================ ================= =================================================================
    Expected type  Required FP Type Conversion        Description
    ============== ================ ================= =================================================================
    i16, u16, b16  f16              f16(num)          Convert to f16 and use bits of the result as an integer value.
                                                      The value has to be encoded as a literal, or an error occurs.
                                                      Note that the value cannot be encoded as an inline constant.
    i32, u32, b32  f32              f32(num)          Convert to f32 and use bits of the result as an integer value.
    i64, u64, b64  \-               \-                Conversion disabled.
    f16            f16              f16(num)          Convert to f16.
    f32            f32              f32(num)          Convert to f32.
    f64            f64              {num.u32.hi,0}    Use high 32 bits of the number as high 32 bits of the result;
                                                      zero-fill low 32 bits of the result.

                                                      Note that the result may differ from the original number.
    ============== ================ ================= =================================================================

Examples of enabled conversions:

.. parsed-literal::

    // GFX9

    v_add_f16 v0, 1.0, 0        // src0 = 0x3C00 (1.0)
    v_add_u16 v0, 1.0, 0        // src0 = 0x3C00
                                //
    v_add_f32 v0, 1.0, 0        // src0 = 0x3F800000 (1.0)
    v_add_u32 v0, 1.0, 0        // src0 = 0x3F800000

                                // src0 before conversion:
                                //   1.7976931348623157e308 = 0x7fefffffffffffff
                                // src0 after conversion:
                                //   1.7976922776554302e308 = 0x7fefffff00000000
    v_ceil_f64 v[0:1], 1.7976931348623157e308

    v_add_f16 v1, 65500.0, v2   // ok for f16.
    v_add_f32 v1, 65600.0, v2   // ok for f32, but would result in overflow for f16.

Examples of disabled conversions:

.. parsed-literal::

    // GFX9

    v_add_f16 v1, 65600.0, v2    // overflow

.. _amdgpu_synid_rl_conv:

Conversion of Relocatable Values
--------------------------------

:ref:`Relocatable expressions<amdgpu_synid_relocatable_expression>`
may be used with 32-bit integer operands and jump targets.

When the value of a relocatable expression is resolved by a linker, it is
converted as needed and truncated to the operand size. The conversion depends
on :ref:`relocation type<amdgpu-relocation-records>` and operand kind.

For example, when a 32-bit operand of an instruction refers
to a relocatable expression *expr*, this reference is evaluated
to a 64-bit offset from the address after the
instruction to the address being referenced, *counted in bytes*.
Then the value is truncated to 32 bits and encoded as a literal:

.. parsed-literal::

    expr = .
    v_add_co_u32_e32 v0, vcc, expr, v1  // 'expr' operand is evaluated to -4
                                        // and then truncated to 0xFFFFFFFC

As another example, when a branch instruction refers to a label,
this reference is evaluated to an offset from the address after the
instruction to the label address, *counted in dwords*.
Then the value is truncated to 16 bits:

.. parsed-literal::

    label:
    s_branch label  // 'label' operand is evaluated to -1 and truncated to 0xFFFF
