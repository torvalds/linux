/* Public-key operation keyctls
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
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
	Opt_err = -1,
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

	ret = params->key->type->asym_query(params, &info);
	if (ret < 0)
		return ret;

	switch (op) {
	case KEYCTL_PKEY_ENCRYPT:
	case KEYCTL_PKEY_DECRYPT:
		if (uparams.in_len  > info.max_enc_size ||
		    uparams.out_len > info.max_dec_size)
			return -EINVAL;
		break;
	case KEYCTL_PKEY_SIGN:
	case KEYCTL_PKEY_VERIFY:
		if (uparams.in_len  > info.max_sig_size ||
		    uparams.out_len > info.max_data_size)
			return -EINVAL;
		break;
	default:
		BUG();
	}

	params->in_len  = uparams.in_len;
	params->out_len = uparams.out_len;
	return 0;
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

	memset(&params, 0, sizeof(params));

	ret = keyctl_pkey_params_get(id, _info, &params);
	if (ret < 0)
		goto error;

	ret = params.key->type->asym_query(&params, &res);
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

	ret = -EOPNOTSUPP;
	if (!params.key->type->asym_eds_op)
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
		BUG();
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

	ret = params.key->type->asym_eds_op(&params, in, out);
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
		goto error_params;

	ret = -EOPNOTSUPP;
	if (!params.key->type->asym_verify_signature)
		goto error_params;

	in = memdup_user(_in, params.in_len);
	if (IS_ERR(in)) {
		ret = PTR_ERR(in);
		goto error_params;
	}

	in2 = memdup_user(_in2, params.in2_len);
	if (IS_ERR(in2)) {
		ret = PTR_ERR(in2);
		goto error_in;
	}

	params.op = kernel_pkey_verify;
	ret = params.key->type->asym_verify_signature(&params, in, in2);

	kfree(in2);
error_in:
	kfree(in);
error_params:
	keyctl_pkey_params_free(&params);
	return ret;
}
