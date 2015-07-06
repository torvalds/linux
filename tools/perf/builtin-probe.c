/*
 * builtin-probe.c
 *
 * Builtin probe command: Set up probe events by C expression
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "perf.h"
#include "builtin.h"
#include "util/util.h"
#include "util/strlist.h"
#include "util/strfilter.h"
#include "util/symbol.h"
#include "util/debug.h"
#include <api/fs/debugfs.h>
#include "util/parse-options.h"
#include "util/probe-finder.h"
#include "util/probe-event.h"

#define DEFAULT_VAR_FILTER "!__k???tab_* & !__crc_*"
#define DEFAULT_FUNC_FILTER "!_*"
#define DEFAULT_LIST_FILTER "*:*"

/* Session management structure */
static struct {
	int command;	/* Command short_name */
	bool list_events;
	bool uprobes;
	bool quiet;
	bool target_used;
	int nevents;
	struct perf_probe_event events[MAX_PROBES];
	struct line_range line_range;
	char *target;
	struct strfilter *filter;
} params;

/* Parse an event definition. Note that any error must die. */
static int parse_probe_event(const char *str)
{
	struct perf_probe_event *pev = &params.events[params.nevents];
	int ret;

	pr_debug("probe-definition(%d): %s\n", params.nevents, str);
	if (++params.nevents == MAX_PROBES) {
		pr_err("Too many probes (> %d) were specified.", MAX_PROBES);
		return -1;
	}

	pev->uprobes = params.uprobes;
	if (params.target) {
		pev->target = strdup(params.target);
		if (!pev->target)
			return -ENOMEM;
		params.target_used = true;
	}

	/* Parse a perf-probe command into event */
	ret = parse_perf_probe_command(str, pev);
	pr_debug("%d arguments\n", pev->nargs);

	return ret;
}

static int params_add_filter(const char *str)
{
	const char *err = NULL;
	int ret = 0;

	pr_debug2("Add filter: %s\n", str);
	if (!params.filter) {
		params.filter = strfilter__new(str, &err);
		if (!params.filter)
			ret = err ? -EINVAL : -ENOMEM;
	} else
		ret = strfilter__or(params.filter, str, &err);

	if (ret == -EINVAL) {
		pr_err("Filter parse error at %td.\n", err - str + 1);
		pr_err("Source: \"%s\"\n", str);
		pr_err("         %*c\n", (int)(err - str + 1), '^');
	}

	return ret;
}

static int set_target(const char *ptr)
{
	int found = 0;
	const char *buf;

	/*
	 * The first argument after options can be an absolute path
	 * to an executable / library or kernel module.
	 *
	 * TODO: Support relative path, and $PATH, $LD_LIBRARY_PATH,
	 * short module name.
	 */
	if (!params.target && ptr && *ptr == '/') {
		params.target = strdup(ptr);
		if (!params.target)
			return -ENOMEM;
		params.target_used = false;

		found = 1;
		buf = ptr + (strlen(ptr) - 3);

		if (strcmp(buf, ".ko"))
			params.uprobes = true;

	}

	return found;
}

static int parse_probe_event_argv(int argc, const char **argv)
{
	int i, len, ret, found_target;
	char *buf;

	found_target = set_target(argv[0]);
	if (found_target < 0)
		return found_target;

	if (found_target && argc == 1)
		return 0;

	/* Bind up rest arguments */
	len = 0;
	for (i = 0; i < argc; i++) {
		if (i == 0 && found_target)
			continue;

		len += strlen(argv[i]) + 1;
	}
	buf = zalloc(len + 1);
	if (buf == NULL)
		return -ENOMEM;
	len = 0;
	for (i = 0; i < argc; i++) {
		if (i == 0 && found_target)
			continue;

		len += sprintf(&buf[len], "%s ", argv[i]);
	}
	ret = parse_probe_event(buf);
	free(buf);
	return ret;
}

static int opt_set_target(const struct option *opt, const char *str,
			int unset __maybe_unused)
{
	int ret = -ENOENT;
	char *tmp;

	if  (str) {
		if (!strcmp(opt->long_name, "exec"))
			params.uprobes = true;
#ifdef HAVE_DWARF_SUPPORT
		else if (!strcmp(opt->long_name, "module"))
			params.uprobes = false;
#endif
		else
			return ret;

		/* Expand given path to absolute path, except for modulename */
		if (params.uprobes || strchr(str, '/')) {
			tmp = realpath(str, NULL);
			if (!tmp) {
				pr_warning("Failed to get the absolute path of %s: %m\n", str);
				return ret;
			}
		} else {
			tmp = strdup(str);
			if (!tmp)
				return -ENOMEM;
		}
		free(params.target);
		params.target = tmp;
		params.target_used = false;
		ret = 0;
	}

	return ret;
}

/* Command option callbacks */

#ifdef HAVE_DWARF_SUPPORT
static int opt_show_lines(const struct option *opt,
			  const char *str, int unset __maybe_unused)
{
	int ret = 0;

	if (!str)
		return 0;

	if (params.command == 'L') {
		pr_warning("Warning: more than one --line options are"
			   " detected. Only the first one is valid.\n");
		return 0;
	}

	params.command = opt->short_name;
	ret = parse_line_range_desc(str, &params.line_range);

	return ret;
}

static int opt_show_vars(const struct option *opt,
			 const char *str, int unset __maybe_unused)
{
	struct perf_probe_event *pev = &params.events[params.nevents];
	int ret;

	if (!str)
		return 0;

	ret = parse_probe_event(str);
	if (!ret && pev->nargs != 0) {
		pr_err("  Error: '--vars' doesn't accept arguments.\n");
		return -EINVAL;
	}
	params.command = opt->short_name;

	return ret;
}
#endif
static int opt_add_probe_event(const struct option *opt,
			      const char *str, int unset __maybe_unused)
{
	if (str) {
		params.command = opt->short_name;
		return parse_probe_event(str);
	}

	return 0;
}

static int opt_set_filter_with_command(const struct option *opt,
				       const char *str, int unset)
{
	if (!unset)
		params.command = opt->short_name;

	if (str)
		return params_add_filter(str);

	return 0;
}

static int opt_set_filter(const struct option *opt __maybe_unused,
			  const char *str, int unset __maybe_unused)
{
	if (str)
		return params_add_filter(str);

	return 0;
}

static int init_params(void)
{
	return line_range__init(&params.line_range);
}

static void cleanup_params(void)
{
	int i;

	for (i = 0; i < params.nevents; i++)
		clear_perf_probe_event(params.events + i);
	line_range__clear(&params.line_range);
	free(params.target);
	if (params.filter)
		strfilter__delete(params.filter);
	memset(&params, 0, sizeof(params));
}

static void pr_err_with_code(const char *msg, int err)
{
	char sbuf[STRERR_BUFSIZE];

	pr_err("%s", msg);
	pr_debug(" Reason: %s (Code: %d)",
		 strerror_r(-err, sbuf, sizeof(sbuf)), err);
	pr_err("\n");
}

static int
__cmd_probe(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const probe_usage[] = {
		"perf probe [<options>] 'PROBEDEF' ['PROBEDEF' ...]",
		"perf probe [<options>] --add 'PROBEDEF' [--add 'PROBEDEF' ...]",
		"perf probe [<options>] --del '[GROUP:]EVENT' ...",
		"perf probe --list [GROUP:]EVENT ...",
#ifdef HAVE_DWARF_SUPPORT
		"perf probe [<options>] --line 'LINEDESC'",
		"perf probe [<options>] --vars 'PROBEPOINT'",
#endif
		"perf probe [<options>] --funcs",
		NULL
	};
	struct option options[] = {
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show parsed arguments, etc)"),
	OPT_BOOLEAN('q', "quiet", &params.quiet,
		    "be quiet (do not show any mesages)"),
	OPT_CALLBACK_DEFAULT('l', "list", NULL, "[GROUP:]EVENT",
			     "list up probe events",
			     opt_set_filter_with_command, DEFAULT_LIST_FILTER),
	OPT_CALLBACK('d', "del", NULL, "[GROUP:]EVENT", "delete a probe event.",
		     opt_set_filter_with_command),
	OPT_CALLBACK('a', "add", NULL,
#ifdef HAVE_DWARF_SUPPORT
		"[EVENT=]FUNC[@SRC][+OFF|%return|:RL|;PT]|SRC:AL|SRC;PT"
		" [[NAME=]ARG ...]",
#else
		"[EVENT=]FUNC[+OFF|%return] [[NAME=]ARG ...]",
#endif
		"probe point definition, where\n"
		"\t\tGROUP:\tGroup name (optional)\n"
		"\t\tEVENT:\tEvent name\n"
		"\t\tFUNC:\tFunction name\n"
		"\t\tOFF:\tOffset from function entry (in byte)\n"
		"\t\t%return:\tPut the probe at function return\n"
#ifdef HAVE_DWARF_SUPPORT
		"\t\tSRC:\tSource code path\n"
		"\t\tRL:\tRelative line number from function entry.\n"
		"\t\tAL:\tAbsolute line number in file.\n"
		"\t\tPT:\tLazy expression of line code.\n"
		"\t\tARG:\tProbe argument (local variable name or\n"
		"\t\t\tkprobe-tracer argument format.)\n",
#else
		"\t\tARG:\tProbe argument (kprobe-tracer argument format.)\n",
#endif
		opt_add_probe_event),
	OPT_BOOLEAN('f', "force", &probe_conf.force_add, "forcibly add events"
		    " with existing name"),
#ifdef HAVE_DWARF_SUPPORT
	OPT_CALLBACK('L', "line", NULL,
		     "FUNC[:RLN[+NUM|-RLN2]]|SRC:ALN[+NUM|-ALN2]",
		     "Show source code lines.", opt_show_lines),
	OPT_CALLBACK('V', "vars", NULL,
		     "FUNC[@SRC][+OFF|%return|:RL|;PT]|SRC:AL|SRC;PT",
		     "Show accessible variables on PROBEDEF", opt_show_vars),
	OPT_BOOLEAN('\0', "externs", &probe_conf.show_ext_vars,
		    "Show external variables too (with --vars only)"),
	OPT_BOOLEAN('\0', "range", &probe_conf.show_location_range,
		"Show variables location range in scope (with --vars only)"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING('s', "source", &symbol_conf.source_prefix,
		   "directory", "path to kernel source"),
	OPT_CALLBACK('m', "module", NULL, "modname|path",
		"target module name (for online) or path (for offline)",
		opt_set_target),
	OPT_BOOLEAN('\0', "no-inlines", &probe_conf.no_inlines,
		"Don't search inlined functions"),
#endif
	OPT__DRY_RUN(&probe_event_dry_run),
	OPT_INTEGER('\0', "max-probes", &probe_conf.max_probes,
		 "Set how many probe points can be found for a probe."),
	OPT_CALLBACK_DEFAULT('F', "funcs", NULL, "[FILTER]",
			     "Show potential probe-able functions.",
			     opt_set_filter_with_command, DEFAULT_FUNC_FILTER),
	OPT_CALLBACK('\0', "filter", NULL,
		     "[!]FILTER", "Set a filter (with --vars/funcs only)\n"
		     "\t\t\t(default: \"" DEFAULT_VAR_FILTER "\" for --vars,\n"
		     "\t\t\t \"" DEFAULT_FUNC_FILTER "\" for --funcs)",
		     opt_set_filter),
	OPT_CALLBACK('x', "exec", NULL, "executable|path",
			"target executable name or path", opt_set_target),
	OPT_BOOLEAN(0, "demangle", &symbol_conf.demangle,
		    "Enable symbol demangling"),
	OPT_BOOLEAN(0, "demangle-kernel", &symbol_conf.demangle_kernel,
		    "Enable kernel symbol demangling"),
	OPT_END()
	};
	int ret;

	set_option_flag(options, 'a', "add", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 'd', "del", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 'l', "list", PARSE_OPT_EXCLUSIVE);
#ifdef HAVE_DWARF_SUPPORT
	set_option_flag(options, 'L', "line", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 'V', "vars", PARSE_OPT_EXCLUSIVE);
#endif
	set_option_flag(options, 'F', "funcs", PARSE_OPT_EXCLUSIVE);

	argc = parse_options(argc, argv, options, probe_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc > 0) {
		if (strcmp(argv[0], "-") == 0) {
			pr_warning("  Error: '-' is not supported.\n");
			usage_with_options(probe_usage, options);
		}
		if (params.command && params.command != 'a') {
			pr_warning("  Error: another command except --add is set.\n");
			usage_with_options(probe_usage, options);
		}
		ret = parse_probe_event_argv(argc, argv);
		if (ret < 0) {
			pr_err_with_code("  Error: Command Parse Error.", ret);
			return ret;
		}
		params.command = 'a';
	}

	if (params.quiet) {
		if (verbose != 0) {
			pr_err("  Error: -v and -q are exclusive.\n");
			return -EINVAL;
		}
		verbose = -1;
	}

	if (probe_conf.max_probes == 0)
		probe_conf.max_probes = MAX_PROBES;

	/*
	 * Only consider the user's kernel image path if given.
	 */
	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);

	switch (params.command) {
	case 'l':
		if (params.uprobes) {
			pr_warning("  Error: Don't use --list with --exec.\n");
			usage_with_options(probe_usage, options);
		}
		ret = show_perf_probe_events(params.filter);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show event list.", ret);
		return ret;
	case 'F':
		ret = show_available_funcs(params.target, params.filter,
					params.uprobes);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show functions.", ret);
		return ret;
#ifdef HAVE_DWARF_SUPPORT
	case 'L':
		ret = show_line_range(&params.line_range, params.target,
				      params.uprobes);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show lines.", ret);
		return ret;
	case 'V':
		if (!params.filter)
			params.filter = strfilter__new(DEFAULT_VAR_FILTER,
						       NULL);

		ret = show_available_vars(params.events, params.nevents,
					  params.filter);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show vars.", ret);
		return ret;
#endif
	case 'd':
		ret = del_perf_probe_events(params.filter);
		if (ret < 0) {
			pr_err_with_code("  Error: Failed to delete events.", ret);
			return ret;
		}
		break;
	case 'a':
		/* Ensure the last given target is used */
		if (params.target && !params.target_used) {
			pr_warning("  Error: -x/-m must follow the probe definitions.\n");
			usage_with_options(probe_usage, options);
		}

		ret = add_perf_probe_events(params.events, params.nevents);
		if (ret < 0) {
			pr_err_with_code("  Error: Failed to add events.", ret);
			return ret;
		}
		break;
	default:
		usage_with_options(probe_usage, options);
	}
	return 0;
}

int cmd_probe(int argc, const char **argv, const char *prefix)
{
	int ret;

	ret = init_params();
	if (!ret) {
		ret = __cmd_probe(argc, argv, prefix);
		cleanup_params();
	}

	return ret < 0 ? ret : 0;
}
