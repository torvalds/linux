Inlining
========

There are several options that control which calls the analyzer will consider for
inlining. The major one is ``-analyzer-config ipa``:

* ``analyzer-config ipa=none`` - All inlining is disabled. This is the only mode
  available in LLVM 3.1 and earlier and in Xcode 4.3 and earlier.

* ``analyzer-config ipa=basic-inlining`` - Turns on inlining for C functions, C++
   static member functions, and blocks -- essentially, the calls that behave
   like simple C function calls. This is essentially the mode used in
   Xcode 4.4.

* ``analyzer-config ipa=inlining`` - Turns on inlining when we can confidently find
    the function/method body corresponding to the call. (C functions, static
    functions, devirtualized C++ methods, Objective-C class methods, Objective-C
    instance methods when ExprEngine is confident about the dynamic type of the
    instance).

* ``analyzer-config ipa=dynamic`` - Inline instance methods for which the type is
   determined at runtime and we are not 100% sure that our type info is
   correct. For virtual calls, inline the most plausible definition.

* ``analyzer-config ipa=dynamic-bifurcate`` - Same as -analyzer-config ipa=dynamic,
   but the path is split. We inline on one branch and do not inline on the
   other. This mode does not drop the coverage in cases when the parent class
   has code that is only exercised when some of its methods are overridden.

Currently, ``-analyzer-config ipa=dynamic-bifurcate`` is the default mode.

While ``-analyzer-config ipa`` determines in general how aggressively the analyzer
will try to inline functions, several additional options control which types of
functions can inlined, in an all-or-nothing way. These options use the
analyzer's configuration table, so they are all specified as follows:

    ``-analyzer-config OPTION=VALUE``

c++-inlining
------------

This option controls which C++ member functions may be inlined.

    ``-analyzer-config c++-inlining=[none | methods | constructors | destructors]``

Each of these modes implies that all the previous member function kinds will be
inlined as well; it doesn't make sense to inline destructors without inlining
constructors, for example.

The default c++-inlining mode is 'destructors', meaning that all member
functions with visible definitions will be considered for inlining. In some
cases the analyzer may still choose not to inline the function.

Note that under 'constructors', constructors for types with non-trivial
destructors will not be inlined. Additionally, no C++ member functions will be
inlined under -analyzer-config ipa=none or -analyzer-config ipa=basic-inlining,
regardless of the setting of the c++-inlining mode.

c++-template-inlining
^^^^^^^^^^^^^^^^^^^^^

This option controls whether C++ templated functions may be inlined.

    ``-analyzer-config c++-template-inlining=[true | false]``

Currently, template functions are considered for inlining by default.

The motivation behind this option is that very generic code can be a source
of false positives, either by considering paths that the caller considers
impossible (by some unstated precondition), or by inlining some but not all
of a deep implementation of a function.

c++-stdlib-inlining
^^^^^^^^^^^^^^^^^^^

This option controls whether functions from the C++ standard library, including
methods of the container classes in the Standard Template Library, should be
considered for inlining.

    ``-analyzer-config c++-stdlib-inlining=[true | false]``

Currently, C++ standard library functions are considered for inlining by
default.

The standard library functions and the STL in particular are used ubiquitously
enough that our tolerance for false positives is even lower here. A false
positive due to poor modeling of the STL leads to a poor user experience, since
most users would not be comfortable adding assertions to system headers in order
to silence analyzer warnings.

c++-container-inlining
^^^^^^^^^^^^^^^^^^^^^^

This option controls whether constructors and destructors of "container" types
should be considered for inlining.

    ``-analyzer-config c++-container-inlining=[true | false]``

Currently, these constructors and destructors are NOT considered for inlining
by default.

The current implementation of this setting checks whether a type has a member
named 'iterator' or a member named 'begin'; these names are idiomatic in C++,
with the latter specified in the C++11 standard. The analyzer currently does a
fairly poor job of modeling certain data structure invariants of container-like
objects. For example, these three expressions should be equivalent:


.. code-block:: cpp

 std::distance(c.begin(), c.end()) == 0
 c.begin() == c.end()
 c.empty()

Many of these issues are avoided if containers always have unknown, symbolic
state, which is what happens when their constructors are treated as opaque.
In the future, we may decide specific containers are "safe" to model through
inlining, or choose to model them directly using checkers instead.


Basics of Implementation
------------------------

The low-level mechanism of inlining a function is handled in
ExprEngine::inlineCall and ExprEngine::processCallExit.

If the conditions are right for inlining, a CallEnter node is created and added
to the analysis work list. The CallEnter node marks the change to a new
LocationContext representing the called function, and its state includes the
contents of the new stack frame. When the CallEnter node is actually processed,
its single successor will be an edge to the first CFG block in the function.

Exiting an inlined function is a bit more work, fortunately broken up into
reasonable steps:

1. The CoreEngine realizes we're at the end of an inlined call and generates a
   CallExitBegin node.

2. ExprEngine takes over (in processCallExit) and finds the return value of the
   function, if it has one. This is bound to the expression that triggered the
   call. (In the case of calls without origin expressions, such as destructors,
   this step is skipped.)

3. Dead symbols and bindings are cleaned out from the state, including any local
   bindings.

4. A CallExitEnd node is generated, which marks the transition back to the
   caller's LocationContext.

5. Custom post-call checks are processed and the final nodes are pushed back
   onto the work list, so that evaluation of the caller can continue.

Retry Without Inlining
^^^^^^^^^^^^^^^^^^^^^^

In some cases, we would like to retry analysis without inlining a particular
call.

Currently, we use this technique to recover coverage in case we stop
analyzing a path due to exceeding the maximum block count inside an inlined
function.

When this situation is detected, we walk up the path to find the first node
before inlining was started and enqueue it on the WorkList with a special
ReplayWithoutInlining bit added to it (ExprEngine::replayWithoutInlining).  The
path is then re-analyzed from that point without inlining that particular call.

Deciding When to Inline
^^^^^^^^^^^^^^^^^^^^^^^

In general, the analyzer attempts to inline as much as possible, since it
provides a better summary of what actually happens in the program.  There are
some cases, however, where the analyzer chooses not to inline:

- If there is no definition available for the called function or method.  In
  this case, there is no opportunity to inline.

- If the CFG cannot be constructed for a called function, or the liveness
  cannot be computed.  These are prerequisites for analyzing a function body,
  with or without inlining.

- If the LocationContext chain for a given ExplodedNode reaches a maximum cutoff
  depth.  This prevents unbounded analysis due to infinite recursion, but also
  serves as a useful cutoff for performance reasons.

- If the function is variadic.  This is not a hard limitation, but an engineering
  limitation.

  Tracked by: <rdar://problem/12147064> Support inlining of variadic functions

- In C++, constructors are not inlined unless the destructor call will be
  processed by the ExprEngine. Thus, if the CFG was built without nodes for
  implicit destructors, or if the destructors for the given object are not
  represented in the CFG, the constructor will not be inlined. (As an exception,
  constructors for objects with trivial constructors can still be inlined.)
  See "C++ Caveats" below.

- In C++, ExprEngine does not inline custom implementations of operator 'new'
  or operator 'delete', nor does it inline the constructors and destructors
  associated with these. See "C++ Caveats" below.

- Calls resulting in "dynamic dispatch" are specially handled.  See more below.

- The FunctionSummaries map stores additional information about declarations,
  some of which is collected at runtime based on previous analyses.
  We do not inline functions which were not profitable to inline in a different
  context (for example, if the maximum block count was exceeded; see
  "Retry Without Inlining").


Dynamic Calls and Devirtualization
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

"Dynamic" calls are those that are resolved at runtime, such as C++ virtual
method calls and Objective-C message sends. Due to the path-sensitive nature of
the analysis, the analyzer may be able to reason about the dynamic type of the
object whose method is being called and thus "devirtualize" the call.

This path-sensitive devirtualization occurs when the analyzer can determine what
method would actually be called at runtime.  This is possible when the type
information is constrained enough for a simulated C++/Objective-C object that
the analyzer can make such a decision.

DynamicTypeInfo
^^^^^^^^^^^^^^^

As the analyzer analyzes a path, it may accrue information to refine the
knowledge about the type of an object.  This can then be used to make better
decisions about the target method of a call.

Such type information is tracked as DynamicTypeInfo.  This is path-sensitive
data that is stored in ProgramState, which defines a mapping from MemRegions to
an (optional) DynamicTypeInfo.

If no DynamicTypeInfo has been explicitly set for a MemRegion, it will be lazily
inferred from the region's type or associated symbol. Information from symbolic
regions is weaker than from true typed regions.

  EXAMPLE: A C++ object declared "A obj" is known to have the class 'A', but a
           reference "A &ref" may dynamically be a subclass of 'A'.

The DynamicTypePropagation checker gathers and propagates DynamicTypeInfo,
updating it as information is observed along a path that can refine that type
information for a region.

  WARNING: Not all of the existing analyzer code has been retrofitted to use
           DynamicTypeInfo, nor is it universally appropriate. In particular,
           DynamicTypeInfo always applies to a region with all casts stripped
           off, but sometimes the information provided by casts can be useful.


RuntimeDefinition
^^^^^^^^^^^^^^^^^

The basis of devirtualization is CallEvent's getRuntimeDefinition() method,
which returns a RuntimeDefinition object.  When asked to provide a definition,
the CallEvents for dynamic calls will use the DynamicTypeInfo in their
ProgramState to attempt to devirtualize the call.  In the case of no dynamic
dispatch, or perfectly constrained devirtualization, the resulting
RuntimeDefinition contains a Decl corresponding to the definition of the called
function, and RuntimeDefinition::mayHaveOtherDefinitions will return FALSE.

In the case of dynamic dispatch where our information is not perfect, CallEvent
can make a guess, but RuntimeDefinition::mayHaveOtherDefinitions will return
TRUE. The RuntimeDefinition object will then also include a MemRegion
corresponding to the object being called (i.e., the "receiver" in Objective-C
parlance), which ExprEngine uses to decide whether or not the call should be
inlined.

Inlining Dynamic Calls
^^^^^^^^^^^^^^^^^^^^^^

The -analyzer-config ipa option has five different modes: none, basic-inlining,
inlining, dynamic, and dynamic-bifurcate. Under -analyzer-config ipa=dynamic,
all dynamic calls are inlined, whether we are certain or not that this will
actually be the definition used at runtime. Under -analyzer-config ipa=inlining,
only "near-perfect" devirtualized calls are inlined*, and other dynamic calls
are evaluated conservatively (as if no definition were available).

* Currently, no Objective-C messages are not inlined under
  -analyzer-config ipa=inlining, even if we are reasonably confident of the type
  of the receiver. We plan to enable this once we have tested our heuristics
  more thoroughly.

The last option, -analyzer-config ipa=dynamic-bifurcate, behaves similarly to
"dynamic", but performs a conservative invalidation in the general virtual case
in *addition* to inlining. The details of this are discussed below.

As stated above, -analyzer-config ipa=basic-inlining does not inline any C++
member functions or Objective-C method calls, even if they are non-virtual or
can be safely devirtualized.


Bifurcation
^^^^^^^^^^^

ExprEngine::BifurcateCall implements the ``-analyzer-config ipa=dynamic-bifurcate``
mode.

When a call is made on an object with imprecise dynamic type information
(RuntimeDefinition::mayHaveOtherDefinitions() evaluates to TRUE), ExprEngine
bifurcates the path and marks the object's region (retrieved from the
RuntimeDefinition object) with a path-sensitive "mode" in the ProgramState.

Currently, there are 2 modes:

* ``DynamicDispatchModeInlined`` - Models the case where the dynamic type information
   of the receiver (MemoryRegion) is assumed to be perfectly constrained so
   that a given definition of a method is expected to be the code actually
   called. When this mode is set, ExprEngine uses the Decl from
   RuntimeDefinition to inline any dynamically dispatched call sent to this
   receiver because the function definition is considered to be fully resolved.

* ``DynamicDispatchModeConservative`` - Models the case where the dynamic type
   information is assumed to be incorrect, for example, implies that the method
   definition is overridden in a subclass. In such cases, ExprEngine does not
   inline the methods sent to the receiver (MemoryRegion), even if a candidate
   definition is available. This mode is conservative about simulating the
   effects of a call.

Going forward along the symbolic execution path, ExprEngine consults the mode
of the receiver's MemRegion to make decisions on whether the calls should be
inlined or not, which ensures that there is at most one split per region.

At a high level, "bifurcation mode" allows for increased semantic coverage in
cases where the parent method contains code which is only executed when the
class is subclassed. The disadvantages of this mode are a (considerable?)
performance hit and the possibility of false positives on the path where the
conservative mode is used.

Objective-C Message Heuristics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ExprEngine relies on a set of heuristics to partition the set of Objective-C
method calls into those that require bifurcation and those that do not. Below
are the cases when the DynamicTypeInfo of the object is considered precise
(cannot be a subclass):

 - If the object was created with +alloc or +new and initialized with an -init
   method.

 - If the calls are property accesses using dot syntax. This is based on the
   assumption that children rarely override properties, or do so in an
   essentially compatible way.

 - If the class interface is declared inside the main source file. In this case
   it is unlikely that it will be subclassed.

 - If the method is not declared outside of main source file, either by the
   receiver's class or by any superclasses.

C++ Caveats
^^^^^^^^^^^

C++11 [class.cdtor]p4 describes how the vtable of an object is modified as it is
being constructed or destructed; that is, the type of the object depends on
which base constructors have been completed. This is tracked using
DynamicTypeInfo in the DynamicTypePropagation checker.

There are several limitations in the current implementation:

* Temporaries are poorly modeled right now because we're not confident in the
  placement of their destructors in the CFG. We currently won't inline their
  constructors unless the destructor is trivial, and don't process their
  destructors at all, not even to invalidate the region.

* 'new' is poorly modeled due to some nasty CFG/design issues.  This is tracked
  in PR12014.  'delete' is not modeled at all.

* Arrays of objects are modeled very poorly right now.  ExprEngine currently
  only simulates the first constructor and first destructor. Because of this,
  ExprEngine does not inline any constructors or destructors for arrays.


CallEvent
^^^^^^^^^

A CallEvent represents a specific call to a function, method, or other body of
code. It is path-sensitive, containing both the current state (ProgramStateRef)
and stack space (LocationContext), and provides uniform access to the argument
values and return type of a call, no matter how the call is written in the
source or what sort of code body is being invoked.

  NOTE: For those familiar with Cocoa, CallEvent is roughly equivalent to
        NSInvocation.

CallEvent should be used whenever there is logic dealing with function calls
that does not care how the call occurred.

Examples include checking that arguments satisfy preconditions (such as
__attribute__((nonnull))), and attempting to inline a call.

CallEvents are reference-counted objects managed by a CallEventManager. While
there is no inherent issue with persisting them (say, in a ProgramState's GDM),
they are intended for short-lived use, and can be recreated from CFGElements or
non-top-level StackFrameContexts fairly easily.
