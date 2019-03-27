/*	$NetBSD: lockf.c,v 1.3 2008/04/28 20:22:59 martin Exp $	*/
/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Klaus Klein.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "un-namespace.h"
#include "libc_private.h"

int
lockf(int filedes, int function, off_t size)
{
	struct flock fl;
	int cmd;

	fl.l_start = 0;
	fl.l_len = size;
	fl.l_whence = SEEK_CUR;

	switch (function) {
	case F_ULOCK:
		cmd = F_SETLK;
		fl.l_type = F_UNLCK;
		break;
	case F_LOCK:
		cmd = F_SETLKW;
		fl.l_type = F_WRLCK;
		break;
	case F_TLOCK:
		cmd = F_SETLK;
		fl.l_type = F_WRLCK;
		break;
	case F_TEST:
		fl.l_type = F_WRLCK;
		if (((int (*)(int, int, ...))
		    __libc_interposing[INTERPOS_fcntl])(filedes, F_GETLK, &fl)
		    == -1)
			return (-1);
		if (fl.l_type == F_UNLCK || (fl.l_sysid == 0 &&
		    fl.l_pid == getpid()))
			return (0);
		errno = EAGAIN;
		return (-1);
		/* NOTREACHED */
	default:
		errno = EINVAL;
		return (-1);
		/* NOTREACHED */
	}

	return (((int (*)(int, int, ...))
	    __libc_interposing[INTERPOS_fcntl])(filedes, cmd, &fl));
}
