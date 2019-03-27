/*	$NetBSD: svc_auth.c,v 1.12 2000/07/06 03:10:35 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc. 
 */

#if defined(LIBC_SCCS) && !defined(lint)
#ident	"@(#)svc_auth.c	1.16	94/04/24 SMI"
static char sccsid[] = "@(#)svc_auth.c 1.26 89/02/07 Copyr 1984 Sun Micro";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * svc_auth.c, Server-side rpc authenticator interface.
 *
 */

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/ucred.h>

#include <rpc/rpc.h>

static enum auth_stat (*_svcauth_rpcsec_gss)(struct svc_req *,
    struct rpc_msg *) = NULL;
static int (*_svcauth_rpcsec_gss_getcred)(struct svc_req *,
    struct ucred **, int *);

static struct svc_auth_ops svc_auth_null_ops;

/*
 * The call rpc message, msg has been obtained from the wire.  The msg contains
 * the raw form of credentials and verifiers.  authenticate returns AUTH_OK
 * if the msg is successfully authenticated.  If AUTH_OK then the routine also
 * does the following things:
 * set rqst->rq_xprt->verf to the appropriate response verifier;
 * sets rqst->rq_client_cred to the "cooked" form of the credentials.
 *
 * NB: rqst->rq_cxprt->verf must be pre-alloctaed;
 * its length is set appropriately.
 *
 * The caller still owns and is responsible for msg->u.cmb.cred and
 * msg->u.cmb.verf.  The authentication system retains ownership of
 * rqst->rq_client_cred, the cooked credentials.
 *
 * There is an assumption that any flavour less than AUTH_NULL is
 * invalid.
 */
enum auth_stat
_authenticate(struct svc_req *rqst, struct rpc_msg *msg)
{
	int cred_flavor;
	enum auth_stat dummy;

	rqst->rq_cred = msg->rm_call.cb_cred;
	rqst->rq_auth.svc_ah_ops = &svc_auth_null_ops;
	rqst->rq_auth.svc_ah_private = NULL;
	cred_flavor = rqst->rq_cred.oa_flavor;
	switch (cred_flavor) {
	case AUTH_NULL:
		dummy = _svcauth_null(rqst, msg);
		return (dummy);
	case AUTH_SYS:
		dummy = _svcauth_unix(rqst, msg);
		return (dummy);
	case AUTH_SHORT:
		dummy = _svcauth_short(rqst, msg);
		return (dummy);
	case RPCSEC_GSS:
		if (!_svcauth_rpcsec_gss)
			return (AUTH_REJECTEDCRED);
		dummy = _svcauth_rpcsec_gss(rqst, msg);
		return (dummy);
	default:
		break;
	}

	return (AUTH_REJECTEDCRED);
}

/*
 * A set of null auth methods used by any authentication protocols
 * that don't need to inspect or modify the message body.
 */
static bool_t
svcauth_null_wrap(SVCAUTH *auth, struct mbuf **mp)
{

	return (TRUE);
}

static bool_t
svcauth_null_unwrap(SVCAUTH *auth, struct mbuf **mp)
{

	return (TRUE);
}

static void
svcauth_null_release(SVCAUTH *auth)
{

}

static struct svc_auth_ops svc_auth_null_ops = {
	svcauth_null_wrap,
	svcauth_null_unwrap,
	svcauth_null_release,
};

/*ARGSUSED*/
enum auth_stat
_svcauth_null(struct svc_req *rqst, struct rpc_msg *msg)
{

	rqst->rq_verf = _null_auth;
	return (AUTH_OK);
}

int
svc_auth_reg(int flavor,
    enum auth_stat (*svcauth)(struct svc_req *, struct rpc_msg *),
    int (*getcred)(struct svc_req *, struct ucred **, int *))
{

	if (flavor == RPCSEC_GSS) {
		_svcauth_rpcsec_gss = svcauth;
		_svcauth_rpcsec_gss_getcred = getcred;
	}
	return (TRUE);
}

int
svc_getcred(struct svc_req *rqst, struct ucred **crp, int *flavorp)
{
	struct ucred *cr = NULL;
	int flavor;
	struct xucred *xcr;

	flavor = rqst->rq_cred.oa_flavor;
	if (flavorp)
		*flavorp = flavor;

	switch (flavor) {
	case AUTH_UNIX:
		xcr = (struct xucred *) rqst->rq_clntcred;
		cr = crget();
		cr->cr_uid = cr->cr_ruid = cr->cr_svuid = xcr->cr_uid;
		crsetgroups(cr, xcr->cr_ngroups, xcr->cr_groups);
		cr->cr_rgid = cr->cr_svgid = cr->cr_groups[0];
		cr->cr_prison = &prison0;
		prison_hold(cr->cr_prison);
		*crp = cr;
		return (TRUE);

	case RPCSEC_GSS:
		if (!_svcauth_rpcsec_gss_getcred)
			return (FALSE);
		return (_svcauth_rpcsec_gss_getcred(rqst, crp, flavorp));

	default:
		return (FALSE);
	}
}

