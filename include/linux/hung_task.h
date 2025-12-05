/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Detect Hung Task: detecting tasks stuck in D state
 *
 * Copyright (C) 2025 Tongcheng Travel (www.ly.com)
 * Author: Lance Yang <mingzhe.yang@ly.com>
 */
#ifndef __LINUX_HUNG_TASK_H
#define __LINUX_HUNG_TASK_H

#include <linux/bug.h>
#include <linux/sched.h>
#include <linux/compiler.h>

/*
 * @blocker: Combines lock address and blocking type.
 *
 * Since lock pointers are at least 4-byte aligned(32-bit) or 8-byte
 * aligned(64-bit). This leaves the 2 least bits (LSBs) of the pointer
 * always zero. So we can use these bits to encode the specific blocking
 * type.
 *
 * Note that on architectures where this is not guaranteed, or for any
 * unaligned lock, this tracking mechanism is silently skipped for that
 * lock.
 *
 * Type encoding:
 * 00 - Blocked on mutex			(BLOCKER_TYPE_MUTEX)
 * 01 - Blocked on semaphore			(BLOCKER_TYPE_SEM)
 * 10 - Blocked on rw-semaphore as READER	(BLOCKER_TYPE_RWSEM_READER)
 * 11 - Blocked on rw-semaphore as WRITER	(BLOCKER_TYPE_RWSEM_WRITER)
 */
#define BLOCKER_TYPE_MUTEX		0x00UL
#define BLOCKER_TYPE_SEM		0x01UL
#define BLOCKER_TYPE_RWSEM_READER	0x02UL
#define BLOCKER_TYPE_RWSEM_WRITER	0x03UL

#define BLOCKER_TYPE_MASK		0x03UL

#ifdef CONFIG_DETECT_HUNG_TASK_BLOCKER
static inline void hung_task_set_blocker(void *lock, unsigned long type)
{
	unsigned long lock_ptr = (unsigned long)lock;

	WARN_ON_ONCE(!lock_ptr);
	WARN_ON_ONCE(READ_ONCE(current->blocker));

	/*
	 * If the lock pointer matches the BLOCKER_TYPE_MASK, return
	 * without writing anything.
	 */
	if (lock_ptr & BLOCKER_TYPE_MASK)
		return;

	WRITE_ONCE(current->blocker, lock_ptr | type);
}

static inline void hung_task_clear_blocker(void)
{
	WRITE_ONCE(current->blocker, 0UL);
}

/*
 * hung_task_get_blocker_type - Extracts blocker type from encoded blocker
 * address.
 *
 * @blocker: Blocker pointer with encoded type (via LSB bits)
 *
 * Returns: BLOCKER_TYPE_MUTEX, BLOCKER_TYPE_SEM, etc.
 */
static inline unsigned long hung_task_get_blocker_type(unsigned long blocker)
{
	WARN_ON_ONCE(!blocker);

	return blocker & BLOCKER_TYPE_MASK;
}

static inline void *hung_task_blocker_to_lock(unsigned long blocker)
{
	WARN_ON_ONCE(!blocker);

	return (void *)(blocker & ~BLOCKER_TYPE_MASK);
}
#else
static inline void hung_task_set_blocker(void *lock, unsigned long type)
{
}
static inline void hung_task_clear_blocker(void)
{
}
static inline unsigned long hung_task_get_blocker_type(unsigned long blocker)
{
	return 0UL;
}
static inline void *hung_task_blocker_to_lock(unsigned long blocker)
{
	return NULL;
}
#endif

#endif /* __LINUX_HUNG_TASK_H */
