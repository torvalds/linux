===================================
Memory Model Relaxation Annotations
===================================

.. contents::
   :local:

Introduction
============

Memory Model Relaxation Annotations (MMRAs) are target-defined properties
on instructions that can be used to selectively relax constraints placed
by the memory model. For example:

* The use of ``VulkanMemoryModel`` in a SPIRV program allows certain
  memory operations to be reordered across ``acquire`` or ``release``
  operations.
* OpenCL APIs expose primitives to only fence a specific set of address
  spaces. Carrying that information to the backend can enable the
  use of faster synchronization instructions, rather than fencing all
  address spaces everytime.

MMRAs offer an opt-in system for targets to relax the default LLVM
memory model.
As such, they are attached to an operation using LLVM metadata which
can always be dropped without affecting correctness.

Definitions
===========

memory operation
    A load, a store, an atomic, or a function call that is marked as
    accessing memory.

synchronizing operation
    An instruction that synchronizes memory with other threads (e.g.
    an atomic or a fence).

tag
    Metadata attached to a memory or synchronizing operation
    that represents some target-defined property regarding memory
    synchronization.

    An operation may have multiple tags that each represent a different
    property.

    A tag is composed of a pair of metadata string: a *prefix* and a *suffix*.

    In LLVM IR, the pair is represented using a metadata tuple.
    In other cases (comments, documentation, etc.), we may use the
    ``prefix:suffix`` notation.
    For example:

    .. code-block::
      :caption: Example: Tags in Metadata

      !0 = !{!"scope", !"workgroup"}  # scope:workgroup
      !1 = !{!"scope", !"device"}     # scope:device
      !2 = !{!"scope", !"system"}     # scope:system

    .. note::

      The only semantics relevant to the optimizer is the
      "compatibility" relation defined below. All other
      semantics are target defined.

    Tags can also be organised in lists to allow operations
    to specify all of the tags they belong to. Such a list
    is referred to as a "set of tags".

    .. code-block::
      :caption: Example: Set of Tags in Metadata

      !0 = !{!"scope", !"workgroup"}
      !1 = !{!"sync-as", !"private"}
      !2 = !{!0, !2}

    .. note::

      If an operation does not have MMRA metadata, it's treated as if
      it has an empty list (``!{}``) of tags.

    Note that it is not an error if a tag is not recognized by the
    instruction it is applied to, or by the current target.
    Such tags are simply ignored.

    Both synchronizing operations and memory operations can have
    zero or more tags attached to them using the ``!mmra`` syntax.

    For the sake of readability in examples below,
    we use a (non-functional) short syntax to represent MMMRA metadata:

    .. code-block::
      :caption: Short Syntax Example

      store %ptr1 # foo:bar
      store %ptr1 !mmra !{!"foo", !"bar"}

    These two notations can be used in this document and are strictly
    equivalent. However, only the second version is functional.

compatibility
    Two sets of tags are said to be *compatible* iff, for every unique
    tag prefix P present in at least one set:

    - the other set contains no tag with prefix P, or
    - at least one tag with prefix P is common to both sets.

    The above definition implies that an empty set is always compatible
    with any other set. This is an important property as it ensures that
    if a transform drops the metadata on an operation, it can never affect
    correctness. In other words, the memory model cannot be relaxed further
    by deleting metadata from instructions.

.. _HappensBefore:

The *happens-before* Relation
==============================

Compatibility checks can be used to opt out of the *happens-before* relation
established between two instructions.

Ordering
    When two instructions' metadata are not compatible, any program order
    between them are not in *happens-before*.

    For example, consider two tags ``foo:bar`` and
    ``foo:baz`` exposed by a target:

    .. code-block::

       A: store %ptr1                 # foo:bar
       B: store %ptr2                 # foo:baz
       X: store atomic release %ptr3  # foo:bar

    In the above figure, ``A`` is compatible with ``X``, and hence ``A``
    happens-before ``X``. But ``B`` is not compatible with
    ``X``, and hence it is not happens-before ``X``.

Synchronization
    If an synchronizing operation has one or more tags, then whether it
    synchronizes-with and participates in the  ``seq_cst`` order with
    other operations is target dependent.

    Whether the following example synchronizes with another sequence depends
    on the target-defined semantics of ``foo:bar`` and ``foo:bux``.

    .. code-block::

       fence release               # foo:bar
       store atomic %ptr1          # foo:bux

Examples
--------

Example 1:
    .. code-block::

      A: store ptr addrspace(1) %ptr2                  # sync-as:1 vulkan:nonprivate
      B: store atomic release ptr addrspace(1) %ptr3   # sync-as:0 vulkan:nonprivate

    A and B are not ordered relative to each other
    (no *happens-before*) because their sets of tags are not compatible.

    Note that the ``sync-as`` value does not have to match the ``addrspace`` value.
    e.g. In Example 1, a store-release to a location in ``addrspace(1)`` wants to
    only synchronize with operations happening in ``addrspace(0)``.

Example 2:
    .. code-block::

      A: store ptr addrspace(1) %ptr2                 # sync-as:1 vulkan:nonprivate
      B: store atomic release ptr addrspace(1) %ptr3  # sync-as:1 vulkan:nonprivate

    The ordering of A and B is unaffected because their set of tags are
    compatible.

    Note that A and B may or may not be in *happens-before* due to other reasons.

Example 3:
    .. code-block::

      A: store ptr addrspace(1) %ptr2                 # sync-as:1 vulkan:nonprivate
      B: store atomic release ptr addrspace(1) %ptr3  # vulkan:nonprivate

    The ordering of A and B is unaffected because their set of tags are
    compatible.

Example 4:
    .. code-block::

      A: store ptr addrspace(1) %ptr2                 # sync-as:1
      B: store atomic release ptr addrspace(1) %ptr3  # sync-as:2

    A and B do not have to be ordered relative to each other
    (no *happens-before*) because their sets of tags are not compatible.

Use-cases
=========

SPIRV ``NonPrivatePointer``
---------------------------

MMRAs can support the SPIRV capability
``VulkanMemoryModel``, where synchronizing operations only affect
memory operations that specify ``NonPrivatePointer`` semantics.

The example below is generated from a SPIRV program using the
following recipe:

- Add ``vulkan:nonprivate`` to every synchronizing operation.
- Add ``vulkan:nonprivate`` to every non-atomic memory operation
  that is marked ``NonPrivatePointer``.
- Add ``vulkan:private`` to tags of every non-atomic memory operation
  that is not marked ``NonPrivatePointer``.

.. code-block::

   Thread T1:
    A: store %ptr1                 # vulkan:nonprivate
    B: store %ptr2                 # vulkan:private
    X: store atomic release %ptr3  # vulkan:nonprivate

   Thread T2:
    Y: load atomic acquire %ptr3   # vulkan:nonprivate
    C: load %ptr2                  # vulkan:private
    D: load %ptr1                  # vulkan:nonprivate

Compatibility ensures that operation ``A`` is ordered
relative to ``X`` while operation ``D`` is ordered relative to ``Y``.
If ``X`` synchronizes with ``Y``, then ``A`` happens-before ``D``.
No such relation can be inferred about operations ``B`` and ``C``.

.. note::
   The `Vulkan Memory Model <https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#memory-model-non-private>`_
   considers all atomic operation non-private.

   Whether ``vulkan:nonprivate`` would be specified on atomic operations is
   an implementation detail, as an atomic operation is always ``nonprivate``.
   The implementation may choose to be explicit and emit IR with
   ``vulkan:nonprivate`` on every atomic operation, or it could choose to
   only emit ``vulkan::private`` and assume ``vulkan:nonprivate``
   by default.

Operations marked with ``vulkan:private`` effectively opt out of the
happens-before order in a SPIRV program since they are incompatible
with every synchronizing operation. Note that SPIRV operations that
are not marked ``NonPrivatePointer`` are not entirely private to the
thread --- they are implicitly synchronized at the start or end of a
thread by the Vulkan *system-synchronizes-with* relationship. This
example assumes that the target-defined semantics of
``vulkan:private`` correctly implements this property.

This scheme is general enough to express the interoperability of SPIRV
programs with other environments.

.. code-block::

   Thread T1:
   A: store %ptr1                 # vulkan:nonprivate
   X: store atomic release %ptr2  # vulkan:nonprivate

   Thread T2:
   Y: load atomic acquire %ptr2   # foo:bar
   B: load %ptr1

In the above example, thread ``T1`` originates from a SPIRV program
while thread ``T2`` originates from a non-SPIRV program. Whether ``X``
can synchronize with ``Y`` is target defined.  If ``X`` synchronizes
with ``Y``, then ``A`` happens before ``B`` (because A/X and
Y/B are compatible).

Implementation Example
~~~~~~~~~~~~~~~~~~~~~~

Consider the implementation of SPIRV ``NonPrivatePointer`` on a target
where all memory operations are cached, and the entire cache is
flushed or invalidated at a ``release`` or ``acquire`` respectively. A
possible scheme is that when translating a SPIRV program, memory
operations marked ``NonPrivatePointer`` should not be cached, and the
cache contents should not be touched during an ``acquire`` and
``release`` operation.

This could be implemented using the tags that share the ``vulkan:`` prefix,
as follows:

- For memory operations:

  - Operations with ``vulkan:nonprivate`` should bypass the cache.
  - Operations with ``vulkan:private`` should be cached.
  - Operations that specify neither or both should conservatively
    bypass the cache to ensure correctness.

- For synchronizing operations:

  - Operations with ``vulkan:nonprivate`` should not flush or
    invalidate the cache.
  - Operations with ``vulkan:private`` should flush or invalidate the cache.
  - Operations that specify neither or both should conservatively
    flush or invalidate the cache to ensure correctness.

.. note::
   In such an implementation, dropping the metadata on an operation, while
   not affecting correctness, may have big performance implications.
   e.g. an operation bypasses the cache when it shouldn't.

Memory Types
------------

MMRAs may express the selective synchronization of
different memory types.

As an example, a target may expose an ``sync-as:<N>`` tag to
pass information about which address spaces are synchronized by the
execution of a synchronizing operation.

.. note::
  Address spaces are used here as a common example, but this concept
  can apply for other "memory types". What "memory types" means here is
  up to the target.

.. code-block::

   # let 1 = global address space
   # let 3 = local address space

   Thread T1:
   A: store %ptr1                                  # sync-as:1
   B: store %ptr2                                  # sync-as:3
   X: store atomic release ptr addrspace(0) %ptr3  # sync-as:3

   Thread T2:
   Y: load atomic acquire ptr addrspace(0) %ptr3   # sync-as:3
   C: load %ptr2                                   # sync-as:3
   D: load %ptr1                                   # sync-as:1

In the above figure, ``X`` and ``Y`` are atomic operations on a
location in the ``global``  address space. If ``X`` synchronizes with
``Y``, then ``B`` happens-before ``C`` in the ``local`` address
space. But no such statement can be made about operations ``A`` and
``D``, although they are peformed on a location in the ``global``
address space.

Implementation Example: Adding Address Space Information to Fences
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Languages such as OpenCL C provide fence operations such as
``atomic_work_item_fence`` that can take an explicit address
space to fence.

By default, LLVM has no means to carry that information in the IR, so
the information is lost during lowering to LLVM IR. This means that
targets such as AMDGPU have to conservatively emit instructions to
fence all address spaces in all cases, which can have a noticeable
performance impact in high-performance applications.

MMRAs may be used to preserve that information at the IR level, all the
way through code generation. For example, a fence that only affects the
global address space ``addrspace(1)`` may be lowered as

.. code-block::

    fence release # sync-as:1

and the target may use the presence of ``sync-as:1`` to infer that it
must only emit instruction to fence the global address space.

Note that as MMRAs are opt in, a fence that does not have MMRA metadata
could still be lowered conservatively, so this optimization would only
apply if the front-end emits the MMRA metadata on the fence instructions.

Additional Topics
=================

.. note::

  The following sections are informational.

Performance Impact
------------------

MMRAs are a way to capture optimization opportunities in the program.
But when an operation mentions no tags or conflicting tags,
the target may need to produce conservative code to ensure correctness
at the cost of performance. This can happen in the following situations:

1. When a target first introduces MMRAs, the
   frontend might not have been updated to emit them.
2. An optimization may drop MMRA metadata.
3. An optimization may add arbitrary tags to an operation.

Note that targets can always choose to ignore (or even drop) MMRAs
and revert to the default behavior/codegen heuristics without
affecting correctness.

Consequences of the Absence of *happens-before*
-----------------------------------------------

In the :ref:`happens-before<HappensBefore>` section, we defined how an
*happens-before* relation between two instruction can be broken
by leveraging compatibility between MMRAs. When the instructions
are incompatible and there is no *happens-before* relation, we say
that the instructions "do not have to be ordered relative to each
other".

"Ordering" in this context is a very broad term which covers both
static and runtime aspects.

When there is no ordering constraint, we *could* statically reorder
the instructions in an optimizer transform if the reordering does
not break other constraints as single location coherence.
Static reordering is one consequence of breaking *happens-before*,
but is not the most interesting one.

Run-time consequences are more interesting. When there is an
*happens-before* relation between instructions, the target has to emit
synchronization code to ensure other threads will observe the effects of
the instructions in the right order.

For instance, the target may have to wait for previous loads & stores to
finish before starting a fence-release, or there may be a need to flush a
memory cache before executing the next instruction.
In the absence of *happens-before*, there is no such requirement and
no waiting or flushing is required. This may noticeably speed up
execution in some cases.

Combining Operations
--------------------

If a pass can combine multiple memory or synchronizing operations
into one, it needs to be able to combine MMRAs. One possible way to
achieve this is by doing a prefix-wise union of the tag sets.

Let A and B be two tags set, and U be the prefix-wise union of A and B.
For every unique tag prefix P present in A or B:

* If either A or B has no tags with prefix P, no tags with prefix
  P are added to U.
* If both A and B have at least one tag with prefix P, all tags with prefix
  P from both sets are added to U.

Passes should avoid aggressively combining MMRAs, as this can result
in significant losses of information. While this cannot affect
correctness, it may affect performance.

As a general rule of thumb, common passes such as SimplifyCFG that
aggressively combine/reorder operations should only combine
instructions that have identical sets of tags.
Passes that combine less frequently, or that are well aware of the cost
of combining the MMRAs can use the prefix-wise union described above.

Examples:

.. code-block::

    A: store release %ptr1  # foo:x, foo:y, bar:x
    B: store release %ptr2  # foo:x, bar:y

    # Unique prefixes P = [foo, bar]
    # "foo:x" is common to A and B so it's added to U.
    # "bar:x" != "bar:y" so it's not added to U.
    U: store release %ptr3  # foo:x

.. code-block::

    A: store release %ptr1  # foo:x, foo:y
    B: store release %ptr2  # foo:x, bux:y

    # Unique prefixes P = [foo, bux]
    # "foo:x" is common to A and B so it's added to U.
    # No tags have the prefix "bux" in A.
    U: store release %ptr3  # foo:x

.. code-block::

    A: store release %ptr1
    B: store release %ptr2  # foo:x, bar:y

    # Unique prefixes P = [foo, bar]
    # No tags with "foo" or "bar" in A, so no tags added.
    U: store release %ptr3
