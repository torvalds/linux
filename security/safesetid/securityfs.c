// SPDX-License-Identifier: GPL-2.0
/*
 * SafeSetID Linux Security Module
 *
 * Author: Micah Morton <mortonm@chromium.org>
 *
 * Copyright (C) 2018 The Chromium OS Authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/security.h>
#include <linux/cred.h>

#include "lsm.h"

static struct dentry *safesetid_policy_dir;

struct safesetid_file_entry {
	const char *name;
	enum safesetid_whitelist_file_write_type type;
	struct dentry *dentry;
};

static struct safesetid_file_entry safesetid_files[] = {
	{.name = "add_whitelist_policy",
	 .type = SAFESETID_WHITELIST_ADD},
	{.name = "flush_whitelist_policies",
	 .type = SAFESETID_WHITELIST_FLUSH},
};

/*
 * In the case the input buffer contains one or more invalid UIDs, the kuid_t
 * variables pointed to by @parent and @child will get updated but this
 * function will return an error.
 * Contents of @buf may be modified.
 */
static int parse_policy_line(
	struct file *file, char *buf, kuid_t *parent, kuid_t *child)
{
	char *child_str;
	int ret;
	u32 parsed_parent, parsed_child;

	/* Format of |buf| string should be <UID>:<UID>. */
	child_str = strchr(buf, ':');
	if (child_str == NULL)
		return -EINVAL;
	*child_str = '\0';
	child_str++;

	ret = kstrtou32(buf, 0, &parsed_parent);
	if (ret)
		return ret;

	ret = kstrtou32(child_str, 0, &parsed_child);
	if (ret)
		return ret;

	*parent = make_kuid(current_user_ns(), parsed_parent);
	*child = make_kuid(current_user_ns(), parsed_child);
	if (!uid_valid(*parent) || !uid_valid(*child))
		return -EINVAL;

	return 0;
}

static int parse_safesetid_whitelist_policy(
	struct file *file, const char __user *buf, size_t len,
	kuid_t *parent, kuid_t *child)
{
	char *kern_buf = memdup_user_nul(buf, len);
	int ret;

	if (IS_ERR(kern_buf))
		return PTR_ERR(kern_buf);
	ret = parse_policy_line(file, kern_buf, parent, child);
	kfree(kern_buf);
	return ret;
}

static ssize_t safesetid_file_write(struct file *file,
				    const char __user *buf,
				    size_t len,
				    loff_t *ppos)
{
	struct safesetid_file_entry *file_entry =
		file->f_inode->i_private;
	kuid_t parent;
	kuid_t child;
	int ret;

	if (!ns_capable(current_user_ns(), CAP_MAC_ADMIN))
		return -EPERM;

	if (*ppos != 0)
		return -EINVAL;

	switch (file_entry->type) {
	case SAFESETID_WHITELIST_FLUSH:
		flush_safesetid_whitelist_entries();
		break;
	case SAFESETID_WHITELIST_ADD:
		ret = parse_safesetid_whitelist_policy(file, buf, len,
						       &parent, &child);
		if (ret)
			return ret;

		ret = add_safesetid_whitelist_entry(parent, child);
		if (ret)
			return ret;
		break;
	default:
		pr_warn("Unknown securityfs file %d\n", file_entry->type);
		break;
	}

	/* Return len on success so caller won't keep trying to write */
	return len;
}

static const struct file_operations safesetid_file_fops = {
	.write = safesetid_file_write,
};

static void safesetid_shutdown_securityfs(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(safesetid_files); ++i) {
		struct safesetid_file_entry *entry =
			&safesetid_files[i];
		securityfs_remove(entry->dentry);
		entry->dentry = NULL;
	}

	securityfs_remove(safesetid_policy_dir);
	safesetid_policy_dir = NULL;
}

static int __init safesetid_init_securityfs(void)
{
	int i;
	int ret;

	if (!safesetid_initialized)
		return 0;

	safesetid_policy_dir = securityfs_create_dir("safesetid", NULL);
	if (IS_ERR(safesetid_policy_dir)) {
		ret = PTR_ERR(safesetid_policy_dir);
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(safesetid_files); ++i) {
		struct safesetid_file_entry *entry =
			&safesetid_files[i];
		entry->dentry = securityfs_create_file(
			entry->name, 0200, safesetid_policy_dir,
			entry, &safesetid_file_fops);
		if (IS_ERR(entry->dentry)) {
			ret = PTR_ERR(entry->dentry);
			goto error;
		}
	}

	return 0;

error:
	safesetid_shutdown_securityfs();
	return ret;
}
fs_initcall(safesetid_init_securityfs);
