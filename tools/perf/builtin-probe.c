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
#include "util/event.h"
#include "util/debug.h"
#include "util/parse-options.h"
#include "util/parse-events.h"	/* For debugfs_path */
#include "util/probe-finder.h"
#include "util/probe-event.h"

/* Default vmlinux search paths */
#define NR_SEARCH_PATH 3
const char *default_search_path[NR_SEARCH_PATH] = {
"/lib/modules/%s/build/vmlinux",		/* Custom build kernel */
"/usr/lib/debug/lib/modules/%s/vmlinux",	/* Red Hat debuginfo */
"/boot/vmlinux-debug-%s",			/* Ubuntu */
};

#define MAX_PATH_LEN 256
#define MAX_PROBES 128

/* Session management structure */
static struct {
	char *vmlinux;
	char *release;
	int need_dwarf;
	int nr_probe;
	struct probe_point probes[MAX_PROBES];
} session;

static bool listing;

/* Parse an event definition. Note that any error must die. */
static void parse_probe_event(const char *str)
{
	struct probe_point *pp = &session.probes[session.nr_probe];

	pr_debug("probe-definition(%d): %s\n", session.nr_probe, str);
	if (++session.nr_probe == MAX_PROBES)
		die("Too many probes (> %d) are specified.", MAX_PROBES);

	/* Parse perf-probe event into probe_point */
	session.need_dwarf = parse_perf_probe_event(str, pp);

	pr_debug("%d arguments\n", pp->nr_args);
}

static int opt_add_probe_event(const struct option *opt __used,
			      const char *str, int unset __used)
{
	if (str)
		parse_probe_event(str);
	return 0;
}

#ifndef NO_LIBDWARF
static int open_default_vmlinux(void)
{
	struct utsname uts;
	char fname[MAX_PATH_LEN];
	int fd, ret, i;

	ret = uname(&uts);
	if (ret) {
		pr_debug("uname() failed.\n");
		return -errno;
	}
	session.release = uts.release;
	for (i = 0; i < NR_SEARCH_PATH; i++) {
		ret = snprintf(fname, MAX_PATH_LEN,
			       default_search_path[i], session.release);
		if (ret >= MAX_PATH_LEN || ret < 0) {
			pr_debug("Filename(%d,%s) is too long.\n", i,
				uts.release);
			errno = E2BIG;
			return -E2BIG;
		}
		pr_debug("try to open %s\n", fname);
		fd = open(fname, O_RDONLY);
		if (fd >= 0)
			break;
	}
	return fd;
}
#endif

static const char * const probe_usage[] = {
	"perf probe [<options>] 'PROBEDEF' ['PROBEDEF' ...]",
	"perf probe [<options>] --add 'PROBEDEF' [--add 'PROBEDEF' ...]",
	"perf probe --list",
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show parsed arguments, etc)"),
#ifndef NO_LIBDWARF
	OPT_STRING('k', "vmlinux", &session.vmlinux, "file",
		"vmlinux/module pathname"),
#endif
	OPT_BOOLEAN('l', "list", &listing, "list up current probes"),
	OPT_CALLBACK('a', "add", NULL,
#ifdef NO_LIBDWARF
		"FUNC[+OFFS|%return] [ARG ...]",
#else
		"FUNC[+OFFS|%return|:RLN][@SRC]|SRC:ALN [ARG ...]",
#endif
		"probe point definition, where\n"
		"\t\tGRP:\tGroup name (optional)\n"
		"\t\tNAME:\tEvent name\n"
		"\t\tFUNC:\tFunction name\n"
		"\t\tOFFS:\tOffset from function entry (in byte)\n"
		"\t\t%return:\tPut the probe at function return\n"
#ifdef NO_LIBDWARF
		"\t\tARG:\tProbe argument (only \n"
#else
		"\t\tSRC:\tSource code path\n"
		"\t\tRLN:\tRelative line number from function entry.\n"
		"\t\tALN:\tAbsolute line number in file.\n"
		"\t\tARG:\tProbe argument (local variable name or\n"
#endif
		"\t\t\tkprobe-tracer argument format.)\n",
		opt_add_probe_event),
	OPT_END()
};

int cmd_probe(int argc, const char **argv, const char *prefix __used)
{
	int i, j, ret;
#ifndef NO_LIBDWARF
	int fd;
#endif
	struct probe_point *pp;

	argc = parse_options(argc, argv, options, probe_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	for (i = 0; i < argc; i++)
		parse_probe_event(argv[i]);

	if ((session.nr_probe == 0 && !listing) ||
	    (session.nr_probe != 0 && listing))
		usage_with_options(probe_usage, options);

	if (listing) {
		show_perf_probe_events();
		return 0;
	}

	if (session.need_dwarf)
#ifdef NO_LIBDWARF
		die("Debuginfo-analysis is not supported");
#else	/* !NO_LIBDWARF */
		pr_debug("Some probes require debuginfo.\n");

	if (session.vmlinux)
		fd = open(session.vmlinux, O_RDONLY);
	else
		fd = open_default_vmlinux();
	if (fd < 0) {
		if (session.need_dwarf)
			die("Could not open vmlinux/module file.");

		pr_debug("Could not open vmlinux/module file."
			 " Try to use symbols.\n");
		goto end_dwarf;
	}

	/* Searching probe points */
	for (j = 0; j < session.nr_probe; j++) {
		pp = &session.probes[j];
		if (pp->found)
			continue;

		lseek(fd, SEEK_SET, 0);
		ret = find_probepoint(fd, pp);
		if (ret < 0) {
			if (session.need_dwarf)
				die("Could not analyze debuginfo.");

			pr_warning("An error occurred in debuginfo analysis. Try to use symbols.\n");
			break;
		}
		if (ret == 0)	/* No error but failed to find probe point. */
			die("No probe point found.");
	}
	close(fd);

end_dwarf:
#endif /* !NO_LIBDWARF */

	/* Synthesize probes without dwarf */
	for (j = 0; j < session.nr_probe; j++) {
		pp = &session.probes[j];
		if (pp->found)	/* This probe is already found. */
			continue;

		ret = synthesize_trace_kprobe_event(pp);
		if (ret == -E2BIG)
			die("probe point definition becomes too long.");
		else if (ret < 0)
			die("Failed to synthesize a probe point.");
	}

	/* Settng up probe points */
	add_trace_kprobe_events(session.probes, session.nr_probe);
	return 0;
}

