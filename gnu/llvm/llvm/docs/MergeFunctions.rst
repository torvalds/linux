=================================
MergeFunctions pass, how it works
=================================

.. contents::
   :local:

Introduction
============
Sometimes code contains equal functions, or functions that does exactly the same
thing even though they are non-equal on the IR level (e.g.: multiplication on 2
and 'shl 1'). It could happen due to several reasons: mainly, the usage of
templates and automatic code generators. Though, sometimes the user itself could
write the same thing twice :-)

The main purpose of this pass is to recognize such functions and merge them.

This document is the extension to pass comments and describes the pass logic. It
describes the algorithm that is used in order to compare functions and
explains how we could combine equal functions correctly to keep the module
valid.

Material is brought in a top-down form, so the reader could start to learn pass
from high level ideas and end with low-level algorithm details, thus preparing
him or her for reading the sources.

The main goal is to describe the algorithm and logic here and the concept. If
you *don't want* to read the source code, but want to understand pass
algorithms, this document is good for you. The author tries not to repeat the
source-code and covers only common cases to avoid the cases of needing to
update this document after any minor code changes.


What should I know to be able to follow along with this document?
-----------------------------------------------------------------

The reader should be familiar with common compile-engineering principles and
LLVM code fundamentals. In this article, we assume the reader is familiar with
`Single Static Assignment
<http://en.wikipedia.org/wiki/Static_single_assignment_form>`_
concept and has an understanding of
`IR structure <https://llvm.org/docs/LangRef.html#high-level-structure>`_.

We will use terms such as
"`module <https://llvm.org/docs/LangRef.html#high-level-structure>`_",
"`function <https://llvm.org/docs/ProgrammersManual.html#the-function-class>`_",
"`basic block <http://en.wikipedia.org/wiki/Basic_block>`_",
"`user <https://llvm.org/docs/ProgrammersManual.html#the-user-class>`_",
"`value <https://llvm.org/docs/ProgrammersManual.html#the-value-class>`_",
"`instruction
<https://llvm.org/docs/ProgrammersManual.html#the-instruction-class>`_".

As a good starting point, the Kaleidoscope tutorial can be used:

:doc:`tutorial/index`

It's especially important to understand chapter 3 of tutorial:

:doc:`tutorial/LangImpl03`

The reader should also know how passes work in LLVM. They could use this
article as a reference and start point here:

:doc:`WritingAnLLVMPass`

What else? Well perhaps the reader should also have some experience in LLVM pass
debugging and bug-fixing.

Narrative structure
-------------------
The article consists of three parts. The first part explains pass functionality
on the top-level. The second part describes the comparison procedure itself.
The third part describes the merging process.

In every part, the author tries to put the contents in the top-down form.
The top-level methods will first be described followed by the terminal ones at
the end, in the tail of each part. If the reader sees the reference to the
method that wasn't described yet, they will find its description a bit below.

Basics
======

How to do it?
-------------
Do we need to merge functions? The obvious answer is: Yes, that is quite a
possible case. We usually *do* have duplicates and it would be good to get rid
of them. But how do we detect duplicates? This is the idea: we split functions
into smaller bricks or parts and compare the "bricks" amount. If equal,
we compare the "bricks" themselves, and then do our conclusions about functions
themselves.

What could the difference be? For example, on a machine with 64-bit pointers
(let's assume we have only one address space), one function stores a 64-bit
integer, while another one stores a pointer. If the target is the machine
mentioned above, and if functions are identical, except the parameter type (we
could consider it as a part of function type), then we can treat a ``uint64_t``
and a ``void*`` as equal.

This is just an example; more possible details are described a bit below.

As another example, the reader may imagine two more functions. The first
function performs a multiplication by 2, while the second one performs an
logical left shift by 1.

Possible solutions
^^^^^^^^^^^^^^^^^^
Let's briefly consider possible options about how and what we have to implement
in order to create full-featured functions merging, and also what it would
mean for us.

Equal function detection obviously supposes that a "detector" method to be
implemented and latter should answer the question "whether functions are equal".
This "detector" method consists of tiny "sub-detectors", which each answers
exactly the same question, but for function parts.

As the second step, we should merge equal functions. So it should be a "merger"
method. "Merger" accepts two functions *F1* and *F2*, and produces *F1F2*
function, the result of merging.

Having such routines in our hands, we can process a whole module, and merge all
equal functions.

In this case, we have to compare every function with every another function. As
the reader may notice, this way seems to be quite expensive. Of course we could
introduce hashing and other helpers, but it is still just an optimization, and
thus the level of O(N*N) complexity.

Can we reach another level? Could we introduce logarithmical search, or random
access lookup? The answer is: "yes".

Random-access
"""""""""""""
How it could this be done? Just convert each function to a number, and gather
all of them in a special hash-table. Functions with equal hashes are equal.
Good hashing means, that every function part must be taken into account. That
means we have to convert every function part into some number, and then add it
into the hash. The lookup-up time would be small, but such an approach adds some
delay due to the hashing routine.

Logarithmical search
""""""""""""""""""""
We could introduce total ordering among the functions set, once ordered we
could then implement a logarithmical search. Lookup time still depends on N,
but adds a little of delay (*log(N)*).

Present state
"""""""""""""
Both of the approaches (random-access and logarithmical) have been implemented
and tested and both give a very good improvement. What was most
surprising is that logarithmical search was faster; sometimes by up to 15%. The
hashing method needs some extra CPU time, which is the main reason why it works
slower; in most cases, total "hashing" time is greater than total
"logarithmical-search" time.

So, preference has been granted to the "logarithmical search".

Though in the case of need, *logarithmical-search* (read "total-ordering") could
be used as a milestone on our way to the *random-access* implementation.

Every comparison is based either on the numbers or on the flags comparison. In
the *random-access* approach, we could use the same comparison algorithm.
During comparison, we exit once we find the difference, but here we might have
to scan the whole function body every time (note, it could be slower). Like in
"total-ordering", we will track every number and flag, but instead of
comparison, we should get the numbers sequence and then create the hash number.
So, once again, *total-ordering* could be considered as a milestone for even
faster (in theory) random-access approach.

MergeFunctions, main fields and runOnModule
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
There are two main important fields in the class:

``FnTree``  – the set of all unique functions. It keeps items that couldn't be
merged with each other. It is defined as:

``std::set<FunctionNode> FnTree;``

Here ``FunctionNode`` is a wrapper for ``llvm::Function`` class, with
implemented “<” operator among the functions set (below we explain how it works
exactly; this is a key point in fast functions comparison).

``Deferred`` – merging process can affect bodies of functions that are in
``FnTree`` already. Obviously, such functions should be rechecked again. In this
case, we remove them from ``FnTree``, and mark them to be rescanned, namely
put them into ``Deferred`` list.

runOnModule
"""""""""""
The algorithm is pretty simple:

1. Put all module's functions into the *worklist*.

2. Scan *worklist*'s functions twice: first enumerate only strong functions and
then only weak ones:

   2.1. Loop body: take a function from *worklist*  (call it *FCur*) and try to
   insert it into *FnTree*: check whether *FCur* is equal to one of functions
   in *FnTree*. If there *is* an equal function in *FnTree*
   (call it *FExists*): merge function *FCur* with *FExists*. Otherwise add
   the function from the *worklist* to *FnTree*.

3. Once the *worklist* scanning and merging operations are complete, check the
*Deferred* list. If it is not empty: refill the *worklist* contents with
*Deferred* list and redo step 2, if the *Deferred* list is empty, then exit
from method.

Comparison and logarithmical search
"""""""""""""""""""""""""""""""""""
Let's recall our task: for every function *F* from module *M*, we have to find
equal functions *F`* in the shortest time possible , and merge them into a
single function.

Defining total ordering among the functions set allows us to organize
functions into a binary tree. The lookup procedure complexity would be
estimated as O(log(N)) in this case. But how do we define *total-ordering*?

We have to introduce a single rule applicable to every pair of functions, and
following this rule, then evaluate which of them is greater. What kind of rule
could it be? Let's declare it as the "compare" method that returns one of 3
possible values:

-1, left is *less* than right,

0, left and right are *equal*,

1, left is *greater* than right.

Of course it means, that we have to maintain
*strict and non-strict order relation properties*:

* reflexivity (``a <= a``, ``a == a``, ``a >= a``),
* antisymmetry (if ``a <= b`` and ``b <= a`` then ``a == b``),
* transitivity (``a <= b`` and ``b <= c``, then ``a <= c``)
* asymmetry (if ``a < b``, then ``a > b`` or ``a == b``).

As mentioned before, the comparison routine consists of
"sub-comparison-routines", with each of them also consisting of
"sub-comparison-routines", and so on. Finally, it ends up with primitive
comparison.

Below, we will use the following operations:

#. ``cmpNumbers(number1, number2)`` is a method that returns -1 if left is less
   than right; 0, if left and right are equal; and 1 otherwise.

#. ``cmpFlags(flag1, flag2)`` is a hypothetical method that compares two flags.
   The logic is the same as in ``cmpNumbers``, where ``true`` is 1, and
   ``false`` is 0.

The rest of the article is based on *MergeFunctions.cpp* source code
(found in *<llvm_dir>/lib/Transforms/IPO/MergeFunctions.cpp*). We would like
to ask reader to keep this file open, so we could use it as a reference
for further explanations.

Now, we're ready to proceed to the next chapter and see how it works.

Functions comparison
====================
At first, let's define how exactly we compare complex objects.

Complex object comparison (function, basic-block, etc) is mostly based on its
sub-object comparison results. It is similar to the next "tree" objects
comparison:

#. For two trees *T1* and *T2* we perform *depth-first-traversal* and have
   two sequences as a product: "*T1Items*" and "*T2Items*".

#. We then compare chains "*T1Items*" and "*T2Items*" in
   the most-significant-item-first order. The result of items comparison
   would be the result of *T1* and *T2* comparison itself.

FunctionComparator::compare(void)
---------------------------------
A brief look at the source code tells us that the comparison starts in the
“``int FunctionComparator::compare(void)``” method.

1. The first parts to be compared are the function's attributes and some
properties that is outside the “attributes” term, but still could make the
function different without changing its body. This part of the comparison is
usually done within simple *cmpNumbers* or *cmpFlags* operations (e.g.
``cmpFlags(F1->hasGC(), F2->hasGC())``). Below is a full list of function's
properties to be compared on this stage:

  * *Attributes* (those are returned by ``Function::getAttributes()``
    method).

  * *GC*, for equivalence, *RHS* and *LHS* should be both either without
    *GC* or with the same one.

  * *Section*, just like a *GC*: *RHS* and *LHS* should be defined in the
    same section.

  * *Variable arguments*. *LHS* and *RHS* should be both either with or
    without *var-args*.

  * *Calling convention* should be the same.

2. Function type. Checked by ``FunctionComparator::cmpType(Type*, Type*)``
method. It checks return type and parameters type; the method itself will be
described later.

3. Associate function formal parameters with each other. Then comparing function
bodies, if we see the usage of *LHS*'s *i*-th argument in *LHS*'s body, then,
we want to see usage of *RHS*'s *i*-th argument at the same place in *RHS*'s
body, otherwise functions are different. On this stage we grant the preference
to those we met later in function body (value we met first would be *less*).
This is done by “``FunctionComparator::cmpValues(const Value*, const Value*)``”
method (will be described a bit later).

4. Function body comparison. As it written in method comments:

“We do a CFG-ordered walk since the actual ordering of the blocks in the linked
list is immaterial. Our walk starts at the entry block for both functions, then
takes each block from each terminator in order. As an artifact, this also means
that unreachable blocks are ignored.”

So, using this walk we get BBs from *left* and *right* in the same order, and
compare them by “``FunctionComparator::compare(const BasicBlock*, const
BasicBlock*)``” method.

We also associate BBs with each other, like we did it with function formal
arguments (see ``cmpValues`` method below).

FunctionComparator::cmpType
---------------------------
Consider how type comparison works.

1. Coerce pointer to integer. If left type is a pointer, try to coerce it to the
integer type. It could be done if its address space is 0, or if address spaces
are ignored at all. Do the same thing for the right type.

2. If left and right types are equal, return 0. Otherwise we need to give
preference to one of them. So proceed to the next step.

3. If types are of different kind (different type IDs). Return result of type
IDs comparison, treating them as numbers (use ``cmpNumbers`` operation).

4. If types are vectors or integers, return result of their pointers comparison,
comparing them as numbers.

5. Check whether type ID belongs to the next group (call it equivalent-group):

   * Void

   * Float

   * Double

   * X86_FP80

   * FP128

   * PPC_FP128

   * Label

   * Metadata.

   If ID belongs to group above, return 0. Since it's enough to see that
   types has the same ``TypeID``. No additional information is required.

6. Left and right are pointers. Return result of address space comparison
(numbers comparison).

7. Complex types (structures, arrays, etc.). Follow complex objects comparison
technique (see the very first paragraph of this chapter). Both *left* and
*right* are to be expanded and their element types will be checked the same
way. If we get -1 or 1 on some stage, return it. Otherwise return 0.

8. Steps 1-6 describe all the possible cases, if we passed steps 1-6 and didn't
get any conclusions, then invoke ``llvm_unreachable``, since it's quite an
unexpectable case.

cmpValues(const Value*, const Value*)
-------------------------------------
Method that compares local values.

This method gives us an answer to a very curious question: whether we could
treat local values as equal, and which value is greater otherwise. It's
better to start from example:

Consider the situation when we're looking at the same place in left
function "*FL*" and in right function "*FR*". Every part of *left* place is
equal to the corresponding part of *right* place, and (!) both parts use
*Value* instances, for example:

.. code-block:: text

   instr0 i32 %LV   ; left side, function FL
   instr0 i32 %RV   ; right side, function FR

So, now our conclusion depends on *Value* instances comparison.

The main purpose of this method is to determine relation between such values.

What can we expect from equal functions? At the same place, in functions
"*FL*" and "*FR*" we expect to see *equal* values, or values *defined* at
the same place in "*FL*" and "*FR*".

Consider a small example here:

.. code-block:: text

  define void %f(i32 %pf0, i32 %pf1) {
    instr0 i32 %pf0 instr1 i32 %pf1 instr2 i32 123
  }

.. code-block:: text

  define void %g(i32 %pg0, i32 %pg1) {
    instr0 i32 %pg0 instr1 i32 %pg0 instr2 i32 123
  }

In this example, *pf0* is associated with *pg0*, *pf1* is associated with
*pg1*, and we also declare that *pf0* < *pf1*, and thus *pg0* < *pf1*.

Instructions with opcode "*instr0*" would be *equal*, since their types and
opcodes are equal, and values are *associated*.

Instructions with opcode "*instr1*" from *f* is *greater* than instructions
with opcode "*instr1*" from *g*; here we have equal types and opcodes, but
"*pf1* is greater than "*pg0*".

Instructions with opcode "*instr2*" are equal, because their opcodes and
types are equal, and the same constant is used as a value.

What we associate in cmpValues?
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
* Function arguments. *i*-th argument from left function associated with
  *i*-th argument from right function.
* BasicBlock instances. In basic-block enumeration loop we associate *i*-th
  BasicBlock from the left function with *i*-th BasicBlock from the right
  function.
* Instructions.
* Instruction operands. Note, we can meet *Value* here we have never seen
  before. In this case it is not a function argument, nor *BasicBlock*, nor
  *Instruction*. It is a global value. It is a constant, since it's the only
  supposed global here. The method also compares: Constants that are of the
  same type and if right constant can be losslessly bit-casted to the left
  one, then we also compare them.

How to implement cmpValues?
^^^^^^^^^^^^^^^^^^^^^^^^^^^
*Association* is a case of equality for us. We just treat such values as equal,
but, in general, we need to implement antisymmetric relation. As mentioned
above, to understand what is *less*, we can use order in which we
meet values. If both values have the same order in a function (met at the same
time), we then treat values as *associated*. Otherwise – it depends on who was
first.

Every time we run the top-level compare method, we initialize two identical
maps (one for the left side, another one for the right side):

``map<Value, int> sn_mapL, sn_mapR;``

The key of the map is the *Value* itself, the *value* – is its order (call it
*serial number*).

To add value *V* we need to perform the next procedure:

``sn_map.insert(std::make_pair(V, sn_map.size()));``

For the first *Value*, map will return *0*, for the second *Value* map will
return *1*, and so on.

We can then check whether left and right values met at the same time with
a simple comparison:

``cmpNumbers(sn_mapL[Left], sn_mapR[Right]);``

Of course, we can combine insertion and comparison:

.. code-block:: c++

  std::pair<iterator, bool>
    LeftRes = sn_mapL.insert(std::make_pair(Left, sn_mapL.size())), RightRes
    = sn_mapR.insert(std::make_pair(Right, sn_mapR.size()));
  return cmpNumbers(LeftRes.first->second, RightRes.first->second);

Let's look, how whole method could be implemented.

1. We have to start with the bad news. Consider function self and
cross-referencing cases:

.. code-block:: c++

  // self-reference unsigned fact0(unsigned n) { return n > 1 ? n
  * fact0(n-1) : 1; } unsigned fact1(unsigned n) { return n > 1 ? n *
  fact1(n-1) : 1; }

  // cross-reference unsigned ping(unsigned n) { return n!= 0 ? pong(n-1) : 0;
  } unsigned pong(unsigned n) { return n!= 0 ? ping(n-1) : 0; }

..

  This comparison has been implemented in initial *MergeFunctions* pass
  version. But, unfortunately, it is not transitive. And this is the only case
  we can't convert to less-equal-greater comparison. It is a seldom case, 4-5
  functions of 10000 (checked in test-suite), and, we hope, the reader would
  forgive us for such a sacrifice in order to get the O(log(N)) pass time.

2. If left/right *Value* is a constant, we have to compare them. Return 0 if it
is the same constant, or use ``cmpConstants`` method otherwise.

3. If left/right is *InlineAsm* instance. Return result of *Value* pointers
comparison.

4. Explicit association of *L* (left value) and *R*  (right value). We need to
find out whether values met at the same time, and thus are *associated*. Or we
need to put the rule: when we treat *L* < *R*. Now it is easy: we just return
the result of numbers comparison:

.. code-block:: c++

   std::pair<iterator, bool>
     LeftRes = sn_mapL.insert(std::make_pair(Left, sn_mapL.size())),
     RightRes = sn_mapR.insert(std::make_pair(Right, sn_mapR.size()));
   if (LeftRes.first->second == RightRes.first->second) return 0;
   if (LeftRes.first->second < RightRes.first->second) return -1;
   return 1;

Now when *cmpValues* returns 0, we can proceed the comparison procedure.
Otherwise, if we get (-1 or 1), we need to pass this result to the top level,
and finish comparison procedure.

cmpConstants
------------
Performs constants comparison as follows:

1. Compare constant types using ``cmpType`` method. If the result is -1 or 1,
goto step 2, otherwise proceed to step 3.

2. If types are different, we still can check whether constants could be
losslessly bitcasted to each other. The further explanation is modification of
``canLosslesslyBitCastTo`` method.

   2.1 Check whether constants are of the first class types
   (``isFirstClassType`` check):

   2.1.1. If both constants are *not* of the first class type: return result
   of ``cmpType``.

   2.1.2. Otherwise, if left type is not of the first class, return -1. If
   right type is not of the first class, return 1.

   2.1.3. If both types are of the first class type, proceed to the next step
   (2.1.3.1).

   2.1.3.1. If types are vectors, compare their bitwidth using the
   *cmpNumbers*. If result is not 0, return it.

   2.1.3.2. Different types, but not a vectors:

   * if both of them are pointers, good for us, we can proceed to step 3.
   * if one of types is pointer, return result of *isPointer* flags
     comparison (*cmpFlags* operation).
   * otherwise we have no methods to prove bitcastability, and thus return
     result of types comparison (-1 or 1).

Steps below are for the case when types are equal, or case when constants are
bitcastable:

3. One of constants is a "*null*" value. Return the result of
``cmpFlags(L->isNullValue, R->isNullValue)`` comparison.

4. Compare value IDs, and return result if it is not 0:

.. code-block:: c++

  if (int Res = cmpNumbers(L->getValueID(), R->getValueID()))
    return Res;

5. Compare the contents of constants. The comparison depends on the kind of
constants, but on this stage it is just a lexicographical comparison. Just see
how it was described in the beginning of "*Functions comparison*" paragraph.
Mathematically, it is equal to the next case: we encode left constant and right
constant (with similar way *bitcode-writer* does). Then compare left code
sequence and right code sequence.

compare(const BasicBlock*, const BasicBlock*)
---------------------------------------------
Compares two *BasicBlock* instances.

It enumerates instructions from left *BB* and right *BB*.

1. It assigns serial numbers to the left and right instructions, using
``cmpValues`` method.

2. If one of left or right is *GEP* (``GetElementPtr``), then treat *GEP* as
greater than other instructions. If both instructions are *GEPs* use ``cmpGEP``
method for comparison. If result is -1 or 1, pass it to the top-level
comparison (return it).

   3.1. Compare operations. Call ``cmpOperation`` method. If result is -1 or
   1, return it.

   3.2. Compare number of operands, if result is -1 or 1, return it.

   3.3. Compare operands themselves, use ``cmpValues`` method. Return result
   if it is -1 or 1.

   3.4. Compare type of operands, using ``cmpType`` method. Return result if
   it is -1 or 1.

   3.5. Proceed to the next instruction.

4. We can finish instruction enumeration in 3 cases:

   4.1. We reached the end of both left and right basic-blocks. We didn't
   exit on steps 1-3, so contents are equal, return 0.

   4.2. We have reached the end of the left basic-block. Return -1.

   4.3. Return 1 (we reached the end of the right basic block).

cmpGEP
------
Compares two GEPs (``getelementptr`` instructions).

It differs from regular operations comparison with the only thing: possibility
to use ``accumulateConstantOffset`` method.

So, if we get constant offset for both left and right *GEPs*, then compare it as
numbers, and return comparison result.

Otherwise treat it like a regular operation (see previous paragraph).

cmpOperation
------------
Compares instruction opcodes and some important operation properties.

1. Compare opcodes, if it differs return the result.

2. Compare number of operands. If it differs – return the result.

3. Compare operation types, use *cmpType*. All the same – if types are
different, return result.

4. Compare *subclassOptionalData*, get it with ``getRawSubclassOptionalData``
method, and compare it like a numbers.

5. Compare operand types.

6. For some particular instructions, check equivalence (relation in our case) of
some significant attributes. For example, we have to compare alignment for
``load`` instructions.

O(log(N))
---------
Methods described above implement order relationship. And latter, could be used
for nodes comparison in a binary tree. So we can organize functions set into
the binary tree and reduce the cost of lookup procedure from
O(N*N) to O(log(N)).

Merging process, mergeTwoFunctions
==================================
Once *MergeFunctions* detected that current function (*G*) is equal to one that
were analyzed before (function *F*) it calls ``mergeTwoFunctions(Function*,
Function*)``.

Operation affects ``FnTree`` contents with next way: *F* will stay in
``FnTree``. *G* being equal to *F* will not be added to ``FnTree``. Calls of
*G* would be replaced with something else. It changes bodies of callers. So,
functions that calls *G* would be put into ``Deferred`` set and removed from
``FnTree``, and analyzed again.

The approach is next:

1. Most wished case: when we can use alias and both of *F* and *G* are weak. We
make both of them with aliases to the third strong function *H*. Actually *H*
is *F*. See below how it's made (but it's better to look straight into the
source code). Well, this is a case when we can just replace *G* with *F*
everywhere, we use ``replaceAllUsesWith`` operation here (*RAUW*).

2. *F* could not be overridden, while *G* could. It would be good to do the
next: after merging the places where overridable function were used, still use
overridable stub. So try to make *G* alias to *F*, or create overridable tail
call wrapper around *F* and replace *G* with that call.

3. Neither *F* nor *G* could be overridden. We can't use *RAUW*. We can just
change the callers: call *F* instead of *G*.  That's what
``replaceDirectCallers`` does.

Below is a detailed body description.

If “F” may be overridden
------------------------
As follows from ``mayBeOverridden`` comments: “whether the definition of this
global may be replaced by something non-equivalent at link time”. If so, that's
ok: we can use alias to *F* instead of *G* or change call instructions itself.

HasGlobalAliases, removeUsers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
First consider the case when we have global aliases of one function name to
another. Our purpose is  make both of them with aliases to the third strong
function. Though if we keep *F* alive and without major changes we can leave it
in ``FnTree``. Try to combine these two goals.

Do stub replacement of *F* itself with an alias to *F*.

1. Create stub function *H*, with the same name and attributes like function
*F*. It takes maximum alignment of *F* and *G*.

2. Replace all uses of function *F* with uses of function *H*. It is the two
steps procedure instead. First of all, we must take into account, all functions
from whom *F* is called would be changed: since we change the call argument
(from *F* to *H*). If so we must to review these caller functions again after
this procedure. We remove callers from ``FnTree``, method with name
``removeUsers(F)`` does that (don't confuse with ``replaceAllUsesWith``):

   2.1. ``Inside removeUsers(Value*
   V)`` we go through the all values that use value *V* (or *F* in our context).
   If value is instruction, we go to function that holds this instruction and
   mark it as to-be-analyzed-again (put to ``Deferred`` set), we also remove
   caller from ``FnTree``.

   2.2. Now we can do the replacement: call ``F->replaceAllUsesWith(H)``.

3. *H* (that now "officially" plays *F*'s role) is replaced with alias to *F*.
Do the same with *G*: replace it with alias to *F*. So finally everywhere *F*
was used, we use *H* and it is alias to *F*, and everywhere *G* was used we
also have alias to *F*.

4. Set *F* linkage to private. Make it strong :-)

No global aliases, replaceDirectCallers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
If global aliases are not supported. We call ``replaceDirectCallers``. Just
go through all calls of *G* and replace it with calls of *F*. If you look into
the method you will see that it scans all uses of *G* too, and if use is callee
(if user is call instruction and *G* is used as what to be called), we replace
it with use of *F*.

If “F” could not be overridden, fix it!
"""""""""""""""""""""""""""""""""""""""

We call ``writeThunkOrAlias(Function *F, Function *G)``. Here we try to replace
*G* with alias to *F* first. The next conditions are essential:

* target should support global aliases,
* the address itself of  *G* should be not significant, not named and not
  referenced anywhere,
* function should come with external, local or weak linkage.

Otherwise we write thunk: some wrapper that has *G's* interface and calls *F*,
so *G* could be replaced with this wrapper.

*writeAlias*

As follows from *llvm* reference:

“Aliases act as *second name* for the aliasee value”. So we just want to create
a second name for *F* and use it instead of *G*:

1. create global alias itself (*GA*),

2. adjust alignment of *F* so it must be maximum of current and *G's* alignment;

3. replace uses of *G*:

   3.1. first mark all callers of *G* as to-be-analyzed-again, using
   ``removeUsers`` method (see chapter above),

   3.2. call ``G->replaceAllUsesWith(GA)``.

4. Get rid of *G*.

*writeThunk*

As it written in method comments:

“Replace G with a simple tail call to bitcast(F). Also replace direct uses of G
with bitcast(F). Deletes G.”

In general it does the same as usual when we want to replace callee, except the
first point:

1. We generate tail call wrapper around *F*, but with interface that allows use
it instead of *G*.

2. “As-usual”: ``removeUsers`` and ``replaceAllUsesWith`` then.

3. Get rid of *G*.


