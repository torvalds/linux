/* auditfilter.c -- filtering of audit events
 *
 * Copyright 2003-2004 Red Hat, Inc.
 * Copyright 2005 Hewlett-Packard Development Company, L.P.
 * Copyright 2005 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/audit.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/netlink.h>
#include <linux/sched.h>
#include <linux/inotify.h>
#include <linux/security.h>
#include "audit.h"

/*
 * Locking model:
 *
 * audit_filter_mutex:
 * 		Synchronizes writes and blocking reads of audit's filterlist
 * 		data.  Rcu is used to traverse the filterlist and access
 * 		contents of structs audit_entry, audit_watch and opaque
 * 		LSM rules during filtering.  If modified, these structures
 * 		must be copied and replace their counterparts in the filterlist.
 * 		An audit_parent struct is not accessed during filtering, so may
 * 		be written directly provided audit_filter_mutex is held.
 */

/*
 * Reference counting:
 *
 * audit_parent: lifetime is from audit_init_parent() to receipt of an IN_IGNORED
 * 	event.  Each audit_watch holds a reference to its associated parent.
 *
 * audit_watch: if added to lists, lifetime is from audit_init_watch() to
 * 	audit_remove_watch().  Additionally, an audit_watch may exist
 * 	temporarily to assist in searching existing filter data.  Each
 * 	audit_krule holds a reference to its associated watch.
 */

struct audit_parent {
	struct list_head	ilist;	/* entry in inotify registration list */
	struct list_head	watches; /* associated watches */
	struct inotify_watch	wdata;	/* inotify watch data */
	unsigned		flags;	/* status flags */
};

/*
 * audit_parent status flags:
 *
 * AUDIT_PARENT_INVALID - set anytime rules/watches are auto-removed due to
 * a filesystem event to ensure we're adding audit watches to a valid parent.
 * Technically not needed for IN_DELETE_SELF or IN_UNMOUNT events, as we cannot
 * receive them while we have nameidata, but must be used for IN_MOVE_SELF which
 * we can receive while holding nameidata.
 */
#define AUDIT_PARENT_INVALID	0x001

/* Audit filter lists, defined in <linux/audit.h> */
struct list_head audit_filter_list[AUDIT_NR_FILTERS] = {
	LIST_HEAD_INIT(audit_filter_list[0]),
	LIST_HEAD_INIT(audit_filter_list[1]),
	LIST_HEAD_INIT(audit_filter_list[2]),
	LIST_HEAD_INIT(audit_filter_list[3]),
	LIST_HEAD_INIT(audit_filter_list[4]),
	LIST_HEAD_INIT(audit_filter_list[5]),
#if AUDIT_NR_FILTERS != 6
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
};

DEFINE_MUTEX(audit_filter_mutex);

/* Inotify events we care about. */
#define AUDIT_IN_WATCH IN_MOVE|IN_CREATE|IN_DELETE|IN_DELETE_SELF|IN_MOVE_SELF

void audit_free_parent(struct inotify_watch *i_watch)
{
	struct audit_parent *parent;

	parent = container_of(i_watch, struct audit_parent, wdata);
	WARN_ON(!list_empty(&parent->watches));
	kfree(parent);
}

static inline void audit_get_watch(struct audit_watch *watch)
{
	atomic_inc(&watch->count);
}

static void audit_put_watch(struct audit_watch *watch)
{
	if (atomic_dec_and_test(&watch->count)) {
		WARN_ON(watch->parent);
		WARN_ON(!list_empty(&watch->rules));
		kfree(watch->path);
		kfree(watch);
	}
}

static void audit_remove_watch(struct audit_watch *watch)
{
	list_del(&watch->wlist);
	put_inotify_watch(&watch->parent->wdata);
	watch->parent = NULL;
	audit_put_watch(watch); /* match initial get */
}

static inline void audit_free_rule(struct audit_entry *e)
{
	int i;
	struct audit_krule *erule = &e->rule;
	/* some rules don't have associated watches */
	if (erule->watch)
		audit_put_watch(erule->watch);
	if (erule->fields)
		for (i = 0; i < erule->field_count; i++) {
			struct audit_field *f = &erule->fields[i];
			kfree(f->lsm_str);
			security_audit_rule_free(f->lsm_rule);
		}
	kfree(erule->fields);
	kfree(erule->filterkey);
	kfree(e);
}

void audit_free_rule_rcu(struct rcu_head *head)
{
	struct audit_entry *e = container_of(head, struct audit_entry, rcu);
	audit_free_rule(e);
}

/* Initialize a parent watch entry. */
static struct audit_parent *audit_init_parent(struct nameidata *ndp)
{
	struct audit_parent *parent;
	s32 wd;

	parent = kzalloc(sizeof(*parent), GFP_KERNEL);
	if (unlikely(!parent))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&parent->watches);
	parent->flags = 0;

	inotify_init_watch(&parent->wdata);
	/* grab a ref so inotify watch hangs around until we take audit_filter_mutex */
	get_inotify_watch(&parent->wdata);
	wd = inotify_add_watch(audit_ih, &parent->wdata,
			       ndp->path.dentry->d_inode, AUDIT_IN_WATCH);
	if (wd < 0) {
		audit_free_parent(&parent->wdata);
		return ERR_PTR(wd);
	}

	return parent;
}

/* Initialize a watch entry. */
static struct audit_watch *audit_init_watch(char *path)
{
	struct audit_watch *watch;

	watch = kzalloc(sizeof(*watch), GFP_KERNEL);
	if (unlikely(!watch))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&watch->rules);
	atomic_set(&watch->count, 1);
	watch->path = path;
	watch->dev = (dev_t)-1;
	watch->ino = (unsigned long)-1;

	return watch;
}

/* Initialize an audit filterlist entry. */
static inline struct audit_entry *audit_init_entry(u32 field_count)
{
	struct audit_entry *entry;
	struct audit_field *fields;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (unlikely(!entry))
		return NULL;

	fields = kzalloc(sizeof(*fields) * field_count, GFP_KERNEL);
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

/* Translate an inode field to kernel respresentation. */
static inline int audit_to_inode(struct audit_krule *krule,
				 struct audit_field *f)
{
	if (krule->listnr != AUDIT_FILTER_EXIT ||
	    krule->watch || krule->inode_f || krule->tree ||
	    (f->op != Audit_equal && f->op != Audit_not_equal))
		return -EINVAL;

	krule->inode_f = f;
	return 0;
}

/* Translate a watch string to kernel respresentation. */
static int audit_to_watch(struct audit_krule *krule, char *path, int len,
			  u32 op)
{
	struct audit_watch *watch;

	if (!audit_ih)
		return -EOPNOTSUPP;

	if (path[0] != '/' || path[len-1] == '/' ||
	    krule->listnr != AUDIT_FILTER_EXIT ||
	    op != Audit_equal ||
	    krule->inode_f || krule->watch || krule->tree)
		return -EINVAL;

	watch = audit_init_watch(path);
	if (IS_ERR(watch))
		return PTR_ERR(watch);

	audit_get_watch(watch);
	krule->watch = watch;

	return 0;
}

static __u32 *classes[AUDIT_SYSCALL_CLASSES];

int __init audit_register_class(int class, unsigned *list)
{
	__u32 *p = kzalloc(AUDIT_BITMASK_SIZE * sizeof(__u32), GFP_KERNEL);
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

	switch(audit_classify_arch(arch->val)) {
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
static inline struct audit_entry *audit_to_entry_common(struct audit_rule *rule)
{
	unsigned listnr;
	struct audit_entry *entry;
	int i, err;

	err = -EINVAL;
	listnr = rule->flags & ~AUDIT_FILTER_PREPEND;
	switch(listnr) {
	default:
		goto exit_err;
	case AUDIT_FILTER_USER:
	case AUDIT_FILTER_TYPE:
#ifdef CONFIG_AUDITSYSCALL
	case AUDIT_FILTER_ENTRY:
	case AUDIT_FILTER_EXIT:
	case AUDIT_FILTER_TASK:
#endif
		;
	}
	if (unlikely(rule->action == AUDIT_POSSIBLE)) {
		printk(KERN_ERR "AUDIT_POSSIBLE is deprecated\n");
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


/* Translate struct audit_rule to kernel's rule respresentation.
 * Exists for backward compatibility with userspace. */
static struct audit_entry *audit_rule_to_entry(struct audit_rule *rule)
{
	struct audit_entry *entry;
	int err = 0;
	int i;

	entry = audit_to_entry_common(rule);
	if (IS_ERR(entry))
		goto exit_nofree;

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &entry->rule.fields[i];
		u32 n;

		n = rule->fields[i] & (AUDIT_NEGATE|AUDIT_OPERATORS);

		/* Support for legacy operators where
		 * AUDIT_NEGATE bit signifies != and otherwise assumes == */
		if (n & AUDIT_NEGATE)
			f->op = Audit_not_equal;
		else if (!n)
			f->op = Audit_equal;
		else
			f->op = audit_to_op(n);

		entry->rule.vers_ops = (n & AUDIT_OPERATORS) ? 2 : 1;

		f->type = rule->fields[i] & ~(AUDIT_NEGATE|AUDIT_OPERATORS);
		f->val = rule->values[i];

		err = -EINVAL;
		if (f->op == Audit_bad)
			goto exit_free;

		switch(f->type) {
		default:
			goto exit_free;
		case AUDIT_PID:
		case AUDIT_UID:
		case AUDIT_EUID:
		case AUDIT_SUID:
		case AUDIT_FSUID:
		case AUDIT_GID:
		case AUDIT_EGID:
		case AUDIT_SGID:
		case AUDIT_FSGID:
		case AUDIT_LOGINUID:
		case AUDIT_PERS:
		case AUDIT_MSGTYPE:
		case AUDIT_PPID:
		case AUDIT_DEVMAJOR:
		case AUDIT_DEVMINOR:
		case AUDIT_EXIT:
		case AUDIT_SUCCESS:
			/* bit ops are only useful on syscall args */
			if (f->op == Audit_bitmask || f->op == Audit_bittest)
				goto exit_free;
			break;
		case AUDIT_ARG0:
		case AUDIT_ARG1:
		case AUDIT_ARG2:
		case AUDIT_ARG3:
			break;
		/* arch is only allowed to be = or != */
		case AUDIT_ARCH:
			if (f->op != Audit_not_equal && f->op != Audit_equal)
				goto exit_free;
			entry->rule.arch_f = f;
			break;
		case AUDIT_PERM:
			if (f->val & ~15)
				goto exit_free;
			break;
		case AUDIT_FILETYPE:
			if ((f->val & ~S_IFMT) > S_IFMT)
				goto exit_free;
			break;
		case AUDIT_INODE:
			err = audit_to_inode(&entry->rule, f);
			if (err)
				goto exit_free;
			break;
		}
	}

	if (entry->rule.inode_f && entry->rule.inode_f->op == Audit_not_equal)
		entry->rule.inode_f = NULL;

exit_nofree:
	return entry;

exit_free:
	audit_free_rule(entry);
	return ERR_PTR(err);
}

/* Translate struct audit_rule_data to kernel's rule respresentation. */
static struct audit_entry *audit_data_to_entry(struct audit_rule_data *data,
					       size_t datasz)
{
	int err = 0;
	struct audit_entry *entry;
	void *bufp;
	size_t remain = datasz - sizeof(struct audit_rule_data);
	int i;
	char *str;

	entry = audit_to_entry_common((struct audit_rule *)data);
	if (IS_ERR(entry))
		goto exit_nofree;

	bufp = data->buf;
	entry->rule.vers_ops = 2;
	for (i = 0; i < data->field_count; i++) {
		struct audit_field *f = &entry->rule.fields[i];

		err = -EINVAL;

		f->op = audit_to_op(data->fieldflags[i]);
		if (f->op == Audit_bad)
			goto exit_free;

		f->type = data->fields[i];
		f->val = data->values[i];
		f->lsm_str = NULL;
		f->lsm_rule = NULL;
		switch(f->type) {
		case AUDIT_PID:
		case AUDIT_UID:
		case AUDIT_EUID:
		case AUDIT_SUID:
		case AUDIT_FSUID:
		case AUDIT_GID:
		case AUDIT_EGID:
		case AUDIT_SGID:
		case AUDIT_FSGID:
		case AUDIT_LOGINUID:
		case AUDIT_PERS:
		case AUDIT_MSGTYPE:
		case AUDIT_PPID:
		case AUDIT_DEVMAJOR:
		case AUDIT_DEVMINOR:
		case AUDIT_EXIT:
		case AUDIT_SUCCESS:
		case AUDIT_ARG0:
		case AUDIT_ARG1:
		case AUDIT_ARG2:
		case AUDIT_ARG3:
			break;
		case AUDIT_ARCH:
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
			str = audit_unpack_string(&bufp, &remain, f->val);
			if (IS_ERR(str))
				goto exit_free;
			entry->rule.buflen += f->val;

			err = security_audit_rule_init(f->type, f->op, str,
						       (void **)&f->lsm_rule);
			/* Keep currently invalid fields around in case they
			 * become valid after a policy reload. */
			if (err == -EINVAL) {
				printk(KERN_WARNING "audit rule for LSM "
				       "\'%s\' is invalid\n",  str);
				err = 0;
			}
			if (err) {
				kfree(str);
				goto exit_free;
			} else
				f->lsm_str = str;
			break;
		case AUDIT_WATCH:
			str = audit_unpack_string(&bufp, &remain, f->val);
			if (IS_ERR(str))
				goto exit_free;
			entry->rule.buflen += f->val;

			err = audit_to_watch(&entry->rule, str, f->val, f->op);
			if (err) {
				kfree(str);
				goto exit_free;
			}
			break;
		case AUDIT_DIR:
			str = audit_unpack_string(&bufp, &remain, f->val);
			if (IS_ERR(str))
				goto exit_free;
			entry->rule.buflen += f->val;

			err = audit_make_tree(&entry->rule, str, f->op);
			kfree(str);
			if (err)
				goto exit_free;
			break;
		case AUDIT_INODE:
			err = audit_to_inode(&entry->rule, f);
			if (err)
				goto exit_free;
			break;
		case AUDIT_FILTERKEY:
			err = -EINVAL;
			if (entry->rule.filterkey || f->val > AUDIT_MAX_KEY_LEN)
				goto exit_free;
			str = audit_unpack_string(&bufp, &remain, f->val);
			if (IS_ERR(str))
				goto exit_free;
			entry->rule.buflen += f->val;
			entry->rule.filterkey = str;
			break;
		case AUDIT_PERM:
			if (f->val & ~15)
				goto exit_free;
			break;
		case AUDIT_FILETYPE:
			if ((f->val & ~S_IFMT) > S_IFMT)
				goto exit_free;
			break;
		default:
			goto exit_free;
		}
	}

	if (entry->rule.inode_f && entry->rule.inode_f->op == Audit_not_equal)
		entry->rule.inode_f = NULL;

exit_nofree:
	return entry;

exit_free:
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

/* Translate kernel rule respresentation to struct audit_rule.
 * Exists for backward compatibility with userspace. */
static struct audit_rule *audit_krule_to_rule(struct audit_krule *krule)
{
	struct audit_rule *rule;
	int i;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (unlikely(!rule))
		return NULL;

	rule->flags = krule->flags | krule->listnr;
	rule->action = krule->action;
	rule->field_count = krule->field_count;
	for (i = 0; i < rule->field_count; i++) {
		rule->values[i] = krule->fields[i].val;
		rule->fields[i] = krule->fields[i].type;

		if (krule->vers_ops == 1) {
			if (krule->fields[i].op == Audit_not_equal)
				rule->fields[i] |= AUDIT_NEGATE;
		} else {
			rule->fields[i] |= audit_ops[krule->fields[i].op];
		}
	}
	for (i = 0; i < AUDIT_BITMASK_SIZE; i++) rule->mask[i] = krule->mask[i];

	return rule;
}

/* Translate kernel rule respresentation to struct audit_rule_data. */
static struct audit_rule_data *audit_krule_to_data(struct audit_krule *krule)
{
	struct audit_rule_data *data;
	void *bufp;
	int i;

	data = kmalloc(sizeof(*data) + krule->buflen, GFP_KERNEL);
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
		switch(f->type) {
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
				audit_pack_string(&bufp, krule->watch->path);
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
		default:
			data->values[i] = f->val;
		}
	}
	for (i = 0; i < AUDIT_BITMASK_SIZE; i++) data->mask[i] = krule->mask[i];

	return data;
}

/* Compare two rules in kernel format.  Considered success if rules
 * don't match. */
static int audit_compare_rule(struct audit_krule *a, struct audit_krule *b)
{
	int i;

	if (a->flags != b->flags ||
	    a->listnr != b->listnr ||
	    a->action != b->action ||
	    a->field_count != b->field_count)
		return 1;

	for (i = 0; i < a->field_count; i++) {
		if (a->fields[i].type != b->fields[i].type ||
		    a->fields[i].op != b->fields[i].op)
			return 1;

		switch(a->fields[i].type) {
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
			if (strcmp(a->watch->path, b->watch->path))
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

/* Duplicate the given audit watch.  The new watch's rules list is initialized
 * to an empty list and wlist is undefined. */
static struct audit_watch *audit_dupe_watch(struct audit_watch *old)
{
	char *path;
	struct audit_watch *new;

	path = kstrdup(old->path, GFP_KERNEL);
	if (unlikely(!path))
		return ERR_PTR(-ENOMEM);

	new = audit_init_watch(path);
	if (IS_ERR(new)) {
		kfree(path);
		goto out;
	}

	new->dev = old->dev;
	new->ino = old->ino;
	get_inotify_watch(&old->parent->wdata);
	new->parent = old->parent;

out:
	return new;
}

/* Duplicate LSM field information.  The lsm_rule is opaque, so must be
 * re-initialized. */
static inline int audit_dupe_lsm_field(struct audit_field *df,
					   struct audit_field *sf)
{
	int ret = 0;
	char *lsm_str;

	/* our own copy of lsm_str */
	lsm_str = kstrdup(sf->lsm_str, GFP_KERNEL);
	if (unlikely(!lsm_str))
		return -ENOMEM;
	df->lsm_str = lsm_str;

	/* our own (refreshed) copy of lsm_rule */
	ret = security_audit_rule_init(df->type, df->op, df->lsm_str,
				       (void **)&df->lsm_rule);
	/* Keep currently invalid fields around in case they
	 * become valid after a policy reload. */
	if (ret == -EINVAL) {
		printk(KERN_WARNING "audit rule for LSM \'%s\' is "
		       "invalid\n", df->lsm_str);
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
static struct audit_entry *audit_dupe_rule(struct audit_krule *old,
					   struct audit_watch *watch)
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
	new->vers_ops = old->vers_ops;
	new->flags = old->flags;
	new->listnr = old->listnr;
	new->action = old->action;
	for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
		new->mask[i] = old->mask[i];
	new->prio = old->prio;
	new->buflen = old->buflen;
	new->inode_f = old->inode_f;
	new->watch = NULL;
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
		}
		if (err) {
			audit_free_rule(entry);
			return ERR_PTR(err);
		}
	}

	if (watch) {
		audit_get_watch(watch);
		new->watch = watch;
	}

	return entry;
}

/* Update inode info in audit rules based on filesystem event. */
static void audit_update_watch(struct audit_parent *parent,
			       const char *dname, dev_t dev,
			       unsigned long ino, unsigned invalidating)
{
	struct audit_watch *owatch, *nwatch, *nextw;
	struct audit_krule *r, *nextr;
	struct audit_entry *oentry, *nentry;

	mutex_lock(&audit_filter_mutex);
	list_for_each_entry_safe(owatch, nextw, &parent->watches, wlist) {
		if (audit_compare_dname_path(dname, owatch->path, NULL))
			continue;

		/* If the update involves invalidating rules, do the inode-based
		 * filtering now, so we don't omit records. */
		if (invalidating && current->audit_context)
			audit_filter_inodes(current, current->audit_context);

		nwatch = audit_dupe_watch(owatch);
		if (IS_ERR(nwatch)) {
			mutex_unlock(&audit_filter_mutex);
			audit_panic("error updating watch, skipping");
			return;
		}
		nwatch->dev = dev;
		nwatch->ino = ino;

		list_for_each_entry_safe(r, nextr, &owatch->rules, rlist) {

			oentry = container_of(r, struct audit_entry, rule);
			list_del(&oentry->rule.rlist);
			list_del_rcu(&oentry->list);

			nentry = audit_dupe_rule(&oentry->rule, nwatch);
			if (IS_ERR(nentry)) {
				list_del(&oentry->rule.list);
				audit_panic("error updating watch, removing");
			} else {
				int h = audit_hash_ino((u32)ino);
				list_add(&nentry->rule.rlist, &nwatch->rules);
				list_add_rcu(&nentry->list, &audit_inode_hash[h]);
				list_replace(&oentry->rule.list,
					     &nentry->rule.list);
			}

			call_rcu(&oentry->rcu, audit_free_rule_rcu);
		}

		if (audit_enabled) {
			struct audit_buffer *ab;
			ab = audit_log_start(NULL, GFP_KERNEL,
				AUDIT_CONFIG_CHANGE);
			audit_log_format(ab, "auid=%u ses=%u",
				audit_get_loginuid(current),
				audit_get_sessionid(current));
			audit_log_format(ab,
				" op=updated rules specifying path=");
			audit_log_untrustedstring(ab, owatch->path);
			audit_log_format(ab, " with dev=%u ino=%lu\n",
				 dev, ino);
			audit_log_format(ab, " list=%d res=1", r->listnr);
			audit_log_end(ab);
		}
		audit_remove_watch(owatch);
		goto add_watch_to_parent; /* event applies to a single watch */
	}
	mutex_unlock(&audit_filter_mutex);
	return;

add_watch_to_parent:
	list_add(&nwatch->wlist, &parent->watches);
	mutex_unlock(&audit_filter_mutex);
	return;
}

/* Remove all watches & rules associated with a parent that is going away. */
static void audit_remove_parent_watches(struct audit_parent *parent)
{
	struct audit_watch *w, *nextw;
	struct audit_krule *r, *nextr;
	struct audit_entry *e;

	mutex_lock(&audit_filter_mutex);
	parent->flags |= AUDIT_PARENT_INVALID;
	list_for_each_entry_safe(w, nextw, &parent->watches, wlist) {
		list_for_each_entry_safe(r, nextr, &w->rules, rlist) {
			e = container_of(r, struct audit_entry, rule);
			if (audit_enabled) {
				struct audit_buffer *ab;
				ab = audit_log_start(NULL, GFP_KERNEL,
					AUDIT_CONFIG_CHANGE);
				audit_log_format(ab, "auid=%u ses=%u",
					audit_get_loginuid(current),
					audit_get_sessionid(current));
				audit_log_format(ab, " op=remove rule path=");
				audit_log_untrustedstring(ab, w->path);
				if (r->filterkey) {
					audit_log_format(ab, " key=");
					audit_log_untrustedstring(ab,
							r->filterkey);
				} else
					audit_log_format(ab, " key=(null)");
				audit_log_format(ab, " list=%d res=1",
					r->listnr);
				audit_log_end(ab);
			}
			list_del(&r->rlist);
			list_del(&r->list);
			list_del_rcu(&e->list);
			call_rcu(&e->rcu, audit_free_rule_rcu);
		}
		audit_remove_watch(w);
	}
	mutex_unlock(&audit_filter_mutex);
}

/* Unregister inotify watches for parents on in_list.
 * Generates an IN_IGNORED event. */
static void audit_inotify_unregister(struct list_head *in_list)
{
	struct audit_parent *p, *n;

	list_for_each_entry_safe(p, n, in_list, ilist) {
		list_del(&p->ilist);
		inotify_rm_watch(audit_ih, &p->wdata);
		/* the unpin matching the pin in audit_do_del_rule() */
		unpin_inotify_watch(&p->wdata);
	}
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

/* Get path information necessary for adding watches. */
static int audit_get_nd(char *path, struct nameidata **ndp,
			struct nameidata **ndw)
{
	struct nameidata *ndparent, *ndwatch;
	int err;

	ndparent = kmalloc(sizeof(*ndparent), GFP_KERNEL);
	if (unlikely(!ndparent))
		return -ENOMEM;

	ndwatch = kmalloc(sizeof(*ndwatch), GFP_KERNEL);
	if (unlikely(!ndwatch)) {
		kfree(ndparent);
		return -ENOMEM;
	}

	err = path_lookup(path, LOOKUP_PARENT, ndparent);
	if (err) {
		kfree(ndparent);
		kfree(ndwatch);
		return err;
	}

	err = path_lookup(path, 0, ndwatch);
	if (err) {
		kfree(ndwatch);
		ndwatch = NULL;
	}

	*ndp = ndparent;
	*ndw = ndwatch;

	return 0;
}

/* Release resources used for watch path information. */
static void audit_put_nd(struct nameidata *ndp, struct nameidata *ndw)
{
	if (ndp) {
		path_put(&ndp->path);
		kfree(ndp);
	}
	if (ndw) {
		path_put(&ndw->path);
		kfree(ndw);
	}
}

/* Associate the given rule with an existing parent inotify_watch.
 * Caller must hold audit_filter_mutex. */
static void audit_add_to_parent(struct audit_krule *krule,
				struct audit_parent *parent)
{
	struct audit_watch *w, *watch = krule->watch;
	int watch_found = 0;

	list_for_each_entry(w, &parent->watches, wlist) {
		if (strcmp(watch->path, w->path))
			continue;

		watch_found = 1;

		/* put krule's and initial refs to temporary watch */
		audit_put_watch(watch);
		audit_put_watch(watch);

		audit_get_watch(w);
		krule->watch = watch = w;
		break;
	}

	if (!watch_found) {
		get_inotify_watch(&parent->wdata);
		watch->parent = parent;

		list_add(&watch->wlist, &parent->watches);
	}
	list_add(&krule->rlist, &watch->rules);
}

/* Find a matching watch entry, or add this one.
 * Caller must hold audit_filter_mutex. */
static int audit_add_watch(struct audit_krule *krule, struct nameidata *ndp,
			   struct nameidata *ndw)
{
	struct audit_watch *watch = krule->watch;
	struct inotify_watch *i_watch;
	struct audit_parent *parent;
	int ret = 0;

	/* update watch filter fields */
	if (ndw) {
		watch->dev = ndw->path.dentry->d_inode->i_sb->s_dev;
		watch->ino = ndw->path.dentry->d_inode->i_ino;
	}

	/* The audit_filter_mutex must not be held during inotify calls because
	 * we hold it during inotify event callback processing.  If an existing
	 * inotify watch is found, inotify_find_watch() grabs a reference before
	 * returning.
	 */
	mutex_unlock(&audit_filter_mutex);

	if (inotify_find_watch(audit_ih, ndp->path.dentry->d_inode,
			       &i_watch) < 0) {
		parent = audit_init_parent(ndp);
		if (IS_ERR(parent)) {
			/* caller expects mutex locked */
			mutex_lock(&audit_filter_mutex);
			return PTR_ERR(parent);
		}
	} else
		parent = container_of(i_watch, struct audit_parent, wdata);

	mutex_lock(&audit_filter_mutex);

	/* parent was moved before we took audit_filter_mutex */
	if (parent->flags & AUDIT_PARENT_INVALID)
		ret = -ENOENT;
	else
		audit_add_to_parent(krule, parent);

	/* match get in audit_init_parent or inotify_find_watch */
	put_inotify_watch(&parent->wdata);
	return ret;
}

static u64 prio_low = ~0ULL/2;
static u64 prio_high = ~0ULL/2 - 1;

/* Add rule to given filterlist if not a duplicate. */
static inline int audit_add_rule(struct audit_entry *entry)
{
	struct audit_entry *e;
	struct audit_watch *watch = entry->rule.watch;
	struct audit_tree *tree = entry->rule.tree;
	struct nameidata *ndp = NULL, *ndw = NULL;
	struct list_head *list;
	int h, err;
#ifdef CONFIG_AUDITSYSCALL
	int dont_count = 0;

	/* If either of these, don't count towards total */
	if (entry->rule.listnr == AUDIT_FILTER_USER ||
		entry->rule.listnr == AUDIT_FILTER_TYPE)
		dont_count = 1;
#endif

	mutex_lock(&audit_filter_mutex);
	e = audit_find_rule(entry, &list);
	mutex_unlock(&audit_filter_mutex);
	if (e) {
		err = -EEXIST;
		/* normally audit_add_tree_rule() will free it on failure */
		if (tree)
			audit_put_tree(tree);
		goto error;
	}

	/* Avoid calling path_lookup under audit_filter_mutex. */
	if (watch) {
		err = audit_get_nd(watch->path, &ndp, &ndw);
		if (err)
			goto error;
	}

	mutex_lock(&audit_filter_mutex);
	if (watch) {
		/* audit_filter_mutex is dropped and re-taken during this call */
		err = audit_add_watch(&entry->rule, ndp, ndw);
		if (err) {
			mutex_unlock(&audit_filter_mutex);
			goto error;
		}
		h = audit_hash_ino((u32)watch->ino);
		list = &audit_inode_hash[h];
	}
	if (tree) {
		err = audit_add_tree_rule(&entry->rule);
		if (err) {
			mutex_unlock(&audit_filter_mutex);
			goto error;
		}
	}

	entry->rule.prio = ~0ULL;
	if (entry->rule.listnr == AUDIT_FILTER_EXIT) {
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

	audit_put_nd(ndp, ndw);		/* NULL args OK */
 	return 0;

error:
	audit_put_nd(ndp, ndw);		/* NULL args OK */
	if (watch)
		audit_put_watch(watch); /* tmp watch, matches initial get */
	return err;
}

/* Remove an existing rule from filterlist. */
static inline int audit_del_rule(struct audit_entry *entry)
{
	struct audit_entry  *e;
	struct audit_watch *watch, *tmp_watch = entry->rule.watch;
	struct audit_tree *tree = entry->rule.tree;
	struct list_head *list;
	LIST_HEAD(inotify_list);
	int ret = 0;
#ifdef CONFIG_AUDITSYSCALL
	int dont_count = 0;

	/* If either of these, don't count towards total */
	if (entry->rule.listnr == AUDIT_FILTER_USER ||
		entry->rule.listnr == AUDIT_FILTER_TYPE)
		dont_count = 1;
#endif

	mutex_lock(&audit_filter_mutex);
	e = audit_find_rule(entry, &list);
	if (!e) {
		mutex_unlock(&audit_filter_mutex);
		ret = -ENOENT;
		goto out;
	}

	watch = e->rule.watch;
	if (watch) {
		struct audit_parent *parent = watch->parent;

		list_del(&e->rule.rlist);

		if (list_empty(&watch->rules)) {
			audit_remove_watch(watch);

			if (list_empty(&parent->watches)) {
				/* Put parent on the inotify un-registration
				 * list.  Grab a reference before releasing
				 * audit_filter_mutex, to be released in
				 * audit_inotify_unregister().
				 * If filesystem is going away, just leave
				 * the sucker alone, eviction will take
				 * care of it.
				 */
				if (pin_inotify_watch(&parent->wdata))
					list_add(&parent->ilist, &inotify_list);
			}
		}
	}

	if (e->rule.tree)
		audit_remove_tree_rule(&e->rule);

	list_del_rcu(&e->list);
	list_del(&e->rule.list);
	call_rcu(&e->rcu, audit_free_rule_rcu);

#ifdef CONFIG_AUDITSYSCALL
	if (!dont_count)
		audit_n_rules--;

	if (!audit_match_signal(entry))
		audit_signals--;
#endif
	mutex_unlock(&audit_filter_mutex);

	if (!list_empty(&inotify_list))
		audit_inotify_unregister(&inotify_list);

out:
	if (tmp_watch)
		audit_put_watch(tmp_watch); /* match initial get */
	if (tree)
		audit_put_tree(tree);	/* that's the temporary one */

	return ret;
}

/* List rules using struct audit_rule.  Exists for backward
 * compatibility with userspace. */
static void audit_list(int pid, int seq, struct sk_buff_head *q)
{
	struct sk_buff *skb;
	struct audit_krule *r;
	int i;

	/* This is a blocking read, so use audit_filter_mutex instead of rcu
	 * iterator to sync with list writers. */
	for (i=0; i<AUDIT_NR_FILTERS; i++) {
		list_for_each_entry(r, &audit_rules_list[i], list) {
			struct audit_rule *rule;

			rule = audit_krule_to_rule(r);
			if (unlikely(!rule))
				break;
			skb = audit_make_reply(pid, seq, AUDIT_LIST, 0, 1,
					 rule, sizeof(*rule));
			if (skb)
				skb_queue_tail(q, skb);
			kfree(rule);
		}
	}
	skb = audit_make_reply(pid, seq, AUDIT_LIST, 1, 1, NULL, 0);
	if (skb)
		skb_queue_tail(q, skb);
}

/* List rules using struct audit_rule_data. */
static void audit_list_rules(int pid, int seq, struct sk_buff_head *q)
{
	struct sk_buff *skb;
	struct audit_krule *r;
	int i;

	/* This is a blocking read, so use audit_filter_mutex instead of rcu
	 * iterator to sync with list writers. */
	for (i=0; i<AUDIT_NR_FILTERS; i++) {
		list_for_each_entry(r, &audit_rules_list[i], list) {
			struct audit_rule_data *data;

			data = audit_krule_to_data(r);
			if (unlikely(!data))
				break;
			skb = audit_make_reply(pid, seq, AUDIT_LIST_RULES, 0, 1,
					 data, sizeof(*data) + data->buflen);
			if (skb)
				skb_queue_tail(q, skb);
			kfree(data);
		}
	}
	skb = audit_make_reply(pid, seq, AUDIT_LIST_RULES, 1, 1, NULL, 0);
	if (skb)
		skb_queue_tail(q, skb);
}

/* Log rule additions and removals */
static void audit_log_rule_change(uid_t loginuid, u32 sessionid, u32 sid,
				  char *action, struct audit_krule *rule,
				  int res)
{
	struct audit_buffer *ab;

	if (!audit_enabled)
		return;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE);
	if (!ab)
		return;
	audit_log_format(ab, "auid=%u ses=%u", loginuid, sessionid);
	if (sid) {
		char *ctx = NULL;
		u32 len;
		if (security_secid_to_secctx(sid, &ctx, &len))
			audit_log_format(ab, " ssid=%u", sid);
		else {
			audit_log_format(ab, " subj=%s", ctx);
			security_release_secctx(ctx, len);
		}
	}
	audit_log_format(ab, " op=%s rule key=", action);
	if (rule->filterkey)
		audit_log_untrustedstring(ab, rule->filterkey);
	else
		audit_log_format(ab, "(null)");
	audit_log_format(ab, " list=%d res=%d", rule->listnr, res);
	audit_log_end(ab);
}

/**
 * audit_receive_filter - apply all rules to the specified message type
 * @type: audit message type
 * @pid: target pid for netlink audit messages
 * @uid: target uid for netlink audit messages
 * @seq: netlink audit message sequence (serial) number
 * @data: payload data
 * @datasz: size of payload data
 * @loginuid: loginuid of sender
 * @sessionid: sessionid for netlink audit message
 * @sid: SE Linux Security ID of sender
 */
int audit_receive_filter(int type, int pid, int uid, int seq, void *data,
			 size_t datasz, uid_t loginuid, u32 sessionid, u32 sid)
{
	struct task_struct *tsk;
	struct audit_netlink_list *dest;
	int err = 0;
	struct audit_entry *entry;

	switch (type) {
	case AUDIT_LIST:
	case AUDIT_LIST_RULES:
		/* We can't just spew out the rules here because we might fill
		 * the available socket buffer space and deadlock waiting for
		 * auditctl to read from it... which isn't ever going to
		 * happen if we're actually running in the context of auditctl
		 * trying to _send_ the stuff */

		dest = kmalloc(sizeof(struct audit_netlink_list), GFP_KERNEL);
		if (!dest)
			return -ENOMEM;
		dest->pid = pid;
		skb_queue_head_init(&dest->q);

		mutex_lock(&audit_filter_mutex);
		if (type == AUDIT_LIST)
			audit_list(pid, seq, &dest->q);
		else
			audit_list_rules(pid, seq, &dest->q);
		mutex_unlock(&audit_filter_mutex);

		tsk = kthread_run(audit_send_list, dest, "audit_send_list");
		if (IS_ERR(tsk)) {
			skb_queue_purge(&dest->q);
			kfree(dest);
			err = PTR_ERR(tsk);
		}
		break;
	case AUDIT_ADD:
	case AUDIT_ADD_RULE:
		if (type == AUDIT_ADD)
			entry = audit_rule_to_entry(data);
		else
			entry = audit_data_to_entry(data, datasz);
		if (IS_ERR(entry))
			return PTR_ERR(entry);

		err = audit_add_rule(entry);
		audit_log_rule_change(loginuid, sessionid, sid, "add",
				      &entry->rule, !err);

		if (err)
			audit_free_rule(entry);
		break;
	case AUDIT_DEL:
	case AUDIT_DEL_RULE:
		if (type == AUDIT_DEL)
			entry = audit_rule_to_entry(data);
		else
			entry = audit_data_to_entry(data, datasz);
		if (IS_ERR(entry))
			return PTR_ERR(entry);

		err = audit_del_rule(entry);
		audit_log_rule_change(loginuid, sessionid, sid, "remove",
				      &entry->rule, !err);

		audit_free_rule(entry);
		break;
	default:
		return -EINVAL;
	}

	return err;
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
		BUG();
		return 0;
	}
}

/* Compare given dentry name with last component in given path,
 * return of 0 indicates a match. */
int audit_compare_dname_path(const char *dname, const char *path,
			     int *dirlen)
{
	int dlen, plen;
	const char *p;

	if (!dname || !path)
		return 1;

	dlen = strlen(dname);
	plen = strlen(path);
	if (plen < dlen)
		return 1;

	/* disregard trailing slashes */
	p = path + plen - 1;
	while ((*p == '/') && (p > path))
		p--;

	/* find last path component */
	p = p - dlen + 1;
	if (p < path)
		return 1;
	else if (p > path) {
		if (*--p != '/')
			return 1;
		else
			p++;
	}

	/* return length of path's directory component */
	if (dirlen)
		*dirlen = p - path;
	return strncmp(p, dname, dlen);
}

static int audit_filter_user_rules(struct netlink_skb_parms *cb,
				   struct audit_krule *rule,
				   enum audit_state *state)
{
	int i;

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &rule->fields[i];
		int result = 0;

		switch (f->type) {
		case AUDIT_PID:
			result = audit_comparator(cb->creds.pid, f->op, f->val);
			break;
		case AUDIT_UID:
			result = audit_comparator(cb->creds.uid, f->op, f->val);
			break;
		case AUDIT_GID:
			result = audit_comparator(cb->creds.gid, f->op, f->val);
			break;
		case AUDIT_LOGINUID:
			result = audit_comparator(cb->loginuid, f->op, f->val);
			break;
		}

		if (!result)
			return 0;
	}
	switch (rule->action) {
	case AUDIT_NEVER:    *state = AUDIT_DISABLED;	    break;
	case AUDIT_ALWAYS:   *state = AUDIT_RECORD_CONTEXT; break;
	}
	return 1;
}

int audit_filter_user(struct netlink_skb_parms *cb)
{
	enum audit_state state = AUDIT_DISABLED;
	struct audit_entry *e;
	int ret = 1;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &audit_filter_list[AUDIT_FILTER_USER], list) {
		if (audit_filter_user_rules(cb, &e->rule, &state)) {
			if (state == AUDIT_DISABLED)
				ret = 0;
			break;
		}
	}
	rcu_read_unlock();

	return ret; /* Audit by default */
}

int audit_filter_type(int type)
{
	struct audit_entry *e;
	int result = 0;

	rcu_read_lock();
	if (list_empty(&audit_filter_list[AUDIT_FILTER_TYPE]))
		goto unlock_and_return;

	list_for_each_entry_rcu(e, &audit_filter_list[AUDIT_FILTER_TYPE],
				list) {
		int i;
		for (i = 0; i < e->rule.field_count; i++) {
			struct audit_field *f = &e->rule.fields[i];
			if (f->type == AUDIT_MSGTYPE) {
				result = audit_comparator(type, f->op, f->val);
				if (!result)
					break;
			}
		}
		if (result)
			goto unlock_and_return;
	}
unlock_and_return:
	rcu_read_unlock();
	return result;
}

static int update_lsm_rule(struct audit_krule *r)
{
	struct audit_entry *entry = container_of(r, struct audit_entry, rule);
	struct audit_entry *nentry;
	struct audit_watch *watch;
	struct audit_tree *tree;
	int err = 0;

	if (!security_audit_rule_known(r))
		return 0;

	watch = r->watch;
	tree = r->tree;
	nentry = audit_dupe_rule(r, watch);
	if (IS_ERR(nentry)) {
		/* save the first error encountered for the
		 * return value */
		err = PTR_ERR(nentry);
		audit_panic("error updating LSM filters");
		if (watch)
			list_del(&r->rlist);
		list_del_rcu(&entry->list);
		list_del(&r->list);
	} else {
		if (watch) {
			list_add(&nentry->rule.rlist, &watch->rules);
			list_del(&r->rlist);
		} else if (tree)
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

/* Update watch data in audit rules based on inotify events. */
void audit_handle_ievent(struct inotify_watch *i_watch, u32 wd, u32 mask,
			 u32 cookie, const char *dname, struct inode *inode)
{
	struct audit_parent *parent;

	parent = container_of(i_watch, struct audit_parent, wdata);

	if (mask & (IN_CREATE|IN_MOVED_TO) && inode)
		audit_update_watch(parent, dname, inode->i_sb->s_dev,
				   inode->i_ino, 0);
	else if (mask & (IN_DELETE|IN_MOVED_FROM))
		audit_update_watch(parent, dname, (dev_t)-1, (unsigned long)-1, 1);
	/* inotify automatically removes the watch and sends IN_IGNORED */
	else if (mask & (IN_DELETE_SELF|IN_UNMOUNT))
		audit_remove_parent_watches(parent);
	/* inotify does not remove the watch, so remove it manually */
	else if(mask & IN_MOVE_SELF) {
		audit_remove_parent_watches(parent);
		inotify_remove_watch_locked(audit_ih, i_watch);
	} else if (mask & IN_IGNORED)
		put_inotify_watch(i_watch);
}
