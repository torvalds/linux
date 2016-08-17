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
 * This file is part of the core Kernel Cryptographic Framework.
 * It implements the SPI functions exported to cryptographic
 * providers.
 */


#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/spi.h>

/*
 * minalloc and maxalloc values to be used for taskq_create().
 */
int crypto_taskq_threads = CRYPTO_TASKQ_THREADS;
int crypto_taskq_minalloc = CYRPTO_TASKQ_MIN;
int crypto_taskq_maxalloc = CRYPTO_TASKQ_MAX;

static void remove_provider(kcf_provider_desc_t *);
static void process_logical_providers(crypto_provider_info_t *,
    kcf_provider_desc_t *);
static int init_prov_mechs(crypto_provider_info_t *, kcf_provider_desc_t *);
static int kcf_prov_kstat_update(kstat_t *, int);
static void delete_kstat(kcf_provider_desc_t *);

static kcf_prov_stats_t kcf_stats_ks_data_template = {
	{ "kcf_ops_total",		KSTAT_DATA_UINT64 },
	{ "kcf_ops_passed",		KSTAT_DATA_UINT64 },
	{ "kcf_ops_failed",		KSTAT_DATA_UINT64 },
	{ "kcf_ops_returned_busy",	KSTAT_DATA_UINT64 }
};

#define	KCF_SPI_COPY_OPS(src, dst, ops) if ((src)->ops != NULL) \
	*((dst)->ops) = *((src)->ops);

/*
 * Copy an ops vector from src to dst. Used during provider registration
 * to copy the ops vector from the provider info structure to the
 * provider descriptor maintained by KCF.
 * Copying the ops vector specified by the provider is needed since the
 * framework does not require the provider info structure to be
 * persistent.
 */
static void
copy_ops_vector_v1(crypto_ops_t *src_ops, crypto_ops_t *dst_ops)
{
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_control_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_digest_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_cipher_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_mac_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_sign_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_verify_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_dual_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_dual_cipher_mac_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_random_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_session_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_object_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_key_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_provider_ops);
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_ctx_ops);
}

static void
copy_ops_vector_v2(crypto_ops_t *src_ops, crypto_ops_t *dst_ops)
{
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_mech_ops);
}

static void
copy_ops_vector_v3(crypto_ops_t *src_ops, crypto_ops_t *dst_ops)
{
	KCF_SPI_COPY_OPS(src_ops, dst_ops, co_nostore_key_ops);
}

/*
 * This routine is used to add cryptographic providers to the KEF framework.
 * Providers pass a crypto_provider_info structure to crypto_register_provider()
 * and get back a handle.  The crypto_provider_info structure contains a
 * list of mechanisms supported by the provider and an ops vector containing
 * provider entry points.  Hardware providers call this routine in their attach
 * routines.  Software providers call this routine in their _init() routine.
 */
int
crypto_register_provider(crypto_provider_info_t *info,
    crypto_kcf_provider_handle_t *handle)
{
	char *ks_name;

	kcf_provider_desc_t *prov_desc = NULL;
	int ret = CRYPTO_ARGUMENTS_BAD;

	if (info->pi_interface_version > CRYPTO_SPI_VERSION_3)
		return (CRYPTO_VERSION_MISMATCH);

	/*
	 * Check provider type, must be software, hardware, or logical.
	 */
	if (info->pi_provider_type != CRYPTO_HW_PROVIDER &&
	    info->pi_provider_type != CRYPTO_SW_PROVIDER &&
	    info->pi_provider_type != CRYPTO_LOGICAL_PROVIDER)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Allocate and initialize a new provider descriptor. We also
	 * hold it and release it when done.
	 */
	prov_desc = kcf_alloc_provider_desc(info);
	KCF_PROV_REFHOLD(prov_desc);

	prov_desc->pd_prov_type = info->pi_provider_type;

	/* provider-private handle, opaque to KCF */
	prov_desc->pd_prov_handle = info->pi_provider_handle;

	/* copy provider description string */
	if (info->pi_provider_description != NULL) {
		/*
		 * pi_provider_descriptor is a string that can contain
		 * up to CRYPTO_PROVIDER_DESCR_MAX_LEN + 1 characters
		 * INCLUDING the terminating null character. A bcopy()
		 * is necessary here as pd_description should not have
		 * a null character. See comments in kcf_alloc_provider_desc()
		 * for details on pd_description field.
		 */
		bcopy(info->pi_provider_description, prov_desc->pd_description,
		    MIN(strlen(info->pi_provider_description),
		    (size_t)CRYPTO_PROVIDER_DESCR_MAX_LEN));
	}

	if (info->pi_provider_type != CRYPTO_LOGICAL_PROVIDER) {
		if (info->pi_ops_vector == NULL) {
			goto bail;
		}
		copy_ops_vector_v1(info->pi_ops_vector,
		    prov_desc->pd_ops_vector);
		if (info->pi_interface_version >= CRYPTO_SPI_VERSION_2) {
			copy_ops_vector_v2(info->pi_ops_vector,
			    prov_desc->pd_ops_vector);
			prov_desc->pd_flags = info->pi_flags;
		}
		if (info->pi_interface_version == CRYPTO_SPI_VERSION_3) {
			copy_ops_vector_v3(info->pi_ops_vector,
			    prov_desc->pd_ops_vector);
		}
	}

	/* object_ops and nostore_key_ops are mutually exclusive */
	if (prov_desc->pd_ops_vector->co_object_ops &&
	    prov_desc->pd_ops_vector->co_nostore_key_ops) {
		goto bail;
	}

	/* process the mechanisms supported by the provider */
	if ((ret = init_prov_mechs(info, prov_desc)) != CRYPTO_SUCCESS)
		goto bail;

	/*
	 * Add provider to providers tables, also sets the descriptor
	 * pd_prov_id field.
	 */
	if ((ret = kcf_prov_tab_add_provider(prov_desc)) != CRYPTO_SUCCESS) {
		undo_register_provider(prov_desc, B_FALSE);
		goto bail;
	}

	/*
	 * We create a taskq only for a hardware provider. The global
	 * software queue is used for software providers. We handle ordering
	 * of multi-part requests in the taskq routine. So, it is safe to
	 * have multiple threads for the taskq. We pass TASKQ_PREPOPULATE flag
	 * to keep some entries cached to improve performance.
	 */
	if (prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER)
		prov_desc->pd_sched_info.ks_taskq = taskq_create("kcf_taskq",
		    crypto_taskq_threads, minclsyspri,
		    crypto_taskq_minalloc, crypto_taskq_maxalloc,
		    TASKQ_PREPOPULATE);
	else
		prov_desc->pd_sched_info.ks_taskq = NULL;

	/* no kernel session to logical providers */
	if (prov_desc->pd_prov_type != CRYPTO_LOGICAL_PROVIDER) {
		/*
		 * Open a session for session-oriented providers. This session
		 * is used for all kernel consumers. This is fine as a provider
		 * is required to support multiple thread access to a session.
		 * We can do this only after the taskq has been created as we
		 * do a kcf_submit_request() to open the session.
		 */
		if (KCF_PROV_SESSION_OPS(prov_desc) != NULL) {
			kcf_req_params_t params;

			KCF_WRAP_SESSION_OPS_PARAMS(&params,
			    KCF_OP_SESSION_OPEN, &prov_desc->pd_sid, 0,
			    CRYPTO_USER, NULL, 0, prov_desc);
			ret = kcf_submit_request(prov_desc, NULL, NULL, &params,
			    B_FALSE);

			if (ret != CRYPTO_SUCCESS) {
				undo_register_provider(prov_desc, B_TRUE);
				ret = CRYPTO_FAILED;
				goto bail;
			}
		}
	}

	if (prov_desc->pd_prov_type != CRYPTO_LOGICAL_PROVIDER) {
		/*
		 * Create the kstat for this provider. There is a kstat
		 * installed for each successfully registered provider.
		 * This kstat is deleted, when the provider unregisters.
		 */
		if (prov_desc->pd_prov_type == CRYPTO_SW_PROVIDER) {
			ks_name = kmem_asprintf("%s_%s",
			    "NONAME", "provider_stats");
		} else {
			ks_name = kmem_asprintf("%s_%d_%u_%s",
			    "NONAME", 0, prov_desc->pd_prov_id,
			    "provider_stats");
		}

		prov_desc->pd_kstat = kstat_create("kcf", 0, ks_name, "crypto",
		    KSTAT_TYPE_NAMED, sizeof (kcf_prov_stats_t) /
		    sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);

		if (prov_desc->pd_kstat != NULL) {
			bcopy(&kcf_stats_ks_data_template,
			    &prov_desc->pd_ks_data,
			    sizeof (kcf_stats_ks_data_template));
			prov_desc->pd_kstat->ks_data = &prov_desc->pd_ks_data;
			KCF_PROV_REFHOLD(prov_desc);
			KCF_PROV_IREFHOLD(prov_desc);
			prov_desc->pd_kstat->ks_private = prov_desc;
			prov_desc->pd_kstat->ks_update = kcf_prov_kstat_update;
			kstat_install(prov_desc->pd_kstat);
		}
		strfree(ks_name);
	}

	if (prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER)
		process_logical_providers(info, prov_desc);

	mutex_enter(&prov_desc->pd_lock);
	prov_desc->pd_state = KCF_PROV_READY;
	mutex_exit(&prov_desc->pd_lock);
	kcf_do_notify(prov_desc, B_TRUE);

	*handle = prov_desc->pd_kcf_prov_handle;
	ret = CRYPTO_SUCCESS;

bail:
	KCF_PROV_REFRELE(prov_desc);
	return (ret);
}

/*
 * This routine is used to notify the framework when a provider is being
 * removed.  Hardware providers call this routine in their detach routines.
 * Software providers call this routine in their _fini() routine.
 */
int
crypto_unregister_provider(crypto_kcf_provider_handle_t handle)
{
	uint_t mech_idx;
	kcf_provider_desc_t *desc;
	kcf_prov_state_t saved_state;

	/* lookup provider descriptor */
	if ((desc = kcf_prov_tab_lookup((crypto_provider_id_t)handle)) == NULL)
		return (CRYPTO_UNKNOWN_PROVIDER);

	mutex_enter(&desc->pd_lock);
	/*
	 * Check if any other thread is disabling or removing
	 * this provider. We return if this is the case.
	 */
	if (desc->pd_state >= KCF_PROV_DISABLED) {
		mutex_exit(&desc->pd_lock);
		/* Release reference held by kcf_prov_tab_lookup(). */
		KCF_PROV_REFRELE(desc);
		return (CRYPTO_BUSY);
	}

	saved_state = desc->pd_state;
	desc->pd_state = KCF_PROV_REMOVED;

	if (saved_state == KCF_PROV_BUSY) {
		/*
		 * The per-provider taskq threads may be waiting. We
		 * signal them so that they can start failing requests.
		 */
		cv_broadcast(&desc->pd_resume_cv);
	}

	if (desc->pd_prov_type == CRYPTO_SW_PROVIDER) {
		/*
		 * Check if this provider is currently being used.
		 * pd_irefcnt is the number of holds from the internal
		 * structures. We add one to account for the above lookup.
		 */
		if (desc->pd_refcnt > desc->pd_irefcnt + 1) {
			desc->pd_state = saved_state;
			mutex_exit(&desc->pd_lock);
			/* Release reference held by kcf_prov_tab_lookup(). */
			KCF_PROV_REFRELE(desc);
			/*
			 * The administrator presumably will stop the clients
			 * thus removing the holds, when they get the busy
			 * return value.  Any retry will succeed then.
			 */
			return (CRYPTO_BUSY);
		}
	}
	mutex_exit(&desc->pd_lock);

	if (desc->pd_prov_type != CRYPTO_SW_PROVIDER) {
		remove_provider(desc);
	}

	if (desc->pd_prov_type != CRYPTO_LOGICAL_PROVIDER) {
		/* remove the provider from the mechanisms tables */
		for (mech_idx = 0; mech_idx < desc->pd_mech_list_count;
		    mech_idx++) {
			kcf_remove_mech_provider(
			    desc->pd_mechanisms[mech_idx].cm_mech_name, desc);
		}
	}

	/* remove provider from providers table */
	if (kcf_prov_tab_rem_provider((crypto_provider_id_t)handle) !=
	    CRYPTO_SUCCESS) {
		/* Release reference held by kcf_prov_tab_lookup(). */
		KCF_PROV_REFRELE(desc);
		return (CRYPTO_UNKNOWN_PROVIDER);
	}

	delete_kstat(desc);

	if (desc->pd_prov_type == CRYPTO_SW_PROVIDER) {
		/* Release reference held by kcf_prov_tab_lookup(). */
		KCF_PROV_REFRELE(desc);

		/*
		 * Wait till the existing requests complete.
		 */
		mutex_enter(&desc->pd_lock);
		while (desc->pd_state != KCF_PROV_FREED)
			cv_wait(&desc->pd_remove_cv, &desc->pd_lock);
		mutex_exit(&desc->pd_lock);
	} else {
		/*
		 * Wait until requests that have been sent to the provider
		 * complete.
		 */
		mutex_enter(&desc->pd_lock);
		while (desc->pd_irefcnt > 0)
			cv_wait(&desc->pd_remove_cv, &desc->pd_lock);
		mutex_exit(&desc->pd_lock);
	}

	kcf_do_notify(desc, B_FALSE);

	if (desc->pd_prov_type == CRYPTO_SW_PROVIDER) {
		/*
		 * This is the only place where kcf_free_provider_desc()
		 * is called directly. KCF_PROV_REFRELE() should free the
		 * structure in all other places.
		 */
		ASSERT(desc->pd_state == KCF_PROV_FREED &&
		    desc->pd_refcnt == 0);
		kcf_free_provider_desc(desc);
	} else {
		KCF_PROV_REFRELE(desc);
	}

	return (CRYPTO_SUCCESS);
}

/*
 * This routine is used to notify the framework that the state of
 * a cryptographic provider has changed. Valid state codes are:
 *
 * CRYPTO_PROVIDER_READY
 * 	The provider indicates that it can process more requests. A provider
 *	will notify with this event if it previously has notified us with a
 *	CRYPTO_PROVIDER_BUSY.
 *
 * CRYPTO_PROVIDER_BUSY
 * 	The provider can not take more requests.
 *
 * CRYPTO_PROVIDER_FAILED
 *	The provider encountered an internal error. The framework will not
 * 	be sending any more requests to the provider. The provider may notify
 *	with a CRYPTO_PROVIDER_READY, if it is able to recover from the error.
 *
 * This routine can be called from user or interrupt context.
 */
void
crypto_provider_notification(crypto_kcf_provider_handle_t handle, uint_t state)
{
	kcf_provider_desc_t *pd;

	/* lookup the provider from the given handle */
	if ((pd = kcf_prov_tab_lookup((crypto_provider_id_t)handle)) == NULL)
		return;

	mutex_enter(&pd->pd_lock);

	if (pd->pd_state <= KCF_PROV_VERIFICATION_FAILED)
		goto out;

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		cmn_err(CE_WARN, "crypto_provider_notification: "
		    "logical provider (%x) ignored\n", handle);
		goto out;
	}
	switch (state) {
	case CRYPTO_PROVIDER_READY:
		switch (pd->pd_state) {
		case KCF_PROV_BUSY:
			pd->pd_state = KCF_PROV_READY;
			/*
			 * Signal the per-provider taskq threads that they
			 * can start submitting requests.
			 */
			cv_broadcast(&pd->pd_resume_cv);
			break;

		case KCF_PROV_FAILED:
			/*
			 * The provider recovered from the error. Let us
			 * use it now.
			 */
			pd->pd_state = KCF_PROV_READY;
			break;
		default:
			break;
		}
		break;

	case CRYPTO_PROVIDER_BUSY:
		switch (pd->pd_state) {
		case KCF_PROV_READY:
			pd->pd_state = KCF_PROV_BUSY;
			break;
		default:
			break;
		}
		break;

	case CRYPTO_PROVIDER_FAILED:
		/*
		 * We note the failure and return. The per-provider taskq
		 * threads check this flag and start failing the
		 * requests, if it is set. See process_req_hwp() for details.
		 */
		switch (pd->pd_state) {
		case KCF_PROV_READY:
			pd->pd_state = KCF_PROV_FAILED;
			break;

		case KCF_PROV_BUSY:
			pd->pd_state = KCF_PROV_FAILED;
			/*
			 * The per-provider taskq threads may be waiting. We
			 * signal them so that they can start failing requests.
			 */
			cv_broadcast(&pd->pd_resume_cv);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
out:
	mutex_exit(&pd->pd_lock);
	KCF_PROV_REFRELE(pd);
}

/*
 * This routine is used to notify the framework the result of
 * an asynchronous request handled by a provider. Valid error
 * codes are the same as the CRYPTO_* errors defined in common.h.
 *
 * This routine can be called from user or interrupt context.
 */
void
crypto_op_notification(crypto_req_handle_t handle, int error)
{
	kcf_call_type_t ctype;

	if (handle == NULL)
		return;

	if ((ctype = GET_REQ_TYPE(handle)) == CRYPTO_SYNCH) {
		kcf_sreq_node_t *sreq = (kcf_sreq_node_t *)handle;

		if (error != CRYPTO_SUCCESS)
			sreq->sn_provider->pd_sched_info.ks_nfails++;
		KCF_PROV_IREFRELE(sreq->sn_provider);
		kcf_sop_done(sreq, error);
	} else {
		kcf_areq_node_t *areq = (kcf_areq_node_t *)handle;

		ASSERT(ctype == CRYPTO_ASYNCH);
		if (error != CRYPTO_SUCCESS)
			areq->an_provider->pd_sched_info.ks_nfails++;
		KCF_PROV_IREFRELE(areq->an_provider);
		kcf_aop_done(areq, error);
	}
}

/*
 * This routine is used by software providers to determine
 * whether to use KM_SLEEP or KM_NOSLEEP during memory allocation.
 * Note that hardware providers can always use KM_SLEEP. So,
 * they do not need to call this routine.
 *
 * This routine can be called from user or interrupt context.
 */
int
crypto_kmflag(crypto_req_handle_t handle)
{
	return (REQHNDL2_KMFLAG(handle));
}

/*
 * Process the mechanism info structures specified by the provider
 * during registration. A NULL crypto_provider_info_t indicates
 * an already initialized provider descriptor.
 *
 * Mechanisms are not added to the kernel's mechanism table if the
 * provider is a logical provider.
 *
 * Returns CRYPTO_SUCCESS on success, CRYPTO_ARGUMENTS if one
 * of the specified mechanisms was malformed, or CRYPTO_HOST_MEMORY
 * if the table of mechanisms is full.
 */
static int
init_prov_mechs(crypto_provider_info_t *info, kcf_provider_desc_t *desc)
{
	uint_t mech_idx;
	uint_t cleanup_idx;
	int err = CRYPTO_SUCCESS;
	kcf_prov_mech_desc_t *pmd;
	int desc_use_count = 0;
	int mcount = desc->pd_mech_list_count;

	if (desc->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		if (info != NULL) {
			ASSERT(info->pi_mechanisms != NULL);
			bcopy(info->pi_mechanisms, desc->pd_mechanisms,
			    sizeof (crypto_mech_info_t) * mcount);
		}
		return (CRYPTO_SUCCESS);
	}

	/*
	 * Copy the mechanism list from the provider info to the provider
	 * descriptor. desc->pd_mechanisms has an extra crypto_mech_info_t
	 * element if the provider has random_ops since we keep an internal
	 * mechanism, SUN_RANDOM, in this case.
	 */
	if (info != NULL) {
		if (info->pi_ops_vector->co_random_ops != NULL) {
			crypto_mech_info_t *rand_mi;

			/*
			 * Need the following check as it is possible to have
			 * a provider that implements just random_ops and has
			 * pi_mechanisms == NULL.
			 */
			if (info->pi_mechanisms != NULL) {
				bcopy(info->pi_mechanisms, desc->pd_mechanisms,
				    sizeof (crypto_mech_info_t) * (mcount - 1));
			}
			rand_mi = &desc->pd_mechanisms[mcount - 1];

			bzero(rand_mi, sizeof (crypto_mech_info_t));
			(void) strncpy(rand_mi->cm_mech_name, SUN_RANDOM,
			    CRYPTO_MAX_MECH_NAME);
			rand_mi->cm_func_group_mask = CRYPTO_FG_RANDOM;
		} else {
			ASSERT(info->pi_mechanisms != NULL);
			bcopy(info->pi_mechanisms, desc->pd_mechanisms,
			    sizeof (crypto_mech_info_t) * mcount);
		}
	}

	/*
	 * For each mechanism support by the provider, add the provider
	 * to the corresponding KCF mechanism mech_entry chain.
	 */
	for (mech_idx = 0; mech_idx < desc->pd_mech_list_count; mech_idx++) {
		crypto_mech_info_t *mi = &desc->pd_mechanisms[mech_idx];

		if ((mi->cm_mech_flags & CRYPTO_KEYSIZE_UNIT_IN_BITS) &&
		    (mi->cm_mech_flags & CRYPTO_KEYSIZE_UNIT_IN_BYTES)) {
			err = CRYPTO_ARGUMENTS_BAD;
			break;
		}

		if (desc->pd_flags & CRYPTO_HASH_NO_UPDATE &&
		    mi->cm_func_group_mask & CRYPTO_FG_DIGEST) {
			/*
			 * We ask the provider to specify the limit
			 * per hash mechanism. But, in practice, a
			 * hardware limitation means all hash mechanisms
			 * will have the same maximum size allowed for
			 * input data. So, we make it a per provider
			 * limit to keep it simple.
			 */
			if (mi->cm_max_input_length == 0) {
				err = CRYPTO_ARGUMENTS_BAD;
				break;
			} else {
				desc->pd_hash_limit = mi->cm_max_input_length;
			}
		}

		if ((err = kcf_add_mech_provider(mech_idx, desc, &pmd)) !=
		    KCF_SUCCESS)
			break;

		if (pmd == NULL)
			continue;

		/* The provider will be used for this mechanism */
		desc_use_count++;
	}

	/*
	 * Don't allow multiple software providers with disabled mechanisms
	 * to register. Subsequent enabling of mechanisms will result in
	 * an unsupported configuration, i.e. multiple software providers
	 * per mechanism.
	 */
	if (desc_use_count == 0 && desc->pd_prov_type == CRYPTO_SW_PROVIDER)
		return (CRYPTO_ARGUMENTS_BAD);

	if (err == KCF_SUCCESS)
		return (CRYPTO_SUCCESS);

	/*
	 * An error occurred while adding the mechanism, cleanup
	 * and bail.
	 */
	for (cleanup_idx = 0; cleanup_idx < mech_idx; cleanup_idx++) {
		kcf_remove_mech_provider(
		    desc->pd_mechanisms[cleanup_idx].cm_mech_name, desc);
	}

	if (err == KCF_MECH_TAB_FULL)
		return (CRYPTO_HOST_MEMORY);

	return (CRYPTO_ARGUMENTS_BAD);
}

/*
 * Update routine for kstat. Only privileged users are allowed to
 * access this information, since this information is sensitive.
 * There are some cryptographic attacks (e.g. traffic analysis)
 * which can use this information.
 */
static int
kcf_prov_kstat_update(kstat_t *ksp, int rw)
{
	kcf_prov_stats_t *ks_data;
	kcf_provider_desc_t *pd = (kcf_provider_desc_t *)ksp->ks_private;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	ks_data = ksp->ks_data;

	ks_data->ps_ops_total.value.ui64 = pd->pd_sched_info.ks_ndispatches;
	ks_data->ps_ops_failed.value.ui64 = pd->pd_sched_info.ks_nfails;
	ks_data->ps_ops_busy_rval.value.ui64 = pd->pd_sched_info.ks_nbusy_rval;
	ks_data->ps_ops_passed.value.ui64 =
	    pd->pd_sched_info.ks_ndispatches -
	    pd->pd_sched_info.ks_nfails -
	    pd->pd_sched_info.ks_nbusy_rval;

	return (0);
}


/*
 * Utility routine called from failure paths in crypto_register_provider()
 * and from crypto_load_soft_disabled().
 */
void
undo_register_provider(kcf_provider_desc_t *desc, boolean_t remove_prov)
{
	uint_t mech_idx;

	/* remove the provider from the mechanisms tables */
	for (mech_idx = 0; mech_idx < desc->pd_mech_list_count;
	    mech_idx++) {
		kcf_remove_mech_provider(
		    desc->pd_mechanisms[mech_idx].cm_mech_name, desc);
	}

	/* remove provider from providers table */
	if (remove_prov)
		(void) kcf_prov_tab_rem_provider(desc->pd_prov_id);
}

/*
 * Utility routine called from crypto_load_soft_disabled(). Callers
 * should have done a prior undo_register_provider().
 */
void
redo_register_provider(kcf_provider_desc_t *pd)
{
	/* process the mechanisms supported by the provider */
	(void) init_prov_mechs(NULL, pd);

	/*
	 * Hold provider in providers table. We should not call
	 * kcf_prov_tab_add_provider() here as the provider descriptor
	 * is still valid which means it has an entry in the provider
	 * table.
	 */
	KCF_PROV_REFHOLD(pd);
	KCF_PROV_IREFHOLD(pd);
}

/*
 * Add provider (p1) to another provider's array of providers (p2).
 * Hardware and logical providers use this array to cross-reference
 * each other.
 */
static void
add_provider_to_array(kcf_provider_desc_t *p1, kcf_provider_desc_t *p2)
{
	kcf_provider_list_t *new;

	new = kmem_alloc(sizeof (kcf_provider_list_t), KM_SLEEP);
	mutex_enter(&p2->pd_lock);
	new->pl_next = p2->pd_provider_list;
	p2->pd_provider_list = new;
	KCF_PROV_IREFHOLD(p1);
	new->pl_provider = p1;
	mutex_exit(&p2->pd_lock);
}

/*
 * Remove provider (p1) from another provider's array of providers (p2).
 * Hardware and logical providers use this array to cross-reference
 * each other.
 */
static void
remove_provider_from_array(kcf_provider_desc_t *p1, kcf_provider_desc_t *p2)
{

	kcf_provider_list_t *pl = NULL, **prev;

	mutex_enter(&p2->pd_lock);
	for (pl = p2->pd_provider_list, prev = &p2->pd_provider_list;
	    pl != NULL; prev = &pl->pl_next, pl = pl->pl_next) {
		if (pl->pl_provider == p1) {
			break;
		}
	}

	if (p1 == NULL) {
		mutex_exit(&p2->pd_lock);
		return;
	}

	/* detach and free kcf_provider_list structure */
	KCF_PROV_IREFRELE(p1);
	*prev = pl->pl_next;
	kmem_free(pl, sizeof (*pl));
	mutex_exit(&p2->pd_lock);
}

/*
 * Convert an array of logical provider handles (crypto_provider_id)
 * stored in a crypto_provider_info structure into an array of provider
 * descriptors (kcf_provider_desc_t) attached to a logical provider.
 */
static void
process_logical_providers(crypto_provider_info_t *info, kcf_provider_desc_t *hp)
{
	kcf_provider_desc_t *lp;
	crypto_provider_id_t handle;
	int count = info->pi_logical_provider_count;
	int i;

	/* add hardware provider to each logical provider */
	for (i = 0; i < count; i++) {
		handle = info->pi_logical_providers[i];
		lp = kcf_prov_tab_lookup((crypto_provider_id_t)handle);
		if (lp == NULL) {
			continue;
		}
		add_provider_to_array(hp, lp);
		hp->pd_flags |= KCF_LPROV_MEMBER;

		/*
		 * A hardware provider has to have the provider descriptor of
		 * every logical provider it belongs to, so it can be removed
		 * from the logical provider if the hardware provider
		 * unregisters from the framework.
		 */
		add_provider_to_array(lp, hp);
		KCF_PROV_REFRELE(lp);
	}
}

/*
 * This routine removes a provider from all of the logical or
 * hardware providers it belongs to, and frees the provider's
 * array of pointers to providers.
 */
static void
remove_provider(kcf_provider_desc_t *pp)
{
	kcf_provider_desc_t *p;
	kcf_provider_list_t *e, *next;

	mutex_enter(&pp->pd_lock);
	for (e = pp->pd_provider_list; e != NULL; e = next) {
		p = e->pl_provider;
		remove_provider_from_array(pp, p);
		if (p->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    p->pd_provider_list == NULL)
			p->pd_flags &= ~KCF_LPROV_MEMBER;
		KCF_PROV_IREFRELE(p);
		next = e->pl_next;
		kmem_free(e, sizeof (*e));
	}
	pp->pd_provider_list = NULL;
	mutex_exit(&pp->pd_lock);
}

/*
 * Dispatch events as needed for a provider. is_added flag tells
 * whether the provider is registering or unregistering.
 */
void
kcf_do_notify(kcf_provider_desc_t *prov_desc, boolean_t is_added)
{
	int i;
	crypto_notify_event_change_t ec;

	ASSERT(prov_desc->pd_state > KCF_PROV_VERIFICATION_FAILED);

	/*
	 * Inform interested clients of the mechanisms becoming
	 * available/unavailable. We skip this for logical providers
	 * as they do not affect mechanisms.
	 */
	if (prov_desc->pd_prov_type != CRYPTO_LOGICAL_PROVIDER) {
		ec.ec_provider_type = prov_desc->pd_prov_type;
		ec.ec_change = is_added ? CRYPTO_MECH_ADDED :
		    CRYPTO_MECH_REMOVED;
		for (i = 0; i < prov_desc->pd_mech_list_count; i++) {
			(void) strlcpy(ec.ec_mech_name,
			    prov_desc->pd_mechanisms[i].cm_mech_name,
			    CRYPTO_MAX_MECH_NAME);
			kcf_walk_ntfylist(CRYPTO_EVENT_MECHS_CHANGED, &ec);
		}

	}

	/*
	 * Inform interested clients about the new or departing provider.
	 * In case of a logical provider, we need to notify the event only
	 * for the logical provider and not for the underlying
	 * providers which are known by the KCF_LPROV_MEMBER bit.
	 */
	if (prov_desc->pd_prov_type == CRYPTO_LOGICAL_PROVIDER ||
	    (prov_desc->pd_flags & KCF_LPROV_MEMBER) == 0) {
		kcf_walk_ntfylist(is_added ? CRYPTO_EVENT_PROVIDER_REGISTERED :
		    CRYPTO_EVENT_PROVIDER_UNREGISTERED, prov_desc);
	}
}

static void
delete_kstat(kcf_provider_desc_t *desc)
{
	/* destroy the kstat created for this provider */
	if (desc->pd_kstat != NULL) {
		kcf_provider_desc_t *kspd = desc->pd_kstat->ks_private;

		/* release reference held by desc->pd_kstat->ks_private */
		ASSERT(desc == kspd);
		kstat_delete(kspd->pd_kstat);
		desc->pd_kstat = NULL;
		KCF_PROV_REFRELE(kspd);
		KCF_PROV_IREFRELE(kspd);
	}
}
