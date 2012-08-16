#ifndef _PERF_UI_HELPLINE_H_
#define _PERF_UI_HELPLINE_H_ 1

#include <stdio.h>
#include <stdarg.h>

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

#endif /* _PERF_UI_HELPLINE_H_ */
