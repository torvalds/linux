/*
 *  fs/timerfd.c
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 *
 *  Thanks to Thomas Gleixner for code reviews and useful comments.
 *
 */

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

struct timerfd_ctx {
	struct hrtimer tmr;
	ktime_t tintv;
	wait_queue_head_t wqh;
	u64 ticks;
	int expired;
	int clockid;
};

/*
 * This gets called when the timer event triggers. We set the "expired"
 * flag, but we do not re-arm the timer (in case it's necessary,
 * tintv.tv64 != 0) until the timer is accessed.
 */
static enum hrtimer_restart timerfd_tmrproc(struct hrtimer *htmr)
{
	struct timerfd_ctx *ctx = container_of(htmr, struct timerfd_ctx, tmr);
	unsigned long flags;

	spin_lock_irqsave(&ctx->wqh.lock, flags);
	ctx->expired = 1;
	ctx->ticks++;
	wake_up_locked(&ctx->wqh);
	spin_unlock_irqrestore(&ctx->wqh.lock, flags);

	return HRTIMER_NORESTART;
}

static ktime_t timerfd_get_remaining(struct timerfd_ctx *ctx)
{
	ktime_t remaining;

	remaining = hrtimer_expires_remaining(&ctx->tmr);
	return remaining.tv64 < 0 ? ktime_set(0, 0): remaining;
}

static void timerfd_setup(struct timerfd_ctx *ctx, int flags,
			  const struct itimerspec *ktmr)
{
	enum hrtimer_mode htmode;
	ktime_t texp;

	htmode = (flags & TFD_TIMER_ABSTIME) ?
		HRTIMER_MODE_ABS: HRTIMER_MODE_REL;

	texp = timespec_to_ktime(ktmr->it_value);
	ctx->expired = 0;
	ctx->ticks = 0;
	ctx->tintv = timespec_to_ktime(ktmr->it_interval);
	hrtimer_init(&ctx->tmr, ctx->clockid, htmode);
	hrtimer_set_expires(&ctx->tmr, texp);
	ctx->tmr.function = timerfd_tmrproc;
	if (texp.tv64 != 0)
		hrtimer_start(&ctx->tmr, texp, htmode);
}

static int timerfd_release(struct inode *inode, struct file *file)
{
	struct timerfd_ctx *ctx = file->private_data;

	hrtimer_cancel(&ctx->tmr);
	kfree(ctx);
	return 0;
}

static unsigned int timerfd_poll(struct file *file, poll_table *wait)
{
	struct timerfd_ctx *ctx = file->private_data;
	unsigned int events = 0;
	unsigned long flags;

	poll_wait(file, &ctx->wqh, wait);

	spin_lock_irqsave(&ctx->wqh.lock, flags);
	if (ctx->ticks)
		events |= POLLIN;
	spin_unlock_irqrestore(&ctx->wqh.lock, flags);

	return events;
}

static ssize_t timerfd_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	struct timerfd_ctx *ctx = file->private_data;
	ssize_t res;
	u64 ticks = 0;

	if (count < sizeof(ticks))
		return -EINVAL;
	spin_lock_irq(&ctx->wqh.lock);
	if (file->f_flags & O_NONBLOCK)
		res = -EAGAIN;
	else
		res = wait_event_interruptible_locked_irq(ctx->wqh, ctx->ticks);
	if (ctx->ticks) {
		ticks = ctx->ticks;
		if (ctx->expired && ctx->tintv.tv64) {
			/*
			 * If tintv.tv64 != 0, this is a periodic timer that
			 * needs to be re-armed. We avoid doing it in the timer
			 * callback to avoid DoS attacks specifying a very
			 * short timer period.
			 */
			ticks += hrtimer_forward_now(&ctx->tmr,
						     ctx->tintv) - 1;
			hrtimer_restart(&ctx->tmr);
		}
		ctx->expired = 0;
		ctx->ticks = 0;
	}
	spin_unlock_irq(&ctx->wqh.lock);
	if (ticks)
		res = put_user(ticks, (u64 __user *) buf) ? -EFAULT: sizeof(ticks);
	return res;
}

static const struct file_operations timerfd_fops = {
	.release	= timerfd_release,
	.poll		= timerfd_poll,
	.read		= timerfd_read,
	.llseek		= noop_llseek,
};

static struct file *timerfd_fget(int fd)
{
	struct file *file;

	file = fget(fd);
	if (!file)
		return ERR_PTR(-EBADF);
	if (file->f_op != &timerfd_fops) {
		fput(file);
		return ERR_PTR(-EINVAL);
	}

	return file;
}

SYSCALL_DEFINE2(timerfd_create, int, clockid, int, flags)
{
	int ufd;
	struct timerfd_ctx *ctx;

	/* Check the TFD_* constants for consistency.  */
	BUILD_BUG_ON(TFD_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON(TFD_NONBLOCK != O_NONBLOCK);

	if ((flags & ~TFD_CREATE_FLAGS) ||
	    (clockid != CLOCK_MONOTONIC &&
	     clockid != CLOCK_REALTIME))
		return -EINVAL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	init_waitqueue_head(&ctx->wqh);
	ctx->clockid = clockid;
	hrtimer_init(&ctx->tmr, clockid, HRTIMER_MODE_ABS);

	ufd = anon_inode_getfd("[timerfd]", &timerfd_fops, ctx,
			       O_RDWR | (flags & TFD_SHARED_FCNTL_FLAGS));
	if (ufd < 0)
		kfree(ctx);

	return ufd;
}

SYSCALL_DEFINE4(timerfd_settime, int, ufd, int, flags,
		const struct itimerspec __user *, utmr,
		struct itimerspec __user *, otmr)
{
	struct file *file;
	struct timerfd_ctx *ctx;
	struct itimerspec ktmr, kotmr;

	if (copy_from_user(&ktmr, utmr, sizeof(ktmr)))
		return -EFAULT;

	if ((flags & ~TFD_SETTIME_FLAGS) ||
	    !timespec_valid(&ktmr.it_value) ||
	    !timespec_valid(&ktmr.it_interval))
		return -EINVAL;

	file = timerfd_fget(ufd);
	if (IS_ERR(file))
		return PTR_ERR(file);
	ctx = file->private_data;

	/*
	 * We need to stop the existing timer before reprogramming
	 * it to the new values.
	 */
	for (;;) {
		spin_lock_irq(&ctx->wqh.lock);
		if (hrtimer_try_to_cancel(&ctx->tmr) >= 0)
			break;
		spin_unlock_irq(&ctx->wqh.lock);
		cpu_relax();
	}

	/*
	 * If the timer is expired and it's periodic, we need to advance it
	 * because the caller may want to know the previous expiration time.
	 * We do not update "ticks" and "expired" since the timer will be
	 * re-programmed again in the following timerfd_setup() call.
	 */
	if (ctx->expired && ctx->tintv.tv64)
		hrtimer_forward_now(&ctx->tmr, ctx->tintv);

	kotmr.it_value = ktime_to_timespec(timerfd_get_remaining(ctx));
	kotmr.it_interval = ktime_to_timespec(ctx->tintv);

	/*
	 * Re-program the timer to the new value ...
	 */
	timerfd_setup(ctx, flags, &ktmr);

	spin_unlock_irq(&ctx->wqh.lock);
	fput(file);
	if (otmr && copy_to_user(otmr, &kotmr, sizeof(kotmr)))
		return -EFAULT;

	return 0;
}

SYSCALL_DEFINE2(timerfd_gettime, int, ufd, struct itimerspec __user *, otmr)
{
	struct file *file;
	struct timerfd_ctx *ctx;
	struct itimerspec kotmr;

	file = timerfd_fget(ufd);
	if (IS_ERR(file))
		return PTR_ERR(file);
	ctx = file->private_data;

	spin_lock_irq(&ctx->wqh.lock);
	if (ctx->expired && ctx->tintv.tv64) {
		ctx->expired = 0;
		ctx->ticks +=
			hrtimer_forward_now(&ctx->tmr, ctx->tintv) - 1;
		hrtimer_restart(&ctx->tmr);
	}
	kotmr.it_value = ktime_to_timespec(timerfd_get_remaining(ctx));
	kotmr.it_interval = ktime_to_timespec(ctx->tintv);
	spin_unlock_irq(&ctx->wqh.lock);
	fput(file);

	return copy_to_user(otmr, &kotmr, sizeof(kotmr)) ? -EFAULT: 0;
}

