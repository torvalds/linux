// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/verification.h>

#include "ipe.h"
#include "policy.h"
#include "policy_parser.h"

/**
 * ipe_free_policy() - Deallocate a given IPE policy.
 * @p: Supplies the policy to free.
 *
 * Safe to call on IS_ERR/NULL.
 */
void ipe_free_policy(struct ipe_policy *p)
{
	if (IS_ERR_OR_NULL(p))
		return;

	ipe_free_parsed_policy(p->parsed);
	/*
	 * p->text is allocated only when p->pkcs7 is not NULL
	 * otherwise it points to the plaintext data inside the pkcs7
	 */
	if (!p->pkcs7)
		kfree(p->text);
	kfree(p->pkcs7);
	kfree(p);
}

static int set_pkcs7_data(void *ctx, const void *data, size_t len,
			  size_t asn1hdrlen __always_unused)
{
	struct ipe_policy *p = ctx;

	p->text = (const char *)data;
	p->textlen = len;

	return 0;
}

/**
 * ipe_new_policy() - Allocate and parse an ipe_policy structure.
 *
 * @text: Supplies a pointer to the plain-text policy to parse.
 * @textlen: Supplies the length of @text.
 * @pkcs7: Supplies a pointer to a pkcs7-signed IPE policy.
 * @pkcs7len: Supplies the length of @pkcs7.
 *
 * @text/@textlen Should be NULL/0 if @pkcs7/@pkcs7len is set.
 *
 * Return:
 * * a pointer to the ipe_policy structure	- Success
 * * %-EBADMSG					- Policy is invalid
 * * %-ENOMEM					- Out of memory (OOM)
 * * %-ERANGE					- Policy version number overflow
 * * %-EINVAL					- Policy version parsing error
 */
struct ipe_policy *ipe_new_policy(const char *text, size_t textlen,
				  const char *pkcs7, size_t pkcs7len)
{
	struct ipe_policy *new = NULL;
	int rc = 0;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);

	if (!text) {
		new->pkcs7len = pkcs7len;
		new->pkcs7 = kmemdup(pkcs7, pkcs7len, GFP_KERNEL);
		if (!new->pkcs7) {
			rc = -ENOMEM;
			goto err;
		}

		rc = verify_pkcs7_signature(NULL, 0, new->pkcs7, pkcs7len, NULL,
					    VERIFYING_UNSPECIFIED_SIGNATURE,
					    set_pkcs7_data, new);
		if (rc)
			goto err;
	} else {
		new->textlen = textlen;
		new->text = kstrdup(text, GFP_KERNEL);
		if (!new->text) {
			rc = -ENOMEM;
			goto err;
		}
	}

	rc = ipe_parse_policy(new);
	if (rc)
		goto err;

	return new;
err:
	ipe_free_policy(new);
	return ERR_PTR(rc);
}
