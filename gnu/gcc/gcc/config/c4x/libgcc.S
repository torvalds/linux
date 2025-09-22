/* libgcc routines for the Texas Instruments TMS320C[34]x
   Copyright (C) 1997,98, 1999 Free Software Foundation, Inc.

 Contributed by Michael Hayes (m.hayes@elec.canterbury.ac.nz)
            and Herman Ten Brugge (Haj.Ten.Brugge@net.HCC.nl).

	
This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

; These routines are called using the standard TI register argument
; passing model.
; The following registers do not have to be saved:
; r0, r1, r2, r3, ar0, ar1, ar2, ir0, ir1, bk, rs, rc, re, (r9, r10, r11)
;
; Perform floating point divqf3
;
; This routine performs a reciprocal of the divisor using the method
; described in the C30/C40 user manuals.  It then multiplies that
; result by the dividend.
; 
; Let r be the reciprocal of the divisor v and let the ith estimate
; of r be denoted by r[i].  An iterative approach can be used to
; improve the estimate of r, given an initial estimate r[0], where
;
; r[i + 1] = r[i] * (2.0 - v * r[i])
;
; The normalized error e[i] at the ith iteration is
;
; e[i] = (r - r[i]) / r = (1 / v - r[i]) * v = (1 - v * r[i])
;
; Note that 
;
; e[i + 1]  = (1 - v * r[i + 1]) = 1 - 2 * v * r[i] + v^2 + (r[i])^2
;           = (1 - v * r[i])^2 = (e[i])^2

; r2 dividend, r3 divisor, r0 quotient
; clobbers r1, ar1
#ifdef L_divsf3
	.text
        .global ___divqf3
___divqf3:

#ifdef _TMS320C4x
	.if .REGPARM == 0
	lda	sp,ar0
	ldf	*-ar0(2), r3
	.endif

	pop	ar1		; Pop return address

; r0 = estimate of r, r1 = tmp, r2 = dividend, r3 = divisor
        rcpf    r3, r0		; Compute initial estimate r[0]

	mpyf3	r0, r3, r1	; r1 = r[0] * v
	subrf	2.0, r1		; r1 = 2.0 - r[0] * v
	mpyf	r1, r0		; r0 = r[0] * (2.0 - r[0] * v) = r[1]
; End of 1st iteration (16 bits accuracy)

	mpyf3	r0, r3, r1	; r1 = r[1] * v
	subrf	2.0, r1		; r1 = 2.0 - r[1] * v

	bud	ar1		; Delayed branch
	mpyf	r1, r0		; r0 = r[1] * (2.0 - r[1] * v) = r[2]
; End of 2nd iteration (32 bits accuracy)
	.if .REGPARM == 0
	mpyf	*-ar0(1), r0	; Multiply by the dividend
	.else
	mpyf	r2, r0		; Multiply by the dividend
	.endif
	rnd	r0
	; Branch occurs here
#else
	.if .REGPARM == 0
	ldiu	sp,ar0
	ldf	*-ar0(2), r3
	.endif

	pop	ar1		; Pop return address

; Initial estimate       r[0] = 1.0 * 2^(-e - 1)
; where                  v = m * 2^e

; r0 = estimate of r, r1 = tmp, r2 = dividend, r3 = divisor

; Calculate initial estimate r[0]
	pushf	r3
	pop	r0
	not	r0		; r0 = -e
				; complement exponent = -e -1
				; complement sign (side effect)
				; complement mantissa (almost 3 bit accurate)
	push	r0
	popf	r0		; r0 = 1.0 * e^(-e - 1) + inverted mantissa
	ldf	-1.0, r1	; undo complement sign bit
	xor	r1, r0

	mpyf3	r0, r3, r1	; r1 = r[0] * v
	subrf	2.0, r1		; r1 = 2.0 - r[0] * v
	mpyf	r1, r0		; r0 = r[0] * (2.0 - r[0] * v) = r[1]
; End of 1st iteration

	mpyf3	r0, r3, r1	; r1 = r[1] * v
	subrf	2.0, r1		; r1 = 2.0 - r[1] * v
	mpyf	r1, r0		; r0 = r[1] * (2.0 - r[1] * v) = r[2]
; End of 2nd iteration

	mpyf3	r0, r3, r1	; r1 = r[2] * v
	subrf	2.0, r1		; r1 = 2.0 - r[2] * v
	mpyf	r1, r0		; r0 = r[2] * (2.0 - r[2] * v) = r[3]
; End of 3rd iteration

	rnd	r0		; Minimize error in x[3]'s LSBs

; Use modified last iteration
; r[4] = (r[3] * (1.0 - (v * r[3]))) + r[3]
	mpyf3	r0, r3, r1	; r1 = r[3] * v
	subrf	1.0, r1		; r1 = 1.0 - r[3] * v
	mpyf	r0, r1		; r1 = r[3] * (1.0 - r[3] * v)
	addf	r1, r0		; r0 = r[3] * (1.0 - r[3] * v) + r[3] = r[4]

        rnd     r0              ; Minimize error in x[4]'s LSBs

	bud	ar1		; Delayed branch

        .if .REGPARM == 0
        ldfu    *-ar0(1), r2    ; Dividend in mem has only 24 bits significance
        .else
        rnd     r2              ; Minimize error in reg dividend's LSBs
				; since this may have 32 bit significance
        .endif

        mpyf    r2, r0          ; Multiply by the dividend
        rnd     r0              ; Round result to 32 bits

	; Branch occurs here
#endif

#endif
;
; Integer signed division
;
; ar2 dividend, r2 divisor, r0 quotient
; clobbers r1, r3, ar0, ar1, ir0, ir1, rc, rs, re
#ifdef L_divsi3
	.text
	.global ___divqi3
	.ref	udivqi3n
___divqi3:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	ldi	*-ar0(2), r2
	.endif

	xor3	ar2, r2, r3	; Get the sign
	absi	ar2, r0
	bvd	divq32
	ldi	r0, ar2
	absi	r2, r2
	cmpi	ar2, r2		; Divisor > dividend?

	pop	ir1
	bhid	zero		; If so, return 0

;
; Normalize oeprands.  Use difference exponents as shift count
; for divisor, and as repeat count for "subc"
;
	float	ar2, r1		; Normalize dividend
	pushf	r1		; Get as integer
	pop	ar0
	lsh	-24, ar0	; Get exponent
	
	float	r2, r1		; Normalize divisor
	pushf	r1		; Get as integer
	pop	ir0
	lsh	-24, ir0	; Get exponent

	subi	ir0, ar0	; Get difference of exponents
	lsh	ar0, r2		; Align divisor with dividend

;
; Do count + 1 subtracts and shifts
;
	rpts	ar0
		subc	r2, ar2

;
; Mask off the lower count+1 bits of ar2
;
	subri	31, ar0		; Shift count is (32 - (ar0 + 1))
	lsh	ar0, ar2	; Shift left
	negi	ar0, ar0
	lsh3	ar0, ar2, r0	; Shift right and put result in r0

;
; Check sign and negate result if necessary
;
	bud	ir1		; Delayed return
	negi	r0, r1		; Negate result
	ash	-31, r3		; Check sign
	ldinz	r1, r0		; If set, use negative result
	; Branch occurs here

zero:	bud	ir1		; Delayed branch
	ldi	0, r0
	nop
	nop
	; Branch occurs here
;
; special case where ar2 = abs(ar2) = 0x80000000.  We handle this by
; calling unsigned divide and negating the result if necessary.
;
divq32:
	push	r3		; Save sign
	call	udivqi3n
	pop	r3
	pop	ir1
	bd	ir1
	negi	r0, r1		; Negate result
	ash	-31, r3		; Check sign
	ldinz	r1, r0		; If set, use negative result
	; Branch occurs here
#endif
;
;
; ar2 dividend, r2 divisor, r0 quotient, 
; clobbers r1, r3, ar0, ar1, ir0, ir1, rc, rs, re
#ifdef L_udivsi3
	.text
	.global ___udivqi3
	.global udivqi3n
___udivqi3:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	ldi	*-ar0(2), r2
	.endif

udivqi3n:
	pop	ir1

	cmpi	ar2, r2		; If divisor > dividend
	bhi	qzero		; return zero
	ldi	r2, ar1		; Store divisor in ar1

	tstb	ar2, ar2	; Check top bit, jump if set to special handler
	bld	div_32		; Delayed branch

;
; Get divisor exponent
;
	float	ar1, r1		; Normalize the divisor
	pushf	r1		; Get into int register
	pop	rc
	; branch occurs here

	bzd	qzero		; if (float) divisor zero, return zero

	float	ar2, r1		; Normalize the dividend
	pushf	r1		; Get into int register
	pop	ar0
	lsh	-24, ar0	; Get both the exponents
	lsh	-24, rc

	subi	rc, ar0		; Get the difference between the exponents
	lsh	ar0, ar1	; Normalize the divisor with the dividend

;
; Do count_1 subtracts and shifts
;
	rpts	ar0
		subc	ar1, ar2

;
; mask off the lower count+1 bits
;
	subri	31, ar0		; Shift count (31 - (ar0+1))
	bud	ir1		; Delayed return
	lsh3	ar0, ar2, r0
	negi	ar0, ar0
	lsh	ar0, r0
	; Branch occurs here

;
; Handle a full 32-bit dividend
;
div_32:	tstb	ar1, ar1
	bld	qone		; if divisor high bit is one, the result is one
	lsh	-24, rc
	subri	31, rc
	lsh	rc, ar1		; Line up the divisor

;
; Now divisor and dividend are aligned.  Do first SUBC by hand, save
; of the forst quotient digit.  Then, shift divisor right rather
; than shifting dividend left.  This leaves a zero in the top bit of
; the divident
;
	ldi	1, ar0		; Initizialize MSB of quotient
	lsh	rc, ar0		; create a mask for MSBs
	subi	1, ar0		; mask is (2 << count) - 1

	subi3	ar1, ar2, r1
	ldihs	r1, ar2
	ldihs	1, r1
	ldilo	0, r1
	lsh	rc, r1

	lsh	-1, ar1
	subi	1, rc
;
; do the rest of the shifts and subtracts
;
	rpts	rc
		subc	ar1, ar2

	bud	ir1
	and	ar0, ar2
	or3	r1, ar2, r0
	nop

qone:
	bud	ir1
	ldi	1, r0
	nop
	nop

qzero:
	bud	ir1
	ldi	0, r0
	nop
	nop
#endif

#ifdef L_umodsi3
	.text
	.global	___umodqi3
	.global	umodqi3n
___umodqi3:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	ldi	*-ar0(2), r2
	.endif

umodqi3n:
	pop     ir1		; return address
        cmpi    ar2, r2		; divisor > dividend ? 
	bhi     uzero		;    if so, return dividend
	ldi     r2, ar1		; load divisor
;
; If top bit of dividend is set, handle specially.
;
        tstb    ar2, ar2	; check top bit
	bld     umod_32		; get divisor exponent, then jump.
;
; Get divisor exponent by converting to float.
;
	float   ar1, r1		; normalize divisor
	pushf   r1		; push as float
	pop     rc		; pop as int to get exponent
        bzd     uzero		; if (float)divisor was zero, return
;
; 31 or less bits in dividend.  Get dividend exponent.
;
        float   ar2, r1		; normalize dividend
	pushf   r1		; push as float
	pop     ar0		; pop as int to get exponent
;
; Use difference in exponents as shift count to line up MSBs.
;
	lsh     -24, rc		; divisor exponent
	lsh     -24, ar0	; dividend exponent
	subi    rc, ar0		; difference
        lsh     ar0, ar1	; shift divisor up
; 
; Do COUNT+1 subtract & shifts.
;
	rpts    ar0
		subc    ar1, ar2  
;
;  Remainder is in upper 31-COUNT bits.
;
	bud     ir1		; delayed branch to return
	addi    1, ar0		; shift count is COUNT+1
	negi    ar0, ar0	; negate for right shift
	lsh3    ar0, ar2, r0	; shift to get result
	; Return occurs here

;
; The following code handles cases of a full 32-bit dividend.  Before
; SUBC can be used, the top bit must be cleared (otherwise SUBC can
; possibly shift a significant 1 out the top of the dividend).  This
; is accomplished by first doing a normal subtraction, then proceeding
; with SUBCs. 
;
umod_32:
;
; If the top bit of the divisor is set too, the remainder is simply
; the difference between the dividend and divisor.  Otherwise, shift 
; the divisor up to line up the MSBs.
;
	tstb    ar1, ar1	; check divisor
	bld     uone		; if negative, remainder is diff

	lsh     -24, rc		; divisor exponent
	subri   31, rc		; shift count = 31 - exp
	negi    rc, ar0		; used later as shift count
	lsh     rc, ar1		; shift up to line up MSBs
;
; Now MSBs are aligned.  Do first SUBC by hand using a plain subtraction.
; Then, shift divisor right rather than shifting dividend left.  This leaves
; a 0 in the top bit of the dividend.
;
	subi3   ar1, ar2, r1	; subtract 
	ldihs   r1, ar2		; if positive, replace dividend
	subi    1, rc		; first iteration is done
	lsh     -1, ar1		; shift divisor down
; 
; Do EXP subtract & shifts.
;
	rpts    rc  
		subc    ar1, ar2   
;
;  Quotient is in EXP+1 LSBs; shift remainder (in MSBs) down.
;
	bud	ir1
	lsh3    ar0, ar2, r0	; COUNT contains -(EXP+1)
	nop
	nop
;
;  Return (dividend - divisor).
;
uone:	bud	ir1
	subi3   r2, ar2, r0  
	nop
	nop
;
;  Return dividend.
;
uzero:	bud	ir1
	ldi     ar2, r0		; set status from result
	nop
	nop
#endif

#ifdef L_modsi3
	.text
	.global	___modqi3
	.ref umodqi3n
___modqi3:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	ldi	*-ar0(2), r2
	.endif

; 
; Determine sign of result.  Get absolute value of operands.
; 
	ldi     ar2, ar0	; sign of result same as dividend
	absi    ar2, r0		; make dividend positive
	bvd     mod_32		; if still negative, escape
	absi    r2, r1		; make divisor positive
	ldi     r1, ar1		; save in ar1       
        cmpi    r0, ar1		; divisor > dividend ? 

        pop     ir1            ; return address
	bhid    return 		;   if so, return dividend
; 
; Normalize operands.  Use difference in exponents as shift count
; for divisor, and as repeat count for SUBC.
;
        float   r1, r1		; normalize divisor
        pushf   r1		; push as float 
	pop     rc		; pop as int
        bzd     return		; if (float)divisor was zero, return

        float   r0, r1		; normalize dividend
        pushf   r1		; push as float
        pop     r1		; pop as int 

	lsh     -24, rc		; get divisor exponent
	lsh     -24, r1		; get dividend exponent
	subi    rc, r1		; get difference in exponents
	lsh     r1, ar1		; align divisor with dividend
; 
; Do COUNT+1 subtract & shifts.
;
	rpts    r1
		subc    ar1, r0
;
;  Remainder is in upper bits of R0
;
	addi    1, r1		; shift count is -(r1+1)
	negi    r1, r1 
	lsh     r1, r0		; shift right
;
;  Check sign and negate result if necessary.
;
return:
	bud     ir1		; delayed branch to return
        negi    r0, r1		; negate result
	cmpi    0, ar0		; check sign
	ldin    r1, r0		; if set, use negative result
	; Return occurs here
;
; The following code handles cases of a full 32-bit dividend.  This occurs
; when R0 = abs(R0) = 080000000h.  Handle this by calling the unsigned mod
; function, then negating the result if necessary.
;
mod_32:
        push    ar0		; remember sign
	call    umodqi3n	; do divide

	brd     return		; return
	pop     ar0		; restore sign
        pop     ir1             ; return address
	nop
#endif

#ifdef L_unsfltconst
	.section .const
        .global ___unsfltconst
___unsfltconst:   .float 4294967296.0
#endif

#ifdef L_unsfltcompare
	.section .const
        .global ___unsfltcompare
___unsfltcompare: .float 2147483648.0
#endif

; Integer 32-bit signed multiplication
;
; The TMS320C3x MPYI instruction takes two 24-bit signed integers
; and produces a 48-bit signed result which is truncated to 32-bits.
;
; A 32-bit by 32-bit multiplication thus requires a number of steps.
;
; Consider the product of two 32-bit signed integers,
;
;	z = x * y
;
; where x = (b << 16) + a,  y = (d << 16) + c
;
; This can be expressed as
;
;	z = ((b << 16) + a) * ((d << 16) + c)
;
;          = ((b * d) << 32) + ((b * c + a * d) << 16) + a * c
;
; Let z = (f << 16) + e where f < (1 << 16).
;
; Since we are only interested in a 32-bit result, we can ignore the 
; (b * d) << 32 term, and thus
;
;	f = b * c + a * d,  e = a * c
;
; We can simplify things if we have some a priori knowledge of the
; operands, for example, if -32768 <= y <= 32767, then y = c and d = 0 and thus
;
;	f = b * c,  e = a * c
;
; ar2 multiplier, r2 multiplicand, r0 product
; clobbers r1, r2, r3
#ifdef L_mulsi3	
	.text
	.global	___mulqi3
___mulqi3:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	ldi	*-ar0(2), r2
	.endif

        pop     ir1		; return address
	ldi	ar2, r0		;
	and	0ffffh, r0	; a
	lsh	-16, ar2	; b
	ldi	r2, r3		; 
	and	0ffffh, r3	; c
	mpyi	r3, ar2		; c * b		
	lsh	-16, r2		; d
	mpyi	r0, r2		; a * d
	addi	ar2, r2		; c * b + a * d
	bd	ir1		; delayed branch to return
	lsh	16, r2		; (c * b + a * d) << 16
	mpyi	r3, r0		; a * c
	addi	r2, r0		; a * c + (c * b + a * d) << 16
; branch occurs here

#endif	

;
; Integer 64 by 64 multiply
; long1 and long2 on stack
; result in r0,r1
;
#ifdef L_muldi3
	.text
	.global	___mulhi3
#ifdef _TMS320C4x
___mulhi3:
	pop	ar0
	ldi	sp,ar2
	ldi	*-ar2(1),r2
	ldi	*-ar2(3),r3
	mpyi3	r2,r3,r0
	mpyuhi3	r2,r3,r1
	mpyi	*-ar2(2),r2
	bd	ar0
	mpyi	*-ar2(0),r3
	addi	r2,r1
	addi	r3,r1
#else
___mulhi3:
	ldi	sp,ar2
	ldi	-16,rs
	ldi	*-ar2(2),ar0
	ldi	*-ar2(4),ar1
	ldi	ar0,r2
	and	0ffffh,r2
	ldi	ar1,r3
	and	0ffffh,r3
	lsh	rs,ar0
	lsh	rs,ar1

	mpyi	r2,r3,r0
	mpyi	ar0,ar1,r1
	mpyi	r2,ar1,rc
	lsh	rs,rc,re
	addi	re,r1
	lsh	16,rc
	addi	rc,r0
	addc	0,r1
	mpyi	r3,ar0,rc
	lsh	rs,rc,re
	addi	re,r1
	lsh	16,rc
	addi	rc,r0
	addc	0,r1

	ldi	*-ar2(1),ar0
	ldi	ar0,r2
	and	0ffffh,r2
	lsh	rs,ar0
	mpyi	r2,r3,rc
	addi	rc,r1
	mpyi	r2,ar1,rc
	mpyi	r3,ar0,re
	addi	re,rc
	lsh	16,rc
	addi	rc,r1

	ldi	*-ar2(2),ar0
	ldi	*-ar2(3),ar1
	ldi	ar0,r2
	and	0ffffh,r2
	ldi	ar1,r3
	and	0ffffh,r3
	lsh	rs,ar0
	lsh	rs,ar1
	mpyi	r2,r3,rc
	addi	rc,r1
	mpyi	r2,ar1,rc
	mpyi	r3,ar0,re
	pop	ar0
	bd	ar0
	addi	re,rc
	lsh	16,rc
	addi	rc,r1
#endif
#endif

;
; Integer 32 by 32 multiply highpart unsigned
; src1 in ar2
; src2 in r2
; result in r0
;
#ifdef L_umuldi3_high
	.text
	.global	___umulhi3_high
___umulhi3_high:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	ldi	*-ar0(2), r2
	.endif

	ldi	-16,rs
	ldi	r2,r3
	and	0ffffh,r2
	ldi	ar2,ar1
	and	0ffffh,ar2
	lsh	rs,r3
	lsh	rs,ar1

	mpyi	ar2,r2,r1
	mpyi	ar1,r3,r0
	mpyi	ar2,r3,rc
	lsh	rs,rc,re
	addi	re,r0
	lsh	16,rc
	addi	rc,r1
	addc	0,r0
	mpyi	r2,ar1,rc
	lsh	rs,rc,re
	addi	re,r0
	pop	ar0
	bd	ar0
	lsh	16,rc
	addi	rc,r1
	addc	0,r0
#endif

;
; Integer 32 by 32 multiply highpart signed
; src1 in ar2
; src2 in r2
; result in r0
;
#ifdef L_smuldi3_high
	.text
	.global	___smulhi3_high
___smulhi3_high:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	ldi	*-ar0(2), r2
	.endif

	ldi	-16,rs
	ldi	0,rc
	subi3	ar2,rc,r0
	ldi	r2,r3
	ldilt	r0,rc
	subi3	r2,rc,r0
	ldi	ar2,ar1
	tstb	ar1,ar1
	ldilt	r0,rc
	and	0ffffh,r2
	and	0ffffh,ar2
	lsh	rs,r3
	lsh	rs,ar1

	mpyi	ar2,r2,r1
	mpyi	ar1,r3,r0
	addi	rc,r0
	mpyi	ar2,r3,rc
	lsh	rs,rc,re
	addi	re,r0
	lsh	16,rc
	addi	rc,r1
	addc	0,r0
	mpyi	r2,ar1,rc
	lsh	rs,rc,re
	addi	re,r0
	pop	ar0
	bd	ar0
	lsh	16,rc
	addi	rc,r1
	addc	0,r0
#endif

;
; Integer 64 by 64 unsigned divide
; long1 and long2 on stack
; divide in r0,r1
; modulo in r2,r3
; routine takes a maximum of 64*8+23=535 cycles = 21.4 us @ 50Mhz
;
#ifdef L_udivdi3
	.text
	.global	___udivhi3
	.global	___udivide
	.global	___umodulo
	.ref udivqi3n
	.ref umodqi3n
___udivhi3:
	ldi	sp,ar2
	ldi     *-ar2(4),ar0
	ldi     *-ar2(3),ar1
	ldi     *-ar2(2),r0
	ldi     *-ar2(1),r1

___udivide:
	or	r1,ar1,r2
	bne	udiv0
	ldi	ar0,r2
	ldi	r0,ar2
	call	udivqi3n
	ldiu	0,r1
	rets

___umodulo:
	or	r1,ar1,r2
	bne	udiv0
	ldi	ar0,r2
	ldi	r0,ar2
	call	umodqi3n
	ldi	r0,r2
	ldiu	0,r3
	rets

udiv0:
	tstb	ar1,ar1
	bne	udiv1
	tstb	ar0,ar0
	bn	udiv1

	ldiu	63,rc
#ifdef _TMS320C4x
	rptbd	udivend0
	ldiu	0,r2
	addi	r0,r0
	rolc	r1
#else
	ldiu	0,r2
	addi	r0,r0
	rolc	r1
	rptb	udivend0
#endif

	rolc	r2
	subi3	ar0,r2,r3
	ldinc	r3,r2
	rolc	r0
udivend0:
	rolc	r1

	not	r0
	not	r1
	ldiu	0,r3
	rets
udiv1:
	push	r4
	push	r5
	ldiu	63,rc
	ldiu	0,r2
#ifdef _TMS320C4x
	rptbd	udivend1
	ldiu	0,r3
	addi	r0,r0
	rolc	r1
#else
	ldiu	0,r3
	addi	r0,r0
	rolc	r1
	rptb	udivend1
#endif

	rolc	r2
	rolc	r3
	subi3	ar0,r2,r4
	subb3	ar1,r3,r5
	ldinc	r4,r2
	ldinc	r5,r3
	rolc	r0
udivend1:
	rolc	r1

	not	r0
	not	r1
	pop	r5
	pop	r4
	rets
#endif

;
; Integer 64 by 64 unsigned modulo
; long1 and long2 on stack
; result in r0,r1
;
#ifdef L_umoddi3
	.text
	.global	___umodhi3
	.ref ___modulo
___umodhi3:
	ldi	sp,ar2
	ldi     *-ar2(4),ar0
	ldi     *-ar2(3),ar1
	ldi     *-ar2(2),r0
	ldi     *-ar2(1),r1
	call	___umodulo
	pop	ar0
	bd	ar0
	ldi	r2,r0
	ldi	r3,r1
	nop
#endif

;
; Integer 64 by 64 signed divide
; long1 and long2 on stack
; result in r0,r1
;
#ifdef L_divdi3
	.text
	.global	___divhi3
	.ref ___udivide
___divhi3:
	ldi	0,ir0
	ldi	sp,ar2
	ldi     *-ar2(4),r0
	ldi     *-ar2(3),r1
	bge	div1
	not	ir0
	negi	r0
	negb	r1
div1:
	ldi	r0,ar0
	ldi	r1,ar1
	ldi     *-ar2(2),r0
	ldi     *-ar2(1),r1
	bge	div2
	not	ir0
	negi	r0
	negb	r1
div2:
	call	___udivide
	tstb	ir0,ir0
	bge	div3
	negi	r0
	negb	r1
div3:	
	rets
#endif

;
; Integer 64 by 64 signed modulo
; long1 and long2 on stack
; result in r0,r1
;
#ifdef L_moddi3
	.text
	.global	___modhi3
	.ref ___umodulo
___modhi3:
	ldi	0,ir0
	ldi	sp,ar2
	ldi     *-ar2(4),r0
	ldi     *-ar2(3),r1
	bge	mod1
	not	ir0
	negi	r0
	negb	r1
mod1:
	ldi	r0,ar0
	ldi	r1,ar1
	ldi     *-ar2(2),r0
	ldi     *-ar2(1),r1
	bge	mod2
	not	ir0
	negi	r0
	negb	r1
mod2:
	call	___umodulo
	ldi	r2,r0
	ldi	r3,r1
	tstb	ir0,ir0
	bge	mod3
	negi	r0
	negb	r1
mod3:	
	rets
#endif

;
; double to signed long long conversion
; input in r2
; result in r0,r1
;
#ifdef L_fix_truncsfdi2
	.text
	.global	___fix_truncqfhi2
	.ref ufix_truncqfhi2n
___fix_truncqfhi2:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldf	*-ar0(1), r2
	.endif

	cmpf	0.0,r2
	bge	ufix_truncqfhi2n
	negf	r2
	call	ufix_truncqfhi2n
	negi	r0
	negb	r1
	rets
#endif

;
; double to unsigned long long conversion
; input in r2
; result in r0,r1
;
#ifdef L_ufix_truncsfdi2
	.text
	.global	___ufix_truncqfhi2
	.global	ufix_truncqfhi2n
___ufix_truncqfhi2:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldf	*-ar0(1), r2
	.endif

ufix_truncqfhi2n:
	cmpf	0.0,r2
	ble	ufix1
	pushf	r2
	pop	r3
	ash	-24,r3
	subi	31,r3
	cmpi	32,r3
	bgt	ufix1
	cmpi	-32,r3
	ble	ufix1
	ldi	1,r0
	ash	31,r0
	or3	r0,r2,r0
	ldi	r0,r1
	lsh3	r3,r0,r0
	subi	32,r3
	cmpi	-32,r3
	ldile	0,r1
	lsh3	r3,r1,r1
	rets
ufix1:
	ldi	0,r0
	ldi	0,r1
	rets
#endif

;
; signed long long to double conversion
; input on stack
; result in r0
;
#ifdef L_floatdisf2
	.text
	.global	___floathiqf2
	.ref ufloathiqf2n
___floathiqf2:
	ldi	sp,ar2
	ldi	*-ar2(2),r0
	ldi	*-ar2(1),r1
	bge	ufloathiqf2n
	negi	r0
	negb	r1
	call	ufloathiqf2n
	negf	r0
	rets
#endif

;
; unsigned long long to double conversion
; input on stack
; result in r0
;
#ifdef L_ufloatdisf2
	.text
	.global	___ufloathiqf2
	.global	ufloathiqf2n
	.ref ___unsfltconst
___ufloathiqf2:
	ldi	sp,ar2
	ldi	*-ar2(2),r0
	ldi	*-ar2(1),r1
ufloathiqf2n:
	.if .BIGMODEL
#ifdef _TMS320C4x
	ldpk	@___unsfltconst
#else
	ldp	@___unsfltconst
#endif
	.endif
	ldf	@___unsfltconst,r2
	float	r0
	bge	uflt1
	addf	r2,r0
uflt1:
	float	r1
	bge	uflt2
	addf	r2,r1
uflt2:
#ifdef _TMS320C4x
	pop	r3
	bd	r3
	mpyf	r2,r1
	addf	r1,r0
	nop
#else
	ldf	r1,r3
	and	0ffh,r3
	norm	r3,r3
	mpyf	r2,r3
	pop	ar2
	bd	ar2
	addf	r3,r0
	mpyf	r2,r1
	addf	r1,r0
#endif
#endif

;
; long double to signed long long conversion
; input in r2
; result in r0,r1
;
#ifdef L_fix_truncdfdi2
	.text
	.global	___fix_trunchfhi2
	.ref ufix_trunchfhi2n
___fix_trunchfhi2:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldf	*-ar0(2), r2
	ldi	*-ar0(1), r2
	.endif

	cmpf	0.0,r2
	bge	ufix_trunchfhi2n
	negf	r2
	call	ufix_trunchfhi2n
	negi	r0
	negb	r1
	rets
#endif

;
; long double to unsigned long long conversion
; input in r2
; result in r0,r1
;
#ifdef L_ufix_truncdfdi2
	.text
	.global	___ufix_trunchfhi2
	.global	ufix_trunchfhi2n
___ufix_trunchfhi2:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldf	*-ar0(2), r2
	ldi	*-ar0(1), r2
	.endif

ufix_trunchfhi2n:
	cmpf	0.0,r2
	ble	ufixh1
	pushf	r2
	pop	r3
	ash	-24,r3
	subi	31,r3
	cmpi	32,r3
	bgt	ufixh1
	cmpi	-32,r3
	ble	ufixh1
	ldi	1,r0
	ash	31,r0
	or3	r0,r2,r0
	ldi	r0,r1
	lsh3	r3,r0,r0
	subi	32,r3
	cmpi	-32,r3
	ldile	0,r1
	lsh3	r3,r1,r1
	rets
ufixh1:
	ldi	0,r0
	ldi	0,r1
	rets
#endif

;
; signed long long to long double conversion
; input on stack
; result in r0
;
#ifdef L_floatdidf2
	.text
	.global	___floathihf2
	.ref ufloathihf2n
___floathihf2:
	ldi	sp,ar2
	ldi	*-ar2(2),r0
	ldi	*-ar2(1),r1
	bge	ufloathihf2n
	negi	r0
	negb	r1
	call	ufloathihf2n
	negf	r0
	rets
#endif

;
; unsigned long long to double conversion
; input on stack
; result in r0
;
#ifdef L_ufloatdidf2
	.text
	.global	___ufloathihf2
	.global	ufloathihf2n
	.ref ___unsfltconst
___ufloathihf2:
	ldi	sp,ar2
	ldi	*-ar2(2),r0
	ldi	*-ar2(1),r1
ufloathihf2n
	.if .BIGMODEL
#ifdef _TMS320C4x
	ldpk	@___unsfltconst
#else
	ldp	@___unsfltconst
#endif
	.endif
	ldf	@___unsfltconst,r2
	float	r0
	bge	uflth1
	addf	r2,r0
uflth1:
	float	r1
	bge	uflth2
	addf	r2,r1
uflth2:
#ifdef _TMS320C4x
	pop	r3
	bd	r3
	mpyf	r2,r1
	addf	r1,r0
	nop
#else
	ldf	r1,r3
	and	0ffh,r3
	norm	r3,r3
	mpyf	r2,r3
	pop	ar2
	bd	ar2
	addf	r3,r0
	mpyf	r2,r1
	addf	r1,r0
#endif
#endif

;
; calculate ffs
; input in ar2
; result in r0
;
#ifdef L_ffs
	.global	___ffs
	.ref ___unsfltconst
	.text
___ffs:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldi	*-ar0(1), ar2
	.endif

	negi	ar2,r0
	and	ar2,r0
	float	r0,r0
	ldfu	0.0,r1
	.if .BIGMODEL
#ifdef _TMS320C4x
	ldpk	@___unsfltconst
#else
	ldp	@___unsfltconst
#endif
	.endif
	ldflt	@___unsfltconst,r1
	addf	r1,r0
	pushf	r0
	pop	r0
	pop	ar0
	bd	ar0
	ash	-24,r0
	ldilt	-1,r0
	addi	1,r0
#endif

;
; calculate long double * long double
; input in r2, r3
; output in r0
;
#ifdef L_muldf3
	.global ___mulhf3
	.text
___mulhf3:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldf	*-ar0(2), r2
	ldi	*-ar0(1), r2
	ldf	*-ar0(4), r3
	ldi	*-ar0(3), r3
	.endif

	pop	ar2		; return ad
	ldf	r2,r0		; copy lsb0
	ldf	r3,r1		; copy lsb1
	and	0ffh,r0		; mask lsb0
	and	0ffh,r1		; mask lsb1
	norm	r0,r0		; correct lsb0
	norm	r1,r1		; correct lsb1
	mpyf	r2,r1		; arg0*lsb1
	mpyf	r3,r0		; arg1*lsb0
	bd	ar2		; return (delayed)
	addf	r0,r1		; arg0*lsb1 + arg1*lsb0
	mpyf	r2,r3,r0	; msb0*msb1
	addf	r1,r0		; msb0*msb1 + arg0*lsb1 + arg1*lsb0
#endif

;
; calculate long double / long double
; r2 dividend, r3 divisor, r0 quotient
;
#ifdef L_divdf3
	.global ___divhf3
	.text
___divhf3:
	.if .REGPARM == 0
#ifdef _TMS320C4x
	lda	sp,ar0
#else
	ldiu	sp,ar0
#endif
	ldf	*-ar0(2), r2
	ldi	*-ar0(1), r2
	ldf	*-ar0(4), r3
	ldi	*-ar0(3), r3
	.endif

#ifdef _TMS320C4x
	pop	ar1
        rcpf    r3, r0
	mpyf3	r0, r3, r1
	subrf	2.0, r1		
	mpyf	r1, r0	
	mpyf3	r0, r3, r1
	bud	ar1
	subrf	2.0, r1	
	mpyf	r1, r0
	mpyf	r2, r0
#else
	pop	ar1
	pushf	r3
	pop	r0
	not	r0	
	push	r0
	popf	r0
	ldf	-1.0, r1
	xor	r1, r0

	mpyf3	r0, r3, r1	; r1 = r[0] * v
	subrf	2.0, r1		; r1 = 2.0 - r[0] * v
	mpyf	r1, r0		; r0 = r[0] * (2.0 - r[0] * v) = r[1]
; End of 1st iteration

	mpyf3	r0, r3, r1	; r1 = r[1] * v
	subrf	2.0, r1		; r1 = 2.0 - r[1] * v
	mpyf	r1, r0		; r0 = r[1] * (2.0 - r[1] * v) = r[2]
; End of 2nd iteration

	mpyf3	r0, r3, r1	; r1 = r[2] * v
	subrf	2.0, r1		; r1 = 2.0 - r[2] * v
	mpyf	r1, r0		; r0 = r[2] * (2.0 - r[2] * v) = r[3]
; End of 3rd iteration

	or	080h, r0
	rnd	r0

;	mpyf3	r0, r3, r1	; r1 = r[3] * v
	push	r4
	pushf	r4
	mpyf	r0, r3, r1

	ldf	r0, r4
	and	0ffh, r4
	norm	r4, r4
	mpyf	r3, r4
	addf	r4, r1

	ldf	r3, r4
	and	0ffh, r4
	norm 	r4, r4
	mpyf	r0, r4
	addf	r4, r1
	
	subrf	2.0, r1		; r1 = 2.0 - r[3] * v

	mpyf	r1, r0, r3	; r3 = r[3] * (2.0 - r[3] * v) = r[5]

	ldf	r1, r4
	and	0ffh, r4
	norm	r4, r4
	mpyf	r0, r4
	addf	r4, r3

	ldf	r0, r4
	and	0ffh, r4
	norm 	r4, r4
	mpyf	r1, r4
	addf	r4, r3

	mpyf	r2, r3, r0	; Multiply by the dividend

	ldf	r2, r4
	and	0ffh, r4
	norm	r4, r4
	mpyf	r3, r4
	addf	r4, r0

	ldf	r3, r4
	and	0ffh, r4
	norm 	r4, r4
	mpyf	r2, r4
	bd	ar1
	addf	r4, r0

	popf	r4
	pop	r4
#endif
#endif
