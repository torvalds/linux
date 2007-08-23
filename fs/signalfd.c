/*
 *  fs/signalfd.c
 *
 *  Copyright (C) 2003  Linus Torvalds
 *
 *  Mon Mar 5, 2007: Davide Libenzi <davidel@xmailserver.org>
 *      Changed ->read() to return a siginfo strcture instead of signal number.
 *      Fixed locking in ->poll().
 *      Added sighand-detach notification.
 *      Added fd re-use in sys_signalfd() syscall.
 *      Now using anonymous inode source.
 *      Thanks to Oleg Nesterov for useful code review and suggestions.
 *      More comments and suggestions from Arnd Bergmann.
 * Sat May 19, 2007: Davi E. M. Arnaut <davi@haxent.com.br>
 *      Retrieve multiple signals with one read() call
 */

#include <linux/file.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/list.h>
#include <linux/anon_inodes.h>
#include <linux/signalfd.h>

struct signalfd_ctx {
	struct list_head lnk;
	wait_queue_head_t wqh;
	sigset_t sigmask;
	struct task_struct *tsk;
};

struct signalfd_lockctx {
	struct task_struct *tsk;
	unsigned long flags;
};

/*
 * Tries to acquire the sighand lock. We do not increment the sighand
 * use count, and we do not even pin the task struct, so we need to
 * do it inside an RCU read lock, and we must be prepared for the
 * ctx->tsk going to NULL (in signalfd_deliver()), and for the sighand
 * being detached. We return 0 if the sighand has been detached, or
 * 1 if we were able to pin the sighand lock.
 */
static int signalfd_lock(struct signalfd_ctx *ctx, struct signalfd_lockctx *lk)
{
	struct sighand_struct *sighand = NULL;

	rcu_read_lock();
	lk->tsk = rcu_dereference(ctx->tsk);
	if (likely(lk->tsk != NULL))
		sighand = lock_task_sighand(lk->tsk, &lk->flags);
	rcu_read_unlock();

	if (!sighand)
		return 0;

	if (!ctx->tsk) {
		unlock_task_sighand(lk->tsk, &lk->flags);
		return 0;
	}

	if (lk->tsk->tgid == current->tgid)
		lk->tsk = current;

	return 1;
}

static void signalfd_unlock(struct signalfd_lockctx *lk)
{
	unlock_task_sighand(lk->tsk, &lk->flags);
}

/*
 * This must be called with the sighand lock held.
 */
void signalfd_deliver(struct task_struct *tsk, int sig)
{
	struct sighand_struct *sighand = tsk->sighand;
	struct signalfd_ctx *ctx, *tmp;

	BUG_ON(!sig);
	list_for_each_entry_safe(ctx, tmp, &sighand->signalfd_list, lnk) {
		/*
		 * We use a negative signal value as a way to broadcast that the
		 * sighand has been orphaned, so that we can notify all the
		 * listeners about this. Remember the ctx->sigmask is inverted,
		 * so if the user is interested in a signal, that corresponding
		 * bit will be zero.
		 */
		if (sig < 0) {
			if (ctx->tsk == tsk) {
				ctx->tsk = NULL;
				list_del_init(&ctx->lnk);
				wake_up(&ctx->wqh);
			}
		} else {
			if (!sigismember(&ctx->sigmask, sig))
				wake_up(&ctx->wqh);
		}
	}
}

static void signalfd_cleanup(struct signalfd_ctx *ctx)
{
	struct signalfd_lockctx lk;

	/*
	 * This is tricky. If the sighand is gone, we do not need to remove
	 * context from the list, the list itself won't be there anymore.
	 */
	if (signalfd_lock(ctx, &lk)) {
		list_del(&ctx->lnk);
		signalfd_unlock(&lk);
	}
	kfree(ctx);
}

static int signalfd_release(struct inode *inode, struct file *file)
{
	signalfd_cleanup(file->private_data);
	return 0;
}

static unsigned int signalfd_poll(struct file *file, poll_table *wait)
{
	struct signalfd_ctx *ctx = file->private_data;
	unsigned int events = 0;
	struct signalfd_lockctx lk;

	poll_wait(file, &ctx->wqh, wait);

	/*
	 * Let the caller get a POLLIN in this case, ala socket recv() when
	 * the peer disconnects.
	 */
	if (signalfd_lock(ctx, &lk)) {
		if ((lk.tsk == current &&
		     next_signal(&lk.tsk->pending, &ctx->sigmask) > 0) ||
		    next_signal(&lk.tsk->signal->shared_pending,
				&ctx->sigmask) > 0)
			events |= POLLIN;
		signalfd_unlock(&lk);
	} else
		events |= POLLIN;

	return events;
}

/*
 * Copied from copy_siginfo_to_user() in kernel/signal.c
 */
static int signalfd_copyinfo(struct signalfd_siginfo __user *uinfo,
			     siginfo_t const *kinfo)
{
	long err;

	BUILD_BUG_ON(sizeof(struct signalfd_siginfo) != 128);

	/*
	 * Unused memebers should be zero ...
	 */
	err = __clear_user(uinfo, sizeof(*uinfo));

	/*
	 * If you change siginfo_t structure, please be sure
	 * this code is fixed accordingly.
	 */
	err |= __put_user(kinfo->si_signo, &uinfo->signo);
	err |= __put_user(kinfo->si_errno, &uinfo->err);
	err |= __put_user((short)kinfo->si_code, &uinfo->code);
	switch (kinfo->si_code & __SI_MASK) {
	case __SI_KILL:
		err |= __put_user(kinfo->si_pid, &uinfo->pid);
		err |= __put_user(kinfo->si_uid, &uinfo->uid);
		break;
	case __SI_TIMER:
		 err |= __put_user(kinfo->si_tid, &uinfo->tid);
		 err |= __put_user(kinfo->si_overrun, &uinfo->overrun);
		 err |= __put_user((long)kinfo->si_ptr, &uinfo->svptr);
		break;
	case __SI_POLL:
		err |= __put_user(kinfo->si_band, &uinfo->band);
		err |= __put_user(kinfo->si_fd, &uinfo->fd);
		break;
	case __SI_FAULT:
		err |= __put_user((long)kinfo->si_addr, &uinfo->addr);
#ifdef __ARCH_SI_TRAPNO
		err |= __put_user(kinfo->si_trapno, &uinfo->trapno);
#endif
		break;
	case __SI_CHLD:
		err |= __put_user(kinfo->si_pid, &uinfo->pid);
		err |= __put_user(kinfo->si_uid, &uinfo->uid);
		err |= __put_user(kinfo->si_status, &uinfo->status);
		err |= __put_user(kinfo->si_utime, &uinfo->utime);
		err |= __put_user(kinfo->si_stime, &uinfo->stime);
		break;
	case __SI_RT: /* This is not generated by the kernel as of now. */
	case __SI_MESGQ: /* But this is */
		err |= __put_user(kinfo->si_pid, &uinfo->pid);
		err |= __put_user(kinfo->si_uid, &uinfo->uid);
		err |= __put_user((long)kinfo->si_ptr, &uinfo->svptr);
		break;
	default: /* this is just in case for now ... */
		err |= __put_user(kinfo->si_pid, &uinfo->pid);
		err |= __put_user(kinfo->si_uid, &uinfo->uid);
		break;
	}

	return err ? -EFAULT: sizeof(*uinfo);
}

static ssize_t signalfd_dequeue(struct signalfd_ctx *ctx, siginfo_t *info,
				int nonblock)
{
	ssize_t ret;
	struct signalfd_lockctx lk;
	DECLARE_WAITQUEUE(wait, current);

	if (!signalfd_lock(ctx, &lk))
		return 0;

	ret = dequeue_signal(lk.tsk, &ctx->sigmask, info);
	switch (ret) {
	case 0:
		if (!nonblock)
			break;
		ret = -EAGAIN;
	default:
		signalfd_unlock(&lk);
		return ret;
	}

	add_wait_queue(&ctx->wqh, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		ret = dequeue_signal(lk.tsk, &ctx->sigmask, info);
		signalfd_unlock(&lk);
		if (ret != 0)
			break;
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
		ret = signalfd_lock(ctx, &lk);
		if (unlikely(!ret)) {
			/*
			 * Let the caller read zero byte, ala socket
			 * recv() when the peer disconnect. This test
			 * must be done before doing a dequeue_signal(),
			 * because if the sighand has been orphaned,
			 * the dequeue_signal() call is going to crash
			 * because ->sighand will be long gone.
			 */
			 break;
		}
	}

	remove_wait_queue(&ctx->wqh, &wait);
	__set_current_state(TASK_RUNNING);

	return ret;
}

/*
 * Returns either the size of a "struct signalfd_siginfo", or zero if the
 * sighand we are attached to, has been orphaned. The "count" parameter
 * must be at least the size of a "struct signalfd_siginfo".
 */
static ssize_t signalfd_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	struct signalfd_ctx *ctx = file->private_data;
	struct signalfd_siginfo __user *siginfo;
	int nonblock = file->f_flags & O_NONBLOCK;
	ssize_t ret, total = 0;
	siginfo_t info;

	count /= sizeof(struct signalfd_siginfo);
	if (!count)
		return -EINVAL;

	siginfo = (struct signalfd_siginfo __user *) buf;

	do {
		ret = signalfd_dequeue(ctx, &info, nonblock);
		if (unlikely(ret <= 0))
			break;
		ret = signalfd_copyinfo(siginfo, &info);
		if (ret < 0)
			break;
		siginfo++;
		total += ret;
		nonblock = 1;
	} while (--count);

	return total ? total : ret;
}

static const struct file_operations signalfd_fops = {
	.release	= signalfd_release,
	.poll		= signalfd_poll,
	.read		= signalfd_read,
};

/*
 * Create a file descriptor that is associated with our signal
 * state. We can pass it around to others if we want to, but
 * it will always be _our_ signal state.
 */
asmlinkage long sys_signalfd(int ufd, sigset_t __user *user_mask, size_t sizemask)
{
	int error;
	sigset_t sigmask;
	struct signalfd_ctx *ctx;
	struct sighand_struct *sighand;
	struct file *file;
	struct inode *inode;
	struct signalfd_lockctx lk;

	if (sizemask != sizeof(sigset_t) ||
	    copy_from_user(&sigmask, user_mask, sizeof(sigmask)))
		return -EINVAL;
	sigdelsetmask(&sigmask, sigmask(SIGKILL) | sigmask(SIGSTOP));
	signotset(&sigmask);

	if (ufd == -1) {
		ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
		if (!ctx)
			return -ENOMEM;

		init_waitqueue_head(&ctx->wqh);
		ctx->sigmask = sigmask;
		ctx->tsk = current->group_leader;

		sighand = current->sighand;
		/*
		 * Add this fd to the list of signal listeners.
		 */
		spin_lock_irq(&sighand->siglock);
		list_add_tail(&ctx->lnk, &sighand->signalfd_list);
		spin_unlock_irq(&sighand->siglock);

		/*
		 * When we call this, the initialization must be complete, since
		 * anon_inode_getfd() will install the fd.
		 */
		error = anon_inode_getfd(&ufd, &inode, &file, "[signalfd]",
					 &signalfd_fops, ctx);
		if (error)
			goto err_fdalloc;
	} else {
		file = fget(ufd);
		if (!file)
			return -EBADF;
		ctx = file->private_data;
		if (file->f_op != &signalfd_fops) {
			fput(file);
			return -EINVAL;
		}
		/*
		 * We need to be prepared of the fact that the sighand this fd
		 * is attached to, has been detched. In that case signalfd_lock()
		 * will return 0, and we'll just skip setting the new mask.
		 */
		if (signalfd_lock(ctx, &lk)) {
			ctx->sigmask = sigmask;
			signalfd_unlock(&lk);
		}
		wake_up(&ctx->wqh);
		fput(file);
	}

	return ufd;

err_fdalloc:
	signalfd_cleanup(ctx);
	return error;
}

