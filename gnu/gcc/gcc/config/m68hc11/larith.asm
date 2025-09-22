/* libgcc routines for M68HC11 & M68HC12.
   Copyright (C) 1999, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

	.file "larith.asm"

#ifdef __HAVE_SHORT_INT__
	.mode mshort
#else
	.mode mlong
#endif

	.macro declare_near name
	.globl \name
	.type  \name,@function
	.size  \name,.Lend-\name
\name:
	.endm

#if defined(__USE_RTC__)
# define ARG(N) N+1

	.macro ret
#if defined(mc68hc12)
	rtc
#else
	jmp __return_32
#endif
	.endm

	.macro declare name
	.globl \name
	.type  \name,@function
	.size  \name,.Lend-\name
	.far   \name
\name:
	.endm

	.macro farsym name
	.far NAME
	.endm

#else
# define ARG(N) N

	.macro ret
	rts
	.endm

	.macro farsym name
	.endm

	.macro declare name
	.globl \name
	.type  \name,@function
	.size  \name,.Lend-\name
\name:
	.endm

#endif

	.sect .text
	

#define REG(NAME)			\
NAME:	.dc.w	1;			\
	.type NAME,@object ;		\
	.size NAME,2

#ifdef L_regs_min
/* Pseudo hard registers used by gcc.
   They should be located in page0.  */

	.sect .softregs
	.globl _.tmp
	.globl _.z,_.xy
REG(_.tmp)
REG(_.z)
REG(_.xy)

#endif

#ifdef L_regs_frame
	.sect .softregs
	.globl _.frame
REG(_.frame)
#endif

#ifdef L_regs_d1_2
	.sect .softregs
	.globl _.d1,_.d2
REG(_.d1)
REG(_.d2)
#endif

#ifdef L_regs_d3_4
	.sect .softregs
	.globl _.d3,_.d4
REG(_.d3)
REG(_.d4)
#endif

#ifdef L_regs_d5_6
	.sect .softregs
	.globl _.d5,_.d6
REG(_.d5)
REG(_.d6)
#endif

#ifdef L_regs_d7_8
	.sect .softregs
	.globl _.d7,_.d8
REG(_.d7)
REG(_.d8)
#endif

#ifdef L_regs_d9_16
/* Pseudo hard registers used by gcc.
   They should be located in page0.  */
	.sect .softregs
	.globl _.d9,_.d10,_.d11,_.d12,_.d13,_.d14
	.globl _.d15,_.d16
REG(_.d9)
REG(_.d10)
REG(_.d11)
REG(_.d12)
REG(_.d13)
REG(_.d14)
REG(_.d15)
REG(_.d16)

#endif

#ifdef L_regs_d17_32
/* Pseudo hard registers used by gcc.
   They should be located in page0.  */
	.sect .softregs
	.globl _.d17,_.d18,_.d19,_.d20,_.d21,_.d22
	.globl _.d23,_.d24,_.d25,_.d26,_.d27,_.d28
	.globl _.d29,_.d30,_.d31,_.d32
REG(_.d17)
REG(_.d18)
REG(_.d19)
REG(_.d20)
REG(_.d21)
REG(_.d22)
REG(_.d23)
REG(_.d24)
REG(_.d25)
REG(_.d26)
REG(_.d27)
REG(_.d28)
REG(_.d29)
REG(_.d30)
REG(_.d31)
REG(_.d32)
#endif

#ifdef L_premain
;;
;; Specific initialization for 68hc11 before the main.
;; Nothing special for a generic routine; Just enable interrupts.
;;
	declare_near	__premain
	clra
	tap	; Clear both I and X.
	rts
#endif

#ifdef L__exit
;;
;; Exit operation.  Just loop forever and wait for interrupts.
;; (no other place to go)
;; This operation is split in several pieces collected together by
;; the linker script.  This allows to support destructors at the
;; exit stage while not impacting program sizes when there is no
;; destructors.
;;
;; _exit:
;;    *(.fini0)		/* Beginning of finish code (_exit symbol).  */
;;    *(.fini1)		/* Place holder for applications.  */
;;    *(.fini2)		/* C++ destructors.  */
;;    *(.fini3)		/* Place holder for applications.  */
;;    *(.fini4)		/* Runtime exit.  */
;;
	.sect .fini0,"ax",@progbits
	.globl _exit
	.globl exit
	.weak  exit
	farsym  exit
	farsym  _exit
exit:
_exit:

	.sect .fini4,"ax",@progbits
fatal:
	cli
	wai
	bra fatal
#endif

#ifdef L_abort
;;
;; Abort operation.  This is defined for the GCC testsuite.
;;
	declare	abort

	ldd	#255		; 
#ifdef mc68hc12
	trap	#0x30
#else
	.byte 0xCD		; Generate an illegal instruction trap
	.byte 0x03		; The simulator catches this and stops.
#endif
	jmp _exit
#endif
	
#ifdef L_cleanup
;;
;; Cleanup operation used by exit().
;;
	declare	_cleanup

	ret
#endif

;-----------------------------------------
; required gcclib code
;-----------------------------------------
#ifdef L_memcpy
       declare	memcpy
       declare	__memcpy

	.weak memcpy
;;;
;;; void* memcpy(void*, const void*, size_t)
;;; 
;;; D    = dst	Pmode
;;; 2,sp = src	Pmode
;;; 4,sp = size	HImode (size_t)
;;; 
#ifdef mc68hc12
	ldx	ARG(2),sp
	ldy	ARG(4),sp
	pshd
	xgdy
	lsrd
	bcc	Start
	movb	1,x+,1,y+
Start:
	beq	Done
Loop:
	movw	2,x+,2,y+
	dbne	d,Loop
Done:
	puld
	ret
#else
	xgdy
	tsx
	ldd	ARG(4),x
	ldx	ARG(2),x	; SRC = X, DST = Y
	cpd	#0
	beq	End
	pshy
	inca			; Correction for the deca below
L0:
	psha			; Save high-counter part
L1:
	ldaa	0,x		; Copy up to 256 bytes
	staa	0,y
	inx
	iny
	decb
	bne	L1
	pula
	deca
	bne	L0
	puly			; Restore Y to return the DST
End:
	xgdy
	ret
#endif
#endif

#ifdef L_memset
       declare	memset
       declare	__memset
;;;
;;; void* memset(void*, int value, size_t)
;;; 
#ifndef __HAVE_SHORT_INT__
;;; D    = dst	Pmode
;;; 2,sp = src	SImode
;;; 6,sp = size	HImode (size_t)
	val  = ARG(5)
	size = ARG(6)
#else
;;; D    = dst	Pmode
;;; 2,sp = src	SImode
;;; 6,sp = size	HImode (size_t)
	val  = ARG(3)
	size = ARG(4)
#endif
#ifdef mc68hc12
	xgdx
	ldab	val,sp
	ldy	size,sp
	pshx
	beq	End
Loop:
	stab	1,x+
	dbne	y,Loop
End:
	puld
	ret
#else
	xgdx
	tsy
	ldab	val,y
	ldy	size,y		; DST = X, CNT = Y
	beq	End
	pshx
L0:
	stab	0,x		; Fill up to 256 bytes
	inx
	dey
	bne	L0
	pulx			; Restore X to return the DST
End:
	xgdx
	ret
#endif
#endif

#ifdef L_adddi3
	declare	___adddi3

	tsx
	xgdy
	ldd	ARG(8),x		; Add LSB
	addd	ARG(16),x
	std	6,y		; Save (carry preserved)

	ldd	ARG(6),x
	adcb	ARG(15),x
	adca	ARG(14),x
	std	4,y

	ldd	ARG(4),x
	adcb	ARG(13),x
	adca	ARG(12),x
	std	2,y
	
	ldd	ARG(2),x
	adcb	ARG(11),x		; Add MSB
	adca	ARG(10),x
	std	0,y

	xgdy
	ret
#endif

#ifdef L_subdi3
	declare	___subdi3

	tsx
	xgdy
	ldd	ARG(8),x		; Subtract LSB
	subd	ARG(16),x
	std	6,y			; Save, borrow preserved

	ldd	ARG(6),x
	sbcb	ARG(15),x
	sbca	ARG(14),x
	std	4,y

	ldd	ARG(4),x
	sbcb	ARG(13),x
	sbca	ARG(12),x
	std	2,y
	
	ldd	ARG(2),x		; Subtract MSB
	sbcb	ARG(11),x
	sbca	ARG(10),x
	std	0,y

	xgdy			;
	ret
#endif
	
#ifdef L_notdi2
	declare	___notdi2

	tsy
	xgdx
	ldd	ARG(8),y
	coma
	comb
	std	6,x
	
	ldd	ARG(6),y
	coma
	comb
	std	4,x

	ldd	ARG(4),y
	coma
	comb
	std	2,x

	ldd	ARG(2),y
	coma
	comb
	std	0,x
	xgdx
	ret
#endif
	
#ifdef L_negsi2
	declare_near ___negsi2

	comb
	coma
	xgdx
	comb
	coma
	inx
	xgdx
	bne	done
	inx
done:
	rts
#endif

#ifdef L_one_cmplsi2
	declare_near ___one_cmplsi2

	comb
	coma
	xgdx
	comb
	coma
	xgdx
	rts
#endif
	
#ifdef L_ashlsi3
	declare_near ___ashlsi3

	xgdy
	clra
	andb	#0x1f
	xgdy
	beq	Return
Loop:
	lsld
	xgdx
	rolb
	rola
	xgdx
	dey
	bne	Loop
Return:
	rts
#endif

#ifdef L_ashrsi3
	declare_near ___ashrsi3

	xgdy
	clra
	andb	#0x1f
	xgdy
	beq	Return
Loop:
	xgdx
	asra
	rorb
	xgdx
	rora
	rorb
	dey
	bne	Loop
Return:
	rts
#endif

#ifdef L_lshrsi3
	declare_near ___lshrsi3

	xgdy
	clra
	andb	#0x1f
	xgdy
	beq	Return
Loop:
	xgdx
	lsrd
	xgdx
	rora
	rorb
	dey
	bne	Loop
Return:
	rts
#endif

#ifdef L_lshrhi3
	declare_near ___lshrhi3

	cpx	#16
	bge	Return_zero
	cpx	#0
	beq	Return
Loop:
	lsrd
	dex
	bne	Loop
Return:
	rts
Return_zero:
	clra
	clrb
	rts
#endif
	
#ifdef L_lshlhi3
	declare_near ___lshlhi3

	cpx	#16
	bge	Return_zero
	cpx	#0
	beq	Return
Loop:
	lsld
	dex
	bne	Loop
Return:
	rts
Return_zero:
	clra
	clrb
	rts
#endif

#ifdef L_rotrhi3
	declare_near ___rotrhi3

___rotrhi3:
	xgdx
	clra
	andb	#0x0f
	xgdx
	beq	Return
Loop:
	tap
	rorb
	rora
	dex
	bne	Loop
Return:
	rts
#endif

#ifdef L_rotlhi3
	declare_near ___rotlhi3

___rotlhi3:
	xgdx
	clra
	andb	#0x0f
	xgdx
	beq	Return
Loop:
	asrb
	rolb
	rola
	rolb
	dex
	bne	Loop
Return:
	rts
#endif

#ifdef L_ashrhi3
	declare_near ___ashrhi3

	cpx	#16
	bge	Return_minus_1_or_zero
	cpx	#0
	beq	Return
Loop:
	asra
	rorb
	dex
	bne	Loop
Return:
	rts
Return_minus_1_or_zero:
	clrb
	tsta
	bpl	Return_zero
	comb
Return_zero:
	tba
	rts
#endif
	
#ifdef L_ashrqi3
	declare_near ___ashrqi3

	cmpa	#8
	bge	Return_minus_1_or_zero
	tsta
	beq	Return
Loop:
	asrb
	deca
	bne	Loop
Return:
	rts
Return_minus_1_or_zero:
	clrb
	tstb
	bpl	Return_zero
	coma
Return_zero:
	tab
	rts
#endif

#ifdef L_lshlqi3
	declare_near ___lshlqi3

	cmpa	#8
	bge	Return_zero
	tsta
	beq	Return
Loop:
	lslb
	deca
	bne	Loop
Return:
	rts
Return_zero:
	clrb
	rts
#endif

#ifdef L_divmodhi4
#ifndef mc68hc12
/* 68HC12 signed divisions are generated inline (idivs).  */

	declare_near __divmodhi4

;
;; D = numerator
;; X = denominator
;;
;; Result:	D = D / X
;;		X = D % X
;; 
	tsta
	bpl	Numerator_pos
	comb			; D = -D <=> D = (~D) + 1
	coma
	xgdx
	inx
	tsta
	bpl	Numerator_neg_denominator_pos
Numerator_neg_denominator_neg:
	comb			; X = -X
	coma
	addd	#1
	xgdx
	idiv
	coma
	comb
	xgdx			; Remainder <= 0 and result >= 0
	inx
	rts

Numerator_pos_denominator_pos:
	xgdx
	idiv
	xgdx			; Both values are >= 0
	rts
	
Numerator_pos:
	xgdx
	tsta
	bpl	Numerator_pos_denominator_pos
Numerator_pos_denominator_neg:
	coma			; X = -X
	comb
	xgdx
	inx
	idiv
	xgdx			; Remainder >= 0 but result <= 0
	coma
	comb
	addd	#1
	rts
	
Numerator_neg_denominator_pos:
	xgdx
	idiv
	coma			; One value is > 0 and the other < 0
	comb			; Change the sign of result and remainder
	xgdx
	inx
	coma
	comb
	addd	#1
	rts
#endif /* !mc68hc12 */
#endif

#ifdef L_mulqi3
	declare_near ___mulqi3

;
; short __mulqi3(signed char a, signed char b);
;
;	signed char a	-> register A
;	signed char b	-> register B
;
; returns the signed result of A * B in register D.
;
	tsta
	bmi	A_neg
	tstb
	bmi	B_neg
	mul
	rts
B_neg:
	negb
	bra	A_or_B_neg
A_neg:
	nega
	tstb
	bmi	AB_neg
A_or_B_neg:
	mul
	coma
	comb
	addd	#1
	rts
AB_neg:
	negb
	mul
	rts
#endif
	
#ifdef L_mulhi3
	declare_near ___mulhi3

;
;
;  unsigned short ___mulhi3(unsigned short a, unsigned short b)
;
;	a = register D
;	b = register X
;
#ifdef mc68hc12
	pshx			; Preserve X
	exg	x,y
	emul
	exg	x,y
	pulx
	rts
#else
#ifdef NO_TMP
	;
	; 16 bit multiplication without temp memory location.
	; (smaller but slower)
	;
	pshx			; (4)
	ins			; (3)
	pshb			; (3)
	psha			; (3)
	pshx			; (4)
	pula			; (4)
	pulx			; (5)
	mul			; (10) B.high * A.low
	xgdx			; (3)
	mul			; (10) B.low * A.high
	abx			; (3)
	pula			; (4)
	pulb			; (4)
	mul			; (10) B.low * A.low
	pshx			; (4) 
	tsx			; (3)
	adda	1,x		; (4)
	pulx			; (5)
	rts			; (5) 20 bytes
				; ---
				; 91 cycles
#else
	stx	*_.tmp		; (4)
	pshb			; (3)
	ldab	*_.tmp+1	; (3)
	mul			; (10) A.high * B.low
	ldaa	*_.tmp		; (3)
	stab	*_.tmp		; (3)
	pulb			; (4)
	pshb			; (4)
	mul			; (10) A.low * B.high
	addb	*_.tmp		; (4)
	stab	*_.tmp		; (3)
	ldaa	*_.tmp+1	; (3)
	pulb			; (4)
	mul			; (10) A.low * B.low
	adda	*_.tmp		; (4)
	rts			; (5) 24/32 bytes
				; 77/85 cycles
#endif
#endif
#endif

#ifdef L_mulhi32

;
;
;  unsigned long __mulhi32(unsigned short a, unsigned short b)
;
;	a = register D
;	b = value on stack
;
;	+---------------+
;       |  B low	| <- 7,x
;	+---------------+
;       |  B high	| <- 6,x
;	+---------------+
;       |  PC low	|  
;	+---------------+
;       |  PC high	|  
;	+---------------+
;	|  Tmp low	|
;	+---------------+
;	|  Tmp high     |
;	+---------------+
;	|  A low	|
;	+---------------+
;	|  A high	|
;	+---------------+  <- 0,x
;
;
;      <B-low>    5,x
;      <B-high>   4,x
;      <ret>      2,x
;      <A-low>    1,x
;      <A-high>   0,x
;
	declare_near	__mulhi32

#ifdef mc68hc12
	ldy	2,sp
	emul
	exg	x,y
	rts
#else
	pshx			; Room for temp value
	pshb
	psha
	tsx
	ldab	6,x
	mul
	xgdy			; A.high * B.high
	ldab	7,x
	pula
	mul			; A.high * B.low
	std	2,x
	ldaa	1,x
	ldab	6,x
	mul			; A.low * B.high
	addd	2,x
	stab	2,x
	tab
	aby
	bcc	N
	ldab	#0xff
	aby
	iny
N:
	ldab	7,x
	pula
	mul			; A.low * B.low
	adda	2,x
	pulx			; Drop temp location
	pshy			; Put high part in X
	pulx
	bcc	Ret
	inx
Ret:
	rts
#endif	
#endif

#ifdef L_mulsi3

;
;      <B-low>    8,y
;      <B-high>   6,y
;      <ret>      4,y
;	<tmp>	  2,y
;      <A-low>    0,y
;
; D,X   -> A
; Stack -> B
;
; The result is:
;
;	(((A.low * B.high) + (A.high * B.low)) << 16) + (A.low * B.low)
;
;
;

	declare	__mulsi3

#ifdef mc68hc12
	pshd				; Save A.low
	ldy	ARG(4),sp
	emul				; A.low * B.high
	ldy	ARG(6),sp
	exg	x,d
	emul				; A.high * B.low
	leax	d,x
	ldy	ARG(6),sp
	puld
	emul				; A.low * B.low
	exg	d,y
	leax	d,x
	exg	d,y
	ret
#else
B_low	=	ARG(8)
B_high	=	ARG(6)
A_low	=	0
A_high	=	2
	pshx
	pshb
	psha
	tsy
;
; If B.low is 0, optimize into: (A.low * B.high) << 16
;
	ldd	B_low,y
	beq	B_low_zero
;
; If A.high is 0, optimize into: (A.low * B.high) << 16 + (A.low * B.low)
;
	cpx	#0
	beq	A_high_zero
	bsr	___mulhi3		; A.high * B.low
;
; If A.low is 0, optimize into: (A.high * B.low) << 16
;
	ldx	A_low,y
	beq	A_low_zero		; X = 0, D = A.high * B.low
	std	2,y
;
; If B.high is 0, we can avoid the (A.low * B.high) << 16 term.
;
	ldd	B_high,y
	beq	B_high_zero
	bsr	___mulhi3		; A.low * B.high
	addd	2,y
	std	2,y
;
; Here, we know that A.low and B.low are not 0.
;
B_high_zero:
	ldd	B_low,y			; A.low is on the stack
	bsr	__mulhi32		; A.low * B.low
	xgdx
	tsy				; Y was clobbered, get it back
	addd	2,y
A_low_zero:				; See A_low_zero_non_optimized below
	xgdx
Return:
	ins
	ins
	ins
	ins
	ret
;
; 
; A_low_zero_non_optimized:
;
; At this step, X = 0 and D = (A.high * B.low)
; Optimize into: (A.high * B.low) << 16
;
;	xgdx
;	clra			; Since X was 0, clearing D is superfuous.
;	clrb
;	bra	Return
; ----------------
; B.low == 0, the result is:	(A.low * B.high) << 16
;
; At this step:
;   D = B.low				= 0 
;   X = A.high				?
;       A.low is at A_low,y		?
;       B.low is at B_low,y		?
;
B_low_zero:
	ldd	A_low,y
	beq	Zero1
	ldx	B_high,y
	beq	Zero2
	bsr	___mulhi3
Zero1:
	xgdx
Zero2:
	clra
	clrb
	bra	Return
; ----------------
; A.high is 0, optimize into: (A.low * B.high) << 16 + (A.low * B.low)
;
; At this step:
;   D = B.low				!= 0 
;   X = A.high				= 0
;       A.low is at A_low,y		?
;       B.low is at B_low,y		?
;
A_high_zero:
	ldd	A_low,y		; A.low
	beq	Zero1
	ldx	B_high,y	; B.high
	beq	A_low_B_low
	bsr	___mulhi3
	std	2,y
	bra	B_high_zero	; Do the (A.low * B.low) and the add.

; ----------------
; A.high and B.high are 0 optimize into: (A.low * B.low)
;
; At this step:
;   D = B.high				= 0 
;   X = A.low				!= 0
;       A.low is at A_low,y		!= 0
;       B.high is at B_high,y		= 0
;
A_low_B_low:
	ldd	B_low,y			; A.low is on the stack
	bsr	__mulhi32
	bra	Return
#endif
#endif

#ifdef L_map_data

	.sect	.install2,"ax",@progbits
	.globl	__map_data_section
	.globl __data_image
#ifdef mc68hc12
	.globl __data_section_size
#endif
__map_data_section:
#ifdef mc68hc12
	ldx	#__data_image
	ldy	#__data_section_start
	ldd	#__data_section_size
	beq	Done
Loop:
	movb	1,x+,1,y+
	dbne	d,Loop
#else
	ldx	#__data_image
	ldy	#__data_section_start
	bra	Start_map
Loop:
	ldaa	0,x
	staa	0,y
	inx
	iny
Start_map:
	cpx	#__data_image_end
	blo	Loop
#endif
Done:

#endif

#ifdef L_init_bss

	.sect	.install2,"ax",@progbits
	.globl	__init_bss_section

__init_bss_section:
	ldd	#__bss_size
	beq	Done
	ldx	#__bss_start
Loop:
#ifdef mc68hc12
	clr	1,x+
	dbne	d,Loop
#else
	clr	0,x
	inx
	subd	#1
	bne	Loop
#endif
Done:

#endif

#ifdef L_ctor

; End of constructor table
	.sect	.install3,"ax",@progbits
	.globl	__do_global_ctors

__do_global_ctors:
	; Start from the end - sizeof(void*)
	ldx	#__CTOR_END__-2
ctors_loop:
	cpx	#__CTOR_LIST__
	blo	ctors_done
	pshx
	ldx	0,x
	jsr	0,x
	pulx
	dex
	dex
	bra	ctors_loop
ctors_done:

#endif

#ifdef L_dtor

	.sect	.fini3,"ax",@progbits
	.globl	__do_global_dtors

;;
;; This piece of code is inserted in the _exit() code by the linker.
;;
__do_global_dtors:
	pshb	; Save exit code
	psha
	ldx	#__DTOR_LIST__
dtors_loop:
	cpx	#__DTOR_END__
	bhs	dtors_done
	pshx
	ldx	0,x
	jsr	0,x
	pulx
	inx
	inx
	bra	dtors_loop
dtors_done:
	pula	; Restore exit code
	pulb

#endif

#ifdef L_far_tramp
#ifdef mc68hc12
	.sect	.tramp,"ax",@progbits
	.globl	__far_trampoline

;; This is a trampoline used by the linker to invoke a function
;; using rtc to return and being called with jsr/bsr.
;; The trampoline generated is:
;;
;;	foo_tramp:
;;		ldy	#foo
;;		call	__far_trampoline,page(foo)
;;
;; The linker transforms:
;;
;;		jsr	foo
;;
;; into
;;		jsr	foo_tramp
;;
;; The linker generated trampoline and _far_trampoline must be in 
;; non-banked memory.
;;
__far_trampoline:
	movb	0,sp, 2,sp	; Copy page register below the caller's return
	leas	2,sp		; address.
	jmp	0,y		; We have a 'call/rtc' stack layout now
				; and can jump to the far handler
				; (whose memory bank is mapped due to the
				; call to the trampoline).
#endif

#ifdef mc68hc11
	.sect	.tramp,"ax",@progbits
	.globl __far_trampoline

;; Trampoline generated by gcc for 68HC11:
;;
;;	pshb
;;	ldab	#%page(func)
;;	ldy	#%addr(func)
;;	jmp	__far_trampoline
;;
__far_trampoline:
	psha				; (2) Save function parameter (high)
	;; <Read current page in A>
	psha				; (2)
	;; <Set currenge page from B>
	pshx				; (4)
	tsx				; (3)
	ldab	4,x			; (4) Restore function parameter (low)
	ldaa	2,x			; (4) Get saved page number
	staa	4,x			; (4) Save it below return PC
	pulx				; (5)
	pula				; (3)
	pula				; (3) Restore function parameter (high)
	jmp	0,y			; (4)
#endif
#endif

#ifdef L_call_far
#ifdef mc68hc11
	.sect	.tramp,"ax",@progbits
	.globl __call_a16
	.globl __call_a32
;;
;; The call methods are used for 68HC11 to support memory bank switching.
;; Every far call is redirected to these call methods.  Its purpose is to:
;;
;;  1/ Save the current page on the stack (1 byte to follow 68HC12 call frame)
;;  2/ Install the new page
;;  3/ Jump to the real function
;;
;; The page switching (get/save) is board dependent.  The default provided
;; here does nothing (just create the appropriate call frame).
;;
;; Call sequence (10 bytes, 13 cycles):
;;
;;	ldx #page			; (3)
;;	ldy #func			; (4)
;;	jsr __call_a16			; (6)
;;
;; Call trampoline (11 bytes, 19 cycles):
;;
__call_a16:
	;; xgdx				; (3)
	;; <Read current page in A>	; (3) ldaa _current_page
	psha				; (2)
	;; <Set current page from B>	; (4) staa _current_page
	;; xgdx				; (3)
	jmp 0,y				; (4)

;;
;; Call sequence (10 bytes, 14 cycles):
;;
;;	pshb				; (2)
;;	ldab #page			; (2)
;;	ldy  #func			; (4)
;;	jsr __call_a32			; (6)
;;
;; Call trampoline (87 bytes, 57 cycles):
;;
__call_a32:
	pshx				; (4)
	psha				; (2)
	;; <Read current page in A>	; (3) ldaa _current_page
	psha				; (2)
	;; <Set current page from B>	; (4) staa _current_page
	tsx				; (3)
	ldab	6,x			; (4) Restore function parameter
	ldaa	5,x			; (4) Move PC return at good place
	staa	6,x			; (4)
	ldaa	4,x			; (4)
	staa	5,x			; (4)
	pula				; (3)
	staa	4,x			; (4)
	pula				; (3)
	pulx				; (5)
	jmp	0,y			; (4)
#endif
#endif

#ifdef L_return_far
#ifdef mc68hc11
	.sect	.tramp,"ax",@progbits
       .globl __return_void
       .globl __return_16
       .globl __return_32

__return_void:
	;; pulb
	;; <Set current page from B> (Board specific)
	;; rts
__return_16:
	;; xgdx
	;; pulb
	;; <Set current page from B> (Board specific)
	;; xgdx
	;; rts
__return_32:
	;; xgdy
	;; pulb
	;; <Set current page from B> (Board specific)
	;; xgdy
	;; rts
	ins
	rts
#endif
#endif
.Lend:
;-----------------------------------------
; end required gcclib code
;-----------------------------------------
