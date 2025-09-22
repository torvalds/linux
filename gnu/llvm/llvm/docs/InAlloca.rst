==========================================
Design and Usage of the InAlloca Attribute
==========================================

Introduction
============

The :ref:`inalloca <attr_inalloca>` attribute is designed to allow
taking the address of an aggregate argument that is being passed by
value through memory.  Primarily, this feature is required for
compatibility with the Microsoft C++ ABI.  Under that ABI, class
instances that are passed by value are constructed directly into
argument stack memory.  Prior to the addition of inalloca, calls in LLVM
were indivisible instructions.  There was no way to perform intermediate
work, such as object construction, between the first stack adjustment
and the final control transfer.  With inalloca, all arguments passed in
memory are modelled as a single alloca, which can be stored to prior to
the call.  Unfortunately, this complicated feature comes with a large
set of restrictions designed to bound the lifetime of the argument
memory around the call.

For now, it is recommended that frontends and optimizers avoid producing
this construct, primarily because it forces the use of a base pointer.
This feature may grow in the future to allow general mid-level
optimization, but for now, it should be regarded as less efficient than
passing by value with a copy.

Intended Usage
==============

The example below is the intended LLVM IR lowering for some C++ code
that passes two default-constructed ``Foo`` objects to ``g`` in the
32-bit Microsoft C++ ABI.

.. code-block:: c++

    // Foo is non-trivial.
    struct Foo { int a, b; Foo(); ~Foo(); Foo(const Foo &); };
    void g(Foo a, Foo b);
    void f() {
      g(Foo(), Foo());
    }

.. code-block:: text

    %struct.Foo = type { i32, i32 }
    declare void @Foo_ctor(%struct.Foo* %this)
    declare void @Foo_dtor(%struct.Foo* %this)
    declare void @g(<{ %struct.Foo, %struct.Foo }>* inalloca %memargs)

    define void @f() {
    entry:
      %base = call i8* @llvm.stacksave()
      %memargs = alloca <{ %struct.Foo, %struct.Foo }>
      %b = getelementptr <{ %struct.Foo, %struct.Foo }>* %memargs, i32 1
      call void @Foo_ctor(%struct.Foo* %b)

      ; If a's ctor throws, we must destruct b.
      %a = getelementptr <{ %struct.Foo, %struct.Foo }>* %memargs, i32 0
      invoke void @Foo_ctor(%struct.Foo* %a)
          to label %invoke.cont unwind %invoke.unwind

    invoke.cont:
      call void @g(<{ %struct.Foo, %struct.Foo }>* inalloca %memargs)
      call void @llvm.stackrestore(i8* %base)
      ...

    invoke.unwind:
      call void @Foo_dtor(%struct.Foo* %b)
      call void @llvm.stackrestore(i8* %base)
      ...
    }

To avoid stack leaks, the frontend saves the current stack pointer with
a call to :ref:`llvm.stacksave <int_stacksave>`.  Then, it allocates the
argument stack space with alloca and calls the default constructor.  The
default constructor could throw an exception, so the frontend has to
create a landing pad.  The frontend has to destroy the already
constructed argument ``b`` before restoring the stack pointer.  If the
constructor does not unwind, ``g`` is called.  In the Microsoft C++ ABI,
``g`` will destroy its arguments, and then the stack is restored in
``f``.

Design Considerations
=====================

Lifetime
--------

The biggest design consideration for this feature is object lifetime.
We cannot model the arguments as static allocas in the entry block,
because all calls need to use the memory at the top of the stack to pass
arguments.  We cannot vend pointers to that memory at function entry
because after code generation they will alias.

The rule against allocas between argument allocations and the call site
avoids this problem, but it creates a cleanup problem.  Cleanup and
lifetime is handled explicitly with stack save and restore calls.  In
the future, we may want to introduce a new construct such as ``freea``
or ``afree`` to make it clear that this stack adjusting cleanup is less
powerful than a full stack save and restore.

Nested Calls and Copy Elision
-----------------------------

We also want to be able to support copy elision into these argument
slots.  This means we have to support multiple live argument
allocations.

Consider the evaluation of:

.. code-block:: c++

    // Foo is non-trivial.
    struct Foo { int a; Foo(); Foo(const &Foo); ~Foo(); };
    Foo bar(Foo b);
    int main() {
      bar(bar(Foo()));
    }

In this case, we want to be able to elide copies into ``bar``'s argument
slots.  That means we need to have more than one set of argument frames
active at the same time.  First, we need to allocate the frame for the
outer call so we can pass it in as the hidden struct return pointer to
the middle call.  Then we do the same for the middle call, allocating a
frame and passing its address to ``Foo``'s default constructor.  By
wrapping the evaluation of the inner ``bar`` with stack save and
restore, we can have multiple overlapping active call frames.

Callee-cleanup Calling Conventions
----------------------------------

Another wrinkle is the existence of callee-cleanup conventions.  On
Windows, all methods and many other functions adjust the stack to clear
the memory used to pass their arguments.  In some sense, this means that
the allocas are automatically cleared by the call.  However, LLVM
instead models this as a write of undef to all of the inalloca values
passed to the call instead of a stack adjustment.  Frontends should
still restore the stack pointer to avoid a stack leak.

Exceptions
----------

There is also the possibility of an exception.  If argument evaluation
or copy construction throws an exception, the landing pad must do
cleanup, which includes adjusting the stack pointer to avoid a stack
leak.  This means the cleanup of the stack memory cannot be tied to the
call itself.  There needs to be a separate IR-level instruction that can
perform independent cleanup of arguments.

Efficiency
----------

Eventually, it should be possible to generate efficient code for this
construct.  In particular, using inalloca should not require a base
pointer.  If the backend can prove that all points in the CFG only have
one possible stack level, then it can address the stack directly from
the stack pointer.  While this is not yet implemented, the plan is that
the inalloca attribute should not change much, but the frontend IR
generation recommendations may change.
