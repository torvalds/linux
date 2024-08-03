// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/dcache.h>
#include <linux/security.h>

#include "ipe.h"
#include "fs.h"
#include "policy.h"

static struct dentry *np __ro_after_init;
static struct dentry *root __ro_after_init;
struct dentry *policy_root __ro_after_init;

/**
 * new_policy() - Write handler for the securityfs node, "ipe/new_policy".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-EPERM			- Insufficient permission
 * * %-ENOMEM			- Out of memory (OOM)
 * * %-EBADMSG			- Policy is invalid
 * * %-ERANGE			- Policy version number overflow
 * * %-EINVAL			- Policy version parsing error
 * * %-EEXIST			- Same name policy already deployed
 */
static ssize_t new_policy(struct file *f, const char __user *data,
			  size_t len, loff_t *offset)
{
	struct ipe_policy *p = NULL;
	char *copy = NULL;
	int rc = 0;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN))
		return -EPERM;

	copy = memdup_user_nul(data, len);
	if (IS_ERR(copy))
		return PTR_ERR(copy);

	p = ipe_new_policy(NULL, 0, copy, len);
	if (IS_ERR(p)) {
		rc = PTR_ERR(p);
		goto out;
	}

	rc = ipe_new_policyfs_node(p);

out:
	if (rc < 0)
		ipe_free_policy(p);
	kfree(copy);
	return (rc < 0) ? rc : len;
}

static const struct file_operations np_fops = {
	.write = new_policy,
};

/**
 * ipe_init_securityfs() - Initialize IPE's securityfs tree at fsinit.
 *
 * Return: %0 on success. If an error occurs, the function will return
 * the -errno.
 */
static int __init ipe_init_securityfs(void)
{
	int rc = 0;

	if (!ipe_enabled)
		return -EOPNOTSUPP;

	root = securityfs_create_dir("ipe", NULL);
	if (IS_ERR(root)) {
		rc = PTR_ERR(root);
		goto err;
	}

	policy_root = securityfs_create_dir("policies", root);
	if (IS_ERR(policy_root)) {
		rc = PTR_ERR(policy_root);
		goto err;
	}

	np = securityfs_create_file("new_policy", 0200, root, NULL, &np_fops);
	if (IS_ERR(np)) {
		rc = PTR_ERR(np);
		goto err;
	}

	return 0;
err:
	securityfs_remove(np);
	securityfs_remove(policy_root);
	securityfs_remove(root);
	return rc;
}

fs_initcall(ipe_init_securityfs);
