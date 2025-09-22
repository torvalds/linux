===============
Opaque Pointers
===============

The Opaque Pointer Type
=======================

Traditionally, LLVM IR pointer types have contained a pointee type. For example,
``i32*`` is a pointer that points to an ``i32`` somewhere in memory. However,
due to a lack of pointee type semantics and various issues with having pointee
types, there is a desire to remove pointee types from pointers.

The opaque pointer type project aims to replace all pointer types containing
pointee types in LLVM with an opaque pointer type. The new pointer type is
represented textually as ``ptr``.

Some instructions still need to know what type to treat the memory pointed to by
the pointer as. For example, a load needs to know how many bytes to load from
memory and what type to treat the resulting value as. In these cases,
instructions themselves contain a type argument. For example the load
instruction from older versions of LLVM

.. code-block:: llvm

  load i64* %p

becomes

.. code-block:: llvm

  load i64, ptr %p

Address spaces are still used to distinguish between different kinds of pointers
where the distinction is relevant for lowering (e.g. data vs function pointers
have different sizes on some architectures). Opaque pointers are not changing
anything related to address spaces and lowering. For more information, see
`DataLayout <LangRef.html#langref-datalayout>`_. Opaque pointers in non-default
address space are spelled ``ptr addrspace(N)``.

This was proposed all the way back in
`2015 <https://lists.llvm.org/pipermail/llvm-dev/2015-February/081822.html>`_.

Issues with explicit pointee types
==================================

LLVM IR pointers can be cast back and forth between pointers with different
pointee types. The pointee type does not necessarily represent the actual
underlying type in memory. In other words, the pointee type carries no real
semantics.

Historically LLVM was some sort of type-safe subset of C. Having pointee types
provided an extra layer of checks to make sure that the Clang frontend matched
its frontend values/operations with the corresponding LLVM IR. However, as other
languages like C++ adopted LLVM, the community realized that pointee types were
more of a hindrance for LLVM development and that the extra type checking with
some frontends wasn't worth it.

LLVM's type system was `originally designed
<https://llvm.org/pubs/2003-05-01-GCCSummit2003.html>`_ to support high-level
optimization. However, years of LLVM implementation experience have demonstrated
that the pointee type system design does not effectively support
optimization. Memory optimization algorithms, such as SROA, GVN, and AA,
generally need to look through LLVM's struct types and reason about the
underlying memory offsets. The community realized that pointee types hinder LLVM
development, rather than helping it. Some of the initially proposed high-level
optimizations have evolved into `TBAA
<https://llvm.org/docs/LangRef.html#tbaa-metadata>`_ due to limitations with
representing higher-level language information directly via SSA values.

Pointee types provide some value to frontends because the IR verifier uses types
to detect straightforward type confusion bugs. However, frontends also have to
deal with the complexity of inserting bitcasts everywhere that they might be
required. The community consensus is that the costs of pointee types
outweight the benefits, and that they should be removed.

Many operations do not actually care about the underlying type. These
operations, typically intrinsics, usually end up taking an arbitrary pointer
type ``i8*`` and sometimes a size. This causes lots of redundant no-op bitcasts
in the IR to and from a pointer with a different pointee type.

No-op bitcasts take up memory/disk space and also take up compile time to look
through. However, perhaps the biggest issue is the code complexity required to
deal with bitcasts. When looking up through def-use chains for pointers it's
easy to forget to call `Value::stripPointerCasts()` to find the true underlying
pointer obfuscated by bitcasts. And when looking down through def-use chains
passes need to iterate through bitcasts to handle uses. Removing no-op pointer
bitcasts prevents a category of missed optimizations and makes writing LLVM
passes a little bit easier.

Fewer no-op pointer bitcasts also reduces the chances of incorrect bitcasts in
regards to address spaces. People maintaining backends that care a lot about
address spaces have complained that frontends like Clang often incorrectly
bitcast pointers, losing address space information.

An analogous transition that happened earlier in LLVM is integer signedness.
Currently there is no distinction between signed and unsigned integer types, but
rather each integer operation (e.g. add) contains flags to signal how to treat
the integer. Previously LLVM IR distinguished between unsigned and signed
integer types and ran into similar issues of no-op casts. The transition from
manifesting signedness in types to instructions happened early on in LLVM's
timeline to make LLVM easier to work with.

Opaque Pointers Mode
====================

During the transition phase, LLVM can be used in two modes: In typed pointer
mode all pointer types have a pointee type and opaque pointers cannot be used.
In opaque pointers mode (the default), all pointers are opaque. The opaque
pointer mode can be disabled using ``-opaque-pointers=0`` in
LLVM tools like ``opt``, or ``-Xclang -no-opaque-pointers`` in clang.
Additionally, opaque pointer mode is automatically disabled for IR and bitcode
files that explicitly mention ``i8*`` style typed pointers.

In opaque pointer mode, all typed pointers used in IR, bitcode, or created
using ``PointerType::get()`` and similar APIs are automatically converted into
opaque pointers. This simplifies migration and allows testing existing IR with
opaque pointers.

.. code-block:: llvm

   define i8* @test(i8* %p) {
     %p2 = getelementptr i8, i8* %p, i64 1
     ret i8* %p2
   }

   ; Is automatically converted into the following if -opaque-pointers
   ; is enabled:

   define ptr @test(ptr %p) {
     %p2 = getelementptr i8, ptr %p, i64 1
     ret ptr %p2
   }

Migration Instructions
======================

In order to support opaque pointers, two types of changes tend to be necessary.
The first is the removal of all calls to ``PointerType::getElementType()`` and
``Type::getPointerElementType()``.

In the LLVM middle-end and backend, this is usually accomplished by inspecting
the type of relevant operations instead. For example, memory access related
analyses and optimizations should use the types encoded in the load and store
instructions instead of querying the pointer type.

Here are some common ways to avoid pointer element type accesses:

* For loads, use ``getType()``.
* For stores, use ``getValueOperand()->getType()``.
* Use ``getLoadStoreType()`` to handle both of the above in one call.
* For getelementptr instructions, use ``getSourceElementType()``.
* For calls, use ``getFunctionType()``.
* For allocas, use ``getAllocatedType()``.
* For globals, use ``getValueType()``.
* For consistency assertions, use
  ``PointerType::isOpaqueOrPointeeTypeEquals()``.
* To create a pointer type in a different address space, use
  ``PointerType::getWithSamePointeeType()``.
* To check that two pointers have the same element type, use
  ``PointerType::hasSameElementTypeAs()``.
* While it is preferred to write code in a way that accepts both typed and
  opaque pointers, ``Type::isOpaquePointerTy()`` and
  ``PointerType::isOpaque()`` can be used to handle opaque pointers specially.
  ``PointerType::getNonOpaquePointerElementType()`` can be used as a marker in
  code-paths where opaque pointers have been explicitly excluded.
* To get the type of a byval argument, use ``getParamByValType()``. Similar
  method exists for other ABI-affecting attributes that need to know the
  element type, such as byref, sret, inalloca and preallocated.
* Some intrinsics require an ``elementtype`` attribute, which can be retrieved
  using ``getParamElementType()``. This attribute is required in cases where
  the intrinsic does not naturally encode a needed element type. This is also
  used for inline assembly.

Note that some of the methods mentioned above only exist to support both typed
and opaque pointers at the same time, and will be dropped once the migration
has completed. For example, ``isOpaqueOrPointeeTypeEquals()`` becomes
meaningless once all pointers are opaque.

While direct usage of pointer element types is immediately apparent in code,
there is a more subtle issue that opaque pointers need to contend with: A lot
of code assumes that pointer equality also implies that the used load/store
type or GEP source element type is the same. Consider the following examples
with typed and opaque pointers:

.. code-block:: llvm

    define i32 @test(i32* %p) {
      store i32 0, i32* %p
      %bc = bitcast i32* %p to i64*
      %v = load i64, i64* %bc
      ret i64 %v
    }

    define i32 @test(ptr %p) {
      store i32 0, ptr %p
      %v = load i64, ptr %p
      ret i64 %v
    }

Without opaque pointers, a check that the pointer operand of the load and
store are the same also ensures that the accessed type is the same. Using a
different type requires a bitcast, which will result in distinct pointer
operands.

With opaque pointers, the bitcast is not present, and this check is no longer
sufficient. In the above example, it could result in store to load forwarding
of an incorrect type. Code making such assumptions needs to be adjusted to
check the accessed type explicitly:
``LI->getType() == SI->getValueOperand()->getType()``.

Frontends
---------

Frontends need to be adjusted to track pointee types independently of LLVM,
insofar as they are necessary for lowering. For example, clang now tracks the
pointee type in the ``Address`` structure.

Frontends using the C API through an FFI interface should be aware that a
number of C API functions are deprecated and will be removed as part of the
opaque pointer transition::

    LLVMBuildLoad -> LLVMBuildLoad2
    LLVMBuildCall -> LLVMBuildCall2
    LLVMBuildInvoke -> LLVMBuildInvoke2
    LLVMBuildGEP -> LLVMBuildGEP2
    LLVMBuildInBoundsGEP -> LLVMBuildInBoundsGEP2
    LLVMBuildStructGEP -> LLVMBuildStructGEP2
    LLVMBuildPtrDiff -> LLVMBuildPtrDiff2
    LLVMConstGEP -> LLVMConstGEP2
    LLVMConstInBoundsGEP -> LLVMConstInBoundsGEP2
    LLVMAddAlias -> LLVMAddAlias2

Additionally, it will no longer be possible to call ``LLVMGetElementType()``
on a pointer type.

It is possible to control whether opaque pointers are used (if you want to
override the default) using ``LLVMContext::setOpaquePointers``.

Temporarily disabling opaque pointers
=====================================

In LLVM 15, opaque pointers are enabled by default, but it it still possible to
use typed pointers using a number of opt-in flags.

For users of the clang driver interface, it is possible to temporarily restore
the old default using the ``-DCLANG_ENABLE_OPAQUE_POINTERS=OFF`` cmake option,
or by passing ``-Xclang -no-opaque-pointers`` to a single clang invocation.

For users of the clang cc1 interface, ``-no-opaque-pointers`` can be passed.
Note that the ``CLANG_ENABLE_OPAQUE_POINTERS`` cmake option has no effect on
the cc1 interface.

Usage for LTO can be disabled by passing ``-Wl,-plugin-opt=no-opaque-pointers``
to the clang driver.

For users of LLVM as a library, opaque pointers can be disabled by calling
``setOpaquePointers(false)`` on the ``LLVMContext``.

For users of LLVM tools like opt, opaque pointers can be disabled by passing
``-opaque-pointers=0``.

Version Support
===============

**LLVM 14:** Supports all necessary APIs for migrating to opaque pointers and deprecates/removes incompatible APIs. However, using opaque pointers in the optimization pipeline is **not** fully supported. This release can be used to make out-of-tree code compatible with opaque pointers, but opaque pointers should **not** be enabled in production.

**LLVM 15:** Opaque pointers are enabled by default. Typed pointers are still
supported.

**LLVM 16:** Opaque pointers are enabled by default. Typed pointers are
supported on a best-effort basis only and not tested.

**LLVM 17:** Only opaque pointers are supported. Typed pointers are not
supported.

Transition State
================

As of July 2023:

Typed pointers are **not** supported on the ``main`` branch.

The following typed pointer functionality has been removed:

* The ``CLANG_ENABLE_OPAQUE_POINTERS`` cmake flag is no longer supported.
* The ``-no-opaque-pointers`` cc1 clang flag is no longer supported.
* The ``-opaque-pointers`` opt flag is no longer supported.
* The ``-plugin-opt=no-opaque-pointers`` LTO flag is no longer supported.
* C APIs that do not support opaque pointers (like ``LLVMBuildLoad``) are no
  longer supported.

The following typed pointer functionality is still to be removed:

* Various APIs that are no longer relevant with opaque pointers.
