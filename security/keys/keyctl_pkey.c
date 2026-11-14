// SPDX-License-Identifier: GPL-2.0-or-later
/* Public-key operation keyctls
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/parser.h>
#include <linux/uaccess.h>
#include <keys/user-type.h>
#include "internal.h"

static void keyctl_pkey_params_free(struct kernel_pkey_params *params)
{
	kfree(params->info);
	key_put(params->key);
}

enum {
	Opt_err,
	Opt_enc,		/* "enc=<encoding>" eg. "enc=oaep" */
	Opt_hash,		/* "hash=<digest-name>" eg. "hash=sha1" */
};

static const match_table_t param_keys = {
	{ Opt_enc,	"enc=%s" },
	{ Opt_hash,	"hash=%s" },
	{ Opt_err,	NULL }
};

/*
 * Parse the information string which consists of key=val pairs.
 */
static int keyctl_pkey_params_parse(struct kernel_pkey_params *params)
{
	unsigned long token_mask = 0;
	substring_t args[MAX_OPT_ARGS];
	char *c = params->info, *p, *q;
	int token;

	while ((p = strsep(&c, " \t"))) {
		if (*p == '\0' || *p == ' ' || *p == '\t')
			continue;
		token = match_token(p, param_keys, args);
		if (token == Opt_err)
			return -EINVAL;
		if (__test_and_set_bit(token, &token_mask))
			return -EINVAL;
		q = args[0].from;
		if (!q[0])
			return -EINVAL;

		switch (token) {
		case Opt_enc:
			params->encoding = q;
			break;

		case Opt_hash:
			params->hash_algo = q;
			break;

		default:
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Interpret parameters.  Callers must always call the free function
 * on params, even if an error is returned.
 */
static int keyctl_pkey_params_get(key_serial_t id,
				  const char __user *_info,
				  struct kernel_pkey_params *params)
{
	key_ref_t key_ref;
	void *p;
	int ret;

	memset(params, 0, sizeof(*params));
	params->encoding = "raw";

	p = strndup_user(_info, PAGE_SIZE);
	if (IS_ERR(p))
		return PTR_ERR(p);
	params->info = p;

	ret = keyctl_pkey_params_parse(params);
	if (ret < 0)
		return ret;

	key_ref = lookup_user_key(id, 0, KEY_NEED_SEARCH);
	if (IS_ERR(key_ref))
		return PTR_ERR(key_ref);
	params->key = key_ref_to_ptr(key_ref);

	if (!params->key->type->asym_query)
		return -EOPNOTSUPP;

	return 0;
}

/*
 * Get parameters from userspace.  Callers must always call the free function
 * on params, even if an error is returned.
 */
static int keyctl_pkey_params_get_2(const struct keyctl_pkey_params __user *_params,
				    const char __user *_info,
				    int op,
				    struct kernel_pkey_params *params)
{
	struct keyctl_pkey_params uparams;
	struct kernel_pkey_query info;
	int ret;

	memset(params, 0, sizeof(*params));
	params->encoding = "raw";

	if (copy_from_user(&uparams, _params, sizeof(uparams)) != 0)
		return -EFAULT;

	ret = keyctl_pkey_params_get(uparams.key_id, _info, params);
	if (ret < 0)
		return ret;

	down_read(&params->key->sem);

	if (!params->key->type->asym_query) {
		ret = -EOPNOTSUPP;
		goto out_up_key_sem;
	}

	ret = params->key->type->asym_query(params, &info);
	if (ret < 0)
		goto out_up_key_sem;

	switch (op) {
	case KEYCTL_PKEY_ENCRYPT:
		if (uparams.in_len  > info.max_dec_size ||
		    uparams.out_len > info.max_enc_size) {
			ret = -EINVAL;
			goto out_up_key_sem;
		}

		params->out_len = info.max_enc_size;
		break;
	case KEYCTL_PKEY_DECRYPT:
		if (uparams.in_len  > info.max_enc_size ||
		    uparams.out_len > info.max_dec_size) {
			ret = -EINVAL;
			goto out_up_key_sem;
		}

		params->out_len = info.max_dec_size;
		break;
	case KEYCTL_PKEY_SIGN:
		if (uparams.in_len  > info.max_data_size ||
		    uparams.out_len > info.max_sig_size) {
			ret = -EINVAL;
			goto out_up_key_sem;
		}

		params->out_len = info.max_sig_size;
		break;
	case KEYCTL_PKEY_VERIFY:
		if (uparams.in_len  > info.max_data_size ||
		    uparams.in2_len > info.max_sig_size) {
			ret = -EINVAL;
			goto out_up_key_sem;
		}

		params->out_len = info.max_sig_size;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto out_up_key_sem;
	}

	params->in_len  = uparams.in_len;

	switch (op) {
	case KEYCTL_PKEY_VERIFY:
		if (!params->key->type->asym_verify_signature)
			ret = -EOPNOTSUPP;
		break;
	case KEYCTL_PKEY_ENCRYPT:
	case KEYCTL_PKEY_DECRYPT:
	case KEYCTL_PKEY_SIGN:
		if (!params->key->type->asym_eds_op)
			ret = -EOPNOTSUPP;
		break;
	}

out_up_key_sem:
	up_read(&params->key->sem);
	return ret;
}

/*
 * Query information about an asymmetric key.
 */
long keyctl_pkey_query(key_serial_t id,
		       const char __user *_info,
		       struct keyctl_pkey_query __user *_res)
{
	struct kernel_pkey_params params;
	struct kernel_pkey_query res;
	long ret;

	ret = keyctl_pkey_params_get(id, _info, &params);
	if (ret < 0)
		goto error;

	down_read(&params.key->sem);
	if (!params.key->type->asym_query) {
		ret = -EOPNOTSUPP;
		goto out_up_key_sem;
	}

	ret = params.key->type->asym_query(&params, &res);

out_up_key_sem:
	up_read(&params.key->sem);
	if (ret < 0)
		goto error;

	ret = -EFAULT;
	if (copy_to_user(_res, &res, sizeof(res)) == 0 &&
	    clear_user(_res->__spare, sizeof(_res->__spare)) == 0)
		ret = 0;

error:
	keyctl_pkey_params_free(&params);
	return ret;
}

/*
 * Encrypt/decrypt/sign
 *
 * Encrypt data, decrypt data or sign data using a public key.
 *
 * _info is a string of supplementary information in key=val format.  For
 * instance, it might contain:
 *
 *	"enc=pkcs1 hash=sha256"
 *
 * where enc= specifies the encoding and hash= selects the OID to go in that
 * particular encoding if required.  If enc= isn't supplied, it's assumed that
 * the caller is supplying raw values.
 *
 * If successful, the amount of data written into the output buffer is
 * returned.
 */
long keyctl_pkey_e_d_s(int op,
		       const struct keyctl_pkey_params __user *_params,
		       const char __user *_info,
		       const void __user *_in,
		       void __user *_out)
{
	struct kernel_pkey_params params;
	void *in, *out;
	long ret;

	ret = keyctl_pkey_params_get_2(_params, _info, op, &params);
	if (ret < 0)
		goto error_params;

	switch (op) {
	case KEYCTL_PKEY_ENCRYPT:
		params.op = kernel_pkey_encrypt;
		break;
	case KEYCTL_PKEY_DECRYPT:
		params.op = kernel_pkey_decrypt;
		break;
	case KEYCTL_PKEY_SIGN:
		params.op = kernel_pkey_sign;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto error_params;
	}

	in = memdup_user(_in, params.in_len);
	if (IS_ERR(in)) {
		ret = PTR_ERR(in);
		goto error_params;
	}

	ret = -ENOMEM;
	out = kmalloc(params.out_len, GFP_KERNEL);
	if (!out)
		goto error_in;

	down_read(&params.key->sem);
	if (!params.key->type->asym_eds_op) {
		ret = -EOPNOTSUPP;
		goto out_up_key_sem;
	}

	ret = params.key->type->asym_eds_op(&params, in, out);

out_up_key_sem:
	up_read(&params.key->sem);
	if (ret < 0)
		goto error_out;

	if (copy_to_user(_out, out, ret) != 0)
		ret = -EFAULT;

error_out:
	kfree(out);
error_in:
	kfree(in);
error_params:
	keyctl_pkey_params_free(&params);
	return ret;
}

/*
 * Verify a signature.
 *
 * Verify a public key signature using the given key, or if not given, search
 * for a matching key.
 *
 * _info is a string of supplementary information in key=val format.  For
 * instance, it might contain:
 *
 *	"enc=pkcs1 hash=sha256"
 *
 * where enc= specifies the signature blob encoding and hash= selects the OID
 * to go in that particular encoding.  If enc= isn't supplied, it's assumed
 * that the caller is supplying raw values.
 *
 * If successful, 0 is returned.
 */
long keyctl_pkey_verify(const struct keyctl_pkey_params __user *_params,
			const char __user *_info,
			const void __user *_in,
			const void __user *_in2)
{
	struct kernel_pkey_params params;
	void *in, *in2;
	long ret;

	ret = keyctl_pkey_params_get_2(_params, _info, KEYCTL_PKEY_VERIFY,
				       &params);
	if (ret < 0)
		goto out_free_params;

	in = memdup_user(_in, params.in_len);
	if (IS_ERR(in)) {
		ret = PTR_ERR(in);
		goto out_free_params;
	}

	in2 = memdup_user(_in2, params.in2_len);
	if (IS_ERR(in2)) {
		ret = PTR_ERR(in2);
		goto out_free_in;
	}

	params.op = kernel_pkey_verify;
	down_read(&params.key->sem);
	if (!params.key->type->asym_verify_signature) {
		ret = -EOPNOTSUPP;
		goto out_up_key_sem;
	}

	ret = params.key->type->asym_verify_signature(&params, in, in2);

out_up_key_sem:
	up_read(&params.key->sem);
	kfree(in2);

out_free_in:
	kfree(in);

out_free_params:
	keyctl_pkey_params_free(&params);
	return ret;
}
