/* 32 and 64-bit millicode, original author Hewlett-Packard
   adapted for gcc by Paul Bame <bame@debian.org>
   and Alan Modra <alan@linuxcare.com.au>.

   Copyright 2001, 2002, 2003, 2007 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifdef pa64
        .level  2.0w
#endif

/* Hardware General Registers.  */
r0:	.reg	%r0
r1:	.reg	%r1
r2:	.reg	%r2
r3:	.reg	%r3
r4:	.reg	%r4
r5:	.reg	%r5
r6:	.reg	%r6
r7:	.reg	%r7
r8:	.reg	%r8
r9:	.reg	%r9
r10:	.reg	%r10
r11:	.reg	%r11
r12:	.reg	%r12
r13:	.reg	%r13
r14:	.reg	%r14
r15:	.reg	%r15
r16:	.reg	%r16
r17:	.reg	%r17
r18:	.reg	%r18
r19:	.reg	%r19
r20:	.reg	%r20
r21:	.reg	%r21
r22:	.reg	%r22
r23:	.reg	%r23
r24:	.reg	%r24
r25:	.reg	%r25
r26:	.reg	%r26
r27:	.reg	%r27
r28:	.reg	%r28
r29:	.reg	%r29
r30:	.reg	%r30
r31:	.reg	%r31

/* Hardware Space Registers.  */
sr0:	.reg	%sr0
sr1:	.reg	%sr1
sr2:	.reg	%sr2
sr3:	.reg	%sr3
sr4:	.reg	%sr4
sr5:	.reg	%sr5
sr6:	.reg	%sr6
sr7:	.reg	%sr7

/* Hardware Floating Point Registers.  */
fr0:	.reg	%fr0
fr1:	.reg	%fr1
fr2:	.reg	%fr2
fr3:	.reg	%fr3
fr4:	.reg	%fr4
fr5:	.reg	%fr5
fr6:	.reg	%fr6
fr7:	.reg	%fr7
fr8:	.reg	%fr8
fr9:	.reg	%fr9
fr10:	.reg	%fr10
fr11:	.reg	%fr11
fr12:	.reg	%fr12
fr13:	.reg	%fr13
fr14:	.reg	%fr14
fr15:	.reg	%fr15

/* Hardware Control Registers.  */
cr11:	.reg	%cr11
sar:	.reg	%cr11	/* Shift Amount Register */

/* Software Architecture General Registers.  */
rp:	.reg    r2	/* return pointer */
#ifdef pa64
mrp:	.reg	r2 	/* millicode return pointer */
#else
mrp:	.reg	r31	/* millicode return pointer */
#endif
ret0:	.reg    r28	/* return value */
ret1:	.reg    r29	/* return value (high part of double) */
sp:	.reg 	r30	/* stack pointer */
dp:	.reg	r27	/* data pointer */
arg0:	.reg	r26	/* argument */
arg1:	.reg	r25	/* argument or high part of double argument */
arg2:	.reg	r24	/* argument */
arg3:	.reg	r23	/* argument or high part of double argument */

/* Software Architecture Space Registers.  */
/* 		sr0	; return link from BLE */
sret:	.reg	sr1	/* return value */
sarg:	.reg	sr1	/* argument */
/* 		sr4	; PC SPACE tracker */
/* 		sr5	; process private data */

/* Frame Offsets (millicode convention!)  Used when calling other
   millicode routines.  Stack unwinding is dependent upon these
   definitions.  */
r31_slot:	.equ	-20	/* "current RP" slot */
sr0_slot:	.equ	-16     /* "static link" slot */
#if defined(pa64)
mrp_slot:       .equ    -16	/* "current RP" slot */
psp_slot:       .equ    -8	/* "previous SP" slot */
#else
mrp_slot:	.equ	-20     /* "current RP" slot (replacing "r31_slot") */
#endif


#define DEFINE(name,value)name:	.EQU	value
#define RDEFINE(name,value)name:	.REG	value
#ifdef milliext
#define MILLI_BE(lbl)   BE    lbl(sr7,r0)
#define MILLI_BEN(lbl)  BE,n  lbl(sr7,r0)
#define MILLI_BLE(lbl)	BLE   lbl(sr7,r0)
#define MILLI_BLEN(lbl)	BLE,n lbl(sr7,r0)
#define MILLIRETN	BE,n  0(sr0,mrp)
#define MILLIRET	BE    0(sr0,mrp)
#define MILLI_RETN	BE,n  0(sr0,mrp)
#define MILLI_RET	BE    0(sr0,mrp)
#else
#define MILLI_BE(lbl)	B     lbl
#define MILLI_BEN(lbl)  B,n   lbl
#define MILLI_BLE(lbl)	BL    lbl,mrp
#define MILLI_BLEN(lbl)	BL,n  lbl,mrp
#define MILLIRETN	BV,n  0(mrp)
#define MILLIRET	BV    0(mrp)
#define MILLI_RETN	BV,n  0(mrp)
#define MILLI_RET	BV    0(mrp)
#endif

#ifdef __STDC__
#define CAT(a,b)	a##b
#else
#define CAT(a,b)	a/**/b
#endif

#ifdef ELF
#define SUBSPA_MILLI	 .section .text
#define SUBSPA_MILLI_DIV .section .text.div,"ax",@progbits! .align 16
#define SUBSPA_MILLI_MUL .section .text.mul,"ax",@progbits! .align 16
#define ATTR_MILLI
#define SUBSPA_DATA	 .section .data
#define ATTR_DATA
#define GLOBAL		 $global$
#define GSYM(sym) 	 !sym:
#define LSYM(sym)	 !CAT(.L,sym:)
#define LREF(sym)	 CAT(.L,sym)

#else

#ifdef coff
/* This used to be .milli but since link32 places different named
   sections in different segments millicode ends up a long ways away
   from .text (1meg?).  This way they will be a lot closer.

   The SUBSPA_MILLI_* specify locality sets for certain millicode
   modules in order to ensure that modules that call one another are
   placed close together. Without locality sets this is unlikely to
   happen because of the Dynamite linker library search algorithm. We
   want these modules close together so that short calls always reach
   (we don't want to require long calls or use long call stubs).  */

#define SUBSPA_MILLI	 .subspa .text
#define SUBSPA_MILLI_DIV .subspa .text$dv,align=16
#define SUBSPA_MILLI_MUL .subspa .text$mu,align=16
#define ATTR_MILLI	 .attr code,read,execute
#define SUBSPA_DATA	 .subspa .data
#define ATTR_DATA	 .attr init_data,read,write
#define GLOBAL		 _gp
#else
#define SUBSPA_MILLI	 .subspa $MILLICODE$,QUAD=0,ALIGN=4,ACCESS=0x2c,SORT=8
#define SUBSPA_MILLI_DIV SUBSPA_MILLI
#define SUBSPA_MILLI_MUL SUBSPA_MILLI
#define ATTR_MILLI
#define SUBSPA_DATA	 .subspa $BSS$,quad=1,align=8,access=0x1f,sort=80,zero
#define ATTR_DATA
#define GLOBAL		 $global$
#endif
#define SPACE_DATA	 .space $PRIVATE$,spnum=1,sort=16

#define GSYM(sym)	 !sym
#define LSYM(sym)	 !CAT(L$,sym)
#define LREF(sym)	 CAT(L$,sym)
#endif

#ifdef L_dyncall
	SUBSPA_MILLI
	ATTR_DATA
GSYM($$dyncall)
	.export $$dyncall,millicode
	.proc
	.callinfo	millicode
	.entry
	bb,>=,n %r22,30,LREF(1)		; branch if not plabel address
	depi	0,31,2,%r22		; clear the two least significant bits
	ldw	4(%r22),%r19		; load new LTP value
	ldw	0(%r22),%r22		; load address of target
LSYM(1)
#ifdef LINUX
	bv	%r0(%r22)		; branch to the real target
#else
	ldsid	(%sr0,%r22),%r1		; get the "space ident" selected by r22
	mtsp	%r1,%sr0		; move that space identifier into sr0
	be	0(%sr0,%r22)		; branch to the real target
#endif
	stw	%r2,-24(%r30)		; save return address into frame marker
	.exit
	.procend
#endif

#ifdef L_divI
/* ROUTINES:	$$divI, $$divoI

   Single precision divide for signed binary integers.

   The quotient is truncated towards zero.
   The sign of the quotient is the XOR of the signs of the dividend and
   divisor.
   Divide by zero is trapped.
   Divide of -2**31 by -1 is trapped for $$divoI but not for $$divI.

   INPUT REGISTERS:
   .	arg0 ==	dividend
   .	arg1 ==	divisor
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 =	undefined
   .	arg1 =	undefined
   .	ret1 =	quotient

   OTHER REGISTERS AFFECTED:
   .	r1   =	undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions:
   .		divisor is zero  (traps with ADDIT,=  0,25,0)
   .		dividend==-2**31  and divisor==-1 and routine is $$divoI
   .				 (traps with ADDO  26,25,0)
   .	Changes memory at the following places:
   .		NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Branchs to other millicode routines using BE
   .		$$div_# for # being 2,3,4,5,6,7,8,9,10,12,14,15
   .
   .	For selected divisors, calls a divide by constant routine written by
   .	Karl Pettis.  Eligible divisors are 1..15 excluding 11 and 13.
   .
   .	The only overflow case is -2**31 divided by -1.
   .	Both routines return -2**31 but only $$divoI traps.  */

RDEFINE(temp,r1)
RDEFINE(retreg,ret1)	/*  r29 */
RDEFINE(temp1,arg0)
	SUBSPA_MILLI_DIV
	ATTR_MILLI
	.import $$divI_2,millicode
	.import $$divI_3,millicode
	.import $$divI_4,millicode
	.import $$divI_5,millicode
	.import $$divI_6,millicode
	.import $$divI_7,millicode
	.import $$divI_8,millicode
	.import $$divI_9,millicode
	.import $$divI_10,millicode
	.import $$divI_12,millicode
	.import $$divI_14,millicode
	.import $$divI_15,millicode
	.export $$divI,millicode
	.export	$$divoI,millicode
	.proc
	.callinfo	millicode
	.entry
GSYM($$divoI)
	comib,=,n  -1,arg1,LREF(negative1)	/*  when divisor == -1 */
GSYM($$divI)
	ldo	-1(arg1),temp		/*  is there at most one bit set ? */
	and,<>	arg1,temp,r0		/*  if not, don't use power of 2 divide */
	addi,>	0,arg1,r0		/*  if divisor > 0, use power of 2 divide */
	b,n	LREF(neg_denom)
LSYM(pow2)
	addi,>=	0,arg0,retreg		/*  if numerator is negative, add the */
	add	arg0,temp,retreg	/*  (denominaotr -1) to correct for shifts */
	extru,=	arg1,15,16,temp		/*  test denominator with 0xffff0000 */
	extrs	retreg,15,16,retreg	/*  retreg = retreg >> 16 */
	or	arg1,temp,arg1		/*  arg1 = arg1 | (arg1 >> 16) */
	ldi	0xcc,temp1		/*  setup 0xcc in temp1 */
	extru,= arg1,23,8,temp		/*  test denominator with 0xff00 */
	extrs	retreg,23,24,retreg	/*  retreg = retreg >> 8 */
	or	arg1,temp,arg1		/*  arg1 = arg1 | (arg1 >> 8) */
	ldi	0xaa,temp		/*  setup 0xaa in temp */
	extru,= arg1,27,4,r0		/*  test denominator with 0xf0 */
	extrs	retreg,27,28,retreg	/*  retreg = retreg >> 4 */
	and,=	arg1,temp1,r0		/*  test denominator with 0xcc */
	extrs	retreg,29,30,retreg	/*  retreg = retreg >> 2 */
	and,=	arg1,temp,r0		/*  test denominator with 0xaa */
	extrs	retreg,30,31,retreg	/*  retreg = retreg >> 1 */
	MILLIRETN
LSYM(neg_denom)
	addi,<	0,arg1,r0		/*  if arg1 >= 0, it's not power of 2 */
	b,n	LREF(regular_seq)
	sub	r0,arg1,temp		/*  make denominator positive */
	comb,=,n  arg1,temp,LREF(regular_seq)	/*  test against 0x80000000 and 0 */
	ldo	-1(temp),retreg		/*  is there at most one bit set ? */
	and,=	temp,retreg,r0		/*  if so, the denominator is power of 2 */
	b,n	LREF(regular_seq)
	sub	r0,arg0,retreg		/*  negate numerator */
	comb,=,n arg0,retreg,LREF(regular_seq) /*  test against 0x80000000 */
	copy	retreg,arg0		/*  set up arg0, arg1 and temp	*/
	copy	temp,arg1		/*  before branching to pow2 */
	b	LREF(pow2)
	ldo	-1(arg1),temp
LSYM(regular_seq)
	comib,>>=,n 15,arg1,LREF(small_divisor)
	add,>=	0,arg0,retreg		/*  move dividend, if retreg < 0, */
LSYM(normal)
	subi	0,retreg,retreg		/*    make it positive */
	sub	0,arg1,temp		/*  clear carry,  */
					/*    negate the divisor */
	ds	0,temp,0		/*  set V-bit to the comple- */
					/*    ment of the divisor sign */
	add	retreg,retreg,retreg	/*  shift msb bit into carry */
	ds	r0,arg1,temp		/*  1st divide step, if no carry */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  2nd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  3rd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  4th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  5th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  6th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  7th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  8th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  9th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  10th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  11th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  12th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  13th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  14th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  15th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  16th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  17th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  18th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  19th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  20th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  21st divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  22nd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  23rd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  24th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  25th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  26th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  27th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  28th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  29th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  30th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  31st divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  32nd divide step, */
	addc	retreg,retreg,retreg	/*  shift last retreg bit into retreg */
	xor,>=	arg0,arg1,0		/*  get correct sign of quotient */
	  sub	0,retreg,retreg		/*    based on operand signs */
	MILLIRETN
	nop

LSYM(small_divisor)

#if defined(pa64)
/*  Clear the upper 32 bits of the arg1 register.  We are working with	*/
/*  small divisors (and 32 bit integers)   We must not be mislead  */
/*  by "1" bits left in the upper 32 bits.  */
	depd %r0,31,32,%r25
#endif
	blr,n	arg1,r0
	nop
/*  table for divisor == 0,1, ... ,15 */
	addit,=	0,arg1,r0	/*  trap if divisor == 0 */
	nop
	MILLIRET		/*  divisor == 1 */
	copy	arg0,retreg
	MILLI_BEN($$divI_2)	/*  divisor == 2 */
	nop
	MILLI_BEN($$divI_3)	/*  divisor == 3 */
	nop
	MILLI_BEN($$divI_4)	/*  divisor == 4 */
	nop
	MILLI_BEN($$divI_5)	/*  divisor == 5 */
	nop
	MILLI_BEN($$divI_6)	/*  divisor == 6 */
	nop
	MILLI_BEN($$divI_7)	/*  divisor == 7 */
	nop
	MILLI_BEN($$divI_8)	/*  divisor == 8 */
	nop
	MILLI_BEN($$divI_9)	/*  divisor == 9 */
	nop
	MILLI_BEN($$divI_10)	/*  divisor == 10 */
	nop
	b	LREF(normal)		/*  divisor == 11 */
	add,>=	0,arg0,retreg
	MILLI_BEN($$divI_12)	/*  divisor == 12 */
	nop
	b	LREF(normal)		/*  divisor == 13 */
	add,>=	0,arg0,retreg
	MILLI_BEN($$divI_14)	/*  divisor == 14 */
	nop
	MILLI_BEN($$divI_15)	/*  divisor == 15 */
	nop

LSYM(negative1)
	sub	0,arg0,retreg	/*  result is negation of dividend */
	MILLIRET
	addo	arg0,arg1,r0	/*  trap iff dividend==0x80000000 && divisor==-1 */
	.exit
	.procend
	.end
#endif

#ifdef L_divU
/* ROUTINE:	$$divU
   .
   .	Single precision divide for unsigned integers.
   .
   .	Quotient is truncated towards zero.
   .	Traps on divide by zero.

   INPUT REGISTERS:
   .	arg0 ==	dividend
   .	arg1 ==	divisor
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 =	undefined
   .	arg1 =	undefined
   .	ret1 =	quotient

   OTHER REGISTERS AFFECTED:
   .	r1   =	undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions:
   .		divisor is zero
   .	Changes memory at the following places:
   .		NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Does not create a stack frame.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Branchs to other millicode routines using BE:
   .		$$divU_# for 3,5,6,7,9,10,12,14,15
   .
   .	For selected small divisors calls the special divide by constant
   .	routines written by Karl Pettis.  These are: 3,5,6,7,9,10,12,14,15.  */

RDEFINE(temp,r1)
RDEFINE(retreg,ret1)	/* r29 */
RDEFINE(temp1,arg0)
	SUBSPA_MILLI_DIV
	ATTR_MILLI
	.export $$divU,millicode
	.import $$divU_3,millicode
	.import $$divU_5,millicode
	.import $$divU_6,millicode
	.import $$divU_7,millicode
	.import $$divU_9,millicode
	.import $$divU_10,millicode
	.import $$divU_12,millicode
	.import $$divU_14,millicode
	.import $$divU_15,millicode
	.proc
	.callinfo	millicode
	.entry
GSYM($$divU)
/* The subtract is not nullified since it does no harm and can be used
   by the two cases that branch back to "normal".  */
	ldo	-1(arg1),temp		/* is there at most one bit set ? */
	and,=	arg1,temp,r0		/* if so, denominator is power of 2 */
	b	LREF(regular_seq)
	addit,=	0,arg1,0		/* trap for zero dvr */
	copy	arg0,retreg
	extru,= arg1,15,16,temp		/* test denominator with 0xffff0000 */
	extru	retreg,15,16,retreg	/* retreg = retreg >> 16 */
	or	arg1,temp,arg1		/* arg1 = arg1 | (arg1 >> 16) */
	ldi	0xcc,temp1		/* setup 0xcc in temp1 */
	extru,= arg1,23,8,temp		/* test denominator with 0xff00 */
	extru	retreg,23,24,retreg	/* retreg = retreg >> 8 */
	or	arg1,temp,arg1		/* arg1 = arg1 | (arg1 >> 8) */
	ldi	0xaa,temp		/* setup 0xaa in temp */
	extru,= arg1,27,4,r0		/* test denominator with 0xf0 */
	extru	retreg,27,28,retreg	/* retreg = retreg >> 4 */
	and,=	arg1,temp1,r0		/* test denominator with 0xcc */
	extru	retreg,29,30,retreg	/* retreg = retreg >> 2 */
	and,=	arg1,temp,r0		/* test denominator with 0xaa */
	extru	retreg,30,31,retreg	/* retreg = retreg >> 1 */
	MILLIRETN
	nop	
LSYM(regular_seq)
	comib,>=  15,arg1,LREF(special_divisor)
	subi	0,arg1,temp		/* clear carry, negate the divisor */
	ds	r0,temp,r0		/* set V-bit to 1 */
LSYM(normal)
	add	arg0,arg0,retreg	/* shift msb bit into carry */
	ds	r0,arg1,temp		/* 1st divide step, if no carry */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 2nd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 3rd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 4th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 5th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 6th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 7th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 8th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 9th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 10th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 11th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 12th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 13th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 14th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 15th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 16th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 17th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 18th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 19th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 20th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 21st divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 22nd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 23rd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 24th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 25th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 26th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 27th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 28th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 29th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 30th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 31st divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 32nd divide step, */
	MILLIRET
	addc	retreg,retreg,retreg	/* shift last retreg bit into retreg */

/* Handle the cases where divisor is a small constant or has high bit on.  */
LSYM(special_divisor)
/*	blr	arg1,r0 */
/*	comib,>,n  0,arg1,LREF(big_divisor) ; nullify previous instruction */

/* Pratap 8/13/90. The 815 Stirling chip set has a bug that prevents us from
   generating such a blr, comib sequence. A problem in nullification. So I
   rewrote this code.  */

#if defined(pa64)
/* Clear the upper 32 bits of the arg1 register.  We are working with
   small divisors (and 32 bit unsigned integers)   We must not be mislead
   by "1" bits left in the upper 32 bits.  */
	depd %r0,31,32,%r25
#endif
	comib,>	0,arg1,LREF(big_divisor)
	nop
	blr	arg1,r0
	nop

LSYM(zero_divisor)	/* this label is here to provide external visibility */
	addit,=	0,arg1,0		/* trap for zero dvr */
	nop
	MILLIRET			/* divisor == 1 */
	copy	arg0,retreg
	MILLIRET			/* divisor == 2 */
	extru	arg0,30,31,retreg
	MILLI_BEN($$divU_3)		/* divisor == 3 */
	nop
	MILLIRET			/* divisor == 4 */
	extru	arg0,29,30,retreg
	MILLI_BEN($$divU_5)		/* divisor == 5 */
	nop
	MILLI_BEN($$divU_6)		/* divisor == 6 */
	nop
	MILLI_BEN($$divU_7)		/* divisor == 7 */
	nop
	MILLIRET			/* divisor == 8 */
	extru	arg0,28,29,retreg
	MILLI_BEN($$divU_9)		/* divisor == 9 */
	nop
	MILLI_BEN($$divU_10)		/* divisor == 10 */
	nop
	b	LREF(normal)		/* divisor == 11 */
	ds	r0,temp,r0		/* set V-bit to 1 */
	MILLI_BEN($$divU_12)		/* divisor == 12 */
	nop
	b	LREF(normal)		/* divisor == 13 */
	ds	r0,temp,r0		/* set V-bit to 1 */
	MILLI_BEN($$divU_14)		/* divisor == 14 */
	nop
	MILLI_BEN($$divU_15)		/* divisor == 15 */
	nop

/* Handle the case where the high bit is on in the divisor.
   Compute:	if( dividend>=divisor) quotient=1; else quotient=0;
   Note:	dividend>==divisor iff dividend-divisor does not borrow
   and		not borrow iff carry.  */
LSYM(big_divisor)
	sub	arg0,arg1,r0
	MILLIRET
	addc	r0,r0,retreg
	.exit
	.procend
	.end
#endif

#ifdef L_remI
/* ROUTINE:	$$remI

   DESCRIPTION:
   .	$$remI returns the remainder of the division of two signed 32-bit
   .	integers.  The sign of the remainder is the same as the sign of
   .	the dividend.


   INPUT REGISTERS:
   .	arg0 == dividend
   .	arg1 == divisor
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 = destroyed
   .	arg1 = destroyed
   .	ret1 = remainder

   OTHER REGISTERS AFFECTED:
   .	r1   = undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions:  DIVIDE BY ZERO
   .	Changes memory at the following places:  NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable
   .	Does not create a stack frame
   .	Is usable for internal or external microcode

   DISCUSSION:
   .	Calls other millicode routines via mrp:  NONE
   .	Calls other millicode routines:  NONE  */

RDEFINE(tmp,r1)
RDEFINE(retreg,ret1)

	SUBSPA_MILLI
	ATTR_MILLI
	.proc
	.callinfo millicode
	.entry
GSYM($$remI)
GSYM($$remoI)
	.export $$remI,MILLICODE
	.export $$remoI,MILLICODE
	ldo		-1(arg1),tmp		/*  is there at most one bit set ? */
	and,<>		arg1,tmp,r0		/*  if not, don't use power of 2 */
	addi,>		0,arg1,r0		/*  if denominator > 0, use power */
						/*  of 2 */
	b,n		LREF(neg_denom)
LSYM(pow2)
	comb,>,n	0,arg0,LREF(neg_num)	/*  is numerator < 0 ? */
	and		arg0,tmp,retreg		/*  get the result */
	MILLIRETN
LSYM(neg_num)
	subi		0,arg0,arg0		/*  negate numerator */
	and		arg0,tmp,retreg		/*  get the result */
	subi		0,retreg,retreg		/*  negate result */
	MILLIRETN
LSYM(neg_denom)
	addi,<		0,arg1,r0		/*  if arg1 >= 0, it's not power */
						/*  of 2 */
	b,n		LREF(regular_seq)
	sub		r0,arg1,tmp		/*  make denominator positive */
	comb,=,n	arg1,tmp,LREF(regular_seq) /*  test against 0x80000000 and 0 */
	ldo		-1(tmp),retreg		/*  is there at most one bit set ? */
	and,=		tmp,retreg,r0		/*  if not, go to regular_seq */
	b,n		LREF(regular_seq)
	comb,>,n	0,arg0,LREF(neg_num_2)	/*  if arg0 < 0, negate it  */
	and		arg0,retreg,retreg
	MILLIRETN
LSYM(neg_num_2)
	subi		0,arg0,tmp		/*  test against 0x80000000 */
	and		tmp,retreg,retreg
	subi		0,retreg,retreg
	MILLIRETN
LSYM(regular_seq)
	addit,=		0,arg1,0		/*  trap if div by zero */
	add,>=		0,arg0,retreg		/*  move dividend, if retreg < 0, */
	sub		0,retreg,retreg		/*    make it positive */
	sub		0,arg1, tmp		/*  clear carry,  */
						/*    negate the divisor */
	ds		0, tmp,0		/*  set V-bit to the comple- */
						/*    ment of the divisor sign */
	or		0,0, tmp		/*  clear  tmp */
	add		retreg,retreg,retreg	/*  shift msb bit into carry */
	ds		 tmp,arg1, tmp		/*  1st divide step, if no carry */
						/*    out, msb of quotient = 0 */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
LSYM(t1)
	ds		 tmp,arg1, tmp		/*  2nd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  3rd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  4th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  5th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  6th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  7th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  8th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  9th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  10th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  11th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  12th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  13th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  14th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  15th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  16th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  17th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  18th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  19th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  20th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  21st divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  22nd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  23rd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  24th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  25th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  26th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  27th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  28th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  29th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  30th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  31st divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  32nd divide step, */
	addc		retreg,retreg,retreg	/*  shift last bit into retreg */
	movb,>=,n	 tmp,retreg,LREF(finish) /*  branch if pos.  tmp */
	add,<		arg1,0,0		/*  if arg1 > 0, add arg1 */
	add,tr		 tmp,arg1,retreg	/*    for correcting remainder tmp */
	sub		 tmp,arg1,retreg	/*  else add absolute value arg1 */
LSYM(finish)
	add,>=		arg0,0,0		/*  set sign of remainder */
	sub		0,retreg,retreg		/*    to sign of dividend */
	MILLIRET
	nop
	.exit
	.procend
#ifdef milliext
	.origin 0x00000200
#endif
	.end
#endif

#ifdef L_remU
/* ROUTINE:	$$remU
   .	Single precision divide for remainder with unsigned binary integers.
   .
   .	The remainder must be dividend-(dividend/divisor)*divisor.
   .	Divide by zero is trapped.

   INPUT REGISTERS:
   .	arg0 ==	dividend
   .	arg1 == divisor
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 =	undefined
   .	arg1 =	undefined
   .	ret1 =	remainder

   OTHER REGISTERS AFFECTED:
   .	r1   =	undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions:  DIVIDE BY ZERO
   .	Changes memory at the following places:  NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Does not create a stack frame.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Calls other millicode routines using mrp: NONE
   .	Calls other millicode routines: NONE  */


RDEFINE(temp,r1)
RDEFINE(rmndr,ret1)	/*  r29 */
	SUBSPA_MILLI
	ATTR_MILLI
	.export $$remU,millicode
	.proc
	.callinfo	millicode
	.entry
GSYM($$remU)
	ldo	-1(arg1),temp		/*  is there at most one bit set ? */
	and,=	arg1,temp,r0		/*  if not, don't use power of 2 */
	b	LREF(regular_seq)
	addit,=	0,arg1,r0		/*  trap on div by zero */
	and	arg0,temp,rmndr		/*  get the result for power of 2 */
	MILLIRETN
LSYM(regular_seq)
	comib,>=,n  0,arg1,LREF(special_case)
	subi	0,arg1,rmndr		/*  clear carry, negate the divisor */
	ds	r0,rmndr,r0		/*  set V-bit to 1 */
	add	arg0,arg0,temp		/*  shift msb bit into carry */
	ds	r0,arg1,rmndr		/*  1st divide step, if no carry */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  2nd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  3rd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  4th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  5th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  6th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  7th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  8th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  9th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  10th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  11th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  12th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  13th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  14th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  15th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  16th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  17th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  18th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  19th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  20th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  21st divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  22nd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  23rd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  24th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  25th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  26th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  27th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  28th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  29th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  30th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  31st divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  32nd divide step, */
	comiclr,<= 0,rmndr,r0
	  add	rmndr,arg1,rmndr	/*  correction */
	MILLIRETN
	nop

/* Putting >= on the last DS and deleting COMICLR does not work!  */
LSYM(special_case)
	sub,>>=	arg0,arg1,rmndr
	  copy	arg0,rmndr
	MILLIRETN
	nop
	.exit
	.procend
	.end
#endif

#ifdef L_div_const
/* ROUTINE:	$$divI_2
   .		$$divI_3	$$divU_3
   .		$$divI_4
   .		$$divI_5	$$divU_5
   .		$$divI_6	$$divU_6
   .		$$divI_7	$$divU_7
   .		$$divI_8
   .		$$divI_9	$$divU_9
   .		$$divI_10	$$divU_10
   .
   .		$$divI_12	$$divU_12
   .
   .		$$divI_14	$$divU_14
   .		$$divI_15	$$divU_15
   .		$$divI_16
   .		$$divI_17	$$divU_17
   .
   .	Divide by selected constants for single precision binary integers.

   INPUT REGISTERS:
   .	arg0 ==	dividend
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 =	undefined
   .	arg1 =	undefined
   .	ret1 =	quotient

   OTHER REGISTERS AFFECTED:
   .	r1   =	undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions: NONE
   .	Changes memory at the following places:  NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Does not create a stack frame.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Calls other millicode routines using mrp:  NONE
   .	Calls other millicode routines:  NONE  */


/* TRUNCATED DIVISION BY SMALL INTEGERS

   We are interested in q(x) = floor(x/y), where x >= 0 and y > 0
   (with y fixed).

   Let a = floor(z/y), for some choice of z.  Note that z will be
   chosen so that division by z is cheap.

   Let r be the remainder(z/y).  In other words, r = z - ay.

   Now, our method is to choose a value for b such that

   q'(x) = floor((ax+b)/z)

   is equal to q(x) over as large a range of x as possible.  If the
   two are equal over a sufficiently large range, and if it is easy to
   form the product (ax), and it is easy to divide by z, then we can
   perform the division much faster than the general division algorithm.

   So, we want the following to be true:

   .	For x in the following range:
   .
   .	    ky <= x < (k+1)y
   .
   .	implies that
   .
   .	    k <= (ax+b)/z < (k+1)

   We want to determine b such that this is true for all k in the
   range {0..K} for some maximum K.

   Since (ax+b) is an increasing function of x, we can take each
   bound separately to determine the "best" value for b.

   (ax+b)/z < (k+1)	       implies

   (a((k+1)y-1)+b < (k+1)z     implies

   b < a + (k+1)(z-ay)	       implies

   b < a + (k+1)r

   This needs to be true for all k in the range {0..K}.  In
   particular, it is true for k = 0 and this leads to a maximum
   acceptable value for b.

   b < a+r   or   b <= a+r-1

   Taking the other bound, we have

   k <= (ax+b)/z	       implies

   k <= (aky+b)/z	       implies

   k(z-ay) <= b		       implies

   kr <= b

   Clearly, the largest range for k will be achieved by maximizing b,
   when r is not zero.	When r is zero, then the simplest choice for b
   is 0.  When r is not 0, set

   .	b = a+r-1

   Now, by construction, q'(x) = floor((ax+b)/z) = q(x) = floor(x/y)
   for all x in the range:

   .	0 <= x < (K+1)y

   We need to determine what K is.  Of our two bounds,

   .	b < a+(k+1)r	is satisfied for all k >= 0, by construction.

   The other bound is

   .	kr <= b

   This is always true if r = 0.  If r is not 0 (the usual case), then
   K = floor((a+r-1)/r), is the maximum value for k.

   Therefore, the formula q'(x) = floor((ax+b)/z) yields the correct
   answer for q(x) = floor(x/y) when x is in the range

   (0,(K+1)y-1)	       K = floor((a+r-1)/r)

   To be most useful, we want (K+1)y-1 = (max x) >= 2**32-1 so that
   the formula for q'(x) yields the correct value of q(x) for all x
   representable by a single word in HPPA.

   We are also constrained in that computing the product (ax), adding
   b, and dividing by z must all be done quickly, otherwise we will be
   better off going through the general algorithm using the DS
   instruction, which uses approximately 70 cycles.

   For each y, there is a choice of z which satisfies the constraints
   for (K+1)y >= 2**32.  We may not, however, be able to satisfy the
   timing constraints for arbitrary y.	It seems that z being equal to
   a power of 2 or a power of 2 minus 1 is as good as we can do, since
   it minimizes the time to do division by z.  We want the choice of z
   to also result in a value for (a) that minimizes the computation of
   the product (ax).  This is best achieved if (a) has a regular bit
   pattern (so the multiplication can be done with shifts and adds).
   The value of (a) also needs to be less than 2**32 so the product is
   always guaranteed to fit in 2 words.

   In actual practice, the following should be done:

   1) For negative x, you should take the absolute value and remember
   .  the fact so that the result can be negated.  This obviously does
   .  not apply in the unsigned case.
   2) For even y, you should factor out the power of 2 that divides y
   .  and divide x by it.  You can then proceed by dividing by the
   .  odd factor of y.

   Here is a table of some odd values of y, and corresponding choices
   for z which are "good".

    y	  z	  r	 a (hex)     max x (hex)

    3	2**32	  1	55555555      100000001
    5	2**32	  1	33333333      100000003
    7  2**24-1	  0	  249249     (infinite)
    9  2**24-1	  0	  1c71c7     (infinite)
   11  2**20-1	  0	   1745d     (infinite)
   13  2**24-1	  0	  13b13b     (infinite)
   15	2**32	  1	11111111      10000000d
   17	2**32	  1	 f0f0f0f      10000000f

   If r is 1, then b = a+r-1 = a.  This simplifies the computation
   of (ax+b), since you can compute (x+1)(a) instead.  If r is 0,
   then b = 0 is ok to use which simplifies (ax+b).

   The bit patterns for 55555555, 33333333, and 11111111 are obviously
   very regular.  The bit patterns for the other values of a above are:

    y	   (hex)	  (binary)

    7	  249249  001001001001001001001001  << regular >>
    9	  1c71c7  000111000111000111000111  << regular >>
   11	   1745d  000000010111010001011101  << irregular >>
   13	  13b13b  000100111011000100111011  << irregular >>

   The bit patterns for (a) corresponding to (y) of 11 and 13 may be
   too irregular to warrant using this method.

   When z is a power of 2 minus 1, then the division by z is slightly
   more complicated, involving an iterative solution.

   The code presented here solves division by 1 through 17, except for
   11 and 13. There are algorithms for both signed and unsigned
   quantities given.

   TIMINGS (cycles)

   divisor  positive  negative	unsigned

   .   1	2	   2	     2
   .   2	4	   4	     2
   .   3       19	  21	    19
   .   4	4	   4	     2
   .   5       18	  22	    19
   .   6       19	  22	    19
   .   8	4	   4	     2
   .  10       18	  19	    17
   .  12       18	  20	    18
   .  15       16	  18	    16
   .  16	4	   4	     2
   .  17       16	  18	    16

   Now, the algorithm for 7, 9, and 14 is an iterative one.  That is,
   a loop body is executed until the tentative quotient is 0.  The
   number of times the loop body is executed varies depending on the
   dividend, but is never more than two times.	If the dividend is
   less than the divisor, then the loop body is not executed at all.
   Each iteration adds 4 cycles to the timings.

   divisor  positive  negative	unsigned

   .   7       19+4n	 20+4n	   20+4n    n = number of iterations
   .   9       21+4n	 22+4n	   21+4n
   .  14       21+4n	 22+4n	   20+4n

   To give an idea of how the number of iterations varies, here is a
   table of dividend versus number of iterations when dividing by 7.

   smallest	 largest       required
   dividend	dividend      iterations

   .	0	     6		    0
   .	7	 0x6ffffff	    1
   0x1000006	0xffffffff	    2

   There is some overlap in the range of numbers requiring 1 and 2
   iterations.	*/

RDEFINE(t2,r1)
RDEFINE(x2,arg0)	/*  r26 */
RDEFINE(t1,arg1)	/*  r25 */
RDEFINE(x1,ret1)	/*  r29 */

	SUBSPA_MILLI_DIV
	ATTR_MILLI

	.proc
	.callinfo	millicode
	.entry
/* NONE of these routines require a stack frame
   ALL of these routines are unwindable from millicode	*/

GSYM($$divide_by_constant)
	.export $$divide_by_constant,millicode
/*  Provides a "nice" label for the code covered by the unwind descriptor
    for things like gprof.  */

/* DIVISION BY 2 (shift by 1) */
GSYM($$divI_2)
	.export		$$divI_2,millicode
	comclr,>=	arg0,0,0
	addi		1,arg0,arg0
	MILLIRET
	extrs		arg0,30,31,ret1


/* DIVISION BY 4 (shift by 2) */
GSYM($$divI_4)
	.export		$$divI_4,millicode
	comclr,>=	arg0,0,0
	addi		3,arg0,arg0
	MILLIRET
	extrs		arg0,29,30,ret1


/* DIVISION BY 8 (shift by 3) */
GSYM($$divI_8)
	.export		$$divI_8,millicode
	comclr,>=	arg0,0,0
	addi		7,arg0,arg0
	MILLIRET
	extrs		arg0,28,29,ret1

/* DIVISION BY 16 (shift by 4) */
GSYM($$divI_16)
	.export		$$divI_16,millicode
	comclr,>=	arg0,0,0
	addi		15,arg0,arg0
	MILLIRET
	extrs		arg0,27,28,ret1

/****************************************************************************
*
*	DIVISION BY DIVISORS OF FFFFFFFF, and powers of 2 times these
*
*	includes 3,5,15,17 and also 6,10,12
*
****************************************************************************/

/* DIVISION BY 3 (use z = 2**32; a = 55555555) */

GSYM($$divI_3)
	.export		$$divI_3,millicode
	comb,<,N	x2,0,LREF(neg3)

	addi		1,x2,x2		/* this cannot overflow	*/
	extru		x2,1,2,x1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(pos)
	addc		x1,0,x1

LSYM(neg3)
	subi		1,x2,x2		/* this cannot overflow	*/
	extru		x2,1,2,x1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(neg)
	addc		x1,0,x1

GSYM($$divU_3)
	.export		$$divU_3,millicode
	addi		1,x2,x2		/* this CAN overflow */
	addc		0,0,x1
	shd		x1,x2,30,t1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(pos)
	addc		x1,t1,x1

/* DIVISION BY 5 (use z = 2**32; a = 33333333) */

GSYM($$divI_5)
	.export		$$divI_5,millicode
	comb,<,N	x2,0,LREF(neg5)

	addi		3,x2,t1		/* this cannot overflow	*/
	sh1add		x2,t1,x2	/* multiply by 3 to get started */
	b		LREF(pos)
	addc		0,0,x1

LSYM(neg5)
	sub		0,x2,x2		/* negate x2			*/
	addi		1,x2,x2		/* this cannot overflow	*/
	shd		0,x2,31,x1	/* get top bit (can be 1)	*/
	sh1add		x2,x2,x2	/* multiply by 3 to get started */
	b		LREF(neg)
	addc		x1,0,x1

GSYM($$divU_5)
	.export		$$divU_5,millicode
	addi		1,x2,x2		/* this CAN overflow */
	addc		0,0,x1
	shd		x1,x2,31,t1	/* multiply by 3 to get started */
	sh1add		x2,x2,x2
	b		LREF(pos)
	addc		t1,x1,x1

/* DIVISION BY	6 (shift to divide by 2 then divide by 3) */
GSYM($$divI_6)
	.export		$$divI_6,millicode
	comb,<,N	x2,0,LREF(neg6)
	extru		x2,30,31,x2	/* divide by 2			*/
	addi		5,x2,t1		/* compute 5*(x2+1) = 5*x2+5	*/
	sh2add		x2,t1,x2	/* multiply by 5 to get started */
	b		LREF(pos)
	addc		0,0,x1

LSYM(neg6)
	subi		2,x2,x2		/* negate, divide by 2, and add 1 */
					/* negation and adding 1 are done */
					/* at the same time by the SUBI   */
	extru		x2,30,31,x2
	shd		0,x2,30,x1
	sh2add		x2,x2,x2	/* multiply by 5 to get started */
	b		LREF(neg)
	addc		x1,0,x1

GSYM($$divU_6)
	.export		$$divU_6,millicode
	extru		x2,30,31,x2	/* divide by 2 */
	addi		1,x2,x2		/* cannot carry */
	shd		0,x2,30,x1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(pos)
	addc		x1,0,x1

/* DIVISION BY 10 (shift to divide by 2 then divide by 5) */
GSYM($$divU_10)
	.export		$$divU_10,millicode
	extru		x2,30,31,x2	/* divide by 2 */
	addi		3,x2,t1		/* compute 3*(x2+1) = (3*x2)+3	*/
	sh1add		x2,t1,x2	/* multiply by 3 to get started */
	addc		0,0,x1
LSYM(pos)
	shd		x1,x2,28,t1	/* multiply by 0x11 */
	shd		x2,0,28,t2
	add		x2,t2,x2
	addc		x1,t1,x1
LSYM(pos_for_17)
	shd		x1,x2,24,t1	/* multiply by 0x101 */
	shd		x2,0,24,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,16,t1	/* multiply by 0x10001 */
	shd		x2,0,16,t2
	add		x2,t2,x2
	MILLIRET
	addc		x1,t1,x1

GSYM($$divI_10)
	.export		$$divI_10,millicode
	comb,<		x2,0,LREF(neg10)
	copy		0,x1
	extru		x2,30,31,x2	/* divide by 2 */
	addib,TR	1,x2,LREF(pos)	/* add 1 (cannot overflow)     */
	sh1add		x2,x2,x2	/* multiply by 3 to get started */

LSYM(neg10)
	subi		2,x2,x2		/* negate, divide by 2, and add 1 */
					/* negation and adding 1 are done */
					/* at the same time by the SUBI   */
	extru		x2,30,31,x2
	sh1add		x2,x2,x2	/* multiply by 3 to get started */
LSYM(neg)
	shd		x1,x2,28,t1	/* multiply by 0x11 */
	shd		x2,0,28,t2
	add		x2,t2,x2
	addc		x1,t1,x1
LSYM(neg_for_17)
	shd		x1,x2,24,t1	/* multiply by 0x101 */
	shd		x2,0,24,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,16,t1	/* multiply by 0x10001 */
	shd		x2,0,16,t2
	add		x2,t2,x2
	addc		x1,t1,x1
	MILLIRET
	sub		0,x1,x1

/* DIVISION BY 12 (shift to divide by 4 then divide by 3) */
GSYM($$divI_12)
	.export		$$divI_12,millicode
	comb,<		x2,0,LREF(neg12)
	copy		0,x1
	extru		x2,29,30,x2	/* divide by 4			*/
	addib,tr	1,x2,LREF(pos)	/* compute 5*(x2+1) = 5*x2+5    */
	sh2add		x2,x2,x2	/* multiply by 5 to get started */

LSYM(neg12)
	subi		4,x2,x2		/* negate, divide by 4, and add 1 */
					/* negation and adding 1 are done */
					/* at the same time by the SUBI   */
	extru		x2,29,30,x2
	b		LREF(neg)
	sh2add		x2,x2,x2	/* multiply by 5 to get started */

GSYM($$divU_12)
	.export		$$divU_12,millicode
	extru		x2,29,30,x2	/* divide by 4   */
	addi		5,x2,t1		/* cannot carry */
	sh2add		x2,t1,x2	/* multiply by 5 to get started */
	b		LREF(pos)
	addc		0,0,x1

/* DIVISION BY 15 (use z = 2**32; a = 11111111) */
GSYM($$divI_15)
	.export		$$divI_15,millicode
	comb,<		x2,0,LREF(neg15)
	copy		0,x1
	addib,tr	1,x2,LREF(pos)+4
	shd		x1,x2,28,t1

LSYM(neg15)
	b		LREF(neg)
	subi		1,x2,x2

GSYM($$divU_15)
	.export		$$divU_15,millicode
	addi		1,x2,x2		/* this CAN overflow */
	b		LREF(pos)
	addc		0,0,x1

/* DIVISION BY 17 (use z = 2**32; a =  f0f0f0f) */
GSYM($$divI_17)
	.export		$$divI_17,millicode
	comb,<,n	x2,0,LREF(neg17)
	addi		1,x2,x2		/* this cannot overflow */
	shd		0,x2,28,t1	/* multiply by 0xf to get started */
	shd		x2,0,28,t2
	sub		t2,x2,x2
	b		LREF(pos_for_17)
	subb		t1,0,x1

LSYM(neg17)
	subi		1,x2,x2		/* this cannot overflow */
	shd		0,x2,28,t1	/* multiply by 0xf to get started */
	shd		x2,0,28,t2
	sub		t2,x2,x2
	b		LREF(neg_for_17)
	subb		t1,0,x1

GSYM($$divU_17)
	.export		$$divU_17,millicode
	addi		1,x2,x2		/* this CAN overflow */
	addc		0,0,x1
	shd		x1,x2,28,t1	/* multiply by 0xf to get started */
LSYM(u17)
	shd		x2,0,28,t2
	sub		t2,x2,x2
	b		LREF(pos_for_17)
	subb		t1,x1,x1


/* DIVISION BY DIVISORS OF FFFFFF, and powers of 2 times these
   includes 7,9 and also 14


   z = 2**24-1
   r = z mod x = 0

   so choose b = 0

   Also, in order to divide by z = 2**24-1, we approximate by dividing
   by (z+1) = 2**24 (which is easy), and then correcting.

   (ax) = (z+1)q' + r
   .	= zq' + (q'+r)

   So to compute (ax)/z, compute q' = (ax)/(z+1) and r = (ax) mod (z+1)
   Then the true remainder of (ax)/z is (q'+r).  Repeat the process
   with this new remainder, adding the tentative quotients together,
   until a tentative quotient is 0 (and then we are done).  There is
   one last correction to be done.  It is possible that (q'+r) = z.
   If so, then (q'+r)/(z+1) = 0 and it looks like we are done.	But,
   in fact, we need to add 1 more to the quotient.  Now, it turns
   out that this happens if and only if the original value x is
   an exact multiple of y.  So, to avoid a three instruction test at
   the end, instead use 1 instruction to add 1 to x at the beginning.  */

/* DIVISION BY 7 (use z = 2**24-1; a = 249249) */
GSYM($$divI_7)
	.export		$$divI_7,millicode
	comb,<,n	x2,0,LREF(neg7)
LSYM(7)
	addi		1,x2,x2		/* cannot overflow */
	shd		0,x2,29,x1
	sh3add		x2,x2,x2
	addc		x1,0,x1
LSYM(pos7)
	shd		x1,x2,26,t1
	shd		x2,0,26,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,20,t1
	shd		x2,0,20,t2
	add		x2,t2,x2
	addc		x1,t1,t1

	/* computed <t1,x2>.  Now divide it by (2**24 - 1)	*/

	copy		0,x1
	shd,=		t1,x2,24,t1	/* tentative quotient  */
LSYM(1)
	addb,tr		t1,x1,LREF(2)	/* add to previous quotient   */
	extru		x2,31,24,x2	/* new remainder (unadjusted) */

	MILLIRETN

LSYM(2)
	addb,tr		t1,x2,LREF(1)	/* adjust remainder */
	extru,=		x2,7,8,t1	/* new quotient     */

LSYM(neg7)
	subi		1,x2,x2		/* negate x2 and add 1 */
LSYM(8)
	shd		0,x2,29,x1
	sh3add		x2,x2,x2
	addc		x1,0,x1

LSYM(neg7_shift)
	shd		x1,x2,26,t1
	shd		x2,0,26,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,20,t1
	shd		x2,0,20,t2
	add		x2,t2,x2
	addc		x1,t1,t1

	/* computed <t1,x2>.  Now divide it by (2**24 - 1)	*/

	copy		0,x1
	shd,=		t1,x2,24,t1	/* tentative quotient  */
LSYM(3)
	addb,tr		t1,x1,LREF(4)	/* add to previous quotient   */
	extru		x2,31,24,x2	/* new remainder (unadjusted) */

	MILLIRET
	sub		0,x1,x1		/* negate result    */

LSYM(4)
	addb,tr		t1,x2,LREF(3)	/* adjust remainder */
	extru,=		x2,7,8,t1	/* new quotient     */

GSYM($$divU_7)
	.export		$$divU_7,millicode
	addi		1,x2,x2		/* can carry */
	addc		0,0,x1
	shd		x1,x2,29,t1
	sh3add		x2,x2,x2
	b		LREF(pos7)
	addc		t1,x1,x1

/* DIVISION BY 9 (use z = 2**24-1; a = 1c71c7) */
GSYM($$divI_9)
	.export		$$divI_9,millicode
	comb,<,n	x2,0,LREF(neg9)
	addi		1,x2,x2		/* cannot overflow */
	shd		0,x2,29,t1
	shd		x2,0,29,t2
	sub		t2,x2,x2
	b		LREF(pos7)
	subb		t1,0,x1

LSYM(neg9)
	subi		1,x2,x2		/* negate and add 1 */
	shd		0,x2,29,t1
	shd		x2,0,29,t2
	sub		t2,x2,x2
	b		LREF(neg7_shift)
	subb		t1,0,x1

GSYM($$divU_9)
	.export		$$divU_9,millicode
	addi		1,x2,x2		/* can carry */
	addc		0,0,x1
	shd		x1,x2,29,t1
	shd		x2,0,29,t2
	sub		t2,x2,x2
	b		LREF(pos7)
	subb		t1,x1,x1

/* DIVISION BY 14 (shift to divide by 2 then divide by 7) */
GSYM($$divI_14)
	.export		$$divI_14,millicode
	comb,<,n	x2,0,LREF(neg14)
GSYM($$divU_14)
	.export		$$divU_14,millicode
	b		LREF(7)		/* go to 7 case */
	extru		x2,30,31,x2	/* divide by 2  */

LSYM(neg14)
	subi		2,x2,x2		/* negate (and add 2) */
	b		LREF(8)
	extru		x2,30,31,x2	/* divide by 2	      */
	.exit
	.procend
	.end
#endif

#ifdef L_mulI
/* VERSION "@(#)$$mulI $ Revision: 12.4 $ $ Date: 94/03/17 17:18:51 $" */
/******************************************************************************
This routine is used on PA2.0 processors when gcc -mno-fpregs is used

ROUTINE:	$$mulI


DESCRIPTION:	

	$$mulI multiplies two single word integers, giving a single 
	word result.  


INPUT REGISTERS:

	arg0 = Operand 1
	arg1 = Operand 2
	r31  == return pc
	sr0  == return space when called externally 


OUTPUT REGISTERS:

	arg0 = undefined
	arg1 = undefined
	ret1 = result 

OTHER REGISTERS AFFECTED:

	r1   = undefined

SIDE EFFECTS:

	Causes a trap under the following conditions:  NONE
	Changes memory at the following places:  NONE

PERMISSIBLE CONTEXT:

	Unwindable
	Does not create a stack frame
	Is usable for internal or external microcode

DISCUSSION:

	Calls other millicode routines via mrp:  NONE
	Calls other millicode routines:  NONE

***************************************************************************/


#define	a0	%arg0
#define	a1	%arg1
#define	t0	%r1
#define	r	%ret1

#define	a0__128a0	zdep	a0,24,25,a0
#define	a0__256a0	zdep	a0,23,24,a0
#define	a1_ne_0_b_l0	comb,<>	a1,0,LREF(l0)
#define	a1_ne_0_b_l1	comb,<>	a1,0,LREF(l1)
#define	a1_ne_0_b_l2	comb,<>	a1,0,LREF(l2)
#define	b_n_ret_t0	b,n	LREF(ret_t0)
#define	b_e_shift	b	LREF(e_shift)
#define	b_e_t0ma0	b	LREF(e_t0ma0)
#define	b_e_t0		b	LREF(e_t0)
#define	b_e_t0a0	b	LREF(e_t0a0)
#define	b_e_t02a0	b	LREF(e_t02a0)
#define	b_e_t04a0	b	LREF(e_t04a0)
#define	b_e_2t0		b	LREF(e_2t0)
#define	b_e_2t0a0	b	LREF(e_2t0a0)
#define	b_e_2t04a0	b	LREF(e2t04a0)
#define	b_e_3t0		b	LREF(e_3t0)
#define	b_e_4t0		b	LREF(e_4t0)
#define	b_e_4t0a0	b	LREF(e_4t0a0)
#define	b_e_4t08a0	b	LREF(e4t08a0)
#define	b_e_5t0		b	LREF(e_5t0)
#define	b_e_8t0		b	LREF(e_8t0)
#define	b_e_8t0a0	b	LREF(e_8t0a0)
#define	r__r_a0		add	r,a0,r
#define	r__r_2a0	sh1add	a0,r,r
#define	r__r_4a0	sh2add	a0,r,r
#define	r__r_8a0	sh3add	a0,r,r
#define	r__r_t0		add	r,t0,r
#define	r__r_2t0	sh1add	t0,r,r
#define	r__r_4t0	sh2add	t0,r,r
#define	r__r_8t0	sh3add	t0,r,r
#define	t0__3a0		sh1add	a0,a0,t0
#define	t0__4a0		sh2add	a0,0,t0
#define	t0__5a0		sh2add	a0,a0,t0
#define	t0__8a0		sh3add	a0,0,t0
#define	t0__9a0		sh3add	a0,a0,t0
#define	t0__16a0	zdep	a0,27,28,t0
#define	t0__32a0	zdep	a0,26,27,t0
#define	t0__64a0	zdep	a0,25,26,t0
#define	t0__128a0	zdep	a0,24,25,t0
#define	t0__t0ma0	sub	t0,a0,t0
#define	t0__t0_a0	add	t0,a0,t0
#define	t0__t0_2a0	sh1add	a0,t0,t0
#define	t0__t0_4a0	sh2add	a0,t0,t0
#define	t0__t0_8a0	sh3add	a0,t0,t0
#define	t0__2t0_a0	sh1add	t0,a0,t0
#define	t0__3t0		sh1add	t0,t0,t0
#define	t0__4t0		sh2add	t0,0,t0
#define	t0__4t0_a0	sh2add	t0,a0,t0
#define	t0__5t0		sh2add	t0,t0,t0
#define	t0__8t0		sh3add	t0,0,t0
#define	t0__8t0_a0	sh3add	t0,a0,t0
#define	t0__9t0		sh3add	t0,t0,t0
#define	t0__16t0	zdep	t0,27,28,t0
#define	t0__32t0	zdep	t0,26,27,t0
#define	t0__256a0	zdep	a0,23,24,t0


	SUBSPA_MILLI
	ATTR_MILLI
	.align 16
	.proc
	.callinfo millicode
	.export $$mulI,millicode
GSYM($$mulI)	
	combt,<<=	a1,a0,LREF(l4)	/* swap args if unsigned a1>a0 */
	copy		0,r		/* zero out the result */
	xor		a0,a1,a0	/* swap a0 & a1 using the */
	xor		a0,a1,a1	/*  old xor trick */
	xor		a0,a1,a0
LSYM(l4)
	combt,<=	0,a0,LREF(l3)		/* if a0>=0 then proceed like unsigned */
	zdep		a1,30,8,t0	/* t0 = (a1&0xff)<<1 ********* */
	sub,>		0,a1,t0		/* otherwise negate both and */
	combt,<=,n	a0,t0,LREF(l2)	/*  swap back if |a0|<|a1| */
	sub		0,a0,a1
	movb,tr,n	t0,a0,LREF(l2)	/* 10th inst.  */

LSYM(l0)	r__r_t0				/* add in this partial product */
LSYM(l1)	a0__256a0			/* a0 <<= 8 ****************** */
LSYM(l2)	zdep		a1,30,8,t0	/* t0 = (a1&0xff)<<1 ********* */
LSYM(l3)	blr		t0,0		/* case on these 8 bits ****** */
		extru		a1,23,24,a1	/* a1 >>= 8 ****************** */

/*16 insts before this.  */
/*			  a0 <<= 8 ************************** */
LSYM(x0)	a1_ne_0_b_l2	! a0__256a0	! MILLIRETN	! nop
LSYM(x1)	a1_ne_0_b_l1	! r__r_a0	! MILLIRETN	! nop
LSYM(x2)	a1_ne_0_b_l1	! r__r_2a0	! MILLIRETN	! nop
LSYM(x3)	a1_ne_0_b_l0	! t0__3a0	! MILLIRET	! r__r_t0
LSYM(x4)	a1_ne_0_b_l1	! r__r_4a0	! MILLIRETN	! nop
LSYM(x5)	a1_ne_0_b_l0	! t0__5a0	! MILLIRET	! r__r_t0
LSYM(x6)	t0__3a0		! a1_ne_0_b_l1	! r__r_2t0	! MILLIRETN
LSYM(x7)	t0__3a0		! a1_ne_0_b_l0	! r__r_4a0	! b_n_ret_t0
LSYM(x8)	a1_ne_0_b_l1	! r__r_8a0	! MILLIRETN	! nop
LSYM(x9)	a1_ne_0_b_l0	! t0__9a0	! MILLIRET	! r__r_t0
LSYM(x10)	t0__5a0		! a1_ne_0_b_l1	! r__r_2t0	! MILLIRETN
LSYM(x11)	t0__3a0		! a1_ne_0_b_l0	! r__r_8a0	! b_n_ret_t0
LSYM(x12)	t0__3a0		! a1_ne_0_b_l1	! r__r_4t0	! MILLIRETN
LSYM(x13)	t0__5a0		! a1_ne_0_b_l0	! r__r_8a0	! b_n_ret_t0
LSYM(x14)	t0__3a0		! t0__2t0_a0	! b_e_shift	! r__r_2t0
LSYM(x15)	t0__5a0		! a1_ne_0_b_l0	! t0__3t0	! b_n_ret_t0
LSYM(x16)	t0__16a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x17)	t0__9a0		! a1_ne_0_b_l0	! t0__t0_8a0	! b_n_ret_t0
LSYM(x18)	t0__9a0		! a1_ne_0_b_l1	! r__r_2t0	! MILLIRETN
LSYM(x19)	t0__9a0		! a1_ne_0_b_l0	! t0__2t0_a0	! b_n_ret_t0
LSYM(x20)	t0__5a0		! a1_ne_0_b_l1	! r__r_4t0	! MILLIRETN
LSYM(x21)	t0__5a0		! a1_ne_0_b_l0	! t0__4t0_a0	! b_n_ret_t0
LSYM(x22)	t0__5a0		! t0__2t0_a0	! b_e_shift	! r__r_2t0
LSYM(x23)	t0__5a0		! t0__2t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x24)	t0__3a0		! a1_ne_0_b_l1	! r__r_8t0	! MILLIRETN
LSYM(x25)	t0__5a0		! a1_ne_0_b_l0	! t0__5t0	! b_n_ret_t0
LSYM(x26)	t0__3a0		! t0__4t0_a0	! b_e_shift	! r__r_2t0
LSYM(x27)	t0__3a0		! a1_ne_0_b_l0	! t0__9t0	! b_n_ret_t0
LSYM(x28)	t0__3a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x29)	t0__3a0		! t0__2t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x30)	t0__5a0		! t0__3t0	! b_e_shift	! r__r_2t0
LSYM(x31)	t0__32a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
LSYM(x32)	t0__32a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x33)	t0__8a0		! a1_ne_0_b_l0	! t0__4t0_a0	! b_n_ret_t0
LSYM(x34)	t0__16a0	! t0__t0_a0	! b_e_shift	! r__r_2t0
LSYM(x35)	t0__9a0		! t0__3t0	! b_e_t0	! t0__t0_8a0
LSYM(x36)	t0__9a0		! a1_ne_0_b_l1	! r__r_4t0	! MILLIRETN
LSYM(x37)	t0__9a0		! a1_ne_0_b_l0	! t0__4t0_a0	! b_n_ret_t0
LSYM(x38)	t0__9a0		! t0__2t0_a0	! b_e_shift	! r__r_2t0
LSYM(x39)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x40)	t0__5a0		! a1_ne_0_b_l1	! r__r_8t0	! MILLIRETN
LSYM(x41)	t0__5a0		! a1_ne_0_b_l0	! t0__8t0_a0	! b_n_ret_t0
LSYM(x42)	t0__5a0		! t0__4t0_a0	! b_e_shift	! r__r_2t0
LSYM(x43)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x44)	t0__5a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x45)	t0__9a0		! a1_ne_0_b_l0	! t0__5t0	! b_n_ret_t0
LSYM(x46)	t0__9a0		! t0__5t0	! b_e_t0	! t0__t0_a0
LSYM(x47)	t0__9a0		! t0__5t0	! b_e_t0	! t0__t0_2a0
LSYM(x48)	t0__3a0		! a1_ne_0_b_l0	! t0__16t0	! b_n_ret_t0
LSYM(x49)	t0__9a0		! t0__5t0	! b_e_t0	! t0__t0_4a0
LSYM(x50)	t0__5a0		! t0__5t0	! b_e_shift	! r__r_2t0
LSYM(x51)	t0__9a0		! t0__t0_8a0	! b_e_t0	! t0__3t0
LSYM(x52)	t0__3a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x53)	t0__3a0		! t0__4t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x54)	t0__9a0		! t0__3t0	! b_e_shift	! r__r_2t0
LSYM(x55)	t0__9a0		! t0__3t0	! b_e_t0	! t0__2t0_a0
LSYM(x56)	t0__3a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x57)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__3t0
LSYM(x58)	t0__3a0		! t0__2t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x59)	t0__9a0		! t0__2t0_a0	! b_e_t02a0	! t0__3t0
LSYM(x60)	t0__5a0		! t0__3t0	! b_e_shift	! r__r_4t0
LSYM(x61)	t0__5a0		! t0__3t0	! b_e_t0	! t0__4t0_a0
LSYM(x62)	t0__32a0	! t0__t0ma0	! b_e_shift	! r__r_2t0
LSYM(x63)	t0__64a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
LSYM(x64)	t0__64a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x65)	t0__8a0		! a1_ne_0_b_l0	! t0__8t0_a0	! b_n_ret_t0
LSYM(x66)	t0__32a0	! t0__t0_a0	! b_e_shift	! r__r_2t0
LSYM(x67)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x68)	t0__8a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x69)	t0__8a0		! t0__2t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x70)	t0__64a0	! t0__t0_4a0	! b_e_t0	! t0__t0_2a0
LSYM(x71)	t0__9a0		! t0__8t0	! b_e_t0	! t0__t0ma0
LSYM(x72)	t0__9a0		! a1_ne_0_b_l1	! r__r_8t0	! MILLIRETN
LSYM(x73)	t0__9a0		! t0__8t0_a0	! b_e_shift	! r__r_t0
LSYM(x74)	t0__9a0		! t0__4t0_a0	! b_e_shift	! r__r_2t0
LSYM(x75)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x76)	t0__9a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x77)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x78)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x79)	t0__16a0	! t0__5t0	! b_e_t0	! t0__t0ma0
LSYM(x80)	t0__16a0	! t0__5t0	! b_e_shift	! r__r_t0
LSYM(x81)	t0__9a0		! t0__9t0	! b_e_shift	! r__r_t0
LSYM(x82)	t0__5a0		! t0__8t0_a0	! b_e_shift	! r__r_2t0
LSYM(x83)	t0__5a0		! t0__8t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x84)	t0__5a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x85)	t0__8a0		! t0__2t0_a0	! b_e_t0	! t0__5t0
LSYM(x86)	t0__5a0		! t0__4t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x87)	t0__9a0		! t0__9t0	! b_e_t02a0	! t0__t0_4a0
LSYM(x88)	t0__5a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x89)	t0__5a0		! t0__2t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x90)	t0__9a0		! t0__5t0	! b_e_shift	! r__r_2t0
LSYM(x91)	t0__9a0		! t0__5t0	! b_e_t0	! t0__2t0_a0
LSYM(x92)	t0__5a0		! t0__2t0_a0	! b_e_4t0	! t0__2t0_a0
LSYM(x93)	t0__32a0	! t0__t0ma0	! b_e_t0	! t0__3t0
LSYM(x94)	t0__9a0		! t0__5t0	! b_e_2t0	! t0__t0_2a0
LSYM(x95)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__5t0
LSYM(x96)	t0__8a0		! t0__3t0	! b_e_shift	! r__r_4t0
LSYM(x97)	t0__8a0		! t0__3t0	! b_e_t0	! t0__4t0_a0
LSYM(x98)	t0__32a0	! t0__3t0	! b_e_t0	! t0__t0_2a0
LSYM(x99)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__3t0
LSYM(x100)	t0__5a0		! t0__5t0	! b_e_shift	! r__r_4t0
LSYM(x101)	t0__5a0		! t0__5t0	! b_e_t0	! t0__4t0_a0
LSYM(x102)	t0__32a0	! t0__t0_2a0	! b_e_t0	! t0__3t0
LSYM(x103)	t0__5a0		! t0__5t0	! b_e_t02a0	! t0__4t0_a0
LSYM(x104)	t0__3a0		! t0__4t0_a0	! b_e_shift	! r__r_8t0
LSYM(x105)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__5t0
LSYM(x106)	t0__3a0		! t0__4t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x107)	t0__9a0		! t0__t0_4a0	! b_e_t02a0	! t0__8t0_a0
LSYM(x108)	t0__9a0		! t0__3t0	! b_e_shift	! r__r_4t0
LSYM(x109)	t0__9a0		! t0__3t0	! b_e_t0	! t0__4t0_a0
LSYM(x110)	t0__9a0		! t0__3t0	! b_e_2t0	! t0__2t0_a0
LSYM(x111)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__3t0
LSYM(x112)	t0__3a0		! t0__2t0_a0	! b_e_t0	! t0__16t0
LSYM(x113)	t0__9a0		! t0__4t0_a0	! b_e_t02a0	! t0__3t0
LSYM(x114)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__3t0
LSYM(x115)	t0__9a0		! t0__2t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x116)	t0__3a0		! t0__2t0_a0	! b_e_4t0	! t0__4t0_a0
LSYM(x117)	t0__3a0		! t0__4t0_a0	! b_e_t0	! t0__9t0
LSYM(x118)	t0__3a0		! t0__4t0_a0	! b_e_t0a0	! t0__9t0
LSYM(x119)	t0__3a0		! t0__4t0_a0	! b_e_t02a0	! t0__9t0
LSYM(x120)	t0__5a0		! t0__3t0	! b_e_shift	! r__r_8t0
LSYM(x121)	t0__5a0		! t0__3t0	! b_e_t0	! t0__8t0_a0
LSYM(x122)	t0__5a0		! t0__3t0	! b_e_2t0	! t0__4t0_a0
LSYM(x123)	t0__5a0		! t0__8t0_a0	! b_e_t0	! t0__3t0
LSYM(x124)	t0__32a0	! t0__t0ma0	! b_e_shift	! r__r_4t0
LSYM(x125)	t0__5a0		! t0__5t0	! b_e_t0	! t0__5t0
LSYM(x126)	t0__64a0	! t0__t0ma0	! b_e_shift	! r__r_2t0
LSYM(x127)	t0__128a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
LSYM(x128)	t0__128a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x129)	t0__128a0	! a1_ne_0_b_l0	! t0__t0_a0	! b_n_ret_t0
LSYM(x130)	t0__64a0	! t0__t0_a0	! b_e_shift	! r__r_2t0
LSYM(x131)	t0__8a0		! t0__8t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x132)	t0__8a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x133)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x134)	t0__8a0		! t0__4t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x135)	t0__9a0		! t0__5t0	! b_e_t0	! t0__3t0
LSYM(x136)	t0__8a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x137)	t0__8a0		! t0__2t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x138)	t0__8a0		! t0__2t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x139)	t0__8a0		! t0__2t0_a0	! b_e_2t0a0	! t0__4t0_a0
LSYM(x140)	t0__3a0		! t0__2t0_a0	! b_e_4t0	! t0__5t0
LSYM(x141)	t0__8a0		! t0__2t0_a0	! b_e_4t0a0	! t0__2t0_a0
LSYM(x142)	t0__9a0		! t0__8t0	! b_e_2t0	! t0__t0ma0
LSYM(x143)	t0__16a0	! t0__9t0	! b_e_t0	! t0__t0ma0
LSYM(x144)	t0__9a0		! t0__8t0	! b_e_shift	! r__r_2t0
LSYM(x145)	t0__9a0		! t0__8t0	! b_e_t0	! t0__2t0_a0
LSYM(x146)	t0__9a0		! t0__8t0_a0	! b_e_shift	! r__r_2t0
LSYM(x147)	t0__9a0		! t0__8t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x148)	t0__9a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x149)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x150)	t0__9a0		! t0__4t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x151)	t0__9a0		! t0__4t0_a0	! b_e_2t0a0	! t0__2t0_a0
LSYM(x152)	t0__9a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x153)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x154)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x155)	t0__32a0	! t0__t0ma0	! b_e_t0	! t0__5t0
LSYM(x156)	t0__9a0		! t0__2t0_a0	! b_e_4t0	! t0__2t0_a0
LSYM(x157)	t0__32a0	! t0__t0ma0	! b_e_t02a0	! t0__5t0
LSYM(x158)	t0__16a0	! t0__5t0	! b_e_2t0	! t0__t0ma0
LSYM(x159)	t0__32a0	! t0__5t0	! b_e_t0	! t0__t0ma0
LSYM(x160)	t0__5a0		! t0__4t0	! b_e_shift	! r__r_8t0
LSYM(x161)	t0__8a0		! t0__5t0	! b_e_t0	! t0__4t0_a0
LSYM(x162)	t0__9a0		! t0__9t0	! b_e_shift	! r__r_2t0
LSYM(x163)	t0__9a0		! t0__9t0	! b_e_t0	! t0__2t0_a0
LSYM(x164)	t0__5a0		! t0__8t0_a0	! b_e_shift	! r__r_4t0
LSYM(x165)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__5t0
LSYM(x166)	t0__5a0		! t0__8t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x167)	t0__5a0		! t0__8t0_a0	! b_e_2t0a0	! t0__2t0_a0
LSYM(x168)	t0__5a0		! t0__4t0_a0	! b_e_shift	! r__r_8t0
LSYM(x169)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x170)	t0__32a0	! t0__t0_2a0	! b_e_t0	! t0__5t0
LSYM(x171)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__9t0
LSYM(x172)	t0__5a0		! t0__4t0_a0	! b_e_4t0	! t0__2t0_a0
LSYM(x173)	t0__9a0		! t0__2t0_a0	! b_e_t02a0	! t0__9t0
LSYM(x174)	t0__32a0	! t0__t0_2a0	! b_e_t04a0	! t0__5t0
LSYM(x175)	t0__8a0		! t0__2t0_a0	! b_e_5t0	! t0__2t0_a0
LSYM(x176)	t0__5a0		! t0__4t0_a0	! b_e_8t0	! t0__t0_a0
LSYM(x177)	t0__5a0		! t0__4t0_a0	! b_e_8t0a0	! t0__t0_a0
LSYM(x178)	t0__5a0		! t0__2t0_a0	! b_e_2t0	! t0__8t0_a0
LSYM(x179)	t0__5a0		! t0__2t0_a0	! b_e_2t0a0	! t0__8t0_a0
LSYM(x180)	t0__9a0		! t0__5t0	! b_e_shift	! r__r_4t0
LSYM(x181)	t0__9a0		! t0__5t0	! b_e_t0	! t0__4t0_a0
LSYM(x182)	t0__9a0		! t0__5t0	! b_e_2t0	! t0__2t0_a0
LSYM(x183)	t0__9a0		! t0__5t0	! b_e_2t0a0	! t0__2t0_a0
LSYM(x184)	t0__5a0		! t0__9t0	! b_e_4t0	! t0__t0_a0
LSYM(x185)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__5t0
LSYM(x186)	t0__32a0	! t0__t0ma0	! b_e_2t0	! t0__3t0
LSYM(x187)	t0__9a0		! t0__4t0_a0	! b_e_t02a0	! t0__5t0
LSYM(x188)	t0__9a0		! t0__5t0	! b_e_4t0	! t0__t0_2a0
LSYM(x189)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__9t0
LSYM(x190)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__5t0
LSYM(x191)	t0__64a0	! t0__3t0	! b_e_t0	! t0__t0ma0
LSYM(x192)	t0__8a0		! t0__3t0	! b_e_shift	! r__r_8t0
LSYM(x193)	t0__8a0		! t0__3t0	! b_e_t0	! t0__8t0_a0
LSYM(x194)	t0__8a0		! t0__3t0	! b_e_2t0	! t0__4t0_a0
LSYM(x195)	t0__8a0		! t0__8t0_a0	! b_e_t0	! t0__3t0
LSYM(x196)	t0__8a0		! t0__3t0	! b_e_4t0	! t0__2t0_a0
LSYM(x197)	t0__8a0		! t0__3t0	! b_e_4t0a0	! t0__2t0_a0
LSYM(x198)	t0__64a0	! t0__t0_2a0	! b_e_t0	! t0__3t0
LSYM(x199)	t0__8a0		! t0__4t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x200)	t0__5a0		! t0__5t0	! b_e_shift	! r__r_8t0
LSYM(x201)	t0__5a0		! t0__5t0	! b_e_t0	! t0__8t0_a0
LSYM(x202)	t0__5a0		! t0__5t0	! b_e_2t0	! t0__4t0_a0
LSYM(x203)	t0__5a0		! t0__5t0	! b_e_2t0a0	! t0__4t0_a0
LSYM(x204)	t0__8a0		! t0__2t0_a0	! b_e_4t0	! t0__3t0
LSYM(x205)	t0__5a0		! t0__8t0_a0	! b_e_t0	! t0__5t0
LSYM(x206)	t0__64a0	! t0__t0_4a0	! b_e_t02a0	! t0__3t0
LSYM(x207)	t0__8a0		! t0__2t0_a0	! b_e_3t0	! t0__4t0_a0
LSYM(x208)	t0__5a0		! t0__5t0	! b_e_8t0	! t0__t0_a0
LSYM(x209)	t0__5a0		! t0__5t0	! b_e_8t0a0	! t0__t0_a0
LSYM(x210)	t0__5a0		! t0__4t0_a0	! b_e_2t0	! t0__5t0
LSYM(x211)	t0__5a0		! t0__4t0_a0	! b_e_2t0a0	! t0__5t0
LSYM(x212)	t0__3a0		! t0__4t0_a0	! b_e_4t0	! t0__4t0_a0
LSYM(x213)	t0__3a0		! t0__4t0_a0	! b_e_4t0a0	! t0__4t0_a0
LSYM(x214)	t0__9a0		! t0__t0_4a0	! b_e_2t04a0	! t0__8t0_a0
LSYM(x215)	t0__5a0		! t0__4t0_a0	! b_e_5t0	! t0__2t0_a0
LSYM(x216)	t0__9a0		! t0__3t0	! b_e_shift	! r__r_8t0
LSYM(x217)	t0__9a0		! t0__3t0	! b_e_t0	! t0__8t0_a0
LSYM(x218)	t0__9a0		! t0__3t0	! b_e_2t0	! t0__4t0_a0
LSYM(x219)	t0__9a0		! t0__8t0_a0	! b_e_t0	! t0__3t0
LSYM(x220)	t0__3a0		! t0__9t0	! b_e_4t0	! t0__2t0_a0
LSYM(x221)	t0__3a0		! t0__9t0	! b_e_4t0a0	! t0__2t0_a0
LSYM(x222)	t0__9a0		! t0__4t0_a0	! b_e_2t0	! t0__3t0
LSYM(x223)	t0__9a0		! t0__4t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x224)	t0__9a0		! t0__3t0	! b_e_8t0	! t0__t0_a0
LSYM(x225)	t0__9a0		! t0__5t0	! b_e_t0	! t0__5t0
LSYM(x226)	t0__3a0		! t0__2t0_a0	! b_e_t02a0	! t0__32t0
LSYM(x227)	t0__9a0		! t0__5t0	! b_e_t02a0	! t0__5t0
LSYM(x228)	t0__9a0		! t0__2t0_a0	! b_e_4t0	! t0__3t0
LSYM(x229)	t0__9a0		! t0__2t0_a0	! b_e_4t0a0	! t0__3t0
LSYM(x230)	t0__9a0		! t0__5t0	! b_e_5t0	! t0__t0_a0
LSYM(x231)	t0__9a0		! t0__2t0_a0	! b_e_3t0	! t0__4t0_a0
LSYM(x232)	t0__3a0		! t0__2t0_a0	! b_e_8t0	! t0__4t0_a0
LSYM(x233)	t0__3a0		! t0__2t0_a0	! b_e_8t0a0	! t0__4t0_a0
LSYM(x234)	t0__3a0		! t0__4t0_a0	! b_e_2t0	! t0__9t0
LSYM(x235)	t0__3a0		! t0__4t0_a0	! b_e_2t0a0	! t0__9t0
LSYM(x236)	t0__9a0		! t0__2t0_a0	! b_e_4t08a0	! t0__3t0
LSYM(x237)	t0__16a0	! t0__5t0	! b_e_3t0	! t0__t0ma0
LSYM(x238)	t0__3a0		! t0__4t0_a0	! b_e_2t04a0	! t0__9t0
LSYM(x239)	t0__16a0	! t0__5t0	! b_e_t0ma0	! t0__3t0
LSYM(x240)	t0__9a0		! t0__t0_a0	! b_e_8t0	! t0__3t0
LSYM(x241)	t0__9a0		! t0__t0_a0	! b_e_8t0a0	! t0__3t0
LSYM(x242)	t0__5a0		! t0__3t0	! b_e_2t0	! t0__8t0_a0
LSYM(x243)	t0__9a0		! t0__9t0	! b_e_t0	! t0__3t0
LSYM(x244)	t0__5a0		! t0__3t0	! b_e_4t0	! t0__4t0_a0
LSYM(x245)	t0__8a0		! t0__3t0	! b_e_5t0	! t0__2t0_a0
LSYM(x246)	t0__5a0		! t0__8t0_a0	! b_e_2t0	! t0__3t0
LSYM(x247)	t0__5a0		! t0__8t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x248)	t0__32a0	! t0__t0ma0	! b_e_shift	! r__r_8t0
LSYM(x249)	t0__32a0	! t0__t0ma0	! b_e_t0	! t0__8t0_a0
LSYM(x250)	t0__5a0		! t0__5t0	! b_e_2t0	! t0__5t0
LSYM(x251)	t0__5a0		! t0__5t0	! b_e_2t0a0	! t0__5t0
LSYM(x252)	t0__64a0	! t0__t0ma0	! b_e_shift	! r__r_4t0
LSYM(x253)	t0__64a0	! t0__t0ma0	! b_e_t0	! t0__4t0_a0
LSYM(x254)	t0__128a0	! t0__t0ma0	! b_e_shift	! r__r_2t0
LSYM(x255)	t0__256a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
/*1040 insts before this.  */
LSYM(ret_t0)	MILLIRET
LSYM(e_t0)	r__r_t0
LSYM(e_shift)	a1_ne_0_b_l2
	a0__256a0	/* a0 <<= 8 *********** */
	MILLIRETN
LSYM(e_t0ma0)	a1_ne_0_b_l0
	t0__t0ma0
	MILLIRET
	r__r_t0
LSYM(e_t0a0)	a1_ne_0_b_l0
	t0__t0_a0
	MILLIRET
	r__r_t0
LSYM(e_t02a0)	a1_ne_0_b_l0
	t0__t0_2a0
	MILLIRET
	r__r_t0
LSYM(e_t04a0)	a1_ne_0_b_l0
	t0__t0_4a0
	MILLIRET
	r__r_t0
LSYM(e_2t0)	a1_ne_0_b_l1
	r__r_2t0
	MILLIRETN
LSYM(e_2t0a0)	a1_ne_0_b_l0
	t0__2t0_a0
	MILLIRET
	r__r_t0
LSYM(e2t04a0)	t0__t0_2a0
	a1_ne_0_b_l1
	r__r_2t0
	MILLIRETN
LSYM(e_3t0)	a1_ne_0_b_l0
	t0__3t0
	MILLIRET
	r__r_t0
LSYM(e_4t0)	a1_ne_0_b_l1
	r__r_4t0
	MILLIRETN
LSYM(e_4t0a0)	a1_ne_0_b_l0
	t0__4t0_a0
	MILLIRET
	r__r_t0
LSYM(e4t08a0)	t0__t0_2a0
	a1_ne_0_b_l1
	r__r_4t0
	MILLIRETN
LSYM(e_5t0)	a1_ne_0_b_l0
	t0__5t0
	MILLIRET
	r__r_t0
LSYM(e_8t0)	a1_ne_0_b_l1
	r__r_8t0
	MILLIRETN
LSYM(e_8t0a0)	a1_ne_0_b_l0
	t0__8t0_a0
	MILLIRET
	r__r_t0

	.procend
	.end
#endif
