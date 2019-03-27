/*-
 * Copyright 1996-1998 John D. Polstra.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

	.text
	.align	4
	.globl	.rtld_start
	.type	.rtld_start,@function
.rtld_start:
	xorl	%ebp,%ebp		# Clear frame pointer for good form
	movl	%esp,%eax		# Save initial stack pointer
	movl	%esp,%esi		# Save initial stack pointer
	andl	$0xfffffff0,%esp	# Align stack pointer
	subl	$16,%esp		# A place to store exit procedure addr
	movl	%esp,%ebx		# save address of exit proc
	movl	%esp,%ecx		# construct address of obj_main
	addl	$4,%ecx
	subl	$4,%esp			# Keep stack aligned
	pushl	%ecx			# Pass address of obj_main
	pushl	%ebx			# Pass address of exit proc
	pushl	%eax			# Pass initial stack pointer to rtld
	call	_rtld			# Call rtld(sp); returns entry point
	addl	$16,%esp		# Remove arguments from stack
	popl	%edx			# Get exit procedure address
	movl	%esi,%esp		# Ignore obj_main
/*
 * At this point, %eax contains the entry point of the main program, and
 * %edx contains a pointer to a termination function that should be
 * registered with atexit().  (crt1.o registers it.)
 */
.globl .rtld_goto_main
.rtld_goto_main:	# This symbol exists just to make debugging easier.
	jmp	*%eax			# Enter main program


/*
 * Binder entry point.  Control is transferred to here by code in the PLT.
 * On entry, there are two arguments on the stack.  In ascending address
 * order, they are (1) "obj", a pointer to the calling object's Obj_Entry,
 * and (2) "reloff", the byte offset of the appropriate relocation entry
 * in the PLT relocation table.
 *
 * We are careful to preserve all registers, even the caller-save
 * registers.  That is because this code may be invoked by low-level
 * assembly-language code that is not ABI-compliant.
 */
	.align	4
	.globl	_rtld_bind_start
	.type	_rtld_bind_start,@function
_rtld_bind_start:
	pushf				# Save eflags
	pushl	%eax			# Save %eax
	pushl	%edx			# Save %edx
	pushl	%ecx			# Save %ecx
	pushl	20(%esp)		# Copy reloff argument
	pushl	20(%esp)		# Copy obj argument

	call	_rtld_bind		# Transfer control to the binder
	/* Now %eax contains the entry point of the function being called. */

	addl	$8,%esp			# Discard binder arguments
	movl	%eax,20(%esp)		# Store target over obj argument
	popl	%ecx			# Restore %ecx
	popl	%edx			# Restore %edx
	popl	%eax			# Restore %eax
	popf				# Restore eflags
	leal	4(%esp),%esp		# Discard reloff, do not change eflags
	ret				# "Return" to target address

	.section .note.GNU-stack,"",%progbits
