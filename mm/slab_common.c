/*
 * Slab allocator functions that are independent of the allocator strategy
 *
 * (C) 2012 Christoph Lameter <cl@linux.com>
 */
#include <linux/slab.h>

#include <linux/mm.h>
#include <linux/poison.h>
#include <linux/interrupt.h>
#include <linux/memory.h>
#include <linux/compiler.h>
#include <linux/module.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/page.h>

#include "slab.h"

enum slab_state slab_state;
LIST_HEAD(slab_caches);
DEFINE_MUTEX(slab_mutex);

/*
 * kmem_cache_create - Create a cache.
 * @name: A string which is used in /proc/slabinfo to identify this cache.
 * @size: The size of objects to be created in this cache.
 * @align: The required alignment for the objects.
 * @flags: SLAB flags
 * @ctor: A constructor for the objects.
 *
 * Returns a ptr to the cache on success, NULL on failure.
 * Cannot be called within a interrupt, but can be interrupted.
 * The @ctor is run when new pages are allocated by the cache.
 *
 * The flags are
 *
 * %SLAB_POISON - Poison the slab with a known test pattern (a5a5a5a5)
 * to catch references to uninitialised memory.
 *
 * %SLAB_RED_ZONE - Insert `Red' zones around the allocated memory to check
 * for buffer overruns.
 *
 * %SLAB_HWCACHE_ALIGN - Align the objects in this cache to a hardware
 * cacheline.  This can be beneficial if you're counting cycles as closely
 * as davem.
 */

struct kmem_cache *kmem_cache_create(const char *name, size_t size, size_t align,
		unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *s = NULL;

#ifdef CONFIG_DEBUG_VM
	if (!name || in_interrupt() || size < sizeof(void *) ||
		size > KMALLOC_MAX_SIZE) {
		printk(KERN_ERR "kmem_cache_create(%s) integrity check"
			" failed\n", name);
		goto out;
	}
#endif

	s = __kmem_cache_create(name, size, align, flags, ctor);

#ifdef CONFIG_DEBUG_VM
out:
#endif
	if (!s && (flags & SLAB_PANIC))
		panic("kmem_cache_create: Failed to create slab '%s'\n", name);

	return s;
}
EXPORT_SYMBOL(kmem_cache_create);

int slab_is_available(void)
{
	return slab_state >= UP;
}
