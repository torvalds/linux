#ifndef _LINUX_PERCPU_RWSEM_H
#define _LINUX_PERCPU_RWSEM_H

#include <linux/atomic.h>
#include <linux/rwsem.h>
#include <linux/percpu.h>
#include <linux/wait.h>
#include <linux/rcu_sync.h>
#include <linux/lockdep.h>

struct percpu_rw_semaphore {
	struct rcu_sync		rss;
	unsigned int __percpu	*fast_read_ctr;
	struct rw_semaphore	rw_sem;
	atomic_t		slow_read_ctr;
	wait_queue_head_t	write_waitq;
};

extern void percpu_down_read(struct percpu_rw_semaphore *);
extern int  percpu_down_read_trylock(struct percpu_rw_semaphore *);
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


#define percpu_rwsem_is_held(sem) lockdep_is_held(&(sem)->rw_sem)

static inline void percpu_rwsem_release(struct percpu_rw_semaphore *sem,
					bool read, unsigned long ip)
{
	lock_release(&sem->rw_sem.dep_map, 1, ip);
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	if (!read)
		sem->rw_sem.owner = NULL;
#endif
}

static inline void percpu_rwsem_acquire(struct percpu_rw_semaphore *sem,
					bool read, unsigned long ip)
{
	lock_acquire(&sem->rw_sem.dep_map, 0, 1, read, 1, NULL, ip);
}

#endif
