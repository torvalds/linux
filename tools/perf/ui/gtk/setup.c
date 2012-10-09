#include "gtk.h"
#include "../../util/cache.h"
#include "../../util/debug.h"

extern struct perf_error_ops perf_gtk_eops;

int perf_gtk__init(void)
{
	perf_error__register(&perf_gtk_eops);
	return gtk_init_check(NULL, NULL) ? 0 : -1;
}

void perf_gtk__exit(bool wait_for_ok __used)
{
	perf_error__unregister(&perf_gtk_eops);
	gtk_main_quit();
}
