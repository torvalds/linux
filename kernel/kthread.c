/* Kernel thread helper functions.
 *   Copyright (C) 2004 IBM Corporation, Rusty Russell.
 *
 * Creation is done via kthreadd, so that we get a clean environment
 * even if we're invoked from userspace (think modprobe, hotplug cpu,
 * etc.).
 */
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/unistd.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <asm/semaphore.h>

static DEFINE_SPINLOCK(kthread_create_lock);
static LIST_HEAD(kthread_create_list);
struct task_struct *kthreadd_task;

struct kthread_create_info
{
	/* Information passed to kthread() from kthreadd. */
	int (*threadfn)(void *data);
	void *data;
	struct completion started;

	/* Result passed back to kthread_create() from kthreadd. */
	struct task_struct *result;
	struct completion done;

	struct list_head list;
};

struct kthread_stop_info
{
	struct task_struct *k;
	int err;
	struct completion done;
};

/* Thread stopping is done by setthing this var: lock serializes
 * multiple kthread_stop calls. */
static DEFINE_MUTEX(kthread_stop_lock);
static struct kthread_stop_info kthread_stop_info;

/**
 * kthread_should_stop - should this kthread return now?
 *
 * When someone calls kthread_stop() on your kthread, it will be woken
 * and this will return true.  You should then return, and your return
 * value will be passed through to kthread_stop().
 */
int kthread_should_stop(void)
{
	return (kthread_stop_info.k == current);
}
EXPORT_SYMBOL(kthread_should_stop);

static int kthread(void *_create)
{
	struct kthread_create_info *create = _create;
	int (*threadfn)(void *data);
	void *data;
	int ret = -EINTR;

	/* Copy data: it's on kthread's stack */
	threadfn = create->threadfn;
	data = create->data;

	/* OK, tell user we're spawned, wait for stop or wakeup */
	__set_current_state(TASK_UNINTERRUPTIBLE);
	complete(&create->started);
	schedule();

	if (!kthread_should_stop())
		ret = threadfn(data);

	/* It might have exited on its own, w/o kthread_stop.  Check. */
	if (kthread_should_stop()) {
		kthread_stop_info.err = ret;
		complete(&kthread_stop_info.done);
	}
	return 0;
}

static void create_kthread(struct kthread_create_info *create)
{
	int pid;

	/* We want our own signal handler (we take no signals by default). */
	pid = kernel_thread(kthread, create, CLONE_FS | CLONE_FILES | SIGCHLD);
	if (pid < 0) {
		create->result = ERR_PTR(pid);
	} else {
		wait_for_completion(&create->started);
		read_lock(&tasklist_lock);
		create->result = find_task_by_pid(pid);
		read_unlock(&tasklist_lock);
	}
	complete(&create->done);
}

/**
 * kthread_create - create a kthread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @namefmt: printf-style name for the thread.
 *
 * Description: This helper function creates and names a kernel
 * thread.  The thread will be stopped: use wake_up_process() to start
 * it.  See also kthread_run(), kthread_create_on_cpu().
 *
 * When woken, the thread will run @threadfn() with @data as its
 * argument. @threadfn() can either call do_exit() directly if it is a
 * standalone thread for which noone will call kthread_stop(), or
 * return when 'kthread_should_stop()' is true (which means
 * kthread_stop() has been called).  The return value should be zero
 * or a negative error number; it will be passed to kthread_stop().
 *
 * Returns a task_struct or ERR_PTR(-ENOMEM).
 */
struct task_struct *kthread_create(int (*threadfn)(void *data),
				   void *data,
				   const char namefmt[],
				   ...)
{
	struct kthread_create_info create;

	create.threadfn = threadfn;
	create.data = data;
	init_completion(&create.started);
	init_completion(&create.done);

	spin_lock(&kthread_create_lock);
	list_add_tail(&create.list, &kthread_create_list);
	wake_up_process(kthreadd_task);
	spin_unlock(&kthread_create_lock);

	wait_for_completion(&create.done);

	if (!IS_ERR(create.result)) {
		va_list args;
		va_start(args, namefmt);
		vsnprintf(create.result->comm, sizeof(create.result->comm),
			  namefmt, args);
		va_end(args);
	}
	return create.result;
}
EXPORT_SYMBOL(kthread_create);

/**
 * kthread_bind - bind a just-created kthread to a cpu.
 * @k: thread created by kthread_create().
 * @cpu: cpu (might not be online, must be possible) for @k to run on.
 *
 * Description: This function is equivalent to set_cpus_allowed(),
 * except that @cpu doesn't need to be online, and the thread must be
 * stopped (i.e., just returned from kthread_create()).
 */
void kthread_bind(struct task_struct *k, unsigned int cpu)
{
	if (k->state != TASK_UNINTERRUPTIBLE) {
		WARN_ON(1);
		return;
	}
	/* Must have done schedule() in kthread() before we set_task_cpu */
	wait_task_inactive(k);
	set_task_cpu(k, cpu);
	k->cpus_allowed = cpumask_of_cpu(cpu);
}
EXPORT_SYMBOL(kthread_bind);

/**
 * kthread_stop - stop a thread created by kthread_create().
 * @k: thread created by kthread_create().
 *
 * Sets kthread_should_stop() for @k to return true, wakes it, and
 * waits for it to exit.  Your threadfn() must not call do_exit()
 * itself if you use this function!  This can also be called after
 * kthread_create() instead of calling wake_up_process(): the thread
 * will exit without calling threadfn().
 *
 * Returns the result of threadfn(), or %-EINTR if wake_up_process()
 * was never called.
 */
int kthread_stop(struct task_struct *k)
{
	int ret;

	mutex_lock(&kthread_stop_lock);

	/* It could exit after stop_info.k set, but before wake_up_process. */
	get_task_struct(k);

	/* Must init completion *before* thread sees kthread_stop_info.k */
	init_completion(&kthread_stop_info.done);
	smp_wmb();

	/* Now set kthread_should_stop() to true, and wake it up. */
	kthread_stop_info.k = k;
	wake_up_process(k);
	put_task_struct(k);

	/* Once it dies, reset stop ptr, gather result and we're done. */
	wait_for_completion(&kthread_stop_info.done);
	kthread_stop_info.k = NULL;
	ret = kthread_stop_info.err;
	mutex_unlock(&kthread_stop_lock);

	return ret;
}
EXPORT_SYMBOL(kthread_stop);

int kthreadd(void *unused)
{
	struct task_struct *tsk = current;

	/* Setup a clean context for our children to inherit. */
	set_task_comm(tsk, "kthreadd");
	ignore_signals(tsk);
	set_user_nice(tsk, -5);
	set_cpus_allowed(tsk, CPU_MASK_ALL);

	current->flags |= PF_NOFREEZE;

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&kthread_create_list))
			schedule();
		__set_current_state(TASK_RUNNING);

		spin_lock(&kthread_create_lock);
		while (!list_empty(&kthread_create_list)) {
			struct kthread_create_info *create;

			create = list_entry(kthread_create_list.next,
					    struct kthread_create_info, list);
			list_del_init(&create->list);
			spin_unlock(&kthread_create_lock);

			create_kthread(create);

			spin_lock(&kthread_create_lock);
		}
		spin_unlock(&kthread_create_lock);
	}

	return 0;
}
