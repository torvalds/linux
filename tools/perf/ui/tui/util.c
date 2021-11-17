// SPDX-License-Identifier: GPL-2.0
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ttydefaults.h>

#include "../browser.h"
#include "../keysyms.h"
#include "../helpline.h"
#include "../ui.h"
#include "../util.h"
#include "../libslang.h"

static void ui_browser__argv_write(struct ui_browser *browser,
				   void *entry, int row)
{
	char **arg = entry;
	bool current_entry = ui_browser__is_current_entry(browser, row);

	ui_browser__set_color(browser, current_entry ? HE_COLORSET_SELECTED :
						       HE_COLORSET_NORMAL);
	ui_browser__write_nstring(browser, *arg, browser->width);
}

static int popup_menu__run(struct ui_browser *menu, int *keyp)
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
			if (keyp) {
				*keyp = key;
				key = menu->nr_entries;
				break;
			}
			continue;
		}

		break;
	}

	ui_browser__hide(menu);
	return key;
}

int ui__popup_menu(int argc, char * const argv[], int *keyp)
{
	struct ui_browser menu = {
		.entries    = (void *)argv,
		.refresh    = ui_browser__argv_refresh,
		.seek	    = ui_browser__argv_seek,
		.write	    = ui_browser__argv_write,
		.nr_entries = argc,
	};
	return popup_menu__run(&menu, keyp);
}

int ui_browser__input_window(const char *title, const char *text, char *input,
			     const char *exit_msg, int delay_secs)
{
	int x, y, len, key;
	int max_len = 60, nr_lines = 0;
	static char buf[50];
	const char *t;

	t = text;
	while (1) {
		const char *sep = strchr(t, '\n');

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

	pthread_mutex_lock(&ui__lock);

	max_len += 2;
	nr_lines += 8;
	y = SLtt_Screen_Rows / 2 - nr_lines / 2;
	x = SLtt_Screen_Cols / 2 - max_len / 2;

	SLsmg_set_color(0);
	SLsmg_draw_box(y, x++, nr_lines, max_len);
	if (title) {
		SLsmg_gotorc(y, x + 1);
		SLsmg_write_string((char *)title);
	}
	SLsmg_gotorc(++y, x);
	nr_lines -= 7;
	max_len -= 2;
	SLsmg_write_wrapped_string((unsigned char *)text, y, x,
				   nr_lines, max_len, 1);
	y += nr_lines;
	len = 5;
	while (len--) {
		SLsmg_gotorc(y + len - 1, x);
		SLsmg_write_nstring((char *)" ", max_len);
	}
	SLsmg_draw_box(y++, x + 1, 3, max_len - 2);

	SLsmg_gotorc(y + 3, x);
	SLsmg_write_nstring((char *)exit_msg, max_len);
	SLsmg_refresh();

	pthread_mutex_unlock(&ui__lock);

	x += 2;
	len = 0;
	key = ui__getch(delay_secs);
	while (key != K_TIMER && key != K_ENTER && key != K_ESC) {
		pthread_mutex_lock(&ui__lock);

		if (key == K_BKSPC) {
			if (len == 0) {
				pthread_mutex_unlock(&ui__lock);
				goto next_key;
			}
			SLsmg_gotorc(y, x + --len);
			SLsmg_write_char(' ');
		} else {
			buf[len] = key;
			SLsmg_gotorc(y, x + len++);
			SLsmg_write_char(key);
		}
		SLsmg_refresh();

		pthread_mutex_unlock(&ui__lock);

		/* XXX more graceful overflow handling needed */
		if (len == sizeof(buf) - 1) {
			ui_helpline__push("maximum size of symbol name reached!");
			key = K_ENTER;
			break;
		}
next_key:
		key = ui__getch(delay_secs);
	}

	buf[len] = '\0';
	strncpy(input, buf, len+1);
	return key;
}

void __ui__info_window(const char *title, const char *text, const char *exit_msg)
{
	int x, y;
	int max_len = 0, nr_lines = 0;
	const char *t;

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

	max_len += 2;
	nr_lines += 2;
	if (exit_msg)
		nr_lines += 2;
	y = SLtt_Screen_Rows / 2 - nr_lines / 2,
	x = SLtt_Screen_Cols / 2 - max_len / 2;

	SLsmg_set_color(0);
	SLsmg_draw_box(y, x++, nr_lines, max_len);
	if (title) {
		SLsmg_gotorc(y, x + 1);
		SLsmg_write_string((char *)title);
	}
	SLsmg_gotorc(++y, x);
	if (exit_msg)
		nr_lines -= 2;
	max_len -= 2;
	SLsmg_write_wrapped_string((unsigned char *)text, y, x,
				   nr_lines, max_len, 1);
	if (exit_msg) {
		SLsmg_gotorc(y + nr_lines - 2, x);
		SLsmg_write_nstring((char *)" ", max_len);
		SLsmg_gotorc(y + nr_lines - 1, x);
		SLsmg_write_nstring((char *)exit_msg, max_len);
	}
}

void ui__info_window(const char *title, const char *text)
{
	pthread_mutex_lock(&ui__lock);
	__ui__info_window(title, text, NULL);
	SLsmg_refresh();
	pthread_mutex_unlock(&ui__lock);
}

int ui__question_window(const char *title, const char *text,
			const char *exit_msg, int delay_secs)
{
	pthread_mutex_lock(&ui__lock);
	__ui__info_window(title, text, exit_msg);
	SLsmg_refresh();
	pthread_mutex_unlock(&ui__lock);
	return ui__getch(delay_secs);
}

int ui__help_window(const char *text)
{
	return ui__question_window("Help", text, "Press any key...", 0);
}

int ui__dialog_yesno(const char *msg)
{
	return ui__question_window(NULL, msg, "Enter: Yes, ESC: No", 0);
}

static int __ui__warning(const char *title, const char *format, va_list args)
{
	char *s;

	if (vasprintf(&s, format, args) > 0) {
		int key;

		key = ui__question_window(title, s, "Press any key...", 0);
		free(s);
		return key;
	}

	fprintf(stderr, "%s\n", title);
	vfprintf(stderr, format, args);
	return K_ESC;
}

static int perf_tui__error(const char *format, va_list args)
{
	return __ui__warning("Error:", format, args);
}

static int perf_tui__warning(const char *format, va_list args)
{
	return __ui__warning("Warning:", format, args);
}

struct perf_error_ops perf_tui_eops = {
	.error		= perf_tui__error,
	.warning	= perf_tui__warning,
};
