#ifndef __LINUX_MUTEX_DEBUG_H
#define __LINUX_MUTEX_DEBUG_H

#include <linux/linkage.h>

/*
 * Mutexes - debugging helpers:
 */

#define __DEBUG_MUTEX_INITIALIZER(lockname) \
	, .held_list = LIST_HEAD_INIT(lockname.held_list), \
	  .name = #lockname , .magic = &lockname

#define mutex_init(sem)		__mutex_init(sem, __FUNCTION__)

extern void FASTCALL(mutex_destroy(struct mutex *lock));

extern void mutex_debug_show_all_locks(void);
extern void mutex_debug_show_held_locks(struct task_struct *filter);
extern void mutex_debug_check_no_locks_held(struct task_struct *task);
extern void mutex_debug_check_no_locks_freed(const void *from, unsigned long len);

#endif
