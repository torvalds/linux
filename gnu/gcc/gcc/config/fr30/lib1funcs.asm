/* libgcc routines for the FR30.
   Copyright (C) 1998, 1999 Free Software Foundation, Inc.

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

	.macro FUNC_START name
	.text
	.globl __\name
	.type  __\name, @function
__\name:
	.endm

	.macro FUNC_END name
	.size  __\name, . - __\name
	.endm

	.macro DIV_BODY reg number
	.if \number
	DIV_BODY  \reg, "\number - 1"
	div1	\reg
	.endif
	.endm
	
#ifdef L_udivsi3
FUNC_START udivsi3
	;; Perform an unsiged division of r4 / r5 and place the result in r4.
	;; Does not handle overflow yet...
	mov	r4, mdl
	div0u	r5
	DIV_BODY r5 32
	mov	mdl, r4
	ret
FUNC_END udivsi3
#endif /* L_udivsi3 */

#ifdef L_divsi3
FUNC_START divsi3
	;; Perform a siged division of r4 / r5 and place the result in r4.
	;; Does not handle overflow yet...
	mov	r4, mdl
	div0s	r5
	DIV_BODY r5 32
	div2    r5
	div3
	div4s
	mov	mdl, r4
	ret
FUNC_END divsi3
#endif /* L_divsi3 */

#ifdef L_umodsi3
FUNC_START umodsi3
	;; Perform an unsiged division of r4 / r5 and places the remainder in r4.
	;; Does not handle overflow yet...
	mov	r4, mdl
	div0u	r5
	DIV_BODY r5 32
	mov	mdh, r4
	ret
FUNC_END umodsi3
#endif /* L_umodsi3 */

#ifdef L_modsi3
FUNC_START modsi3
	;; Perform a siged division of r4 / r5 and place the remainder in r4.
	;; Does not handle overflow yet...
	mov	r4, mdl
	div0s	r5
	DIV_BODY r5 32
	div2    r5
	div3
	div4s
	mov	mdh, r4
	ret
FUNC_END modsi3
#endif /* L_modsi3 */

#ifdef L_negsi2
FUNC_START negsi2
	ldi:8	#0, r0
	sub	r4, r0
	mov	r0, r4
	ret
FUNC_END negsi2
#endif /* L_negsi2 */

#ifdef L_one_cmplsi2
FUNC_START one_cmplsi2
	ldi:8	#0xff, r0
	extsb	r0
	eor	r0, r4
	ret
FUNC_END one_cmplsi2
#endif /* L_one_cmplsi2 */


