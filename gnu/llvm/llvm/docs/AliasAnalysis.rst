==================================
LLVM Alias Analysis Infrastructure
==================================

.. contents::
   :local:

Introduction
============

Alias Analysis (aka Pointer Analysis) is a class of techniques which attempt to
determine whether or not two pointers ever can point to the same object in
memory.  There are many different algorithms for alias analysis and many
different ways of classifying them: flow-sensitive vs. flow-insensitive,
context-sensitive vs. context-insensitive, field-sensitive
vs. field-insensitive, unification-based vs. subset-based, etc.  Traditionally,
alias analyses respond to a query with a `Must, May, or No`_ alias response,
indicating that two pointers always point to the same object, might point to the
same object, or are known to never point to the same object.

The LLVM `AliasAnalysis
<https://llvm.org/doxygen/classllvm_1_1AliasAnalysis.html>`__ class is the
primary interface used by clients and implementations of alias analyses in the
LLVM system.  This class is the common interface between clients of alias
analysis information and the implementations providing it, and is designed to
support a wide range of implementations and clients (but currently all clients
are assumed to be flow-insensitive).  In addition to simple alias analysis
information, this class exposes Mod/Ref information from those implementations
which can provide it, allowing for powerful analyses and transformations to work
well together.

This document contains information necessary to successfully implement this
interface, use it, and to test both sides.  It also explains some of the finer
points about what exactly results mean.

``AliasAnalysis`` Class Overview
================================

The `AliasAnalysis <https://llvm.org/doxygen/classllvm_1_1AliasAnalysis.html>`__
class defines the interface that the various alias analysis implementations
should support.  This class exports two important enums: ``AliasResult`` and
``ModRefResult`` which represent the result of an alias query or a mod/ref
query, respectively.

The ``AliasAnalysis`` interface exposes information about memory, represented in
several different ways.  In particular, memory objects are represented as a
starting address and size, and function calls are represented as the actual
``call`` or ``invoke`` instructions that performs the call.  The
``AliasAnalysis`` interface also exposes some helper methods which allow you to
get mod/ref information for arbitrary instructions.

All ``AliasAnalysis`` interfaces require that in queries involving multiple
values, values which are not :ref:`constants <constants>` are all
defined within the same function.

Representation of Pointers
--------------------------

Most importantly, the ``AliasAnalysis`` class provides several methods which are
used to query whether or not two memory objects alias, whether function calls
can modify or read a memory object, etc.  For all of these queries, memory
objects are represented as a pair of their starting address (a symbolic LLVM
``Value*``) and a static size.

Representing memory objects as a starting address and a size is critically
important for correct Alias Analyses.  For example, consider this (silly, but
possible) C code:

.. code-block:: c++

  int i;
  char C[2];
  char A[10];
  /* ... */
  for (i = 0; i != 10; ++i) {
    C[0] = A[i];          /* One byte store */
    C[1] = A[9-i];        /* One byte store */
  }

In this case, the ``basic-aa`` pass will disambiguate the stores to ``C[0]`` and
``C[1]`` because they are accesses to two distinct locations one byte apart, and
the accesses are each one byte.  In this case, the Loop Invariant Code Motion
(LICM) pass can use store motion to remove the stores from the loop.  In
contrast, the following code:

.. code-block:: c++

  int i;
  char C[2];
  char A[10];
  /* ... */
  for (i = 0; i != 10; ++i) {
    ((short*)C)[0] = A[i];  /* Two byte store! */
    C[1] = A[9-i];          /* One byte store */
  }

In this case, the two stores to C do alias each other, because the access to the
``&C[0]`` element is a two byte access.  If size information wasn't available in
the query, even the first case would have to conservatively assume that the
accesses alias.

.. _alias:

The ``alias`` method
--------------------

The ``alias`` method is the primary interface used to determine whether or not
two memory objects alias each other.  It takes two memory objects as input and
returns MustAlias, PartialAlias, MayAlias, or NoAlias as appropriate.

Like all ``AliasAnalysis`` interfaces, the ``alias`` method requires that either
the two pointer values be defined within the same function, or at least one of
the values is a :ref:`constant <constants>`.

.. _Must, May, or No:

Must, May, and No Alias Responses
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``NoAlias`` response may be used when there is never an immediate dependence
between any memory reference *based* on one pointer and any memory reference
*based* the other. The most obvious example is when the two pointers point to
non-overlapping memory ranges. Another is when the two pointers are only ever
used for reading memory. Another is when the memory is freed and reallocated
between accesses through one pointer and accesses through the other --- in this
case, there is a dependence, but it's mediated by the free and reallocation.

As an exception to this is with the :ref:`noalias <noalias>` keyword;
the "irrelevant" dependencies are ignored.

The ``MayAlias`` response is used whenever the two pointers might refer to the
same object.

The ``PartialAlias`` response is used when the two memory objects are known to
be overlapping in some way, regardless whether they start at the same address
or not.

The ``MustAlias`` response may only be returned if the two memory objects are
guaranteed to always start at exactly the same location. A ``MustAlias``
response does not imply that the pointers compare equal.

The ``getModRefInfo`` methods
-----------------------------

The ``getModRefInfo`` methods return information about whether the execution of
an instruction can read or modify a memory location.  Mod/Ref information is
always conservative: if an instruction **might** read or write a location,
``ModRef`` is returned.

The ``AliasAnalysis`` class also provides a ``getModRefInfo`` method for testing
dependencies between function calls.  This method takes two call sites (``CS1``
& ``CS2``), returns ``NoModRef`` if neither call writes to memory read or
written by the other, ``Ref`` if ``CS1`` reads memory written by ``CS2``,
``Mod`` if ``CS1`` writes to memory read or written by ``CS2``, or ``ModRef`` if
``CS1`` might read or write memory written to by ``CS2``.  Note that this
relation is not commutative.

Other useful ``AliasAnalysis`` methods
--------------------------------------

Several other tidbits of information are often collected by various alias
analysis implementations and can be put to good use by various clients.

The ``getModRefInfoMask`` method
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``getModRefInfoMask`` method returns a bound on Mod/Ref information for
the supplied pointer, based on knowledge about whether the pointer points to
globally-constant memory (for which it returns ``NoModRef``) or
locally-invariant memory (for which it returns ``Ref``). Globally-constant
memory includes functions, constant global variables, and the null pointer.
Locally-invariant memory is memory that we know is invariant for the lifetime
of its SSA value, but not necessarily for the life of the program: for example,
the memory pointed to by ``readonly`` ``noalias`` parameters is known-invariant
for the duration of the corresponding function call. Given Mod/Ref information
``MRI`` for a memory location ``Loc``, ``MRI`` can be refined with a statement
like ``MRI &= AA.getModRefInfoMask(Loc);``. Another useful idiom is
``isModSet(AA.getModRefInfoMask(Loc))``; this checks to see if the given
location can be modified at all. For convenience, there is also a method
``pointsToConstantMemory(Loc)``; this is synonymous with
``isNoModRef(AA.getModRefInfoMask(Loc))``.

.. _never access memory or only read memory:

The ``doesNotAccessMemory`` and  ``onlyReadsMemory`` methods
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These methods are used to provide very simple mod/ref information for function
calls.  The ``doesNotAccessMemory`` method returns true for a function if the
analysis can prove that the function never reads or writes to memory, or if the
function only reads from constant memory.  Functions with this property are
side-effect free and only depend on their input arguments, allowing them to be
eliminated if they form common subexpressions or be hoisted out of loops.  Many
common functions behave this way (e.g., ``sin`` and ``cos``) but many others do
not (e.g., ``acos``, which modifies the ``errno`` variable).

The ``onlyReadsMemory`` method returns true for a function if analysis can prove
that (at most) the function only reads from non-volatile memory.  Functions with
this property are side-effect free, only depending on their input arguments and
the state of memory when they are called.  This property allows calls to these
functions to be eliminated and moved around, as long as there is no store
instruction that changes the contents of memory.  Note that all functions that
satisfy the ``doesNotAccessMemory`` method also satisfy ``onlyReadsMemory``.

Writing a new ``AliasAnalysis`` Implementation
==============================================

Writing a new alias analysis implementation for LLVM is quite straight-forward.
There are already several implementations that you can use for examples, and the
following information should help fill in any details.  For examples, take a
look at the `various alias analysis implementations`_ included with LLVM.

Different Pass styles
---------------------

The first step to determining what type of :doc:`LLVM pass <WritingAnLLVMPass>`
you need to use for your Alias Analysis.  As is the case with most other
analyses and transformations, the answer should be fairly obvious from what type
of problem you are trying to solve:

#. If you require interprocedural analysis, it should be a ``Pass``.
#. If you are a function-local analysis, subclass ``FunctionPass``.
#. If you don't need to look at the program at all, subclass ``ImmutablePass``.

In addition to the pass that you subclass, you should also inherit from the
``AliasAnalysis`` interface, of course, and use the ``RegisterAnalysisGroup``
template to register as an implementation of ``AliasAnalysis``.

Required initialization calls
-----------------------------

Your subclass of ``AliasAnalysis`` is required to invoke two methods on the
``AliasAnalysis`` base class: ``getAnalysisUsage`` and
``InitializeAliasAnalysis``.  In particular, your implementation of
``getAnalysisUsage`` should explicitly call into the
``AliasAnalysis::getAnalysisUsage`` method in addition to doing any declaring
any pass dependencies your pass has.  Thus you should have something like this:

.. code-block:: c++

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AliasAnalysis::getAnalysisUsage(AU);
    // declare your dependencies here.
  }

Additionally, your must invoke the ``InitializeAliasAnalysis`` method from your
analysis run method (``run`` for a ``Pass``, ``runOnFunction`` for a
``FunctionPass``, or ``InitializePass`` for an ``ImmutablePass``).  For example
(as part of a ``Pass``):

.. code-block:: c++

  bool run(Module &M) {
    InitializeAliasAnalysis(this);
    // Perform analysis here...
    return false;
  }

Required methods to override
----------------------------

You must override the ``getAdjustedAnalysisPointer`` method on all subclasses
of ``AliasAnalysis``. An example implementation of this method would look like:

.. code-block:: c++

  void *getAdjustedAnalysisPointer(const void* ID) override {
    if (ID == &AliasAnalysis::ID)
      return (AliasAnalysis*)this;
    return this;
  }

Interfaces which may be specified
---------------------------------

All of the `AliasAnalysis
<https://llvm.org/doxygen/classllvm_1_1AliasAnalysis.html>`__ virtual methods
default to providing :ref:`chaining <aliasanalysis-chaining>` to another alias
analysis implementation, which ends up returning conservatively correct
information (returning "May" Alias and "Mod/Ref" for alias and mod/ref queries
respectively).  Depending on the capabilities of the analysis you are
implementing, you just override the interfaces you can improve.

.. _aliasanalysis-chaining:

``AliasAnalysis`` chaining behavior
-----------------------------------

Every alias analysis pass chains to another alias analysis implementation (for
example, the user can specify "``-basic-aa -ds-aa -licm``" to get the maximum
benefit from both alias analyses).  The alias analysis class automatically
takes care of most of this for methods that you don't override.  For methods
that you do override, in code paths that return a conservative MayAlias or
Mod/Ref result, simply return whatever the superclass computes.  For example:

.. code-block:: c++

  AliasResult alias(const Value *V1, unsigned V1Size,
                    const Value *V2, unsigned V2Size) {
    if (...)
      return NoAlias;
    ...

    // Couldn't determine a must or no-alias result.
    return AliasAnalysis::alias(V1, V1Size, V2, V2Size);
  }

In addition to analysis queries, you must make sure to unconditionally pass LLVM
`update notification`_ methods to the superclass as well if you override them,
which allows all alias analyses in a change to be updated.

.. _update notification:

Updating analysis results for transformations
---------------------------------------------

Alias analysis information is initially computed for a static snapshot of the
program, but clients will use this information to make transformations to the
code.  All but the most trivial forms of alias analysis will need to have their
analysis results updated to reflect the changes made by these transformations.

The ``AliasAnalysis`` interface exposes four methods which are used to
communicate program changes from the clients to the analysis implementations.
Various alias analysis implementations should use these methods to ensure that
their internal data structures are kept up-to-date as the program changes (for
example, when an instruction is deleted), and clients of alias analysis must be
sure to call these interfaces appropriately.

The ``deleteValue`` method
^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``deleteValue`` method is called by transformations when they remove an
instruction or any other value from the program (including values that do not
use pointers).  Typically alias analyses keep data structures that have entries
for each value in the program.  When this method is called, they should remove
any entries for the specified value, if they exist.

The ``copyValue`` method
^^^^^^^^^^^^^^^^^^^^^^^^

The ``copyValue`` method is used when a new value is introduced into the
program.  There is no way to introduce a value into the program that did not
exist before (this doesn't make sense for a safe compiler transformation), so
this is the only way to introduce a new value.  This method indicates that the
new value has exactly the same properties as the value being copied.

The ``replaceWithNewValue`` method
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This method is a simple helper method that is provided to make clients easier to
use.  It is implemented by copying the old analysis information to the new
value, then deleting the old value.  This method cannot be overridden by alias
analysis implementations.

The ``addEscapingUse`` method
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``addEscapingUse`` method is used when the uses of a pointer value have
changed in ways that may invalidate precomputed analysis information.
Implementations may either use this callback to provide conservative responses
for points whose uses have change since analysis time, or may recompute some or
all of their internal state to continue providing accurate responses.

In general, any new use of a pointer value is considered an escaping use, and
must be reported through this callback, *except* for the uses below:

* A ``bitcast`` or ``getelementptr`` of the pointer
* A ``store`` through the pointer (but not a ``store`` *of* the pointer)
* A ``load`` through the pointer

Efficiency Issues
-----------------

From the LLVM perspective, the only thing you need to do to provide an efficient
alias analysis is to make sure that alias analysis **queries** are serviced
quickly.  The actual calculation of the alias analysis results (the "run"
method) is only performed once, but many (perhaps duplicate) queries may be
performed.  Because of this, try to move as much computation to the run method
as possible (within reason).

Limitations
-----------

The AliasAnalysis infrastructure has several limitations which make writing a
new ``AliasAnalysis`` implementation difficult.

There is no way to override the default alias analysis. It would be very useful
to be able to do something like "``opt -my-aa -O2``" and have it use ``-my-aa``
for all passes which need AliasAnalysis, but there is currently no support for
that, short of changing the source code and recompiling. Similarly, there is
also no way of setting a chain of analyses as the default.

There is no way for transform passes to declare that they preserve
``AliasAnalysis`` implementations. The ``AliasAnalysis`` interface includes
``deleteValue`` and ``copyValue`` methods which are intended to allow a pass to
keep an AliasAnalysis consistent, however there's no way for a pass to declare
in its ``getAnalysisUsage`` that it does so. Some passes attempt to use
``AU.addPreserved<AliasAnalysis>``, however this doesn't actually have any
effect.

Similarly, the ``opt -p`` option introduces ``ModulePass`` passes between each
pass, which prevents the use of ``FunctionPass`` alias analysis passes.

The ``AliasAnalysis`` API does have functions for notifying implementations when
values are deleted or copied, however these aren't sufficient. There are many
other ways that LLVM IR can be modified which could be relevant to
``AliasAnalysis`` implementations which can not be expressed.

The ``AliasAnalysisDebugger`` utility seems to suggest that ``AliasAnalysis``
implementations can expect that they will be informed of any relevant ``Value``
before it appears in an alias query. However, popular clients such as ``GVN``
don't support this, and are known to trigger errors when run with the
``AliasAnalysisDebugger``.

The ``AliasSetTracker`` class (which is used by ``LICM``) makes a
non-deterministic number of alias queries. This can cause debugging techniques
involving pausing execution after a predetermined number of queries to be
unreliable.

Many alias queries can be reformulated in terms of other alias queries. When
multiple ``AliasAnalysis`` queries are chained together, it would make sense to
start those queries from the beginning of the chain, with care taken to avoid
infinite looping, however currently an implementation which wants to do this can
only start such queries from itself.

Using alias analysis results
============================

There are several different ways to use alias analysis results.  In order of
preference, these are:

Using the ``MemoryDependenceAnalysis`` Pass
-------------------------------------------

The ``memdep`` pass uses alias analysis to provide high-level dependence
information about memory-using instructions.  This will tell you which store
feeds into a load, for example.  It uses caching and other techniques to be
efficient, and is used by Dead Store Elimination, GVN, and memcpy optimizations.

.. _AliasSetTracker:

Using the ``AliasSetTracker`` class
-----------------------------------

Many transformations need information about alias **sets** that are active in
some scope, rather than information about pairwise aliasing.  The
`AliasSetTracker <https://llvm.org/doxygen/classllvm_1_1AliasSetTracker.html>`__
class is used to efficiently build these Alias Sets from the pairwise alias
analysis information provided by the ``AliasAnalysis`` interface.

First you initialize the AliasSetTracker by using the "``add``" methods to add
information about various potentially aliasing instructions in the scope you are
interested in.  Once all of the alias sets are completed, your pass should
simply iterate through the constructed alias sets, using the ``AliasSetTracker``
``begin()``/``end()`` methods.

The ``AliasSet``\s formed by the ``AliasSetTracker`` are guaranteed to be
disjoint, calculate mod/ref information and volatility for the set, and keep
track of whether or not all of the pointers in the set are Must aliases.  The
AliasSetTracker also makes sure that sets are properly folded due to call
instructions, and can provide a list of pointers in each set.

As an example user of this, the `Loop Invariant Code Motion
<doxygen/structLICM.html>`_ pass uses ``AliasSetTracker``\s to calculate alias
sets for each loop nest.  If an ``AliasSet`` in a loop is not modified, then all
load instructions from that set may be hoisted out of the loop.  If any alias
sets are stored to **and** are must alias sets, then the stores may be sunk
to outside of the loop, promoting the memory location to a register for the
duration of the loop nest.  Both of these transformations only apply if the
pointer argument is loop-invariant.

The AliasSetTracker implementation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The AliasSetTracker class is implemented to be as efficient as possible.  It
uses the union-find algorithm to efficiently merge AliasSets when a pointer is
inserted into the AliasSetTracker that aliases multiple sets.  The primary data
structure is a hash table mapping pointers to the AliasSet they are in.

The AliasSetTracker class must maintain a list of all of the LLVM ``Value*``\s
that are in each AliasSet.  Since the hash table already has entries for each
LLVM ``Value*`` of interest, the AliasesSets thread the linked list through
these hash-table nodes to avoid having to allocate memory unnecessarily, and to
make merging alias sets extremely efficient (the linked list merge is constant
time).

You shouldn't need to understand these details if you are just a client of the
AliasSetTracker, but if you look at the code, hopefully this brief description
will help make sense of why things are designed the way they are.

Using the ``AliasAnalysis`` interface directly
----------------------------------------------

If neither of these utility class are what your pass needs, you should use the
interfaces exposed by the ``AliasAnalysis`` class directly.  Try to use the
higher-level methods when possible (e.g., use mod/ref information instead of the
`alias`_ method directly if possible) to get the best precision and efficiency.

Existing alias analysis implementations and clients
===================================================

If you're going to be working with the LLVM alias analysis infrastructure, you
should know what clients and implementations of alias analysis are available.
In particular, if you are implementing an alias analysis, you should be aware of
the `the clients`_ that are useful for monitoring and evaluating different
implementations.

.. _various alias analysis implementations:

Available ``AliasAnalysis`` implementations
-------------------------------------------

This section lists the various implementations of the ``AliasAnalysis``
interface. All of these :ref:`chain <aliasanalysis-chaining>` to other
alias analysis implementations.

The ``-basic-aa`` pass
^^^^^^^^^^^^^^^^^^^^^^

The ``-basic-aa`` pass is an aggressive local analysis that *knows* many
important facts:

* Distinct globals, stack allocations, and heap allocations can never alias.
* Globals, stack allocations, and heap allocations never alias the null pointer.
* Different fields of a structure do not alias.
* Indexes into arrays with statically differing subscripts cannot alias.
* Many common standard C library functions `never access memory or only read
  memory`_.
* Pointers that obviously point to constant globals "``pointToConstantMemory``".
* Function calls can not modify or references stack allocations if they never
  escape from the function that allocates them (a common case for automatic
  arrays).

The ``-globalsmodref-aa`` pass
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This pass implements a simple context-sensitive mod/ref and alias analysis for
internal global variables that don't "have their address taken".  If a global
does not have its address taken, the pass knows that no pointers alias the
global.  This pass also keeps track of functions that it knows never access
memory or never read memory.  This allows certain optimizations (e.g. GVN) to
eliminate call instructions entirely.

The real power of this pass is that it provides context-sensitive mod/ref
information for call instructions.  This allows the optimizer to know that calls
to a function do not clobber or read the value of the global, allowing loads and
stores to be eliminated.

.. note::

  This pass is somewhat limited in its scope (only support non-address taken
  globals), but is very quick analysis.

The ``-steens-aa`` pass
^^^^^^^^^^^^^^^^^^^^^^^

The ``-steens-aa`` pass implements a variation on the well-known "Steensgaard's
algorithm" for interprocedural alias analysis.  Steensgaard's algorithm is a
unification-based, flow-insensitive, context-insensitive, and field-insensitive
alias analysis that is also very scalable (effectively linear time).

The LLVM ``-steens-aa`` pass implements a "speculatively field-**sensitive**"
version of Steensgaard's algorithm using the Data Structure Analysis framework.
This gives it substantially more precision than the standard algorithm while
maintaining excellent analysis scalability.

.. note::

  ``-steens-aa`` is available in the optional "poolalloc" module. It is not part
  of the LLVM core.

The ``-ds-aa`` pass
^^^^^^^^^^^^^^^^^^^

The ``-ds-aa`` pass implements the full Data Structure Analysis algorithm.  Data
Structure Analysis is a modular unification-based, flow-insensitive,
context-**sensitive**, and speculatively field-**sensitive** alias
analysis that is also quite scalable, usually at ``O(n * log(n))``.

This algorithm is capable of responding to a full variety of alias analysis
queries, and can provide context-sensitive mod/ref information as well.  The
only major facility not implemented so far is support for must-alias
information.

.. note::

  ``-ds-aa`` is available in the optional "poolalloc" module. It is not part of
  the LLVM core.

The ``-scev-aa`` pass
^^^^^^^^^^^^^^^^^^^^^

The ``-scev-aa`` pass implements AliasAnalysis queries by translating them into
ScalarEvolution queries. This gives it a more complete understanding of
``getelementptr`` instructions and loop induction variables than other alias
analyses have.

Alias analysis driven transformations
-------------------------------------

LLVM includes several alias-analysis driven transformations which can be used
with any of the implementations above.

The ``-adce`` pass
^^^^^^^^^^^^^^^^^^

The ``-adce`` pass, which implements Aggressive Dead Code Elimination uses the
``AliasAnalysis`` interface to delete calls to functions that do not have
side-effects and are not used.

The ``-licm`` pass
^^^^^^^^^^^^^^^^^^

The ``-licm`` pass implements various Loop Invariant Code Motion related
transformations.  It uses the ``AliasAnalysis`` interface for several different
transformations:

* It uses mod/ref information to hoist or sink load instructions out of loops if
  there are no instructions in the loop that modifies the memory loaded.

* It uses mod/ref information to hoist function calls out of loops that do not
  write to memory and are loop-invariant.

* It uses alias information to promote memory objects that are loaded and stored
  to in loops to live in a register instead.  It can do this if there are no may
  aliases to the loaded/stored memory location.

The ``-argpromotion`` pass
^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``-argpromotion`` pass promotes by-reference arguments to be passed in
by-value instead.  In particular, if pointer arguments are only loaded from it
passes in the value loaded instead of the address to the function.  This pass
uses alias information to make sure that the value loaded from the argument
pointer is not modified between the entry of the function and any load of the
pointer.

The ``-gvn``, ``-memcpyopt``, and ``-dse`` passes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These passes use AliasAnalysis information to reason about loads and stores.

.. _the clients:

Clients for debugging and evaluation of implementations
-------------------------------------------------------

These passes are useful for evaluating the various alias analysis
implementations.  You can use them with commands like:

.. code-block:: bash

  % opt -ds-aa -aa-eval foo.bc -disable-output -stats

The ``-print-alias-sets`` pass
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``-print-alias-sets`` pass is exposed as part of the ``opt`` tool to print
out the Alias Sets formed by the `AliasSetTracker`_ class.  This is useful if
you're using the ``AliasSetTracker`` class.  To use it, use something like:

.. code-block:: bash

  % opt -ds-aa -print-alias-sets -disable-output

The ``-aa-eval`` pass
^^^^^^^^^^^^^^^^^^^^^

The ``-aa-eval`` pass simply iterates through all pairs of pointers in a
function and asks an alias analysis whether or not the pointers alias.  This
gives an indication of the precision of the alias analysis.  Statistics are
printed indicating the percent of no/may/must aliases found (a more precise
algorithm will have a lower number of may aliases).

Memory Dependence Analysis
==========================

.. note::

  We are currently in the process of migrating things from
  ``MemoryDependenceAnalysis`` to :doc:`MemorySSA`. Please try to use
  that instead.

If you're just looking to be a client of alias analysis information, consider
using the Memory Dependence Analysis interface instead.  MemDep is a lazy,
caching layer on top of alias analysis that is able to answer the question of
what preceding memory operations a given instruction depends on, either at an
intra- or inter-block level.  Because of its laziness and caching policy, using
MemDep can be a significant performance win over accessing alias analysis
directly.
