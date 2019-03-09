// SPDX-License-Identifier: GPL-2.0
#include "../../util/sort.h"
#include "../../util/util.h"
#include "../../util/hist.h"
#include "../../util/debug.h"
#include "../../util/symbol.h"
#include "../browser.h"
#include "../libslang.h"

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

static void run_script(char *cmd)
{
	pr_debug("Running %s\n", cmd);
	SLang_reset_tty();
	if (system(cmd) < 0)
		pr_warning("Cannot run %s\n", cmd);
	/*
	 * SLang doesn't seem to reset the whole terminal, so be more
	 * forceful to get back to the original state.
	 */
	printf("\033[c\033[H\033[J");
	fflush(stdout);
	SLang_init_tty(0, 0, 0);
	SLsmg_refresh();
}

int script_browse(const char *script_opt)
{
	char cmd[SCRIPT_FULLPATH_LEN*2], script_name[SCRIPT_FULLPATH_LEN];

	memset(script_name, 0, SCRIPT_FULLPATH_LEN);
	if (list_scripts(script_name))
		return -1;

	sprintf(cmd, "perf script -s %s ", script_name);

	if (script_opt)
		strcat(cmd, script_opt);

	if (input_name) {
		strcat(cmd, " -i ");
		strcat(cmd, input_name);
	}

	strcat(cmd, " 2>&1 | less");

	run_script(cmd);

	return 0;
}
