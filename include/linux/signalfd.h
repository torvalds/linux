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
 * Deliver the signal to listening signalfd. This must be called
 * with the sighand lock held. Same are the following that end up
 * calling signalfd_deliver().
 */
void signalfd_deliver(struct task_struct *tsk, int sig);

/*
 * No need to fall inside signalfd_deliver() if no signal listeners
 * are available.
 */
static inline void signalfd_notify(struct task_struct *tsk, int sig)
{
	if (unlikely(!list_empty(&tsk->sighand->signalfd_list)))
		signalfd_deliver(tsk, sig);
}

/*
 * The signal -1 is used to notify the signalfd that the sighand
 * is on its way to be detached.
 */
static inline void signalfd_detach_locked(struct task_struct *tsk)
{
	if (unlikely(!list_empty(&tsk->sighand->signalfd_list)))
		signalfd_deliver(tsk, -1);
}

static inline void signalfd_detach(struct task_struct *tsk)
{
	struct sighand_struct *sighand = tsk->sighand;

	if (unlikely(!list_empty(&sighand->signalfd_list))) {
		spin_lock_irq(&sighand->siglock);
		signalfd_deliver(tsk, -1);
		spin_unlock_irq(&sighand->siglock);
	}
}

#else /* CONFIG_SIGNALFD */

#define signalfd_deliver(t, s) do { } while (0)
#define signalfd_notify(t, s) do { } while (0)
#define signalfd_detach_locked(t) do { } while (0)
#define signalfd_detach(t) do { } while (0)

#endif /* CONFIG_SIGNALFD */

#endif /* __KERNEL__ */

#endif /* _LINUX_SIGNALFD_H */

