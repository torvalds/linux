/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_CRED_H_
#define	_OPENSOLARIS_SYS_CRED_H_

#include <sys/param.h>
#define	_WANT_UCRED
#include <sys/ucred.h>
#undef _WANT_UCRED

typedef struct ucred cred_t;
typedef struct ucred ucred_t;

#ifdef _KERNEL
#define	CRED()		(curthread->td_ucred)

/*
 * kcred is used when you need all privileges.
 */
#define	kcred	(thread0.td_ucred)

#define	crgetuid(cred)		((cred)->cr_uid)
#define	crgetruid(cred)		((cred)->cr_ruid)
#define	crgetgid(cred)		((cred)->cr_gid)
#define	crgetgroups(cred)	((cred)->cr_groups)
#define	crgetngroups(cred)	((cred)->cr_ngroups)
#define	crgetsid(cred, i)	(NULL)
#else	/* !_KERNEL */
#define	kcred		NULL
#define	CRED()		NULL
#endif	/* !_KERNEL */

#endif	/* _OPENSOLARIS_SYS_CRED_H_ */
