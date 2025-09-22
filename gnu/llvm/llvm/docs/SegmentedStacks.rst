========================
Segmented Stacks in LLVM
========================

.. contents::
   :local:

Introduction
============

Segmented stack allows stack space to be allocated incrementally than as a
monolithic chunk (of some worst case size) at thread initialization. This is
done by allocating stack blocks (henceforth called *stacklets*) and linking them
into a doubly linked list. The function prologue is responsible for checking if
the current stacklet has enough space for the function to execute; and if not,
call into the libgcc runtime to allocate more stack space. Segmented stacks are
enabled with the ``"split-stack"`` attribute on LLVM functions.

The runtime functionality is `already there in libgcc
<http://gcc.gnu.org/wiki/SplitStacks>`_.

Implementation Details
======================

.. _allocating stacklets:

Allocating Stacklets
--------------------

As mentioned above, the function prologue checks if the current stacklet has
enough space. The current approach is to use a slot in the TCB to store the
current stack limit (minus the amount of space needed to allocate a new block) -
this slot's offset is again dictated by ``libgcc``. The generated
assembly looks like this on x86-64:

.. code-block:: text

    leaq     -8(%rsp), %r10
    cmpq     %fs:112,  %r10
    jg       .LBB0_2

    # More stack space needs to be allocated
    movabsq  $8, %r10   # The amount of space needed
    movabsq  $0, %r11   # The total size of arguments passed on stack
    callq    __morestack
    ret                 # The reason for this extra return is explained below
  .LBB0_2:
    # Usual prologue continues here

The size of function arguments on the stack needs to be passed to
``__morestack`` (this function is implemented in ``libgcc``) since that number
of bytes has to be copied from the previous stacklet to the current one. This is
so that SP (and FP) relative addressing of function arguments work as expected.

The unusual ``ret`` is needed to have the function which made a call to
``__morestack`` return correctly. ``__morestack``, instead of returning, calls
into ``.LBB0_2``. This is possible since both, the size of the ``ret``
instruction and the PC of call to ``__morestack`` are known. When the function
body returns, control is transferred back to ``__morestack``. ``__morestack``
then de-allocates the new stacklet, restores the correct SP value, and does a
second return, which returns control to the correct caller.

Variable Sized Allocas
----------------------

The section on `allocating stacklets`_ automatically assumes that every stack
frame will be of fixed size. However, LLVM allows the use of the ``llvm.alloca``
intrinsic to allocate dynamically sized blocks of memory on the stack. When
faced with such a variable-sized alloca, code is generated to:

* Check if the current stacklet has enough space. If yes, just bump the SP, like
  in the normal case.
* If not, generate a call to ``libgcc``, which allocates the memory from the
  heap.

The memory allocated from the heap is linked into a list in the current
stacklet, and freed along with the same. This prevents a memory leak.
