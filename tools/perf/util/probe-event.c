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
#include <elf.h>

#include "util.h"
#include "event.h"
#include "strlist.h"
#include "debug.h"
#include "cache.h"
#include "color.h"
#include "symbol.h"
#include "thread.h"
#include <api/fs/debugfs.h>
#include "trace-event.h"	/* For __maybe_unused */
#include "probe-event.h"
#include "probe-finder.h"
#include "session.h"

#define MAX_CMDLEN 256
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
static void clear_probe_trace_event(struct probe_trace_event *tev);
static struct machine *host_machine;

/* Initialize symbol maps and path of vmlinux/modules */
static int init_symbol_maps(bool user_only)
{
	int ret;

	symbol_conf.sort_by_name = true;
	ret = symbol__init();
	if (ret < 0) {
		pr_debug("Failed to init symbol map.\n");
		goto out;
	}

	if (host_machine || user_only)	/* already initialized */
		return 0;

	if (symbol_conf.vmlinux_name)
		pr_debug("Use vmlinux: %s\n", symbol_conf.vmlinux_name);

	host_machine = machine__new_host();
	if (!host_machine) {
		pr_debug("machine__new_host() failed.\n");
		symbol__exit();
		ret = -1;
	}
out:
	if (ret < 0)
		pr_warning("Failed to init vmlinux path.\n");
	return ret;
}

static void exit_symbol_maps(void)
{
	if (host_machine) {
		machine__delete(host_machine);
		host_machine = NULL;
	}
	symbol__exit();
}

static struct symbol *__find_kernel_function_by_name(const char *name,
						     struct map **mapp)
{
	return machine__find_kernel_function_by_name(host_machine, name, mapp,
						     NULL);
}

static struct symbol *__find_kernel_function(u64 addr, struct map **mapp)
{
	return machine__find_kernel_function(host_machine, addr, mapp, NULL);
}

static struct ref_reloc_sym *kernel_get_ref_reloc_sym(void)
{
	/* kmap->ref_reloc_sym should be set if host_machine is initialized */
	struct kmap *kmap;

	if (map__load(host_machine->vmlinux_maps[MAP__FUNCTION], NULL) < 0)
		return NULL;

	kmap = map__kmap(host_machine->vmlinux_maps[MAP__FUNCTION]);
	return kmap->ref_reloc_sym;
}

static u64 kernel_get_symbol_address_by_name(const char *name, bool reloc)
{
	struct ref_reloc_sym *reloc_sym;
	struct symbol *sym;
	struct map *map;

	/* ref_reloc_sym is just a label. Need a special fix*/
	reloc_sym = kernel_get_ref_reloc_sym();
	if (reloc_sym && strcmp(name, reloc_sym->name) == 0)
		return (reloc) ? reloc_sym->addr : reloc_sym->unrelocated_addr;
	else {
		sym = __find_kernel_function_by_name(name, &map);
		if (sym)
			return map->unmap_ip(map, sym->start) -
				(reloc) ? 0 : map->reloc;
	}
	return 0;
}

static struct map *kernel_get_module_map(const char *module)
{
	struct rb_node *nd;
	struct map_groups *grp = &host_machine->kmaps;

	/* A file path -- this is an offline module */
	if (module && strchr(module, '/'))
		return machine__new_module(host_machine, 0, module);

	if (!module)
		module = "kernel";

	for (nd = rb_first(&grp->maps[MAP__FUNCTION]); nd; nd = rb_next(nd)) {
		struct map *pos = rb_entry(nd, struct map, rb_node);
		if (strncmp(pos->dso->short_name + 1, module,
			    pos->dso->short_name_len - 2) == 0) {
			return pos;
		}
	}
	return NULL;
}

static struct dso *kernel_get_module_dso(const char *module)
{
	struct dso *dso;
	struct map *map;
	const char *vmlinux_name;

	if (module) {
		list_for_each_entry(dso, &host_machine->kernel_dsos, node) {
			if (strncmp(dso->short_name + 1, module,
				    dso->short_name_len - 2) == 0)
				goto found;
		}
		pr_debug("Failed to find module %s.\n", module);
		return NULL;
	}

	map = host_machine->vmlinux_maps[MAP__FUNCTION];
	dso = map->dso;

	vmlinux_name = symbol_conf.vmlinux_name;
	if (vmlinux_name) {
		if (dso__load_vmlinux(dso, map, vmlinux_name, false, NULL) <= 0)
			return NULL;
	} else {
		if (dso__load_vmlinux_path(dso, map, NULL) <= 0) {
			pr_debug("Failed to load kernel map.\n");
			return NULL;
		}
	}
found:
	return dso;
}

const char *kernel_get_module_path(const char *module)
{
	struct dso *dso = kernel_get_module_dso(module);
	return (dso) ? dso->long_name : NULL;
}

static int convert_exec_to_group(const char *exec, char **result)
{
	char *ptr1, *ptr2, *exec_copy;
	char buf[64];
	int ret;

	exec_copy = strdup(exec);
	if (!exec_copy)
		return -ENOMEM;

	ptr1 = basename(exec_copy);
	if (!ptr1) {
		ret = -EINVAL;
		goto out;
	}

	ptr2 = strpbrk(ptr1, "-._");
	if (ptr2)
		*ptr2 = '\0';
	ret = e_snprintf(buf, 64, "%s_%s", PERFPROBE_GROUP, ptr1);
	if (ret < 0)
		goto out;

	*result = strdup(buf);
	ret = *result ? 0 : -ENOMEM;

out:
	free(exec_copy);
	return ret;
}

static void clear_probe_trace_events(struct probe_trace_event *tevs, int ntevs)
{
	int i;

	for (i = 0; i < ntevs; i++)
		clear_probe_trace_event(tevs + i);
}

#ifdef HAVE_DWARF_SUPPORT

/* Open new debuginfo of given module */
static struct debuginfo *open_debuginfo(const char *module)
{
	const char *path = module;

	if (!module || !strchr(module, '/')) {
		path = kernel_get_module_path(module);
		if (!path) {
			pr_err("Failed to find path of %s module.\n",
			       module ?: "kernel");
			return NULL;
		}
	}
	return debuginfo__new(path);
}

static int get_text_start_address(const char *exec, unsigned long *address)
{
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	int fd, ret = -ENOENT;

	fd = open(exec, O_RDONLY);
	if (fd < 0)
		return -errno;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL)
		return -EINVAL;

	if (gelf_getehdr(elf, &ehdr) == NULL)
		goto out;

	if (!elf_section_by_name(elf, &ehdr, &shdr, ".text", NULL))
		goto out;

	*address = shdr.sh_addr - shdr.sh_offset;
	ret = 0;
out:
	elf_end(elf);
	return ret;
}

/*
 * Convert trace point to probe point with debuginfo
 */
static int find_perf_probe_point_from_dwarf(struct probe_trace_point *tp,
					    struct perf_probe_point *pp,
					    bool is_kprobe)
{
	struct debuginfo *dinfo = NULL;
	unsigned long stext = 0;
	u64 addr = tp->address;
	int ret = -ENOENT;

	/* convert the address to dwarf address */
	if (!is_kprobe) {
		if (!addr) {
			ret = -EINVAL;
			goto error;
		}
		ret = get_text_start_address(tp->module, &stext);
		if (ret < 0)
			goto error;
		addr += stext;
	} else {
		addr = kernel_get_symbol_address_by_name(tp->symbol, false);
		if (addr == 0)
			goto error;
		addr += tp->offset;
	}

	pr_debug("try to find information at %" PRIx64 " in %s\n", addr,
		 tp->module ? : "kernel");

	dinfo = open_debuginfo(tp->module);
	if (dinfo) {
		ret = debuginfo__find_probe_point(dinfo,
						 (unsigned long)addr, pp);
		debuginfo__delete(dinfo);
	} else {
		pr_debug("Failed to open debuginfo at 0x%" PRIx64 "\n", addr);
		ret = -ENOENT;
	}

	if (ret > 0) {
		pp->retprobe = tp->retprobe;
		return 0;
	}
error:
	pr_debug("Failed to find corresponding probes from debuginfo.\n");
	return ret ? : -ENOENT;
}

static int add_exec_to_probe_trace_events(struct probe_trace_event *tevs,
					  int ntevs, const char *exec)
{
	int i, ret = 0;
	unsigned long stext = 0;

	if (!exec)
		return 0;

	ret = get_text_start_address(exec, &stext);
	if (ret < 0)
		return ret;

	for (i = 0; i < ntevs && ret >= 0; i++) {
		/* point.address is the addres of point.symbol + point.offset */
		tevs[i].point.address -= stext;
		tevs[i].point.module = strdup(exec);
		if (!tevs[i].point.module) {
			ret = -ENOMEM;
			break;
		}
		tevs[i].uprobes = true;
	}

	return ret;
}

static int add_module_to_probe_trace_events(struct probe_trace_event *tevs,
					    int ntevs, const char *module)
{
	int i, ret = 0;
	char *tmp;

	if (!module)
		return 0;

	tmp = strrchr(module, '/');
	if (tmp) {
		/* This is a module path -- get the module name */
		module = strdup(tmp + 1);
		if (!module)
			return -ENOMEM;
		tmp = strchr(module, '.');
		if (tmp)
			*tmp = '\0';
		tmp = (char *)module;	/* For free() */
	}

	for (i = 0; i < ntevs; i++) {
		tevs[i].point.module = strdup(module);
		if (!tevs[i].point.module) {
			ret = -ENOMEM;
			break;
		}
	}

	free(tmp);
	return ret;
}

/* Post processing the probe events */
static int post_process_probe_trace_events(struct probe_trace_event *tevs,
					   int ntevs, const char *module,
					   bool uprobe)
{
	struct ref_reloc_sym *reloc_sym;
	char *tmp;
	int i;

	if (uprobe)
		return add_exec_to_probe_trace_events(tevs, ntevs, module);

	/* Note that currently ref_reloc_sym based probe is not for drivers */
	if (module)
		return add_module_to_probe_trace_events(tevs, ntevs, module);

	reloc_sym = kernel_get_ref_reloc_sym();
	if (!reloc_sym) {
		pr_warning("Relocated base symbol is not found!\n");
		return -EINVAL;
	}

	for (i = 0; i < ntevs; i++) {
		if (tevs[i].point.address) {
			tmp = strdup(reloc_sym->name);
			if (!tmp)
				return -ENOMEM;
			free(tevs[i].point.symbol);
			tevs[i].point.symbol = tmp;
			tevs[i].point.offset = tevs[i].point.address -
					       reloc_sym->unrelocated_addr;
		}
	}
	return 0;
}

/* Try to find perf_probe_event with debuginfo */
static int try_to_find_probe_trace_events(struct perf_probe_event *pev,
					  struct probe_trace_event **tevs,
					  int max_tevs, const char *target)
{
	bool need_dwarf = perf_probe_event_need_dwarf(pev);
	struct debuginfo *dinfo;
	int ntevs, ret = 0;

	dinfo = open_debuginfo(target);

	if (!dinfo) {
		if (need_dwarf) {
			pr_warning("Failed to open debuginfo file.\n");
			return -ENOENT;
		}
		pr_debug("Could not open debuginfo. Try to use symbols.\n");
		return 0;
	}

	pr_debug("Try to find probe point from debuginfo.\n");
	/* Searching trace events corresponding to a probe event */
	ntevs = debuginfo__find_trace_events(dinfo, pev, tevs, max_tevs);

	debuginfo__delete(dinfo);

	if (ntevs > 0) {	/* Succeeded to find trace events */
		pr_debug("Found %d probe_trace_events.\n", ntevs);
		ret = post_process_probe_trace_events(*tevs, ntevs,
							target, pev->uprobes);
		if (ret < 0) {
			clear_probe_trace_events(*tevs, ntevs);
			zfree(tevs);
		}
		return ret < 0 ? ret : ntevs;
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
			pr_debug("Trying to use symbols.\n");
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
				zfree(new_path);
				return -ENOENT;
			}
			continue;

		default:
			zfree(new_path);
			return -errno;
		}
	}
}

#define LINEBUF_SIZE 256
#define NR_ADDITIONAL_LINES 2

static int __show_one_line(FILE *fp, int l, bool skip, bool show_num)
{
	char buf[LINEBUF_SIZE];
	const char *color = show_num ? "" : PERF_COLOR_BLUE;
	const char *prefix = NULL;

	do {
		if (fgets(buf, LINEBUF_SIZE, fp) == NULL)
			goto error;
		if (skip)
			continue;
		if (!prefix) {
			prefix = show_num ? "%7d  " : "         ";
			color_fprintf(stdout, color, prefix, l);
		}
		color_fprintf(stdout, color, "%s", buf);

	} while (strchr(buf, '\n') == NULL);

	return 1;
error:
	if (ferror(fp)) {
		pr_warning("File read error: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int _show_one_line(FILE *fp, int l, bool skip, bool show_num)
{
	int rv = __show_one_line(fp, l, skip, show_num);
	if (rv == 0) {
		pr_warning("Source file is shorter than expected.\n");
		rv = -1;
	}
	return rv;
}

#define show_one_line_with_num(f,l)	_show_one_line(f,l,false,true)
#define show_one_line(f,l)		_show_one_line(f,l,false,false)
#define skip_one_line(f,l)		_show_one_line(f,l,true,false)
#define show_one_line_or_eof(f,l)	__show_one_line(f,l,false,false)

/*
 * Show line-range always requires debuginfo to find source file and
 * line number.
 */
static int __show_line_range(struct line_range *lr, const char *module)
{
	int l = 1;
	struct int_node *ln;
	struct debuginfo *dinfo;
	FILE *fp;
	int ret;
	char *tmp;

	/* Search a line range */
	dinfo = open_debuginfo(module);
	if (!dinfo) {
		pr_warning("Failed to open debuginfo file.\n");
		return -ENOENT;
	}

	ret = debuginfo__find_line_range(dinfo, lr);
	debuginfo__delete(dinfo);
	if (ret == 0 || ret == -ENOENT) {
		pr_warning("Specified source line is not found.\n");
		return -ENOENT;
	} else if (ret < 0) {
		pr_warning("Debuginfo analysis failed.\n");
		return ret;
	}

	/* Convert source file path */
	tmp = lr->path;
	ret = get_real_path(tmp, lr->comp_dir, &lr->path);
	free(tmp);	/* Free old path */
	if (ret < 0) {
		pr_warning("Failed to find source file path.\n");
		return ret;
	}

	setup_pager();

	if (lr->function)
		fprintf(stdout, "<%s@%s:%d>\n", lr->function, lr->path,
			lr->start - lr->offset);
	else
		fprintf(stdout, "<%s:%d>\n", lr->path, lr->start);

	fp = fopen(lr->path, "r");
	if (fp == NULL) {
		pr_warning("Failed to open %s: %s\n", lr->path,
			   strerror(errno));
		return -errno;
	}
	/* Skip to starting line number */
	while (l < lr->start) {
		ret = skip_one_line(fp, l++);
		if (ret < 0)
			goto end;
	}

	intlist__for_each(ln, lr->line_list) {
		for (; ln->i > l; l++) {
			ret = show_one_line(fp, l - lr->offset);
			if (ret < 0)
				goto end;
		}
		ret = show_one_line_with_num(fp, l++ - lr->offset);
		if (ret < 0)
			goto end;
	}

	if (lr->end == INT_MAX)
		lr->end = l + NR_ADDITIONAL_LINES;
	while (l <= lr->end) {
		ret = show_one_line_or_eof(fp, l++ - lr->offset);
		if (ret <= 0)
			break;
	}
end:
	fclose(fp);
	return ret;
}

int show_line_range(struct line_range *lr, const char *module)
{
	int ret;

	ret = init_symbol_maps(false);
	if (ret < 0)
		return ret;
	ret = __show_line_range(lr, module);
	exit_symbol_maps();

	return ret;
}

static int show_available_vars_at(struct debuginfo *dinfo,
				  struct perf_probe_event *pev,
				  int max_vls, struct strfilter *_filter,
				  bool externs)
{
	char *buf;
	int ret, i, nvars;
	struct str_node *node;
	struct variable_list *vls = NULL, *vl;
	const char *var;

	buf = synthesize_perf_probe_point(&pev->point);
	if (!buf)
		return -EINVAL;
	pr_debug("Searching variables at %s\n", buf);

	ret = debuginfo__find_available_vars_at(dinfo, pev, &vls,
						max_vls, externs);
	if (ret <= 0) {
		if (ret == 0 || ret == -ENOENT) {
			pr_err("Failed to find the address of %s\n", buf);
			ret = -ENOENT;
		} else
			pr_warning("Debuginfo analysis failed.\n");
		goto end;
	}

	/* Some variables are found */
	fprintf(stdout, "Available variables at %s\n", buf);
	for (i = 0; i < ret; i++) {
		vl = &vls[i];
		/*
		 * A probe point might be converted to
		 * several trace points.
		 */
		fprintf(stdout, "\t@<%s+%lu>\n", vl->point.symbol,
			vl->point.offset);
		zfree(&vl->point.symbol);
		nvars = 0;
		if (vl->vars) {
			strlist__for_each(node, vl->vars) {
				var = strchr(node->s, '\t') + 1;
				if (strfilter__compare(_filter, var)) {
					fprintf(stdout, "\t\t%s\n", node->s);
					nvars++;
				}
			}
			strlist__delete(vl->vars);
		}
		if (nvars == 0)
			fprintf(stdout, "\t\t(No matched variables)\n");
	}
	free(vls);
end:
	free(buf);
	return ret;
}

/* Show available variables on given probe point */
int show_available_vars(struct perf_probe_event *pevs, int npevs,
			int max_vls, const char *module,
			struct strfilter *_filter, bool externs)
{
	int i, ret = 0;
	struct debuginfo *dinfo;

	ret = init_symbol_maps(false);
	if (ret < 0)
		return ret;

	dinfo = open_debuginfo(module);
	if (!dinfo) {
		pr_warning("Failed to open debuginfo file.\n");
		ret = -ENOENT;
		goto out;
	}

	setup_pager();

	for (i = 0; i < npevs && ret >= 0; i++)
		ret = show_available_vars_at(dinfo, &pevs[i], max_vls, _filter,
					     externs);

	debuginfo__delete(dinfo);
out:
	exit_symbol_maps();
	return ret;
}

#else	/* !HAVE_DWARF_SUPPORT */

static int
find_perf_probe_point_from_dwarf(struct probe_trace_point *tp __maybe_unused,
				 struct perf_probe_point *pp __maybe_unused,
				 bool is_kprobe __maybe_unused)
{
	return -ENOSYS;
}

static int try_to_find_probe_trace_events(struct perf_probe_event *pev,
				struct probe_trace_event **tevs __maybe_unused,
				int max_tevs __maybe_unused,
				const char *target __maybe_unused)
{
	if (perf_probe_event_need_dwarf(pev)) {
		pr_warning("Debuginfo-analysis is not supported.\n");
		return -ENOSYS;
	}

	return 0;
}

int show_line_range(struct line_range *lr __maybe_unused,
		    const char *module __maybe_unused)
{
	pr_warning("Debuginfo-analysis is not supported.\n");
	return -ENOSYS;
}

int show_available_vars(struct perf_probe_event *pevs __maybe_unused,
			int npevs __maybe_unused, int max_vls __maybe_unused,
			const char *module __maybe_unused,
			struct strfilter *filter __maybe_unused,
			bool externs __maybe_unused)
{
	pr_warning("Debuginfo-analysis is not supported.\n");
	return -ENOSYS;
}
#endif

void line_range__clear(struct line_range *lr)
{
	free(lr->function);
	free(lr->file);
	free(lr->path);
	free(lr->comp_dir);
	intlist__delete(lr->line_list);
	memset(lr, 0, sizeof(*lr));
}

int line_range__init(struct line_range *lr)
{
	memset(lr, 0, sizeof(*lr));
	lr->line_list = intlist__new(NULL);
	if (!lr->line_list)
		return -ENOMEM;
	else
		return 0;
}

static int parse_line_num(char **ptr, int *val, const char *what)
{
	const char *start = *ptr;

	errno = 0;
	*val = strtol(*ptr, ptr, 0);
	if (errno || *ptr == start) {
		semantic_error("'%s' is not a valid number.\n", what);
		return -EINVAL;
	}
	return 0;
}

/*
 * Stuff 'lr' according to the line range described by 'arg'.
 * The line range syntax is described by:
 *
 *         SRC[:SLN[+NUM|-ELN]]
 *         FNC[@SRC][:SLN[+NUM|-ELN]]
 */
int parse_line_range_desc(const char *arg, struct line_range *lr)
{
	char *range, *file, *name = strdup(arg);
	int err;

	if (!name)
		return -ENOMEM;

	lr->start = 0;
	lr->end = INT_MAX;

	range = strchr(name, ':');
	if (range) {
		*range++ = '\0';

		err = parse_line_num(&range, &lr->start, "start line");
		if (err)
			goto err;

		if (*range == '+' || *range == '-') {
			const char c = *range++;

			err = parse_line_num(&range, &lr->end, "end line");
			if (err)
				goto err;

			if (c == '+') {
				lr->end += lr->start;
				/*
				 * Adjust the number of lines here.
				 * If the number of lines == 1, the
				 * the end of line should be equal to
				 * the start of line.
				 */
				lr->end--;
			}
		}

		pr_debug("Line range is %d to %d\n", lr->start, lr->end);

		err = -EINVAL;
		if (lr->start > lr->end) {
			semantic_error("Start line must be smaller"
				       " than end line.\n");
			goto err;
		}
		if (*range != '\0') {
			semantic_error("Tailing with invalid str '%s'.\n", range);
			goto err;
		}
	}

	file = strchr(name, '@');
	if (file) {
		*file = '\0';
		lr->file = strdup(++file);
		if (lr->file == NULL) {
			err = -ENOMEM;
			goto err;
		}
		lr->function = name;
	} else if (strchr(name, '.'))
		lr->file = name;
	else
		lr->function = name;

	return 0;
err:
	free(name);
	return err;
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
		semantic_error("Lazy pattern can't be used with"
			       " line number.\n");
		return -EINVAL;
	}

	if (pp->lazy_line && pp->offset) {
		semantic_error("Lazy pattern can't be used with offset.\n");
		return -EINVAL;
	}

	if (pp->line && pp->offset) {
		semantic_error("Offset can't be used with line number.\n");
		return -EINVAL;
	}

	if (!pp->line && !pp->lazy_line && pp->file && !pp->function) {
		semantic_error("File always requires line number or "
			       "lazy pattern.\n");
		return -EINVAL;
	}

	if (pp->offset && !pp->function) {
		semantic_error("Offset requires an entry function.\n");
		return -EINVAL;
	}

	if (pp->retprobe && !pp->function) {
		semantic_error("Return probe requires an entry function.\n");
		return -EINVAL;
	}

	if ((pp->offset || pp->line || pp->lazy_line) && pp->retprobe) {
		semantic_error("Offset/Line/Lazy pattern can't be used with "
			       "return probe.\n");
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
	char *argv0_str = NULL, *fmt, *fmt1_str, *fmt2_str, *fmt3_str;
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
	argv0_str = strdup(argv[0]);
	if (argv0_str == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	fmt1_str = strtok_r(argv0_str, ":", &fmt);
	fmt2_str = strtok_r(NULL, "/", &fmt);
	fmt3_str = strtok_r(NULL, " \t", &fmt);
	if (fmt1_str == NULL || strlen(fmt1_str) != 1 || fmt2_str == NULL
	    || fmt3_str == NULL) {
		semantic_error("Failed to parse event name: %s\n", argv[0]);
		ret = -EINVAL;
		goto out;
	}
	pr = fmt1_str[0];
	tev->group = strdup(fmt2_str);
	tev->event = strdup(fmt3_str);
	if (tev->group == NULL || tev->event == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	pr_debug("Group:%s Event:%s probe:%c\n", tev->group, tev->event, pr);

	tp->retprobe = (pr == 'r');

	/* Scan module name(if there), function name and offset */
	p = strchr(argv[1], ':');
	if (p) {
		tp->module = strndup(argv[1], p - argv[1]);
		p++;
	} else
		p = argv[1];
	fmt1_str = strtok_r(p, "+", &fmt);
	if (fmt1_str[0] == '0')	/* only the address started with 0x */
		tp->address = strtoul(fmt1_str, NULL, 0);
	else {
		/* Only the symbol-based probe has offset */
		tp->symbol = strdup(fmt1_str);
		if (tp->symbol == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		fmt2_str = strtok_r(NULL, "", &fmt);
		if (fmt2_str == NULL)
			tp->offset = 0;
		else
			tp->offset = strtoul(fmt2_str, NULL, 10);
	}

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
	free(argv0_str);
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
	pr_debug("Failed to synthesize perf probe argument: %s\n",
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
		tmp = pp->file;
		len = strlen(tmp);
		if (len > 30) {
			tmp = strchr(pp->file + len - 30, '/');
			tmp = tmp ? tmp + 1 : pp->file + len - 30;
		}
		ret = e_snprintf(file, 32, "@%s", tmp);
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
	pr_debug("Failed to synthesize perf probe point: %s\n",
		 strerror(-ret));
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

	len = e_snprintf(buf, MAX_CMDLEN, "%c:%s/%s ", tp->retprobe ? 'r' : 'p',
			 tev->group, tev->event);
	if (len <= 0)
		goto error;

	/* Uprobes must have tp->address and tp->module */
	if (tev->uprobes && (!tp->address || !tp->module))
		goto error;

	/* Use the tp->address for uprobes */
	if (tev->uprobes)
		ret = e_snprintf(buf + len, MAX_CMDLEN - len, "%s:0x%lx",
				 tp->module, tp->address);
	else
		ret = e_snprintf(buf + len, MAX_CMDLEN - len, "%s%s%s+%lu",
				 tp->module ?: "", tp->module ? ":" : "",
				 tp->symbol, tp->offset);

	if (ret <= 0)
		goto error;
	len += ret;

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

static int find_perf_probe_point_from_map(struct probe_trace_point *tp,
					  struct perf_probe_point *pp,
					  bool is_kprobe)
{
	struct symbol *sym = NULL;
	struct map *map;
	u64 addr;
	int ret = -ENOENT;

	if (!is_kprobe) {
		map = dso__new_map(tp->module);
		if (!map)
			goto out;
		addr = tp->address;
		sym = map__find_symbol(map, addr, NULL);
	} else {
		addr = kernel_get_symbol_address_by_name(tp->symbol, true);
		if (addr) {
			addr += tp->offset;
			sym = __find_kernel_function(addr, &map);
		}
	}
	if (!sym)
		goto out;

	pp->retprobe = tp->retprobe;
	pp->offset = addr - map->unmap_ip(map, sym->start);
	pp->function = strdup(sym->name);
	ret = pp->function ? 0 : -ENOMEM;

out:
	if (map && !is_kprobe) {
		dso__delete(map->dso);
		map__delete(map);
	}

	return ret;
}

static int convert_to_perf_probe_point(struct probe_trace_point *tp,
					struct perf_probe_point *pp,
					bool is_kprobe)
{
	char buf[128];
	int ret;

	ret = find_perf_probe_point_from_dwarf(tp, pp, is_kprobe);
	if (!ret)
		return 0;
	ret = find_perf_probe_point_from_map(tp, pp, is_kprobe);
	if (!ret)
		return 0;

	pr_debug("Failed to find probe point from both of dwarf and map.\n");

	if (tp->symbol) {
		pp->function = strdup(tp->symbol);
		pp->offset = tp->offset;
	} else if (!tp->module && !is_kprobe) {
		ret = e_snprintf(buf, 128, "0x%" PRIx64, (u64)tp->address);
		if (ret < 0)
			return ret;
		pp->function = strdup(buf);
		pp->offset = 0;
	}
	if (pp->function == NULL)
		return -ENOMEM;

	pp->retprobe = tp->retprobe;

	return 0;
}

static int convert_to_perf_probe_event(struct probe_trace_event *tev,
			       struct perf_probe_event *pev, bool is_kprobe)
{
	char buf[64] = "";
	int i, ret;

	/* Convert event/group name */
	pev->event = strdup(tev->event);
	pev->group = strdup(tev->group);
	if (pev->event == NULL || pev->group == NULL)
		return -ENOMEM;

	/* Convert trace_point to probe_point */
	ret = convert_to_perf_probe_point(&tev->point, &pev->point, is_kprobe);
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

	free(pev->event);
	free(pev->group);
	free(pp->file);
	free(pp->function);
	free(pp->lazy_line);

	for (i = 0; i < pev->nargs; i++) {
		free(pev->args[i].name);
		free(pev->args[i].var);
		free(pev->args[i].type);
		field = pev->args[i].field;
		while (field) {
			next = field->next;
			zfree(&field->name);
			free(field);
			field = next;
		}
	}
	free(pev->args);
	memset(pev, 0, sizeof(*pev));
}

static void clear_probe_trace_event(struct probe_trace_event *tev)
{
	struct probe_trace_arg_ref *ref, *next;
	int i;

	free(tev->event);
	free(tev->group);
	free(tev->point.symbol);
	free(tev->point.module);
	for (i = 0; i < tev->nargs; i++) {
		free(tev->args[i].name);
		free(tev->args[i].value);
		free(tev->args[i].type);
		ref = tev->args[i].ref;
		while (ref) {
			next = ref->next;
			free(ref);
			ref = next;
		}
	}
	free(tev->args);
	memset(tev, 0, sizeof(*tev));
}

static void print_warn_msg(const char *file, bool is_kprobe)
{

	if (errno == ENOENT) {
		const char *config;

		if (!is_kprobe)
			config = "CONFIG_UPROBE_EVENTS";
		else
			config = "CONFIG_KPROBE_EVENTS";

		pr_warning("%s file does not exist - please rebuild kernel"
				" with %s.\n", file, config);
	} else
		pr_warning("Failed to open %s file: %s\n", file,
				strerror(errno));
}

static int open_probe_events(const char *trace_file, bool readwrite,
				bool is_kprobe)
{
	char buf[PATH_MAX];
	const char *__debugfs;
	int ret;

	__debugfs = debugfs_find_mountpoint();
	if (__debugfs == NULL) {
		pr_warning("Debugfs is not mounted.\n");
		return -ENOENT;
	}

	ret = e_snprintf(buf, PATH_MAX, "%s/%s", __debugfs, trace_file);
	if (ret >= 0) {
		pr_debug("Opening %s write=%d\n", buf, readwrite);
		if (readwrite && !probe_event_dry_run)
			ret = open(buf, O_RDWR, O_APPEND);
		else
			ret = open(buf, O_RDONLY, 0);

		if (ret < 0)
			print_warn_msg(buf, is_kprobe);
	}
	return ret;
}

static int open_kprobe_events(bool readwrite)
{
	return open_probe_events("tracing/kprobe_events", readwrite, true);
}

static int open_uprobe_events(bool readwrite)
{
	return open_probe_events("tracing/uprobe_events", readwrite, false);
}

/* Get raw string list of current kprobe_events  or uprobe_events */
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
static int show_perf_probe_event(struct perf_probe_event *pev,
				 const char *module)
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
	if (module)
		printf(" in %s", module);

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

static int __show_perf_probe_events(int fd, bool is_kprobe)
{
	int ret = 0;
	struct probe_trace_event tev;
	struct perf_probe_event pev;
	struct strlist *rawlist;
	struct str_node *ent;

	memset(&tev, 0, sizeof(tev));
	memset(&pev, 0, sizeof(pev));

	rawlist = get_probe_trace_command_rawlist(fd);
	if (!rawlist)
		return -ENOENT;

	strlist__for_each(ent, rawlist) {
		ret = parse_probe_trace_command(ent->s, &tev);
		if (ret >= 0) {
			ret = convert_to_perf_probe_event(&tev, &pev,
								is_kprobe);
			if (ret >= 0)
				ret = show_perf_probe_event(&pev,
							    tev.point.module);
		}
		clear_perf_probe_event(&pev);
		clear_probe_trace_event(&tev);
		if (ret < 0)
			break;
	}
	strlist__delete(rawlist);

	return ret;
}

/* List up current perf-probe events */
int show_perf_probe_events(void)
{
	int fd, ret;

	setup_pager();
	fd = open_kprobe_events(false);

	if (fd < 0)
		return fd;

	ret = init_symbol_maps(false);
	if (ret < 0)
		return ret;

	ret = __show_perf_probe_events(fd, true);
	close(fd);

	fd = open_uprobe_events(false);
	if (fd >= 0) {
		ret = __show_perf_probe_events(fd, false);
		close(fd);
	}

	exit_symbol_maps();
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

	if (pev->uprobes)
		fd = open_uprobe_events(true);
	else
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
	printf("Added new event%s\n", (ntevs > 1) ? "s:" : ":");
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
		show_perf_probe_event(pev, tev->point.module);
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
		printf("\nYou can now use it in all perf tools, such as:\n\n");
		printf("\tperf record -e %s:%s -aR sleep 1\n\n", tev->group,
			 tev->event);
	}

	strlist__delete(namelist);
	close(fd);
	return ret;
}

static char *looking_function_name;
static int num_matched_functions;

static int probe_function_filter(struct map *map __maybe_unused,
				      struct symbol *sym)
{
	if ((sym->binding == STB_GLOBAL || sym->binding == STB_LOCAL) &&
	    strcmp(looking_function_name, sym->name) == 0) {
		num_matched_functions++;
		return 0;
	}
	return 1;
}

#define strdup_or_goto(str, label)	\
	({ char *__p = strdup(str); if (!__p) goto label; __p; })

/*
 * Find probe function addresses from map.
 * Return an error or the number of found probe_trace_event
 */
static int find_probe_trace_events_from_map(struct perf_probe_event *pev,
					    struct probe_trace_event **tevs,
					    int max_tevs, const char *target)
{
	struct map *map = NULL;
	struct kmap *kmap = NULL;
	struct ref_reloc_sym *reloc_sym = NULL;
	struct symbol *sym;
	struct rb_node *nd;
	struct probe_trace_event *tev;
	struct perf_probe_point *pp = &pev->point;
	struct probe_trace_point *tp;
	int ret, i;

	/* Init maps of given executable or kernel */
	if (pev->uprobes)
		map = dso__new_map(target);
	else
		map = kernel_get_module_map(target);
	if (!map) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Load matched symbols: Since the different local symbols may have
	 * same name but different addresses, this lists all the symbols.
	 */
	num_matched_functions = 0;
	looking_function_name = pp->function;
	ret = map__load(map, probe_function_filter);
	if (ret || num_matched_functions == 0) {
		pr_err("Failed to find symbol %s in %s\n", pp->function,
			target ? : "kernel");
		ret = -ENOENT;
		goto out;
	} else if (num_matched_functions > max_tevs) {
		pr_err("Too many functions matched in %s\n",
			target ? : "kernel");
		ret = -E2BIG;
		goto out;
	}

	if (!pev->uprobes) {
		kmap = map__kmap(map);
		reloc_sym = kmap->ref_reloc_sym;
		if (!reloc_sym) {
			pr_warning("Relocated base symbol is not found!\n");
			ret = -EINVAL;
			goto out;
		}
	}

	/* Setup result trace-probe-events */
	*tevs = zalloc(sizeof(*tev) * num_matched_functions);
	if (!*tevs) {
		ret = -ENOMEM;
		goto out;
	}

	ret = 0;
	map__for_each_symbol(map, sym, nd) {
		tev = (*tevs) + ret;
		tp = &tev->point;
		if (ret == num_matched_functions) {
			pr_warning("Too many symbols are listed. Skip it.\n");
			break;
		}
		ret++;

		if (pp->offset > sym->end - sym->start) {
			pr_warning("Offset %ld is bigger than the size of %s\n",
				   pp->offset, sym->name);
			ret = -ENOENT;
			goto err_out;
		}
		/* Add one probe point */
		tp->address = map->unmap_ip(map, sym->start) + pp->offset;
		if (reloc_sym) {
			tp->symbol = strdup_or_goto(reloc_sym->name, nomem_out);
			tp->offset = tp->address - reloc_sym->addr;
		} else {
			tp->symbol = strdup_or_goto(sym->name, nomem_out);
			tp->offset = pp->offset;
		}
		tp->retprobe = pp->retprobe;
		if (target)
			tev->point.module = strdup_or_goto(target, nomem_out);
		tev->uprobes = pev->uprobes;
		tev->nargs = pev->nargs;
		if (tev->nargs) {
			tev->args = zalloc(sizeof(struct probe_trace_arg) *
					   tev->nargs);
			if (tev->args == NULL)
				goto nomem_out;
		}
		for (i = 0; i < tev->nargs; i++) {
			if (pev->args[i].name)
				tev->args[i].name =
					strdup_or_goto(pev->args[i].name,
							nomem_out);

			tev->args[i].value = strdup_or_goto(pev->args[i].var,
							    nomem_out);
			if (pev->args[i].type)
				tev->args[i].type =
					strdup_or_goto(pev->args[i].type,
							nomem_out);
		}
	}

out:
	if (map && pev->uprobes) {
		/* Only when using uprobe(exec) map needs to be released */
		dso__delete(map->dso);
		map__delete(map);
	}
	return ret;

nomem_out:
	ret = -ENOMEM;
err_out:
	clear_probe_trace_events(*tevs, num_matched_functions);
	zfree(tevs);
	goto out;
}

static int convert_to_probe_trace_events(struct perf_probe_event *pev,
					  struct probe_trace_event **tevs,
					  int max_tevs, const char *target)
{
	int ret;

	if (pev->uprobes && !pev->group) {
		/* Replace group name if not given */
		ret = convert_exec_to_group(target, &pev->group);
		if (ret != 0) {
			pr_warning("Failed to make a group name.\n");
			return ret;
		}
	}

	/* Convert perf_probe_event with debuginfo */
	ret = try_to_find_probe_trace_events(pev, tevs, max_tevs, target);
	if (ret != 0)
		return ret;	/* Found in debuginfo or got an error */

	return find_probe_trace_events_from_map(pev, tevs, max_tevs, target);
}

struct __event_package {
	struct perf_probe_event		*pev;
	struct probe_trace_event	*tevs;
	int				ntevs;
};

int add_perf_probe_events(struct perf_probe_event *pevs, int npevs,
			  int max_tevs, const char *target, bool force_add)
{
	int i, j, ret;
	struct __event_package *pkgs;

	ret = 0;
	pkgs = zalloc(sizeof(struct __event_package) * npevs);

	if (pkgs == NULL)
		return -ENOMEM;

	ret = init_symbol_maps(pevs->uprobes);
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
						     target);
		if (ret < 0)
			goto end;
		pkgs[i].ntevs = ret;
	}

	/* Loop 2: add all events */
	for (i = 0; i < npevs; i++) {
		ret = __add_probe_trace_events(pkgs[i].pev, pkgs[i].tevs,
						pkgs[i].ntevs, force_add);
		if (ret < 0)
			break;
	}
end:
	/* Loop 3: cleanup and free trace events  */
	for (i = 0; i < npevs; i++) {
		for (j = 0; j < pkgs[i].ntevs; j++)
			clear_probe_trace_event(&pkgs[i].tevs[j]);
		zfree(&pkgs[i].tevs);
	}
	free(pkgs);
	exit_symbol_maps();

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
	if (ret < 0) {
		ret = -errno;
		goto error;
	}

	printf("Removed event: %s\n", ent->s);
	return 0;
error:
	pr_warning("Failed to delete event: %s\n", strerror(-ret));
	return ret;
}

static int del_trace_probe_event(int fd, const char *buf,
						  struct strlist *namelist)
{
	struct str_node *ent, *n;
	int ret = -1;

	if (strpbrk(buf, "*?")) { /* Glob-exp */
		strlist__for_each_safe(ent, n, namelist)
			if (strglobmatch(ent->s, buf)) {
				ret = __del_trace_probe_event(fd, ent);
				if (ret < 0)
					break;
				strlist__remove(namelist, ent);
			}
	} else {
		ent = strlist__find(namelist, buf);
		if (ent) {
			ret = __del_trace_probe_event(fd, ent);
			if (ret >= 0)
				strlist__remove(namelist, ent);
		}
	}

	return ret;
}

int del_perf_probe_events(struct strlist *dellist)
{
	int ret = -1, ufd = -1, kfd = -1;
	char buf[128];
	const char *group, *event;
	char *p, *str;
	struct str_node *ent;
	struct strlist *namelist = NULL, *unamelist = NULL;

	/* Get current event names */
	kfd = open_kprobe_events(true);
	if (kfd < 0)
		return kfd;

	namelist = get_probe_trace_event_names(kfd, true);
	ufd = open_uprobe_events(true);

	if (ufd >= 0)
		unamelist = get_probe_trace_event_names(ufd, true);

	if (namelist == NULL && unamelist == NULL)
		goto error;

	strlist__for_each(ent, dellist) {
		str = strdup(ent->s);
		if (str == NULL) {
			ret = -ENOMEM;
			goto error;
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

		ret = e_snprintf(buf, 128, "%s:%s", group, event);
		if (ret < 0) {
			pr_err("Failed to copy event.");
			free(str);
			goto error;
		}

		pr_debug("Group: %s, Event: %s\n", group, event);

		if (namelist)
			ret = del_trace_probe_event(kfd, buf, namelist);

		if (unamelist && ret != 0)
			ret = del_trace_probe_event(ufd, buf, unamelist);

		if (ret != 0)
			pr_info("Info: Event \"%s\" does not exist.\n", buf);

		free(str);
	}

error:
	if (kfd >= 0) {
		strlist__delete(namelist);
		close(kfd);
	}

	if (ufd >= 0) {
		strlist__delete(unamelist);
		close(ufd);
	}

	return ret;
}

/* TODO: don't use a global variable for filter ... */
static struct strfilter *available_func_filter;

/*
 * If a symbol corresponds to a function with global binding and
 * matches filter return 0. For all others return 1.
 */
static int filter_available_functions(struct map *map __maybe_unused,
				      struct symbol *sym)
{
	if ((sym->binding == STB_GLOBAL || sym->binding == STB_LOCAL) &&
	    strfilter__compare(available_func_filter, sym->name))
		return 0;
	return 1;
}

int show_available_funcs(const char *target, struct strfilter *_filter,
					bool user)
{
	struct map *map;
	int ret;

	ret = init_symbol_maps(user);
	if (ret < 0)
		return ret;

	/* Get a symbol map */
	if (user)
		map = dso__new_map(target);
	else
		map = kernel_get_module_map(target);
	if (!map) {
		pr_err("Failed to get a map for %s\n", (target) ? : "kernel");
		return -EINVAL;
	}

	/* Load symbols with given filter */
	available_func_filter = _filter;
	if (map__load(map, filter_available_functions)) {
		pr_err("Failed to load symbols in %s\n", (target) ? : "kernel");
		goto end;
	}
	if (!dso__sorted_by_name(map->dso, map->type))
		dso__sort_by_name(map->dso, map->type);

	/* Show all (filtered) symbols */
	setup_pager();
	dso__fprintf_symbols_by_name(map->dso, map->type, stdout);
end:
	if (user) {
		dso__delete(map->dso);
		map__delete(map);
	}
	exit_symbol_maps();

	return ret;
}

