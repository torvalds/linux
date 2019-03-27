/*-
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 1990 The Regents of the University of California.
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
/*
  svc_rpcsec_gss.c
  
  Copyright (c) 2000 The Regents of the University of Michigan.
  All rights reserved.

  Copyright (c) 2000 Dug Song <dugsong@UMICH.EDU>.
  All rights reserved, all wrongs reversed.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the University nor the names of its
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  $Id: svc_auth_gss.c,v 1.27 2002/01/15 15:43:00 andros Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/ucred.h>

#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>

#include "rpcsec_gss_int.h"

static bool_t   svc_rpc_gss_wrap(SVCAUTH *, struct mbuf **);
static bool_t   svc_rpc_gss_unwrap(SVCAUTH *, struct mbuf **);
static void     svc_rpc_gss_release(SVCAUTH *);
static enum auth_stat svc_rpc_gss(struct svc_req *, struct rpc_msg *);
static int rpc_gss_svc_getcred(struct svc_req *, struct ucred **, int *);

static struct svc_auth_ops svc_auth_gss_ops = {
	svc_rpc_gss_wrap,
	svc_rpc_gss_unwrap,
	svc_rpc_gss_release,
};

struct sx svc_rpc_gss_lock;

struct svc_rpc_gss_callback {
	SLIST_ENTRY(svc_rpc_gss_callback) cb_link;
	rpc_gss_callback_t	cb_callback;
};
static SLIST_HEAD(svc_rpc_gss_callback_list, svc_rpc_gss_callback)
	svc_rpc_gss_callbacks = SLIST_HEAD_INITIALIZER(svc_rpc_gss_callbacks);

struct svc_rpc_gss_svc_name {
	SLIST_ENTRY(svc_rpc_gss_svc_name) sn_link;
	char			*sn_principal;
	gss_OID			sn_mech;
	u_int			sn_req_time;
	gss_cred_id_t		sn_cred;
	u_int			sn_program;
	u_int			sn_version;
};
static SLIST_HEAD(svc_rpc_gss_svc_name_list, svc_rpc_gss_svc_name)
	svc_rpc_gss_svc_names = SLIST_HEAD_INITIALIZER(svc_rpc_gss_svc_names);

enum svc_rpc_gss_client_state {
	CLIENT_NEW,				/* still authenticating */
	CLIENT_ESTABLISHED,			/* context established */
	CLIENT_STALE				/* garbage to collect */
};

#define SVC_RPC_GSS_SEQWINDOW	128

struct svc_rpc_gss_clientid {
	unsigned long		ci_hostid;
	uint32_t		ci_boottime;
	uint32_t		ci_id;
};

struct svc_rpc_gss_client {
	TAILQ_ENTRY(svc_rpc_gss_client) cl_link;
	TAILQ_ENTRY(svc_rpc_gss_client) cl_alllink;
	volatile u_int		cl_refs;
	struct sx		cl_lock;
	struct svc_rpc_gss_clientid cl_id;
	time_t			cl_expiration;	/* when to gc */
	enum svc_rpc_gss_client_state cl_state;	/* client state */
	bool_t			cl_locked;	/* fixed service+qop */
	gss_ctx_id_t		cl_ctx;		/* context id */
	gss_cred_id_t		cl_creds;	/* delegated creds */
	gss_name_t		cl_cname;	/* client name */
	struct svc_rpc_gss_svc_name *cl_sname;	/* server name used */
	rpc_gss_rawcred_t	cl_rawcred;	/* raw credentials */
	rpc_gss_ucred_t		cl_ucred;	/* unix-style credentials */
	struct ucred		*cl_cred;	/* kernel-style credentials */
	int			cl_rpcflavor;	/* RPC pseudo sec flavor */
	bool_t			cl_done_callback; /* TRUE after call */
	void			*cl_cookie;	/* user cookie from callback */
	gid_t			cl_gid_storage[NGROUPS];
	gss_OID			cl_mech;	/* mechanism */
	gss_qop_t		cl_qop;		/* quality of protection */
	uint32_t		cl_seqlast;	/* sequence window origin */
	uint32_t		cl_seqmask[SVC_RPC_GSS_SEQWINDOW/32]; /* bitmask of seqnums */
};
TAILQ_HEAD(svc_rpc_gss_client_list, svc_rpc_gss_client);

/*
 * This structure holds enough information to unwrap arguments or wrap
 * results for a given request. We use the rq_clntcred area for this
 * (which is a per-request buffer).
 */
struct svc_rpc_gss_cookedcred {
	struct svc_rpc_gss_client *cc_client;
	rpc_gss_service_t	cc_service;
	uint32_t		cc_seq;
};

#define CLIENT_HASH_SIZE	256
#define CLIENT_MAX		1024
u_int svc_rpc_gss_client_max = CLIENT_MAX;
u_int svc_rpc_gss_client_hash_size = CLIENT_HASH_SIZE;

SYSCTL_NODE(_kern, OID_AUTO, rpc, CTLFLAG_RW, 0, "RPC");
SYSCTL_NODE(_kern_rpc, OID_AUTO, gss, CTLFLAG_RW, 0, "GSS");

SYSCTL_UINT(_kern_rpc_gss, OID_AUTO, client_max, CTLFLAG_RW,
    &svc_rpc_gss_client_max, 0,
    "Max number of rpc-gss clients");

SYSCTL_UINT(_kern_rpc_gss, OID_AUTO, client_hash, CTLFLAG_RDTUN,
    &svc_rpc_gss_client_hash_size, 0,
    "Size of rpc-gss client hash table");

static u_int svc_rpc_gss_client_count;
SYSCTL_UINT(_kern_rpc_gss, OID_AUTO, client_count, CTLFLAG_RD,
    &svc_rpc_gss_client_count, 0,
    "Number of rpc-gss clients");

struct svc_rpc_gss_client_list *svc_rpc_gss_client_hash;
struct svc_rpc_gss_client_list svc_rpc_gss_clients;
static uint32_t svc_rpc_gss_next_clientid = 1;

static void
svc_rpc_gss_init(void *arg)
{
	int i;

	svc_rpc_gss_client_hash = mem_alloc(sizeof(struct svc_rpc_gss_client_list) * svc_rpc_gss_client_hash_size);
	for (i = 0; i < svc_rpc_gss_client_hash_size; i++)
		TAILQ_INIT(&svc_rpc_gss_client_hash[i]);
	TAILQ_INIT(&svc_rpc_gss_clients);
	svc_auth_reg(RPCSEC_GSS, svc_rpc_gss, rpc_gss_svc_getcred);
	sx_init(&svc_rpc_gss_lock, "gsslock");
}
SYSINIT(svc_rpc_gss_init, SI_SUB_KMEM, SI_ORDER_ANY, svc_rpc_gss_init, NULL);

bool_t
rpc_gss_set_callback(rpc_gss_callback_t *cb)
{
	struct svc_rpc_gss_callback *scb;

	scb = mem_alloc(sizeof(struct svc_rpc_gss_callback));
	if (!scb) {
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
		return (FALSE);
	}
	scb->cb_callback = *cb;
	sx_xlock(&svc_rpc_gss_lock);
	SLIST_INSERT_HEAD(&svc_rpc_gss_callbacks, scb, cb_link);
	sx_xunlock(&svc_rpc_gss_lock);

	return (TRUE);
}

void
rpc_gss_clear_callback(rpc_gss_callback_t *cb)
{
	struct svc_rpc_gss_callback *scb;

	sx_xlock(&svc_rpc_gss_lock);
	SLIST_FOREACH(scb, &svc_rpc_gss_callbacks, cb_link) {
		if (scb->cb_callback.program == cb->program
		    && scb->cb_callback.version == cb->version
		    && scb->cb_callback.callback == cb->callback) {
			SLIST_REMOVE(&svc_rpc_gss_callbacks, scb,
			    svc_rpc_gss_callback, cb_link);
			sx_xunlock(&svc_rpc_gss_lock);
			mem_free(scb, sizeof(*scb));
			return;
		}
	}
	sx_xunlock(&svc_rpc_gss_lock);
}

static bool_t
rpc_gss_acquire_svc_cred(struct svc_rpc_gss_svc_name *sname)
{
	OM_uint32		maj_stat, min_stat;
	gss_buffer_desc		namebuf;
	gss_name_t		name;
	gss_OID_set_desc	oid_set;

	oid_set.count = 1;
	oid_set.elements = sname->sn_mech;

	namebuf.value = (void *) sname->sn_principal;
	namebuf.length = strlen(sname->sn_principal);

	maj_stat = gss_import_name(&min_stat, &namebuf,
				   GSS_C_NT_HOSTBASED_SERVICE, &name);
	if (maj_stat != GSS_S_COMPLETE)
		return (FALSE);

	if (sname->sn_cred != GSS_C_NO_CREDENTIAL)
		gss_release_cred(&min_stat, &sname->sn_cred);

	maj_stat = gss_acquire_cred(&min_stat, name,
	    sname->sn_req_time, &oid_set, GSS_C_ACCEPT, &sname->sn_cred,
	    NULL, NULL);
	if (maj_stat != GSS_S_COMPLETE) {
		gss_release_name(&min_stat, &name);
		return (FALSE);
	}
	gss_release_name(&min_stat, &name);

	return (TRUE);
}

bool_t
rpc_gss_set_svc_name(const char *principal, const char *mechanism,
    u_int req_time, u_int program, u_int version)
{
	struct svc_rpc_gss_svc_name *sname;
	gss_OID			mech_oid;

	if (!rpc_gss_mech_to_oid(mechanism, &mech_oid))
		return (FALSE);

	sname = mem_alloc(sizeof(*sname));
	if (!sname)
		return (FALSE);
	sname->sn_principal = strdup(principal, M_RPC);
	sname->sn_mech = mech_oid;
	sname->sn_req_time = req_time;
	sname->sn_cred = GSS_C_NO_CREDENTIAL;
	sname->sn_program = program;
	sname->sn_version = version;

	if (!rpc_gss_acquire_svc_cred(sname)) {
		free(sname->sn_principal, M_RPC);
		mem_free(sname, sizeof(*sname));
		return (FALSE);
	}

	sx_xlock(&svc_rpc_gss_lock);
	SLIST_INSERT_HEAD(&svc_rpc_gss_svc_names, sname, sn_link);
	sx_xunlock(&svc_rpc_gss_lock);

	return (TRUE);
}

void
rpc_gss_clear_svc_name(u_int program, u_int version)
{
	OM_uint32		min_stat;
	struct svc_rpc_gss_svc_name *sname;

	sx_xlock(&svc_rpc_gss_lock);
	SLIST_FOREACH(sname, &svc_rpc_gss_svc_names, sn_link) {
		if (sname->sn_program == program
		    && sname->sn_version == version) {
			SLIST_REMOVE(&svc_rpc_gss_svc_names, sname,
			    svc_rpc_gss_svc_name, sn_link);
			sx_xunlock(&svc_rpc_gss_lock);
			gss_release_cred(&min_stat, &sname->sn_cred);
			free(sname->sn_principal, M_RPC);
			mem_free(sname, sizeof(*sname));
			return;
		}
	}
	sx_xunlock(&svc_rpc_gss_lock);
}

bool_t
rpc_gss_get_principal_name(rpc_gss_principal_t *principal,
    const char *mech, const char *name, const char *node, const char *domain)
{
	OM_uint32		maj_stat, min_stat;
	gss_OID			mech_oid;
	size_t			namelen;
	gss_buffer_desc		buf;
	gss_name_t		gss_name, gss_mech_name;
	rpc_gss_principal_t	result;

	if (!rpc_gss_mech_to_oid(mech, &mech_oid))
		return (FALSE);

	/*
	 * Construct a gss_buffer containing the full name formatted
	 * as "name/node@domain" where node and domain are optional.
	 */
	namelen = strlen(name) + 1;
	if (node) {
		namelen += strlen(node) + 1;
	}
	if (domain) {
		namelen += strlen(domain) + 1;
	}

	buf.value = mem_alloc(namelen);
	buf.length = namelen;
	strcpy((char *) buf.value, name);
	if (node) {
		strcat((char *) buf.value, "/");
		strcat((char *) buf.value, node);
	}
	if (domain) {
		strcat((char *) buf.value, "@");
		strcat((char *) buf.value, domain);
	}

	/*
	 * Convert that to a gss_name_t and then convert that to a
	 * mechanism name in the selected mechanism.
	 */
	maj_stat = gss_import_name(&min_stat, &buf,
	    GSS_C_NT_USER_NAME, &gss_name);
	mem_free(buf.value, buf.length);
	if (maj_stat != GSS_S_COMPLETE) {
		rpc_gss_log_status("gss_import_name", mech_oid, maj_stat, min_stat);
		return (FALSE);
	}
	maj_stat = gss_canonicalize_name(&min_stat, gss_name, mech_oid,
	    &gss_mech_name);
	if (maj_stat != GSS_S_COMPLETE) {
		rpc_gss_log_status("gss_canonicalize_name", mech_oid, maj_stat,
		    min_stat);
		gss_release_name(&min_stat, &gss_name);
		return (FALSE);
	}
	gss_release_name(&min_stat, &gss_name);

	/*
	 * Export the mechanism name and use that to construct the
	 * rpc_gss_principal_t result.
	 */
	maj_stat = gss_export_name(&min_stat, gss_mech_name, &buf);
	if (maj_stat != GSS_S_COMPLETE) {
		rpc_gss_log_status("gss_export_name", mech_oid, maj_stat, min_stat);
		gss_release_name(&min_stat, &gss_mech_name);
		return (FALSE);
	}
	gss_release_name(&min_stat, &gss_mech_name);

	result = mem_alloc(sizeof(int) + buf.length);
	if (!result) {
		gss_release_buffer(&min_stat, &buf);
		return (FALSE);
	}
	result->len = buf.length;
	memcpy(result->name, buf.value, buf.length);
	gss_release_buffer(&min_stat, &buf);

	*principal = result;
	return (TRUE);
}

bool_t
rpc_gss_getcred(struct svc_req *req, rpc_gss_rawcred_t **rcred,
    rpc_gss_ucred_t **ucred, void **cookie)
{
	struct svc_rpc_gss_cookedcred *cc;
	struct svc_rpc_gss_client *client;

	if (req->rq_cred.oa_flavor != RPCSEC_GSS)
		return (FALSE);

	cc = req->rq_clntcred;
	client = cc->cc_client;
	if (rcred)
		*rcred = &client->cl_rawcred;
	if (ucred)
		*ucred = &client->cl_ucred;
	if (cookie)
		*cookie = client->cl_cookie;
	return (TRUE);
}

/*
 * This simpler interface is used by svc_getcred to copy the cred data
 * into a kernel cred structure.
 */
static int
rpc_gss_svc_getcred(struct svc_req *req, struct ucred **crp, int *flavorp)
{
	struct ucred *cr;
	struct svc_rpc_gss_cookedcred *cc;
	struct svc_rpc_gss_client *client;
	rpc_gss_ucred_t *uc;

	if (req->rq_cred.oa_flavor != RPCSEC_GSS)
		return (FALSE);

	cc = req->rq_clntcred;
	client = cc->cc_client;

	if (flavorp)
		*flavorp = client->cl_rpcflavor;

	if (client->cl_cred) {
		*crp = crhold(client->cl_cred);
		return (TRUE);
	}

	uc = &client->cl_ucred;
	cr = client->cl_cred = crget();
	cr->cr_uid = cr->cr_ruid = cr->cr_svuid = uc->uid;
	cr->cr_rgid = cr->cr_svgid = uc->gid;
	crsetgroups(cr, uc->gidlen, uc->gidlist);
	cr->cr_prison = &prison0;
	prison_hold(cr->cr_prison);
	*crp = crhold(cr);

	return (TRUE);
}

int
rpc_gss_svc_max_data_length(struct svc_req *req, int max_tp_unit_len)
{
	struct svc_rpc_gss_cookedcred *cc = req->rq_clntcred;
	struct svc_rpc_gss_client *client = cc->cc_client;
	int			want_conf;
	OM_uint32		max;
	OM_uint32		maj_stat, min_stat;
	int			result;

	switch (client->cl_rawcred.service) {
	case rpc_gss_svc_none:
		return (max_tp_unit_len);
		break;

	case rpc_gss_svc_default:
	case rpc_gss_svc_integrity:
		want_conf = FALSE;
		break;

	case rpc_gss_svc_privacy:
		want_conf = TRUE;
		break;

	default:
		return (0);
	}

	maj_stat = gss_wrap_size_limit(&min_stat, client->cl_ctx, want_conf,
	    client->cl_qop, max_tp_unit_len, &max);

	if (maj_stat == GSS_S_COMPLETE) {
		result = (int) max;
		if (result < 0)
			result = 0;
		return (result);
	} else {
		rpc_gss_log_status("gss_wrap_size_limit", client->cl_mech,
		    maj_stat, min_stat);
		return (0);
	}
}

static struct svc_rpc_gss_client *
svc_rpc_gss_find_client(struct svc_rpc_gss_clientid *id)
{
	struct svc_rpc_gss_client *client;
	struct svc_rpc_gss_client_list *list;
	struct timeval boottime;
	unsigned long hostid;

	rpc_gss_log_debug("in svc_rpc_gss_find_client(%d)", id->ci_id);

	getcredhostid(curthread->td_ucred, &hostid);
	getboottime(&boottime);
	if (id->ci_hostid != hostid || id->ci_boottime != boottime.tv_sec)
		return (NULL);

	list = &svc_rpc_gss_client_hash[id->ci_id % svc_rpc_gss_client_hash_size];
	sx_xlock(&svc_rpc_gss_lock);
	TAILQ_FOREACH(client, list, cl_link) {
		if (client->cl_id.ci_id == id->ci_id) {
			/*
			 * Move this client to the front of the LRU
			 * list.
			 */
			TAILQ_REMOVE(&svc_rpc_gss_clients, client, cl_alllink);
			TAILQ_INSERT_HEAD(&svc_rpc_gss_clients, client,
			    cl_alllink);
			refcount_acquire(&client->cl_refs);
			break;
		}
	}
	sx_xunlock(&svc_rpc_gss_lock);

	return (client);
}

static struct svc_rpc_gss_client *
svc_rpc_gss_create_client(void)
{
	struct svc_rpc_gss_client *client;
	struct svc_rpc_gss_client_list *list;
	struct timeval boottime;
	unsigned long hostid;

	rpc_gss_log_debug("in svc_rpc_gss_create_client()");

	client = mem_alloc(sizeof(struct svc_rpc_gss_client));
	memset(client, 0, sizeof(struct svc_rpc_gss_client));
	refcount_init(&client->cl_refs, 1);
	sx_init(&client->cl_lock, "GSS-client");
	getcredhostid(curthread->td_ucred, &hostid);
	client->cl_id.ci_hostid = hostid;
	getboottime(&boottime);
	client->cl_id.ci_boottime = boottime.tv_sec;
	client->cl_id.ci_id = svc_rpc_gss_next_clientid++;
	list = &svc_rpc_gss_client_hash[client->cl_id.ci_id % svc_rpc_gss_client_hash_size];
	sx_xlock(&svc_rpc_gss_lock);
	TAILQ_INSERT_HEAD(list, client, cl_link);
	TAILQ_INSERT_HEAD(&svc_rpc_gss_clients, client, cl_alllink);
	svc_rpc_gss_client_count++;
	sx_xunlock(&svc_rpc_gss_lock);

	/*
	 * Start the client off with a short expiration time. We will
	 * try to get a saner value from the client creds later.
	 */
	client->cl_state = CLIENT_NEW;
	client->cl_locked = FALSE;
	client->cl_expiration = time_uptime + 5*60;

	return (client);
}

static void
svc_rpc_gss_destroy_client(struct svc_rpc_gss_client *client)
{
	OM_uint32 min_stat;

	rpc_gss_log_debug("in svc_rpc_gss_destroy_client()");

	if (client->cl_ctx)
		gss_delete_sec_context(&min_stat,
		    &client->cl_ctx, GSS_C_NO_BUFFER);

	if (client->cl_cname)
		gss_release_name(&min_stat, &client->cl_cname);

	if (client->cl_rawcred.client_principal)
		mem_free(client->cl_rawcred.client_principal,
		    sizeof(*client->cl_rawcred.client_principal)
		    + client->cl_rawcred.client_principal->len);

	if (client->cl_cred)
		crfree(client->cl_cred);

	sx_destroy(&client->cl_lock);
	mem_free(client, sizeof(*client));
}

/*
 * Drop a reference to a client and free it if that was the last reference.
 */
static void
svc_rpc_gss_release_client(struct svc_rpc_gss_client *client)
{

	if (!refcount_release(&client->cl_refs))
		return;
	svc_rpc_gss_destroy_client(client);
}

/*
 * Remove a client from our global lists.
 * Must be called with svc_rpc_gss_lock held.
 */
static void
svc_rpc_gss_forget_client_locked(struct svc_rpc_gss_client *client)
{
	struct svc_rpc_gss_client_list *list;

	sx_assert(&svc_rpc_gss_lock, SX_XLOCKED);
	list = &svc_rpc_gss_client_hash[client->cl_id.ci_id % svc_rpc_gss_client_hash_size];
	TAILQ_REMOVE(list, client, cl_link);
	TAILQ_REMOVE(&svc_rpc_gss_clients, client, cl_alllink);
	svc_rpc_gss_client_count--;
}

/*
 * Remove a client from our global lists and free it if we can.
 */
static void
svc_rpc_gss_forget_client(struct svc_rpc_gss_client *client)
{
	struct svc_rpc_gss_client_list *list;
	struct svc_rpc_gss_client *tclient;

	list = &svc_rpc_gss_client_hash[client->cl_id.ci_id % svc_rpc_gss_client_hash_size];
	sx_xlock(&svc_rpc_gss_lock);
	TAILQ_FOREACH(tclient, list, cl_link) {
		/*
		 * Make sure this client has not already been removed
		 * from the lists by svc_rpc_gss_forget_client() or
		 * svc_rpc_gss_forget_client_locked().
		 */
		if (client == tclient) {
			svc_rpc_gss_forget_client_locked(client);
			sx_xunlock(&svc_rpc_gss_lock);
			svc_rpc_gss_release_client(client);
			return;
		}
	}
	sx_xunlock(&svc_rpc_gss_lock);
}

static void
svc_rpc_gss_timeout_clients(void)
{
	struct svc_rpc_gss_client *client;
	time_t now = time_uptime;

	rpc_gss_log_debug("in svc_rpc_gss_timeout_clients()");

	/*
	 * First enforce the max client limit. We keep
	 * svc_rpc_gss_clients in LRU order.
	 */
	sx_xlock(&svc_rpc_gss_lock);
	client = TAILQ_LAST(&svc_rpc_gss_clients, svc_rpc_gss_client_list);
	while (svc_rpc_gss_client_count > svc_rpc_gss_client_max && client != NULL) {
		svc_rpc_gss_forget_client_locked(client);
		sx_xunlock(&svc_rpc_gss_lock);
		svc_rpc_gss_release_client(client);
		sx_xlock(&svc_rpc_gss_lock);
		client = TAILQ_LAST(&svc_rpc_gss_clients,
		    svc_rpc_gss_client_list);
	}
again:
	TAILQ_FOREACH(client, &svc_rpc_gss_clients, cl_alllink) {
		if (client->cl_state == CLIENT_STALE
		    || now > client->cl_expiration) {
			svc_rpc_gss_forget_client_locked(client);
			sx_xunlock(&svc_rpc_gss_lock);
			rpc_gss_log_debug("expiring client %p", client);
			svc_rpc_gss_release_client(client);
			sx_xlock(&svc_rpc_gss_lock);
			goto again;
		}
	}
	sx_xunlock(&svc_rpc_gss_lock);
}

#ifdef DEBUG
/*
 * OID<->string routines.  These are uuuuugly.
 */
static OM_uint32
gss_oid_to_str(OM_uint32 *minor_status, gss_OID oid, gss_buffer_t oid_str)
{
	char		numstr[128];
	unsigned long	number;
	int		numshift;
	size_t		string_length;
	size_t		i;
	unsigned char	*cp;
	char		*bp;

	/* Decoded according to krb5/gssapi_krb5.c */

	/* First determine the size of the string */
	string_length = 0;
	number = 0;
	numshift = 0;
	cp = (unsigned char *) oid->elements;
	number = (unsigned long) cp[0];
	sprintf(numstr, "%ld ", number/40);
	string_length += strlen(numstr);
	sprintf(numstr, "%ld ", number%40);
	string_length += strlen(numstr);
	for (i=1; i<oid->length; i++) {
		if ( (size_t) (numshift+7) < (sizeof(unsigned long)*8)) {
			number = (number << 7) | (cp[i] & 0x7f);
			numshift += 7;
		}
		else {
			*minor_status = 0;
			return(GSS_S_FAILURE);
		}
		if ((cp[i] & 0x80) == 0) {
			sprintf(numstr, "%ld ", number);
			string_length += strlen(numstr);
			number = 0;
			numshift = 0;
		}
	}
	/*
	 * If we get here, we've calculated the length of "n n n ... n ".  Add 4
	 * here for "{ " and "}\0".
	 */
	string_length += 4;
	if ((bp = (char *) mem_alloc(string_length))) {
		strcpy(bp, "{ ");
		number = (unsigned long) cp[0];
		sprintf(numstr, "%ld ", number/40);
		strcat(bp, numstr);
		sprintf(numstr, "%ld ", number%40);
		strcat(bp, numstr);
		number = 0;
		cp = (unsigned char *) oid->elements;
		for (i=1; i<oid->length; i++) {
			number = (number << 7) | (cp[i] & 0x7f);
			if ((cp[i] & 0x80) == 0) {
				sprintf(numstr, "%ld ", number);
				strcat(bp, numstr);
				number = 0;
			}
		}
		strcat(bp, "}");
		oid_str->length = strlen(bp)+1;
		oid_str->value = (void *) bp;
		*minor_status = 0;
		return(GSS_S_COMPLETE);
	}
	*minor_status = 0;
	return(GSS_S_FAILURE);
}
#endif

static void
svc_rpc_gss_build_ucred(struct svc_rpc_gss_client *client,
    const gss_name_t name)
{
	OM_uint32		maj_stat, min_stat;
	rpc_gss_ucred_t		*uc = &client->cl_ucred;
	int			numgroups;

	uc->uid = 65534;
	uc->gid = 65534;
	uc->gidlist = client->cl_gid_storage;

	numgroups = NGROUPS;
	maj_stat = gss_pname_to_unix_cred(&min_stat, name, client->cl_mech,
	    &uc->uid, &uc->gid, &numgroups, &uc->gidlist[0]);
	if (GSS_ERROR(maj_stat))
		uc->gidlen = 0;
	else
		uc->gidlen = numgroups;
}

static void
svc_rpc_gss_set_flavor(struct svc_rpc_gss_client *client)
{
	static gss_OID_desc krb5_mech_oid =
		{9, (void *) "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02" };

	/*
	 * Attempt to translate mech type and service into a
	 * 'pseudo flavor'. Hardwire in krb5 support for now.
	 */
	if (kgss_oid_equal(client->cl_mech, &krb5_mech_oid)) {
		switch (client->cl_rawcred.service) {
		case rpc_gss_svc_default:
		case rpc_gss_svc_none:
			client->cl_rpcflavor = RPCSEC_GSS_KRB5;
			break;
		case rpc_gss_svc_integrity:
			client->cl_rpcflavor = RPCSEC_GSS_KRB5I;
			break;
		case rpc_gss_svc_privacy:
			client->cl_rpcflavor = RPCSEC_GSS_KRB5P;
			break;
		}
	} else {
		client->cl_rpcflavor = RPCSEC_GSS;
	}
}

static bool_t
svc_rpc_gss_accept_sec_context(struct svc_rpc_gss_client *client,
			       struct svc_req *rqst,
			       struct rpc_gss_init_res *gr,
			       struct rpc_gss_cred *gc)
{
	gss_buffer_desc		recv_tok;
	gss_OID			mech;
	OM_uint32		maj_stat = 0, min_stat = 0, ret_flags;
	OM_uint32		cred_lifetime;
	struct svc_rpc_gss_svc_name *sname;

	rpc_gss_log_debug("in svc_rpc_gss_accept_context()");
	
	/* Deserialize arguments. */
	memset(&recv_tok, 0, sizeof(recv_tok));
	
	if (!svc_getargs(rqst,
		(xdrproc_t) xdr_gss_buffer_desc,
		(caddr_t) &recv_tok)) {
		client->cl_state = CLIENT_STALE;
		return (FALSE);
	}

	/*
	 * First time round, try all the server names we have until
	 * one matches. Afterwards, stick with that one.
	 */
	sx_xlock(&svc_rpc_gss_lock);
	if (!client->cl_sname) {
		SLIST_FOREACH(sname, &svc_rpc_gss_svc_names, sn_link) {
			if (sname->sn_program == rqst->rq_prog
			    && sname->sn_version == rqst->rq_vers) {
			retry:
				gr->gr_major = gss_accept_sec_context(
					&gr->gr_minor,
					&client->cl_ctx,
					sname->sn_cred,
					&recv_tok,
					GSS_C_NO_CHANNEL_BINDINGS,
					&client->cl_cname,
					&mech,
					&gr->gr_token,
					&ret_flags,
					&cred_lifetime,
					&client->cl_creds);
				if (gr->gr_major == 
				    GSS_S_CREDENTIALS_EXPIRED) {
					/*
					 * Either our creds really did
					 * expire or gssd was
					 * restarted.
					 */
					if (rpc_gss_acquire_svc_cred(sname))
						goto retry;
				}
				client->cl_sname = sname;
				break;
			}
		}
		if (!sname) {
			xdr_free((xdrproc_t) xdr_gss_buffer_desc,
			    (char *) &recv_tok);
			sx_xunlock(&svc_rpc_gss_lock);
			return (FALSE);
		}
	} else {
		gr->gr_major = gss_accept_sec_context(
			&gr->gr_minor,
			&client->cl_ctx,
			client->cl_sname->sn_cred,
			&recv_tok,
			GSS_C_NO_CHANNEL_BINDINGS,
			&client->cl_cname,
			&mech,
			&gr->gr_token,
			&ret_flags,
			&cred_lifetime,
			NULL);
	}
	sx_xunlock(&svc_rpc_gss_lock);
	
	xdr_free((xdrproc_t) xdr_gss_buffer_desc, (char *) &recv_tok);

	/*
	 * If we get an error from gss_accept_sec_context, send the
	 * reply anyway so that the client gets a chance to see what
	 * is wrong.
	 */
	if (gr->gr_major != GSS_S_COMPLETE &&
	    gr->gr_major != GSS_S_CONTINUE_NEEDED) {
		rpc_gss_log_status("accept_sec_context", client->cl_mech,
		    gr->gr_major, gr->gr_minor);
		client->cl_state = CLIENT_STALE;
		return (TRUE);
	}

	gr->gr_handle.value = &client->cl_id;
	gr->gr_handle.length = sizeof(client->cl_id);
	gr->gr_win = SVC_RPC_GSS_SEQWINDOW;
	
	/* Save client info. */
	client->cl_mech = mech;
	client->cl_qop = GSS_C_QOP_DEFAULT;
	client->cl_done_callback = FALSE;

	if (gr->gr_major == GSS_S_COMPLETE) {
		gss_buffer_desc	export_name;

		/*
		 * Change client expiration time to be near when the
		 * client creds expire (or 24 hours if we can't figure
		 * that out).
		 */
		if (cred_lifetime == GSS_C_INDEFINITE)
			cred_lifetime = time_uptime + 24*60*60;

		client->cl_expiration = time_uptime + cred_lifetime;

		/*
		 * Fill in cred details in the rawcred structure.
		 */
		client->cl_rawcred.version = RPCSEC_GSS_VERSION;
		rpc_gss_oid_to_mech(mech, &client->cl_rawcred.mechanism);
		maj_stat = gss_export_name(&min_stat, client->cl_cname,
		    &export_name);
		if (maj_stat != GSS_S_COMPLETE) {
			rpc_gss_log_status("gss_export_name", client->cl_mech,
			    maj_stat, min_stat);
			return (FALSE);
		}
		client->cl_rawcred.client_principal =
			mem_alloc(sizeof(*client->cl_rawcred.client_principal)
			    + export_name.length);
		client->cl_rawcred.client_principal->len = export_name.length;
		memcpy(client->cl_rawcred.client_principal->name,
		    export_name.value, export_name.length);
		gss_release_buffer(&min_stat, &export_name);
		client->cl_rawcred.svc_principal =
			client->cl_sname->sn_principal;
		client->cl_rawcred.service = gc->gc_svc;

		/*
		 * Use gss_pname_to_uid to map to unix creds. For
		 * kerberos5, this uses krb5_aname_to_localname.
		 */
		svc_rpc_gss_build_ucred(client, client->cl_cname);
		svc_rpc_gss_set_flavor(client);
		gss_release_name(&min_stat, &client->cl_cname);

#ifdef DEBUG
		{
			gss_buffer_desc mechname;

			gss_oid_to_str(&min_stat, mech, &mechname);
			
			rpc_gss_log_debug("accepted context for %s with "
			    "<mech %.*s, qop %d, svc %d>",
			    client->cl_rawcred.client_principal->name,
			    mechname.length, (char *)mechname.value,
			    client->cl_qop, client->cl_rawcred.service);

			gss_release_buffer(&min_stat, &mechname);
		}
#endif /* DEBUG */
	}
	return (TRUE);
}

static bool_t
svc_rpc_gss_validate(struct svc_rpc_gss_client *client, struct rpc_msg *msg,
    gss_qop_t *qop, rpc_gss_proc_t gcproc)
{
	struct opaque_auth	*oa;
	gss_buffer_desc		 rpcbuf, checksum;
	OM_uint32		 maj_stat, min_stat;
	gss_qop_t		 qop_state;
	int32_t			 rpchdr[128 / sizeof(int32_t)];
	int32_t			*buf;

	rpc_gss_log_debug("in svc_rpc_gss_validate()");
	
	memset(rpchdr, 0, sizeof(rpchdr));

	/* Reconstruct RPC header for signing (from xdr_callmsg). */
	buf = rpchdr;
	IXDR_PUT_LONG(buf, msg->rm_xid);
	IXDR_PUT_ENUM(buf, msg->rm_direction);
	IXDR_PUT_LONG(buf, msg->rm_call.cb_rpcvers);
	IXDR_PUT_LONG(buf, msg->rm_call.cb_prog);
	IXDR_PUT_LONG(buf, msg->rm_call.cb_vers);
	IXDR_PUT_LONG(buf, msg->rm_call.cb_proc);
	oa = &msg->rm_call.cb_cred;
	IXDR_PUT_ENUM(buf, oa->oa_flavor);
	IXDR_PUT_LONG(buf, oa->oa_length);
	if (oa->oa_length) {
		memcpy((caddr_t)buf, oa->oa_base, oa->oa_length);
		buf += RNDUP(oa->oa_length) / sizeof(int32_t);
	}
	rpcbuf.value = rpchdr;
	rpcbuf.length = (u_char *)buf - (u_char *)rpchdr;

	checksum.value = msg->rm_call.cb_verf.oa_base;
	checksum.length = msg->rm_call.cb_verf.oa_length;
	
	maj_stat = gss_verify_mic(&min_stat, client->cl_ctx, &rpcbuf, &checksum,
				  &qop_state);
	
	if (maj_stat != GSS_S_COMPLETE) {
		rpc_gss_log_status("gss_verify_mic", client->cl_mech,
		    maj_stat, min_stat);
		/*
		 * A bug in some versions of the Linux client generates a
		 * Destroy operation with a bogus encrypted checksum. Deleting
		 * the credential handle for that case causes the mount to fail.
		 * Since the checksum is bogus (gss_verify_mic() failed), it
		 * doesn't make sense to destroy the handle and not doing so
		 * fixes the Linux mount.
		 */
		if (gcproc != RPCSEC_GSS_DESTROY)
			client->cl_state = CLIENT_STALE;
		return (FALSE);
	}

	*qop = qop_state;
	return (TRUE);
}

static bool_t
svc_rpc_gss_nextverf(struct svc_rpc_gss_client *client,
    struct svc_req *rqst, u_int seq)
{
	gss_buffer_desc		signbuf;
	gss_buffer_desc		mic;
	OM_uint32		maj_stat, min_stat;
	uint32_t		nseq;       

	rpc_gss_log_debug("in svc_rpc_gss_nextverf()");

	nseq = htonl(seq);
	signbuf.value = &nseq;
	signbuf.length = sizeof(nseq);

	maj_stat = gss_get_mic(&min_stat, client->cl_ctx, client->cl_qop,
	    &signbuf, &mic);

	if (maj_stat != GSS_S_COMPLETE) {
		rpc_gss_log_status("gss_get_mic", client->cl_mech, maj_stat, min_stat);
		client->cl_state = CLIENT_STALE;
		return (FALSE);
	}

	KASSERT(mic.length <= MAX_AUTH_BYTES,
	    ("MIC too large for RPCSEC_GSS"));

	rqst->rq_verf.oa_flavor = RPCSEC_GSS;
	rqst->rq_verf.oa_length = mic.length;
	bcopy(mic.value, rqst->rq_verf.oa_base, mic.length);

	gss_release_buffer(&min_stat, &mic);
	
	return (TRUE);
}

static bool_t
svc_rpc_gss_callback(struct svc_rpc_gss_client *client, struct svc_req *rqst)
{
	struct svc_rpc_gss_callback *scb;
	rpc_gss_lock_t	lock;
	void		*cookie;
	bool_t		cb_res;
	bool_t		result;

	/*
	 * See if we have a callback for this guy.
	 */
	result = TRUE;
	SLIST_FOREACH(scb, &svc_rpc_gss_callbacks, cb_link) {
		if (scb->cb_callback.program == rqst->rq_prog
		    && scb->cb_callback.version == rqst->rq_vers) {
			/*
			 * This one matches. Call the callback and see
			 * if it wants to veto or something.
			 */
			lock.locked = FALSE;
			lock.raw_cred = &client->cl_rawcred;
			cb_res = scb->cb_callback.callback(rqst,
			    client->cl_creds,
			    client->cl_ctx,
			    &lock,
			    &cookie);

			if (!cb_res) {
				client->cl_state = CLIENT_STALE;
				result = FALSE;
				break;
			}

			/*
			 * The callback accepted the connection - it
			 * is responsible for freeing client->cl_creds
			 * now.
			 */
			client->cl_creds = GSS_C_NO_CREDENTIAL;
			client->cl_locked = lock.locked;
			client->cl_cookie = cookie;
			return (TRUE);
		}
	}

	/*
	 * Either no callback exists for this program/version or one
	 * of the callbacks rejected the connection. We just need to
	 * clean up the delegated client creds, if any.
	 */
	if (client->cl_creds) {
		OM_uint32 min_ver;
		gss_release_cred(&min_ver, &client->cl_creds);
	}
	return (result);
}

static bool_t
svc_rpc_gss_check_replay(struct svc_rpc_gss_client *client, uint32_t seq)
{
	u_int32_t offset;
	int word, bit;
	bool_t result;

	sx_xlock(&client->cl_lock);
	if (seq <= client->cl_seqlast) {
		/*
		 * The request sequence number is less than
		 * the largest we have seen so far. If it is
		 * outside the window or if we have seen a
		 * request with this sequence before, silently
		 * discard it.
		 */
		offset = client->cl_seqlast - seq;
		if (offset >= SVC_RPC_GSS_SEQWINDOW) {
			result = FALSE;
			goto out;
		}
		word = offset / 32;
		bit = offset % 32;
		if (client->cl_seqmask[word] & (1 << bit)) {
			result = FALSE;
			goto out;
		}
	}

	result = TRUE;
out:
	sx_xunlock(&client->cl_lock);
	return (result);
}

static void
svc_rpc_gss_update_seq(struct svc_rpc_gss_client *client, uint32_t seq)
{
	int offset, i, word, bit;
	uint32_t carry, newcarry;

	sx_xlock(&client->cl_lock);
	if (seq > client->cl_seqlast) {
		/*
		 * This request has a sequence number greater
		 * than any we have seen so far. Advance the
		 * seq window and set bit zero of the window
		 * (which corresponds to the new sequence
		 * number)
		 */
		offset = seq - client->cl_seqlast;
		while (offset > 32) {
			for (i = (SVC_RPC_GSS_SEQWINDOW / 32) - 1;
			     i > 0; i--) {
				client->cl_seqmask[i] = client->cl_seqmask[i-1];
			}
			client->cl_seqmask[0] = 0;
			offset -= 32;
		}
		carry = 0;
		for (i = 0; i < SVC_RPC_GSS_SEQWINDOW / 32; i++) {
			newcarry = client->cl_seqmask[i] >> (32 - offset);
			client->cl_seqmask[i] =
				(client->cl_seqmask[i] << offset) | carry;
			carry = newcarry;
		}
		client->cl_seqmask[0] |= 1;
		client->cl_seqlast = seq;
	} else {
		offset = client->cl_seqlast - seq;
		word = offset / 32;
		bit = offset % 32;
		client->cl_seqmask[word] |= (1 << bit);
	}
	sx_xunlock(&client->cl_lock);
}

enum auth_stat
svc_rpc_gss(struct svc_req *rqst, struct rpc_msg *msg)

{
	OM_uint32		 min_stat;
	XDR	 		 xdrs;
	struct svc_rpc_gss_cookedcred *cc;
	struct svc_rpc_gss_client *client;
	struct rpc_gss_cred	 gc;
	struct rpc_gss_init_res	 gr;
	gss_qop_t		 qop;
	int			 call_stat;
	enum auth_stat		 result;
	
	rpc_gss_log_debug("in svc_rpc_gss()");
	
	/* Garbage collect old clients. */
	svc_rpc_gss_timeout_clients();

	/* Initialize reply. */
	rqst->rq_verf = _null_auth;

	/* Deserialize client credentials. */
	if (rqst->rq_cred.oa_length <= 0)
		return (AUTH_BADCRED);
	
	memset(&gc, 0, sizeof(gc));
	
	xdrmem_create(&xdrs, rqst->rq_cred.oa_base,
	    rqst->rq_cred.oa_length, XDR_DECODE);
	
	if (!xdr_rpc_gss_cred(&xdrs, &gc)) {
		XDR_DESTROY(&xdrs);
		return (AUTH_BADCRED);
	}
	XDR_DESTROY(&xdrs);

	client = NULL;

	/* Check version. */
	if (gc.gc_version != RPCSEC_GSS_VERSION) {
		result = AUTH_BADCRED;
		goto out;
	}

	/* Check the proc and find the client (or create it) */
	if (gc.gc_proc == RPCSEC_GSS_INIT) {
		if (gc.gc_handle.length != 0) {
			result = AUTH_BADCRED;
			goto out;
		}
		client = svc_rpc_gss_create_client();
		refcount_acquire(&client->cl_refs);
	} else {
		struct svc_rpc_gss_clientid *p;
		if (gc.gc_handle.length != sizeof(*p)) {
			result = AUTH_BADCRED;
			goto out;
		}
		p = gc.gc_handle.value;
		client = svc_rpc_gss_find_client(p);
		if (!client) {
			/*
			 * Can't find the client - we may have
			 * destroyed it - tell the other side to
			 * re-authenticate.
			 */
			result = RPCSEC_GSS_CREDPROBLEM;
			goto out;
		}
	}
	cc = rqst->rq_clntcred;
	cc->cc_client = client;
	cc->cc_service = gc.gc_svc;
	cc->cc_seq = gc.gc_seq;

	/*
	 * The service and sequence number must be ignored for
	 * RPCSEC_GSS_INIT and RPCSEC_GSS_CONTINUE_INIT.
	 */
	if (gc.gc_proc != RPCSEC_GSS_INIT
	    && gc.gc_proc != RPCSEC_GSS_CONTINUE_INIT) {
		/*
		 * Check for sequence number overflow.
		 */
		if (gc.gc_seq >= MAXSEQ) {
			result = RPCSEC_GSS_CTXPROBLEM;
			goto out;
		}

		/*
		 * Check for valid service.
		 */
		if (gc.gc_svc != rpc_gss_svc_none &&
		    gc.gc_svc != rpc_gss_svc_integrity &&
		    gc.gc_svc != rpc_gss_svc_privacy) {
			result = AUTH_BADCRED;
			goto out;
		}
	}

	/* Handle RPCSEC_GSS control procedure. */
	switch (gc.gc_proc) {

	case RPCSEC_GSS_INIT:
	case RPCSEC_GSS_CONTINUE_INIT:
		if (rqst->rq_proc != NULLPROC) {
			result = AUTH_REJECTEDCRED;
			break;
		}

		memset(&gr, 0, sizeof(gr));
		if (!svc_rpc_gss_accept_sec_context(client, rqst, &gr, &gc)) {
			result = AUTH_REJECTEDCRED;
			break;
		}

		if (gr.gr_major == GSS_S_COMPLETE) {
			/*
			 * We borrow the space for the call verf to
			 * pack our reply verf.
			 */
			rqst->rq_verf = msg->rm_call.cb_verf;
			if (!svc_rpc_gss_nextverf(client, rqst, gr.gr_win)) {
				result = AUTH_REJECTEDCRED;
				break;
			}
		} else {
			rqst->rq_verf = _null_auth;
		}
		
		call_stat = svc_sendreply(rqst,
		    (xdrproc_t) xdr_rpc_gss_init_res,
		    (caddr_t) &gr);

		gss_release_buffer(&min_stat, &gr.gr_token);

		if (!call_stat) {
			result = AUTH_FAILED;
			break;
		}

		if (gr.gr_major == GSS_S_COMPLETE)
			client->cl_state = CLIENT_ESTABLISHED;

		result = RPCSEC_GSS_NODISPATCH;
		break;
		
	case RPCSEC_GSS_DATA:
	case RPCSEC_GSS_DESTROY:
		if (!svc_rpc_gss_check_replay(client, gc.gc_seq)) {
			result = RPCSEC_GSS_NODISPATCH;
			break;
		}

		if (!svc_rpc_gss_validate(client, msg, &qop, gc.gc_proc)) {
			result = RPCSEC_GSS_CREDPROBLEM;
			break;
		}
		
		/*
		 * We borrow the space for the call verf to pack our
		 * reply verf.
		 */
		rqst->rq_verf = msg->rm_call.cb_verf;
		if (!svc_rpc_gss_nextverf(client, rqst, gc.gc_seq)) {
			result = RPCSEC_GSS_CTXPROBLEM;
			break;
		}

		svc_rpc_gss_update_seq(client, gc.gc_seq);

		/*
		 * Change the SVCAUTH ops on the request to point at
		 * our own code so that we can unwrap the arguments
		 * and wrap the result. The caller will re-set this on
		 * every request to point to a set of null wrap/unwrap
		 * methods. Acquire an extra reference to the client
		 * which will be released by svc_rpc_gss_release()
		 * after the request has finished processing.
		 */
		refcount_acquire(&client->cl_refs);
		rqst->rq_auth.svc_ah_ops = &svc_auth_gss_ops;
		rqst->rq_auth.svc_ah_private = cc;

		if (gc.gc_proc == RPCSEC_GSS_DATA) {
			/*
			 * We might be ready to do a callback to the server to
			 * see if it wants to accept/reject the connection.
			 */
			sx_xlock(&client->cl_lock);
			if (!client->cl_done_callback) {
				client->cl_done_callback = TRUE;
				client->cl_qop = qop;
				client->cl_rawcred.qop = _rpc_gss_num_to_qop(
					client->cl_rawcred.mechanism, qop);
				if (!svc_rpc_gss_callback(client, rqst)) {
					result = AUTH_REJECTEDCRED;
					sx_xunlock(&client->cl_lock);
					break;
				}
			}
			sx_xunlock(&client->cl_lock);

			/*
			 * If the server has locked this client to a
			 * particular service+qop pair, enforce that
			 * restriction now.
			 */
			if (client->cl_locked) {
				if (client->cl_rawcred.service != gc.gc_svc) {
					result = AUTH_FAILED;
					break;
				} else if (client->cl_qop != qop) {
					result = AUTH_BADVERF;
					break;
				}
			}

			/*
			 * If the qop changed, look up the new qop
			 * name for rawcred.
			 */
			if (client->cl_qop != qop) {
				client->cl_qop = qop;
				client->cl_rawcred.qop = _rpc_gss_num_to_qop(
					client->cl_rawcred.mechanism, qop);
			}

			/*
			 * Make sure we use the right service value
			 * for unwrap/wrap.
			 */
			if (client->cl_rawcred.service != gc.gc_svc) {
				client->cl_rawcred.service = gc.gc_svc;
				svc_rpc_gss_set_flavor(client);
			}

			result = AUTH_OK;
		} else {
			if (rqst->rq_proc != NULLPROC) {
				result = AUTH_REJECTEDCRED;
				break;
			}

			call_stat = svc_sendreply(rqst,
			    (xdrproc_t) xdr_void, (caddr_t) NULL);

			if (!call_stat) {
				result = AUTH_FAILED;
				break;
			}

			svc_rpc_gss_forget_client(client);

			result = RPCSEC_GSS_NODISPATCH;
			break;
		}
		break;

	default:
		result = AUTH_BADCRED;
		break;
	}
out:
	if (client)
		svc_rpc_gss_release_client(client);

	xdr_free((xdrproc_t) xdr_rpc_gss_cred, (char *) &gc);
	return (result);
}

static bool_t
svc_rpc_gss_wrap(SVCAUTH *auth, struct mbuf **mp)
{
	struct svc_rpc_gss_cookedcred *cc;
	struct svc_rpc_gss_client *client;
	
	rpc_gss_log_debug("in svc_rpc_gss_wrap()");

	cc = (struct svc_rpc_gss_cookedcred *) auth->svc_ah_private;
	client = cc->cc_client;
	if (client->cl_state != CLIENT_ESTABLISHED
	    || cc->cc_service == rpc_gss_svc_none || *mp == NULL) {
		return (TRUE);
	}
	
	return (xdr_rpc_gss_wrap_data(mp,
		client->cl_ctx, client->cl_qop,
		cc->cc_service, cc->cc_seq));
}

static bool_t
svc_rpc_gss_unwrap(SVCAUTH *auth, struct mbuf **mp)
{
	struct svc_rpc_gss_cookedcred *cc;
	struct svc_rpc_gss_client *client;

	rpc_gss_log_debug("in svc_rpc_gss_unwrap()");
	
	cc = (struct svc_rpc_gss_cookedcred *) auth->svc_ah_private;
	client = cc->cc_client;
	if (client->cl_state != CLIENT_ESTABLISHED
	    || cc->cc_service == rpc_gss_svc_none) {
		return (TRUE);
	}

	return (xdr_rpc_gss_unwrap_data(mp,
		client->cl_ctx, client->cl_qop,
		cc->cc_service, cc->cc_seq));
}

static void
svc_rpc_gss_release(SVCAUTH *auth)
{
	struct svc_rpc_gss_cookedcred *cc;
	struct svc_rpc_gss_client *client;

	rpc_gss_log_debug("in svc_rpc_gss_release()");

	cc = (struct svc_rpc_gss_cookedcred *) auth->svc_ah_private;
	client = cc->cc_client;
	svc_rpc_gss_release_client(client);
}
