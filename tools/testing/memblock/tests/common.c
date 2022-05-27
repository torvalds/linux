// SPDX-License-Identifier: GPL-2.0-or-later
#include "tests/common.h"
#include <string.h>

#define INIT_MEMBLOCK_REGIONS			128
#define INIT_MEMBLOCK_RESERVED_REGIONS		INIT_MEMBLOCK_REGIONS

static struct test_memory memory_block;

void reset_memblock_regions(void)
{
	memset(memblock.memory.regions, 0,
	       memblock.memory.cnt * sizeof(struct memblock_region));
	memblock.memory.cnt	= 1;
	memblock.memory.max	= INIT_MEMBLOCK_REGIONS;
	memblock.memory.total_size = 0;

	memset(memblock.reserved.regions, 0,
	       memblock.reserved.cnt * sizeof(struct memblock_region));
	memblock.reserved.cnt	= 1;
	memblock.reserved.max	= INIT_MEMBLOCK_RESERVED_REGIONS;
	memblock.reserved.total_size = 0;
}

void reset_memblock_attributes(void)
{
	memblock.memory.name	= "memory";
	memblock.reserved.name	= "reserved";
	memblock.bottom_up	= false;
	memblock.current_limit	= MEMBLOCK_ALLOC_ANYWHERE;
}

void setup_memblock(void)
{
	reset_memblock_regions();
	memblock_add((phys_addr_t)memory_block.base, MEM_SIZE);
}

void dummy_physical_memory_init(void)
{
	memory_block.base = malloc(MEM_SIZE);
	assert(memory_block.base);
}

void dummy_physical_memory_cleanup(void)
{
	free(memory_block.base);
}
