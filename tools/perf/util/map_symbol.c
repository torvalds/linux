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
