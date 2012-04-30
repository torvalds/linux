#include "gtk.h"
#include "../../util/cache.h"

void perf_gtk__init(bool fallback_to_pager __used)
{
	gtk_init(NULL, NULL);
}

void perf_gtk__exit(bool wait_for_ok __used)
{
	gtk_main_quit();
}
