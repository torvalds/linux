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
#define	CRYPTO_DIGEST_OFFSET(f)		offsetof(crypto_digest_ops_t, f)

/*
 * Message digest routines
 */

/*
 * The following are the possible returned values common to all the routines
 * below. The applicability of some of these return values depends on the
 * presence of the arguments.
 *
 *	CRYPTO_SUCCESS:	The operation completed successfully.
 *	CRYPTO_QUEUED:	A request was submitted successfully. The callback
 *			routine will be called when the operation is done.
 *	CRYPTO_MECHANISM_INVALID or CRYPTO_INVALID_MECH_PARAM
 *			for problems with the 'mech'.
 *	CRYPTO_INVALID_DATA for bogus 'data'
 *	CRYPTO_HOST_MEMORY for failure to allocate memory to handle this work.
 *	CRYPTO_INVALID_CONTEXT: Not a valid context.
 *	CRYPTO_BUSY:	Cannot process the request now. Schedule a
 *			crypto_bufcall(), or try later.
 *	CRYPTO_NOT_SUPPORTED and CRYPTO_MECH_NOT_SUPPORTED:
 *			No provider is capable of a function or a mechanism.
 */


/*
 * crypto_digest_prov()
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
 *	data:	The message to be digested.
 *	digest:	Storage for the digest. The length needed depends on the
 *		mechanism.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs the
 *	digesting operation of 'data' on the specified
 *	provider with the specified session.
 *	When complete and successful, 'digest' will contain the digest value.
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
crypto_digest_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_data_t *data, crypto_data_t *digest,
    crypto_call_req_t *crq)
{
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;
	int rv;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		rv = kcf_get_hardware_provider(mech->cm_type,
		    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq),
		    pd, &real_provider, CRYPTO_FG_DIGEST_ATOMIC);

		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}
	KCF_WRAP_DIGEST_OPS_PARAMS(&params, KCF_OP_ATOMIC, sid, mech, NULL,
	    data, digest);

	/* no crypto context to carry between multiple parts. */
	rv = kcf_submit_request(real_provider, NULL, crq, &params, B_FALSE);
	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

	return (rv);
}


/*
 * Same as crypto_digest_prov(), but relies on the KCF scheduler to
 * choose a provider. See crypto_digest_prov() comments for more information.
 */
int
crypto_digest(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_data_t *digest, crypto_call_req_t *crq)
{
	int error;
	kcf_provider_desc_t *pd;
	kcf_req_params_t params;
	kcf_prov_tried_t *list = NULL;

retry:
	/* The pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, NULL, &error, list,
	    CRYPTO_FG_DIGEST_ATOMIC, CHECK_RESTRICT(crq),
	    data->cd_length)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(crq, pd)) {
		crypto_mechanism_t lmech;

		lmech = *mech;
		KCF_SET_PROVIDER_MECHNUM(mech->cm_type, pd, &lmech);
		error = KCF_PROV_DIGEST_ATOMIC(pd, pd->pd_sid, &lmech, data,
		    digest, KCF_SWFP_RHNDL(crq));
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		if (pd->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    (pd->pd_flags & CRYPTO_HASH_NO_UPDATE) &&
		    (data->cd_length > pd->pd_hash_limit)) {
			error = CRYPTO_BUFFER_TOO_BIG;
		} else {
			KCF_WRAP_DIGEST_OPS_PARAMS(&params, KCF_OP_ATOMIC,
			    pd->pd_sid, mech, NULL, data, digest);

			/* no crypto context to carry between multiple parts. */
			error = kcf_submit_request(pd, NULL, crq, &params,
			    B_FALSE);
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
 * crypto_digest_init_prov()
 *
 *	pd:	pointer to the descriptor of the provider to use for this
 *		operation.
 *	sid:	provider session id.
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	ctxp:	Pointer to a crypto_context_t.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs the
 *	initialization of a message digest operation on the specified
 *	provider with the specified session.
 *	When complete and successful, 'ctxp' will contain a crypto_context_t
 *	valid for later calls to digest_update() and digest_final().
 *	The caller should hold a reference on the specified provider
 *	descriptor before calling this function.
 */
int
crypto_digest_init_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_context_t *ctxp, crypto_call_req_t  *crq)
{
	int error;
	crypto_ctx_t *ctx;
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		error = kcf_get_hardware_provider(mech->cm_type,
		    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
		    &real_provider, CRYPTO_FG_DIGEST);

		if (error != CRYPTO_SUCCESS)
			return (error);
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
		error = KCF_PROV_DIGEST_INIT(real_provider, ctx, &lmech,
		    KCF_SWFP_RHNDL(crq));
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DIGEST_OPS_PARAMS(&params, KCF_OP_INIT, sid,
		    mech, NULL, NULL, NULL);
		error = kcf_submit_request(real_provider, ctx, crq, &params,
		    B_FALSE);
	}

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

	if ((error == CRYPTO_SUCCESS) || (error == CRYPTO_QUEUED))
		*ctxp = (crypto_context_t)ctx;
	else {
		/* Release the hold done in kcf_new_ctx(). */
		KCF_CONTEXT_REFRELE((kcf_context_t *)ctx->cc_framework_private);
	}

	return (error);
}

/*
 * Same as crypto_digest_init_prov(), but relies on the KCF scheduler
 * to choose a provider. See crypto_digest_init_prov() comments for
 * more information.
 */
int
crypto_digest_init(crypto_mechanism_t *mech, crypto_context_t *ctxp,
    crypto_call_req_t  *crq)
{
	int error;
	kcf_provider_desc_t *pd;
	kcf_prov_tried_t *list = NULL;

retry:
	/* The pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, NULL, &error,
	    list, CRYPTO_FG_DIGEST, CHECK_RESTRICT(crq), 0)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	if (pd->pd_prov_type == CRYPTO_HW_PROVIDER &&
	    (pd->pd_flags & CRYPTO_HASH_NO_UPDATE)) {
		/*
		 * The hardware provider has limited digest support.
		 * So, we fallback early here to using a software provider.
		 *
		 * XXX - need to enhance to do the fallback later in
		 * crypto_digest_update() if the size of accumulated input data
		 * exceeds the maximum size digestable by hardware provider.
		 */
		error = CRYPTO_BUFFER_TOO_BIG;
	} else {
		error = crypto_digest_init_prov(pd, pd->pd_sid,
		    mech, ctxp, crq);
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
 * crypto_digest_update()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by digest_init().
 *	data:	The part of message to be digested.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	part of a message digest operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_digest_update(crypto_context_t context, crypto_data_t *data,
    crypto_call_req_t *cr)
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

	ASSERT(pd->pd_prov_type != CRYPTO_LOGICAL_PROVIDER);

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(cr, pd)) {
		error = KCF_PROV_DIGEST_UPDATE(pd, ctx, data, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DIGEST_OPS_PARAMS(&params, KCF_OP_UPDATE,
		    ctx->cc_session, NULL, NULL, data, NULL);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	return (error);
}

/*
 * crypto_digest_final()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by digest_init().
 *	digest:	The storage for the digest.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs the
 *	final part of a message digest operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_digest_final(crypto_context_t context, crypto_data_t *digest,
    crypto_call_req_t *cr)
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

	ASSERT(pd->pd_prov_type != CRYPTO_LOGICAL_PROVIDER);

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(cr, pd)) {
		error = KCF_PROV_DIGEST_FINAL(pd, ctx, digest, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DIGEST_OPS_PARAMS(&params, KCF_OP_FINAL,
		    ctx->cc_session, NULL, NULL, NULL, digest);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(error, kcf_ctx);
	return (error);
}

/*
 * Performs a digest update on the specified key. Note that there is
 * no k-API crypto_digest_key() equivalent of this function.
 */
int
crypto_digest_key_prov(crypto_context_t context, crypto_key_t *key,
    crypto_call_req_t *cr)
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

	ASSERT(pd->pd_prov_type != CRYPTO_LOGICAL_PROVIDER);

	/* The fast path for SW providers. */
	if (CHECK_FASTPATH(cr, pd)) {
		error = KCF_PROV_DIGEST_KEY(pd, ctx, key, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DIGEST_OPS_PARAMS(&params, KCF_OP_DIGEST_KEY,
		    ctx->cc_session, NULL, key, NULL, NULL);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	return (error);
}

/*
 * See comments for crypto_digest_update() and crypto_digest_final().
 */
int
crypto_digest_single(crypto_context_t context, crypto_data_t *data,
    crypto_data_t *digest, crypto_call_req_t *cr)
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
		error = KCF_PROV_DIGEST(pd, ctx, data, digest, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DIGEST_OPS_PARAMS(&params, KCF_OP_SINGLE, pd->pd_sid,
		    NULL, NULL, data, digest);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(error, kcf_ctx);
	return (error);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(crypto_digest_prov);
EXPORT_SYMBOL(crypto_digest);
EXPORT_SYMBOL(crypto_digest_init_prov);
EXPORT_SYMBOL(crypto_digest_init);
EXPORT_SYMBOL(crypto_digest_update);
EXPORT_SYMBOL(crypto_digest_final);
EXPORT_SYMBOL(crypto_digest_key_prov);
EXPORT_SYMBOL(crypto_digest_single);
#endif
