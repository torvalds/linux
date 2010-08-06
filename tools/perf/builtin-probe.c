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
#define _GNU_SOURCE
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#undef _GNU_SOURCE
#include "perf.h"
#include "builtin.h"
#include "util/util.h"
#include "util/strlist.h"
#include "util/symbol.h"
#include "util/debug.h"
#include "util/debugfs.h"
#include "util/parse-options.h"
#include "util/probe-finder.h"
#include "util/probe-event.h"

#define MAX_PATH_LEN 256

/* Session management structure */
static struct {
	bool list_events;
	bool force_add;
	bool show_lines;
	int nevents;
	struct perf_probe_event events[MAX_PROBES];
	struct strlist *dellist;
	struct line_range line_range;
	int max_probe_points;
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

	/* Parse a perf-probe command into event */
	ret = parse_perf_probe_command(str, pev);
	pr_debug("%d arguments\n", pev->nargs);

	return ret;
}

static int parse_probe_event_argv(int argc, const char **argv)
{
	int i, len, ret;
	char *buf;

	/* Bind up rest arguments */
	len = 0;
	for (i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	buf = zalloc(len + 1);
	if (buf == NULL)
		return -ENOMEM;
	len = 0;
	for (i = 0; i < argc; i++)
		len += sprintf(&buf[len], "%s ", argv[i]);
	ret = parse_probe_event(buf);
	free(buf);
	return ret;
}

static int opt_add_probe_event(const struct option *opt __used,
			      const char *str, int unset __used)
{
	if (str)
		return parse_probe_event(str);
	else
		return 0;
}

static int opt_del_probe_event(const struct option *opt __used,
			       const char *str, int unset __used)
{
	if (str) {
		if (!params.dellist)
			params.dellist = strlist__new(true, NULL);
		strlist__add(params.dellist, str);
	}
	return 0;
}

#ifdef DWARF_SUPPORT
static int opt_show_lines(const struct option *opt __used,
			  const char *str, int unset __used)
{
	int ret = 0;

	if (str)
		ret = parse_line_range_desc(str, &params.line_range);
	INIT_LIST_HEAD(&params.line_range.line_list);
	params.show_lines = true;

	return ret;
}
#endif

static const char * const probe_usage[] = {
	"perf probe [<options>] 'PROBEDEF' ['PROBEDEF' ...]",
	"perf probe [<options>] --add 'PROBEDEF' [--add 'PROBEDEF' ...]",
	"perf probe [<options>] --del '[GROUP:]EVENT' ...",
	"perf probe --list",
#ifdef DWARF_SUPPORT
	"perf probe --line 'LINEDESC'",
#endif
	NULL
};

static const struct option options[] = {
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show parsed arguments, etc)"),
	OPT_BOOLEAN('l', "list", &params.list_events,
		    "list up current probe events"),
	OPT_CALLBACK('d', "del", NULL, "[GROUP:]EVENT", "delete a probe event.",
		opt_del_probe_event),
	OPT_CALLBACK('a', "add", NULL,
#ifdef DWARF_SUPPORT
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
#ifdef DWARF_SUPPORT
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
	OPT_BOOLEAN('f', "force", &params.force_add, "forcibly add events"
		    " with existing name"),
#ifdef DWARF_SUPPORT
	OPT_CALLBACK('L', "line", NULL,
		     "FUNC[:RLN[+NUM|-RLN2]]|SRC:ALN[+NUM|-ALN2]",
		     "Show source code lines.", opt_show_lines),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
#endif
	OPT__DRY_RUN(&probe_event_dry_run),
	OPT_INTEGER('\0', "max-probes", &params.max_probe_points,
		 "Set how many probe points can be found for a probe."),
	OPT_END()
};

int cmd_probe(int argc, const char **argv, const char *prefix __used)
{
	int ret;

	argc = parse_options(argc, argv, options, probe_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc > 0) {
		if (strcmp(argv[0], "-") == 0) {
			pr_warning("  Error: '-' is not supported.\n");
			usage_with_options(probe_usage, options);
		}
		ret = parse_probe_event_argv(argc, argv);
		if (ret < 0) {
			pr_err("  Error: Parse Error.  (%d)\n", ret);
			return ret;
		}
	}

	if (params.max_probe_points == 0)
		params.max_probe_points = MAX_PROBES;

	if ((!params.nevents && !params.dellist && !params.list_events &&
	     !params.show_lines))
		usage_with_options(probe_usage, options);

	if (params.list_events) {
		if (params.nevents != 0 || params.dellist) {
			pr_err("  Error: Don't use --list with --add/--del.\n");
			usage_with_options(probe_usage, options);
		}
		if (params.show_lines) {
			pr_err("  Error: Don't use --list with --line.\n");
			usage_with_options(probe_usage, options);
		}
		ret = show_perf_probe_events();
		if (ret < 0)
			pr_err("  Error: Failed to show event list. (%d)\n",
			       ret);
		return ret;
	}

#ifdef DWARF_SUPPORT
	if (params.show_lines) {
		if (params.nevents != 0 || params.dellist) {
			pr_warning("  Error: Don't use --line with"
				   " --add/--del.\n");
			usage_with_options(probe_usage, options);
		}

		ret = show_line_range(&params.line_range);
		if (ret < 0)
			pr_err("  Error: Failed to show lines. (%d)\n", ret);
		return ret;
	}
#endif

	if (params.dellist) {
		ret = del_perf_probe_events(params.dellist);
		strlist__delete(params.dellist);
		if (ret < 0) {
			pr_err("  Error: Failed to delete events. (%d)\n", ret);
			return ret;
		}
	}

	if (params.nevents) {
		ret = add_perf_probe_events(params.events, params.nevents,
					    params.force_add,
					    params.max_probe_points);
		if (ret < 0) {
			pr_err("  Error: Failed to add events. (%d)\n", ret);
			return ret;
		}
	}
	return 0;
}

