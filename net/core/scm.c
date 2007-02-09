/* scm.c - Socket level control messages processing.
 *
 * Author:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *              Alignment and value checking mods by Craig Metz
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/sched.h>
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

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/compat.h>
#include <net/scm.h>


/*
 *	Only allow a user to send credentials, that they could set with
 *	setu(g)id.
 */

static __inline__ int scm_check_creds(struct ucred *creds)
{
	if ((creds->pid == current->tgid || capable(CAP_SYS_ADMIN)) &&
	    ((creds->uid == current->uid || creds->uid == current->euid ||
	      creds->uid == current->suid) || capable(CAP_SETUID)) &&
	    ((creds->gid == current->gid || creds->gid == current->egid ||
	      creds->gid == current->sgid) || capable(CAP_SETGID))) {
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

	num = (cmsg->cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr)))/sizeof(int);

	if (num <= 0)
		return 0;

	if (num > SCM_MAX_FD)
		return -EINVAL;

	if (!fpl)
	{
		fpl = kmalloc(sizeof(struct scm_fp_list), GFP_KERNEL);
		if (!fpl)
			return -ENOMEM;
		*fplp = fpl;
		fpl->count = 0;
	}
	fpp = &fpl->fp[fpl->count];

	if (fpl->count + num > SCM_MAX_FD)
		return -EINVAL;

	/*
	 *	Verify the descriptors and increment the usage count.
	 */

	for (i=0; i< num; i++)
	{
		int fd = fdp[i];
		struct file *file;

		if (fd < 0 || !(file = fget(fd)))
			return -EBADF;
		*fpp++ = file;
		fpl->count++;
	}
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
		kfree(fpl);
	}
}

int __scm_send(struct socket *sock, struct msghdr *msg, struct scm_cookie *p)
{
	struct cmsghdr *cmsg;
	int err;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg))
	{
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
			err=scm_fp_copy(cmsg, &p->fp);
			if (err<0)
				goto error;
			break;
		case SCM_CREDENTIALS:
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(struct ucred)))
				goto error;
			memcpy(&p->creds, CMSG_DATA(cmsg), sizeof(struct ucred));
			err = scm_check_creds(&p->creds);
			if (err)
				goto error;
			break;
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

int put_cmsg(struct msghdr * msg, int level, int type, int len, void *data)
{
	struct cmsghdr __user *cm = (struct cmsghdr __user *)msg->msg_control;
	struct cmsghdr cmhdr;
	int cmlen = CMSG_LEN(len);
	int err;

	if (MSG_CMSG_COMPAT & msg->msg_flags)
		return put_cmsg_compat(msg, level, type, len, data);

	if (cm==NULL || msg->msg_controllen < sizeof(*cm)) {
		msg->msg_flags |= MSG_CTRUNC;
		return 0; /* XXX: return error? check spec. */
	}
	if (msg->msg_controllen < cmlen) {
		msg->msg_flags |= MSG_CTRUNC;
		cmlen = msg->msg_controllen;
	}
	cmhdr.cmsg_level = level;
	cmhdr.cmsg_type = type;
	cmhdr.cmsg_len = cmlen;

	err = -EFAULT;
	if (copy_to_user(cm, &cmhdr, sizeof cmhdr))
		goto out;
	if (copy_to_user(CMSG_DATA(cm), data, cmlen - sizeof(struct cmsghdr)))
		goto out;
	cmlen = CMSG_SPACE(len);
	msg->msg_control += cmlen;
	msg->msg_controllen -= cmlen;
	err = 0;
out:
	return err;
}

void scm_detach_fds(struct msghdr *msg, struct scm_cookie *scm)
{
	struct cmsghdr __user *cm = (struct cmsghdr __user*)msg->msg_control;

	int fdmax = 0;
	int fdnum = scm->fp->count;
	struct file **fp = scm->fp->fp;
	int __user *cmfptr;
	int err = 0, i;

	if (MSG_CMSG_COMPAT & msg->msg_flags) {
		scm_detach_fds_compat(msg, scm);
		return;
	}

	if (msg->msg_controllen > sizeof(struct cmsghdr))
		fdmax = ((msg->msg_controllen - sizeof(struct cmsghdr))
			 / sizeof(int));

	if (fdnum < fdmax)
		fdmax = fdnum;

	for (i=0, cmfptr=(int __user *)CMSG_DATA(cm); i<fdmax; i++, cmfptr++)
	{
		int new_fd;
		err = security_file_receive(fp[i]);
		if (err)
			break;
		err = get_unused_fd();
		if (err < 0)
			break;
		new_fd = err;
		err = put_user(new_fd, cmfptr);
		if (err) {
			put_unused_fd(new_fd);
			break;
		}
		/* Bump the usage count and install the file. */
		get_file(fp[i]);
		fd_install(new_fd, fp[i]);
	}

	if (i > 0)
	{
		int cmlen = CMSG_LEN(i*sizeof(int));
		err = put_user(SOL_SOCKET, &cm->cmsg_level);
		if (!err)
			err = put_user(SCM_RIGHTS, &cm->cmsg_type);
		if (!err)
			err = put_user(cmlen, &cm->cmsg_len);
		if (!err) {
			cmlen = CMSG_SPACE(i*sizeof(int));
			msg->msg_control += cmlen;
			msg->msg_controllen -= cmlen;
		}
	}
	if (i < fdnum || (fdnum && fdmax <= 0))
		msg->msg_flags |= MSG_CTRUNC;

	/*
	 * All of the files that fit in the message have had their
	 * usage counts incremented, so we just free the list.
	 */
	__scm_destroy(scm);
}

struct scm_fp_list *scm_fp_dup(struct scm_fp_list *fpl)
{
	struct scm_fp_list *new_fpl;
	int i;

	if (!fpl)
		return NULL;

	new_fpl = kmalloc(sizeof(*fpl), GFP_KERNEL);
	if (new_fpl) {
		for (i=fpl->count-1; i>=0; i--)
			get_file(fpl->fp[i]);
		memcpy(new_fpl, fpl, sizeof(*fpl));
	}
	return new_fpl;
}

EXPORT_SYMBOL(__scm_destroy);
EXPORT_SYMBOL(__scm_send);
EXPORT_SYMBOL(put_cmsg);
EXPORT_SYMBOL(scm_detach_fds);
EXPORT_SYMBOL(scm_fp_dup);
