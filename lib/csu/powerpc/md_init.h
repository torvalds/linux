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
	__asm (".section "#section", \"ax\"\n"	\
	"	bl " #func "\n"		\
	"	.previous")

#define MD_SECTION_PROLOGUE(sect, entry_pt)	\
	__asm (					\
	".section "#sect",\"ax\",@progbits	\n" \
	"	.globl " #entry_pt "		\n" \
	"	.type " #entry_pt ",@function	\n" \
	"	.align 4			\n" \
	#entry_pt":				\n" \
	"	stwu	%r1,-16(%r1)		\n" \
	"	mflr	%r0			\n" \
	"	stw	%r0,12(%r1)		\n" \
	"	/* fall thru */			\n" \
	"	.previous")


#define MD_SECTION_EPILOGUE(sect)		\
	__asm (					\
	".section "#sect",\"ax\",@progbits	\n" \
	"	lwz	%r0,12(%r1)		\n" \
	"	mtlr	%r0			\n" \
	"	addi	%r1,%r1,16		\n" \
	"	blr				\n" \
	"	.previous")

#define	MD_CRT0_START							\
__asm(									\
"	.text								\n" \
"	.section	\".text\"					\n" \
"	.align 2							\n" \
"	.globl	_start							\n" \
"	.type	_start, @function					\n" \
"	.globl	__start							\n" \
"	.type	__start, @function					\n" \
"_start:								\n" \
"__start:								\n" \
"	# put cleanup in r6 instead of r7				\n" \
"	mr %r6, %r7							\n" \
"	b ___start							\n" \
)

#define	MD_RCRT0_START							\
__asm(									\
"	.text								\n" \
"	.section	\".text\"					\n" \
"	.align 2							\n" \
"	.globl	_start							\n" \
"	.type	_start, @function					\n" \
"	.globl	__start							\n" \
"	.type	__start, @function					\n" \
"_start:								\n" \
"__start:								\n" \
"	mr	%r19, %r1		# save stack in r19		\n" \
"	stwu	1, (-16 -((9+3)*4))(%r1) # allocate dl_data		\n" \
"									\n" \
"	# move argument registers to saved registers for startup flush	\n" \
"	mr %r20, %r3			# argc				\n" \
"	mr %r21, %r4			# argv				\n" \
"	mr %r22, %r5			# envp				\n" \
"	mflr	%r27	/* save off old link register */		\n" \
"	stw	%r27, 4(%r19)		# save in normal location	\n" \
"									\n" \
"	bcl	20, 31, 1f						\n" \
"1:	mflr	%r18							\n" \
"	addis	%r18, %r18, _DYNAMIC-1b@ha				\n" \
"	addi	%r18, %r18, _DYNAMIC-1b@l				\n" \
"									\n" \
"	subi	%r3, %r21, 4	# Get stack pointer (arg0 for _dl_boot). \n" \
"	addi	%r4, %r1, 8	# dl_data				\n" \
"	mr	%r5, %r18	# dynamicp				\n" \
"									\n" \
"	bl	_dl_boot_bind@local					\n" \
"									\n" \
"	mtlr %r27							\n" \
"	# move argument registers back from saved registers		\n" \
"	mr %r3, %r20							\n" \
"	mr %r4, %r21							\n" \
"	mr %r5, %r22							\n" \
"	li %r6, 0							\n" \
"	b ___start							\n" \
"									\n" \
"	.text								\n" \
"	.align 2							\n" \
"	.globl	_csu_abort						\n" \
"	.type	_csu_abort, @function					\n" \
"_csu_abort:								\n" \
"	.long 0 # illegal						\n" \
)
