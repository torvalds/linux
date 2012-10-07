#ifndef _PERF_UI_HELPLINE_H_
#define _PERF_UI_HELPLINE_H_ 1

#include <stdio.h>
#include <stdarg.h>

#include "../util/cache.h"

struct ui_helpline {
	void (*pop)(void);
	void (*push)(const char *msg);
};

extern struct ui_helpline *helpline_fns;

void ui_helpline__init(void);

void ui_helpline__pop(void);
void ui_helpline__push(const char *msg);
void ui_helpline__vpush(const char *fmt, va_list ap);
void ui_helpline__fpush(const char *fmt, ...);
void ui_helpline__puts(const char *msg);

extern char ui_helpline__current[512];

#ifdef NO_NEWT_SUPPORT
static inline int ui_helpline__show_help(const char *format __maybe_unused,
					 va_list ap __maybe_unused)
{
	return 0;
}
#else
extern char ui_helpline__last_msg[];
int ui_helpline__show_help(const char *format, va_list ap);
#endif /* NO_NEWT_SUPPORT */

#ifdef NO_GTK2_SUPPORT
static inline int perf_gtk__show_helpline(const char *format __maybe_unused,
					  va_list ap __maybe_unused)
{
	return 0;
}
#else
int perf_gtk__show_helpline(const char *format, va_list ap);
#endif /* NO_GTK2_SUPPORT */

#endif /* _PERF_UI_HELPLINE_H_ */
