#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <newt.h>

#include "helpline.h"

void ui_helpline__pop(void)
{
	newtPopHelpLine();
}

void ui_helpline__push(const char *msg)
{
	newtPushHelpLine(msg);
}

static void ui_helpline__vpush(const char *fmt, va_list ap)
{
	char *s;

	if (vasprintf(&s, fmt, ap) < 0)
		vfprintf(stderr, fmt, ap);
	else {
		ui_helpline__push(s);
		free(s);
	}
}

void ui_helpline__fpush(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ui_helpline__vpush(fmt, ap);
	va_end(ap);
}

void ui_helpline__puts(const char *msg)
{
	ui_helpline__pop();
	ui_helpline__push(msg);
}
