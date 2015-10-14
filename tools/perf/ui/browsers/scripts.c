#include <elf.h>
#include <inttypes.h>
#include <sys/ttydefaults.h>
#include <string.h>
#include "../../util/sort.h"
#include "../../util/util.h"
#include "../../util/hist.h"
#include "../../util/debug.h"
#include "../../util/symbol.h"
#include "../browser.h"
#include "../helpline.h"
#include "../libslang.h"

/* 2048 lines should be enough for a script output */
#define MAX_LINES		2048

/* 160 bytes for one output line */
#define AVERAGE_LINE_LEN	160

struct script_line {
	struct list_head node;
	char line[AVERAGE_LINE_LEN];
};

struct perf_script_browser {
	struct ui_browser b;
	struct list_head entries;
	const char *script_name;
	int nr_lines;
};

#define SCRIPT_NAMELEN	128
#define SCRIPT_MAX_NO	64
/*
 * Usually the full path for a script is:
 *	/home/username/libexec/perf-core/scripts/python/xxx.py
 *	/home/username/libexec/perf-core/scripts/perl/xxx.pl
 * So 256 should be long enough to contain the full path.
 */
#define SCRIPT_FULLPATH_LEN	256

/*
 * When success, will copy the full path of the selected script
 * into  the buffer pointed by script_name, and return 0.
 * Return -1 on failure.
 */
static int list_scripts(char *script_name)
{
	char *buf, *names[SCRIPT_MAX_NO], *paths[SCRIPT_MAX_NO];
	int i, num, choice, ret = -1;

	/* Preset the script name to SCRIPT_NAMELEN */
	buf = malloc(SCRIPT_MAX_NO * (SCRIPT_NAMELEN + SCRIPT_FULLPATH_LEN));
	if (!buf)
		return ret;

	for (i = 0; i < SCRIPT_MAX_NO; i++) {
		names[i] = buf + i * (SCRIPT_NAMELEN + SCRIPT_FULLPATH_LEN);
		paths[i] = names[i] + SCRIPT_NAMELEN;
	}

	num = find_scripts(names, paths);
	if (num > 0) {
		choice = ui__popup_menu(num, names);
		if (choice < num && choice >= 0) {
			strcpy(script_name, paths[choice]);
			ret = 0;
		}
	}

	free(buf);
	return ret;
}

static void script_browser__write(struct ui_browser *browser,
				   void *entry, int row)
{
	struct script_line *sline = list_entry(entry, struct script_line, node);
	bool current_entry = ui_browser__is_current_entry(browser, row);

	ui_browser__set_color(browser, current_entry ? HE_COLORSET_SELECTED :
						       HE_COLORSET_NORMAL);

	ui_browser__write_nstring(browser, sline->line, browser->width);
}

static int script_browser__run(struct perf_script_browser *browser)
{
	int key;

	if (ui_browser__show(&browser->b, browser->script_name,
			     "Press <- or ESC to exit") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(&browser->b, 0);

		/* We can add some special key handling here if needed */
		break;
	}

	ui_browser__hide(&browser->b);
	return key;
}


int script_browse(const char *script_opt)
{
	char cmd[SCRIPT_FULLPATH_LEN*2], script_name[SCRIPT_FULLPATH_LEN];
	char *line = NULL;
	size_t len = 0;
	ssize_t retlen;
	int ret = -1, nr_entries = 0;
	FILE *fp;
	void *buf;
	struct script_line *sline;

	struct perf_script_browser script = {
		.b = {
			.refresh    = ui_browser__list_head_refresh,
			.seek	    = ui_browser__list_head_seek,
			.write	    = script_browser__write,
		},
		.script_name = script_name,
	};

	INIT_LIST_HEAD(&script.entries);

	/* Save each line of the output in one struct script_line object. */
	buf = zalloc((sizeof(*sline)) * MAX_LINES);
	if (!buf)
		return -1;
	sline = buf;

	memset(script_name, 0, SCRIPT_FULLPATH_LEN);
	if (list_scripts(script_name))
		goto exit;

	sprintf(cmd, "perf script -s %s ", script_name);

	if (script_opt)
		strcat(cmd, script_opt);

	if (input_name) {
		strcat(cmd, " -i ");
		strcat(cmd, input_name);
	}

	strcat(cmd, " 2>&1");

	fp = popen(cmd, "r");
	if (!fp)
		goto exit;

	while ((retlen = getline(&line, &len, fp)) != -1) {
		strncpy(sline->line, line, AVERAGE_LINE_LEN);

		/* If one output line is very large, just cut it short */
		if (retlen >= AVERAGE_LINE_LEN) {
			sline->line[AVERAGE_LINE_LEN - 1] = '\0';
			sline->line[AVERAGE_LINE_LEN - 2] = '\n';
		}
		list_add_tail(&sline->node, &script.entries);

		if (script.b.width < retlen)
			script.b.width = retlen;

		if (nr_entries++ >= MAX_LINES - 1)
			break;
		sline++;
	}

	if (script.b.width > AVERAGE_LINE_LEN)
		script.b.width = AVERAGE_LINE_LEN;

	free(line);
	pclose(fp);

	script.nr_lines = nr_entries;
	script.b.nr_entries = nr_entries;
	script.b.entries = &script.entries;

	ret = script_browser__run(&script);
exit:
	free(buf);
	return ret;
}
