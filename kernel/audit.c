/* audit.c -- Auditing support
 * Gateway between the kernel (e.g., selinux) and the user-space audit daemon.
 * System-call specific features have moved to auditsc.c
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
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
 * Goals: 1) Integrate fully with SELinux.
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
#include <asm/atomic.h>
#include <asm/types.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <linux/audit.h>

#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>

/* No auditing will take place until audit_initialized != 0.
 * (Initialization happens after skb_init is called.) */
static int	audit_initialized;

/* No syscall auditing will take place unless audit_enabled != 0. */
int		audit_enabled;

/* Default state when kernel boots without any parameters. */
static int	audit_default;

/* If auditing cannot proceed, audit_failure selects what happens. */
static int	audit_failure = AUDIT_FAIL_PRINTK;

/* If audit records are to be written to the netlink socket, audit_pid
 * contains the (non-zero) pid. */
static int	audit_pid;

/* If audit_limit is non-zero, limit the rate of sending audit records
 * to that number per second.  This prevents DoS attacks, but results in
 * audit records being dropped. */
static int	audit_rate_limit;

/* Number of outstanding audit_buffers allowed. */
static int	audit_backlog_limit = 64;
static atomic_t	audit_backlog	    = ATOMIC_INIT(0);

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

/* There are two lists of audit buffers.  The txlist contains audit
 * buffers that cannot be sent immediately to the netlink device because
 * we are in an irq context (these are sent later in a tasklet).
 *
 * The second list is a list of pre-allocated audit buffers (if more
 * than AUDIT_MAXFREE are in use, the audit buffer is freed instead of
 * being placed on the freelist). */
static DEFINE_SPINLOCK(audit_txlist_lock);
static DEFINE_SPINLOCK(audit_freelist_lock);
static int	   audit_freelist_count = 0;
static LIST_HEAD(audit_txlist);
static LIST_HEAD(audit_freelist);

/* There are three lists of rules -- one to search at task creation
 * time, one to search at syscall entry time, and another to search at
 * syscall exit time. */
static LIST_HEAD(audit_tsklist);
static LIST_HEAD(audit_entlist);
static LIST_HEAD(audit_extlist);

/* The netlink socket is only to be read by 1 CPU, which lets us assume
 * that list additions and deletions never happen simultaneiously in
 * auditsc.c */
static DECLARE_MUTEX(audit_netlink_sem);

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
	struct sk_buff_head  sklist;	/* formatted skbs ready to send */
	struct audit_context *ctx;	/* NULL or associated context */
	int		     len;	/* used area of tmp */
	char		     tmp[AUDIT_BUFSIZ];

				/* Pointer to header and contents */
	struct nlmsghdr      *nlh;
	int		     total;
	int		     type;
	int		     pid;
};

void audit_set_type(struct audit_buffer *ab, int type)
{
	ab->type = type;
}

struct audit_entry {
	struct list_head  list;
	struct audit_rule rule;
};

static void audit_log_end_irq(struct audit_buffer *ab);
static void audit_log_end_fast(struct audit_buffer *ab);

static void audit_panic(const char *message)
{
	switch (audit_failure)
	{
	case AUDIT_FAIL_SILENT:
		break;
	case AUDIT_FAIL_PRINTK:
		printk(KERN_ERR "audit: %s\n", message);
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

/* Emit at least 1 message per second, even if audit_rate_check is
 * throttling. */
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
		printk(KERN_WARNING
		       "audit: audit_lost=%d audit_backlog=%d"
		       " audit_rate_limit=%d audit_backlog_limit=%d\n",
		       atomic_read(&audit_lost),
		       atomic_read(&audit_backlog),
		       audit_rate_limit,
		       audit_backlog_limit);
		audit_panic(message);
	}

}

static int audit_set_rate_limit(int limit, uid_t loginuid)
{
	int old		 = audit_rate_limit;
	audit_rate_limit = limit;
	audit_log(NULL, "audit_rate_limit=%d old=%d by auid %u",
			audit_rate_limit, old, loginuid);
	return old;
}

static int audit_set_backlog_limit(int limit, uid_t loginuid)
{
	int old		 = audit_backlog_limit;
	audit_backlog_limit = limit;
	audit_log(NULL, "audit_backlog_limit=%d old=%d by auid %u",
			audit_backlog_limit, old, loginuid);
	return old;
}

static int audit_set_enabled(int state, uid_t loginuid)
{
	int old		 = audit_enabled;
	if (state != 0 && state != 1)
		return -EINVAL;
	audit_enabled = state;
	audit_log(NULL, "audit_enabled=%d old=%d by auid %u",
		  audit_enabled, old, loginuid);
	return old;
}

static int audit_set_failure(int state, uid_t loginuid)
{
	int old		 = audit_failure;
	if (state != AUDIT_FAIL_SILENT
	    && state != AUDIT_FAIL_PRINTK
	    && state != AUDIT_FAIL_PANIC)
		return -EINVAL;
	audit_failure = state;
	audit_log(NULL, "audit_failure=%d old=%d by auid %u",
		  audit_failure, old, loginuid);
	return old;
}

#ifdef CONFIG_NET
void audit_send_reply(int pid, int seq, int type, int done, int multi,
		      void *payload, int size)
{
	struct sk_buff	*skb;
	struct nlmsghdr	*nlh;
	int		len = NLMSG_SPACE(size);
	void		*data;
	int		flags = multi ? NLM_F_MULTI : 0;
	int		t     = done  ? NLMSG_DONE  : type;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		goto nlmsg_failure;

	nlh		 = NLMSG_PUT(skb, pid, seq, t, len - sizeof(*nlh));
	nlh->nlmsg_flags = flags;
	data		 = NLMSG_DATA(nlh);
	memcpy(data, payload, size);
	netlink_unicast(audit_sock, skb, pid, MSG_DONTWAIT);
	return;

nlmsg_failure:			/* Used by NLMSG_PUT */
	if (skb)
		kfree_skb(skb);
}

/*
 * Check for appropriate CAP_AUDIT_ capabilities on incoming audit
 * control messages.
 */
static int audit_netlink_ok(kernel_cap_t eff_cap, u16 msg_type)
{
	int err = 0;

	switch (msg_type) {
	case AUDIT_GET:
	case AUDIT_LIST:
	case AUDIT_SET:
	case AUDIT_ADD:
	case AUDIT_DEL:
		if (!cap_raised(eff_cap, CAP_AUDIT_CONTROL))
			err = -EPERM;
		break;
	case AUDIT_USER:
		if (!cap_raised(eff_cap, CAP_AUDIT_WRITE))
			err = -EPERM;
		break;
	default:  /* bad msg */
		err = -EINVAL;
	}

	return err;
}

static int audit_receive_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	u32			uid, pid, seq;
	void			*data;
	struct audit_status	*status_get, status_set;
	int			err;
	struct audit_buffer	*ab;
	u16			msg_type = nlh->nlmsg_type;
	uid_t			loginuid; /* loginuid of sender */

	err = audit_netlink_ok(NETLINK_CB(skb).eff_cap, msg_type);
	if (err)
		return err;

	pid  = NETLINK_CREDS(skb)->pid;
	uid  = NETLINK_CREDS(skb)->uid;
	loginuid = NETLINK_CB(skb).loginuid;
	seq  = nlh->nlmsg_seq;
	data = NLMSG_DATA(nlh);

	switch (msg_type) {
	case AUDIT_GET:
		status_set.enabled	 = audit_enabled;
		status_set.failure	 = audit_failure;
		status_set.pid		 = audit_pid;
		status_set.rate_limit	 = audit_rate_limit;
		status_set.backlog_limit = audit_backlog_limit;
		status_set.lost		 = atomic_read(&audit_lost);
		status_set.backlog	 = atomic_read(&audit_backlog);
		audit_send_reply(NETLINK_CB(skb).pid, seq, AUDIT_GET, 0, 0,
				 &status_set, sizeof(status_set));
		break;
	case AUDIT_SET:
		if (nlh->nlmsg_len < sizeof(struct audit_status))
			return -EINVAL;
		status_get   = (struct audit_status *)data;
		if (status_get->mask & AUDIT_STATUS_ENABLED) {
			err = audit_set_enabled(status_get->enabled, loginuid);
			if (err < 0) return err;
		}
		if (status_get->mask & AUDIT_STATUS_FAILURE) {
			err = audit_set_failure(status_get->failure, loginuid);
			if (err < 0) return err;
		}
		if (status_get->mask & AUDIT_STATUS_PID) {
			int old   = audit_pid;
			audit_pid = status_get->pid;
			audit_log(NULL, "audit_pid=%d old=%d by auid %u",
				  audit_pid, old, loginuid);
		}
		if (status_get->mask & AUDIT_STATUS_RATE_LIMIT)
			audit_set_rate_limit(status_get->rate_limit, loginuid);
		if (status_get->mask & AUDIT_STATUS_BACKLOG_LIMIT)
			audit_set_backlog_limit(status_get->backlog_limit,
							loginuid);
		break;
	case AUDIT_USER:
		ab = audit_log_start(NULL);
		if (!ab)
			break;	/* audit_panic has been called */
		audit_log_format(ab,
				 "user pid=%d uid=%d length=%d loginuid=%u"
				 " msg='%.1024s'",
				 pid, uid,
				 (int)(nlh->nlmsg_len
				       - ((char *)data - (char *)nlh)),
				 loginuid, (char *)data);
		ab->type = AUDIT_USER;
		ab->pid  = pid;
		audit_log_end(ab);
		break;
	case AUDIT_ADD:
	case AUDIT_DEL:
		if (nlh->nlmsg_len < sizeof(struct audit_rule))
			return -EINVAL;
		/* fallthrough */
	case AUDIT_LIST:
#ifdef CONFIG_AUDITSYSCALL
		err = audit_receive_filter(nlh->nlmsg_type, NETLINK_CB(skb).pid,
					   uid, seq, data, loginuid);
#else
		err = -EOPNOTSUPP;
#endif
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err < 0 ? err : 0;
}

/* Get message from skb (based on rtnetlink_rcv_skb).  Each message is
 * processed by audit_receive_msg.  Malformed skbs with wrong length are
 * discarded silently.  */
static void audit_receive_skb(struct sk_buff *skb)
{
	int		err;
	struct nlmsghdr	*nlh;
	u32		rlen;

	while (skb->len >= NLMSG_SPACE(0)) {
		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) || skb->len < nlh->nlmsg_len)
			return;
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;
		if ((err = audit_receive_msg(skb, nlh))) {
			netlink_ack(skb, nlh, err);
		} else if (nlh->nlmsg_flags & NLM_F_ACK)
			netlink_ack(skb, nlh, 0);
		skb_pull(skb, rlen);
	}
}

/* Receive messages from netlink socket. */
static void audit_receive(struct sock *sk, int length)
{
	struct sk_buff  *skb;
	unsigned int qlen;

	down(&audit_netlink_sem);

	for (qlen = skb_queue_len(&sk->sk_receive_queue); qlen; qlen--) {
		skb = skb_dequeue(&sk->sk_receive_queue);
		audit_receive_skb(skb);
		kfree_skb(skb);
	}
	up(&audit_netlink_sem);
}

/* Move data from tmp buffer into an skb.  This is an extra copy, and
 * that is unfortunate.  However, the copy will only occur when a record
 * is being written to user space, which is already a high-overhead
 * operation.  (Elimination of the copy is possible, for example, by
 * writing directly into a pre-allocated skb, at the cost of wasting
 * memory. */
static void audit_log_move(struct audit_buffer *ab)
{
	struct sk_buff	*skb;
	char		*start;
	int		extra = ab->nlh ? 0 : NLMSG_SPACE(0);

	/* possible resubmission */
	if (ab->len == 0)
		return;

	skb = skb_peek_tail(&ab->sklist);
	if (!skb || skb_tailroom(skb) <= ab->len + extra) {
		skb = alloc_skb(2 * ab->len + extra, GFP_ATOMIC);
		if (!skb) {
			ab->len = 0; /* Lose information in ab->tmp */
			audit_log_lost("out of memory in audit_log_move");
			return;
		}
		__skb_queue_tail(&ab->sklist, skb);
		if (!ab->nlh)
			ab->nlh = (struct nlmsghdr *)skb_put(skb,
							     NLMSG_SPACE(0));
	}
	start = skb_put(skb, ab->len);
	memcpy(start, ab->tmp, ab->len);
	ab->len = 0;
}

/* Iterate over the skbuff in the audit_buffer, sending their contents
 * to user space. */
static inline int audit_log_drain(struct audit_buffer *ab)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ab->sklist))) {
		int retval = 0;

		if (audit_pid) {
			if (ab->nlh) {
				ab->nlh->nlmsg_len   = ab->total;
				ab->nlh->nlmsg_type  = ab->type;
				ab->nlh->nlmsg_flags = 0;
				ab->nlh->nlmsg_seq   = 0;
				ab->nlh->nlmsg_pid   = ab->pid;
			}
			skb_get(skb); /* because netlink_* frees */
			retval = netlink_unicast(audit_sock, skb, audit_pid,
						 MSG_DONTWAIT);
		}
		if (retval == -EAGAIN &&
		    (atomic_read(&audit_backlog)) < audit_backlog_limit) {
			skb_queue_head(&ab->sklist, skb);
			audit_log_end_irq(ab);
			return 1;
		}
		if (retval < 0) {
			if (retval == -ECONNREFUSED) {
				printk(KERN_ERR
				       "audit: *NO* daemon at audit_pid=%d\n",
				       audit_pid);
				audit_pid = 0;
			} else
				audit_log_lost("netlink socket too busy");
		}
		if (!audit_pid) { /* No daemon */
			int offset = ab->nlh ? NLMSG_SPACE(0) : 0;
			int len    = skb->len - offset;
			skb->data[offset + len] = '\0';
			printk(KERN_ERR "%s\n", skb->data + offset);
		}
		kfree_skb(skb);
		ab->nlh = NULL;
	}
	return 0;
}

/* Initialize audit support at boot time. */
static int __init audit_init(void)
{
	printk(KERN_INFO "audit: initializing netlink socket (%s)\n",
	       audit_default ? "enabled" : "disabled");
	audit_sock = netlink_kernel_create(NETLINK_AUDIT, audit_receive);
	if (!audit_sock)
		audit_panic("cannot initialize netlink socket");

	audit_initialized = 1;
	audit_enabled = audit_default;
	audit_log(NULL, "initialized");
	return 0;
}

#else
/* Without CONFIG_NET, we have no skbuffs.  For now, print what we have
 * in the buffer. */
static void audit_log_move(struct audit_buffer *ab)
{
	printk(KERN_ERR "%*.*s\n", ab->len, ab->len, ab->tmp);
	ab->len = 0;
}

static inline int audit_log_drain(struct audit_buffer *ab)
{
	return 0;
}

/* Initialize audit support at boot time. */
int __init audit_init(void)
{
	printk(KERN_INFO "audit: initializing WITHOUT netlink support\n");
	audit_sock = NULL;
	audit_pid  = 0;

	audit_initialized = 1;
	audit_enabled = audit_default;
	audit_log(NULL, "initialized");
	return 0;
}
#endif

__initcall(audit_init);

/* Process kernel command-line parameter at boot time.  audit=0 or audit=1. */
static int __init audit_enable(char *str)
{
	audit_default = !!simple_strtol(str, NULL, 0);
	printk(KERN_INFO "audit: %s%s\n",
	       audit_default ? "enabled" : "disabled",
	       audit_initialized ? "" : " (after initialization)");
	if (audit_initialized)
		audit_enabled = audit_default;
	return 0;
}

__setup("audit=", audit_enable);


/* Obtain an audit buffer.  This routine does locking to obtain the
 * audit buffer, but then no locking is required for calls to
 * audit_log_*format.  If the tsk is a task that is currently in a
 * syscall, then the syscall is marked as auditable and an audit record
 * will be written at syscall exit.  If there is no associated task, tsk
 * should be NULL. */
struct audit_buffer *audit_log_start(struct audit_context *ctx)
{
	struct audit_buffer	*ab	= NULL;
	unsigned long		flags;
	struct timespec		t;
	unsigned int		serial;

	if (!audit_initialized)
		return NULL;

	if (audit_backlog_limit
	    && atomic_read(&audit_backlog) > audit_backlog_limit) {
		if (audit_rate_check())
			printk(KERN_WARNING
			       "audit: audit_backlog=%d > "
			       "audit_backlog_limit=%d\n",
			       atomic_read(&audit_backlog),
			       audit_backlog_limit);
		audit_log_lost("backlog limit exceeded");
		return NULL;
	}

	spin_lock_irqsave(&audit_freelist_lock, flags);
	if (!list_empty(&audit_freelist)) {
		ab = list_entry(audit_freelist.next,
				struct audit_buffer, list);
		list_del(&ab->list);
		--audit_freelist_count;
	}
	spin_unlock_irqrestore(&audit_freelist_lock, flags);

	if (!ab)
		ab = kmalloc(sizeof(*ab), GFP_ATOMIC);
	if (!ab) {
		audit_log_lost("out of memory in audit_log_start");
		return NULL;
	}

	atomic_inc(&audit_backlog);
	skb_queue_head_init(&ab->sklist);

	ab->ctx   = ctx;
	ab->len   = 0;
	ab->nlh   = NULL;
	ab->total = 0;
	ab->type  = AUDIT_KERNEL;
	ab->pid   = 0;

#ifdef CONFIG_AUDITSYSCALL
	if (ab->ctx)
		audit_get_stamp(ab->ctx, &t, &serial);
	else
#endif
	{
		t = CURRENT_TIME;
		serial = 0;
	}
	audit_log_format(ab, "audit(%lu.%03lu:%u): ",
			 t.tv_sec, t.tv_nsec/1000000, serial);
	return ab;
}


/* Format an audit message into the audit buffer.  If there isn't enough
 * room in the audit buffer, more room will be allocated and vsnprint
 * will be called a second time.  Currently, we assume that a printk
 * can't format message larger than 1024 bytes, so we don't either. */
static void audit_log_vformat(struct audit_buffer *ab, const char *fmt,
			      va_list args)
{
	int len, avail;

	if (!ab)
		return;

	avail = sizeof(ab->tmp) - ab->len;
	if (avail <= 0) {
		audit_log_move(ab);
		avail = sizeof(ab->tmp) - ab->len;
	}
	len   = vsnprintf(ab->tmp + ab->len, avail, fmt, args);
	if (len >= avail) {
		/* The printk buffer is 1024 bytes long, so if we get
		 * here and AUDIT_BUFSIZ is at least 1024, then we can
		 * log everything that printk could have logged. */
		audit_log_move(ab);
		avail = sizeof(ab->tmp) - ab->len;
		len   = vsnprintf(ab->tmp + ab->len, avail, fmt, args);
	}
	ab->len   += (len < avail) ? len : avail;
	ab->total += (len < avail) ? len : avail;
}

/* Format a message into the audit buffer.  All the work is done in
 * audit_log_vformat. */
void audit_log_format(struct audit_buffer *ab, const char *fmt, ...)
{
	va_list args;

	if (!ab)
		return;
	va_start(args, fmt);
	audit_log_vformat(ab, fmt, args);
	va_end(args);
}

void audit_log_hex(struct audit_buffer *ab, const unsigned char *buf, size_t len)
{
	int i;

	for (i=0; i<len; i++)
		audit_log_format(ab, "%02x", buf[i]);
}

void audit_log_untrustedstring(struct audit_buffer *ab, const char *string)
{
	const unsigned char *p = string;

	while (*p) {
		if (*p == '"' || *p == ' ' || *p < 0x20 || *p > 0x7f) {
			audit_log_hex(ab, string, strlen(string));
			return;
		}
		p++;
	}
	audit_log_format(ab, "\"%s\"", string);
}


/* This is a helper-function to print the d_path without using a static
 * buffer or allocating another buffer in addition to the one in
 * audit_buffer. */
void audit_log_d_path(struct audit_buffer *ab, const char *prefix,
		      struct dentry *dentry, struct vfsmount *vfsmnt)
{
	char *p;
	int  len, avail;

	if (prefix) audit_log_format(ab, " %s", prefix);

	if (ab->len > 128)
		audit_log_move(ab);
	avail = sizeof(ab->tmp) - ab->len;
	p = d_path(dentry, vfsmnt, ab->tmp + ab->len, avail);
	if (IS_ERR(p)) {
		/* FIXME: can we save some information here? */
		audit_log_format(ab, "<toolong>");
	} else {
				/* path isn't at start of buffer */
		len	   = (ab->tmp + sizeof(ab->tmp) - 1) - p;
		memmove(ab->tmp + ab->len, p, len);
		ab->len   += len;
		ab->total += len;
	}
}

/* Remove queued messages from the audit_txlist and send them to userspace. */
static void audit_tasklet_handler(unsigned long arg)
{
	LIST_HEAD(list);
	struct audit_buffer *ab;
	unsigned long	    flags;

	spin_lock_irqsave(&audit_txlist_lock, flags);
	list_splice_init(&audit_txlist, &list);
	spin_unlock_irqrestore(&audit_txlist_lock, flags);

	while (!list_empty(&list)) {
		ab = list_entry(list.next, struct audit_buffer, list);
		list_del(&ab->list);
		audit_log_end_fast(ab);
	}
}

static DECLARE_TASKLET(audit_tasklet, audit_tasklet_handler, 0);

/* The netlink_* functions cannot be called inside an irq context, so
 * the audit buffer is places on a queue and a tasklet is scheduled to
 * remove them from the queue outside the irq context.  May be called in
 * any context. */
static void audit_log_end_irq(struct audit_buffer *ab)
{
	unsigned long flags;

	if (!ab)
		return;
	spin_lock_irqsave(&audit_txlist_lock, flags);
	list_add_tail(&ab->list, &audit_txlist);
	spin_unlock_irqrestore(&audit_txlist_lock, flags);

	tasklet_schedule(&audit_tasklet);
}

/* Send the message in the audit buffer directly to user space.  May not
 * be called in an irq context. */
static void audit_log_end_fast(struct audit_buffer *ab)
{
	unsigned long flags;

	BUG_ON(in_irq());
	if (!ab)
		return;
	if (!audit_rate_check()) {
		audit_log_lost("rate limit exceeded");
	} else {
		audit_log_move(ab);
		if (audit_log_drain(ab))
			return;
	}

	atomic_dec(&audit_backlog);
	spin_lock_irqsave(&audit_freelist_lock, flags);
	if (++audit_freelist_count > AUDIT_MAXFREE)
		kfree(ab);
	else
		list_add(&ab->list, &audit_freelist);
	spin_unlock_irqrestore(&audit_freelist_lock, flags);
}

/* Send or queue the message in the audit buffer, depending on the
 * current context.  (A convenience function that may be called in any
 * context.) */
void audit_log_end(struct audit_buffer *ab)
{
	if (in_irq())
		audit_log_end_irq(ab);
	else
		audit_log_end_fast(ab);
}

/* Log an audit record.  This is a convenience function that calls
 * audit_log_start, audit_log_vformat, and audit_log_end.  It may be
 * called in any context. */
void audit_log(struct audit_context *ctx, const char *fmt, ...)
{
	struct audit_buffer *ab;
	va_list args;

	ab = audit_log_start(ctx);
	if (ab) {
		va_start(args, fmt);
		audit_log_vformat(ab, fmt, args);
		va_end(args);
		audit_log_end(ab);
	}
}
