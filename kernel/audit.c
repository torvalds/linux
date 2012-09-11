/* audit.c -- Auditing support
 * Gateway between the kernel (e.g., selinux) and the user-space audit daemon.
 * System-call specific features have moved to auditsc.c
 *
 * Copyright 2003-2007 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
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
 * Example user-space utilities: http://people.redhat.com/sgrubb/audit/
 */

#include <linux/init.h>
#include <asm/types.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kthread.h>

#include <linux/audit.h>

#include <net/sock.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#ifdef CONFIG_SECURITY
#include <linux/security.h>
#endif
#include <linux/netlink.h>
#include <linux/freezer.h>
#include <linux/tty.h>
#include <linux/pid_namespace.h>

#include "audit.h"

/* No auditing will take place until audit_initialized == AUDIT_INITIALIZED.
 * (Initialization happens after skb_init is called.) */
#define AUDIT_DISABLED		-1
#define AUDIT_UNINITIALIZED	0
#define AUDIT_INITIALIZED	1
static int	audit_initialized;

#define AUDIT_OFF	0
#define AUDIT_ON	1
#define AUDIT_LOCKED	2
int		audit_enabled;
int		audit_ever_enabled;

EXPORT_SYMBOL_GPL(audit_enabled);

/* Default state when kernel boots without any parameters. */
static int	audit_default;

/* If auditing cannot proceed, audit_failure selects what happens. */
static int	audit_failure = AUDIT_FAIL_PRINTK;

/*
 * If audit records are to be written to the netlink socket, audit_pid
 * contains the pid of the auditd process and audit_nlk_pid contains
 * the pid to use to send netlink messages to that process.
 */
int		audit_pid;
static int	audit_nlk_pid;

/* If audit_rate_limit is non-zero, limit the rate of sending audit records
 * to that number per second.  This prevents DoS attacks, but results in
 * audit records being dropped. */
static int	audit_rate_limit;

/* Number of outstanding audit_buffers allowed. */
static int	audit_backlog_limit = 64;
static int	audit_backlog_wait_time = 60 * HZ;
static int	audit_backlog_wait_overflow = 0;

/* The identity of the user shutting down the audit system. */
uid_t		audit_sig_uid = -1;
pid_t		audit_sig_pid = -1;
u32		audit_sig_sid = 0;

/* Records can be lost in several ways:
   0) [suppressed in audit_alloc]
   1) out of memory in audit_log_start [kmalloc of struct audit_buffer]
   2) out of memory in audit_log_move [alloc_skb]
   3) suppressed due to audit_rate_limit
   4) suppressed due to audit_backlog_limit
*/
static atomic_t    audit_lost = ATOMIC_INIT(0);

/* The netlink socket. */
static struct sock *audit_sock;

/* Hash for inode-based rules */
struct list_head audit_inode_hash[AUDIT_INODE_BUCKETS];

/* The audit_freelist is a list of pre-allocated audit buffers (if more
 * than AUDIT_MAXFREE are in use, the audit buffer is freed instead of
 * being placed on the freelist). */
static DEFINE_SPINLOCK(audit_freelist_lock);
static int	   audit_freelist_count;
static LIST_HEAD(audit_freelist);

static struct sk_buff_head audit_skb_queue;
/* queue of skbs to send to auditd when/if it comes back */
static struct sk_buff_head audit_skb_hold_queue;
static struct task_struct *kauditd_task;
static DECLARE_WAIT_QUEUE_HEAD(kauditd_wait);
static DECLARE_WAIT_QUEUE_HEAD(audit_backlog_wait);

/* Serialize requests from userspace. */
DEFINE_MUTEX(audit_cmd_mutex);

/* AUDIT_BUFSIZ is the size of the temporary buffer used for formatting
 * audit records.  Since printk uses a 1024 byte buffer, this buffer
 * should be at least that large. */
#define AUDIT_BUFSIZ 1024

/* AUDIT_MAXFREE is the number of empty audit_buffers we keep on the
 * audit_freelist.  Doing so eliminates many kmalloc/kfree calls. */
#define AUDIT_MAXFREE  (2*NR_CPUS)

/* The audit_buffer is used when formatting an audit record.  The caller
 * locks briefly to get the record off the freelist or to allocate the
 * buffer, and locks briefly to send the buffer to the netlink layer or
 * to place it on a transmit queue.  Multiple audit_buffers can be in
 * use simultaneously. */
struct audit_buffer {
	struct list_head     list;
	struct sk_buff       *skb;	/* formatted skb ready to send */
	struct audit_context *ctx;	/* NULL or associated context */
	gfp_t		     gfp_mask;
};

struct audit_reply {
	int pid;
	struct sk_buff *skb;
};

static void audit_set_pid(struct audit_buffer *ab, pid_t pid)
{
	if (ab) {
		struct nlmsghdr *nlh = nlmsg_hdr(ab->skb);
		nlh->nlmsg_pid = pid;
	}
}

void audit_panic(const char *message)
{
	switch (audit_failure)
	{
	case AUDIT_FAIL_SILENT:
		break;
	case AUDIT_FAIL_PRINTK:
		if (printk_ratelimit())
			printk(KERN_ERR "audit: %s\n", message);
		break;
	case AUDIT_FAIL_PANIC:
		/* test audit_pid since printk is always losey, why bother? */
		if (audit_pid)
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
			printk(KERN_WARNING
				"audit: audit_lost=%d audit_rate_limit=%d "
				"audit_backlog_limit=%d\n",
				atomic_read(&audit_lost),
				audit_rate_limit,
				audit_backlog_limit);
		audit_panic(message);
	}
}

static int audit_log_config_change(char *function_name, int new, int old,
				   uid_t loginuid, u32 sessionid, u32 sid,
				   int allow_changes)
{
	struct audit_buffer *ab;
	int rc = 0;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE);
	audit_log_format(ab, "%s=%d old=%d auid=%u ses=%u", function_name, new,
			 old, loginuid, sessionid);
	if (sid) {
		char *ctx = NULL;
		u32 len;

		rc = security_secid_to_secctx(sid, &ctx, &len);
		if (rc) {
			audit_log_format(ab, " sid=%u", sid);
			allow_changes = 0; /* Something weird, deny request */
		} else {
			audit_log_format(ab, " subj=%s", ctx);
			security_release_secctx(ctx, len);
		}
	}
	audit_log_format(ab, " res=%d", allow_changes);
	audit_log_end(ab);
	return rc;
}

static int audit_do_config_change(char *function_name, int *to_change,
				  int new, uid_t loginuid, u32 sessionid,
				  u32 sid)
{
	int allow_changes, rc = 0, old = *to_change;

	/* check if we are locked */
	if (audit_enabled == AUDIT_LOCKED)
		allow_changes = 0;
	else
		allow_changes = 1;

	if (audit_enabled != AUDIT_OFF) {
		rc = audit_log_config_change(function_name, new, old, loginuid,
					     sessionid, sid, allow_changes);
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

static int audit_set_rate_limit(int limit, uid_t loginuid, u32 sessionid,
				u32 sid)
{
	return audit_do_config_change("audit_rate_limit", &audit_rate_limit,
				      limit, loginuid, sessionid, sid);
}

static int audit_set_backlog_limit(int limit, uid_t loginuid, u32 sessionid,
				   u32 sid)
{
	return audit_do_config_change("audit_backlog_limit", &audit_backlog_limit,
				      limit, loginuid, sessionid, sid);
}

static int audit_set_enabled(int state, uid_t loginuid, u32 sessionid, u32 sid)
{
	int rc;
	if (state < AUDIT_OFF || state > AUDIT_LOCKED)
		return -EINVAL;

	rc =  audit_do_config_change("audit_enabled", &audit_enabled, state,
				     loginuid, sessionid, sid);

	if (!rc)
		audit_ever_enabled |= !!state;

	return rc;
}

static int audit_set_failure(int state, uid_t loginuid, u32 sessionid, u32 sid)
{
	if (state != AUDIT_FAIL_SILENT
	    && state != AUDIT_FAIL_PRINTK
	    && state != AUDIT_FAIL_PANIC)
		return -EINVAL;

	return audit_do_config_change("audit_failure", &audit_failure, state,
				      loginuid, sessionid, sid);
}

/*
 * Queue skbs to be sent to auditd when/if it comes back.  These skbs should
 * already have been sent via prink/syslog and so if these messages are dropped
 * it is not a huge concern since we already passed the audit_log_lost()
 * notification and stuff.  This is just nice to get audit messages during
 * boot before auditd is running or messages generated while auditd is stopped.
 * This only holds messages is audit_default is set, aka booting with audit=1
 * or building your kernel that way.
 */
static void audit_hold_skb(struct sk_buff *skb)
{
	if (audit_default &&
	    skb_queue_len(&audit_skb_hold_queue) < audit_backlog_limit)
		skb_queue_tail(&audit_skb_hold_queue, skb);
	else
		kfree_skb(skb);
}

/*
 * For one reason or another this nlh isn't getting delivered to the userspace
 * audit daemon, just send it to printk.
 */
static void audit_printk_skb(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	char *data = nlmsg_data(nlh);

	if (nlh->nlmsg_type != AUDIT_EOE) {
		if (printk_ratelimit())
			printk(KERN_NOTICE "type=%d %s\n", nlh->nlmsg_type, data);
		else
			audit_log_lost("printk limit exceeded\n");
	}

	audit_hold_skb(skb);
}

static void kauditd_send_skb(struct sk_buff *skb)
{
	int err;
	/* take a reference in case we can't send it and we want to hold it */
	skb_get(skb);
	err = netlink_unicast(audit_sock, skb, audit_nlk_pid, 0);
	if (err < 0) {
		BUG_ON(err != -ECONNREFUSED); /* Shouldn't happen */
		printk(KERN_ERR "audit: *NO* daemon at audit_pid=%d\n", audit_pid);
		audit_log_lost("auditd disappeared\n");
		audit_pid = 0;
		/* we might get lucky and get this in the next auditd */
		audit_hold_skb(skb);
	} else
		/* drop the extra reference if sent ok */
		consume_skb(skb);
}

static int kauditd_thread(void *dummy)
{
	struct sk_buff *skb;

	set_freezable();
	while (!kthread_should_stop()) {
		/*
		 * if auditd just started drain the queue of messages already
		 * sent to syslog/printk.  remember loss here is ok.  we already
		 * called audit_log_lost() if it didn't go out normally.  so the
		 * race between the skb_dequeue and the next check for audit_pid
		 * doesn't matter.
		 *
		 * if you ever find kauditd to be too slow we can get a perf win
		 * by doing our own locking and keeping better track if there
		 * are messages in this queue.  I don't see the need now, but
		 * in 5 years when I want to play with this again I'll see this
		 * note and still have no friggin idea what i'm thinking today.
		 */
		if (audit_default && audit_pid) {
			skb = skb_dequeue(&audit_skb_hold_queue);
			if (unlikely(skb)) {
				while (skb && audit_pid) {
					kauditd_send_skb(skb);
					skb = skb_dequeue(&audit_skb_hold_queue);
				}
			}
		}

		skb = skb_dequeue(&audit_skb_queue);
		wake_up(&audit_backlog_wait);
		if (skb) {
			if (audit_pid)
				kauditd_send_skb(skb);
			else
				audit_printk_skb(skb);
		} else {
			DECLARE_WAITQUEUE(wait, current);
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&kauditd_wait, &wait);

			if (!skb_queue_len(&audit_skb_queue)) {
				try_to_freeze();
				schedule();
			}

			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&kauditd_wait, &wait);
		}
	}
	return 0;
}

static int audit_prepare_user_tty(pid_t pid, uid_t loginuid, u32 sessionid)
{
	struct task_struct *tsk;
	int err;

	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	if (!tsk) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(tsk);
	rcu_read_unlock();
	err = tty_audit_push_task(tsk, loginuid, sessionid);
	put_task_struct(tsk);
	return err;
}

int audit_send_list(void *_dest)
{
	struct audit_netlink_list *dest = _dest;
	int pid = dest->pid;
	struct sk_buff *skb;

	/* wait for parent to finish and send an ACK */
	mutex_lock(&audit_cmd_mutex);
	mutex_unlock(&audit_cmd_mutex);

	while ((skb = __skb_dequeue(&dest->q)) != NULL)
		netlink_unicast(audit_sock, skb, pid, 0);

	kfree(dest);

	return 0;
}

struct sk_buff *audit_make_reply(int pid, int seq, int type, int done,
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

	nlh	= nlmsg_put(skb, pid, seq, t, size, flags);
	if (!nlh)
		goto out_kfree_skb;
	data = nlmsg_data(nlh);
	memcpy(data, payload, size);
	return skb;

out_kfree_skb:
	kfree_skb(skb);
	return NULL;
}

static int audit_send_reply_thread(void *arg)
{
	struct audit_reply *reply = (struct audit_reply *)arg;

	mutex_lock(&audit_cmd_mutex);
	mutex_unlock(&audit_cmd_mutex);

	/* Ignore failure. It'll only happen if the sender goes away,
	   because our timeout is set to infinite. */
	netlink_unicast(audit_sock, reply->skb, reply->pid, 0);
	kfree(reply);
	return 0;
}
/**
 * audit_send_reply - send an audit reply message via netlink
 * @pid: process id to send reply to
 * @seq: sequence number
 * @type: audit message type
 * @done: done (last) flag
 * @multi: multi-part message flag
 * @payload: payload data
 * @size: payload size
 *
 * Allocates an skb, builds the netlink message, and sends it to the pid.
 * No failure notifications.
 */
static void audit_send_reply(int pid, int seq, int type, int done, int multi,
			     const void *payload, int size)
{
	struct sk_buff *skb;
	struct task_struct *tsk;
	struct audit_reply *reply = kmalloc(sizeof(struct audit_reply),
					    GFP_KERNEL);

	if (!reply)
		return;

	skb = audit_make_reply(pid, seq, type, done, multi, payload, size);
	if (!skb)
		goto out;

	reply->pid = pid;
	reply->skb = skb;

	tsk = kthread_run(audit_send_reply_thread, reply, "audit_send_reply");
	if (!IS_ERR(tsk))
		return;
	kfree_skb(skb);
out:
	kfree(reply);
}

/*
 * Check for appropriate CAP_AUDIT_ capabilities on incoming audit
 * control messages.
 */
static int audit_netlink_ok(struct sk_buff *skb, u16 msg_type)
{
	int err = 0;

	/* Only support the initial namespaces for now. */
	if ((current_user_ns() != &init_user_ns) ||
	    (task_active_pid_ns(current) != &init_pid_ns))
		return -EPERM;

	switch (msg_type) {
	case AUDIT_GET:
	case AUDIT_LIST:
	case AUDIT_LIST_RULES:
	case AUDIT_SET:
	case AUDIT_ADD:
	case AUDIT_ADD_RULE:
	case AUDIT_DEL:
	case AUDIT_DEL_RULE:
	case AUDIT_SIGNAL_INFO:
	case AUDIT_TTY_GET:
	case AUDIT_TTY_SET:
	case AUDIT_TRIM:
	case AUDIT_MAKE_EQUIV:
		if (!capable(CAP_AUDIT_CONTROL))
			err = -EPERM;
		break;
	case AUDIT_USER:
	case AUDIT_FIRST_USER_MSG ... AUDIT_LAST_USER_MSG:
	case AUDIT_FIRST_USER_MSG2 ... AUDIT_LAST_USER_MSG2:
		if (!capable(CAP_AUDIT_WRITE))
			err = -EPERM;
		break;
	default:  /* bad msg */
		err = -EINVAL;
	}

	return err;
}

static int audit_log_common_recv_msg(struct audit_buffer **ab, u16 msg_type,
				     u32 pid, u32 uid, uid_t auid, u32 ses,
				     u32 sid)
{
	int rc = 0;
	char *ctx = NULL;
	u32 len;

	if (!audit_enabled) {
		*ab = NULL;
		return rc;
	}

	*ab = audit_log_start(NULL, GFP_KERNEL, msg_type);
	audit_log_format(*ab, "pid=%d uid=%u auid=%u ses=%u",
			 pid, uid, auid, ses);
	if (sid) {
		rc = security_secid_to_secctx(sid, &ctx, &len);
		if (rc)
			audit_log_format(*ab, " ssid=%u", sid);
		else {
			audit_log_format(*ab, " subj=%s", ctx);
			security_release_secctx(ctx, len);
		}
	}

	return rc;
}

static int audit_receive_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	u32			uid, pid, seq, sid;
	void			*data;
	struct audit_status	*status_get, status_set;
	int			err;
	struct audit_buffer	*ab;
	u16			msg_type = nlh->nlmsg_type;
	uid_t			loginuid; /* loginuid of sender */
	u32			sessionid;
	struct audit_sig_info   *sig_data;
	char			*ctx = NULL;
	u32			len;

	err = audit_netlink_ok(skb, msg_type);
	if (err)
		return err;

	/* As soon as there's any sign of userspace auditd,
	 * start kauditd to talk to it */
	if (!kauditd_task)
		kauditd_task = kthread_run(kauditd_thread, NULL, "kauditd");
	if (IS_ERR(kauditd_task)) {
		err = PTR_ERR(kauditd_task);
		kauditd_task = NULL;
		return err;
	}

	pid  = NETLINK_CREDS(skb)->pid;
	uid  = NETLINK_CREDS(skb)->uid;
	loginuid = audit_get_loginuid(current);
	sessionid = audit_get_sessionid(current);
	security_task_getsecid(current, &sid);
	seq  = nlh->nlmsg_seq;
	data = nlmsg_data(nlh);

	switch (msg_type) {
	case AUDIT_GET:
		status_set.enabled	 = audit_enabled;
		status_set.failure	 = audit_failure;
		status_set.pid		 = audit_pid;
		status_set.rate_limit	 = audit_rate_limit;
		status_set.backlog_limit = audit_backlog_limit;
		status_set.lost		 = atomic_read(&audit_lost);
		status_set.backlog	 = skb_queue_len(&audit_skb_queue);
		audit_send_reply(NETLINK_CB(skb).pid, seq, AUDIT_GET, 0, 0,
				 &status_set, sizeof(status_set));
		break;
	case AUDIT_SET:
		if (nlh->nlmsg_len < sizeof(struct audit_status))
			return -EINVAL;
		status_get   = (struct audit_status *)data;
		if (status_get->mask & AUDIT_STATUS_ENABLED) {
			err = audit_set_enabled(status_get->enabled,
						loginuid, sessionid, sid);
			if (err < 0)
				return err;
		}
		if (status_get->mask & AUDIT_STATUS_FAILURE) {
			err = audit_set_failure(status_get->failure,
						loginuid, sessionid, sid);
			if (err < 0)
				return err;
		}
		if (status_get->mask & AUDIT_STATUS_PID) {
			int new_pid = status_get->pid;

			if (audit_enabled != AUDIT_OFF)
				audit_log_config_change("audit_pid", new_pid,
							audit_pid, loginuid,
							sessionid, sid, 1);

			audit_pid = new_pid;
			audit_nlk_pid = NETLINK_CB(skb).pid;
		}
		if (status_get->mask & AUDIT_STATUS_RATE_LIMIT) {
			err = audit_set_rate_limit(status_get->rate_limit,
						   loginuid, sessionid, sid);
			if (err < 0)
				return err;
		}
		if (status_get->mask & AUDIT_STATUS_BACKLOG_LIMIT)
			err = audit_set_backlog_limit(status_get->backlog_limit,
						      loginuid, sessionid, sid);
		break;
	case AUDIT_USER:
	case AUDIT_FIRST_USER_MSG ... AUDIT_LAST_USER_MSG:
	case AUDIT_FIRST_USER_MSG2 ... AUDIT_LAST_USER_MSG2:
		if (!audit_enabled && msg_type != AUDIT_USER_AVC)
			return 0;

		err = audit_filter_user();
		if (err == 1) {
			err = 0;
			if (msg_type == AUDIT_USER_TTY) {
				err = audit_prepare_user_tty(pid, loginuid,
							     sessionid);
				if (err)
					break;
			}
			audit_log_common_recv_msg(&ab, msg_type, pid, uid,
						  loginuid, sessionid, sid);

			if (msg_type != AUDIT_USER_TTY)
				audit_log_format(ab, " msg='%.1024s'",
						 (char *)data);
			else {
				int size;

				audit_log_format(ab, " msg=");
				size = nlmsg_len(nlh);
				if (size > 0 &&
				    ((unsigned char *)data)[size - 1] == '\0')
					size--;
				audit_log_n_untrustedstring(ab, data, size);
			}
			audit_set_pid(ab, pid);
			audit_log_end(ab);
		}
		break;
	case AUDIT_ADD:
	case AUDIT_DEL:
		if (nlmsg_len(nlh) < sizeof(struct audit_rule))
			return -EINVAL;
		if (audit_enabled == AUDIT_LOCKED) {
			audit_log_common_recv_msg(&ab, AUDIT_CONFIG_CHANGE, pid,
						  uid, loginuid, sessionid, sid);

			audit_log_format(ab, " audit_enabled=%d res=0",
					 audit_enabled);
			audit_log_end(ab);
			return -EPERM;
		}
		/* fallthrough */
	case AUDIT_LIST:
		err = audit_receive_filter(msg_type, NETLINK_CB(skb).pid,
					   uid, seq, data, nlmsg_len(nlh),
					   loginuid, sessionid, sid);
		break;
	case AUDIT_ADD_RULE:
	case AUDIT_DEL_RULE:
		if (nlmsg_len(nlh) < sizeof(struct audit_rule_data))
			return -EINVAL;
		if (audit_enabled == AUDIT_LOCKED) {
			audit_log_common_recv_msg(&ab, AUDIT_CONFIG_CHANGE, pid,
						  uid, loginuid, sessionid, sid);

			audit_log_format(ab, " audit_enabled=%d res=0",
					 audit_enabled);
			audit_log_end(ab);
			return -EPERM;
		}
		/* fallthrough */
	case AUDIT_LIST_RULES:
		err = audit_receive_filter(msg_type, NETLINK_CB(skb).pid,
					   uid, seq, data, nlmsg_len(nlh),
					   loginuid, sessionid, sid);
		break;
	case AUDIT_TRIM:
		audit_trim_trees();

		audit_log_common_recv_msg(&ab, AUDIT_CONFIG_CHANGE, pid,
					  uid, loginuid, sessionid, sid);

		audit_log_format(ab, " op=trim res=1");
		audit_log_end(ab);
		break;
	case AUDIT_MAKE_EQUIV: {
		void *bufp = data;
		u32 sizes[2];
		size_t msglen = nlmsg_len(nlh);
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

		audit_log_common_recv_msg(&ab, AUDIT_CONFIG_CHANGE, pid,
					  uid, loginuid, sessionid, sid);

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
		sig_data->uid = audit_sig_uid;
		sig_data->pid = audit_sig_pid;
		if (audit_sig_sid) {
			memcpy(sig_data->ctx, ctx, len);
			security_release_secctx(ctx, len);
		}
		audit_send_reply(NETLINK_CB(skb).pid, seq, AUDIT_SIGNAL_INFO,
				0, 0, sig_data, sizeof(*sig_data) + len);
		kfree(sig_data);
		break;
	case AUDIT_TTY_GET: {
		struct audit_tty_status s;
		struct task_struct *tsk;
		unsigned long flags;

		rcu_read_lock();
		tsk = find_task_by_vpid(pid);
		if (tsk && lock_task_sighand(tsk, &flags)) {
			s.enabled = tsk->signal->audit_tty != 0;
			unlock_task_sighand(tsk, &flags);
		} else
			err = -ESRCH;
		rcu_read_unlock();

		if (!err)
			audit_send_reply(NETLINK_CB(skb).pid, seq,
					 AUDIT_TTY_GET, 0, 0, &s, sizeof(s));
		break;
	}
	case AUDIT_TTY_SET: {
		struct audit_tty_status *s;
		struct task_struct *tsk;
		unsigned long flags;

		if (nlh->nlmsg_len < sizeof(struct audit_tty_status))
			return -EINVAL;
		s = data;
		if (s->enabled != 0 && s->enabled != 1)
			return -EINVAL;
		rcu_read_lock();
		tsk = find_task_by_vpid(pid);
		if (tsk && lock_task_sighand(tsk, &flags)) {
			tsk->signal->audit_tty = s->enabled != 0;
			unlock_task_sighand(tsk, &flags);
		} else
			err = -ESRCH;
		rcu_read_unlock();
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	return err < 0 ? err : 0;
}

/*
 * Get message from skb.  Each message is processed by audit_receive_msg.
 * Malformed skbs with wrong length are discarded silently.
 */
static void audit_receive_skb(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	/*
	 * len MUST be signed for NLMSG_NEXT to be able to dec it below 0
	 * if the nlmsg_len was not aligned
	 */
	int len;
	int err;

	nlh = nlmsg_hdr(skb);
	len = skb->len;

	while (NLMSG_OK(nlh, len)) {
		err = audit_receive_msg(skb, nlh);
		/* if err or if this message says it wants a response */
		if (err || (nlh->nlmsg_flags & NLM_F_ACK))
			netlink_ack(skb, nlh, err);

		nlh = NLMSG_NEXT(nlh, len);
	}
}

/* Receive messages from netlink socket. */
static void audit_receive(struct sk_buff  *skb)
{
	mutex_lock(&audit_cmd_mutex);
	audit_receive_skb(skb);
	mutex_unlock(&audit_cmd_mutex);
}

/* Initialize audit support at boot time. */
static int __init audit_init(void)
{
	int i;
	struct netlink_kernel_cfg cfg = {
		.input	= audit_receive,
	};

	if (audit_initialized == AUDIT_DISABLED)
		return 0;

	printk(KERN_INFO "audit: initializing netlink socket (%s)\n",
	       audit_default ? "enabled" : "disabled");
	audit_sock = netlink_kernel_create(&init_net, NETLINK_AUDIT,
					   THIS_MODULE, &cfg);
	if (!audit_sock)
		audit_panic("cannot initialize netlink socket");
	else
		audit_sock->sk_sndtimeo = MAX_SCHEDULE_TIMEOUT;

	skb_queue_head_init(&audit_skb_queue);
	skb_queue_head_init(&audit_skb_hold_queue);
	audit_initialized = AUDIT_INITIALIZED;
	audit_enabled = audit_default;
	audit_ever_enabled |= !!audit_default;

	audit_log(NULL, GFP_KERNEL, AUDIT_KERNEL, "initialized");

	for (i = 0; i < AUDIT_INODE_BUCKETS; i++)
		INIT_LIST_HEAD(&audit_inode_hash[i]);

	return 0;
}
__initcall(audit_init);

/* Process kernel command-line parameter at boot time.  audit=0 or audit=1. */
static int __init audit_enable(char *str)
{
	audit_default = !!simple_strtol(str, NULL, 0);
	if (!audit_default)
		audit_initialized = AUDIT_DISABLED;

	printk(KERN_INFO "audit: %s", audit_default ? "enabled" : "disabled");

	if (audit_initialized == AUDIT_INITIALIZED) {
		audit_enabled = audit_default;
		audit_ever_enabled |= !!audit_default;
	} else if (audit_initialized == AUDIT_UNINITIALIZED) {
		printk(" (after initialization)");
	} else {
		printk(" (until reboot)");
	}
	printk("\n");

	return 1;
}

__setup("audit=", audit_enable);

static void audit_buffer_free(struct audit_buffer *ab)
{
	unsigned long flags;

	if (!ab)
		return;

	if (ab->skb)
		kfree_skb(ab->skb);

	spin_lock_irqsave(&audit_freelist_lock, flags);
	if (audit_freelist_count > AUDIT_MAXFREE)
		kfree(ab);
	else {
		audit_freelist_count++;
		list_add(&ab->list, &audit_freelist);
	}
	spin_unlock_irqrestore(&audit_freelist_lock, flags);
}

static struct audit_buffer * audit_buffer_alloc(struct audit_context *ctx,
						gfp_t gfp_mask, int type)
{
	unsigned long flags;
	struct audit_buffer *ab = NULL;
	struct nlmsghdr *nlh;

	spin_lock_irqsave(&audit_freelist_lock, flags);
	if (!list_empty(&audit_freelist)) {
		ab = list_entry(audit_freelist.next,
				struct audit_buffer, list);
		list_del(&ab->list);
		--audit_freelist_count;
	}
	spin_unlock_irqrestore(&audit_freelist_lock, flags);

	if (!ab) {
		ab = kmalloc(sizeof(*ab), gfp_mask);
		if (!ab)
			goto err;
	}

	ab->ctx = ctx;
	ab->gfp_mask = gfp_mask;

	ab->skb = nlmsg_new(AUDIT_BUFSIZ, gfp_mask);
	if (!ab->skb)
		goto err;

	nlh = nlmsg_put(ab->skb, 0, 0, type, 0, 0);
	if (!nlh)
		goto out_kfree_skb;

	return ab;

out_kfree_skb:
	kfree_skb(ab->skb);
	ab->skb = NULL;
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
	static DEFINE_SPINLOCK(serial_lock);
	static unsigned int serial = 0;

	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&serial_lock, flags);
	do {
		ret = ++serial;
	} while (unlikely(!ret));
	spin_unlock_irqrestore(&serial_lock, flags);

	return ret;
}

static inline void audit_get_stamp(struct audit_context *ctx,
				   struct timespec *t, unsigned int *serial)
{
	if (!ctx || !auditsc_get_stamp(ctx, t, serial)) {
		*t = CURRENT_TIME;
		*serial = audit_serial();
	}
}

/* Obtain an audit buffer.  This routine does locking to obtain the
 * audit buffer, but then no locking is required for calls to
 * audit_log_*format.  If the tsk is a task that is currently in a
 * syscall, then the syscall is marked as auditable and an audit record
 * will be written at syscall exit.  If there is no associated task, tsk
 * should be NULL. */

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
	struct audit_buffer	*ab	= NULL;
	struct timespec		t;
	unsigned int		uninitialized_var(serial);
	int reserve;
	unsigned long timeout_start = jiffies;

	if (audit_initialized != AUDIT_INITIALIZED)
		return NULL;

	if (unlikely(audit_filter_type(type)))
		return NULL;

	if (gfp_mask & __GFP_WAIT)
		reserve = 0;
	else
		reserve = 5; /* Allow atomic callers to go up to five
				entries over the normal backlog limit */

	while (audit_backlog_limit
	       && skb_queue_len(&audit_skb_queue) > audit_backlog_limit + reserve) {
		if (gfp_mask & __GFP_WAIT && audit_backlog_wait_time
		    && time_before(jiffies, timeout_start + audit_backlog_wait_time)) {

			/* Wait for auditd to drain the queue a little */
			DECLARE_WAITQUEUE(wait, current);
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&audit_backlog_wait, &wait);

			if (audit_backlog_limit &&
			    skb_queue_len(&audit_skb_queue) > audit_backlog_limit)
				schedule_timeout(timeout_start + audit_backlog_wait_time - jiffies);

			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&audit_backlog_wait, &wait);
			continue;
		}
		if (audit_rate_check() && printk_ratelimit())
			printk(KERN_WARNING
			       "audit: audit_backlog=%d > "
			       "audit_backlog_limit=%d\n",
			       skb_queue_len(&audit_skb_queue),
			       audit_backlog_limit);
		audit_log_lost("backlog limit exceeded");
		audit_backlog_wait_time = audit_backlog_wait_overflow;
		wake_up(&audit_backlog_wait);
		return NULL;
	}

	ab = audit_buffer_alloc(ctx, gfp_mask, type);
	if (!ab) {
		audit_log_lost("out of memory in audit_log_start");
		return NULL;
	}

	audit_get_stamp(ab->ctx, &t, &serial);

	audit_log_format(ab, "audit(%lu.%03lu:%u): ",
			 t.tv_sec, t.tv_nsec/1000000, serial);
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
 * audit_log_hex - convert a buffer to hex and append it to the audit skb
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
	static const unsigned char *hex = "0123456789ABCDEF";

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
	for (i=0; i<len; i++) {
		*ptr++ = hex[(buf[i] & 0xF0)>>4]; /* Upper nibble */
		*ptr++ = hex[buf[i] & 0x0F];	  /* Lower nibble */
	}
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
int audit_string_contains_control(const char *string, size_t len)
{
	const unsigned char *p;
	for (p = string; p < (const unsigned char *)string + len; p++) {
		if (*p == '"' || *p < 0x21 || *p > 0x7e)
			return 1;
	}
	return 0;
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
		audit_log_string(ab, "<no_memory>");
		return;
	}
	p = d_path(path, pathname, PATH_MAX+11);
	if (IS_ERR(p)) { /* Should never happen since we send PATH_MAX */
		/* FIXME: can we save some information here? */
		audit_log_string(ab, "<too_long>");
	} else
		audit_log_untrustedstring(ab, p);
	kfree(pathname);
}

void audit_log_key(struct audit_buffer *ab, char *key)
{
	audit_log_format(ab, " key=");
	if (key)
		audit_log_untrustedstring(ab, key);
	else
		audit_log_format(ab, "(null)");
}

/**
 * audit_log_link_denied - report a link restriction denial
 * @operation: specific link opreation
 * @link: the path that triggered the restriction
 */
void audit_log_link_denied(const char *operation, struct path *link)
{
	struct audit_buffer *ab;

	ab = audit_log_start(current->audit_context, GFP_KERNEL,
			     AUDIT_ANOM_LINK);
	audit_log_format(ab, "op=%s action=denied", operation);
	audit_log_format(ab, " pid=%d comm=", current->pid);
	audit_log_untrustedstring(ab, current->comm);
	audit_log_d_path(ab, " path=", link);
	audit_log_format(ab, " dev=");
	audit_log_untrustedstring(ab, link->dentry->d_inode->i_sb->s_id);
	audit_log_format(ab, " ino=%lu", link->dentry->d_inode->i_ino);
	audit_log_end(ab);
}

/**
 * audit_log_end - end one audit record
 * @ab: the audit_buffer
 *
 * The netlink_* functions cannot be called inside an irq context, so
 * the audit buffer is placed on a queue and a tasklet is scheduled to
 * remove them from the queue outside the irq context.  May be called in
 * any context.
 */
void audit_log_end(struct audit_buffer *ab)
{
	if (!ab)
		return;
	if (!audit_rate_check()) {
		audit_log_lost("rate limit exceeded");
	} else {
		struct nlmsghdr *nlh = nlmsg_hdr(ab->skb);
		nlh->nlmsg_len = ab->skb->len - NLMSG_SPACE(0);

		if (audit_pid) {
			skb_queue_tail(&audit_skb_queue, ab->skb);
			wake_up_interruptible(&kauditd_wait);
		} else {
			audit_printk_skb(ab->skb);
		}
		ab->skb = NULL;
	}
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

#ifdef CONFIG_SECURITY
/**
 * audit_log_secctx - Converts and logs SELinux context
 * @ab: audit_buffer
 * @secid: security number
 *
 * This is a helper function that calls security_secid_to_secctx to convert
 * secid to secctx and then adds the (converted) SELinux context to the audit
 * log by calling audit_log_format, thus also preventing leak of internal secid
 * to userspace. If secid cannot be converted audit_panic is called.
 */
void audit_log_secctx(struct audit_buffer *ab, u32 secid)
{
	u32 len;
	char *secctx;

	if (security_secid_to_secctx(secid, &secctx, &len)) {
		audit_panic("Cannot convert secid to context");
	} else {
		audit_log_format(ab, " obj=%s", secctx);
		security_release_secctx(secctx, len);
	}
}
EXPORT_SYMBOL(audit_log_secctx);
#endif

EXPORT_SYMBOL(audit_log_start);
EXPORT_SYMBOL(audit_log_end);
EXPORT_SYMBOL(audit_log_format);
EXPORT_SYMBOL(audit_log);
