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
/*
  SPDX-License-Identifier: BSD-3-Clause

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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>
#include "rpcsec_gss_int.h"

static void	rpc_gss_nextverf(AUTH*);
static bool_t	rpc_gss_marshal(AUTH *, XDR *);
static bool_t	rpc_gss_init(AUTH *auth, rpc_gss_options_ret_t *options_ret);
static bool_t	rpc_gss_refresh(AUTH *, void *);
static bool_t	rpc_gss_validate(AUTH *, struct opaque_auth *);
static void	rpc_gss_destroy(AUTH *);
static void	rpc_gss_destroy_context(AUTH *, bool_t);

static struct auth_ops rpc_gss_ops = {
	rpc_gss_nextverf,
	rpc_gss_marshal,
	rpc_gss_validate,
	rpc_gss_refresh,
	rpc_gss_destroy
};

enum rpcsec_gss_state {
	RPCSEC_GSS_START,
	RPCSEC_GSS_CONTEXT,
	RPCSEC_GSS_ESTABLISHED
};

struct rpc_gss_data {
	rpc_gss_options_req_t	gd_options;	/* GSS context options */
	enum rpcsec_gss_state	gd_state;	/* connection state */
	gss_buffer_desc		gd_verf;	/* save GSS_S_COMPLETE
						 * NULL RPC verfier to
						 * process at end of
						 * context negotiation */
	CLIENT			*gd_clnt;	/* client handle */
	gss_name_t		gd_name;	/* service name */
	gss_OID			gd_mech;	/* mechanism to use */
	gss_qop_t		gd_qop;		/* quality of protection */
	gss_ctx_id_t		gd_ctx;		/* context id */
	struct rpc_gss_cred	gd_cred;	/* client credentials */
	u_int			gd_win;		/* sequence window */
};

#define	AUTH_PRIVATE(auth)	((struct rpc_gss_data *)auth->ah_private)

static struct timeval AUTH_TIMEOUT = { 25, 0 };

AUTH *
rpc_gss_seccreate(CLIENT *clnt, const char *principal,
    const char *mechanism, rpc_gss_service_t service, const char *qop,
    rpc_gss_options_req_t *options_req, rpc_gss_options_ret_t *options_ret)
{
	AUTH			*auth, *save_auth;
	rpc_gss_options_ret_t	options;
	gss_OID			oid;
	u_int			qop_num;
	struct rpc_gss_data	*gd;
	OM_uint32		maj_stat = 0, min_stat = 0;
	gss_buffer_desc		principal_desc;

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

	log_debug("in rpc_gss_seccreate()");
	
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
		free(auth);
		return (NULL);
	}

	auth->ah_ops = &rpc_gss_ops;
	auth->ah_private = (caddr_t) gd;
	auth->ah_cred.oa_flavor = RPCSEC_GSS;
	
	principal_desc.value = (void *)(intptr_t) principal;
	principal_desc.length = strlen(principal);
	maj_stat = gss_import_name(&min_stat, &principal_desc,
	    GSS_C_NT_HOSTBASED_SERVICE, &gd->gd_name);
	if (maj_stat != GSS_S_COMPLETE) {
		options_ret->major_status = maj_stat;
		options_ret->minor_status = min_stat;
		goto bad;
	}

	if (options_req) {
		gd->gd_options = *options_req;
	} else {
		gd->gd_options.req_flags = GSS_C_MUTUAL_FLAG;
		gd->gd_options.time_req = 0;
		gd->gd_options.my_cred = GSS_C_NO_CREDENTIAL;
		gd->gd_options.input_channel_bindings = NULL;
	}
	gd->gd_clnt = clnt;
	gd->gd_ctx = GSS_C_NO_CONTEXT;
	gd->gd_mech = oid;
	gd->gd_qop = qop_num;

	gd->gd_cred.gc_version = RPCSEC_GSS_VERSION;
	gd->gd_cred.gc_proc = RPCSEC_GSS_INIT;
	gd->gd_cred.gc_seq = 0;
	gd->gd_cred.gc_svc = service;
	
	save_auth = clnt->cl_auth;

	clnt->cl_auth = auth;
	if (!rpc_gss_init(auth, options_ret)) {
		clnt->cl_auth = save_auth;
		goto bad;
	}
	
	clnt->cl_auth = save_auth;
	
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
rpc_gss_nextverf(__unused AUTH *auth)
{

	/* not used */
}

static bool_t
rpc_gss_marshal(__unused AUTH *auth, __unused XDR *xdrs)
{

	/* not used */
	return (FALSE);
}

static bool_t
rpc_gss_validate(AUTH *auth, struct opaque_auth *verf)
{
	struct rpc_gss_data	*gd;
	gss_qop_t		qop_state;
	uint32_t		num;
	gss_buffer_desc		signbuf, checksum;
	OM_uint32		maj_stat, min_stat;

	log_debug("in rpc_gss_validate()");
	
	gd = AUTH_PRIVATE(auth);

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
			fprintf(stderr, "gss_validate: out of memory\n");
			_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
			return (FALSE);
		}
		memcpy(gd->gd_verf.value, verf->oa_base, verf->oa_length);
		gd->gd_verf.length = verf->oa_length;
		return (TRUE);
	}

	num = htonl(gd->gd_cred.gc_seq);
	signbuf.value = &num;
	signbuf.length = sizeof(num);
	
	checksum.value = verf->oa_base;
	checksum.length = verf->oa_length;
	
	maj_stat = gss_verify_mic(&min_stat, gd->gd_ctx, &signbuf,
	    &checksum, &qop_state);
	if (maj_stat != GSS_S_COMPLETE || qop_state != gd->gd_qop) {
		log_status("gss_verify_mic", gd->gd_mech, maj_stat, min_stat);
		if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
			rpc_gss_destroy_context(auth, TRUE);
		}
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, EPERM);
		return (FALSE);
	}
	return (TRUE);
}

static bool_t
rpc_gss_init(AUTH *auth, rpc_gss_options_ret_t *options_ret)
{
	struct rpc_gss_data	*gd;
	struct rpc_gss_init_res	 gr;
	gss_buffer_desc		*recv_tokenp, recv_token, send_token;
	OM_uint32		 maj_stat, min_stat, call_stat;
	const char		*mech;

	log_debug("in rpc_gss_refresh()");
	
	gd = AUTH_PRIVATE(auth);
	
	if (gd->gd_state != RPCSEC_GSS_START)
		return (TRUE);
	
	/* GSS context establishment loop. */
	gd->gd_state = RPCSEC_GSS_CONTEXT;
	gd->gd_cred.gc_proc = RPCSEC_GSS_INIT;
	gd->gd_cred.gc_seq = 0;

	memset(&recv_token, 0, sizeof(recv_token));
	memset(&gr, 0, sizeof(gr));
	recv_tokenp = GSS_C_NO_BUFFER;
	
	for (;;) {
		maj_stat = gss_init_sec_context(&min_stat,
		    gd->gd_options.my_cred,
		    &gd->gd_ctx,
		    gd->gd_name,
		    gd->gd_mech,
		    gd->gd_options.req_flags,
		    gd->gd_options.time_req,
		    gd->gd_options.input_channel_bindings,
		    recv_tokenp,
		    &gd->gd_mech,	/* used mech */
		    &send_token,
		    &options_ret->ret_flags,
		    &options_ret->time_req);
		
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
		if (maj_stat != GSS_S_COMPLETE &&
		    maj_stat != GSS_S_CONTINUE_NEEDED) {
			log_status("gss_init_sec_context", gd->gd_mech,
			    maj_stat, min_stat);
			options_ret->major_status = maj_stat;
			options_ret->minor_status = min_stat;
			break;
		}
		if (send_token.length != 0) {
			memset(&gr, 0, sizeof(gr));
			
			call_stat = clnt_call(gd->gd_clnt, NULLPROC,
			    (xdrproc_t)xdr_gss_buffer_desc,
			    &send_token,
			    (xdrproc_t)xdr_rpc_gss_init_res,
			    (caddr_t)&gr, AUTH_TIMEOUT);
			
			gss_release_buffer(&min_stat, &send_token);
			
			if (call_stat != RPC_SUCCESS)
				break;

			if (gr.gr_major != GSS_S_COMPLETE &&
			    gr.gr_major != GSS_S_CONTINUE_NEEDED) {
				log_status("server reply", gd->gd_mech,
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
				log_status("gss_verify_mic", gd->gd_mech,
				    maj_stat, min_stat);
				if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
					rpc_gss_destroy_context(auth, TRUE);
				}
				_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR,
				    EPERM);
				options_ret->major_status = maj_stat;
				options_ret->minor_status = min_stat;
				return (FALSE);
			}

			options_ret->major_status = GSS_S_COMPLETE;
			options_ret->minor_status = 0;
			options_ret->rpcsec_version = gd->gd_cred.gc_version;
			options_ret->gss_context = gd->gd_ctx;
			if (rpc_gss_oid_to_mech(gd->gd_mech, &mech)) {
				strlcpy(options_ret->actual_mechanism,
				    mech,
				    sizeof(options_ret->actual_mechanism));
			}

			gd->gd_state = RPCSEC_GSS_ESTABLISHED;
			gd->gd_cred.gc_proc = RPCSEC_GSS_DATA;
			gd->gd_cred.gc_seq = 0;
			gd->gd_win = gr.gr_win;
			break;
		}
	}
	xdr_free((xdrproc_t) xdr_gss_buffer_desc,
	    (char *) &gd->gd_verf);

	/* End context negotiation loop. */
	if (gd->gd_cred.gc_proc != RPCSEC_GSS_DATA) {
		rpc_createerr.cf_stat = RPC_AUTHERROR;
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, EPERM);
		return (FALSE);
	}
	
	return (TRUE);
}

static bool_t
rpc_gss_refresh(AUTH *auth, void *msg)
{
	struct rpc_msg *reply = (struct rpc_msg *) msg;
	rpc_gss_options_ret_t options;

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
	OM_uint32		 min_stat;

	log_debug("in rpc_gss_destroy_context()");
	
	gd = AUTH_PRIVATE(auth);
	
	if (gd->gd_state == RPCSEC_GSS_ESTABLISHED && send_destroy) {
		gd->gd_cred.gc_proc = RPCSEC_GSS_DESTROY;
		clnt_call(gd->gd_clnt, NULLPROC,
		    (xdrproc_t)xdr_void, NULL,
		    (xdrproc_t)xdr_void, NULL, AUTH_TIMEOUT);
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

	gd->gd_state = RPCSEC_GSS_START;
}

static void
rpc_gss_destroy(AUTH *auth)
{
	struct rpc_gss_data	*gd;
	OM_uint32		 min_stat;
	
	log_debug("in rpc_gss_destroy()");
	
	gd = AUTH_PRIVATE(auth);
	
	rpc_gss_destroy_context(auth, TRUE);
	
	if (gd->gd_name != GSS_C_NO_NAME)
		gss_release_name(&min_stat, &gd->gd_name);
	if (gd->gd_verf.value)
		xdr_free((xdrproc_t) xdr_gss_buffer_desc,
		    (char *) &gd->gd_verf);

	mem_free(gd, sizeof(*gd));
	mem_free(auth, sizeof(*auth));
}

bool_t
__rpc_gss_wrap(AUTH *auth, void *header, size_t headerlen,
    XDR* xdrs, xdrproc_t xdr_args, void *args_ptr)
{
	XDR			 tmpxdrs;
	char			 credbuf[MAX_AUTH_BYTES];
	char			 tmpheader[MAX_AUTH_BYTES];
	struct opaque_auth	 creds, verf;
	struct rpc_gss_data	*gd;
	gss_buffer_desc		 rpcbuf, checksum;
	OM_uint32		 maj_stat, min_stat;
	bool_t			 xdr_stat;
	
	log_debug("in rpc_gss_wrap()");
	
	gd = AUTH_PRIVATE(auth);

	if (gd->gd_state == RPCSEC_GSS_ESTABLISHED)
		gd->gd_cred.gc_seq++;
	
	/*
	 * We need to encode our creds and then put the header and
	 * creds together in a buffer so that we can create a checksum
	 * for the verf.
	 */
	xdrmem_create(&tmpxdrs, credbuf, sizeof(credbuf), XDR_ENCODE);
	if (!xdr_rpc_gss_cred(&tmpxdrs, &gd->gd_cred)) {
		XDR_DESTROY(&tmpxdrs);
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
		return (FALSE);
	}
	creds.oa_flavor = RPCSEC_GSS;
	creds.oa_base = credbuf;
	creds.oa_length = XDR_GETPOS(&tmpxdrs);
	XDR_DESTROY(&tmpxdrs);
	
	xdrmem_create(&tmpxdrs, tmpheader, sizeof(tmpheader), XDR_ENCODE);
	if (!XDR_PUTBYTES(&tmpxdrs, header, headerlen) ||
	    !xdr_opaque_auth(&tmpxdrs, &creds)) {
		XDR_DESTROY(&tmpxdrs);
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
		return (FALSE);
	}
	headerlen = XDR_GETPOS(&tmpxdrs);
	XDR_DESTROY(&tmpxdrs);
		
	if (!XDR_PUTBYTES(xdrs, tmpheader, headerlen)) {
		_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
		return (FALSE);
	}
	
	if (gd->gd_cred.gc_proc == RPCSEC_GSS_INIT ||
	    gd->gd_cred.gc_proc == RPCSEC_GSS_CONTINUE_INIT) {
		if (!xdr_opaque_auth(xdrs, &_null_auth)) {
			_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOMEM);
			return (FALSE);
		}
	} else {
		/*
		 * Checksum serialized RPC header, up to and including
		 * credential.
		 */
		rpcbuf.length = headerlen;
		rpcbuf.value = tmpheader;
	
		maj_stat = gss_get_mic(&min_stat, gd->gd_ctx, gd->gd_qop,
		    &rpcbuf, &checksum);

		if (maj_stat != GSS_S_COMPLETE) {
			log_status("gss_get_mic", gd->gd_mech,
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
	}
	
	if (gd->gd_state != RPCSEC_GSS_ESTABLISHED ||
	    gd->gd_cred.gc_svc == rpc_gss_svc_none) {
		return (xdr_args(xdrs, args_ptr));
	}
	return (xdr_rpc_gss_wrap_data(xdrs, xdr_args, args_ptr,
		gd->gd_ctx, gd->gd_qop, gd->gd_cred.gc_svc,
		gd->gd_cred.gc_seq));
}

bool_t
__rpc_gss_unwrap(AUTH *auth, XDR *xdrs, xdrproc_t xdr_func, void *xdr_ptr)
{
	struct rpc_gss_data	*gd;

	log_debug("in rpc_gss_unwrap()");
	
	gd = AUTH_PRIVATE(auth);
	
	if (gd->gd_state != RPCSEC_GSS_ESTABLISHED ||
	    gd->gd_cred.gc_svc == rpc_gss_svc_none) {
		return (xdr_func(xdrs, xdr_ptr));
	}
	return (xdr_rpc_gss_unwrap_data(xdrs, xdr_func, xdr_ptr,
		gd->gd_ctx, gd->gd_qop, gd->gd_cred.gc_svc,
		gd->gd_cred.gc_seq));
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
		log_status("gss_wrap_size_limit", gd->gd_mech,
		    maj_stat, min_stat);
		return (0);
	}
}
