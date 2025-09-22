.. FIXME: move to the stylesheet or Sphinx plugin

.. raw:: html

  <style>
    .arc-term { font-style: italic; font-weight: bold; }
    .revision { font-style: italic; }
    .when-revised { font-weight: bold; font-style: normal; }

    /*
     * Automatic numbering is described in this article:
     * https://dev.opera.com/articles/view/automatic-numbering-with-css-counters/
     */
    /*
     * Automatic numbering for the TOC.
     * This is wrong from the semantics point of view, since it is an ordered
     * list, but uses "ul" tag.
     */
    div#contents.contents.local ul {
      counter-reset: toc-section;
      list-style-type: none;
    }
    div#contents.contents.local ul li {
      counter-increment: toc-section;
      background: none; // Remove bullets
    }
    div#contents.contents.local ul li a.reference:before {
      content: counters(toc-section, ".") " ";
    }

    /* Automatic numbering for the body. */
    body {
      counter-reset: section subsection subsubsection;
    }
    .section h2 {
      counter-reset: subsection subsubsection;
      counter-increment: section;
    }
    .section h2 a.toc-backref:before {
      content: counter(section) " ";
    }
    .section h3 {
      counter-reset: subsubsection;
      counter-increment: subsection;
    }
    .section h3 a.toc-backref:before {
      content: counter(section) "." counter(subsection) " ";
    }
    .section h4 {
      counter-increment: subsubsection;
    }
    .section h4 a.toc-backref:before {
      content: counter(section) "." counter(subsection) "." counter(subsubsection) " ";
    }
  </style>

.. role:: arc-term
.. role:: revision
.. role:: when-revised

==============================================
Objective-C Automatic Reference Counting (ARC)
==============================================

.. contents::
   :local:

.. _arc.meta:

About this document
===================

.. _arc.meta.purpose:

Purpose
-------

The first and primary purpose of this document is to serve as a complete
technical specification of Automatic Reference Counting.  Given a core
Objective-C compiler and runtime, it should be possible to write a compiler and
runtime which implements these new semantics.

The secondary purpose is to act as a rationale for why ARC was designed in this
way.  This should remain tightly focused on the technical design and should not
stray into marketing speculation.

.. _arc.meta.background:

Background
----------

This document assumes a basic familiarity with C.

:arc-term:`Blocks` are a C language extension for creating anonymous functions.
Users interact with and transfer block objects using :arc-term:`block
pointers`, which are represented like a normal pointer.  A block may capture
values from local variables; when this occurs, memory must be dynamically
allocated.  The initial allocation is done on the stack, but the runtime
provides a ``Block_copy`` function which, given a block pointer, either copies
the underlying block object to the heap, setting its reference count to 1 and
returning the new block pointer, or (if the block object is already on the
heap) increases its reference count by 1.  The paired function is
``Block_release``, which decreases the reference count by 1 and destroys the
object if the count reaches zero and is on the heap.

Objective-C is a set of language extensions, significant enough to be
considered a different language.  It is a strict superset of C.  The extensions
can also be imposed on C++, producing a language called Objective-C++.  The
primary feature is a single-inheritance object system; we briefly describe the
modern dialect.

Objective-C defines a new type kind, collectively called the :arc-term:`object
pointer types`.  This kind has two notable builtin members, ``id`` and
``Class``; ``id`` is the final supertype of all object pointers.  The validity
of conversions between object pointer types is not checked at runtime.  Users
may define :arc-term:`classes`; each class is a type, and the pointer to that
type is an object pointer type.  A class may have a superclass; its pointer
type is a subtype of its superclass's pointer type.  A class has a set of
:arc-term:`ivars`, fields which appear on all instances of that class.  For
every class *T* there's an associated metaclass; it has no fields, its
superclass is the metaclass of *T*'s superclass, and its metaclass is a global
class.  Every class has a global object whose class is the class's metaclass;
metaclasses have no associated type, so pointers to this object have type
``Class``.

A class declaration (``@interface``) declares a set of :arc-term:`methods`.  A
method has a return type, a list of argument types, and a :arc-term:`selector`:
a name like ``foo:bar:baz:``, where the number of colons corresponds to the
number of formal arguments.  A method may be an instance method, in which case
it can be invoked on objects of the class, or a class method, in which case it
can be invoked on objects of the metaclass.  A method may be invoked by
providing an object (called the :arc-term:`receiver`) and a list of formal
arguments interspersed with the selector, like so:

.. code-block:: objc

  [receiver foo: fooArg bar: barArg baz: bazArg]

This looks in the dynamic class of the receiver for a method with this name,
then in that class's superclass, etc., until it finds something it can execute.
The receiver "expression" may also be the name of a class, in which case the
actual receiver is the class object for that class, or (within method
definitions) it may be ``super``, in which case the lookup algorithm starts
with the static superclass instead of the dynamic class.  The actual methods
dynamically found in a class are not those declared in the ``@interface``, but
those defined in a separate ``@implementation`` declaration; however, when
compiling a call, typechecking is done based on the methods declared in the
``@interface``.

Method declarations may also be grouped into :arc-term:`protocols`, which are not
inherently associated with any class, but which classes may claim to follow.
Object pointer types may be qualified with additional protocols that the object
is known to support.

:arc-term:`Class extensions` are collections of ivars and methods, designed to
allow a class's ``@interface`` to be split across multiple files; however,
there is still a primary implementation file which must see the
``@interface``\ s of all class extensions.  :arc-term:`Categories` allow
methods (but not ivars) to be declared *post hoc* on an arbitrary class; the
methods in the category's ``@implementation`` will be dynamically added to that
class's method tables which the category is loaded at runtime, replacing those
methods in case of a collision.

In the standard environment, objects are allocated on the heap, and their
lifetime is manually managed using a reference count.  This is done using two
instance methods which all classes are expected to implement: ``retain``
increases the object's reference count by 1, whereas ``release`` decreases it
by 1 and calls the instance method ``dealloc`` if the count reaches 0.  To
simplify certain operations, there is also an :arc-term:`autorelease pool`, a
thread-local list of objects to call ``release`` on later; an object can be
added to this pool by calling ``autorelease`` on it.

Block pointers may be converted to type ``id``; block objects are laid out in a
way that makes them compatible with Objective-C objects.  There is a builtin
class that all block objects are considered to be objects of; this class
implements ``retain`` by adjusting the reference count, not by calling
``Block_copy``.

.. _arc.meta.evolution:

Evolution
---------

ARC is under continual evolution, and this document must be updated as the
language progresses.

If a change increases the expressiveness of the language, for example by
lifting a restriction or by adding new syntax, the change will be annotated
with a revision marker, like so:

  ARC applies to Objective-C pointer types, block pointer types, and
  :when-revised:`[beginning Apple 8.0, LLVM 3.8]` :revision:`BPTRs declared
  within` ``extern "BCPL"`` blocks.

For now, it is sensible to version this document by the releases of its sole
implementation (and its host project), clang.  "LLVM X.Y" refers to an
open-source release of clang from the LLVM project.  "Apple X.Y" refers to an
Apple-provided release of the Apple LLVM Compiler.  Other organizations that
prepare their own, separately-versioned clang releases and wish to maintain
similar information in this document should send requests to cfe-dev.

If a change decreases the expressiveness of the language, for example by
imposing a new restriction, this should be taken as an oversight in the
original specification and something to be avoided in all versions.  Such
changes are generally to be avoided.

.. _arc.general:

General
=======

Automatic Reference Counting implements automatic memory management for
Objective-C objects and blocks, freeing the programmer from the need to
explicitly insert retains and releases.  It does not provide a cycle collector;
users must explicitly manage the lifetime of their objects, breaking cycles
manually or with weak or unsafe references.

ARC may be explicitly enabled with the compiler flag ``-fobjc-arc``.  It may
also be explicitly disabled with the compiler flag ``-fno-objc-arc``.  The last
of these two flags appearing on the compile line "wins".

If ARC is enabled, ``__has_feature(objc_arc)`` will expand to 1 in the
preprocessor.  For more information about ``__has_feature``, see the
:ref:`language extensions <langext-__has_feature-__has_extension>` document.

.. _arc.objects:

Retainable object pointers
==========================

This section describes retainable object pointers, their basic operations, and
the restrictions imposed on their use under ARC.  Note in particular that it
covers the rules for pointer *values* (patterns of bits indicating the location
of a pointed-to object), not pointer *objects* (locations in memory which store
pointer values).  The rules for objects are covered in the next section.

A :arc-term:`retainable object pointer` (or "retainable pointer") is a value of
a :arc-term:`retainable object pointer type` ("retainable type").  There are
three kinds of retainable object pointer types:

* block pointers (formed by applying the caret (``^``) declarator sigil to a
  function type)
* Objective-C object pointers (``id``, ``Class``, ``NSFoo*``, etc.)
* typedefs marked with ``__attribute__((NSObject))``

Other pointer types, such as ``int*`` and ``CFStringRef``, are not subject to
ARC's semantics and restrictions.

.. admonition:: Rationale

  We are not at liberty to require all code to be recompiled with ARC;
  therefore, ARC must interoperate with Objective-C code which manages retains
  and releases manually.  In general, there are three requirements in order for
  a compiler-supported reference-count system to provide reliable
  interoperation:

  * The type system must reliably identify which objects are to be managed.  An
    ``int*`` might be a pointer to a ``malloc``'ed array, or it might be an
    interior pointer to such an array, or it might point to some field or local
    variable.  In contrast, values of the retainable object pointer types are
    never interior.

  * The type system must reliably indicate how to manage objects of a type.
    This usually means that the type must imply a procedure for incrementing
    and decrementing retain counts.  Supporting single-ownership objects
    requires a lot more explicit mediation in the language.

  * There must be reliable conventions for whether and when "ownership" is
    passed between caller and callee, for both arguments and return values.
    Objective-C methods follow such a convention very reliably, at least for
    system libraries on macOS, and functions always pass objects at +0.  The
    C-based APIs for Core Foundation objects, on the other hand, have much more
    varied transfer semantics.

The use of ``__attribute__((NSObject))`` typedefs is not recommended.  If it's
absolutely necessary to use this attribute, be very explicit about using the
typedef, and do not assume that it will be preserved by language features like
``__typeof`` and C++ template argument substitution.

.. admonition:: Rationale

  Any compiler operation which incidentally strips type "sugar" from a type
  will yield a type without the attribute, which may result in unexpected
  behavior.

.. _arc.objects.retains:

Retain count semantics
----------------------

A retainable object pointer is either a :arc-term:`null pointer` or a pointer
to a valid object.  Furthermore, if it has block pointer type and is not
``null`` then it must actually be a pointer to a block object, and if it has
``Class`` type (possibly protocol-qualified) then it must actually be a pointer
to a class object.  Otherwise ARC does not enforce the Objective-C type system
as long as the implementing methods follow the signature of the static type.
It is undefined behavior if ARC is exposed to an invalid pointer.

For ARC's purposes, a valid object is one with "well-behaved" retaining
operations.  Specifically, the object must be laid out such that the
Objective-C message send machinery can successfully send it the following
messages:

* ``retain``, taking no arguments and returning a pointer to the object.
* ``release``, taking no arguments and returning ``void``.
* ``autorelease``, taking no arguments and returning a pointer to the object.

The behavior of these methods is constrained in the following ways.  The term
:arc-term:`high-level semantics` is an intentionally vague term; the intent is
that programmers must implement these methods in a way such that the compiler,
modifying code in ways it deems safe according to these constraints, will not
violate their requirements.  For example, if the user puts logging statements
in ``retain``, they should not be surprised if those statements are executed
more or less often depending on optimization settings.  These constraints are
not exhaustive of the optimization opportunities: values held in local
variables are subject to additional restrictions, described later in this
document.

It is undefined behavior if a computation history featuring a send of
``retain`` followed by a send of ``release`` to the same object, with no
intervening ``release`` on that object, is not equivalent under the high-level
semantics to a computation history in which these sends are removed.  Note that
this implies that these methods may not raise exceptions.

It is undefined behavior if a computation history features any use whatsoever
of an object following the completion of a send of ``release`` that is not
preceded by a send of ``retain`` to the same object.

The behavior of ``autorelease`` must be equivalent to sending ``release`` when
one of the autorelease pools currently in scope is popped.  It may not throw an
exception.

When the semantics call for performing one of these operations on a retainable
object pointer, if that pointer is ``null`` then the effect is a no-op.

All of the semantics described in this document are subject to additional
:ref:`optimization rules <arc.optimization>` which permit the removal or
optimization of operations based on local knowledge of data flow.  The
semantics describe the high-level behaviors that the compiler implements, not
an exact sequence of operations that a program will be compiled into.

.. _arc.objects.operands:

Retainable object pointers as operands and arguments
----------------------------------------------------

In general, ARC does not perform retain or release operations when simply using
a retainable object pointer as an operand within an expression.  This includes:

* loading a retainable pointer from an object with non-weak :ref:`ownership
  <arc.ownership>`,
* passing a retainable pointer as an argument to a function or method, and
* receiving a retainable pointer as the result of a function or method call.

.. admonition:: Rationale

  While this might seem uncontroversial, it is actually unsafe when multiple
  expressions are evaluated in "parallel", as with binary operators and calls,
  because (for example) one expression might load from an object while another
  writes to it.  However, C and C++ already call this undefined behavior
  because the evaluations are unsequenced, and ARC simply exploits that here to
  avoid needing to retain arguments across a large number of calls.

The remainder of this section describes exceptions to these rules, how those
exceptions are detected, and what those exceptions imply semantically.

.. _arc.objects.operands.consumed:

Consumed parameters
^^^^^^^^^^^^^^^^^^^

A function or method parameter of retainable object pointer type may be marked
as :arc-term:`consumed`, signifying that the callee expects to take ownership
of a +1 retain count.  This is done by adding the ``ns_consumed`` attribute to
the parameter declaration, like so:

.. code-block:: objc

  void foo(__attribute((ns_consumed)) id x);
  - (void) foo: (id) __attribute((ns_consumed)) x;

This attribute is part of the type of the function or method, not the type of
the parameter.  It controls only how the argument is passed and received.

When passing such an argument, ARC retains the argument prior to making the
call.

When receiving such an argument, ARC releases the argument at the end of the
function, subject to the usual optimizations for local values.

.. admonition:: Rationale

  This formalizes direct transfers of ownership from a caller to a callee.  The
  most common scenario here is passing the ``self`` parameter to ``init``, but
  it is useful to generalize.  Typically, local optimization will remove any
  extra retains and releases: on the caller side the retain will be merged with
  a +1 source, and on the callee side the release will be rolled into the
  initialization of the parameter.

The implicit ``self`` parameter of a method may be marked as consumed by adding
``__attribute__((ns_consumes_self))`` to the method declaration.  Methods in
the ``init`` :ref:`family <arc.method-families>` are treated as if they were
implicitly marked with this attribute.

It is undefined behavior if an Objective-C message send to a method with
``ns_consumed`` parameters (other than self) is made with a null receiver.  It
is undefined behavior if the method to which an Objective-C message send
statically resolves to has a different set of ``ns_consumed`` parameters than
the method it dynamically resolves to.  It is undefined behavior if a block or
function call is made through a static type with a different set of
``ns_consumed`` parameters than the implementation of the called block or
function.

.. admonition:: Rationale

  Consumed parameters with null receiver are a guaranteed leak.  Mismatches
  with consumed parameters will cause over-retains or over-releases, depending
  on the direction.  The rule about function calls is really just an
  application of the existing C/C++ rule about calling functions through an
  incompatible function type, but it's useful to state it explicitly.

.. _arc.object.operands.retained-return-values:

Retained return values
^^^^^^^^^^^^^^^^^^^^^^

A function or method which returns a retainable object pointer type may be
marked as returning a retained value, signifying that the caller expects to take
ownership of a +1 retain count.  This is done by adding the
``ns_returns_retained`` attribute to the function or method declaration, like
so:

.. code-block:: objc

  id foo(void) __attribute((ns_returns_retained));
  - (id) foo __attribute((ns_returns_retained));

This attribute is part of the type of the function or method.

When returning from such a function or method, ARC retains the value at the
point of evaluation of the return statement, before leaving all local scopes.

When receiving a return result from such a function or method, ARC releases the
value at the end of the full-expression it is contained within, subject to the
usual optimizations for local values.

.. admonition:: Rationale

  This formalizes direct transfers of ownership from a callee to a caller.  The
  most common scenario this models is the retained return from ``init``,
  ``alloc``, ``new``, and ``copy`` methods, but there are other cases in the
  frameworks.  After optimization there are typically no extra retains and
  releases required.

Methods in the ``alloc``, ``copy``, ``init``, ``mutableCopy``, and ``new``
:ref:`families <arc.method-families>` are implicitly marked
``__attribute__((ns_returns_retained))``.  This may be suppressed by explicitly
marking the method ``__attribute__((ns_returns_not_retained))``.

It is undefined behavior if the method to which an Objective-C message send
statically resolves has different retain semantics on its result from the
method it dynamically resolves to.  It is undefined behavior if a block or
function call is made through a static type with different retain semantics on
its result from the implementation of the called block or function.

.. admonition:: Rationale

  Mismatches with returned results will cause over-retains or over-releases,
  depending on the direction.  Again, the rule about function calls is really
  just an application of the existing C/C++ rule about calling functions
  through an incompatible function type.

.. _arc.objects.operands.unretained-returns:

Unretained return values
^^^^^^^^^^^^^^^^^^^^^^^^

A method or function which returns a retainable object type but does not return
a retained value must ensure that the object is still valid across the return
boundary.

When returning from such a function or method, ARC retains the value at the
point of evaluation of the return statement, then leaves all local scopes, and
then balances out the retain while ensuring that the value lives across the
call boundary.  In the worst case, this may involve an ``autorelease``, but
callers must not assume that the value is actually in the autorelease pool.

ARC performs no extra mandatory work on the caller side, although it may elect
to do something to shorten the lifetime of the returned value.

.. admonition:: Rationale

  It is common in non-ARC code to not return an autoreleased value; therefore
  the convention does not force either path.  It is convenient to not be
  required to do unnecessary retains and autoreleases; this permits
  optimizations such as eliding retain/autoreleases when it can be shown that
  the original pointer will still be valid at the point of return.

A method or function may be marked with
``__attribute__((ns_returns_autoreleased))`` to indicate that it returns a
pointer which is guaranteed to be valid at least as long as the innermost
autorelease pool.  There are no additional semantics enforced in the definition
of such a method; it merely enables optimizations in callers.

.. _arc.objects.operands.casts:

Bridged casts
^^^^^^^^^^^^^

A :arc-term:`bridged cast` is a C-style cast annotated with one of three
keywords:

* ``(__bridge T) op`` casts the operand to the destination type ``T``.  If
  ``T`` is a retainable object pointer type, then ``op`` must have a
  non-retainable pointer type.  If ``T`` is a non-retainable pointer type,
  then ``op`` must have a retainable object pointer type.  Otherwise the cast
  is ill-formed.  There is no transfer of ownership, and ARC inserts no retain
  operations.
* ``(__bridge_retained T) op`` casts the operand, which must have retainable
  object pointer type, to the destination type, which must be a non-retainable
  pointer type.  ARC retains the value, subject to the usual optimizations on
  local values, and the recipient is responsible for balancing that +1.
* ``(__bridge_transfer T) op`` casts the operand, which must have
  non-retainable pointer type, to the destination type, which must be a
  retainable object pointer type.  ARC will release the value at the end of
  the enclosing full-expression, subject to the usual optimizations on local
  values.

These casts are required in order to transfer objects in and out of ARC
control; see the rationale in the section on :ref:`conversion of retainable
object pointers <arc.objects.restrictions.conversion>`.

Using a ``__bridge_retained`` or ``__bridge_transfer`` cast purely to convince
ARC to emit an unbalanced retain or release, respectively, is poor form.

.. _arc.objects.restrictions:

Restrictions
------------

.. _arc.objects.restrictions.conversion:

Conversion of retainable object pointers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In general, a program which attempts to implicitly or explicitly convert a
value of retainable object pointer type to any non-retainable type, or
vice-versa, is ill-formed.  For example, an Objective-C object pointer shall
not be converted to ``void*``.  As an exception, cast to ``intptr_t`` is
allowed because such casts are not transferring ownership.  The :ref:`bridged
casts <arc.objects.operands.casts>` may be used to perform these conversions
where necessary.

.. admonition:: Rationale

  We cannot ensure the correct management of the lifetime of objects if they
  may be freely passed around as unmanaged types.  The bridged casts are
  provided so that the programmer may explicitly describe whether the cast
  transfers control into or out of ARC.

However, the following exceptions apply.

.. _arc.objects.restrictions.conversion.with.known.semantics:

Conversion to retainable object pointer type of expressions with known semantics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:when-revised:`[beginning Apple 4.0, LLVM 3.1]`
:revision:`These exceptions have been greatly expanded; they previously applied
only to a much-reduced subset which is difficult to categorize but which
included null pointers, message sends (under the given rules), and the various
global constants.`

An unbridged conversion to a retainable object pointer type from a type other
than a retainable object pointer type is ill-formed, as discussed above, unless
the operand of the cast has a syntactic form which is known retained, known
unretained, or known retain-agnostic.

An expression is :arc-term:`known retain-agnostic` if it is:

* an Objective-C string literal,
* a load from a ``const`` system global variable of :ref:`C retainable pointer
  type <arc.misc.c-retainable>`, or
* a null pointer constant.

An expression is :arc-term:`known unretained` if it is an rvalue of :ref:`C
retainable pointer type <arc.misc.c-retainable>` and it is:

* a direct call to a function, and either that function has the
  ``cf_returns_not_retained`` attribute or it is an :ref:`audited
  <arc.misc.c-retainable.audit>` function that does not have the
  ``cf_returns_retained`` attribute and does not follow the create/copy naming
  convention,
* a message send, and the declared method either has the
  ``cf_returns_not_retained`` attribute or it has neither the
  ``cf_returns_retained`` attribute nor a :ref:`selector family
  <arc.method-families>` that implies a retained result, or
* :when-revised:`[beginning LLVM 3.6]` :revision:`a load from a` ``const``
  :revision:`non-system global variable.`

An expression is :arc-term:`known retained` if it is an rvalue of :ref:`C
retainable pointer type <arc.misc.c-retainable>` and it is:

* a message send, and the declared method either has the
  ``cf_returns_retained`` attribute, or it does not have the
  ``cf_returns_not_retained`` attribute but it does have a :ref:`selector
  family <arc.method-families>` that implies a retained result.

Furthermore:

* a comma expression is classified according to its right-hand side,
* a statement expression is classified according to its result expression, if
  it has one,
* an lvalue-to-rvalue conversion applied to an Objective-C property lvalue is
  classified according to the underlying message send, and
* a conditional operator is classified according to its second and third
  operands, if they agree in classification, or else the other if one is known
  retain-agnostic.

If the cast operand is known retained, the conversion is treated as a
``__bridge_transfer`` cast.  If the cast operand is known unretained or known
retain-agnostic, the conversion is treated as a ``__bridge`` cast.

.. admonition:: Rationale

  Bridging casts are annoying.  Absent the ability to completely automate the
  management of CF objects, however, we are left with relatively poor attempts
  to reduce the need for a glut of explicit bridges.  Hence these rules.

  We've so far consciously refrained from implicitly turning retained CF
  results from function calls into ``__bridge_transfer`` casts.  The worry is
  that some code patterns  ---  for example, creating a CF value, assigning it
  to an ObjC-typed local, and then calling ``CFRelease`` when done  ---  are a
  bit too likely to be accidentally accepted, leading to mysterious behavior.

  For loads from ``const`` global variables of :ref:`C retainable pointer type
  <arc.misc.c-retainable>`, it is reasonable to assume that global system
  constants were initialized with true constants (e.g. string literals), but
  user constants might have been initialized with something dynamically
  allocated, using a global initializer.

.. _arc.objects.restrictions.conversion-exception-contextual:

Conversion from retainable object pointer type in certain contexts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:when-revised:`[beginning Apple 4.0, LLVM 3.1]`

If an expression of retainable object pointer type is explicitly cast to a
:ref:`C retainable pointer type <arc.misc.c-retainable>`, the program is
ill-formed as discussed above unless the result is immediately used:

* to initialize a parameter in an Objective-C message send where the parameter
  is not marked with the ``cf_consumed`` attribute, or
* to initialize a parameter in a direct call to an
  :ref:`audited <arc.misc.c-retainable.audit>` function where the parameter is
  not marked with the ``cf_consumed`` attribute.

.. admonition:: Rationale

  Consumed parameters are left out because ARC would naturally balance them
  with a retain, which was judged too treacherous.  This is in part because
  several of the most common consuming functions are in the ``Release`` family,
  and it would be quite unfortunate for explicit releases to be silently
  balanced out in this way.

.. _arc.ownership:

Ownership qualification
=======================

This section describes the behavior of *objects* of retainable object pointer
type; that is, locations in memory which store retainable object pointers.

A type is a :arc-term:`retainable object owner type` if it is a retainable
object pointer type or an array type whose element type is a retainable object
owner type.

An :arc-term:`ownership qualifier` is a type qualifier which applies only to
retainable object owner types.  An array type is ownership-qualified according
to its element type, and adding an ownership qualifier to an array type so
qualifies its element type.

A program is ill-formed if it attempts to apply an ownership qualifier to a
type which is already ownership-qualified, even if it is the same qualifier.
There is a single exception to this rule: an ownership qualifier may be applied
to a substituted template type parameter, which overrides the ownership
qualifier provided by the template argument.

When forming a function type, the result type is adjusted so that any
top-level ownership qualifier is deleted.

Except as described under the :ref:`inference rules <arc.ownership.inference>`,
a program is ill-formed if it attempts to form a pointer or reference type to a
retainable object owner type which lacks an ownership qualifier.

.. admonition:: Rationale

  These rules, together with the inference rules, ensure that all objects and
  lvalues of retainable object pointer type have an ownership qualifier.  The
  ability to override an ownership qualifier during template substitution is
  required to counteract the :ref:`inference of __strong for template type
  arguments <arc.ownership.inference.template.arguments>`.  Ownership qualifiers
  on return types are dropped because they serve no purpose there except to
  cause spurious problems with overloading and templates.

There are four ownership qualifiers:

* ``__autoreleasing``
* ``__strong``
* ``__unsafe_unretained``
* ``__weak``

A type is :arc-term:`nontrivially ownership-qualified` if it is qualified with
``__autoreleasing``, ``__strong``, or ``__weak``.

.. _arc.ownership.spelling:

Spelling
--------

The names of the ownership qualifiers are reserved for the implementation.  A
program may not assume that they are or are not implemented with macros, or
what those macros expand to.

An ownership qualifier may be written anywhere that any other type qualifier
may be written.

If an ownership qualifier appears in the *declaration-specifiers*, the
following rules apply:

* if the type specifier is a retainable object owner type, the qualifier
  initially applies to that type;

* otherwise, if the outermost non-array declarator is a pointer
  or block pointer declarator, the qualifier initially applies to
  that type;

* otherwise the program is ill-formed.

* If the qualifier is so applied at a position in the declaration
  where the next-innermost declarator is a function declarator, and
  there is an block declarator within that function declarator, then
  the qualifier applies instead to that block declarator and this rule
  is considered afresh beginning from the new position.

If an ownership qualifier appears on the declarator name, or on the declared
object, it is applied to the innermost pointer or block-pointer type.

If an ownership qualifier appears anywhere else in a declarator, it applies to
the type there.

.. admonition:: Rationale

  Ownership qualifiers are like ``const`` and ``volatile`` in the sense
  that they may sensibly apply at multiple distinct positions within a
  declarator.  However, unlike those qualifiers, there are many
  situations where they are not meaningful, and so we make an effort
  to "move" the qualifier to a place where it will be meaningful.  The
  general goal is to allow the programmer to write, say, ``__strong``
  before the entire declaration and have it apply in the leftmost
  sensible place.

.. _arc.ownership.spelling.property:

Property declarations
^^^^^^^^^^^^^^^^^^^^^

A property of retainable object pointer type may have ownership.  If the
property's type is ownership-qualified, then the property has that ownership.
If the property has one of the following modifiers, then the property has the
corresponding ownership.  A property is ill-formed if it has conflicting
sources of ownership, or if it has redundant ownership modifiers, or if it has
``__autoreleasing`` ownership.

* ``assign`` implies ``__unsafe_unretained`` ownership.
* ``copy`` implies ``__strong`` ownership, as well as the usual behavior of
  copy semantics on the setter.
* ``retain`` implies ``__strong`` ownership.
* ``strong`` implies ``__strong`` ownership.
* ``unsafe_unretained`` implies ``__unsafe_unretained`` ownership.
* ``weak`` implies ``__weak`` ownership.

With the exception of ``weak``, these modifiers are available in non-ARC
modes.

A property's specified ownership is preserved in its metadata, but otherwise
the meaning is purely conventional unless the property is synthesized.  If a
property is synthesized, then the :arc-term:`associated instance variable` is
the instance variable which is named, possibly implicitly, by the
``@synthesize`` declaration.  If the associated instance variable already
exists, then its ownership qualification must equal the ownership of the
property; otherwise, the instance variable is created with that ownership
qualification.

A property of retainable object pointer type which is synthesized without a
source of ownership has the ownership of its associated instance variable, if it
already exists; otherwise, :when-revised:`[beginning Apple 3.1, LLVM 3.1]`
:revision:`its ownership is implicitly` ``strong``.  Prior to this revision, it
was ill-formed to synthesize such a property.

.. admonition:: Rationale

  Using ``strong`` by default is safe and consistent with the generic ARC rule
  about :ref:`inferring ownership <arc.ownership.inference.variables>`.  It is,
  unfortunately, inconsistent with the non-ARC rule which states that such
  properties are implicitly ``assign``.  However, that rule is clearly
  untenable in ARC, since it leads to default-unsafe code.  The main merit to
  banning the properties is to avoid confusion with non-ARC practice, which did
  not ultimately strike us as sufficient to justify requiring extra syntax and
  (more importantly) forcing novices to understand ownership rules just to
  declare a property when the default is so reasonable.  Changing the rule away
  from non-ARC practice was acceptable because we had conservatively banned the
  synthesis in order to give ourselves exactly this leeway.

Applying ``__attribute__((NSObject))`` to a property not of retainable object
pointer type has the same behavior it does outside of ARC: it requires the
property type to be some sort of pointer and permits the use of modifiers other
than ``assign``.  These modifiers only affect the synthesized getter and
setter; direct accesses to the ivar (even if synthesized) still have primitive
semantics, and the value in the ivar will not be automatically released during
deallocation.

.. _arc.ownership.semantics:

Semantics
---------

There are five :arc-term:`managed operations` which may be performed on an
object of retainable object pointer type.  Each qualifier specifies different
semantics for each of these operations.  It is still undefined behavior to
access an object outside of its lifetime.

A load or store with "primitive semantics" has the same semantics as the
respective operation would have on an ``void*`` lvalue with the same alignment
and non-ownership qualification.

:arc-term:`Reading` occurs when performing a lvalue-to-rvalue conversion on an
object lvalue.

* For ``__weak`` objects, the current pointee is retained and then released at
  the end of the current full-expression. In particular, messaging a ``__weak``
  object keeps the object retained until the end of the full expression.

  .. code-block:: objc

    __weak MyObject *weakObj;

    void foo() {
      // weakObj is retained before the message send and released at the end of
      // the full expression.
      [weakObj m];
    }

  This must execute atomically with respect to assignments and to the final
  release of the pointee.
* For all other objects, the lvalue is loaded with primitive semantics.

:arc-term:`Assignment` occurs when evaluating an assignment operator.  The
semantics vary based on the qualification:

* For ``__strong`` objects, the new pointee is first retained; second, the
  lvalue is loaded with primitive semantics; third, the new pointee is stored
  into the lvalue with primitive semantics; and finally, the old pointee is
  released.  This is not performed atomically; external synchronization must be
  used to make this safe in the face of concurrent loads and stores.
* For ``__weak`` objects, the lvalue is updated to point to the new pointee,
  unless the new pointee is an object currently undergoing deallocation, in
  which case the lvalue is updated to a null pointer.  This must execute
  atomically with respect to other assignments to the object, to reads from the
  object, and to the final release of the new pointee.
* For ``__unsafe_unretained`` objects, the new pointee is stored into the
  lvalue using primitive semantics.
* For ``__autoreleasing`` objects, the new pointee is retained, autoreleased,
  and stored into the lvalue using primitive semantics.

:arc-term:`Initialization` occurs when an object's lifetime begins, which
depends on its storage duration.  Initialization proceeds in two stages:

#. First, a null pointer is stored into the lvalue using primitive semantics.
   This step is skipped if the object is ``__unsafe_unretained``.
#. Second, if the object has an initializer, that expression is evaluated and
   then assigned into the object using the usual assignment semantics.

:arc-term:`Destruction` occurs when an object's lifetime ends.  In all cases it
is semantically equivalent to assigning a null pointer to the object, with the
proviso that of course the object cannot be legally read after the object's
lifetime ends.

:arc-term:`Moving` occurs in specific situations where an lvalue is "moved
from", meaning that its current pointee will be used but the object may be left
in a different (but still valid) state.  This arises with ``__block`` variables
and rvalue references in C++.  For ``__strong`` lvalues, moving is equivalent
to loading the lvalue with primitive semantics, writing a null pointer to it
with primitive semantics, and then releasing the result of the load at the end
of the current full-expression.  For all other lvalues, moving is equivalent to
reading the object.

.. _arc.ownership.restrictions:

Restrictions
------------

.. _arc.ownership.restrictions.weak:

Weak-unavailable types
^^^^^^^^^^^^^^^^^^^^^^

It is explicitly permitted for Objective-C classes to not support ``__weak``
references.  It is undefined behavior to perform an operation with weak
assignment semantics with a pointer to an Objective-C object whose class does
not support ``__weak`` references.

.. admonition:: Rationale

  Historically, it has been possible for a class to provide its own
  reference-count implementation by overriding ``retain``, ``release``, etc.
  However, weak references to an object require coordination with its class's
  reference-count implementation because, among other things, weak loads and
  stores must be atomic with respect to the final release.  Therefore, existing
  custom reference-count implementations will generally not support weak
  references without additional effort.  This is unavoidable without breaking
  binary compatibility.

A class may indicate that it does not support weak references by providing the
``objc_arc_weak_reference_unavailable`` attribute on the class's interface declaration.  A
retainable object pointer type is **weak-unavailable** if
is a pointer to an (optionally protocol-qualified) Objective-C class ``T`` where
``T`` or one of its superclasses has the ``objc_arc_weak_reference_unavailable``
attribute.  A program is ill-formed if it applies the ``__weak`` ownership
qualifier to a weak-unavailable type or if the value operand of a weak
assignment operation has a weak-unavailable type.

.. _arc.ownership.restrictions.autoreleasing:

Storage duration of ``__autoreleasing`` objects
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A program is ill-formed if it declares an ``__autoreleasing`` object of
non-automatic storage duration.  A program is ill-formed if it captures an
``__autoreleasing`` object in a block or, unless by reference, in a C++11
lambda.

.. admonition:: Rationale

  Autorelease pools are tied to the current thread and scope by their nature.
  While it is possible to have temporary objects whose instance variables are
  filled with autoreleased objects, there is no way that ARC can provide any
  sort of safety guarantee there.

It is undefined behavior if a non-null pointer is assigned to an
``__autoreleasing`` object while an autorelease pool is in scope and then that
object is read after the autorelease pool's scope is left.

.. _arc.ownership.restrictions.conversion.indirect:

Conversion of pointers to ownership-qualified types
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A program is ill-formed if an expression of type ``T*`` is converted,
explicitly or implicitly, to the type ``U*``, where ``T`` and ``U`` have
different ownership qualification, unless:

* ``T`` is qualified with ``__strong``, ``__autoreleasing``, or
  ``__unsafe_unretained``, and ``U`` is qualified with both ``const`` and
  ``__unsafe_unretained``; or
* either ``T`` or ``U`` is ``cv void``, where ``cv`` is an optional sequence
  of non-ownership qualifiers; or
* the conversion is requested with a ``reinterpret_cast`` in Objective-C++; or
* the conversion is a well-formed :ref:`pass-by-writeback
  <arc.ownership.restrictions.pass_by_writeback>`.

The analogous rule applies to ``T&`` and ``U&`` in Objective-C++.

.. admonition:: Rationale

  These rules provide a reasonable level of type-safety for indirect pointers,
  as long as the underlying memory is not deallocated.  The conversion to
  ``const __unsafe_unretained`` is permitted because the semantics of reads are
  equivalent across all these ownership semantics, and that's a very useful and
  common pattern.  The interconversion with ``void*`` is useful for allocating
  memory or otherwise escaping the type system, but use it carefully.
  ``reinterpret_cast`` is considered to be an obvious enough sign of taking
  responsibility for any problems.

It is undefined behavior to access an ownership-qualified object through an
lvalue of a differently-qualified type, except that any non-``__weak`` object
may be read through an ``__unsafe_unretained`` lvalue.

It is undefined behavior if the storage of a ``__strong`` or ``__weak``
object is not properly initialized before the first managed operation
is performed on the object, or if the storage of such an object is freed
or reused before the object has been properly deinitialized.  Storage for
a ``__strong`` or ``__weak`` object may be properly initialized by filling
it with the representation of a null pointer, e.g. by acquiring the memory
with ``calloc`` or using ``bzero`` to zero it out.  A ``__strong`` or
``__weak`` object may be properly deinitialized by assigning a null pointer
into it.  A ``__strong`` object may also be properly initialized
by copying into it (e.g. with ``memcpy``) the representation of a
different ``__strong`` object whose storage has been properly initialized;
doing this properly deinitializes the source object and causes its storage
to no longer be properly initialized.  A ``__weak`` object may not be
representation-copied in this way.

These requirements are followed automatically for objects whose
initialization and deinitialization are under the control of ARC:

* objects of static, automatic, and temporary storage duration
* instance variables of Objective-C objects
* elements of arrays where the array object's initialization and
  deinitialization are under the control of ARC
* fields of Objective-C struct types where the struct object's
  initialization and deinitialization are under the control of ARC
* non-static data members of Objective-C++ non-union class types
* Objective-C++ objects and arrays of dynamic storage duration created
  with the ``new`` or ``new[]`` operators and destroyed with the
  corresponding ``delete`` or ``delete[]`` operator

They are not followed automatically for these objects:

* objects of dynamic storage duration created in other memory, such as
  that returned by ``malloc``
* union members

.. admonition:: Rationale

  ARC must perform special operations when initializing an object and
  when destroying it.  In many common situations, ARC knows when an
  object is created and when it is destroyed and can ensure that these
  operations are performed correctly.  Otherwise, however, ARC requires
  programmer cooperation to establish its initialization invariants
  because it is infeasible for ARC to dynamically infer whether they
  are intact.  For example, there is no syntactic difference in C between
  an assignment that is intended by the programmer to initialize a variable
  and one that is intended to replace the existing value stored there,
  but ARC must perform one operation or the other.  ARC chooses to always
  assume that objects are initialized (except when it is in charge of
  initializing them) because the only workable alternative would be to
  ban all code patterns that could potentially be used to access
  uninitialized memory, and that would be too limiting.  In practice,
  this is rarely a problem because programmers do not generally need to
  work with objects for which the requirements are not handled
  automatically.

Note that dynamically-allocated Objective-C++ arrays of
nontrivially-ownership-qualified type are not ABI-compatible with non-ARC
code because the non-ARC code will consider the element type to be POD.
Such arrays that are ``new[]``'d in ARC translation units cannot be
``delete[]``'d in non-ARC translation units and vice-versa.

.. _arc.ownership.restrictions.pass_by_writeback:

Passing to an out parameter by writeback
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If the argument passed to a parameter of type ``T __autoreleasing *`` has type
``U oq *``, where ``oq`` is an ownership qualifier, then the argument is a
candidate for :arc-term:`pass-by-writeback`` if:

* ``oq`` is ``__strong`` or ``__weak``, and
* it would be legal to initialize a ``T __strong *`` with a ``U __strong *``.

For purposes of overload resolution, an implicit conversion sequence requiring
a pass-by-writeback is always worse than an implicit conversion sequence not
requiring a pass-by-writeback.

The pass-by-writeback is ill-formed if the argument expression does not have a
legal form:

* ``&var``, where ``var`` is a scalar variable of automatic storage duration
  with retainable object pointer type
* a conditional expression where the second and third operands are both legal
  forms
* a cast whose operand is a legal form
* a null pointer constant

.. admonition:: Rationale

  The restriction in the form of the argument serves two purposes.  First, it
  makes it impossible to pass the address of an array to the argument, which
  serves to protect against an otherwise serious risk of mis-inferring an
  "array" argument as an out-parameter.  Second, it makes it much less likely
  that the user will see confusing aliasing problems due to the implementation,
  below, where their store to the writeback temporary is not immediately seen
  in the original argument variable.

A pass-by-writeback is evaluated as follows:

#. The argument is evaluated to yield a pointer ``p`` of type ``U oq *``.
#. If ``p`` is a null pointer, then a null pointer is passed as the argument,
   and no further work is required for the pass-by-writeback.
#. Otherwise, a temporary of type ``T __autoreleasing`` is created and
   initialized to a null pointer.
#. If the parameter is not an Objective-C method parameter marked ``out``,
   then ``*p`` is read, and the result is written into the temporary with
   primitive semantics.
#. The address of the temporary is passed as the argument to the actual call.
#. After the call completes, the temporary is loaded with primitive
   semantics, and that value is assigned into ``*p``.

.. admonition:: Rationale

  This is all admittedly convoluted.  In an ideal world, we would see that a
  local variable is being passed to an out-parameter and retroactively modify
  its type to be ``__autoreleasing`` rather than ``__strong``.  This would be
  remarkably difficult and not always well-founded under the C type system.
  However, it was judged unacceptably invasive to require programmers to write
  ``__autoreleasing`` on all the variables they intend to use for
  out-parameters.  This was the least bad solution.

.. _arc.ownership.restrictions.records:

Ownership-qualified fields of structs and unions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A member of a struct or union may be declared to have ownership-qualified
type.  If the type is qualified with ``__unsafe_unretained``, the semantics
of the containing aggregate are unchanged from the semantics of an unqualified type in a non-ARC mode.  If the type is qualified with ``__autoreleasing``, the program is ill-formed.  Otherwise, if the type is nontrivially ownership-qualified, additional rules apply.

Both Objective-C and Objective-C++ support nontrivially ownership-qualified
fields.  Due to formal differences between the standards, the formal
treatment is different; however, the basic language model is intended to
be the same for identical code.

.. admonition:: Rationale

  Permitting ``__strong`` and ``__weak`` references in aggregate types
  allows programmers to take advantage of the normal language tools of
  C and C++ while still automatically managing memory.  While it is
  usually simpler and more idiomatic to use Objective-C objects for
  secondary data structures, doing so can introduce extra allocation
  and message-send overhead, which can cause to unacceptable
  performance.  Using structs can resolve some of this tension.

  ``__autoreleasing`` is forbidden because it is treacherous to rely
  on autoreleases as an ownership tool outside of a function-local
  contexts.

  Earlier releases of Clang permitted ``__strong`` and ``__weak`` only
  references in Objective-C++ classes, not in Objective-C.  This
  restriction was an undesirable short-term constraint arising from the
  complexity of adding support for non-trivial struct types to C.

In Objective-C++, nontrivially ownership-qualified types are treated
for nearly all purposes as if they were class types with non-trivial
default constructors, copy constructors, move constructors, copy assignment
operators, move assignment operators, and destructors.  This includes the
determination of the triviality of special members of classes with a
non-static data member of such a type.

In Objective-C, the definition cannot be so succinct: because the C
standard lacks rules for non-trivial types, those rules must first be
developed.  They are given in the next section.  The intent is that these
rules are largely consistent with the rules of C++ for code expressible
in both languages.

Formal rules for non-trivial types in C
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following are base rules which can be added to C to support
implementation-defined non-trivial types.

A type in C is said to be *non-trivial to copy*, *non-trivial to destroy*,
or *non-trivial to default-initialize* if:

- it is a struct or union containing a member whose type is non-trivial
  to (respectively) copy, destroy, or default-initialize;

- it is a qualified type whose unqualified type is non-trivial to
  (respectively) copy, destroy, or default-initialize (for at least
  the standard C qualifiers); or

- it is an array type whose element type is non-trivial to (respectively)
  copy, destroy, or default-initialize.

A type in C is said to be *illegal to copy*, *illegal to destroy*, or
*illegal to default-initialize* if:

- it is a union which contains a member whose type is either illegal
  or non-trivial to (respectively) copy, destroy, or initialize;

- it is a qualified type whose unqualified type is illegal to
  (respectively) copy, destroy, or default-initialize (for at least
  the standard C qualifiers); or

- it is an array type whose element type is illegal to (respectively)
  copy, destroy, or default-initialize.

No type describable under the rules of the C standard shall be either
non-trivial or illegal to copy, destroy, or default-initialize.
An implementation may provide additional types which have one or more
of these properties.

An expression calls for a type to be copied if it:

- passes an argument of that type to a function call,
- defines a function which declares a parameter of that type,
- calls or defines a function which returns a value of that type,
- assigns to an l-value of that type, or
- converts an l-value of that type to an r-value.

A program calls for a type to be destroyed if it:

- passes an argument of that type to a function call,
- defines a function which declares a parameter of that type,
- calls or defines a function which returns a value of that type,
- creates an object of automatic storage duration of that type,
- assigns to an l-value of that type, or
- converts an l-value of that type to an r-value.

A program calls for a type to be default-initialized if it:

- declares a variable of that type without an initializer.

An expression is ill-formed if calls for a type to be copied,
destroyed, or default-initialized and that type is illegal to
(respectively) copy, destroy, or default-initialize.

A program is ill-formed if it contains a function type specifier
with a parameter or return type that is illegal to copy or
destroy.  If a function type specifier would be ill-formed for this
reason except that the parameter or return type was incomplete at
that point in the translation unit, the program is ill-formed but
no diagnostic is required.

A ``goto`` or ``switch`` is ill-formed if it jumps into the scope of
an object of automatic storage duration whose type is non-trivial to
destroy.

C specifies that it is generally undefined behavior to access an l-value
if there is no object of that type at that location.  Implementations
are often lenient about this, but non-trivial types generally require
it to be enforced more strictly.  The following rules apply:

The *static subobjects* of a type ``T`` at a location ``L`` are:

  - an object of type ``T`` spanning from ``L`` to ``L + sizeof(T)``;

  - if ``T`` is a struct type, then for each field ``f`` of that struct,
    the static subobjects of ``T`` at location ``L + offsetof(T, .f)``; and

  - if ``T`` is the array type ``E[N]``, then for each ``i`` satisfying
    ``0 <= i < N``, the static subobjects of ``E`` at location
    ``L + i * sizeof(E)``.

If an l-value is converted to an r-value, then all static subobjects
whose types are non-trivial to copy are accessed.  If an l-value is
assigned to, or if an object of automatic storage duration goes out of
scope, then all static subobjects of types that are non-trivial to destroy
are accessed.

A dynamic object is created at a location if an initialization initializes
an object of that type there.  A dynamic object ceases to exist at a
location if the memory is repurposed.  Memory is repurposed if it is
freed or if a different dynamic object is created there, for example by
assigning into a different union member.  An implementation may provide
additional rules for what constitutes creating or destroying a dynamic
object.

If an object is accessed under these rules at a location where no such
dynamic object exists, the program has undefined behavior.
If memory for a location is repurposed while a dynamic object that is
non-trivial to destroy exists at that location, the program has
undefined behavior.

.. admonition:: Rationale

  While these rules are far less fine-grained than C++, they are
  nonetheless sufficient to express a wide spectrum of types.
  Types that express some sort of ownership will generally be non-trivial
  to both copy and destroy and either non-trivial or illegal to
  default-initialize.  Types that don't express ownership may still
  be non-trivial to copy because of some sort of address sensitivity;
  for example, a relative reference.  Distinguishing default
  initialization allows types to impose policies about how they are
  created.

  These rules assume that assignment into an l-value is always a
  modification of an existing object rather than an initialization.
  Assignment is then a compound operation where the old value is
  read and destroyed, if necessary, and the new value is put into
  place.  These are the natural semantics of value propagation, where
  all basic operations on the type come down to copies and destroys,
  and everything else is just an optimization on top of those.

  The most glaring weakness of programming with non-trivial types in C
  is that there are no language mechanisms (akin to C++'s placement
  ``new`` and explicit destructor calls) for explicitly creating and
  destroying objects.  Clang should consider adding builtins for this
  purpose, as well as for common optimizations like destructive
  relocation.

Application of the formal C rules to nontrivial ownership qualifiers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Nontrivially ownership-qualified types are considered non-trivial
to copy, destroy, and default-initialize.

A dynamic object of nontrivially ownership-qualified type contingently
exists at a location if the memory is filled with a zero pattern, e.g.
by ``calloc`` or ``bzero``.  Such an object can be safely accessed in
all of the cases above, but its memory can also be safely repurposed.
Assigning a null pointer into an l-value of ``__weak`` or
``__strong``-qualified type accesses the dynamic object there (and thus
may have undefined behavior if no such object exists), but afterwards
the object's memory is guaranteed to be filled with a zero pattern
and thus may be either further accessed or repurposed as needed.
The upshot is that programs may safely initialize dynamically-allocated
memory for nontrivially ownership-qualified types by ensuring it is zero-initialized, and they may safely deinitialize memory before
freeing it by storing ``nil`` into any ``__strong`` or ``__weak``
references previously created in that memory.

C/C++ compatibility for structs and unions with non-trivial members
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Structs and unions with non-trivial members are compatible in
different language modes (e.g. between Objective-C and Objective-C++,
or between ARC and non-ARC modes) under the following conditions:

- The types must be compatible ignoring ownership qualifiers according
  to the baseline, non-ARC rules (e.g. C struct compatibility or C++'s
  ODR).  This condition implies a pairwise correspondence between
  fields.

  Note that an Objective-C++ class with base classes, a user-provided
  copy or move constructor, or a user-provided destructor is never
  compatible with an Objective-C type.

- If two fields correspond as above, and at least one of the fields is
  ownership-qualified, then:

    - the fields must be identically qualified, or else

    - one type must be unqualified (and thus declared in a non-ARC mode),
      and the other type must be qualified with ``__unsafe_unretained``
      or ``__strong``.

  Note that ``__weak`` fields must always be declared ``__weak``  because
  of the need to pin those fields in memory and keep them properly
  registered with the Objective-C runtime.  Non-ARC modes may still
  declare fields ``__weak`` by enabling ``-fobjc-weak``.

These compatibility rules permit a function that takes a parameter
of non-trivial struct type to be written in ARC and called from
non-ARC or vice-versa.  The convention for this always transfers
ownership of objects stored in ``__strong`` fields from the caller
to the callee, just as for an ``ns_consumed`` argument.  Therefore,
non-ARC callers must ensure that such fields are initialized to a +1
reference, and non-ARC callees must balance that +1 by releasing the
reference or transferring it as appropriate.

Likewise, a function returning a non-trivial struct may be written in
ARC and called from non-ARC or vice-versa.  The convention for this
always transfers ownership of objects stored in ``__strong`` fields
from the callee to the caller, and so callees must initialize such
fields with +1 references, and callers must balance that +1 by releasing
or transferring them.

Similar transfers of responsibility occur for ``__weak`` fields, but
since both sides must use native ``__weak`` support to ensure
calling convention compatibility, this transfer is always handled
automatically by the compiler.

.. admonition:: Rationale

  In earlier releases, when non-trivial ownership was only permitted
  on fields in Objective-C++, the ABI used for such classes was the
  ordinary ABI for non-trivial C++ classes, which passes arguments and
  returns indirectly and does not transfer responsibility for arguments.
  When support for Objective-C structs was added, it was decided to
  change to the current ABI for three reasons:

  - It permits ARC / non-ARC compatibility for structs containing only
    ``__strong`` references, as long as the non-ARC side is careful about
    transferring ownership.

  - It avoids unnecessary indirection for sufficiently small types that
    the C ABI would prefer to pass in registers.

  - Given that struct arguments must be produced at +1 to satisfy C's
    semantics of initializing the local parameter variable, transferring
    ownership of that copy to the callee is generally better for ARC
    optimization, since otherwise there will be releases in the caller
    that are much harder to pair with transfers in the callee.

  Breaking compatibility with existing Objective-C++ structures was
  considered an acceptable cost, as most Objective-C++ code does not have
  binary-compatibility requirements.  Any existing code which cannot accept
  this compatibility break, which is necessarily Objective-C++, should
  force the use of the standard C++ ABI by declaring an empty (but
  non-defaulted) destructor.

.. _arc.ownership.inference:

Ownership inference
-------------------

.. _arc.ownership.inference.variables:

Objects
^^^^^^^

If an object is declared with retainable object owner type, but without an
explicit ownership qualifier, its type is implicitly adjusted to have
``__strong`` qualification.

As a special case, if the object's base type is ``Class`` (possibly
protocol-qualified), the type is adjusted to have ``__unsafe_unretained``
qualification instead.

.. _arc.ownership.inference.indirect_parameters:

Indirect parameters
^^^^^^^^^^^^^^^^^^^

If a function or method parameter has type ``T*``, where ``T`` is an
ownership-unqualified retainable object pointer type, then:

* if ``T`` is ``const``-qualified or ``Class``, then it is implicitly
  qualified with ``__unsafe_unretained``;
* otherwise, it is implicitly qualified with ``__autoreleasing``.

.. admonition:: Rationale

  ``__autoreleasing`` exists mostly for this case, the Cocoa convention for
  out-parameters.  Since a pointer to ``const`` is obviously not an
  out-parameter, we instead use a type more useful for passing arrays.  If the
  user instead intends to pass in a *mutable* array, inferring
  ``__autoreleasing`` is the wrong thing to do; this directs some of the
  caution in the following rules about writeback.

Such a type written anywhere else would be ill-formed by the general rule
requiring ownership qualifiers.

This rule does not apply in Objective-C++ if a parameter's type is dependent in
a template pattern and is only *instantiated* to a type which would be a
pointer to an unqualified retainable object pointer type.  Such code is still
ill-formed.

.. admonition:: Rationale

  The convention is very unlikely to be intentional in template code.

.. _arc.ownership.inference.template.arguments:

Template arguments
^^^^^^^^^^^^^^^^^^

If a template argument for a template type parameter is an retainable object
owner type that does not have an explicit ownership qualifier, it is adjusted
to have ``__strong`` qualification.  This adjustment occurs regardless of
whether the template argument was deduced or explicitly specified.

.. admonition:: Rationale

  ``__strong`` is a useful default for containers (e.g., ``std::vector<id>``),
  which would otherwise require explicit qualification.  Moreover, unqualified
  retainable object pointer types are unlikely to be useful within templates,
  since they generally need to have a qualifier applied to the before being
  used.

.. _arc.method-families:

Method families
===============

An Objective-C method may fall into a :arc-term:`method family`, which is a
conventional set of behaviors ascribed to it by the Cocoa conventions.

A method is in a certain method family if:

* it has a ``objc_method_family`` attribute placing it in that family; or if
  not that,
* it does not have an ``objc_method_family`` attribute placing it in a
  different or no family, and
* its selector falls into the corresponding selector family, and
* its signature obeys the added restrictions of the method family.

A selector is in a certain selector family if, ignoring any leading
underscores, the first component of the selector either consists entirely of
the name of the method family or it begins with that name followed by a
character other than a lowercase letter.  For example, ``_perform:with:`` and
``performWith:`` would fall into the ``perform`` family (if we recognized one),
but ``performing:with`` would not.

The families and their added restrictions are:

* ``alloc`` methods must return a retainable object pointer type.
* ``copy`` methods must return a retainable object pointer type.
* ``mutableCopy`` methods must return a retainable object pointer type.
* ``new`` methods must return a retainable object pointer type.
* ``init`` methods must be instance methods and must return an Objective-C
  pointer type.  Additionally, a program is ill-formed if it declares or
  contains a call to an ``init`` method whose return type is neither ``id`` nor
  a pointer to a super-class or sub-class of the declaring class (if the method
  was declared on a class) or the static receiver type of the call (if it was
  declared on a protocol).

  .. admonition:: Rationale

    There are a fair number of existing methods with ``init``-like selectors
    which nonetheless don't follow the ``init`` conventions.  Typically these
    are either accidental naming collisions or helper methods called during
    initialization.  Because of the peculiar retain/release behavior of
    ``init`` methods, it's very important not to treat these methods as
    ``init`` methods if they aren't meant to be.  It was felt that implicitly
    defining these methods out of the family based on the exact relationship
    between the return type and the declaring class would be much too subtle
    and fragile.  Therefore we identify a small number of legitimate-seeming
    return types and call everything else an error.  This serves the secondary
    purpose of encouraging programmers not to accidentally give methods names
    in the ``init`` family.

    Note that a method with an ``init``-family selector which returns a
    non-Objective-C type (e.g. ``void``) is perfectly well-formed; it simply
    isn't in the ``init`` family.

A program is ill-formed if a method's declarations, implementations, and
overrides do not all have the same method family.

.. _arc.family.attribute:

Explicit method family control
------------------------------

A method may be annotated with the ``objc_method_family`` attribute to
precisely control which method family it belongs to.  If a method in an
``@implementation`` does not have this attribute, but there is a method
declared in the corresponding ``@interface`` that does, then the attribute is
copied to the declaration in the ``@implementation``.  The attribute is
available outside of ARC, and may be tested for with the preprocessor query
``__has_attribute(objc_method_family)``.

The attribute is spelled
``__attribute__((objc_method_family(`` *family* ``)))``.  If *family* is
``none``, the method has no family, even if it would otherwise be considered to
have one based on its selector and type.  Otherwise, *family* must be one of
``alloc``, ``copy``, ``init``, ``mutableCopy``, or ``new``, in which case the
method is considered to belong to the corresponding family regardless of its
selector.  It is an error if a method that is explicitly added to a family in
this way does not meet the requirements of the family other than the selector
naming convention.

.. admonition:: Rationale

  The rules codified in this document describe the standard conventions of
  Objective-C.  However, as these conventions have not heretofore been enforced
  by an unforgiving mechanical system, they are only imperfectly kept,
  especially as they haven't always even been precisely defined.  While it is
  possible to define low-level ownership semantics with attributes like
  ``ns_returns_retained``, this attribute allows the user to communicate
  semantic intent, which is of use both to ARC (which, e.g., treats calls to
  ``init`` specially) and the static analyzer.

.. _arc.family.semantics:

Semantics of method families
----------------------------

A method's membership in a method family may imply non-standard semantics for
its parameters and return type.

Methods in the ``alloc``, ``copy``, ``mutableCopy``, and ``new`` families ---
that is, methods in all the currently-defined families except ``init`` ---
implicitly :ref:`return a retained object
<arc.object.operands.retained-return-values>` as if they were annotated with
the ``ns_returns_retained`` attribute.  This can be overridden by annotating
the method with either of the ``ns_returns_autoreleased`` or
``ns_returns_not_retained`` attributes.

Properties also follow same naming rules as methods.  This means that those in
the ``alloc``, ``copy``, ``mutableCopy``, and ``new`` families provide access
to :ref:`retained objects <arc.object.operands.retained-return-values>`.  This
can be overridden by annotating the property with ``ns_returns_not_retained``
attribute.

.. _arc.family.semantics.init:

Semantics of ``init``
^^^^^^^^^^^^^^^^^^^^^

Methods in the ``init`` family implicitly :ref:`consume
<arc.objects.operands.consumed>` their ``self`` parameter and :ref:`return a
retained object <arc.object.operands.retained-return-values>`.  Neither of
these properties can be altered through attributes.

A call to an ``init`` method with a receiver that is either ``self`` (possibly
parenthesized or casted) or ``super`` is called a :arc-term:`delegate init
call`.  It is an error for a delegate init call to be made except from an
``init`` method, and excluding blocks within such methods.

As an exception to the :ref:`usual rule <arc.misc.self>`, the variable ``self``
is mutable in an ``init`` method and has the usual semantics for a ``__strong``
variable.  However, it is undefined behavior and the program is ill-formed, no
diagnostic required, if an ``init`` method attempts to use the previous value
of ``self`` after the completion of a delegate init call.  It is conventional,
but not required, for an ``init`` method to return ``self``.

It is undefined behavior for a program to cause two or more calls to ``init``
methods on the same object, except that each ``init`` method invocation may
perform at most one delegate init call.

.. _arc.family.semantics.result_type:

Related result types
^^^^^^^^^^^^^^^^^^^^

Certain methods are candidates to have :arc-term:`related result types`:

* class methods in the ``alloc`` and ``new`` method families
* instance methods in the ``init`` family
* the instance method ``self``
* outside of ARC, the instance methods ``retain`` and ``autorelease``

If the formal result type of such a method is ``id`` or protocol-qualified
``id``, or a type equal to the declaring class or a superclass, then it is said
to have a related result type.  In this case, when invoked in an explicit
message send, it is assumed to return a type related to the type of the
receiver:

* if it is a class method, and the receiver is a class name ``T``, the message
  send expression has type ``T*``; otherwise
* if it is an instance method, and the receiver has type ``T``, the message
  send expression has type ``T``; otherwise
* the message send expression has the normal result type of the method.

This is a new rule of the Objective-C language and applies outside of ARC.

.. admonition:: Rationale

  ARC's automatic code emission is more prone than most code to signature
  errors, i.e. errors where a call was emitted against one method signature,
  but the implementing method has an incompatible signature.  Having more
  precise type information helps drastically lower this risk, as well as
  catching a number of latent bugs.

.. _arc.optimization:

Optimization
============

Within this section, the word :arc-term:`function` will be used to
refer to any structured unit of code, be it a C function, an
Objective-C method, or a block.

This specification describes ARC as performing specific ``retain`` and
``release`` operations on retainable object pointers at specific
points during the execution of a program.  These operations make up a
non-contiguous subsequence of the computation history of the program.
The portion of this sequence for a particular retainable object
pointer for which a specific function execution is directly
responsible is the :arc-term:`formal local retain history` of the
object pointer.  The corresponding actual sequence executed is the
`dynamic local retain history`.

However, under certain circumstances, ARC is permitted to re-order and
eliminate operations in a manner which may alter the overall
computation history beyond what is permitted by the general "as if"
rule of C/C++ and the :ref:`restrictions <arc.objects.retains>` on
the implementation of ``retain`` and ``release``.

.. admonition:: Rationale

  Specifically, ARC is sometimes permitted to optimize ``release``
  operations in ways which might cause an object to be deallocated
  before it would otherwise be.  Without this, it would be almost
  impossible to eliminate any ``retain``/``release`` pairs.  For
  example, consider the following code:

  .. code-block:: objc

    id x = _ivar;
    [x foo];

  If we were not permitted in any event to shorten the lifetime of the
  object in ``x``, then we would not be able to eliminate this retain
  and release unless we could prove that the message send could not
  modify ``_ivar`` (or deallocate ``self``).  Since message sends are
  opaque to the optimizer, this is not possible, and so ARC's hands
  would be almost completely tied.

ARC makes no guarantees about the execution of a computation history
which contains undefined behavior.  In particular, ARC makes no
guarantees in the presence of race conditions.

ARC may assume that any retainable object pointers it receives or
generates are instantaneously valid from that point until a point
which, by the concurrency model of the host language, happens-after
the generation of the pointer and happens-before a release of that
object (possibly via an aliasing pointer or indirectly due to
destruction of a different object).

.. admonition:: Rationale

  There is very little point in trying to guarantee correctness in the
  presence of race conditions.  ARC does not have a stack-scanning
  garbage collector, and guaranteeing the atomicity of every load and
  store operation would be prohibitive and preclude a vast amount of
  optimization.

ARC may assume that non-ARC code engages in sensible balancing
behavior and does not rely on exact or minimum retain count values
except as guaranteed by ``__strong`` object invariants or +1 transfer
conventions.  For example, if an object is provably double-retained
and double-released, ARC may eliminate the inner retain and release;
it does not need to guard against code which performs an unbalanced
release followed by a "balancing" retain.

.. _arc.optimization.liveness:

Object liveness
---------------

ARC may not allow a retainable object ``X`` to be deallocated at a
time ``T`` in a computation history if:

* ``X`` is the value stored in a ``__strong`` object ``S`` with
  :ref:`precise lifetime semantics <arc.optimization.precise>`, or

* ``X`` is the value stored in a ``__strong`` object ``S`` with
  imprecise lifetime semantics and, at some point after ``T`` but
  before the next store to ``S``, the computation history features a
  load from ``S`` and in some way depends on the value loaded, or

* ``X`` is a value described as being released at the end of the
  current full-expression and, at some point after ``T`` but before
  the end of the full-expression, the computation history depends
  on that value.

.. admonition:: Rationale

  The intent of the second rule is to say that objects held in normal
  ``__strong`` local variables may be released as soon as the value in
  the variable is no longer being used: either the variable stops
  being used completely or a new value is stored in the variable.

  The intent of the third rule is to say that return values may be
  released after they've been used.

A computation history depends on a pointer value ``P`` if it:

* performs a pointer comparison with ``P``,
* loads from ``P``,
* stores to ``P``,
* depends on a pointer value ``Q`` derived via pointer arithmetic
  from ``P`` (including an instance-variable or field access), or
* depends on a pointer value ``Q`` loaded from ``P``.

Dependency applies only to values derived directly or indirectly from
a particular expression result and does not occur merely because a
separate pointer value dynamically aliases ``P``.  Furthermore, this
dependency is not carried by values that are stored to objects.

.. admonition:: Rationale

  The restrictions on dependency are intended to make this analysis
  feasible by an optimizer with only incomplete information about a
  program.  Essentially, dependence is carried to "obvious" uses of a
  pointer.  Merely passing a pointer argument to a function does not
  itself cause dependence, but since generally the optimizer will not
  be able to prove that the function doesn't depend on that parameter,
  it will be forced to conservatively assume it does.

  Dependency propagates to values loaded from a pointer because those
  values might be invalidated by deallocating the object.  For
  example, given the code ``__strong id x = p->ivar;``, ARC must not
  move the release of ``p`` to between the load of ``p->ivar`` and the
  retain of that value for storing into ``x``.

  Dependency does not propagate through stores of dependent pointer
  values because doing so would allow dependency to outlive the
  full-expression which produced the original value.  For example, the
  address of an instance variable could be written to some global
  location and then freely accessed during the lifetime of the local,
  or a function could return an inner pointer of an object and store
  it to a local.  These cases would be potentially impossible to
  reason about and so would basically prevent any optimizations based
  on imprecise lifetime.  There are also uncommon enough to make it
  reasonable to require the precise-lifetime annotation if someone
  really wants to rely on them.

  Dependency does propagate through return values of pointer type.
  The compelling source of need for this rule is a property accessor
  which returns an un-autoreleased result; the calling function must
  have the chance to operate on the value, e.g. to retain it, before
  ARC releases the original pointer.  Note again, however, that
  dependence does not survive a store, so ARC does not guarantee the
  continued validity of the return value past the end of the
  full-expression.

.. _arc.optimization.object_lifetime:

No object lifetime extension
----------------------------

If, in the formal computation history of the program, an object ``X``
has been deallocated by the time of an observable side-effect, then
ARC must cause ``X`` to be deallocated by no later than the occurrence
of that side-effect, except as influenced by the re-ordering of the
destruction of objects.

.. admonition:: Rationale

  This rule is intended to prohibit ARC from observably extending the
  lifetime of a retainable object, other than as specified in this
  document.  Together with the rule limiting the transformation of
  releases, this rule requires ARC to eliminate retains and release
  only in pairs.

  ARC's power to reorder the destruction of objects is critical to its
  ability to do any optimization, for essentially the same reason that
  it must retain the power to decrease the lifetime of an object.
  Unfortunately, while it's generally poor style for the destruction
  of objects to have arbitrary side-effects, it's certainly possible.
  Hence the caveat.

.. _arc.optimization.precise:

Precise lifetime semantics
--------------------------

In general, ARC maintains an invariant that a retainable object pointer held in
a ``__strong`` object will be retained for the full formal lifetime of the
object.  Objects subject to this invariant have :arc-term:`precise lifetime
semantics`.

By default, local variables of automatic storage duration do not have precise
lifetime semantics.  Such objects are simply strong references which hold
values of retainable object pointer type, and these values are still fully
subject to the optimizations on values under local control.

.. admonition:: Rationale

  Applying these precise-lifetime semantics strictly would be prohibitive.
  Many useful optimizations that might theoretically decrease the lifetime of
  an object would be rendered impossible.  Essentially, it promises too much.

A local variable of retainable object owner type and automatic storage duration
may be annotated with the ``objc_precise_lifetime`` attribute to indicate that
it should be considered to be an object with precise lifetime semantics.

.. admonition:: Rationale

  Nonetheless, it is sometimes useful to be able to force an object to be
  released at a precise time, even if that object does not appear to be used.
  This is likely to be uncommon enough that the syntactic weight of explicitly
  requesting these semantics will not be burdensome, and may even make the code
  clearer.

.. _arc.misc:

Miscellaneous
=============

.. _arc.misc.special_methods:

Special methods
---------------

.. _arc.misc.special_methods.retain:

Memory management methods
^^^^^^^^^^^^^^^^^^^^^^^^^

A program is ill-formed if it contains a method definition, message send, or
``@selector`` expression for any of the following selectors:

* ``autorelease``
* ``release``
* ``retain``
* ``retainCount``

.. admonition:: Rationale

  ``retainCount`` is banned because ARC robs it of consistent semantics.  The
  others were banned after weighing three options for how to deal with message
  sends:

  **Honoring** them would work out very poorly if a programmer naively or
  accidentally tried to incorporate code written for manual retain/release code
  into an ARC program.  At best, such code would do twice as much work as
  necessary; quite frequently, however, ARC and the explicit code would both
  try to balance the same retain, leading to crashes.  The cost is losing the
  ability to perform "unrooted" retains, i.e. retains not logically
  corresponding to a strong reference in the object graph.

  **Ignoring** them would badly violate user expectations about their code.
  While it *would* make it easier to develop code simultaneously for ARC and
  non-ARC, there is very little reason to do so except for certain library
  developers.  ARC and non-ARC translation units share an execution model and
  can seamlessly interoperate.  Within a translation unit, a developer who
  faithfully maintains their code in non-ARC mode is suffering all the
  restrictions of ARC for zero benefit, while a developer who isn't testing the
  non-ARC mode is likely to be unpleasantly surprised if they try to go back to
  it.

  **Banning** them has the disadvantage of making it very awkward to migrate
  existing code to ARC.  The best answer to that, given a number of other
  changes and restrictions in ARC, is to provide a specialized tool to assist
  users in that migration.

  Implementing these methods was banned because they are too integral to the
  semantics of ARC; many tricks which worked tolerably under manual reference
  counting will misbehave if ARC performs an ephemeral extra retain or two.  If
  absolutely required, it is still possible to implement them in non-ARC code,
  for example in a category; the implementations must obey the :ref:`semantics
  <arc.objects.retains>` laid out elsewhere in this document.

.. _arc.misc.special_methods.dealloc:

``dealloc``
^^^^^^^^^^^

A program is ill-formed if it contains a message send or ``@selector``
expression for the selector ``dealloc``.

.. admonition:: Rationale

  There are no legitimate reasons to call ``dealloc`` directly.

A class may provide a method definition for an instance method named
``dealloc``.  This method will be called after the final ``release`` of the
object but before it is deallocated or any of its instance variables are
destroyed.  The superclass's implementation of ``dealloc`` will be called
automatically when the method returns.

.. admonition:: Rationale

  Even though ARC destroys instance variables automatically, there are still
  legitimate reasons to write a ``dealloc`` method, such as freeing
  non-retainable resources.  Failing to call ``[super dealloc]`` in such a
  method is nearly always a bug.  Sometimes, the object is simply trying to
  prevent itself from being destroyed, but ``dealloc`` is really far too late
  for the object to be raising such objections.  Somewhat more legitimately, an
  object may have been pool-allocated and should not be deallocated with
  ``free``; for now, this can only be supported with a ``dealloc``
  implementation outside of ARC.  Such an implementation must be very careful
  to do all the other work that ``NSObject``'s ``dealloc`` would, which is
  outside the scope of this document to describe.

The instance variables for an ARC-compiled class will be destroyed at some
point after control enters the ``dealloc`` method for the root class of the
class.  The ordering of the destruction of instance variables is unspecified,
both within a single class and between subclasses and superclasses.

.. admonition:: Rationale

  The traditional, non-ARC pattern for destroying instance variables is to
  destroy them immediately before calling ``[super dealloc]``.  Unfortunately,
  message sends from the superclass are quite capable of reaching methods in
  the subclass, and those methods may well read or write to those instance
  variables.  Making such message sends from dealloc is generally discouraged,
  since the subclass may well rely on other invariants that were broken during
  ``dealloc``, but it's not so inescapably dangerous that we felt comfortable
  calling it undefined behavior.  Therefore we chose to delay destroying the
  instance variables to a point at which message sends are clearly disallowed:
  the point at which the root class's deallocation routines take over.

  In most code, the difference is not observable.  It can, however, be observed
  if an instance variable holds a strong reference to an object whose
  deallocation will trigger a side-effect which must be carefully ordered with
  respect to the destruction of the super class.  Such code violates the design
  principle that semantically important behavior should be explicit.  A simple
  fix is to clear the instance variable manually during ``dealloc``; a more
  holistic solution is to move semantically important side-effects out of
  ``dealloc`` and into a separate teardown phase which can rely on working with
  well-formed objects.

.. _arc.misc.autoreleasepool:

``@autoreleasepool``
--------------------

To simplify the use of autorelease pools, and to bring them under the control
of the compiler, a new kind of statement is available in Objective-C.  It is
written ``@autoreleasepool`` followed by a *compound-statement*, i.e.  by a new
scope delimited by curly braces.  Upon entry to this block, the current state
of the autorelease pool is captured.  When the block is exited normally,
whether by fallthrough or directed control flow (such as ``return`` or
``break``), the autorelease pool is restored to the saved state, releasing all
the objects in it.  When the block is exited with an exception, the pool is not
drained.

``@autoreleasepool`` may be used in non-ARC translation units, with equivalent
semantics.

A program is ill-formed if it refers to the ``NSAutoreleasePool`` class.

.. admonition:: Rationale

  Autorelease pools are clearly important for the compiler to reason about, but
  it is far too much to expect the compiler to accurately reason about control
  dependencies between two calls.  It is also very easy to accidentally forget
  to drain an autorelease pool when using the manual API, and this can
  significantly inflate the process's high-water-mark.  The introduction of a
  new scope is unfortunate but basically required for sane interaction with the
  rest of the language.  Not draining the pool during an unwind is apparently
  required by the Objective-C exceptions implementation.

.. _arc.misc.externally_retained:

Externally-Retained Variables
-----------------------------

In some situations, variables with strong ownership are considered
externally-retained by the implementation. This means that the variable is
retained elsewhere, and therefore the implementation can elide retaining and
releasing its value. Such a variable is implicitly ``const`` for safety. In
contrast with ``__unsafe_unretained``, an externally-retained variable still
behaves as a strong variable outside of initialization and destruction. For
instance, when an externally-retained variable is captured in a block the value
of the variable is retained and released on block capture and destruction. It
also affects C++ features such as lambda capture, ``decltype``, and template
argument deduction.

Implicitly, the implementation assumes that the :ref:`self parameter in a
non-init method <arc.misc.self>` and the :ref:`variable in a for-in loop
<arc.misc.enumeration>` are externally-retained.

Externally-retained semantics can also be opted into with the
``objc_externally_retained`` attribute. This attribute can apply to strong local
variables, functions, methods, or blocks:

.. code-block:: objc

  @class WobbleAmount;

  @interface Widget : NSObject
  -(void)wobble:(WobbleAmount *)amount;
  @end

  @implementation Widget

  -(void)wobble:(WobbleAmount *)amount
           __attribute__((objc_externally_retained)) {
    // 'amount' and 'alias' aren't retained on entry, nor released on exit.
    __attribute__((objc_externally_retained)) WobbleAmount *alias = amount;
  }
  @end

Annotating a function with this attribute makes every parameter with strong
retainable object pointer type externally-retained, unless the variable was
explicitly qualified with ``__strong``. For instance, ``first_param`` is
externally-retained (and therefore ``const``) below, but not ``second_param``:

.. code-block:: objc

  __attribute__((objc_externally_retained))
  void f(NSArray *first_param, __strong NSArray *second_param) {
    // ...
  }

You can test if your compiler has support for ``objc_externally_retained`` with
``__has_attribute``:

.. code-block:: objc

  #if __has_attribute(objc_externally_retained)
  // Use externally retained...
  #endif

.. _arc.misc.self:

``self``
--------

The ``self`` parameter variable of an non-init Objective-C method is considered
:ref:`externally-retained <arc.misc.externally_retained>` by the implementation.
It is undefined behavior, or at least dangerous, to cause an object to be
deallocated during a message send to that object.  In an init method, ``self``
follows the :ref:``init family rules <arc.family.semantics.init>``.

.. admonition:: Rationale

  The cost of retaining ``self`` in all methods was found to be prohibitive, as
  it tends to be live across calls, preventing the optimizer from proving that
  the retain and release are unnecessary --- for good reason, as it's quite
  possible in theory to cause an object to be deallocated during its execution
  without this retain and release.  Since it's extremely uncommon to actually
  do so, even unintentionally, and since there's no natural way for the
  programmer to remove this retain/release pair otherwise (as there is for
  other parameters by, say, making the variable ``objc_externally_retained`` or
  qualifying it with ``__unsafe_unretained``), we chose to make this optimizing
  assumption and shift some amount of risk to the user.

.. _arc.misc.enumeration:

Fast enumeration iteration variables
------------------------------------

If a variable is declared in the condition of an Objective-C fast enumeration
loop, and the variable has no explicit ownership qualifier, then it is
implicitly :ref:`externally-retained <arc.misc.externally_retained>` so that
objects encountered during the enumeration are not actually retained and
released.

.. admonition:: Rationale

  This is an optimization made possible because fast enumeration loops promise
  to keep the objects retained during enumeration, and the collection itself
  cannot be synchronously modified.  It can be overridden by explicitly
  qualifying the variable with ``__strong``, which will make the variable
  mutable again and cause the loop to retain the objects it encounters.

.. _arc.misc.blocks:

Blocks
------

The implicit ``const`` capture variables created when evaluating a block
literal expression have the same ownership semantics as the local variables
they capture.  The capture is performed by reading from the captured variable
and initializing the capture variable with that value; the capture variable is
destroyed when the block literal is, i.e. at the end of the enclosing scope.

The :ref:`inference <arc.ownership.inference>` rules apply equally to
``__block`` variables, which is a shift in semantics from non-ARC, where
``__block`` variables did not implicitly retain during capture.

``__block`` variables of retainable object owner type are moved off the stack
by initializing the heap copy with the result of moving from the stack copy.

With the exception of retains done as part of initializing a ``__strong``
parameter variable or reading a ``__weak`` variable, whenever these semantics
call for retaining a value of block-pointer type, it has the effect of a
``Block_copy``.  The optimizer may remove such copies when it sees that the
result is used only as an argument to a call.

When a block pointer type is converted to a non-block pointer type (such as
``id``), ``Block_copy`` is called. This is necessary because a block allocated
on the stack won't get copied to the heap when the non-block pointer escapes.
A block pointer is implicitly converted to ``id`` when it is passed to a
function as a variadic argument.

.. _arc.misc.exceptions:

Exceptions
----------

By default in Objective C, ARC is not exception-safe for normal releases:

* It does not end the lifetime of ``__strong`` variables when their scopes are
  abnormally terminated by an exception.
* It does not perform releases which would occur at the end of a
  full-expression if that full-expression throws an exception.

A program may be compiled with the option ``-fobjc-arc-exceptions`` in order to
enable these, or with the option ``-fno-objc-arc-exceptions`` to explicitly
disable them, with the last such argument "winning".

.. admonition:: Rationale

  The standard Cocoa convention is that exceptions signal programmer error and
  are not intended to be recovered from.  Making code exceptions-safe by
  default would impose severe runtime and code size penalties on code that
  typically does not actually care about exceptions safety.  Therefore,
  ARC-generated code leaks by default on exceptions, which is just fine if the
  process is going to be immediately terminated anyway.  Programs which do care
  about recovering from exceptions should enable the option.

In Objective-C++, ``-fobjc-arc-exceptions`` is enabled by default.

.. admonition:: Rationale

  C++ already introduces pervasive exceptions-cleanup code of the sort that ARC
  introduces.  C++ programmers who have not already disabled exceptions are
  much more likely to actual require exception-safety.

ARC does end the lifetimes of ``__weak`` objects when an exception terminates
their scope unless exceptions are disabled in the compiler.

.. admonition:: Rationale

  The consequence of a local ``__weak`` object not being destroyed is very
  likely to be corruption of the Objective-C runtime, so we want to be safer
  here.  Of course, potentially massive leaks are about as likely to take down
  the process as this corruption is if the program does try to recover from
  exceptions.

.. _arc.misc.interior:

Interior pointers
-----------------

An Objective-C method returning a non-retainable pointer may be annotated with
the ``objc_returns_inner_pointer`` attribute to indicate that it returns a
handle to the internal data of an object, and that this reference will be
invalidated if the object is destroyed.  When such a message is sent to an
object, the object's lifetime will be extended until at least the earliest of:

* the last use of the returned pointer, or any pointer derived from it, in the
  calling function or
* the autorelease pool is restored to a previous state.

.. admonition:: Rationale

  Rationale: not all memory and resources are managed with reference counts; it
  is common for objects to manage private resources in their own, private way.
  Typically these resources are completely encapsulated within the object, but
  some classes offer their users direct access for efficiency.  If ARC is not
  aware of methods that return such "interior" pointers, its optimizations can
  cause the owning object to be reclaimed too soon.  This attribute informs ARC
  that it must tread lightly.

  The extension rules are somewhat intentionally vague.  The autorelease pool
  limit is there to permit a simple implementation to simply retain and
  autorelease the receiver.  The other limit permits some amount of
  optimization.  The phrase "derived from" is intended to encompass the results
  both of pointer transformations, such as casts and arithmetic, and of loading
  from such derived pointers; furthermore, it applies whether or not such
  derivations are applied directly in the calling code or by other utility code
  (for example, the C library routine ``strchr``).  However, the implementation
  never need account for uses after a return from the code which calls the
  method returning an interior pointer.

As an exception, no extension is required if the receiver is loaded directly
from a ``__strong`` object with :ref:`precise lifetime semantics
<arc.optimization.precise>`.

.. admonition:: Rationale

  Implicit autoreleases carry the risk of significantly inflating memory use,
  so it's important to provide users a way of avoiding these autoreleases.
  Tying this to precise lifetime semantics is ideal, as for local variables
  this requires a very explicit annotation, which allows ARC to trust the user
  with good cheer.

.. _arc.misc.c-retainable:

C retainable pointer types
--------------------------

A type is a :arc-term:`C retainable pointer type` if it is a pointer to
(possibly qualified) ``void`` or a pointer to a (possibly qualifier) ``struct``
or ``class`` type.

.. admonition:: Rationale

  ARC does not manage pointers of CoreFoundation type (or any of the related
  families of retainable C pointers which interoperate with Objective-C for
  retain/release operation).  In fact, ARC does not even know how to
  distinguish these types from arbitrary C pointer types.  The intent of this
  concept is to filter out some obviously non-object types while leaving a hook
  for later tightening if a means of exhaustively marking CF types is made
  available.

.. _arc.misc.c-retainable.audit:

Auditing of C retainable pointer interfaces
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:when-revised:`[beginning Apple 4.0, LLVM 3.1]`

A C function may be marked with the ``cf_audited_transfer`` attribute to
express that, except as otherwise marked with attributes, it obeys the
parameter (consuming vs. non-consuming) and return (retained vs. non-retained)
conventions for a C function of its name, namely:

* A parameter of C retainable pointer type is assumed to not be consumed
  unless it is marked with the ``cf_consumed`` attribute, and
* A result of C retainable pointer type is assumed to not be returned retained
  unless the function is either marked ``cf_returns_retained`` or it follows
  the create/copy naming convention and is not marked
  ``cf_returns_not_retained``.

A function obeys the :arc-term:`create/copy` naming convention if its name
contains as a substring:

* either "Create" or "Copy" not followed by a lowercase letter, or
* either "create" or "copy" not followed by a lowercase letter and
  not preceded by any letter, whether uppercase or lowercase.

A second attribute, ``cf_unknown_transfer``, signifies that a function's
transfer semantics cannot be accurately captured using any of these
annotations.  A program is ill-formed if it annotates the same function with
both ``cf_audited_transfer`` and ``cf_unknown_transfer``.

A pragma is provided to facilitate the mass annotation of interfaces:

.. code-block:: objc

  #pragma clang arc_cf_code_audited begin
  ...
  #pragma clang arc_cf_code_audited end

All C functions declared within the extent of this pragma are treated as if
annotated with the ``cf_audited_transfer`` attribute unless they otherwise have
the ``cf_unknown_transfer`` attribute.  The pragma is accepted in all language
modes.  A program is ill-formed if it attempts to change files, whether by
including a file or ending the current file, within the extent of this pragma.

It is possible to test for all the features in this section with
``__has_feature(arc_cf_code_audited)``.

.. admonition:: Rationale

  A significant inconvenience in ARC programming is the necessity of
  interacting with APIs based around C retainable pointers.  These features are
  designed to make it relatively easy for API authors to quickly review and
  annotate their interfaces, in turn improving the fidelity of tools such as
  the static analyzer and ARC.  The single-file restriction on the pragma is
  designed to eliminate the risk of accidentally annotating some other header's
  interfaces.

.. _arc.runtime:

Runtime support
===============

This section describes the interaction between the ARC runtime and the code
generated by the ARC compiler.  This is not part of the ARC language
specification; instead, it is effectively a language-specific ABI supplement,
akin to the "Itanium" generic ABI for C++.

Ownership qualification does not alter the storage requirements for objects,
except that it is undefined behavior if a ``__weak`` object is inadequately
aligned for an object of type ``id``.  The other qualifiers may be used on
explicitly under-aligned memory.

The runtime tracks ``__weak`` objects which holds non-null values.  It is
undefined behavior to direct modify a ``__weak`` object which is being tracked
by the runtime except through an
:ref:`objc_storeWeak <arc.runtime.objc_storeWeak>`,
:ref:`objc_destroyWeak <arc.runtime.objc_destroyWeak>`, or
:ref:`objc_moveWeak <arc.runtime.objc_moveWeak>` call.

The runtime must provide a number of new entrypoints which the compiler may
emit, which are described in the remainder of this section.

.. admonition:: Rationale

  Several of these functions are semantically equivalent to a message send; we
  emit calls to C functions instead because:

  * the machine code to do so is significantly smaller,
  * it is much easier to recognize the C functions in the ARC optimizer, and
  * a sufficient sophisticated runtime may be able to avoid the message send in
    common cases.

  Several other of these functions are "fused" operations which can be
  described entirely in terms of other operations.  We use the fused operations
  primarily as a code-size optimization, although in some cases there is also a
  real potential for avoiding redundant operations in the runtime.

.. _arc.runtime.objc_autorelease:

``id objc_autorelease(id value);``
----------------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it adds the object
to the innermost autorelease pool exactly as if the object had been sent the
``autorelease`` message.

Always returns ``value``.

.. _arc.runtime.objc_autoreleasePoolPop:

``void objc_autoreleasePoolPop(void *pool);``
---------------------------------------------

*Precondition:* ``pool`` is the result of a previous call to
:ref:`objc_autoreleasePoolPush <arc.runtime.objc_autoreleasePoolPush>` on the
current thread, where neither ``pool`` nor any enclosing pool have previously
been popped.

Releases all the objects added to the given autorelease pool and any
autorelease pools it encloses, then sets the current autorelease pool to the
pool directly enclosing ``pool``.

.. _arc.runtime.objc_autoreleasePoolPush:

``void *objc_autoreleasePoolPush(void);``
-----------------------------------------

Creates a new autorelease pool that is enclosed by the current pool, makes that
the current pool, and returns an opaque "handle" to it.

.. admonition:: Rationale

  While the interface is described as an explicit hierarchy of pools, the rules
  allow the implementation to just keep a stack of objects, using the stack
  depth as the opaque pool handle.

.. _arc.runtime.objc_autoreleaseReturnValue:

``id objc_autoreleaseReturnValue(id value);``
---------------------------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it makes a best
effort to hand off ownership of a retain count on the object to a call to
:ref:`objc_retainAutoreleasedReturnValue
<arc.runtime.objc_retainAutoreleasedReturnValue>` (or
:ref:`objc_unsafeClaimAutoreleasedReturnValue
<arc.runtime.objc_unsafeClaimAutoreleasedReturnValue>`) for the same object in
an enclosing call frame.  If this is not possible, the object is autoreleased as
above.

Always returns ``value``.

.. _arc.runtime.objc_copyWeak:

``void objc_copyWeak(id *dest, id *src);``
------------------------------------------

*Precondition:* ``src`` is a valid pointer which either contains a null pointer
or has been registered as a ``__weak`` object.  ``dest`` is a valid pointer
which has not been registered as a ``__weak`` object.

``dest`` is initialized to be equivalent to ``src``, potentially registering it
with the runtime.  Equivalent to the following code:

.. code-block:: objc

  void objc_copyWeak(id *dest, id *src) {
    objc_release(objc_initWeak(dest, objc_loadWeakRetained(src)));
  }

Must be atomic with respect to calls to ``objc_storeWeak`` on ``src``.

.. _arc.runtime.objc_destroyWeak:

``void objc_destroyWeak(id *object);``
--------------------------------------

*Precondition:* ``object`` is a valid pointer which either contains a null
pointer or has been registered as a ``__weak`` object.

``object`` is unregistered as a weak object, if it ever was.  The current value
of ``object`` is left unspecified; otherwise, equivalent to the following code:

.. code-block:: objc

  void objc_destroyWeak(id *object) {
    objc_storeWeak(object, nil);
  }

Does not need to be atomic with respect to calls to ``objc_storeWeak`` on
``object``.

.. _arc.runtime.objc_initWeak:

``id objc_initWeak(id *object, id value);``
-------------------------------------------

*Precondition:* ``object`` is a valid pointer which has not been registered as
a ``__weak`` object.  ``value`` is null or a pointer to a valid object.

If ``value`` is a null pointer or the object to which it points has begun
deallocation, ``object`` is zero-initialized.  Otherwise, ``object`` is
registered as a ``__weak`` object pointing to ``value``.  Equivalent to the
following code:

.. code-block:: objc

  id objc_initWeak(id *object, id value) {
    *object = nil;
    return objc_storeWeak(object, value);
  }

Returns the value of ``object`` after the call.

Does not need to be atomic with respect to calls to ``objc_storeWeak`` on
``object``.

.. _arc.runtime.objc_loadWeak:

``id objc_loadWeak(id *object);``
---------------------------------

*Precondition:* ``object`` is a valid pointer which either contains a null
pointer or has been registered as a ``__weak`` object.

If ``object`` is registered as a ``__weak`` object, and the last value stored
into ``object`` has not yet been deallocated or begun deallocation, retains and
autoreleases that value and returns it.  Otherwise returns null.  Equivalent to
the following code:

.. code-block:: objc

  id objc_loadWeak(id *object) {
    return objc_autorelease(objc_loadWeakRetained(object));
  }

Must be atomic with respect to calls to ``objc_storeWeak`` on ``object``.

.. admonition:: Rationale

  Loading weak references would be inherently prone to race conditions without
  the retain.

.. _arc.runtime.objc_loadWeakRetained:

``id objc_loadWeakRetained(id *object);``
-----------------------------------------

*Precondition:* ``object`` is a valid pointer which either contains a null
pointer or has been registered as a ``__weak`` object.

If ``object`` is registered as a ``__weak`` object, and the last value stored
into ``object`` has not yet been deallocated or begun deallocation, retains
that value and returns it.  Otherwise returns null.

Must be atomic with respect to calls to ``objc_storeWeak`` on ``object``.

.. _arc.runtime.objc_moveWeak:

``void objc_moveWeak(id *dest, id *src);``
------------------------------------------

*Precondition:* ``src`` is a valid pointer which either contains a null pointer
or has been registered as a ``__weak`` object.  ``dest`` is a valid pointer
which has not been registered as a ``__weak`` object.

``dest`` is initialized to be equivalent to ``src``, potentially registering it
with the runtime.  ``src`` may then be left in its original state, in which
case this call is equivalent to :ref:`objc_copyWeak
<arc.runtime.objc_copyWeak>`, or it may be left as null.

Must be atomic with respect to calls to ``objc_storeWeak`` on ``src``.

.. _arc.runtime.objc_release:

``void objc_release(id value);``
--------------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it performs a
release operation exactly as if the object had been sent the ``release``
message.

.. _arc.runtime.objc_retain:

``id objc_retain(id value);``
-----------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it performs a retain
operation exactly as if the object had been sent the ``retain`` message.

Always returns ``value``.

.. _arc.runtime.objc_retainAutorelease:

``id objc_retainAutorelease(id value);``
----------------------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it performs a retain
operation followed by an autorelease operation.  Equivalent to the following
code:

.. code-block:: objc

  id objc_retainAutorelease(id value) {
    return objc_autorelease(objc_retain(value));
  }

Always returns ``value``.

.. _arc.runtime.objc_retainAutoreleaseReturnValue:

``id objc_retainAutoreleaseReturnValue(id value);``
---------------------------------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it performs a retain
operation followed by the operation described in
:ref:`objc_autoreleaseReturnValue <arc.runtime.objc_autoreleaseReturnValue>`.
Equivalent to the following code:

.. code-block:: objc

  id objc_retainAutoreleaseReturnValue(id value) {
    return objc_autoreleaseReturnValue(objc_retain(value));
  }

Always returns ``value``.

.. _arc.runtime.objc_retainAutoreleasedReturnValue:

``id objc_retainAutoreleasedReturnValue(id value);``
----------------------------------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it attempts to
accept a hand off of a retain count from a call to
:ref:`objc_autoreleaseReturnValue <arc.runtime.objc_autoreleaseReturnValue>` on
``value`` in a recently-called function or something it tail-calls.  If that
fails, it performs a retain operation exactly like :ref:`objc_retain
<arc.runtime.objc_retain>`.

Always returns ``value``.

.. _arc.runtime.objc_retainBlock:

``id objc_retainBlock(id value);``
----------------------------------

*Precondition:* ``value`` is null or a pointer to a valid block object.

If ``value`` is null, this call has no effect.  Otherwise, if the block pointed
to by ``value`` is still on the stack, it is copied to the heap and the address
of the copy is returned.  Otherwise a retain operation is performed on the
block exactly as if it had been sent the ``retain`` message.

.. _arc.runtime.objc_storeStrong:

``void objc_storeStrong(id *object, id value);``
------------------------------------------------

*Precondition:* ``object`` is a valid pointer to a ``__strong`` object which is
adequately aligned for a pointer.  ``value`` is null or a pointer to a valid
object.

Performs the complete sequence for assigning to a ``__strong`` object of
non-block type [*]_.  Equivalent to the following code:

.. code-block:: objc

  void objc_storeStrong(id *object, id value) {
    id oldValue = *object;
    value = [value retain];
    *object = value;
    [oldValue release];
  }

.. [*] This does not imply that a ``__strong`` object of block type is an
   invalid argument to this function. Rather it implies that an ``objc_retain``
   and not an ``objc_retainBlock`` operation will be emitted if the argument is
   a block.

.. _arc.runtime.objc_storeWeak:

``id objc_storeWeak(id *object, id value);``
--------------------------------------------

*Precondition:* ``object`` is a valid pointer which either contains a null
pointer or has been registered as a ``__weak`` object.  ``value`` is null or a
pointer to a valid object.

If ``value`` is a null pointer or the object to which it points has begun
deallocation, ``object`` is assigned null and unregistered as a ``__weak``
object.  Otherwise, ``object`` is registered as a ``__weak`` object or has its
registration updated to point to ``value``.

Returns the value of ``object`` after the call.

.. _arc.runtime.objc_unsafeClaimAutoreleasedReturnValue:

``id objc_unsafeClaimAutoreleasedReturnValue(id value);``
---------------------------------------------------------

*Precondition:* ``value`` is null or a pointer to a valid object.

If ``value`` is null, this call has no effect.  Otherwise, it attempts to
accept a hand off of a retain count from a call to
:ref:`objc_autoreleaseReturnValue <arc.runtime.objc_autoreleaseReturnValue>` on
``value`` in a recently-called function or something it tail-calls (in a manner
similar to :ref:`objc_retainAutoreleasedReturnValue
<arc.runtime.objc_retainAutoreleasedReturnValue>`).  If that succeeds,
it performs a release operation exactly like :ref:`objc_release
<arc.runtime.objc_release>`.  If the handoff fails, this call has no effect.

Always returns ``value``.

