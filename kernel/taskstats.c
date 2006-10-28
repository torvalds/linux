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
kmem_cache_t *taskstats_cache;

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
	skb = nlmsg_new(genlmsg_total_size(size), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (!info) {
		int seq = get_cpu_var(taskstats_seqnum)++;
		put_cpu_var(taskstats_seqnum);

		reply = genlmsg_put(skb, 0, seq,
				family.id, 0, 0,
				cmd, family.version);
	} else
		reply = genlmsg_put(skb, info->snd_pid, info->snd_seq,
				family.id, 0, 0,
				cmd, family.version);
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
static void send_cpu_listeners(struct sk_buff *skb, unsigned int cpu)
{
	struct genlmsghdr *genlhdr = nlmsg_data((struct nlmsghdr *)skb->data);
	struct listener_list *listeners;
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
	listeners = &per_cpu(listener_array, cpu);
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

static int fill_pid(pid_t pid, struct task_struct *pidtsk,
		struct taskstats *stats)
{
	int rc = 0;
	struct task_struct *tsk = pidtsk;

	if (!pidtsk) {
		read_lock(&tasklist_lock);
		tsk = find_task_by_pid(pid);
		if (!tsk) {
			read_unlock(&tasklist_lock);
			return -ESRCH;
		}
		get_task_struct(tsk);
		read_unlock(&tasklist_lock);
	} else
		get_task_struct(tsk);

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

static int fill_tgid(pid_t tgid, struct task_struct *tgidtsk,
		struct taskstats *stats)
{
	struct task_struct *tsk, *first;
	unsigned long flags;

	/*
	 * Add additional stats from live tasks except zombie thread group
	 * leaders who are already counted with the dead tasks
	 */
	first = tgidtsk;
	if (!first) {
		read_lock(&tasklist_lock);
		first = find_task_by_pid(tgid);
		if (!first) {
			read_unlock(&tasklist_lock);
			return -ESRCH;
		}
		get_task_struct(first);
		read_unlock(&tasklist_lock);
	} else
		get_task_struct(first);


	tsk = first;
	read_lock(&tasklist_lock);
	/* Start with stats from dead tasks */
	if (first->signal) {
		spin_lock_irqsave(&first->signal->stats_lock, flags);
		if (first->signal->stats)
			memcpy(stats, first->signal->stats, sizeof(*stats));
		spin_unlock_irqrestore(&first->signal->stats_lock, flags);
	}

	do {
		if (tsk->exit_state == EXIT_ZOMBIE && thread_group_leader(tsk))
			continue;
		/*
		 * Accounting subsystem can call its functions here to
		 * fill in relevant parts of struct taskstsats as follows
		 *
		 *	per-task-foo(stats, tsk);
		 */
		delayacct_add_tsk(stats, tsk);

	} while_each_thread(first, tsk);
	read_unlock(&tasklist_lock);
	stats->version = TASKSTATS_VERSION;

	/*
	 * Accounting subsytems can also add calls here to modify
	 * fields of taskstats.
	 */
	put_task_struct(first);
	return 0;
}


static void fill_tgid_exit(struct task_struct *tsk)
{
	unsigned long flags;

	spin_lock_irqsave(&tsk->signal->stats_lock, flags);
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
	spin_unlock_irqrestore(&tsk->signal->stats_lock, flags);
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

static int taskstats_user_cmd(struct sk_buff *skb, struct genl_info *info)
{
	int rc = 0;
	struct sk_buff *rep_skb;
	struct taskstats stats;
	void *reply;
	size_t size;
	struct nlattr *na;
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

	memset(&stats, 0, sizeof(stats));
	rc = prepare_reply(info, TASKSTATS_CMD_NEW, &rep_skb, &reply, size);
	if (rc < 0)
		return rc;

	if (info->attrs[TASKSTATS_CMD_ATTR_PID]) {
		u32 pid = nla_get_u32(info->attrs[TASKSTATS_CMD_ATTR_PID]);
		rc = fill_pid(pid, NULL, &stats);
		if (rc < 0)
			goto err;

		na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_PID);
		NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_PID, pid);
		NLA_PUT_TYPE(rep_skb, struct taskstats, TASKSTATS_TYPE_STATS,
				stats);
	} else if (info->attrs[TASKSTATS_CMD_ATTR_TGID]) {
		u32 tgid = nla_get_u32(info->attrs[TASKSTATS_CMD_ATTR_TGID]);
		rc = fill_tgid(tgid, NULL, &stats);
		if (rc < 0)
			goto err;

		na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_TGID);
		NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_TGID, tgid);
		NLA_PUT_TYPE(rep_skb, struct taskstats, TASKSTATS_TYPE_STATS,
				stats);
	} else {
		rc = -EINVAL;
		goto err;
	}

	nla_nest_end(rep_skb, na);

	return send_reply(rep_skb, info->snd_pid);

nla_put_failure:
	return genlmsg_cancel(rep_skb, reply);
err:
	nlmsg_free(rep_skb);
	return rc;
}

void taskstats_exit_alloc(struct taskstats **ptidstats, unsigned int *mycpu)
{
	struct listener_list *listeners;
	struct taskstats *tmp;
	/*
	 * This is the cpu on which the task is exiting currently and will
	 * be the one for which the exit event is sent, even if the cpu
	 * on which this function is running changes later.
	 */
	*mycpu = raw_smp_processor_id();

	*ptidstats = NULL;
	tmp = kmem_cache_zalloc(taskstats_cache, SLAB_KERNEL);
	if (!tmp)
		return;

	listeners = &per_cpu(listener_array, *mycpu);
	down_read(&listeners->sem);
	if (!list_empty(&listeners->list)) {
		*ptidstats = tmp;
		tmp = NULL;
	}
	up_read(&listeners->sem);
	kfree(tmp);
}

/* Send pid data out on exit */
void taskstats_exit_send(struct task_struct *tsk, struct taskstats *tidstats,
			int group_dead, unsigned int mycpu)
{
	int rc;
	struct sk_buff *rep_skb;
	void *reply;
	size_t size;
	int is_thread_group;
	struct nlattr *na;
	unsigned long flags;

	if (!family_registered || !tidstats)
		return;

	spin_lock_irqsave(&tsk->signal->stats_lock, flags);
	is_thread_group = tsk->signal->stats ? 1 : 0;
	spin_unlock_irqrestore(&tsk->signal->stats_lock, flags);

	rc = 0;
	/*
	 * Size includes space for nested attributes
	 */
	size = nla_total_size(sizeof(u32)) +
		nla_total_size(sizeof(struct taskstats)) + nla_total_size(0);

	if (is_thread_group)
		size = 2 * size;	/* PID + STATS + TGID + STATS */

	rc = prepare_reply(NULL, TASKSTATS_CMD_NEW, &rep_skb, &reply, size);
	if (rc < 0)
		goto ret;

	rc = fill_pid(tsk->pid, tsk, tidstats);
	if (rc < 0)
		goto err_skb;

	na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_PID);
	NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_PID, (u32)tsk->pid);
	NLA_PUT_TYPE(rep_skb, struct taskstats, TASKSTATS_TYPE_STATS,
			*tidstats);
	nla_nest_end(rep_skb, na);

	if (!is_thread_group)
		goto send;

	/*
	 * tsk has/had a thread group so fill the tsk->signal->stats structure
	 * Doesn't matter if tsk is the leader or the last group member leaving
	 */

	fill_tgid_exit(tsk);
	if (!group_dead)
		goto send;

	na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_TGID);
	NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_TGID, (u32)tsk->tgid);
	/* No locking needed for tsk->signal->stats since group is dead */
	NLA_PUT_TYPE(rep_skb, struct taskstats, TASKSTATS_TYPE_STATS,
			*tsk->signal->stats);
	nla_nest_end(rep_skb, na);

send:
	send_cpu_listeners(rep_skb, mycpu);
	return;

nla_put_failure:
	genlmsg_cancel(rep_skb, reply);
	goto ret;
err_skb:
	nlmsg_free(rep_skb);
ret:
	return;
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
