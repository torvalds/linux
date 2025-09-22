/*	$OpenBSD: SYS.h,v 1.26 2023/12/10 16:45:51 deraadt Exp $*/
/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 */

#include <sys/syscall.h>
#include "DEFS.h"


#define	__CONCAT(p,x)		p##x
#define	__ENTRY(p,x)		ENTRY(__CONCAT(p,x))
#define	__END(p,x)		END(__CONCAT(p,x)); \
				_HIDDEN_ALIAS(x,__CONCAT(p,x)); \
				END(_HIDDEN(x))
#define	__SYSCALLNAME(p,x)	__CONCAT(p,x)
#define	__ALIAS(prefix,name)	WEAK_ALIAS(name,__CONCAT(prefix,name))

#ifdef __PIC__
#define	PIC_SAVE(reg)		or reg, %r25, %r0
#define	PIC_RESTORE(reg)	or %r25, reg, %r0
#define	PIC_SETUP							\
	or	%r11, %r0,  %r1;					\
	or.u	%r25, %r0,  %hi16(.Lpic#abdiff);			\
	bsr.n	.Lpic;							\
	 or	%r25, %r25, %lo16(.Lpic#abdiff);			\
.Lpic:	add	%r25, %r25, %r1;					\
	or	%r1,  %r0,  %r11
#if __PIC__ > 1
#define	PIC_LOAD(reg,sym)						\
	or.u	%r11, %r0,  %hi16(__CONCAT(sym,#got_rel));		\
	or	%r11, %r11, %lo16(__CONCAT(sym,#got_rel));		\
	ld	reg,  %r25, %r11
#define	PIC_STORE(reg,sym)						\
	or.u	%r11, %r0,  %hi16(__CONCAT(sym,#got_rel));		\
	or	%r11, %r11, %lo16(__CONCAT(sym,#got_rel));		\
	st	reg,  %r25, %r11
#else		/* -fpic */
#define	PIC_LOAD(reg,sym)						\
	ld	%r11, %r25, __CONCAT(sym,#got_rel);			\
	ld	reg,  %r11, %r0
#define	PIC_STORE(reg,sym)						\
	ld	%r11, %r25, __CONCAT(sym,#got_rel);			\
	st	reg,  %r11, %r0
#endif
#define	CERROR	__cerror#plt
#else	/* __PIC__ */
#define	CERROR	__cerror
#endif	/* __PIC__ */

#define	__DO_SYSCALL(x)							\
	or %r13, %r0, __SYSCALLNAME(SYS_,x);				\
97:	tb0 0, %r0, 450;						\
	PINSYSCALL(__SYSCALLNAME(SYS_,x), 97b)

#define	__SYSCALL__NOERROR(p,x,y)					\
	__ENTRY(p,x);							\
	__ALIAS(p,x);							\
	__DO_SYSCALL(y)
#define	__SYSCALL_HIDDEN__NOERROR(p,x,y)				\
	__ENTRY(p,x);							\
	__DO_SYSCALL(y)

#define	__SYSCALL(p,x,y)						\
	__SYSCALL__NOERROR(p,x,y);					\
	br CERROR
#define	__SYSCALL_HIDDEN(p,x,y)						\
	__SYSCALL_HIDDEN__NOERROR(p,x,y);				\
	br CERROR

#define	__PSEUDO_NOERROR(p,x,y)						\
	__SYSCALL__NOERROR(p,x,y);					\
	or %r0, %r0, %r0;						\
	jmp %r1;							\
	__END(p,x); END(x)

#define	__PSEUDO(p,x,y)							\
	__SYSCALL(p,x,y);						\
	jmp %r1;							\
	__END(p,x); END(x)
#define	__PSEUDO_HIDDEN(p,x,y)						\
	__SYSCALL_HIDDEN(p,x,y);					\
	jmp %r1;							\
	__END(p,x)

/*
 * System calls entry points are really named _thread_sys_{syscall},
 * and weakly aliased to the name {syscall}. This allows the thread
 * library to replace system calls at link time.
 */
#define	SYSCALL(x)		__SYSCALL(_thread_sys_,x,x)
#define	RSYSCALL(x)		__PSEUDO(_thread_sys_,x,x)
#define	RSYSCALL_HIDDEN(x)	__PSEUDO_HIDDEN(_thread_sys_,x,x)
#define	PSEUDO(x,y)		__PSEUDO(_thread_sys_,x,y)
#define	PSEUDO_NOERROR(x,y)	__PSEUDO_NOERROR(_thread_sys_,x,y)
#define	SYSENTRY_HIDDEN(x)	__ENTRY(_thread_sys_,x)
#define	SYSENTRY(x)		SYSENTRY_HIDDEN(x);		\
				__ALIAS(_thread_sys_,x)
#define	SYSCALL_END_HIDDEN(x)	__END(_thread_sys_,x)
#define	SYSCALL_END(x)		SYSCALL_END_HIDDEN(x); END(x)

#define	ASMSTR		.asciz
