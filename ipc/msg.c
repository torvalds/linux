// SPDX-License-Identifier: GPL-2.0
/*
 * linux/ipc/msg.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *
 * Removed all the remaining kerneld mess
 * Catch the -EFAULT stuff properly
 * Use GFP_KERNEL for messages as in 1.2
 * Fixed up the unchecked user space derefs
 * Copyright (C) 1998 Alan Cox & Andi Kleen
 *
 * /proc/sysvipc/msg support (c) 1999 Dragos Acostachioaie <dragos@iname.com>
 *
 * mostly rewritten, threaded and wake-one semantics added
 * MSGMAX limit removed, sysctl's added
 * (c) 1999 Manfred Spraul <manfred@colorfullife.com>
 *
 * support for audit of ipc object properties and permission changes
 * Dustin Kirkland <dustin.kirkland@us.ibm.com>
 *
 * namespaces support
 * OpenVZ, SWsoft Inc.
 * Pavel Emelianov <xemul@openvz.org>
 */

#include <linux/capability.h>
#include <linux/msg.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/security.h>
#include <linux/sched/wake_q.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/seq_file.h>
#include <linux/rwsem.h>
#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>

#include <asm/current.h>
#include <linux/uaccess.h>
#include "util.h"

/* one msq_queue structure for each present queue on the system */
struct msg_queue {
	struct kern_ipc_perm q_perm;
	time64_t q_stime;		/* last msgsnd time */
	time64_t q_rtime;		/* last msgrcv time */
	time64_t q_ctime;		/* last change time */
	unsigned long q_cbytes;		/* current number of bytes on queue */
	unsigned long q_qnum;		/* number of messages in queue */
	unsigned long q_qbytes;		/* max number of bytes on queue */
	struct pid *q_lspid;		/* pid of last msgsnd */
	struct pid *q_lrpid;		/* last receive pid */

	struct list_head q_messages;
	struct list_head q_receivers;
	struct list_head q_senders;
} __randomize_layout;

/* one msg_receiver structure for each sleeping receiver */
struct msg_receiver {
	struct list_head	r_list;
	struct task_struct	*r_tsk;

	int			r_mode;
	long			r_msgtype;
	long			r_maxsize;

	struct msg_msg		*r_msg;
};

/* one msg_sender for each sleeping sender */
struct msg_sender {
	struct list_head	list;
	struct task_struct	*tsk;
	size_t                  msgsz;
};

#define SEARCH_ANY		1
#define SEARCH_EQUAL		2
#define SEARCH_NOTEQUAL		3
#define SEARCH_LESSEQUAL	4
#define SEARCH_NUMBER		5

#define msg_ids(ns)	((ns)->ids[IPC_MSG_IDS])

static inline struct msg_queue *msq_obtain_object(struct ipc_namespace *ns, int id)
{
	struct kern_ipc_perm *ipcp = ipc_obtain_object_idr(&msg_ids(ns), id);

	if (IS_ERR(ipcp))
		return ERR_CAST(ipcp);

	return container_of(ipcp, struct msg_queue, q_perm);
}

static inline struct msg_queue *msq_obtain_object_check(struct ipc_namespace *ns,
							int id)
{
	struct kern_ipc_perm *ipcp = ipc_obtain_object_check(&msg_ids(ns), id);

	if (IS_ERR(ipcp))
		return ERR_CAST(ipcp);

	return container_of(ipcp, struct msg_queue, q_perm);
}

static inline void msg_rmid(struct ipc_namespace *ns, struct msg_queue *s)
{
	ipc_rmid(&msg_ids(ns), &s->q_perm);
}

static void msg_rcu_free(struct rcu_head *head)
{
	struct kern_ipc_perm *p = container_of(head, struct kern_ipc_perm, rcu);
	struct msg_queue *msq = container_of(p, struct msg_queue, q_perm);

	security_msg_queue_free(&msq->q_perm);
	kvfree(msq);
}

/**
 * newque - Create a new msg queue
 * @ns: namespace
 * @params: ptr to the structure that contains the key and msgflg
 *
 * Called with msg_ids.rwsem held (writer)
 */
static int newque(struct ipc_namespace *ns, struct ipc_params *params)
{
	struct msg_queue *msq;
	int retval;
	key_t key = params->key;
	int msgflg = params->flg;

	msq = kvmalloc(sizeof(*msq), GFP_KERNEL);
	if (unlikely(!msq))
		return -ENOMEM;

	msq->q_perm.mode = msgflg & S_IRWXUGO;
	msq->q_perm.key = key;

	msq->q_perm.security = NULL;
	retval = security_msg_queue_alloc(&msq->q_perm);
	if (retval) {
		kvfree(msq);
		return retval;
	}

	msq->q_stime = msq->q_rtime = 0;
	msq->q_ctime = ktime_get_real_seconds();
	msq->q_cbytes = msq->q_qnum = 0;
	msq->q_qbytes = ns->msg_ctlmnb;
	msq->q_lspid = msq->q_lrpid = NULL;
	INIT_LIST_HEAD(&msq->q_messages);
	INIT_LIST_HEAD(&msq->q_receivers);
	INIT_LIST_HEAD(&msq->q_senders);

	/* ipc_addid() locks msq upon success. */
	retval = ipc_addid(&msg_ids(ns), &msq->q_perm, ns->msg_ctlmni);
	if (retval < 0) {
		call_rcu(&msq->q_perm.rcu, msg_rcu_free);
		return retval;
	}

	ipc_unlock_object(&msq->q_perm);
	rcu_read_unlock();

	return msq->q_perm.id;
}

static inline bool msg_fits_inqueue(struct msg_queue *msq, size_t msgsz)
{
	return msgsz + msq->q_cbytes <= msq->q_qbytes &&
		1 + msq->q_qnum <= msq->q_qbytes;
}

static inline void ss_add(struct msg_queue *msq,
			  struct msg_sender *mss, size_t msgsz)
{
	mss->tsk = current;
	mss->msgsz = msgsz;
	__set_current_state(TASK_INTERRUPTIBLE);
	list_add_tail(&mss->list, &msq->q_senders);
}

static inline void ss_del(struct msg_sender *mss)
{
	if (mss->list.next)
		list_del(&mss->list);
}

static void ss_wakeup(struct msg_queue *msq,
		      struct wake_q_head *wake_q, bool kill)
{
	struct msg_sender *mss, *t;
	struct task_struct *stop_tsk = NULL;
	struct list_head *h = &msq->q_senders;

	list_for_each_entry_safe(mss, t, h, list) {
		if (kill)
			mss->list.next = NULL;

		/*
		 * Stop at the first task we don't wakeup,
		 * we've already iterated the original
		 * sender queue.
		 */
		else if (stop_tsk == mss->tsk)
			break;
		/*
		 * We are not in an EIDRM scenario here, therefore
		 * verify that we really need to wakeup the task.
		 * To maintain current semantics and wakeup order,
		 * move the sender to the tail on behalf of the
		 * blocked task.
		 */
		else if (!msg_fits_inqueue(msq, mss->msgsz)) {
			if (!stop_tsk)
				stop_tsk = mss->tsk;

			list_move_tail(&mss->list, &msq->q_senders);
			continue;
		}

		wake_q_add(wake_q, mss->tsk);
	}
}

static void expunge_all(struct msg_queue *msq, int res,
			struct wake_q_head *wake_q)
{
	struct msg_receiver *msr, *t;

	list_for_each_entry_safe(msr, t, &msq->q_receivers, r_list) {
		wake_q_add(wake_q, msr->r_tsk);
		WRITE_ONCE(msr->r_msg, ERR_PTR(res));
	}
}

/*
 * freeque() wakes up waiters on the sender and receiver waiting queue,
 * removes the message queue from message queue ID IDR, and cleans up all the
 * messages associated with this queue.
 *
 * msg_ids.rwsem (writer) and the spinlock for this message queue are held
 * before freeque() is called. msg_ids.rwsem remains locked on exit.
 */
static void freeque(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp)
{
	struct msg_msg *msg, *t;
	struct msg_queue *msq = container_of(ipcp, struct msg_queue, q_perm);
	DEFINE_WAKE_Q(wake_q);

	expunge_all(msq, -EIDRM, &wake_q);
	ss_wakeup(msq, &wake_q, true);
	msg_rmid(ns, msq);
	ipc_unlock_object(&msq->q_perm);
	wake_up_q(&wake_q);
	rcu_read_unlock();

	list_for_each_entry_safe(msg, t, &msq->q_messages, m_list) {
		atomic_dec(&ns->msg_hdrs);
		free_msg(msg);
	}
	atomic_sub(msq->q_cbytes, &ns->msg_bytes);
	ipc_update_pid(&msq->q_lspid, NULL);
	ipc_update_pid(&msq->q_lrpid, NULL);
	ipc_rcu_putref(&msq->q_perm, msg_rcu_free);
}

long ksys_msgget(key_t key, int msgflg)
{
	struct ipc_namespace *ns;
	static const struct ipc_ops msg_ops = {
		.getnew = newque,
		.associate = security_msg_queue_associate,
	};
	struct ipc_params msg_params;

	ns = current->nsproxy->ipc_ns;

	msg_params.key = key;
	msg_params.flg = msgflg;

	return ipcget(ns, &msg_ids(ns), &msg_ops, &msg_params);
}

SYSCALL_DEFINE2(msgget, key_t, key, int, msgflg)
{
	return ksys_msgget(key, msgflg);
}

static inline unsigned long
copy_msqid_to_user(void __user *buf, struct msqid64_ds *in, int version)
{
	switch (version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	{
		struct msqid_ds out;

		memset(&out, 0, sizeof(out));

		ipc64_perm_to_ipc_perm(&in->msg_perm, &out.msg_perm);

		out.msg_stime		= in->msg_stime;
		out.msg_rtime		= in->msg_rtime;
		out.msg_ctime		= in->msg_ctime;

		if (in->msg_cbytes > USHRT_MAX)
			out.msg_cbytes	= USHRT_MAX;
		else
			out.msg_cbytes	= in->msg_cbytes;
		out.msg_lcbytes		= in->msg_cbytes;

		if (in->msg_qnum > USHRT_MAX)
			out.msg_qnum	= USHRT_MAX;
		else
			out.msg_qnum	= in->msg_qnum;

		if (in->msg_qbytes > USHRT_MAX)
			out.msg_qbytes	= USHRT_MAX;
		else
			out.msg_qbytes	= in->msg_qbytes;
		out.msg_lqbytes		= in->msg_qbytes;

		out.msg_lspid		= in->msg_lspid;
		out.msg_lrpid		= in->msg_lrpid;

		return copy_to_user(buf, &out, sizeof(out));
	}
	default:
		return -EINVAL;
	}
}

static inline unsigned long
copy_msqid_from_user(struct msqid64_ds *out, void __user *buf, int version)
{
	switch (version) {
	case IPC_64:
		if (copy_from_user(out, buf, sizeof(*out)))
			return -EFAULT;
		return 0;
	case IPC_OLD:
	{
		struct msqid_ds tbuf_old;

		if (copy_from_user(&tbuf_old, buf, sizeof(tbuf_old)))
			return -EFAULT;

		out->msg_perm.uid	= tbuf_old.msg_perm.uid;
		out->msg_perm.gid	= tbuf_old.msg_perm.gid;
		out->msg_perm.mode	= tbuf_old.msg_perm.mode;

		if (tbuf_old.msg_qbytes == 0)
			out->msg_qbytes	= tbuf_old.msg_lqbytes;
		else
			out->msg_qbytes	= tbuf_old.msg_qbytes;

		return 0;
	}
	default:
		return -EINVAL;
	}
}

/*
 * This function handles some msgctl commands which require the rwsem
 * to be held in write mode.
 * NOTE: no locks must be held, the rwsem is taken inside this function.
 */
static int msgctl_down(struct ipc_namespace *ns, int msqid, int cmd,
			struct msqid64_ds *msqid64)
{
	struct kern_ipc_perm *ipcp;
	struct msg_queue *msq;
	int err;

	down_write(&msg_ids(ns).rwsem);
	rcu_read_lock();

	ipcp = ipcctl_pre_down_nolock(ns, &msg_ids(ns), msqid, cmd,
				      &msqid64->msg_perm, msqid64->msg_qbytes);
	if (IS_ERR(ipcp)) {
		err = PTR_ERR(ipcp);
		goto out_unlock1;
	}

	msq = container_of(ipcp, struct msg_queue, q_perm);

	err = security_msg_queue_msgctl(&msq->q_perm, cmd);
	if (err)
		goto out_unlock1;

	switch (cmd) {
	case IPC_RMID:
		ipc_lock_object(&msq->q_perm);
		/* freeque unlocks the ipc object and rcu */
		freeque(ns, ipcp);
		goto out_up;
	case IPC_SET:
	{
		DEFINE_WAKE_Q(wake_q);

		if (msqid64->msg_qbytes > ns->msg_ctlmnb &&
		    !capable(CAP_SYS_RESOURCE)) {
			err = -EPERM;
			goto out_unlock1;
		}

		ipc_lock_object(&msq->q_perm);
		err = ipc_update_perm(&msqid64->msg_perm, ipcp);
		if (err)
			goto out_unlock0;

		msq->q_qbytes = msqid64->msg_qbytes;

		msq->q_ctime = ktime_get_real_seconds();
		/*
		 * Sleeping receivers might be excluded by
		 * stricter permissions.
		 */
		expunge_all(msq, -EAGAIN, &wake_q);
		/*
		 * Sleeping senders might be able to send
		 * due to a larger queue size.
		 */
		ss_wakeup(msq, &wake_q, false);
		ipc_unlock_object(&msq->q_perm);
		wake_up_q(&wake_q);

		goto out_unlock1;
	}
	default:
		err = -EINVAL;
		goto out_unlock1;
	}

out_unlock0:
	ipc_unlock_object(&msq->q_perm);
out_unlock1:
	rcu_read_unlock();
out_up:
	up_write(&msg_ids(ns).rwsem);
	return err;
}

static int msgctl_info(struct ipc_namespace *ns, int msqid,
			 int cmd, struct msginfo *msginfo)
{
	int err;
	int max_id;

	/*
	 * We must not return kernel stack data.
	 * due to padding, it's not enough
	 * to set all member fields.
	 */
	err = security_msg_queue_msgctl(NULL, cmd);
	if (err)
		return err;

	memset(msginfo, 0, sizeof(*msginfo));
	msginfo->msgmni = ns->msg_ctlmni;
	msginfo->msgmax = ns->msg_ctlmax;
	msginfo->msgmnb = ns->msg_ctlmnb;
	msginfo->msgssz = MSGSSZ;
	msginfo->msgseg = MSGSEG;
	down_read(&msg_ids(ns).rwsem);
	if (cmd == MSG_INFO) {
		msginfo->msgpool = msg_ids(ns).in_use;
		msginfo->msgmap = atomic_read(&ns->msg_hdrs);
		msginfo->msgtql = atomic_read(&ns->msg_bytes);
	} else {
		msginfo->msgmap = MSGMAP;
		msginfo->msgpool = MSGPOOL;
		msginfo->msgtql = MSGTQL;
	}
	max_id = ipc_get_maxid(&msg_ids(ns));
	up_read(&msg_ids(ns).rwsem);
	return (max_id < 0) ? 0 : max_id;
}

static int msgctl_stat(struct ipc_namespace *ns, int msqid,
			 int cmd, struct msqid64_ds *p)
{
	struct msg_queue *msq;
	int id = 0;
	int err;

	memset(p, 0, sizeof(*p));

	rcu_read_lock();
	if (cmd == MSG_STAT || cmd == MSG_STAT_ANY) {
		msq = msq_obtain_object(ns, msqid);
		if (IS_ERR(msq)) {
			err = PTR_ERR(msq);
			goto out_unlock;
		}
		id = msq->q_perm.id;
	} else { /* IPC_STAT */
		msq = msq_obtain_object_check(ns, msqid);
		if (IS_ERR(msq)) {
			err = PTR_ERR(msq);
			goto out_unlock;
		}
	}

	/* see comment for SHM_STAT_ANY */
	if (cmd == MSG_STAT_ANY)
		audit_ipc_obj(&msq->q_perm);
	else {
		err = -EACCES;
		if (ipcperms(ns, &msq->q_perm, S_IRUGO))
			goto out_unlock;
	}

	err = security_msg_queue_msgctl(&msq->q_perm, cmd);
	if (err)
		goto out_unlock;

	ipc_lock_object(&msq->q_perm);

	if (!ipc_valid_object(&msq->q_perm)) {
		ipc_unlock_object(&msq->q_perm);
		err = -EIDRM;
		goto out_unlock;
	}

	kernel_to_ipc64_perm(&msq->q_perm, &p->msg_perm);
	p->msg_stime  = msq->q_stime;
	p->msg_rtime  = msq->q_rtime;
	p->msg_ctime  = msq->q_ctime;
	p->msg_cbytes = msq->q_cbytes;
	p->msg_qnum   = msq->q_qnum;
	p->msg_qbytes = msq->q_qbytes;
	p->msg_lspid  = pid_vnr(msq->q_lspid);
	p->msg_lrpid  = pid_vnr(msq->q_lrpid);

	ipc_unlock_object(&msq->q_perm);
	rcu_read_unlock();
	return id;

out_unlock:
	rcu_read_unlock();
	return err;
}

long ksys_msgctl(int msqid, int cmd, struct msqid_ds __user *buf)
{
	int version;
	struct ipc_namespace *ns;
	struct msqid64_ds msqid64;
	int err;

	if (msqid < 0 || cmd < 0)
		return -EINVAL;

	version = ipc_parse_version(&cmd);
	ns = current->nsproxy->ipc_ns;

	switch (cmd) {
	case IPC_INFO:
	case MSG_INFO: {
		struct msginfo msginfo;
		err = msgctl_info(ns, msqid, cmd, &msginfo);
		if (err < 0)
			return err;
		if (copy_to_user(buf, &msginfo, sizeof(struct msginfo)))
			err = -EFAULT;
		return err;
	}
	case MSG_STAT:	/* msqid is an index rather than a msg queue id */
	case MSG_STAT_ANY:
	case IPC_STAT:
		err = msgctl_stat(ns, msqid, cmd, &msqid64);
		if (err < 0)
			return err;
		if (copy_msqid_to_user(buf, &msqid64, version))
			err = -EFAULT;
		return err;
	case IPC_SET:
		if (copy_msqid_from_user(&msqid64, buf, version))
			return -EFAULT;
		/* fallthru */
	case IPC_RMID:
		return msgctl_down(ns, msqid, cmd, &msqid64);
	default:
		return  -EINVAL;
	}
}

SYSCALL_DEFINE3(msgctl, int, msqid, int, cmd, struct msqid_ds __user *, buf)
{
	return ksys_msgctl(msqid, cmd, buf);
}

#ifdef CONFIG_COMPAT

struct compat_msqid_ds {
	struct compat_ipc_perm msg_perm;
	compat_uptr_t msg_first;
	compat_uptr_t msg_last;
	compat_time_t msg_stime;
	compat_time_t msg_rtime;
	compat_time_t msg_ctime;
	compat_ulong_t msg_lcbytes;
	compat_ulong_t msg_lqbytes;
	unsigned short msg_cbytes;
	unsigned short msg_qnum;
	unsigned short msg_qbytes;
	compat_ipc_pid_t msg_lspid;
	compat_ipc_pid_t msg_lrpid;
};

static int copy_compat_msqid_from_user(struct msqid64_ds *out, void __user *buf,
					int version)
{
	memset(out, 0, sizeof(*out));
	if (version == IPC_64) {
		struct compat_msqid64_ds __user *p = buf;
		if (get_compat_ipc64_perm(&out->msg_perm, &p->msg_perm))
			return -EFAULT;
		if (get_user(out->msg_qbytes, &p->msg_qbytes))
			return -EFAULT;
	} else {
		struct compat_msqid_ds __user *p = buf;
		if (get_compat_ipc_perm(&out->msg_perm, &p->msg_perm))
			return -EFAULT;
		if (get_user(out->msg_qbytes, &p->msg_qbytes))
			return -EFAULT;
	}
	return 0;
}

static int copy_compat_msqid_to_user(void __user *buf, struct msqid64_ds *in,
					int version)
{
	if (version == IPC_64) {
		struct compat_msqid64_ds v;
		memset(&v, 0, sizeof(v));
		to_compat_ipc64_perm(&v.msg_perm, &in->msg_perm);
		v.msg_stime = in->msg_stime;
		v.msg_rtime = in->msg_rtime;
		v.msg_ctime = in->msg_ctime;
		v.msg_cbytes = in->msg_cbytes;
		v.msg_qnum = in->msg_qnum;
		v.msg_qbytes = in->msg_qbytes;
		v.msg_lspid = in->msg_lspid;
		v.msg_lrpid = in->msg_lrpid;
		return copy_to_user(buf, &v, sizeof(v));
	} else {
		struct compat_msqid_ds v;
		memset(&v, 0, sizeof(v));
		to_compat_ipc_perm(&v.msg_perm, &in->msg_perm);
		v.msg_stime = in->msg_stime;
		v.msg_rtime = in->msg_rtime;
		v.msg_ctime = in->msg_ctime;
		v.msg_cbytes = in->msg_cbytes;
		v.msg_qnum = in->msg_qnum;
		v.msg_qbytes = in->msg_qbytes;
		v.msg_lspid = in->msg_lspid;
		v.msg_lrpid = in->msg_lrpid;
		return copy_to_user(buf, &v, sizeof(v));
	}
}

long compat_ksys_msgctl(int msqid, int cmd, void __user *uptr)
{
	struct ipc_namespace *ns;
	int err;
	struct msqid64_ds msqid64;
	int version = compat_ipc_parse_version(&cmd);

	ns = current->nsproxy->ipc_ns;

	if (msqid < 0 || cmd < 0)
		return -EINVAL;

	switch (cmd & (~IPC_64)) {
	case IPC_INFO:
	case MSG_INFO: {
		struct msginfo msginfo;
		err = msgctl_info(ns, msqid, cmd, &msginfo);
		if (err < 0)
			return err;
		if (copy_to_user(uptr, &msginfo, sizeof(struct msginfo)))
			err = -EFAULT;
		return err;
	}
	case IPC_STAT:
	case MSG_STAT:
	case MSG_STAT_ANY:
		err = msgctl_stat(ns, msqid, cmd, &msqid64);
		if (err < 0)
			return err;
		if (copy_compat_msqid_to_user(uptr, &msqid64, version))
			err = -EFAULT;
		return err;
	case IPC_SET:
		if (copy_compat_msqid_from_user(&msqid64, uptr, version))
			return -EFAULT;
		/* fallthru */
	case IPC_RMID:
		return msgctl_down(ns, msqid, cmd, &msqid64);
	default:
		return -EINVAL;
	}
}

COMPAT_SYSCALL_DEFINE3(msgctl, int, msqid, int, cmd, void __user *, uptr)
{
	return compat_ksys_msgctl(msqid, cmd, uptr);
}
#endif

static int testmsg(struct msg_msg *msg, long type, int mode)
{
	switch (mode) {
	case SEARCH_ANY:
	case SEARCH_NUMBER:
		return 1;
	case SEARCH_LESSEQUAL:
		if (msg->m_type <= type)
			return 1;
		break;
	case SEARCH_EQUAL:
		if (msg->m_type == type)
			return 1;
		break;
	case SEARCH_NOTEQUAL:
		if (msg->m_type != type)
			return 1;
		break;
	}
	return 0;
}

static inline int pipelined_send(struct msg_queue *msq, struct msg_msg *msg,
				 struct wake_q_head *wake_q)
{
	struct msg_receiver *msr, *t;

	list_for_each_entry_safe(msr, t, &msq->q_receivers, r_list) {
		if (testmsg(msg, msr->r_msgtype, msr->r_mode) &&
		    !security_msg_queue_msgrcv(&msq->q_perm, msg, msr->r_tsk,
					       msr->r_msgtype, msr->r_mode)) {

			list_del(&msr->r_list);
			if (msr->r_maxsize < msg->m_ts) {
				wake_q_add(wake_q, msr->r_tsk);
				WRITE_ONCE(msr->r_msg, ERR_PTR(-E2BIG));
			} else {
				ipc_update_pid(&msq->q_lrpid, task_pid(msr->r_tsk));
				msq->q_rtime = get_seconds();

				wake_q_add(wake_q, msr->r_tsk);
				WRITE_ONCE(msr->r_msg, msg);
				return 1;
			}
		}
	}

	return 0;
}

static long do_msgsnd(int msqid, long mtype, void __user *mtext,
		size_t msgsz, int msgflg)
{
	struct msg_queue *msq;
	struct msg_msg *msg;
	int err;
	struct ipc_namespace *ns;
	DEFINE_WAKE_Q(wake_q);

	ns = current->nsproxy->ipc_ns;

	if (msgsz > ns->msg_ctlmax || (long) msgsz < 0 || msqid < 0)
		return -EINVAL;
	if (mtype < 1)
		return -EINVAL;

	msg = load_msg(mtext, msgsz);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->m_type = mtype;
	msg->m_ts = msgsz;

	rcu_read_lock();
	msq = msq_obtain_object_check(ns, msqid);
	if (IS_ERR(msq)) {
		err = PTR_ERR(msq);
		goto out_unlock1;
	}

	ipc_lock_object(&msq->q_perm);

	for (;;) {
		struct msg_sender s;

		err = -EACCES;
		if (ipcperms(ns, &msq->q_perm, S_IWUGO))
			goto out_unlock0;

		/* raced with RMID? */
		if (!ipc_valid_object(&msq->q_perm)) {
			err = -EIDRM;
			goto out_unlock0;
		}

		err = security_msg_queue_msgsnd(&msq->q_perm, msg, msgflg);
		if (err)
			goto out_unlock0;

		if (msg_fits_inqueue(msq, msgsz))
			break;

		/* queue full, wait: */
		if (msgflg & IPC_NOWAIT) {
			err = -EAGAIN;
			goto out_unlock0;
		}

		/* enqueue the sender and prepare to block */
		ss_add(msq, &s, msgsz);

		if (!ipc_rcu_getref(&msq->q_perm)) {
			err = -EIDRM;
			goto out_unlock0;
		}

		ipc_unlock_object(&msq->q_perm);
		rcu_read_unlock();
		schedule();

		rcu_read_lock();
		ipc_lock_object(&msq->q_perm);

		ipc_rcu_putref(&msq->q_perm, msg_rcu_free);
		/* raced with RMID? */
		if (!ipc_valid_object(&msq->q_perm)) {
			err = -EIDRM;
			goto out_unlock0;
		}
		ss_del(&s);

		if (signal_pending(current)) {
			err = -ERESTARTNOHAND;
			goto out_unlock0;
		}

	}

	ipc_update_pid(&msq->q_lspid, task_tgid(current));
	msq->q_stime = get_seconds();

	if (!pipelined_send(msq, msg, &wake_q)) {
		/* no one is waiting for this message, enqueue it */
		list_add_tail(&msg->m_list, &msq->q_messages);
		msq->q_cbytes += msgsz;
		msq->q_qnum++;
		atomic_add(msgsz, &ns->msg_bytes);
		atomic_inc(&ns->msg_hdrs);
	}

	err = 0;
	msg = NULL;

out_unlock0:
	ipc_unlock_object(&msq->q_perm);
	wake_up_q(&wake_q);
out_unlock1:
	rcu_read_unlock();
	if (msg != NULL)
		free_msg(msg);
	return err;
}

long ksys_msgsnd(int msqid, struct msgbuf __user *msgp, size_t msgsz,
		 int msgflg)
{
	long mtype;

	if (get_user(mtype, &msgp->mtype))
		return -EFAULT;
	return do_msgsnd(msqid, mtype, msgp->mtext, msgsz, msgflg);
}

SYSCALL_DEFINE4(msgsnd, int, msqid, struct msgbuf __user *, msgp, size_t, msgsz,
		int, msgflg)
{
	return ksys_msgsnd(msqid, msgp, msgsz, msgflg);
}

#ifdef CONFIG_COMPAT

struct compat_msgbuf {
	compat_long_t mtype;
	char mtext[1];
};

long compat_ksys_msgsnd(int msqid, compat_uptr_t msgp,
		       compat_ssize_t msgsz, int msgflg)
{
	struct compat_msgbuf __user *up = compat_ptr(msgp);
	compat_long_t mtype;

	if (get_user(mtype, &up->mtype))
		return -EFAULT;
	return do_msgsnd(msqid, mtype, up->mtext, (ssize_t)msgsz, msgflg);
}

COMPAT_SYSCALL_DEFINE4(msgsnd, int, msqid, compat_uptr_t, msgp,
		       compat_ssize_t, msgsz, int, msgflg)
{
	return compat_ksys_msgsnd(msqid, msgp, msgsz, msgflg);
}
#endif

static inline int convert_mode(long *msgtyp, int msgflg)
{
	if (msgflg & MSG_COPY)
		return SEARCH_NUMBER;
	/*
	 *  find message of correct type.
	 *  msgtyp = 0 => get first.
	 *  msgtyp > 0 => get first message of matching type.
	 *  msgtyp < 0 => get message with least type must be < abs(msgtype).
	 */
	if (*msgtyp == 0)
		return SEARCH_ANY;
	if (*msgtyp < 0) {
		if (*msgtyp == LONG_MIN) /* -LONG_MIN is undefined */
			*msgtyp = LONG_MAX;
		else
			*msgtyp = -*msgtyp;
		return SEARCH_LESSEQUAL;
	}
	if (msgflg & MSG_EXCEPT)
		return SEARCH_NOTEQUAL;
	return SEARCH_EQUAL;
}

static long do_msg_fill(void __user *dest, struct msg_msg *msg, size_t bufsz)
{
	struct msgbuf __user *msgp = dest;
	size_t msgsz;

	if (put_user(msg->m_type, &msgp->mtype))
		return -EFAULT;

	msgsz = (bufsz > msg->m_ts) ? msg->m_ts : bufsz;
	if (store_msg(msgp->mtext, msg, msgsz))
		return -EFAULT;
	return msgsz;
}

#ifdef CONFIG_CHECKPOINT_RESTORE
/*
 * This function creates new kernel message structure, large enough to store
 * bufsz message bytes.
 */
static inline struct msg_msg *prepare_copy(void __user *buf, size_t bufsz)
{
	struct msg_msg *copy;

	/*
	 * Create dummy message to copy real message to.
	 */
	copy = load_msg(buf, bufsz);
	if (!IS_ERR(copy))
		copy->m_ts = bufsz;
	return copy;
}

static inline void free_copy(struct msg_msg *copy)
{
	if (copy)
		free_msg(copy);
}
#else
static inline struct msg_msg *prepare_copy(void __user *buf, size_t bufsz)
{
	return ERR_PTR(-ENOSYS);
}

static inline void free_copy(struct msg_msg *copy)
{
}
#endif

static struct msg_msg *find_msg(struct msg_queue *msq, long *msgtyp, int mode)
{
	struct msg_msg *msg, *found = NULL;
	long count = 0;

	list_for_each_entry(msg, &msq->q_messages, m_list) {
		if (testmsg(msg, *msgtyp, mode) &&
		    !security_msg_queue_msgrcv(&msq->q_perm, msg, current,
					       *msgtyp, mode)) {
			if (mode == SEARCH_LESSEQUAL && msg->m_type != 1) {
				*msgtyp = msg->m_type - 1;
				found = msg;
			} else if (mode == SEARCH_NUMBER) {
				if (*msgtyp == count)
					return msg;
			} else
				return msg;
			count++;
		}
	}

	return found ?: ERR_PTR(-EAGAIN);
}

static long do_msgrcv(int msqid, void __user *buf, size_t bufsz, long msgtyp, int msgflg,
	       long (*msg_handler)(void __user *, struct msg_msg *, size_t))
{
	int mode;
	struct msg_queue *msq;
	struct ipc_namespace *ns;
	struct msg_msg *msg, *copy = NULL;
	DEFINE_WAKE_Q(wake_q);

	ns = current->nsproxy->ipc_ns;

	if (msqid < 0 || (long) bufsz < 0)
		return -EINVAL;

	if (msgflg & MSG_COPY) {
		if ((msgflg & MSG_EXCEPT) || !(msgflg & IPC_NOWAIT))
			return -EINVAL;
		copy = prepare_copy(buf, min_t(size_t, bufsz, ns->msg_ctlmax));
		if (IS_ERR(copy))
			return PTR_ERR(copy);
	}
	mode = convert_mode(&msgtyp, msgflg);

	rcu_read_lock();
	msq = msq_obtain_object_check(ns, msqid);
	if (IS_ERR(msq)) {
		rcu_read_unlock();
		free_copy(copy);
		return PTR_ERR(msq);
	}

	for (;;) {
		struct msg_receiver msr_d;

		msg = ERR_PTR(-EACCES);
		if (ipcperms(ns, &msq->q_perm, S_IRUGO))
			goto out_unlock1;

		ipc_lock_object(&msq->q_perm);

		/* raced with RMID? */
		if (!ipc_valid_object(&msq->q_perm)) {
			msg = ERR_PTR(-EIDRM);
			goto out_unlock0;
		}

		msg = find_msg(msq, &msgtyp, mode);
		if (!IS_ERR(msg)) {
			/*
			 * Found a suitable message.
			 * Unlink it from the queue.
			 */
			if ((bufsz < msg->m_ts) && !(msgflg & MSG_NOERROR)) {
				msg = ERR_PTR(-E2BIG);
				goto out_unlock0;
			}
			/*
			 * If we are copying, then do not unlink message and do
			 * not update queue parameters.
			 */
			if (msgflg & MSG_COPY) {
				msg = copy_msg(msg, copy);
				goto out_unlock0;
			}

			list_del(&msg->m_list);
			msq->q_qnum--;
			msq->q_rtime = get_seconds();
			ipc_update_pid(&msq->q_lrpid, task_tgid(current));
			msq->q_cbytes -= msg->m_ts;
			atomic_sub(msg->m_ts, &ns->msg_bytes);
			atomic_dec(&ns->msg_hdrs);
			ss_wakeup(msq, &wake_q, false);

			goto out_unlock0;
		}

		/* No message waiting. Wait for a message */
		if (msgflg & IPC_NOWAIT) {
			msg = ERR_PTR(-ENOMSG);
			goto out_unlock0;
		}

		list_add_tail(&msr_d.r_list, &msq->q_receivers);
		msr_d.r_tsk = current;
		msr_d.r_msgtype = msgtyp;
		msr_d.r_mode = mode;
		if (msgflg & MSG_NOERROR)
			msr_d.r_maxsize = INT_MAX;
		else
			msr_d.r_maxsize = bufsz;
		msr_d.r_msg = ERR_PTR(-EAGAIN);
		__set_current_state(TASK_INTERRUPTIBLE);

		ipc_unlock_object(&msq->q_perm);
		rcu_read_unlock();
		schedule();

		/*
		 * Lockless receive, part 1:
		 * We don't hold a reference to the queue and getting a
		 * reference would defeat the idea of a lockless operation,
		 * thus the code relies on rcu to guarantee the existence of
		 * msq:
		 * Prior to destruction, expunge_all(-EIRDM) changes r_msg.
		 * Thus if r_msg is -EAGAIN, then the queue not yet destroyed.
		 */
		rcu_read_lock();

		/*
		 * Lockless receive, part 2:
		 * The work in pipelined_send() and expunge_all():
		 * - Set pointer to message
		 * - Queue the receiver task for later wakeup
		 * - Wake up the process after the lock is dropped.
		 *
		 * Should the process wake up before this wakeup (due to a
		 * signal) it will either see the message and continue ...
		 */
		msg = READ_ONCE(msr_d.r_msg);
		if (msg != ERR_PTR(-EAGAIN))
			goto out_unlock1;

		 /*
		  * ... or see -EAGAIN, acquire the lock to check the message
		  * again.
		  */
		ipc_lock_object(&msq->q_perm);

		msg = msr_d.r_msg;
		if (msg != ERR_PTR(-EAGAIN))
			goto out_unlock0;

		list_del(&msr_d.r_list);
		if (signal_pending(current)) {
			msg = ERR_PTR(-ERESTARTNOHAND);
			goto out_unlock0;
		}

		ipc_unlock_object(&msq->q_perm);
	}

out_unlock0:
	ipc_unlock_object(&msq->q_perm);
	wake_up_q(&wake_q);
out_unlock1:
	rcu_read_unlock();
	if (IS_ERR(msg)) {
		free_copy(copy);
		return PTR_ERR(msg);
	}

	bufsz = msg_handler(buf, msg, bufsz);
	free_msg(msg);

	return bufsz;
}

long ksys_msgrcv(int msqid, struct msgbuf __user *msgp, size_t msgsz,
		 long msgtyp, int msgflg)
{
	return do_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg, do_msg_fill);
}

SYSCALL_DEFINE5(msgrcv, int, msqid, struct msgbuf __user *, msgp, size_t, msgsz,
		long, msgtyp, int, msgflg)
{
	return ksys_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);
}

#ifdef CONFIG_COMPAT
static long compat_do_msg_fill(void __user *dest, struct msg_msg *msg, size_t bufsz)
{
	struct compat_msgbuf __user *msgp = dest;
	size_t msgsz;

	if (put_user(msg->m_type, &msgp->mtype))
		return -EFAULT;

	msgsz = (bufsz > msg->m_ts) ? msg->m_ts : bufsz;
	if (store_msg(msgp->mtext, msg, msgsz))
		return -EFAULT;
	return msgsz;
}

long compat_ksys_msgrcv(int msqid, compat_uptr_t msgp, compat_ssize_t msgsz,
			compat_long_t msgtyp, int msgflg)
{
	return do_msgrcv(msqid, compat_ptr(msgp), (ssize_t)msgsz, (long)msgtyp,
			 msgflg, compat_do_msg_fill);
}

COMPAT_SYSCALL_DEFINE5(msgrcv, int, msqid, compat_uptr_t, msgp,
		       compat_ssize_t, msgsz, compat_long_t, msgtyp,
		       int, msgflg)
{
	return compat_ksys_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);
}
#endif

int msg_init_ns(struct ipc_namespace *ns)
{
	ns->msg_ctlmax = MSGMAX;
	ns->msg_ctlmnb = MSGMNB;
	ns->msg_ctlmni = MSGMNI;

	atomic_set(&ns->msg_bytes, 0);
	atomic_set(&ns->msg_hdrs, 0);
	return ipc_init_ids(&ns->ids[IPC_MSG_IDS]);
}

#ifdef CONFIG_IPC_NS
void msg_exit_ns(struct ipc_namespace *ns)
{
	free_ipcs(ns, &msg_ids(ns), freeque);
	idr_destroy(&ns->ids[IPC_MSG_IDS].ipcs_idr);
	rhashtable_destroy(&ns->ids[IPC_MSG_IDS].key_ht);
}
#endif

#ifdef CONFIG_PROC_FS
static int sysvipc_msg_proc_show(struct seq_file *s, void *it)
{
	struct pid_namespace *pid_ns = ipc_seq_pid_ns(s);
	struct user_namespace *user_ns = seq_user_ns(s);
	struct kern_ipc_perm *ipcp = it;
	struct msg_queue *msq = container_of(ipcp, struct msg_queue, q_perm);

	seq_printf(s,
		   "%10d %10d  %4o  %10lu %10lu %5u %5u %5u %5u %5u %5u %10llu %10llu %10llu\n",
		   msq->q_perm.key,
		   msq->q_perm.id,
		   msq->q_perm.mode,
		   msq->q_cbytes,
		   msq->q_qnum,
		   pid_nr_ns(msq->q_lspid, pid_ns),
		   pid_nr_ns(msq->q_lrpid, pid_ns),
		   from_kuid_munged(user_ns, msq->q_perm.uid),
		   from_kgid_munged(user_ns, msq->q_perm.gid),
		   from_kuid_munged(user_ns, msq->q_perm.cuid),
		   from_kgid_munged(user_ns, msq->q_perm.cgid),
		   msq->q_stime,
		   msq->q_rtime,
		   msq->q_ctime);

	return 0;
}
#endif

int __init msg_init(void)
{
	const int err = msg_init_ns(&init_ipc_ns);

	ipc_init_proc_interface("sysvipc/msg",
				"       key      msqid perms      cbytes       qnum lspid lrpid   uid   gid  cuid  cgid      stime      rtime      ctime\n",
				IPC_MSG_IDS, sysvipc_msg_proc_show);
	return err;
}
