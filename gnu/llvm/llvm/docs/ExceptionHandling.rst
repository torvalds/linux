==========================
Exception Handling in LLVM
==========================

.. contents::
   :local:

Introduction
============

This document is the central repository for all information pertaining to
exception handling in LLVM.  It describes the format that LLVM exception
handling information takes, which is useful for those interested in creating
front-ends or dealing directly with the information.  Further, this document
provides specific examples of what exception handling information is used for in
C and C++.

Itanium ABI Zero-cost Exception Handling
----------------------------------------

Exception handling for most programming languages is designed to recover from
conditions that rarely occur during general use of an application.  To that end,
exception handling should not interfere with the main flow of an application's
algorithm by performing checkpointing tasks, such as saving the current pc or
register state.

The Itanium ABI Exception Handling Specification defines a methodology for
providing outlying data in the form of exception tables without inlining
speculative exception handling code in the flow of an application's main
algorithm.  Thus, the specification is said to add "zero-cost" to the normal
execution of an application.

A more complete description of the Itanium ABI exception handling runtime
support of can be found at `Itanium C++ ABI: Exception Handling
<http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html>`_. A description of the
exception frame format can be found at `Exception Frames
<http://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html>`_,
with details of the DWARF 4 specification at `DWARF 4 Standard
<http://dwarfstd.org/Dwarf4Std.php>`_.  A description for the C++ exception
table formats can be found at `Exception Handling Tables
<http://itanium-cxx-abi.github.io/cxx-abi/exceptions.pdf>`_.

Setjmp/Longjmp Exception Handling
---------------------------------

Setjmp/Longjmp (SJLJ) based exception handling uses LLVM intrinsics
`llvm.eh.sjlj.setjmp`_ and `llvm.eh.sjlj.longjmp`_ to handle control flow for
exception handling.

For each function which does exception processing --- be it ``try``/``catch``
blocks or cleanups --- that function registers itself on a global frame
list. When exceptions are unwinding, the runtime uses this list to identify
which functions need processing.

Landing pad selection is encoded in the call site entry of the function
context. The runtime returns to the function via `llvm.eh.sjlj.longjmp`_, where
a switch table transfers control to the appropriate landing pad based on the
index stored in the function context.

In contrast to DWARF exception handling, which encodes exception regions and
frame information in out-of-line tables, SJLJ exception handling builds and
removes the unwind frame context at runtime. This results in faster exception
handling at the expense of slower execution when no exceptions are thrown. As
exceptions are, by their nature, intended for uncommon code paths, DWARF
exception handling is generally preferred to SJLJ.

Windows Runtime Exception Handling
-----------------------------------

LLVM supports handling exceptions produced by the Windows runtime, but it
requires a very different intermediate representation. It is not based on the
":ref:`landingpad <i_landingpad>`" instruction like the other two models, and is
described later in this document under :ref:`wineh`.

Overview
--------

When an exception is thrown in LLVM code, the runtime does its best to find a
handler suited to processing the circumstance.

The runtime first attempts to find an *exception frame* corresponding to the
function where the exception was thrown.  If the programming language supports
exception handling (e.g. C++), the exception frame contains a reference to an
exception table describing how to process the exception.  If the language does
not support exception handling (e.g. C), or if the exception needs to be
forwarded to a prior activation, the exception frame contains information about
how to unwind the current activation and restore the state of the prior
activation.  This process is repeated until the exception is handled. If the
exception is not handled and no activations remain, then the application is
terminated with an appropriate error message.

Because different programming languages have different behaviors when handling
exceptions, the exception handling ABI provides a mechanism for
supplying *personalities*. An exception handling personality is defined by
way of a *personality function* (e.g. ``__gxx_personality_v0`` in C++),
which receives the context of the exception, an *exception structure*
containing the exception object type and value, and a reference to the exception
table for the current function.  The personality function for the current
compile unit is specified in a *common exception frame*.

The organization of an exception table is language dependent. For C++, an
exception table is organized as a series of code ranges defining what to do if
an exception occurs in that range. Typically, the information associated with a
range defines which types of exception objects (using C++ *type info*) that are
handled in that range, and an associated action that should take place. Actions
typically pass control to a *landing pad*.

A landing pad corresponds roughly to the code found in the ``catch`` portion of
a ``try``/``catch`` sequence. When execution resumes at a landing pad, it
receives an *exception structure* and a *selector value* corresponding to the
*type* of exception thrown. The selector is then used to determine which *catch*
should actually process the exception.

LLVM Code Generation
====================

From a C++ developer's perspective, exceptions are defined in terms of the
``throw`` and ``try``/``catch`` statements. In this section we will describe the
implementation of LLVM exception handling in terms of C++ examples.

Throw
-----

Languages that support exception handling typically provide a ``throw``
operation to initiate the exception process. Internally, a ``throw`` operation
breaks down into two steps.

#. A request is made to allocate exception space for an exception structure.
   This structure needs to survive beyond the current activation. This structure
   will contain the type and value of the object being thrown.

#. A call is made to the runtime to raise the exception, passing the exception
   structure as an argument.

In C++, the allocation of the exception structure is done by the
``__cxa_allocate_exception`` runtime function. The exception raising is handled
by ``__cxa_throw``. The type of the exception is represented using a C++ RTTI
structure.

Try/Catch
---------

A call within the scope of a *try* statement can potentially raise an
exception. In those circumstances, the LLVM C++ front-end replaces the call with
an ``invoke`` instruction. Unlike a call, the ``invoke`` has two potential
continuation points:

#. where to continue when the call succeeds as per normal, and

#. where to continue if the call raises an exception, either by a throw or the
   unwinding of a throw

The term used to define the place where an ``invoke`` continues after an
exception is called a *landing pad*. LLVM landing pads are conceptually
alternative function entry points where an exception structure reference and a
type info index are passed in as arguments. The landing pad saves the exception
structure reference and then proceeds to select the catch block that corresponds
to the type info of the exception object.

The LLVM :ref:`i_landingpad` is used to convey information about the landing
pad to the back end. For C++, the ``landingpad`` instruction returns a pointer
and integer pair corresponding to the pointer to the *exception structure* and
the *selector value* respectively.

The ``landingpad`` instruction looks for a reference to the personality
function to be used for this ``try``/``catch`` sequence in the parent
function's attribute list. The instruction contains a list of *cleanup*,
*catch*, and *filter* clauses. The exception is tested against the clauses
sequentially from first to last. The clauses have the following meanings:

-  ``catch <type> @ExcType``

   - This clause means that the landingpad block should be entered if the
     exception being thrown is of type ``@ExcType`` or a subtype of
     ``@ExcType``. For C++, ``@ExcType`` is a pointer to the ``std::type_info``
     object (an RTTI object) representing the C++ exception type.

   - If ``@ExcType`` is ``null``, any exception matches, so the landingpad
     should always be entered. This is used for C++ catch-all blocks ("``catch
     (...)``").

   - When this clause is matched, the selector value will be equal to the value
     returned by "``@llvm.eh.typeid.for(i8* @ExcType)``". This will always be a
     positive value.

-  ``filter <type> [<type> @ExcType1, ..., <type> @ExcTypeN]``

   - This clause means that the landingpad should be entered if the exception
     being thrown does *not* match any of the types in the list (which, for C++,
     are again specified as ``std::type_info`` pointers).

   - C++ front-ends use this to implement the C++ exception specifications, such as
     "``void foo() throw (ExcType1, ..., ExcTypeN) { ... }``". (Note: this
     functionality was deprecated in C++11 and removed in C++17.)

   - When this clause is matched, the selector value will be negative.

   - The array argument to ``filter`` may be empty; for example, "``[0 x i8**]
     undef``". This means that the landingpad should always be entered. (Note
     that such a ``filter`` would not be equivalent to "``catch i8* null``",
     because ``filter`` and ``catch`` produce negative and positive selector
     values respectively.)

-  ``cleanup``

   - This clause means that the landingpad should always be entered.

   - C++ front-ends use this for calling objects' destructors.

   - When this clause is matched, the selector value will be zero.

   - The runtime may treat "``cleanup``" differently from "``catch <type>
     null``".

     In C++, if an unhandled exception occurs, the language runtime will call
     ``std::terminate()``, but it is implementation-defined whether the runtime
     unwinds the stack and calls object destructors first. For example, the GNU
     C++ unwinder does not call object destructors when an unhandled exception
     occurs. The reason for this is to improve debuggability: it ensures that
     ``std::terminate()`` is called from the context of the ``throw``, so that
     this context is not lost by unwinding the stack. A runtime will typically
     implement this by searching for a matching non-``cleanup`` clause, and
     aborting if it does not find one, before entering any landingpad blocks.

Once the landing pad has the type info selector, the code branches to the code
for the first catch. The catch then checks the value of the type info selector
against the index of type info for that catch.  Since the type info index is not
known until all the type infos have been gathered in the backend, the catch code
must call the `llvm.eh.typeid.for`_ intrinsic to determine the index for a given
type info. If the catch fails to match the selector then control is passed on to
the next catch.

Finally, the entry and exit of catch code is bracketed with calls to
``__cxa_begin_catch`` and ``__cxa_end_catch``.

* ``__cxa_begin_catch`` takes an exception structure reference as an argument
  and returns the value of the exception object.

* ``__cxa_end_catch`` takes no arguments. This function:

  #. Locates the most recently caught exception and decrements its handler
     count,

  #. Removes the exception from the *caught* stack if the handler count goes to
     zero, and

  #. Destroys the exception if the handler count goes to zero and the exception
     was not re-thrown by throw.

  .. note::

    a rethrow from within the catch may replace this call with a
    ``__cxa_rethrow``.

Cleanups
--------

A cleanup is extra code which needs to be run as part of unwinding a scope.  C++
destructors are a typical example, but other languages and language extensions
provide a variety of different kinds of cleanups. In general, a landing pad may
need to run arbitrary amounts of cleanup code before actually entering a catch
block. To indicate the presence of cleanups, a :ref:`i_landingpad` should have
a *cleanup* clause.  Otherwise, the unwinder will not stop at the landing pad if
there are no catches or filters that require it to.

.. note::

  Do not allow a new exception to propagate out of the execution of a
  cleanup. This can corrupt the internal state of the unwinder.  Different
  languages describe different high-level semantics for these situations: for
  example, C++ requires that the process be terminated, whereas Ada cancels both
  exceptions and throws a third.

When all cleanups are finished, if the exception is not handled by the current
function, resume unwinding by calling the :ref:`resume instruction <i_resume>`,
passing in the result of the ``landingpad`` instruction for the original
landing pad.

Throw Filters
-------------

Prior to C++17, C++ allowed the specification of which exception types may be
thrown from a function. To represent this, a top level landing pad may exist to
filter out invalid types. To express this in LLVM code the :ref:`i_landingpad`
will have a filter clause. The clause consists of an array of type infos.
``landingpad`` will return a negative value
if the exception does not match any of the type infos. If no match is found then
a call to ``__cxa_call_unexpected`` should be made, otherwise
``_Unwind_Resume``.  Each of these functions requires a reference to the
exception structure.  Note that the most general form of a ``landingpad``
instruction can have any number of catch, cleanup, and filter clauses (though
having more than one cleanup is pointless). The LLVM C++ front-end can generate
such ``landingpad`` instructions due to inlining creating nested exception
handling scopes.

Restrictions
------------

The unwinder delegates the decision of whether to stop in a call frame to that
call frame's language-specific personality function. Not all unwinders guarantee
that they will stop to perform cleanups. For example, the GNU C++ unwinder
doesn't do so unless the exception is actually caught somewhere further up the
stack.

In order for inlining to behave correctly, landing pads must be prepared to
handle selector results that they did not originally advertise. Suppose that a
function catches exceptions of type ``A``, and it's inlined into a function that
catches exceptions of type ``B``. The inliner will update the ``landingpad``
instruction for the inlined landing pad to include the fact that ``B`` is also
caught. If that landing pad assumes that it will only be entered to catch an
``A``, it's in for a rude awakening.  Consequently, landing pads must test for
the selector results they understand and then resume exception propagation with
the `resume instruction <LangRef.html#i_resume>`_ if none of the conditions
match.

Exception Handling Intrinsics
=============================

In addition to the ``landingpad`` and ``resume`` instructions, LLVM uses several
intrinsic functions (name prefixed with ``llvm.eh``) to provide exception
handling information at various points in generated code.

.. _llvm.eh.typeid.for:

``llvm.eh.typeid.for``
----------------------

.. code-block:: llvm

  i32 @llvm.eh.typeid.for(i8* %type_info)


This intrinsic returns the type info index in the exception table of the current
function.  This value can be used to compare against the result of
``landingpad`` instruction.  The single argument is a reference to a type info.

Uses of this intrinsic are generated by the C++ front-end.

.. _llvm.eh.exceptionpointer:

``llvm.eh.exceptionpointer``
----------------------------

.. code-block:: text

  i8 addrspace(N)* @llvm.eh.padparam.pNi8(token %catchpad)


This intrinsic retrieves a pointer to the exception caught by the given
``catchpad``.


SJLJ Intrinsics
---------------

The ``llvm.eh.sjlj`` intrinsics are used internally within LLVM's
backend.  Uses of them are generated by the backend's
``SjLjEHPrepare`` pass.

.. _llvm.eh.sjlj.setjmp:

``llvm.eh.sjlj.setjmp``
~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: text

  i32 @llvm.eh.sjlj.setjmp(i8* %setjmp_buf)

For SJLJ based exception handling, this intrinsic forces register saving for the
current function and stores the address of the following instruction for use as
a destination address by `llvm.eh.sjlj.longjmp`_. The buffer format and the
overall functioning of this intrinsic is compatible with the GCC
``__builtin_setjmp`` implementation allowing code built with the clang and GCC
to interoperate.

The single parameter is a pointer to a five word buffer in which the calling
context is saved. The front end places the frame pointer in the first word, and
the target implementation of this intrinsic should place the destination address
for a `llvm.eh.sjlj.longjmp`_ in the second word. The following three words are
available for use in a target-specific manner.

.. _llvm.eh.sjlj.longjmp:

``llvm.eh.sjlj.longjmp``
~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: llvm

  void @llvm.eh.sjlj.longjmp(i8* %setjmp_buf)

For SJLJ based exception handling, the ``llvm.eh.sjlj.longjmp`` intrinsic is
used to implement ``__builtin_longjmp()``. The single parameter is a pointer to
a buffer populated by `llvm.eh.sjlj.setjmp`_. The frame pointer and stack
pointer are restored from the buffer, then control is transferred to the
destination address.

``llvm.eh.sjlj.lsda``
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: llvm

  i8* @llvm.eh.sjlj.lsda()

For SJLJ based exception handling, the ``llvm.eh.sjlj.lsda`` intrinsic returns
the address of the Language Specific Data Area (LSDA) for the current
function. The SJLJ front-end code stores this address in the exception handling
function context for use by the runtime.

``llvm.eh.sjlj.callsite``
~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: llvm

  void @llvm.eh.sjlj.callsite(i32 %call_site_num)

For SJLJ based exception handling, the ``llvm.eh.sjlj.callsite`` intrinsic
identifies the callsite value associated with the following ``invoke``
instruction. This is used to ensure that landing pad entries in the LSDA are
generated in matching order.

Asm Table Formats
=================

There are two tables that are used by the exception handling runtime to
determine which actions should be taken when an exception is thrown.

Exception Handling Frame
------------------------

An exception handling frame ``eh_frame`` is very similar to the unwind frame
used by DWARF debug info. The frame contains all the information necessary to
tear down the current frame and restore the state of the prior frame. There is
an exception handling frame for each function in a compile unit, plus a common
exception handling frame that defines information common to all functions in the
unit.

The format of this call frame information (CFI) is often platform-dependent,
however. ARM, for example, defines their own format. Apple has their own compact
unwind info format.  On Windows, another format is used for all architectures
since 32-bit x86.  LLVM will emit whatever information is required by the
target.

Exception Tables
----------------

An exception table contains information about what actions to take when an
exception is thrown in a particular part of a function's code. This is typically
referred to as the language-specific data area (LSDA). The format of the LSDA
table is specific to the personality function, but the majority of personalities
out there use a variation of the tables consumed by ``__gxx_personality_v0``.
There is one exception table per function, except leaf functions and functions
that have calls only to non-throwing functions. They do not need an exception
table.

.. _wineh:

Exception Handling using the Windows Runtime
=================================================

Background on Windows exceptions
---------------------------------

Interacting with exceptions on Windows is significantly more complicated than
on Itanium C++ ABI platforms. The fundamental difference between the two models
is that Itanium EH is designed around the idea of "successive unwinding," while
Windows EH is not.

Under Itanium, throwing an exception typically involves allocating thread local
memory to hold the exception, and calling into the EH runtime. The runtime
identifies frames with appropriate exception handling actions, and successively
resets the register context of the current thread to the most recently active
frame with actions to run. In LLVM, execution resumes at a ``landingpad``
instruction, which produces register values provided by the runtime. If a
function is only cleaning up allocated resources, the function is responsible
for calling ``_Unwind_Resume`` to transition to the next most recently active
frame after it is finished cleaning up. Eventually, the frame responsible for
handling the exception calls ``__cxa_end_catch`` to destroy the exception,
release its memory, and resume normal control flow.

The Windows EH model does not use these successive register context resets.
Instead, the active exception is typically described by a frame on the stack.
In the case of C++ exceptions, the exception object is allocated in stack memory
and its address is passed to ``__CxxThrowException``. General purpose structured
exceptions (SEH) are more analogous to Linux signals, and they are dispatched by
userspace DLLs provided with Windows. Each frame on the stack has an assigned EH
personality routine, which decides what actions to take to handle the exception.
There are a few major personalities for C and C++ code: the C++ personality
(``__CxxFrameHandler3``) and the SEH personalities (``_except_handler3``,
``_except_handler4``, and ``__C_specific_handler``). All of them implement
cleanups by calling back into a "funclet" contained in the parent function.

Funclets, in this context, are regions of the parent function that can be called
as though they were a function pointer with a very special calling convention.
The frame pointer of the parent frame is passed into the funclet either using
the standard EBP register or as the first parameter register, depending on the
architecture. The funclet implements the EH action by accessing local variables
in memory through the frame pointer, and returning some appropriate value,
continuing the EH process.  No variables live in to or out of the funclet can be
allocated in registers.

The C++ personality also uses funclets to contain the code for catch blocks
(i.e. all user code between the braces in ``catch (Type obj) { ... }``). The
runtime must use funclets for catch bodies because the C++ exception object is
allocated in a child stack frame of the function handling the exception. If the
runtime rewound the stack back to frame of the catch, the memory holding the
exception would be overwritten quickly by subsequent function calls.  The use of
funclets also allows ``__CxxFrameHandler3`` to implement rethrow without
resorting to TLS. Instead, the runtime throws a special exception, and then uses
SEH (``__try / __except``) to resume execution with new information in the child
frame.

In other words, the successive unwinding approach is incompatible with Visual
C++ exceptions and general purpose Windows exception handling. Because the C++
exception object lives in stack memory, LLVM cannot provide a custom personality
function that uses landingpads.  Similarly, SEH does not provide any mechanism
to rethrow an exception or continue unwinding.  Therefore, LLVM must use the IR
constructs described later in this document to implement compatible exception
handling.

SEH filter expressions
-----------------------

The SEH personality functions also use funclets to implement filter expressions,
which allow executing arbitrary user code to decide which exceptions to catch.
Filter expressions should not be confused with the ``filter`` clause of the LLVM
``landingpad`` instruction.  Typically filter expressions are used to determine
if the exception came from a particular DLL or code region, or if code faulted
while accessing a particular memory address range. LLVM does not currently have
IR to represent filter expressions because it is difficult to represent their
control dependencies.  Filter expressions run during the first phase of EH,
before cleanups run, making it very difficult to build a faithful control flow
graph.  For now, the new EH instructions cannot represent SEH filter
expressions, and frontends must outline them ahead of time. Local variables of
the parent function can be escaped and accessed using the ``llvm.localescape``
and ``llvm.localrecover`` intrinsics.

New exception handling instructions
------------------------------------

The primary design goal of the new EH instructions is to support funclet
generation while preserving information about the CFG so that SSA formation
still works.  As a secondary goal, they are designed to be generic across MSVC
and Itanium C++ exceptions. They make very few assumptions about the data
required by the personality, so long as it uses the familiar core EH actions:
catch, cleanup, and terminate.  However, the new instructions are hard to modify
without knowing details of the EH personality. While they can be used to
represent Itanium EH, the landingpad model is strictly better for optimization
purposes.

The following new instructions are considered "exception handling pads", in that
they must be the first non-phi instruction of a basic block that may be the
unwind destination of an EH flow edge:
``catchswitch``, ``catchpad``, and ``cleanuppad``.
As with landingpads, when entering a try scope, if the
frontend encounters a call site that may throw an exception, it should emit an
invoke that unwinds to a ``catchswitch`` block. Similarly, inside the scope of a
C++ object with a destructor, invokes should unwind to a ``cleanuppad``.

New instructions are also used to mark the points where control is transferred
out of a catch/cleanup handler (which will correspond to exits from the
generated funclet).  A catch handler which reaches its end by normal execution
executes a ``catchret`` instruction, which is a terminator indicating where in
the function control is returned to.  A cleanup handler which reaches its end
by normal execution executes a ``cleanupret`` instruction, which is a terminator
indicating where the active exception will unwind to next.

Each of these new EH pad instructions has a way to identify which action should
be considered after this action. The ``catchswitch`` instruction is a terminator
and has an unwind destination operand analogous to the unwind destination of an
invoke.  The ``cleanuppad`` instruction is not
a terminator, so the unwind destination is stored on the ``cleanupret``
instruction instead. Successfully executing a catch handler should resume
normal control flow, so neither ``catchpad`` nor ``catchret`` instructions can
unwind. All of these "unwind edges" may refer to a basic block that contains an
EH pad instruction, or they may unwind to the caller.  Unwinding to the caller
has roughly the same semantics as the ``resume`` instruction in the landingpad
model. When inlining through an invoke, instructions that unwind to the caller
are hooked up to unwind to the unwind destination of the call site.

Putting things together, here is a hypothetical lowering of some C++ that uses
all of the new IR instructions:

.. code-block:: c

  struct Cleanup {
    Cleanup();
    ~Cleanup();
    int m;
  };
  void may_throw();
  int f() noexcept {
    try {
      Cleanup obj;
      may_throw();
    } catch (int e) {
      may_throw();
      return e;
    }
    return 0;
  }

.. code-block:: text

  define i32 @f() nounwind personality ptr @__CxxFrameHandler3 {
  entry:
    %obj = alloca %struct.Cleanup, align 4
    %e = alloca i32, align 4
    %call = invoke ptr @"??0Cleanup@@QEAA@XZ"(ptr nonnull %obj)
            to label %invoke.cont unwind label %lpad.catch

  invoke.cont:                                      ; preds = %entry
    invoke void @"?may_throw@@YAXXZ"()
            to label %invoke.cont.2 unwind label %lpad.cleanup

  invoke.cont.2:                                    ; preds = %invoke.cont
    call void @"??_DCleanup@@QEAA@XZ"(ptr nonnull %obj) nounwind
    br label %return

  return:                                           ; preds = %invoke.cont.3, %invoke.cont.2
    %retval.0 = phi i32 [ 0, %invoke.cont.2 ], [ %3, %invoke.cont.3 ]
    ret i32 %retval.0

  lpad.cleanup:                                     ; preds = %invoke.cont.2
    %0 = cleanuppad within none []
    call void @"??1Cleanup@@QEAA@XZ"(ptr nonnull %obj) nounwind
    cleanupret from %0 unwind label %lpad.catch

  lpad.catch:                                       ; preds = %lpad.cleanup, %entry
    %1 = catchswitch within none [label %catch.body] unwind label %lpad.terminate

  catch.body:                                       ; preds = %lpad.catch
    %catch = catchpad within %1 [ptr @"??_R0H@8", i32 0, ptr %e]
    invoke void @"?may_throw@@YAXXZ"()
            to label %invoke.cont.3 unwind label %lpad.terminate

  invoke.cont.3:                                    ; preds = %catch.body
    %3 = load i32, ptr %e, align 4
    catchret from %catch to label %return

  lpad.terminate:                                   ; preds = %catch.body, %lpad.catch
    cleanuppad within none []
    call void @"?terminate@@YAXXZ"()
    unreachable
  }

Funclet parent tokens
-----------------------

In order to produce tables for EH personalities that use funclets, it is
necessary to recover the nesting that was present in the source. This funclet
parent relationship is encoded in the IR using tokens produced by the new "pad"
instructions. The token operand of a "pad" or "ret" instruction indicates which
funclet it is in, or "none" if it is not nested within another funclet.

The ``catchpad`` and ``cleanuppad`` instructions establish new funclets, and
their tokens are consumed by other "pad" instructions to establish membership.
The ``catchswitch`` instruction does not create a funclet, but it produces a
token that is always consumed by its immediate successor ``catchpad``
instructions. This ensures that every catch handler modelled by a ``catchpad``
belongs to exactly one ``catchswitch``, which models the dispatch point after a
C++ try.

Here is an example of what this nesting looks like using some hypothetical
C++ code:

.. code-block:: c

  void f() {
    try {
      throw;
    } catch (...) {
      try {
        throw;
      } catch (...) {
      }
    }
  }

.. code-block:: text

  define void @f() #0 personality i8* bitcast (i32 (...)* @__CxxFrameHandler3 to i8*) {
  entry:
    invoke void @_CxxThrowException(i8* null, %eh.ThrowInfo* null) #1
            to label %unreachable unwind label %catch.dispatch

  catch.dispatch:                                   ; preds = %entry
    %0 = catchswitch within none [label %catch] unwind to caller

  catch:                                            ; preds = %catch.dispatch
    %1 = catchpad within %0 [i8* null, i32 64, i8* null]
    invoke void @_CxxThrowException(i8* null, %eh.ThrowInfo* null) #1
            to label %unreachable unwind label %catch.dispatch2

  catch.dispatch2:                                  ; preds = %catch
    %2 = catchswitch within %1 [label %catch3] unwind to caller

  catch3:                                           ; preds = %catch.dispatch2
    %3 = catchpad within %2 [i8* null, i32 64, i8* null]
    catchret from %3 to label %try.cont

  try.cont:                                         ; preds = %catch3
    catchret from %1 to label %try.cont6

  try.cont6:                                        ; preds = %try.cont
    ret void

  unreachable:                                      ; preds = %catch, %entry
    unreachable
  }

The "inner" ``catchswitch`` consumes ``%1`` which is produced by the outer
catchswitch.

.. _wineh-constraints:

Funclet transitions
-----------------------

The EH tables for personalities that use funclets make implicit use of the
funclet nesting relationship to encode unwind destinations, and so are
constrained in the set of funclet transitions they can represent.  The related
LLVM IR instructions accordingly have constraints that ensure encodability of
the EH edges in the flow graph.

A ``catchswitch``, ``catchpad``, or ``cleanuppad`` is said to be "entered"
when it executes.  It may subsequently be "exited" by any of the following
means:

* A ``catchswitch`` is immediately exited when none of its constituent
  ``catchpad``\ s are appropriate for the in-flight exception and it unwinds
  to its unwind destination or the caller.
* A ``catchpad`` and its parent ``catchswitch`` are both exited when a
  ``catchret`` from the ``catchpad`` is executed.
* A ``cleanuppad`` is exited when a ``cleanupret`` from it is executed.
* Any of these pads is exited when control unwinds to the function's caller,
  either by a ``call`` which unwinds all the way to the function's caller,
  a nested ``catchswitch`` marked "``unwinds to caller``", or a nested
  ``cleanuppad``\ 's ``cleanupret`` marked "``unwinds to caller"``.
* Any of these pads is exited when an unwind edge (from an ``invoke``,
  nested ``catchswitch``, or nested ``cleanuppad``\ 's ``cleanupret``)
  unwinds to a destination pad that is not a descendant of the given pad.

Note that the ``ret`` instruction is *not* a valid way to exit a funclet pad;
it is undefined behavior to execute a ``ret`` when a pad has been entered but
not exited.

A single unwind edge may exit any number of pads (with the restrictions that
the edge from a ``catchswitch`` must exit at least itself, and the edge from
a ``cleanupret`` must exit at least its ``cleanuppad``), and then must enter
exactly one pad, which must be distinct from all the exited pads.  The parent
of the pad that an unwind edge enters must be the most-recently-entered
not-yet-exited pad (after exiting from any pads that the unwind edge exits),
or "none" if there is no such pad.  This ensures that the stack of executing
funclets at run-time always corresponds to some path in the funclet pad tree
that the parent tokens encode.

All unwind edges which exit any given funclet pad (including ``cleanupret``
edges exiting their ``cleanuppad`` and ``catchswitch`` edges exiting their
``catchswitch``) must share the same unwind destination.  Similarly, any
funclet pad which may be exited by unwind to caller must not be exited by
any exception edges which unwind anywhere other than the caller.  This
ensures that each funclet as a whole has only one unwind destination, which
EH tables for funclet personalities may require.  Note that any unwind edge
which exits a ``catchpad`` also exits its parent ``catchswitch``, so this
implies that for any given ``catchswitch``, its unwind destination must also
be the unwind destination of any unwind edge that exits any of its constituent
``catchpad``\s.  Because ``catchswitch`` has no ``nounwind`` variant, and
because IR producers are not *required* to annotate calls which will not
unwind as ``nounwind``, it is legal to nest a ``call`` or an "``unwind to
caller``\ " ``catchswitch`` within a funclet pad that has an unwind
destination other than caller; it is undefined behavior for such a ``call``
or ``catchswitch`` to unwind.

Finally, the funclet pads' unwind destinations cannot form a cycle.  This
ensures that EH lowering can construct "try regions" with a tree-like
structure, which funclet-based personalities may require.

Exception Handling support on the target
=================================================

In order to support exception handling on particular target, there are a few
items need to be implemented.

* CFI directives

  First, you have to assign each target register with a unique DWARF number.
  Then in ``TargetFrameLowering``'s ``emitPrologue``, you have to emit `CFI
  directives <https://sourceware.org/binutils/docs/as/CFI-directives.html>`_
  to specify how to calculate the CFA (Canonical Frame Address) and how register
  is restored from the address pointed by the CFA with an offset. The assembler
  is instructed by CFI directives to build ``.eh_frame`` section, which is used
  by th unwinder to unwind stack during exception handling.

* ``getExceptionPointerRegister`` and ``getExceptionSelectorRegister``

  ``TargetLowering`` must implement both functions. The *personality function*
  passes the *exception structure* (a pointer) and *selector value* (an integer)
  to the landing pad through the registers specified by ``getExceptionPointerRegister``
  and ``getExceptionSelectorRegister`` respectively. On most platforms, they
  will be GPRs and will be the same as the ones specified in the calling convention.

* ``EH_RETURN``

  The ISD node represents the undocumented GCC extension ``__builtin_eh_return (offset, handler)``,
  which adjusts the stack by offset and then jumps to the handler. ``__builtin_eh_return``
  is used in GCC unwinder (`libgcc <https://gcc.gnu.org/onlinedocs/gccint/Libgcc.html>`_),
  but not in LLVM unwinder (`libunwind <https://clang.llvm.org/docs/Toolchain.html#unwind-library>`_).
  If you are on the top of ``libgcc`` and have particular requirement on your target,
  you have to handle ``EH_RETURN`` in ``TargetLowering``.

If you don't leverage the existing runtime (``libstdc++`` and ``libgcc``),
you have to take a look on `libc++ <https://libcxx.llvm.org/>`_ and
`libunwind <https://clang.llvm.org/docs/Toolchain.html#unwind-library>`_
to see what have to be done there. For ``libunwind``, you have to do the following

* ``__libunwind_config.h``

  Define macros for your target.

* ``include/libunwind.h``

  Define enum for the target registers.

* ``src/Registers.hpp``

  Define ``Registers`` class for your target, implement setter and getter functions.

* ``src/UnwindCursor.hpp``

  Define ``dwarfEncoding`` and ``stepWithCompactEncoding`` for your ``Registers``
  class.

* ``src/UnwindRegistersRestore.S``

  Write an assembly function to restore all your target registers from the memory.

* ``src/UnwindRegistersSave.S``

  Write an assembly function to save all your target registers on the memory.
