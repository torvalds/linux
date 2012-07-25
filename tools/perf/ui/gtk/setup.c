#include "gtk.h"
#include "../../util/cache.h"

int perf_gtk__init(void)
{
	return gtk_init_check(NULL, NULL) ? 0 : -1;
}

void perf_gtk__exit(bool wait_for_ok __used)
{
	gtk_main_quit();
}
