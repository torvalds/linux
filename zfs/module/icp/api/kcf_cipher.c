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
#define	CRYPTO_CIPHER_OFFSET(f)		offsetof(crypto_cipher_ops_t, f)

/*
 * Encryption and decryption routines.
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
 *	CRYPTO_INVALID_PLAINTEXT: bogus 'plaintext' argument.
 *	CRYPTO_INVALID_CIPHERTEXT: bogus 'ciphertext' argument.
 */

/*
 * crypto_cipher_init_prov()
 *
 * Arguments:
 *
 *	pd:	provider descriptor
 *	sid:	session id
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of an
 *		encryption  or decryption with the 'mech' using 'key'.
 *		'tmpl' is created by a previous call to
 *		crypto_create_ctx_template().
 *	ctxp:	Pointer to a crypto_context_t.
 *	func:	CRYPTO_FG_ENCRYPT or CRYPTO_FG_DECRYPT.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	This is a common function invoked internally by both
 *	crypto_encrypt_init() and crypto_decrypt_init().
 *	Asynchronously submits a request for, or synchronously performs the
 *	initialization of an encryption or a decryption operation.
 *	When possible and applicable, will internally use the pre-expanded key
 *	schedule from the context template, tmpl.
 *	When complete and successful, 'ctxp' will contain a crypto_context_t
 *	valid for later calls to encrypt_update() and encrypt_final(), or
 *	decrypt_update() and decrypt_final().
 *	The caller should hold a reference on the specified provider
 *	descriptor before calling this function.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
static int
crypto_cipher_init_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_spi_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *crq, crypto_func_group_t func)
{
	int error;
	crypto_ctx_t *ctx;
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		if (func == CRYPTO_FG_ENCRYPT) {
			error = kcf_get_hardware_provider(mech->cm_type,
			    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
			    &real_provider, CRYPTO_FG_ENCRYPT);
		} else {
			error = kcf_get_hardware_provider(mech->cm_type,
			    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
			    &real_provider, CRYPTO_FG_DECRYPT);
		}

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

		if (func == CRYPTO_FG_ENCRYPT)
			error = KCF_PROV_ENCRYPT_INIT(real_provider, ctx,
			    &lmech, key, tmpl, KCF_SWFP_RHNDL(crq));
		else {
			ASSERT(func == CRYPTO_FG_DECRYPT);

			error = KCF_PROV_DECRYPT_INIT(real_provider, ctx,
			    &lmech, key, tmpl, KCF_SWFP_RHNDL(crq));
		}
		KCF_PROV_INCRSTATS(pd, error);

		goto done;
	}

	/* Check if context sharing is possible */
	if (pd->pd_prov_type == CRYPTO_HW_PROVIDER &&
	    key->ck_format == CRYPTO_KEY_RAW &&
	    KCF_CAN_SHARE_OPSTATE(pd, mech->cm_type)) {
		kcf_context_t *tctxp = (kcf_context_t *)ctx;
		kcf_provider_desc_t *tpd = NULL;
		crypto_mech_info_t *sinfo;

		if ((kcf_get_sw_prov(mech->cm_type, &tpd, &tctxp->kc_mech,
		    B_FALSE) == CRYPTO_SUCCESS)) {
			int tlen;

			sinfo = &(KCF_TO_PROV_MECHINFO(tpd, mech->cm_type));
			/*
			 * key->ck_length from the consumer is always in bits.
			 * We convert it to be in the same unit registered by
			 * the provider in order to do a comparison.
			 */
			if (sinfo->cm_mech_flags & CRYPTO_KEYSIZE_UNIT_IN_BYTES)
				tlen = key->ck_length >> 3;
			else
				tlen = key->ck_length;
			/*
			 * Check if the software provider can support context
			 * sharing and support this key length.
			 */
			if ((sinfo->cm_mech_flags & CRYPTO_CAN_SHARE_OPSTATE) &&
			    (tlen >= sinfo->cm_min_key_length) &&
			    (tlen <= sinfo->cm_max_key_length)) {
				ctx->cc_flags = CRYPTO_INIT_OPSTATE;
				tctxp->kc_sw_prov_desc = tpd;
			} else
				KCF_PROV_REFRELE(tpd);
		}
	}

	if (func == CRYPTO_FG_ENCRYPT) {
		KCF_WRAP_ENCRYPT_OPS_PARAMS(&params, KCF_OP_INIT, sid,
		    mech, key, NULL, NULL, tmpl);
	} else {
		ASSERT(func == CRYPTO_FG_DECRYPT);
		KCF_WRAP_DECRYPT_OPS_PARAMS(&params, KCF_OP_INIT, sid,
		    mech, key, NULL, NULL, tmpl);
	}

	error = kcf_submit_request(real_provider, ctx, crq, &params,
	    B_FALSE);

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

done:
	if ((error == CRYPTO_SUCCESS) || (error == CRYPTO_QUEUED))
		*ctxp = (crypto_context_t)ctx;
	else {
		/* Release the hold done in kcf_new_ctx(). */
		KCF_CONTEXT_REFRELE((kcf_context_t *)ctx->cc_framework_private);
	}

	return (error);
}

/*
 * Same as crypto_cipher_init_prov(), but relies on the scheduler to pick
 * an appropriate provider. See crypto_cipher_init_prov() comments for more
 * details.
 */
static int
crypto_cipher_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *crq, crypto_func_group_t func)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, func, CHECK_RESTRICT(crq), 0)) == NULL) {
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

	error = crypto_cipher_init_prov(pd, pd->pd_sid, mech, key,
	    spi_ctx_tmpl, ctxp, crq, func);
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
 * crypto_encrypt_prov()
 *
 * Arguments:
 *	pd:	provider descriptor
 *	sid:	session id
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	plaintext: The message to be encrypted
 *	ciphertext: Storage for the encrypted message. The length needed
 *		depends on the mechanism, and the plaintext's size.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of an
 *		encryption with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	single-part encryption of 'plaintext' with the mechanism 'mech', using
 *	the key 'key'.
 *	When complete and successful, 'ciphertext' will contain the encrypted
 *	message.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_encrypt_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_data_t *plaintext, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_data_t *ciphertext,
    crypto_call_req_t *crq)
{
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;
	int error;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		error = kcf_get_hardware_provider(mech->cm_type,
		    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
		    &real_provider, CRYPTO_FG_ENCRYPT_ATOMIC);

		if (error != CRYPTO_SUCCESS)
			return (error);
	}

	KCF_WRAP_ENCRYPT_OPS_PARAMS(&params, KCF_OP_ATOMIC, sid, mech, key,
	    plaintext, ciphertext, tmpl);

	error = kcf_submit_request(real_provider, NULL, crq, &params, B_FALSE);
	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

	return (error);
}

/*
 * Same as crypto_encrypt_prov(), but relies on the scheduler to pick
 * a provider. See crypto_encrypt_prov() for more details.
 */
int
crypto_encrypt(crypto_mechanism_t *mech, crypto_data_t *plaintext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *ciphertext,
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
	/* pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_ENCRYPT_ATOMIC, CHECK_RESTRICT(crq),
	    plaintext->cd_length)) == NULL) {
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

		error = KCF_PROV_ENCRYPT_ATOMIC(pd, pd->pd_sid, &lmech, key,
		    plaintext, ciphertext, spi_ctx_tmpl, KCF_SWFP_RHNDL(crq));
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_ENCRYPT_OPS_PARAMS(&params, KCF_OP_ATOMIC, pd->pd_sid,
		    mech, key, plaintext, ciphertext, spi_ctx_tmpl);
		error = kcf_submit_request(pd, NULL, crq, &params, B_FALSE);
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
 * crypto_encrypt_init_prov()
 *
 * Calls crypto_cipher_init_prov() to initialize an encryption operation.
 */
int
crypto_encrypt_init_prov(crypto_provider_t pd, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *crq)
{
	return (crypto_cipher_init_prov(pd, sid, mech, key, tmpl, ctxp, crq,
	    CRYPTO_FG_ENCRYPT));
}

/*
 * crypto_encrypt_init()
 *
 * Calls crypto_cipher_init() to initialize an encryption operation
 */
int
crypto_encrypt_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *crq)
{
	return (crypto_cipher_init(mech, key, tmpl, ctxp, crq,
	    CRYPTO_FG_ENCRYPT));
}

/*
 * crypto_encrypt_update()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by encrypt_init().
 *	plaintext: The message part to be encrypted
 *	ciphertext: Storage for the encrypted message part.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	part of an encryption operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_encrypt_update(crypto_context_t context, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_call_req_t *cr)
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
		error = KCF_PROV_ENCRYPT_UPDATE(pd, ctx, plaintext,
		    ciphertext, NULL);
		KCF_PROV_INCRSTATS(pd, error);
		return (error);
	}

	/* Check if we should use a software provider for small jobs */
	if ((ctx->cc_flags & CRYPTO_USE_OPSTATE) && cr == NULL) {
		if (plaintext->cd_length < kcf_ctx->kc_mech->me_threshold &&
		    kcf_ctx->kc_sw_prov_desc != NULL &&
		    KCF_IS_PROV_USABLE(kcf_ctx->kc_sw_prov_desc)) {
			pd = kcf_ctx->kc_sw_prov_desc;
		}
	}

	KCF_WRAP_ENCRYPT_OPS_PARAMS(&params, KCF_OP_UPDATE,
	    ctx->cc_session, NULL, NULL, plaintext, ciphertext, NULL);
	error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);

	return (error);
}

/*
 * crypto_encrypt_final()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by encrypt_init().
 *	ciphertext: Storage for the last part of encrypted message
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs the
 *	final part of an encryption operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_encrypt_final(crypto_context_t context, crypto_data_t *ciphertext,
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
		error = KCF_PROV_ENCRYPT_FINAL(pd, ctx, ciphertext, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_ENCRYPT_OPS_PARAMS(&params, KCF_OP_FINAL,
		    ctx->cc_session, NULL, NULL, NULL, ciphertext, NULL);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(error, kcf_ctx);
	return (error);
}

/*
 * crypto_decrypt_prov()
 *
 * Arguments:
 *	pd:	provider descriptor
 *	sid:	session id
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	ciphertext: The message to be encrypted
 *	plaintext: Storage for the encrypted message. The length needed
 *		depends on the mechanism, and the plaintext's size.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of an
 *		encryption with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	single-part decryption of 'ciphertext' with the mechanism 'mech', using
 *	the key 'key'.
 *	When complete and successful, 'plaintext' will contain the decrypted
 *	message.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_decrypt_prov(crypto_provider_t provider, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_data_t *ciphertext, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_data_t *plaintext,
    crypto_call_req_t *crq)
{
	kcf_req_params_t params;
	kcf_provider_desc_t *pd = provider;
	kcf_provider_desc_t *real_provider = pd;
	int rv;

	ASSERT(KCF_PROV_REFHELD(pd));

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		rv = kcf_get_hardware_provider(mech->cm_type,
		    CRYPTO_MECH_INVALID, CHECK_RESTRICT(crq), pd,
		    &real_provider, CRYPTO_FG_DECRYPT_ATOMIC);

		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}

	KCF_WRAP_DECRYPT_OPS_PARAMS(&params, KCF_OP_ATOMIC, sid, mech, key,
	    ciphertext, plaintext, tmpl);

	rv = kcf_submit_request(real_provider, NULL, crq, &params, B_FALSE);
	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		KCF_PROV_REFRELE(real_provider);

	return (rv);
}

/*
 * Same as crypto_decrypt_prov(), but relies on the KCF scheduler to
 * choose a provider. See crypto_decrypt_prov() comments for more
 * information.
 */
int
crypto_decrypt(crypto_mechanism_t *mech, crypto_data_t *ciphertext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *plaintext,
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
	/* pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_DECRYPT_ATOMIC, CHECK_RESTRICT(crq),
	    ciphertext->cd_length)) == NULL) {
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

		error = KCF_PROV_DECRYPT_ATOMIC(pd, pd->pd_sid, &lmech, key,
		    ciphertext, plaintext, spi_ctx_tmpl, KCF_SWFP_RHNDL(crq));
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DECRYPT_OPS_PARAMS(&params, KCF_OP_ATOMIC, pd->pd_sid,
		    mech, key, ciphertext, plaintext, spi_ctx_tmpl);
		error = kcf_submit_request(pd, NULL, crq, &params, B_FALSE);
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
 * crypto_decrypt_init_prov()
 *
 * Calls crypto_cipher_init_prov() to initialize a decryption operation
 */
int
crypto_decrypt_init_prov(crypto_provider_t pd, crypto_session_id_t sid,
    crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *crq)
{
	return (crypto_cipher_init_prov(pd, sid, mech, key, tmpl, ctxp, crq,
	    CRYPTO_FG_DECRYPT));
}

/*
 * crypto_decrypt_init()
 *
 * Calls crypto_cipher_init() to initialize a decryption operation
 */
int
crypto_decrypt_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *crq)
{
	return (crypto_cipher_init(mech, key, tmpl, ctxp, crq,
	    CRYPTO_FG_DECRYPT));
}

/*
 * crypto_decrypt_update()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by decrypt_init().
 *	ciphertext: The message part to be decrypted
 *	plaintext: Storage for the decrypted message part.
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	part of an decryption operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_decrypt_update(crypto_context_t context, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_call_req_t *cr)
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
		error = KCF_PROV_DECRYPT_UPDATE(pd, ctx, ciphertext,
		    plaintext, NULL);
		KCF_PROV_INCRSTATS(pd, error);
		return (error);
	}

	/* Check if we should use a software provider for small jobs */
	if ((ctx->cc_flags & CRYPTO_USE_OPSTATE) && cr == NULL) {
		if (ciphertext->cd_length < kcf_ctx->kc_mech->me_threshold &&
		    kcf_ctx->kc_sw_prov_desc != NULL &&
		    KCF_IS_PROV_USABLE(kcf_ctx->kc_sw_prov_desc)) {
			pd = kcf_ctx->kc_sw_prov_desc;
		}
	}

	KCF_WRAP_DECRYPT_OPS_PARAMS(&params, KCF_OP_UPDATE,
	    ctx->cc_session, NULL, NULL, ciphertext, plaintext, NULL);
	error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);

	return (error);
}

/*
 * crypto_decrypt_final()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by decrypt_init().
 *	plaintext: Storage for the last part of the decrypted message
 *	cr:	crypto_call_req_t calling conditions and call back info.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs the
 *	final part of a decryption operation.
 *
 * Context:
 *	Process or interrupt, according to the semantics dictated by the 'cr'.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_decrypt_final(crypto_context_t context, crypto_data_t *plaintext,
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
		error = KCF_PROV_DECRYPT_FINAL(pd, ctx, plaintext,
		    NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DECRYPT_OPS_PARAMS(&params, KCF_OP_FINAL,
		    ctx->cc_session, NULL, NULL, NULL, plaintext, NULL);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(error, kcf_ctx);
	return (error);
}

/*
 * See comments for crypto_encrypt_update().
 */
int
crypto_encrypt_single(crypto_context_t context, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_call_req_t *cr)
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
		error = KCF_PROV_ENCRYPT(pd, ctx, plaintext,
		    ciphertext, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_ENCRYPT_OPS_PARAMS(&params, KCF_OP_SINGLE, pd->pd_sid,
		    NULL, NULL, plaintext, ciphertext, NULL);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(error, kcf_ctx);
	return (error);
}

/*
 * See comments for crypto_decrypt_update().
 */
int
crypto_decrypt_single(crypto_context_t context, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_call_req_t *cr)
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
		error = KCF_PROV_DECRYPT(pd, ctx, ciphertext,
		    plaintext, NULL);
		KCF_PROV_INCRSTATS(pd, error);
	} else {
		KCF_WRAP_DECRYPT_OPS_PARAMS(&params, KCF_OP_SINGLE, pd->pd_sid,
		    NULL, NULL, ciphertext, plaintext, NULL);
		error = kcf_submit_request(pd, ctx, cr, &params, B_FALSE);
	}

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(error, kcf_ctx);
	return (error);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(crypto_cipher_init_prov);
EXPORT_SYMBOL(crypto_cipher_init);
EXPORT_SYMBOL(crypto_encrypt_prov);
EXPORT_SYMBOL(crypto_encrypt);
EXPORT_SYMBOL(crypto_encrypt_init_prov);
EXPORT_SYMBOL(crypto_encrypt_init);
EXPORT_SYMBOL(crypto_encrypt_update);
EXPORT_SYMBOL(crypto_encrypt_final);
EXPORT_SYMBOL(crypto_decrypt_prov);
EXPORT_SYMBOL(crypto_decrypt);
EXPORT_SYMBOL(crypto_decrypt_init_prov);
EXPORT_SYMBOL(crypto_decrypt_init);
EXPORT_SYMBOL(crypto_decrypt_update);
EXPORT_SYMBOL(crypto_decrypt_final);
EXPORT_SYMBOL(crypto_encrypt_single);
EXPORT_SYMBOL(crypto_decrypt_single);
#endif
