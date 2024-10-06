// SPDX-License-Identifier: GPL-2.0
#include "map_symbol.h"
#include "maps.h"
#include "map.h"

void map_symbol__exit(struct map_symbol *ms)
{
	maps__zput(ms->maps);
	map__zput(ms->map);
}

void addr_map_symbol__exit(struct addr_map_symbol *ams)
{
	map_symbol__exit(&ams->ms);
}

void map_symbol__copy(struct map_symbol *dst, struct map_symbol *src)
{
	dst->maps = maps__get(src->maps);
	dst->map = map__get(src->map);
	dst->sym = src->sym;
}

void addr_map_symbol__copy(struct addr_map_symbol *dst, struct addr_map_symbol *src)
{
	map_symbol__copy(&dst->ms, &src->ms);

	dst->addr = src->addr;
	dst->al_addr = src->al_addr;
	dst->al_level = src->al_level;
	dst->phys_addr = src->phys_addr;
	dst->data_page_size = src->data_page_size;
}
