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
 * Confirm privilege is required to set process audit properties; we first
 * query current properties so that the attempted operation is a no-op.
 */

#include <sys/types.h>

#include <bsm/audit.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>

#include "main.h"

static auditinfo_t ai;
static auditinfo_addr_t aia;

int
priv_audit_setaudit_setup(int asroot, int injail, struct test *test)
{

	if (getaudit(&ai) < 0) {
		warn("priv_audit_setaudit_setup: getaudit");
		return (-1);
	}
	if (getaudit_addr(&aia, sizeof(aia)) < 0) {
		warn("priv_audit_setaudit_setup: getaudit_addr");
		return (-1);
	}

	return (0);
}

void
priv_audit_setaudit(int asroot, int injail, struct test *test)
{
	int error;

	error = setaudit(&ai);
	if (asroot && injail)
		expect("priv_audit_setaudit(asroot, injail)", error, -1,
		    ENOSYS);
	if (asroot && !injail)
		expect("priv_audit_setaudit(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_audit_setaudit(!asroot, injail)", error, -1,
		    ENOSYS);
	if (!asroot && !injail)
		expect("priv_audit_setaudit(!asroot, !injail)", error, -1,
		    EPERM);
}

void
priv_audit_setaudit_addr(int asroot, int injail, struct test *test)
{
	int error;

	error = setaudit_addr(&aia, sizeof(aia));
	if (asroot && injail)
		expect("priv_audit_setaudit_addr(asroot, injail)", error, -1,
		    ENOSYS);
	if (asroot && !injail)
		expect("priv_audit_setaudit_addr(asroot, !injail)", error, 0,
		    0);
	if (!asroot && injail)
		expect("priv_audit_setaudit_addr(!asroot, injail)", error,
		    -1, ENOSYS);
	if (!asroot && !injail)
		expect("priv_audit_setaudit_addr(!asroot, !injail)", error,
		    -1, EPERM);
}

void
priv_audit_setaudit_cleanup(int asroot, int injail, struct test *test)
{

}
