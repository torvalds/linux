// SPDX-License-Identifier: GPL-2.0-only
/*
 * 32bit Socket syscall emulation. Based on arch/sparc64/kernel/sys_sparc32.c.
 *
 * Copyright (C) 2000		VA Linux Co
 * Copyright (C) 2000		Don Dugger <n0ano@valinux.com>
 * Copyright (C) 1999 		Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 1997,1998 	Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 		David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000		Hewlett-Packard Co.
 * Copyright (C) 2000		David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000,2001	Andi Kleen, SuSE Labs
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/icmpv6.h>
#include <linux/socket.h>
#include <linux/syscalls.h>
#include <linux/filter.h>
#include <linux/compat.h>
#include <linux/security.h>
#include <linux/audit.h>
#include <linux/export.h>

#include <net/scm.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <linux/uaccess.h>
#include <net/compat.h>

int __get_compat_msghdr(struct msghdr *kmsg,
			struct compat_msghdr *msg,
			struct sockaddr __user **save_addr)
{
	ssize_t err;

	kmsg->msg_flags = msg->msg_flags;
	kmsg->msg_namelen = msg->msg_namelen;

	if (!msg->msg_name)
		kmsg->msg_namelen = 0;

	if (kmsg->msg_namelen < 0)
		return -EINVAL;

	if (kmsg->msg_namelen > sizeof(struct sockaddr_storage))
		kmsg->msg_namelen = sizeof(struct sockaddr_storage);

	kmsg->msg_control_is_user = true;
	kmsg->msg_get_inq = 0;
	kmsg->msg_control_user = compat_ptr(msg->msg_control);
	kmsg->msg_controllen = msg->msg_controllen;

	if (save_addr)
		*save_addr = compat_ptr(msg->msg_name);

	if (msg->msg_name && kmsg->msg_namelen) {
		if (!save_addr) {
			err = move_addr_to_kernel(compat_ptr(msg->msg_name),
						  kmsg->msg_namelen,
						  kmsg->msg_name);
			if (err < 0)
				return err;
		}
	} else {
		kmsg->msg_name = NULL;
		kmsg->msg_namelen = 0;
	}

	if (msg->msg_iovlen > UIO_MAXIOV)
		return -EMSGSIZE;

	kmsg->msg_iocb = NULL;
	kmsg->msg_ubuf = NULL;
	return 0;
}

int get_compat_msghdr(struct msghdr *kmsg,
		      struct compat_msghdr __user *umsg,
		      struct sockaddr __user **save_addr,
		      struct iovec **iov)
{
	struct compat_msghdr msg;
	ssize_t err;

	if (copy_from_user(&msg, umsg, sizeof(*umsg)))
		return -EFAULT;

	err = __get_compat_msghdr(kmsg, &msg, save_addr);
	if (err)
		return err;

	err = import_iovec(save_addr ? ITER_DEST : ITER_SOURCE,
			   compat_ptr(msg.msg_iov), msg.msg_iovlen,
			   UIO_FASTIOV, iov, &kmsg->msg_iter);
	return err < 0 ? err : 0;
}

/* Bleech... */
#define CMSG_COMPAT_ALIGN(len)	ALIGN((len), sizeof(s32))

#define CMSG_COMPAT_DATA(cmsg)				\
	((void __user *)((char __user *)(cmsg) + sizeof(struct compat_cmsghdr)))
#define CMSG_COMPAT_SPACE(len)				\
	(sizeof(struct compat_cmsghdr) + CMSG_COMPAT_ALIGN(len))
#define CMSG_COMPAT_LEN(len)				\
	(sizeof(struct compat_cmsghdr) + (len))

#define CMSG_COMPAT_FIRSTHDR(msg)			\
	(((msg)->msg_controllen) >= sizeof(struct compat_cmsghdr) ?	\
	 (struct compat_cmsghdr __user *)((msg)->msg_control_user) :	\
	 (struct compat_cmsghdr __user *)NULL)

#define CMSG_COMPAT_OK(ucmlen, ucmsg, mhdr) \
	((ucmlen) >= sizeof(struct compat_cmsghdr) && \
	 (ucmlen) <= (unsigned long) \
	 ((mhdr)->msg_controllen - \
	  ((char __user *)(ucmsg) - (char __user *)(mhdr)->msg_control_user)))

static inline struct compat_cmsghdr __user *cmsg_compat_nxthdr(struct msghdr *msg,
		struct compat_cmsghdr __user *cmsg, int cmsg_len)
{
	char __user *ptr = (char __user *)cmsg + CMSG_COMPAT_ALIGN(cmsg_len);
	if ((unsigned long)(ptr + 1 - (char __user *)msg->msg_control_user) >
			msg->msg_controllen)
		return NULL;
	return (struct compat_cmsghdr __user *)ptr;
}

/* There is a lot of hair here because the alignment rules (and
 * thus placement) of cmsg headers and length are different for
 * 32-bit apps.  -DaveM
 */
int cmsghdr_from_user_compat_to_kern(struct msghdr *kmsg, struct sock *sk,
			       unsigned char *stackbuf, int stackbuf_size)
{
	struct compat_cmsghdr __user *ucmsg;
	struct cmsghdr *kcmsg, *kcmsg_base;
	compat_size_t ucmlen;
	__kernel_size_t kcmlen, tmp;
	int err = -EFAULT;

	BUILD_BUG_ON(sizeof(struct compat_cmsghdr) !=
		     CMSG_COMPAT_ALIGN(sizeof(struct compat_cmsghdr)));

	kcmlen = 0;
	kcmsg_base = kcmsg = (struct cmsghdr *)stackbuf;
	ucmsg = CMSG_COMPAT_FIRSTHDR(kmsg);
	while (ucmsg != NULL) {
		if (get_user(ucmlen, &ucmsg->cmsg_len))
			return -EFAULT;

		/* Catch bogons. */
		if (!CMSG_COMPAT_OK(ucmlen, ucmsg, kmsg))
			return -EINVAL;

		tmp = ((ucmlen - sizeof(*ucmsg)) + sizeof(struct cmsghdr));
		tmp = CMSG_ALIGN(tmp);
		kcmlen += tmp;
		ucmsg = cmsg_compat_nxthdr(kmsg, ucmsg, ucmlen);
	}
	if (kcmlen == 0)
		return -EINVAL;

	/* The kcmlen holds the 64-bit version of the control length.
	 * It may not be modified as we do not stick it into the kmsg
	 * until we have successfully copied over all of the data
	 * from the user.
	 */
	if (kcmlen > stackbuf_size)
		kcmsg_base = kcmsg = sock_kmalloc(sk, kcmlen, GFP_KERNEL);
	if (kcmsg == NULL)
		return -ENOMEM;

	/* Now copy them over neatly. */
	memset(kcmsg, 0, kcmlen);
	ucmsg = CMSG_COMPAT_FIRSTHDR(kmsg);
	while (ucmsg != NULL) {
		struct compat_cmsghdr cmsg;
		if (copy_from_user(&cmsg, ucmsg, sizeof(cmsg)))
			goto Efault;
		if (!CMSG_COMPAT_OK(cmsg.cmsg_len, ucmsg, kmsg))
			goto Einval;
		tmp = ((cmsg.cmsg_len - sizeof(*ucmsg)) + sizeof(struct cmsghdr));
		if ((char *)kcmsg_base + kcmlen - (char *)kcmsg < CMSG_ALIGN(tmp))
			goto Einval;
		kcmsg->cmsg_len = tmp;
		kcmsg->cmsg_level = cmsg.cmsg_level;
		kcmsg->cmsg_type = cmsg.cmsg_type;
		tmp = CMSG_ALIGN(tmp);
		if (copy_from_user(CMSG_DATA(kcmsg),
				   CMSG_COMPAT_DATA(ucmsg),
				   (cmsg.cmsg_len - sizeof(*ucmsg))))
			goto Efault;

		/* Advance. */
		kcmsg = (struct cmsghdr *)((char *)kcmsg + tmp);
		ucmsg = cmsg_compat_nxthdr(kmsg, ucmsg, cmsg.cmsg_len);
	}

	/*
	 * check the length of messages copied in is the same as the
	 * what we get from the first loop
	 */
	if ((char *)kcmsg - (char *)kcmsg_base != kcmlen)
		goto Einval;

	/* Ok, looks like we made it.  Hook it up and return success. */
	kmsg->msg_control_is_user = false;
	kmsg->msg_control = kcmsg_base;
	kmsg->msg_controllen = kcmlen;
	return 0;

Einval:
	err = -EINVAL;
Efault:
	if (kcmsg_base != (struct cmsghdr *)stackbuf)
		sock_kfree_s(sk, kcmsg_base, kcmlen);
	return err;
}

int put_cmsg_compat(struct msghdr *kmsg, int level, int type, int len, void *data)
{
	struct compat_cmsghdr __user *cm = (struct compat_cmsghdr __user *) kmsg->msg_control_user;
	struct compat_cmsghdr cmhdr;
	struct old_timeval32 ctv;
	struct old_timespec32 cts[3];
	int cmlen;

	if (cm == NULL || kmsg->msg_controllen < sizeof(*cm)) {
		kmsg->msg_flags |= MSG_CTRUNC;
		return 0; /* XXX: return error? check spec. */
	}

	if (!COMPAT_USE_64BIT_TIME) {
		if (level == SOL_SOCKET && type == SO_TIMESTAMP_OLD) {
			struct __kernel_old_timeval *tv = (struct __kernel_old_timeval *)data;
			ctv.tv_sec = tv->tv_sec;
			ctv.tv_usec = tv->tv_usec;
			data = &ctv;
			len = sizeof(ctv);
		}
		if (level == SOL_SOCKET &&
		    (type == SO_TIMESTAMPNS_OLD || type == SO_TIMESTAMPING_OLD)) {
			int count = type == SO_TIMESTAMPNS_OLD ? 1 : 3;
			int i;
			struct __kernel_old_timespec *ts = data;
			for (i = 0; i < count; i++) {
				cts[i].tv_sec = ts[i].tv_sec;
				cts[i].tv_nsec = ts[i].tv_nsec;
			}
			data = &cts;
			len = sizeof(cts[0]) * count;
		}
	}

	cmlen = CMSG_COMPAT_LEN(len);
	if (kmsg->msg_controllen < cmlen) {
		kmsg->msg_flags |= MSG_CTRUNC;
		cmlen = kmsg->msg_controllen;
	}
	cmhdr.cmsg_level = level;
	cmhdr.cmsg_type = type;
	cmhdr.cmsg_len = cmlen;

	if (copy_to_user(cm, &cmhdr, sizeof cmhdr))
		return -EFAULT;
	if (copy_to_user(CMSG_COMPAT_DATA(cm), data, cmlen - sizeof(struct compat_cmsghdr)))
		return -EFAULT;
	cmlen = CMSG_COMPAT_SPACE(len);
	if (kmsg->msg_controllen < cmlen)
		cmlen = kmsg->msg_controllen;
	kmsg->msg_control_user += cmlen;
	kmsg->msg_controllen -= cmlen;
	return 0;
}

static int scm_max_fds_compat(struct msghdr *msg)
{
	if (msg->msg_controllen <= sizeof(struct compat_cmsghdr))
		return 0;
	return (msg->msg_controllen - sizeof(struct compat_cmsghdr)) / sizeof(int);
}

void scm_detach_fds_compat(struct msghdr *msg, struct scm_cookie *scm)
{
	struct compat_cmsghdr __user *cm =
		(struct compat_cmsghdr __user *)msg->msg_control_user;
	unsigned int o_flags = (msg->msg_flags & MSG_CMSG_CLOEXEC) ? O_CLOEXEC : 0;
	int fdmax = min_t(int, scm_max_fds_compat(msg), scm->fp->count);
	int __user *cmsg_data = CMSG_COMPAT_DATA(cm);
	int err = 0, i;

	for (i = 0; i < fdmax; i++) {
		err = receive_fd_user(scm->fp->fp[i], cmsg_data + i, o_flags);
		if (err < 0)
			break;
	}

	if (i > 0) {
		int cmlen = CMSG_COMPAT_LEN(i * sizeof(int));

		err = put_user(SOL_SOCKET, &cm->cmsg_level);
		if (!err)
			err = put_user(SCM_RIGHTS, &cm->cmsg_type);
		if (!err)
			err = put_user(cmlen, &cm->cmsg_len);
		if (!err) {
			cmlen = CMSG_COMPAT_SPACE(i * sizeof(int));
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

/* Argument list sizes for compat_sys_socketcall */
#define AL(x) ((x) * sizeof(u32))
static unsigned char nas[21] = {
	AL(0), AL(3), AL(3), AL(3), AL(2), AL(3),
	AL(3), AL(3), AL(4), AL(4), AL(4), AL(6),
	AL(6), AL(2), AL(5), AL(5), AL(3), AL(3),
	AL(4), AL(5), AL(4)
};
#undef AL

static inline long __compat_sys_sendmsg(int fd,
					struct compat_msghdr __user *msg,
					unsigned int flags)
{
	return __sys_sendmsg(fd, (struct user_msghdr __user *)msg,
			     flags | MSG_CMSG_COMPAT, false);
}

COMPAT_SYSCALL_DEFINE3(sendmsg, int, fd, struct compat_msghdr __user *, msg,
		       unsigned int, flags)
{
	return __compat_sys_sendmsg(fd, msg, flags);
}

static inline long __compat_sys_sendmmsg(int fd,
					 struct compat_mmsghdr __user *mmsg,
					 unsigned int vlen, unsigned int flags)
{
	return __sys_sendmmsg(fd, (struct mmsghdr __user *)mmsg, vlen,
			      flags | MSG_CMSG_COMPAT, false);
}

COMPAT_SYSCALL_DEFINE4(sendmmsg, int, fd, struct compat_mmsghdr __user *, mmsg,
		       unsigned int, vlen, unsigned int, flags)
{
	return __compat_sys_sendmmsg(fd, mmsg, vlen, flags);
}

static inline long __compat_sys_recvmsg(int fd,
					struct compat_msghdr __user *msg,
					unsigned int flags)
{
	return __sys_recvmsg(fd, (struct user_msghdr __user *)msg,
			     flags | MSG_CMSG_COMPAT, false);
}

COMPAT_SYSCALL_DEFINE3(recvmsg, int, fd, struct compat_msghdr __user *, msg,
		       unsigned int, flags)
{
	return __compat_sys_recvmsg(fd, msg, flags);
}

static inline long __compat_sys_recvfrom(int fd, void __user *buf,
					 compat_size_t len, unsigned int flags,
					 struct sockaddr __user *addr,
					 int __user *addrlen)
{
	return __sys_recvfrom(fd, buf, len, flags | MSG_CMSG_COMPAT, addr,
			      addrlen);
}

COMPAT_SYSCALL_DEFINE4(recv, int, fd, void __user *, buf, compat_size_t, len, unsigned int, flags)
{
	return __compat_sys_recvfrom(fd, buf, len, flags, NULL, NULL);
}

COMPAT_SYSCALL_DEFINE6(recvfrom, int, fd, void __user *, buf, compat_size_t, len,
		       unsigned int, flags, struct sockaddr __user *, addr,
		       int __user *, addrlen)
{
	return __compat_sys_recvfrom(fd, buf, len, flags, addr, addrlen);
}

COMPAT_SYSCALL_DEFINE5(recvmmsg_time64, int, fd, struct compat_mmsghdr __user *, mmsg,
		       unsigned int, vlen, unsigned int, flags,
		       struct __kernel_timespec __user *, timeout)
{
	return __sys_recvmmsg(fd, (struct mmsghdr __user *)mmsg, vlen,
			      flags | MSG_CMSG_COMPAT, timeout, NULL);
}

#ifdef CONFIG_COMPAT_32BIT_TIME
COMPAT_SYSCALL_DEFINE5(recvmmsg_time32, int, fd, struct compat_mmsghdr __user *, mmsg,
		       unsigned int, vlen, unsigned int, flags,
		       struct old_timespec32 __user *, timeout)
{
	return __sys_recvmmsg(fd, (struct mmsghdr __user *)mmsg, vlen,
			      flags | MSG_CMSG_COMPAT, NULL, timeout);
}
#endif

COMPAT_SYSCALL_DEFINE2(socketcall, int, call, u32 __user *, args)
{
	u32 a[AUDITSC_ARGS];
	unsigned int len;
	u32 a0, a1;
	int ret;

	if (call < SYS_SOCKET || call > SYS_SENDMMSG)
		return -EINVAL;
	len = nas[call];
	if (len > sizeof(a))
		return -EINVAL;

	if (copy_from_user(a, args, len))
		return -EFAULT;

	ret = audit_socketcall_compat(len / sizeof(a[0]), a);
	if (ret)
		return ret;

	a0 = a[0];
	a1 = a[1];

	switch (call) {
	case SYS_SOCKET:
		ret = __sys_socket(a0, a1, a[2]);
		break;
	case SYS_BIND:
		ret = __sys_bind(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_CONNECT:
		ret = __sys_connect(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_LISTEN:
		ret = __sys_listen(a0, a1);
		break;
	case SYS_ACCEPT:
		ret = __sys_accept4(a0, compat_ptr(a1), compat_ptr(a[2]), 0);
		break;
	case SYS_GETSOCKNAME:
		ret = __sys_getsockname(a0, compat_ptr(a1), compat_ptr(a[2]));
		break;
	case SYS_GETPEERNAME:
		ret = __sys_getpeername(a0, compat_ptr(a1), compat_ptr(a[2]));
		break;
	case SYS_SOCKETPAIR:
		ret = __sys_socketpair(a0, a1, a[2], compat_ptr(a[3]));
		break;
	case SYS_SEND:
		ret = __sys_sendto(a0, compat_ptr(a1), a[2], a[3], NULL, 0);
		break;
	case SYS_SENDTO:
		ret = __sys_sendto(a0, compat_ptr(a1), a[2], a[3],
				   compat_ptr(a[4]), a[5]);
		break;
	case SYS_RECV:
		ret = __compat_sys_recvfrom(a0, compat_ptr(a1), a[2], a[3],
					    NULL, NULL);
		break;
	case SYS_RECVFROM:
		ret = __compat_sys_recvfrom(a0, compat_ptr(a1), a[2], a[3],
					    compat_ptr(a[4]),
					    compat_ptr(a[5]));
		break;
	case SYS_SHUTDOWN:
		ret = __sys_shutdown(a0, a1);
		break;
	case SYS_SETSOCKOPT:
		ret = __sys_setsockopt(a0, a1, a[2], compat_ptr(a[3]), a[4]);
		break;
	case SYS_GETSOCKOPT:
		ret = __sys_getsockopt(a0, a1, a[2], compat_ptr(a[3]),
				       compat_ptr(a[4]));
		break;
	case SYS_SENDMSG:
		ret = __compat_sys_sendmsg(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_SENDMMSG:
		ret = __compat_sys_sendmmsg(a0, compat_ptr(a1), a[2], a[3]);
		break;
	case SYS_RECVMSG:
		ret = __compat_sys_recvmsg(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_RECVMMSG:
		ret = __sys_recvmmsg(a0, compat_ptr(a1), a[2],
				     a[3] | MSG_CMSG_COMPAT, NULL,
				     compat_ptr(a[4]));
		break;
	case SYS_ACCEPT4:
		ret = __sys_accept4(a0, compat_ptr(a1), compat_ptr(a[2]), a[3]);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
