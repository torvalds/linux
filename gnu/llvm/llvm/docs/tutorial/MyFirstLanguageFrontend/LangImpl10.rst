======================================================
Kaleidoscope: Conclusion and other useful LLVM tidbits
======================================================

.. contents::
   :local:

Tutorial Conclusion
===================

Welcome to the final chapter of the "`Implementing a language with
LLVM <index.html>`_" tutorial. In the course of this tutorial, we have
grown our little Kaleidoscope language from being a useless toy, to
being a semi-interesting (but probably still useless) toy. :)

It is interesting to see how far we've come, and how little code it has
taken. We built the entire lexer, parser, AST, code generator, an
interactive run-loop (with a JIT!), and emitted debug information in
standalone executables - all in under 1000 lines of (non-comment/non-blank)
code.

Our little language supports a couple of interesting features: it
supports user defined binary and unary operators, it uses JIT
compilation for immediate evaluation, and it supports a few control flow
constructs with SSA construction.

Part of the idea of this tutorial was to show you how easy and fun it
can be to define, build, and play with languages. Building a compiler
need not be a scary or mystical process! Now that you've seen some of
the basics, I strongly encourage you to take the code and hack on it.
For example, try adding:

-  **global variables** - While global variables have questionable value
   in modern software engineering, they are often useful when putting
   together quick little hacks like the Kaleidoscope compiler itself.
   Fortunately, our current setup makes it very easy to add global
   variables: just have value lookup check to see if an unresolved
   variable is in the global variable symbol table before rejecting it.
   To create a new global variable, make an instance of the LLVM
   ``GlobalVariable`` class.
-  **typed variables** - Kaleidoscope currently only supports variables
   of type double. This gives the language a very nice elegance, because
   only supporting one type means that you never have to specify types.
   Different languages have different ways of handling this. The easiest
   way is to require the user to specify types for every variable
   definition, and record the type of the variable in the symbol table
   along with its Value\*.
-  **arrays, structs, vectors, etc** - Once you add types, you can start
   extending the type system in all sorts of interesting ways. Simple
   arrays are very easy and are quite useful for many different
   applications. Adding them is mostly an exercise in learning how the
   LLVM `getelementptr <../../LangRef.html#getelementptr-instruction>`_ instruction
   works: it is so nifty/unconventional, it `has its own
   FAQ <../../GetElementPtr.html>`_!
-  **standard runtime** - Our current language allows the user to access
   arbitrary external functions, and we use it for things like "printd"
   and "putchard". As you extend the language to add higher-level
   constructs, often these constructs make the most sense if they are
   lowered to calls into a language-supplied runtime. For example, if
   you add hash tables to the language, it would probably make sense to
   add the routines to a runtime, instead of inlining them all the way.
-  **memory management** - Currently we can only access the stack in
   Kaleidoscope. It would also be useful to be able to allocate heap
   memory, either with calls to the standard libc malloc/free interface
   or with a garbage collector. If you would like to use garbage
   collection, note that LLVM fully supports `Accurate Garbage
   Collection <../../GarbageCollection.html>`_ including algorithms that
   move objects and need to scan/update the stack.
-  **exception handling support** - LLVM supports generation of `zero
   cost exceptions <../../ExceptionHandling.html>`_ which interoperate with
   code compiled in other languages. You could also generate code by
   implicitly making every function return an error value and checking
   it. You could also make explicit use of setjmp/longjmp. There are
   many different ways to go here.
-  **object orientation, generics, database access, complex numbers,
   geometric programming, ...** - Really, there is no end of crazy
   features that you can add to the language.
-  **unusual domains** - We've been talking about applying LLVM to a
   domain that many people are interested in: building a compiler for a
   specific language. However, there are many other domains that can use
   compiler technology that are not typically considered. For example,
   LLVM has been used to implement OpenGL graphics acceleration,
   translate C++ code to ActionScript, and many other cute and clever
   things. Maybe you will be the first to JIT compile a regular
   expression interpreter into native code with LLVM?

Have fun - try doing something crazy and unusual. Building a language
like everyone else always has, is much less fun than trying something a
little crazy or off the wall and seeing how it turns out. If you get
stuck or want to talk about it, please post on the `LLVM forums 
<https://discourse.llvm.org>`_: it has lots of people who are interested
in languages and are often willing to help out.

Before we end this tutorial, I want to talk about some "tips and tricks"
for generating LLVM IR. These are some of the more subtle things that
may not be obvious, but are very useful if you want to take advantage of
LLVM's capabilities.

Properties of the LLVM IR
=========================

We have a couple of common questions about code in the LLVM IR form -
let's just get these out of the way right now, shall we?

Target Independence
-------------------

Kaleidoscope is an example of a "portable language": any program written
in Kaleidoscope will work the same way on any target that it runs on.
Many other languages have this property, e.g. lisp, java, haskell,
javascript, python, etc (note that while these languages are portable,
not all their libraries are).

One nice aspect of LLVM is that it is often capable of preserving target
independence in the IR: you can take the LLVM IR for a
Kaleidoscope-compiled program and run it on any target that LLVM
supports, even emitting C code and compiling that on targets that LLVM
doesn't support natively. You can trivially tell that the Kaleidoscope
compiler generates target-independent code because it never queries for
any target-specific information when generating code.

The fact that LLVM provides a compact, target-independent,
representation for code gets a lot of people excited. Unfortunately,
these people are usually thinking about C or a language from the C
family when they are asking questions about language portability. I say
"unfortunately", because there is really no way to make (fully general)
C code portable, other than shipping the source code around (and of
course, C source code is not actually portable in general either - ever
port a really old application from 32- to 64-bits?).

The problem with C (again, in its full generality) is that it is heavily
laden with target specific assumptions. As one simple example, the
preprocessor often destructively removes target-independence from the
code when it processes the input text:

.. code-block:: c

    #ifdef __i386__
      int X = 1;
    #else
      int X = 42;
    #endif

While it is possible to engineer more and more complex solutions to
problems like this, it cannot be solved in full generality in a way that
is better than shipping the actual source code.

That said, there are interesting subsets of C that can be made portable.
If you are willing to fix primitive types to a fixed size (say int =
32-bits, and long = 64-bits), don't care about ABI compatibility with
existing binaries, and are willing to give up some other minor features,
you can have portable code. This can make sense for specialized domains
such as an in-kernel language.

Safety Guarantees
-----------------

Many of the languages above are also "safe" languages: it is impossible
for a program written in Java to corrupt its address space and crash the
process (assuming the JVM has no bugs). Safety is an interesting
property that requires a combination of language design, runtime
support, and often operating system support.

It is certainly possible to implement a safe language in LLVM, but LLVM
IR does not itself guarantee safety. The LLVM IR allows unsafe pointer
casts, use after free bugs, buffer over-runs, and a variety of other
problems. Safety needs to be implemented as a layer on top of LLVM and,
conveniently, several groups have investigated this. Ask on the `LLVM
forums <https://discourse.llvm.org>`_ if you are interested in more details.

Language-Specific Optimizations
-------------------------------

One thing about LLVM that turns off many people is that it does not
solve all the world's problems in one system.  One specific
complaint is that people perceive LLVM as being incapable of performing
high-level language-specific optimization: LLVM "loses too much
information".  Here are a few observations about this:

First, you're right that LLVM does lose information. For example, as of
this writing, there is no way to distinguish in the LLVM IR whether an
SSA-value came from a C "int" or a C "long" on an ILP32 machine (other
than debug info). Both get compiled down to an 'i32' value and the
information about what it came from is lost. The more general issue
here, is that the LLVM type system uses "structural equivalence" instead
of "name equivalence". Another place this surprises people is if you
have two types in a high-level language that have the same structure
(e.g. two different structs that have a single int field): these types
will compile down into a single LLVM type and it will be impossible to
tell what it came from.

Second, while LLVM does lose information, LLVM is not a fixed target: we
continue to enhance and improve it in many different ways. In addition
to adding new features (LLVM did not always support exceptions or debug
info), we also extend the IR to capture important information for
optimization (e.g. whether an argument is sign or zero extended,
information about pointers aliasing, etc). Many of the enhancements are
user-driven: people want LLVM to include some specific feature, so they
go ahead and extend it.

Third, it is *possible and easy* to add language-specific optimizations,
and you have a number of choices in how to do it. As one trivial
example, it is easy to add language-specific optimization passes that
"know" things about code compiled for a language. In the case of the C
family, there is an optimization pass that "knows" about the standard C
library functions. If you call "exit(0)" in main(), it knows that it is
safe to optimize that into "return 0;" because C specifies what the
'exit' function does.

In addition to simple library knowledge, it is possible to embed a
variety of other language-specific information into the LLVM IR. If you
have a specific need and run into a wall, please bring the topic up on
the llvm-dev list. At the very worst, you can always treat LLVM as if it
were a "dumb code generator" and implement the high-level optimizations
you desire in your front-end, on the language-specific AST.

Tips and Tricks
===============

There is a variety of useful tips and tricks that you come to know after
working on/with LLVM that aren't obvious at first glance. Instead of
letting everyone rediscover them, this section talks about some of these
issues.

Implementing portable offsetof/sizeof
-------------------------------------

One interesting thing that comes up, if you are trying to keep the code
generated by your compiler "target independent", is that you often need
to know the size of some LLVM type or the offset of some field in an
llvm structure. For example, you might need to pass the size of a type
into a function that allocates memory.

Unfortunately, this can vary widely across targets: for example the
width of a pointer is trivially target-specific. However, there is a
`clever way to use the getelementptr
instruction <http://nondot.org/sabre/LLVMNotes/SizeOf-OffsetOf-VariableSizedStructs.txt>`_
that allows you to compute this in a portable way.

Garbage Collected Stack Frames
------------------------------

Some languages want to explicitly manage their stack frames, often so
that they are garbage collected or to allow easy implementation of
closures. There are often better ways to implement these features than
explicit stack frames, but `LLVM does support
them, <http://nondot.org/sabre/LLVMNotes/ExplicitlyManagedStackFrames.txt>`_
if you want. It requires your front-end to convert the code into
`Continuation Passing
Style <http://en.wikipedia.org/wiki/Continuation-passing_style>`_ and
the use of tail calls (which LLVM also supports).

