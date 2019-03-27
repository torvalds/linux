/* From: NetBSD: setjmp.h,v 1.2 1997/04/06 08:47:41 cgd Exp */

/*-
 * SPDX-License-Identifier: MIT-CMU
 *
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
 *
 *	JNPR: setjmp.h,v 1.2 2006/12/02 09:53:41 katta
 * $FreeBSD$
 */

#ifndef _MACHINE_SETJMP_H_
#define	_MACHINE_SETJMP_H_

/*
 * machine/setjmp.h: machine dependent setjmp-related information.
 */

#include <sys/cdefs.h>

#define	_JBLEN	95		/* size, in longs (or long longs), of a jmp_buf */

/*
 * jmp_buf and sigjmp_buf are encapsulated in different structs to force
 * compile-time diagnostics for mismatches.  The structs are the same
 * internally to avoid some run-time errors for mismatches.
 */
#ifndef _LOCORE
#ifndef __ASSEMBLER__
#if __BSD_VISIBLE || __POSIX_VISIBLE || __XSI_VISIBLE
#ifdef __mips_n32
typedef struct _sigjmp_buf { long long _sjb[_JBLEN + 1]; } sigjmp_buf[1];
#else
typedef struct _sigjmp_buf { long _sjb[_JBLEN + 1]; } sigjmp_buf[1];
#endif
#endif

#ifdef __mips_n32
typedef struct _jmp_buf { long long _jb[_JBLEN + 1]; } jmp_buf[1];
#else
typedef struct _jmp_buf { long _jb[_JBLEN + 1]; } jmp_buf[1];
#endif
#endif /* __ASSEMBLER__ */
#endif /* _LOCORE */

#endif /* _MACHINE_SETJMP_H_ */
