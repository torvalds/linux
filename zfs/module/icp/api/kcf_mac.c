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

#define	CRYPTO_OPS_OFFSET(f)		offsetof(crypto_ops_t, co_##f)
#define	CRYPTO_MAC_OFFSET(f)		offsetof(crypto_mac_ops_t, f)

/*
 * Message authentication codes routines.
 */

/*
 * The following are the possible returned values common to all the routines
 * below. The applicability of some of these return values depends on the
 * presence of the arguments.
 *
 *	CRYPTO_SUCCESS:	The operation completed successfully.
 *	CRYPTO_QUEUED:	A request was submitted successfully. The callback
 *			routine will be called when the operation is done.
 *	CRYPTO_INVALID_MECH_NUMBER, CRYPTO_INVALID_MECH_PARAM, or
 *	CRYPTO_INVALID_MECH for problems with the 'mech'.
 *	CRYPTO_INVALID_DATA for bogus 'data'
 *	CRYPTO_HOST_MEMORY for failure to allocate memory to handle this work.
 *	CRYPTO_INVALID_CONTEXT: Not a valid context.
 *	CRYPTO_BUSY:	Cannot process the request now. Schedule a
 *			crypto_bufcall(), or try later.
 *	CRYPTO_NOT_SUPPORTED and CRYPTO_MECH_NOT_SUPPORTED: No provider is
 *			capable of a function or a mechanism.
 *	CRYPTO_INVALID_KEY: bogus 'key' argument.
 *	CRYPTO_INVALID_MAC: bogus 'mac' argument.
 */

/*
 * crypto_mac_prov()
 *
 * Arguments:
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	data:	The message to compute the MAC for.
 *	mac: Storage for the MAC. The length needed depends on the mechanism.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of a
 *		MAC with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	single-part message authentication of 'data' with the mechanism
 *	'mech', using *	the key 'key', on the specified provider with
 *	the specified session id.
 *	When complete and successful, 'mac' will contain the message
 *	authentication code.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'crq'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_mac_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_data_t *data, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_data_t *mac, crypto_call_req_t *crq)
{
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;
	int rv;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		rv = kcf_get_hardware_provider(mech->cm_type,
		    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
		    &real_provider, CRYPTO_FG_MAC_ATOMIC);

		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}

	KCF_WRAP_MAC_OPS_PARAMS(&params, KCF_OP_ATOMIC, sid, mech, key,
	    data, mac, tmpl);
	rv = kcf_submit_request(real_provider, NULL, crq, &params, B_FALSE);
	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

	return (rv);
}

/*
 * Same as crypto_mac_prov(), but relies on the KCF scheduler to choose
 * a provider. See crypto_mac() comments for more information.
 */
int
crypto_mac(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *mac,
    crypto_call_req_t *crq)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_req_params_t params;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* The pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_MAC_ATOMIC, CHECK_RESTRICT(crq),
	    data->cd_length)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	/*
	 * For SW providers, check the validity of the context template
	 * It is very rare that the generation number mis-matches, so
	 * is acceptable to fail here, and let the consumer recover by
	 * freeing this tmpl and create a new one for the key and new SW
	 * provider
	 */
	if ((pd->pd_prov_type == CRYPTO_SW_PROVIDER) &&
	    ((ctx_tmpl = (kcf_ctx_template_t *)tmpl) != NULL)) {
		if (ctx_tmpl->ct_generation != me->me_gen_swprov) {
			if (list != NULL)
				kcf_free_triedlist(list);
			KCF_PROV_REFRELE(pd);
			return (CRYPTO_OLD_CTX_TEMPLATE);
		} else {
			spi_ctx_tmpl = ctx_tmpl->ct_prov_tmpl;
		}
	}

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(crq, pd)) {
		crypto_mechanism_t lmech;

		lmech = *mech;
		KCF_SET_PROVIDER_MECHNUM(mech->cm_type, pd, &lmech);

		error = KCF_PROV_MAC_ATOMIC(pd, pd->pd_sid, &lmech, key, data,
		    mac, spi_ctx_tmpl, KCF_SWFP_RHNDL(crq));
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		if (pd->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    (pd->pd_flags & CRYPTO_HASH_NO_UPDATE) &&
		    (data->cd_length > pd->pd_hash_limit)) {
			/*
			 * XXX - We need a check to see if this is indeed
			 * a HMAC. So far, all kernel clients use
			 * this interface only for HMAC. So, this is fine
			 * for now.
			 */
			error = CRYPTO_BUFFER_TOO_BIG;
		} else {
			KCF_WRAP_MAC_OPS_PARAMS(&params, KCF_OP_ATOMIC,
			    pd->pd_sid, mech, key, data, mac, spi_ctx_tmpl);

			error = kcf_submit_request(pd, NULL, crq, &params,
			    KCF_ISDUALREQ(crq));
		}
	}

	if (error != CRYPTO_SUCCESS && error != CRYPTO_QUEUED &&
	    IS_RECOVERABLE(error)) {
		/* Add pd to the linked list of providers tried. */
		if (kcf_insert_triedlist(&list, pd, KCF_KMFLAG(crq)) != NULL)
			goto retry;
	}

	if (list != NULL)
		kcf_free_triedlist(list);

	KCF_PROV_REFRELE(pd);
	return (error);
}

/*
 * Single part operation to compute the MAC corresponding to the specified
 * 'data' and to verify that it matches the MAC specified by 'mac'.
 * The other arguments are the same as the function crypto_mac_prov().
 */
int
crypto_mac_verify_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_data_t *data, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_data_t *mac, crypto_call_req_t *crq)
{
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;
	int rv;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		rv = kcf_get_hardware_provider(mech->cm_type,
		    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
		    &real_provider, CRYPTO_FG_MAC_ATOMIC);

		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}

	KCF_WRAP_MAC_OPS_PARAMS(&params, KCF_OP_MAC_VERIFY_ATOMIC, sid, mech,
	    key, data, mac, tmpl);
	rv = kcf_submit_request(real_provider, NULL, crq, &params, B_FALSE);
	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

	return (rv);
}

/*
 * Same as crypto_mac_verify_prov(), but relies on the KCF scheduler to choose
 * a provider. See crypto_mac_verify_prov() comments for more information.
 */
int
crypto_mac_verify(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *mac,
    crypto_call_req_t *crq)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_req_params_t params;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* The pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_MAC_ATOMIC, CHECK_RESTRICT(crq),
	    data->cd_length)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	/*
	 * For SW providers, check the validity of the context template
	 * It is very rare that the generation number mis-matches, so
	 * is acceptable to fail here, and let the consumer recover by
	 * freeing this tmpl and create a new one for the key and new SW
	 * provider
	 */
	if ((pd->pd_prov_type == CRYPTO_SW_PROVIDER) &&
	    ((ctx_tmpl = (kcf_ctx_template_t *)tmpl) != NULL)) {
		if (ctx_tmpl->ct_generation != me->me_gen_swprov) {
			if (list != NULL)
				kcf_free_triedlist(list);
			KCF_PROV_REFRELE(pd);
			return (CRYPTO_OLD_CTX_TEMPLATE);
		} else {
			spi_ctx_tmpl = ctx_tmpl->ct_prov_tmpl;
		}
	}

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(crq, pd)) {
		crypto_mechanism_t lmech;

		lmech = *mech;
		KCF_SET_PROVIDER_MECHNUM(mech->cm_type, pd, &lmech);

		error = KCF_PROV_MAC_VERIFY_ATOMIC(pd, pd->pd_sid, &lmech, key,
		    data, mac, spi_ctx_tmpl, KCF_SWFP_RHNDL(crq));
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		if (pd->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    (pd->pd_flags & CRYPTO_HASH_NO_UPDATE) &&
		    (data->cd_length > pd->pd_hash_limit)) {
			/* see comments in crypto_mac() */
			error = CRYPTO_BUFFER_TOO_BIG;
		} else {
			KCF_WRAP_MAC_OPS_PARAMS(&params,
			    KCF_OP_MAC_VERIFY_ATOMIC, pd->pd_sid, mech,
			    key, data, mac, spi_ctx_tmpl);

			error = kcf_submit_request(pd, NULL, crq, &params,
			    KCF_ISDUALREQ(crq));
		}
	}

	if (error != CRYPTO_SUCCESS && error != CRYPTO_QUEUED &&
	    IS_RECOVERABLE(error)) {
		/* Add pd to the linked list of providers tried. */
		if (kcf_insert_triedlist(&list, pd, KCF_KMFLAG(crq)) != NULL)
			goto retry;
	}

	if (list != NULL)
		kcf_free_triedlist(list);

	KCF_PROV_REFRELE(pd);
	return (error);
}

/*
 * crypto_mac_init_prov()
 *
 * Arguments:
 *	pd:	pointer to the descriptor of the provider to use for this
 *		operation.
 *	sid:	provider session id.
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of a
 *		MAC with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *	ctxp:	Pointer to a crypto_context_t.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs the
 *	initialization of a MAC operation on the specified provider with
 *	the specified session.
 *	When possible and applicable, will internally use the pre-computed MAC
 *	context from the context template, tmpl.
 *	When complete and successful, 'ctxp' will contain a crypto_context_t
 *	valid for later calls to mac_update() and mac_final().
 *	The caller should hold a reference on the specified provider
 *	descriptor before calling this function.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_mac_init_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_key_t *key, crypto_spi_ctx_template_t tmpl,
    crypto_context_t *ctxp, crypto_call_req_t *crq)
{
	int rv;
	crypto_ctx_t *ctx;
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		rv = kcf_get_hardware_provider(mech->cm_type,
		    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
		    &real_provider, CRYPTO_FG_MAC);

		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}

	/* Allocate and initialize the canonical context */
	if ((ctx = kcf_new_ctx(crq, real_provider, sid)) == NULL) {
		if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
			KCF_PROV_REFRELE(real_provider);
		return (CRYPTO_HOST_MEMORY);
	}

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(crq, pd)) {
		crypto_mechanism_t lmech;

		lmech = *mech;
		KCF_SET_PROVIDER_MECHNUM(mech->cm_type, real_provider, &lmech);
		rv = KCF_PROV_MAC_INIT(real_provider, ctx, &lmech, key, tmpl,
		    KCF_SWFP_RHNDL(crq));
		KCF_PROV_INCRSTATS(pd, rv);
	} else {
		KCF_WRAP_MAC_OPS_PARAMS(&params, KCF_OP_INIT, sid, mech, key,
		    NULL, NULL, tmpl);
		rv = kcf_submit_request(real_provider, ctx, crq, &params,
		    B_FALSE);
	}

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

	if ((rv == CRYPTO_SUCCESS) || (rv == CRYPTO_QUEUED))
		*ctxp = (crypto_context_t)ctx;
	else {
		/* Release the hold done in kcf_new_ctx(). */
		KCF_CONTEXT_REFRELE((kcf_context_t *)ctx->cc_framework_private);
	}

	return (rv);
}

/*
 * Same as crypto_mac_init_prov(), but relies on the KCF scheduler to
 * choose a provider. See crypto_mac_init_prov() comments for more
 * information.
 */
int
crypto_mac_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t  *crq)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* The pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_MAC, CHECK_RESTRICT(crq), 0)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	/*
	 * For SW providers, check the validity of the context template
	 * It is very rare that the generation number mis-matches, so
	 * is acceptable to fail here, and let the consumer recover by
	 * freeing this tmpl and create a new one for the key and new SW
	 * provider
	 */

	if ((pd->pd_prov_type == CRYPTO_SW_PROVIDER) &&
	    ((ctx_tmpl = (kcf_ctx_template_t *)tmpl) != NULL)) {
		if (ctx_tmpl->ct_generation != me->me_gen_swprov) {
			if (list != NULL)
				kcf_free_triedlist(list);
			KCF_PROV_REFRELE(pd);
			return (CRYPTO_OLD_CTX_TEMPLATE);
		} else {
			spi_ctx_tmpl = ctx_tmpl->ct_prov_tmpl;
		}
	}

	if (pd->pd_prov_type == CRYPTO_HW_PROVIDER &&
	    (pd->pd_flags & CRYPTO_HASH_NO_UPDATE)) {
		/*
		 * The hardware provider has limited HMAC support.
		 * So, we fallback early here to using a software provider.
		 *
		 * XXX - need to enhance to do the fallback later in
		 * crypto_mac_update() if the size of accumulated input data
		 * exceeds the maximum size digestable by hardware provider.
		 */
		error = CRYPTO_BUFFER_TOO_BIG;
	} else {
		error = crypto_mac_init_prov(pd, pd->pd_sid, mech, key,
		    spi_ctx_tmpl, ctxp, crq);
	}
	if (error != CRYPTO_SUCCESS && error != CRYPTO_QUEUED &&
	    IS_RECOVERABLE(error)) {
		/* Add pd to the linked list of providers tried. */
		if (kcf_insert_triedlist(&list, pd, KCF_KMFLAG(crq)) != NULL)
			goto retry;
	}

	if (list != NULL)
		kcf_free_triedlist(list);

	KCF_PROV_REFRELE(pd);
	return (error);
}

/*
 * crypto_mac_update()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by mac_init().
 *	data: The message part to be MAC'ed
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	part of a MAC operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_mac_update(crypto_context_t context, crypto_data_t *data,
    crypto_call_req_t *cr)
{
	crypto_ctx_t *ctx = (crypto_ctx_t *)context;
	kcf_context_t *kcf_ctx;
	kcf_provider_desc_t *pd;
	kcf_req_params_t params;
	int rv;

	if ((ctx == NULL) ||
	    ((kcf_ctx = (kcf_context_t *)ctx->cc_framework_private) == NULL) ||
	    ((pd = kcf_ctx->kc_prov_desc) == NULL)) {
		return (CRYPTO_INVALID_CONTEXT);
	}

	ASSERT(pd->pd_prov_type != CRYPTO_LOGICAL_PROVIDER);

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(cr, pd)) {
		rv = KCF_PROV_MAC_UPDATE(pd, ctx, data, NULL);
		KCF_PROV_INCRSTATS(pd, rv);
	} else {
		KCF_WRAP_MAC_OPS_PARAMS(&params, KCF_OP_UPDATE,
		    ctx->cc_session, NULL, NULL, data, NULL, NULL);
		rv = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	return (rv);
}

/*
 * crypto_mac_final()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by mac_init().
 *	mac: Storage for the message authentication code.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	part of a message authentication operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_mac_final(crypto_context_t context, crypto_data_t *mac,
    crypto_call_req_t *cr)
{
	crypto_ctx_t *ctx = (crypto_ctx_t *)context;
	kcf_context_t *kcf_ctx;
	kcf_provider_desc_t *pd;
	kcf_req_params_t params;
	int rv;

	if ((ctx == NULL) ||
	    ((kcf_ctx = (kcf_context_t *)ctx->cc_framework_private) == NULL) ||
	    ((pd = kcf_ctx->kc_prov_desc) == NULL)) {
		return (CRYPTO_INVALID_CONTEXT);
	}

	ASSERT(pd->pd_prov_type != CRYPTO_LOGICAL_PROVIDER);

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(cr, pd)) {
		rv = KCF_PROV_MAC_FINAL(pd, ctx, mac, NULL);
		KCF_PROV_INCRSTATS(pd, rv);
	} else {
		KCF_WRAP_MAC_OPS_PARAMS(&params, KCF_OP_FINAL,
		    ctx->cc_session, NULL, NULL, NULL, mac, NULL);
		rv = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(rv, kcf_ctx);
	return (rv);
}

/*
 * See comments for crypto_mac_update() and crypto_mac_final().
 */
int
crypto_mac_single(crypto_context_t context, crypto_data_t *data,
    crypto_data_t *mac, crypto_call_req_t *cr)
{
	crypto_ctx_t *ctx = (crypto_ctx_t *)context;
	kcf_context_t *kcf_ctx;
	kcf_provider_desc_t *pd;
	int error;
	kcf_req_params_t params;


	if ((ctx == NULL) ||
	    ((kcf_ctx = (kcf_context_t *)ctx->cc_framework_private) == NULL) ||
	    ((pd = kcf_ctx->kc_prov_desc) == NULL)) {
		return (CRYPTO_INVALID_CONTEXT);
	}


	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(cr, pd)) {
		error = KCF_PROV_MAC(pd, ctx, data, mac, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_MAC_OPS_PARAMS(&params, KCF_OP_SINGLE, pd->pd_sid,
		    NULL, NULL, data, mac, NULL);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(error, kcf_ctx);
	return (error);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(crypto_mac_prov);
EXPORT_SYMBOL(crypto_mac);
EXPORT_SYMBOL(crypto_mac_verify_prov);
EXPORT_SYMBOL(crypto_mac_verify);
EXPORT_SYMBOL(crypto_mac_init_prov);
EXPORT_SYMBOL(crypto_mac_init);
EXPORT_SYMBOL(crypto_mac_update);
EXPORT_SYMBOL(crypto_mac_final);
EXPORT_SYMBOL(crypto_mac_single);
#endif
