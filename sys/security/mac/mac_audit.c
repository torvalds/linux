/*-
 * Copyright (c) 1999-2002, 2009 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001-2004 Networks Associates Technology, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 *
 * This software was developed by Robert Watson and Ilmar Habibulin for the
 * TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/sdt.h>
#include <sys/vnode.h>

#include <security/audit/audit.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

MAC_CHECK_PROBE_DEFINE2(cred_check_setaudit, "struct ucred *",
    "struct auditinfo *");

int
mac_cred_check_setaudit(struct ucred *cred, struct auditinfo *ai)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(cred_check_setaudit, cred, ai);
	MAC_CHECK_PROBE2(cred_check_setaudit, error, cred, ai);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(cred_check_setaudit_addr, "struct ucred *",
    "struct auditinfo_addr *");

int
mac_cred_check_setaudit_addr(struct ucred *cred, struct auditinfo_addr *aia)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(cred_check_setaudit_addr, cred, aia);
	MAC_CHECK_PROBE2(cred_check_setaudit_addr, error, cred, aia);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(cred_check_setauid, "struct ucred *", "uid_t");

int
mac_cred_check_setauid(struct ucred *cred, uid_t auid)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(cred_check_setauid, cred, auid);
	MAC_CHECK_PROBE2(cred_check_setauid, error, cred, auid);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(system_check_audit, "struct ucred *", "void *",
    "int");

int
mac_system_check_audit(struct ucred *cred, void *record, int length)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(system_check_audit, cred, record, length);
	MAC_CHECK_PROBE3(system_check_audit, error, cred, record, length);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(system_check_auditctl, "struct ucred *",
    "struct vnode *");

int
mac_system_check_auditctl(struct ucred *cred, struct vnode *vp)
{
	int error;
	struct label *vl;

	ASSERT_VOP_LOCKED(vp, "mac_system_check_auditctl");

	vl = (vp != NULL) ? vp->v_label : NULL;
	MAC_POLICY_CHECK(system_check_auditctl, cred, vp, vl);
	MAC_CHECK_PROBE2(system_check_auditctl, error, cred, vp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(system_check_auditon, "struct ucred *", "int");

int
mac_system_check_auditon(struct ucred *cred, int cmd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(system_check_auditon, cred, cmd);
	MAC_CHECK_PROBE2(system_check_auditon, error, cred, cmd);

	return (error);
}
