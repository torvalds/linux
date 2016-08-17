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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CRYPTO_IMPL_H
#define	_SYS_CRYPTO_IMPL_H

/*
 * Kernel Cryptographic Framework private implementation definitions.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/ioctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	KCF_MODULE "kcf"

/*
 * Prefixes convention: structures internal to the kernel cryptographic
 * framework start with 'kcf_'. Exposed structure start with 'crypto_'.
 */

/* Provider stats. Not protected. */
typedef	struct kcf_prov_stats {
	kstat_named_t	ps_ops_total;
	kstat_named_t	ps_ops_passed;
	kstat_named_t	ps_ops_failed;
	kstat_named_t	ps_ops_busy_rval;
} kcf_prov_stats_t;

/* Various kcf stats. Not protected. */
typedef	struct kcf_stats {
	kstat_named_t	ks_thrs_in_pool;
	kstat_named_t	ks_idle_thrs;
	kstat_named_t	ks_minthrs;
	kstat_named_t	ks_maxthrs;
	kstat_named_t	ks_swq_njobs;
	kstat_named_t	ks_swq_maxjobs;
	kstat_named_t	ks_taskq_threads;
	kstat_named_t	ks_taskq_minalloc;
	kstat_named_t	ks_taskq_maxalloc;
} kcf_stats_t;

/*
 * Keep all the information needed by the scheduler from
 * this provider.
 */
typedef struct kcf_sched_info {
	/* The number of operations dispatched. */
	uint64_t	ks_ndispatches;

	/* The number of operations that failed. */
	uint64_t	ks_nfails;

	/* The number of operations that returned CRYPTO_BUSY. */
	uint64_t	ks_nbusy_rval;

	/* taskq used to dispatch crypto requests */
	taskq_t	*ks_taskq;
} kcf_sched_info_t;

/*
 * pd_irefcnt approximates the number of inflight requests to the
 * provider. Though we increment this counter during registration for
 * other purposes, that base value is mostly same across all providers.
 * So, it is a good measure of the load on a provider when it is not
 * in a busy state. Once a provider notifies it is busy, requests
 * backup in the taskq. So, we use tq_nalloc in that case which gives
 * the number of task entries in the task queue. Note that we do not
 * acquire any locks here as it is not critical to get the exact number
 * and the lock contention may be too costly for this code path.
 */
#define	KCF_PROV_LOAD(pd)	((pd)->pd_state != KCF_PROV_BUSY ?	\
	(pd)->pd_irefcnt : (pd)->pd_sched_info.ks_taskq->tq_nalloc)

#define	KCF_PROV_INCRSTATS(pd, error)	{				\
	(pd)->pd_sched_info.ks_ndispatches++;				\
	if (error == CRYPTO_BUSY)					\
		(pd)->pd_sched_info.ks_nbusy_rval++;			\
	else if (error != CRYPTO_SUCCESS && error != CRYPTO_QUEUED)	\
		(pd)->pd_sched_info.ks_nfails++;			\
}


/*
 * The following two macros should be
 * #define	KCF_OPS_CLASSSIZE (KCF_LAST_OPSCLASS - KCF_FIRST_OPSCLASS + 2)
 * #define	KCF_MAXMECHTAB KCF_MAXCIPHER
 *
 * However, doing that would involve reorganizing the header file a bit.
 * When impl.h is broken up (bug# 4703218), this will be done. For now,
 * we hardcode these values.
 */
#define	KCF_OPS_CLASSSIZE	8
#define	KCF_MAXMECHTAB		32

/*
 * Valid values for the state of a provider. The order of
 * the elements is important.
 *
 * Routines which get a provider or the list of providers
 * should pick only those that are either in KCF_PROV_READY state
 * or in KCF_PROV_BUSY state.
 */
typedef enum {
	KCF_PROV_ALLOCATED = 1,
	KCF_PROV_UNVERIFIED,
	KCF_PROV_VERIFICATION_FAILED,
	/*
	 * state < KCF_PROV_READY means the provider can not
	 * be used at all.
	 */
	KCF_PROV_READY,
	KCF_PROV_BUSY,
	/*
	 * state > KCF_PROV_BUSY means the provider can not
	 * be used for new requests.
	 */
	KCF_PROV_FAILED,
	/*
	 * Threads setting the following two states should do so only
	 * if the current state < KCF_PROV_DISABLED.
	 */
	KCF_PROV_DISABLED,
	KCF_PROV_REMOVED,
	KCF_PROV_FREED
} kcf_prov_state_t;

#define	KCF_IS_PROV_UNVERIFIED(pd) ((pd)->pd_state == KCF_PROV_UNVERIFIED)
#define	KCF_IS_PROV_USABLE(pd) ((pd)->pd_state == KCF_PROV_READY || \
	(pd)->pd_state == KCF_PROV_BUSY)
#define	KCF_IS_PROV_REMOVED(pd)	((pd)->pd_state >= KCF_PROV_REMOVED)

/* Internal flags valid for pd_flags field */
#define	KCF_PROV_RESTRICTED	0x40000000
#define	KCF_LPROV_MEMBER	0x80000000 /* is member of a logical provider */

/*
 * A provider descriptor structure. There is one such structure per
 * provider. It is allocated and initialized at registration time and
 * freed when the provider unregisters.
 *
 * pd_prov_type:	Provider type, hardware or software
 * pd_sid:		Session ID of the provider used by kernel clients.
 *			This is valid only for session-oriented providers.
 * pd_refcnt:		Reference counter to this provider descriptor
 * pd_irefcnt:		References held by the framework internal structs
 * pd_lock:		lock protects pd_state and pd_provider_list
 * pd_state:		State value of the provider
 * pd_provider_list:	Used to cross-reference logical providers and their
 *			members. Not used for software providers.
 * pd_resume_cv:	cv to wait for state to change from KCF_PROV_BUSY
 * pd_prov_handle:	Provider handle specified by provider
 * pd_ops_vector:	The ops vector specified by Provider
 * pd_mech_indx:	Lookup table which maps a core framework mechanism
 *			number to an index in pd_mechanisms array
 * pd_mechanisms:	Array of mechanisms supported by the provider, specified
 *			by the provider during registration
 * pd_sched_info:	Scheduling information associated with the provider
 * pd_mech_list_count:	The number of entries in pi_mechanisms, specified
 *			by the provider during registration
 * pd_name:		Device name or module name
 * pd_instance:		Device instance
 * pd_module_id:	Module ID returned by modload
 * pd_mctlp:		Pointer to modctl structure for this provider
 * pd_remove_cv:	cv to wait on while the provider queue drains
 * pd_description:	Provider description string
 * pd_flags		bitwise OR of pi_flags from crypto_provider_info_t
 *			and other internal flags defined above.
 * pd_hash_limit	Maximum data size that hash mechanisms of this provider
 * 			can support.
 * pd_kcf_prov_handle:	KCF-private handle assigned by KCF
 * pd_prov_id:		Identification # assigned by KCF to provider
 * pd_kstat:		kstat associated with the provider
 * pd_ks_data:		kstat data
 */
typedef struct kcf_provider_desc {
	crypto_provider_type_t		pd_prov_type;
	crypto_session_id_t		pd_sid;
	uint_t				pd_refcnt;
	uint_t				pd_irefcnt;
	kmutex_t			pd_lock;
	kcf_prov_state_t		pd_state;
	struct kcf_provider_list	*pd_provider_list;
	kcondvar_t			pd_resume_cv;
	crypto_provider_handle_t	pd_prov_handle;
	crypto_ops_t			*pd_ops_vector;
	ushort_t			pd_mech_indx[KCF_OPS_CLASSSIZE]\
					    [KCF_MAXMECHTAB];
	crypto_mech_info_t		*pd_mechanisms;
	kcf_sched_info_t		pd_sched_info;
	uint_t				pd_mech_list_count;
	// char				*pd_name;
	// uint_t				pd_instance;
	// int				pd_module_id;
	// struct modctl			*pd_mctlp;
	kcondvar_t			pd_remove_cv;
	char				*pd_description;
	uint_t				pd_flags;
	uint_t				pd_hash_limit;
	crypto_kcf_provider_handle_t	pd_kcf_prov_handle;
	crypto_provider_id_t		pd_prov_id;
	kstat_t				*pd_kstat;
	kcf_prov_stats_t		pd_ks_data;
} kcf_provider_desc_t;

/* useful for making a list of providers */
typedef struct kcf_provider_list {
	struct kcf_provider_list *pl_next;
	struct kcf_provider_desc *pl_provider;
} kcf_provider_list_t;

/* atomic operations in linux implictly form a memory barrier */
#define	membar_exit()

/*
 * If a component has a reference to a kcf_provider_desc_t,
 * it REFHOLD()s. A new provider descriptor which is referenced only
 * by the providers table has a reference counter of one.
 */
#define	KCF_PROV_REFHOLD(desc) {		\
	atomic_add_32(&(desc)->pd_refcnt, 1);	\
	ASSERT((desc)->pd_refcnt != 0);		\
}

#define	KCF_PROV_IREFHOLD(desc) {		\
	atomic_add_32(&(desc)->pd_irefcnt, 1);	\
	ASSERT((desc)->pd_irefcnt != 0);	\
}

#define	KCF_PROV_IREFRELE(desc) {				\
	ASSERT((desc)->pd_irefcnt != 0);			\
	membar_exit();						\
	if (atomic_add_32_nv(&(desc)->pd_irefcnt, -1) == 0) {	\
		cv_broadcast(&(desc)->pd_remove_cv);		\
	}							\
}

#define	KCF_PROV_REFHELD(desc)	((desc)->pd_refcnt >= 1)

#define	KCF_PROV_REFRELE(desc) {				\
	ASSERT((desc)->pd_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(desc)->pd_refcnt, -1) == 0) {	\
		kcf_provider_zero_refcnt((desc));		\
	}							\
}


/* list of crypto_mech_info_t valid as the second mech in a dual operation */

typedef	struct crypto_mech_info_list {
	struct crypto_mech_info_list	*ml_next;
	crypto_mech_type_t		ml_kcf_mechid;	/* KCF's id */
	crypto_mech_info_t		ml_mech_info;
} crypto_mech_info_list_t;

/*
 * An element in a mechanism provider descriptors chain.
 * The kcf_prov_mech_desc_t is duplicated in every chain the provider belongs
 * to. This is a small tradeoff memory vs mutex spinning time to access the
 * common provider field.
 */

typedef struct kcf_prov_mech_desc {
	struct kcf_mech_entry		*pm_me;		/* Back to the head */
	struct kcf_prov_mech_desc	*pm_next;	/* Next in the chain */
	crypto_mech_info_t		pm_mech_info;	/* Provider mech info */
	crypto_mech_info_list_t		*pm_mi_list;	/* list for duals */
	kcf_provider_desc_t		*pm_prov_desc;	/* Common desc. */
} kcf_prov_mech_desc_t;

/* and the notation shortcuts ... */
#define	pm_provider_type	pm_prov_desc.pd_provider_type
#define	pm_provider_handle	pm_prov_desc.pd_provider_handle
#define	pm_ops_vector		pm_prov_desc.pd_ops_vector

/*
 * A mechanism entry in an xxx_mech_tab[]. me_pad was deemed
 * to be unnecessary and removed.
 */
typedef	struct kcf_mech_entry {
	crypto_mech_name_t	me_name;	/* mechanism name */
	crypto_mech_type_t	me_mechid;	/* Internal id for mechanism */
	kmutex_t		me_mutex;	/* access protection	*/
	kcf_prov_mech_desc_t	*me_hw_prov_chain;  /* list of HW providers */
	kcf_prov_mech_desc_t	*me_sw_prov;    /* SW provider */
	/*
	 * Number of HW providers in the chain. There is only one
	 * SW provider. So, we need only a count of HW providers.
	 */
	int			me_num_hwprov;
	/*
	 * When a SW provider is present, this is the generation number that
	 * ensures no objects from old SW providers are used in the new one
	 */
	uint32_t		me_gen_swprov;
	/*
	 *  threshold for using hardware providers for this mech
	 */
	size_t			me_threshold;
} kcf_mech_entry_t;

/*
 * A policy descriptor structure. It is allocated and initialized
 * when administrative ioctls load disabled mechanisms.
 *
 * pd_prov_type:	Provider type, hardware or software
 * pd_name:		Device name or module name.
 * pd_instance:		Device instance.
 * pd_refcnt:		Reference counter for this policy descriptor
 * pd_mutex:		Protects array and count of disabled mechanisms.
 * pd_disabled_count:	Count of disabled mechanisms.
 * pd_disabled_mechs:	Array of disabled mechanisms.
 */
typedef struct kcf_policy_desc {
	crypto_provider_type_t	pd_prov_type;
	char			*pd_name;
	uint_t			pd_instance;
	uint_t			pd_refcnt;
	kmutex_t		pd_mutex;
	uint_t			pd_disabled_count;
	crypto_mech_name_t	*pd_disabled_mechs;
} kcf_policy_desc_t;

/*
 * If a component has a reference to a kcf_policy_desc_t,
 * it REFHOLD()s. A new policy descriptor which is referenced only
 * by the policy table has a reference count of one.
 */
#define	KCF_POLICY_REFHOLD(desc) {		\
	atomic_add_32(&(desc)->pd_refcnt, 1);	\
	ASSERT((desc)->pd_refcnt != 0);		\
}

/*
 * Releases a reference to a policy descriptor. When the last
 * reference is released, the descriptor is freed.
 */
#define	KCF_POLICY_REFRELE(desc) {				\
	ASSERT((desc)->pd_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(desc)->pd_refcnt, -1) == 0)	\
		kcf_policy_free_desc(desc);			\
}

/*
 * This entry stores the name of a software module and its
 * mechanisms.  The mechanisms are 'hints' that are used to
 * trigger loading of the module.
 */
typedef struct kcf_soft_conf_entry {
	struct kcf_soft_conf_entry	*ce_next;
	char				*ce_name;
	crypto_mech_name_t		*ce_mechs;
	uint_t				ce_count;
} kcf_soft_conf_entry_t;

extern kmutex_t soft_config_mutex;
extern kcf_soft_conf_entry_t *soft_config_list;

/*
 * Global tables. The sizes are from the predefined PKCS#11 v2.20 mechanisms,
 * with a margin of few extra empty entry points
 */

#define	KCF_MAXDIGEST		16	/* Digests */
#define	KCF_MAXCIPHER		64	/* Ciphers */
#define	KCF_MAXMAC		40	/* Message authentication codes */
#define	KCF_MAXSIGN		24	/* Sign/Verify */
#define	KCF_MAXKEYOPS		116	/* Key generation and derivation */
#define	KCF_MAXMISC		16	/* Others ... */

#define	KCF_MAXMECHS		KCF_MAXDIGEST + KCF_MAXCIPHER + KCF_MAXMAC + \
				KCF_MAXSIGN + KCF_MAXKEYOPS + \
				KCF_MAXMISC

extern kcf_mech_entry_t kcf_digest_mechs_tab[];
extern kcf_mech_entry_t kcf_cipher_mechs_tab[];
extern kcf_mech_entry_t kcf_mac_mechs_tab[];
extern kcf_mech_entry_t kcf_sign_mechs_tab[];
extern kcf_mech_entry_t kcf_keyops_mechs_tab[];
extern kcf_mech_entry_t kcf_misc_mechs_tab[];

extern kmutex_t kcf_mech_tabs_lock;

typedef	enum {
	KCF_DIGEST_CLASS = 1,
	KCF_CIPHER_CLASS,
	KCF_MAC_CLASS,
	KCF_SIGN_CLASS,
	KCF_KEYOPS_CLASS,
	KCF_MISC_CLASS
} kcf_ops_class_t;

#define	KCF_FIRST_OPSCLASS	KCF_DIGEST_CLASS
#define	KCF_LAST_OPSCLASS	KCF_MISC_CLASS

/* The table of all the kcf_xxx_mech_tab[]s, indexed by kcf_ops_class */

typedef	struct kcf_mech_entry_tab {
	int			met_size;	/* Size of the met_tab[] */
	kcf_mech_entry_t	*met_tab;	/* the table		 */
} kcf_mech_entry_tab_t;

extern kcf_mech_entry_tab_t kcf_mech_tabs_tab[];

#define	KCF_MECHID(class, index)				\
	(((crypto_mech_type_t)(class) << 32) | (crypto_mech_type_t)(index))

#define	KCF_MECH2CLASS(mech_type) ((kcf_ops_class_t)((mech_type) >> 32))

#define	KCF_MECH2INDEX(mech_type) ((int)(mech_type))

#define	KCF_TO_PROV_MECH_INDX(pd, mech_type) 			\
	((pd)->pd_mech_indx[KCF_MECH2CLASS(mech_type)] 		\
	[KCF_MECH2INDEX(mech_type)])

#define	KCF_TO_PROV_MECHINFO(pd, mech_type)			\
	((pd)->pd_mechanisms[KCF_TO_PROV_MECH_INDX(pd, mech_type)])

#define	KCF_TO_PROV_MECHNUM(pd, mech_type)			\
	(KCF_TO_PROV_MECHINFO(pd, mech_type).cm_mech_number)

#define	KCF_CAN_SHARE_OPSTATE(pd, mech_type)			\
	((KCF_TO_PROV_MECHINFO(pd, mech_type).cm_mech_flags) &	\
	CRYPTO_CAN_SHARE_OPSTATE)

/* ps_refcnt is protected by cm_lock in the crypto_minor structure */
typedef struct crypto_provider_session {
	struct crypto_provider_session *ps_next;
	crypto_session_id_t		ps_session;
	kcf_provider_desc_t		*ps_provider;
	kcf_provider_desc_t		*ps_real_provider;
	uint_t				ps_refcnt;
} crypto_provider_session_t;

typedef struct crypto_session_data {
	kmutex_t			sd_lock;
	kcondvar_t			sd_cv;
	uint32_t			sd_flags;
	int				sd_pre_approved_amount;
	crypto_ctx_t			*sd_digest_ctx;
	crypto_ctx_t			*sd_encr_ctx;
	crypto_ctx_t			*sd_decr_ctx;
	crypto_ctx_t			*sd_sign_ctx;
	crypto_ctx_t			*sd_verify_ctx;
	crypto_ctx_t			*sd_sign_recover_ctx;
	crypto_ctx_t			*sd_verify_recover_ctx;
	kcf_provider_desc_t		*sd_provider;
	void				*sd_find_init_cookie;
	crypto_provider_session_t	*sd_provider_session;
} crypto_session_data_t;

#define	CRYPTO_SESSION_IN_USE		0x00000001
#define	CRYPTO_SESSION_IS_BUSY		0x00000002
#define	CRYPTO_SESSION_IS_CLOSED	0x00000004

#define	KCF_MAX_PIN_LEN			1024

/*
 * Per-minor info.
 *
 * cm_lock protects everything in this structure except for cm_refcnt.
 */
typedef struct crypto_minor {
	uint_t				cm_refcnt;
	kmutex_t			cm_lock;
	kcondvar_t			cm_cv;
	crypto_session_data_t		**cm_session_table;
	uint_t				cm_session_table_count;
	kcf_provider_desc_t		**cm_provider_array;
	uint_t				cm_provider_count;
	crypto_provider_session_t	*cm_provider_session;
} crypto_minor_t;

/*
 * Return codes for internal functions
 */
#define	KCF_SUCCESS		0x0	/* Successful call */
#define	KCF_INVALID_MECH_NUMBER	0x1	/* invalid mechanism number */
#define	KCF_INVALID_MECH_NAME	0x2	/* invalid mechanism name */
#define	KCF_INVALID_MECH_CLASS	0x3	/* invalid mechanism class */
#define	KCF_MECH_TAB_FULL	0x4	/* Need more room in the mech tabs. */
#define	KCF_INVALID_INDX	((ushort_t)-1)

/*
 * kCF internal mechanism and function group for tracking RNG providers.
 */
#define	SUN_RANDOM		"random"
#define	CRYPTO_FG_RANDOM	0x80000000	/* generate_random() */

/*
 * Wrappers for ops vectors. In the wrapper definitions below, the pd
 * argument always corresponds to a pointer to a provider descriptor
 * of type kcf_prov_desc_t.
 */

#define	KCF_PROV_CONTROL_OPS(pd)	((pd)->pd_ops_vector->co_control_ops)
#define	KCF_PROV_CTX_OPS(pd)		((pd)->pd_ops_vector->co_ctx_ops)
#define	KCF_PROV_DIGEST_OPS(pd)		((pd)->pd_ops_vector->co_digest_ops)
#define	KCF_PROV_CIPHER_OPS(pd)		((pd)->pd_ops_vector->co_cipher_ops)
#define	KCF_PROV_MAC_OPS(pd)		((pd)->pd_ops_vector->co_mac_ops)
#define	KCF_PROV_SIGN_OPS(pd)		((pd)->pd_ops_vector->co_sign_ops)
#define	KCF_PROV_VERIFY_OPS(pd)		((pd)->pd_ops_vector->co_verify_ops)
#define	KCF_PROV_DUAL_OPS(pd)		((pd)->pd_ops_vector->co_dual_ops)
#define	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) \
	((pd)->pd_ops_vector->co_dual_cipher_mac_ops)
#define	KCF_PROV_RANDOM_OPS(pd)		((pd)->pd_ops_vector->co_random_ops)
#define	KCF_PROV_SESSION_OPS(pd)	((pd)->pd_ops_vector->co_session_ops)
#define	KCF_PROV_OBJECT_OPS(pd)		((pd)->pd_ops_vector->co_object_ops)
#define	KCF_PROV_KEY_OPS(pd)		((pd)->pd_ops_vector->co_key_ops)
#define	KCF_PROV_PROVIDER_OPS(pd)	((pd)->pd_ops_vector->co_provider_ops)
#define	KCF_PROV_MECH_OPS(pd)		((pd)->pd_ops_vector->co_mech_ops)
#define	KCF_PROV_NOSTORE_KEY_OPS(pd)	\
	((pd)->pd_ops_vector->co_nostore_key_ops)

/*
 * Wrappers for crypto_control_ops(9S) entry points.
 */

#define	KCF_PROV_STATUS(pd, status) ( \
	(KCF_PROV_CONTROL_OPS(pd) && \
	KCF_PROV_CONTROL_OPS(pd)->provider_status) ? \
	KCF_PROV_CONTROL_OPS(pd)->provider_status( \
	    (pd)->pd_prov_handle, status) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_ctx_ops(9S) entry points.
 */

#define	KCF_PROV_CREATE_CTX_TEMPLATE(pd, mech, key, template, size, req) ( \
	(KCF_PROV_CTX_OPS(pd) && KCF_PROV_CTX_OPS(pd)->create_ctx_template) ? \
	KCF_PROV_CTX_OPS(pd)->create_ctx_template( \
	    (pd)->pd_prov_handle, mech, key, template, size, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_FREE_CONTEXT(pd, ctx) ( \
	(KCF_PROV_CTX_OPS(pd) && KCF_PROV_CTX_OPS(pd)->free_context) ? \
	KCF_PROV_CTX_OPS(pd)->free_context(ctx) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_COPYIN_MECH(pd, umech, kmech, errorp, mode) ( \
	(KCF_PROV_MECH_OPS(pd) && KCF_PROV_MECH_OPS(pd)->copyin_mechanism) ? \
	KCF_PROV_MECH_OPS(pd)->copyin_mechanism( \
	    (pd)->pd_prov_handle, umech, kmech, errorp, mode) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_COPYOUT_MECH(pd, kmech, umech, errorp, mode) ( \
	(KCF_PROV_MECH_OPS(pd) && KCF_PROV_MECH_OPS(pd)->copyout_mechanism) ? \
	KCF_PROV_MECH_OPS(pd)->copyout_mechanism( \
	    (pd)->pd_prov_handle, kmech, umech, errorp, mode) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_FREE_MECH(pd, prov_mech) ( \
	(KCF_PROV_MECH_OPS(pd) && KCF_PROV_MECH_OPS(pd)->free_mechanism) ? \
	KCF_PROV_MECH_OPS(pd)->free_mechanism( \
	    (pd)->pd_prov_handle, prov_mech) : CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_digest_ops(9S) entry points.
 */

#define	KCF_PROV_DIGEST_INIT(pd, ctx, mech, req) ( \
	(KCF_PROV_DIGEST_OPS(pd) && KCF_PROV_DIGEST_OPS(pd)->digest_init) ? \
	KCF_PROV_DIGEST_OPS(pd)->digest_init(ctx, mech, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * The _ (underscore) in _digest is needed to avoid replacing the
 * function digest().
 */
#define	KCF_PROV_DIGEST(pd, ctx, data, _digest, req) ( \
	(KCF_PROV_DIGEST_OPS(pd) && KCF_PROV_DIGEST_OPS(pd)->digest) ? \
	KCF_PROV_DIGEST_OPS(pd)->digest(ctx, data, _digest, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DIGEST_UPDATE(pd, ctx, data, req) ( \
	(KCF_PROV_DIGEST_OPS(pd) && KCF_PROV_DIGEST_OPS(pd)->digest_update) ? \
	KCF_PROV_DIGEST_OPS(pd)->digest_update(ctx, data, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DIGEST_KEY(pd, ctx, key, req) ( \
	(KCF_PROV_DIGEST_OPS(pd) && KCF_PROV_DIGEST_OPS(pd)->digest_key) ? \
	KCF_PROV_DIGEST_OPS(pd)->digest_key(ctx, key, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DIGEST_FINAL(pd, ctx, digest, req) ( \
	(KCF_PROV_DIGEST_OPS(pd) && KCF_PROV_DIGEST_OPS(pd)->digest_final) ? \
	KCF_PROV_DIGEST_OPS(pd)->digest_final(ctx, digest, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DIGEST_ATOMIC(pd, session, mech, data, digest, req) ( \
	(KCF_PROV_DIGEST_OPS(pd) && KCF_PROV_DIGEST_OPS(pd)->digest_atomic) ? \
	KCF_PROV_DIGEST_OPS(pd)->digest_atomic( \
	    (pd)->pd_prov_handle, session, mech, data, digest, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_cipher_ops(9S) entry points.
 */

#define	KCF_PROV_ENCRYPT_INIT(pd, ctx, mech, key, template, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->encrypt_init) ? \
	KCF_PROV_CIPHER_OPS(pd)->encrypt_init(ctx, mech, key, template, \
	    req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT(pd, ctx, plaintext, ciphertext, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->encrypt) ? \
	KCF_PROV_CIPHER_OPS(pd)->encrypt(ctx, plaintext, ciphertext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_UPDATE(pd, ctx, plaintext, ciphertext, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->encrypt_update) ? \
	KCF_PROV_CIPHER_OPS(pd)->encrypt_update(ctx, plaintext, \
	    ciphertext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_FINAL(pd, ctx, ciphertext, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->encrypt_final) ? \
	KCF_PROV_CIPHER_OPS(pd)->encrypt_final(ctx, ciphertext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_ATOMIC(pd, session, mech, key, plaintext, ciphertext, \
	    template, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->encrypt_atomic) ? \
	KCF_PROV_CIPHER_OPS(pd)->encrypt_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, plaintext, ciphertext, \
	    template, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT_INIT(pd, ctx, mech, key, template, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->decrypt_init) ? \
	KCF_PROV_CIPHER_OPS(pd)->decrypt_init(ctx, mech, key, template, \
	    req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT(pd, ctx, ciphertext, plaintext, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->decrypt) ? \
	KCF_PROV_CIPHER_OPS(pd)->decrypt(ctx, ciphertext, plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT_UPDATE(pd, ctx, ciphertext, plaintext, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->decrypt_update) ? \
	KCF_PROV_CIPHER_OPS(pd)->decrypt_update(ctx, ciphertext, \
	    plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT_FINAL(pd, ctx, plaintext, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->decrypt_final) ? \
	KCF_PROV_CIPHER_OPS(pd)->decrypt_final(ctx, plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT_ATOMIC(pd, session, mech, key, ciphertext, plaintext, \
	    template, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->decrypt_atomic) ? \
	KCF_PROV_CIPHER_OPS(pd)->decrypt_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, ciphertext, plaintext, \
	    template, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_mac_ops(9S) entry points.
 */

#define	KCF_PROV_MAC_INIT(pd, ctx, mech, key, template, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_init) ? \
	KCF_PROV_MAC_OPS(pd)->mac_init(ctx, mech, key, template, req) \
	: CRYPTO_NOT_SUPPORTED)

/*
 * The _ (underscore) in _mac is needed to avoid replacing the
 * function mac().
 */
#define	KCF_PROV_MAC(pd, ctx, data, _mac, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac) ? \
	KCF_PROV_MAC_OPS(pd)->mac(ctx, data, _mac, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_UPDATE(pd, ctx, data, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_update) ? \
	KCF_PROV_MAC_OPS(pd)->mac_update(ctx, data, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_FINAL(pd, ctx, mac, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_final) ? \
	KCF_PROV_MAC_OPS(pd)->mac_final(ctx, mac, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_ATOMIC(pd, session, mech, key, data, mac, template, \
	    req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_atomic) ? \
	KCF_PROV_MAC_OPS(pd)->mac_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, data, mac, template, \
	    req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_VERIFY_ATOMIC(pd, session, mech, key, data, mac, \
	    template, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_verify_atomic) ? \
	KCF_PROV_MAC_OPS(pd)->mac_verify_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, data, mac, template, \
	    req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_sign_ops(9S) entry points.
 */

#define	KCF_PROV_SIGN_INIT(pd, ctx, mech, key, template, req) ( \
	(KCF_PROV_SIGN_OPS(pd) && KCF_PROV_SIGN_OPS(pd)->sign_init) ? \
	KCF_PROV_SIGN_OPS(pd)->sign_init( \
	    ctx, mech, key, template, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN(pd, ctx, data, sig, req) ( \
	(KCF_PROV_SIGN_OPS(pd) && KCF_PROV_SIGN_OPS(pd)->sign) ? \
	KCF_PROV_SIGN_OPS(pd)->sign(ctx, data, sig, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN_UPDATE(pd, ctx, data, req) ( \
	(KCF_PROV_SIGN_OPS(pd) && KCF_PROV_SIGN_OPS(pd)->sign_update) ? \
	KCF_PROV_SIGN_OPS(pd)->sign_update(ctx, data, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN_FINAL(pd, ctx, sig, req) ( \
	(KCF_PROV_SIGN_OPS(pd) && KCF_PROV_SIGN_OPS(pd)->sign_final) ? \
	KCF_PROV_SIGN_OPS(pd)->sign_final(ctx, sig, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN_ATOMIC(pd, session, mech, key, data, template, \
	    sig, req) ( \
	(KCF_PROV_SIGN_OPS(pd) && KCF_PROV_SIGN_OPS(pd)->sign_atomic) ? \
	KCF_PROV_SIGN_OPS(pd)->sign_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, data, sig, template, \
	    req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN_RECOVER_INIT(pd, ctx, mech, key, template, \
	    req) ( \
	(KCF_PROV_SIGN_OPS(pd) && KCF_PROV_SIGN_OPS(pd)->sign_recover_init) ? \
	KCF_PROV_SIGN_OPS(pd)->sign_recover_init(ctx, mech, key, template, \
	    req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN_RECOVER(pd, ctx, data, sig, req) ( \
	(KCF_PROV_SIGN_OPS(pd) && KCF_PROV_SIGN_OPS(pd)->sign_recover) ? \
	KCF_PROV_SIGN_OPS(pd)->sign_recover(ctx, data, sig, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN_RECOVER_ATOMIC(pd, session, mech, key, data, template, \
	    sig, req) ( \
	(KCF_PROV_SIGN_OPS(pd) && \
	KCF_PROV_SIGN_OPS(pd)->sign_recover_atomic) ? \
	KCF_PROV_SIGN_OPS(pd)->sign_recover_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, data, sig, template, \
	    req) : CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_verify_ops(9S) entry points.
 */

#define	KCF_PROV_VERIFY_INIT(pd, ctx, mech, key, template, req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && KCF_PROV_VERIFY_OPS(pd)->verify_init) ? \
	KCF_PROV_VERIFY_OPS(pd)->verify_init(ctx, mech, key, template, \
	    req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_VERIFY(pd, ctx, data, sig, req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && KCF_PROV_VERIFY_OPS(pd)->do_verify) ? \
	KCF_PROV_VERIFY_OPS(pd)->do_verify(ctx, data, sig, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_VERIFY_UPDATE(pd, ctx, data, req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && KCF_PROV_VERIFY_OPS(pd)->verify_update) ? \
	KCF_PROV_VERIFY_OPS(pd)->verify_update(ctx, data, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_VERIFY_FINAL(pd, ctx, sig, req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && KCF_PROV_VERIFY_OPS(pd)->verify_final) ? \
	KCF_PROV_VERIFY_OPS(pd)->verify_final(ctx, sig, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_VERIFY_ATOMIC(pd, session, mech, key, data, template, sig, \
	    req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && KCF_PROV_VERIFY_OPS(pd)->verify_atomic) ? \
	KCF_PROV_VERIFY_OPS(pd)->verify_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, data, sig, template, \
	    req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_VERIFY_RECOVER_INIT(pd, ctx, mech, key, template, \
	    req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && \
	KCF_PROV_VERIFY_OPS(pd)->verify_recover_init) ? \
	KCF_PROV_VERIFY_OPS(pd)->verify_recover_init(ctx, mech, key, \
	    template, req) : CRYPTO_NOT_SUPPORTED)

/* verify_recover() CSPI routine has different argument order than verify() */
#define	KCF_PROV_VERIFY_RECOVER(pd, ctx, sig, data, req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && KCF_PROV_VERIFY_OPS(pd)->verify_recover) ? \
	KCF_PROV_VERIFY_OPS(pd)->verify_recover(ctx, sig, data, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * verify_recover_atomic() CSPI routine has different argument order
 * than verify_atomic().
 */
#define	KCF_PROV_VERIFY_RECOVER_ATOMIC(pd, session, mech, key, sig, \
	    template, data,  req) ( \
	(KCF_PROV_VERIFY_OPS(pd) && \
	KCF_PROV_VERIFY_OPS(pd)->verify_recover_atomic) ? \
	KCF_PROV_VERIFY_OPS(pd)->verify_recover_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, sig, data, template, \
	    req) : CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_dual_ops(9S) entry points.
 */

#define	KCF_PROV_DIGEST_ENCRYPT_UPDATE(digest_ctx, encrypt_ctx, plaintext, \
	    ciphertext, req) ( \
	(KCF_PROV_DUAL_OPS(pd) && \
	KCF_PROV_DUAL_OPS(pd)->digest_encrypt_update) ? \
	KCF_PROV_DUAL_OPS(pd)->digest_encrypt_update( \
	    digest_ctx, encrypt_ctx, plaintext, ciphertext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT_DIGEST_UPDATE(decrypt_ctx, digest_ctx, ciphertext, \
	    plaintext, req) ( \
	(KCF_PROV_DUAL_OPS(pd) && \
	KCF_PROV_DUAL_OPS(pd)->decrypt_digest_update) ? \
	KCF_PROV_DUAL_OPS(pd)->decrypt_digest_update( \
	    decrypt_ctx, digest_ctx, ciphertext, plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SIGN_ENCRYPT_UPDATE(sign_ctx, encrypt_ctx, plaintext, \
	    ciphertext, req) ( \
	(KCF_PROV_DUAL_OPS(pd) && \
	KCF_PROV_DUAL_OPS(pd)->sign_encrypt_update) ? \
	KCF_PROV_DUAL_OPS(pd)->sign_encrypt_update( \
	    sign_ctx, encrypt_ctx, plaintext, ciphertext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT_VERIFY_UPDATE(decrypt_ctx, verify_ctx, ciphertext, \
	    plaintext, req) ( \
	(KCF_PROV_DUAL_OPS(pd) && \
	KCF_PROV_DUAL_OPS(pd)->decrypt_verify_update) ? \
	KCF_PROV_DUAL_OPS(pd)->decrypt_verify_update( \
	    decrypt_ctx, verify_ctx, ciphertext, plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_dual_cipher_mac_ops(9S) entry points.
 */

#define	KCF_PROV_ENCRYPT_MAC_INIT(pd, ctx, encr_mech, encr_key, mac_mech, \
	    mac_key, encr_ctx_template, mac_ctx_template, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_init) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_init( \
	    ctx, encr_mech, encr_key, mac_mech, mac_key, encr_ctx_template, \
	    mac_ctx_template, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_MAC(pd, ctx, plaintext, ciphertext, mac, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac( \
	    ctx, plaintext, ciphertext, mac, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_MAC_UPDATE(pd, ctx, plaintext, ciphertext, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_update) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_update( \
	    ctx, plaintext, ciphertext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_MAC_FINAL(pd, ctx, ciphertext, mac, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_final) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_final( \
	    ctx, ciphertext, mac, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_MAC_ATOMIC(pd, session, encr_mech, encr_key, \
	    mac_mech, mac_key, plaintext, ciphertext, mac, \
	    encr_ctx_template, mac_ctx_template, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_atomic) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->encrypt_mac_atomic( \
	    (pd)->pd_prov_handle, session, encr_mech, encr_key, \
	    mac_mech, mac_key, plaintext, ciphertext, mac, \
	    encr_ctx_template, mac_ctx_template, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_DECRYPT_INIT(pd, ctx, mac_mech, mac_key, decr_mech, \
	    decr_key, mac_ctx_template, decr_ctx_template, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_init) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_init( \
	    ctx, mac_mech, mac_key, decr_mech, decr_key, mac_ctx_template, \
	    decr_ctx_template, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_DECRYPT(pd, ctx, ciphertext, mac, plaintext, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt( \
	    ctx, ciphertext, mac, plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_DECRYPT_UPDATE(pd, ctx, ciphertext, plaintext, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_update) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_update( \
	    ctx, ciphertext, plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_DECRYPT_FINAL(pd, ctx, mac, plaintext, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_final) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_final( \
	    ctx, mac, plaintext, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_DECRYPT_ATOMIC(pd, session, mac_mech, mac_key, \
	    decr_mech, decr_key, ciphertext, mac, plaintext, \
	    mac_ctx_template, decr_ctx_template, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_atomic) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_decrypt_atomic( \
	    (pd)->pd_prov_handle, session, mac_mech, mac_key, \
	    decr_mech, decr_key, ciphertext, mac, plaintext, \
	    mac_ctx_template, decr_ctx_template, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_VERIFY_DECRYPT_ATOMIC(pd, session, mac_mech, mac_key, \
	    decr_mech, decr_key, ciphertext, mac, plaintext, \
	    mac_ctx_template, decr_ctx_template, req) ( \
	(KCF_PROV_DUAL_CIPHER_MAC_OPS(pd) && \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_verify_decrypt_atomic \
	    != NULL) ? \
	KCF_PROV_DUAL_CIPHER_MAC_OPS(pd)->mac_verify_decrypt_atomic( \
	    (pd)->pd_prov_handle, session, mac_mech, mac_key, \
	    decr_mech, decr_key, ciphertext, mac, plaintext, \
	    mac_ctx_template, decr_ctx_template, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_random_number_ops(9S) entry points.
 */

#define	KCF_PROV_SEED_RANDOM(pd, session, buf, len, est, flags, req) ( \
	(KCF_PROV_RANDOM_OPS(pd) && KCF_PROV_RANDOM_OPS(pd)->seed_random) ? \
	KCF_PROV_RANDOM_OPS(pd)->seed_random((pd)->pd_prov_handle, \
	    session, buf, len, est, flags, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_GENERATE_RANDOM(pd, session, buf, len, req) ( \
	(KCF_PROV_RANDOM_OPS(pd) && \
	KCF_PROV_RANDOM_OPS(pd)->generate_random) ? \
	KCF_PROV_RANDOM_OPS(pd)->generate_random((pd)->pd_prov_handle, \
	    session, buf, len, req) : CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_session_ops(9S) entry points.
 *
 * ops_pd is the provider descriptor that supplies the ops_vector.
 * pd is the descriptor that supplies the provider handle.
 * Only session open/close needs two handles.
 */

#define	KCF_PROV_SESSION_OPEN(ops_pd, session, req, pd) ( \
	(KCF_PROV_SESSION_OPS(ops_pd) && \
	KCF_PROV_SESSION_OPS(ops_pd)->session_open) ? \
	KCF_PROV_SESSION_OPS(ops_pd)->session_open((pd)->pd_prov_handle, \
	    session, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SESSION_CLOSE(ops_pd, session, req, pd) ( \
	(KCF_PROV_SESSION_OPS(ops_pd) && \
	KCF_PROV_SESSION_OPS(ops_pd)->session_close) ? \
	KCF_PROV_SESSION_OPS(ops_pd)->session_close((pd)->pd_prov_handle, \
	    session, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SESSION_LOGIN(pd, session, user_type, pin, len, req) ( \
	(KCF_PROV_SESSION_OPS(pd) && \
	KCF_PROV_SESSION_OPS(pd)->session_login) ? \
	KCF_PROV_SESSION_OPS(pd)->session_login((pd)->pd_prov_handle, \
	    session, user_type, pin, len, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SESSION_LOGOUT(pd, session, req) ( \
	(KCF_PROV_SESSION_OPS(pd) && \
	KCF_PROV_SESSION_OPS(pd)->session_logout) ? \
	KCF_PROV_SESSION_OPS(pd)->session_logout((pd)->pd_prov_handle, \
	    session, req) : CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_object_ops(9S) entry points.
 */

#define	KCF_PROV_OBJECT_CREATE(pd, session, template, count, object, req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && KCF_PROV_OBJECT_OPS(pd)->object_create) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_create((pd)->pd_prov_handle, \
	    session, template, count, object, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_COPY(pd, session, object, template, count, \
	    new_object, req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && KCF_PROV_OBJECT_OPS(pd)->object_copy) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_copy((pd)->pd_prov_handle, \
	session, object, template, count, new_object, req) : \
	    CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_DESTROY(pd, session, object, req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && KCF_PROV_OBJECT_OPS(pd)->object_destroy) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_destroy((pd)->pd_prov_handle, \
	    session, object, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_GET_SIZE(pd, session, object, size, req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && \
	KCF_PROV_OBJECT_OPS(pd)->object_get_size) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_get_size((pd)->pd_prov_handle, \
	    session, object, size, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_GET_ATTRIBUTE_VALUE(pd, session, object, template, \
	    count, req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && \
	KCF_PROV_OBJECT_OPS(pd)->object_get_attribute_value) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_get_attribute_value( \
	(pd)->pd_prov_handle, session, object, template, count, req) : \
	    CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_SET_ATTRIBUTE_VALUE(pd, session, object, template, \
	    count, req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && \
	KCF_PROV_OBJECT_OPS(pd)->object_set_attribute_value) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_set_attribute_value( \
	(pd)->pd_prov_handle, session, object, template, count, req) : \
	    CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_FIND_INIT(pd, session, template, count, ppriv, \
	    req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && \
	KCF_PROV_OBJECT_OPS(pd)->object_find_init) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_find_init((pd)->pd_prov_handle, \
	session, template, count, ppriv, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_FIND(pd, ppriv, objects, max_objects, object_count, \
	    req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && KCF_PROV_OBJECT_OPS(pd)->object_find) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_find( \
	(pd)->pd_prov_handle, ppriv, objects, max_objects, object_count, \
	req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_OBJECT_FIND_FINAL(pd, ppriv, req) ( \
	(KCF_PROV_OBJECT_OPS(pd) && \
	KCF_PROV_OBJECT_OPS(pd)->object_find_final) ? \
	KCF_PROV_OBJECT_OPS(pd)->object_find_final( \
	    (pd)->pd_prov_handle, ppriv, req) : CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_key_ops(9S) entry points.
 */

#define	KCF_PROV_KEY_GENERATE(pd, session, mech, template, count, object, \
	    req) ( \
	(KCF_PROV_KEY_OPS(pd) && KCF_PROV_KEY_OPS(pd)->key_generate) ? \
	KCF_PROV_KEY_OPS(pd)->key_generate((pd)->pd_prov_handle, \
	    session, mech, template, count, object, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_KEY_GENERATE_PAIR(pd, session, mech, pub_template, \
	    pub_count, priv_template, priv_count, pub_key, priv_key, req) ( \
	(KCF_PROV_KEY_OPS(pd) && KCF_PROV_KEY_OPS(pd)->key_generate_pair) ? \
	KCF_PROV_KEY_OPS(pd)->key_generate_pair((pd)->pd_prov_handle, \
	    session, mech, pub_template, pub_count, priv_template, \
	    priv_count, pub_key, priv_key, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_KEY_WRAP(pd, session, mech, wrapping_key, key, wrapped_key, \
	    wrapped_key_len, req) ( \
	(KCF_PROV_KEY_OPS(pd) && KCF_PROV_KEY_OPS(pd)->key_wrap) ? \
	KCF_PROV_KEY_OPS(pd)->key_wrap((pd)->pd_prov_handle, \
	    session, mech, wrapping_key, key, wrapped_key, wrapped_key_len, \
	    req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_KEY_UNWRAP(pd, session, mech, unwrapping_key, wrapped_key, \
	    wrapped_key_len, template, count, key, req) ( \
	(KCF_PROV_KEY_OPS(pd) && KCF_PROV_KEY_OPS(pd)->key_unwrap) ? \
	KCF_PROV_KEY_OPS(pd)->key_unwrap((pd)->pd_prov_handle, \
	    session, mech, unwrapping_key, wrapped_key, wrapped_key_len, \
	    template, count, key, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_KEY_DERIVE(pd, session, mech, base_key, template, count, \
	    key, req) ( \
	(KCF_PROV_KEY_OPS(pd) && KCF_PROV_KEY_OPS(pd)->key_derive) ? \
	KCF_PROV_KEY_OPS(pd)->key_derive((pd)->pd_prov_handle, \
	    session, mech, base_key, template, count, key, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_KEY_CHECK(pd, mech, key) ( \
	(KCF_PROV_KEY_OPS(pd) && KCF_PROV_KEY_OPS(pd)->key_check) ? \
	KCF_PROV_KEY_OPS(pd)->key_check((pd)->pd_prov_handle, mech, key) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_provider_management_ops(9S) entry points.
 *
 * ops_pd is the provider descriptor that supplies the ops_vector.
 * pd is the descriptor that supplies the provider handle.
 * Only ext_info needs two handles.
 */

#define	KCF_PROV_EXT_INFO(ops_pd, provext_info, req, pd) ( \
	(KCF_PROV_PROVIDER_OPS(ops_pd) && \
	KCF_PROV_PROVIDER_OPS(ops_pd)->ext_info) ? \
	KCF_PROV_PROVIDER_OPS(ops_pd)->ext_info((pd)->pd_prov_handle, \
	    provext_info, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_INIT_TOKEN(pd, pin, pin_len, label, req) ( \
	(KCF_PROV_PROVIDER_OPS(pd) && KCF_PROV_PROVIDER_OPS(pd)->init_token) ? \
	KCF_PROV_PROVIDER_OPS(pd)->init_token((pd)->pd_prov_handle, \
	    pin, pin_len, label, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_INIT_PIN(pd, session, pin, pin_len, req) ( \
	(KCF_PROV_PROVIDER_OPS(pd) && KCF_PROV_PROVIDER_OPS(pd)->init_pin) ? \
	KCF_PROV_PROVIDER_OPS(pd)->init_pin((pd)->pd_prov_handle, \
	    session, pin, pin_len, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_SET_PIN(pd, session, old_pin, old_len, new_pin, new_len, \
	    req) ( \
	(KCF_PROV_PROVIDER_OPS(pd) && KCF_PROV_PROVIDER_OPS(pd)->set_pin) ? \
	KCF_PROV_PROVIDER_OPS(pd)->set_pin((pd)->pd_prov_handle, \
	session, old_pin, old_len, new_pin, new_len, req) : \
	    CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_nostore_key_ops(9S) entry points.
 */

#define	KCF_PROV_NOSTORE_KEY_GENERATE(pd, session, mech, template, count, \
	    out_template, out_count, req) ( \
	(KCF_PROV_NOSTORE_KEY_OPS(pd) && \
	    KCF_PROV_NOSTORE_KEY_OPS(pd)->nostore_key_generate) ? \
	KCF_PROV_NOSTORE_KEY_OPS(pd)->nostore_key_generate( \
	    (pd)->pd_prov_handle, session, mech, template, count, \
	    out_template, out_count, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_NOSTORE_KEY_GENERATE_PAIR(pd, session, mech, pub_template, \
	    pub_count, priv_template, priv_count, out_pub_template, \
	    out_pub_count, out_priv_template, out_priv_count, req) ( \
	(KCF_PROV_NOSTORE_KEY_OPS(pd) && \
	    KCF_PROV_NOSTORE_KEY_OPS(pd)->nostore_key_generate_pair) ? \
	KCF_PROV_NOSTORE_KEY_OPS(pd)->nostore_key_generate_pair( \
	    (pd)->pd_prov_handle, session, mech, pub_template, pub_count, \
	    priv_template, priv_count, out_pub_template, out_pub_count, \
	    out_priv_template, out_priv_count, req) : CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_NOSTORE_KEY_DERIVE(pd, session, mech, base_key, template, \
	    count, out_template, out_count, req) ( \
	(KCF_PROV_NOSTORE_KEY_OPS(pd) && \
	    KCF_PROV_NOSTORE_KEY_OPS(pd)->nostore_key_derive) ? \
	KCF_PROV_NOSTORE_KEY_OPS(pd)->nostore_key_derive( \
	    (pd)->pd_prov_handle, session, mech, base_key, template, count, \
	    out_template, out_count, req) : CRYPTO_NOT_SUPPORTED)

/*
 * The following routines are exported by the kcf module (/kernel/misc/kcf)
 * to the crypto and cryptoadmin modules.
 */

/* Digest/mac/cipher entry points that take a provider descriptor and session */
extern int crypto_digest_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);

extern int crypto_mac_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);

extern int crypto_encrypt_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);

extern int crypto_decrypt_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);


/* Other private digest/mac/cipher entry points not exported through k-API */
extern int crypto_digest_key_prov(crypto_context_t, crypto_key_t *,
    crypto_call_req_t *);

/* Private sign entry points exported by KCF */
extern int crypto_sign_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);

extern int crypto_sign_recover_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);

/* Private verify entry points exported by KCF */
extern int crypto_verify_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);

extern int crypto_verify_recover_single(crypto_context_t, crypto_data_t *,
    crypto_data_t *, crypto_call_req_t *);

/* Private dual operations entry points exported by KCF */
extern int crypto_digest_encrypt_update(crypto_context_t, crypto_context_t,
    crypto_data_t *, crypto_data_t *, crypto_call_req_t *);
extern int crypto_decrypt_digest_update(crypto_context_t, crypto_context_t,
    crypto_data_t *, crypto_data_t *, crypto_call_req_t *);
extern int crypto_sign_encrypt_update(crypto_context_t, crypto_context_t,
    crypto_data_t *, crypto_data_t *, crypto_call_req_t *);
extern int crypto_decrypt_verify_update(crypto_context_t, crypto_context_t,
    crypto_data_t *, crypto_data_t *, crypto_call_req_t *);

/* Random Number Generation */
int crypto_seed_random(crypto_provider_handle_t provider, uchar_t *buf,
    size_t len, crypto_call_req_t *req);
int crypto_generate_random(crypto_provider_handle_t provider, uchar_t *buf,
    size_t len, crypto_call_req_t *req);

/* Provider Management */
int crypto_get_provider_info(crypto_provider_id_t id,
    crypto_provider_info_t **info, crypto_call_req_t *req);
int crypto_get_provider_mechanisms(crypto_minor_t *, crypto_provider_id_t id,
    uint_t *count, crypto_mech_name_t **list);
int crypto_init_token(crypto_provider_handle_t provider, char *pin,
    size_t pin_len, char *label, crypto_call_req_t *);
int crypto_init_pin(crypto_provider_handle_t provider, char *pin,
    size_t pin_len, crypto_call_req_t *req);
int crypto_set_pin(crypto_provider_handle_t provider, char *old_pin,
    size_t old_len, char *new_pin, size_t new_len, crypto_call_req_t *req);
void crypto_free_provider_list(crypto_provider_entry_t *list, uint_t count);
void crypto_free_provider_info(crypto_provider_info_t *info);

/* Administrative */
int crypto_get_dev_list(uint_t *count, crypto_dev_list_entry_t **list);
int crypto_get_soft_list(uint_t *count, char **list, size_t *len);
int crypto_get_dev_info(char *name, uint_t instance, uint_t *count,
    crypto_mech_name_t **list);
int crypto_get_soft_info(caddr_t name, uint_t *count,
    crypto_mech_name_t **list);
int crypto_load_dev_disabled(char *name, uint_t instance, uint_t count,
    crypto_mech_name_t *list);
int crypto_load_soft_disabled(caddr_t name, uint_t count,
    crypto_mech_name_t *list);
int crypto_unload_soft_module(caddr_t path);
int crypto_load_soft_config(caddr_t name, uint_t count,
    crypto_mech_name_t *list);
int crypto_load_door(uint_t did);
void crypto_free_mech_list(crypto_mech_name_t *list, uint_t count);
void crypto_free_dev_list(crypto_dev_list_entry_t *list, uint_t count);

/* Miscellaneous */
int crypto_get_mechanism_number(caddr_t name, crypto_mech_type_t *number);
int crypto_get_function_list(crypto_provider_id_t id,
    crypto_function_list_t **list, int kmflag);
void crypto_free_function_list(crypto_function_list_t *list);
int crypto_build_permitted_mech_names(kcf_provider_desc_t *,
    crypto_mech_name_t **, uint_t *, int);
extern void kcf_destroy_mech_tabs(void);
extern void kcf_init_mech_tabs(void);
extern int kcf_add_mech_provider(short, kcf_provider_desc_t *,
    kcf_prov_mech_desc_t **);
extern void kcf_remove_mech_provider(char *, kcf_provider_desc_t *);
extern int kcf_get_mech_entry(crypto_mech_type_t, kcf_mech_entry_t **);
extern kcf_provider_desc_t *kcf_alloc_provider_desc(crypto_provider_info_t *);
extern void kcf_provider_zero_refcnt(kcf_provider_desc_t *);
extern void kcf_free_provider_desc(kcf_provider_desc_t *);
extern void kcf_soft_config_init(void);
extern int get_sw_provider_for_mech(crypto_mech_name_t, char **);
extern crypto_mech_type_t crypto_mech2id_common(char *, boolean_t);
extern void undo_register_provider(kcf_provider_desc_t *, boolean_t);
extern void redo_register_provider(kcf_provider_desc_t *);
extern void kcf_rnd_init(void);
extern boolean_t kcf_rngprov_check(void);
extern int kcf_rnd_get_pseudo_bytes(uint8_t *, size_t);
extern int kcf_rnd_get_bytes(uint8_t *, size_t, boolean_t, boolean_t);
extern int random_add_pseudo_entropy(uint8_t *, size_t, uint_t);
extern void kcf_rnd_schedule_timeout(boolean_t);
extern int crypto_uio_data(crypto_data_t *, uchar_t *, int, cmd_type_t,
    void *, void (*update)(void));
extern int crypto_mblk_data(crypto_data_t *, uchar_t *, int, cmd_type_t,
    void *, void (*update)(void));
extern int crypto_put_output_data(uchar_t *, crypto_data_t *, int);
extern int crypto_get_input_data(crypto_data_t *, uchar_t **, uchar_t *);
extern int crypto_copy_key_to_ctx(crypto_key_t *, crypto_key_t **, size_t *,
    int kmflag);
extern int crypto_digest_data(crypto_data_t *, void *, uchar_t *,
    void (*update)(void), void (*final)(void), uchar_t);
extern int crypto_update_iov(void *, crypto_data_t *, crypto_data_t *,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *));
extern int crypto_update_uio(void *, crypto_data_t *, crypto_data_t *,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *));
extern int crypto_update_mp(void *, crypto_data_t *, crypto_data_t *,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *));
extern int crypto_get_key_attr(crypto_key_t *, crypto_attr_type_t, uchar_t **,
    ssize_t *);

/* Access to the provider's table */
extern void kcf_prov_tab_destroy(void);
extern void kcf_prov_tab_init(void);
extern int kcf_prov_tab_add_provider(kcf_provider_desc_t *);
extern int kcf_prov_tab_rem_provider(crypto_provider_id_t);
extern kcf_provider_desc_t *kcf_prov_tab_lookup_by_name(char *);
extern kcf_provider_desc_t *kcf_prov_tab_lookup_by_dev(char *, uint_t);
extern int kcf_get_hw_prov_tab(uint_t *, kcf_provider_desc_t ***, int,
    char *, uint_t, boolean_t);
extern int kcf_get_slot_list(uint_t *, kcf_provider_desc_t ***, boolean_t);
extern void kcf_free_provider_tab(uint_t, kcf_provider_desc_t **);
extern kcf_provider_desc_t *kcf_prov_tab_lookup(crypto_provider_id_t);
extern int kcf_get_sw_prov(crypto_mech_type_t, kcf_provider_desc_t **,
    kcf_mech_entry_t **, boolean_t);

/* Access to the policy table */
extern boolean_t is_mech_disabled(kcf_provider_desc_t *, crypto_mech_name_t);
extern boolean_t is_mech_disabled_byname(crypto_provider_type_t, char *,
    uint_t, crypto_mech_name_t);
extern void kcf_policy_tab_init(void);
extern void kcf_policy_free_desc(kcf_policy_desc_t *);
extern void kcf_policy_remove_by_name(char *, uint_t *, crypto_mech_name_t **);
extern void kcf_policy_remove_by_dev(char *, uint_t, uint_t *,
    crypto_mech_name_t **);
extern kcf_policy_desc_t *kcf_policy_lookup_by_name(char *);
extern kcf_policy_desc_t *kcf_policy_lookup_by_dev(char *, uint_t);
extern int kcf_policy_load_soft_disabled(char *, uint_t, crypto_mech_name_t *,
    uint_t *, crypto_mech_name_t **);
extern int kcf_policy_load_dev_disabled(char *, uint_t, uint_t,
    crypto_mech_name_t *, uint_t *, crypto_mech_name_t **);
extern boolean_t in_soft_config_list(char *);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_IMPL_H */
