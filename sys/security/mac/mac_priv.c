/*-
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project under contract to nCircle Network Security, Inc.
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
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
 */

/*
 * MAC checks for system privileges.
 */

#include "sys/cdefs.h"
__FBSDID("$FreeBSD$");

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/sdt.h>
#include <sys/module.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

/*
 * The MAC Framework interacts with kernel privilege checks in two ways: it
 * may restrict the granting of privilege to a subject, and it may grant
 * additional privileges to the subject.  Policies may implement none, one,
 * or both of these entry points.  Restriction of privilege by any policy
 * always overrides granting of privilege by any policy or other privilege
 * mechanism.  See kern_priv.c:priv_check_cred() for details of the
 * composition.
 */

MAC_CHECK_PROBE_DEFINE2(priv_check, "struct ucred *", "int");

/*
 * Restrict access to a privilege for a credential.  Return failure if any
 * policy denies access.
 */
int
mac_priv_check(struct ucred *cred, int priv)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(priv_check, cred, priv);
	MAC_CHECK_PROBE2(priv_check, error, cred, priv);

	return (error);
}

MAC_GRANT_PROBE_DEFINE2(priv_grant, "struct ucred *", "int");

/*
 * Grant access to a privilege for a credential.  Return success if any
 * policy grants access.
 */
int
mac_priv_grant(struct ucred *cred, int priv)
{
	int error;

	MAC_POLICY_GRANT_NOSLEEP(priv_grant, cred, priv);
	MAC_GRANT_PROBE2(priv_grant, error, cred, priv);

	return (error);
}
