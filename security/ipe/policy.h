/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */
#ifndef _IPE_POLICY_H
#define _IPE_POLICY_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/fs.h>

enum ipe_op_type {
	IPE_OP_EXEC = 0,
	IPE_OP_FIRMWARE,
	IPE_OP_KERNEL_MODULE,
	IPE_OP_KEXEC_IMAGE,
	IPE_OP_KEXEC_INITRAMFS,
	IPE_OP_POLICY,
	IPE_OP_X509,
	__IPE_OP_MAX,
};

#define IPE_OP_INVALID __IPE_OP_MAX

enum ipe_action_type {
	IPE_ACTION_ALLOW = 0,
	IPE_ACTION_DENY,
	__IPE_ACTION_MAX
};

#define IPE_ACTION_INVALID __IPE_ACTION_MAX

enum ipe_prop_type {
	IPE_PROP_BOOT_VERIFIED_FALSE,
	IPE_PROP_BOOT_VERIFIED_TRUE,
	IPE_PROP_DMV_ROOTHASH,
	IPE_PROP_DMV_SIG_FALSE,
	IPE_PROP_DMV_SIG_TRUE,
	IPE_PROP_FSV_DIGEST,
	IPE_PROP_FSV_SIG_FALSE,
	IPE_PROP_FSV_SIG_TRUE,
	__IPE_PROP_MAX
};

#define IPE_PROP_INVALID __IPE_PROP_MAX

struct ipe_prop {
	struct list_head next;
	enum ipe_prop_type type;
	void *value;
};

struct ipe_rule {
	enum ipe_op_type op;
	enum ipe_action_type action;
	struct list_head props;
	struct list_head next;
};

struct ipe_op_table {
	struct list_head rules;
	enum ipe_action_type default_action;
};

struct ipe_parsed_policy {
	const char *name;
	struct {
		u16 major;
		u16 minor;
		u16 rev;
	} version;

	enum ipe_action_type global_default_action;

	struct ipe_op_table rules[__IPE_OP_MAX];
};

struct ipe_policy {
	const char *pkcs7;
	size_t pkcs7len;

	const char *text;
	size_t textlen;

	struct ipe_parsed_policy *parsed;

	struct dentry *policyfs;
};

struct ipe_policy *ipe_new_policy(const char *text, size_t textlen,
				  const char *pkcs7, size_t pkcs7len);
void ipe_free_policy(struct ipe_policy *pol);
int ipe_update_policy(struct inode *root, const char *text, size_t textlen,
		      const char *pkcs7, size_t pkcs7len);
int ipe_set_active_pol(const struct ipe_policy *p);
extern struct mutex ipe_policy_lock;

#endif /* _IPE_POLICY_H */
