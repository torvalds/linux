// SPDX-License-Identifier: GPL-2.0
#include "ui/browser.h"
#include "ui/keysyms.h"
#include "ui/ui.h"
#include "ui/util.h"
#include "ui/libslang.h"
#include "util/header.h"
#include "util/session.h"

#include <sys/ttydefaults.h>

static void ui_browser__argv_write(struct ui_browser *browser,
				   void *entry, int row)
{
	char **arg = entry;
	char *str = *arg;
	char empty[] = " ";
	bool current_entry = ui_browser__is_current_entry(browser, row);
	unsigned long offset = (unsigned long)browser->priv;

	if (offset >= strlen(str))
		str = empty;
	else
		str = str + offset;

	ui_browser__set_color(browser, current_entry ? HE_COLORSET_SELECTED :
						       HE_COLORSET_NORMAL);

	ui_browser__write_nstring(browser, str, browser->width);
}

static int list_menu__run(struct ui_browser *menu)
{
	int key;
	unsigned long offset;
	static const char help[] =
	"h/?/F1        Show this window\n"
	"UP/DOWN/PGUP\n"
	"PGDN/SPACE\n"
	"LEFT/RIGHT    Navigate\n"
	"q/ESC/CTRL+C  Exit browser";

	if (ui_browser__show(menu, "Header information", "Press 'q' to exit") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(menu, 0);

		switch (key) {
		case K_RIGHT:
			offset = (unsigned long)menu->priv;
			offset += 10;
			menu->priv = (void *)offset;
			continue;
		case K_LEFT:
			offset = (unsigned long)menu->priv;
			if (offset >= 10)
				offset -= 10;
			menu->priv = (void *)offset;
			continue;
		case K_F1:
		case 'h':
		case '?':
			ui_browser__help_window(menu, help);
			continue;
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

static int ui__list_menu(int argc, char * const argv[])
{
	struct ui_browser menu = {
		.entries    = (void *)argv,
		.refresh    = ui_browser__argv_refresh,
		.seek	    = ui_browser__argv_seek,
		.write	    = ui_browser__argv_write,
		.nr_entries = argc,
	};

	return list_menu__run(&menu);
}

int tui__header_window(struct perf_env *env)
{
	int i, argc = 0;
	char **argv;
	struct perf_session *session;
	char *ptr, *pos;
	size_t size;
	FILE *fp = open_memstream(&ptr, &size);

	session = container_of(env, struct perf_session, header.env);
	perf_header__fprintf_info(session, fp, true);
	fclose(fp);

	for (pos = ptr, argc = 0; (pos = strchr(pos, '\n')) != NULL; pos++)
		argc++;

	argv = calloc(argc + 1, sizeof(*argv));
	if (argv == NULL)
		goto out;

	argv[0] = pos = ptr;
	for (i = 1; (pos = strchr(pos, '\n')) != NULL; i++) {
		*pos++ = '\0';
		argv[i] = pos;
	}

	BUG_ON(i != argc + 1);

	ui__list_menu(argc, argv);

out:
	free(argv);
	free(ptr);
	return 0;
}
