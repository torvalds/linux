// SPDX-License-Identifier: GPL-2.0
#include "../../util/util.h" // perf_exe()
#include "../util.h"
#include "../../util/evlist.h"
#include "../../util/hist.h"
#include "../../util/debug.h"
#include "../../util/session.h"
#include "../../util/symbol.h"
#include "../browser.h"
#include "../libslang.h"
#include "config.h"
#include <linux/err.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <subcmd/exec-cmd.h>
#include <stdlib.h>

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
 * Some scripts specify the required events in their "xxx-record" file,
 * this function will check if the events in perf.data match those
 * mentioned in the "xxx-record".
 *
 * Fixme: All existing "xxx-record" are all in good formats "-e event ",
 * which is covered well now. And new parsing code should be added to
 * cover the future complex formats like event groups etc.
 */
static int check_ev_match(int dir_fd, const char *scriptname, struct perf_session *session)
{
	char line[BUFSIZ];
	FILE *fp;

	{
		char filename[NAME_MAX + 5];
		int fd;

		scnprintf(filename, sizeof(filename), "bin/%s-record", scriptname);
		fd = openat(dir_fd, filename, O_RDONLY);
		if (fd == -1)
			return -1;
		fp = fdopen(fd, "r");
		if (!fp)
			return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *p = skip_spaces(line);

		if (*p == '#')
			continue;

		while (strlen(p)) {
			int match, len;
			struct evsel *pos;
			char evname[128];

			p = strstr(p, "-e");
			if (!p)
				break;

			p += 2;
			p = skip_spaces(p);
			len = strcspn(p, " \t");
			if (!len)
				break;

			snprintf(evname, len + 1, "%s", p);

			match = 0;
			evlist__for_each_entry(session->evlist, pos) {
				if (evsel__name_is(pos, evname)) {
					match = 1;
					break;
				}
			}

			if (!match) {
				fclose(fp);
				return -1;
			}
		}
	}

	fclose(fp);
	return 0;
}

/*
 * Return -1 if none is found, otherwise the actual scripts number.
 *
 * Currently the only user of this function is the script browser, which
 * will list all statically runnable scripts, select one, execute it and
 * show the output in a perf browser.
 */
static int find_scripts(char **scripts_array, char **scripts_path_array, int num,
		 int pathlen)
{
	struct dirent *script_dirent, *lang_dirent;
	int scripts_dir_fd, lang_dir_fd;
	DIR *scripts_dir, *lang_dir;
	struct perf_session *session;
	struct perf_data data = {
		.path = input_name,
		.mode = PERF_DATA_MODE_READ,
	};
	char *temp;
	int i = 0;
	const char *exec_path = get_argv_exec_path();

	session = perf_session__new(&data, NULL);
	if (IS_ERR(session))
		return PTR_ERR(session);

	{
		char scripts_path[PATH_MAX];

		snprintf(scripts_path, sizeof(scripts_path), "%s/scripts", exec_path);
		scripts_dir_fd = open(scripts_path, O_DIRECTORY);
		pr_err("Failed to open directory '%s'", scripts_path);
		if (scripts_dir_fd == -1) {
			perf_session__delete(session);
			return -1;
		}
	}
	scripts_dir = fdopendir(scripts_dir_fd);
	if (!scripts_dir) {
		close(scripts_dir_fd);
		perf_session__delete(session);
		return -1;
	}

	while ((lang_dirent = readdir(scripts_dir)) != NULL) {
		if (lang_dirent->d_type != DT_DIR &&
		    (lang_dirent->d_type == DT_UNKNOWN &&
		     !is_directory_at(scripts_dir_fd, lang_dirent->d_name)))
			continue;
		if (!strcmp(lang_dirent->d_name, ".") || !strcmp(lang_dirent->d_name, ".."))
			continue;

#ifndef HAVE_LIBPERL_SUPPORT
		if (strstr(lang_dirent->d_name, "perl"))
			continue;
#endif
#ifndef HAVE_LIBPYTHON_SUPPORT
		if (strstr(lang_dirent->d_name, "python"))
			continue;
#endif

		lang_dir_fd = openat(scripts_dir_fd, lang_dirent->d_name, O_DIRECTORY);
		if (lang_dir_fd == -1)
			continue;
		lang_dir = fdopendir(lang_dir_fd);
		if (!lang_dir) {
			close(lang_dir_fd);
			continue;
		}
		while ((script_dirent = readdir(lang_dir)) != NULL) {
			if (script_dirent->d_type == DT_DIR)
				continue;
			if (script_dirent->d_type == DT_UNKNOWN &&
			    is_directory_at(lang_dir_fd, script_dirent->d_name))
				continue;
			/* Skip those real time scripts: xxxtop.p[yl] */
			if (strstr(script_dirent->d_name, "top."))
				continue;
			if (i >= num)
				break;
			scnprintf(scripts_path_array[i], pathlen, "%s/scripts/%s/%s",
				exec_path,
				lang_dirent->d_name,
				script_dirent->d_name);
			temp = strchr(script_dirent->d_name, '.');
			snprintf(scripts_array[i],
				(temp - script_dirent->d_name) + 1,
				"%s", script_dirent->d_name);

			if (check_ev_match(lang_dir_fd, scripts_array[i], session))
				continue;

			i++;
		}
		closedir(lang_dir);
	}

	closedir(scripts_dir);
	perf_session__delete(session);
	return i;
}

/*
 * When success, will copy the full path of the selected script
 * into  the buffer pointed by script_name, and return 0.
 * Return -1 on failure.
 */
static int list_scripts(char *script_name, bool *custom,
			struct evsel *evsel)
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
		attr_to_script(scriptc.extra_format, &evsel->core.attr);
	add_script_option("Show individual samples", "", &scriptc);
	add_script_option("Show individual samples with assembler", "-F +disasm",
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
	choice = ui__popup_menu(num + max_std, (char * const *)names, NULL);
	if (choice < 0) {
		ret = -1;
		goto out;
	}
	if (choice == custom_perf) {
		char script_args[50];
		int key = ui_browser__input_window("perf script command",
				"Enter perf script command line (without perf script prefix)",
				script_args, "", 0);
		if (key != K_ENTER) {
			ret = -1;
			goto out;
		}
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
	SLtty_set_suspend_state(true);
	SLsmg_refresh();
}

int script_browse(const char *script_opt, struct evsel *evsel)
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
