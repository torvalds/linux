=====================================================================
Building a JIT: Adding Optimizations -- An introduction to ORC Layers
=====================================================================

.. contents::
   :local:

**This tutorial is under active development. It is incomplete and details may
change frequently.** Nonetheless we invite you to try it out as it stands, and
we welcome any feedback.

Chapter 2 Introduction
======================

**Warning: This tutorial is currently being updated to account for ORC API
changes. Only Chapters 1 and 2 are up-to-date.**

**Example code from Chapters 3 to 5 will compile and run, but has not been
updated**

Welcome to Chapter 2 of the "Building an ORC-based JIT in LLVM" tutorial. In
`Chapter 1 <BuildingAJIT1.html>`_ of this series we examined a basic JIT
class, KaleidoscopeJIT, that could take LLVM IR modules as input and produce
executable code in memory. KaleidoscopeJIT was able to do this with relatively
little code by composing two off-the-shelf *ORC layers*: IRCompileLayer and
ObjectLinkingLayer, to do much of the heavy lifting.

In this layer we'll learn more about the ORC layer concept by using a new layer,
IRTransformLayer, to add IR optimization support to KaleidoscopeJIT.

Optimizing Modules using the IRTransformLayer
=============================================

In `Chapter 4 <LangImpl04.html>`_ of the "Implementing a language with LLVM"
tutorial series the llvm *FunctionPassManager* is introduced as a means for
optimizing LLVM IR. Interested readers may read that chapter for details, but
in short: to optimize a Module we create an llvm::FunctionPassManager
instance, configure it with a set of optimizations, then run the PassManager on
a Module to mutate it into a (hopefully) more optimized but semantically
equivalent form. In the original tutorial series the FunctionPassManager was
created outside the KaleidoscopeJIT and modules were optimized before being
added to it. In this Chapter we will make optimization a phase of our JIT
instead. For now this will provide us a motivation to learn more about ORC
layers, but in the long term making optimization part of our JIT will yield an
important benefit: When we begin lazily compiling code (i.e. deferring
compilation of each function until the first time it's run) having
optimization managed by our JIT will allow us to optimize lazily too, rather
than having to do all our optimization up-front.

To add optimization support to our JIT we will take the KaleidoscopeJIT from
Chapter 1 and compose an ORC *IRTransformLayer* on top. We will look at how the
IRTransformLayer works in more detail below, but the interface is simple: the
constructor for this layer takes a reference to the execution session and the
layer below (as all layers do) plus an *IR optimization function* that it will
apply to each Module that is added via addModule:

.. code-block:: c++

  class KaleidoscopeJIT {
  private:
    ExecutionSession ES;
    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer CompileLayer;
    IRTransformLayer TransformLayer;

    DataLayout DL;
    MangleAndInterner Mangle;
    ThreadSafeContext Ctx;

  public:

    KaleidoscopeJIT(JITTargetMachineBuilder JTMB, DataLayout DL)
        : ObjectLayer(ES,
                      []() { return std::make_unique<SectionMemoryManager>(); }),
          CompileLayer(ES, ObjectLayer, ConcurrentIRCompiler(std::move(JTMB))),
          TransformLayer(ES, CompileLayer, optimizeModule),
          DL(std::move(DL)), Mangle(ES, this->DL),
          Ctx(std::make_unique<LLVMContext>()) {
      ES.getMainJITDylib().addGenerator(
          cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(DL.getGlobalPrefix())));
    }

Our extended KaleidoscopeJIT class starts out the same as it did in Chapter 1,
but after the CompileLayer we introduce a new member, TransformLayer, which sits
on top of our CompileLayer. We initialize our OptimizeLayer with a reference to
the ExecutionSession and output layer (standard practice for layers), along with
a *transform function*. For our transform function we supply our classes
optimizeModule static method.

.. code-block:: c++

  // ...
  return cantFail(OptimizeLayer.addModule(std::move(M),
                                          std::move(Resolver)));
  // ...

Next we need to update our addModule method to replace the call to
``CompileLayer::add`` with a call to ``OptimizeLayer::add`` instead.

.. code-block:: c++

  static Expected<ThreadSafeModule>
  optimizeModule(ThreadSafeModule M, const MaterializationResponsibility &R) {
    // Create a function pass manager.
    auto FPM = std::make_unique<legacy::FunctionPassManager>(M.get());

    // Add some optimizations.
    FPM->add(createInstructionCombiningPass());
    FPM->add(createReassociatePass());
    FPM->add(createGVNPass());
    FPM->add(createCFGSimplificationPass());
    FPM->doInitialization();

    // Run the optimizations over all functions in the module being added to
    // the JIT.
    for (auto &F : *M)
      FPM->run(F);

    return M;
  }

At the bottom of our JIT we add a private method to do the actual optimization:
*optimizeModule*. This function takes the module to be transformed as input (as
a ThreadSafeModule) along with a reference to a reference to a new class:
``MaterializationResponsibility``. The MaterializationResponsibility argument
can be used to query JIT state for the module being transformed, such as the set
of definitions in the module that JIT'd code is actively trying to call/access.
For now we will ignore this argument and use a standard optimization
pipeline. To do this we set up a FunctionPassManager, add some passes to it, run
it over every function in the module, and then return the mutated module. The
specific optimizations are the same ones used in `Chapter 4 <LangImpl04.html>`_
of the "Implementing a language with LLVM" tutorial series. Readers may visit
that chapter for a more in-depth discussion of these, and of IR optimization in
general.

And that's it in terms of changes to KaleidoscopeJIT: When a module is added via
addModule the OptimizeLayer will call our optimizeModule function before passing
the transformed module on to the CompileLayer below. Of course, we could have
called optimizeModule directly in our addModule function and not gone to the
bother of using the IRTransformLayer, but doing so gives us another opportunity
to see how layers compose. It also provides a neat entry point to the *layer*
concept itself, because IRTransformLayer is one of the simplest layers that
can be implemented.

.. code-block:: c++

  // From IRTransformLayer.h:
  class IRTransformLayer : public IRLayer {
  public:
    using TransformFunction = std::function<Expected<ThreadSafeModule>(
        ThreadSafeModule, const MaterializationResponsibility &R)>;

    IRTransformLayer(ExecutionSession &ES, IRLayer &BaseLayer,
                     TransformFunction Transform = identityTransform);

    void setTransform(TransformFunction Transform) {
      this->Transform = std::move(Transform);
    }

    static ThreadSafeModule
    identityTransform(ThreadSafeModule TSM,
                      const MaterializationResponsibility &R) {
      return TSM;
    }

    void emit(MaterializationResponsibility R, ThreadSafeModule TSM) override;

  private:
    IRLayer &BaseLayer;
    TransformFunction Transform;
  };

  // From IRTransformLayer.cpp:

  IRTransformLayer::IRTransformLayer(ExecutionSession &ES,
                                     IRLayer &BaseLayer,
                                     TransformFunction Transform)
      : IRLayer(ES), BaseLayer(BaseLayer), Transform(std::move(Transform)) {}

  void IRTransformLayer::emit(MaterializationResponsibility R,
                              ThreadSafeModule TSM) {
    assert(TSM.getModule() && "Module must not be null");

    if (auto TransformedTSM = Transform(std::move(TSM), R))
      BaseLayer.emit(std::move(R), std::move(*TransformedTSM));
    else {
      R.failMaterialization();
      getExecutionSession().reportError(TransformedTSM.takeError());
    }
  }

This is the whole definition of IRTransformLayer, from
``llvm/include/llvm/ExecutionEngine/Orc/IRTransformLayer.h`` and
``llvm/lib/ExecutionEngine/Orc/IRTransformLayer.cpp``.  This class is concerned
with two very simple jobs: (1) Running every IR Module that is emitted via this
layer through the transform function object, and (2) implementing the ORC
``IRLayer`` interface (which itself conforms to the general ORC Layer concept,
more on that below). Most of the class is straightforward: a typedef for the
transform function, a constructor to initialize the members, a setter for the
transform function value, and a default no-op transform. The most important
method is ``emit`` as this is half of our IRLayer interface. The emit method
applies our transform to each module that it is called on and, if the transform
succeeds, passes the transformed module to the base layer. If the transform
fails, our emit function calls
``MaterializationResponsibility::failMaterialization`` (this JIT clients who
may be waiting on other threads know that the code they were waiting for has
failed to compile) and logs the error with the execution session before bailing
out.

The other half of the IRLayer interface we inherit unmodified from the IRLayer
class:

.. code-block:: c++

  Error IRLayer::add(JITDylib &JD, ThreadSafeModule TSM, VModuleKey K) {
    return JD.define(std::make_unique<BasicIRLayerMaterializationUnit>(
        *this, std::move(K), std::move(TSM)));
  }

This code, from ``llvm/lib/ExecutionEngine/Orc/Layer.cpp``, adds a
ThreadSafeModule to a given JITDylib by wrapping it up in a
``MaterializationUnit`` (in this case a ``BasicIRLayerMaterializationUnit``).
Most layers that derived from IRLayer can rely on this default implementation
of the ``add`` method.

These two operations, ``add`` and ``emit``, together constitute the layer
concept: A layer is a way to wrap a part of a compiler pipeline (in this case
the "opt" phase of an LLVM compiler) whose API is opaque to ORC with an
interface that ORC can call as needed. The add method takes an
module in some input program representation (in this case an LLVM IR module)
and stores it in the target ``JITDylib``, arranging for it to be passed back
to the layer's emit method when any symbol defined by that module is requested.
Each layer can complete its own work by calling the ``emit`` method of its base
layer. For example, in this tutorial our IRTransformLayer calls through to
our IRCompileLayer to compile the transformed IR, and our IRCompileLayer in
turn calls our ObjectLayer to link the object file produced by our compiler.

So far we have learned how to optimize and compile our LLVM IR, but we have
not focused on when compilation happens. Our current REPL optimizes and
compiles each function as soon as it is referenced by any other code,
regardless of whether it is ever called at runtime. In the next chapter we
will introduce a fully lazy compilation, in which functions are not compiled
until they are first called at run-time. At this point the trade-offs get much
more interesting: the lazier we are, the quicker we can start executing the
first function, but the more often we will have to pause to compile newly
encountered functions. If we only code-gen lazily, but optimize eagerly, we
will have a longer startup time (as everything is optimized at that time) but
relatively short pauses as each function just passes through code-gen. If we
both optimize and code-gen lazily we can start executing the first function
more quickly, but we will have longer pauses as each function has to be both
optimized and code-gen'd when it is first executed. Things become even more
interesting if we consider interprocedural optimizations like inlining, which
must be performed eagerly. These are complex trade-offs, and there is no
one-size-fits all solution to them, but by providing composable layers we leave
the decisions to the person implementing the JIT, and make it easy for them to
experiment with different configurations.

`Next: Adding Per-function Lazy Compilation <BuildingAJIT3.html>`_

Full Code Listing
=================

Here is the complete code listing for our running example with an
IRTransformLayer added to enable optimization. To build this example, use:

.. code-block:: bash

    # Compile
    clang++ -g toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o toy
    # Run
    ./toy

Here is the code:

.. literalinclude:: ../../examples/Kaleidoscope/BuildingAJIT/Chapter2/KaleidoscopeJIT.h
   :language: c++
