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

#define pr_fmt(fmt) "SafeSetID: " fmt

#include <linux/security.h>
#include <linux/cred.h>

#include "lsm.h"

static DEFINE_MUTEX(policy_update_lock);

/*
 * In the case the input buffer contains one or more invalid UIDs, the kuid_t
 * variables pointed to by @parent and @child will get updated but this
 * function will return an error.
 * Contents of @buf may be modified.
 */
static int parse_policy_line(struct file *file, char *buf,
	struct setuid_rule *rule)
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

	rule->src_uid = make_kuid(file->f_cred->user_ns, parsed_parent);
	rule->dst_uid = make_kuid(file->f_cred->user_ns, parsed_child);
	if (!uid_valid(rule->src_uid) || !uid_valid(rule->dst_uid))
		return -EINVAL;

	return 0;
}

static void __release_ruleset(struct rcu_head *rcu)
{
	struct setuid_ruleset *pol =
		container_of(rcu, struct setuid_ruleset, rcu);
	int bucket;
	struct setuid_rule *rule;
	struct hlist_node *tmp;

	hash_for_each_safe(pol->rules, bucket, tmp, rule, next)
		kfree(rule);
	kfree(pol->policy_str);
	kfree(pol);
}

static void release_ruleset(struct setuid_ruleset *pol)
{
	call_rcu(&pol->rcu, __release_ruleset);
}

static void insert_rule(struct setuid_ruleset *pol, struct setuid_rule *rule)
{
	hash_add(pol->rules, &rule->next, __kuid_val(rule->src_uid));
}

static int verify_ruleset(struct setuid_ruleset *pol)
{
	int bucket;
	struct setuid_rule *rule, *nrule;
	int res = 0;

	hash_for_each(pol->rules, bucket, rule, next) {
		if (_setuid_policy_lookup(pol, rule->dst_uid, INVALID_UID) ==
		    SIDPOL_DEFAULT) {
			pr_warn("insecure policy detected: uid %d is constrained but transitively unconstrained through uid %d\n",
				__kuid_val(rule->src_uid),
				__kuid_val(rule->dst_uid));
			res = -EINVAL;

			/* fix it up */
			nrule = kmalloc(sizeof(struct setuid_rule), GFP_KERNEL);
			if (!nrule)
				return -ENOMEM;
			nrule->src_uid = rule->dst_uid;
			nrule->dst_uid = rule->dst_uid;
			insert_rule(pol, nrule);
		}
	}
	return res;
}

static ssize_t handle_policy_update(struct file *file,
				    const char __user *ubuf, size_t len)
{
	struct setuid_ruleset *pol;
	char *buf, *p, *end;
	int err;

	pol = kmalloc(sizeof(struct setuid_ruleset), GFP_KERNEL);
	if (!pol)
		return -ENOMEM;
	pol->policy_str = NULL;
	hash_init(pol->rules);

	p = buf = memdup_user_nul(ubuf, len);
	if (IS_ERR(buf)) {
		err = PTR_ERR(buf);
		goto out_free_pol;
	}
	pol->policy_str = kstrdup(buf, GFP_KERNEL);
	if (pol->policy_str == NULL) {
		err = -ENOMEM;
		goto out_free_buf;
	}

	/* policy lines, including the last one, end with \n */
	while (*p != '\0') {
		struct setuid_rule *rule;

		end = strchr(p, '\n');
		if (end == NULL) {
			err = -EINVAL;
			goto out_free_buf;
		}
		*end = '\0';

		rule = kmalloc(sizeof(struct setuid_rule), GFP_KERNEL);
		if (!rule) {
			err = -ENOMEM;
			goto out_free_buf;
		}

		err = parse_policy_line(file, p, rule);
		if (err)
			goto out_free_rule;

		if (_setuid_policy_lookup(pol, rule->src_uid, rule->dst_uid) ==
		    SIDPOL_ALLOWED) {
			pr_warn("bad policy: duplicate entry\n");
			err = -EEXIST;
			goto out_free_rule;
		}

		insert_rule(pol, rule);
		p = end + 1;
		continue;

out_free_rule:
		kfree(rule);
		goto out_free_buf;
	}

	err = verify_ruleset(pol);
	/* bogus policy falls through after fixing it up */
	if (err && err != -EINVAL)
		goto out_free_buf;

	/*
	 * Everything looks good, apply the policy and release the old one.
	 * What we really want here is an xchg() wrapper for RCU, but since that
	 * doesn't currently exist, just use a spinlock for now.
	 */
	mutex_lock(&policy_update_lock);
	rcu_swap_protected(safesetid_setuid_rules, pol,
			   lockdep_is_held(&policy_update_lock));
	mutex_unlock(&policy_update_lock);
	err = len;

out_free_buf:
	kfree(buf);
out_free_pol:
	if (pol)
                release_ruleset(pol);
	return err;
}

static ssize_t safesetid_file_write(struct file *file,
				    const char __user *buf,
				    size_t len,
				    loff_t *ppos)
{
	if (!file_ns_capable(file, &init_user_ns, CAP_MAC_ADMIN))
		return -EPERM;

	if (*ppos != 0)
		return -EINVAL;

	return handle_policy_update(file, buf, len);
}

static ssize_t safesetid_file_read(struct file *file, char __user *buf,
				   size_t len, loff_t *ppos)
{
	ssize_t res = 0;
	struct setuid_ruleset *pol;
	const char *kbuf;

	mutex_lock(&policy_update_lock);
	pol = rcu_dereference_protected(safesetid_setuid_rules,
					lockdep_is_held(&policy_update_lock));
	if (pol) {
		kbuf = pol->policy_str;
		res = simple_read_from_buffer(buf, len, ppos,
					      kbuf, strlen(kbuf));
	}
	mutex_unlock(&policy_update_lock);
	return res;
}

static const struct file_operations safesetid_file_fops = {
	.read = safesetid_file_read,
	.write = safesetid_file_write,
};

static int __init safesetid_init_securityfs(void)
{
	int ret;
	struct dentry *policy_dir;
	struct dentry *policy_file;

	if (!safesetid_initialized)
		return 0;

	policy_dir = securityfs_create_dir("safesetid", NULL);
	if (IS_ERR(policy_dir)) {
		ret = PTR_ERR(policy_dir);
		goto error;
	}

	policy_file = securityfs_create_file("whitelist_policy", 0600,
			policy_dir, NULL, &safesetid_file_fops);
	if (IS_ERR(policy_file)) {
		ret = PTR_ERR(policy_file);
		goto error;
	}

	return 0;

error:
	securityfs_remove(policy_dir);
	return ret;
}
fs_initcall(safesetid_init_securityfs);
