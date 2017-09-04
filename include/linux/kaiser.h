#ifndef _LINUX_KAISER_H
#define _LINUX_KAISER_H

#ifdef CONFIG_KAISER
#include <asm/kaiser.h>

static inline int kaiser_map_thread_stack(void *stack)
{
	/*
	 * Map that page of kernel stack on which we enter from user context.
	 */
	return kaiser_add_mapping((unsigned long)stack +
			THREAD_SIZE - PAGE_SIZE, PAGE_SIZE, __PAGE_KERNEL);
}

static inline void kaiser_unmap_thread_stack(void *stack)
{
	/*
	 * Note: may be called even when kaiser_map_thread_stack() failed.
	 */
	kaiser_remove_mapping((unsigned long)stack +
			THREAD_SIZE - PAGE_SIZE, PAGE_SIZE);
}
#else

/*
 * These stubs are used whenever CONFIG_KAISER is off, which
 * includes architectures that support KAISER, but have it disabled.
 */

static inline void kaiser_init(void)
{
}
static inline int kaiser_add_mapping(unsigned long addr,
				     unsigned long size, unsigned long flags)
{
	return 0;
}
static inline void kaiser_remove_mapping(unsigned long start,
					 unsigned long size)
{
}
static inline int kaiser_map_thread_stack(void *stack)
{
	return 0;
}
static inline void kaiser_unmap_thread_stack(void *stack)
{
}

#endif /* !CONFIG_KAISER */
#endif /* _LINUX_KAISER_H */
