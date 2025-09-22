/* $OpenBSD: md_init.h,v 1.12 2023/11/18 16:26:16 deraadt Exp $ */

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
	__asm (".section "#section", \"ax\"		\n" \
	"	bl " #func "				\n" \
	"	.previous")

#define MD_SECTION_PROLOGUE(sect, entry_pt)	\
	__asm (					\
	".section "#sect",\"ax\",%progbits	\n" \
	"	.globl " #entry_pt "		\n" \
	"	.type " #entry_pt ",%function	\n" \
	"	.align 4			\n" \
	#entry_pt":				\n" \
	"	bti	c			\n" \
	"	sub	sp, sp, #16		\n" \
	"	str	lr, [sp]		\n" \
	"	/* fall thru */			\n" \
	"	.previous")


#define MD_SECTION_EPILOGUE(sect)		\
	__asm (					\
	".section "#sect",\"ax\",%progbits	\n" \
	"	ldr	lr, [sp]		\n" \
	"	add	sp, sp, #16		\n" \
	"	ret				\n" \
	"	.previous")


#define	MD_CRT0_START				\
	__asm(					\
	".text					\n" \
	"	.align	0			\n" \
	"	.globl	_start			\n" \
	"	.globl	__start			\n" \
	"_start:				\n" \
	"__start:				\n" \
	"	bti	c			\n" \
	"	mov	x3, x2	/* cleanup */	\n" \
	"/* Get argc/argv/envp from stack */	\n" \
	"	ldr	x0, [sp]		\n" \
	"	add	x1, sp, #0x0008		\n" \
	"	add	x2, x1, x0, lsl #3	\n" \
	"	add	x2, x2, #0x0008		\n" \
	"					\n" \
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
	"	mov	x0, fp			\n" \
	"					\n" \
	"	sub	sp, sp, #8+8+(16*8)	\n" \
	"	add	x1, sp, #4		\n" \
	"					\n" \
	"	adrp	x2, _DYNAMIC		\n" \
	"	add	x2, x2, #:lo12:_DYNAMIC	\n" \
	"					\n" \
	"	bl	_dl_boot_bind		\n" \
	"					\n" \
	"	mov	sp, fp			\n" \
	"	mov	fp, #0			\n" \
	"					\n" \
	"	mov	x3, #0	/* cleanup */	\n" \
	"/* Get argc/argv/envp from stack */	\n" \
	"	ldr	x0, [sp]		\n" \
	"	add	x1, sp, #0x0008		\n" \
	"	add	x2, x1, x0, lsl #3	\n" \
	"	add	x2, x2, #0x0008		\n" \
	"					\n" \
	"	b	___start		\n" \
	"					\n" \
	"_csu_abort:				\n" \
	"	udf	#0			\n" \
	".previous");
