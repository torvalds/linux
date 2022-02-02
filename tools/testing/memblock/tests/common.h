/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MEMBLOCK_TEST_H
#define _MEMBLOCK_TEST_H

#include <linux/types.h>
#include <linux/memblock.h>

struct region {
	phys_addr_t base;
	phys_addr_t size;
};

void reset_memblock(void);

#endif
