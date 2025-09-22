================================
Source Level Debugging with LLVM
================================

.. contents::
   :local:

Introduction
============

This document is the central repository for all information pertaining to debug
information in LLVM.  It describes the :ref:`actual format that the LLVM debug
information takes <format>`, which is useful for those interested in creating
front-ends or dealing directly with the information.  Further, this document
provides specific examples of what debug information for C/C++ looks like.

Philosophy behind LLVM debugging information
--------------------------------------------

The idea of the LLVM debugging information is to capture how the important
pieces of the source-language's Abstract Syntax Tree map onto LLVM code.
Several design aspects have shaped the solution that appears here.  The
important ones are:

* Debugging information should have very little impact on the rest of the
  compiler.  No transformations, analyses, or code generators should need to
  be modified because of debugging information.

* LLVM optimizations should interact in :ref:`well-defined and easily described
  ways <intro_debugopt>` with the debugging information.

* Because LLVM is designed to support arbitrary programming languages,
  LLVM-to-LLVM tools should not need to know anything about the semantics of
  the source-level-language.

* Source-level languages are often **widely** different from one another.
  LLVM should not put any restrictions of the flavor of the source-language,
  and the debugging information should work with any language.

* With code generator support, it should be possible to use an LLVM compiler
  to compile a program to native machine code and standard debugging
  formats.  This allows compatibility with traditional machine-code level
  debuggers, like GDB or DBX.

The approach used by the LLVM implementation is to use a small set of
:ref:`debug records <debug_records>` to define a mapping
between LLVM program objects and the source-level objects.  The description of
the source-level program is maintained in LLVM metadata in an
:ref:`implementation-defined format <ccxx_frontend>` (the C/C++ front-end
currently uses working draft 7 of the `DWARF 3 standard
<http://www.eagercon.com/dwarf/dwarf3std.htm>`_).

When a program is being debugged, a debugger interacts with the user and turns
the stored debug information into source-language specific information.  As
such, a debugger must be aware of the source-language, and is thus tied to a
specific language or family of languages.

Debug information consumers
---------------------------

The role of debug information is to provide meta information normally stripped
away during the compilation process.  This meta information provides an LLVM
user a relationship between generated code and the original program source
code.

Currently, there are two backend consumers of debug info: DwarfDebug and
CodeViewDebug. DwarfDebug produces DWARF suitable for use with GDB, LLDB, and
other DWARF-based debuggers. :ref:`CodeViewDebug <codeview>` produces CodeView,
the Microsoft debug info format, which is usable with Microsoft debuggers such
as Visual Studio and WinDBG. LLVM's debug information format is mostly derived
from and inspired by DWARF, but it is feasible to translate into other target
debug info formats such as STABS.

It would also be reasonable to use debug information to feed profiling tools
for analysis of generated code, or, tools for reconstructing the original
source from generated code.

.. _intro_debugopt:

Debug information and optimizations
-----------------------------------

An extremely high priority of LLVM debugging information is to make it interact
well with optimizations and analysis.  In particular, the LLVM debug
information provides the following guarantees:

* LLVM debug information **always provides information to accurately read
  the source-level state of the program**, regardless of which LLVM
  optimizations have been run. :doc:`HowToUpdateDebugInfo` specifies how debug
  info should be updated in various kinds of code transformations to avoid
  breaking this guarantee, and how to preserve as much useful debug info as
  possible.  Note that some optimizations may impact the ability to modify the
  current state of the program with a debugger, such as setting program
  variables, or calling functions that have been deleted.

* As desired, LLVM optimizations can be upgraded to be aware of debugging
  information, allowing them to update the debugging information as they
  perform aggressive optimizations.  This means that, with effort, the LLVM
  optimizers could optimize debug code just as well as non-debug code.

* LLVM debug information does not prevent optimizations from
  happening (for example inlining, basic block reordering/merging/cleanup,
  tail duplication, etc).

* LLVM debug information is automatically optimized along with the rest of
  the program, using existing facilities.  For example, duplicate
  information is automatically merged by the linker, and unused information
  is automatically removed.

Basically, the debug information allows you to compile a program with
"``-O0 -g``" and get full debug information, allowing you to arbitrarily modify
the program as it executes from a debugger.  Compiling a program with
"``-O3 -g``" gives you full debug information that is always available and
accurate for reading (e.g., you get accurate stack traces despite tail call
elimination and inlining), but you might lose the ability to modify the program
and call functions which were optimized out of the program, or inlined away
completely.

The :doc:`LLVM test-suite <TestSuiteMakefileGuide>` provides a framework to
test the optimizer's handling of debugging information.  It can be run like
this:

.. code-block:: bash

  % cd llvm/projects/test-suite/MultiSource/Benchmarks  # or some other level
  % make TEST=dbgopt

This will test impact of debugging information on optimization passes.  If
debugging information influences optimization passes then it will be reported
as a failure.  See :doc:`TestingGuide` for more information on LLVM test
infrastructure and how to run various tests.

.. _format:

Debugging information format
============================

LLVM debugging information has been carefully designed to make it possible for
the optimizer to optimize the program and debugging information without
necessarily having to know anything about debugging information.  In
particular, the use of metadata avoids duplicated debugging information from
the beginning, and the global dead code elimination pass automatically deletes
debugging information for a function if it decides to delete the function.

To do this, most of the debugging information (descriptors for types,
variables, functions, source files, etc) is inserted by the language front-end
in the form of LLVM metadata.

Debug information is designed to be agnostic about the target debugger and
debugging information representation (e.g. DWARF/Stabs/etc).  It uses a generic
pass to decode the information that represents variables, types, functions,
namespaces, etc: this allows for arbitrary source-language semantics and
type-systems to be used, as long as there is a module written for the target
debugger to interpret the information.

To provide basic functionality, the LLVM debugger does have to make some
assumptions about the source-level language being debugged, though it keeps
these to a minimum.  The only common features that the LLVM debugger assumes
exist are `source files <LangRef.html#difile>`_, and `program objects
<LangRef.html#diglobalvariable>`_.  These abstract objects are used by a
debugger to form stack traces, show information about local variables, etc.

This section of the documentation first describes the representation aspects
common to any source-language.  :ref:`ccxx_frontend` describes the data layout
conventions used by the C and C++ front-ends.

Debug information descriptors are `specialized metadata nodes
<LangRef.html#specialized-metadata>`_, first-class subclasses of ``Metadata``.

There are two models for defining the values of source variables at different
states of the program and tracking these values through optimization and code
generation: :ref:`debug records <debug_records>`, the current default, and
:ref:`intrinsic function calls <format_common_intrinsics>`, which are
non-default but currently supported for backwards compatibility - though these
two models must never be mixed within an IR module. For an explanation of why
we changed to the new model, how it works, and guidance on how to update old
code or IR to use debug records, see the `RemoveDIs <RemoveDIsDebugInfo.html>`_
document.

.. _debug_records:

Debug Records
----------------------------

Debug records define the value that a source variable has during execution of
the program; they appear interleaved with instructions, although they are not
instructions themselves and have no effect on the code generated by the
compiler.

LLVM uses several types of debug records to define source variables. The
common syntax for these records is:

.. code-block:: llvm

    #dbg_<kind>([<arg>, ]* <DILocation>)
  ; Using the intrinsic model, the above is equivalent to:
  call void llvm.dbg.<kind>([metadata <arg>, ]*), !dbg <DILocation>

Debug records are always printed with an extra level of indentation compared
to instructions, and always have the prefix `#dbg_` and a list of
comma-separated arguments in parentheses, as with a `call`.

``#dbg_declare``
^^^^^^^^^^^^^^^^

.. code-block:: llvm

    #dbg_declare([Value|MDNode], DILocalVariable, DIExpression, DILocation)

This record provides information about a local element (e.g., variable).
The first argument is an SSA value corresponding to a variable address, and is
typically a static alloca in the function entry block.  The second argument is a
`local variable <LangRef.html#dilocalvariable>`_ containing a description of
the variable.  The third argument is a `complex expression
<LangRef.html#diexpression>`_. The fourth argument is a `source location
<LangRef.html#dilocation>`_. A ``#dbg_declare`` record describes the
*address* of a source variable.

.. code-block:: llvm

    %i.addr = alloca i32, align 4
      #dbg_declare(ptr %i.addr, !1, !DIExpression(), !2)
    ; ...
    !1 = !DILocalVariable(name: "i", ...) ; int i
    !2 = !DILocation(...)
    ; ...
    %buffer = alloca [256 x i8], align 8
    ; The address of i is buffer+64.
      #dbg_declare(ptr %buffer, !3, !DIExpression(DW_OP_plus, 64), !4)
    ; ...
    !3 = !DILocalVariable(name: "i", ...) ; int i
    !4 = !DILocation(...)

A frontend should generate exactly one ``#dbg_declare`` record at the point
of declaration of a source variable. Optimization passes that fully promote the
variable from memory to SSA values will replace this record with possibly
multiple ``#dbg_value``` records. Passes that delete stores are effectively
partial promotion, and they will insert a mix of ``#dbg_value`` records to
track the source variable value when it is available. After optimization, there
may be multiple ``#dbg_declare`` records describing the program points where
the variables lives in memory. All calls for the same concrete source variable
must agree on the memory location.


``#dbg_value``
^^^^^^^^^^^^^^

.. code-block:: llvm

    #dbg_value([Value|DIArgList|MDNode], DILocalVariable, DIExpression, DILocation)

This record provides information when a user source variable is set to a new
value.  The first argument is the new value. The second argument is a `local
variable <LangRef.html#dilocalvariable>`_ containing a description of the
variable.  The third argument is a `complex expression
<LangRef.html#diexpression>`_. The fourth argument is a `source location
<LangRef.html#dilocation>`_.

A ``#dbg_value`` record describes the *value* of a source variable
directly, not its address.  Note that the value operand of this intrinsic may
be indirect (i.e, a pointer to the source variable), provided that interpreting
the complex expression derives the direct value.

``#dbg_assign``
^^^^^^^^^^^^^^^^^^^
.. toctree::
   :hidden:

   AssignmentTracking

.. code-block:: llvm

  #dbg_assign( [Value|DIArgList|MDNode] Value,
               DILocalVariable Variable,
               DIExpression ValueExpression,
               DIAssignID ID,
               [Value|MDNode] Address,
               DIExpression AddressExpression,
               DILocation SourceLocation )

This record marks the position in IR where a source assignment occurred. It
encodes the value of the variable. It references the store, if any, that
performs the assignment, and the destination address.

The first three arguments are the same as for a ``#dbg_value``. The fourth
argument is a ``DIAssignID`` used to reference a store. The fifth is the
destination of the store, the sixth is a `complex
expression <LangRef.html#diexpression>`_ that modfies it, and the seventh is a
`source location <LangRef.html#dilocation>`_.

See :doc:`AssignmentTracking` for more info.

Debugger intrinsic functions
----------------------------

.. _format_common_intrinsics:

In intrinsic-mode, LLVM uses several intrinsic functions (name prefixed with "``llvm.dbg``") to
track source local variables through optimization and code generation. These
intrinsic functions each correspond to one of the debug records above, with a
few syntactic differences: each argument to a debugger intrinsic must be wrapped
as metadata, meaning it must be prefixed with ``metadata``, and the
``DILocation`` argument in each record must be a metadata attachment to the
call instruction, meaning it appears after the argument list with the prefix
``!dbg``.

``llvm.dbg.declare``
^^^^^^^^^^^^^^^^^^^^

.. code-block:: llvm

  void @llvm.dbg.declare(metadata, metadata, metadata)

This intrinsic is equivalent to ``#dbg_declare``:

.. code-block:: llvm

      #dbg_declare(i32* %i.addr, !1, !DIExpression(), !2)
    call void @llvm.dbg.declare(metadata i32* %i.addr, metadata !1,
                                metadata !DIExpression()), !dbg !2

``llvm.dbg.value``
^^^^^^^^^^^^^^^^^^

.. code-block:: llvm

  void @llvm.dbg.value(metadata, metadata, metadata)

This intrinsic is equivalent to ``#dbg_value``:

.. code-block:: llvm

      #dbg_value(i32 %i, !1, !DIExpression(), !2)
    call void @llvm.dbg.value(metadata i32 %i, metadata !1,
                              metadata !DIExpression()), !dbg !2

``llvm.dbg.assign``
^^^^^^^^^^^^^^^^^^^

.. code-block:: llvm

  void @llvm.dbg.assign(metadata, metadata, metadata, metadata, metadata, metadata)

This intrinsic is equivalent to ``#dbg_assign``:

.. code-block:: llvm

      #dbg_assign(i32 %i, !1, !DIExpression(), !2, 
                  ptr %i.addr, !DIExpression(), !3)
    call void @llvm.dbg.assign(
      metadata i32 %i, metadata !1, metadata !DIExpression(), metadata !2,
      metadata ptr %i.addr, metadata !DIExpression(), metadata !3), !dbg !3


Object lifetimes and scoping
============================

In many languages, the local variables in functions can have their lifetimes or
scopes limited to a subset of a function.  In the C family of languages, for
example, variables are only live (readable and writable) within the source
block that they are defined in.  In functional languages, values are only
readable after they have been defined.  Though this is a very obvious concept,
it is non-trivial to model in LLVM, because it has no notion of scoping in this
sense, and does not want to be tied to a language's scoping rules.

In order to handle this, the LLVM debug format uses the metadata attached to
llvm instructions to encode line number and scoping information.  Consider the
following C fragment, for example:

.. code-block:: c

  1.  void foo() {
  2.    int X = 21;
  3.    int Y = 22;
  4.    {
  5.      int Z = 23;
  6.      Z = X;
  7.    }
  8.    X = Y;
  9.  }

Compiled to LLVM, this function would be represented like this:

.. code-block:: text

  ; Function Attrs: nounwind ssp uwtable
  define void @foo() #0 !dbg !4 {
  entry:
    %X = alloca i32, align 4
    %Y = alloca i32, align 4
    %Z = alloca i32, align 4
      #dbg_declare(ptr %X, !11, !DIExpression(), !14)
    store i32 21, i32* %X, align 4, !dbg !14
      #dbg_declare(ptr %Y, !15, !DIExpression(), !16)
    store i32 22, i32* %Y, align 4, !dbg !16
      #dbg_declare(ptr %Z, !17, !DIExpression(), !19)
    store i32 23, i32* %Z, align 4, !dbg !19
    %0 = load i32, i32* %X, align 4, !dbg !20
    store i32 %0, i32* %Z, align 4, !dbg !21
    %1 = load i32, i32* %Y, align 4, !dbg !22
    store i32 %1, i32* %X, align 4, !dbg !23
    ret void, !dbg !24
  }

  attributes #0 = { nounwind ssp uwtable "less-precise-fpmad"="false" "frame-pointer"="all" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
  attributes #1 = { nounwind readnone }

  !llvm.dbg.cu = !{!0}
  !llvm.module.flags = !{!7, !8, !9}
  !llvm.ident = !{!10}

  !0 = !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 3.7.0 (trunk 231150) (llvm/trunk 231154)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, retainedTypes: !2, subprograms: !3, globals: !2, imports: !2)
  !1 = !DIFile(filename: "/dev/stdin", directory: "/Users/dexonsmith/data/llvm/debug-info")
  !2 = !{}
  !3 = !{!4}
  !4 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 1, type: !5, isLocal: false, isDefinition: true, scopeLine: 1, isOptimized: false, retainedNodes: !2)
  !5 = !DISubroutineType(types: !6)
  !6 = !{null}
  !7 = !{i32 2, !"Dwarf Version", i32 2}
  !8 = !{i32 2, !"Debug Info Version", i32 3}
  !9 = !{i32 1, !"PIC Level", i32 2}
  !10 = !{!"clang version 3.7.0 (trunk 231150) (llvm/trunk 231154)"}
  !11 = !DILocalVariable(name: "X", scope: !4, file: !1, line: 2, type: !12)
  !12 = !DIBasicType(name: "int", size: 32, align: 32, encoding: DW_ATE_signed)
  !13 = !DILocation(line: 2, column: 9, scope: !4)
  !14 = !DILocalVariable(name: "Y", scope: !4, file: !1, line: 3, type: !12)
  !15 = !DILocation(line: 3, column: 9, scope: !4)
  !16 = !DILocalVariable(name: "Z", scope: !18, file: !1, line: 5, type: !12)
  !17 = distinct !DILexicalBlock(scope: !4, file: !1, line: 4, column: 5)
  !18 = !DILocation(line: 5, column: 11, scope: !18)
  !29 = !DILocation(line: 6, column: 11, scope: !18)
  !20 = !DILocation(line: 6, column: 9, scope: !18)
  !21 = !DILocation(line: 8, column: 9, scope: !4)
  !22 = !DILocation(line: 8, column: 7, scope: !4)
  !23 = !DILocation(line: 9, column: 3, scope: !4)


This example illustrates a few important details about LLVM debugging
information.  In particular, it shows how the ``#dbg_declare`` record and
location information, which are attached to an instruction, are applied
together to allow a debugger to analyze the relationship between statements,
variable definitions, and the code used to implement the function.

.. code-block:: llvm

    #dbg_declare(ptr %X, !11, !DIExpression(), !14)
    ; [debug line = 2:7] [debug variable = X]

The first record ``#dbg_declare`` encodes debugging information for the
variable ``X``.  The location ``!14`` at the end of the record provides
scope information for the variable ``X``.

.. code-block:: text

  !14 = !DILocation(line: 2, column: 9, scope: !4)
  !4 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 1, type: !5,
                              isLocal: false, isDefinition: true, scopeLine: 1,
                              isOptimized: false, retainedNodes: !2)

Here ``!14`` is metadata providing `location information
<LangRef.html#dilocation>`_.  In this example, scope is encoded by ``!4``, a
`subprogram descriptor <LangRef.html#disubprogram>`_.  This way the location
information parameter to the records indicates that the variable ``X`` is
declared at line number 2 at a function level scope in function ``foo``.

Now lets take another example.

.. code-block:: llvm

    #dbg_declare(ptr %Z, !17, !DIExpression(), !19)
    ; [debug line = 5:9] [debug variable = Z]

The third record ``#dbg_declare`` encodes debugging information for
variable ``Z``.  The metadata ``!19`` at the end of the record provides
scope information for the variable ``Z``.

.. code-block:: text

  !18 = distinct !DILexicalBlock(scope: !4, file: !1, line: 4, column: 5)
  !19 = !DILocation(line: 5, column: 11, scope: !18)

Here ``!19`` indicates that ``Z`` is declared at line number 5 and column
number 11 inside of lexical scope ``!18``.  The lexical scope itself resides
inside of subprogram ``!4`` described above.

The scope information attached with each instruction provides a straightforward
way to find instructions covered by a scope.

Object lifetime in optimized code
=================================

In the example above, every variable assignment uniquely corresponds to a
memory store to the variable's position on the stack. However in heavily
optimized code LLVM promotes most variables into SSA values, which can
eventually be placed in physical registers or memory locations. To track SSA
values through compilation, when objects are promoted to SSA values a
``#dbg_value`` record is created for each assignment, recording the
variable's new location. Compared with the ``#dbg_declare`` record:

* A #dbg_value terminates the effect of any preceding #dbg_values for (any
  overlapping fragments of) the specified variable.
* The #dbg_value's position in the IR defines where in the instruction stream
  the variable's value changes.
* Operands can be constants, indicating the variable is assigned a
  constant value.

Care must be taken to update ``#dbg_value`` records when optimization
passes alter or move instructions and blocks -- the developer could observe such
changes reflected in the value of variables when debugging the program. For any
execution of the optimized program, the set of variable values presented to the
developer by the debugger should not show a state that would never have existed
in the execution of the unoptimized program, given the same input. Doing so
risks misleading the developer by reporting a state that does not exist,
damaging their understanding of the optimized program and undermining their
trust in the debugger.

Sometimes perfectly preserving variable locations is not possible, often when a
redundant calculation is optimized out. In such cases, a ``#dbg_value``
with operand ``poison`` should be used, to terminate earlier variable locations
and let the debugger present ``optimized out`` to the developer. Withholding
these potentially stale variable values from the developer diminishes the
amount of available debug information, but increases the reliability of the
remaining information.

To illustrate some potential issues, consider the following example:

.. code-block:: llvm

  define i32 @foo(i32 %bar, i1 %cond) {
  entry:
      #dbg_value(i32 0, !1, !DIExpression(), !4)
    br i1 %cond, label %truebr, label %falsebr
  truebr:
    %tval = add i32 %bar, 1
      #dbg_value(i32 %tval, !1, !DIExpression(), !4)
    %g1 = call i32 @gazonk()
    br label %exit
  falsebr:
    %fval = add i32 %bar, 2
      #dbg_value(i32 %fval, !1, !DIExpression(), !4)
    %g2 = call i32 @gazonk()
    br label %exit
  exit:
    %merge = phi [ %tval, %truebr ], [ %fval, %falsebr ]
    %g = phi [ %g1, %truebr ], [ %g2, %falsebr ]
      #dbg_value(i32 %merge, !1, !DIExpression(), !4)
      #dbg_value(i32 %g, !3, !DIExpression(), !4)
    %plusten = add i32 %merge, 10
    %toret = add i32 %plusten, %g
      #dbg_value(i32 %toret, !1, !DIExpression(), !4)
    ret i32 %toret
  }

Containing two source-level variables in ``!1`` and ``!3``. The function could,
perhaps, be optimized into the following code:

.. code-block:: llvm

  define i32 @foo(i32 %bar, i1 %cond) {
  entry:
    %g = call i32 @gazonk()
    %addoper = select i1 %cond, i32 11, i32 12
    %plusten = add i32 %bar, %addoper
    %toret = add i32 %plusten, %g
    ret i32 %toret
  }

What ``#dbg_value`` records should be placed to represent the original variable
locations in this code? Unfortunately the second, third and fourth
#dbg_values for ``!1`` in the source function have had their operands
(%tval, %fval, %merge) optimized out. Assuming we cannot recover them, we
might consider this placement of #dbg_values:

.. code-block:: llvm

  define i32 @foo(i32 %bar, i1 %cond) {
  entry:
      #dbg_value(i32 0, !1, !DIExpression(), !4)
    %g = call i32 @gazonk()
      #dbg_value(i32 %g, !3, !DIExpression(), !4)
    %addoper = select i1 %cond, i32 11, i32 12
    %plusten = add i32 %bar, %addoper
    %toret = add i32 %plusten, %g
      #dbg_value(i32 %toret, !1, !DIExpression(), !4)
    ret i32 %toret
  }

However, this will cause ``!3`` to have the return value of ``@gazonk()`` at
the same time as ``!1`` has the constant value zero -- a pair of assignments
that never occurred in the unoptimized program. To avoid this, we must terminate
the range that ``!1`` has the constant value assignment by inserting an poison
#dbg_value before the #dbg_value for ``!3``:

.. code-block:: llvm

  define i32 @foo(i32 %bar, i1 %cond) {
  entry:
      #dbg_value(i32 0, !1, !DIExpression(), !2)
    %g = call i32 @gazonk()
      #dbg_value(i32 poison, !1, !DIExpression(), !2)
      #dbg_value(i32 %g, !3, !DIExpression(), !2)
    %addoper = select i1 %cond, i32 11, i32 12
    %plusten = add i32 %bar, %addoper
    %toret = add i32 %plusten, %g
      #dbg_value(i32 %toret, !1, !DIExpression(), !2)
    ret i32 %toret
  }

There are a few other #dbg_value configurations that mean it terminates
dominating location definitions without adding a new location. The complete
list is:

* Any location operand is ``poison`` (or ``undef``).
* Any location operand is an empty metadata tuple (``!{}``) (which cannot
  occur in a ``!DIArgList``).
* There are no location operands (empty ``DIArgList``) and the ``DIExpression``
  is empty.

This class of #dbg_value that kills variable locations is called a "kill
#dbg_value" or "kill location", and for legacy reasons the term "undef
#dbg_value" may be used in existing code. The ``DbgVariableIntrinsic`` methods
``isKillLocation`` and ``setKillLocation`` should be used where possible rather
than inspecting location operands directly to check or set whether a #dbg_value
is a kill location.

In general, if any #dbg_value has its operand optimized out and cannot be
recovered, then a kill #dbg_value is necessary to terminate earlier variable
locations. Additional kill #dbg_values may be necessary when the debugger can
observe re-ordering of assignments.

How variable location metadata is transformed during CodeGen
============================================================

LLVM preserves debug information throughout mid-level and backend passes,
ultimately producing a mapping between source-level information and
instruction ranges. This
is relatively straightforwards for line number information, as mapping
instructions to line numbers is a simple association. For variable locations
however the story is more complex. As each ``#dbg_value`` record
represents a source-level assignment of a value to a source variable, the
debug records effectively embed a small imperative program
within the LLVM IR. By the end of CodeGen, this becomes a mapping from each
variable to their machine locations over ranges of instructions.
From IR to object emission, the major transformations which affect variable
location fidelity are:

1. Instruction Selection
2. Register allocation
3. Block layout

each of which are discussed below. In addition, instruction scheduling can
significantly change the ordering of the program, and occurs in a number of
different passes.

Some variable locations are not transformed during CodeGen. Stack locations
specified by ``#dbg_declare`` are valid and unchanging for the entire
duration of the function, and are recorded in a simple MachineFunction table.
Location changes in the prologue and epilogue of a function are also ignored:
frame setup and destruction may take several instructions, require a
disproportionate amount of debugging information in the output binary to
describe, and should be stepped over by debuggers anyway.

Variable locations in Instruction Selection and MIR
---------------------------------------------------

Instruction selection creates a MIR function from an IR function, and just as
it transforms ``intermediate`` instructions into machine instructions, so must
``intermediate`` variable locations become machine variable locations.
Within IR, variable locations are always identified by a Value, but in MIR
there can be different types of variable locations. In addition, some IR
locations become unavailable, for example if the operation of multiple IR
instructions are combined into one machine instruction (such as
multiply-and-accumulate) then intermediate Values are lost. To track variable
locations through instruction selection, they are first separated into
locations that do not depend on code generation (constants, stack locations,
allocated virtual registers) and those that do. For those that do, debug
metadata is attached to SDNodes in SelectionDAGs. After instruction selection
has occurred and a MIR function is created, if the SDNode associated with debug
metadata is allocated a virtual register, that virtual register is used as the
variable location. If the SDNode is folded into a machine instruction or
otherwise transformed into a non-register, the variable location becomes
unavailable.

Locations that are unavailable are treated as if they have been optimized out:
in IR the location would be assigned ``undef`` by a debug record, and in MIR
the equivalent location is used.

After MIR locations are assigned to each variable, machine pseudo-instructions
corresponding to each ``#dbg_value`` record are inserted. There are two
forms of this type of instruction.

The first form, ``DBG_VALUE``, appears thus:

.. code-block:: text

  DBG_VALUE %1, $noreg, !123, !DIExpression()

And has the following operands:
 * The first operand can record the variable location as a register,
   a frame index, an immediate, or the base address register if the original
   debug record referred to memory. ``$noreg`` indicates the variable
   location is undefined, equivalent to an ``undef`` #dbg_value operand.
 * The type of the second operand indicates whether the variable location is
   directly referred to by the DBG_VALUE, or whether it is indirect. The
   ``$noreg`` register signifies the former, an immediate operand (0) the
   latter.
 * Operand 3 is the Variable field of the original debug record.
 * Operand 4 is the Expression field of the original debug record.

The second form, ``DBG_VALUE_LIST``, appears thus:

.. code-block:: text

  DBG_VALUE_LIST !123, !DIExpression(DW_OP_LLVM_arg, 0, DW_OP_LLVM_arg, 1, DW_OP_plus), %1, %2

And has the following operands:
 * The first operand is the Variable field of the original debug record.
 * The second operand is the Expression field of the original debug record.
 * Any number of operands, from the 3rd onwards, record a sequence of variable
   location operands, which may take any of the same values as the first
   operand of the ``DBG_VALUE`` instruction above. These variable location
   operands are inserted into the final DWARF Expression in positions indicated
   by the DW_OP_LLVM_arg operator in the `DIExpression
   <LangRef.html#diexpression>`_.

The position at which the DBG_VALUEs are inserted should correspond to the
positions of their matching ``#dbg_value`` records in the IR block.  As
with optimization, LLVM aims to preserve the order in which variable
assignments occurred in the source program. However SelectionDAG performs some
instruction scheduling, which can reorder assignments (discussed below).
Function parameter locations are moved to the beginning of the function if
they're not already, to ensure they're immediately available on function entry.

To demonstrate variable locations during instruction selection, consider
the following example:

.. code-block:: llvm

  define i32 @foo(i32* %addr) {
  entry:
      #dbg_value(i32 0, !3, !DIExpression(), !5)
    br label %bb1, !dbg !5

  bb1:                                              ; preds = %bb1, %entry
    %bar.0 = phi i32 [ 0, %entry ], [ %add, %bb1 ]
      #dbg_value(i32 %bar.0, !3, !DIExpression(), !5)
    %addr1 = getelementptr i32, i32 *%addr, i32 1, !dbg !5
      #dbg_value(i32 *%addr1, !3, !DIExpression(), !5)
    %loaded1 = load i32, i32* %addr1, !dbg !5
    %addr2 = getelementptr i32, i32 *%addr, i32 %bar.0, !dbg !5
      #dbg_value(i32 *%addr2, !3, !DIExpression(), !5)
    %loaded2 = load i32, i32* %addr2, !dbg !5
    %add = add i32 %bar.0, 1, !dbg !5
      #dbg_value(i32 %add, !3, !DIExpression(), !5)
    %added = add i32 %loaded1, %loaded2
    %cond = icmp ult i32 %added, %bar.0, !dbg !5
    br i1 %cond, label %bb1, label %bb2, !dbg !5

  bb2:                                              ; preds = %bb1
    ret i32 0, !dbg !5
  }

If one compiles this IR with ``llc -o - -start-after=codegen-prepare -stop-after=expand-isel-pseudos -mtriple=x86_64--``, the following MIR is produced:

.. code-block:: text

  bb.0.entry:
    successors: %bb.1(0x80000000)
    liveins: $rdi

    %2:gr64 = COPY $rdi
    %3:gr32 = MOV32r0 implicit-def dead $eflags
    DBG_VALUE 0, $noreg, !3, !DIExpression(), debug-location !5

  bb.1.bb1:
    successors: %bb.1(0x7c000000), %bb.2(0x04000000)

    %0:gr32 = PHI %3, %bb.0, %1, %bb.1
    DBG_VALUE %0, $noreg, !3, !DIExpression(), debug-location !5
    DBG_VALUE %2, $noreg, !3, !DIExpression(DW_OP_plus_uconst, 4, DW_OP_stack_value), debug-location !5
    %4:gr32 = MOV32rm %2, 1, $noreg, 4, $noreg, debug-location !5 :: (load 4 from %ir.addr1)
    %5:gr64_nosp = MOVSX64rr32 %0, debug-location !5
    DBG_VALUE $noreg, $noreg, !3, !DIExpression(), debug-location !5
    %1:gr32 = INC32r %0, implicit-def dead $eflags, debug-location !5
    DBG_VALUE %1, $noreg, !3, !DIExpression(), debug-location !5
    %6:gr32 = ADD32rm %4, %2, 4, killed %5, 0, $noreg, implicit-def dead $eflags :: (load 4 from %ir.addr2)
    %7:gr32 = SUB32rr %6, %0, implicit-def $eflags, debug-location !5
    JB_1 %bb.1, implicit $eflags, debug-location !5
    JMP_1 %bb.2, debug-location !5

  bb.2.bb2:
    %8:gr32 = MOV32r0 implicit-def dead $eflags
    $eax = COPY %8, debug-location !5
    RET 0, $eax, debug-location !5

Observe first that there is a DBG_VALUE instruction for every ``#dbg_value``
record in the source IR, ensuring no source level assignments go missing.
Then consider the different ways in which variable locations have been recorded:

* For the first #dbg_value an immediate operand is used to record a zero value.
* The #dbg_value of the PHI instruction leads to a DBG_VALUE of virtual register
  ``%0``.
* The first GEP has its effect folded into the first load instruction
  (as a 4-byte offset), but the variable location is salvaged by folding
  the GEPs effect into the DIExpression.
* The second GEP is also folded into the corresponding load. However, it is
  insufficiently simple to be salvaged, and is emitted as a ``$noreg``
  DBG_VALUE, indicating that the variable takes on an undefined location.
* The final #dbg_value has its Value placed in virtual register ``%1``.

Instruction Scheduling
----------------------

A number of passes can reschedule instructions, notably instruction selection
and the pre-and-post RA machine schedulers. Instruction scheduling can
significantly change the nature of the program -- in the (very unlikely) worst
case the instruction sequence could be completely reversed. In such
circumstances LLVM follows the principle applied to optimizations, that it is
better for the debugger not to display any state than a misleading state.
Thus, whenever instructions are advanced in order of execution, any
corresponding DBG_VALUE is kept in its original position, and if an instruction
is delayed then the variable is given an undefined location for the duration
of the delay. To illustrate, consider this pseudo-MIR:

.. code-block:: text

  %1:gr32 = MOV32rm %0, 1, $noreg, 4, $noreg, debug-location !5 :: (load 4 from %ir.addr1)
  DBG_VALUE %1, $noreg, !1, !2
  %4:gr32 = ADD32rr %3, %2, implicit-def dead $eflags
  DBG_VALUE %4, $noreg, !3, !4
  %7:gr32 = SUB32rr %6, %5, implicit-def dead $eflags
  DBG_VALUE %7, $noreg, !5, !6

Imagine that the SUB32rr were moved forward to give us the following MIR:

.. code-block:: text

  %7:gr32 = SUB32rr %6, %5, implicit-def dead $eflags
  %1:gr32 = MOV32rm %0, 1, $noreg, 4, $noreg, debug-location !5 :: (load 4 from %ir.addr1)
  DBG_VALUE %1, $noreg, !1, !2
  %4:gr32 = ADD32rr %3, %2, implicit-def dead $eflags
  DBG_VALUE %4, $noreg, !3, !4
  DBG_VALUE %7, $noreg, !5, !6

In this circumstance LLVM would leave the MIR as shown above. Were we to move
the DBG_VALUE of virtual register %7 upwards with the SUB32rr, we would re-order
assignments and introduce a new state of the program. Whereas with the solution
above, the debugger will see one fewer combination of variable values, because
``!3`` and ``!5`` will change value at the same time. This is preferred over
misrepresenting the original program.

In comparison, if one sunk the MOV32rm, LLVM would produce the following:

.. code-block:: text

  DBG_VALUE $noreg, $noreg, !1, !2
  %4:gr32 = ADD32rr %3, %2, implicit-def dead $eflags
  DBG_VALUE %4, $noreg, !3, !4
  %7:gr32 = SUB32rr %6, %5, implicit-def dead $eflags
  DBG_VALUE %7, $noreg, !5, !6
  %1:gr32 = MOV32rm %0, 1, $noreg, 4, $noreg, debug-location !5 :: (load 4 from %ir.addr1)
  DBG_VALUE %1, $noreg, !1, !2

Here, to avoid presenting a state in which the first assignment to ``!1``
disappears, the DBG_VALUE at the top of the block assigns the variable the
undefined location, until its value is available at the end of the block where
an additional DBG_VALUE is added. Were any other DBG_VALUE for ``!1`` to occur
in the instructions that the MOV32rm was sunk past, the DBG_VALUE for ``%1``
would be dropped and the debugger would never observe it in the variable. This
accurately reflects that the value is not available during the corresponding
portion of the original program.

Variable locations during Register Allocation
---------------------------------------------

To avoid debug instructions interfering with the register allocator, the
LiveDebugVariables pass extracts variable locations from a MIR function and
deletes the corresponding DBG_VALUE instructions. Some localized copy
propagation is performed within blocks. After register allocation, the
VirtRegRewriter pass re-inserts DBG_VALUE instructions in their original
positions, translating virtual register references into their physical
machine locations. To avoid encoding incorrect variable locations, in this
pass any DBG_VALUE of a virtual register that is not live, is replaced by
the undefined location. The LiveDebugVariables may insert redundant DBG_VALUEs
because of virtual register rewriting. These will be subsequently removed by
the RemoveRedundantDebugValues pass.

LiveDebugValues expansion of variable locations
-----------------------------------------------

After all optimizations have run and shortly before emission, the
LiveDebugValues pass runs to achieve two aims:

* To propagate the location of variables through copies and register spills,
* For every block, to record every valid variable location in that block.

After this pass the DBG_VALUE instruction changes meaning: rather than
corresponding to a source-level assignment where the variable may change value,
it asserts the location of a variable in a block, and loses effect outside the
block. Propagating variable locations through copies and spills is
straightforwards: determining the variable location in every basic block
requires the consideration of control flow. Consider the following IR, which
presents several difficulties:

.. code-block:: text

  define dso_local i32 @foo(i1 %cond, i32 %input) !dbg !12 {
  entry:
    br i1 %cond, label %truebr, label %falsebr

  bb1:
    %value = phi i32 [ %value1, %truebr ], [ %value2, %falsebr ]
    br label %exit, !dbg !26

  truebr:
      #dbg_value(i32 %input, !30, !DIExpression(), !24)
      #dbg_value(i32 1, !23, !DIExpression(), !24)
    %value1 = add i32 %input, 1
    br label %bb1

  falsebr:
      #dbg_value(i32 %input, !30, !DIExpression(), !24)
      #dbg_value(i32 2, !23, !DIExpression(), !24)
    %value2 = add i32 %input, 2
    br label %bb1

  exit:
    ret i32 %value, !dbg !30
  }

Here the difficulties are:

* The control flow is roughly the opposite of basic block order
* The value of the ``!23`` variable merges into ``%bb1``, but there is no PHI
  node

As mentioned above, the ``#dbg_value`` records essentially form an
imperative program embedded in the IR, with each record defining a variable
location. This *could* be converted to an SSA form by mem2reg, in the same way
that it uses use-def chains to identify control flow merges and insert phi
nodes for IR Values. However, because debug variable locations are defined for
every machine instruction, in effect every IR instruction uses every variable
location, which would lead to a large number of debugging records being
generated.

Examining the example above, variable ``!30`` is assigned ``%input`` on both
conditional paths through the function, while ``!23`` is assigned differing
constant values on either path. Where control flow merges in ``%bb1`` we would
want ``!30`` to keep its location (``%input``), but ``!23`` to become undefined
as we cannot determine at runtime what value it should have in %bb1 without
inserting a PHI node. mem2reg does not insert the PHI node to avoid changing
codegen when debugging is enabled, and does not insert the other #dbg_values
to avoid adding very large numbers of records.

Instead, LiveDebugValues determines variable locations when control
flow merges. A dataflow analysis is used to propagate locations between blocks:
when control flow merges, if a variable has the same location in all
predecessors then that location is propagated into the successor. If the
predecessor locations disagree, the location becomes undefined.

Once LiveDebugValues has run, every block should have all valid variable
locations described by DBG_VALUE instructions within the block. Very little
effort is then required by supporting classes (such as
DbgEntityHistoryCalculator) to build a map of each instruction to every
valid variable location, without the need to consider control flow. From
the example above, it is otherwise difficult to determine that the location
of variable ``!30`` should flow "up" into block ``%bb1``, but that the location
of variable ``!23`` should not flow "down" into the ``%exit`` block.

.. _ccxx_frontend:

C/C++ front-end specific debug information
==========================================

The C and C++ front-ends represent information about the program in a
format that is effectively identical to `DWARF <http://www.dwarfstd.org/>`_
in terms of information content.  This allows code generators to
trivially support native debuggers by generating standard dwarf
information, and contains enough information for non-dwarf targets to
translate it as needed.

This section describes the forms used to represent C and C++ programs.  Other
languages could pattern themselves after this (which itself is tuned to
representing programs in the same way that DWARF does), or they could choose
to provide completely different forms if they don't fit into the DWARF model.
As support for debugging information gets added to the various LLVM
source-language front-ends, the information used should be documented here.

The following sections provide examples of a few C/C++ constructs and
the debug information that would best describe those constructs.  The
canonical references are the ``DINode`` classes defined in
``include/llvm/IR/DebugInfoMetadata.h`` and the implementations of the
helper functions in ``lib/IR/DIBuilder.cpp``.

C/C++ source file information
-----------------------------

``llvm::Instruction`` provides easy access to metadata attached with an
instruction.  One can extract line number information encoded in LLVM IR using
``Instruction::getDebugLoc()`` and ``DILocation::getLine()``.

.. code-block:: c++

  if (DILocation *Loc = I->getDebugLoc()) { // Here I is an LLVM instruction
    unsigned Line = Loc->getLine();
    StringRef File = Loc->getFilename();
    StringRef Dir = Loc->getDirectory();
    bool ImplicitCode = Loc->isImplicitCode();
  }

When the flag ImplicitCode is true then it means that the Instruction has been
added by the front-end but doesn't correspond to source code written by the user. For example

.. code-block:: c++

  if (MyBoolean) {
    MyObject MO;
    ...
  }

At the end of the scope the MyObject's destructor is called but it isn't written
explicitly. This information is useful to avoid to have counters on brackets when
making code coverage.

C/C++ global variable information
---------------------------------

Given an integer global variable declared as follows:

.. code-block:: c

  _Alignas(8) int MyGlobal = 100;

a C/C++ front-end would generate the following descriptors:

.. code-block:: text

  ;;
  ;; Define the global itself.
  ;;
  @MyGlobal = global i32 100, align 8, !dbg !0

  ;;
  ;; List of debug info of globals
  ;;
  !llvm.dbg.cu = !{!1}

  ;; Some unrelated metadata.
  !llvm.module.flags = !{!6, !7}
  !llvm.ident = !{!8}

  ;; Define the global variable itself
  !0 = distinct !DIGlobalVariable(name: "MyGlobal", scope: !1, file: !2, line: 1, type: !5, isLocal: false, isDefinition: true, align: 64)

  ;; Define the compile unit.
  !1 = distinct !DICompileUnit(language: DW_LANG_C99, file: !2,
                               producer: "clang version 4.0.0",
                               isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug,
                               enums: !3, globals: !4)

  ;;
  ;; Define the file
  ;;
  !2 = !DIFile(filename: "/dev/stdin",
               directory: "/Users/dexonsmith/data/llvm/debug-info")

  ;; An empty array.
  !3 = !{}

  ;; The Array of Global Variables
  !4 = !{!0}

  ;;
  ;; Define the type
  ;;
  !5 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)

  ;; Dwarf version to output.
  !6 = !{i32 2, !"Dwarf Version", i32 4}

  ;; Debug info schema version.
  !7 = !{i32 2, !"Debug Info Version", i32 3}

  ;; Compiler identification
  !8 = !{!"clang version 4.0.0"}


The align value in DIGlobalVariable description specifies variable alignment in
case it was forced by C11 _Alignas(), C++11 alignas() keywords or compiler
attribute __attribute__((aligned ())). In other case (when this field is missing)
alignment is considered default. This is used when producing DWARF output
for DW_AT_alignment value.

C/C++ function information
--------------------------

Given a function declared as follows:

.. code-block:: c

  int main(int argc, char *argv[]) {
    return 0;
  }

a C/C++ front-end would generate the following descriptors:

.. code-block:: text

  ;;
  ;; Define the anchor for subprograms.
  ;;
  !4 = !DISubprogram(name: "main", scope: !1, file: !1, line: 1, type: !5,
                     isLocal: false, isDefinition: true, scopeLine: 1,
                     flags: DIFlagPrototyped, isOptimized: false,
                     retainedNodes: !2)

  ;;
  ;; Define the subprogram itself.
  ;;
  define i32 @main(i32 %argc, i8** %argv) !dbg !4 {
  ...
  }

C++ specific debug information
==============================

C++ special member functions information
----------------------------------------

DWARF v5 introduces attributes defined to enhance debugging information of C++ programs. LLVM can generate (or omit) these appropriate DWARF attributes. In C++ a special member function Ctors, Dtors, Copy/Move Ctors, assignment operators can be declared with C++11 keyword deleted. This is represented in LLVM using spFlags value DISPFlagDeleted.

Given a class declaration with copy constructor declared as deleted:

.. code-block:: c

  class foo {
   public:
     foo(const foo&) = deleted;
  };

A C++ frontend would generate following:

.. code-block:: text

  !17 = !DISubprogram(name: "foo", scope: !11, file: !1, line: 5, type: !18, scopeLine: 5, flags: DIFlagPublic | DIFlagPrototyped, spFlags: DISPFlagDeleted)

and this will produce an additional DWARF attribute as:

.. code-block:: text

  DW_TAG_subprogram [7] *
    DW_AT_name [DW_FORM_strx1]    (indexed (00000006) string = "foo")
    DW_AT_decl_line [DW_FORM_data1]       (5)
    ...
    DW_AT_deleted [DW_FORM_flag_present]  (true)

Fortran specific debug information
==================================

Fortran function information
----------------------------

There are a few DWARF attributes defined to support client debugging of Fortran programs.  LLVM can generate (or omit) the appropriate DWARF attributes for the prefix-specs of ELEMENTAL, PURE, IMPURE, RECURSIVE, and NON_RECURSIVE.  This is done by using the spFlags values: DISPFlagElemental, DISPFlagPure, and DISPFlagRecursive.

.. code-block:: fortran

  elemental function elem_func(a)

a Fortran front-end would generate the following descriptors:

.. code-block:: text

  !11 = distinct !DISubprogram(name: "subroutine2", scope: !1, file: !1,
          line: 5, type: !8, scopeLine: 6,
          spFlags: DISPFlagDefinition | DISPFlagElemental, unit: !0,
          retainedNodes: !2)

and this will materialize an additional DWARF attribute as:

.. code-block:: text

  DW_TAG_subprogram [3]
     DW_AT_low_pc [DW_FORM_addr]     (0x0000000000000010 ".text")
     DW_AT_high_pc [DW_FORM_data4]   (0x00000001)
     ...
     DW_AT_elemental [DW_FORM_flag_present]  (true)

There are a few DWARF tags defined to represent Fortran specific constructs i.e DW_TAG_string_type for representing Fortran character(n). In LLVM this is represented as DIStringType.

.. code-block:: fortran

  character(len=*), intent(in) :: string

a Fortran front-end would generate the following descriptors:

.. code-block:: text

  !DILocalVariable(name: "string", arg: 1, scope: !10, file: !3, line: 4, type: !15)
  !DIStringType(name: "character(*)!2", stringLength: !16, stringLengthExpression: !DIExpression(), size: 32)

A fortran deferred-length character can also contain the information of raw storage of the characters in addition to the length of the string. This information is encoded in the  stringLocationExpression field. Based on this information, DW_AT_data_location attribute is emitted in a DW_TAG_string_type debug info.

  !DIStringType(name: "character(*)!2", stringLengthExpression: !DIExpression(), stringLocationExpression: !DIExpression(DW_OP_push_object_address, DW_OP_deref), size: 32)

and this will materialize in DWARF tags as:

.. code-block:: text

   DW_TAG_string_type
                DW_AT_name      ("character(*)!2")
                DW_AT_string_length     (0x00000064)
   0x00000064:    DW_TAG_variable
                  DW_AT_location      (DW_OP_fbreg +16)
                  DW_AT_type  (0x00000083 "integer*8")
                  DW_AT_data_location (DW_OP_push_object_address, DW_OP_deref)
                  ...
                  DW_AT_artificial    (true)

A Fortran front-end may need to generate a *trampoline* function to call a
function defined in a different compilation unit. In this case, the front-end
can emit the following descriptor for the trampoline function:

.. code-block:: text

  !DISubprogram(name: "sub1_.t0p", linkageName: "sub1_.t0p", scope: !4, file: !4, type: !5, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !7, retainedNodes: !24, targetFuncName: "sub1_")

The targetFuncName field is the name of the function that the trampoline
calls. This descriptor results in the following DWARF tag:

.. code-block:: text

  DW_TAG_subprogram
    ...
    DW_AT_linkage_name	("sub1_.t0p")
    DW_AT_name	("sub1_.t0p")
    DW_AT_trampoline	("sub1_")

Debugging information format
============================

Debugging Information Extension for Objective C Properties
----------------------------------------------------------

Introduction
^^^^^^^^^^^^

Objective C provides a simpler way to declare and define accessor methods using
declared properties.  The language provides features to declare a property and
to let compiler synthesize accessor methods.

The debugger lets developer inspect Objective C interfaces and their instance
variables and class variables.  However, the debugger does not know anything
about the properties defined in Objective C interfaces.  The debugger consumes
information generated by compiler in DWARF format.  The format does not support
encoding of Objective C properties.  This proposal describes DWARF extensions to
encode Objective C properties, which the debugger can use to let developers
inspect Objective C properties.

Proposal
^^^^^^^^

Objective C properties exist separately from class members.  A property can be
defined only by "setter" and "getter" selectors, and be calculated anew on each
access.  Or a property can just be a direct access to some declared ivar.
Finally it can have an ivar "automatically synthesized" for it by the compiler,
in which case the property can be referred to in user code directly using the
standard C dereference syntax as well as through the property "dot" syntax, but
there is no entry in the ``@interface`` declaration corresponding to this ivar.

To facilitate debugging, these properties we will add a new DWARF TAG into the
``DW_TAG_structure_type`` definition for the class to hold the description of a
given property, and a set of DWARF attributes that provide said description.
The property tag will also contain the name and declared type of the property.

If there is a related ivar, there will also be a DWARF property attribute placed
in the ``DW_TAG_member`` DIE for that ivar referring back to the property TAG
for that property.  And in the case where the compiler synthesizes the ivar
directly, the compiler is expected to generate a ``DW_TAG_member`` for that
ivar (with the ``DW_AT_artificial`` set to 1), whose name will be the name used
to access this ivar directly in code, and with the property attribute pointing
back to the property it is backing.

The following examples will serve as illustration for our discussion:

.. code-block:: objc

  @interface I1 {
    int n2;
  }

  @property int p1;
  @property int p2;
  @end

  @implementation I1
  @synthesize p1;
  @synthesize p2 = n2;
  @end

This produces the following DWARF (this is a "pseudo dwarfdump" output):

.. code-block:: none

  0x00000100:  TAG_structure_type [7] *
                 AT_APPLE_runtime_class( 0x10 )
                 AT_name( "I1" )
                 AT_decl_file( "Objc_Property.m" )
                 AT_decl_line( 3 )

  0x00000110    TAG_APPLE_property
                  AT_name ( "p1" )
                  AT_type ( {0x00000150} ( int ) )

  0x00000120:   TAG_APPLE_property
                  AT_name ( "p2" )
                  AT_type ( {0x00000150} ( int ) )

  0x00000130:   TAG_member [8]
                  AT_name( "_p1" )
                  AT_APPLE_property ( {0x00000110} "p1" )
                  AT_type( {0x00000150} ( int ) )
                  AT_artificial ( 0x1 )

  0x00000140:    TAG_member [8]
                   AT_name( "n2" )
                   AT_APPLE_property ( {0x00000120} "p2" )
                   AT_type( {0x00000150} ( int ) )

  0x00000150:  AT_type( ( int ) )

Note, the current convention is that the name of the ivar for an
auto-synthesized property is the name of the property from which it derives
with an underscore prepended, as is shown in the example.  But we actually
don't need to know this convention, since we are given the name of the ivar
directly.

Also, it is common practice in ObjC to have different property declarations in
the @interface and @implementation - e.g. to provide a read-only property in
the interface, and a read-write interface in the implementation.  In that case,
the compiler should emit whichever property declaration will be in force in the
current translation unit.

Developers can decorate a property with attributes which are encoded using
``DW_AT_APPLE_property_attribute``.

.. code-block:: objc

  @property (readonly, nonatomic) int pr;

.. code-block:: none

  TAG_APPLE_property [8]
    AT_name( "pr" )
    AT_type ( {0x00000147} (int) )
    AT_APPLE_property_attribute (DW_APPLE_PROPERTY_readonly, DW_APPLE_PROPERTY_nonatomic)

The setter and getter method names are attached to the property using
``DW_AT_APPLE_property_setter`` and ``DW_AT_APPLE_property_getter`` attributes.

.. code-block:: objc

  @interface I1
  @property (setter=myOwnP3Setter:) int p3;
  -(void)myOwnP3Setter:(int)a;
  @end

  @implementation I1
  @synthesize p3;
  -(void)myOwnP3Setter:(int)a{ }
  @end

The DWARF for this would be:

.. code-block:: none

  0x000003bd: TAG_structure_type [7] *
                AT_APPLE_runtime_class( 0x10 )
                AT_name( "I1" )
                AT_decl_file( "Objc_Property.m" )
                AT_decl_line( 3 )

  0x000003cd      TAG_APPLE_property
                    AT_name ( "p3" )
                    AT_APPLE_property_setter ( "myOwnP3Setter:" )
                    AT_type( {0x00000147} ( int ) )

  0x000003f3:     TAG_member [8]
                    AT_name( "_p3" )
                    AT_type ( {0x00000147} ( int ) )
                    AT_APPLE_property ( {0x000003cd} )
                    AT_artificial ( 0x1 )

New DWARF Tags
^^^^^^^^^^^^^^

+-----------------------+--------+
| TAG                   | Value  |
+=======================+========+
| DW_TAG_APPLE_property | 0x4200 |
+-----------------------+--------+

New DWARF Attributes
^^^^^^^^^^^^^^^^^^^^

+--------------------------------+--------+-----------+
| Attribute                      | Value  | Classes   |
+================================+========+===========+
| DW_AT_APPLE_property           | 0x3fed | Reference |
+--------------------------------+--------+-----------+
| DW_AT_APPLE_property_getter    | 0x3fe9 | String    |
+--------------------------------+--------+-----------+
| DW_AT_APPLE_property_setter    | 0x3fea | String    |
+--------------------------------+--------+-----------+
| DW_AT_APPLE_property_attribute | 0x3feb | Constant  |
+--------------------------------+--------+-----------+

New DWARF Constants
^^^^^^^^^^^^^^^^^^^

+--------------------------------------+-------+
| Name                                 | Value |
+======================================+=======+
| DW_APPLE_PROPERTY_readonly           | 0x01  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_getter             | 0x02  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_assign             | 0x04  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_readwrite          | 0x08  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_retain             | 0x10  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_copy               | 0x20  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_nonatomic          | 0x40  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_setter             | 0x80  |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_atomic             | 0x100 |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_weak               | 0x200 |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_strong             | 0x400 |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_unsafe_unretained  | 0x800 |
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_nullability        | 0x1000|
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_null_resettable    | 0x2000|
+--------------------------------------+-------+
| DW_APPLE_PROPERTY_class              | 0x4000|
+--------------------------------------+-------+

Name Accelerator Tables
-----------------------

Introduction
^^^^^^^^^^^^

The "``.debug_pubnames``" and "``.debug_pubtypes``" formats are not what a
debugger needs.  The "``pub``" in the section name indicates that the entries
in the table are publicly visible names only.  This means no static or hidden
functions show up in the "``.debug_pubnames``".  No static variables or private
class variables are in the "``.debug_pubtypes``".  Many compilers add different
things to these tables, so we can't rely upon the contents between gcc, icc, or
clang.

The typical query given by users tends not to match up with the contents of
these tables.  For example, the DWARF spec states that "In the case of the name
of a function member or static data member of a C++ structure, class or union,
the name presented in the "``.debug_pubnames``" section is not the simple name
given by the ``DW_AT_name attribute`` of the referenced debugging information
entry, but rather the fully qualified name of the data or function member."
So the only names in these tables for complex C++ entries is a fully
qualified name.  Debugger users tend not to enter their search strings as
"``a::b::c(int,const Foo&) const``", but rather as "``c``", "``b::c``" , or
"``a::b::c``".  So the name entered in the name table must be demangled in
order to chop it up appropriately and additional names must be manually entered
into the table to make it effective as a name lookup table for debuggers to
use.

All debuggers currently ignore the "``.debug_pubnames``" table as a result of
its inconsistent and useless public-only name content making it a waste of
space in the object file.  These tables, when they are written to disk, are not
sorted in any way, leaving every debugger to do its own parsing and sorting.
These tables also include an inlined copy of the string values in the table
itself making the tables much larger than they need to be on disk, especially
for large C++ programs.

Can't we just fix the sections by adding all of the names we need to this
table? No, because that is not what the tables are defined to contain and we
won't know the difference between the old bad tables and the new good tables.
At best we could make our own renamed sections that contain all of the data we
need.

These tables are also insufficient for what a debugger like LLDB needs.  LLDB
uses clang for its expression parsing where LLDB acts as a PCH.  LLDB is then
often asked to look for type "``foo``" or namespace "``bar``", or list items in
namespace "``baz``".  Namespaces are not included in the pubnames or pubtypes
tables.  Since clang asks a lot of questions when it is parsing an expression,
we need to be very fast when looking up names, as it happens a lot.  Having new
accelerator tables that are optimized for very quick lookups will benefit this
type of debugging experience greatly.

We would like to generate name lookup tables that can be mapped into memory
from disk, and used as is, with little or no up-front parsing.  We would also
be able to control the exact content of these different tables so they contain
exactly what we need.  The Name Accelerator Tables were designed to fix these
issues.  In order to solve these issues we need to:

* Have a format that can be mapped into memory from disk and used as is
* Lookups should be very fast
* Extensible table format so these tables can be made by many producers
* Contain all of the names needed for typical lookups out of the box
* Strict rules for the contents of tables

Table size is important and the accelerator table format should allow the reuse
of strings from common string tables so the strings for the names are not
duplicated.  We also want to make sure the table is ready to be used as-is by
simply mapping the table into memory with minimal header parsing.

The name lookups need to be fast and optimized for the kinds of lookups that
debuggers tend to do.  Optimally we would like to touch as few parts of the
mapped table as possible when doing a name lookup and be able to quickly find
the name entry we are looking for, or discover there are no matches.  In the
case of debuggers we optimized for lookups that fail most of the time.

Each table that is defined should have strict rules on exactly what is in the
accelerator tables and documented so clients can rely on the content.

Hash Tables
^^^^^^^^^^^

Standard Hash Tables
""""""""""""""""""""

Typical hash tables have a header, buckets, and each bucket points to the
bucket contents:

.. code-block:: none

  .------------.
  |  HEADER    |
  |------------|
  |  BUCKETS   |
  |------------|
  |  DATA      |
  `------------'

The BUCKETS are an array of offsets to DATA for each hash:

.. code-block:: none

  .------------.
  | 0x00001000 | BUCKETS[0]
  | 0x00002000 | BUCKETS[1]
  | 0x00002200 | BUCKETS[2]
  | 0x000034f0 | BUCKETS[3]
  |            | ...
  | 0xXXXXXXXX | BUCKETS[n_buckets]
  '------------'

So for ``bucket[3]`` in the example above, we have an offset into the table
0x000034f0 which points to a chain of entries for the bucket.  Each bucket must
contain a next pointer, full 32 bit hash value, the string itself, and the data
for the current string value.

.. code-block:: none

              .------------.
  0x000034f0: | 0x00003500 | next pointer
              | 0x12345678 | 32 bit hash
              | "erase"    | string value
              | data[n]    | HashData for this bucket
              |------------|
  0x00003500: | 0x00003550 | next pointer
              | 0x29273623 | 32 bit hash
              | "dump"     | string value
              | data[n]    | HashData for this bucket
              |------------|
  0x00003550: | 0x00000000 | next pointer
              | 0x82638293 | 32 bit hash
              | "main"     | string value
              | data[n]    | HashData for this bucket
              `------------'

The problem with this layout for debuggers is that we need to optimize for the
negative lookup case where the symbol we're searching for is not present.  So
if we were to lookup "``printf``" in the table above, we would make a 32-bit
hash for "``printf``", it might match ``bucket[3]``.  We would need to go to
the offset 0x000034f0 and start looking to see if our 32 bit hash matches.  To
do so, we need to read the next pointer, then read the hash, compare it, and
skip to the next bucket.  Each time we are skipping many bytes in memory and
touching new pages just to do the compare on the full 32 bit hash.  All of
these accesses then tell us that we didn't have a match.

Name Hash Tables
""""""""""""""""

To solve the issues mentioned above we have structured the hash tables a bit
differently: a header, buckets, an array of all unique 32 bit hash values,
followed by an array of hash value data offsets, one for each hash value, then
the data for all hash values:

.. code-block:: none

  .-------------.
  |  HEADER     |
  |-------------|
  |  BUCKETS    |
  |-------------|
  |  HASHES     |
  |-------------|
  |  OFFSETS    |
  |-------------|
  |  DATA       |
  `-------------'

The ``BUCKETS`` in the name tables are an index into the ``HASHES`` array.  By
making all of the full 32 bit hash values contiguous in memory, we allow
ourselves to efficiently check for a match while touching as little memory as
possible.  Most often checking the 32 bit hash values is as far as the lookup
goes.  If it does match, it usually is a match with no collisions.  So for a
table with "``n_buckets``" buckets, and "``n_hashes``" unique 32 bit hash
values, we can clarify the contents of the ``BUCKETS``, ``HASHES`` and
``OFFSETS`` as:

.. code-block:: none

  .-------------------------.
  |  HEADER.magic           | uint32_t
  |  HEADER.version         | uint16_t
  |  HEADER.hash_function   | uint16_t
  |  HEADER.bucket_count    | uint32_t
  |  HEADER.hashes_count    | uint32_t
  |  HEADER.header_data_len | uint32_t
  |  HEADER_DATA            | HeaderData
  |-------------------------|
  |  BUCKETS                | uint32_t[n_buckets] // 32 bit hash indexes
  |-------------------------|
  |  HASHES                 | uint32_t[n_hashes] // 32 bit hash values
  |-------------------------|
  |  OFFSETS                | uint32_t[n_hashes] // 32 bit offsets to hash value data
  |-------------------------|
  |  ALL HASH DATA          |
  `-------------------------'

So taking the exact same data from the standard hash example above we end up
with:

.. code-block:: none

              .------------.
              | HEADER     |
              |------------|
              |          0 | BUCKETS[0]
              |          2 | BUCKETS[1]
              |          5 | BUCKETS[2]
              |          6 | BUCKETS[3]
              |            | ...
              |        ... | BUCKETS[n_buckets]
              |------------|
              | 0x........ | HASHES[0]
              | 0x........ | HASHES[1]
              | 0x........ | HASHES[2]
              | 0x........ | HASHES[3]
              | 0x........ | HASHES[4]
              | 0x........ | HASHES[5]
              | 0x12345678 | HASHES[6]    hash for BUCKETS[3]
              | 0x29273623 | HASHES[7]    hash for BUCKETS[3]
              | 0x82638293 | HASHES[8]    hash for BUCKETS[3]
              | 0x........ | HASHES[9]
              | 0x........ | HASHES[10]
              | 0x........ | HASHES[11]
              | 0x........ | HASHES[12]
              | 0x........ | HASHES[13]
              | 0x........ | HASHES[n_hashes]
              |------------|
              | 0x........ | OFFSETS[0]
              | 0x........ | OFFSETS[1]
              | 0x........ | OFFSETS[2]
              | 0x........ | OFFSETS[3]
              | 0x........ | OFFSETS[4]
              | 0x........ | OFFSETS[5]
              | 0x000034f0 | OFFSETS[6]   offset for BUCKETS[3]
              | 0x00003500 | OFFSETS[7]   offset for BUCKETS[3]
              | 0x00003550 | OFFSETS[8]   offset for BUCKETS[3]
              | 0x........ | OFFSETS[9]
              | 0x........ | OFFSETS[10]
              | 0x........ | OFFSETS[11]
              | 0x........ | OFFSETS[12]
              | 0x........ | OFFSETS[13]
              | 0x........ | OFFSETS[n_hashes]
              |------------|
              |            |
              |            |
              |            |
              |            |
              |            |
              |------------|
  0x000034f0: | 0x00001203 | .debug_str ("erase")
              | 0x00000004 | A 32 bit array count - number of HashData with name "erase"
              | 0x........ | HashData[0]
              | 0x........ | HashData[1]
              | 0x........ | HashData[2]
              | 0x........ | HashData[3]
              | 0x00000000 | String offset into .debug_str (terminate data for hash)
              |------------|
  0x00003500: | 0x00001203 | String offset into .debug_str ("collision")
              | 0x00000002 | A 32 bit array count - number of HashData with name "collision"
              | 0x........ | HashData[0]
              | 0x........ | HashData[1]
              | 0x00001203 | String offset into .debug_str ("dump")
              | 0x00000003 | A 32 bit array count - number of HashData with name "dump"
              | 0x........ | HashData[0]
              | 0x........ | HashData[1]
              | 0x........ | HashData[2]
              | 0x00000000 | String offset into .debug_str (terminate data for hash)
              |------------|
  0x00003550: | 0x00001203 | String offset into .debug_str ("main")
              | 0x00000009 | A 32 bit array count - number of HashData with name "main"
              | 0x........ | HashData[0]
              | 0x........ | HashData[1]
              | 0x........ | HashData[2]
              | 0x........ | HashData[3]
              | 0x........ | HashData[4]
              | 0x........ | HashData[5]
              | 0x........ | HashData[6]
              | 0x........ | HashData[7]
              | 0x........ | HashData[8]
              | 0x00000000 | String offset into .debug_str (terminate data for hash)
              `------------'

So we still have all of the same data, we just organize it more efficiently for
debugger lookup.  If we repeat the same "``printf``" lookup from above, we
would hash "``printf``" and find it matches ``BUCKETS[3]`` by taking the 32 bit
hash value and modulo it by ``n_buckets``.  ``BUCKETS[3]`` contains "6" which
is the index into the ``HASHES`` table.  We would then compare any consecutive
32 bit hashes values in the ``HASHES`` array as long as the hashes would be in
``BUCKETS[3]``.  We do this by verifying that each subsequent hash value modulo
``n_buckets`` is still 3.  In the case of a failed lookup we would access the
memory for ``BUCKETS[3]``, and then compare a few consecutive 32 bit hashes
before we know that we have no match.  We don't end up marching through
multiple words of memory and we really keep the number of processor data cache
lines being accessed as small as possible.

The string hash that is used for these lookup tables is the Daniel J.
Bernstein hash which is also used in the ELF ``GNU_HASH`` sections.  It is a
very good hash for all kinds of names in programs with very few hash
collisions.

Empty buckets are designated by using an invalid hash index of ``UINT32_MAX``.

Details
^^^^^^^

These name hash tables are designed to be generic where specializations of the
table get to define additional data that goes into the header ("``HeaderData``"),
how the string value is stored ("``KeyType``") and the content of the data for each
hash value.

Header Layout
"""""""""""""

The header has a fixed part, and the specialized part.  The exact format of the
header is:

.. code-block:: c

  struct Header
  {
    uint32_t   magic;           // 'HASH' magic value to allow endian detection
    uint16_t   version;         // Version number
    uint16_t   hash_function;   // The hash function enumeration that was used
    uint32_t   bucket_count;    // The number of buckets in this hash table
    uint32_t   hashes_count;    // The total number of unique hash values and hash data offsets in this table
    uint32_t   header_data_len; // The bytes to skip to get to the hash indexes (buckets) for correct alignment
                                // Specifically the length of the following HeaderData field - this does not
                                // include the size of the preceding fields
    HeaderData header_data;     // Implementation specific header data
  };

The header starts with a 32 bit "``magic``" value which must be ``'HASH'``
encoded as an ASCII integer.  This allows the detection of the start of the
hash table and also allows the table's byte order to be determined so the table
can be correctly extracted.  The "``magic``" value is followed by a 16 bit
``version`` number which allows the table to be revised and modified in the
future.  The current version number is 1. ``hash_function`` is a ``uint16_t``
enumeration that specifies which hash function was used to produce this table.
The current values for the hash function enumerations include:

.. code-block:: c

  enum HashFunctionType
  {
    eHashFunctionDJB = 0u, // Daniel J Bernstein hash function
  };

``bucket_count`` is a 32 bit unsigned integer that represents how many buckets
are in the ``BUCKETS`` array.  ``hashes_count`` is the number of unique 32 bit
hash values that are in the ``HASHES`` array, and is the same number of offsets
are contained in the ``OFFSETS`` array.  ``header_data_len`` specifies the size
in bytes of the ``HeaderData`` that is filled in by specialized versions of
this table.

Fixed Lookup
""""""""""""

The header is followed by the buckets, hashes, offsets, and hash value data.

.. code-block:: c

  struct FixedTable
  {
    uint32_t buckets[Header.bucket_count];  // An array of hash indexes into the "hashes[]" array below
    uint32_t hashes [Header.hashes_count];  // Every unique 32 bit hash for the entire table is in this table
    uint32_t offsets[Header.hashes_count];  // An offset that corresponds to each item in the "hashes[]" array above
  };

``buckets`` is an array of 32 bit indexes into the ``hashes`` array.  The
``hashes`` array contains all of the 32 bit hash values for all names in the
hash table.  Each hash in the ``hashes`` table has an offset in the ``offsets``
array that points to the data for the hash value.

This table setup makes it very easy to repurpose these tables to contain
different data, while keeping the lookup mechanism the same for all tables.
This layout also makes it possible to save the table to disk and map it in
later and do very efficient name lookups with little or no parsing.

DWARF lookup tables can be implemented in a variety of ways and can store a lot
of information for each name.  We want to make the DWARF tables extensible and
able to store the data efficiently so we have used some of the DWARF features
that enable efficient data storage to define exactly what kind of data we store
for each name.

The ``HeaderData`` contains a definition of the contents of each HashData chunk.
We might want to store an offset to all of the debug information entries (DIEs)
for each name.  To keep things extensible, we create a list of items, or
Atoms, that are contained in the data for each name.  First comes the type of
the data in each atom:

.. code-block:: c

  enum AtomType
  {
    eAtomTypeNULL       = 0u,
    eAtomTypeDIEOffset  = 1u,   // DIE offset, check form for encoding
    eAtomTypeCUOffset   = 2u,   // DIE offset of the compiler unit header that contains the item in question
    eAtomTypeTag        = 3u,   // DW_TAG_xxx value, should be encoded as DW_FORM_data1 (if no tags exceed 255) or DW_FORM_data2
    eAtomTypeNameFlags  = 4u,   // Flags from enum NameFlags
    eAtomTypeTypeFlags  = 5u,   // Flags from enum TypeFlags
  };

The enumeration values and their meanings are:

.. code-block:: none

  eAtomTypeNULL       - a termination atom that specifies the end of the atom list
  eAtomTypeDIEOffset  - an offset into the .debug_info section for the DWARF DIE for this name
  eAtomTypeCUOffset   - an offset into the .debug_info section for the CU that contains the DIE
  eAtomTypeDIETag     - The DW_TAG_XXX enumeration value so you don't have to parse the DWARF to see what it is
  eAtomTypeNameFlags  - Flags for functions and global variables (isFunction, isInlined, isExternal...)
  eAtomTypeTypeFlags  - Flags for types (isCXXClass, isObjCClass, ...)

Then we allow each atom type to define the atom type and how the data for each
atom type data is encoded:

.. code-block:: c

  struct Atom
  {
    uint16_t type;  // AtomType enum value
    uint16_t form;  // DWARF DW_FORM_XXX defines
  };

The ``form`` type above is from the DWARF specification and defines the exact
encoding of the data for the Atom type.  See the DWARF specification for the
``DW_FORM_`` definitions.

.. code-block:: c

  struct HeaderData
  {
    uint32_t die_offset_base;
    uint32_t atom_count;
    Atoms    atoms[atom_count0];
  };

``HeaderData`` defines the base DIE offset that should be added to any atoms
that are encoded using the ``DW_FORM_ref1``, ``DW_FORM_ref2``,
``DW_FORM_ref4``, ``DW_FORM_ref8`` or ``DW_FORM_ref_udata``.  It also defines
what is contained in each ``HashData`` object -- ``Atom.form`` tells us how large
each field will be in the ``HashData`` and the ``Atom.type`` tells us how this data
should be interpreted.

For the current implementations of the "``.apple_names``" (all functions +
globals), the "``.apple_types``" (names of all types that are defined), and
the "``.apple_namespaces``" (all namespaces), we currently set the ``Atom``
array to be:

.. code-block:: c

  HeaderData.atom_count = 1;
  HeaderData.atoms[0].type = eAtomTypeDIEOffset;
  HeaderData.atoms[0].form = DW_FORM_data4;

This defines the contents to be the DIE offset (eAtomTypeDIEOffset) that is
encoded as a 32 bit value (DW_FORM_data4).  This allows a single name to have
multiple matching DIEs in a single file, which could come up with an inlined
function for instance.  Future tables could include more information about the
DIE such as flags indicating if the DIE is a function, method, block,
or inlined.

The KeyType for the DWARF table is a 32 bit string table offset into the
".debug_str" table.  The ".debug_str" is the string table for the DWARF which
may already contain copies of all of the strings.  This helps make sure, with
help from the compiler, that we reuse the strings between all of the DWARF
sections and keeps the hash table size down.  Another benefit to having the
compiler generate all strings as DW_FORM_strp in the debug info, is that
DWARF parsing can be made much faster.

After a lookup is made, we get an offset into the hash data.  The hash data
needs to be able to deal with 32 bit hash collisions, so the chunk of data
at the offset in the hash data consists of a triple:

.. code-block:: c

  uint32_t str_offset
  uint32_t hash_data_count
  HashData[hash_data_count]

If "str_offset" is zero, then the bucket contents are done. 99.9% of the
hash data chunks contain a single item (no 32 bit hash collision):

.. code-block:: none

  .------------.
  | 0x00001023 | uint32_t KeyType (.debug_str[0x0001023] => "main")
  | 0x00000004 | uint32_t HashData count
  | 0x........ | uint32_t HashData[0] DIE offset
  | 0x........ | uint32_t HashData[1] DIE offset
  | 0x........ | uint32_t HashData[2] DIE offset
  | 0x........ | uint32_t HashData[3] DIE offset
  | 0x00000000 | uint32_t KeyType (end of hash chain)
  `------------'

If there are collisions, you will have multiple valid string offsets:

.. code-block:: none

  .------------.
  | 0x00001023 | uint32_t KeyType (.debug_str[0x0001023] => "main")
  | 0x00000004 | uint32_t HashData count
  | 0x........ | uint32_t HashData[0] DIE offset
  | 0x........ | uint32_t HashData[1] DIE offset
  | 0x........ | uint32_t HashData[2] DIE offset
  | 0x........ | uint32_t HashData[3] DIE offset
  | 0x00002023 | uint32_t KeyType (.debug_str[0x0002023] => "print")
  | 0x00000002 | uint32_t HashData count
  | 0x........ | uint32_t HashData[0] DIE offset
  | 0x........ | uint32_t HashData[1] DIE offset
  | 0x00000000 | uint32_t KeyType (end of hash chain)
  `------------'

Current testing with real world C++ binaries has shown that there is around 1
32 bit hash collision per 100,000 name entries.

Contents
^^^^^^^^

As we said, we want to strictly define exactly what is included in the
different tables.  For DWARF, we have 3 tables: "``.apple_names``",
"``.apple_types``", and "``.apple_namespaces``".

"``.apple_names``" sections should contain an entry for each DWARF DIE whose
``DW_TAG`` is a ``DW_TAG_label``, ``DW_TAG_inlined_subroutine``, or
``DW_TAG_subprogram`` that has address attributes: ``DW_AT_low_pc``,
``DW_AT_high_pc``, ``DW_AT_ranges`` or ``DW_AT_entry_pc``.  It also contains
``DW_TAG_variable`` DIEs that have a ``DW_OP_addr`` in the location (global and
static variables).  All global and static variables should be included,
including those scoped within functions and classes.  For example using the
following code:

.. code-block:: c

  static int var = 0;

  void f ()
  {
    static int var = 0;
  }

Both of the static ``var`` variables would be included in the table.  All
functions should emit both their full names and their basenames.  For C or C++,
the full name is the mangled name (if available) which is usually in the
``DW_AT_MIPS_linkage_name`` attribute, and the ``DW_AT_name`` contains the
function basename.  If global or static variables have a mangled name in a
``DW_AT_MIPS_linkage_name`` attribute, this should be emitted along with the
simple name found in the ``DW_AT_name`` attribute.

"``.apple_types``" sections should contain an entry for each DWARF DIE whose
tag is one of:

* DW_TAG_array_type
* DW_TAG_class_type
* DW_TAG_enumeration_type
* DW_TAG_pointer_type
* DW_TAG_reference_type
* DW_TAG_string_type
* DW_TAG_structure_type
* DW_TAG_subroutine_type
* DW_TAG_typedef
* DW_TAG_union_type
* DW_TAG_ptr_to_member_type
* DW_TAG_set_type
* DW_TAG_subrange_type
* DW_TAG_base_type
* DW_TAG_const_type
* DW_TAG_immutable_type
* DW_TAG_file_type
* DW_TAG_namelist
* DW_TAG_packed_type
* DW_TAG_volatile_type
* DW_TAG_restrict_type
* DW_TAG_atomic_type
* DW_TAG_interface_type
* DW_TAG_unspecified_type
* DW_TAG_shared_type

Only entries with a ``DW_AT_name`` attribute are included, and the entry must
not be a forward declaration (``DW_AT_declaration`` attribute with a non-zero
value).  For example, using the following code:

.. code-block:: c

  int main ()
  {
    int *b = 0;
    return *b;
  }

We get a few type DIEs:

.. code-block:: none

  0x00000067:     TAG_base_type [5]
                  AT_encoding( DW_ATE_signed )
                  AT_name( "int" )
                  AT_byte_size( 0x04 )

  0x0000006e:     TAG_pointer_type [6]
                  AT_type( {0x00000067} ( int ) )
                  AT_byte_size( 0x08 )

The DW_TAG_pointer_type is not included because it does not have a ``DW_AT_name``.

"``.apple_namespaces``" section should contain all ``DW_TAG_namespace`` DIEs.
If we run into a namespace that has no name this is an anonymous namespace, and
the name should be output as "``(anonymous namespace)``" (without the quotes).
Why?  This matches the output of the ``abi::cxa_demangle()`` that is in the
standard C++ library that demangles mangled names.


Language Extensions and File Format Changes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Objective-C Extensions
""""""""""""""""""""""

"``.apple_objc``" section should contain all ``DW_TAG_subprogram`` DIEs for an
Objective-C class.  The name used in the hash table is the name of the
Objective-C class itself.  If the Objective-C class has a category, then an
entry is made for both the class name without the category, and for the class
name with the category.  So if we have a DIE at offset 0x1234 with a name of
method "``-[NSString(my_additions) stringWithSpecialString:]``", we would add
an entry for "``NSString``" that points to DIE 0x1234, and an entry for
"``NSString(my_additions)``" that points to 0x1234.  This allows us to quickly
track down all Objective-C methods for an Objective-C class when doing
expressions.  It is needed because of the dynamic nature of Objective-C where
anyone can add methods to a class.  The DWARF for Objective-C methods is also
emitted differently from C++ classes where the methods are not usually
contained in the class definition, they are scattered about across one or more
compile units.  Categories can also be defined in different shared libraries.
So we need to be able to quickly find all of the methods and class functions
given the Objective-C class name, or quickly find all methods and class
functions for a class + category name.  This table does not contain any
selector names, it just maps Objective-C class names (or class names +
category) to all of the methods and class functions.  The selectors are added
as function basenames in the "``.debug_names``" section.

In the "``.apple_names``" section for Objective-C functions, the full name is
the entire function name with the brackets ("``-[NSString
stringWithCString:]``") and the basename is the selector only
("``stringWithCString:``").

Mach-O Changes
""""""""""""""

The sections names for the apple hash tables are for non-mach-o files.  For
mach-o files, the sections should be contained in the ``__DWARF`` segment with
names as follows:

* "``.apple_names``" -> "``__apple_names``"
* "``.apple_types``" -> "``__apple_types``"
* "``.apple_namespaces``" -> "``__apple_namespac``" (16 character limit)
* "``.apple_objc``" -> "``__apple_objc``"

.. _codeview:

CodeView Debug Info Format
==========================

LLVM supports emitting CodeView, the Microsoft debug info format, and this
section describes the design and implementation of that support.

Format Background
-----------------

CodeView as a format is clearly oriented around C++ debugging, and in C++, the
majority of debug information tends to be type information. Therefore, the
overriding design constraint of CodeView is the separation of type information
from other "symbol" information so that type information can be efficiently
merged across translation units. Both type information and symbol information is
generally stored as a sequence of records, where each record begins with a
16-bit record size and a 16-bit record kind.

Type information is usually stored in the ``.debug$T`` section of the object
file.  All other debug info, such as line info, string table, symbol info, and
inlinee info, is stored in one or more ``.debug$S`` sections. There may only be
one ``.debug$T`` section per object file, since all other debug info refers to
it. If a PDB (enabled by the ``/Zi`` MSVC option) was used during compilation,
the ``.debug$T`` section will contain only an ``LF_TYPESERVER2`` record pointing
to the PDB. When using PDBs, symbol information appears to remain in the object
file ``.debug$S`` sections.

Type records are referred to by their index, which is the number of records in
the stream before a given record plus ``0x1000``. Many common basic types, such
as the basic integral types and unqualified pointers to them, are represented
using type indices less than ``0x1000``. Such basic types are built in to
CodeView consumers and do not require type records.

Each type record may only contain type indices that are less than its own type
index. This ensures that the graph of type stream references is acyclic. While
the source-level type graph may contain cycles through pointer types (consider a
linked list struct), these cycles are removed from the type stream by always
referring to the forward declaration record of user-defined record types. Only
"symbol" records in the ``.debug$S`` streams may refer to complete,
non-forward-declaration type records.

Working with CodeView
---------------------

These are instructions for some common tasks for developers working to improve
LLVM's CodeView support. Most of them revolve around using the CodeView dumper
embedded in ``llvm-readobj``.

* Testing MSVC's output::

    $ cl -c -Z7 foo.cpp # Use /Z7 to keep types in the object file
    $ llvm-readobj --codeview foo.obj

* Getting LLVM IR debug info out of Clang::

    $ clang -g -gcodeview --target=x86_64-windows-msvc foo.cpp -S -emit-llvm

  Use this to generate LLVM IR for LLVM test cases.

* Generate and dump CodeView from LLVM IR metadata::

    $ llc foo.ll -filetype=obj -o foo.obj
    $ llvm-readobj --codeview foo.obj > foo.txt

  Use this pattern in lit test cases and FileCheck the output of llvm-readobj

Improving LLVM's CodeView support is a process of finding interesting type
records, constructing a C++ test case that makes MSVC emit those records,
dumping the records, understanding them, and then generating equivalent records
in LLVM's backend.
