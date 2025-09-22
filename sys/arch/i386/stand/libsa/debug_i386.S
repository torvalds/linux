/*	$OpenBSD: debug_i386.S,v 1.13 2022/12/08 01:25:45 guenther Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <machine/asm.h>
#include <machine/psl.h>
#define _LOCORE
#include <machine/segments.h>
#include <machine/trap.h>
#include <debug_md.h>
#undef _LOCORE

	/* do this way because some cpus (like cyrix 6x86) store garbage
	 * into high word, instead of zeroes.
	 */
#define	pushsr(sr)	movl sr, %eax; pushl %eax

	.text
	.globl	alltraps

alltraps:
	pushal
	pushsr(%ds)
	pushsr(%es)
	pushsr(%fs)
	pushsr(%gs)
		/* muck %cs, see note about pushsr() above */
	movl	0xf*4(%esp), %eax
	movzwl	%ax, %eax
	movl	%eax, 0xf*4(%esp)
#ifdef DEBUG_DEBUG
	movl	$0xb8280, %edi
	movl	0x0c*4(%esp), %eax	/* trapno */
	addb	$'0', %al
	movb	$0x17, %ah
	shll	$16, %eax
	movb	0x0d*4(%esp), %al	/* error */
	addb	$'0', %al
	movb	$0x17, %ah
	movl	%eax, (%edi)
#endif
	call	check_regs
#ifdef DEBUG_DEBUG
	movl	$0xb8290, %edi
	movl	$0x47394738, (%edi)
#endif
	popl	%gs
	popl	%fs
	popl	%es
	popl	%ds
	popal
	addl	$0x8, %esp
	hlt
	iret

ENTRY(check_regs)
#ifdef DEBUG_DEBUG
	movl	$0xb8284, %edi
	movl	$0x47334732, (%edi)
#endif
	movl	$0x10, %eax
	mov	%ax, %ds
	mov	%ax, %es
	movl	$reg, %edi
	cld
	movl	0x0c*4(%esp), %eax; stosl /* %eax	*/
	movl	0x0b*4(%esp), %eax; stosl /* %ecx	*/
	movl	0x0a*4(%esp), %eax; stosl /* %edx	*/
	movl	0x09*4(%esp), %eax; stosl /* %ebx	*/
	movl	0x08*4(%esp), %eax; stosl /* %esp	*/
	movl	0x07*4(%esp), %eax; stosl /* %ebp	*/
	movl	0x06*4(%esp), %eax; stosl /* %esi	*/
	movl	0x05*4(%esp), %eax; stosl /* %edi	*/
	movl	0x0f*4(%esp), %eax; stosl /* %eip	*/
	movl	0x11*4(%esp), %eax; stosl /* %eflags	*/
	movl	0x10*4(%esp), %eax; stosl /* %cs	*/
	mov		%ss,  %ax ; stosl /* %ss	*/
	movl	0x04*4(%esp), %eax; stosl /* %ds	*/
	movl	0x03*4(%esp), %eax; stosl /* %es	*/
	movl	0x02*4(%esp), %eax; stosl /* %fs	*/
	movl	0x01*4(%esp), %eax; stosl /* %gs	*/

#ifdef DEBUG_DEBUG
	movl	$0xb8288, %edi
	movl	$0x47354734, (%edi)
#endif
	movl	0x0e*4(%esp), %ecx	/* error */
	movl	0x0d*4(%esp), %eax	/* trapno */
	pushl	%ecx
	pushl	%eax
	call	dump_regs
	popl	%eax
	popl	%eax

#ifdef DEBUG_DEBUG
	movl	$0xb828c, %edi
	movl	$0x47374736, (%edi)
#endif
	ret
