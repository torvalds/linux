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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/modctl.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>

#include <sha1/sha1.h>
#include <sha1/sha1_impl.h>

/*
 * The sha1 module is created with two modlinkages:
 * - a modlmisc that allows consumers to directly call the entry points
 *   SHA1Init, SHA1Update, and SHA1Final.
 * - a modlcrypto that allows the module to register with the Kernel
 *   Cryptographic Framework (KCF) as a software provider for the SHA1
 *   mechanisms.
 */

static struct modlcrypto modlcrypto = {
	&mod_cryptoops,
	"SHA1 Kernel SW Provider 1.1"
};

static struct modlinkage modlinkage = {
	MODREV_1, { &modlcrypto, NULL }
};


/*
 * Macros to access the SHA1 or SHA1-HMAC contexts from a context passed
 * by KCF to one of the entry points.
 */

#define	PROV_SHA1_CTX(ctx)	((sha1_ctx_t *)(ctx)->cc_provider_private)
#define	PROV_SHA1_HMAC_CTX(ctx)	((sha1_hmac_ctx_t *)(ctx)->cc_provider_private)

/* to extract the digest length passed as mechanism parameter */
#define	PROV_SHA1_GET_DIGEST_LEN(m, len) {				\
	if (IS_P2ALIGNED((m)->cm_param, sizeof (ulong_t)))		\
		(len) = (uint32_t)*((ulong_t *)(void *)mechanism->cm_param); \
	else {								\
		ulong_t tmp_ulong;					\
		bcopy((m)->cm_param, &tmp_ulong, sizeof (ulong_t));	\
		(len) = (uint32_t)tmp_ulong;				\
	}								\
}

#define	PROV_SHA1_DIGEST_KEY(ctx, key, len, digest) {	\
	SHA1Init(ctx);					\
	SHA1Update(ctx, key, len);			\
	SHA1Final(digest, ctx);				\
}

/*
 * Mechanism info structure passed to KCF during registration.
 */
static crypto_mech_info_t sha1_mech_info_tab[] = {
	/* SHA1 */
	{SUN_CKM_SHA1, SHA1_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, 0, CRYPTO_KEYSIZE_UNIT_IN_BITS},
	/* SHA1-HMAC */
	{SUN_CKM_SHA1_HMAC, SHA1_HMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC,
	    SHA1_HMAC_MIN_KEY_LEN, SHA1_HMAC_MAX_KEY_LEN,
	    CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	/* SHA1-HMAC GENERAL */
	{SUN_CKM_SHA1_HMAC_GENERAL, SHA1_HMAC_GEN_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC,
	    SHA1_HMAC_MIN_KEY_LEN, SHA1_HMAC_MAX_KEY_LEN,
	    CRYPTO_KEYSIZE_UNIT_IN_BYTES}
};

static void sha1_provider_status(crypto_provider_handle_t, uint_t *);

static crypto_control_ops_t sha1_control_ops = {
	sha1_provider_status
};

static int sha1_digest_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_req_handle_t);
static int sha1_digest(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int sha1_digest_update(crypto_ctx_t *, crypto_data_t *,
    crypto_req_handle_t);
static int sha1_digest_final(crypto_ctx_t *, crypto_data_t *,
    crypto_req_handle_t);
static int sha1_digest_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);

static crypto_digest_ops_t sha1_digest_ops = {
	.digest_init = sha1_digest_init,
	.digest = sha1_digest,
	.digest_update = sha1_digest_update,
	.digest_key = NULL,
	.digest_final = sha1_digest_final,
	.digest_atomic = sha1_digest_atomic
};

static int sha1_mac_init(crypto_ctx_t *, crypto_mechanism_t *, crypto_key_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);
static int sha1_mac_update(crypto_ctx_t *, crypto_data_t *,
    crypto_req_handle_t);
static int sha1_mac_final(crypto_ctx_t *, crypto_data_t *, crypto_req_handle_t);
static int sha1_mac_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *, crypto_data_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);
static int sha1_mac_verify_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *, crypto_data_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);

static crypto_mac_ops_t sha1_mac_ops = {
	.mac_init = sha1_mac_init,
	.mac = NULL,
	.mac_update = sha1_mac_update,
	.mac_final = sha1_mac_final,
	.mac_atomic = sha1_mac_atomic,
	.mac_verify_atomic = sha1_mac_verify_atomic
};

static int sha1_create_ctx_template(crypto_provider_handle_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_spi_ctx_template_t *,
    size_t *, crypto_req_handle_t);
static int sha1_free_context(crypto_ctx_t *);

static crypto_ctx_ops_t sha1_ctx_ops = {
	.create_ctx_template = sha1_create_ctx_template,
	.free_context = sha1_free_context
};

static crypto_ops_t sha1_crypto_ops = {{{{{
	&sha1_control_ops,
	&sha1_digest_ops,
	NULL,
	&sha1_mac_ops,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	&sha1_ctx_ops,
}}}}};

static crypto_provider_info_t sha1_prov_info = {{{{
	CRYPTO_SPI_VERSION_1,
	"SHA1 Software Provider",
	CRYPTO_SW_PROVIDER,
	NULL,
	&sha1_crypto_ops,
	sizeof (sha1_mech_info_tab)/sizeof (crypto_mech_info_t),
	sha1_mech_info_tab
}}}};

static crypto_kcf_provider_handle_t sha1_prov_handle = 0;

int
sha1_mod_init(void)
{
	int ret;

	if ((ret = mod_install(&modlinkage)) != 0)
		return (ret);

	/*
	 * Register with KCF. If the registration fails, log an
	 * error but do not uninstall the module, since the functionality
	 * provided by misc/sha1 should still be available.
	 */
	if ((ret = crypto_register_provider(&sha1_prov_info,
	    &sha1_prov_handle)) != CRYPTO_SUCCESS)
		cmn_err(CE_WARN, "sha1 _init: "
		    "crypto_register_provider() failed (0x%x)", ret);

	return (0);
}

int
sha1_mod_fini(void)
{
	int ret;

	if (sha1_prov_handle != 0) {
		if ((ret = crypto_unregister_provider(sha1_prov_handle)) !=
		    CRYPTO_SUCCESS) {
			cmn_err(CE_WARN,
			    "sha1 _fini: crypto_unregister_provider() "
			    "failed (0x%x)", ret);
			return (EBUSY);
		}
		sha1_prov_handle = 0;
	}

	return (mod_remove(&modlinkage));
}

/*
 * KCF software provider control entry points.
 */
/* ARGSUSED */
static void
sha1_provider_status(crypto_provider_handle_t provider, uint_t *status)
{
	*status = CRYPTO_PROVIDER_READY;
}

/*
 * KCF software provider digest entry points.
 */

static int
sha1_digest_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_req_handle_t req)
{
	if (mechanism->cm_type != SHA1_MECH_INFO_TYPE)
		return (CRYPTO_MECHANISM_INVALID);

	/*
	 * Allocate and initialize SHA1 context.
	 */
	ctx->cc_provider_private = kmem_alloc(sizeof (sha1_ctx_t),
	    crypto_kmflag(req));
	if (ctx->cc_provider_private == NULL)
		return (CRYPTO_HOST_MEMORY);

	PROV_SHA1_CTX(ctx)->sc_mech_type = SHA1_MECH_INFO_TYPE;
	SHA1Init(&PROV_SHA1_CTX(ctx)->sc_sha1_ctx);

	return (CRYPTO_SUCCESS);
}

/*
 * Helper SHA1 digest update function for uio data.
 */
static int
sha1_digest_update_uio(SHA1_CTX *sha1_ctx, crypto_data_t *data)
{
	off_t offset = data->cd_offset;
	size_t length = data->cd_length;
	uint_t vec_idx;
	size_t cur_len;

	/* we support only kernel buffer */
	if (data->cd_uio->uio_segflg != UIO_SYSSPACE)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Jump to the first iovec containing data to be
	 * digested.
	 */
	for (vec_idx = 0; vec_idx < data->cd_uio->uio_iovcnt &&
	    offset >= data->cd_uio->uio_iov[vec_idx].iov_len;
	    offset -= data->cd_uio->uio_iov[vec_idx++].iov_len)
		;
	if (vec_idx == data->cd_uio->uio_iovcnt) {
		/*
		 * The caller specified an offset that is larger than the
		 * total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * Now do the digesting on the iovecs.
	 */
	while (vec_idx < data->cd_uio->uio_iovcnt && length > 0) {
		cur_len = MIN(data->cd_uio->uio_iov[vec_idx].iov_len -
		    offset, length);

		SHA1Update(sha1_ctx,
		    (uint8_t *)data->cd_uio->uio_iov[vec_idx].iov_base + offset,
		    cur_len);

		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == data->cd_uio->uio_iovcnt && length > 0) {
		/*
		 * The end of the specified iovec's was reached but
		 * the length requested could not be processed, i.e.
		 * The caller requested to digest more data than it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	return (CRYPTO_SUCCESS);
}

/*
 * Helper SHA1 digest final function for uio data.
 * digest_len is the length of the desired digest. If digest_len
 * is smaller than the default SHA1 digest length, the caller
 * must pass a scratch buffer, digest_scratch, which must
 * be at least SHA1_DIGEST_LENGTH bytes.
 */
static int
sha1_digest_final_uio(SHA1_CTX *sha1_ctx, crypto_data_t *digest,
    ulong_t digest_len, uchar_t *digest_scratch)
{
	off_t offset = digest->cd_offset;
	uint_t vec_idx;

	/* we support only kernel buffer */
	if (digest->cd_uio->uio_segflg != UIO_SYSSPACE)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Jump to the first iovec containing ptr to the digest to
	 * be returned.
	 */
	for (vec_idx = 0; offset >= digest->cd_uio->uio_iov[vec_idx].iov_len &&
	    vec_idx < digest->cd_uio->uio_iovcnt;
	    offset -= digest->cd_uio->uio_iov[vec_idx++].iov_len)
		;
	if (vec_idx == digest->cd_uio->uio_iovcnt) {
		/*
		 * The caller specified an offset that is
		 * larger than the total size of the buffers
		 * it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	if (offset + digest_len <=
	    digest->cd_uio->uio_iov[vec_idx].iov_len) {
		/*
		 * The computed SHA1 digest will fit in the current
		 * iovec.
		 */
		if (digest_len != SHA1_DIGEST_LENGTH) {
			/*
			 * The caller requested a short digest. Digest
			 * into a scratch buffer and return to
			 * the user only what was requested.
			 */
			SHA1Final(digest_scratch, sha1_ctx);
			bcopy(digest_scratch, (uchar_t *)digest->
			    cd_uio->uio_iov[vec_idx].iov_base + offset,
			    digest_len);
		} else {
			SHA1Final((uchar_t *)digest->
			    cd_uio->uio_iov[vec_idx].iov_base + offset,
			    sha1_ctx);
		}
	} else {
		/*
		 * The computed digest will be crossing one or more iovec's.
		 * This is bad performance-wise but we need to support it.
		 * Allocate a small scratch buffer on the stack and
		 * copy it piece meal to the specified digest iovec's.
		 */
		uchar_t digest_tmp[SHA1_DIGEST_LENGTH];
		off_t scratch_offset = 0;
		size_t length = digest_len;
		size_t cur_len;

		SHA1Final(digest_tmp, sha1_ctx);

		while (vec_idx < digest->cd_uio->uio_iovcnt && length > 0) {
			cur_len = MIN(digest->cd_uio->uio_iov[vec_idx].iov_len -
			    offset, length);
			bcopy(digest_tmp + scratch_offset,
			    digest->cd_uio->uio_iov[vec_idx].iov_base + offset,
			    cur_len);

			length -= cur_len;
			vec_idx++;
			scratch_offset += cur_len;
			offset = 0;
		}

		if (vec_idx == digest->cd_uio->uio_iovcnt && length > 0) {
			/*
			 * The end of the specified iovec's was reached but
			 * the length requested could not be processed, i.e.
			 * The caller requested to digest more data than it
			 * provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}
	}

	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
static int
sha1_digest(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *digest,
    crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;

	ASSERT(ctx->cc_provider_private != NULL);

	/*
	 * We need to just return the length needed to store the output.
	 * We should not destroy the context for the following cases.
	 */
	if ((digest->cd_length == 0) ||
	    (digest->cd_length < SHA1_DIGEST_LENGTH)) {
		digest->cd_length = SHA1_DIGEST_LENGTH;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/*
	 * Do the SHA1 update on the specified input data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA1Update(&PROV_SHA1_CTX(ctx)->sc_sha1_ctx,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_update_uio(&PROV_SHA1_CTX(ctx)->sc_sha1_ctx,
		    data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret != CRYPTO_SUCCESS) {
		/* the update failed, free context and bail */
		kmem_free(ctx->cc_provider_private, sizeof (sha1_ctx_t));
		ctx->cc_provider_private = NULL;
		digest->cd_length = 0;
		return (ret);
	}

	/*
	 * Do a SHA1 final, must be done separately since the digest
	 * type can be different than the input data type.
	 */
	switch (digest->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA1Final((unsigned char *)digest->cd_raw.iov_base +
		    digest->cd_offset, &PROV_SHA1_CTX(ctx)->sc_sha1_ctx);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_final_uio(&PROV_SHA1_CTX(ctx)->sc_sha1_ctx,
		    digest, SHA1_DIGEST_LENGTH, NULL);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	/* all done, free context and return */

	if (ret == CRYPTO_SUCCESS) {
		digest->cd_length = SHA1_DIGEST_LENGTH;
	} else {
		digest->cd_length = 0;
	}

	kmem_free(ctx->cc_provider_private, sizeof (sha1_ctx_t));
	ctx->cc_provider_private = NULL;
	return (ret);
}

/* ARGSUSED */
static int
sha1_digest_update(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;

	ASSERT(ctx->cc_provider_private != NULL);

	/*
	 * Do the SHA1 update on the specified input data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA1Update(&PROV_SHA1_CTX(ctx)->sc_sha1_ctx,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_update_uio(&PROV_SHA1_CTX(ctx)->sc_sha1_ctx,
		    data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	return (ret);
}

/* ARGSUSED */
static int
sha1_digest_final(crypto_ctx_t *ctx, crypto_data_t *digest,
    crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;

	ASSERT(ctx->cc_provider_private != NULL);

	/*
	 * We need to just return the length needed to store the output.
	 * We should not destroy the context for the following cases.
	 */
	if ((digest->cd_length == 0) ||
	    (digest->cd_length < SHA1_DIGEST_LENGTH)) {
		digest->cd_length = SHA1_DIGEST_LENGTH;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/*
	 * Do a SHA1 final.
	 */
	switch (digest->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA1Final((unsigned char *)digest->cd_raw.iov_base +
		    digest->cd_offset, &PROV_SHA1_CTX(ctx)->sc_sha1_ctx);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_final_uio(&PROV_SHA1_CTX(ctx)->sc_sha1_ctx,
		    digest, SHA1_DIGEST_LENGTH, NULL);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	/* all done, free context and return */

	if (ret == CRYPTO_SUCCESS) {
		digest->cd_length = SHA1_DIGEST_LENGTH;
	} else {
		digest->cd_length = 0;
	}

	kmem_free(ctx->cc_provider_private, sizeof (sha1_ctx_t));
	ctx->cc_provider_private = NULL;

	return (ret);
}

/* ARGSUSED */
static int
sha1_digest_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_data_t *data, crypto_data_t *digest,
    crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;
	SHA1_CTX sha1_ctx;

	if (mechanism->cm_type != SHA1_MECH_INFO_TYPE)
		return (CRYPTO_MECHANISM_INVALID);

	/*
	 * Do the SHA1 init.
	 */
	SHA1Init(&sha1_ctx);

	/*
	 * Do the SHA1 update on the specified input data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA1Update(&sha1_ctx,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_update_uio(&sha1_ctx, data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret != CRYPTO_SUCCESS) {
		/* the update failed, bail */
		digest->cd_length = 0;
		return (ret);
	}

	/*
	 * Do a SHA1 final, must be done separately since the digest
	 * type can be different than the input data type.
	 */
	switch (digest->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA1Final((unsigned char *)digest->cd_raw.iov_base +
		    digest->cd_offset, &sha1_ctx);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_final_uio(&sha1_ctx, digest,
		    SHA1_DIGEST_LENGTH, NULL);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		digest->cd_length = SHA1_DIGEST_LENGTH;
	} else {
		digest->cd_length = 0;
	}

	return (ret);
}

/*
 * KCF software provider mac entry points.
 *
 * SHA1 HMAC is: SHA1(key XOR opad, SHA1(key XOR ipad, text))
 *
 * Init:
 * The initialization routine initializes what we denote
 * as the inner and outer contexts by doing
 * - for inner context: SHA1(key XOR ipad)
 * - for outer context: SHA1(key XOR opad)
 *
 * Update:
 * Each subsequent SHA1 HMAC update will result in an
 * update of the inner context with the specified data.
 *
 * Final:
 * The SHA1 HMAC final will do a SHA1 final operation on the
 * inner context, and the resulting digest will be used
 * as the data for an update on the outer context. Last
 * but not least, a SHA1 final on the outer context will
 * be performed to obtain the SHA1 HMAC digest to return
 * to the user.
 */

/*
 * Initialize a SHA1-HMAC context.
 */
static void
sha1_mac_init_ctx(sha1_hmac_ctx_t *ctx, void *keyval, uint_t length_in_bytes)
{
	uint32_t ipad[SHA1_HMAC_INTS_PER_BLOCK];
	uint32_t opad[SHA1_HMAC_INTS_PER_BLOCK];
	uint_t i;

	bzero(ipad, SHA1_HMAC_BLOCK_SIZE);
	bzero(opad, SHA1_HMAC_BLOCK_SIZE);

	bcopy(keyval, ipad, length_in_bytes);
	bcopy(keyval, opad, length_in_bytes);

	/* XOR key with ipad (0x36) and opad (0x5c) */
	for (i = 0; i < SHA1_HMAC_INTS_PER_BLOCK; i++) {
		ipad[i] ^= 0x36363636;
		opad[i] ^= 0x5c5c5c5c;
	}

	/* perform SHA1 on ipad */
	SHA1Init(&ctx->hc_icontext);
	SHA1Update(&ctx->hc_icontext, (uint8_t *)ipad, SHA1_HMAC_BLOCK_SIZE);

	/* perform SHA1 on opad */
	SHA1Init(&ctx->hc_ocontext);
	SHA1Update(&ctx->hc_ocontext, (uint8_t *)opad, SHA1_HMAC_BLOCK_SIZE);
}

/*
 */
static int
sha1_mac_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);

	if (mechanism->cm_type != SHA1_HMAC_MECH_INFO_TYPE &&
	    mechanism->cm_type != SHA1_HMAC_GEN_MECH_INFO_TYPE)
		return (CRYPTO_MECHANISM_INVALID);

	/* Add support for key by attributes (RFE 4706552) */
	if (key->ck_format != CRYPTO_KEY_RAW)
		return (CRYPTO_ARGUMENTS_BAD);

	ctx->cc_provider_private = kmem_alloc(sizeof (sha1_hmac_ctx_t),
	    crypto_kmflag(req));
	if (ctx->cc_provider_private == NULL)
		return (CRYPTO_HOST_MEMORY);

	if (ctx_template != NULL) {
		/* reuse context template */
		bcopy(ctx_template, PROV_SHA1_HMAC_CTX(ctx),
		    sizeof (sha1_hmac_ctx_t));
	} else {
		/* no context template, compute context */
		if (keylen_in_bytes > SHA1_HMAC_BLOCK_SIZE) {
			uchar_t digested_key[SHA1_DIGEST_LENGTH];
			sha1_hmac_ctx_t *hmac_ctx = ctx->cc_provider_private;

			/*
			 * Hash the passed-in key to get a smaller key.
			 * The inner context is used since it hasn't been
			 * initialized yet.
			 */
			PROV_SHA1_DIGEST_KEY(&hmac_ctx->hc_icontext,
			    key->ck_data, keylen_in_bytes, digested_key);
			sha1_mac_init_ctx(PROV_SHA1_HMAC_CTX(ctx),
			    digested_key, SHA1_DIGEST_LENGTH);
		} else {
			sha1_mac_init_ctx(PROV_SHA1_HMAC_CTX(ctx),
			    key->ck_data, keylen_in_bytes);
		}
	}

	/*
	 * Get the mechanism parameters, if applicable.
	 */
	PROV_SHA1_HMAC_CTX(ctx)->hc_mech_type = mechanism->cm_type;
	if (mechanism->cm_type == SHA1_HMAC_GEN_MECH_INFO_TYPE) {
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (ulong_t))
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
		PROV_SHA1_GET_DIGEST_LEN(mechanism,
		    PROV_SHA1_HMAC_CTX(ctx)->hc_digest_len);
		if (PROV_SHA1_HMAC_CTX(ctx)->hc_digest_len >
		    SHA1_DIGEST_LENGTH)
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
	}

	if (ret != CRYPTO_SUCCESS) {
		bzero(ctx->cc_provider_private, sizeof (sha1_hmac_ctx_t));
		kmem_free(ctx->cc_provider_private, sizeof (sha1_hmac_ctx_t));
		ctx->cc_provider_private = NULL;
	}

	return (ret);
}

/* ARGSUSED */
static int
sha1_mac_update(crypto_ctx_t *ctx, crypto_data_t *data, crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;

	ASSERT(ctx->cc_provider_private != NULL);

	/*
	 * Do a SHA1 update of the inner context using the specified
	 * data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA1Update(&PROV_SHA1_HMAC_CTX(ctx)->hc_icontext,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_update_uio(
		    &PROV_SHA1_HMAC_CTX(ctx)->hc_icontext, data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	return (ret);
}

/* ARGSUSED */
static int
sha1_mac_final(crypto_ctx_t *ctx, crypto_data_t *mac, crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;
	uchar_t digest[SHA1_DIGEST_LENGTH];
	uint32_t digest_len = SHA1_DIGEST_LENGTH;

	ASSERT(ctx->cc_provider_private != NULL);

	if (PROV_SHA1_HMAC_CTX(ctx)->hc_mech_type ==
	    SHA1_HMAC_GEN_MECH_INFO_TYPE)
		digest_len = PROV_SHA1_HMAC_CTX(ctx)->hc_digest_len;

	/*
	 * We need to just return the length needed to store the output.
	 * We should not destroy the context for the following cases.
	 */
	if ((mac->cd_length == 0) || (mac->cd_length < digest_len)) {
		mac->cd_length = digest_len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/*
	 * Do a SHA1 final on the inner context.
	 */
	SHA1Final(digest, &PROV_SHA1_HMAC_CTX(ctx)->hc_icontext);

	/*
	 * Do a SHA1 update on the outer context, feeding the inner
	 * digest as data.
	 */
	SHA1Update(&PROV_SHA1_HMAC_CTX(ctx)->hc_ocontext, digest,
	    SHA1_DIGEST_LENGTH);

	/*
	 * Do a SHA1 final on the outer context, storing the computing
	 * digest in the users buffer.
	 */
	switch (mac->cd_format) {
	case CRYPTO_DATA_RAW:
		if (digest_len != SHA1_DIGEST_LENGTH) {
			/*
			 * The caller requested a short digest. Digest
			 * into a scratch buffer and return to
			 * the user only what was requested.
			 */
			SHA1Final(digest,
			    &PROV_SHA1_HMAC_CTX(ctx)->hc_ocontext);
			bcopy(digest, (unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset, digest_len);
		} else {
			SHA1Final((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset,
			    &PROV_SHA1_HMAC_CTX(ctx)->hc_ocontext);
		}
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_final_uio(
		    &PROV_SHA1_HMAC_CTX(ctx)->hc_ocontext, mac,
		    digest_len, digest);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		mac->cd_length = digest_len;
	} else {
		mac->cd_length = 0;
	}

	bzero(ctx->cc_provider_private, sizeof (sha1_hmac_ctx_t));
	kmem_free(ctx->cc_provider_private, sizeof (sha1_hmac_ctx_t));
	ctx->cc_provider_private = NULL;

	return (ret);
}

#define	SHA1_MAC_UPDATE(data, ctx, ret) {				\
	switch (data->cd_format) {					\
	case CRYPTO_DATA_RAW:						\
		SHA1Update(&(ctx).hc_icontext,				\
		    (uint8_t *)data->cd_raw.iov_base +			\
		    data->cd_offset, data->cd_length);			\
		break;							\
	case CRYPTO_DATA_UIO:						\
		ret = sha1_digest_update_uio(&(ctx).hc_icontext, data); \
		break;							\
	default:							\
		ret = CRYPTO_ARGUMENTS_BAD;				\
	}								\
}

/* ARGSUSED */
static int
sha1_mac_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;
	uchar_t digest[SHA1_DIGEST_LENGTH];
	sha1_hmac_ctx_t sha1_hmac_ctx;
	uint32_t digest_len = SHA1_DIGEST_LENGTH;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);

	if (mechanism->cm_type != SHA1_HMAC_MECH_INFO_TYPE &&
	    mechanism->cm_type != SHA1_HMAC_GEN_MECH_INFO_TYPE)
		return (CRYPTO_MECHANISM_INVALID);

	/* Add support for key by attributes (RFE 4706552) */
	if (key->ck_format != CRYPTO_KEY_RAW)
		return (CRYPTO_ARGUMENTS_BAD);

	if (ctx_template != NULL) {
		/* reuse context template */
		bcopy(ctx_template, &sha1_hmac_ctx, sizeof (sha1_hmac_ctx_t));
	} else {
		/* no context template, initialize context */
		if (keylen_in_bytes > SHA1_HMAC_BLOCK_SIZE) {
			/*
			 * Hash the passed-in key to get a smaller key.
			 * The inner context is used since it hasn't been
			 * initialized yet.
			 */
			PROV_SHA1_DIGEST_KEY(&sha1_hmac_ctx.hc_icontext,
			    key->ck_data, keylen_in_bytes, digest);
			sha1_mac_init_ctx(&sha1_hmac_ctx, digest,
			    SHA1_DIGEST_LENGTH);
		} else {
			sha1_mac_init_ctx(&sha1_hmac_ctx, key->ck_data,
			    keylen_in_bytes);
		}
	}

	/* get the mechanism parameters, if applicable */
	if (mechanism->cm_type == SHA1_HMAC_GEN_MECH_INFO_TYPE) {
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (ulong_t)) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
		PROV_SHA1_GET_DIGEST_LEN(mechanism, digest_len);
		if (digest_len > SHA1_DIGEST_LENGTH) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
	}

	/* do a SHA1 update of the inner context using the specified data */
	SHA1_MAC_UPDATE(data, sha1_hmac_ctx, ret);
	if (ret != CRYPTO_SUCCESS)
		/* the update failed, free context and bail */
		goto bail;

	/*
	 * Do a SHA1 final on the inner context.
	 */
	SHA1Final(digest, &sha1_hmac_ctx.hc_icontext);

	/*
	 * Do an SHA1 update on the outer context, feeding the inner
	 * digest as data.
	 */
	SHA1Update(&sha1_hmac_ctx.hc_ocontext, digest, SHA1_DIGEST_LENGTH);

	/*
	 * Do a SHA1 final on the outer context, storing the computed
	 * digest in the users buffer.
	 */
	switch (mac->cd_format) {
	case CRYPTO_DATA_RAW:
		if (digest_len != SHA1_DIGEST_LENGTH) {
			/*
			 * The caller requested a short digest. Digest
			 * into a scratch buffer and return to
			 * the user only what was requested.
			 */
			SHA1Final(digest, &sha1_hmac_ctx.hc_ocontext);
			bcopy(digest, (unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset, digest_len);
		} else {
			SHA1Final((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset, &sha1_hmac_ctx.hc_ocontext);
		}
		break;
	case CRYPTO_DATA_UIO:
		ret = sha1_digest_final_uio(&sha1_hmac_ctx.hc_ocontext, mac,
		    digest_len, digest);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		mac->cd_length = digest_len;
	} else {
		mac->cd_length = 0;
	}
	/* Extra paranoia: zeroize the context on the stack */
	bzero(&sha1_hmac_ctx, sizeof (sha1_hmac_ctx_t));

	return (ret);
bail:
	bzero(&sha1_hmac_ctx, sizeof (sha1_hmac_ctx_t));
	mac->cd_length = 0;
	return (ret);
}

/* ARGSUSED */
static int
sha1_mac_verify_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;
	uchar_t digest[SHA1_DIGEST_LENGTH];
	sha1_hmac_ctx_t sha1_hmac_ctx;
	uint32_t digest_len = SHA1_DIGEST_LENGTH;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);

	if (mechanism->cm_type != SHA1_HMAC_MECH_INFO_TYPE &&
	    mechanism->cm_type != SHA1_HMAC_GEN_MECH_INFO_TYPE)
		return (CRYPTO_MECHANISM_INVALID);

	/* Add support for key by attributes (RFE 4706552) */
	if (key->ck_format != CRYPTO_KEY_RAW)
		return (CRYPTO_ARGUMENTS_BAD);

	if (ctx_template != NULL) {
		/* reuse context template */
		bcopy(ctx_template, &sha1_hmac_ctx, sizeof (sha1_hmac_ctx_t));
	} else {
		/* no context template, initialize context */
		if (keylen_in_bytes > SHA1_HMAC_BLOCK_SIZE) {
			/*
			 * Hash the passed-in key to get a smaller key.
			 * The inner context is used since it hasn't been
			 * initialized yet.
			 */
			PROV_SHA1_DIGEST_KEY(&sha1_hmac_ctx.hc_icontext,
			    key->ck_data, keylen_in_bytes, digest);
			sha1_mac_init_ctx(&sha1_hmac_ctx, digest,
			    SHA1_DIGEST_LENGTH);
		} else {
			sha1_mac_init_ctx(&sha1_hmac_ctx, key->ck_data,
			    keylen_in_bytes);
		}
	}

	/* get the mechanism parameters, if applicable */
	if (mechanism->cm_type == SHA1_HMAC_GEN_MECH_INFO_TYPE) {
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (ulong_t)) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
		PROV_SHA1_GET_DIGEST_LEN(mechanism, digest_len);
		if (digest_len > SHA1_DIGEST_LENGTH) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
	}

	if (mac->cd_length != digest_len) {
		ret = CRYPTO_INVALID_MAC;
		goto bail;
	}

	/* do a SHA1 update of the inner context using the specified data */
	SHA1_MAC_UPDATE(data, sha1_hmac_ctx, ret);
	if (ret != CRYPTO_SUCCESS)
		/* the update failed, free context and bail */
		goto bail;

	/* do a SHA1 final on the inner context */
	SHA1Final(digest, &sha1_hmac_ctx.hc_icontext);

	/*
	 * Do an SHA1 update on the outer context, feeding the inner
	 * digest as data.
	 */
	SHA1Update(&sha1_hmac_ctx.hc_ocontext, digest, SHA1_DIGEST_LENGTH);

	/*
	 * Do a SHA1 final on the outer context, storing the computed
	 * digest in the users buffer.
	 */
	SHA1Final(digest, &sha1_hmac_ctx.hc_ocontext);

	/*
	 * Compare the computed digest against the expected digest passed
	 * as argument.
	 */

	switch (mac->cd_format) {

	case CRYPTO_DATA_RAW:
		if (bcmp(digest, (unsigned char *)mac->cd_raw.iov_base +
		    mac->cd_offset, digest_len) != 0)
			ret = CRYPTO_INVALID_MAC;
		break;

	case CRYPTO_DATA_UIO: {
		off_t offset = mac->cd_offset;
		uint_t vec_idx;
		off_t scratch_offset = 0;
		size_t length = digest_len;
		size_t cur_len;

		/* we support only kernel buffer */
		if (mac->cd_uio->uio_segflg != UIO_SYSSPACE)
			return (CRYPTO_ARGUMENTS_BAD);

		/* jump to the first iovec containing the expected digest */
		for (vec_idx = 0;
		    offset >= mac->cd_uio->uio_iov[vec_idx].iov_len &&
		    vec_idx < mac->cd_uio->uio_iovcnt;
		    offset -= mac->cd_uio->uio_iov[vec_idx++].iov_len)
			;
		if (vec_idx == mac->cd_uio->uio_iovcnt) {
			/*
			 * The caller specified an offset that is
			 * larger than the total size of the buffers
			 * it provided.
			 */
			ret = CRYPTO_DATA_LEN_RANGE;
			break;
		}

		/* do the comparison of computed digest vs specified one */
		while (vec_idx < mac->cd_uio->uio_iovcnt && length > 0) {
			cur_len = MIN(mac->cd_uio->uio_iov[vec_idx].iov_len -
			    offset, length);

			if (bcmp(digest + scratch_offset,
			    mac->cd_uio->uio_iov[vec_idx].iov_base + offset,
			    cur_len) != 0) {
				ret = CRYPTO_INVALID_MAC;
				break;
			}

			length -= cur_len;
			vec_idx++;
			scratch_offset += cur_len;
			offset = 0;
		}
		break;
	}

	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	bzero(&sha1_hmac_ctx, sizeof (sha1_hmac_ctx_t));
	return (ret);
bail:
	bzero(&sha1_hmac_ctx, sizeof (sha1_hmac_ctx_t));
	mac->cd_length = 0;
	return (ret);
}

/*
 * KCF software provider context management entry points.
 */

/* ARGSUSED */
static int
sha1_create_ctx_template(crypto_provider_handle_t provider,
    crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t *ctx_template, size_t *ctx_template_size,
    crypto_req_handle_t req)
{
	sha1_hmac_ctx_t *sha1_hmac_ctx_tmpl;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);

	if ((mechanism->cm_type != SHA1_HMAC_MECH_INFO_TYPE) &&
	    (mechanism->cm_type != SHA1_HMAC_GEN_MECH_INFO_TYPE)) {
		return (CRYPTO_MECHANISM_INVALID);
	}

	/* Add support for key by attributes (RFE 4706552) */
	if (key->ck_format != CRYPTO_KEY_RAW)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Allocate and initialize SHA1 context.
	 */
	sha1_hmac_ctx_tmpl = kmem_alloc(sizeof (sha1_hmac_ctx_t),
	    crypto_kmflag(req));
	if (sha1_hmac_ctx_tmpl == NULL)
		return (CRYPTO_HOST_MEMORY);

	if (keylen_in_bytes > SHA1_HMAC_BLOCK_SIZE) {
		uchar_t digested_key[SHA1_DIGEST_LENGTH];

		/*
		 * Hash the passed-in key to get a smaller key.
		 * The inner context is used since it hasn't been
		 * initialized yet.
		 */
		PROV_SHA1_DIGEST_KEY(&sha1_hmac_ctx_tmpl->hc_icontext,
		    key->ck_data, keylen_in_bytes, digested_key);
		sha1_mac_init_ctx(sha1_hmac_ctx_tmpl, digested_key,
		    SHA1_DIGEST_LENGTH);
	} else {
		sha1_mac_init_ctx(sha1_hmac_ctx_tmpl, key->ck_data,
		    keylen_in_bytes);
	}

	sha1_hmac_ctx_tmpl->hc_mech_type = mechanism->cm_type;
	*ctx_template = (crypto_spi_ctx_template_t)sha1_hmac_ctx_tmpl;
	*ctx_template_size = sizeof (sha1_hmac_ctx_t);


	return (CRYPTO_SUCCESS);
}

static int
sha1_free_context(crypto_ctx_t *ctx)
{
	uint_t ctx_len;
	sha1_mech_type_t mech_type;

	if (ctx->cc_provider_private == NULL)
		return (CRYPTO_SUCCESS);

	/*
	 * We have to free either SHA1 or SHA1-HMAC contexts, which
	 * have different lengths.
	 */

	mech_type = PROV_SHA1_CTX(ctx)->sc_mech_type;
	if (mech_type == SHA1_MECH_INFO_TYPE)
		ctx_len = sizeof (sha1_ctx_t);
	else {
		ASSERT(mech_type == SHA1_HMAC_MECH_INFO_TYPE ||
		    mech_type == SHA1_HMAC_GEN_MECH_INFO_TYPE);
		ctx_len = sizeof (sha1_hmac_ctx_t);
	}

	bzero(ctx->cc_provider_private, ctx_len);
	kmem_free(ctx->cc_provider_private, ctx_len);
	ctx->cc_provider_private = NULL;

	return (CRYPTO_SUCCESS);
}
