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
#include <linux/security.h>
#include <linux/magic.h>
#include <linux/parser.h>

#include "ima.h"

/* flags definitions */
#define IMA_FUNC 	0x0001
#define IMA_MASK 	0x0002
#define IMA_FSMAGIC	0x0004
#define IMA_UID		0x0008

enum ima_action { UNKNOWN = -1, DONT_MEASURE = 0, MEASURE };

#define MAX_LSM_RULES 6
enum lsm_rule_types { LSM_OBJ_USER, LSM_OBJ_ROLE, LSM_OBJ_TYPE,
	LSM_SUBJ_USER, LSM_SUBJ_ROLE, LSM_SUBJ_TYPE
};

struct ima_measure_rule_entry {
	struct list_head list;
	enum ima_action action;
	unsigned int flags;
	enum ima_hooks func;
	int mask;
	unsigned long fsmagic;
	uid_t uid;
	struct {
		void *rule;	/* LSM file metadata specific */
		int type;	/* audit type */
	} lsm[MAX_LSM_RULES];
};

/*
 * Without LSM specific knowledge, the default policy can only be
 * written in terms of .action, .func, .mask, .fsmagic, and .uid
 */

/*
 * The minimum rule set to allow for full TCB coverage.  Measures all files
 * opened or mmap for exec and everything read by root.  Dangerous because
 * normal users can easily run the machine out of memory simply building
 * and running executables.
 */
static struct ima_measure_rule_entry default_rules[] = {
	{.action = DONT_MEASURE,.fsmagic = PROC_SUPER_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = SYSFS_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = DEBUGFS_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = TMPFS_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = SECURITYFS_MAGIC,.flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE,.fsmagic = SELINUX_MAGIC,.flags = IMA_FSMAGIC},
	{.action = MEASURE,.func = FILE_MMAP,.mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE,.func = BPRM_CHECK,.mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE,.func = PATH_CHECK,.mask = MAY_READ,.uid = 0,
	 .flags = IMA_FUNC | IMA_MASK | IMA_UID},
};

static LIST_HEAD(measure_default_rules);
static LIST_HEAD(measure_policy_rules);
static struct list_head *ima_measure;

static DEFINE_MUTEX(ima_measure_mutex);

static bool ima_use_tcb __initdata;
static int __init default_policy_setup(char *str)
{
	ima_use_tcb = 1;
	return 1;
}
__setup("ima_tcb", default_policy_setup);

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
	int i;

	if ((rule->flags & IMA_FUNC) && rule->func != func)
		return false;
	if ((rule->flags & IMA_MASK) && rule->mask != mask)
		return false;
	if ((rule->flags & IMA_FSMAGIC)
	    && rule->fsmagic != inode->i_sb->s_magic)
		return false;
	if ((rule->flags & IMA_UID) && rule->uid != tsk->cred->uid)
		return false;
	for (i = 0; i < MAX_LSM_RULES; i++) {
		int rc = 0;
		u32 osid, sid;

		if (!rule->lsm[i].rule)
			continue;

		switch (i) {
		case LSM_OBJ_USER:
		case LSM_OBJ_ROLE:
		case LSM_OBJ_TYPE:
			security_inode_getsecid(inode, &osid);
			rc = security_filter_rule_match(osid,
							rule->lsm[i].type,
							Audit_equal,
							rule->lsm[i].rule,
							NULL);
			break;
		case LSM_SUBJ_USER:
		case LSM_SUBJ_ROLE:
		case LSM_SUBJ_TYPE:
			security_task_getsecid(tsk, &sid);
			rc = security_filter_rule_match(sid,
							rule->lsm[i].type,
							Audit_equal,
							rule->lsm[i].rule,
							NULL);
		default:
			break;
		}
		if (!rc)
			return false;
	}
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
 * ima_measure points to either the measure_default_rules or the
 * the new measure_policy_rules.
 */
void __init ima_init_policy(void)
{
	int i, entries;

	/* if !ima_use_tcb set entries = 0 so we load NO default rules */
	if (ima_use_tcb)
		entries = ARRAY_SIZE(default_rules);
	else
		entries = 0;

	for (i = 0; i < entries; i++)
		list_add_tail(&default_rules[i].list, &measure_default_rules);
	ima_measure = &measure_default_rules;
}

/**
 * ima_update_policy - update default_rules with new measure rules
 *
 * Called on file .release to update the default rules with a complete new
 * policy.  Once updated, the policy is locked, no additional rules can be
 * added to the policy.
 */
void ima_update_policy(void)
{
	const char *op = "policy_update";
	const char *cause = "already exists";
	int result = 1;
	int audit_info = 0;

	if (ima_measure == &measure_default_rules) {
		ima_measure = &measure_policy_rules;
		cause = "complete";
		result = 0;
	}
	integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
			    NULL, op, cause, result, audit_info);
}

enum {
	Opt_err = -1,
	Opt_measure = 1, Opt_dont_measure,
	Opt_obj_user, Opt_obj_role, Opt_obj_type,
	Opt_subj_user, Opt_subj_role, Opt_subj_type,
	Opt_func, Opt_mask, Opt_fsmagic, Opt_uid
};

static match_table_t policy_tokens = {
	{Opt_measure, "measure"},
	{Opt_dont_measure, "dont_measure"},
	{Opt_obj_user, "obj_user=%s"},
	{Opt_obj_role, "obj_role=%s"},
	{Opt_obj_type, "obj_type=%s"},
	{Opt_subj_user, "subj_user=%s"},
	{Opt_subj_role, "subj_role=%s"},
	{Opt_subj_type, "subj_type=%s"},
	{Opt_func, "func=%s"},
	{Opt_mask, "mask=%s"},
	{Opt_fsmagic, "fsmagic=%s"},
	{Opt_uid, "uid=%s"},
	{Opt_err, NULL}
};

static int ima_lsm_rule_init(struct ima_measure_rule_entry *entry,
			     char *args, int lsm_rule, int audit_type)
{
	int result;

	entry->lsm[lsm_rule].type = audit_type;
	result = security_filter_rule_init(entry->lsm[lsm_rule].type,
					   Audit_equal, args,
					   &entry->lsm[lsm_rule].rule);
	if (!entry->lsm[lsm_rule].rule)
		return -EINVAL;
	return result;
}

static int ima_parse_rule(char *rule, struct ima_measure_rule_entry *entry)
{
	struct audit_buffer *ab;
	char *p;
	int result = 0;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_INTEGRITY_RULE);

	entry->action = -1;
	while ((p = strsep(&rule, " \n")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		unsigned long lnum;

		if (result < 0)
			break;
		if (!*p)
			continue;
		token = match_token(p, policy_tokens, args);
		switch (token) {
		case Opt_measure:
			audit_log_format(ab, "%s ", "measure");
			entry->action = MEASURE;
			break;
		case Opt_dont_measure:
			audit_log_format(ab, "%s ", "dont_measure");
			entry->action = DONT_MEASURE;
			break;
		case Opt_func:
			audit_log_format(ab, "func=%s ", args[0].from);
			if (strcmp(args[0].from, "PATH_CHECK") == 0)
				entry->func = PATH_CHECK;
			else if (strcmp(args[0].from, "FILE_MMAP") == 0)
				entry->func = FILE_MMAP;
			else if (strcmp(args[0].from, "BPRM_CHECK") == 0)
				entry->func = BPRM_CHECK;
			else
				result = -EINVAL;
			if (!result)
				entry->flags |= IMA_FUNC;
			break;
		case Opt_mask:
			audit_log_format(ab, "mask=%s ", args[0].from);
			if ((strcmp(args[0].from, "MAY_EXEC")) == 0)
				entry->mask = MAY_EXEC;
			else if (strcmp(args[0].from, "MAY_WRITE") == 0)
				entry->mask = MAY_WRITE;
			else if (strcmp(args[0].from, "MAY_READ") == 0)
				entry->mask = MAY_READ;
			else if (strcmp(args[0].from, "MAY_APPEND") == 0)
				entry->mask = MAY_APPEND;
			else
				result = -EINVAL;
			if (!result)
				entry->flags |= IMA_MASK;
			break;
		case Opt_fsmagic:
			audit_log_format(ab, "fsmagic=%s ", args[0].from);
			result = strict_strtoul(args[0].from, 16,
						&entry->fsmagic);
			if (!result)
				entry->flags |= IMA_FSMAGIC;
			break;
		case Opt_uid:
			audit_log_format(ab, "uid=%s ", args[0].from);
			result = strict_strtoul(args[0].from, 10, &lnum);
			if (!result) {
				entry->uid = (uid_t) lnum;
				if (entry->uid != lnum)
					result = -EINVAL;
				else
					entry->flags |= IMA_UID;
			}
			break;
		case Opt_obj_user:
			audit_log_format(ab, "obj_user=%s ", args[0].from);
			result = ima_lsm_rule_init(entry, args[0].from,
						   LSM_OBJ_USER,
						   AUDIT_OBJ_USER);
			break;
		case Opt_obj_role:
			audit_log_format(ab, "obj_role=%s ", args[0].from);
			result = ima_lsm_rule_init(entry, args[0].from,
						   LSM_OBJ_ROLE,
						   AUDIT_OBJ_ROLE);
			break;
		case Opt_obj_type:
			audit_log_format(ab, "obj_type=%s ", args[0].from);
			result = ima_lsm_rule_init(entry, args[0].from,
						   LSM_OBJ_TYPE,
						   AUDIT_OBJ_TYPE);
			break;
		case Opt_subj_user:
			audit_log_format(ab, "subj_user=%s ", args[0].from);
			result = ima_lsm_rule_init(entry, args[0].from,
						   LSM_SUBJ_USER,
						   AUDIT_SUBJ_USER);
			break;
		case Opt_subj_role:
			audit_log_format(ab, "subj_role=%s ", args[0].from);
			result = ima_lsm_rule_init(entry, args[0].from,
						   LSM_SUBJ_ROLE,
						   AUDIT_SUBJ_ROLE);
			break;
		case Opt_subj_type:
			audit_log_format(ab, "subj_type=%s ", args[0].from);
			result = ima_lsm_rule_init(entry, args[0].from,
						   LSM_SUBJ_TYPE,
						   AUDIT_SUBJ_TYPE);
			break;
		case Opt_err:
			audit_log_format(ab, "UNKNOWN=%s ", p);
			break;
		}
	}
	if (entry->action == UNKNOWN)
		result = -EINVAL;

	audit_log_format(ab, "res=%d", !result ? 0 : 1);
	audit_log_end(ab);
	return result;
}

/**
 * ima_parse_add_rule - add a rule to measure_policy_rules
 * @rule - ima measurement policy rule
 *
 * Uses a mutex to protect the policy list from multiple concurrent writers.
 * Returns 0 on success, an error code on failure.
 */
int ima_parse_add_rule(char *rule)
{
	const char *op = "update_policy";
	struct ima_measure_rule_entry *entry;
	int result = 0;
	int audit_info = 0;

	/* Prevent installed policy from changing */
	if (ima_measure != &measure_default_rules) {
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, op, "already exists",
				    -EACCES, audit_info);
		return -EACCES;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, op, "-ENOMEM", -ENOMEM, audit_info);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&entry->list);

	result = ima_parse_rule(rule, entry);
	if (!result) {
		mutex_lock(&ima_measure_mutex);
		list_add_tail(&entry->list, &measure_policy_rules);
		mutex_unlock(&ima_measure_mutex);
	} else {
		kfree(entry);
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, op, "invalid policy", result,
				    audit_info);
	}
	return result;
}

/* ima_delete_rules called to cleanup invalid policy */
void ima_delete_rules(void)
{
	struct ima_measure_rule_entry *entry, *tmp;

	mutex_lock(&ima_measure_mutex);
	list_for_each_entry_safe(entry, tmp, &measure_policy_rules, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&ima_measure_mutex);
}
