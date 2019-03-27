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
 * Test that configuring accounting requires privilege.  We test four cases
 * across {!jail, jail}:
 *
 * priv_acct_enable - enable accounting from a disabled state
 * priv_acct_disable - disable accounting from an enabled state
 * priv_acct_rotate - rotate the accounting file
 * priv_acct_noopdisable - disable accounting when already disabled
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"

#define	SYSCTL_NAME	"kern.acct_configured"

/*
 * Actual filenames used across all of the tests.
 */
static int	fpath1_initialized;
static char	fpath1[1024];
static int	fpath2_initialized;
static char	fpath2[1024];

int
priv_acct_setup(int asroot, int injail, struct test *test)
{
	size_t len;
	int i;

	len = sizeof(i);
	if (sysctlbyname(SYSCTL_NAME, &i, &len, NULL, 0) < 0) {
		warn("priv_acct_setup: sysctlbyname(%s)", SYSCTL_NAME);
		return (-1);
	}
	if (i != 0) {
		warnx("sysctlbyname(%s) indicates accounting configured",
		    SYSCTL_NAME);
		return (-1);
	}
	setup_file("priv_acct_setup: fpath1", fpath1, 0, 0, 0666);
	fpath1_initialized = 1;
	setup_file("priv_acct_setup: fpath2", fpath2, 0, 0, 0666);
	fpath2_initialized = 1;

	if (test->t_test_func == priv_acct_enable ||
	    test->t_test_func == priv_acct_noopdisable) {
		if (acct(NULL) != 0) {
			warn("priv_acct_setup: acct(NULL)");
			return (-1);
		}
	} else if (test->t_test_func == priv_acct_disable ||
	     test->t_test_func == priv_acct_rotate) {
		if (acct(fpath1) != 0) {
			warn("priv_acct_setup: acct(\"%s\")", fpath1);
			return (-1);
		}
	}
	return (0);
}

void
priv_acct_cleanup(int asroot, int injail, struct test *test)
{

	(void)acct(NULL);
	if (fpath1_initialized) {
		(void)unlink(fpath1);
		fpath1_initialized = 0;
	}
	if (fpath2_initialized) {
		(void)unlink(fpath2);
		fpath2_initialized = 0;
	}
}

void
priv_acct_enable(int asroot, int injail, struct test *test)
{
	int error;

	error = acct(fpath1);
	if (asroot && injail)
		expect("priv_acct_enable(root, jail)", error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_acct_enable(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_acct_enable(!root, jail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_acct_enable(!root, !jail)", error, -1, EPERM);
}

void
priv_acct_disable(int asroot, int injail, struct test *test)
{
	int error;

	error = acct(NULL);
	if (asroot && injail)
		expect("priv_acct_disable(root, jail)", error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_acct_disable(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_acct_disable(!root, jail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_acct_disable(!root, !jail)", error, -1, EPERM);
}

void
priv_acct_rotate(int asroot, int injail, struct test *test)
{
	int error;

	error = acct(fpath2);
	if (asroot && injail)
		expect("priv_acct_rotate(root, jail)", error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_acct_rotate(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_acct_rotate(!root, jail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_acct_rotate(!root, !jail)", error, -1, EPERM);
}

void
priv_acct_noopdisable(int asroot, int injail, struct test *test)
{
	int error;

	error = acct(NULL);
	if (asroot && injail)
		expect("priv_acct_noopdisable(root, jail)", error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_acct_noopdisable(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_acct_noopdisable(!root, jail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_acct_noopdisable(!root, !jail)", error, -1, EPERM);
}
