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
#include <linux/netlink.h>
#include <linux/selinux.h>
#include "audit.h"

/* There are three lists of rules -- one to search at task creation
 * time, one to search at syscall entry time, and another to search at
 * syscall exit time. */
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

static inline void audit_free_rule(struct audit_entry *e)
{
	int i;
	if (e->rule.fields)
		for (i = 0; i < e->rule.field_count; i++) {
			struct audit_field *f = &e->rule.fields[i];
			kfree(f->se_str);
			selinux_audit_rule_free(f->se_rule);
		}
	kfree(e->rule.fields);
	kfree(e);
}

static inline void audit_free_rule_rcu(struct rcu_head *head)
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
static char *audit_unpack_string(void **bufp, size_t *remain, size_t len)
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
	if (rule->action != AUDIT_NEVER && rule->action != AUDIT_POSSIBLE &&
	    rule->action != AUDIT_ALWAYS)
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

	return entry;

exit_err:
	return ERR_PTR(err);
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

		f->op = rule->fields[i] & (AUDIT_NEGATE|AUDIT_OPERATORS);
		f->type = rule->fields[i] & ~(AUDIT_NEGATE|AUDIT_OPERATORS);
		f->val = rule->values[i];

		if (f->type & AUDIT_UNUSED_BITS ||
		    f->type == AUDIT_SE_USER ||
		    f->type == AUDIT_SE_ROLE ||
		    f->type == AUDIT_SE_TYPE ||
		    f->type == AUDIT_SE_SEN ||
		    f->type == AUDIT_SE_CLR) {
			err = -EINVAL;
			goto exit_free;
		}

		entry->rule.vers_ops = (f->op & AUDIT_OPERATORS) ? 2 : 1;

		/* Support for legacy operators where
		 * AUDIT_NEGATE bit signifies != and otherwise assumes == */
		if (f->op & AUDIT_NEGATE)
			f->op = AUDIT_NOT_EQUAL;
		else if (!f->op)
			f->op = AUDIT_EQUAL;
		else if (f->op == AUDIT_OPERATORS) {
			err = -EINVAL;
			goto exit_free;
		}
	}

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
		if (!(data->fieldflags[i] & AUDIT_OPERATORS) ||
		    data->fieldflags[i] & ~AUDIT_OPERATORS)
			goto exit_free;

		f->op = data->fieldflags[i] & AUDIT_OPERATORS;
		f->type = data->fields[i];
		f->val = data->values[i];
		f->se_str = NULL;
		f->se_rule = NULL;
		switch(f->type) {
		case AUDIT_SE_USER:
		case AUDIT_SE_ROLE:
		case AUDIT_SE_TYPE:
		case AUDIT_SE_SEN:
		case AUDIT_SE_CLR:
			str = audit_unpack_string(&bufp, &remain, f->val);
			if (IS_ERR(str))
				goto exit_free;
			entry->rule.buflen += f->val;

			err = selinux_audit_rule_init(f->type, f->op, str,
						      &f->se_rule);
			/* Keep currently invalid fields around in case they
			 * become valid after a policy reload. */
			if (err == -EINVAL) {
				printk(KERN_WARNING "audit rule for selinux "
				       "\'%s\' is invalid\n",  str);
				err = 0;
			}
			if (err) {
				kfree(str);
				goto exit_free;
			} else
				f->se_str = str;
			break;
		}
	}

exit_nofree:
	return entry;

exit_free:
	audit_free_rule(entry);
	return ERR_PTR(err);
}

/* Pack a filter field's string representation into data block. */
static inline size_t audit_pack_string(void **bufp, char *str)
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

	rule = kmalloc(sizeof(*rule), GFP_KERNEL);
	if (unlikely(!rule))
		return ERR_PTR(-ENOMEM);
	memset(rule, 0, sizeof(*rule));

	rule->flags = krule->flags | krule->listnr;
	rule->action = krule->action;
	rule->field_count = krule->field_count;
	for (i = 0; i < rule->field_count; i++) {
		rule->values[i] = krule->fields[i].val;
		rule->fields[i] = krule->fields[i].type;

		if (krule->vers_ops == 1) {
			if (krule->fields[i].op & AUDIT_NOT_EQUAL)
				rule->fields[i] |= AUDIT_NEGATE;
		} else {
			rule->fields[i] |= krule->fields[i].op;
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
		return ERR_PTR(-ENOMEM);
	memset(data, 0, sizeof(*data));

	data->flags = krule->flags | krule->listnr;
	data->action = krule->action;
	data->field_count = krule->field_count;
	bufp = data->buf;
	for (i = 0; i < data->field_count; i++) {
		struct audit_field *f = &krule->fields[i];

		data->fields[i] = f->type;
		data->fieldflags[i] = f->op;
		switch(f->type) {
		case AUDIT_SE_USER:
		case AUDIT_SE_ROLE:
		case AUDIT_SE_TYPE:
		case AUDIT_SE_SEN:
		case AUDIT_SE_CLR:
			data->buflen += data->values[i] =
				audit_pack_string(&bufp, f->se_str);
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
		case AUDIT_SE_USER:
		case AUDIT_SE_ROLE:
		case AUDIT_SE_TYPE:
		case AUDIT_SE_SEN:
		case AUDIT_SE_CLR:
			if (strcmp(a->fields[i].se_str, b->fields[i].se_str))
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

/* Duplicate selinux field information.  The se_rule is opaque, so must be
 * re-initialized. */
static inline int audit_dupe_selinux_field(struct audit_field *df,
					   struct audit_field *sf)
{
	int ret = 0;
	char *se_str;

	/* our own copy of se_str */
	se_str = kstrdup(sf->se_str, GFP_KERNEL);
	if (unlikely(IS_ERR(se_str)))
	    return -ENOMEM;
	df->se_str = se_str;

	/* our own (refreshed) copy of se_rule */
	ret = selinux_audit_rule_init(df->type, df->op, df->se_str,
				      &df->se_rule);
	/* Keep currently invalid fields around in case they
	 * become valid after a policy reload. */
	if (ret == -EINVAL) {
		printk(KERN_WARNING "audit rule for selinux \'%s\' is "
		       "invalid\n", df->se_str);
		ret = 0;
	}

	return ret;
}

/* Duplicate an audit rule.  This will be a deep copy with the exception
 * of the watch - that pointer is carried over.  The selinux specific fields
 * will be updated in the copy.  The point is to be able to replace the old
 * rule with the new rule in the filterlist, then free the old rule. */
static struct audit_entry *audit_dupe_rule(struct audit_krule *old)
{
	u32 fcount = old->field_count;
	struct audit_entry *entry;
	struct audit_krule *new;
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
	new->buflen = old->buflen;
	new->field_count = old->field_count;
	memcpy(new->fields, old->fields, sizeof(struct audit_field) * fcount);

	/* deep copy this information, updating the se_rule fields, because
	 * the originals will all be freed when the old rule is freed. */
	for (i = 0; i < fcount; i++) {
		switch (new->fields[i].type) {
		case AUDIT_SE_USER:
		case AUDIT_SE_ROLE:
		case AUDIT_SE_TYPE:
		case AUDIT_SE_SEN:
		case AUDIT_SE_CLR:
			err = audit_dupe_selinux_field(&new->fields[i],
						       &old->fields[i]);
		}
		if (err) {
			audit_free_rule(entry);
			return ERR_PTR(err);
		}
	}

	return entry;
}

/* Add rule to given filterlist if not a duplicate.  Protected by
 * audit_netlink_mutex. */
static inline int audit_add_rule(struct audit_entry *entry,
				  struct list_head *list)
{
	struct audit_entry *e;

	/* Do not use the _rcu iterator here, since this is the only
	 * addition routine. */
	list_for_each_entry(e, list, list) {
		if (!audit_compare_rule(&entry->rule, &e->rule))
			return -EEXIST;
	}

	if (entry->rule.flags & AUDIT_FILTER_PREPEND) {
		list_add_rcu(&entry->list, list);
	} else {
		list_add_tail_rcu(&entry->list, list);
	}

	return 0;
}

/* Remove an existing rule from filterlist.  Protected by
 * audit_netlink_mutex. */
static inline int audit_del_rule(struct audit_entry *entry,
				 struct list_head *list)
{
	struct audit_entry  *e;

	/* Do not use the _rcu iterator here, since this is the only
	 * deletion routine. */
	list_for_each_entry(e, list, list) {
		if (!audit_compare_rule(&entry->rule, &e->rule)) {
			list_del_rcu(&e->list);
			call_rcu(&e->rcu, audit_free_rule_rcu);
			return 0;
		}
	}
	return -ENOENT;		/* No matching rule */
}

/* List rules using struct audit_rule.  Exists for backward
 * compatibility with userspace. */
static int audit_list(void *_dest)
{
	int pid, seq;
	int *dest = _dest;
	struct audit_entry *entry;
	int i;

	pid = dest[0];
	seq = dest[1];
	kfree(dest);

	mutex_lock(&audit_netlink_mutex);

	/* The *_rcu iterators not needed here because we are
	   always called with audit_netlink_mutex held. */
	for (i=0; i<AUDIT_NR_FILTERS; i++) {
		list_for_each_entry(entry, &audit_filter_list[i], list) {
			struct audit_rule *rule;

			rule = audit_krule_to_rule(&entry->rule);
			if (unlikely(!rule))
				break;
			audit_send_reply(pid, seq, AUDIT_LIST, 0, 1,
					 rule, sizeof(*rule));
			kfree(rule);
		}
	}
	audit_send_reply(pid, seq, AUDIT_LIST, 1, 1, NULL, 0);
	
	mutex_unlock(&audit_netlink_mutex);
	return 0;
}

/* List rules using struct audit_rule_data. */
static int audit_list_rules(void *_dest)
{
	int pid, seq;
	int *dest = _dest;
	struct audit_entry *e;
	int i;

	pid = dest[0];
	seq = dest[1];
	kfree(dest);

	mutex_lock(&audit_netlink_mutex);

	/* The *_rcu iterators not needed here because we are
	   always called with audit_netlink_mutex held. */
	for (i=0; i<AUDIT_NR_FILTERS; i++) {
		list_for_each_entry(e, &audit_filter_list[i], list) {
			struct audit_rule_data *data;

			data = audit_krule_to_data(&e->rule);
			if (unlikely(!data))
				break;
			audit_send_reply(pid, seq, AUDIT_LIST_RULES, 0, 1,
					 data, sizeof(*data));
			kfree(data);
		}
	}
	audit_send_reply(pid, seq, AUDIT_LIST_RULES, 1, 1, NULL, 0);

	mutex_unlock(&audit_netlink_mutex);
	return 0;
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
 * @sid: SE Linux Security ID of sender
 */
int audit_receive_filter(int type, int pid, int uid, int seq, void *data,
			 size_t datasz, uid_t loginuid, u32 sid)
{
	struct task_struct *tsk;
	int *dest;
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
		 
		dest = kmalloc(2 * sizeof(int), GFP_KERNEL);
		if (!dest)
			return -ENOMEM;
		dest[0] = pid;
		dest[1] = seq;

		if (type == AUDIT_LIST)
			tsk = kthread_run(audit_list, dest, "audit_list");
		else
			tsk = kthread_run(audit_list_rules, dest,
					  "audit_list_rules");
		if (IS_ERR(tsk)) {
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

		err = audit_add_rule(entry,
				     &audit_filter_list[entry->rule.listnr]);
		if (sid) {
			char *ctx = NULL;
			u32 len;
			if (selinux_ctxid_to_string(sid, &ctx, &len)) {
				/* Maybe call audit_panic? */
				audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				 "auid=%u ssid=%u add rule to list=%d res=%d",
				 loginuid, sid, entry->rule.listnr, !err);
			} else
				audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				 "auid=%u subj=%s add rule to list=%d res=%d",
				 loginuid, ctx, entry->rule.listnr, !err);
			kfree(ctx);
		} else
			audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				"auid=%u add rule to list=%d res=%d",
				loginuid, entry->rule.listnr, !err);

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

		err = audit_del_rule(entry,
				     &audit_filter_list[entry->rule.listnr]);

		if (sid) {
			char *ctx = NULL;
			u32 len;
			if (selinux_ctxid_to_string(sid, &ctx, &len)) {
				/* Maybe call audit_panic? */
				audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
					"auid=%u ssid=%u remove rule from list=%d res=%d",
					 loginuid, sid, entry->rule.listnr, !err);
			} else
				audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
					"auid=%u subj=%s remove rule from list=%d res=%d",
					 loginuid, ctx, entry->rule.listnr, !err);
			kfree(ctx);
		} else
			audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				"auid=%u remove rule from list=%d res=%d",
				loginuid, entry->rule.listnr, !err);

		audit_free_rule(entry);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

int audit_comparator(const u32 left, const u32 op, const u32 right)
{
	switch (op) {
	case AUDIT_EQUAL:
		return (left == right);
	case AUDIT_NOT_EQUAL:
		return (left != right);
	case AUDIT_LESS_THAN:
		return (left < right);
	case AUDIT_LESS_THAN_OR_EQUAL:
		return (left <= right);
	case AUDIT_GREATER_THAN:
		return (left > right);
	case AUDIT_GREATER_THAN_OR_EQUAL:
		return (left >= right);
	}
	BUG();
	return 0;
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
	case AUDIT_POSSIBLE: *state = AUDIT_BUILD_CONTEXT;  break;
	case AUDIT_ALWAYS:   *state = AUDIT_RECORD_CONTEXT; break;
	}
	return 1;
}

int audit_filter_user(struct netlink_skb_parms *cb, int type)
{
	struct audit_entry *e;
	enum audit_state   state;
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

/* Check to see if the rule contains any selinux fields.  Returns 1 if there
   are selinux fields specified in the rule, 0 otherwise. */
static inline int audit_rule_has_selinux(struct audit_krule *rule)
{
	int i;

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &rule->fields[i];
		switch (f->type) {
		case AUDIT_SE_USER:
		case AUDIT_SE_ROLE:
		case AUDIT_SE_TYPE:
		case AUDIT_SE_SEN:
		case AUDIT_SE_CLR:
			return 1;
		}
	}

	return 0;
}

/* This function will re-initialize the se_rule field of all applicable rules.
 * It will traverse the filter lists serarching for rules that contain selinux
 * specific filter fields.  When such a rule is found, it is copied, the
 * selinux field is re-initialized, and the old rule is replaced with the
 * updated rule. */
int selinux_audit_rule_update(void)
{
	struct audit_entry *entry, *n, *nentry;
	int i, err = 0;

	/* audit_netlink_mutex synchronizes the writers */
	mutex_lock(&audit_netlink_mutex);

	for (i = 0; i < AUDIT_NR_FILTERS; i++) {
		list_for_each_entry_safe(entry, n, &audit_filter_list[i], list) {
			if (!audit_rule_has_selinux(&entry->rule))
				continue;

			nentry = audit_dupe_rule(&entry->rule);
			if (unlikely(IS_ERR(nentry))) {
				/* save the first error encountered for the
				 * return value */
				if (!err)
					err = PTR_ERR(nentry);
				audit_panic("error updating selinux filters");
				list_del_rcu(&entry->list);
			} else {
				list_replace_rcu(&entry->list, &nentry->list);
			}
			call_rcu(&entry->rcu, audit_free_rule_rcu);
		}
	}

	mutex_unlock(&audit_netlink_mutex);

	return err;
}
