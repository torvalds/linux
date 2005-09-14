#ifndef __ASM_SPINLOCK_TYPES_H
#define __ASM_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

typedef struct {
	volatile unsigned int lock[4];
} raw_spinlock_t;

#define __RAW_SPIN_LOCK_UNLOCKED	{ { 1, 1, 1, 1 } }

typedef struct {
	raw_spinlock_t lock;
	volatile int counter;
} raw_rwlock_t;

#define __RAW_RW_LOCK_UNLOCKED		{ __RAW_SPIN_LOCK_UNLOCKED, 0 }

#endif
