// SPDX-License-Identifier: GPL-2.0-or-later
/* audit.c -- Auditing support
 * Gateway between the kernel (e.g., selinux) and the user-space audit daemon.
 * System-call specific features have moved to auditsc.c
 *
 * Copyright 2003-2007 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * Written by Rickard E. (Rik) Faith <faith@redhat.com>
 *
 * Goals: 1) Integrate fully with Security Modules.
 *	  2) Minimal run-time overhead:
 *	     a) Minimal when syscall auditing is disabled (audit_enable=0).
 *	     b) Small when syscall auditing is enabled and no audit record
 *		is generated (defer as much work as possible to record
 *		generation time):
 *		i) context is allocated,
 *		ii) names from getname are stored without a copy, and
 *		iii) inode information stored from path_lookup.
 *	  3) Ability to disable syscall auditing at boot time (audit=0).
 *	  4) Usable by other parts of the kernel (if audit_log* is called,
 *	     then a syscall record will be generated automatically for the
 *	     current syscall).
 *	  5) Netlink interface to user-space.
 *	  6) Support low-overhead kernel-based filtering to minimize the
 *	     information that must be passed to user-space.
 *
 * Audit userspace, documentation, tests, and bug/issue trackers:
 * 	https://github.com/linux-audit
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/file.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/pid.h>

#include <linux/audit.h>

#include <net/sock.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#ifdef CONFIG_SECURITY
#include <linux/security.h>
#endif
#include <linux/freezer.h>
#include <linux/pid_namespace.h>
#include <net/netns/generic.h>

#include "audit.h"

/* No auditing will take place until audit_initialized == AUDIT_INITIALIZED.
 * (Initialization happens after skb_init is called.) */
#define AUDIT_DISABLED		-1
#define AUDIT_UNINITIALIZED	0
#define AUDIT_INITIALIZED	1
static int	audit_initialized;

u32		audit_enabled = AUDIT_OFF;
bool		audit_ever_enabled = !!AUDIT_OFF;

EXPORT_SYMBOL_GPL(audit_enabled);

/* Default state when kernel boots without any parameters. */
static u32	audit_default = AUDIT_OFF;

/* If auditing cannot proceed, audit_failure selects what happens. */
static u32	audit_failure = AUDIT_FAIL_PRINTK;

/* private audit network namespace index */
static unsigned int audit_net_id;

/**
 * struct audit_net - audit private network namespace data
 * @sk: communication socket
 */
struct audit_net {
	struct sock *sk;
};

/**
 * struct auditd_connection - kernel/auditd connection state
 * @pid: auditd PID
 * @portid: netlink portid
 * @net: the associated network namespace
 * @rcu: RCU head
 *
 * Description:
 * This struct is RCU protected; you must either hold the RCU lock for reading
 * or the associated spinlock for writing.
 */
struct auditd_connection {
	struct pid *pid;
	u32 portid;
	struct net *net;
	struct rcu_head rcu;
};
static struct auditd_connection __rcu *auditd_conn;
static DEFINE_SPINLOCK(auditd_conn_lock);

/* If audit_rate_limit is non-zero, limit the rate of sending audit records
 * to that number per second.  This prevents DoS attacks, but results in
 * audit records being dropped. */
static u32	audit_rate_limit;

/* Number of outstanding audit_buffers allowed.
 * When set to zero, this means unlimited. */
static u32	audit_backlog_limit = 64;
#define AUDIT_BACKLOG_WAIT_TIME (60 * HZ)
static u32	audit_backlog_wait_time = AUDIT_BACKLOG_WAIT_TIME;

/* The identity of the user shutting down the audit system. */
static kuid_t		audit_sig_uid = INVALID_UID;
static pid_t		audit_sig_pid = -1;
static u32		audit_sig_sid;

/* Records can be lost in several ways:
   0) [suppressed in audit_alloc]
   1) out of memory in audit_log_start [kmalloc of struct audit_buffer]
   2) out of memory in audit_log_move [alloc_skb]
   3) suppressed due to audit_rate_limit
   4) suppressed due to audit_backlog_limit
*/
static atomic_t	audit_lost = ATOMIC_INIT(0);

/* Monotonically increasing sum of time the kernel has spent
 * waiting while the backlog limit is exceeded.
 */
static atomic_t audit_backlog_wait_time_actual = ATOMIC_INIT(0);

/* Hash for inode-based rules */
struct list_head audit_inode_hash[AUDIT_INODE_BUCKETS];

static struct kmem_cache *audit_buffer_cache;

/* queue msgs to send via kauditd_task */
static struct sk_buff_head audit_queue;
/* queue msgs due to temporary unicast send problems */
static struct sk_buff_head audit_retry_queue;
/* queue msgs waiting for new auditd connection */
static struct sk_buff_head audit_hold_queue;

/* queue servicing thread */
static struct task_struct *kauditd_task;
static DECLARE_WAIT_QUEUE_HEAD(kauditd_wait);

/* waitqueue for callers who are blocked on the audit backlog */
static DECLARE_WAIT_QUEUE_HEAD(audit_backlog_wait);

static struct audit_features af = {.vers = AUDIT_FEATURE_VERSION,
				   .mask = -1,
				   .features = 0,
				   .lock = 0,};

static char *audit_feature_names[2] = {
	"only_unset_loginuid",
	"loginuid_immutable",
};

/**
 * struct audit_ctl_mutex - serialize requests from userspace
 * @lock: the mutex used for locking
 * @owner: the task which owns the lock
 *
 * Description:
 * This is the lock struct used to ensure we only process userspace requests
 * in an orderly fashion.  We can't simply use a mutex/lock here because we
 * need to track lock ownership so we don't end up blocking the lock owner in
 * audit_log_start() or similar.
 */
static struct audit_ctl_mutex {
	struct mutex lock;
	void *owner;
} audit_cmd_mutex;

/* AUDIT_BUFSIZ is the size of the temporary buffer used for formatting
 * audit records.  Since printk uses a 1024 byte buffer, this buffer
 * should be at least that large. */
#define AUDIT_BUFSIZ 1024

/* The audit_buffer is used when formatting an audit record.  The caller
 * locks briefly to get the record off the freelist or to allocate the
 * buffer, and locks briefly to send the buffer to the netlink layer or
 * to place it on a transmit queue.  Multiple audit_buffers can be in
 * use simultaneously. */
struct audit_buffer {
	struct sk_buff       *skb;	/* formatted skb ready to send */
	struct audit_context *ctx;	/* NULL or associated context */
	gfp_t		     gfp_mask;
};

struct audit_reply {
	__u32 portid;
	struct net *net;
	struct sk_buff *skb;
};

/**
 * auditd_test_task - Check to see if a given task is an audit daemon
 * @task: the task to check
 *
 * Description:
 * Return 1 if the task is a registered audit daemon, 0 otherwise.
 */
int auditd_test_task(struct task_struct *task)
{
	int rc;
	struct auditd_connection *ac;

	rcu_read_lock();
	ac = rcu_dereference(auditd_conn);
	rc = (ac && ac->pid == task_tgid(task) ? 1 : 0);
	rcu_read_unlock();

	return rc;
}

/**
 * audit_ctl_lock - Take the audit control lock
 */
void audit_ctl_lock(void)
{
	mutex_lock(&audit_cmd_mutex.lock);
	audit_cmd_mutex.owner = current;
}

/**
 * audit_ctl_unlock - Drop the audit control lock
 */
void audit_ctl_unlock(void)
{
	audit_cmd_mutex.owner = NULL;
	mutex_unlock(&audit_cmd_mutex.lock);
}

/**
 * audit_ctl_owner_current - Test to see if the current task owns the lock
 *
 * Description:
 * Return true if the current task owns the audit control lock, false if it
 * doesn't own the lock.
 */
static bool audit_ctl_owner_current(void)
{
	return (current == audit_cmd_mutex.owner);
}

/**
 * auditd_pid_vnr - Return the auditd PID relative to the namespace
 *
 * Description:
 * Returns the PID in relation to the namespace, 0 on failure.
 */
static pid_t auditd_pid_vnr(void)
{
	pid_t pid;
	const struct auditd_connection *ac;

	rcu_read_lock();
	ac = rcu_dereference(auditd_conn);
	if (!ac || !ac->pid)
		pid = 0;
	else
		pid = pid_vnr(ac->pid);
	rcu_read_unlock();

	return pid;
}

/**
 * audit_get_sk - Return the audit socket for the given network namespace
 * @net: the destination network namespace
 *
 * Description:
 * Returns the sock pointer if valid, NULL otherwise.  The caller must ensure
 * that a reference is held for the network namespace while the sock is in use.
 */
static struct sock *audit_get_sk(const struct net *net)
{
	struct audit_net *aunet;

	if (!net)
		return NULL;

	aunet = net_generic(net, audit_net_id);
	return aunet->sk;
}

void audit_panic(const char *message)
{
	switch (audit_failure) {
	case AUDIT_FAIL_SILENT:
		break;
	case AUDIT_FAIL_PRINTK:
		if (printk_ratelimit())
			pr_err("%s\n", message);
		break;
	case AUDIT_FAIL_PANIC:
		panic("audit: %s\n", message);
		break;
	}
}

static inline int audit_rate_check(void)
{
	static unsigned long	last_check = 0;
	static int		messages   = 0;
	static DEFINE_SPINLOCK(lock);
	unsigned long		flags;
	unsigned long		now;
	unsigned long		elapsed;
	int			retval	   = 0;

	if (!audit_rate_limit) return 1;

	spin_lock_irqsave(&lock, flags);
	if (++messages < audit_rate_limit) {
		retval = 1;
	} else {
		now     = jiffies;
		elapsed = now - last_check;
		if (elapsed > HZ) {
			last_check = now;
			messages   = 0;
			retval     = 1;
		}
	}
	spin_unlock_irqrestore(&lock, flags);

	return retval;
}

/**
 * audit_log_lost - conditionally log lost audit message event
 * @message: the message stating reason for lost audit message
 *
 * Emit at least 1 message per second, even if audit_rate_check is
 * throttling.
 * Always increment the lost messages counter.
*/
void audit_log_lost(const char *message)
{
	static unsigned long	last_msg = 0;
	static DEFINE_SPINLOCK(lock);
	unsigned long		flags;
	unsigned long		now;
	int			print;

	atomic_inc(&audit_lost);

	print = (audit_failure == AUDIT_FAIL_PANIC || !audit_rate_limit);

	if (!print) {
		spin_lock_irqsave(&lock, flags);
		now = jiffies;
		if (now - last_msg > HZ) {
			print = 1;
			last_msg = now;
		}
		spin_unlock_irqrestore(&lock, flags);
	}

	if (print) {
		if (printk_ratelimit())
			pr_warn("audit_lost=%u audit_rate_limit=%u audit_backlog_limit=%u\n",
				atomic_read(&audit_lost),
				audit_rate_limit,
				audit_backlog_limit);
		audit_panic(message);
	}
}

static int audit_log_config_change(char *function_name, u32 new, u32 old,
				   int allow_changes)
{
	struct audit_buffer *ab;
	int rc = 0;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_CONFIG_CHANGE);
	if (unlikely(!ab))
		return rc;
	audit_log_format(ab, "op=set %s=%u old=%u ", function_name, new, old);
	audit_log_session_info(ab);
	rc = audit_log_task_context(ab);
	if (rc)
		allow_changes = 0; /* Something weird, deny request */
	audit_log_format(ab, " res=%d", allow_changes);
	audit_log_end(ab);
	return rc;
}

static int audit_do_config_change(char *function_name, u32 *to_change, u32 new)
{
	int allow_changes, rc = 0;
	u32 old = *to_change;

	/* check if we are locked */
	if (audit_enabled == AUDIT_LOCKED)
		allow_changes = 0;
	else
		allow_changes = 1;

	if (audit_enabled != AUDIT_OFF) {
		rc = audit_log_config_change(function_name, new, old, allow_changes);
		if (rc)
			allow_changes = 0;
	}

	/* If we are allowed, make the change */
	if (allow_changes == 1)
		*to_change = new;
	/* Not allowed, update reason */
	else if (rc == 0)
		rc = -EPERM;
	return rc;
}

static int audit_set_rate_limit(u32 limit)
{
	return audit_do_config_change("audit_rate_limit", &audit_rate_limit, limit);
}

static int audit_set_backlog_limit(u32 limit)
{
	return audit_do_config_change("audit_backlog_limit", &audit_backlog_limit, limit);
}

static int audit_set_backlog_wait_time(u32 timeout)
{
	return audit_do_config_change("audit_backlog_wait_time",
				      &audit_backlog_wait_time, timeout);
}

static int audit_set_enabled(u32 state)
{
	int rc;
	if (state > AUDIT_LOCKED)
		return -EINVAL;

	rc =  audit_do_config_change("audit_enabled", &audit_enabled, state);
	if (!rc)
		audit_ever_enabled |= !!state;

	return rc;
}

static int audit_set_failure(u32 state)
{
	if (state != AUDIT_FAIL_SILENT
	    && state != AUDIT_FAIL_PRINTK
	    && state != AUDIT_FAIL_PANIC)
		return -EINVAL;

	return audit_do_config_change("audit_failure", &audit_failure, state);
}

/**
 * auditd_conn_free - RCU helper to release an auditd connection struct
 * @rcu: RCU head
 *
 * Description:
 * Drop any references inside the auditd connection tracking struct and free
 * the memory.
 */
static void auditd_conn_free(struct rcu_head *rcu)
{
	struct auditd_connection *ac;

	ac = container_of(rcu, struct auditd_connection, rcu);
	put_pid(ac->pid);
	put_net(ac->net);
	kfree(ac);
}

/**
 * auditd_set - Set/Reset the auditd connection state
 * @pid: auditd PID
 * @portid: auditd netlink portid
 * @net: auditd network namespace pointer
 *
 * Description:
 * This function will obtain and drop network namespace references as
 * necessary.  Returns zero on success, negative values on failure.
 */
static int auditd_set(struct pid *pid, u32 portid, struct net *net)
{
	unsigned long flags;
	struct auditd_connection *ac_old, *ac_new;

	if (!pid || !net)
		return -EINVAL;

	ac_new = kzalloc(sizeof(*ac_new), GFP_KERNEL);
	if (!ac_new)
		return -ENOMEM;
	ac_new->pid = get_pid(pid);
	ac_new->portid = portid;
	ac_new->net = get_net(net);

	spin_lock_irqsave(&auditd_conn_lock, flags);
	ac_old = rcu_dereference_protected(auditd_conn,
					   lockdep_is_held(&auditd_conn_lock));
	rcu_assign_pointer(auditd_conn, ac_new);
	spin_unlock_irqrestore(&auditd_conn_lock, flags);

	if (ac_old)
		call_rcu(&ac_old->rcu, auditd_conn_free);

	return 0;
}

/**
 * kauditd_print_skb - Print the audit record to the ring buffer
 * @skb: audit record
 *
 * Whatever the reason, this packet may not make it to the auditd connection
 * so write it via printk so the information isn't completely lost.
 */
static void kauditd_printk_skb(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	char *data = nlmsg_data(nlh);

	if (nlh->nlmsg_type != AUDIT_EOE && printk_ratelimit())
		pr_notice("type=%d %s\n", nlh->nlmsg_type, data);
}

/**
 * kauditd_rehold_skb - Handle a audit record send failure in the hold queue
 * @skb: audit record
 *
 * Description:
 * This should only be used by the kauditd_thread when it fails to flush the
 * hold queue.
 */
static void kauditd_rehold_skb(struct sk_buff *skb)
{
	/* put the record back in the queue at the same place */
	skb_queue_head(&audit_hold_queue, skb);
}

/**
 * kauditd_hold_skb - Queue an audit record, waiting for auditd
 * @skb: audit record
 *
 * Description:
 * Queue the audit record, waiting for an instance of auditd.  When this
 * function is called we haven't given up yet on sending the record, but things
 * are not looking good.  The first thing we want to do is try to write the
 * record via printk and then see if we want to try and hold on to the record
 * and queue it, if we have room.  If we want to hold on to the record, but we
 * don't have room, record a record lost message.
 */
static void kauditd_hold_skb(struct sk_buff *skb)
{
	/* at this point it is uncertain if we will ever send this to auditd so
	 * try to send the message via printk before we go any further */
	kauditd_printk_skb(skb);

	/* can we just silently drop the message? */
	if (!audit_default) {
		kfree_skb(skb);
		return;
	}

	/* if we have room, queue the message */
	if (!audit_backlog_limit ||
	    skb_queue_len(&audit_hold_queue) < audit_backlog_limit) {
		skb_queue_tail(&audit_hold_queue, skb);
		return;
	}

	/* we have no other options - drop the message */
	audit_log_lost("kauditd hold queue overflow");
	kfree_skb(skb);
}

/**
 * kauditd_retry_skb - Queue an audit record, attempt to send again to auditd
 * @skb: audit record
 *
 * Description:
 * Not as serious as kauditd_hold_skb() as we still have a connected auditd,
 * but for some reason we are having problems sending it audit records so
 * queue the given record and attempt to resend.
 */
static void kauditd_retry_skb(struct sk_buff *skb)
{
	/* NOTE: because records should only live in the retry queue for a
	 * short period of time, before either being sent or moved to the hold
	 * queue, we don't currently enforce a limit on this queue */
	skb_queue_tail(&audit_retry_queue, skb);
}

/**
 * auditd_reset - Disconnect the auditd connection
 * @ac: auditd connection state
 *
 * Description:
 * Break the auditd/kauditd connection and move all the queued records into the
 * hold queue in case auditd reconnects.  It is important to note that the @ac
 * pointer should never be dereferenced inside this function as it may be NULL
 * or invalid, you can only compare the memory address!  If @ac is NULL then
 * the connection will always be reset.
 */
static void auditd_reset(const struct auditd_connection *ac)
{
	unsigned long flags;
	struct sk_buff *skb;
	struct auditd_connection *ac_old;

	/* if it isn't already broken, break the connection */
	spin_lock_irqsave(&auditd_conn_lock, flags);
	ac_old = rcu_dereference_protected(auditd_conn,
					   lockdep_is_held(&auditd_conn_lock));
	if (ac && ac != ac_old) {
		/* someone already registered a new auditd connection */
		spin_unlock_irqrestore(&auditd_conn_lock, flags);
		return;
	}
	rcu_assign_pointer(auditd_conn, NULL);
	spin_unlock_irqrestore(&auditd_conn_lock, flags);

	if (ac_old)
		call_rcu(&ac_old->rcu, auditd_conn_free);

	/* flush the retry queue to the hold queue, but don't touch the main
	 * queue since we need to process that normally for multicast */
	while ((skb = skb_dequeue(&audit_retry_queue)))
		kauditd_hold_skb(skb);
}

/**
 * auditd_send_unicast_skb - Send a record via unicast to auditd
 * @skb: audit record
 *
 * Description:
 * Send a skb to the audit daemon, returns positive/zero values on success and
 * negative values on failure; in all cases the skb will be consumed by this
 * function.  If the send results in -ECONNREFUSED the connection with auditd
 * will be reset.  This function may sleep so callers should not hold any locks
 * where this would cause a problem.
 */
static int auditd_send_unicast_skb(struct sk_buff *skb)
{
	int rc;
	u32 portid;
	struct net *net;
	struct sock *sk;
	struct auditd_connection *ac;

	/* NOTE: we can't call netlink_unicast while in the RCU section so
	 *       take a reference to the network namespace and grab local
	 *       copies of the namespace, the sock, and the portid; the
	 *       namespace and sock aren't going to go away while we hold a
	 *       reference and if the portid does become invalid after the RCU
	 *       section netlink_unicast() should safely return an error */

	rcu_read_lock();
	ac = rcu_dereference(auditd_conn);
	if (!ac) {
		rcu_read_unlock();
		kfree_skb(skb);
		rc = -ECONNREFUSED;
		goto err;
	}
	net = get_net(ac->net);
	sk = audit_get_sk(net);
	portid = ac->portid;
	rcu_read_unlock();

	rc = netlink_unicast(sk, skb, portid, 0);
	put_net(net);
	if (rc < 0)
		goto err;

	return rc;

err:
	if (ac && rc == -ECONNREFUSED)
		auditd_reset(ac);
	return rc;
}

/**
 * kauditd_send_queue - Helper for kauditd_thread to flush skb queues
 * @sk: the sending sock
 * @portid: the netlink destination
 * @queue: the skb queue to process
 * @retry_limit: limit on number of netlink unicast failures
 * @skb_hook: per-skb hook for additional processing
 * @err_hook: hook called if the skb fails the netlink unicast send
 *
 * Description:
 * Run through the given queue and attempt to send the audit records to auditd,
 * returns zero on success, negative values on failure.  It is up to the caller
 * to ensure that the @sk is valid for the duration of this function.
 *
 */
static int kauditd_send_queue(struct sock *sk, u32 portid,
			      struct sk_buff_head *queue,
			      unsigned int retry_limit,
			      void (*skb_hook)(struct sk_buff *skb),
			      void (*err_hook)(struct sk_buff *skb))
{
	int rc = 0;
	struct sk_buff *skb;
	static unsigned int failed = 0;

	/* NOTE: kauditd_thread takes care of all our locking, we just use
	 *       the netlink info passed to us (e.g. sk and portid) */

	while ((skb = skb_dequeue(queue))) {
		/* call the skb_hook for each skb we touch */
		if (skb_hook)
			(*skb_hook)(skb);

		/* can we send to anyone via unicast? */
		if (!sk) {
			if (err_hook)
				(*err_hook)(skb);
			continue;
		}

		/* grab an extra skb reference in case of error */
		skb_get(skb);
		rc = netlink_unicast(sk, skb, portid, 0);
		if (rc < 0) {
			/* fatal failure for our queue flush attempt? */
			if (++failed >= retry_limit ||
			    rc == -ECONNREFUSED || rc == -EPERM) {
				/* yes - error processing for the queue */
				sk = NULL;
				if (err_hook)
					(*err_hook)(skb);
				if (!skb_hook)
					goto out;
				/* keep processing with the skb_hook */
				continue;
			} else
				/* no - requeue to preserve ordering */
				skb_queue_head(queue, skb);
		} else {
			/* it worked - drop the extra reference and continue */
			consume_skb(skb);
			failed = 0;
		}
	}

out:
	return (rc >= 0 ? 0 : rc);
}

/*
 * kauditd_send_multicast_skb - Send a record to any multicast listeners
 * @skb: audit record
 *
 * Description:
 * Write a multicast message to anyone listening in the initial network
 * namespace.  This function doesn't consume an skb as might be expected since
 * it has to copy it anyways.
 */
static void kauditd_send_multicast_skb(struct sk_buff *skb)
{
	struct sk_buff *copy;
	struct sock *sock = audit_get_sk(&init_net);
	struct nlmsghdr *nlh;

	/* NOTE: we are not taking an additional reference for init_net since
	 *       we don't have to worry about it going away */

	if (!netlink_has_listeners(sock, AUDIT_NLGRP_READLOG))
		return;

	/*
	 * The seemingly wasteful skb_copy() rather than bumping the refcount
	 * using skb_get() is necessary because non-standard mods are made to
	 * the skb by the original kaudit unicast socket send routine.  The
	 * existing auditd daemon assumes this breakage.  Fixing this would
	 * require co-ordinating a change in the established protocol between
	 * the kaudit kernel subsystem and the auditd userspace code.  There is
	 * no reason for new multicast clients to continue with this
	 * non-compliance.
	 */
	copy = skb_copy(skb, GFP_KERNEL);
	if (!copy)
		return;
	nlh = nlmsg_hdr(copy);
	nlh->nlmsg_len = skb->len;

	nlmsg_multicast(sock, copy, 0, AUDIT_NLGRP_READLOG, GFP_KERNEL);
}

/**
 * kauditd_thread - Worker thread to send audit records to userspace
 * @dummy: unused
 */
static int kauditd_thread(void *dummy)
{
	int rc;
	u32 portid = 0;
	struct net *net = NULL;
	struct sock *sk = NULL;
	struct auditd_connection *ac;

#define UNICAST_RETRIES 5

	set_freezable();
	while (!kthread_should_stop()) {
		/* NOTE: see the lock comments in auditd_send_unicast_skb() */
		rcu_read_lock();
		ac = rcu_dereference(auditd_conn);
		if (!ac) {
			rcu_read_unlock();
			goto main_queue;
		}
		net = get_net(ac->net);
		sk = audit_get_sk(net);
		portid = ac->portid;
		rcu_read_unlock();

		/* attempt to flush the hold queue */
		rc = kauditd_send_queue(sk, portid,
					&audit_hold_queue, UNICAST_RETRIES,
					NULL, kauditd_rehold_skb);
		if (rc < 0) {
			sk = NULL;
			auditd_reset(ac);
			goto main_queue;
		}

		/* attempt to flush the retry queue */
		rc = kauditd_send_queue(sk, portid,
					&audit_retry_queue, UNICAST_RETRIES,
					NULL, kauditd_hold_skb);
		if (rc < 0) {
			sk = NULL;
			auditd_reset(ac);
			goto main_queue;
		}

main_queue:
		/* process the main queue - do the multicast send and attempt
		 * unicast, dump failed record sends to the retry queue; if
		 * sk == NULL due to previous failures we will just do the
		 * multicast send and move the record to the hold queue */
		rc = kauditd_send_queue(sk, portid, &audit_queue, 1,
					kauditd_send_multicast_skb,
					(sk ?
					 kauditd_retry_skb : kauditd_hold_skb));
		if (ac && rc < 0)
			auditd_reset(ac);
		sk = NULL;

		/* drop our netns reference, no auditd sends past this line */
		if (net) {
			put_net(net);
			net = NULL;
		}

		/* we have processed all the queues so wake everyone */
		wake_up(&audit_backlog_wait);

		/* NOTE: we want to wake up if there is anything on the queue,
		 *       regardless of if an auditd is connected, as we need to
		 *       do the multicast send and rotate records from the
		 *       main queue to the retry/hold queues */
		wait_event_freezable(kauditd_wait,
				     (skb_queue_len(&audit_queue) ? 1 : 0));
	}

	return 0;
}

int audit_send_list_thread(void *_dest)
{
	struct audit_netlink_list *dest = _dest;
	struct sk_buff *skb;
	struct sock *sk = audit_get_sk(dest->net);

	/* wait for parent to finish and send an ACK */
	audit_ctl_lock();
	audit_ctl_unlock();

	while ((skb = __skb_dequeue(&dest->q)) != NULL)
		netlink_unicast(sk, skb, dest->portid, 0);

	put_net(dest->net);
	kfree(dest);

	return 0;
}

struct sk_buff *audit_make_reply(int seq, int type, int done,
				 int multi, const void *payload, int size)
{
	struct sk_buff	*skb;
	struct nlmsghdr	*nlh;
	void		*data;
	int		flags = multi ? NLM_F_MULTI : 0;
	int		t     = done  ? NLMSG_DONE  : type;

	skb = nlmsg_new(size, GFP_KERNEL);
	if (!skb)
		return NULL;

	nlh	= nlmsg_put(skb, 0, seq, t, size, flags);
	if (!nlh)
		goto out_kfree_skb;
	data = nlmsg_data(nlh);
	memcpy(data, payload, size);
	return skb;

out_kfree_skb:
	kfree_skb(skb);
	return NULL;
}

static void audit_free_reply(struct audit_reply *reply)
{
	if (!reply)
		return;

	kfree_skb(reply->skb);
	if (reply->net)
		put_net(reply->net);
	kfree(reply);
}

static int audit_send_reply_thread(void *arg)
{
	struct audit_reply *reply = (struct audit_reply *)arg;

	audit_ctl_lock();
	audit_ctl_unlock();

	/* Ignore failure. It'll only happen if the sender goes away,
	   because our timeout is set to infinite. */
	netlink_unicast(audit_get_sk(reply->net), reply->skb, reply->portid, 0);
	reply->skb = NULL;
	audit_free_reply(reply);
	return 0;
}

/**
 * audit_send_reply - send an audit reply message via netlink
 * @request_skb: skb of request we are replying to (used to target the reply)
 * @seq: sequence number
 * @type: audit message type
 * @done: done (last) flag
 * @multi: multi-part message flag
 * @payload: payload data
 * @size: payload size
 *
 * Allocates a skb, builds the netlink message, and sends it to the port id.
 */
static void audit_send_reply(struct sk_buff *request_skb, int seq, int type, int done,
			     int multi, const void *payload, int size)
{
	struct task_struct *tsk;
	struct audit_reply *reply;

	reply = kzalloc(sizeof(*reply), GFP_KERNEL);
	if (!reply)
		return;

	reply->skb = audit_make_reply(seq, type, done, multi, payload, size);
	if (!reply->skb)
		goto err;
	reply->net = get_net(sock_net(NETLINK_CB(request_skb).sk));
	reply->portid = NETLINK_CB(request_skb).portid;

	tsk = kthread_run(audit_send_reply_thread, reply, "audit_send_reply");
	if (IS_ERR(tsk))
		goto err;

	return;

err:
	audit_free_reply(reply);
}

/*
 * Check for appropriate CAP_AUDIT_ capabilities on incoming audit
 * control messages.
 */
static int audit_netlink_ok(struct sk_buff *skb, u16 msg_type)
{
	int err = 0;

	/* Only support initial user namespace for now. */
	/*
	 * We return ECONNREFUSED because it tricks userspace into thinking
	 * that audit was not configured into the kernel.  Lots of users
	 * configure their PAM stack (because that's what the distro does)
	 * to reject login if unable to send messages to audit.  If we return
	 * ECONNREFUSED the PAM stack thinks the kernel does not have audit
	 * configured in and will let login proceed.  If we return EPERM
	 * userspace will reject all logins.  This should be removed when we
	 * support non init namespaces!!
	 */
	if (current_user_ns() != &init_user_ns)
		return -ECONNREFUSED;

	switch (msg_type) {
	case AUDIT_LIST:
	case AUDIT_ADD:
	case AUDIT_DEL:
		return -EOPNOTSUPP;
	case AUDIT_GET:
	case AUDIT_SET:
	case AUDIT_GET_FEATURE:
	case AUDIT_SET_FEATURE:
	case AUDIT_LIST_RULES:
	case AUDIT_ADD_RULE:
	case AUDIT_DEL_RULE:
	case AUDIT_SIGNAL_INFO:
	case AUDIT_TTY_GET:
	case AUDIT_TTY_SET:
	case AUDIT_TRIM:
	case AUDIT_MAKE_EQUIV:
		/* Only support auditd and auditctl in initial pid namespace
		 * for now. */
		if (task_active_pid_ns(current) != &init_pid_ns)
			return -EPERM;

		if (!netlink_capable(skb, CAP_AUDIT_CONTROL))
			err = -EPERM;
		break;
	case AUDIT_USER:
	case AUDIT_FIRST_USER_MSG ... AUDIT_LAST_USER_MSG:
	case AUDIT_FIRST_USER_MSG2 ... AUDIT_LAST_USER_MSG2:
		if (!netlink_capable(skb, CAP_AUDIT_WRITE))
			err = -EPERM;
		break;
	default:  /* bad msg */
		err = -EINVAL;
	}

	return err;
}

static void audit_log_common_recv_msg(struct audit_context *context,
					struct audit_buffer **ab, u16 msg_type)
{
	uid_t uid = from_kuid(&init_user_ns, current_uid());
	pid_t pid = task_tgid_nr(current);

	if (!audit_enabled && msg_type != AUDIT_USER_AVC) {
		*ab = NULL;
		return;
	}

	*ab = audit_log_start(context, GFP_KERNEL, msg_type);
	if (unlikely(!*ab))
		return;
	audit_log_format(*ab, "pid=%d uid=%u ", pid, uid);
	audit_log_session_info(*ab);
	audit_log_task_context(*ab);
}

static inline void audit_log_user_recv_msg(struct audit_buffer **ab,
					   u16 msg_type)
{
	audit_log_common_recv_msg(NULL, ab, msg_type);
}

int is_audit_feature_set(int i)
{
	return af.features & AUDIT_FEATURE_TO_MASK(i);
}


static int audit_get_feature(struct sk_buff *skb)
{
	u32 seq;

	seq = nlmsg_hdr(skb)->nlmsg_seq;

	audit_send_reply(skb, seq, AUDIT_GET_FEATURE, 0, 0, &af, sizeof(af));

	return 0;
}

static void audit_log_feature_change(int which, u32 old_feature, u32 new_feature,
				     u32 old_lock, u32 new_lock, int res)
{
	struct audit_buffer *ab;

	if (audit_enabled == AUDIT_OFF)
		return;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_FEATURE_CHANGE);
	if (!ab)
		return;
	audit_log_task_info(ab);
	audit_log_format(ab, " feature=%s old=%u new=%u old_lock=%u new_lock=%u res=%d",
			 audit_feature_names[which], !!old_feature, !!new_feature,
			 !!old_lock, !!new_lock, res);
	audit_log_end(ab);
}

static int audit_set_feature(struct audit_features *uaf)
{
	int i;

	BUILD_BUG_ON(AUDIT_LAST_FEATURE + 1 > ARRAY_SIZE(audit_feature_names));

	/* if there is ever a version 2 we should handle that here */

	for (i = 0; i <= AUDIT_LAST_FEATURE; i++) {
		u32 feature = AUDIT_FEATURE_TO_MASK(i);
		u32 old_feature, new_feature, old_lock, new_lock;

		/* if we are not changing this feature, move along */
		if (!(feature & uaf->mask))
			continue;

		old_feature = af.features & feature;
		new_feature = uaf->features & feature;
		new_lock = (uaf->lock | af.lock) & feature;
		old_lock = af.lock & feature;

		/* are we changing a locked feature? */
		if (old_lock && (new_feature != old_feature)) {
			audit_log_feature_change(i, old_feature, new_feature,
						 old_lock, new_lock, 0);
			return -EPERM;
		}
	}
	/* nothing invalid, do the changes */
	for (i = 0; i <= AUDIT_LAST_FEATURE; i++) {
		u32 feature = AUDIT_FEATURE_TO_MASK(i);
		u32 old_feature, new_feature, old_lock, new_lock;

		/* if we are not changing this feature, move along */
		if (!(feature & uaf->mask))
			continue;

		old_feature = af.features & feature;
		new_feature = uaf->features & feature;
		old_lock = af.lock & feature;
		new_lock = (uaf->lock | af.lock) & feature;

		if (new_feature != old_feature)
			audit_log_feature_change(i, old_feature, new_feature,
						 old_lock, new_lock, 1);

		if (new_feature)
			af.features |= feature;
		else
			af.features &= ~feature;
		af.lock |= new_lock;
	}

	return 0;
}

static int audit_replace(struct pid *pid)
{
	pid_t pvnr;
	struct sk_buff *skb;

	pvnr = pid_vnr(pid);
	skb = audit_make_reply(0, AUDIT_REPLACE, 0, 0, &pvnr, sizeof(pvnr));
	if (!skb)
		return -ENOMEM;
	return auditd_send_unicast_skb(skb);
}

static int audit_receive_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	u32			seq;
	void			*data;
	int			data_len;
	int			err;
	struct audit_buffer	*ab;
	u16			msg_type = nlh->nlmsg_type;
	struct audit_sig_info   *sig_data;
	char			*ctx = NULL;
	u32			len;

	err = audit_netlink_ok(skb, msg_type);
	if (err)
		return err;

	seq  = nlh->nlmsg_seq;
	data = nlmsg_data(nlh);
	data_len = nlmsg_len(nlh);

	switch (msg_type) {
	case AUDIT_GET: {
		struct audit_status	s;
		memset(&s, 0, sizeof(s));
		s.enabled		   = audit_enabled;
		s.failure		   = audit_failure;
		/* NOTE: use pid_vnr() so the PID is relative to the current
		 *       namespace */
		s.pid			   = auditd_pid_vnr();
		s.rate_limit		   = audit_rate_limit;
		s.backlog_limit		   = audit_backlog_limit;
		s.lost			   = atomic_read(&audit_lost);
		s.backlog		   = skb_queue_len(&audit_queue);
		s.feature_bitmap	   = AUDIT_FEATURE_BITMAP_ALL;
		s.backlog_wait_time	   = audit_backlog_wait_time;
		s.backlog_wait_time_actual = atomic_read(&audit_backlog_wait_time_actual);
		audit_send_reply(skb, seq, AUDIT_GET, 0, 0, &s, sizeof(s));
		break;
	}
	case AUDIT_SET: {
		struct audit_status	s;
		memset(&s, 0, sizeof(s));
		/* guard against past and future API changes */
		memcpy(&s, data, min_t(size_t, sizeof(s), data_len));
		if (s.mask & AUDIT_STATUS_ENABLED) {
			err = audit_set_enabled(s.enabled);
			if (err < 0)
				return err;
		}
		if (s.mask & AUDIT_STATUS_FAILURE) {
			err = audit_set_failure(s.failure);
			if (err < 0)
				return err;
		}
		if (s.mask & AUDIT_STATUS_PID) {
			/* NOTE: we are using the vnr PID functions below
			 *       because the s.pid value is relative to the
			 *       namespace of the caller; at present this
			 *       doesn't matter much since you can really only
			 *       run auditd from the initial pid namespace, but
			 *       something to keep in mind if this changes */
			pid_t new_pid = s.pid;
			pid_t auditd_pid;
			struct pid *req_pid = task_tgid(current);

			/* Sanity check - PID values must match. Setting
			 * pid to 0 is how auditd ends auditing. */
			if (new_pid && (new_pid != pid_vnr(req_pid)))
				return -EINVAL;

			/* test the auditd connection */
			audit_replace(req_pid);

			auditd_pid = auditd_pid_vnr();
			if (auditd_pid) {
				/* replacing a healthy auditd is not allowed */
				if (new_pid) {
					audit_log_config_change("audit_pid",
							new_pid, auditd_pid, 0);
					return -EEXIST;
				}
				/* only current auditd can unregister itself */
				if (pid_vnr(req_pid) != auditd_pid) {
					audit_log_config_change("audit_pid",
							new_pid, auditd_pid, 0);
					return -EACCES;
				}
			}

			if (new_pid) {
				/* register a new auditd connection */
				err = auditd_set(req_pid,
						 NETLINK_CB(skb).portid,
						 sock_net(NETLINK_CB(skb).sk));
				if (audit_enabled != AUDIT_OFF)
					audit_log_config_change("audit_pid",
								new_pid,
								auditd_pid,
								err ? 0 : 1);
				if (err)
					return err;

				/* try to process any backlog */
				wake_up_interruptible(&kauditd_wait);
			} else {
				if (audit_enabled != AUDIT_OFF)
					audit_log_config_change("audit_pid",
								new_pid,
								auditd_pid, 1);

				/* unregister the auditd connection */
				auditd_reset(NULL);
			}
		}
		if (s.mask & AUDIT_STATUS_RATE_LIMIT) {
			err = audit_set_rate_limit(s.rate_limit);
			if (err < 0)
				return err;
		}
		if (s.mask & AUDIT_STATUS_BACKLOG_LIMIT) {
			err = audit_set_backlog_limit(s.backlog_limit);
			if (err < 0)
				return err;
		}
		if (s.mask & AUDIT_STATUS_BACKLOG_WAIT_TIME) {
			if (sizeof(s) > (size_t)nlh->nlmsg_len)
				return -EINVAL;
			if (s.backlog_wait_time > 10*AUDIT_BACKLOG_WAIT_TIME)
				return -EINVAL;
			err = audit_set_backlog_wait_time(s.backlog_wait_time);
			if (err < 0)
				return err;
		}
		if (s.mask == AUDIT_STATUS_LOST) {
			u32 lost = atomic_xchg(&audit_lost, 0);

			audit_log_config_change("lost", 0, lost, 1);
			return lost;
		}
		if (s.mask == AUDIT_STATUS_BACKLOG_WAIT_TIME_ACTUAL) {
			u32 actual = atomic_xchg(&audit_backlog_wait_time_actual, 0);

			audit_log_config_change("backlog_wait_time_actual", 0, actual, 1);
			return actual;
		}
		break;
	}
	case AUDIT_GET_FEATURE:
		err = audit_get_feature(skb);
		if (err)
			return err;
		break;
	case AUDIT_SET_FEATURE:
		if (data_len < sizeof(struct audit_features))
			return -EINVAL;
		err = audit_set_feature(data);
		if (err)
			return err;
		break;
	case AUDIT_USER:
	case AUDIT_FIRST_USER_MSG ... AUDIT_LAST_USER_MSG:
	case AUDIT_FIRST_USER_MSG2 ... AUDIT_LAST_USER_MSG2:
		if (!audit_enabled && msg_type != AUDIT_USER_AVC)
			return 0;
		/* exit early if there isn't at least one character to print */
		if (data_len < 2)
			return -EINVAL;

		err = audit_filter(msg_type, AUDIT_FILTER_USER);
		if (err == 1) { /* match or error */
			char *str = data;

			err = 0;
			if (msg_type == AUDIT_USER_TTY) {
				err = tty_audit_push();
				if (err)
					break;
			}
			audit_log_user_recv_msg(&ab, msg_type);
			if (msg_type != AUDIT_USER_TTY) {
				/* ensure NULL termination */
				str[data_len - 1] = '\0';
				audit_log_format(ab, " msg='%.*s'",
						 AUDIT_MESSAGE_TEXT_MAX,
						 str);
			} else {
				audit_log_format(ab, " data=");
				if (data_len > 0 && str[data_len - 1] == '\0')
					data_len--;
				audit_log_n_untrustedstring(ab, str, data_len);
			}
			audit_log_end(ab);
		}
		break;
	case AUDIT_ADD_RULE:
	case AUDIT_DEL_RULE:
		if (data_len < sizeof(struct audit_rule_data))
			return -EINVAL;
		if (audit_enabled == AUDIT_LOCKED) {
			audit_log_common_recv_msg(audit_context(), &ab,
						  AUDIT_CONFIG_CHANGE);
			audit_log_format(ab, " op=%s audit_enabled=%d res=0",
					 msg_type == AUDIT_ADD_RULE ?
						"add_rule" : "remove_rule",
					 audit_enabled);
			audit_log_end(ab);
			return -EPERM;
		}
		err = audit_rule_change(msg_type, seq, data, data_len);
		break;
	case AUDIT_LIST_RULES:
		err = audit_list_rules_send(skb, seq);
		break;
	case AUDIT_TRIM:
		audit_trim_trees();
		audit_log_common_recv_msg(audit_context(), &ab,
					  AUDIT_CONFIG_CHANGE);
		audit_log_format(ab, " op=trim res=1");
		audit_log_end(ab);
		break;
	case AUDIT_MAKE_EQUIV: {
		void *bufp = data;
		u32 sizes[2];
		size_t msglen = data_len;
		char *old, *new;

		err = -EINVAL;
		if (msglen < 2 * sizeof(u32))
			break;
		memcpy(sizes, bufp, 2 * sizeof(u32));
		bufp += 2 * sizeof(u32);
		msglen -= 2 * sizeof(u32);
		old = audit_unpack_string(&bufp, &msglen, sizes[0]);
		if (IS_ERR(old)) {
			err = PTR_ERR(old);
			break;
		}
		new = audit_unpack_string(&bufp, &msglen, sizes[1]);
		if (IS_ERR(new)) {
			err = PTR_ERR(new);
			kfree(old);
			break;
		}
		/* OK, here comes... */
		err = audit_tag_tree(old, new);

		audit_log_common_recv_msg(audit_context(), &ab,
					  AUDIT_CONFIG_CHANGE);
		audit_log_format(ab, " op=make_equiv old=");
		audit_log_untrustedstring(ab, old);
		audit_log_format(ab, " new=");
		audit_log_untrustedstring(ab, new);
		audit_log_format(ab, " res=%d", !err);
		audit_log_end(ab);
		kfree(old);
		kfree(new);
		break;
	}
	case AUDIT_SIGNAL_INFO:
		len = 0;
		if (audit_sig_sid) {
			err = security_secid_to_secctx(audit_sig_sid, &ctx, &len);
			if (err)
				return err;
		}
		sig_data = kmalloc(sizeof(*sig_data) + len, GFP_KERNEL);
		if (!sig_data) {
			if (audit_sig_sid)
				security_release_secctx(ctx, len);
			return -ENOMEM;
		}
		sig_data->uid = from_kuid(&init_user_ns, audit_sig_uid);
		sig_data->pid = audit_sig_pid;
		if (audit_sig_sid) {
			memcpy(sig_data->ctx, ctx, len);
			security_release_secctx(ctx, len);
		}
		audit_send_reply(skb, seq, AUDIT_SIGNAL_INFO, 0, 0,
				 sig_data, sizeof(*sig_data) + len);
		kfree(sig_data);
		break;
	case AUDIT_TTY_GET: {
		struct audit_tty_status s;
		unsigned int t;

		t = READ_ONCE(current->signal->audit_tty);
		s.enabled = t & AUDIT_TTY_ENABLE;
		s.log_passwd = !!(t & AUDIT_TTY_LOG_PASSWD);

		audit_send_reply(skb, seq, AUDIT_TTY_GET, 0, 0, &s, sizeof(s));
		break;
	}
	case AUDIT_TTY_SET: {
		struct audit_tty_status s, old;
		struct audit_buffer	*ab;
		unsigned int t;

		memset(&s, 0, sizeof(s));
		/* guard against past and future API changes */
		memcpy(&s, data, min_t(size_t, sizeof(s), data_len));
		/* check if new data is valid */
		if ((s.enabled != 0 && s.enabled != 1) ||
		    (s.log_passwd != 0 && s.log_passwd != 1))
			err = -EINVAL;

		if (err)
			t = READ_ONCE(current->signal->audit_tty);
		else {
			t = s.enabled | (-s.log_passwd & AUDIT_TTY_LOG_PASSWD);
			t = xchg(&current->signal->audit_tty, t);
		}
		old.enabled = t & AUDIT_TTY_ENABLE;
		old.log_passwd = !!(t & AUDIT_TTY_LOG_PASSWD);

		audit_log_common_recv_msg(audit_context(), &ab,
					  AUDIT_CONFIG_CHANGE);
		audit_log_format(ab, " op=tty_set old-enabled=%d new-enabled=%d"
				 " old-log_passwd=%d new-log_passwd=%d res=%d",
				 old.enabled, s.enabled, old.log_passwd,
				 s.log_passwd, !err);
		audit_log_end(ab);
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	return err < 0 ? err : 0;
}

/**
 * audit_receive - receive messages from a netlink control socket
 * @skb: the message buffer
 *
 * Parse the provided skb and deal with any messages that may be present,
 * malformed skbs are discarded.
 */
static void audit_receive(struct sk_buff  *skb)
{
	struct nlmsghdr *nlh;
	/*
	 * len MUST be signed for nlmsg_next to be able to dec it below 0
	 * if the nlmsg_len was not aligned
	 */
	int len;
	int err;

	nlh = nlmsg_hdr(skb);
	len = skb->len;

	audit_ctl_lock();
	while (nlmsg_ok(nlh, len)) {
		err = audit_receive_msg(skb, nlh);
		/* if err or if this message says it wants a response */
		if (err || (nlh->nlmsg_flags & NLM_F_ACK))
			netlink_ack(skb, nlh, err, NULL);

		nlh = nlmsg_next(nlh, &len);
	}
	audit_ctl_unlock();
}

/* Log information about who is connecting to the audit multicast socket */
static void audit_log_multicast(int group, const char *op, int err)
{
	const struct cred *cred;
	struct tty_struct *tty;
	char comm[sizeof(current->comm)];
	struct audit_buffer *ab;

	if (!audit_enabled)
		return;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_EVENT_LISTENER);
	if (!ab)
		return;

	cred = current_cred();
	tty = audit_get_tty();
	audit_log_format(ab, "pid=%u uid=%u auid=%u tty=%s ses=%u",
			 task_pid_nr(current),
			 from_kuid(&init_user_ns, cred->uid),
			 from_kuid(&init_user_ns, audit_get_loginuid(current)),
			 tty ? tty_name(tty) : "(none)",
			 audit_get_sessionid(current));
	audit_put_tty(tty);
	audit_log_task_context(ab); /* subj= */
	audit_log_format(ab, " comm=");
	audit_log_untrustedstring(ab, get_task_comm(comm, current));
	audit_log_d_path_exe(ab, current->mm); /* exe= */
	audit_log_format(ab, " nl-mcgrp=%d op=%s res=%d", group, op, !err);
	audit_log_end(ab);
}

/* Run custom bind function on netlink socket group connect or bind requests. */
static int audit_multicast_bind(struct net *net, int group)
{
	int err = 0;

	if (!capable(CAP_AUDIT_READ))
		err = -EPERM;
	audit_log_multicast(group, "connect", err);
	return err;
}

static void audit_multicast_unbind(struct net *net, int group)
{
	audit_log_multicast(group, "disconnect", 0);
}

static int __net_init audit_net_init(struct net *net)
{
	struct netlink_kernel_cfg cfg = {
		.input	= audit_receive,
		.bind	= audit_multicast_bind,
		.unbind	= audit_multicast_unbind,
		.flags	= NL_CFG_F_NONROOT_RECV,
		.groups	= AUDIT_NLGRP_MAX,
	};

	struct audit_net *aunet = net_generic(net, audit_net_id);

	aunet->sk = netlink_kernel_create(net, NETLINK_AUDIT, &cfg);
	if (aunet->sk == NULL) {
		audit_panic("cannot initialize netlink socket in namespace");
		return -ENOMEM;
	}
	aunet->sk->sk_sndtimeo = MAX_SCHEDULE_TIMEOUT;

	return 0;
}

static void __net_exit audit_net_exit(struct net *net)
{
	struct audit_net *aunet = net_generic(net, audit_net_id);

	/* NOTE: you would think that we would want to check the auditd
	 * connection and potentially reset it here if it lives in this
	 * namespace, but since the auditd connection tracking struct holds a
	 * reference to this namespace (see auditd_set()) we are only ever
	 * going to get here after that connection has been released */

	netlink_kernel_release(aunet->sk);
}

static struct pernet_operations audit_net_ops __net_initdata = {
	.init = audit_net_init,
	.exit = audit_net_exit,
	.id = &audit_net_id,
	.size = sizeof(struct audit_net),
};

/* Initialize audit support at boot time. */
static int __init audit_init(void)
{
	int i;

	if (audit_initialized == AUDIT_DISABLED)
		return 0;

	audit_buffer_cache = kmem_cache_create("audit_buffer",
					       sizeof(struct audit_buffer),
					       0, SLAB_PANIC, NULL);

	skb_queue_head_init(&audit_queue);
	skb_queue_head_init(&audit_retry_queue);
	skb_queue_head_init(&audit_hold_queue);

	for (i = 0; i < AUDIT_INODE_BUCKETS; i++)
		INIT_LIST_HEAD(&audit_inode_hash[i]);

	mutex_init(&audit_cmd_mutex.lock);
	audit_cmd_mutex.owner = NULL;

	pr_info("initializing netlink subsys (%s)\n",
		audit_default ? "enabled" : "disabled");
	register_pernet_subsys(&audit_net_ops);

	audit_initialized = AUDIT_INITIALIZED;

	kauditd_task = kthread_run(kauditd_thread, NULL, "kauditd");
	if (IS_ERR(kauditd_task)) {
		int err = PTR_ERR(kauditd_task);
		panic("audit: failed to start the kauditd thread (%d)\n", err);
	}

	audit_log(NULL, GFP_KERNEL, AUDIT_KERNEL,
		"state=initialized audit_enabled=%u res=1",
		 audit_enabled);

	return 0;
}
postcore_initcall(audit_init);

/*
 * Process kernel command-line parameter at boot time.
 * audit={0|off} or audit={1|on}.
 */
static int __init audit_enable(char *str)
{
	if (!strcasecmp(str, "off") || !strcmp(str, "0"))
		audit_default = AUDIT_OFF;
	else if (!strcasecmp(str, "on") || !strcmp(str, "1"))
		audit_default = AUDIT_ON;
	else {
		pr_err("audit: invalid 'audit' parameter value (%s)\n", str);
		audit_default = AUDIT_ON;
	}

	if (audit_default == AUDIT_OFF)
		audit_initialized = AUDIT_DISABLED;
	if (audit_set_enabled(audit_default))
		pr_err("audit: error setting audit state (%d)\n",
		       audit_default);

	pr_info("%s\n", audit_default ?
		"enabled (after initialization)" : "disabled (until reboot)");

	return 1;
}
__setup("audit=", audit_enable);

/* Process kernel command-line parameter at boot time.
 * audit_backlog_limit=<n> */
static int __init audit_backlog_limit_set(char *str)
{
	u32 audit_backlog_limit_arg;

	pr_info("audit_backlog_limit: ");
	if (kstrtouint(str, 0, &audit_backlog_limit_arg)) {
		pr_cont("using default of %u, unable to parse %s\n",
			audit_backlog_limit, str);
		return 1;
	}

	audit_backlog_limit = audit_backlog_limit_arg;
	pr_cont("%d\n", audit_backlog_limit);

	return 1;
}
__setup("audit_backlog_limit=", audit_backlog_limit_set);

static void audit_buffer_free(struct audit_buffer *ab)
{
	if (!ab)
		return;

	kfree_skb(ab->skb);
	kmem_cache_free(audit_buffer_cache, ab);
}

static struct audit_buffer *audit_buffer_alloc(struct audit_context *ctx,
					       gfp_t gfp_mask, int type)
{
	struct audit_buffer *ab;

	ab = kmem_cache_alloc(audit_buffer_cache, gfp_mask);
	if (!ab)
		return NULL;

	ab->skb = nlmsg_new(AUDIT_BUFSIZ, gfp_mask);
	if (!ab->skb)
		goto err;
	if (!nlmsg_put(ab->skb, 0, 0, type, 0, 0))
		goto err;

	ab->ctx = ctx;
	ab->gfp_mask = gfp_mask;

	return ab;

err:
	audit_buffer_free(ab);
	return NULL;
}

/**
 * audit_serial - compute a serial number for the audit record
 *
 * Compute a serial number for the audit record.  Audit records are
 * written to user-space as soon as they are generated, so a complete
 * audit record may be written in several pieces.  The timestamp of the
 * record and this serial number are used by the user-space tools to
 * determine which pieces belong to the same audit record.  The
 * (timestamp,serial) tuple is unique for each syscall and is live from
 * syscall entry to syscall exit.
 *
 * NOTE: Another possibility is to store the formatted records off the
 * audit context (for those records that have a context), and emit them
 * all at syscall exit.  However, this could delay the reporting of
 * significant errors until syscall exit (or never, if the system
 * halts).
 */
unsigned int audit_serial(void)
{
	static atomic_t serial = ATOMIC_INIT(0);

	return atomic_add_return(1, &serial);
}

static inline void audit_get_stamp(struct audit_context *ctx,
				   struct timespec64 *t, unsigned int *serial)
{
	if (!ctx || !auditsc_get_stamp(ctx, t, serial)) {
		ktime_get_coarse_real_ts64(t);
		*serial = audit_serial();
	}
}

/**
 * audit_log_start - obtain an audit buffer
 * @ctx: audit_context (may be NULL)
 * @gfp_mask: type of allocation
 * @type: audit message type
 *
 * Returns audit_buffer pointer on success or NULL on error.
 *
 * Obtain an audit buffer.  This routine does locking to obtain the
 * audit buffer, but then no locking is required for calls to
 * audit_log_*format.  If the task (ctx) is a task that is currently in a
 * syscall, then the syscall is marked as auditable and an audit record
 * will be written at syscall exit.  If there is no associated task, then
 * task context (ctx) should be NULL.
 */
struct audit_buffer *audit_log_start(struct audit_context *ctx, gfp_t gfp_mask,
				     int type)
{
	struct audit_buffer *ab;
	struct timespec64 t;
	unsigned int serial;

	if (audit_initialized != AUDIT_INITIALIZED)
		return NULL;

	if (unlikely(!audit_filter(type, AUDIT_FILTER_EXCLUDE)))
		return NULL;

	/* NOTE: don't ever fail/sleep on these two conditions:
	 * 1. auditd generated record - since we need auditd to drain the
	 *    queue; also, when we are checking for auditd, compare PIDs using
	 *    task_tgid_vnr() since auditd_pid is set in audit_receive_msg()
	 *    using a PID anchored in the caller's namespace
	 * 2. generator holding the audit_cmd_mutex - we don't want to block
	 *    while holding the mutex */
	if (!(auditd_test_task(current) || audit_ctl_owner_current())) {
		long stime = audit_backlog_wait_time;

		while (audit_backlog_limit &&
		       (skb_queue_len(&audit_queue) > audit_backlog_limit)) {
			/* wake kauditd to try and flush the queue */
			wake_up_interruptible(&kauditd_wait);

			/* sleep if we are allowed and we haven't exhausted our
			 * backlog wait limit */
			if (gfpflags_allow_blocking(gfp_mask) && (stime > 0)) {
				long rtime = stime;

				DECLARE_WAITQUEUE(wait, current);

				add_wait_queue_exclusive(&audit_backlog_wait,
							 &wait);
				set_current_state(TASK_UNINTERRUPTIBLE);
				stime = schedule_timeout(rtime);
				atomic_add(rtime - stime, &audit_backlog_wait_time_actual);
				remove_wait_queue(&audit_backlog_wait, &wait);
			} else {
				if (audit_rate_check() && printk_ratelimit())
					pr_warn("audit_backlog=%d > audit_backlog_limit=%d\n",
						skb_queue_len(&audit_queue),
						audit_backlog_limit);
				audit_log_lost("backlog limit exceeded");
				return NULL;
			}
		}
	}

	ab = audit_buffer_alloc(ctx, gfp_mask, type);
	if (!ab) {
		audit_log_lost("out of memory in audit_log_start");
		return NULL;
	}

	audit_get_stamp(ab->ctx, &t, &serial);
	audit_log_format(ab, "audit(%llu.%03lu:%u): ",
			 (unsigned long long)t.tv_sec, t.tv_nsec/1000000, serial);

	return ab;
}

/**
 * audit_expand - expand skb in the audit buffer
 * @ab: audit_buffer
 * @extra: space to add at tail of the skb
 *
 * Returns 0 (no space) on failed expansion, or available space if
 * successful.
 */
static inline int audit_expand(struct audit_buffer *ab, int extra)
{
	struct sk_buff *skb = ab->skb;
	int oldtail = skb_tailroom(skb);
	int ret = pskb_expand_head(skb, 0, extra, ab->gfp_mask);
	int newtail = skb_tailroom(skb);

	if (ret < 0) {
		audit_log_lost("out of memory in audit_expand");
		return 0;
	}

	skb->truesize += newtail - oldtail;
	return newtail;
}

/*
 * Format an audit message into the audit buffer.  If there isn't enough
 * room in the audit buffer, more room will be allocated and vsnprint
 * will be called a second time.  Currently, we assume that a printk
 * can't format message larger than 1024 bytes, so we don't either.
 */
static void audit_log_vformat(struct audit_buffer *ab, const char *fmt,
			      va_list args)
{
	int len, avail;
	struct sk_buff *skb;
	va_list args2;

	if (!ab)
		return;

	BUG_ON(!ab->skb);
	skb = ab->skb;
	avail = skb_tailroom(skb);
	if (avail == 0) {
		avail = audit_expand(ab, AUDIT_BUFSIZ);
		if (!avail)
			goto out;
	}
	va_copy(args2, args);
	len = vsnprintf(skb_tail_pointer(skb), avail, fmt, args);
	if (len >= avail) {
		/* The printk buffer is 1024 bytes long, so if we get
		 * here and AUDIT_BUFSIZ is at least 1024, then we can
		 * log everything that printk could have logged. */
		avail = audit_expand(ab,
			max_t(unsigned, AUDIT_BUFSIZ, 1+len-avail));
		if (!avail)
			goto out_va_end;
		len = vsnprintf(skb_tail_pointer(skb), avail, fmt, args2);
	}
	if (len > 0)
		skb_put(skb, len);
out_va_end:
	va_end(args2);
out:
	return;
}

/**
 * audit_log_format - format a message into the audit buffer.
 * @ab: audit_buffer
 * @fmt: format string
 * @...: optional parameters matching @fmt string
 *
 * All the work is done in audit_log_vformat.
 */
void audit_log_format(struct audit_buffer *ab, const char *fmt, ...)
{
	va_list args;

	if (!ab)
		return;
	va_start(args, fmt);
	audit_log_vformat(ab, fmt, args);
	va_end(args);
}

/**
 * audit_log_n_hex - convert a buffer to hex and append it to the audit skb
 * @ab: the audit_buffer
 * @buf: buffer to convert to hex
 * @len: length of @buf to be converted
 *
 * No return value; failure to expand is silently ignored.
 *
 * This function will take the passed buf and convert it into a string of
 * ascii hex digits. The new string is placed onto the skb.
 */
void audit_log_n_hex(struct audit_buffer *ab, const unsigned char *buf,
		size_t len)
{
	int i, avail, new_len;
	unsigned char *ptr;
	struct sk_buff *skb;

	if (!ab)
		return;

	BUG_ON(!ab->skb);
	skb = ab->skb;
	avail = skb_tailroom(skb);
	new_len = len<<1;
	if (new_len >= avail) {
		/* Round the buffer request up to the next multiple */
		new_len = AUDIT_BUFSIZ*(((new_len-avail)/AUDIT_BUFSIZ) + 1);
		avail = audit_expand(ab, new_len);
		if (!avail)
			return;
	}

	ptr = skb_tail_pointer(skb);
	for (i = 0; i < len; i++)
		ptr = hex_byte_pack_upper(ptr, buf[i]);
	*ptr = 0;
	skb_put(skb, len << 1); /* new string is twice the old string */
}

/*
 * Format a string of no more than slen characters into the audit buffer,
 * enclosed in quote marks.
 */
void audit_log_n_string(struct audit_buffer *ab, const char *string,
			size_t slen)
{
	int avail, new_len;
	unsigned char *ptr;
	struct sk_buff *skb;

	if (!ab)
		return;

	BUG_ON(!ab->skb);
	skb = ab->skb;
	avail = skb_tailroom(skb);
	new_len = slen + 3;	/* enclosing quotes + null terminator */
	if (new_len > avail) {
		avail = audit_expand(ab, new_len);
		if (!avail)
			return;
	}
	ptr = skb_tail_pointer(skb);
	*ptr++ = '"';
	memcpy(ptr, string, slen);
	ptr += slen;
	*ptr++ = '"';
	*ptr = 0;
	skb_put(skb, slen + 2);	/* don't include null terminator */
}

/**
 * audit_string_contains_control - does a string need to be logged in hex
 * @string: string to be checked
 * @len: max length of the string to check
 */
bool audit_string_contains_control(const char *string, size_t len)
{
	const unsigned char *p;
	for (p = string; p < (const unsigned char *)string + len; p++) {
		if (*p == '"' || *p < 0x21 || *p > 0x7e)
			return true;
	}
	return false;
}

/**
 * audit_log_n_untrustedstring - log a string that may contain random characters
 * @ab: audit_buffer
 * @len: length of string (not including trailing null)
 * @string: string to be logged
 *
 * This code will escape a string that is passed to it if the string
 * contains a control character, unprintable character, double quote mark,
 * or a space. Unescaped strings will start and end with a double quote mark.
 * Strings that are escaped are printed in hex (2 digits per char).
 *
 * The caller specifies the number of characters in the string to log, which may
 * or may not be the entire string.
 */
void audit_log_n_untrustedstring(struct audit_buffer *ab, const char *string,
				 size_t len)
{
	if (audit_string_contains_control(string, len))
		audit_log_n_hex(ab, string, len);
	else
		audit_log_n_string(ab, string, len);
}

/**
 * audit_log_untrustedstring - log a string that may contain random characters
 * @ab: audit_buffer
 * @string: string to be logged
 *
 * Same as audit_log_n_untrustedstring(), except that strlen is used to
 * determine string length.
 */
void audit_log_untrustedstring(struct audit_buffer *ab, const char *string)
{
	audit_log_n_untrustedstring(ab, string, strlen(string));
}

/* This is a helper-function to print the escaped d_path */
void audit_log_d_path(struct audit_buffer *ab, const char *prefix,
		      const struct path *path)
{
	char *p, *pathname;

	if (prefix)
		audit_log_format(ab, "%s", prefix);

	/* We will allow 11 spaces for ' (deleted)' to be appended */
	pathname = kmalloc(PATH_MAX+11, ab->gfp_mask);
	if (!pathname) {
		audit_log_format(ab, "\"<no_memory>\"");
		return;
	}
	p = d_path(path, pathname, PATH_MAX+11);
	if (IS_ERR(p)) { /* Should never happen since we send PATH_MAX */
		/* FIXME: can we save some information here? */
		audit_log_format(ab, "\"<too_long>\"");
	} else
		audit_log_untrustedstring(ab, p);
	kfree(pathname);
}

void audit_log_session_info(struct audit_buffer *ab)
{
	unsigned int sessionid = audit_get_sessionid(current);
	uid_t auid = from_kuid(&init_user_ns, audit_get_loginuid(current));

	audit_log_format(ab, "auid=%u ses=%u", auid, sessionid);
}

void audit_log_key(struct audit_buffer *ab, char *key)
{
	audit_log_format(ab, " key=");
	if (key)
		audit_log_untrustedstring(ab, key);
	else
		audit_log_format(ab, "(null)");
}

int audit_log_task_context(struct audit_buffer *ab)
{
	char *ctx = NULL;
	unsigned len;
	int error;
	u32 sid;

	security_task_getsecid(current, &sid);
	if (!sid)
		return 0;

	error = security_secid_to_secctx(sid, &ctx, &len);
	if (error) {
		if (error != -EINVAL)
			goto error_path;
		return 0;
	}

	audit_log_format(ab, " subj=%s", ctx);
	security_release_secctx(ctx, len);
	return 0;

error_path:
	audit_panic("error in audit_log_task_context");
	return error;
}
EXPORT_SYMBOL(audit_log_task_context);

void audit_log_d_path_exe(struct audit_buffer *ab,
			  struct mm_struct *mm)
{
	struct file *exe_file;

	if (!mm)
		goto out_null;

	exe_file = get_mm_exe_file(mm);
	if (!exe_file)
		goto out_null;

	audit_log_d_path(ab, " exe=", &exe_file->f_path);
	fput(exe_file);
	return;
out_null:
	audit_log_format(ab, " exe=(null)");
}

struct tty_struct *audit_get_tty(void)
{
	struct tty_struct *tty = NULL;
	unsigned long flags;

	spin_lock_irqsave(&current->sighand->siglock, flags);
	if (current->signal)
		tty = tty_kref_get(current->signal->tty);
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
	return tty;
}

void audit_put_tty(struct tty_struct *tty)
{
	tty_kref_put(tty);
}

void audit_log_task_info(struct audit_buffer *ab)
{
	const struct cred *cred;
	char comm[sizeof(current->comm)];
	struct tty_struct *tty;

	if (!ab)
		return;

	cred = current_cred();
	tty = audit_get_tty();
	audit_log_format(ab,
			 " ppid=%d pid=%d auid=%u uid=%u gid=%u"
			 " euid=%u suid=%u fsuid=%u"
			 " egid=%u sgid=%u fsgid=%u tty=%s ses=%u",
			 task_ppid_nr(current),
			 task_tgid_nr(current),
			 from_kuid(&init_user_ns, audit_get_loginuid(current)),
			 from_kuid(&init_user_ns, cred->uid),
			 from_kgid(&init_user_ns, cred->gid),
			 from_kuid(&init_user_ns, cred->euid),
			 from_kuid(&init_user_ns, cred->suid),
			 from_kuid(&init_user_ns, cred->fsuid),
			 from_kgid(&init_user_ns, cred->egid),
			 from_kgid(&init_user_ns, cred->sgid),
			 from_kgid(&init_user_ns, cred->fsgid),
			 tty ? tty_name(tty) : "(none)",
			 audit_get_sessionid(current));
	audit_put_tty(tty);
	audit_log_format(ab, " comm=");
	audit_log_untrustedstring(ab, get_task_comm(comm, current));
	audit_log_d_path_exe(ab, current->mm);
	audit_log_task_context(ab);
}
EXPORT_SYMBOL(audit_log_task_info);

/**
 * audit_log_path_denied - report a path restriction denial
 * @type: audit message type (AUDIT_ANOM_LINK, AUDIT_ANOM_CREAT, etc)
 * @operation: specific operation name
 */
void audit_log_path_denied(int type, const char *operation)
{
	struct audit_buffer *ab;

	if (!audit_enabled || audit_dummy_context())
		return;

	/* Generate log with subject, operation, outcome. */
	ab = audit_log_start(audit_context(), GFP_KERNEL, type);
	if (!ab)
		return;
	audit_log_format(ab, "op=%s", operation);
	audit_log_task_info(ab);
	audit_log_format(ab, " res=0");
	audit_log_end(ab);
}

/* global counter which is incremented every time something logs in */
static atomic_t session_id = ATOMIC_INIT(0);

static int audit_set_loginuid_perm(kuid_t loginuid)
{
	/* if we are unset, we don't need privs */
	if (!audit_loginuid_set(current))
		return 0;
	/* if AUDIT_FEATURE_LOGINUID_IMMUTABLE means never ever allow a change*/
	if (is_audit_feature_set(AUDIT_FEATURE_LOGINUID_IMMUTABLE))
		return -EPERM;
	/* it is set, you need permission */
	if (!capable(CAP_AUDIT_CONTROL))
		return -EPERM;
	/* reject if this is not an unset and we don't allow that */
	if (is_audit_feature_set(AUDIT_FEATURE_ONLY_UNSET_LOGINUID)
				 && uid_valid(loginuid))
		return -EPERM;
	return 0;
}

static void audit_log_set_loginuid(kuid_t koldloginuid, kuid_t kloginuid,
				   unsigned int oldsessionid,
				   unsigned int sessionid, int rc)
{
	struct audit_buffer *ab;
	uid_t uid, oldloginuid, loginuid;
	struct tty_struct *tty;

	if (!audit_enabled)
		return;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_LOGIN);
	if (!ab)
		return;

	uid = from_kuid(&init_user_ns, task_uid(current));
	oldloginuid = from_kuid(&init_user_ns, koldloginuid);
	loginuid = from_kuid(&init_user_ns, kloginuid),
	tty = audit_get_tty();

	audit_log_format(ab, "pid=%d uid=%u", task_tgid_nr(current), uid);
	audit_log_task_context(ab);
	audit_log_format(ab, " old-auid=%u auid=%u tty=%s old-ses=%u ses=%u res=%d",
			 oldloginuid, loginuid, tty ? tty_name(tty) : "(none)",
			 oldsessionid, sessionid, !rc);
	audit_put_tty(tty);
	audit_log_end(ab);
}

/**
 * audit_set_loginuid - set current task's loginuid
 * @loginuid: loginuid value
 *
 * Returns 0.
 *
 * Called (set) from fs/proc/base.c::proc_loginuid_write().
 */
int audit_set_loginuid(kuid_t loginuid)
{
	unsigned int oldsessionid, sessionid = AUDIT_SID_UNSET;
	kuid_t oldloginuid;
	int rc;

	oldloginuid = audit_get_loginuid(current);
	oldsessionid = audit_get_sessionid(current);

	rc = audit_set_loginuid_perm(loginuid);
	if (rc)
		goto out;

	/* are we setting or clearing? */
	if (uid_valid(loginuid)) {
		sessionid = (unsigned int)atomic_inc_return(&session_id);
		if (unlikely(sessionid == AUDIT_SID_UNSET))
			sessionid = (unsigned int)atomic_inc_return(&session_id);
	}

	current->sessionid = sessionid;
	current->loginuid = loginuid;
out:
	audit_log_set_loginuid(oldloginuid, loginuid, oldsessionid, sessionid, rc);
	return rc;
}

/**
 * audit_signal_info - record signal info for shutting down audit subsystem
 * @sig: signal value
 * @t: task being signaled
 *
 * If the audit subsystem is being terminated, record the task (pid)
 * and uid that is doing that.
 */
int audit_signal_info(int sig, struct task_struct *t)
{
	kuid_t uid = current_uid(), auid;

	if (auditd_test_task(t) &&
	    (sig == SIGTERM || sig == SIGHUP ||
	     sig == SIGUSR1 || sig == SIGUSR2)) {
		audit_sig_pid = task_tgid_nr(current);
		auid = audit_get_loginuid(current);
		if (uid_valid(auid))
			audit_sig_uid = auid;
		else
			audit_sig_uid = uid;
		security_task_getsecid(current, &audit_sig_sid);
	}

	return audit_signal_info_syscall(t);
}

/**
 * audit_log_end - end one audit record
 * @ab: the audit_buffer
 *
 * We can not do a netlink send inside an irq context because it blocks (last
 * arg, flags, is not set to MSG_DONTWAIT), so the audit buffer is placed on a
 * queue and a tasklet is scheduled to remove them from the queue outside the
 * irq context.  May be called in any context.
 */
void audit_log_end(struct audit_buffer *ab)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;

	if (!ab)
		return;

	if (audit_rate_check()) {
		skb = ab->skb;
		ab->skb = NULL;

		/* setup the netlink header, see the comments in
		 * kauditd_send_multicast_skb() for length quirks */
		nlh = nlmsg_hdr(skb);
		nlh->nlmsg_len = skb->len - NLMSG_HDRLEN;

		/* queue the netlink packet and poke the kauditd thread */
		skb_queue_tail(&audit_queue, skb);
		wake_up_interruptible(&kauditd_wait);
	} else
		audit_log_lost("rate limit exceeded");

	audit_buffer_free(ab);
}

/**
 * audit_log - Log an audit record
 * @ctx: audit context
 * @gfp_mask: type of allocation
 * @type: audit message type
 * @fmt: format string to use
 * @...: variable parameters matching the format string
 *
 * This is a convenience function that calls audit_log_start,
 * audit_log_vformat, and audit_log_end.  It may be called
 * in any context.
 */
void audit_log(struct audit_context *ctx, gfp_t gfp_mask, int type,
	       const char *fmt, ...)
{
	struct audit_buffer *ab;
	va_list args;

	ab = audit_log_start(ctx, gfp_mask, type);
	if (ab) {
		va_start(args, fmt);
		audit_log_vformat(ab, fmt, args);
		va_end(args);
		audit_log_end(ab);
	}
}

EXPORT_SYMBOL(audit_log_start);
EXPORT_SYMBOL(audit_log_end);
EXPORT_SYMBOL(audit_log_format);
EXPORT_SYMBOL(audit_log);
