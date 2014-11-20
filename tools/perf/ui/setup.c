#include <pthread.h>
#include <dlfcn.h>

#include "../util/cache.h"
#include "../util/debug.h"
#include "../util/hist.h"

pthread_mutex_t ui__lock = PTHREAD_MUTEX_INITIALIZER;
void *perf_gtk_handle;

#ifdef HAVE_GTK2_SUPPORT
static int setup_gtk_browser(void)
{
	int (*perf_ui_init)(void);

	if (perf_gtk_handle)
		return 0;

	perf_gtk_handle = dlopen(PERF_GTK_DSO, RTLD_LAZY);
	if (perf_gtk_handle == NULL) {
		char buf[PATH_MAX];
		scnprintf(buf, sizeof(buf), "%s/%s", LIBDIR, PERF_GTK_DSO);
		perf_gtk_handle = dlopen(buf, RTLD_LAZY);
	}
	if (perf_gtk_handle == NULL)
		return -1;

	perf_ui_init = dlsym(perf_gtk_handle, "perf_gtk__init");
	if (perf_ui_init == NULL)
		goto out_close;

	if (perf_ui_init() == 0)
		return 0;

out_close:
	dlclose(perf_gtk_handle);
	return -1;
}

static void exit_gtk_browser(bool wait_for_ok)
{
	void (*perf_ui_exit)(bool);

	if (perf_gtk_handle == NULL)
		return;

	perf_ui_exit = dlsym(perf_gtk_handle, "perf_gtk__exit");
	if (perf_ui_exit == NULL)
		goto out_close;

	perf_ui_exit(wait_for_ok);

out_close:
	dlclose(perf_gtk_handle);

	perf_gtk_handle = NULL;
}
#else
static inline int setup_gtk_browser(void) { return -1; }
static inline void exit_gtk_browser(bool wait_for_ok __maybe_unused) {}
#endif

void setup_browser(bool fallback_to_pager)
{
	if (use_browser < 2 && (!isatty(1) || dump_trace))
		use_browser = 0;

	/* default to TUI */
	if (use_browser < 0)
		use_browser = 1;

	switch (use_browser) {
	case 2:
		if (setup_gtk_browser() == 0)
			break;
		printf("GTK browser requested but could not find %s\n",
		       PERF_GTK_DSO);
		sleep(1);
		/* fall through */
	case 1:
		use_browser = 1;
		if (ui__init() == 0)
			break;
		/* fall through */
	default:
		use_browser = 0;
		if (fallback_to_pager)
			setup_pager();
		break;
	}
}

void exit_browser(bool wait_for_ok)
{
	switch (use_browser) {
	case 2:
		exit_gtk_browser(wait_for_ok);
		break;

	case 1:
		ui__exit(wait_for_ok);
		break;

	default:
		break;
	}
}
