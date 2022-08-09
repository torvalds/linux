// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/mmzone.h>

struct pglist_data *first_online_pgdat(void)
{
	return NULL;
}

struct pglist_data *next_online_pgdat(struct pglist_data *pgdat)
{
	return NULL;
}

void reserve_bootmem_region(phys_addr_t start, phys_addr_t end)
{
}

void atomic_long_set(atomic_long_t *v, long i)
{
}
