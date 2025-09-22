============================================================
Extending LLVM: Adding instructions, intrinsics, types, etc.
============================================================

Introduction and Warning
========================


During the course of using LLVM, you may wish to customize it for your research
project or for experimentation. At this point, you may realize that you need to
add something to LLVM, whether it be a new fundamental type, a new intrinsic
function, or a whole new instruction.

When you come to this realization, stop and think. Do you really need to extend
LLVM? Is it a new fundamental capability that LLVM does not support at its
current incarnation or can it be synthesized from already pre-existing LLVM
elements? If you are not sure, ask on the `LLVM forums
<https://discourse.llvm.org>`_. The reason is that
extending LLVM will get involved as you need to update all the different passes
that you intend to use with your extension, and there are ``many`` LLVM analyses
and transformations, so it may be quite a bit of work.

Adding an `intrinsic function`_ is far easier than adding an
instruction, and is transparent to optimization passes.  If your added
functionality can be expressed as a function call, an intrinsic function is the
method of choice for LLVM extension.

Before you invest a significant amount of effort into a non-trivial extension,
**ask on the list** if what you are looking to do can be done with
already-existing infrastructure, or if maybe someone else is already working on
it. You will save yourself a lot of time and effort by doing so.

.. _intrinsic function:

Adding a new intrinsic function
===============================

Adding a new intrinsic function to LLVM is much easier than adding a new
instruction.  Almost all extensions to LLVM should start as an intrinsic
function and then be turned into an instruction if warranted.

#. ``llvm/docs/LangRef.html``:

   Document the intrinsic.  Decide whether it is code generator specific and
   what the restrictions are.  Talk to other people about it so that you are
   sure it's a good idea.

#. ``llvm/include/llvm/IR/Intrinsics*.td``:

   Add an entry for your intrinsic.  Describe its memory access
   characteristics for optimization (this controls whether it will be
   DCE'd, CSE'd, etc). If any arguments need to be immediates, these
   must be indicated with the ImmArg property. Note that any intrinsic
   using one of the ``llvm_any*_ty`` types for an argument or return
   type will be deemed by ``tblgen`` as overloaded and the
   corresponding suffix will be required on the intrinsic's name.

#. ``llvm/lib/Analysis/ConstantFolding.cpp``:

   If it is possible to constant fold your intrinsic, add support to it in the
   ``canConstantFoldCallTo`` and ``ConstantFoldCall`` functions.

#. ``llvm/test/*``:

   Add test cases for your test cases to the test suite

Once the intrinsic has been added to the system, you must add code generator
support for it.  Generally you must do the following steps:

Add support to the .td file for the target(s) of your choice in
``lib/Target/*/*.td``.

  This is usually a matter of adding a pattern to the .td file that matches the
  intrinsic, though it may obviously require adding the instructions you want to
  generate as well.  There are lots of examples in the PowerPC and X86 backend
  to follow.

Adding a new SelectionDAG node
==============================

As with intrinsics, adding a new SelectionDAG node to LLVM is much easier than
adding a new instruction.  New nodes are often added to help represent
instructions common to many targets.  These nodes often map to an LLVM
instruction (add, sub) or intrinsic (byteswap, population count).  In other
cases, new nodes have been added to allow many targets to perform a common task
(converting between floating point and integer representation) or capture more
complicated behavior in a single node (rotate).

#. ``include/llvm/CodeGen/ISDOpcodes.h``:

   Add an enum value for the new SelectionDAG node.

#. ``lib/CodeGen/SelectionDAG/SelectionDAG.cpp``:

   Add code to print the node to ``getOperationName``.  If your new node can be
    evaluated at compile time when given constant arguments (such as an add of a
    constant with another constant), find the ``getNode`` method that takes the
    appropriate number of arguments, and add a case for your node to the switch
    statement that performs constant folding for nodes that take the same number
    of arguments as your new node.

#. ``lib/CodeGen/SelectionDAG/LegalizeDAG.cpp``:

   Add code to `legalize, promote, and expand
   <CodeGenerator.html#selectiondag_legalize>`_ the node as necessary.  At a
   minimum, you will need to add a case statement for your node in
   ``LegalizeOp`` which calls LegalizeOp on the node's operands, and returns a
   new node if any of the operands changed as a result of being legalized.  It
   is likely that not all targets supported by the SelectionDAG framework will
   natively support the new node.  In this case, you must also add code in your
   node's case statement in ``LegalizeOp`` to Expand your node into simpler,
   legal operations.  The case for ``ISD::UREM`` for expanding a remainder into
   a divide, multiply, and a subtract is a good example.

#. ``lib/CodeGen/SelectionDAG/LegalizeDAG.cpp``:

   If targets may support the new node being added only at certain sizes, you
    will also need to add code to your node's case statement in ``LegalizeOp``
    to Promote your node's operands to a larger size, and perform the correct
    operation.  You will also need to add code to ``PromoteOp`` to do this as
    well.  For a good example, see ``ISD::BSWAP``, which promotes its operand to
    a wider size, performs the byteswap, and then shifts the correct bytes right
    to emulate the narrower byteswap in the wider type.

#. ``lib/CodeGen/SelectionDAG/LegalizeDAG.cpp``:

   Add a case for your node in ``ExpandOp`` to teach the legalizer how to
   perform the action represented by the new node on a value that has been split
   into high and low halves.  This case will be used to support your node with a
   64 bit operand on a 32 bit target.

#. ``lib/CodeGen/SelectionDAG/DAGCombiner.cpp``:

   If your node can be combined with itself, or other existing nodes in a
   peephole-like fashion, add a visit function for it, and call that function
   from. There are several good examples for simple combines you can do;
   ``visitFABS`` and ``visitSRL`` are good starting places.

#. ``lib/Target/PowerPC/PPCISelLowering.cpp``:

   Each target has an implementation of the ``TargetLowering`` class, usually in
   its own file (although some targets include it in the same file as the
   DAGToDAGISel).  The default behavior for a target is to assume that your new
   node is legal for all types that are legal for that target.  If this target
   does not natively support your node, then tell the target to either Promote
   it (if it is supported at a larger type) or Expand it.  This will cause the
   code you wrote in ``LegalizeOp`` above to decompose your new node into other
   legal nodes for this target.

#. ``include/llvm/Target/TargetSelectionDAG.td``:

   Most current targets supported by LLVM generate code using the DAGToDAG
   method, where SelectionDAG nodes are pattern matched to target-specific
   nodes, which represent individual instructions.  In order for the targets to
   match an instruction to your new node, you must add a def for that node to
   the list in this file, with the appropriate type constraints. Look at
   ``add``, ``bswap``, and ``fadd`` for examples.

#. ``lib/Target/PowerPC/PPCInstrInfo.td``:

   Each target has a tablegen file that describes the target's instruction set.
   For targets that use the DAGToDAG instruction selection framework, add a
   pattern for your new node that uses one or more target nodes.  Documentation
   for this is a bit sparse right now, but there are several decent examples.
   See the patterns for ``rotl`` in ``PPCInstrInfo.td``.

#. TODO: document complex patterns.

#. ``llvm/test/CodeGen/*``:

   Add test cases for your new node to the test suite.
   ``llvm/test/CodeGen/X86/bswap.ll`` is a good example.

Adding a new instruction
========================

.. warning::

  Adding instructions changes the bitcode format, and it will take some effort
  to maintain compatibility with the previous version. Only add an instruction
  if it is absolutely necessary.

#. ``llvm/include/llvm/IR/Instruction.def``:

   add a number for your instruction and an enum name

#. ``llvm/include/llvm/IR/Instructions.h``:

   add a definition for the class that will represent your instruction

#. ``llvm/include/llvm/IR/InstVisitor.h``:

   add a prototype for a visitor to your new instruction type

#. ``llvm/lib/AsmParser/LLLexer.cpp``:

   add a new token to parse your instruction from assembly text file

#. ``llvm/lib/AsmParser/LLParser.cpp``:

   add the grammar on how your instruction can be read and what it will
   construct as a result

#. ``llvm/lib/Bitcode/Reader/BitcodeReader.cpp``:

   add a case for your instruction and how it will be parsed from bitcode

#. ``llvm/lib/Bitcode/Writer/BitcodeWriter.cpp``:

   add a case for your instruction and how it will be parsed from bitcode

#. ``llvm/lib/IR/Instruction.cpp``:

   add a case for how your instruction will be printed out to assembly

#. ``llvm/lib/IR/Instructions.cpp``:

   implement the class you defined in ``llvm/include/llvm/Instructions.h``

#. Test your instruction

#. ``llvm/lib/Target/*``:

   add support for your instruction to code generators, or add a lowering pass.

#. ``llvm/test/*``:

   add your test cases to the test suite.

Also, you need to implement (or modify) any analyses or passes that you want to
understand this new instruction.

Adding a new type
=================

.. warning::

  Adding new types changes the bitcode format, and will break compatibility with
  currently-existing LLVM installations. Only add new types if it is absolutely
  necessary.

Adding a fundamental type
-------------------------

#. ``llvm/include/llvm/IR/Type.h``:

   add enum for the new type; add static ``Type*`` for this type

#. ``llvm/lib/IR/Type.cpp`` and ``llvm/lib/CodeGen/ValueTypes.cpp``:

   add mapping from ``TypeID`` => ``Type*``; initialize the static ``Type*``

#. ``llvm/include/llvm-c/Core.h`` and ``llvm/lib/IR/Core.cpp``:

   add enum ``LLVMTypeKind`` and modify
   ``LLVMTypeKind LLVMGetTypeKind(LLVMTypeRef Ty)`` for the new type

#. ``llvm/lib/AsmParser/LLLexer.cpp``:

   add ability to parse in the type from text assembly

#. ``llvm/lib/AsmParser/LLParser.cpp``:

   add a token for that type

#. ``llvm/lib/Bitcode/Writer/BitcodeWriter.cpp``:

   modify ``void ModuleBitcodeWriter::writeTypeTable()`` to serialize your type

#. ``llvm/lib/Bitcode/Reader/BitcodeReader.cpp``:

   modify ``Error BitcodeReader::parseTypeTableBody()`` to read your data type

#. ``include/llvm/Bitcode/LLVMBitCodes.h``:

   add enum ``TypeCodes`` for the new type

Adding a derived type
---------------------

#. ``llvm/include/llvm/IR/Type.h``:

   add enum for the new type; add a forward declaration of the type also

#. ``llvm/include/llvm/IR/DerivedTypes.h``:

   add new class to represent new class in the hierarchy; add forward
   declaration to the TypeMap value type

#. ``llvm/lib/IR/Type.cpp`` and ``llvm/lib/CodeGen/ValueTypes.cpp``:

   add support for derived type, notably `enum TypeID` and `is`, `get` methods.

#. ``llvm/include/llvm-c/Core.h`` and ``llvm/lib/IR/Core.cpp``:

   add enum ``LLVMTypeKind`` and modify
   `LLVMTypeKind LLVMGetTypeKind(LLVMTypeRef Ty)` for the new type

#. ``llvm/lib/AsmParser/LLLexer.cpp``:

   modify ``lltok::Kind LLLexer::LexIdentifier()`` to add ability to
   parse in the type from text assembly

#. ``llvm/lib/Bitcode/Writer/BitcodeWriter.cpp``:

   modify ``void ModuleBitcodeWriter::writeTypeTable()`` to serialize your type

#. ``llvm/lib/Bitcode/Reader/BitcodeReader.cpp``:

   modify ``Error BitcodeReader::parseTypeTableBody()`` to read your data type

#. ``include/llvm/Bitcode/LLVMBitCodes.h``:

   add enum ``TypeCodes`` for the new type

#. ``llvm/lib/IR/AsmWriter.cpp``:

   modify ``void TypePrinting::print(Type *Ty, raw_ostream &OS)``
   to output the new derived type
