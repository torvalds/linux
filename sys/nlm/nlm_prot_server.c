/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
#ifndef lint
/*static char sccsid[] = "from: @(#)nlm_prot.x 1.8 87/09/21 Copyr 1987 Sun Micro";*/
/*static char sccsid[] = "from: * @(#)nlm_prot.x	2.1 88/08/01 4.0 RPCSRC";*/
__RCSID("$NetBSD: nlm_prot.x,v 1.6 2000/06/07 14:30:15 bouyer Exp $");
#endif /* not lint */
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <nlm/nlm_prot.h>
#include <nlm/nlm.h>

/**********************************************************************/

/*
 * Convert between various versions of the protocol structures.
 */

static void
nlm_convert_to_nlm4_lock(struct nlm4_lock *dst, struct nlm_lock *src)
{

	dst->caller_name = src->caller_name;
	dst->fh = src->fh;
	dst->oh = src->oh;
	dst->svid = src->svid;
	dst->l_offset = src->l_offset;
	dst->l_len = src->l_len;
}

static void
nlm_convert_to_nlm4_share(struct nlm4_share *dst, struct nlm_share *src)
{

	dst->caller_name = src->caller_name;
	dst->fh = src->fh;
	dst->oh = src->oh;
	dst->mode = src->mode;
	dst->access = src->access;
}

static void
nlm_convert_to_nlm_holder(struct nlm_holder *dst, struct nlm4_holder *src)
{

	dst->exclusive = src->exclusive;
	dst->svid = src->svid;
	dst->oh = src->oh;
	dst->l_offset = src->l_offset;
	dst->l_len = src->l_len;
}

static void
nlm_convert_to_nlm4_holder(struct nlm4_holder *dst, struct nlm_holder *src)
{

	dst->exclusive = src->exclusive;
	dst->svid = src->svid;
	dst->oh = src->oh;
	dst->l_offset = src->l_offset;
	dst->l_len = src->l_len;
}

static enum nlm_stats
nlm_convert_to_nlm_stats(enum nlm4_stats src)
{
	if (src > nlm4_deadlck)
		return nlm_denied;
	return (enum nlm_stats) src;
}

static void
nlm_convert_to_nlm_res(struct nlm_res *dst, struct nlm4_res *src)
{
	dst->cookie = src->cookie;
	dst->stat.stat = nlm_convert_to_nlm_stats(src->stat.stat);
}

static void
nlm_convert_to_nlm4_res(struct nlm4_res *dst, struct nlm_res *src)
{
	dst->cookie = src->cookie;
	dst->stat.stat = (enum nlm4_stats) src->stat.stat;
}

/**********************************************************************/

/*
 * RPC server stubs.
 */

bool_t
nlm_sm_notify_0_svc(struct nlm_sm_status *argp, void *result, struct svc_req *rqstp)
{
	nlm_sm_notify(argp);

	return (TRUE);
}

bool_t
nlm_test_1_svc(struct nlm_testargs *argp, nlm_testres *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_testargs args4;
	nlm4_testres res4;

	args4.cookie = argp->cookie;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	retval = nlm4_test_4_svc(&args4, &res4, rqstp);
	if (retval) {
		result->cookie = res4.cookie;
		result->stat.stat = nlm_convert_to_nlm_stats(res4.stat.stat);
		if (result->stat.stat == nlm_denied)
			nlm_convert_to_nlm_holder(
				&result->stat.nlm_testrply_u.holder,
				&res4.stat.nlm4_testrply_u.holder);
	}

	return (retval);
}

bool_t
nlm_lock_1_svc(struct nlm_lockargs *argp, nlm_res *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_lockargs args4;
	nlm4_res res4;

	args4.cookie = argp->cookie;
	args4.block = argp->block;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);
	args4.reclaim = argp->reclaim;
	args4.state = argp->state;

	retval = nlm4_lock_4_svc(&args4, &res4, rqstp);
	if (retval)
		nlm_convert_to_nlm_res(result, &res4);

	return (retval);
}

bool_t
nlm_cancel_1_svc(struct nlm_cancargs *argp, nlm_res *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_cancargs args4;
	nlm4_res res4;

	args4.cookie = argp->cookie;
	args4.block = argp->block;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	retval = nlm4_cancel_4_svc(&args4, &res4, rqstp);
	if (retval)
		nlm_convert_to_nlm_res(result, &res4);

	return (retval);
}

bool_t
nlm_unlock_1_svc(struct nlm_unlockargs *argp, nlm_res *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_unlockargs args4;
	nlm4_res res4;

	args4.cookie = argp->cookie;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	retval = nlm4_unlock_4_svc(&args4, &res4, rqstp);
	if (retval)
		nlm_convert_to_nlm_res(result, &res4);

	return (retval);
}

bool_t
nlm_granted_1_svc(struct nlm_testargs *argp, nlm_res *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_testargs args4;
	nlm4_res res4;

	args4.cookie = argp->cookie;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	retval = nlm4_granted_4_svc(&args4, &res4, rqstp);
	if (retval)
		nlm_convert_to_nlm_res(result, &res4);

	return (retval);
}

bool_t
nlm_test_msg_1_svc(struct nlm_testargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_testargs args4;
	nlm4_testres res4;
	nlm_testres res;
	CLIENT *rpc;
	char dummy;

	args4.cookie = argp->cookie;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	if (nlm_do_test(&args4, &res4, rqstp, &rpc))
		return (FALSE);

	res.cookie = res4.cookie;
	res.stat.stat = nlm_convert_to_nlm_stats(res4.stat.stat);
	if (res.stat.stat == nlm_denied)
		nlm_convert_to_nlm_holder(
			&res.stat.nlm_testrply_u.holder,
			&res4.stat.nlm4_testrply_u.holder);

	if (rpc) {
		nlm_test_res_1(&res, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm_testres, &res);

	return (FALSE);
}

bool_t
nlm_lock_msg_1_svc(struct nlm_lockargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_lockargs args4;
	nlm4_res res4;
	nlm_res res;
	CLIENT *rpc;
	char dummy;

	args4.cookie = argp->cookie;
	args4.block = argp->block;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);
	args4.reclaim = argp->reclaim;
	args4.state = argp->state;

	if (nlm_do_lock(&args4, &res4, rqstp, TRUE, &rpc))
		return (FALSE);

	nlm_convert_to_nlm_res(&res, &res4);

	if (rpc) {
		nlm_lock_res_1(&res, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm_res, &res);

	return (FALSE);
}

bool_t
nlm_cancel_msg_1_svc(struct nlm_cancargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_cancargs args4;
	nlm4_res res4;
	nlm_res res;
	CLIENT *rpc;
	char dummy;

	args4.cookie = argp->cookie;
	args4.block = argp->block;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	if (nlm_do_cancel(&args4, &res4, rqstp, &rpc))
		return (FALSE);

	nlm_convert_to_nlm_res(&res, &res4);

	if (rpc) {
		nlm_cancel_res_1(&res, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm_res, &res);

	return (FALSE);
}

bool_t
nlm_unlock_msg_1_svc(struct nlm_unlockargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_unlockargs args4;
	nlm4_res res4;
	nlm_res res;
	CLIENT *rpc;
	char dummy;

	args4.cookie = argp->cookie;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	if (nlm_do_unlock(&args4, &res4, rqstp, &rpc))
		return (FALSE);

	nlm_convert_to_nlm_res(&res, &res4);

	if (rpc) {
		nlm_unlock_res_1(&res, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm_res, &res);

	return (FALSE);
}

bool_t
nlm_granted_msg_1_svc(struct nlm_testargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_testargs args4;
	nlm4_res res4;
	nlm_res res;
	CLIENT *rpc;
	char dummy;

	args4.cookie = argp->cookie;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);

	if (nlm_do_granted(&args4, &res4, rqstp, &rpc))
		return (FALSE);

	nlm_convert_to_nlm_res(&res, &res4);

	if (rpc) {
		nlm_granted_res_1(&res, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm_res, &res);

	return (FALSE);
}

bool_t
nlm_test_res_1_svc(nlm_testres *argp, void *result, struct svc_req *rqstp)
{
	nlm4_testres args4;

	args4.cookie = argp->cookie;
	if (argp->stat.stat == nlm_denied)
		nlm_convert_to_nlm4_holder(
			&args4.stat.nlm4_testrply_u.holder,
			&argp->stat.nlm_testrply_u.holder);

	return (nlm4_test_res_4_svc(&args4, result, rqstp));
}

bool_t
nlm_lock_res_1_svc(nlm_res *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res arg4;

	nlm_convert_to_nlm4_res(&arg4, argp);
	return (nlm4_lock_res_4_svc(&arg4, result, rqstp));
}

bool_t
nlm_cancel_res_1_svc(nlm_res *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res arg4;

	nlm_convert_to_nlm4_res(&arg4, argp);
	return (nlm4_cancel_res_4_svc(&arg4, result, rqstp));
}

bool_t
nlm_unlock_res_1_svc(nlm_res *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res arg4;

	nlm_convert_to_nlm4_res(&arg4, argp);
	return (nlm4_unlock_res_4_svc(&arg4, result, rqstp));
}

bool_t
nlm_granted_res_1_svc(nlm_res *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res arg4;

	nlm_convert_to_nlm4_res(&arg4, argp);
	return (nlm4_granted_res_4_svc(&arg4, result, rqstp));
}

int
nlm_prog_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{

	(void) xdr_free(xdr_result, result);
	return (TRUE);
}

bool_t
nlm_share_3_svc(nlm_shareargs *argp, nlm_shareres *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_shareargs args4;
	nlm4_shareres res4;

	args4.cookie = argp->cookie;
	nlm_convert_to_nlm4_share(&args4.share, &argp->share);
	args4.reclaim = argp->reclaim;

	retval = nlm4_share_4_svc(&args4, &res4, rqstp);
	if (retval) {
		result->cookie = res4.cookie;
		result->stat = nlm_convert_to_nlm_stats(res4.stat);
		result->sequence = res4.sequence;
	}

	return (retval);
}

bool_t
nlm_unshare_3_svc(nlm_shareargs *argp, nlm_shareres *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_shareargs args4;
	nlm4_shareres res4;

	args4.cookie = argp->cookie;
	nlm_convert_to_nlm4_share(&args4.share, &argp->share);
	args4.reclaim = argp->reclaim;

	retval = nlm4_unshare_4_svc(&args4, &res4, rqstp);
	if (retval) {
		result->cookie = res4.cookie;
		result->stat = nlm_convert_to_nlm_stats(res4.stat);
		result->sequence = res4.sequence;
	}

	return (retval);
}

bool_t
nlm_nm_lock_3_svc(nlm_lockargs *argp, nlm_res *result, struct svc_req *rqstp)
{
	bool_t retval;
	nlm4_lockargs args4;
	nlm4_res res4;

	args4.cookie = argp->cookie;
	args4.block = argp->block;
	args4.exclusive = argp->exclusive;
	nlm_convert_to_nlm4_lock(&args4.alock, &argp->alock);
	args4.reclaim = argp->reclaim;
	args4.state = argp->state;

	retval = nlm4_nm_lock_4_svc(&args4, &res4, rqstp);
	if (retval)
		nlm_convert_to_nlm_res(result, &res4);

	return (retval);
}

bool_t
nlm_free_all_3_svc(nlm_notify *argp, void *result, struct svc_req *rqstp)
{
	struct nlm4_notify args4;

	args4.name = argp->name;
	args4.state = argp->state;

	return (nlm4_free_all_4_svc(&args4, result, rqstp));
}

int
nlm_prog_3_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{

	(void) xdr_free(xdr_result, result);
	return (TRUE);
}

bool_t
nlm4_test_4_svc(nlm4_testargs *argp, nlm4_testres *result, struct svc_req *rqstp)
{

	nlm_do_test(argp, result, rqstp, NULL);
	return (TRUE);
}

bool_t
nlm4_lock_4_svc(nlm4_lockargs *argp, nlm4_res *result, struct svc_req *rqstp)
{

	nlm_do_lock(argp, result, rqstp, TRUE, NULL);
	return (TRUE);
}

bool_t
nlm4_cancel_4_svc(nlm4_cancargs *argp, nlm4_res *result, struct svc_req *rqstp)
{

	nlm_do_cancel(argp, result, rqstp, NULL);
	return (TRUE);
}

bool_t
nlm4_unlock_4_svc(nlm4_unlockargs *argp, nlm4_res *result, struct svc_req *rqstp)
{

	nlm_do_unlock(argp, result, rqstp, NULL);
	return (TRUE);
}

bool_t
nlm4_granted_4_svc(nlm4_testargs *argp, nlm4_res *result, struct svc_req *rqstp)
{

	nlm_do_granted(argp, result, rqstp, NULL);
	return (TRUE);
}

bool_t
nlm4_test_msg_4_svc(nlm4_testargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_testres res4;
	CLIENT *rpc;
	char dummy;

	if (nlm_do_test(argp, &res4, rqstp, &rpc))
		return (FALSE);
	if (rpc) {
		nlm4_test_res_4(&res4, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm4_testres, &res4);

	return (FALSE);
}

bool_t
nlm4_lock_msg_4_svc(nlm4_lockargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res res4;
	CLIENT *rpc;
	char dummy;

	if (nlm_do_lock(argp, &res4, rqstp, TRUE, &rpc))
		return (FALSE);
	if (rpc) {
		nlm4_lock_res_4(&res4, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm4_res, &res4);

	return (FALSE);
}

bool_t
nlm4_cancel_msg_4_svc(nlm4_cancargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res res4;
	CLIENT *rpc;
	char dummy;

	if (nlm_do_cancel(argp, &res4, rqstp, &rpc))
		return (FALSE);
	if (rpc) {
		nlm4_cancel_res_4(&res4, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm4_res, &res4);

	return (FALSE);
}

bool_t
nlm4_unlock_msg_4_svc(nlm4_unlockargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res res4;
	CLIENT *rpc;
	char dummy;

	if (nlm_do_unlock(argp, &res4, rqstp, &rpc))
		return (FALSE);
	if (rpc) {
		nlm4_unlock_res_4(&res4, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm4_res, &res4);

	return (FALSE);
}

bool_t
nlm4_granted_msg_4_svc(nlm4_testargs *argp, void *result, struct svc_req *rqstp)
{
	nlm4_res res4;
	CLIENT *rpc;
	char dummy;

	if (nlm_do_granted(argp, &res4, rqstp, &rpc))
		return (FALSE);
	if (rpc) {
		nlm4_granted_res_4(&res4, &dummy, rpc, NULL, nlm_zero_tv);
		CLNT_RELEASE(rpc);
	}
	xdr_free((xdrproc_t) xdr_nlm4_res, &res4);

	return (FALSE);
}

bool_t
nlm4_test_res_4_svc(nlm4_testres *argp, void *result, struct svc_req *rqstp)
{

	return (FALSE);
}

bool_t
nlm4_lock_res_4_svc(nlm4_res *argp, void *result, struct svc_req *rqstp)
{

	return (FALSE);
}

bool_t
nlm4_cancel_res_4_svc(nlm4_res *argp, void *result, struct svc_req *rqstp)
{

	return (FALSE);
}

bool_t
nlm4_unlock_res_4_svc(nlm4_res *argp, void *result, struct svc_req *rqstp)
{

	return (FALSE);
}

bool_t
nlm4_granted_res_4_svc(nlm4_res *argp, void *result, struct svc_req *rqstp)
{

	nlm_do_granted_res(argp, rqstp);
	return (FALSE);
}

bool_t
nlm4_share_4_svc(nlm4_shareargs *argp, nlm4_shareres *result, struct svc_req *rqstp)
{

	memset(result, 0, sizeof(*result));
	result->stat = nlm4_denied;
	return (TRUE);
}

bool_t
nlm4_unshare_4_svc(nlm4_shareargs *argp, nlm4_shareres *result, struct svc_req *rqstp)
{

	memset(result, 0, sizeof(*result));
	result->stat = nlm4_denied;
	return (TRUE);
}

bool_t
nlm4_nm_lock_4_svc(nlm4_lockargs *argp, nlm4_res *result, struct svc_req *rqstp)
{

	nlm_do_lock(argp, result, rqstp, FALSE, NULL);
	return (TRUE);
}

bool_t
nlm4_free_all_4_svc(nlm4_notify *argp, void *result, struct svc_req *rqstp)
{

	nlm_do_free_all(argp);
	return (TRUE);
}

int
nlm_prog_4_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{

	(void) xdr_free(xdr_result, result);
	return (TRUE);
}
