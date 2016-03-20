/* See include/linux/lglock.h for description */
#include <linux/module.h>
#include <linux/lglock.h>
#include <linux/cpu.h>
#include <linux/string.h>

/*
 * Note there is no uninit, so lglocks cannot be defined in
 * modules (but it's fine to use them from there)
 * Could be added though, just undo lg_lock_init
 */

void lg_lock_init(struct lglock *lg, char *name)
{
	LOCKDEP_INIT_MAP(&lg->lock_dep_map, name, &lg->lock_key, 0);
}
EXPORT_SYMBOL(lg_lock_init);

void lg_local_lock(struct lglock *lg)
{
	arch_spinlock_t *lock;

	preempt_disable();
	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	lock = this_cpu_ptr(lg->lock);
	arch_spin_lock(lock);
}
EXPORT_SYMBOL(lg_local_lock);

void lg_local_unlock(struct lglock *lg)
{
	arch_spinlock_t *lock;

	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
	lock = this_cpu_ptr(lg->lock);
	arch_spin_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(lg_local_unlock);

void lg_local_lock_cpu(struct lglock *lg, int cpu)
{
	arch_spinlock_t *lock;

	preempt_disable();
	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	lock = per_cpu_ptr(lg->lock, cpu);
	arch_spin_lock(lock);
}
EXPORT_SYMBOL(lg_local_lock_cpu);

void lg_local_unlock_cpu(struct lglock *lg, int cpu)
{
	arch_spinlock_t *lock;

	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
	lock = per_cpu_ptr(lg->lock, cpu);
	arch_spin_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(lg_local_unlock_cpu);

void lg_double_lock(struct lglock *lg, int cpu1, int cpu2)
{
	BUG_ON(cpu1 == cpu2);

	/* lock in cpu order, just like lg_global_lock */
	if (cpu2 < cpu1)
		swap(cpu1, cpu2);

	preempt_disable();
	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	arch_spin_lock(per_cpu_ptr(lg->lock, cpu1));
	arch_spin_lock(per_cpu_ptr(lg->lock, cpu2));
}

void lg_double_unlock(struct lglock *lg, int cpu1, int cpu2)
{
	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
	arch_spin_unlock(per_cpu_ptr(lg->lock, cpu1));
	arch_spin_unlock(per_cpu_ptr(lg->lock, cpu2));
	preempt_enable();
}

void lg_global_lock(struct lglock *lg)
{
	int i;

	preempt_disable();
	lock_acquire_exclusive(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	for_each_possible_cpu(i) {
		arch_spinlock_t *lock;
		lock = per_cpu_ptr(lg->lock, i);
		arch_spin_lock(lock);
	}
}
EXPORT_SYMBOL(lg_global_lock);

void lg_global_unlock(struct lglock *lg)
{
	int i;

	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
	for_each_possible_cpu(i) {
		arch_spinlock_t *lock;
		lock = per_cpu_ptr(lg->lock, i);
		arch_spin_unlock(lock);
	}
	preempt_enable();
}
EXPORT_SYMBOL(lg_global_unlock);
