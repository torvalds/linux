// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>

void * __must_check __realloc_size(2)
rust_helper_krealloc_node_align(const void *objp, size_t new_size, unsigned long align,
				gfp_t flags, int node)
{
	return krealloc_node_align(objp, new_size, align, flags, node);
}

void * __must_check __realloc_size(2)
rust_helper_kvrealloc_node_align(const void *p, size_t size, unsigned long align,
				 gfp_t flags, int node)
{
	return kvrealloc_node_align(p, size, align, flags, node);
}
