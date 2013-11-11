#ifndef _LINUX_PERCPU_RWSEM_H
#define _LINUX_PERCPU_RWSEM_H

#include <linux/atomic.h>
#include <linux/rwsem.h>
#include <linux/percpu.h>
#include <linux/wait.h>
#include <linux/lockdep.h>

struct percpu_rw_semaphore {
	unsigned int __percpu	*fast_read_ctr;
	atomic_t		write_ctr;
	struct rw_semaphore	rw_sem;
	atomic_t		slow_read_ctr;
	wait_queue_head_t	write_waitq;
};

extern void percpu_down_read(struct percpu_rw_semaphore *);
extern void percpu_up_read(struct percpu_rw_semaphore *);

extern void percpu_down_write(struct percpu_rw_semaphore *);
extern void percpu_up_write(struct percpu_rw_semaphore *);

extern int __percpu_init_rwsem(struct percpu_rw_semaphore *,
				const char *, struct lock_class_key *);
extern void percpu_free_rwsem(struct percpu_rw_semaphore *);

#define percpu_init_rwsem(brw)	\
({								\
	static struct lock_class_key rwsem_key;			\
	__percpu_init_rwsem(brw, #brw, &rwsem_key);		\
})

#endif
