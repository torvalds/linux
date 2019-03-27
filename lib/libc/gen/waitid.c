/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Jukka A. Ukkonen
 * All rights reserved.
 *
 * This software was developed by Jukka Ukkonen for FreeBSD.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <stddef.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "un-namespace.h"
#include "libc_private.h"

int __waitid(idtype_t, id_t, siginfo_t *, int);

int
__waitid(idtype_t idtype, id_t id, siginfo_t *info, int flags)
{
	int status;
	pid_t ret;

	ret = ((pid_t (*)(idtype_t, id_t, int *, int, struct __wrusage *,
	    siginfo_t *))__libc_interposing[INTERPOS_wait6])(idtype, id,
	    &status, flags, NULL, info);

	/*
	 * According to SUSv4, waitid() shall not return a PID when a
	 * process is found, but only 0.  If a process was actually
	 * found, siginfo_t fields si_signo and si_pid will be
	 * non-zero.  In case WNOHANG was set in the flags and no
	 * process was found those fields are set to zero using
	 * memset() below.
	 */
	if (ret == 0 && info != NULL)
		memset(info, 0, sizeof(*info));
	else if (ret > 0)
		ret = 0;
	return (ret);
}

__weak_reference(__waitid, waitid);
__weak_reference(__waitid, _waitid);
