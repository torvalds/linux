/*	$NetBSD: rpc_prot.c,v 1.16 2000/06/02 23:11:13 fvdl Exp $	*/

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

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid2 = "@(#)rpc_prot.c 1.36 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)rpc_prot.c	2.3 88/08/07 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rpc_prot.c
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * This set of routines implements the rpc message definition,
 * its serializer and some common rpc utility routines.
 * The routines are meant for various implementations of rpc -
 * they are NOT for the rpc client or rpc service implementations!
 * Because authentication stuff is easy and is part of rpc, the opaque
 * routines are also in this program.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>

MALLOC_DEFINE(M_RPC, "rpc", "Remote Procedure Call");

#define assert(exp)	KASSERT(exp, ("bad arguments"))

static enum clnt_stat accepted(enum accept_stat, struct rpc_err *);
static enum clnt_stat rejected(enum reject_stat, struct rpc_err *);

/* * * * * * * * * * * * * * XDR Authentication * * * * * * * * * * * */

struct opaque_auth _null_auth;

/*
 * XDR an opaque authentication struct
 * (see auth.h)
 */
bool_t
xdr_opaque_auth(XDR *xdrs, struct opaque_auth *ap)
{

	assert(xdrs != NULL);
	assert(ap != NULL);

	if (xdr_enum(xdrs, &(ap->oa_flavor)))
		return (xdr_bytes(xdrs, &ap->oa_base,
			&ap->oa_length, MAX_AUTH_BYTES));
	return (FALSE);
}

/* * * * * * * * * * * * * * XDR RPC MESSAGE * * * * * * * * * * * * * * * */

/*
 * XDR the MSG_ACCEPTED part of a reply message union
 */
bool_t
xdr_accepted_reply(XDR *xdrs, struct accepted_reply *ar)
{
	enum accept_stat *par_stat;

	assert(xdrs != NULL);
	assert(ar != NULL);

	par_stat = &ar->ar_stat;

	/* personalized union, rather than calling xdr_union */
	if (! xdr_opaque_auth(xdrs, &(ar->ar_verf)))
		return (FALSE);
	if (! xdr_enum(xdrs, (enum_t *) par_stat))
		return (FALSE);
	switch (ar->ar_stat) {

	case SUCCESS:
		if (ar->ar_results.proc != (xdrproc_t) xdr_void)
			return ((*(ar->ar_results.proc))(xdrs,
				ar->ar_results.where));
		else
			return (TRUE);

	case PROG_MISMATCH:
		if (! xdr_uint32_t(xdrs, &(ar->ar_vers.low)))
			return (FALSE);
		return (xdr_uint32_t(xdrs, &(ar->ar_vers.high)));

	case GARBAGE_ARGS:
	case SYSTEM_ERR:
	case PROC_UNAVAIL:
	case PROG_UNAVAIL:
		break;
	}
	return (TRUE);  /* TRUE => open ended set of problems */
}

/*
 * XDR the MSG_DENIED part of a reply message union
 */
bool_t 
xdr_rejected_reply(XDR *xdrs, struct rejected_reply *rr)
{
	enum reject_stat *prj_stat;
	enum auth_stat *prj_why;

	assert(xdrs != NULL);
	assert(rr != NULL);

	prj_stat = &rr->rj_stat;

	/* personalized union, rather than calling xdr_union */
	if (! xdr_enum(xdrs, (enum_t *) prj_stat))
		return (FALSE);
	switch (rr->rj_stat) {

	case RPC_MISMATCH:
		if (! xdr_uint32_t(xdrs, &(rr->rj_vers.low)))
			return (FALSE);
		return (xdr_uint32_t(xdrs, &(rr->rj_vers.high)));

	case AUTH_ERROR:
		prj_why = &rr->rj_why;
		return (xdr_enum(xdrs, (enum_t *) prj_why));
	}
	/* NOTREACHED */
	assert(0);
	return (FALSE);
}

static const struct xdr_discrim reply_dscrm[3] = {
	{ (int)MSG_ACCEPTED, (xdrproc_t)xdr_accepted_reply },
	{ (int)MSG_DENIED, (xdrproc_t)xdr_rejected_reply },
	{ __dontcare__, NULL_xdrproc_t } };

/*
 * XDR a reply message
 */
bool_t
xdr_replymsg(XDR *xdrs, struct rpc_msg *rmsg)
{
	int32_t *buf;
	enum msg_type *prm_direction;
	enum reply_stat *prp_stat;

	assert(xdrs != NULL);
	assert(rmsg != NULL);

	if (xdrs->x_op == XDR_DECODE) {
		buf = XDR_INLINE(xdrs, 3 * BYTES_PER_XDR_UNIT);
		if (buf != NULL) {
			rmsg->rm_xid = IXDR_GET_UINT32(buf);
			rmsg->rm_direction = IXDR_GET_ENUM(buf, enum msg_type);
			if (rmsg->rm_direction != REPLY) {
				return (FALSE);
			}
			rmsg->rm_reply.rp_stat =
				IXDR_GET_ENUM(buf, enum reply_stat);
			if (rmsg->rm_reply.rp_stat == MSG_ACCEPTED)
				return (xdr_accepted_reply(xdrs,
					&rmsg->acpted_rply));
			else if (rmsg->rm_reply.rp_stat == MSG_DENIED)
				return (xdr_rejected_reply(xdrs,
					&rmsg->rjcted_rply));
			else
				return (FALSE);
		}
	}

	prm_direction = &rmsg->rm_direction;
	prp_stat = &rmsg->rm_reply.rp_stat;

	if (
	    xdr_uint32_t(xdrs, &(rmsg->rm_xid)) && 
	    xdr_enum(xdrs, (enum_t *) prm_direction) &&
	    (rmsg->rm_direction == REPLY) )
		return (xdr_union(xdrs, (enum_t *) prp_stat,
		   (caddr_t)(void *)&(rmsg->rm_reply.ru), reply_dscrm,
		   NULL_xdrproc_t));
	return (FALSE);
}


/*
 * Serializes the "static part" of a call message header.
 * The fields include: rm_xid, rm_direction, rpcvers, prog, and vers.
 * The rm_xid is not really static, but the user can easily munge on the fly.
 */
bool_t
xdr_callhdr(XDR *xdrs, struct rpc_msg *cmsg)
{
	enum msg_type *prm_direction;

	assert(xdrs != NULL);
	assert(cmsg != NULL);

	prm_direction = &cmsg->rm_direction;

	cmsg->rm_direction = CALL;
	cmsg->rm_call.cb_rpcvers = RPC_MSG_VERSION;
	if (
	    (xdrs->x_op == XDR_ENCODE) &&
	    xdr_uint32_t(xdrs, &(cmsg->rm_xid)) &&
	    xdr_enum(xdrs, (enum_t *) prm_direction) &&
	    xdr_uint32_t(xdrs, &(cmsg->rm_call.cb_rpcvers)) &&
	    xdr_uint32_t(xdrs, &(cmsg->rm_call.cb_prog)) )
		return (xdr_uint32_t(xdrs, &(cmsg->rm_call.cb_vers)));
	return (FALSE);
}

/* ************************** Client utility routine ************* */

static enum clnt_stat
accepted(enum accept_stat acpt_stat, struct rpc_err *error)
{

	assert(error != NULL);

	switch (acpt_stat) {

	case PROG_UNAVAIL:
		error->re_status = RPC_PROGUNAVAIL;
		return (RPC_PROGUNAVAIL);

	case PROG_MISMATCH:
		error->re_status = RPC_PROGVERSMISMATCH;
		return (RPC_PROGVERSMISMATCH);

	case PROC_UNAVAIL:
		return (RPC_PROCUNAVAIL);

	case GARBAGE_ARGS:
		return (RPC_CANTDECODEARGS);

	case SYSTEM_ERR:
		return (RPC_SYSTEMERROR);

	case SUCCESS:
		return (RPC_SUCCESS);
	}
	/* NOTREACHED */
	/* something's wrong, but we don't know what ... */
	error->re_lb.s1 = (int32_t)MSG_ACCEPTED;
	error->re_lb.s2 = (int32_t)acpt_stat;
	return (RPC_FAILED);
}

static enum clnt_stat
rejected(enum reject_stat rjct_stat, struct rpc_err *error)
{

	assert(error != NULL);

	switch (rjct_stat) {
	case RPC_MISMATCH:
		return (RPC_VERSMISMATCH);

	case AUTH_ERROR:
		return (RPC_AUTHERROR);
	}
	/* something's wrong, but we don't know what ... */
	/* NOTREACHED */
	error->re_lb.s1 = (int32_t)MSG_DENIED;
	error->re_lb.s2 = (int32_t)rjct_stat;
	return (RPC_FAILED);
}

/*
 * given a reply message, fills in the error
 */
enum clnt_stat
_seterr_reply(struct rpc_msg *msg, struct rpc_err *error)
{
	enum clnt_stat stat;

	assert(msg != NULL);
	assert(error != NULL);

	/* optimized for normal, SUCCESSful case */
	switch (msg->rm_reply.rp_stat) {

	case MSG_ACCEPTED:
		if (msg->acpted_rply.ar_stat == SUCCESS) {
			stat = RPC_SUCCESS;
			return (stat);
		}
		stat = accepted(msg->acpted_rply.ar_stat, error);
		break;

	case MSG_DENIED:
		stat = rejected(msg->rjcted_rply.rj_stat, error);
		break;

	default:
		stat = RPC_FAILED;
		error->re_lb.s1 = (int32_t)(msg->rm_reply.rp_stat);
		break;
	}
	error->re_status = stat;

	switch (stat) {

	case RPC_VERSMISMATCH:
		error->re_vers.low = msg->rjcted_rply.rj_vers.low;
		error->re_vers.high = msg->rjcted_rply.rj_vers.high;
		break;

	case RPC_AUTHERROR:
		error->re_why = msg->rjcted_rply.rj_why;
		break;

	case RPC_PROGVERSMISMATCH:
		error->re_vers.low = msg->acpted_rply.ar_vers.low;
		error->re_vers.high = msg->acpted_rply.ar_vers.high;
		break;

	case RPC_FAILED:
	case RPC_SUCCESS:
	case RPC_PROGNOTREGISTERED:
	case RPC_PMAPFAILURE:
	case RPC_UNKNOWNPROTO:
	case RPC_UNKNOWNHOST:
	case RPC_SYSTEMERROR:
	case RPC_CANTDECODEARGS:
	case RPC_PROCUNAVAIL:
	case RPC_PROGUNAVAIL:
	case RPC_TIMEDOUT:
	case RPC_CANTRECV:
	case RPC_CANTSEND:
	case RPC_CANTDECODERES:
	case RPC_CANTENCODEARGS:
	default:
		break;
	}

	return (stat);
}
