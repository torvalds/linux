=======================================================
Hardware-assisted AddressSanitizer Design Documentation
=======================================================

This page is a design document for
**hardware-assisted AddressSanitizer** (or **HWASAN**)
a tool similar to :doc:`AddressSanitizer`,
but based on partial hardware assistance.


Introduction
============

:doc:`AddressSanitizer`
tags every 8 bytes of the application memory with a 1 byte tag (using *shadow memory*),
uses *redzones* to find buffer-overflows and
*quarantine* to find use-after-free.
The redzones, the quarantine, and, to a less extent, the shadow, are the
sources of AddressSanitizer's memory overhead.
See the `AddressSanitizer paper`_ for details.

AArch64 has `Address Tagging`_ (or top-byte-ignore, TBI), a hardware feature that allows
software to use the 8 most significant bits of a 64-bit pointer as
a tag. HWASAN uses `Address Tagging`_
to implement a memory safety tool, similar to :doc:`AddressSanitizer`,
but with smaller memory overhead and slightly different (mostly better)
accuracy guarantees.

Intel's `Linear Address Masking`_ (LAM) also provides address tagging for
x86_64, though it is not widely available in hardware yet.  For x86_64, HWASAN
has a limited implementation using page aliasing instead.

Algorithm
=========
* Every heap/stack/global memory object is forcibly aligned by `TG` bytes
  (`TG` is e.g. 16 or 64). We call `TG` the **tagging granularity**.
* For every such object a random `TS`-bit tag `T` is chosen (`TS`, or tag size, is e.g. 4 or 8)
* The pointer to the object is tagged with `T`.
* The memory for the object is also tagged with `T` (using a `TG=>1` shadow memory)
* Every load and store is instrumented to read the memory tag and compare it
  with the pointer tag, exception is raised on tag mismatch.

For a more detailed discussion of this approach see https://arxiv.org/pdf/1802.09517.pdf

Short granules
--------------

A short granule is a granule of size between 1 and `TG-1` bytes. The size
of a short granule is stored at the location in shadow memory where the
granule's tag is normally stored, while the granule's actual tag is stored
in the last byte of the granule. This means that in order to verify that a
pointer tag matches a memory tag, HWASAN must check for two possibilities:

* the pointer tag is equal to the memory tag in shadow memory, or
* the shadow memory tag is actually a short granule size, the value being loaded
  is in bounds of the granule and the pointer tag is equal to the last byte of
  the granule.

Pointer tags between 1 to `TG-1` are possible and are as likely as any other
tag. This means that these tags in memory have two interpretations: the full
tag interpretation (where the pointer tag is between 1 and `TG-1` and the
last byte of the granule is ordinary data) and the short tag interpretation
(where the pointer tag is stored in the granule).

When HWASAN detects an error near a memory tag between 1 and `TG-1`, it
will show both the memory tag and the last byte of the granule. Currently,
it is up to the user to disambiguate the two possibilities.

Instrumentation
===============

Memory Accesses
---------------
In the majority of cases, memory accesses are prefixed with a call to
an outlined instruction sequence that verifies the tags. The code size
and performance overhead of the call is reduced by using a custom calling
convention that

* preserves most registers, and
* is specialized to the register containing the address, and the type and
  size of the memory access.

Currently, the following sequence is used:

.. code-block:: none

  // int foo(int *a) { return *a; }
  // clang -O2 --target=aarch64-linux-android30 -fsanitize=hwaddress -S -o - load.c
  [...]
  foo:
        stp     x30, x20, [sp, #-16]!
        adrp    x20, :got:__hwasan_shadow               // load shadow address from GOT into x20
        ldr     x20, [x20, :got_lo12:__hwasan_shadow]
        bl      __hwasan_check_x0_2_short_v2            // call outlined tag check
                                                        // (arguments: x0 = address, x20 = shadow base;
                                                        // "2" encodes the access type and size)
        ldr     w0, [x0]                                // inline load
        ldp     x30, x20, [sp], #16
        ret

  [...]
  __hwasan_check_x0_2_short_v2:
        sbfx    x16, x0, #4, #52                        // shadow offset
        ldrb    w16, [x20, x16]                         // load shadow tag
        cmp     x16, x0, lsr #56                        // extract address tag, compare with shadow tag
        b.ne    .Ltmp0                                  // jump to short tag handler on mismatch
  .Ltmp1:
        ret
  .Ltmp0:
        cmp     w16, #15                                // is this a short tag?
        b.hi    .Ltmp2                                  // if not, error
        and     x17, x0, #0xf                           // find the address's position in the short granule
        add     x17, x17, #3                            // adjust to the position of the last byte loaded
        cmp     w16, w17                                // check that position is in bounds
        b.ls    .Ltmp2                                  // if not, error
        orr     x16, x0, #0xf                           // compute address of last byte of granule
        ldrb    w16, [x16]                              // load tag from it
        cmp     x16, x0, lsr #56                        // compare with pointer tag
        b.eq    .Ltmp1                                  // if matches, continue
  .Ltmp2:
        stp     x0, x1, [sp, #-256]!                    // save original x0, x1 on stack (they will be overwritten)
        stp     x29, x30, [sp, #232]                    // create frame record
        mov     x1, #2                                  // set x1 to a constant indicating the type of failure
        adrp    x16, :got:__hwasan_tag_mismatch_v2      // call runtime function to save remaining registers and report error
        ldr     x16, [x16, :got_lo12:__hwasan_tag_mismatch_v2] // (load address from GOT to avoid potential register clobbers in delay load handler)
        br      x16

Heap
----

Tagging the heap memory/pointers is done by `malloc`.
This can be based on any malloc that forces all objects to be TG-aligned.
`free` tags the memory with a different tag.

Stack
-----

Stack frames are instrumented by aligning all non-promotable allocas
by `TG` and tagging stack memory in function prologue and epilogue.

Tags for different allocas in one function are **not** generated
independently; doing that in a function with `M` allocas would require
maintaining `M` live stack pointers, significantly increasing register
pressure. Instead we generate a single base tag value in the prologue,
and build the tag for alloca number `M` as `ReTag(BaseTag, M)`, where
ReTag can be as simple as exclusive-or with constant `M`.

Stack instrumentation is expected to be a major source of overhead,
but could be optional.

Globals
-------

Most globals in HWASAN instrumented code are tagged. This is accomplished
using the following mechanisms:

  * The address of each global has a static tag associated with it. The first
    defined global in a translation unit has a pseudorandom tag associated
    with it, based on the hash of the file path. Subsequent global tags are
    incremental from the previously-assigned tag.

  * The global's tag is added to its symbol address in the object file's symbol
    table. This causes the global's address to be tagged when its address is
    taken.

  * When the address of a global is taken directly (i.e. not via the GOT), a special
    instruction sequence needs to be used to add the tag to the address,
    because the tag would otherwise take the address outside of the small code
    model (4GB on AArch64). No changes are required when the address is taken
    via the GOT because the address stored in the GOT will contain the tag.

  * An associated ``hwasan_globals`` section is emitted for each tagged global,
    which indicates the address of the global, its size and its tag.  These
    sections are concatenated by the linker into a single ``hwasan_globals``
    section that is enumerated by the runtime (via an ELF note) when a binary
    is loaded and the memory is tagged accordingly.

A complete example is given below:

.. code-block:: none

  // int x = 1; int *f() { return &x; }
  // clang -O2 --target=aarch64-linux-android30 -fsanitize=hwaddress -S -o - global.c

  [...]
  f:
        adrp    x0, :pg_hi21_nc:x            // set bits 12-63 to upper bits of untagged address
        movk    x0, #:prel_g3:x+0x100000000  // set bits 48-63 to tag
        add     x0, x0, :lo12:x              // set bits 0-11 to lower bits of address
        ret

  [...]
        .data
  .Lx.hwasan:
        .word   1

        .globl  x
        .set x, .Lx.hwasan+0x2d00000000000000

  [...]
        .section        .note.hwasan.globals,"aG",@note,hwasan.module_ctor,comdat
  .Lhwasan.note:
        .word   8                            // namesz
        .word   8                            // descsz
        .word   3                            // NT_LLVM_HWASAN_GLOBALS
        .asciz  "LLVM\000\000\000"
        .word   __start_hwasan_globals-.Lhwasan.note
        .word   __stop_hwasan_globals-.Lhwasan.note

  [...]
        .section        hwasan_globals,"ao",@progbits,.Lx.hwasan,unique,2
  .Lx.hwasan.descriptor:
        .word   .Lx.hwasan-.Lx.hwasan.descriptor
        .word   0x2d000004                   // tag = 0x2d, size = 4

Error reporting
---------------

Errors are generated by the `HLT` instruction and are handled by a signal handler.

Attribute
---------

HWASAN uses its own LLVM IR Attribute `sanitize_hwaddress` and a matching
C function attribute. An alternative would be to re-use ASAN's attribute
`sanitize_address`. The reasons to use a separate attribute are:

  * Users may need to disable ASAN but not HWASAN, or vise versa,
    because the tools have different trade-offs and compatibility issues.
  * LLVM (ideally) does not use flags to decide which pass is being used,
    ASAN or HWASAN are being applied, based on the function attributes.

This does mean that users of HWASAN may need to add the new attribute
to the code that already uses the old attribute.


Comparison with AddressSanitizer
================================

HWASAN:
  * Is less portable than :doc:`AddressSanitizer`
    as it relies on hardware `Address Tagging`_ (AArch64).
    Address Tagging can be emulated with compiler instrumentation,
    but it will require the instrumentation to remove the tags before
    any load or store, which is infeasible in any realistic environment
    that contains non-instrumented code.
  * May have compatibility problems if the target code uses higher
    pointer bits for other purposes.
  * May require changes in the OS kernels (e.g. Linux seems to dislike
    tagged pointers passed from address space:
    https://www.kernel.org/doc/Documentation/arm64/tagged-pointers.txt).
  * **Does not require redzones to detect buffer overflows**,
    but the buffer overflow detection is probabilistic, with roughly
    `1/(2**TS)` chance of missing a bug (6.25% or 0.39% with 4 and 8-bit TS
    respectively).
  * **Does not require quarantine to detect heap-use-after-free,
    or stack-use-after-return**.
    The detection is similarly probabilistic.

The memory overhead of HWASAN is expected to be much smaller
than that of AddressSanitizer:
`1/TG` extra memory for the shadow
and some overhead due to `TG`-aligning all objects.

Supported architectures
=======================
HWASAN relies on `Address Tagging`_ which is only available on AArch64.
For other 64-bit architectures it is possible to remove the address tags
before every load and store by compiler instrumentation, but this variant
will have limited deployability since not all of the code is
typically instrumented.

On x86_64, HWASAN utilizes page aliasing to place tags in userspace address
bits.  Currently only heap tagging is supported.  The page aliases rely on
shared memory, which will cause heap memory to be shared between processes if
the application calls ``fork()``.  Therefore x86_64 is really only safe for
applications that do not fork.

HWASAN does not currently support 32-bit architectures since they do not
support `Address Tagging`_ and the address space is too constrained to easily
implement page aliasing.


Related Work
============
* `SPARC ADI`_ implements a similar tool mostly in hardware.
* `Effective and Efficient Memory Protection Using Dynamic Tainting`_ discusses
  similar approaches ("lock & key").
* `Watchdog`_ discussed a heavier, but still somewhat similar
  "lock & key" approach.
* *TODO: add more "related work" links. Suggestions are welcome.*


.. _Watchdog: https://www.cis.upenn.edu/acg/papers/isca12_watchdog.pdf
.. _Effective and Efficient Memory Protection Using Dynamic Tainting: https://www.cc.gatech.edu/~orso/papers/clause.doudalis.orso.prvulovic.pdf
.. _SPARC ADI: https://lazytyped.blogspot.com/2017/09/getting-started-with-adi.html
.. _AddressSanitizer paper: https://www.usenix.org/system/files/conference/atc12/atc12-final39.pdf
.. _Address Tagging: http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0024a/ch12s05s01.html
.. _Linear Address Masking: https://software.intel.com/content/www/us/en/develop/download/intel-architecture-instruction-set-extensions-programming-reference.html
