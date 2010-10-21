/*
 * probe-event.c : perf-probe definition to probe_events format converter
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
#include "debugfs.h"
#include "trace-event.h"	/* For __unused */
#include "probe-event.h"
#include "probe-finder.h"

#define MAX_CMDLEN 256
#define MAX_PROBE_ARGS 128
#define PERFPROBE_GROUP "probe"

bool probe_event_dry_run;	/* Dry run flag */

#define semantic_error(msg ...) pr_err("Semantic error :" msg)

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

static char *synthesize_perf_probe_point(struct perf_probe_point *pp);
static struct machine machine;

/* Initialize symbol maps and path of vmlinux/modules */
static int init_vmlinux(void)
{
	int ret;

	symbol_conf.sort_by_name = true;
	if (symbol_conf.vmlinux_name == NULL)
		symbol_conf.try_vmlinux_path = true;
	else
		pr_debug("Use vmlinux: %s\n", symbol_conf.vmlinux_name);
	ret = symbol__init();
	if (ret < 0) {
		pr_debug("Failed to init symbol map.\n");
		goto out;
	}

	ret = machine__init(&machine, "", HOST_KERNEL_ID);
	if (ret < 0)
		goto out;

	if (machine__create_kernel_maps(&machine) < 0) {
		pr_debug("machine__create_kernel_maps ");
		goto out;
	}
out:
	if (ret < 0)
		pr_warning("Failed to init vmlinux path.\n");
	return ret;
}

static struct symbol *__find_kernel_function_by_name(const char *name,
						     struct map **mapp)
{
	return machine__find_kernel_function_by_name(&machine, name, mapp,
						     NULL);
}

const char *kernel_get_module_path(const char *module)
{
	struct dso *dso;

	if (module) {
		list_for_each_entry(dso, &machine.kernel_dsos, node) {
			if (strncmp(dso->short_name + 1, module,
				    dso->short_name_len - 2) == 0)
				goto found;
		}
		pr_debug("Failed to find module %s.\n", module);
		return NULL;
	} else {
		dso = machine.vmlinux_maps[MAP__FUNCTION]->dso;
		if (dso__load_vmlinux_path(dso,
			 machine.vmlinux_maps[MAP__FUNCTION], NULL) < 0) {
			pr_debug("Failed to load kernel map.\n");
			return NULL;
		}
	}
found:
	return dso->long_name;
}

#ifdef DWARF_SUPPORT
static int open_vmlinux(const char *module)
{
	const char *path = kernel_get_module_path(module);
	if (!path) {
		pr_err("Failed to find path of %s module", module ?: "kernel");
		return -ENOENT;
	}
	pr_debug("Try to open %s\n", path);
	return open(path, O_RDONLY);
}

/*
 * Convert trace point to probe point with debuginfo
 * Currently only handles kprobes.
 */
static int kprobe_convert_to_perf_probe(struct probe_trace_point *tp,
					struct perf_probe_point *pp)
{
	struct symbol *sym;
	struct map *map;
	u64 addr;
	int ret = -ENOENT;

	sym = __find_kernel_function_by_name(tp->symbol, &map);
	if (sym) {
		addr = map->unmap_ip(map, sym->start + tp->offset);
		pr_debug("try to find %s+%ld@%llx\n", tp->symbol,
			 tp->offset, addr);
		ret = find_perf_probe_point((unsigned long)addr, pp);
	}
	if (ret <= 0) {
		pr_debug("Failed to find corresponding probes from "
			 "debuginfo. Use kprobe event information.\n");
		pp->function = strdup(tp->symbol);
		if (pp->function == NULL)
			return -ENOMEM;
		pp->offset = tp->offset;
	}
	pp->retprobe = tp->retprobe;

	return 0;
}

/* Try to find perf_probe_event with debuginfo */
static int try_to_find_probe_trace_events(struct perf_probe_event *pev,
					   struct probe_trace_event **tevs,
					   int max_tevs, const char *module)
{
	bool need_dwarf = perf_probe_event_need_dwarf(pev);
	int fd, ntevs;

	fd = open_vmlinux(module);
	if (fd < 0) {
		if (need_dwarf) {
			pr_warning("Failed to open debuginfo file.\n");
			return fd;
		}
		pr_debug("Could not open vmlinux. Try to use symbols.\n");
		return 0;
	}

	/* Searching trace events corresponding to probe event */
	ntevs = find_probe_trace_events(fd, pev, tevs, max_tevs);
	close(fd);

	if (ntevs > 0) {	/* Succeeded to find trace events */
		pr_debug("find %d probe_trace_events.\n", ntevs);
		return ntevs;
	}

	if (ntevs == 0)	{	/* No error but failed to find probe point. */
		pr_warning("Probe point '%s' not found.\n",
			   synthesize_perf_probe_point(&pev->point));
		return -ENOENT;
	}
	/* Error path : ntevs < 0 */
	pr_debug("An error occurred in debuginfo analysis (%d).\n", ntevs);
	if (ntevs == -EBADF) {
		pr_warning("Warning: No dwarf info found in the vmlinux - "
			"please rebuild kernel with CONFIG_DEBUG_INFO=y.\n");
		if (!need_dwarf) {
			pr_debug("Trying to use symbols.\nn");
			return 0;
		}
	}
	return ntevs;
}

/*
 * Find a src file from a DWARF tag path. Prepend optional source path prefix
 * and chop off leading directories that do not exist. Result is passed back as
 * a newly allocated path on success.
 * Return 0 if file was found and readable, -errno otherwise.
 */
static int get_real_path(const char *raw_path, const char *comp_dir,
			 char **new_path)
{
	const char *prefix = symbol_conf.source_prefix;

	if (!prefix) {
		if (raw_path[0] != '/' && comp_dir)
			/* If not an absolute path, try to use comp_dir */
			prefix = comp_dir;
		else {
			if (access(raw_path, R_OK) == 0) {
				*new_path = strdup(raw_path);
				return 0;
			} else
				return -errno;
		}
	}

	*new_path = malloc((strlen(prefix) + strlen(raw_path) + 2));
	if (!*new_path)
		return -ENOMEM;

	for (;;) {
		sprintf(*new_path, "%s/%s", prefix, raw_path);

		if (access(*new_path, R_OK) == 0)
			return 0;

		if (!symbol_conf.source_prefix)
			/* In case of searching comp_dir, don't retry */
			return -errno;

		switch (errno) {
		case ENAMETOOLONG:
		case ENOENT:
		case EROFS:
		case EFAULT:
			raw_path = strchr(++raw_path, '/');
			if (!raw_path) {
				free(*new_path);
				*new_path = NULL;
				return -ENOENT;
			}
			continue;

		default:
			free(*new_path);
			*new_path = NULL;
			return -errno;
		}
	}
}

#define LINEBUF_SIZE 256
#define NR_ADDITIONAL_LINES 2

static int show_one_line(FILE *fp, int l, bool skip, bool show_num)
{
	char buf[LINEBUF_SIZE];
	const char *color = PERF_COLOR_BLUE;

	if (fgets(buf, LINEBUF_SIZE, fp) == NULL)
		goto error;
	if (!skip) {
		if (show_num)
			fprintf(stdout, "%7d  %s", l, buf);
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

	return 0;
error:
	if (feof(fp))
		pr_warning("Source file is shorter than expected.\n");
	else
		pr_warning("File read error: %s\n", strerror(errno));

	return -1;
}

/*
 * Show line-range always requires debuginfo to find source file and
 * line number.
 */
int show_line_range(struct line_range *lr, const char *module)
{
	int l = 1;
	struct line_node *ln;
	FILE *fp;
	int fd, ret;
	char *tmp;

	/* Search a line range */
	ret = init_vmlinux();
	if (ret < 0)
		return ret;

	fd = open_vmlinux(module);
	if (fd < 0) {
		pr_warning("Failed to open debuginfo file.\n");
		return fd;
	}

	ret = find_line_range(fd, lr);
	close(fd);
	if (ret == 0) {
		pr_warning("Specified source line is not found.\n");
		return -ENOENT;
	} else if (ret < 0) {
		pr_warning("Debuginfo analysis failed. (%d)\n", ret);
		return ret;
	}

	/* Convert source file path */
	tmp = lr->path;
	ret = get_real_path(tmp, lr->comp_dir, &lr->path);
	free(tmp);	/* Free old path */
	if (ret < 0) {
		pr_warning("Failed to find source file. (%d)\n", ret);
		return ret;
	}

	setup_pager();

	if (lr->function)
		fprintf(stdout, "<%s:%d>\n", lr->function,
			lr->start - lr->offset);
	else
		fprintf(stdout, "<%s:%d>\n", lr->file, lr->start);

	fp = fopen(lr->path, "r");
	if (fp == NULL) {
		pr_warning("Failed to open %s: %s\n", lr->path,
			   strerror(errno));
		return -errno;
	}
	/* Skip to starting line number */
	while (l < lr->start && ret >= 0)
		ret = show_one_line(fp, l++, true, false);
	if (ret < 0)
		goto end;

	list_for_each_entry(ln, &lr->line_list, list) {
		while (ln->line > l && ret >= 0)
			ret = show_one_line(fp, (l++) - lr->offset,
					    false, false);
		if (ret >= 0)
			ret = show_one_line(fp, (l++) - lr->offset,
					    false, true);
		if (ret < 0)
			goto end;
	}

	if (lr->end == INT_MAX)
		lr->end = l + NR_ADDITIONAL_LINES;
	while (l <= lr->end && !feof(fp) && ret >= 0)
		ret = show_one_line(fp, (l++) - lr->offset, false, false);
end:
	fclose(fp);
	return ret;
}

static int show_available_vars_at(int fd, struct perf_probe_event *pev,
				  int max_vls, bool externs)
{
	char *buf;
	int ret, i;
	struct str_node *node;
	struct variable_list *vls = NULL, *vl;

	buf = synthesize_perf_probe_point(&pev->point);
	if (!buf)
		return -EINVAL;
	pr_debug("Searching variables at %s\n", buf);

	ret = find_available_vars_at(fd, pev, &vls, max_vls, externs);
	if (ret > 0) {
		/* Some variables were found */
		fprintf(stdout, "Available variables at %s\n", buf);
		for (i = 0; i < ret; i++) {
			vl = &vls[i];
			/*
			 * A probe point might be converted to
			 * several trace points.
			 */
			fprintf(stdout, "\t@<%s+%lu>\n", vl->point.symbol,
				vl->point.offset);
			free(vl->point.symbol);
			if (vl->vars) {
				strlist__for_each(node, vl->vars)
					fprintf(stdout, "\t\t%s\n", node->s);
				strlist__delete(vl->vars);
			} else
				fprintf(stdout, "(No variables)\n");
		}
		free(vls);
	} else
		pr_err("Failed to find variables at %s (%d)\n", buf, ret);

	free(buf);
	return ret;
}

/* Show available variables on given probe point */
int show_available_vars(struct perf_probe_event *pevs, int npevs,
			int max_vls, const char *module, bool externs)
{
	int i, fd, ret = 0;

	ret = init_vmlinux();
	if (ret < 0)
		return ret;

	fd = open_vmlinux(module);
	if (fd < 0) {
		pr_warning("Failed to open debuginfo file.\n");
		return fd;
	}

	setup_pager();

	for (i = 0; i < npevs && ret >= 0; i++)
		ret = show_available_vars_at(fd, &pevs[i], max_vls, externs);

	close(fd);
	return ret;
}

#else	/* !DWARF_SUPPORT */

static int kprobe_convert_to_perf_probe(struct probe_trace_point *tp,
					struct perf_probe_point *pp)
{
	struct symbol *sym;

	sym = __find_kernel_function_by_name(tp->symbol, NULL);
	if (!sym) {
		pr_err("Failed to find symbol %s in kernel.\n", tp->symbol);
		return -ENOENT;
	}
	pp->function = strdup(tp->symbol);
	if (pp->function == NULL)
		return -ENOMEM;
	pp->offset = tp->offset;
	pp->retprobe = tp->retprobe;

	return 0;
}

static int try_to_find_probe_trace_events(struct perf_probe_event *pev,
				struct probe_trace_event **tevs __unused,
				int max_tevs __unused, const char *mod __unused)
{
	if (perf_probe_event_need_dwarf(pev)) {
		pr_warning("Debuginfo-analysis is not supported.\n");
		return -ENOSYS;
	}
	return 0;
}

int show_line_range(struct line_range *lr __unused, const char *module __unused)
{
	pr_warning("Debuginfo-analysis is not supported.\n");
	return -ENOSYS;
}

int show_available_vars(struct perf_probe_event *pevs __unused,
			int npevs __unused, int max_vls __unused,
			const char *module __unused, bool externs __unused)
{
	pr_warning("Debuginfo-analysis is not supported.\n");
	return -ENOSYS;
}
#endif

int parse_line_range_desc(const char *arg, struct line_range *lr)
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
		lr->start = (int)strtoul(ptr + 1, &tmp, 0);
		if (*tmp == '+') {
			lr->end = lr->start + (int)strtoul(tmp + 1, &tmp, 0);
			lr->end--;	/*
					 * Adjust the number of lines here.
					 * If the number of lines == 1, the
					 * the end of line should be equal to
					 * the start of line.
					 */
		} else if (*tmp == '-')
			lr->end = (int)strtoul(tmp + 1, &tmp, 0);
		else
			lr->end = INT_MAX;
		pr_debug("Line range is %d to %d\n", lr->start, lr->end);
		if (lr->start > lr->end) {
			semantic_error("Start line must be smaller"
				       " than end line.\n");
			return -EINVAL;
		}
		if (*tmp != '\0') {
			semantic_error("Tailing with invalid character '%d'.\n",
				       *tmp);
			return -EINVAL;
		}
		tmp = strndup(arg, (ptr - arg));
	} else {
		tmp = strdup(arg);
		lr->end = INT_MAX;
	}

	if (tmp == NULL)
		return -ENOMEM;

	if (strchr(tmp, '.'))
		lr->file = tmp;
	else
		lr->function = tmp;

	return 0;
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
static int parse_perf_probe_point(char *arg, struct perf_probe_event *pev)
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
		if (strchr(arg, ':')) {
			semantic_error("Group name is not supported yet.\n");
			return -ENOTSUP;
		}
		if (!check_event_name(arg)) {
			semantic_error("%s is bad for event name -it must "
				       "follow C symbol-naming rule.\n", arg);
			return -EINVAL;
		}
		pev->event = strdup(arg);
		if (pev->event == NULL)
			return -ENOMEM;
		pev->group = NULL;
		arg = tmp;
	}

	ptr = strpbrk(arg, ";:+@%");
	if (ptr) {
		nc = *ptr;
		*ptr++ = '\0';
	}

	tmp = strdup(arg);
	if (tmp == NULL)
		return -ENOMEM;

	/* Check arg is function or file and copy it */
	if (strchr(tmp, '.'))	/* File */
		pp->file = tmp;
	else			/* Function */
		pp->function = tmp;

	/* Parse other options */
	while (ptr) {
		arg = ptr;
		c = nc;
		if (c == ';') {	/* Lazy pattern must be the last part */
			pp->lazy_line = strdup(arg);
			if (pp->lazy_line == NULL)
				return -ENOMEM;
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
			if (*tmp != '\0') {
				semantic_error("There is non-digit char"
					       " in line number.\n");
				return -EINVAL;
			}
			break;
		case '+':	/* Byte offset from a symbol */
			pp->offset = strtoul(arg, &tmp, 0);
			if (*tmp != '\0') {
				semantic_error("There is non-digit character"
						" in offset.\n");
				return -EINVAL;
			}
			break;
		case '@':	/* File name */
			if (pp->file) {
				semantic_error("SRC@SRC is not allowed.\n");
				return -EINVAL;
			}
			pp->file = strdup(arg);
			if (pp->file == NULL)
				return -ENOMEM;
			break;
		case '%':	/* Probe places */
			if (strcmp(arg, "return") == 0) {
				pp->retprobe = 1;
			} else {	/* Others not supported yet */
				semantic_error("%%%s is not supported.\n", arg);
				return -ENOTSUP;
			}
			break;
		default:	/* Buggy case */
			pr_err("This program has a bug at %s:%d.\n",
				__FILE__, __LINE__);
			return -ENOTSUP;
			break;
		}
	}

	/* Exclusion check */
	if (pp->lazy_line && pp->line) {
		semantic_error("Lazy pattern can't be used with line number.");
		return -EINVAL;
	}

	if (pp->lazy_line && pp->offset) {
		semantic_error("Lazy pattern can't be used with offset.");
		return -EINVAL;
	}

	if (pp->line && pp->offset) {
		semantic_error("Offset can't be used with line number.");
		return -EINVAL;
	}

	if (!pp->line && !pp->lazy_line && pp->file && !pp->function) {
		semantic_error("File always requires line number or "
			       "lazy pattern.");
		return -EINVAL;
	}

	if (pp->offset && !pp->function) {
		semantic_error("Offset requires an entry function.");
		return -EINVAL;
	}

	if (pp->retprobe && !pp->function) {
		semantic_error("Return probe requires an entry function.");
		return -EINVAL;
	}

	if ((pp->offset || pp->line || pp->lazy_line) && pp->retprobe) {
		semantic_error("Offset/Line/Lazy pattern can't be used with "
			       "return probe.");
		return -EINVAL;
	}

	pr_debug("symbol:%s file:%s line:%d offset:%lu return:%d lazy:%s\n",
		 pp->function, pp->file, pp->line, pp->offset, pp->retprobe,
		 pp->lazy_line);
	return 0;
}

/* Parse perf-probe event argument */
static int parse_perf_probe_arg(char *str, struct perf_probe_arg *arg)
{
	char *tmp, *goodname;
	struct perf_probe_arg_field **fieldp;

	pr_debug("parsing arg: %s into ", str);

	tmp = strchr(str, '=');
	if (tmp) {
		arg->name = strndup(str, tmp - str);
		if (arg->name == NULL)
			return -ENOMEM;
		pr_debug("name:%s ", arg->name);
		str = tmp + 1;
	}

	tmp = strchr(str, ':');
	if (tmp) {	/* Type setting */
		*tmp = '\0';
		arg->type = strdup(tmp + 1);
		if (arg->type == NULL)
			return -ENOMEM;
		pr_debug("type:%s ", arg->type);
	}

	tmp = strpbrk(str, "-.[");
	if (!is_c_varname(str) || !tmp) {
		/* A variable, register, symbol or special value */
		arg->var = strdup(str);
		if (arg->var == NULL)
			return -ENOMEM;
		pr_debug("%s\n", arg->var);
		return 0;
	}

	/* Structure fields or array element */
	arg->var = strndup(str, tmp - str);
	if (arg->var == NULL)
		return -ENOMEM;
	goodname = arg->var;
	pr_debug("%s, ", arg->var);
	fieldp = &arg->field;

	do {
		*fieldp = zalloc(sizeof(struct perf_probe_arg_field));
		if (*fieldp == NULL)
			return -ENOMEM;
		if (*tmp == '[') {	/* Array */
			str = tmp;
			(*fieldp)->index = strtol(str + 1, &tmp, 0);
			(*fieldp)->ref = true;
			if (*tmp != ']' || tmp == str + 1) {
				semantic_error("Array index must be a"
						" number.\n");
				return -EINVAL;
			}
			tmp++;
			if (*tmp == '\0')
				tmp = NULL;
		} else {		/* Structure */
			if (*tmp == '.') {
				str = tmp + 1;
				(*fieldp)->ref = false;
			} else if (tmp[1] == '>') {
				str = tmp + 2;
				(*fieldp)->ref = true;
			} else {
				semantic_error("Argument parse error: %s\n",
					       str);
				return -EINVAL;
			}
			tmp = strpbrk(str, "-.[");
		}
		if (tmp) {
			(*fieldp)->name = strndup(str, tmp - str);
			if ((*fieldp)->name == NULL)
				return -ENOMEM;
			if (*str != '[')
				goodname = (*fieldp)->name;
			pr_debug("%s(%d), ", (*fieldp)->name, (*fieldp)->ref);
			fieldp = &(*fieldp)->next;
		}
	} while (tmp);
	(*fieldp)->name = strdup(str);
	if ((*fieldp)->name == NULL)
		return -ENOMEM;
	if (*str != '[')
		goodname = (*fieldp)->name;
	pr_debug("%s(%d)\n", (*fieldp)->name, (*fieldp)->ref);

	/* If no name is specified, set the last field name (not array index)*/
	if (!arg->name) {
		arg->name = strdup(goodname);
		if (arg->name == NULL)
			return -ENOMEM;
	}
	return 0;
}

/* Parse perf-probe event command */
int parse_perf_probe_command(const char *cmd, struct perf_probe_event *pev)
{
	char **argv;
	int argc, i, ret = 0;

	argv = argv_split(cmd, &argc);
	if (!argv) {
		pr_debug("Failed to split arguments.\n");
		return -ENOMEM;
	}
	if (argc - 1 > MAX_PROBE_ARGS) {
		semantic_error("Too many probe arguments (%d).\n", argc - 1);
		ret = -ERANGE;
		goto out;
	}
	/* Parse probe point */
	ret = parse_perf_probe_point(argv[0], pev);
	if (ret < 0)
		goto out;

	/* Copy arguments and ensure return probe has no C argument */
	pev->nargs = argc - 1;
	pev->args = zalloc(sizeof(struct perf_probe_arg) * pev->nargs);
	if (pev->args == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < pev->nargs && ret >= 0; i++) {
		ret = parse_perf_probe_arg(argv[i + 1], &pev->args[i]);
		if (ret >= 0 &&
		    is_c_varname(pev->args[i].var) && pev->point.retprobe) {
			semantic_error("You can't specify local variable for"
				       " kretprobe.\n");
			ret = -EINVAL;
		}
	}
out:
	argv_free(argv);

	return ret;
}

/* Return true if this perf_probe_event requires debuginfo */
bool perf_probe_event_need_dwarf(struct perf_probe_event *pev)
{
	int i;

	if (pev->point.file || pev->point.line || pev->point.lazy_line)
		return true;

	for (i = 0; i < pev->nargs; i++)
		if (is_c_varname(pev->args[i].var))
			return true;

	return false;
}

/* Parse probe_events event into struct probe_point */
static int parse_probe_trace_command(const char *cmd,
					struct probe_trace_event *tev)
{
	struct probe_trace_point *tp = &tev->point;
	char pr;
	char *p;
	int ret, i, argc;
	char **argv;

	pr_debug("Parsing probe_events: %s\n", cmd);
	argv = argv_split(cmd, &argc);
	if (!argv) {
		pr_debug("Failed to split arguments.\n");
		return -ENOMEM;
	}
	if (argc < 2) {
		semantic_error("Too few probe arguments.\n");
		ret = -ERANGE;
		goto out;
	}

	/* Scan event and group name. */
	ret = sscanf(argv[0], "%c:%a[^/ \t]/%a[^ \t]",
		     &pr, (float *)(void *)&tev->group,
		     (float *)(void *)&tev->event);
	if (ret != 3) {
		semantic_error("Failed to parse event name: %s\n", argv[0]);
		ret = -EINVAL;
		goto out;
	}
	pr_debug("Group:%s Event:%s probe:%c\n", tev->group, tev->event, pr);

	tp->retprobe = (pr == 'r');

	/* Scan function name and offset */
	ret = sscanf(argv[1], "%a[^+]+%lu", (float *)(void *)&tp->symbol,
		     &tp->offset);
	if (ret == 1)
		tp->offset = 0;

	tev->nargs = argc - 2;
	tev->args = zalloc(sizeof(struct probe_trace_arg) * tev->nargs);
	if (tev->args == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < tev->nargs; i++) {
		p = strchr(argv[i + 2], '=');
		if (p)	/* We don't need which register is assigned. */
			*p++ = '\0';
		else
			p = argv[i + 2];
		tev->args[i].name = strdup(argv[i + 2]);
		/* TODO: parse regs and offset */
		tev->args[i].value = strdup(p);
		if (tev->args[i].name == NULL || tev->args[i].value == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	ret = 0;
out:
	argv_free(argv);
	return ret;
}

/* Compose only probe arg */
int synthesize_perf_probe_arg(struct perf_probe_arg *pa, char *buf, size_t len)
{
	struct perf_probe_arg_field *field = pa->field;
	int ret;
	char *tmp = buf;

	if (pa->name && pa->var)
		ret = e_snprintf(tmp, len, "%s=%s", pa->name, pa->var);
	else
		ret = e_snprintf(tmp, len, "%s", pa->name ? pa->name : pa->var);
	if (ret <= 0)
		goto error;
	tmp += ret;
	len -= ret;

	while (field) {
		if (field->name[0] == '[')
			ret = e_snprintf(tmp, len, "%s", field->name);
		else
			ret = e_snprintf(tmp, len, "%s%s",
					 field->ref ? "->" : ".", field->name);
		if (ret <= 0)
			goto error;
		tmp += ret;
		len -= ret;
		field = field->next;
	}

	if (pa->type) {
		ret = e_snprintf(tmp, len, ":%s", pa->type);
		if (ret <= 0)
			goto error;
		tmp += ret;
		len -= ret;
	}

	return tmp - buf;
error:
	pr_debug("Failed to synthesize perf probe argument: %s",
		 strerror(-ret));
	return ret;
}

/* Compose only probe point (not argument) */
static char *synthesize_perf_probe_point(struct perf_probe_point *pp)
{
	char *buf, *tmp;
	char offs[32] = "", line[32] = "", file[32] = "";
	int ret, len;

	buf = zalloc(MAX_CMDLEN);
	if (buf == NULL) {
		ret = -ENOMEM;
		goto error;
	}
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
		len = strlen(pp->file) - 31;
		if (len < 0)
			len = 0;
		tmp = strchr(pp->file + len, '/');
		if (!tmp)
			tmp = pp->file + len;
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
	pr_debug("Failed to synthesize perf probe point: %s",
		 strerror(-ret));
	if (buf)
		free(buf);
	return NULL;
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

static int __synthesize_probe_trace_arg_ref(struct probe_trace_arg_ref *ref,
					     char **buf, size_t *buflen,
					     int depth)
{
	int ret;
	if (ref->next) {
		depth = __synthesize_probe_trace_arg_ref(ref->next, buf,
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

static int synthesize_probe_trace_arg(struct probe_trace_arg *arg,
				       char *buf, size_t buflen)
{
	struct probe_trace_arg_ref *ref = arg->ref;
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

	/* Special case: @XXX */
	if (arg->value[0] == '@' && arg->ref)
			ref = ref->next;

	/* Dereferencing arguments */
	if (ref) {
		depth = __synthesize_probe_trace_arg_ref(ref, &buf,
							  &buflen, 1);
		if (depth < 0)
			return depth;
	}

	/* Print argument value */
	if (arg->value[0] == '@' && arg->ref)
		ret = e_snprintf(buf, buflen, "%s%+ld", arg->value,
				 arg->ref->offset);
	else
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
	/* Print argument type */
	if (arg->type) {
		ret = e_snprintf(buf, buflen, ":%s", arg->type);
		if (ret <= 0)
			return ret;
		buf += ret;
	}

	return buf - tmp;
}

char *synthesize_probe_trace_command(struct probe_trace_event *tev)
{
	struct probe_trace_point *tp = &tev->point;
	char *buf;
	int i, len, ret;

	buf = zalloc(MAX_CMDLEN);
	if (buf == NULL)
		return NULL;

	len = e_snprintf(buf, MAX_CMDLEN, "%c:%s/%s %s+%lu",
			 tp->retprobe ? 'r' : 'p',
			 tev->group, tev->event,
			 tp->symbol, tp->offset);
	if (len <= 0)
		goto error;

	for (i = 0; i < tev->nargs; i++) {
		ret = synthesize_probe_trace_arg(&tev->args[i], buf + len,
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

static int convert_to_perf_probe_event(struct probe_trace_event *tev,
				       struct perf_probe_event *pev)
{
	char buf[64] = "";
	int i, ret;

	/* Convert event/group name */
	pev->event = strdup(tev->event);
	pev->group = strdup(tev->group);
	if (pev->event == NULL || pev->group == NULL)
		return -ENOMEM;

	/* Convert trace_point to probe_point */
	ret = kprobe_convert_to_perf_probe(&tev->point, &pev->point);
	if (ret < 0)
		return ret;

	/* Convert trace_arg to probe_arg */
	pev->nargs = tev->nargs;
	pev->args = zalloc(sizeof(struct perf_probe_arg) * pev->nargs);
	if (pev->args == NULL)
		return -ENOMEM;
	for (i = 0; i < tev->nargs && ret >= 0; i++) {
		if (tev->args[i].name)
			pev->args[i].name = strdup(tev->args[i].name);
		else {
			ret = synthesize_probe_trace_arg(&tev->args[i],
							  buf, 64);
			pev->args[i].name = strdup(buf);
		}
		if (pev->args[i].name == NULL && ret >= 0)
			ret = -ENOMEM;
	}

	if (ret < 0)
		clear_perf_probe_event(pev);

	return ret;
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
		if (pev->args[i].var)
			free(pev->args[i].var);
		if (pev->args[i].type)
			free(pev->args[i].type);
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

static void clear_probe_trace_event(struct probe_trace_event *tev)
{
	struct probe_trace_arg_ref *ref, *next;
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
		if (tev->args[i].type)
			free(tev->args[i].type);
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
	const char *__debugfs;
	int ret;

	__debugfs = debugfs_find_mountpoint();
	if (__debugfs == NULL) {
		pr_warning("Debugfs is not mounted.\n");
		return -ENOENT;
	}

	ret = e_snprintf(buf, PATH_MAX, "%stracing/kprobe_events", __debugfs);
	if (ret >= 0) {
		pr_debug("Opening %s write=%d\n", buf, readwrite);
		if (readwrite && !probe_event_dry_run)
			ret = open(buf, O_RDWR, O_APPEND);
		else
			ret = open(buf, O_RDONLY, 0);
	}

	if (ret < 0) {
		if (errno == ENOENT)
			pr_warning("kprobe_events file does not exist - please"
				 " rebuild kernel with CONFIG_KPROBE_EVENT.\n");
		else
			pr_warning("Failed to open kprobe_events file: %s\n",
				   strerror(errno));
	}
	return ret;
}

/* Get raw string list of current kprobe_events */
static struct strlist *get_probe_trace_command_rawlist(int fd)
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
		if (ret < 0) {
			pr_debug("strlist__add failed: %s\n", strerror(-ret));
			strlist__delete(sl);
			return NULL;
		}
	}
	fclose(fp);

	return sl;
}

/* Show an event */
static int show_perf_probe_event(struct perf_probe_event *pev)
{
	int i, ret;
	char buf[128];
	char *place;

	/* Synthesize only event probe point */
	place = synthesize_perf_probe_point(&pev->point);
	if (!place)
		return -EINVAL;

	ret = e_snprintf(buf, 128, "%s:%s", pev->group, pev->event);
	if (ret < 0)
		return ret;

	printf("  %-20s (on %s", buf, place);

	if (pev->nargs > 0) {
		printf(" with");
		for (i = 0; i < pev->nargs; i++) {
			ret = synthesize_perf_probe_arg(&pev->args[i],
							buf, 128);
			if (ret < 0)
				break;
			printf(" %s", buf);
		}
	}
	printf(")\n");
	free(place);
	return ret;
}

/* List up current perf-probe events */
int show_perf_probe_events(void)
{
	int fd, ret;
	struct probe_trace_event tev;
	struct perf_probe_event pev;
	struct strlist *rawlist;
	struct str_node *ent;

	setup_pager();
	ret = init_vmlinux();
	if (ret < 0)
		return ret;

	memset(&tev, 0, sizeof(tev));
	memset(&pev, 0, sizeof(pev));

	fd = open_kprobe_events(false);
	if (fd < 0)
		return fd;

	rawlist = get_probe_trace_command_rawlist(fd);
	close(fd);
	if (!rawlist)
		return -ENOENT;

	strlist__for_each(ent, rawlist) {
		ret = parse_probe_trace_command(ent->s, &tev);
		if (ret >= 0) {
			ret = convert_to_perf_probe_event(&tev, &pev);
			if (ret >= 0)
				ret = show_perf_probe_event(&pev);
		}
		clear_perf_probe_event(&pev);
		clear_probe_trace_event(&tev);
		if (ret < 0)
			break;
	}
	strlist__delete(rawlist);

	return ret;
}

/* Get current perf-probe event names */
static struct strlist *get_probe_trace_event_names(int fd, bool include_group)
{
	char buf[128];
	struct strlist *sl, *rawlist;
	struct str_node *ent;
	struct probe_trace_event tev;
	int ret = 0;

	memset(&tev, 0, sizeof(tev));
	rawlist = get_probe_trace_command_rawlist(fd);
	sl = strlist__new(true, NULL);
	strlist__for_each(ent, rawlist) {
		ret = parse_probe_trace_command(ent->s, &tev);
		if (ret < 0)
			break;
		if (include_group) {
			ret = e_snprintf(buf, 128, "%s:%s", tev.group,
					tev.event);
			if (ret >= 0)
				ret = strlist__add(sl, buf);
		} else
			ret = strlist__add(sl, tev.event);
		clear_probe_trace_event(&tev);
		if (ret < 0)
			break;
	}
	strlist__delete(rawlist);

	if (ret < 0) {
		strlist__delete(sl);
		return NULL;
	}
	return sl;
}

static int write_probe_trace_event(int fd, struct probe_trace_event *tev)
{
	int ret = 0;
	char *buf = synthesize_probe_trace_command(tev);

	if (!buf) {
		pr_debug("Failed to synthesize probe trace event.\n");
		return -EINVAL;
	}

	pr_debug("Writing event: %s\n", buf);
	if (!probe_event_dry_run) {
		ret = write(fd, buf, strlen(buf));
		if (ret <= 0)
			pr_warning("Failed to write event: %s\n",
				   strerror(errno));
	}
	free(buf);
	return ret;
}

static int get_new_event_name(char *buf, size_t len, const char *base,
			      struct strlist *namelist, bool allow_suffix)
{
	int i, ret;

	/* Try no suffix */
	ret = e_snprintf(buf, len, "%s", base);
	if (ret < 0) {
		pr_debug("snprintf() failed: %s\n", strerror(-ret));
		return ret;
	}
	if (!strlist__has_entry(namelist, buf))
		return 0;

	if (!allow_suffix) {
		pr_warning("Error: event \"%s\" already exists. "
			   "(Use -f to force duplicates.)\n", base);
		return -EEXIST;
	}

	/* Try to add suffix */
	for (i = 1; i < MAX_EVENT_INDEX; i++) {
		ret = e_snprintf(buf, len, "%s_%d", base, i);
		if (ret < 0) {
			pr_debug("snprintf() failed: %s\n", strerror(-ret));
			return ret;
		}
		if (!strlist__has_entry(namelist, buf))
			break;
	}
	if (i == MAX_EVENT_INDEX) {
		pr_warning("Too many events are on the same function.\n");
		ret = -ERANGE;
	}

	return ret;
}

static int __add_probe_trace_events(struct perf_probe_event *pev,
				     struct probe_trace_event *tevs,
				     int ntevs, bool allow_suffix)
{
	int i, fd, ret;
	struct probe_trace_event *tev = NULL;
	char buf[64];
	const char *event, *group;
	struct strlist *namelist;

	fd = open_kprobe_events(true);
	if (fd < 0)
		return fd;
	/* Get current event names */
	namelist = get_probe_trace_event_names(fd, false);
	if (!namelist) {
		pr_debug("Failed to get current event list.\n");
		return -EIO;
	}

	ret = 0;
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
		ret = get_new_event_name(buf, 64, event,
					 namelist, allow_suffix);
		if (ret < 0)
			break;
		event = buf;

		tev->event = strdup(event);
		tev->group = strdup(group);
		if (tev->event == NULL || tev->group == NULL) {
			ret = -ENOMEM;
			break;
		}
		ret = write_probe_trace_event(fd, tev);
		if (ret < 0)
			break;
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

	if (ret >= 0) {
		/* Show how to use the event. */
		printf("\nYou can now use it on all perf tools, such as:\n\n");
		printf("\tperf record -e %s:%s -aR sleep 1\n\n", tev->group,
			 tev->event);
	}

	strlist__delete(namelist);
	close(fd);
	return ret;
}

static int convert_to_probe_trace_events(struct perf_probe_event *pev,
					  struct probe_trace_event **tevs,
					  int max_tevs, const char *module)
{
	struct symbol *sym;
	int ret = 0, i;
	struct probe_trace_event *tev;

	/* Convert perf_probe_event with debuginfo */
	ret = try_to_find_probe_trace_events(pev, tevs, max_tevs, module);
	if (ret != 0)
		return ret;

	/* Allocate trace event buffer */
	tev = *tevs = zalloc(sizeof(struct probe_trace_event));
	if (tev == NULL)
		return -ENOMEM;

	/* Copy parameters */
	tev->point.symbol = strdup(pev->point.function);
	if (tev->point.symbol == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	tev->point.offset = pev->point.offset;
	tev->point.retprobe = pev->point.retprobe;
	tev->nargs = pev->nargs;
	if (tev->nargs) {
		tev->args = zalloc(sizeof(struct probe_trace_arg)
				   * tev->nargs);
		if (tev->args == NULL) {
			ret = -ENOMEM;
			goto error;
		}
		for (i = 0; i < tev->nargs; i++) {
			if (pev->args[i].name) {
				tev->args[i].name = strdup(pev->args[i].name);
				if (tev->args[i].name == NULL) {
					ret = -ENOMEM;
					goto error;
				}
			}
			tev->args[i].value = strdup(pev->args[i].var);
			if (tev->args[i].value == NULL) {
				ret = -ENOMEM;
				goto error;
			}
			if (pev->args[i].type) {
				tev->args[i].type = strdup(pev->args[i].type);
				if (tev->args[i].type == NULL) {
					ret = -ENOMEM;
					goto error;
				}
			}
		}
	}

	/* Currently just checking function name from symbol map */
	sym = __find_kernel_function_by_name(tev->point.symbol, NULL);
	if (!sym) {
		pr_warning("Kernel symbol \'%s\' not found.\n",
			   tev->point.symbol);
		ret = -ENOENT;
		goto error;
	}

	return 1;
error:
	clear_probe_trace_event(tev);
	free(tev);
	*tevs = NULL;
	return ret;
}

struct __event_package {
	struct perf_probe_event		*pev;
	struct probe_trace_event	*tevs;
	int				ntevs;
};

int add_perf_probe_events(struct perf_probe_event *pevs, int npevs,
			  int max_tevs, const char *module, bool force_add)
{
	int i, j, ret;
	struct __event_package *pkgs;

	pkgs = zalloc(sizeof(struct __event_package) * npevs);
	if (pkgs == NULL)
		return -ENOMEM;

	/* Init vmlinux path */
	ret = init_vmlinux();
	if (ret < 0) {
		free(pkgs);
		return ret;
	}

	/* Loop 1: convert all events */
	for (i = 0; i < npevs; i++) {
		pkgs[i].pev = &pevs[i];
		/* Convert with or without debuginfo */
		ret  = convert_to_probe_trace_events(pkgs[i].pev,
						     &pkgs[i].tevs,
						     max_tevs,
						     module);
		if (ret < 0)
			goto end;
		pkgs[i].ntevs = ret;
	}

	/* Loop 2: add all events */
	for (i = 0; i < npevs && ret >= 0; i++)
		ret = __add_probe_trace_events(pkgs[i].pev, pkgs[i].tevs,
						pkgs[i].ntevs, force_add);
end:
	/* Loop 3: cleanup and free trace events  */
	for (i = 0; i < npevs; i++) {
		for (j = 0; j < pkgs[i].ntevs; j++)
			clear_probe_trace_event(&pkgs[i].tevs[j]);
		free(pkgs[i].tevs);
	}
	free(pkgs);

	return ret;
}

static int __del_trace_probe_event(int fd, struct str_node *ent)
{
	char *p;
	char buf[128];
	int ret;

	/* Convert from perf-probe event to trace-probe event */
	ret = e_snprintf(buf, 128, "-:%s", ent->s);
	if (ret < 0)
		goto error;

	p = strchr(buf + 2, ':');
	if (!p) {
		pr_debug("Internal error: %s should have ':' but not.\n",
			 ent->s);
		ret = -ENOTSUP;
		goto error;
	}
	*p = '/';

	pr_debug("Writing event: %s\n", buf);
	ret = write(fd, buf, strlen(buf));
	if (ret < 0)
		goto error;

	printf("Remove event: %s\n", ent->s);
	return 0;
error:
	pr_warning("Failed to delete event: %s\n", strerror(-ret));
	return ret;
}

static int del_trace_probe_event(int fd, const char *group,
				  const char *event, struct strlist *namelist)
{
	char buf[128];
	struct str_node *ent, *n;
	int found = 0, ret = 0;

	ret = e_snprintf(buf, 128, "%s:%s", group, event);
	if (ret < 0) {
		pr_err("Failed to copy event.");
		return ret;
	}

	if (strpbrk(buf, "*?")) { /* Glob-exp */
		strlist__for_each_safe(ent, n, namelist)
			if (strglobmatch(ent->s, buf)) {
				found++;
				ret = __del_trace_probe_event(fd, ent);
				if (ret < 0)
					break;
				strlist__remove(namelist, ent);
			}
	} else {
		ent = strlist__find(namelist, buf);
		if (ent) {
			found++;
			ret = __del_trace_probe_event(fd, ent);
			if (ret >= 0)
				strlist__remove(namelist, ent);
		}
	}
	if (found == 0 && ret >= 0)
		pr_info("Info: Event \"%s\" does not exist.\n", buf);

	return ret;
}

int del_perf_probe_events(struct strlist *dellist)
{
	int fd, ret = 0;
	const char *group, *event;
	char *p, *str;
	struct str_node *ent;
	struct strlist *namelist;

	fd = open_kprobe_events(true);
	if (fd < 0)
		return fd;

	/* Get current event names */
	namelist = get_probe_trace_event_names(fd, true);
	if (namelist == NULL)
		return -EINVAL;

	strlist__for_each(ent, dellist) {
		str = strdup(ent->s);
		if (str == NULL) {
			ret = -ENOMEM;
			break;
		}
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
		ret = del_trace_probe_event(fd, group, event, namelist);
		free(str);
		if (ret < 0)
			break;
	}
	strlist__delete(namelist);
	close(fd);

	return ret;
}

