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
#include <stdarg.h>
#include <limits.h>

#undef _GNU_SOURCE
#include "event.h"
#include "string.h"
#include "strlist.h"
#include "debug.h"
#include "parse-events.h"  /* For debugfs_path */
#include "probe-event.h"

#define MAX_CMDLEN 256
#define MAX_PROBE_ARGS 128
#define PERFPROBE_GROUP "probe"

#define semantic_error(msg ...) die("Semantic error :" msg)

/* If there is no space to write, returns -E2BIG. */
static int e_snprintf(char *str, size_t size, const char *format, ...)
	__attribute__((format(printf, 3, 4)));

static int e_snprintf(char *str, size_t size, const char *format, ...)
{
	int ret;
	va_list ap;
	va_start(ap, format);
	ret = vsnprintf(str, size, format, ap);
	va_end(ap);
	if (ret >= (int)size)
		ret = -E2BIG;
	return ret;
}

/* Check the name is good for event/group */
static bool check_event_name(const char *name)
{
	if (!isalpha(*name) && *name != '_')
		return false;
	while (*++name != '\0') {
		if (!isalpha(*name) && !isdigit(*name) && *name != '_')
			return false;
	}
	return true;
}

/* Parse probepoint definition. */
static void parse_perf_probe_probepoint(char *arg, struct probe_point *pp)
{
	char *ptr, *tmp;
	char c, nc = 0;
	/*
	 * <Syntax>
	 * perf probe [EVENT=]SRC:LN
	 * perf probe [EVENT=]FUNC[+OFFS|%return][@SRC]
	 *
	 * TODO:Group name support
	 */

	ptr = strchr(arg, '=');
	if (ptr) {	/* Event name */
		*ptr = '\0';
		tmp = ptr + 1;
		ptr = strchr(arg, ':');
		if (ptr)	/* Group name is not supported yet. */
			semantic_error("Group name is not supported yet.");
		if (!check_event_name(arg))
			semantic_error("%s is bad for event name -it must "
				       "follow C symbol-naming rule.", arg);
		pp->event = strdup(arg);
		arg = tmp;
	}

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
void parse_perf_probe_event(const char *str, struct probe_point *pp,
			    bool *need_dwarf)
{
	char **argv;
	int argc, i;

	*need_dwarf = false;

	argv = argv_split(str, &argc);
	if (!argv)
		die("argv_split failed.");
	if (argc > MAX_PROBE_ARGS + 1)
		semantic_error("Too many arguments");

	/* Parse probe point */
	parse_perf_probe_probepoint(argv[0], pp);
	if (pp->file || pp->line)
		*need_dwarf = true;

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
			*need_dwarf = true;
		}
	}

	argv_free(argv);
}

/* Parse kprobe_events event into struct probe_point */
void parse_trace_kprobe_event(const char *str, struct probe_point *pp)
{
	char pr;
	char *p;
	int ret, i, argc;
	char **argv;

	pr_debug("Parsing kprobe_events: %s\n", str);
	argv = argv_split(str, &argc);
	if (!argv)
		die("argv_split failed.");
	if (argc < 2)
		semantic_error("Too less arguments.");

	/* Scan event and group name. */
	ret = sscanf(argv[0], "%c:%a[^/ \t]/%a[^ \t]",
		     &pr, (float *)(void *)&pp->group,
		     (float *)(void *)&pp->event);
	if (ret != 3)
		semantic_error("Failed to parse event name: %s", argv[0]);
	pr_debug("Group:%s Event:%s probe:%c\n", pp->group, pp->event, pr);

	pp->retprobe = (pr == 'r');

	/* Scan function name and offset */
	ret = sscanf(argv[1], "%a[^+]+%d", (float *)(void *)&pp->function,
		     &pp->offset);
	if (ret == 1)
		pp->offset = 0;

	/* kprobe_events doesn't have this information */
	pp->line = 0;
	pp->file = NULL;

	pp->nr_args = argc - 2;
	pp->args = zalloc(sizeof(char *) * pp->nr_args);
	for (i = 0; i < pp->nr_args; i++) {
		p = strchr(argv[i + 2], '=');
		if (p)	/* We don't need which register is assigned. */
			*p = '\0';
		pp->args[i] = strdup(argv[i + 2]);
		if (!pp->args[i])
			die("Failed to copy argument.");
	}

	argv_free(argv);
}

/* Synthesize only probe point (not argument) */
int synthesize_perf_probe_point(struct probe_point *pp)
{
	char *buf;
	char offs[64] = "", line[64] = "";
	int ret;

	pp->probes[0] = buf = zalloc(MAX_CMDLEN);
	pp->found = 1;
	if (!buf)
		die("Failed to allocate memory by zalloc.");
	if (pp->offset) {
		ret = e_snprintf(offs, 64, "+%d", pp->offset);
		if (ret <= 0)
			goto error;
	}
	if (pp->line) {
		ret = e_snprintf(line, 64, ":%d", pp->line);
		if (ret <= 0)
			goto error;
	}

	if (pp->function)
		ret = e_snprintf(buf, MAX_CMDLEN, "%s%s%s%s", pp->function,
				 offs, pp->retprobe ? "%return" : "", line);
	else
		ret = e_snprintf(buf, MAX_CMDLEN, "%s%s", pp->file, line);
	if (ret <= 0) {
error:
		free(pp->probes[0]);
		pp->probes[0] = NULL;
		pp->found = 0;
	}
	return ret;
}

int synthesize_perf_probe_event(struct probe_point *pp)
{
	char *buf;
	int i, len, ret;

	len = synthesize_perf_probe_point(pp);
	if (len < 0)
		return 0;

	buf = pp->probes[0];
	for (i = 0; i < pp->nr_args; i++) {
		ret = e_snprintf(&buf[len], MAX_CMDLEN - len, " %s",
				 pp->args[i]);
		if (ret <= 0)
			goto error;
		len += ret;
	}
	pp->found = 1;

	return pp->found;
error:
	free(pp->probes[0]);
	pp->probes[0] = NULL;

	return ret;
}

int synthesize_trace_kprobe_event(struct probe_point *pp)
{
	char *buf;
	int i, len, ret;

	pp->probes[0] = buf = zalloc(MAX_CMDLEN);
	if (!buf)
		die("Failed to allocate memory by zalloc.");
	ret = e_snprintf(buf, MAX_CMDLEN, "%s+%d", pp->function, pp->offset);
	if (ret <= 0)
		goto error;
	len = ret;

	for (i = 0; i < pp->nr_args; i++) {
		ret = e_snprintf(&buf[len], MAX_CMDLEN - len, " %s",
				 pp->args[i]);
		if (ret <= 0)
			goto error;
		len += ret;
	}
	pp->found = 1;

	return pp->found;
error:
	free(pp->probes[0]);
	pp->probes[0] = NULL;

	return ret;
}

static int open_kprobe_events(int flags, int mode)
{
	char buf[PATH_MAX];
	int ret;

	ret = e_snprintf(buf, PATH_MAX, "%s/../kprobe_events", debugfs_path);
	if (ret < 0)
		die("Failed to make kprobe_events path.");

	ret = open(buf, flags, mode);
	if (ret < 0) {
		if (errno == ENOENT)
			die("kprobe_events file does not exist -"
			    " please rebuild with CONFIG_KPROBE_TRACER.");
		else
			die("Could not open kprobe_events file: %s",
			    strerror(errno));
	}
	return ret;
}

/* Get raw string list of current kprobe_events */
static struct strlist *get_trace_kprobe_event_rawlist(int fd)
{
	int ret, idx;
	FILE *fp;
	char buf[MAX_CMDLEN];
	char *p;
	struct strlist *sl;

	sl = strlist__new(true, NULL);

	fp = fdopen(dup(fd), "r");
	while (!feof(fp)) {
		p = fgets(buf, MAX_CMDLEN, fp);
		if (!p)
			break;

		idx = strlen(p) - 1;
		if (p[idx] == '\n')
			p[idx] = '\0';
		ret = strlist__add(sl, buf);
		if (ret < 0)
			die("strlist__add failed: %s", strerror(-ret));
	}
	fclose(fp);

	return sl;
}

/* Free and zero clear probe_point */
static void clear_probe_point(struct probe_point *pp)
{
	int i;

	if (pp->event)
		free(pp->event);
	if (pp->group)
		free(pp->group);
	if (pp->function)
		free(pp->function);
	if (pp->file)
		free(pp->file);
	for (i = 0; i < pp->nr_args; i++)
		free(pp->args[i]);
	if (pp->args)
		free(pp->args);
	for (i = 0; i < pp->found; i++)
		free(pp->probes[i]);
	memset(pp, 0, sizeof(*pp));
}

/* Show an event */
static void show_perf_probe_event(const char *event, const char *place,
				  struct probe_point *pp)
{
	int i, ret;
	char buf[128];

	ret = e_snprintf(buf, 128, "%s:%s", pp->group, event);
	if (ret < 0)
		die("Failed to copy event: %s", strerror(-ret));
	printf("  %-40s (on %s", buf, place);

	if (pp->nr_args > 0) {
		printf(" with");
		for (i = 0; i < pp->nr_args; i++)
			printf(" %s", pp->args[i]);
	}
	printf(")\n");
}

/* List up current perf-probe events */
void show_perf_probe_events(void)
{
	int fd;
	struct probe_point pp;
	struct strlist *rawlist;
	struct str_node *ent;

	memset(&pp, 0, sizeof(pp));
	fd = open_kprobe_events(O_RDONLY, 0);
	rawlist = get_trace_kprobe_event_rawlist(fd);
	close(fd);

	strlist__for_each(ent, rawlist) {
		parse_trace_kprobe_event(ent->s, &pp);
		/* Synthesize only event probe point */
		synthesize_perf_probe_point(&pp);
		/* Show an event */
		show_perf_probe_event(pp.event, pp.probes[0], &pp);
		clear_probe_point(&pp);
	}

	strlist__delete(rawlist);
}

/* Get current perf-probe event names */
static struct strlist *get_perf_event_names(int fd, bool include_group)
{
	char buf[128];
	struct strlist *sl, *rawlist;
	struct str_node *ent;
	struct probe_point pp;

	memset(&pp, 0, sizeof(pp));
	rawlist = get_trace_kprobe_event_rawlist(fd);

	sl = strlist__new(true, NULL);
	strlist__for_each(ent, rawlist) {
		parse_trace_kprobe_event(ent->s, &pp);
		if (include_group) {
			if (e_snprintf(buf, 128, "%s:%s", pp.group,
				       pp.event) < 0)
				die("Failed to copy group:event name.");
			strlist__add(sl, buf);
		} else
			strlist__add(sl, pp.event);
		clear_probe_point(&pp);
	}

	strlist__delete(rawlist);

	return sl;
}

static void write_trace_kprobe_event(int fd, const char *buf)
{
	int ret;

	pr_debug("Writing event: %s\n", buf);
	ret = write(fd, buf, strlen(buf));
	if (ret <= 0)
		die("Failed to write event: %s", strerror(errno));
}

static void get_new_event_name(char *buf, size_t len, const char *base,
			       struct strlist *namelist, bool allow_suffix)
{
	int i, ret;

	/* Try no suffix */
	ret = e_snprintf(buf, len, "%s", base);
	if (ret < 0)
		die("snprintf() failed: %s", strerror(-ret));
	if (!strlist__has_entry(namelist, buf))
		return;

	if (!allow_suffix) {
		pr_warning("Error: event \"%s\" already exists. "
			   "(Use -f to force duplicates.)\n", base);
		die("Can't add new event.");
	}

	/* Try to add suffix */
	for (i = 1; i < MAX_EVENT_INDEX; i++) {
		ret = e_snprintf(buf, len, "%s_%d", base, i);
		if (ret < 0)
			die("snprintf() failed: %s", strerror(-ret));
		if (!strlist__has_entry(namelist, buf))
			break;
	}
	if (i == MAX_EVENT_INDEX)
		die("Too many events are on the same function.");
}

void add_trace_kprobe_events(struct probe_point *probes, int nr_probes,
			     bool force_add)
{
	int i, j, fd;
	struct probe_point *pp;
	char buf[MAX_CMDLEN];
	char event[64];
	struct strlist *namelist;
	bool allow_suffix;

	fd = open_kprobe_events(O_RDWR, O_APPEND);
	/* Get current event names */
	namelist = get_perf_event_names(fd, false);

	for (j = 0; j < nr_probes; j++) {
		pp = probes + j;
		if (!pp->event)
			pp->event = strdup(pp->function);
		if (!pp->group)
			pp->group = strdup(PERFPROBE_GROUP);
		DIE_IF(!pp->event || !pp->group);
		/* If force_add is true, suffix search is allowed */
		allow_suffix = force_add;
		for (i = 0; i < pp->found; i++) {
			/* Get an unused new event name */
			get_new_event_name(event, 64, pp->event, namelist,
					   allow_suffix);
			snprintf(buf, MAX_CMDLEN, "%c:%s/%s %s\n",
				 pp->retprobe ? 'r' : 'p',
				 pp->group, event,
				 pp->probes[i]);
			write_trace_kprobe_event(fd, buf);
			printf("Added new event:\n");
			/* Get the first parameter (probe-point) */
			sscanf(pp->probes[i], "%s", buf);
			show_perf_probe_event(event, buf, pp);
			/* Add added event name to namelist */
			strlist__add(namelist, event);
			/*
			 * Probes after the first probe which comes from same
			 * user input are always allowed to add suffix, because
			 * there might be several addresses corresponding to
			 * one code line.
			 */
			allow_suffix = true;
		}
	}
	/* Show how to use the event. */
	printf("\nYou can now use it on all perf tools, such as:\n\n");
	printf("\tperf record -e %s:%s -a sleep 1\n\n", PERFPROBE_GROUP, event);

	strlist__delete(namelist);
	close(fd);
}

static void __del_trace_kprobe_event(int fd, struct str_node *ent)
{
	char *p;
	char buf[128];

	/* Convert from perf-probe event to trace-kprobe event */
	if (e_snprintf(buf, 128, "-:%s", ent->s) < 0)
		die("Failed to copy event.");
	p = strchr(buf + 2, ':');
	if (!p)
		die("Internal error: %s should have ':' but not.", ent->s);
	*p = '/';

	write_trace_kprobe_event(fd, buf);
	printf("Remove event: %s\n", ent->s);
}

static void del_trace_kprobe_event(int fd, const char *group,
				   const char *event, struct strlist *namelist)
{
	char buf[128];
	struct str_node *ent, *n;
	int found = 0;

	if (e_snprintf(buf, 128, "%s:%s", group, event) < 0)
		die("Failed to copy event.");

	if (strpbrk(buf, "*?")) { /* Glob-exp */
		strlist__for_each_safe(ent, n, namelist)
			if (strglobmatch(ent->s, buf)) {
				found++;
				__del_trace_kprobe_event(fd, ent);
				strlist__remove(namelist, ent);
			}
	} else {
		ent = strlist__find(namelist, buf);
		if (ent) {
			found++;
			__del_trace_kprobe_event(fd, ent);
			strlist__remove(namelist, ent);
		}
	}
	if (found == 0)
		pr_info("Info: event \"%s\" does not exist, could not remove it.\n", buf);
}

void del_trace_kprobe_events(struct strlist *dellist)
{
	int fd;
	const char *group, *event;
	char *p, *str;
	struct str_node *ent;
	struct strlist *namelist;

	fd = open_kprobe_events(O_RDWR, O_APPEND);
	/* Get current event names */
	namelist = get_perf_event_names(fd, true);

	strlist__for_each(ent, dellist) {
		str = strdup(ent->s);
		if (!str)
			die("Failed to copy event.");
		pr_debug("Parsing: %s\n", str);
		p = strchr(str, ':');
		if (p) {
			group = str;
			*p = '\0';
			event = p + 1;
		} else {
			group = "*";
			event = str;
		}
		pr_debug("Group: %s, Event: %s\n", group, event);
		del_trace_kprobe_event(fd, group, event, namelist);
		free(str);
	}
	strlist__delete(namelist);
	close(fd);
}

