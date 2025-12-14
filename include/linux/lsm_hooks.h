/*
 * Linux Security Module interfaces
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 * Copyright (C) 2015 Intel Corporation.
 * Copyright (C) 2015 Casey Schaufler <casey@schaufler-ca.com>
 * Copyright (C) 2016 Mellanox Techonologies
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Due to this file being licensed under the GPL there is controversy over
 *	whether this permits you to write a module that #includes this file
 *	without placing your module under the GPL.  Please consult a lawyer for
 *	advice before doing this.
 *
 */

#ifndef __LINUX_LSM_HOOKS_H
#define __LINUX_LSM_HOOKS_H

#include <uapi/linux/lsm.h>
#include <linux/security.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/xattr.h>
#include <linux/static_call.h>
#include <linux/unroll.h>
#include <linux/jump_label.h>
#include <linux/lsm_count.h>

union security_list_options {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...) RET (*NAME)(__VA_ARGS__);
	#include "lsm_hook_defs.h"
	#undef LSM_HOOK
	void *lsm_func_addr;
};

/*
 * @key: static call key as defined by STATIC_CALL_KEY
 * @trampoline: static call trampoline as defined by STATIC_CALL_TRAMP
 * @hl: The security_hook_list as initialized by the owning LSM.
 * @active: Enabled when the static call has an LSM hook associated.
 */
struct lsm_static_call {
	struct static_call_key *key;
	void *trampoline;
	struct security_hook_list *hl;
	/* this needs to be true or false based on what the key defaults to */
	struct static_key_false *active;
} __randomize_layout;

/*
 * Table of the static calls for each LSM hook.
 * Once the LSMs are initialized, their callbacks will be copied to these
 * tables such that the calls are filled backwards (from last to first).
 * This way, we can jump directly to the first used static call, and execute
 * all of them after. This essentially makes the entry point
 * dynamic to adapt the number of static calls to the number of callbacks.
 */
struct lsm_static_calls_table {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...) \
		struct lsm_static_call NAME[MAX_LSM_COUNT];
	#include <linux/lsm_hook_defs.h>
	#undef LSM_HOOK
} __packed __randomize_layout;

/**
 * struct lsm_id - Identify a Linux Security Module.
 * @name: name of the LSM, must be approved by the LSM maintainers
 * @id: LSM ID number from uapi/linux/lsm.h
 *
 * Contains the information that identifies the LSM.
 */
struct lsm_id {
	const char *name;
	u64 id;
};

/*
 * Security module hook list structure.
 * For use with generic list macros for common operations.
 *
 * struct security_hook_list - Contents of a cacheable, mappable object.
 * @scalls: The beginning of the array of static calls assigned to this hook.
 * @hook: The callback for the hook.
 * @lsm: The name of the lsm that owns this hook.
 */
struct security_hook_list {
	struct lsm_static_call *scalls;
	union security_list_options hook;
	const struct lsm_id *lsmid;
} __randomize_layout;

/*
 * Security blob size or offset data.
 */
struct lsm_blob_sizes {
	unsigned int lbs_cred;
	unsigned int lbs_file;
	unsigned int lbs_ib;
	unsigned int lbs_inode;
	unsigned int lbs_sock;
	unsigned int lbs_superblock;
	unsigned int lbs_ipc;
	unsigned int lbs_key;
	unsigned int lbs_msg_msg;
	unsigned int lbs_perf_event;
	unsigned int lbs_task;
	unsigned int lbs_xattr_count; /* num xattr slots in new_xattrs array */
	unsigned int lbs_tun_dev;
	unsigned int lbs_bdev;
	unsigned int lbs_bpf_map;
	unsigned int lbs_bpf_prog;
	unsigned int lbs_bpf_token;
};

/*
 * LSM_RET_VOID is used as the default value in LSM_HOOK definitions for void
 * LSM hooks (in include/linux/lsm_hook_defs.h).
 */
#define LSM_RET_VOID ((void) 0)

/*
 * Initializing a security_hook_list structure takes
 * up a lot of space in a source file. This macro takes
 * care of the common case and reduces the amount of
 * text involved.
 */
#define LSM_HOOK_INIT(NAME, HOOK)			\
	{						\
		.scalls = static_calls_table.NAME,	\
		.hook = { .NAME = HOOK }		\
	}

extern void security_add_hooks(struct security_hook_list *hooks, int count,
			       const struct lsm_id *lsmid);

#define LSM_FLAG_LEGACY_MAJOR	BIT(0)
#define LSM_FLAG_EXCLUSIVE	BIT(1)

enum lsm_order {
	LSM_ORDER_FIRST = -1,	/* This is only for capabilities. */
	LSM_ORDER_MUTABLE = 0,
	LSM_ORDER_LAST = 1,	/* This is only for integrity. */
};

/**
 * struct lsm_info - Define an individual LSM for the LSM framework.
 * @id: LSM name/ID info
 * @order: ordering with respect to other LSMs, optional
 * @flags: descriptive flags, optional
 * @blobs: LSM blob sharing, optional
 * @enabled: controlled by CONFIG_LSM, optional
 * @init: LSM specific initialization routine
 * @initcall_pure: LSM callback for initcall_pure() setup, optional
 * @initcall_early: LSM callback for early_initcall setup, optional
 * @initcall_core: LSM callback for core_initcall() setup, optional
 * @initcall_subsys: LSM callback for subsys_initcall() setup, optional
 * @initcall_fs: LSM callback for fs_initcall setup, optional
 * @initcall_device: LSM callback for device_initcall() setup, optional
 * @initcall_late: LSM callback for late_initcall() setup, optional
 */
struct lsm_info {
	const struct lsm_id *id;
	enum lsm_order order;
	unsigned long flags;
	struct lsm_blob_sizes *blobs;
	int *enabled;
	int (*init)(void);
	int (*initcall_pure)(void);
	int (*initcall_early)(void);
	int (*initcall_core)(void);
	int (*initcall_subsys)(void);
	int (*initcall_fs)(void);
	int (*initcall_device)(void);
	int (*initcall_late)(void);
};

#define DEFINE_LSM(lsm)							\
	static struct lsm_info __lsm_##lsm				\
		__used __section(".lsm_info.init")			\
		__aligned(sizeof(unsigned long))

#define DEFINE_EARLY_LSM(lsm)						\
	static struct lsm_info __early_lsm_##lsm			\
		__used __section(".early_lsm_info.init")		\
		__aligned(sizeof(unsigned long))


/* DO NOT tamper with these variables outside of the LSM framework */
extern struct lsm_static_calls_table static_calls_table __ro_after_init;

/**
 * lsm_get_xattr_slot - Return the next available slot and increment the index
 * @xattrs: array storing LSM-provided xattrs
 * @xattr_count: number of already stored xattrs (updated)
 *
 * Retrieve the first available slot in the @xattrs array to fill with an xattr,
 * and increment @xattr_count.
 *
 * Return: The slot to fill in @xattrs if non-NULL, NULL otherwise.
 */
static inline struct xattr *lsm_get_xattr_slot(struct xattr *xattrs,
					       int *xattr_count)
{
	if (unlikely(!xattrs))
		return NULL;
	return &xattrs[(*xattr_count)++];
}

#endif /* ! __LINUX_LSM_HOOKS_H */
