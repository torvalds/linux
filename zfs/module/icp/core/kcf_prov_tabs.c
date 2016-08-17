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
 * It implements the management of tables of Providers. Entries to
 * added and removed when cryptographic providers register with
 * and unregister from the framework, respectively. The KCF scheduler
 * and ioctl pseudo driver call this function to obtain the list
 * of available providers.
 *
 * The provider table is indexed by crypto_provider_id_t. Each
 * element of the table contains a pointer to a provider descriptor,
 * or NULL if the entry is free.
 *
 * This file also implements helper functions to allocate and free
 * provider descriptors.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/spi.h>

#define	KCF_MAX_PROVIDERS	512	/* max number of providers */

/*
 * Prov_tab is an array of providers which is updated when
 * a crypto provider registers with kcf. The provider calls the
 * SPI routine, crypto_register_provider(), which in turn calls
 * kcf_prov_tab_add_provider().
 *
 * A provider unregisters by calling crypto_unregister_provider()
 * which triggers the removal of the prov_tab entry.
 * It also calls kcf_remove_mech_provider().
 *
 * prov_tab entries are not updated from kcf.conf or by cryptoadm(1M).
 */
static kcf_provider_desc_t **prov_tab = NULL;
static kmutex_t prov_tab_mutex; /* ensure exclusive access to the table */
static uint_t prov_tab_num = 0; /* number of providers in table */
static uint_t prov_tab_max = KCF_MAX_PROVIDERS;

void
kcf_prov_tab_destroy(void)
{
	mutex_destroy(&prov_tab_mutex);

	if (prov_tab)
		kmem_free(prov_tab, prov_tab_max *
		    sizeof (kcf_provider_desc_t *));
}

/*
 * Initialize a mutex and the KCF providers table, prov_tab.
 * The providers table is dynamically allocated with prov_tab_max entries.
 * Called from kcf module _init().
 */
void
kcf_prov_tab_init(void)
{
	mutex_init(&prov_tab_mutex, NULL, MUTEX_DEFAULT, NULL);

	prov_tab = kmem_zalloc(prov_tab_max * sizeof (kcf_provider_desc_t *),
	    KM_SLEEP);
}

/*
 * Add a provider to the provider table. If no free entry can be found
 * for the new provider, returns CRYPTO_HOST_MEMORY. Otherwise, add
 * the provider to the table, initialize the pd_prov_id field
 * of the specified provider descriptor to the index in that table,
 * and return CRYPTO_SUCCESS. Note that a REFHOLD is done on the
 * provider when pointed to by a table entry.
 */
int
kcf_prov_tab_add_provider(kcf_provider_desc_t *prov_desc)
{
	uint_t i;

	ASSERT(prov_tab != NULL);

	mutex_enter(&prov_tab_mutex);

	/* find free slot in providers table */
	for (i = 1; i < KCF_MAX_PROVIDERS && prov_tab[i] != NULL; i++)
		;
	if (i == KCF_MAX_PROVIDERS) {
		/* ran out of providers entries */
		mutex_exit(&prov_tab_mutex);
		cmn_err(CE_WARN, "out of providers entries");
		return (CRYPTO_HOST_MEMORY);
	}

	/* initialize entry */
	prov_tab[i] = prov_desc;
	KCF_PROV_REFHOLD(prov_desc);
	KCF_PROV_IREFHOLD(prov_desc);
	prov_tab_num++;

	mutex_exit(&prov_tab_mutex);

	/* update provider descriptor */
	prov_desc->pd_prov_id = i;

	/*
	 * The KCF-private provider handle is defined as the internal
	 * provider id.
	 */
	prov_desc->pd_kcf_prov_handle =
	    (crypto_kcf_provider_handle_t)prov_desc->pd_prov_id;

	return (CRYPTO_SUCCESS);
}

/*
 * Remove the provider specified by its id. A REFRELE is done on the
 * corresponding provider descriptor before this function returns.
 * Returns CRYPTO_UNKNOWN_PROVIDER if the provider id is not valid.
 */
int
kcf_prov_tab_rem_provider(crypto_provider_id_t prov_id)
{
	kcf_provider_desc_t *prov_desc;

	ASSERT(prov_tab != NULL);
	ASSERT(prov_tab_num >= 0);

	/*
	 * Validate provider id, since it can be specified by a 3rd-party
	 * provider.
	 */

	mutex_enter(&prov_tab_mutex);
	if (prov_id >= KCF_MAX_PROVIDERS ||
	    ((prov_desc = prov_tab[prov_id]) == NULL)) {
		mutex_exit(&prov_tab_mutex);
		return (CRYPTO_INVALID_PROVIDER_ID);
	}
	mutex_exit(&prov_tab_mutex);

	/*
	 * The provider id must remain valid until the associated provider
	 * descriptor is freed. For this reason, we simply release our
	 * reference to the descriptor here. When the reference count
	 * reaches zero, kcf_free_provider_desc() will be invoked and
	 * the associated entry in the providers table will be released
	 * at that time.
	 */

	KCF_PROV_REFRELE(prov_desc);
	KCF_PROV_IREFRELE(prov_desc);

	return (CRYPTO_SUCCESS);
}

/*
 * Returns the provider descriptor corresponding to the specified
 * provider id. A REFHOLD is done on the descriptor before it is
 * returned to the caller. It is the responsibility of the caller
 * to do a REFRELE once it is done with the provider descriptor.
 */
kcf_provider_desc_t *
kcf_prov_tab_lookup(crypto_provider_id_t prov_id)
{
	kcf_provider_desc_t *prov_desc;

	mutex_enter(&prov_tab_mutex);

	prov_desc = prov_tab[prov_id];

	if (prov_desc == NULL) {
		mutex_exit(&prov_tab_mutex);
		return (NULL);
	}

	KCF_PROV_REFHOLD(prov_desc);

	mutex_exit(&prov_tab_mutex);

	return (prov_desc);
}

static void
allocate_ops_v1(crypto_ops_t *src, crypto_ops_t *dst, uint_t *mech_list_count)
{
	if (src->co_control_ops != NULL)
		dst->co_control_ops = kmem_alloc(sizeof (crypto_control_ops_t),
		    KM_SLEEP);

	if (src->co_digest_ops != NULL)
		dst->co_digest_ops = kmem_alloc(sizeof (crypto_digest_ops_t),
		    KM_SLEEP);

	if (src->co_cipher_ops != NULL)
		dst->co_cipher_ops = kmem_alloc(sizeof (crypto_cipher_ops_t),
		    KM_SLEEP);

	if (src->co_mac_ops != NULL)
		dst->co_mac_ops = kmem_alloc(sizeof (crypto_mac_ops_t),
		    KM_SLEEP);

	if (src->co_sign_ops != NULL)
		dst->co_sign_ops = kmem_alloc(sizeof (crypto_sign_ops_t),
		    KM_SLEEP);

	if (src->co_verify_ops != NULL)
		dst->co_verify_ops = kmem_alloc(sizeof (crypto_verify_ops_t),
		    KM_SLEEP);

	if (src->co_dual_ops != NULL)
		dst->co_dual_ops = kmem_alloc(sizeof (crypto_dual_ops_t),
		    KM_SLEEP);

	if (src->co_dual_cipher_mac_ops != NULL)
		dst->co_dual_cipher_mac_ops = kmem_alloc(
		    sizeof (crypto_dual_cipher_mac_ops_t), KM_SLEEP);

	if (src->co_random_ops != NULL) {
		dst->co_random_ops = kmem_alloc(
		    sizeof (crypto_random_number_ops_t), KM_SLEEP);

		/*
		 * Allocate storage to store the array of supported mechanisms
		 * specified by provider. We allocate extra mechanism storage
		 * if the provider has random_ops since we keep an internal
		 * mechanism, SUN_RANDOM, in this case.
		 */
		(*mech_list_count)++;
	}

	if (src->co_session_ops != NULL)
		dst->co_session_ops = kmem_alloc(sizeof (crypto_session_ops_t),
		    KM_SLEEP);

	if (src->co_object_ops != NULL)
		dst->co_object_ops = kmem_alloc(sizeof (crypto_object_ops_t),
		    KM_SLEEP);

	if (src->co_key_ops != NULL)
		dst->co_key_ops = kmem_alloc(sizeof (crypto_key_ops_t),
		    KM_SLEEP);

	if (src->co_provider_ops != NULL)
		dst->co_provider_ops = kmem_alloc(
		    sizeof (crypto_provider_management_ops_t), KM_SLEEP);

	if (src->co_ctx_ops != NULL)
		dst->co_ctx_ops = kmem_alloc(sizeof (crypto_ctx_ops_t),
		    KM_SLEEP);
}

static void
allocate_ops_v2(crypto_ops_t *src, crypto_ops_t *dst)
{
	if (src->co_mech_ops != NULL)
		dst->co_mech_ops = kmem_alloc(sizeof (crypto_mech_ops_t),
		    KM_SLEEP);
}

static void
allocate_ops_v3(crypto_ops_t *src, crypto_ops_t *dst)
{
	if (src->co_nostore_key_ops != NULL)
		dst->co_nostore_key_ops =
		    kmem_alloc(sizeof (crypto_nostore_key_ops_t), KM_SLEEP);
}

/*
 * Allocate a provider descriptor. mech_list_count specifies the
 * number of mechanisms supported by the providers, and is used
 * to allocate storage for the mechanism table.
 * This function may sleep while allocating memory, which is OK
 * since it is invoked from user context during provider registration.
 */
kcf_provider_desc_t *
kcf_alloc_provider_desc(crypto_provider_info_t *info)
{
	int i, j;
	kcf_provider_desc_t *desc;
	uint_t mech_list_count = info->pi_mech_list_count;
	crypto_ops_t *src_ops = info->pi_ops_vector;

	desc = kmem_zalloc(sizeof (kcf_provider_desc_t), KM_SLEEP);

	/*
	 * pd_description serves two purposes
	 * - Appears as a blank padded PKCS#11 style string, that will be
	 *   returned to applications in CK_SLOT_INFO.slotDescription.
	 *   This means that we should not have a null character in the
	 *   first CRYPTO_PROVIDER_DESCR_MAX_LEN bytes.
	 * - Appears as a null-terminated string that can be used by
	 *   other kcf routines.
	 *
	 * So, we allocate enough room for one extra null terminator
	 * which keeps every one happy.
	 */
	desc->pd_description = kmem_alloc(CRYPTO_PROVIDER_DESCR_MAX_LEN + 1,
	    KM_SLEEP);
	(void) memset(desc->pd_description, ' ',
	    CRYPTO_PROVIDER_DESCR_MAX_LEN);
	desc->pd_description[CRYPTO_PROVIDER_DESCR_MAX_LEN] = '\0';

	/*
	 * Since the framework does not require the ops vector specified
	 * by the providers during registration to be persistent,
	 * KCF needs to allocate storage where copies of the ops
	 * vectors are copied.
	 */
	desc->pd_ops_vector = kmem_zalloc(sizeof (crypto_ops_t), KM_SLEEP);

	if (info->pi_provider_type != CRYPTO_LOGICAL_PROVIDER) {
		allocate_ops_v1(src_ops, desc->pd_ops_vector, &mech_list_count);
		if (info->pi_interface_version >= CRYPTO_SPI_VERSION_2)
			allocate_ops_v2(src_ops, desc->pd_ops_vector);
		if (info->pi_interface_version == CRYPTO_SPI_VERSION_3)
			allocate_ops_v3(src_ops, desc->pd_ops_vector);
	}

	desc->pd_mech_list_count = mech_list_count;
	desc->pd_mechanisms = kmem_zalloc(sizeof (crypto_mech_info_t) *
	    mech_list_count, KM_SLEEP);
	for (i = 0; i < KCF_OPS_CLASSSIZE; i++)
		for (j = 0; j < KCF_MAXMECHTAB; j++)
			desc->pd_mech_indx[i][j] = KCF_INVALID_INDX;

	desc->pd_prov_id = KCF_PROVID_INVALID;
	desc->pd_state = KCF_PROV_ALLOCATED;

	mutex_init(&desc->pd_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&desc->pd_resume_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&desc->pd_remove_cv, NULL, CV_DEFAULT, NULL);

	return (desc);
}

/*
 * Called by KCF_PROV_REFRELE when a provider's reference count drops
 * to zero. We free the descriptor when the last reference is released.
 * However, for software providers, we do not free it when there is an
 * unregister thread waiting. We signal that thread in this case and
 * that thread is responsible for freeing the descriptor.
 */
void
kcf_provider_zero_refcnt(kcf_provider_desc_t *desc)
{
	mutex_enter(&desc->pd_lock);
	switch (desc->pd_prov_type) {
	case CRYPTO_SW_PROVIDER:
		if (desc->pd_state == KCF_PROV_REMOVED ||
		    desc->pd_state == KCF_PROV_DISABLED) {
			desc->pd_state = KCF_PROV_FREED;
			cv_broadcast(&desc->pd_remove_cv);
			mutex_exit(&desc->pd_lock);
			break;
		}
		/* FALLTHRU */

	case CRYPTO_HW_PROVIDER:
	case CRYPTO_LOGICAL_PROVIDER:
		mutex_exit(&desc->pd_lock);
		kcf_free_provider_desc(desc);
	}
}

/*
 * Free a provider descriptor.
 */
void
kcf_free_provider_desc(kcf_provider_desc_t *desc)
{
	if (desc == NULL)
		return;

	mutex_enter(&prov_tab_mutex);
	if (desc->pd_prov_id != KCF_PROVID_INVALID) {
		/* release the associated providers table entry */
		ASSERT(prov_tab[desc->pd_prov_id] != NULL);
		prov_tab[desc->pd_prov_id] = NULL;
		prov_tab_num--;
	}
	mutex_exit(&prov_tab_mutex);

	/* free the kernel memory associated with the provider descriptor */

	if (desc->pd_description != NULL)
		kmem_free(desc->pd_description,
		    CRYPTO_PROVIDER_DESCR_MAX_LEN + 1);

	if (desc->pd_ops_vector != NULL) {

		if (desc->pd_ops_vector->co_control_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_control_ops,
			    sizeof (crypto_control_ops_t));

		if (desc->pd_ops_vector->co_digest_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_digest_ops,
			    sizeof (crypto_digest_ops_t));

		if (desc->pd_ops_vector->co_cipher_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_cipher_ops,
			    sizeof (crypto_cipher_ops_t));

		if (desc->pd_ops_vector->co_mac_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_mac_ops,
			    sizeof (crypto_mac_ops_t));

		if (desc->pd_ops_vector->co_sign_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_sign_ops,
			    sizeof (crypto_sign_ops_t));

		if (desc->pd_ops_vector->co_verify_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_verify_ops,
			    sizeof (crypto_verify_ops_t));

		if (desc->pd_ops_vector->co_dual_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_dual_ops,
			    sizeof (crypto_dual_ops_t));

		if (desc->pd_ops_vector->co_dual_cipher_mac_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_dual_cipher_mac_ops,
			    sizeof (crypto_dual_cipher_mac_ops_t));

		if (desc->pd_ops_vector->co_random_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_random_ops,
			    sizeof (crypto_random_number_ops_t));

		if (desc->pd_ops_vector->co_session_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_session_ops,
			    sizeof (crypto_session_ops_t));

		if (desc->pd_ops_vector->co_object_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_object_ops,
			    sizeof (crypto_object_ops_t));

		if (desc->pd_ops_vector->co_key_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_key_ops,
			    sizeof (crypto_key_ops_t));

		if (desc->pd_ops_vector->co_provider_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_provider_ops,
			    sizeof (crypto_provider_management_ops_t));

		if (desc->pd_ops_vector->co_ctx_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_ctx_ops,
			    sizeof (crypto_ctx_ops_t));

		if (desc->pd_ops_vector->co_mech_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_mech_ops,
			    sizeof (crypto_mech_ops_t));

		if (desc->pd_ops_vector->co_nostore_key_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_nostore_key_ops,
			    sizeof (crypto_nostore_key_ops_t));

		kmem_free(desc->pd_ops_vector, sizeof (crypto_ops_t));
	}

	if (desc->pd_mechanisms != NULL)
		/* free the memory associated with the mechanism info's */
		kmem_free(desc->pd_mechanisms, sizeof (crypto_mech_info_t) *
		    desc->pd_mech_list_count);

	if (desc->pd_sched_info.ks_taskq != NULL)
		taskq_destroy(desc->pd_sched_info.ks_taskq);

	mutex_destroy(&desc->pd_lock);
	cv_destroy(&desc->pd_resume_cv);
	cv_destroy(&desc->pd_remove_cv);

	kmem_free(desc, sizeof (kcf_provider_desc_t));
}

/*
 * Returns an array of hardware and logical provider descriptors,
 * a.k.a the PKCS#11 slot list. A REFHOLD is done on each descriptor
 * before the array is returned. The entire table can be freed by
 * calling kcf_free_provider_tab().
 */
int
kcf_get_slot_list(uint_t *count, kcf_provider_desc_t ***array,
    boolean_t unverified)
{
	kcf_provider_desc_t *prov_desc;
	kcf_provider_desc_t **p = NULL;
	char *last;
	uint_t cnt = 0;
	uint_t i, j;
	int rval = CRYPTO_SUCCESS;
	size_t n, final_size;

	/* count the providers */
	mutex_enter(&prov_tab_mutex);
	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    ((prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    (prov_desc->pd_flags & CRYPTO_HIDE_PROVIDER) == 0) ||
		    prov_desc->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)) {
			if (KCF_IS_PROV_USABLE(prov_desc) ||
			    (unverified && KCF_IS_PROV_UNVERIFIED(prov_desc))) {
				cnt++;
			}
		}
	}
	mutex_exit(&prov_tab_mutex);

	if (cnt == 0)
		goto out;

	n = cnt * sizeof (kcf_provider_desc_t *);
again:
	p = kmem_zalloc(n, KM_SLEEP);

	/* pointer to last entry in the array */
	last = (char *)&p[cnt-1];

	mutex_enter(&prov_tab_mutex);
	/* fill the slot list */
	for (i = 0, j = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    ((prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    (prov_desc->pd_flags & CRYPTO_HIDE_PROVIDER) == 0) ||
		    prov_desc->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)) {
			if (KCF_IS_PROV_USABLE(prov_desc) ||
			    (unverified && KCF_IS_PROV_UNVERIFIED(prov_desc))) {
				if ((char *)&p[j] > last) {
					mutex_exit(&prov_tab_mutex);
					kcf_free_provider_tab(cnt, p);
					n = n << 1;
					cnt = cnt << 1;
					goto again;
				}
				p[j++] = prov_desc;
				KCF_PROV_REFHOLD(prov_desc);
			}
		}
	}
	mutex_exit(&prov_tab_mutex);

	final_size = j * sizeof (kcf_provider_desc_t *);
	cnt = j;
	ASSERT(final_size <= n);

	/* check if buffer we allocated is too large */
	if (final_size < n) {
		char *final_buffer = NULL;

		if (final_size > 0) {
			final_buffer = kmem_alloc(final_size, KM_SLEEP);
			bcopy(p, final_buffer, final_size);
		}
		kmem_free(p, n);
		p = (kcf_provider_desc_t **)final_buffer;
	}
out:
	*count = cnt;
	*array = p;
	return (rval);
}

/*
 * Free an array of hardware provider descriptors.  A REFRELE
 * is done on each descriptor before the table is freed.
 */
void
kcf_free_provider_tab(uint_t count, kcf_provider_desc_t **array)
{
	kcf_provider_desc_t *prov_desc;
	int i;

	for (i = 0; i < count; i++) {
		if ((prov_desc = array[i]) != NULL) {
			KCF_PROV_REFRELE(prov_desc);
		}
	}
	kmem_free(array, count * sizeof (kcf_provider_desc_t *));
}

/*
 * Returns in the location pointed to by pd a pointer to the descriptor
 * for the software provider for the specified mechanism.
 * The provider descriptor is returned held and it is the caller's
 * responsibility to release it when done. The mechanism entry
 * is returned if the optional argument mep is non NULL.
 *
 * Returns one of the CRYPTO_ * error codes on failure, and
 * CRYPTO_SUCCESS on success.
 */
int
kcf_get_sw_prov(crypto_mech_type_t mech_type, kcf_provider_desc_t **pd,
    kcf_mech_entry_t **mep, boolean_t log_warn)
{
	kcf_mech_entry_t *me;

	/* get the mechanism entry for this mechanism */
	if (kcf_get_mech_entry(mech_type, &me) != KCF_SUCCESS)
		return (CRYPTO_MECHANISM_INVALID);

	/*
	 * Get the software provider for this mechanism.
	 * Lock the mech_entry until we grab the 'pd'.
	 */
	mutex_enter(&me->me_mutex);

	if (me->me_sw_prov == NULL ||
	    (*pd = me->me_sw_prov->pm_prov_desc) == NULL) {
		/* no SW provider for this mechanism */
		if (log_warn)
			cmn_err(CE_WARN, "no SW provider for \"%s\"\n",
			    me->me_name);
		mutex_exit(&me->me_mutex);
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	KCF_PROV_REFHOLD(*pd);
	mutex_exit(&me->me_mutex);

	if (mep != NULL)
		*mep = me;

	return (CRYPTO_SUCCESS);
}
