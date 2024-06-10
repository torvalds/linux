// SPDX-License-Identifier: GPL-2.0
/*
 *  fs/timerfd.c
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 *
 *  Thanks to Thomas Gleixner for code reviews and useful comments.
 *
 */

#include <linux/alarmtimer.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/anon_inodes.h>
#include <linux/timerfd.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/rcupdate.h>
#include <linux/time_namespace.h>

struct timerfd_ctx {
	union {
		struct hrtimer tmr;
		struct alarm alarm;
	} t;
	ktime_t tintv;
	ktime_t moffs;
	wait_queue_head_t wqh;
	u64 ticks;
	int clockid;
	short unsigned expired;
	short unsigned settime_flags;	/* to show in fdinfo */
	struct rcu_head rcu;
	struct list_head clist;
	spinlock_t cancel_lock;
	bool might_cancel;
};

static LIST_HEAD(cancel_list);
static DEFINE_SPINLOCK(cancel_lock);

static inline bool isalarm(struct timerfd_ctx *ctx)
{
	return ctx->clockid == CLOCK_REALTIME_ALARM ||
		ctx->clockid == CLOCK_BOOTTIME_ALARM;
}

/*
 * This gets called when the timer event triggers. We set the "expired"
 * flag, but we do not re-arm the timer (in case it's necessary,
 * tintv != 0) until the timer is accessed.
 */
static void timerfd_triggered(struct timerfd_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->wqh.lock, flags);
	ctx->expired = 1;
	ctx->ticks++;
	wake_up_locked_poll(&ctx->wqh, EPOLLIN);
	spin_unlock_irqrestore(&ctx->wqh.lock, flags);
}

static enum hrtimer_restart timerfd_tmrproc(struct hrtimer *htmr)
{
	struct timerfd_ctx *ctx = container_of(htmr, struct timerfd_ctx,
					       t.tmr);
	timerfd_triggered(ctx);
	return HRTIMER_NORESTART;
}

static enum alarmtimer_restart timerfd_alarmproc(struct alarm *alarm,
	ktime_t now)
{
	struct timerfd_ctx *ctx = container_of(alarm, struct timerfd_ctx,
					       t.alarm);
	timerfd_triggered(ctx);
	return ALARMTIMER_NORESTART;
}

/*
 * Called when the clock was set to cancel the timers in the cancel
 * list. This will wake up processes waiting on these timers. The
 * wake-up requires ctx->ticks to be non zero, therefore we increment
 * it before calling wake_up_locked().
 */
void timerfd_clock_was_set(void)
{
	ktime_t moffs = ktime_mono_to_real(0);
	struct timerfd_ctx *ctx;
	unsigned long flags;

	rcu_read_lock();
	list_for_each_entry_rcu(ctx, &cancel_list, clist) {
		if (!ctx->might_cancel)
			continue;
		spin_lock_irqsave(&ctx->wqh.lock, flags);
		if (ctx->moffs != moffs) {
			ctx->moffs = KTIME_MAX;
			ctx->ticks++;
			wake_up_locked_poll(&ctx->wqh, EPOLLIN);
		}
		spin_unlock_irqrestore(&ctx->wqh.lock, flags);
	}
	rcu_read_unlock();
}

static void timerfd_resume_work(struct work_struct *work)
{
	timerfd_clock_was_set();
}

static DECLARE_WORK(timerfd_work, timerfd_resume_work);

/*
 * Invoked from timekeeping_resume(). Defer the actual update to work so
 * timerfd_clock_was_set() runs in task context.
 */
void timerfd_resume(void)
{
	schedule_work(&timerfd_work);
}

static void __timerfd_remove_cancel(struct timerfd_ctx *ctx)
{
	if (ctx->might_cancel) {
		ctx->might_cancel = false;
		spin_lock(&cancel_lock);
		list_del_rcu(&ctx->clist);
		spin_unlock(&cancel_lock);
	}
}

static void timerfd_remove_cancel(struct timerfd_ctx *ctx)
{
	spin_lock(&ctx->cancel_lock);
	__timerfd_remove_cancel(ctx);
	spin_unlock(&ctx->cancel_lock);
}

static bool timerfd_canceled(struct timerfd_ctx *ctx)
{
	if (!ctx->might_cancel || ctx->moffs != KTIME_MAX)
		return false;
	ctx->moffs = ktime_mono_to_real(0);
	return true;
}

static void timerfd_setup_cancel(struct timerfd_ctx *ctx, int flags)
{
	spin_lock(&ctx->cancel_lock);
	if ((ctx->clockid == CLOCK_REALTIME ||
	     ctx->clockid == CLOCK_REALTIME_ALARM) &&
	    (flags & TFD_TIMER_ABSTIME) && (flags & TFD_TIMER_CANCEL_ON_SET)) {
		if (!ctx->might_cancel) {
			ctx->might_cancel = true;
			spin_lock(&cancel_lock);
			list_add_rcu(&ctx->clist, &cancel_list);
			spin_unlock(&cancel_lock);
		}
	} else {
		__timerfd_remove_cancel(ctx);
	}
	spin_unlock(&ctx->cancel_lock);
}

static ktime_t timerfd_get_remaining(struct timerfd_ctx *ctx)
{
	ktime_t remaining;

	if (isalarm(ctx))
		remaining = alarm_expires_remaining(&ctx->t.alarm);
	else
		remaining = hrtimer_expires_remaining_adjusted(&ctx->t.tmr);

	return remaining < 0 ? 0: remaining;
}

static int timerfd_setup(struct timerfd_ctx *ctx, int flags,
			 const struct itimerspec64 *ktmr)
{
	enum hrtimer_mode htmode;
	ktime_t texp;
	int clockid = ctx->clockid;

	htmode = (flags & TFD_TIMER_ABSTIME) ?
		HRTIMER_MODE_ABS: HRTIMER_MODE_REL;

	texp = timespec64_to_ktime(ktmr->it_value);
	ctx->expired = 0;
	ctx->ticks = 0;
	ctx->tintv = timespec64_to_ktime(ktmr->it_interval);

	if (isalarm(ctx)) {
		alarm_init(&ctx->t.alarm,
			   ctx->clockid == CLOCK_REALTIME_ALARM ?
			   ALARM_REALTIME : ALARM_BOOTTIME,
			   timerfd_alarmproc);
	} else {
		hrtimer_init(&ctx->t.tmr, clockid, htmode);
		hrtimer_set_expires(&ctx->t.tmr, texp);
		ctx->t.tmr.function = timerfd_tmrproc;
	}

	if (texp != 0) {
		if (flags & TFD_TIMER_ABSTIME)
			texp = timens_ktime_to_host(clockid, texp);
		if (isalarm(ctx)) {
			if (flags & TFD_TIMER_ABSTIME)
				alarm_start(&ctx->t.alarm, texp);
			else
				alarm_start_relative(&ctx->t.alarm, texp);
		} else {
			hrtimer_start(&ctx->t.tmr, texp, htmode);
		}

		if (timerfd_canceled(ctx))
			return -ECANCELED;
	}

	ctx->settime_flags = flags & TFD_SETTIME_FLAGS;
	return 0;
}

static int timerfd_release(struct inode *inode, struct file *file)
{
	struct timerfd_ctx *ctx = file->private_data;

	timerfd_remove_cancel(ctx);

	if (isalarm(ctx))
		alarm_cancel(&ctx->t.alarm);
	else
		hrtimer_cancel(&ctx->t.tmr);
	kfree_rcu(ctx, rcu);
	return 0;
}

static __poll_t timerfd_poll(struct file *file, poll_table *wait)
{
	struct timerfd_ctx *ctx = file->private_data;
	__poll_t events = 0;
	unsigned long flags;

	poll_wait(file, &ctx->wqh, wait);

	spin_lock_irqsave(&ctx->wqh.lock, flags);
	if (ctx->ticks)
		events |= EPOLLIN;
	spin_unlock_irqrestore(&ctx->wqh.lock, flags);

	return events;
}

static ssize_t timerfd_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct timerfd_ctx *ctx = file->private_data;
	ssize_t res;
	u64 ticks = 0;

	if (iov_iter_count(to) < sizeof(ticks))
		return -EINVAL;

	spin_lock_irq(&ctx->wqh.lock);
	if (file->f_flags & O_NONBLOCK || iocb->ki_flags & IOCB_NOWAIT)
		res = -EAGAIN;
	else
		res = wait_event_interruptible_locked_irq(ctx->wqh, ctx->ticks);

	/*
	 * If clock has changed, we do not care about the
	 * ticks and we do not rearm the timer. Userspace must
	 * reevaluate anyway.
	 */
	if (timerfd_canceled(ctx)) {
		ctx->ticks = 0;
		ctx->expired = 0;
		res = -ECANCELED;
	}

	if (ctx->ticks) {
		ticks = ctx->ticks;

		if (ctx->expired && ctx->tintv) {
			/*
			 * If tintv != 0, this is a periodic timer that
			 * needs to be re-armed. We avoid doing it in the timer
			 * callback to avoid DoS attacks specifying a very
			 * short timer period.
			 */
			if (isalarm(ctx)) {
				ticks += alarm_forward_now(
					&ctx->t.alarm, ctx->tintv) - 1;
				alarm_restart(&ctx->t.alarm);
			} else {
				ticks += hrtimer_forward_now(&ctx->t.tmr,
							     ctx->tintv) - 1;
				hrtimer_restart(&ctx->t.tmr);
			}
		}
		ctx->expired = 0;
		ctx->ticks = 0;
	}
	spin_unlock_irq(&ctx->wqh.lock);
	if (ticks) {
		res = copy_to_iter(&ticks, sizeof(ticks), to);
		if (!res)
			res = -EFAULT;
	}
	return res;
}

#ifdef CONFIG_PROC_FS
static void timerfd_show(struct seq_file *m, struct file *file)
{
	struct timerfd_ctx *ctx = file->private_data;
	struct timespec64 value, interval;

	spin_lock_irq(&ctx->wqh.lock);
	value = ktime_to_timespec64(timerfd_get_remaining(ctx));
	interval = ktime_to_timespec64(ctx->tintv);
	spin_unlock_irq(&ctx->wqh.lock);

	seq_printf(m,
		   "clockid: %d\n"
		   "ticks: %llu\n"
		   "settime flags: 0%o\n"
		   "it_value: (%llu, %llu)\n"
		   "it_interval: (%llu, %llu)\n",
		   ctx->clockid,
		   (unsigned long long)ctx->ticks,
		   ctx->settime_flags,
		   (unsigned long long)value.tv_sec,
		   (unsigned long long)value.tv_nsec,
		   (unsigned long long)interval.tv_sec,
		   (unsigned long long)interval.tv_nsec);
}
#else
#define timerfd_show NULL
#endif

#ifdef CONFIG_CHECKPOINT_RESTORE
static long timerfd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct timerfd_ctx *ctx = file->private_data;
	int ret = 0;

	switch (cmd) {
	case TFD_IOC_SET_TICKS: {
		u64 ticks;

		if (copy_from_user(&ticks, (u64 __user *)arg, sizeof(ticks)))
			return -EFAULT;
		if (!ticks)
			return -EINVAL;

		spin_lock_irq(&ctx->wqh.lock);
		if (!timerfd_canceled(ctx)) {
			ctx->ticks = ticks;
			wake_up_locked_poll(&ctx->wqh, EPOLLIN);
		} else
			ret = -ECANCELED;
		spin_unlock_irq(&ctx->wqh.lock);
		break;
	}
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}
#else
#define timerfd_ioctl NULL
#endif

static const struct file_operations timerfd_fops = {
	.release	= timerfd_release,
	.poll		= timerfd_poll,
	.read_iter	= timerfd_read_iter,
	.llseek		= noop_llseek,
	.show_fdinfo	= timerfd_show,
	.unlocked_ioctl	= timerfd_ioctl,
};

static int timerfd_fget(int fd, struct fd *p)
{
	struct fd f = fdget(fd);
	if (!f.file)
		return -EBADF;
	if (f.file->f_op != &timerfd_fops) {
		fdput(f);
		return -EINVAL;
	}
	*p = f;
	return 0;
}

SYSCALL_DEFINE2(timerfd_create, int, clockid, int, flags)
{
	int ufd;
	struct timerfd_ctx *ctx;
	struct file *file;

	/* Check the TFD_* constants for consistency.  */
	BUILD_BUG_ON(TFD_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON(TFD_NONBLOCK != O_NONBLOCK);

	if ((flags & ~TFD_CREATE_FLAGS) ||
	    (clockid != CLOCK_MONOTONIC &&
	     clockid != CLOCK_REALTIME &&
	     clockid != CLOCK_REALTIME_ALARM &&
	     clockid != CLOCK_BOOTTIME &&
	     clockid != CLOCK_BOOTTIME_ALARM))
		return -EINVAL;

	if ((clockid == CLOCK_REALTIME_ALARM ||
	     clockid == CLOCK_BOOTTIME_ALARM) &&
	    !capable(CAP_WAKE_ALARM))
		return -EPERM;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	init_waitqueue_head(&ctx->wqh);
	spin_lock_init(&ctx->cancel_lock);
	ctx->clockid = clockid;

	if (isalarm(ctx))
		alarm_init(&ctx->t.alarm,
			   ctx->clockid == CLOCK_REALTIME_ALARM ?
			   ALARM_REALTIME : ALARM_BOOTTIME,
			   timerfd_alarmproc);
	else
		hrtimer_init(&ctx->t.tmr, clockid, HRTIMER_MODE_ABS);

	ctx->moffs = ktime_mono_to_real(0);

	ufd = get_unused_fd_flags(flags & TFD_SHARED_FCNTL_FLAGS);
	if (ufd < 0) {
		kfree(ctx);
		return ufd;
	}

	file = anon_inode_getfile("[timerfd]", &timerfd_fops, ctx,
				    O_RDWR | (flags & TFD_SHARED_FCNTL_FLAGS));
	if (IS_ERR(file)) {
		put_unused_fd(ufd);
		kfree(ctx);
		return PTR_ERR(file);
	}

	file->f_mode |= FMODE_NOWAIT;
	fd_install(ufd, file);
	return ufd;
}

static int do_timerfd_settime(int ufd, int flags, 
		const struct itimerspec64 *new,
		struct itimerspec64 *old)
{
	struct fd f;
	struct timerfd_ctx *ctx;
	int ret;

	if ((flags & ~TFD_SETTIME_FLAGS) ||
		 !itimerspec64_valid(new))
		return -EINVAL;

	ret = timerfd_fget(ufd, &f);
	if (ret)
		return ret;
	ctx = f.file->private_data;

	if (isalarm(ctx) && !capable(CAP_WAKE_ALARM)) {
		fdput(f);
		return -EPERM;
	}

	timerfd_setup_cancel(ctx, flags);

	/*
	 * We need to stop the existing timer before reprogramming
	 * it to the new values.
	 */
	for (;;) {
		spin_lock_irq(&ctx->wqh.lock);

		if (isalarm(ctx)) {
			if (alarm_try_to_cancel(&ctx->t.alarm) >= 0)
				break;
		} else {
			if (hrtimer_try_to_cancel(&ctx->t.tmr) >= 0)
				break;
		}
		spin_unlock_irq(&ctx->wqh.lock);

		if (isalarm(ctx))
			hrtimer_cancel_wait_running(&ctx->t.alarm.timer);
		else
			hrtimer_cancel_wait_running(&ctx->t.tmr);
	}

	/*
	 * If the timer is expired and it's periodic, we need to advance it
	 * because the caller may want to know the previous expiration time.
	 * We do not update "ticks" and "expired" since the timer will be
	 * re-programmed again in the following timerfd_setup() call.
	 */
	if (ctx->expired && ctx->tintv) {
		if (isalarm(ctx))
			alarm_forward_now(&ctx->t.alarm, ctx->tintv);
		else
			hrtimer_forward_now(&ctx->t.tmr, ctx->tintv);
	}

	old->it_value = ktime_to_timespec64(timerfd_get_remaining(ctx));
	old->it_interval = ktime_to_timespec64(ctx->tintv);

	/*
	 * Re-program the timer to the new value ...
	 */
	ret = timerfd_setup(ctx, flags, new);

	spin_unlock_irq(&ctx->wqh.lock);
	fdput(f);
	return ret;
}

static int do_timerfd_gettime(int ufd, struct itimerspec64 *t)
{
	struct fd f;
	struct timerfd_ctx *ctx;
	int ret = timerfd_fget(ufd, &f);
	if (ret)
		return ret;
	ctx = f.file->private_data;

	spin_lock_irq(&ctx->wqh.lock);
	if (ctx->expired && ctx->tintv) {
		ctx->expired = 0;

		if (isalarm(ctx)) {
			ctx->ticks +=
				alarm_forward_now(
					&ctx->t.alarm, ctx->tintv) - 1;
			alarm_restart(&ctx->t.alarm);
		} else {
			ctx->ticks +=
				hrtimer_forward_now(&ctx->t.tmr, ctx->tintv)
				- 1;
			hrtimer_restart(&ctx->t.tmr);
		}
	}
	t->it_value = ktime_to_timespec64(timerfd_get_remaining(ctx));
	t->it_interval = ktime_to_timespec64(ctx->tintv);
	spin_unlock_irq(&ctx->wqh.lock);
	fdput(f);
	return 0;
}

SYSCALL_DEFINE4(timerfd_settime, int, ufd, int, flags,
		const struct __kernel_itimerspec __user *, utmr,
		struct __kernel_itimerspec __user *, otmr)
{
	struct itimerspec64 new, old;
	int ret;

	if (get_itimerspec64(&new, utmr))
		return -EFAULT;
	ret = do_timerfd_settime(ufd, flags, &new, &old);
	if (ret)
		return ret;
	if (otmr && put_itimerspec64(&old, otmr))
		return -EFAULT;

	return ret;
}

SYSCALL_DEFINE2(timerfd_gettime, int, ufd, struct __kernel_itimerspec __user *, otmr)
{
	struct itimerspec64 kotmr;
	int ret = do_timerfd_gettime(ufd, &kotmr);
	if (ret)
		return ret;
	return put_itimerspec64(&kotmr, otmr) ? -EFAULT : 0;
}

#ifdef CONFIG_COMPAT_32BIT_TIME
SYSCALL_DEFINE4(timerfd_settime32, int, ufd, int, flags,
		const struct old_itimerspec32 __user *, utmr,
		struct old_itimerspec32 __user *, otmr)
{
	struct itimerspec64 new, old;
	int ret;

	if (get_old_itimerspec32(&new, utmr))
		return -EFAULT;
	ret = do_timerfd_settime(ufd, flags, &new, &old);
	if (ret)
		return ret;
	if (otmr && put_old_itimerspec32(&old, otmr))
		return -EFAULT;
	return ret;
}

SYSCALL_DEFINE2(timerfd_gettime32, int, ufd,
		struct old_itimerspec32 __user *, otmr)
{
	struct itimerspec64 kotmr;
	int ret = do_timerfd_gettime(ufd, &kotmr);
	if (ret)
		return ret;
	return put_old_itimerspec32(&kotmr, otmr) ? -EFAULT : 0;
}
#endif
