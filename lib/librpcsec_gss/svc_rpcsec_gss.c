/*-
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
 *
 *	$FreeBSD$
 */
/*-
 SPDX-License-Identifier: BSD-3-Clause

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <unistd.h>
#include <sys/queue.h>
#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>
#include "rpcsec_gss_int.h"

static bool_t	svc_rpc_gss_initialised = FALSE;

static bool_t   svc_rpc_gss_wrap(SVCAUTH *, XDR *, xdrproc_t, caddr_t);
static bool_t   svc_rpc_gss_unwrap(SVCAUTH *, XDR *, xdrproc_t, caddr_t);
static enum auth_stat svc_rpc_gss(struct svc_req *, struct rpc_msg *);

static struct svc_auth_ops svc_auth_gss_ops = {
	svc_rpc_gss_wrap,
	svc_rpc_gss_unwrap,
};

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

struct svc_rpc_gss_client {
	TAILQ_ENTRY(svc_rpc_gss_client) cl_link;
	TAILQ_ENTRY(svc_rpc_gss_client) cl_alllink;
	uint32_t		cl_id;
	time_t			cl_expiration;	/* when to gc */
	enum svc_rpc_gss_client_state cl_state;	/* client state */
	bool_t			cl_locked;	/* fixed service+qop */
	gss_ctx_id_t		cl_ctx;		/* context id */
	gss_cred_id_t		cl_creds;	/* delegated creds */
	gss_name_t		cl_cname;	/* client name */
	struct svc_rpc_gss_svc_name *cl_sname;	/* server name used */
	rpc_gss_rawcred_t	cl_rawcred;	/* raw credentials */
	rpc_gss_ucred_t		cl_ucred;	/* unix-style credentials */
	bool_t			cl_done_callback; /* TRUE after call */
	void			*cl_cookie;	/* user cookie from callback */
	gid_t			cl_gid_storage[NGRPS];
	gss_OID			cl_mech;	/* mechanism */
	gss_qop_t		cl_qop;		/* quality of protection */
	u_int			cl_seq;		/* current sequence number */
	u_int			cl_win;		/* sequence window size */
	u_int			cl_seqlast;	/* sequence window origin */
	uint32_t		cl_seqmask[SVC_RPC_GSS_SEQWINDOW/32]; /* bitmask of seqnums */
	gss_buffer_desc		cl_verf;	/* buffer for verf checksum */
};
TAILQ_HEAD(svc_rpc_gss_client_list, svc_rpc_gss_client);

#define CLIENT_HASH_SIZE	256
#define CLIENT_MAX		128
static struct svc_rpc_gss_client_list svc_rpc_gss_client_hash[CLIENT_HASH_SIZE];
static struct svc_rpc_gss_client_list svc_rpc_gss_clients;
static size_t svc_rpc_gss_client_count;
static uint32_t svc_rpc_gss_next_clientid = 1;

#ifdef __GNUC__
static void svc_rpc_gss_init(void) __attribute__ ((constructor));
#endif

static void
svc_rpc_gss_init(void)
{
	int i;

	if (!svc_rpc_gss_initialised) {
		for (i = 0; i < CLIENT_HASH_SIZE; i++)
			TAILQ_INIT(&svc_rpc_gss_client_hash[i]);
		TAILQ_INIT(&svc_rpc_gss_clients);
		svc_auth_reg(RPCSEC_GSS, svc_rpc_gss);
		svc_rpc_gss_initialised = TRUE;
	}
}

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
	SLIST_INSERT_HEAD(&svc_rpc_gss_callbacks, scb, cb_link);

	return (TRUE);
}

bool_t
rpc_gss_set_svc_name(const char *principal, const char *mechanism,
    u_int req_time, u_int program, u_int version)
{
	OM_uint32		maj_stat, min_stat;
	struct svc_rpc_gss_svc_name *sname;
	gss_buffer_desc		namebuf;
	gss_name_t		name;
	gss_OID			mech_oid;
	gss_OID_set_desc	oid_set;
	gss_cred_id_t		cred;

	svc_rpc_gss_init();

	if (!rpc_gss_mech_to_oid(mechanism, &mech_oid))
		return (FALSE);
	oid_set.count = 1;
	oid_set.elements = mech_oid;

	namebuf.value = (void *)(intptr_t) principal;
	namebuf.length = strlen(principal);

	maj_stat = gss_import_name(&min_stat, &namebuf,
				   GSS_C_NT_HOSTBASED_SERVICE, &name);
	if (maj_stat != GSS_S_COMPLETE)
		return (FALSE);

	maj_stat = gss_acquire_cred(&min_stat, name,
	    req_time, &oid_set, GSS_C_ACCEPT, &cred, NULL, NULL);
	if (maj_stat != GSS_S_COMPLETE)
		return (FALSE);

	gss_release_name(&min_stat, &name);

	sname = malloc(sizeof(struct svc_rpc_gss_svc_name));
	if (!sname)
		return (FALSE);
	sname->sn_principal = strdup(principal);
	sname->sn_mech = mech_oid;
	sname->sn_req_time = req_time;
	sname->sn_cred = cred;
	sname->sn_program = program;
	sname->sn_version = version;
	SLIST_INSERT_HEAD(&svc_rpc_gss_svc_names, sname, sn_link);

	return (TRUE);
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

	svc_rpc_gss_init();

	if (!rpc_gss_mech_to_oid(mech, &mech_oid))
		return (FALSE);

	/*
	 * Construct a gss_buffer containing the full name formatted
	 * as "name/node@domain" where node and domain are optional.
	 */
	namelen = strlen(name);
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
		log_status("gss_import_name", mech_oid, maj_stat, min_stat);
		return (FALSE);
	}
	maj_stat = gss_canonicalize_name(&min_stat, gss_name, mech_oid,
	    &gss_mech_name);
	if (maj_stat != GSS_S_COMPLETE) {
		log_status("gss_canonicalize_name", mech_oid, maj_stat,
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
		log_status("gss_export_name", mech_oid, maj_stat, min_stat);
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
	struct svc_rpc_gss_client *client;

	if (req->rq_cred.oa_flavor != RPCSEC_GSS)
		return (FALSE);

	client = req->rq_clntcred;
	if (rcred)
		*rcred = &client->cl_rawcred;
	if (ucred)
		*ucred = &client->cl_ucred;
	if (cookie)
		*cookie = client->cl_cookie;
	return (TRUE);
}

int
rpc_gss_svc_max_data_length(struct svc_req *req, int max_tp_unit_len)
{
	struct svc_rpc_gss_client *client = req->rq_clntcred;
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
		log_status("gss_wrap_size_limit", client->cl_mech,
		    maj_stat, min_stat);
		return (0);
	}
}

static struct svc_rpc_gss_client *
svc_rpc_gss_find_client(uint32_t clientid)
{
	struct svc_rpc_gss_client *client;
	struct svc_rpc_gss_client_list *list;


	log_debug("in svc_rpc_gss_find_client(%d)", clientid);

	list = &svc_rpc_gss_client_hash[clientid % CLIENT_HASH_SIZE];
	TAILQ_FOREACH(client, list, cl_link) {
		if (client->cl_id == clientid) {
			/*
			 * Move this client to the front of the LRU
			 * list.
			 */
			TAILQ_REMOVE(&svc_rpc_gss_clients, client, cl_alllink);
			TAILQ_INSERT_HEAD(&svc_rpc_gss_clients, client,
			    cl_alllink);
			return client;
		}
	}

	return (NULL);
}

static struct svc_rpc_gss_client *
svc_rpc_gss_create_client(void)
{
	struct svc_rpc_gss_client *client;
	struct svc_rpc_gss_client_list *list;

	log_debug("in svc_rpc_gss_create_client()");

	client = mem_alloc(sizeof(struct svc_rpc_gss_client));
	memset(client, 0, sizeof(struct svc_rpc_gss_client));
	client->cl_id = svc_rpc_gss_next_clientid++;
	list = &svc_rpc_gss_client_hash[client->cl_id % CLIENT_HASH_SIZE];
	TAILQ_INSERT_HEAD(list, client, cl_link);
	TAILQ_INSERT_HEAD(&svc_rpc_gss_clients, client, cl_alllink);

	/*
	 * Start the client off with a short expiration time. We will
	 * try to get a saner value from the client creds later.
	 */
	client->cl_state = CLIENT_NEW;
	client->cl_locked = FALSE;
	client->cl_expiration = time(0) + 5*60;
	svc_rpc_gss_client_count++;

	return (client);
}

static void
svc_rpc_gss_destroy_client(struct svc_rpc_gss_client *client)
{
	struct svc_rpc_gss_client_list *list;
	OM_uint32 min_stat;

	log_debug("in svc_rpc_gss_destroy_client()");

	if (client->cl_ctx)
		gss_delete_sec_context(&min_stat,
		    &client->cl_ctx, GSS_C_NO_BUFFER);

	if (client->cl_cname)
		gss_release_name(&min_stat, &client->cl_cname);

	if (client->cl_rawcred.client_principal)
		mem_free(client->cl_rawcred.client_principal,
		    sizeof(*client->cl_rawcred.client_principal)
		    + client->cl_rawcred.client_principal->len);

	if (client->cl_verf.value)
		gss_release_buffer(&min_stat, &client->cl_verf);

	list = &svc_rpc_gss_client_hash[client->cl_id % CLIENT_HASH_SIZE];
	TAILQ_REMOVE(list, client, cl_link);
	TAILQ_REMOVE(&svc_rpc_gss_clients, client, cl_alllink);
	svc_rpc_gss_client_count--;
	mem_free(client, sizeof(*client));
}

static void
svc_rpc_gss_timeout_clients(void)
{
	struct svc_rpc_gss_client *client;
	struct svc_rpc_gss_client *nclient;
	time_t now = time(0);

	log_debug("in svc_rpc_gss_timeout_clients()");
	/*
	 * First enforce the max client limit. We keep
	 * svc_rpc_gss_clients in LRU order.
	 */
	while (svc_rpc_gss_client_count > CLIENT_MAX)
		svc_rpc_gss_destroy_client(TAILQ_LAST(&svc_rpc_gss_clients,
			    svc_rpc_gss_client_list));
	TAILQ_FOREACH_SAFE(client, &svc_rpc_gss_clients, cl_alllink, nclient) {
		if (client->cl_state == CLIENT_STALE
		    || now > client->cl_expiration) {
			log_debug("expiring client %p", client);
			svc_rpc_gss_destroy_client(client);
		}
	}
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
	char			buf[128];
	uid_t			uid;
	struct passwd		pwd, *pw;
	rpc_gss_ucred_t		*uc = &client->cl_ucred;

	uc->uid = 65534;
	uc->gid = 65534;
	uc->gidlen = 0;
	uc->gidlist = client->cl_gid_storage;

	maj_stat = gss_pname_to_uid(&min_stat, name, client->cl_mech, &uid);
	if (maj_stat != GSS_S_COMPLETE)
		return;

	getpwuid_r(uid, &pwd, buf, sizeof(buf), &pw);
	if (pw) {
		int len = NGRPS;
		uc->uid = pw->pw_uid;
		uc->gid = pw->pw_gid;
		uc->gidlist = client->cl_gid_storage;
		getgrouplist(pw->pw_name, pw->pw_gid, uc->gidlist, &len);
		uc->gidlen = len;
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

	log_debug("in svc_rpc_gss_accept_context()");
	
	/* Deserialize arguments. */
	memset(&recv_tok, 0, sizeof(recv_tok));
	
	if (!svc_getargs(rqst->rq_xprt,
		(xdrproc_t) xdr_gss_buffer_desc,
		(caddr_t) &recv_tok)) {
		client->cl_state = CLIENT_STALE;
		return (FALSE);
	}

	/*
	 * First time round, try all the server names we have until
	 * one matches. Afterwards, stick with that one.
	 */
	if (!client->cl_sname) {
		SLIST_FOREACH(sname, &svc_rpc_gss_svc_names, sn_link) {
			if (sname->sn_program == rqst->rq_prog
			    && sname->sn_version == rqst->rq_vers) {
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
				client->cl_sname = sname;
				break;
			}
		}
		if (!sname) {
			xdr_free((xdrproc_t) xdr_gss_buffer_desc,
			    (char *) &recv_tok);
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
	
	xdr_free((xdrproc_t) xdr_gss_buffer_desc, (char *) &recv_tok);

	/*
	 * If we get an error from gss_accept_sec_context, send the
	 * reply anyway so that the client gets a chance to see what
	 * is wrong.
	 */
	if (gr->gr_major != GSS_S_COMPLETE &&
	    gr->gr_major != GSS_S_CONTINUE_NEEDED) {
		log_status("accept_sec_context", client->cl_mech,
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
	client->cl_seq = gc->gc_seq;
	client->cl_win = gr->gr_win;
	client->cl_done_callback = FALSE;

	if (gr->gr_major == GSS_S_COMPLETE) {
		gss_buffer_desc	export_name;

		/*
		 * Change client expiration time to be near when the
		 * client creds expire (or 24 hours if we can't figure
		 * that out).
		 */
		if (cred_lifetime == GSS_C_INDEFINITE)
			cred_lifetime = time(0) + 24*60*60;

		client->cl_expiration = time(0) + cred_lifetime;

		/*
		 * Fill in cred details in the rawcred structure.
		 */
		client->cl_rawcred.version = RPCSEC_GSS_VERSION;
		rpc_gss_oid_to_mech(mech, &client->cl_rawcred.mechanism);
		maj_stat = gss_export_name(&min_stat, client->cl_cname,
		    &export_name);
		if (maj_stat != GSS_S_COMPLETE) {
			log_status("gss_export_name", client->cl_mech,
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
		gss_release_name(&min_stat, &client->cl_cname);

#ifdef DEBUG
		{
			gss_buffer_desc mechname;

			gss_oid_to_str(&min_stat, mech, &mechname);
			
			log_debug("accepted context for %s with "
			    "<mech %.*s, qop %d, svc %d>",
			    client->cl_rawcred.client_principal->name,
			    mechname.length, (char *)mechname.value,
			    client->cl_qop, client->rawcred.service);

			gss_release_buffer(&min_stat, &mechname);
		}
#endif /* DEBUG */
	}
	return (TRUE);
}

static bool_t
svc_rpc_gss_validate(struct svc_rpc_gss_client *client, struct rpc_msg *msg,
	gss_qop_t *qop)
{
	struct opaque_auth	*oa;
	gss_buffer_desc		 rpcbuf, checksum;
	OM_uint32		 maj_stat, min_stat;
	gss_qop_t		 qop_state;
	int32_t			 rpchdr[128 / sizeof(int32_t)];
	int32_t			*buf;

	log_debug("in svc_rpc_gss_validate()");
	
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
		log_status("gss_verify_mic", client->cl_mech,
		    maj_stat, min_stat);
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
	OM_uint32		maj_stat, min_stat;
	uint32_t		nseq;       

	log_debug("in svc_rpc_gss_nextverf()");

	nseq = htonl(seq);
	signbuf.value = &nseq;
	signbuf.length = sizeof(nseq);

	if (client->cl_verf.value)
		gss_release_buffer(&min_stat, &client->cl_verf);

	maj_stat = gss_get_mic(&min_stat, client->cl_ctx, client->cl_qop,
	    &signbuf, &client->cl_verf);

	if (maj_stat != GSS_S_COMPLETE) {
		log_status("gss_get_mic", client->cl_mech, maj_stat, min_stat);
		client->cl_state = CLIENT_STALE;
		return (FALSE);
	}
	rqst->rq_xprt->xp_verf.oa_flavor = RPCSEC_GSS;
	rqst->rq_xprt->xp_verf.oa_base = (caddr_t)client->cl_verf.value;
	rqst->rq_xprt->xp_verf.oa_length = (u_int)client->cl_verf.length;
	
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

	if (seq <= client->cl_seqlast) {
		/*
		 * The request sequence number is less than
		 * the largest we have seen so far. If it is
		 * outside the window or if we have seen a
		 * request with this sequence before, silently
		 * discard it.
		 */
		offset = client->cl_seqlast - seq;
		if (offset >= SVC_RPC_GSS_SEQWINDOW)
			return (FALSE);
		word = offset / 32;
		bit = offset % 32;
		if (client->cl_seqmask[word] & (1 << bit))
			return (FALSE);
	}

	return (TRUE);
}

static void
svc_rpc_gss_update_seq(struct svc_rpc_gss_client *client, uint32_t seq)
{
	int offset, i, word, bit;
	uint32_t carry, newcarry;
	uint32_t* maskp;

	maskp = client->cl_seqmask;
	if (seq > client->cl_seqlast) {
		/*
		 * This request has a sequence number greater
		 * than any we have seen so far. Advance the
		 * seq window and set bit zero of the window
		 * (which corresponds to the new sequence
		 * number)
		 */
		offset = seq - client->cl_seqlast;
		while (offset >= 32) {
			for (i = (SVC_RPC_GSS_SEQWINDOW / 32) - 1;
			     i > 0; i--) {
				maskp[i] = maskp[i-1];
			}
			maskp[0] = 0;
			offset -= 32;
		}
		if (offset > 0) {
			carry = 0;
			for (i = 0; i < SVC_RPC_GSS_SEQWINDOW / 32; i++) {
				newcarry = maskp[i] >> (32 - offset);
				maskp[i] = (maskp[i] << offset) | carry;
				carry = newcarry;
			}
		}
		maskp[0] |= 1;
		client->cl_seqlast = seq;
	} else {
		offset = client->cl_seqlast - seq;
		word = offset / 32;
		bit = offset % 32;
		maskp[word] |= (1 << bit);
	}

}

enum auth_stat
svc_rpc_gss(struct svc_req *rqst, struct rpc_msg *msg)

{
	OM_uint32		 min_stat;
	XDR	 		 xdrs;
	struct svc_rpc_gss_client *client;
	struct rpc_gss_cred	 gc;
	struct rpc_gss_init_res	 gr;
	gss_qop_t		 qop;
	int			 call_stat;
	enum auth_stat		 result;
	
	log_debug("in svc_rpc_gss()");
	
	/* Garbage collect old clients. */
	svc_rpc_gss_timeout_clients();

	/* Initialize reply. */
	rqst->rq_xprt->xp_verf = _null_auth;

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
	} else {
		if (gc.gc_handle.length != sizeof(uint32_t)) {
			result = AUTH_BADCRED;
			goto out;
		}
		uint32_t *p = gc.gc_handle.value;
		client = svc_rpc_gss_find_client(*p);
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
	rqst->rq_clntcred = client;

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
		client->cl_seq = gc.gc_seq;

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
			if (!svc_rpc_gss_nextverf(client, rqst, gr.gr_win)) {
				result = AUTH_REJECTEDCRED;
				break;
			}
		} else {
			rqst->rq_xprt->xp_verf.oa_flavor = AUTH_NULL;
			rqst->rq_xprt->xp_verf.oa_length = 0;
		}
		
		call_stat = svc_sendreply(rqst->rq_xprt,
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

		if (!svc_rpc_gss_validate(client, msg, &qop)) {
			result = RPCSEC_GSS_CREDPROBLEM;
			break;
		}
		
		if (!svc_rpc_gss_nextverf(client, rqst, gc.gc_seq)) {
			result = RPCSEC_GSS_CTXPROBLEM;
			break;
		}

		svc_rpc_gss_update_seq(client, gc.gc_seq);

		/*
		 * Change the SVCAUTH ops on the transport to point at
		 * our own code so that we can unwrap the arguments
		 * and wrap the result. The caller will re-set this on
		 * every request to point to a set of null wrap/unwrap
		 * methods.
		 */
		SVC_AUTH(rqst->rq_xprt).svc_ah_ops = &svc_auth_gss_ops;
		SVC_AUTH(rqst->rq_xprt).svc_ah_private = client;

		if (gc.gc_proc == RPCSEC_GSS_DATA) {
			/*
			 * We might be ready to do a callback to the server to
			 * see if it wants to accept/reject the connection.
			 */
			if (!client->cl_done_callback) {
				client->cl_done_callback = TRUE;
				client->cl_qop = qop;
				client->cl_rawcred.qop = _rpc_gss_num_to_qop(
					client->cl_rawcred.mechanism, qop);
				if (!svc_rpc_gss_callback(client, rqst)) {
					result = AUTH_REJECTEDCRED;
					break;
				}
			}

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
			client->cl_rawcred.service = gc.gc_svc;

			result = AUTH_OK;
		} else {
			if (rqst->rq_proc != NULLPROC) {
				result = AUTH_REJECTEDCRED;
				break;
			}

			call_stat = svc_sendreply(rqst->rq_xprt,
			    (xdrproc_t) xdr_void, (caddr_t) NULL);

			if (!call_stat) {
				result = AUTH_FAILED;
				break;
			}

			svc_rpc_gss_destroy_client(client);

			result = RPCSEC_GSS_NODISPATCH;
			break;
		}
		break;

	default:
		result = AUTH_BADCRED;
		break;
	}
out:
	xdr_free((xdrproc_t) xdr_rpc_gss_cred, (char *) &gc);
	return (result);
}

bool_t
svc_rpc_gss_wrap(SVCAUTH *auth, XDR *xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr)
{
	struct svc_rpc_gss_client *client;
	
	log_debug("in svc_rpc_gss_wrap()");

	client = (struct svc_rpc_gss_client *) auth->svc_ah_private;
	if (client->cl_state != CLIENT_ESTABLISHED
	    || client->cl_rawcred.service == rpc_gss_svc_none) {
		return xdr_func(xdrs, xdr_ptr);
	}
	return (xdr_rpc_gss_wrap_data(xdrs, xdr_func, xdr_ptr,
		client->cl_ctx, client->cl_qop,
		client->cl_rawcred.service, client->cl_seq));
}

bool_t
svc_rpc_gss_unwrap(SVCAUTH *auth, XDR *xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr)
{
	struct svc_rpc_gss_client *client;

	log_debug("in svc_rpc_gss_unwrap()");
	
	client = (struct svc_rpc_gss_client *) auth->svc_ah_private;
	if (client->cl_state != CLIENT_ESTABLISHED
	    || client->cl_rawcred.service == rpc_gss_svc_none) {
		return xdr_func(xdrs, xdr_ptr);
	}
	return (xdr_rpc_gss_unwrap_data(xdrs, xdr_func, xdr_ptr,
		client->cl_ctx, client->cl_qop,
		client->cl_rawcred.service, client->cl_seq));
}
