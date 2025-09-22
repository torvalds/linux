/*	$OpenBSD: SYS.h,v 1.18 2023/12/11 22:24:15 kettenis Exp $	*/
/*	$NetBSD: SYS.h,v 1.4 1996/10/17 03:03:53 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <machine/asm.h>
#include <machine/pal.h>		/* for PAL_rdunique */
#include <sys/syscall.h>


/* offsetof(struct tib, tib_errno) - offsetof(struct tib, __tib_tcb) */
#define	TCB_OFFSET_ERRNO	(-12)

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
 * END() uses the alpha .end pseudo-op which requires a matching .ent,
 * so here's a short hand for just doing .size
 */
#define _END(x)		.size x, . - x

#define PINSYSCALL(sysno, label)					\
	.pushsection .openbsd.syscalls,"",@progbits;			\
	.p2align 2;							\
	.long label;							\
	.long sysno;							\
	.popsection;

/*
 * For functions implemented in ASM that aren't syscalls.
 *   END_STRONG(x)	Like DEF_STRONG() in C; for standard/reserved C names
 *   END_WEAK(x)	Like DEF_WEAK() in C; for non-ISO C names
 */
#define	END_STRONG(x)	END(x); _HIDDEN_FALIAS(x,x);		\
			_END(_HIDDEN(x))
#define	END_WEAK(x)	END_STRONG(x); .weak x

#define	CALLSYS_NOERROR(name)					\
	ldiq	v0, ___CONCAT(SYS_,name);			\
97:	call_pal PAL_OSF1_callsys;				\
	PINSYSCALL(___CONCAT(SYS_,name), 97b)

#define	CALLSYS_ERROR(name)					\
	CALLSYS_NOERROR(name);					\
	beq	a3, LLABEL(name,1);				\
	mov	v0, t0;						\
	call_pal PAL_rdunique;					\
	stl	t0, TCB_OFFSET_ERRNO(v0);			\
	ldiq	v0, -1;						\
	RET;							\
LLABEL(name,1):

#define __LEAF(p,n,e)						\
	LEAF(___CONCAT(p,n),e)
#define __END(p,n)						\
	END(___CONCAT(p,n));					\
	_HIDDEN_FALIAS(n,___CONCAT(p,n));			\
	_END(_HIDDEN(n))

#define	__SYSCALL(p,name)					\
__LEAF(p,name,0);			/* XXX # of args? */	\
	CALLSYS_ERROR(name)

#define	__SYSCALL_NOERROR(p,name)				\
__LEAF(p,name,0);			/* XXX # of args? */	\
	CALLSYS_NOERROR(name)


#define __RSYSCALL(p,name)					\
	__SYSCALL(p,name);					\
	RET;							\
__END(p,name)

#define __RSYSCALL_NOERROR(p,name)				\
	__SYSCALL_NOERROR(p,name);				\
	RET;							\
__END(p,name)


#define	__PSEUDO(p,label,name)					\
__LEAF(p,label,0);			/* XXX # of args? */	\
	CALLSYS_ERROR(name);					\
	RET;							\
__END(p,label);

#define	__PSEUDO_NOERROR(p,label,name)				\
__LEAF(p,label,0);			/* XXX # of args? */	\
	CALLSYS_NOERROR(name);					\
	RET;							\
__END(p,label);

#define ALIAS(prefix,name) WEAK_ALIAS(name, ___CONCAT(prefix,name));

/*
 * For the thread_safe versions, we prepend _thread_sys_ to the function
 * name so that the 'C' wrapper can go around the real name.
 */
# define SYSCALL(x)		ALIAS(_thread_sys_,x) \
				__SYSCALL(_thread_sys_,x)
# define SYSCALL_NOERROR(x)	ALIAS(_thread_sys_,x) \
				__SYSCALL_NOERROR(_thread_sys_,x)
# define RSYSCALL(x)		ALIAS(_thread_sys_,x) \
				__RSYSCALL(_thread_sys_,x); \
				_END(x)
# define RSYSCALL_HIDDEN(x)	__RSYSCALL(_thread_sys_,x)
# define RSYSCALL_NOERROR(x)	ALIAS(_thread_sys_,x) \
				__RSYSCALL_NOERROR(_thread_sys_,x); \
				_END(x)
# define PSEUDO(x,y)		ALIAS(_thread_sys_,x) \
				__PSEUDO(_thread_sys_,x,y); \
				_END(x)
# define PSEUDO_NOERROR(x,y)	ALIAS(_thread_sys_,x) \
				__PSEUDO_NOERROR(_thread_sys_,x,y); \
				_END(x)
# define SYSLEAF_HIDDEN(x,e)	__LEAF(_thread_sys_,x,e)
# define SYSLEAF(x,e)		ALIAS(_thread_sys_,x) \
				SYSLEAF_HIDDEN(x,e)
# define SYSCALL_END_HIDDEN(x)	__END(_thread_sys_,x)
# define SYSCALL_END(x)		SYSCALL_END_HIDDEN(x); \
				_END(x)
