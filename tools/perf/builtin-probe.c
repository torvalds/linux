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

/* Default vmlinux search paths */
#define NR_SEARCH_PATH 3
const char *default_search_path[NR_SEARCH_PATH] = {
"/lib/modules/%s/build/vmlinux",		/* Custom build kernel */
"/usr/lib/debug/lib/modules/%s/vmlinux",	/* Red Hat debuginfo */
"/boot/vmlinux-debug-%s",			/* Ubuntu */
};

#define MAX_PATH_LEN 256
#define MAX_PROBES 128
#define MAX_PROBE_ARGS 128
#define PERFPROBE_GROUP "perfprobe"

/* Session management structure */
static struct {
	char *vmlinux;
	char *release;
	int need_dwarf;
	int nr_probe;
	struct probe_point probes[MAX_PROBES];
} session;

#define semantic_error(msg ...) die("Semantic error :" msg)

/* Parse probe point. Return 1 if return probe */
static void parse_probe_point(char *arg, struct probe_point *pp)
{
	char *ptr, *tmp;
	char c, nc;
	/*
	 * <Syntax>
	 * perf probe SRC:LN
	 * perf probe FUNC[+OFFS|%return][@SRC]
	 */

	ptr = strpbrk(arg, ":+@%");
	if (ptr) {
		nc = *ptr;
		*ptr++ = '\0';
	}

	/* Check arg is function or file and copy it */
	if (strchr(arg, '.'))	/* File */
		pp->file = strdup(arg);
	else			/* Function */
		pp->function = strdup(arg);
	DIE_IF(pp->file == NULL && pp->function == NULL);

	/* Parse other options */
	while (ptr) {
		arg = ptr;
		c = nc;
		ptr = strpbrk(arg, ":+@%");
		if (ptr) {
			nc = *ptr;
			*ptr++ = '\0';
		}
		switch (c) {
		case ':':	/* Line number */
			pp->line = strtoul(arg, &tmp, 0);
			if (*tmp != '\0')
				semantic_error("There is non-digit charactor"
						" in line number.");
			break;
		case '+':	/* Byte offset from a symbol */
			pp->offset = strtoul(arg, &tmp, 0);
			if (*tmp != '\0')
				semantic_error("There is non-digit charactor"
						" in offset.");
			break;
		case '@':	/* File name */
			if (pp->file)
				semantic_error("SRC@SRC is not allowed.");
			pp->file = strdup(arg);
			DIE_IF(pp->file == NULL);
			if (ptr)
				semantic_error("@SRC must be the last "
					       "option.");
			break;
		case '%':	/* Probe places */
			if (strcmp(arg, "return") == 0) {
				pp->retprobe = 1;
			} else	/* Others not supported yet */
				semantic_error("%%%s is not supported.", arg);
			break;
		default:
			DIE_IF("Program has a bug.");
			break;
		}
	}

	/* Exclusion check */
	if (pp->line && pp->offset)
		semantic_error("Offset can't be used with line number.");
	if (!pp->line && pp->file && !pp->function)
		semantic_error("File always requires line number.");
	if (pp->offset && !pp->function)
		semantic_error("Offset requires an entry function.");
	if (pp->retprobe && !pp->function)
		semantic_error("Return probe requires an entry function.");
	if ((pp->offset || pp->line) && pp->retprobe)
		semantic_error("Offset/Line can't be used with return probe.");

	pr_debug("symbol:%s file:%s line:%d offset:%d, return:%d\n",
		 pp->function, pp->file, pp->line, pp->offset, pp->retprobe);
}

/* Parse an event definition. Note that any error must die. */
static void parse_probe_event(const char *str)
{
	char *argv[MAX_PROBE_ARGS + 2];	/* Event + probe + args */
	int argc, i;
	struct probe_point *pp = &session.probes[session.nr_probe];

	pr_debug("probe-definition(%d): %s\n", session.nr_probe, str);
	if (++session.nr_probe == MAX_PROBES)
		semantic_error("Too many probes");

	/* Separate arguments, similar to argv_split */
	argc = 0;
	do {
		/* Skip separators */
		while (isspace(*str))
			str++;

		/* Add an argument */
		if (*str != '\0') {
			const char *s = str;

			/* Skip the argument */
			while (!isspace(*str) && *str != '\0')
				str++;

			/* Duplicate the argument */
			argv[argc] = strndup(s, str - s);
			if (argv[argc] == NULL)
				die("strndup");
			if (++argc == MAX_PROBE_ARGS)
				semantic_error("Too many arguments");
			pr_debug("argv[%d]=%s\n", argc, argv[argc - 1]);
		}
	} while (*str != '\0');
	if (!argc)
		semantic_error("An empty argument.");

	/* Parse probe point */
	parse_probe_point(argv[0], pp);
	free(argv[0]);
	if (pp->file)
		session.need_dwarf = 1;

	/* Copy arguments */
	pp->nr_args = argc - 1;
	if (pp->nr_args > 0) {
		pp->args = (char **)malloc(sizeof(char *) * pp->nr_args);
		if (!pp->args)
			die("malloc");
		memcpy(pp->args, &argv[1], sizeof(char *) * pp->nr_args);
	}

	/* Ensure return probe has no C argument */
	for (i = 0; i < pp->nr_args; i++)
		if (is_c_varname(pp->args[i])) {
			if (pp->retprobe)
				semantic_error("You can't specify local"
						" variable for kretprobe");
			session.need_dwarf = 1;
		}

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
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show parsed arguments, etc)"),
#ifndef NO_LIBDWARF
	OPT_STRING('k', "vmlinux", &session.vmlinux, "file",
		"vmlinux/module pathname"),
#endif
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
		"\t\t\tkprobe-tracer argument format is supported.)\n",
		opt_add_probe_event),
	OPT_END()
};

static int write_new_event(int fd, const char *buf)
{
	int ret;

	printf("Adding new event: %s\n", buf);
	ret = write(fd, buf, strlen(buf));
	if (ret <= 0)
		die("failed to create event.");

	return ret;
}

#define MAX_CMDLEN 256

static int synthesize_probe_event(struct probe_point *pp)
{
	char *buf;
	int i, len, ret;
	pp->probes[0] = buf = (char *)calloc(MAX_CMDLEN, sizeof(char));
	if (!buf)
		die("calloc");
	ret = snprintf(buf, MAX_CMDLEN, "%s+%d", pp->function, pp->offset);
	if (ret <= 0 || ret >= MAX_CMDLEN)
		goto error;
	len = ret;

	for (i = 0; i < pp->nr_args; i++) {
		ret = snprintf(&buf[len], MAX_CMDLEN - len, " %s",
			       pp->args[i]);
		if (ret <= 0 || ret >= MAX_CMDLEN - len)
			goto error;
		len += ret;
	}
	pp->found = 1;
	return pp->found;
error:
	free(pp->probes[0]);
	if (ret > 0)
		ret = -E2BIG;
	return ret;
}

int cmd_probe(int argc, const char **argv, const char *prefix __used)
{
	int i, j, fd, ret;
	struct probe_point *pp;
	char buf[MAX_CMDLEN];

	argc = parse_options(argc, argv, options, probe_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	for (i = 0; i < argc; i++)
		parse_probe_event(argv[i]);

	if (session.nr_probe == 0)
		usage_with_options(probe_usage, options);

#ifdef NO_LIBDWARF
	if (session.need_dwarf)
		semantic_error("Dwarf-analysis is not supported");
#endif

	/* Synthesize probes without dwarf */
	for (j = 0; j < session.nr_probe; j++) {
#ifndef NO_LIBDWARF
		if (!session.probes[j].retprobe) {
			session.need_dwarf = 1;
			continue;
		}
#endif
		ret = synthesize_probe_event(&session.probes[j]);
		if (ret == -E2BIG)
			semantic_error("probe point is too long.");
		else if (ret < 0)
			die("snprintf");
	}

#ifndef NO_LIBDWARF
	if (!session.need_dwarf)
		goto setup_probes;

	if (session.vmlinux)
		fd = open(session.vmlinux, O_RDONLY);
	else
		fd = open_default_vmlinux();
	if (fd < 0)
		die("vmlinux/module file open");

	/* Searching probe points */
	for (j = 0; j < session.nr_probe; j++) {
		pp = &session.probes[j];
		if (pp->found)
			continue;

		lseek(fd, SEEK_SET, 0);
		ret = find_probepoint(fd, pp);
		if (ret <= 0)
			die("No probe point found.\n");
	}
	close(fd);

setup_probes:
#endif /* !NO_LIBDWARF */

	/* Settng up probe points */
	snprintf(buf, MAX_CMDLEN, "%s/../kprobe_events", debugfs_path);
	fd = open(buf, O_WRONLY, O_APPEND);
	if (fd < 0)
		die("kprobe_events open");
	for (j = 0; j < session.nr_probe; j++) {
		pp = &session.probes[j];
		if (pp->found == 1) {
			snprintf(buf, MAX_CMDLEN, "%c:%s/%s_%x %s\n",
				pp->retprobe ? 'r' : 'p', PERFPROBE_GROUP,
				pp->function, pp->offset, pp->probes[0]);
			write_new_event(fd, buf);
		} else
			for (i = 0; i < pp->found; i++) {
				snprintf(buf, MAX_CMDLEN, "%c:%s/%s_%x_%d %s\n",
					pp->retprobe ? 'r' : 'p',
					PERFPROBE_GROUP,
					pp->function, pp->offset, i,
					pp->probes[0]);
				write_new_event(fd, buf);
			}
	}
	close(fd);
	return 0;
}

