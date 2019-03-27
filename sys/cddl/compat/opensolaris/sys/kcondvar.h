/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2013 iXsystems, Inc.
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

#ifndef _OPENSOLARIS_SYS_CONDVAR_H_
#define	_OPENSOLARIS_SYS_CONDVAR_H_

#include <sys/param.h>
#include <sys/proc.h>

#ifdef _KERNEL

#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/time.h>

typedef struct cv	kcondvar_t;

typedef enum {
	CV_DEFAULT,
	CV_DRIVER
} kcv_type_t;

#define	zfs_cv_init(cv, name, type, arg)	do {			\
	const char *_name;						\
	ASSERT((type) == CV_DEFAULT);					\
	for (_name = #cv; *_name != '\0'; _name++) {			\
		if (*_name >= 'a' && *_name <= 'z')			\
			break;						\
	}								\
	if (*_name == '\0')						\
		_name = #cv;						\
	cv_init((cv), _name);						\
} while (0)
#define	cv_init(cv, name, type, arg)	zfs_cv_init(cv, name, type, arg)

static clock_t
cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim, hrtime_t res,
    int flag)
{

	return (cv_timedwait_sbt(cvp, mp, nstosbt(tim), nstosbt(res), 0));
}

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_CONDVAR_H_ */
