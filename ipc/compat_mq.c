/*
 *  ipc/compat_mq.c
 *    32 bit emulation for POSIX message queue system calls
 *
 *    Copyright (C) 2004 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author: Arnd Bergmann <arnd@arndb.de>
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mqueue.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>

struct compat_mq_attr {
	compat_long_t mq_flags;      /* message queue flags		     */
	compat_long_t mq_maxmsg;     /* maximum number of messages	     */
	compat_long_t mq_msgsize;    /* maximum message size		     */
	compat_long_t mq_curmsgs;    /* number of messages currently queued  */
	compat_long_t __reserved[4]; /* ignored for input, zeroed for output */
};

static inline int get_compat_mq_attr(struct mq_attr *attr,
			const struct compat_mq_attr __user *uattr)
{
	if (!access_ok(VERIFY_READ, uattr, sizeof *uattr))
		return -EFAULT;

	return __get_user(attr->mq_flags, &uattr->mq_flags)
		| __get_user(attr->mq_maxmsg, &uattr->mq_maxmsg)
		| __get_user(attr->mq_msgsize, &uattr->mq_msgsize)
		| __get_user(attr->mq_curmsgs, &uattr->mq_curmsgs);
}

static inline int put_compat_mq_attr(const struct mq_attr *attr,
			struct compat_mq_attr __user *uattr)
{
	if (clear_user(uattr, sizeof *uattr))
		return -EFAULT;

	return __put_user(attr->mq_flags, &uattr->mq_flags)
		| __put_user(attr->mq_maxmsg, &uattr->mq_maxmsg)
		| __put_user(attr->mq_msgsize, &uattr->mq_msgsize)
		| __put_user(attr->mq_curmsgs, &uattr->mq_curmsgs);
}

asmlinkage long compat_sys_mq_open(const char __user *u_name,
			int oflag, compat_mode_t mode,
			struct compat_mq_attr __user *u_attr)
{
	void __user *p = NULL;
	if (u_attr && oflag & O_CREAT) {
		struct mq_attr attr;

		memset(&attr, 0, sizeof(attr));

		p = compat_alloc_user_space(sizeof(attr));
		if (get_compat_mq_attr(&attr, u_attr) ||
		    copy_to_user(p, &attr, sizeof(attr)))
			return -EFAULT;
	}
	return sys_mq_open(u_name, oflag, mode, p);
}

static int compat_prepare_timeout(struct timespec __user * *p,
				  const struct compat_timespec __user *u)
{
	struct timespec ts;
	if (!u) {
		*p = NULL;
		return 0;
	}
	*p = compat_alloc_user_space(sizeof(ts));
	if (get_compat_timespec(&ts, u) || copy_to_user(*p, &ts, sizeof(ts)))
		return -EFAULT;
	return 0;
}

asmlinkage long compat_sys_mq_timedsend(mqd_t mqdes,
			const char __user *u_msg_ptr,
			size_t msg_len, unsigned int msg_prio,
			const struct compat_timespec __user *u_abs_timeout)
{
	struct timespec __user *u_ts;

	if (compat_prepare_timeout(&u_ts, u_abs_timeout))
		return -EFAULT;

	return sys_mq_timedsend(mqdes, u_msg_ptr, msg_len,
			msg_prio, u_ts);
}

asmlinkage ssize_t compat_sys_mq_timedreceive(mqd_t mqdes,
			char __user *u_msg_ptr,
			size_t msg_len, unsigned int __user *u_msg_prio,
			const struct compat_timespec __user *u_abs_timeout)
{
	struct timespec __user *u_ts;
	if (compat_prepare_timeout(&u_ts, u_abs_timeout))
		return -EFAULT;

	return sys_mq_timedreceive(mqdes, u_msg_ptr, msg_len,
			u_msg_prio, u_ts);
}

asmlinkage long compat_sys_mq_notify(mqd_t mqdes,
			const struct compat_sigevent __user *u_notification)
{
	struct sigevent __user *p = NULL;
	if (u_notification) {
		struct sigevent n;
		p = compat_alloc_user_space(sizeof(*p));
		if (get_compat_sigevent(&n, u_notification))
			return -EFAULT;
		if (n.sigev_notify == SIGEV_THREAD)
			n.sigev_value.sival_ptr = compat_ptr(n.sigev_value.sival_int);
		if (copy_to_user(p, &n, sizeof(*p)))
			return -EFAULT;
	}
	return sys_mq_notify(mqdes, p);
}

asmlinkage long compat_sys_mq_getsetattr(mqd_t mqdes,
			const struct compat_mq_attr __user *u_mqstat,
			struct compat_mq_attr __user *u_omqstat)
{
	struct mq_attr mqstat;
	struct mq_attr __user *p = compat_alloc_user_space(2 * sizeof(*p));
	long ret;

	memset(&mqstat, 0, sizeof(mqstat));

	if (u_mqstat) {
		if (get_compat_mq_attr(&mqstat, u_mqstat) ||
		    copy_to_user(p, &mqstat, sizeof(mqstat)))
			return -EFAULT;
	}
	ret = sys_mq_getsetattr(mqdes,
				u_mqstat ? p : NULL,
				u_omqstat ? p + 1 : NULL);
	if (ret)
		return ret;
	if (u_omqstat) {
		if (copy_from_user(&mqstat, p + 1, sizeof(mqstat)) ||
		    put_compat_mq_attr(&mqstat, u_omqstat))
			return -EFAULT;
	}
	return 0;
}
