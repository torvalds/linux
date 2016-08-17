/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 */

#include <sys/modctl.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#define	SKEIN_MODULE_IMPL
#include <sys/skein.h>

/*
 * Like the sha2 module, we create the skein module with two modlinkages:
 * - modlmisc to allow direct calls to Skein_* API functions.
 * - modlcrypto to integrate well into the Kernel Crypto Framework (KCF).
 */
static struct modlmisc modlmisc = {
	&mod_cryptoops,
	"Skein Message-Digest Algorithm"
};

static struct modlcrypto modlcrypto = {
	&mod_cryptoops,
	"Skein Kernel SW Provider"
};

static struct modlinkage modlinkage = {
	MODREV_1, {&modlmisc, &modlcrypto, NULL}
};

static crypto_mech_info_t skein_mech_info_tab[] = {
	{CKM_SKEIN_256, SKEIN_256_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, 0, CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{CKM_SKEIN_256_MAC, SKEIN_256_MAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC, 1, INT_MAX,
	    CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{CKM_SKEIN_512, SKEIN_512_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, 0, CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{CKM_SKEIN_512_MAC, SKEIN_512_MAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC, 1, INT_MAX,
	    CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{CKM_SKEIN1024, SKEIN1024_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, 0, CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{CKM_SKEIN1024_MAC, SKEIN1024_MAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC, 1, INT_MAX,
	    CRYPTO_KEYSIZE_UNIT_IN_BYTES}
};

static void skein_provider_status(crypto_provider_handle_t, uint_t *);

static crypto_control_ops_t skein_control_ops = {
	skein_provider_status
};

static int skein_digest_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_req_handle_t);
static int skein_digest(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int skein_update(crypto_ctx_t *, crypto_data_t *, crypto_req_handle_t);
static int skein_final(crypto_ctx_t *, crypto_data_t *, crypto_req_handle_t);
static int skein_digest_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);

static crypto_digest_ops_t skein_digest_ops = {
	.digest_init = skein_digest_init,
	.digest = skein_digest,
	.digest_update = skein_update,
	.digest_key = NULL,
	.digest_final = skein_final,
	.digest_atomic = skein_digest_atomic
};

static int skein_mac_init(crypto_ctx_t *, crypto_mechanism_t *, crypto_key_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);
static int skein_mac_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *, crypto_data_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);

static crypto_mac_ops_t skein_mac_ops = {
	.mac_init = skein_mac_init,
	.mac = NULL,
	.mac_update = skein_update, /* using regular digest update is OK here */
	.mac_final = skein_final,   /* using regular digest final is OK here */
	.mac_atomic = skein_mac_atomic,
	.mac_verify_atomic = NULL
};

static int skein_create_ctx_template(crypto_provider_handle_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_spi_ctx_template_t *,
    size_t *, crypto_req_handle_t);
static int skein_free_context(crypto_ctx_t *);

static crypto_ctx_ops_t skein_ctx_ops = {
	.create_ctx_template = skein_create_ctx_template,
	.free_context = skein_free_context
};

static crypto_ops_t skein_crypto_ops = {{{{{
	&skein_control_ops,
	&skein_digest_ops,
	NULL,
	&skein_mac_ops,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	&skein_ctx_ops,
}}}}};

static crypto_provider_info_t skein_prov_info = {{{{
	CRYPTO_SPI_VERSION_1,
	"Skein Software Provider",
	CRYPTO_SW_PROVIDER,
	NULL,
	&skein_crypto_ops,
	sizeof (skein_mech_info_tab) / sizeof (crypto_mech_info_t),
	skein_mech_info_tab
}}}};

static crypto_kcf_provider_handle_t skein_prov_handle = 0;

typedef struct skein_ctx {
	skein_mech_type_t		sc_mech_type;
	size_t				sc_digest_bitlen;
	/*LINTED(E_ANONYMOUS_UNION_DECL)*/
	union {
		Skein_256_Ctxt_t	sc_256;
		Skein_512_Ctxt_t	sc_512;
		Skein1024_Ctxt_t	sc_1024;
	};
} skein_ctx_t;
#define	SKEIN_CTX(_ctx_)	((skein_ctx_t *)((_ctx_)->cc_provider_private))
#define	SKEIN_CTX_LVALUE(_ctx_)	(_ctx_)->cc_provider_private
#define	SKEIN_OP(_skein_ctx, _op, ...)					\
	do {								\
		skein_ctx_t	*sc = (_skein_ctx);			\
		switch (sc->sc_mech_type) {				\
		case SKEIN_256_MECH_INFO_TYPE:				\
		case SKEIN_256_MAC_MECH_INFO_TYPE:			\
			(void) Skein_256_ ## _op(&sc->sc_256, __VA_ARGS__);\
			break;						\
		case SKEIN_512_MECH_INFO_TYPE:				\
		case SKEIN_512_MAC_MECH_INFO_TYPE:			\
			(void) Skein_512_ ## _op(&sc->sc_512, __VA_ARGS__);\
			break;						\
		case SKEIN1024_MECH_INFO_TYPE:				\
		case SKEIN1024_MAC_MECH_INFO_TYPE:			\
			(void) Skein1024_ ## _op(&sc->sc_1024, __VA_ARGS__);\
			break;						\
		}							\
		_NOTE(CONSTCOND)					\
	} while (0)

static int
skein_get_digest_bitlen(const crypto_mechanism_t *mechanism, size_t *result)
{
	if (mechanism->cm_param != NULL) {
		/*LINTED(E_BAD_PTR_CAST_ALIGN)*/
		skein_param_t	*param = (skein_param_t *)mechanism->cm_param;

		if (mechanism->cm_param_len != sizeof (*param) ||
		    param->sp_digest_bitlen == 0) {
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		*result = param->sp_digest_bitlen;
	} else {
		switch (mechanism->cm_type) {
		case SKEIN_256_MECH_INFO_TYPE:
			*result = 256;
			break;
		case SKEIN_512_MECH_INFO_TYPE:
			*result = 512;
			break;
		case SKEIN1024_MECH_INFO_TYPE:
			*result = 1024;
			break;
		default:
			return (CRYPTO_MECHANISM_INVALID);
		}
	}
	return (CRYPTO_SUCCESS);
}

int
skein_mod_init(void)
{
	int error;

	if ((error = mod_install(&modlinkage)) != 0)
		return (error);

	/*
	 * Try to register with KCF - failure shouldn't unload us, since we
	 * still may want to continue providing misc/skein functionality.
	 */
	(void) crypto_register_provider(&skein_prov_info, &skein_prov_handle);

	return (0);
}

int
skein_mod_fini(void)
{
	int ret;

	if (skein_prov_handle != 0) {
		if ((ret = crypto_unregister_provider(skein_prov_handle)) !=
		    CRYPTO_SUCCESS) {
			cmn_err(CE_WARN,
			    "skein _fini: crypto_unregister_provider() "
			    "failed (0x%x)", ret);
			return (EBUSY);
		}
		skein_prov_handle = 0;
	}

	return (mod_remove(&modlinkage));
}

/*
 * KCF software provider control entry points.
 */
/* ARGSUSED */
static void
skein_provider_status(crypto_provider_handle_t provider, uint_t *status)
{
	*status = CRYPTO_PROVIDER_READY;
}

/*
 * General Skein hashing helper functions.
 */

/*
 * Performs an Update on a context with uio input data.
 */
static int
skein_digest_update_uio(skein_ctx_t *ctx, const crypto_data_t *data)
{
	off_t		offset = data->cd_offset;
	size_t		length = data->cd_length;
	uint_t		vec_idx;
	size_t		cur_len;
	const uio_t	*uio = data->cd_uio;

	/* we support only kernel buffer */
	if (uio->uio_segflg != UIO_SYSSPACE)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Jump to the first iovec containing data to be
	 * digested.
	 */
	for (vec_idx = 0; vec_idx < uio->uio_iovcnt &&
	    offset >= uio->uio_iov[vec_idx].iov_len;
	    offset -= uio->uio_iov[vec_idx++].iov_len)
		;
	if (vec_idx == uio->uio_iovcnt) {
		/*
		 * The caller specified an offset that is larger than the
		 * total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * Now do the digesting on the iovecs.
	 */
	while (vec_idx < uio->uio_iovcnt && length > 0) {
		cur_len = MIN(uio->uio_iov[vec_idx].iov_len - offset, length);
		SKEIN_OP(ctx, Update, (uint8_t *)uio->uio_iov[vec_idx].iov_base
		    + offset, cur_len);
		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == uio->uio_iovcnt && length > 0) {
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
 * Performs a Final on a context and writes to a uio digest output.
 */
static int
skein_digest_final_uio(skein_ctx_t *ctx, crypto_data_t *digest,
    crypto_req_handle_t req)
{
	off_t	offset = digest->cd_offset;
	uint_t	vec_idx;
	uio_t	*uio = digest->cd_uio;

	/* we support only kernel buffer */
	if (uio->uio_segflg != UIO_SYSSPACE)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Jump to the first iovec containing ptr to the digest to be returned.
	 */
	for (vec_idx = 0; offset >= uio->uio_iov[vec_idx].iov_len &&
	    vec_idx < uio->uio_iovcnt;
	    offset -= uio->uio_iov[vec_idx++].iov_len)
		;
	if (vec_idx == uio->uio_iovcnt) {
		/*
		 * The caller specified an offset that is larger than the
		 * total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}
	if (offset + CRYPTO_BITS2BYTES(ctx->sc_digest_bitlen) <=
	    uio->uio_iov[vec_idx].iov_len) {
		/* The computed digest will fit in the current iovec. */
		SKEIN_OP(ctx, Final,
		    (uchar_t *)uio->uio_iov[vec_idx].iov_base + offset);
	} else {
		uint8_t *digest_tmp;
		off_t scratch_offset = 0;
		size_t length = CRYPTO_BITS2BYTES(ctx->sc_digest_bitlen);
		size_t cur_len;

		digest_tmp = kmem_alloc(CRYPTO_BITS2BYTES(
		    ctx->sc_digest_bitlen), crypto_kmflag(req));
		if (digest_tmp == NULL)
			return (CRYPTO_HOST_MEMORY);
		SKEIN_OP(ctx, Final, digest_tmp);
		while (vec_idx < uio->uio_iovcnt && length > 0) {
			cur_len = MIN(uio->uio_iov[vec_idx].iov_len - offset,
			    length);
			bcopy(digest_tmp + scratch_offset,
			    uio->uio_iov[vec_idx].iov_base + offset, cur_len);

			length -= cur_len;
			vec_idx++;
			scratch_offset += cur_len;
			offset = 0;
		}
		kmem_free(digest_tmp, CRYPTO_BITS2BYTES(ctx->sc_digest_bitlen));

		if (vec_idx == uio->uio_iovcnt && length > 0) {
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

/*
 * KCF software provider digest entry points.
 */

/*
 * Initializes a skein digest context to the configuration in `mechanism'.
 * The mechanism cm_type must be one of SKEIN_*_MECH_INFO_TYPE. The cm_param
 * field may contain a skein_param_t structure indicating the length of the
 * digest the algorithm should produce. Otherwise the default output lengths
 * are applied (32 bytes for Skein-256, 64 bytes for Skein-512 and 128 bytes
 * for Skein-1024).
 */
static int
skein_digest_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_req_handle_t req)
{
	int	error = CRYPTO_SUCCESS;

	if (!VALID_SKEIN_DIGEST_MECH(mechanism->cm_type))
		return (CRYPTO_MECHANISM_INVALID);

	SKEIN_CTX_LVALUE(ctx) = kmem_alloc(sizeof (*SKEIN_CTX(ctx)),
	    crypto_kmflag(req));
	if (SKEIN_CTX(ctx) == NULL)
		return (CRYPTO_HOST_MEMORY);

	SKEIN_CTX(ctx)->sc_mech_type = mechanism->cm_type;
	error = skein_get_digest_bitlen(mechanism,
	    &SKEIN_CTX(ctx)->sc_digest_bitlen);
	if (error != CRYPTO_SUCCESS)
		goto errout;
	SKEIN_OP(SKEIN_CTX(ctx), Init, SKEIN_CTX(ctx)->sc_digest_bitlen);

	return (CRYPTO_SUCCESS);
errout:
	bzero(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
	kmem_free(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
	SKEIN_CTX_LVALUE(ctx) = NULL;
	return (error);
}

/*
 * Executes a skein_update and skein_digest on a pre-initialized crypto
 * context in a single step. See the documentation to these functions to
 * see what to pass here.
 */
static int
skein_digest(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *digest,
    crypto_req_handle_t req)
{
	int error = CRYPTO_SUCCESS;

	ASSERT(SKEIN_CTX(ctx) != NULL);

	if (digest->cd_length <
	    CRYPTO_BITS2BYTES(SKEIN_CTX(ctx)->sc_digest_bitlen)) {
		digest->cd_length =
		    CRYPTO_BITS2BYTES(SKEIN_CTX(ctx)->sc_digest_bitlen);
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	error = skein_update(ctx, data, req);
	if (error != CRYPTO_SUCCESS) {
		bzero(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
		kmem_free(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
		SKEIN_CTX_LVALUE(ctx) = NULL;
		digest->cd_length = 0;
		return (error);
	}
	error = skein_final(ctx, digest, req);

	return (error);
}

/*
 * Performs a skein Update with the input message in `data' (successive calls
 * can push more data). This is used both for digest and MAC operation.
 * Supported input data formats are raw, uio and mblk.
 */
/*ARGSUSED*/
static int
skein_update(crypto_ctx_t *ctx, crypto_data_t *data, crypto_req_handle_t req)
{
	int error = CRYPTO_SUCCESS;

	ASSERT(SKEIN_CTX(ctx) != NULL);

	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SKEIN_OP(SKEIN_CTX(ctx), Update,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		error = skein_digest_update_uio(SKEIN_CTX(ctx), data);
		break;
	default:
		error = CRYPTO_ARGUMENTS_BAD;
	}

	return (error);
}

/*
 * Performs a skein Final, writing the output to `digest'. This is used both
 * for digest and MAC operation.
 * Supported output digest formats are raw, uio and mblk.
 */
/*ARGSUSED*/
static int
skein_final(crypto_ctx_t *ctx, crypto_data_t *digest, crypto_req_handle_t req)
{
	int error = CRYPTO_SUCCESS;

	ASSERT(SKEIN_CTX(ctx) != NULL);

	if (digest->cd_length <
	    CRYPTO_BITS2BYTES(SKEIN_CTX(ctx)->sc_digest_bitlen)) {
		digest->cd_length =
		    CRYPTO_BITS2BYTES(SKEIN_CTX(ctx)->sc_digest_bitlen);
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	switch (digest->cd_format) {
	case CRYPTO_DATA_RAW:
		SKEIN_OP(SKEIN_CTX(ctx), Final,
		    (uint8_t *)digest->cd_raw.iov_base + digest->cd_offset);
		break;
	case CRYPTO_DATA_UIO:
		error = skein_digest_final_uio(SKEIN_CTX(ctx), digest, req);
		break;
	default:
		error = CRYPTO_ARGUMENTS_BAD;
	}

	if (error == CRYPTO_SUCCESS)
		digest->cd_length =
		    CRYPTO_BITS2BYTES(SKEIN_CTX(ctx)->sc_digest_bitlen);
	else
		digest->cd_length = 0;

	bzero(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
	kmem_free(SKEIN_CTX(ctx), sizeof (*(SKEIN_CTX(ctx))));
	SKEIN_CTX_LVALUE(ctx) = NULL;

	return (error);
}

/*
 * Performs a full skein digest computation in a single call, configuring the
 * algorithm according to `mechanism', reading the input to be digested from
 * `data' and writing the output to `digest'.
 * Supported input/output formats are raw, uio and mblk.
 */
/*ARGSUSED*/
static int
skein_digest_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_data_t *data, crypto_data_t *digest, crypto_req_handle_t req)
{
	int		error;
	skein_ctx_t	skein_ctx;
	crypto_ctx_t	ctx;
	SKEIN_CTX_LVALUE(&ctx) = &skein_ctx;

	/* Init */
	if (!VALID_SKEIN_DIGEST_MECH(mechanism->cm_type))
		return (CRYPTO_MECHANISM_INVALID);
	skein_ctx.sc_mech_type = mechanism->cm_type;
	error = skein_get_digest_bitlen(mechanism, &skein_ctx.sc_digest_bitlen);
	if (error != CRYPTO_SUCCESS)
		goto out;
	SKEIN_OP(&skein_ctx, Init, skein_ctx.sc_digest_bitlen);

	if ((error = skein_update(&ctx, data, digest)) != CRYPTO_SUCCESS)
		goto out;
	if ((error = skein_final(&ctx, data, digest)) != CRYPTO_SUCCESS)
		goto out;

out:
	if (error == CRYPTO_SUCCESS)
		digest->cd_length =
		    CRYPTO_BITS2BYTES(skein_ctx.sc_digest_bitlen);
	else
		digest->cd_length = 0;
	bzero(&skein_ctx, sizeof (skein_ctx));

	return (error);
}

/*
 * Helper function that builds a Skein MAC context from the provided
 * mechanism and key.
 */
static int
skein_mac_ctx_build(skein_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key)
{
	int error;

	if (!VALID_SKEIN_MAC_MECH(mechanism->cm_type))
		return (CRYPTO_MECHANISM_INVALID);
	if (key->ck_format != CRYPTO_KEY_RAW)
		return (CRYPTO_ARGUMENTS_BAD);
	ctx->sc_mech_type = mechanism->cm_type;
	error = skein_get_digest_bitlen(mechanism, &ctx->sc_digest_bitlen);
	if (error != CRYPTO_SUCCESS)
		return (error);
	SKEIN_OP(ctx, InitExt, ctx->sc_digest_bitlen, 0, key->ck_data,
	    CRYPTO_BITS2BYTES(key->ck_length));

	return (CRYPTO_SUCCESS);
}

/*
 * KCF software provide mac entry points.
 */
/*
 * Initializes a skein MAC context. You may pass a ctx_template, in which
 * case the template will be reused to make initialization more efficient.
 * Otherwise a new context will be constructed. The mechanism cm_type must
 * be one of SKEIN_*_MAC_MECH_INFO_TYPE. Same as in skein_digest_init, you
 * may pass a skein_param_t in cm_param to configure the length of the
 * digest. The key must be in raw format.
 */
static int
skein_mac_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int	error;

	SKEIN_CTX_LVALUE(ctx) = kmem_alloc(sizeof (*SKEIN_CTX(ctx)),
	    crypto_kmflag(req));
	if (SKEIN_CTX(ctx) == NULL)
		return (CRYPTO_HOST_MEMORY);

	if (ctx_template != NULL) {
		bcopy(ctx_template, SKEIN_CTX(ctx),
		    sizeof (*SKEIN_CTX(ctx)));
	} else {
		error = skein_mac_ctx_build(SKEIN_CTX(ctx), mechanism, key);
		if (error != CRYPTO_SUCCESS)
			goto errout;
	}

	return (CRYPTO_SUCCESS);
errout:
	bzero(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
	kmem_free(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
	return (error);
}

/*
 * The MAC update and final calls are reused from the regular digest code.
 */

/*ARGSUSED*/
/*
 * Same as skein_digest_atomic, performs an atomic Skein MAC operation in
 * one step. All the same properties apply to the arguments of this
 * function as to those of the partial operations above.
 */
static int
skein_mac_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	/* faux crypto context just for skein_digest_{update,final} */
	int		error;
	crypto_ctx_t	ctx;
	skein_ctx_t	skein_ctx;
	SKEIN_CTX_LVALUE(&ctx) = &skein_ctx;

	if (ctx_template != NULL) {
		bcopy(ctx_template, &skein_ctx, sizeof (skein_ctx));
	} else {
		error = skein_mac_ctx_build(&skein_ctx, mechanism, key);
		if (error != CRYPTO_SUCCESS)
			goto errout;
	}

	if ((error = skein_update(&ctx, data, req)) != CRYPTO_SUCCESS)
		goto errout;
	if ((error = skein_final(&ctx, mac, req)) != CRYPTO_SUCCESS)
		goto errout;

	return (CRYPTO_SUCCESS);
errout:
	bzero(&skein_ctx, sizeof (skein_ctx));
	return (error);
}

/*
 * KCF software provider context management entry points.
 */

/*
 * Constructs a context template for the Skein MAC algorithm. The same
 * properties apply to the arguments of this function as to those of
 * skein_mac_init.
 */
/*ARGSUSED*/
static int
skein_create_ctx_template(crypto_provider_handle_t provider,
    crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t *ctx_template, size_t *ctx_template_size,
    crypto_req_handle_t req)
{
	int		error;
	skein_ctx_t	*ctx_tmpl;

	ctx_tmpl = kmem_alloc(sizeof (*ctx_tmpl), crypto_kmflag(req));
	if (ctx_tmpl == NULL)
		return (CRYPTO_HOST_MEMORY);
	error = skein_mac_ctx_build(ctx_tmpl, mechanism, key);
	if (error != CRYPTO_SUCCESS)
		goto errout;
	*ctx_template = ctx_tmpl;
	*ctx_template_size = sizeof (*ctx_tmpl);

	return (CRYPTO_SUCCESS);
errout:
	bzero(ctx_tmpl, sizeof (*ctx_tmpl));
	kmem_free(ctx_tmpl, sizeof (*ctx_tmpl));
	return (error);
}

/*
 * Frees a skein context in a parent crypto context.
 */
static int
skein_free_context(crypto_ctx_t *ctx)
{
	if (SKEIN_CTX(ctx) != NULL) {
		bzero(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
		kmem_free(SKEIN_CTX(ctx), sizeof (*SKEIN_CTX(ctx)));
		SKEIN_CTX_LVALUE(ctx) = NULL;
	}

	return (CRYPTO_SUCCESS);
}
