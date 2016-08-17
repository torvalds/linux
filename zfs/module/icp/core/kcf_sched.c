/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file contains the core framework routines for the
 * kernel cryptographic framework. These routines are at the
 * layer, between the kernel API/ioctls and the SPI.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/api.h>

kcf_global_swq_t *gswq;	/* Global software queue */

/* Thread pool related variables */
static kcf_pool_t *kcfpool;	/* Thread pool of kcfd LWPs */
int kcf_maxthreads = 2;
int kcf_minthreads = 1;
int kcf_thr_multiple = 2;	/* Boot-time tunable for experimentation */
static ulong_t	kcf_idlethr_timeout;
#define	KCF_DEFAULT_THRTIMEOUT	60000000	/* 60 seconds */

/* kmem caches used by the scheduler */
static kmem_cache_t *kcf_sreq_cache;
static kmem_cache_t *kcf_areq_cache;
static kmem_cache_t *kcf_context_cache;

/* Global request ID table */
static kcf_reqid_table_t *kcf_reqid_table[REQID_TABLES];

/* KCF stats. Not protected. */
static kcf_stats_t kcf_ksdata = {
	{ "total threads in pool",	KSTAT_DATA_UINT32},
	{ "idle threads in pool",	KSTAT_DATA_UINT32},
	{ "min threads in pool",	KSTAT_DATA_UINT32},
	{ "max threads in pool",	KSTAT_DATA_UINT32},
	{ "requests in gswq",		KSTAT_DATA_UINT32},
	{ "max requests in gswq",	KSTAT_DATA_UINT32},
	{ "threads for HW taskq",	KSTAT_DATA_UINT32},
	{ "minalloc for HW taskq",	KSTAT_DATA_UINT32},
	{ "maxalloc for HW taskq",	KSTAT_DATA_UINT32}
};

static kstat_t *kcf_misc_kstat = NULL;
ulong_t kcf_swprov_hndl = 0;

static kcf_areq_node_t *kcf_areqnode_alloc(kcf_provider_desc_t *,
    kcf_context_t *, crypto_call_req_t *, kcf_req_params_t *, boolean_t);
static int kcf_disp_sw_request(kcf_areq_node_t *);
static void process_req_hwp(void *);
static int kcf_enqueue(kcf_areq_node_t *);
static void kcfpool_alloc(void);
static void kcf_reqid_delete(kcf_areq_node_t *areq);
static crypto_req_id_t kcf_reqid_insert(kcf_areq_node_t *areq);
static int kcf_misc_kstat_update(kstat_t *ksp, int rw);

/*
 * Create a new context.
 */
crypto_ctx_t *
kcf_new_ctx(crypto_call_req_t *crq, kcf_provider_desc_t *pd,
    crypto_session_id_t sid)
{
	crypto_ctx_t *ctx;
	kcf_context_t *kcf_ctx;

	kcf_ctx = kmem_cache_alloc(kcf_context_cache,
	    (crq == NULL) ? KM_SLEEP : KM_NOSLEEP);
	if (kcf_ctx == NULL)
		return (NULL);

	/* initialize the context for the consumer */
	kcf_ctx->kc_refcnt = 1;
	kcf_ctx->kc_req_chain_first = NULL;
	kcf_ctx->kc_req_chain_last = NULL;
	kcf_ctx->kc_secondctx = NULL;
	KCF_PROV_REFHOLD(pd);
	kcf_ctx->kc_prov_desc = pd;
	kcf_ctx->kc_sw_prov_desc = NULL;
	kcf_ctx->kc_mech = NULL;

	ctx = &kcf_ctx->kc_glbl_ctx;
	ctx->cc_provider = pd->pd_prov_handle;
	ctx->cc_session = sid;
	ctx->cc_provider_private = NULL;
	ctx->cc_framework_private = (void *)kcf_ctx;
	ctx->cc_flags = 0;
	ctx->cc_opstate = NULL;

	return (ctx);
}

/*
 * Allocate a new async request node.
 *
 * ictx - Framework private context pointer
 * crq - Has callback function and argument. Should be non NULL.
 * req - The parameters to pass to the SPI
 */
static kcf_areq_node_t *
kcf_areqnode_alloc(kcf_provider_desc_t *pd, kcf_context_t *ictx,
    crypto_call_req_t *crq, kcf_req_params_t *req, boolean_t isdual)
{
	kcf_areq_node_t	*arptr, *areq;

	ASSERT(crq != NULL);
	arptr = kmem_cache_alloc(kcf_areq_cache, KM_NOSLEEP);
	if (arptr == NULL)
		return (NULL);

	arptr->an_state = REQ_ALLOCATED;
	arptr->an_reqarg = *crq;
	arptr->an_params = *req;
	arptr->an_context = ictx;
	arptr->an_isdual = isdual;

	arptr->an_next = arptr->an_prev = NULL;
	KCF_PROV_REFHOLD(pd);
	arptr->an_provider = pd;
	arptr->an_tried_plist = NULL;
	arptr->an_refcnt = 1;
	arptr->an_idnext = arptr->an_idprev = NULL;

	/*
	 * Requests for context-less operations do not use the
	 * fields - an_is_my_turn, and an_ctxchain_next.
	 */
	if (ictx == NULL)
		return (arptr);

	KCF_CONTEXT_REFHOLD(ictx);
	/*
	 * Chain this request to the context.
	 */
	mutex_enter(&ictx->kc_in_use_lock);
	arptr->an_ctxchain_next = NULL;
	if ((areq = ictx->kc_req_chain_last) == NULL) {
		arptr->an_is_my_turn = B_TRUE;
		ictx->kc_req_chain_last =
		    ictx->kc_req_chain_first = arptr;
	} else {
		ASSERT(ictx->kc_req_chain_first != NULL);
		arptr->an_is_my_turn = B_FALSE;
		/* Insert the new request to the end of the chain. */
		areq->an_ctxchain_next = arptr;
		ictx->kc_req_chain_last = arptr;
	}
	mutex_exit(&ictx->kc_in_use_lock);

	return (arptr);
}

/*
 * Queue the request node and do one of the following:
 *	- If there is an idle thread signal it to run.
 *	- If there is no idle thread and max running threads is not
 *	  reached, signal the creator thread for more threads.
 *
 * If the two conditions above are not met, we don't need to do
 * any thing. The request will be picked up by one of the
 * worker threads when it becomes available.
 */
static int
kcf_disp_sw_request(kcf_areq_node_t *areq)
{
	int err;
	int cnt = 0;

	if ((err = kcf_enqueue(areq)) != 0)
		return (err);

	if (kcfpool->kp_idlethreads > 0) {
		/* Signal an idle thread to run */
		mutex_enter(&gswq->gs_lock);
		cv_signal(&gswq->gs_cv);
		mutex_exit(&gswq->gs_lock);

		return (CRYPTO_QUEUED);
	}

	/*
	 * We keep the number of running threads to be at
	 * kcf_minthreads to reduce gs_lock contention.
	 */
	cnt = kcf_minthreads -
	    (kcfpool->kp_threads - kcfpool->kp_blockedthreads);
	if (cnt > 0) {
		/*
		 * The following ensures the number of threads in pool
		 * does not exceed kcf_maxthreads.
		 */
		cnt = MIN(cnt, kcf_maxthreads - (int)kcfpool->kp_threads);
		if (cnt > 0) {
			/* Signal the creator thread for more threads */
			mutex_enter(&kcfpool->kp_user_lock);
			if (!kcfpool->kp_signal_create_thread) {
				kcfpool->kp_signal_create_thread = B_TRUE;
				kcfpool->kp_nthrs = cnt;
				cv_signal(&kcfpool->kp_user_cv);
			}
			mutex_exit(&kcfpool->kp_user_lock);
		}
	}

	return (CRYPTO_QUEUED);
}

/*
 * This routine is called by the taskq associated with
 * each hardware provider. We notify the kernel consumer
 * via the callback routine in case of CRYPTO_SUCCESS or
 * a failure.
 *
 * A request can be of type kcf_areq_node_t or of type
 * kcf_sreq_node_t.
 */
static void
process_req_hwp(void *ireq)
{
	int error = 0;
	crypto_ctx_t *ctx;
	kcf_call_type_t ctype;
	kcf_provider_desc_t *pd;
	kcf_areq_node_t *areq = (kcf_areq_node_t *)ireq;
	kcf_sreq_node_t *sreq = (kcf_sreq_node_t *)ireq;

	pd = ((ctype = GET_REQ_TYPE(ireq)) == CRYPTO_SYNCH) ?
	    sreq->sn_provider : areq->an_provider;

	/*
	 * Wait if flow control is in effect for the provider. A
	 * CRYPTO_PROVIDER_READY or CRYPTO_PROVIDER_FAILED
	 * notification will signal us. We also get signaled if
	 * the provider is unregistering.
	 */
	if (pd->pd_state == KCF_PROV_BUSY) {
		mutex_enter(&pd->pd_lock);
		while (pd->pd_state == KCF_PROV_BUSY)
			cv_wait(&pd->pd_resume_cv, &pd->pd_lock);
		mutex_exit(&pd->pd_lock);
	}

	/*
	 * Bump the internal reference count while the request is being
	 * processed. This is how we know when it's safe to unregister
	 * a provider. This step must precede the pd_state check below.
	 */
	KCF_PROV_IREFHOLD(pd);

	/*
	 * Fail the request if the provider has failed. We return a
	 * recoverable error and the notified clients attempt any
	 * recovery. For async clients this is done in kcf_aop_done()
	 * and for sync clients it is done in the k-api routines.
	 */
	if (pd->pd_state >= KCF_PROV_FAILED) {
		error = CRYPTO_DEVICE_ERROR;
		goto bail;
	}

	if (ctype == CRYPTO_SYNCH) {
		mutex_enter(&sreq->sn_lock);
		sreq->sn_state = REQ_INPROGRESS;
		mutex_exit(&sreq->sn_lock);

		ctx = sreq->sn_context ? &sreq->sn_context->kc_glbl_ctx : NULL;
		error = common_submit_request(sreq->sn_provider, ctx,
		    sreq->sn_params, sreq);
	} else {
		kcf_context_t *ictx;
		ASSERT(ctype == CRYPTO_ASYNCH);

		/*
		 * We are in the per-hardware provider thread context and
		 * hence can sleep. Note that the caller would have done
		 * a taskq_dispatch(..., TQ_NOSLEEP) and would have returned.
		 */
		ctx = (ictx = areq->an_context) ? &ictx->kc_glbl_ctx : NULL;

		mutex_enter(&areq->an_lock);
		/*
		 * We need to maintain ordering for multi-part requests.
		 * an_is_my_turn is set to B_TRUE initially for a request
		 * when it is enqueued and there are no other requests
		 * for that context. It is set later from kcf_aop_done() when
		 * the request before us in the chain of requests for the
		 * context completes. We get signaled at that point.
		 */
		if (ictx != NULL) {
			ASSERT(ictx->kc_prov_desc == areq->an_provider);

			while (areq->an_is_my_turn == B_FALSE) {
				cv_wait(&areq->an_turn_cv, &areq->an_lock);
			}
		}
		areq->an_state = REQ_INPROGRESS;
		mutex_exit(&areq->an_lock);

		error = common_submit_request(areq->an_provider, ctx,
		    &areq->an_params, areq);
	}

bail:
	if (error == CRYPTO_QUEUED) {
		/*
		 * The request is queued by the provider and we should
		 * get a crypto_op_notification() from the provider later.
		 * We notify the consumer at that time.
		 */
		return;
	} else {		/* CRYPTO_SUCCESS or other failure */
		KCF_PROV_IREFRELE(pd);
		if (ctype == CRYPTO_SYNCH)
			kcf_sop_done(sreq, error);
		else
			kcf_aop_done(areq, error);
	}
}

/*
 * This routine checks if a request can be retried on another
 * provider. If true, mech1 is initialized to point to the mechanism
 * structure. mech2 is also initialized in case of a dual operation. fg
 * is initialized to the correct crypto_func_group_t bit flag. They are
 * initialized by this routine, so that the caller can pass them to a
 * kcf_get_mech_provider() or kcf_get_dual_provider() with no further change.
 *
 * We check that the request is for a init or atomic routine and that
 * it is for one of the operation groups used from k-api .
 */
static boolean_t
can_resubmit(kcf_areq_node_t *areq, crypto_mechanism_t **mech1,
    crypto_mechanism_t **mech2, crypto_func_group_t *fg)
{
	kcf_req_params_t *params;
	kcf_op_type_t optype;

	params = &areq->an_params;
	optype = params->rp_optype;

	if (!(IS_INIT_OP(optype) || IS_ATOMIC_OP(optype)))
		return (B_FALSE);

	switch (params->rp_opgrp) {
	case KCF_OG_DIGEST: {
		kcf_digest_ops_params_t *dops = &params->rp_u.digest_params;

		dops->do_mech.cm_type = dops->do_framework_mechtype;
		*mech1 = &dops->do_mech;
		*fg = (optype == KCF_OP_INIT) ? CRYPTO_FG_DIGEST :
		    CRYPTO_FG_DIGEST_ATOMIC;
		break;
	}

	case KCF_OG_MAC: {
		kcf_mac_ops_params_t *mops = &params->rp_u.mac_params;

		mops->mo_mech.cm_type = mops->mo_framework_mechtype;
		*mech1 = &mops->mo_mech;
		*fg = (optype == KCF_OP_INIT) ? CRYPTO_FG_MAC :
		    CRYPTO_FG_MAC_ATOMIC;
		break;
	}

	case KCF_OG_SIGN: {
		kcf_sign_ops_params_t *sops = &params->rp_u.sign_params;

		sops->so_mech.cm_type = sops->so_framework_mechtype;
		*mech1 = &sops->so_mech;
		switch (optype) {
		case KCF_OP_INIT:
			*fg = CRYPTO_FG_SIGN;
			break;
		case KCF_OP_ATOMIC:
			*fg = CRYPTO_FG_SIGN_ATOMIC;
			break;
		default:
			ASSERT(optype == KCF_OP_SIGN_RECOVER_ATOMIC);
			*fg = CRYPTO_FG_SIGN_RECOVER_ATOMIC;
		}
		break;
	}

	case KCF_OG_VERIFY: {
		kcf_verify_ops_params_t *vops = &params->rp_u.verify_params;

		vops->vo_mech.cm_type = vops->vo_framework_mechtype;
		*mech1 = &vops->vo_mech;
		switch (optype) {
		case KCF_OP_INIT:
			*fg = CRYPTO_FG_VERIFY;
			break;
		case KCF_OP_ATOMIC:
			*fg = CRYPTO_FG_VERIFY_ATOMIC;
			break;
		default:
			ASSERT(optype == KCF_OP_VERIFY_RECOVER_ATOMIC);
			*fg = CRYPTO_FG_VERIFY_RECOVER_ATOMIC;
		}
		break;
	}

	case KCF_OG_ENCRYPT: {
		kcf_encrypt_ops_params_t *eops = &params->rp_u.encrypt_params;

		eops->eo_mech.cm_type = eops->eo_framework_mechtype;
		*mech1 = &eops->eo_mech;
		*fg = (optype == KCF_OP_INIT) ? CRYPTO_FG_ENCRYPT :
		    CRYPTO_FG_ENCRYPT_ATOMIC;
		break;
	}

	case KCF_OG_DECRYPT: {
		kcf_decrypt_ops_params_t *dcrops = &params->rp_u.decrypt_params;

		dcrops->dop_mech.cm_type = dcrops->dop_framework_mechtype;
		*mech1 = &dcrops->dop_mech;
		*fg = (optype == KCF_OP_INIT) ? CRYPTO_FG_DECRYPT :
		    CRYPTO_FG_DECRYPT_ATOMIC;
		break;
	}

	case KCF_OG_ENCRYPT_MAC: {
		kcf_encrypt_mac_ops_params_t *eops =
		    &params->rp_u.encrypt_mac_params;

		eops->em_encr_mech.cm_type = eops->em_framework_encr_mechtype;
		*mech1 = &eops->em_encr_mech;
		eops->em_mac_mech.cm_type = eops->em_framework_mac_mechtype;
		*mech2 = &eops->em_mac_mech;
		*fg = (optype == KCF_OP_INIT) ? CRYPTO_FG_ENCRYPT_MAC :
		    CRYPTO_FG_ENCRYPT_MAC_ATOMIC;
		break;
	}

	case KCF_OG_MAC_DECRYPT: {
		kcf_mac_decrypt_ops_params_t *dops =
		    &params->rp_u.mac_decrypt_params;

		dops->md_mac_mech.cm_type = dops->md_framework_mac_mechtype;
		*mech1 = &dops->md_mac_mech;
		dops->md_decr_mech.cm_type = dops->md_framework_decr_mechtype;
		*mech2 = &dops->md_decr_mech;
		*fg = (optype == KCF_OP_INIT) ? CRYPTO_FG_MAC_DECRYPT :
		    CRYPTO_FG_MAC_DECRYPT_ATOMIC;
		break;
	}

	default:
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * This routine is called when a request to a provider has failed
 * with a recoverable error. This routine tries to find another provider
 * and dispatches the request to the new provider, if one is available.
 * We reuse the request structure.
 *
 * A return value of NULL from kcf_get_mech_provider() indicates
 * we have tried the last provider.
 */
static int
kcf_resubmit_request(kcf_areq_node_t *areq)
{
	int error = CRYPTO_FAILED;
	kcf_context_t *ictx;
	kcf_provider_desc_t *old_pd;
	kcf_provider_desc_t *new_pd;
	crypto_mechanism_t *mech1 = NULL, *mech2 = NULL;
	crypto_mech_type_t prov_mt1, prov_mt2;
	crypto_func_group_t fg = 0;

	if (!can_resubmit(areq, &mech1, &mech2, &fg))
		return (error);

	old_pd = areq->an_provider;
	/*
	 * Add old_pd to the list of providers already tried. We release
	 * the hold on old_pd (from the earlier kcf_get_mech_provider()) in
	 * kcf_free_triedlist().
	 */
	if (kcf_insert_triedlist(&areq->an_tried_plist, old_pd,
	    KM_NOSLEEP) == NULL)
		return (error);

	if (mech1 && !mech2) {
		new_pd = kcf_get_mech_provider(mech1->cm_type, NULL, &error,
		    areq->an_tried_plist, fg,
		    (areq->an_reqarg.cr_flag & CRYPTO_RESTRICTED), 0);
	} else {
		ASSERT(mech1 != NULL && mech2 != NULL);

		new_pd = kcf_get_dual_provider(mech1, mech2, NULL, &prov_mt1,
		    &prov_mt2, &error, areq->an_tried_plist, fg, fg,
		    (areq->an_reqarg.cr_flag & CRYPTO_RESTRICTED), 0);
	}

	if (new_pd == NULL)
		return (error);

	/*
	 * We reuse the old context by resetting provider specific
	 * fields in it.
	 */
	if ((ictx = areq->an_context) != NULL) {
		crypto_ctx_t *ctx;

		ASSERT(old_pd == ictx->kc_prov_desc);
		KCF_PROV_REFRELE(ictx->kc_prov_desc);
		KCF_PROV_REFHOLD(new_pd);
		ictx->kc_prov_desc = new_pd;

		ctx = &ictx->kc_glbl_ctx;
		ctx->cc_provider = new_pd->pd_prov_handle;
		ctx->cc_session = new_pd->pd_sid;
		ctx->cc_provider_private = NULL;
	}

	/* We reuse areq. by resetting the provider and context fields. */
	KCF_PROV_REFRELE(old_pd);
	KCF_PROV_REFHOLD(new_pd);
	areq->an_provider = new_pd;
	mutex_enter(&areq->an_lock);
	areq->an_state = REQ_WAITING;
	mutex_exit(&areq->an_lock);

	switch (new_pd->pd_prov_type) {
	case CRYPTO_SW_PROVIDER:
		error = kcf_disp_sw_request(areq);
		break;

	case CRYPTO_HW_PROVIDER: {
		taskq_t *taskq = new_pd->pd_sched_info.ks_taskq;

		if (taskq_dispatch(taskq, process_req_hwp, areq, TQ_NOSLEEP) ==
		    TASKQID_INVALID) {
			error = CRYPTO_HOST_MEMORY;
		} else {
			error = CRYPTO_QUEUED;
		}

		break;
	default:
		break;
	}
	}

	return (error);
}

static inline int EMPTY_TASKQ(taskq_t *tq)
{
#ifdef _KERNEL
	return (tq->tq_lowest_id == tq->tq_next_id);
#else
	return (tq->tq_task.tqent_next == &tq->tq_task || tq->tq_active == 0);
#endif
}

/*
 * Routine called by both ioctl and k-api. The consumer should
 * bundle the parameters into a kcf_req_params_t structure. A bunch
 * of macros are available in ops_impl.h for this bundling. They are:
 *
 * 	KCF_WRAP_DIGEST_OPS_PARAMS()
 *	KCF_WRAP_MAC_OPS_PARAMS()
 *	KCF_WRAP_ENCRYPT_OPS_PARAMS()
 *	KCF_WRAP_DECRYPT_OPS_PARAMS() ... etc.
 *
 * It is the caller's responsibility to free the ctx argument when
 * appropriate. See the KCF_CONTEXT_COND_RELEASE macro for details.
 */
int
kcf_submit_request(kcf_provider_desc_t *pd, crypto_ctx_t *ctx,
    crypto_call_req_t *crq, kcf_req_params_t *params, boolean_t cont)
{
	int error = CRYPTO_SUCCESS;
	kcf_areq_node_t *areq;
	kcf_sreq_node_t *sreq;
	kcf_context_t *kcf_ctx;
	taskq_t *taskq = pd->pd_sched_info.ks_taskq;

	kcf_ctx = ctx ? (kcf_context_t *)ctx->cc_framework_private : NULL;

	/* Synchronous cases */
	if (crq == NULL) {
		switch (pd->pd_prov_type) {
		case CRYPTO_SW_PROVIDER:
			error = common_submit_request(pd, ctx, params,
			    KCF_RHNDL(KM_SLEEP));
			break;

		case CRYPTO_HW_PROVIDER:
			/*
			 * Special case for CRYPTO_SYNCHRONOUS providers that
			 * never return a CRYPTO_QUEUED error. We skip any
			 * request allocation and call the SPI directly.
			 */
			if ((pd->pd_flags & CRYPTO_SYNCHRONOUS) &&
			    EMPTY_TASKQ(taskq)) {
				KCF_PROV_IREFHOLD(pd);
				if (pd->pd_state == KCF_PROV_READY) {
					error = common_submit_request(pd, ctx,
					    params, KCF_RHNDL(KM_SLEEP));
					KCF_PROV_IREFRELE(pd);
					ASSERT(error != CRYPTO_QUEUED);
					break;
				}
				KCF_PROV_IREFRELE(pd);
			}

			sreq = kmem_cache_alloc(kcf_sreq_cache, KM_SLEEP);
			sreq->sn_state = REQ_ALLOCATED;
			sreq->sn_rv = CRYPTO_FAILED;
			sreq->sn_params = params;

			/*
			 * Note that we do not need to hold the context
			 * for synchronous case as the context will never
			 * become invalid underneath us. We do not need to hold
			 * the provider here either as the caller has a hold.
			 */
			sreq->sn_context = kcf_ctx;
			ASSERT(KCF_PROV_REFHELD(pd));
			sreq->sn_provider = pd;

			ASSERT(taskq != NULL);
			/*
			 * Call the SPI directly if the taskq is empty and the
			 * provider is not busy, else dispatch to the taskq.
			 * Calling directly is fine as this is the synchronous
			 * case. This is unlike the asynchronous case where we
			 * must always dispatch to the taskq.
			 */
			if (EMPTY_TASKQ(taskq) &&
			    pd->pd_state == KCF_PROV_READY) {
				process_req_hwp(sreq);
			} else {
				/*
				 * We can not tell from taskq_dispatch() return
				 * value if we exceeded maxalloc. Hence the
				 * check here. Since we are allowed to wait in
				 * the synchronous case, we wait for the taskq
				 * to become empty.
				 */
				if (taskq->tq_nalloc >= crypto_taskq_maxalloc) {
					taskq_wait(taskq);
				}

				(void) taskq_dispatch(taskq, process_req_hwp,
				    sreq, TQ_SLEEP);
			}

			/*
			 * Wait for the notification to arrive,
			 * if the operation is not done yet.
			 * Bug# 4722589 will make the wait a cv_wait_sig().
			 */
			mutex_enter(&sreq->sn_lock);
			while (sreq->sn_state < REQ_DONE)
				cv_wait(&sreq->sn_cv, &sreq->sn_lock);
			mutex_exit(&sreq->sn_lock);

			error = sreq->sn_rv;
			kmem_cache_free(kcf_sreq_cache, sreq);

			break;

		default:
			error = CRYPTO_FAILED;
			break;
		}

	} else {	/* Asynchronous cases */
		switch (pd->pd_prov_type) {
		case CRYPTO_SW_PROVIDER:
			if (!(crq->cr_flag & CRYPTO_ALWAYS_QUEUE)) {
				/*
				 * This case has less overhead since there is
				 * no switching of context.
				 */
				error = common_submit_request(pd, ctx, params,
				    KCF_RHNDL(KM_NOSLEEP));
			} else {
				/*
				 * CRYPTO_ALWAYS_QUEUE is set. We need to
				 * queue the request and return.
				 */
				areq = kcf_areqnode_alloc(pd, kcf_ctx, crq,
				    params, cont);
				if (areq == NULL)
					error = CRYPTO_HOST_MEMORY;
				else {
					if (!(crq->cr_flag
					    & CRYPTO_SKIP_REQID)) {
					/*
					 * Set the request handle. This handle
					 * is used for any crypto_cancel_req(9f)
					 * calls from the consumer. We have to
					 * do this before dispatching the
					 * request.
					 */
					crq->cr_reqid = kcf_reqid_insert(areq);
					}

					error = kcf_disp_sw_request(areq);
					/*
					 * There is an error processing this
					 * request. Remove the handle and
					 * release the request structure.
					 */
					if (error != CRYPTO_QUEUED) {
						if (!(crq->cr_flag
						    & CRYPTO_SKIP_REQID))
							kcf_reqid_delete(areq);
						KCF_AREQ_REFRELE(areq);
					}
				}
			}
			break;

		case CRYPTO_HW_PROVIDER:
			/*
			 * We need to queue the request and return.
			 */
			areq = kcf_areqnode_alloc(pd, kcf_ctx, crq, params,
			    cont);
			if (areq == NULL) {
				error = CRYPTO_HOST_MEMORY;
				goto done;
			}

			ASSERT(taskq != NULL);
			/*
			 * We can not tell from taskq_dispatch() return
			 * value if we exceeded maxalloc. Hence the check
			 * here.
			 */
			if (taskq->tq_nalloc >= crypto_taskq_maxalloc) {
				error = CRYPTO_BUSY;
				KCF_AREQ_REFRELE(areq);
				goto done;
			}

			if (!(crq->cr_flag & CRYPTO_SKIP_REQID)) {
			/*
			 * Set the request handle. This handle is used
			 * for any crypto_cancel_req(9f) calls from the
			 * consumer. We have to do this before dispatching
			 * the request.
			 */
			crq->cr_reqid = kcf_reqid_insert(areq);
			}

			if (taskq_dispatch(taskq,
			    process_req_hwp, areq, TQ_NOSLEEP) ==
			    TASKQID_INVALID) {
				error = CRYPTO_HOST_MEMORY;
				if (!(crq->cr_flag & CRYPTO_SKIP_REQID))
					kcf_reqid_delete(areq);
				KCF_AREQ_REFRELE(areq);
			} else {
				error = CRYPTO_QUEUED;
			}
			break;

		default:
			error = CRYPTO_FAILED;
			break;
		}
	}

done:
	return (error);
}

/*
 * We're done with this framework context, so free it. Note that freeing
 * framework context (kcf_context) frees the global context (crypto_ctx).
 *
 * The provider is responsible for freeing provider private context after a
 * final or single operation and resetting the cc_provider_private field
 * to NULL. It should do this before it notifies the framework of the
 * completion. We still need to call KCF_PROV_FREE_CONTEXT to handle cases
 * like crypto_cancel_ctx(9f).
 */
void
kcf_free_context(kcf_context_t *kcf_ctx)
{
	kcf_provider_desc_t *pd = kcf_ctx->kc_prov_desc;
	crypto_ctx_t *gctx = &kcf_ctx->kc_glbl_ctx;
	kcf_context_t *kcf_secondctx = kcf_ctx->kc_secondctx;

	/* Release the second context, if any */

	if (kcf_secondctx != NULL)
		KCF_CONTEXT_REFRELE(kcf_secondctx);

	if (gctx->cc_provider_private != NULL) {
		mutex_enter(&pd->pd_lock);
		if (!KCF_IS_PROV_REMOVED(pd)) {
			/*
			 * Increment the provider's internal refcnt so it
			 * doesn't unregister from the framework while
			 * we're calling the entry point.
			 */
			KCF_PROV_IREFHOLD(pd);
			mutex_exit(&pd->pd_lock);
			(void) KCF_PROV_FREE_CONTEXT(pd, gctx);
			KCF_PROV_IREFRELE(pd);
		} else {
			mutex_exit(&pd->pd_lock);
		}
	}

	/* kcf_ctx->kc_prov_desc has a hold on pd */
	KCF_PROV_REFRELE(kcf_ctx->kc_prov_desc);

	/* check if this context is shared with a software provider */
	if ((gctx->cc_flags & CRYPTO_INIT_OPSTATE) &&
	    kcf_ctx->kc_sw_prov_desc != NULL) {
		KCF_PROV_REFRELE(kcf_ctx->kc_sw_prov_desc);
	}

	kmem_cache_free(kcf_context_cache, kcf_ctx);
}

/*
 * Free the request after releasing all the holds.
 */
void
kcf_free_req(kcf_areq_node_t *areq)
{
	KCF_PROV_REFRELE(areq->an_provider);
	if (areq->an_context != NULL)
		KCF_CONTEXT_REFRELE(areq->an_context);

	if (areq->an_tried_plist != NULL)
		kcf_free_triedlist(areq->an_tried_plist);
	kmem_cache_free(kcf_areq_cache, areq);
}

/*
 * Utility routine to remove a request from the chain of requests
 * hanging off a context.
 */
void
kcf_removereq_in_ctxchain(kcf_context_t *ictx, kcf_areq_node_t *areq)
{
	kcf_areq_node_t *cur, *prev;

	/*
	 * Get context lock, search for areq in the chain and remove it.
	 */
	ASSERT(ictx != NULL);
	mutex_enter(&ictx->kc_in_use_lock);
	prev = cur = ictx->kc_req_chain_first;

	while (cur != NULL) {
		if (cur == areq) {
			if (prev == cur) {
				if ((ictx->kc_req_chain_first =
				    cur->an_ctxchain_next) == NULL)
					ictx->kc_req_chain_last = NULL;
			} else {
				if (cur == ictx->kc_req_chain_last)
					ictx->kc_req_chain_last = prev;
				prev->an_ctxchain_next = cur->an_ctxchain_next;
			}

			break;
		}
		prev = cur;
		cur = cur->an_ctxchain_next;
	}
	mutex_exit(&ictx->kc_in_use_lock);
}

/*
 * Remove the specified node from the global software queue.
 *
 * The caller must hold the queue lock and request lock (an_lock).
 */
void
kcf_remove_node(kcf_areq_node_t *node)
{
	kcf_areq_node_t *nextp = node->an_next;
	kcf_areq_node_t *prevp = node->an_prev;

	if (nextp != NULL)
		nextp->an_prev = prevp;
	else
		gswq->gs_last = prevp;

	if (prevp != NULL)
		prevp->an_next = nextp;
	else
		gswq->gs_first = nextp;

	node->an_state = REQ_CANCELED;
}

/*
 * Add the request node to the end of the global software queue.
 *
 * The caller should not hold the queue lock. Returns 0 if the
 * request is successfully queued. Returns CRYPTO_BUSY if the limit
 * on the number of jobs is exceeded.
 */
static int
kcf_enqueue(kcf_areq_node_t *node)
{
	kcf_areq_node_t *tnode;

	mutex_enter(&gswq->gs_lock);

	if (gswq->gs_njobs >= gswq->gs_maxjobs) {
		mutex_exit(&gswq->gs_lock);
		return (CRYPTO_BUSY);
	}

	if (gswq->gs_last == NULL) {
		gswq->gs_first = gswq->gs_last = node;
	} else {
		ASSERT(gswq->gs_last->an_next == NULL);
		tnode = gswq->gs_last;
		tnode->an_next = node;
		gswq->gs_last = node;
		node->an_prev = tnode;
	}

	gswq->gs_njobs++;

	/* an_lock not needed here as we hold gs_lock */
	node->an_state = REQ_WAITING;

	mutex_exit(&gswq->gs_lock);

	return (0);
}

/*
 * kmem_cache_alloc constructor for sync request structure.
 */
/* ARGSUSED */
static int
kcf_sreq_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	kcf_sreq_node_t *sreq = (kcf_sreq_node_t *)buf;

	sreq->sn_type = CRYPTO_SYNCH;
	cv_init(&sreq->sn_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&sreq->sn_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}

/* ARGSUSED */
static void
kcf_sreq_cache_destructor(void *buf, void *cdrarg)
{
	kcf_sreq_node_t *sreq = (kcf_sreq_node_t *)buf;

	mutex_destroy(&sreq->sn_lock);
	cv_destroy(&sreq->sn_cv);
}

/*
 * kmem_cache_alloc constructor for async request structure.
 */
/* ARGSUSED */
static int
kcf_areq_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	kcf_areq_node_t *areq = (kcf_areq_node_t *)buf;

	areq->an_type = CRYPTO_ASYNCH;
	areq->an_refcnt = 0;
	mutex_init(&areq->an_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&areq->an_done, NULL, CV_DEFAULT, NULL);
	cv_init(&areq->an_turn_cv, NULL, CV_DEFAULT, NULL);

	return (0);
}

/* ARGSUSED */
static void
kcf_areq_cache_destructor(void *buf, void *cdrarg)
{
	kcf_areq_node_t *areq = (kcf_areq_node_t *)buf;

	ASSERT(areq->an_refcnt == 0);
	mutex_destroy(&areq->an_lock);
	cv_destroy(&areq->an_done);
	cv_destroy(&areq->an_turn_cv);
}

/*
 * kmem_cache_alloc constructor for kcf_context structure.
 */
/* ARGSUSED */
static int
kcf_context_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	kcf_context_t *kctx = (kcf_context_t *)buf;

	kctx->kc_refcnt = 0;
	mutex_init(&kctx->kc_in_use_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}

/* ARGSUSED */
static void
kcf_context_cache_destructor(void *buf, void *cdrarg)
{
	kcf_context_t *kctx = (kcf_context_t *)buf;

	ASSERT(kctx->kc_refcnt == 0);
	mutex_destroy(&kctx->kc_in_use_lock);
}

void
kcf_sched_destroy(void)
{
	int i;

	if (kcf_misc_kstat)
		kstat_delete(kcf_misc_kstat);

	if (kcfpool) {
		mutex_destroy(&kcfpool->kp_thread_lock);
		cv_destroy(&kcfpool->kp_nothr_cv);
		mutex_destroy(&kcfpool->kp_user_lock);
		cv_destroy(&kcfpool->kp_user_cv);

		kmem_free(kcfpool, sizeof (kcf_pool_t));
	}

	for (i = 0; i < REQID_TABLES; i++) {
		if (kcf_reqid_table[i]) {
			mutex_destroy(&(kcf_reqid_table[i]->rt_lock));
			kmem_free(kcf_reqid_table[i],
			    sizeof (kcf_reqid_table_t));
		}
	}

	if (gswq) {
		mutex_destroy(&gswq->gs_lock);
		cv_destroy(&gswq->gs_cv);
		kmem_free(gswq, sizeof (kcf_global_swq_t));
	}

	if (kcf_context_cache)
		kmem_cache_destroy(kcf_context_cache);
	if (kcf_areq_cache)
		kmem_cache_destroy(kcf_areq_cache);
	if (kcf_sreq_cache)
		kmem_cache_destroy(kcf_sreq_cache);

	mutex_destroy(&ntfy_list_lock);
	cv_destroy(&ntfy_list_cv);
}

/*
 * Creates and initializes all the structures needed by the framework.
 */
void
kcf_sched_init(void)
{
	int i;
	kcf_reqid_table_t *rt;

	/*
	 * Create all the kmem caches needed by the framework. We set the
	 * align argument to 64, to get a slab aligned to 64-byte as well as
	 * have the objects (cache_chunksize) to be a 64-byte multiple.
	 * This helps to avoid false sharing as this is the size of the
	 * CPU cache line.
	 */
	kcf_sreq_cache = kmem_cache_create("kcf_sreq_cache",
	    sizeof (struct kcf_sreq_node), 64, kcf_sreq_cache_constructor,
	    kcf_sreq_cache_destructor, NULL, NULL, NULL, 0);

	kcf_areq_cache = kmem_cache_create("kcf_areq_cache",
	    sizeof (struct kcf_areq_node), 64, kcf_areq_cache_constructor,
	    kcf_areq_cache_destructor, NULL, NULL, NULL, 0);

	kcf_context_cache = kmem_cache_create("kcf_context_cache",
	    sizeof (struct kcf_context), 64, kcf_context_cache_constructor,
	    kcf_context_cache_destructor, NULL, NULL, NULL, 0);

	gswq = kmem_alloc(sizeof (kcf_global_swq_t), KM_SLEEP);

	mutex_init(&gswq->gs_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&gswq->gs_cv, NULL, CV_DEFAULT, NULL);
	gswq->gs_njobs = 0;
	gswq->gs_maxjobs = kcf_maxthreads * crypto_taskq_maxalloc;
	gswq->gs_first = gswq->gs_last = NULL;

	/* Initialize the global reqid table */
	for (i = 0; i < REQID_TABLES; i++) {
		rt = kmem_zalloc(sizeof (kcf_reqid_table_t), KM_SLEEP);
		kcf_reqid_table[i] = rt;
		mutex_init(&rt->rt_lock, NULL, MUTEX_DEFAULT, NULL);
		rt->rt_curid = i;
	}

	/* Allocate and initialize the thread pool */
	kcfpool_alloc();

	/* Initialize the event notification list variables */
	mutex_init(&ntfy_list_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ntfy_list_cv, NULL, CV_DEFAULT, NULL);

	/* Create the kcf kstat */
	kcf_misc_kstat = kstat_create("kcf", 0, "framework_stats", "crypto",
	    KSTAT_TYPE_NAMED, sizeof (kcf_stats_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (kcf_misc_kstat != NULL) {
		kcf_misc_kstat->ks_data = &kcf_ksdata;
		kcf_misc_kstat->ks_update = kcf_misc_kstat_update;
		kstat_install(kcf_misc_kstat);
	}
}

/*
 * Signal the waiting sync client.
 */
void
kcf_sop_done(kcf_sreq_node_t *sreq, int error)
{
	mutex_enter(&sreq->sn_lock);
	sreq->sn_state = REQ_DONE;
	sreq->sn_rv = error;
	cv_signal(&sreq->sn_cv);
	mutex_exit(&sreq->sn_lock);
}

/*
 * Callback the async client with the operation status.
 * We free the async request node and possibly the context.
 * We also handle any chain of requests hanging off of
 * the context.
 */
void
kcf_aop_done(kcf_areq_node_t *areq, int error)
{
	kcf_op_type_t optype;
	boolean_t skip_notify = B_FALSE;
	kcf_context_t *ictx;
	kcf_areq_node_t *nextreq;

	/*
	 * Handle recoverable errors. This has to be done first
	 * before doing any thing else in this routine so that
	 * we do not change the state of the request.
	 */
	if (error != CRYPTO_SUCCESS && IS_RECOVERABLE(error)) {
		/*
		 * We try another provider, if one is available. Else
		 * we continue with the failure notification to the
		 * client.
		 */
		if (kcf_resubmit_request(areq) == CRYPTO_QUEUED)
			return;
	}

	mutex_enter(&areq->an_lock);
	areq->an_state = REQ_DONE;
	mutex_exit(&areq->an_lock);

	optype = (&areq->an_params)->rp_optype;
	if ((ictx = areq->an_context) != NULL) {
		/*
		 * A request after it is removed from the request
		 * queue, still stays on a chain of requests hanging
		 * of its context structure. It needs to be removed
		 * from this chain at this point.
		 */
		mutex_enter(&ictx->kc_in_use_lock);
		nextreq = areq->an_ctxchain_next;
		if (nextreq != NULL) {
			mutex_enter(&nextreq->an_lock);
			nextreq->an_is_my_turn = B_TRUE;
			cv_signal(&nextreq->an_turn_cv);
			mutex_exit(&nextreq->an_lock);
		}

		ictx->kc_req_chain_first = nextreq;
		if (nextreq == NULL)
			ictx->kc_req_chain_last = NULL;
		mutex_exit(&ictx->kc_in_use_lock);

		if (IS_SINGLE_OP(optype) || IS_FINAL_OP(optype)) {
			ASSERT(nextreq == NULL);
			KCF_CONTEXT_REFRELE(ictx);
		} else if (error != CRYPTO_SUCCESS && IS_INIT_OP(optype)) {
		/*
		 * NOTE - We do not release the context in case of update
		 * operations. We require the consumer to free it explicitly,
		 * in case it wants to abandon an update operation. This is done
		 * as there may be mechanisms in ECB mode that can continue
		 * even if an operation on a block fails.
		 */
			KCF_CONTEXT_REFRELE(ictx);
		}
	}

	/* Deal with the internal continuation to this request first */

	if (areq->an_isdual) {
		kcf_dual_req_t *next_arg;
		next_arg = (kcf_dual_req_t *)areq->an_reqarg.cr_callback_arg;
		next_arg->kr_areq = areq;
		KCF_AREQ_REFHOLD(areq);
		areq->an_isdual = B_FALSE;

		NOTIFY_CLIENT(areq, error);
		return;
	}

	/*
	 * If CRYPTO_NOTIFY_OPDONE flag is set, we should notify
	 * always. If this flag is clear, we skip the notification
	 * provided there are no errors.  We check this flag for only
	 * init or update operations. It is ignored for single, final or
	 * atomic operations.
	 */
	skip_notify = (IS_UPDATE_OP(optype) || IS_INIT_OP(optype)) &&
	    (!(areq->an_reqarg.cr_flag & CRYPTO_NOTIFY_OPDONE)) &&
	    (error == CRYPTO_SUCCESS);

	if (!skip_notify) {
		NOTIFY_CLIENT(areq, error);
	}

	if (!(areq->an_reqarg.cr_flag & CRYPTO_SKIP_REQID))
		kcf_reqid_delete(areq);

	KCF_AREQ_REFRELE(areq);
}

/*
 * Allocate the thread pool and initialize all the fields.
 */
static void
kcfpool_alloc()
{
	kcfpool = kmem_alloc(sizeof (kcf_pool_t), KM_SLEEP);

	kcfpool->kp_threads = kcfpool->kp_idlethreads = 0;
	kcfpool->kp_blockedthreads = 0;
	kcfpool->kp_signal_create_thread = B_FALSE;
	kcfpool->kp_nthrs = 0;
	kcfpool->kp_user_waiting = B_FALSE;

	mutex_init(&kcfpool->kp_thread_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&kcfpool->kp_nothr_cv, NULL, CV_DEFAULT, NULL);

	mutex_init(&kcfpool->kp_user_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&kcfpool->kp_user_cv, NULL, CV_DEFAULT, NULL);

	kcf_idlethr_timeout = KCF_DEFAULT_THRTIMEOUT;
}

/*
 * Insert the async request in the hash table after assigning it
 * an ID. Returns the ID.
 *
 * The ID is used by the caller to pass as an argument to a
 * cancel_req() routine later.
 */
static crypto_req_id_t
kcf_reqid_insert(kcf_areq_node_t *areq)
{
	int indx;
	crypto_req_id_t id;
	kcf_areq_node_t *headp;
	kcf_reqid_table_t *rt;

	kpreempt_disable();
	rt = kcf_reqid_table[CPU_SEQID & REQID_TABLE_MASK];
	kpreempt_enable();

	mutex_enter(&rt->rt_lock);

	rt->rt_curid = id =
	    (rt->rt_curid - REQID_COUNTER_LOW) | REQID_COUNTER_HIGH;
	SET_REQID(areq, id);
	indx = REQID_HASH(id);
	headp = areq->an_idnext = rt->rt_idhash[indx];
	areq->an_idprev = NULL;
	if (headp != NULL)
		headp->an_idprev = areq;

	rt->rt_idhash[indx] = areq;
	mutex_exit(&rt->rt_lock);

	return (id);
}

/*
 * Delete the async request from the hash table.
 */
static void
kcf_reqid_delete(kcf_areq_node_t *areq)
{
	int indx;
	kcf_areq_node_t *nextp, *prevp;
	crypto_req_id_t id = GET_REQID(areq);
	kcf_reqid_table_t *rt;

	rt = kcf_reqid_table[id & REQID_TABLE_MASK];
	indx = REQID_HASH(id);

	mutex_enter(&rt->rt_lock);

	nextp = areq->an_idnext;
	prevp = areq->an_idprev;
	if (nextp != NULL)
		nextp->an_idprev = prevp;
	if (prevp != NULL)
		prevp->an_idnext = nextp;
	else
		rt->rt_idhash[indx] = nextp;

	SET_REQID(areq, 0);
	cv_broadcast(&areq->an_done);

	mutex_exit(&rt->rt_lock);
}

/*
 * Cancel a single asynchronous request.
 *
 * We guarantee that no problems will result from calling
 * crypto_cancel_req() for a request which is either running, or
 * has already completed. We remove the request from any queues
 * if it is possible. We wait for request completion if the
 * request is dispatched to a provider.
 *
 * Calling context:
 * 	Can be called from user context only.
 *
 * NOTE: We acquire the following locks in this routine (in order):
 *	- rt_lock (kcf_reqid_table_t)
 *	- gswq->gs_lock
 *	- areq->an_lock
 *	- ictx->kc_in_use_lock (from kcf_removereq_in_ctxchain())
 *
 * This locking order MUST be maintained in code every where else.
 */
void
crypto_cancel_req(crypto_req_id_t id)
{
	int indx;
	kcf_areq_node_t *areq;
	kcf_provider_desc_t *pd;
	kcf_context_t *ictx;
	kcf_reqid_table_t *rt;

	rt = kcf_reqid_table[id & REQID_TABLE_MASK];
	indx = REQID_HASH(id);

	mutex_enter(&rt->rt_lock);
	for (areq = rt->rt_idhash[indx]; areq; areq = areq->an_idnext) {
	if (GET_REQID(areq) == id) {
		/*
		 * We found the request. It is either still waiting
		 * in the framework queues or running at the provider.
		 */
		pd = areq->an_provider;
		ASSERT(pd != NULL);

		switch (pd->pd_prov_type) {
		case CRYPTO_SW_PROVIDER:
			mutex_enter(&gswq->gs_lock);
			mutex_enter(&areq->an_lock);

			/* This request can be safely canceled. */
			if (areq->an_state <= REQ_WAITING) {
				/* Remove from gswq, global software queue. */
				kcf_remove_node(areq);
				if ((ictx = areq->an_context) != NULL)
					kcf_removereq_in_ctxchain(ictx, areq);

				mutex_exit(&areq->an_lock);
				mutex_exit(&gswq->gs_lock);
				mutex_exit(&rt->rt_lock);

				/* Remove areq from hash table and free it. */
				kcf_reqid_delete(areq);
				KCF_AREQ_REFRELE(areq);
				return;
			}

			mutex_exit(&areq->an_lock);
			mutex_exit(&gswq->gs_lock);
			break;

		case CRYPTO_HW_PROVIDER:
			/*
			 * There is no interface to remove an entry
			 * once it is on the taskq. So, we do not do
			 * any thing for a hardware provider.
			 */
			break;
		default:
			break;
		}

		/*
		 * The request is running. Wait for the request completion
		 * to notify us.
		 */
		KCF_AREQ_REFHOLD(areq);
		while (GET_REQID(areq) == id)
			cv_wait(&areq->an_done, &rt->rt_lock);
		KCF_AREQ_REFRELE(areq);
		break;
	}
	}

	mutex_exit(&rt->rt_lock);
}

/*
 * Cancel all asynchronous requests associated with the
 * passed in crypto context and free it.
 *
 * A client SHOULD NOT call this routine after calling a crypto_*_final
 * routine. This routine is called only during intermediate operations.
 * The client should not use the crypto context after this function returns
 * since we destroy it.
 *
 * Calling context:
 * 	Can be called from user context only.
 */
void
crypto_cancel_ctx(crypto_context_t ctx)
{
	kcf_context_t *ictx;
	kcf_areq_node_t *areq;

	if (ctx == NULL)
		return;

	ictx = (kcf_context_t *)((crypto_ctx_t *)ctx)->cc_framework_private;

	mutex_enter(&ictx->kc_in_use_lock);

	/* Walk the chain and cancel each request */
	while ((areq = ictx->kc_req_chain_first) != NULL) {
		/*
		 * We have to drop the lock here as we may have
		 * to wait for request completion. We hold the
		 * request before dropping the lock though, so that it
		 * won't be freed underneath us.
		 */
		KCF_AREQ_REFHOLD(areq);
		mutex_exit(&ictx->kc_in_use_lock);

		crypto_cancel_req(GET_REQID(areq));
		KCF_AREQ_REFRELE(areq);

		mutex_enter(&ictx->kc_in_use_lock);
	}

	mutex_exit(&ictx->kc_in_use_lock);
	KCF_CONTEXT_REFRELE(ictx);
}

/*
 * Update kstats.
 */
static int
kcf_misc_kstat_update(kstat_t *ksp, int rw)
{
	uint_t tcnt;
	kcf_stats_t *ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	ks_data = ksp->ks_data;

	ks_data->ks_thrs_in_pool.value.ui32 = kcfpool->kp_threads;
	/*
	 * The failover thread is counted in kp_idlethreads in
	 * some corner cases. This is done to avoid doing more checks
	 * when submitting a request. We account for those cases below.
	 */
	if ((tcnt = kcfpool->kp_idlethreads) == (kcfpool->kp_threads + 1))
		tcnt--;
	ks_data->ks_idle_thrs.value.ui32 = tcnt;
	ks_data->ks_minthrs.value.ui32 = kcf_minthreads;
	ks_data->ks_maxthrs.value.ui32 = kcf_maxthreads;
	ks_data->ks_swq_njobs.value.ui32 = gswq->gs_njobs;
	ks_data->ks_swq_maxjobs.value.ui32 = gswq->gs_maxjobs;
	ks_data->ks_taskq_threads.value.ui32 = crypto_taskq_threads;
	ks_data->ks_taskq_minalloc.value.ui32 = crypto_taskq_minalloc;
	ks_data->ks_taskq_maxalloc.value.ui32 = crypto_taskq_maxalloc;

	return (0);
}

/*
 * Allocate and initiatize a kcf_dual_req, used for saving the arguments of
 * a dual operation or an atomic operation that has to be internally
 * simulated with multiple single steps.
 * crq determines the memory allocation flags.
 */

kcf_dual_req_t *
kcf_alloc_req(crypto_call_req_t *crq)
{
	kcf_dual_req_t *kcr;

	kcr = kmem_alloc(sizeof (kcf_dual_req_t), KCF_KMFLAG(crq));

	if (kcr == NULL)
		return (NULL);

	/* Copy the whole crypto_call_req struct, as it isn't persistant */
	if (crq != NULL)
		kcr->kr_callreq = *crq;
	else
		bzero(&(kcr->kr_callreq), sizeof (crypto_call_req_t));
	kcr->kr_areq = NULL;
	kcr->kr_saveoffset = 0;
	kcr->kr_savelen = 0;

	return (kcr);
}

/*
 * Callback routine for the next part of a simulated dual part.
 * Schedules the next step.
 *
 * This routine can be called from interrupt context.
 */
void
kcf_next_req(void *next_req_arg, int status)
{
	kcf_dual_req_t *next_req = (kcf_dual_req_t *)next_req_arg;
	kcf_req_params_t *params = &(next_req->kr_params);
	kcf_areq_node_t *areq = next_req->kr_areq;
	int error = status;
	kcf_provider_desc_t *pd = NULL;
	crypto_dual_data_t *ct = NULL;

	/* Stop the processing if an error occured at this step */
	if (error != CRYPTO_SUCCESS) {
out:
		areq->an_reqarg = next_req->kr_callreq;
		KCF_AREQ_REFRELE(areq);
		kmem_free(next_req, sizeof (kcf_dual_req_t));
		areq->an_isdual = B_FALSE;
		kcf_aop_done(areq, error);
		return;
	}

	switch (params->rp_opgrp) {
	case KCF_OG_MAC: {

		/*
		 * The next req is submitted with the same reqid as the
		 * first part. The consumer only got back that reqid, and
		 * should still be able to cancel the operation during its
		 * second step.
		 */
		kcf_mac_ops_params_t *mops = &(params->rp_u.mac_params);
		crypto_ctx_template_t mac_tmpl;
		kcf_mech_entry_t *me;

		ct = (crypto_dual_data_t *)mops->mo_data;
		mac_tmpl = (crypto_ctx_template_t)mops->mo_templ;

		/* No expected recoverable failures, so no retry list */
		pd = kcf_get_mech_provider(mops->mo_framework_mechtype,
		    &me, &error, NULL, CRYPTO_FG_MAC_ATOMIC,
		    (areq->an_reqarg.cr_flag & CRYPTO_RESTRICTED), ct->dd_len2);

		if (pd == NULL) {
			error = CRYPTO_MECH_NOT_SUPPORTED;
			goto out;
		}
		/* Validate the MAC context template here */
		if ((pd->pd_prov_type == CRYPTO_SW_PROVIDER) &&
		    (mac_tmpl != NULL)) {
			kcf_ctx_template_t *ctx_mac_tmpl;

			ctx_mac_tmpl = (kcf_ctx_template_t *)mac_tmpl;

			if (ctx_mac_tmpl->ct_generation != me->me_gen_swprov) {
				KCF_PROV_REFRELE(pd);
				error = CRYPTO_OLD_CTX_TEMPLATE;
				goto out;
			}
			mops->mo_templ = ctx_mac_tmpl->ct_prov_tmpl;
		}

		break;
	}
	case KCF_OG_DECRYPT: {
		kcf_decrypt_ops_params_t *dcrops =
		    &(params->rp_u.decrypt_params);

		ct = (crypto_dual_data_t *)dcrops->dop_ciphertext;
		/* No expected recoverable failures, so no retry list */
		pd = kcf_get_mech_provider(dcrops->dop_framework_mechtype,
		    NULL, &error, NULL, CRYPTO_FG_DECRYPT_ATOMIC,
		    (areq->an_reqarg.cr_flag & CRYPTO_RESTRICTED), ct->dd_len1);

		if (pd == NULL) {
			error = CRYPTO_MECH_NOT_SUPPORTED;
			goto out;
		}
		break;
	}
	default:
		break;
	}

	/* The second step uses len2 and offset2 of the dual_data */
	next_req->kr_saveoffset = ct->dd_offset1;
	next_req->kr_savelen = ct->dd_len1;
	ct->dd_offset1 = ct->dd_offset2;
	ct->dd_len1 = ct->dd_len2;

	/* preserve if the caller is restricted */
	if (areq->an_reqarg.cr_flag & CRYPTO_RESTRICTED) {
		areq->an_reqarg.cr_flag = CRYPTO_RESTRICTED;
	} else {
		areq->an_reqarg.cr_flag = 0;
	}

	areq->an_reqarg.cr_callback_func = kcf_last_req;
	areq->an_reqarg.cr_callback_arg = next_req;
	areq->an_isdual = B_TRUE;

	/*
	 * We would like to call kcf_submit_request() here. But,
	 * that is not possible as that routine allocates a new
	 * kcf_areq_node_t request structure, while we need to
	 * reuse the existing request structure.
	 */
	switch (pd->pd_prov_type) {
	case CRYPTO_SW_PROVIDER:
		error = common_submit_request(pd, NULL, params,
		    KCF_RHNDL(KM_NOSLEEP));
		break;

	case CRYPTO_HW_PROVIDER: {
		kcf_provider_desc_t *old_pd;
		taskq_t *taskq = pd->pd_sched_info.ks_taskq;

		/*
		 * Set the params for the second step in the
		 * dual-ops.
		 */
		areq->an_params = *params;
		old_pd = areq->an_provider;
		KCF_PROV_REFRELE(old_pd);
		KCF_PROV_REFHOLD(pd);
		areq->an_provider = pd;

		/*
		 * Note that we have to do a taskq_dispatch()
		 * here as we may be in interrupt context.
		 */
		if (taskq_dispatch(taskq, process_req_hwp, areq,
		    TQ_NOSLEEP) == (taskqid_t)0) {
			error = CRYPTO_HOST_MEMORY;
		} else {
			error = CRYPTO_QUEUED;
		}
		break;
	}
	default:
		break;
	}

	/*
	 * We have to release the holds on the request and the provider
	 * in all cases.
	 */
	KCF_AREQ_REFRELE(areq);
	KCF_PROV_REFRELE(pd);

	if (error != CRYPTO_QUEUED) {
		/* restore, clean up, and invoke the client's callback */

		ct->dd_offset1 = next_req->kr_saveoffset;
		ct->dd_len1 = next_req->kr_savelen;
		areq->an_reqarg = next_req->kr_callreq;
		kmem_free(next_req, sizeof (kcf_dual_req_t));
		areq->an_isdual = B_FALSE;
		kcf_aop_done(areq, error);
	}
}

/*
 * Last part of an emulated dual operation.
 * Clean up and restore ...
 */
void
kcf_last_req(void *last_req_arg, int status)
{
	kcf_dual_req_t *last_req = (kcf_dual_req_t *)last_req_arg;

	kcf_req_params_t *params = &(last_req->kr_params);
	kcf_areq_node_t *areq = last_req->kr_areq;
	crypto_dual_data_t *ct = NULL;

	switch (params->rp_opgrp) {
	case KCF_OG_MAC: {
		kcf_mac_ops_params_t *mops = &(params->rp_u.mac_params);

		ct = (crypto_dual_data_t *)mops->mo_data;
		break;
	}
	case KCF_OG_DECRYPT: {
		kcf_decrypt_ops_params_t *dcrops =
		    &(params->rp_u.decrypt_params);

		ct = (crypto_dual_data_t *)dcrops->dop_ciphertext;
		break;
	}
	default: {
		panic("invalid kcf_op_group_t %d", (int)params->rp_opgrp);
		return;
	}
	}
	ct->dd_offset1 = last_req->kr_saveoffset;
	ct->dd_len1 = last_req->kr_savelen;

	/* The submitter used kcf_last_req as its callback */

	if (areq == NULL) {
		crypto_call_req_t *cr = &last_req->kr_callreq;

		(*(cr->cr_callback_func))(cr->cr_callback_arg, status);
		kmem_free(last_req, sizeof (kcf_dual_req_t));
		return;
	}
	areq->an_reqarg = last_req->kr_callreq;
	KCF_AREQ_REFRELE(areq);
	kmem_free(last_req, sizeof (kcf_dual_req_t));
	areq->an_isdual = B_FALSE;
	kcf_aop_done(areq, status);
}
