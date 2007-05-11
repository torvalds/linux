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
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/anon_inodes.h>
#include <linux/timerfd.h>

struct timerfd_ctx {
	struct hrtimer tmr;
	ktime_t tintv;
	spinlock_t lock;
	wait_queue_head_t wqh;
	int expired;
};

/*
 * This gets called when the timer event triggers. We set the "expired"
 * flag, but we do not re-arm the timer (in case it's necessary,
 * tintv.tv64 != 0) until the timer is read.
 */
static enum hrtimer_restart timerfd_tmrproc(struct hrtimer *htmr)
{
	struct timerfd_ctx *ctx = container_of(htmr, struct timerfd_ctx, tmr);
	unsigned long flags;

	spin_lock_irqsave(&ctx->lock, flags);
	ctx->expired = 1;
	wake_up_locked(&ctx->wqh);
	spin_unlock_irqrestore(&ctx->lock, flags);

	return HRTIMER_NORESTART;
}

static void timerfd_setup(struct timerfd_ctx *ctx, int clockid, int flags,
			  const struct itimerspec *ktmr)
{
	enum hrtimer_mode htmode;
	ktime_t texp;

	htmode = (flags & TFD_TIMER_ABSTIME) ?
		HRTIMER_MODE_ABS: HRTIMER_MODE_REL;

	texp = timespec_to_ktime(ktmr->it_value);
	ctx->expired = 0;
	ctx->tintv = timespec_to_ktime(ktmr->it_interval);
	hrtimer_init(&ctx->tmr, clockid, htmode);
	ctx->tmr.expires = texp;
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

	spin_lock_irqsave(&ctx->lock, flags);
	if (ctx->expired)
		events |= POLLIN;
	spin_unlock_irqrestore(&ctx->lock, flags);

	return events;
}

static ssize_t timerfd_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	struct timerfd_ctx *ctx = file->private_data;
	ssize_t res;
	u32 ticks = 0;
	DECLARE_WAITQUEUE(wait, current);

	if (count < sizeof(ticks))
		return -EINVAL;
	spin_lock_irq(&ctx->lock);
	res = -EAGAIN;
	if (!ctx->expired && !(file->f_flags & O_NONBLOCK)) {
		__add_wait_queue(&ctx->wqh, &wait);
		for (res = 0;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (ctx->expired) {
				res = 0;
				break;
			}
			if (signal_pending(current)) {
				res = -ERESTARTSYS;
				break;
			}
			spin_unlock_irq(&ctx->lock);
			schedule();
			spin_lock_irq(&ctx->lock);
		}
		__remove_wait_queue(&ctx->wqh, &wait);
		__set_current_state(TASK_RUNNING);
	}
	if (ctx->expired) {
		ctx->expired = 0;
		if (ctx->tintv.tv64 != 0) {
			/*
			 * If tintv.tv64 != 0, this is a periodic timer that
			 * needs to be re-armed. We avoid doing it in the timer
			 * callback to avoid DoS attacks specifying a very
			 * short timer period.
			 */
			ticks = (u32)
				hrtimer_forward(&ctx->tmr,
						hrtimer_cb_get_time(&ctx->tmr),
						ctx->tintv);
			hrtimer_restart(&ctx->tmr);
		} else
			ticks = 1;
	}
	spin_unlock_irq(&ctx->lock);
	if (ticks)
		res = put_user(ticks, buf) ? -EFAULT: sizeof(ticks);
	return res;
}

static const struct file_operations timerfd_fops = {
	.release	= timerfd_release,
	.poll		= timerfd_poll,
	.read		= timerfd_read,
};

asmlinkage long sys_timerfd(int ufd, int clockid, int flags,
			    const struct itimerspec __user *utmr)
{
	int error;
	struct timerfd_ctx *ctx;
	struct file *file;
	struct inode *inode;
	struct itimerspec ktmr;

	if (copy_from_user(&ktmr, utmr, sizeof(ktmr)))
		return -EFAULT;

	if (clockid != CLOCK_MONOTONIC &&
	    clockid != CLOCK_REALTIME)
		return -EINVAL;
	if (!timespec_valid(&ktmr.it_value) ||
	    !timespec_valid(&ktmr.it_interval))
		return -EINVAL;

	if (ufd == -1) {
		ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
		if (!ctx)
			return -ENOMEM;

		init_waitqueue_head(&ctx->wqh);
		spin_lock_init(&ctx->lock);

		timerfd_setup(ctx, clockid, flags, &ktmr);

		/*
		 * When we call this, the initialization must be complete, since
		 * anon_inode_getfd() will install the fd.
		 */
		error = anon_inode_getfd(&ufd, &inode, &file, "[timerfd]",
					 &timerfd_fops, ctx);
		if (error)
			goto err_tmrcancel;
	} else {
		file = fget(ufd);
		if (!file)
			return -EBADF;
		ctx = file->private_data;
		if (file->f_op != &timerfd_fops) {
			fput(file);
			return -EINVAL;
		}
		/*
		 * We need to stop the existing timer before reprogramming
		 * it to the new values.
		 */
		for (;;) {
			spin_lock_irq(&ctx->lock);
			if (hrtimer_try_to_cancel(&ctx->tmr) >= 0)
				break;
			spin_unlock_irq(&ctx->lock);
			cpu_relax();
		}
		/*
		 * Re-program the timer to the new value ...
		 */
		timerfd_setup(ctx, clockid, flags, &ktmr);

		spin_unlock_irq(&ctx->lock);
		fput(file);
	}

	return ufd;

err_tmrcancel:
	hrtimer_cancel(&ctx->tmr);
	kfree(ctx);
	return error;
}

