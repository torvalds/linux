=======================================================
Building a JIT: Starting out with KaleidoscopeJIT
=======================================================

.. contents::
   :local:

Chapter 1 Introduction
======================

**Warning: This tutorial is currently being updated to account for ORC API
changes. Only Chapters 1 and 2 are up-to-date.**

**Example code from Chapters 3 to 5 will compile and run, but has not been
updated**

Welcome to Chapter 1 of the "Building an ORC-based JIT in LLVM" tutorial. This
tutorial runs through the implementation of a JIT compiler using LLVM's
On-Request-Compilation (ORC) APIs. It begins with a simplified version of the
KaleidoscopeJIT class used in the
`Implementing a language with LLVM <LangImpl01.html>`_ tutorials and then
introduces new features like concurrent compilation, optimization, lazy
compilation and remote execution.

The goal of this tutorial is to introduce you to LLVM's ORC JIT APIs, show how
these APIs interact with other parts of LLVM, and to teach you how to recombine
them to build a custom JIT that is suited to your use-case.

The structure of the tutorial is:

- Chapter #1: Investigate the simple KaleidoscopeJIT class. This will
  introduce some of the basic concepts of the ORC JIT APIs, including the
  idea of an ORC *Layer*.

- `Chapter #2 <BuildingAJIT2.html>`_: Extend the basic KaleidoscopeJIT by adding
  a new layer that will optimize IR and generated code.

- `Chapter #3 <BuildingAJIT3.html>`_: Further extend the JIT by adding a
  Compile-On-Demand layer to lazily compile IR.

- `Chapter #4 <BuildingAJIT4.html>`_: Improve the laziness of our JIT by
  replacing the Compile-On-Demand layer with a custom layer that uses the ORC
  Compile Callbacks API directly to defer IR-generation until functions are
  called.

- `Chapter #5 <BuildingAJIT5.html>`_: Add process isolation by JITing code into
  a remote process with reduced privileges using the JIT Remote APIs.

To provide input for our JIT we will use a lightly modified version of the
Kaleidoscope REPL from `Chapter 7 <LangImpl07.html>`_ of the "Implementing a
language in LLVM tutorial".

Finally, a word on API generations: ORC is the 3rd generation of LLVM JIT API.
It was preceded by MCJIT, and before that by the (now deleted) legacy JIT.
These tutorials don't assume any experience with these earlier APIs, but
readers acquainted with them will see many familiar elements. Where appropriate
we will make this connection with the earlier APIs explicit to help people who
are transitioning from them to ORC.

JIT API Basics
==============

The purpose of a JIT compiler is to compile code "on-the-fly" as it is needed,
rather than compiling whole programs to disk ahead of time as a traditional
compiler does. To support that aim our initial, bare-bones JIT API will have
just two functions:

1. ``Error addModule(std::unique_ptr<Module> M)``: Make the given IR module
   available for execution.
2. ``Expected<ExecutorSymbolDef> lookup()``: Search for pointers to
   symbols (functions or variables) that have been added to the JIT.

A basic use-case for this API, executing the 'main' function from a module,
will look like:

.. code-block:: c++

  JIT J;
  J.addModule(buildModule());
  auto *Main = J.lookup("main").getAddress().toPtr<int(*)(int, char *[])>();
  int Result = Main();

The APIs that we build in these tutorials will all be variations on this simple
theme. Behind this API we will refine the implementation of the JIT to add
support for concurrent compilation, optimization and lazy compilation.
Eventually we will extend the API itself to allow higher-level program
representations (e.g. ASTs) to be added to the JIT.

KaleidoscopeJIT
===============

In the previous section we described our API, now we examine a simple
implementation of it: The KaleidoscopeJIT class [1]_ that was used in the
`Implementing a language with LLVM <LangImpl01.html>`_ tutorials. We will use
the REPL code from `Chapter 7 <LangImpl07.html>`_ of that tutorial to supply the
input for our JIT: Each time the user enters an expression the REPL will add a
new IR module containing the code for that expression to the JIT. If the
expression is a top-level expression like '1+1' or 'sin(x)', the REPL will also
use the lookup method of our JIT class find and execute the code for the
expression. In later chapters of this tutorial we will modify the REPL to enable
new interactions with our JIT class, but for now we will take this setup for
granted and focus our attention on the implementation of our JIT itself.

Our KaleidoscopeJIT class is defined in the KaleidoscopeJIT.h header. After the
usual include guards and #includes [2]_, we get to the definition of our class:

.. code-block:: c++

  #ifndef LLVM_EXECUTIONENGINE_ORC_KALEIDOSCOPEJIT_H
  #define LLVM_EXECUTIONENGINE_ORC_KALEIDOSCOPEJIT_H

  #include "llvm/ADT/StringRef.h"
  #include "llvm/ExecutionEngine/Orc/CompileUtils.h"
  #include "llvm/ExecutionEngine/Orc/Core.h"
  #include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
  #include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
  #include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
  #include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
  #include "llvm/ExecutionEngine/SectionMemoryManager.h"
  #include "llvm/IR/DataLayout.h"
  #include "llvm/IR/LLVMContext.h"
  #include <memory>

  namespace llvm {
  namespace orc {

  class KaleidoscopeJIT {
  private:
    ExecutionSession ES;
    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer CompileLayer;

    DataLayout DL;
    MangleAndInterner Mangle;
    ThreadSafeContext Ctx;

  public:
    KaleidoscopeJIT(JITTargetMachineBuilder JTMB, DataLayout DL)
        : ObjectLayer(ES,
                      []() { return std::make_unique<SectionMemoryManager>(); }),
          CompileLayer(ES, ObjectLayer, ConcurrentIRCompiler(std::move(JTMB))),
          DL(std::move(DL)), Mangle(ES, this->DL),
          Ctx(std::make_unique<LLVMContext>()) {
      ES.getMainJITDylib().addGenerator(
          cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(DL.getGlobalPrefix())));
    }

Our class begins with six member variables: An ExecutionSession member, ``ES``,
which provides context for our running JIT'd code (including the string pool,
global mutex, and error reporting facilities); An RTDyldObjectLinkingLayer,
``ObjectLayer``, that can be used to add object files to our JIT (though we will
not use it directly); An IRCompileLayer, ``CompileLayer``, that can be used to
add LLVM Modules to our JIT (and which builds on the ObjectLayer), A DataLayout
and MangleAndInterner, ``DL`` and ``Mangle``, that will be used for symbol mangling
(more on that later); and finally an LLVMContext that clients will use when
building IR files for the JIT.

Next up we have our class constructor, which takes a `JITTargetMachineBuilder``
that will be used by our IRCompiler, and a ``DataLayout`` that we will use to
initialize our DL member. The constructor begins by initializing our
ObjectLayer.  The ObjectLayer requires a reference to the ExecutionSession, and
a function object that will build a JIT memory manager for each module that is
added (a JIT memory manager manages memory allocations, memory permissions, and
registration of exception handlers for JIT'd code). For this we use a lambda
that returns a SectionMemoryManager, an off-the-shelf utility that provides all
the basic memory management functionality required for this chapter. Next we
initialize our CompileLayer. The CompileLayer needs three things: (1) A
reference to the ExecutionSession, (2) A reference to our object layer, and (3)
a compiler instance to use to perform the actual compilation from IR to object
files. We use the off-the-shelf ConcurrentIRCompiler utility as our compiler,
which we construct using this constructor's JITTargetMachineBuilder argument.
The ConcurrentIRCompiler utility will use the JITTargetMachineBuilder to build
llvm TargetMachines (which are not thread safe) as needed for compiles. After
this, we initialize our supporting members: ``DL``, ``Mangler`` and ``Ctx`` with
the input DataLayout, the ExecutionSession and DL member, and a new default
constructed LLVMContext respectively. Now that our members have been initialized,
so the one thing that remains to do is to tweak the configuration of the
*JITDylib* that we will store our code in. We want to modify this dylib to
contain not only the symbols that we add to it, but also the symbols from our
REPL process as well. We do this by attaching a
``DynamicLibrarySearchGenerator`` instance using the
``DynamicLibrarySearchGenerator::GetForCurrentProcess`` method.


.. code-block:: c++

  static Expected<std::unique_ptr<KaleidoscopeJIT>> Create() {
    auto JTMB = JITTargetMachineBuilder::detectHost();

    if (!JTMB)
      return JTMB.takeError();

    auto DL = JTMB->getDefaultDataLayoutForTarget();
    if (!DL)
      return DL.takeError();

    return std::make_unique<KaleidoscopeJIT>(std::move(*JTMB), std::move(*DL));
  }

  const DataLayout &getDataLayout() const { return DL; }

  LLVMContext &getContext() { return *Ctx.getContext(); }

Next we have a named constructor, ``Create``, which will build a KaleidoscopeJIT
instance that is configured to generate code for our host process. It does this
by first generating a JITTargetMachineBuilder instance using that classes'
detectHost method and then using that instance to generate a datalayout for
the target process. Each of these operations can fail, so each returns its
result wrapped in an Expected value [3]_ that we must check for error before
continuing. If both operations succeed we can unwrap their results (using the
dereference operator) and pass them into KaleidoscopeJIT's constructor on the
last line of the function.

Following the named constructor we have the ``getDataLayout()`` and
``getContext()`` methods. These are used to make data structures created and
managed by the JIT (especially the LLVMContext) available to the REPL code that
will build our IR modules.

.. code-block:: c++

  void addModule(std::unique_ptr<Module> M) {
    cantFail(CompileLayer.add(ES.getMainJITDylib(),
                              ThreadSafeModule(std::move(M), Ctx)));
  }

  Expected<ExecutorSymbolDef> lookup(StringRef Name) {
    return ES.lookup({&ES.getMainJITDylib()}, Mangle(Name.str()));
  }

Now we come to the first of our JIT API methods: addModule. This method is
responsible for adding IR to the JIT and making it available for execution. In
this initial implementation of our JIT we will make our modules "available for
execution" by adding them to the CompileLayer, which will it turn store the
Module in the main JITDylib. This process will create new symbol table entries
in the JITDylib for each definition in the module, and will defer compilation of
the module until any of its definitions is looked up. Note that this is not lazy
compilation: just referencing a definition, even if it is never used, will be
enough to trigger compilation. In later chapters we will teach our JIT to defer
compilation of functions until they're actually called.  To add our Module we
must first wrap it in a ThreadSafeModule instance, which manages the lifetime of
the Module's LLVMContext (our Ctx member) in a thread-friendly way. In our
example, all modules will share the Ctx member, which will exist for the
duration of the JIT. Once we switch to concurrent compilation in later chapters
we will use a new context per module.

Our last method is ``lookup``, which allows us to look up addresses for
function and variable definitions added to the JIT based on their symbol names.
As noted above, lookup will implicitly trigger compilation for any symbol
that has not already been compiled. Our lookup method calls through to
`ExecutionSession::lookup`, passing in a list of dylibs to search (in our case
just the main dylib), and the symbol name to search for, with a twist: We have
to *mangle* the name of the symbol we're searching for first. The ORC JIT
components use mangled symbols internally the same way a static compiler and
linker would, rather than using plain IR symbol names. This allows JIT'd code
to interoperate easily with precompiled code in the application or shared
libraries. The kind of mangling will depend on the DataLayout, which in turn
depends on the target platform. To allow us to remain portable and search based
on the un-mangled name, we just re-produce this mangling ourselves using our
``Mangle`` member function object.

This brings us to the end of Chapter 1 of Building a JIT. You now have a basic
but fully functioning JIT stack that you can use to take LLVM IR and make it
executable within the context of your JIT process. In the next chapter we'll
look at how to extend this JIT to produce better quality code, and in the
process take a deeper look at the ORC layer concept.

`Next: Extending the KaleidoscopeJIT <BuildingAJIT2.html>`_

Full Code Listing
=================

Here is the complete code listing for our running example. To build this
example, use:

.. code-block:: bash

    # Compile
    clang++ -g toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o toy
    # Run
    ./toy

Here is the code:

.. literalinclude:: ../../examples/Kaleidoscope/BuildingAJIT/Chapter1/KaleidoscopeJIT.h
   :language: c++

.. [1] Actually we use a cut-down version of KaleidoscopeJIT that makes a
       simplifying assumption: symbols cannot be re-defined. This will make it
       impossible to re-define symbols in the REPL, but will make our symbol
       lookup logic simpler. Re-introducing support for symbol redefinition is
       left as an exercise for the reader. (The KaleidoscopeJIT.h used in the
       original tutorials will be a helpful reference).

.. [2] +-----------------------------+-----------------------------------------------+
       |         File                |               Reason for inclusion            |
       +=============================+===============================================+
       |       CompileUtils.h        | Provides the SimpleCompiler class.            |
       +-----------------------------+-----------------------------------------------+
       |           Core.h            | Core utilities such as ExecutionSession and   |
       |                             | JITDylib.                                     |
       +-----------------------------+-----------------------------------------------+
       |      ExecutionUtils.h       | Provides the DynamicLibrarySearchGenerator    |
       |                             | class.                                        |
       +-----------------------------+-----------------------------------------------+
       |      IRCompileLayer.h       | Provides the IRCompileLayer class.            |
       +-----------------------------+-----------------------------------------------+
       |  JITTargetMachineBuilder.h  | Provides the JITTargetMachineBuilder class.   |
       +-----------------------------+-----------------------------------------------+
       | RTDyldObjectLinkingLayer.h  | Provides the RTDyldObjectLinkingLayer class.  |
       +-----------------------------+-----------------------------------------------+
       |   SectionMemoryManager.h    | Provides the SectionMemoryManager class.      |
       +-----------------------------+-----------------------------------------------+
       |        DataLayout.h         | Provides the DataLayout class.                |
       +-----------------------------+-----------------------------------------------+
       |        LLVMContext.h        | Provides the LLVMContext class.               |
       +-----------------------------+-----------------------------------------------+

.. [3] See the ErrorHandling section in the LLVM Programmer's Manual
       (https://llvm.org/docs/ProgrammersManual.html#error-handling)
