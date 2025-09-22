/*	$OpenBSD: impyu.S,v 1.12 2011/04/16 20:52:12 deraadt Exp $	*/
/*
  (c) Copyright 1986 HEWLETT-PACKARD COMPANY
  To anyone who acknowledges that this file is provided "AS IS"
  without any express or implied warranty:
      permission to use, copy, modify, and distribute this file
  for any purpose is hereby granted without fee, provided that
  the above copyright notice and this notice appears in all
  copies, and that the name of Hewlett-Packard Company not be
  used in advertising or publicity pertaining to distribution
  of the software without specific, written prior permission.
  Hewlett-Packard Company makes no representations about the
  suitability of this software for any purpose.
*/
/* @(#)impyu.s: Revision: 1.11.88.1 Date: 93/12/07 15:06:31 */

#include <machine/asm.h>
#include <machine/frame.h>

;****************************************************************************
;
;Implement an integer multiply routine for 32-bit operands and 64-bit product
; with operand values of zero (multiplicand only) and 2**32reated specially.
; The algorithm uses the multiplier, four bits at a time, from right to left,
; to generate partial product.  Execution speed is more important than program
; size in this implementation.
;
;******************************************************************************
;
; Definitions - General registers
;
gr0	.reg		%r0		; General register zero
pu	.reg		%r3		; upper part of product
pl	.reg		%r4		; lower part of product
op2	.reg		%r4		; multiplier
op1	.reg		%r5		; multiplicand
cnt	.reg		%r6		; count in multiply
brindex	.reg		%r7		; index into the br. table
saveop2	.reg		%r8		; save op2 if high bit of multiplicand
					; is set
pc	.reg		%r9		; carry bit of product, = 00...01
pm	.reg		%r10		; value of -1 used in shifting
temp	.reg		%r6

;****************************************************************************
	.text
LEAF_ENTRY(u_xmpy)
	stws,ma		pu,4(sp)		; save registers on stack
	stws,ma		pl,4(sp)		; save registers on stack
	stws,ma		op1,4(sp)		; save registers on stack
	stws,ma		cnt,4(sp)		; save registers on stack
	stws,ma		brindex,4(sp)		; save registers on stack
	stws,ma		saveop2,4(sp)		; save registers on stack
	stws,ma		pc,4(sp)		; save registers on stack
	stws,ma		pm,4(sp)		; save registers on stack
;
;   Start multiply process
;
	ldws		0(arg0),op1		; get multiplicand
	ldws		0(arg1),op2		; get multiplier
	addib,=		0,op1,fini0		; op1 = 0, product = 0
	addi		0,gr0,pu		; clear product
	bb,>=		op1,0,mpy1		; test msb of multiplicand
	addi		0,gr0,saveop2		; clear saveop2
;
; msb of multiplicand is set so will save multiplier for a final
; addition into the result
;
	extru,=		op1,31,31,op1		; clear msb of multiplicand
	b		mpy1			; if op1 < 2**32, start multiply
	add		op2,gr0,saveop2		;   save op2 in saveop2
	shd		gr0,op2,1,pu		; shift op2 left 31 for result
	b		fini			; go to finish
	shd		op2,gr0,1,pl
;
mpy1	addi		-1,gr0,pm		; initialize pm to 111...1
	addi		1,gr0,pc		; initialize pc to 00...01
	movib,tr	8,cnt,mloop		; set count for mpy loop
	extru		op2,31,4,brindex	; 4 bits as index into table
;
	.align		8
;
	b		sh4c			; br. if sign overflow
sh4n	shd		pu,pl,4,pl		; shift product right 4 bits
	addib,<=	-1,cnt,mulend		; reduce count by 1, exit if
	extru		pu,27,28,pu		;   <= zero
;
mloop	blr		brindex,gr0		; br. into table
						;   entries of 2 words
	extru		op2,27,4,brindex	; next 4 bits into index
;
;
;	branch table for the multiplication process with four multiplier bits
;
mtable						; two words per entry
;
; ----	bits = 0000 ---- shift product 4 bits -------------------------------
;
	b		sh4n+4			; just shift partial
	shd		pu,pl,4,pl		;   product right 4 bits
;
;  ----	bits = 0001 ---- add op1, then shift 4 bits
;
	addb,tr		op1,pu,sh4n+4		; add op1 to product, to shift
	shd		pu,pl,4,pl		;   product right 4 bits
;
;  ----	bits = 0010 ---- add op1, add op1, then shift 4 bits
;
	addb,tr		op1,pu,sh4n		; add 2*op1, to shift
	addb,uv		op1,pu,sh4c		;   product right 4 bits
;
;  ---- bits = 0011 ---- add op1, add 2*op1, shift 4 bits
;
	addb,tr		op1,pu,sh4n-4		; add op1 & 2*op1, shift
	sh1add,nuv	op1,pu,pu			;   product right 4 bits
;
;  ----	bits = 0100 ---- shift 2, add op1, shift 2
;
	b		sh2sa
	shd		pu,pl,2,pl		; shift product 2 bits
;
;  ----	bits = 0101 ---- add op1, shift 2, add op1, and shift 2 again
;
	addb,tr		op1,pu,sh2us		; add op1 to product
	shd		pu,pl,2,pl		; shift 2 bits
;
;  ----	bits = 0110 ---- add op1, add op1, shift 2, add op1, and shift 2 again
;
	addb,tr		op1,pu,sh2c		; add 2*op1, to shift 2 bits
	addb,nuv	op1,pu,sh2us		; br. if not overflow
;
;  ----	bits = 0111 ---- subtract op1, shift 3, add op1, and shift 1
;
	b		sh3s
	sub		pu,op1,pu		; subtract op1, br. to sh3s

;
;  ----	bits = 1000 ---- shift 3, add op1, shift 1
;
	b		sh3sa
	shd		pu,pl,3,pl		; shift product right 3 bits
;
;  ----	bits = 1001 ---- add op1, shift 3, add op1, shift 1
;
	addb,tr		op1,pu,sh3us		; add op1, to shift 3, add op1,
	shd		pu,pl,3,pl		;   and shift 1
;
;  ----	bits = 1010 ---- add op1, add op1, shift 3, add op1, shift 1
;
	addb,tr		op1,pu,sh3c		; add 2*op1, to shift 3 bits
	addb,nuv	op1,pu,sh3us		;   br. if no overflow
;
;  ----	bits = 1011 ---- add -op1, shift 2, add -op1, shift 2, inc. next index
;
	addib,tr	1,brindex,sh2s		; add 1 to index, subtract op1,
	sub		pu,op1,pu		;   shift 2 with minus sign
;
;  ----	bits = 1100 ---- shift 2, subtract op1, shift 2, increment next index
;
	addib,tr	1,brindex,sh2sb		; add 1 to index, to shift
	shd		pu,pl,2,pl		; shift right 2 bits signed
;
;  ----	bits = 1101 ---- add op1, shift 2, add -op1, shift 2
;
	addb,tr		op1,pu,sh2ns		; add op1, to shift 2
	shd		pu,pl,2,pl		;   right 2 unsigned, etc.
;
;  ----	bits = 1110 ---- shift 1 signed, add -op1, shift 3 signed
;
	addib,tr	1,brindex,sh1sa		; add 1 to index, to shift
	shd		pu,pl,1,pl		; shift 1 bit
;
;  ----	bits = 1111 ---- add -op1, shift 4 signed
;
	addib,tr	1,brindex,sh4s		; add 1 to index, subtract op1,
	sub		pu,op1,pu		;   to shift 4 signed

;
;  ----	bits = 10000 ---- shift 4 signed
;
	addib,tr	1,brindex,sh4s+4		; add 1 to index
	shd		pu,pl,4,pl		; shift 4 signed
;
;  ---- end of table ---------------------------------------------------------
;
sh4s	shd		pu,pl,4,pl
	addib,>		-1,cnt,mloop		; decrement count, loop if > 0
	shd		pm,pu,4,pu		; shift 4, minus signed
	addb,tr		op1,pu,lastadd		; do one more add, then finish
	addb,=,n	saveop2,gr0,fini	; check saveop2
;
sh4c	addib,>		-1,cnt,mloop		; decrement count, loop if > 0
	shd		pc,pu,4,pu		; shift 4 with overflow
	b		lastadd			; end of multiply
	addb,=,n	saveop2,gr0,fini	; check saveop2
;
sh3c	shd		pu,pl,3,pl		; shift product 3 bits
	shd		pc,pu,3,pu		; shift 3 signed
	addb,tr		op1,pu,sh1		; add op1, to shift 1 bit
	shd		pu,pl,1,pl
;
sh3us	extru		pu,28,29,pu		; shift 3 unsigned
	addb,tr		op1,pu,sh1		; add op1, to shift 1 bit
	shd		pu,pl,1,pl
;
sh3sa	extrs		pu,28,29,pu		; shift 3 signed
	addb,tr		op1,pu,sh1		; add op1, to shift 1 bit
	shd		pu,pl,1,pl
;
sh3s	shd		pu,pl,3,pl		; shift 3 minus signed
	shd		pm,pu,3,pu
	addb,tr		op1,pu,sh1		; add op1, to shift 1 bit
	shd		pu,pl,1,pl
;
sh1	addib,>		-1,cnt,mloop		; loop if count > 0
	extru		pu,30,31,pu
	b		lastadd			; end of multiply
	addb,=,n	saveop2,gr0,fini	; check saveop2
;
sh2ns	addib,tr	1,brindex,sh2sb+4	; increment index
	extru		pu,29,30,pu		; shift unsigned
;
sh2s	shd		pu,pl,2,pl		; shift with minus sign
	shd		pm,pu,2,pu		;
	sub		pu,op1,pu		; subtract op1
	shd		pu,pl,2,pl		; shift with minus sign
	addib,>		-1,cnt,mloop		; decrement count, loop if > 0
	shd		pm,pu,2,pu		; shift with minus sign
	addb,tr		op1,pu,lastadd		; do one more add, then finish
	addb,=,n	saveop2,gr0,fini	; check saveop2
;
sh2sb	extrs		pu,29,30,pu		; shift 2 signed
	sub		pu,op1,pu		; subtract op1 from product
	shd		pu,pl,2,pl		; shift with minus sign
	addib,>		-1,cnt,mloop		; decrement count, loop if > 0
	shd		pm,pu,2,pu		; shift with minus sign
	addb,tr		op1,pu,lastadd		; do one more add, then finish
	addb,=,n	saveop2,gr0,fini	; check saveop2
;
sh1sa	extrs		pu,30,31,pu		;   signed
	sub		pu,op1,pu		; subtract op1 from product
	shd		pu,pl,3,pl		; shift 3 with minus sign
	addib,>		-1,cnt,mloop		; decrement count, loop if >0
	shd		pm,pu,3,pu
	addb,tr		op1,pu,lastadd		; do one more add, then finish
	addb,=,n	saveop2,gr0,fini	; check saveop2
;
fini0	movib,tr	0,pl,fini		; product = 0 as op1 = 0
	stws		pu,0(arg2)		; save high part of result
;
sh2us	extru		pu,29,30,pu		; shift 2 unsigned
	addb,tr		op1,pu,sh2a		; add op1
	shd		pu,pl,2,pl		; shift 2 bits
;
sh2c	shd		pu,pl,2,pl
	shd		pc,pu,2,pu		; shift with carry
	addb,tr		op1,pu,sh2a		; add op1 to product
	shd		pu,pl,2,pl		; br. to sh2 to shift pu
;
sh2sa	extrs		pu,29,30,pu		; shift with sign
	addb,tr		op1,pu,sh2a		; add op1 to product
	shd		pu,pl,2,pl		; br. to sh2 to shift pu
;
sh2a	addib,>		-1,cnt,mloop		; loop if count > 0
	extru		pu,29,30,pu
;
mulend	addb,=,n	saveop2,gr0,fini	; check saveop2
lastadd	shd		saveop2,gr0,1,temp	;  if saveop2 <> 0, shift it
	shd		gr0,saveop2,1,saveop2	;  left 31 and add to result
	add		pl,temp,pl
	addc		pu,saveop2,pu
;
;	finish
;
fini	stws		pu,0(arg2)		; save high part of result
	stws		pl,4(arg2)		; save low part of result

	ldws,mb		-4(sp),pm		; restore registers
	ldws,mb		-4(sp),pc		; restore registers
	ldws,mb		-4(sp),saveop2		; restore registers
	ldws,mb		-4(sp),brindex		; restore registers
	ldws,mb		-4(sp),cnt		; restore registers
	ldws,mb		-4(sp),op1		; restore registers
	ldws,mb		-4(sp),pl		; restore registers
	bv		0(rp)			; return
	ldws,mb		-4(sp),pu		; restore registers
EXIT(u_xmpy)

	.end
