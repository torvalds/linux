/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#include <linux/kernel.h>
#include <linux/jffs2.h>
#include <linux/mtd/mtd.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include "nodelist.h"


static int jffs2_garbage_collect_thread(void *);

void jffs2_garbage_collect_trigger(struct jffs2_sb_info *c)
{
	spin_lock(&c->erase_completion_lock);
	if (c->gc_task && jffs2_thread_should_wake(c))
		send_sig(SIGHUP, c->gc_task, 1);
	spin_unlock(&c->erase_completion_lock);
}

/* This must only ever be called when no GC thread is currently running */
int jffs2_start_garbage_collect_thread(struct jffs2_sb_info *c)
{
	struct task_struct *tsk;
	int ret = 0;

	BUG_ON(c->gc_task);

	init_completion(&c->gc_thread_start);
	init_completion(&c->gc_thread_exit);

	tsk = kthread_run(jffs2_garbage_collect_thread, c, "jffs2_gcd_mtd%d", c->mtd->index);
	if (IS_ERR(tsk)) {
		printk(KERN_WARNING "fork failed for JFFS2 garbage collect thread: %ld\n", -PTR_ERR(tsk));
		complete(&c->gc_thread_exit);
		ret = PTR_ERR(tsk);
	} else {
		/* Wait for it... */
		D1(printk(KERN_DEBUG "JFFS2: Garbage collect thread is pid %d\n", tsk->pid));
		wait_for_completion(&c->gc_thread_start);
		ret = tsk->pid;
	}

	return ret;
}

void jffs2_stop_garbage_collect_thread(struct jffs2_sb_info *c)
{
	int wait = 0;
	spin_lock(&c->erase_completion_lock);
	if (c->gc_task) {
		D1(printk(KERN_DEBUG "jffs2: Killing GC task %d\n", c->gc_task->pid));
		send_sig(SIGKILL, c->gc_task, 1);
		wait = 1;
	}
	spin_unlock(&c->erase_completion_lock);
	if (wait)
		wait_for_completion(&c->gc_thread_exit);
}

static int jffs2_garbage_collect_thread(void *_c)
{
	struct jffs2_sb_info *c = _c;

	allow_signal(SIGKILL);
	allow_signal(SIGSTOP);
	allow_signal(SIGCONT);

	c->gc_task = current;
	complete(&c->gc_thread_start);

	set_user_nice(current, 10);

	set_freezable();
	for (;;) {
		allow_signal(SIGHUP);
	again:
		spin_lock(&c->erase_completion_lock);
		if (!jffs2_thread_should_wake(c)) {
			set_current_state (TASK_INTERRUPTIBLE);
			spin_unlock(&c->erase_completion_lock);
			D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread sleeping...\n"));
			schedule();
		} else
			spin_unlock(&c->erase_completion_lock);
			

		/* Problem - immediately after bootup, the GCD spends a lot
		 * of time in places like jffs2_kill_fragtree(); so much so
		 * that userspace processes (like gdm and X) are starved
		 * despite plenty of cond_resched()s and renicing.  Yield()
		 * doesn't help, either (presumably because userspace and GCD
		 * are generally competing for a higher latency resource -
		 * disk).
		 * This forces the GCD to slow the hell down.   Pulling an
		 * inode in with read_inode() is much preferable to having
		 * the GC thread get there first. */
		schedule_timeout_interruptible(msecs_to_jiffies(50));

		if (kthread_should_stop()) {
			D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread():  kthread_stop() called.\n"));
			goto die;
		}

		/* Put_super will send a SIGKILL and then wait on the sem.
		 */
		while (signal_pending(current) || freezing(current)) {
			siginfo_t info;
			unsigned long signr;

			if (try_to_freeze())
				goto again;

			signr = dequeue_signal_lock(current, &current->blocked, &info);

			switch(signr) {
			case SIGSTOP:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): SIGSTOP received.\n"));
				set_current_state(TASK_STOPPED);
				schedule();
				break;

			case SIGKILL:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): SIGKILL received.\n"));
				goto die;

			case SIGHUP:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): SIGHUP received.\n"));
				break;
			default:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): signal %ld received\n", signr));
			}
		}
		/* We don't want SIGHUP to interrupt us. STOP and KILL are OK though. */
		disallow_signal(SIGHUP);

		D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): pass\n"));
		if (jffs2_garbage_collect_pass(c) == -ENOSPC) {
			printk(KERN_NOTICE "No space for garbage collection. Aborting GC thread\n");
			goto die;
		}
	}
 die:
	spin_lock(&c->erase_completion_lock);
	c->gc_task = NULL;
	spin_unlock(&c->erase_completion_lock);
	complete_and_exit(&c->gc_thread_exit, 0);
}
