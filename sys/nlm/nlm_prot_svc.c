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

#include <sys/param.h>
#include <sys/systm.h>

#include <nlm/nlm_prot.h>
#include <nlm/nlm.h>

#include <sys/cdefs.h>
#ifndef lint
/*static char sccsid[] = "from: @(#)nlm_prot.x 1.8 87/09/21 Copyr 1987 Sun Micro";*/
/*static char sccsid[] = "from: * @(#)nlm_prot.x	2.1 88/08/01 4.0 RPCSRC";*/
__RCSID("$NetBSD: nlm_prot.x,v 1.6 2000/06/07 14:30:15 bouyer Exp $");
#endif /* not lint */
__FBSDID("$FreeBSD$");

void nlm_prog_0(struct svc_req *rqstp, SVCXPRT *transp);
void nlm_prog_1(struct svc_req *rqstp, SVCXPRT *transp);
void nlm_prog_3(struct svc_req *rqstp, SVCXPRT *transp);
void nlm_prog_4(struct svc_req *rqstp, SVCXPRT *transp);

void
nlm_prog_0(struct svc_req *rqstp, SVCXPRT *transp)
{
	union {
		struct nlm_sm_status nlm_sm_notify_0_arg;
	} argument;
	char result;
	bool_t retval;
	xdrproc_t xdr_argument, xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(rqstp,
			(xdrproc_t) xdr_void, (char *)NULL);
		svc_freereq(rqstp);
		return;

	case NLM_SM_NOTIFY:
		xdr_argument = (xdrproc_t) xdr_nlm_sm_status;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_sm_notify_0_svc;
		break;

	default:
		svcerr_noproc(rqstp);
		svc_freereq(rqstp);
		return;
	}
	(void) memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		svcerr_decode(rqstp);
		svc_freereq(rqstp);
		return;
	}
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval > 0 && !svc_sendreply(rqstp, xdr_result, (char *)&result)) {
		svcerr_systemerr(rqstp);
	}
	if (!svc_freeargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		printf("unable to free arguments");
		//exit(1);
	}
	svc_freereq(rqstp);

	return;
}

void
nlm_prog_1(struct svc_req *rqstp, SVCXPRT *transp)
{
	union {
		struct nlm_testargs nlm_test_1_arg;
		struct nlm_lockargs nlm_lock_1_arg;
		struct nlm_cancargs nlm_cancel_1_arg;
		struct nlm_unlockargs nlm_unlock_1_arg;
		struct nlm_testargs nlm_granted_1_arg;
		struct nlm_testargs nlm_test_msg_1_arg;
		struct nlm_lockargs nlm_lock_msg_1_arg;
		struct nlm_cancargs nlm_cancel_msg_1_arg;
		struct nlm_unlockargs nlm_unlock_msg_1_arg;
		struct nlm_testargs nlm_granted_msg_1_arg;
		nlm_testres nlm_test_res_1_arg;
		nlm_res nlm_lock_res_1_arg;
		nlm_res nlm_cancel_res_1_arg;
		nlm_res nlm_unlock_res_1_arg;
		nlm_res nlm_granted_res_1_arg;
	} argument;
	union {
		nlm_testres nlm_test_1_res;
		nlm_res nlm_lock_1_res;
		nlm_res nlm_cancel_1_res;
		nlm_res nlm_unlock_1_res;
		nlm_res nlm_granted_1_res;
	} result;
	bool_t retval;
	xdrproc_t xdr_argument, xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(rqstp,
			(xdrproc_t) xdr_void, (char *)NULL);
		svc_freereq(rqstp);
		return;

	case NLM_TEST:
		xdr_argument = (xdrproc_t) xdr_nlm_testargs;
		xdr_result = (xdrproc_t) xdr_nlm_testres;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_test_1_svc;
		break;

	case NLM_LOCK:
		xdr_argument = (xdrproc_t) xdr_nlm_lockargs;
		xdr_result = (xdrproc_t) xdr_nlm_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_lock_1_svc;
		break;

	case NLM_CANCEL:
		xdr_argument = (xdrproc_t) xdr_nlm_cancargs;
		xdr_result = (xdrproc_t) xdr_nlm_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_cancel_1_svc;
		break;

	case NLM_UNLOCK:
		xdr_argument = (xdrproc_t) xdr_nlm_unlockargs;
		xdr_result = (xdrproc_t) xdr_nlm_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_unlock_1_svc;
		break;

	case NLM_GRANTED:
		xdr_argument = (xdrproc_t) xdr_nlm_testargs;
		xdr_result = (xdrproc_t) xdr_nlm_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_granted_1_svc;
		break;

	case NLM_TEST_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm_testargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_test_msg_1_svc;
		break;

	case NLM_LOCK_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm_lockargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_lock_msg_1_svc;
		break;

	case NLM_CANCEL_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm_cancargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_cancel_msg_1_svc;
		break;

	case NLM_UNLOCK_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm_unlockargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_unlock_msg_1_svc;
		break;

	case NLM_GRANTED_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm_testargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_granted_msg_1_svc;
		break;

	case NLM_TEST_RES:
		xdr_argument = (xdrproc_t) xdr_nlm_testres;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_test_res_1_svc;
		break;

	case NLM_LOCK_RES:
		xdr_argument = (xdrproc_t) xdr_nlm_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_lock_res_1_svc;
		break;

	case NLM_CANCEL_RES:
		xdr_argument = (xdrproc_t) xdr_nlm_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_cancel_res_1_svc;
		break;

	case NLM_UNLOCK_RES:
		xdr_argument = (xdrproc_t) xdr_nlm_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_unlock_res_1_svc;
		break;

	case NLM_GRANTED_RES:
		xdr_argument = (xdrproc_t) xdr_nlm_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_granted_res_1_svc;
		break;

	default:
		svcerr_noproc(rqstp);
		svc_freereq(rqstp);
		return;
	}
	(void) memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		svcerr_decode(rqstp);
		svc_freereq(rqstp);
		return;
	}
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval > 0 && !svc_sendreply(rqstp, xdr_result, (char *)&result)) {
		svcerr_systemerr(rqstp);
	}
	if (!svc_freeargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		printf("unable to free arguments");
		//exit(1);
	}
	svc_freereq(rqstp);
	if (!nlm_prog_1_freeresult(transp, xdr_result, (caddr_t) &result))
		printf("unable to free results");

	return;
}

void
nlm_prog_3(struct svc_req *rqstp, SVCXPRT *transp)
{
	union {
		nlm_shareargs nlm_share_3_arg;
		nlm_shareargs nlm_unshare_3_arg;
		nlm_lockargs nlm_nm_lock_3_arg;
		nlm_notify nlm_free_all_3_arg;
	} argument;
	union {
		nlm_shareres nlm_share_3_res;
		nlm_shareres nlm_unshare_3_res;
		nlm_res nlm_nm_lock_3_res;
	} result;
	bool_t retval;
	xdrproc_t xdr_argument, xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(rqstp,
			(xdrproc_t) xdr_void, (char *)NULL);
		svc_freereq(rqstp);
		return;

	case NLM_TEST:
	case NLM_LOCK:
	case NLM_CANCEL:
	case NLM_UNLOCK:
	case NLM_GRANTED:
	case NLM_TEST_MSG:
	case NLM_LOCK_MSG:
	case NLM_CANCEL_MSG:
	case NLM_UNLOCK_MSG:
	case NLM_GRANTED_MSG:
	case NLM_TEST_RES:
	case NLM_LOCK_RES:
	case NLM_CANCEL_RES:
	case NLM_UNLOCK_RES:
	case NLM_GRANTED_RES:
		nlm_prog_1(rqstp, transp);
		return;

	case NLM_SHARE:
		xdr_argument = (xdrproc_t) xdr_nlm_shareargs;
		xdr_result = (xdrproc_t) xdr_nlm_shareres;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_share_3_svc;
		break;

	case NLM_UNSHARE:
		xdr_argument = (xdrproc_t) xdr_nlm_shareargs;
		xdr_result = (xdrproc_t) xdr_nlm_shareres;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_unshare_3_svc;
		break;

	case NLM_NM_LOCK:
		xdr_argument = (xdrproc_t) xdr_nlm_lockargs;
		xdr_result = (xdrproc_t) xdr_nlm_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_nm_lock_3_svc;
		break;

	case NLM_FREE_ALL:
		xdr_argument = (xdrproc_t) xdr_nlm_notify;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm_free_all_3_svc;
		break;

	default:
		svcerr_noproc(rqstp);
		svc_freereq(rqstp);
		return;
	}
	(void) memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		svcerr_decode(rqstp);
		svc_freereq(rqstp);
		return;
	}
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval > 0 && !svc_sendreply(rqstp, xdr_result, (char *)&result)) {
		svcerr_systemerr(rqstp);
	}
	if (!svc_freeargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		printf("unable to free arguments");
		//exit(1);
	}
	svc_freereq(rqstp);
	if (!nlm_prog_3_freeresult(transp, xdr_result, (caddr_t) &result))
		printf("unable to free results");

	return;
}

void
nlm_prog_4(struct svc_req *rqstp, SVCXPRT *transp)
{
	union {
		nlm4_testargs nlm4_test_4_arg;
		nlm4_lockargs nlm4_lock_4_arg;
		nlm4_cancargs nlm4_cancel_4_arg;
		nlm4_unlockargs nlm4_unlock_4_arg;
		nlm4_testargs nlm4_granted_4_arg;
		nlm4_testargs nlm4_test_msg_4_arg;
		nlm4_lockargs nlm4_lock_msg_4_arg;
		nlm4_cancargs nlm4_cancel_msg_4_arg;
		nlm4_unlockargs nlm4_unlock_msg_4_arg;
		nlm4_testargs nlm4_granted_msg_4_arg;
		nlm4_testres nlm4_test_res_4_arg;
		nlm4_res nlm4_lock_res_4_arg;
		nlm4_res nlm4_cancel_res_4_arg;
		nlm4_res nlm4_unlock_res_4_arg;
		nlm4_res nlm4_granted_res_4_arg;
		nlm4_shareargs nlm4_share_4_arg;
		nlm4_shareargs nlm4_unshare_4_arg;
		nlm4_lockargs nlm4_nm_lock_4_arg;
		nlm4_notify nlm4_free_all_4_arg;
	} argument;
	union {
		nlm4_testres nlm4_test_4_res;
		nlm4_res nlm4_lock_4_res;
		nlm4_res nlm4_cancel_4_res;
		nlm4_res nlm4_unlock_4_res;
		nlm4_res nlm4_granted_4_res;
		nlm4_shareres nlm4_share_4_res;
		nlm4_shareres nlm4_unshare_4_res;
		nlm4_res nlm4_nm_lock_4_res;
	} result;
	bool_t retval;
	xdrproc_t xdr_argument, xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(rqstp,
			(xdrproc_t) xdr_void, (char *)NULL);
		svc_freereq(rqstp);
		return;

	case NLM4_TEST:
		xdr_argument = (xdrproc_t) xdr_nlm4_testargs;
		xdr_result = (xdrproc_t) xdr_nlm4_testres;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_test_4_svc;
		break;

	case NLM4_LOCK:
		xdr_argument = (xdrproc_t) xdr_nlm4_lockargs;
		xdr_result = (xdrproc_t) xdr_nlm4_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_lock_4_svc;
		break;

	case NLM4_CANCEL:
		xdr_argument = (xdrproc_t) xdr_nlm4_cancargs;
		xdr_result = (xdrproc_t) xdr_nlm4_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_cancel_4_svc;
		break;

	case NLM4_UNLOCK:
		xdr_argument = (xdrproc_t) xdr_nlm4_unlockargs;
		xdr_result = (xdrproc_t) xdr_nlm4_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_unlock_4_svc;
		break;

	case NLM4_GRANTED:
		xdr_argument = (xdrproc_t) xdr_nlm4_testargs;
		xdr_result = (xdrproc_t) xdr_nlm4_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_granted_4_svc;
		break;

	case NLM4_TEST_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm4_testargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_test_msg_4_svc;
		break;

	case NLM4_LOCK_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm4_lockargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_lock_msg_4_svc;
		break;

	case NLM4_CANCEL_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm4_cancargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_cancel_msg_4_svc;
		break;

	case NLM4_UNLOCK_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm4_unlockargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_unlock_msg_4_svc;
		break;

	case NLM4_GRANTED_MSG:
		xdr_argument = (xdrproc_t) xdr_nlm4_testargs;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_granted_msg_4_svc;
		break;

	case NLM4_TEST_RES:
		xdr_argument = (xdrproc_t) xdr_nlm4_testres;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_test_res_4_svc;
		break;

	case NLM4_LOCK_RES:
		xdr_argument = (xdrproc_t) xdr_nlm4_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_lock_res_4_svc;
		break;

	case NLM4_CANCEL_RES:
		xdr_argument = (xdrproc_t) xdr_nlm4_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_cancel_res_4_svc;
		break;

	case NLM4_UNLOCK_RES:
		xdr_argument = (xdrproc_t) xdr_nlm4_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_unlock_res_4_svc;
		break;

	case NLM4_GRANTED_RES:
		xdr_argument = (xdrproc_t) xdr_nlm4_res;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_granted_res_4_svc;
		break;

	case NLM4_SHARE:
		xdr_argument = (xdrproc_t) xdr_nlm4_shareargs;
		xdr_result = (xdrproc_t) xdr_nlm4_shareres;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_share_4_svc;
		break;

	case NLM4_UNSHARE:
		xdr_argument = (xdrproc_t) xdr_nlm4_shareargs;
		xdr_result = (xdrproc_t) xdr_nlm4_shareres;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_unshare_4_svc;
		break;

	case NLM4_NM_LOCK:
		xdr_argument = (xdrproc_t) xdr_nlm4_lockargs;
		xdr_result = (xdrproc_t) xdr_nlm4_res;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_nm_lock_4_svc;
		break;

	case NLM4_FREE_ALL:
		xdr_argument = (xdrproc_t) xdr_nlm4_notify;
		xdr_result = (xdrproc_t) xdr_void;
		local = (bool_t (*) (char *,  void *,  struct svc_req *))nlm4_free_all_4_svc;
		break;

	default:
		svcerr_noproc(rqstp);
		svc_freereq(rqstp);
		return;
	}
	(void) memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		svcerr_decode(rqstp);
		svc_freereq(rqstp);
		return;
	}
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval > 0 && !svc_sendreply(rqstp, xdr_result, (char *)&result)) {
		svcerr_systemerr(rqstp);
	}
	if (!svc_freeargs(rqstp, xdr_argument, (char *)(caddr_t) &argument)) {
		printf("unable to free arguments");
		//exit(1);
	}
	svc_freereq(rqstp);
	if (!nlm_prog_4_freeresult(transp, xdr_result, (caddr_t) &result))
		printf("unable to free results");

	return;
}
