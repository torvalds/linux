; libgcc routines for ARC cpu.

/* Copyright (C) 1995, 1997,2004 Free Software Foundation, Inc.

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
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef  L_mulsi3
	.section .text
	.align 4

#ifdef __base__
	.cpu base
	.global ___mulsi3
___mulsi3:

/* This the simple version.

  while (a) 
    {
      if (a & 1)
        r += b;
      a >>= 1;
      b <<= 1;
    }
*/
	mov r2,0		; Accumulate result here.
.Lloop:
	sub.f 0,r0,0		; while (a)
	nop
	beq.nd .Ldone
	and.f 0,r0,1		; if (a & 1)
	add.nz r2,r2,r1		; r += b
	lsr r0,r0		; a >>= 1
	b.d .Lloop
	lsl r1,r1		; b <<= 1
.Ldone:
	j.d blink
	mov r0,r2
#endif

#endif /* L_mulsi3 */

#ifdef  L_umulsidi3
	.section .text
	.align 4

#ifdef __base__
	.cpu base
	.global ___umulsidi3
___umulsidi3:

/* This the simple version.

  while (a) 
    {
      if (a & 1)
        r += b;
      a >>= 1;
      b <<= 1;
    }
*/
	mov r2,0		; Top part of b.
	mov r3,0		; Accumulate result here.
	mov r4,0
.Lloop:
	sub.f 0,r0,0		; while (a)
	nop
	beq.nd .Ldone
	and.f 0,r0,1		; if (a & 1)
	sub.f 0,r0,0
	nop
	beq .Ldontadd
	add.f r4,r4,r1		; r += b
	adc   r3,r3,r2
.Ldontadd:
	lsr r0,r0		; a >>= 1
	lsl.f r1,r1		; b <<= 1
	b.d .Lloop
	rlc r2,r2
.Ldone:
#ifdef __big_endian__
	mov r1,r4
	j.d blink
	mov r0,r3
#else
	mov r0,r4
	j.d blink
	mov r1,r3
#endif
#endif

#endif /* L_umulsidi3 */

#ifdef L_divmod_tools

; Utilities used by all routines.

	.section .text
	.align 4

; inputs: r0 = numerator, r1 = denominator
; outputs: positive r0/r1,
;          r6.bit1 = sign of numerator, r6.bit0 = sign of result

	.global ___divnorm
___divnorm:
	mov r6,0		; keep sign in r6
	sub.f 0,r0,0		; is numerator -ve?
	sub.lt r0,0,r0		; negate numerator
	mov.lt r6,3		; sign is -ve
	sub.f 0,r1,0		; is denominator -ve?
	sub.lt r1,0,r1		; negate denominator
	xor.lt r6,r6,1		; toggle sign
	j.nd blink

/*
unsigned long
udivmodsi4(int modwanted, unsigned long num, unsigned long den)
{
  unsigned long bit = 1;
  unsigned long res = 0;

  while (den < num && bit && !(den & (1L<<31)))
    {
      den <<=1;
      bit <<=1;
    }
  while (bit)
    {
      if (num >= den)
	{
	  num -= den;
	  res |= bit;
	}
      bit >>=1;
      den >>=1;
    }
  if (modwanted) return num;
  return res;
}
*/

; inputs: r0 = numerator, r1 = denominator
; outputs: r0 = quotient, r1 = remainder, r2/r3 trashed

	.global ___udivmodsi4
___udivmodsi4:
	mov r2,1		; bit = 1
	mov r3,0		; res = 0
.Lloop1:
	sub.f 0,r1,r0		; while (den < num
	nop
	bnc.nd .Lloop2
	sub.f 0,r2,0		; && bit
	nop
	bz.nd .Lloop2
	lsl.f 0,r1		; && !(den & (1<<31))
	nop
	bc.nd .Lloop2
	lsl r1,r1		; den <<= 1
	b.d .Lloop1
	lsl r2,r2		; bit <<= 1
.Lloop2:
	sub.f 0,r2,0		; while (bit)
	nop
	bz.nd .Ldivmodend
	sub.f 0,r0,r1		; if (num >= den)
	nop
	bc.nd .Lshiftdown
	sub r0,r0,r1		; num -= den
	or r3,r3,r2		; res |= bit
.Lshiftdown:
	lsr r2,r2		; bit >>= 1
	b.d .Lloop2
	lsr r1,r1		; den >>= 1
.Ldivmodend:
	mov r1,r0		; r1 = mod
	j.d blink
	mov r0,r3		; r0 = res

#endif

#ifdef  L_udivsi3
	.section .text
	.align 4

#ifdef __base__
	.cpu base
	.global ___udivsi3
___udivsi3:
	mov r7,blink
	bl.nd ___udivmodsi4
	j.nd r7
#endif

#endif /* L_udivsi3 */

#ifdef  L_divsi3
	.section .text
	.align 4

#ifdef __base__
	.cpu base
	.global ___divsi3
___divsi3:
	mov r7,blink
	bl.nd ___divnorm
	bl.nd ___udivmodsi4
	and.f 0,r6,1
	sub.nz r0,0,r0		; cannot go in delay slot, has limm value
	j.nd r7
#endif

#endif /* L_divsi3 */

#ifdef  L_umodsi3
	.section .text
	.align 4

#ifdef __base__
	.cpu base
	.global ___umodsi3
___umodsi3:
	mov r7,blink
	bl.nd ___udivmodsi4
	j.d r7
	mov r0,r1
#endif

#endif /* L_umodsi3 */

#ifdef  L_modsi3
	.section .text
	.align 4

#ifdef __base__
	.cpu base
	.global ___modsi3
___modsi3:
	mov r7,blink
	bl.nd ___divnorm
	bl.nd ___udivmodsi4
	and.f 0,r6,2
	sub.nz r1,0,r1
	j.d r7
	mov r0,r1
#endif

#endif /* L_modsi3 */
