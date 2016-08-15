#ifndef _PERF_UI_H_
#define _PERF_UI_H_ 1

#include <pthread.h>
#include <stdbool.h>
#include <linux/compiler.h>

extern pthread_mutex_t ui__lock;
extern void *perf_gtk_handle;

extern int use_browser;

void setup_browser(bool fallback_to_pager);
void exit_browser(bool wait_for_ok);

#ifdef HAVE_SLANG_SUPPORT
int ui__init(void);
void ui__exit(bool wait_for_ok);
#else
static inline int ui__init(void)
{
	return -1;
}
static inline void ui__exit(bool wait_for_ok __maybe_unused) {}
#endif

void ui__refresh_dimensions(bool force);

struct option;

int stdio__config_color(const struct option *opt, const char *mode, int unset);

#endif /* _PERF_UI_H_ */
