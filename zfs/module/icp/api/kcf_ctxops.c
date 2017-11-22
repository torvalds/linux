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

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/sched_impl.h>

/*
 * Crypto contexts manipulation routines
 */

/*
 * crypto_create_ctx_template()
 *
 * Arguments:
 *
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	ptmpl:	a storage for the opaque crypto_ctx_template_t, allocated and
 *		initialized by the software provider this routine is
 *		dispatched to.
 *	kmflag:	KM_SLEEP/KM_NOSLEEP mem. alloc. flag.
 *
 * Description:
 *	Redirects the call to the software provider of the specified
 *	mechanism. That provider will allocate and pre-compute/pre-expand
 *	the context template, reusable by later calls to crypto_xxx_init().
 *	The size and address of that provider context template are stored
 *	in an internal structure, kcf_ctx_template_t. The address of that
 *	structure is given back to the caller in *ptmpl.
 *
 * Context:
 *	Process or interrupt.
 *
 * Returns:
 *	CRYPTO_SUCCESS when the context template is successfully created.
 *	CRYPTO_HOST_MEMEORY: mem alloc failure
 *	CRYPTO_ARGUMENTS_BAD: NULL storage for the ctx template.
 *	RYPTO_MECHANISM_INVALID: invalid mechanism 'mech'.
 */
int
crypto_create_ctx_template(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t *ptmpl, int kmflag)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_mechanism_t prov_mech;

	/* A few args validation */

	if (ptmpl == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if (mech == NULL)
		return (CRYPTO_MECHANISM_INVALID);

	error = kcf_get_sw_prov(mech->cm_type, &pd, &me, B_TRUE);
	if (error != CRYPTO_SUCCESS)
		return (error);

	if ((ctx_tmpl = (kcf_ctx_template_t *)kmem_alloc(
	    sizeof (kcf_ctx_template_t), kmflag)) == NULL) {
		KCF_PROV_REFRELE(pd);
		return (CRYPTO_HOST_MEMORY);
	}

	/* Pass a mechtype that the provider understands */
	prov_mech.cm_type = KCF_TO_PROV_MECHNUM(pd, mech->cm_type);
	prov_mech.cm_param = mech->cm_param;
	prov_mech.cm_param_len = mech->cm_param_len;

	error = KCF_PROV_CREATE_CTX_TEMPLATE(pd, &prov_mech, key,
	    &(ctx_tmpl->ct_prov_tmpl), &(ctx_tmpl->ct_size), KCF_RHNDL(kmflag));

	if (error == CRYPTO_SUCCESS) {
		ctx_tmpl->ct_generation = me->me_gen_swprov;
		*ptmpl = ctx_tmpl;
	} else {
		kmem_free(ctx_tmpl, sizeof (kcf_ctx_template_t));
	}
	KCF_PROV_REFRELE(pd);

	return (error);
}

/*
 * crypto_destroy_ctx_template()
 *
 * Arguments:
 *
 *	tmpl:	an opaque crypto_ctx_template_t previously created by
 *		crypto_create_ctx_template()
 *
 * Description:
 *	Frees the inbedded crypto_spi_ctx_template_t, then the
 *	kcf_ctx_template_t.
 *
 * Context:
 *	Process or interrupt.
 *
 */
void
crypto_destroy_ctx_template(crypto_ctx_template_t tmpl)
{
	kcf_ctx_template_t *ctx_tmpl = (kcf_ctx_template_t *)tmpl;

	if (ctx_tmpl == NULL)
		return;

	ASSERT(ctx_tmpl->ct_prov_tmpl != NULL);

	bzero(ctx_tmpl->ct_prov_tmpl, ctx_tmpl->ct_size);
	kmem_free(ctx_tmpl->ct_prov_tmpl, ctx_tmpl->ct_size);
	kmem_free(ctx_tmpl, sizeof (kcf_ctx_template_t));
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(crypto_create_ctx_template);
EXPORT_SYMBOL(crypto_destroy_ctx_template);
#endif
