/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
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
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <sys/dtrace.h>
#include <sys/dtrace_bsd.h>

#include <fs/nfs/nfsproto.h>

#include <fs/nfsclient/nfs_kdtrace.h>

/*
 * dtnfscl is a DTrace provider that tracks the intent to perform RPCs
 * in the NFS client, as well as access to and maintenance of the access and
 * attribute caches.  This is not quite the same as RPCs, because NFS may
 * issue multiple RPC transactions in the event that authentication fails,
 * there's a jukebox error, or none at all if the access or attribute cache
 * hits.  However, it cleanly represents the logical layer between RPC
 * transmission and vnode/vfs operations, providing access to state linking
 * the two.
 */

static int	dtnfsclient_unload(void);
static void	dtnfsclient_getargdesc(void *, dtrace_id_t, void *,
		    dtrace_argdesc_t *);
static void	dtnfsclient_provide(void *, dtrace_probedesc_t *);
static void	dtnfsclient_destroy(void *, dtrace_id_t, void *);
static void	dtnfsclient_enable(void *, dtrace_id_t, void *);
static void	dtnfsclient_disable(void *, dtrace_id_t, void *);
static void	dtnfsclient_load(void *);

static dtrace_pattr_t dtnfsclient_attr = {
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
};

/*
 * Description of NFSv4, NFSv3 and (optional) NFSv2 probes for a procedure.
 */
struct dtnfsclient_rpc {
	char		*nr_v4_name;
	char		*nr_v3_name;	/* Or NULL if none. */
	char		*nr_v2_name;	/* Or NULL if none. */

	/*
	 * IDs for the start and done cases, for NFSv2, NFSv3 and NFSv4.
	 */
	uint32_t	 nr_v2_id_start, nr_v2_id_done;
	uint32_t	 nr_v3_id_start, nr_v3_id_done;
	uint32_t	 nr_v4_id_start, nr_v4_id_done;
};

/*
 * This table is indexed by NFSv3 procedure number, but also used for NFSv2
 * procedure names and NFSv4 operations.
 */
static struct dtnfsclient_rpc	dtnfsclient_rpcs[NFSV41_NPROCS + 1] = {
	{ "null", "null", "null" },
	{ "getattr", "getattr", "getattr" },
	{ "setattr", "setattr", "setattr" },
	{ "lookup", "lookup", "lookup" },
	{ "access", "access", "noop" },
	{ "readlink", "readlink", "readlink" },
	{ "read", "read", "read" },
	{ "write", "write", "write" },
	{ "create", "create", "create" },
	{ "mkdir", "mkdir", "mkdir" },
	{ "symlink", "symlink", "symlink" },
	{ "mknod", "mknod" },
	{ "remove", "remove", "remove" },
	{ "rmdir", "rmdir", "rmdir" },
	{ "rename", "rename", "rename" },
	{ "link", "link", "link" },
	{ "readdir", "readdir", "readdir" },
	{ "readdirplus", "readdirplus" },
	{ "fsstat", "fsstat", "statfs" },
	{ "fsinfo", "fsinfo" },
	{ "pathconf", "pathconf" },
	{ "commit", "commit" },
	{ "lookupp" },
	{ "setclientid" },
	{ "setclientidcfrm" },
	{ "lock" },
	{ "locku" },
	{ "open" },
	{ "close" },
	{ "openconfirm" },
	{ "lockt" },
	{ "opendowngrade" },
	{ "renew" },
	{ "putrootfh" },
	{ "releaselckown" },
	{ "delegreturn" },
	{ "retdelegremove" },
	{ "retdelegrename1" },
	{ "retdelegrename2" },
	{ "getacl" },
	{ "setacl" },
	{ "noop", "noop", "noop" }
};

/*
 * Module name strings.
 */
static char	*dtnfsclient_accesscache_str = "accesscache";
static char	*dtnfsclient_attrcache_str = "attrcache";
static char	*dtnfsclient_nfs2_str = "nfs2";
static char	*dtnfsclient_nfs3_str = "nfs3";
static char	*dtnfsclient_nfs4_str = "nfs4";

/*
 * Function name strings.
 */
static char	*dtnfsclient_flush_str = "flush";
static char	*dtnfsclient_load_str = "load";
static char	*dtnfsclient_get_str = "get";

/*
 * Name strings.
 */
static char	*dtnfsclient_done_str = "done";
static char	*dtnfsclient_hit_str = "hit";
static char	*dtnfsclient_miss_str = "miss";
static char	*dtnfsclient_start_str = "start";

static dtrace_pops_t dtnfsclient_pops = {
	.dtps_provide =		dtnfsclient_provide,
	.dtps_provide_module =	NULL,
	.dtps_enable =		dtnfsclient_enable,
	.dtps_disable =		dtnfsclient_disable,
	.dtps_suspend =		NULL,
	.dtps_resume =		NULL,
	.dtps_getargdesc =	dtnfsclient_getargdesc,
	.dtps_getargval =	NULL,
	.dtps_usermode =	NULL,
	.dtps_destroy =		dtnfsclient_destroy
};

static dtrace_provider_id_t	dtnfsclient_id;

/*
 * When tracing on a procedure is enabled, the DTrace ID for an RPC event is
 * stored in one of these two NFS client-allocated arrays; 0 indicates that
 * the event is not being traced so probes should not be called.
 *
 * For simplicity, we allocate both v2, v3 and v4 arrays as NFSV41_NPROCS + 1,
 * and the v2, v3 arrays are simply sparse.
 */
extern uint32_t			nfscl_nfs2_start_probes[NFSV41_NPROCS + 1];
extern uint32_t			nfscl_nfs2_done_probes[NFSV41_NPROCS + 1];

extern uint32_t			nfscl_nfs3_start_probes[NFSV41_NPROCS + 1];
extern uint32_t			nfscl_nfs3_done_probes[NFSV41_NPROCS + 1];

extern uint32_t			nfscl_nfs4_start_probes[NFSV41_NPROCS + 1];
extern uint32_t			nfscl_nfs4_done_probes[NFSV41_NPROCS + 1];

/*
 * Look up a DTrace probe ID to see if it's associated with a "done" event --
 * if so, we will return a fourth argument type of "int".
 */
static int
dtnfs234_isdoneprobe(dtrace_id_t id)
{
	int i;

	for (i = 0; i < NFSV41_NPROCS + 1; i++) {
		if (dtnfsclient_rpcs[i].nr_v4_id_done == id ||
		    dtnfsclient_rpcs[i].nr_v3_id_done == id ||
		    dtnfsclient_rpcs[i].nr_v2_id_done == id)
			return (1);
	}
	return (0);
}

static void
dtnfsclient_getargdesc(void *arg, dtrace_id_t id, void *parg,
    dtrace_argdesc_t *desc)
{
	const char *p = NULL;

	if (id == nfscl_accesscache_flush_done_id ||
	    id == nfscl_attrcache_flush_done_id ||
	    id == nfscl_attrcache_get_miss_id) {
		switch (desc->dtargd_ndx) {
		case 0:
			p = "struct vnode *";
			break;
		default:
			desc->dtargd_ndx = DTRACE_ARGNONE;
			break;
		}
	} else if (id == nfscl_accesscache_get_hit_id ||
	    id == nfscl_accesscache_get_miss_id) {
		switch (desc->dtargd_ndx) {
		case 0:
			p = "struct vnode *";
			break;
		case 1:
			p = "uid_t";
			break;
		case 2:
			p = "uint32_t";
			break;
		default:
			desc->dtargd_ndx = DTRACE_ARGNONE;
			break;
		}
	} else if (id == nfscl_accesscache_load_done_id) {
		switch (desc->dtargd_ndx) {
		case 0:
			p = "struct vnode *";
			break;
		case 1:
			p = "uid_t";
			break;
		case 2:
			p = "uint32_t";
			break;
		case 3:
			p = "int";
			break;
		default:
			desc->dtargd_ndx = DTRACE_ARGNONE;
			break;
		}
	} else if (id == nfscl_attrcache_get_hit_id) {
		switch (desc->dtargd_ndx) {
		case 0:
			p = "struct vnode *";
			break;
		case 1:
			p = "struct vattr *";
			break;
		default:
			desc->dtargd_ndx = DTRACE_ARGNONE;
			break;
		}
	} else if (id == nfscl_attrcache_load_done_id) {
		switch (desc->dtargd_ndx) {
		case 0:
			p = "struct vnode *";
			break;
		case 1:
			p = "struct vattr *";
			break;
		case 2:
			p = "int";
			break;
		default:
			desc->dtargd_ndx = DTRACE_ARGNONE;
			break;
		}
	} else {
		switch (desc->dtargd_ndx) {
		case 0:
			p = "struct vnode *";
			break;
		case 1:
			p = "struct mbuf *";
			break;
		case 2:
			p = "struct ucred *";
			break;
		case 3:
			p = "int";
			break;
		case 4:
			if (dtnfs234_isdoneprobe(id)) {
				p = "int";
				break;
			}
			/* FALLSTHROUGH */
		default:
			desc->dtargd_ndx = DTRACE_ARGNONE;
			break;
		}
	}
	if (p != NULL)
		strlcpy(desc->dtargd_native, p, sizeof(desc->dtargd_native));
}

static void
dtnfsclient_provide(void *arg, dtrace_probedesc_t *desc)
{
	int i;

	if (desc != NULL)
		return;

	/*
	 * Register access cache probes.
	 */
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_accesscache_str,
	    dtnfsclient_flush_str, dtnfsclient_done_str) == 0) {
		nfscl_accesscache_flush_done_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_accesscache_str,
		    dtnfsclient_flush_str, dtnfsclient_done_str, 0, NULL);
	}
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_accesscache_str,
	    dtnfsclient_get_str, dtnfsclient_hit_str) == 0) {
		nfscl_accesscache_get_hit_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_accesscache_str,
		    dtnfsclient_get_str, dtnfsclient_hit_str, 0, NULL);
	}
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_accesscache_str,
	    dtnfsclient_get_str, dtnfsclient_miss_str) == 0) {
		nfscl_accesscache_get_miss_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_accesscache_str,
		    dtnfsclient_get_str, dtnfsclient_miss_str, 0, NULL);
	}
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_accesscache_str,
	    dtnfsclient_load_str, dtnfsclient_done_str) == 0) {
		nfscl_accesscache_load_done_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_accesscache_str,
		    dtnfsclient_load_str, dtnfsclient_done_str, 0, NULL);
	}

	/*
	 * Register attribute cache probes.
	 */
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_attrcache_str,
	    dtnfsclient_flush_str, dtnfsclient_done_str) == 0) {
		nfscl_attrcache_flush_done_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_attrcache_str,
		    dtnfsclient_flush_str, dtnfsclient_done_str, 0, NULL);
	}
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_attrcache_str,
	    dtnfsclient_get_str, dtnfsclient_hit_str) == 0) {
		nfscl_attrcache_get_hit_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_attrcache_str,
		    dtnfsclient_get_str, dtnfsclient_hit_str, 0, NULL);
	}
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_attrcache_str,
	    dtnfsclient_get_str, dtnfsclient_miss_str) == 0) {
		nfscl_attrcache_get_miss_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_attrcache_str,
		    dtnfsclient_get_str, dtnfsclient_miss_str, 0, NULL);
	}
	if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_attrcache_str,
	    dtnfsclient_load_str, dtnfsclient_done_str) == 0) {
		nfscl_attrcache_load_done_id = dtrace_probe_create(
		    dtnfsclient_id, dtnfsclient_attrcache_str,
		    dtnfsclient_load_str, dtnfsclient_done_str, 0, NULL);
	}

	/*
	 * Register NFSv2 RPC procedures; note sparseness check for each slot
	 * in the NFSv3, NFSv4 procnum-indexed array.
	 */
	for (i = 0; i < NFSV41_NPROCS + 1; i++) {
		if (dtnfsclient_rpcs[i].nr_v2_name != NULL &&
		    dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_nfs2_str,
		    dtnfsclient_rpcs[i].nr_v2_name, dtnfsclient_start_str) ==
		    0) {
			dtnfsclient_rpcs[i].nr_v2_id_start =
			    dtrace_probe_create(dtnfsclient_id,
			    dtnfsclient_nfs2_str,
			    dtnfsclient_rpcs[i].nr_v2_name,
			    dtnfsclient_start_str, 0,
			    &nfscl_nfs2_start_probes[i]);
		}
		if (dtnfsclient_rpcs[i].nr_v2_name != NULL &&
		    dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_nfs2_str,
		    dtnfsclient_rpcs[i].nr_v2_name, dtnfsclient_done_str) ==
		    0) {
			dtnfsclient_rpcs[i].nr_v2_id_done = 
			    dtrace_probe_create(dtnfsclient_id,
			    dtnfsclient_nfs2_str,
			    dtnfsclient_rpcs[i].nr_v2_name,
			    dtnfsclient_done_str, 0,
			    &nfscl_nfs2_done_probes[i]);
		}
	}

	/*
	 * Register NFSv3 RPC procedures; note sparseness check for each slot
	 * in the NFSv4 procnum-indexed array.
	 */
	for (i = 0; i < NFSV41_NPROCS + 1; i++) {
		if (dtnfsclient_rpcs[i].nr_v3_name != NULL &&
		    dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_nfs3_str,
		    dtnfsclient_rpcs[i].nr_v3_name, dtnfsclient_start_str) ==
		    0) {
			dtnfsclient_rpcs[i].nr_v3_id_start =
			    dtrace_probe_create(dtnfsclient_id,
			    dtnfsclient_nfs3_str,
			    dtnfsclient_rpcs[i].nr_v3_name,
			    dtnfsclient_start_str, 0,
			    &nfscl_nfs3_start_probes[i]);
		}
		if (dtnfsclient_rpcs[i].nr_v3_name != NULL &&
		    dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_nfs3_str,
		    dtnfsclient_rpcs[i].nr_v3_name, dtnfsclient_done_str) ==
		    0) {
			dtnfsclient_rpcs[i].nr_v3_id_done = 
			    dtrace_probe_create(dtnfsclient_id,
			    dtnfsclient_nfs3_str,
			    dtnfsclient_rpcs[i].nr_v3_name,
			    dtnfsclient_done_str, 0,
			    &nfscl_nfs3_done_probes[i]);
		}
	}

	/*
	 * Register NFSv4 RPC procedures.
	 */
	for (i = 0; i < NFSV41_NPROCS + 1; i++) {
		if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_nfs4_str,
		    dtnfsclient_rpcs[i].nr_v4_name, dtnfsclient_start_str) ==
		    0) {
			dtnfsclient_rpcs[i].nr_v4_id_start =
			    dtrace_probe_create(dtnfsclient_id,
			    dtnfsclient_nfs4_str,
			    dtnfsclient_rpcs[i].nr_v4_name,
			    dtnfsclient_start_str, 0,
			    &nfscl_nfs4_start_probes[i]);
		}
		if (dtrace_probe_lookup(dtnfsclient_id, dtnfsclient_nfs4_str,
		    dtnfsclient_rpcs[i].nr_v4_name, dtnfsclient_done_str) ==
		    0) {
			dtnfsclient_rpcs[i].nr_v4_id_done = 
			    dtrace_probe_create(dtnfsclient_id,
			    dtnfsclient_nfs4_str,
			    dtnfsclient_rpcs[i].nr_v4_name,
			    dtnfsclient_done_str, 0,
			    &nfscl_nfs4_done_probes[i]);
		}
	}
}

static void
dtnfsclient_destroy(void *arg, dtrace_id_t id, void *parg)
{
}

static void
dtnfsclient_enable(void *arg, dtrace_id_t id, void *parg)
{
	uint32_t *p = parg;
	void *f = dtrace_probe;

	if (id == nfscl_accesscache_flush_done_id)
		dtrace_nfscl_accesscache_flush_done_probe = f;
	else if (id == nfscl_accesscache_get_hit_id)
		dtrace_nfscl_accesscache_get_hit_probe = f;
	else if (id == nfscl_accesscache_get_miss_id)
		dtrace_nfscl_accesscache_get_miss_probe = f;
	else if (id == nfscl_accesscache_load_done_id)
		dtrace_nfscl_accesscache_load_done_probe = f;
	else if (id == nfscl_attrcache_flush_done_id)
		dtrace_nfscl_attrcache_flush_done_probe = f;
	else if (id == nfscl_attrcache_get_hit_id)
		dtrace_nfscl_attrcache_get_hit_probe = f;
	else if (id == nfscl_attrcache_get_miss_id)
		dtrace_nfscl_attrcache_get_miss_probe = f;
	else if (id == nfscl_attrcache_load_done_id)
		dtrace_nfscl_attrcache_load_done_probe = f;
	else
		*p = id;
}

static void
dtnfsclient_disable(void *arg, dtrace_id_t id, void *parg)
{
	uint32_t *p = parg;

	if (id == nfscl_accesscache_flush_done_id)
		dtrace_nfscl_accesscache_flush_done_probe = NULL;
	else if (id == nfscl_accesscache_get_hit_id)
		dtrace_nfscl_accesscache_get_hit_probe = NULL;
	else if (id == nfscl_accesscache_get_miss_id)
		dtrace_nfscl_accesscache_get_miss_probe = NULL;
	else if (id == nfscl_accesscache_load_done_id)
		dtrace_nfscl_accesscache_load_done_probe = NULL;
	else if (id == nfscl_attrcache_flush_done_id)
		dtrace_nfscl_attrcache_flush_done_probe = NULL;
	else if (id == nfscl_attrcache_get_hit_id)
		dtrace_nfscl_attrcache_get_hit_probe = NULL;
	else if (id == nfscl_attrcache_get_miss_id)
		dtrace_nfscl_attrcache_get_miss_probe = NULL;
	else if (id == nfscl_attrcache_load_done_id)
		dtrace_nfscl_attrcache_load_done_probe = NULL;
	else
		*p = 0;
}

static void
dtnfsclient_load(void *dummy)
{

	if (dtrace_register("nfscl", &dtnfsclient_attr,
	    DTRACE_PRIV_USER, NULL, &dtnfsclient_pops, NULL,
	    &dtnfsclient_id) != 0)
		return;

	dtrace_nfscl_nfs234_start_probe =
	    (dtrace_nfsclient_nfs23_start_probe_func_t)dtrace_probe;
	dtrace_nfscl_nfs234_done_probe =
	    (dtrace_nfsclient_nfs23_done_probe_func_t)dtrace_probe;
}


static int
dtnfsclient_unload()
{

	dtrace_nfscl_nfs234_start_probe = NULL;
	dtrace_nfscl_nfs234_done_probe = NULL;

	return (dtrace_unregister(dtnfsclient_id));
}

static int
dtnfsclient_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		break;

	case MOD_UNLOAD:
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

SYSINIT(dtnfsclient_load, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY,
    dtnfsclient_load, NULL);
SYSUNINIT(dtnfsclient_unload, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY,
    dtnfsclient_unload, NULL);

DEV_MODULE(dtnfscl, dtnfsclient_modevent, NULL);
MODULE_VERSION(dtnfscl, 1);
MODULE_DEPEND(dtnfscl, dtrace, 1, 1, 1);
MODULE_DEPEND(dtnfscl, opensolaris, 1, 1, 1);
MODULE_DEPEND(dtnfscl, nfscl, 1, 1, 1);
MODULE_DEPEND(dtnfscl, nfscommon, 1, 1, 1);
