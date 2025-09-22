/* Miscellaneous BPABI functions.

   Copyright (C) 2003, 2004  Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.

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

#ifdef __ARMEB__
#define xxh r0
#define xxl r1
#define yyh r2
#define yyl r3
#else
#define xxh r1
#define xxl r0
#define yyh r3
#define yyl r2
#endif	
	
#ifdef L_aeabi_lcmp

ARM_FUNC_START aeabi_lcmp
	subs	ip, xxl, yyl
	sbcs	ip, xxh, yyh
	subeqs  ip, xxl, yyl
	mov	r0, ip
	RET
	FUNC_END aeabi_lcmp

#endif /* L_aeabi_lcmp */
	
#ifdef L_aeabi_ulcmp

ARM_FUNC_START aeabi_ulcmp
	cmp	xxh, yyh
	movlo	r0, #-1
	movhi	r0, #1
	RETc(ne)
	cmp	xxl, yyl
	movlo	r0, #-1
	movhi	r0, #1
	moveq	r0, #0
	RET
	FUNC_END aeabi_ulcmp

#endif /* L_aeabi_ulcmp */

#ifdef L_aeabi_ldivmod

ARM_FUNC_START aeabi_ldivmod
	sub sp, sp, #8
	stmfd sp!, {sp, lr}
	bl SYM(__gnu_ldivmod_helper) __PLT__
	ldr lr, [sp, #4]
	add sp, sp, #8
	ldmfd sp!, {r2, r3}
	RET
	
#endif /* L_aeabi_ldivmod */

#ifdef L_aeabi_uldivmod

ARM_FUNC_START aeabi_uldivmod
	sub sp, sp, #8
	stmfd sp!, {sp, lr}
	bl SYM(__gnu_uldivmod_helper) __PLT__
	ldr lr, [sp, #4]
	add sp, sp, #8
	ldmfd sp!, {r2, r3}
	RET
	
#endif /* L_aeabi_divmod */
	
