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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_CRYPTO_SCHED_IMPL_H
#define	_SYS_CRYPTO_SCHED_IMPL_H

/*
 * Scheduler internal structures.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/common.h>
#include <sys/crypto/ops_impl.h>

typedef void (kcf_func_t)(void *, int);

typedef enum kcf_req_status {
	REQ_ALLOCATED = 1,
	REQ_WAITING,		/* At the framework level */
	REQ_INPROGRESS,		/* At the provider level */
	REQ_DONE,
	REQ_CANCELED
} kcf_req_status_t;

typedef enum kcf_call_type {
	CRYPTO_SYNCH = 1,
	CRYPTO_ASYNCH
} kcf_call_type_t;

#define	CHECK_RESTRICT(crq) (crq != NULL &&	\
	((crq)->cr_flag & CRYPTO_RESTRICTED))

#define	CHECK_RESTRICT_FALSE	B_FALSE

#define	CHECK_FASTPATH(crq, pd) ((crq) == NULL ||	\
	!((crq)->cr_flag & CRYPTO_ALWAYS_QUEUE)) &&	\
	(pd)->pd_prov_type == CRYPTO_SW_PROVIDER

#define	KCF_KMFLAG(crq)	(((crq) == NULL) ? KM_SLEEP : KM_NOSLEEP)

/*
 * The framework keeps an internal handle to use in the adaptive
 * asynchronous case. This is the case when a client has the
 * CRYPTO_ALWAYS_QUEUE bit clear and a software provider is used for
 * the request. The request is completed in the context of the calling
 * thread and kernel memory must be allocated with KM_NOSLEEP.
 *
 * The framework passes a pointer to the handle in crypto_req_handle_t
 * argument when it calls the SPI of the software provider. The macros
 * KCF_RHNDL() and KCF_SWFP_RHNDL() are used to do this.
 *
 * When a provider asks the framework for kmflag value via
 * crypto_kmflag(9S) we use REQHNDL2_KMFLAG() macro.
 */
extern ulong_t kcf_swprov_hndl;
#define	KCF_RHNDL(kmflag) (((kmflag) == KM_SLEEP) ? NULL : &kcf_swprov_hndl)
#define	KCF_SWFP_RHNDL(crq) (((crq) == NULL) ? NULL : &kcf_swprov_hndl)
#define	REQHNDL2_KMFLAG(rhndl) \
	((rhndl == &kcf_swprov_hndl) ? KM_NOSLEEP : KM_SLEEP)

/* Internal call_req flags. They start after the public ones in api.h */

#define	CRYPTO_SETDUAL	0x00001000	/* Set the 'cont' boolean before */
					/* submitting the request */
#define	KCF_ISDUALREQ(crq)	\
	(((crq) == NULL) ? B_FALSE : (crq->cr_flag & CRYPTO_SETDUAL))

typedef struct kcf_prov_tried {
	kcf_provider_desc_t	*pt_pd;
	struct kcf_prov_tried	*pt_next;
} kcf_prov_tried_t;

#define	IS_FG_SUPPORTED(mdesc, fg)		\
	(((mdesc)->pm_mech_info.cm_func_group_mask & (fg)) != 0)

#define	IS_PROVIDER_TRIED(pd, tlist)		\
	(tlist != NULL && is_in_triedlist(pd, tlist))

#define	IS_RECOVERABLE(error)			\
	(error == CRYPTO_BUFFER_TOO_BIG ||	\
	error == CRYPTO_BUSY ||			\
	error == CRYPTO_DEVICE_ERROR ||		\
	error == CRYPTO_DEVICE_MEMORY ||	\
	error == CRYPTO_KEY_SIZE_RANGE ||	\
	error == CRYPTO_NO_PERMISSION)

#define	KCF_ATOMIC_INCR(x)	atomic_add_32(&(x), 1)
#define	KCF_ATOMIC_DECR(x)	atomic_add_32(&(x), -1)

/*
 * Node structure for synchronous requests.
 */
typedef struct kcf_sreq_node {
	/* Should always be the first field in this structure */
	kcf_call_type_t		sn_type;
	/*
	 * sn_cv and sr_lock are used to wait for the
	 * operation to complete. sn_lock also protects
	 * the sn_state field.
	 */
	kcondvar_t		sn_cv;
	kmutex_t		sn_lock;
	kcf_req_status_t	sn_state;

	/*
	 * Return value from the operation. This will be
	 * one of the CRYPTO_* errors defined in common.h.
	 */
	int			sn_rv;

	/*
	 * parameters to call the SPI with. This can be
	 * a pointer as we know the caller context/stack stays.
	 */
	struct kcf_req_params	*sn_params;

	/* Internal context for this request */
	struct kcf_context	*sn_context;

	/* Provider handling this request */
	kcf_provider_desc_t	*sn_provider;
} kcf_sreq_node_t;

/*
 * Node structure for asynchronous requests. A node can be on
 * on a chain of requests hanging of the internal context
 * structure and can be in the global software provider queue.
 */
typedef struct kcf_areq_node {
	/* Should always be the first field in this structure */
	kcf_call_type_t		an_type;

	/* an_lock protects the field an_state  */
	kmutex_t		an_lock;
	kcf_req_status_t	an_state;
	crypto_call_req_t	an_reqarg;

	/*
	 * parameters to call the SPI with. We need to
	 * save the params since the caller stack can go away.
	 */
	struct kcf_req_params	an_params;

	/*
	 * The next two fields should be NULL for operations that
	 * don't need a context.
	 */
	/* Internal context for this request */
	struct kcf_context	*an_context;

	/* next in chain of requests for context */
	struct kcf_areq_node	*an_ctxchain_next;

	kcondvar_t		an_turn_cv;
	boolean_t		an_is_my_turn;
	boolean_t		an_isdual;	/* for internal reuse */

	/*
	 * Next and previous nodes in the global software
	 * queue. These fields are NULL for a hardware
	 * provider since we use a taskq there.
	 */
	struct kcf_areq_node	*an_next;
	struct kcf_areq_node	*an_prev;

	/* Provider handling this request */
	kcf_provider_desc_t	*an_provider;
	kcf_prov_tried_t	*an_tried_plist;

	struct kcf_areq_node	*an_idnext;	/* Next in ID hash */
	struct kcf_areq_node	*an_idprev;	/* Prev in ID hash */
	kcondvar_t		an_done;	/* Signal request completion */
	uint_t			an_refcnt;
} kcf_areq_node_t;

#define	KCF_AREQ_REFHOLD(areq) {		\
	atomic_add_32(&(areq)->an_refcnt, 1);	\
	ASSERT((areq)->an_refcnt != 0);		\
}

#define	KCF_AREQ_REFRELE(areq) {				\
	ASSERT((areq)->an_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(areq)->an_refcnt, -1) == 0)	\
		kcf_free_req(areq);				\
}

#define	GET_REQ_TYPE(arg) *((kcf_call_type_t *)(arg))

#define	NOTIFY_CLIENT(areq, err) (*(areq)->an_reqarg.cr_callback_func)(\
	(areq)->an_reqarg.cr_callback_arg, err);

/* For internally generated call requests for dual operations */
typedef	struct kcf_call_req {
	crypto_call_req_t	kr_callreq;	/* external client call req */
	kcf_req_params_t	kr_params;	/* Params saved for next call */
	kcf_areq_node_t		*kr_areq;	/* Use this areq */
	off_t			kr_saveoffset;
	size_t			kr_savelen;
} kcf_dual_req_t;

/*
 * The following are some what similar to macros in callo.h, which implement
 * callout tables.
 *
 * The lower four bits of the ID are used to encode the table ID to
 * index in to. The REQID_COUNTER_HIGH bit is used to avoid any check for
 * wrap around when generating ID. We assume that there won't be a request
 * which takes more time than 2^^(sizeof (long) - 5) other requests submitted
 * after it. This ensures there won't be any ID collision.
 */
#define	REQID_COUNTER_HIGH	(1UL << (8 * sizeof (long) - 1))
#define	REQID_COUNTER_SHIFT	4
#define	REQID_COUNTER_LOW	(1 << REQID_COUNTER_SHIFT)
#define	REQID_TABLES		16
#define	REQID_TABLE_MASK	(REQID_TABLES - 1)

#define	REQID_BUCKETS		512
#define	REQID_BUCKET_MASK	(REQID_BUCKETS - 1)
#define	REQID_HASH(id)	(((id) >> REQID_COUNTER_SHIFT) & REQID_BUCKET_MASK)

#define	GET_REQID(areq) (areq)->an_reqarg.cr_reqid
#define	SET_REQID(areq, val)	GET_REQID(areq) = val

/*
 * Hash table for async requests.
 */
typedef struct kcf_reqid_table {
	kmutex_t		rt_lock;
	crypto_req_id_t		rt_curid;
	kcf_areq_node_t		*rt_idhash[REQID_BUCKETS];
} kcf_reqid_table_t;

/*
 * Global software provider queue structure. Requests to be
 * handled by a SW provider and have the ALWAYS_QUEUE flag set
 * get queued here.
 */
typedef struct kcf_global_swq {
	/*
	 * gs_cv and gs_lock are used to wait for new requests.
	 * gs_lock protects the changes to the queue.
	 */
	kcondvar_t		gs_cv;
	kmutex_t		gs_lock;
	uint_t			gs_njobs;
	uint_t			gs_maxjobs;
	kcf_areq_node_t		*gs_first;
	kcf_areq_node_t		*gs_last;
} kcf_global_swq_t;


/*
 * Internal representation of a canonical context. We contain crypto_ctx_t
 * structure in order to have just one memory allocation. The SPI
 * ((crypto_ctx_t *)ctx)->cc_framework_private maps to this structure.
 */
typedef struct kcf_context {
	crypto_ctx_t		kc_glbl_ctx;
	uint_t			kc_refcnt;
	kmutex_t		kc_in_use_lock;
	/*
	 * kc_req_chain_first and kc_req_chain_last are used to chain
	 * multiple async requests using the same context. They should be
	 * NULL for sync requests.
	 */
	kcf_areq_node_t		*kc_req_chain_first;
	kcf_areq_node_t		*kc_req_chain_last;
	kcf_provider_desc_t	*kc_prov_desc;	/* Prov. descriptor */
	kcf_provider_desc_t	*kc_sw_prov_desc;	/* Prov. descriptor */
	kcf_mech_entry_t	*kc_mech;
	struct kcf_context	*kc_secondctx;	/* for dual contexts */
} kcf_context_t;

/*
 * Bump up the reference count on the framework private context. A
 * global context or a request that references this structure should
 * do a hold.
 */
#define	KCF_CONTEXT_REFHOLD(ictx) {		\
	atomic_add_32(&(ictx)->kc_refcnt, 1);	\
	ASSERT((ictx)->kc_refcnt != 0);		\
}

/*
 * Decrement the reference count on the framework private context.
 * When the last reference is released, the framework private
 * context structure is freed along with the global context.
 */
#define	KCF_CONTEXT_REFRELE(ictx) {				\
	ASSERT((ictx)->kc_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(ictx)->kc_refcnt, -1) == 0)	\
		kcf_free_context(ictx);				\
}

/*
 * Check if we can release the context now. In case of CRYPTO_QUEUED
 * we do not release it as we can do it only after the provider notified
 * us. In case of CRYPTO_BUSY, the client can retry the request using
 * the context, so we do not release the context.
 *
 * This macro should be called only from the final routine in
 * an init/update/final sequence. We do not release the context in case
 * of update operations. We require the consumer to free it
 * explicitly, in case it wants to abandon the operation. This is done
 * as there may be mechanisms in ECB mode that can continue even if
 * an operation on a block fails.
 */
#define	KCF_CONTEXT_COND_RELEASE(rv, kcf_ctx) {			\
	if (KCF_CONTEXT_DONE(rv))				\
		KCF_CONTEXT_REFRELE(kcf_ctx);			\
}

/*
 * This macro determines whether we're done with a context.
 */
#define	KCF_CONTEXT_DONE(rv)					\
	((rv) != CRYPTO_QUEUED && (rv) != CRYPTO_BUSY &&	\
	    (rv) != CRYPTO_BUFFER_TOO_SMALL)

/*
 * A crypto_ctx_template_t is internally a pointer to this struct
 */
typedef	struct kcf_ctx_template {
	crypto_kcf_provider_handle_t	ct_prov_handle;	/* provider handle */
	uint_t				ct_generation;	/* generation # */
	size_t				ct_size;	/* for freeing */
	crypto_spi_ctx_template_t	ct_prov_tmpl;	/* context template */
							/* from the SW prov */
} kcf_ctx_template_t;

/*
 * Structure for pool of threads working on global software queue.
 */
typedef struct kcf_pool {
	uint32_t	kp_threads;		/* Number of threads in pool */
	uint32_t	kp_idlethreads;		/* Idle threads in pool */
	uint32_t	kp_blockedthreads;	/* Blocked threads in pool */

	/*
	 * cv & lock to monitor the condition when no threads
	 * are around. In this case the failover thread kicks in.
	 */
	kcondvar_t	kp_nothr_cv;
	kmutex_t	kp_thread_lock;

	/* Userspace thread creator variables. */
	boolean_t	kp_signal_create_thread; /* Create requested flag  */
	int		kp_nthrs;		/* # of threads to create */
	boolean_t	kp_user_waiting;	/* Thread waiting for work */

	/*
	 * cv & lock for the condition where more threads need to be
	 * created. kp_user_lock also protects the three fileds above.
	 */
	kcondvar_t	kp_user_cv;		/* Creator cond. variable */
	kmutex_t	kp_user_lock;		/* Creator lock */
} kcf_pool_t;


/*
 * State of a crypto bufcall element.
 */
typedef enum cbuf_state {
	CBUF_FREE = 1,
	CBUF_WAITING,
	CBUF_RUNNING
} cbuf_state_t;

/*
 * Structure of a crypto bufcall element.
 */
typedef struct kcf_cbuf_elem {
	/*
	 * lock and cv to wait for CBUF_RUNNING to be done
	 * kc_lock also protects kc_state.
	 */
	kmutex_t		kc_lock;
	kcondvar_t		kc_cv;
	cbuf_state_t		kc_state;

	struct kcf_cbuf_elem	*kc_next;
	struct kcf_cbuf_elem	*kc_prev;

	void			(*kc_func)(void *arg);
	void			*kc_arg;
} kcf_cbuf_elem_t;

/*
 * State of a notify element.
 */
typedef enum ntfy_elem_state {
	NTFY_WAITING = 1,
	NTFY_RUNNING
} ntfy_elem_state_t;

/*
 * Structure of a notify list element.
 */
typedef struct kcf_ntfy_elem {
	/*
	 * lock and cv to wait for NTFY_RUNNING to be done.
	 * kn_lock also protects kn_state.
	 */
	kmutex_t			kn_lock;
	kcondvar_t			kn_cv;
	ntfy_elem_state_t		kn_state;

	struct kcf_ntfy_elem		*kn_next;
	struct kcf_ntfy_elem		*kn_prev;

	crypto_notify_callback_t	kn_func;
	uint32_t			kn_event_mask;
} kcf_ntfy_elem_t;


/*
 * The following values are based on the assumption that it would
 * take around eight cpus to load a hardware provider (This is true for
 * at least one product) and a kernel client may come from different
 * low-priority interrupt levels. We will have CYRPTO_TASKQ_MIN number
 * of cached taskq entries. The CRYPTO_TASKQ_MAX number is based on
 * a throughput of 1GB/s using 512-byte buffers. These are just
 * reasonable estimates and might need to change in future.
 */
#define	CRYPTO_TASKQ_THREADS	8
#define	CYRPTO_TASKQ_MIN	64
#define	CRYPTO_TASKQ_MAX	2 * 1024 * 1024

extern int crypto_taskq_threads;
extern int crypto_taskq_minalloc;
extern int crypto_taskq_maxalloc;
extern kcf_global_swq_t *gswq;
extern int kcf_maxthreads;
extern int kcf_minthreads;

/*
 * All pending crypto bufcalls are put on a list. cbuf_list_lock
 * protects changes to this list.
 */
extern kmutex_t cbuf_list_lock;
extern kcondvar_t cbuf_list_cv;

/*
 * All event subscribers are put on a list. kcf_notify_list_lock
 * protects changes to this list.
 */
extern kmutex_t ntfy_list_lock;
extern kcondvar_t ntfy_list_cv;

boolean_t kcf_get_next_logical_provider_member(kcf_provider_desc_t *,
    kcf_provider_desc_t *, kcf_provider_desc_t **);
extern int kcf_get_hardware_provider(crypto_mech_type_t, crypto_mech_type_t,
    boolean_t, kcf_provider_desc_t *, kcf_provider_desc_t **,
    crypto_func_group_t);
extern int kcf_get_hardware_provider_nomech(offset_t, offset_t,
    boolean_t, kcf_provider_desc_t *, kcf_provider_desc_t **);
extern void kcf_free_triedlist(kcf_prov_tried_t *);
extern kcf_prov_tried_t *kcf_insert_triedlist(kcf_prov_tried_t **,
    kcf_provider_desc_t *, int);
extern kcf_provider_desc_t *kcf_get_mech_provider(crypto_mech_type_t,
    kcf_mech_entry_t **, int *, kcf_prov_tried_t *, crypto_func_group_t,
    boolean_t, size_t);
extern kcf_provider_desc_t *kcf_get_dual_provider(crypto_mechanism_t *,
    crypto_mechanism_t *, kcf_mech_entry_t **, crypto_mech_type_t *,
    crypto_mech_type_t *, int *, kcf_prov_tried_t *,
    crypto_func_group_t, crypto_func_group_t, boolean_t, size_t);
extern crypto_ctx_t *kcf_new_ctx(crypto_call_req_t  *, kcf_provider_desc_t *,
    crypto_session_id_t);
extern int kcf_submit_request(kcf_provider_desc_t *, crypto_ctx_t *,
    crypto_call_req_t *, kcf_req_params_t *, boolean_t);
extern void kcf_sched_destroy(void);
extern void kcf_sched_init(void);
extern void kcf_sched_start(void);
extern void kcf_sop_done(kcf_sreq_node_t *, int);
extern void kcf_aop_done(kcf_areq_node_t *, int);
extern int common_submit_request(kcf_provider_desc_t *,
    crypto_ctx_t *, kcf_req_params_t *, crypto_req_handle_t);
extern void kcf_free_context(kcf_context_t *);

extern int kcf_svc_wait(int *);
extern int kcf_svc_do_run(void);
extern int kcf_need_signature_verification(kcf_provider_desc_t *);
extern void kcf_verify_signature(void *);
extern struct modctl *kcf_get_modctl(crypto_provider_info_t *);
extern void verify_unverified_providers(void);
extern void kcf_free_req(kcf_areq_node_t *areq);
extern void crypto_bufcall_service(void);

extern void kcf_walk_ntfylist(uint32_t, void *);
extern void kcf_do_notify(kcf_provider_desc_t *, boolean_t);

extern kcf_dual_req_t *kcf_alloc_req(crypto_call_req_t *);
extern void kcf_next_req(void *, int);
extern void kcf_last_req(void *, int);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CRYPTO_SCHED_IMPL_H */
