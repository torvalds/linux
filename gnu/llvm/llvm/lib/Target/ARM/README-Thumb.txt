//===---------------------------------------------------------------------===//
// Random ideas for the ARM backend (Thumb specific).
//===---------------------------------------------------------------------===//

* Add support for compiling functions in both ARM and Thumb mode, then taking
  the smallest.

* Add support for compiling individual basic blocks in thumb mode, when in a 
  larger ARM function.  This can be used for presumed cold code, like paths
  to abort (failure path of asserts), EH handling code, etc.

* Thumb doesn't have normal pre/post increment addressing modes, but you can
  load/store 32-bit integers with pre/postinc by using load/store multiple
  instrs with a single register.

* Make better use of high registers r8, r10, r11, r12 (ip). Some variants of add
  and cmp instructions can use high registers. Also, we can use them as
  temporaries to spill values into.

* In thumb mode, short, byte, and bool preferred alignments are currently set
  to 4 to accommodate ISA restriction (i.e. add sp, #imm, imm must be multiple
  of 4).

//===---------------------------------------------------------------------===//

Potential jumptable improvements:

* If we know function size is less than (1 << 16) * 2 bytes, we can use 16-bit
  jumptable entries (e.g. (L1 - L2) >> 1). Or even smaller entries if the
  function is even smaller. This also applies to ARM.

* Thumb jumptable codegen can improve given some help from the assembler. This
  is what we generate right now:

	.set PCRELV0, (LJTI1_0_0-(LPCRELL0+4))
LPCRELL0:
	mov r1, #PCRELV0
	add r1, pc
	ldr r0, [r0, r1]
	mov pc, r0 
	.align	2
LJTI1_0_0:
	.long	 LBB1_3
        ...

Note there is another pc relative add that we can take advantage of.
     add r1, pc, #imm_8 * 4

We should be able to generate:

LPCRELL0:
	add r1, LJTI1_0_0
	ldr r0, [r0, r1]
	mov pc, r0 
	.align	2
LJTI1_0_0:
	.long	 LBB1_3

if the assembler can translate the add to:
       add r1, pc, #((LJTI1_0_0-(LPCRELL0+4))&0xfffffffc)

Note the assembler also does something similar to constpool load:
LPCRELL0:
     ldr r0, LCPI1_0
=>
     ldr r0, pc, #((LCPI1_0-(LPCRELL0+4))&0xfffffffc)


//===---------------------------------------------------------------------===//

We compile the following:

define i16 @func_entry_2E_ce(i32 %i) {
        switch i32 %i, label %bb12.exitStub [
                 i32 0, label %bb4.exitStub
                 i32 1, label %bb9.exitStub
                 i32 2, label %bb4.exitStub
                 i32 3, label %bb4.exitStub
                 i32 7, label %bb9.exitStub
                 i32 8, label %bb.exitStub
                 i32 9, label %bb9.exitStub
        ]

bb12.exitStub:
        ret i16 0

bb4.exitStub:
        ret i16 1

bb9.exitStub:
        ret i16 2

bb.exitStub:
        ret i16 3
}

into:

_func_entry_2E_ce:
        mov r2, #1
        lsl r2, r0
        cmp r0, #9
        bhi LBB1_4      @bb12.exitStub
LBB1_1: @newFuncRoot
        mov r1, #13
        tst r2, r1
        bne LBB1_5      @bb4.exitStub
LBB1_2: @newFuncRoot
        ldr r1, LCPI1_0
        tst r2, r1
        bne LBB1_6      @bb9.exitStub
LBB1_3: @newFuncRoot
        mov r1, #1
        lsl r1, r1, #8
        tst r2, r1
        bne LBB1_7      @bb.exitStub
LBB1_4: @bb12.exitStub
        mov r0, #0
        bx lr
LBB1_5: @bb4.exitStub
        mov r0, #1
        bx lr
LBB1_6: @bb9.exitStub
        mov r0, #2
        bx lr
LBB1_7: @bb.exitStub
        mov r0, #3
        bx lr
LBB1_8:
        .align  2
LCPI1_0:
        .long   642


gcc compiles to:

	cmp	r0, #9
	@ lr needed for prologue
	bhi	L2
	ldr	r3, L11
	mov	r2, #1
	mov	r1, r2, asl r0
	ands	r0, r3, r2, asl r0
	movne	r0, #2
	bxne	lr
	tst	r1, #13
	beq	L9
L3:
	mov	r0, r2
	bx	lr
L9:
	tst	r1, #256
	movne	r0, #3
	bxne	lr
L2:
	mov	r0, #0
	bx	lr
L12:
	.align 2
L11:
	.long	642
        

GCC is doing a couple of clever things here:
  1. It is predicating one of the returns.  This isn't a clear win though: in
     cases where that return isn't taken, it is replacing one condbranch with
     two 'ne' predicated instructions.
  2. It is sinking the shift of "1 << i" into the tst, and using ands instead of
     tst.  This will probably require whole function isel.
  3. GCC emits:
  	tst	r1, #256
     we emit:
        mov r1, #1
        lsl r1, r1, #8
        tst r2, r1

//===---------------------------------------------------------------------===//

When spilling in thumb mode and the sp offset is too large to fit in the ldr /
str offset field, we load the offset from a constpool entry and add it to sp:

ldr r2, LCPI
add r2, sp
ldr r2, [r2]

These instructions preserve the condition code which is important if the spill
is between a cmp and a bcc instruction. However, we can use the (potentially)
cheaper sequence if we know it's ok to clobber the condition register.

add r2, sp, #255 * 4
add r2, #132
ldr r2, [r2, #7 * 4]

This is especially bad when dynamic alloca is used. The all fixed size stack
objects are referenced off the frame pointer with negative offsets. See
oggenc for an example.

//===---------------------------------------------------------------------===//

Poor codegen test/CodeGen/ARM/select.ll f7:

	ldr r5, LCPI1_0
LPC0:
	add r5, pc
	ldr r6, LCPI1_1
	ldr r2, LCPI1_2
	mov r3, r6
	mov lr, pc
	bx r5

//===---------------------------------------------------------------------===//

Make register allocator / spiller smarter so we can re-materialize "mov r, imm",
etc. Almost all Thumb instructions clobber condition code.

//===---------------------------------------------------------------------===//

Thumb load / store address mode offsets are scaled. The values kept in the
instruction operands are pre-scale values. This probably ought to be changed
to avoid extra work when we convert Thumb2 instructions to Thumb1 instructions.

//===---------------------------------------------------------------------===//

We need to make (some of the) Thumb1 instructions predicable. That will allow
shrinking of predicated Thumb2 instructions. To allow this, we need to be able
to toggle the 's' bit since they do not set CPSR when they are inside IT blocks.

//===---------------------------------------------------------------------===//

Make use of hi register variants of cmp: tCMPhir / tCMPZhir.

//===---------------------------------------------------------------------===//

Thumb1 immediate field sometimes keep pre-scaled values. See
ThumbRegisterInfo::eliminateFrameIndex. This is inconsistent from ARM and
Thumb2.

//===---------------------------------------------------------------------===//

Rather than having tBR_JTr print a ".align 2" and constant island pass pad it,
add a target specific ALIGN instruction instead. That way, getInstSizeInBytes
won't have to over-estimate. It can also be used for loop alignment pass.

//===---------------------------------------------------------------------===//

We generate conditional code for icmp when we don't need to. This code:

  int foo(int s) {
    return s == 1;
  }

produces:

foo:
        cmp     r0, #1
        mov.w   r0, #0
        it      eq
        moveq   r0, #1
        bx      lr

when it could use subs + adcs. This is GCC PR46975.
