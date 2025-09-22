/*  -*- Mode: Asm -*-  */
/* Copyright (C) 1998, 1999, 2000 Free Software Foundation, Inc.
   Contributed by Denis Chertykov <denisc@overta.ru>

This file is free software; you can redistribute it and/or modify it
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

#define __zero_reg__ r1
#define __tmp_reg__ r0
#define __SREG__ 0x3f
#define __SP_H__ 0x3e
#define __SP_L__ 0x3d

/* Most of the functions here are called directly from avr.md
   patterns, instead of using the standard libcall mechanisms.
   This can make better code because GCC knows exactly which
   of the call-used registers (not all of them) are clobbered.  */

	.section .text.libgcc, "ax", @progbits

	.macro	mov_l  r_dest, r_src
#if defined (__AVR_HAVE_MOVW__)
	movw	\r_dest, \r_src
#else
	mov	\r_dest, \r_src
#endif
	.endm

	.macro	mov_h  r_dest, r_src
#if defined (__AVR_HAVE_MOVW__)
	; empty
#else
	mov	\r_dest, \r_src
#endif
	.endm

/* Note: mulqi3, mulhi3 are open-coded on the enhanced core.  */
#if !defined (__AVR_ENHANCED__)
/*******************************************************
               Multiplication  8 x 8
*******************************************************/
#if defined (L_mulqi3)

#define	r_arg2	r22		/* multiplicand */
#define	r_arg1 	r24		/* multiplier */
#define r_res	__tmp_reg__	/* result */

	.global	__mulqi3
	.func	__mulqi3
__mulqi3:
	clr	r_res		; clear result
__mulqi3_loop:
	sbrc	r_arg1,0
	add	r_res,r_arg2
	add	r_arg2,r_arg2	; shift multiplicand
	breq	__mulqi3_exit	; while multiplicand != 0
	lsr	r_arg1		; 
	brne	__mulqi3_loop	; exit if multiplier = 0
__mulqi3_exit:	
	mov	r_arg1,r_res	; result to return register
	ret

#undef r_arg2  
#undef r_arg1  
#undef r_res   
	
.endfunc
#endif 	/* defined (L_mulqi3) */

#if defined (L_mulqihi3)
	.global	__mulqihi3
	.func	__mulqihi3
__mulqihi3:
	clr	r25
	sbrc	r24, 7
	dec	r25
	clr	r23
	sbrc	r22, 7
	dec	r22
	rjmp	__mulhi3
	.endfunc
#endif /* defined (L_mulqihi3) */

#if defined (L_umulqihi3)
	.global	__umulqihi3
	.func	__umulqihi3
__umulqihi3:
	clr	r25
	clr	r23
	rjmp	__mulhi3
	.endfunc
#endif /* defined (L_umulqihi3) */

/*******************************************************
               Multiplication  16 x 16
*******************************************************/
#if defined (L_mulhi3)
#define	r_arg1L	r24		/* multiplier Low */
#define	r_arg1H	r25		/* multiplier High */
#define	r_arg2L	r22		/* multiplicand Low */
#define	r_arg2H	r23		/* multiplicand High */
#define r_resL	__tmp_reg__	/* result Low */
#define r_resH  r21		/* result High */

	.global	__mulhi3
	.func	__mulhi3
__mulhi3:
	clr	r_resH		; clear result
	clr	r_resL		; clear result
__mulhi3_loop:
	sbrs	r_arg1L,0
	rjmp	__mulhi3_skip1
	add	r_resL,r_arg2L	; result + multiplicand
	adc	r_resH,r_arg2H
__mulhi3_skip1:	
	add	r_arg2L,r_arg2L	; shift multiplicand
	adc	r_arg2H,r_arg2H

	cp	r_arg2L,__zero_reg__
	cpc	r_arg2H,__zero_reg__
	breq	__mulhi3_exit	; while multiplicand != 0

	lsr	r_arg1H		; gets LSB of multiplier
	ror	r_arg1L
	sbiw	r_arg1L,0
	brne	__mulhi3_loop	; exit if multiplier = 0
__mulhi3_exit:
	mov	r_arg1H,r_resH	; result to return register
	mov	r_arg1L,r_resL
	ret

#undef r_arg1L
#undef r_arg1H
#undef r_arg2L
#undef r_arg2H
#undef r_resL 	
#undef r_resH 

.endfunc
#endif /* defined (L_mulhi3) */
#endif /* !defined (__AVR_ENHANCED__) */

#if defined (L_mulhisi3)
	.global	__mulhisi3
	.func	__mulhisi3
__mulhisi3:
	mov_l	r18, r24
	mov_h	r19, r25
	clr	r24
	sbrc	r23, 7
	dec	r24
	mov	r25, r24
	clr	r20
	sbrc	r19, 7
	dec	r20
	mov	r21, r20
	rjmp	__mulsi3
	.endfunc
#endif /* defined (L_mulhisi3) */

#if defined (L_umulhisi3)
	.global	__umulhisi3
	.func	__umulhisi3
__umulhisi3:
	mov_l	r18, r24
	mov_h	r19, r25
	clr	r24
	clr	r25
	clr	r20
	clr	r21
	rjmp	__mulsi3
	.endfunc
#endif /* defined (L_umulhisi3) */

#if defined (L_mulsi3)
/*******************************************************
               Multiplication  32 x 32
*******************************************************/
#define r_arg1L  r22		/* multiplier Low */
#define r_arg1H  r23
#define	r_arg1HL r24
#define	r_arg1HH r25		/* multiplier High */


#define	r_arg2L  r18		/* multiplicand Low */
#define	r_arg2H  r19	
#define	r_arg2HL r20
#define	r_arg2HH r21		/* multiplicand High */
	
#define r_resL	 r26		/* result Low */
#define r_resH   r27
#define r_resHL	 r30
#define r_resHH  r31		/* result High */

	
	.global	__mulsi3
	.func	__mulsi3
__mulsi3:
#if defined (__AVR_ENHANCED__)
	mul	r_arg1L, r_arg2L
	movw	r_resL, r0
	mul	r_arg1H, r_arg2H
	movw	r_resHL, r0
	mul	r_arg1HL, r_arg2L
	add	r_resHL, r0
	adc	r_resHH, r1
	mul	r_arg1L, r_arg2HL
	add	r_resHL, r0
	adc	r_resHH, r1
	mul	r_arg1HH, r_arg2L
	add	r_resHH, r0
	mul	r_arg1HL, r_arg2H
	add	r_resHH, r0
	mul	r_arg1H, r_arg2HL
	add	r_resHH, r0
	mul	r_arg1L, r_arg2HH
	add	r_resHH, r0
	clr	r_arg1HH	; use instead of __zero_reg__ to add carry
	mul	r_arg1H, r_arg2L
	add	r_resH, r0
	adc	r_resHL, r1
	adc	r_resHH, r_arg1HH ; add carry
	mul	r_arg1L, r_arg2H
	add	r_resH, r0
	adc	r_resHL, r1
	adc	r_resHH, r_arg1HH ; add carry
	movw	r_arg1L, r_resL
	movw	r_arg1HL, r_resHL
	clr	r1		; __zero_reg__ clobbered by "mul"
	ret
#else
	clr	r_resHH		; clear result
	clr	r_resHL		; clear result
	clr	r_resH		; clear result
	clr	r_resL		; clear result
__mulsi3_loop:
	sbrs	r_arg1L,0
	rjmp	__mulsi3_skip1
	add	r_resL,r_arg2L		; result + multiplicand
	adc	r_resH,r_arg2H
	adc	r_resHL,r_arg2HL
	adc	r_resHH,r_arg2HH
__mulsi3_skip1:
	add	r_arg2L,r_arg2L		; shift multiplicand
	adc	r_arg2H,r_arg2H
	adc	r_arg2HL,r_arg2HL
	adc	r_arg2HH,r_arg2HH
	
	lsr	r_arg1HH	; gets LSB of multiplier
	ror	r_arg1HL
	ror	r_arg1H
	ror	r_arg1L
	brne	__mulsi3_loop
	sbiw	r_arg1HL,0
	cpc	r_arg1H,r_arg1L
	brne	__mulsi3_loop		; exit if multiplier = 0
__mulsi3_exit:
	mov_h	r_arg1HH,r_resHH	; result to return register
	mov_l	r_arg1HL,r_resHL
	mov_h	r_arg1H,r_resH
	mov_l	r_arg1L,r_resL
	ret
#endif /* !defined (__AVR_ENHANCED__) */
#undef r_arg1L 
#undef r_arg1H 
#undef r_arg1HL
#undef r_arg1HH
             
             
#undef r_arg2L 
#undef r_arg2H 
#undef r_arg2HL
#undef r_arg2HH
             
#undef r_resL  
#undef r_resH  
#undef r_resHL 
#undef r_resHH 

.endfunc
#endif /* defined (L_mulsi3) */
	
/*******************************************************
       Division 8 / 8 => (result + remainder)
*******************************************************/
#define	r_rem	r25	/* remainder */
#define	r_arg1	r24	/* dividend, quotient */
#define	r_arg2	r22	/* divisor */
#define	r_cnt	r23	/* loop count */

#if defined (L_udivmodqi4)
	.global	__udivmodqi4
	.func	__udivmodqi4
__udivmodqi4:
	sub	r_rem,r_rem	; clear remainder and carry
	ldi	r_cnt,9		; init loop counter
	rjmp	__udivmodqi4_ep	; jump to entry point
__udivmodqi4_loop:
	rol	r_rem		; shift dividend into remainder
	cp	r_rem,r_arg2	; compare remainder & divisor
	brcs	__udivmodqi4_ep	; remainder <= divisor
	sub	r_rem,r_arg2	; restore remainder
__udivmodqi4_ep:
	rol	r_arg1		; shift dividend (with CARRY)
	dec	r_cnt		; decrement loop counter
	brne	__udivmodqi4_loop
	com	r_arg1		; complement result 
				; because C flag was complemented in loop
	ret
	.endfunc
#endif /* defined (L_udivmodqi4) */

#if defined (L_divmodqi4)
	.global	__divmodqi4
	.func	__divmodqi4
__divmodqi4:
        bst     r_arg1,7	; store sign of dividend
        mov     __tmp_reg__,r_arg1
        eor     __tmp_reg__,r_arg2; r0.7 is sign of result
        sbrc	r_arg1,7
	neg     r_arg1		; dividend negative : negate
        sbrc	r_arg2,7
	neg     r_arg2		; divisor negative : negate
	rcall	__udivmodqi4	; do the unsigned div/mod
	brtc	__divmodqi4_1
	neg	r_rem		; correct remainder sign
__divmodqi4_1:
	sbrc	__tmp_reg__,7
	neg	r_arg1		; correct result sign
__divmodqi4_exit:
	ret
	.endfunc
#endif /* defined (L_divmodqi4) */

#undef r_rem
#undef r_arg1
#undef r_arg2
#undef r_cnt
	
		
/*******************************************************
       Division 16 / 16 => (result + remainder)
*******************************************************/
#define	r_remL	r26	/* remainder Low */
#define	r_remH	r27	/* remainder High */

/* return: remainder */
#define	r_arg1L	r24	/* dividend Low */
#define	r_arg1H	r25	/* dividend High */

/* return: quotient */
#define	r_arg2L	r22	/* divisor Low */
#define	r_arg2H	r23	/* divisor High */
	
#define	r_cnt	r21	/* loop count */

#if defined (L_udivmodhi4)
	.global	__udivmodhi4
	.func	__udivmodhi4
__udivmodhi4:
	sub	r_remL,r_remL
	sub	r_remH,r_remH	; clear remainder and carry
	ldi	r_cnt,17	; init loop counter
	rjmp	__udivmodhi4_ep	; jump to entry point
__udivmodhi4_loop:
        rol	r_remL		; shift dividend into remainder
	rol	r_remH
        cp	r_remL,r_arg2L	; compare remainder & divisor
	cpc	r_remH,r_arg2H
        brcs	__udivmodhi4_ep	; remainder < divisor
        sub	r_remL,r_arg2L	; restore remainder
        sbc	r_remH,r_arg2H
__udivmodhi4_ep:
        rol	r_arg1L		; shift dividend (with CARRY)
        rol	r_arg1H
        dec	r_cnt		; decrement loop counter
        brne	__udivmodhi4_loop
	com	r_arg1L
	com	r_arg1H
; div/mod results to return registers, as for the div() function
	mov_l	r_arg2L, r_arg1L	; quotient
	mov_h	r_arg2H, r_arg1H
	mov_l	r_arg1L, r_remL		; remainder
	mov_h	r_arg1H, r_remH
	ret
	.endfunc
#endif /* defined (L_udivmodhi4) */

#if defined (L_divmodhi4)
	.global	__divmodhi4
	.func	__divmodhi4
__divmodhi4:
	.global	_div
_div:
        bst     r_arg1H,7	; store sign of dividend
        mov     __tmp_reg__,r_arg1H
        eor     __tmp_reg__,r_arg2H   ; r0.7 is sign of result
	rcall	__divmodhi4_neg1 ; dividend negative : negate
	sbrc	r_arg2H,7
	rcall	__divmodhi4_neg2 ; divisor negative : negate
	rcall	__udivmodhi4	; do the unsigned div/mod
	rcall	__divmodhi4_neg1 ; correct remainder sign
	tst	__tmp_reg__
	brpl	__divmodhi4_exit
__divmodhi4_neg2:
	com	r_arg2H
	neg	r_arg2L		; correct divisor/result sign
	sbci	r_arg2H,0xff
__divmodhi4_exit:
	ret
__divmodhi4_neg1:
	brtc	__divmodhi4_exit
	com	r_arg1H
	neg	r_arg1L		; correct dividend/remainder sign
	sbci	r_arg1H,0xff
	ret
	.endfunc
#endif /* defined (L_divmodhi4) */

#undef r_remH  
#undef r_remL  
             
#undef r_arg1H 
#undef r_arg1L 
             
#undef r_arg2H 
#undef r_arg2L 
             	
#undef r_cnt   	
	
/*******************************************************
       Division 32 / 32 => (result + remainder)
*******************************************************/
#define	r_remHH	r31	/* remainder High */
#define	r_remHL	r30
#define	r_remH	r27
#define	r_remL	r26	/* remainder Low */

/* return: remainder */
#define	r_arg1HH r25	/* dividend High */
#define	r_arg1HL r24
#define	r_arg1H  r23
#define	r_arg1L  r22	/* dividend Low */

/* return: quotient */
#define	r_arg2HH r21	/* divisor High */
#define	r_arg2HL r20
#define	r_arg2H  r19
#define	r_arg2L  r18	/* divisor Low */
	
#define	r_cnt __zero_reg__  /* loop count (0 after the loop!) */

#if defined (L_udivmodsi4)
	.global	__udivmodsi4
	.func	__udivmodsi4
__udivmodsi4:
	ldi	r_remL, 33	; init loop counter
	mov	r_cnt, r_remL
	sub	r_remL,r_remL
	sub	r_remH,r_remH	; clear remainder and carry
	mov_l	r_remHL, r_remL
	mov_h	r_remHH, r_remH
	rjmp	__udivmodsi4_ep	; jump to entry point
__udivmodsi4_loop:
        rol	r_remL		; shift dividend into remainder
	rol	r_remH
	rol	r_remHL
	rol	r_remHH
        cp	r_remL,r_arg2L	; compare remainder & divisor
	cpc	r_remH,r_arg2H
	cpc	r_remHL,r_arg2HL
	cpc	r_remHH,r_arg2HH
	brcs	__udivmodsi4_ep	; remainder <= divisor
        sub	r_remL,r_arg2L	; restore remainder
        sbc	r_remH,r_arg2H
        sbc	r_remHL,r_arg2HL
        sbc	r_remHH,r_arg2HH
__udivmodsi4_ep:
        rol	r_arg1L		; shift dividend (with CARRY)
        rol	r_arg1H
        rol	r_arg1HL
        rol	r_arg1HH
        dec	r_cnt		; decrement loop counter
        brne	__udivmodsi4_loop
				; __zero_reg__ now restored (r_cnt == 0)
	com	r_arg1L
	com	r_arg1H
	com	r_arg1HL
	com	r_arg1HH
; div/mod results to return registers, as for the ldiv() function
	mov_l	r_arg2L,  r_arg1L	; quotient
	mov_h	r_arg2H,  r_arg1H
	mov_l	r_arg2HL, r_arg1HL
	mov_h	r_arg2HH, r_arg1HH
	mov_l	r_arg1L,  r_remL	; remainder
	mov_h	r_arg1H,  r_remH
	mov_l	r_arg1HL, r_remHL
	mov_h	r_arg1HH, r_remHH
	ret
	.endfunc
#endif /* defined (L_udivmodsi4) */

#if defined (L_divmodsi4)
	.global	__divmodsi4
	.func	__divmodsi4
__divmodsi4:
        bst     r_arg1HH,7	; store sign of dividend
        mov     __tmp_reg__,r_arg1HH
        eor     __tmp_reg__,r_arg2HH   ; r0.7 is sign of result
	rcall	__divmodsi4_neg1 ; dividend negative : negate
	sbrc	r_arg2HH,7
	rcall	__divmodsi4_neg2 ; divisor negative : negate
	rcall	__udivmodsi4	; do the unsigned div/mod
	rcall	__divmodsi4_neg1 ; correct remainder sign
	rol	__tmp_reg__
	brcc	__divmodsi4_exit
__divmodsi4_neg2:
	com	r_arg2HH
	com	r_arg2HL
	com	r_arg2H
	neg	r_arg2L		; correct divisor/quotient sign
	sbci	r_arg2H,0xff
	sbci	r_arg2HL,0xff
	sbci	r_arg2HH,0xff
__divmodsi4_exit:
	ret
__divmodsi4_neg1:
	brtc	__divmodsi4_exit
	com	r_arg1HH
	com	r_arg1HL
	com	r_arg1H
	neg	r_arg1L		; correct dividend/remainder sign
	sbci	r_arg1H, 0xff
	sbci	r_arg1HL,0xff
	sbci	r_arg1HH,0xff
	ret
	.endfunc
#endif /* defined (L_divmodsi4) */

/**********************************
 * This is a prologue subroutine
 **********************************/
#if defined (L_prologue)

	.global	__prologue_saves__
	.func	__prologue_saves__
__prologue_saves__:
	push r2
	push r3
	push r4
	push r5
	push r6
	push r7
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15
	push r16
	push r17
	push r28
	push r29
	in	r28,__SP_L__
	in	r29,__SP_H__
	sub	r28,r26
	sbc	r29,r27
	in	__tmp_reg__,__SREG__
	cli
	out	__SP_H__,r29
	out	__SREG__,__tmp_reg__
	out	__SP_L__,r28
	ijmp
.endfunc
#endif /* defined (L_prologue) */

/*
 * This is an epilogue subroutine
 */
#if defined (L_epilogue)

	.global	__epilogue_restores__
	.func	__epilogue_restores__
__epilogue_restores__:
	ldd	r2,Y+18
	ldd	r3,Y+17
	ldd	r4,Y+16
	ldd	r5,Y+15
	ldd	r6,Y+14
	ldd	r7,Y+13
	ldd	r8,Y+12
	ldd	r9,Y+11
	ldd	r10,Y+10
	ldd	r11,Y+9
	ldd	r12,Y+8
	ldd	r13,Y+7
	ldd	r14,Y+6
	ldd	r15,Y+5
	ldd	r16,Y+4
	ldd	r17,Y+3
	ldd	r26,Y+2
	ldd	r27,Y+1
	add	r28,r30
	adc	r29,__zero_reg__
	in	__tmp_reg__,__SREG__
	cli
	out	__SP_H__,r29
	out	__SREG__,__tmp_reg__
	out	__SP_L__,r28
	mov_l	r28, r26
	mov_h	r29, r27
	ret
.endfunc
#endif /* defined (L_epilogue) */

#ifdef L_exit
	.section .fini9,"ax",@progbits
	.global _exit
	.func	_exit
_exit:
	.weak	exit
exit:

	/* Code from .fini8 ... .fini1 sections inserted by ld script.  */

	.section .fini0,"ax",@progbits
__stop_program:
	rjmp	__stop_program
	.endfunc
#endif /* defined (L_exit) */

#ifdef L_cleanup
	.weak	_cleanup
	.func	_cleanup
_cleanup:
	ret
.endfunc
#endif /* defined (L_cleanup) */

#ifdef L_tablejump
	.global __tablejump2__
	.func	__tablejump2__
__tablejump2__:
	lsl	r30
	rol	r31
	.global __tablejump__
__tablejump__:
#if defined (__AVR_ENHANCED__)
	lpm	__tmp_reg__, Z+
	lpm	r31, Z
	mov	r30, __tmp_reg__
	ijmp
#else
	lpm
	adiw	r30, 1
	push	r0
	lpm
	push	r0
	ret
#endif
	.endfunc
#endif /* defined (L_tablejump) */

/* __do_copy_data is only necessary if there is anything in .data section.
   Does not use RAMPZ - crt*.o provides a replacement for >64K devices.  */

#ifdef L_copy_data
	.section .init4,"ax",@progbits
	.global __do_copy_data
__do_copy_data:
	ldi	r17, hi8(__data_end)
	ldi	r26, lo8(__data_start)
	ldi	r27, hi8(__data_start)
	ldi	r30, lo8(__data_load_start)
	ldi	r31, hi8(__data_load_start)
	rjmp	.do_copy_data_start
.do_copy_data_loop:
#if defined (__AVR_HAVE_LPMX__)
	lpm	r0, Z+
#else
	lpm
	adiw	r30, 1
#endif
	st	X+, r0
.do_copy_data_start:
	cpi	r26, lo8(__data_end)
	cpc	r27, r17
	brne	.do_copy_data_loop
#endif /* L_copy_data */

/* __do_clear_bss is only necessary if there is anything in .bss section.  */

#ifdef L_clear_bss
	.section .init4,"ax",@progbits
	.global __do_clear_bss
__do_clear_bss:
	ldi	r17, hi8(__bss_end)
	ldi	r26, lo8(__bss_start)
	ldi	r27, hi8(__bss_start)
	rjmp	.do_clear_bss_start
.do_clear_bss_loop:
	st	X+, __zero_reg__
.do_clear_bss_start:
	cpi	r26, lo8(__bss_end)
	cpc	r27, r17
	brne	.do_clear_bss_loop
#endif /* L_clear_bss */

/* __do_global_ctors and __do_global_dtors are only necessary
   if there are any constructors/destructors.  */

#if defined (__AVR_MEGA__)
#define XCALL call
#else
#define XCALL rcall
#endif

#ifdef L_ctors
	.section .init6,"ax",@progbits
	.global	__do_global_ctors
__do_global_ctors:
	ldi	r17, hi8(__ctors_start)
	ldi	r28, lo8(__ctors_end)
	ldi	r29, hi8(__ctors_end)
	rjmp	.do_global_ctors_start
.do_global_ctors_loop:
	sbiw	r28, 2
	mov_h	r31, r29
	mov_l	r30, r28
	XCALL	__tablejump__
.do_global_ctors_start:
	cpi	r28, lo8(__ctors_start)
	cpc	r29, r17
	brne	.do_global_ctors_loop
#endif /* L_ctors */

#ifdef L_dtors
	.section .fini6,"ax",@progbits
	.global	__do_global_dtors
__do_global_dtors:
	ldi	r17, hi8(__dtors_end)
	ldi	r28, lo8(__dtors_start)
	ldi	r29, hi8(__dtors_start)
	rjmp	.do_global_dtors_start
.do_global_dtors_loop:
	mov_h	r31, r29
	mov_l	r30, r28
	XCALL	__tablejump__
	adiw	r28, 2
.do_global_dtors_start:
	cpi	r28, lo8(__dtors_end)
	cpc	r29, r17
	brne	.do_global_dtors_loop
#endif /* L_dtors */

