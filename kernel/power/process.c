/*
 * drivers/power/process.c - Functions for starting/stopping processes on 
 *                           suspend transitions.
 *
 * Originally from swsusp.
 */


#undef DEBUG

#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/module.h>

/* 
 * Timeout for stopping processes
 */
#define TIMEOUT	(6 * HZ)


static inline int freezeable(struct task_struct * p)
{
	if ((p == current) || 
	    (p->flags & PF_NOFREEZE) ||
	    (p->exit_state == EXIT_ZOMBIE) ||
	    (p->exit_state == EXIT_DEAD) ||
	    (p->state == TASK_STOPPED) ||
	    (p->state == TASK_TRACED))
		return 0;
	return 1;
}

/* Refrigerator is place where frozen processes are stored :-). */
void refrigerator(unsigned long flag)
{
	/* Hmm, should we be allowed to suspend when there are realtime
	   processes around? */
	long save;
	save = current->state;
	current->state = TASK_UNINTERRUPTIBLE;
	pr_debug("%s entered refrigerator\n", current->comm);
	printk("=");
	current->flags &= ~PF_FREEZE;

	spin_lock_irq(&current->sighand->siglock);
	recalc_sigpending(); /* We sent fake signal, clean it up */
	spin_unlock_irq(&current->sighand->siglock);

	current->flags |= PF_FROZEN;
	while (current->flags & PF_FROZEN)
		schedule();
	pr_debug("%s left refrigerator\n", current->comm);
	current->state = save;
}

/* 0 = success, else # of processes that we failed to stop */
int freeze_processes(void)
{
       int todo;
       unsigned long start_time;
	struct task_struct *g, *p;
	
	printk( "Stopping tasks: " );
	start_time = jiffies;
	do {
		todo = 0;
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			unsigned long flags;
			if (!freezeable(p))
				continue;
			if ((p->flags & PF_FROZEN) ||
			    (p->state == TASK_TRACED) ||
			    (p->state == TASK_STOPPED))
				continue;

			/* FIXME: smp problem here: we may not access other process' flags
			   without locking */
			p->flags |= PF_FREEZE;
			spin_lock_irqsave(&p->sighand->siglock, flags);
			signal_wake_up(p, 0);
			spin_unlock_irqrestore(&p->sighand->siglock, flags);
			todo++;
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
		yield();			/* Yield is okay here */
		if (time_after(jiffies, start_time + TIMEOUT)) {
			printk( "\n" );
			printk(KERN_ERR " stopping tasks failed (%d tasks remaining)\n", todo );
			return todo;
		}
	} while(todo);
	
	printk( "|\n" );
	BUG_ON(in_atomic());
	return 0;
}

void thaw_processes(void)
{
	struct task_struct *g, *p;

	printk( "Restarting tasks..." );
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if (!freezeable(p))
			continue;
		if (p->flags & PF_FROZEN) {
			p->flags &= ~PF_FROZEN;
			wake_up_process(p);
		} else
			printk(KERN_INFO " Strange, %s not stopped\n", p->comm );
	} while_each_thread(g, p);

	read_unlock(&tasklist_lock);
	schedule();
	printk( " done\n" );
}

EXPORT_SYMBOL(refrigerator);
