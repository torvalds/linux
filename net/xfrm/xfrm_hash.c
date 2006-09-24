/* xfrm_hash.c: Common hash table code.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/xfrm.h>

#include "xfrm_hash.h"

struct hlist_head *xfrm_hash_alloc(unsigned int sz)
{
	struct hlist_head *n;

	if (sz <= PAGE_SIZE)
		n = kmalloc(sz, GFP_KERNEL);
	else if (hashdist)
		n = __vmalloc(sz, GFP_KERNEL, PAGE_KERNEL);
	else
		n = (struct hlist_head *)
			__get_free_pages(GFP_KERNEL, get_order(sz));

	if (n)
		memset(n, 0, sz);

	return n;
}

void xfrm_hash_free(struct hlist_head *n, unsigned int sz)
{
	if (sz <= PAGE_SIZE)
		kfree(n);
	else if (hashdist)
		vfree(n);
	else
		free_pages((unsigned long)n, get_order(sz));
}
