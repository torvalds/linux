#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
/* Simple interface for creating and stopping kernel threads without mess. */
#include <linux/err.h>
#include <linux/sched.h>

/**
 * kthread_create: create a kthread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @namefmt: printf-style name for the thread.
 *
 * Description: This helper function creates and names a kernel
 * thread.  The thread will be stopped: use wake_up_process() to start
 * it.  See also kthread_run(), kthread_create_on_cpu().
 *
 * When woken, the thread will run @threadfn() with @data as its
 * argument. @threadfn can either call do_exit() directly if it is a
 * standalone thread for which noone will call kthread_stop(), or
 * return when 'kthread_should_stop()' is true (which means
 * kthread_stop() has been called).  The return value should be zero
 * or a negative error number: it will be passed to kthread_stop().
 *
 * Returns a task_struct or ERR_PTR(-ENOMEM).
 */
struct task_struct *kthread_create(int (*threadfn)(void *data),
				   void *data,
				   const char namefmt[], ...);

/**
 * kthread_run: create and wake a thread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @namefmt: printf-style name for the thread.
 *
 * Description: Convenient wrapper for kthread_create() followed by
 * wake_up_process().  Returns the kthread, or ERR_PTR(-ENOMEM). */
#define kthread_run(threadfn, data, namefmt, ...)			   \
({									   \
	struct task_struct *__k						   \
		= kthread_create(threadfn, data, namefmt, ## __VA_ARGS__); \
	if (!IS_ERR(__k))						   \
		wake_up_process(__k);					   \
	__k;								   \
})

/**
 * kthread_bind: bind a just-created kthread to a cpu.
 * @k: thread created by kthread_create().
 * @cpu: cpu (might not be online, must be possible) for @k to run on.
 *
 * Description: This function is equivalent to set_cpus_allowed(),
 * except that @cpu doesn't need to be online, and the thread must be
 * stopped (ie. just returned from kthread_create().
 */
void kthread_bind(struct task_struct *k, unsigned int cpu);

/**
 * kthread_stop: stop a thread created by kthread_create().
 * @k: thread created by kthread_create().
 *
 * Sets kthread_should_stop() for @k to return true, wakes it, and
 * waits for it to exit.  Your threadfn() must not call do_exit()
 * itself if you use this function!  This can also be called after
 * kthread_create() instead of calling wake_up_process(): the thread
 * will exit without calling threadfn().
 *
 * Returns the result of threadfn(), or -EINTR if wake_up_process()
 * was never called. */
int kthread_stop(struct task_struct *k);

/**
 * kthread_stop_sem: stop a thread created by kthread_create().
 * @k: thread created by kthread_create().
 * @s: semaphore that @k waits on while idle.
 *
 * Does essentially the same thing as kthread_stop() above, but wakes
 * @k by calling up(@s).
 *
 * Returns the result of threadfn(), or -EINTR if wake_up_process()
 * was never called. */
int kthread_stop_sem(struct task_struct *k, struct semaphore *s);

/**
 * kthread_should_stop: should this kthread return now?
 *
 * When someone calls kthread_stop on your kthread, it will be woken
 * and this will return true.  You should then return, and your return
 * value will be passed through to kthread_stop().
 */
int kthread_should_stop(void);

#endif /* _LINUX_KTHREAD_H */
