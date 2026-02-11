// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 IBM Corporation, Srish Srinivasan <ssrish@linux.ibm.com>
 */

#include <keys/trusted_pkwm.h>
#include <keys/trusted-type.h>
#include <linux/build_bug.h>
#include <linux/key-type.h>
#include <linux/parser.h>
#include <asm/plpks.h>

enum {
	Opt_err,
	Opt_wrap_flags,
};

static const match_table_t key_tokens = {
	{Opt_wrap_flags, "wrap_flags=%s"},
	{Opt_err, NULL}
};

static int getoptions(char *datablob, struct trusted_key_options *opt)
{
	substring_t args[MAX_OPT_ARGS];
	char *p = datablob;
	int token;
	int res;
	u16 wrap_flags;
	unsigned long token_mask = 0;
	struct trusted_pkwm_options *pkwm;

	if (!datablob)
		return 0;

	pkwm = opt->private;

	while ((p = strsep(&datablob, " \t"))) {
		if (*p == '\0' || *p == ' ' || *p == '\t')
			continue;

		token = match_token(p, key_tokens, args);
		if (test_and_set_bit(token, &token_mask))
			return -EINVAL;

		switch (token) {
		case Opt_wrap_flags:
			res = kstrtou16(args[0].from, 16, &wrap_flags);
			if (res < 0 || wrap_flags > 2)
				return -EINVAL;
			pkwm->wrap_flags = wrap_flags;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static struct trusted_key_options *trusted_options_alloc(void)
{
	struct trusted_key_options *options;
	struct trusted_pkwm_options *pkwm;

	options = kzalloc(sizeof(*options), GFP_KERNEL);

	if (options) {
		pkwm = kzalloc(sizeof(*pkwm), GFP_KERNEL);

		if (!pkwm) {
			kfree_sensitive(options);
			options = NULL;
		} else {
			options->private = pkwm;
		}
	}

	return options;
}

static int trusted_pkwm_seal(struct trusted_key_payload *p, char *datablob)
{
	struct trusted_key_options *options = NULL;
	struct trusted_pkwm_options *pkwm = NULL;
	u8 *input_buf, *output_buf;
	u32 output_len, input_len;
	int rc;

	options = trusted_options_alloc();

	if (!options)
		return -ENOMEM;

	rc = getoptions(datablob, options);
	if (rc < 0)
		goto out;
	dump_options(options);

	input_len = p->key_len;
	input_buf = kmalloc(ALIGN(input_len, 4096), GFP_KERNEL);
	if (!input_buf) {
		pr_err("Input buffer allocation failed. Returning -ENOMEM.");
		rc = -ENOMEM;
		goto out;
	}

	memcpy(input_buf, p->key, p->key_len);

	pkwm = options->private;

	rc = plpks_wrap_object(&input_buf, input_len, pkwm->wrap_flags,
			       &output_buf, &output_len);
	if (!rc) {
		memcpy(p->blob, output_buf, output_len);
		p->blob_len = output_len;
		dump_payload(p);
	} else {
		pr_err("Wrapping of payload key failed: %d\n", rc);
	}

	kfree(input_buf);
	kfree(output_buf);

out:
	kfree_sensitive(options->private);
	kfree_sensitive(options);
	return rc;
}

static int trusted_pkwm_unseal(struct trusted_key_payload *p, char *datablob)
{
	u8 *input_buf, *output_buf;
	u32 input_len, output_len;
	int rc;

	input_len = p->blob_len;
	input_buf = kmalloc(ALIGN(input_len, 4096), GFP_KERNEL);
	if (!input_buf) {
		pr_err("Input buffer allocation failed. Returning -ENOMEM.");
		return -ENOMEM;
	}

	memcpy(input_buf, p->blob, p->blob_len);

	rc = plpks_unwrap_object(&input_buf, input_len, &output_buf,
				 &output_len);
	if (!rc) {
		memcpy(p->key, output_buf, output_len);
		p->key_len = output_len;
		dump_payload(p);
	} else {
		pr_err("Unwrapping of payload failed: %d\n", rc);
	}

	kfree(input_buf);
	kfree(output_buf);

	return rc;
}

static int trusted_pkwm_init(void)
{
	int ret;

	if (!plpks_wrapping_is_supported()) {
		pr_err("H_PKS_WRAP_OBJECT interface not supported\n");
		return -ENODEV;
	}

	ret = plpks_gen_wrapping_key();
	if (ret) {
		pr_err("Failed to generate default wrapping key\n");
		return -EINVAL;
	}

	return register_key_type(&key_type_trusted);
}

static void trusted_pkwm_exit(void)
{
	unregister_key_type(&key_type_trusted);
}

struct trusted_key_ops pkwm_trusted_key_ops = {
	.migratable = 0, /* non-migratable */
	.init = trusted_pkwm_init,
	.seal = trusted_pkwm_seal,
	.unseal = trusted_pkwm_unseal,
	.exit = trusted_pkwm_exit,
};
