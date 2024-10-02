// SPDX-License-Identifier: GPL-2.0-or-later
/* auditfilter.c -- filtering of audit events
 *
 * Copyright 2003-2004 Red Hat, Inc.
 * Copyright 2005 Hewlett-Packard Development Company, L.P.
 * Copyright 2005 IBM Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/audit.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/netlink.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include "audit.h"

/*
 * Locking model:
 *
 * audit_filter_mutex:
 *		Synchronizes writes and blocking reads of audit's filterlist
 *		data.  Rcu is used to traverse the filterlist and access
 *		contents of structs audit_entry, audit_watch and opaque
 *		LSM rules during filtering.  If modified, these structures
 *		must be copied and replace their counterparts in the filterlist.
 *		An audit_parent struct is not accessed during filtering, so may
 *		be written directly provided audit_filter_mutex is held.
 */

/* Audit filter lists, defined in <linux/audit.h> */
struct list_head audit_filter_list[AUDIT_NR_FILTERS] = {
	LIST_HEAD_INIT(audit_filter_list[0]),
	LIST_HEAD_INIT(audit_filter_list[1]),
	LIST_HEAD_INIT(audit_filter_list[2]),
	LIST_HEAD_INIT(audit_filter_list[3]),
	LIST_HEAD_INIT(audit_filter_list[4]),
	LIST_HEAD_INIT(audit_filter_list[5]),
	LIST_HEAD_INIT(audit_filter_list[6]),
	LIST_HEAD_INIT(audit_filter_list[7]),
#if AUDIT_NR_FILTERS != 8
#error Fix audit_filter_list initialiser
#endif
};
static struct list_head audit_rules_list[AUDIT_NR_FILTERS] = {
	LIST_HEAD_INIT(audit_rules_list[0]),
	LIST_HEAD_INIT(audit_rules_list[1]),
	LIST_HEAD_INIT(audit_rules_list[2]),
	LIST_HEAD_INIT(audit_rules_list[3]),
	LIST_HEAD_INIT(audit_rules_list[4]),
	LIST_HEAD_INIT(audit_rules_list[5]),
	LIST_HEAD_INIT(audit_rules_list[6]),
	LIST_HEAD_INIT(audit_rules_list[7]),
};

DEFINE_MUTEX(audit_filter_mutex);

static void audit_free_lsm_field(struct audit_field *f)
{
	switch (f->type) {
	case AUDIT_SUBJ_USER:
	case AUDIT_SUBJ_ROLE:
	case AUDIT_SUBJ_TYPE:
	case AUDIT_SUBJ_SEN:
	case AUDIT_SUBJ_CLR:
	case AUDIT_OBJ_USER:
	case AUDIT_OBJ_ROLE:
	case AUDIT_OBJ_TYPE:
	case AUDIT_OBJ_LEV_LOW:
	case AUDIT_OBJ_LEV_HIGH:
		kfree(f->lsm_str);
		security_audit_rule_free(f->lsm_rule);
	}
}

static inline void audit_free_rule(struct audit_entry *e)
{
	int i;
	struct audit_krule *erule = &e->rule;

	/* some rules don't have associated watches */
	if (erule->watch)
		audit_put_watch(erule->watch);
	if (erule->fields)
		for (i = 0; i < erule->field_count; i++)
			audit_free_lsm_field(&erule->fields[i]);
	kfree(erule->fields);
	kfree(erule->filterkey);
	kfree(e);
}

void audit_free_rule_rcu(struct rcu_head *head)
{
	struct audit_entry *e = container_of(head, struct audit_entry, rcu);
	audit_free_rule(e);
}

/* Initialize an audit filterlist entry. */
static inline struct audit_entry *audit_init_entry(u32 field_count)
{
	struct audit_entry *entry;
	struct audit_field *fields;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (unlikely(!entry))
		return NULL;

	fields = kcalloc(field_count, sizeof(*fields), GFP_KERNEL);
	if (unlikely(!fields)) {
		kfree(entry);
		return NULL;
	}
	entry->rule.fields = fields;

	return entry;
}

/* Unpack a filter field's string representation from user-space
 * buffer. */
char *audit_unpack_string(void **bufp, size_t *remain, size_t len)
{
	char *str;

	if (!*bufp || (len == 0) || (len > *remain))
		return ERR_PTR(-EINVAL);

	/* Of the currently implemented string fields, PATH_MAX
	 * defines the longest valid length.
	 */
	if (len > PATH_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	str = kmalloc(len + 1, GFP_KERNEL);
	if (unlikely(!str))
		return ERR_PTR(-ENOMEM);

	memcpy(str, *bufp, len);
	str[len] = 0;
	*bufp += len;
	*remain -= len;

	return str;
}

/* Translate an inode field to kernel representation. */
static inline int audit_to_inode(struct audit_krule *krule,
				 struct audit_field *f)
{
	if ((krule->listnr != AUDIT_FILTER_EXIT &&
	     krule->listnr != AUDIT_FILTER_URING_EXIT) ||
	    krule->inode_f || krule->watch || krule->tree ||
	    (f->op != Audit_equal && f->op != Audit_not_equal))
		return -EINVAL;

	krule->inode_f = f;
	return 0;
}

static __u32 *classes[AUDIT_SYSCALL_CLASSES];

int __init audit_register_class(int class, unsigned *list)
{
	__u32 *p = kcalloc(AUDIT_BITMASK_SIZE, sizeof(__u32), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	while (*list != ~0U) {
		unsigned n = *list++;
		if (n >= AUDIT_BITMASK_SIZE * 32 - AUDIT_SYSCALL_CLASSES) {
			kfree(p);
			return -EINVAL;
		}
		p[AUDIT_WORD(n)] |= AUDIT_BIT(n);
	}
	if (class >= AUDIT_SYSCALL_CLASSES || classes[class]) {
		kfree(p);
		return -EINVAL;
	}
	classes[class] = p;
	return 0;
}

int audit_match_class(int class, unsigned syscall)
{
	if (unlikely(syscall >= AUDIT_BITMASK_SIZE * 32))
		return 0;
	if (unlikely(class >= AUDIT_SYSCALL_CLASSES || !classes[class]))
		return 0;
	return classes[class][AUDIT_WORD(syscall)] & AUDIT_BIT(syscall);
}

#ifdef CONFIG_AUDITSYSCALL
static inline int audit_match_class_bits(int class, u32 *mask)
{
	int i;

	if (classes[class]) {
		for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
			if (mask[i] & classes[class][i])
				return 0;
	}
	return 1;
}

static int audit_match_signal(struct audit_entry *entry)
{
	struct audit_field *arch = entry->rule.arch_f;

	if (!arch) {
		/* When arch is unspecified, we must check both masks on biarch
		 * as syscall number alone is ambiguous. */
		return (audit_match_class_bits(AUDIT_CLASS_SIGNAL,
					       entry->rule.mask) &&
			audit_match_class_bits(AUDIT_CLASS_SIGNAL_32,
					       entry->rule.mask));
	}

	switch (audit_classify_arch(arch->val)) {
	case 0: /* native */
		return (audit_match_class_bits(AUDIT_CLASS_SIGNAL,
					       entry->rule.mask));
	case 1: /* 32bit on biarch */
		return (audit_match_class_bits(AUDIT_CLASS_SIGNAL_32,
					       entry->rule.mask));
	default:
		return 1;
	}
}
#endif

/* Common user-space to kernel rule translation. */
static inline struct audit_entry *audit_to_entry_common(struct audit_rule_data *rule)
{
	unsigned listnr;
	struct audit_entry *entry;
	int i, err;

	err = -EINVAL;
	listnr = rule->flags & ~AUDIT_FILTER_PREPEND;
	switch (listnr) {
	default:
		goto exit_err;
#ifdef CONFIG_AUDITSYSCALL
	case AUDIT_FILTER_ENTRY:
		pr_err("AUDIT_FILTER_ENTRY is deprecated\n");
		goto exit_err;
	case AUDIT_FILTER_EXIT:
	case AUDIT_FILTER_URING_EXIT:
	case AUDIT_FILTER_TASK:
#endif
	case AUDIT_FILTER_USER:
	case AUDIT_FILTER_EXCLUDE:
	case AUDIT_FILTER_FS:
		;
	}
	if (unlikely(rule->action == AUDIT_POSSIBLE)) {
		pr_err("AUDIT_POSSIBLE is deprecated\n");
		goto exit_err;
	}
	if (rule->action != AUDIT_NEVER && rule->action != AUDIT_ALWAYS)
		goto exit_err;
	if (rule->field_count > AUDIT_MAX_FIELDS)
		goto exit_err;

	err = -ENOMEM;
	entry = audit_init_entry(rule->field_count);
	if (!entry)
		goto exit_err;

	entry->rule.flags = rule->flags & AUDIT_FILTER_PREPEND;
	entry->rule.listnr = listnr;
	entry->rule.action = rule->action;
	entry->rule.field_count = rule->field_count;

	for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
		entry->rule.mask[i] = rule->mask[i];

	for (i = 0; i < AUDIT_SYSCALL_CLASSES; i++) {
		int bit = AUDIT_BITMASK_SIZE * 32 - i - 1;
		__u32 *p = &entry->rule.mask[AUDIT_WORD(bit)];
		__u32 *class;

		if (!(*p & AUDIT_BIT(bit)))
			continue;
		*p &= ~AUDIT_BIT(bit);
		class = classes[i];
		if (class) {
			int j;
			for (j = 0; j < AUDIT_BITMASK_SIZE; j++)
				entry->rule.mask[j] |= class[j];
		}
	}

	return entry;

exit_err:
	return ERR_PTR(err);
}

static u32 audit_ops[] =
{
	[Audit_equal] = AUDIT_EQUAL,
	[Audit_not_equal] = AUDIT_NOT_EQUAL,
	[Audit_bitmask] = AUDIT_BIT_MASK,
	[Audit_bittest] = AUDIT_BIT_TEST,
	[Audit_lt] = AUDIT_LESS_THAN,
	[Audit_gt] = AUDIT_GREATER_THAN,
	[Audit_le] = AUDIT_LESS_THAN_OR_EQUAL,
	[Audit_ge] = AUDIT_GREATER_THAN_OR_EQUAL,
};

static u32 audit_to_op(u32 op)
{
	u32 n;
	for (n = Audit_equal; n < Audit_bad && audit_ops[n] != op; n++)
		;
	return n;
}

/* check if an audit field is valid */
static int audit_field_valid(struct audit_entry *entry, struct audit_field *f)
{
	switch (f->type) {
	case AUDIT_MSGTYPE:
		if (entry->rule.listnr != AUDIT_FILTER_EXCLUDE &&
		    entry->rule.listnr != AUDIT_FILTER_USER)
			return -EINVAL;
		break;
	case AUDIT_FSTYPE:
		if (entry->rule.listnr != AUDIT_FILTER_FS)
			return -EINVAL;
		break;
	case AUDIT_PERM:
		if (entry->rule.listnr == AUDIT_FILTER_URING_EXIT)
			return -EINVAL;
		break;
	}

	switch (entry->rule.listnr) {
	case AUDIT_FILTER_FS:
		switch (f->type) {
		case AUDIT_FSTYPE:
		case AUDIT_FILTERKEY:
			break;
		default:
			return -EINVAL;
		}
	}

	/* Check for valid field type and op */
	switch (f->type) {
	case AUDIT_ARG0:
	case AUDIT_ARG1:
	case AUDIT_ARG2:
	case AUDIT_ARG3:
	case AUDIT_PERS: /* <uapi/linux/personality.h> */
	case AUDIT_DEVMINOR:
		/* all ops are valid */
		break;
	case AUDIT_UID:
	case AUDIT_EUID:
	case AUDIT_SUID:
	case AUDIT_FSUID:
	case AUDIT_LOGINUID:
	case AUDIT_OBJ_UID:
	case AUDIT_GID:
	case AUDIT_EGID:
	case AUDIT_SGID:
	case AUDIT_FSGID:
	case AUDIT_OBJ_GID:
	case AUDIT_PID:
	case AUDIT_MSGTYPE:
	case AUDIT_PPID:
	case AUDIT_DEVMAJOR:
	case AUDIT_EXIT:
	case AUDIT_SUCCESS:
	case AUDIT_INODE:
	case AUDIT_SESSIONID:
	case AUDIT_SUBJ_SEN:
	case AUDIT_SUBJ_CLR:
	case AUDIT_OBJ_LEV_LOW:
	case AUDIT_OBJ_LEV_HIGH:
	case AUDIT_SADDR_FAM:
		/* bit ops are only useful on syscall args */
		if (f->op == Audit_bitmask || f->op == Audit_bittest)
			return -EINVAL;
		break;
	case AUDIT_SUBJ_USER:
	case AUDIT_SUBJ_ROLE:
	case AUDIT_SUBJ_TYPE:
	case AUDIT_OBJ_USER:
	case AUDIT_OBJ_ROLE:
	case AUDIT_OBJ_TYPE:
	case AUDIT_WATCH:
	case AUDIT_DIR:
	case AUDIT_FILTERKEY:
	case AUDIT_LOGINUID_SET:
	case AUDIT_ARCH:
	case AUDIT_FSTYPE:
	case AUDIT_PERM:
	case AUDIT_FILETYPE:
	case AUDIT_FIELD_COMPARE:
	case AUDIT_EXE:
		/* only equal and not equal valid ops */
		if (f->op != Audit_not_equal && f->op != Audit_equal)
			return -EINVAL;
		break;
	default:
		/* field not recognized */
		return -EINVAL;
	}

	/* Check for select valid field values */
	switch (f->type) {
	case AUDIT_LOGINUID_SET:
		if ((f->val != 0) && (f->val != 1))
			return -EINVAL;
		break;
	case AUDIT_PERM:
		if (f->val & ~15)
			return -EINVAL;
		break;
	case AUDIT_FILETYPE:
		if (f->val & ~S_IFMT)
			return -EINVAL;
		break;
	case AUDIT_FIELD_COMPARE:
		if (f->val > AUDIT_MAX_FIELD_COMPARE)
			return -EINVAL;
		break;
	case AUDIT_SADDR_FAM:
		if (f->val >= AF_MAX)
			return -EINVAL;
		break;
	default:
		break;
	}

	return 0;
}

/* Translate struct audit_rule_data to kernel's rule representation. */
static struct audit_entry *audit_data_to_entry(struct audit_rule_data *data,
					       size_t datasz)
{
	int err = 0;
	struct audit_entry *entry;
	void *bufp;
	size_t remain = datasz - sizeof(struct audit_rule_data);
	int i;
	char *str;
	struct audit_fsnotify_mark *audit_mark;

	entry = audit_to_entry_common(data);
	if (IS_ERR(entry))
		goto exit_nofree;

	bufp = data->buf;
	for (i = 0; i < data->field_count; i++) {
		struct audit_field *f = &entry->rule.fields[i];
		u32 f_val;

		err = -EINVAL;

		f->op = audit_to_op(data->fieldflags[i]);
		if (f->op == Audit_bad)
			goto exit_free;

		f->type = data->fields[i];
		f_val = data->values[i];

		/* Support legacy tests for a valid loginuid */
		if ((f->type == AUDIT_LOGINUID) && (f_val == AUDIT_UID_UNSET)) {
			f->type = AUDIT_LOGINUID_SET;
			f_val = 0;
			entry->rule.pflags |= AUDIT_LOGINUID_LEGACY;
		}

		err = audit_field_valid(entry, f);
		if (err)
			goto exit_free;

		err = -EINVAL;
		switch (f->type) {
		case AUDIT_LOGINUID:
		case AUDIT_UID:
		case AUDIT_EUID:
		case AUDIT_SUID:
		case AUDIT_FSUID:
		case AUDIT_OBJ_UID:
			f->uid = make_kuid(current_user_ns(), f_val);
			if (!uid_valid(f->uid))
				goto exit_free;
			break;
		case AUDIT_GID:
		case AUDIT_EGID:
		case AUDIT_SGID:
		case AUDIT_FSGID:
		case AUDIT_OBJ_GID:
			f->gid = make_kgid(current_user_ns(), f_val);
			if (!gid_valid(f->gid))
				goto exit_free;
			break;
		case AUDIT_ARCH:
			f->val = f_val;
			entry->rule.arch_f = f;
			break;
		case AUDIT_SUBJ_USER:
		case AUDIT_SUBJ_ROLE:
		case AUDIT_SUBJ_TYPE:
		case AUDIT_SUBJ_SEN:
		case AUDIT_SUBJ_CLR:
		case AUDIT_OBJ_USER:
		case AUDIT_OBJ_ROLE:
		case AUDIT_OBJ_TYPE:
		case AUDIT_OBJ_LEV_LOW:
		case AUDIT_OBJ_LEV_HIGH:
			str = audit_unpack_string(&bufp, &remain, f_val);
			if (IS_ERR(str)) {
				err = PTR_ERR(str);
				goto exit_free;
			}
			entry->rule.buflen += f_val;
			f->lsm_str = str;
			err = security_audit_rule_init(f->type, f->op, str,
						       (void **)&f->lsm_rule,
						       GFP_KERNEL);
			/* Keep currently invalid fields around in case they
			 * become valid after a policy reload. */
			if (err == -EINVAL) {
				pr_warn("audit rule for LSM \'%s\' is invalid\n",
					str);
				err = 0;
			} else if (err)
				goto exit_free;
			break;
		case AUDIT_WATCH:
			str = audit_unpack_string(&bufp, &remain, f_val);
			if (IS_ERR(str)) {
				err = PTR_ERR(str);
				goto exit_free;
			}
			err = audit_to_watch(&entry->rule, str, f_val, f->op);
			if (err) {
				kfree(str);
				goto exit_free;
			}
			entry->rule.buflen += f_val;
			break;
		case AUDIT_DIR:
			str = audit_unpack_string(&bufp, &remain, f_val);
			if (IS_ERR(str)) {
				err = PTR_ERR(str);
				goto exit_free;
			}
			err = audit_make_tree(&entry->rule, str, f->op);
			kfree(str);
			if (err)
				goto exit_free;
			entry->rule.buflen += f_val;
			break;
		case AUDIT_INODE:
			f->val = f_val;
			err = audit_to_inode(&entry->rule, f);
			if (err)
				goto exit_free;
			break;
		case AUDIT_FILTERKEY:
			if (entry->rule.filterkey || f_val > AUDIT_MAX_KEY_LEN)
				goto exit_free;
			str = audit_unpack_string(&bufp, &remain, f_val);
			if (IS_ERR(str)) {
				err = PTR_ERR(str);
				goto exit_free;
			}
			entry->rule.buflen += f_val;
			entry->rule.filterkey = str;
			break;
		case AUDIT_EXE:
			if (entry->rule.exe || f_val > PATH_MAX)
				goto exit_free;
			str = audit_unpack_string(&bufp, &remain, f_val);
			if (IS_ERR(str)) {
				err = PTR_ERR(str);
				goto exit_free;
			}
			audit_mark = audit_alloc_mark(&entry->rule, str, f_val);
			if (IS_ERR(audit_mark)) {
				kfree(str);
				err = PTR_ERR(audit_mark);
				goto exit_free;
			}
			entry->rule.buflen += f_val;
			entry->rule.exe = audit_mark;
			break;
		default:
			f->val = f_val;
			break;
		}
	}

	if (entry->rule.inode_f && entry->rule.inode_f->op == Audit_not_equal)
		entry->rule.inode_f = NULL;

exit_nofree:
	return entry;

exit_free:
	if (entry->rule.tree)
		audit_put_tree(entry->rule.tree); /* that's the temporary one */
	if (entry->rule.exe)
		audit_remove_mark(entry->rule.exe); /* that's the template one */
	audit_free_rule(entry);
	return ERR_PTR(err);
}

/* Pack a filter field's string representation into data block. */
static inline size_t audit_pack_string(void **bufp, const char *str)
{
	size_t len = strlen(str);

	memcpy(*bufp, str, len);
	*bufp += len;

	return len;
}

/* Translate kernel rule representation to struct audit_rule_data. */
static struct audit_rule_data *audit_krule_to_data(struct audit_krule *krule)
{
	struct audit_rule_data *data;
	void *bufp;
	int i;

	data = kmalloc(struct_size(data, buf, krule->buflen), GFP_KERNEL);
	if (unlikely(!data))
		return NULL;
	memset(data, 0, sizeof(*data));

	data->flags = krule->flags | krule->listnr;
	data->action = krule->action;
	data->field_count = krule->field_count;
	bufp = data->buf;
	for (i = 0; i < data->field_count; i++) {
		struct audit_field *f = &krule->fields[i];

		data->fields[i] = f->type;
		data->fieldflags[i] = audit_ops[f->op];
		switch (f->type) {
		case AUDIT_SUBJ_USER:
		case AUDIT_SUBJ_ROLE:
		case AUDIT_SUBJ_TYPE:
		case AUDIT_SUBJ_SEN:
		case AUDIT_SUBJ_CLR:
		case AUDIT_OBJ_USER:
		case AUDIT_OBJ_ROLE:
		case AUDIT_OBJ_TYPE:
		case AUDIT_OBJ_LEV_LOW:
		case AUDIT_OBJ_LEV_HIGH:
			data->buflen += data->values[i] =
				audit_pack_string(&bufp, f->lsm_str);
			break;
		case AUDIT_WATCH:
			data->buflen += data->values[i] =
				audit_pack_string(&bufp,
						  audit_watch_path(krule->watch));
			break;
		case AUDIT_DIR:
			data->buflen += data->values[i] =
				audit_pack_string(&bufp,
						  audit_tree_path(krule->tree));
			break;
		case AUDIT_FILTERKEY:
			data->buflen += data->values[i] =
				audit_pack_string(&bufp, krule->filterkey);
			break;
		case AUDIT_EXE:
			data->buflen += data->values[i] =
				audit_pack_string(&bufp, audit_mark_path(krule->exe));
			break;
		case AUDIT_LOGINUID_SET:
			if (krule->pflags & AUDIT_LOGINUID_LEGACY && !f->val) {
				data->fields[i] = AUDIT_LOGINUID;
				data->values[i] = AUDIT_UID_UNSET;
				break;
			}
			fallthrough;	/* if set */
		default:
			data->values[i] = f->val;
		}
	}
	for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
		data->mask[i] = krule->mask[i];

	return data;
}

/* Compare two rules in kernel format.  Considered success if rules
 * don't match. */
static int audit_compare_rule(struct audit_krule *a, struct audit_krule *b)
{
	int i;

	if (a->flags != b->flags ||
	    a->pflags != b->pflags ||
	    a->listnr != b->listnr ||
	    a->action != b->action ||
	    a->field_count != b->field_count)
		return 1;

	for (i = 0; i < a->field_count; i++) {
		if (a->fields[i].type != b->fields[i].type ||
		    a->fields[i].op != b->fields[i].op)
			return 1;

		switch (a->fields[i].type) {
		case AUDIT_SUBJ_USER:
		case AUDIT_SUBJ_ROLE:
		case AUDIT_SUBJ_TYPE:
		case AUDIT_SUBJ_SEN:
		case AUDIT_SUBJ_CLR:
		case AUDIT_OBJ_USER:
		case AUDIT_OBJ_ROLE:
		case AUDIT_OBJ_TYPE:
		case AUDIT_OBJ_LEV_LOW:
		case AUDIT_OBJ_LEV_HIGH:
			if (strcmp(a->fields[i].lsm_str, b->fields[i].lsm_str))
				return 1;
			break;
		case AUDIT_WATCH:
			if (strcmp(audit_watch_path(a->watch),
				   audit_watch_path(b->watch)))
				return 1;
			break;
		case AUDIT_DIR:
			if (strcmp(audit_tree_path(a->tree),
				   audit_tree_path(b->tree)))
				return 1;
			break;
		case AUDIT_FILTERKEY:
			/* both filterkeys exist based on above type compare */
			if (strcmp(a->filterkey, b->filterkey))
				return 1;
			break;
		case AUDIT_EXE:
			/* both paths exist based on above type compare */
			if (strcmp(audit_mark_path(a->exe),
				   audit_mark_path(b->exe)))
				return 1;
			break;
		case AUDIT_UID:
		case AUDIT_EUID:
		case AUDIT_SUID:
		case AUDIT_FSUID:
		case AUDIT_LOGINUID:
		case AUDIT_OBJ_UID:
			if (!uid_eq(a->fields[i].uid, b->fields[i].uid))
				return 1;
			break;
		case AUDIT_GID:
		case AUDIT_EGID:
		case AUDIT_SGID:
		case AUDIT_FSGID:
		case AUDIT_OBJ_GID:
			if (!gid_eq(a->fields[i].gid, b->fields[i].gid))
				return 1;
			break;
		default:
			if (a->fields[i].val != b->fields[i].val)
				return 1;
		}
	}

	for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
		if (a->mask[i] != b->mask[i])
			return 1;

	return 0;
}

/* Duplicate LSM field information.  The lsm_rule is opaque, so must be
 * re-initialized. */
static inline int audit_dupe_lsm_field(struct audit_field *df,
					   struct audit_field *sf)
{
	int ret;
	char *lsm_str;

	/* our own copy of lsm_str */
	lsm_str = kstrdup(sf->lsm_str, GFP_KERNEL);
	if (unlikely(!lsm_str))
		return -ENOMEM;
	df->lsm_str = lsm_str;

	/* our own (refreshed) copy of lsm_rule */
	ret = security_audit_rule_init(df->type, df->op, df->lsm_str,
				       (void **)&df->lsm_rule, GFP_KERNEL);
	/* Keep currently invalid fields around in case they
	 * become valid after a policy reload. */
	if (ret == -EINVAL) {
		pr_warn("audit rule for LSM \'%s\' is invalid\n",
			df->lsm_str);
		ret = 0;
	}

	return ret;
}

/* Duplicate an audit rule.  This will be a deep copy with the exception
 * of the watch - that pointer is carried over.  The LSM specific fields
 * will be updated in the copy.  The point is to be able to replace the old
 * rule with the new rule in the filterlist, then free the old rule.
 * The rlist element is undefined; list manipulations are handled apart from
 * the initial copy. */
struct audit_entry *audit_dupe_rule(struct audit_krule *old)
{
	u32 fcount = old->field_count;
	struct audit_entry *entry;
	struct audit_krule *new;
	char *fk;
	int i, err = 0;

	entry = audit_init_entry(fcount);
	if (unlikely(!entry))
		return ERR_PTR(-ENOMEM);

	new = &entry->rule;
	new->flags = old->flags;
	new->pflags = old->pflags;
	new->listnr = old->listnr;
	new->action = old->action;
	for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
		new->mask[i] = old->mask[i];
	new->prio = old->prio;
	new->buflen = old->buflen;
	new->inode_f = old->inode_f;
	new->field_count = old->field_count;

	/*
	 * note that we are OK with not refcounting here; audit_match_tree()
	 * never dereferences tree and we can't get false positives there
	 * since we'd have to have rule gone from the list *and* removed
	 * before the chunks found by lookup had been allocated, i.e. before
	 * the beginning of list scan.
	 */
	new->tree = old->tree;
	memcpy(new->fields, old->fields, sizeof(struct audit_field) * fcount);

	/* deep copy this information, updating the lsm_rule fields, because
	 * the originals will all be freed when the old rule is freed. */
	for (i = 0; i < fcount; i++) {
		switch (new->fields[i].type) {
		case AUDIT_SUBJ_USER:
		case AUDIT_SUBJ_ROLE:
		case AUDIT_SUBJ_TYPE:
		case AUDIT_SUBJ_SEN:
		case AUDIT_SUBJ_CLR:
		case AUDIT_OBJ_USER:
		case AUDIT_OBJ_ROLE:
		case AUDIT_OBJ_TYPE:
		case AUDIT_OBJ_LEV_LOW:
		case AUDIT_OBJ_LEV_HIGH:
			err = audit_dupe_lsm_field(&new->fields[i],
						       &old->fields[i]);
			break;
		case AUDIT_FILTERKEY:
			fk = kstrdup(old->filterkey, GFP_KERNEL);
			if (unlikely(!fk))
				err = -ENOMEM;
			else
				new->filterkey = fk;
			break;
		case AUDIT_EXE:
			err = audit_dupe_exe(new, old);
			break;
		}
		if (err) {
			if (new->exe)
				audit_remove_mark(new->exe);
			audit_free_rule(entry);
			return ERR_PTR(err);
		}
	}

	if (old->watch) {
		audit_get_watch(old->watch);
		new->watch = old->watch;
	}

	return entry;
}

/* Find an existing audit rule.
 * Caller must hold audit_filter_mutex to prevent stale rule data. */
static struct audit_entry *audit_find_rule(struct audit_entry *entry,
					   struct list_head **p)
{
	struct audit_entry *e, *found = NULL;
	struct list_head *list;
	int h;

	if (entry->rule.inode_f) {
		h = audit_hash_ino(entry->rule.inode_f->val);
		*p = list = &audit_inode_hash[h];
	} else if (entry->rule.watch) {
		/* we don't know the inode number, so must walk entire hash */
		for (h = 0; h < AUDIT_INODE_BUCKETS; h++) {
			list = &audit_inode_hash[h];
			list_for_each_entry(e, list, list)
				if (!audit_compare_rule(&entry->rule, &e->rule)) {
					found = e;
					goto out;
				}
		}
		goto out;
	} else {
		*p = list = &audit_filter_list[entry->rule.listnr];
	}

	list_for_each_entry(e, list, list)
		if (!audit_compare_rule(&entry->rule, &e->rule)) {
			found = e;
			goto out;
		}

out:
	return found;
}

static u64 prio_low = ~0ULL/2;
static u64 prio_high = ~0ULL/2 - 1;

/* Add rule to given filterlist if not a duplicate. */
static inline int audit_add_rule(struct audit_entry *entry)
{
	struct audit_entry *e;
	struct audit_watch *watch = entry->rule.watch;
	struct audit_tree *tree = entry->rule.tree;
	struct list_head *list;
	int err = 0;
#ifdef CONFIG_AUDITSYSCALL
	int dont_count = 0;

	/* If any of these, don't count towards total */
	switch (entry->rule.listnr) {
	case AUDIT_FILTER_USER:
	case AUDIT_FILTER_EXCLUDE:
	case AUDIT_FILTER_FS:
		dont_count = 1;
	}
#endif

	mutex_lock(&audit_filter_mutex);
	e = audit_find_rule(entry, &list);
	if (e) {
		mutex_unlock(&audit_filter_mutex);
		err = -EEXIST;
		/* normally audit_add_tree_rule() will free it on failure */
		if (tree)
			audit_put_tree(tree);
		return err;
	}

	if (watch) {
		/* audit_filter_mutex is dropped and re-taken during this call */
		err = audit_add_watch(&entry->rule, &list);
		if (err) {
			mutex_unlock(&audit_filter_mutex);
			/*
			 * normally audit_add_tree_rule() will free it
			 * on failure
			 */
			if (tree)
				audit_put_tree(tree);
			return err;
		}
	}
	if (tree) {
		err = audit_add_tree_rule(&entry->rule);
		if (err) {
			mutex_unlock(&audit_filter_mutex);
			return err;
		}
	}

	entry->rule.prio = ~0ULL;
	if (entry->rule.listnr == AUDIT_FILTER_EXIT ||
	    entry->rule.listnr == AUDIT_FILTER_URING_EXIT) {
		if (entry->rule.flags & AUDIT_FILTER_PREPEND)
			entry->rule.prio = ++prio_high;
		else
			entry->rule.prio = --prio_low;
	}

	if (entry->rule.flags & AUDIT_FILTER_PREPEND) {
		list_add(&entry->rule.list,
			 &audit_rules_list[entry->rule.listnr]);
		list_add_rcu(&entry->list, list);
		entry->rule.flags &= ~AUDIT_FILTER_PREPEND;
	} else {
		list_add_tail(&entry->rule.list,
			      &audit_rules_list[entry->rule.listnr]);
		list_add_tail_rcu(&entry->list, list);
	}
#ifdef CONFIG_AUDITSYSCALL
	if (!dont_count)
		audit_n_rules++;

	if (!audit_match_signal(entry))
		audit_signals++;
#endif
	mutex_unlock(&audit_filter_mutex);

	return err;
}

/* Remove an existing rule from filterlist. */
int audit_del_rule(struct audit_entry *entry)
{
	struct audit_entry  *e;
	struct audit_tree *tree = entry->rule.tree;
	struct list_head *list;
	int ret = 0;
#ifdef CONFIG_AUDITSYSCALL
	int dont_count = 0;

	/* If any of these, don't count towards total */
	switch (entry->rule.listnr) {
	case AUDIT_FILTER_USER:
	case AUDIT_FILTER_EXCLUDE:
	case AUDIT_FILTER_FS:
		dont_count = 1;
	}
#endif

	mutex_lock(&audit_filter_mutex);
	e = audit_find_rule(entry, &list);
	if (!e) {
		ret = -ENOENT;
		goto out;
	}

	if (e->rule.watch)
		audit_remove_watch_rule(&e->rule);

	if (e->rule.tree)
		audit_remove_tree_rule(&e->rule);

	if (e->rule.exe)
		audit_remove_mark_rule(&e->rule);

#ifdef CONFIG_AUDITSYSCALL
	if (!dont_count)
		audit_n_rules--;

	if (!audit_match_signal(entry))
		audit_signals--;
#endif

	list_del_rcu(&e->list);
	list_del(&e->rule.list);
	call_rcu(&e->rcu, audit_free_rule_rcu);

out:
	mutex_unlock(&audit_filter_mutex);

	if (tree)
		audit_put_tree(tree);	/* that's the temporary one */

	return ret;
}

/* List rules using struct audit_rule_data. */
static void audit_list_rules(int seq, struct sk_buff_head *q)
{
	struct sk_buff *skb;
	struct audit_krule *r;
	int i;

	/* This is a blocking read, so use audit_filter_mutex instead of rcu
	 * iterator to sync with list writers. */
	for (i = 0; i < AUDIT_NR_FILTERS; i++) {
		list_for_each_entry(r, &audit_rules_list[i], list) {
			struct audit_rule_data *data;

			data = audit_krule_to_data(r);
			if (unlikely(!data))
				break;
			skb = audit_make_reply(seq, AUDIT_LIST_RULES, 0, 1,
					       data,
					       struct_size(data, buf, data->buflen));
			if (skb)
				skb_queue_tail(q, skb);
			kfree(data);
		}
	}
	skb = audit_make_reply(seq, AUDIT_LIST_RULES, 1, 1, NULL, 0);
	if (skb)
		skb_queue_tail(q, skb);
}

/* Log rule additions and removals */
static void audit_log_rule_change(char *action, struct audit_krule *rule, int res)
{
	struct audit_buffer *ab;

	if (!audit_enabled)
		return;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_CONFIG_CHANGE);
	if (!ab)
		return;
	audit_log_session_info(ab);
	audit_log_task_context(ab);
	audit_log_format(ab, " op=%s", action);
	audit_log_key(ab, rule->filterkey);
	audit_log_format(ab, " list=%d res=%d", rule->listnr, res);
	audit_log_end(ab);
}

/**
 * audit_rule_change - apply all rules to the specified message type
 * @type: audit message type
 * @seq: netlink audit message sequence (serial) number
 * @data: payload data
 * @datasz: size of payload data
 */
int audit_rule_change(int type, int seq, void *data, size_t datasz)
{
	int err = 0;
	struct audit_entry *entry;

	switch (type) {
	case AUDIT_ADD_RULE:
		entry = audit_data_to_entry(data, datasz);
		if (IS_ERR(entry))
			return PTR_ERR(entry);
		err = audit_add_rule(entry);
		audit_log_rule_change("add_rule", &entry->rule, !err);
		break;
	case AUDIT_DEL_RULE:
		entry = audit_data_to_entry(data, datasz);
		if (IS_ERR(entry))
			return PTR_ERR(entry);
		err = audit_del_rule(entry);
		audit_log_rule_change("remove_rule", &entry->rule, !err);
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (err || type == AUDIT_DEL_RULE) {
		if (entry->rule.exe)
			audit_remove_mark(entry->rule.exe);
		audit_free_rule(entry);
	}

	return err;
}

/**
 * audit_list_rules_send - list the audit rules
 * @request_skb: skb of request we are replying to (used to target the reply)
 * @seq: netlink audit message sequence (serial) number
 */
int audit_list_rules_send(struct sk_buff *request_skb, int seq)
{
	struct task_struct *tsk;
	struct audit_netlink_list *dest;

	/* We can't just spew out the rules here because we might fill
	 * the available socket buffer space and deadlock waiting for
	 * auditctl to read from it... which isn't ever going to
	 * happen if we're actually running in the context of auditctl
	 * trying to _send_ the stuff */

	dest = kmalloc(sizeof(*dest), GFP_KERNEL);
	if (!dest)
		return -ENOMEM;
	dest->net = get_net(sock_net(NETLINK_CB(request_skb).sk));
	dest->portid = NETLINK_CB(request_skb).portid;
	skb_queue_head_init(&dest->q);

	mutex_lock(&audit_filter_mutex);
	audit_list_rules(seq, &dest->q);
	mutex_unlock(&audit_filter_mutex);

	tsk = kthread_run(audit_send_list_thread, dest, "audit_send_list");
	if (IS_ERR(tsk)) {
		skb_queue_purge(&dest->q);
		put_net(dest->net);
		kfree(dest);
		return PTR_ERR(tsk);
	}

	return 0;
}

int audit_comparator(u32 left, u32 op, u32 right)
{
	switch (op) {
	case Audit_equal:
		return (left == right);
	case Audit_not_equal:
		return (left != right);
	case Audit_lt:
		return (left < right);
	case Audit_le:
		return (left <= right);
	case Audit_gt:
		return (left > right);
	case Audit_ge:
		return (left >= right);
	case Audit_bitmask:
		return (left & right);
	case Audit_bittest:
		return ((left & right) == right);
	default:
		return 0;
	}
}

int audit_uid_comparator(kuid_t left, u32 op, kuid_t right)
{
	switch (op) {
	case Audit_equal:
		return uid_eq(left, right);
	case Audit_not_equal:
		return !uid_eq(left, right);
	case Audit_lt:
		return uid_lt(left, right);
	case Audit_le:
		return uid_lte(left, right);
	case Audit_gt:
		return uid_gt(left, right);
	case Audit_ge:
		return uid_gte(left, right);
	case Audit_bitmask:
	case Audit_bittest:
	default:
		return 0;
	}
}

int audit_gid_comparator(kgid_t left, u32 op, kgid_t right)
{
	switch (op) {
	case Audit_equal:
		return gid_eq(left, right);
	case Audit_not_equal:
		return !gid_eq(left, right);
	case Audit_lt:
		return gid_lt(left, right);
	case Audit_le:
		return gid_lte(left, right);
	case Audit_gt:
		return gid_gt(left, right);
	case Audit_ge:
		return gid_gte(left, right);
	case Audit_bitmask:
	case Audit_bittest:
	default:
		return 0;
	}
}

/**
 * parent_len - find the length of the parent portion of a pathname
 * @path: pathname of which to determine length
 */
int parent_len(const char *path)
{
	int plen;
	const char *p;

	plen = strlen(path);

	if (plen == 0)
		return plen;

	/* disregard trailing slashes */
	p = path + plen - 1;
	while ((*p == '/') && (p > path))
		p--;

	/* walk backward until we find the next slash or hit beginning */
	while ((*p != '/') && (p > path))
		p--;

	/* did we find a slash? Then increment to include it in path */
	if (*p == '/')
		p++;

	return p - path;
}

/**
 * audit_compare_dname_path - compare given dentry name with last component in
 * 			      given path. Return of 0 indicates a match.
 * @dname:	dentry name that we're comparing
 * @path:	full pathname that we're comparing
 * @parentlen:	length of the parent if known. Passing in AUDIT_NAME_FULL
 * 		here indicates that we must compute this value.
 */
int audit_compare_dname_path(const struct qstr *dname, const char *path, int parentlen)
{
	int dlen, pathlen;
	const char *p;

	dlen = dname->len;
	pathlen = strlen(path);
	if (pathlen < dlen)
		return 1;

	parentlen = parentlen == AUDIT_NAME_FULL ? parent_len(path) : parentlen;
	if (pathlen - parentlen != dlen)
		return 1;

	p = path + parentlen;

	return strncmp(p, dname->name, dlen);
}

int audit_filter(int msgtype, unsigned int listtype)
{
	struct audit_entry *e;
	int ret = 1; /* Audit by default */

	rcu_read_lock();
	list_for_each_entry_rcu(e, &audit_filter_list[listtype], list) {
		int i, result = 0;

		for (i = 0; i < e->rule.field_count; i++) {
			struct audit_field *f = &e->rule.fields[i];
			pid_t pid;
			u32 sid;

			switch (f->type) {
			case AUDIT_PID:
				pid = task_tgid_nr(current);
				result = audit_comparator(pid, f->op, f->val);
				break;
			case AUDIT_UID:
				result = audit_uid_comparator(current_uid(), f->op, f->uid);
				break;
			case AUDIT_GID:
				result = audit_gid_comparator(current_gid(), f->op, f->gid);
				break;
			case AUDIT_LOGINUID:
				result = audit_uid_comparator(audit_get_loginuid(current),
							      f->op, f->uid);
				break;
			case AUDIT_LOGINUID_SET:
				result = audit_comparator(audit_loginuid_set(current),
							  f->op, f->val);
				break;
			case AUDIT_MSGTYPE:
				result = audit_comparator(msgtype, f->op, f->val);
				break;
			case AUDIT_SUBJ_USER:
			case AUDIT_SUBJ_ROLE:
			case AUDIT_SUBJ_TYPE:
			case AUDIT_SUBJ_SEN:
			case AUDIT_SUBJ_CLR:
				if (f->lsm_rule) {
					security_current_getsecid_subj(&sid);
					result = security_audit_rule_match(sid,
						   f->type, f->op, f->lsm_rule);
				}
				break;
			case AUDIT_EXE:
				result = audit_exe_compare(current, e->rule.exe);
				if (f->op == Audit_not_equal)
					result = !result;
				break;
			default:
				goto unlock_and_return;
			}
			if (result < 0) /* error */
				goto unlock_and_return;
			if (!result)
				break;
		}
		if (result > 0) {
			if (e->rule.action == AUDIT_NEVER || listtype == AUDIT_FILTER_EXCLUDE)
				ret = 0;
			break;
		}
	}
unlock_and_return:
	rcu_read_unlock();
	return ret;
}

static int update_lsm_rule(struct audit_krule *r)
{
	struct audit_entry *entry = container_of(r, struct audit_entry, rule);
	struct audit_entry *nentry;
	int err = 0;

	if (!security_audit_rule_known(r))
		return 0;

	nentry = audit_dupe_rule(r);
	if (entry->rule.exe)
		audit_remove_mark(entry->rule.exe);
	if (IS_ERR(nentry)) {
		/* save the first error encountered for the
		 * return value */
		err = PTR_ERR(nentry);
		audit_panic("error updating LSM filters");
		if (r->watch)
			list_del(&r->rlist);
		list_del_rcu(&entry->list);
		list_del(&r->list);
	} else {
		if (r->watch || r->tree)
			list_replace_init(&r->rlist, &nentry->rule.rlist);
		list_replace_rcu(&entry->list, &nentry->list);
		list_replace(&r->list, &nentry->rule.list);
	}
	call_rcu(&entry->rcu, audit_free_rule_rcu);

	return err;
}

/* This function will re-initialize the lsm_rule field of all applicable rules.
 * It will traverse the filter lists serarching for rules that contain LSM
 * specific filter fields.  When such a rule is found, it is copied, the
 * LSM field is re-initialized, and the old rule is replaced with the
 * updated rule. */
int audit_update_lsm_rules(void)
{
	struct audit_krule *r, *n;
	int i, err = 0;

	/* audit_filter_mutex synchronizes the writers */
	mutex_lock(&audit_filter_mutex);

	for (i = 0; i < AUDIT_NR_FILTERS; i++) {
		list_for_each_entry_safe(r, n, &audit_rules_list[i], list) {
			int res = update_lsm_rule(r);
			if (!err)
				err = res;
		}
	}
	mutex_unlock(&audit_filter_mutex);

	return err;
}
