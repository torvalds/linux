//===---------------------------------------------------------------------===//
// Random ideas for the X86 backend.
//===---------------------------------------------------------------------===//

Improvements to the multiply -> shift/add algorithm:
http://gcc.gnu.org/ml/gcc-patches/2004-08/msg01590.html

//===---------------------------------------------------------------------===//

Improve code like this (occurs fairly frequently, e.g. in LLVM):
long long foo(int x) { return 1LL << x; }

http://gcc.gnu.org/ml/gcc-patches/2004-09/msg01109.html
http://gcc.gnu.org/ml/gcc-patches/2004-09/msg01128.html
http://gcc.gnu.org/ml/gcc-patches/2004-09/msg01136.html

Another useful one would be  ~0ULL >> X and ~0ULL << X.

One better solution for 1LL << x is:
        xorl    %eax, %eax
        xorl    %edx, %edx
        testb   $32, %cl
        sete    %al
        setne   %dl
        sall    %cl, %eax
        sall    %cl, %edx

But that requires good 8-bit subreg support.

Also, this might be better.  It's an extra shift, but it's one instruction
shorter, and doesn't stress 8-bit subreg support.
(From http://gcc.gnu.org/ml/gcc-patches/2004-09/msg01148.html,
but without the unnecessary and.)
        movl %ecx, %eax
        shrl $5, %eax
        movl %eax, %edx
        xorl $1, %edx
        sall %cl, %eax
        sall %cl. %edx

64-bit shifts (in general) expand to really bad code.  Instead of using
cmovs, we should expand to a conditional branch like GCC produces.

//===---------------------------------------------------------------------===//

Some isel ideas:

1. Dynamic programming based approach when compile time is not an
   issue.
2. Code duplication (addressing mode) during isel.
3. Other ideas from "Register-Sensitive Selection, Duplication, and
   Sequencing of Instructions".
4. Scheduling for reduced register pressure.  E.g. "Minimum Register
   Instruction Sequence Problem: Revisiting Optimal Code Generation for DAGs"
   and other related papers.
   http://citeseer.ist.psu.edu/govindarajan01minimum.html

//===---------------------------------------------------------------------===//

Should we promote i16 to i32 to avoid partial register update stalls?

//===---------------------------------------------------------------------===//

Leave any_extend as pseudo instruction and hint to register
allocator. Delay codegen until post register allocation.
Note. any_extend is now turned into an INSERT_SUBREG. We still need to teach
the coalescer how to deal with it though.

//===---------------------------------------------------------------------===//

It appears icc use push for parameter passing. Need to investigate.

//===---------------------------------------------------------------------===//

The instruction selector sometimes misses folding a load into a compare.  The
pattern is written as (cmp reg, (load p)).  Because the compare isn't
commutative, it is not matched with the load on both sides.  The dag combiner
should be made smart enough to canonicalize the load into the RHS of a compare
when it can invert the result of the compare for free.

//===---------------------------------------------------------------------===//

In many cases, LLVM generates code like this:

_test:
        movl 8(%esp), %eax
        cmpl %eax, 4(%esp)
        setl %al
        movzbl %al, %eax
        ret

on some processors (which ones?), it is more efficient to do this:

_test:
        movl 8(%esp), %ebx
        xor  %eax, %eax
        cmpl %ebx, 4(%esp)
        setl %al
        ret

Doing this correctly is tricky though, as the xor clobbers the flags.

//===---------------------------------------------------------------------===//

We should generate bts/btr/etc instructions on targets where they are cheap or
when codesize is important.  e.g., for:

void setbit(int *target, int bit) {
    *target |= (1 << bit);
}
void clearbit(int *target, int bit) {
    *target &= ~(1 << bit);
}

//===---------------------------------------------------------------------===//

Instead of the following for memset char*, 1, 10:

	movl $16843009, 4(%edx)
	movl $16843009, (%edx)
	movw $257, 8(%edx)

It might be better to generate

	movl $16843009, %eax
	movl %eax, 4(%edx)
	movl %eax, (%edx)
	movw al, 8(%edx)
	
when we can spare a register. It reduces code size.

//===---------------------------------------------------------------------===//

Evaluate what the best way to codegen sdiv X, (2^C) is.  For X/8, we currently
get this:

define i32 @test1(i32 %X) {
    %Y = sdiv i32 %X, 8
    ret i32 %Y
}

_test1:
        movl 4(%esp), %eax
        movl %eax, %ecx
        sarl $31, %ecx
        shrl $29, %ecx
        addl %ecx, %eax
        sarl $3, %eax
        ret

GCC knows several different ways to codegen it, one of which is this:

_test1:
        movl    4(%esp), %eax
        cmpl    $-1, %eax
        leal    7(%eax), %ecx
        cmovle  %ecx, %eax
        sarl    $3, %eax
        ret

which is probably slower, but it's interesting at least :)

//===---------------------------------------------------------------------===//

We are currently lowering large (1MB+) memmove/memcpy to rep/stosl and rep/movsl
We should leave these as libcalls for everything over a much lower threshold,
since libc is hand tuned for medium and large mem ops (avoiding RFO for large
stores, TLB preheating, etc)

//===---------------------------------------------------------------------===//

Optimize this into something reasonable:
 x * copysign(1.0, y) * copysign(1.0, z)

//===---------------------------------------------------------------------===//

Optimize copysign(x, *y) to use an integer load from y.

//===---------------------------------------------------------------------===//

The following tests perform worse with LSR:

lambda, siod, optimizer-eval, ackermann, hash2, nestedloop, strcat, and Treesor.

//===---------------------------------------------------------------------===//

Adding to the list of cmp / test poor codegen issues:

int test(__m128 *A, __m128 *B) {
  if (_mm_comige_ss(*A, *B))
    return 3;
  else
    return 4;
}

_test:
	movl 8(%esp), %eax
	movaps (%eax), %xmm0
	movl 4(%esp), %eax
	movaps (%eax), %xmm1
	comiss %xmm0, %xmm1
	setae %al
	movzbl %al, %ecx
	movl $3, %eax
	movl $4, %edx
	cmpl $0, %ecx
	cmove %edx, %eax
	ret

Note the setae, movzbl, cmpl, cmove can be replaced with a single cmovae. There
are a number of issues. 1) We are introducing a setcc between the result of the
intrisic call and select. 2) The intrinsic is expected to produce a i32 value
so a any extend (which becomes a zero extend) is added.

We probably need some kind of target DAG combine hook to fix this.

//===---------------------------------------------------------------------===//

We generate significantly worse code for this than GCC:
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=21150
http://gcc.gnu.org/bugzilla/attachment.cgi?id=8701

There is also one case we do worse on PPC.

//===---------------------------------------------------------------------===//

For this:

int test(int a)
{
  return a * 3;
}

We currently emits
	imull $3, 4(%esp), %eax

Perhaps this is what we really should generate is? Is imull three or four
cycles? Note: ICC generates this:
	movl	4(%esp), %eax
	leal	(%eax,%eax,2), %eax

The current instruction priority is based on pattern complexity. The former is
more "complex" because it folds a load so the latter will not be emitted.

Perhaps we should use AddedComplexity to give LEA32r a higher priority? We
should always try to match LEA first since the LEA matching code does some
estimate to determine whether the match is profitable.

However, if we care more about code size, then imull is better. It's two bytes
shorter than movl + leal.

On a Pentium M, both variants have the same characteristics with regard
to throughput; however, the multiplication has a latency of four cycles, as
opposed to two cycles for the movl+lea variant.

//===---------------------------------------------------------------------===//

It appears gcc place string data with linkonce linkage in
.section __TEXT,__const_coal,coalesced instead of
.section __DATA,__const_coal,coalesced.
Take a look at darwin.h, there are other Darwin assembler directives that we
do not make use of.

//===---------------------------------------------------------------------===//

define i32 @foo(i32* %a, i32 %t) {
entry:
	br label %cond_true

cond_true:		; preds = %cond_true, %entry
	%x.0.0 = phi i32 [ 0, %entry ], [ %tmp9, %cond_true ]		; <i32> [#uses=3]
	%t_addr.0.0 = phi i32 [ %t, %entry ], [ %tmp7, %cond_true ]		; <i32> [#uses=1]
	%tmp2 = getelementptr i32* %a, i32 %x.0.0		; <i32*> [#uses=1]
	%tmp3 = load i32* %tmp2		; <i32> [#uses=1]
	%tmp5 = add i32 %t_addr.0.0, %x.0.0		; <i32> [#uses=1]
	%tmp7 = add i32 %tmp5, %tmp3		; <i32> [#uses=2]
	%tmp9 = add i32 %x.0.0, 1		; <i32> [#uses=2]
	%tmp = icmp sgt i32 %tmp9, 39		; <i1> [#uses=1]
	br i1 %tmp, label %bb12, label %cond_true

bb12:		; preds = %cond_true
	ret i32 %tmp7
}
is pessimized by -loop-reduce and -indvars

//===---------------------------------------------------------------------===//

u32 to float conversion improvement:

float uint32_2_float( unsigned u ) {
  float fl = (int) (u & 0xffff);
  float fh = (int) (u >> 16);
  fh *= 0x1.0p16f;
  return fh + fl;
}

00000000        subl    $0x04,%esp
00000003        movl    0x08(%esp,1),%eax
00000007        movl    %eax,%ecx
00000009        shrl    $0x10,%ecx
0000000c        cvtsi2ss        %ecx,%xmm0
00000010        andl    $0x0000ffff,%eax
00000015        cvtsi2ss        %eax,%xmm1
00000019        mulss   0x00000078,%xmm0
00000021        addss   %xmm1,%xmm0
00000025        movss   %xmm0,(%esp,1)
0000002a        flds    (%esp,1)
0000002d        addl    $0x04,%esp
00000030        ret

//===---------------------------------------------------------------------===//

When using fastcc abi, align stack slot of argument of type double on 8 byte
boundary to improve performance.

//===---------------------------------------------------------------------===//

GCC's ix86_expand_int_movcc function (in i386.c) has a ton of interesting
simplifications for integer "x cmp y ? a : b".

//===---------------------------------------------------------------------===//

Consider the expansion of:

define i32 @test3(i32 %X) {
        %tmp1 = urem i32 %X, 255
        ret i32 %tmp1
}

Currently it compiles to:

...
        movl $2155905153, %ecx
        movl 8(%esp), %esi
        movl %esi, %eax
        mull %ecx
...

This could be "reassociated" into:

        movl $2155905153, %eax
        movl 8(%esp), %ecx
        mull %ecx

to avoid the copy.  In fact, the existing two-address stuff would do this
except that mul isn't a commutative 2-addr instruction.  I guess this has
to be done at isel time based on the #uses to mul?

//===---------------------------------------------------------------------===//

Make sure the instruction which starts a loop does not cross a cacheline
boundary. This requires knowning the exact length of each machine instruction.
That is somewhat complicated, but doable. Example 256.bzip2:

In the new trace, the hot loop has an instruction which crosses a cacheline
boundary.  In addition to potential cache misses, this can't help decoding as I
imagine there has to be some kind of complicated decoder reset and realignment
to grab the bytes from the next cacheline.

532  532 0x3cfc movb     (1809(%esp, %esi), %bl   <<<--- spans 2 64 byte lines
942  942 0x3d03 movl     %dh, (1809(%esp, %esi)
937  937 0x3d0a incl     %esi
3    3   0x3d0b cmpb     %bl, %dl
27   27  0x3d0d jnz      0x000062db <main+11707>

//===---------------------------------------------------------------------===//

In c99 mode, the preprocessor doesn't like assembly comments like #TRUNCATE.

//===---------------------------------------------------------------------===//

This could be a single 16-bit load.

int f(char *p) {
    if ((p[0] == 1) & (p[1] == 2)) return 1;
    return 0;
}

//===---------------------------------------------------------------------===//

We should inline lrintf and probably other libc functions.

//===---------------------------------------------------------------------===//

This code:

void test(int X) {
  if (X) abort();
}

is currently compiled to:

_test:
        subl $12, %esp
        cmpl $0, 16(%esp)
        jne LBB1_1
        addl $12, %esp
        ret
LBB1_1:
        call L_abort$stub

It would be better to produce:

_test:
        subl $12, %esp
        cmpl $0, 16(%esp)
        jne L_abort$stub
        addl $12, %esp
        ret

This can be applied to any no-return function call that takes no arguments etc.
Alternatively, the stack save/restore logic could be shrink-wrapped, producing
something like this:

_test:
        cmpl $0, 4(%esp)
        jne LBB1_1
        ret
LBB1_1:
        subl $12, %esp
        call L_abort$stub

Both are useful in different situations.  Finally, it could be shrink-wrapped
and tail called, like this:

_test:
        cmpl $0, 4(%esp)
        jne LBB1_1
        ret
LBB1_1:
        pop %eax   # realign stack.
        call L_abort$stub

Though this probably isn't worth it.

//===---------------------------------------------------------------------===//

Sometimes it is better to codegen subtractions from a constant (e.g. 7-x) with
a neg instead of a sub instruction.  Consider:

int test(char X) { return 7-X; }

we currently produce:
_test:
        movl $7, %eax
        movsbl 4(%esp), %ecx
        subl %ecx, %eax
        ret

We would use one fewer register if codegen'd as:

        movsbl 4(%esp), %eax
	neg %eax
        add $7, %eax
        ret

Note that this isn't beneficial if the load can be folded into the sub.  In
this case, we want a sub:

int test(int X) { return 7-X; }
_test:
        movl $7, %eax
        subl 4(%esp), %eax
        ret

//===---------------------------------------------------------------------===//

Leaf functions that require one 4-byte spill slot have a prolog like this:

_foo:
        pushl   %esi
        subl    $4, %esp
...
and an epilog like this:
        addl    $4, %esp
        popl    %esi
        ret

It would be smaller, and potentially faster, to push eax on entry and to
pop into a dummy register instead of using addl/subl of esp.  Just don't pop 
into any return registers :)

//===---------------------------------------------------------------------===//

The X86 backend should fold (branch (or (setcc, setcc))) into multiple 
branches.  We generate really poor code for:

double testf(double a) {
       return a == 0.0 ? 0.0 : (a > 0.0 ? 1.0 : -1.0);
}

For example, the entry BB is:

_testf:
        subl    $20, %esp
        pxor    %xmm0, %xmm0
        movsd   24(%esp), %xmm1
        ucomisd %xmm0, %xmm1
        setnp   %al
        sete    %cl
        testb   %cl, %al
        jne     LBB1_5  # UnifiedReturnBlock
LBB1_1: # cond_true


it would be better to replace the last four instructions with:

	jp LBB1_1
	je LBB1_5
LBB1_1:

We also codegen the inner ?: into a diamond:

       cvtss2sd        LCPI1_0(%rip), %xmm2
        cvtss2sd        LCPI1_1(%rip), %xmm3
        ucomisd %xmm1, %xmm0
        ja      LBB1_3  # cond_true
LBB1_2: # cond_true
        movapd  %xmm3, %xmm2
LBB1_3: # cond_true
        movapd  %xmm2, %xmm0
        ret

We should sink the load into xmm3 into the LBB1_2 block.  This should
be pretty easy, and will nuke all the copies.

//===---------------------------------------------------------------------===//

This:
        #include <algorithm>
        inline std::pair<unsigned, bool> full_add(unsigned a, unsigned b)
        { return std::make_pair(a + b, a + b < a); }
        bool no_overflow(unsigned a, unsigned b)
        { return !full_add(a, b).second; }

Should compile to:
	addl	%esi, %edi
	setae	%al
	movzbl	%al, %eax
	ret

on x86-64, instead of the rather stupid-looking:
	addl	%esi, %edi
	setb	%al
	xorb	$1, %al
	movzbl	%al, %eax
	ret


//===---------------------------------------------------------------------===//

The following code:

bb114.preheader:		; preds = %cond_next94
	%tmp231232 = sext i16 %tmp62 to i32		; <i32> [#uses=1]
	%tmp233 = sub i32 32, %tmp231232		; <i32> [#uses=1]
	%tmp245246 = sext i16 %tmp65 to i32		; <i32> [#uses=1]
	%tmp252253 = sext i16 %tmp68 to i32		; <i32> [#uses=1]
	%tmp254 = sub i32 32, %tmp252253		; <i32> [#uses=1]
	%tmp553554 = bitcast i16* %tmp37 to i8*		; <i8*> [#uses=2]
	%tmp583584 = sext i16 %tmp98 to i32		; <i32> [#uses=1]
	%tmp585 = sub i32 32, %tmp583584		; <i32> [#uses=1]
	%tmp614615 = sext i16 %tmp101 to i32		; <i32> [#uses=1]
	%tmp621622 = sext i16 %tmp104 to i32		; <i32> [#uses=1]
	%tmp623 = sub i32 32, %tmp621622		; <i32> [#uses=1]
	br label %bb114

produces:

LBB3_5:	# bb114.preheader
	movswl	-68(%ebp), %eax
	movl	$32, %ecx
	movl	%ecx, -80(%ebp)
	subl	%eax, -80(%ebp)
	movswl	-52(%ebp), %eax
	movl	%ecx, -84(%ebp)
	subl	%eax, -84(%ebp)
	movswl	-70(%ebp), %eax
	movl	%ecx, -88(%ebp)
	subl	%eax, -88(%ebp)
	movswl	-50(%ebp), %eax
	subl	%eax, %ecx
	movl	%ecx, -76(%ebp)
	movswl	-42(%ebp), %eax
	movl	%eax, -92(%ebp)
	movswl	-66(%ebp), %eax
	movl	%eax, -96(%ebp)
	movw	$0, -98(%ebp)

This appears to be bad because the RA is not folding the store to the stack 
slot into the movl.  The above instructions could be:
	movl    $32, -80(%ebp)
...
	movl    $32, -84(%ebp)
...
This seems like a cross between remat and spill folding.

This has redundant subtractions of %eax from a stack slot. However, %ecx doesn't
change, so we could simply subtract %eax from %ecx first and then use %ecx (or
vice-versa).

//===---------------------------------------------------------------------===//

This code:

	%tmp659 = icmp slt i16 %tmp654, 0		; <i1> [#uses=1]
	br i1 %tmp659, label %cond_true662, label %cond_next715

produces this:

	testw	%cx, %cx
	movswl	%cx, %esi
	jns	LBB4_109	# cond_next715

Shark tells us that using %cx in the testw instruction is sub-optimal. It
suggests using the 32-bit register (which is what ICC uses).

//===---------------------------------------------------------------------===//

We compile this:

void compare (long long foo) {
  if (foo < 4294967297LL)
    abort();
}

to:

compare:
        subl    $4, %esp
        cmpl    $0, 8(%esp)
        setne   %al
        movzbw  %al, %ax
        cmpl    $1, 12(%esp)
        setg    %cl
        movzbw  %cl, %cx
        cmove   %ax, %cx
        testb   $1, %cl
        jne     .LBB1_2 # UnifiedReturnBlock
.LBB1_1:        # ifthen
        call    abort
.LBB1_2:        # UnifiedReturnBlock
        addl    $4, %esp
        ret

(also really horrible code on ppc).  This is due to the expand code for 64-bit
compares.  GCC produces multiple branches, which is much nicer:

compare:
        subl    $12, %esp
        movl    20(%esp), %edx
        movl    16(%esp), %eax
        decl    %edx
        jle     .L7
.L5:
        addl    $12, %esp
        ret
        .p2align 4,,7
.L7:
        jl      .L4
        cmpl    $0, %eax
        .p2align 4,,8
        ja      .L5
.L4:
        .p2align 4,,9
        call    abort

//===---------------------------------------------------------------------===//

Tail call optimization improvements: Tail call optimization currently
pushes all arguments on the top of the stack (their normal place for
non-tail call optimized calls) that source from the callers arguments
or  that source from a virtual register (also possibly sourcing from
callers arguments).
This is done to prevent overwriting of parameters (see example
below) that might be used later.

example:  

int callee(int32, int64); 
int caller(int32 arg1, int32 arg2) { 
  int64 local = arg2 * 2; 
  return callee(arg2, (int64)local); 
}

[arg1]          [!arg2 no longer valid since we moved local onto it]
[arg2]      ->  [(int64)
[RETADDR]        local  ]

Moving arg1 onto the stack slot of callee function would overwrite
arg2 of the caller.

Possible optimizations:


 - Analyse the actual parameters of the callee to see which would
   overwrite a caller parameter which is used by the callee and only
   push them onto the top of the stack.

   int callee (int32 arg1, int32 arg2);
   int caller (int32 arg1, int32 arg2) {
       return callee(arg1,arg2);
   }

   Here we don't need to write any variables to the top of the stack
   since they don't overwrite each other.

   int callee (int32 arg1, int32 arg2);
   int caller (int32 arg1, int32 arg2) {
       return callee(arg2,arg1);
   }

   Here we need to push the arguments because they overwrite each
   other.

//===---------------------------------------------------------------------===//

main ()
{
  int i = 0;
  unsigned long int z = 0;

  do {
    z -= 0x00004000;
    i++;
    if (i > 0x00040000)
      abort ();
  } while (z > 0);
  exit (0);
}

gcc compiles this to:

_main:
	subl	$28, %esp
	xorl	%eax, %eax
	jmp	L2
L3:
	cmpl	$262144, %eax
	je	L10
L2:
	addl	$1, %eax
	cmpl	$262145, %eax
	jne	L3
	call	L_abort$stub
L10:
	movl	$0, (%esp)
	call	L_exit$stub

llvm:

_main:
	subl	$12, %esp
	movl	$1, %eax
	movl	$16384, %ecx
LBB1_1:	# bb
	cmpl	$262145, %eax
	jge	LBB1_4	# cond_true
LBB1_2:	# cond_next
	incl	%eax
	addl	$4294950912, %ecx
	cmpl	$16384, %ecx
	jne	LBB1_1	# bb
LBB1_3:	# bb11
	xorl	%eax, %eax
	addl	$12, %esp
	ret
LBB1_4:	# cond_true
	call	L_abort$stub

1. LSR should rewrite the first cmp with induction variable %ecx.
2. DAG combiner should fold
        leal    1(%eax), %edx
        cmpl    $262145, %edx
   =>
        cmpl    $262144, %eax

//===---------------------------------------------------------------------===//

define i64 @test(double %X) {
	%Y = fptosi double %X to i64
	ret i64 %Y
}

compiles to:

_test:
	subl	$20, %esp
	movsd	24(%esp), %xmm0
	movsd	%xmm0, 8(%esp)
	fldl	8(%esp)
	fisttpll	(%esp)
	movl	4(%esp), %edx
	movl	(%esp), %eax
	addl	$20, %esp
	#FP_REG_KILL
	ret

This should just fldl directly from the input stack slot.

//===---------------------------------------------------------------------===//

This code:
int foo (int x) { return (x & 65535) | 255; }

Should compile into:

_foo:
        movzwl  4(%esp), %eax
        orl     $255, %eax
        ret

instead of:
_foo:
	movl	$65280, %eax
	andl	4(%esp), %eax
	orl	$255, %eax
	ret

//===---------------------------------------------------------------------===//

We're codegen'ing multiply of long longs inefficiently:

unsigned long long LLM(unsigned long long arg1, unsigned long long arg2) {
  return arg1 *  arg2;
}

We compile to (fomit-frame-pointer):

_LLM:
	pushl	%esi
	movl	8(%esp), %ecx
	movl	16(%esp), %esi
	movl	%esi, %eax
	mull	%ecx
	imull	12(%esp), %esi
	addl	%edx, %esi
	imull	20(%esp), %ecx
	movl	%esi, %edx
	addl	%ecx, %edx
	popl	%esi
	ret

This looks like a scheduling deficiency and lack of remat of the load from
the argument area.  ICC apparently produces:

        movl      8(%esp), %ecx
        imull     12(%esp), %ecx
        movl      16(%esp), %eax
        imull     4(%esp), %eax 
        addl      %eax, %ecx  
        movl      4(%esp), %eax
        mull      12(%esp) 
        addl      %ecx, %edx
        ret

Note that it remat'd loads from 4(esp) and 12(esp).  See this GCC PR:
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=17236

//===---------------------------------------------------------------------===//

We can fold a store into "zeroing a reg".  Instead of:

xorl    %eax, %eax
movl    %eax, 124(%esp)

we should get:

movl    $0, 124(%esp)

if the flags of the xor are dead.

Likewise, we isel "x<<1" into "add reg,reg".  If reg is spilled, this should
be folded into: shl [mem], 1

//===---------------------------------------------------------------------===//

In SSE mode, we turn abs and neg into a load from the constant pool plus a xor
or and instruction, for example:

	xorpd	LCPI1_0, %xmm2

However, if xmm2 gets spilled, we end up with really ugly code like this:

	movsd	(%esp), %xmm0
	xorpd	LCPI1_0, %xmm0
	movsd	%xmm0, (%esp)

Since we 'know' that this is a 'neg', we can actually "fold" the spill into
the neg/abs instruction, turning it into an *integer* operation, like this:

	xorl 2147483648, [mem+4]     ## 2147483648 = (1 << 31)

you could also use xorb, but xorl is less likely to lead to a partial register
stall.  Here is a contrived testcase:

double a, b, c;
void test(double *P) {
  double X = *P;
  a = X;
  bar();
  X = -X;
  b = X;
  bar();
  c = X;
}

//===---------------------------------------------------------------------===//

The generated code on x86 for checking for signed overflow on a multiply the
obvious way is much longer than it needs to be.

int x(int a, int b) {
  long long prod = (long long)a*b;
  return  prod > 0x7FFFFFFF || prod < (-0x7FFFFFFF-1);
}

See PR2053 for more details.

//===---------------------------------------------------------------------===//

We should investigate using cdq/ctld (effect: edx = sar eax, 31)
more aggressively; it should cost the same as a move+shift on any modern
processor, but it's a lot shorter. Downside is that it puts more
pressure on register allocation because it has fixed operands.

Example:
int abs(int x) {return x < 0 ? -x : x;}

gcc compiles this to the following when using march/mtune=pentium2/3/4/m/etc.:
abs:
        movl    4(%esp), %eax
        cltd
        xorl    %edx, %eax
        subl    %edx, %eax
        ret

//===---------------------------------------------------------------------===//

Take the following code (from 
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=16541):

extern unsigned char first_one[65536];
int FirstOnet(unsigned long long arg1)
{
  if (arg1 >> 48)
    return (first_one[arg1 >> 48]);
  return 0;
}


The following code is currently generated:
FirstOnet:
        movl    8(%esp), %eax
        cmpl    $65536, %eax
        movl    4(%esp), %ecx
        jb      .LBB1_2 # UnifiedReturnBlock
.LBB1_1:        # ifthen
        shrl    $16, %eax
        movzbl  first_one(%eax), %eax
        ret
.LBB1_2:        # UnifiedReturnBlock
        xorl    %eax, %eax
        ret

We could change the "movl 8(%esp), %eax" into "movzwl 10(%esp), %eax"; this
lets us change the cmpl into a testl, which is shorter, and eliminate the shift.

//===---------------------------------------------------------------------===//

We compile this function:

define i32 @foo(i32 %a, i32 %b, i32 %c, i8 zeroext  %d) nounwind  {
entry:
	%tmp2 = icmp eq i8 %d, 0		; <i1> [#uses=1]
	br i1 %tmp2, label %bb7, label %bb

bb:		; preds = %entry
	%tmp6 = add i32 %b, %a		; <i32> [#uses=1]
	ret i32 %tmp6

bb7:		; preds = %entry
	%tmp10 = sub i32 %a, %c		; <i32> [#uses=1]
	ret i32 %tmp10
}

to:

foo:                                    # @foo
# %bb.0:                                # %entry
	movl	4(%esp), %ecx
	cmpb	$0, 16(%esp)
	je	.LBB0_2
# %bb.1:                                # %bb
	movl	8(%esp), %eax
	addl	%ecx, %eax
	ret
.LBB0_2:                                # %bb7
	movl	12(%esp), %edx
	movl	%ecx, %eax
	subl	%edx, %eax
	ret

There's an obviously unnecessary movl in .LBB0_2, and we could eliminate a
couple more movls by putting 4(%esp) into %eax instead of %ecx.

//===---------------------------------------------------------------------===//

Take the following:

target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:64-f32:32:32-f64:32:64-v64:64:64-v128:128:128-a0:0:64-f80:128:128-S128"
target triple = "i386-apple-darwin8"
@in_exit.4870.b = internal global i1 false		; <i1*> [#uses=2]
define fastcc void @abort_gzip() noreturn nounwind  {
entry:
	%tmp.b.i = load i1* @in_exit.4870.b		; <i1> [#uses=1]
	br i1 %tmp.b.i, label %bb.i, label %bb4.i
bb.i:		; preds = %entry
	tail call void @exit( i32 1 ) noreturn nounwind 
	unreachable
bb4.i:		; preds = %entry
	store i1 true, i1* @in_exit.4870.b
	tail call void @exit( i32 1 ) noreturn nounwind 
	unreachable
}
declare void @exit(i32) noreturn nounwind 

This compiles into:
_abort_gzip:                            ## @abort_gzip
## %bb.0:                               ## %entry
	subl	$12, %esp
	movb	_in_exit.4870.b, %al
	cmpb	$1, %al
	jne	LBB0_2

We somehow miss folding the movb into the cmpb.

//===---------------------------------------------------------------------===//

We compile:

int test(int x, int y) {
  return x-y-1;
}

into (-m64):

_test:
	decl	%edi
	movl	%edi, %eax
	subl	%esi, %eax
	ret

it would be better to codegen as: x+~y  (notl+addl)

//===---------------------------------------------------------------------===//

This code:

int foo(const char *str,...)
{
 __builtin_va_list a; int x;
 __builtin_va_start(a,str); x = __builtin_va_arg(a,int); __builtin_va_end(a);
 return x;
}

gets compiled into this on x86-64:
	subq    $200, %rsp
        movaps  %xmm7, 160(%rsp)
        movaps  %xmm6, 144(%rsp)
        movaps  %xmm5, 128(%rsp)
        movaps  %xmm4, 112(%rsp)
        movaps  %xmm3, 96(%rsp)
        movaps  %xmm2, 80(%rsp)
        movaps  %xmm1, 64(%rsp)
        movaps  %xmm0, 48(%rsp)
        movq    %r9, 40(%rsp)
        movq    %r8, 32(%rsp)
        movq    %rcx, 24(%rsp)
        movq    %rdx, 16(%rsp)
        movq    %rsi, 8(%rsp)
        leaq    (%rsp), %rax
        movq    %rax, 192(%rsp)
        leaq    208(%rsp), %rax
        movq    %rax, 184(%rsp)
        movl    $48, 180(%rsp)
        movl    $8, 176(%rsp)
        movl    176(%rsp), %eax
        cmpl    $47, %eax
        jbe     .LBB1_3 # bb
.LBB1_1:        # bb3
        movq    184(%rsp), %rcx
        leaq    8(%rcx), %rax
        movq    %rax, 184(%rsp)
.LBB1_2:        # bb4
        movl    (%rcx), %eax
        addq    $200, %rsp
        ret
.LBB1_3:        # bb
        movl    %eax, %ecx
        addl    $8, %eax
        addq    192(%rsp), %rcx
        movl    %eax, 176(%rsp)
        jmp     .LBB1_2 # bb4

gcc 4.3 generates:
	subq    $96, %rsp
.LCFI0:
        leaq    104(%rsp), %rax
        movq    %rsi, -80(%rsp)
        movl    $8, -120(%rsp)
        movq    %rax, -112(%rsp)
        leaq    -88(%rsp), %rax
        movq    %rax, -104(%rsp)
        movl    $8, %eax
        cmpl    $48, %eax
        jb      .L6
        movq    -112(%rsp), %rdx
        movl    (%rdx), %eax
        addq    $96, %rsp
        ret
        .p2align 4,,10
        .p2align 3
.L6:
        mov     %eax, %edx
        addq    -104(%rsp), %rdx
        addl    $8, %eax
        movl    %eax, -120(%rsp)
        movl    (%rdx), %eax
        addq    $96, %rsp
        ret

and it gets compiled into this on x86:
	pushl   %ebp
        movl    %esp, %ebp
        subl    $4, %esp
        leal    12(%ebp), %eax
        movl    %eax, -4(%ebp)
        leal    16(%ebp), %eax
        movl    %eax, -4(%ebp)
        movl    12(%ebp), %eax
        addl    $4, %esp
        popl    %ebp
        ret

gcc 4.3 generates:
	pushl   %ebp
        movl    %esp, %ebp
        movl    12(%ebp), %eax
        popl    %ebp
        ret

//===---------------------------------------------------------------------===//

Teach tblgen not to check bitconvert source type in some cases. This allows us
to consolidate the following patterns in X86InstrMMX.td:

def : Pat<(v2i32 (bitconvert (i64 (vector_extract (v2i64 VR128:$src),
                                                  (iPTR 0))))),
          (v2i32 (MMX_MOVDQ2Qrr VR128:$src))>;
def : Pat<(v4i16 (bitconvert (i64 (vector_extract (v2i64 VR128:$src),
                                                  (iPTR 0))))),
          (v4i16 (MMX_MOVDQ2Qrr VR128:$src))>;
def : Pat<(v8i8 (bitconvert (i64 (vector_extract (v2i64 VR128:$src),
                                                  (iPTR 0))))),
          (v8i8 (MMX_MOVDQ2Qrr VR128:$src))>;

There are other cases in various td files.

//===---------------------------------------------------------------------===//

Take something like the following on x86-32:
unsigned a(unsigned long long x, unsigned y) {return x % y;}

We currently generate a libcall, but we really shouldn't: the expansion is
shorter and likely faster than the libcall.  The expected code is something
like the following:

	movl	12(%ebp), %eax
	movl	16(%ebp), %ecx
	xorl	%edx, %edx
	divl	%ecx
	movl	8(%ebp), %eax
	divl	%ecx
	movl	%edx, %eax
	ret

A similar code sequence works for division.

//===---------------------------------------------------------------------===//

We currently compile this:

define i32 @func1(i32 %v1, i32 %v2) nounwind {
entry:
  %t = call {i32, i1} @llvm.sadd.with.overflow.i32(i32 %v1, i32 %v2)
  %sum = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  br i1 %obit, label %overflow, label %normal
normal:
  ret i32 %sum
overflow:
  call void @llvm.trap()
  unreachable
}
declare {i32, i1} @llvm.sadd.with.overflow.i32(i32, i32)
declare void @llvm.trap()

to:

_func1:
	movl	4(%esp), %eax
	addl	8(%esp), %eax
	jo	LBB1_2	## overflow
LBB1_1:	## normal
	ret
LBB1_2:	## overflow
	ud2

it would be nice to produce "into" someday.

//===---------------------------------------------------------------------===//

Test instructions can be eliminated by using EFLAGS values from arithmetic
instructions. This is currently not done for mul, and, or, xor, neg, shl,
sra, srl, shld, shrd, atomic ops, and others. It is also currently not done
for read-modify-write instructions. It is also current not done if the
OF or CF flags are needed.

The shift operators have the complication that when the shift count is
zero, EFLAGS is not set, so they can only subsume a test instruction if
the shift count is known to be non-zero. Also, using the EFLAGS value
from a shift is apparently very slow on some x86 implementations.

In read-modify-write instructions, the root node in the isel match is
the store, and isel has no way for the use of the EFLAGS result of the
arithmetic to be remapped to the new node.

Add and subtract instructions set OF on signed overflow and CF on unsiged
overflow, while test instructions always clear OF and CF. In order to
replace a test with an add or subtract in a situation where OF or CF is
needed, codegen must be able to prove that the operation cannot see
signed or unsigned overflow, respectively.

//===---------------------------------------------------------------------===//

memcpy/memmove do not lower to SSE copies when possible.  A silly example is:
define <16 x float> @foo(<16 x float> %A) nounwind {
	%tmp = alloca <16 x float>, align 16
	%tmp2 = alloca <16 x float>, align 16
	store <16 x float> %A, <16 x float>* %tmp
	%s = bitcast <16 x float>* %tmp to i8*
	%s2 = bitcast <16 x float>* %tmp2 to i8*
	call void @llvm.memcpy.i64(i8* %s, i8* %s2, i64 64, i32 16)
	%R = load <16 x float>* %tmp2
	ret <16 x float> %R
}

declare void @llvm.memcpy.i64(i8* nocapture, i8* nocapture, i64, i32) nounwind

which compiles to:

_foo:
	subl	$140, %esp
	movaps	%xmm3, 112(%esp)
	movaps	%xmm2, 96(%esp)
	movaps	%xmm1, 80(%esp)
	movaps	%xmm0, 64(%esp)
	movl	60(%esp), %eax
	movl	%eax, 124(%esp)
	movl	56(%esp), %eax
	movl	%eax, 120(%esp)
	movl	52(%esp), %eax
        <many many more 32-bit copies>
      	movaps	(%esp), %xmm0
	movaps	16(%esp), %xmm1
	movaps	32(%esp), %xmm2
	movaps	48(%esp), %xmm3
	addl	$140, %esp
	ret

On Nehalem, it may even be cheaper to just use movups when unaligned than to
fall back to lower-granularity chunks.

//===---------------------------------------------------------------------===//

Implement processor-specific optimizations for parity with GCC on these
processors.  GCC does two optimizations:

1. ix86_pad_returns inserts a noop before ret instructions if immediately
   preceded by a conditional branch or is the target of a jump.
2. ix86_avoid_jump_misspredicts inserts noops in cases where a 16-byte block of
   code contains more than 3 branches.
   
The first one is done for all AMDs, Core2, and "Generic"
The second one is done for: Atom, Pentium Pro, all AMDs, Pentium 4, Nocona,
  Core 2, and "Generic"

//===---------------------------------------------------------------------===//
Testcase:
int x(int a) { return (a&0xf0)>>4; }

Current output:
	movl	4(%esp), %eax
	shrl	$4, %eax
	andl	$15, %eax
	ret

Ideal output:
	movzbl	4(%esp), %eax
	shrl	$4, %eax
	ret

//===---------------------------------------------------------------------===//

Re-implement atomic builtins __sync_add_and_fetch() and __sync_sub_and_fetch
properly.

When the return value is not used (i.e. only care about the value in the
memory), x86 does not have to use add to implement these. Instead, it can use
add, sub, inc, dec instructions with the "lock" prefix.

This is currently implemented using a bit of instruction selection trick. The
issue is the target independent pattern produces one output and a chain and we
want to map it into one that just output a chain. The current trick is to select
it into a MERGE_VALUES with the first definition being an implicit_def. The
proper solution is to add new ISD opcodes for the no-output variant. DAG
combiner can then transform the node before it gets to target node selection.

Problem #2 is we are adding a whole bunch of x86 atomic instructions when in
fact these instructions are identical to the non-lock versions. We need a way to
add target specific information to target nodes and have this information
carried over to machine instructions. Asm printer (or JIT) can use this
information to add the "lock" prefix.

//===---------------------------------------------------------------------===//

struct B {
  unsigned char y0 : 1;
};

int bar(struct B* a) { return a->y0; }

define i32 @bar(%struct.B* nocapture %a) nounwind readonly optsize {
  %1 = getelementptr inbounds %struct.B* %a, i64 0, i32 0
  %2 = load i8* %1, align 1
  %3 = and i8 %2, 1
  %4 = zext i8 %3 to i32
  ret i32 %4
}

bar:                                    # @bar
# %bb.0:
        movb    (%rdi), %al
        andb    $1, %al
        movzbl  %al, %eax
        ret

Missed optimization: should be movl+andl.

//===---------------------------------------------------------------------===//

The x86_64 abi says:

Booleans, when stored in a memory object, are stored as single byte objects the
value of which is always 0 (false) or 1 (true).

We are not using this fact:

int bar(_Bool *a) { return *a; }

define i32 @bar(i8* nocapture %a) nounwind readonly optsize {
  %1 = load i8* %a, align 1, !tbaa !0
  %tmp = and i8 %1, 1
  %2 = zext i8 %tmp to i32
  ret i32 %2
}

bar:
        movb    (%rdi), %al
        andb    $1, %al
        movzbl  %al, %eax
        ret

GCC produces

bar:
        movzbl  (%rdi), %eax
        ret

//===---------------------------------------------------------------------===//

Take the following C code:
int f(int a, int b) { return (unsigned char)a == (unsigned char)b; }

We generate the following IR with clang:
define i32 @f(i32 %a, i32 %b) nounwind readnone {
entry:
  %tmp = xor i32 %b, %a                           ; <i32> [#uses=1]
  %tmp6 = and i32 %tmp, 255                       ; <i32> [#uses=1]
  %cmp = icmp eq i32 %tmp6, 0                     ; <i1> [#uses=1]
  %conv5 = zext i1 %cmp to i32                    ; <i32> [#uses=1]
  ret i32 %conv5
}

And the following x86 code:
	xorl	%esi, %edi
	testb	$-1, %dil
	sete	%al
	movzbl	%al, %eax
	ret

A cmpb instead of the xorl+testb would be one instruction shorter.

//===---------------------------------------------------------------------===//

Given the following C code:
int f(int a, int b) { return (signed char)a == (signed char)b; }

We generate the following IR with clang:
define i32 @f(i32 %a, i32 %b) nounwind readnone {
entry:
  %sext = shl i32 %a, 24                          ; <i32> [#uses=1]
  %conv1 = ashr i32 %sext, 24                     ; <i32> [#uses=1]
  %sext6 = shl i32 %b, 24                         ; <i32> [#uses=1]
  %conv4 = ashr i32 %sext6, 24                    ; <i32> [#uses=1]
  %cmp = icmp eq i32 %conv1, %conv4               ; <i1> [#uses=1]
  %conv5 = zext i1 %cmp to i32                    ; <i32> [#uses=1]
  ret i32 %conv5
}

And the following x86 code:
	movsbl	%sil, %eax
	movsbl	%dil, %ecx
	cmpl	%eax, %ecx
	sete	%al
	movzbl	%al, %eax
	ret


It should be possible to eliminate the sign extensions.

//===---------------------------------------------------------------------===//

LLVM misses a load+store narrowing opportunity in this code:

%struct.bf = type { i64, i16, i16, i32 }

@bfi = external global %struct.bf*                ; <%struct.bf**> [#uses=2]

define void @t1() nounwind ssp {
entry:
  %0 = load %struct.bf** @bfi, align 8            ; <%struct.bf*> [#uses=1]
  %1 = getelementptr %struct.bf* %0, i64 0, i32 1 ; <i16*> [#uses=1]
  %2 = bitcast i16* %1 to i32*                    ; <i32*> [#uses=2]
  %3 = load i32* %2, align 1                      ; <i32> [#uses=1]
  %4 = and i32 %3, -65537                         ; <i32> [#uses=1]
  store i32 %4, i32* %2, align 1
  %5 = load %struct.bf** @bfi, align 8            ; <%struct.bf*> [#uses=1]
  %6 = getelementptr %struct.bf* %5, i64 0, i32 1 ; <i16*> [#uses=1]
  %7 = bitcast i16* %6 to i32*                    ; <i32*> [#uses=2]
  %8 = load i32* %7, align 1                      ; <i32> [#uses=1]
  %9 = and i32 %8, -131073                        ; <i32> [#uses=1]
  store i32 %9, i32* %7, align 1
  ret void
}

LLVM currently emits this:

  movq  bfi(%rip), %rax
  andl  $-65537, 8(%rax)
  movq  bfi(%rip), %rax
  andl  $-131073, 8(%rax)
  ret

It could narrow the loads and stores to emit this:

  movq  bfi(%rip), %rax
  andb  $-2, 10(%rax)
  movq  bfi(%rip), %rax
  andb  $-3, 10(%rax)
  ret

The trouble is that there is a TokenFactor between the store and the
load, making it non-trivial to determine if there's anything between
the load and the store which would prohibit narrowing.

//===---------------------------------------------------------------------===//

This code:
void foo(unsigned x) {
  if (x == 0) bar();
  else if (x == 1) qux();
}

currently compiles into:
_foo:
	movl	4(%esp), %eax
	cmpl	$1, %eax
	je	LBB0_3
	testl	%eax, %eax
	jne	LBB0_4

the testl could be removed:
_foo:
	movl	4(%esp), %eax
	cmpl	$1, %eax
	je	LBB0_3
	jb	LBB0_4

0 is the only unsigned number < 1.

//===---------------------------------------------------------------------===//

This code:

%0 = type { i32, i1 }

define i32 @add32carry(i32 %sum, i32 %x) nounwind readnone ssp {
entry:
  %uadd = tail call %0 @llvm.uadd.with.overflow.i32(i32 %sum, i32 %x)
  %cmp = extractvalue %0 %uadd, 1
  %inc = zext i1 %cmp to i32
  %add = add i32 %x, %sum
  %z.0 = add i32 %add, %inc
  ret i32 %z.0
}

declare %0 @llvm.uadd.with.overflow.i32(i32, i32) nounwind readnone

compiles to:

_add32carry:                            ## @add32carry
	addl	%esi, %edi
	sbbl	%ecx, %ecx
	movl	%edi, %eax
	subl	%ecx, %eax
	ret

But it could be:

_add32carry:
	leal	(%rsi,%rdi), %eax
	cmpl	%esi, %eax
	adcl	$0, %eax
	ret

//===---------------------------------------------------------------------===//

The hot loop of 256.bzip2 contains code that looks a bit like this:

int foo(char *P, char *Q, int x, int y) {
  if (P[0] != Q[0])
     return P[0] < Q[0];
  if (P[1] != Q[1])
     return P[1] < Q[1];
  if (P[2] != Q[2])
     return P[2] < Q[2];
   return P[3] < Q[3];
}

In the real code, we get a lot more wrong than this.  However, even in this
code we generate:

_foo:                                   ## @foo
## %bb.0:                               ## %entry
	movb	(%rsi), %al
	movb	(%rdi), %cl
	cmpb	%al, %cl
	je	LBB0_2
LBB0_1:                                 ## %if.then
	cmpb	%al, %cl
	jmp	LBB0_5
LBB0_2:                                 ## %if.end
	movb	1(%rsi), %al
	movb	1(%rdi), %cl
	cmpb	%al, %cl
	jne	LBB0_1
## %bb.3:                               ## %if.end38
	movb	2(%rsi), %al
	movb	2(%rdi), %cl
	cmpb	%al, %cl
	jne	LBB0_1
## %bb.4:                               ## %if.end60
	movb	3(%rdi), %al
	cmpb	3(%rsi), %al
LBB0_5:                                 ## %if.end60
	setl	%al
	movzbl	%al, %eax
	ret

Note that we generate jumps to LBB0_1 which does a redundant compare.  The
redundant compare also forces the register values to be live, which prevents
folding one of the loads into the compare.  In contrast, GCC 4.2 produces:

_foo:
	movzbl	(%rsi), %eax
	cmpb	%al, (%rdi)
	jne	L10
L12:
	movzbl	1(%rsi), %eax
	cmpb	%al, 1(%rdi)
	jne	L10
	movzbl	2(%rsi), %eax
	cmpb	%al, 2(%rdi)
	jne	L10
	movzbl	3(%rdi), %eax
	cmpb	3(%rsi), %al
L10:
	setl	%al
	movzbl	%al, %eax
	ret

which is "perfect".

//===---------------------------------------------------------------------===//

For the branch in the following code:
int a();
int b(int x, int y) {
  if (x & (1<<(y&7)))
    return a();
  return y;
}

We currently generate:
	movb	%sil, %al
	andb	$7, %al
	movzbl	%al, %eax
	btl	%eax, %edi
	jae	.LBB0_2

movl+andl would be shorter than the movb+andb+movzbl sequence.

//===---------------------------------------------------------------------===//

For the following:
struct u1 {
    float x, y;
};
float foo(struct u1 u) {
    return u.x + u.y;
}

We currently generate:
	movdqa	%xmm0, %xmm1
	pshufd	$1, %xmm0, %xmm0        # xmm0 = xmm0[1,0,0,0]
	addss	%xmm1, %xmm0
	ret

We could save an instruction here by commuting the addss.

//===---------------------------------------------------------------------===//

This (from PR9661):

float clamp_float(float a) {
        if (a > 1.0f)
                return 1.0f;
        else if (a < 0.0f)
                return 0.0f;
        else
                return a;
}

Could compile to:

clamp_float:                            # @clamp_float
        movss   .LCPI0_0(%rip), %xmm1
        minss   %xmm1, %xmm0
        pxor    %xmm1, %xmm1
        maxss   %xmm1, %xmm0
        ret

with -ffast-math.

//===---------------------------------------------------------------------===//

This function (from PR9803):

int clamp2(int a) {
        if (a > 5)
                a = 5;
        if (a < 0) 
                return 0;
        return a;
}

Compiles to:

_clamp2:                                ## @clamp2
        pushq   %rbp
        movq    %rsp, %rbp
        cmpl    $5, %edi
        movl    $5, %ecx
        cmovlel %edi, %ecx
        testl   %ecx, %ecx
        movl    $0, %eax
        cmovnsl %ecx, %eax
        popq    %rbp
        ret

The move of 0 could be scheduled above the test to make it is xor reg,reg.

//===---------------------------------------------------------------------===//

GCC PR48986.  We currently compile this:

void bar(void);
void yyy(int* p) {
    if (__sync_fetch_and_add(p, -1) == 1)
      bar();
}

into:
	movl	$-1, %eax
	lock
	xaddl	%eax, (%rdi)
	cmpl	$1, %eax
	je	LBB0_2

Instead we could generate:

	lock
	dec %rdi
	je LBB0_2

The trick is to match "fetch_and_add(X, -C) == C".

//===---------------------------------------------------------------------===//

unsigned t(unsigned a, unsigned b) {
  return a <= b ? 5 : -5;
}

We generate:
	movl	$5, %ecx
	cmpl	%esi, %edi
	movl	$-5, %eax
	cmovbel	%ecx, %eax

GCC:
	cmpl	%edi, %esi
	sbbl	%eax, %eax
	andl	$-10, %eax
	addl	$5, %eax

//===---------------------------------------------------------------------===//
