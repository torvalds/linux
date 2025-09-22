/* Public domain. */

#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#define down_read(rwl)			rw_enter_read(rwl)
#define down_read_trylock(rwl)		(rw_enter(rwl, RW_READ | RW_NOSLEEP) == 0)
#define down_write_trylock(rwl)		(rw_enter(rwl, RW_WRITE | RW_NOSLEEP) == 0)
#define up_read(rwl)			rw_exit_read(rwl)
#define down_write(rwl)			rw_enter_write(rwl)
#define down_write_nest_lock(rwl, x)	rw_enter_write(rwl)
#define up_write(rwl)			rw_exit_write(rwl)
#define downgrade_write(rwl)		rw_enter(rwl, RW_DOWNGRADE)

#define DECLARE_RWSEM(rwl) \
	struct rwlock rwl = RWLOCK_INITIALIZER(#rwl)
/* no interface to check if another caller wants the lock */
#define rwsem_is_contended(rwl)		0

#endif
