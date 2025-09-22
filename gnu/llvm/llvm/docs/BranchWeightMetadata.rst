===========================
LLVM Branch Weight Metadata
===========================

.. contents::
   :local:

Introduction
============

Branch Weight Metadata represents branch weights as its likeliness to be taken
(see :doc:`BlockFrequencyTerminology`). Metadata is assigned to an
``Instruction`` that is a terminator as a ``MDNode`` of the ``MD_prof`` kind.
The first operator is always a ``MDString`` node with the string
"branch_weights".  Number of operators depends on the terminator type.

Branch weights might be fetch from the profiling file, or generated based on
`__builtin_expect`_ and `__builtin_expect_with_probability`_ instruction.

All weights are represented as an unsigned 32-bit values, where higher value
indicates greater chance to be taken.

Supported Instructions
======================

``BranchInst``
^^^^^^^^^^^^^^

Metadata is only assigned to the conditional branches. There are two extra
operands for the true and the false branch.
We optionally track if the metadata was added by ``__builtin_expect`` or
``__builtin_expect_with_probability`` with an optional field ``!"expected"``.

.. code-block:: none

  !0 = !{
    !"branch_weights",
    [ !"expected", ]
    i32 <TRUE_BRANCH_WEIGHT>,
    i32 <FALSE_BRANCH_WEIGHT>
  }

``SwitchInst``
^^^^^^^^^^^^^^

Branch weights are assigned to every case (including the ``default`` case which
is always case #0).

.. code-block:: none

  !0 = !{
    !"branch_weights",
    [ !"expected", ]
    i32 <DEFAULT_BRANCH_WEIGHT>
    [ , i32 <CASE_BRANCH_WEIGHT> ... ]
  }

``IndirectBrInst``
^^^^^^^^^^^^^^^^^^

Branch weights are assigned to every destination.

.. code-block:: none

  !0 = !{
    !"branch_weights",
    [ !"expected", ]
    i32 <LABEL_BRANCH_WEIGHT>
    [ , i32 <LABEL_BRANCH_WEIGHT> ... ]
  }

``CallInst``
^^^^^^^^^^^^^^^^^^

Calls may have branch weight metadata, containing the execution count of
the call. It is currently used in SamplePGO mode only, to augment the
block and entry counts which may not be accurate with sampling.

.. code-block:: none

  !0 = !{
    !"branch_weights",
    [ !"expected", ]
    i32 <CALL_BRANCH_WEIGHT>
  }

``InvokeInst``
^^^^^^^^^^^^^^^^^^

Invoke instruction may have branch weight metadata with one or two weights.
The second weight is optional and corresponds to the unwind branch.
If only one weight is set then it contains the execution count of the call
and used in SamplePGO mode only as described for the call instruction. If both
weights are specified then the second weight contains count of unwind branch
taken and the first weights contains the execution count of the call minus
the count of unwind branch taken. Both weights specified are used to calculate
BranchProbability as for BranchInst and for SamplePGO the sum of both weights
is used.

.. code-block:: none

  !0 = !{
    !"branch_weights",
    [ !"expected", ]
    i32 <INVOKE_NORMAL_WEIGHT>
    [ , i32 <INVOKE_UNWIND_WEIGHT> ]
  }

Other
^^^^^

Other terminator instructions are not allowed to contain Branch Weight Metadata.

.. _\__builtin_expect:

Built-in ``expect`` Instructions
================================

``__builtin_expect(long exp, long c)`` instruction provides branch prediction
information. The return value is the value of ``exp``.

It is especially useful in conditional statements. Currently Clang supports two
conditional statements:

``if`` statement
^^^^^^^^^^^^^^^^

The ``exp`` parameter is the condition. The ``c`` parameter is the expected
comparison value. If it is equal to 1 (true), the condition is likely to be
true, in other case condition is likely to be false. For example:

.. code-block:: c++

  if (__builtin_expect(x > 0, 1)) {
    // This block is likely to be taken.
  }

``switch`` statement
^^^^^^^^^^^^^^^^^^^^

The ``exp`` parameter is the value. The ``c`` parameter is the expected
value. If the expected value doesn't show on the cases list, the ``default``
case is assumed to be likely taken.

.. code-block:: c++

  switch (__builtin_expect(x, 5)) {
  default: break;
  case 0:  // ...
  case 3:  // ...
  case 5:  // This case is likely to be taken.
  }

.. _\__builtin_expect_with_probability:

Built-in ``expect.with.probability`` Instruction
================================================

``__builtin_expect_with_probability(long exp, long c, double probability)`` has
the same semantics as ``__builtin_expect``, but the caller provides the
probability that ``exp == c``. The last argument ``probability`` must be
constant floating-point expression and be in the range [0.0, 1.0] inclusive.
The usage is also similar as ``__builtin_expect``, for example:

``if`` statement
^^^^^^^^^^^^^^^^

If the expect comparison value ``c`` is equal to 1(true), and probability
value ``probability`` is set to 0.8, that means the probability of condition
to be true is 80% while that of false is 20%.

.. code-block:: c++

  if (__builtin_expect_with_probability(x > 0, 1, 0.8)) {
    // This block is likely to be taken with probability 80%.
  }

``switch`` statement
^^^^^^^^^^^^^^^^^^^^

This is basically the same as ``switch`` statement in ``__builtin_expect``.
The probability that ``exp`` is equal to the expect value is given in
the third argument ``probability``, while the probability of other value is
the average of remaining probability(``1.0 - probability``). For example:

.. code-block:: c++

  switch (__builtin_expect_with_probability(x, 5, 0.7)) {
  default: break;  // Take this case with probability 10%
  case 0:  break;  // Take this case with probability 10%
  case 3:  break;  // Take this case with probability 10%
  case 5:  break;  // This case is likely to be taken with probability 70%
  }

CFG Modifications
=================

Branch Weight Metatada is not proof against CFG changes. If terminator operands'
are changed some action should be taken. In other case some misoptimizations may
occur due to incorrect branch prediction information.

Function Entry Counts
=====================

To allow comparing different functions during inter-procedural analysis and
optimization, ``MD_prof`` nodes can also be assigned to a function definition.
The first operand is a string indicating the name of the associated counter.

Currently, one counter is supported: "function_entry_count". The second operand
is a 64-bit counter that indicates the number of times that this function was
invoked (in the case of instrumentation-based profiles). In the case of
sampling-based profiles, this operand is an approximation of how many times
the function was invoked.

For example, in the code below, the instrumentation for function foo()
indicates that it was called 2,590 times at runtime.

.. code-block:: llvm

  define i32 @foo() !prof !1 {
    ret i32 0
  }
  !1 = !{!"function_entry_count", i64 2590}

If "function_entry_count" has more than 2 operands, the later operands are
the GUID of the functions that needs to be imported by ThinLTO. This is only
set by sampling based profile. It is needed because the sampling based profile
was collected on a binary that had already imported and inlined these functions,
and we need to ensure the IR matches in the ThinLTO backends for profile
annotation. The reason why we cannot annotate this on the callsite is that it
can only goes down 1 level in the call chain. For the cases where
foo_in_a_cc()->bar_in_b_cc()->baz_in_c_cc(), we will need to go down 2 levels
in the call chain to import both bar_in_b_cc and baz_in_c_cc.
