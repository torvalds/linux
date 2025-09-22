
.. _gmir-opcodes:

Generic Opcodes
===============

.. contents::
   :local:

.. note::

  This documentation does not yet fully account for vectors. Many of the
  scalar/integer/floating-point operations can also take vectors.

Constants
---------

G_IMPLICIT_DEF
^^^^^^^^^^^^^^

An undefined value.

.. code-block:: none

  %0:_(s32) = G_IMPLICIT_DEF

G_CONSTANT
^^^^^^^^^^

An integer constant.

.. code-block:: none

  %0:_(s32) = G_CONSTANT i32 1

G_FCONSTANT
^^^^^^^^^^^

A floating point constant.

.. code-block:: none

  %0:_(s32) = G_FCONSTANT float 1.0

G_FRAME_INDEX
^^^^^^^^^^^^^

The address of an object in the stack frame.

.. code-block:: none

  %1:_(p0) = G_FRAME_INDEX %stack.0.ptr0

G_GLOBAL_VALUE
^^^^^^^^^^^^^^

The address of a global value.

.. code-block:: none

  %0(p0) = G_GLOBAL_VALUE @var_local

G_PTRAUTH_GLOBAL_VALUE
^^^^^^^^^^^^^^^^^^^^^^

The signed address of a global value. Operands: address to be signed (pointer),
key (32-bit imm), address for address discrimination (zero if not needed) and
an extra discriminator (64-bit imm).

.. code-block:: none

  %0:_(p0) = G_PTRAUTH_GLOBAL_VALUE %1:_(p0), s32, %2:_(p0), s64

G_BLOCK_ADDR
^^^^^^^^^^^^

The address of a basic block.

.. code-block:: none

  %0:_(p0) = G_BLOCK_ADDR blockaddress(@test_blockaddress, %ir-block.block)

G_CONSTANT_POOL
^^^^^^^^^^^^^^^

The address of an object in the constant pool.

.. code-block:: none

  %0:_(p0) = G_CONSTANT_POOL %const.0

Integer Extension and Truncation
--------------------------------

G_ANYEXT
^^^^^^^^

Extend the underlying scalar type of an operation, leaving the high bits
unspecified.

.. code-block:: none

  %1:_(s32) = G_ANYEXT %0:_(s16)

G_SEXT
^^^^^^

Sign extend the underlying scalar type of an operation, copying the sign bit
into the newly-created space.

.. code-block:: none

  %1:_(s32) = G_SEXT %0:_(s16)

G_SEXT_INREG
^^^^^^^^^^^^

Sign extend the value from an arbitrary bit position, copying the sign bit
into all bits above it. This is equivalent to a shl + ashr pair with an
appropriate shift amount. $sz is an immediate (MachineOperand::isImm()
returns true) to allow targets to have some bitwidths legal and others
lowered. This opcode is particularly useful if the target has sign-extension
instructions that are cheaper than the constituent shifts as the optimizer is
able to make decisions on whether it's better to hang on to the G_SEXT_INREG
or to lower it and optimize the individual shifts.

.. code-block:: none

  %1:_(s32) = G_SEXT_INREG %0:_(s32), 16

G_ZEXT
^^^^^^

Zero extend the underlying scalar type of an operation, putting zero bits
into the newly-created space.

.. code-block:: none

  %1:_(s32) = G_ZEXT %0:_(s16)

G_TRUNC
^^^^^^^

Truncate the underlying scalar type of an operation. This is equivalent to
G_EXTRACT for scalar types, but acts elementwise on vectors.

.. code-block:: none

  %1:_(s16) = G_TRUNC %0:_(s32)

Type Conversions
----------------

G_INTTOPTR
^^^^^^^^^^

Convert an integer to a pointer.

.. code-block:: none

  %1:_(p0) = G_INTTOPTR %0:_(s32)

G_PTRTOINT
^^^^^^^^^^

Convert a pointer to an integer.

.. code-block:: none

  %1:_(s32) = G_PTRTOINT %0:_(p0)

G_BITCAST
^^^^^^^^^

Reinterpret a value as a new type. This is usually done without
changing any bits but this is not always the case due a subtlety in the
definition of the :ref:`LLVM-IR Bitcast Instruction <i_bitcast>`. It
is allowed to bitcast between pointers with the same size, but
different address spaces.

.. code-block:: none

  %1:_(s64) = G_BITCAST %0:_(<2 x s32>)

G_ADDRSPACE_CAST
^^^^^^^^^^^^^^^^

Convert a pointer to an address space to a pointer to another address space.

.. code-block:: none

  %1:_(p1) = G_ADDRSPACE_CAST %0:_(p0)

.. caution::

  :ref:`i_addrspacecast` doesn't mention what happens if the cast is simply
  invalid (i.e. if the address spaces are disjoint).

Scalar Operations
-----------------

G_EXTRACT
^^^^^^^^^

Extract a register of the specified size, starting from the block given by
index. This will almost certainly be mapped to sub-register COPYs after
register banks have been selected.

.. code-block:: none

  %3:_(s32) = G_EXTRACT %2:_(s64), 32

G_INSERT
^^^^^^^^

Insert a smaller register into a larger one at the specified bit-index.

.. code-block:: none

  %2:_(s64) = G_INSERT %0:(_s64), %1:_(s32), 0

G_MERGE_VALUES
^^^^^^^^^^^^^^

Concatenate multiple registers of the same size into a wider register.
The input operands are always ordered from lowest bits to highest:

.. code-block:: none

  %0:(s32) = G_MERGE_VALUES %bits_0_7:(s8), %bits_8_15:(s8),
                            %bits_16_23:(s8), %bits_24_31:(s8)

G_UNMERGE_VALUES
^^^^^^^^^^^^^^^^

Extract multiple registers of the specified size, starting from blocks given by
indexes. This will almost certainly be mapped to sub-register COPYs after
register banks have been selected.
The output operands are always ordered from lowest bits to highest:

.. code-block:: none

  %bits_0_7:(s8), %bits_8_15:(s8),
      %bits_16_23:(s8), %bits_24_31:(s8) = G_UNMERGE_VALUES %0:(s32)

G_BSWAP
^^^^^^^

Reverse the order of the bytes in a scalar.

.. code-block:: none

  %1:_(s32) = G_BSWAP %0:_(s32)

G_BITREVERSE
^^^^^^^^^^^^

Reverse the order of the bits in a scalar.

.. code-block:: none

  %1:_(s32) = G_BITREVERSE %0:_(s32)

G_SBFX, G_UBFX
^^^^^^^^^^^^^^

Extract a range of bits from a register.

The source operands are registers as follows:

- Source
- The least-significant bit for the extraction
- The width of the extraction

The least-significant bit (lsb) and width operands are in the range:

::

      0 <= lsb < lsb + width <= source bitwidth, where all values are unsigned

G_SBFX sign-extends the result, while G_UBFX zero-extends the result.

.. code-block:: none

  ; Extract 5 bits starting at bit 1 from %x and store them in %a.
  ; Sign-extend the result.
  ;
  ; Example:
  ; %x = 0...0000[10110]1 ---> %a = 1...111111[10110]
  %lsb_one = G_CONSTANT i32 1
  %width_five = G_CONSTANT i32 5
  %a:_(s32) = G_SBFX %x, %lsb_one, %width_five

  ; Extract 3 bits starting at bit 2 from %x and store them in %b. Zero-extend
  ; the result.
  ;
  ; Example:
  ; %x = 1...11111[100]11 ---> %b = 0...00000[100]
  %lsb_two = G_CONSTANT i32 2
  %width_three = G_CONSTANT i32 3
  %b:_(s32) = G_UBFX %x, %lsb_two, %width_three

Integer Operations
-------------------

G_ADD, G_SUB, G_MUL, G_AND, G_OR, G_XOR, G_SDIV, G_UDIV, G_SREM, G_UREM
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These each perform their respective integer arithmetic on a scalar.

.. code-block:: none

  %dst:_(s32) = G_ADD %src0:_(s32), %src1:_(s32)

The above example adds %src1 to %src0 and stores the result in %dst.

G_SDIVREM, G_UDIVREM
^^^^^^^^^^^^^^^^^^^^

Perform integer division and remainder thereby producing two results.

.. code-block:: none

  %div:_(s32), %rem:_(s32) = G_SDIVREM %0:_(s32), %1:_(s32)

G_SADDSAT, G_UADDSAT, G_SSUBSAT, G_USUBSAT, G_SSHLSAT, G_USHLSAT
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Signed and unsigned addition, subtraction and left shift with saturation.

.. code-block:: none

  %2:_(s32) = G_SADDSAT %0:_(s32), %1:_(s32)

G_SHL, G_LSHR, G_ASHR
^^^^^^^^^^^^^^^^^^^^^

Shift the bits of a scalar left or right inserting zeros (sign-bit for G_ASHR).

G_ROTR, G_ROTL
^^^^^^^^^^^^^^

Rotate the bits right (G_ROTR) or left (G_ROTL).

G_ICMP
^^^^^^

Perform integer comparison producing non-zero (true) or zero (false). It's
target specific whether a true value is 1, ~0U, or some other non-zero value.

G_SCMP
^^^^^^

Perform signed 3-way integer comparison producing -1 (smaller), 0 (equal), or 1 (larger).

.. code-block:: none

  %5:_(s32) = G_SCMP %6, %2


G_UCMP
^^^^^^

Perform unsigned 3-way integer comparison producing -1 (smaller), 0 (equal), or 1 (larger).

.. code-block:: none

  %7:_(s32) = G_UCMP %2, %6


G_SELECT
^^^^^^^^

Select between two values depending on a zero/non-zero value.

.. code-block:: none

  %5:_(s32) = G_SELECT %4(s1), %6, %2

G_PTR_ADD
^^^^^^^^^

Add a scalar offset in addressible units to a pointer. Addressible units are
typically bytes but this may vary between targets.

.. code-block:: none

  %1:_(p0) = G_PTR_ADD %0:_(p0), %1:_(s32)

.. caution::

  There are currently no in-tree targets that use this with addressable units
  not equal to 8 bit.

G_PTRMASK
^^^^^^^^^^

Zero out an arbitrary mask of bits of a pointer. The mask type must be
an integer, and the number of vector elements must match for all
operands. This corresponds to `i_intr_llvm_ptrmask`.

.. code-block:: none

  %2:_(p0) = G_PTRMASK %0, %1

G_SMIN, G_SMAX, G_UMIN, G_UMAX
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Take the minimum/maximum of two values.

.. code-block:: none

  %5:_(s32) = G_SMIN %6, %2

G_ABS
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Take the absolute value of a signed integer. The absolute value of the minimum
negative value (e.g. the 8-bit value `0x80`) is defined to be itself.

.. code-block:: none

  %1:_(s32) = G_ABS %0

G_UADDO, G_SADDO, G_USUBO, G_SSUBO, G_SMULO, G_UMULO
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Perform the requested arithmetic and produce a carry output in addition to the
normal result.

.. code-block:: none

  %3:_(s32), %4:_(s1) = G_UADDO %0, %1

G_UADDE, G_SADDE, G_USUBE, G_SSUBE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Perform the requested arithmetic and consume a carry input in addition to the
normal input. Also produce a carry output in addition to the normal result.

.. code-block:: none

  %4:_(s32), %5:_(s1) = G_UADDE %0, %1, %3:_(s1)

G_UMULH, G_SMULH
^^^^^^^^^^^^^^^^

Multiply two numbers at twice the incoming bit width (unsigned or signed) and
return the high half of the result.

.. code-block:: none

  %3:_(s32) = G_UMULH %0, %1

G_CTLZ, G_CTTZ, G_CTPOP
^^^^^^^^^^^^^^^^^^^^^^^

Count leading zeros, trailing zeros, or number of set bits.

.. code-block:: none

  %2:_(s33) = G_CTLZ_ZERO_UNDEF %1
  %2:_(s33) = G_CTTZ_ZERO_UNDEF %1
  %2:_(s33) = G_CTPOP %1

G_CTLZ_ZERO_UNDEF, G_CTTZ_ZERO_UNDEF
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Count leading zeros or trailing zeros. If the value is zero then the result is
undefined.

.. code-block:: none

  %2:_(s33) = G_CTLZ_ZERO_UNDEF %1
  %2:_(s33) = G_CTTZ_ZERO_UNDEF %1

Floating Point Operations
-------------------------

G_FCMP
^^^^^^

Perform floating point comparison producing non-zero (true) or zero
(false). It's target specific whether a true value is 1, ~0U, or some other
non-zero value.

G_FNEG
^^^^^^

Floating point negation.

G_FPEXT
^^^^^^^

Convert a floating point value to a larger type.

G_FPTRUNC
^^^^^^^^^

Convert a floating point value to a narrower type.

G_FPTOSI, G_FPTOUI, G_SITOFP, G_UITOFP
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Convert between integer and floating point.

G_FABS
^^^^^^

Take the absolute value of a floating point value.

G_FCOPYSIGN
^^^^^^^^^^^

Copy the value of the first operand, replacing the sign bit with that of the
second operand.

G_FCANONICALIZE
^^^^^^^^^^^^^^^

See :ref:`i_intr_llvm_canonicalize`.

G_IS_FPCLASS
^^^^^^^^^^^^

Tests if the first operand, which must be floating-point scalar or vector, has
floating-point class specified by the second operand. Returns non-zero (true)
or zero (false). It's target specific whether a true value is 1, ~0U, or some
other non-zero value. If the first operand is a vector, the returned value is a
vector of the same length.

G_FMINNUM
^^^^^^^^^

Perform floating-point minimum on two values.

In the case where a single input is a NaN (either signaling or quiet),
the non-NaN input is returned.

The return value of (FMINNUM 0.0, -0.0) could be either 0.0 or -0.0.

G_FMAXNUM
^^^^^^^^^

Perform floating-point maximum on two values.

In the case where a single input is a NaN (either signaling or quiet),
the non-NaN input is returned.

The return value of (FMAXNUM 0.0, -0.0) could be either 0.0 or -0.0.

G_FMINNUM_IEEE
^^^^^^^^^^^^^^

Perform floating-point minimum on two values, following IEEE-754
definitions. This differs from FMINNUM in the handling of signaling
NaNs.

If one input is a signaling NaN, returns a quiet NaN. This matches
IEEE-754 2008's minnum/maxnum for signaling NaNs (which differs from
2019).

These treat -0 as ordered less than +0, matching the behavior of
IEEE-754 2019's minimumNumber/maximumNumber (which was unspecified in
2008).

G_FMAXNUM_IEEE
^^^^^^^^^^^^^^

Perform floating-point maximum on two values, following IEEE-754
definitions. This differs from FMAXNUM in the handling of signaling
NaNs.

If one input is a signaling NaN, returns a quiet NaN. This matches
IEEE-754 2008's minnum/maxnum for signaling NaNs (which differs from
2019).

These treat -0 as ordered less than +0, matching the behavior of
IEEE-754 2019's minimumNumber/maximumNumber (which was unspecified in
2008).

G_FMINIMUM
^^^^^^^^^^

NaN-propagating minimum that also treat -0.0 as less than 0.0. While
FMINNUM_IEEE follow IEEE 754-2008 semantics, FMINIMUM follows IEEE
754-2019 semantics.

G_FMAXIMUM
^^^^^^^^^^

NaN-propagating maximum that also treat -0.0 as less than 0.0. While
FMAXNUM_IEEE follow IEEE 754-2008 semantics, FMAXIMUM follows IEEE
754-2019 semantics.

G_FADD, G_FSUB, G_FMUL, G_FDIV, G_FREM
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Perform the specified floating point arithmetic.

G_FMA
^^^^^

Perform a fused multiply add (i.e. without the intermediate rounding step).

G_FMAD
^^^^^^

Perform a non-fused multiply add (i.e. with the intermediate rounding step).

G_FPOW
^^^^^^

Raise the first operand to the power of the second.

G_FEXP, G_FEXP2
^^^^^^^^^^^^^^^

Calculate the base-e or base-2 exponential of a value

G_FLOG, G_FLOG2, G_FLOG10
^^^^^^^^^^^^^^^^^^^^^^^^^

Calculate the base-e, base-2, or base-10 respectively.

G_FCEIL, G_FSQRT, G_FFLOOR, G_FRINT, G_FNEARBYINT
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These correspond to the standard C functions of the same name.

G_FCOS, G_FSIN, G_FTAN, G_FACOS, G_FASIN, G_FATAN, G_FCOSH, G_FSINH, G_FTANH
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These correspond to the standard C trigonometry functions of the same name.

G_INTRINSIC_TRUNC
^^^^^^^^^^^^^^^^^

Returns the operand rounded to the nearest integer not larger in magnitude than the operand.

G_INTRINSIC_ROUND
^^^^^^^^^^^^^^^^^

Returns the operand rounded to the nearest integer.

G_LROUND, G_LLROUND
^^^^^^^^^^^^^^^^^^^

Returns the source operand rounded to the nearest integer with ties away from
zero.

See the LLVM LangRef entry on '``llvm.lround.*'`` for details on behaviour.

.. code-block:: none

  %rounded_32:_(s32) = G_LROUND %round_me:_(s64)
  %rounded_64:_(s64) = G_LLROUND %round_me:_(s64)

Vector Specific Operations
--------------------------

G_VSCALE
^^^^^^^^

Puts the value of the runtime ``vscale`` multiplied by the value in the source
operand into the destination register. This can be useful in determining the
actual runtime number of elements in a vector.

.. code-block::

  %0:_(s32) = G_VSCALE 4

G_INSERT_SUBVECTOR
^^^^^^^^^^^^^^^^^^

Insert the second source vector into the first source vector. The index operand
represents the starting index in the first source vector at which the second
source vector should be inserted into.

The index must be a constant multiple of the second source vector's minimum
vector length. If the vectors are scalable, then the index is first scaled by
the runtime scaling factor. The indices inserted in the source vector must be
valid indices of that vector. If this condition cannot be determined statically
but is false at runtime, then the result vector is undefined.

.. code-block:: none

  %2:_(<vscale x 4 x i64>) = G_INSERT_SUBVECTOR %0:_(<vscale x 4 x i64>), %1:_(<vscale x 2 x i64>), 0

G_EXTRACT_SUBVECTOR
^^^^^^^^^^^^^^^^^^^

Extract a vector of destination type from the source vector. The index operand
represents the starting index from which a subvector is extracted from
the source vector.

The index must be a constant multiple of the source vector's minimum vector
length. If the source vector is a scalable vector, then the index is first
scaled by the runtime scaling factor. The indices extracted from the source
vector must be valid indices of that vector. If this condition cannot be
determined statically but is false at runtime, then the result vector is
undefined.

.. code-block:: none

  %3:_(<vscale x 4 x i64>) = G_EXTRACT_SUBVECTOR %2:_(<vscale x 8 x i64>), 2

G_CONCAT_VECTORS
^^^^^^^^^^^^^^^^

Concatenate two vectors to form a longer vector.

G_BUILD_VECTOR, G_BUILD_VECTOR_TRUNC
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Create a vector from multiple scalar registers. No implicit
conversion is performed (i.e. the result element type must be the
same as all source operands)

The _TRUNC version truncates the larger operand types to fit the
destination vector elt type.

G_INSERT_VECTOR_ELT
^^^^^^^^^^^^^^^^^^^

Insert an element into a vector

G_EXTRACT_VECTOR_ELT
^^^^^^^^^^^^^^^^^^^^

Extract an element from a vector

G_SHUFFLE_VECTOR
^^^^^^^^^^^^^^^^

Concatenate two vectors and shuffle the elements according to the mask operand.
The mask operand should be an IR Constant which exactly matches the
corresponding mask for the IR shufflevector instruction.

G_SPLAT_VECTOR
^^^^^^^^^^^^^^^^

Create a vector where all elements are the scalar from the source operand.

The type of the operand must be equal to or larger than the vector element
type. If the operand is larger than the vector element type, the scalar is
implicitly truncated to the vector element type.

G_VECTOR_COMPRESS
^^^^^^^^^^^^^^^^^

Given an input vector, a mask vector, and a passthru vector, continuously place
all selected (i.e., where mask[i] = true) input lanes in an output vector. All
remaining lanes in the output are taken from passthru, which may be undef.

Vector Reduction Operations
---------------------------

These operations represent horizontal vector reduction, producing a scalar result.

G_VECREDUCE_SEQ_FADD, G_VECREDUCE_SEQ_FMUL
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The SEQ variants perform reductions in sequential order. The first operand is
an initial scalar accumulator value, and the second operand is the vector to reduce.

G_VECREDUCE_FADD, G_VECREDUCE_FMUL
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These reductions are relaxed variants which may reduce the elements in any order.

G_VECREDUCE_FMAX, G_VECREDUCE_FMIN, G_VECREDUCE_FMAXIMUM, G_VECREDUCE_FMINIMUM
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

FMIN/FMAX/FMINIMUM/FMAXIMUM nodes can have flags, for NaN/NoNaN variants.


Integer/bitwise reductions
^^^^^^^^^^^^^^^^^^^^^^^^^^

* G_VECREDUCE_ADD
* G_VECREDUCE_MUL
* G_VECREDUCE_AND
* G_VECREDUCE_OR
* G_VECREDUCE_XOR
* G_VECREDUCE_SMAX
* G_VECREDUCE_SMIN
* G_VECREDUCE_UMAX
* G_VECREDUCE_UMIN

Integer reductions may have a result type larger than the vector element type.
However, the reduction is performed using the vector element type and the value
in the top bits is unspecified.

Memory Operations
-----------------

G_LOAD, G_SEXTLOAD, G_ZEXTLOAD
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Generic load. Expects a MachineMemOperand in addition to explicit
operands. If the result size is larger than the memory size, the
high bits are undefined, sign-extended, or zero-extended respectively.

Only G_LOAD is valid if the result is a vector type. If the result is larger
than the memory size, the high elements are undefined (i.e. this is not a
per-element, vector anyextload)

Unlike in SelectionDAG, atomic loads are expressed with the same
opcodes as regular loads. G_LOAD, G_SEXTLOAD and G_ZEXTLOAD may all
have atomic memory operands.

G_INDEXED_LOAD
^^^^^^^^^^^^^^

Generic indexed load. Combines a GEP with a load. $newaddr is set to $base + $offset.
If $am is 0 (post-indexed), then the value is loaded from $base; if $am is 1 (pre-indexed)
then the value is loaded from $newaddr.

G_INDEXED_SEXTLOAD
^^^^^^^^^^^^^^^^^^

Same as G_INDEXED_LOAD except that the load performed is sign-extending, as with G_SEXTLOAD.

G_INDEXED_ZEXTLOAD
^^^^^^^^^^^^^^^^^^

Same as G_INDEXED_LOAD except that the load performed is zero-extending, as with G_ZEXTLOAD.

G_STORE
^^^^^^^

Generic store. Expects a MachineMemOperand in addition to explicit
operands. If the stored value size is greater than the memory size,
the high bits are implicitly truncated. If this is a vector store, the
high elements are discarded (i.e. this does not function as a per-lane
vector, truncating store)

G_INDEXED_STORE
^^^^^^^^^^^^^^^

Combines a store with a GEP. See description of G_INDEXED_LOAD for indexing behaviour.

G_ATOMIC_CMPXCHG_WITH_SUCCESS
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Generic atomic cmpxchg with internal success check. Expects a
MachineMemOperand in addition to explicit operands.

G_ATOMIC_CMPXCHG
^^^^^^^^^^^^^^^^

Generic atomic cmpxchg. Expects a MachineMemOperand in addition to explicit
operands.

|all_g_atomicrmw|
^^^^^^^^^^^^^^^^^

.. |all_g_atomicrmw| replace:: G_ATOMICRMW_XCHG, G_ATOMICRMW_ADD,
                               G_ATOMICRMW_SUB, G_ATOMICRMW_AND,
                               G_ATOMICRMW_NAND, G_ATOMICRMW_OR,
                               G_ATOMICRMW_XOR, G_ATOMICRMW_MAX,
                               G_ATOMICRMW_MIN, G_ATOMICRMW_UMAX,
                               G_ATOMICRMW_UMIN, G_ATOMICRMW_FADD,
                               G_ATOMICRMW_FSUB, G_ATOMICRMW_FMAX,
                               G_ATOMICRMW_FMIN

Generic atomicrmw. Expects a MachineMemOperand in addition to explicit
operands.

G_FENCE
^^^^^^^

Generic fence. The first operand is the memory ordering. The second operand is
the syncscope.

See the LLVM LangRef entry on the '``fence'`` instruction for more details.

G_MEMCPY
^^^^^^^^

Generic memcpy. Expects two MachineMemOperands covering the store and load
respectively, in addition to explicit operands.

G_MEMCPY_INLINE
^^^^^^^^^^^^^^^

Generic inlined memcpy. Like G_MEMCPY, but it is guaranteed that this version
will not be lowered as a call to an external function. Currently the size
operand is required to evaluate as a constant (not an immediate), though that is
expected to change when llvm.memcpy.inline is taught to support dynamic sizes.

G_MEMMOVE
^^^^^^^^^

Generic memmove. Similar to G_MEMCPY, but the source and destination memory
ranges are allowed to overlap.

G_MEMSET
^^^^^^^^

Generic memset. Expects a MachineMemOperand in addition to explicit operands.

G_BZERO
^^^^^^^

Generic bzero. Expects a MachineMemOperand in addition to explicit operands.

Control Flow
------------

G_PHI
^^^^^

Implement the φ node in the SSA graph representing the function.

.. code-block:: none

  %dst(s8) = G_PHI %src1(s8), %bb.<id1>, %src2(s8), %bb.<id2>

G_BR
^^^^

Unconditional branch

.. code-block:: none

  G_BR %bb.<id>

G_BRCOND
^^^^^^^^

Conditional branch

.. code-block:: none

  G_BRCOND %condition, %basicblock.<id>

G_BRINDIRECT
^^^^^^^^^^^^

Indirect branch

.. code-block:: none

  G_BRINDIRECT %src(p0)

G_BRJT
^^^^^^

Indirect branch to jump table entry

.. code-block:: none

  G_BRJT %ptr(p0), %jti, %idx(s64)

G_JUMP_TABLE
^^^^^^^^^^^^

Generates a pointer to the address of the jump table specified by the source
operand. The source operand is a jump table index.
G_JUMP_TABLE can be used in conjunction with G_BRJT to support jump table
codegen with GlobalISel.

.. code-block:: none

  %dst:_(p0) = G_JUMP_TABLE %jump-table.0

The above example generates a pointer to the source jump table index.

G_INVOKE_REGION_START
^^^^^^^^^^^^^^^^^^^^^

A marker instruction that acts as a pseudo-terminator for regions of code that may
throw exceptions. Being a terminator, it prevents code from being inserted after
it during passes like legalization. This is needed because calls to exception
throw routines do not return, so no code that must be on an executable path must
be placed after throwing.

G_INTRINSIC, G_INTRINSIC_CONVERGENT
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Call an intrinsic that has no side-effects.

The _CONVERGENT variant corresponds to an LLVM IR intrinsic marked `convergent`.

.. note::

  Unlike SelectionDAG, there is no _VOID variant. Both of these are permitted
  to have zero, one, or multiple results.

G_INTRINSIC_W_SIDE_EFFECTS, G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Call an intrinsic that is considered to have unknown side-effects and as such
cannot be reordered across other side-effecting instructions.

The _CONVERGENT variant corresponds to an LLVM IR intrinsic marked `convergent`.

.. note::

  Unlike SelectionDAG, there is no _VOID variant. Both of these are permitted
  to have zero, one, or multiple results.

G_TRAP, G_DEBUGTRAP, G_UBSANTRAP
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Represents :ref:`llvm.trap <llvm.trap>`, :ref:`llvm.debugtrap <llvm.debugtrap>`
and :ref:`llvm.ubsantrap <llvm.ubsantrap>` that generate a target dependent
trap instructions.

.. code-block:: none

  G_TRAP

.. code-block:: none

  G_DEBUGTRAP

.. code-block:: none

  G_UBSANTRAP 12

Variadic Arguments
------------------

G_VASTART
^^^^^^^^^

.. caution::

  I found no documentation for this instruction at the time of writing.

G_VAARG
^^^^^^^

.. caution::

  I found no documentation for this instruction at the time of writing.

Other Operations
----------------

G_DYN_STACKALLOC
^^^^^^^^^^^^^^^^

Dynamically realigns the stack pointer to the specified size and alignment.
An alignment value of `0` or `1` means no specific alignment.

.. code-block:: none

  %8:_(p0) = G_DYN_STACKALLOC %7(s64), 32

Optimization Hints
------------------

These instructions do not correspond to any target instructions. They act as
hints for various combines.

G_ASSERT_SEXT, G_ASSERT_ZEXT
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This signifies that the contents of a register were previously extended from a
smaller type.

The smaller type is denoted using an immediate operand. For scalars, this is the
width of the entire smaller type. For vectors, this is the width of the smaller
element type.

.. code-block:: none

  %x_was_zexted:_(s32) = G_ASSERT_ZEXT %x(s32), 16
  %y_was_zexted:_(<2 x s32>) = G_ASSERT_ZEXT %y(<2 x s32>), 16

  %z_was_sexted:_(s32) = G_ASSERT_SEXT %z(s32), 8

G_ASSERT_SEXT and G_ASSERT_ZEXT act like copies, albeit with some restrictions.

The source and destination registers must

- Be virtual
- Belong to the same register class
- Belong to the same register bank

It should always be safe to

- Look through the source register
- Replace the destination register with the source register


Miscellaneous
-------------

G_CONSTANT_FOLD_BARRIER
^^^^^^^^^^^^^^^^^^^^^^^

This operation is used as an opaque barrier to prevent constant folding. Combines
and other transformations should not look through this. These have no other
semantics and can be safely eliminated if a target chooses.
