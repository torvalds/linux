//===- README_ALTIVEC.txt - Notes for improving Altivec code gen ----------===//

Implement PPCInstrInfo::isLoadFromStackSlot/isStoreToStackSlot for vector
registers, to generate better spill code.

//===----------------------------------------------------------------------===//

The first should be a single lvx from the constant pool, the second should be 
a xor/stvx:

void foo(void) {
  int x[8] __attribute__((aligned(128))) = { 1, 1, 1, 17, 1, 1, 1, 1 };
  bar (x);
}

#include <string.h>
void foo(void) {
  int x[8] __attribute__((aligned(128)));
  memset (x, 0, sizeof (x));
  bar (x);
}

//===----------------------------------------------------------------------===//

Altivec: Codegen'ing MUL with vector FMADD should add -0.0, not 0.0:
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=8763

When -ffast-math is on, we can use 0.0.

//===----------------------------------------------------------------------===//

  Consider this:
  v4f32 Vector;
  v4f32 Vector2 = { Vector.X, Vector.X, Vector.X, Vector.X };

Since we know that "Vector" is 16-byte aligned and we know the element offset 
of ".X", we should change the load into a lve*x instruction, instead of doing
a load/store/lve*x sequence.

//===----------------------------------------------------------------------===//

Implement passing vectors by value into calls and receiving them as arguments.

//===----------------------------------------------------------------------===//

GCC apparently tries to codegen { C1, C2, Variable, C3 } as a constant pool load
of C1/C2/C3, then a load and vperm of Variable.

//===----------------------------------------------------------------------===//

We need a way to teach tblgen that some operands of an intrinsic are required to
be constants.  The verifier should enforce this constraint.

//===----------------------------------------------------------------------===//

We currently codegen SCALAR_TO_VECTOR as a store of the scalar to a 16-byte
aligned stack slot, followed by a load/vperm.  We should probably just store it
to a scalar stack slot, then use lvsl/vperm to load it.  If the value is already
in memory this is a big win.

//===----------------------------------------------------------------------===//

extract_vector_elt of an arbitrary constant vector can be done with the 
following instructions:

vTemp = vec_splat(v0,2);    // 2 is the element the src is in.
vec_ste(&destloc,0,vTemp);

We can do an arbitrary non-constant value by using lvsr/perm/ste.

//===----------------------------------------------------------------------===//

If we want to tie instruction selection into the scheduler, we can do some
constant formation with different instructions.  For example, we can generate
"vsplti -1" with "vcmpequw R,R" and 1,1,1,1 with "vsubcuw R,R", and 0,0,0,0 with
"vsplti 0" or "vxor", each of which use different execution units, thus could
help scheduling.

This is probably only reasonable for a post-pass scheduler.

//===----------------------------------------------------------------------===//

For this function:

void test(vector float *A, vector float *B) {
  vector float C = (vector float)vec_cmpeq(*A, *B);
  if (!vec_any_eq(*A, *B))
    *B = (vector float){0,0,0,0};
  *A = C;
}

we get the following basic block:

	...
        lvx v2, 0, r4
        lvx v3, 0, r3
        vcmpeqfp v4, v3, v2
        vcmpeqfp. v2, v3, v2
        bne cr6, LBB1_2 ; cond_next

The vcmpeqfp/vcmpeqfp. instructions currently cannot be merged when the
vcmpeqfp. result is used by a branch.  This can be improved.

//===----------------------------------------------------------------------===//

The code generated for this is truly aweful:

vector float test(float a, float b) {
 return (vector float){ 0.0, a, 0.0, 0.0}; 
}

LCPI1_0:                                        ;  float
        .space  4
        .text
        .globl  _test
        .align  4
_test:
        mfspr r2, 256
        oris r3, r2, 4096
        mtspr 256, r3
        lis r3, ha16(LCPI1_0)
        addi r4, r1, -32
        stfs f1, -16(r1)
        addi r5, r1, -16
        lfs f0, lo16(LCPI1_0)(r3)
        stfs f0, -32(r1)
        lvx v2, 0, r4
        lvx v3, 0, r5
        vmrghw v3, v3, v2
        vspltw v2, v2, 0
        vmrghw v2, v2, v3
        mtspr 256, r2
        blr

//===----------------------------------------------------------------------===//

int foo(vector float *x, vector float *y) {
        if (vec_all_eq(*x,*y)) return 3245; 
        else return 12;
}

A predicate compare being used in a select_cc should have the same peephole
applied to it as a predicate compare used by a br_cc.  There should be no
mfcr here:

_foo:
        mfspr r2, 256
        oris r5, r2, 12288
        mtspr 256, r5
        li r5, 12
        li r6, 3245
        lvx v2, 0, r4
        lvx v3, 0, r3
        vcmpeqfp. v2, v3, v2
        mfcr r3, 2
        rlwinm r3, r3, 25, 31, 31
        cmpwi cr0, r3, 0
        bne cr0, LBB1_2 ; entry
LBB1_1: ; entry
        mr r6, r5
LBB1_2: ; entry
        mr r3, r6
        mtspr 256, r2
        blr

//===----------------------------------------------------------------------===//

CodeGen/PowerPC/vec_constants.ll has an and operation that should be
codegen'd to andc.  The issue is that the 'all ones' build vector is
SelectNodeTo'd a VSPLTISB instruction node before the and/xor is selected
which prevents the vnot pattern from matching.


//===----------------------------------------------------------------------===//

An alternative to the store/store/load approach for illegal insert element 
lowering would be:

1. store element to any ol' slot
2. lvx the slot
3. lvsl 0; splat index; vcmpeq to generate a select mask
4. lvsl slot + x; vperm to rotate result into correct slot
5. vsel result together.

//===----------------------------------------------------------------------===//

Should codegen branches on vec_any/vec_all to avoid mfcr.  Two examples:

#include <altivec.h>
 int f(vector float a, vector float b)
 {
  int aa = 0;
  if (vec_all_ge(a, b))
    aa |= 0x1;
  if (vec_any_ge(a,b))
    aa |= 0x2;
  return aa;
}

vector float f(vector float a, vector float b) { 
  if (vec_any_eq(a, b)) 
    return a; 
  else 
    return b; 
}

//===----------------------------------------------------------------------===//

We should do a little better with eliminating dead stores.
The stores to the stack are dead since %a and %b are not needed

; Function Attrs: nounwind
define <16 x i8> @test_vpmsumb() #0 {
  entry:
  %a = alloca <16 x i8>, align 16
  %b = alloca <16 x i8>, align 16
  store <16 x i8> <i8 1, i8 2, i8 3, i8 4, i8 5, i8 6, i8 7, i8 8, i8 9, i8 10, i8 11, i8 12, i8 13, i8 14, i8 15, i8 16>, <16 x i8>* %a, align 16
  store <16 x i8> <i8 113, i8 114, i8 115, i8 116, i8 117, i8 118, i8 119, i8 120, i8 121, i8 122, i8 123, i8 124, i8 125, i8 126, i8 127, i8 112>, <16 x i8>* %b, align 16
  %0 = load <16 x i8>* %a, align 16
  %1 = load <16 x i8>* %b, align 16
  %2 = call <16 x i8> @llvm.ppc.altivec.crypto.vpmsumb(<16 x i8> %0, <16 x i8> %1)
  ret <16 x i8> %2
}


; Function Attrs: nounwind readnone
declare <16 x i8> @llvm.ppc.altivec.crypto.vpmsumb(<16 x i8>, <16 x i8>) #1


Produces the following code with -mtriple=powerpc64-unknown-linux-gnu:
# %bb.0:                                # %entry
    addis 3, 2, .LCPI0_0@toc@ha
    addis 4, 2, .LCPI0_1@toc@ha
    addi 3, 3, .LCPI0_0@toc@l
    addi 4, 4, .LCPI0_1@toc@l
    lxvw4x 0, 0, 3
    addi 3, 1, -16
    lxvw4x 35, 0, 4
    stxvw4x 0, 0, 3
    ori 2, 2, 0
    lxvw4x 34, 0, 3
    addi 3, 1, -32
    stxvw4x 35, 0, 3
    vpmsumb 2, 2, 3
    blr
    .long   0
    .quad   0

The two stxvw4x instructions are not needed.
With -mtriple=powerpc64le-unknown-linux-gnu, the associated permutes
are present too.

//===----------------------------------------------------------------------===//

The following example is found in test/CodeGen/PowerPC/vec_add_sub_doubleword.ll:

define <2 x i64> @increment_by_val(<2 x i64> %x, i64 %val) nounwind {
       %tmpvec = insertelement <2 x i64> <i64 0, i64 0>, i64 %val, i32 0
       %tmpvec2 = insertelement <2 x i64> %tmpvec, i64 %val, i32 1
       %result = add <2 x i64> %x, %tmpvec2
       ret <2 x i64> %result

This will generate the following instruction sequence:
        std 5, -8(1)
        std 5, -16(1)
        addi 3, 1, -16
        ori 2, 2, 0
        lxvd2x 35, 0, 3
        vaddudm 2, 2, 3
        blr

This will almost certainly cause a load-hit-store hazard.  
Since val is a value parameter, it should not need to be saved onto
the stack, unless it's being done set up the vector register. Instead,
it would be better to splat the value into a vector register, and then
remove the (dead) stores to the stack.

//===----------------------------------------------------------------------===//

At the moment we always generate a lxsdx in preference to lfd, or stxsdx in
preference to stfd.  When we have a reg-immediate addressing mode, this is a
poor choice, since we have to load the address into an index register.  This
should be fixed for P7/P8. 

//===----------------------------------------------------------------------===//

Right now, ShuffleKind 0 is supported only on BE, and ShuffleKind 2 only on LE.
However, we could actually support both kinds on either endianness, if we check
for the appropriate shufflevector pattern for each case ...  this would cause
some additional shufflevectors to be recognized and implemented via the
"swapped" form.

//===----------------------------------------------------------------------===//

There is a utility program called PerfectShuffle that generates a table of the
shortest instruction sequence for implementing a shufflevector operation on
PowerPC.  However, this was designed for big-endian code generation.  We could
modify this program to create a little endian version of the table.  The table
is used in PPCISelLowering.cpp, PPCTargetLowering::LOWERVECTOR_SHUFFLE().

//===----------------------------------------------------------------------===//

Opportunies to use instructions from PPCInstrVSX.td during code gen
  - Conversion instructions (Sections 7.6.1.5 and 7.6.1.6 of ISA 2.07)
  - Scalar comparisons (xscmpodp and xscmpudp)
  - Min and max (xsmaxdp, xsmindp, xvmaxdp, xvmindp, xvmaxsp, xvminsp)

Related to this: we currently do not generate the lxvw4x instruction for either
v4f32 or v4i32, probably because adding a dag pattern to the recognizer requires
a single target type.  This should probably be addressed in the PPCISelDAGToDAG logic.

//===----------------------------------------------------------------------===//

Currently EXTRACT_VECTOR_ELT and INSERT_VECTOR_ELT are type-legal only
for v2f64 with VSX available.  We should create custom lowering
support for the other vector types.  Without this support, we generate
sequences with load-hit-store hazards.

v4f32 can be supported with VSX by shifting the correct element into
big-endian lane 0, using xscvspdpn to produce a double-precision
representation of the single-precision value in big-endian
double-precision lane 0, and reinterpreting lane 0 as an FPR or
vector-scalar register.

v2i64 can be supported with VSX and P8Vector in the same manner as
v2f64, followed by a direct move to a GPR.

v4i32 can be supported with VSX and P8Vector by shifting the correct
element into big-endian lane 1, using a direct move to a GPR, and
sign-extending the 32-bit result to 64 bits.

v8i16 can be supported with VSX and P8Vector by shifting the correct
element into big-endian lane 3, using a direct move to a GPR, and
sign-extending the 16-bit result to 64 bits.

v16i8 can be supported with VSX and P8Vector by shifting the correct
element into big-endian lane 7, using a direct move to a GPR, and
sign-extending the 8-bit result to 64 bits.
