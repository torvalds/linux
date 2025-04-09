// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <string.h>

#include <urcu/uatomic.h>
#include <linux/slab.h>
#include <malloc.h>
#include <linux/gfp.h>

int kmalloc_nr_allocated;
int kmalloc_verbose;

void *kmalloc(size_t size, gfp_t gfp)
{
	void *ret;

	if (!(gfp & __GFP_DIRECT_RECLAIM))
		return NULL;

	ret = malloc(size);
	uatomic_inc(&kmalloc_nr_allocated);
	if (kmalloc_verbose)
		printf("Allocating %p from malloc\n", ret);
	if (gfp & __GFP_ZERO)
		memset(ret, 0, size);
	return ret;
}

void kfree(void *p)
{
	if (!p)
		return;
	uatomic_dec(&kmalloc_nr_allocated);
	if (kmalloc_verbose)
		printf("Freeing %p to malloc\n", p);
	free(p);
}

void *kmalloc_array(size_t n, size_t size, gfp_t gfp)
{
	void *ret;

	if (!(gfp & __GFP_DIRECT_RECLAIM))
		return NULL;

	ret = calloc(n, size);
	uatomic_inc(&kmalloc_nr_allocated);
	if (kmalloc_verbose)
		printf("Allocating %p from calloc\n", ret);
	if (gfp & __GFP_ZERO)
		memset(ret, 0, n * size);
	return ret;
}
