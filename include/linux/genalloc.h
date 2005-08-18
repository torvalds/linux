/*
 * Basic general purpose allocator for managing special purpose memory
 * not managed by the regular kmalloc/kfree interface.
 * Uses for this includes on-device special memory, uncached memory
 * etc.
 *
 * This code is based on the buddy allocator found in the sym53c8xx_2
 * driver, adapted for general purpose use.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/spinlock.h>

#define ALLOC_MIN_SHIFT		5 /* 32 bytes minimum */
/*
 *  Link between free memory chunks of a given size.
 */
struct gen_pool_link {
	struct gen_pool_link *next;
};

/*
 *  Memory pool descriptor.
 */
struct gen_pool {
	spinlock_t lock;
	unsigned long (*get_new_chunk)(struct gen_pool *);
	struct gen_pool *next;
	struct gen_pool_link *h;
	unsigned long private;
	int max_chunk_shift;
};

unsigned long gen_pool_alloc(struct gen_pool *poolp, int size);
void gen_pool_free(struct gen_pool *mp, unsigned long ptr, int size);
struct gen_pool *gen_pool_create(int nr_chunks, int max_chunk_shift,
				 unsigned long (*fp)(struct gen_pool *),
				 unsigned long data);
