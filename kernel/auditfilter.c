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

/* Copy rule from user-space to kernel-space.  Called from 
 * audit_add_rule during AUDIT_ADD. */
static inline int audit_copy_rule(struct audit_rule *d, struct audit_rule *s)
{
	int i;

	if (s->action != AUDIT_NEVER
	    && s->action != AUDIT_POSSIBLE
	    && s->action != AUDIT_ALWAYS)
		return -1;
	if (s->field_count < 0 || s->field_count > AUDIT_MAX_FIELDS)
		return -1;
	if ((s->flags & ~AUDIT_FILTER_PREPEND) >= AUDIT_NR_FILTERS)
		return -1;

	d->flags	= s->flags;
	d->action	= s->action;
	d->field_count	= s->field_count;
	for (i = 0; i < d->field_count; i++) {
		d->fields[i] = s->fields[i];
		d->values[i] = s->values[i];
	}
	for (i = 0; i < AUDIT_BITMASK_SIZE; i++) d->mask[i] = s->mask[i];
	return 0;
}

/* Check to see if two rules are identical.  It is called from
 * audit_add_rule during AUDIT_ADD and 
 * audit_del_rule during AUDIT_DEL. */
static inline int audit_compare_rule(struct audit_rule *a, struct audit_rule *b)
{
	int i;

	if (a->flags != b->flags)
		return 1;

	if (a->action != b->action)
		return 1;

	if (a->field_count != b->field_count)
		return 1;

	for (i = 0; i < a->field_count; i++) {
		if (a->fields[i] != b->fields[i]
		    || a->values[i] != b->values[i])
			return 1;
	}

	for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
		if (a->mask[i] != b->mask[i])
			return 1;

	return 0;
}

/* Note that audit_add_rule and audit_del_rule are called via
 * audit_receive() in audit.c, and are protected by
 * audit_netlink_sem. */
static inline int audit_add_rule(struct audit_rule *rule,
				  struct list_head *list)
{
	struct audit_entry  *entry;
	int i;

	/* Do not use the _rcu iterator here, since this is the only
	 * addition routine. */
	list_for_each_entry(entry, list, list) {
		if (!audit_compare_rule(rule, &entry->rule)) {
			return -EEXIST;
		}
	}

	for (i = 0; i < rule->field_count; i++) {
		if (rule->fields[i] & AUDIT_UNUSED_BITS)
			return -EINVAL;
		if ( rule->fields[i] & AUDIT_NEGATE )
			rule->fields[i] |= AUDIT_NOT_EQUAL;
		else if ( (rule->fields[i] & AUDIT_OPERATORS) == 0 )
			rule->fields[i] |= AUDIT_EQUAL;
		rule->fields[i] &= (~AUDIT_NEGATE);
	}

	if (!(entry = kmalloc(sizeof(*entry), GFP_KERNEL)))
		return -ENOMEM;
	if (audit_copy_rule(&entry->rule, rule)) {
		kfree(entry);
		return -EINVAL;
	}

	if (entry->rule.flags & AUDIT_FILTER_PREPEND) {
		entry->rule.flags &= ~AUDIT_FILTER_PREPEND;
		list_add_rcu(&entry->list, list);
	} else {
		list_add_tail_rcu(&entry->list, list);
	}

	return 0;
}

static inline void audit_free_rule(struct rcu_head *head)
{
	struct audit_entry *e = container_of(head, struct audit_entry, rcu);
	kfree(e);
}

/* Note that audit_add_rule and audit_del_rule are called via
 * audit_receive() in audit.c, and are protected by
 * audit_netlink_sem. */
static inline int audit_del_rule(struct audit_rule *rule,
				 struct list_head *list)
{
	struct audit_entry  *e;

	/* Do not use the _rcu iterator here, since this is the only
	 * deletion routine. */
	list_for_each_entry(e, list, list) {
		if (!audit_compare_rule(rule, &e->rule)) {
			list_del_rcu(&e->list);
			call_rcu(&e->rcu, audit_free_rule);
			return 0;
		}
	}
	return -ENOENT;		/* No matching rule */
}

static int audit_list_rules(void *_dest)
{
	int pid, seq;
	int *dest = _dest;
	struct audit_entry *entry;
	int i;

	pid = dest[0];
	seq = dest[1];
	kfree(dest);

	down(&audit_netlink_sem);

	/* The *_rcu iterators not needed here because we are
	   always called with audit_netlink_sem held. */
	for (i=0; i<AUDIT_NR_FILTERS; i++) {
		list_for_each_entry(entry, &audit_filter_list[i], list)
			audit_send_reply(pid, seq, AUDIT_LIST, 0, 1,
					 &entry->rule, sizeof(entry->rule));
	}
	audit_send_reply(pid, seq, AUDIT_LIST, 1, 1, NULL, 0);
	
	up(&audit_netlink_sem);
	return 0;
}

/**
 * audit_receive_filter - apply all rules to the specified message type
 * @type: audit message type
 * @pid: target pid for netlink audit messages
 * @uid: target uid for netlink audit messages
 * @seq: netlink audit message sequence (serial) number
 * @data: payload data
 * @loginuid: loginuid of sender
 */
int audit_receive_filter(int type, int pid, int uid, int seq, void *data,
							uid_t loginuid)
{
	struct task_struct *tsk;
	int *dest;
	int		   err = 0;
	unsigned listnr;

	switch (type) {
	case AUDIT_LIST:
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

		tsk = kthread_run(audit_list_rules, dest, "audit_list_rules");
		if (IS_ERR(tsk)) {
			kfree(dest);
			err = PTR_ERR(tsk);
		}
		break;
	case AUDIT_ADD:
		listnr = ((struct audit_rule *)data)->flags & ~AUDIT_FILTER_PREPEND;
		switch(listnr) {
		default:
			return -EINVAL;

		case AUDIT_FILTER_USER:
		case AUDIT_FILTER_TYPE:
#ifdef CONFIG_AUDITSYSCALL
		case AUDIT_FILTER_ENTRY:
		case AUDIT_FILTER_EXIT:
		case AUDIT_FILTER_TASK:
#endif
			;
		}
		err = audit_add_rule(data, &audit_filter_list[listnr]);
		if (!err)
			audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				  "auid=%u added an audit rule\n", loginuid);
		break;
	case AUDIT_DEL:
		listnr =((struct audit_rule *)data)->flags & ~AUDIT_FILTER_PREPEND;
		if (listnr >= AUDIT_NR_FILTERS)
			return -EINVAL;

		err = audit_del_rule(data, &audit_filter_list[listnr]);
		if (!err)
			audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				  "auid=%u removed an audit rule\n", loginuid);
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
	default:
		return -EINVAL;
	}
}



static int audit_filter_user_rules(struct netlink_skb_parms *cb,
				   struct audit_rule *rule,
				   enum audit_state *state)
{
	int i;

	for (i = 0; i < rule->field_count; i++) {
		u32 field  = rule->fields[i] & ~AUDIT_OPERATORS;
		u32 op  = rule->fields[i] & AUDIT_OPERATORS;
		u32 value  = rule->values[i];
		int result = 0;

		switch (field) {
		case AUDIT_PID:
			result = audit_comparator(cb->creds.pid, op, value);
			break;
		case AUDIT_UID:
			result = audit_comparator(cb->creds.uid, op, value);
			break;
		case AUDIT_GID:
			result = audit_comparator(cb->creds.gid, op, value);
			break;
		case AUDIT_LOGINUID:
			result = audit_comparator(cb->loginuid, op, value);
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
		struct audit_rule *rule = &e->rule;
		int i;
		for (i = 0; i < rule->field_count; i++) {
			u32 field  = rule->fields[i] & ~AUDIT_OPERATORS;
			u32 op  = rule->fields[i] & AUDIT_OPERATORS;
			u32 value  = rule->values[i];
			if ( field == AUDIT_MSGTYPE ) {
				result = audit_comparator(type, op, value); 
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


