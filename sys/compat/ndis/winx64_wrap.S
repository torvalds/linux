/*-
 * Copyright (c) 2005
 *      Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *      This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The x86_64 callback routines were written and graciously submitted
 * by Ville-Pertti Keinonen <will@exomi.com>.
 *
 * $FreeBSD$
 */

#include <machine/asmacros.h>

/*
 * Wrapper for handling up to 16 arguments. We can't really
 * know how many arguments the caller will pass us. I'm taking an
 * educated guess that we'll never get over 16. Handling too
 * few arguments is bad. Handling too many is inefficient, but
 * not fatal. If someone can think of a way to handle an arbitrary
 * number of arguments with more elegant code, freel free to let
 * me know.
 *
 * Standard amd64 calling conventions specify the following registers
 * to be used for passing the first 6 arguments:
 *
 *   %rdi, %rsi, %rdx, %rcx, %r8, %r9
 *
 * Further arguments are passed on the stack (the 7th argument is
 * located immediately after the return address).
 *
 * Windows x86_64 calling conventions only pass the first 4
 * arguments in registers:
 *
 *   %rcx, %rdx, %r8, %r9
 *
 * Even when arguments are passed in registers, the stack must have
 * space reserved for those arguments.  Thus the 5th argument (the
 * first non-register argument) is placed 32 bytes after the return
 * address.  Additionally, %rdi and %rsi must be preserved. (These
 * two registers are not scratch registers in the standard convention.)
 *
 * Note that in this template, we load a contrived 64 bit address into
 * %r11 to represent our jump address. This is to guarantee that the
 * assembler leaves enough room to patch in an absolute 64-bit address
 * later. The idea behind this code is that we want to avoid having to
 * manually create all the wrapper functions at compile time with
 * a bunch of macros. This is doable, but a) messy and b) requires
 * us to maintain two separate tables (one for the UNIX function
 * pointers and another with the wrappers). This means I'd have to
 * update two different tables each time I added a function.
 *
 * To avoid this, we create the wrappers at runtime instead. The
 * image patch tables now contain two pointers: one two the normal
 * routine, and a blank one for the wrapper. To construct a wrapper,
 * we allocate some memory and copy the template function into it,
 * then patch the function pointer for the routine we want to wrap
 * into the newly created wrapper. The subr_pe module can then
 * simply patch the wrapper routine into the jump table into the
 * windows image. As a bonus, the wrapper pointer not only serves
 * as the wrapper entry point address, it's also a data pointer
 * that we can pass to free() later when we unload the module.
 */

	.globl x86_64_wrap_call
	.globl x86_64_wrap_end

ENTRY(x86_64_wrap)
	push	%rbp		# insure that the stack
	mov	%rsp,%rbp	# is 16-byte aligned
	and	$-16,%rsp	#
	subq	$96,%rsp	# allocate space on stack
	mov     %rsi,96-8(%rsp)	# save %rsi
	mov     %rdi,96-16(%rsp)# save %rdi
	mov	%rcx,%r10	# temporarily save %rcx in scratch
	lea	56+8(%rbp),%rsi	# source == old stack top (stack+56)
	mov	%rsp,%rdi	# destination == new stack top
	mov	$10,%rcx	# count == 10 quadwords
	rep
	movsq			# copy old stack contents to new location
	mov	%r10,%rdi	# set up arg0 (%rcx -> %rdi)
	mov     %rdx,%rsi	# set up arg1 (%rdx -> %rsi)
	mov     %r8,%rdx	# set up arg2 (%r8 -> %rdx)
	mov     %r9,%rcx	# set up arg3 (%r9 -> %rcx)
	mov	40+8(%rbp),%r8	# set up arg4 (stack+40 -> %r8)
	mov	48+8(%rbp),%r9	# set up arg5 (stack+48 -> %r9)
	xor	%rax,%rax	# clear return value
x86_64_wrap_call:
	mov	$0xFF00FF00FF00FF00,%r11
	callq	*%r11		# call routine
	mov	96-16(%rsp),%rdi# restore %rdi
	mov	96-8(%rsp),%rsi	# restore %rsi
	leave			# delete space on stack
	ret
x86_64_wrap_end:

/*
 * Functions for invoking x86_64 callbacks.  In each case, the first
 * argument is a pointer to the function.
 */

ENTRY(x86_64_call1)
	subq	$40,%rsp
	mov	%rsi,%rcx
	call	*%rdi
	addq	$40,%rsp
	ret

ENTRY(x86_64_call2)
	subq	$40,%rsp
	mov	%rsi,%rcx
	/* %rdx is already correct */
	call	*%rdi
	addq	$40,%rsp
	ret

ENTRY(x86_64_call3)
	subq	$40,%rsp
	mov	%rcx,%r8
	mov	%rsi,%rcx
	call	*%rdi
	addq	$40,%rsp
	ret

ENTRY(x86_64_call4)
	subq	$40,%rsp
	mov	%r8,%r9
	mov	%rcx,%r8
	mov	%rsi,%rcx
	call	*%rdi
	addq	$40,%rsp
	ret

ENTRY(x86_64_call5)
	subq	$48,%rsp
	mov	%r9,32(%rsp)
	mov	%r8,%r9
	mov	%rcx,%r8
	mov	%rsi,%rcx
	call	*%rdi
	addq	$48,%rsp
	ret

ENTRY(x86_64_call6)
	subq	$56,%rsp
	mov	56+8(%rsp),%rax
	mov	%r9,32(%rsp)
	mov	%rax,40(%rsp)
	mov	%r8,%r9
	mov	%rcx,%r8
	mov	%rsi,%rcx
	call	*%rdi
	addq	$56,%rsp
	ret
