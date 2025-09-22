================
The LLVM Lexicon
================

.. note::

    This document is a work in progress!

Definitions
===========

A
-

**ADCE**
    Aggressive Dead Code Elimination

**AST**
    Abstract Syntax Tree.

    Due to Clang's influence (mostly the fact that parsing and semantic
    analysis are so intertwined for C and especially C++), the typical
    working definition of AST in the LLVM community is roughly "the
    compiler's first complete symbolic (as opposed to textual)
    representation of an input program".
    As such, an "AST" might be a more general graph instead of a "tree"
    (consider the symbolic representation for the type of a typical "linked
    list node"). This working definition is closer to what some authors
    call an "annotated abstract syntax tree".

    Consult your favorite compiler book or search engine for more details.

B
-

.. _lexicon-bb-vectorization:

**BB Vectorization**
    Basic-Block Vectorization

**BDCE**
    Bit-tracking dead code elimination. Some bit-wise instructions (shifts,
    ands, ors, etc.) "kill" some of their input bits -- that is, they make it
    such that those bits can be either zero or one without affecting control or
    data flow of a program. The BDCE pass removes instructions that only
    compute these dead bits.

**BURS**
    Bottom Up Rewriting System --- A method of instruction selection for code
    generation.  An example is the `BURG
    <http://www.program-transformation.org/Transform/BURG>`_ tool.

C
-

**CFI**
    This abbreviation has two meanings.
    Either:
    Call Frame Information. Used in DWARF debug info and in C++ unwind info
    to show how the function prolog lays out the stack frame.

    Or:
    Control Flow Integrity. A general term for computer security techniques
    that prevent a wide variety of malware attacks from redirecting the flow
    of execution (the control flow) of a program.

**CIE**
    Common Information Entry.  A kind of CFI used to reduce the size of FDEs.
    The compiler creates a CIE which contains the information common across all
    the FDEs.  Each FDE then points to its CIE.

**CSE**
    Common Subexpression Elimination. An optimization that removes common
    subexpression computation. For example ``(a+b)*(a+b)`` has two
    subexpressions that are the same: ``(a+b)``. This optimization would
    perform the addition only once and then perform the multiply (but only if
    it's computationally correct/safe).

D
-

**DAG**
    Directed Acyclic Graph

.. _derived pointer:
.. _derived pointers:

**Derived Pointer**
    A pointer to the interior of an object, such that a garbage collector is
    unable to use the pointer for reachability analysis. While a derived pointer
    is live, the corresponding object pointer must be kept in a root, otherwise
    the collector might free the referenced object. With copying collectors,
    derived pointers pose an additional hazard that they may be invalidated at
    any `safe point`_. This term is used in opposition to `object pointer`_.

**DSA**
    Data Structure Analysis

**DSE**
    Dead Store Elimination

E
-

**ento**
    This namespace houses the
    `Clang Static Analyzer <https://clang.llvm.org/docs/ClangStaticAnalyzer.html>`_.
    It is an abbreviation of `entomology <https://en.wikipedia.org/wiki/Entomology>`_.

      *"Entomology is the scientific study of insects."*

    In the past, this namespace had not only the name `GR` (aka. Graph Reachability)
    but also `entoSA`.

F
-

**FCA**
    First Class Aggregate

**FDE**
    Frame Description Entry. A kind of CFI used to describe the stack frame of
    one function.

G
-

**GC**
    Garbage Collection. The practice of using reachability analysis instead of
    explicit memory management to reclaim unused memory.

**GEP**
    ``GetElementPtr``. An LLVM IR instruction that is used to get the address
    of a subelement of an aggregate data structure. It is documented in detail
    `here <https://llvm.org/docs/GetElementPtr.html>`_.

**GVN**
    Global Value Numbering. GVN is a pass that partitions values computed by a
    function into congruence classes. Values ending up in the same congruence
    class are guaranteed to be the same for every execution of the program.
    In that respect, congruency is a compile-time approximation of equivalence
    of values at runtime.

H
-

.. _heap:

**Heap**
    In garbage collection, the region of memory which is managed using
    reachability analysis.

I
-

**ICE**
    Internal Compiler Error. This abbreviation is used to describe errors
    that occur in LLVM or Clang as they are compiling source code. For example,
    if a valid C++ source program were to trigger an assert in Clang when
    compiled, that could be referred to as an "ICE".

**ICF**
    Identical Code Folding

**ICP**
    Indirect Call Promotion

**IPA**
    Inter-Procedural Analysis. Refers to any variety of code analysis that
    occurs between procedures, functions or compilation units (modules).

**IPO**
    Inter-Procedural Optimization. Refers to any variety of code optimization
    that occurs between procedures, functions or compilation units (modules).

**ISel**
    Instruction Selection

L
-

**LCSSA**
    Loop-Closed Static Single Assignment Form

**LGTM**
    "Looks Good To Me". In a review thread, this indicates that the
    reviewer thinks that the patch is okay to commit.

**LICM**
    Loop Invariant Code Motion

**LSDA**
    Language Specific Data Area.  C++ "zero cost" unwinding is built on top a
    generic unwinding mechanism.  As the unwinder walks each frame, it calls
    a "personality" function to do language specific analysis.  Each function's
    FDE points to an optional LSDA which is passed to the personality function.
    For C++, the LSDA contain info about the type and location of catch
    statements in that function.

**Load-VN**
    Load Value Numbering

**LTO**
    Link-Time Optimization

M
-

**MC**
    Machine Code

N
-
.. _nfc:

**NFC**
  "No functional change". Used in a commit message to indicate that a patch
  is a pure refactoring/cleanup.
  Usually used in the first line, so it is visible without opening the
  actual commit email.

O
-
.. _object pointer:
.. _object pointers:

**Object Pointer**
    A pointer to an object such that the garbage collector is able to trace
    references contained within the object. This term is used in opposition to
    `derived pointer`_.

P
-

**PGO**
    Profile-Guided Optimization

**PR**
    Problem report. A bug filed on `the LLVM Bug Tracking System
    <https://bugs.llvm.org/enter_bug.cgi>`_.

**PRE**
    Partial Redundancy Elimination

R
-

**RAUW**

    Replace All Uses With. The functions ``User::replaceUsesOfWith()``,
    ``Value::replaceAllUsesWith()``, and
    ``Constant::replaceUsesOfWithOnConstant()`` implement the replacement of one
    Value with another by iterating over its def/use chain and fixing up all of
    the pointers to point to the new value.  See
    also `def/use chains <ProgrammersManual.html#iterating-over-def-use-use-def-chains>`_.

**Reassociation**
    Rearranging associative expressions to promote better redundancy elimination
    and other optimization.  For example, changing ``(A+B-A)`` into ``(B+A-A)``,
    permitting it to be optimized into ``(B+0)`` then ``(B)``.

**RFC**
  Request for Comment. An email sent to a project mailing list in order to
  solicit feedback on a proposed change.

.. _roots:
.. _stack roots:

**Root**
    In garbage collection, a pointer variable lying outside of the `heap`_ from
    which the collector begins its reachability analysis. In the context of code
    generation, "root" almost always refers to a "stack root" --- a local or
    temporary variable within an executing function.

**RPO**
    Reverse postorder

**RTTI**
    Run-time Type Information

S
-

.. _safe point:

**Safe Point**
    In garbage collection, it is necessary to identify `stack roots`_ so that
    reachability analysis may proceed. It may be infeasible to provide this
    information for every instruction, so instead the information is
    calculated only at designated safe points. With a copying collector,
    `derived pointers`_ must not be retained across safe points and `object
    pointers`_ must be reloaded from stack roots.

**SDISel**
    Selection DAG Instruction Selection.

**SCC**
    Strongly Connected Component

**SCCP**
    Sparse Conditional Constant Propagation

**SLP**
    Superword-Level Parallelism, same as :ref:`Basic-Block Vectorization
    <lexicon-bb-vectorization>`.

**Splat**
    Splat refers to a vector of identical scalar elements.

    The term is based on the PowerPC Altivec instructions that provided
    this functionality in hardware. For example, "vsplth" and the corresponding
    software intrinsic "vec_splat()". Examples of other hardware names for this
    action include "duplicate" (ARM) and "broadcast" (x86).

**SRoA**
    Scalar Replacement of Aggregates

**SSA**
    Static Single Assignment

**Stack Map**
    In garbage collection, metadata emitted by the code generator which
    identifies `roots`_ within the stack frame of an executing function.

T
-

**TBAA**
    Type-Based Alias Analysis


W
-

**WPD**
    Whole Program Devirtualization

