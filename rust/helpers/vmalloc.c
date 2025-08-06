// SPDX-License-Identifier: GPL-2.0

#include <linux/vmalloc.h>

void * __must_check __realloc_size(2)
rust_helper_vrealloc_node(const void *p, size_t size, gfp_t flags, int node)
{
	return vrealloc_node(p, size, flags, node);
}
