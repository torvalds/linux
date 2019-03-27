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
 * $FreeBSD$
 */

/* The 'ret' macro doesn't work in this file if GPROF is enabled. */
#ifdef GPROF
#undef GPROF
#endif

#include <machine/asmacros.h>

/*
 * This file contains assembly language wrappers for the different
 * calling conventions supported by Windows on the i386 architecture.
 * In FreeBSD, the whole OS typically use same C calling convention
 * everywhere, namely _cdecl. Windows, on the other hand, uses several
 * different C calling conventions depending on the circumstances:
 *
 * _stdcall: Used for most ordinary Windows APIs. With _stdcall,
 * arguments are passed on the stack, and the callee unwinds the stack
 * before returning control to the caller. Not suitable for variadic
 * functions.
 *
 * _fastcall: Used for some APIs that may be invoked frequently and
 * where speed is a critical factor (e.g. KeAcquireSpinLock() and
 * KeReleaseSpinLock()) Similar to _stdcall, except the first 2 32-bit
 * or smaller arguments are passed in the %ecx and %edx registers
 * instead of on the stack. Not suitable for variadic functions.
 *
 * _cdecl: Used for standard C library routines and for variadic
 * functions.
 *
 * _regparm(3): Used for certain assembly routines. All arguments
 * passed in %eax, %ecx and %edx.
 *
 * Furthermore, there is an additional wrinkle that's not obvious
 * with all code: Microsoft supports the use of exceptions in C
 * (__try/__except) both in user _and_ kernel mode. Sadly, Windows
 * structured exception handling uses machine-specific features
 * that conflict rather badly with FreeBSD. (See utility routines
 * at the end of this module for more details.)
 *
 * We want to support these calling conventions in as portable a manner
 * as possible. The trick is doing it not only with different versions
 * of GNU C, but with compilers other than GNU C (e.g. the Solaris
 * SunOne C compiler). The only sure fire method is with assembly
 * language trampoline code which both fixes up the argument passing,
 * stack unwinding and exception/thread context all at once.
 *
 * You'll notice that we call the thunk/unthunk routines in the
 * *_wrap() functions in an awkward way. Rather than branching
 * directly to the address, we load the address into a register
 * first as a literal value, then we branch to it. This is done
 * to insure that the assembler doesn't translate the branch into
 * a relative branch. We use the *_wrap() routines here as templates
 * and create the actual trampolines at run time, at which point
 * we only know the absolute addresses of the thunk and unthunk
 * routines. So we need to make sure the templates have enough
 * room in them for the full address.
 *
 * Also note that when we call the a thunk/unthunk routine after
 * invoking a wrapped function, we have to make sure to preserve
 * the value returned from that function. Most functions return
 * a 32-bit value in %eax, however some routines return 64-bit
 * values, which span both %eax and %edx. Consequently, we have
 * to preserve both registers.
 */

/*
 * Handle _stdcall going from Windows to UNIX.
 * This is frustrating, because to do it right you have to
 * know how many arguments the called function takes, and there's
 * no way to figure this out on the fly: you just have to be told
 * ahead of time. We assume there will be 16 arguments. I don't
 * think there are any Windows APIs that require this many.
 */
 
	.globl x86_stdcall_wrap_call
	.globl x86_stdcall_wrap_arg
	.globl x86_stdcall_wrap_end

ENTRY(x86_stdcall_wrap)
	push	%esi
	push	%edi
	sub	$64,%esp
	mov	%esp,%esi
	add	$64+8+4,%esi
	mov	%esp,%edi
	mov	$16,%ecx	# handle up to 16 args
	rep
	movsl

	movl	$ctxsw_wtou, %eax
	call	*%eax           # unthunk

x86_stdcall_wrap_call:
        movl    $0,%eax
	call	*%eax		# jump to routine
	push	%eax		# preserve return val
	push	%edx

	movl	$ctxsw_utow, %eax
	call	*%eax		# thunk

	pop	%edx
	pop	%eax		# restore return val

	add	$64,%esp	# clean the stack
	pop	%edi
	pop	%esi
x86_stdcall_wrap_arg:
	ret	$0xFF
x86_stdcall_wrap_end:


/*
 * Handle _stdcall going from UNIX to Windows. This routine
 * expects to be passed the function to be called, number of
 * args and the arguments for the Windows function on the stack.
 */

ENTRY(x86_stdcall_call)
	push	%esi		# must preserve %esi
	push	%edi		# and %edi

	mov	16(%esp),%eax	# get arg cnt
	mov	%eax,%ecx	# save as copy count
	mov	%esp,%esi	# Set source address register to point to
	add	$20,%esi	# first agument to be forwarded.
	shl	$2,%eax		# turn arg cnt into offset
	sub	%eax,%esp	# shift stack to new location 
	mov	%esp,%edi	# store dest copy addr
	rep			# do the copy
	movsl

	call	ctxsw_utow	# thunk

	call	*12(%edi)	# branch to stdcall routine
	push	%eax		# preserve return val
	push	%edx

	call	ctxsw_wtou	# unthunk

	pop	%edx
	pop	%eax		# restore return val
	mov	%edi,%esp	# restore stack
	pop	%edi		# restore %edi
	pop	%esi		# and %esi
	ret

/*
 * Fastcall support. Similar to _stdcall, except the first
 * two arguments are passed in %ecx and %edx. It happens we
 * only support a small number of _fastcall APIs, none of them
 * take more than three arguments. So to keep the code size
 * and complexity down, we only handle 3 arguments here.
 */

/* Call _fastcall function going from Windows to UNIX. */

	.globl x86_fastcall_wrap_call
	.globl x86_fastcall_wrap_arg
	.globl x86_fastcall_wrap_end

ENTRY(x86_fastcall_wrap)
	mov	4(%esp),%eax
	push	%eax
	push	%edx
	push	%ecx

	movl	$ctxsw_wtou, %eax
	call	*%eax		# unthunk

x86_fastcall_wrap_call:
	mov	$0,%eax
	call	*%eax		# branch to fastcall routine
	push	%eax		# preserve return val
	push	%edx

	movl	$ctxsw_utow, %eax
	call	*%eax		# thunk

	pop	%edx
	pop	%eax		# restore return val
	add	$12,%esp	# clean the stack
x86_fastcall_wrap_arg:
	ret	$0xFF
x86_fastcall_wrap_end:

/*
 * Call _fastcall function going from UNIX to Windows.
 * This routine isn't normally used since NDIS miniport drivers
 * only have _stdcall entry points, but it's provided anyway
 * to round out the API, and for testing purposes.
 */

ENTRY(x86_fastcall_call)
	mov	4(%esp),%eax
	push	16(%esp)

	call	ctxsw_utow	# thunk

	mov	12(%esp),%ecx
	mov	16(%esp),%edx
	call	*8(%esp)	# branch to fastcall routine
	push	%eax		# preserve return val
	push	%edx

	call	ctxsw_wtou	# unthunk

	pop	%edx
	pop	%eax		# restore return val
	add	$4,%esp		# clean the stack
	ret

/*
 * Call regparm(3) function going from Windows to UNIX. Arguments
 * are passed in %eax, %edx and %ecx. Note that while additional
 * arguments are passed on the stack, we never bother when them,
 * since the only regparm(3) routines we need to wrap never take
 * more than 3 arguments.
 */

	.globl x86_regparm_wrap_call
	.globl x86_regparm_wrap_end

ENTRY(x86_regparm_wrap)
	push	%ecx
	push	%edx
	push	%eax

	movl	$ctxsw_wtou, %eax
	call	*%eax		# unthunk

x86_regparm_wrap_call:
	movl	$0,%eax
	call	*%eax		# jump to routine
	push	%eax		# preserve return val
	push	%edx		# preserve return val

	movl	$ctxsw_utow, %eax
	call	*%eax		# thunk

	pop	%edx		# restore return val
	pop	%eax		# restore return val
	add	$12,%esp	# restore stack
	ret
x86_regparm_wrap_end:

/*
 * Call regparm(3) function going from UNIX to Windows.
 * This routine isn't normally used since NDIS miniport drivers
 * only have _stdcall entry points, but it's provided anyway
 * to round out the API, and for testing purposes.
 */

ENTRY(x86_regparm_call)
	call	ctxsw_utow	# thunk

	mov	8(%esp),%eax
	mov	12(%esp),%edx
	mov	16(%esp),%ecx
	call	*4(%esp)	# branch to fastcall routine
	push	%eax		# preserve return val
	push	%edx		# preserve return val

	call	ctxsw_wtou	# unthunk

	pop	%edx		# restore return val
	pop	%eax		# restore return val
	ret

/*
 * Ugly hack alert:
 *
 * On Win32/i386, using __try/__except results in code that tries to
 * manipulate what's supposed to be the Windows Threada Environment 
 * Block (TEB), which one accesses via the %fs register. In particular,
 * %fs:0 (the first DWORD in the TEB) points to the exception
 * registration list. Unfortunately, FreeBSD uses %fs for the
 * per-cpu data structure (pcpu), and we can't allow Windows code
 * to muck with that. I don't even know what Solaris uses %fs for
 * (or if it even uses it at all).
 *
 * Even worse, in 32-bit protected mode, %fs is a selector that
 * refers to an entry in either the GDT or the LDT. Ideally, we would
 * like to be able to temporarily point it at another descriptor
 * while Windows code executes, but to do that we need a separate
 * descriptor entry of our own to play with.
 *
 * Therefore, we go to some trouble to learn the existing layout of
 * the GDT and update it to include an extra entry that we can use.
 * We need the following utility routines to help us do that. On
 * FreeBSD, index #7 in the GDT happens to be unused, so we turn
 * this into our own data segment descriptor. It would be better
 * if we could use a private LDT entry, but there's no easy way to
 * do that in SMP mode because of the way FreeBSD handles user LDTs.
 *
 * Once we have a custom descriptor, we have to thunk/unthunk whenever
 * we cross between FreeBSD code and Windows code. The thunking is
 * based on the premise that when executing instructions in the
 * Windows binary itself, we won't go to sleep. This is because in
 * order to yield the CPU, the code has to call back out to a FreeBSD
 * routine first, and when that happens we can unthunk in order to
 * restore FreeBSD context. What we're desperately trying to avoid is
 * being involuntarily pre-empted with the %fs register still pointing
 * to our fake TIB: if FreeBSD code runs with %fs pointing at our
 * Windows TIB instead of pcpu, we'll panic the kernel. Fortunately,
 * the only way involuntary preemption can occur is if an interrupt
 * fires, and the trap handler saves/restores %fs for us.
 *
 * The thunking routines themselves, ctxsw_utow() (Context SWitch UNIX
 * to Windows) and ctxsw_wtou() (Context SWitch Windows to UNIX), are
 * external to this module. This is done simply because it's easier 
 * to manipulate data structures in C rather than assembly.
 */

ENTRY(x86_getldt)
	movl	4(%esp),%eax
	sgdtl	(%eax)
	movl	8(%esp),%eax
	sldt	(%eax)
	xor	%eax,%eax
	ret

ENTRY(x86_setldt)
	movl    4(%esp),%eax
	lgdt	(%eax)
	jmp	1f
	nop
1:
	movl    8(%esp),%eax
	lldt	%ax
	xor	%eax,%eax
	ret

ENTRY(x86_getfs)
	mov	%fs,%ax
	ret

ENTRY(x86_setfs)
	mov	4(%esp),%fs
	ret

ENTRY(x86_gettid)
	mov	%fs:12,%eax
	ret

ENTRY(x86_critical_enter)
	cli
	ret

ENTRY(x86_critical_exit)
	sti
	ret
