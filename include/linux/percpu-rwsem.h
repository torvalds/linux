#ifndef _LINUX_PERCPU_RWSEM_H
#define _LINUX_PERCPU_RWSEM_H

#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/percpu.h>
#include <linux/wait.h>

struct percpu_rw_semaphore {
	unsigned int __percpu	*fast_read_ctr;
	struct mutex		writer_mutex;
	struct rw_semaphore	rw_sem;
	atomic_t		slow_read_ctr;
	wait_queue_head_t	write_waitq;
};

extern void percpu_down_read(struct percpu_rw_semaphore *);
extern void percpu_up_read(struct percpu_rw_semaphore *);

extern void percpu_down_write(struct percpu_rw_semaphore *);
extern void percpu_up_write(struct percpu_rw_semaphore *);

extern int percpu_init_rwsem(struct percpu_rw_semaphore *);
extern void percpu_free_rwsem(struct percpu_rw_semaphore *);

#endif
