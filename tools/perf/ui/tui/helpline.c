#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "../../util/debug.h"
#include "../helpline.h"
#include "../ui.h"
#include "../libslang.h"

char ui_helpline__last_msg[1024];

static void tui_helpline__pop(void)
{
}

static void tui_helpline__push(const char *msg)
{
	const size_t sz = sizeof(ui_helpline__current);

	SLsmg_gotorc(SLtt_Screen_Rows - 1, 0);
	SLsmg_set_color(0);
	SLsmg_write_nstring((char *)msg, SLtt_Screen_Cols);
	SLsmg_refresh();
	strncpy(ui_helpline__current, msg, sz)[sz - 1] = '\0';
}

static int tui_helpline__show(const char *format, va_list ap)
{
	int ret;
	static int backlog;

	pthread_mutex_lock(&ui__lock);
	ret = vscnprintf(ui_helpline__last_msg + backlog,
			sizeof(ui_helpline__last_msg) - backlog, format, ap);
	backlog += ret;

	if (ui_helpline__last_msg[backlog - 1] == '\n') {
		ui_helpline__puts(ui_helpline__last_msg);
		SLsmg_refresh();
		backlog = 0;
	}
	pthread_mutex_unlock(&ui__lock);

	return ret;
}

struct ui_helpline tui_helpline_fns = {
	.pop	= tui_helpline__pop,
	.push	= tui_helpline__push,
	.show	= tui_helpline__show,
};

void ui_helpline__init(void)
{
	helpline_fns = &tui_helpline_fns;
	ui_helpline__puts(" ");
}
