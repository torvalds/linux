/*	$OpenBSD: divu.S,v 1.11 2011/04/16 20:52:12 deraadt Exp $	*/
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
/* @(#)divu.s: Revision: 1.11.88.1 Date: 93/12/07 15:06:01 */

#include <machine/asm.h>
#include <machine/frame.h>

;************************************************************************
; Implement an integer divide routine for 32-bit operands and 32-bit quotient
; and remainder with operand values of zero (divisor only) treated specially.
;
;****************************************************************************
; Definitions
;****************************************************************************
;
;	General registers
;
gr0	.reg		%r0		; General register zero
rem	.reg		%r3		; remainder and upper part of dividend
quo	.reg		%r4		; quotient and lower part of dividend
dvr	.reg		%r5		; divisor
tp	.reg		%r6		; temp. reg.

;******************************************************************************
	.text
LEAF_ENTRY(divu)
	stws,ma		rem,4(sp)		; save registers on stack
	stws,ma		quo,4(sp)		; save registers on stack
	stws,ma		dvr,4(sp)		; save registers on stack
	stws,ma		tp,4(sp)		; save registers on stack

	addi		0,arg2,dvr		; get divisor
	addi		0,arg1,quo		; get lower dividend
	addi		0,arg0,rem		; get upper dividend

	comib,>,n	0,dvr,hibit		; check for dvr >= 2**31
	addi		-1,gr0,tp		; set V-bit to 1
	ds		0,tp,0
	add		quo,quo,quo		; shift msb bit into carry
	ds		rem,dvr,rem		; 1st divide step, if carry
						;   out, msb of quotient = 0
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 2nd divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 3rd divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 4th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 5th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 6th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 7th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 8th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 9th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 10th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 11th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 12th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 13th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 14th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 15th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 16th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 17th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 18th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 19th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 20th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 21st divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 22nd divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 23rd divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 24th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 25th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 26th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 27th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 28th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 29th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 30th divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 31st divide step
	addc		quo,quo,quo		; shift quo with/into carry
	ds		rem,dvr,rem		; 32nd divide step,
	addc		quo,quo,quo		; shift last quo bit into quo
	addb,>=,n	rem,0,finish		; branch if pos. rem
	add,<		dvr,0,0			; if dvr > 0, add dvr
	add,tr		rem,dvr,rem		;   for correcting rem.
	sub		rem,dvr,rem		; else subtract dvr
;
;	end of divide routine
;
finish	stws		rem,0(arg3)		; save remainder in high part
						;   of result
	stws		quo,4(arg3)		; save quotient in low part
						;   of result
	ldws,mb		-4(sp),tp		; restore registers
	ldws,mb		-4(sp),dvr		; restore registers
	ldws,mb		-4(sp),quo		; restore registers
	bv		0(rp)			; return
	ldws,mb		-4(sp),rem		; restore registers
;
hibit	ldo		32(0),tp		; initialize loop counter
	add		quo,quo,quo		; shift high bit into carry
loop	addc		rem,rem,rem		; shift in high bit of dvdl
	addc,<>		0,0,0			; if bit shifted out of dvdu,
						;   want to do subtract
	comb,<<,n	rem,dvr,nosub		; if upper dividend > dvr,
	sub		rem,dvr,rem		;   subtract and
	add,tr		dvr,dvr,0		;   set carry
nosub	addi		0,0,0			; otherwise clear carry
	addib,>		-1,tp,loop		; inc. counter; finished?
	addc		quo,quo,quo		; shift bit of result into dvdl
	b		finish+4		; finish up
	stws		rem,0(arg3)		; save remainder in high part
						;   of result
EXIT(divu)

	.end
