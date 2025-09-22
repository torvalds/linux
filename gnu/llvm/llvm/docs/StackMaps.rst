===================================
Stack maps and patch points in LLVM
===================================

.. contents::
   :local:
   :depth: 2

Definitions
===========

In this document we refer to the "runtime" collectively as all
components that serve as the LLVM client, including the LLVM IR
generator, object code consumer, and code patcher.

A stack map records the location of ``live values`` at a particular
instruction address. These ``live values`` do not refer to all the
LLVM values live across the stack map. Instead, they are only the
values that the runtime requires to be live at this point. For
example, they may be the values the runtime will need to resume
program execution at that point independent of the compiled function
containing the stack map.

LLVM emits stack map data into the object code within a designated
:ref:`stackmap-section`. This stack map data contains a record for
each stack map. The record stores the stack map's instruction address
and contains an entry for each mapped value. Each entry encodes a
value's location as a register, stack offset, or constant.

A patch point is an instruction address at which space is reserved for
patching a new instruction sequence at run time. Patch points look
much like calls to LLVM. They take arguments that follow a calling
convention and may return a value. They also imply stack map
generation, which allows the runtime to locate the patchpoint and
find the location of ``live values`` at that point.

Motivation
==========

This functionality is currently experimental but is potentially useful
in a variety of settings, the most obvious being a runtime (JIT)
compiler. Example applications of the patchpoint intrinsics are
implementing an inline call cache for polymorphic method dispatch or
optimizing the retrieval of properties in dynamically typed languages
such as JavaScript.

The intrinsics documented here are currently used by the JavaScript
compiler within the open source WebKit project, see the `FTL JIT
<https://trac.webkit.org/wiki/FTLJIT>`_, but they are designed to be
used whenever stack maps or code patching are needed. Because the
intrinsics have experimental status, compatibility across LLVM
releases is not guaranteed.

The stack map functionality described in this document is separate
from the functionality described in
:ref:`stack-map`. `GCFunctionMetadata` provides the location of
pointers into a collected heap captured by the `GCRoot` intrinsic,
which can also be considered a "stack map". Unlike the stack maps
defined above, the `GCFunctionMetadata` stack map interface does not
provide a way to associate live register values of arbitrary type with
an instruction address, nor does it specify a format for the resulting
stack map. The stack maps described here could potentially provide
richer information to a garbage collecting runtime, but that usage
will not be discussed in this document.

Intrinsics
==========

The following two kinds of intrinsics can be used to implement stack
maps and patch points: ``llvm.experimental.stackmap`` and
``llvm.experimental.patchpoint``. Both kinds of intrinsics generate a
stack map record, and they both allow some form of code patching. They
can be used independently (i.e. ``llvm.experimental.patchpoint``
implicitly generates a stack map without the need for an additional
call to ``llvm.experimental.stackmap``). The choice of which to use
depends on whether it is necessary to reserve space for code patching
and whether any of the intrinsic arguments should be lowered according
to calling conventions. ``llvm.experimental.stackmap`` does not
reserve any space, nor does it expect any call arguments. If the
runtime patches code at the stack map's address, it will destructively
overwrite the program text. This is unlike
``llvm.experimental.patchpoint``, which reserves space for in-place
patching without overwriting surrounding code. The
``llvm.experimental.patchpoint`` intrinsic also lowers a specified
number of arguments according to its calling convention. This allows
patched code to make in-place function calls without marshaling.

Each instance of one of these intrinsics generates a stack map record
in the :ref:`stackmap-section`. The record includes an ID, allowing
the runtime to uniquely identify the stack map, and the offset within
the code from the beginning of the enclosing function.

'``llvm.experimental.stackmap``' Intrinsic
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Syntax:
"""""""

::

      declare void
        @llvm.experimental.stackmap(i64 <id>, i32 <numShadowBytes>, ...)

Overview:
"""""""""

The '``llvm.experimental.stackmap``' intrinsic records the location of
specified values in the stack map without generating any code.

Operands:
"""""""""

The first operand is an ID to be encoded within the stack map. The
second operand is the number of shadow bytes following the
intrinsic. The variable number of operands that follow are the ``live
values`` for which locations will be recorded in the stack map.

To use this intrinsic as a bare-bones stack map, with no code patching
support, the number of shadow bytes can be set to zero.

Semantics:
""""""""""

The stack map intrinsic generates no code in place, unless nops are
needed to cover its shadow (see below). However, its offset from
function entry is stored in the stack map. This is the relative
instruction address immediately following the instructions that
precede the stack map.

The stack map ID allows a runtime to locate the desired stack map
record. LLVM passes this ID through directly to the stack map
record without checking uniqueness.

LLVM guarantees a shadow of instructions following the stack map's
instruction offset during which neither the end of the basic block nor
another call to ``llvm.experimental.stackmap`` or
``llvm.experimental.patchpoint`` may occur. This allows the runtime to
patch the code at this point in response to an event triggered from
outside the code. The code for instructions following the stack map
may be emitted in the stack map's shadow, and these instructions may
be overwritten by destructive patching. Without shadow bytes, this
destructive patching could overwrite program text or data outside the
current function. We disallow overlapping stack map shadows so that
the runtime does not need to consider this corner case.

For example, a stack map with 8 byte shadow:

.. code-block:: llvm

  call void @runtime()
  call void (i64, i32, ...) @llvm.experimental.stackmap(i64 77, i32 8,
                                                        ptr %ptr)
  %val = load i64, ptr %ptr
  %add = add i64 %val, 3
  ret i64 %add

May require one byte of nop-padding:

.. code-block:: none

  0x00 callq _runtime
  0x05 nop                <--- stack map address
  0x06 movq (%rdi), %rax
  0x07 addq $3, %rax
  0x0a popq %rdx
  0x0b ret                <---- end of 8-byte shadow

Now, if the runtime needs to invalidate the compiled code, it may
patch 8 bytes of code at the stack map's address at follows:

.. code-block:: none

  0x00 callq _runtime
  0x05 movl  $0xffff, %rax <--- patched code at stack map address
  0x0a callq *%rax         <---- end of 8-byte shadow

This way, after the normal call to the runtime returns, the code will
execute a patched call to a special entry point that can rebuild a
stack frame from the values located by the stack map.

'``llvm.experimental.patchpoint.*``' Intrinsic
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Syntax:
"""""""

::

      declare void
        @llvm.experimental.patchpoint.void(i64 <id>, i32 <numBytes>,
                                           ptr <target>, i32 <numArgs>, ...)
      declare i64
        @llvm.experimental.patchpoint.i64(i64 <id>, i32 <numBytes>,
                                          ptr <target>, i32 <numArgs>, ...)

Overview:
"""""""""

The '``llvm.experimental.patchpoint.*``' intrinsics creates a function
call to the specified ``<target>`` and records the location of specified
values in the stack map.

Operands:
"""""""""

The first operand is an ID, the second operand is the number of bytes
reserved for the patchable region, the third operand is the target
address of a function (optionally null), and the fourth operand
specifies how many of the following variable operands are considered
function call arguments. The remaining variable number of operands are
the ``live values`` for which locations will be recorded in the stack
map.

Semantics:
""""""""""

The patch point intrinsic generates a stack map. It also emits a
function call to the address specified by ``<target>`` if the address
is not a constant null. The function call and its arguments are
lowered according to the calling convention specified at the
intrinsic's callsite. Variants of the intrinsic with non-void return
type also return a value according to calling convention.

On PowerPC, note that ``<target>`` must be the ABI function pointer for the
intended target of the indirect call. Specifically, when compiling for the
ELF V1 ABI, ``<target>`` is the function-descriptor address normally used as
the C/C++ function-pointer representation.

Requesting zero patch point arguments is valid. In this case, all
variable operands are handled just like
``llvm.experimental.stackmap.*``. The difference is that space will
still be reserved for patching, a call will be emitted, and a return
value is allowed.

The location of the arguments are not normally recorded in the stack
map because they are already fixed by the calling convention. The
remaining ``live values`` will have their location recorded, which
could be a register, stack location, or constant. A special calling
convention has been introduced for use with stack maps, anyregcc,
which forces the arguments to be loaded into registers but allows
those register to be dynamically allocated. These argument registers
will have their register locations recorded in the stack map in
addition to the remaining ``live values``.

The patch point also emits nops to cover at least ``<numBytes>`` of
instruction encoding space. Hence, the client must ensure that
``<numBytes>`` is enough to encode a call to the target address on the
supported targets. If the call target is constant null, then there is
no minimum requirement. A zero-byte null target patchpoint is
valid.

The runtime may patch the code emitted for the patch point, including
the call sequence and nops. However, the runtime may not assume
anything about the code LLVM emits within the reserved space. Partial
patching is not allowed. The runtime must patch all reserved bytes,
padding with nops if necessary.

This example shows a patch point reserving 15 bytes, with one argument
in $rdi, and a return value in $rax per native calling convention:

.. code-block:: llvm

  %target = inttoptr i64 -281474976710654 to ptr
  %val = call i64 (i64, i32, ...)
           @llvm.experimental.patchpoint.i64(i64 78, i32 15,
                                             ptr %target, i32 1, ptr %ptr)
  %add = add i64 %val, 3
  ret i64 %add

May generate:

.. code-block:: none

  0x00 movabsq $0xffff000000000002, %r11 <--- patch point address
  0x0a callq   *%r11
  0x0d nop
  0x0e nop                               <--- end of reserved 15-bytes
  0x0f addq    $0x3, %rax
  0x10 movl    %rax, 8(%rsp)

Note that no stack map locations will be recorded. If the patched code
sequence does not need arguments fixed to specific calling convention
registers, then the ``anyregcc`` convention may be used:

.. code-block:: none

  %val = call anyregcc @llvm.experimental.patchpoint(i64 78, i32 15,
                                                     ptr %target, i32 1,
                                                     ptr %ptr)

The stack map now indicates the location of the %ptr argument and
return value:

.. code-block:: none

  Stack Map: ID=78, Loc0=%r9 Loc1=%r8

The patch code sequence may now use the argument that happened to be
allocated in %r8 and return a value allocated in %r9:

.. code-block:: none

  0x00 movslq 4(%r8) %r9              <--- patched code at patch point address
  0x03 nop
  ...
  0x0e nop                            <--- end of reserved 15-bytes
  0x0f addq    $0x3, %r9
  0x10 movl    %r9, 8(%rsp)

.. _stackmap-format:

Stack Map Format
================

The existence of a stack map or patch point intrinsic within an LLVM
Module forces code emission to create a :ref:`stackmap-section`. The
format of this section follows:

.. code-block:: none

  Header {
    uint8  : Stack Map Version (current version is 3)
    uint8  : Reserved (expected to be 0)
    uint16 : Reserved (expected to be 0)
  }
  uint32 : NumFunctions
  uint32 : NumConstants
  uint32 : NumRecords
  StkSizeRecord[NumFunctions] {
    uint64 : Function Address
    uint64 : Stack Size (or UINT64_MAX if not statically known)
    uint64 : Record Count
  }
  Constants[NumConstants] {
    uint64 : LargeConstant
  }
  StkMapRecord[NumRecords] {
    uint64 : PatchPoint ID
    uint32 : Instruction Offset
    uint16 : Reserved (record flags)
    uint16 : NumLocations
    Location[NumLocations] {
      uint8  : Register | Direct | Indirect | Constant | ConstantIndex
      uint8  : Reserved (expected to be 0)
      uint16 : Location Size
      uint16 : Dwarf RegNum
      uint16 : Reserved (expected to be 0)
      int32  : Offset or SmallConstant
    }
    uint32 : Padding (only if required to align to 8 byte)
    uint16 : Padding
    uint16 : NumLiveOuts
    LiveOuts[NumLiveOuts]
      uint16 : Dwarf RegNum
      uint8  : Reserved
      uint8  : Size in Bytes
    }
    uint32 : Padding (only if required to align to 8 byte)
  }

The first byte of each location encodes a type that indicates how to
interpret the ``RegNum`` and ``Offset`` fields as follows:

======== ========== =================== ===========================
Encoding Type       Value               Description
-------- ---------- ------------------- ---------------------------
0x1      Register   Reg                 Value in a register
0x2      Direct     Reg + Offset        Frame index value
0x3      Indirect   [Reg + Offset]      Spilled value
0x4      Constant   Offset              Small constant
0x5      ConstIndex Constants[Offset]   Large constant
======== ========== =================== ===========================

In the common case, a value is available in a register, and the
``Offset`` field will be zero. Values spilled to the stack are encoded
as ``Indirect`` locations. The runtime must load those values from a
stack address, typically in the form ``[BP + Offset]``. If an
``alloca`` value is passed directly to a stack map intrinsic, then
LLVM may fold the frame index into the stack map as an optimization to
avoid allocating a register or stack slot. These frame indices will be
encoded as ``Direct`` locations in the form ``BP + Offset``. LLVM may
also optimize constants by emitting them directly in the stack map,
either in the ``Offset`` of a ``Constant`` location or in the constant
pool, referred to by ``ConstantIndex`` locations.

At each callsite, a "liveout" register list is also recorded. These
are the registers that are live across the stackmap and therefore must
be saved by the runtime. This is an important optimization when the
patchpoint intrinsic is used with a calling convention that by default
preserves most registers as callee-save.

Each entry in the liveout register list contains a DWARF register
number and size in bytes. The stackmap format deliberately omits
specific subregister information. Instead the runtime must interpret
this information conservatively. For example, if the stackmap reports
one byte at ``%rax``, then the value may be in either ``%al`` or
``%ah``. It doesn't matter in practice, because the runtime will
simply save ``%rax``. However, if the stackmap reports 16 bytes at
``%ymm0``, then the runtime can safely optimize by saving only
``%xmm0``.

The stack map format is a contract between an LLVM SVN revision and
the runtime. It is currently experimental and may change in the short
term, but minimizing the need to update the runtime is
important. Consequently, the stack map design is motivated by
simplicity and extensibility. Compactness of the representation is
secondary because the runtime is expected to parse the data
immediately after compiling a module and encode the information in its
own format. Since the runtime controls the allocation of sections, it
can reuse the same stack map space for multiple modules.

Stackmap support is currently only implemented for 64-bit
platforms. However, a 32-bit implementation should be able to use the
same format with an insignificant amount of wasted space.

.. _stackmap-section:

Stack Map Section
^^^^^^^^^^^^^^^^^

A JIT compiler can easily access this section by providing its own
memory manager via the LLVM C API
``LLVMCreateSimpleMCJITMemoryManager()``. When creating the memory
manager, the JIT provides a callback:
``LLVMMemoryManagerAllocateDataSectionCallback()``. When LLVM creates
this section, it invokes the callback and passes the section name. The
JIT can record the in-memory address of the section at this time and
later parse it to recover the stack map data.

For MachO (e.g. on Darwin), the stack map section name is
"__llvm_stackmaps". The segment name is "__LLVM_STACKMAPS".

For ELF (e.g. on Linux), the stack map section name is
".llvm_stackmaps".  The segment name is "__LLVM_STACKMAPS".

Stack Map Usage
===============

The stack map support described in this document can be used to
precisely determine the location of values at a specific position in
the code. LLVM does not maintain any mapping between those values and
any higher-level entity. The runtime must be able to interpret the
stack map record given only the ID, offset, and the order of the
locations, records, and functions, which LLVM preserves.

Note that this is quite different from the goal of debug information,
which is a best-effort attempt to track the location of named
variables at every instruction.

An important motivation for this design is to allow a runtime to
commandeer a stack frame when execution reaches an instruction address
associated with a stack map. The runtime must be able to rebuild a
stack frame and resume program execution using the information
provided by the stack map. For example, execution may resume in an
interpreter or a recompiled version of the same function.

This usage restricts LLVM optimization. Clearly, LLVM must not move
stores across a stack map. However, loads must also be handled
conservatively. If the load may trigger an exception, hoisting it
above a stack map could be invalid. For example, the runtime may
determine that a load is safe to execute without a type check given
the current state of the type system. If the type system changes while
some activation of the load's function exists on the stack, the load
becomes unsafe. The runtime can prevent subsequent execution of that
load by immediately patching any stack map location that lies between
the current call site and the load (typically, the runtime would
simply patch all stack map locations to invalidate the function). If
the compiler had hoisted the load above the stack map, then the
program could crash before the runtime could take back control.

To enforce these semantics, stackmap and patchpoint intrinsics are
considered to potentially read and write all memory. This may limit
optimization more than some clients desire. This limitation may be
avoided by marking the call site as "readonly". In the future we may
also allow meta-data to be added to the intrinsic call to express
aliasing, thereby allowing optimizations to hoist certain loads above
stack maps.

Direct Stack Map Entries
^^^^^^^^^^^^^^^^^^^^^^^^

As shown in :ref:`stackmap-section`, a Direct stack map location
records the address of frame index. This address is itself the value
that the runtime requested. This differs from Indirect locations,
which refer to a stack locations from which the requested values must
be loaded. Direct locations can communicate the address if an alloca,
while Indirect locations handle register spills.

For example:

.. code-block:: none

  entry:
    %a = alloca i64...
    llvm.experimental.stackmap(i64 <ID>, i32 <shadowBytes>, ptr %a)

The runtime can determine this alloca's relative location on the
stack immediately after compilation, or at any time thereafter. This
differs from Register and Indirect locations, because the runtime can
only read the values in those locations when execution reaches the
instruction address of the stack map.

This functionality requires LLVM to treat entry-block allocas
specially when they are directly consumed by an intrinsics. (This is
the same requirement imposed by the llvm.gcroot intrinsic.) LLVM
transformations must not substitute the alloca with any intervening
value. This can be verified by the runtime simply by checking that the
stack map's location is a Direct location type.


Supported Architectures
=======================

Support for StackMap generation and the related intrinsics requires
some code for each backend.  Today, only a subset of LLVM's backends
are supported.  The currently supported architectures are X86_64,
PowerPC, AArch64 and SystemZ.
