/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * ima_policy.c
 * 	- initialize default measure policy rules
 *
 */
#include <linux/module.h>
#include <linux/list.h>
#include <linux/audit.h>
#include <linux/security.h>
#include <linux/magic.h>

#include "ima.h"

/* flags definitions */
#define IMA_FUNC 	0x0001
#define IMA_MASK 	0x0002
#define IMA_FSMAGIC	0x0004
#define IMA_UID		0x0008

enum ima_action { DONT_MEASURE, MEASURE };

struct ima_measure_rule_entry {
	struct list_head list;
	enum ima_action action;
	unsigned int flags;
	enum ima_hooks func;
	int mask;
	unsigned long fsmagic;
	uid_t uid;
};

static struct ima_measure_rule_entry default_rules[] = {
	{.action = DONT_MEASURE,.fsmagic = PROC_SUPER_MAGIC,
	 .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = SYSFS_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = DEBUGFS_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = TMPFS_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = SECURITYFS_MAGIC,
	 .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = 0xF97CFF8C,.flags = IMA_FSMAGIC},
	{.action = MEASURE,.func = FILE_MMAP,.mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE,.func = BPRM_CHECK,.mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE,.func = PATH_CHECK,.mask = MAY_READ,.uid = 0,
	 .flags = IMA_FUNC | IMA_MASK | IMA_UID}
};

static LIST_HEAD(measure_default_rules);
static struct list_head *ima_measure;

/**
 * ima_match_rules - determine whether an inode matches the measure rule.
 * @rule: a pointer to a rule
 * @inode: a pointer to an inode
 * @func: LIM hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 *
 * Returns true on rule match, false on failure.
 */
static bool ima_match_rules(struct ima_measure_rule_entry *rule,
			    struct inode *inode, enum ima_hooks func, int mask)
{
	struct task_struct *tsk = current;

	if ((rule->flags & IMA_FUNC) && rule->func != func)
		return false;
	if ((rule->flags & IMA_MASK) && rule->mask != mask)
		return false;
	if ((rule->flags & IMA_FSMAGIC)
	    && rule->fsmagic != inode->i_sb->s_magic)
		return false;
	if ((rule->flags & IMA_UID) && rule->uid != tsk->cred->uid)
		return false;
	return true;
}

/**
 * ima_match_policy - decision based on LSM and other conditions
 * @inode: pointer to an inode for which the policy decision is being made
 * @func: IMA hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 *
 * Measure decision based on func/mask/fsmagic and LSM(subj/obj/type)
 * conditions.
 *
 * (There is no need for locking when walking the policy list,
 * as elements in the list are never deleted, nor does the list
 * change.)
 */
int ima_match_policy(struct inode *inode, enum ima_hooks func, int mask)
{
	struct ima_measure_rule_entry *entry;

	list_for_each_entry(entry, ima_measure, list) {
		bool rc;

		rc = ima_match_rules(entry, inode, func, mask);
		if (rc)
			return entry->action;
	}
	return 0;
}

/**
 * ima_init_policy - initialize the default measure rules.
 *
 * (Could use the default_rules directly, but in policy patch
 * ima_measure points to either the measure_default_rules or the
 * the new measure_policy_rules.)
 */
void ima_init_policy(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(default_rules); i++)
		list_add_tail(&default_rules[i].list, &measure_default_rules);
	ima_measure = &measure_default_rules;
}
