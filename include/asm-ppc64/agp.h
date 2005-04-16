#ifndef AGP_H
#define AGP_H 1

#include <asm/io.h>

/* nothing much needed here */

#define map_page_into_agp(page)
#define unmap_page_from_agp(page)
#define flush_agp_mappings()
#define flush_agp_cache() mb()

#endif
