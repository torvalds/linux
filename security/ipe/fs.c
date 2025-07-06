// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/dcache.h>
#include <linux/security.h>

#include "ipe.h"
#include "fs.h"
#include "eval.h"
#include "policy.h"
#include "audit.h"

static struct dentry *np __ro_after_init;
static struct dentry *root __ro_after_init;
struct dentry *policy_root __ro_after_init;
static struct dentry *audit_node __ro_after_init;
static struct dentry *enforce_node __ro_after_init;

/**
 * setaudit() - Write handler for the securityfs node, "ipe/success_audit"
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-EPERM			- Insufficient permission
 */
static ssize_t setaudit(struct file *f, const char __user *data,
			size_t len, loff_t *offset)
{
	int rc = 0;
	bool value;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN))
		return -EPERM;

	rc = kstrtobool_from_user(data, len, &value);
	if (rc)
		return rc;

	WRITE_ONCE(success_audit, value);

	return len;
}

/**
 * getaudit() - Read handler for the securityfs node, "ipe/success_audit"
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the read syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * Return: Length of buffer written
 */
static ssize_t getaudit(struct file *f, char __user *data,
			size_t len, loff_t *offset)
{
	const char *result;

	result = ((READ_ONCE(success_audit)) ? "1" : "0");

	return simple_read_from_buffer(data, len, offset, result, 1);
}

/**
 * setenforce() - Write handler for the securityfs node, "ipe/enforce"
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-EPERM			- Insufficient permission
 */
static ssize_t setenforce(struct file *f, const char __user *data,
			  size_t len, loff_t *offset)
{
	int rc = 0;
	bool new_value, old_value;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN))
		return -EPERM;

	old_value = READ_ONCE(enforce);
	rc = kstrtobool_from_user(data, len, &new_value);
	if (rc)
		return rc;

	if (new_value != old_value) {
		ipe_audit_enforce(new_value, old_value);
		WRITE_ONCE(enforce, new_value);
	}

	return len;
}

/**
 * getenforce() - Read handler for the securityfs node, "ipe/enforce"
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the read syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * Return: Length of buffer written
 */
static ssize_t getenforce(struct file *f, char __user *data,
			  size_t len, loff_t *offset)
{
	const char *result;

	result = ((READ_ONCE(enforce)) ? "1" : "0");

	return simple_read_from_buffer(data, len, offset, result, 1);
}

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
 * * %-ENOKEY			- Policy signing key not found
 * * %-EKEYREJECTED		- Policy signature verification failed
 */
static ssize_t new_policy(struct file *f, const char __user *data,
			  size_t len, loff_t *offset)
{
	struct ipe_policy *p = NULL;
	char *copy = NULL;
	int rc = 0;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN)) {
		rc = -EPERM;
		goto out;
	}

	copy = memdup_user_nul(data, len);
	if (IS_ERR(copy)) {
		rc = PTR_ERR(copy);
		copy = NULL;
		goto out;
	}

	p = ipe_new_policy(NULL, 0, copy, len);
	if (IS_ERR(p)) {
		rc = PTR_ERR(p);
		goto out;
	}

	rc = ipe_new_policyfs_node(p);
	if (rc)
		goto out;

out:
	kfree(copy);
	if (rc < 0) {
		ipe_free_policy(p);
		ipe_audit_policy_load(ERR_PTR(rc));
	} else {
		ipe_audit_policy_load(p);
	}
	return (rc < 0) ? rc : len;
}

static const struct file_operations np_fops = {
	.write = new_policy,
};

static const struct file_operations audit_fops = {
	.write = setaudit,
	.read = getaudit,
};

static const struct file_operations enforce_fops = {
	.write = setenforce,
	.read = getenforce,
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
	struct ipe_policy *ap;

	if (!ipe_enabled)
		return -EOPNOTSUPP;

	root = securityfs_create_dir("ipe", NULL);
	if (IS_ERR(root)) {
		rc = PTR_ERR(root);
		goto err;
	}

	audit_node = securityfs_create_file("success_audit", 0600, root,
					    NULL, &audit_fops);
	if (IS_ERR(audit_node)) {
		rc = PTR_ERR(audit_node);
		goto err;
	}

	enforce_node = securityfs_create_file("enforce", 0600, root, NULL,
					      &enforce_fops);
	if (IS_ERR(enforce_node)) {
		rc = PTR_ERR(enforce_node);
		goto err;
	}

	policy_root = securityfs_create_dir("policies", root);
	if (IS_ERR(policy_root)) {
		rc = PTR_ERR(policy_root);
		goto err;
	}

	ap = rcu_access_pointer(ipe_active_policy);
	if (ap) {
		rc = ipe_new_policyfs_node(ap);
		if (rc)
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
	securityfs_remove(enforce_node);
	securityfs_remove(audit_node);
	securityfs_remove(root);
	return rc;
}

fs_initcall(ipe_init_securityfs);
