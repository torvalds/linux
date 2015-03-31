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
#include <linux/export.h>

#include <net/scm.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <asm/uaccess.h>
#include <net/compat.h>

ssize_t get_compat_msghdr(struct msghdr *kmsg,
			  struct compat_msghdr __user *umsg,
			  struct sockaddr __user **save_addr,
			  struct iovec **iov)
{
	compat_uptr_t uaddr, uiov, tmp3;
	compat_size_t nr_segs;
	ssize_t err;

	if (!access_ok(VERIFY_READ, umsg, sizeof(*umsg)) ||
	    __get_user(uaddr, &umsg->msg_name) ||
	    __get_user(kmsg->msg_namelen, &umsg->msg_namelen) ||
	    __get_user(uiov, &umsg->msg_iov) ||
	    __get_user(nr_segs, &umsg->msg_iovlen) ||
	    __get_user(tmp3, &umsg->msg_control) ||
	    __get_user(kmsg->msg_controllen, &umsg->msg_controllen) ||
	    __get_user(kmsg->msg_flags, &umsg->msg_flags))
		return -EFAULT;

	if (!uaddr)
		kmsg->msg_namelen = 0;

	if (kmsg->msg_namelen < 0)
		return -EINVAL;

	if (kmsg->msg_namelen > sizeof(struct sockaddr_storage))
		kmsg->msg_namelen = sizeof(struct sockaddr_storage);
	kmsg->msg_control = compat_ptr(tmp3);

	if (save_addr)
		*save_addr = compat_ptr(uaddr);

	if (uaddr && kmsg->msg_namelen) {
		if (!save_addr) {
			err = move_addr_to_kernel(compat_ptr(uaddr),
						  kmsg->msg_namelen,
						  kmsg->msg_name);
			if (err < 0)
				return err;
		}
	} else {
		kmsg->msg_name = NULL;
		kmsg->msg_namelen = 0;
	}

	if (nr_segs > UIO_MAXIOV)
		return -EMSGSIZE;

	err = compat_rw_copy_check_uvector(save_addr ? READ : WRITE,
					   compat_ptr(uiov), nr_segs,
					   UIO_FASTIOV, *iov, iov);
	if (err >= 0)
		iov_iter_init(&kmsg->msg_iter, save_addr ? READ : WRITE,
			      *iov, nr_segs, err);
	return err;
}

/* Bleech... */
#define CMSG_COMPAT_ALIGN(len)	ALIGN((len), sizeof(s32))

#define CMSG_COMPAT_DATA(cmsg)				\
	((void __user *)((char __user *)(cmsg) + CMSG_COMPAT_ALIGN(sizeof(struct compat_cmsghdr))))
#define CMSG_COMPAT_SPACE(len)				\
	(CMSG_COMPAT_ALIGN(sizeof(struct compat_cmsghdr)) + CMSG_COMPAT_ALIGN(len))
#define CMSG_COMPAT_LEN(len)				\
	(CMSG_COMPAT_ALIGN(sizeof(struct compat_cmsghdr)) + (len))

#define CMSG_COMPAT_FIRSTHDR(msg)			\
	(((msg)->msg_controllen) >= sizeof(struct compat_cmsghdr) ?	\
	 (struct compat_cmsghdr __user *)((msg)->msg_control) :		\
	 (struct compat_cmsghdr __user *)NULL)

#define CMSG_COMPAT_OK(ucmlen, ucmsg, mhdr) \
	((ucmlen) >= sizeof(struct compat_cmsghdr) && \
	 (ucmlen) <= (unsigned long) \
	 ((mhdr)->msg_controllen - \
	  ((char *)(ucmsg) - (char *)(mhdr)->msg_control)))

static inline struct compat_cmsghdr __user *cmsg_compat_nxthdr(struct msghdr *msg,
		struct compat_cmsghdr __user *cmsg, int cmsg_len)
{
	char __user *ptr = (char __user *)cmsg + CMSG_COMPAT_ALIGN(cmsg_len);
	if ((unsigned long)(ptr + 1 - (char __user *)msg->msg_control) >
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

	kcmlen = 0;
	kcmsg_base = kcmsg = (struct cmsghdr *)stackbuf;
	ucmsg = CMSG_COMPAT_FIRSTHDR(kmsg);
	while (ucmsg != NULL) {
		if (get_user(ucmlen, &ucmsg->cmsg_len))
			return -EFAULT;

		/* Catch bogons. */
		if (!CMSG_COMPAT_OK(ucmlen, ucmsg, kmsg))
			return -EINVAL;

		tmp = ((ucmlen - CMSG_COMPAT_ALIGN(sizeof(*ucmsg))) +
		       CMSG_ALIGN(sizeof(struct cmsghdr)));
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
		return -ENOBUFS;

	/* Now copy them over neatly. */
	memset(kcmsg, 0, kcmlen);
	ucmsg = CMSG_COMPAT_FIRSTHDR(kmsg);
	while (ucmsg != NULL) {
		if (__get_user(ucmlen, &ucmsg->cmsg_len))
			goto Efault;
		if (!CMSG_COMPAT_OK(ucmlen, ucmsg, kmsg))
			goto Einval;
		tmp = ((ucmlen - CMSG_COMPAT_ALIGN(sizeof(*ucmsg))) +
		       CMSG_ALIGN(sizeof(struct cmsghdr)));
		if ((char *)kcmsg_base + kcmlen - (char *)kcmsg < CMSG_ALIGN(tmp))
			goto Einval;
		kcmsg->cmsg_len = tmp;
		tmp = CMSG_ALIGN(tmp);
		if (__get_user(kcmsg->cmsg_level, &ucmsg->cmsg_level) ||
		    __get_user(kcmsg->cmsg_type, &ucmsg->cmsg_type) ||
		    copy_from_user(CMSG_DATA(kcmsg),
				   CMSG_COMPAT_DATA(ucmsg),
				   (ucmlen - CMSG_COMPAT_ALIGN(sizeof(*ucmsg)))))
			goto Efault;

		/* Advance. */
		kcmsg = (struct cmsghdr *)((char *)kcmsg + tmp);
		ucmsg = cmsg_compat_nxthdr(kmsg, ucmsg, ucmlen);
	}

	/* Ok, looks like we made it.  Hook it up and return success. */
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
	struct compat_cmsghdr __user *cm = (struct compat_cmsghdr __user *) kmsg->msg_control;
	struct compat_cmsghdr cmhdr;
	struct compat_timeval ctv;
	struct compat_timespec cts[3];
	int cmlen;

	if (cm == NULL || kmsg->msg_controllen < sizeof(*cm)) {
		kmsg->msg_flags |= MSG_CTRUNC;
		return 0; /* XXX: return error? check spec. */
	}

	if (!COMPAT_USE_64BIT_TIME) {
		if (level == SOL_SOCKET && type == SCM_TIMESTAMP) {
			struct timeval *tv = (struct timeval *)data;
			ctv.tv_sec = tv->tv_sec;
			ctv.tv_usec = tv->tv_usec;
			data = &ctv;
			len = sizeof(ctv);
		}
		if (level == SOL_SOCKET &&
		    (type == SCM_TIMESTAMPNS || type == SCM_TIMESTAMPING)) {
			int count = type == SCM_TIMESTAMPNS ? 1 : 3;
			int i;
			struct timespec *ts = (struct timespec *)data;
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
	kmsg->msg_control += cmlen;
	kmsg->msg_controllen -= cmlen;
	return 0;
}

void scm_detach_fds_compat(struct msghdr *kmsg, struct scm_cookie *scm)
{
	struct compat_cmsghdr __user *cm = (struct compat_cmsghdr __user *) kmsg->msg_control;
	int fdmax = (kmsg->msg_controllen - sizeof(struct compat_cmsghdr)) / sizeof(int);
	int fdnum = scm->fp->count;
	struct file **fp = scm->fp->fp;
	int __user *cmfptr;
	int err = 0, i;

	if (fdnum < fdmax)
		fdmax = fdnum;

	for (i = 0, cmfptr = (int __user *) CMSG_COMPAT_DATA(cm); i < fdmax; i++, cmfptr++) {
		int new_fd;
		err = security_file_receive(fp[i]);
		if (err)
			break;
		err = get_unused_fd_flags(MSG_CMSG_CLOEXEC & kmsg->msg_flags
					  ? O_CLOEXEC : 0);
		if (err < 0)
			break;
		new_fd = err;
		err = put_user(new_fd, cmfptr);
		if (err) {
			put_unused_fd(new_fd);
			break;
		}
		/* Bump the usage count and install the file. */
		fd_install(new_fd, get_file(fp[i]));
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
			kmsg->msg_control += cmlen;
			kmsg->msg_controllen -= cmlen;
		}
	}
	if (i < fdnum)
		kmsg->msg_flags |= MSG_CTRUNC;

	/*
	 * All of the files that fit in the message have had their
	 * usage counts incremented, so we just free the list.
	 */
	__scm_destroy(scm);
}

static int do_set_attach_filter(struct socket *sock, int level, int optname,
				char __user *optval, unsigned int optlen)
{
	struct compat_sock_fprog __user *fprog32 = (struct compat_sock_fprog __user *)optval;
	struct sock_fprog __user *kfprog = compat_alloc_user_space(sizeof(struct sock_fprog));
	compat_uptr_t ptr;
	u16 len;

	if (!access_ok(VERIFY_READ, fprog32, sizeof(*fprog32)) ||
	    !access_ok(VERIFY_WRITE, kfprog, sizeof(struct sock_fprog)) ||
	    __get_user(len, &fprog32->len) ||
	    __get_user(ptr, &fprog32->filter) ||
	    __put_user(len, &kfprog->len) ||
	    __put_user(compat_ptr(ptr), &kfprog->filter))
		return -EFAULT;

	return sock_setsockopt(sock, level, optname, (char __user *)kfprog,
			      sizeof(struct sock_fprog));
}

static int do_set_sock_timeout(struct socket *sock, int level,
		int optname, char __user *optval, unsigned int optlen)
{
	struct compat_timeval __user *up = (struct compat_timeval __user *)optval;
	struct timeval ktime;
	mm_segment_t old_fs;
	int err;

	if (optlen < sizeof(*up))
		return -EINVAL;
	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    __get_user(ktime.tv_sec, &up->tv_sec) ||
	    __get_user(ktime.tv_usec, &up->tv_usec))
		return -EFAULT;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sock_setsockopt(sock, level, optname, (char *)&ktime, sizeof(ktime));
	set_fs(old_fs);

	return err;
}

static int compat_sock_setsockopt(struct socket *sock, int level, int optname,
				char __user *optval, unsigned int optlen)
{
	if (optname == SO_ATTACH_FILTER)
		return do_set_attach_filter(sock, level, optname,
					    optval, optlen);
	if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
		return do_set_sock_timeout(sock, level, optname, optval, optlen);

	return sock_setsockopt(sock, level, optname, optval, optlen);
}

COMPAT_SYSCALL_DEFINE5(setsockopt, int, fd, int, level, int, optname,
		       char __user *, optval, unsigned int, optlen)
{
	int err;
	struct socket *sock = sockfd_lookup(fd, &err);

	if (sock) {
		err = security_socket_setsockopt(sock, level, optname);
		if (err) {
			sockfd_put(sock);
			return err;
		}

		if (level == SOL_SOCKET)
			err = compat_sock_setsockopt(sock, level,
					optname, optval, optlen);
		else if (sock->ops->compat_setsockopt)
			err = sock->ops->compat_setsockopt(sock, level,
					optname, optval, optlen);
		else
			err = sock->ops->setsockopt(sock, level,
					optname, optval, optlen);
		sockfd_put(sock);
	}
	return err;
}

static int do_get_sock_timeout(struct socket *sock, int level, int optname,
		char __user *optval, int __user *optlen)
{
	struct compat_timeval __user *up;
	struct timeval ktime;
	mm_segment_t old_fs;
	int len, err;

	up = (struct compat_timeval __user *) optval;
	if (get_user(len, optlen))
		return -EFAULT;
	if (len < sizeof(*up))
		return -EINVAL;
	len = sizeof(ktime);
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sock_getsockopt(sock, level, optname, (char *) &ktime, &len);
	set_fs(old_fs);

	if (!err) {
		if (put_user(sizeof(*up), optlen) ||
		    !access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
		    __put_user(ktime.tv_sec, &up->tv_sec) ||
		    __put_user(ktime.tv_usec, &up->tv_usec))
			err = -EFAULT;
	}
	return err;
}

static int compat_sock_getsockopt(struct socket *sock, int level, int optname,
				char __user *optval, int __user *optlen)
{
	if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
		return do_get_sock_timeout(sock, level, optname, optval, optlen);
	return sock_getsockopt(sock, level, optname, optval, optlen);
}

int compat_sock_get_timestamp(struct sock *sk, struct timeval __user *userstamp)
{
	struct compat_timeval __user *ctv;
	int err;
	struct timeval tv;

	if (COMPAT_USE_64BIT_TIME)
		return sock_get_timestamp(sk, userstamp);

	ctv = (struct compat_timeval __user *) userstamp;
	err = -ENOENT;
	if (!sock_flag(sk, SOCK_TIMESTAMP))
		sock_enable_timestamp(sk, SOCK_TIMESTAMP);
	tv = ktime_to_timeval(sk->sk_stamp);
	if (tv.tv_sec == -1)
		return err;
	if (tv.tv_sec == 0) {
		sk->sk_stamp = ktime_get_real();
		tv = ktime_to_timeval(sk->sk_stamp);
	}
	err = 0;
	if (put_user(tv.tv_sec, &ctv->tv_sec) ||
			put_user(tv.tv_usec, &ctv->tv_usec))
		err = -EFAULT;
	return err;
}
EXPORT_SYMBOL(compat_sock_get_timestamp);

int compat_sock_get_timestampns(struct sock *sk, struct timespec __user *userstamp)
{
	struct compat_timespec __user *ctv;
	int err;
	struct timespec ts;

	if (COMPAT_USE_64BIT_TIME)
		return sock_get_timestampns (sk, userstamp);

	ctv = (struct compat_timespec __user *) userstamp;
	err = -ENOENT;
	if (!sock_flag(sk, SOCK_TIMESTAMP))
		sock_enable_timestamp(sk, SOCK_TIMESTAMP);
	ts = ktime_to_timespec(sk->sk_stamp);
	if (ts.tv_sec == -1)
		return err;
	if (ts.tv_sec == 0) {
		sk->sk_stamp = ktime_get_real();
		ts = ktime_to_timespec(sk->sk_stamp);
	}
	err = 0;
	if (put_user(ts.tv_sec, &ctv->tv_sec) ||
			put_user(ts.tv_nsec, &ctv->tv_nsec))
		err = -EFAULT;
	return err;
}
EXPORT_SYMBOL(compat_sock_get_timestampns);

COMPAT_SYSCALL_DEFINE5(getsockopt, int, fd, int, level, int, optname,
		       char __user *, optval, int __user *, optlen)
{
	int err;
	struct socket *sock = sockfd_lookup(fd, &err);

	if (sock) {
		err = security_socket_getsockopt(sock, level, optname);
		if (err) {
			sockfd_put(sock);
			return err;
		}

		if (level == SOL_SOCKET)
			err = compat_sock_getsockopt(sock, level,
					optname, optval, optlen);
		else if (sock->ops->compat_getsockopt)
			err = sock->ops->compat_getsockopt(sock, level,
					optname, optval, optlen);
		else
			err = sock->ops->getsockopt(sock, level,
					optname, optval, optlen);
		sockfd_put(sock);
	}
	return err;
}

struct compat_group_req {
	__u32				 gr_interface;
	struct __kernel_sockaddr_storage gr_group
		__attribute__ ((aligned(4)));
} __packed;

struct compat_group_source_req {
	__u32				 gsr_interface;
	struct __kernel_sockaddr_storage gsr_group
		__attribute__ ((aligned(4)));
	struct __kernel_sockaddr_storage gsr_source
		__attribute__ ((aligned(4)));
} __packed;

struct compat_group_filter {
	__u32				 gf_interface;
	struct __kernel_sockaddr_storage gf_group
		__attribute__ ((aligned(4)));
	__u32				 gf_fmode;
	__u32				 gf_numsrc;
	struct __kernel_sockaddr_storage gf_slist[1]
		__attribute__ ((aligned(4)));
} __packed;

#define __COMPAT_GF0_SIZE (sizeof(struct compat_group_filter) - \
			sizeof(struct __kernel_sockaddr_storage))


int compat_mc_setsockopt(struct sock *sock, int level, int optname,
	char __user *optval, unsigned int optlen,
	int (*setsockopt)(struct sock *, int, int, char __user *, unsigned int))
{
	char __user	*koptval = optval;
	int		koptlen = optlen;

	switch (optname) {
	case MCAST_JOIN_GROUP:
	case MCAST_LEAVE_GROUP:
	{
		struct compat_group_req __user *gr32 = (void *)optval;
		struct group_req __user *kgr =
			compat_alloc_user_space(sizeof(struct group_req));
		u32 interface;

		if (!access_ok(VERIFY_READ, gr32, sizeof(*gr32)) ||
		    !access_ok(VERIFY_WRITE, kgr, sizeof(struct group_req)) ||
		    __get_user(interface, &gr32->gr_interface) ||
		    __put_user(interface, &kgr->gr_interface) ||
		    copy_in_user(&kgr->gr_group, &gr32->gr_group,
				sizeof(kgr->gr_group)))
			return -EFAULT;
		koptval = (char __user *)kgr;
		koptlen = sizeof(struct group_req);
		break;
	}
	case MCAST_JOIN_SOURCE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
	{
		struct compat_group_source_req __user *gsr32 = (void *)optval;
		struct group_source_req __user *kgsr = compat_alloc_user_space(
			sizeof(struct group_source_req));
		u32 interface;

		if (!access_ok(VERIFY_READ, gsr32, sizeof(*gsr32)) ||
		    !access_ok(VERIFY_WRITE, kgsr,
			sizeof(struct group_source_req)) ||
		    __get_user(interface, &gsr32->gsr_interface) ||
		    __put_user(interface, &kgsr->gsr_interface) ||
		    copy_in_user(&kgsr->gsr_group, &gsr32->gsr_group,
				sizeof(kgsr->gsr_group)) ||
		    copy_in_user(&kgsr->gsr_source, &gsr32->gsr_source,
				sizeof(kgsr->gsr_source)))
			return -EFAULT;
		koptval = (char __user *)kgsr;
		koptlen = sizeof(struct group_source_req);
		break;
	}
	case MCAST_MSFILTER:
	{
		struct compat_group_filter __user *gf32 = (void *)optval;
		struct group_filter __user *kgf;
		u32 interface, fmode, numsrc;

		if (!access_ok(VERIFY_READ, gf32, __COMPAT_GF0_SIZE) ||
		    __get_user(interface, &gf32->gf_interface) ||
		    __get_user(fmode, &gf32->gf_fmode) ||
		    __get_user(numsrc, &gf32->gf_numsrc))
			return -EFAULT;
		koptlen = optlen + sizeof(struct group_filter) -
				sizeof(struct compat_group_filter);
		if (koptlen < GROUP_FILTER_SIZE(numsrc))
			return -EINVAL;
		kgf = compat_alloc_user_space(koptlen);
		if (!access_ok(VERIFY_WRITE, kgf, koptlen) ||
		    __put_user(interface, &kgf->gf_interface) ||
		    __put_user(fmode, &kgf->gf_fmode) ||
		    __put_user(numsrc, &kgf->gf_numsrc) ||
		    copy_in_user(&kgf->gf_group, &gf32->gf_group,
				sizeof(kgf->gf_group)) ||
		    (numsrc && copy_in_user(kgf->gf_slist, gf32->gf_slist,
				numsrc * sizeof(kgf->gf_slist[0]))))
			return -EFAULT;
		koptval = (char __user *)kgf;
		break;
	}

	default:
		break;
	}
	return setsockopt(sock, level, optname, koptval, koptlen);
}
EXPORT_SYMBOL(compat_mc_setsockopt);

int compat_mc_getsockopt(struct sock *sock, int level, int optname,
	char __user *optval, int __user *optlen,
	int (*getsockopt)(struct sock *, int, int, char __user *, int __user *))
{
	struct compat_group_filter __user *gf32 = (void *)optval;
	struct group_filter __user *kgf;
	int __user	*koptlen;
	u32 interface, fmode, numsrc;
	int klen, ulen, err;

	if (optname != MCAST_MSFILTER)
		return getsockopt(sock, level, optname, optval, optlen);

	koptlen = compat_alloc_user_space(sizeof(*koptlen));
	if (!access_ok(VERIFY_READ, optlen, sizeof(*optlen)) ||
	    __get_user(ulen, optlen))
		return -EFAULT;

	/* adjust len for pad */
	klen = ulen + sizeof(*kgf) - sizeof(*gf32);

	if (klen < GROUP_FILTER_SIZE(0))
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, koptlen, sizeof(*koptlen)) ||
	    __put_user(klen, koptlen))
		return -EFAULT;

	/* have to allow space for previous compat_alloc_user_space, too */
	kgf = compat_alloc_user_space(klen+sizeof(*optlen));

	if (!access_ok(VERIFY_READ, gf32, __COMPAT_GF0_SIZE) ||
	    __get_user(interface, &gf32->gf_interface) ||
	    __get_user(fmode, &gf32->gf_fmode) ||
	    __get_user(numsrc, &gf32->gf_numsrc) ||
	    __put_user(interface, &kgf->gf_interface) ||
	    __put_user(fmode, &kgf->gf_fmode) ||
	    __put_user(numsrc, &kgf->gf_numsrc) ||
	    copy_in_user(&kgf->gf_group, &gf32->gf_group, sizeof(kgf->gf_group)))
		return -EFAULT;

	err = getsockopt(sock, level, optname, (char __user *)kgf, koptlen);
	if (err)
		return err;

	if (!access_ok(VERIFY_READ, koptlen, sizeof(*koptlen)) ||
	    __get_user(klen, koptlen))
		return -EFAULT;

	ulen = klen - (sizeof(*kgf)-sizeof(*gf32));

	if (!access_ok(VERIFY_WRITE, optlen, sizeof(*optlen)) ||
	    __put_user(ulen, optlen))
		return -EFAULT;

	if (!access_ok(VERIFY_READ, kgf, klen) ||
	    !access_ok(VERIFY_WRITE, gf32, ulen) ||
	    __get_user(interface, &kgf->gf_interface) ||
	    __get_user(fmode, &kgf->gf_fmode) ||
	    __get_user(numsrc, &kgf->gf_numsrc) ||
	    __put_user(interface, &gf32->gf_interface) ||
	    __put_user(fmode, &gf32->gf_fmode) ||
	    __put_user(numsrc, &gf32->gf_numsrc))
		return -EFAULT;
	if (numsrc) {
		int copylen;

		klen -= GROUP_FILTER_SIZE(0);
		copylen = numsrc * sizeof(gf32->gf_slist[0]);
		if (copylen > klen)
			copylen = klen;
		if (copy_in_user(gf32->gf_slist, kgf->gf_slist, copylen))
			return -EFAULT;
	}
	return err;
}
EXPORT_SYMBOL(compat_mc_getsockopt);


/* Argument list sizes for compat_sys_socketcall */
#define AL(x) ((x) * sizeof(u32))
static unsigned char nas[21] = {
	AL(0), AL(3), AL(3), AL(3), AL(2), AL(3),
	AL(3), AL(3), AL(4), AL(4), AL(4), AL(6),
	AL(6), AL(2), AL(5), AL(5), AL(3), AL(3),
	AL(4), AL(5), AL(4)
};
#undef AL

COMPAT_SYSCALL_DEFINE3(sendmsg, int, fd, struct compat_msghdr __user *, msg, unsigned int, flags)
{
	return __sys_sendmsg(fd, (struct user_msghdr __user *)msg, flags | MSG_CMSG_COMPAT);
}

COMPAT_SYSCALL_DEFINE4(sendmmsg, int, fd, struct compat_mmsghdr __user *, mmsg,
		       unsigned int, vlen, unsigned int, flags)
{
	return __sys_sendmmsg(fd, (struct mmsghdr __user *)mmsg, vlen,
			      flags | MSG_CMSG_COMPAT);
}

COMPAT_SYSCALL_DEFINE3(recvmsg, int, fd, struct compat_msghdr __user *, msg, unsigned int, flags)
{
	return __sys_recvmsg(fd, (struct user_msghdr __user *)msg, flags | MSG_CMSG_COMPAT);
}

COMPAT_SYSCALL_DEFINE4(recv, int, fd, void __user *, buf, compat_size_t, len, unsigned int, flags)
{
	return sys_recv(fd, buf, len, flags | MSG_CMSG_COMPAT);
}

COMPAT_SYSCALL_DEFINE6(recvfrom, int, fd, void __user *, buf, compat_size_t, len,
		       unsigned int, flags, struct sockaddr __user *, addr,
		       int __user *, addrlen)
{
	return sys_recvfrom(fd, buf, len, flags | MSG_CMSG_COMPAT, addr, addrlen);
}

COMPAT_SYSCALL_DEFINE5(recvmmsg, int, fd, struct compat_mmsghdr __user *, mmsg,
		       unsigned int, vlen, unsigned int, flags,
		       struct compat_timespec __user *, timeout)
{
	int datagrams;
	struct timespec ktspec;

	if (timeout == NULL)
		return __sys_recvmmsg(fd, (struct mmsghdr __user *)mmsg, vlen,
				      flags | MSG_CMSG_COMPAT, NULL);

	if (compat_get_timespec(&ktspec, timeout))
		return -EFAULT;

	datagrams = __sys_recvmmsg(fd, (struct mmsghdr __user *)mmsg, vlen,
				   flags | MSG_CMSG_COMPAT, &ktspec);
	if (datagrams > 0 && compat_put_timespec(&ktspec, timeout))
		datagrams = -EFAULT;

	return datagrams;
}

COMPAT_SYSCALL_DEFINE2(socketcall, int, call, u32 __user *, args)
{
	int ret;
	u32 a[6];
	u32 a0, a1;

	if (call < SYS_SOCKET || call > SYS_SENDMMSG)
		return -EINVAL;
	if (copy_from_user(a, args, nas[call]))
		return -EFAULT;
	a0 = a[0];
	a1 = a[1];

	switch (call) {
	case SYS_SOCKET:
		ret = sys_socket(a0, a1, a[2]);
		break;
	case SYS_BIND:
		ret = sys_bind(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_CONNECT:
		ret = sys_connect(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_LISTEN:
		ret = sys_listen(a0, a1);
		break;
	case SYS_ACCEPT:
		ret = sys_accept4(a0, compat_ptr(a1), compat_ptr(a[2]), 0);
		break;
	case SYS_GETSOCKNAME:
		ret = sys_getsockname(a0, compat_ptr(a1), compat_ptr(a[2]));
		break;
	case SYS_GETPEERNAME:
		ret = sys_getpeername(a0, compat_ptr(a1), compat_ptr(a[2]));
		break;
	case SYS_SOCKETPAIR:
		ret = sys_socketpair(a0, a1, a[2], compat_ptr(a[3]));
		break;
	case SYS_SEND:
		ret = sys_send(a0, compat_ptr(a1), a[2], a[3]);
		break;
	case SYS_SENDTO:
		ret = sys_sendto(a0, compat_ptr(a1), a[2], a[3], compat_ptr(a[4]), a[5]);
		break;
	case SYS_RECV:
		ret = compat_sys_recv(a0, compat_ptr(a1), a[2], a[3]);
		break;
	case SYS_RECVFROM:
		ret = compat_sys_recvfrom(a0, compat_ptr(a1), a[2], a[3],
					  compat_ptr(a[4]), compat_ptr(a[5]));
		break;
	case SYS_SHUTDOWN:
		ret = sys_shutdown(a0, a1);
		break;
	case SYS_SETSOCKOPT:
		ret = compat_sys_setsockopt(a0, a1, a[2],
				compat_ptr(a[3]), a[4]);
		break;
	case SYS_GETSOCKOPT:
		ret = compat_sys_getsockopt(a0, a1, a[2],
				compat_ptr(a[3]), compat_ptr(a[4]));
		break;
	case SYS_SENDMSG:
		ret = compat_sys_sendmsg(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_SENDMMSG:
		ret = compat_sys_sendmmsg(a0, compat_ptr(a1), a[2], a[3]);
		break;
	case SYS_RECVMSG:
		ret = compat_sys_recvmsg(a0, compat_ptr(a1), a[2]);
		break;
	case SYS_RECVMMSG:
		ret = compat_sys_recvmmsg(a0, compat_ptr(a1), a[2], a[3],
					  compat_ptr(a[4]));
		break;
	case SYS_ACCEPT4:
		ret = sys_accept4(a0, compat_ptr(a1), compat_ptr(a[2]), a[3]);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
