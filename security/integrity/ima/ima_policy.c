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
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/magic.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include <linux/genhd.h>
#include <linux/seq_file.h>

#include "ima.h"

/* flags definitions */
#define IMA_FUNC	0x0001
#define IMA_MASK	0x0002
#define IMA_FSMAGIC	0x0004
#define IMA_UID		0x0008
#define IMA_FOWNER	0x0010
#define IMA_FSUUID	0x0020
#define IMA_INMASK	0x0040
#define IMA_EUID	0x0080
#define IMA_PCR		0x0100
#define IMA_FSNAME	0x0200

#define UNKNOWN		0
#define MEASURE		0x0001	/* same as IMA_MEASURE */
#define DONT_MEASURE	0x0002
#define APPRAISE	0x0004	/* same as IMA_APPRAISE */
#define DONT_APPRAISE	0x0008
#define AUDIT		0x0040
#define HASH		0x0100
#define DONT_HASH	0x0200

#define INVALID_PCR(a) (((a) < 0) || \
	(a) >= (FIELD_SIZEOF(struct integrity_iint_cache, measured_pcrs) * 8))

int ima_policy_flag;
static int temp_ima_appraise;

#define MAX_LSM_RULES 6
enum lsm_rule_types { LSM_OBJ_USER, LSM_OBJ_ROLE, LSM_OBJ_TYPE,
	LSM_SUBJ_USER, LSM_SUBJ_ROLE, LSM_SUBJ_TYPE
};

enum policy_types { ORIGINAL_TCB = 1, DEFAULT_TCB };

struct ima_rule_entry {
	struct list_head list;
	int action;
	unsigned int flags;
	enum ima_hooks func;
	int mask;
	unsigned long fsmagic;
	uuid_t fsuuid;
	kuid_t uid;
	kuid_t fowner;
	bool (*uid_op)(kuid_t, kuid_t);    /* Handlers for operators       */
	bool (*fowner_op)(kuid_t, kuid_t); /* uid_eq(), uid_gt(), uid_lt() */
	int pcr;
	struct {
		void *rule;	/* LSM file metadata specific */
		void *args_p;	/* audit value */
		int type;	/* audit type */
	} lsm[MAX_LSM_RULES];
	char *fsname;
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
static struct ima_rule_entry dont_measure_rules[] __ro_after_init = {
	{.action = DONT_MEASURE, .fsmagic = PROC_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = SYSFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = DEBUGFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = TMPFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = DEVPTS_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = BINFMTFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = SECURITYFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = SELINUX_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = SMACK_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = CGROUP_SUPER_MAGIC,
	 .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = CGROUP2_SUPER_MAGIC,
	 .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = NSFS_MAGIC, .flags = IMA_FSMAGIC}
};

static struct ima_rule_entry original_measurement_rules[] __ro_after_init = {
	{.action = MEASURE, .func = MMAP_CHECK, .mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE, .func = BPRM_CHECK, .mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE, .func = FILE_CHECK, .mask = MAY_READ,
	 .uid = GLOBAL_ROOT_UID, .uid_op = &uid_eq,
	 .flags = IMA_FUNC | IMA_MASK | IMA_UID},
	{.action = MEASURE, .func = MODULE_CHECK, .flags = IMA_FUNC},
	{.action = MEASURE, .func = FIRMWARE_CHECK, .flags = IMA_FUNC},
};

static struct ima_rule_entry default_measurement_rules[] __ro_after_init = {
	{.action = MEASURE, .func = MMAP_CHECK, .mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE, .func = BPRM_CHECK, .mask = MAY_EXEC,
	 .flags = IMA_FUNC | IMA_MASK},
	{.action = MEASURE, .func = FILE_CHECK, .mask = MAY_READ,
	 .uid = GLOBAL_ROOT_UID, .uid_op = &uid_eq,
	 .flags = IMA_FUNC | IMA_INMASK | IMA_EUID},
	{.action = MEASURE, .func = FILE_CHECK, .mask = MAY_READ,
	 .uid = GLOBAL_ROOT_UID, .uid_op = &uid_eq,
	 .flags = IMA_FUNC | IMA_INMASK | IMA_UID},
	{.action = MEASURE, .func = MODULE_CHECK, .flags = IMA_FUNC},
	{.action = MEASURE, .func = FIRMWARE_CHECK, .flags = IMA_FUNC},
	{.action = MEASURE, .func = POLICY_CHECK, .flags = IMA_FUNC},
};

static struct ima_rule_entry default_appraise_rules[] __ro_after_init = {
	{.action = DONT_APPRAISE, .fsmagic = PROC_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = SYSFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = DEBUGFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = TMPFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = RAMFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = DEVPTS_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = BINFMTFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = SECURITYFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = SELINUX_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = SMACK_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = NSFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = CGROUP_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = CGROUP2_SUPER_MAGIC, .flags = IMA_FSMAGIC},
#ifdef CONFIG_IMA_WRITE_POLICY
	{.action = APPRAISE, .func = POLICY_CHECK,
	.flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
#endif
#ifndef CONFIG_IMA_APPRAISE_SIGNED_INIT
	{.action = APPRAISE, .fowner = GLOBAL_ROOT_UID, .fowner_op = &uid_eq,
	 .flags = IMA_FOWNER},
#else
	/* force signature */
	{.action = APPRAISE, .fowner = GLOBAL_ROOT_UID, .fowner_op = &uid_eq,
	 .flags = IMA_FOWNER | IMA_DIGSIG_REQUIRED},
#endif
};

static struct ima_rule_entry secure_boot_rules[] __ro_after_init = {
	{.action = APPRAISE, .func = MODULE_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
	{.action = APPRAISE, .func = FIRMWARE_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
	{.action = APPRAISE, .func = KEXEC_KERNEL_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
	{.action = APPRAISE, .func = POLICY_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
};

static LIST_HEAD(ima_default_rules);
static LIST_HEAD(ima_policy_rules);
static LIST_HEAD(ima_temp_rules);
static struct list_head *ima_rules;

static int ima_policy __initdata;

static int __init default_measure_policy_setup(char *str)
{
	if (ima_policy)
		return 1;

	ima_policy = ORIGINAL_TCB;
	return 1;
}
__setup("ima_tcb", default_measure_policy_setup);

static bool ima_use_appraise_tcb __initdata;
static bool ima_use_secure_boot __initdata;
static bool ima_fail_unverifiable_sigs __ro_after_init;
static int __init policy_setup(char *str)
{
	char *p;

	while ((p = strsep(&str, " |\n")) != NULL) {
		if (*p == ' ')
			continue;
		if ((strcmp(p, "tcb") == 0) && !ima_policy)
			ima_policy = DEFAULT_TCB;
		else if (strcmp(p, "appraise_tcb") == 0)
			ima_use_appraise_tcb = true;
		else if (strcmp(p, "secure_boot") == 0)
			ima_use_secure_boot = true;
		else if (strcmp(p, "fail_securely") == 0)
			ima_fail_unverifiable_sigs = true;
	}

	return 1;
}
__setup("ima_policy=", policy_setup);

static int __init default_appraise_policy_setup(char *str)
{
	ima_use_appraise_tcb = true;
	return 1;
}
__setup("ima_appraise_tcb", default_appraise_policy_setup);

/*
 * The LSM policy can be reloaded, leaving the IMA LSM based rules referring
 * to the old, stale LSM policy.  Update the IMA LSM based rules to reflect
 * the reloaded LSM policy.  We assume the rules still exist; and BUG_ON() if
 * they don't.
 */
static void ima_lsm_update_rules(void)
{
	struct ima_rule_entry *entry;
	int result;
	int i;

	list_for_each_entry(entry, &ima_policy_rules, list) {
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
}

/**
 * ima_match_rules - determine whether an inode matches the measure rule.
 * @rule: a pointer to a rule
 * @inode: a pointer to an inode
 * @cred: a pointer to a credentials structure for user validation
 * @secid: the secid of the task to be validated
 * @func: LIM hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 *
 * Returns true on rule match, false on failure.
 */
static bool ima_match_rules(struct ima_rule_entry *rule, struct inode *inode,
			    const struct cred *cred, u32 secid,
			    enum ima_hooks func, int mask)
{
	int i;

	if ((rule->flags & IMA_FUNC) &&
	    (rule->func != func && func != POST_SETATTR))
		return false;
	if ((rule->flags & IMA_MASK) &&
	    (rule->mask != mask && func != POST_SETATTR))
		return false;
	if ((rule->flags & IMA_INMASK) &&
	    (!(rule->mask & mask) && func != POST_SETATTR))
		return false;
	if ((rule->flags & IMA_FSMAGIC)
	    && rule->fsmagic != inode->i_sb->s_magic)
		return false;
	if ((rule->flags & IMA_FSNAME)
	    && strcmp(rule->fsname, inode->i_sb->s_type->name))
		return false;
	if ((rule->flags & IMA_FSUUID) &&
	    !uuid_equal(&rule->fsuuid, &inode->i_sb->s_uuid))
		return false;
	if ((rule->flags & IMA_UID) && !rule->uid_op(cred->uid, rule->uid))
		return false;
	if (rule->flags & IMA_EUID) {
		if (has_capability_noaudit(current, CAP_SETUID)) {
			if (!rule->uid_op(cred->euid, rule->uid)
			    && !rule->uid_op(cred->suid, rule->uid)
			    && !rule->uid_op(cred->uid, rule->uid))
				return false;
		} else if (!rule->uid_op(cred->euid, rule->uid))
			return false;
	}

	if ((rule->flags & IMA_FOWNER) &&
	    !rule->fowner_op(inode->i_uid, rule->fowner))
		return false;
	for (i = 0; i < MAX_LSM_RULES; i++) {
		int rc = 0;
		u32 osid;
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
			rc = security_filter_rule_match(secid,
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
static int get_subaction(struct ima_rule_entry *rule, enum ima_hooks func)
{
	if (!(rule->flags & IMA_FUNC))
		return IMA_FILE_APPRAISE;

	switch (func) {
	case MMAP_CHECK:
		return IMA_MMAP_APPRAISE;
	case BPRM_CHECK:
		return IMA_BPRM_APPRAISE;
	case CREDS_CHECK:
		return IMA_CREDS_APPRAISE;
	case FILE_CHECK:
	case POST_SETATTR:
		return IMA_FILE_APPRAISE;
	case MODULE_CHECK ... MAX_CHECK - 1:
	default:
		return IMA_READ_APPRAISE;
	}
}

/**
 * ima_match_policy - decision based on LSM and other conditions
 * @inode: pointer to an inode for which the policy decision is being made
 * @cred: pointer to a credentials structure for which the policy decision is
 *        being made
 * @secid: LSM secid of the task to be validated
 * @func: IMA hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 * @pcr: set the pcr to extend
 *
 * Measure decision based on func/mask/fsmagic and LSM(subj/obj/type)
 * conditions.
 *
 * Since the IMA policy may be updated multiple times we need to lock the
 * list when walking it.  Reads are many orders of magnitude more numerous
 * than writes so ima_match_policy() is classical RCU candidate.
 */
int ima_match_policy(struct inode *inode, const struct cred *cred, u32 secid,
		     enum ima_hooks func, int mask, int flags, int *pcr)
{
	struct ima_rule_entry *entry;
	int action = 0, actmask = flags | (flags << 1);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, ima_rules, list) {

		if (!(entry->action & actmask))
			continue;

		if (!ima_match_rules(entry, inode, cred, secid, func, mask))
			continue;

		action |= entry->flags & IMA_ACTION_FLAGS;

		action |= entry->action & IMA_DO_MASK;
		if (entry->action & IMA_APPRAISE) {
			action |= get_subaction(entry, func);
			action &= ~IMA_HASH;
			if (ima_fail_unverifiable_sigs)
				action |= IMA_FAIL_UNVERIFIABLE_SIGS;
		}

		if (entry->action & IMA_DO_MASK)
			actmask &= ~(entry->action | entry->action << 1);
		else
			actmask &= ~(entry->action | entry->action >> 1);

		if ((pcr) && (entry->flags & IMA_PCR))
			*pcr = entry->pcr;

		if (!actmask)
			break;
	}
	rcu_read_unlock();

	return action;
}

/*
 * Initialize the ima_policy_flag variable based on the currently
 * loaded policy.  Based on this flag, the decision to short circuit
 * out of a function or not call the function in the first place
 * can be made earlier.
 */
void ima_update_policy_flag(void)
{
	struct ima_rule_entry *entry;

	list_for_each_entry(entry, ima_rules, list) {
		if (entry->action & IMA_DO_MASK)
			ima_policy_flag |= entry->action;
	}

	ima_appraise |= temp_ima_appraise;
	if (!ima_appraise)
		ima_policy_flag &= ~IMA_APPRAISE;
}

static int ima_appraise_flag(enum ima_hooks func)
{
	if (func == MODULE_CHECK)
		return IMA_APPRAISE_MODULES;
	else if (func == FIRMWARE_CHECK)
		return IMA_APPRAISE_FIRMWARE;
	else if (func == POLICY_CHECK)
		return IMA_APPRAISE_POLICY;
	return 0;
}

/**
 * ima_init_policy - initialize the default measure rules.
 *
 * ima_rules points to either the ima_default_rules or the
 * the new ima_policy_rules.
 */
void __init ima_init_policy(void)
{
	int i, measure_entries, appraise_entries, secure_boot_entries;

	/* if !ima_policy set entries = 0 so we load NO default rules */
	measure_entries = ima_policy ? ARRAY_SIZE(dont_measure_rules) : 0;
	appraise_entries = ima_use_appraise_tcb ?
			 ARRAY_SIZE(default_appraise_rules) : 0;
	secure_boot_entries = ima_use_secure_boot ?
			ARRAY_SIZE(secure_boot_rules) : 0;

	for (i = 0; i < measure_entries; i++)
		list_add_tail(&dont_measure_rules[i].list, &ima_default_rules);

	switch (ima_policy) {
	case ORIGINAL_TCB:
		for (i = 0; i < ARRAY_SIZE(original_measurement_rules); i++)
			list_add_tail(&original_measurement_rules[i].list,
				      &ima_default_rules);
		break;
	case DEFAULT_TCB:
		for (i = 0; i < ARRAY_SIZE(default_measurement_rules); i++)
			list_add_tail(&default_measurement_rules[i].list,
				      &ima_default_rules);
	default:
		break;
	}

	/*
	 * Insert the appraise rules requiring file signatures, prior to
	 * any other appraise rules.
	 */
	for (i = 0; i < secure_boot_entries; i++) {
		list_add_tail(&secure_boot_rules[i].list, &ima_default_rules);
		temp_ima_appraise |=
		    ima_appraise_flag(secure_boot_rules[i].func);
	}

	for (i = 0; i < appraise_entries; i++) {
		list_add_tail(&default_appraise_rules[i].list,
			      &ima_default_rules);
		if (default_appraise_rules[i].func == POLICY_CHECK)
			temp_ima_appraise |= IMA_APPRAISE_POLICY;
	}

	ima_rules = &ima_default_rules;
	ima_update_policy_flag();
}

/* Make sure we have a valid policy, at least containing some rules. */
int ima_check_policy(void)
{
	if (list_empty(&ima_temp_rules))
		return -EINVAL;
	return 0;
}

/**
 * ima_update_policy - update default_rules with new measure rules
 *
 * Called on file .release to update the default rules with a complete new
 * policy.  What we do here is to splice ima_policy_rules and ima_temp_rules so
 * they make a queue.  The policy may be updated multiple times and this is the
 * RCU updater.
 *
 * Policy rules are never deleted so ima_policy_flag gets zeroed only once when
 * we switch from the default policy to user defined.
 */
void ima_update_policy(void)
{
	struct list_head *policy = &ima_policy_rules;

	list_splice_tail_init_rcu(&ima_temp_rules, policy, synchronize_rcu);

	if (ima_rules != policy) {
		ima_policy_flag = 0;
		ima_rules = policy;
	}
	ima_update_policy_flag();
}

enum {
	Opt_err = -1,
	Opt_measure = 1, Opt_dont_measure,
	Opt_appraise, Opt_dont_appraise,
	Opt_audit, Opt_hash, Opt_dont_hash,
	Opt_obj_user, Opt_obj_role, Opt_obj_type,
	Opt_subj_user, Opt_subj_role, Opt_subj_type,
	Opt_func, Opt_mask, Opt_fsmagic, Opt_fsname,
	Opt_fsuuid, Opt_uid_eq, Opt_euid_eq, Opt_fowner_eq,
	Opt_uid_gt, Opt_euid_gt, Opt_fowner_gt,
	Opt_uid_lt, Opt_euid_lt, Opt_fowner_lt,
	Opt_appraise_type, Opt_permit_directio,
	Opt_pcr
};

static match_table_t policy_tokens = {
	{Opt_measure, "measure"},
	{Opt_dont_measure, "dont_measure"},
	{Opt_appraise, "appraise"},
	{Opt_dont_appraise, "dont_appraise"},
	{Opt_audit, "audit"},
	{Opt_hash, "hash"},
	{Opt_dont_hash, "dont_hash"},
	{Opt_obj_user, "obj_user=%s"},
	{Opt_obj_role, "obj_role=%s"},
	{Opt_obj_type, "obj_type=%s"},
	{Opt_subj_user, "subj_user=%s"},
	{Opt_subj_role, "subj_role=%s"},
	{Opt_subj_type, "subj_type=%s"},
	{Opt_func, "func=%s"},
	{Opt_mask, "mask=%s"},
	{Opt_fsmagic, "fsmagic=%s"},
	{Opt_fsname, "fsname=%s"},
	{Opt_fsuuid, "fsuuid=%s"},
	{Opt_uid_eq, "uid=%s"},
	{Opt_euid_eq, "euid=%s"},
	{Opt_fowner_eq, "fowner=%s"},
	{Opt_uid_gt, "uid>%s"},
	{Opt_euid_gt, "euid>%s"},
	{Opt_fowner_gt, "fowner>%s"},
	{Opt_uid_lt, "uid<%s"},
	{Opt_euid_lt, "euid<%s"},
	{Opt_fowner_lt, "fowner<%s"},
	{Opt_appraise_type, "appraise_type=%s"},
	{Opt_permit_directio, "permit_directio"},
	{Opt_pcr, "pcr=%s"},
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

static void ima_log_string_op(struct audit_buffer *ab, char *key, char *value,
			      bool (*rule_operator)(kuid_t, kuid_t))
{
	if (rule_operator == &uid_gt)
		audit_log_format(ab, "%s>", key);
	else if (rule_operator == &uid_lt)
		audit_log_format(ab, "%s<", key);
	else
		audit_log_format(ab, "%s=", key);
	audit_log_untrustedstring(ab, value);
	audit_log_format(ab, " ");
}
static void ima_log_string(struct audit_buffer *ab, char *key, char *value)
{
	ima_log_string_op(ab, key, value, NULL);
}

static int ima_parse_rule(char *rule, struct ima_rule_entry *entry)
{
	struct audit_buffer *ab;
	char *from;
	char *p;
	bool uid_token;
	int result = 0;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_INTEGRITY_RULE);

	entry->uid = INVALID_UID;
	entry->fowner = INVALID_UID;
	entry->uid_op = &uid_eq;
	entry->fowner_op = &uid_eq;
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
		case Opt_hash:
			ima_log_string(ab, "action", "hash");

			if (entry->action != UNKNOWN)
				result = -EINVAL;

			entry->action = HASH;
			break;
		case Opt_dont_hash:
			ima_log_string(ab, "action", "dont_hash");

			if (entry->action != UNKNOWN)
				result = -EINVAL;

			entry->action = DONT_HASH;
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
			else if (strcmp(args[0].from, "FIRMWARE_CHECK") == 0)
				entry->func = FIRMWARE_CHECK;
			else if ((strcmp(args[0].from, "FILE_MMAP") == 0)
				|| (strcmp(args[0].from, "MMAP_CHECK") == 0))
				entry->func = MMAP_CHECK;
			else if (strcmp(args[0].from, "BPRM_CHECK") == 0)
				entry->func = BPRM_CHECK;
			else if (strcmp(args[0].from, "CREDS_CHECK") == 0)
				entry->func = CREDS_CHECK;
			else if (strcmp(args[0].from, "KEXEC_KERNEL_CHECK") ==
				 0)
				entry->func = KEXEC_KERNEL_CHECK;
			else if (strcmp(args[0].from, "KEXEC_INITRAMFS_CHECK")
				 == 0)
				entry->func = KEXEC_INITRAMFS_CHECK;
			else if (strcmp(args[0].from, "POLICY_CHECK") == 0)
				entry->func = POLICY_CHECK;
			else
				result = -EINVAL;
			if (!result)
				entry->flags |= IMA_FUNC;
			break;
		case Opt_mask:
			ima_log_string(ab, "mask", args[0].from);

			if (entry->mask)
				result = -EINVAL;

			from = args[0].from;
			if (*from == '^')
				from++;

			if ((strcmp(from, "MAY_EXEC")) == 0)
				entry->mask = MAY_EXEC;
			else if (strcmp(from, "MAY_WRITE") == 0)
				entry->mask = MAY_WRITE;
			else if (strcmp(from, "MAY_READ") == 0)
				entry->mask = MAY_READ;
			else if (strcmp(from, "MAY_APPEND") == 0)
				entry->mask = MAY_APPEND;
			else
				result = -EINVAL;
			if (!result)
				entry->flags |= (*args[0].from == '^')
				     ? IMA_INMASK : IMA_MASK;
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
		case Opt_fsname:
			ima_log_string(ab, "fsname", args[0].from);

			entry->fsname = kstrdup(args[0].from, GFP_KERNEL);
			if (!entry->fsname) {
				result = -ENOMEM;
				break;
			}
			result = 0;
			entry->flags |= IMA_FSNAME;
			break;
		case Opt_fsuuid:
			ima_log_string(ab, "fsuuid", args[0].from);

			if (!uuid_is_null(&entry->fsuuid)) {
				result = -EINVAL;
				break;
			}

			result = uuid_parse(args[0].from, &entry->fsuuid);
			if (!result)
				entry->flags |= IMA_FSUUID;
			break;
		case Opt_uid_gt:
		case Opt_euid_gt:
			entry->uid_op = &uid_gt;
		case Opt_uid_lt:
		case Opt_euid_lt:
			if ((token == Opt_uid_lt) || (token == Opt_euid_lt))
				entry->uid_op = &uid_lt;
		case Opt_uid_eq:
		case Opt_euid_eq:
			uid_token = (token == Opt_uid_eq) ||
				    (token == Opt_uid_gt) ||
				    (token == Opt_uid_lt);

			ima_log_string_op(ab, uid_token ? "uid" : "euid",
					  args[0].from, entry->uid_op);

			if (uid_valid(entry->uid)) {
				result = -EINVAL;
				break;
			}

			result = kstrtoul(args[0].from, 10, &lnum);
			if (!result) {
				entry->uid = make_kuid(current_user_ns(),
						       (uid_t) lnum);
				if (!uid_valid(entry->uid) ||
				    (uid_t)lnum != lnum)
					result = -EINVAL;
				else
					entry->flags |= uid_token
					    ? IMA_UID : IMA_EUID;
			}
			break;
		case Opt_fowner_gt:
			entry->fowner_op = &uid_gt;
		case Opt_fowner_lt:
			if (token == Opt_fowner_lt)
				entry->fowner_op = &uid_lt;
		case Opt_fowner_eq:
			ima_log_string_op(ab, "fowner", args[0].from,
					  entry->fowner_op);

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
		case Opt_pcr:
			if (entry->action != MEASURE) {
				result = -EINVAL;
				break;
			}
			ima_log_string(ab, "pcr", args[0].from);

			result = kstrtoint(args[0].from, 10, &entry->pcr);
			if (result || INVALID_PCR(entry->pcr))
				result = -EINVAL;
			else
				entry->flags |= IMA_PCR;

			break;
		case Opt_err:
			ima_log_string(ab, "UNKNOWN", p);
			result = -EINVAL;
			break;
		}
	}
	if (!result && (entry->action == UNKNOWN))
		result = -EINVAL;
	else if (entry->action == APPRAISE)
		temp_ima_appraise |= ima_appraise_flag(entry->func);

	audit_log_format(ab, "res=%d", !result);
	audit_log_end(ab);
	return result;
}

/**
 * ima_parse_add_rule - add a rule to ima_policy_rules
 * @rule - ima measurement policy rule
 *
 * Avoid locking by allowing just one writer at a time in ima_write_policy()
 * Returns the length of the rule parsed, an error code on failure
 */
ssize_t ima_parse_add_rule(char *rule)
{
	static const char op[] = "update_policy";
	char *p;
	struct ima_rule_entry *entry;
	ssize_t result, len;
	int audit_info = 0;

	p = strsep(&rule, "\n");
	len = strlen(p) + 1;
	p += strspn(p, " \t");

	if (*p == '#' || *p == '\0')
		return len;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, op, "-ENOMEM", -ENOMEM, audit_info);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&entry->list);

	result = ima_parse_rule(p, entry);
	if (result) {
		kfree(entry);
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, op, "invalid-policy", result,
				    audit_info);
		return result;
	}

	list_add_tail(&entry->list, &ima_temp_rules);

	return len;
}

/**
 * ima_delete_rules() called to cleanup invalid in-flight policy.
 * We don't need locking as we operate on the temp list, which is
 * different from the active one.  There is also only one user of
 * ima_delete_rules() at a time.
 */
void ima_delete_rules(void)
{
	struct ima_rule_entry *entry, *tmp;
	int i;

	temp_ima_appraise = 0;
	list_for_each_entry_safe(entry, tmp, &ima_temp_rules, list) {
		for (i = 0; i < MAX_LSM_RULES; i++)
			kfree(entry->lsm[i].args_p);

		list_del(&entry->list);
		kfree(entry);
	}
}

#ifdef	CONFIG_IMA_READ_POLICY
enum {
	mask_exec = 0, mask_write, mask_read, mask_append
};

static const char *const mask_tokens[] = {
	"MAY_EXEC",
	"MAY_WRITE",
	"MAY_READ",
	"MAY_APPEND"
};

#define __ima_hook_stringify(str)	(#str),

static const char *const func_tokens[] = {
	__ima_hooks(__ima_hook_stringify)
};

void *ima_policy_start(struct seq_file *m, loff_t *pos)
{
	loff_t l = *pos;
	struct ima_rule_entry *entry;

	rcu_read_lock();
	list_for_each_entry_rcu(entry, ima_rules, list) {
		if (!l--) {
			rcu_read_unlock();
			return entry;
		}
	}
	rcu_read_unlock();
	return NULL;
}

void *ima_policy_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ima_rule_entry *entry = v;

	rcu_read_lock();
	entry = list_entry_rcu(entry->list.next, struct ima_rule_entry, list);
	rcu_read_unlock();
	(*pos)++;

	return (&entry->list == ima_rules) ? NULL : entry;
}

void ima_policy_stop(struct seq_file *m, void *v)
{
}

#define pt(token)	policy_tokens[token + Opt_err].pattern
#define mt(token)	mask_tokens[token]

/*
 * policy_func_show - display the ima_hooks policy rule
 */
static void policy_func_show(struct seq_file *m, enum ima_hooks func)
{
	if (func > 0 && func < MAX_CHECK)
		seq_printf(m, "func=%s ", func_tokens[func]);
	else
		seq_printf(m, "func=%d ", func);
}

int ima_policy_show(struct seq_file *m, void *v)
{
	struct ima_rule_entry *entry = v;
	int i;
	char tbuf[64] = {0,};

	rcu_read_lock();

	if (entry->action & MEASURE)
		seq_puts(m, pt(Opt_measure));
	if (entry->action & DONT_MEASURE)
		seq_puts(m, pt(Opt_dont_measure));
	if (entry->action & APPRAISE)
		seq_puts(m, pt(Opt_appraise));
	if (entry->action & DONT_APPRAISE)
		seq_puts(m, pt(Opt_dont_appraise));
	if (entry->action & AUDIT)
		seq_puts(m, pt(Opt_audit));
	if (entry->action & HASH)
		seq_puts(m, pt(Opt_hash));
	if (entry->action & DONT_HASH)
		seq_puts(m, pt(Opt_dont_hash));

	seq_puts(m, " ");

	if (entry->flags & IMA_FUNC)
		policy_func_show(m, entry->func);

	if (entry->flags & IMA_MASK) {
		if (entry->mask & MAY_EXEC)
			seq_printf(m, pt(Opt_mask), mt(mask_exec));
		if (entry->mask & MAY_WRITE)
			seq_printf(m, pt(Opt_mask), mt(mask_write));
		if (entry->mask & MAY_READ)
			seq_printf(m, pt(Opt_mask), mt(mask_read));
		if (entry->mask & MAY_APPEND)
			seq_printf(m, pt(Opt_mask), mt(mask_append));
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_FSMAGIC) {
		snprintf(tbuf, sizeof(tbuf), "0x%lx", entry->fsmagic);
		seq_printf(m, pt(Opt_fsmagic), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_FSNAME) {
		snprintf(tbuf, sizeof(tbuf), "%s", entry->fsname);
		seq_printf(m, pt(Opt_fsname), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_PCR) {
		snprintf(tbuf, sizeof(tbuf), "%d", entry->pcr);
		seq_printf(m, pt(Opt_pcr), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_FSUUID) {
		seq_printf(m, "fsuuid=%pU", &entry->fsuuid);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_UID) {
		snprintf(tbuf, sizeof(tbuf), "%d", __kuid_val(entry->uid));
		if (entry->uid_op == &uid_gt)
			seq_printf(m, pt(Opt_uid_gt), tbuf);
		else if (entry->uid_op == &uid_lt)
			seq_printf(m, pt(Opt_uid_lt), tbuf);
		else
			seq_printf(m, pt(Opt_uid_eq), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_EUID) {
		snprintf(tbuf, sizeof(tbuf), "%d", __kuid_val(entry->uid));
		if (entry->uid_op == &uid_gt)
			seq_printf(m, pt(Opt_euid_gt), tbuf);
		else if (entry->uid_op == &uid_lt)
			seq_printf(m, pt(Opt_euid_lt), tbuf);
		else
			seq_printf(m, pt(Opt_euid_eq), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_FOWNER) {
		snprintf(tbuf, sizeof(tbuf), "%d", __kuid_val(entry->fowner));
		if (entry->fowner_op == &uid_gt)
			seq_printf(m, pt(Opt_fowner_gt), tbuf);
		else if (entry->fowner_op == &uid_lt)
			seq_printf(m, pt(Opt_fowner_lt), tbuf);
		else
			seq_printf(m, pt(Opt_fowner_eq), tbuf);
		seq_puts(m, " ");
	}

	for (i = 0; i < MAX_LSM_RULES; i++) {
		if (entry->lsm[i].rule) {
			switch (i) {
			case LSM_OBJ_USER:
				seq_printf(m, pt(Opt_obj_user),
					   (char *)entry->lsm[i].args_p);
				break;
			case LSM_OBJ_ROLE:
				seq_printf(m, pt(Opt_obj_role),
					   (char *)entry->lsm[i].args_p);
				break;
			case LSM_OBJ_TYPE:
				seq_printf(m, pt(Opt_obj_type),
					   (char *)entry->lsm[i].args_p);
				break;
			case LSM_SUBJ_USER:
				seq_printf(m, pt(Opt_subj_user),
					   (char *)entry->lsm[i].args_p);
				break;
			case LSM_SUBJ_ROLE:
				seq_printf(m, pt(Opt_subj_role),
					   (char *)entry->lsm[i].args_p);
				break;
			case LSM_SUBJ_TYPE:
				seq_printf(m, pt(Opt_subj_type),
					   (char *)entry->lsm[i].args_p);
				break;
			}
		}
	}
	if (entry->flags & IMA_DIGSIG_REQUIRED)
		seq_puts(m, "appraise_type=imasig ");
	if (entry->flags & IMA_PERMIT_DIRECTIO)
		seq_puts(m, "permit_directio ");
	rcu_read_unlock();
	seq_puts(m, "\n");
	return 0;
}
#endif	/* CONFIG_IMA_READ_POLICY */
