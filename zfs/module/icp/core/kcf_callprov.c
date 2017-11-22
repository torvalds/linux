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

#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>

static int kcf_emulate_dual(kcf_provider_desc_t *, crypto_ctx_t *,
    kcf_req_params_t *);

void
kcf_free_triedlist(kcf_prov_tried_t *list)
{
	kcf_prov_tried_t *l;

	while ((l = list) != NULL) {
		list = list->pt_next;
		KCF_PROV_REFRELE(l->pt_pd);
		kmem_free(l, sizeof (kcf_prov_tried_t));
	}
}

kcf_prov_tried_t *
kcf_insert_triedlist(kcf_prov_tried_t **list, kcf_provider_desc_t *pd,
    int kmflag)
{
	kcf_prov_tried_t *l;

	l = kmem_alloc(sizeof (kcf_prov_tried_t), kmflag);
	if (l == NULL)
		return (NULL);

	l->pt_pd = pd;
	l->pt_next = *list;
	*list = l;

	return (l);
}

static boolean_t
is_in_triedlist(kcf_provider_desc_t *pd, kcf_prov_tried_t *triedl)
{
	while (triedl != NULL) {
		if (triedl->pt_pd == pd)
			return (B_TRUE);
		triedl = triedl->pt_next;
	};

	return (B_FALSE);
}

/*
 * Search a mech entry's hardware provider list for the specified
 * provider. Return true if found.
 */
static boolean_t
is_valid_provider_for_mech(kcf_provider_desc_t *pd, kcf_mech_entry_t *me,
    crypto_func_group_t fg)
{
	kcf_prov_mech_desc_t *prov_chain;

	prov_chain = me->me_hw_prov_chain;
	if (prov_chain != NULL) {
		ASSERT(me->me_num_hwprov > 0);
		for (; prov_chain != NULL; prov_chain = prov_chain->pm_next) {
			if (prov_chain->pm_prov_desc == pd &&
			    IS_FG_SUPPORTED(prov_chain, fg)) {
				return (B_TRUE);
			}
		}
	}
	return (B_FALSE);
}

/*
 * This routine, given a logical provider, returns the least loaded
 * provider belonging to the logical provider. The provider must be
 * able to do the specified mechanism, i.e. check that the mechanism
 * hasn't been disabled. In addition, just in case providers are not
 * entirely equivalent, the provider's entry point is checked for
 * non-nullness. This is accomplished by having the caller pass, as
 * arguments, the offset of the function group (offset_1), and the
 * offset of the function within the function group (offset_2).
 * Returns NULL if no provider can be found.
 */
int
kcf_get_hardware_provider(crypto_mech_type_t mech_type_1,
    crypto_mech_type_t mech_type_2, boolean_t call_restrict,
    kcf_provider_desc_t *old, kcf_provider_desc_t **new, crypto_func_group_t fg)
{
	kcf_provider_desc_t *provider, *real_pd = old;
	kcf_provider_desc_t *gpd = NULL;	/* good provider */
	kcf_provider_desc_t *bpd = NULL;	/* busy provider */
	kcf_provider_list_t *p;
	kcf_ops_class_t class;
	kcf_mech_entry_t *me;
	kcf_mech_entry_tab_t *me_tab;
	int index, len, gqlen = INT_MAX, rv = CRYPTO_SUCCESS;

	/* get the mech entry for the specified mechanism */
	class = KCF_MECH2CLASS(mech_type_1);
	if ((class < KCF_FIRST_OPSCLASS) || (class > KCF_LAST_OPSCLASS)) {
		return (CRYPTO_MECHANISM_INVALID);
	}

	me_tab = &kcf_mech_tabs_tab[class];
	index = KCF_MECH2INDEX(mech_type_1);
	if ((index < 0) || (index >= me_tab->met_size)) {
		return (CRYPTO_MECHANISM_INVALID);
	}

	me = &((me_tab->met_tab)[index]);
	mutex_enter(&me->me_mutex);

	/*
	 * We assume the provider descriptor will not go away because
	 * it is being held somewhere, i.e. its reference count has been
	 * incremented. In the case of the crypto module, the provider
	 * descriptor is held by the session structure.
	 */
	if (old->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		if (old->pd_provider_list == NULL) {
			real_pd = NULL;
			rv = CRYPTO_DEVICE_ERROR;
			goto out;
		}
		/*
		 * Find the least loaded real provider. KCF_PROV_LOAD gives
		 * the load (number of pending requests) of the provider.
		 */
		mutex_enter(&old->pd_lock);
		p = old->pd_provider_list;
		while (p != NULL) {
			provider = p->pl_provider;

			ASSERT(provider->pd_prov_type !=
			    CRYPTO_LOGICAL_PROVIDER);

			if (call_restrict &&
			    (provider->pd_flags & KCF_PROV_RESTRICTED)) {
				p = p->pl_next;
				continue;
			}

			if (!is_valid_provider_for_mech(provider, me, fg)) {
				p = p->pl_next;
				continue;
			}

			/* provider does second mech */
			if (mech_type_2 != CRYPTO_MECH_INVALID) {
				int i;

				i = KCF_TO_PROV_MECH_INDX(provider,
				    mech_type_2);
				if (i == KCF_INVALID_INDX) {
					p = p->pl_next;
					continue;
				}
			}

			if (provider->pd_state != KCF_PROV_READY) {
				/* choose BUSY if no READY providers */
				if (provider->pd_state == KCF_PROV_BUSY)
					bpd = provider;
				p = p->pl_next;
				continue;
			}

			len = KCF_PROV_LOAD(provider);
			if (len < gqlen) {
				gqlen = len;
				gpd = provider;
			}

			p = p->pl_next;
		}

		if (gpd != NULL) {
			real_pd = gpd;
			KCF_PROV_REFHOLD(real_pd);
		} else if (bpd != NULL) {
			real_pd = bpd;
			KCF_PROV_REFHOLD(real_pd);
		} else {
			/* can't find provider */
			real_pd = NULL;
			rv = CRYPTO_MECHANISM_INVALID;
		}
		mutex_exit(&old->pd_lock);

	} else {
		if (!KCF_IS_PROV_USABLE(old) ||
		    (call_restrict && (old->pd_flags & KCF_PROV_RESTRICTED))) {
			real_pd = NULL;
			rv = CRYPTO_DEVICE_ERROR;
			goto out;
		}

		if (!is_valid_provider_for_mech(old, me, fg)) {
			real_pd = NULL;
			rv = CRYPTO_MECHANISM_INVALID;
			goto out;
		}

		KCF_PROV_REFHOLD(real_pd);
	}
out:
	mutex_exit(&me->me_mutex);
	*new = real_pd;
	return (rv);
}

/*
 * Return the best provider for the specified mechanism. The provider
 * is held and it is the caller's responsibility to release it when done.
 * The fg input argument is used as a search criterion to pick a provider.
 * A provider has to support this function group to be picked.
 *
 * Find the least loaded provider in the list of providers. We do a linear
 * search to find one. This is fine as we assume there are only a few
 * number of providers in this list. If this assumption ever changes,
 * we should revisit this.
 *
 * call_restrict represents if the caller should not be allowed to
 * use restricted providers.
 */
kcf_provider_desc_t *
kcf_get_mech_provider(crypto_mech_type_t mech_type, kcf_mech_entry_t **mepp,
    int *error, kcf_prov_tried_t *triedl, crypto_func_group_t fg,
    boolean_t call_restrict, size_t data_size)
{
	kcf_provider_desc_t *pd = NULL, *gpd = NULL;
	kcf_prov_mech_desc_t *prov_chain, *mdesc;
	int len, gqlen = INT_MAX;
	kcf_ops_class_t class;
	int index;
	kcf_mech_entry_t *me;
	kcf_mech_entry_tab_t *me_tab;

	class = KCF_MECH2CLASS(mech_type);
	if ((class < KCF_FIRST_OPSCLASS) || (class > KCF_LAST_OPSCLASS)) {
		*error = CRYPTO_MECHANISM_INVALID;
		return (NULL);
	}

	me_tab = &kcf_mech_tabs_tab[class];
	index = KCF_MECH2INDEX(mech_type);
	if ((index < 0) || (index >= me_tab->met_size)) {
		*error = CRYPTO_MECHANISM_INVALID;
		return (NULL);
	}

	me = &((me_tab->met_tab)[index]);
	if (mepp != NULL)
		*mepp = me;

	mutex_enter(&me->me_mutex);

	prov_chain = me->me_hw_prov_chain;

	/*
	 * We check for the threshold for using a hardware provider for
	 * this amount of data. If there is no software provider available
	 * for the mechanism, then the threshold is ignored.
	 */
	if ((prov_chain != NULL) &&
	    ((data_size == 0) || (me->me_threshold == 0) ||
	    (data_size >= me->me_threshold) ||
	    ((mdesc = me->me_sw_prov) == NULL) ||
	    (!IS_FG_SUPPORTED(mdesc, fg)) ||
	    (!KCF_IS_PROV_USABLE(mdesc->pm_prov_desc)))) {
		ASSERT(me->me_num_hwprov > 0);
		/* there is at least one provider */

		/*
		 * Find the least loaded real provider. KCF_PROV_LOAD gives
		 * the load (number of pending requests) of the provider.
		 */
		while (prov_chain != NULL) {
			pd = prov_chain->pm_prov_desc;

			if (!IS_FG_SUPPORTED(prov_chain, fg) ||
			    !KCF_IS_PROV_USABLE(pd) ||
			    IS_PROVIDER_TRIED(pd, triedl) ||
			    (call_restrict &&
			    (pd->pd_flags & KCF_PROV_RESTRICTED))) {
				prov_chain = prov_chain->pm_next;
				continue;
			}

			if ((len = KCF_PROV_LOAD(pd)) < gqlen) {
				gqlen = len;
				gpd = pd;
			}

			prov_chain = prov_chain->pm_next;
		}

		pd = gpd;
	}

	/* No HW provider for this mech, is there a SW provider? */
	if (pd == NULL && (mdesc = me->me_sw_prov) != NULL) {
		pd = mdesc->pm_prov_desc;
		if (!IS_FG_SUPPORTED(mdesc, fg) ||
		    !KCF_IS_PROV_USABLE(pd) ||
		    IS_PROVIDER_TRIED(pd, triedl) ||
		    (call_restrict && (pd->pd_flags & KCF_PROV_RESTRICTED)))
			pd = NULL;
	}

	if (pd == NULL) {
		/*
		 * We do not want to report CRYPTO_MECH_NOT_SUPPORTED, when
		 * we are in the "fallback to the next provider" case. Rather
		 * we preserve the error, so that the client gets the right
		 * error code.
		 */
		if (triedl == NULL)
			*error = CRYPTO_MECH_NOT_SUPPORTED;
	} else
		KCF_PROV_REFHOLD(pd);

	mutex_exit(&me->me_mutex);
	return (pd);
}

/*
 * Very similar to kcf_get_mech_provider(). Finds the best provider capable of
 * a dual operation with both me1 and me2.
 * When no dual-ops capable providers are available, return the best provider
 * for me1 only, and sets *prov_mt2 to CRYPTO_INVALID_MECHID;
 * We assume/expect that a slower HW capable of the dual is still
 * faster than the 2 fastest providers capable of the individual ops
 * separately.
 */
kcf_provider_desc_t *
kcf_get_dual_provider(crypto_mechanism_t *mech1, crypto_mechanism_t *mech2,
    kcf_mech_entry_t **mepp, crypto_mech_type_t *prov_mt1,
    crypto_mech_type_t *prov_mt2, int *error, kcf_prov_tried_t *triedl,
    crypto_func_group_t fg1, crypto_func_group_t fg2, boolean_t call_restrict,
    size_t data_size)
{
	kcf_provider_desc_t *pd = NULL, *pdm1 = NULL, *pdm1m2 = NULL;
	kcf_prov_mech_desc_t *prov_chain, *mdesc;
	int len, gqlen = INT_MAX, dgqlen = INT_MAX;
	crypto_mech_info_list_t *mil;
	crypto_mech_type_t m2id =  mech2->cm_type;
	kcf_mech_entry_t *me;

	/* when mech is a valid mechanism, me will be its mech_entry */
	if (kcf_get_mech_entry(mech1->cm_type, &me) != KCF_SUCCESS) {
		*error = CRYPTO_MECHANISM_INVALID;
		return (NULL);
	}

	*prov_mt2 = CRYPTO_MECH_INVALID;

	if (mepp != NULL)
		*mepp = me;
	mutex_enter(&me->me_mutex);

	prov_chain = me->me_hw_prov_chain;
	/*
	 * We check the threshold for using a hardware provider for
	 * this amount of data. If there is no software provider available
	 * for the first mechanism, then the threshold is ignored.
	 */
	if ((prov_chain != NULL) &&
	    ((data_size == 0) || (me->me_threshold == 0) ||
	    (data_size >= me->me_threshold) ||
	    ((mdesc = me->me_sw_prov) == NULL) ||
	    (!IS_FG_SUPPORTED(mdesc, fg1)) ||
	    (!KCF_IS_PROV_USABLE(mdesc->pm_prov_desc)))) {
		/* there is at least one provider */
		ASSERT(me->me_num_hwprov > 0);

		/*
		 * Find the least loaded provider capable of the combo
		 * me1 + me2, and save a pointer to the least loaded
		 * provider capable of me1 only.
		 */
		while (prov_chain != NULL) {
			pd = prov_chain->pm_prov_desc;
			len = KCF_PROV_LOAD(pd);

			if (!IS_FG_SUPPORTED(prov_chain, fg1) ||
			    !KCF_IS_PROV_USABLE(pd) ||
			    IS_PROVIDER_TRIED(pd, triedl) ||
			    (call_restrict &&
			    (pd->pd_flags & KCF_PROV_RESTRICTED))) {
				prov_chain = prov_chain->pm_next;
				continue;
			}

			/* Save the best provider capable of m1 */
			if (len < gqlen) {
				*prov_mt1 =
				    prov_chain->pm_mech_info.cm_mech_number;
				gqlen = len;
				pdm1 = pd;
			}

			/* See if pd can do me2 too */
			for (mil = prov_chain->pm_mi_list;
			    mil != NULL; mil = mil->ml_next) {
				if ((mil->ml_mech_info.cm_func_group_mask &
				    fg2) == 0)
					continue;

				if ((mil->ml_kcf_mechid == m2id) &&
				    (len < dgqlen)) {
					/* Bingo! */
					dgqlen = len;
					pdm1m2 = pd;
					*prov_mt2 =
					    mil->ml_mech_info.cm_mech_number;
					*prov_mt1 = prov_chain->
					    pm_mech_info.cm_mech_number;
					break;
				}
			}

			prov_chain = prov_chain->pm_next;
		}

		pd =  (pdm1m2 != NULL) ? pdm1m2 : pdm1;
	}

	/* no HW provider for this mech, is there a SW provider? */
	if (pd == NULL && (mdesc = me->me_sw_prov) != NULL) {
		pd = mdesc->pm_prov_desc;
		if (!IS_FG_SUPPORTED(mdesc, fg1) ||
		    !KCF_IS_PROV_USABLE(pd) ||
		    IS_PROVIDER_TRIED(pd, triedl) ||
		    (call_restrict && (pd->pd_flags & KCF_PROV_RESTRICTED)))
			pd = NULL;
		else {
			/* See if pd can do me2 too */
			for (mil = me->me_sw_prov->pm_mi_list;
			    mil != NULL; mil = mil->ml_next) {
				if ((mil->ml_mech_info.cm_func_group_mask &
				    fg2) == 0)
					continue;

				if (mil->ml_kcf_mechid == m2id) {
					/* Bingo! */
					*prov_mt2 =
					    mil->ml_mech_info.cm_mech_number;
					break;
				}
			}
			*prov_mt1 = me->me_sw_prov->pm_mech_info.cm_mech_number;
		}
	}

	if (pd == NULL)
		*error = CRYPTO_MECH_NOT_SUPPORTED;
	else
		KCF_PROV_REFHOLD(pd);

	mutex_exit(&me->me_mutex);
	return (pd);
}

/*
 * Do the actual work of calling the provider routines.
 *
 * pd - Provider structure
 * ctx - Context for this operation
 * params - Parameters for this operation
 * rhndl - Request handle to use for notification
 *
 * The return values are the same as that of the respective SPI.
 */
int
common_submit_request(kcf_provider_desc_t *pd, crypto_ctx_t *ctx,
    kcf_req_params_t *params, crypto_req_handle_t rhndl)
{
	int err = CRYPTO_ARGUMENTS_BAD;
	kcf_op_type_t optype;

	optype = params->rp_optype;

	switch (params->rp_opgrp) {
	case KCF_OG_DIGEST: {
		kcf_digest_ops_params_t *dops = &params->rp_u.digest_params;

		switch (optype) {
		case KCF_OP_INIT:
			/*
			 * We should do this only here and not in KCF_WRAP_*
			 * macros. This is because we may want to try other
			 * providers, in case we recover from a failure.
			 */
			KCF_SET_PROVIDER_MECHNUM(dops->do_framework_mechtype,
			    pd, &dops->do_mech);

			err = KCF_PROV_DIGEST_INIT(pd, ctx, &dops->do_mech,
			    rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_DIGEST(pd, ctx, dops->do_data,
			    dops->do_digest, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_DIGEST_UPDATE(pd, ctx,
			    dops->do_data, rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_DIGEST_FINAL(pd, ctx,
			    dops->do_digest, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(dops->do_framework_mechtype,
			    pd, &dops->do_mech);
			err = KCF_PROV_DIGEST_ATOMIC(pd, dops->do_sid,
			    &dops->do_mech, dops->do_data, dops->do_digest,
			    rhndl);
			break;

		case KCF_OP_DIGEST_KEY:
			err = KCF_PROV_DIGEST_KEY(pd, ctx, dops->do_digest_key,
			    rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_MAC: {
		kcf_mac_ops_params_t *mops = &params->rp_u.mac_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(mops->mo_framework_mechtype,
			    pd, &mops->mo_mech);

			err = KCF_PROV_MAC_INIT(pd, ctx, &mops->mo_mech,
			    mops->mo_key, mops->mo_templ, rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_MAC(pd, ctx, mops->mo_data,
			    mops->mo_mac, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_MAC_UPDATE(pd, ctx, mops->mo_data,
			    rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_MAC_FINAL(pd, ctx, mops->mo_mac, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(mops->mo_framework_mechtype,
			    pd, &mops->mo_mech);

			err = KCF_PROV_MAC_ATOMIC(pd, mops->mo_sid,
			    &mops->mo_mech, mops->mo_key, mops->mo_data,
			    mops->mo_mac, mops->mo_templ, rhndl);
			break;

		case KCF_OP_MAC_VERIFY_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(mops->mo_framework_mechtype,
			    pd, &mops->mo_mech);

			err = KCF_PROV_MAC_VERIFY_ATOMIC(pd, mops->mo_sid,
			    &mops->mo_mech, mops->mo_key, mops->mo_data,
			    mops->mo_mac, mops->mo_templ, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_ENCRYPT: {
		kcf_encrypt_ops_params_t *eops = &params->rp_u.encrypt_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(eops->eo_framework_mechtype,
			    pd, &eops->eo_mech);

			err = KCF_PROV_ENCRYPT_INIT(pd, ctx, &eops->eo_mech,
			    eops->eo_key, eops->eo_templ, rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_ENCRYPT(pd, ctx, eops->eo_plaintext,
			    eops->eo_ciphertext, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_ENCRYPT_UPDATE(pd, ctx,
			    eops->eo_plaintext, eops->eo_ciphertext, rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_ENCRYPT_FINAL(pd, ctx,
			    eops->eo_ciphertext, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(eops->eo_framework_mechtype,
			    pd, &eops->eo_mech);

			err = KCF_PROV_ENCRYPT_ATOMIC(pd, eops->eo_sid,
			    &eops->eo_mech, eops->eo_key, eops->eo_plaintext,
			    eops->eo_ciphertext, eops->eo_templ, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_DECRYPT: {
		kcf_decrypt_ops_params_t *dcrops = &params->rp_u.decrypt_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(dcrops->dop_framework_mechtype,
			    pd, &dcrops->dop_mech);

			err = KCF_PROV_DECRYPT_INIT(pd, ctx, &dcrops->dop_mech,
			    dcrops->dop_key, dcrops->dop_templ, rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_DECRYPT(pd, ctx, dcrops->dop_ciphertext,
			    dcrops->dop_plaintext, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_DECRYPT_UPDATE(pd, ctx,
			    dcrops->dop_ciphertext, dcrops->dop_plaintext,
			    rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_DECRYPT_FINAL(pd, ctx,
			    dcrops->dop_plaintext, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(dcrops->dop_framework_mechtype,
			    pd, &dcrops->dop_mech);

			err = KCF_PROV_DECRYPT_ATOMIC(pd, dcrops->dop_sid,
			    &dcrops->dop_mech, dcrops->dop_key,
			    dcrops->dop_ciphertext, dcrops->dop_plaintext,
			    dcrops->dop_templ, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_SIGN: {
		kcf_sign_ops_params_t *sops = &params->rp_u.sign_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(sops->so_framework_mechtype,
			    pd, &sops->so_mech);

			err = KCF_PROV_SIGN_INIT(pd, ctx, &sops->so_mech,
			    sops->so_key, sops->so_templ, rhndl);
			break;

		case KCF_OP_SIGN_RECOVER_INIT:
			KCF_SET_PROVIDER_MECHNUM(sops->so_framework_mechtype,
			    pd, &sops->so_mech);

			err = KCF_PROV_SIGN_RECOVER_INIT(pd, ctx,
			    &sops->so_mech, sops->so_key, sops->so_templ,
			    rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_SIGN(pd, ctx, sops->so_data,
			    sops->so_signature, rhndl);
			break;

		case KCF_OP_SIGN_RECOVER:
			err = KCF_PROV_SIGN_RECOVER(pd, ctx,
			    sops->so_data, sops->so_signature, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_SIGN_UPDATE(pd, ctx, sops->so_data,
			    rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_SIGN_FINAL(pd, ctx, sops->so_signature,
			    rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(sops->so_framework_mechtype,
			    pd, &sops->so_mech);

			err = KCF_PROV_SIGN_ATOMIC(pd, sops->so_sid,
			    &sops->so_mech, sops->so_key, sops->so_data,
			    sops->so_templ, sops->so_signature, rhndl);
			break;

		case KCF_OP_SIGN_RECOVER_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(sops->so_framework_mechtype,
			    pd, &sops->so_mech);

			err = KCF_PROV_SIGN_RECOVER_ATOMIC(pd, sops->so_sid,
			    &sops->so_mech, sops->so_key, sops->so_data,
			    sops->so_templ, sops->so_signature, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_VERIFY: {
		kcf_verify_ops_params_t *vops = &params->rp_u.verify_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(vops->vo_framework_mechtype,
			    pd, &vops->vo_mech);

			err = KCF_PROV_VERIFY_INIT(pd, ctx, &vops->vo_mech,
			    vops->vo_key, vops->vo_templ, rhndl);
			break;

		case KCF_OP_VERIFY_RECOVER_INIT:
			KCF_SET_PROVIDER_MECHNUM(vops->vo_framework_mechtype,
			    pd, &vops->vo_mech);

			err = KCF_PROV_VERIFY_RECOVER_INIT(pd, ctx,
			    &vops->vo_mech, vops->vo_key, vops->vo_templ,
			    rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_VERIFY(pd, ctx, vops->vo_data,
			    vops->vo_signature, rhndl);
			break;

		case KCF_OP_VERIFY_RECOVER:
			err = KCF_PROV_VERIFY_RECOVER(pd, ctx,
			    vops->vo_signature, vops->vo_data, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_VERIFY_UPDATE(pd, ctx, vops->vo_data,
			    rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_VERIFY_FINAL(pd, ctx, vops->vo_signature,
			    rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(vops->vo_framework_mechtype,
			    pd, &vops->vo_mech);

			err = KCF_PROV_VERIFY_ATOMIC(pd, vops->vo_sid,
			    &vops->vo_mech, vops->vo_key, vops->vo_data,
			    vops->vo_templ, vops->vo_signature, rhndl);
			break;

		case KCF_OP_VERIFY_RECOVER_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(vops->vo_framework_mechtype,
			    pd, &vops->vo_mech);

			err = KCF_PROV_VERIFY_RECOVER_ATOMIC(pd, vops->vo_sid,
			    &vops->vo_mech, vops->vo_key, vops->vo_signature,
			    vops->vo_templ, vops->vo_data, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_ENCRYPT_MAC: {
		kcf_encrypt_mac_ops_params_t *eops =
		    &params->rp_u.encrypt_mac_params;
		kcf_context_t *kcf_secondctx;

		switch (optype) {
		case KCF_OP_INIT:
			kcf_secondctx = ((kcf_context_t *)
			    (ctx->cc_framework_private))->kc_secondctx;

			if (kcf_secondctx != NULL) {
				err = kcf_emulate_dual(pd, ctx, params);
				break;
			}
			KCF_SET_PROVIDER_MECHNUM(
			    eops->em_framework_encr_mechtype,
			    pd, &eops->em_encr_mech);

			KCF_SET_PROVIDER_MECHNUM(
			    eops->em_framework_mac_mechtype,
			    pd, &eops->em_mac_mech);

			err = KCF_PROV_ENCRYPT_MAC_INIT(pd, ctx,
			    &eops->em_encr_mech, eops->em_encr_key,
			    &eops->em_mac_mech, eops->em_mac_key,
			    eops->em_encr_templ, eops->em_mac_templ,
			    rhndl);

			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_ENCRYPT_MAC(pd, ctx,
			    eops->em_plaintext, eops->em_ciphertext,
			    eops->em_mac, rhndl);
			break;

		case KCF_OP_UPDATE:
			kcf_secondctx = ((kcf_context_t *)
			    (ctx->cc_framework_private))->kc_secondctx;
			if (kcf_secondctx != NULL) {
				err = kcf_emulate_dual(pd, ctx, params);
				break;
			}
			err = KCF_PROV_ENCRYPT_MAC_UPDATE(pd, ctx,
			    eops->em_plaintext, eops->em_ciphertext, rhndl);
			break;

		case KCF_OP_FINAL:
			kcf_secondctx = ((kcf_context_t *)
			    (ctx->cc_framework_private))->kc_secondctx;
			if (kcf_secondctx != NULL) {
				err = kcf_emulate_dual(pd, ctx, params);
				break;
			}
			err = KCF_PROV_ENCRYPT_MAC_FINAL(pd, ctx,
			    eops->em_ciphertext, eops->em_mac, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);

			KCF_SET_PROVIDER_MECHNUM(
			    eops->em_framework_encr_mechtype,
			    pd, &eops->em_encr_mech);

			KCF_SET_PROVIDER_MECHNUM(
			    eops->em_framework_mac_mechtype,
			    pd, &eops->em_mac_mech);

			err = KCF_PROV_ENCRYPT_MAC_ATOMIC(pd, eops->em_sid,
			    &eops->em_encr_mech, eops->em_encr_key,
			    &eops->em_mac_mech, eops->em_mac_key,
			    eops->em_plaintext, eops->em_ciphertext,
			    eops->em_mac,
			    eops->em_encr_templ, eops->em_mac_templ,
			    rhndl);

			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_MAC_DECRYPT: {
		kcf_mac_decrypt_ops_params_t *dops =
		    &params->rp_u.mac_decrypt_params;
		kcf_context_t *kcf_secondctx;

		switch (optype) {
		case KCF_OP_INIT:
			kcf_secondctx = ((kcf_context_t *)
			    (ctx->cc_framework_private))->kc_secondctx;

			if (kcf_secondctx != NULL) {
				err = kcf_emulate_dual(pd, ctx, params);
				break;
			}
			KCF_SET_PROVIDER_MECHNUM(
			    dops->md_framework_mac_mechtype,
			    pd, &dops->md_mac_mech);

			KCF_SET_PROVIDER_MECHNUM(
			    dops->md_framework_decr_mechtype,
			    pd, &dops->md_decr_mech);

			err = KCF_PROV_MAC_DECRYPT_INIT(pd, ctx,
			    &dops->md_mac_mech, dops->md_mac_key,
			    &dops->md_decr_mech, dops->md_decr_key,
			    dops->md_mac_templ, dops->md_decr_templ,
			    rhndl);

			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_MAC_DECRYPT(pd, ctx,
			    dops->md_ciphertext, dops->md_mac,
			    dops->md_plaintext, rhndl);
			break;

		case KCF_OP_UPDATE:
			kcf_secondctx = ((kcf_context_t *)
			    (ctx->cc_framework_private))->kc_secondctx;
			if (kcf_secondctx != NULL) {
				err = kcf_emulate_dual(pd, ctx, params);
				break;
			}
			err = KCF_PROV_MAC_DECRYPT_UPDATE(pd, ctx,
			    dops->md_ciphertext, dops->md_plaintext, rhndl);
			break;

		case KCF_OP_FINAL:
			kcf_secondctx = ((kcf_context_t *)
			    (ctx->cc_framework_private))->kc_secondctx;
			if (kcf_secondctx != NULL) {
				err = kcf_emulate_dual(pd, ctx, params);
				break;
			}
			err = KCF_PROV_MAC_DECRYPT_FINAL(pd, ctx,
			    dops->md_mac, dops->md_plaintext, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);

			KCF_SET_PROVIDER_MECHNUM(
			    dops->md_framework_mac_mechtype,
			    pd, &dops->md_mac_mech);

			KCF_SET_PROVIDER_MECHNUM(
			    dops->md_framework_decr_mechtype,
			    pd, &dops->md_decr_mech);

			err = KCF_PROV_MAC_DECRYPT_ATOMIC(pd, dops->md_sid,
			    &dops->md_mac_mech, dops->md_mac_key,
			    &dops->md_decr_mech, dops->md_decr_key,
			    dops->md_ciphertext, dops->md_mac,
			    dops->md_plaintext,
			    dops->md_mac_templ, dops->md_decr_templ,
			    rhndl);

			break;

		case KCF_OP_MAC_VERIFY_DECRYPT_ATOMIC:
			ASSERT(ctx == NULL);

			KCF_SET_PROVIDER_MECHNUM(
			    dops->md_framework_mac_mechtype,
			    pd, &dops->md_mac_mech);

			KCF_SET_PROVIDER_MECHNUM(
			    dops->md_framework_decr_mechtype,
			    pd, &dops->md_decr_mech);

			err = KCF_PROV_MAC_VERIFY_DECRYPT_ATOMIC(pd,
			    dops->md_sid, &dops->md_mac_mech, dops->md_mac_key,
			    &dops->md_decr_mech, dops->md_decr_key,
			    dops->md_ciphertext, dops->md_mac,
			    dops->md_plaintext,
			    dops->md_mac_templ, dops->md_decr_templ,
			    rhndl);

			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_KEY: {
		kcf_key_ops_params_t *kops = &params->rp_u.key_params;

		ASSERT(ctx == NULL);
		KCF_SET_PROVIDER_MECHNUM(kops->ko_framework_mechtype, pd,
		    &kops->ko_mech);

		switch (optype) {
		case KCF_OP_KEY_GENERATE:
			err = KCF_PROV_KEY_GENERATE(pd, kops->ko_sid,
			    &kops->ko_mech,
			    kops->ko_key_template, kops->ko_key_attribute_count,
			    kops->ko_key_object_id_ptr, rhndl);
			break;

		case KCF_OP_KEY_GENERATE_PAIR:
			err = KCF_PROV_KEY_GENERATE_PAIR(pd, kops->ko_sid,
			    &kops->ko_mech,
			    kops->ko_key_template, kops->ko_key_attribute_count,
			    kops->ko_private_key_template,
			    kops->ko_private_key_attribute_count,
			    kops->ko_key_object_id_ptr,
			    kops->ko_private_key_object_id_ptr, rhndl);
			break;

		case KCF_OP_KEY_WRAP:
			err = KCF_PROV_KEY_WRAP(pd, kops->ko_sid,
			    &kops->ko_mech,
			    kops->ko_key, kops->ko_key_object_id_ptr,
			    kops->ko_wrapped_key, kops->ko_wrapped_key_len_ptr,
			    rhndl);
			break;

		case KCF_OP_KEY_UNWRAP:
			err = KCF_PROV_KEY_UNWRAP(pd, kops->ko_sid,
			    &kops->ko_mech,
			    kops->ko_key, kops->ko_wrapped_key,
			    kops->ko_wrapped_key_len_ptr,
			    kops->ko_key_template, kops->ko_key_attribute_count,
			    kops->ko_key_object_id_ptr, rhndl);
			break;

		case KCF_OP_KEY_DERIVE:
			err = KCF_PROV_KEY_DERIVE(pd, kops->ko_sid,
			    &kops->ko_mech,
			    kops->ko_key, kops->ko_key_template,
			    kops->ko_key_attribute_count,
			    kops->ko_key_object_id_ptr, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_RANDOM: {
		kcf_random_number_ops_params_t *rops =
		    &params->rp_u.random_number_params;

		ASSERT(ctx == NULL);

		switch (optype) {
		case KCF_OP_RANDOM_SEED:
			err = KCF_PROV_SEED_RANDOM(pd, rops->rn_sid,
			    rops->rn_buf, rops->rn_buflen, rops->rn_entropy_est,
			    rops->rn_flags, rhndl);
			break;

		case KCF_OP_RANDOM_GENERATE:
			err = KCF_PROV_GENERATE_RANDOM(pd, rops->rn_sid,
			    rops->rn_buf, rops->rn_buflen, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_SESSION: {
		kcf_session_ops_params_t *sops = &params->rp_u.session_params;

		ASSERT(ctx == NULL);
		switch (optype) {
		case KCF_OP_SESSION_OPEN:
			/*
			 * so_pd may be a logical provider, in which case
			 * we need to check whether it has been removed.
			 */
			if (KCF_IS_PROV_REMOVED(sops->so_pd)) {
				err = CRYPTO_DEVICE_ERROR;
				break;
			}
			err = KCF_PROV_SESSION_OPEN(pd, sops->so_sid_ptr,
			    rhndl, sops->so_pd);
			break;

		case KCF_OP_SESSION_CLOSE:
			/*
			 * so_pd may be a logical provider, in which case
			 * we need to check whether it has been removed.
			 */
			if (KCF_IS_PROV_REMOVED(sops->so_pd)) {
				err = CRYPTO_DEVICE_ERROR;
				break;
			}
			err = KCF_PROV_SESSION_CLOSE(pd, sops->so_sid,
			    rhndl, sops->so_pd);
			break;

		case KCF_OP_SESSION_LOGIN:
			err = KCF_PROV_SESSION_LOGIN(pd, sops->so_sid,
			    sops->so_user_type, sops->so_pin,
			    sops->so_pin_len, rhndl);
			break;

		case KCF_OP_SESSION_LOGOUT:
			err = KCF_PROV_SESSION_LOGOUT(pd, sops->so_sid, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_OBJECT: {
		kcf_object_ops_params_t *jops = &params->rp_u.object_params;

		ASSERT(ctx == NULL);
		switch (optype) {
		case KCF_OP_OBJECT_CREATE:
			err = KCF_PROV_OBJECT_CREATE(pd, jops->oo_sid,
			    jops->oo_template, jops->oo_attribute_count,
			    jops->oo_object_id_ptr, rhndl);
			break;

		case KCF_OP_OBJECT_COPY:
			err = KCF_PROV_OBJECT_COPY(pd, jops->oo_sid,
			    jops->oo_object_id,
			    jops->oo_template, jops->oo_attribute_count,
			    jops->oo_object_id_ptr, rhndl);
			break;

		case KCF_OP_OBJECT_DESTROY:
			err = KCF_PROV_OBJECT_DESTROY(pd, jops->oo_sid,
			    jops->oo_object_id, rhndl);
			break;

		case KCF_OP_OBJECT_GET_SIZE:
			err = KCF_PROV_OBJECT_GET_SIZE(pd, jops->oo_sid,
			    jops->oo_object_id, jops->oo_object_size, rhndl);
			break;

		case KCF_OP_OBJECT_GET_ATTRIBUTE_VALUE:
			err = KCF_PROV_OBJECT_GET_ATTRIBUTE_VALUE(pd,
			    jops->oo_sid, jops->oo_object_id,
			    jops->oo_template, jops->oo_attribute_count, rhndl);
			break;

		case KCF_OP_OBJECT_SET_ATTRIBUTE_VALUE:
			err = KCF_PROV_OBJECT_SET_ATTRIBUTE_VALUE(pd,
			    jops->oo_sid, jops->oo_object_id,
			    jops->oo_template, jops->oo_attribute_count, rhndl);
			break;

		case KCF_OP_OBJECT_FIND_INIT:
			err = KCF_PROV_OBJECT_FIND_INIT(pd, jops->oo_sid,
			    jops->oo_template, jops->oo_attribute_count,
			    jops->oo_find_init_pp_ptr, rhndl);
			break;

		case KCF_OP_OBJECT_FIND:
			err = KCF_PROV_OBJECT_FIND(pd, jops->oo_find_pp,
			    jops->oo_object_id_ptr, jops->oo_max_object_count,
			    jops->oo_object_count_ptr, rhndl);
			break;

		case KCF_OP_OBJECT_FIND_FINAL:
			err = KCF_PROV_OBJECT_FIND_FINAL(pd, jops->oo_find_pp,
			    rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_PROVMGMT: {
		kcf_provmgmt_ops_params_t *pops = &params->rp_u.provmgmt_params;

		ASSERT(ctx == NULL);
		switch (optype) {
		case KCF_OP_MGMT_EXTINFO:
			/*
			 * po_pd may be a logical provider, in which case
			 * we need to check whether it has been removed.
			 */
			if (KCF_IS_PROV_REMOVED(pops->po_pd)) {
				err = CRYPTO_DEVICE_ERROR;
				break;
			}
			err = KCF_PROV_EXT_INFO(pd, pops->po_ext_info, rhndl,
			    pops->po_pd);
			break;

		case KCF_OP_MGMT_INITTOKEN:
			err = KCF_PROV_INIT_TOKEN(pd, pops->po_pin,
			    pops->po_pin_len, pops->po_label, rhndl);
			break;

		case KCF_OP_MGMT_INITPIN:
			err = KCF_PROV_INIT_PIN(pd, pops->po_sid, pops->po_pin,
			    pops->po_pin_len, rhndl);
			break;

		case KCF_OP_MGMT_SETPIN:
			err = KCF_PROV_SET_PIN(pd, pops->po_sid,
			    pops->po_old_pin, pops->po_old_pin_len,
			    pops->po_pin, pops->po_pin_len, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_NOSTORE_KEY: {
		kcf_key_ops_params_t *kops = &params->rp_u.key_params;

		ASSERT(ctx == NULL);
		KCF_SET_PROVIDER_MECHNUM(kops->ko_framework_mechtype, pd,
		    &kops->ko_mech);

		switch (optype) {
		case KCF_OP_KEY_GENERATE:
			err = KCF_PROV_NOSTORE_KEY_GENERATE(pd, kops->ko_sid,
			    &kops->ko_mech, kops->ko_key_template,
			    kops->ko_key_attribute_count,
			    kops->ko_out_template1,
			    kops->ko_out_attribute_count1, rhndl);
			break;

		case KCF_OP_KEY_GENERATE_PAIR:
			err = KCF_PROV_NOSTORE_KEY_GENERATE_PAIR(pd,
			    kops->ko_sid, &kops->ko_mech,
			    kops->ko_key_template, kops->ko_key_attribute_count,
			    kops->ko_private_key_template,
			    kops->ko_private_key_attribute_count,
			    kops->ko_out_template1,
			    kops->ko_out_attribute_count1,
			    kops->ko_out_template2,
			    kops->ko_out_attribute_count2,
			    rhndl);
			break;

		case KCF_OP_KEY_DERIVE:
			err = KCF_PROV_NOSTORE_KEY_DERIVE(pd, kops->ko_sid,
			    &kops->ko_mech, kops->ko_key,
			    kops->ko_key_template,
			    kops->ko_key_attribute_count,
			    kops->ko_out_template1,
			    kops->ko_out_attribute_count1, rhndl);
			break;

		default:
			break;
		}
		break;
	}
	default:
		break;
	}		/* end of switch(params->rp_opgrp) */

	KCF_PROV_INCRSTATS(pd, err);
	return (err);
}


/*
 * Emulate the call for a multipart dual ops with 2 single steps.
 * This routine is always called in the context of a working thread
 * running kcf_svc_do_run().
 * The single steps are submitted in a pure synchronous way (blocking).
 * When this routine returns, kcf_svc_do_run() will call kcf_aop_done()
 * so the originating consumer's callback gets invoked. kcf_aop_done()
 * takes care of freeing the operation context. So, this routine does
 * not free the operation context.
 *
 * The provider descriptor is assumed held by the callers.
 */
static int
kcf_emulate_dual(kcf_provider_desc_t *pd, crypto_ctx_t *ctx,
    kcf_req_params_t *params)
{
	int err = CRYPTO_ARGUMENTS_BAD;
	kcf_op_type_t optype;
	size_t save_len;
	off_t save_offset;

	optype = params->rp_optype;

	switch (params->rp_opgrp) {
	case KCF_OG_ENCRYPT_MAC: {
		kcf_encrypt_mac_ops_params_t *cmops =
		    &params->rp_u.encrypt_mac_params;
		kcf_context_t *encr_kcf_ctx;
		crypto_ctx_t *mac_ctx;
		kcf_req_params_t encr_params;

		encr_kcf_ctx = (kcf_context_t *)(ctx->cc_framework_private);

		switch (optype) {
		case KCF_OP_INIT: {
			encr_kcf_ctx->kc_secondctx = NULL;

			KCF_WRAP_ENCRYPT_OPS_PARAMS(&encr_params, KCF_OP_INIT,
			    pd->pd_sid, &cmops->em_encr_mech,
			    cmops->em_encr_key, NULL, NULL,
			    cmops->em_encr_templ);

			err = kcf_submit_request(pd, ctx, NULL, &encr_params,
			    B_FALSE);

			/* It can't be CRYPTO_QUEUED */
			if (err != CRYPTO_SUCCESS) {
				break;
			}

			err = crypto_mac_init(&cmops->em_mac_mech,
			    cmops->em_mac_key, cmops->em_mac_templ,
			    (crypto_context_t *)&mac_ctx, NULL);

			if (err == CRYPTO_SUCCESS) {
				encr_kcf_ctx->kc_secondctx = (kcf_context_t *)
				    mac_ctx->cc_framework_private;
				KCF_CONTEXT_REFHOLD((kcf_context_t *)
				    mac_ctx->cc_framework_private);
			}

			break;

		}
		case KCF_OP_UPDATE: {
			crypto_dual_data_t *ct = cmops->em_ciphertext;
			crypto_data_t *pt = cmops->em_plaintext;
			kcf_context_t *mac_kcf_ctx = encr_kcf_ctx->kc_secondctx;
			crypto_ctx_t *mac_ctx = &mac_kcf_ctx->kc_glbl_ctx;

			KCF_WRAP_ENCRYPT_OPS_PARAMS(&encr_params, KCF_OP_UPDATE,
			    pd->pd_sid, NULL, NULL, pt, (crypto_data_t *)ct,
			    NULL);

			err = kcf_submit_request(pd, ctx, NULL, &encr_params,
			    B_FALSE);

			/* It can't be CRYPTO_QUEUED */
			if (err != CRYPTO_SUCCESS) {
				break;
			}

			save_offset = ct->dd_offset1;
			save_len = ct->dd_len1;
			if (ct->dd_len2 == 0) {
				/*
				 * The previous encrypt step was an
				 * accumulation only and didn't produce any
				 * partial output
				 */
				if (ct->dd_len1 == 0)
					break;

			} else {
				ct->dd_offset1 = ct->dd_offset2;
				ct->dd_len1 = ct->dd_len2;
			}
			err = crypto_mac_update((crypto_context_t)mac_ctx,
			    (crypto_data_t *)ct, NULL);

			ct->dd_offset1 = save_offset;
			ct->dd_len1 = save_len;

			break;
		}
		case KCF_OP_FINAL: {
			crypto_dual_data_t *ct = cmops->em_ciphertext;
			crypto_data_t *mac = cmops->em_mac;
			kcf_context_t *mac_kcf_ctx = encr_kcf_ctx->kc_secondctx;
			crypto_ctx_t *mac_ctx = &mac_kcf_ctx->kc_glbl_ctx;
			crypto_context_t mac_context = mac_ctx;

			KCF_WRAP_ENCRYPT_OPS_PARAMS(&encr_params, KCF_OP_FINAL,
			    pd->pd_sid, NULL, NULL, NULL, (crypto_data_t *)ct,
			    NULL);

			err = kcf_submit_request(pd, ctx, NULL, &encr_params,
			    B_FALSE);

			/* It can't be CRYPTO_QUEUED */
			if (err != CRYPTO_SUCCESS) {
				crypto_cancel_ctx(mac_context);
				break;
			}

			if (ct->dd_len2 > 0) {
				save_offset = ct->dd_offset1;
				save_len = ct->dd_len1;
				ct->dd_offset1 = ct->dd_offset2;
				ct->dd_len1 = ct->dd_len2;

				err = crypto_mac_update(mac_context,
				    (crypto_data_t *)ct, NULL);

				ct->dd_offset1 = save_offset;
				ct->dd_len1 = save_len;

				if (err != CRYPTO_SUCCESS)  {
					crypto_cancel_ctx(mac_context);
					return (err);
				}
			}

			/* and finally, collect the MAC */
			err = crypto_mac_final(mac_context, mac, NULL);
			break;
		}

		default:
			break;
		}
		KCF_PROV_INCRSTATS(pd, err);
		break;
	}
	case KCF_OG_MAC_DECRYPT: {
		kcf_mac_decrypt_ops_params_t *mdops =
		    &params->rp_u.mac_decrypt_params;
		kcf_context_t *decr_kcf_ctx;
		crypto_ctx_t *mac_ctx;
		kcf_req_params_t decr_params;

		decr_kcf_ctx = (kcf_context_t *)(ctx->cc_framework_private);

		switch (optype) {
		case KCF_OP_INIT: {
			decr_kcf_ctx->kc_secondctx = NULL;

			err = crypto_mac_init(&mdops->md_mac_mech,
			    mdops->md_mac_key, mdops->md_mac_templ,
			    (crypto_context_t *)&mac_ctx, NULL);

			/* It can't be CRYPTO_QUEUED */
			if (err != CRYPTO_SUCCESS) {
				break;
			}

			KCF_WRAP_DECRYPT_OPS_PARAMS(&decr_params, KCF_OP_INIT,
			    pd->pd_sid, &mdops->md_decr_mech,
			    mdops->md_decr_key, NULL, NULL,
			    mdops->md_decr_templ);

			err = kcf_submit_request(pd, ctx, NULL, &decr_params,
			    B_FALSE);

			/* It can't be CRYPTO_QUEUED */
			if (err != CRYPTO_SUCCESS) {
				crypto_cancel_ctx((crypto_context_t)mac_ctx);
				break;
			}

			decr_kcf_ctx->kc_secondctx = (kcf_context_t *)
			    mac_ctx->cc_framework_private;
			KCF_CONTEXT_REFHOLD((kcf_context_t *)
			    mac_ctx->cc_framework_private);

			break;
		default:
			break;

		}
		case KCF_OP_UPDATE: {
			crypto_dual_data_t *ct = mdops->md_ciphertext;
			crypto_data_t *pt = mdops->md_plaintext;
			kcf_context_t *mac_kcf_ctx = decr_kcf_ctx->kc_secondctx;
			crypto_ctx_t *mac_ctx = &mac_kcf_ctx->kc_glbl_ctx;

			err = crypto_mac_update((crypto_context_t)mac_ctx,
			    (crypto_data_t *)ct, NULL);

			if (err != CRYPTO_SUCCESS)
				break;

			save_offset = ct->dd_offset1;
			save_len = ct->dd_len1;

			/* zero ct->dd_len2 means decrypt everything */
			if (ct->dd_len2 > 0) {
				ct->dd_offset1 = ct->dd_offset2;
				ct->dd_len1 = ct->dd_len2;
			}

			err = crypto_decrypt_update((crypto_context_t)ctx,
			    (crypto_data_t *)ct, pt, NULL);

			ct->dd_offset1 = save_offset;
			ct->dd_len1 = save_len;

			break;
		}
		case KCF_OP_FINAL: {
			crypto_data_t *pt = mdops->md_plaintext;
			crypto_data_t *mac = mdops->md_mac;
			kcf_context_t *mac_kcf_ctx = decr_kcf_ctx->kc_secondctx;
			crypto_ctx_t *mac_ctx = &mac_kcf_ctx->kc_glbl_ctx;

			err = crypto_mac_final((crypto_context_t)mac_ctx,
			    mac, NULL);

			if (err != CRYPTO_SUCCESS) {
				crypto_cancel_ctx(ctx);
				break;
			}

			/* Get the last chunk of plaintext */
			KCF_CONTEXT_REFHOLD(decr_kcf_ctx);
			err = crypto_decrypt_final((crypto_context_t)ctx, pt,
			    NULL);

			break;
		}
		}
		break;
	}
	default:

		break;
	}		/* end of switch(params->rp_opgrp) */

	return (err);
}
