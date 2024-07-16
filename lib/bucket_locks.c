#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/* Allocate an array of spinlocks to be accessed by a hash. Two arguments
 * indicate the number of elements to allocate in the array. max_size
 * gives the maximum number of elements to allocate. cpu_mult gives
 * the number of locks per CPU to allocate. The size is rounded up
 * to a power of 2 to be suitable as a hash table.
 */

int __alloc_bucket_spinlocks(spinlock_t **locks, unsigned int *locks_mask,
			     size_t max_size, unsigned int cpu_mult, gfp_t gfp,
			     const char *name, struct lock_class_key *key)
{
	spinlock_t *tlocks = NULL;
	unsigned int i, size;
#if defined(CONFIG_PROVE_LOCKING)
	unsigned int nr_pcpus = 2;
#else
	unsigned int nr_pcpus = num_possible_cpus();
#endif

	if (cpu_mult) {
		nr_pcpus = min_t(unsigned int, nr_pcpus, 64UL);
		size = min_t(unsigned int, nr_pcpus * cpu_mult, max_size);
	} else {
		size = max_size;
	}

	if (sizeof(spinlock_t) != 0) {
		tlocks = kvmalloc_array(size, sizeof(spinlock_t), gfp);
		if (!tlocks)
			return -ENOMEM;
		for (i = 0; i < size; i++) {
			spin_lock_init(&tlocks[i]);
			lockdep_init_map(&tlocks[i].dep_map, name, key, 0);
		}
	}

	*locks = tlocks;
	*locks_mask = size - 1;

	return 0;
}
EXPORT_SYMBOL(__alloc_bucket_spinlocks);

void free_bucket_spinlocks(spinlock_t *locks)
{
	kvfree(locks);
}
EXPORT_SYMBOL(free_bucket_spinlocks);
