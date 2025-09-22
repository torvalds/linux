//===---------------------------------------------------------------------===//
// Random ideas for the X86 backend: FP stack related stuff
//===---------------------------------------------------------------------===//

//===---------------------------------------------------------------------===//

Some targets (e.g. athlons) prefer freep to fstp ST(0):
http://gcc.gnu.org/ml/gcc-patches/2004-04/msg00659.html

//===---------------------------------------------------------------------===//

This should use fiadd on chips where it is profitable:
double foo(double P, int *I) { return P+*I; }

We have fiadd patterns now but the followings have the same cost and
complexity. We need a way to specify the later is more profitable.

def FpADD32m  : FpI<(ops RFP:$dst, RFP:$src1, f32mem:$src2), OneArgFPRW,
                    [(set RFP:$dst, (fadd RFP:$src1,
                                     (extloadf64f32 addr:$src2)))]>;
                // ST(0) = ST(0) + [mem32]

def FpIADD32m : FpI<(ops RFP:$dst, RFP:$src1, i32mem:$src2), OneArgFPRW,
                    [(set RFP:$dst, (fadd RFP:$src1,
                                     (X86fild addr:$src2, i32)))]>;
                // ST(0) = ST(0) + [mem32int]

//===---------------------------------------------------------------------===//

The FP stackifier should handle simple permutates to reduce number of shuffle
instructions, e.g. turning:

fld P	->		fld Q
fld Q			fld P
fxch

or:

fxch	->		fucomi
fucomi			jl X
jg X

Ideas:
http://gcc.gnu.org/ml/gcc-patches/2004-11/msg02410.html


//===---------------------------------------------------------------------===//

Add a target specific hook to DAG combiner to handle SINT_TO_FP and
FP_TO_SINT when the source operand is already in memory.

//===---------------------------------------------------------------------===//

Open code rint,floor,ceil,trunc:
http://gcc.gnu.org/ml/gcc-patches/2004-08/msg02006.html
http://gcc.gnu.org/ml/gcc-patches/2004-08/msg02011.html

Opencode the sincos[f] libcall.

//===---------------------------------------------------------------------===//

None of the FPStack instructions are handled in
X86RegisterInfo::foldMemoryOperand, which prevents the spiller from
folding spill code into the instructions.

//===---------------------------------------------------------------------===//

Currently the x86 codegen isn't very good at mixing SSE and FPStack
code:

unsigned int foo(double x) { return x; }

foo:
	subl $20, %esp
	movsd 24(%esp), %xmm0
	movsd %xmm0, 8(%esp)
	fldl 8(%esp)
	fisttpll (%esp)
	movl (%esp), %eax
	addl $20, %esp
	ret

This just requires being smarter when custom expanding fptoui.

//===---------------------------------------------------------------------===//
