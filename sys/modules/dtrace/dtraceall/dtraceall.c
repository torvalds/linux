/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 John Birrell <jb@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include "opt_nfs.h"

static int
dtraceall_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		break;

	case MOD_UNLOAD:
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;

	}

	return (error);
}

DEV_MODULE(dtraceall, dtraceall_modevent, NULL);
MODULE_VERSION(dtraceall, 1);

/* All the DTrace modules should be dependencies here: */
MODULE_DEPEND(dtraceall, opensolaris, 1, 1, 1);
MODULE_DEPEND(dtraceall, dtrace, 1, 1, 1);
MODULE_DEPEND(dtraceall, dtmalloc, 1, 1, 1);
#if defined(NFSCL)
MODULE_DEPEND(dtraceall, dtnfscl, 1, 1, 1);
#endif
#if defined(__aarch64__) || defined(__amd64__) || defined(__arm__) || \
    defined(__i386__) || defined(__mips__) || \
    defined(__powerpc__) || defined(__riscv)
MODULE_DEPEND(dtraceall, fbt, 1, 1, 1);
#endif
#if defined(__amd64__) || defined(__i386__)
MODULE_DEPEND(dtraceall, fasttrap, 1, 1, 1);
#endif
MODULE_DEPEND(dtraceall, sdt, 1, 1, 1);
MODULE_DEPEND(dtraceall, systrace, 1, 1, 1);
#if defined(COMPAT_FREEBSD32)
MODULE_DEPEND(dtraceall, systrace_freebsd32, 1, 1, 1);
#endif
MODULE_DEPEND(dtraceall, profile, 1, 1, 1);
