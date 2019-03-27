/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_CMN_ERR_H
#define	_SYS_CMN_ERR_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if defined(_KERNEL) && !defined(_ASM)
#include <sys/va_list.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* Common error handling severity levels */

#define	CE_CONT		0	/* continuation		*/
#define	CE_NOTE		1	/* notice		*/
#define	CE_WARN		2	/* warning		*/
#define	CE_PANIC	3	/* panic		*/
#define	CE_IGNORE	4	/* print nothing	*/

#ifndef _ASM

#ifdef _KERNEL

/*PRINTFLIKE2*/
extern void cmn_err(int, const char *, ...)
    __KPRINTFLIKE(2);
#pragma rarely_called(cmn_err)

extern void vzcmn_err(zoneid_t, int, const char *, __va_list)
    __KVPRINTFLIKE(3);
#pragma rarely_called(vzcmn_err)

extern void vcmn_err(int, const char *, __va_list)
    __KVPRINTFLIKE(2);
#pragma rarely_called(vcmn_err)

/*PRINTFLIKE3*/
extern void zcmn_err(zoneid_t, int, const char *, ...)
    __KPRINTFLIKE(3);
#pragma rarely_called(zcmn_err)

/*PRINTFLIKE1*/
extern void printf(const char *, ...)
    __KPRINTFLIKE(1);
#pragma	rarely_called(printf)

extern void vzprintf(zoneid_t, const char *, __va_list)
    __KVPRINTFLIKE(2);
#pragma rarely_called(vzprintf)

/*PRINTFLIKE2*/
extern void zprintf(zoneid_t, const char *, ...)
    __KPRINTFLIKE(2);
#pragma rarely_called(zprintf)

extern void vprintf(const char *, __va_list)
    __KVPRINTFLIKE(1);
#pragma	rarely_called(vprintf)

/*PRINTFLIKE1*/
extern void uprintf(const char *, ...)
    __KPRINTFLIKE(1);
#pragma rarely_called(uprintf)

extern void vuprintf(const char *, __va_list)
    __KVPRINTFLIKE(1);
#pragma rarely_called(vuprintf)

/*PRINTFLIKE3*/
extern size_t snprintf(char *, size_t, const char *, ...)
    __KPRINTFLIKE(3);
extern size_t vsnprintf(char *, size_t, const char *, __va_list)
    __KVPRINTFLIKE(3);
/*PRINTFLIKE2*/
extern char *sprintf(char *, const char *, ...)
    __KPRINTFLIKE(2);
extern char *vsprintf(char *, const char *, __va_list)
    __KVPRINTFLIKE(2);

/*PRINTFLIKE1*/
extern void panic(const char *, ...)
    __KPRINTFLIKE(1) __NORETURN;
#pragma rarely_called(panic)

extern void vpanic(const char *, __va_list)
    __KVPRINTFLIKE(1) __NORETURN;
#pragma rarely_called(vpanic)

#endif /* _KERNEL */
#endif /* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CMN_ERR_H */
