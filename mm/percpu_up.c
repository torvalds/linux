/*
 * mm/percpu_up.c - dummy percpu memory allocator implementation for UP
 */

#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/slab.h>

void __percpu *__alloc_percpu(size_t size, size_t align)
{
	/*
	 * Can't easily make larger alignment work with kmalloc.  WARN
	 * on it.  Larger alignment should only be used for module
	 * percpu sections on SMP for which this path isn't used.
	 */
	WARN_ON_ONCE(align > SMP_CACHE_BYTES);
	return kzalloc(size, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(__alloc_percpu);

void free_percpu(void __percpu *p)
{
	kfree(p);
}
EXPORT_SYMBOL_GPL(free_percpu);

phys_addr_t per_cpu_ptr_to_phys(void *addr)
{
	return __pa(addr);
}
