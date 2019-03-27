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
 * Two privileges exist for writing sysctls -- one for sysctls writable only
 * outside of jail (PRIV_SYSCTL_WRITE) and one for those also writable inside
 * jail (PRIV_SYSCTL_WRITEJAIL).
 *
 * Test the prior by attempting to write to kern.domainname, and the latter
 * by attempting to write to kern.hostname.
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

#define KERN_HOSTNAME_STRING	"kern.hostname"
#define	KERN_DOMAINNAME_STRING	"kern.domainname"

static char stored_hostname[1024];
static char stored_domainname[1024];

int
priv_sysctl_write_setup(int asroot, int injail, struct test *test)
{
	size_t len;
	int error;

	len = sizeof(stored_hostname);
	error = sysctlbyname(KERN_HOSTNAME_STRING, stored_hostname, &len,
	    NULL, 0);
	if (error) {
		warn("priv_sysctl_write_setup: sysctlbyname(\"%s\")",
		    KERN_HOSTNAME_STRING);
		return (-1);
	}

	len = sizeof(stored_hostname);
	error = sysctlbyname(KERN_DOMAINNAME_STRING, stored_domainname, &len,
	    NULL, 0);
	if (error) {
		warn("priv_sysctl_write_setup: sysctlbyname(\"%s\")",
		    KERN_DOMAINNAME_STRING);
		return (-1);
	}

	return (0);
}

void
priv_sysctl_write(int asroot, int injail, struct test *test)
{
	int error;

	error = sysctlbyname(KERN_DOMAINNAME_STRING, NULL, NULL,
	    stored_domainname, strlen(stored_domainname));
	if (asroot && injail)
		expect("priv_sysctl_write(asroot, injail)", error, -1,
		    EPERM);
	if (asroot && !injail)
		expect("priv_sysctl_write(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_sysctl_write(!asroot, injail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_sysctl_write(!asroot, !injail)", error, -1,
		    EPERM);
}

void
priv_sysctl_writejail(int asroot, int injail, struct test *test)
{
	int error;

	error = sysctlbyname(KERN_HOSTNAME_STRING, NULL, NULL,
	    stored_hostname, strlen(stored_hostname));
	if (asroot && injail)
		expect("priv_sysctl_writejail(asroot, injail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_sysctl_writejail(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_sysctl_writejail(!asroot, injail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_sysctl_writejail(!asroot, !injail)", error, -1,
		    EPERM);
}

void
priv_sysctl_write_cleanup(int asroot, int injail, struct test *test)
{

}
