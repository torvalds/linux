/*-
 * SPDX-License-Identifier: BSD-3-Clause
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
  auth_gss.c

  RPCSEC_GSS client routines.
  
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

  $Id: auth_gss.c,v 1.32 2002/01/15 15:43:00 andros Exp $
*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/hash.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/refcount.h>
#include <sys/sx.h>
#include <sys/ucred.h>

#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>

#include <kgssapi/krb5/kcrypto.h>

#include "rpcsec_gss_int.h"

static void	rpc_gss_nextverf(AUTH*);
static bool_t	rpc_gss_marshal(AUTH *, uint32_t, XDR *, struct mbuf *);
static bool_t	rpc_gss_init(AUTH *auth, rpc_gss_options_ret_t *options_ret);
static bool_t	rpc_gss_refresh(AUTH *, void *);
static bool_t	rpc_gss_validate(AUTH *, uint32_t, struct opaque_auth *,
    struct mbuf **);
static void	rpc_gss_destroy(AUTH *);
static void	rpc_gss_destroy_context(AUTH *, bool_t);

static struct auth_ops rpc_gss_ops = {
	rpc_gss_nextverf,
	rpc_gss_marshal,
	rpc_gss_validate,
	rpc_gss_refresh,
	rpc_gss_destroy,
};

enum rpcsec_gss_state {
	RPCSEC_GSS_START,
	RPCSEC_GSS_CONTEXT,
	RPCSEC_GSS_ESTABLISHED,
	RPCSEC_GSS_DESTROYING
};

struct rpc_pending_request {
	uint32_t		pr_xid;		/* XID of rpc */
	uint32_t		pr_seq;		/* matching GSS seq */
	LIST_ENTRY(rpc_pending_request) pr_link;
};
LIST_HEAD(rpc_pending_request_list, rpc_pending_request);

struct rpc_gss_data {
	volatile u_int		gd_refs;	/* number of current users */
	struct mtx		gd_lock;
	uint32_t		gd_hash;
	AUTH			*gd_auth;	/* link back to AUTH */
	struct ucred		*gd_ucred;	/* matching local cred */
	char			*gd_principal;	/* server principal name */
	char			*gd_clntprincipal; /* client principal name */
	rpc_gss_options_req_t	gd_options;	/* GSS context options */
	enum rpcsec_gss_state	gd_state;	/* connection state */
	gss_buffer_desc		gd_verf;	/* save GSS_S_COMPLETE
						 * NULL RPC verfier to
						 * process at end of
						 * context negotiation */
	CLIENT			*gd_clnt;	/* client handle */
	gss_OID			gd_mech;	/* mechanism to use */
	gss_qop_t		gd_qop;		/* quality of protection */
	gss_ctx_id_t		gd_ctx;		/* context id */
	struct rpc_gss_cred	gd_cred;	/* client credentials */
	uint32_t		gd_seq;		/* next sequence number */
	u_int			gd_win;		/* sequence window */
	struct rpc_pending_request_list gd_reqs;
	TAILQ_ENTRY(rpc_gss_data) gd_link;
	TAILQ_ENTRY(rpc_gss_data) gd_alllink;
};
TAILQ_HEAD(rpc_gss_data_list, rpc_gss_data);

#define	AUTH_PRIVATE(auth)	((struct rpc_gss_data *)auth->ah_private)

static struct timeval AUTH_TIMEOUT = { 25, 0 };

#define RPC_GSS_HASH_SIZE	11
#define RPC_GSS_MAX		256
static struct rpc_gss_data_list rpc_gss_cache[RPC_GSS_HASH_SIZE];
static struct rpc_gss_data_list rpc_gss_all;
static struct sx rpc_gss_lock;
static int rpc_gss_count;

static AUTH *rpc_gss_seccreate_int(CLIENT *, struct ucred *, const char *,
    const char *, gss_OID, rpc_gss_service_t, u_int, rpc_gss_options_req_t *,
    rpc_gss_options_ret_t *);

static void
rpc_gss_hashinit(void *dummy)
{
	int i;

	for (i = 0; i < RPC_GSS_HASH_SIZE; i++)
		TAILQ_INIT(&rpc_gss_cache[i]);
	TAILQ_INIT(&rpc_gss_all);
	sx_init(&rpc_gss_lock, "rpc_gss_lock");
}
SYSINIT(rpc_gss_hashinit, SI_SUB_KMEM, SI_ORDER_ANY, rpc_gss_hashinit, NULL);

static uint32_t
rpc_gss_hash(const char *principal, gss_OID mech,
    struct ucred *cred, rpc_gss_service_t service)
{
	uint32_t h;

	h = HASHSTEP(HASHINIT, cred->cr_uid);
	h = hash32_str(principal, h);
	h = hash32_buf(mech->elements, mech->length, h);
	h = HASHSTEP(h, (int) service);

	return (h % RPC_GSS_HASH_SIZE);
}

/*
 * Simplified interface to create a security association for the
 * current thread's * ucred.
 */
AUTH *
rpc_gss_secfind(CLIENT *clnt, struct ucred *cred, const char *principal,
    gss_OID mech_oid, rpc_gss_service_t service)
{
	uint32_t		h, th;
	AUTH			*auth;
	struct rpc_gss_data	*gd, *tgd;
	rpc_gss_options_ret_t	options;

	if (rpc_gss_count > RPC_GSS_MAX) {
		while (rpc_gss_count > RPC_GSS_MAX) {
			sx_xlock(&rpc_gss_lock);
			tgd = TAILQ_FIRST(&rpc_gss_all);
			th = tgd->gd_hash;
			TAILQ_REMOVE(&rpc_gss_cache[th], tgd, gd_link);
			TAILQ_REMOVE(&rpc_gss_all, tgd, gd_alllink);
			rpc_gss_count--;
			sx_xunlock(&rpc_gss_lock);
			AUTH_DESTROY(tgd->gd_auth);
		}
	}

	/*
	 * See if we already have an AUTH which matches.
	 */
	h = rpc_gss_hash(principal, mech_oid, cred, service);

again:
	sx_slock(&rpc_gss_lock);
	TAILQ_FOREACH(gd, &rpc_gss_cache[h], gd_link) {
		if (gd->gd_ucred->cr_uid == cred->cr_uid
		    && !strcmp(gd->gd_principal, principal)
		    && gd->gd_mech == mech_oid
		    && gd->gd_cred.gc_svc == service) {
			refcount_acquire(&gd->gd_refs);
			if (sx_try_upgrade(&rpc_gss_lock)) {
				/*
				 * Keep rpc_gss_all LRU sorted.
				 */
				TAILQ_REMOVE(&rpc_gss_all, gd, gd_alllink);
				TAILQ_INSERT_TAIL(&rpc_gss_all, gd,
				    gd_alllink);
				sx_xunlock(&rpc_gss_lock);
			} else {
				sx_sunlock(&rpc_gss_lock);
			}

			/*
			 * If the state != ESTABLISHED, try and initialize
			 * the authenticator again. This will happen if the
			 * user's credentials have expired. It may succeed now,
			 * if they have done a kinit or similar.
			 */
			if (gd->gd_state != RPCSEC_GSS_ESTABLISHED) {
				memset(&options, 0, sizeof (options));
				(void) rpc_gss_init(gd->gd_auth, &options);
			}
			return (gd->gd_auth);
		}
	}
	sx_sunlock(&rpc_gss_lock);

	/*
	 * We missed in the cache - create a new association.
	 */
	auth = rpc_gss_seccreate_int(clnt, cred, NULL, principal, mech_oid,
	    service, GSS_C_QOP_DEFAULT, NULL, NULL);
	if (!auth)
		return (NULL);

	gd = AUTH_PRIVATE(auth);
	gd->gd_hash = h;
	
	sx_xlock(&rpc_gss_lock);
	TAILQ_FOREACH(tgd, &rpc_gss_cache[h], gd_link) {
		if (tgd->gd_ucred->cr_uid == cred->cr_uid
		    && !strcmp(tgd->gd_principal, principal)
		    && tgd->gd_mech == mech_oid
		    && tgd->gd_cred.gc_svc == service) {
			/*
			 * We lost a race to create the AUTH that
			 * matches this cred.
			 */
			sx_xunlock(&rpc_gss_lock);
			AUTH_DESTROY(auth);
			goto again;
		}
	}

	rpc_gss_count++;
	TAILQ_INSERT_TAIL(&rpc_gss_cache[h], gd, gd_link);
	TAILQ_INSERT_TAIL(&rpc_gss_all, gd, gd_alllink);
	refcount_acquire(&gd->gd_refs);	/* one for the cache, one for user */
	sx_xunlock(&rpc_gss_lock);

	return (auth);
}

void
rpc_gss_secpurge(CLIENT *clnt)
{
	uint32_t		h;
	struct rpc_gss_data	*gd, *tgd;

	TAILQ_FOREACH_SAFE(gd, &rpc_gss_all, gd_alllink, tgd) {
		if (gd->gd_clnt == clnt) {
			sx_xlock(&rpc_gss_lock);
			h = gd->gd_hash;
			TAILQ_REMOVE(&rpc_gss_cache[h], gd, gd_link);
			TAILQ_REMOVE(&rpc_gss_all, gd, gd_alllink);
			rpc_gss_count--;
			sx_xunlock(&rpc_gss_lock);
			AUTH_DESTROY(gd->gd_auth);
		}
	}
}

AUTH *
rpc_gss_seccreate(CLIENT *clnt, struct ucred *cred, const char *clnt_principal,
    const char *principal, const char *mechanism, rpc_gss_service_t service,
    const char *qop, rpc_gss_options_req_t *options_req,
    rpc_gss_options_ret_t *options_ret)
{
	gss_OID			oid;
	u_int			qop_num;

	/*
	 * Bail out now if we don't know this mechanism.
	 */
	if (!rpc_gss_mech_to_oid(mechanism, &oid))
		return (NULL);

	if (qop) {
		if (!rpc_gss_qop_to_num(qop, mechanism, &qop_num))
			return (NULL);
	} else {
		qop_num = GSS_C_QOP_DEFAULT;
	}

	return (rpc_gss_seccreate_int(clnt, cred, clnt_principal, principal,
		oid, service, qop_num, options_req, options_ret));
}

void
rpc_gss_refresh_auth(AUTH *auth)
{
	struct rpc_gss_data	*gd;
	rpc_gss_options_ret_t	options;

	gd = AUTH_PRIVATE(auth);
	/*
	 * If the state != ESTABLISHED, try and initialize
	 * the authenticator again. This will happen if the
	 * user's credentials have expired. It may succeed now,
	 * if they have done a kinit or similar.
	 */
	if (gd->gd_state != RPCSEC_GSS_ESTABLISHED) {
		memset(&options, 0, sizeof (options));
		(void) rpc_gss_init(auth, &options);
	}
}

static AUTH *
rpc_gss_seccreate_int(CLIENT *clnt, struct ucred *cred,
    const char *clnt_principal, const char *principal, gss_OID mech_oid,
    rpc_gss_service_t service, u_int qop_num,
    rpc_gss_options_req_t *options_req, rpc_gss_options_ret_t *options_ret)
{
	AUTH			*auth;
	rpc_gss_options_ret_t	options;
	struct rpc_gss_data	*gd;

	/*
	 * If the caller doesn't want the options, point at local
	 * storage to simplify the code below.
	 */
	if (!options_ret)
		options_ret = &options;

	/*
	 * Default service is integrity.
	 */
	if (service == rpc_gss_svc_default)
		service = rpc_gss_svc_integrity;

	memset(options_ret, 0, sizeof(*options_ret));

	rpc_gss_log_debug("in rpc_gss_seccreate()");
	
	memset(&rpc_createerr, 0, sizeof(rpc_createerr));
	
	auth = mem_alloc(sizeof(*auth));
	if (auth == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = ENOMEM;
		return (NULL);
	}
	gd = mem_alloc(sizeof(*gd));
	if (gd == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = ENOMEM;
		mem_free(auth, sizeof(*auth));
		return (NULL);
	}

	auth->ah_ops = &rpc_gss_ops;
	auth->ah_private = (caddr_t) gd;
	auth->ah_cred.oa_flavor = RPCSEC_GSS;
	
	refcount_init(&gd->gd_refs, 1);
	mtx_init(&gd->gd_lock, "gd->gd_lock", NULL, MTX_DEF);
	gd->gd_auth = auth;
	gd->gd_ucred = crdup(cred);
	gd->gd_principal = strdup(principal, M_RPC);
	if (clnt_principal != NULL)
		gd->gd_clntprincipal = strdup(clnt_principal, M_RPC);
	else
		gd->gd_clntprincipal = NULL;


	if (options_req) {
		gd->gd_options = *options_req;
	} else {
		gd->gd_options.req_flags = GSS_C_MUTUAL_FLAG;
		gd->gd_options.time_req = 0;
		gd->gd_options.my_cred = GSS_C_NO_CREDENTIAL;
		gd->gd_options.input_channel_bindings = NULL;
	}
	CLNT_ACQUIRE(clnt);
	gd->gd_clnt = clnt;
	gd->gd_ctx = GSS_C_NO_CONTEXT;
	gd->gd_mech = mech_oid;
	gd->gd_qop = qop_num;

	gd->gd_cred.gc_version = RPCSEC_GSS_VERSION;
	gd->gd_cred.gc_proc = RPCSEC_GSS_INIT;
	gd->gd_cred.gc_seq = 0;
	gd->gd_cred.gc_svc = service;
	LIST_INIT(&gd->gd_reqs);
	
	if (!rpc_gss_init(auth, options_ret)) {
		goto bad;
	}
	
	return (auth);

 bad:
	AUTH_DESTROY(auth);
	return (NULL);
}

bool_t
rpc_gss_set_defaults(AUTH *auth, rpc_gss_service_t service, const char *qop)
{
	struct rpc_gss_data	*gd;
	u_int			qop_num;
	const char		*mechanism;

	gd = AUTH_PRIVATE(auth);
	if (!rpc_gss_oid_to_mech(gd->gd_mech, &mechanism)) {
		return (FALSE);
	}

	if (qop) {
		if (!rpc_gss_qop_to_num(qop, mechanism, &qop_num)) {
			return (FALSE);
		}
	} else {
		qop_num = GSS_C_QOP_DEFAULT;
	}

	gd->gd_cred.gc_svc = service;
	gd->gd_qop = qop_num;
	return (TRUE);
}

static void
rpc_gss_purge_xid(struct rpc_gss_data *gd, uint32_t xid)
{
	struct rpc_pending_request *pr, *npr;
	struct rpc_pending_request_list reqs;

	LIST_INIT(&reqs);
	mtx_lock(&gd->gd_lock);
	LIST_FOREACH_SAFE(pr, &gd->gd_reqs, pr_link, npr) {
		if (pr->pr_xid == xid) {
			LIST_REMOVE(pr, pr_link);
			LIST_INSERT_HEAD(&reqs, pr, pr_link);
		}
	}

	mtx_unlock(&gd->gd_lock);

	LIST_FOREACH_SAFE(pr, &reqs, pr_link, npr) {
		mem_free(pr, sizeof(*pr));
	}
}

static uint32_t
rpc_gss_alloc_seq(struct rpc_gss_data *gd)
{
	uint32_t seq;

	mtx_lock(&gd->gd_lock);
	seq = gd->gd_seq;
	gd->gd_seq++;
	mtx_unlock(&gd->gd_lock);

	return (seq);
}

static void
rpc_gss_nextverf(__unused AUTH *auth)
{

	/* not used */
}

static bool_t
rpc_gss_marshal(AUTH *auth, uint32_t xid, XDR *xdrs, struct mbuf *args)
{
	struct rpc_gss_data	*gd;
	struct rpc_pending_request *pr;
	uint32_t		 seq;
	XDR			 tmpxdrs;
	struct rpc_gss_cred	 gsscred;
	char			 credbuf[MAX_AUTH_BYTES];
	struct opaque_auth	 creds, verf;
	gss_buffer_desc		 rpcbuf, checksum;
	OM_uint32		 maj_stat, min_stat;
	bool_t			 xdr_stat;

	rpc_gss_log_debug("in rpc_gss_marshal()");

	gd = AUTH_PRIVATE(auth);
	
	gsscred = gd->gd_cred;
	seq = rpc_gss_alloc_seq(gd);
	gsscred.gc_seq = seq;

	xdrmem_create(&tmpxdrs, credbuf, sizeof(credbuf), XDR_ENCODE);
	if (!xdr_rpc_gss_cred(&tmpxdrs, &gsscred)) {
		XDR_DESTROY(&tmpxdrs);
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
		return (FALSE);
	}
	creds.oa_flavor = RPCSEC_GSS;
	creds.oa_base = credbuf;
	creds.oa_length = XDR_GETPOS(&tmpxdrs);
	XDR_DESTROY(&tmpxdrs);

	xdr_opaque_auth(xdrs, &creds);

	if (gd->gd_cred.gc_proc == RPCSEC_GSS_INIT ||
	    gd->gd_cred.gc_proc == RPCSEC_GSS_CONTINUE_INIT) {
		if (!xdr_opaque_auth(xdrs, &_null_auth)) {
			_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
			return (FALSE);
		}
		xdrmbuf_append(xdrs, args);
		return (TRUE);
	} else {
		/*
		 * Keep track of this XID + seq pair so that we can do
		 * the matching gss_verify_mic in AUTH_VALIDATE.
		 */
		pr = mem_alloc(sizeof(struct rpc_pending_request));
		mtx_lock(&gd->gd_lock);
		pr->pr_xid = xid;
		pr->pr_seq = seq;
		LIST_INSERT_HEAD(&gd->gd_reqs, pr, pr_link);
		mtx_unlock(&gd->gd_lock);

		/*
		 * Checksum serialized RPC header, up to and including
		 * credential. For the in-kernel environment, we
		 * assume that our XDR stream is on a contiguous
		 * memory buffer (e.g. an mbuf).
		 */
		rpcbuf.length = XDR_GETPOS(xdrs);
		XDR_SETPOS(xdrs, 0);
		rpcbuf.value = XDR_INLINE(xdrs, rpcbuf.length);

		maj_stat = gss_get_mic(&min_stat, gd->gd_ctx, gd->gd_qop,
		    &rpcbuf, &checksum);

		if (maj_stat != GSS_S_COMPLETE) {
			rpc_gss_log_status("gss_get_mic", gd->gd_mech,
			    maj_stat, min_stat);
			if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
				rpc_gss_destroy_context(auth, TRUE);
			}
			_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, EPERM);
			return (FALSE);
		}

		verf.oa_flavor = RPCSEC_GSS;
		verf.oa_base = checksum.value;
		verf.oa_length = checksum.length;

		xdr_stat = xdr_opaque_auth(xdrs, &verf);
		gss_release_buffer(&min_stat, &checksum);
		if (!xdr_stat) {
			_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
			return (FALSE);
		}
		if (gd->gd_state != RPCSEC_GSS_ESTABLISHED ||
		    gd->gd_cred.gc_svc == rpc_gss_svc_none) {
			xdrmbuf_append(xdrs, args);
			return (TRUE);
		} else {
			if (!xdr_rpc_gss_wrap_data(&args,
				gd->gd_ctx, gd->gd_qop, gd->gd_cred.gc_svc,
				seq))
				return (FALSE);
			xdrmbuf_append(xdrs, args);
			return (TRUE);
		}
	}

	return (TRUE);
}

static bool_t
rpc_gss_validate(AUTH *auth, uint32_t xid, struct opaque_auth *verf,
    struct mbuf **resultsp)
{
	struct rpc_gss_data	*gd;
	struct rpc_pending_request *pr, *npr;
	struct rpc_pending_request_list reqs;
	gss_qop_t		qop_state;
	uint32_t		num, seq;
	gss_buffer_desc		signbuf, checksum;
	OM_uint32		maj_stat, min_stat;

	rpc_gss_log_debug("in rpc_gss_validate()");
	
	gd = AUTH_PRIVATE(auth);

	/*
	 * The client will call us with a NULL verf when it gives up
	 * on an XID.
	 */
	if (!verf) {
		rpc_gss_purge_xid(gd, xid);
		return (TRUE);
	}

	if (gd->gd_state == RPCSEC_GSS_CONTEXT) {
		/*
		 * Save the on the wire verifier to validate last INIT
		 * phase packet after decode if the major status is
		 * GSS_S_COMPLETE.
		 */
		if (gd->gd_verf.value)
			xdr_free((xdrproc_t) xdr_gss_buffer_desc,
			    (char *) &gd->gd_verf);
		gd->gd_verf.value = mem_alloc(verf->oa_length);
		if (gd->gd_verf.value == NULL) {
			printf("gss_validate: out of memory\n");
			_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
			m_freem(*resultsp);
			*resultsp = NULL;
			return (FALSE);
		}
		memcpy(gd->gd_verf.value, verf->oa_base, verf->oa_length);
		gd->gd_verf.length = verf->oa_length;

		return (TRUE);
	}

	/*
	 * We need to check the verifier against all the requests
	 * we've send for this XID - for unreliable protocols, we
	 * retransmit with the same XID but different sequence
	 * number. We temporarily take this set of requests out of the
	 * list so that we can work through the list without having to
	 * hold the lock.
	 */
	mtx_lock(&gd->gd_lock);
	LIST_INIT(&reqs);
	LIST_FOREACH_SAFE(pr, &gd->gd_reqs, pr_link, npr) {
		if (pr->pr_xid == xid) {
			LIST_REMOVE(pr, pr_link);
			LIST_INSERT_HEAD(&reqs, pr, pr_link);
		}
	}
	mtx_unlock(&gd->gd_lock);
	LIST_FOREACH(pr, &reqs, pr_link) {
		if (pr->pr_xid == xid) {
			seq = pr->pr_seq;
			num = htonl(seq);
			signbuf.value = &num;
			signbuf.length = sizeof(num);
	
			checksum.value = verf->oa_base;
			checksum.length = verf->oa_length;
	
			maj_stat = gss_verify_mic(&min_stat, gd->gd_ctx,
			    &signbuf, &checksum, &qop_state);
			if (maj_stat != GSS_S_COMPLETE
			    || qop_state != gd->gd_qop) {
				continue;
			}
			if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
				rpc_gss_destroy_context(auth, TRUE);
				break;
			}
			//rpc_gss_purge_reqs(gd, seq);
			LIST_FOREACH_SAFE(pr, &reqs, pr_link, npr)
				mem_free(pr, sizeof(*pr));

			if (gd->gd_cred.gc_svc == rpc_gss_svc_none) {
				return (TRUE);
			} else {
				if (!xdr_rpc_gss_unwrap_data(resultsp,
					gd->gd_ctx, gd->gd_qop,
					gd->gd_cred.gc_svc, seq)) {
					return (FALSE);
				}
			}
			return (TRUE);
		}
	}

	/*
	 * We didn't match - put back any entries for this XID so that
	 * a future call to validate can retry.
	 */
	mtx_lock(&gd->gd_lock);
	LIST_FOREACH_SAFE(pr, &reqs, pr_link, npr) {
		LIST_REMOVE(pr, pr_link);
		LIST_INSERT_HEAD(&gd->gd_reqs, pr, pr_link);
	}
	mtx_unlock(&gd->gd_lock);

	/*
	 * Nothing matches - give up.
	 */
	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, EPERM);
	m_freem(*resultsp);
	*resultsp = NULL;
	return (FALSE);
}

static bool_t
rpc_gss_init(AUTH *auth, rpc_gss_options_ret_t *options_ret)
{
	struct thread		*td = curthread;
	struct ucred		*crsave;
	struct rpc_gss_data	*gd;
	struct rpc_gss_init_res	 gr;
	gss_buffer_desc		principal_desc;
	gss_buffer_desc		*recv_tokenp, recv_token, send_token;
	gss_name_t		name;
	OM_uint32		 maj_stat, min_stat, call_stat;
	const char		*mech;
	struct rpc_callextra	 ext;
	gss_OID			mech_oid;
	gss_OID_set		mechlist;

	rpc_gss_log_debug("in rpc_gss_refresh()");
	
	gd = AUTH_PRIVATE(auth);
	
	mtx_lock(&gd->gd_lock);
	/*
	 * If the context isn't in START state, someone else is
	 * refreshing - we wait till they are done. If they fail, they
	 * will put the state back to START and we can try (most
	 * likely to also fail).
	 */
	while (gd->gd_state != RPCSEC_GSS_START
	    && gd->gd_state != RPCSEC_GSS_ESTABLISHED) {
		msleep(gd, &gd->gd_lock, 0, "gssstate", 0);
	}
	if (gd->gd_state == RPCSEC_GSS_ESTABLISHED) {
		mtx_unlock(&gd->gd_lock);
		return (TRUE);
	}
	gd->gd_state = RPCSEC_GSS_CONTEXT;
	mtx_unlock(&gd->gd_lock);

	gd->gd_cred.gc_proc = RPCSEC_GSS_INIT;
	gd->gd_cred.gc_seq = 0;

	/*
	 * For KerberosV, if there is a client principal name, that implies
	 * that this is a host based initiator credential in the default
	 * keytab file. For this case, it is necessary to do a
	 * gss_acquire_cred(). When this is done, the gssd daemon will
	 * do the equivalent of "kinit -k" to put a TGT for the name in
	 * the credential cache file for the gssd daemon.
	 */
	if (gd->gd_clntprincipal != NULL &&
	    rpc_gss_mech_to_oid("kerberosv5", &mech_oid) &&
	    gd->gd_mech == mech_oid) {
		/* Get rid of any old credential. */
		if (gd->gd_options.my_cred != GSS_C_NO_CREDENTIAL) {
			gss_release_cred(&min_stat, &gd->gd_options.my_cred);
			gd->gd_options.my_cred = GSS_C_NO_CREDENTIAL;
		}
	
		/*
		 * The mechanism must be set to KerberosV for acquisition
		 * of credentials to work reliably.
		 */
		maj_stat = gss_create_empty_oid_set(&min_stat, &mechlist);
		if (maj_stat != GSS_S_COMPLETE) {
			options_ret->major_status = maj_stat;
			options_ret->minor_status = min_stat;
			goto out;
		}
		maj_stat = gss_add_oid_set_member(&min_stat, gd->gd_mech,
		    &mechlist);
		if (maj_stat != GSS_S_COMPLETE) {
			options_ret->major_status = maj_stat;
			options_ret->minor_status = min_stat;
			gss_release_oid_set(&min_stat, &mechlist);
			goto out;
		}
	
		principal_desc.value = (void *)gd->gd_clntprincipal;
		principal_desc.length = strlen(gd->gd_clntprincipal);
		maj_stat = gss_import_name(&min_stat, &principal_desc,
		    GSS_C_NT_HOSTBASED_SERVICE, &name);
		if (maj_stat != GSS_S_COMPLETE) {
			options_ret->major_status = maj_stat;
			options_ret->minor_status = min_stat;
			gss_release_oid_set(&min_stat, &mechlist);
			goto out;
		}
		/* Acquire the credentials. */
		maj_stat = gss_acquire_cred(&min_stat, name, 0,
		    mechlist, GSS_C_INITIATE,
		    &gd->gd_options.my_cred, NULL, NULL);
		gss_release_name(&min_stat, &name);
		gss_release_oid_set(&min_stat, &mechlist);
		if (maj_stat != GSS_S_COMPLETE) {
			options_ret->major_status = maj_stat;
			options_ret->minor_status = min_stat;
			goto out;
		}
	}

	principal_desc.value = (void *)gd->gd_principal;
	principal_desc.length = strlen(gd->gd_principal);
	maj_stat = gss_import_name(&min_stat, &principal_desc,
	    GSS_C_NT_HOSTBASED_SERVICE, &name);
	if (maj_stat != GSS_S_COMPLETE) {
		options_ret->major_status = maj_stat;
		options_ret->minor_status = min_stat;
		goto out;
	}

	/* GSS context establishment loop. */
	memset(&recv_token, 0, sizeof(recv_token));
	memset(&gr, 0, sizeof(gr));
	memset(options_ret, 0, sizeof(*options_ret));
	options_ret->major_status = GSS_S_FAILURE;
	recv_tokenp = GSS_C_NO_BUFFER;
	
	for (;;) {
		crsave = td->td_ucred;
		td->td_ucred = gd->gd_ucred;
		maj_stat = gss_init_sec_context(&min_stat,
		    gd->gd_options.my_cred,
		    &gd->gd_ctx,
		    name,
		    gd->gd_mech,
		    gd->gd_options.req_flags,
		    gd->gd_options.time_req,
		    gd->gd_options.input_channel_bindings,
		    recv_tokenp,
		    &gd->gd_mech,	/* used mech */
		    &send_token,
		    &options_ret->ret_flags,
		    &options_ret->time_req);
		td->td_ucred = crsave;
		
		/*
		 * Free the token which we got from the server (if
		 * any).  Remember that this was allocated by XDR, not
		 * GSS-API.
		 */
		if (recv_tokenp != GSS_C_NO_BUFFER) {
			xdr_free((xdrproc_t) xdr_gss_buffer_desc,
			    (char *) &recv_token);
			recv_tokenp = GSS_C_NO_BUFFER;
		}
		if (gd->gd_mech && rpc_gss_oid_to_mech(gd->gd_mech, &mech)) {
			strlcpy(options_ret->actual_mechanism,
			    mech,
			    sizeof(options_ret->actual_mechanism));
		}
		if (maj_stat != GSS_S_COMPLETE &&
		    maj_stat != GSS_S_CONTINUE_NEEDED) {
			rpc_gss_log_status("gss_init_sec_context", gd->gd_mech,
			    maj_stat, min_stat);
			options_ret->major_status = maj_stat;
			options_ret->minor_status = min_stat;
			break;
		}
		if (send_token.length != 0) {
			memset(&gr, 0, sizeof(gr));
			
			bzero(&ext, sizeof(ext));
			ext.rc_auth = auth;
			call_stat = CLNT_CALL_EXT(gd->gd_clnt, &ext, NULLPROC,
			    (xdrproc_t)xdr_gss_buffer_desc,
			    &send_token,
			    (xdrproc_t)xdr_rpc_gss_init_res,
			    (caddr_t)&gr, AUTH_TIMEOUT);
			
			gss_release_buffer(&min_stat, &send_token);
			
			if (call_stat != RPC_SUCCESS)
				break;

			if (gr.gr_major != GSS_S_COMPLETE &&
			    gr.gr_major != GSS_S_CONTINUE_NEEDED) {
				rpc_gss_log_status("server reply", gd->gd_mech,
				    gr.gr_major, gr.gr_minor);
				options_ret->major_status = gr.gr_major;
				options_ret->minor_status = gr.gr_minor;
				break;
			}
			
			/*
			 * Save the server's gr_handle value, freeing
			 * what we have already (remember that this
			 * was allocated by XDR, not GSS-API).
			 */
			if (gr.gr_handle.length != 0) {
				xdr_free((xdrproc_t) xdr_gss_buffer_desc,
				    (char *) &gd->gd_cred.gc_handle);
				gd->gd_cred.gc_handle = gr.gr_handle;
			}

			/*
			 * Save the server's token as well.
			 */
			if (gr.gr_token.length != 0) {
				recv_token = gr.gr_token;
				recv_tokenp = &recv_token;
			}

			/*
			 * Since we have copied out all the bits of gr
			 * which XDR allocated for us, we don't need
			 * to free it.
			 */
			gd->gd_cred.gc_proc = RPCSEC_GSS_CONTINUE_INIT;
		}

		if (maj_stat == GSS_S_COMPLETE) {
			gss_buffer_desc   bufin;
			u_int seq, qop_state = 0;

			/* 
			 * gss header verifier,
			 * usually checked in gss_validate
			 */
			seq = htonl(gr.gr_win);
			bufin.value = (unsigned char *)&seq;
			bufin.length = sizeof(seq);

			maj_stat = gss_verify_mic(&min_stat, gd->gd_ctx,
			    &bufin, &gd->gd_verf, &qop_state);

			if (maj_stat != GSS_S_COMPLETE ||
			    qop_state != gd->gd_qop) {
				rpc_gss_log_status("gss_verify_mic", gd->gd_mech,
				    maj_stat, min_stat);
				if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
					rpc_gss_destroy_context(auth, TRUE);
				}
				_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR,
				    EPERM);
				options_ret->major_status = maj_stat;
				options_ret->minor_status = min_stat;
				break;
			}

			options_ret->major_status = GSS_S_COMPLETE;
			options_ret->minor_status = 0;
			options_ret->rpcsec_version = gd->gd_cred.gc_version;
			options_ret->gss_context = gd->gd_ctx;

			gd->gd_cred.gc_proc = RPCSEC_GSS_DATA;
			gd->gd_seq = 1;
			gd->gd_win = gr.gr_win;
			break;
		}
	}

	gss_release_name(&min_stat, &name);
	xdr_free((xdrproc_t) xdr_gss_buffer_desc,
	    (char *) &gd->gd_verf);

out:
	/* End context negotiation loop. */
	if (gd->gd_cred.gc_proc != RPCSEC_GSS_DATA) {
		rpc_createerr.cf_stat = RPC_AUTHERROR;
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, EPERM);
		if (gd->gd_ctx) {
			gss_delete_sec_context(&min_stat, &gd->gd_ctx,
				GSS_C_NO_BUFFER);
		}
		mtx_lock(&gd->gd_lock);
		gd->gd_state = RPCSEC_GSS_START;
		wakeup(gd);
		mtx_unlock(&gd->gd_lock);
		return (FALSE);
	}
	
	mtx_lock(&gd->gd_lock);
	gd->gd_state = RPCSEC_GSS_ESTABLISHED;
	wakeup(gd);
	mtx_unlock(&gd->gd_lock);

	return (TRUE);
}

static bool_t
rpc_gss_refresh(AUTH *auth, void *msg)
{
	struct rpc_msg *reply = (struct rpc_msg *) msg;
	rpc_gss_options_ret_t options;
	struct rpc_gss_data *gd;

	gd = AUTH_PRIVATE(auth);
	
	/*
	 * If the context is in DESTROYING state, then just return, since
	 * there is no point in refreshing the credentials.
	 */
	mtx_lock(&gd->gd_lock);
	if (gd->gd_state == RPCSEC_GSS_DESTROYING) {
		mtx_unlock(&gd->gd_lock);
		return (FALSE);
	}
	mtx_unlock(&gd->gd_lock);

	/*
	 * If the error was RPCSEC_GSS_CREDPROBLEM of
	 * RPCSEC_GSS_CTXPROBLEM we start again from scratch. All
	 * other errors are fatal.
	 */
	if (reply->rm_reply.rp_stat == MSG_DENIED
	    && reply->rm_reply.rp_rjct.rj_stat == AUTH_ERROR
	    && (reply->rm_reply.rp_rjct.rj_why == RPCSEC_GSS_CREDPROBLEM
		|| reply->rm_reply.rp_rjct.rj_why == RPCSEC_GSS_CTXPROBLEM)) {
		rpc_gss_destroy_context(auth, FALSE);
		memset(&options, 0, sizeof(options));
		return (rpc_gss_init(auth, &options));
	}

	return (FALSE);
}

static void
rpc_gss_destroy_context(AUTH *auth, bool_t send_destroy)
{
	struct rpc_gss_data	*gd;
	struct rpc_pending_request *pr;
	OM_uint32		 min_stat;
	struct rpc_callextra	 ext;

	rpc_gss_log_debug("in rpc_gss_destroy_context()");
	
	gd = AUTH_PRIVATE(auth);
	
	mtx_lock(&gd->gd_lock);
	/*
	 * If the context isn't in ESTABISHED state, someone else is
	 * destroying/refreshing - we wait till they are done.
	 */
	if (gd->gd_state != RPCSEC_GSS_ESTABLISHED) {
		while (gd->gd_state != RPCSEC_GSS_START
		    && gd->gd_state != RPCSEC_GSS_ESTABLISHED)
			msleep(gd, &gd->gd_lock, 0, "gssstate", 0);
		mtx_unlock(&gd->gd_lock);
		return;
	}
	gd->gd_state = RPCSEC_GSS_DESTROYING;
	mtx_unlock(&gd->gd_lock);

	if (send_destroy) {
		gd->gd_cred.gc_proc = RPCSEC_GSS_DESTROY;
		bzero(&ext, sizeof(ext));
		ext.rc_auth = auth;
		CLNT_CALL_EXT(gd->gd_clnt, &ext, NULLPROC,
		    (xdrproc_t)xdr_void, NULL,
		    (xdrproc_t)xdr_void, NULL, AUTH_TIMEOUT);
	}

	while ((pr = LIST_FIRST(&gd->gd_reqs)) != NULL) {
		LIST_REMOVE(pr, pr_link);
		mem_free(pr, sizeof(*pr));
	}

	/*
	 * Free the context token. Remember that this was
	 * allocated by XDR, not GSS-API.
	 */
	xdr_free((xdrproc_t) xdr_gss_buffer_desc,
	    (char *) &gd->gd_cred.gc_handle);
	gd->gd_cred.gc_handle.length = 0;

	if (gd->gd_ctx != GSS_C_NO_CONTEXT)
		gss_delete_sec_context(&min_stat, &gd->gd_ctx, NULL);

	mtx_lock(&gd->gd_lock);
	gd->gd_state = RPCSEC_GSS_START;
	wakeup(gd);
	mtx_unlock(&gd->gd_lock);
}

static void
rpc_gss_destroy(AUTH *auth)
{
	struct rpc_gss_data	*gd;
	
	rpc_gss_log_debug("in rpc_gss_destroy()");
	
	gd = AUTH_PRIVATE(auth);
	
	if (!refcount_release(&gd->gd_refs))
		return;

	rpc_gss_destroy_context(auth, TRUE);
	
	CLNT_RELEASE(gd->gd_clnt);
	crfree(gd->gd_ucred);
	free(gd->gd_principal, M_RPC);
	if (gd->gd_clntprincipal != NULL)
		free(gd->gd_clntprincipal, M_RPC);
	if (gd->gd_verf.value)
		xdr_free((xdrproc_t) xdr_gss_buffer_desc,
		    (char *) &gd->gd_verf);
	mtx_destroy(&gd->gd_lock);

	mem_free(gd, sizeof(*gd));
	mem_free(auth, sizeof(*auth));
}

int
rpc_gss_max_data_length(AUTH *auth, int max_tp_unit_len)
{
	struct rpc_gss_data	*gd;
	int			want_conf;
	OM_uint32		max;
	OM_uint32		maj_stat, min_stat;
	int			result;

	gd = AUTH_PRIVATE(auth);

	switch (gd->gd_cred.gc_svc) {
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

	maj_stat = gss_wrap_size_limit(&min_stat, gd->gd_ctx, want_conf,
	    gd->gd_qop, max_tp_unit_len, &max);

	if (maj_stat == GSS_S_COMPLETE) {
		result = (int) max;
		if (result < 0)
			result = 0;
		return (result);
	} else {
		rpc_gss_log_status("gss_wrap_size_limit", gd->gd_mech,
		    maj_stat, min_stat);
		return (0);
	}
}
