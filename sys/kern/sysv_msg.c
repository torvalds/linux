/*	$OpenBSD: sysv_msg.c,v 1.41 2023/04/11 00:45:09 jsg Exp $	*/
/*	$NetBSD: sysv_msg.c,v 1.19 1996/02/09 19:00:18 christos Exp $	*/
/*
 * Copyright (c) 2009 Bret S. Lambert <blambert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Implementation of SVID messages
 *
 * Author:  Daniel Boulet
 *
 * Copyright 1993 Daniel Boulet and RTMX Inc.
 *
 * This system call was implemented by Daniel Boulet under contract from RTMX.
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/syscallargs.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

struct que *que_create(key_t, struct ucred *, int);
struct que *que_lookup(int);
struct que *que_key_lookup(key_t);
void que_wakewriters(void);
void que_free(struct que *);
struct msg *msg_create(struct que *);
void msg_free(struct msg *);
void msg_enqueue(struct que *, struct msg *, struct proc *);
void msg_dequeue(struct que *, struct msg *, struct proc *);
struct msg *msg_lookup(struct que *, int);
int msg_copyin(struct msg *, const char *, size_t, struct proc *);
int msg_copyout(struct msg *, char *, size_t *, struct proc *);

struct	pool sysvmsgpl;
struct	msginfo msginfo;

TAILQ_HEAD(, que) msg_queues;

int num_ques;
int num_msgs;
int sequence;
int maxmsgs;

void
msginit(void)
{
	msginfo.msgmax = MSGMAX;
	msginfo.msgmni = MSGMNI;
	msginfo.msgmnb = MSGMNB;
	msginfo.msgtql = MSGTQL;
	msginfo.msgssz = MSGSSZ;
	msginfo.msgseg = MSGSEG;

	pool_init(&sysvmsgpl, sizeof(struct msg), 0, IPL_NONE, PR_WAITOK,
	    "sysvmsgpl", NULL);

	TAILQ_INIT(&msg_queues);

	num_ques = 0;
	num_msgs = 0;
	sequence = 1;
	maxmsgs = 0;
}

int
sys_msgctl(struct proc *p, void *v, register_t *retval)
{
	struct sys_msgctl_args /* {
		syscallarg(int) msqid;
		syscallarg(int) cmd;
		syscallarg(struct msqid_ds *) buf;
	} */ *uap = v;
	struct msqid_ds tmp, *umsq = SCARG(uap, buf);
	struct ucred *cred = p->p_ucred;
	struct que *que;
	int msqid = SCARG(uap, msqid);
	int cmd = SCARG(uap, cmd);
	int error;

	if ((que = que_lookup(msqid)) == NULL)
		return (EINVAL);

	QREF(que);

	switch (cmd) {

	case IPC_RMID:
		if ((error = ipcperm(cred, &que->msqid_ds.msg_perm, IPC_M)))
			goto out;

		TAILQ_REMOVE(&msg_queues, que, que_next);
		que->que_flags |= MSGQ_DYING;

		/* lose interest in the queue and wait for others to too */
		if (--que->que_references > 0) {
			wakeup(que);
			tsleep_nsec(&que->que_references, PZERO, "msgqrm",
			    INFSLP);
		}

		que_free(que);

		return (0);

	case IPC_SET:
		if ((error = ipcperm(cred, &que->msqid_ds.msg_perm, IPC_M)))
			goto out;
		if ((error = copyin(umsq, &tmp, sizeof(struct msqid_ds))))
			goto out;

		/* only superuser can bump max bytes in queue */
		if (tmp.msg_qbytes > que->msqid_ds.msg_qbytes &&
		    cred->cr_uid != 0) {
			error = EPERM;
			goto out;
		}

		/* restrict max bytes in queue to system limit */
		if (tmp.msg_qbytes > msginfo.msgmnb)
			tmp.msg_qbytes = msginfo.msgmnb;

		/* can't reduce msg_bytes to 0 */
		if (tmp.msg_qbytes == 0) {
			error = EINVAL;		/* non-standard errno! */
			goto out;
		}

		que->msqid_ds.msg_perm.uid = tmp.msg_perm.uid;
		que->msqid_ds.msg_perm.gid = tmp.msg_perm.gid;
		que->msqid_ds.msg_perm.mode =
		    (que->msqid_ds.msg_perm.mode & ~0777) |
		    (tmp.msg_perm.mode & 0777);
		que->msqid_ds.msg_qbytes = tmp.msg_qbytes;
		que->msqid_ds.msg_ctime = gettime();
		break;

	case IPC_STAT:
		if ((error = ipcperm(cred, &que->msqid_ds.msg_perm, IPC_R)))
			goto out;
		error = copyout(&que->msqid_ds, umsq, sizeof(struct msqid_ds));
		break;

	default:
		error = EINVAL;
		break;
	}
out:
	QRELE(que);

	return (error);
}

int
sys_msgget(struct proc *p, void *v, register_t *retval)
{
	struct sys_msgget_args /* {
		syscallarg(key_t) key;
		syscallarg(int) msgflg;
	} */ *uap = v;
	struct ucred *cred = p->p_ucred;
	struct que *que;
	key_t key = SCARG(uap, key);
	int msgflg = SCARG(uap, msgflg);
	int error = 0;

again:
	if (key != IPC_PRIVATE) {
		que = que_key_lookup(key);
		if (que) {
			if ((msgflg & IPC_CREAT) && (msgflg & IPC_EXCL))
				return (EEXIST);
			if ((error = ipcperm(cred, &que->msqid_ds.msg_perm,
			    msgflg & 0700)))
				return (error);
			goto found;
		}
	}

	/* don't create a new message queue if the caller doesn't want to */
	if (key != IPC_PRIVATE && !(msgflg & IPC_CREAT))
		return (ENOENT);

	/* enforce limits on the maximum number of message queues */
	if (num_ques >= msginfo.msgmni)
		return (ENOSPC);

	/*
	 * if que_create returns NULL, it means that a que with an identical
	 * key was created while this process was sleeping, so start over
	 */
	if ((que = que_create(key, cred, msgflg & 0777)) == NULL)
		goto again;

found:
	*retval = IXSEQ_TO_IPCID(que->que_ix, que->msqid_ds.msg_perm);
	return (error);
}

#define	MSGQ_SPACE(q)	((q)->msqid_ds.msg_qbytes - (q)->msqid_ds.msg_cbytes)

int
sys_msgsnd(struct proc *p, void *v, register_t *retval)
{
	struct sys_msgsnd_args /* {
		syscallarg(int) msqid;
		syscallarg(const void *) msgp;
		syscallarg(size_t) msgsz;
		syscallarg(int) msgflg;
	} */ *uap = v;
	struct ucred *cred = p->p_ucred;
	struct que *que;
	struct msg *msg;
	size_t msgsz = SCARG(uap, msgsz);
	int error;

	if ((que = que_lookup(SCARG(uap, msqid))) == NULL)
		return (EINVAL);

	if (msgsz > que->msqid_ds.msg_qbytes || msgsz > msginfo.msgmax)
		return (EINVAL);

	if ((error = ipcperm(cred, &que->msqid_ds.msg_perm, IPC_W)))
		return (error);

	QREF(que);

	while (MSGQ_SPACE(que) < msgsz || num_msgs >= msginfo.msgtql) {

		if (SCARG(uap, msgflg) & IPC_NOWAIT) {
			error = EAGAIN;
			goto out;
		}

		/* notify world that process may wedge here */
		if (num_msgs >= msginfo.msgtql)
			maxmsgs = 1;

		que->que_flags |= MSGQ_WRITERS;
		if ((error = tsleep_nsec(que, PZERO|PCATCH, "msgwait", INFSLP)))
			goto out;

		if (que->que_flags & MSGQ_DYING) {
			error = EIDRM;
			goto out;
		}
	}

	/* if msg_create returns NULL, the queue is being removed */
	if ((msg = msg_create(que)) == NULL) {
		error = EIDRM;
		goto out;
	}

	/* msg_copyin frees msg on error */
	if ((error = msg_copyin(msg, (const char *)SCARG(uap, msgp), msgsz, p)))
		goto out;

	msg_enqueue(que, msg, p);

	if (que->que_flags & MSGQ_READERS) {
		que->que_flags &= ~MSGQ_READERS;
		wakeup(que);
	}

	if (que->que_flags & MSGQ_DYING) {
		error = EIDRM;
		wakeup(que);
	}
out:
	QRELE(que);

	return (error);
}

int
sys_msgrcv(struct proc *p, void *v, register_t *retval)
{
	struct sys_msgrcv_args /* {
		syscallarg(int) msqid;
		syscallarg(void *) msgp;
		syscallarg(size_t) msgsz;
		syscallarg(long) msgtyp;
		syscallarg(int) msgflg;
	} */ *uap = v;
	struct ucred *cred = p->p_ucred;
	char *msgp = SCARG(uap, msgp);
	struct que *que;
	struct msg *msg;
	size_t msgsz = SCARG(uap, msgsz);
	long msgtyp = SCARG(uap, msgtyp);
	int error;

	if ((que = que_lookup(SCARG(uap, msqid))) == NULL)
		return (EINVAL);

	if ((error = ipcperm(cred, &que->msqid_ds.msg_perm, IPC_R)))
		return (error);

	QREF(que);

	/* msg_lookup handles matching; sleeping gets handled here */
	while ((msg = msg_lookup(que, msgtyp)) == NULL) {

		if (SCARG(uap, msgflg) & IPC_NOWAIT) {
			error = ENOMSG;
			goto out;
		}

		que->que_flags |= MSGQ_READERS;
		if ((error = tsleep_nsec(que, PZERO|PCATCH, "msgwait", INFSLP)))
			goto out;

		/* make sure the queue still alive */
		if (que->que_flags & MSGQ_DYING) {
			error = EIDRM;
			goto out;
		}
	}

	/* if msg_copyout fails, keep the message around so it isn't lost */
	if ((error = msg_copyout(msg, msgp, &msgsz, p)))
		goto out;

	msg_dequeue(que, msg, p);
	msg_free(msg);

	if (que->que_flags & MSGQ_WRITERS) {
		que->que_flags &= ~MSGQ_WRITERS;
		wakeup(que);
	}

	/* ensure processes waiting on the global limit don't wedge */
	if (maxmsgs) {
		maxmsgs = 0;
		que_wakewriters();
	}

	*retval = msgsz;
out:
	QRELE(que);

	return (error);
}

/*
 * que management functions
 */

struct que *
que_create(key_t key, struct ucred *cred, int mode)
{
	struct que *que, *que2;
	int nextix = 1;

	que = malloc(sizeof(*que), M_TEMP, M_WAIT|M_ZERO);

	/* if malloc slept, a queue with the same key may have been created */
	if (que_key_lookup(key)) {
		free(que, M_TEMP, sizeof *que);
		return (NULL);
	}

	/* find next available "index" */
	TAILQ_FOREACH(que2, &msg_queues, que_next) {
		if (nextix < que2->que_ix)
			break;
		nextix = que2->que_ix + 1;
	}
	que->que_ix = nextix;

	que->msqid_ds.msg_perm.key = key;
	que->msqid_ds.msg_perm.cuid = cred->cr_uid;
	que->msqid_ds.msg_perm.uid = cred->cr_uid;
	que->msqid_ds.msg_perm.cgid = cred->cr_gid;
	que->msqid_ds.msg_perm.gid = cred->cr_gid;
	que->msqid_ds.msg_perm.mode = mode & 0777;
	que->msqid_ds.msg_perm.seq = ++sequence & 0x7fff;
	que->msqid_ds.msg_qbytes = msginfo.msgmnb;
	que->msqid_ds.msg_ctime = gettime();

	TAILQ_INIT(&que->que_msgs);

	/* keep queues in "index" order */
	if (que2)
		TAILQ_INSERT_BEFORE(que2, que, que_next);
	else
		TAILQ_INSERT_TAIL(&msg_queues, que, que_next);
	num_ques++;

	return (que);
}

struct que *
que_lookup(int id)
{
	struct que *que;

	TAILQ_FOREACH(que, &msg_queues, que_next)
		if (que->que_ix == IPCID_TO_IX(id))
			break;

	/* don't return queues marked for removal */
	if (que && que->que_flags & MSGQ_DYING)
		return (NULL);

	return (que);
}

struct que *
que_key_lookup(key_t key)
{
	struct que *que;

	if (key == IPC_PRIVATE)
		return (NULL);

	TAILQ_FOREACH(que, &msg_queues, que_next)
		if (que->msqid_ds.msg_perm.key == key)
			break;

	/* don't return queues marked for removal */
	if (que && que->que_flags & MSGQ_DYING)
		return (NULL);

	return (que);
}

void
que_wakewriters(void)
{
	struct que *que;

	TAILQ_FOREACH(que, &msg_queues, que_next) {
		if (que->que_flags & MSGQ_WRITERS) {
			que->que_flags &= ~MSGQ_WRITERS;
			wakeup(que);
		}
	}
}

void
que_free(struct que *que)
{
	struct msg *msg;
#ifdef DIAGNOSTIC
	if (que->que_references > 0)
		panic("freeing message queue with active references");
#endif

	while ((msg = TAILQ_FIRST(&que->que_msgs))) {
		TAILQ_REMOVE(&que->que_msgs, msg, msg_next);
		msg_free(msg);
	}
	free(que, M_TEMP, sizeof *que);
	num_ques--;
}

/*
 * msg management functions
 */

struct msg *
msg_create(struct que *que)
{
	struct msg *msg;

	msg = pool_get(&sysvmsgpl, PR_WAITOK|PR_ZERO);

	/* if the queue has died during allocation, return NULL */
	if (que->que_flags & MSGQ_DYING) {
		pool_put(&sysvmsgpl, msg);
		wakeup(que);
		return(NULL);
	}

	num_msgs++;

	return (msg);
}

struct msg *
msg_lookup(struct que *que, int msgtyp)
{
	struct msg *msg;

	/*
	 * Three different matches are performed based on the value of msgtyp:
	 * 1) msgtyp > 0 => match exactly
	 * 2) msgtyp = 0 => match any
	 * 3) msgtyp < 0 => match any up to absolute value of msgtyp
	 */
	TAILQ_FOREACH(msg, &que->que_msgs, msg_next)
		if (msgtyp == 0 || msgtyp == msg->msg_type ||
		    (msgtyp < 0 && -msgtyp <= msg->msg_type))
			break;

	return (msg);
}

void
msg_free(struct msg *msg)
{
	m_freem(msg->msg_data);
	pool_put(&sysvmsgpl, msg);
	num_msgs--;
}

void
msg_enqueue(struct que *que, struct msg *msg, struct proc *p)
{
	que->msqid_ds.msg_cbytes += msg->msg_len;
	que->msqid_ds.msg_qnum++;
	que->msqid_ds.msg_lspid = p->p_p->ps_pid;
	que->msqid_ds.msg_stime = gettime();

	TAILQ_INSERT_TAIL(&que->que_msgs, msg, msg_next);
}

void
msg_dequeue(struct que *que, struct msg *msg, struct proc *p)
{
	que->msqid_ds.msg_cbytes -= msg->msg_len;
	que->msqid_ds.msg_qnum--;
	que->msqid_ds.msg_lrpid = p->p_p->ps_pid;
	que->msqid_ds.msg_rtime = gettime();

	TAILQ_REMOVE(&que->que_msgs, msg, msg_next);
}

/*
 * The actual I/O routines. A note concerning the layout of SysV msg buffers:
 *
 * The data to be copied is laid out as a single userspace buffer, with a
 * long preceding an opaque buffer of len bytes. The long value ends
 * up being the message type, which needs to be copied separately from
 * the buffer data, which is stored in mbufs.
 */

int
msg_copyin(struct msg *msg, const char *ubuf, size_t len, struct proc *p)
{
	struct mbuf **mm, *m;
	size_t xfer;
	int error;

	if (msg == NULL)
		panic ("msg NULL");

	if ((error = copyin(ubuf, &msg->msg_type, sizeof(long)))) {
		msg_free(msg);
		return (error);
	}

	if (msg->msg_type < 1) {
		msg_free(msg);
		return (EINVAL);
	}

	ubuf += sizeof(long);

	msg->msg_len = 0;
	mm = &msg->msg_data;

	while (msg->msg_len < len) {
		m = m_get(M_WAIT, MT_DATA);
		if (len >= MINCLSIZE) {
			MCLGET(m, M_WAIT);
			xfer = min(len, MCLBYTES);
		} else {
			xfer = min(len, MLEN);
		}
		m->m_len = xfer;
		msg->msg_len += xfer;
		*mm = m;
		mm = &m->m_next;
	}

	for (m = msg->msg_data; m; m = m->m_next) {
		if ((error = copyin(ubuf, mtod(m, void *), m->m_len))) {
			msg_free(msg);
			return (error);
		}
		ubuf += m->m_len;
	}

	return (0);
}

int
msg_copyout(struct msg *msg, char *ubuf, size_t *len, struct proc *p)
{
	struct mbuf *m;
	size_t xfer;
	int error;

#ifdef DIAGNOSTIC
	if (msg->msg_len > MSGMAX)
		panic("SysV message longer than MSGMAX");
#endif

	/* silently truncate messages too large for user buffer */
	xfer = min(*len, msg->msg_len);

	if ((error = copyout(&msg->msg_type, ubuf, sizeof(long))))
		return (error);

	ubuf += sizeof(long);
	*len = xfer;

	for (m = msg->msg_data; m; m = m->m_next) {
		if ((error = copyout(mtod(m, void *), ubuf, m->m_len)))
			return (error);
		ubuf += m->m_len;
	}

	return (0);
}

int
sysctl_sysvmsg(int *name, u_int namelen, void *where, size_t *sizep)
{
	struct msg_sysctl_info *info;
	struct que *que;
	size_t infolen, infolen0;
	int error;

	switch (*name) {
	case KERN_SYSVIPC_MSG_INFO:

		if (namelen != 1)
			return (ENOTDIR);

		/*
		 * The userland ipcs(1) utility expects to be able
		 * to iterate over at least msginfo.msgmni queues,
		 * even if those queues don't exist. This is an
		 * artifact of the previous implementation of
		 * message queues; for now, emulate this behavior
		 * until a more thorough fix can be made.
		 */
		infolen0 = sizeof(msginfo) +
		    msginfo.msgmni * sizeof(struct msqid_ds);
		if (where == NULL) {
			*sizep = infolen0;
			return (0);
		}

		/*
		 * More special-casing due to previous implementation:
		 * if the caller just wants the msginfo struct, then
		 * sizep will point to the value sizeof(struct msginfo).
		 * In that case, only copy out the msginfo struct to
		 * the caller.
		 */
		if (*sizep == sizeof(struct msginfo))
			return (copyout(&msginfo, where, sizeof(msginfo)));

		info = malloc(infolen0, M_TEMP, M_WAIT|M_ZERO);

		/* if the malloc slept, this may have changed */
		infolen = sizeof(msginfo) +
		    msginfo.msgmni * sizeof(struct msqid_ds);

		if (*sizep < infolen) {
			free(info, M_TEMP, infolen0);
			return (ENOMEM);
		}

		memcpy(&info->msginfo, &msginfo, sizeof(struct msginfo));

		/*
		 * Special case #3: the previous array-based implementation
		 * exported the array indices and userland has come to rely
		 * upon these indices, so keep behavior consistent.
		 */
		TAILQ_FOREACH(que, &msg_queues, que_next)
			memcpy(&info->msgids[que->que_ix], &que->msqid_ds,
			    sizeof(struct msqid_ds));

		error = copyout(info, where, infolen);

		free(info, M_TEMP, infolen0);

		return (error);

	default:
		return (EINVAL);
	}
}
