================================
Frequently Asked Questions (FAQ)
================================

.. contents::
   :local:


License
=======

Can I modify LLVM source code and redistribute the modified source?
-------------------------------------------------------------------
Yes.  The modified source distribution must retain the copyright notice and
follow the conditions listed in the `Apache License v2.0 with LLVM Exceptions
<https://github.com/llvm/llvm-project/blob/main/llvm/LICENSE.TXT>`_.


Can I modify the LLVM source code and redistribute binaries or other tools based on it, without redistributing the source?
--------------------------------------------------------------------------------------------------------------------------
Yes. This is why we distribute LLVM under a less restrictive license than GPL,
as explained in the first question above.


Source Code
===========

In what language is LLVM written?
---------------------------------
All of the LLVM tools and libraries are written in C++ with extensive use of
the STL.


How portable is the LLVM source code?
-------------------------------------
The LLVM source code should be portable to most modern Unix-like operating
systems. LLVM also has excellent support on Windows systems.
Most of the code is written in standard C++ with operating system
services abstracted to a support library.  The tools required to build and
test LLVM have been ported to a plethora of platforms.


What API do I use to store a value to one of the virtual registers in LLVM IR's SSA representation?
---------------------------------------------------------------------------------------------------

In short: you can't. It's actually kind of a silly question once you grok
what's going on. Basically, in code like:

.. code-block:: llvm

    %result = add i32 %foo, %bar

, ``%result`` is just a name given to the ``Value`` of the ``add``
instruction. In other words, ``%result`` *is* the add instruction. The
"assignment" doesn't explicitly "store" anything to any "virtual register";
the "``=``" is more like the mathematical sense of equality.

Longer explanation: In order to generate a textual representation of the
IR, some kind of name has to be given to each instruction so that other
instructions can textually reference it. However, the isomorphic in-memory
representation that you manipulate from C++ has no such restriction since
instructions can simply keep pointers to any other ``Value``'s that they
reference. In fact, the names of dummy numbered temporaries like ``%1`` are
not explicitly represented in the in-memory representation at all (see
``Value::getName()``).


Source Languages
================

What source languages are supported?
------------------------------------

LLVM currently has full support for C and C++ source languages through
`Clang <https://clang.llvm.org/>`_. Many other language frontends have
been written using LLVM, and an incomplete list is available at
`projects with LLVM <https://llvm.org/ProjectsWithLLVM/>`_.


I'd like to write a self-hosting LLVM compiler. How should I interface with the LLVM middle-end optimizers and back-end code generators?
----------------------------------------------------------------------------------------------------------------------------------------
Your compiler front-end will communicate with LLVM by creating a module in the
LLVM intermediate representation (IR) format. Assuming you want to write your
language's compiler in the language itself (rather than C++), there are 3
major ways to tackle generating LLVM IR from a front-end:

1. **Call into the LLVM libraries code using your language's FFI (foreign
   function interface).**

  * *for:* best tracks changes to the LLVM IR, .ll syntax, and .bc format

  * *for:* enables running LLVM optimization passes without a emit/parse
    overhead

  * *for:* adapts well to a JIT context

  * *against:* lots of ugly glue code to write

2. **Emit LLVM assembly from your compiler's native language.**

  * *for:* very straightforward to get started

  * *against:* the .ll parser is slower than the bitcode reader when
    interfacing to the middle end

  * *against:* it may be harder to track changes to the IR

3. **Emit LLVM bitcode from your compiler's native language.**

  * *for:* can use the more-efficient bitcode reader when interfacing to the
    middle end

  * *against:* you'll have to re-engineer the LLVM IR object model and bitcode
    writer in your language

  * *against:* it may be harder to track changes to the IR

If you go with the first option, the C bindings in include/llvm-c should help
a lot, since most languages have strong support for interfacing with C. The
most common hurdle with calling C from managed code is interfacing with the
garbage collector. The C interface was designed to require very little memory
management, and so is straightforward in this regard.

What support is there for a higher level source language constructs for building a compiler?
--------------------------------------------------------------------------------------------
Currently, there isn't much. LLVM supports an intermediate representation
which is useful for code representation but will not support the high level
(abstract syntax tree) representation needed by most compilers. There are no
facilities for lexical nor semantic analysis.


I don't understand the ``GetElementPtr`` instruction. Help!
-----------------------------------------------------------
See `The Often Misunderstood GEP Instruction <GetElementPtr.html>`_.


Using the C and C++ Front Ends
==============================

Can I compile C or C++ code to platform-independent LLVM bitcode?
-----------------------------------------------------------------
No. C and C++ are inherently platform-dependent languages. The most obvious
example of this is the preprocessor. A very common way that C code is made
portable is by using the preprocessor to include platform-specific code. In
practice, information about other platforms is lost after preprocessing, so
the result is inherently dependent on the platform that the preprocessing was
targeting.

Another example is ``sizeof``. It's common for ``sizeof(long)`` to vary
between platforms. In most C front-ends, ``sizeof`` is expanded to a
constant immediately, thus hard-wiring a platform-specific detail.

Also, since many platforms define their ABIs in terms of C, and since LLVM is
lower-level than C, front-ends currently must emit platform-specific IR in
order to have the result conform to the platform ABI.


Questions about code generated by the demo page
===============================================

What is this ``llvm.global_ctors`` and ``_GLOBAL__I_a...`` stuff that happens when I ``#include <iostream>``?
-------------------------------------------------------------------------------------------------------------
If you ``#include`` the ``<iostream>`` header into a C++ translation unit,
the file will probably use the ``std::cin``/``std::cout``/... global objects.
However, C++ does not guarantee an order of initialization between static
objects in different translation units, so if a static ctor/dtor in your .cpp
file used ``std::cout``, for example, the object would not necessarily be
automatically initialized before your use.

To make ``std::cout`` and friends work correctly in these scenarios, the STL
that we use declares a static object that gets created in every translation
unit that includes ``<iostream>``.  This object has a static constructor
and destructor that initializes and destroys the global iostream objects
before they could possibly be used in the file.  The code that you see in the
``.ll`` file corresponds to the constructor and destructor registration code.

If you would like to make it easier to *understand* the LLVM code generated
by the compiler in the demo page, consider using ``printf()`` instead of
``iostream``\s to print values.


Where did all of my code go??
-----------------------------
If you are using the LLVM demo page, you may often wonder what happened to
all of the code that you typed in.  Remember that the demo script is running
the code through the LLVM optimizers, so if your code doesn't actually do
anything useful, it might all be deleted.

To prevent this, make sure that the code is actually needed.  For example, if
you are computing some expression, return the value from the function instead
of leaving it in a local variable.  If you really want to constrain the
optimizer, you can read from and assign to ``volatile`` global variables.


What is this "``undef``" thing that shows up in my code?
--------------------------------------------------------
``undef`` is the LLVM way of representing a value that is not defined.  You
can get these if you do not initialize a variable before you use it.  For
example, the C function:

.. code-block:: c

   int X() { int i; return i; }

Is compiled to "``ret i32 undef``" because "``i``" never has a value specified
for it.


Why does instcombine + simplifycfg turn a call to a function with a mismatched calling convention into "unreachable"? Why not make the verifier reject it?
----------------------------------------------------------------------------------------------------------------------------------------------------------
This is a common problem run into by authors of front-ends that are using
custom calling conventions: you need to make sure to set the right calling
convention on both the function and on each call to the function.  For
example, this code:

.. code-block:: llvm

   define fastcc void @foo() {
       ret void
   }
   define void @bar() {
       call void @foo()
       ret void
   }

Is optimized to:

.. code-block:: llvm

   define fastcc void @foo() {
       ret void
   }
   define void @bar() {
       unreachable
   }

... with "``opt -instcombine -simplifycfg``".  This often bites people because
"all their code disappears".  Setting the calling convention on the caller and
callee is required for indirect calls to work, so people often ask why not
make the verifier reject this sort of thing.

The answer is that this code has undefined behavior, but it is not illegal.
If we made it illegal, then every transformation that could potentially create
this would have to ensure that it doesn't, and there is valid code that can
create this sort of construct (in dead code).  The sorts of things that can
cause this to happen are fairly contrived, but we still need to accept them.
Here's an example:

.. code-block:: llvm

   define fastcc void @foo() {
       ret void
   }
   define internal void @bar(void()* %FP, i1 %cond) {
       br i1 %cond, label %T, label %F
   T:
       call void %FP()
       ret void
   F:
       call fastcc void %FP()
       ret void
   }
   define void @test() {
       %X = or i1 false, false
       call void @bar(void()* @foo, i1 %X)
       ret void
   }

In this example, "test" always passes ``@foo``/``false`` into ``bar``, which
ensures that it is dynamically called with the right calling conv (thus, the
code is perfectly well defined).  If you run this through the inliner, you
get this (the explicit "or" is there so that the inliner doesn't dead code
eliminate a bunch of stuff):

.. code-block:: llvm

   define fastcc void @foo() {
       ret void
   }
   define void @test() {
       %X = or i1 false, false
       br i1 %X, label %T.i, label %F.i
   T.i:
       call void @foo()
       br label %bar.exit
   F.i:
       call fastcc void @foo()
       br label %bar.exit
   bar.exit:
       ret void
   }

Here you can see that the inlining pass made an undefined call to ``@foo``
with the wrong calling convention.  We really don't want to make the inliner
have to know about this sort of thing, so it needs to be valid code.  In this
case, dead code elimination can trivially remove the undefined code.  However,
if ``%X`` was an input argument to ``@test``, the inliner would produce this:

.. code-block:: llvm

   define fastcc void @foo() {
       ret void
   }

   define void @test(i1 %X) {
       br i1 %X, label %T.i, label %F.i
   T.i:
       call void @foo()
       br label %bar.exit
   F.i:
       call fastcc void @foo()
       br label %bar.exit
   bar.exit:
       ret void
   }

The interesting thing about this is that ``%X`` *must* be false for the
code to be well-defined, but no amount of dead code elimination will be able
to delete the broken call as unreachable.  However, since
``instcombine``/``simplifycfg`` turns the undefined call into unreachable, we
end up with a branch on a condition that goes to unreachable: a branch to
unreachable can never happen, so "``-inline -instcombine -simplifycfg``" is
able to produce:

.. code-block:: llvm

   define fastcc void @foo() {
      ret void
   }
   define void @test(i1 %X) {
   F.i:
      call fastcc void @foo()
      ret void
   }
