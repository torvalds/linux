// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Pengutronix, Ahmad Fatoum <kernel@pengutronix.de>
 * Copyright 2025 NXP
 */

#include <keys/trusted_caam.h>
#include <keys/trusted-type.h>
#include <linux/build_bug.h>
#include <linux/key-type.h>
#include <linux/parser.h>
#include <soc/fsl/caam-blob.h>

static struct caam_blob_priv *blobifier;

#define KEYMOD "SECURE_KEY"

static_assert(MAX_KEY_SIZE + CAAM_BLOB_OVERHEAD <= CAAM_BLOB_MAX_LEN);
static_assert(MAX_BLOB_SIZE <= CAAM_BLOB_MAX_LEN);

enum {
	opt_err,
	opt_key_enc_algo,
};

static const match_table_t key_tokens = {
	{opt_key_enc_algo, "key_enc_algo=%s"},
	{opt_err, NULL}
};

#ifdef CAAM_DEBUG
static inline void dump_options(const struct caam_pkey_info *pkey_info)
{
	pr_info("key encryption algo %d\n", pkey_info->key_enc_algo);
}
#else
static inline void dump_options(const struct caam_pkey_info *pkey_info)
{
}
#endif

static int get_pkey_options(char *c,
			    struct caam_pkey_info *pkey_info)
{
	substring_t args[MAX_OPT_ARGS];
	unsigned long token_mask = 0;
	u16 key_enc_algo;
	char *p = c;
	int token;
	int res;

	if (!c)
		return 0;

	while ((p = strsep(&c, " \t"))) {
		if (*p == '\0' || *p == ' ' || *p == '\t')
			continue;
		token = match_token(p, key_tokens, args);
		if (test_and_set_bit(token, &token_mask))
			return -EINVAL;

		switch (token) {
		case opt_key_enc_algo:
			res = kstrtou16(args[0].from, 16, &key_enc_algo);
			if (res < 0)
				return -EINVAL;
			pkey_info->key_enc_algo = key_enc_algo;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static bool is_key_pkey(char **datablob)
{
	char *c = NULL;

	do {
		/* Second argument onwards,
		 * determine if tied to HW
		 */
		c = strsep(datablob, " \t");
		if (c && (strcmp(c, "pk") == 0))
			return true;
	} while (c);

	return false;
}

static int trusted_caam_seal(struct trusted_key_payload *p, char *datablob)
{
	int ret;
	struct caam_blob_info info = {
		.input  = p->key,  .input_len   = p->key_len,
		.output = p->blob, .output_len  = MAX_BLOB_SIZE,
		.key_mod = KEYMOD, .key_mod_len = sizeof(KEYMOD) - 1,
	};

	/*
	 * If it is to be treated as protected key,
	 * read next arguments too.
	 */
	if (is_key_pkey(&datablob)) {
		info.pkey_info.plain_key_sz = p->key_len;
		info.pkey_info.is_pkey = 1;
		ret = get_pkey_options(datablob, &info.pkey_info);
		if (ret < 0)
			return 0;
		dump_options(&info.pkey_info);
	}

	ret = caam_encap_blob(blobifier, &info);
	if (ret)
		return ret;

	p->blob_len = info.output_len;
	if (info.pkey_info.is_pkey) {
		p->key_len = p->blob_len + sizeof(struct caam_pkey_info);
		memcpy(p->key, &info.pkey_info, sizeof(struct caam_pkey_info));
		memcpy(p->key + sizeof(struct caam_pkey_info), p->blob, p->blob_len);
	}

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

	if (is_key_pkey(&datablob)) {
		info.pkey_info.plain_key_sz = p->blob_len - CAAM_BLOB_OVERHEAD;
		info.pkey_info.is_pkey = 1;
		ret = get_pkey_options(datablob, &info.pkey_info);
		if (ret < 0)
			return 0;
		dump_options(&info.pkey_info);

		p->key_len = p->blob_len + sizeof(struct caam_pkey_info);
		memcpy(p->key, &info.pkey_info, sizeof(struct caam_pkey_info));
		memcpy(p->key + sizeof(struct caam_pkey_info), p->blob, p->blob_len);

		return 0;
	}

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
