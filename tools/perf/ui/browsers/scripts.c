// SPDX-License-Identifier: GPL-2.0
#include "../../util/sort.h"
#include "../../util/hist.h"
#include "../../util/debug.h"
#include "../../util/symbol.h"
#include "../browser.h"
#include "../libslang.h"
#include "config.h"
#include <linux/zalloc.h>

#define SCRIPT_NAMELEN	128
#define SCRIPT_MAX_NO	64
/*
 * Usually the full path for a script is:
 *	/home/username/libexec/perf-core/scripts/python/xxx.py
 *	/home/username/libexec/perf-core/scripts/perl/xxx.pl
 * So 256 should be long enough to contain the full path.
 */
#define SCRIPT_FULLPATH_LEN	256

struct script_config {
	const char **names;
	char **paths;
	int index;
	const char *perf;
	char extra_format[256];
};

void attr_to_script(char *extra_format, struct perf_event_attr *attr)
{
	extra_format[0] = 0;
	if (attr->read_format & PERF_FORMAT_GROUP)
		strcat(extra_format, " -F +metric");
	if (attr->sample_type & PERF_SAMPLE_BRANCH_STACK)
		strcat(extra_format, " -F +brstackinsn --xed");
	if (attr->sample_type & PERF_SAMPLE_REGS_INTR)
		strcat(extra_format, " -F +iregs");
	if (attr->sample_type & PERF_SAMPLE_REGS_USER)
		strcat(extra_format, " -F +uregs");
	if (attr->sample_type & PERF_SAMPLE_PHYS_ADDR)
		strcat(extra_format, " -F +phys_addr");
}

static int add_script_option(const char *name, const char *opt,
			     struct script_config *c)
{
	c->names[c->index] = name;
	if (asprintf(&c->paths[c->index],
		     "%s script %s -F +metric %s %s",
		     c->perf, opt, symbol_conf.inline_name ? " --inline" : "",
		     c->extra_format) < 0)
		return -1;
	c->index++;
	return 0;
}

static int scripts_config(const char *var, const char *value, void *data)
{
	struct script_config *c = data;

	if (!strstarts(var, "scripts."))
		return -1;
	if (c->index >= SCRIPT_MAX_NO)
		return -1;
	c->names[c->index] = strdup(var + 7);
	if (!c->names[c->index])
		return -1;
	if (asprintf(&c->paths[c->index], "%s %s", value,
		     c->extra_format) < 0)
		return -1;
	c->index++;
	return 0;
}

/*
 * When success, will copy the full path of the selected script
 * into  the buffer pointed by script_name, and return 0.
 * Return -1 on failure.
 */
static int list_scripts(char *script_name, bool *custom,
			struct perf_evsel *evsel)
{
	char *buf, *paths[SCRIPT_MAX_NO], *names[SCRIPT_MAX_NO];
	int i, num, choice;
	int ret = 0;
	int max_std, custom_perf;
	char pbuf[256];
	const char *perf = perf_exe(pbuf, sizeof pbuf);
	struct script_config scriptc = {
		.names = (const char **)names,
		.paths = paths,
		.perf = perf
	};

	script_name[0] = 0;

	/* Preset the script name to SCRIPT_NAMELEN */
	buf = malloc(SCRIPT_MAX_NO * (SCRIPT_NAMELEN + SCRIPT_FULLPATH_LEN));
	if (!buf)
		return -1;

	if (evsel)
		attr_to_script(scriptc.extra_format, &evsel->attr);
	add_script_option("Show individual samples", "", &scriptc);
	add_script_option("Show individual samples with assembler", "-F +insn --xed",
			  &scriptc);
	add_script_option("Show individual samples with source", "-F +srcline,+srccode",
			  &scriptc);
	perf_config(scripts_config, &scriptc);
	custom_perf = scriptc.index;
	add_script_option("Show samples with custom perf script arguments", "", &scriptc);
	i = scriptc.index;
	max_std = i;

	for (; i < SCRIPT_MAX_NO; i++) {
		names[i] = buf + (i - max_std) * (SCRIPT_NAMELEN + SCRIPT_FULLPATH_LEN);
		paths[i] = names[i] + SCRIPT_NAMELEN;
	}

	num = find_scripts(names + max_std, paths + max_std, SCRIPT_MAX_NO - max_std,
			SCRIPT_FULLPATH_LEN);
	if (num < 0)
		num = 0;
	choice = ui__popup_menu(num + max_std, (char * const *)names);
	if (choice < 0) {
		ret = -1;
		goto out;
	}
	if (choice == custom_perf) {
		char script_args[50];
		int key = ui_browser__input_window("perf script command",
				"Enter perf script command line (without perf script prefix)",
				script_args, "", 0);
		if (key != K_ENTER)
			return -1;
		sprintf(script_name, "%s script %s", perf, script_args);
	} else if (choice < num + max_std) {
		strcpy(script_name, paths[choice]);
	}
	*custom = choice >= max_std;

out:
	free(buf);
	for (i = 0; i < max_std; i++)
		zfree(&paths[i]);
	return ret;
}

void run_script(char *cmd)
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

int script_browse(const char *script_opt, struct perf_evsel *evsel)
{
	char *cmd, script_name[SCRIPT_FULLPATH_LEN];
	bool custom = false;

	memset(script_name, 0, SCRIPT_FULLPATH_LEN);
	if (list_scripts(script_name, &custom, evsel))
		return -1;

	if (asprintf(&cmd, "%s%s %s %s%s 2>&1 | less",
			custom ? "perf script -s " : "",
			script_name,
			script_opt ? script_opt : "",
			input_name ? "-i " : "",
			input_name ? input_name : "") < 0)
		return -1;

	run_script(cmd);
	free(cmd);

	return 0;
}
