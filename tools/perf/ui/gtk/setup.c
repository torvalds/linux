// SPDX-License-Identifier: GPL-2.0
#include "gtk.h"
#include <linux/compiler.h>
#include "../util.h"

extern struct perf_error_ops perf_gtk_eops;

int perf_gtk__init(void)
{
	perf_error__register(&perf_gtk_eops);
	perf_gtk__init_helpline();
	gtk_ui_progress__init();
	perf_gtk__init_hpp();

	return gtk_init_check(NULL, NULL) ? 0 : -1;
}

void perf_gtk__exit(bool wait_for_ok __maybe_unused)
{
	if (!perf_gtk__is_active_context(pgctx))
		return;
	perf_error__unregister(&perf_gtk_eops);
	gtk_main_quit();
}
