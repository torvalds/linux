.. _amdgpu-dwarf-extensions-for-heterogeneous-debugging:

********************************************
DWARF Extensions For Heterogeneous Debugging
********************************************

.. contents::
   :local:

.. warning::

   This document describes **provisional extensions** to DWARF Version 5
   [:ref:`DWARF <amdgpu-dwarf-DWARF>`] to support heterogeneous debugging. It is
   not currently fully implemented and is subject to change.

.. _amdgpu-dwarf-introduction:

1. Introduction
===============

AMD [:ref:`AMD <amdgpu-dwarf-AMD>`] has been working on supporting heterogeneous
computing. A heterogeneous computing program can be written in a high level
language such as C++ or Fortran with OpenMP pragmas, OpenCL, or HIP (a portable
C++ programming environment for heterogeneous computing [:ref:`HIP
<amdgpu-dwarf-HIP>`]). A heterogeneous compiler and runtime allows a program to
execute on multiple devices within the same native process. Devices could
include CPUs, GPUs, DSPs, FPGAs, or other special purpose accelerators.
Currently HIP programs execute on systems with CPUs and GPUs.

The AMD [:ref:`AMD <amdgpu-dwarf-AMD>`] ROCm platform [:ref:`AMD-ROCm
<amdgpu-dwarf-AMD-ROCm>`] is an implementation of the industry standard for
heterogeneous computing devices defined by the Heterogeneous System Architecture
(HSA) Foundation [:ref:`HSA <amdgpu-dwarf-HSA>`]. It is open sourced and
includes contributions to open source projects such as LLVM [:ref:`LLVM
<amdgpu-dwarf-LLVM>`] for compilation and GDB for debugging [:ref:`GDB
<amdgpu-dwarf-GDB>`].

The LLVM compiler has upstream support for commercially available AMD GPU
hardware (AMDGPU) [:ref:`AMDGPU-LLVM <amdgpu-dwarf-AMDGPU-LLVM>`]. The open
source ROCgdb [:ref:`AMD-ROCgdb <amdgpu-dwarf-AMD-ROCgdb>`] GDB based debugger
also has support for AMDGPU which is being upstreamed. Support for AMDGPU is
also being added by third parties to the GCC [:ref:`GCC <amdgpu-dwarf-GCC>`]
compiler and the Perforce TotalView HPC Debugger [:ref:`Perforce-TotalView
<amdgpu-dwarf-Perforce-TotalView>`].

To support debugging heterogeneous programs several features that are not
provided by current DWARF Version 5 [:ref:`DWARF <amdgpu-dwarf-DWARF>`] have
been identified. The :ref:`amdgpu-dwarf-extensions` section gives an overview of
the extensions devised to address the missing features. The extensions seek to
be general in nature and backwards compatible with DWARF Version 5. Their goal
is to be applicable to meeting the needs of any heterogeneous system and not be
vendor or architecture specific. That is followed by appendix
:ref:`amdgpu-dwarf-changes-relative-to-dwarf-version-5` which contains the
textual changes for the extensions relative to the DWARF Version 5 standard.
There are a number of notes included that raise open questions, or provide
alternative approaches that may be worth considering. Then appendix
:ref:`amdgpu-dwarf-further-examples` links to the AMD GPU specific usage of the
extensions that includes an example. Finally, appendix
:ref:`amdgpu-dwarf-references` provides references to further information.

.. _amdgpu-dwarf-extensions:

2. Extensions
=============

The extensions continue to evolve through collaboration with many individuals and
active prototyping within the GDB debugger and LLVM compiler. Input has also
been very much appreciated from the developers working on the Perforce TotalView
HPC Debugger and GCC compiler.

The inputs provided and insights gained so far have been incorporated into this
current version. The plan is to participate in upstreaming the work and
addressing any feedback. If there is general interest then some or all of these
extensions could be submitted as future DWARF standard proposals.

The general principles in designing the extensions have been:

1.  Be backwards compatible with the DWARF Version 5 [:ref:`DWARF
    <amdgpu-dwarf-DWARF>`] standard.

2.  Be vendor and architecture neutral. They are intended to apply to other
    heterogeneous hardware devices including GPUs, DSPs, FPGAs, and other
    specialized hardware. These collectively include similar characteristics and
    requirements as AMDGPU devices.

3.  Provide improved optimization support for non-GPU code. For example, some
    extensions apply to traditional CPU hardware that supports large vector
    registers. Compilers can map source languages, and source language
    extensions, that describe large scale parallel execution, onto the lanes of
    the vector registers. This is common in programming languages used in ML and
    HPC.

4.  Fully define well-formed DWARF in a consistent style based on the DWARF
    Version 5 specification.

It is possible that some of the generalizations may also benefit other DWARF
issues that have been raised.

The remainder of this section enumerates the extensions and provides motivation
for each in terms of heterogeneous debugging.

.. _amdgpu-dwarf-allow-location-description-on-the-dwarf-evaluation-stack:

2.1 Allow Location Description on the DWARF Expression Stack
------------------------------------------------------------

DWARF Version 5 does not allow location descriptions to be entries on the DWARF
expression stack. They can only be the final result of the evaluation of a DWARF
expression. However, by allowing a location description to be a first-class
entry on the DWARF expression stack it becomes possible to compose expressions
containing both values and location descriptions naturally. It allows objects to
be located in any kind of memory address space, in registers, be implicit
values, be undefined, or a composite of any of these.

By extending DWARF carefully, all existing DWARF expressions can retain their
current semantic meaning. DWARF has implicit conversions that convert from a
value that represents an address in the default address space to a memory
location description. This can be extended to allow a default address space
memory location description to be implicitly converted back to its address
value. This allows all DWARF Version 5 expressions to retain their same meaning,
while enabling the ability to explicitly create memory location descriptions in
non-default address spaces and generalizing the power of composite location
descriptions to any kind of location description.

For those familiar with the definition of location descriptions in DWARF Version
5, the definitions in these extensions are presented differently, but does in
fact define the same concept with the same fundamental semantics. However, it
does so in a way that allows the concept to extend to support address spaces,
bit addressing, the ability for composite location descriptions to be composed
of any kind of location description, and the ability to support objects located
at multiple places. Collectively these changes expand the set of architectures
that can be supported and improves support for optimized code.

Several approaches were considered, and the one presented, together with the
extensions it enables, appears to be the simplest and cleanest one that offers
the greatest improvement of DWARF's ability to support debugging optimized GPU
and non-GPU code. Examining the GDB debugger and LLVM compiler, it appears only
to require modest changes as they both already have to support general use of
location descriptions. It is anticipated that will also be the case for other
debuggers and compilers.

GDB has been modified to evaluate DWARF Version 5 expressions with location
descriptions as stack entries and with implicit conversions. All GDB tests have
passed, except one that turned out to be an invalid test case by DWARF Version 5
rules. The code in GDB actually became simpler as all evaluation is done on a
single stack and there was no longer a need to maintain a separate structure for
the location description results. This gives confidence in backwards
compatibility.

See :ref:`amdgpu-dwarf-expressions` and nested sections.

This extension is separately described at *Allow Location Descriptions on the
DWARF Expression Stack* [:ref:`AMDGPU-DWARF-LOC
<amdgpu-dwarf-AMDGPU-DWARF-LOC>`].

2.2 Generalize CFI to Allow Any Location Description Kind
---------------------------------------------------------

CFI describes restoring callee saved registers that are spilled. Currently CFI
only allows a location description that is a register, memory address, or
implicit location description. AMDGPU optimized code may spill scalar registers
into portions of vector registers. This requires extending CFI to allow any
location description kind to be supported.

See :ref:`amdgpu-dwarf-call-frame-information`.

2.3 Generalize DWARF Operation Expressions to Support Multiple Places
---------------------------------------------------------------------

In DWARF Version 5 a location description is defined as a single location
description or a location list. A location list is defined as either
effectively an undefined location description or as one or more single
location descriptions to describe an object with multiple places.

With
:ref:`amdgpu-dwarf-allow-location-description-on-the-dwarf-evaluation-stack`,
the ``DW_OP_push_object_address`` and ``DW_OP_call*`` operations can put a
location description on the stack. Furthermore, debugger information entry
attributes such as ``DW_AT_data_member_location``, ``DW_AT_use_location``, and
``DW_AT_vtable_elem_location`` are defined as pushing a location description on
the expression stack before evaluating the expression.

DWARF Version 5 only allows the stack to contain values and so only a single
memory address can be on the stack. This makes these operations and attributes
incapable of handling location descriptions with multiple places, or places
other than memory.

Since
:ref:`amdgpu-dwarf-allow-location-description-on-the-dwarf-evaluation-stack`
allows the stack to contain location descriptions, the operations are
generalized to support location descriptions that can have multiple places. This
is backwards compatible with DWARF Version 5 and allows objects with multiple
places to be supported. For example, the expression that describes how to access
the field of an object can be evaluated with a location description that has
multiple places and will result in a location description with multiple places.

With this change, the separate DWARF Version 5 sections that described DWARF
expressions and location lists are unified into a single section that describes
DWARF expressions in general. This unification is a natural consequence of, and
a necessity of, allowing location descriptions to be part of the evaluation
stack.

See :ref:`amdgpu-dwarf-location-description`.

2.4 Generalize Offsetting of Location Descriptions
--------------------------------------------------

The ``DW_OP_plus`` and ``DW_OP_minus`` operations can be defined to operate on a
memory location description in the default target architecture specific address
space and a generic type value to produce an updated memory location
description. This allows them to continue to be used to offset an address.

To generalize offsetting to any location description, including location
descriptions that describe when bytes are in registers, are implicit, or a
composite of these, the ``DW_OP_LLVM_offset``, ``DW_OP_LLVM_offset_uconst``, and
``DW_OP_LLVM_bit_offset`` offset operations are added.

The offset operations can operate on location storage of any size. For example,
implicit location storage could be any number of bits in size. It is simpler to
define offsets that exceed the size of the location storage as being an
evaluation error, than having to force an implementation to support potentially
infinite precision offsets to allow it to correctly track a series of positive
and negative offsets that may transiently overflow or underflow, but end up in
range. This is simple for the arithmetic operations as they are defined in terms
of two's complement arithmetic on a base type of a fixed size. Therefore, the
offset operation define that integer overflow is ill-formed. This is in contrast
to the ``DW_OP_plus``, ``DW_OP_plus_uconst``, and ``DW_OP_minus`` arithmetic
operations which define that it causes wrap-around.

Having the offset operations allows ``DW_OP_push_object_address`` to push a
location description that may be in a register, or be an implicit value. The
DWARF expression of ``DW_TAG_ptr_to_member_type`` can use the offset operations
without regard to what kind of location description was pushed.

Since
:ref:`amdgpu-dwarf-allow-location-description-on-the-dwarf-evaluation-stack` has
generalized location storage to be bit indexable, ``DW_OP_LLVM_bit_offset``
generalizes DWARF to work with bit fields. This is generally not possible in
DWARF Version 5.

The ``DW_OP_*piece`` operations only allow literal indices. A way to use a
computed offset of an arbitrary location description (such as a vector register)
is required. The offset operations provide this ability since they can be used
to compute a location description on the stack.

It could be possible to define ``DW_OP_plus``, ``DW_OP_plus_uconst``, and
``DW_OP_minus`` to operate on location descriptions to avoid needing
``DW_OP_LLVM_offset`` and ``DW_OP_LLVM_offset_uconst``. However, this is not
proposed since currently the arithmetic operations are defined to require values
of the same base type and produces a result with the same base type. Allowing
these operations to act on location descriptions would permit the first operand
to be a location description and the second operand to be an integral value
type, or vice versa, and return a location description. This complicates the
rules for implicit conversions between default address space memory location
descriptions and generic base type values. Currently the rules would convert
such a location description to the memory address value and then perform two's
compliment wrap around arithmetic. If the result was used as a location
description, it would be implicitly converted back to a default address space
memory location description. This is different to the overflow rules on location
descriptions. To allow control, an operation that converts a memory location
description to an address integral type value would be required. Keeping a
separation of location description operations and arithmetic operations avoids
this semantic complexity.

See ``DW_OP_LLVM_offset``, ``DW_OP_LLVM_offset_uconst``, and
``DW_OP_LLVM_bit_offset`` in
:ref:`amdgpu-dwarf-general-location-description-operations`.

2.5 Generalize Creation of Undefined Location Descriptions
----------------------------------------------------------

Current DWARF uses an empty expression to indicate an undefined location
description. Since
:ref:`amdgpu-dwarf-allow-location-description-on-the-dwarf-evaluation-stack`
allows location descriptions to be created on the stack, it is necessary to have
an explicit way to specify an undefined location description.

For example, the ``DW_OP_LLVM_select_bit_piece`` (see
:ref:`amdgpu-dwarf-support-for-divergent-control-flow-of-simt-hardware`)
operation takes more than one location description on the stack. Without this
ability, it is not possible to specify that a particular one of the input
location descriptions is undefined.

See the ``DW_OP_LLVM_undefined`` operation in
:ref:`amdgpu-dwarf-undefined-location-description-operations`.

2.6 Generalize Creation of Composite Location Descriptions
----------------------------------------------------------

To allow composition of composite location descriptions, an explicit operation
that indicates the end of the definition of a composite location description is
required. This can be implied if the end of a DWARF expression is reached,
allowing current DWARF expressions to remain legal.

See ``DW_OP_LLVM_piece_end`` in
:ref:`amdgpu-dwarf-composite-location-description-operations`.

2.7 Generalize DWARF Base Objects to Allow Any Location Description Kind
------------------------------------------------------------------------

The number of registers and the cost of memory operations is much higher for
AMDGPU than a typical CPU. The compiler attempts to optimize whole variables and
arrays into registers.

Currently DWARF only allows ``DW_OP_push_object_address`` and related operations
to work with a global memory location. To support AMDGPU optimized code it is
required to generalize DWARF to allow any location description to be used. This
allows registers, or composite location descriptions that may be a mixture of
memory, registers, or even implicit values.

See ``DW_OP_push_object_address`` in
:ref:`amdgpu-dwarf-general-location-description-operations`.

2.8 General Support for Address Spaces
--------------------------------------

AMDGPU needs to be able to describe addresses that are in different kinds of
memory. Optimized code may need to describe a variable that resides in pieces
that are in different kinds of storage which may include parts of registers,
memory that is in a mixture of memory kinds, implicit values, or be undefined.

DWARF has the concept of segment addresses. However, the segment cannot be
specified within a DWARF expression, which is only able to specify the offset
portion of a segment address. The segment index is only provided by the entity
that specifies the DWARF expression. Therefore, the segment index is a property
that can only be put on complete objects, such as a variable. That makes it only
suitable for describing an entity (such as variable or subprogram code) that is
in a single kind of memory.

AMDGPU uses multiple address spaces. For example, a variable may be allocated in
a register that is partially spilled to the call stack which is in the private
address space, and partially spilled to the local address space. DWARF mentions
address spaces, for example as an argument to the ``DW_OP_xderef*`` operations.
A new section that defines address spaces is added (see
:ref:`amdgpu-dwarf-address-spaces`).

A new attribute ``DW_AT_LLVM_address_space`` is added to pointer and reference
types (see :ref:`amdgpu-dwarf-type-modifier-entries`). This allows the compiler
to specify which address space is being used to represent the pointer or
reference type.

DWARF uses the concept of an address in many expression operations but does not
define how it relates to address spaces. For example,
``DW_OP_push_object_address`` pushes the address of an object. Other contexts
implicitly push an address on the stack before evaluating an expression. For
example, the ``DW_AT_use_location`` attribute of the
``DW_TAG_ptr_to_member_type``. The expression belongs to a source language type
which may apply to objects allocated in different kinds of storage. Therefore,
it is desirable that the expression that uses the address can do so without
regard to what kind of storage it specifies, including the address space of a
memory location description. For example, a pointer to member value may want to
be applied to an object that may reside in any address space.

The DWARF ``DW_OP_xderef*`` operations allow a value to be converted into an
address of a specified address space which is then read. But it provides no
way to create a memory location description for an address in the non-default
address space. For example, AMDGPU variables can be allocated in the local
address space at a fixed address.

The ``DW_OP_LLVM_form_aspace_address`` (see
:ref:`amdgpu-dwarf-memory-location-description-operations`) operation is defined
to create a memory location description from an address and address space. If
can be used to specify the location of a variable that is allocated in a
specific address space. This allows the size of addresses in an address space to
be larger than the generic type. It also allows a consumer great implementation
freedom. It allows the implicit conversion back to a value to be limited only to
the default address space to maintain compatibility with DWARF Version 5. For
other address spaces the producer can use the new operations that explicitly
specify the address space.

In contrast, if the ``DW_OP_LLVM_form_aspace_address`` operation had been
defined to produce a value, and an implicit conversion to a memory location
description was defined, then it would be limited to the size of the generic
type (which matches the size of the default address space). An implementation
would likely have to use *reserved ranges* of value to represent different
address spaces. Such a value would likely not match any address value in the
actual hardware. That would require the consumer to have special treatment for
such values.

``DW_OP_breg*`` treats the register as containing an address in the default
address space. A ``DW_OP_LLVM_aspace_bregx`` (see
:ref:`amdgpu-dwarf-memory-location-description-operations`) operation is added
to allow the address space of the address held in a register to be specified.

Similarly, ``DW_OP_implicit_pointer`` treats its implicit pointer value as being
in the default address space. A ``DW_OP_LLVM_aspace_implicit_pointer``
(:ref:`amdgpu-dwarf-implicit-location-description-operations`) operation is
added to allow the address space to be specified.

Almost all uses of addresses in DWARF are limited to defining location
descriptions, or to be dereferenced to read memory. The exception is
``DW_CFA_val_offset`` which uses the address to set the value of a register. In
order to support address spaces, the CFA DWARF expression is defined to be a
memory location description. This allows it to specify an address space which is
used to convert the offset address back to an address in that address space. See
:ref:`amdgpu-dwarf-call-frame-information`.

This approach of extending memory location descriptions to support address
spaces, allows all existing DWARF Version 5 expressions to have the identical
semantics. It allows the compiler to explicitly specify the address space it is
using. For example, a compiler could choose to access private memory in a
swizzled manner when mapping a source language thread to the lane of a wavefront
in a SIMT manner. Or a compiler could choose to access it in an unswizzled
manner if mapping the same language with the wavefront being the thread.

It also allows the compiler to mix the address space it uses to access private
memory. For example, for SIMT it can still spill entire vector registers in an
unswizzled manner, while using a swizzled private memory for SIMT variable
access.

This approach also allows memory location descriptions for different address
spaces to be combined using the regular ``DW_OP_*piece`` operations.

Location descriptions are an abstraction of storage. They give freedom to the
consumer on how to implement them. They allow the address space to encode lane
information so they can be used to read memory with only the memory location
description and no extra information. The same set of operations can operate on
locations independent of their kind of storage. The ``DW_OP_deref*`` therefore
can be used on any storage kind, including memory location descriptions of
different address spaces. Therefore, the ``DW_OP_xderef*`` operations are
unnecessary, except to become a more compact way to encode a non-default address
space address followed by dereferencing it. See
:ref:`amdgpu-dwarf-general-operations`.

2.9 Support for Vector Base Types
---------------------------------

The vector registers of the AMDGPU are represented as their full wavefront
size, meaning the wavefront size times the dword size. This reflects the
actual hardware and allows the compiler to generate DWARF for languages that
map a thread to the complete wavefront. It also allows more efficient DWARF to
be generated to describe the CFI as only a single expression is required for
the whole vector register, rather than a separate expression for each lane's
dword of the vector register. It also allows the compiler to produce DWARF
that indexes the vector register if it spills scalar registers into portions
of a vector register.

Since DWARF stack value entries have a base type and AMDGPU registers are a
vector of dwords, the ability to specify that a base type is a vector is
required.

See ``DW_AT_LLVM_vector_size`` in :ref:`amdgpu-dwarf-base-type-entries`.

.. _amdgpu-dwarf-operation-to-create-vector-composite-location-descriptions:

2.10 DWARF Operations to Create Vector Composite Location Descriptions
----------------------------------------------------------------------

AMDGPU optimized code may spill vector registers to non-global address space
memory, and this spilling may be done only for SIMT lanes that are active on
entry to the subprogram. To support this the CFI rule for the partially spilled
register needs to use an expression that uses the EXEC register as a bit mask to
select between the register (for inactive lanes) and the stack spill location
(for active lanes that are spilled). This needs to evaluate to a location
description, and not a value, as a debugger needs to change the value if the
user assigns to the variable.

Another usage is to create an expression that evaluates to provide a vector of
logical PCs for active and inactive lanes in a SIMT execution model. Again the
EXEC register is used to select between active and inactive PC values. In order
to represent a vector of PC values, a way to create a composite location
description that is a vector of a single location is used.

It may be possible to use existing DWARF to incrementally build the composite
location description, possibly using the DWARF operations for control flow to
create a loop. However, for the AMDGPU that would require loop iteration of 64.
A concern is that the resulting DWARF would have a significant size and would be
reasonably common as it is needed for every vector register that is spilled in a
function. AMDGPU can have up to 512 vector registers. Another concern is the
time taken to evaluate such non-trivial expressions repeatedly.

To avoid these issues, a composite location description that can be created as a
masked select is proposed. In addition, an operation that creates a composite
location description that is a vector on another location description is needed.
These operations generate the composite location description using a single
DWARF operation that combines all lanes of the vector in one step. The DWARF
expression is more compact, and can be evaluated by a consumer far more
efficiently.

An example that uses these operations is referenced in the
:ref:`amdgpu-dwarf-further-examples` appendix.

See ``DW_OP_LLVM_select_bit_piece`` and ``DW_OP_LLVM_extend`` in
:ref:`amdgpu-dwarf-composite-location-description-operations`.

2.11 DWARF Operation to Access Call Frame Entry Registers
---------------------------------------------------------

As described in
:ref:`amdgpu-dwarf-operation-to-create-vector-composite-location-descriptions`,
a DWARF expression involving the set of SIMT lanes active on entry to a
subprogram is required. The SIMT active lane mask may be held in a register that
is modified as the subprogram executes. However, its value may be saved on entry
to the subprogram.

The  Call Frame Information (CFI) already encodes such register saving, so it is
more efficient to provide an operation to return the location of a saved
register than have to generate a loclist to describe the same information. This
is now possible since
:ref:`amdgpu-dwarf-allow-location-description-on-the-dwarf-evaluation-stack`
allows location descriptions on the stack.

See ``DW_OP_LLVM_call_frame_entry_reg`` in
:ref:`amdgpu-dwarf-general-location-description-operations` and
:ref:`amdgpu-dwarf-call-frame-information`.

2.12 Support for Source Languages Mapped to SIMT Hardware
---------------------------------------------------------

If the source language is mapped onto the AMDGPU wavefronts in a SIMT manner,
then the variable DWARF location expressions must compute the location for a
single lane of the wavefront. Therefore, a DWARF operation is required to denote
the current lane, much like ``DW_OP_push_object_address`` denotes the current
object. See ``DW_OP_LLVM_push_lane`` in :ref:`amdgpu-dwarf-literal-operations`.

In addition, a way is needed for the compiler to communicate how many source
language threads of execution are mapped to a target architecture thread's SIMT
lanes. See ``DW_AT_LLVM_lanes`` in :ref:`amdgpu-dwarf-low-level-information`.

.. _amdgpu-dwarf-support-for-divergent-control-flow-of-simt-hardware:

2.13 Support for Divergent Control Flow of SIMT Hardware
--------------------------------------------------------

If the source language is mapped onto the AMDGPU wavefronts in a SIMT manner the
compiler can use the AMDGPU execution mask register to control which lanes are
active. To describe the conceptual location of non-active lanes requires an
attribute that has an expression that computes the source location PC for each
lane.

For efficiency, the expression calculates the source location the wavefront as a
whole. This can be done using the ``DW_OP_LLVM_select_bit_piece`` (see
:ref:`amdgpu-dwarf-operation-to-create-vector-composite-location-descriptions`)
operation.

The AMDGPU may update the execution mask to perform whole wavefront operations.
Therefore, there is a need for an attribute that computes the current active
lane mask. This can have an expression that may evaluate to the SIMT active lane
mask register or to a saved mask when in whole wavefront execution mode.

An example that uses these attributes is referenced in the
:ref:`amdgpu-dwarf-further-examples` appendix.

See ``DW_AT_LLVM_lane_pc`` and ``DW_AT_LLVM_active_lane`` in
:ref:`amdgpu-dwarf-composite-location-description-operations`.

2.14 Define Source Language Memory Classes
-------------------------------------------

AMDGPU supports languages, such as OpenCL [:ref:`OpenCL <amdgpu-dwarf-OpenCL>`],
that define source language memory classes. Support is added to define language
specific memory spaces so they can be used in a consistent way by consumers.

Support for using memory spaces in defining source language types and data
object allocation is also added.

See :ref:`amdgpu-dwarf-memory-spaces`.

2.15 Define Augmentation Strings to Support Multiple Extensions
---------------------------------------------------------------

A ``DW_AT_LLVM_augmentation`` attribute is added to a compilation unit debugger
information entry to indicate that there is additional target architecture
specific information in the debugging information entries of that compilation
unit. This allows a consumer to know what extensions are present in the debugger
information entries as is possible with the augmentation string of other
sections. See .

The format that should be used for an augmentation string is also recommended.
This allows a consumer to parse the string when it contains information from
multiple vendors. Augmentation strings occur in the ``DW_AT_LLVM_augmentation``
attribute, in the lookup by name table, and in the CFI Common Information Entry
(CIE).

See :ref:`amdgpu-dwarf-full-and-partial-compilation-unit-entries`,
:ref:`amdgpu-dwarf-name-index-section-header`, and
:ref:`amdgpu-dwarf-structure_of-call-frame-information`.

2.16 Support Embedding Source Text for Online Compilation
---------------------------------------------------------

AMDGPU supports programming languages that include online compilation where the
source text may be created at runtime. For example, the OpenCL and HIP language
runtimes support online compilation. To support is, a way to embed the source
text in the debug information is provided.

See :ref:`amdgpu-dwarf-line-number-information`.

2.17 Allow MD5 Checksums to be Optionally Present
-------------------------------------------------

In DWARF Version 5 the file timestamp and file size can be optional, but if the
MD5 checksum is present it must be valid for all files. This is a problem if
using link time optimization to combine compilation units where some have MD5
checksums and some do not. Therefore, sSupport to allow MD5 checksums to be
optionally present in the line table is added.

See :ref:`amdgpu-dwarf-line-number-information`.

2.18 Add the HIP Programing Language
------------------------------------

The HIP programming language [:ref:`HIP <amdgpu-dwarf-HIP>`], which is supported
by the AMDGPU, is added.

See :ref:`amdgpu-dwarf-language-names-table`.

2.19 Support for Source Language Optimizations that Result in Concurrent Iteration Execution
--------------------------------------------------------------------------------------------

A compiler can perform loop optimizations that result in the generated code
executing multiple iterations concurrently. For example, software pipelining
schedules multiple iterations in an interleaved fashion to allow the
instructions of one iteration to hide the latencies of the instructions of
another iteration. Another example is vectorization that can exploit SIMD
hardware to allow a single instruction to execute multiple iterations using
vector registers.

Note that although this is similar to SIMT execution, the way a client debugger
uses the information is fundamentally different. In SIMT execution the debugger
needs to present the concurrent execution as distinct source language threads
that the user can list and switch focus between. With iteration concurrency
optimizations, such as software pipelining and vectorized SIMD, the debugger
must not present the concurrency as distinct source language threads. Instead,
it must inform the user that multiple loop iterations are executing in parallel
and allow the user to select between them.

In general, SIMT execution fixes the number of concurrent executions per target
architecture thread. However, both software pipelining and SIMD vectorization
may vary the number of concurrent iterations for different loops executed by a
single source language thread.

It is possible for the compiler to use both SIMT concurrency and iteration
concurrency techniques in the code of a single source language thread.

Therefore, a DWARF operation is required to denote the current concurrent
iteration instance, much like ``DW_OP_push_object_address`` denotes the current
object. See ``DW_OP_LLVM_push_iteration`` in
:ref:`amdgpu-dwarf-literal-operations`.

In addition, a way is needed for the compiler to communicate how many source
language loop iterations are executing concurrently. See
``DW_AT_LLVM_iterations`` in :ref:`amdgpu-dwarf-low-level-information`.

2.20 DWARF Operation to Create Runtime Overlay Composite Location Description
-----------------------------------------------------------------------------

It is common in SIMD vectorization for the compiler to generate code that
promotes portions of an array into vector registers. For example, if the
hardware has vector registers with 8 elements, and 8 wide SIMD instructions, the
compiler may vectorize a loop so that is executes 8 iterations concurrently for
each vectorized loop iteration.

On the first iteration of the generated vectorized loop, iterations 0 to 7 of
the source language loop will be executed using SIMD instructions. Then on the
next iteration of the generated vectorized loop, iteration 8 to 15 will be
executed, and so on.

If the source language loop accesses an array element based on the loop
iteration index, the compiler may read the element into a register for the
duration of that iteration. Next iteration it will read the next element into
the register, and so on. With SIMD, this generalizes to the compiler reading
array elements 0 to 7 into a vector register on the first vectorized loop
iteration, then array elements 8 to 15 on the next iteration, and so on.

The DWARF location description for the array needs to express that all elements
are in memory, except the slice that has been promoted to the vector register.
The starting position of the slice is a runtime value based on the iteration
index modulo the vectorization size. This cannot be expressed by ``DW_OP_piece``
and ``DW_OP_bit_piece`` which only allow constant offsets to be expressed.

Therefore, a new operator is defined that takes two location descriptions, an
offset and a size, and creates a composite that effectively uses the second
location description as an overlay of the first, positioned according to the
offset and size. See ``DW_OP_LLVM_overlay`` and ``DW_OP_LLVM_bit_overlay`` in
:ref:`amdgpu-dwarf-composite-location-description-operations`.

Consider an array that has been partially registerized such that the currently
processed elements are held in registers, whereas the remainder of the array
remains in memory. Consider the loop in this C function, for example:

.. code::
  :number-lines:

  extern void foo(uint32_t dst[], uint32_t src[], int len) {
    for (int i = 0; i < len; ++i)
      dst[i] += src[i];
  }

Inside the loop body, the machine code loads ``src[i]`` and ``dst[i]`` into
registers, adds them, and stores the result back into ``dst[i]``.

Considering the location of ``dst`` and ``src`` in the loop body, the elements
``dst[i]`` and ``src[i]`` would be located in registers, all other elements are
located in memory. Let register ``R0`` contain the base address of ``dst``,
register ``R1`` contain ``i``, and register ``R2`` contain the registerized
``dst[i]`` element. We can describe the location of ``dst`` as a memory location
with a register location overlaid at a runtime offset involving ``i``:

.. code::
  :number-lines:

  // 1. Memory location description of dst elements located in memory:
  DW_OP_breg0 0

  // 2. Register location description of element dst[i] is located in R2:
  DW_OP_reg2

  // 3. Offset of the register within the memory of dst:
  DW_OP_breg1 0
  DW_OP_lit4
  DW_OP_mul

  // 4. The size of the register element:
  DW_OP_lit4

  // 5. Make a composite location description for dst that is the memory #1 with
  //    the register #2 positioned as an overlay at offset #3 of size #4:
  DW_OP_LLVM_overlay

2.21 Support for Source Language Memory Spaces
----------------------------------------------

AMDGPU supports languages, such as OpenCL, that define source language memory
spaces. Support is added to define language specific memory spaces so they can
be used in a consistent way by consumers. See :ref:`amdgpu-dwarf-memory-spaces`.

A new attribute ``DW_AT_LLVM_memory_space`` is added to support using memory
spaces in defining source language pointer and reference types (see
:ref:`amdgpu-dwarf-type-modifier-entries`) and data object allocation (see
:ref:`amdgpu-dwarf-data-object-entries`).

2.22 Expression Operation Vendor Extensibility Opcode
-----------------------------------------------------

The vendor extension encoding space for DWARF expression operations
accommodates only 32 unique operations. In practice, the lack of a central
registry and a desire for backwards compatibility means vendor extensions are
never retired, even when standard versions are accepted into DWARF proper. This
has produced a situation where the effective encoding space available for new
vendor extensions is miniscule today.

To expand this encoding space a new DWARF operation ``DW_OP_LLVM_user`` is
added which acts as a "prefix" for vendor extensions. It is followed by a
ULEB128 encoded vendor extension opcode, which is then followed by the operands
of the corresponding vendor extension operation.

This approach allows all remaining operations defined in these extensions to be
encoded without conflicting with existing vendor extensions.

See ``DW_OP_LLVM_user`` in :ref:`amdgpu-dwarf-vendor-extensions-operations`.

.. _amdgpu-dwarf-changes-relative-to-dwarf-version-5:

A. Changes Relative to DWARF Version 5
======================================

.. note::

  This appendix provides changes relative to DWARF Version 5. It has been
  defined such that it is backwards compatible with DWARF Version 5.
  Non-normative text is shown in *italics*. The section numbers generally
  correspond to those in the DWARF Version 5 standard unless specified
  otherwise. Definitions are given for the additional operations, as well as
  clarifying how existing expression operations, CFI operations, and attributes
  behave with respect to generalized location descriptions that support address
  spaces and multiple places.

  The names for the new operations, attributes, and constants include "\
  ``LLVM``\ " and are encoded with vendor specific codes so these extensions
  can be implemented as an LLVM vendor extension to DWARF Version 5. New
  operations other than ``DW_OP_LLVM_user`` are "prefixed" by
  ``DW_OP_LLVM_user`` to make enough encoding space available for their
  implementation.

  .. note::

    Notes are included to describe how the changes are to be applied to the
    DWARF Version 5 standard. They also describe rational and issues that may
    need further consideration.

A.2 General Description
-----------------------

A.2.2 Attribute Types
~~~~~~~~~~~~~~~~~~~~~

.. note::

  This augments DWARF Version 5 section 2.2 and Table 2.2.

The following table provides the additional attributes.

.. table:: Attribute names
   :name: amdgpu-dwarf-attribute-names-table

   ============================ ====================================
   Attribute                    Usage
   ============================ ====================================
   ``DW_AT_LLVM_active_lane``   SIMT active lanes (see :ref:`amdgpu-dwarf-low-level-information`)
   ``DW_AT_LLVM_augmentation``  Compilation unit augmentation string (see :ref:`amdgpu-dwarf-full-and-partial-compilation-unit-entries`)
   ``DW_AT_LLVM_lane_pc``       SIMT lane program location (see :ref:`amdgpu-dwarf-low-level-information`)
   ``DW_AT_LLVM_lanes``         SIMT lane count (see :ref:`amdgpu-dwarf-low-level-information`)
   ``DW_AT_LLVM_iterations``    Concurrent iteration count (see :ref:`amdgpu-dwarf-low-level-information`)
   ``DW_AT_LLVM_vector_size``   Base type vector size (see :ref:`amdgpu-dwarf-base-type-entries`)
   ``DW_AT_LLVM_address_space`` Architecture specific address space (see :ref:`amdgpu-dwarf-address-spaces`)
   ``DW_AT_LLVM_memory_space``  Pointer or reference types (see 5.3 "Type Modifier Entries")
                                Data objects (see 4.1 "Data Object Entries")
   ============================ ====================================

.. _amdgpu-dwarf-expressions:

A.2.5 DWARF Expressions
~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This section, and its nested sections, replaces DWARF Version 5 section 2.5
  and section 2.6. The new DWARF expression operation extensions are defined as
  well as clarifying the extensions to already existing DWARF Version 5
  operations. It is based on the text of the existing DWARF Version 5 standard.

DWARF expressions describe how to compute a value or specify a location.

*The evaluation of a DWARF expression can provide the location of an object, the
value of an array bound, the length of a dynamic string, the desired value
itself, and so on.*

If the evaluation of a DWARF expression does not encounter an error, then it can
either result in a value (see :ref:`amdgpu-dwarf-expression-value`) or a
location description (see :ref:`amdgpu-dwarf-location-description`). When a
DWARF expression is evaluated, it may be specified whether a value or location
description is required as the result kind.

If a result kind is specified, and the result of the evaluation does not match
the specified result kind, then the implicit conversions described in
:ref:`amdgpu-dwarf-memory-location-description-operations` are performed if
valid. Otherwise, the DWARF expression is ill-formed.

If the evaluation of a DWARF expression encounters an evaluation error, then the
result is an evaluation error.

.. note::

  Decided to define the concept of an evaluation error. An alternative is to
  introduce an undefined value base type in a similar way to location
  descriptions having an undefined location description. Then operations that
  encounter an evaluation error can return the undefined location description or
  value with an undefined base type.

  All operations that act on values would return an undefined entity if given an
  undefined value. The expression would then always evaluate to completion, and
  can be tested to determine if it is an undefined entity.

  However, this would add considerable additional complexity and does not match
  that GDB throws an exception when these evaluation errors occur.

If a DWARF expression is ill-formed, then the result is undefined.

The following sections detail the rules for when a DWARF expression is
ill-formed or results in an evaluation error.

A DWARF expression can either be encoded as an operation expression (see
:ref:`amdgpu-dwarf-operation-expressions`), or as a location list expression
(see :ref:`amdgpu-dwarf-location-list-expressions`).

.. _amdgpu-dwarf-expression-evaluation-context:

A.2.5.1 DWARF Expression Evaluation Context
+++++++++++++++++++++++++++++++++++++++++++

A DWARF expression is evaluated in a context that can include a number of
context elements. If multiple context elements are specified then they must be
self consistent or the result of the evaluation is undefined. The context
elements that can be specified are:

*A current result kind*

  The kind of result required by the DWARF expression evaluation. If specified
  it can be a location description or a value.

*A current thread*

  The target architecture thread identifier. For source languages that are not
  implemented using a SIMT execution model, this corresponds to the source
  program thread of execution for which a user presented expression is currently
  being evaluated. For source languages that are implemented using a SIMT
  execution model, this together with the current lane corresponds to the source
  program thread of execution for which a user presented expression is currently
  being evaluated.

  It is required for operations that are related to target architecture threads.

  *For example, the* ``DW_OP_regval_type`` *operation, or the*
  ``DW_OP_form_tls_address`` *and* ``DW_OP_LLVM_form_aspace_address``
  *operations when given an address space that is target architecture thread
  specific.*

*A current lane*

  The 0 based SIMT lane identifier to be used in evaluating a user presented
  expression. This applies to source languages that are implemented for a target
  architecture using a SIMT execution model. These implementations map source
  language threads of execution to lanes of the target architecture threads.

  It is required for operations that are related to SIMT lanes.

  *For example, the* ``DW_OP_LLVM_push_lane`` *operation and*
  ``DW_OP_LLVM_form_aspace_address`` *operation when given an address space that
  is SIMT lane specific.*

  If specified, it must be consistent with the value of the ``DW_AT_LLVM_lanes``
  attribute of the subprogram corresponding to context's frame and program
  location. It is consistent if the value is greater than or equal to 0 and less
  than the, possibly default, value of the ``DW_AT_LLVM_lanes`` attribute.
  Otherwise the result is undefined.

*A current iteration*

  The 0 based source language iteration instance to be used in evaluating a user
  presented expression. This applies to target architectures that support
  optimizations that result in executing multiple source language loop iterations
  concurrently.

  *For example, software pipelining and SIMD vectorization.*

  It is required for operations that are related to source language loop
  iterations.

  *For example, the* ``DW_OP_LLVM_push_iteration`` *operation.*

  If specified, it must be consistent with the value of the
  ``DW_AT_LLVM_iterations`` attribute of the subprogram corresponding to
  context's frame and program location. It is consistent if the value is greater
  than or equal to 0 and less than the, possibly default, value of the
  ``DW_AT_LLVM_iterations`` attribute. Otherwise the result is undefined.

*A current call frame*

  The target architecture call frame identifier. It identifies a call frame that
  corresponds to an active invocation of a subprogram in the current thread. It
  is identified by its address on the call stack. The address is referred to as
  the Canonical Frame Address (CFA). The call frame information is used to
  determine the CFA for the call frames of the current thread's call stack (see
  :ref:`amdgpu-dwarf-call-frame-information`).

  It is required for operations that specify target architecture registers to
  support virtual unwinding of the call stack.

  *For example, the* ``DW_OP_*reg*`` *operations.*

  If specified, it must be an active call frame in the current thread. If the
  current lane is specified, then that lane must have been active on entry to
  the call frame (see the ``DW_AT_LLVM_lane_pc`` attribute). Otherwise the
  result is undefined.

  If it is the currently executing call frame, then it is termed the top call
  frame.

*A current program location*

  The target architecture program location corresponding to the current call
  frame of the current thread.

  The program location of the top call frame is the target architecture program
  counter for the current thread. The call frame information is used to obtain
  the value of the return address register to determine the program location of
  the other call frames (see :ref:`amdgpu-dwarf-call-frame-information`).

  It is required for the evaluation of location list expressions to select
  amongst multiple program location ranges. It is required for operations that
  specify target architecture registers to support virtual unwinding of the call
  stack (see :ref:`amdgpu-dwarf-call-frame-information`).

  If specified:

  * If the current lane is not specified:

    * If the current call frame is the top call frame, it must be the current
      target architecture program location.

    * If the current call frame F is not the top call frame, it must be the
      program location associated with the call site in the current caller frame
      F that invoked the callee frame.

  * If the current lane is specified and the architecture program location LPC
    computed by the ``DW_AT_LLVM_lane_pc`` attribute for the current lane is not
    the undefined location description (indicating the lane was not active on
    entry to the call frame), it must be LPC.

  * Otherwise the result is undefined.

*A current compilation unit*

  The compilation unit debug information entry that contains the DWARF expression
  being evaluated.

  It is required for operations that reference debug information associated with
  the same compilation unit, including indicating if such references use the
  32-bit or 64-bit DWARF format. It can also provide the default address space
  address size if no current target architecture is specified.

  *For example, the* ``DW_OP_constx`` *and* ``DW_OP_addrx`` *operations.*

  *Note that this compilation unit may not be the same as the compilation unit
  determined from the loaded code object corresponding to the current program
  location. For example, the evaluation of the expression E associated with a*
  ``DW_AT_location`` *attribute of the debug information entry operand of the*
  ``DW_OP_call*`` *operations is evaluated with the compilation unit that
  contains E and not the one that contains the* ``DW_OP_call*`` *operation
  expression.*

*A current target architecture*

  The target architecture.

  It is required for operations that specify target architecture specific
  entities.

  *For example, target architecture specific entities include DWARF register
  identifiers, DWARF lane identifiers, DWARF address space identifiers, the
  default address space, and the address space address sizes.*

  If specified:

  * If the current frame is specified, then the current target architecture must
    be the same as the target architecture of the current frame.

  * If the current frame is specified and is the top frame, and if the current
    thread is specified, then the current target architecture must be the same
    as the target architecture of the current thread.

  * If the current compilation unit is specified, then the current target
    architecture default address space address size must be the same as the
    ``address_size`` field in the header of the current compilation unit and any
    associated entry in the ``.debug_aranges`` section.

  * If the current program location is specified, then the current target
    architecture must be the same as the target architecture of any line number
    information entry (see :ref:`amdgpu-dwarf-line-number-information`)
    corresponding to the current program location.

  * If the current program location is specified, then the current target
    architecture default address space address size must be the same as the
    ``address_size`` field in the header of any entry corresponding to the
    current program location in the ``.debug_addr``, ``.debug_line``,
    ``.debug_rnglists``, ``.debug_rnglists.dwo``, ``.debug_loclists``, and
    ``.debug_loclists.dwo`` sections.

  * Otherwise the result is undefined.

*A current object*

  The location description of a program object.

  It is required for the ``DW_OP_push_object_address`` operation.

  *For example, the* ``DW_AT_data_location`` *attribute on type debug
  information entries specifies the program object corresponding to a runtime
  descriptor as the current object when it evaluates its associated expression.*

  The result is undefined if the location description is invalid (see
  :ref:`amdgpu-dwarf-location-description`).

*An initial stack*

  This is a list of values or location descriptions that will be pushed on the
  operation expression evaluation stack in the order provided before evaluation
  of an operation expression starts.

  Some debugger information entries have attributes that evaluate their DWARF
  expression value with initial stack entries. In all other cases the initial
  stack is empty.

  The result is undefined if any location descriptions are invalid (see
  :ref:`amdgpu-dwarf-location-description`).

If the evaluation requires a context element that is not specified, then the
result of the evaluation is an error.

*A DWARF expression for a location description may be able to be evaluated
without a thread, lane, call frame, program location, or architecture context.
For example, the location of a global variable may be able to be evaluated
without such context. If the expression evaluates with an error then it may
indicate the variable has been optimized and so requires more context.*

*The DWARF expression for call frame information (see*
:ref:`amdgpu-dwarf-call-frame-information`\ *) operations are restricted to
those that do not require the compilation unit context to be specified.*

The DWARF is ill-formed if all the ``address_size`` fields in the headers of all
the entries in the ``.debug_info``, ``.debug_addr``, ``.debug_line``,
``.debug_rnglists``, ``.debug_rnglists.dwo``, ``.debug_loclists``, and
``.debug_loclists.dwo`` sections corresponding to any given program location do
not match.

.. _amdgpu-dwarf-expression-value:

A.2.5.2 DWARF Expression Value
++++++++++++++++++++++++++++++

A value has a type and a literal value. It can represent a literal value of any
supported base type of the target architecture. The base type specifies the
size, encoding, and endianity of the literal value.

.. note::

  It may be desirable to add an implicit pointer base type encoding. It would be
  used for the type of the value that is produced when the ``DW_OP_deref*``
  operation retrieves the full contents of an implicit pointer location storage
  created by the ``DW_OP_implicit_pointer`` or
  ``DW_OP_LLVM_aspace_implicit_pointer`` operations. The literal value would
  record the debugging information entry and byte displacement specified by the
  associated ``DW_OP_implicit_pointer`` or
  ``DW_OP_LLVM_aspace_implicit_pointer`` operations.

There is a distinguished base type termed the generic type, which is an integral
type that has the size of an address in the target architecture default address
space, a target architecture defined endianity, and unspecified signedness.

*The generic type is the same as the unspecified type used for stack operations
defined in DWARF Version 4 and before.*

An integral type is a base type that has an encoding of ``DW_ATE_signed``,
``DW_ATE_signed_char``, ``DW_ATE_unsigned``, ``DW_ATE_unsigned_char``,
``DW_ATE_boolean``, or any target architecture defined integral encoding in the
inclusive range ``DW_ATE_lo_user`` to ``DW_ATE_hi_user``.

.. note::

  It is unclear if ``DW_ATE_address`` is an integral type. GDB does not seem to
  consider it as integral.

.. _amdgpu-dwarf-location-description:

A.2.5.3 DWARF Location Description
++++++++++++++++++++++++++++++++++

*Debugging information must provide consumers a way to find the location of
program variables, determine the bounds of dynamic arrays and strings, and
possibly to find the base address of a subprogram’s call frame or the return
address of a subprogram. Furthermore, to meet the needs of recent computer
architectures and optimization techniques, debugging information must be able to
describe the location of an object whose location changes over the object’s
lifetime, and may reside at multiple locations simultaneously during parts of an
object's lifetime.*

Information about the location of program objects is provided by location
descriptions.

Location descriptions can consist of one or more single location descriptions.

A single location description specifies the location storage that holds a
program object and a position within the location storage where the program
object starts. The position within the location storage is expressed as a bit
offset relative to the start of the location storage.

A location storage is a linear stream of bits that can hold values. Each
location storage has a size in bits and can be accessed using a zero-based bit
offset. The ordering of bits within a location storage uses the bit numbering
and direction conventions that are appropriate to the current language on the
target architecture.

There are five kinds of location storage:

*memory location storage*
  Corresponds to the target architecture memory address spaces.

*register location storage*
  Corresponds to the target architecture registers.

*implicit location storage*
  Corresponds to fixed values that can only be read.

*undefined location storage*
  Indicates no value is available and therefore cannot be read or written.

*composite location storage*
  Allows a mixture of these where some bits come from one location storage and
  some from another location storage, or from disjoint parts of the same
  location storage.

.. note::

  It may be better to add an implicit pointer location storage kind used by the
  ``DW_OP_implicit_pointer`` and ``DW_OP_LLVM_aspace_implicit_pointer``
  operations. It would specify the debugger information entry and byte offset
  provided by the operations.

*Location descriptions are a language independent representation of addressing
rules.*

* *They can be the result of evaluating a debugger information entry attribute
  that specifies an operation expression of arbitrary complexity. In this usage
  they can describe the location of an object as long as its lifetime is either
  static or the same as the lexical block (see
  :ref:`amdgpu-dwarf-lexical-block-entries`) that owns it, and it does not move
  during its lifetime.*

* *They can be the result of evaluating a debugger information entry attribute
  that specifies a location list expression. In this usage they can describe the
  location of an object that has a limited lifetime, changes its location during
  its lifetime, or has multiple locations over part or all of its lifetime.*

If a location description has more than one single location description, the
DWARF expression is ill-formed if the object value held in each single location
description's position within the associated location storage is not the same
value, except for the parts of the value that are uninitialized.

*A location description that has more than one single location description can
only be created by a location list expression that has overlapping program
location ranges, or certain expression operations that act on a location
description that has more than one single location description. There are no
operation expression operations that can directly create a location description
with more than one single location description.*

*A location description with more than one single location description can be
used to describe objects that reside in more than one piece of storage at the
same time. An object may have more than one location as a result of
optimization. For example, a value that is only read may be promoted from memory
to a register for some region of code, but later code may revert to reading the
value from memory as the register may be used for other purposes. For the code
region where the value is in a register, any change to the object value must be
made in both the register and the memory so both regions of code will read the
updated value.*

*A consumer of a location description with more than one single location
description can read the object's value from any of the single location
descriptions (since they all refer to location storage that has the same value),
but must write any changed value to all the single location descriptions.*

The evaluation of an expression may require context elements to create a
location description. If such a location description is accessed, the storage it
denotes is that associated with the context element values specified when the
location description was created, which may differ from the context at the time
it is accessed.

*For example, creating a register location description requires the thread
context: the location storage is for the specified register of that thread.
Creating a memory location description for an address space may required a
thread and a lane context: the location storage is the memory associated with
that thread and lane.*

If any of the context elements required to create a location description change,
the location description becomes invalid and accessing it is undefined.

*Examples of context that can invalidate a location description are:*

* *The thread context is required and execution causes the thread to terminate.*
* *The call frame context is required and further execution causes the call
  frame to return to the calling frame.*
* *The program location is required and further execution of the thread occurs.
  That could change the location list entry or call frame information entry that
  applies.*
* *An operation uses call frame information:*

  * *Any of the frames used in the virtual call frame unwinding return.*
  * *The top call frame is used, the program location is used to select the call
    frame information entry, and further execution of the thread occurs.*

*A DWARF expression can be used to compute a location description for an object.
A subsequent DWARF expression evaluation can be given the object location
description as the object context or initial stack context to compute a
component of the object. The final result is undefined if the object location
description becomes invalid between the two expression evaluations.*

A change of a thread's program location may not make a location description
invalid, yet may still render it as no longer meaningful. Accessing such a
location description, or using it as the object context or initial stack context
of an expression evaluation, may produce an undefined result.

*For example, a location description may specify a register that no longer holds
the intended program object after a program location change. One way to avoid
such problems is to recompute location descriptions associated with threads when
their program locations change.*

.. _amdgpu-dwarf-operation-expressions:

A.2.5.4 DWARF Operation Expressions
+++++++++++++++++++++++++++++++++++

An operation expression is comprised of a stream of operations, each consisting
of an opcode followed by zero or more operands. The number of operands is
implied by the opcode.

Operations represent a postfix operation on a simple stack machine. Each stack
entry can hold either a value or a location description. Operations can act on
entries on the stack, including adding entries and removing entries. If the kind
of a stack entry does not match the kind required by the operation and is not
implicitly convertible to the required kind (see
:ref:`amdgpu-dwarf-memory-location-description-operations`), then the DWARF
operation expression is ill-formed.

Evaluation of an operation expression starts with an empty stack on which the
entries from the initial stack provided by the context are pushed in the order
provided. Then the operations are evaluated, starting with the first operation
of the stream. Evaluation continues until either an operation has an evaluation
error, or until one past the last operation of the stream is reached.

The result of the evaluation is:

* If an operation has an evaluation error, or an operation evaluates an
  expression that has an evaluation error, then the result is an evaluation
  error.

* If the current result kind specifies a location description, then:

  * If the stack is empty, the result is a location description with one
    undefined location description.

    *This rule is for backwards compatibility with DWARF Version 5 which has no
    explicit operation to create an undefined location description, and uses an
    empty operation expression for this purpose.*

  * If the top stack entry is a location description, or can be converted
    to one (see :ref:`amdgpu-dwarf-memory-location-description-operations`),
    then the result is that, possibly converted, location description. Any other
    entries on the stack are discarded.

  * Otherwise the DWARF expression is ill-formed.

    .. note::

      Could define this case as returning an implicit location description as
      if the ``DW_OP_implicit`` operation is performed.

* If the current result kind specifies a value, then:

  * If the top stack entry is a value, or can be converted to one (see
    :ref:`amdgpu-dwarf-memory-location-description-operations`), then the result
    is that, possibly converted, value. Any other entries on the stack are
    discarded.

  * Otherwise the DWARF expression is ill-formed.

* If the current result kind is not specified, then:

  * If the stack is empty, the result is a location description with one
    undefined location description.

    *This rule is for backwards compatibility with DWARF Version 5 which has no
    explicit operation to create an undefined location description, and uses an
    empty operation expression for this purpose.*

    .. note::

      This rule is consistent with the rule above for when a location
      description is requested. However, GDB appears to report this as an error
      and no GDB tests appear to cause an empty stack for this case.

  * Otherwise, the top stack entry is returned. Any other entries on the stack
    are discarded.

An operation expression is encoded as a byte block with some form of prefix that
specifies the byte count. It can be used:

* as the value of a debugging information entry attribute that is encoded using
  class ``exprloc`` (see :ref:`amdgpu-dwarf-classes-and-forms`),

* as the operand to certain operation expression operations,

* as the operand to certain call frame information operations (see
  :ref:`amdgpu-dwarf-call-frame-information`),

* and in location list entries (see
  :ref:`amdgpu-dwarf-location-list-expressions`).

.. _amdgpu-dwarf-vendor-extensions-operations:

A.2.5.4.0 Vendor Extension Operations
#####################################

1.  ``DW_OP_LLVM_user``

  ``DW_OP_LLVM_user`` encodes a vendor extension operation. It has at least one
  operand: a ULEB128 constant identifying a vendor extension operation. The
  remaining operands are defined by the vendor extension. The vendor extension
  opcode 0 is reserved and cannot be used by any vendor extension.

  *The DW_OP_user encoding space can be understood to supplement the space
  defined by DW_OP_lo_user and DW_OP_hi_user that is allocated by the standard
  for the same purpose.*

.. _amdgpu-dwarf-stack-operations:

A.2.5.4.1 Stack Operations
##########################

.. note::

  This section replaces DWARF Version 5 section 2.5.1.3.

The following operations manipulate the DWARF stack. Operations that index the
stack assume that the top of the stack (most recently added entry) has index 0.
They allow the stack entries to be either a value or location description.

If any stack entry accessed by a stack operation is an incomplete composite
location description (see
:ref:`amdgpu-dwarf-composite-location-description-operations`), then the DWARF
expression is ill-formed.

.. note::

  These operations now support stack entries that are values and location
  descriptions.

.. note::

  If it is desired to also make them work with incomplete composite location
  descriptions, then would need to define that the composite location storage
  specified by the incomplete composite location description is also replicated
  when a copy is pushed. This ensures that each copy of the incomplete composite
  location description can update the composite location storage they specify
  independently.

1.  ``DW_OP_dup``

    ``DW_OP_dup`` duplicates the stack entry at the top of the stack.

2.  ``DW_OP_drop``

    ``DW_OP_drop`` pops the stack entry at the top of the stack and discards it.

3.  ``DW_OP_pick``

    ``DW_OP_pick`` has a single unsigned 1-byte operand that represents an index
    I. A copy of the stack entry with index I is pushed onto the stack.

4.  ``DW_OP_over``

    ``DW_OP_over`` pushes a copy of the entry with index 1.

    *This is equivalent to a* ``DW_OP_pick 1`` *operation.*

5.  ``DW_OP_swap``

    ``DW_OP_swap`` swaps the top two stack entries. The entry at the top of the
    stack becomes the second stack entry, and the second stack entry becomes the
    top of the stack.

6.  ``DW_OP_rot``

    ``DW_OP_rot`` rotates the first three stack entries. The entry at the top of
    the stack becomes the third stack entry, the second entry becomes the top of
    the stack, and the third entry becomes the second entry.

*Examples illustrating many of these stack operations are found in Appendix
D.1.2 on page 289.*

.. _amdgpu-dwarf-control-flow-operations:

A.2.5.4.2 Control Flow Operations
#################################

.. note::

  This section replaces DWARF Version 5 section 2.5.1.5.

The following operations provide simple control of the flow of a DWARF operation
expression.

1.  ``DW_OP_nop``

    ``DW_OP_nop`` is a place holder. It has no effect on the DWARF stack
    entries.

2.  ``DW_OP_le``, ``DW_OP_ge``, ``DW_OP_eq``, ``DW_OP_lt``, ``DW_OP_gt``,
    ``DW_OP_ne``

    .. note::

      The same as in DWARF Version 5 section 2.5.1.5.

3.  ``DW_OP_skip``

    ``DW_OP_skip`` is an unconditional branch. Its single operand is a 2-byte
    signed integer constant. The 2-byte constant is the number of bytes of the
    DWARF expression to skip forward or backward from the current operation,
    beginning after the 2-byte constant.

    If the updated position is at one past the end of the last operation, then
    the operation expression evaluation is complete.

    Otherwise, the DWARF expression is ill-formed if the updated operation
    position is not in the range of the first to last operation inclusive, or
    not at the start of an operation.

4.  ``DW_OP_bra``

    ``DW_OP_bra`` is a conditional branch. Its single operand is a 2-byte signed
    integer constant. This operation pops the top of stack. If the value popped
    is not the constant 0, the 2-byte constant operand is the number of bytes of
    the DWARF operation expression to skip forward or backward from the current
    operation, beginning after the 2-byte constant.

    If the updated position is at one past the end of the last operation, then
    the operation expression evaluation is complete.

    Otherwise, the DWARF expression is ill-formed if the updated operation
    position is not in the range of the first to last operation inclusive, or
    not at the start of an operation.

5.  ``DW_OP_call2, DW_OP_call4, DW_OP_call_ref``

    ``DW_OP_call2``, ``DW_OP_call4``, and ``DW_OP_call_ref`` perform DWARF
    procedure calls during evaluation of a DWARF operation expression.

    ``DW_OP_call2`` and ``DW_OP_call4``, have one operand that is, respectively,
    a 2-byte or 4-byte unsigned offset DR that represents the byte offset of a
    debugging information entry D relative to the beginning of the current
    compilation unit.

    ``DW_OP_call_ref`` has one operand that is a 4-byte unsigned value in the
    32-bit DWARF format, or an 8-byte unsigned value in the 64-bit DWARF format,
    that represents the byte offset DR of a debugging information entry D
    relative to the beginning of the ``.debug_info`` section that contains the
    current compilation unit. D may not be in the current compilation unit.

    .. note::

      DWARF Version 5 states that DR can be an offset in a ``.debug_info``
      section other than the one that contains the current compilation unit. It
      states that relocation of references from one executable or shared object
      file to another must be performed by the consumer. But given that DR is
      defined as an offset in a ``.debug_info`` section this seems impossible.
      If DR was defined as an implementation defined value, then the consumer
      could choose to interpret the value in an implementation defined manner to
      reference a debug information in another executable or shared object.

      In ELF the ``.debug_info`` section is in a non-\ ``PT_LOAD`` segment so
      standard dynamic relocations cannot be used. But even if they were loaded
      segments and dynamic relocations were used, DR would need to be the
      address of D, not an offset in a ``.debug_info`` section. That would also
      need DR to be the size of a global address. So it would not be possible to
      use the 32-bit DWARF format in a 64-bit global address space. In addition,
      the consumer would need to determine what executable or shared object the
      relocated address was in so it could determine the containing compilation
      unit.

      GDB only interprets DR as an offset in the ``.debug_info`` section that
      contains the current compilation unit.

      This comment also applies to ``DW_OP_implicit_pointer`` and
      ``DW_OP_LLVM_aspace_implicit_pointer``.

    *Operand interpretation of* ``DW_OP_call2``\ *,* ``DW_OP_call4``\ *, and*
    ``DW_OP_call_ref`` *is exactly like that for* ``DW_FORM_ref2``\ *,
    ``DW_FORM_ref4``\ *, and* ``DW_FORM_ref_addr``\ *, respectively.*

    The call operation is evaluated by:

    * If D has a ``DW_AT_location`` attribute that is encoded as a ``exprloc``
      that specifies an operation expression E, then execution of the current
      operation expression continues from the first operation of E. Execution
      continues until one past the last operation of E is reached, at which
      point execution continues with the operation following the call operation.
      The operations of E are evaluated with the same current context, except
      current compilation unit is the one that contains D and the stack is the
      same as that being used by the call operation. After the call operation
      has been evaluated, the stack is therefore as it is left by the evaluation
      of the operations of E. Since E is evaluated on the same stack as the call
      operation, E can use, and/or remove entries already on the stack, and can
      add new entries to the stack.

      *Values on the stack at the time of the call may be used as parameters by
      the called expression and values left on the stack by the called expression
      may be used as return values by prior agreement between the calling and
      called expressions.*

    * If D has a ``DW_AT_location`` attribute that is encoded as a ``loclist`` or
      ``loclistsptr``, then the specified location list expression E is
      evaluated. The evaluation of E uses the current context, except the result
      kind is a location description, the compilation unit is the one that
      contains D, and the initial stack is empty. The location description
      result is pushed on the stack.

      .. note::

        This rule avoids having to define how to execute a matched location list
        entry operation expression on the same stack as the call when there are
        multiple matches. But it allows the call to obtain the location
        description for a variable or formal parameter which may use a location
        list expression.

        An alternative is to treat the case when D has a ``DW_AT_location``
        attribute that is encoded as a ``loclist`` or ``loclistsptr``, and the
        specified location list expression E' matches a single location list
        entry with operation expression E, the same as the ``exprloc`` case and
        evaluate on the same stack.

        But this is not attractive as if the attribute is for a variable that
        happens to end with a non-singleton stack, it will not simply put a
        location description on the stack. Presumably the intent of using
        ``DW_OP_call*`` on a variable or formal parameter debugger information
        entry is to push just one location description on the stack. That
        location description may have more than one single location description.

        The previous rule for ``exprloc`` also has the same problem, as normally
        a variable or formal parameter location expression may leave multiple
        entries on the stack and only return the top entry.

        GDB implements ``DW_OP_call*`` by always executing E on the same stack.
        If the location list has multiple matching entries, it simply picks the
        first one and ignores the rest. This seems fundamentally at odds with
        the desire to support multiple places for variables.

        So, it feels like ``DW_OP_call*`` should both support pushing a location
        description on the stack for a variable or formal parameter, and also
        support being able to execute an operation expression on the same stack.
        Being able to specify a different operation expression for different
        program locations seems a desirable feature to retain.

        A solution to that is to have a distinct ``DW_AT_LLVM_proc`` attribute
        for the ``DW_TAG_dwarf_procedure`` debugging information entry. Then the
        ``DW_AT_location`` attribute expression is always executed separately
        and pushes a location description (that may have multiple single
        location descriptions), and the ``DW_AT_LLVM_proc`` attribute expression
        is always executed on the same stack and can leave anything on the
        stack.

        The ``DW_AT_LLVM_proc`` attribute could have the new classes
        ``exprproc``, ``loclistproc``, and ``loclistsptrproc`` to indicate that
        the expression is executed on the same stack. ``exprproc`` is the same
        encoding as ``exprloc``. ``loclistproc`` and ``loclistsptrproc`` are the
        same encoding as their non-\ ``proc`` counterparts, except the DWARF is
        ill-formed if the location list does not match exactly one location list
        entry and a default entry is required. These forms indicate explicitly
        that the matched single operation expression must be executed on the
        same stack. This is better than ad hoc special rules for ``loclistproc``
        and ``loclistsptrproc`` which are currently clearly defined to always
        return a location description. The producer then explicitly indicates
        the intent through the attribute classes.

        Such a change would be a breaking change for how GDB implements
        ``DW_OP_call*``. However, are the breaking cases actually occurring in
        practice? GDB could implement the current approach for DWARF Version 5,
        and the new semantics for DWARF Version 6 which has been done for some
        other features.

        Another option is to limit the execution to be on the same stack only to
        the evaluation of an expression E that is the value of a
        ``DW_AT_location`` attribute of a ``DW_TAG_dwarf_procedure`` debugging
        information entry. The DWARF would be ill-formed if E is a location list
        expression that does not match exactly one location list entry. In all
        other cases the evaluation of an expression E that is the value of a
        ``DW_AT_location`` attribute would evaluate E with the current context,
        except the result kind is a location description, the compilation unit
        is the one that contains D, and the initial stack is empty. The location
        description result is pushed on the stack.

    * If D has a ``DW_AT_const_value`` attribute with a value V, then it is as
      if a ``DW_OP_implicit_value V`` operation was executed.

      *This allows a call operation to be used to compute the location
      description for any variable or formal parameter regardless of whether the
      producer has optimized it to a constant. This is consistent with the*
      ``DW_OP_implicit_pointer`` *operation.*

      .. note::

        Alternatively, could deprecate using ``DW_AT_const_value`` for
        ``DW_TAG_variable`` and ``DW_TAG_formal_parameter`` debugger information
        entries that are constants and instead use ``DW_AT_location`` with an
        operation expression that results in a location description with one
        implicit location description. Then this rule would not be required.

    * Otherwise, there is no effect and no changes are made to the stack.

      .. note::

        In DWARF Version 5, if D does not have a ``DW_AT_location`` then
        ``DW_OP_call*`` is defined to have no effect. It is unclear that this is
        the right definition as a producer should be able to rely on using
        ``DW_OP_call*`` to get a location description for any non-\
        ``DW_TAG_dwarf_procedure`` debugging information entries. Also, the
        producer should not be creating DWARF with ``DW_OP_call*`` to a
        ``DW_TAG_dwarf_procedure`` that does not have a ``DW_AT_location``
        attribute. So, should this case be defined as an ill-formed DWARF
        expression?

    *The* ``DW_TAG_dwarf_procedure`` *debugging information entry can be used to
    define DWARF procedures that can be called.*

.. _amdgpu-dwarf-value-operations:

A.2.5.4.3 Value Operations
##########################

This section describes the operations that push values on the stack.

Each value stack entry has a type and a literal value. It can represent a
literal value of any supported base type of the target architecture. The base
type specifies the size, encoding, and endianity of the literal value.

The base type of value stack entries can be the distinguished generic type.

.. _amdgpu-dwarf-literal-operations:

A.2.5.4.3.1 Literal Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces DWARF Version 5 section 2.5.1.1.

The following operations all push a literal value onto the DWARF stack.

Operations other than ``DW_OP_const_type`` push a value V with the generic type.
If V is larger than the generic type, then V is truncated to the generic type
size and the low-order bits used.

1.  ``DW_OP_lit0``, ``DW_OP_lit1``, ..., ``DW_OP_lit31``

    ``DW_OP_lit<N>`` operations encode an unsigned literal value N from 0
    through 31, inclusive. They push the value N with the generic type.

2.  ``DW_OP_const1u``, ``DW_OP_const2u``, ``DW_OP_const4u``, ``DW_OP_const8u``

    ``DW_OP_const<N>u`` operations have a single operand that is a 1, 2, 4, or
    8-byte unsigned integer constant U, respectively. They push the value U with
    the generic type.

3.  ``DW_OP_const1s``, ``DW_OP_const2s``, ``DW_OP_const4s``, ``DW_OP_const8s``

    ``DW_OP_const<N>s`` operations have a single operand that is a 1, 2, 4, or
    8-byte signed integer constant S, respectively. They push the value S with
    the generic type.

4.  ``DW_OP_constu``

    ``DW_OP_constu`` has a single unsigned LEB128 integer operand N. It pushes
    the value N with the generic type.

5.  ``DW_OP_consts``

    ``DW_OP_consts`` has a single signed LEB128 integer operand N. It pushes the
    value N with the generic type.

6.  ``DW_OP_constx``

    ``DW_OP_constx`` has a single unsigned LEB128 integer operand that
    represents a zero-based index into the ``.debug_addr`` section relative to
    the value of the ``DW_AT_addr_base`` attribute of the associated compilation
    unit. The value N in the ``.debug_addr`` section has the size of the generic
    type. It pushes the value N with the generic type.

    *The* ``DW_OP_constx`` *operation is provided for constants that require
    link-time relocation but should not be interpreted by the consumer as a
    relocatable address (for example, offsets to thread-local storage).*

7.  ``DW_OP_const_type``

    ``DW_OP_const_type`` has three operands. The first is an unsigned LEB128
    integer DR that represents the byte offset of a debugging information entry
    D relative to the beginning of the current compilation unit, that provides
    the type T of the constant value. The second is a 1-byte unsigned integral
    constant S. The third is a block of bytes B, with a length equal to S.

    TS is the bit size of the type T. The least significant TS bits of B are
    interpreted as a value V of the type D. It pushes the value V with the type
    D.

    The DWARF is ill-formed if D is not a ``DW_TAG_base_type`` debugging
    information entry in the current compilation unit, or if TS divided by 8
    (the byte size) and rounded up to a whole number is not equal to S.

    *While the size of the byte block B can be inferred from the type D
    definition, it is encoded explicitly into the operation so that the
    operation can be parsed easily without reference to the* ``.debug_info``
    *section.*

8.  ``DW_OP_LLVM_push_lane`` *New*

    ``DW_OP_LLVM_push_lane`` pushes the current lane as a value with the generic
    type.

    *For source languages that are implemented using a SIMT execution model,
    this is the zero-based lane number that corresponds to the source language
    thread of execution upon which the user is focused.*

    The value must be greater than or equal to 0 and less than the value of the
    ``DW_AT_LLVM_lanes`` attribute, otherwise the DWARF expression is
    ill-formed. See :ref:`amdgpu-dwarf-low-level-information`.

9.  ``DW_OP_LLVM_push_iteration`` *New*

    ``DW_OP_LLVM_push_iteration`` pushes the current iteration as a value with
    the generic type.

    *For source language implementations with optimizations that cause multiple
    loop iterations to execute concurrently, this is the zero-based iteration
    number that corresponds to the source language concurrent loop iteration
    upon which the user is focused.*

    The value must be greater than or equal to 0 and less than the value of the
    ``DW_AT_LLVM_iterations`` attribute, otherwise the DWARF expression is
    ill-formed. See :ref:`amdgpu-dwarf-low-level-information`.

.. _amdgpu-dwarf-arithmetic-logical-operations:

A.2.5.4.3.2 Arithmetic and Logical Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section is the same as DWARF Version 5 section 2.5.1.4.

.. _amdgpu-dwarf-type-conversions-operations:

A.2.5.4.3.3 Type Conversion Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section is the same as DWARF Version 5 section 2.5.1.6.

.. _amdgpu-dwarf-general-operations:

A.2.5.4.3.4 Special Value Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces parts of DWARF Version 5 sections 2.5.1.2, 2.5.1.3, and
  2.5.1.7.

There are these special value operations currently defined:

1.  ``DW_OP_regval_type``

    ``DW_OP_regval_type`` has two operands. The first is an unsigned LEB128
    integer that represents a register number R. The second is an unsigned
    LEB128 integer DR that represents the byte offset of a debugging information
    entry D relative to the beginning of the current compilation unit, that
    provides the type T of the register value.

    The operation is equivalent to performing ``DW_OP_regx R; DW_OP_deref_type
    DR``.

    .. note::

      Should DWARF allow the type T to be a larger size than the size of the
      register R? Restricting a larger bit size avoids any issue of conversion
      as the, possibly truncated, bit contents of the register is simply
      interpreted as a value of T. If a conversion is wanted it can be done
      explicitly using a ``DW_OP_convert`` operation.

      GDB has a per register hook that allows a target specific conversion on a
      register by register basis. It defaults to truncation of bigger registers.
      Removing use of the target hook does not cause any test failures in common
      architectures. If the compiler for a target architecture did want some
      form of conversion, including a larger result type, it could always
      explicitly use the ``DW_OP_convert`` operation.

      If T is a larger type than the register size, then the default GDB
      register hook reads bytes from the next register (or reads out of bounds
      for the last register!). Removing use of the target hook does not cause
      any test failures in common architectures (except an illegal hand written
      assembly test). If a target architecture requires this behavior, these
      extensions allow a composite location description to be used to combine
      multiple registers.

2.  ``DW_OP_deref``

    S is the bit size of the generic type divided by 8 (the byte size) and
    rounded up to a whole number. DR is the offset of a hypothetical debug
    information entry D in the current compilation unit for a base type of the
    generic type.

    The operation is equivalent to performing ``DW_OP_deref_type S, DR``.

3.  ``DW_OP_deref_size``

    ``DW_OP_deref_size`` has a single 1-byte unsigned integral constant that
    represents a byte result size S.

    TS is the smaller of the generic type bit size and S scaled by 8 (the byte
    size). If TS is smaller than the generic type bit size then T is an unsigned
    integral type of bit size TS, otherwise T is the generic type. DR is the
    offset of a hypothetical debug information entry D in the current
    compilation unit for a base type T.

    .. note::

      Truncating the value when S is larger than the generic type matches what
      GDB does. This allows the generic type size to not be an integral byte
      size. It does allow S to be arbitrarily large. Should S be restricted to
      the size of the generic type rounded up to a multiple of 8?

    The operation is equivalent to performing ``DW_OP_deref_type S, DR``, except
    if T is not the generic type, the value V pushed is zero-extended to the
    generic type bit size and its type changed to the generic type.

4.  ``DW_OP_deref_type``

    ``DW_OP_deref_type`` has two operands. The first is a 1-byte unsigned
    integral constant S. The second is an unsigned LEB128 integer DR that
    represents the byte offset of a debugging information entry D relative to
    the beginning of the current compilation unit, that provides the type T of
    the result value.

    TS is the bit size of the type T.

    *While the size of the pushed value V can be inferred from the type T, it is
    encoded explicitly as the operand S so that the operation can be parsed
    easily without reference to the* ``.debug_info`` *section.*

    .. note::

      It is unclear why the operand S is needed. Unlike ``DW_OP_const_type``,
      the size is not needed for parsing. Any evaluation needs to get the base
      type T to push with the value to know its encoding and bit size.

    It pops one stack entry that must be a location description L.

    A value V of TS bits is retrieved from the location storage LS specified by
    one of the single location descriptions SL of L.

    *If L, or the location description of any composite location description
    part that is a subcomponent of L, has more than one single location
    description, then any one of them can be selected as they are required to
    all have the same value. For any single location description SL, bits are
    retrieved from the associated storage location starting at the bit offset
    specified by SL. For a composite location description, the retrieved bits
    are the concatenation of the N bits from each composite location part PL,
    where N is limited to the size of PL.*

    V is pushed on the stack with the type T.

    .. note::

      This definition makes it an evaluation error if L is a register location
      description that has less than TS bits remaining in the register storage.
      Particularly since these extensions extend location descriptions to have
      a bit offset, it would be odd to define this as performing sign extension
      based on the type, or be target architecture dependent, as the number of
      remaining bits could be any number. This matches the GDB implementation
      for ``DW_OP_deref_type``.

      These extensions define ``DW_OP_*breg*`` in terms of
      ``DW_OP_regval_type``. ``DW_OP_regval_type`` is defined in terms of
      ``DW_OP_regx``, which uses a 0 bit offset, and ``DW_OP_deref_type``.
      Therefore, it requires the register size to be greater or equal to the
      address size of the address space. This matches the GDB implementation for
      ``DW_OP_*breg*``.

    The DWARF is ill-formed if D is not in the current compilation unit, D is
    not a ``DW_TAG_base_type`` debugging information entry, or if TS divided by
    8 (the byte size) and rounded up to a whole number is not equal to S.

    .. note::

      This definition allows the base type to be a bit size since there seems no
      reason to restrict it.

    It is an evaluation error if any bit of the value is retrieved from the
    undefined location storage or the offset of any bit exceeds the size of the
    location storage LS specified by any single location description SL of L.

    See :ref:`amdgpu-dwarf-implicit-location-description-operations` for special
    rules concerning implicit location descriptions created by the
    ``DW_OP_implicit_pointer`` and ``DW_OP_LLVM_aspace_implicit_pointer``
    operations.

5.  ``DW_OP_xderef`` *Deprecated*

    ``DW_OP_xderef`` pops two stack entries. The first must be an integral type
    value that represents an address A. The second must be an integral type
    value that represents a target architecture specific address space
    identifier AS.

    The operation is equivalent to performing ``DW_OP_swap;
    DW_OP_LLVM_form_aspace_address; DW_OP_deref``. The value V retrieved is left
    on the stack with the generic type.

    *This operation is deprecated as the* ``DW_OP_LLVM_form_aspace_address``
    *operation can be used and provides greater expressiveness.*

6.  ``DW_OP_xderef_size`` *Deprecated*

    ``DW_OP_xderef_size`` has a single 1-byte unsigned integral constant that
    represents a byte result size S.

    It pops two stack entries. The first must be an integral type value that
    represents an address A. The second must be an integral type value that
    represents a target architecture specific address space identifier AS.

    The operation is equivalent to performing ``DW_OP_swap;
    DW_OP_LLVM_form_aspace_address; DW_OP_deref_size S``. The zero-extended
    value V retrieved is left on the stack with the generic type.

    *This operation is deprecated as the* ``DW_OP_LLVM_form_aspace_address``
    *operation can be used and provides greater expressiveness.*

7.  ``DW_OP_xderef_type`` *Deprecated*

    ``DW_OP_xderef_type`` has two operands. The first is a 1-byte unsigned
    integral constant S. The second operand is an unsigned LEB128 integer DR
    that represents the byte offset of a debugging information entry D relative
    to the beginning of the current compilation unit, that provides the type T
    of the result value.

    It pops two stack entries. The first must be an integral type value that
    represents an address A. The second must be an integral type value that
    represents a target architecture specific address space identifier AS.

    The operation is equivalent to performing ``DW_OP_swap;
    DW_OP_LLVM_form_aspace_address; DW_OP_deref_type S DR``. The value V
    retrieved is left on the stack with the type T.

    *This operation is deprecated as the* ``DW_OP_LLVM_form_aspace_address``
    *operation can be used and provides greater expressiveness.*

8.  ``DW_OP_entry_value`` *Deprecated*

    ``DW_OP_entry_value`` pushes the value of an expression that is evaluated in
    the context of the calling frame.

    *It may be used to determine the value of arguments on entry to the current
    call frame provided they are not clobbered.*

    It has two operands. The first is an unsigned LEB128 integer S. The second
    is a block of bytes, with a length equal S, interpreted as a DWARF
    operation expression E.

    E is evaluated with the current context, except the result kind is
    unspecified, the call frame is the one that called the current frame, the
    program location is the call site in the calling frame, the object is
    unspecified, and the initial stack is empty. The calling frame information
    is obtained by virtually unwinding the current call frame using the call
    frame information (see :ref:`amdgpu-dwarf-call-frame-information`).

    If the result of E is a location description L (see
    :ref:`amdgpu-dwarf-register-location-description-operations`), and the last
    operation executed by E is a ``DW_OP_reg*`` for register R with a target
    architecture specific base type of T, then the contents of the register are
    retrieved as if a ``DW_OP_deref_type DR`` operation was performed where DR
    is the offset of a hypothetical debug information entry in the current
    compilation unit for T. The resulting value V s pushed on the stack.

    *Using* ``DW_OP_reg*`` *provides a more compact form for the case where the
    value was in a register on entry to the subprogram.*

    .. note::

      It is unclear how this provides a more compact expression, as
      ``DW_OP_regval_type`` could be used which is marginally larger.

    If the result of E is a value V, then V is pushed on the stack.

    Otherwise, the DWARF expression is ill-formed.

    *The* ``DW_OP_entry_value`` *operation is deprecated as its main usage is
    provided by other means. DWARF Version 5 added the*
    ``DW_TAG_call_site_parameter`` *debugger information entry for call sites
    that has* ``DW_AT_call_value``\ *,* ``DW_AT_call_data_location``\ *, and*
    ``DW_AT_call_data_value`` *attributes that provide DWARF expressions to
    compute actual parameter values at the time of the call, and requires the
    producer to ensure the expressions are valid to evaluate even when virtually
    unwound. The* ``DW_OP_LLVM_call_frame_entry_reg`` *operation provides access
    to registers in the virtually unwound calling frame.*

    .. note::

      GDB only implements ``DW_OP_entry_value`` when E is exactly
      ``DW_OP_reg*`` or ``DW_OP_breg*; DW_OP_deref*``.

.. _amdgpu-dwarf-location-description-operations:

A.2.5.4.4 Location Description Operations
#########################################

This section describes the operations that push location descriptions on the
stack.

.. _amdgpu-dwarf-general-location-description-operations:

A.2.5.4.4.1 General Location Description Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces part of DWARF Version 5 section 2.5.1.3.

1.  ``DW_OP_LLVM_offset`` *New*

    ``DW_OP_LLVM_offset`` pops two stack entries. The first must be an integral
    type value that represents a byte displacement B. The second must be a
    location description L.

    It adds the value of B scaled by 8 (the byte size) to the bit offset of each
    single location description SL of L, and pushes the updated L.

    It is an evaluation error if the updated bit offset of any SL is less than 0
    or greater than or equal to the size of the location storage specified by
    SL.

2.  ``DW_OP_LLVM_offset_uconst`` *New*

    ``DW_OP_LLVM_offset_uconst`` has a single unsigned LEB128 integer operand
    that represents a byte displacement B.

    The operation is equivalent to performing ``DW_OP_constu B;
    DW_OP_LLVM_offset``.

    *This operation is supplied specifically to be able to encode more field
    displacements in two bytes than can be done with* ``DW_OP_lit*;
    DW_OP_LLVM_offset``\ *.*

    .. note::

      Should this be named ``DW_OP_LLVM_offset_uconst`` to match
      ``DW_OP_plus_uconst``, or ``DW_OP_LLVM_offset_constu`` to match
      ``DW_OP_constu``?

3.  ``DW_OP_LLVM_bit_offset`` *New*

    ``DW_OP_LLVM_bit_offset`` pops two stack entries. The first must be an
    integral type value that represents a bit displacement B. The second must be
    a location description L.

    It adds the value of B to the bit offset of each single location description
    SL of L, and pushes the updated L.

    It is an evaluation error if the updated bit offset of any SL is less than 0
    or greater than or equal to the size of the location storage specified by
    SL.

4.  ``DW_OP_push_object_address``

    ``DW_OP_push_object_address`` pushes the location description L of the
    current object.

    *This object may correspond to an independent variable that is part of a
    user presented expression that is being evaluated. The object location
    description may be determined from the variable's own debugging information
    entry or it may be a component of an array, structure, or class whose
    address has been dynamically determined by an earlier step during user
    expression evaluation.*

    *This operation provides explicit functionality (especially for arrays
    involving descriptors) that is analogous to the implicit push of the base
    location description of a structure prior to evaluation of a*
    ``DW_AT_data_member_location`` *to access a data member of a structure.*

    .. note::

      This operation could be removed and the object location description
      specified as the initial stack as for ``DW_AT_data_member_location``.

      Or this operation could be used instead of needing to specify an initial
      stack. The latter approach is more composable as access to the object may
      be needed at any point of the expression, and passing it as the initial
      stack requires the entire expression to be aware where on the stack it is.
      If this were done, ``DW_AT_use_location`` would require a
      ``DW_OP_push_object2_address`` operation for the second object.

      Or a more general way to pass an arbitrary number of arguments in and an
      operation to get the Nth one such as ``DW_OP_arg N``. A vector of
      arguments would then be passed in the expression context rather than an
      initial stack. This could also resolve the issues with ``DW_OP_call*`` by
      allowing a specific number of arguments passed in and returned to be
      specified. The ``DW_OP_call*`` operation could then always execute on a
      separate stack: the number of arguments would be specified in a new call
      operation and taken from the callers stack, and similarly the number of
      return results specified and copied from the called stack back to the
      callee stack when the called expression was complete.

      The only attribute that specifies a current object is
      ``DW_AT_data_location`` so the non-normative text seems to overstate how
      this is being used. Or are there other attributes that need to state they
      pass an object?

5.  ``DW_OP_LLVM_call_frame_entry_reg`` *New*

    ``DW_OP_LLVM_call_frame_entry_reg`` has a single unsigned LEB128 integer
    operand that represents a target architecture register number R.

    It pushes a location description L that holds the value of register R on
    entry to the current subprogram as defined by the call frame information
    (see :ref:`amdgpu-dwarf-call-frame-information`).

    *If there is no call frame information defined, then the default rules for
    the target architecture are used. If the register rule is* undefined\ *, then
    the undefined location description is pushed. If the register rule is* same
    value\ *, then a register location description for R is pushed.*

.. _amdgpu-dwarf-undefined-location-description-operations:

A.2.5.4.4.2 Undefined Location Description Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces DWARF Version 5 section 2.6.1.1.1.

*The undefined location storage represents a piece or all of an object that is
present in the source but not in the object code (perhaps due to optimization).
Neither reading nor writing to the undefined location storage is meaningful.*

An undefined location description specifies the undefined location storage.
There is no concept of the size of the undefined location storage, nor of a bit
offset for an undefined location description. The ``DW_OP_LLVM_*offset``
operations leave an undefined location description unchanged. The
``DW_OP_*piece`` operations can explicitly or implicitly specify an undefined
location description, allowing any size and offset to be specified, and results
in a part with all undefined bits.

1.  ``DW_OP_LLVM_undefined`` *New*

    ``DW_OP_LLVM_undefined`` pushes a location description L that comprises one
    undefined location description SL.

.. _amdgpu-dwarf-memory-location-description-operations:

A.2.5.4.4.3 Memory Location Description Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces parts of DWARF Version 5 section 2.5.1.1, 2.5.1.2,
  2.5.1.3, and 2.6.1.1.2.

Each of the target architecture specific address spaces has a corresponding
memory location storage that denotes the linear addressable memory of that
address space. The size of each memory location storage corresponds to the range
of the addresses in the corresponding address space.

*It is target architecture defined how address space location storage maps to
target architecture physical memory. For example, they may be independent
memory, or more than one location storage may alias the same physical memory
possibly at different offsets and with different interleaving. The mapping may
also be dictated by the source language address classes.*

A memory location description specifies a memory location storage. The bit
offset corresponds to a bit position within a byte of the memory. Bits accessed
using a memory location description, access the corresponding target
architecture memory starting at the bit position within the byte specified by
the bit offset.

A memory location description that has a bit offset that is a multiple of 8 (the
byte size) is defined to be a byte address memory location description. It has a
memory byte address A that is equal to the bit offset divided by 8.

A memory location description that does not have a bit offset that is a multiple
of 8 (the byte size) is defined to be a bit field memory location description.
It has a bit position B equal to the bit offset modulo 8, and a memory byte
address A equal to the bit offset minus B that is then divided by 8.

The address space AS of a memory location description is defined to be the
address space that corresponds to the memory location storage associated with
the memory location description.

A location description that is comprised of one byte address memory location
description SL is defined to be a memory byte address location description. It
has a byte address equal to A and an address space equal to AS of the
corresponding SL.

``DW_ASPACE_LLVM_none`` is defined as the target architecture default address
space. See :ref:`amdgpu-dwarf-address-spaces`.

If a stack entry is required to be a location description, but it is a value V
with the generic type, then it is implicitly converted to a location description
L with one memory location description SL. SL specifies the memory location
storage that corresponds to the target architecture default address space with a
bit offset equal to V scaled by 8 (the byte size).

.. note::

  If it is wanted to allow any integral type value to be implicitly converted to
  a memory location description in the target architecture default address
  space:

    If a stack entry is required to be a location description, but is a value V
    with an integral type, then it is implicitly converted to a location
    description L with a one memory location description SL. If the type size of
    V is less than the generic type size, then the value V is zero extended to
    the size of the generic type. The least significant generic type size bits
    are treated as an unsigned value to be used as an address A. SL specifies
    memory location storage corresponding to the target architecture default
    address space with a bit offset equal to A scaled by 8 (the byte size).

  The implicit conversion could also be defined as target architecture specific.
  For example, GDB checks if V is an integral type. If it is not it gives an
  error. Otherwise, GDB zero-extends V to 64 bits. If the GDB target defines a
  hook function, then it is called. The target specific hook function can modify
  the 64-bit value, possibly sign extending based on the original value type.
  Finally, GDB treats the 64-bit value V as a memory location address.

If a stack entry is required to be a location description, but it is an implicit
pointer value IPV with the target architecture default address space, then it is
implicitly converted to a location description with one single location
description specified by IPV. See
:ref:`amdgpu-dwarf-implicit-location-description-operations`.

.. note::

  Is this rule required for DWARF Version 5 backwards compatibility? If not, it
  can be eliminated, and the producer can use
  ``DW_OP_LLVM_form_aspace_address``.

If a stack entry is required to be a value, but it is a location description L
with one memory location description SL in the target architecture default
address space with a bit offset B that is a multiple of 8, then it is implicitly
converted to a value equal to B divided by 8 (the byte size) with the generic
type.

1.  ``DW_OP_addr``

    ``DW_OP_addr`` has a single byte constant value operand, which has the size
    of the generic type, that represents an address A.

    It pushes a location description L with one memory location description SL
    on the stack. SL specifies the memory location storage corresponding to the
    target architecture default address space with a bit offset equal to A
    scaled by 8 (the byte size).

    *If the DWARF is part of a code object, then A may need to be relocated. For
    example, in the ELF code object format, A must be adjusted by the difference
    between the ELF segment virtual address and the virtual address at which the
    segment is loaded.*

2.  ``DW_OP_addrx``

    ``DW_OP_addrx`` has a single unsigned LEB128 integer operand that represents
    a zero-based index into the ``.debug_addr`` section relative to the value of
    the ``DW_AT_addr_base`` attribute of the associated compilation unit. The
    address value A in the ``.debug_addr`` section has the size of the generic
    type.

    It pushes a location description L with one memory location description SL
    on the stack. SL specifies the memory location storage corresponding to the
    target architecture default address space with a bit offset equal to A
    scaled by 8 (the byte size).

    *If the DWARF is part of a code object, then A may need to be relocated. For
    example, in the ELF code object format, A must be adjusted by the difference
    between the ELF segment virtual address and the virtual address at which the
    segment is loaded.*

3.  ``DW_OP_LLVM_form_aspace_address`` *New*

    ``DW_OP_LLVM_form_aspace_address`` pops top two stack entries. The first
    must be an integral type value that represents a target architecture
    specific address space identifier AS. The second must be an integral type
    value that represents an address A.

    The address size S is defined as the address bit size of the target
    architecture specific address space that corresponds to AS.

    A is adjusted to S bits by zero extending if necessary, and then treating
    the least significant S bits as an unsigned value A'.

    It pushes a location description L with one memory location description SL
    on the stack. SL specifies the memory location storage LS that corresponds
    to AS with a bit offset equal to A' scaled by 8 (the byte size).

    If AS is an address space that is specific to context elements, then LS
    corresponds to the location storage associated with the current context.

    *For example, if AS is for per thread storage then LS is the location
    storage for the current thread. For languages that are implemented using a
    SIMT execution model, then if AS is for per lane storage then LS is the
    location storage for the current lane of the current thread. Therefore, if L
    is accessed by an operation, the location storage selected when the location
    description was created is accessed, and not the location storage associated
    with the current context of the access operation.*

    The DWARF expression is ill-formed if AS is not one of the values defined by
    the target architecture specific ``DW_ASPACE_LLVM_*`` values.

    See :ref:`amdgpu-dwarf-implicit-location-description-operations` for special
    rules concerning implicit pointer values produced by dereferencing implicit
    location descriptions created by the ``DW_OP_implicit_pointer`` and
    ``DW_OP_LLVM_aspace_implicit_pointer`` operations.

4.  ``DW_OP_form_tls_address``

    ``DW_OP_form_tls_address`` pops one stack entry that must be an integral
    type value and treats it as a thread-local storage address TA.

    It pushes a location description L with one memory location description SL
    on the stack. SL is the target architecture specific memory location
    description that corresponds to the thread-local storage address TA.

    The meaning of the thread-local storage address TA is defined by the
    run-time environment. If the run-time environment supports multiple
    thread-local storage blocks for a single thread, then the block
    corresponding to the executable or shared library containing this DWARF
    expression is used.

    *Some implementations of C, C++, Fortran, and other languages, support a
    thread-local storage class. Variables with this storage class have distinct
    values and addresses in distinct threads, much as automatic variables have
    distinct values and addresses in each subprogram invocation. Typically,
    there is a single block of storage containing all thread-local variables
    declared in the main executable, and a separate block for the variables
    declared in each shared library. Each thread-local variable can then be
    accessed in its block using an identifier. This identifier is typically a
    byte offset into the block and pushed onto the DWARF stack by one of the*
    ``DW_OP_const*`` *operations prior to the* ``DW_OP_form_tls_address``
    *operation. Computing the address of the appropriate block can be complex
    (in some cases, the compiler emits a function call to do it), and difficult
    to describe using ordinary DWARF location descriptions. Instead of forcing
    complex thread-local storage calculations into the DWARF expressions, the*
    ``DW_OP_form_tls_address`` *allows the consumer to perform the computation
    based on the target architecture specific run-time environment.*

5.  ``DW_OP_call_frame_cfa``

    ``DW_OP_call_frame_cfa`` pushes the location description L of the Canonical
    Frame Address (CFA) of the current subprogram, obtained from the call frame
    information on the stack. See :ref:`amdgpu-dwarf-call-frame-information`.

    *Although the value of the* ``DW_AT_frame_base`` *attribute of the debugger
    information entry corresponding to the current subprogram can be computed
    using a location list expression, in some cases this would require an
    extensive location list because the values of the registers used in
    computing the CFA change during a subprogram execution. If the call frame
    information is present, then it already encodes such changes, and it is
    space efficient to reference that using the* ``DW_OP_call_frame_cfa``
    *operation.*

6.  ``DW_OP_fbreg``

    ``DW_OP_fbreg`` has a single signed LEB128 integer operand that represents a
    byte displacement B.

    The location description L for the *frame base* of the current subprogram is
    obtained from the ``DW_AT_frame_base`` attribute of the debugger information
    entry corresponding to the current subprogram as described in
    :ref:`amdgpu-dwarf-low-level-information`.

    The location description L is updated as if the ``DW_OP_LLVM_offset_uconst
    B`` operation was applied. The updated L is pushed on the stack.

7.  ``DW_OP_breg0``, ``DW_OP_breg1``, ..., ``DW_OP_breg31``

    The ``DW_OP_breg<N>`` operations encode the numbers of up to 32 registers,
    numbered from 0 through 31, inclusive. The register number R corresponds to
    the N in the operation name.

    They have a single signed LEB128 integer operand that represents a byte
    displacement B.

    The address space identifier AS is defined as the one corresponding to the
    target architecture specific default address space.

    The address size S is defined as the address bit size of the target
    architecture specific address space corresponding to AS.

    The contents of the register specified by R are retrieved as if a
    ``DW_OP_regval_type R, DR`` operation was performed where DR is the offset
    of a hypothetical debug information entry in the current compilation unit
    for an unsigned integral base type of size S bits. B is added and the least
    significant S bits are treated as an unsigned value to be used as an address
    A.

    They push a location description L comprising one memory location
    description LS on the stack. LS specifies the memory location storage that
    corresponds to AS with a bit offset equal to A scaled by 8 (the byte size).

8.  ``DW_OP_bregx``

    ``DW_OP_bregx`` has two operands. The first is an unsigned LEB128 integer
    that represents a register number R. The second is a signed LEB128
    integer that represents a byte displacement B.

    The action is the same as for ``DW_OP_breg<N>``, except that R is used as
    the register number and B is used as the byte displacement.

9.  ``DW_OP_LLVM_aspace_bregx`` *New*

    ``DW_OP_LLVM_aspace_bregx`` has two operands. The first is an unsigned
    LEB128 integer that represents a register number R. The second is a signed
    LEB128 integer that represents a byte displacement B. It pops one stack
    entry that is required to be an integral type value that represents a target
    architecture specific address space identifier AS.

    The action is the same as for ``DW_OP_breg<N>``, except that R is used as
    the register number, B is used as the byte displacement, and AS is used as
    the address space identifier.

    The DWARF expression is ill-formed if AS is not one of the values defined by
    the target architecture specific ``DW_ASPACE_LLVM_*`` values.

    .. note::

      Could also consider adding ``DW_OP_LLVM_aspace_breg0,
      DW_OP_LLVM_aspace_breg1, ..., DW_OP_LLVM_aspace_breg31`` which would save
      encoding size.

.. _amdgpu-dwarf-register-location-description-operations:

A.2.5.4.4.4 Register Location Description Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces DWARF Version 5 section 2.6.1.1.3.

There is a register location storage that corresponds to each of the target
architecture registers. The size of each register location storage corresponds
to the size of the corresponding target architecture register.

A register location description specifies a register location storage. The bit
offset corresponds to a bit position within the register. Bits accessed using a
register location description access the corresponding target architecture
register starting at the specified bit offset.

1.  ``DW_OP_reg0``, ``DW_OP_reg1``, ..., ``DW_OP_reg31``

    ``DW_OP_reg<N>`` operations encode the numbers of up to 32 registers,
    numbered from 0 through 31, inclusive. The target architecture register
    number R corresponds to the N in the operation name.

    The operation is equivalent to performing ``DW_OP_regx R``.

2.  ``DW_OP_regx``

    ``DW_OP_regx`` has a single unsigned LEB128 integer operand that represents
    a target architecture register number R.

    If the current call frame is the top call frame, it pushes a location
    description L that specifies one register location description SL on the
    stack. SL specifies the register location storage that corresponds to R with
    a bit offset of 0 for the current thread.

    If the current call frame is not the top call frame, call frame information
    (see :ref:`amdgpu-dwarf-call-frame-information`) is used to determine the
    location description that holds the register for the current call frame and
    current program location of the current thread. The resulting location
    description L is pushed.

    *Note that if call frame information is used, the resulting location
    description may be register, memory, or undefined.*

    *An implementation may evaluate the call frame information immediately, or
    may defer evaluation until L is accessed by an operation. If evaluation is
    deferred, R and the current context can be recorded in L. When accessed, the
    recorded context is used to evaluate the call frame information, not the
    current context of the access operation.*

*These operations obtain a register location. To fetch the contents of a
register, it is necessary to use* ``DW_OP_regval_type``\ *, use one of the*
``DW_OP_breg*`` *register-based addressing operations, or use* ``DW_OP_deref*``
*on a register location description.*

.. _amdgpu-dwarf-implicit-location-description-operations:

A.2.5.4.4.5 Implicit Location Description Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces DWARF Version 5 section 2.6.1.1.4.

Implicit location storage represents a piece or all of an object which has no
actual location in the program but whose contents are nonetheless known, either
as a constant or can be computed from other locations and values in the program.

An implicit location description specifies an implicit location storage. The bit
offset corresponds to a bit position within the implicit location storage. Bits
accessed using an implicit location description, access the corresponding
implicit storage value starting at the bit offset.

1.  ``DW_OP_implicit_value``

    ``DW_OP_implicit_value`` has two operands. The first is an unsigned LEB128
    integer that represents a byte size S. The second is a block of bytes with a
    length equal to S treated as a literal value V.

    An implicit location storage LS is created with the literal value V and a
    size of S.

    It pushes location description L with one implicit location description SL
    on the stack. SL specifies LS with a bit offset of 0.

2.  ``DW_OP_stack_value``

    ``DW_OP_stack_value`` pops one stack entry that must be a value V.

    An implicit location storage LS is created with the literal value V using
    the size, encoding, and endianity specified by V's base type.

    It pushes a location description L with one implicit location description SL
    on the stack. SL specifies LS with a bit offset of 0.

    *The* ``DW_OP_stack_value`` *operation specifies that the object does not
    exist in memory, but its value is nonetheless known. In this form, the
    location description specifies the actual value of the object, rather than
    specifying the memory or register storage that holds the value.*

    See ``DW_OP_implicit_pointer`` (following) for special rules concerning
    implicit pointer values produced by dereferencing implicit location
    descriptions created by the ``DW_OP_implicit_pointer`` and
    ``DW_OP_LLVM_aspace_implicit_pointer`` operations.

    Note: Since location descriptions are allowed on the stack, the
    ``DW_OP_stack_value`` operation no longer terminates the DWARF operation
    expression execution as in DWARF Version 5.

3.  ``DW_OP_implicit_pointer``

    *An optimizing compiler may eliminate a pointer, while still retaining the
    value that the pointer addressed.* ``DW_OP_implicit_pointer`` *allows a
    producer to describe this value.*

    ``DW_OP_implicit_pointer`` *specifies an object is a pointer to the target
    architecture default address space that cannot be represented as a real
    pointer, even though the value it would point to can be described. In this
    form, the location description specifies a debugging information entry that
    represents the actual location description of the object to which the
    pointer would point. Thus, a consumer of the debug information would be able
    to access the dereferenced pointer, even when it cannot access the pointer
    itself.*

    ``DW_OP_implicit_pointer`` has two operands. The first operand is a 4-byte
    unsigned value in the 32-bit DWARF format, or an 8-byte unsigned value in
    the 64-bit DWARF format, that represents the byte offset DR of a debugging
    information entry D relative to the beginning of the ``.debug_info`` section
    that contains the current compilation unit. The second operand is a signed
    LEB128 integer that represents a byte displacement B.

    *Note that D might not be in the current compilation unit.*

    *The first operand interpretation is exactly like that for*
    ``DW_FORM_ref_addr``\ *.*

    The address space identifier AS is defined as the one corresponding to the
    target architecture specific default address space.

    The address size S is defined as the address bit size of the target
    architecture specific address space corresponding to AS.

    An implicit location storage LS is created with the debugging information
    entry D, address space AS, and size of S.

    It pushes a location description L that comprises one implicit location
    description SL on the stack. SL specifies LS with a bit offset of 0.

    It is an evaluation error if a ``DW_OP_deref*`` operation pops a location
    description L', and retrieves S bits, such that any retrieved bits come from
    an implicit location storage that is the same as LS, unless both the
    following conditions are met:

    1.  All retrieved bits come from an implicit location description that
        refers to an implicit location storage that is the same as LS.

        *Note that all bits do not have to come from the same implicit location
        description, as L' may involve composite location descriptions.*

    2.  The bits come from consecutive ascending offsets within their respective
        implicit location storage.

    *These rules are equivalent to retrieving the complete contents of LS.*

    If both the above conditions are met, then the value V pushed by the
    ``DW_OP_deref*`` operation is an implicit pointer value IPV with a target
    architecture specific address space of AS, a debugging information entry of
    D, and a base type of T. If AS is the target architecture default address
    space, then T is the generic type. Otherwise, T is a target architecture
    specific integral type with a bit size equal to S.

    If IPV is either implicitly converted to a location description (only done
    if AS is the target architecture default address space) or used by
    ``DW_OP_LLVM_form_aspace_address`` (only done if the address space popped by
    ``DW_OP_LLVM_form_aspace_address`` is AS), then the resulting location
    description RL is:

    * If D has a ``DW_AT_location`` attribute, the DWARF expression E from the
      ``DW_AT_location`` attribute is evaluated with the current context, except
      that the result kind is a location description, the compilation unit is
      the one that contains D, the object is unspecified, and the initial stack
      is empty. RL is the expression result.

      *Note that E is evaluated with the context of the expression accessing
      IPV, and not the context of the expression that contained the*
      ``DW_OP_implicit_pointer`` *or* ``DW_OP_LLVM_aspace_implicit_pointer``
      *operation that created L.*

    * If D has a ``DW_AT_const_value`` attribute, then an implicit location
      storage RLS is created from the ``DW_AT_const_value`` attribute's value
      with a size matching the size of the ``DW_AT_const_value`` attribute's
      value. RL comprises one implicit location description SRL. SRL specifies
      RLS with a bit offset of 0.

      .. note::

        If using ``DW_AT_const_value`` for variables and formal parameters is
        deprecated and instead ``DW_AT_location`` is used with an implicit
        location description, then this rule would not be required.

    * Otherwise, it is an evaluation error.

    The bit offset of RL is updated as if the ``DW_OP_LLVM_offset_uconst B``
    operation was applied.

    If a ``DW_OP_stack_value`` operation pops a value that is the same as IPV,
    then it pushes a location description that is the same as L.

    It is an evaluation error if LS or IPV is accessed in any other manner.

    *The restrictions on how an implicit pointer location description created
    by* ``DW_OP_implicit_pointer`` *and* ``DW_OP_LLVM_aspace_implicit_pointer``
    *can be used are to simplify the DWARF consumer. Similarly, for an implicit
    pointer value created by* ``DW_OP_deref*`` *and* ``DW_OP_stack_value``\ *.*

4.  ``DW_OP_LLVM_aspace_implicit_pointer`` *New*

    ``DW_OP_LLVM_aspace_implicit_pointer`` has two operands that are the same as
    for ``DW_OP_implicit_pointer``.

    It pops one stack entry that must be an integral type value that represents
    a target architecture specific address space identifier AS.

    The location description L that is pushed on the stack is the same as for
    ``DW_OP_implicit_pointer``, except that the address space identifier used is
    AS.

    The DWARF expression is ill-formed if AS is not one of the values defined by
    the target architecture specific ``DW_ASPACE_LLVM_*`` values.

    .. note::

      This definition of ``DW_OP_LLVM_aspace_implicit_pointer`` may change when
      full support for address classes is added as required for languages such
      as OpenCL/SyCL.

*Typically a* ``DW_OP_implicit_pointer`` *or*
``DW_OP_LLVM_aspace_implicit_pointer`` *operation is used in a DWARF expression
E*\ :sub:`1` *of a* ``DW_TAG_variable`` *or* ``DW_TAG_formal_parameter``
*debugging information entry D*\ :sub:`1`\ *'s* ``DW_AT_location`` *attribute.
The debugging information entry referenced by the* ``DW_OP_implicit_pointer``
*or* ``DW_OP_LLVM_aspace_implicit_pointer`` *operations is typically itself a*
``DW_TAG_variable`` *or* ``DW_TAG_formal_parameter`` *debugging information
entry D*\ :sub:`2` *whose* ``DW_AT_location`` *attribute gives a second DWARF
expression E*\ :sub:`2`\ *.*

*D*\ :sub:`1` *and E*\ :sub:`1` *are describing the location of a pointer type
object. D*\ :sub:`2` *and E*\ :sub:`2` *are describing the location of the
object pointed to by that pointer object.*

*However, D*\ :sub:`2` *may be any debugging information entry that contains a*
``DW_AT_location`` *or* ``DW_AT_const_value`` *attribute (for example,*
``DW_TAG_dwarf_procedure``\ *). By using E*\ :sub:`2`\ *, a consumer can
reconstruct the value of the object when asked to dereference the pointer
described by E*\ :sub:`1` *which contains the* ``DW_OP_implicit_pointer`` *or*
``DW_OP_LLVM_aspace_implicit_pointer`` *operation.*

.. _amdgpu-dwarf-composite-location-description-operations:

A.2.5.4.4.6 Composite Location Description Operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  This section replaces DWARF Version 5 section 2.6.1.2.

A composite location storage represents an object or value which may be
contained in part of another location storage or contained in parts of more
than one location storage.

Each part has a part location description L and a part bit size S. L can have
one or more single location descriptions SL. If there are more than one SL then
that indicates that part is located in more than one place. The bits of each
place of the part comprise S contiguous bits from the location storage LS
specified by SL starting at the bit offset specified by SL. All the bits must
be within the size of LS or the DWARF expression is ill-formed.

A composite location storage can have zero or more parts. The parts are
contiguous such that the zero-based location storage bit index will range over
each part with no gaps between them. Therefore, the size of a composite location
storage is the sum of the size of its parts. The DWARF expression is ill-formed
if the size of the contiguous location storage is larger than the size of the
memory location storage corresponding to the largest target architecture
specific address space.

A composite location description specifies a composite location storage. The bit
offset corresponds to a bit position within the composite location storage.

There are operations that create a composite location storage.

There are other operations that allow a composite location storage to be
incrementally created. Each part is created by a separate operation. There may
be one or more operations to create the final composite location storage. A
series of such operations describes the parts of the composite location storage
that are in the order that the associated part operations are executed.

To support incremental creation, a composite location storage can be in an
incomplete state. When an incremental operation operates on an incomplete
composite location storage, it adds a new part, otherwise it creates a new
composite location storage. The ``DW_OP_LLVM_piece_end`` operation explicitly
makes an incomplete composite location storage complete.

A composite location description that specifies a composite location storage
that is incomplete is termed an incomplete composite location description. A
composite location description that specifies a composite location storage that
is complete is termed a complete composite location description.

If the top stack entry is a location description that has one incomplete
composite location description SL after the execution of an operation expression
has completed, SL is converted to a complete composite location description.

*Note that this conversion does not happen after the completion of an operation
expression that is evaluated on the same stack by the* ``DW_OP_call*``
*operations. Such executions are not a separate evaluation of an operation
expression, but rather the continued evaluation of the same operation expression
that contains the* ``DW_OP_call*`` *operation.*

If a stack entry is required to be a location description L, but L has an
incomplete composite location description, then the DWARF expression is
ill-formed. The exception is for the operations involved in incrementally
creating a composite location description as described below.

*Note that a DWARF operation expression may arbitrarily compose composite
location descriptions from any other location description, including those that
have multiple single location descriptions, and those that have composite
location descriptions.*

*The incremental composite location description operations are defined to be
compatible with the definitions in DWARF Version 5.*

1.  ``DW_OP_piece``

    ``DW_OP_piece`` has a single unsigned LEB128 integer that represents a byte
    size S.

    The action is based on the context:

    * If the stack is empty, then a location description L comprised of one
      incomplete composite location description SL is pushed on the stack.

      An incomplete composite location storage LS is created with a single part
      P. P specifies a location description PL and has a bit size of S scaled by
      8 (the byte size). PL is comprised of one undefined location description
      PSL.

      SL specifies LS with a bit offset of 0.

    * Otherwise, if the top stack entry is a location description L comprised of
      one incomplete composite location description SL, then the incomplete
      composite location storage LS that SL specifies is updated to append a new
      part P. P specifies a location description PL and has a bit size of S
      scaled by 8 (the byte size). PL is comprised of one undefined location
      description PSL. L is left on the stack.

    * Otherwise, if the top stack entry is a location description or can be
      converted to one, then it is popped and treated as a part location
      description PL. Then:

      * If the top stack entry (after popping PL) is a location description L
        comprised of one incomplete composite location description SL, then the
        incomplete composite location storage LS that SL specifies is updated to
        append a new part P. P specifies the location description PL and has a
        bit size of S scaled by 8 (the byte size). L is left on the stack.

      * Otherwise, a location description L comprised of one incomplete
        composite location description SL is pushed on the stack.

        An incomplete composite location storage LS is created with a single
        part P. P specifies the location description PL and has a bit size of S
        scaled by 8 (the byte size).

        SL specifies LS with a bit offset of 0.

    * Otherwise, the DWARF expression is ill-formed

    *Many compilers store a single variable in sets of registers or store a
    variable partially in memory and partially in registers.* ``DW_OP_piece``
    *provides a way of describing where a part of a variable is located.*

    *If a non-0 byte displacement is required, the* ``DW_OP_LLVM_offset``
    *operation can be used to update the location description before using it as
    the part location description of a* ``DW_OP_piece`` *operation.*

    *The evaluation rules for the* ``DW_OP_piece`` *operation allow it to be
    compatible with the DWARF Version 5 definition.*

    .. note::

      Since these extensions allow location descriptions to be entries on the
      stack, a simpler operation to create composite location descriptions could
      be defined. For example, just one operation that specifies how many parts,
      and pops pairs of stack entries for the part size and location
      description. Not only would this be a simpler operation and avoid the
      complexities of incomplete composite location descriptions, but it may
      also have a smaller encoding in practice. However, the desire for
      compatibility with DWARF Version 5 is likely a stronger consideration.

2.  ``DW_OP_bit_piece``

    ``DW_OP_bit_piece`` has two operands. The first is an unsigned LEB128
    integer that represents the part bit size S. The second is an unsigned
    LEB128 integer that represents a bit displacement B.

    The action is the same as for ``DW_OP_piece``, except that any part created
    has the bit size S, and the location description PL of any created part is
    updated as if the ``DW_OP_constu B; DW_OP_LLVM_bit_offset`` operations were
    applied.

    ``DW_OP_bit_piece`` *is used instead of* ``DW_OP_piece`` *when the piece to
    be assembled is not byte-sized or is not at the start of the part location
    description.*

    *If a computed bit displacement is required, the* ``DW_OP_LLVM_bit_offset``
    *operation can be used to update the location description before using it as
    the part location description of a* ``DW_OP_bit_piece`` *operation.*

    .. note::

      The bit offset operand is not needed as ``DW_OP_LLVM_bit_offset`` can be
      used on the part's location description.

3.  ``DW_OP_LLVM_piece_end`` *New*

    If the top stack entry is not a location description L comprised of one
    incomplete composite location description SL, then the DWARF expression is
    ill-formed.

    Otherwise, the incomplete composite location storage LS specified by SL is
    updated to be a complete composite location description with the same parts.

4.  ``DW_OP_LLVM_extend`` *New*

    ``DW_OP_LLVM_extend`` has two operands. The first is an unsigned LEB128
    integer that represents the element bit size S. The second is an unsigned
    LEB128 integer that represents a count C.

    It pops one stack entry that must be a location description and is treated
    as the part location description PL.

    A location description L comprised of one complete composite location
    description SL is pushed on the stack.

    A complete composite location storage LS is created with C identical parts
    P. Each P specifies PL and has a bit size of S.

    SL specifies LS with a bit offset of 0.

    The DWARF expression is ill-formed if the element bit size or count are 0.

5.  ``DW_OP_LLVM_select_bit_piece`` *New*

    ``DW_OP_LLVM_select_bit_piece`` has two operands. The first is an unsigned
    LEB128 integer that represents the element bit size S. The second is an
    unsigned LEB128 integer that represents a count C.

    It pops three stack entries. The first must be an integral type value that
    represents a bit mask value M. The second must be a location description
    that represents the one-location description L1. The third must be a
    location description that represents the zero-location description L0.

    A complete composite location storage LS is created with C parts P\ :sub:`N`
    ordered in ascending N from 0 to C-1 inclusive. Each P\ :sub:`N` specifies
    location description PL\ :sub:`N` and has a bit size of S.

    PL\ :sub:`N` is as if the ``DW_OP_LLVM_bit_offset N*S`` operation was
    applied to PLX\ :sub:`N`\ .

    PLX\ :sub:`N` is the same as L0 if the N\ :sup:`th` least significant bit of
    M is a zero, otherwise it is the same as L1.

    A location description L comprised of one complete composite location
    description SL is pushed on the stack. SL specifies LS with a bit offset of
    0.

    The DWARF expression is ill-formed if S or C are 0, or if the bit size of M
    is less than C.

    .. note::

      Should the count operand for DW_OP_extend and DW_OP_select_bit_piece be
      changed to get the count value off the stack? This would allow support for
      architectures that have variable length vector instructions such as ARM
      and RISC-V.

6.  ``DW_OP_LLVM_overlay`` *New*

    ``DW_OP_LLVM_overlay`` pops four stack entries. The first must be an
    integral type value that represents the overlay byte size value S. The
    second must be an integral type value that represents the overlay byte
    offset value O. The third must be a location description that represents the
    overlay location description OL. The fourth must be a location description
    that represents the base location description BL.

    The action is the same as for ``DW_OP_LLVM_bit_overlay``, except that the
    overlay bit size BS and overlay bit offset BO used are S and O respectively
    scaled by 8 (the byte size).

7.  ``DW_OP_LLVM_bit_overlay`` *New*

    ``DW_OP_LLVM_bit_overlay`` pops four stack entries. The first must be an
    integral type value that represents the overlay bit size value BS. The
    second must be an integral type value that represents the overlay bit offset
    value BO. The third must be a location description that represents the
    overlay location description OL. The fourth must be a location description
    that represents the base location description BL.

    The DWARF expression is ill-formed if BS or BO are negative values.

    *rbss(L)* is the minimum remaining bit storage size of L which is defined as
    follows. LS is the location storage and LO is the location bit offset
    specified by a single location description SL of L. The remaining bit
    storage size RBSS of SL is the bit size of LS minus LO. *rbss(L)* is the
    minimum RBSS of each single location description SL of L.

    The DWARF expression is ill-formed if *rbss(BL)* is less than BO plus BS.

    If BS is 0, then the operation pushes BL.

    If BO is 0 and BS equals *rbss(BL)*, then the operation pushes OL.

    Otherwise, the operation is equivalent to performing the following steps to
    push a composite location description.

    *The composite location description is conceptually the base location
    description BL with the overlay location description OL positioned as an
    overlay starting at the overlay offset BO and covering overlay bit size BS.*

    1.  If BO is not 0 then push BL followed by performing the ``DW_OP_bit_piece
        BO, 0`` operation.
    2.  Push OL followed by performing the ``DW_OP_bit_piece BS, 0`` operation.
    3.  If *rbss(BL)* is greater than BO plus BS, push BL followed by performing
        the ``DW_OP_bit_piece (rbss(BL) - BO - BS), (BO + BS)`` operation.
    4.  Perform the ``DW_OP_LLVM_piece_end`` operation.

.. _amdgpu-dwarf-location-list-expressions:

A.2.5.5 DWARF Location List Expressions
+++++++++++++++++++++++++++++++++++++++

.. note::

  This section replaces DWARF Version 5 section 2.6.2.

*To meet the needs of recent computer architectures and optimization techniques,
debugging information must be able to describe the location of an object whose
location changes over the object’s lifetime, and may reside at multiple
locations during parts of an object's lifetime. Location list expressions are
used in place of operation expressions whenever the object whose location is
being described has these requirements.*

A location list expression consists of a series of location list entries. Each
location list entry is one of the following kinds:

*Bounded location description*

  This kind of location list entry provides an operation expression that
  evaluates to the location description of an object that is valid over a
  lifetime bounded by a starting and ending address. The starting address is the
  lowest address of the address range over which the location is valid. The
  ending address is the address of the first location past the highest address
  of the address range.

  The location list entry matches when the current program location is within
  the given range.

  There are several kinds of bounded location description entries which differ
  in the way that they specify the starting and ending addresses.

*Default location description*

  This kind of location list entry provides an operation expression that
  evaluates to the location description of an object that is valid when no
  bounded location description entry applies.

  The location list entry matches when the current program location is not
  within the range of any bounded location description entry.

*Base address*

  This kind of location list entry provides an address to be used as the base
  address for beginning and ending address offsets given in certain kinds of
  bounded location description entries. The applicable base address of a bounded
  location description entry is the address specified by the closest preceding
  base address entry in the same location list. If there is no preceding base
  address entry, then the applicable base address defaults to the base address
  of the compilation unit (see DWARF Version 5 section 3.1.1).

  In the case of a compilation unit where all of the machine code is contained
  in a single contiguous section, no base address entry is needed.

*End-of-list*

  This kind of location list entry marks the end of the location list
  expression.

The address ranges defined by the bounded location description entries of a
location list expression may overlap. When they do, they describe a situation in
which an object exists simultaneously in more than one place.

If all of the address ranges in a given location list expression do not
collectively cover the entire range over which the object in question is
defined, and there is no following default location description entry, it is
assumed that the object is not available for the portion of the range that is
not covered.

The result of the evaluation of a DWARF location list expression is:

* If the current program location is not specified, then it is an evaluation
  error.

  .. note::

    If the location list only has a single default entry, should that be
    considered a match if there is no program location? If there are non-default
    entries then it seems it has to be an evaluation error when there is no
    program location as that indicates the location depends on the program
    location which is not known.

* If there are no matching location list entries, then the result is a location
  description that comprises one undefined location description.

* Otherwise, the operation expression E of each matching location list entry is
  evaluated with the current context, except that the result kind is a location
  description, the object is unspecified, and the initial stack is empty. The
  location list entry result is the location description returned by the
  evaluation of E.

  The result is a location description that is comprised of the union of the
  single location descriptions of the location description result of each
  matching location list entry.

A location list expression can only be used as the value of a debugger
information entry attribute that is encoded using class ``loclist`` or
``loclistsptr`` (see :ref:`amdgpu-dwarf-classes-and-forms`). The value of the
attribute provides an index into a separate object file section called
``.debug_loclists`` or ``.debug_loclists.dwo`` (for split DWARF object files)
that contains the location list entries.

A ``DW_OP_call*`` and ``DW_OP_implicit_pointer`` operation can be used to
specify a debugger information entry attribute that has a location list
expression. Several debugger information entry attributes allow DWARF
expressions that are evaluated with an initial stack that includes a location
description that may originate from the evaluation of a location list
expression.

*This location list representation, the* ``loclist`` *and* ``loclistsptr``
*class, and the related* ``DW_AT_loclists_base`` *attribute are new in DWARF
Version 5. Together they eliminate most, or all of the code object relocations
previously needed for location list expressions.*

.. note::

  The rest of this section is the same as DWARF Version 5 section 2.6.2.

.. _amdgpu-dwarf-address-spaces:

A.2.13 Address Spaces
~~~~~~~~~~~~~~~~~~~~~

.. note::

  This is a new section after DWARF Version 5 section 2.12 Segmented Addresses.

DWARF address spaces correspond to target architecture specific linear
addressable memory areas. They are used in DWARF expression location
descriptions to describe in which target architecture specific memory area data
resides.

*Target architecture specific DWARF address spaces may correspond to hardware
supported facilities such as memory utilizing base address registers, scratchpad
memory, and memory with special interleaving. The size of addresses in these
address spaces may vary. Their access and allocation may be hardware managed
with each thread or group of threads having access to independent storage. For
these reasons they may have properties that do not allow them to be viewed as
part of the unified global virtual address space accessible by all threads.*

*It is target architecture specific whether multiple DWARF address spaces are
supported and how source language memory spaces map to target architecture
specific DWARF address spaces. A target architecture may map multiple source
language memory spaces to the same target architecture specific DWARF address
class. Optimization may determine that variable lifetime and access pattern
allows them to be allocated in faster scratchpad memory represented by a
different DWARF address space than the default for the source language memory
space.*

Although DWARF address space identifiers are target architecture specific,
``DW_ASPACE_LLVM_none`` is a common address space supported by all target
architectures, and defined as the target architecture default address space.

DWARF address space identifiers are used by:

* The ``DW_AT_LLVM_address_space`` attribute.

* The DWARF expression operations: ``DW_OP_aspace_bregx``,
  ``DW_OP_form_aspace_address``, ``DW_OP_aspace_implicit_pointer``, and
  ``DW_OP_xderef*``.

* The CFI instructions: ``DW_CFA_def_aspace_cfa`` and
  ``DW_CFA_def_aspace_cfa_sf``.

.. note::

  Currently, DWARF defines address class values as being target architecture
  specific, and defines a DW_AT_address_class attribute. With the removal of
  DW_AT_segment in DWARF 6, it is unclear how the address class is intended to
  be used as the term is not used elsewhere. Should these be replaced by this
  proposal's more complete address space? Or are they intended to represent
  source language memory spaces such as in OpenCL?

.. _amdgpu-dwarf-memory-spaces:

A.2.14 Memory Spaces
~~~~~~~~~~~~~~~~~~~~

.. note::

  This is a new section after DWARF Version 5 section 2.12 Segmented Addresses.

DWARF memory spaces are used for source languages that have the concept of
memory spaces. They are used in the ``DW_AT_LLVM_memory_space`` attribute for
pointer type, reference type, variable, formal parameter, and constant debugger
information entries.

Each DWARF memory space is conceptually a separate source language memory space
with its own lifetime and aliasing rules. DWARF memory spaces are used to
specify the source language memory spaces that pointer type and reference type
values refer, and to specify the source language memory space in which variables
are allocated.

Although DWARF memory space identifiers are source language specific,
``DW_MSPACE_LLVM_none`` is a common memory space supported by all source
languages, and defined as the source language default memory space.

The set of currently defined DWARF memory spaces, together with source language
mappings, is given in :ref:`amdgpu-dwarf-source-language-memory-spaces-table`.

Vendor defined source language memory spaces may be defined using codes in the
range ``DW_MSPACE_LLVM_lo_user`` to ``DW_MSPACE_LLVM_hi_user``.

.. table:: Source language memory spaces
   :name: amdgpu-dwarf-source-language-memory-spaces-table

   =========================== ============ ============== ============== ==============
   Memory Space Name           Meaning      C/C++          OpenCL         CUDA/HIP
   =========================== ============ ============== ============== ==============
   ``DW_MSPACE_LLVM_none``     generic      *default*      generic        *default*
   ``DW_MSPACE_LLVM_global``   global                      global
   ``DW_MSPACE_LLVM_constant`` constant                    constant       constant
   ``DW_MSPACE_LLVM_group``    thread-group                local          shared
   ``DW_MSPACE_LLVM_private``  thread                      private
   ``DW_MSPACE_LLVM_lo_user``
   ``DW_MSPACE_LLVM_hi_user``
   =========================== ============ ============== ============== ==============

.. note::

  The approach presented in
  :ref:`amdgpu-dwarf-source-language-memory-spaces-table` is to define the
  default ``DW_MSPACE_LLVM_none`` to be the generic address class and not the
  global address class. This matches how CLANG and LLVM have added support for
  CUDA-like languages on top of existing C++ language support. This allows all
  addresses to be generic by default which matches CUDA-like languages.

  An alternative approach is to define ``DW_MSPACE_LLVM_none`` as being the
  global memory space and then change ``DW_MSPACE_LLVM_global`` to
  ``DW_MSPACE_LLVM_generic``. This would match the reality that languages that
  do not support multiple memory spaces only have one default global memory
  space. Generally, in these languages if they expose that the target
  architecture supports multiple memory spaces, the default one is still the
  global memory space. Then a language that does support multiple memory spaces
  has to explicitly indicate which pointers have the added ability to reference
  more than the global memory space. However, compilers generating DWARF for
  CUDA-like languages would then have to define every CUDA-like language pointer
  type or reference type with a ``DW_AT_LLVM_memory_space`` attribute of
  ``DW_MSPACE_LLVM_generic`` to match the language semantics.

A.3 Program Scope Entries
-------------------------

.. note::

  This section provides changes to existing debugger information entry
  attributes. These would be incorporated into the corresponding DWARF Version 5
  chapter 3 sections.

A.3.1 Unit Entries
~~~~~~~~~~~~~~~~~~

.. _amdgpu-dwarf-full-and-partial-compilation-unit-entries:

A.3.1.1 Full and Partial Compilation Unit Entries
+++++++++++++++++++++++++++++++++++++++++++++++++

.. note::

  This augments DWARF Version 5 section 3.1.1 and Table 3.1.

Additional language codes defined for use with the ``DW_AT_language`` attribute
are defined in :ref:`amdgpu-dwarf-language-names-table`.

.. table:: Language Names
   :name: amdgpu-dwarf-language-names-table

   ==================== =============================
   Language Name        Meaning
   ==================== =============================
   ``DW_LANG_LLVM_HIP`` HIP Language.
   ==================== =============================

The HIP language [:ref:`HIP <amdgpu-dwarf-HIP>`] can be supported by extending
the C++ language.

.. note::

  The following new attribute is added.

1.  A ``DW_TAG_compile_unit`` debugger information entry for a compilation unit
    may have a ``DW_AT_LLVM_augmentation`` attribute, whose value is an
    augmentation string.

    *The augmentation string allows producers to indicate that there is
    additional vendor or target specific information in the debugging
    information entries. For example, this might be information about the
    version of vendor specific extensions that are being used.*

    If not present, or if the string is empty, then the compilation unit has no
    augmentation string.

    The format for the augmentation string is:

      | ``[``\ *vendor*\ ``:v``\ *X*\ ``.``\ *Y*\ [\ ``:``\ *options*\ ]\ ``]``\ *

    Where *vendor* is the producer, ``vX.Y`` specifies the major X and minor Y
    version number of the extensions used, and *options* is an optional string
    providing additional information about the extensions. The version number
    must conform to semantic versioning [:ref:`SEMVER <amdgpu-dwarf-SEMVER>`].
    The *options* string must not contain the "\ ``]``\ " character.

    For example:

      ::

        [abc:v0.0][def:v1.2:feature-a=on,feature-b=3]

A.3.3 Subroutine and Entry Point Entries
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _amdgpu-dwarf-low-level-information:

A.3.3.5 Low-Level Information
+++++++++++++++++++++++++++++

1.  A ``DW_TAG_subprogram``, ``DW_TAG_inlined_subroutine``, or
    ``DW_TAG_entry_point`` debugger information entry may have a
    ``DW_AT_return_addr`` attribute, whose value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any. The result of the evaluation is the location
    description L of the place where the return address for the current call
    frame's subprogram or entry point is stored.

    The DWARF is ill-formed if L is not comprised of one memory location
    description for one of the target architecture specific address spaces.

    .. note::

      It is unclear why ``DW_TAG_inlined_subroutine`` has a
      ``DW_AT_return_addr`` attribute but not a ``DW_AT_frame_base`` or
      ``DW_AT_static_link`` attribute. Seems it would either have all of them or
      none. Since inlined subprograms do not have a call frame it seems they
      would have none of these attributes.

2.  A ``DW_TAG_subprogram`` or ``DW_TAG_entry_point`` debugger information entry
    may have a ``DW_AT_frame_base`` attribute, whose value is a DWARF expression
    E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any.

    The DWARF is ill-formed if E contains a ``DW_OP_fbreg`` operation, or the
    resulting location description L is not comprised of one single location
    description SL.

    If SL is a register location description for register R, then L is replaced
    with the result of evaluating a ``DW_OP_bregx R, 0`` operation. This
    computes the frame base memory location description in the target
    architecture default address space.

    *This allows the more compact* ``DW_OP_reg*`` *to be used instead of*
    ``DW_OP_breg* 0``\ *.*

    .. note::

      This rule could be removed and require the producer to create the required
      location description directly using ``DW_OP_call_frame_cfa``,
      ``DW_OP_breg*``, or ``DW_OP_LLVM_aspace_bregx``. This would also then
      allow a target to implement the call frames within a large register.

    Otherwise, the DWARF is ill-formed if SL is not a memory location
    description in any of the target architecture specific address spaces.

    The resulting L is the *frame base* for the subprogram or entry point.

    *Typically, E will use the* ``DW_OP_call_frame_cfa`` *operation or be a
    stack pointer register plus or minus some offset.*

    *The frame base for a subprogram is typically an address relative to the
    first unit of storage allocated for the subprogram's stack frame. The*
    ``DW_AT_frame_base`` *attribute can be used in several ways:*

    1.  *In subprograms that need location lists to locate local variables, the*
        ``DW_AT_frame_base`` *can hold the needed location list, while all
        variables' location descriptions can be simpler ones involving the frame
        base.*

    2.  *It can be used in resolving "up-level" addressing within
        nested routines. (See also* ``DW_AT_static_link``\ *, below)*

    *Some languages support nested subroutines. In such languages, it is
    possible to reference the local variables of an outer subroutine from within
    an inner subroutine. The* ``DW_AT_static_link`` *and* ``DW_AT_frame_base``
    *attributes allow debuggers to support this same kind of referencing.*

3.  If a ``DW_TAG_subprogram`` or ``DW_TAG_entry_point`` debugger information
    entry is lexically nested, it may have a ``DW_AT_static_link`` attribute,
    whose value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any. The result of the evaluation is the location
    description L of the *canonical frame address* (see
    :ref:`amdgpu-dwarf-call-frame-information`) of the relevant call frame of
    the subprogram instance that immediately lexically encloses the current call
    frame's subprogram or entry point.

    The DWARF is ill-formed if L is not comprised of one memory location
    description for one of the target architecture specific address spaces.

    In the context of supporting nested subroutines, the DW_AT_frame_base
    attribute value obeys the following constraints:

    1.  It computes a value that does not change during the life of the
        subprogram, and

    2.  The computed value is unique among instances of the same subroutine.

    *For typical DW_AT_frame_base use, this means that a recursive subroutine's
    stack frame must have non-zero size.*

    *If a debugger is attempting to resolve an up-level reference to a variable,
    it uses the nesting structure of DWARF to determine which subroutine is the
    lexical parent and the* ``DW_AT_static_link`` *value to identify the
    appropriate active frame of the parent. It can then attempt to find the
    reference within the context of the parent.*

    .. note::

      The following new attributes are added.

4.  For languages that are implemented using a SIMT execution model, a
    ``DW_TAG_subprogram``, ``DW_TAG_inlined_subroutine``, or
    ``DW_TAG_entry_point`` debugger information entry may have a
    ``DW_AT_LLVM_lanes`` attribute whose value is an integer constant that is
    the number of source language threads of execution per target architecture
    thread.

    *For example, a compiler may map source language threads of execution onto
    lanes of a target architecture thread using a SIMT execution model.*

    It is the static number of source language threads of execution per target
    architecture thread. It is not the dynamic number of source language threads
    of execution with which the target architecture thread was initiated, for
    example, due to smaller or partial work-groups.

    If not present, the default value of 1 is used.

    The DWARF is ill-formed if the value is less than or equal to 0.

5.  For source languages that are implemented using a SIMT execution model, a
    ``DW_TAG_subprogram``, ``DW_TAG_inlined_subroutine``, or
    ``DW_TAG_entry_point`` debugging information entry may have a
    ``DW_AT_LLVM_lane_pc`` attribute whose value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any.

    The resulting location description L is for a lane count sized vector of
    generic type elements. The lane count is the value of the
    ``DW_AT_LLVM_lanes`` attribute. Each element holds the conceptual program
    location of the corresponding lane. If the lane was not active when the
    current subprogram was called, its element is an undefined location
    description.

    The DWARF is ill-formed if L does not have exactly one single location
    description.

    ``DW_AT_LLVM_lane_pc`` *allows the compiler to indicate conceptually where
    each SIMT lane of a target architecture thread is positioned even when it is
    in divergent control flow that is not active.*

    *Typically, the result is a location description with one composite location
    description with each part being a location description with either one
    undefined location description or one memory location description.*

    If not present, the target architecture thread is not being used in a SIMT
    manner, and the thread's current program location is used.

6.  For languages that are implemented using a SIMT execution model, a
    ``DW_TAG_subprogram``, ``DW_TAG_inlined_subroutine``, or
    ``DW_TAG_entry_point`` debugger information entry may have a
    ``DW_AT_LLVM_active_lane`` attribute whose value is a DWARF expression E.

    E is evaluated with a context that has a result kind of a location
    description, an unspecified object, the compilation unit that contains E, an
    empty initial stack, and other context elements corresponding to the source
    language thread of execution upon which the user is focused, if any.

    The DWARF is ill-formed if L does not have exactly one single location
    description SL.

    The active lane bit mask V for the current program location is obtained by
    reading from SL using a target architecture specific integral base type T
    that has a bit size equal to the value of the ``DW_AT_LLVM_lanes`` attribute
    of the subprogram corresponding to context's frame and program location. The
    N\ :sup:`th` least significant bit of the mask corresponds to the N\
    :sup:`th` lane. If the bit is 1 the lane is active, otherwise it is
    inactive. The result of the attribute is the value V.

    *Some targets may update the target architecture execution mask for regions
    of code that must execute with different sets of lanes than the current
    active lanes. For example, some code must execute with all lanes made
    temporarily active.* ``DW_AT_LLVM_active_lane`` *allows the compiler to
    provide the means to determine the source language active lanes at any
    program location. Typically, this attribute will use a loclist to express
    different locations of the active lane mask at different program locations.*

    If not present and ``DW_AT_LLVM_lanes`` is greater than 1, then the target
    architecture execution mask is used.

7.  A ``DW_TAG_subprogram``, ``DW_TAG_inlined_subroutine``, or
    ``DW_TAG_entry_point`` debugger information entry may have a
    ``DW_AT_LLVM_iterations`` attribute whose value is an integer constant or a
    DWARF expression E. Its value is the number of source language loop
    iterations executing concurrently by the target architecture for a single
    source language thread of execution.

    *A compiler may generate code that executes more than one iteration of a
    source language loop concurrently using optimization techniques such as
    software pipelining or SIMD vectorization. The number of concurrent
    iterations may vary for different loop nests in the same subprogram.
    Typically, this attribute will use a loclist to express different values at
    different program locations.*

    If the attribute is an integer constant, then the value is the constant. The
    DWARF is ill-formed if the constant is less than or equal to 0.

    Otherwise, E is evaluated with a context that has a result kind of a
    location description, an unspecified object, the compilation unit that
    contains E, an empty initial stack, and other context elements corresponding
    to the source language thread of execution upon which the user is focused,
    if any. The DWARF is ill-formed if the result is not a location description
    comprised of one implicit location description, that when read as the
    generic type, results in a value V that is less than or equal to 0. The
    result of the attribute is the value V.

    If not present, the default value of 1 is used.

A.3.4 Call Site Entries and Parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A.3.4.2 Call Site Parameters
++++++++++++++++++++++++++++

1.  The call site entry may own ``DW_TAG_call_site_parameter`` debugging
    information entries representing the parameters passed to the call. Call
    site parameter entries occur in the same order as the corresponding
    parameters in the source. Each such entry has a ``DW_AT_location`` attribute
    which is a location description. This location description describes where
    the parameter is passed (usually either some register, or a memory location
    expressible as the contents of the stack register plus some offset).

2.  A ``DW_TAG_call_site_parameter`` debugger information entry may have a
    ``DW_AT_call_value`` attribute, whose value is a DWARF operation expression
    E\ :sub:`1`\ .

    The result of the ``DW_AT_call_value`` attribute is obtained by evaluating
    E\ :sub:`1` with a context that has a result kind of a value, an unspecified
    object, the compilation unit that contains E, an empty initial stack, and
    other context elements corresponding to the source language thread of
    execution upon which the user is focused, if any. The resulting value V\
    :sub:`1` is the value of the parameter at the time of the call made by the
    call site.

    For parameters passed by reference, where the code passes a pointer to a
    location which contains the parameter, or for reference type parameters, the
    ``DW_TAG_call_site_parameter`` debugger information entry may also have a
    ``DW_AT_call_data_location`` attribute whose value is a DWARF operation
    expression E\ :sub:`2`\ , and a ``DW_AT_call_data_value`` attribute whose
    value is a DWARF operation expression E\ :sub:`3`\ .

    The value of the ``DW_AT_call_data_location`` attribute is obtained by
    evaluating E\ :sub:`2` with a context that has a result kind of a location
    description, an unspecified object, the compilation unit that contains E, an
    empty initial stack, and other context elements corresponding to the source
    language thread of execution upon which the user is focused, if any. The
    resulting location description L\ :sub:`2` is the location where the
    referenced parameter lives during the call made by the call site. If E\
    :sub:`2` would just be a ``DW_OP_push_object_address``, then the
    ``DW_AT_call_data_location`` attribute may be omitted.

    .. note::

      The DWARF Version 5 implies that ``DW_OP_push_object_address`` may be used
      but does not state what object must be specified in the context. Either
      ``DW_OP_push_object_address`` cannot be used, or the object to be passed
      in the context must be defined.

    The value of the ``DW_AT_call_data_value`` attribute is obtained by
    evaluating E\ :sub:`3` with a context that has a result kind of a value, an
    unspecified object, the compilation unit that contains E, an empty initial
    stack, and other context elements corresponding to the source language
    thread of execution upon which the user is focused, if any. The resulting
    value V\ :sub:`3` is the value in L\ :sub:`2` at the time of the call made
    by the call site.

    The result of these attributes is undefined if the current call frame is not
    for the subprogram containing the ``DW_TAG_call_site_parameter`` debugger
    information entry or the current program location is not for the call site
    containing the ``DW_TAG_call_site_parameter`` debugger information entry in
    the current call frame.

    *The consumer may have to virtually unwind to the call site (see*
    :ref:`amdgpu-dwarf-call-frame-information`\ *) in order to evaluate these
    attributes. This will ensure the source language thread of execution upon
    which the user is focused corresponds to the call site needed to evaluate
    the expression.*

    If it is not possible to avoid the expressions of these attributes from
    accessing registers or memory locations that might be clobbered by the
    subprogram being called by the call site, then the associated attribute
    should not be provided.

    *The reason for the restriction is that the parameter may need to be
    accessed during the execution of the callee. The consumer may virtually
    unwind from the called subprogram back to the caller and then evaluate the
    attribute expressions. The call frame information (see*
    :ref:`amdgpu-dwarf-call-frame-information`\ *) will not be able to restore
    registers that have been clobbered, and clobbered memory will no longer have
    the value at the time of the call.*

3.  Each call site parameter entry may also have a ``DW_AT_call_parameter``
    attribute which contains a reference to a ``DW_TAG_formal_parameter`` entry,
    ``DW_AT_type attribute`` referencing the type of the parameter or
    ``DW_AT_name`` attribute describing the parameter's name.

*Examples using call site entries and related attributes are found in Appendix
D.15.*

.. _amdgpu-dwarf-lexical-block-entries:

A.3.5 Lexical Block Entries
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This section is the same as DWARF Version 5 section 3.5.

A.4 Data Object and Object List Entries
---------------------------------------

.. note::

  This section provides changes to existing debugger information entry
  attributes. These would be incorporated into the corresponding DWARF Version 5
  chapter 4 sections.

.. _amdgpu-dwarf-data-object-entries:

A.4.1 Data Object Entries
~~~~~~~~~~~~~~~~~~~~~~~~~

Program variables, formal parameters and constants are represented by debugging
information entries with the tags ``DW_TAG_variable``,
``DW_TAG_formal_parameter`` and ``DW_TAG_constant``, respectively.

*The tag DW_TAG_constant is used for languages that have true named constants.*

The debugging information entry for a program variable, formal parameter or
constant may have the following attributes:

1.  A ``DW_AT_location`` attribute, whose value is a DWARF expression E that
    describes the location of a variable or parameter at run-time.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any. The result of the evaluation is the location
    description of the base of the data object.

    See :ref:`amdgpu-dwarf-control-flow-operations` for special evaluation rules
    used by the ``DW_OP_call*`` operations.

    .. note::

      Delete the description of how the ``DW_OP_call*`` operations evaluate a
      ``DW_AT_location`` attribute as that is now described in the operations.

    .. note::

      See the discussion about the ``DW_AT_location`` attribute in the
      ``DW_OP_call*`` operation. Having each attribute only have a single
      purpose and single execution semantics seems desirable. It makes it easier
      for the consumer that no longer have to track the context. It makes it
      easier for the producer as it can rely on a single semantics for each
      attribute.

      For that reason, limiting the ``DW_AT_location`` attribute to only
      supporting evaluating the location description of an object, and using a
      different attribute and encoding class for the evaluation of DWARF
      expression *procedures* on the same operation expression stack seems
      desirable.

2.  ``DW_AT_const_value``

    .. note::

      Could deprecate using the ``DW_AT_const_value`` attribute for
      ``DW_TAG_variable`` or ``DW_TAG_formal_parameter`` debugger information
      entries that have been optimized to a constant. Instead,
      ``DW_AT_location`` could be used with a DWARF expression that produces an
      implicit location description now that any location description can be
      used within a DWARF expression. This allows the ``DW_OP_call*`` operations
      to be used to push the location description of any variable regardless of
      how it is optimized.

3.  ``DW_AT_LLVM_memory_space``

    A ``DW_AT_memory_space`` attribute with a constant value representing a source
    language specific DWARF memory space (see 2.14 "Memory Spaces"). If omitted,
    defaults to ``DW_MSPACE_none``.


A.4.2 Common Block Entries
~~~~~~~~~~~~~~~~~~~~~~~~~~

A common block entry also has a ``DW_AT_location`` attribute whose value is a
DWARF expression E that describes the location of the common block at run-time.
The result of the attribute is obtained by evaluating E with a context that has
a result kind of a location description, an unspecified object, the compilation
unit that contains E, an empty initial stack, and other context elements
corresponding to the source language thread of execution upon which the user is
focused, if any. The result of the evaluation is the location description of the
base of the common block. See :ref:`amdgpu-dwarf-control-flow-operations` for
special evaluation rules used by the ``DW_OP_call*`` operations.

A.5 Type Entries
----------------

.. note::

  This section provides changes to existing debugger information entry
  attributes. These would be incorporated into the corresponding DWARF Version 5
  chapter 5 sections.

.. _amdgpu-dwarf-base-type-entries:

A.5.1 Base Type Entries
~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  The following new attribute is added.

1.  A ``DW_TAG_base_type`` debugger information entry for a base type T may have
    a ``DW_AT_LLVM_vector_size`` attribute whose value is an integer constant
    that is the vector type size N.

    The representation of a vector base type is as N contiguous elements, each
    one having the representation of a base type T' that is the same as T
    without the ``DW_AT_LLVM_vector_size`` attribute.

    If a ``DW_TAG_base_type`` debugger information entry does not have a
    ``DW_AT_LLVM_vector_size`` attribute, then the base type is not a vector
    type.

    The DWARF is ill-formed if N is not greater than 0.

    .. note::

      LLVM has mention of a non-upstreamed debugger information entry that is
      intended to support vector types. However, that was not for a base type so
      would not be suitable as the type of a stack value entry. But perhaps that
      could be replaced by using this attribute.

    .. note::

      Compare this with the ``DW_AT_GNU_vector`` extension supported by GNU. Is
      it better to add an attribute to the existing ``DW_TAG_base_type`` debug
      entry, or allow some forms of ``DW_TAG_array_type`` (those that have the
      ``DW_AT_GNU_vector`` attribute) to be used as stack entry value types?

.. _amdgpu-dwarf-type-modifier-entries:

A.5.3 Type Modifier Entries
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This section augments DWARF Version 5 section 5.3.

A modified type entry describing a pointer or reference type (using
``DW_TAG_pointer_type``, ``DW_TAG_reference_type`` or
``DW_TAG_rvalue_reference_type``\ ) may have a ``DW_AT_LLVM_memory_space``
attribute with a constant value representing a source language specific DWARF
memory space (see :ref:`amdgpu-dwarf-memory-spaces`). If omitted, defaults to
DW_MSPACE_LLVM_none.

A modified type entry describing a pointer or reference type (using
``DW_TAG_pointer_type``, ``DW_TAG_reference_type`` or
``DW_TAG_rvalue_reference_type``\ ) may have a ``DW_AT_LLVM_address_space``
attribute with a constant value AS representing an architecture specific DWARF
address space (see :ref:`amdgpu-dwarf-address-spaces`). If omitted, defaults to
``DW_ASPACE_LLVM_none``. DR is the offset of a hypothetical debug information
entry D in the current compilation unit for an integral base type matching the
address size of AS. An object P having the given pointer or reference type are
dereferenced as if the ``DW_OP_push_object_address; DW_OP_deref_type DR;
DW_OP_constu AS; DW_OP_form_aspace_address`` operation expression was evaluated
with the current context except: the result kind is location description; the
initial stack is empty; and the object is the location description of P.

.. note::

  What if the current context does not have a current target architecture
  defined?

.. note::

  With the expanded support for DWARF address spaces, it may be worth examining
  if they can be used for what was formerly supported by DWARF 5 segments. That
  would include specifying the address space of all code addresses (compilation
  units, subprograms, subprogram entries, labels, subprogram types, etc.).
  Either the code address attributes could be extended to allow a exprloc form
  (so that ``DW_OP_form_aspace_address`` can be used) or the
  ``DW_AT_LLVM_address_space`` attribute be allowed on all DIEs that allow
  ``DW_AT_segment``.

A.5.7 Structure, Union, Class and Interface Type Entries
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A.5.7.3 Derived or Extended Structures, Classes and Interfaces
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

1.  For a ``DW_AT_data_member_location`` attribute there are two cases:

    1.  If the attribute is an integer constant B, it provides the offset in
        bytes from the beginning of the containing entity.

        The result of the attribute is obtained by evaluating a
        ``DW_OP_LLVM_offset B`` operation with an initial stack comprising the
        location description of the beginning of the containing entity. The
        result of the evaluation is the location description of the base of the
        member entry.

        *If the beginning of the containing entity is not byte aligned, then the
        beginning of the member entry has the same bit displacement within a
        byte.*

    2.  Otherwise, the attribute must be a DWARF expression E which is evaluated
        with a context that has a result kind of a location description, an
        unspecified object, the compilation unit that contains E, an initial
        stack comprising the location description of the beginning of the
        containing entity, and other context elements corresponding to the
        source language thread of execution upon which the user is focused, if
        any. The result of the evaluation is the location description of the
        base of the member entry.

    .. note::

      The beginning of the containing entity can now be any location
      description, including those with more than one single location
      description, and those with single location descriptions that are of any
      kind and have any bit offset.

A.5.7.8 Member Function Entries
+++++++++++++++++++++++++++++++

1.  An entry for a virtual function also has a ``DW_AT_vtable_elem_location``
    attribute whose value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an initial stack comprising the location
    description of the object of the enclosing type, and other context elements
    corresponding to the source language thread of execution upon which the user
    is focused, if any. The result of the evaluation is the location description
    of the slot for the function within the virtual function table for the
    enclosing class.

A.5.14 Pointer to Member Type Entries
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1.  The ``DW_TAG_ptr_to_member_type`` debugging information entry has a
    ``DW_AT_use_location`` attribute whose value is a DWARF expression E. It is
    used to compute the location description of the member of the class to which
    the pointer to member entry points.

    *The method used to find the location description of a given member of a
    class, structure, or union is common to any instance of that class,
    structure, or union and to any instance of the pointer to member type. The
    method is thus associated with the pointer to member type, rather than with
    each object that has a pointer to member type.*

    The ``DW_AT_use_location`` DWARF expression is used in conjunction with the
    location description for a particular object of the given pointer to member
    type and for a particular structure or class instance.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an initial stack comprising two entries,
    and other context elements corresponding to the source language thread of
    execution upon which the user is focused, if any. The first stack entry is
    the value of the pointer to member object itself. The second stack entry is
    the location description of the base of the entire class, structure, or
    union instance containing the member whose location is being calculated. The
    result of the evaluation is the location description of the member of the
    class to which the pointer to member entry points.

A.5.18 Dynamic Properties of Types
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A.5.18.1 Data Location
++++++++++++++++++++++

*Some languages may represent objects using descriptors to hold information,
including a location and/or run-time parameters, about the data that represents
the value for that object.*

1.  The ``DW_AT_data_location`` attribute may be used with any type that
    provides one or more levels of hidden indirection and/or run-time parameters
    in its representation. Its value is a DWARF operation expression E which
    computes the location description of the data for an object. When this
    attribute is omitted, the location description of the data is the same as
    the location description of the object.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an object that is the location
    description of the data descriptor, the compilation unit that contains E, an
    empty initial stack, and other context elements corresponding to the source
    language thread of execution upon which the user is focused, if any. The
    result of the evaluation is the location description of the base of the
    member entry.

    *E will typically involve an operation expression that begins with a*
    ``DW_OP_push_object_address`` *operation which loads the location
    description of the object which can then serve as a descriptor in subsequent
    calculation.*

    .. note::

      Since ``DW_AT_data_member_location``, ``DW_AT_use_location``, and
      ``DW_AT_vtable_elem_location`` allow both operation expressions and
      location list expressions, why does ``DW_AT_data_location`` not allow
      both? In all cases they apply to data objects so less likely that
      optimization would cause different operation expressions for different
      program location ranges. But if supporting for some then should be for
      all.

      It seems odd this attribute is not the same as
      ``DW_AT_data_member_location`` in having an initial stack with the
      location description of the object since the expression has to need it.

A.6 Other Debugging Information
-------------------------------

.. note::

  This section provides changes to existing debugger information entry
  attributes. These would be incorporated into the corresponding DWARF Version 5
  chapter 6 sections.

A.6.1 Accelerated Access
~~~~~~~~~~~~~~~~~~~~~~~~

.. _amdgpu-dwarf-lookup-by-name:

A.6.1.1 Lookup By Name
++++++++++++++++++++++

A.6.1.1.1 Contents of the Name Index
####################################

.. note::

  The following provides changes to DWARF Version 5 section 6.1.1.1.

  The rule for debugger information entries included in the name index in the
  optional ``.debug_names`` section is extended to also include named
  ``DW_TAG_variable`` debugging information entries with a ``DW_AT_location``
  attribute that includes a ``DW_OP_LLVM_form_aspace_address`` operation.

The name index must contain an entry for each debugging information entry that
defines a named subprogram, label, variable, type, or namespace, subject to the
following rules:

* ``DW_TAG_variable`` debugging information entries with a ``DW_AT_location``
  attribute that includes a ``DW_OP_addr``, ``DW_OP_LLVM_form_aspace_address``,
  or ``DW_OP_form_tls_address`` operation are included; otherwise, they are
  excluded.

A.6.1.1.4 Data Representation of the Name Index
###############################################

.. _amdgpu-dwarf-name-index-section-header:


A.6.1.1.4.1 Section Header
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

  The following provides an addition to DWARF Version 5 section 6.1.1.4.1 item
  14 ``augmentation_string``.

A null-terminated UTF-8 vendor specific augmentation string, which provides
additional information about the contents of this index. If provided, the
recommended format for augmentation string is:

  | ``[``\ *vendor*\ ``:v``\ *X*\ ``.``\ *Y*\ [\ ``:``\ *options*\ ]\ ``]``\ *

Where *vendor* is the producer, ``vX.Y`` specifies the major X and minor Y
version number of the extensions used in the DWARF of the compilation unit, and
*options* is an optional string providing additional information about the
extensions. The version number must conform to semantic versioning [:ref:`SEMVER
<amdgpu-dwarf-SEMVER>`]. The *options* string must not contain the "\ ``]``\ "
character.

For example:

  ::

    [abc:v0.0][def:v1.2:feature-a=on,feature-b=3]

.. note::

  This is different to the definition in DWARF Version 5 but is consistent with
  the other augmentation strings and allows multiple vendor extensions to be
  supported.

.. _amdgpu-dwarf-line-number-information:

A.6.2 Line Number Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A.6.2.4 The Line Number Program Header
++++++++++++++++++++++++++++++++++++++

A.6.2.4.1 Standard Content Descriptions
#######################################

.. note::

  This augments DWARF Version 5 section 6.2.4.1.

.. _amdgpu-dwarf-line-number-information-dw-lnct-llvm-source:

1.  ``DW_LNCT_LLVM_source``

    The component is a null-terminated UTF-8 source text string with "\ ``\n``\
    " line endings. This content code is paired with the same forms as
    ``DW_LNCT_path``. It can be used for file name entries.

    The value is an empty null-terminated string if no source is available. If
    the source is available but is an empty file then the value is a
    null-terminated single "\ ``\n``\ ".

    *When the source field is present, consumers can use the embedded source
    instead of attempting to discover the source on disk using the file path
    provided by the* ``DW_LNCT_path`` *field. When the source field is absent,
    consumers can access the file to get the source text.*

    *This is particularly useful for programming languages that support runtime
    compilation and runtime generation of source text. In these cases, the
    source text does not reside in any permanent file. For example, the OpenCL
    language [:ref:`OpenCL <amdgpu-dwarf-OpenCL>`] supports online compilation.*

2.  ``DW_LNCT_LLVM_is_MD5``

    ``DW_LNCT_LLVM_is_MD5`` indicates if the ``DW_LNCT_MD5`` content kind, if
    present, is valid: when 0 it is not valid and when 1 it is valid. If
    ``DW_LNCT_LLVM_is_MD5`` content kind is not present, and ``DW_LNCT_MD5``
    content kind is present, then the MD5 checksum is valid.

    ``DW_LNCT_LLVM_is_MD5`` is always paired with the ``DW_FORM_udata`` form.

    *This allows a compilation unit to have a mixture of files with and without
    MD5 checksums. This can happen when multiple relocatable files are linked
    together.*

.. _amdgpu-dwarf-call-frame-information:

A.6.4 Call Frame Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This section provides changes to existing call frame information and defines
  instructions added by these extensions. Additional support is added for
  address spaces. Register unwind DWARF expressions are generalized to allow any
  location description, including those with composite and implicit location
  descriptions.

  These changes would be incorporated into the DWARF Version 5 section 6.4.

.. _amdgpu-dwarf-structure_of-call-frame-information:

A.6.4.1 Structure of Call Frame Information
+++++++++++++++++++++++++++++++++++++++++++

The register rules are:

*undefined*
  A register that has this rule has no recoverable value in the previous frame.
  The previous value of this register is the undefined location description (see
  :ref:`amdgpu-dwarf-undefined-location-description-operations`).

  *By convention, the register is not preserved by a callee.*

*same value*
  This register has not been modified from the previous caller frame.

  If the current frame is the top frame, then the previous value of this
  register is the location description L that specifies one register location
  description SL. SL specifies the register location storage that corresponds to
  the register with a bit offset of 0 for the current thread.

  If the current frame is not the top frame, then the previous value of this
  register is the location description obtained using the call frame information
  for the callee frame and callee program location invoked by the current caller
  frame for the same register.

  *By convention, the register is preserved by the callee, but the callee has
  not modified it.*

*offset(N)*
  N is a signed byte offset. The previous value of this register is saved at the
  location description computed as if the DWARF operation expression
  ``DW_OP_LLVM_offset N`` is evaluated with the current context, except the
  result kind is a location description, the compilation unit is unspecified,
  the object is unspecified, and an initial stack comprising the location
  description of the current CFA (see
  :ref:`amdgpu-dwarf-operation-expressions`).

*val_offset(N)*
  N is a signed byte offset. The previous value of this register is the memory
  byte address of the location description computed as if the DWARF operation
  expression ``DW_OP_LLVM_offset N`` is evaluated with the current context,
  except the result kind is a location description, the compilation unit is
  unspecified, the object is unspecified, and an initial stack comprising the
  location description of the current CFA (see
  :ref:`amdgpu-dwarf-operation-expressions`).

  The DWARF is ill-formed if the CFA location description is not a memory byte
  address location description, or if the register size does not match the size
  of an address in the address space of the current CFA location description.

  *Since the CFA location description is required to be a memory byte address
  location description, the value of val_offset(N) will also be a memory byte
  address location description since it is offsetting the CFA location
  description by N bytes. Furthermore, the value of val_offset(N) will be a
  memory byte address in the same address space as the CFA location
  description.*

  .. note::

    Should DWARF allow the address size to be a different size to the size of
    the register? Requiring them to be the same bit size avoids any issue of
    conversion as the bit contents of the register is simply interpreted as a
    value of the address.

    GDB has a per register hook that allows a target specific conversion on a
    register by register basis. It defaults to truncation of bigger registers,
    and to actually reading bytes from the next register (or reads out of bounds
    for the last register) for smaller registers. There are no GDB tests that
    read a register out of bounds (except an illegal hand written assembly
    test).

*register(R)*
  This register has been stored in another register numbered R.

  The previous value of this register is the location description obtained using
  the call frame information for the current frame and current program location
  for register R.

  The DWARF is ill-formed if the size of this register does not match the size
  of register R or if there is a cyclic dependency in the call frame
  information.

  .. note::

    Should this also allow R to be larger than this register? If so is the value
    stored in the low order bits and it is undefined what is stored in the
    extra upper bits?

*expression(E)*
  The previous value of this register is located at the location description
  produced by evaluating the DWARF operation expression E (see
  :ref:`amdgpu-dwarf-operation-expressions`).

  E is evaluated with the current context, except the result kind is a location
  description, the compilation unit is unspecified, the object is unspecified,
  and an initial stack comprising the location description of the current CFA
  (see :ref:`amdgpu-dwarf-operation-expressions`).

*val_expression(E)*
  The previous value of this register is located at the implicit location
  description created from the value produced by evaluating the DWARF operation
  expression E (see :ref:`amdgpu-dwarf-operation-expressions`).

  E is evaluated with the current context, except the result kind is a value,
  the compilation unit is unspecified, the object is unspecified, and an initial
  stack comprising the location description of the current CFA (see
  :ref:`amdgpu-dwarf-operation-expressions`).

  The DWARF is ill-formed if the resulting value type size does not match the
  register size.

  .. note::

    This has limited usefulness as the DWARF expression E can only produce
    values up to the size of the generic type. This is due to not allowing any
    operations that specify a type in a CFI operation expression. This makes it
    unusable for registers that are larger than the generic type. However,
    *expression(E)* can be used to create an implicit location description of
    any size.

*architectural*
  The rule is defined externally to this specification by the augmenter.

*This table would be extremely large if actually constructed as described. Most
of the entries at any point in the table are identical to the ones above them.
The whole table can be represented quite compactly by recording just the
differences starting at the beginning address of each subroutine in the
program.*

The virtual unwind information is encoded in a self-contained section called
``.debug_frame``. Entries in a ``.debug_frame`` section are aligned on a
multiple of the address size relative to the start of the section and come in
two forms: a Common Information Entry (CIE) and a Frame Description Entry (FDE).

*If the range of code addresses for a function is not contiguous, there may be
multiple CIEs and FDEs corresponding to the parts of that function.*

A Common Information Entry (CIE) holds information that is shared among many
Frame Description Entries (FDE). There is at least one CIE in every non-empty
``.debug_frame`` section. A CIE contains the following fields, in order:

1.  ``length`` (initial length)

    A constant that gives the number of bytes of the CIE structure, not
    including the length field itself (see Section 7.2.2 Initial Length Values).
    The size of the length field plus the value of length must be an integral
    multiple of the address size specified in the ``address_size`` field.

2.  ``CIE_id`` (4 or 8 bytes, see
    :ref:`amdgpu-dwarf-32-bit-and-64-bit-dwarf-formats`)

    A constant that is used to distinguish CIEs from FDEs.

    In the 32-bit DWARF format, the value of the CIE id in the CIE header is
    0xffffffff; in the 64-bit DWARF format, the value is 0xffffffffffffffff.

3.  ``version`` (ubyte)

    A version number (see Section 7.24 Call Frame Information). This number is
    specific to the call frame information and is independent of the DWARF
    version number.

    The value of the CIE version number is 4.

    .. note::

      Would this be increased to 5 to reflect the changes in these extensions?

4.  ``augmentation`` (sequence of UTF-8 characters)

    A null-terminated UTF-8 string that identifies the augmentation to this CIE
    or to the FDEs that use it. If a reader encounters an augmentation string
    that is unexpected, then only the following fields can be read:

    * CIE: length, CIE_id, version, augmentation
    * FDE: length, CIE_pointer, initial_location, address_range

    If there is no augmentation, this value is a zero byte.

    *The augmentation string allows users to indicate that there is additional
    vendor and target architecture specific information in the CIE or FDE which
    is needed to virtually unwind a stack frame. For example, this might be
    information about dynamically allocated data which needs to be freed on exit
    from the routine.*

    *Because the* ``.debug_frame`` *section is useful independently of any*
    ``.debug_info`` *section, the augmentation string always uses UTF-8
    encoding.*

    The recommended format for the augmentation string is:

      | ``[``\ *vendor*\ ``:v``\ *X*\ ``.``\ *Y*\ [\ ``:``\ *options*\ ]\ ``]``\ *

    Where *vendor* is the producer, ``vX.Y`` specifies the major X and minor Y
    version number of the extensions used, and *options* is an optional string
    providing additional information about the extensions. The version number
    must conform to semantic versioning [:ref:`SEMVER <amdgpu-dwarf-SEMVER>`].
    The *options* string must not contain the "\ ``]``\ " character.

    For example:

      ::

        [abc:v0.0][def:v1.2:feature-a=on,feature-b=3]

5.  ``address_size`` (ubyte)

    The size of a target address in this CIE and any FDEs that use it, in bytes.
    If a compilation unit exists for this frame, its address size must match the
    address size here.

6.  ``segment_selector_size`` (ubyte)

    The size of a segment selector in this CIE and any FDEs that use it, in
    bytes.

7.  ``code_alignment_factor`` (unsigned LEB128)

    A constant that is factored out of all advance location instructions (see
    :ref:`amdgpu-dwarf-row-creation-instructions`). The resulting value is
    ``(operand * code_alignment_factor)``.

8.  ``data_alignment_factor`` (signed LEB128)

    A constant that is factored out of certain offset instructions (see
    :ref:`amdgpu-dwarf-cfa-definition-instructions` and
    :ref:`amdgpu-dwarf-register-rule-instructions`). The resulting value is
    ``(operand * data_alignment_factor)``.

9.  ``return_address_register`` (unsigned LEB128)

    An unsigned LEB128 constant that indicates which column in the rule table
    represents the return address of the subprogram. Note that this column might
    not correspond to an actual machine register.

    The value of the return address register is used to determine the program
    location of the caller frame. The program location of the top frame is the
    target architecture program counter value of the current thread.

10. ``initial_instructions`` (array of ubyte)

    A sequence of rules that are interpreted to create the initial setting of
    each column in the table.

    The default rule for all columns before interpretation of the initial
    instructions is the undefined rule. However, an ABI authoring body or a
    compilation system authoring body may specify an alternate default value for
    any or all columns.

11. ``padding`` (array of ubyte)

    Enough ``DW_CFA_nop`` instructions to make the size of this entry match the
    length value above.

An FDE contains the following fields, in order:

1.  ``length`` (initial length)

    A constant that gives the number of bytes of the header and instruction
    stream for this subprogram, not including the length field itself (see
    Section 7.2.2 Initial Length Values). The size of the length field plus the
    value of length must be an integral multiple of the address size.

2.  ``CIE_pointer`` (4 or 8 bytes, see
    :ref:`amdgpu-dwarf-32-bit-and-64-bit-dwarf-formats`)

    A constant offset into the ``.debug_frame`` section that denotes the CIE
    that is associated with this FDE.

3.  ``initial_location`` (segment selector and target address)

    The address of the first location associated with this table entry. If the
    segment_selector_size field of this FDE’s CIE is non-zero, the initial
    location is preceded by a segment selector of the given length.

4.  ``address_range`` (target address)

    The number of bytes of program instructions described by this entry.

5.  ``instructions`` (array of ubyte)

    A sequence of table defining instructions that are described in
    :ref:`amdgpu-dwarf-call-frame-instructions`.

6.  ``padding`` (array of ubyte)

    Enough ``DW_CFA_nop`` instructions to make the size of this entry match the
    length value above.

.. _amdgpu-dwarf-call-frame-instructions:

A.6.4.2 Call Frame Instructions
+++++++++++++++++++++++++++++++

Each call frame instruction is defined to take 0 or more operands. Some of the
operands may be encoded as part of the opcode (see
:ref:`amdgpu-dwarf-call-frame-information-encoding`). The instructions are
defined in the following sections.

Some call frame instructions have operands that are encoded as DWARF operation
expressions E (see :ref:`amdgpu-dwarf-operation-expressions`). The DWARF
operations that can be used in E have the following restrictions:

* ``DW_OP_addrx``, ``DW_OP_call2``, ``DW_OP_call4``, ``DW_OP_call_ref``,
  ``DW_OP_const_type``, ``DW_OP_constx``, ``DW_OP_convert``,
  ``DW_OP_deref_type``, ``DW_OP_fbreg``, ``DW_OP_implicit_pointer``,
  ``DW_OP_regval_type``, ``DW_OP_reinterpret``, and ``DW_OP_xderef_type``
  operations are not allowed because the call frame information must not depend
  on other debug sections.

* ``DW_OP_push_object_address`` is not allowed because there is no object
  context to provide a value to push.

* ``DW_OP_LLVM_push_lane`` and ``DW_OP_LLVM_push_iteration`` are not allowed
  because the call frame instructions describe the actions for the whole target
  architecture thread, not the lanes or iterations independently.

* ``DW_OP_call_frame_cfa`` and ``DW_OP_entry_value`` are not allowed because
  their use would be circular.

* ``DW_OP_LLVM_call_frame_entry_reg`` is not allowed if evaluating E causes a
  circular dependency between ``DW_OP_LLVM_call_frame_entry_reg`` operations.

  *For example, if a register R1 has a* ``DW_CFA_def_cfa_expression``
  *instruction that evaluates a* ``DW_OP_LLVM_call_frame_entry_reg`` *operation
  that specifies register R2, and register R2 has a*
  ``DW_CFA_def_cfa_expression`` *instruction that that evaluates a*
  ``DW_OP_LLVM_call_frame_entry_reg`` *operation that specifies register R1.*

*Call frame instructions to which these restrictions apply include*
``DW_CFA_def_cfa_expression``\ *,* ``DW_CFA_expression``\ *, and*
``DW_CFA_val_expression``\ *.*

.. _amdgpu-dwarf-row-creation-instructions:

A.6.4.2.1 Row Creation Instructions
###################################

.. note::

  These instructions are the same as in DWARF Version 5 section 6.4.2.1.

.. _amdgpu-dwarf-cfa-definition-instructions:

A.6.4.2.2 CFA Definition Instructions
#####################################

1.  ``DW_CFA_def_cfa``

    The ``DW_CFA_def_cfa`` instruction takes two unsigned LEB128 operands
    representing a register number R and a (non-factored) byte displacement B.
    AS is set to the target architecture default address space identifier. The
    required action is to define the current CFA rule to be equivalent to the
    result of evaluating the DWARF operation expression ``DW_OP_constu AS;
    DW_OP_LLVM_aspace_bregx R, B`` as a location description.

2.  ``DW_CFA_def_cfa_sf``

    The ``DW_CFA_def_cfa_sf`` instruction takes two operands: an unsigned LEB128
    value representing a register number R and a signed LEB128 factored byte
    displacement B. AS is set to the target architecture default address space
    identifier. The required action is to define the current CFA rule to be
    equivalent to the result of evaluating the DWARF operation expression
    ``DW_OP_constu AS; DW_OP_LLVM_aspace_bregx R, B * data_alignment_factor`` as
    a location description.

    *The action is the same as* ``DW_CFA_def_cfa``\ *, except that the second
    operand is signed and factored.*

3.  ``DW_CFA_LLVM_def_aspace_cfa`` *New*

    The ``DW_CFA_LLVM_def_aspace_cfa`` instruction takes three unsigned LEB128
    operands representing a register number R, a (non-factored) byte
    displacement B, and a target architecture specific address space identifier
    AS. The required action is to define the current CFA rule to be equivalent
    to the result of evaluating the DWARF operation expression ``DW_OP_constu
    AS; DW_OP_LLVM_aspace_bregx R, B`` as a location description.

    If AS is not one of the values defined by the target architecture specific
    ``DW_ASPACE_LLVM_*`` values then the DWARF expression is ill-formed.

4.  ``DW_CFA_LLVM_def_aspace_cfa_sf`` *New*

    The ``DW_CFA_LLVM_def_aspace_cfa_sf`` instruction takes three operands: an
    unsigned LEB128 value representing a register number R, a signed LEB128
    factored byte displacement B, and an unsigned LEB128 value representing a
    target architecture specific address space identifier AS. The required
    action is to define the current CFA rule to be equivalent to the result of
    evaluating the DWARF operation expression ``DW_OP_constu AS;
    DW_OP_LLVM_aspace_bregx R, B * data_alignment_factor`` as a location
    description.

    If AS is not one of the values defined by the target architecture specific
    ``DW_ASPACE_LLVM_*`` values, then the DWARF expression is ill-formed.

    *The action is the same as* ``DW_CFA_aspace_def_cfa``\ *, except that the
    second operand is signed and factored.*

5.  ``DW_CFA_def_cfa_register``

    The ``DW_CFA_def_cfa_register`` instruction takes a single unsigned LEB128
    operand representing a register number R. The required action is to define
    the current CFA rule to be equivalent to the result of evaluating the DWARF
    operation expression ``DW_OP_constu AS; DW_OP_LLVM_aspace_bregx R, B`` as a
    location description. B and AS are the old CFA byte displacement and address
    space respectively.

    If the subprogram has no current CFA rule, or the rule was defined by a
    ``DW_CFA_def_cfa_expression`` instruction, then the DWARF is ill-formed.

6.  ``DW_CFA_def_cfa_offset``

    The ``DW_CFA_def_cfa_offset`` instruction takes a single unsigned LEB128
    operand representing a (non-factored) byte displacement B. The required
    action is to define the current CFA rule to be equivalent to the result of
    evaluating the DWARF operation expression ``DW_OP_constu AS;
    DW_OP_LLVM_aspace_bregx R, B`` as a location description. R and AS are the
    old CFA register number and address space respectively.

    If the subprogram has no current CFA rule, or the rule was defined by a
    ``DW_CFA_def_cfa_expression`` instruction, then the DWARF is ill-formed.

7.  ``DW_CFA_def_cfa_offset_sf``

    The ``DW_CFA_def_cfa_offset_sf`` instruction takes a signed LEB128 operand
    representing a factored byte displacement B. The required action is to
    define the current CFA rule to be equivalent to the result of evaluating the
    DWARF operation expression ``DW_OP_constu AS; DW_OP_LLVM_aspace_bregx R, B *
    data_alignment_factor`` as a location description. R and AS are the old CFA
    register number and address space respectively.

    If the subprogram has no current CFA rule, or the rule was defined by a
    ``DW_CFA_def_cfa_expression`` instruction, then the DWARF is ill-formed.

    *The action is the same as* ``DW_CFA_def_cfa_offset``\ *, except that the
    operand is signed and factored.*

8.  ``DW_CFA_def_cfa_expression``

    The ``DW_CFA_def_cfa_expression`` instruction takes a single operand encoded
    as a ``DW_FORM_exprloc`` value representing a DWARF operation expression E.
    The required action is to define the current CFA rule to be equivalent to
    the result of evaluating E with the current context, except the result kind
    is a location description, the compilation unit is unspecified, the object
    is unspecified, and an empty initial stack.

    *See* :ref:`amdgpu-dwarf-call-frame-instructions` *regarding restrictions on
    the DWARF expression operations that can be used in E.*

    The DWARF is ill-formed if the result of evaluating E is not a memory byte
    address location description.

.. _amdgpu-dwarf-register-rule-instructions:

A.6.4.2.3 Register Rule Instructions
####################################

1.  ``DW_CFA_undefined``

    The ``DW_CFA_undefined`` instruction takes a single unsigned LEB128 operand
    that represents a register number R. The required action is to set the rule
    for the register specified by R to ``undefined``.

2.  ``DW_CFA_same_value``

    The ``DW_CFA_same_value`` instruction takes a single unsigned LEB128 operand
    that represents a register number R. The required action is to set the rule
    for the register specified by R to ``same value``.

3.  ``DW_CFA_offset``

    The ``DW_CFA_offset`` instruction takes two operands: a register number R
    (encoded with the opcode) and an unsigned LEB128 constant representing a
    factored displacement B. The required action is to change the rule for the
    register specified by R to be an *offset(B \* data_alignment_factor)* rule.

    .. note::

      Seems this should be named ``DW_CFA_offset_uf`` since the offset is
      unsigned factored.

4.  ``DW_CFA_offset_extended``

    The ``DW_CFA_offset_extended`` instruction takes two unsigned LEB128
    operands representing a register number R and a factored displacement B.
    This instruction is identical to ``DW_CFA_offset``, except for the encoding
    and size of the register operand.

    .. note::

      Seems this should be named ``DW_CFA_offset_extended_uf`` since the
      displacement is unsigned factored.

5.  ``DW_CFA_offset_extended_sf``

    The ``DW_CFA_offset_extended_sf`` instruction takes two operands: an
    unsigned LEB128 value representing a register number R and a signed LEB128
    factored displacement B. This instruction is identical to
    ``DW_CFA_offset_extended``, except that B is signed.

6.  ``DW_CFA_val_offset``

    The ``DW_CFA_val_offset`` instruction takes two unsigned LEB128 operands
    representing a register number R and a factored displacement B. The required
    action is to change the rule for the register indicated by R to be a
    *val_offset(B \* data_alignment_factor)* rule.

    .. note::

      Seems this should be named ``DW_CFA_val_offset_uf`` since the displacement
      is unsigned factored.

    .. note::

      An alternative is to define ``DW_CFA_val_offset`` to implicitly use the
      target architecture default address space, and add another operation that
      specifies the address space.

7.  ``DW_CFA_val_offset_sf``

    The ``DW_CFA_val_offset_sf`` instruction takes two operands: an unsigned
    LEB128 value representing a register number R and a signed LEB128 factored
    displacement B. This instruction is identical to ``DW_CFA_val_offset``,
    except that B is signed.

8.  ``DW_CFA_register``

    The ``DW_CFA_register`` instruction takes two unsigned LEB128 operands
    representing register numbers R1 and R2 respectively. The required action is
    to set the rule for the register specified by R1 to be a *register(R2)* rule.

9.  ``DW_CFA_expression``

    The ``DW_CFA_expression`` instruction takes two operands: an unsigned LEB128
    value representing a register number R, and a ``DW_FORM_block`` value
    representing a DWARF operation expression E. The required action is to
    change the rule for the register specified by R to be an *expression(E)*
    rule.

    *That is, E computes the location description where the register value can
    be retrieved.*

    *See* :ref:`amdgpu-dwarf-call-frame-instructions` *regarding restrictions on
    the DWARF expression operations that can be used in E.*

10. ``DW_CFA_val_expression``

    The ``DW_CFA_val_expression`` instruction takes two operands: an unsigned
    LEB128 value representing a register number R, and a ``DW_FORM_block`` value
    representing a DWARF operation expression E. The required action is to
    change the rule for the register specified by R to be a *val_expression(E)*
    rule.

    *That is, E computes the value of register R.*

    *See* :ref:`amdgpu-dwarf-call-frame-instructions` *regarding restrictions on
    the DWARF expression operations that can be used in E.*

    If the result of evaluating E is not a value with a base type size that
    matches the register size, then the DWARF is ill-formed.

11. ``DW_CFA_restore``

    The ``DW_CFA_restore`` instruction takes a single operand (encoded with the
    opcode) that represents a register number R. The required action is to
    change the rule for the register specified by R to the rule assigned it by
    the ``initial_instructions`` in the CIE.

12. ``DW_CFA_restore_extended``

    The ``DW_CFA_restore_extended`` instruction takes a single unsigned LEB128
    operand that represents a register number R. This instruction is identical
    to ``DW_CFA_restore``, except for the encoding and size of the register
    operand.

A.6.4.2.4 Row State Instructions
################################

.. note::

  These instructions are the same as in DWARF Version 5 section 6.4.2.4.

A.6.4.2.5 Padding Instruction
#############################

.. note::

  These instructions are the same as in DWARF Version 5 section 6.4.2.5.

A.6.4.3 Call Frame Instruction Usage
++++++++++++++++++++++++++++++++++++

.. note::

  The same as in DWARF Version 5 section 6.4.3.

.. _amdgpu-dwarf-call-frame-calling-address:

A.6.4.4 Call Frame Calling Address
++++++++++++++++++++++++++++++++++

.. note::

  The same as in DWARF Version 5 section 6.4.4.

A.7 Data Representation
-----------------------

.. note::

  This section provides changes to existing debugger information entry
  attributes. These would be incorporated into the corresponding DWARF Version 5
  chapter 7 sections.

.. _amdgpu-dwarf-32-bit-and-64-bit-dwarf-formats:

A.7.4 32-Bit and 64-Bit DWARF Formats
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This augments DWARF Version 5 section 7.4 list item 3's table.

.. table:: ``.debug_info`` section attribute form roles
  :name: amdgpu-dwarf-debug-info-section-attribute-form-roles-table

  ================================== ===================================
  Form                               Role
  ================================== ===================================
  DW_OP_LLVM_aspace_implicit_pointer offset in ``.debug_info``
  ================================== ===================================

A.7.5 Format of Debugging Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A.7.5.4 Attribute Encodings
+++++++++++++++++++++++++++

.. note::

  This augments DWARF Version 5 section 7.5.4 and Table 7.5.

The following table gives the encoding of the additional debugging information
entry attributes.

.. table:: Attribute encodings
   :name: amdgpu-dwarf-attribute-encodings-table

   ================================== ====== ===================================
   Attribute Name                     Value  Classes
   ================================== ====== ===================================
   ``DW_AT_LLVM_active_lane``         0x3e08 exprloc, loclist
   ``DW_AT_LLVM_augmentation``        0x3e09 string
   ``DW_AT_LLVM_lanes``               0x3e0a constant
   ``DW_AT_LLVM_lane_pc``             0x3e0b exprloc, loclist
   ``DW_AT_LLVM_vector_size``         0x3e0c constant
   ``DW_AT_LLVM_iterations``          0x3e0a constant, exprloc, loclist
   ``DW_AT_LLVM_address_space``       TBA    constant
   ``DW_AT_LLVM_memory_space``        TBA    constant
   ================================== ====== ===================================

.. _amdgpu-dwarf-classes-and-forms:

A.7.5.5 Classes and Forms
+++++++++++++++++++++++++

.. note::

  The following modifies the matching text in DWARF Version 5 section 7.5.5.

* reference
    There are four types of reference.

      - The first type of reference...

      - The second type of reference can identify any debugging information
        entry within a .debug_info section; in particular, it may refer to an
        entry in a different compilation unit from the unit containing the
        reference, and may refer to an entry in a different shared object file.
        This type of reference (DW_FORM_ref_addr) is an offset from the
        beginning of the .debug_info section of the target executable or shared
        object file, or, for references within a supplementary object file, an
        offset from the beginning of the local .debug_info section; it is
        relocatable in a relocatable object file and frequently relocated in an
        executable or shared object file. In the 32-bit DWARF format, this
        offset is a 4-byte unsigned value; in the 64-bit DWARF format, it is an
        8-byte unsigned value (see
        :ref:`amdgpu-dwarf-32-bit-and-64-bit-dwarf-formats`).

        *A debugging information entry that may be referenced by another
        compilation unit using DW_FORM_ref_addr must have a global symbolic
        name.*

        *For a reference from one executable or shared object file to another,
        the reference is resolved by the debugger to identify the executable or
        shared object file and the offset into that file's* ``.debug_info``
        *section in the same fashion as the run time loader, either when the
        debug information is first read, or when the reference is used.*

A.7.7 DWARF Expressions
~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  Rename DWARF Version 5 section 7.7 to reflect the unification of location
  descriptions into DWARF expressions.

A.7.7.1 Operation Expressions
+++++++++++++++++++++++++++++

.. note::

  Rename DWARF Version 5 section 7.7.1 and delete section 7.7.2 to reflect the
  unification of location descriptions into DWARF expressions.

  This augments DWARF Version 5 section 7.7.1 and Table 7.9, and adds a new
  table describing vendor extension operations for ``DW_OP_LLVM_user``.

A DWARF operation expression is stored in a block of contiguous bytes. The bytes
form a sequence of operations. Each operation is a 1-byte code that identifies
that operation, followed by zero or more bytes of additional data. The encoding
for the operation ``DW_OP_LLVM_user`` is described in
:ref:`amdgpu-dwarf-operation-encodings-table`, and the encoding of all
``DW_OP_LLVM_user`` vendor extensions operations are described in
:ref:`amdgpu-dwarf-dw-op-llvm-user-vendor-extension-operation-encodings-table`.

.. table:: DWARF Operation Encodings
   :name: amdgpu-dwarf-operation-encodings-table

   ====================================== ===== ======== =========================================================================================
   Operation                              Code  Number   Notes
                                                of
                                                Operands
   ====================================== ===== ======== =========================================================================================
   ``DW_OP_LLVM_user``                    0xe9     1+    ULEB128 vendor extension opcode, followed by vendor extension operands
                                                         defined in :ref:`amdgpu-dwarf-dw-op-llvm-user-vendor-extension-operation-encodings-table`
   ====================================== ===== ======== =========================================================================================

.. table:: DWARF DW_OP_LLVM_user Vendor Extension Operation Encodings
   :name: amdgpu-dwarf-dw-op-llvm-user-vendor-extension-operation-encodings-table

   ====================================== ========= ========== ===============================
   Operation                              Vendor    Number     Notes
                                          Extension of
                                          Opcode    Additional
                                                    Operands
   ====================================== ========= ========== ===============================
   ``DW_OP_LLVM_form_aspace_address``     0x02          0
   ``DW_OP_LLVM_push_lane``               0x03          0
   ``DW_OP_LLVM_offset``                  0x04          0
   ``DW_OP_LLVM_offset_uconst``           0x05          1      ULEB128 byte displacement
   ``DW_OP_LLVM_bit_offset``              0x06          0
   ``DW_OP_LLVM_call_frame_entry_reg``    0x07          1      ULEB128 register number
   ``DW_OP_LLVM_undefined``               0x08          0
   ``DW_OP_LLVM_aspace_bregx``            0x09          2      ULEB128 register number,
                                                               SLEB128 byte displacement
   ``DW_OP_LLVM_piece_end``               0x0a          0
   ``DW_OP_LLVM_extend``                  0x0b          2      ULEB128 bit size,
                                                               ULEB128 count
   ``DW_OP_LLVM_select_bit_piece``        0x0c          2      ULEB128 bit size,
                                                               ULEB128 count
   ``DW_OP_LLVM_aspace_implicit_pointer`` TBA           2      4-byte or 8-byte offset of DIE,
                                                               SLEB128 byte displacement
   ``DW_OP_LLVM_push_iteration``          TBA           0
   ``DW_OP_LLVM_overlay``                 TBA           0
   ``DW_OP_LLVM_bit_overlay``             TBA           0
   ====================================== ========= ========== ===============================

A.7.7.3 Location List Expressions
+++++++++++++++++++++++++++++++++

.. note::

  Rename DWARF Version 5 section 7.7.3 to reflect that location lists are a kind
  of DWARF expression.

A.7.12 Source Languages
~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This augments DWARF Version 5 section 7.12 and Table 7.17.

The following table gives the encoding of the additional DWARF languages.

.. table:: Language encodings
   :name: amdgpu-dwarf-language-encodings-table

   ==================== ====== ===================
   Language Name        Value  Default Lower Bound
   ==================== ====== ===================
   ``DW_LANG_LLVM_HIP`` 0x8100 0
   ==================== ====== ===================

A.7.14 Address Space Encodings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This is a new section after DWARF Version 5 section 7.13 "Address Class and
  Address Space Encodings".

The value of the common address space encoding ``DW_ASPACE_LLVM_none`` is 0.

A.7.15 Memory Space Encodings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This is a new section after DWARF Version 5 section 7.13 "Address Class and
  Address Space Encodings".

The encodings of the constants used for the currently defined memory spaces
are given in :ref:`amdgpu-dwarf-memory-space-encodings-table`.

.. table:: Memory space encodings
   :name: amdgpu-dwarf-memory-space-encodings-table

   =========================== ======
   Memory Space Name           Value
   =========================== ======
   ``DW_MSPACE_LLVM_none``     0x0000
   ``DW_MSPACE_LLVM_global``   0x0001
   ``DW_MSPACE_LLVM_constant`` 0x0002
   ``DW_MSPACE_LLVM_group``    0x0003
   ``DW_MSPACE_LLVM_private``  0x0004
   ``DW_MSPACE_LLVM_lo_user``  0x8000
   ``DW_MSPACE_LLVM_hi_user``  0xffff
   =========================== ======

A.7.22 Line Number Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This augments DWARF Version 5 section 7.22 and Table 7.27.

The following table gives the encoding of the additional line number header
entry formats.

.. table:: Line number header entry format encodings
  :name: amdgpu-dwarf-line-number-header-entry-format-encodings-table

  ====================================  ====================
  Line number header entry format name  Value
  ====================================  ====================
  ``DW_LNCT_LLVM_source``               0x2001
  ``DW_LNCT_LLVM_is_MD5``               0x2002
  ====================================  ====================

.. _amdgpu-dwarf-call-frame-information-encoding:

A.7.24 Call Frame Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This augments DWARF Version 5 section 7.24 and Table 7.29.

The following table gives the encoding of the additional call frame information
instructions.

.. table:: Call frame instruction encodings
   :name: amdgpu-dwarf-call-frame-instruction-encodings-table

   ================================= ====== ====== ================ ================ =====================
   Instruction                       High 2 Low 6  Operand 1        Operand 2        Operand 3
                                     Bits   Bits
   ================================= ====== ====== ================ ================ =====================
   ``DW_CFA_LLVM_def_aspace_cfa``    0      0x30   ULEB128 register ULEB128 offset   ULEB128 address space
   ``DW_CFA_LLVM_def_aspace_cfa_sf`` 0      0x31   ULEB128 register SLEB128 offset   ULEB128 address space
   ================================= ====== ====== ================ ================ =====================

A.7.32 Type Signature Computation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::

  This augments (in alphabetical order) DWARF Version 5 section 7.32, Table
  7.32.

.. table:: Attributes used in type signature computation
   :name: amdgpu-dwarf-attributes-used-in-type-signature-computation-table

   ================================== =======
   ``DW_AT_LLVM_address_space``
   ``DW_AT_LLVM_memory_space``
   ``DW_AT_LLVM_vector_size``
   ================================== =======

A. Attributes by Tag Value (Informative)
----------------------------------------

.. note::

  This augments DWARF Version 5 Appendix A and Table A.1.

The following table provides the additional attributes that are applicable to
debugger information entries.

.. table:: Attributes by tag value
   :name: amdgpu-dwarf-attributes-by-tag-value-table

   ================================== =============================
   Tag Name                           Applicable Attributes
   ================================== =============================
   ``DW_TAG_base_type``               * ``DW_AT_LLVM_vector_size``
   ``DW_TAG_pointer_type``            * ``DW_AT_LLVM_address_space``
                                      * ``DW_AT_LLVM_memory_space``
   ``DW_TAG_reference_type``          * ``DW_AT_LLVM_address_space``
                                      * ``DW_AT_LLVM_memory_space``
   ``DW_TAG_rvalue_reference_type``   * ``DW_AT_LLVM_address_space``
                                      * ``DW_AT_LLVM_memory_space``
   ``DW_TAG_variable``                * ``DW_AT_LLVM_memory_space``
   ``DW_TAG_formal_parameter``        * ``DW_AT_LLVM_memory_space``
   ``DW_TAG_constant``                * ``DW_AT_LLVM_memory_space``
   ``DW_TAG_compile_unit``            * ``DW_AT_LLVM_augmentation``
   ``DW_TAG_entry_point``             * ``DW_AT_LLVM_active_lane``
                                      * ``DW_AT_LLVM_lane_pc``
                                      * ``DW_AT_LLVM_lanes``
                                      * ``DW_AT_LLVM_iterations``
   ``DW_TAG_inlined_subroutine``      * ``DW_AT_LLVM_active_lane``
                                      * ``DW_AT_LLVM_lane_pc``
                                      * ``DW_AT_LLVM_lanes``
                                      * ``DW_AT_LLVM_iterations``
   ``DW_TAG_subprogram``              * ``DW_AT_LLVM_active_lane``
                                      * ``DW_AT_LLVM_lane_pc``
                                      * ``DW_AT_LLVM_lanes``
                                      * ``DW_AT_LLVM_iterations``
   ================================== =============================

D. Examples (Informative)
-------------------------

.. note::

  This modifies the corresponding DWARF Version 5 Appendix D examples.

D.1 General Description Examples
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

D.1.3 DWARF Location Description Examples
+++++++++++++++++++++++++++++++++++++++++

``DW_OP_offset_uconst 4``
  A structure member is four bytes from the start of the structure instance. The
  location description of the base of the structure instance is assumed to be
  already on the stack.

``DW_OP_entry_value 1 DW_OP_reg5 DW_OP_offset_uconst 16``
  The address of the memory location is calculated by adding 16 to the value
  contained in register 5 upon entering the current subprogram.

D.2 Aggregate Examples
~~~~~~~~~~~~~~~~~~~~~~

D.2.1 Fortran Simple Array Example
++++++++++++++++++++++++++++++++++

Figure D.4: Fortran array example: DWARF description

.. code::
  :number-lines:

  -------------------------------------------------------------------------------
  ! Description for type of 'ap'
  !
  1$: DW_TAG_array_type
          ! No name, default (Fortran) ordering, default stride
          DW_AT_type(reference to REAL)
          DW_AT_associated(expression=    ! Test 'ptr_assoc' flag
              DW_OP_push_object_address
              DW_OP_lit<n>                ! where n == offset(ptr_assoc)
              DW_OP_offset
              DW_OP_deref
              DW_OP_lit1                  ! mask for 'ptr_assoc' flag
              DW_OP_and)
          DW_AT_data_location(expression= ! Get raw data address
              DW_OP_push_object_address
              DW_OP_lit<n>                ! where n == offset(base)
              DW_OP_offset
              DW_OP_deref)                ! Type of index of array 'ap'
  2$:     DW_TAG_subrange_type
              ! No name, default stride
              DW_AT_type(reference to INTEGER)
              DW_AT_lower_bound(expression=
                  DW_OP_push_object_address
                  DW_OP_lit<n>            ! where n ==
                                          !   offset(desc, dims) +
                                          !   offset(dims_str, lower_bound)
                  DW_OP_offset
                  DW_OP_deref)
              DW_AT_upper_bound(expression=
                  DW_OP_push_object_address
                  DW_OP_lit<n>            ! where n ==
                                          !   offset(desc, dims) +
                                          !   offset(dims_str, upper_bound)
                  DW_OP_offset
                  DW_OP_deref)
  !  Note: for the m'th dimension, the second operator becomes
  !  DW_OP_lit<n> where
  !       n == offset(desc, dims)          +
  !                (m-1)*sizeof(dims_str)  +
  !                 offset(dims_str, [lower|upper]_bound)
  !  That is, the expression does not get longer for each successive
  !  dimension (other than to express the larger offsets involved).
  3$: DW_TAG_structure_type
          DW_AT_name("array_ptr")
          DW_AT_byte_size(constant sizeof(REAL) + sizeof(desc<1>))
  4$:     DW_TAG_member
              DW_AT_name("myvar")
              DW_AT_type(reference to REAL)
              DW_AT_data_member_location(constant 0)
  5$:     DW_TAG_member
              DW_AT_name("ap");
              DW_AT_type(reference to 1$)
              DW_AT_data_member_location(constant sizeof(REAL))
  6$: DW_TAG_array_type
          ! No name, default (Fortran) ordering, default stride
          DW_AT_type(reference to 3$)
          DW_AT_allocated(expression=       ! Test 'ptr_alloc' flag
              DW_OP_push_object_address
              DW_OP_lit<n>                  ! where n == offset(ptr_alloc)
              DW_OP_offset
              DW_OP_deref
              DW_OP_lit2                    ! Mask for 'ptr_alloc' flag
              DW_OP_and)
          DW_AT_data_location(expression=   ! Get raw data address
              DW_OP_push_object_address
              DW_OP_lit<n>                  ! where n == offset(base)
              DW_OP_offset
              DW_OP_deref)
  7$:     DW_TAG_subrange_type
              ! No name, default stride
              DW_AT_type(reference to INTEGER)
              DW_AT_lower_bound(expression=
                  DW_OP_push_object_address
                  DW_OP_lit<n>              ! where n == ...
                  DW_OP_offset
                  DW_OP_deref)
              DW_AT_upper_bound(expression=
                  DW_OP_push_object_address
                  DW_OP_lit<n>              ! where n == ...
                  DW_OP_offset
                  DW_OP_deref)
  8$: DW_TAG_variable
          DW_AT_name("arrayvar")
          DW_AT_type(reference to 6$)
          DW_AT_location(expression=
              ...as appropriate...)         ! Assume static allocation
  -------------------------------------------------------------------------------

D.2.3 Fortran 2008 Assumed-rank Array Example
+++++++++++++++++++++++++++++++++++++++++++++

Figure D.13: Sample DWARF for the array descriptor in Figure D.12

.. code::
  :number-lines:

  ----------------------------------------------------------------------------
  10$:  DW_TAG_array_type
          DW_AT_type(reference to real)
          DW_AT_rank(expression=
              DW_OP_push_object_address
              DW_OP_lit<n>
              DW_OP_offset
              DW_OP_deref)
          DW_AT_data_location(expression=
              DW_OP_push_object_address
              DW_OP_lit<n>
              DW_OP_offset
              DW_OP_deref)
  11$:     DW_TAG_generic_subrange
              DW_AT_type(reference to integer)
              !   offset of rank in descriptor
              !   offset of data in descriptor
              DW_AT_lower_bound(expression=
              !   Looks up the lower bound of dimension i.
              !   Operation                       ! Stack effect
              !   (implicit)                      ! i
                  DW_OP_lit<n>                    ! i sizeof(dim)
                  DW_OP_mul                       ! dim[i]
                  DW_OP_lit<n>                    ! dim[i] offsetof(dim)
                  DW_OP_plus                      ! dim[i]+offset
                  DW_OP_push_object_address       ! dim[i]+offsetof(dim) objptr
                  DW_OP_swap                      ! objptr dim[i]+offsetof(dim)
                  DW_OP_offset                    ! objptr.dim[i]
                  DW_OP_lit<n>                    ! objptr.dim[i] offsetof(lb)
                  DW_OP_offset                    ! objptr.dim[i].lowerbound
                  DW_OP_deref)                    ! *objptr.dim[i].lowerbound
              DW_AT_upper_bound(expression=
              !   Looks up the upper bound of dimension i.
                  DW_OP_lit<n>                    ! sizeof(dim)
                  DW_OP_mul
                  DW_OP_lit<n>                    ! offsetof(dim)
                  DW_OP_plus
                  DW_OP_push_object_address
                  DW_OP_swap
                  DW_OP_offset
                  DW_OP_lit<n>                    ! offset of upperbound in dim
                  DW_OP_offset
                  DW_OP_deref)
              DW_AT_byte_stride(expression=
              !   Looks up the byte stride of dimension i.
                  ...
              !   (analogous to DW_AT_upper_bound)
                  )
  ----------------------------------------------------------------------------

.. note::

  This example suggests that ``DW_AT_lower_bound`` and ``DW_AT_upper_bound``
  evaluate an exprloc with an initial stack containing the rank value. The
  attribute definition should be updated to state this.

D.2.6 Ada Example
+++++++++++++++++

Figure D.20: Ada example: DWARF description

.. code::
  :number-lines:

  ----------------------------------------------------------------------------
  11$:  DW_TAG_variable
            DW_AT_name("M")
            DW_AT_type(reference to INTEGER)
  12$:  DW_TAG_array_type
            ! No name, default (Ada) order, default stride
            DW_AT_type(reference to INTEGER)
  13$:      DW_TAG_subrange_type
                DW_AT_type(reference to INTEGER)
                DW_AT_lower_bound(constant 1)
                DW_AT_upper_bound(reference to variable M at 11$)
  14$:  DW_TAG_variable
            DW_AT_name("VEC1")
            DW_AT_type(reference to array type at 12$)
        ...
  21$:  DW_TAG_subrange_type
            DW_AT_name("TEENY")
            DW_AT_type(reference to INTEGER)
            DW_AT_lower_bound(constant 1)
            DW_AT_upper_bound(constant 100)
        ...
  26$:  DW_TAG_structure_type
            DW_AT_name("REC2")
  27$:      DW_TAG_member
                DW_AT_name("N")
                DW_AT_type(reference to subtype TEENY at 21$)
                DW_AT_data_member_location(constant 0)
  28$:      DW_TAG_array_type
                ! No name, default (Ada) order, default stride
                ! Default data location
                DW_AT_type(reference to INTEGER)
  29$:          DW_TAG_subrange_type
                    DW_AT_type(reference to subrange TEENY at 21$)
                    DW_AT_lower_bound(constant 1)
                    DW_AT_upper_bound(reference to member N at 27$)
  30$:      DW_TAG_member
                DW_AT_name("VEC2")
                DW_AT_type(reference to array "subtype" at 28$)
                DW_AT_data_member_location(machine=
                    DW_OP_lit<n>                ! where n == offset(REC2, VEC2)
                    DW_OP_offset)
        ...
  41$:  DW_TAG_variable
            DW_AT_name("OBJ2B")
            DW_AT_type(reference to REC2 at 26$)
            DW_AT_location(...as appropriate...)
  ----------------------------------------------------------------------------

.. _amdgpu-dwarf-further-examples:

C. Further Examples
===================

The AMD GPU specific usage of the features in these extensions, including
examples, is available at *User Guide for AMDGPU Backend* section
:ref:`amdgpu-dwarf-debug-information`.

.. note::

  Change examples to use ``DW_OP_LLVM_offset`` instead of ``DW_OP_add`` when
  acting on a location description.

  Need to provide examples of new features.

.. _amdgpu-dwarf-references:

D. References
=============

    .. _amdgpu-dwarf-AMD:

1.  [AMD] `Advanced Micro Devices <https://www.amd.com/>`__

    .. _amdgpu-dwarf-AMD-ROCgdb:

2.  [AMD-ROCgdb] `AMD ROCm Debugger (ROCgdb) <https://github.com/ROCm-Developer-Tools/ROCgdb>`__

    .. _amdgpu-dwarf-AMD-ROCm:

3.  [AMD-ROCm] `AMD ROCm Platform <https://rocm-documentation.readthedocs.io>`__

    .. _amdgpu-dwarf-AMDGPU-DWARF-LOC:

4.  [AMDGPU-DWARF-LOC] `Allow Location Descriptions on the DWARF Expression Stack <https://llvm.org/docs/AMDGPUDwarfExtensionAllowLocationDescriptionOnTheDwarfExpressionStack/AMDGPUDwarfExtensionAllowLocationDescriptionOnTheDwarfExpressionStack.html>`__

    .. _amdgpu-dwarf-AMDGPU-LLVM:

5.  [AMDGPU-LLVM] `User Guide for AMDGPU LLVM Backend <https://llvm.org/docs/AMDGPUUsage.html>`__

    .. _amdgpu-dwarf-CUDA:

6.  [CUDA] `Nvidia CUDA Language <https://docs.nvidia.com/cuda/cuda-c-programming-guide/>`__

    .. _amdgpu-dwarf-DWARF:

7.  [DWARF] `DWARF Debugging Information Format <http://dwarfstd.org/>`__

    .. _amdgpu-dwarf-ELF:

8.  [ELF] `Executable and Linkable Format (ELF) <http://www.sco.com/developers/gabi/>`__

    .. _amdgpu-dwarf-GCC:

9.  [GCC] `GCC: The GNU Compiler Collection <https://www.gnu.org/software/gcc/>`__

    .. _amdgpu-dwarf-GDB:

10. [GDB] `GDB: The GNU Project Debugger <https://www.gnu.org/software/gdb/>`__

    .. _amdgpu-dwarf-HIP:

11. [HIP] `HIP Programming Guide <https://rocm-documentation.readthedocs.io/en/latest/Programming_Guides/Programming-Guides.html#hip-programing-guide>`__

    .. _amdgpu-dwarf-HSA:

12. [HSA] `Heterogeneous System Architecture (HSA) Foundation <http://www.hsafoundation.com/>`__

    .. _amdgpu-dwarf-LLVM:

13. [LLVM] `The LLVM Compiler Infrastructure <https://llvm.org/>`__

    .. _amdgpu-dwarf-OpenCL:

14. [OpenCL] `The OpenCL Specification Version 2.0 <http://www.khronos.org/registry/cl/specs/opencl-2.0.pdf>`__

    .. _amdgpu-dwarf-Perforce-TotalView:

15. [Perforce-TotalView] `Perforce TotalView HPC Debugging Software <https://totalview.io/products/totalview>`__

    .. _amdgpu-dwarf-SEMVER:

16. [SEMVER] `Semantic Versioning <https://semver.org/>`__
