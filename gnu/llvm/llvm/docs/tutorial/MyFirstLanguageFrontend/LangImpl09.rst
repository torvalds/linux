======================================
Kaleidoscope: Adding Debug Information
======================================

.. contents::
   :local:

Chapter 9 Introduction
======================

Welcome to Chapter 9 of the "`Implementing a language with
LLVM <index.html>`_" tutorial. In chapters 1 through 8, we've built a
decent little programming language with functions and variables.
What happens if something goes wrong though, how do you debug your
program?

Source level debugging uses formatted data that helps a debugger
translate from binary and the state of the machine back to the
source that the programmer wrote. In LLVM we generally use a format
called `DWARF <http://dwarfstd.org>`_. DWARF is a compact encoding
that represents types, source locations, and variable locations.

The short summary of this chapter is that we'll go through the
various things you have to add to a programming language to
support debug info, and how you translate that into DWARF.

Caveat: For now we can't debug via the JIT, so we'll need to compile
our program down to something small and standalone. As part of this
we'll make a few modifications to the running of the language and
how programs are compiled. This means that we'll have a source file
with a simple program written in Kaleidoscope rather than the
interactive JIT. It does involve a limitation that we can only
have one "top level" command at a time to reduce the number of
changes necessary.

Here's the sample program we'll be compiling:

.. code-block:: python

   def fib(x)
     if x < 3 then
       1
     else
       fib(x-1)+fib(x-2);

   fib(10)


Why is this a hard problem?
===========================

Debug information is a hard problem for a few different reasons - mostly
centered around optimized code. First, optimization makes keeping source
locations more difficult. In LLVM IR we keep the original source location
for each IR level instruction on the instruction. Optimization passes
should keep the source locations for newly created instructions, but merged
instructions only get to keep a single location - this can cause jumping
around when stepping through optimized programs. Secondly, optimization
can move variables in ways that are either optimized out, shared in memory
with other variables, or difficult to track. For the purposes of this
tutorial we're going to avoid optimization (as you'll see with one of the
next sets of patches).

Ahead-of-Time Compilation Mode
==============================

To highlight only the aspects of adding debug information to a source
language without needing to worry about the complexities of JIT debugging
we're going to make a few changes to Kaleidoscope to support compiling
the IR emitted by the front end into a simple standalone program that
you can execute, debug, and see results.

First we make our anonymous function that contains our top level
statement be our "main":

.. code-block:: udiff

  -    auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
  +    auto Proto = std::make_unique<PrototypeAST>("main", std::vector<std::string>());

just with the simple change of giving it a name.

Then we're going to remove the command line code wherever it exists:

.. code-block:: udiff

  @@ -1129,7 +1129,6 @@ static void HandleTopLevelExpression() {
   /// top ::= definition | external | expression | ';'
   static void MainLoop() {
     while (true) {
  -    fprintf(stderr, "ready> ");
       switch (CurTok) {
       case tok_eof:
         return;
  @@ -1184,7 +1183,6 @@ int main() {
     BinopPrecedence['*'] = 40; // highest.

     // Prime the first token.
  -  fprintf(stderr, "ready> ");
     getNextToken();

Lastly we're going to disable all of the optimization passes and the JIT so
that the only thing that happens after we're done parsing and generating
code is that the LLVM IR goes to standard error:

.. code-block:: udiff

  @@ -1108,17 +1108,8 @@ static void HandleExtern() {
   static void HandleTopLevelExpression() {
     // Evaluate a top-level expression into an anonymous function.
     if (auto FnAST = ParseTopLevelExpr()) {
  -    if (auto *FnIR = FnAST->codegen()) {
  -      // We're just doing this to make sure it executes.
  -      TheExecutionEngine->finalizeObject();
  -      // JIT the function, returning a function pointer.
  -      void *FPtr = TheExecutionEngine->getPointerToFunction(FnIR);
  -
  -      // Cast it to the right type (takes no arguments, returns a double) so we
  -      // can call it as a native function.
  -      double (*FP)() = (double (*)())(intptr_t)FPtr;
  -      // Ignore the return value for this.
  -      (void)FP;
  +    if (!FnAST->codegen()) {
  +      fprintf(stderr, "Error generating code for top level expr");
       }
     } else {
       // Skip token for error recovery.
  @@ -1439,11 +1459,11 @@ int main() {
     // target lays out data structures.
     TheModule->setDataLayout(TheExecutionEngine->getDataLayout());
     OurFPM.add(new DataLayoutPass());
  +#if 0
     OurFPM.add(createBasicAliasAnalysisPass());
     // Promote allocas to registers.
     OurFPM.add(createPromoteMemoryToRegisterPass());
  @@ -1218,7 +1210,7 @@ int main() {
     OurFPM.add(createGVNPass());
     // Simplify the control flow graph (deleting unreachable blocks, etc).
     OurFPM.add(createCFGSimplificationPass());
  -
  +  #endif
     OurFPM.doInitialization();

     // Set the global so the code gen can use this.

This relatively small set of changes get us to the point that we can compile
our piece of Kaleidoscope language down to an executable program via this
command line:

.. code-block:: bash

  Kaleidoscope-Ch9 < fib.ks | & clang -x ir -

which gives an a.out/a.exe in the current working directory.

Compile Unit
============

The top level container for a section of code in DWARF is a compile unit.
This contains the type and function data for an individual translation unit
(read: one file of source code). So the first thing we need to do is
construct one for our fib.ks file.

DWARF Emission Setup
====================

Similar to the ``IRBuilder`` class we have a
`DIBuilder <https://llvm.org/doxygen/classllvm_1_1DIBuilder.html>`_ class
that helps in constructing debug metadata for an LLVM IR file. It
corresponds 1:1 similarly to ``IRBuilder`` and LLVM IR, but with nicer names.
Using it does require that you be more familiar with DWARF terminology than
you needed to be with ``IRBuilder`` and ``Instruction`` names, but if you
read through the general documentation on the
`Metadata Format <https://llvm.org/docs/SourceLevelDebugging.html>`_ it
should be a little more clear. We'll be using this class to construct all
of our IR level descriptions. Construction for it takes a module so we
need to construct it shortly after we construct our module. We've left it
as a global static variable to make it a bit easier to use.

Next we're going to create a small container to cache some of our frequent
data. The first will be our compile unit, but we'll also write a bit of
code for our one type since we won't have to worry about multiple typed
expressions:

.. code-block:: c++

  static std::unique_ptr<DIBuilder> DBuilder;

  struct DebugInfo {
    DICompileUnit *TheCU;
    DIType *DblTy;

    DIType *getDoubleTy();
  } KSDbgInfo;

  DIType *DebugInfo::getDoubleTy() {
    if (DblTy)
      return DblTy;

    DblTy = DBuilder->createBasicType("double", 64, dwarf::DW_ATE_float);
    return DblTy;
  }

And then later on in ``main`` when we're constructing our module:

.. code-block:: c++

  DBuilder = std::make_unique<DIBuilder>(*TheModule);

  KSDbgInfo.TheCU = DBuilder->createCompileUnit(
      dwarf::DW_LANG_C, DBuilder->createFile("fib.ks", "."),
      "Kaleidoscope Compiler", false, "", 0);

There are a couple of things to note here. First, while we're producing a
compile unit for a language called Kaleidoscope we used the language
constant for C. This is because a debugger wouldn't necessarily understand
the calling conventions or default ABI for a language it doesn't recognize
and we follow the C ABI in our LLVM code generation so it's the closest
thing to accurate. This ensures we can actually call functions from the
debugger and have them execute. Secondly, you'll see the "fib.ks" in the
call to ``createCompileUnit``. This is a default hard coded value since
we're using shell redirection to put our source into the Kaleidoscope
compiler. In a usual front end you'd have an input file name and it would
go there.

One last thing as part of emitting debug information via DIBuilder is that
we need to "finalize" the debug information. The reasons are part of the
underlying API for DIBuilder, but make sure you do this near the end of
main:

.. code-block:: c++

  DBuilder->finalize();

before you dump out the module.

Functions
=========

Now that we have our ``Compile Unit`` and our source locations, we can add
function definitions to the debug info. So in ``FunctionAST::codegen()`` we
add a few lines of code to describe a context for our subprogram, in this
case the "File", and the actual definition of the function itself.

So the context:

.. code-block:: c++

  DIFile *Unit = DBuilder->createFile(KSDbgInfo.TheCU->getFilename(),
                                      KSDbgInfo.TheCU->getDirectory());

giving us an DIFile and asking the ``Compile Unit`` we created above for the
directory and filename where we are currently. Then, for now, we use some
source locations of 0 (since our AST doesn't currently have source location
information) and construct our function definition:

.. code-block:: c++

  DIScope *FContext = Unit;
  unsigned LineNo = 0;
  unsigned ScopeLine = 0;
  DISubprogram *SP = DBuilder->createFunction(
      FContext, P.getName(), StringRef(), Unit, LineNo,
      CreateFunctionType(TheFunction->arg_size()),
      ScopeLine,
      DINode::FlagPrototyped,
      DISubprogram::SPFlagDefinition);
  TheFunction->setSubprogram(SP);

and we now have an DISubprogram that contains a reference to all of our
metadata for the function.

Source Locations
================

The most important thing for debug information is accurate source location -
this makes it possible to map your source code back. We have a problem though,
Kaleidoscope really doesn't have any source location information in the lexer
or parser so we'll need to add it.

.. code-block:: c++

   struct SourceLocation {
     int Line;
     int Col;
   };
   static SourceLocation CurLoc;
   static SourceLocation LexLoc = {1, 0};

   static int advance() {
     int LastChar = getchar();

     if (LastChar == '\n' || LastChar == '\r') {
       LexLoc.Line++;
       LexLoc.Col = 0;
     } else
       LexLoc.Col++;
     return LastChar;
   }

In this set of code we've added some functionality on how to keep track of the
line and column of the "source file". As we lex every token we set our current
current "lexical location" to the assorted line and column for the beginning
of the token. We do this by overriding all of the previous calls to
``getchar()`` with our new ``advance()`` that keeps track of the information
and then we have added to all of our AST classes a source location:

.. code-block:: c++

   class ExprAST {
     SourceLocation Loc;

     public:
       ExprAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}
       virtual ~ExprAST() {}
       virtual Value* codegen() = 0;
       int getLine() const { return Loc.Line; }
       int getCol() const { return Loc.Col; }
       virtual raw_ostream &dump(raw_ostream &out, int ind) {
         return out << ':' << getLine() << ':' << getCol() << '\n';
       }

that we pass down through when we create a new expression:

.. code-block:: c++

   LHS = std::make_unique<BinaryExprAST>(BinLoc, BinOp, std::move(LHS),
                                          std::move(RHS));

giving us locations for each of our expressions and variables.

To make sure that every instruction gets proper source location information,
we have to tell ``Builder`` whenever we're at a new source location.
We use a small helper function for this:

.. code-block:: c++

  void DebugInfo::emitLocation(ExprAST *AST) {
    if (!AST)
      return Builder->SetCurrentDebugLocation(DebugLoc());
    DIScope *Scope;
    if (LexicalBlocks.empty())
      Scope = TheCU;
    else
      Scope = LexicalBlocks.back();
    Builder->SetCurrentDebugLocation(
        DILocation::get(Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
  }

This both tells the main ``IRBuilder`` where we are, but also what scope
we're in. The scope can either be on compile-unit level or be the nearest
enclosing lexical block like the current function.
To represent this we create a stack of scopes in ``DebugInfo``:

.. code-block:: c++

   std::vector<DIScope *> LexicalBlocks;

and push the scope (function) to the top of the stack when we start
generating the code for each function:

.. code-block:: c++

  KSDbgInfo.LexicalBlocks.push_back(SP);

Also, we may not forget to pop the scope back off of the scope stack at the
end of the code generation for the function:

.. code-block:: c++

  // Pop off the lexical block for the function since we added it
  // unconditionally.
  KSDbgInfo.LexicalBlocks.pop_back();

Then we make sure to emit the location every time we start to generate code
for a new AST object:

.. code-block:: c++

   KSDbgInfo.emitLocation(this);

Variables
=========

Now that we have functions, we need to be able to print out the variables
we have in scope. Let's get our function arguments set up so we can get
decent backtraces and see how our functions are being called. It isn't
a lot of code, and we generally handle it when we're creating the
argument allocas in ``FunctionAST::codegen``.

.. code-block:: c++

    // Record the function arguments in the NamedValues map.
    NamedValues.clear();
    unsigned ArgIdx = 0;
    for (auto &Arg : TheFunction->args()) {
      // Create an alloca for this variable.
      AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

      // Create a debug descriptor for the variable.
      DILocalVariable *D = DBuilder->createParameterVariable(
          SP, Arg.getName(), ++ArgIdx, Unit, LineNo, KSDbgInfo.getDoubleTy(),
          true);

      DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                              DILocation::get(SP->getContext(), LineNo, 0, SP),
                              Builder->GetInsertBlock());

      // Store the initial value into the alloca.
      Builder->CreateStore(&Arg, Alloca);

      // Add arguments to variable symbol table.
      NamedValues[std::string(Arg.getName())] = Alloca;
    }


Here we're first creating the variable, giving it the scope (``SP``),
the name, source location, type, and since it's an argument, the argument
index. Next, we create a ``#dbg_declare`` record to indicate at the IR
level that we've got a variable in an alloca (and it gives a starting
location for the variable), and setting a source location for the
beginning of the scope on the declare.

One interesting thing to note at this point is that various debuggers have
assumptions based on how code and debug information was generated for them
in the past. In this case we need to do a little bit of a hack to avoid
generating line information for the function prologue so that the debugger
knows to skip over those instructions when setting a breakpoint. So in
``FunctionAST::CodeGen`` we add some more lines:

.. code-block:: c++

  // Unset the location for the prologue emission (leading instructions with no
  // location in a function are considered part of the prologue and the debugger
  // will run past them when breaking on a function)
  KSDbgInfo.emitLocation(nullptr);

and then emit a new location when we actually start generating code for the
body of the function:

.. code-block:: c++

  KSDbgInfo.emitLocation(Body.get());

With this we have enough debug information to set breakpoints in functions,
print out argument variables, and call functions. Not too bad for just a
few simple lines of code!

Full Code Listing
=================

Here is the complete code listing for our running example, enhanced with
debug information. To build this example, use:

.. code-block:: bash

    # Compile
    clang++ -g toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o toy
    # Run
    ./toy

Here is the code:

.. literalinclude:: ../../../examples/Kaleidoscope/Chapter9/toy.cpp
   :language: c++

`Next: Conclusion and other useful LLVM tidbits <LangImpl10.html>`_

