/* $OpenBSD: md_init.h,v 1.11 2023/11/18 16:26:16 deraadt Exp $ */

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
	__asm (".section "#section", \"ax\"\n"	\
	"	call " #func "\n"		\
	"	.previous")

#define MD_SECTION_PROLOGUE(sect, entry_pt)	\
	__asm (					\
	".section "#sect",\"ax\",@progbits	\n" \
	"	.globl " #entry_pt "		\n" \
	"	.type " #entry_pt ",@function	\n" \
	"	.align 16			\n" \
	#entry_pt":				\n" \
	"	endbr64				\n" \
	"	subq	$8,%rsp			\n" \
	"	.previous")


#define MD_SECTION_EPILOGUE(sect)		\
	__asm (					\
	".section "#sect",\"ax\",@progbits	\n" \
	"	addq	$8,%rsp			\n" \
	"	ret				\n" \
	"	.previous")


#define	MD_CRT0_START				\
	__asm(					\
	".text					\n" \
	"	.align	8			\n" \
	"	.globl	__start			\n" \
	"	.globl	_start			\n" \
	"_start:				\n" \
	"__start:				\n" \
	"	endbr64				\n" \
	"	movq	%rdx,%rcx		\n" \
	"	movq	(%rsp),%rdi		\n" \
	"	leaq	16(%rsp,%rdi,8),%rdx	\n" \
	"	leaq	8(%rsp),%rsi		\n" \
	"	subq	$8,%rsp			\n" \
	"	andq	$~15,%rsp		\n" \
	"	addq	$8,%rsp			\n" \
	"	jmp	___start		\n" \
	"	.previous")

#define	MD_RCRT0_START					\
	__asm(						\
	".text						\n" \
	"	.align	8				\n" \
	"	.globl	__start				\n" \
	"	.type	__start,@function		\n" \
	"_start:					\n" \
	"__start:					\n" \
	"	endbr64					\n" \
	"	movq	%rsp, %r12			\n" \
	"	subq	$8, %rsp			\n" \
	"	andq	$~15, %rsp			\n" \
	"	addq	$8, %rsp			\n" \
	"	pushq	%rbx				\n" \
	"	subq	$(16*8), %rsp			\n" \
	"	leaq	_DYNAMIC(%rip),%rdx		\n" \
	"	movq	%rsp, %rsi			\n" \
	"	movq	%r12, %rdi			\n" \
	"	call	_dl_boot_bind@PLT		\n" \
	"						\n" \
	"	movq	$0, %rcx			\n" \
	"	movq	%r12, %rsp			\n" \
	"	movq	(%rsp),%rdi			\n" \
	"	leaq	16(%rsp,%rdi,8),%rdx		\n" \
	"	leaq	8(%rsp),%rsi			\n" \
	"	subq	$8,%rsp				\n" \
	"	andq	$~15,%rsp			\n" \
	"	addq	$8,%rsp				\n" \
	"	jmp	___start			\n" \
	"						\n" \
	"	.global	_csu_abort			\n" \
	"	.type	_csu_abort,@function		\n" \
	"	.align	8				\n" \
	"_csu_abort:					\n" \
	"	endbr64					\n" \
	"	int3					\n" \
	"	.previous")
