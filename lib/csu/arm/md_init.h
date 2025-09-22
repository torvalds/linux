/* $OpenBSD: md_init.h,v 1.19 2024/08/17 09:48:31 phessler Exp $ */

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

/*
 * arm overrides these because it uses %progbits instead of @progbits
 */
#define MD_DATA_SECTION_FLAGS_SYMBOL(section, flags, type, symbol)	\
	extern __dso_hidden type symbol[];				\
	__asm("	.section "section",\""flags"\",%progbits		\n" \
	"	.balign 4						\n" \
	#symbol":							\n" \
	"	.previous")
#define MD_DATA_SECTION_SYMBOL_VALUE(section, type, symbol, value)	\
	extern __dso_hidden type symbol[];				\
	__asm("	.section "section",\"aw\",%progbits			\n" \
	"	.balign 4						\n" \
	#symbol":							\n" \
	"	.int "#value"						\n" \
	"	.previous")
#define MD_DATA_SECTION_FLAGS_VALUE(section, flags, value)		\
	__asm("	.section "section",\""flags"\",%progbits		\n" \
	"	.balign 4						\n" \
	"	.int "#value"						\n" \
	"	.previous")

#define MD_SECT_CALL_FUNC(section, func) \
	__asm (".section "#section", \"ax\"		\n" \
	"	movw	r0, #:lower16:" #func "- 1f - 8	\n" \
	"	movt	r0, #:upper16:" #func "- 1f - 8	\n" \
	"1:	add	r0, r0, pc			\n" \
	"	blx	r0				\n" \
	"	.previous")

#define MD_SECTION_PROLOGUE(sect, entry_pt)	\
	__asm (					\
	".section "#sect",\"ax\",%progbits	\n" \
	"	.globl " #entry_pt "		\n" \
	"	.type " #entry_pt ",%function	\n" \
	"	.align 4			\n" \
	#entry_pt":				\n" \
	"	push	{r4, lr}		\n" \
	"	/* fall thru */			\n" \
	"	.previous")


#define MD_SECTION_EPILOGUE(sect)		\
	__asm (					\
	".section "#sect",\"ax\",%progbits	\n" \
	"	pop	{r4, pc}		\n" \
	"	.previous")


/*
 * The definitions of environ and __progname prevent the creation
 * of COPY relocations for WEAK symbols.
 */
#define	MD_CRT0_START				\
	__asm(					\
	".text					\n" \
	"	.align	0			\n" \
	"	.globl	_start			\n" \
	"	.globl	__start			\n" \
	"_start:				\n" \
	"__start:				\n" \
	"	mov	r3, r0	/* cleanup */	\n" \
	"/* Get argc/argv/envp from stack */	\n" \
	"	ldr	r0, [sp, #0]		\n" \
	"	add	r1, sp, #4		\n" \
	"	add	r2, r1, r0, lsl #2	\n" \
	"	add	r2, r2, #4		\n" \
	"					\n" \
	"/*					\n" \
	" * Ensure the stack is properly	\n" \
	" * aligned before calling C code.	\n" \
	" */					\n" \
	"	bic	sp, sp, #7" /*__STRING(STACKALIGNBYTES)*/ "	\n" \
	"	b	___start		\n" \
	".previous");

#define	MD_RCRT0_START				\
	char **environ, *__progname;		\
	__asm(					\
	".text					\n" \
	"	.align	0			\n" \
	"	.globl	_start			\n" \
	"	.globl	__start			\n" \
	"_start:				\n" \
	"__start:				\n" \
	"	mov	fp, sp			\n" \
	"	mov	r0, fp			\n" \
	"					\n" \
	"	sub	sp, sp, #4+4+(16*4)	\n" \
	"	add	r1, sp, #4		\n" \
	"					\n" \
	"	ldr	r8, .L_GOT		\n" \
	"1:	add	r8, pc, r8		\n" \
	"	ldr	r2, .L__DYNAMIC		\n" \
	"	add	r2, r2, r8		\n" \
	"					\n" \
	"	bl	_dl_boot_bind		\n" \
	"					\n" \
	"	mov	sp, fp			\n" \
	"	mov	fp, #0			\n" \
	"					\n" \
	"	mov	r3, #0	/* cleanup */	\n" \
	"/* Get argc/argv/envp from stack */	\n" \
	"	ldr	r0, [sp, #0]		\n" \
	"	add	r1, sp, #4		\n" \
	"	add	r2, r1, r0, lsl #2	\n" \
	"	add	r2, r2, #4		\n" \
	"					\n" \
	"/*					\n" \
	" * Ensure the stack is properly	\n" \
	" * aligned before calling C code.	\n" \
	" */					\n" \
	"	bic	sp, sp, #7" /*__STRING(STACKALIGNBYTES)*/ "	\n" \
	"	b	___start		\n" \
	"					\n" \
	".L_GOT:				\n" \
	"	.long	_GLOBAL_OFFSET_TABLE_-(1b+8)	\n" \
	".L__DYNAMIC:				\n" \
	"	.long	_DYNAMIC(GOTOFF)	\n" \
	"					\n" \
	"_csu_abort:				\n" \
	"	udf	#0			\n" \
	".previous");
