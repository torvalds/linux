
.. _tblgen-mirpats:

========================
MIR Patterns in TableGen
========================

.. contents::
   :local:


User's Guide
============

This section is intended for developers who want to use MIR patterns in their
TableGen files.

``NOTE``:
This feature is still in active development. This document may become outdated
over time. If you see something that's incorrect, please update it.

Use Cases
---------

MIR patterns are supported in the following places:

* GlobalISel ``GICombineRule``
* GlobalISel ``GICombinePatFrag``

Syntax
------

MIR patterns use the DAG datatype in TableGen.

.. code-block:: text

  (inst operand0, operand1, ...)

``inst`` must be a def which inherits from ``Instruction`` (e.g. ``G_FADD``),
``Intrinsic`` or ``GICombinePatFrag``.

Operands essentially fall into one of two categories:

* immediates

  * untyped, unnamed: ``0``
  * untyped, named: ``0:$y``
  * typed, unnamed: ``(i32 0)``
  * typed, named: ``(i32 0):$y``

* machine operands

  * untyped: ``$x``
  * typed: ``i32:$x``

Semantics:

* A typed operand always adds an operand type check to the matcher.
* There is a trivial type inference system to propagate types.

  * e.g. You only need to use ``i32:$x`` once in any pattern of a
    ``GICombinePatFrag`` alternative or ``GICombineRule``, then all
    other patterns in that rule/alternative can simply use ``$x``
    (``i32:$x`` is redundant).

* A named operand's behavior depends on whether the name has been seen before.

  * For match patterns, reusing an operand name checks that the operands
    are identical (see example 2 below).
  * For apply patterns, reusing an operand name simply copies that operand into
    the new instruction (see example 2 below).

Operands are ordered just like they would be in a MachineInstr: the defs (outs)
come first, then the uses (ins).

Patterns are generally grouped into another DAG datatype with a dummy operator
such as ``match``, ``apply`` or ``pattern``.

Finally, any DAG datatype in TableGen can be named. This also holds for
patterns. e.g. the following is valid: ``(G_FOO $root, (i32 0):$cst):$mypat``.
This may also be helpful to debug issues. Patterns are *always* named, and if
they don't have a name, an "anonymous" one is given to them. If you're trying
to debug an error related to a MIR pattern, but the error mentions an anonymous
pattern, you can try naming your patterns to see exactly where the issue is.

.. code-block:: text
  :caption: Pattern Example 1

  // Match
  //    %imp = G_IMPLICIT_DEF
  //    %root = G_MUL %x, %imp
  (match (G_IMPLICIT_DEF $imp),
         (G_MUL $root, $x, $imp))

.. code-block:: text
  :caption: Pattern Example 2

  // using $x twice here checks that the operand 1 and 2 of the G_AND are
  // identical.
  (match (G_AND $root, $x, $x))
  // using $x again here copies operand 1 from G_AND into the new inst.
  (apply (COPY $root, $x))

Types
-----

ValueType
~~~~~~~~~

Subclasses of ``ValueType`` are valid types, e.g. ``i32``.

GITypeOf
~~~~~~~~

``GITypeOf<"$x">`` is a ``GISpecialType`` that allows for the creation of a
register or immediate with the same type as another (register) operand.

Operand:

* An operand name as a string, prefixed by ``$``.

Semantics:

* Can only appear in an 'apply' pattern.
* The operand name used must appear in the 'match' pattern of the
  same ``GICombineRule``.

.. code-block:: text
  :caption: Example: Immediate

  def mul_by_neg_one: GICombineRule <
    (defs root:$root),
    (match (G_MUL $dst, $x, -1)),
    (apply (G_SUB $dst, (GITypeOf<"$x"> 0), $x))
  >;

.. code-block:: text
  :caption: Example: Temp Reg

  def Test0 : GICombineRule<
    (defs root:$dst),
    (match (G_FMUL $dst, $src, -1)),
    (apply (G_FSUB $dst, $src, $tmp),
           (G_FNEG GITypeOf<"$dst">:$tmp, $src))>;

Builtin Operations
------------------

MIR Patterns also offer builtin operations, also called "builtin instructions".
They offer some powerful features that would otherwise require use of C++ code.

GIReplaceReg
~~~~~~~~~~~~

.. code-block:: text
  :caption: Usage

  (apply (GIReplaceReg $old, $new))

Operands:

* ``$old`` (out) register defined by a matched instruction
* ``$new`` (in)  register

Semantics:

* Can only appear in an 'apply' pattern.
* If both old/new are operands of matched instructions,
  ``canReplaceReg`` is checked before applying the rule.


GIEraseRoot
~~~~~~~~~~~

.. code-block:: text
  :caption: Usage

  (apply (GIEraseRoot))

Semantics:

* Can only appear as the only pattern of an 'apply' pattern list.
* The root cannot have any output operands.
* The root must be a CodeGenInstruction

Instruction Flags
-----------------

MIR Patterns support both matching & writing ``MIFlags``.

.. code-block:: text
  :caption: Example

  def Test : GICombineRule<
    (defs root:$dst),
    (match (G_FOO $dst, $src, (MIFlags FmNoNans, FmNoInfs))),
    (apply (G_BAR $dst, $src, (MIFlags FmReassoc)))>;

In ``apply`` patterns, we also support referring to a matched instruction to
"take" its MIFlags.

.. code-block:: text
  :caption: Example

  ; We match NoNans/NoInfs, but $zext may have more flags.
  ; Copy them all into the output instruction, and set Reassoc on the output inst.
  def TestCpyFlags : GICombineRule<
    (defs root:$dst),
    (match (G_FOO $dst, $src, (MIFlags FmNoNans, FmNoInfs)):$zext),
    (apply (G_BAR $dst, $src, (MIFlags $zext, FmReassoc)))>;

The ``not`` operator can be used to check that a flag is NOT present
on a matched instruction, and to remove a flag from a generated instruction.

.. code-block:: text
  :caption: Example

  ; We match NoInfs but we don't want NoNans/Reassoc to be set. $zext may have more flags.
  ; Copy them all into the output instruction but remove NoInfs on the output inst.
  def TestNot : GICombineRule<
    (defs root:$dst),
    (match (G_FOO $dst, $src, (MIFlags FmNoInfs, (not FmNoNans, FmReassoc))):$zext),
    (apply (G_BAR $dst, $src, (MIFlags $zext, (not FmNoInfs))))>;

Limitations
-----------

This a non-exhaustive list of known issues with MIR patterns at this time.

* Using ``GICombinePatFrag`` within another ``GICombinePatFrag`` is not
  supported.
* ``GICombinePatFrag`` can only have a single root.
* Instructions with multiple defs cannot be the root of a ``GICombinePatFrag``.
* Using ``GICombinePatFrag`` in the ``apply`` pattern of a ``GICombineRule``
  is not supported.
* We cannot rewrite a matched instruction other than the root.
* Matching/creating a (CImm) immediate >64 bits is not supported
  (see comment in ``GIM_CheckConstantInt``)
* There is currently no way to constrain two register/immediate types to
  match. e.g. if a pattern needs to work on both i32 and i64, you either
  need to leave it untyped and check the type in C++, or duplicate the
  pattern.

GICombineRule
-------------

MIR patterns can appear in the ``match`` or ``apply`` patterns of a
``GICombineRule``.

The ``root`` of the rule can either be a def of an instruction, or a
named pattern. The latter is helpful when the instruction you want
to match has no defs. The former is generally preferred because
it's less verbose.

.. code-block:: text
  :caption: Combine Rule root is a def

  // Fold x op 1 -> x
  def right_identity_one: GICombineRule<
    (defs root:$dst),
    (match (G_MUL $dst, $x, 1)),
    // Note: Patterns always need to create something, we can't just replace $dst with $x, so we need a COPY.
    (apply (COPY $dst, $x))
  >;

.. code-block:: text
  :caption: Combine Rule root is a named pattern

  def Foo : GICombineRule<
    (defs root:$root),
    (match (G_ZEXT $tmp, (i32 0)),
           (G_STORE $tmp, $ptr):$root),
    (apply (G_STORE (i32 0), $ptr):$root)>;


Combine Rules also allow mixing C++ code with MIR patterns, so that you
may perform additional checks when matching, or run a C++ action after
matching.

Note that C++ code in ``apply`` pattern is mutually exclusive with
other patterns. However, you can freely mix C++ code with other
types of patterns in ``match`` patterns.
C++ code in ``match`` patterns is always run last, after all other
patterns matched.

.. code-block:: text
  :caption: Apply Pattern Examples with C++ code

  // Valid
  def Foo : GICombineRule<
    (defs root:$root),
    (match (G_ZEXT $tmp, (i32 0)),
           (G_STORE $tmp, $ptr):$root,
           "return myFinalCheck()"),
    (apply "runMyAction(${root})")>;

  // error: 'apply' patterns cannot mix C++ code with other types of patterns
  def Bar : GICombineRule<
    (defs root:$dst),
    (match (G_ZEXT $dst, $src):$mi),
    (apply (G_MUL $dst, $src, $src),
           "runMyAction(${root})")>;

The following expansions are available for MIR patterns:

* operand names (``MachineOperand &``)
* pattern names (``MachineInstr *`` for ``match``,
  ``MachineInstrBuilder &`` for apply)

.. code-block:: text
  :caption: Example C++ Expansions

  def Foo : GICombineRule<
    (defs root:$root),
    (match (G_ZEXT $root, $src):$mi),
    (apply "foobar(${root}.getReg(), ${src}.getReg(), ${mi}->hasImplicitDef())")>;

Common Pattern #1: Replace a Register with Another
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The 'apply' pattern must always redefine all operands defined by the match root.
Sometimes, we do not need to create instructions, simply replace a def with
another matched register. The ``GIReplaceReg`` builtin can do just that.

.. code-block:: text

  def Foo : GICombineRule<
    (defs root:$dst),
    (match (G_FNEG $tmp, $src), (G_FNEG $dst, $tmp)),
    (apply (GIReplaceReg $dst, $src))>;

This also works if the replacement register is a temporary register from the
``apply`` pattern.

.. code-block:: text

  def ReplaceTemp : GICombineRule<
    (defs root:$a),
    (match    (G_BUILD_VECTOR $tmp, $x, $y),
              (G_UNMERGE_VALUES $a, $b, $tmp)),
    (apply  (G_UNMERGE_VALUES $a, i32:$new, $y),
            (GIReplaceReg $b, $new))>

Common Pattern #2: Erasing a Def-less Root
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If we simply want to erase a def-less match root, we can use the
``GIEraseRoot`` builtin.

.. code-block:: text

  def Foo : GICombineRule<
    (defs root:$mi),
    (match (G_STORE $a, $b):$mi),
    (apply (GIEraseRoot))>;

Common Pattern #3: Emitting a Constant Value
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When an immediate operand appears in an 'apply' pattern, the behavior
depends on whether it's typed or not.

* If the immediate is typed, ``MachineIRBuilder::buildConstant`` is used
  to create a ``G_CONSTANT``. A ``G_BUILD_VECTOR`` will be used for vectors.
* If the immediate is untyped, a simple immediate is added
  (``MachineInstrBuilder::addImm``).

There is of course a special case for ``G_CONSTANT``. Immediates for
``G_CONSTANT`` must always be typed, and a CImm is added
(``MachineInstrBuilder::addCImm``).

.. code-block:: text
  :caption: Constant Emission Examples:

  // Example output:
  //    %0 = G_CONSTANT i32 0
  //    %dst = COPY %0
  def Foo : GICombineRule<
    (defs root:$dst),
    (match (G_FOO $dst, $src)),
    (apply (COPY $dst, (i32 0)))>;

  // Example output:
  //    %dst = COPY 0
  // Note that this would be ill-formed because COPY
  // expects a register operand!
  def Bar : GICombineRule<
    (defs root:$dst),
    (match (G_FOO $dst, $src)),
    (apply (COPY $dst, (i32 0)))>;

  // Example output:
  //    %dst = G_CONSTANT i32 0
  def Bux : GICombineRule<
    (defs root:$dst),
    (match (G_FOO $dst, $src)),
    (apply (G_CONSTANT $dst, (i32 0)))>;

GICombinePatFrag
----------------

``GICombinePatFrag`` is an equivalent of ``PatFrags`` for MIR patterns.
They have two main usecases:

* Reduce repetition by creating a ``GICombinePatFrag`` for common
  patterns (see example 1).
* Implicitly duplicate a CombineRule for multiple variants of a
  pattern (see example 2).

A ``GICombinePatFrag`` is composed of three elements:

* zero or more ``in`` (def) parameter
* zero or more ``out`` parameter
* A list of MIR patterns that can match.

  * When a ``GICombinePatFrag`` is used within a pattern, the pattern is
    cloned once for each alternative that can match.

Parameters can have the following types:

* ``gi_mo``, which is the implicit default (no type = ``gi_mo``).

  * Refers to any operand of an instruction (register, BB ref, imm, etc.).
  * Can be used in both ``in`` and ``out`` parameters.
  * Users of the PatFrag can only use an operand name for this
    parameter (e.g. ``(my_pat_frag $foo)``).

* ``root``

  * This is identical to ``gi_mo``.
  * Can only be used in ``out`` parameters to declare the root of the
    pattern.
  * Non-empty ``out`` parameter lists must always have exactly one ``root``.

* ``gi_imm``

  * Refers to an (potentially typed) immediate.
  * Can only be used in ``in`` parameters.
  * Users of the PatFrag can only use an immediate for this parameter
    (e.g. ``(my_pat_frag 0)`` or ``(my_pat_frag (i32 0))``)

``out`` operands can only be empty if the ``GICombinePatFrag`` only contains
C++ code. If the fragment contains instruction patterns, it has to have at
least one ``out`` operand of type ``root``.

``in`` operands are less restricted, but there is one important concept to
remember: you can pass "unbound" operand names, but only if the
``GICombinePatFrag`` binds it. See example 3 below.

``GICombinePatFrag`` are used just like any other instructions.
Note that the ``out`` operands are defs, so they come first in the list
of operands.

.. code-block:: text
  :caption: Example 1: Reduce Repetition

  def zext_cst : GICombinePatFrag<(outs root:$dst, $cst), (ins gi_imm:$val),
    [(pattern (G_CONSTANT $cst, $val),
              (G_ZEXT $dst, $cst))]
  >;

  def foo_to_impdef : GICombineRule<
   (defs root:$dst),
   (match (zext_cst $y, $cst, (i32 0))
          (G_FOO $dst, $y)),
   (apply (G_IMPLICIT_DEF $dst))>;

  def store_ext_zero : GICombineRule<
   (defs root:$root),
   (match (zext_cst $y, $cst, (i32 0))
          (G_STORE $y, $ptr):$root),
   (apply (G_STORE $cst, $ptr):$root)>;

.. code-block:: text
  :caption: Example 2: Generate Multiple Rules at Once

  // Fold (freeze (freeze x)) -> (freeze x).
  // Fold (fabs (fabs x)) -> (fabs x).
  // Fold (fcanonicalize (fcanonicalize x)) -> (fcanonicalize x).
  def idempotent_prop_frags : GICombinePatFrag<(outs root:$dst, $src), (ins),
    [
      (pattern (G_FREEZE $dst, $src), (G_FREEZE $src, $x)),
      (pattern (G_FABS $dst, $src), (G_FABS $src, $x)),
      (pattern (G_FCANONICALIZE $dst, $src), (G_FCANONICALIZE $src, $x))
    ]
  >;

  def idempotent_prop : GICombineRule<
    (defs root:$dst),
    (match (idempotent_prop_frags $dst, $src)),
    (apply (COPY $dst, $src))>;



.. code-block:: text
  :caption: Example 3: Unbound Operand Names

  // This fragment binds $x to an operand in all of its
  // alternative patterns.
  def always_binds : GICombinePatFrag<
    (outs root:$dst), (ins $x),
    [
      (pattern (G_FREEZE $dst, $x)),
      (pattern (G_FABS $dst, $x)),
    ]
  >;

  // This fragment does not bind $x to an operand in any
  // of its alternative patterns.
  def does_not_bind : GICombinePatFrag<
    (outs root:$dst), (ins $x),
    [
      (pattern (G_FREEZE $dst, $x)), // binds $x
      (pattern (G_FOO $dst (i32 0))), // does not bind $x
      (pattern "return myCheck(${x}.getReg())"), // does not bind $x
    ]
  >;

  // Here we pass $x, which is unbound, to always_binds.
  // This works because if $x is unbound, always_binds will bind it for us.
  def test0 : GICombineRule<
    (defs root:$dst),
    (match (always_binds $dst, $x)),
    (apply (COPY $dst, $x))>;

  // Here we pass $x, which is unbound, to does_not_bind.
  // This cannot work because $x may not have been initialized in 'apply'.
  // error: operand 'x' (for parameter 'src' of 'does_not_bind') cannot be unbound
  def test1 : GICombineRule<
    (defs root:$dst),
    (match (does_not_bind $dst, $x)),
    (apply (COPY $dst, $x))>;

  // Here we pass $x, which is bound, to does_not_bind.
  // This is fine because $x will always be bound when emitting does_not_bind
  def test2 : GICombineRule<
    (defs root:$dst),
    (match (does_not_bind $tmp, $x)
           (G_MUL $dst, $x, $tmp)),
    (apply (COPY $dst, $x))>;




Gallery
=======

We should use precise patterns that state our intentions. Please avoid
using wip_match_opcode in patterns.

.. code-block:: text
  :caption: Example fold zext(trunc:nuw)

  // Imprecise: matches any G_ZEXT
  def zext : GICombineRule<
    (defs root:$root),
    (match (wip_match_opcode G_ZEXT):$root,
    [{ return Helper.matchZextOfTrunc(*${root}, ${matchinfo}); }]),
    (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;


  // Imprecise: matches G_ZEXT of G_TRUNC
  def zext_of_trunc : GICombineRule<
    (defs root:$root),
    (match (G_TRUNC $src, $x),
           (G_ZEXT $root, $src),
    [{ return Helper.matchZextOfTrunc(${root}, ${matchinfo}); }]),
    (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;


  // Precise: matches G_ZEXT of G_TRUNC with nuw flag
  def zext_of_trunc_nuw : GICombineRule<
    (defs root:$root),
    (match (G_TRUNC $src, $x, (MIFlags NoUWrap)),
           (G_ZEXT $root, $src),
    [{ return Helper.matchZextOfTrunc(${root}, ${matchinfo}); }]),
    (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;
