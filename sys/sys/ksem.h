/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Alfred Perlstein <alfred@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _POSIX4_KSEM_H_
#define	_POSIX4_KSEM_H_

#if !defined(_KERNEL) && !defined(_WANT_FILE)
#error "no user-serviceable parts inside"
#endif

#include <sys/condvar.h>

struct ksem {
	int		ks_ref;		/* number of references */
	mode_t		ks_mode;	/* protection bits */
	uid_t		ks_uid;		/* creator uid */
	gid_t		ks_gid;		/* creator gid */
	unsigned int	ks_value;	/* current value */
	struct cv	ks_cv;		/* waiters sleep here */
	int		ks_waiters;	/* number of waiters */
	int		ks_flags;

	/*
	 * Values maintained solely to make this a better-behaved file
	 * descriptor for fstat() to run on.
	 *
	 * XXX: dubious
	 */
	struct timespec	ks_atime;
	struct timespec	ks_mtime;
	struct timespec	ks_ctime;
	struct timespec	ks_birthtime;

	struct label	*ks_label;	/* MAC label */
	const char	*ks_path;
};

#define	KS_ANONYMOUS	0x0001		/* Anonymous (unnamed) semaphore. */
#define	KS_DEAD		0x0002		/* No new waiters allowed. */

#endif /* !_POSIX4_KSEM_H_ */
