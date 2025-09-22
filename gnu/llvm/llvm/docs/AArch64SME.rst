*****************************************************
Support for AArch64 Scalable Matrix Extension in LLVM
*****************************************************

.. contents::
   :local:

1. Introduction
===============

The :ref:`AArch64 SME ACLE <aarch64_sme_acle>` provides a number of
attributes for users to control PSTATE.SM and PSTATE.ZA.
The :ref:`AArch64 SME ABI<aarch64_sme_abi>` describes the requirements for
calls between functions when at least one of those functions uses PSTATE.SM or
PSTATE.ZA.

This document describes how the SME ACLE attributes map to LLVM IR
attributes and how LLVM lowers these attributes to implement the rules and
requirements of the ABI.

Below we describe the LLVM IR attributes and their relation to the C/C++
level ACLE attributes:

``aarch64_pstate_sm_enabled``
    is used for functions with ``__arm_streaming``

``aarch64_pstate_sm_compatible``
    is used for functions with ``__arm_streaming_compatible``

``aarch64_pstate_sm_body``
  is used for functions with ``__arm_locally_streaming`` and is
  only valid on function definitions (not declarations)

``aarch64_new_za``
  is used for functions with ``__arm_new("za")``

``aarch64_in_za``
  is used for functions with ``__arm_in("za")``

``aarch64_out_za``
  is used for functions with ``__arm_out("za")``

``aarch64_inout_za``
  is used for functions with ``__arm_inout("za")``

``aarch64_preserves_za``
  is used for functions with ``__arm_preserves("za")``

``aarch64_expanded_pstate_za``
  is used for functions with ``__arm_new_za``

Clang must ensure that the above attributes are added both to the
function's declaration/definition as well as to their call-sites. This is
important for calls to attributed function pointers, where there is no
definition or declaration available.


2. Handling PSTATE.SM
=====================

When changing PSTATE.SM the execution of FP/vector operations may be transferred
to another processing element. This has three important implications:

* The runtime SVE vector length may change.

* The contents of FP/AdvSIMD/SVE registers are zeroed.

* The set of allowable instructions changes.

This leads to certain restrictions on IR and optimizations. For example, it
is undefined behaviour to share vector-length dependent state between functions
that may operate with different values for PSTATE.SM. Front-ends must honour
these restrictions when generating LLVM IR.

Even though the runtime SVE vector length may change, for the purpose of LLVM IR
and almost all parts of CodeGen we can assume that the runtime value for
``vscale`` does not. If we let the compiler insert the appropriate ``smstart``
and ``smstop`` instructions around call boundaries, then the effects on SVE
state can be mitigated. By limiting the state changes to a very brief window
around the call we can control how the operations are scheduled and how live
values remain preserved between state transitions.

In order to control PSTATE.SM at this level of granularity, we use function and
callsite attributes rather than intrinsics.


Restrictions on attributes
--------------------------

* It is undefined behaviour to pass or return (pointers to) scalable vector
  objects to/from functions which may use a different SVE vector length.
  This includes functions with a non-streaming interface, but marked with
  ``aarch64_pstate_sm_body``.

* It is not allowed for a function to be decorated with both
  ``aarch64_pstate_sm_compatible`` and ``aarch64_pstate_sm_enabled``.

* It is not allowed for a function to be decorated with more than one of the
  following attributes:
  ``aarch64_new_za``, ``aarch64_in_za``, ``aarch64_out_za``, ``aarch64_inout_za``,
  ``aarch64_preserves_za``.

These restrictions also apply in the higher level SME ACLE, which means we can
emit diagnostics in Clang to signal users about incorrect behaviour.


Compiler inserted streaming-mode changes
----------------------------------------

The table below describes the transitions in PSTATE.SM the compiler has to
account for when doing calls between functions with different attributes.
In this table, we use the following abbreviations:

``N``
  functions with a normal interface (PSTATE.SM=0 on entry, PSTATE.SM=0 on
  return)

``S``
  functions with a Streaming interface (PSTATE.SM=1 on entry, PSTATE.SM=1
  on return)

``SC``
  functions with a Streaming-Compatible interface (PSTATE.SM can be
  either 0 or 1 on entry, and is unchanged on return).

Functions with ``__attribute__((arm_locally_streaming))`` are excluded from this
table because for the caller the attribute is synonymous to 'streaming', and
for the callee it is merely an implementation detail that is explicitly not
exposed to the caller.

.. table:: Combinations of calls for functions with different attributes

   ==== ==== =============================== ============================== ==============================
   From To   Before call                     After call                     After exception
   ==== ==== =============================== ============================== ==============================
   N    N
   N    S    SMSTART                         SMSTOP
   N    SC
   S    N    SMSTOP                          SMSTART                        SMSTART
   S    S                                                                   SMSTART
   S    SC                                                                  SMSTART
   SC   N    If PSTATE.SM before call is 1,  If PSTATE.SM before call is 1, If PSTATE.SM before call is 1,
             then SMSTOP                     then SMSTART                   then SMSTART
   SC   S    If PSTATE.SM before call is 0,  If PSTATE.SM before call is 0, If PSTATE.SM before call is 1,
             then SMSTART                    then SMSTOP                    then SMSTART
   SC   SC                                                                  If PSTATE.SM before call is 1,
                                                                            then SMSTART
   ==== ==== =============================== ============================== ==============================


Because changing PSTATE.SM zeroes the FP/vector registers, it is best to emit
the ``smstart`` and ``smstop`` instructions before register allocation, so that
the register allocator can spill/reload registers around the mode change.

The compiler should also have sufficient information on which operations are
part of the call/function's arguments/result and which operations are part of
the function's body, so that it can place the mode changes in exactly the right
position. The suitable place to do this seems to be SelectionDAG, where it lowers
the call's arguments/return values to implement the specified calling convention.
SelectionDAG provides Chains and Glue to specify the order of operations and give
preliminary control over the instruction's scheduling.


Example of preserving state
---------------------------

When passing and returning a ``float`` value to/from a function
that has a streaming interface from a function that has a normal interface, the
call-site will need to ensure that the argument/result registers are preserved
and that no other code is scheduled in between the ``smstart/smstop`` and the call.

.. code-block:: llvm

    define float @foo(float %f) nounwind {
      %res = call float @bar(float %f) "aarch64_pstate_sm_enabled"
      ret float %res
    }

    declare float @bar(float) "aarch64_pstate_sm_enabled"

The program needs to preserve the value of the floating point argument and
return value in register ``s0``:

.. code-block:: none

    foo:                                    // @foo
    // %bb.0:
            stp     d15, d14, [sp, #-80]!           // 16-byte Folded Spill
            stp     d13, d12, [sp, #16]             // 16-byte Folded Spill
            stp     d11, d10, [sp, #32]             // 16-byte Folded Spill
            stp     d9, d8, [sp, #48]               // 16-byte Folded Spill
            str     x30, [sp, #64]                  // 8-byte Folded Spill
            str     s0, [sp, #76]                   // 4-byte Folded Spill
            smstart sm
            ldr     s0, [sp, #76]                   // 4-byte Folded Reload
            bl      bar
            str     s0, [sp, #76]                   // 4-byte Folded Spill
            smstop  sm
            ldp     d9, d8, [sp, #48]               // 16-byte Folded Reload
            ldp     d11, d10, [sp, #32]             // 16-byte Folded Reload
            ldp     d13, d12, [sp, #16]             // 16-byte Folded Reload
            ldr     s0, [sp, #76]                   // 4-byte Folded Reload
            ldr     x30, [sp, #64]                  // 8-byte Folded Reload
            ldp     d15, d14, [sp], #80             // 16-byte Folded Reload
            ret

Setting the correct register masks on the ISD nodes and inserting the
``smstart/smstop`` in the right places should ensure this is done correctly.


Instruction Selection Nodes
---------------------------

.. code-block:: none

  AArch64ISD::SMSTART Chain, [SM|ZA|Both], CurrentState, ExpectedState[, RegMask]
  AArch64ISD::SMSTOP  Chain, [SM|ZA|Both], CurrentState, ExpectedState[, RegMask]

The ``SMSTART/SMSTOP`` nodes take ``CurrentState`` and ``ExpectedState`` operand for
the case of a conditional SMSTART/SMSTOP. The instruction will only be executed
if CurrentState != ExpectedState.

When ``CurrentState`` and ``ExpectedState`` can be evaluated at compile-time
(i.e. they are both constants) then an unconditional ``smstart/smstop``
instruction is emitted. Otherwise the node is matched to a Pseudo instruction
which expands to a compare/branch and a ``smstart/smstop``. This is necessary to
implement transitions from ``SC -> N`` and ``SC -> S``.


Unchained Function calls
------------------------
When a function with "``aarch64_pstate_sm_enabled``" calls a function that is not
streaming compatible, the compiler has to insert a SMSTOP before the call and
insert a SMSTOP after the call.

If the function that is called is an intrinsic with no side-effects which in
turn is lowered to a function call (e.g. ``@llvm.cos()``), then the call to
``@llvm.cos()`` is not part of any Chain; it can be scheduled freely.

Lowering of a Callsite creates a small chain of nodes which:

- starts a call sequence

- copies input values from virtual registers to physical registers specified by
  the ABI

- executes a branch-and-link

- stops the call sequence

- copies the output values from their physical registers to virtual registers

When the callsite's Chain is not used, only the result value from the chained
sequence is used, but the Chain itself is discarded.

The ``SMSTART`` and ``SMSTOP`` ISD nodes return a Chain, but no real
values, so when the ``SMSTART/SMSTOP`` nodes are part of a Chain that isn't
used, these nodes are not considered for scheduling and are
removed from the DAG.  In order to prevent these nodes
from being removed, we need a way to ensure the results from the
``CopyFromReg`` can only be **used after** the ``SMSTART/SMSTOP`` has been
executed.

We can use a CopyToReg -> CopyFromReg sequence for this, which moves the
value to/from a virtual register and chains these nodes with the
SMSTART/SMSTOP to make them part of the expression that calculates
the result value. The resulting COPY nodes are removed by the register
allocator.

The example below shows how this is used in a DAG that does not link
together the result by a Chain, but rather by a value:

.. code-block:: none

               t0: ch,glue = AArch64ISD::SMSTOP ...
             t1: ch,glue = ISD::CALL ....
           t2: res,ch,glue = CopyFromReg t1, ...
         t3: ch,glue = AArch64ISD::SMSTART t2:1, ....   <- this is now part of the expression that returns the result value.
       t4: ch = CopyToReg t3, Register:f64 %vreg, t2
     t5: res,ch = CopyFromReg t4, Register:f64 %vreg
   t6: res = FADD t5, t9

We also need this for locally streaming functions, where an ``SMSTART`` needs to
be inserted into the DAG at the start of the function.

Functions with __attribute__((arm_locally_streaming))
-----------------------------------------------------

If a function is marked as ``arm_locally_streaming``, then the runtime SVE
vector length in the prologue/epilogue may be different from the vector length
in the function's body. This happens because we invoke smstart after setting up
the stack-frame and similarly invoke smstop before deallocating the stack-frame.

To ensure we use the correct SVE vector length to allocate the locals with, we
can use the streaming vector-length to allocate the stack-slots through the
``ADDSVL`` instruction, even when the CPU is not yet in streaming mode.

This only works for locals and not callee-save slots, since LLVM doesn't support
mixing two different scalable vector lengths in one stack frame. That means that the
case where a function is marked ``arm_locally_streaming`` and needs to spill SVE
callee-saves in the prologue is currently unsupported.  However, it is unlikely
for this to happen without user intervention, because ``arm_locally_streaming``
functions cannot take or return vector-length-dependent values. This would otherwise
require forcing both the SVE PCS using '``aarch64_sve_pcs``' combined with using
``arm_locally_streaming`` in order to encounter this problem. This combination
can be prevented in Clang through emitting a diagnostic.


An example of how the prologue/epilogue would look for a function that is
attributed with ``arm_locally_streaming``:

.. code-block:: c++

    #define N 64

    void __attribute__((arm_streaming_compatible)) some_use(svfloat32_t *);

    // Use a float argument type, to check the value isn't clobbered by smstart.
    // Use a float return type to check the value isn't clobbered by smstop.
    float __attribute__((noinline, arm_locally_streaming)) foo(float arg) {
      // Create local for SVE vector to check local is created with correct
      // size when not yet in streaming mode (ADDSVL).
      float array[N];
      svfloat32_t vector;

      some_use(&vector);
      svst1_f32(svptrue_b32(), &array[0], vector);
      return array[N - 1] + arg;
    }

should use ADDSVL for allocating the stack space and should avoid clobbering
the return/argument values.

.. code-block:: none

    _Z3foof:                                // @_Z3foof
    // %bb.0:                               // %entry
            stp     d15, d14, [sp, #-96]!           // 16-byte Folded Spill
            stp     d13, d12, [sp, #16]             // 16-byte Folded Spill
            stp     d11, d10, [sp, #32]             // 16-byte Folded Spill
            stp     d9, d8, [sp, #48]               // 16-byte Folded Spill
            stp     x29, x30, [sp, #64]             // 16-byte Folded Spill
            add     x29, sp, #64
            str     x28, [sp, #80]                  // 8-byte Folded Spill
            addsvl  sp, sp, #-1
            sub     sp, sp, #256
            str     s0, [x29, #28]                  // 4-byte Folded Spill
            smstart sm
            sub     x0, x29, #64
            addsvl  x0, x0, #-1
            bl      _Z10some_usePu13__SVFloat32_t
            sub     x8, x29, #64
            ptrue   p0.s
            ld1w    { z0.s }, p0/z, [x8, #-1, mul vl]
            ldr     s1, [x29, #28]                  // 4-byte Folded Reload
            st1w    { z0.s }, p0, [sp]
            ldr     s0, [sp, #252]
            fadd    s0, s0, s1
            str     s0, [x29, #28]                  // 4-byte Folded Spill
            smstop  sm
            ldr     s0, [x29, #28]                  // 4-byte Folded Reload
            addsvl  sp, sp, #1
            add     sp, sp, #256
            ldp     x29, x30, [sp, #64]             // 16-byte Folded Reload
            ldp     d9, d8, [sp, #48]               // 16-byte Folded Reload
            ldp     d11, d10, [sp, #32]             // 16-byte Folded Reload
            ldp     d13, d12, [sp, #16]             // 16-byte Folded Reload
            ldr     x28, [sp, #80]                  // 8-byte Folded Reload
            ldp     d15, d14, [sp], #96             // 16-byte Folded Reload
            ret


Preventing the use of illegal instructions in Streaming Mode
------------------------------------------------------------

* When executing a program in streaming-mode (PSTATE.SM=1) a subset of SVE/SVE2
  instructions and most AdvSIMD/NEON instructions are invalid.

* When executing a program in normal mode (PSTATE.SM=0), a subset of SME
  instructions are invalid.

* Streaming-compatible functions must only use instructions that are valid when
  either PSTATE.SM=0 or PSTATE.SM=1.

The value of PSTATE.SM is not controlled by the feature flags, but rather by the
function attributes. This means that we can compile for '``+sme``' and the compiler
will code-generate any instructions, even if they are not legal under the requested
streaming mode. The compiler needs to use the function attributes to ensure the
compiler doesn't do transformations under the assumption that certain operations
are available at runtime.

We made a conscious choice not to model this with feature flags, because we
still want to support inline-asm in either mode (with the user placing
smstart/smstop manually), and this became rather complicated to implement at the
individual instruction level (see `D120261 <https://reviews.llvm.org/D120261>`_
and `D121208 <https://reviews.llvm.org/D121208>`_) because of limitations in
TableGen.

As a first step, this means we'll disable vectorization (LoopVectorize/SLP)
entirely when the a function has either of the ``aarch64_pstate_sm_enabled``,
``aarch64_pstate_sm_body`` or ``aarch64_pstate_sm_compatible`` attributes,
in order to avoid the use of vector instructions.

Later on we'll aim to relax these restrictions to enable scalable
auto-vectorization with a subset of streaming-compatible instructions, but that
requires changes to the CostModel, Legalization and SelectionDAG lowering.

We will also emit diagnostics in Clang to prevent the use of
non-streaming(-compatible) operations, e.g. through ACLE intrinsics, when a
function is decorated with the streaming mode attributes.


Other things to consider
------------------------

* Inlining must be disabled when the call-site needs to toggle PSTATE.SM or
  when the callee's function body is executed in a different streaming mode than
  its caller. This is needed because function calls are the boundaries for
  streaming mode changes.

* Tail call optimization must be disabled when the call-site needs to toggle
  PSTATE.SM, such that the caller can restore the original value of PSTATE.SM.


3. Handling PSTATE.ZA
=====================

In contrast to PSTATE.SM, enabling PSTATE.ZA does not affect the SVE vector
length and also doesn't clobber FP/AdvSIMD/SVE registers. This means it is safe
to toggle PSTATE.ZA using intrinsics. This also makes it simpler to setup a
lazy-save mechanism for calls to private-ZA functions (i.e. functions that may
either directly or indirectly clobber ZA state).

For the purpose of handling functions marked with ``aarch64_new_za``,
we have introduced a new LLVM IR pass (SMEABIPass) that is run just before
SelectionDAG. Any such functions dealt with by this pass are marked with
``aarch64_expanded_pstate_za``.

Setting up a lazy-save
----------------------

Committing a lazy-save
----------------------

Exception handling and ZA
-------------------------

4. Types
========

AArch64 Predicate-as-Counter Type
---------------------------------

:Overview:

The predicate-as-counter type represents the type of a predicate-as-counter
value held in a AArch64 SVE predicate register. Such a value contains
information about the number of active lanes, the element width and a bit that
tells whether the generated mask should be inverted. ACLE intrinsics should be
used to move the predicate-as-counter value to/from a predicate vector.

There are certain limitations on the type:

* The type can be used for function parameters and return values.

* The supported LLVM operations on this type are limited to ``load``, ``store``,
  ``phi``, ``select`` and ``alloca`` instructions.

The predicate-as-counter type is a scalable type.

:Syntax:

::

      target("aarch64.svcount")



5. References
=============

    .. _aarch64_sme_acle:

1.  `SME ACLE Pull-request <https://github.com/ARM-software/acle/pull/188>`__

    .. _aarch64_sme_abi:

2.  `SME ABI Pull-request <https://github.com/ARM-software/abi-aa/pull/123>`__
