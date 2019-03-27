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

/*
 * Test setting a kernel environment variable, then try to unset it.
 */

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <kenv.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

int
priv_kenv_unset_setup(int asroot, int injail, struct test *test)
{

	if (kenv(KENV_SET, KENV_VAR_NAME, KENV_VAR_VALUE, KENV_VAR_LEN) < 0) {
		warn("priv_kenv_unset: kenv");
		return (-1);
	}
	return (0);
}

void
priv_kenv_unset(int asroot, int injail, struct test *test)
{
	int error;

	error = kenv(KENV_UNSET, KENV_VAR_NAME, NULL, 0);
	if (asroot && injail)
		expect("priv_kenv_unset(asroot, injail)", error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_kenv_unset(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_kenv_unset(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_kenv_unset(!asroot, !injail)", error, -1, EPERM);
}

void
priv_kenv_unset_cleanup(int asroot, int injail, struct test *test)
{

	(void)kenv(KENV_UNSET, KENV_VAR_NAME, NULL, 0);
}
