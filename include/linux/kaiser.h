#ifndef _INCLUDE_KAISER_H
#define _INCLUDE_KAISER_H

#ifdef CONFIG_KAISER
#include <asm/kaiser.h>
#else

/*
 * These stubs are used whenever CONFIG_KAISER is off, which
 * includes architectures that support KAISER, but have it
 * disabled.
 */

static inline void kaiser_init(void)
{
}
static inline void kaiser_remove_mapping(unsigned long start, unsigned long size)
{
}
static inline int kaiser_add_mapping(unsigned long addr, unsigned long size, unsigned long flags)
{
	return 0;
}

#endif /* !CONFIG_KAISER */
#endif /* _INCLUDE_KAISER_H */
