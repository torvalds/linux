/* thread_info.h: common low-level thread information accessors
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds
 */

#ifndef _LINUX_THREAD_INFO_H
#define _LINUX_THREAD_INFO_H

/*
 * System call restart block. 
 */
struct restart_block {
	long (*fn)(struct restart_block *);
	unsigned long arg0, arg1, arg2, arg3;
};

extern long do_no_restart_syscall(struct restart_block *parm);

#include <linux/bitops.h>
#include <asm/thread_info.h>

#ifdef __KERNEL__

/*
 * flag set/clear/test wrappers
 * - pass TIF_xxxx constants to these functions
 */

static inline void set_thread_flag(int flag)
{
	set_bit(flag,&current_thread_info()->flags);
}

static inline void clear_thread_flag(int flag)
{
	clear_bit(flag,&current_thread_info()->flags);
}

static inline int test_and_set_thread_flag(int flag)
{
	return test_and_set_bit(flag,&current_thread_info()->flags);
}

static inline int test_and_clear_thread_flag(int flag)
{
	return test_and_clear_bit(flag,&current_thread_info()->flags);
}

static inline int test_thread_flag(int flag)
{
	return test_bit(flag,&current_thread_info()->flags);
}

static inline void set_ti_thread_flag(struct thread_info *ti, int flag)
{
	set_bit(flag,&ti->flags);
}

static inline void clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	clear_bit(flag,&ti->flags);
}

static inline int test_and_set_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_and_set_bit(flag,&ti->flags);
}

static inline int test_and_clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_and_clear_bit(flag,&ti->flags);
}

static inline int test_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_bit(flag,&ti->flags);
}

static inline void set_need_resched(void)
{
	set_thread_flag(TIF_NEED_RESCHED);
}

static inline void clear_need_resched(void)
{
	clear_thread_flag(TIF_NEED_RESCHED);
}

#endif

#endif /* _LINUX_THREAD_INFO_H */
