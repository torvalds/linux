// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>

void * __must_check __realloc_size(2)
rust_helper_krealloc_node(const void *objp, size_t new_size, gfp_t flags, int node)
{
	return krealloc_node(objp, new_size, flags, node);
}

void * __must_check __realloc_size(2)
rust_helper_kvrealloc_node(const void *p, size_t size, gfp_t flags, int node)
{
	return kvrealloc_node(p, size, flags, node);
}
