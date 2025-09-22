===============
ShadowCallStack
===============

.. contents::
   :local:

Introduction
============

ShadowCallStack is an instrumentation pass, currently only implemented for
aarch64 and RISC-V, that protects programs against return address overwrites
(e.g. stack buffer overflows.) It works by saving a function's return address
to a separately allocated 'shadow call stack' in the function prolog in
non-leaf functions and loading the return address from the shadow call stack
in the function epilog. The return address is also stored on the regular stack
for compatibility with unwinders, but is otherwise unused.

The aarch64 implementation is considered production ready, and
an `implementation of the runtime`_ has been added to Android's libc
(bionic). An x86_64 implementation was evaluated using Chromium and was found
to have critical performance and security deficiencies--it was removed in
LLVM 9.0. Details on the x86_64 implementation can be found in the
`Clang 7.0.1 documentation`_.

.. _`implementation of the runtime`: https://android.googlesource.com/platform/bionic/+/808d176e7e0dd727c7f929622ec017f6e065c582/libc/bionic/pthread_create.cpp#128
.. _`Clang 7.0.1 documentation`: https://releases.llvm.org/7.0.1/tools/clang/docs/ShadowCallStack.html

Comparison
----------

To optimize for memory consumption and cache locality, the shadow call
stack stores only an array of return addresses. This is in contrast to other
schemes, like :doc:`SafeStack`, that mirror the entire stack and trade-off
consuming more memory for shorter function prologs and epilogs with fewer
memory accesses.

`Return Flow Guard`_ is a pure software implementation of shadow call stacks
on x86_64. Like the previous implementation of ShadowCallStack on x86_64, it is
inherently racy due to the architecture's use of the stack for calls and
returns.

Intel `Control-flow Enforcement Technology`_ (CET) is a proposed hardware
extension that would add native support to use a shadow stack to store/check
return addresses at call/return time. Being a hardware implementation, it
would not suffer from race conditions and would not incur the overhead of
function instrumentation, but it does require operating system support.

.. _`Return Flow Guard`: https://xlab.tencent.com/en/2016/11/02/return-flow-guard/
.. _`Control-flow Enforcement Technology`: https://software.intel.com/sites/default/files/managed/4d/2a/control-flow-enforcement-technology-preview.pdf

Compatibility
-------------

A runtime is not provided in compiler-rt so one must be provided by the
compiled application or the operating system. Integrating the runtime into
the operating system should be preferred since otherwise all thread creation
and destruction would need to be intercepted by the application.

The instrumentation makes use of the platform register ``x18`` on AArch64,
``x3`` (``gp``) on RISC-V with software shadow stack and ``ssp`` on RISC-V with
hardware shadow stack, which needs `Zicfiss`_ and ``-mno-forced-sw-shadow-stack``
(default option). Note that with ``Zicfiss``_ the RISC-V backend will default to
the hardware based shadow call stack. Users can force the RISC-V backend to
generate the software shadow call stack with ``Zicfiss``_ by passing
``-mforced-sw-shadow-stack``.
For simplicity we will refer to this as the ``SCSReg``. On some platforms,
``SCSReg`` is reserved, and on others, it is designated as a scratch register.
This generally means that any code that may run on the same thread as code
compiled with ShadowCallStack must either target one of the platforms whose ABI
reserves ``SCSReg`` (currently Android, Darwin, Fuchsia and Windows) or be
compiled with a flag to reserve that register (e.g., ``-ffixed-x18``). If
absolutely necessary, code compiled without reserving the register may be run on
the same thread as code that uses ShadowCallStack by saving the register value
temporarily on the stack (`example in Android`_) but this should be done with
care since it risks leaking the shadow call stack address.

.. _`Zicfiss`: https://github.com/riscv/riscv-cfi/blob/main/cfi_backward.adoc
.. _`example in Android`: https://android-review.googlesource.com/c/platform/frameworks/base/+/803717

Because it requires a dedicated register, the ShadowCallStack feature is
incompatible with any other feature that may use ``SCSReg``. However, there is
no inherent reason why ShadowCallStack needs to use a specific register; in
principle, a platform could choose to reserve and use another register for
ShadowCallStack, but this would be incompatible with the ABI standards
published in AAPCS64 and the RISC-V psABI.

Special unwind information is required on functions that are compiled
with ShadowCallStack and that may be unwound, i.e. functions compiled with
``-fexceptions`` (which is the default in C++). Some unwinders (such as the
libgcc 4.9 unwinder) do not understand this unwind info and will segfault
when encountering it. LLVM libunwind processes this unwind info correctly,
however. This means that if exceptions are used together with ShadowCallStack,
the program must use a compatible unwinder.

Security
========

ShadowCallStack is intended to be a stronger alternative to
``-fstack-protector``. It protects from non-linear overflows and arbitrary
memory writes to the return address slot.

The instrumentation makes use of the ``SCSReg`` register to reference the shadow
call stack, meaning that references to the shadow call stack do not have
to be stored in memory. This makes it possible to implement a runtime that
avoids exposing the address of the shadow call stack to attackers that can
read arbitrary memory. However, attackers could still try to exploit side
channels exposed by the operating system `[1]`_ `[2]`_ or processor `[3]`_
to discover the address of the shadow call stack.

.. _`[1]`: https://eyalitkin.wordpress.com/2017/09/01/cartography-lighting-up-the-shadows/
.. _`[2]`: https://www.blackhat.com/docs/eu-16/materials/eu-16-Goktas-Bypassing-Clangs-SafeStack.pdf
.. _`[3]`: https://www.vusec.net/projects/anc/

Unless care is taken when allocating the shadow call stack, it may be
possible for an attacker to guess its address using the addresses of
other allocations. Therefore, the address should be chosen to make this
difficult. One way to do this is to allocate a large guard region without
read/write permissions, randomly select a small region within it to be
used as the address of the shadow call stack and mark only that region as
read/write. This also mitigates somewhat against processor side channels.
The intent is that the Android runtime `will do this`_, but the platform will
first need to be `changed`_ to avoid using ``setrlimit(RLIMIT_AS)`` to limit
memory allocations in certain processes, as this also limits the number of
guard regions that can be allocated.

.. _`will do this`: https://android-review.googlesource.com/c/platform/bionic/+/891622
.. _`changed`: https://android-review.googlesource.com/c/platform/frameworks/av/+/837745

The runtime will need the address of the shadow call stack in order to
deallocate it when destroying the thread. If the entire program is compiled
with ``SCSReg`` reserved, this is trivial: the address can be derived from the
value stored in ``SCSReg`` (e.g. by masking out the lower bits). If a guard
region is used, the address of the start of the guard region could then be
stored at the start of the shadow call stack itself. But if it is possible
for code compiled without reserving ``SCSReg`` to run on a thread managed by the
runtime, which is the case on Android for example, the address must be stored
somewhere else instead. On Android we store the address of the start of the
guard region in TLS and deallocate the entire guard region including the
shadow call stack at thread exit. This is considered acceptable given that
the address of the start of the guard region is already somewhat guessable.

One way in which the address of the shadow call stack could leak is in the
``jmp_buf`` data structure used by ``setjmp`` and ``longjmp``. The Android
runtime `avoids this`_ by only storing the low bits of ``SCSReg`` in the
``jmp_buf``, which requires the address of the shadow call stack to be
aligned to its size.

.. _`avoids this`: https://android.googlesource.com/platform/bionic/+/808d176e7e0dd727c7f929622ec017f6e065c582/libc/arch-arm64/bionic/setjmp.S#49

The architecture's call and return instructions (``bl`` and ``ret``) operate on
a register rather than the stack, which means that leaf functions are generally
protected from return address overwrites even without ShadowCallStack.

Usage
=====

To enable ShadowCallStack, just pass the ``-fsanitize=shadow-call-stack`` flag
to both compile and link command lines. On aarch64, you also need to pass
``-ffixed-x18`` unless your target already reserves ``x18``. No additional flags
need to be passed on RISC-V because the software based shadow stack uses
``x3`` (``gp``), which is always reserved, and the hardware based shadow call
stack uses a dedicated register, ``ssp``.
However, it is important to disable GP relaxation in the linker when using the
software based shadow call stack on RISC-V. This can be done with the
``--no-relax-gp`` flag in GNU ld, and is off by default in LLD.

Low-level API
-------------

``__has_feature(shadow_call_stack)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In some cases one may need to execute different code depending on whether
ShadowCallStack is enabled. The macro ``__has_feature(shadow_call_stack)`` can
be used for this purpose.

.. code-block:: c

    #if defined(__has_feature)
    #  if __has_feature(shadow_call_stack)
    // code that builds only under ShadowCallStack
    #  endif
    #endif

``__attribute__((no_sanitize("shadow-call-stack")))``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``__attribute__((no_sanitize("shadow-call-stack")))`` on a function
declaration to specify that the shadow call stack instrumentation should not be
applied to that function, even if enabled globally.

Example
=======

The following example code:

.. code-block:: c++

    int foo() {
      return bar() + 1;
    }

Generates the following aarch64 assembly when compiled with ``-O2``:

.. code-block:: none

    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    bl      bar
    add     w0, w0, #1
    ldp     x29, x30, [sp], #16
    ret

Adding ``-fsanitize=shadow-call-stack`` would output the following assembly:

.. code-block:: none

    str     x30, [x18], #8
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    bl      bar
    add     w0, w0, #1
    ldp     x29, x30, [sp], #16
    ldr     x30, [x18, #-8]!
    ret
