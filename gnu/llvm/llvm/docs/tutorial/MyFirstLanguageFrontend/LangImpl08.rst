========================================
 Kaleidoscope: Compiling to Object Code
========================================

.. contents::
   :local:

Chapter 8 Introduction
======================

Welcome to Chapter 8 of the "`Implementing a language with LLVM
<index.html>`_" tutorial. This chapter describes how to compile our
language down to object files.

Choosing a target
=================

LLVM has native support for cross-compilation. You can compile to the
architecture of your current machine, or just as easily compile for
other architectures. In this tutorial, we'll target the current
machine.

To specify the architecture that you want to target, we use a string
called a "target triple". This takes the form
``<arch><sub>-<vendor>-<sys>-<abi>`` (see the `cross compilation docs
<https://clang.llvm.org/docs/CrossCompilation.html#target-triple>`_).

As an example, we can see what clang thinks is our current target
triple:

::

    $ clang --version | grep Target
    Target: x86_64-unknown-linux-gnu

Running this command may show something different on your machine as
you might be using a different architecture or operating system to me.

Fortunately, we don't need to hard-code a target triple to target the
current machine. LLVM provides ``sys::getDefaultTargetTriple``, which
returns the target triple of the current machine.

.. code-block:: c++

    auto TargetTriple = sys::getDefaultTargetTriple();

LLVM doesn't require us to link in all the target
functionality. For example, if we're just using the JIT, we don't need
the assembly printers. Similarly, if we're only targeting certain
architectures, we can only link in the functionality for those
architectures.

For this example, we'll initialize all the targets for emitting object
code.

.. code-block:: c++

    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();

We can now use our target triple to get a ``Target``:

.. code-block:: c++

  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);

  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!Target) {
    errs() << Error;
    return 1;
  }

Target Machine
==============

We will also need a ``TargetMachine``. This class provides a complete
machine description of the machine we're targeting. If we want to
target a specific feature (such as SSE) or a specific CPU (such as
Intel's Sandylake), we do so now.

To see which features and CPUs that LLVM knows about, we can use
``llc``. For example, let's look at x86:

::

    $ llvm-as < /dev/null | llc -march=x86 -mattr=help
    Available CPUs for this target:

      amdfam10      - Select the amdfam10 processor.
      athlon        - Select the athlon processor.
      athlon-4      - Select the athlon-4 processor.
      ...

    Available features for this target:

      16bit-mode            - 16-bit mode (i8086).
      32bit-mode            - 32-bit mode (80386).
      3dnow                 - Enable 3DNow! instructions.
      3dnowa                - Enable 3DNow! Athlon instructions.
      ...

For our example, we'll use the generic CPU without any additional feature or
target option.

.. code-block:: c++

  auto CPU = "generic";
  auto Features = "";

  TargetOptions opt;
  auto TargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, Reloc::PIC_);


Configuring the Module
======================

We're now ready to configure our module, to specify the target and
data layout. This isn't strictly necessary, but the `frontend
performance guide <../../Frontend/PerformanceTips.html>`_ recommends
this. Optimizations benefit from knowing about the target and data
layout.

.. code-block:: c++

  TheModule->setDataLayout(TargetMachine->createDataLayout());
  TheModule->setTargetTriple(TargetTriple);

Emit Object Code
================

We're ready to emit object code! Let's define where we want to write
our file to:

.. code-block:: c++

  auto Filename = "output.o";
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Could not open file: " << EC.message();
    return 1;
  }

Finally, we define a pass that emits object code, then we run that
pass:

.. code-block:: c++

  legacy::PassManager pass;
  auto FileType = CodeGenFileType::ObjectFile;

  if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TargetMachine can't emit a file of this type";
    return 1;
  }

  pass.run(*TheModule);
  dest.flush();

Putting It All Together
=======================

Does it work? Let's give it a try. We need to compile our code, but
note that the arguments to ``llvm-config`` are different to the previous chapters.

::

    $ clang++ -g -O3 toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs all` -o toy

Let's run it, and define a simple ``average`` function. Press Ctrl-D
when you're done.

::

    $ ./toy
    ready> def average(x y) (x + y) * 0.5;
    ^D
    Wrote output.o

We have an object file! To test it, let's write a simple program and
link it with our output. Here's the source code:

.. code-block:: c++

    #include <iostream>

    extern "C" {
        double average(double, double);
    }

    int main() {
        std::cout << "average of 3.0 and 4.0: " << average(3.0, 4.0) << std::endl;
    }

We link our program to output.o and check the result is what we
expected:

::

    $ clang++ main.cpp output.o -o main
    $ ./main
    average of 3.0 and 4.0: 3.5

Full Code Listing
=================

.. literalinclude:: ../../../examples/Kaleidoscope/Chapter8/toy.cpp
   :language: c++

`Next: Adding Debug Information <LangImpl09.html>`_
