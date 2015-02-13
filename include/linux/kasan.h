#ifndef _LINUX_KASAN_H
#define _LINUX_KASAN_H

#include <linux/types.h>

struct kmem_cache;
struct page;

#ifdef CONFIG_KASAN

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_OFFSET _AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

#include <asm/kasan.h>
#include <linux/sched.h>

static inline void *kasan_mem_to_shadow(const void *addr)
{
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
}

/* Enable reporting bugs after kasan_disable_current() */
static inline void kasan_enable_current(void)
{
	current->kasan_depth++;
}

/* Disable reporting bugs for current task */
static inline void kasan_disable_current(void)
{
	current->kasan_depth--;
}

void kasan_unpoison_shadow(const void *address, size_t size);

#else /* CONFIG_KASAN */

static inline void kasan_unpoison_shadow(const void *address, size_t size) {}

static inline void kasan_enable_current(void) {}
static inline void kasan_disable_current(void) {}

#endif /* CONFIG_KASAN */

#endif /* LINUX_KASAN_H */
