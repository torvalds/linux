/*-
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * Copyright (c) 2007 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project under contract to nCircle Network Security, Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR, NCIRCLE NETWORK SECURITY,
 * INC., OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*-
 * Test that raising current resource limits above hard resource limits
 * requires privilege.  We test three cases:
 *
 * - Raise the current above the maximum (privileged).
 * - Raise the current to the maximum (unprivileged).
 * - Raise the maximum (privileged).
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>

#include "main.h"

static int initialized;
static struct rlimit rl_base;
static struct rlimit rl_lowered;

int
priv_proc_setrlimit_setup(int asroot, int injail, struct test *test)
{

	if (getrlimit(RLIMIT_DATA, &rl_base) < 0) {
		warn("priv_proc_setrlimit_setup: getrlimit");
		return (-1);
	}

	/*
	 * Must lower current and limit to make sure there's room to try to
	 * raise them during tests.  Set current lower than max so we can
	 * raise it later also.
	 */
	rl_lowered = rl_base;
	rl_lowered.rlim_cur -= 20;
	rl_lowered.rlim_max -= 10;
	if (setrlimit(RLIMIT_DATA, &rl_lowered) < 0) {
		warn("priv_proc_setrlimit_setup: setrlimit");
		return (-1);
	}
	initialized = 1;
	return (0);
}

/*
 * Try increasing the maximum limits on the process, which requires
 * privilege.
 */
void
priv_proc_setrlimit_raisemax(int asroot, int injail, struct test *test)
{
	struct rlimit rl;
	int error;

	rl = rl_lowered;
	rl.rlim_max = rl_base.rlim_max;
	error = setrlimit(RLIMIT_DATA, &rl);
	if (asroot && injail)
		expect("priv_proc_setrlimit_raisemax(asroot, injail)", error,
		    0, 0);
	if (asroot && !injail)
		expect("priv_proc_setrlimit_raisemax(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_proc_setrlimit_raisemax(!asroot, injail)",
		    error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_proc_setrlimit_raisemax(!asroot, !injail)",
		    error, -1, EPERM);
}

/*
 * Try setting the current limit to the current maximum, which is allowed
 * without privilege.
 */
void
priv_proc_setrlimit_raisecur_nopriv(int asroot, int injail,
    struct test *test)
{
	struct rlimit rl;
	int error;

	rl = rl_lowered;
	rl.rlim_cur = rl.rlim_max;
	error = setrlimit(RLIMIT_DATA, &rl);
	if (asroot && injail)
		expect("priv_proc_setrlimit_raiscur_nopriv(asroot, injail)",
		    error, 0, 0);
	if (asroot && !injail)
		expect("priv_proc_setrlimit_raisecur_nopriv(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_proc_setrlimit_raisecur_nopriv(!asroot, injail)",
		    error, 0, 0);
	if (!asroot && !injail)
		expect("priv_proc_setrlimit_raisecur_nopriv(!asroot, !injail)",
		    error, 0, 0);
}

/*
 * Try raising the current limits above the maximum, which requires
 * privilege.
 */
void
priv_proc_setrlimit_raisecur(int asroot, int injail, struct test *test)
{
	struct rlimit rl;
	int error;

	rl = rl_lowered;
	rl.rlim_cur = rl.rlim_max + 10;
	error = setrlimit(RLIMIT_DATA, &rl);
	if (asroot && injail)
		expect("priv_proc_setrlimit_raisecur(asroot, injail)", error,
		    0, 0);
	if (asroot && !injail)
		expect("priv_proc_setrlimit_raisecur(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_proc_setrlimit_raisecur(!asroot, injail)",
		    error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_proc_setrlimit_raisecur(!asroot, !injail)",
		    error, -1, EPERM);
}

void
priv_proc_setrlimit_cleanup(int asroot, int injail, struct test *test)
{

	if (initialized)
		(void)setrlimit(RLIMIT_DATA, &rl_base);
	initialized = 0;
}
