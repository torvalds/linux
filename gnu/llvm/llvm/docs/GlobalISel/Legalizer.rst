.. _milegalizer:

Legalizer
---------

This pass transforms the generic machine instructions such that they are legal.

A legal instruction is defined as:

* **selectable** --- the target will later be able to select it to a
  target-specific (non-generic) instruction. This doesn't necessarily mean that
  :doc:`InstructionSelect` has to handle it though. It just means that
  **something** must handle it.

* operating on **vregs that can be loaded and stored** -- if necessary, the
  target can select a ``G_LOAD``/``G_STORE`` of each gvreg operand.

As opposed to SelectionDAG, there are no legalization phases.  In particular,
'type' and 'operation' legalization are not separate.

Legalization is iterative, and all state is contained in GMIR.  To maintain the
validity of the intermediate code, instructions are introduced:

* ``G_MERGE_VALUES`` --- concatenate multiple registers of the same
  size into a single wider register.

* ``G_UNMERGE_VALUES`` --- extract multiple registers of the same size
  from a single wider register.

* ``G_EXTRACT`` --- extract a simple register (as contiguous sequences of bits)
  from a single wider register.

As they are expected to be temporary byproducts of the legalization process,
they are combined at the end of the :ref:`milegalizer` pass.
If any remain, they are expected to always be selectable, using loads and stores
if necessary.

The legality of an instruction may only depend on the instruction itself and
must not depend on any context in which the instruction is used. However, after
deciding that an instruction is not legal, using the context of the instruction
to decide how to legalize the instruction is permitted. As an example, if we
have a ``G_FOO`` instruction of the form::

  %1:_(s32) = G_CONSTANT i32 1
  %2:_(s32) = G_FOO %0:_(s32), %1:_(s32)

it's impossible to say that G_FOO is legal iff %1 is a ``G_CONSTANT`` with
value ``1``. However, the following::

  %2:_(s32) = G_FOO %0:_(s32), i32 1

can say that it's legal iff operand 2 is an immediate with value ``1`` because
that information is entirely contained within the single instruction.

.. _api-legalizerinfo:

API: LegalizerInfo
^^^^^^^^^^^^^^^^^^

The recommended [#legalizer-legacy-footnote]_ API looks like this::

  getActionDefinitionsBuilder({G_ADD, G_SUB, G_MUL, G_AND, G_OR, G_XOR, G_SHL})
      .legalFor({s32, s64, v2s32, v4s32, v2s64})
      .clampScalar(0, s32, s64)
      .widenScalarToNextPow2(0)
      .clampNumElements(0, v2s32, v4s32)
      .clampNumElements(0, v2s64, v2s64)
      .moreElementsToNextPow2(0);

and describes a set of rules by which we can either declare an instruction legal
or decide which action to take to make it more legal.

At the core of this ruleset is the ``LegalityQuery`` which describes the
instruction. We use a description rather than the instruction to both allow other
passes to determine legality without having to create an instruction and also to
limit the information available to the predicates to that which is safe to rely
on. Currently, the information available to the predicates that determine
legality contains:

* The opcode for the instruction

* The type of each type index (see ``type0``, ``type1``, etc.)

* The size in bytes and atomic ordering for each MachineMemOperand

.. note::

  An alternative worth investigating is to generalize the API to represent
  actions using ``std::function`` that implements the action, instead of explicit
  enum tokens (``Legal``, ``WidenScalar``, ...) that instruct it to call a
  function. This would have some benefits, most notable being that Custom could
  be removed.

.. rubric:: Footnotes

.. [#legalizer-legacy-footnote] An API is broadly similar to
   SelectionDAG/TargetLowering is available but is not recommended as a more
   powerful API is available.

Rule Processing and Declaring Rules
"""""""""""""""""""""""""""""""""""

The ``getActionDefinitionsBuilder`` function generates a ruleset for the given
opcode(s) that rules can be added to. If multiple opcodes are given, they are
all permanently bound to the same ruleset. The rules in a ruleset are executed
from top to bottom and will start again from the top if an instruction is
legalized as a result of the rules. If the ruleset is exhausted without
satisfying any rule, then it is considered unsupported.

When it doesn't declare the instruction legal, each pass over the rules may
request that one type changes to another type. Sometimes this can cause multiple
types to change but we avoid this as much as possible as making multiple changes
can make it difficult to avoid infinite loops where, for example, narrowing one
type causes another to be too small and widening that type causes the first one
to be too big.

In general, it's advisable to declare instructions legal as close to the top of
the rule as possible and to place any expensive rules as low as possible. This
helps with performance as testing for legality happens more often than
legalization and legalization can require multiple passes over the rules.

As a concrete example, consider the rule::

  getActionDefinitionsBuilder({G_ADD, G_SUB, G_MUL, G_AND, G_OR, G_XOR, G_SHL})
      .legalFor({s32, s64, v2s32, v4s32, v2s64})
      .clampScalar(0, s32, s64)
      .widenScalarToNextPow2(0);

and the instruction::

  %2:_(s7) = G_ADD %0:_(s7), %1:_(s7)

this doesn't meet the predicate for the :ref:`.legalFor() <legalfor>` as ``s7``
is not one of the listed types so it falls through to the
:ref:`.clampScalar() <clampscalar>`. It does meet the predicate for this rule
as the type is smaller than the ``s32`` and this rule instructs the legalizer
to change type 0 to ``s32``. It then restarts from the top. This time it does
satisfy ``.legalFor()`` and the resulting output is::

  %3:_(s32) = G_ANYEXT %0:_(s7)
  %4:_(s32) = G_ANYEXT %1:_(s7)
  %5:_(s32) = G_ADD %3:_(s32), %4:_(s32)
  %2:_(s7) = G_TRUNC %5:_(s32)

where the ``G_ADD`` is legal and the other instructions are scheduled for
processing by the legalizer.

Rule Actions
""""""""""""

There are various rule factories that append rules to a ruleset but they have a
few actions in common:

.. _legalfor:

* ``legalIf()``, ``legalFor()``, etc. declare an instruction to be legal if the
  predicate is satisfied.

* ``narrowScalarIf()``, ``narrowScalarFor()``, etc. declare an instruction to be illegal
  if the predicate is satisfied and indicates that narrowing the scalars in one
  of the types to a specific type would make it more legal. This action supports
  both scalars and vectors.

* ``widenScalarIf()``, ``widenScalarFor()``, etc. declare an instruction to be illegal
  if the predicate is satisfied and indicates that widening the scalars in one
  of the types to a specific type would make it more legal. This action supports
  both scalars and vectors.

* ``fewerElementsIf()``, ``fewerElementsFor()``, etc. declare an instruction to be
  illegal if the predicate is satisfied and indicates reducing the number of
  vector elements in one of the types to a specific type would make it more
  legal. This action supports vectors.

* ``moreElementsIf()``, ``moreElementsFor()``, etc. declare an instruction to be illegal
  if the predicate is satisfied and indicates increasing the number of vector
  elements in one of the types to a specific type would make it more legal.
  This action supports vectors.

* ``lowerIf()``, ``lowerFor()``, etc. declare an instruction to be
  illegal if the predicate is satisfied and indicates that replacing
  it with equivalent instruction(s) would make it more legal. Support
  for this action differs for each opcode. These may provide an
  optional LegalizeMutation containing a type to attempt to perform
  the expansion in a different type.

* ``libcallIf()``, ``libcallFor()``, etc. declare an instruction to be illegal if the
  predicate is satisfied and indicates that replacing it with a libcall would
  make it more legal. Support for this action differs for
  each opcode.

* ``customIf()``, ``customFor()``, etc. declare an instruction to be illegal if the
  predicate is satisfied and indicates that the backend developer will supply
  a means of making it more legal.

* ``unsupportedIf()``, ``unsupportedFor()``, etc. declare an instruction to be illegal
  if the predicate is satisfied and indicates that there is no way to make it
  legal and the compiler should fail.

* ``fallback()`` falls back on an older API and should only be used while porting
  existing code from that API.

Rule Predicates
"""""""""""""""

The rule factories also have predicates in common:

* ``legal()``, ``lower()``, etc. are always satisfied.

* ``legalIf()``, ``narrowScalarIf()``, etc. are satisfied if the user-supplied
  ``LegalityPredicate`` function returns true. This predicate has access to the
  information in the ``LegalityQuery`` to make its decision.
  User-supplied predicates can also be combined using ``all(P0, P1, ...)``.

* ``legalFor()``, ``narrowScalarFor()``, etc. are satisfied if the type matches one in
  a given set of types. For example ``.legalFor({s16, s32})`` declares the
  instruction legal if type 0 is either s16 or s32. Additional versions for two
  and three type indices are generally available. For these, all the type
  indices considered together must match all the types in one of the tuples. So
  ``.legalFor({{s16, s32}, {s32, s64}})`` will only accept ``{s16, s32}``, or
  ``{s32, s64}`` but will not accept ``{s16, s64}``.

* ``legalForTypesWithMemSize()``, ``narrowScalarForTypesWithMemSize()``, etc. are
  similar to ``legalFor()``, ``narrowScalarFor()``, etc. but additionally require a
  MachineMemOperand to have a given size in each tuple.

* ``legalForCartesianProduct()``, ``narrowScalarForCartesianProduct()``, etc. are
  satisfied if each type index matches one element in each of the independent
  sets. So ``.legalForCartesianProduct({s16, s32}, {s32, s64})`` will accept
  ``{s16, s32}``, ``{s16, s64}``, ``{s32, s32}``, and ``{s32, s64}``.

Composite Rules
"""""""""""""""

There are some composite rules for common situations built out of the above facilities:

* ``widenScalarToNextPow2()`` is like ``widenScalarIf()`` but is satisfied iff the type
  size in bits is not a power of 2 and selects a target type that is the next
  largest power of 2.

.. _clampscalar:

* ``minScalar()`` is like ``widenScalarIf()`` but is satisfied iff the type
  size in bits is smaller than the given minimum and selects the minimum as the
  target type. Similarly, there is also a ``maxScalar()`` for the maximum and a
  ``clampScalar()`` to do both at once.

* ``minScalarSameAs()`` is like ``minScalar()`` but the minimum is taken from another
  type index.

* ``moreElementsToNextMultiple()`` is like ``moreElementsToNextPow2()`` but is based on
  multiples of X rather than powers of 2.

.. _min-legalizerinfo:

Minimum Rule Set
^^^^^^^^^^^^^^^^

GlobalISel's legalizer has a great deal of flexibility in how a given target
shapes the GMIR that the rest of the backend must handle. However, there are
a small number of requirements that all targets must meet.

Before discussing the minimum requirements, we'll need some terminology:

Producer Type Set
  The set of types which is the union of all possible types produced by at
  least one legal instruction.

Consumer Type Set
  The set of types which is the union of all possible types consumed by at
  least one legal instruction.

Both sets are often identical but there's no guarantee of that. For example,
it's not uncommon to be unable to consume s64 but still be able to produce it
for a few specific instructions.

Minimum Rules For Scalars
"""""""""""""""""""""""""

* G_ANYEXT must be legal for all inputs from the producer type set and all larger
  outputs from the consumer type set.
* G_TRUNC must be legal for all inputs from the producer type set and all
  smaller outputs from the consumer type set.

G_ANYEXT, and G_TRUNC have mandatory legality since the GMIR requires a means to
connect operations with different type sizes. They are usually trivial to support
since G_ANYEXT doesn't define the value of the additional bits and G_TRUNC is
discarding bits. The other conversions can be lowered into G_ANYEXT/G_TRUNC
with some additional operations that are subject to further legalization. For
example, G_SEXT can lower to::

  %1 = G_ANYEXT %0
  %2 = G_CONSTANT ...
  %3 = G_SHL %1, %2
  %4 = G_ASHR %3, %2

and the G_CONSTANT/G_SHL/G_ASHR can further lower to other operations or target
instructions. Similarly, G_FPEXT has no legality requirement since it can lower
to a G_ANYEXT followed by a target instruction.

G_MERGE_VALUES and G_UNMERGE_VALUES do not have legality requirements since the
former can lower to G_ANYEXT and some other legalizable instructions, while the
latter can lower to some legalizable instructions followed by G_TRUNC.

Minimum Legality For Vectors
""""""""""""""""""""""""""""

Within the vector types, there aren't any defined conversions in LLVM IR as
vectors are often converted by reinterpreting the bits or by decomposing the
vector and reconstituting it as a different type. As such, G_BITCAST is the
only operation to account for. We generally don't require that it's legal
because it can usually be lowered to COPY (or to nothing using
replaceAllUses()). However, there are situations where G_BITCAST is non-trivial
(e.g. little-endian vectors of big-endian data such as on big-endian MIPS MSA and
big-endian ARM NEON, see `_i_bitcast`). To account for this G_BITCAST must be
legal for all type combinations that change the bit pattern in the value.

There are no legality requirements for G_BUILD_VECTOR, or G_BUILD_VECTOR_TRUNC
since these can be handled by:
* Declaring them legal.
* Scalarizing them.
* Lowering them to G_TRUNC+G_ANYEXT and some legalizable instructions.
* Lowering them to target instructions which are legal by definition.

The same reasoning also allows G_UNMERGE_VALUES to lack legality requirements
for vector inputs.

Minimum Legality for Pointers
"""""""""""""""""""""""""""""

There are no minimum rules for pointers since G_INTTOPTR and G_PTRTOINT can
be selected to a COPY from register class to another by the legalizer.

Minimum Legality For Operations
"""""""""""""""""""""""""""""""

The rules for G_ANYEXT, G_MERGE_VALUES, G_BITCAST, G_BUILD_VECTOR,
G_BUILD_VECTOR_TRUNC, G_CONCAT_VECTORS, G_UNMERGE_VALUES, G_PTRTOINT, and
G_INTTOPTR have already been noted above. In addition to those, the following
operations have requirements:

* At least one G_IMPLICIT_DEF must be legal. This is usually trivial as it
  requires no code to be selected.
* G_PHI must be legal for all types in the producer and consumer typesets. This
  is usually trivial as it requires no code to be selected.
* At least one G_FRAME_INDEX must be legal
* At least one G_BLOCK_ADDR must be legal

There are many other operations you'd expect to have legality requirements but
they can be lowered to target instructions which are legal by definition.
