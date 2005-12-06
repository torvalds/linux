#ifndef __ASM_SPINLOCK_TYPES_H
#define __ASM_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

typedef struct {
#ifdef CONFIG_PA20
	volatile unsigned int slock;
# define __RAW_SPIN_LOCK_UNLOCKED { 1 }
#else
	volatile unsigned int lock[4];
# define __RAW_SPIN_LOCK_UNLOCKED	{ { 1, 1, 1, 1 } }
#endif
} raw_spinlock_t;

typedef struct {
	raw_spinlock_t lock;
	volatile int counter;
} raw_rwlock_t;

#define __RAW_RW_LOCK_UNLOCKED		{ __RAW_SPIN_LOCK_UNLOCKED, 0 }

#endif
