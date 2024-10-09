// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include "digest.h"

/**
 * ipe_digest_parse() - parse a digest in IPE's policy.
 * @valstr: Supplies the string parsed from the policy.
 *
 * Digests in IPE are defined in a standard way:
 *	<alg_name>:<hex>
 *
 * Use this function to create a property to parse the digest
 * consistently. The parsed digest will be saved in @value in IPE's
 * policy.
 *
 * Return: The parsed digest_info structure on success. If an error occurs,
 * the function will return the error value (via ERR_PTR).
 */
struct digest_info *ipe_digest_parse(const char *valstr)
{
	struct digest_info *info = NULL;
	char *sep, *raw_digest;
	size_t raw_digest_len;
	u8 *digest = NULL;
	char *alg = NULL;
	int rc = 0;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	sep = strchr(valstr, ':');
	if (!sep) {
		rc = -EBADMSG;
		goto err;
	}

	alg = kstrndup(valstr, sep - valstr, GFP_KERNEL);
	if (!alg) {
		rc = -ENOMEM;
		goto err;
	}

	raw_digest = sep + 1;
	raw_digest_len = strlen(raw_digest);

	info->digest_len = (raw_digest_len + 1) / 2;
	digest = kzalloc(info->digest_len, GFP_KERNEL);
	if (!digest) {
		rc = -ENOMEM;
		goto err;
	}

	rc = hex2bin(digest, raw_digest, info->digest_len);
	if (rc < 0) {
		rc = -EINVAL;
		goto err;
	}

	info->alg = alg;
	info->digest = digest;
	return info;

err:
	kfree(alg);
	kfree(digest);
	kfree(info);
	return ERR_PTR(rc);
}

/**
 * ipe_digest_eval() - evaluate an IPE digest against another digest.
 * @expected: Supplies the policy-provided digest value.
 * @digest: Supplies the digest to compare against the policy digest value.
 *
 * Return:
 * * %true	- digests match
 * * %false	- digests do not match
 */
bool ipe_digest_eval(const struct digest_info *expected,
		     const struct digest_info *digest)
{
	return (expected->digest_len == digest->digest_len) &&
	       (!strcmp(expected->alg, digest->alg)) &&
	       (!memcmp(expected->digest, digest->digest, expected->digest_len));
}

/**
 * ipe_digest_free() - free an IPE digest.
 * @info: Supplies a pointer the policy-provided digest to free.
 */
void ipe_digest_free(struct digest_info *info)
{
	if (IS_ERR_OR_NULL(info))
		return;

	kfree(info->alg);
	kfree(info->digest);
	kfree(info);
}

/**
 * ipe_digest_audit() - audit a digest that was sourced from IPE's policy.
 * @ab: Supplies the audit_buffer to append the formatted result.
 * @info: Supplies a pointer to source the audit record from.
 *
 * Digests in IPE are audited in this format:
 *	<alg_name>:<hex>
 */
void ipe_digest_audit(struct audit_buffer *ab, const struct digest_info *info)
{
	audit_log_untrustedstring(ab, info->alg);
	audit_log_format(ab, ":");
	audit_log_n_hex(ab, info->digest, info->digest_len);
}
