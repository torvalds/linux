#ifndef __LINUX_MUTEX_DEBUG_H
#define __LINUX_MUTEX_DEBUG_H

#include <linux/linkage.h>

/*
 * Mutexes - debugging helpers:
 */

#define __DEBUG_MUTEX_INITIALIZER(lockname)				\
	, .magic = &lockname

#define mutex_init(sem)		__mutex_init(sem, __FILE__":"#sem)

extern void FASTCALL(mutex_destroy(struct mutex *lock));

#endif
