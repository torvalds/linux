Target Independent Opportunities:

//===---------------------------------------------------------------------===//

We should recognized various "overflow detection" idioms and translate them into
llvm.uadd.with.overflow and similar intrinsics.  Here is a multiply idiom:

unsigned int mul(unsigned int a,unsigned int b) {
 if ((unsigned long long)a*b>0xffffffff)
   exit(0);
  return a*b;
}

The legalization code for mul-with-overflow needs to be made more robust before
this can be implemented though.

//===---------------------------------------------------------------------===//

Get the C front-end to expand hypot(x,y) -> llvm.sqrt(x*x+y*y) when errno and
precision don't matter (ffastmath).  Misc/mandel will like this. :)  This isn't
safe in general, even on darwin.  See the libm implementation of hypot for
examples (which special case when x/y are exactly zero to get signed zeros etc
right).

//===---------------------------------------------------------------------===//

On targets with expensive 64-bit multiply, we could LSR this:

for (i = ...; ++i) {
   x = 1ULL << i;

into:
 long long tmp = 1;
 for (i = ...; ++i, tmp+=tmp)
   x = tmp;

This would be a win on ppc32, but not x86 or ppc64.

//===---------------------------------------------------------------------===//

Shrink: (setlt (loadi32 P), 0) -> (setlt (loadi8 Phi), 0)

//===---------------------------------------------------------------------===//

Reassociate should turn things like:

int factorial(int X) {
 return X*X*X*X*X*X*X*X;
}

into llvm.powi calls, allowing the code generator to produce balanced
multiplication trees.

First, the intrinsic needs to be extended to support integers, and second the
code generator needs to be enhanced to lower these to multiplication trees.

//===---------------------------------------------------------------------===//

Interesting? testcase for add/shift/mul reassoc:

int bar(int x, int y) {
  return x*x*x+y+x*x*x*x*x*y*y*y*y;
}
int foo(int z, int n) {
  return bar(z, n) + bar(2*z, 2*n);
}

This is blocked on not handling X*X*X -> powi(X, 3) (see note above).  The issue
is that we end up getting t = 2*X  s = t*t   and don't turn this into 4*X*X,
which is the same number of multiplies and is canonical, because the 2*X has
multiple uses.  Here's a simple example:

define i32 @test15(i32 %X1) {
  %B = mul i32 %X1, 47   ; X1*47
  %C = mul i32 %B, %B
  ret i32 %C
}


//===---------------------------------------------------------------------===//

Reassociate should handle the example in GCC PR16157:

extern int a0, a1, a2, a3, a4; extern int b0, b1, b2, b3, b4; 
void f () {  /* this can be optimized to four additions... */ 
        b4 = a4 + a3 + a2 + a1 + a0; 
        b3 = a3 + a2 + a1 + a0; 
        b2 = a2 + a1 + a0; 
        b1 = a1 + a0; 
} 

This requires reassociating to forms of expressions that are already available,
something that reassoc doesn't think about yet.


//===---------------------------------------------------------------------===//

These two functions should generate the same code on big-endian systems:

int g(int *j,int *l)  {  return memcmp(j,l,4);  }
int h(int *j, int *l) {  return *j - *l; }

this could be done in SelectionDAGISel.cpp, along with other special cases,
for 1,2,4,8 bytes.

//===---------------------------------------------------------------------===//

It would be nice to revert this patch:
http://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20060213/031986.html

And teach the dag combiner enough to simplify the code expanded before 
legalize.  It seems plausible that this knowledge would let it simplify other
stuff too.

//===---------------------------------------------------------------------===//

For vector types, DataLayout.cpp::getTypeInfo() returns alignment that is equal
to the type size. It works but can be overly conservative as the alignment of
specific vector types are target dependent.

//===---------------------------------------------------------------------===//

We should produce an unaligned load from code like this:

v4sf example(float *P) {
  return (v4sf){P[0], P[1], P[2], P[3] };
}

//===---------------------------------------------------------------------===//

Add support for conditional increments, and other related patterns.  Instead
of:

	movl 136(%esp), %eax
	cmpl $0, %eax
	je LBB16_2	#cond_next
LBB16_1:	#cond_true
	incl _foo
LBB16_2:	#cond_next

emit:
	movl	_foo, %eax
	cmpl	$1, %edi
	sbbl	$-1, %eax
	movl	%eax, _foo

//===---------------------------------------------------------------------===//

Combine: a = sin(x), b = cos(x) into a,b = sincos(x).

Expand these to calls of sin/cos and stores:
      double sincos(double x, double *sin, double *cos);
      float sincosf(float x, float *sin, float *cos);
      long double sincosl(long double x, long double *sin, long double *cos);

Doing so could allow SROA of the destination pointers.  See also:
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=17687

This is now easily doable with MRVs.  We could even make an intrinsic for this
if anyone cared enough about sincos.

//===---------------------------------------------------------------------===//

quantum_sigma_x in 462.libquantum contains the following loop:

      for(i=0; i<reg->size; i++)
	{
	  /* Flip the target bit of each basis state */
	  reg->node[i].state ^= ((MAX_UNSIGNED) 1 << target);
	} 

Where MAX_UNSIGNED/state is a 64-bit int.  On a 32-bit platform it would be just
so cool to turn it into something like:

   long long Res = ((MAX_UNSIGNED) 1 << target);
   if (target < 32) {
     for(i=0; i<reg->size; i++)
       reg->node[i].state ^= Res & 0xFFFFFFFFULL;
   } else {
     for(i=0; i<reg->size; i++)
       reg->node[i].state ^= Res & 0xFFFFFFFF00000000ULL
   }
   
... which would only do one 32-bit XOR per loop iteration instead of two.

It would also be nice to recognize the reg->size doesn't alias reg->node[i],
but this requires TBAA.

//===---------------------------------------------------------------------===//

This isn't recognized as bswap by instcombine (yes, it really is bswap):

unsigned long reverse(unsigned v) {
    unsigned t;
    t = v ^ ((v << 16) | (v >> 16));
    t &= ~0xff0000;
    v = (v << 24) | (v >> 8);
    return v ^ (t >> 8);
}

//===---------------------------------------------------------------------===//

[LOOP DELETION]

We don't delete this output free loop, because trip count analysis doesn't
realize that it is finite (if it were infinite, it would be undefined).  Not
having this blocks Loop Idiom from matching strlen and friends.  

void foo(char *C) {
  int x = 0;
  while (*C)
    ++x,++C;
}

//===---------------------------------------------------------------------===//

[LOOP RECOGNITION]

These idioms should be recognized as popcount (see PR1488):

unsigned countbits_slow(unsigned v) {
  unsigned c;
  for (c = 0; v; v >>= 1)
    c += v & 1;
  return c;
}

unsigned int popcount(unsigned int input) {
  unsigned int count = 0;
  for (unsigned int i =  0; i < 4 * 8; i++)
    count += (input >> i) & i;
  return count;
}

This should be recognized as CLZ:  https://github.com/llvm/llvm-project/issues/64167

unsigned clz_a(unsigned a) {
  int i;
  for (i=0;i<32;i++)
    if (a & (1<<(31-i)))
      return i;
  return 32;
}

This sort of thing should be added to the loop idiom pass.

//===---------------------------------------------------------------------===//

These should turn into single 16-bit (unaligned?) loads on little/big endian
processors.

unsigned short read_16_le(const unsigned char *adr) {
  return adr[0] | (adr[1] << 8);
}
unsigned short read_16_be(const unsigned char *adr) {
  return (adr[0] << 8) | adr[1];
}

//===---------------------------------------------------------------------===//

-instcombine should handle this transform:
   icmp pred (sdiv X / C1 ), C2
when X, C1, and C2 are unsigned.  Similarly for udiv and signed operands. 

Currently InstCombine avoids this transform but will do it when the signs of
the operands and the sign of the divide match. See the FIXME in 
InstructionCombining.cpp in the visitSetCondInst method after the switch case 
for Instruction::UDiv (around line 4447) for more details.

The SingleSource/Benchmarks/Shootout-C++/hash and hash2 tests have examples of
this construct. 

//===---------------------------------------------------------------------===//

[LOOP OPTIMIZATION]

SingleSource/Benchmarks/Misc/dt.c shows several interesting optimization
opportunities in its double_array_divs_variable function: it needs loop
interchange, memory promotion (which LICM already does), vectorization and
variable trip count loop unrolling (since it has a constant trip count). ICC
apparently produces this very nice code with -ffast-math:

..B1.70:                        # Preds ..B1.70 ..B1.69
       mulpd     %xmm0, %xmm1                                  #108.2
       mulpd     %xmm0, %xmm1                                  #108.2
       mulpd     %xmm0, %xmm1                                  #108.2
       mulpd     %xmm0, %xmm1                                  #108.2
       addl      $8, %edx                                      #
       cmpl      $131072, %edx                                 #108.2
       jb        ..B1.70       # Prob 99%                      #108.2

It would be better to count down to zero, but this is a lot better than what we
do.

//===---------------------------------------------------------------------===//

Consider:

typedef unsigned U32;
typedef unsigned long long U64;
int test (U32 *inst, U64 *regs) {
    U64 effective_addr2;
    U32 temp = *inst;
    int r1 = (temp >> 20) & 0xf;
    int b2 = (temp >> 16) & 0xf;
    effective_addr2 = temp & 0xfff;
    if (b2) effective_addr2 += regs[b2];
    b2 = (temp >> 12) & 0xf;
    if (b2) effective_addr2 += regs[b2];
    effective_addr2 &= regs[4];
     if ((effective_addr2 & 3) == 0)
        return 1;
    return 0;
}

Note that only the low 2 bits of effective_addr2 are used.  On 32-bit systems,
we don't eliminate the computation of the top half of effective_addr2 because
we don't have whole-function selection dags.  On x86, this means we use one
extra register for the function when effective_addr2 is declared as U64 than
when it is declared U32.

PHI Slicing could be extended to do this.

//===---------------------------------------------------------------------===//

Tail call elim should be more aggressive, checking to see if the call is
followed by an uncond branch to an exit block.

; This testcase is due to tail-duplication not wanting to copy the return
; instruction into the terminating blocks because there was other code
; optimized out of the function after the taildup happened.
; RUN: llvm-as < %s | opt -tailcallelim | llvm-dis | not grep call

define i32 @t4(i32 %a) {
entry:
	%tmp.1 = and i32 %a, 1		; <i32> [#uses=1]
	%tmp.2 = icmp ne i32 %tmp.1, 0		; <i1> [#uses=1]
	br i1 %tmp.2, label %then.0, label %else.0

then.0:		; preds = %entry
	%tmp.5 = add i32 %a, -1		; <i32> [#uses=1]
	%tmp.3 = call i32 @t4( i32 %tmp.5 )		; <i32> [#uses=1]
	br label %return

else.0:		; preds = %entry
	%tmp.7 = icmp ne i32 %a, 0		; <i1> [#uses=1]
	br i1 %tmp.7, label %then.1, label %return

then.1:		; preds = %else.0
	%tmp.11 = add i32 %a, -2		; <i32> [#uses=1]
	%tmp.9 = call i32 @t4( i32 %tmp.11 )		; <i32> [#uses=1]
	br label %return

return:		; preds = %then.1, %else.0, %then.0
	%result.0 = phi i32 [ 0, %else.0 ], [ %tmp.3, %then.0 ],
                            [ %tmp.9, %then.1 ]
	ret i32 %result.0
}

//===---------------------------------------------------------------------===//

Tail recursion elimination should handle:

int pow2m1(int n) {
 if (n == 0)
   return 0;
 return 2 * pow2m1 (n - 1) + 1;
}

Also, multiplies can be turned into SHL's, so they should be handled as if
they were associative.  "return foo() << 1" can be tail recursion eliminated.

//===---------------------------------------------------------------------===//

Argument promotion should promote arguments for recursive functions, like 
this:

; RUN: llvm-as < %s | opt -argpromotion | llvm-dis | grep x.val

define internal i32 @foo(i32* %x) {
entry:
	%tmp = load i32* %x		; <i32> [#uses=0]
	%tmp.foo = call i32 @foo( i32* %x )		; <i32> [#uses=1]
	ret i32 %tmp.foo
}

define i32 @bar(i32* %x) {
entry:
	%tmp3 = call i32 @foo( i32* %x )		; <i32> [#uses=1]
	ret i32 %tmp3
}

//===---------------------------------------------------------------------===//

We should investigate an instruction sinking pass.  Consider this silly
example in pic mode:

#include <assert.h>
void foo(int x) {
  assert(x);
  //...
}

we compile this to:
_foo:
	subl	$28, %esp
	call	"L1$pb"
"L1$pb":
	popl	%eax
	cmpl	$0, 32(%esp)
	je	LBB1_2	# cond_true
LBB1_1:	# return
	# ...
	addl	$28, %esp
	ret
LBB1_2:	# cond_true
...

The PIC base computation (call+popl) is only used on one path through the 
code, but is currently always computed in the entry block.  It would be 
better to sink the picbase computation down into the block for the 
assertion, as it is the only one that uses it.  This happens for a lot of 
code with early outs.

Another example is loads of arguments, which are usually emitted into the 
entry block on targets like x86.  If not used in all paths through a 
function, they should be sunk into the ones that do.

In this case, whole-function-isel would also handle this.

//===---------------------------------------------------------------------===//

Investigate lowering of sparse switch statements into perfect hash tables:
http://burtleburtle.net/bob/hash/perfect.html

//===---------------------------------------------------------------------===//

We should turn things like "load+fabs+store" and "load+fneg+store" into the
corresponding integer operations.  On a yonah, this loop:

double a[256];
void foo() {
  int i, b;
  for (b = 0; b < 10000000; b++)
  for (i = 0; i < 256; i++)
    a[i] = -a[i];
}

is twice as slow as this loop:

long long a[256];
void foo() {
  int i, b;
  for (b = 0; b < 10000000; b++)
  for (i = 0; i < 256; i++)
    a[i] ^= (1ULL << 63);
}

and I suspect other processors are similar.  On X86 in particular this is a
big win because doing this with integers allows the use of read/modify/write
instructions.

//===---------------------------------------------------------------------===//

DAG Combiner should try to combine small loads into larger loads when 
profitable.  For example, we compile this C++ example:

struct THotKey { short Key; bool Control; bool Shift; bool Alt; };
extern THotKey m_HotKey;
THotKey GetHotKey () { return m_HotKey; }

into (-m64 -O3 -fno-exceptions -static -fomit-frame-pointer):

__Z9GetHotKeyv:                         ## @_Z9GetHotKeyv
	movq	_m_HotKey@GOTPCREL(%rip), %rax
	movzwl	(%rax), %ecx
	movzbl	2(%rax), %edx
	shlq	$16, %rdx
	orq	%rcx, %rdx
	movzbl	3(%rax), %ecx
	shlq	$24, %rcx
	orq	%rdx, %rcx
	movzbl	4(%rax), %eax
	shlq	$32, %rax
	orq	%rcx, %rax
	ret

//===---------------------------------------------------------------------===//

We should add an FRINT node to the DAG to model targets that have legal
implementations of ceil/floor/rint.

//===---------------------------------------------------------------------===//

Consider:

int test() {
  long long input[8] = {1,0,1,0,1,0,1,0};
  foo(input);
}

Clang compiles this into:

  call void @llvm.memset.p0i8.i64(i8* %tmp, i8 0, i64 64, i32 16, i1 false)
  %0 = getelementptr [8 x i64]* %input, i64 0, i64 0
  store i64 1, i64* %0, align 16
  %1 = getelementptr [8 x i64]* %input, i64 0, i64 2
  store i64 1, i64* %1, align 16
  %2 = getelementptr [8 x i64]* %input, i64 0, i64 4
  store i64 1, i64* %2, align 16
  %3 = getelementptr [8 x i64]* %input, i64 0, i64 6
  store i64 1, i64* %3, align 16

Which gets codegen'd into:

	pxor	%xmm0, %xmm0
	movaps	%xmm0, -16(%rbp)
	movaps	%xmm0, -32(%rbp)
	movaps	%xmm0, -48(%rbp)
	movaps	%xmm0, -64(%rbp)
	movq	$1, -64(%rbp)
	movq	$1, -48(%rbp)
	movq	$1, -32(%rbp)
	movq	$1, -16(%rbp)

It would be better to have 4 movq's of 0 instead of the movaps's.

//===---------------------------------------------------------------------===//

http://llvm.org/PR717:

The following code should compile into "ret int undef". Instead, LLVM
produces "ret int 0":

int f() {
  int x = 4;
  int y;
  if (x == 3) y = 0;
  return y;
}

//===---------------------------------------------------------------------===//

The loop unroller should partially unroll loops (instead of peeling them)
when code growth isn't too bad and when an unroll count allows simplification
of some code within the loop.  One trivial example is:

#include <stdio.h>
int main() {
    int nRet = 17;
    int nLoop;
    for ( nLoop = 0; nLoop < 1000; nLoop++ ) {
        if ( nLoop & 1 )
            nRet += 2;
        else
            nRet -= 1;
    }
    return nRet;
}

Unrolling by 2 would eliminate the '&1' in both copies, leading to a net
reduction in code size.  The resultant code would then also be suitable for
exit value computation.

//===---------------------------------------------------------------------===//

We miss a bunch of rotate opportunities on various targets, including ppc, x86,
etc.  On X86, we miss a bunch of 'rotate by variable' cases because the rotate
matching code in dag combine doesn't look through truncates aggressively 
enough.  Here are some testcases reduces from GCC PR17886:

unsigned long long f5(unsigned long long x, unsigned long long y) {
  return (x << 8) | ((y >> 48) & 0xffull);
}
unsigned long long f6(unsigned long long x, unsigned long long y, int z) {
  switch(z) {
  case 1:
    return (x << 8) | ((y >> 48) & 0xffull);
  case 2:
    return (x << 16) | ((y >> 40) & 0xffffull);
  case 3:
    return (x << 24) | ((y >> 32) & 0xffffffull);
  case 4:
    return (x << 32) | ((y >> 24) & 0xffffffffull);
  default:
    return (x << 40) | ((y >> 16) & 0xffffffffffull);
  }
}

//===---------------------------------------------------------------------===//

This (and similar related idioms):

unsigned int foo(unsigned char i) {
  return i | (i<<8) | (i<<16) | (i<<24);
} 

compiles into:

define i32 @foo(i8 zeroext %i) nounwind readnone ssp noredzone {
entry:
  %conv = zext i8 %i to i32
  %shl = shl i32 %conv, 8
  %shl5 = shl i32 %conv, 16
  %shl9 = shl i32 %conv, 24
  %or = or i32 %shl9, %conv
  %or6 = or i32 %or, %shl5
  %or10 = or i32 %or6, %shl
  ret i32 %or10
}

it would be better as:

unsigned int bar(unsigned char i) {
  unsigned int j=i | (i << 8); 
  return j | (j<<16);
}

aka:

define i32 @bar(i8 zeroext %i) nounwind readnone ssp noredzone {
entry:
  %conv = zext i8 %i to i32
  %shl = shl i32 %conv, 8
  %or = or i32 %shl, %conv
  %shl5 = shl i32 %or, 16
  %or6 = or i32 %shl5, %or
  ret i32 %or6
}

or even i*0x01010101, depending on the speed of the multiplier.  The best way to
handle this is to canonicalize it to a multiply in IR and have codegen handle
lowering multiplies to shifts on cpus where shifts are faster.

//===---------------------------------------------------------------------===//

We do a number of simplifications in simplify libcalls to strength reduce
standard library functions, but we don't currently merge them together.  For
example, it is useful to merge memcpy(a,b,strlen(b)) -> strcpy.  This can only
be done safely if "b" isn't modified between the strlen and memcpy of course.

//===---------------------------------------------------------------------===//

We compile this program: (from GCC PR11680)
http://gcc.gnu.org/bugzilla/attachment.cgi?id=4487

Into code that runs the same speed in fast/slow modes, but both modes run 2x
slower than when compile with GCC (either 4.0 or 4.2):

$ llvm-g++ perf.cpp -O3 -fno-exceptions
$ time ./a.out fast
1.821u 0.003s 0:01.82 100.0%	0+0k 0+0io 0pf+0w

$ g++ perf.cpp -O3 -fno-exceptions
$ time ./a.out fast
0.821u 0.001s 0:00.82 100.0%	0+0k 0+0io 0pf+0w

It looks like we are making the same inlining decisions, so this may be raw
codegen badness or something else (haven't investigated).

//===---------------------------------------------------------------------===//

Divisibility by constant can be simplified (according to GCC PR12849) from
being a mulhi to being a mul lo (cheaper).  Testcase:

void bar(unsigned n) {
  if (n % 3 == 0)
    true();
}

This is equivalent to the following, where 2863311531 is the multiplicative
inverse of 3, and 1431655766 is ((2^32)-1)/3+1:
void bar(unsigned n) {
  if (n * 2863311531U < 1431655766U)
    true();
}

The same transformation can work with an even modulo with the addition of a
rotate: rotate the result of the multiply to the right by the number of bits
which need to be zero for the condition to be true, and shrink the compare RHS
by the same amount.  Unless the target supports rotates, though, that
transformation probably isn't worthwhile.

The transformation can also easily be made to work with non-zero equality
comparisons: just transform, for example, "n % 3 == 1" to "(n-1) % 3 == 0".

//===---------------------------------------------------------------------===//

Better mod/ref analysis for scanf would allow us to eliminate the vtable and a
bunch of other stuff from this example (see PR1604): 

#include <cstdio>
struct test {
    int val;
    virtual ~test() {}
};

int main() {
    test t;
    std::scanf("%d", &t.val);
    std::printf("%d\n", t.val);
}

//===---------------------------------------------------------------------===//

These functions perform the same computation, but produce different assembly.

define i8 @select(i8 %x) readnone nounwind {
  %A = icmp ult i8 %x, 250
  %B = select i1 %A, i8 0, i8 1
  ret i8 %B 
}

define i8 @addshr(i8 %x) readnone nounwind {
  %A = zext i8 %x to i9
  %B = add i9 %A, 6       ;; 256 - 250 == 6
  %C = lshr i9 %B, 8
  %D = trunc i9 %C to i8
  ret i8 %D
}

//===---------------------------------------------------------------------===//

From gcc bug 24696:
int
f (unsigned long a, unsigned long b, unsigned long c)
{
  return ((a & (c - 1)) != 0) || ((b & (c - 1)) != 0);
}
int
f (unsigned long a, unsigned long b, unsigned long c)
{
  return ((a & (c - 1)) != 0) | ((b & (c - 1)) != 0);
}
Both should combine to ((a|b) & (c-1)) != 0.  Currently not optimized with
"clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

From GCC Bug 20192:
#define PMD_MASK    (~((1UL << 23) - 1))
void clear_pmd_range(unsigned long start, unsigned long end)
{
   if (!(start & ~PMD_MASK) && !(end & ~PMD_MASK))
       f();
}
The expression should optimize to something like
"!((start|end)&~PMD_MASK). Currently not optimized with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

unsigned int f(unsigned int i, unsigned int n) {++i; if (i == n) ++i; return
i;}
unsigned int f2(unsigned int i, unsigned int n) {++i; i += i == n; return i;}
These should combine to the same thing.  Currently, the first function
produces better code on X86.

//===---------------------------------------------------------------------===//

From GCC Bug 15784:
#define abs(x) x>0?x:-x
int f(int x, int y)
{
 return (abs(x)) >= 0;
}
This should optimize to x == INT_MIN. (With -fwrapv.)  Currently not
optimized with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

From GCC Bug 14753:
void
rotate_cst (unsigned int a)
{
 a = (a << 10) | (a >> 22);
 if (a == 123)
   bar ();
}
void
minus_cst (unsigned int a)
{
 unsigned int tem;

 tem = 20 - a;
 if (tem == 5)
   bar ();
}
void
mask_gt (unsigned int a)
{
 /* This is equivalent to a > 15.  */
 if ((a & ~7) > 8)
   bar ();
}
void
rshift_gt (unsigned int a)
{
 /* This is equivalent to a > 23.  */
 if ((a >> 2) > 5)
   bar ();
}

All should simplify to a single comparison.  All of these are
currently not optimized with "clang -emit-llvm-bc | opt
-O3".

//===---------------------------------------------------------------------===//

From GCC Bug 32605:
int c(int* x) {return (char*)x+2 == (char*)x;}
Should combine to 0.  Currently not optimized with "clang
-emit-llvm-bc | opt -O3" (although llc can optimize it).

//===---------------------------------------------------------------------===//

int a(unsigned b) {return ((b << 31) | (b << 30)) >> 31;}
Should be combined to  "((b >> 1) | b) & 1".  Currently not optimized
with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

unsigned a(unsigned x, unsigned y) { return x | (y & 1) | (y & 2);}
Should combine to "x | (y & 3)".  Currently not optimized with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int a(int a, int b, int c) {return (~a & c) | ((c|a) & b);}
Should fold to "(~a & c) | (a & b)".  Currently not optimized with
"clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int a(int a,int b) {return (~(a|b))|a;}
Should fold to "a|~b".  Currently not optimized with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int a(int a, int b) {return (a&&b) || (a&&!b);}
Should fold to "a".  Currently not optimized with "clang -emit-llvm-bc
| opt -O3".

//===---------------------------------------------------------------------===//

int a(int a, int b, int c) {return (a&&b) || (!a&&c);}
Should fold to "a ? b : c", or at least something sane.  Currently not
optimized with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int a(int a, int b, int c) {return (a&&b) || (a&&c) || (a&&b&&c);}
Should fold to a && (b || c).  Currently not optimized with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int a(int x) {return x | ((x & 8) ^ 8);}
Should combine to x | 8.  Currently not optimized with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int a(int x) {return x ^ ((x & 8) ^ 8);}
Should also combine to x | 8.  Currently not optimized with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int a(int x) {return ((x | -9) ^ 8) & x;}
Should combine to x & -9.  Currently not optimized with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

unsigned a(unsigned a) {return a * 0x11111111 >> 28 & 1;}
Should combine to "a * 0x88888888 >> 31".  Currently not optimized
with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

unsigned a(char* x) {if ((*x & 32) == 0) return b();}
There's an unnecessary zext in the generated code with "clang
-emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

unsigned a(unsigned long long x) {return 40 * (x >> 1);}
Should combine to "20 * (((unsigned)x) & -2)".  Currently not
optimized with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int g(int x) { return (x - 10) < 0; }
Should combine to "x <= 9" (the sub has nsw).  Currently not
optimized with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int g(int x) { return (x + 10) < 0; }
Should combine to "x < -10" (the add has nsw).  Currently not
optimized with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

int f(int i, int j) { return i < j + 1; }
int g(int i, int j) { return j > i - 1; }
Should combine to "i <= j" (the add/sub has nsw).  Currently not
optimized with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

unsigned f(unsigned x) { return ((x & 7) + 1) & 15; }
The & 15 part should be optimized away, it doesn't change the result. Currently
not optimized with "clang -emit-llvm-bc | opt -O3".

//===---------------------------------------------------------------------===//

This was noticed in the entryblock for grokdeclarator in 403.gcc:

        %tmp = icmp eq i32 %decl_context, 4          
        %decl_context_addr.0 = select i1 %tmp, i32 3, i32 %decl_context 
        %tmp1 = icmp eq i32 %decl_context_addr.0, 1 
        %decl_context_addr.1 = select i1 %tmp1, i32 0, i32 %decl_context_addr.0

tmp1 should be simplified to something like:
  (!tmp || decl_context == 1)

This allows recursive simplifications, tmp1 is used all over the place in
the function, e.g. by:

        %tmp23 = icmp eq i32 %decl_context_addr.1, 0            ; <i1> [#uses=1]
        %tmp24 = xor i1 %tmp1, true             ; <i1> [#uses=1]
        %or.cond8 = and i1 %tmp23, %tmp24               ; <i1> [#uses=1]

later.

//===---------------------------------------------------------------------===//

[STORE SINKING]

Store sinking: This code:

void f (int n, int *cond, int *res) {
    int i;
    *res = 0;
    for (i = 0; i < n; i++)
        if (*cond)
            *res ^= 234; /* (*) */
}

On this function GVN hoists the fully redundant value of *res, but nothing
moves the store out.  This gives us this code:

bb:		; preds = %bb2, %entry
	%.rle = phi i32 [ 0, %entry ], [ %.rle6, %bb2 ]	
	%i.05 = phi i32 [ 0, %entry ], [ %indvar.next, %bb2 ]
	%1 = load i32* %cond, align 4
	%2 = icmp eq i32 %1, 0
	br i1 %2, label %bb2, label %bb1

bb1:		; preds = %bb
	%3 = xor i32 %.rle, 234	
	store i32 %3, i32* %res, align 4
	br label %bb2

bb2:		; preds = %bb, %bb1
	%.rle6 = phi i32 [ %3, %bb1 ], [ %.rle, %bb ]	
	%indvar.next = add i32 %i.05, 1	
	%exitcond = icmp eq i32 %indvar.next, %n
	br i1 %exitcond, label %return, label %bb

DSE should sink partially dead stores to get the store out of the loop.

Here's another partial dead case:
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=12395

//===---------------------------------------------------------------------===//

Scalar PRE hoists the mul in the common block up to the else:

int test (int a, int b, int c, int g) {
  int d, e;
  if (a)
    d = b * c;
  else
    d = b - c;
  e = b * c + g;
  return d + e;
}

It would be better to do the mul once to reduce codesize above the if.
This is GCC PR38204.


//===---------------------------------------------------------------------===//
This simple function from 179.art:

int winner, numf2s;
struct { double y; int   reset; } *Y;

void find_match() {
   int i;
   winner = 0;
   for (i=0;i<numf2s;i++)
       if (Y[i].y > Y[winner].y)
              winner =i;
}

Compiles into (with clang TBAA):

for.body:                                         ; preds = %for.inc, %bb.nph
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.inc ]
  %i.01718 = phi i32 [ 0, %bb.nph ], [ %i.01719, %for.inc ]
  %tmp4 = getelementptr inbounds %struct.anon* %tmp3, i64 %indvar, i32 0
  %tmp5 = load double* %tmp4, align 8, !tbaa !4
  %idxprom7 = sext i32 %i.01718 to i64
  %tmp10 = getelementptr inbounds %struct.anon* %tmp3, i64 %idxprom7, i32 0
  %tmp11 = load double* %tmp10, align 8, !tbaa !4
  %cmp12 = fcmp ogt double %tmp5, %tmp11
  br i1 %cmp12, label %if.then, label %for.inc

if.then:                                          ; preds = %for.body
  %i.017 = trunc i64 %indvar to i32
  br label %for.inc

for.inc:                                          ; preds = %for.body, %if.then
  %i.01719 = phi i32 [ %i.01718, %for.body ], [ %i.017, %if.then ]
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %tmp22
  br i1 %exitcond, label %for.cond.for.end_crit_edge, label %for.body


It is good that we hoisted the reloads of numf2's, and Y out of the loop and
sunk the store to winner out.

However, this is awful on several levels: the conditional truncate in the loop
(-indvars at fault? why can't we completely promote the IV to i64?).

Beyond that, we have a partially redundant load in the loop: if "winner" (aka 
%i.01718) isn't updated, we reload Y[winner].y the next time through the loop.
Similarly, the addressing that feeds it (including the sext) is redundant. In
the end we get this generated assembly:

LBB0_2:                                 ## %for.body
                                        ## =>This Inner Loop Header: Depth=1
	movsd	(%rdi), %xmm0
	movslq	%edx, %r8
	shlq	$4, %r8
	ucomisd	(%rcx,%r8), %xmm0
	jbe	LBB0_4
	movl	%esi, %edx
LBB0_4:                                 ## %for.inc
	addq	$16, %rdi
	incq	%rsi
	cmpq	%rsi, %rax
	jne	LBB0_2

All things considered this isn't too bad, but we shouldn't need the movslq or
the shlq instruction, or the load folded into ucomisd every time through the
loop.

On an x86-specific topic, if the loop can't be restructure, the movl should be a
cmov.

//===---------------------------------------------------------------------===//

[STORE SINKING]

GCC PR37810 is an interesting case where we should sink load/store reload
into the if block and outside the loop, so we don't reload/store it on the
non-call path.

for () {
  *P += 1;
  if ()
    call();
  else
    ...
->
tmp = *P
for () {
  tmp += 1;
  if () {
    *P = tmp;
    call();
    tmp = *P;
  } else ...
}
*P = tmp;

We now hoist the reload after the call (Transforms/GVN/lpre-call-wrap.ll), but
we don't sink the store.  We need partially dead store sinking.

//===---------------------------------------------------------------------===//

[LOAD PRE CRIT EDGE SPLITTING]

GCC PR37166: Sinking of loads prevents SROA'ing the "g" struct on the stack
leading to excess stack traffic. This could be handled by GVN with some crazy
symbolic phi translation.  The code we get looks like (g is on the stack):

bb2:		; preds = %bb1
..
	%9 = getelementptr %struct.f* %g, i32 0, i32 0		
	store i32 %8, i32* %9, align  bel %bb3

bb3:		; preds = %bb1, %bb2, %bb
	%c_addr.0 = phi %struct.f* [ %g, %bb2 ], [ %c, %bb ], [ %c, %bb1 ]
	%b_addr.0 = phi %struct.f* [ %b, %bb2 ], [ %g, %bb ], [ %b, %bb1 ]
	%10 = getelementptr %struct.f* %c_addr.0, i32 0, i32 0
	%11 = load i32* %10, align 4

%11 is partially redundant, an in BB2 it should have the value %8.

GCC PR33344 and PR35287 are similar cases.


//===---------------------------------------------------------------------===//

[LOAD PRE]

There are many load PRE testcases in testsuite/gcc.dg/tree-ssa/loadpre* in the
GCC testsuite, ones we don't get yet are (checked through loadpre25):

[CRIT EDGE BREAKING]
predcom-4.c

[PRE OF READONLY CALL]
loadpre5.c

[TURN SELECT INTO BRANCH]
loadpre14.c loadpre15.c 

actually a conditional increment: loadpre18.c loadpre19.c

//===---------------------------------------------------------------------===//

[LOAD PRE / STORE SINKING / SPEC HACK]

This is a chunk of code from 456.hmmer:

int f(int M, int *mc, int *mpp, int *tpmm, int *ip, int *tpim, int *dpp,
     int *tpdm, int xmb, int *bp, int *ms) {
 int k, sc;
 for (k = 1; k <= M; k++) {
     mc[k] = mpp[k-1]   + tpmm[k-1];
     if ((sc = ip[k-1]  + tpim[k-1]) > mc[k])  mc[k] = sc;
     if ((sc = dpp[k-1] + tpdm[k-1]) > mc[k])  mc[k] = sc;
     if ((sc = xmb  + bp[k])         > mc[k])  mc[k] = sc;
     mc[k] += ms[k];
   }
}

It is very profitable for this benchmark to turn the conditional stores to mc[k]
into a conditional move (select instr in IR) and allow the final store to do the
store.  See GCC PR27313 for more details.  Note that this is valid to xform even
with the new C++ memory model, since mc[k] is previously loaded and later
stored.

//===---------------------------------------------------------------------===//

[SCALAR PRE]
There are many PRE testcases in testsuite/gcc.dg/tree-ssa/ssa-pre-*.c in the
GCC testsuite.

//===---------------------------------------------------------------------===//

There are some interesting cases in testsuite/gcc.dg/tree-ssa/pred-comm* in the
GCC testsuite.  For example, we get the first example in predcom-1.c, but 
miss the second one:

unsigned fib[1000];
unsigned avg[1000];

__attribute__ ((noinline))
void count_averages(int n) {
  int i;
  for (i = 1; i < n; i++)
    avg[i] = (((unsigned long) fib[i - 1] + fib[i] + fib[i + 1]) / 3) & 0xffff;
}

which compiles into two loads instead of one in the loop.

predcom-2.c is the same as predcom-1.c

predcom-3.c is very similar but needs loads feeding each other instead of
store->load.


//===---------------------------------------------------------------------===//

[ALIAS ANALYSIS]

Type based alias analysis:
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=14705

We should do better analysis of posix_memalign.  At the least it should
no-capture its pointer argument, at best, we should know that the out-value
result doesn't point to anything (like malloc).  One example of this is in
SingleSource/Benchmarks/Misc/dt.c

//===---------------------------------------------------------------------===//

Interesting missed case because of control flow flattening (should be 2 loads):
http://gcc.gnu.org/bugzilla/show_bug.cgi?id=26629
With: llvm-gcc t2.c -S -o - -O0 -emit-llvm | llvm-as | 
             opt -mem2reg -gvn -instcombine | llvm-dis
we miss it because we need 1) CRIT EDGE 2) MULTIPLE DIFFERENT
VALS PRODUCED BY ONE BLOCK OVER DIFFERENT PATHS

//===---------------------------------------------------------------------===//

http://gcc.gnu.org/bugzilla/show_bug.cgi?id=19633
We could eliminate the branch condition here, loading from null is undefined:

struct S { int w, x, y, z; };
struct T { int r; struct S s; };
void bar (struct S, int);
void foo (int a, struct T b)
{
  struct S *c = 0;
  if (a)
    c = &b.s;
  bar (*c, a);
}

//===---------------------------------------------------------------------===//

simplifylibcalls should do several optimizations for strspn/strcspn:

strcspn(x, "a") -> inlined loop for up to 3 letters (similarly for strspn):

size_t __strcspn_c3 (__const char *__s, int __reject1, int __reject2,
                     int __reject3) {
  register size_t __result = 0;
  while (__s[__result] != '\0' && __s[__result] != __reject1 &&
         __s[__result] != __reject2 && __s[__result] != __reject3)
    ++__result;
  return __result;
}

This should turn into a switch on the character.  See PR3253 for some notes on
codegen.

456.hmmer apparently uses strcspn and strspn a lot.  471.omnetpp uses strspn.

//===---------------------------------------------------------------------===//

simplifylibcalls should turn these snprintf idioms into memcpy (GCC PR47917)

char buf1[6], buf2[6], buf3[4], buf4[4];
int i;

int foo (void) {
  int ret = snprintf (buf1, sizeof buf1, "abcde");
  ret += snprintf (buf2, sizeof buf2, "abcdef") * 16;
  ret += snprintf (buf3, sizeof buf3, "%s", i++ < 6 ? "abc" : "def") * 256;
  ret += snprintf (buf4, sizeof buf4, "%s", i++ > 10 ? "abcde" : "defgh")*4096;
  return ret;
}

//===---------------------------------------------------------------------===//

"gas" uses this idiom:
  else if (strchr ("+-/*%|&^:[]()~", *intel_parser.op_string))
..
  else if (strchr ("<>", *intel_parser.op_string)

Those should be turned into a switch.  SimplifyLibCalls only gets the second
case.

//===---------------------------------------------------------------------===//

252.eon contains this interesting code:

        %3072 = getelementptr [100 x i8]* %tempString, i32 0, i32 0
        %3073 = call i8* @strcpy(i8* %3072, i8* %3071) nounwind
        %strlen = call i32 @strlen(i8* %3072)    ; uses = 1
        %endptr = getelementptr [100 x i8]* %tempString, i32 0, i32 %strlen
        call void @llvm.memcpy.i32(i8* %endptr, 
          i8* getelementptr ([5 x i8]* @"\01LC42", i32 0, i32 0), i32 5, i32 1)
        %3074 = call i32 @strlen(i8* %endptr) nounwind readonly 
        
This is interesting for a couple reasons.  First, in this:

The memcpy+strlen strlen can be replaced with:

        %3074 = call i32 @strlen([5 x i8]* @"\01LC42") nounwind readonly 

Because the destination was just copied into the specified memory buffer.  This,
in turn, can be constant folded to "4".

In other code, it contains:

        %endptr6978 = bitcast i8* %endptr69 to i32*            
        store i32 7107374, i32* %endptr6978, align 1
        %3167 = call i32 @strlen(i8* %endptr69) nounwind readonly    

Which could also be constant folded.  Whatever is producing this should probably
be fixed to leave this as a memcpy from a string.

Further, eon also has an interesting partially redundant strlen call:

bb8:            ; preds = %_ZN18eonImageCalculatorC1Ev.exit
        %682 = getelementptr i8** %argv, i32 6          ; <i8**> [#uses=2]
        %683 = load i8** %682, align 4          ; <i8*> [#uses=4]
        %684 = load i8* %683, align 1           ; <i8> [#uses=1]
        %685 = icmp eq i8 %684, 0               ; <i1> [#uses=1]
        br i1 %685, label %bb10, label %bb9

bb9:            ; preds = %bb8
        %686 = call i32 @strlen(i8* %683) nounwind readonly          
        %687 = icmp ugt i32 %686, 254           ; <i1> [#uses=1]
        br i1 %687, label %bb10, label %bb11

bb10:           ; preds = %bb9, %bb8
        %688 = call i32 @strlen(i8* %683) nounwind readonly          

This could be eliminated by doing the strlen once in bb8, saving code size and
improving perf on the bb8->9->10 path.

//===---------------------------------------------------------------------===//

I see an interesting fully redundant call to strlen left in 186.crafty:InputMove
which looks like:
       %movetext11 = getelementptr [128 x i8]* %movetext, i32 0, i32 0 
 

bb62:           ; preds = %bb55, %bb53
        %promote.0 = phi i32 [ %169, %bb55 ], [ 0, %bb53 ]             
        %171 = call i32 @strlen(i8* %movetext11) nounwind readonly align 1
        %172 = add i32 %171, -1         ; <i32> [#uses=1]
        %173 = getelementptr [128 x i8]* %movetext, i32 0, i32 %172       

...  no stores ...
       br i1 %or.cond, label %bb65, label %bb72

bb65:           ; preds = %bb62
        store i8 0, i8* %173, align 1
        br label %bb72

bb72:           ; preds = %bb65, %bb62
        %trank.1 = phi i32 [ %176, %bb65 ], [ -1, %bb62 ]            
        %177 = call i32 @strlen(i8* %movetext11) nounwind readonly align 1

Note that on the bb62->bb72 path, that the %177 strlen call is partially
redundant with the %171 call.  At worst, we could shove the %177 strlen call
up into the bb65 block moving it out of the bb62->bb72 path.   However, note
that bb65 stores to the string, zeroing out the last byte.  This means that on
that path the value of %177 is actually just %171-1.  A sub is cheaper than a
strlen!

This pattern repeats several times, basically doing:

  A = strlen(P);
  P[A-1] = 0;
  B = strlen(P);
  where it is "obvious" that B = A-1.

//===---------------------------------------------------------------------===//

186.crafty has this interesting pattern with the "out.4543" variable:

call void @llvm.memcpy.i32(
        i8* getelementptr ([10 x i8]* @out.4543, i32 0, i32 0),
       i8* getelementptr ([7 x i8]* @"\01LC28700", i32 0, i32 0), i32 7, i32 1) 
%101 = call@printf(i8* ...   @out.4543, i32 0, i32 0)) nounwind 

It is basically doing:

  memcpy(globalarray, "string");
  printf(...,  globalarray);
  
Anyway, by knowing that printf just reads the memory and forward substituting
the string directly into the printf, this eliminates reads from globalarray.
Since this pattern occurs frequently in crafty (due to the "DisplayTime" and
other similar functions) there are many stores to "out".  Once all the printfs
stop using "out", all that is left is the memcpy's into it.  This should allow
globalopt to remove the "stored only" global.

//===---------------------------------------------------------------------===//

This code:

define inreg i32 @foo(i8* inreg %p) nounwind {
  %tmp0 = load i8* %p
  %tmp1 = ashr i8 %tmp0, 5
  %tmp2 = sext i8 %tmp1 to i32
  ret i32 %tmp2
}

could be dagcombine'd to a sign-extending load with a shift.
For example, on x86 this currently gets this:

	movb	(%eax), %al
	sarb	$5, %al
	movsbl	%al, %eax

while it could get this:

	movsbl	(%eax), %eax
	sarl	$5, %eax

//===---------------------------------------------------------------------===//

GCC PR31029:

int test(int x) { return 1-x == x; }     // --> return false
int test2(int x) { return 2-x == x; }    // --> return x == 1 ?

Always foldable for odd constants, what is the rule for even?

//===---------------------------------------------------------------------===//

PR 3381: GEP to field of size 0 inside a struct could be turned into GEP
for next field in struct (which is at same address).

For example: store of float into { {{}}, float } could be turned into a store to
the float directly.

//===---------------------------------------------------------------------===//

The arg promotion pass should make use of nocapture to make its alias analysis
stuff much more precise.

//===---------------------------------------------------------------------===//

The following functions should be optimized to use a select instead of a
branch (from gcc PR40072):

char char_int(int m) {if(m>7) return 0; return m;}
int int_char(char m) {if(m>7) return 0; return m;}

//===---------------------------------------------------------------------===//

int func(int a, int b) { if (a & 0x80) b |= 0x80; else b &= ~0x80; return b; }

Generates this:

define i32 @func(i32 %a, i32 %b) nounwind readnone ssp {
entry:
  %0 = and i32 %a, 128                            ; <i32> [#uses=1]
  %1 = icmp eq i32 %0, 0                          ; <i1> [#uses=1]
  %2 = or i32 %b, 128                             ; <i32> [#uses=1]
  %3 = and i32 %b, -129                           ; <i32> [#uses=1]
  %b_addr.0 = select i1 %1, i32 %3, i32 %2        ; <i32> [#uses=1]
  ret i32 %b_addr.0
}

However, it's functionally equivalent to:

         b = (b & ~0x80) | (a & 0x80);

Which generates this:

define i32 @func(i32 %a, i32 %b) nounwind readnone ssp {
entry:
  %0 = and i32 %b, -129                           ; <i32> [#uses=1]
  %1 = and i32 %a, 128                            ; <i32> [#uses=1]
  %2 = or i32 %0, %1                              ; <i32> [#uses=1]
  ret i32 %2
}

This can be generalized for other forms:

     b = (b & ~0x80) | (a & 0x40) << 1;

//===---------------------------------------------------------------------===//

These two functions produce different code. They shouldn't:

#include <stdint.h>
 
uint8_t p1(uint8_t b, uint8_t a) {
  b = (b & ~0xc0) | (a & 0xc0);
  return (b);
}
 
uint8_t p2(uint8_t b, uint8_t a) {
  b = (b & ~0x40) | (a & 0x40);
  b = (b & ~0x80) | (a & 0x80);
  return (b);
}

define zeroext i8 @p1(i8 zeroext %b, i8 zeroext %a) nounwind readnone ssp {
entry:
  %0 = and i8 %b, 63                              ; <i8> [#uses=1]
  %1 = and i8 %a, -64                             ; <i8> [#uses=1]
  %2 = or i8 %1, %0                               ; <i8> [#uses=1]
  ret i8 %2
}

define zeroext i8 @p2(i8 zeroext %b, i8 zeroext %a) nounwind readnone ssp {
entry:
  %0 = and i8 %b, 63                              ; <i8> [#uses=1]
  %.masked = and i8 %a, 64                        ; <i8> [#uses=1]
  %1 = and i8 %a, -128                            ; <i8> [#uses=1]
  %2 = or i8 %1, %0                               ; <i8> [#uses=1]
  %3 = or i8 %2, %.masked                         ; <i8> [#uses=1]
  ret i8 %3
}

//===---------------------------------------------------------------------===//

IPSCCP does not currently propagate argument dependent constants through
functions where it does not not all of the callers.  This includes functions
with normal external linkage as well as templates, C99 inline functions etc.
Specifically, it does nothing to:

define i32 @test(i32 %x, i32 %y, i32 %z) nounwind {
entry:
  %0 = add nsw i32 %y, %z                         
  %1 = mul i32 %0, %x                             
  %2 = mul i32 %y, %z                             
  %3 = add nsw i32 %1, %2                         
  ret i32 %3
}

define i32 @test2() nounwind {
entry:
  %0 = call i32 @test(i32 1, i32 2, i32 4) nounwind
  ret i32 %0
}

It would be interesting extend IPSCCP to be able to handle simple cases like
this, where all of the arguments to a call are constant.  Because IPSCCP runs
before inlining, trivial templates and inline functions are not yet inlined.
The results for a function + set of constant arguments should be memoized in a
map.

//===---------------------------------------------------------------------===//

The libcall constant folding stuff should be moved out of SimplifyLibcalls into
libanalysis' constantfolding logic.  This would allow IPSCCP to be able to
handle simple things like this:

static int foo(const char *X) { return strlen(X); }
int bar() { return foo("abcd"); }

//===---------------------------------------------------------------------===//

function-attrs doesn't know much about memcpy/memset.  This function should be
marked readnone rather than readonly, since it only twiddles local memory, but
function-attrs doesn't handle memset/memcpy/memmove aggressively:

struct X { int *p; int *q; };
int foo() {
 int i = 0, j = 1;
 struct X x, y;
 int **p;
 y.p = &i;
 x.q = &j;
 p = __builtin_memcpy (&x, &y, sizeof (int *));
 return **p;
}

This can be seen at:
$ clang t.c -S -o - -mkernel -O0 -emit-llvm | opt -function-attrs -S


//===---------------------------------------------------------------------===//

Missed instcombine transformation:
define i1 @a(i32 %x) nounwind readnone {
entry:
  %cmp = icmp eq i32 %x, 30
  %sub = add i32 %x, -30
  %cmp2 = icmp ugt i32 %sub, 9
  %or = or i1 %cmp, %cmp2
  ret i1 %or
}
This should be optimized to a single compare.  Testcase derived from gcc.

//===---------------------------------------------------------------------===//

Missed instcombine or reassociate transformation:
int a(int a, int b) { return (a==12)&(b>47)&(b<58); }

The sgt and slt should be combined into a single comparison. Testcase derived
from gcc.

//===---------------------------------------------------------------------===//

Missed instcombine transformation:

  %382 = srem i32 %tmp14.i, 64                    ; [#uses=1]
  %383 = zext i32 %382 to i64                     ; [#uses=1]
  %384 = shl i64 %381, %383                       ; [#uses=1]
  %385 = icmp slt i32 %tmp14.i, 64                ; [#uses=1]

The srem can be transformed to an and because if %tmp14.i is negative, the
shift is undefined.  Testcase derived from 403.gcc.

//===---------------------------------------------------------------------===//

This is a range comparison on a divided result (from 403.gcc):

  %1337 = sdiv i32 %1336, 8                       ; [#uses=1]
  %.off.i208 = add i32 %1336, 7                   ; [#uses=1]
  %1338 = icmp ult i32 %.off.i208, 15             ; [#uses=1]
  
We already catch this (removing the sdiv) if there isn't an add, we should
handle the 'add' as well.  This is a common idiom with it's builtin_alloca code.
C testcase:

int a(int x) { return (unsigned)(x/16+7) < 15; }

Another similar case involves truncations on 64-bit targets:

  %361 = sdiv i64 %.046, 8                        ; [#uses=1]
  %362 = trunc i64 %361 to i32                    ; [#uses=2]
...
  %367 = icmp eq i32 %362, 0                      ; [#uses=1]

//===---------------------------------------------------------------------===//

Missed instcombine/dagcombine transformation:
define void @lshift_lt(i8 zeroext %a) nounwind {
entry:
  %conv = zext i8 %a to i32
  %shl = shl i32 %conv, 3
  %cmp = icmp ult i32 %shl, 33
  br i1 %cmp, label %if.then, label %if.end

if.then:
  tail call void @bar() nounwind
  ret void

if.end:
  ret void
}
declare void @bar() nounwind

The shift should be eliminated.  Testcase derived from gcc.

//===---------------------------------------------------------------------===//

These compile into different code, one gets recognized as a switch and the
other doesn't due to phase ordering issues (PR6212):

int test1(int mainType, int subType) {
  if (mainType == 7)
    subType = 4;
  else if (mainType == 9)
    subType = 6;
  else if (mainType == 11)
    subType = 9;
  return subType;
}

int test2(int mainType, int subType) {
  if (mainType == 7)
    subType = 4;
  if (mainType == 9)
    subType = 6;
  if (mainType == 11)
    subType = 9;
  return subType;
}

//===---------------------------------------------------------------------===//

The following test case (from PR6576):

define i32 @mul(i32 %a, i32 %b) nounwind readnone {
entry:
 %cond1 = icmp eq i32 %b, 0                      ; <i1> [#uses=1]
 br i1 %cond1, label %exit, label %bb.nph
bb.nph:                                           ; preds = %entry
 %tmp = mul i32 %b, %a                           ; <i32> [#uses=1]
 ret i32 %tmp
exit:                                             ; preds = %entry
 ret i32 0
}

could be reduced to:

define i32 @mul(i32 %a, i32 %b) nounwind readnone {
entry:
 %tmp = mul i32 %b, %a
 ret i32 %tmp
}

//===---------------------------------------------------------------------===//

We should use DSE + llvm.lifetime.end to delete dead vtable pointer updates.
See GCC PR34949

Another interesting case is that something related could be used for variables
that go const after their ctor has finished.  In these cases, globalopt (which
can statically run the constructor) could mark the global const (so it gets put
in the readonly section).  A testcase would be:

#include <complex>
using namespace std;
const complex<char> should_be_in_rodata (42,-42);
complex<char> should_be_in_data (42,-42);
complex<char> should_be_in_bss;

Where we currently evaluate the ctors but the globals don't become const because
the optimizer doesn't know they "become const" after the ctor is done.  See
GCC PR4131 for more examples.

//===---------------------------------------------------------------------===//

In this code:

long foo(long x) {
  return x > 1 ? x : 1;
}

LLVM emits a comparison with 1 instead of 0. 0 would be equivalent
and cheaper on most targets.

LLVM prefers comparisons with zero over non-zero in general, but in this
case it choses instead to keep the max operation obvious.

//===---------------------------------------------------------------------===//

define void @a(i32 %x) nounwind {
entry:
  switch i32 %x, label %if.end [
    i32 0, label %if.then
    i32 1, label %if.then
    i32 2, label %if.then
    i32 3, label %if.then
    i32 5, label %if.then
  ]
if.then:
  tail call void @foo() nounwind
  ret void
if.end:
  ret void
}
declare void @foo()

Generated code on x86-64 (other platforms give similar results):
a:
	cmpl	$5, %edi
	ja	LBB2_2
	cmpl	$4, %edi
	jne	LBB2_3
.LBB0_2:
	ret
.LBB0_3:
	jmp	foo  # TAILCALL

If we wanted to be really clever, we could simplify the whole thing to
something like the following, which eliminates a branch:
	xorl    $1, %edi
	cmpl	$4, %edi
	ja	.LBB0_2
	ret
.LBB0_2:
	jmp	foo  # TAILCALL

//===---------------------------------------------------------------------===//

We compile this:

int foo(int a) { return (a & (~15)) / 16; }

Into:

define i32 @foo(i32 %a) nounwind readnone ssp {
entry:
  %and = and i32 %a, -16
  %div = sdiv i32 %and, 16
  ret i32 %div
}

but this code (X & -A)/A is X >> log2(A) when A is a power of 2, so this case
should be instcombined into just "a >> 4".

We do get this at the codegen level, so something knows about it, but 
instcombine should catch it earlier:

_foo:                                   ## @foo
## %bb.0:                               ## %entry
	movl	%edi, %eax
	sarl	$4, %eax
	ret

//===---------------------------------------------------------------------===//

This code (from GCC PR28685):

int test(int a, int b) {
  int lt = a < b;
  int eq = a == b;
  if (lt)
    return 1;
  return eq;
}

Is compiled to:

define i32 @test(i32 %a, i32 %b) nounwind readnone ssp {
entry:
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %return, label %if.end

if.end:                                           ; preds = %entry
  %cmp5 = icmp eq i32 %a, %b
  %conv6 = zext i1 %cmp5 to i32
  ret i32 %conv6

return:                                           ; preds = %entry
  ret i32 1
}

it could be:

define i32 @test__(i32 %a, i32 %b) nounwind readnone ssp {
entry:
  %0 = icmp sle i32 %a, %b
  %retval = zext i1 %0 to i32
  ret i32 %retval
}

//===---------------------------------------------------------------------===//

This code can be seen in viterbi:

  %64 = call noalias i8* @malloc(i64 %62) nounwind
...
  %67 = call i64 @llvm.objectsize.i64(i8* %64, i1 false) nounwind
  %68 = call i8* @__memset_chk(i8* %64, i32 0, i64 %62, i64 %67) nounwind

llvm.objectsize.i64 should be taught about malloc/calloc, allowing it to
fold to %62.  This is a security win (overflows of malloc will get caught)
and also a performance win by exposing more memsets to the optimizer.

This occurs several times in viterbi.

Note that this would change the semantics of @llvm.objectsize which by its
current definition always folds to a constant. We also should make sure that
we remove checking in code like

  char *p = malloc(strlen(s)+1);
  __strcpy_chk(p, s, __builtin_object_size(p, 0));

//===---------------------------------------------------------------------===//

clang -O3 currently compiles this code

int g(unsigned int a) {
  unsigned int c[100];
  c[10] = a;
  c[11] = a;
  unsigned int b = c[10] + c[11];
  if(b > a*2) a = 4;
  else a = 8;
  return a + 7;
}

into

define i32 @g(i32 a) nounwind readnone {
  %add = shl i32 %a, 1
  %mul = shl i32 %a, 1
  %cmp = icmp ugt i32 %add, %mul
  %a.addr.0 = select i1 %cmp, i32 11, i32 15
  ret i32 %a.addr.0
}

The icmp should fold to false. This CSE opportunity is only available
after GVN and InstCombine have run.

//===---------------------------------------------------------------------===//

memcpyopt should turn this:

define i8* @test10(i32 %x) {
  %alloc = call noalias i8* @malloc(i32 %x) nounwind
  call void @llvm.memset.p0i8.i32(i8* %alloc, i8 0, i32 %x, i32 1, i1 false)
  ret i8* %alloc
}

into a call to calloc.  We should make sure that we analyze calloc as
aggressively as malloc though.

//===---------------------------------------------------------------------===//

clang -O3 doesn't optimize this:

void f1(int* begin, int* end) {
  std::fill(begin, end, 0);
}

into a memset.  This is PR8942.

//===---------------------------------------------------------------------===//

clang -O3 -fno-exceptions currently compiles this code:

void f(int N) {
  std::vector<int> v(N);

  extern void sink(void*); sink(&v);
}

into

define void @_Z1fi(i32 %N) nounwind {
entry:
  %v2 = alloca [3 x i32*], align 8
  %v2.sub = getelementptr inbounds [3 x i32*]* %v2, i64 0, i64 0
  %tmpcast = bitcast [3 x i32*]* %v2 to %"class.std::vector"*
  %conv = sext i32 %N to i64
  store i32* null, i32** %v2.sub, align 8, !tbaa !0
  %tmp3.i.i.i.i.i = getelementptr inbounds [3 x i32*]* %v2, i64 0, i64 1
  store i32* null, i32** %tmp3.i.i.i.i.i, align 8, !tbaa !0
  %tmp4.i.i.i.i.i = getelementptr inbounds [3 x i32*]* %v2, i64 0, i64 2
  store i32* null, i32** %tmp4.i.i.i.i.i, align 8, !tbaa !0
  %cmp.i.i.i.i = icmp eq i32 %N, 0
  br i1 %cmp.i.i.i.i, label %_ZNSt12_Vector_baseIiSaIiEEC2EmRKS0_.exit.thread.i.i, label %cond.true.i.i.i.i

_ZNSt12_Vector_baseIiSaIiEEC2EmRKS0_.exit.thread.i.i: ; preds = %entry
  store i32* null, i32** %v2.sub, align 8, !tbaa !0
  store i32* null, i32** %tmp3.i.i.i.i.i, align 8, !tbaa !0
  %add.ptr.i5.i.i = getelementptr inbounds i32* null, i64 %conv
  store i32* %add.ptr.i5.i.i, i32** %tmp4.i.i.i.i.i, align 8, !tbaa !0
  br label %_ZNSt6vectorIiSaIiEEC1EmRKiRKS0_.exit

cond.true.i.i.i.i:                                ; preds = %entry
  %cmp.i.i.i.i.i = icmp slt i32 %N, 0
  br i1 %cmp.i.i.i.i.i, label %if.then.i.i.i.i.i, label %_ZNSt12_Vector_baseIiSaIiEEC2EmRKS0_.exit.i.i

if.then.i.i.i.i.i:                                ; preds = %cond.true.i.i.i.i
  call void @_ZSt17__throw_bad_allocv() noreturn nounwind
  unreachable

_ZNSt12_Vector_baseIiSaIiEEC2EmRKS0_.exit.i.i:    ; preds = %cond.true.i.i.i.i
  %mul.i.i.i.i.i = shl i64 %conv, 2
  %call3.i.i.i.i.i = call noalias i8* @_Znwm(i64 %mul.i.i.i.i.i) nounwind
  %0 = bitcast i8* %call3.i.i.i.i.i to i32*
  store i32* %0, i32** %v2.sub, align 8, !tbaa !0
  store i32* %0, i32** %tmp3.i.i.i.i.i, align 8, !tbaa !0
  %add.ptr.i.i.i = getelementptr inbounds i32* %0, i64 %conv
  store i32* %add.ptr.i.i.i, i32** %tmp4.i.i.i.i.i, align 8, !tbaa !0
  call void @llvm.memset.p0i8.i64(i8* %call3.i.i.i.i.i, i8 0, i64 %mul.i.i.i.i.i, i32 4, i1 false)
  br label %_ZNSt6vectorIiSaIiEEC1EmRKiRKS0_.exit

This is just the handling the construction of the vector. Most surprising here
is the fact that all three null stores in %entry are dead (because we do no
cross-block DSE).

Also surprising is that %conv isn't simplified to 0 in %....exit.thread.i.i.
This is a because the client of LazyValueInfo doesn't simplify all instruction
operands, just selected ones.

//===---------------------------------------------------------------------===//

clang -O3 -fno-exceptions currently compiles this code:

void f(char* a, int n) {
  __builtin_memset(a, 0, n);
  for (int i = 0; i < n; ++i)
    a[i] = 0;
}

into:

define void @_Z1fPci(i8* nocapture %a, i32 %n) nounwind {
entry:
  %conv = sext i32 %n to i64
  tail call void @llvm.memset.p0i8.i64(i8* %a, i8 0, i64 %conv, i32 1, i1 false)
  %cmp8 = icmp sgt i32 %n, 0
  br i1 %cmp8, label %for.body.lr.ph, label %for.end

for.body.lr.ph:                                   ; preds = %entry
  %tmp10 = add i32 %n, -1
  %tmp11 = zext i32 %tmp10 to i64
  %tmp12 = add i64 %tmp11, 1
  call void @llvm.memset.p0i8.i64(i8* %a, i8 0, i64 %tmp12, i32 1, i1 false)
  ret void

for.end:                                          ; preds = %entry
  ret void
}

This shouldn't need the ((zext (%n - 1)) + 1) game, and it should ideally fold
the two memset's together.

The issue with the addition only occurs in 64-bit mode, and appears to be at
least partially caused by Scalar Evolution not keeping its cache updated: it
returns the "wrong" result immediately after indvars runs, but figures out the
expected result if it is run from scratch on IR resulting from running indvars.

//===---------------------------------------------------------------------===//

clang -O3 -fno-exceptions currently compiles this code:

struct S {
  unsigned short m1, m2;
  unsigned char m3, m4;
};

void f(int N) {
  std::vector<S> v(N);
  extern void sink(void*); sink(&v);
}

into poor code for zero-initializing 'v' when N is >0. The problem is that
S is only 6 bytes, but each element is 8 byte-aligned. We generate a loop and
4 stores on each iteration. If the struct were 8 bytes, this gets turned into
a memset.

In order to handle this we have to:
  A) Teach clang to generate metadata for memsets of structs that have holes in
     them.
  B) Teach clang to use such a memset for zero init of this struct (since it has
     a hole), instead of doing elementwise zeroing.

//===---------------------------------------------------------------------===//

clang -O3 currently compiles this code:

extern const int magic;
double f() { return 0.0 * magic; }

into

@magic = external constant i32

define double @_Z1fv() nounwind readnone {
entry:
  %tmp = load i32* @magic, align 4, !tbaa !0
  %conv = sitofp i32 %tmp to double
  %mul = fmul double %conv, 0.000000e+00
  ret double %mul
}

We should be able to fold away this fmul to 0.0.  More generally, fmul(x,0.0)
can be folded to 0.0 if we can prove that the LHS is not -0.0, not a NaN, and
not an INF.  The CannotBeNegativeZero predicate in value tracking should be
extended to support general "fpclassify" operations that can return 
yes/no/unknown for each of these predicates.

In this predicate, we know that uitofp is trivially never NaN or -0.0, and
we know that it isn't +/-Inf if the floating point type has enough exponent bits
to represent the largest integer value as < inf.

//===---------------------------------------------------------------------===//

When optimizing a transformation that can change the sign of 0.0 (such as the
0.0*val -> 0.0 transformation above), it might be provable that the sign of the
expression doesn't matter.  For example, by the above rules, we can't transform
fmul(sitofp(x), 0.0) into 0.0, because x might be -1 and the result of the
expression is defined to be -0.0.

If we look at the uses of the fmul for example, we might be able to prove that
all uses don't care about the sign of zero.  For example, if we have:

  fadd(fmul(sitofp(x), 0.0), 2.0)

Since we know that x+2.0 doesn't care about the sign of any zeros in X, we can
transform the fmul to 0.0, and then the fadd to 2.0.

//===---------------------------------------------------------------------===//

We should enhance memcpy/memcpy/memset to allow a metadata node on them
indicating that some bytes of the transfer are undefined.  This is useful for
frontends like clang when lowering struct copies, when some elements of the
struct are undefined.  Consider something like this:

struct x {
  char a;
  int b[4];
};
void foo(struct x*P);
struct x testfunc() {
  struct x V1, V2;
  foo(&V1);
  V2 = V1;

  return V2;
}

We currently compile this to:
$ clang t.c -S -o - -O0 -emit-llvm | opt -sroa -S


%struct.x = type { i8, [4 x i32] }

define void @testfunc(%struct.x* sret %agg.result) nounwind ssp {
entry:
  %V1 = alloca %struct.x, align 4
  call void @foo(%struct.x* %V1)
  %tmp1 = bitcast %struct.x* %V1 to i8*
  %0 = bitcast %struct.x* %V1 to i160*
  %srcval1 = load i160* %0, align 4
  %tmp2 = bitcast %struct.x* %agg.result to i8*
  %1 = bitcast %struct.x* %agg.result to i160*
  store i160 %srcval1, i160* %1, align 4
  ret void
}

This happens because SRoA sees that the temp alloca has is being memcpy'd into
and out of and it has holes and it has to be conservative.  If we knew about the
holes, then this could be much much better.

Having information about these holes would also improve memcpy (etc) lowering at
llc time when it gets inlined, because we can use smaller transfers.  This also
avoids partial register stalls in some important cases.

//===---------------------------------------------------------------------===//

We don't fold (icmp (add) (add)) unless the two adds only have a single use.
There are a lot of cases that we're refusing to fold in (e.g.) 256.bzip2, for
example:

 %indvar.next90 = add i64 %indvar89, 1     ;; Has 2 uses
 %tmp96 = add i64 %tmp95, 1                ;; Has 1 use
 %exitcond97 = icmp eq i64 %indvar.next90, %tmp96

We don't fold this because we don't want to introduce an overlapped live range
of the ivar.  However if we can make this more aggressive without causing
performance issues in two ways:

1. If *either* the LHS or RHS has a single use, we can definitely do the
   transformation.  In the overlapping liverange case we're trading one register
   use for one fewer operation, which is a reasonable trade.  Before doing this
   we should verify that the llc output actually shrinks for some benchmarks.
2. If both ops have multiple uses, we can still fold it if the operations are
   both sinkable to *after* the icmp (e.g. in a subsequent block) which doesn't
   increase register pressure.

There are a ton of icmp's we aren't simplifying because of the reg pressure
concern.  Care is warranted here though because many of these are induction
variables and other cases that matter a lot to performance, like the above.
Here's a blob of code that you can drop into the bottom of visitICmp to see some
missed cases:

  { Value *A, *B, *C, *D;
    if (match(Op0, m_Add(m_Value(A), m_Value(B))) && 
        match(Op1, m_Add(m_Value(C), m_Value(D))) &&
        (A == C || A == D || B == C || B == D)) {
      errs() << "OP0 = " << *Op0 << "  U=" << Op0->getNumUses() << "\n";
      errs() << "OP1 = " << *Op1 << "  U=" << Op1->getNumUses() << "\n";
      errs() << "CMP = " << I << "\n\n";
    }
  }

//===---------------------------------------------------------------------===//

define i1 @test1(i32 %x) nounwind {
  %and = and i32 %x, 3
  %cmp = icmp ult i32 %and, 2
  ret i1 %cmp
}

Can be folded to (x & 2) == 0.

define i1 @test2(i32 %x) nounwind {
  %and = and i32 %x, 3
  %cmp = icmp ugt i32 %and, 1
  ret i1 %cmp
}

Can be folded to (x & 2) != 0.

SimplifyDemandedBits shrinks the "and" constant to 2 but instcombine misses the
icmp transform.

//===---------------------------------------------------------------------===//

This code:

typedef struct {
int f1:1;
int f2:1;
int f3:1;
int f4:29;
} t1;

typedef struct {
int f1:1;
int f2:1;
int f3:30;
} t2;

t1 s1;
t2 s2;

void func1(void)
{
s1.f1 = s2.f1;
s1.f2 = s2.f2;
}

Compiles into this IR (on x86-64 at least):

%struct.t1 = type { i8, [3 x i8] }
@s2 = global %struct.t1 zeroinitializer, align 4
@s1 = global %struct.t1 zeroinitializer, align 4
define void @func1() nounwind ssp noredzone {
entry:
  %0 = load i32* bitcast (%struct.t1* @s2 to i32*), align 4
  %bf.val.sext5 = and i32 %0, 1
  %1 = load i32* bitcast (%struct.t1* @s1 to i32*), align 4
  %2 = and i32 %1, -4
  %3 = or i32 %2, %bf.val.sext5
  %bf.val.sext26 = and i32 %0, 2
  %4 = or i32 %3, %bf.val.sext26
  store i32 %4, i32* bitcast (%struct.t1* @s1 to i32*), align 4
  ret void
}

The two or/and's should be merged into one each.

//===---------------------------------------------------------------------===//

Machine level code hoisting can be useful in some cases.  For example, PR9408
is about:

typedef union {
 void (*f1)(int);
 void (*f2)(long);
} funcs;

void foo(funcs f, int which) {
 int a = 5;
 if (which) {
   f.f1(a);
 } else {
   f.f2(a);
 }
}

which we compile to:

foo:                                    # @foo
# %bb.0:                                # %entry
       pushq   %rbp
       movq    %rsp, %rbp
       testl   %esi, %esi
       movq    %rdi, %rax
       je      .LBB0_2
# %bb.1:                                # %if.then
       movl    $5, %edi
       callq   *%rax
       popq    %rbp
       ret
.LBB0_2:                                # %if.else
       movl    $5, %edi
       callq   *%rax
       popq    %rbp
       ret

Note that bb1 and bb2 are the same.  This doesn't happen at the IR level
because one call is passing an i32 and the other is passing an i64.

//===---------------------------------------------------------------------===//

I see this sort of pattern in 176.gcc in a few places (e.g. the start of
store_bit_field).  The rem should be replaced with a multiply and subtract:

  %3 = sdiv i32 %A, %B
  %4 = srem i32 %A, %B

Similarly for udiv/urem.  Note that this shouldn't be done on X86 or ARM,
which can do this in a single operation (instruction or libcall).  It is
probably best to do this in the code generator.

//===---------------------------------------------------------------------===//

unsigned foo(unsigned x, unsigned y) { return (x & y) == 0 || x == 0; }
should fold to (x & y) == 0.

//===---------------------------------------------------------------------===//

unsigned foo(unsigned x, unsigned y) { return x > y && x != 0; }
should fold to x > y.

//===---------------------------------------------------------------------===//
