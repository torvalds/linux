/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Doug Rabson
 * All rights reserved.
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
#include <sys/systm.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>

#include "rpcsec_gss_int.h"

bool_t
rpc_gss_mech_to_oid(const char *mech, gss_OID *oid_ret)
{
	gss_OID oid = kgss_find_mech_by_name(mech);

	if (oid) {
		*oid_ret = oid;
		return (TRUE);
	}
	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (FALSE);
}

bool_t
rpc_gss_oid_to_mech(gss_OID oid, const char **mech_ret)
{
	const char *name = kgss_find_mech_by_oid(oid);

	if (name) {
		*mech_ret = name;
		return (TRUE);
	}
	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (FALSE);
}

bool_t
rpc_gss_qop_to_num(const char *qop, const char *mech, u_int *num_ret)
{

	if (!strcmp(qop, "default")) {
		*num_ret = GSS_C_QOP_DEFAULT;
		return (TRUE);
	}
	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (FALSE);
}

const char *
_rpc_gss_num_to_qop(const char *mech, u_int num)
{

	if (num == GSS_C_QOP_DEFAULT)
		return "default";

	return (NULL);
}

const char **
rpc_gss_get_mechanisms(void)
{
	static const char **mech_names = NULL;
	struct kgss_mech *km;
	int count;

	if (mech_names)
		return (mech_names);

	count = 0;
	LIST_FOREACH(km, &kgss_mechs, km_link) {
		count++;
	}
	count++;

	mech_names = malloc(count * sizeof(const char *), M_RPC, M_WAITOK);
	count = 0;
	LIST_FOREACH(km, &kgss_mechs, km_link) {
		mech_names[count++] = km->km_mech_name;
	}
	mech_names[count++] = NULL;
	
	return (mech_names);
}

#if 0
const char **
rpc_gss_get_mech_info(const char *mech, rpc_gss_service_t *service)
{
	struct mech_info *info;

	_rpc_gss_load_mech();
	_rpc_gss_load_qop();
	SLIST_FOREACH(info, &mechs, link) {
		if (!strcmp(mech, info->name)) {
			/*
			 * I'm not sure what to do with service
			 * here. The Solaris manpages are not clear on
			 * the subject and the OpenSolaris code just
			 * sets it to rpc_gss_svc_privacy
			 * unconditionally with a comment noting that
			 * it is bogus.
			 */
			*service = rpc_gss_svc_privacy;
			return info->qops;
		}
	}

	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (NULL);
}
#endif

bool_t
rpc_gss_get_versions(u_int *vers_hi, u_int *vers_lo)
{

	*vers_hi = 1;
	*vers_lo = 1;
	return (TRUE);
}

bool_t
rpc_gss_is_installed(const char *mech)
{
	gss_OID oid = kgss_find_mech_by_name(mech);

	if (oid)
		return (TRUE);
	else
		return (FALSE);
}

