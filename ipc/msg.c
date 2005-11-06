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
 * (c) 1999 Manfred Spraul <manfreds@colorfullife.com>
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/msg.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/security.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/seq_file.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include "util.h"

/* sysctl: */
int msg_ctlmax = MSGMAX;
int msg_ctlmnb = MSGMNB;
int msg_ctlmni = MSGMNI;

/* one msg_receiver structure for each sleeping receiver */
struct msg_receiver {
	struct list_head r_list;
	struct task_struct* r_tsk;

	int r_mode;
	long r_msgtype;
	long r_maxsize;

	struct msg_msg* volatile r_msg;
};

/* one msg_sender for each sleeping sender */
struct msg_sender {
	struct list_head list;
	struct task_struct* tsk;
};

#define SEARCH_ANY		1
#define SEARCH_EQUAL		2
#define SEARCH_NOTEQUAL		3
#define SEARCH_LESSEQUAL	4

static atomic_t msg_bytes = ATOMIC_INIT(0);
static atomic_t msg_hdrs = ATOMIC_INIT(0);

static struct ipc_ids msg_ids;

#define msg_lock(id)	((struct msg_queue*)ipc_lock(&msg_ids,id))
#define msg_unlock(msq)	ipc_unlock(&(msq)->q_perm)
#define msg_rmid(id)	((struct msg_queue*)ipc_rmid(&msg_ids,id))
#define msg_checkid(msq, msgid)	\
	ipc_checkid(&msg_ids,&msq->q_perm,msgid)
#define msg_buildid(id, seq) \
	ipc_buildid(&msg_ids, id, seq)

static void freeque (struct msg_queue *msq, int id);
static int newque (key_t key, int msgflg);
#ifdef CONFIG_PROC_FS
static int sysvipc_msg_proc_show(struct seq_file *s, void *it);
#endif

void __init msg_init (void)
{
	ipc_init_ids(&msg_ids,msg_ctlmni);
	ipc_init_proc_interface("sysvipc/msg",
				"       key      msqid perms      cbytes       qnum lspid lrpid   uid   gid  cuid  cgid      stime      rtime      ctime\n",
				&msg_ids,
				sysvipc_msg_proc_show);
}

static int newque (key_t key, int msgflg)
{
	int id;
	int retval;
	struct msg_queue *msq;

	msq  = ipc_rcu_alloc(sizeof(*msq));
	if (!msq) 
		return -ENOMEM;

	msq->q_perm.mode = (msgflg & S_IRWXUGO);
	msq->q_perm.key = key;

	msq->q_perm.security = NULL;
	retval = security_msg_queue_alloc(msq);
	if (retval) {
		ipc_rcu_putref(msq);
		return retval;
	}

	id = ipc_addid(&msg_ids, &msq->q_perm, msg_ctlmni);
	if(id == -1) {
		security_msg_queue_free(msq);
		ipc_rcu_putref(msq);
		return -ENOSPC;
	}

	msq->q_id = msg_buildid(id,msq->q_perm.seq);
	msq->q_stime = msq->q_rtime = 0;
	msq->q_ctime = get_seconds();
	msq->q_cbytes = msq->q_qnum = 0;
	msq->q_qbytes = msg_ctlmnb;
	msq->q_lspid = msq->q_lrpid = 0;
	INIT_LIST_HEAD(&msq->q_messages);
	INIT_LIST_HEAD(&msq->q_receivers);
	INIT_LIST_HEAD(&msq->q_senders);
	msg_unlock(msq);

	return msq->q_id;
}

static inline void ss_add(struct msg_queue* msq, struct msg_sender* mss)
{
	mss->tsk=current;
	current->state=TASK_INTERRUPTIBLE;
	list_add_tail(&mss->list,&msq->q_senders);
}

static inline void ss_del(struct msg_sender* mss)
{
	if(mss->list.next != NULL)
		list_del(&mss->list);
}

static void ss_wakeup(struct list_head* h, int kill)
{
	struct list_head *tmp;

	tmp = h->next;
	while (tmp != h) {
		struct msg_sender* mss;
		
		mss = list_entry(tmp,struct msg_sender,list);
		tmp = tmp->next;
		if(kill)
			mss->list.next=NULL;
		wake_up_process(mss->tsk);
	}
}

static void expunge_all(struct msg_queue* msq, int res)
{
	struct list_head *tmp;

	tmp = msq->q_receivers.next;
	while (tmp != &msq->q_receivers) {
		struct msg_receiver* msr;
		
		msr = list_entry(tmp,struct msg_receiver,r_list);
		tmp = tmp->next;
		msr->r_msg = NULL;
		wake_up_process(msr->r_tsk);
		smp_mb();
		msr->r_msg = ERR_PTR(res);
	}
}
/* 
 * freeque() wakes up waiters on the sender and receiver waiting queue, 
 * removes the message queue from message queue ID 
 * array, and cleans up all the messages associated with this queue.
 *
 * msg_ids.sem and the spinlock for this message queue is hold
 * before freeque() is called. msg_ids.sem remains locked on exit.
 */
static void freeque (struct msg_queue *msq, int id)
{
	struct list_head *tmp;

	expunge_all(msq,-EIDRM);
	ss_wakeup(&msq->q_senders,1);
	msq = msg_rmid(id);
	msg_unlock(msq);
		
	tmp = msq->q_messages.next;
	while(tmp != &msq->q_messages) {
		struct msg_msg* msg = list_entry(tmp,struct msg_msg,m_list);
		tmp = tmp->next;
		atomic_dec(&msg_hdrs);
		free_msg(msg);
	}
	atomic_sub(msq->q_cbytes, &msg_bytes);
	security_msg_queue_free(msq);
	ipc_rcu_putref(msq);
}

asmlinkage long sys_msgget (key_t key, int msgflg)
{
	int id, ret = -EPERM;
	struct msg_queue *msq;
	
	down(&msg_ids.sem);
	if (key == IPC_PRIVATE) 
		ret = newque(key, msgflg);
	else if ((id = ipc_findkey(&msg_ids, key)) == -1) { /* key not used */
		if (!(msgflg & IPC_CREAT))
			ret = -ENOENT;
		else
			ret = newque(key, msgflg);
	} else if (msgflg & IPC_CREAT && msgflg & IPC_EXCL) {
		ret = -EEXIST;
	} else {
		msq = msg_lock(id);
		if(msq==NULL)
			BUG();
		if (ipcperms(&msq->q_perm, msgflg))
			ret = -EACCES;
		else {
			int qid = msg_buildid(id, msq->q_perm.seq);
		    	ret = security_msg_queue_associate(msq, msgflg);
			if (!ret)
				ret = qid;
		}
		msg_unlock(msq);
	}
	up(&msg_ids.sem);
	return ret;
}

static inline unsigned long copy_msqid_to_user(void __user *buf, struct msqid64_ds *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user (buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct msqid_ds out;

		memset(&out,0,sizeof(out));

		ipc64_perm_to_ipc_perm(&in->msg_perm, &out.msg_perm);

		out.msg_stime		= in->msg_stime;
		out.msg_rtime		= in->msg_rtime;
		out.msg_ctime		= in->msg_ctime;

		if(in->msg_cbytes > USHRT_MAX)
			out.msg_cbytes	= USHRT_MAX;
		else
			out.msg_cbytes	= in->msg_cbytes;
		out.msg_lcbytes		= in->msg_cbytes;

		if(in->msg_qnum > USHRT_MAX)
			out.msg_qnum	= USHRT_MAX;
		else
			out.msg_qnum	= in->msg_qnum;

		if(in->msg_qbytes > USHRT_MAX)
			out.msg_qbytes	= USHRT_MAX;
		else
			out.msg_qbytes	= in->msg_qbytes;
		out.msg_lqbytes		= in->msg_qbytes;

		out.msg_lspid		= in->msg_lspid;
		out.msg_lrpid		= in->msg_lrpid;

		return copy_to_user (buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

struct msq_setbuf {
	unsigned long	qbytes;
	uid_t		uid;
	gid_t		gid;
	mode_t		mode;
};

static inline unsigned long copy_msqid_from_user(struct msq_setbuf *out, void __user *buf, int version)
{
	switch(version) {
	case IPC_64:
	    {
		struct msqid64_ds tbuf;

		if (copy_from_user (&tbuf, buf, sizeof (tbuf)))
			return -EFAULT;

		out->qbytes		= tbuf.msg_qbytes;
		out->uid		= tbuf.msg_perm.uid;
		out->gid		= tbuf.msg_perm.gid;
		out->mode		= tbuf.msg_perm.mode;

		return 0;
	    }
	case IPC_OLD:
	    {
		struct msqid_ds tbuf_old;

		if (copy_from_user (&tbuf_old, buf, sizeof (tbuf_old)))
			return -EFAULT;

		out->uid		= tbuf_old.msg_perm.uid;
		out->gid		= tbuf_old.msg_perm.gid;
		out->mode		= tbuf_old.msg_perm.mode;

		if(tbuf_old.msg_qbytes == 0)
			out->qbytes	= tbuf_old.msg_lqbytes;
		else
			out->qbytes	= tbuf_old.msg_qbytes;

		return 0;
	    }
	default:
		return -EINVAL;
	}
}

asmlinkage long sys_msgctl (int msqid, int cmd, struct msqid_ds __user *buf)
{
	int err, version;
	struct msg_queue *msq;
	struct msq_setbuf setbuf;
	struct kern_ipc_perm *ipcp;
	
	if (msqid < 0 || cmd < 0)
		return -EINVAL;

	version = ipc_parse_version(&cmd);

	switch (cmd) {
	case IPC_INFO: 
	case MSG_INFO: 
	{ 
		struct msginfo msginfo;
		int max_id;
		if (!buf)
			return -EFAULT;
		/* We must not return kernel stack data.
		 * due to padding, it's not enough
		 * to set all member fields.
		 */

		err = security_msg_queue_msgctl(NULL, cmd);
		if (err)
			return err;

		memset(&msginfo,0,sizeof(msginfo));	
		msginfo.msgmni = msg_ctlmni;
		msginfo.msgmax = msg_ctlmax;
		msginfo.msgmnb = msg_ctlmnb;
		msginfo.msgssz = MSGSSZ;
		msginfo.msgseg = MSGSEG;
		down(&msg_ids.sem);
		if (cmd == MSG_INFO) {
			msginfo.msgpool = msg_ids.in_use;
			msginfo.msgmap = atomic_read(&msg_hdrs);
			msginfo.msgtql = atomic_read(&msg_bytes);
		} else {
			msginfo.msgmap = MSGMAP;
			msginfo.msgpool = MSGPOOL;
			msginfo.msgtql = MSGTQL;
		}
		max_id = msg_ids.max_id;
		up(&msg_ids.sem);
		if (copy_to_user (buf, &msginfo, sizeof(struct msginfo)))
			return -EFAULT;
		return (max_id < 0) ? 0: max_id;
	}
	case MSG_STAT:
	case IPC_STAT:
	{
		struct msqid64_ds tbuf;
		int success_return;
		if (!buf)
			return -EFAULT;
		if(cmd == MSG_STAT && msqid >= msg_ids.entries->size)
			return -EINVAL;

		memset(&tbuf,0,sizeof(tbuf));

		msq = msg_lock(msqid);
		if (msq == NULL)
			return -EINVAL;

		if(cmd == MSG_STAT) {
			success_return = msg_buildid(msqid, msq->q_perm.seq);
		} else {
			err = -EIDRM;
			if (msg_checkid(msq,msqid))
				goto out_unlock;
			success_return = 0;
		}
		err = -EACCES;
		if (ipcperms (&msq->q_perm, S_IRUGO))
			goto out_unlock;

		err = security_msg_queue_msgctl(msq, cmd);
		if (err)
			goto out_unlock;

		kernel_to_ipc64_perm(&msq->q_perm, &tbuf.msg_perm);
		tbuf.msg_stime  = msq->q_stime;
		tbuf.msg_rtime  = msq->q_rtime;
		tbuf.msg_ctime  = msq->q_ctime;
		tbuf.msg_cbytes = msq->q_cbytes;
		tbuf.msg_qnum   = msq->q_qnum;
		tbuf.msg_qbytes = msq->q_qbytes;
		tbuf.msg_lspid  = msq->q_lspid;
		tbuf.msg_lrpid  = msq->q_lrpid;
		msg_unlock(msq);
		if (copy_msqid_to_user(buf, &tbuf, version))
			return -EFAULT;
		return success_return;
	}
	case IPC_SET:
		if (!buf)
			return -EFAULT;
		if (copy_msqid_from_user (&setbuf, buf, version))
			return -EFAULT;
		if ((err = audit_ipc_perms(setbuf.qbytes, setbuf.uid, setbuf.gid, setbuf.mode)))
			return err;
		break;
	case IPC_RMID:
		break;
	default:
		return  -EINVAL;
	}

	down(&msg_ids.sem);
	msq = msg_lock(msqid);
	err=-EINVAL;
	if (msq == NULL)
		goto out_up;

	err = -EIDRM;
	if (msg_checkid(msq,msqid))
		goto out_unlock_up;
	ipcp = &msq->q_perm;
	err = -EPERM;
	if (current->euid != ipcp->cuid && 
	    current->euid != ipcp->uid && !capable(CAP_SYS_ADMIN))
	    /* We _could_ check for CAP_CHOWN above, but we don't */
		goto out_unlock_up;

	err = security_msg_queue_msgctl(msq, cmd);
	if (err)
		goto out_unlock_up;

	switch (cmd) {
	case IPC_SET:
	{
		err = -EPERM;
		if (setbuf.qbytes > msg_ctlmnb && !capable(CAP_SYS_RESOURCE))
			goto out_unlock_up;

		msq->q_qbytes = setbuf.qbytes;

		ipcp->uid = setbuf.uid;
		ipcp->gid = setbuf.gid;
		ipcp->mode = (ipcp->mode & ~S_IRWXUGO) | 
			(S_IRWXUGO & setbuf.mode);
		msq->q_ctime = get_seconds();
		/* sleeping receivers might be excluded by
		 * stricter permissions.
		 */
		expunge_all(msq,-EAGAIN);
		/* sleeping senders might be able to send
		 * due to a larger queue size.
		 */
		ss_wakeup(&msq->q_senders,0);
		msg_unlock(msq);
		break;
	}
	case IPC_RMID:
		freeque (msq, msqid); 
		break;
	}
	err = 0;
out_up:
	up(&msg_ids.sem);
	return err;
out_unlock_up:
	msg_unlock(msq);
	goto out_up;
out_unlock:
	msg_unlock(msq);
	return err;
}

static int testmsg(struct msg_msg* msg,long type,int mode)
{
	switch(mode)
	{
		case SEARCH_ANY:
			return 1;
		case SEARCH_LESSEQUAL:
			if(msg->m_type <=type)
				return 1;
			break;
		case SEARCH_EQUAL:
			if(msg->m_type == type)
				return 1;
			break;
		case SEARCH_NOTEQUAL:
			if(msg->m_type != type)
				return 1;
			break;
	}
	return 0;
}

static inline int pipelined_send(struct msg_queue* msq, struct msg_msg* msg)
{
	struct list_head* tmp;

	tmp = msq->q_receivers.next;
	while (tmp != &msq->q_receivers) {
		struct msg_receiver* msr;
		msr = list_entry(tmp,struct msg_receiver,r_list);
		tmp = tmp->next;
		if(testmsg(msg,msr->r_msgtype,msr->r_mode) &&
		   !security_msg_queue_msgrcv(msq, msg, msr->r_tsk, msr->r_msgtype, msr->r_mode)) {
			list_del(&msr->r_list);
			if(msr->r_maxsize < msg->m_ts) {
				msr->r_msg = NULL;
				wake_up_process(msr->r_tsk);
				smp_mb();
				msr->r_msg = ERR_PTR(-E2BIG);
			} else {
				msr->r_msg = NULL;
				msq->q_lrpid = msr->r_tsk->pid;
				msq->q_rtime = get_seconds();
				wake_up_process(msr->r_tsk);
				smp_mb();
				msr->r_msg = msg;
				return 1;
			}
		}
	}
	return 0;
}

asmlinkage long sys_msgsnd (int msqid, struct msgbuf __user *msgp, size_t msgsz, int msgflg)
{
	struct msg_queue *msq;
	struct msg_msg *msg;
	long mtype;
	int err;
	
	if (msgsz > msg_ctlmax || (long) msgsz < 0 || msqid < 0)
		return -EINVAL;
	if (get_user(mtype, &msgp->mtype))
		return -EFAULT; 
	if (mtype < 1)
		return -EINVAL;

	msg = load_msg(msgp->mtext, msgsz);
	if(IS_ERR(msg))
		return PTR_ERR(msg);

	msg->m_type = mtype;
	msg->m_ts = msgsz;

	msq = msg_lock(msqid);
	err=-EINVAL;
	if(msq==NULL)
		goto out_free;

	err= -EIDRM;
	if (msg_checkid(msq,msqid))
		goto out_unlock_free;

	for (;;) {
		struct msg_sender s;

		err=-EACCES;
		if (ipcperms(&msq->q_perm, S_IWUGO))
			goto out_unlock_free;

		err = security_msg_queue_msgsnd(msq, msg, msgflg);
		if (err)
			goto out_unlock_free;

		if(msgsz + msq->q_cbytes <= msq->q_qbytes &&
				1 + msq->q_qnum <= msq->q_qbytes) {
			break;
		}

		/* queue full, wait: */
		if(msgflg&IPC_NOWAIT) {
			err=-EAGAIN;
			goto out_unlock_free;
		}
		ss_add(msq, &s);
		ipc_rcu_getref(msq);
		msg_unlock(msq);
		schedule();

		ipc_lock_by_ptr(&msq->q_perm);
		ipc_rcu_putref(msq);
		if (msq->q_perm.deleted) {
			err = -EIDRM;
			goto out_unlock_free;
		}
		ss_del(&s);
		
		if (signal_pending(current)) {
			err=-ERESTARTNOHAND;
			goto out_unlock_free;
		}
	}

	msq->q_lspid = current->tgid;
	msq->q_stime = get_seconds();

	if(!pipelined_send(msq,msg)) {
		/* noone is waiting for this message, enqueue it */
		list_add_tail(&msg->m_list,&msq->q_messages);
		msq->q_cbytes += msgsz;
		msq->q_qnum++;
		atomic_add(msgsz,&msg_bytes);
		atomic_inc(&msg_hdrs);
	}
	
	err = 0;
	msg = NULL;

out_unlock_free:
	msg_unlock(msq);
out_free:
	if(msg!=NULL)
		free_msg(msg);
	return err;
}

static inline int convert_mode(long* msgtyp, int msgflg)
{
	/* 
	 *  find message of correct type.
	 *  msgtyp = 0 => get first.
	 *  msgtyp > 0 => get first message of matching type.
	 *  msgtyp < 0 => get message with least type must be < abs(msgtype).  
	 */
	if(*msgtyp==0)
		return SEARCH_ANY;
	if(*msgtyp<0) {
		*msgtyp=-(*msgtyp);
		return SEARCH_LESSEQUAL;
	}
	if(msgflg & MSG_EXCEPT)
		return SEARCH_NOTEQUAL;
	return SEARCH_EQUAL;
}

asmlinkage long sys_msgrcv (int msqid, struct msgbuf __user *msgp, size_t msgsz,
			    long msgtyp, int msgflg)
{
	struct msg_queue *msq;
	struct msg_msg *msg;
	int mode;

	if (msqid < 0 || (long) msgsz < 0)
		return -EINVAL;
	mode = convert_mode(&msgtyp,msgflg);

	msq = msg_lock(msqid);
	if(msq==NULL)
		return -EINVAL;

	msg = ERR_PTR(-EIDRM);
	if (msg_checkid(msq,msqid))
		goto out_unlock;

	for (;;) {
		struct msg_receiver msr_d;
		struct list_head* tmp;

		msg = ERR_PTR(-EACCES);
		if (ipcperms (&msq->q_perm, S_IRUGO))
			goto out_unlock;

		msg = ERR_PTR(-EAGAIN);
		tmp = msq->q_messages.next;
		while (tmp != &msq->q_messages) {
			struct msg_msg *walk_msg;
			walk_msg = list_entry(tmp,struct msg_msg,m_list);
			if(testmsg(walk_msg,msgtyp,mode) &&
			   !security_msg_queue_msgrcv(msq, walk_msg, current, msgtyp, mode)) {
				msg = walk_msg;
				if(mode == SEARCH_LESSEQUAL && walk_msg->m_type != 1) {
					msg=walk_msg;
					msgtyp=walk_msg->m_type-1;
				} else {
					msg=walk_msg;
					break;
				}
			}
			tmp = tmp->next;
		}
		if(!IS_ERR(msg)) {
			/* Found a suitable message. Unlink it from the queue. */
			if ((msgsz < msg->m_ts) && !(msgflg & MSG_NOERROR)) {
				msg = ERR_PTR(-E2BIG);
				goto out_unlock;
			}
			list_del(&msg->m_list);
			msq->q_qnum--;
			msq->q_rtime = get_seconds();
			msq->q_lrpid = current->tgid;
			msq->q_cbytes -= msg->m_ts;
			atomic_sub(msg->m_ts,&msg_bytes);
			atomic_dec(&msg_hdrs);
			ss_wakeup(&msq->q_senders,0);
			msg_unlock(msq);
			break;
		}
		/* No message waiting. Wait for a message */
		if (msgflg & IPC_NOWAIT) {
			msg = ERR_PTR(-ENOMSG);
			goto out_unlock;
		}
		list_add_tail(&msr_d.r_list,&msq->q_receivers);
		msr_d.r_tsk = current;
		msr_d.r_msgtype = msgtyp;
		msr_d.r_mode = mode;
		if(msgflg & MSG_NOERROR)
			msr_d.r_maxsize = INT_MAX;
		 else
			msr_d.r_maxsize = msgsz;
		msr_d.r_msg = ERR_PTR(-EAGAIN);
		current->state = TASK_INTERRUPTIBLE;
		msg_unlock(msq);

		schedule();

		/* Lockless receive, part 1:
		 * Disable preemption.  We don't hold a reference to the queue
		 * and getting a reference would defeat the idea of a lockless
		 * operation, thus the code relies on rcu to guarantee the
		 * existance of msq:
		 * Prior to destruction, expunge_all(-EIRDM) changes r_msg.
		 * Thus if r_msg is -EAGAIN, then the queue not yet destroyed.
		 * rcu_read_lock() prevents preemption between reading r_msg
		 * and the spin_lock() inside ipc_lock_by_ptr().
		 */
		rcu_read_lock();

		/* Lockless receive, part 2:
		 * Wait until pipelined_send or expunge_all are outside of
		 * wake_up_process(). There is a race with exit(), see
		 * ipc/mqueue.c for the details.
		 */
		msg = (struct msg_msg*) msr_d.r_msg;
		while (msg == NULL) {
			cpu_relax();
			msg = (struct msg_msg*) msr_d.r_msg;
		}

		/* Lockless receive, part 3:
		 * If there is a message or an error then accept it without
		 * locking.
		 */
		if(msg != ERR_PTR(-EAGAIN)) {
			rcu_read_unlock();
			break;
		}

		/* Lockless receive, part 3:
		 * Acquire the queue spinlock.
		 */
		ipc_lock_by_ptr(&msq->q_perm);
		rcu_read_unlock();

		/* Lockless receive, part 4:
		 * Repeat test after acquiring the spinlock.
		 */
		msg = (struct msg_msg*)msr_d.r_msg;
		if(msg != ERR_PTR(-EAGAIN))
			goto out_unlock;

		list_del(&msr_d.r_list);
		if (signal_pending(current)) {
			msg = ERR_PTR(-ERESTARTNOHAND);
out_unlock:
			msg_unlock(msq);
			break;
		}
	}
	if (IS_ERR(msg))
       		return PTR_ERR(msg);

	msgsz = (msgsz > msg->m_ts) ? msg->m_ts : msgsz;
	if (put_user (msg->m_type, &msgp->mtype) ||
	    store_msg(msgp->mtext, msg, msgsz)) {
		    msgsz = -EFAULT;
	}
	free_msg(msg);
	return msgsz;
}

#ifdef CONFIG_PROC_FS
static int sysvipc_msg_proc_show(struct seq_file *s, void *it)
{
	struct msg_queue *msq = it;

	return seq_printf(s,
			  "%10d %10d  %4o  %10lu %10lu %5u %5u %5u %5u %5u %5u %10lu %10lu %10lu\n",
			  msq->q_perm.key,
			  msq->q_id,
			  msq->q_perm.mode,
			  msq->q_cbytes,
			  msq->q_qnum,
			  msq->q_lspid,
			  msq->q_lrpid,
			  msq->q_perm.uid,
			  msq->q_perm.gid,
			  msq->q_perm.cuid,
			  msq->q_perm.cgid,
			  msq->q_stime,
			  msq->q_rtime,
			  msq->q_ctime);
}
#endif
