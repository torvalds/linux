/*
 * Copyright (C) 2008 Eduard - Gabriel Munteanu
 *
 * This file is released under GPL version 2.
 */

#ifndef _LINUX_KMEMTRACE_H
#define _LINUX_KMEMTRACE_H

#ifdef __KERNEL__

#include <trace/kmem.h>

#ifdef CONFIG_KMEMTRACE
extern void kmemtrace_init(void);
#else
static inline void kmemtrace_init(void)
{
}
#endif

#endif /* __KERNEL__ */

#endif /* _LINUX_KMEMTRACE_H */

