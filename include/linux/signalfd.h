/*
 *  include/linux/signalfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_SIGNALFD_H
#define _LINUX_SIGNALFD_H


struct signalfd_siginfo {
	__u32 signo;
	__s32 err;
	__s32 code;
	__u32 pid;
	__u32 uid;
	__s32 fd;
	__u32 tid;
	__u32 band;
	__u32 overrun;
	__u32 trapno;
	__s32 status;
	__s32 svint;
	__u64 svptr;
	__u64 utime;
	__u64 stime;
	__u64 addr;

	/*
	 * Pad strcture to 128 bytes. Remember to update the
	 * pad size when you add new memebers. We use a fixed
	 * size structure to avoid compatibility problems with
	 * future versions, and we leave extra space for additional
	 * members. We use fixed size members because this strcture
	 * comes out of a read(2) and we really don't want to have
	 * a compat on read(2).
	 */
	__u8 __pad[48];
};


#ifdef __KERNEL__

#ifdef CONFIG_SIGNALFD

/*
 * Deliver the signal to listening signalfd.
 */
static inline void signalfd_notify(struct task_struct *tsk, int sig)
{
	if (unlikely(waitqueue_active(&tsk->sighand->signalfd_wqh)))
		wake_up(&tsk->sighand->signalfd_wqh);
}

#else /* CONFIG_SIGNALFD */

static inline void signalfd_notify(struct task_struct *tsk, int sig) { }

#endif /* CONFIG_SIGNALFD */

#endif /* __KERNEL__ */

#endif /* _LINUX_SIGNALFD_H */

