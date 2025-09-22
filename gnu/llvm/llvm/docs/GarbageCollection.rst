=====================================
Garbage Collection with LLVM
=====================================

.. contents::
   :local:

Abstract
========

This document covers how to integrate LLVM into a compiler for a language which
supports garbage collection.  **Note that LLVM itself does not provide a
garbage collector.**  You must provide your own.

Quick Start
============

First, you should pick a collector strategy.  LLVM includes a number of built
in ones, but you can also implement a loadable plugin with a custom definition.
Note that the collector strategy is a description of how LLVM should generate
code such that it interacts with your collector and runtime, not a description
of the collector itself.

Next, mark your generated functions as using your chosen collector strategy.
From c++, you can call:

.. code-block:: c++

  F.setGC(<collector description name>);


This will produce IR like the following fragment:

.. code-block:: llvm

  define void @foo() gc "<collector description name>" { ... }


When generating LLVM IR for your functions, you will need to:

* Use ``@llvm.gcread`` and/or ``@llvm.gcwrite`` in place of standard load and
  store instructions.  These intrinsics are used to represent load and store
  barriers.  If you collector does not require such barriers, you can skip
  this step.

* Use the memory allocation routines provided by your garbage collector's
  runtime library.

* If your collector requires them, generate type maps according to your
  runtime's binary interface.  LLVM is not involved in the process.  In
  particular, the LLVM type system is not suitable for conveying such
  information though the compiler.

* Insert any coordination code required for interacting with your collector.
  Many collectors require running application code to periodically check a
  flag and conditionally call a runtime function.  This is often referred to
  as a safepoint poll.

You will need to identify roots (i.e. references to heap objects your collector
needs to know about) in your generated IR, so that LLVM can encode them into
your final stack maps.  Depending on the collector strategy chosen, this is
accomplished by using either the ``@llvm.gcroot`` intrinsics or an
``gc.statepoint`` relocation sequence.

Don't forget to create a root for each intermediate value that is generated when
evaluating an expression.  In ``h(f(), g())``, the result of ``f()`` could
easily be collected if evaluating ``g()`` triggers a collection.

Finally, you need to link your runtime library with the generated program
executable (for a static compiler) or ensure the appropriate symbols are
available for the runtime linker (for a JIT compiler).


Introduction
============

What is Garbage Collection?
---------------------------

Garbage collection is a widely used technique that frees the programmer from
having to know the lifetimes of heap objects, making software easier to produce
and maintain.  Many programming languages rely on garbage collection for
automatic memory management.  There are two primary forms of garbage collection:
conservative and accurate.

Conservative garbage collection often does not require any special support from
either the language or the compiler: it can handle non-type-safe programming
languages (such as C/C++) and does not require any special information from the
compiler.  The `Boehm collector
<https://hboehm.info/gc/>`__ is an example of a
state-of-the-art conservative collector.

Accurate garbage collection requires the ability to identify all pointers in the
program at run-time (which requires that the source-language be type-safe in
most cases).  Identifying pointers at run-time requires compiler support to
locate all places that hold live pointer variables at run-time, including the
:ref:`processor stack and registers <gcroot>`.

Conservative garbage collection is attractive because it does not require any
special compiler support, but it does have problems.  In particular, because the
conservative garbage collector cannot *know* that a particular word in the
machine is a pointer, it cannot move live objects in the heap (preventing the
use of compacting and generational GC algorithms) and it can occasionally suffer
from memory leaks due to integer values that happen to point to objects in the
program.  In addition, some aggressive compiler transformations can break
conservative garbage collectors (though these seem rare in practice).

Accurate garbage collectors do not suffer from any of these problems, but they
can suffer from degraded scalar optimization of the program.  In particular,
because the runtime must be able to identify and update all pointers active in
the program, some optimizations are less effective.  In practice, however, the
locality and performance benefits of using aggressive garbage collection
techniques dominates any low-level losses.

This document describes the mechanisms and interfaces provided by LLVM to
support accurate garbage collection.

Goals and non-goals
-------------------

LLVM's intermediate representation provides :ref:`garbage collection intrinsics
<gc_intrinsics>` that offer support for a broad class of collector models.  For
instance, the intrinsics permit:

* semi-space collectors

* mark-sweep collectors

* generational collectors

* incremental collectors

* concurrent collectors

* cooperative collectors

* reference counting

We hope that the support built into the LLVM IR is sufficient to support a
broad class of garbage collected languages including Scheme, ML, Java, C#,
Perl, Python, Lua, Ruby, other scripting languages, and more.

Note that LLVM **does not itself provide a garbage collector** --- this should
be part of your language's runtime library.  LLVM provides a framework for
describing the garbage collectors requirements to the compiler.  In particular,
LLVM provides support for generating stack maps at call sites, polling for a
safepoint, and emitting load and store barriers.  You can also extend LLVM -
possibly through a loadable :ref:`code generation plugins <plugin>` - to
generate code and data structures which conforms to the *binary interface*
specified by the *runtime library*.  This is similar to the relationship between
LLVM and DWARF debugging info, for example.  The difference primarily lies in
the lack of an established standard in the domain of garbage collection --- thus
the need for a flexible extension mechanism.

The aspects of the binary interface with which LLVM's GC support is
concerned are:

* Creation of GC safepoints within code where collection is allowed to execute
  safely.

* Computation of the stack map.  For each safe point in the code, object
  references within the stack frame must be identified so that the collector may
  traverse and perhaps update them.

* Write barriers when storing object references to the heap.  These are commonly
  used to optimize incremental scans in generational collectors.

* Emission of read barriers when loading object references.  These are useful
  for interoperating with concurrent collectors.

There are additional areas that LLVM does not directly address:

* Registration of global roots with the runtime.

* Registration of stack map entries with the runtime.

* The functions used by the program to allocate memory, trigger a collection,
  etc.

* Computation or compilation of type maps, or registration of them with the
  runtime.  These are used to crawl the heap for object references.

In general, LLVM's support for GC does not include features which can be
adequately addressed with other features of the IR and does not specify a
particular binary interface.  On the plus side, this means that you should be
able to integrate LLVM with an existing runtime.  On the other hand, it can
have the effect of leaving a lot of work for the developer of a novel
language.  We try to mitigate this by providing built in collector strategy
descriptions that can work with many common collector designs and easy
extension points.  If you don't already have a specific binary interface
you need to support, we recommend trying to use one of these built in collector
strategies.

.. _gc_intrinsics:

LLVM IR Features
================

This section describes the garbage collection facilities provided by the
:doc:`LLVM intermediate representation <LangRef>`.  The exact behavior of these
IR features is specified by the selected :ref:`GC strategy description
<plugin>`.

Specifying GC code generation: ``gc "..."``
-------------------------------------------

.. code-block:: text

  define <returntype> @name(...) gc "name" { ... }

The ``gc`` function attribute is used to specify the desired GC strategy to the
compiler.  Its programmatic equivalent is the ``setGC`` method of ``Function``.

Setting ``gc "name"`` on a function triggers a search for a matching subclass
of GCStrategy.  Some collector strategies are built in.  You can add others
using either the loadable plugin mechanism, or by patching your copy of LLVM.
It is the selected GC strategy which defines the exact nature of the code
generated to support GC.  If none is found, the compiler will raise an error.

Specifying the GC style on a per-function basis allows LLVM to link together
programs that use different garbage collection algorithms (or none at all).

.. _gcroot:

Identifying GC roots on the stack
----------------------------------

LLVM currently supports two different mechanisms for describing references in
compiled code at safepoints.  ``llvm.gcroot`` is the older mechanism;
``gc.statepoint`` has been added more recently.  At the moment, you can choose
either implementation (on a per :ref:`GC strategy <plugin>` basis).  Longer
term, we will probably either migrate away from ``llvm.gcroot`` entirely, or
substantially merge their implementations. Note that most new development
work is focused on ``gc.statepoint``.

Using ``gc.statepoint``
^^^^^^^^^^^^^^^^^^^^^^^^
:doc:`This page <Statepoints>` contains detailed documentation for
``gc.statepoint``.

Using ``llvm.gcwrite``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: llvm

  void @llvm.gcroot(i8** %ptrloc, i8* %metadata)

The ``llvm.gcroot`` intrinsic is used to inform LLVM that a stack variable
references an object on the heap and is to be tracked for garbage collection.
The exact impact on generated code is specified by the Function's selected
:ref:`GC strategy <plugin>`.  All calls to ``llvm.gcroot`` **must** reside
inside the first basic block.

The first argument **must** be a value referring to an alloca instruction or a
bitcast of an alloca.  The second contains a pointer to metadata that should be
associated with the pointer, and **must** be a constant or global value
address.  If your target collector uses tags, use a null pointer for metadata.

A compiler which performs manual SSA construction **must** ensure that SSA
values representing GC references are stored in to the alloca passed to the
respective ``gcroot`` before every call site and reloaded after every call.
A compiler which uses mem2reg to raise imperative code using ``alloca`` into
SSA form need only add a call to ``@llvm.gcroot`` for those variables which
are pointers into the GC heap.

It is also important to mark intermediate values with ``llvm.gcroot``.  For
example, consider ``h(f(), g())``.  Beware leaking the result of ``f()`` in the
case that ``g()`` triggers a collection.  Note, that stack variables must be
initialized and marked with ``llvm.gcroot`` in function's prologue.

The ``%metadata`` argument can be used to avoid requiring heap objects to have
'isa' pointers or tag bits. [Appel89_, Goldberg91_, Tolmach94_] If specified,
its value will be tracked along with the location of the pointer in the stack
frame.

Consider the following fragment of Java code:

.. code-block:: java

   {
     Object X;   // A null-initialized reference to an object
     ...
   }

This block (which may be located in the middle of a function or in a loop nest),
could be compiled to this LLVM code:

.. code-block:: llvm

  Entry:
     ;; In the entry block for the function, allocate the
     ;; stack space for X, which is an LLVM pointer.
     %X = alloca %Object*

     ;; Tell LLVM that the stack space is a stack root.
     ;; Java has type-tags on objects, so we pass null as metadata.
     %tmp = bitcast %Object** %X to i8**
     call void @llvm.gcroot(i8** %tmp, i8* null)
     ...

     ;; "CodeBlock" is the block corresponding to the start
     ;;  of the scope above.
  CodeBlock:
     ;; Java null-initializes pointers.
     store %Object* null, %Object** %X

     ...

     ;; As the pointer goes out of scope, store a null value into
     ;; it, to indicate that the value is no longer live.
     store %Object* null, %Object** %X
     ...

Reading and writing references in the heap
------------------------------------------

Some collectors need to be informed when the mutator (the program that needs
garbage collection) either reads a pointer from or writes a pointer to a field
of a heap object.  The code fragments inserted at these points are called *read
barriers* and *write barriers*, respectively.  The amount of code that needs to
be executed is usually quite small and not on the critical path of any
computation, so the overall performance impact of the barrier is tolerable.

Barriers often require access to the *object pointer* rather than the *derived
pointer* (which is a pointer to the field within the object).  Accordingly,
these intrinsics take both pointers as separate arguments for completeness.  In
this snippet, ``%object`` is the object pointer, and ``%derived`` is the derived
pointer:

.. code-block:: llvm

  ;; An array type.
  %class.Array = type { %class.Object, i32, [0 x %class.Object*] }
  ...

  ;; Load the object pointer from a gcroot.
  %object = load %class.Array** %object_addr

  ;; Compute the derived pointer.
  %derived = getelementptr %object, i32 0, i32 2, i32 %n

LLVM does not enforce this relationship between the object and derived pointer
(although a particular :ref:`collector strategy <plugin>` might).  However, it
would be an unusual collector that violated it.

The use of these intrinsics is naturally optional if the target GC does not
require the corresponding barrier.  The GC strategy used with such a collector
should replace the intrinsic calls with the corresponding ``load`` or
``store`` instruction if they are used.

One known deficiency with the current design is that the barrier intrinsics do
not include the size or alignment of the underlying operation performed.  It is
currently assumed that the operation is of pointer size and the alignment is
assumed to be the target machine's default alignment.

Write barrier: ``llvm.gcwrite``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: llvm

  void @llvm.gcwrite(i8* %value, i8* %object, i8** %derived)

For write barriers, LLVM provides the ``llvm.gcwrite`` intrinsic function.  It
has exactly the same semantics as a non-volatile ``store`` to the derived
pointer (the third argument).  The exact code generated is specified by the
Function's selected :ref:`GC strategy <plugin>`.

Many important algorithms require write barriers, including generational and
concurrent collectors.  Additionally, write barriers could be used to implement
reference counting.

Read barrier: ``llvm.gcread``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: llvm

  i8* @llvm.gcread(i8* %object, i8** %derived)

For read barriers, LLVM provides the ``llvm.gcread`` intrinsic function.  It has
exactly the same semantics as a non-volatile ``load`` from the derived pointer
(the second argument).  The exact code generated is specified by the Function's
selected :ref:`GC strategy <plugin>`.

Read barriers are needed by fewer algorithms than write barriers, and may have a
greater performance impact since pointer reads are more frequent than writes.

.. _plugin:

.. _builtin-gc-strategies:

Built In GC Strategies
======================

LLVM includes built in support for several varieties of garbage collectors.

The Shadow Stack GC
----------------------

To use this collector strategy, mark your functions with:

.. code-block:: c++

  F.setGC("shadow-stack");

Unlike many GC algorithms which rely on a cooperative code generator to compile
stack maps, this algorithm carefully maintains a linked list of stack roots
[:ref:`Henderson2002 <henderson02>`].  This so-called "shadow stack" mirrors the
machine stack.  Maintaining this data structure is slower than using a stack map
compiled into the executable as constant data, but has a significant portability
advantage because it requires no special support from the target code generator,
and does not require tricky platform-specific code to crawl the machine stack.

The tradeoff for this simplicity and portability is:

* High overhead per function call.

* Not thread-safe.

Still, it's an easy way to get started.  After your compiler and runtime are up
and running, writing a :ref:`plugin <plugin>` will allow you to take advantage
of :ref:`more advanced GC features <collector-algos>` of LLVM in order to
improve performance.


The shadow stack doesn't imply a memory allocation algorithm.  A semispace
collector or building atop ``malloc`` are great places to start, and can be
implemented with very little code.

When it comes time to collect, however, your runtime needs to traverse the stack
roots, and for this it needs to integrate with the shadow stack.  Luckily, doing
so is very simple. (This code is heavily commented to help you understand the
data structure, but there are only 20 lines of meaningful code.)

.. code-block:: c++

  /// The map for a single function's stack frame.  One of these is
  ///        compiled as constant data into the executable for each function.
  ///
  /// Storage of metadata values is elided if the %metadata parameter to
  /// @llvm.gcroot is null.
  struct FrameMap {
    int32_t NumRoots;    //< Number of roots in stack frame.
    int32_t NumMeta;     //< Number of metadata entries.  May be < NumRoots.
    const void *Meta[0]; //< Metadata for each root.
  };

  /// A link in the dynamic shadow stack.  One of these is embedded in
  ///        the stack frame of each function on the call stack.
  struct StackEntry {
    StackEntry *Next;    //< Link to next stack entry (the caller's).
    const FrameMap *Map; //< Pointer to constant FrameMap.
    void *Roots[0];      //< Stack roots (in-place array).
  };

  /// The head of the singly-linked list of StackEntries.  Functions push
  ///        and pop onto this in their prologue and epilogue.
  ///
  /// Since there is only a global list, this technique is not threadsafe.
  StackEntry *llvm_gc_root_chain;

  /// Calls Visitor(root, meta) for each GC root on the stack.
  ///        root and meta are exactly the values passed to
  ///        @llvm.gcroot.
  ///
  /// Visitor could be a function to recursively mark live objects.  Or it
  /// might copy them to another heap or generation.
  ///
  /// @param Visitor A function to invoke for every GC root on the stack.
  void visitGCRoots(void (*Visitor)(void **Root, const void *Meta)) {
    for (StackEntry *R = llvm_gc_root_chain; R; R = R->Next) {
      unsigned i = 0;

      // For roots [0, NumMeta), the metadata pointer is in the FrameMap.
      for (unsigned e = R->Map->NumMeta; i != e; ++i)
        Visitor(&R->Roots[i], R->Map->Meta[i]);

      // For roots [NumMeta, NumRoots), the metadata pointer is null.
      for (unsigned e = R->Map->NumRoots; i != e; ++i)
        Visitor(&R->Roots[i], NULL);
    }
  }


The 'Erlang' and 'Ocaml' GCs
-----------------------------

LLVM ships with two example collectors which leverage the ``gcroot``
mechanisms.  To our knowledge, these are not actually used by any language
runtime, but they do provide a reasonable starting point for someone interested
in writing an ``gcroot`` compatible GC plugin.  In particular, these are the
only in tree examples of how to produce a custom binary stack map format using
a ``gcroot`` strategy.

As there names imply, the binary format produced is intended to model that
used by the Erlang and OCaml compilers respectively.

.. _statepoint_example_gc:

The Statepoint Example GC
-------------------------

.. code-block:: c++

  F.setGC("statepoint-example");

This GC provides an example of how one might use the infrastructure provided
by ``gc.statepoint``. This example GC is compatible with the
:ref:`PlaceSafepoints` and :ref:`RewriteStatepointsForGC` utility passes
which simplify ``gc.statepoint`` sequence insertion. If you need to build a
custom GC strategy around the ``gc.statepoints`` mechanisms, it is recommended
that you use this one as a starting point.

This GC strategy does not support read or write barriers.  As a result, these
intrinsics are lowered to normal loads and stores.

The stack map format generated by this GC strategy can be found in the
:ref:`stackmap-section` using a format documented :ref:`here
<statepoint-stackmap-format>`. This format is intended to be the standard
format supported by LLVM going forward.

The CoreCLR GC
-------------------------

.. code-block:: c++

  F.setGC("coreclr");

This GC leverages the ``gc.statepoint`` mechanism to support the
`CoreCLR <https://github.com/dotnet/coreclr>`__ runtime.

Support for this GC strategy is a work in progress. This strategy will
differ from
:ref:`statepoint-example GC<statepoint_example_gc>` strategy in
certain aspects like:

* Base-pointers of interior pointers are not explicitly
  tracked and reported.

* A different format is used for encoding stack maps.

* Safe-point polls are only needed before loop-back edges
  and before tail-calls (not needed at function-entry).

Custom GC Strategies
====================

If none of the built in GC strategy descriptions met your needs above, you will
need to define a custom GCStrategy and possibly, a custom LLVM pass to perform
lowering.  Your best example of where to start defining a custom GCStrategy
would be to look at one of the built in strategies.

You may be able to structure this additional code as a loadable plugin library.
Loadable plugins are sufficient if all you need is to enable a different
combination of built in functionality, but if you need to provide a custom
lowering pass, you will need to build a patched version of LLVM.  If you think
you need a patched build, please ask for advice on llvm-dev.  There may be an
easy way we can extend the support to make it work for your use case without
requiring a custom build.

Collector Requirements
----------------------

You should be able to leverage any existing collector library that includes the following elements:

#. A memory allocator which exposes an allocation function your compiled
   code can call.

#. A binary format for the stack map.  A stack map describes the location
   of references at a safepoint and is used by precise collectors to identify
   references within a stack frame on the machine stack. Note that collectors
   which conservatively scan the stack don't require such a structure.

#. A stack crawler to discover functions on the call stack, and enumerate the
   references listed in the stack map for each call site.

#. A mechanism for identifying references in global locations (e.g. global
   variables).

#. If you collector requires them, an LLVM IR implementation of your collectors
   load and store barriers.  Note that since many collectors don't require
   barriers at all, LLVM defaults to lowering such barriers to normal loads
   and stores unless you arrange otherwise.


Implementing a collector plugin
-------------------------------

User code specifies which GC code generation to use with the ``gc`` function
attribute or, equivalently, with the ``setGC`` method of ``Function``.

To implement a GC plugin, it is necessary to subclass ``llvm::GCStrategy``,
which can be accomplished in a few lines of boilerplate code.  LLVM's
infrastructure provides access to several important algorithms.  For an
uncontroversial collector, all that remains may be to compile LLVM's computed
stack map to assembly code (using the binary representation expected by the
runtime library).  This can be accomplished in about 100 lines of code.

This is not the appropriate place to implement a garbage collected heap or a
garbage collector itself.  That code should exist in the language's runtime
library.  The compiler plugin is responsible for generating code which conforms
to the binary interface defined by library, most essentially the :ref:`stack map
<stack-map>`.

To subclass ``llvm::GCStrategy`` and register it with the compiler:

.. code-block:: c++

  // lib/MyGC/MyGC.cpp - Example LLVM GC plugin

  #include "llvm/CodeGen/GCStrategy.h"
  #include "llvm/CodeGen/GCMetadata.h"
  #include "llvm/Support/Compiler.h"

  using namespace llvm;

  namespace {
    class LLVM_LIBRARY_VISIBILITY MyGC : public GCStrategy {
    public:
      MyGC() {}
    };

    GCRegistry::Add<MyGC>
    X("mygc", "My bespoke garbage collector.");
  }

This boilerplate collector does nothing.  More specifically:

* ``llvm.gcread`` calls are replaced with the corresponding ``load``
  instruction.

* ``llvm.gcwrite`` calls are replaced with the corresponding ``store``
  instruction.

* No safe points are added to the code.

* The stack map is not compiled into the executable.

Using the LLVM makefiles, this code
can be compiled as a plugin using a simple makefile:

.. code-block:: make

  # lib/MyGC/Makefile

  LEVEL := ../..
  LIBRARYNAME = MyGC
  LOADABLE_MODULE = 1

  include $(LEVEL)/Makefile.common

Once the plugin is compiled, code using it may be compiled using ``llc
-load=MyGC.so`` (though MyGC.so may have some other platform-specific
extension):

::

  $ cat sample.ll
  define void @f() gc "mygc" {
  entry:
    ret void
  }
  $ llvm-as < sample.ll | llc -load=MyGC.so

It is also possible to statically link the collector plugin into tools, such as
a language-specific compiler front-end.

.. _collector-algos:

Overview of available features
------------------------------

``GCStrategy`` provides a range of features through which a plugin may do useful
work.  Some of these are callbacks, some are algorithms that can be enabled,
disabled, or customized.  This matrix summarizes the supported (and planned)
features and correlates them with the collection techniques which typically
require them.

.. |v| unicode:: 0x2714
   :trim:

.. |x| unicode:: 0x2718
   :trim:

+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| Algorithm  | Done | Shadow | refcount | mark- | copying | incremental | threaded | concurrent |
|            |      | stack  |          | sweep |         |             |          |            |
+============+======+========+==========+=======+=========+=============+==========+============+
| stack map  | |v|  |        |          | |x|   | |x|     | |x|         | |x|      | |x|        |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| initialize | |v|  | |x|    | |x|      | |x|   | |x|     | |x|         | |x|      | |x|        |
| roots      |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| derived    | NO   |        |          |       |         |             | **N**\*  | **N**\*    |
| pointers   |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| **custom   | |v|  |        |          |       |         |             |          |            |
| lowering** |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *gcroot*   | |v|  | |x|    | |x|      |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *gcwrite*  | |v|  |        | |x|      |       |         | |x|         |          | |x|        |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *gcread*   | |v|  |        |          |       |         |             |          | |x|        |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| **safe     |      |        |          |       |         |             |          |            |
| points**   |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *in        | |v|  |        |          | |x|   | |x|     | |x|         | |x|      | |x|        |
| calls*     |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *before    | |v|  |        |          |       |         |             | |x|      | |x|        |
| calls*     |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *for       | NO   |        |          |       |         |             | **N**    | **N**      |
| loops*     |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *before    | |v|  |        |          |       |         |             | |x|      | |x|        |
| escape*    |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| emit code  | NO   |        |          |       |         |             | **N**    | **N**      |
| at safe    |      |        |          |       |         |             |          |            |
| points     |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| **output** |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *assembly* | |v|  |        |          | |x|   | |x|     | |x|         | |x|      | |x|        |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *JIT*      | NO   |        |          | **?** | **?**   | **?**       | **?**    | **?**      |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| *obj*      | NO   |        |          | **?** | **?**   | **?**       | **?**    | **?**      |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| live       | NO   |        |          | **?** | **?**   | **?**       | **?**    | **?**      |
| analysis   |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| register   | NO   |        |          | **?** | **?**   | **?**       | **?**    | **?**      |
| map        |      |        |          |       |         |             |          |            |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| \* Derived pointers only pose a hazard to copying collections.                                |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+
| **?** denotes a feature which could be utilized if available.                                 |
+------------+------+--------+----------+-------+---------+-------------+----------+------------+

To be clear, the collection techniques above are defined as:

Shadow Stack
  The mutator carefully maintains a linked list of stack roots.

Reference Counting
  The mutator maintains a reference count for each object and frees an object
  when its count falls to zero.

Mark-Sweep
  When the heap is exhausted, the collector marks reachable objects starting
  from the roots, then deallocates unreachable objects in a sweep phase.

Copying
  As reachability analysis proceeds, the collector copies objects from one heap
  area to another, compacting them in the process.  Copying collectors enable
  highly efficient "bump pointer" allocation and can improve locality of
  reference.

Incremental
  (Including generational collectors.) Incremental collectors generally have all
  the properties of a copying collector (regardless of whether the mature heap
  is compacting), but bring the added complexity of requiring write barriers.

Threaded
  Denotes a multithreaded mutator; the collector must still stop the mutator
  ("stop the world") before beginning reachability analysis.  Stopping a
  multithreaded mutator is a complicated problem.  It generally requires highly
  platform-specific code in the runtime, and the production of carefully
  designed machine code at safe points.

Concurrent
  In this technique, the mutator and the collector run concurrently, with the
  goal of eliminating pause times.  In a *cooperative* collector, the mutator
  further aids with collection should a pause occur, allowing collection to take
  advantage of multiprocessor hosts.  The "stop the world" problem of threaded
  collectors is generally still present to a limited extent.  Sophisticated
  marking algorithms are necessary.  Read barriers may be necessary.

As the matrix indicates, LLVM's garbage collection infrastructure is already
suitable for a wide variety of collectors, but does not currently extend to
multithreaded programs.  This will be added in the future as there is
interest.

.. _stack-map:

Computing stack maps
--------------------

LLVM automatically computes a stack map.  One of the most important features
of a ``GCStrategy`` is to compile this information into the executable in
the binary representation expected by the runtime library.

The stack map consists of the location and identity of each GC root in the
each function in the module.  For each root:

* ``RootNum``: The index of the root.

* ``StackOffset``: The offset of the object relative to the frame pointer.

* ``RootMetadata``: The value passed as the ``%metadata`` parameter to the
  ``@llvm.gcroot`` intrinsic.

Also, for the function as a whole:

* ``getFrameSize()``: The overall size of the function's initial stack frame,
   not accounting for any dynamic allocation.

* ``roots_size()``: The count of roots in the function.

To access the stack map, use ``GCFunctionMetadata::roots_begin()`` and
-``end()`` from the :ref:`GCMetadataPrinter <assembly>`:

.. code-block:: c++

  for (iterator I = begin(), E = end(); I != E; ++I) {
    GCFunctionInfo *FI = *I;
    unsigned FrameSize = FI->getFrameSize();
    size_t RootCount = FI->roots_size();

    for (GCFunctionInfo::roots_iterator RI = FI->roots_begin(),
                                        RE = FI->roots_end();
                                        RI != RE; ++RI) {
      int RootNum = RI->Num;
      int RootStackOffset = RI->StackOffset;
      Constant *RootMetadata = RI->Metadata;
    }
  }

If the ``llvm.gcroot`` intrinsic is eliminated before code generation by a
custom lowering pass, LLVM will compute an empty stack map.  This may be useful
for collector plugins which implement reference counting or a shadow stack.

.. _init-roots:

Initializing roots to null
---------------------------

It is recommended that frontends initialize roots explicitly to avoid
potentially confusing the optimizer.  This prevents the GC from visiting
uninitialized pointers, which will almost certainly cause it to crash.

As a fallback, LLVM will automatically initialize each root to ``null``
upon entry to the function.  Support for this mode in code generation is
largely a legacy detail to keep old collector implementations working.

Custom lowering of intrinsics
------------------------------

For GCs which use barriers or unusual treatment of stack roots, the
implementor is responsibly for providing a custom pass to lower the
intrinsics with the desired semantics.  If you have opted in to custom
lowering of a particular intrinsic your pass **must** eliminate all
instances of the corresponding intrinsic in functions which opt in to
your GC.  The best example of such a pass is the ShadowStackGC and it's
ShadowStackGCLowering pass.

There is currently no way to register such a custom lowering pass
without building a custom copy of LLVM.

.. _safe-points:

Generating safe points
-----------------------

LLVM provides support for associating stackmaps with the return address of
a call.  Any loop or return safepoints required by a given collector design
can be modeled via calls to runtime routines, or potentially patchable call
sequences.  Using gcroot, all call instructions are inferred to be possible
safepoints and will thus have an associated stackmap.

.. _assembly:

Emitting assembly code: ``GCMetadataPrinter``
---------------------------------------------

LLVM allows a plugin to print arbitrary assembly code before and after the rest
of a module's assembly code.  At the end of the module, the GC can compile the
LLVM stack map into assembly code. (At the beginning, this information is not
yet computed.)

Since AsmWriter and CodeGen are separate components of LLVM, a separate abstract
base class and registry is provided for printing assembly code, the
``GCMetadaPrinter`` and ``GCMetadataPrinterRegistry``.  The AsmWriter will look
for such a subclass if the ``GCStrategy`` sets ``UsesMetadata``:

.. code-block:: c++

  MyGC::MyGC() {
    UsesMetadata = true;
  }

This separation allows JIT-only clients to be smaller.

Note that LLVM does not currently have analogous APIs to support code generation
in the JIT, nor using the object writers.

.. code-block:: c++

  // lib/MyGC/MyGCPrinter.cpp - Example LLVM GC printer

  #include "llvm/CodeGen/GCMetadataPrinter.h"
  #include "llvm/Support/Compiler.h"

  using namespace llvm;

  namespace {
    class LLVM_LIBRARY_VISIBILITY MyGCPrinter : public GCMetadataPrinter {
    public:
      virtual void beginAssembly(AsmPrinter &AP);

      virtual void finishAssembly(AsmPrinter &AP);
    };

    GCMetadataPrinterRegistry::Add<MyGCPrinter>
    X("mygc", "My bespoke garbage collector.");
  }

The collector should use ``AsmPrinter`` to print portable assembly code.  The
collector itself contains the stack map for the entire module, and may access
the ``GCFunctionInfo`` using its own ``begin()`` and ``end()`` methods.  Here's
a realistic example:

.. code-block:: c++

  #include "llvm/CodeGen/AsmPrinter.h"
  #include "llvm/IR/Function.h"
  #include "llvm/IR/DataLayout.h"
  #include "llvm/Target/TargetAsmInfo.h"
  #include "llvm/Target/TargetMachine.h"

  void MyGCPrinter::beginAssembly(AsmPrinter &AP) {
    // Nothing to do.
  }

  void MyGCPrinter::finishAssembly(AsmPrinter &AP) {
    MCStreamer &OS = AP.OutStreamer;
    unsigned IntPtrSize = AP.getPointerSize();

    // Put this in the data section.
    OS.switchSection(AP.getObjFileLowering().getDataSection());

    // For each function...
    for (iterator FI = begin(), FE = end(); FI != FE; ++FI) {
      GCFunctionInfo &MD = **FI;

      // A compact GC layout. Emit this data structure:
      //
      // struct {
      //   int32_t PointCount;
      //   void *SafePointAddress[PointCount];
      //   int32_t StackFrameSize; // in words
      //   int32_t StackArity;
      //   int32_t LiveCount;
      //   int32_t LiveOffsets[LiveCount];
      // } __gcmap_<FUNCTIONNAME>;

      // Align to address width.
      AP.emitAlignment(IntPtrSize == 4 ? 2 : 3);

      // Emit PointCount.
      OS.AddComment("safe point count");
      AP.emitInt32(MD.size());

      // And each safe point...
      for (GCFunctionInfo::iterator PI = MD.begin(),
                                    PE = MD.end(); PI != PE; ++PI) {
        // Emit the address of the safe point.
        OS.AddComment("safe point address");
        MCSymbol *Label = PI->Label;
        AP.emitLabelPlusOffset(Label/*Hi*/, 0/*Offset*/, 4/*Size*/);
      }

      // Stack information never change in safe points! Only print info from the
      // first call-site.
      GCFunctionInfo::iterator PI = MD.begin();

      // Emit the stack frame size.
      OS.AddComment("stack frame size (in words)");
      AP.emitInt32(MD.getFrameSize() / IntPtrSize);

      // Emit stack arity, i.e. the number of stacked arguments.
      unsigned RegisteredArgs = IntPtrSize == 4 ? 5 : 6;
      unsigned StackArity = MD.getFunction().arg_size() > RegisteredArgs ?
                            MD.getFunction().arg_size() - RegisteredArgs : 0;
      OS.AddComment("stack arity");
      AP.emitInt32(StackArity);

      // Emit the number of live roots in the function.
      OS.AddComment("live root count");
      AP.emitInt32(MD.live_size(PI));

      // And for each live root...
      for (GCFunctionInfo::live_iterator LI = MD.live_begin(PI),
                                         LE = MD.live_end(PI);
                                         LI != LE; ++LI) {
        // Emit live root's offset within the stack frame.
        OS.AddComment("stack index (offset / wordsize)");
        AP.emitInt32(LI->StackOffset);
      }
    }
  }

References
==========

.. _appel89:

[Appel89] Runtime Tags Aren't Necessary. Andrew W. Appel. Lisp and Symbolic
Computation 19(7):703-705, July 1989.

.. _goldberg91:

[Goldberg91] Tag-free garbage collection for strongly typed programming
languages. Benjamin Goldberg. ACM SIGPLAN PLDI'91.

.. _tolmach94:

[Tolmach94] Tag-free garbage collection using explicit type parameters. Andrew
Tolmach. Proceedings of the 1994 ACM conference on LISP and functional
programming.

.. _henderson02:

[Henderson2002] `Accurate Garbage Collection in an Uncooperative Environment
<http://citeseer.ist.psu.edu/henderson02accurate.html>`__
