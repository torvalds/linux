#include <newt.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ttydefaults.h>

#include "../cache.h"
#include "../debug.h"
#include "browser.h"
#include "keysyms.h"
#include "helpline.h"
#include "ui.h"
#include "util.h"

static void ui_browser__argv_write(struct ui_browser *browser,
				   void *entry, int row)
{
	char **arg = entry;
	bool current_entry = ui_browser__is_current_entry(browser, row);

	ui_browser__set_color(browser, current_entry ? HE_COLORSET_SELECTED :
						       HE_COLORSET_NORMAL);
	slsmg_write_nstring(*arg, browser->width);
}

static int popup_menu__run(struct ui_browser *menu)
{
	int key;

	if (ui_browser__show(menu, " ", "ESC: exit, ENTER|->: Select option") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(menu, 0);

		switch (key) {
		case K_RIGHT:
		case K_ENTER:
			key = menu->index;
			break;
		case K_LEFT:
		case K_ESC:
		case 'q':
		case CTRL('c'):
			key = -1;
			break;
		default:
			continue;
		}

		break;
	}

	ui_browser__hide(menu);
	return key;
}

static void newt_form__set_exit_keys(newtComponent self)
{
	newtFormAddHotKey(self, NEWT_KEY_LEFT);
	newtFormAddHotKey(self, NEWT_KEY_ESCAPE);
	newtFormAddHotKey(self, 'Q');
	newtFormAddHotKey(self, 'q');
	newtFormAddHotKey(self, CTRL('c'));
}

static newtComponent newt_form__new(void)
{
	newtComponent self = newtForm(NULL, NULL, 0);
	if (self)
		newt_form__set_exit_keys(self);
	return self;
}

int ui__popup_menu(int argc, char * const argv[])
{
	struct ui_browser menu = {
		.entries    = (void *)argv,
		.refresh    = ui_browser__argv_refresh,
		.seek	    = ui_browser__argv_seek,
		.write	    = ui_browser__argv_write,
		.nr_entries = argc,
	};

	return popup_menu__run(&menu);
}

int ui__help_window(const char *text)
{
	struct newtExitStruct es;
	newtComponent tb, form = newt_form__new();
	int rc = -1;
	int max_len = 0, nr_lines = 0;
	const char *t;

	if (form == NULL)
		return -1;

	t = text;
	while (1) {
		const char *sep = strchr(t, '\n');
		int len;

		if (sep == NULL)
			sep = strchr(t, '\0');
		len = sep - t;
		if (max_len < len)
			max_len = len;
		++nr_lines;
		if (*sep == '\0')
			break;
		t = sep + 1;
	}

	tb = newtTextbox(0, 0, max_len, nr_lines, 0);
	if (tb == NULL)
		goto out_destroy_form;

	newtTextboxSetText(tb, text);
	newtFormAddComponent(form, tb);
	newtCenteredWindow(max_len, nr_lines, NULL);
	newtFormRun(form, &es);
	newtPopWindow();
	rc = 0;
out_destroy_form:
	newtFormDestroy(form);
	return rc;
}

static const char yes[] = "Yes", no[] = "No",
		  warning_str[] = "Warning!", ok[] = "Ok";

bool ui__dialog_yesno(const char *msg)
{
	/* newtWinChoice should really be accepting const char pointers... */
	return newtWinChoice(NULL, (char *)yes, (char *)no, (char *)msg) == 1;
}

void ui__warning(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	if (use_browser > 0) {
		pthread_mutex_lock(&ui__lock);
		newtWinMessagev((char *)warning_str, (char *)ok,
				(char *)format, args);
		pthread_mutex_unlock(&ui__lock);
	} else
		vfprintf(stderr, format, args);
	va_end(args);
}
