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
 * Confirm that when security.bsd.unprivileged_read_msgbuf is set to 0,
 * privilege is required to read the kernel message buffer.
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>

#include "main.h"

#define	MSGBUF_CONTROL_NAME	"security.bsd.unprivileged_read_msgbuf"
#define	MSGBUF_NAME		"kern.msgbuf"

/*
 * We must query and save the original value, then restore it when done.
 */
static int unprivileged_read_msgbuf;
static int unprivileged_read_msgbuf_initialized;

int
priv_msgbuf_privonly_setup(int asroot, int injail, struct test *test)
{
	size_t len;
	int newval;

	/*
	 * Separately query and set to make debugging easier.
	 */
	len = sizeof(unprivileged_read_msgbuf);
	if (sysctlbyname(MSGBUF_CONTROL_NAME, &unprivileged_read_msgbuf,
	    &len, NULL, 0) < 0) {
		warn("priv_msgbuf_privonly_setup: sysctlbyname query");
		return (-1);
	}
	newval = 0;
	if (sysctlbyname(MSGBUF_CONTROL_NAME, NULL, NULL, &newval,
	    sizeof(newval)) < 0) {
		warn("priv_msgbuf_privonly_setup: sysctlbyname set");
		return (-1);
	}
	unprivileged_read_msgbuf_initialized = 1;
	return (0);
}

void
priv_msgbuf_privonly(int asroot, int injail, struct test *test)
{
	size_t len;
	int error;

	error = sysctlbyname(MSGBUF_NAME, NULL, &len, NULL, 0);
	if (asroot && injail)
		expect("priv_msgbuf_privonly(asroot, injail)", error, -1,
		    EPERM);
	if (asroot && !injail)
		expect("priv_msgbuf_privonly(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_msgbuf_privonly(!asroot, injail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_msgbuf_privonly(!asroot, !injail)", error, -1,
		    EPERM);
}

int
priv_msgbuf_unprivok_setup(int asroot, int injail, struct test *test)
{
	size_t len;
	int newval;

	/*
	 * Separately query and set to make debugging easier.
	 */
	len = sizeof(unprivileged_read_msgbuf);
	if (sysctlbyname(MSGBUF_CONTROL_NAME, &unprivileged_read_msgbuf, &len,
	    NULL, 0) < 0) {
		warn("priv_msgbuf_unprivok_setup: sysctlbyname query");
		return (-1);
	}
	newval = 1;
	if (sysctlbyname(MSGBUF_CONTROL_NAME, NULL, NULL, &newval,
	    sizeof(newval)) < 0) {
		warn("priv_msgbuf_unprivok_setup: sysctlbyname set");
		return (-1);
	}
	unprivileged_read_msgbuf_initialized = 1;
	return (0);
}

void
priv_msgbuf_unprivok(int asroot, int injail, struct test *test)
{
	size_t len;
	int error;

	error = sysctlbyname(MSGBUF_NAME, NULL, &len, NULL, 0);
	if (asroot && injail)
		expect("priv_msgbuf_unprivok(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_msgbuf_unprivok(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_msgbuf_unprivok(!asroot, injail)", error, 0, 0);
	if (!asroot && !injail)
		expect("priv_msgbuf_unprivok(!asroot, !injail)", error, 0, 0);
}

void
priv_msgbuf_cleanup(int asroot, int injail, struct test *test)
{

	if (unprivileged_read_msgbuf_initialized) {
		(void)sysctlbyname(MSGBUF_NAME, NULL, NULL,
		    &unprivileged_read_msgbuf,
		    sizeof(unprivileged_read_msgbuf));
		unprivileged_read_msgbuf_initialized = 0;
	}
}
