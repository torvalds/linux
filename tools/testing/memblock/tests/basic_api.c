// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include <linux/memblock.h>
#include "basic_api.h"

#define EXPECTED_MEMBLOCK_REGIONS			128

static int memblock_initialization_check(void)
{
	reset_memblock();

	assert(memblock.memory.regions);
	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.max == EXPECTED_MEMBLOCK_REGIONS);
	assert(strcmp(memblock.memory.name, "memory") == 0);

	assert(memblock.reserved.regions);
	assert(memblock.reserved.cnt == 1);
	assert(memblock.memory.max == EXPECTED_MEMBLOCK_REGIONS);
	assert(strcmp(memblock.reserved.name, "reserved") == 0);

	assert(!memblock.bottom_up);
	assert(memblock.current_limit == MEMBLOCK_ALLOC_ANYWHERE);

	return 0;
}

int memblock_basic_checks(void)
{
	memblock_initialization_check();
	return 0;
}
