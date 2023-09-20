// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * ima_policy.c
 *	- initialize default measure policy rules
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel_read_file.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/magic.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include <linux/seq_file.h>
#include <linux/ima.h>

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
#define IMA_KEYRINGS	0x0400
#define IMA_LABEL	0x0800
#define IMA_VALIDATE_ALGOS	0x1000
#define IMA_GID		0x2000
#define IMA_EGID	0x4000
#define IMA_FGROUP	0x8000

#define UNKNOWN		0
#define MEASURE		0x0001	/* same as IMA_MEASURE */
#define DONT_MEASURE	0x0002
#define APPRAISE	0x0004	/* same as IMA_APPRAISE */
#define DONT_APPRAISE	0x0008
#define AUDIT		0x0040
#define HASH		0x0100
#define DONT_HASH	0x0200

#define INVALID_PCR(a) (((a) < 0) || \
	(a) >= (sizeof_field(struct integrity_iint_cache, measured_pcrs) * 8))

int ima_policy_flag;
static int temp_ima_appraise;
static int build_ima_appraise __ro_after_init;

atomic_t ima_setxattr_allowed_hash_algorithms;

#define MAX_LSM_RULES 6
enum lsm_rule_types { LSM_OBJ_USER, LSM_OBJ_ROLE, LSM_OBJ_TYPE,
	LSM_SUBJ_USER, LSM_SUBJ_ROLE, LSM_SUBJ_TYPE
};

enum policy_types { ORIGINAL_TCB = 1, DEFAULT_TCB };

enum policy_rule_list { IMA_DEFAULT_POLICY = 1, IMA_CUSTOM_POLICY };

struct ima_rule_opt_list {
	size_t count;
	char *items[];
};

/*
 * These comparators are needed nowhere outside of ima so just define them here.
 * This pattern should hopefully never be needed outside of ima.
 */
static inline bool vfsuid_gt_kuid(vfsuid_t vfsuid, kuid_t kuid)
{
	return __vfsuid_val(vfsuid) > __kuid_val(kuid);
}

static inline bool vfsgid_gt_kgid(vfsgid_t vfsgid, kgid_t kgid)
{
	return __vfsgid_val(vfsgid) > __kgid_val(kgid);
}

static inline bool vfsuid_lt_kuid(vfsuid_t vfsuid, kuid_t kuid)
{
	return __vfsuid_val(vfsuid) < __kuid_val(kuid);
}

static inline bool vfsgid_lt_kgid(vfsgid_t vfsgid, kgid_t kgid)
{
	return __vfsgid_val(vfsgid) < __kgid_val(kgid);
}

struct ima_rule_entry {
	struct list_head list;
	int action;
	unsigned int flags;
	enum ima_hooks func;
	int mask;
	unsigned long fsmagic;
	uuid_t fsuuid;
	kuid_t uid;
	kgid_t gid;
	kuid_t fowner;
	kgid_t fgroup;
	bool (*uid_op)(kuid_t cred_uid, kuid_t rule_uid);    /* Handlers for operators       */
	bool (*gid_op)(kgid_t cred_gid, kgid_t rule_gid);
	bool (*fowner_op)(vfsuid_t vfsuid, kuid_t rule_uid); /* vfsuid_eq_kuid(), vfsuid_gt_kuid(), vfsuid_lt_kuid() */
	bool (*fgroup_op)(vfsgid_t vfsgid, kgid_t rule_gid); /* vfsgid_eq_kgid(), vfsgid_gt_kgid(), vfsgid_lt_kgid() */
	int pcr;
	unsigned int allowed_algos; /* bitfield of allowed hash algorithms */
	struct {
		void *rule;	/* LSM file metadata specific */
		char *args_p;	/* audit value */
		int type;	/* audit type */
	} lsm[MAX_LSM_RULES];
	char *fsname;
	struct ima_rule_opt_list *keyrings; /* Measure keys added to these keyrings */
	struct ima_rule_opt_list *label; /* Measure data grouped under this label */
	struct ima_template_desc *template;
};

/*
 * sanity check in case the kernels gains more hash algorithms that can
 * fit in an unsigned int
 */
static_assert(
	8 * sizeof(unsigned int) >= HASH_ALGO__LAST,
	"The bitfield allowed_algos in ima_rule_entry is too small to contain all the supported hash algorithms, consider using a bigger type");

/*
 * Without LSM specific knowledge, the default policy can only be
 * written in terms of .action, .func, .mask, .fsmagic, .uid, .gid,
 * .fowner, and .fgroup
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
	{.action = DONT_MEASURE, .fsmagic = NSFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_MEASURE, .fsmagic = EFIVARFS_MAGIC, .flags = IMA_FSMAGIC}
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
	{.action = DONT_APPRAISE, .fsmagic = EFIVARFS_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = CGROUP_SUPER_MAGIC, .flags = IMA_FSMAGIC},
	{.action = DONT_APPRAISE, .fsmagic = CGROUP2_SUPER_MAGIC, .flags = IMA_FSMAGIC},
#ifdef CONFIG_IMA_WRITE_POLICY
	{.action = APPRAISE, .func = POLICY_CHECK,
	.flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
#endif
#ifndef CONFIG_IMA_APPRAISE_SIGNED_INIT
	{.action = APPRAISE, .fowner = GLOBAL_ROOT_UID, .fowner_op = &vfsuid_eq_kuid,
	 .flags = IMA_FOWNER},
#else
	/* force signature */
	{.action = APPRAISE, .fowner = GLOBAL_ROOT_UID, .fowner_op = &vfsuid_eq_kuid,
	 .flags = IMA_FOWNER | IMA_DIGSIG_REQUIRED},
#endif
};

static struct ima_rule_entry build_appraise_rules[] __ro_after_init = {
#ifdef CONFIG_IMA_APPRAISE_REQUIRE_MODULE_SIGS
	{.action = APPRAISE, .func = MODULE_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
#endif
#ifdef CONFIG_IMA_APPRAISE_REQUIRE_FIRMWARE_SIGS
	{.action = APPRAISE, .func = FIRMWARE_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
#endif
#ifdef CONFIG_IMA_APPRAISE_REQUIRE_KEXEC_SIGS
	{.action = APPRAISE, .func = KEXEC_KERNEL_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
#endif
#ifdef CONFIG_IMA_APPRAISE_REQUIRE_POLICY_SIGS
	{.action = APPRAISE, .func = POLICY_CHECK,
	 .flags = IMA_FUNC | IMA_DIGSIG_REQUIRED},
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

static struct ima_rule_entry critical_data_rules[] __ro_after_init = {
	{.action = MEASURE, .func = CRITICAL_DATA, .flags = IMA_FUNC},
};

/* An array of architecture specific rules */
static struct ima_rule_entry *arch_policy_entry __ro_after_init;

static LIST_HEAD(ima_default_rules);
static LIST_HEAD(ima_policy_rules);
static LIST_HEAD(ima_temp_rules);
static struct list_head __rcu *ima_rules = (struct list_head __rcu *)(&ima_default_rules);

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
static bool ima_use_critical_data __initdata;
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
		else if (strcmp(p, "critical_data") == 0)
			ima_use_critical_data = true;
		else if (strcmp(p, "fail_securely") == 0)
			ima_fail_unverifiable_sigs = true;
		else
			pr_err("policy \"%s\" not found", p);
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

static struct ima_rule_opt_list *ima_alloc_rule_opt_list(const substring_t *src)
{
	struct ima_rule_opt_list *opt_list;
	size_t count = 0;
	char *src_copy;
	char *cur, *next;
	size_t i;

	src_copy = match_strdup(src);
	if (!src_copy)
		return ERR_PTR(-ENOMEM);

	next = src_copy;
	while ((cur = strsep(&next, "|"))) {
		/* Don't accept an empty list item */
		if (!(*cur)) {
			kfree(src_copy);
			return ERR_PTR(-EINVAL);
		}
		count++;
	}

	/* Don't accept an empty list */
	if (!count) {
		kfree(src_copy);
		return ERR_PTR(-EINVAL);
	}

	opt_list = kzalloc(struct_size(opt_list, items, count), GFP_KERNEL);
	if (!opt_list) {
		kfree(src_copy);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * strsep() has already replaced all instances of '|' with '\0',
	 * leaving a byte sequence of NUL-terminated strings. Reference each
	 * string with the array of items.
	 *
	 * IMPORTANT: Ownership of the allocated buffer is transferred from
	 * src_copy to the first element in the items array. To free the
	 * buffer, kfree() must only be called on the first element of the
	 * array.
	 */
	for (i = 0, cur = src_copy; i < count; i++) {
		opt_list->items[i] = cur;
		cur = strchr(cur, '\0') + 1;
	}
	opt_list->count = count;

	return opt_list;
}

static void ima_free_rule_opt_list(struct ima_rule_opt_list *opt_list)
{
	if (!opt_list)
		return;

	if (opt_list->count) {
		kfree(opt_list->items[0]);
		opt_list->count = 0;
	}

	kfree(opt_list);
}

static void ima_lsm_free_rule(struct ima_rule_entry *entry)
{
	int i;

	for (i = 0; i < MAX_LSM_RULES; i++) {
		ima_filter_rule_free(entry->lsm[i].rule);
		kfree(entry->lsm[i].args_p);
	}
}

static void ima_free_rule(struct ima_rule_entry *entry)
{
	if (!entry)
		return;

	/*
	 * entry->template->fields may be allocated in ima_parse_rule() but that
	 * reference is owned by the corresponding ima_template_desc element in
	 * the defined_templates list and cannot be freed here
	 */
	kfree(entry->fsname);
	ima_free_rule_opt_list(entry->keyrings);
	ima_lsm_free_rule(entry);
	kfree(entry);
}

static struct ima_rule_entry *ima_lsm_copy_rule(struct ima_rule_entry *entry)
{
	struct ima_rule_entry *nentry;
	int i;

	/*
	 * Immutable elements are copied over as pointers and data; only
	 * lsm rules can change
	 */
	nentry = kmemdup(entry, sizeof(*nentry), GFP_KERNEL);
	if (!nentry)
		return NULL;

	memset(nentry->lsm, 0, sizeof_field(struct ima_rule_entry, lsm));

	for (i = 0; i < MAX_LSM_RULES; i++) {
		if (!entry->lsm[i].args_p)
			continue;

		nentry->lsm[i].type = entry->lsm[i].type;
		nentry->lsm[i].args_p = entry->lsm[i].args_p;

		ima_filter_rule_init(nentry->lsm[i].type, Audit_equal,
				     nentry->lsm[i].args_p,
				     &nentry->lsm[i].rule);
		if (!nentry->lsm[i].rule)
			pr_warn("rule for LSM \'%s\' is undefined\n",
				nentry->lsm[i].args_p);
	}
	return nentry;
}

static int ima_lsm_update_rule(struct ima_rule_entry *entry)
{
	int i;
	struct ima_rule_entry *nentry;

	nentry = ima_lsm_copy_rule(entry);
	if (!nentry)
		return -ENOMEM;

	list_replace_rcu(&entry->list, &nentry->list);
	synchronize_rcu();
	/*
	 * ima_lsm_copy_rule() shallow copied all references, except for the
	 * LSM references, from entry to nentry so we only want to free the LSM
	 * references and the entry itself. All other memory references will now
	 * be owned by nentry.
	 */
	for (i = 0; i < MAX_LSM_RULES; i++)
		ima_filter_rule_free(entry->lsm[i].rule);
	kfree(entry);

	return 0;
}

static bool ima_rule_contains_lsm_cond(struct ima_rule_entry *entry)
{
	int i;

	for (i = 0; i < MAX_LSM_RULES; i++)
		if (entry->lsm[i].args_p)
			return true;

	return false;
}

/*
 * The LSM policy can be reloaded, leaving the IMA LSM based rules referring
 * to the old, stale LSM policy.  Update the IMA LSM based rules to reflect
 * the reloaded LSM policy.
 */
static void ima_lsm_update_rules(void)
{
	struct ima_rule_entry *entry, *e;
	int result;

	list_for_each_entry_safe(entry, e, &ima_policy_rules, list) {
		if (!ima_rule_contains_lsm_cond(entry))
			continue;

		result = ima_lsm_update_rule(entry);
		if (result) {
			pr_err("lsm rule update error %d\n", result);
			return;
		}
	}
}

int ima_lsm_policy_change(struct notifier_block *nb, unsigned long event,
			  void *lsm_data)
{
	if (event != LSM_POLICY_CHANGE)
		return NOTIFY_DONE;

	ima_lsm_update_rules();
	return NOTIFY_OK;
}

/**
 * ima_match_rule_data - determine whether func_data matches the policy rule
 * @rule: a pointer to a rule
 * @func_data: data to match against the measure rule data
 * @cred: a pointer to a credentials structure for user validation
 *
 * Returns true if func_data matches one in the rule, false otherwise.
 */
static bool ima_match_rule_data(struct ima_rule_entry *rule,
				const char *func_data,
				const struct cred *cred)
{
	const struct ima_rule_opt_list *opt_list = NULL;
	bool matched = false;
	size_t i;

	if ((rule->flags & IMA_UID) && !rule->uid_op(cred->uid, rule->uid))
		return false;

	switch (rule->func) {
	case KEY_CHECK:
		if (!rule->keyrings)
			return true;

		opt_list = rule->keyrings;
		break;
	case CRITICAL_DATA:
		if (!rule->label)
			return true;

		opt_list = rule->label;
		break;
	default:
		return false;
	}

	if (!func_data)
		return false;

	for (i = 0; i < opt_list->count; i++) {
		if (!strcmp(opt_list->items[i], func_data)) {
			matched = true;
			break;
		}
	}

	return matched;
}

/**
 * ima_match_rules - determine whether an inode matches the policy rule.
 * @rule: a pointer to a rule
 * @idmap: idmap of the mount the inode was found from
 * @inode: a pointer to an inode
 * @cred: a pointer to a credentials structure for user validation
 * @secid: the secid of the task to be validated
 * @func: LIM hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 * @func_data: func specific data, may be NULL
 *
 * Returns true on rule match, false on failure.
 */
static bool ima_match_rules(struct ima_rule_entry *rule,
			    struct mnt_idmap *idmap,
			    struct inode *inode, const struct cred *cred,
			    u32 secid, enum ima_hooks func, int mask,
			    const char *func_data)
{
	int i;
	bool result = false;
	struct ima_rule_entry *lsm_rule = rule;
	bool rule_reinitialized = false;

	if ((rule->flags & IMA_FUNC) &&
	    (rule->func != func && func != POST_SETATTR))
		return false;

	switch (func) {
	case KEY_CHECK:
	case CRITICAL_DATA:
		return ((rule->func == func) &&
			ima_match_rule_data(rule, func_data, cred));
	default:
		break;
	}

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
	if ((rule->flags & IMA_GID) && !rule->gid_op(cred->gid, rule->gid))
		return false;
	if (rule->flags & IMA_EGID) {
		if (has_capability_noaudit(current, CAP_SETGID)) {
			if (!rule->gid_op(cred->egid, rule->gid)
			    && !rule->gid_op(cred->sgid, rule->gid)
			    && !rule->gid_op(cred->gid, rule->gid))
				return false;
		} else if (!rule->gid_op(cred->egid, rule->gid))
			return false;
	}
	if ((rule->flags & IMA_FOWNER) &&
	    !rule->fowner_op(i_uid_into_vfsuid(idmap, inode),
			     rule->fowner))
		return false;
	if ((rule->flags & IMA_FGROUP) &&
	    !rule->fgroup_op(i_gid_into_vfsgid(idmap, inode),
			     rule->fgroup))
		return false;
	for (i = 0; i < MAX_LSM_RULES; i++) {
		int rc = 0;
		u32 osid;

		if (!lsm_rule->lsm[i].rule) {
			if (!lsm_rule->lsm[i].args_p)
				continue;
			else
				return false;
		}

retry:
		switch (i) {
		case LSM_OBJ_USER:
		case LSM_OBJ_ROLE:
		case LSM_OBJ_TYPE:
			security_inode_getsecid(inode, &osid);
			rc = ima_filter_rule_match(osid, lsm_rule->lsm[i].type,
						   Audit_equal,
						   lsm_rule->lsm[i].rule);
			break;
		case LSM_SUBJ_USER:
		case LSM_SUBJ_ROLE:
		case LSM_SUBJ_TYPE:
			rc = ima_filter_rule_match(secid, lsm_rule->lsm[i].type,
						   Audit_equal,
						   lsm_rule->lsm[i].rule);
			break;
		default:
			break;
		}

		if (rc == -ESTALE && !rule_reinitialized) {
			lsm_rule = ima_lsm_copy_rule(rule);
			if (lsm_rule) {
				rule_reinitialized = true;
				goto retry;
			}
		}
		if (!rc) {
			result = false;
			goto out;
		}
	}
	result = true;

out:
	if (rule_reinitialized) {
		for (i = 0; i < MAX_LSM_RULES; i++)
			ima_filter_rule_free(lsm_rule->lsm[i].rule);
		kfree(lsm_rule);
	}
	return result;
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
	case MMAP_CHECK_REQPROT:
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
 * @idmap: idmap of the mount the inode was found from
 * @inode: pointer to an inode for which the policy decision is being made
 * @cred: pointer to a credentials structure for which the policy decision is
 *        being made
 * @secid: LSM secid of the task to be validated
 * @func: IMA hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 * @flags: IMA actions to consider (e.g. IMA_MEASURE | IMA_APPRAISE)
 * @pcr: set the pcr to extend
 * @template_desc: the template that should be used for this rule
 * @func_data: func specific data, may be NULL
 * @allowed_algos: allowlist of hash algorithms for the IMA xattr
 *
 * Measure decision based on func/mask/fsmagic and LSM(subj/obj/type)
 * conditions.
 *
 * Since the IMA policy may be updated multiple times we need to lock the
 * list when walking it.  Reads are many orders of magnitude more numerous
 * than writes so ima_match_policy() is classical RCU candidate.
 */
int ima_match_policy(struct mnt_idmap *idmap, struct inode *inode,
		     const struct cred *cred, u32 secid, enum ima_hooks func,
		     int mask, int flags, int *pcr,
		     struct ima_template_desc **template_desc,
		     const char *func_data, unsigned int *allowed_algos)
{
	struct ima_rule_entry *entry;
	int action = 0, actmask = flags | (flags << 1);
	struct list_head *ima_rules_tmp;

	if (template_desc && !*template_desc)
		*template_desc = ima_template_desc_current();

	rcu_read_lock();
	ima_rules_tmp = rcu_dereference(ima_rules);
	list_for_each_entry_rcu(entry, ima_rules_tmp, list) {

		if (!(entry->action & actmask))
			continue;

		if (!ima_match_rules(entry, idmap, inode, cred, secid,
				     func, mask, func_data))
			continue;

		action |= entry->flags & IMA_NONACTION_FLAGS;

		action |= entry->action & IMA_DO_MASK;
		if (entry->action & IMA_APPRAISE) {
			action |= get_subaction(entry, func);
			action &= ~IMA_HASH;
			if (ima_fail_unverifiable_sigs)
				action |= IMA_FAIL_UNVERIFIABLE_SIGS;

			if (allowed_algos &&
			    entry->flags & IMA_VALIDATE_ALGOS)
				*allowed_algos = entry->allowed_algos;
		}

		if (entry->action & IMA_DO_MASK)
			actmask &= ~(entry->action | entry->action << 1);
		else
			actmask &= ~(entry->action | entry->action >> 1);

		if ((pcr) && (entry->flags & IMA_PCR))
			*pcr = entry->pcr;

		if (template_desc && entry->template)
			*template_desc = entry->template;

		if (!actmask)
			break;
	}
	rcu_read_unlock();

	return action;
}

/**
 * ima_update_policy_flags() - Update global IMA variables
 *
 * Update ima_policy_flag and ima_setxattr_allowed_hash_algorithms
 * based on the currently loaded policy.
 *
 * With ima_policy_flag, the decision to short circuit out of a function
 * or not call the function in the first place can be made earlier.
 *
 * With ima_setxattr_allowed_hash_algorithms, the policy can restrict the
 * set of hash algorithms accepted when updating the security.ima xattr of
 * a file.
 *
 * Context: called after a policy update and at system initialization.
 */
void ima_update_policy_flags(void)
{
	struct ima_rule_entry *entry;
	int new_policy_flag = 0;
	struct list_head *ima_rules_tmp;

	rcu_read_lock();
	ima_rules_tmp = rcu_dereference(ima_rules);
	list_for_each_entry_rcu(entry, ima_rules_tmp, list) {
		/*
		 * SETXATTR_CHECK rules do not implement a full policy check
		 * because rule checking would probably have an important
		 * performance impact on setxattr(). As a consequence, only one
		 * SETXATTR_CHECK can be active at a given time.
		 * Because we want to preserve that property, we set out to use
		 * atomic_cmpxchg. Either:
		 * - the atomic was non-zero: a setxattr hash policy is
		 *   already enforced, we do nothing
		 * - the atomic was zero: no setxattr policy was set, enable
		 *   the setxattr hash policy
		 */
		if (entry->func == SETXATTR_CHECK) {
			atomic_cmpxchg(&ima_setxattr_allowed_hash_algorithms,
				       0, entry->allowed_algos);
			/* SETXATTR_CHECK doesn't impact ima_policy_flag */
			continue;
		}

		if (entry->action & IMA_DO_MASK)
			new_policy_flag |= entry->action;
	}
	rcu_read_unlock();

	ima_appraise |= (build_ima_appraise | temp_ima_appraise);
	if (!ima_appraise)
		new_policy_flag &= ~IMA_APPRAISE;

	ima_policy_flag = new_policy_flag;
}

static int ima_appraise_flag(enum ima_hooks func)
{
	if (func == MODULE_CHECK)
		return IMA_APPRAISE_MODULES;
	else if (func == FIRMWARE_CHECK)
		return IMA_APPRAISE_FIRMWARE;
	else if (func == POLICY_CHECK)
		return IMA_APPRAISE_POLICY;
	else if (func == KEXEC_KERNEL_CHECK)
		return IMA_APPRAISE_KEXEC;
	return 0;
}

static void add_rules(struct ima_rule_entry *entries, int count,
		      enum policy_rule_list policy_rule)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		struct ima_rule_entry *entry;

		if (policy_rule & IMA_DEFAULT_POLICY)
			list_add_tail(&entries[i].list, &ima_default_rules);

		if (policy_rule & IMA_CUSTOM_POLICY) {
			entry = kmemdup(&entries[i], sizeof(*entry),
					GFP_KERNEL);
			if (!entry)
				continue;

			list_add_tail(&entry->list, &ima_policy_rules);
		}
		if (entries[i].action == APPRAISE) {
			if (entries != build_appraise_rules)
				temp_ima_appraise |=
					ima_appraise_flag(entries[i].func);
			else
				build_ima_appraise |=
					ima_appraise_flag(entries[i].func);
		}
	}
}

static int ima_parse_rule(char *rule, struct ima_rule_entry *entry);

static int __init ima_init_arch_policy(void)
{
	const char * const *arch_rules;
	const char * const *rules;
	int arch_entries = 0;
	int i = 0;

	arch_rules = arch_get_ima_policy();
	if (!arch_rules)
		return arch_entries;

	/* Get number of rules */
	for (rules = arch_rules; *rules != NULL; rules++)
		arch_entries++;

	arch_policy_entry = kcalloc(arch_entries + 1,
				    sizeof(*arch_policy_entry), GFP_KERNEL);
	if (!arch_policy_entry)
		return 0;

	/* Convert each policy string rules to struct ima_rule_entry format */
	for (rules = arch_rules, i = 0; *rules != NULL; rules++) {
		char rule[255];
		int result;

		result = strscpy(rule, *rules, sizeof(rule));

		INIT_LIST_HEAD(&arch_policy_entry[i].list);
		result = ima_parse_rule(rule, &arch_policy_entry[i]);
		if (result) {
			pr_warn("Skipping unknown architecture policy rule: %s\n",
				rule);
			memset(&arch_policy_entry[i], 0,
			       sizeof(*arch_policy_entry));
			continue;
		}
		i++;
	}
	return i;
}

/**
 * ima_init_policy - initialize the default measure rules.
 *
 * ima_rules points to either the ima_default_rules or the new ima_policy_rules.
 */
void __init ima_init_policy(void)
{
	int build_appraise_entries, arch_entries;

	/* if !ima_policy, we load NO default rules */
	if (ima_policy)
		add_rules(dont_measure_rules, ARRAY_SIZE(dont_measure_rules),
			  IMA_DEFAULT_POLICY);

	switch (ima_policy) {
	case ORIGINAL_TCB:
		add_rules(original_measurement_rules,
			  ARRAY_SIZE(original_measurement_rules),
			  IMA_DEFAULT_POLICY);
		break;
	case DEFAULT_TCB:
		add_rules(default_measurement_rules,
			  ARRAY_SIZE(default_measurement_rules),
			  IMA_DEFAULT_POLICY);
		break;
	default:
		break;
	}

	/*
	 * Based on runtime secure boot flags, insert arch specific measurement
	 * and appraise rules requiring file signatures for both the initial
	 * and custom policies, prior to other appraise rules.
	 * (Highest priority)
	 */
	arch_entries = ima_init_arch_policy();
	if (!arch_entries)
		pr_info("No architecture policies found\n");
	else
		add_rules(arch_policy_entry, arch_entries,
			  IMA_DEFAULT_POLICY | IMA_CUSTOM_POLICY);

	/*
	 * Insert the builtin "secure_boot" policy rules requiring file
	 * signatures, prior to other appraise rules.
	 */
	if (ima_use_secure_boot)
		add_rules(secure_boot_rules, ARRAY_SIZE(secure_boot_rules),
			  IMA_DEFAULT_POLICY);

	/*
	 * Insert the build time appraise rules requiring file signatures
	 * for both the initial and custom policies, prior to other appraise
	 * rules. As the secure boot rules includes all of the build time
	 * rules, include either one or the other set of rules, but not both.
	 */
	build_appraise_entries = ARRAY_SIZE(build_appraise_rules);
	if (build_appraise_entries) {
		if (ima_use_secure_boot)
			add_rules(build_appraise_rules, build_appraise_entries,
				  IMA_CUSTOM_POLICY);
		else
			add_rules(build_appraise_rules, build_appraise_entries,
				  IMA_DEFAULT_POLICY | IMA_CUSTOM_POLICY);
	}

	if (ima_use_appraise_tcb)
		add_rules(default_appraise_rules,
			  ARRAY_SIZE(default_appraise_rules),
			  IMA_DEFAULT_POLICY);

	if (ima_use_critical_data)
		add_rules(critical_data_rules,
			  ARRAY_SIZE(critical_data_rules),
			  IMA_DEFAULT_POLICY);

	atomic_set(&ima_setxattr_allowed_hash_algorithms, 0);

	ima_update_policy_flags();
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

	if (ima_rules != (struct list_head __rcu *)policy) {
		ima_policy_flag = 0;

		rcu_assign_pointer(ima_rules, policy);
		/*
		 * IMA architecture specific policy rules are specified
		 * as strings and converted to an array of ima_entry_rules
		 * on boot.  After loading a custom policy, free the
		 * architecture specific rules stored as an array.
		 */
		kfree(arch_policy_entry);
	}
	ima_update_policy_flags();

	/* Custom IMA policy has been loaded */
	ima_process_queued_keys();
}

/* Keep the enumeration in sync with the policy_tokens! */
enum policy_opt {
	Opt_measure, Opt_dont_measure,
	Opt_appraise, Opt_dont_appraise,
	Opt_audit, Opt_hash, Opt_dont_hash,
	Opt_obj_user, Opt_obj_role, Opt_obj_type,
	Opt_subj_user, Opt_subj_role, Opt_subj_type,
	Opt_func, Opt_mask, Opt_fsmagic, Opt_fsname, Opt_fsuuid,
	Opt_uid_eq, Opt_euid_eq, Opt_gid_eq, Opt_egid_eq,
	Opt_fowner_eq, Opt_fgroup_eq,
	Opt_uid_gt, Opt_euid_gt, Opt_gid_gt, Opt_egid_gt,
	Opt_fowner_gt, Opt_fgroup_gt,
	Opt_uid_lt, Opt_euid_lt, Opt_gid_lt, Opt_egid_lt,
	Opt_fowner_lt, Opt_fgroup_lt,
	Opt_digest_type,
	Opt_appraise_type, Opt_appraise_flag, Opt_appraise_algos,
	Opt_permit_directio, Opt_pcr, Opt_template, Opt_keyrings,
	Opt_label, Opt_err
};

static const match_table_t policy_tokens = {
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
	{Opt_gid_eq, "gid=%s"},
	{Opt_egid_eq, "egid=%s"},
	{Opt_fowner_eq, "fowner=%s"},
	{Opt_fgroup_eq, "fgroup=%s"},
	{Opt_uid_gt, "uid>%s"},
	{Opt_euid_gt, "euid>%s"},
	{Opt_gid_gt, "gid>%s"},
	{Opt_egid_gt, "egid>%s"},
	{Opt_fowner_gt, "fowner>%s"},
	{Opt_fgroup_gt, "fgroup>%s"},
	{Opt_uid_lt, "uid<%s"},
	{Opt_euid_lt, "euid<%s"},
	{Opt_gid_lt, "gid<%s"},
	{Opt_egid_lt, "egid<%s"},
	{Opt_fowner_lt, "fowner<%s"},
	{Opt_fgroup_lt, "fgroup<%s"},
	{Opt_digest_type, "digest_type=%s"},
	{Opt_appraise_type, "appraise_type=%s"},
	{Opt_appraise_flag, "appraise_flag=%s"},
	{Opt_appraise_algos, "appraise_algos=%s"},
	{Opt_permit_directio, "permit_directio"},
	{Opt_pcr, "pcr=%s"},
	{Opt_template, "template=%s"},
	{Opt_keyrings, "keyrings=%s"},
	{Opt_label, "label=%s"},
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
	result = ima_filter_rule_init(entry->lsm[lsm_rule].type, Audit_equal,
				      entry->lsm[lsm_rule].args_p,
				      &entry->lsm[lsm_rule].rule);
	if (!entry->lsm[lsm_rule].rule) {
		pr_warn("rule for LSM \'%s\' is undefined\n",
			entry->lsm[lsm_rule].args_p);

		if (ima_rules == (struct list_head __rcu *)(&ima_default_rules)) {
			kfree(entry->lsm[lsm_rule].args_p);
			entry->lsm[lsm_rule].args_p = NULL;
			result = -EINVAL;
		} else
			result = 0;
	}

	return result;
}

static void ima_log_string_op(struct audit_buffer *ab, char *key, char *value,
			      enum policy_opt rule_operator)
{
	if (!ab)
		return;

	switch (rule_operator) {
	case Opt_uid_gt:
	case Opt_euid_gt:
	case Opt_gid_gt:
	case Opt_egid_gt:
	case Opt_fowner_gt:
	case Opt_fgroup_gt:
		audit_log_format(ab, "%s>", key);
		break;
	case Opt_uid_lt:
	case Opt_euid_lt:
	case Opt_gid_lt:
	case Opt_egid_lt:
	case Opt_fowner_lt:
	case Opt_fgroup_lt:
		audit_log_format(ab, "%s<", key);
		break;
	default:
		audit_log_format(ab, "%s=", key);
	}
	audit_log_format(ab, "%s ", value);
}
static void ima_log_string(struct audit_buffer *ab, char *key, char *value)
{
	ima_log_string_op(ab, key, value, Opt_err);
}

/*
 * Validating the appended signature included in the measurement list requires
 * the file hash calculated without the appended signature (i.e., the 'd-modsig'
 * field). Therefore, notify the user if they have the 'modsig' field but not
 * the 'd-modsig' field in the template.
 */
static void check_template_modsig(const struct ima_template_desc *template)
{
#define MSG "template with 'modsig' field also needs 'd-modsig' field\n"
	bool has_modsig, has_dmodsig;
	static bool checked;
	int i;

	/* We only need to notify the user once. */
	if (checked)
		return;

	has_modsig = has_dmodsig = false;
	for (i = 0; i < template->num_fields; i++) {
		if (!strcmp(template->fields[i]->field_id, "modsig"))
			has_modsig = true;
		else if (!strcmp(template->fields[i]->field_id, "d-modsig"))
			has_dmodsig = true;
	}

	if (has_modsig && !has_dmodsig)
		pr_notice(MSG);

	checked = true;
#undef MSG
}

/*
 * Warn if the template does not contain the given field.
 */
static void check_template_field(const struct ima_template_desc *template,
				 const char *field, const char *msg)
{
	int i;

	for (i = 0; i < template->num_fields; i++)
		if (!strcmp(template->fields[i]->field_id, field))
			return;

	pr_notice_once("%s", msg);
}

static bool ima_validate_rule(struct ima_rule_entry *entry)
{
	/* Ensure that the action is set and is compatible with the flags */
	if (entry->action == UNKNOWN)
		return false;

	if (entry->action != MEASURE && entry->flags & IMA_PCR)
		return false;

	if (entry->action != APPRAISE &&
	    entry->flags & (IMA_DIGSIG_REQUIRED | IMA_MODSIG_ALLOWED |
			    IMA_CHECK_BLACKLIST | IMA_VALIDATE_ALGOS))
		return false;

	/*
	 * The IMA_FUNC bit must be set if and only if there's a valid hook
	 * function specified, and vice versa. Enforcing this property allows
	 * for the NONE case below to validate a rule without an explicit hook
	 * function.
	 */
	if (((entry->flags & IMA_FUNC) && entry->func == NONE) ||
	    (!(entry->flags & IMA_FUNC) && entry->func != NONE))
		return false;

	/*
	 * Ensure that the hook function is compatible with the other
	 * components of the rule
	 */
	switch (entry->func) {
	case NONE:
	case FILE_CHECK:
	case MMAP_CHECK:
	case MMAP_CHECK_REQPROT:
	case BPRM_CHECK:
	case CREDS_CHECK:
	case POST_SETATTR:
	case FIRMWARE_CHECK:
	case POLICY_CHECK:
		if (entry->flags & ~(IMA_FUNC | IMA_MASK | IMA_FSMAGIC |
				     IMA_UID | IMA_FOWNER | IMA_FSUUID |
				     IMA_INMASK | IMA_EUID | IMA_PCR |
				     IMA_FSNAME | IMA_GID | IMA_EGID |
				     IMA_FGROUP | IMA_DIGSIG_REQUIRED |
				     IMA_PERMIT_DIRECTIO | IMA_VALIDATE_ALGOS |
				     IMA_VERITY_REQUIRED))
			return false;

		break;
	case MODULE_CHECK:
	case KEXEC_KERNEL_CHECK:
	case KEXEC_INITRAMFS_CHECK:
		if (entry->flags & ~(IMA_FUNC | IMA_MASK | IMA_FSMAGIC |
				     IMA_UID | IMA_FOWNER | IMA_FSUUID |
				     IMA_INMASK | IMA_EUID | IMA_PCR |
				     IMA_FSNAME | IMA_GID | IMA_EGID |
				     IMA_FGROUP | IMA_DIGSIG_REQUIRED |
				     IMA_PERMIT_DIRECTIO | IMA_MODSIG_ALLOWED |
				     IMA_CHECK_BLACKLIST | IMA_VALIDATE_ALGOS))
			return false;

		break;
	case KEXEC_CMDLINE:
		if (entry->action & ~(MEASURE | DONT_MEASURE))
			return false;

		if (entry->flags & ~(IMA_FUNC | IMA_FSMAGIC | IMA_UID |
				     IMA_FOWNER | IMA_FSUUID | IMA_EUID |
				     IMA_PCR | IMA_FSNAME | IMA_GID | IMA_EGID |
				     IMA_FGROUP))
			return false;

		break;
	case KEY_CHECK:
		if (entry->action & ~(MEASURE | DONT_MEASURE))
			return false;

		if (entry->flags & ~(IMA_FUNC | IMA_UID | IMA_GID | IMA_PCR |
				     IMA_KEYRINGS))
			return false;

		if (ima_rule_contains_lsm_cond(entry))
			return false;

		break;
	case CRITICAL_DATA:
		if (entry->action & ~(MEASURE | DONT_MEASURE))
			return false;

		if (entry->flags & ~(IMA_FUNC | IMA_UID | IMA_GID | IMA_PCR |
				     IMA_LABEL))
			return false;

		if (ima_rule_contains_lsm_cond(entry))
			return false;

		break;
	case SETXATTR_CHECK:
		/* any action other than APPRAISE is unsupported */
		if (entry->action != APPRAISE)
			return false;

		/* SETXATTR_CHECK requires an appraise_algos parameter */
		if (!(entry->flags & IMA_VALIDATE_ALGOS))
			return false;

		/*
		 * full policies are not supported, they would have too
		 * much of a performance impact
		 */
		if (entry->flags & ~(IMA_FUNC | IMA_VALIDATE_ALGOS))
			return false;

		break;
	default:
		return false;
	}

	/* Ensure that combinations of flags are compatible with each other */
	if (entry->flags & IMA_CHECK_BLACKLIST &&
	    !(entry->flags & IMA_MODSIG_ALLOWED))
		return false;

	/*
	 * Unlike for regular IMA 'appraise' policy rules where security.ima
	 * xattr may contain either a file hash or signature, the security.ima
	 * xattr for fsverity must contain a file signature (sigv3).  Ensure
	 * that 'appraise' rules for fsverity require file signatures by
	 * checking the IMA_DIGSIG_REQUIRED flag is set.
	 */
	if (entry->action == APPRAISE &&
	    (entry->flags & IMA_VERITY_REQUIRED) &&
	    !(entry->flags & IMA_DIGSIG_REQUIRED))
		return false;

	return true;
}

static unsigned int ima_parse_appraise_algos(char *arg)
{
	unsigned int res = 0;
	int idx;
	char *token;

	while ((token = strsep(&arg, ",")) != NULL) {
		idx = match_string(hash_algo_name, HASH_ALGO__LAST, token);

		if (idx < 0) {
			pr_err("unknown hash algorithm \"%s\"",
			       token);
			return 0;
		}

		if (!crypto_has_alg(hash_algo_name[idx], 0, 0)) {
			pr_err("unavailable hash algorithm \"%s\", check your kernel configuration",
			       token);
			return 0;
		}

		/* Add the hash algorithm to the 'allowed' bitfield */
		res |= (1U << idx);
	}

	return res;
}

static int ima_parse_rule(char *rule, struct ima_rule_entry *entry)
{
	struct audit_buffer *ab;
	char *from;
	char *p;
	bool eid_token; /* either euid or egid */
	struct ima_template_desc *template_desc;
	int result = 0;

	ab = integrity_audit_log_start(audit_context(), GFP_KERNEL,
				       AUDIT_INTEGRITY_POLICY_RULE);

	entry->uid = INVALID_UID;
	entry->gid = INVALID_GID;
	entry->fowner = INVALID_UID;
	entry->fgroup = INVALID_GID;
	entry->uid_op = &uid_eq;
	entry->gid_op = &gid_eq;
	entry->fowner_op = &vfsuid_eq_kuid;
	entry->fgroup_op = &vfsgid_eq_kgid;
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
			else if ((strcmp(args[0].from, "MMAP_CHECK_REQPROT") == 0))
				entry->func = MMAP_CHECK_REQPROT;
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
			else if (strcmp(args[0].from, "KEXEC_CMDLINE") == 0)
				entry->func = KEXEC_CMDLINE;
			else if (IS_ENABLED(CONFIG_IMA_MEASURE_ASYMMETRIC_KEYS) &&
				 strcmp(args[0].from, "KEY_CHECK") == 0)
				entry->func = KEY_CHECK;
			else if (strcmp(args[0].from, "CRITICAL_DATA") == 0)
				entry->func = CRITICAL_DATA;
			else if (strcmp(args[0].from, "SETXATTR_CHECK") == 0)
				entry->func = SETXATTR_CHECK;
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
		case Opt_keyrings:
			ima_log_string(ab, "keyrings", args[0].from);

			if (!IS_ENABLED(CONFIG_IMA_MEASURE_ASYMMETRIC_KEYS) ||
			    entry->keyrings) {
				result = -EINVAL;
				break;
			}

			entry->keyrings = ima_alloc_rule_opt_list(args);
			if (IS_ERR(entry->keyrings)) {
				result = PTR_ERR(entry->keyrings);
				entry->keyrings = NULL;
				break;
			}

			entry->flags |= IMA_KEYRINGS;
			break;
		case Opt_label:
			ima_log_string(ab, "label", args[0].from);

			if (entry->label) {
				result = -EINVAL;
				break;
			}

			entry->label = ima_alloc_rule_opt_list(args);
			if (IS_ERR(entry->label)) {
				result = PTR_ERR(entry->label);
				entry->label = NULL;
				break;
			}

			entry->flags |= IMA_LABEL;
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
			fallthrough;
		case Opt_uid_lt:
		case Opt_euid_lt:
			if ((token == Opt_uid_lt) || (token == Opt_euid_lt))
				entry->uid_op = &uid_lt;
			fallthrough;
		case Opt_uid_eq:
		case Opt_euid_eq:
			eid_token = (token == Opt_euid_eq) ||
				    (token == Opt_euid_gt) ||
				    (token == Opt_euid_lt);

			ima_log_string_op(ab, eid_token ? "euid" : "uid",
					  args[0].from, token);

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
					entry->flags |= eid_token
					    ? IMA_EUID : IMA_UID;
			}
			break;
		case Opt_gid_gt:
		case Opt_egid_gt:
			entry->gid_op = &gid_gt;
			fallthrough;
		case Opt_gid_lt:
		case Opt_egid_lt:
			if ((token == Opt_gid_lt) || (token == Opt_egid_lt))
				entry->gid_op = &gid_lt;
			fallthrough;
		case Opt_gid_eq:
		case Opt_egid_eq:
			eid_token = (token == Opt_egid_eq) ||
				    (token == Opt_egid_gt) ||
				    (token == Opt_egid_lt);

			ima_log_string_op(ab, eid_token ? "egid" : "gid",
					  args[0].from, token);

			if (gid_valid(entry->gid)) {
				result = -EINVAL;
				break;
			}

			result = kstrtoul(args[0].from, 10, &lnum);
			if (!result) {
				entry->gid = make_kgid(current_user_ns(),
						       (gid_t)lnum);
				if (!gid_valid(entry->gid) ||
				    (((gid_t)lnum) != lnum))
					result = -EINVAL;
				else
					entry->flags |= eid_token
					    ? IMA_EGID : IMA_GID;
			}
			break;
		case Opt_fowner_gt:
			entry->fowner_op = &vfsuid_gt_kuid;
			fallthrough;
		case Opt_fowner_lt:
			if (token == Opt_fowner_lt)
				entry->fowner_op = &vfsuid_lt_kuid;
			fallthrough;
		case Opt_fowner_eq:
			ima_log_string_op(ab, "fowner", args[0].from, token);

			if (uid_valid(entry->fowner)) {
				result = -EINVAL;
				break;
			}

			result = kstrtoul(args[0].from, 10, &lnum);
			if (!result) {
				entry->fowner = make_kuid(current_user_ns(),
							  (uid_t)lnum);
				if (!uid_valid(entry->fowner) ||
				    (((uid_t)lnum) != lnum))
					result = -EINVAL;
				else
					entry->flags |= IMA_FOWNER;
			}
			break;
		case Opt_fgroup_gt:
			entry->fgroup_op = &vfsgid_gt_kgid;
			fallthrough;
		case Opt_fgroup_lt:
			if (token == Opt_fgroup_lt)
				entry->fgroup_op = &vfsgid_lt_kgid;
			fallthrough;
		case Opt_fgroup_eq:
			ima_log_string_op(ab, "fgroup", args[0].from, token);

			if (gid_valid(entry->fgroup)) {
				result = -EINVAL;
				break;
			}

			result = kstrtoul(args[0].from, 10, &lnum);
			if (!result) {
				entry->fgroup = make_kgid(current_user_ns(),
							  (gid_t)lnum);
				if (!gid_valid(entry->fgroup) ||
				    (((gid_t)lnum) != lnum))
					result = -EINVAL;
				else
					entry->flags |= IMA_FGROUP;
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
		case Opt_digest_type:
			ima_log_string(ab, "digest_type", args[0].from);
			if (entry->flags & IMA_DIGSIG_REQUIRED)
				result = -EINVAL;
			else if ((strcmp(args[0].from, "verity")) == 0)
				entry->flags |= IMA_VERITY_REQUIRED;
			else
				result = -EINVAL;
			break;
		case Opt_appraise_type:
			ima_log_string(ab, "appraise_type", args[0].from);

			if ((strcmp(args[0].from, "imasig")) == 0) {
				if (entry->flags & IMA_VERITY_REQUIRED)
					result = -EINVAL;
				else
					entry->flags |= IMA_DIGSIG_REQUIRED;
			} else if (strcmp(args[0].from, "sigv3") == 0) {
				/* Only fsverity supports sigv3 for now */
				if (entry->flags & IMA_VERITY_REQUIRED)
					entry->flags |= IMA_DIGSIG_REQUIRED;
				else
					result = -EINVAL;
			} else if (IS_ENABLED(CONFIG_IMA_APPRAISE_MODSIG) &&
				 strcmp(args[0].from, "imasig|modsig") == 0) {
				if (entry->flags & IMA_VERITY_REQUIRED)
					result = -EINVAL;
				else
					entry->flags |= IMA_DIGSIG_REQUIRED |
						IMA_MODSIG_ALLOWED;
			} else {
				result = -EINVAL;
			}
			break;
		case Opt_appraise_flag:
			ima_log_string(ab, "appraise_flag", args[0].from);
			if (IS_ENABLED(CONFIG_IMA_APPRAISE_MODSIG) &&
			    strstr(args[0].from, "blacklist"))
				entry->flags |= IMA_CHECK_BLACKLIST;
			else
				result = -EINVAL;
			break;
		case Opt_appraise_algos:
			ima_log_string(ab, "appraise_algos", args[0].from);

			if (entry->allowed_algos) {
				result = -EINVAL;
				break;
			}

			entry->allowed_algos =
				ima_parse_appraise_algos(args[0].from);
			/* invalid or empty list of algorithms */
			if (!entry->allowed_algos) {
				result = -EINVAL;
				break;
			}

			entry->flags |= IMA_VALIDATE_ALGOS;

			break;
		case Opt_permit_directio:
			entry->flags |= IMA_PERMIT_DIRECTIO;
			break;
		case Opt_pcr:
			ima_log_string(ab, "pcr", args[0].from);

			result = kstrtoint(args[0].from, 10, &entry->pcr);
			if (result || INVALID_PCR(entry->pcr))
				result = -EINVAL;
			else
				entry->flags |= IMA_PCR;

			break;
		case Opt_template:
			ima_log_string(ab, "template", args[0].from);
			if (entry->action != MEASURE) {
				result = -EINVAL;
				break;
			}
			template_desc = lookup_template_desc(args[0].from);
			if (!template_desc || entry->template) {
				result = -EINVAL;
				break;
			}

			/*
			 * template_desc_init_fields() does nothing if
			 * the template is already initialised, so
			 * it's safe to do this unconditionally
			 */
			template_desc_init_fields(template_desc->fmt,
						 &(template_desc->fields),
						 &(template_desc->num_fields));
			entry->template = template_desc;
			break;
		case Opt_err:
			ima_log_string(ab, "UNKNOWN", p);
			result = -EINVAL;
			break;
		}
	}
	if (!result && !ima_validate_rule(entry))
		result = -EINVAL;
	else if (entry->action == APPRAISE)
		temp_ima_appraise |= ima_appraise_flag(entry->func);

	if (!result && entry->flags & IMA_MODSIG_ALLOWED) {
		template_desc = entry->template ? entry->template :
						  ima_template_desc_current();
		check_template_modsig(template_desc);
	}

	/* d-ngv2 template field recommended for unsigned fs-verity digests */
	if (!result && entry->action == MEASURE &&
	    entry->flags & IMA_VERITY_REQUIRED) {
		template_desc = entry->template ? entry->template :
						  ima_template_desc_current();
		check_template_field(template_desc, "d-ngv2",
				     "verity rules should include d-ngv2");
	}

	audit_log_format(ab, "res=%d", !result);
	audit_log_end(ab);
	return result;
}

/**
 * ima_parse_add_rule - add a rule to ima_policy_rules
 * @rule: ima measurement policy rule
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
		ima_free_rule(entry);
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, op, "invalid-policy", result,
				    audit_info);
		return result;
	}

	list_add_tail(&entry->list, &ima_temp_rules);

	return len;
}

/**
 * ima_delete_rules() - called to cleanup invalid in-flight policy.
 *
 * We don't need locking as we operate on the temp list, which is
 * different from the active one.  There is also only one user of
 * ima_delete_rules() at a time.
 */
void ima_delete_rules(void)
{
	struct ima_rule_entry *entry, *tmp;

	temp_ima_appraise = 0;
	list_for_each_entry_safe(entry, tmp, &ima_temp_rules, list) {
		list_del(&entry->list);
		ima_free_rule(entry);
	}
}

#define __ima_hook_stringify(func, str)	(#func),

const char *const func_tokens[] = {
	__ima_hooks(__ima_hook_stringify)
};

#ifdef	CONFIG_IMA_READ_POLICY
enum {
	mask_exec = 0, mask_write, mask_read, mask_append
};

static const char *const mask_tokens[] = {
	"^MAY_EXEC",
	"^MAY_WRITE",
	"^MAY_READ",
	"^MAY_APPEND"
};

void *ima_policy_start(struct seq_file *m, loff_t *pos)
{
	loff_t l = *pos;
	struct ima_rule_entry *entry;
	struct list_head *ima_rules_tmp;

	rcu_read_lock();
	ima_rules_tmp = rcu_dereference(ima_rules);
	list_for_each_entry_rcu(entry, ima_rules_tmp, list) {
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

	return (&entry->list == &ima_default_rules ||
		&entry->list == &ima_policy_rules) ? NULL : entry;
}

void ima_policy_stop(struct seq_file *m, void *v)
{
}

#define pt(token)	policy_tokens[token].pattern
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

static void ima_show_rule_opt_list(struct seq_file *m,
				   const struct ima_rule_opt_list *opt_list)
{
	size_t i;

	for (i = 0; i < opt_list->count; i++)
		seq_printf(m, "%s%s", i ? "|" : "", opt_list->items[i]);
}

static void ima_policy_show_appraise_algos(struct seq_file *m,
					   unsigned int allowed_hashes)
{
	int idx, list_size = 0;

	for (idx = 0; idx < HASH_ALGO__LAST; idx++) {
		if (!(allowed_hashes & (1U << idx)))
			continue;

		/* only add commas if the list contains multiple entries */
		if (list_size++)
			seq_puts(m, ",");

		seq_puts(m, hash_algo_name[idx]);
	}
}

int ima_policy_show(struct seq_file *m, void *v)
{
	struct ima_rule_entry *entry = v;
	int i;
	char tbuf[64] = {0,};
	int offset = 0;

	rcu_read_lock();

	/* Do not print rules with inactive LSM labels */
	for (i = 0; i < MAX_LSM_RULES; i++) {
		if (entry->lsm[i].args_p && !entry->lsm[i].rule) {
			rcu_read_unlock();
			return 0;
		}
	}

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

	if ((entry->flags & IMA_MASK) || (entry->flags & IMA_INMASK)) {
		if (entry->flags & IMA_MASK)
			offset = 1;
		if (entry->mask & MAY_EXEC)
			seq_printf(m, pt(Opt_mask), mt(mask_exec) + offset);
		if (entry->mask & MAY_WRITE)
			seq_printf(m, pt(Opt_mask), mt(mask_write) + offset);
		if (entry->mask & MAY_READ)
			seq_printf(m, pt(Opt_mask), mt(mask_read) + offset);
		if (entry->mask & MAY_APPEND)
			seq_printf(m, pt(Opt_mask), mt(mask_append) + offset);
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

	if (entry->flags & IMA_KEYRINGS) {
		seq_puts(m, "keyrings=");
		ima_show_rule_opt_list(m, entry->keyrings);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_LABEL) {
		seq_puts(m, "label=");
		ima_show_rule_opt_list(m, entry->label);
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

	if (entry->flags & IMA_GID) {
		snprintf(tbuf, sizeof(tbuf), "%d", __kgid_val(entry->gid));
		if (entry->gid_op == &gid_gt)
			seq_printf(m, pt(Opt_gid_gt), tbuf);
		else if (entry->gid_op == &gid_lt)
			seq_printf(m, pt(Opt_gid_lt), tbuf);
		else
			seq_printf(m, pt(Opt_gid_eq), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_EGID) {
		snprintf(tbuf, sizeof(tbuf), "%d", __kgid_val(entry->gid));
		if (entry->gid_op == &gid_gt)
			seq_printf(m, pt(Opt_egid_gt), tbuf);
		else if (entry->gid_op == &gid_lt)
			seq_printf(m, pt(Opt_egid_lt), tbuf);
		else
			seq_printf(m, pt(Opt_egid_eq), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_FOWNER) {
		snprintf(tbuf, sizeof(tbuf), "%d", __kuid_val(entry->fowner));
		if (entry->fowner_op == &vfsuid_gt_kuid)
			seq_printf(m, pt(Opt_fowner_gt), tbuf);
		else if (entry->fowner_op == &vfsuid_lt_kuid)
			seq_printf(m, pt(Opt_fowner_lt), tbuf);
		else
			seq_printf(m, pt(Opt_fowner_eq), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_FGROUP) {
		snprintf(tbuf, sizeof(tbuf), "%d", __kgid_val(entry->fgroup));
		if (entry->fgroup_op == &vfsgid_gt_kgid)
			seq_printf(m, pt(Opt_fgroup_gt), tbuf);
		else if (entry->fgroup_op == &vfsgid_lt_kgid)
			seq_printf(m, pt(Opt_fgroup_lt), tbuf);
		else
			seq_printf(m, pt(Opt_fgroup_eq), tbuf);
		seq_puts(m, " ");
	}

	if (entry->flags & IMA_VALIDATE_ALGOS) {
		seq_puts(m, "appraise_algos=");
		ima_policy_show_appraise_algos(m, entry->allowed_algos);
		seq_puts(m, " ");
	}

	for (i = 0; i < MAX_LSM_RULES; i++) {
		if (entry->lsm[i].rule) {
			switch (i) {
			case LSM_OBJ_USER:
				seq_printf(m, pt(Opt_obj_user),
					   entry->lsm[i].args_p);
				break;
			case LSM_OBJ_ROLE:
				seq_printf(m, pt(Opt_obj_role),
					   entry->lsm[i].args_p);
				break;
			case LSM_OBJ_TYPE:
				seq_printf(m, pt(Opt_obj_type),
					   entry->lsm[i].args_p);
				break;
			case LSM_SUBJ_USER:
				seq_printf(m, pt(Opt_subj_user),
					   entry->lsm[i].args_p);
				break;
			case LSM_SUBJ_ROLE:
				seq_printf(m, pt(Opt_subj_role),
					   entry->lsm[i].args_p);
				break;
			case LSM_SUBJ_TYPE:
				seq_printf(m, pt(Opt_subj_type),
					   entry->lsm[i].args_p);
				break;
			}
			seq_puts(m, " ");
		}
	}
	if (entry->template)
		seq_printf(m, "template=%s ", entry->template->name);
	if (entry->flags & IMA_DIGSIG_REQUIRED) {
		if (entry->flags & IMA_VERITY_REQUIRED)
			seq_puts(m, "appraise_type=sigv3 ");
		else if (entry->flags & IMA_MODSIG_ALLOWED)
			seq_puts(m, "appraise_type=imasig|modsig ");
		else
			seq_puts(m, "appraise_type=imasig ");
	}
	if (entry->flags & IMA_VERITY_REQUIRED)
		seq_puts(m, "digest_type=verity ");
	if (entry->flags & IMA_CHECK_BLACKLIST)
		seq_puts(m, "appraise_flag=check_blacklist ");
	if (entry->flags & IMA_PERMIT_DIRECTIO)
		seq_puts(m, "permit_directio ");
	rcu_read_unlock();
	seq_puts(m, "\n");
	return 0;
}
#endif	/* CONFIG_IMA_READ_POLICY */

#if defined(CONFIG_IMA_APPRAISE) && defined(CONFIG_INTEGRITY_TRUSTED_KEYRING)
/*
 * ima_appraise_signature: whether IMA will appraise a given function using
 * an IMA digital signature. This is restricted to cases where the kernel
 * has a set of built-in trusted keys in order to avoid an attacker simply
 * loading additional keys.
 */
bool ima_appraise_signature(enum kernel_read_file_id id)
{
	struct ima_rule_entry *entry;
	bool found = false;
	enum ima_hooks func;
	struct list_head *ima_rules_tmp;

	if (id >= READING_MAX_ID)
		return false;

	if (id == READING_KEXEC_IMAGE && !(ima_appraise & IMA_APPRAISE_ENFORCE)
	    && security_locked_down(LOCKDOWN_KEXEC))
		return false;

	func = read_idmap[id] ?: FILE_CHECK;

	rcu_read_lock();
	ima_rules_tmp = rcu_dereference(ima_rules);
	list_for_each_entry_rcu(entry, ima_rules_tmp, list) {
		if (entry->action != APPRAISE)
			continue;

		/*
		 * A generic entry will match, but otherwise require that it
		 * match the func we're looking for
		 */
		if (entry->func && entry->func != func)
			continue;

		/*
		 * We require this to be a digital signature, not a raw IMA
		 * hash.
		 */
		if (entry->flags & IMA_DIGSIG_REQUIRED)
			found = true;

		/*
		 * We've found a rule that matches, so break now even if it
		 * didn't require a digital signature - a later rule that does
		 * won't override it, so would be a false positive.
		 */
		break;
	}

	rcu_read_unlock();
	return found;
}
#endif /* CONFIG_IMA_APPRAISE && CONFIG_INTEGRITY_TRUSTED_KEYRING */
