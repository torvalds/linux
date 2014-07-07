/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * ima_policy.c
 *	- initialize default measure policy rules
 *
 */
#include <linux/module.h>
#include <linux/list.h>
#include <linux/security.h>
#include <linux/magic.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/genhd.h>

#include "ima.h"

/* flags definitions */
#define IMA_FUNC	0x0001
#define IMA_MASK	0x0002
#define IMA_FSMAGIC	0x0004
#define IMA_UID		0x0008
#define IMA_FOWNER	0x0010
#define IMA_FSUUID	0x0020

#define UNKNOWN		0
#define MEASURE		0x0001	/* same as IMA_MEASURE */
#define DONT_MEASURE	0x0002
#define APPRAISE	0x0004	/* same as IMA_APPRAISE */
#define DONT_APPRAISE	0x0008
#define AUDIT		0x0040

#define MAX_LSM_RULES 6
enum lsm_rule_types { LSM_OBJ_USER, LSM_OBJ_ROLE, LSM_OBJ_TYPE,
	LSM_SUBJ_USER, LSM_SUBJ_ROLE, LSM_SUBJ_TYPE
};

struct ima_rule_entry {
	struct list_head list;
	int action;
	unsigned int flags;
	enum ima_hooks func;
	int mask;
	unsigned long fsmagic;
	u8 fsuuid[16];
	kuid_t uid;
	kuid_t fowner;
	struct {
		void *rule;	/* LSM file metadata specific */
		void *args_p;	/* audit value */
		int type;	/* audit type */
	} lsm[MAX_LSM_RULES];
};

/*
 * Without LSM specific knowledge, the default policy can only be
 * written in terms of .action, .func, .mask, .fsmagic, .uid, and .fowner
 */

/*
 * The minimum rule set to allow for full TCB coverage.  Measures all files
 * opened or mmap for exec and everything read by root.  Dangerous because
 * normal users can easily run the machine out of memory simply building
 * and running executables.
 */
static struct ima_rule_entry default_rules[] = {
	{.action = DONT_MEASURE, .fsmagic = PROC_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = SYSFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = DEBUGFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = TMPFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = DEVPTS_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = BINFMTFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = SECURITYFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = SELINUX_MAGIC, .flags = IMA_FSMAGIC},
	{.action = MEASURE, .func = MMAP_CHECK, .mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE, .func = BPRM_CHECK, .mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE, .func = FILE_CHECK, .mask = MAY_READ, .uid = GLOBAL_ROOT_UID,
	 .flags = IMA_FUNC | IMA_MASK | IMA_UID},
	{.action = MEASURE, .func = MODULE_CHECK, .flags = IMA_FUNC},
};

static struct ima_rule_entry default_appraise_rules[] = {
	{.action = DONT_APPRAISE, .fsmagic = PROC_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = SYSFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = DEBUGFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = TMPFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = RAMFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = DEVPTS_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = BINFMTFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = SECURITYFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = SELINUX_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = CGROUP_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = APPRAISE, .fowner = GLOBAL_ROOT_UID, .flags = IMA_FOWNER},
};

static LIST_HEAD(ima_default_rules);
static LIST_HEAD(ima_policy_rules);
static struct list_head *ima_rules;

static DEFINE_MUTEX(ima_rules_mutex);

static bool ima_use_tcb __initdata;
static int __init default_measure_policy_setup(char *str)
{
	ima_use_tcb = 1;
	return 1;
}
__setup("ima_tcb", default_measure_policy_setup);

static bool ima_use_appraise_tcb __initdata;
static int __init default_appraise_policy_setup(char *str)
{
	ima_use_appraise_tcb = 1;
	return 1;
}
__setup("ima_appraise_tcb", default_appraise_policy_setup);

/*
 * Although the IMA policy does not change, the LSM policy can be
 * reloaded, leaving the IMA LSM based rules referring to the old,
 * stale LSM policy.
 *
 * Update the IMA LSM based rules to reflect the reloaded LSM policy.
 * We assume the rules still exist; and BUG_ON() if they don't.
 */
static void ima_lsm_update_rules(void)
{
	struct ima_rule_entry *entry, *tmp;
	int result;
	int i;

	mutex_lock(&ima_rules_mutex);
	list_for_each_entry_safe(entry, tmp, &ima_policy_rules, list) {
		for (i = 0; i < MAX_LSM_RULES; i++) {
			if (!entry->lsm[i].rule)
				continue;
			result = security_filter_rule_init(entry->lsm[i].type,
							   Audit_equal,
							   entry->lsm[i].args_p,
							   &entry->lsm[i].rule);
			BUG_ON(!entry->lsm[i].rule);
		}
	}
	mutex_unlock(&ima_rules_mutex);
}

/**
 * ima_match_rules - determine whether an inode matches the measure rule.
 * @rule: a pointer to a rule
 * @inode: a pointer to an inode
 * @func: LIM hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 *
 * Returns true on rule match, false on failure.
 */
static bool ima_match_rules(struct ima_rule_entry *rule,
			    struct inode *inode, enum ima_hooks func, int mask)
{
	struct task_struct *tsk = current;
	const struct cred *cred = current_cred();
	int i;

	if ((rule->flags & IMA_FUNC) &&
	    (rule->func != func && func != POST_SETATTR))
		return false;
	if ((rule->flags & IMA_MASK) &&
	    (rule->mask != mask && func != POST_SETATTR))
		return false;
	if ((rule->flags & IMA_FSMAGIC)
	    && rule->fsmagic != inode->i_sb->s_magic)
		return false;
	if ((rule->flags & IMA_FSUUID) &&
	    memcmp(rule->fsuuid, inode->i_sb->s_uuid, sizeof(rule->fsuuid)))
		return false;
	if ((rule->flags & IMA_UID) && !uid_eq(rule->uid, cred->uid))
		return false;
	if ((rule->flags & IMA_FOWNER) && !uid_eq(rule->fowner, inode->i_uid))
		return false;
	for (i = 0; i < MAX_LSM_RULES; i++) {
		int rc = 0;
		u32 osid, sid;
		int retried = 0;

		if (!rule->lsm[i].rule)
			continue;
retry:
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
		if ((rc < 0) && (!retried)) {
			retried = 1;
			ima_lsm_update_rules();
			goto retry;
		}
		if (!rc)
			return false;
	}
	return true;
}

/*
 * In addition to knowing that we need to appraise the file in general,
 * we need to differentiate between calling hooks, for hook specific rules.
 */
static int get_subaction(struct ima_rule_entry *rule, int func)
{
	if (!(rule->flags & IMA_FUNC))
		return IMA_FILE_APPRAISE;

	switch (func) {
	case MMAP_CHECK:
		return IMA_MMAP_APPRAISE;
	case BPRM_CHECK:
		return IMA_BPRM_APPRAISE;
	case MODULE_CHECK:
		return IMA_MODULE_APPRAISE;
	case FILE_CHECK:
	default:
		return IMA_FILE_APPRAISE;
	}
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
int ima_match_policy(struct inode *inode, enum ima_hooks func, int mask,
		     int flags)
{
	struct ima_rule_entry *entry;
	int action = 0, actmask = flags | (flags << 1);

	list_for_each_entry(entry, ima_rules, list) {

		if (!(entry->action & actmask))
			continue;

		if (!ima_match_rules(entry, inode, func, mask))
			continue;

		action |= entry->flags & IMA_ACTION_FLAGS;

		action |= entry->action & IMA_DO_MASK;
		if (entry->action & IMA_APPRAISE)
			action |= get_subaction(entry, func);

		if (entry->action & IMA_DO_MASK)
			actmask &= ~(entry->action | entry->action << 1);
		else
			actmask &= ~(entry->action | entry->action >> 1);

		if (!actmask)
			break;
	}

	return action;
}

/**
 * ima_init_policy - initialize the default measure rules.
 *
 * ima_rules points to either the ima_default_rules or the
 * the new ima_policy_rules.
 */
void __init ima_init_policy(void)
{
	int i, measure_entries, appraise_entries;

	/* if !ima_use_tcb set entries = 0 so we load NO default rules */
	measure_entries = ima_use_tcb ? ARRAY_SIZE(default_rules) : 0;
	appraise_entries = ima_use_appraise_tcb ?
			 ARRAY_SIZE(default_appraise_rules) : 0;

	for (i = 0; i < measure_entries + appraise_entries; i++) {
		if (i < measure_entries)
			list_add_tail(&default_rules[i].list,
				      &ima_default_rules);
		else {
			int j = i - measure_entries;

			list_add_tail(&default_appraise_rules[j].list,
				      &ima_default_rules);
		}
	}

	ima_rules = &ima_default_rules;
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
	static const char op[] = "policy_update";
	const char *cause = "already exists";
	int result = 1;
	int audit_info = 0;

	if (ima_rules == &ima_default_rules) {
		ima_rules = &ima_policy_rules;
		cause = "complete";
		result = 0;
	}
	integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
			    NULL, op, cause, result, audit_info);
}

enum {
	Opt_err = -1,
	Opt_measure = 1, Opt_dont_measure,
	Opt_appraise, Opt_dont_appraise,
	Opt_audit,
	Opt_obj_user, Opt_obj_role, Opt_obj_type,
	Opt_subj_user, Opt_subj_role, Opt_subj_type,
	Opt_func, Opt_mask, Opt_fsmagic, Opt_uid, Opt_fowner,
	Opt_appraise_type, Opt_fsuuid, Opt_permit_directio
};

static match_table_t policy_tokens = {
	{Opt_measure, "measure"},
	{Opt_dont_measure, "dont_measure"},
	{Opt_appraise, "appraise"},
	{Opt_dont_appraise, "dont_appraise"},
	{Opt_audit, "audit"},
	{Opt_obj_user, "obj_user=%s"},
	{Opt_obj_role, "obj_role=%s"},
	{Opt_obj_type, "obj_type=%s"},
	{Opt_subj_user, "subj_user=%s"},
	{Opt_subj_role, "subj_role=%s"},
	{Opt_subj_type, "subj_type=%s"},
	{Opt_func, "func=%s"},
	{Opt_mask, "mask=%s"},
	{Opt_fsmagic, "fsmagic=%s"},
	{Opt_fsuuid, "fsuuid=%s"},
	{Opt_uid, "uid=%s"},
	{Opt_fowner, "fowner=%s"},
	{Opt_appraise_type, "appraise_type=%s"},
	{Opt_permit_directio, "permit_directio"},
	{Opt_err, NULL}
};

static int ima_lsm_rule_init(struct ima_rule_entry *entry,
			     substring_t *args, int lsm_rule, int audit_type)
{
	int result;

	if (entry->lsm[lsm_rule].rule)
		return -EINVAL;

	entry->lsm[lsm_rule].args_p = match_strdup(args);
	if (!entry->lsm[lsm_rule].args_p)
		return -ENOMEM;

	entry->lsm[lsm_rule].type = audit_type;
	result = security_filter_rule_init(entry->lsm[lsm_rule].type,
					   Audit_equal,
					   entry->lsm[lsm_rule].args_p,
					   &entry->lsm[lsm_rule].rule);
	if (!entry->lsm[lsm_rule].rule) {
		kfree(entry->lsm[lsm_rule].args_p);
		return -EINVAL;
	}

	return result;
}

static void ima_log_string(struct audit_buffer *ab, char *key, char *value)
{
	audit_log_format(ab, "%s=", key);
	audit_log_untrustedstring(ab, value);
	audit_log_format(ab, " ");
}

static int ima_parse_rule(char *rule, struct ima_rule_entry *entry)
{
	struct audit_buffer *ab;
	char *p;
	int result = 0;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_INTEGRITY_RULE);

	entry->uid = INVALID_UID;
	entry->fowner = INVALID_UID;
	entry->action = UNKNOWN;
	while ((p = strsep(&rule, " \t")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		unsigned long lnum;

		if (result < 0)
			break;
		if ((*p == '\0') || (*p == ' ') || (*p == '\t'))
			continue;
		token = match_token(p, policy_tokens, args);
		switch (token) {
		case Opt_measure:
			ima_log_string(ab, "action", "measure");

			if (entry->action != UNKNOWN)
				result = -EINVAL;

			entry->action = MEASURE;
			break;
		case Opt_dont_measure:
			ima_log_string(ab, "action", "dont_measure");

			if (entry->action != UNKNOWN)
				result = -EINVAL;

			entry->action = DONT_MEASURE;
			break;
		case Opt_appraise:
			ima_log_string(ab, "action", "appraise");

			if (entry->action != UNKNOWN)
				result = -EINVAL;

			entry->action = APPRAISE;
			break;
		case Opt_dont_appraise:
			ima_log_string(ab, "action", "dont_appraise");

			if (entry->action != UNKNOWN)
				result = -EINVAL;

			entry->action = DONT_APPRAISE;
			break;
		case Opt_audit:
			ima_log_string(ab, "action", "audit");

			if (entry->action != UNKNOWN)
				result = -EINVAL;

			entry->action = AUDIT;
			break;
		case Opt_func:
			ima_log_string(ab, "func", args[0].from);

			if (entry->func)
				result = -EINVAL;

			if (strcmp(args[0].from, "FILE_CHECK") == 0)
				entry->func = FILE_CHECK;
			/* PATH_CHECK is for backwards compat */
			else if (strcmp(args[0].from, "PATH_CHECK") == 0)
				entry->func = FILE_CHECK;
			else if (strcmp(args[0].from, "MODULE_CHECK") == 0)
				entry->func = MODULE_CHECK;
			else if ((strcmp(args[0].from, "FILE_MMAP") == 0)
				|| (strcmp(args[0].from, "MMAP_CHECK") == 0))
				entry->func = MMAP_CHECK;
			else if (strcmp(args[0].from, "BPRM_CHECK") == 0)
				entry->func = BPRM_CHECK;
			else
				result = -EINVAL;
			if (!result)
				entry->flags |= IMA_FUNC;
			break;
		case Opt_mask:
			ima_log_string(ab, "mask", args[0].from);

			if (entry->mask)
				result = -EINVAL;

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
			ima_log_string(ab, "fsmagic", args[0].from);

			if (entry->fsmagic) {
				result = -EINVAL;
				break;
			}

			result = kstrtoul(args[0].from, 16, &entry->fsmagic);
			if (!result)
				entry->flags |= IMA_FSMAGIC;
			break;
		case Opt_fsuuid:
			ima_log_string(ab, "fsuuid", args[0].from);

			if (memchr_inv(entry->fsuuid, 0x00,
				       sizeof(entry->fsuuid))) {
				result = -EINVAL;
				break;
			}

			result = blk_part_pack_uuid(args[0].from,
						    entry->fsuuid);
			if (!result)
				entry->flags |= IMA_FSUUID;
			break;
		case Opt_uid:
			ima_log_string(ab, "uid", args[0].from);

			if (uid_valid(entry->uid)) {
				result = -EINVAL;
				break;
			}

			result = kstrtoul(args[0].from, 10, &lnum);
			if (!result) {
				entry->uid = make_kuid(current_user_ns(), (uid_t)lnum);
				if (!uid_valid(entry->uid) || (((uid_t)lnum) != lnum))
					result = -EINVAL;
				else
					entry->flags |= IMA_UID;
			}
			break;
		case Opt_fowner:
			ima_log_string(ab, "fowner", args[0].from);

			if (uid_valid(entry->fowner)) {
				result = -EINVAL;
				break;
			}

			result = kstrtoul(args[0].from, 10, &lnum);
			if (!result) {
				entry->fowner = make_kuid(current_user_ns(), (uid_t)lnum);
				if (!uid_valid(entry->fowner) || (((uid_t)lnum) != lnum))
					result = -EINVAL;
				else
					entry->flags |= IMA_FOWNER;
			}
			break;
		case Opt_obj_user:
			ima_log_string(ab, "obj_user", args[0].from);
			result = ima_lsm_rule_init(entry, args,
						   LSM_OBJ_USER,
						   AUDIT_OBJ_USER);
			break;
		case Opt_obj_role:
			ima_log_string(ab, "obj_role", args[0].from);
			result = ima_lsm_rule_init(entry, args,
						   LSM_OBJ_ROLE,
						   AUDIT_OBJ_ROLE);
			break;
		case Opt_obj_type:
			ima_log_string(ab, "obj_type", args[0].from);
			result = ima_lsm_rule_init(entry, args,
						   LSM_OBJ_TYPE,
						   AUDIT_OBJ_TYPE);
			break;
		case Opt_subj_user:
			ima_log_string(ab, "subj_user", args[0].from);
			result = ima_lsm_rule_init(entry, args,
						   LSM_SUBJ_USER,
						   AUDIT_SUBJ_USER);
			break;
		case Opt_subj_role:
			ima_log_string(ab, "subj_role", args[0].from);
			result = ima_lsm_rule_init(entry, args,
						   LSM_SUBJ_ROLE,
						   AUDIT_SUBJ_ROLE);
			break;
		case Opt_subj_type:
			ima_log_string(ab, "subj_type", args[0].from);
			result = ima_lsm_rule_init(entry, args,
						   LSM_SUBJ_TYPE,
						   AUDIT_SUBJ_TYPE);
			break;
		case Opt_appraise_type:
			if (entry->action != APPRAISE) {
				result = -EINVAL;
				break;
			}

			ima_log_string(ab, "appraise_type", args[0].from);
			if ((strcmp(args[0].from, "imasig")) == 0)
				entry->flags |= IMA_DIGSIG_REQUIRED;
			else
				result = -EINVAL;
			break;
		case Opt_permit_directio:
			entry->flags |= IMA_PERMIT_DIRECTIO;
			break;
		case Opt_err:
			ima_log_string(ab, "UNKNOWN", p);
			result = -EINVAL;
			break;
		}
	}
	if (!result && (entry->action == UNKNOWN))
		result = -EINVAL;
	else if (entry->func == MODULE_CHECK)
		ima_appraise |= IMA_APPRAISE_MODULES;
	audit_log_format(ab, "res=%d", !result);
	audit_log_end(ab);
	return result;
}

/**
 * ima_parse_add_rule - add a rule to ima_policy_rules
 * @rule - ima measurement policy rule
 *
 * Uses a mutex to protect the policy list from multiple concurrent writers.
 * Returns the length of the rule parsed, an error code on failure
 */
ssize_t ima_parse_add_rule(char *rule)
{
	static const char op[] = "update_policy";
	char *p;
	struct ima_rule_entry *entry;
	ssize_t result, len;
	int audit_info = 0;

	/* Prevent installed policy from changing */
	if (ima_rules != &ima_default_rules) {
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

	p = strsep(&rule, "\n");
	len = strlen(p) + 1;

	if (*p == '#') {
		kfree(entry);
		return len;
	}

	result = ima_parse_rule(p, entry);
	if (result) {
		kfree(entry);
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, op, "invalid policy", result,
				    audit_info);
		return result;
	}

	mutex_lock(&ima_rules_mutex);
	list_add_tail(&entry->list, &ima_policy_rules);
	mutex_unlock(&ima_rules_mutex);

	return len;
}

/* ima_delete_rules called to cleanup invalid policy */
void ima_delete_rules(void)
{
	struct ima_rule_entry *entry, *tmp;
	int i;

	mutex_lock(&ima_rules_mutex);
	list_for_each_entry_safe(entry, tmp, &ima_policy_rules, list) {
		for (i = 0; i < MAX_LSM_RULES; i++)
			kfree(entry->lsm[i].args_p);

		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&ima_rules_mutex);
}
