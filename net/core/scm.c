// SPDX-License-Identifier: GPL-2.0-or-later
/* scm.c - Socket level control messages processing.
 *
 * Author:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *              Alignment and value checking mods by Craig Metz
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/user.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/security.h>
#include <linux/pid_namespace.h>
#include <linux/pid.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/errqueue.h>

#include <linux/uaccess.h>

#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/compat.h>
#include <net/scm.h>
#include <net/cls_cgroup.h>


/*
 *	Only allow a user to send credentials, that they could set with
 *	setu(g)id.
 */

static __inline__ int scm_check_creds(struct ucred *creds)
{
	const struct cred *cred = current_cred();
	kuid_t uid = make_kuid(cred->user_ns, creds->uid);
	kgid_t gid = make_kgid(cred->user_ns, creds->gid);

	if (!uid_valid(uid) || !gid_valid(gid))
		return -EINVAL;

	if ((creds->pid == task_tgid_vnr(current) ||
	     ns_capable(task_active_pid_ns(current)->user_ns, CAP_SYS_ADMIN)) &&
	    ((uid_eq(uid, cred->uid)   || uid_eq(uid, cred->euid) ||
	      uid_eq(uid, cred->suid)) || ns_capable(cred->user_ns, CAP_SETUID)) &&
	    ((gid_eq(gid, cred->gid)   || gid_eq(gid, cred->egid) ||
	      gid_eq(gid, cred->sgid)) || ns_capable(cred->user_ns, CAP_SETGID))) {
	       return 0;
	}
	return -EPERM;
}

static int scm_fp_copy(struct cmsghdr *cmsg, struct scm_fp_list **fplp)
{
	int *fdp = (int*)CMSG_DATA(cmsg);
	struct scm_fp_list *fpl = *fplp;
	struct file **fpp;
	int i, num;

	num = (cmsg->cmsg_len - sizeof(struct cmsghdr))/sizeof(int);

	if (num <= 0)
		return 0;

	if (num > SCM_MAX_FD)
		return -EINVAL;

	if (!fpl)
	{
		fpl = kmalloc(sizeof(struct scm_fp_list), GFP_KERNEL_ACCOUNT);
		if (!fpl)
			return -ENOMEM;
		*fplp = fpl;
		fpl->count = 0;
		fpl->max = SCM_MAX_FD;
		fpl->user = NULL;
	}
	fpp = &fpl->fp[fpl->count];

	if (fpl->count + num > fpl->max)
		return -EINVAL;

	/*
	 *	Verify the descriptors and increment the usage count.
	 */

	for (i=0; i< num; i++)
	{
		int fd = fdp[i];
		struct file *file;

		if (fd < 0 || !(file = fget_raw(fd)))
			return -EBADF;
		*fpp++ = file;
		fpl->count++;
	}

	if (!fpl->user)
		fpl->user = get_uid(current_user());

	return num;
}

void __scm_destroy(struct scm_cookie *scm)
{
	struct scm_fp_list *fpl = scm->fp;
	int i;

	if (fpl) {
		scm->fp = NULL;
		for (i=fpl->count-1; i>=0; i--)
			fput(fpl->fp[i]);
		free_uid(fpl->user);
		kfree(fpl);
	}
}
EXPORT_SYMBOL(__scm_destroy);

int __scm_send(struct socket *sock, struct msghdr *msg, struct scm_cookie *p)
{
	struct cmsghdr *cmsg;
	int err;

	for_each_cmsghdr(cmsg, msg) {
		err = -EINVAL;

		/* Verify that cmsg_len is at least sizeof(struct cmsghdr) */
		/* The first check was omitted in <= 2.2.5. The reasoning was
		   that parser checks cmsg_len in any case, so that
		   additional check would be work duplication.
		   But if cmsg_level is not SOL_SOCKET, we do not check
		   for too short ancillary data object at all! Oops.
		   OK, let's add it...
		 */
		if (!CMSG_OK(msg, cmsg))
			goto error;

		if (cmsg->cmsg_level != SOL_SOCKET)
			continue;

		switch (cmsg->cmsg_type)
		{
		case SCM_RIGHTS:
			if (!sock->ops || sock->ops->family != PF_UNIX)
				goto error;
			err=scm_fp_copy(cmsg, &p->fp);
			if (err<0)
				goto error;
			break;
		case SCM_CREDENTIALS:
		{
			struct ucred creds;
			kuid_t uid;
			kgid_t gid;
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(struct ucred)))
				goto error;
			memcpy(&creds, CMSG_DATA(cmsg), sizeof(struct ucred));
			err = scm_check_creds(&creds);
			if (err)
				goto error;

			p->creds.pid = creds.pid;
			if (!p->pid || pid_vnr(p->pid) != creds.pid) {
				struct pid *pid;
				err = -ESRCH;
				pid = find_get_pid(creds.pid);
				if (!pid)
					goto error;
				put_pid(p->pid);
				p->pid = pid;
			}

			err = -EINVAL;
			uid = make_kuid(current_user_ns(), creds.uid);
			gid = make_kgid(current_user_ns(), creds.gid);
			if (!uid_valid(uid) || !gid_valid(gid))
				goto error;

			p->creds.uid = uid;
			p->creds.gid = gid;
			break;
		}
		default:
			goto error;
		}
	}

	if (p->fp && !p->fp->count)
	{
		kfree(p->fp);
		p->fp = NULL;
	}
	return 0;

error:
	scm_destroy(p);
	return err;
}
EXPORT_SYMBOL(__scm_send);

int put_cmsg(struct msghdr * msg, int level, int type, int len, void *data)
{
	int cmlen = CMSG_LEN(len);

	if (msg->msg_flags & MSG_CMSG_COMPAT)
		return put_cmsg_compat(msg, level, type, len, data);

	if (!msg->msg_control || msg->msg_controllen < sizeof(struct cmsghdr)) {
		msg->msg_flags |= MSG_CTRUNC;
		return 0; /* XXX: return error? check spec. */
	}
	if (msg->msg_controllen < cmlen) {
		msg->msg_flags |= MSG_CTRUNC;
		cmlen = msg->msg_controllen;
	}

	if (msg->msg_control_is_user) {
		struct cmsghdr __user *cm = msg->msg_control_user;

		if (!user_write_access_begin(cm, cmlen))
			goto efault;

		unsafe_put_user(cmlen, &cm->cmsg_len, efault_end);
		unsafe_put_user(level, &cm->cmsg_level, efault_end);
		unsafe_put_user(type, &cm->cmsg_type, efault_end);
		unsafe_copy_to_user(CMSG_USER_DATA(cm), data,
				    cmlen - sizeof(*cm), efault_end);
		user_write_access_end();
	} else {
		struct cmsghdr *cm = msg->msg_control;

		cm->cmsg_level = level;
		cm->cmsg_type = type;
		cm->cmsg_len = cmlen;
		memcpy(CMSG_DATA(cm), data, cmlen - sizeof(*cm));
	}

	cmlen = min(CMSG_SPACE(len), msg->msg_controllen);
	msg->msg_control += cmlen;
	msg->msg_controllen -= cmlen;
	return 0;

efault_end:
	user_write_access_end();
efault:
	return -EFAULT;
}
EXPORT_SYMBOL(put_cmsg);

void put_cmsg_scm_timestamping64(struct msghdr *msg, struct scm_timestamping_internal *tss_internal)
{
	struct scm_timestamping64 tss;
	int i;

	for (i = 0; i < ARRAY_SIZE(tss.ts); i++) {
		tss.ts[i].tv_sec = tss_internal->ts[i].tv_sec;
		tss.ts[i].tv_nsec = tss_internal->ts[i].tv_nsec;
	}

	put_cmsg(msg, SOL_SOCKET, SO_TIMESTAMPING_NEW, sizeof(tss), &tss);
}
EXPORT_SYMBOL(put_cmsg_scm_timestamping64);

void put_cmsg_scm_timestamping(struct msghdr *msg, struct scm_timestamping_internal *tss_internal)
{
	struct scm_timestamping tss;
	int i;

	for (i = 0; i < ARRAY_SIZE(tss.ts); i++) {
		tss.ts[i].tv_sec = tss_internal->ts[i].tv_sec;
		tss.ts[i].tv_nsec = tss_internal->ts[i].tv_nsec;
	}

	put_cmsg(msg, SOL_SOCKET, SO_TIMESTAMPING_OLD, sizeof(tss), &tss);
}
EXPORT_SYMBOL(put_cmsg_scm_timestamping);

static int scm_max_fds(struct msghdr *msg)
{
	if (msg->msg_controllen <= sizeof(struct cmsghdr))
		return 0;
	return (msg->msg_controllen - sizeof(struct cmsghdr)) / sizeof(int);
}

void scm_detach_fds(struct msghdr *msg, struct scm_cookie *scm)
{
	struct cmsghdr __user *cm =
		(__force struct cmsghdr __user *)msg->msg_control;
	unsigned int o_flags = (msg->msg_flags & MSG_CMSG_CLOEXEC) ? O_CLOEXEC : 0;
	int fdmax = min_t(int, scm_max_fds(msg), scm->fp->count);
	int __user *cmsg_data = CMSG_USER_DATA(cm);
	int err = 0, i;

	/* no use for FD passing from kernel space callers */
	if (WARN_ON_ONCE(!msg->msg_control_is_user))
		return;

	if (msg->msg_flags & MSG_CMSG_COMPAT) {
		scm_detach_fds_compat(msg, scm);
		return;
	}

	for (i = 0; i < fdmax; i++) {
		err = receive_fd_user(scm->fp->fp[i], cmsg_data + i, o_flags);
		if (err < 0)
			break;
	}

	if (i > 0) {
		int cmlen = CMSG_LEN(i * sizeof(int));

		err = put_user(SOL_SOCKET, &cm->cmsg_level);
		if (!err)
			err = put_user(SCM_RIGHTS, &cm->cmsg_type);
		if (!err)
			err = put_user(cmlen, &cm->cmsg_len);
		if (!err) {
			cmlen = CMSG_SPACE(i * sizeof(int));
			if (msg->msg_controllen < cmlen)
				cmlen = msg->msg_controllen;
			msg->msg_control += cmlen;
			msg->msg_controllen -= cmlen;
		}
	}

	if (i < scm->fp->count || (scm->fp->count && fdmax <= 0))
		msg->msg_flags |= MSG_CTRUNC;

	/*
	 * All of the files that fit in the message have had their usage counts
	 * incremented, so we just free the list.
	 */
	__scm_destroy(scm);
}
EXPORT_SYMBOL(scm_detach_fds);

struct scm_fp_list *scm_fp_dup(struct scm_fp_list *fpl)
{
	struct scm_fp_list *new_fpl;
	int i;

	if (!fpl)
		return NULL;

	new_fpl = kmemdup(fpl, offsetof(struct scm_fp_list, fp[fpl->count]),
			  GFP_KERNEL_ACCOUNT);
	if (new_fpl) {
		for (i = 0; i < fpl->count; i++)
			get_file(fpl->fp[i]);
		new_fpl->max = new_fpl->count;
		new_fpl->user = get_uid(fpl->user);
	}
	return new_fpl;
}
EXPORT_SYMBOL(scm_fp_dup);
