/*	$OpenBSD: SYS.h,v 1.16 2024/03/27 20:03:29 miod Exp $	*/
/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)SYS.h	5.5 (Berkeley) 5/7/91
 *	$NetBSD: SYS.h,v 1.9 2006/01/06 06:19:20 uwe Exp $
 */

#include <machine/asm.h>
#include <sys/syscall.h>


/*
 * We need to offset the TCB pointer (in register gbr) by this much:
 *	offsetof(struct tib, tib_errno) - offsetof(struct tib, __tib_tcb)
 * That's negative on a variant I arch like sh, but you can't directly
 * load negative numbers or use them as displacements.  Ha!  So this is the
 * negative of the real value and we'll explicitly subtract it in the asm
 */
#define	TCB_OFFSET_ERRNO_NEG	8

/*
 * We define a hidden alias with the prefix "_libc_" for each global symbol
 * that may be used internally.  By referencing _libc_x instead of x, other
 * parts of libc prevent overriding by the application and avoid unnecessary
 * relocations.
 */
#define _HIDDEN(x)		_libc_##x
#define _HIDDEN_ALIAS(x,y)			\
	STRONG_ALIAS(_HIDDEN(x),y);		\
	.hidden _HIDDEN(x)
#define _HIDDEN_FALIAS(x,y)			\
	_HIDDEN_ALIAS(x,y);			\
	.type _HIDDEN(x),@function

/*
 * For functions implemented in ASM that aren't syscalls.
 *   END_STRONG(x)	Like DEF_STRONG() in C; for standard/reserved C names
 *   END_WEAK(x)	Like DEF_WEAK() in C; for non-ISO C names
 */
#define	END_STRONG(x)	SET_ENTRY_SIZE(x);		\
			_HIDDEN_FALIAS(x,x);		\
			SET_ENTRY_SIZE(_HIDDEN(x))
#define	END_WEAK(x)	END_STRONG(x); .weak x


#define	SYSENTRY(x)					\
	WEAK_ALIAS(x,_thread_sys_ ## x);		\
	ENTRY(_thread_sys_ ## x)
#define	SYSENTRY_HIDDEN(x)				\
	ENTRY(_thread_sys_ ## x)

#define PINSYSCALL(sysno, label)					\
	.pushsection .openbsd.syscalls,"",@progbits;			\
	.p2align 2;							\
	.long label;							\
	.long sysno;							\
	.popsection;

#ifdef __ASSEMBLER__
/*
 * If the system call number fits in a 8-bit signed value (i.e. fits in 7 bits),
 * then we can use the #imm8 addressing mode. Otherwise, we'll load the number
 * from memory at the end of the system call wrapper.
 */

.macro systrap num
.iflt \num - 128
	mov	# \num, r0
97:	trapa	#0x80
	PINSYSCALL(\num, 97b)
.else
	mov.l	903f, r0
97:	trapa	#0x80
	PINSYSCALL(\num, 97b)
.endif
.endm

.macro systrap_data num
.iflt \num - 128
.else
	.text
	.align	2
 903:	.long	\num
.endif
.endm

.macro syscall_error num
.ifeq \num - SYS_lseek
	mov	#-1, r1
.endif
	rts
	 mov	#-1, r0
.endm

#endif

#define SYSTRAP(x)					\
		systrap	SYS_ ## x

#define _SYSCALL_NOERROR(x,y)				\
		SYSENTRY(x);				\
		SYSTRAP(y)
#define _SYSCALL_HIDDEN_NOERROR(x,y)			\
		SYSENTRY_HIDDEN(x);			\
		SYSTRAP(y)

#define SET_ERRNO_AND_RETURN(y)				\
		stc	gbr,r1;				\
		mov	#TCB_OFFSET_ERRNO_NEG,r2;	\
		sub	r2,r1;				\
		mov.l	r0,@r1;				\
		syscall_error SYS_ ## y

#define _SYSCALL(x,y)					\
		.text;					\
	911:	SET_ERRNO_AND_RETURN(y);		\
		_SYSCALL_NOERROR(x,y);			\
		bf	911b
#define _SYSCALL_HIDDEN(x,y)				\
		.text;					\
	911:	SET_ERRNO_AND_RETURN(y);		\
		_SYSCALL_HIDDEN_NOERROR(x,y);		\
		bf	911b

#define	__END_HIDDEN(x,y)				\
		systrap_data SYS_ ## y;			\
		SET_ENTRY_SIZE(_thread_sys_ ## x);	\
		_HIDDEN_FALIAS(x,_thread_sys_ ## x);	\
		SET_ENTRY_SIZE(_HIDDEN(x))
#define	__END(x,y)					\
		__END_HIDDEN(x,y); SET_ENTRY_SIZE(x)

#define SYSCALL_NOERROR(x)				\
		_SYSCALL_NOERROR(x,x)

#define SYSCALL(x)					\
		_SYSCALL(x,x)

#define PSEUDO_NOERROR(x,y)				\
		_SYSCALL_NOERROR(x,y);			\
		rts;					\
		 nop;					\
		__END(x,y)

#define PSEUDO(x,y)					\
		_SYSCALL(x,y);				\
		rts;					\
		 nop;					\
		__END(x,y)

#define PSEUDO_HIDDEN(x,y)				\
		_SYSCALL_HIDDEN(x,y);			\
		rts;					\
		 nop;					\
		__END_HIDDEN(x,y)

#define RSYSCALL_NOERROR(x)		PSEUDO_NOERROR(x,x)
#define RSYSCALL(x)			PSEUDO(x,x)
#define RSYSCALL_HIDDEN(x)		PSEUDO_HIDDEN(x,x)
#define SYSCALL_END(x)			__END(x,x)
#define SYSCALL_END_HIDDEN(x)		__END_HIDDEN(x,x)
