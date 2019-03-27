/*-
 * Copyright (c) 2007 Robert M. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project.
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

/*
 * Confirm that various UID/GID/etc-related system calls require root
 * privilege in the absence of any saved/real/etc variations in the
 * credential.  It would be nice to also check cases where those bits of the
 * credential are more interesting.
 *
 * XXXRW: Add support for testing more diverse real/saved scenarios.
 */

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "main.h"

int
priv_cred_setup(int asroot, int injail, struct test *test)
{

	return (0);
}

void
priv_cred_setuid(int asroot, int injail, struct test *test)
{
	int error;

	error = setuid(UID_OTHER);
	if (asroot && injail)
		expect("priv_setuid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setuid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setuid(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setuid(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_seteuid(int asroot, int injail, struct test *test)
{
	int error;

	error = seteuid(UID_OTHER);
	if (asroot && injail)
		expect("priv_seteuid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_seteuid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_seteuid(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_seteuid(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_setgid(int asroot, int injail, struct test *test)
{
	int error;

	error = setgid(GID_OTHER);
	if (asroot && injail)
		expect("priv_setgid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setgid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setgid(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setgid(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_setegid(int asroot, int injail, struct test *test)
{
	int error;

	error = setegid(GID_OTHER);
	if (asroot && injail)
		expect("priv_setegid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setegid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setegd(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setegid(!asroot, !injail)", error, -1, EPERM);
}

static const gid_t	gidset[] = {GID_WHEEL, GID_OTHER};
static const int	gidset_len = sizeof(gidset) / sizeof(gid_t);

void
priv_cred_setgroups(int asroot, int injail, struct test *test)
{
	int error;

	error = setgroups(gidset_len, gidset);
	if (asroot && injail)
		expect("priv_setgroups(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setgroups(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setgroups(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setgroups(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_setreuid(int asroot, int injail, struct test *test)
{
	int error;

	error = setreuid(UID_OTHER, UID_OTHER);
	if (asroot && injail)
		expect("priv_setreuid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setreuid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setreuid(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setreuid(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_setregid(int asroot, int injail, struct test *test)
{
	int error;

	error = setregid(GID_OTHER, GID_OTHER);
	if (asroot && injail)
		expect("priv_setregid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setregid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setregid(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setregid(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_setresuid(int asroot, int injail, struct test *test)
{
	int error;

	error = setresuid(UID_OTHER, UID_OTHER, UID_OTHER);
	if (asroot && injail)
		expect("priv_setresuid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setresuid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setresuid(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setresuid(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_setresgid(int asroot, int injail, struct test *test)
{
	int error;

	error = setresgid(GID_OTHER, GID_OTHER, GID_OTHER);
	if (asroot && injail)
		expect("priv_setresgid(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_setresgid(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_setresgid(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_setresgid(!asroot, !injail)", error, -1, EPERM);
}

void
priv_cred_cleanup(int asroot, int injail, struct test *test)
{

}
