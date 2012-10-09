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
#include <linux/cpu.h>
#include <linux/uaccess.h>
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

	get_online_cpus();
	mutex_lock(&slab_mutex);

#ifdef CONFIG_DEBUG_VM
	list_for_each_entry(s, &slab_caches, list) {
		char tmp;
		int res;

		/*
		 * This happens when the module gets unloaded and doesn't
		 * destroy its slab cache and no-one else reuses the vmalloc
		 * area of the module.  Print a warning.
		 */
		res = probe_kernel_address(s->name, tmp);
		if (res) {
			printk(KERN_ERR
			       "Slab cache with size %d has lost its name\n",
			       s->object_size);
			continue;
		}

		if (!strcmp(s->name, name)) {
			printk(KERN_ERR "kmem_cache_create(%s): Cache name"
				" already exists.\n",
				name);
			dump_stack();
			s = NULL;
			goto oops;
		}
	}

	WARN_ON(strchr(name, ' '));	/* It confuses parsers */
#endif

	s = __kmem_cache_create(name, size, align, flags, ctor);

#ifdef CONFIG_DEBUG_VM
oops:
#endif
	mutex_unlock(&slab_mutex);
	put_online_cpus();

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
