// SPDX-License-Identifier: GPL-2.0-or-later
#include "tests/common.h"
#include <string.h>

#define INIT_MEMBLOCK_REGIONS			128
#define INIT_MEMBLOCK_RESERVED_REGIONS		INIT_MEMBLOCK_REGIONS

void reset_memblock(void)
{
	memset(memblock.memory.regions, 0,
	       memblock.memory.cnt * sizeof(struct memblock_region));
	memset(memblock.reserved.regions, 0,
	       memblock.reserved.cnt * sizeof(struct memblock_region));

	memblock.memory.cnt	= 1;
	memblock.memory.max	= INIT_MEMBLOCK_REGIONS;
	memblock.memory.name	= "memory";
	memblock.memory.total_size = 0;

	memblock.reserved.cnt	= 1;
	memblock.reserved.max	= INIT_MEMBLOCK_RESERVED_REGIONS;
	memblock.reserved.name	= "reserved";
	memblock.reserved.total_size = 0;

	memblock.bottom_up	= false;
	memblock.current_limit	= MEMBLOCK_ALLOC_ANYWHERE;
}
