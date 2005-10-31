/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: background.c,v 1.54 2005/05/20 21:37:12 gleixner Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/jffs2.h>
#include <linux/mtd/mtd.h>
#include <linux/completion.h>
#include <linux/sched.h>
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
	pid_t pid;
	int ret = 0;

	if (c->gc_task)
		BUG();

	init_completion(&c->gc_thread_start);
	init_completion(&c->gc_thread_exit);

	pid = kernel_thread(jffs2_garbage_collect_thread, c, CLONE_FS|CLONE_FILES);
	if (pid < 0) {
		printk(KERN_WARNING "fork failed for JFFS2 garbage collect thread: %d\n", -pid);
		complete(&c->gc_thread_exit);
		ret = pid;
	} else {
		/* Wait for it... */
		D1(printk(KERN_DEBUG "JFFS2: Garbage collect thread is pid %d\n", pid));
		wait_for_completion(&c->gc_thread_start);
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

	daemonize("jffs2_gcd_mtd%d", c->mtd->index);
	allow_signal(SIGKILL);
	allow_signal(SIGSTOP);
	allow_signal(SIGCONT);

	c->gc_task = current;
	complete(&c->gc_thread_start);

	set_user_nice(current, 10);

	for (;;) {
		allow_signal(SIGHUP);

		if (!jffs2_thread_should_wake(c)) {
			set_current_state (TASK_INTERRUPTIBLE);
			D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread sleeping...\n"));
			/* Yes, there's a race here; we checked jffs2_thread_should_wake()
			   before setting current->state to TASK_INTERRUPTIBLE. But it doesn't
			   matter - We don't care if we miss a wakeup, because the GC thread
			   is only an optimisation anyway. */
			schedule();
		}

		if (try_to_freeze())
			continue;

		cond_resched();

		/* Put_super will send a SIGKILL and then wait on the sem. 
		 */
		while (signal_pending(current)) {
			siginfo_t info;
			unsigned long signr;

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
