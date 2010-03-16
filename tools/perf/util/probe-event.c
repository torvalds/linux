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
#include "util.h"
#include "event.h"
#include "string.h"
#include "strlist.h"
#include "debug.h"
#include "cache.h"
#include "color.h"
#include "symbol.h"
#include "thread.h"
#include "parse-events.h"  /* For debugfs_path */
#include "probe-event.h"
#include "probe-finder.h"

#define MAX_CMDLEN 256
#define MAX_PROBE_ARGS 128
#define PERFPROBE_GROUP "probe"

bool probe_event_dry_run;	/* Dry run flag */

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

static struct map_groups kmap_groups;
static struct map *kmaps[MAP__NR_TYPES];

/* Initialize symbol maps for vmlinux */
static void init_vmlinux(void)
{
	symbol_conf.sort_by_name = true;
	if (symbol_conf.vmlinux_name == NULL)
		symbol_conf.try_vmlinux_path = true;
	else
		pr_debug("Use vmlinux: %s\n", symbol_conf.vmlinux_name);
	if (symbol__init() < 0)
		die("Failed to init symbol map.");

	map_groups__init(&kmap_groups);
	if (map_groups__create_kernel_maps(&kmap_groups, kmaps) < 0)
		die("Failed to create kernel maps.");
}

#ifndef NO_DWARF_SUPPORT
static int open_vmlinux(void)
{
	if (map__load(kmaps[MAP__FUNCTION], NULL) < 0) {
		pr_debug("Failed to load kernel map.\n");
		return -EINVAL;
	}
	pr_debug("Try to open %s\n", kmaps[MAP__FUNCTION]->dso->long_name);
	return open(kmaps[MAP__FUNCTION]->dso->long_name, O_RDONLY);
}
#endif

void parse_line_range_desc(const char *arg, struct line_range *lr)
{
	const char *ptr;
	char *tmp;
	/*
	 * <Syntax>
	 * SRC:SLN[+NUM|-ELN]
	 * FUNC[:SLN[+NUM|-ELN]]
	 */
	ptr = strchr(arg, ':');
	if (ptr) {
		lr->start = (unsigned int)strtoul(ptr + 1, &tmp, 0);
		if (*tmp == '+')
			lr->end = lr->start + (unsigned int)strtoul(tmp + 1,
								    &tmp, 0);
		else if (*tmp == '-')
			lr->end = (unsigned int)strtoul(tmp + 1, &tmp, 0);
		else
			lr->end = 0;
		pr_debug("Line range is %u to %u\n", lr->start, lr->end);
		if (lr->end && lr->start > lr->end)
			semantic_error("Start line must be smaller"
				       " than end line.");
		if (*tmp != '\0')
			semantic_error("Tailing with invalid character '%d'.",
				       *tmp);
		tmp = xstrndup(arg, (ptr - arg));
	} else
		tmp = xstrdup(arg);

	if (strchr(tmp, '.'))
		lr->file = tmp;
	else
		lr->function = tmp;
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
static void parse_perf_probe_point(char *arg, struct perf_probe_event *pev)
{
	struct perf_probe_point *pp = &pev->point;
	char *ptr, *tmp;
	char c, nc = 0;
	/*
	 * <Syntax>
	 * perf probe [EVENT=]SRC[:LN|;PTN]
	 * perf probe [EVENT=]FUNC[@SRC][+OFFS|%return|:LN|;PAT]
	 *
	 * TODO:Group name support
	 */

	ptr = strpbrk(arg, ";=@+%");
	if (ptr && *ptr == '=') {	/* Event name */
		*ptr = '\0';
		tmp = ptr + 1;
		ptr = strchr(arg, ':');
		if (ptr)	/* Group name is not supported yet. */
			semantic_error("Group name is not supported yet.");
		if (!check_event_name(arg))
			semantic_error("%s is bad for event name -it must "
				       "follow C symbol-naming rule.", arg);
		pev->event = xstrdup(arg);
		pev->group = NULL;
		arg = tmp;
	}

	ptr = strpbrk(arg, ";:+@%");
	if (ptr) {
		nc = *ptr;
		*ptr++ = '\0';
	}

	/* Check arg is function or file and copy it */
	if (strchr(arg, '.'))	/* File */
		pp->file = xstrdup(arg);
	else			/* Function */
		pp->function = xstrdup(arg);

	/* Parse other options */
	while (ptr) {
		arg = ptr;
		c = nc;
		if (c == ';') {	/* Lazy pattern must be the last part */
			pp->lazy_line = xstrdup(arg);
			break;
		}
		ptr = strpbrk(arg, ";:+@%");
		if (ptr) {
			nc = *ptr;
			*ptr++ = '\0';
		}
		switch (c) {
		case ':':	/* Line number */
			pp->line = strtoul(arg, &tmp, 0);
			if (*tmp != '\0')
				semantic_error("There is non-digit char"
					       " in line number.");
			break;
		case '+':	/* Byte offset from a symbol */
			pp->offset = strtoul(arg, &tmp, 0);
			if (*tmp != '\0')
				semantic_error("There is non-digit character"
						" in offset.");
			break;
		case '@':	/* File name */
			if (pp->file)
				semantic_error("SRC@SRC is not allowed.");
			pp->file = xstrdup(arg);
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
	if (pp->lazy_line && pp->line)
		semantic_error("Lazy pattern can't be used with line number.");

	if (pp->lazy_line && pp->offset)
		semantic_error("Lazy pattern can't be used with offset.");

	if (pp->line && pp->offset)
		semantic_error("Offset can't be used with line number.");

	if (!pp->line && !pp->lazy_line && pp->file && !pp->function)
		semantic_error("File always requires line number or "
			       "lazy pattern.");

	if (pp->offset && !pp->function)
		semantic_error("Offset requires an entry function.");

	if (pp->retprobe && !pp->function)
		semantic_error("Return probe requires an entry function.");

	if ((pp->offset || pp->line || pp->lazy_line) && pp->retprobe)
		semantic_error("Offset/Line/Lazy pattern can't be used with "
			       "return probe.");

	pr_debug("symbol:%s file:%s line:%d offset:%lu return:%d lazy:%s\n",
		 pp->function, pp->file, pp->line, pp->offset, pp->retprobe,
		 pp->lazy_line);
}

/* Parse perf-probe event argument */
static void parse_perf_probe_arg(const char *str, struct perf_probe_arg *arg)
{
	const char *tmp;
	struct perf_probe_arg_field **fieldp;

	pr_debug("parsing arg: %s into ", str);

	tmp = strpbrk(str, "-.");
	if (!is_c_varname(str) || !tmp) {
		/* A variable, register, symbol or special value */
		arg->name = xstrdup(str);
		pr_debug("%s\n", arg->name);
		return;
	}

	/* Structure fields */
	arg->name = xstrndup(str, tmp - str);
	pr_debug("%s, ", arg->name);
	fieldp = &arg->field;

	do {
		*fieldp = xzalloc(sizeof(struct perf_probe_arg_field));
		if (*tmp == '.') {
			str = tmp + 1;
			(*fieldp)->ref = false;
		} else if (tmp[1] == '>') {
			str = tmp + 2;
			(*fieldp)->ref = true;
		} else
			semantic_error("Argument parse error: %s", str);

		tmp = strpbrk(str, "-.");
		if (tmp) {
			(*fieldp)->name = xstrndup(str, tmp - str);
			pr_debug("%s(%d), ", (*fieldp)->name, (*fieldp)->ref);
			fieldp = &(*fieldp)->next;
		}
	} while (tmp);
	(*fieldp)->name = xstrdup(str);
	pr_debug("%s(%d)\n", (*fieldp)->name, (*fieldp)->ref);
}

/* Parse perf-probe event command */
void parse_perf_probe_command(const char *cmd, struct perf_probe_event *pev)
{
	char **argv;
	int argc, i;

	argv = argv_split(cmd, &argc);
	if (!argv)
		die("argv_split failed.");
	if (argc > MAX_PROBE_ARGS + 1)
		semantic_error("Too many arguments");

	/* Parse probe point */
	parse_perf_probe_point(argv[0], pev);

	/* Copy arguments and ensure return probe has no C argument */
	pev->nargs = argc - 1;
	pev->args = xzalloc(sizeof(struct perf_probe_arg) * pev->nargs);
	for (i = 0; i < pev->nargs; i++) {
		parse_perf_probe_arg(argv[i + 1], &pev->args[i]);
		if (is_c_varname(pev->args[i].name) && pev->point.retprobe)
			semantic_error("You can't specify local variable for"
				       " kretprobe");
	}

	argv_free(argv);
}

/* Return true if this perf_probe_event requires debuginfo */
bool perf_probe_event_need_dwarf(struct perf_probe_event *pev)
{
	int i;

	if (pev->point.file || pev->point.line || pev->point.lazy_line)
		return true;

	for (i = 0; i < pev->nargs; i++)
		if (is_c_varname(pev->args[i].name))
			return true;

	return false;
}

/* Parse kprobe_events event into struct probe_point */
void parse_kprobe_trace_command(const char *cmd, struct kprobe_trace_event *tev)
{
	struct kprobe_trace_point *tp = &tev->point;
	char pr;
	char *p;
	int ret, i, argc;
	char **argv;

	pr_debug("Parsing kprobe_events: %s\n", cmd);
	argv = argv_split(cmd, &argc);
	if (!argv)
		die("argv_split failed.");
	if (argc < 2)
		semantic_error("Too less arguments.");

	/* Scan event and group name. */
	ret = sscanf(argv[0], "%c:%a[^/ \t]/%a[^ \t]",
		     &pr, (float *)(void *)&tev->group,
		     (float *)(void *)&tev->event);
	if (ret != 3)
		semantic_error("Failed to parse event name: %s", argv[0]);
	pr_debug("Group:%s Event:%s probe:%c\n", tev->group, tev->event, pr);

	tp->retprobe = (pr == 'r');

	/* Scan function name and offset */
	ret = sscanf(argv[1], "%a[^+]+%lu", (float *)(void *)&tp->symbol,
		     &tp->offset);
	if (ret == 1)
		tp->offset = 0;

	tev->nargs = argc - 2;
	tev->args = xzalloc(sizeof(struct kprobe_trace_arg) * tev->nargs);
	for (i = 0; i < tev->nargs; i++) {
		p = strchr(argv[i + 2], '=');
		if (p)	/* We don't need which register is assigned. */
			*p++ = '\0';
		else
			p = argv[i + 2];
		tev->args[i].name = xstrdup(argv[i + 2]);
		/* TODO: parse regs and offset */
		tev->args[i].value = xstrdup(p);
	}

	argv_free(argv);
}

/* Compose only probe arg */
int synthesize_perf_probe_arg(struct perf_probe_arg *pa, char *buf, size_t len)
{
	struct perf_probe_arg_field *field = pa->field;
	int ret;
	char *tmp = buf;

	ret = e_snprintf(tmp, len, "%s", pa->name);
	if (ret <= 0)
		goto error;
	tmp += ret;
	len -= ret;

	while (field) {
		ret = e_snprintf(tmp, len, "%s%s", field->ref ? "->" : ".",
				 field->name);
		if (ret <= 0)
			goto error;
		tmp += ret;
		len -= ret;
		field = field->next;
	}
	return tmp - buf;
error:
	die("Failed to synthesize perf probe argument: %s", strerror(-ret));
}

/* Compose only probe point (not argument) */
static char *synthesize_perf_probe_point(struct perf_probe_point *pp)
{
	char *buf, *tmp;
	char offs[32] = "", line[32] = "", file[32] = "";
	int ret, len;

	buf = xzalloc(MAX_CMDLEN);
	if (pp->offset) {
		ret = e_snprintf(offs, 32, "+%lu", pp->offset);
		if (ret <= 0)
			goto error;
	}
	if (pp->line) {
		ret = e_snprintf(line, 32, ":%d", pp->line);
		if (ret <= 0)
			goto error;
	}
	if (pp->file) {
		len = strlen(pp->file) - 32;
		if (len < 0)
			len = 0;
		tmp = strchr(pp->file + len, '/');
		if (!tmp)
			tmp = pp->file + len - 1;
		ret = e_snprintf(file, 32, "@%s", tmp + 1);
		if (ret <= 0)
			goto error;
	}

	if (pp->function)
		ret = e_snprintf(buf, MAX_CMDLEN, "%s%s%s%s%s", pp->function,
				 offs, pp->retprobe ? "%return" : "", line,
				 file);
	else
		ret = e_snprintf(buf, MAX_CMDLEN, "%s%s", file, line);
	if (ret <= 0)
		goto error;

	return buf;
error:
	die("Failed to synthesize perf probe point: %s", strerror(-ret));
}

#if 0
char *synthesize_perf_probe_command(struct perf_probe_event *pev)
{
	char *buf;
	int i, len, ret;

	buf = synthesize_perf_probe_point(&pev->point);
	if (!buf)
		return NULL;

	len = strlen(buf);
	for (i = 0; i < pev->nargs; i++) {
		ret = e_snprintf(&buf[len], MAX_CMDLEN - len, " %s",
				 pev->args[i].name);
		if (ret <= 0) {
			free(buf);
			return NULL;
		}
		len += ret;
	}

	return buf;
}
#endif

static int __synthesize_kprobe_trace_arg_ref(struct kprobe_trace_arg_ref *ref,
					     char **buf, size_t *buflen,
					     int depth)
{
	int ret;
	if (ref->next) {
		depth = __synthesize_kprobe_trace_arg_ref(ref->next, buf,
							 buflen, depth + 1);
		if (depth < 0)
			goto out;
	}

	ret = e_snprintf(*buf, *buflen, "%+ld(", ref->offset);
	if (ret < 0)
		depth = ret;
	else {
		*buf += ret;
		*buflen -= ret;
	}
out:
	return depth;

}

static int synthesize_kprobe_trace_arg(struct kprobe_trace_arg *arg,
				       char *buf, size_t buflen)
{
	int ret, depth = 0;
	char *tmp = buf;

	/* Argument name or separator */
	if (arg->name)
		ret = e_snprintf(buf, buflen, " %s=", arg->name);
	else
		ret = e_snprintf(buf, buflen, " ");
	if (ret < 0)
		return ret;
	buf += ret;
	buflen -= ret;

	/* Dereferencing arguments */
	if (arg->ref) {
		depth = __synthesize_kprobe_trace_arg_ref(arg->ref, &buf,
							  &buflen, 1);
		if (depth < 0)
			return depth;
	}

	/* Print argument value */
	ret = e_snprintf(buf, buflen, "%s", arg->value);
	if (ret < 0)
		return ret;
	buf += ret;
	buflen -= ret;

	/* Closing */
	while (depth--) {
		ret = e_snprintf(buf, buflen, ")");
		if (ret < 0)
			return ret;
		buf += ret;
		buflen -= ret;
	}

	return buf - tmp;
}

char *synthesize_kprobe_trace_command(struct kprobe_trace_event *tev)
{
	struct kprobe_trace_point *tp = &tev->point;
	char *buf;
	int i, len, ret;

	buf = xzalloc(MAX_CMDLEN);
	len = e_snprintf(buf, MAX_CMDLEN, "%c:%s/%s %s+%lu",
			 tp->retprobe ? 'r' : 'p',
			 tev->group, tev->event,
			 tp->symbol, tp->offset);
	if (len <= 0)
		goto error;

	for (i = 0; i < tev->nargs; i++) {
		ret = synthesize_kprobe_trace_arg(&tev->args[i], buf + len,
						  MAX_CMDLEN - len);
		if (ret <= 0)
			goto error;
		len += ret;
	}

	return buf;
error:
	free(buf);
	return NULL;
}

void convert_to_perf_probe_event(struct kprobe_trace_event *tev,
				 struct perf_probe_event *pev)
{
	char buf[64];
	int i;
#ifndef NO_DWARF_SUPPORT
	struct symbol *sym;
	int fd, ret = 0;

	sym = map__find_symbol_by_name(kmaps[MAP__FUNCTION],
				       tev->point.symbol, NULL);
	if (sym) {
		fd = open_vmlinux();
		ret = find_perf_probe_point(fd, sym->start + tev->point.offset,
					    &pev->point);
		close(fd);
	}
	if (ret <= 0) {
		pev->point.function = xstrdup(tev->point.symbol);
		pev->point.offset = tev->point.offset;
	}
#else
	/* Convert trace_point to probe_point */
	pev->point.function = xstrdup(tev->point.symbol);
	pev->point.offset = tev->point.offset;
#endif
	pev->point.retprobe = tev->point.retprobe;

	pev->event = xstrdup(tev->event);
	pev->group = xstrdup(tev->group);

	/* Convert trace_arg to probe_arg */
	pev->nargs = tev->nargs;
	pev->args = xzalloc(sizeof(struct perf_probe_arg) * pev->nargs);
	for (i = 0; i < tev->nargs; i++)
		if (tev->args[i].name)
			pev->args[i].name = xstrdup(tev->args[i].name);
		else {
			synthesize_kprobe_trace_arg(&tev->args[i], buf, 64);
			pev->args[i].name = xstrdup(buf);
		}
}

void clear_perf_probe_event(struct perf_probe_event *pev)
{
	struct perf_probe_point *pp = &pev->point;
	struct perf_probe_arg_field *field, *next;
	int i;

	if (pev->event)
		free(pev->event);
	if (pev->group)
		free(pev->group);
	if (pp->file)
		free(pp->file);
	if (pp->function)
		free(pp->function);
	if (pp->lazy_line)
		free(pp->lazy_line);
	for (i = 0; i < pev->nargs; i++) {
		if (pev->args[i].name)
			free(pev->args[i].name);
		field = pev->args[i].field;
		while (field) {
			next = field->next;
			if (field->name)
				free(field->name);
			free(field);
			field = next;
		}
	}
	if (pev->args)
		free(pev->args);
	memset(pev, 0, sizeof(*pev));
}

void clear_kprobe_trace_event(struct kprobe_trace_event *tev)
{
	struct kprobe_trace_arg_ref *ref, *next;
	int i;

	if (tev->event)
		free(tev->event);
	if (tev->group)
		free(tev->group);
	if (tev->point.symbol)
		free(tev->point.symbol);
	for (i = 0; i < tev->nargs; i++) {
		if (tev->args[i].name)
			free(tev->args[i].name);
		if (tev->args[i].value)
			free(tev->args[i].value);
		ref = tev->args[i].ref;
		while (ref) {
			next = ref->next;
			free(ref);
			ref = next;
		}
	}
	if (tev->args)
		free(tev->args);
	memset(tev, 0, sizeof(*tev));
}

static int open_kprobe_events(bool readwrite)
{
	char buf[PATH_MAX];
	int ret;

	ret = e_snprintf(buf, PATH_MAX, "%s/../kprobe_events", debugfs_path);
	if (ret < 0)
		die("Failed to make kprobe_events path.");

	if (readwrite && !probe_event_dry_run)
		ret = open(buf, O_RDWR, O_APPEND);
	else
		ret = open(buf, O_RDONLY, 0);

	if (ret < 0) {
		if (errno == ENOENT)
			die("kprobe_events file does not exist -"
			    " please rebuild with CONFIG_KPROBE_EVENT.");
		else
			die("Could not open kprobe_events file: %s",
			    strerror(errno));
	}
	return ret;
}

/* Get raw string list of current kprobe_events */
static struct strlist *get_kprobe_trace_command_rawlist(int fd)
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

/* Show an event */
static void show_perf_probe_event(struct perf_probe_event *pev)
{
	int i, ret;
	char buf[128];
	char *place;

	/* Synthesize only event probe point */
	place = synthesize_perf_probe_point(&pev->point);

	ret = e_snprintf(buf, 128, "%s:%s", pev->group, pev->event);
	if (ret < 0)
		die("Failed to copy event: %s", strerror(-ret));
	printf("  %-20s (on %s", buf, place);

	if (pev->nargs > 0) {
		printf(" with");
		for (i = 0; i < pev->nargs; i++) {
			synthesize_perf_probe_arg(&pev->args[i], buf, 128);
			printf(" %s", buf);
		}
	}
	printf(")\n");
	free(place);
}

/* List up current perf-probe events */
void show_perf_probe_events(void)
{
	int fd;
	struct kprobe_trace_event tev;
	struct perf_probe_event pev;
	struct strlist *rawlist;
	struct str_node *ent;

	setup_pager();
	init_vmlinux();

	memset(&tev, 0, sizeof(tev));
	memset(&pev, 0, sizeof(pev));

	fd = open_kprobe_events(false);
	rawlist = get_kprobe_trace_command_rawlist(fd);
	close(fd);

	strlist__for_each(ent, rawlist) {
		parse_kprobe_trace_command(ent->s, &tev);
		convert_to_perf_probe_event(&tev, &pev);
		/* Show an event */
		show_perf_probe_event(&pev);
		clear_perf_probe_event(&pev);
		clear_kprobe_trace_event(&tev);
	}

	strlist__delete(rawlist);
}

/* Get current perf-probe event names */
static struct strlist *get_kprobe_trace_event_names(int fd, bool include_group)
{
	char buf[128];
	struct strlist *sl, *rawlist;
	struct str_node *ent;
	struct kprobe_trace_event tev;

	memset(&tev, 0, sizeof(tev));

	rawlist = get_kprobe_trace_command_rawlist(fd);
	sl = strlist__new(true, NULL);
	strlist__for_each(ent, rawlist) {
		parse_kprobe_trace_command(ent->s, &tev);
		if (include_group) {
			if (e_snprintf(buf, 128, "%s:%s", tev.group,
				       tev.event) < 0)
				die("Failed to copy group:event name.");
			strlist__add(sl, buf);
		} else
			strlist__add(sl, tev.event);
		clear_kprobe_trace_event(&tev);
	}

	strlist__delete(rawlist);

	return sl;
}

static void write_kprobe_trace_event(int fd, struct kprobe_trace_event *tev)
{
	int ret;
	char *buf = synthesize_kprobe_trace_command(tev);

	pr_debug("Writing event: %s\n", buf);
	if (!probe_event_dry_run) {
		ret = write(fd, buf, strlen(buf));
		if (ret <= 0)
			die("Failed to write event: %s", strerror(errno));
	}
	free(buf);
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

static void __add_kprobe_trace_events(struct perf_probe_event *pev,
				      struct kprobe_trace_event *tevs,
				      int ntevs, bool allow_suffix)
{
	int i, fd;
	struct kprobe_trace_event *tev;
	char buf[64];
	const char *event, *group;
	struct strlist *namelist;

	fd = open_kprobe_events(true);
	/* Get current event names */
	namelist = get_kprobe_trace_event_names(fd, false);

	printf("Add new event%s\n", (ntevs > 1) ? "s:" : ":");
	for (i = 0; i < ntevs; i++) {
		tev = &tevs[i];
		if (pev->event)
			event = pev->event;
		else
			if (pev->point.function)
				event = pev->point.function;
			else
				event = tev->point.symbol;
		if (pev->group)
			group = pev->group;
		else
			group = PERFPROBE_GROUP;

		/* Get an unused new event name */
		get_new_event_name(buf, 64, event, namelist, allow_suffix);
		event = buf;

		tev->event = xstrdup(event);
		tev->group = xstrdup(group);
		write_kprobe_trace_event(fd, tev);
		/* Add added event name to namelist */
		strlist__add(namelist, event);

		/* Trick here - save current event/group */
		event = pev->event;
		group = pev->group;
		pev->event = tev->event;
		pev->group = tev->group;
		show_perf_probe_event(pev);
		/* Trick here - restore current event/group */
		pev->event = (char *)event;
		pev->group = (char *)group;

		/*
		 * Probes after the first probe which comes from same
		 * user input are always allowed to add suffix, because
		 * there might be several addresses corresponding to
		 * one code line.
		 */
		allow_suffix = true;
	}
	/* Show how to use the event. */
	printf("\nYou can now use it on all perf tools, such as:\n\n");
	printf("\tperf record -e %s:%s -a sleep 1\n\n", tev->group, tev->event);

	strlist__delete(namelist);
	close(fd);
}

static int convert_to_kprobe_trace_events(struct perf_probe_event *pev,
					  struct kprobe_trace_event **tevs)
{
	struct symbol *sym;
	bool need_dwarf;
#ifndef NO_DWARF_SUPPORT
	int fd;
#endif
	int ntevs = 0, i;
	struct kprobe_trace_event *tev;

	need_dwarf = perf_probe_event_need_dwarf(pev);

	if (need_dwarf)
#ifdef NO_DWARF_SUPPORT
		die("Debuginfo-analysis is not supported");
#else	/* !NO_DWARF_SUPPORT */
		pr_debug("Some probes require debuginfo.\n");

	fd = open_vmlinux();
	if (fd < 0) {
		if (need_dwarf)
			die("Could not open debuginfo file.");

		pr_debug("Could not open vmlinux/module file."
			 " Try to use symbols.\n");
		goto end_dwarf;
	}

	/* Searching probe points */
	ntevs = find_kprobe_trace_events(fd, pev, tevs);

	if (ntevs > 0)	/* Found */
		goto found;

	if (ntevs == 0)	/* No error but failed to find probe point. */
		die("Probe point '%s' not found. - probe not added.",
		    synthesize_perf_probe_point(&pev->point));

	/* Error path */
	if (need_dwarf) {
		if (ntevs == -ENOENT)
			pr_warning("No dwarf info found in the vmlinux - please rebuild with CONFIG_DEBUG_INFO=y.\n");
		die("Could not analyze debuginfo.");
	}
	pr_debug("An error occurred in debuginfo analysis."
		 " Try to use symbols.\n");

end_dwarf:
#endif /* !NO_DWARF_SUPPORT */

	/* Allocate trace event buffer */
	ntevs = 1;
	tev = *tevs = xzalloc(sizeof(struct kprobe_trace_event));

	/* Copy parameters */
	tev->point.symbol = xstrdup(pev->point.function);
	tev->point.offset = pev->point.offset;
	tev->nargs = pev->nargs;
	if (tev->nargs) {
		tev->args = xzalloc(sizeof(struct kprobe_trace_arg)
				    * tev->nargs);
		for (i = 0; i < tev->nargs; i++)
			tev->args[i].value = xstrdup(pev->args[i].name);
	}

	/* Currently just checking function name from symbol map */
	sym = map__find_symbol_by_name(kmaps[MAP__FUNCTION],
				       tev->point.symbol, NULL);
	if (!sym)
		die("Kernel symbol \'%s\' not found - probe not added.",
		    tev->point.symbol);
found:
	close(fd);
	return ntevs;
}

struct __event_package {
	struct perf_probe_event		*pev;
	struct kprobe_trace_event	*tevs;
	int				ntevs;
};

void add_perf_probe_events(struct perf_probe_event *pevs, int npevs,
			   bool force_add)
{
	int i;
	struct __event_package *pkgs;

	pkgs = xzalloc(sizeof(struct __event_package) * npevs);

	/* Init vmlinux path */
	init_vmlinux();

	/* Loop 1: convert all events */
	for (i = 0; i < npevs; i++) {
		pkgs[i].pev = &pevs[i];
		/* Convert with or without debuginfo */
		pkgs[i].ntevs = convert_to_kprobe_trace_events(pkgs[i].pev,
							       &pkgs[i].tevs);
	}

	/* Loop 2: add all events */
	for (i = 0; i < npevs; i++)
		__add_kprobe_trace_events(pkgs[i].pev, pkgs[i].tevs,
					  pkgs[i].ntevs, force_add);
	/* TODO: cleanup all trace events? */
}

static void __del_trace_kprobe_event(int fd, struct str_node *ent)
{
	char *p;
	char buf[128];
	int ret;

	/* Convert from perf-probe event to trace-kprobe event */
	if (e_snprintf(buf, 128, "-:%s", ent->s) < 0)
		die("Failed to copy event.");
	p = strchr(buf + 2, ':');
	if (!p)
		die("Internal error: %s should have ':' but not.", ent->s);
	*p = '/';

	pr_debug("Writing event: %s\n", buf);
	ret = write(fd, buf, strlen(buf));
	if (ret <= 0)
		die("Failed to write event: %s", strerror(errno));
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

void del_perf_probe_events(struct strlist *dellist)
{
	int fd;
	const char *group, *event;
	char *p, *str;
	struct str_node *ent;
	struct strlist *namelist;

	fd = open_kprobe_events(true);
	/* Get current event names */
	namelist = get_kprobe_trace_event_names(fd, true);

	strlist__for_each(ent, dellist) {
		str = xstrdup(ent->s);
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

#define LINEBUF_SIZE 256
#define NR_ADDITIONAL_LINES 2

static void show_one_line(FILE *fp, unsigned int l, bool skip, bool show_num)
{
	char buf[LINEBUF_SIZE];
	const char *color = PERF_COLOR_BLUE;

	if (fgets(buf, LINEBUF_SIZE, fp) == NULL)
		goto error;
	if (!skip) {
		if (show_num)
			fprintf(stdout, "%7u  %s", l, buf);
		else
			color_fprintf(stdout, color, "         %s", buf);
	}

	while (strlen(buf) == LINEBUF_SIZE - 1 &&
	       buf[LINEBUF_SIZE - 2] != '\n') {
		if (fgets(buf, LINEBUF_SIZE, fp) == NULL)
			goto error;
		if (!skip) {
			if (show_num)
				fprintf(stdout, "%s", buf);
			else
				color_fprintf(stdout, color, "%s", buf);
		}
	}
	return;
error:
	if (feof(fp))
		die("Source file is shorter than expected.");
	else
		die("File read error: %s", strerror(errno));
}

void show_line_range(struct line_range *lr)
{
	unsigned int l = 1;
	struct line_node *ln;
	FILE *fp;
	int fd, ret;

	/* Search a line range */
	init_vmlinux();
	fd = open_vmlinux();
	if (fd < 0)
		die("Could not open debuginfo file.");
	ret = find_line_range(fd, lr);
	if (ret <= 0)
		die("Source line is not found.\n");
	close(fd);

	setup_pager();

	if (lr->function)
		fprintf(stdout, "<%s:%d>\n", lr->function,
			lr->start - lr->offset);
	else
		fprintf(stdout, "<%s:%d>\n", lr->file, lr->start);

	fp = fopen(lr->path, "r");
	if (fp == NULL)
		die("Failed to open %s: %s", lr->path, strerror(errno));
	/* Skip to starting line number */
	while (l < lr->start)
		show_one_line(fp, l++, true, false);

	list_for_each_entry(ln, &lr->line_list, list) {
		while (ln->line > l)
			show_one_line(fp, (l++) - lr->offset, false, false);
		show_one_line(fp, (l++) - lr->offset, false, true);
	}

	if (lr->end == INT_MAX)
		lr->end = l + NR_ADDITIONAL_LINES;
	while (l < lr->end && !feof(fp))
		show_one_line(fp, (l++) - lr->offset, false, false);

	fclose(fp);
}


