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
#include <uapi/linux/pidfd.h>
#include <linux/pidfs.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/errqueue.h>
#include <linux/io_uring.h>

#include <linux/uaccess.h>

#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/compat.h>
#include <net/scm.h>
#include <net/cls_cgroup.h>
#include <net/af_unix.h>


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
		fpl->count_unix = 0;
		fpl->max = SCM_MAX_FD;
		fpl->user = NULL;
#if IS_ENABLED(CONFIG_UNIX)
		fpl->inflight = false;
		fpl->dead = false;
		fpl->edges = NULL;
		INIT_LIST_HEAD(&fpl->vertices);
#endif
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
		/* don't allow io_uring files */
		if (io_is_uring_fops(file)) {
			fput(file);
			return -EINVAL;
		}
		if (unix_get_socket(file))
			fpl->count_unix++;

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

static inline int scm_replace_pid(struct scm_cookie *scm, struct pid *pid)
{
	int err;

	/* drop all previous references */
	scm_destroy_cred(scm);

	err = pidfs_register_pid(pid);
	if (unlikely(err))
		return err;

	scm->pid = pid;
	scm->creds.pid = pid_vnr(pid);
	return 0;
}

int __scm_send(struct socket *sock, struct msghdr *msg, struct scm_cookie *p)
{
	const struct proto_ops *ops = READ_ONCE(sock->ops);
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
			if (!ops || ops->family != PF_UNIX)
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

			if (!p->pid || pid_vnr(p->pid) != creds.pid) {
				struct pid *pid;
				err = -ESRCH;
				pid = find_get_pid(creds.pid);
				if (!pid)
					goto error;

				/* pass a struct pid reference from
				 * find_get_pid() to scm_replace_pid().
				 */
				err = scm_replace_pid(p, pid);
				if (err) {
					put_pid(pid);
					goto error;
				}
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

		check_object_size(data, cmlen - sizeof(*cm), true);

		if (can_do_masked_user_access())
			cm = masked_user_access_begin(cm);
		else if (!user_write_access_begin(cm, cmlen))
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
	if (msg->msg_control_is_user)
		msg->msg_control_user += cmlen;
	else
		msg->msg_control += cmlen;
	msg->msg_controllen -= cmlen;
	return 0;

efault_end:
	user_write_access_end();
efault:
	return -EFAULT;
}
EXPORT_SYMBOL(put_cmsg);

int put_cmsg_notrunc(struct msghdr *msg, int level, int type, int len,
		     void *data)
{
	/* Don't produce truncated CMSGs */
	if (!msg->msg_control || msg->msg_controllen < CMSG_LEN(len))
		return -ETOOSMALL;

	return put_cmsg(msg, level, type, len, data);
}

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
		(__force struct cmsghdr __user *)msg->msg_control_user;
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
		err = scm_recv_one_fd(scm->fp->fp[i], cmsg_data + i, o_flags);
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
			msg->msg_control_user += cmlen;
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
#if IS_ENABLED(CONFIG_UNIX)
		new_fpl->inflight = false;
		new_fpl->edges = NULL;
		INIT_LIST_HEAD(&new_fpl->vertices);
#endif
	}
	return new_fpl;
}
EXPORT_SYMBOL(scm_fp_dup);

#ifdef CONFIG_SECURITY_NETWORK
static void scm_passec(struct sock *sk, struct msghdr *msg, struct scm_cookie *scm)
{
	struct lsm_context ctx;
	int err;

	if (sk->sk_scm_security) {
		err = security_secid_to_secctx(scm->secid, &ctx);

		if (err >= 0) {
			put_cmsg(msg, SOL_SOCKET, SCM_SECURITY, ctx.len,
				 ctx.context);

			security_release_secctx(&ctx);
		}
	}
}

static bool scm_has_secdata(struct sock *sk)
{
	return sk->sk_scm_security;
}
#else
static void scm_passec(struct sock *sk, struct msghdr *msg, struct scm_cookie *scm)
{
}

static bool scm_has_secdata(struct sock *sk)
{
	return false;
}
#endif

static void scm_pidfd_recv(struct msghdr *msg, struct scm_cookie *scm)
{
	struct file *pidfd_file = NULL;
	int len, pidfd;

	/* put_cmsg() doesn't return an error if CMSG is truncated,
	 * that's why we need to opencode these checks here.
	 */
	if (msg->msg_flags & MSG_CMSG_COMPAT)
		len = sizeof(struct compat_cmsghdr) + sizeof(int);
	else
		len = sizeof(struct cmsghdr) + sizeof(int);

	if (msg->msg_controllen < len) {
		msg->msg_flags |= MSG_CTRUNC;
		return;
	}

	if (!scm->pid)
		return;

	pidfd = pidfd_prepare(scm->pid, PIDFD_STALE, &pidfd_file);

	if (put_cmsg(msg, SOL_SOCKET, SCM_PIDFD, sizeof(int), &pidfd)) {
		if (pidfd_file) {
			put_unused_fd(pidfd);
			fput(pidfd_file);
		}

		return;
	}

	if (pidfd_file)
		fd_install(pidfd, pidfd_file);
}

static bool __scm_recv_common(struct sock *sk, struct msghdr *msg,
			      struct scm_cookie *scm, int flags)
{
	if (!msg->msg_control) {
		if (sk->sk_scm_credentials || sk->sk_scm_pidfd ||
		    scm->fp || scm_has_secdata(sk))
			msg->msg_flags |= MSG_CTRUNC;

		scm_destroy(scm);
		return false;
	}

	if (sk->sk_scm_credentials) {
		struct user_namespace *current_ns = current_user_ns();
		struct ucred ucreds = {
			.pid = scm->creds.pid,
			.uid = from_kuid_munged(current_ns, scm->creds.uid),
			.gid = from_kgid_munged(current_ns, scm->creds.gid),
		};

		put_cmsg(msg, SOL_SOCKET, SCM_CREDENTIALS, sizeof(ucreds), &ucreds);
	}

	scm_passec(sk, msg, scm);

	if (scm->fp)
		scm_detach_fds(msg, scm);

	return true;
}

void scm_recv(struct socket *sock, struct msghdr *msg,
	      struct scm_cookie *scm, int flags)
{
	if (!__scm_recv_common(sock->sk, msg, scm, flags))
		return;

	scm_destroy_cred(scm);
}
EXPORT_SYMBOL(scm_recv);

void scm_recv_unix(struct socket *sock, struct msghdr *msg,
		   struct scm_cookie *scm, int flags)
{
	if (!__scm_recv_common(sock->sk, msg, scm, flags))
		return;

	if (sock->sk->sk_scm_pidfd)
		scm_pidfd_recv(msg, scm);

	scm_destroy_cred(scm);
}
