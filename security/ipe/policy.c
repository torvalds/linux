// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/verification.h>

#include "ipe.h"
#include "eval.h"
#include "fs.h"
#include "policy.h"
#include "policy_parser.h"
#include "audit.h"

/* lock for synchronizing writers across ipe policy */
DEFINE_MUTEX(ipe_policy_lock);

/**
 * ver_to_u64() - Convert an internal ipe_policy_version to a u64.
 * @p: Policy to extract the version from.
 *
 * Bits (LSB is index 0):
 *	[48,32] -> Major
 *	[32,16] -> Minor
 *	[16, 0] -> Revision
 *
 * Return: u64 version of the embedded version structure.
 */
static inline u64 ver_to_u64(const struct ipe_policy *const p)
{
	u64 r;

	r = (((u64)p->parsed->version.major) << 32)
	  | (((u64)p->parsed->version.minor) << 16)
	  | ((u64)(p->parsed->version.rev));

	return r;
}

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

	ipe_del_policyfs_node(p);
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
 * ipe_update_policy() - parse a new policy and replace old with it.
 * @root: Supplies a pointer to the securityfs inode saved the policy.
 * @text: Supplies a pointer to the plain text policy.
 * @textlen: Supplies the length of @text.
 * @pkcs7: Supplies a pointer to a buffer containing a pkcs7 message.
 * @pkcs7len: Supplies the length of @pkcs7len.
 *
 * @text/@textlen is mutually exclusive with @pkcs7/@pkcs7len - see
 * ipe_new_policy.
 *
 * Context: Requires root->i_rwsem to be held.
 * Return: %0 on success. If an error occurs, the function will return
 * the -errno.
 */
int ipe_update_policy(struct inode *root, const char *text, size_t textlen,
		      const char *pkcs7, size_t pkcs7len)
{
	struct ipe_policy *old, *ap, *new = NULL;
	int rc = 0;

	old = (struct ipe_policy *)root->i_private;
	if (!old)
		return -ENOENT;

	new = ipe_new_policy(text, textlen, pkcs7, pkcs7len);
	if (IS_ERR(new))
		return PTR_ERR(new);

	if (strcmp(new->parsed->name, old->parsed->name)) {
		rc = -EINVAL;
		goto err;
	}

	if (ver_to_u64(old) >= ver_to_u64(new)) {
		rc = -ESTALE;
		goto err;
	}

	root->i_private = new;
	swap(new->policyfs, old->policyfs);
	ipe_audit_policy_load(new);

	mutex_lock(&ipe_policy_lock);
	ap = rcu_dereference_protected(ipe_active_policy,
				       lockdep_is_held(&ipe_policy_lock));
	if (old == ap) {
		rcu_assign_pointer(ipe_active_policy, new);
		mutex_unlock(&ipe_policy_lock);
		ipe_audit_policy_activation(old, new);
	} else {
		mutex_unlock(&ipe_policy_lock);
	}
	synchronize_rcu();
	ipe_free_policy(old);

	return 0;
err:
	ipe_free_policy(new);
	return rc;
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

		rc = verify_pkcs7_signature(NULL, 0, new->pkcs7, pkcs7len,
#ifdef CONFIG_IPE_POLICY_SIG_SECONDARY_KEYRING
					    VERIFY_USE_SECONDARY_KEYRING,
#else
					    NULL,
#endif
					    VERIFYING_UNSPECIFIED_SIGNATURE,
					    set_pkcs7_data, new);
#ifdef CONFIG_IPE_POLICY_SIG_PLATFORM_KEYRING
		if (rc == -ENOKEY || rc == -EKEYREJECTED)
			rc = verify_pkcs7_signature(NULL, 0, new->pkcs7, pkcs7len,
						    VERIFY_USE_PLATFORM_KEYRING,
						    VERIFYING_UNSPECIFIED_SIGNATURE,
						    set_pkcs7_data, new);
#endif
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

/**
 * ipe_set_active_pol() - Make @p the active policy.
 * @p: Supplies a pointer to the policy to make active.
 *
 * Context: Requires root->i_rwsem, which i_private has the policy, to be held.
 * Return:
 * * %0	- Success
 * * %-EINVAL	- New active policy version is invalid
 */
int ipe_set_active_pol(const struct ipe_policy *p)
{
	struct ipe_policy *ap = NULL;

	mutex_lock(&ipe_policy_lock);

	ap = rcu_dereference_protected(ipe_active_policy,
				       lockdep_is_held(&ipe_policy_lock));
	if (ap == p) {
		mutex_unlock(&ipe_policy_lock);
		return 0;
	}
	if (ap && ver_to_u64(ap) > ver_to_u64(p)) {
		mutex_unlock(&ipe_policy_lock);
		return -EINVAL;
	}

	rcu_assign_pointer(ipe_active_policy, p);
	ipe_audit_policy_activation(ap, p);
	mutex_unlock(&ipe_policy_lock);

	return 0;
}
