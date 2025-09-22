/* $OpenBSD: md_init.h,v 1.11 2025/01/30 21:41:37 kurt Exp $ */

/*-
 * Copyright (c) 2001 Ross Harvey
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define MD_SECT_CALL_FUNC(section, func) \
	__asm (".section "#section", \"ax\"	\n" \
	"	call " #func "			\n" \
	"	 nop				\n" \
	"	.previous")

#define MD_SECTION_PROLOGUE(sect, entry_pt)	\
	__asm (					\
	".section "#sect",\"ax\",@progbits	\n" \
	"	.globl " #entry_pt "		\n" \
	"	.type " #entry_pt ",@function	\n" \
	#entry_pt":				\n" \
	"	save	%sp, -192, %sp		\n" \
	"	.align 4			\n" \
	"	/* fall thru */			\n" \
	"	.previous")


#define MD_SECTION_EPILOGUE(sect)		\
	__asm (					\
	".section "#sect",\"ax\",@progbits	\n" \
	"	ret				\n" \
	"	 restore			\n" \
	"	.previous")


#define	MD_CRT0_START				\
	__asm__(				\
	".text					\n" \
	"	.align	4			\n" \
	"	.global	_start			\n" \
	"	.global	__start			\n" \
	"_start:				\n" \
	"__start:				\n" \
	"	clr	%fp			\n" \
	"	add	%sp, 2175, %o0	/* stack */\n" \
	"	ba,pt	%icc, ___start		\n" \
	"	 mov	%g1, %o1		\n" \
	"	.previous")

#define	MD_RCRT0_START				\
	__asm__(				\
	".text					\n" \
	"	.align	4			\n" \
	"	.global	_start			\n" \
	"	.global	__start			\n" \
	"_start:				\n" \
	"__start:				\n" \
	"	clr	%fp			\n" \
	"	sub	%sp, 48 + 16*8, %sp	\n" \
	"	add	%sp, 2223, %l3 		\n" \
	"	add	%l3, 16*8, %o0		\n" \
	"	mov	%o0, %l0		\n" \
	"	call	0f			\n" \
	"	 nop				\n" \
	"	call	_DYNAMIC+8		\n" \
	"0:	ld	[%o7+8], %o2		\n" \
	"	sll	%o2, 2, %o2		\n" \
	"	sra	%o2, 0, %o2		\n" \
	"	add	%o2, %o7, %o2		\n" \
	"	call	_dl_boot_bind		\n" \
	"	 mov	%l3, %o1		\n" \
	"	add	%sp, 48 + 16*8, %sp	\n" \
	"	add	%sp, 2175, %o0	/* stack */\n" \
	"	ba,pt	%icc, ___start		\n" \
	"	 clr	%o1			\n" \
	"					\n" \
	"	.global	_csu_abort		\n" \
	"_csu_abort:				\n" \
	"	unimp				\n" \
	"	.previous")


#define	MD_START_ARGS		char **sp, void (*cleanup)(void)
#define	MD_START_SETUP				\
	char **argv, **envp;			\
	long argc;				\
						\
	argc = *(long *)sp;			\
	argv = sp + 1;				\
	envp = sp + 2 + argc;		/* 2: argc + NULL ending argv */
