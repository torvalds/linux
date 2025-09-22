========================================
Kaleidoscope: Code generation to LLVM IR
========================================

.. contents::
   :local:

Chapter 3 Introduction
======================

Welcome to Chapter 3 of the "`Implementing a language with
LLVM <index.html>`_" tutorial. This chapter shows you how to transform
the `Abstract Syntax Tree <LangImpl02.html>`_, built in Chapter 2, into
LLVM IR. This will teach you a little bit about how LLVM does things, as
well as demonstrate how easy it is to use. It's much more work to build
a lexer and parser than it is to generate LLVM IR code. :)

**Please note**: the code in this chapter and later require LLVM 3.7 or
later. LLVM 3.6 and before will not work with it. Also note that you
need to use a version of this tutorial that matches your LLVM release:
If you are using an official LLVM release, use the version of the
documentation included with your release or on the `llvm.org releases
page <https://llvm.org/releases/>`_.

Code Generation Setup
=====================

In order to generate LLVM IR, we want some simple setup to get started.
First we define virtual code generation (codegen) methods in each AST
class:

.. code-block:: c++

    /// ExprAST - Base class for all expression nodes.
    class ExprAST {
    public:
      virtual ~ExprAST() = default;
      virtual Value *codegen() = 0;
    };

    /// NumberExprAST - Expression class for numeric literals like "1.0".
    class NumberExprAST : public ExprAST {
      double Val;

    public:
      NumberExprAST(double Val) : Val(Val) {}
      Value *codegen() override;
    };
    ...

The codegen() method says to emit IR for that AST node along with all
the things it depends on, and they all return an LLVM Value object.
"Value" is the class used to represent a "`Static Single Assignment
(SSA) <http://en.wikipedia.org/wiki/Static_single_assignment_form>`_
register" or "SSA value" in LLVM. The most distinct aspect of SSA values
is that their value is computed as the related instruction executes, and
it does not get a new value until (and if) the instruction re-executes.
In other words, there is no way to "change" an SSA value. For more
information, please read up on `Static Single
Assignment <http://en.wikipedia.org/wiki/Static_single_assignment_form>`_
- the concepts are really quite natural once you grok them.

Note that instead of adding virtual methods to the ExprAST class
hierarchy, it could also make sense to use a `visitor
pattern <http://en.wikipedia.org/wiki/Visitor_pattern>`_ or some other
way to model this. Again, this tutorial won't dwell on good software
engineering practices: for our purposes, adding a virtual method is
simplest.

The second thing we want is a "LogError" method like we used for the
parser, which will be used to report errors found during code generation
(for example, use of an undeclared parameter):

.. code-block:: c++

    static std::unique_ptr<LLVMContext> TheContext;
    static std::unique_ptr<IRBuilder<>> Builder(TheContext);
    static std::unique_ptr<Module> TheModule;
    static std::map<std::string, Value *> NamedValues;

    Value *LogErrorV(const char *Str) {
      LogError(Str);
      return nullptr;
    }

The static variables will be used during code generation. ``TheContext``
is an opaque object that owns a lot of core LLVM data structures, such as
the type and constant value tables. We don't need to understand it in
detail, we just need a single instance to pass into APIs that require it.

The ``Builder`` object is a helper object that makes it easy to generate
LLVM instructions. Instances of the
`IRBuilder <https://llvm.org/doxygen/IRBuilder_8h_source.html>`_
class template keep track of the current place to insert instructions
and has methods to create new instructions.

``TheModule`` is an LLVM construct that contains functions and global
variables. In many ways, it is the top-level structure that the LLVM IR
uses to contain code. It will own the memory for all of the IR that we
generate, which is why the codegen() method returns a raw Value\*,
rather than a unique_ptr<Value>.

The ``NamedValues`` map keeps track of which values are defined in the
current scope and what their LLVM representation is. (In other words, it
is a symbol table for the code). In this form of Kaleidoscope, the only
things that can be referenced are function parameters. As such, function
parameters will be in this map when generating code for their function
body.

With these basics in place, we can start talking about how to generate
code for each expression. Note that this assumes that the ``Builder``
has been set up to generate code *into* something. For now, we'll assume
that this has already been done, and we'll just use it to emit code.

Expression Code Generation
==========================

Generating LLVM code for expression nodes is very straightforward: less
than 45 lines of commented code for all four of our expression nodes.
First we'll do numeric literals:

.. code-block:: c++

    Value *NumberExprAST::codegen() {
      return ConstantFP::get(*TheContext, APFloat(Val));
    }

In the LLVM IR, numeric constants are represented with the
``ConstantFP`` class, which holds the numeric value in an ``APFloat``
internally (``APFloat`` has the capability of holding floating point
constants of Arbitrary Precision). This code basically just creates
and returns a ``ConstantFP``. Note that in the LLVM IR that constants
are all uniqued together and shared. For this reason, the API uses the
"foo::get(...)" idiom instead of "new foo(..)" or "foo::Create(..)".

.. code-block:: c++

    Value *VariableExprAST::codegen() {
      // Look this variable up in the function.
      Value *V = NamedValues[Name];
      if (!V)
        LogErrorV("Unknown variable name");
      return V;
    }

References to variables are also quite simple using LLVM. In the simple
version of Kaleidoscope, we assume that the variable has already been
emitted somewhere and its value is available. In practice, the only
values that can be in the ``NamedValues`` map are function arguments.
This code simply checks to see that the specified name is in the map (if
not, an unknown variable is being referenced) and returns the value for
it. In future chapters, we'll add support for `loop induction
variables <LangImpl05.html#for-loop-expression>`_ in the symbol table, and for `local
variables <LangImpl07.html#user-defined-local-variables>`_.

.. code-block:: c++

    Value *BinaryExprAST::codegen() {
      Value *L = LHS->codegen();
      Value *R = RHS->codegen();
      if (!L || !R)
        return nullptr;

      switch (Op) {
      case '+':
        return Builder->CreateFAdd(L, R, "addtmp");
      case '-':
        return Builder->CreateFSub(L, R, "subtmp");
      case '*':
        return Builder->CreateFMul(L, R, "multmp");
      case '<':
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        // Convert bool 0/1 to double 0.0 or 1.0
        return Builder->CreateUIToFP(L, Type::getDoubleTy(TheContext),
                                     "booltmp");
      default:
        return LogErrorV("invalid binary operator");
      }
    }

Binary operators start to get more interesting. The basic idea here is
that we recursively emit code for the left-hand side of the expression,
then the right-hand side, then we compute the result of the binary
expression. In this code, we do a simple switch on the opcode to create
the right LLVM instruction.

In the example above, the LLVM builder class is starting to show its
value. IRBuilder knows where to insert the newly created instruction,
all you have to do is specify what instruction to create (e.g. with
``CreateFAdd``), which operands to use (``L`` and ``R`` here) and
optionally provide a name for the generated instruction.

One nice thing about LLVM is that the name is just a hint. For instance,
if the code above emits multiple "addtmp" variables, LLVM will
automatically provide each one with an increasing, unique numeric
suffix. Local value names for instructions are purely optional, but it
makes it much easier to read the IR dumps.

`LLVM instructions <../../LangRef.html#instruction-reference>`_ are constrained by strict
rules: for example, the Left and Right operands of an `add
instruction <../../LangRef.html#add-instruction>`_ must have the same type, and the
result type of the add must match the operand types. Because all values
in Kaleidoscope are doubles, this makes for very simple code for add,
sub and mul.

On the other hand, LLVM specifies that the `fcmp
instruction <../../LangRef.html#fcmp-instruction>`_ always returns an 'i1' value (a
one bit integer). The problem with this is that Kaleidoscope wants the
value to be a 0.0 or 1.0 value. In order to get these semantics, we
combine the fcmp instruction with a `uitofp
instruction <../../LangRef.html#uitofp-to-instruction>`_. This instruction converts its
input integer into a floating point value by treating the input as an
unsigned value. In contrast, if we used the `sitofp
instruction <../../LangRef.html#sitofp-to-instruction>`_, the Kaleidoscope '<' operator
would return 0.0 and -1.0, depending on the input value.

.. code-block:: c++

    Value *CallExprAST::codegen() {
      // Look up the name in the global module table.
      Function *CalleeF = TheModule->getFunction(Callee);
      if (!CalleeF)
        return LogErrorV("Unknown function referenced");

      // If argument mismatch error.
      if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

      std::vector<Value *> ArgsV;
      for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
          return nullptr;
      }

      return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
    }

Code generation for function calls is quite straightforward with LLVM. The code
above initially does a function name lookup in the LLVM Module's symbol table.
Recall that the LLVM Module is the container that holds the functions we are
JIT'ing. By giving each function the same name as what the user specifies, we
can use the LLVM symbol table to resolve function names for us.

Once we have the function to call, we recursively codegen each argument
that is to be passed in, and create an LLVM `call
instruction <../../LangRef.html#call-instruction>`_. Note that LLVM uses the native C
calling conventions by default, allowing these calls to also call into
standard library functions like "sin" and "cos", with no additional
effort.

This wraps up our handling of the four basic expressions that we have so
far in Kaleidoscope. Feel free to go in and add some more. For example,
by browsing the `LLVM language reference <../../LangRef.html>`_ you'll find
several other interesting instructions that are really easy to plug into
our basic framework.

Function Code Generation
========================

Code generation for prototypes and functions must handle a number of
details, which make their code less beautiful than expression code
generation, but allows us to illustrate some important points. First,
let's talk about code generation for prototypes: they are used both for
function bodies and external function declarations. The code starts
with:

.. code-block:: c++

    Function *PrototypeAST::codegen() {
      // Make the function type:  double(double,double) etc.
      std::vector<Type*> Doubles(Args.size(),
                                 Type::getDoubleTy(*TheContext));
      FunctionType *FT =
        FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

      Function *F =
        Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

This code packs a lot of power into a few lines. Note first that this
function returns a "Function\*" instead of a "Value\*". Because a
"prototype" really talks about the external interface for a function
(not the value computed by an expression), it makes sense for it to
return the LLVM Function it corresponds to when codegen'd.

The call to ``FunctionType::get`` creates the ``FunctionType`` that
should be used for a given Prototype. Since all function arguments in
Kaleidoscope are of type double, the first line creates a vector of "N"
LLVM double types. It then uses the ``Functiontype::get`` method to
create a function type that takes "N" doubles as arguments, returns one
double as a result, and that is not vararg (the false parameter
indicates this). Note that Types in LLVM are uniqued just like Constants
are, so you don't "new" a type, you "get" it.

The final line above actually creates the IR Function corresponding to
the Prototype. This indicates the type, linkage and name to use, as
well as which module to insert into. "`external
linkage <../../LangRef.html#linkage>`_" means that the function may be
defined outside the current module and/or that it is callable by
functions outside the module. The Name passed in is the name the user
specified: since "``TheModule``" is specified, this name is registered
in "``TheModule``"s symbol table.

.. code-block:: c++

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;

Finally, we set the name of each of the function's arguments according to the
names given in the Prototype. This step isn't strictly necessary, but keeping
the names consistent makes the IR more readable, and allows subsequent code to
refer directly to the arguments for their names, rather than having to look up
them up in the Prototype AST.

At this point we have a function prototype with no body. This is how LLVM IR
represents function declarations. For extern statements in Kaleidoscope, this
is as far as we need to go. For function definitions however, we need to
codegen and attach a function body.

.. code-block:: c++

  Function *FunctionAST::codegen() {
      // First, check for an existing function from a previous 'extern' declaration.
    Function *TheFunction = TheModule->getFunction(Proto->getName());

    if (!TheFunction)
      TheFunction = Proto->codegen();

    if (!TheFunction)
      return nullptr;

    if (!TheFunction->empty())
      return (Function*)LogErrorV("Function cannot be redefined.");


For function definitions, we start by searching TheModule's symbol table for an
existing version of this function, in case one has already been created using an
'extern' statement. If Module::getFunction returns null then no previous version
exists, so we'll codegen one from the Prototype. In either case, we want to
assert that the function is empty (i.e. has no body yet) before we start.

.. code-block:: c++

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

Now we get to the point where the ``Builder`` is set up. The first line
creates a new `basic block <http://en.wikipedia.org/wiki/Basic_block>`_
(named "entry"), which is inserted into ``TheFunction``. The second line
then tells the builder that new instructions should be inserted into the
end of the new basic block. Basic blocks in LLVM are an important part
of functions that define the `Control Flow
Graph <http://en.wikipedia.org/wiki/Control_flow_graph>`_. Since we
don't have any control flow, our functions will only contain one block
at this point. We'll fix this in `Chapter 5 <LangImpl05.html>`_ :).

Next we add the function arguments to the NamedValues map (after first clearing
it out) so that they're accessible to ``VariableExprAST`` nodes.

.. code-block:: c++

      if (Value *RetVal = Body->codegen()) {
        // Finish off the function.
        Builder->CreateRet(RetVal);

        // Validate the generated code, checking for consistency.
        verifyFunction(*TheFunction);

        return TheFunction;
      }

Once the insertion point has been set up and the NamedValues map populated,
we call the ``codegen()`` method for the root expression of the function. If no
error happens, this emits code to compute the expression into the entry block
and returns the value that was computed. Assuming no error, we then create an
LLVM `ret instruction <../../LangRef.html#ret-instruction>`_, which completes the function.
Once the function is built, we call ``verifyFunction``, which is
provided by LLVM. This function does a variety of consistency checks on
the generated code, to determine if our compiler is doing everything
right. Using this is important: it can catch a lot of bugs. Once the
function is finished and validated, we return it.

.. code-block:: c++

      // Error reading body, remove function.
      TheFunction->eraseFromParent();
      return nullptr;
    }

The only piece left here is handling of the error case. For simplicity,
we handle this by merely deleting the function we produced with the
``eraseFromParent`` method. This allows the user to redefine a function
that they incorrectly typed in before: if we didn't delete it, it would
live in the symbol table, with a body, preventing future redefinition.

This code does have a bug, though: If the ``FunctionAST::codegen()`` method
finds an existing IR Function, it does not validate its signature against the
definition's own prototype. This means that an earlier 'extern' declaration will
take precedence over the function definition's signature, which can cause
codegen to fail, for instance if the function arguments are named differently.
There are a number of ways to fix this bug, see what you can come up with! Here
is a testcase:

::

    extern foo(a);     # ok, defines foo.
    def foo(b) b;      # Error: Unknown variable name. (decl using 'a' takes precedence).

Driver Changes and Closing Thoughts
===================================

For now, code generation to LLVM doesn't really get us much, except that
we can look at the pretty IR calls. The sample code inserts calls to
codegen into the "``HandleDefinition``", "``HandleExtern``" etc
functions, and then dumps out the LLVM IR. This gives a nice way to look
at the LLVM IR for simple functions. For example:

::

    ready> 4+5;
    Read top-level expression:
    define double @0() {
    entry:
      ret double 9.000000e+00
    }

Note how the parser turns the top-level expression into anonymous
functions for us. This will be handy when we add `JIT
support <LangImpl04.html#adding-a-jit-compiler>`_ in the next chapter. Also note that the
code is very literally transcribed, no optimizations are being performed
except simple constant folding done by IRBuilder. We will `add
optimizations <LangImpl04.html#trivial-constant-folding>`_ explicitly in the next
chapter.

::

    ready> def foo(a b) a*a + 2*a*b + b*b;
    Read function definition:
    define double @foo(double %a, double %b) {
    entry:
      %multmp = fmul double %a, %a
      %multmp1 = fmul double 2.000000e+00, %a
      %multmp2 = fmul double %multmp1, %b
      %addtmp = fadd double %multmp, %multmp2
      %multmp3 = fmul double %b, %b
      %addtmp4 = fadd double %addtmp, %multmp3
      ret double %addtmp4
    }

This shows some simple arithmetic. Notice the striking similarity to the
LLVM builder calls that we use to create the instructions.

::

    ready> def bar(a) foo(a, 4.0) + bar(31337);
    Read function definition:
    define double @bar(double %a) {
    entry:
      %calltmp = call double @foo(double %a, double 4.000000e+00)
      %calltmp1 = call double @bar(double 3.133700e+04)
      %addtmp = fadd double %calltmp, %calltmp1
      ret double %addtmp
    }

This shows some function calls. Note that this function will take a long
time to execute if you call it. In the future we'll add conditional
control flow to actually make recursion useful :).

::

    ready> extern cos(x);
    Read extern:
    declare double @cos(double)

    ready> cos(1.234);
    Read top-level expression:
    define double @1() {
    entry:
      %calltmp = call double @cos(double 1.234000e+00)
      ret double %calltmp
    }

This shows an extern for the libm "cos" function, and a call to it.

.. TODO:: Abandon Pygments' horrible `llvm` lexer. It just totally gives up
   on highlighting this due to the first line.

::

    ready> ^D
    ; ModuleID = 'my cool jit'

    define double @0() {
    entry:
      %addtmp = fadd double 4.000000e+00, 5.000000e+00
      ret double %addtmp
    }

    define double @foo(double %a, double %b) {
    entry:
      %multmp = fmul double %a, %a
      %multmp1 = fmul double 2.000000e+00, %a
      %multmp2 = fmul double %multmp1, %b
      %addtmp = fadd double %multmp, %multmp2
      %multmp3 = fmul double %b, %b
      %addtmp4 = fadd double %addtmp, %multmp3
      ret double %addtmp4
    }

    define double @bar(double %a) {
    entry:
      %calltmp = call double @foo(double %a, double 4.000000e+00)
      %calltmp1 = call double @bar(double 3.133700e+04)
      %addtmp = fadd double %calltmp, %calltmp1
      ret double %addtmp
    }

    declare double @cos(double)

    define double @1() {
    entry:
      %calltmp = call double @cos(double 1.234000e+00)
      ret double %calltmp
    }

When you quit the current demo (by sending an EOF via CTRL+D on Linux
or CTRL+Z and ENTER on Windows), it dumps out the IR for the entire
module generated. Here you can see the big picture with all the
functions referencing each other.

This wraps up the third chapter of the Kaleidoscope tutorial. Up next,
we'll describe how to `add JIT codegen and optimizer
support <LangImpl04.html>`_ to this so we can actually start running
code!

Full Code Listing
=================

Here is the complete code listing for our running example, enhanced with
the LLVM code generator. Because this uses the LLVM libraries, we need
to link them in. To do this, we use the
`llvm-config <https://llvm.org/cmds/llvm-config.html>`_ tool to inform
our makefile/command line about which options to use:

.. code-block:: bash

    # Compile
    clang++ -g -O3 toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o toy
    # Run
    ./toy

Here is the code:

.. literalinclude:: ../../../examples/Kaleidoscope/Chapter3/toy.cpp
   :language: c++

`Next: Adding JIT and Optimizer Support <LangImpl04.html>`_

