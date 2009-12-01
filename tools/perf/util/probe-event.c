/*
 * probe-event.c : perf-probe definition to kprobe_events format converter
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
#include "event.h"
#include "string.h"
#include "debug.h"
#include "parse-events.h"  /* For debugfs_path */
#include "probe-event.h"

#define MAX_CMDLEN 256
#define MAX_PROBE_ARGS 128
#define PERFPROBE_GROUP "probe"

#define semantic_error(msg ...) die("Semantic error :" msg)

/* Parse probepoint definition. */
static void parse_perf_probe_probepoint(char *arg, struct probe_point *pp)
{
	char *ptr, *tmp;
	char c, nc = 0;
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

/* Parse perf-probe event definition */
int parse_perf_probe_event(const char *str, struct probe_point *pp)
{
	char **argv;
	int argc, i, need_dwarf = 0;

	argv = argv_split(str, &argc);
	if (!argv)
		die("argv_split failed.");
	if (argc > MAX_PROBE_ARGS + 1)
		semantic_error("Too many arguments");

	/* Parse probe point */
	parse_perf_probe_probepoint(argv[0], pp);
	if (pp->file || pp->line)
		need_dwarf = 1;

	/* Copy arguments and ensure return probe has no C argument */
	pp->nr_args = argc - 1;
	pp->args = zalloc(sizeof(char *) * pp->nr_args);
	for (i = 0; i < pp->nr_args; i++) {
		pp->args[i] = strdup(argv[i + 1]);
		if (!pp->args[i])
			die("Failed to copy argument.");
		if (is_c_varname(pp->args[i])) {
			if (pp->retprobe)
				semantic_error("You can't specify local"
						" variable for kretprobe");
			need_dwarf = 1;
		}
	}

	argv_free(argv);
	return need_dwarf;
}

int synthesize_trace_kprobe_event(struct probe_point *pp)
{
	char *buf;
	int i, len, ret;

	pp->probes[0] = buf = zalloc(MAX_CMDLEN);
	if (!buf)
		die("Failed to allocate memory by zalloc.");
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

static int write_trace_kprobe_event(int fd, const char *buf)
{
	int ret;

	ret = write(fd, buf, strlen(buf));
	if (ret <= 0)
		die("Failed to create event.");
	else
		printf("Added new event: %s\n", buf);

	return ret;
}

void add_trace_kprobe_events(struct probe_point *probes, int nr_probes)
{
	int i, j, fd;
	struct probe_point *pp;
	char buf[MAX_CMDLEN];

	snprintf(buf, MAX_CMDLEN, "%s/../kprobe_events", debugfs_path);
	fd = open(buf, O_WRONLY, O_APPEND);
	if (fd < 0) {
		if (errno == ENOENT)
			die("kprobe_events file does not exist -"
			    " please rebuild with CONFIG_KPROBE_TRACER.");
		else
			die("Could not open kprobe_events file: %s",
			    strerror(errno));
	}

	for (j = 0; j < nr_probes; j++) {
		pp = probes + j;
		if (pp->found == 1) {
			snprintf(buf, MAX_CMDLEN, "%c:%s/%s_%x %s\n",
				pp->retprobe ? 'r' : 'p', PERFPROBE_GROUP,
				pp->function, pp->offset, pp->probes[0]);
			write_trace_kprobe_event(fd, buf);
		} else
			for (i = 0; i < pp->found; i++) {
				snprintf(buf, MAX_CMDLEN, "%c:%s/%s_%x_%d %s\n",
					pp->retprobe ? 'r' : 'p',
					PERFPROBE_GROUP,
					pp->function, pp->offset, i,
					pp->probes[i]);
				write_trace_kprobe_event(fd, buf);
			}
	}
	close(fd);
}
