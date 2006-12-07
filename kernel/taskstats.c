/*
 * taskstats.c - Export per-task statistics to userland
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *           (C) Balbir Singh,   IBM Corp. 2006
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
 */

#include <linux/kernel.h>
#include <linux/taskstats_kern.h>
#include <linux/tsacct_kern.h>
#include <linux/delayacct.h>
#include <linux/tsacct_kern.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <net/genetlink.h>
#include <asm/atomic.h>

/*
 * Maximum length of a cpumask that can be specified in
 * the TASKSTATS_CMD_ATTR_REGISTER/DEREGISTER_CPUMASK attribute
 */
#define TASKSTATS_CPUMASK_MAXLEN	(100+6*NR_CPUS)

static DEFINE_PER_CPU(__u32, taskstats_seqnum) = { 0 };
static int family_registered;
struct kmem_cache *taskstats_cache;

static struct genl_family family = {
	.id		= GENL_ID_GENERATE,
	.name		= TASKSTATS_GENL_NAME,
	.version	= TASKSTATS_GENL_VERSION,
	.maxattr	= TASKSTATS_CMD_ATTR_MAX,
};

static struct nla_policy taskstats_cmd_get_policy[TASKSTATS_CMD_ATTR_MAX+1]
__read_mostly = {
	[TASKSTATS_CMD_ATTR_PID]  = { .type = NLA_U32 },
	[TASKSTATS_CMD_ATTR_TGID] = { .type = NLA_U32 },
	[TASKSTATS_CMD_ATTR_REGISTER_CPUMASK] = { .type = NLA_STRING },
	[TASKSTATS_CMD_ATTR_DEREGISTER_CPUMASK] = { .type = NLA_STRING },};

struct listener {
	struct list_head list;
	pid_t pid;
	char valid;
};

struct listener_list {
	struct rw_semaphore sem;
	struct list_head list;
};
static DEFINE_PER_CPU(struct listener_list, listener_array);

enum actions {
	REGISTER,
	DEREGISTER,
	CPU_DONT_CARE
};

static int prepare_reply(struct genl_info *info, u8 cmd, struct sk_buff **skbp,
			void **replyp, size_t size)
{
	struct sk_buff *skb;
	void *reply;

	/*
	 * If new attributes are added, please revisit this allocation
	 */
	skb = genlmsg_new(size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (!info) {
		int seq = get_cpu_var(taskstats_seqnum)++;
		put_cpu_var(taskstats_seqnum);

		reply = genlmsg_put(skb, 0, seq, &family, 0, cmd);
	} else
		reply = genlmsg_put_reply(skb, info, &family, 0, cmd);
	if (reply == NULL) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	*skbp = skb;
	*replyp = reply;
	return 0;
}

/*
 * Send taskstats data in @skb to listener with nl_pid @pid
 */
static int send_reply(struct sk_buff *skb, pid_t pid)
{
	struct genlmsghdr *genlhdr = nlmsg_data((struct nlmsghdr *)skb->data);
	void *reply = genlmsg_data(genlhdr);
	int rc;

	rc = genlmsg_end(skb, reply);
	if (rc < 0) {
		nlmsg_free(skb);
		return rc;
	}

	return genlmsg_unicast(skb, pid);
}

/*
 * Send taskstats data in @skb to listeners registered for @cpu's exit data
 */
static void send_cpu_listeners(struct sk_buff *skb,
					struct listener_list *listeners)
{
	struct genlmsghdr *genlhdr = nlmsg_data((struct nlmsghdr *)skb->data);
	struct listener *s, *tmp;
	struct sk_buff *skb_next, *skb_cur = skb;
	void *reply = genlmsg_data(genlhdr);
	int rc, delcount = 0;

	rc = genlmsg_end(skb, reply);
	if (rc < 0) {
		nlmsg_free(skb);
		return;
	}

	rc = 0;
	down_read(&listeners->sem);
	list_for_each_entry(s, &listeners->list, list) {
		skb_next = NULL;
		if (!list_is_last(&s->list, &listeners->list)) {
			skb_next = skb_clone(skb_cur, GFP_KERNEL);
			if (!skb_next)
				break;
		}
		rc = genlmsg_unicast(skb_cur, s->pid);
		if (rc == -ECONNREFUSED) {
			s->valid = 0;
			delcount++;
		}
		skb_cur = skb_next;
	}
	up_read(&listeners->sem);

	if (skb_cur)
		nlmsg_free(skb_cur);

	if (!delcount)
		return;

	/* Delete invalidated entries */
	down_write(&listeners->sem);
	list_for_each_entry_safe(s, tmp, &listeners->list, list) {
		if (!s->valid) {
			list_del(&s->list);
			kfree(s);
		}
	}
	up_write(&listeners->sem);
}

static int fill_pid(pid_t pid, struct task_struct *tsk,
		struct taskstats *stats)
{
	int rc = 0;

	if (!tsk) {
		rcu_read_lock();
		tsk = find_task_by_pid(pid);
		if (tsk)
			get_task_struct(tsk);
		rcu_read_unlock();
		if (!tsk)
			return -ESRCH;
	} else
		get_task_struct(tsk);

	memset(stats, 0, sizeof(*stats));
	/*
	 * Each accounting subsystem adds calls to its functions to
	 * fill in relevant parts of struct taskstsats as follows
	 *
	 *	per-task-foo(stats, tsk);
	 */

	delayacct_add_tsk(stats, tsk);

	/* fill in basic acct fields */
	stats->version = TASKSTATS_VERSION;
	bacct_add_tsk(stats, tsk);

	/* fill in extended acct fields */
	xacct_add_tsk(stats, tsk);

	/* Define err: label here if needed */
	put_task_struct(tsk);
	return rc;

}

static int fill_tgid(pid_t tgid, struct task_struct *first,
		struct taskstats *stats)
{
	struct task_struct *tsk;
	unsigned long flags;
	int rc = -ESRCH;

	/*
	 * Add additional stats from live tasks except zombie thread group
	 * leaders who are already counted with the dead tasks
	 */
	rcu_read_lock();
	if (!first)
		first = find_task_by_pid(tgid);

	if (!first || !lock_task_sighand(first, &flags))
		goto out;

	if (first->signal->stats)
		memcpy(stats, first->signal->stats, sizeof(*stats));
	else
		memset(stats, 0, sizeof(*stats));

	tsk = first;
	do {
		if (tsk->exit_state)
			continue;
		/*
		 * Accounting subsystem can call its functions here to
		 * fill in relevant parts of struct taskstsats as follows
		 *
		 *	per-task-foo(stats, tsk);
		 */
		delayacct_add_tsk(stats, tsk);

	} while_each_thread(first, tsk);

	unlock_task_sighand(first, &flags);
	rc = 0;
out:
	rcu_read_unlock();

	stats->version = TASKSTATS_VERSION;
	/*
	 * Accounting subsytems can also add calls here to modify
	 * fields of taskstats.
	 */
	return rc;
}


static void fill_tgid_exit(struct task_struct *tsk)
{
	unsigned long flags;

	spin_lock_irqsave(&tsk->sighand->siglock, flags);
	if (!tsk->signal->stats)
		goto ret;

	/*
	 * Each accounting subsystem calls its functions here to
	 * accumalate its per-task stats for tsk, into the per-tgid structure
	 *
	 *	per-task-foo(tsk->signal->stats, tsk);
	 */
	delayacct_add_tsk(tsk->signal->stats, tsk);
ret:
	spin_unlock_irqrestore(&tsk->sighand->siglock, flags);
	return;
}

static int add_del_listener(pid_t pid, cpumask_t *maskp, int isadd)
{
	struct listener_list *listeners;
	struct listener *s, *tmp;
	unsigned int cpu;
	cpumask_t mask = *maskp;

	if (!cpus_subset(mask, cpu_possible_map))
		return -EINVAL;

	if (isadd == REGISTER) {
		for_each_cpu_mask(cpu, mask) {
			s = kmalloc_node(sizeof(struct listener), GFP_KERNEL,
					 cpu_to_node(cpu));
			if (!s)
				goto cleanup;
			s->pid = pid;
			INIT_LIST_HEAD(&s->list);
			s->valid = 1;

			listeners = &per_cpu(listener_array, cpu);
			down_write(&listeners->sem);
			list_add(&s->list, &listeners->list);
			up_write(&listeners->sem);
		}
		return 0;
	}

	/* Deregister or cleanup */
cleanup:
	for_each_cpu_mask(cpu, mask) {
		listeners = &per_cpu(listener_array, cpu);
		down_write(&listeners->sem);
		list_for_each_entry_safe(s, tmp, &listeners->list, list) {
			if (s->pid == pid) {
				list_del(&s->list);
				kfree(s);
				break;
			}
		}
		up_write(&listeners->sem);
	}
	return 0;
}

static int parse(struct nlattr *na, cpumask_t *mask)
{
	char *data;
	int len;
	int ret;

	if (na == NULL)
		return 1;
	len = nla_len(na);
	if (len > TASKSTATS_CPUMASK_MAXLEN)
		return -E2BIG;
	if (len < 1)
		return -EINVAL;
	data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	nla_strlcpy(data, na, len);
	ret = cpulist_parse(data, *mask);
	kfree(data);
	return ret;
}

static struct taskstats *mk_reply(struct sk_buff *skb, int type, u32 pid)
{
	struct nlattr *na, *ret;
	int aggr;

	aggr = TASKSTATS_TYPE_AGGR_TGID;
	if (type == TASKSTATS_TYPE_PID)
		aggr = TASKSTATS_TYPE_AGGR_PID;

	na = nla_nest_start(skb, aggr);
	if (nla_put(skb, type, sizeof(pid), &pid) < 0)
		goto err;
	ret = nla_reserve(skb, TASKSTATS_TYPE_STATS, sizeof(struct taskstats));
	if (!ret)
		goto err;
	nla_nest_end(skb, na);

	return nla_data(ret);
err:
	return NULL;
}

static int taskstats_user_cmd(struct sk_buff *skb, struct genl_info *info)
{
	int rc = 0;
	struct sk_buff *rep_skb;
	struct taskstats *stats;
	void *reply;
	size_t size;
	cpumask_t mask;

	rc = parse(info->attrs[TASKSTATS_CMD_ATTR_REGISTER_CPUMASK], &mask);
	if (rc < 0)
		return rc;
	if (rc == 0)
		return add_del_listener(info->snd_pid, &mask, REGISTER);

	rc = parse(info->attrs[TASKSTATS_CMD_ATTR_DEREGISTER_CPUMASK], &mask);
	if (rc < 0)
		return rc;
	if (rc == 0)
		return add_del_listener(info->snd_pid, &mask, DEREGISTER);

	/*
	 * Size includes space for nested attributes
	 */
	size = nla_total_size(sizeof(u32)) +
		nla_total_size(sizeof(struct taskstats)) + nla_total_size(0);

	rc = prepare_reply(info, TASKSTATS_CMD_NEW, &rep_skb, &reply, size);
	if (rc < 0)
		return rc;

	rc = -EINVAL;
	if (info->attrs[TASKSTATS_CMD_ATTR_PID]) {
		u32 pid = nla_get_u32(info->attrs[TASKSTATS_CMD_ATTR_PID]);
		stats = mk_reply(rep_skb, TASKSTATS_TYPE_PID, pid);
		if (!stats)
			goto nla_err;

		rc = fill_pid(pid, NULL, stats);
		if (rc < 0)
			goto nla_err;
	} else if (info->attrs[TASKSTATS_CMD_ATTR_TGID]) {
		u32 tgid = nla_get_u32(info->attrs[TASKSTATS_CMD_ATTR_TGID]);
		stats = mk_reply(rep_skb, TASKSTATS_TYPE_TGID, tgid);
		if (!stats)
			goto nla_err;

		rc = fill_tgid(tgid, NULL, stats);
		if (rc < 0)
			goto nla_err;
	} else
		goto err;

	return send_reply(rep_skb, info->snd_pid);

nla_err:
	genlmsg_cancel(rep_skb, reply);
err:
	nlmsg_free(rep_skb);
	return rc;
}

static struct taskstats *taskstats_tgid_alloc(struct task_struct *tsk)
{
	struct signal_struct *sig = tsk->signal;
	struct taskstats *stats;

	if (sig->stats || thread_group_empty(tsk))
		goto ret;

	/* No problem if kmem_cache_zalloc() fails */
	stats = kmem_cache_zalloc(taskstats_cache, GFP_KERNEL);

	spin_lock_irq(&tsk->sighand->siglock);
	if (!sig->stats) {
		sig->stats = stats;
		stats = NULL;
	}
	spin_unlock_irq(&tsk->sighand->siglock);

	if (stats)
		kmem_cache_free(taskstats_cache, stats);
ret:
	return sig->stats;
}

/* Send pid data out on exit */
void taskstats_exit(struct task_struct *tsk, int group_dead)
{
	int rc;
	struct listener_list *listeners;
	struct taskstats *stats;
	struct sk_buff *rep_skb;
	void *reply;
	size_t size;
	int is_thread_group;

	if (!family_registered)
		return;

	/*
	 * Size includes space for nested attributes
	 */
	size = nla_total_size(sizeof(u32)) +
		nla_total_size(sizeof(struct taskstats)) + nla_total_size(0);

	is_thread_group = !!taskstats_tgid_alloc(tsk);
	if (is_thread_group) {
		/* PID + STATS + TGID + STATS */
		size = 2 * size;
		/* fill the tsk->signal->stats structure */
		fill_tgid_exit(tsk);
	}

	listeners = &__raw_get_cpu_var(listener_array);
	if (list_empty(&listeners->list))
		return;

	rc = prepare_reply(NULL, TASKSTATS_CMD_NEW, &rep_skb, &reply, size);
	if (rc < 0)
		return;

	stats = mk_reply(rep_skb, TASKSTATS_TYPE_PID, tsk->pid);
	if (!stats)
		goto nla_err;

	rc = fill_pid(tsk->pid, tsk, stats);
	if (rc < 0)
		goto nla_err;

	/*
	 * Doesn't matter if tsk is the leader or the last group member leaving
	 */
	if (!is_thread_group || !group_dead)
		goto send;

	stats = mk_reply(rep_skb, TASKSTATS_TYPE_TGID, tsk->tgid);
	if (!stats)
		goto nla_err;

	memcpy(stats, tsk->signal->stats, sizeof(*stats));

send:
	send_cpu_listeners(rep_skb, listeners);
	return;

nla_err:
	genlmsg_cancel(rep_skb, reply);
	nlmsg_free(rep_skb);
}

static struct genl_ops taskstats_ops = {
	.cmd		= TASKSTATS_CMD_GET,
	.doit		= taskstats_user_cmd,
	.policy		= taskstats_cmd_get_policy,
};

/* Needed early in initialization */
void __init taskstats_init_early(void)
{
	unsigned int i;

	taskstats_cache = kmem_cache_create("taskstats_cache",
						sizeof(struct taskstats),
						0, SLAB_PANIC, NULL, NULL);
	for_each_possible_cpu(i) {
		INIT_LIST_HEAD(&(per_cpu(listener_array, i).list));
		init_rwsem(&(per_cpu(listener_array, i).sem));
	}
}

static int __init taskstats_init(void)
{
	int rc;

	rc = genl_register_family(&family);
	if (rc)
		return rc;

	rc = genl_register_ops(&family, &taskstats_ops);
	if (rc < 0)
		goto err;

	family_registered = 1;
	return 0;
err:
	genl_unregister_family(&family);
	return rc;
}

/*
 * late initcall ensures initialization of statistics collection
 * mechanisms precedes initialization of the taskstats interface
 */
late_initcall(taskstats_init);
