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
#include "util/parse-events.h"	/* For debugfs_path */
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
} params;


/* Parse an event definition. Note that any error must die. */
static void parse_probe_event(const char *str)
{
	struct perf_probe_event *pev = &params.events[params.nevents];

	pr_debug("probe-definition(%d): %s\n", params.nevents, str);
	if (++params.nevents == MAX_PROBES)
		die("Too many probes (> %d) are specified.", MAX_PROBES);

	/* Parse a perf-probe command into event */
	parse_perf_probe_command(str, pev);

	pr_debug("%d arguments\n", pev->nargs);
}

static void parse_probe_event_argv(int argc, const char **argv)
{
	int i, len;
	char *buf;

	/* Bind up rest arguments */
	len = 0;
	for (i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	buf = xzalloc(len + 1);
	len = 0;
	for (i = 0; i < argc; i++)
		len += sprintf(&buf[len], "%s ", argv[i]);
	parse_probe_event(buf);
	free(buf);
}

static int opt_add_probe_event(const struct option *opt __used,
			      const char *str, int unset __used)
{
	if (str)
		parse_probe_event(str);
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

#ifndef NO_DWARF_SUPPORT
static int opt_show_lines(const struct option *opt __used,
			  const char *str, int unset __used)
{
	if (str)
		parse_line_range_desc(str, &params.line_range);
	INIT_LIST_HEAD(&params.line_range.line_list);
	params.show_lines = true;
	return 0;
}
#endif

static const char * const probe_usage[] = {
	"perf probe [<options>] 'PROBEDEF' ['PROBEDEF' ...]",
	"perf probe [<options>] --add 'PROBEDEF' [--add 'PROBEDEF' ...]",
	"perf probe [<options>] --del '[GROUP:]EVENT' ...",
	"perf probe --list",
#ifndef NO_DWARF_SUPPORT
	"perf probe --line 'LINEDESC'",
#endif
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show parsed arguments, etc)"),
#ifndef NO_DWARF_SUPPORT
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
#endif
	OPT_BOOLEAN('l', "list", &params.list_events,
		    "list up current probe events"),
	OPT_CALLBACK('d', "del", NULL, "[GROUP:]EVENT", "delete a probe event.",
		opt_del_probe_event),
	OPT_CALLBACK('a', "add", NULL,
#ifdef NO_DWARF_SUPPORT
		"[EVENT=]FUNC[+OFF|%return] [ARG ...]",
#else
		"[EVENT=]FUNC[@SRC][+OFF|%return|:RL|;PT]|SRC:AL|SRC;PT"
		" [ARG ...]",
#endif
		"probe point definition, where\n"
		"\t\tGROUP:\tGroup name (optional)\n"
		"\t\tEVENT:\tEvent name\n"
		"\t\tFUNC:\tFunction name\n"
		"\t\tOFF:\tOffset from function entry (in byte)\n"
		"\t\t%return:\tPut the probe at function return\n"
#ifdef NO_DWARF_SUPPORT
		"\t\tARG:\tProbe argument (only \n"
#else
		"\t\tSRC:\tSource code path\n"
		"\t\tRL:\tRelative line number from function entry.\n"
		"\t\tAL:\tAbsolute line number in file.\n"
		"\t\tPT:\tLazy expression of line code.\n"
		"\t\tARG:\tProbe argument (local variable name or\n"
#endif
		"\t\t\tkprobe-tracer argument format.)\n",
		opt_add_probe_event),
	OPT_BOOLEAN('f', "force", &params.force_add, "forcibly add events"
		    " with existing name"),
#ifndef NO_DWARF_SUPPORT
	OPT_CALLBACK('L', "line", NULL,
		     "FUNC[:RLN[+NUM|:RLN2]]|SRC:ALN[+NUM|:ALN2]",
		     "Show source code lines.", opt_show_lines),
#endif
	OPT__DRY_RUN(&probe_event_dry_run),
	OPT_END()
};

int cmd_probe(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, probe_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc > 0) {
		if (strcmp(argv[0], "-") == 0) {
			pr_warning("  Error: '-' is not supported.\n");
			usage_with_options(probe_usage, options);
		}
		parse_probe_event_argv(argc, argv);
	}

	if ((!params.nevents && !params.dellist && !params.list_events &&
	     !params.show_lines))
		usage_with_options(probe_usage, options);

	if (debugfs_valid_mountpoint(debugfs_path) < 0)
		die("Failed to find debugfs path.");

	if (params.list_events) {
		if (params.nevents != 0 || params.dellist) {
			pr_warning("  Error: Don't use --list with"
				   " --add/--del.\n");
			usage_with_options(probe_usage, options);
		}
		if (params.show_lines) {
			pr_warning("  Error: Don't use --list with --line.\n");
			usage_with_options(probe_usage, options);
		}
		show_perf_probe_events();
		return 0;
	}

#ifndef NO_DWARF_SUPPORT
	if (params.show_lines) {
		if (params.nevents != 0 || params.dellist) {
			pr_warning("  Error: Don't use --line with"
				   " --add/--del.\n");
			usage_with_options(probe_usage, options);
		}

		show_line_range(&params.line_range);
		return 0;
	}
#endif

	if (params.dellist) {
		del_perf_probe_events(params.dellist);
		strlist__delete(params.dellist);
		if (params.nevents == 0)
			return 0;
	}

	add_perf_probe_events(params.events, params.nevents, params.force_add);
	return 0;
}

