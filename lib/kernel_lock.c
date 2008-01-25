/*
 * lib/kernel_lock.c
 *
 * This is the traditional BKL - big kernel lock. Largely
 * relegated to obsolescence, but used by various less
 * important (or lazy) subsystems.
 */
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

/*
 * The 'big kernel semaphore'
 *
 * This mutex is taken and released recursively by lock_kernel()
 * and unlock_kernel().  It is transparently dropped and reacquired
 * over schedule().  It is used to protect legacy code that hasn't
 * been migrated to a proper locking design yet.
 *
 * Note: code locked by this semaphore will only be serialized against
 * other code using the same locking facility. The code guarantees that
 * the task remains on the same CPU.
 *
 * Don't use in new code.
 */
static DECLARE_MUTEX(kernel_sem);

/*
 * Re-acquire the kernel semaphore.
 *
 * This function is called with preemption off.
 *
 * We are executing in schedule() so the code must be extremely careful
 * about recursion, both due to the down() and due to the enabling of
 * preemption. schedule() will re-check the preemption flag after
 * reacquiring the semaphore.
 */
int __lockfunc __reacquire_kernel_lock(void)
{
	struct task_struct *task = current;
	int saved_lock_depth = task->lock_depth;

	BUG_ON(saved_lock_depth < 0);

	task->lock_depth = -1;
	preempt_enable_no_resched();

	down(&kernel_sem);

	preempt_disable();
	task->lock_depth = saved_lock_depth;

	return 0;
}

void __lockfunc __release_kernel_lock(void)
{
	up(&kernel_sem);
}

/*
 * Getting the big kernel semaphore.
 */
void __lockfunc lock_kernel(void)
{
	struct task_struct *task = current;
	int depth = task->lock_depth + 1;

	if (likely(!depth))
		/*
		 * No recursion worries - we set up lock_depth _after_
		 */
		down(&kernel_sem);

	task->lock_depth = depth;
}

void __lockfunc unlock_kernel(void)
{
	struct task_struct *task = current;

	BUG_ON(task->lock_depth < 0);

	if (likely(--task->lock_depth < 0))
		up(&kernel_sem);
}

EXPORT_SYMBOL(lock_kernel);
EXPORT_SYMBOL(unlock_kernel);

