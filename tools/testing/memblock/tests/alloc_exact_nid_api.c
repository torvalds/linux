// SPDX-License-Identifier: GPL-2.0-or-later
#include "alloc_exact_nid_api.h"
#include "alloc_nid_api.h"

#define FUNC_NAME			"memblock_alloc_exact_nid_raw"

int memblock_alloc_exact_nid_checks(void)
{
	prefix_reset();
	prefix_push(FUNC_NAME);

	reset_memblock_attributes();
	dummy_physical_memory_init();

	memblock_alloc_exact_nid_range_checks();

	dummy_physical_memory_cleanup();

	prefix_pop();

	return 0;
}
