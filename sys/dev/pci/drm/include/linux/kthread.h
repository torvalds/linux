/* Public domain. */

#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H

/* both for printf */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/task.h>

struct proc *kthread_run(int (*)(void *), void *, const char *);
void 	kthread_park(struct proc *);
void 	kthread_unpark(struct proc *);
int	kthread_should_park(void);
void 	kthread_parkme(void);
void	kthread_stop(struct proc *);
int	kthread_should_stop(void);

struct kthread_work {
	struct task	 task;
	struct taskq	*tq;
};

struct kthread_worker {
	struct taskq	*tq;
};

struct kthread_worker *
	kthread_create_worker(unsigned int, const char *, ...);
void	kthread_destroy_worker(struct kthread_worker *);
void	kthread_init_work(struct kthread_work *, void (*)(struct kthread_work *));
bool	kthread_queue_work(struct kthread_worker *, struct kthread_work *);
bool	kthread_cancel_work_sync(struct kthread_work *);
void	kthread_flush_work(struct kthread_work *);
void	kthread_flush_worker(struct kthread_worker *);

#endif
