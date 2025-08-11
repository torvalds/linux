// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/types.h>
#include <linux/dcache.h>
#include <linux/security.h>

#include "ipe.h"
#include "policy.h"
#include "eval.h"
#include "fs.h"
#include "audit.h"

#define MAX_VERSION_SIZE ARRAY_SIZE("65535.65535.65535")

/**
 * struct ipefs_file - defines a file in securityfs.
 *
 * @name: file name inside the policy subdirectory
 * @access: file permissions
 * @fops: &file_operations specific to this file
 */
struct ipefs_file {
	const char *name;
	umode_t access;
	const struct file_operations *fops;
};

/**
 * read_pkcs7() - Read handler for "ipe/policies/$name/pkcs7".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * @data will be populated with the pkcs7 blob representing the policy
 * on success. If the policy is unsigned (like the boot policy), this
 * will return -ENOENT.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-ENOENT			- Policy initializing/deleted or is unsigned
 */
static ssize_t read_pkcs7(struct file *f, char __user *data,
			  size_t len, loff_t *offset)
{
	const struct ipe_policy *p = NULL;
	struct inode *root = NULL;
	int rc = 0;

	root = d_inode(f->f_path.dentry->d_parent);

	inode_lock_shared(root);
	p = (struct ipe_policy *)root->i_private;
	if (!p) {
		rc = -ENOENT;
		goto out;
	}

	if (!p->pkcs7) {
		rc = -ENOENT;
		goto out;
	}

	rc = simple_read_from_buffer(data, len, offset, p->pkcs7, p->pkcs7len);

out:
	inode_unlock_shared(root);

	return rc;
}

/**
 * read_policy() - Read handler for "ipe/policies/$name/policy".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * @data will be populated with the plain-text version of the policy
 * on success.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-ENOENT			- Policy initializing/deleted
 */
static ssize_t read_policy(struct file *f, char __user *data,
			   size_t len, loff_t *offset)
{
	const struct ipe_policy *p = NULL;
	struct inode *root = NULL;
	int rc = 0;

	root = d_inode(f->f_path.dentry->d_parent);

	inode_lock_shared(root);
	p = (struct ipe_policy *)root->i_private;
	if (!p) {
		rc = -ENOENT;
		goto out;
	}

	rc = simple_read_from_buffer(data, len, offset, p->text, p->textlen);

out:
	inode_unlock_shared(root);

	return rc;
}

/**
 * read_name() - Read handler for "ipe/policies/$name/name".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * @data will be populated with the policy_name attribute on success.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-ENOENT			- Policy initializing/deleted
 */
static ssize_t read_name(struct file *f, char __user *data,
			 size_t len, loff_t *offset)
{
	const struct ipe_policy *p = NULL;
	struct inode *root = NULL;
	int rc = 0;

	root = d_inode(f->f_path.dentry->d_parent);

	inode_lock_shared(root);
	p = (struct ipe_policy *)root->i_private;
	if (!p) {
		rc = -ENOENT;
		goto out;
	}

	rc = simple_read_from_buffer(data, len, offset, p->parsed->name,
				     strlen(p->parsed->name));

out:
	inode_unlock_shared(root);

	return rc;
}

/**
 * read_version() - Read handler for "ipe/policies/$name/version".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * @data will be populated with the version string on success.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-ENOENT			- Policy initializing/deleted
 */
static ssize_t read_version(struct file *f, char __user *data,
			    size_t len, loff_t *offset)
{
	char buffer[MAX_VERSION_SIZE] = { 0 };
	const struct ipe_policy *p = NULL;
	struct inode *root = NULL;
	size_t strsize = 0;
	ssize_t rc = 0;

	root = d_inode(f->f_path.dentry->d_parent);

	inode_lock_shared(root);
	p = (struct ipe_policy *)root->i_private;
	if (!p) {
		rc = -ENOENT;
		goto out;
	}

	strsize = scnprintf(buffer, ARRAY_SIZE(buffer), "%hu.%hu.%hu",
			    p->parsed->version.major, p->parsed->version.minor,
			    p->parsed->version.rev);

	rc = simple_read_from_buffer(data, len, offset, buffer, strsize);

out:
	inode_unlock_shared(root);

	return rc;
}

/**
 * setactive() - Write handler for "ipe/policies/$name/active".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-EPERM			- Insufficient permission
 * * %-EINVAL			- Invalid input
 * * %-ENOENT			- Policy initializing/deleted
 */
static ssize_t setactive(struct file *f, const char __user *data,
			 size_t len, loff_t *offset)
{
	const struct ipe_policy *p = NULL;
	struct inode *root = NULL;
	bool value = false;
	int rc = 0;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN))
		return -EPERM;

	rc = kstrtobool_from_user(data, len, &value);
	if (rc)
		return rc;

	if (!value)
		return -EINVAL;

	root = d_inode(f->f_path.dentry->d_parent);
	inode_lock(root);

	p = (struct ipe_policy *)root->i_private;
	if (!p) {
		rc = -ENOENT;
		goto out;
	}

	rc = ipe_set_active_pol(p);

out:
	inode_unlock(root);
	return (rc < 0) ? rc : len;
}

/**
 * getactive() - Read handler for "ipe/policies/$name/active".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * @data will be populated with the 1 or 0 depending on if the
 * corresponding policy is active.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-ENOENT			- Policy initializing/deleted
 */
static ssize_t getactive(struct file *f, char __user *data,
			 size_t len, loff_t *offset)
{
	const struct ipe_policy *p = NULL;
	struct inode *root = NULL;
	const char *str;
	int rc = 0;

	root = d_inode(f->f_path.dentry->d_parent);

	inode_lock_shared(root);
	p = (struct ipe_policy *)root->i_private;
	if (!p) {
		inode_unlock_shared(root);
		return -ENOENT;
	}
	inode_unlock_shared(root);

	str = (p == rcu_access_pointer(ipe_active_policy)) ? "1" : "0";
	rc = simple_read_from_buffer(data, len, offset, str, 1);

	return rc;
}

/**
 * update_policy() - Write handler for "ipe/policies/$name/update".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * On success this updates the policy represented by $name,
 * in-place.
 *
 * Return:
 * * Length of buffer written		- Success
 * * %-EPERM				- Insufficient permission
 * * %-ENOMEM				- Out of memory (OOM)
 * * %-ENOENT				- Policy was deleted while updating
 * * %-EINVAL				- Policy name mismatch
 * * %-ESTALE				- Policy version too old
 */
static ssize_t update_policy(struct file *f, const char __user *data,
			     size_t len, loff_t *offset)
{
	struct inode *root = NULL;
	char *copy = NULL;
	int rc = 0;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN)) {
		rc = -EPERM;
		goto out;
	}

	copy = memdup_user(data, len);
	if (IS_ERR(copy)) {
		rc = PTR_ERR(copy);
		copy = NULL;
		goto out;
	}

	root = d_inode(f->f_path.dentry->d_parent);
	inode_lock(root);
	rc = ipe_update_policy(root, NULL, 0, copy, len);
	inode_unlock(root);

out:
	kfree(copy);
	if (rc) {
		ipe_audit_policy_load(ERR_PTR(rc));
		return rc;
	}

	return len;
}

/**
 * delete_policy() - write handler for  "ipe/policies/$name/delete".
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the write syscall.
 * @len: Supplies the length of @data.
 * @offset: unused.
 *
 * On success this deletes the policy represented by $name.
 *
 * Return:
 * * Length of buffer written	- Success
 * * %-EPERM			- Insufficient permission/deleting active policy
 * * %-EINVAL			- Invalid input
 * * %-ENOENT			- Policy initializing/deleted
 */
static ssize_t delete_policy(struct file *f, const char __user *data,
			     size_t len, loff_t *offset)
{
	struct ipe_policy *ap = NULL;
	struct ipe_policy *p = NULL;
	struct inode *root = NULL;
	bool value = false;
	int rc = 0;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN))
		return -EPERM;

	rc = kstrtobool_from_user(data, len, &value);
	if (rc)
		return rc;

	if (!value)
		return -EINVAL;

	root = d_inode(f->f_path.dentry->d_parent);
	inode_lock(root);
	p = (struct ipe_policy *)root->i_private;
	if (!p) {
		inode_unlock(root);
		return -ENOENT;
	}

	mutex_lock(&ipe_policy_lock);
	ap = rcu_dereference_protected(ipe_active_policy,
				       lockdep_is_held(&ipe_policy_lock));
	if (p == ap) {
		mutex_unlock(&ipe_policy_lock);
		inode_unlock(root);
		return -EPERM;
	}
	mutex_unlock(&ipe_policy_lock);

	root->i_private = NULL;
	inode_unlock(root);

	synchronize_rcu();
	ipe_free_policy(p);

	return len;
}

static const struct file_operations content_fops = {
	.read = read_policy,
};

static const struct file_operations pkcs7_fops = {
	.read = read_pkcs7,
};

static const struct file_operations name_fops = {
	.read = read_name,
};

static const struct file_operations ver_fops = {
	.read = read_version,
};

static const struct file_operations active_fops = {
	.write = setactive,
	.read = getactive,
};

static const struct file_operations update_fops = {
	.write = update_policy,
};

static const struct file_operations delete_fops = {
	.write = delete_policy,
};

/*
 * policy_subdir - files under a policy subdirectory
 */
static const struct ipefs_file policy_subdir[] = {
	{ "pkcs7", 0444, &pkcs7_fops },
	{ "policy", 0444, &content_fops },
	{ "name", 0444, &name_fops },
	{ "version", 0444, &ver_fops },
	{ "active", 0600, &active_fops },
	{ "update", 0200, &update_fops },
	{ "delete", 0200, &delete_fops },
};

/**
 * ipe_del_policyfs_node() - Delete a securityfs entry for @p.
 * @p: Supplies a pointer to the policy to delete a securityfs entry for.
 */
void ipe_del_policyfs_node(struct ipe_policy *p)
{
	securityfs_remove(p->policyfs);
	p->policyfs = NULL;
}

/**
 * ipe_new_policyfs_node() - Create a securityfs entry for @p.
 * @p: Supplies a pointer to the policy to create a securityfs entry for.
 *
 * Return: %0 on success. If an error occurs, the function will return
 * the -errno.
 */
int ipe_new_policyfs_node(struct ipe_policy *p)
{
	const struct ipefs_file *f = NULL;
	struct dentry *policyfs = NULL;
	struct inode *root = NULL;
	struct dentry *d = NULL;
	size_t i = 0;
	int rc = 0;

	if (p->policyfs)
		return 0;

	policyfs = securityfs_create_dir(p->parsed->name, policy_root);
	if (IS_ERR(policyfs))
		return PTR_ERR(policyfs);

	root = d_inode(policyfs);

	for (i = 0; i < ARRAY_SIZE(policy_subdir); ++i) {
		f = &policy_subdir[i];

		d = securityfs_create_file(f->name, f->access, policyfs,
					   NULL, f->fops);
		if (IS_ERR(d)) {
			rc = PTR_ERR(d);
			goto err;
		}
	}

	inode_lock(root);
	p->policyfs = policyfs;
	root->i_private = p;
	inode_unlock(root);

	return 0;
err:
	securityfs_remove(policyfs);
	return rc;
}
