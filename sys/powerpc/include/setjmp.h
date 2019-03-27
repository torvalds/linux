/*-
 *	$NetBSD: setjmp.h,v 1.3 1998/09/16 23:51:27 thorpej Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_SETJMP_H_
#define	_MACHINE_SETJMP_H_

#include <sys/cdefs.h>

#ifdef _KERNEL
#define	_JBLEN	25	/* Kernel doesn't save FP and Altivec regs */
#else
#define	_JBLEN	100
#endif

/*
 * jmp_buf and sigjmp_buf are encapsulated in different structs to force
 * compile-time diagnostics for mismatches.  The structs are the same
 * internally to avoid some run-time errors for mismatches.
 */
#if __BSD_VISIBLE || __POSIX_VISIBLE || __XSI_VISIBLE
typedef	struct _sigjmp_buf { long _sjb[_JBLEN + 1]; } sigjmp_buf[1];
#endif

typedef	struct _jmp_buf { long _jb[_JBLEN + 1]; } jmp_buf[1];

#endif /* !_MACHINE_SETJMP_H_ */
