// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Pengutronix, Ahmad Fatoum <kernel@pengutronix.de>
 */

#include <keys/trusted_caam.h>
#include <keys/trusted-type.h>
#include <linux/build_bug.h>
#include <linux/key-type.h>
#include <soc/fsl/caam-blob.h>

static struct caam_blob_priv *blobifier;

#define KEYMOD "SECURE_KEY"

static_assert(MAX_KEY_SIZE + CAAM_BLOB_OVERHEAD <= CAAM_BLOB_MAX_LEN);
static_assert(MAX_BLOB_SIZE <= CAAM_BLOB_MAX_LEN);

static int trusted_caam_seal(struct trusted_key_payload *p, char *datablob)
{
	int ret;
	struct caam_blob_info info = {
		.input  = p->key,  .input_len   = p->key_len,
		.output = p->blob, .output_len  = MAX_BLOB_SIZE,
		.key_mod = KEYMOD, .key_mod_len = sizeof(KEYMOD) - 1,
	};

	ret = caam_encap_blob(blobifier, &info);
	if (ret)
		return ret;

	p->blob_len = info.output_len;
	return 0;
}

static int trusted_caam_unseal(struct trusted_key_payload *p, char *datablob)
{
	int ret;
	struct caam_blob_info info = {
		.input   = p->blob,  .input_len  = p->blob_len,
		.output  = p->key,   .output_len = MAX_KEY_SIZE,
		.key_mod = KEYMOD,  .key_mod_len = sizeof(KEYMOD) - 1,
	};

	ret = caam_decap_blob(blobifier, &info);
	if (ret)
		return ret;

	p->key_len = info.output_len;
	return 0;
}

static int trusted_caam_init(void)
{
	int ret;

	blobifier = caam_blob_gen_init();
	if (IS_ERR(blobifier))
		return PTR_ERR(blobifier);

	ret = register_key_type(&key_type_trusted);
	if (ret)
		caam_blob_gen_exit(blobifier);

	return ret;
}

static void trusted_caam_exit(void)
{
	unregister_key_type(&key_type_trusted);
	caam_blob_gen_exit(blobifier);
}

struct trusted_key_ops trusted_key_caam_ops = {
	.migratable = 0, /* non-migratable */
	.init = trusted_caam_init,
	.seal = trusted_caam_seal,
	.unseal = trusted_caam_unseal,
	.exit = trusted_caam_exit,
};
