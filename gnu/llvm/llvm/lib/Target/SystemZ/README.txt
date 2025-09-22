//===---------------------------------------------------------------------===//
// Random notes about and ideas for the SystemZ backend.
//===---------------------------------------------------------------------===//

The initial backend is deliberately restricted to z10.  We should add support
for later architectures at some point.

--

If an inline asm ties an i32 "r" result to an i64 input, the input
will be treated as an i32, leaving the upper bits uninitialised.
For example:

define void @f4(i32 *%dst) {
  %val = call i32 asm "blah $0", "=r,0" (i64 103)
  store i32 %val, i32 *%dst
  ret void
}

from CodeGen/SystemZ/asm-09.ll will use LHI rather than LGHI.
to load 103.  This seems to be a general target-independent problem.

--

The tuning of the choice between LOAD ADDRESS (LA) and addition in
SystemZISelDAGToDAG.cpp is suspect.  It should be tweaked based on
performance measurements.

--

There is no scheduling support.

--

We don't use the BRANCH ON INDEX instructions.

--

We only use MVC, XC and CLC for constant-length block operations.
We could extend them to variable-length operations too,
using EXECUTE RELATIVE LONG.

MVCIN, MVCLE and CLCLE may be worthwhile too.

--

We don't use CUSE or the TRANSLATE family of instructions for string
operations.  The TRANSLATE ones are probably more difficult to exploit.

--

We don't take full advantage of builtins like fabsl because the calling
conventions require f128s to be returned by invisible reference.

--

ADD LOGICAL WITH SIGNED IMMEDIATE could be useful when we need to
produce a carry.  SUBTRACT LOGICAL IMMEDIATE could be useful when we
need to produce a borrow.  (Note that there are no memory forms of
ADD LOGICAL WITH CARRY and SUBTRACT LOGICAL WITH BORROW, so the high
part of 128-bit memory operations would probably need to be done
via a register.)

--

We don't use ICM, STCM, or CLM.

--

We don't use ADD (LOGICAL) HIGH, SUBTRACT (LOGICAL) HIGH,
or COMPARE (LOGICAL) HIGH yet.

--

DAGCombiner doesn't yet fold truncations of extended loads.  Functions like:

    unsigned long f (unsigned long x, unsigned short *y)
    {
      return (x << 32) | *y;
    }

therefore end up as:

        sllg    %r2, %r2, 32
        llgh    %r0, 0(%r3)
        lr      %r2, %r0
        br      %r14

but truncating the load would give:

        sllg    %r2, %r2, 32
        lh      %r2, 0(%r3)
        br      %r14

--

Functions like:

define i64 @f1(i64 %a) {
  %and = and i64 %a, 1
  ret i64 %and
}

ought to be implemented as:

        lhi     %r0, 1
        ngr     %r2, %r0
        br      %r14

but two-address optimizations reverse the order of the AND and force:

        lhi     %r0, 1
        ngr     %r0, %r2
        lgr     %r2, %r0
        br      %r14

CodeGen/SystemZ/and-04.ll has several examples of this.

--

Out-of-range displacements are usually handled by loading the full
address into a register.  In many cases it would be better to create
an anchor point instead.  E.g. for:

define void @f4a(i128 *%aptr, i64 %base) {
  %addr = add i64 %base, 524288
  %bptr = inttoptr i64 %addr to i128 *
  %a = load volatile i128 *%aptr
  %b = load i128 *%bptr
  %add = add i128 %a, %b
  store i128 %add, i128 *%aptr
  ret void
}

(from CodeGen/SystemZ/int-add-08.ll) we load %base+524288 and %base+524296
into separate registers, rather than using %base+524288 as a base for both.

--

Dynamic stack allocations round the size to 8 bytes and then allocate
that rounded amount.  It would be simpler to subtract the unrounded
size from the copy of the stack pointer and then align the result.
See CodeGen/SystemZ/alloca-01.ll for an example.

--

If needed, we can support 16-byte atomics using LPQ, STPQ and CSDG.

--

We might want to model all access registers and use them to spill
32-bit values.

--

We might want to use the 'overflow' condition of eg. AR to support
llvm.sadd.with.overflow.i32 and related instructions - the generated code
for signed overflow check is currently quite bad.  This would improve
the results of using -ftrapv.
