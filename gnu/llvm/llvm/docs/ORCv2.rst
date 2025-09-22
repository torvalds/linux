===============================
ORC Design and Implementation
===============================

.. contents::
   :local:

Introduction
============

This document aims to provide a high-level overview of the design and
implementation of the ORC JIT APIs. Except where otherwise stated all discussion
refers to the modern ORCv2 APIs (available since LLVM 7). Clients wishing to
transition from OrcV1 should see Section :ref:`transitioning_orcv1_to_orcv2`.

Use-cases
=========

ORC provides a modular API for building JIT compilers. There are a number
of use cases for such an API. For example:

1. The LLVM tutorials use a simple ORC-based JIT class to execute expressions
compiled from a toy language: Kaleidoscope.

2. The LLVM debugger, LLDB, uses a cross-compiling JIT for expression
evaluation. In this use case, cross compilation allows expressions compiled
in the debugger process to be executed on the debug target process, which may
be on a different device/architecture.

3. In high-performance JITs (e.g. JVMs, Julia) that want to make use of LLVM's
optimizations within an existing JIT infrastructure.

4. In interpreters and REPLs, e.g. Cling (C++) and the Swift interpreter.

By adopting a modular, library-based design we aim to make ORC useful in as many
of these contexts as possible.

Features
========

ORC provides the following features:

**JIT-linking**
  ORC provides APIs to link relocatable object files (COFF, ELF, MachO) [1]_
  into a target process at runtime. The target process may be the same process
  that contains the JIT session object and jit-linker, or may be another process
  (even one running on a different machine or architecture) that communicates
  with the JIT via RPC.

**LLVM IR compilation**
  ORC provides off the shelf components (IRCompileLayer, SimpleCompiler,
  ConcurrentIRCompiler) that make it easy to add LLVM IR to a JIT'd process.

**Eager and lazy compilation**
  By default, ORC will compile symbols as soon as they are looked up in the JIT
  session object (``ExecutionSession``). Compiling eagerly by default makes it
  easy to use ORC as an in-memory compiler for an existing JIT (similar to how
  MCJIT is commonly used). However ORC also provides built-in support for lazy
  compilation via lazy-reexports (see :ref:`Laziness`).

**Support for Custom Compilers and Program Representations**
  Clients can supply custom compilers for each symbol that they define in their
  JIT session. ORC will run the user-supplied compiler when the a definition of
  a symbol is needed. ORC is actually fully language agnostic: LLVM IR is not
  treated specially, and is supported via the same wrapper mechanism (the
  ``MaterializationUnit`` class) that is used for custom compilers.

**Concurrent JIT'd code** and **Concurrent Compilation**
  JIT'd code may be executed in multiple threads, may spawn new threads, and may
  re-enter the ORC (e.g. to request lazy compilation) concurrently from multiple
  threads. Compilers launched my ORC can run concurrently (provided the client
  sets up an appropriate dispatcher). Built-in dependency tracking ensures that
  ORC does not release pointers to JIT'd code or data until all dependencies
  have also been JIT'd and they are safe to call or use.

**Removable Code**
  Resources for JIT'd program representations

**Orthogonality** and **Composability**
  Each of the features above can be used independently. It is possible to put
  ORC components together to make a non-lazy, in-process, single threaded JIT
  or a lazy, out-of-process, concurrent JIT, or anything in between.

LLJIT and LLLazyJIT
===================

ORC provides two basic JIT classes off-the-shelf. These are useful both as
examples of how to assemble ORC components to make a JIT, and as replacements
for earlier LLVM JIT APIs (e.g. MCJIT).

The LLJIT class uses an IRCompileLayer and RTDyldObjectLinkingLayer to support
compilation of LLVM IR and linking of relocatable object files. All operations
are performed eagerly on symbol lookup (i.e. a symbol's definition is compiled
as soon as you attempt to look up its address). LLJIT is a suitable replacement
for MCJIT in most cases (note: some more advanced features, e.g.
JITEventListeners are not supported yet).

The LLLazyJIT extends LLJIT and adds a CompileOnDemandLayer to enable lazy
compilation of LLVM IR. When an LLVM IR module is added via the addLazyIRModule
method, function bodies in that module will not be compiled until they are first
called. LLLazyJIT aims to provide a replacement of LLVM's original (pre-MCJIT)
JIT API.

LLJIT and LLLazyJIT instances can be created using their respective builder
classes: LLJITBuilder and LLazyJITBuilder. For example, assuming you have a
module ``M`` loaded on a ThreadSafeContext ``Ctx``:

.. code-block:: c++

  // Try to detect the host arch and construct an LLJIT instance.
  auto JIT = LLJITBuilder().create();

  // If we could not construct an instance, return an error.
  if (!JIT)
    return JIT.takeError();

  // Add the module.
  if (auto Err = JIT->addIRModule(TheadSafeModule(std::move(M), Ctx)))
    return Err;

  // Look up the JIT'd code entry point.
  auto EntrySym = JIT->lookup("entry");
  if (!EntrySym)
    return EntrySym.takeError();

  // Cast the entry point address to a function pointer.
  auto *Entry = EntrySym.getAddress().toPtr<void(*)()>();

  // Call into JIT'd code.
  Entry();

The builder classes provide a number of configuration options that can be
specified before the JIT instance is constructed. For example:

.. code-block:: c++

  // Build an LLLazyJIT instance that uses four worker threads for compilation,
  // and jumps to a specific error handler (rather than null) on lazy compile
  // failures.

  void handleLazyCompileFailure() {
    // JIT'd code will jump here if lazy compilation fails, giving us an
    // opportunity to exit or throw an exception into JIT'd code.
    throw JITFailed();
  }

  auto JIT = LLLazyJITBuilder()
               .setNumCompileThreads(4)
               .setLazyCompileFailureAddr(
                   ExecutorAddr::fromPtr(&handleLazyCompileFailure))
               .create();

  // ...

For users wanting to get started with LLJIT a minimal example program can be
found at ``llvm/examples/HowToUseLLJIT``.

Design Overview
===============

ORC's JIT program model aims to emulate the linking and symbol resolution
rules used by the static and dynamic linkers. This allows ORC to JIT
arbitrary LLVM IR, including IR produced by an ordinary static compiler (e.g.
clang) that uses constructs like symbol linkage and visibility, and weak [3]_
and common symbol definitions.

To see how this works, imagine a program ``foo`` which links against a pair
of dynamic libraries: ``libA`` and ``libB``. On the command line, building this
program might look like:

.. code-block:: bash

  $ clang++ -shared -o libA.dylib a1.cpp a2.cpp
  $ clang++ -shared -o libB.dylib b1.cpp b2.cpp
  $ clang++ -o myapp myapp.cpp -L. -lA -lB
  $ ./myapp

In ORC, this would translate into API calls on a hypothetical CXXCompilingLayer
(with error checking omitted for brevity) as:

.. code-block:: c++

  ExecutionSession ES;
  RTDyldObjectLinkingLayer ObjLinkingLayer(
      ES, []() { return std::make_unique<SectionMemoryManager>(); });
  CXXCompileLayer CXXLayer(ES, ObjLinkingLayer);

  // Create JITDylib "A" and add code to it using the CXX layer.
  auto &LibA = ES.createJITDylib("A");
  CXXLayer.add(LibA, MemoryBuffer::getFile("a1.cpp"));
  CXXLayer.add(LibA, MemoryBuffer::getFile("a2.cpp"));

  // Create JITDylib "B" and add code to it using the CXX layer.
  auto &LibB = ES.createJITDylib("B");
  CXXLayer.add(LibB, MemoryBuffer::getFile("b1.cpp"));
  CXXLayer.add(LibB, MemoryBuffer::getFile("b2.cpp"));

  // Create and specify the search order for the main JITDylib. This is
  // equivalent to a "links against" relationship in a command-line link.
  auto &MainJD = ES.createJITDylib("main");
  MainJD.addToLinkOrder(&LibA);
  MainJD.addToLinkOrder(&LibB);
  CXXLayer.add(MainJD, MemoryBuffer::getFile("main.cpp"));

  // Look up the JIT'd main, cast it to a function pointer, then call it.
  auto MainSym = ExitOnErr(ES.lookup({&MainJD}, "main"));
  auto *Main = MainSym.getAddress().toPtr<int(*)(int, char *[])>();

  int Result = Main(...);

This example tells us nothing about *how* or *when* compilation will happen.
That will depend on the implementation of the hypothetical CXXCompilingLayer.
The same linker-based symbol resolution rules will apply regardless of that
implementation, however. For example, if a1.cpp and a2.cpp both define a
function "foo" then ORCv2 will generate a duplicate definition error. On the
other hand, if a1.cpp and b1.cpp both define "foo" there is no error (different
dynamic libraries may define the same symbol). If main.cpp refers to "foo", it
should bind to the definition in LibA rather than the one in LibB, since
main.cpp is part of the "main" dylib, and the main dylib links against LibA
before LibB.

Many JIT clients will have no need for this strict adherence to the usual
ahead-of-time linking rules, and should be able to get by just fine by putting
all of their code in a single JITDylib. However, clients who want to JIT code
for languages/projects that traditionally rely on ahead-of-time linking (e.g.
C++) will find that this feature makes life much easier.

Symbol lookup in ORC serves two other important functions, beyond providing
addresses for symbols: (1) It triggers compilation of the symbol(s) searched for
(if they have not been compiled already), and (2) it provides the
synchronization mechanism for concurrent compilation. The pseudo-code for the
lookup process is:

.. code-block:: none

  construct a query object from a query set and query handler
  lock the session
  lodge query against requested symbols, collect required materializers (if any)
  unlock the session
  dispatch materializers (if any)

In this context a materializer is something that provides a working definition
of a symbol upon request. Usually materializers are just wrappers for compilers,
but they may also wrap a jit-linker directly (if the program representation
backing the definitions is an object file), or may even be a class that writes
bits directly into memory (for example, if the definitions are
stubs). Materialization is the blanket term for any actions (compiling, linking,
splatting bits, registering with runtimes, etc.) that are required to generate a
symbol definition that is safe to call or access.

As each materializer completes its work it notifies the JITDylib, which in turn
notifies any query objects that are waiting on the newly materialized
definitions. Each query object maintains a count of the number of symbols that
it is still waiting on, and once this count reaches zero the query object calls
the query handler with a *SymbolMap* (a map of symbol names to addresses)
describing the result. If any symbol fails to materialize the query immediately
calls the query handler with an error.

The collected materialization units are sent to the ExecutionSession to be
dispatched, and the dispatch behavior can be set by the client. By default each
materializer is run on the calling thread. Clients are free to create new
threads to run materializers, or to send the work to a work queue for a thread
pool (this is what LLJIT/LLLazyJIT do).

Top Level APIs
==============

Many of ORC's top-level APIs are visible in the example above:

- *ExecutionSession* represents the JIT'd program and provides context for the
  JIT: It contains the JITDylibs, error reporting mechanisms, and dispatches the
  materializers.

- *JITDylibs* provide the symbol tables.

- *Layers* (ObjLinkingLayer and CXXLayer) are wrappers around compilers and
  allow clients to add uncompiled program representations supported by those
  compilers to JITDylibs.

- *ResourceTrackers* allow you to remove code.

Several other important APIs are used explicitly. JIT clients need not be aware
of them, but Layer authors will use them:

- *MaterializationUnit* - When XXXLayer::add is invoked it wraps the given
  program representation (in this example, C++ source) in a MaterializationUnit,
  which is then stored in the JITDylib. MaterializationUnits are responsible for
  describing the definitions they provide, and for unwrapping the program
  representation and passing it back to the layer when compilation is required
  (this ownership shuffle makes writing thread-safe layers easier, since the
  ownership of the program representation will be passed back on the stack,
  rather than having to be fished out of a Layer member, which would require
  synchronization).

- *MaterializationResponsibility* - When a MaterializationUnit hands a program
  representation back to the layer it comes with an associated
  MaterializationResponsibility object. This object tracks the definitions
  that must be materialized and provides a way to notify the JITDylib once they
  are either successfully materialized or a failure occurs.

Absolute Symbols, Aliases, and Reexports
========================================

ORC makes it easy to define symbols with absolute addresses, or symbols that
are simply aliases of other symbols:

Absolute Symbols
----------------

Absolute symbols are symbols that map directly to addresses without requiring
further materialization, for example: "foo" = 0x1234. One use case for
absolute symbols is allowing resolution of process symbols. E.g.

.. code-block:: c++

  JD.define(absoluteSymbols(SymbolMap({
      { Mangle("printf"),
        { ExecutorAddr::fromPtr(&printf),
          JITSymbolFlags::Callable } }
    });

With this mapping established code added to the JIT can refer to printf
symbolically rather than requiring the address of printf to be "baked in".
This in turn allows cached versions of the JIT'd code (e.g. compiled objects)
to be re-used across JIT sessions as the JIT'd code no longer changes, only the
absolute symbol definition does.

For process and library symbols the DynamicLibrarySearchGenerator utility (See
:ref:`How to Add Process and Library Symbols to JITDylibs
<ProcessAndLibrarySymbols>`) can be used to automatically build absolute
symbol mappings for you. However the absoluteSymbols function is still useful
for making non-global objects in your JIT visible to JIT'd code. For example,
imagine that your JIT standard library needs access to your JIT object to make
some calls. We could bake the address of your object into the library, but then
it would need to be recompiled for each session:

.. code-block:: c++

  // From standard library for JIT'd code:

  class MyJIT {
  public:
    void log(const char *Msg);
  };

  void log(const char *Msg) { ((MyJIT*)0x1234)->log(Msg); }

We can turn this into a symbolic reference in the JIT standard library:

.. code-block:: c++

  extern MyJIT *__MyJITInstance;

  void log(const char *Msg) { __MyJITInstance->log(Msg); }

And then make our JIT object visible to the JIT standard library with an
absolute symbol definition when the JIT is started:

.. code-block:: c++

  MyJIT J = ...;

  auto &JITStdLibJD = ... ;

  JITStdLibJD.define(absoluteSymbols(SymbolMap({
      { Mangle("__MyJITInstance"),
        { ExecutorAddr::fromPtr(&J), JITSymbolFlags() } }
    });

Aliases and Reexports
---------------------

Aliases and reexports allow you to define new symbols that map to existing
symbols. This can be useful for changing linkage relationships between symbols
across sessions without having to recompile code. For example, imagine that
JIT'd code has access to a log function, ``void log(const char*)`` for which
there are two implementations in the JIT standard library: ``log_fast`` and
``log_detailed``. Your JIT can choose which one of these definitions will be
used when the ``log`` symbol is referenced by setting up an alias at JIT startup
time:

.. code-block:: c++

  auto &JITStdLibJD = ... ;

  auto LogImplementationSymbol =
   Verbose ? Mangle("log_detailed") : Mangle("log_fast");

  JITStdLibJD.define(
    symbolAliases(SymbolAliasMap({
        { Mangle("log"),
          { LogImplementationSymbol
            JITSymbolFlags::Exported | JITSymbolFlags::Callable } }
      });

The ``symbolAliases`` function allows you to define aliases within a single
JITDylib. The ``reexports`` function provides the same functionality, but
operates across JITDylib boundaries. E.g.

.. code-block:: c++

  auto &JD1 = ... ;
  auto &JD2 = ... ;

  // Make 'bar' in JD2 an alias for 'foo' from JD1.
  JD2.define(
    reexports(JD1, SymbolAliasMap({
        { Mangle("bar"), { Mangle("foo"), JITSymbolFlags::Exported } }
      });

The reexports utility can be handy for composing a single JITDylib interface by
re-exporting symbols from several other JITDylibs.

.. _Laziness:

Laziness
========

Laziness in ORC is provided by a utility called "lazy reexports". A lazy
reexport is similar to a regular reexport or alias: It provides a new name for
an existing symbol. Unlike regular reexports however, lookups of lazy reexports
do not trigger immediate materialization of the reexported symbol. Instead, they
only trigger materialization of a function stub. This function stub is
initialized to point at a *lazy call-through*, which provides reentry into the
JIT. If the stub is called at runtime then the lazy call-through will look up
the reexported symbol (triggering materialization for it if necessary), update
the stub (to call directly to the reexported symbol on subsequent calls), and
then return via the reexported symbol. By re-using the existing symbol lookup
mechanism, lazy reexports inherit the same concurrency guarantees: calls to lazy
reexports can be made from multiple threads concurrently, and the reexported
symbol can be any state of compilation (uncompiled, already in the process of
being compiled, or already compiled) and the call will succeed. This allows
laziness to be safely mixed with features like remote compilation, concurrent
compilation, concurrent JIT'd code, and speculative compilation.

There is one other key difference between regular reexports and lazy reexports
that some clients must be aware of: The address of a lazy reexport will be
*different* from the address of the reexported symbol (whereas a regular
reexport is guaranteed to have the same address as the reexported symbol).
Clients who care about pointer equality will generally want to use the address
of the reexport as the canonical address of the reexported symbol. This will
allow the address to be taken without forcing materialization of the reexport.

Usage example:

If JITDylib ``JD`` contains definitions for symbols ``foo_body`` and
``bar_body``, we can create lazy entry points ``Foo`` and ``Bar`` in JITDylib
``JD2`` by calling:

.. code-block:: c++

  auto ReexportFlags = JITSymbolFlags::Exported | JITSymbolFlags::Callable;
  JD2.define(
    lazyReexports(CallThroughMgr, StubsMgr, JD,
                  SymbolAliasMap({
                    { Mangle("foo"), { Mangle("foo_body"), ReexportedFlags } },
                    { Mangle("bar"), { Mangle("bar_body"), ReexportedFlags } }
                  }));

A full example of how to use lazyReexports with the LLJIT class can be found at
``llvm/examples/OrcV2Examples/LLJITWithLazyReexports``.

Supporting Custom Compilers
===========================

TBD.

.. _transitioning_orcv1_to_orcv2:

Transitioning from ORCv1 to ORCv2
=================================

Since LLVM 7.0, new ORC development work has focused on adding support for
concurrent JIT compilation. The new APIs (including new layer interfaces and
implementations, and new utilities) that support concurrency are collectively
referred to as ORCv2, and the original, non-concurrent layers and utilities
are now referred to as ORCv1.

The majority of the ORCv1 layers and utilities were renamed with a 'Legacy'
prefix in LLVM 8.0, and have deprecation warnings attached in LLVM 9.0. In LLVM
12.0 ORCv1 will be removed entirely.

Transitioning from ORCv1 to ORCv2 should be easy for most clients. Most of the
ORCv1 layers and utilities have ORCv2 counterparts [2]_ that can be directly
substituted. However there are some design differences between ORCv1 and ORCv2
to be aware of:

  1. ORCv2 fully adopts the JIT-as-linker model that began with MCJIT. Modules
     (and other program representations, e.g. Object Files)  are no longer added
     directly to JIT classes or layers. Instead, they are added to ``JITDylib``
     instances *by* layers. The ``JITDylib`` determines *where* the definitions
     reside, the layers determine *how* the definitions will be compiled.
     Linkage relationships between ``JITDylibs`` determine how inter-module
     references are resolved, and symbol resolvers are no longer used. See the
     section `Design Overview`_ for more details.

     Unless multiple JITDylibs are needed to model linkage relationships, ORCv1
     clients should place all code in a single JITDylib.
     MCJIT clients should use LLJIT (see `LLJIT and LLLazyJIT`_), and can place
     code in LLJIT's default created main JITDylib (See
     ``LLJIT::getMainJITDylib()``).

  2. All JIT stacks now need an ``ExecutionSession`` instance. ExecutionSession
     manages the string pool, error reporting, synchronization, and symbol
     lookup.

  3. ORCv2 uses uniqued strings (``SymbolStringPtr`` instances) rather than
     string values in order to reduce memory overhead and improve lookup
     performance. See the subsection `How to manage symbol strings`_.

  4. IR layers require ThreadSafeModule instances, rather than
     std::unique_ptr<Module>s. ThreadSafeModule is a wrapper that ensures that
     Modules that use the same LLVMContext are not accessed concurrently.
     See `How to use ThreadSafeModule and ThreadSafeContext`_.

  5. Symbol lookup is no longer handled by layers. Instead, there is a
     ``lookup`` method on JITDylib that takes a list of JITDylibs to scan.

     .. code-block:: c++

       ExecutionSession ES;
       JITDylib &JD1 = ...;
       JITDylib &JD2 = ...;

       auto Sym = ES.lookup({&JD1, &JD2}, ES.intern("_main"));

  6. The removeModule/removeObject methods are replaced by
     ``ResourceTracker::remove``.
     See the subsection `How to remove code`_.

For code examples and suggestions of how to use the ORCv2 APIs, please see
the section `How-tos`_.

How-tos
=======

How to manage symbol strings
----------------------------

Symbol strings in ORC are uniqued to improve lookup performance, reduce memory
overhead, and allow symbol names to function as efficient keys. To get the
unique ``SymbolStringPtr`` for a string value, call the
``ExecutionSession::intern`` method:

  .. code-block:: c++

    ExecutionSession ES;
    /// ...
    auto MainSymbolName = ES.intern("main");

If you wish to perform lookup using the C/IR name of a symbol you will also
need to apply the platform linker-mangling before interning the string. On
Linux this mangling is a no-op, but on other platforms it usually involves
adding a prefix to the string (e.g. '_' on Darwin). The mangling scheme is
based on the DataLayout for the target. Given a DataLayout and an
ExecutionSession, you can create a MangleAndInterner function object that
will perform both jobs for you:

  .. code-block:: c++

    ExecutionSession ES;
    const DataLayout &DL = ...;
    MangleAndInterner Mangle(ES, DL);

    // ...

    // Portable IR-symbol-name lookup:
    auto Sym = ES.lookup({&MainJD}, Mangle("main"));

How to create JITDylibs and set up linkage relationships
--------------------------------------------------------

In ORC, all symbol definitions reside in JITDylibs. JITDylibs are created by
calling the ``ExecutionSession::createJITDylib`` method with a unique name:

  .. code-block:: c++

    ExecutionSession ES;
    auto &JD = ES.createJITDylib("libFoo.dylib");

The JITDylib is owned by the ``ExecutionEngine`` instance and will be freed
when it is destroyed.

How to remove code
------------------

To remove an individual module from a JITDylib it must first be added using an
explicit ``ResourceTracker``. The module can then be removed by calling
``ResourceTracker::remove``:

  .. code-block:: c++

    auto &JD = ... ;
    auto M = ... ;

    auto RT = JD.createResourceTracker();
    Layer.add(RT, std::move(M)); // Add M to JD, tracking resources with RT

    RT.remove(); // Remove M from JD.

Modules added directly to a JITDylib will be tracked by that JITDylib's default
resource tracker.

All code can be removed from a JITDylib by calling ``JITDylib::clear``. This
leaves the cleared JITDylib in an empty but usable state.

JITDylibs can be removed by calling ``ExecutionSession::removeJITDylib``. This
clears the JITDylib and then puts it into a defunct state. No further operations
can be performed on the JITDylib, and it will be destroyed as soon as the last
handle to it is released.

An example of how to use the resource management APIs can be found at
``llvm/examples/OrcV2Examples/LLJITRemovableCode``.


How to add the support for custom program representation
--------------------------------------------------------
In order to add the support for a custom program representation, a custom ``MaterializationUnit``
for the program representation, and a custom ``Layer`` are needed. The Layer will have two
operations: ``add`` and ``emit``. The ``add`` operation takes an instance of your program
representation, builds one of your custom ``MaterializationUnits`` to hold it, then adds it
to a ``JITDylib``. The emit operation takes a ``MaterializationResponsibility`` object and an
instance of your program representation and materializes it, usually by compiling it and handing
the resulting object off to an ``ObjectLinkingLayer``.

Your custom ``MaterializationUnit`` will have two operations: ``materialize`` and ``discard``. The
``materialize`` function will be called for you when any symbol provided by the unit is looked up,
and it should just call the ``emit`` function on your layer, passing in the given
``MaterializationResponsibility`` and the wrapped program representation. The ``discard`` function
will be called if some weak symbol provided by your unit is not needed (because the JIT found an
overriding definition). You can use this to drop your definition early, or just ignore it and let
the linker drops the definition later.

Here is an example of an ASTLayer:

  .. code-block:: c++

    // ... In you JIT class
    AstLayer astLayer;
    // ...


    class AstMaterializationUnit : public orc::MaterializationUnit {
    public:
      AstMaterializationUnit(AstLayer &l, Ast &ast)
      : llvm::orc::MaterializationUnit(l.getInterface(ast)), astLayer(l),
      ast(ast) {};

      llvm::StringRef getName() const override {
        return "AstMaterializationUnit";
      }

      void materialize(std::unique_ptr<orc::MaterializationResponsibility> r) override {
        astLayer.emit(std::move(r), ast);
      };

    private:
      void discard(const llvm::orc::JITDylib &jd, const llvm::orc::SymbolStringPtr &sym) override {
        llvm_unreachable("functions are not overridable");
      }


      AstLayer &astLayer;
      Ast &ast;
    };

    class AstLayer {
      llvhm::orc::IRLayer &baseLayer;
      llvhm::orc::MangleAndInterner &mangler;

    public:
      AstLayer(llvm::orc::IRLayer &baseLayer, llvm::orc::MangleAndInterner &mangler)
      : baseLayer(baseLayer), mangler(mangler){};

      llvm::Error add(llvm::orc::ResourceTrackerSP &rt, Ast &ast) {
        return rt->getJITDylib().define(std::make_unique<AstMaterializationUnit>(*this, ast), rt);
      }

      void emit(std::unique_ptr<orc::MaterializationResponsibility> mr, Ast &ast) {
        // compileAst is just function that compiles the given AST and returns
        // a `llvm::orc::ThreadSafeModule`
        baseLayer.emit(std::move(mr), compileAst(ast));
      }

      llvm::orc::MaterializationUnit::Interface getInterface(Ast &ast) {
          SymbolFlagsMap Symbols;
          // Find all the symbols in the AST and for each of them
          // add it to the Symbols map.
          Symbols[mangler(someNameFromAST)] =
            JITSymbolFlags(JITSymbolFlags::Exported | JITSymbolFlags::Callable);
          return MaterializationUnit::Interface(std::move(Symbols), nullptr);
      }
    };

Take look at the source code of `Building A JIT's Chapter 4 <tutorial/BuildingAJIT4.html>`_ for a complete example.

How to use ThreadSafeModule and ThreadSafeContext
-------------------------------------------------

ThreadSafeModule and ThreadSafeContext are wrappers around Modules and
LLVMContexts respectively. A ThreadSafeModule is a pair of a
std::unique_ptr<Module> and a (possibly shared) ThreadSafeContext value. A
ThreadSafeContext is a pair of a std::unique_ptr<LLVMContext> and a lock.
This design serves two purposes: providing a locking scheme and lifetime
management for LLVMContexts. The ThreadSafeContext may be locked to prevent
accidental concurrent access by two Modules that use the same LLVMContext.
The underlying LLVMContext is freed once all ThreadSafeContext values pointing
to it are destroyed, allowing the context memory to be reclaimed as soon as
the Modules referring to it are destroyed.

ThreadSafeContexts can be explicitly constructed from a
std::unique_ptr<LLVMContext>:

  .. code-block:: c++

    ThreadSafeContext TSCtx(std::make_unique<LLVMContext>());

ThreadSafeModules can be constructed from a pair of a std::unique_ptr<Module>
and a ThreadSafeContext value. ThreadSafeContext values may be shared between
multiple ThreadSafeModules:

  .. code-block:: c++

    ThreadSafeModule TSM1(
      std::make_unique<Module>("M1", *TSCtx.getContext()), TSCtx);

    ThreadSafeModule TSM2(
      std::make_unique<Module>("M2", *TSCtx.getContext()), TSCtx);

Before using a ThreadSafeContext, clients should ensure that either the context
is only accessible on the current thread, or that the context is locked. In the
example above (where the context is never locked) we rely on the fact that both
``TSM1`` and ``TSM2``, and TSCtx are all created on one thread. If a context is
going to be shared between threads then it must be locked before any accessing
or creating any Modules attached to it. E.g.

  .. code-block:: c++

    ThreadSafeContext TSCtx(std::make_unique<LLVMContext>());

    DefaultThreadPool TP(NumThreads);
    JITStack J;

    for (auto &ModulePath : ModulePaths) {
      TP.async(
        [&]() {
          auto Lock = TSCtx.getLock();
          auto M = loadModuleOnContext(ModulePath, TSCtx.getContext());
          J.addModule(ThreadSafeModule(std::move(M), TSCtx));
        });
    }

    TP.wait();

To make exclusive access to Modules easier to manage the ThreadSafeModule class
provides a convenience function, ``withModuleDo``, that implicitly (1) locks the
associated context, (2) runs a given function object, (3) unlocks the context,
and (3) returns the result generated by the function object. E.g.

  .. code-block:: c++

    ThreadSafeModule TSM = getModule(...);

    // Dump the module:
    size_t NumFunctionsInModule =
      TSM.withModuleDo(
        [](Module &M) { // <- Context locked before entering lambda.
          return M.size();
        } // <- Context unlocked after leaving.
      );

Clients wishing to maximize possibilities for concurrent compilation will want
to create every new ThreadSafeModule on a new ThreadSafeContext. For this
reason a convenience constructor for ThreadSafeModule is provided that implicitly
constructs a new ThreadSafeContext value from a std::unique_ptr<LLVMContext>:

  .. code-block:: c++

    // Maximize concurrency opportunities by loading every module on a
    // separate context.
    for (const auto &IRPath : IRPaths) {
      auto Ctx = std::make_unique<LLVMContext>();
      auto M = std::make_unique<Module>("M", *Ctx);
      CompileLayer.add(MainJD, ThreadSafeModule(std::move(M), std::move(Ctx)));
    }

Clients who plan to run single-threaded may choose to save memory by loading
all modules on the same context:

  .. code-block:: c++

    // Save memory by using one context for all Modules:
    ThreadSafeContext TSCtx(std::make_unique<LLVMContext>());
    for (const auto &IRPath : IRPaths) {
      ThreadSafeModule TSM(parsePath(IRPath, *TSCtx.getContext()), TSCtx);
      CompileLayer.add(MainJD, ThreadSafeModule(std::move(TSM));
    }

.. _ProcessAndLibrarySymbols:

How to Add Process and Library Symbols to JITDylibs
===================================================

JIT'd code may need to access symbols in the host program or in supporting
libraries. The best way to enable this is to reflect these symbols into your
JITDylibs so that they appear the same as any other symbol defined within the
execution session (i.e. they are findable via `ExecutionSession::lookup`, and
so visible to the JIT linker during linking).

One way to reflect external symbols is to add them manually using the
absoluteSymbols function:

  .. code-block:: c++

    const DataLayout &DL = getDataLayout();
    MangleAndInterner Mangle(ES, DL);

    auto &JD = ES.createJITDylib("main");

    JD.define(
      absoluteSymbols({
        { Mangle("puts"), ExecutorAddr::fromPtr(&puts)},
        { Mangle("gets"), ExecutorAddr::fromPtr(&getS)}
      }));

Using absoluteSymbols is reasonable if the set of symbols to be reflected is
small and fixed. On the other hand, if the set of symbols is large or variable
it may make more sense to have the definitions added for you on demand by a
*definition generator*.A definition generator is an object that can be attached
to a JITDylib, receiving a callback whenever a lookup within that JITDylib fails
to find one or more symbols. The definition generator is given a chance to
produce a definition of the missing symbol(s) before the lookup proceeds.

ORC provides the ``DynamicLibrarySearchGenerator`` utility for reflecting symbols
from the process (or a specific dynamic library) for you. For example, to reflect
the whole interface of a runtime library:

  .. code-block:: c++

    const DataLayout &DL = getDataLayout();
    auto &JD = ES.createJITDylib("main");

    if (auto DLSGOrErr =
        DynamicLibrarySearchGenerator::Load("/path/to/lib"
                                            DL.getGlobalPrefix()))
      JD.addGenerator(std::move(*DLSGOrErr);
    else
      return DLSGOrErr.takeError();

    // IR added to JD can now link against all symbols exported by the library
    // at '/path/to/lib'.
    CompileLayer.add(JD, loadModule(...));

The ``DynamicLibrarySearchGenerator`` utility can also be constructed with a
filter function to restrict the set of symbols that may be reflected. For
example, to expose an allowed set of symbols from the main process:

  .. code-block:: c++

    const DataLayout &DL = getDataLayout();
    MangleAndInterner Mangle(ES, DL);

    auto &JD = ES.createJITDylib("main");

    DenseSet<SymbolStringPtr> AllowList({
        Mangle("puts"),
        Mangle("gets")
      });

    // Use GetForCurrentProcess with a predicate function that checks the
    // allowed list.
    JD.addGenerator(cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
          DL.getGlobalPrefix(),
          [&](const SymbolStringPtr &S) { return AllowList.count(S); })));

    // IR added to JD can now link against any symbols exported by the process
    // and contained in the list.
    CompileLayer.add(JD, loadModule(...));

References to process or library symbols could also be hardcoded into your IR
or object files using the symbols' raw addresses, however symbolic resolution
using the JIT symbol tables should be preferred: it keeps the IR and objects
readable and reusable in subsequent JIT sessions. Hardcoded addresses are
difficult to read, and usually only good for one session.

Roadmap
=======

ORC is still undergoing active development. Some current and future works are
listed below.

Current Work
------------

1. **TargetProcessControl: Improvements to in-tree support for out-of-process
   execution**

   The ``TargetProcessControl`` API provides various operations on the JIT
   target process (the one which will execute the JIT'd code), including
   memory allocation, memory writes, function execution, and process queries
   (e.g. for the target triple). By targeting this API new components can be
   developed which will work equally well for in-process and out-of-process
   JITing.


2. **ORC RPC based TargetProcessControl implementation**

   An ORC RPC based implementation of the ``TargetProcessControl`` API is
   currently under development to enable easy out-of-process JITing via
   file descriptors / sockets.

3. **Core State Machine Cleanup**

   The core ORC state machine is currently implemented between JITDylib and
   ExecutionSession. Methods are slowly being moved to `ExecutionSession`. This
   will tidy up the code base, and also allow us to support asynchronous removal
   of JITDylibs (in practice deleting an associated state object in
   ExecutionSession and leaving the JITDylib instance in a defunct state until
   all references to it have been released).

Near Future Work
----------------

1. **ORC JIT Runtime Libraries**

   We need a runtime library for JIT'd code. This would include things like
   TLS registration, reentry functions, registration code for language runtimes
   (e.g. Objective C and Swift) and other JIT specific runtime code. This should
   be built in a similar manner to compiler-rt (possibly even as part of it).

2. **Remote jit_dlopen / jit_dlclose**

   To more fully mimic the environment that static programs operate in we would
   like JIT'd code to be able to "dlopen" and "dlclose" JITDylibs, running all of
   their initializers/deinitializers on the current thread. This would require
   support from the runtime library described above.

3. **Debugging support**

   ORC currently supports the GDBRegistrationListener API when using RuntimeDyld
   as the underlying JIT linker. We will need a new solution for JITLink based
   platforms.

Further Future Work
-------------------

1. **Speculative Compilation**

   ORC's support for concurrent compilation allows us to easily enable
   *speculative* JIT compilation: compilation of code that is not needed yet,
   but which we have reason to believe will be needed in the future. This can be
   used to hide compile latency and improve JIT throughput. A proof-of-concept
   example of speculative compilation with ORC has already been developed (see
   ``llvm/examples/SpeculativeJIT``). Future work on this is likely to focus on
   re-using and improving existing profiling support (currently used by PGO) to
   feed speculation decisions, as well as built-in tools to simplify use of
   speculative compilation.

.. [1] Formats/architectures vary in terms of supported features. MachO and
       ELF tend to have better support than COFF. Patches very welcome!

.. [2] The ``LazyEmittingLayer``, ``RemoteObjectClientLayer`` and
       ``RemoteObjectServerLayer`` do not have counterparts in the new
       system. In the case of ``LazyEmittingLayer`` it was simply no longer
       needed: in ORCv2, deferring compilation until symbols are looked up is
       the default. The removal of ``RemoteObjectClientLayer`` and
       ``RemoteObjectServerLayer`` means that JIT stacks can no longer be split
       across processes, however this functionality appears not to have been
       used.

.. [3] Weak definitions are currently handled correctly within dylibs, but if
       multiple dylibs provide a weak definition of a symbol then each will end
       up with its own definition (similar to how weak definitions are handled
       in Windows DLLs). This will be fixed in the future.
