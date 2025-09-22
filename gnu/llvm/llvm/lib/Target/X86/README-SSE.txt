//===---------------------------------------------------------------------===//
// Random ideas for the X86 backend: SSE-specific stuff.
//===---------------------------------------------------------------------===//

//===---------------------------------------------------------------------===//

SSE Variable shift can be custom lowered to something like this, which uses a
small table + unaligned load + shuffle instead of going through memory.

__m128i_shift_right:
	.byte	  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
	.byte	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1

...
__m128i shift_right(__m128i value, unsigned long offset) {
  return _mm_shuffle_epi8(value,
               _mm_loadu_si128((__m128 *) (___m128i_shift_right + offset)));
}

//===---------------------------------------------------------------------===//

SSE has instructions for doing operations on complex numbers, we should pattern
match them.   For example, this should turn into a horizontal add:

typedef float __attribute__((vector_size(16))) v4f32;
float f32(v4f32 A) {
  return A[0]+A[1]+A[2]+A[3];
}

Instead we get this:

_f32:                                   ## @f32
	pshufd	$1, %xmm0, %xmm1        ## xmm1 = xmm0[1,0,0,0]
	addss	%xmm0, %xmm1
	pshufd	$3, %xmm0, %xmm2        ## xmm2 = xmm0[3,0,0,0]
	movhlps	%xmm0, %xmm0            ## xmm0 = xmm0[1,1]
	movaps	%xmm0, %xmm3
	addss	%xmm1, %xmm3
	movdqa	%xmm2, %xmm0
	addss	%xmm3, %xmm0
	ret

Also, there are cases where some simple local SLP would improve codegen a bit.
compiling this:

_Complex float f32(_Complex float A, _Complex float B) {
  return A+B;
}

into:

_f32:                                   ## @f32
	movdqa	%xmm0, %xmm2
	addss	%xmm1, %xmm2
	pshufd	$1, %xmm1, %xmm1        ## xmm1 = xmm1[1,0,0,0]
	pshufd	$1, %xmm0, %xmm3        ## xmm3 = xmm0[1,0,0,0]
	addss	%xmm1, %xmm3
	movaps	%xmm2, %xmm0
	unpcklps	%xmm3, %xmm0    ## xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1]
	ret

seems silly when it could just be one addps.


//===---------------------------------------------------------------------===//

Expand libm rounding functions inline:  Significant speedups possible.
http://gcc.gnu.org/ml/gcc-patches/2006-10/msg00909.html

//===---------------------------------------------------------------------===//

When compiled with unsafemath enabled, "main" should enable SSE DAZ mode and
other fast SSE modes.

//===---------------------------------------------------------------------===//

Think about doing i64 math in SSE regs on x86-32.

//===---------------------------------------------------------------------===//

This testcase should have no SSE instructions in it, and only one load from
a constant pool:

double %test3(bool %B) {
        %C = select bool %B, double 123.412, double 523.01123123
        ret double %C
}

Currently, the select is being lowered, which prevents the dag combiner from
turning 'select (load CPI1), (load CPI2)' -> 'load (select CPI1, CPI2)'

The pattern isel got this one right.

//===---------------------------------------------------------------------===//

Lower memcpy / memset to a series of SSE 128 bit move instructions when it's
feasible.

//===---------------------------------------------------------------------===//

Codegen:
  if (copysign(1.0, x) == copysign(1.0, y))
into:
  if (x^y & mask)
when using SSE.

//===---------------------------------------------------------------------===//

Use movhps to update upper 64-bits of a v4sf value. Also movlps on lower half
of a v4sf value.

//===---------------------------------------------------------------------===//

Better codegen for vector_shuffles like this { x, 0, 0, 0 } or { x, 0, x, 0}.
Perhaps use pxor / xorp* to clear a XMM register first?

//===---------------------------------------------------------------------===//

External test Nurbs exposed some problems. Look for
__ZN15Nurbs_SSE_Cubic17TessellateSurfaceE, bb cond_next140. This is what icc
emits:

        movaps    (%edx), %xmm2                                 #59.21
        movaps    (%edx), %xmm5                                 #60.21
        movaps    (%edx), %xmm4                                 #61.21
        movaps    (%edx), %xmm3                                 #62.21
        movl      40(%ecx), %ebp                                #69.49
        shufps    $0, %xmm2, %xmm5                              #60.21
        movl      100(%esp), %ebx                               #69.20
        movl      (%ebx), %edi                                  #69.20
        imull     %ebp, %edi                                    #69.49
        addl      (%eax), %edi                                  #70.33
        shufps    $85, %xmm2, %xmm4                             #61.21
        shufps    $170, %xmm2, %xmm3                            #62.21
        shufps    $255, %xmm2, %xmm2                            #63.21
        lea       (%ebp,%ebp,2), %ebx                           #69.49
        negl      %ebx                                          #69.49
        lea       -3(%edi,%ebx), %ebx                           #70.33
        shll      $4, %ebx                                      #68.37
        addl      32(%ecx), %ebx                                #68.37
        testb     $15, %bl                                      #91.13
        jne       L_B1.24       # Prob 5%                       #91.13

This is the llvm code after instruction scheduling:

cond_next140 (0xa910740, LLVM BB @0xa90beb0):
	%reg1078 = MOV32ri -3
	%reg1079 = ADD32rm %reg1078, %reg1068, 1, %noreg, 0
	%reg1037 = MOV32rm %reg1024, 1, %noreg, 40
	%reg1080 = IMUL32rr %reg1079, %reg1037
	%reg1081 = MOV32rm %reg1058, 1, %noreg, 0
	%reg1038 = LEA32r %reg1081, 1, %reg1080, -3
	%reg1036 = MOV32rm %reg1024, 1, %noreg, 32
	%reg1082 = SHL32ri %reg1038, 4
	%reg1039 = ADD32rr %reg1036, %reg1082
	%reg1083 = MOVAPSrm %reg1059, 1, %noreg, 0
	%reg1034 = SHUFPSrr %reg1083, %reg1083, 170
	%reg1032 = SHUFPSrr %reg1083, %reg1083, 0
	%reg1035 = SHUFPSrr %reg1083, %reg1083, 255
	%reg1033 = SHUFPSrr %reg1083, %reg1083, 85
	%reg1040 = MOV32rr %reg1039
	%reg1084 = AND32ri8 %reg1039, 15
	CMP32ri8 %reg1084, 0
	JE mbb<cond_next204,0xa914d30>

Still ok. After register allocation:

cond_next140 (0xa910740, LLVM BB @0xa90beb0):
	%eax = MOV32ri -3
	%edx = MOV32rm %stack.3, 1, %noreg, 0
	ADD32rm %eax<def&use>, %edx, 1, %noreg, 0
	%edx = MOV32rm %stack.7, 1, %noreg, 0
	%edx = MOV32rm %edx, 1, %noreg, 40
	IMUL32rr %eax<def&use>, %edx
	%esi = MOV32rm %stack.5, 1, %noreg, 0
	%esi = MOV32rm %esi, 1, %noreg, 0
	MOV32mr %stack.4, 1, %noreg, 0, %esi
	%eax = LEA32r %esi, 1, %eax, -3
	%esi = MOV32rm %stack.7, 1, %noreg, 0
	%esi = MOV32rm %esi, 1, %noreg, 32
	%edi = MOV32rr %eax
	SHL32ri %edi<def&use>, 4
	ADD32rr %edi<def&use>, %esi
	%xmm0 = MOVAPSrm %ecx, 1, %noreg, 0
	%xmm1 = MOVAPSrr %xmm0
	SHUFPSrr %xmm1<def&use>, %xmm1, 170
	%xmm2 = MOVAPSrr %xmm0
	SHUFPSrr %xmm2<def&use>, %xmm2, 0
	%xmm3 = MOVAPSrr %xmm0
	SHUFPSrr %xmm3<def&use>, %xmm3, 255
	SHUFPSrr %xmm0<def&use>, %xmm0, 85
	%ebx = MOV32rr %edi
	AND32ri8 %ebx<def&use>, 15
	CMP32ri8 %ebx, 0
	JE mbb<cond_next204,0xa914d30>

This looks really bad. The problem is shufps is a destructive opcode. Since it
appears as operand two in more than one shufps ops. It resulted in a number of
copies. Note icc also suffers from the same problem. Either the instruction
selector should select pshufd or The register allocator can made the two-address
to three-address transformation.

It also exposes some other problems. See MOV32ri -3 and the spills.

//===---------------------------------------------------------------------===//

Consider:

__m128 test(float a) {
  return _mm_set_ps(0.0, 0.0, 0.0, a*a);
}

This compiles into:

movss 4(%esp), %xmm1
mulss %xmm1, %xmm1
xorps %xmm0, %xmm0
movss %xmm1, %xmm0
ret

Because mulss doesn't modify the top 3 elements, the top elements of 
xmm1 are already zero'd.  We could compile this to:

movss 4(%esp), %xmm0
mulss %xmm0, %xmm0
ret

//===---------------------------------------------------------------------===//

Here's a sick and twisted idea.  Consider code like this:

__m128 test(__m128 a) {
  float b = *(float*)&A;
  ...
  return _mm_set_ps(0.0, 0.0, 0.0, b);
}

This might compile to this code:

movaps c(%esp), %xmm1
xorps %xmm0, %xmm0
movss %xmm1, %xmm0
ret

Now consider if the ... code caused xmm1 to get spilled.  This might produce
this code:

movaps c(%esp), %xmm1
movaps %xmm1, c2(%esp)
...

xorps %xmm0, %xmm0
movaps c2(%esp), %xmm1
movss %xmm1, %xmm0
ret

However, since the reload is only used by these instructions, we could 
"fold" it into the uses, producing something like this:

movaps c(%esp), %xmm1
movaps %xmm1, c2(%esp)
...

movss c2(%esp), %xmm0
ret

... saving two instructions.

The basic idea is that a reload from a spill slot, can, if only one 4-byte 
chunk is used, bring in 3 zeros the one element instead of 4 elements.
This can be used to simplify a variety of shuffle operations, where the
elements are fixed zeros.

//===---------------------------------------------------------------------===//

This code generates ugly code, probably due to costs being off or something:

define void @test(float* %P, <4 x float>* %P2 ) {
        %xFloat0.688 = load float* %P
        %tmp = load <4 x float>* %P2
        %inFloat3.713 = insertelement <4 x float> %tmp, float 0.0, i32 3
        store <4 x float> %inFloat3.713, <4 x float>* %P2
        ret void
}

Generates:

_test:
	movl	8(%esp), %eax
	movaps	(%eax), %xmm0
	pxor	%xmm1, %xmm1
	movaps	%xmm0, %xmm2
	shufps	$50, %xmm1, %xmm2
	shufps	$132, %xmm2, %xmm0
	movaps	%xmm0, (%eax)
	ret

Would it be better to generate:

_test:
        movl 8(%esp), %ecx
        movaps (%ecx), %xmm0
	xor %eax, %eax
        pinsrw $6, %eax, %xmm0
        pinsrw $7, %eax, %xmm0
        movaps %xmm0, (%ecx)
        ret

?

//===---------------------------------------------------------------------===//

Some useful information in the Apple Altivec / SSE Migration Guide:

http://developer.apple.com/documentation/Performance/Conceptual/
Accelerate_sse_migration/index.html

e.g. SSE select using and, andnot, or. Various SSE compare translations.

//===---------------------------------------------------------------------===//

Add hooks to commute some CMPP operations.

//===---------------------------------------------------------------------===//

Apply the same transformation that merged four float into a single 128-bit load
to loads from constant pool.

//===---------------------------------------------------------------------===//

Floating point max / min are commutable when -enable-unsafe-fp-path is
specified. We should turn int_x86_sse_max_ss and X86ISD::FMIN etc. into other
nodes which are selected to max / min instructions that are marked commutable.

//===---------------------------------------------------------------------===//

We should materialize vector constants like "all ones" and "signbit" with 
code like:

     cmpeqps xmm1, xmm1   ; xmm1 = all-ones

and:
     cmpeqps xmm1, xmm1   ; xmm1 = all-ones
     psrlq   xmm1, 31     ; xmm1 = all 100000000000...

instead of using a load from the constant pool.  The later is important for
ABS/NEG/copysign etc.

//===---------------------------------------------------------------------===//

These functions:

#include <xmmintrin.h>
__m128i a;
void x(unsigned short n) {
  a = _mm_slli_epi32 (a, n);
}
void y(unsigned n) {
  a = _mm_slli_epi32 (a, n);
}

compile to ( -O3 -static -fomit-frame-pointer):
_x:
        movzwl  4(%esp), %eax
        movd    %eax, %xmm0
        movaps  _a, %xmm1
        pslld   %xmm0, %xmm1
        movaps  %xmm1, _a
        ret
_y:
        movd    4(%esp), %xmm0
        movaps  _a, %xmm1
        pslld   %xmm0, %xmm1
        movaps  %xmm1, _a
        ret

"y" looks good, but "x" does silly movzwl stuff around into a GPR.  It seems
like movd would be sufficient in both cases as the value is already zero 
extended in the 32-bit stack slot IIRC.  For signed short, it should also be
save, as a really-signed value would be undefined for pslld.


//===---------------------------------------------------------------------===//

#include <math.h>
int t1(double d) { return signbit(d); }

This currently compiles to:
	subl	$12, %esp
	movsd	16(%esp), %xmm0
	movsd	%xmm0, (%esp)
	movl	4(%esp), %eax
	shrl	$31, %eax
	addl	$12, %esp
	ret

We should use movmskp{s|d} instead.

//===---------------------------------------------------------------------===//

CodeGen/X86/vec_align.ll tests whether we can turn 4 scalar loads into a single
(aligned) vector load.  This functionality has a couple of problems.

1. The code to infer alignment from loads of globals is in the X86 backend,
   not the dag combiner.  This is because dagcombine2 needs to be able to see
   through the X86ISD::Wrapper node, which DAGCombine can't really do.
2. The code for turning 4 x load into a single vector load is target 
   independent and should be moved to the dag combiner.
3. The code for turning 4 x load into a vector load can only handle a direct 
   load from a global or a direct load from the stack.  It should be generalized
   to handle any load from P, P+4, P+8, P+12, where P can be anything.
4. The alignment inference code cannot handle loads from globals in non-static
   mode because it doesn't look through the extra dyld stub load.  If you try
   vec_align.ll without -relocation-model=static, you'll see what I mean.

//===---------------------------------------------------------------------===//

We should lower store(fneg(load p), q) into an integer load+xor+store, which
eliminates a constant pool load.  For example, consider:

define i64 @ccosf(float %z.0, float %z.1) nounwind readonly  {
entry:
 %tmp6 = fsub float -0.000000e+00, %z.1		; <float> [#uses=1]
 %tmp20 = tail call i64 @ccoshf( float %tmp6, float %z.0 ) nounwind readonly
 ret i64 %tmp20
}
declare i64 @ccoshf(float %z.0, float %z.1) nounwind readonly

This currently compiles to:

LCPI1_0:					#  <4 x float>
	.long	2147483648	# float -0
	.long	2147483648	# float -0
	.long	2147483648	# float -0
	.long	2147483648	# float -0
_ccosf:
	subl	$12, %esp
	movss	16(%esp), %xmm0
	movss	%xmm0, 4(%esp)
	movss	20(%esp), %xmm0
	xorps	LCPI1_0, %xmm0
	movss	%xmm0, (%esp)
	call	L_ccoshf$stub
	addl	$12, %esp
	ret

Note the load into xmm0, then xor (to negate), then store.  In PIC mode,
this code computes the pic base and does two loads to do the constant pool 
load, so the improvement is much bigger.

The tricky part about this xform is that the argument load/store isn't exposed
until post-legalize, and at that point, the fneg has been custom expanded into 
an X86 fxor.  This means that we need to handle this case in the x86 backend
instead of in target independent code.

//===---------------------------------------------------------------------===//

Non-SSE4 insert into 16 x i8 is atrociously bad.

//===---------------------------------------------------------------------===//

<2 x i64> extract is substantially worse than <2 x f64>, even if the destination
is memory.

//===---------------------------------------------------------------------===//

INSERTPS can match any insert (extract, imm1), imm2 for 4 x float, and insert
any number of 0.0 simultaneously.  Currently we only use it for simple
insertions.

See comments in LowerINSERT_VECTOR_ELT_SSE4.

//===---------------------------------------------------------------------===//

On a random note, SSE2 should declare insert/extract of 2 x f64 as legal, not
Custom.  All combinations of insert/extract reg-reg, reg-mem, and mem-reg are
legal, it'll just take a few extra patterns written in the .td file.

Note: this is not a code quality issue; the custom lowered code happens to be
right, but we shouldn't have to custom lower anything.  This is probably related
to <2 x i64> ops being so bad.

//===---------------------------------------------------------------------===//

LLVM currently generates stack realignment code, when it is not necessary
needed. The problem is that we need to know about stack alignment too early,
before RA runs.

At that point we don't know, whether there will be vector spill, or not.
Stack realignment logic is overly conservative here, but otherwise we can
produce unaligned loads/stores.

Fixing this will require some huge RA changes.

Testcase:
#include <emmintrin.h>

typedef short vSInt16 __attribute__ ((__vector_size__ (16)));

static const vSInt16 a = {- 22725, - 12873, - 22725, - 12873, - 22725, - 12873,
- 22725, - 12873};;

vSInt16 madd(vSInt16 b)
{
    return _mm_madd_epi16(a, b);
}

Generated code (x86-32, linux):
madd:
        pushl   %ebp
        movl    %esp, %ebp
        andl    $-16, %esp
        movaps  .LCPI1_0, %xmm1
        pmaddwd %xmm1, %xmm0
        movl    %ebp, %esp
        popl    %ebp
        ret

//===---------------------------------------------------------------------===//

Consider:
#include <emmintrin.h> 
__m128 foo2 (float x) {
 return _mm_set_ps (0, 0, x, 0);
}

In x86-32 mode, we generate this spiffy code:

_foo2:
	movss	4(%esp), %xmm0
	pshufd	$81, %xmm0, %xmm0
	ret

in x86-64 mode, we generate this code, which could be better:

_foo2:
	xorps	%xmm1, %xmm1
	movss	%xmm0, %xmm1
	pshufd	$81, %xmm1, %xmm0
	ret

In sse4 mode, we could use insertps to make both better.

Here's another testcase that could use insertps [mem]:

#include <xmmintrin.h>
extern float x2, x3;
__m128 foo1 (float x1, float x4) {
 return _mm_set_ps (x2, x1, x3, x4);
}

gcc mainline compiles it to:

foo1:
       insertps        $0x10, x2(%rip), %xmm0
       insertps        $0x10, x3(%rip), %xmm1
       movaps  %xmm1, %xmm2
       movlhps %xmm0, %xmm2
       movaps  %xmm2, %xmm0
       ret

//===---------------------------------------------------------------------===//

We compile vector multiply-by-constant into poor code:

define <4 x i32> @f(<4 x i32> %i) nounwind  {
	%A = mul <4 x i32> %i, < i32 10, i32 10, i32 10, i32 10 >
	ret <4 x i32> %A
}

On targets without SSE4.1, this compiles into:

LCPI1_0:					##  <4 x i32>
	.long	10
	.long	10
	.long	10
	.long	10
	.text
	.align	4,0x90
	.globl	_f
_f:
	pshufd	$3, %xmm0, %xmm1
	movd	%xmm1, %eax
	imull	LCPI1_0+12, %eax
	movd	%eax, %xmm1
	pshufd	$1, %xmm0, %xmm2
	movd	%xmm2, %eax
	imull	LCPI1_0+4, %eax
	movd	%eax, %xmm2
	punpckldq	%xmm1, %xmm2
	movd	%xmm0, %eax
	imull	LCPI1_0, %eax
	movd	%eax, %xmm1
	movhlps	%xmm0, %xmm0
	movd	%xmm0, %eax
	imull	LCPI1_0+8, %eax
	movd	%eax, %xmm0
	punpckldq	%xmm0, %xmm1
	movaps	%xmm1, %xmm0
	punpckldq	%xmm2, %xmm0
	ret

It would be better to synthesize integer vector multiplication by constants
using shifts and adds, pslld and paddd here. And even on targets with SSE4.1,
simple cases such as multiplication by powers of two would be better as
vector shifts than as multiplications.

//===---------------------------------------------------------------------===//

We compile this:

__m128i
foo2 (char x)
{
  return _mm_set_epi8 (1, 0, 0, 0, 0, 0, 0, 0, 0, x, 0, 1, 0, 0, 0, 0);
}

into:
	movl	$1, %eax
	xorps	%xmm0, %xmm0
	pinsrw	$2, %eax, %xmm0
	movzbl	4(%esp), %eax
	pinsrw	$3, %eax, %xmm0
	movl	$256, %eax
	pinsrw	$7, %eax, %xmm0
	ret


gcc-4.2:
	subl	$12, %esp
	movzbl	16(%esp), %eax
	movdqa	LC0, %xmm0
	pinsrw	$3, %eax, %xmm0
	addl	$12, %esp
	ret
	.const
	.align 4
LC0:
	.word	0
	.word	0
	.word	1
	.word	0
	.word	0
	.word	0
	.word	0
	.word	256

With SSE4, it should be
      movdqa  .LC0(%rip), %xmm0
      pinsrb  $6, %edi, %xmm0

//===---------------------------------------------------------------------===//

We should transform a shuffle of two vectors of constants into a single vector
of constants. Also, insertelement of a constant into a vector of constants
should also result in a vector of constants. e.g. 2008-06-25-VecISelBug.ll.

We compiled it to something horrible:

	.align	4
LCPI1_1:					##  float
	.long	1065353216	## float 1
	.const

	.align	4
LCPI1_0:					##  <4 x float>
	.space	4
	.long	1065353216	## float 1
	.space	4
	.long	1065353216	## float 1
	.text
	.align	4,0x90
	.globl	_t
_t:
	xorps	%xmm0, %xmm0
	movhps	LCPI1_0, %xmm0
	movss	LCPI1_1, %xmm1
	movaps	%xmm0, %xmm2
	shufps	$2, %xmm1, %xmm2
	shufps	$132, %xmm2, %xmm0
	movaps	%xmm0, 0

//===---------------------------------------------------------------------===//

Consider using movlps instead of movsd to implement (scalar_to_vector (loadf64))
when code size is critical. movlps is slower than movsd on core2 but it's one
byte shorter.

//===---------------------------------------------------------------------===//

We should use a dynamic programming based approach to tell when using FPStack
operations is cheaper than SSE.  SciMark montecarlo contains code like this
for example:

double MonteCarlo_num_flops(int Num_samples) {
    return ((double) Num_samples)* 4.0;
}

In fpstack mode, this compiles into:

LCPI1_0:					
	.long	1082130432	## float 4.000000e+00
_MonteCarlo_num_flops:
	subl	$4, %esp
	movl	8(%esp), %eax
	movl	%eax, (%esp)
	fildl	(%esp)
	fmuls	LCPI1_0
	addl	$4, %esp
	ret
        
in SSE mode, it compiles into significantly slower code:

_MonteCarlo_num_flops:
	subl	$12, %esp
	cvtsi2sd	16(%esp), %xmm0
	mulsd	LCPI1_0, %xmm0
	movsd	%xmm0, (%esp)
	fldl	(%esp)
	addl	$12, %esp
	ret

There are also other cases in scimark where using fpstack is better, it is
cheaper to do fld1 than load from a constant pool for example, so
"load, add 1.0, store" is better done in the fp stack, etc.

//===---------------------------------------------------------------------===//

These should compile into the same code (PR6214): Perhaps instcombine should
canonicalize the former into the later?

define float @foo(float %x) nounwind {
  %t = bitcast float %x to i32
  %s = and i32 %t, 2147483647
  %d = bitcast i32 %s to float
  ret float %d
}

declare float @fabsf(float %n)
define float @bar(float %x) nounwind {
  %d = call float @fabsf(float %x)
  ret float %d
}

//===---------------------------------------------------------------------===//

This IR (from PR6194):

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-apple-darwin10.0.0"

%0 = type { double, double }
%struct.float3 = type { float, float, float }

define void @test(%0, %struct.float3* nocapture %res) nounwind noinline ssp {
entry:
  %tmp18 = extractvalue %0 %0, 0                  ; <double> [#uses=1]
  %tmp19 = bitcast double %tmp18 to i64           ; <i64> [#uses=1]
  %tmp20 = zext i64 %tmp19 to i128                ; <i128> [#uses=1]
  %tmp10 = lshr i128 %tmp20, 32                   ; <i128> [#uses=1]
  %tmp11 = trunc i128 %tmp10 to i32               ; <i32> [#uses=1]
  %tmp12 = bitcast i32 %tmp11 to float            ; <float> [#uses=1]
  %tmp5 = getelementptr inbounds %struct.float3* %res, i64 0, i32 1 ; <float*> [#uses=1]
  store float %tmp12, float* %tmp5
  ret void
}

Compiles to:

_test:                                  ## @test
	movd	%xmm0, %rax
	shrq	$32, %rax
	movl	%eax, 4(%rdi)
	ret

This would be better kept in the SSE unit by treating XMM0 as a 4xfloat and
doing a shuffle from v[1] to v[0] then a float store.

//===---------------------------------------------------------------------===//

[UNSAFE FP]

void foo(double, double, double);
void norm(double x, double y, double z) {
  double scale = __builtin_sqrt(x*x + y*y + z*z);
  foo(x/scale, y/scale, z/scale);
}

We currently generate an sqrtsd and 3 divsd instructions. This is bad, fp div is
slow and not pipelined. In -ffast-math mode we could compute "1.0/scale" first
and emit 3 mulsd in place of the divs. This can be done as a target-independent
transform.

If we're dealing with floats instead of doubles we could even replace the sqrtss
and inversion with an rsqrtss instruction, which computes 1/sqrt faster at the
cost of reduced accuracy.

//===---------------------------------------------------------------------===//
