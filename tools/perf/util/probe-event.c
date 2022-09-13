// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * probe-event.c : perf-probe definition to probe_events format converter
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 */

#include <inttypes.h>
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

#include "build-id.h"
#include "event.h"
#include "namespaces.h"
#include "strlist.h"
#include "strfilter.h"
#include "debug.h"
#include "dso.h"
#include "color.h"
#include "map.h"
#include "maps.h"
#include "symbol.h"
#include <api/fs/fs.h>
#include "trace-event.h"	/* For __maybe_unused */
#include "probe-event.h"
#include "probe-finder.h"
#include "probe-file.h"
#include "session.h"
#include "string2.h"
#include "strbuf.h"

#include <subcmd/pager.h>
#include <linux/ctype.h>
#include <linux/zalloc.h>

#ifdef HAVE_DEBUGINFOD_SUPPORT
#include <elfutils/debuginfod.h>
#endif

#define PERFPROBE_GROUP "probe"

bool probe_event_dry_run;	/* Dry run flag */
struct probe_conf probe_conf = { .magic_num = DEFAULT_PROBE_MAGIC_NUM };

#define semantic_error(msg ...) pr_err("Semantic error :" msg)

int e_snprintf(char *str, size_t size, const char *format, ...)
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

static struct machine *host_machine;

/* Initialize symbol maps and path of vmlinux/modules */
int init_probe_symbol_maps(bool user_only)
{
	int ret;

	symbol_conf.sort_by_name = true;
	symbol_conf.allow_aliases = true;
	ret = symbol__init(NULL);
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

void exit_probe_symbol_maps(void)
{
	machine__delete(host_machine);
	host_machine = NULL;
	symbol__exit();
}

static struct ref_reloc_sym *kernel_get_ref_reloc_sym(struct map **pmap)
{
	struct kmap *kmap;
	struct map *map = machine__kernel_map(host_machine);

	if (map__load(map) < 0)
		return NULL;

	kmap = map__kmap(map);
	if (!kmap)
		return NULL;

	if (pmap)
		*pmap = map;

	return kmap->ref_reloc_sym;
}

static int kernel_get_symbol_address_by_name(const char *name, u64 *addr,
					     bool reloc, bool reladdr)
{
	struct ref_reloc_sym *reloc_sym;
	struct symbol *sym;
	struct map *map;

	/* ref_reloc_sym is just a label. Need a special fix*/
	reloc_sym = kernel_get_ref_reloc_sym(&map);
	if (reloc_sym && strcmp(name, reloc_sym->name) == 0)
		*addr = (!map->reloc || reloc) ? reloc_sym->addr :
			reloc_sym->unrelocated_addr;
	else {
		sym = machine__find_kernel_symbol_by_name(host_machine, name, &map);
		if (!sym)
			return -ENOENT;
		*addr = map->unmap_ip(map, sym->start) -
			((reloc) ? 0 : map->reloc) -
			((reladdr) ? map->start : 0);
	}
	return 0;
}

static struct map *kernel_get_module_map(const char *module)
{
	struct maps *maps = machine__kernel_maps(host_machine);
	struct map *pos;

	/* A file path -- this is an offline module */
	if (module && strchr(module, '/'))
		return dso__new_map(module);

	if (!module) {
		pos = machine__kernel_map(host_machine);
		return map__get(pos);
	}

	maps__for_each_entry(maps, pos) {
		/* short_name is "[module]" */
		if (strncmp(pos->dso->short_name + 1, module,
			    pos->dso->short_name_len - 2) == 0 &&
		    module[pos->dso->short_name_len - 2] == '\0') {
			return map__get(pos);
		}
	}
	return NULL;
}

struct map *get_target_map(const char *target, struct nsinfo *nsi, bool user)
{
	/* Init maps of given executable or kernel */
	if (user) {
		struct map *map;

		map = dso__new_map(target);
		if (map && map->dso) {
			nsinfo__put(map->dso->nsinfo);
			map->dso->nsinfo = nsinfo__get(nsi);
		}
		return map;
	} else {
		return kernel_get_module_map(target);
	}
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

	for (ptr2 = ptr1; *ptr2 != '\0'; ptr2++) {
		if (!isalnum(*ptr2) && *ptr2 != '_') {
			*ptr2 = '\0';
			break;
		}
	}

	ret = e_snprintf(buf, 64, "%s_%s", PERFPROBE_GROUP, ptr1);
	if (ret < 0)
		goto out;

	*result = strdup(buf);
	ret = *result ? 0 : -ENOMEM;

out:
	free(exec_copy);
	return ret;
}

static void clear_perf_probe_point(struct perf_probe_point *pp)
{
	zfree(&pp->file);
	zfree(&pp->function);
	zfree(&pp->lazy_line);
}

static void clear_probe_trace_events(struct probe_trace_event *tevs, int ntevs)
{
	int i;

	for (i = 0; i < ntevs; i++)
		clear_probe_trace_event(tevs + i);
}

static bool kprobe_blacklist__listed(u64 address);
static bool kprobe_warn_out_range(const char *symbol, u64 address)
{
	struct map *map;
	bool ret = false;

	map = kernel_get_module_map(NULL);
	if (map) {
		ret = address <= map->start || map->end < address;
		if (ret)
			pr_warning("%s is out of .text, skip it.\n", symbol);
		map__put(map);
	}
	if (!ret && kprobe_blacklist__listed(address)) {
		pr_warning("%s is blacklisted function, skip it.\n", symbol);
		ret = true;
	}

	return ret;
}

/*
 * @module can be module name of module file path. In case of path,
 * inspect elf and find out what is actual module name.
 * Caller has to free mod_name after using it.
 */
static char *find_module_name(const char *module)
{
	int fd;
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	Elf_Data *data;
	Elf_Scn *sec;
	char *mod_name = NULL;
	int name_offset;

	fd = open(module, O_RDONLY);
	if (fd < 0)
		return NULL;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL)
		goto elf_err;

	if (gelf_getehdr(elf, &ehdr) == NULL)
		goto ret_err;

	sec = elf_section_by_name(elf, &ehdr, &shdr,
			".gnu.linkonce.this_module", NULL);
	if (!sec)
		goto ret_err;

	data = elf_getdata(sec, NULL);
	if (!data || !data->d_buf)
		goto ret_err;

	/*
	 * NOTE:
	 * '.gnu.linkonce.this_module' section of kernel module elf directly
	 * maps to 'struct module' from linux/module.h. This section contains
	 * actual module name which will be used by kernel after loading it.
	 * But, we cannot use 'struct module' here since linux/module.h is not
	 * exposed to user-space. Offset of 'name' has remained same from long
	 * time, so hardcoding it here.
	 */
	if (ehdr.e_ident[EI_CLASS] == ELFCLASS32)
		name_offset = 12;
	else	/* expect ELFCLASS64 by default */
		name_offset = 24;

	mod_name = strdup((char *)data->d_buf + name_offset);

ret_err:
	elf_end(elf);
elf_err:
	close(fd);
	return mod_name;
}

#ifdef HAVE_DWARF_SUPPORT

static int kernel_get_module_dso(const char *module, struct dso **pdso)
{
	struct dso *dso;
	struct map *map;
	const char *vmlinux_name;
	int ret = 0;

	if (module) {
		char module_name[128];

		snprintf(module_name, sizeof(module_name), "[%s]", module);
		map = maps__find_by_name(&host_machine->kmaps, module_name);
		if (map) {
			dso = map->dso;
			goto found;
		}
		pr_debug("Failed to find module %s.\n", module);
		return -ENOENT;
	}

	map = machine__kernel_map(host_machine);
	dso = map->dso;
	if (!dso->has_build_id)
		dso__read_running_kernel_build_id(dso, host_machine);

	vmlinux_name = symbol_conf.vmlinux_name;
	dso->load_errno = 0;
	if (vmlinux_name)
		ret = dso__load_vmlinux(dso, map, vmlinux_name, false);
	else
		ret = dso__load_vmlinux_path(dso, map);
found:
	*pdso = dso;
	return ret;
}

/*
 * Some binaries like glibc have special symbols which are on the symbol
 * table, but not in the debuginfo. If we can find the address of the
 * symbol from map, we can translate the address back to the probe point.
 */
static int find_alternative_probe_point(struct debuginfo *dinfo,
					struct perf_probe_point *pp,
					struct perf_probe_point *result,
					const char *target, struct nsinfo *nsi,
					bool uprobes)
{
	struct map *map = NULL;
	struct symbol *sym;
	u64 address = 0;
	int ret = -ENOENT;

	/* This can work only for function-name based one */
	if (!pp->function || pp->file)
		return -ENOTSUP;

	map = get_target_map(target, nsi, uprobes);
	if (!map)
		return -EINVAL;

	/* Find the address of given function */
	map__for_each_symbol_by_name(map, pp->function, sym) {
		if (uprobes) {
			address = sym->start;
			if (sym->type == STT_GNU_IFUNC)
				pr_warning("Warning: The probe function (%s) is a GNU indirect function.\n"
					   "Consider identifying the final function used at run time and set the probe directly on that.\n",
					   pp->function);
		} else
			address = map->unmap_ip(map, sym->start) - map->reloc;
		break;
	}
	if (!address) {
		ret = -ENOENT;
		goto out;
	}
	pr_debug("Symbol %s address found : %" PRIx64 "\n",
			pp->function, address);

	ret = debuginfo__find_probe_point(dinfo, address, result);
	if (ret <= 0)
		ret = (!ret) ? -ENOENT : ret;
	else {
		result->offset += pp->offset;
		result->line += pp->line;
		result->retprobe = pp->retprobe;
		ret = 0;
	}

out:
	map__put(map);
	return ret;

}

static int get_alternative_probe_event(struct debuginfo *dinfo,
				       struct perf_probe_event *pev,
				       struct perf_probe_point *tmp)
{
	int ret;

	memcpy(tmp, &pev->point, sizeof(*tmp));
	memset(&pev->point, 0, sizeof(pev->point));
	ret = find_alternative_probe_point(dinfo, tmp, &pev->point, pev->target,
					   pev->nsi, pev->uprobes);
	if (ret < 0)
		memcpy(&pev->point, tmp, sizeof(*tmp));

	return ret;
}

static int get_alternative_line_range(struct debuginfo *dinfo,
				      struct line_range *lr,
				      const char *target, bool user)
{
	struct perf_probe_point pp = { .function = lr->function,
				       .file = lr->file,
				       .line = lr->start };
	struct perf_probe_point result;
	int ret, len = 0;

	memset(&result, 0, sizeof(result));

	if (lr->end != INT_MAX)
		len = lr->end - lr->start;
	ret = find_alternative_probe_point(dinfo, &pp, &result,
					   target, NULL, user);
	if (!ret) {
		lr->function = result.function;
		lr->file = result.file;
		lr->start = result.line;
		if (lr->end != INT_MAX)
			lr->end = lr->start + len;
		clear_perf_probe_point(&pp);
	}
	return ret;
}

#ifdef HAVE_DEBUGINFOD_SUPPORT
static struct debuginfo *open_from_debuginfod(struct dso *dso, struct nsinfo *nsi,
					      bool silent)
{
	debuginfod_client *c = debuginfod_begin();
	char sbuild_id[SBUILD_ID_SIZE + 1];
	struct debuginfo *ret = NULL;
	struct nscookie nsc;
	char *path;
	int fd;

	if (!c)
		return NULL;

	build_id__sprintf(&dso->bid, sbuild_id);
	fd = debuginfod_find_debuginfo(c, (const unsigned char *)sbuild_id,
					0, &path);
	if (fd >= 0)
		close(fd);
	debuginfod_end(c);
	if (fd < 0) {
		if (!silent)
			pr_debug("Failed to find debuginfo in debuginfod.\n");
		return NULL;
	}
	if (!silent)
		pr_debug("Load debuginfo from debuginfod (%s)\n", path);

	nsinfo__mountns_enter(nsi, &nsc);
	ret = debuginfo__new((const char *)path);
	nsinfo__mountns_exit(&nsc);
	return ret;
}
#else
static inline
struct debuginfo *open_from_debuginfod(struct dso *dso __maybe_unused,
				       struct nsinfo *nsi __maybe_unused,
				       bool silent __maybe_unused)
{
	return NULL;
}
#endif

/* Open new debuginfo of given module */
static struct debuginfo *open_debuginfo(const char *module, struct nsinfo *nsi,
					bool silent)
{
	const char *path = module;
	char reason[STRERR_BUFSIZE];
	struct debuginfo *ret = NULL;
	struct dso *dso = NULL;
	struct nscookie nsc;
	int err;

	if (!module || !strchr(module, '/')) {
		err = kernel_get_module_dso(module, &dso);
		if (err < 0) {
			if (!dso || dso->load_errno == 0) {
				if (!str_error_r(-err, reason, STRERR_BUFSIZE))
					strcpy(reason, "(unknown)");
			} else
				dso__strerror_load(dso, reason, STRERR_BUFSIZE);
			if (dso)
				ret = open_from_debuginfod(dso, nsi, silent);
			if (ret)
				return ret;
			if (!silent) {
				if (module)
					pr_err("Module %s is not loaded, please specify its full path name.\n", module);
				else
					pr_err("Failed to find the path for the kernel: %s\n", reason);
			}
			return NULL;
		}
		path = dso->long_name;
	}
	nsinfo__mountns_enter(nsi, &nsc);
	ret = debuginfo__new(path);
	if (!ret && !silent) {
		pr_warning("The %s file has no debug information.\n", path);
		if (!module || !strtailcmp(path, ".ko"))
			pr_warning("Rebuild with CONFIG_DEBUG_INFO=y, ");
		else
			pr_warning("Rebuild with -g, ");
		pr_warning("or install an appropriate debuginfo package.\n");
	}
	nsinfo__mountns_exit(&nsc);
	return ret;
}

/* For caching the last debuginfo */
static struct debuginfo *debuginfo_cache;
static char *debuginfo_cache_path;

static struct debuginfo *debuginfo_cache__open(const char *module, bool silent)
{
	const char *path = module;

	/* If the module is NULL, it should be the kernel. */
	if (!module)
		path = "kernel";

	if (debuginfo_cache_path && !strcmp(debuginfo_cache_path, path))
		goto out;

	/* Copy module path */
	free(debuginfo_cache_path);
	debuginfo_cache_path = strdup(path);
	if (!debuginfo_cache_path) {
		debuginfo__delete(debuginfo_cache);
		debuginfo_cache = NULL;
		goto out;
	}

	debuginfo_cache = open_debuginfo(module, NULL, silent);
	if (!debuginfo_cache)
		zfree(&debuginfo_cache_path);
out:
	return debuginfo_cache;
}

static void debuginfo_cache__exit(void)
{
	debuginfo__delete(debuginfo_cache);
	debuginfo_cache = NULL;
	zfree(&debuginfo_cache_path);
}


static int get_text_start_address(const char *exec, u64 *address,
				  struct nsinfo *nsi)
{
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	int fd, ret = -ENOENT;
	struct nscookie nsc;

	nsinfo__mountns_enter(nsi, &nsc);
	fd = open(exec, O_RDONLY);
	nsinfo__mountns_exit(&nsc);
	if (fd < 0)
		return -errno;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		ret = -EINVAL;
		goto out_close;
	}

	if (gelf_getehdr(elf, &ehdr) == NULL)
		goto out;

	if (!elf_section_by_name(elf, &ehdr, &shdr, ".text", NULL))
		goto out;

	*address = shdr.sh_addr - shdr.sh_offset;
	ret = 0;
out:
	elf_end(elf);
out_close:
	close(fd);

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
	u64 stext = 0;
	u64 addr = tp->address;
	int ret = -ENOENT;

	/* convert the address to dwarf address */
	if (!is_kprobe) {
		if (!addr) {
			ret = -EINVAL;
			goto error;
		}
		ret = get_text_start_address(tp->module, &stext, NULL);
		if (ret < 0)
			goto error;
		addr += stext;
	} else if (tp->symbol) {
		/* If the module is given, this returns relative address */
		ret = kernel_get_symbol_address_by_name(tp->symbol, &addr,
							false, !!tp->module);
		if (ret != 0)
			goto error;
		addr += tp->offset;
	}

	pr_debug("try to find information at %" PRIx64 " in %s\n", addr,
		 tp->module ? : "kernel");

	dinfo = debuginfo_cache__open(tp->module, verbose <= 0);
	if (dinfo)
		ret = debuginfo__find_probe_point(dinfo, addr, pp);
	else
		ret = -ENOENT;

	if (ret > 0) {
		pp->retprobe = tp->retprobe;
		return 0;
	}
error:
	pr_debug("Failed to find corresponding probes from debuginfo.\n");
	return ret ? : -ENOENT;
}

/* Adjust symbol name and address */
static int post_process_probe_trace_point(struct probe_trace_point *tp,
					   struct map *map, u64 offs)
{
	struct symbol *sym;
	u64 addr = tp->address - offs;

	sym = map__find_symbol(map, addr);
	if (!sym) {
		/*
		 * If the address is in the inittext section, map can not
		 * find it. Ignore it if we are probing offline kernel.
		 */
		return (symbol_conf.ignore_vmlinux_buildid) ? 0 : -ENOENT;
	}

	if (strcmp(sym->name, tp->symbol)) {
		/* If we have no realname, use symbol for it */
		if (!tp->realname)
			tp->realname = tp->symbol;
		else
			free(tp->symbol);
		tp->symbol = strdup(sym->name);
		if (!tp->symbol)
			return -ENOMEM;
	}
	tp->offset = addr - sym->start;
	tp->address -= offs;

	return 0;
}

/*
 * Rename DWARF symbols to ELF symbols -- gcc sometimes optimizes functions
 * and generate new symbols with suffixes such as .constprop.N or .isra.N
 * etc. Since those symbols are not recorded in DWARF, we have to find
 * correct generated symbols from offline ELF binary.
 * For online kernel or uprobes we don't need this because those are
 * rebased on _text, or already a section relative address.
 */
static int
post_process_offline_probe_trace_events(struct probe_trace_event *tevs,
					int ntevs, const char *pathname)
{
	struct map *map;
	u64 stext = 0;
	int i, ret = 0;

	/* Prepare a map for offline binary */
	map = dso__new_map(pathname);
	if (!map || get_text_start_address(pathname, &stext, NULL) < 0) {
		pr_warning("Failed to get ELF symbols for %s\n", pathname);
		return -EINVAL;
	}

	for (i = 0; i < ntevs; i++) {
		ret = post_process_probe_trace_point(&tevs[i].point,
						     map, stext);
		if (ret < 0)
			break;
	}
	map__put(map);

	return ret;
}

static int add_exec_to_probe_trace_events(struct probe_trace_event *tevs,
					  int ntevs, const char *exec,
					  struct nsinfo *nsi)
{
	int i, ret = 0;
	u64 stext = 0;

	if (!exec)
		return 0;

	ret = get_text_start_address(exec, &stext, nsi);
	if (ret < 0)
		return ret;

	for (i = 0; i < ntevs && ret >= 0; i++) {
		/* point.address is the address of point.symbol + point.offset */
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

static int
post_process_module_probe_trace_events(struct probe_trace_event *tevs,
				       int ntevs, const char *module,
				       struct debuginfo *dinfo)
{
	Dwarf_Addr text_offs = 0;
	int i, ret = 0;
	char *mod_name = NULL;
	struct map *map;

	if (!module)
		return 0;

	map = get_target_map(module, NULL, false);
	if (!map || debuginfo__get_text_offset(dinfo, &text_offs, true) < 0) {
		pr_warning("Failed to get ELF symbols for %s\n", module);
		return -EINVAL;
	}

	mod_name = find_module_name(module);
	for (i = 0; i < ntevs; i++) {
		ret = post_process_probe_trace_point(&tevs[i].point,
						map, text_offs);
		if (ret < 0)
			break;
		tevs[i].point.module =
			strdup(mod_name ? mod_name : module);
		if (!tevs[i].point.module) {
			ret = -ENOMEM;
			break;
		}
	}

	free(mod_name);
	map__put(map);

	return ret;
}

static int
post_process_kernel_probe_trace_events(struct probe_trace_event *tevs,
				       int ntevs)
{
	struct ref_reloc_sym *reloc_sym;
	struct map *map;
	char *tmp;
	int i, skipped = 0;

	/* Skip post process if the target is an offline kernel */
	if (symbol_conf.ignore_vmlinux_buildid)
		return post_process_offline_probe_trace_events(tevs, ntevs,
						symbol_conf.vmlinux_name);

	reloc_sym = kernel_get_ref_reloc_sym(&map);
	if (!reloc_sym) {
		pr_warning("Relocated base symbol is not found! "
			   "Check /proc/sys/kernel/kptr_restrict\n"
			   "and /proc/sys/kernel/perf_event_paranoid. "
			   "Or run as privileged perf user.\n\n");
		return -EINVAL;
	}

	for (i = 0; i < ntevs; i++) {
		if (!tevs[i].point.address)
			continue;
		if (tevs[i].point.retprobe && !kretprobe_offset_is_supported())
			continue;
		/*
		 * If we found a wrong one, mark it by NULL symbol.
		 * Since addresses in debuginfo is same as objdump, we need
		 * to convert it to addresses on memory.
		 */
		if (kprobe_warn_out_range(tevs[i].point.symbol,
			map__objdump_2mem(map, tevs[i].point.address))) {
			tmp = NULL;
			skipped++;
		} else {
			tmp = strdup(reloc_sym->name);
			if (!tmp)
				return -ENOMEM;
		}
		/* If we have no realname, use symbol for it */
		if (!tevs[i].point.realname)
			tevs[i].point.realname = tevs[i].point.symbol;
		else
			free(tevs[i].point.symbol);
		tevs[i].point.symbol = tmp;
		tevs[i].point.offset = tevs[i].point.address -
			(map->reloc ? reloc_sym->unrelocated_addr :
				      reloc_sym->addr);
	}
	return skipped;
}

void __weak
arch__post_process_probe_trace_events(struct perf_probe_event *pev __maybe_unused,
				      int ntevs __maybe_unused)
{
}

/* Post processing the probe events */
static int post_process_probe_trace_events(struct perf_probe_event *pev,
					   struct probe_trace_event *tevs,
					   int ntevs, const char *module,
					   bool uprobe, struct debuginfo *dinfo)
{
	int ret;

	if (uprobe)
		ret = add_exec_to_probe_trace_events(tevs, ntevs, module,
						     pev->nsi);
	else if (module)
		/* Currently ref_reloc_sym based probe is not for drivers */
		ret = post_process_module_probe_trace_events(tevs, ntevs,
							     module, dinfo);
	else
		ret = post_process_kernel_probe_trace_events(tevs, ntevs);

	if (ret >= 0)
		arch__post_process_probe_trace_events(pev, ntevs);

	return ret;
}

/* Try to find perf_probe_event with debuginfo */
static int try_to_find_probe_trace_events(struct perf_probe_event *pev,
					  struct probe_trace_event **tevs)
{
	bool need_dwarf = perf_probe_event_need_dwarf(pev);
	struct perf_probe_point tmp;
	struct debuginfo *dinfo;
	int ntevs, ret = 0;

	/* Workaround for gcc #98776 issue.
	 * Perf failed to add kretprobe event with debuginfo of vmlinux which is
	 * compiled by gcc with -fpatchable-function-entry option enabled. The
	 * same issue with kernel module. The retprobe doesn`t need debuginfo.
	 * This workaround solution use map to query the probe function address
	 * for retprobe event.
	 */
	if (pev->point.retprobe)
		return 0;

	dinfo = open_debuginfo(pev->target, pev->nsi, !need_dwarf);
	if (!dinfo) {
		if (need_dwarf)
			return -ENOENT;
		pr_debug("Could not open debuginfo. Try to use symbols.\n");
		return 0;
	}

	pr_debug("Try to find probe point from debuginfo.\n");
	/* Searching trace events corresponding to a probe event */
	ntevs = debuginfo__find_trace_events(dinfo, pev, tevs);

	if (ntevs == 0)	{  /* Not found, retry with an alternative */
		ret = get_alternative_probe_event(dinfo, pev, &tmp);
		if (!ret) {
			ntevs = debuginfo__find_trace_events(dinfo, pev, tevs);
			/*
			 * Write back to the original probe_event for
			 * setting appropriate (user given) event name
			 */
			clear_perf_probe_point(&pev->point);
			memcpy(&pev->point, &tmp, sizeof(tmp));
		}
	}

	if (ntevs > 0) {	/* Succeeded to find trace events */
		pr_debug("Found %d probe_trace_events.\n", ntevs);
		ret = post_process_probe_trace_events(pev, *tevs, ntevs,
					pev->target, pev->uprobes, dinfo);
		if (ret < 0 || ret == ntevs) {
			pr_debug("Post processing failed or all events are skipped. (%d)\n", ret);
			clear_probe_trace_events(*tevs, ntevs);
			zfree(tevs);
			ntevs = 0;
		}
	}

	debuginfo__delete(dinfo);

	if (ntevs == 0)	{	/* No error but failed to find probe point. */
		pr_warning("Probe point '%s' not found.\n",
			   synthesize_perf_probe_point(&pev->point));
		return -ENOENT;
	} else if (ntevs < 0) {
		/* Error path : ntevs < 0 */
		pr_debug("An error occurred in debuginfo analysis (%d).\n", ntevs);
		if (ntevs == -EBADF)
			pr_warning("Warning: No dwarf info found in the vmlinux - "
				"please rebuild kernel with CONFIG_DEBUG_INFO=y.\n");
		if (!need_dwarf) {
			pr_debug("Trying to use symbols.\n");
			return 0;
		}
	}
	return ntevs;
}

#define LINEBUF_SIZE 256
#define NR_ADDITIONAL_LINES 2

static int __show_one_line(FILE *fp, int l, bool skip, bool show_num)
{
	char buf[LINEBUF_SIZE], sbuf[STRERR_BUFSIZE];
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
		pr_warning("File read error: %s\n",
			   str_error_r(errno, sbuf, sizeof(sbuf)));
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
static int __show_line_range(struct line_range *lr, const char *module,
			     bool user)
{
	struct build_id bid;
	int l = 1;
	struct int_node *ln;
	struct debuginfo *dinfo;
	FILE *fp;
	int ret;
	char *tmp;
	char sbuf[STRERR_BUFSIZE];
	char sbuild_id[SBUILD_ID_SIZE] = "";

	/* Search a line range */
	dinfo = open_debuginfo(module, NULL, false);
	if (!dinfo)
		return -ENOENT;

	ret = debuginfo__find_line_range(dinfo, lr);
	if (!ret) {	/* Not found, retry with an alternative */
		ret = get_alternative_line_range(dinfo, lr, module, user);
		if (!ret)
			ret = debuginfo__find_line_range(dinfo, lr);
	}
	if (dinfo->build_id) {
		build_id__init(&bid, dinfo->build_id, BUILD_ID_SIZE);
		build_id__sprintf(&bid, sbuild_id);
	}
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
	ret = find_source_path(tmp, sbuild_id, lr->comp_dir, &lr->path);

	/* Free old path when new path is assigned */
	if (tmp != lr->path)
		free(tmp);

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
			   str_error_r(errno, sbuf, sizeof(sbuf)));
		return -errno;
	}
	/* Skip to starting line number */
	while (l < lr->start) {
		ret = skip_one_line(fp, l++);
		if (ret < 0)
			goto end;
	}

	intlist__for_each_entry(ln, lr->line_list) {
		for (; ln->i > (unsigned long)l; l++) {
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

int show_line_range(struct line_range *lr, const char *module,
		    struct nsinfo *nsi, bool user)
{
	int ret;
	struct nscookie nsc;

	ret = init_probe_symbol_maps(user);
	if (ret < 0)
		return ret;
	nsinfo__mountns_enter(nsi, &nsc);
	ret = __show_line_range(lr, module, user);
	nsinfo__mountns_exit(&nsc);
	exit_probe_symbol_maps();

	return ret;
}

static int show_available_vars_at(struct debuginfo *dinfo,
				  struct perf_probe_event *pev,
				  struct strfilter *_filter)
{
	char *buf;
	int ret, i, nvars;
	struct str_node *node;
	struct variable_list *vls = NULL, *vl;
	struct perf_probe_point tmp;
	const char *var;

	buf = synthesize_perf_probe_point(&pev->point);
	if (!buf)
		return -EINVAL;
	pr_debug("Searching variables at %s\n", buf);

	ret = debuginfo__find_available_vars_at(dinfo, pev, &vls);
	if (!ret) {  /* Not found, retry with an alternative */
		ret = get_alternative_probe_event(dinfo, pev, &tmp);
		if (!ret) {
			ret = debuginfo__find_available_vars_at(dinfo, pev,
								&vls);
			/* Release the old probe_point */
			clear_perf_probe_point(&tmp);
		}
	}
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
			strlist__for_each_entry(node, vl->vars) {
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
			struct strfilter *_filter)
{
	int i, ret = 0;
	struct debuginfo *dinfo;

	ret = init_probe_symbol_maps(pevs->uprobes);
	if (ret < 0)
		return ret;

	dinfo = open_debuginfo(pevs->target, pevs->nsi, false);
	if (!dinfo) {
		ret = -ENOENT;
		goto out;
	}

	setup_pager();

	for (i = 0; i < npevs && ret >= 0; i++)
		ret = show_available_vars_at(dinfo, &pevs[i], _filter);

	debuginfo__delete(dinfo);
out:
	exit_probe_symbol_maps();
	return ret;
}

#else	/* !HAVE_DWARF_SUPPORT */

static void debuginfo_cache__exit(void)
{
}

static int
find_perf_probe_point_from_dwarf(struct probe_trace_point *tp __maybe_unused,
				 struct perf_probe_point *pp __maybe_unused,
				 bool is_kprobe __maybe_unused)
{
	return -ENOSYS;
}

static int try_to_find_probe_trace_events(struct perf_probe_event *pev,
				struct probe_trace_event **tevs __maybe_unused)
{
	if (perf_probe_event_need_dwarf(pev)) {
		pr_warning("Debuginfo-analysis is not supported.\n");
		return -ENOSYS;
	}

	return 0;
}

int show_line_range(struct line_range *lr __maybe_unused,
		    const char *module __maybe_unused,
		    struct nsinfo *nsi __maybe_unused,
		    bool user __maybe_unused)
{
	pr_warning("Debuginfo-analysis is not supported.\n");
	return -ENOSYS;
}

int show_available_vars(struct perf_probe_event *pevs __maybe_unused,
			int npevs __maybe_unused,
			struct strfilter *filter __maybe_unused)
{
	pr_warning("Debuginfo-analysis is not supported.\n");
	return -ENOSYS;
}
#endif

void line_range__clear(struct line_range *lr)
{
	zfree(&lr->function);
	zfree(&lr->file);
	zfree(&lr->path);
	zfree(&lr->comp_dir);
	intlist__delete(lr->line_list);
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

/* Check the name is good for event, group or function */
static bool is_c_func_name(const char *name)
{
	if (!isalpha(*name) && *name != '_')
		return false;
	while (*++name != '\0') {
		if (!isalpha(*name) && !isdigit(*name) && *name != '_')
			return false;
	}
	return true;
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
	} else if (strchr(name, '/') || strchr(name, '.'))
		lr->file = name;
	else if (is_c_func_name(name))/* We reuse it for checking funcname */
		lr->function = name;
	else {	/* Invalid name */
		semantic_error("'%s' is not a valid function name.\n", name);
		err = -EINVAL;
		goto err;
	}

	return 0;
err:
	free(name);
	return err;
}

static int parse_perf_probe_event_name(char **arg, struct perf_probe_event *pev)
{
	char *ptr;

	ptr = strpbrk_esc(*arg, ":");
	if (ptr) {
		*ptr = '\0';
		if (!pev->sdt && !is_c_func_name(*arg))
			goto ng_name;
		pev->group = strdup_esc(*arg);
		if (!pev->group)
			return -ENOMEM;
		*arg = ptr + 1;
	} else
		pev->group = NULL;

	pev->event = strdup_esc(*arg);
	if (pev->event == NULL)
		return -ENOMEM;

	if (!pev->sdt && !is_c_func_name(pev->event)) {
		zfree(&pev->event);
ng_name:
		zfree(&pev->group);
		semantic_error("%s is bad for event name -it must "
			       "follow C symbol-naming rule.\n", *arg);
		return -EINVAL;
	}
	return 0;
}

/* Parse probepoint definition. */
static int parse_perf_probe_point(char *arg, struct perf_probe_event *pev)
{
	struct perf_probe_point *pp = &pev->point;
	char *ptr, *tmp;
	char c, nc = 0;
	bool file_spec = false;
	int ret;

	/*
	 * <Syntax>
	 * perf probe [GRP:][EVENT=]SRC[:LN|;PTN]
	 * perf probe [GRP:][EVENT=]FUNC[@SRC][+OFFS|%return|:LN|;PAT]
	 * perf probe %[GRP:]SDT_EVENT
	 */
	if (!arg)
		return -EINVAL;

	if (is_sdt_event(arg)) {
		pev->sdt = true;
		if (arg[0] == '%')
			arg++;
	}

	ptr = strpbrk_esc(arg, ";=@+%");
	if (pev->sdt) {
		if (ptr) {
			if (*ptr != '@') {
				semantic_error("%s must be an SDT name.\n",
					       arg);
				return -EINVAL;
			}
			/* This must be a target file name or build id */
			tmp = build_id_cache__complement(ptr + 1);
			if (tmp) {
				pev->target = build_id_cache__origname(tmp);
				free(tmp);
			} else
				pev->target = strdup_esc(ptr + 1);
			if (!pev->target)
				return -ENOMEM;
			*ptr = '\0';
		}
		ret = parse_perf_probe_event_name(&arg, pev);
		if (ret == 0) {
			if (asprintf(&pev->point.function, "%%%s", pev->event) < 0)
				ret = -errno;
		}
		return ret;
	}

	if (ptr && *ptr == '=') {	/* Event name */
		*ptr = '\0';
		tmp = ptr + 1;
		ret = parse_perf_probe_event_name(&arg, pev);
		if (ret < 0)
			return ret;

		arg = tmp;
	}

	/*
	 * Check arg is function or file name and copy it.
	 *
	 * We consider arg to be a file spec if and only if it satisfies
	 * all of the below criteria::
	 * - it does not include any of "+@%",
	 * - it includes one of ":;", and
	 * - it has a period '.' in the name.
	 *
	 * Otherwise, we consider arg to be a function specification.
	 */
	if (!strpbrk_esc(arg, "+@%")) {
		ptr = strpbrk_esc(arg, ";:");
		/* This is a file spec if it includes a '.' before ; or : */
		if (ptr && memchr(arg, '.', ptr - arg))
			file_spec = true;
	}

	ptr = strpbrk_esc(arg, ";:+@%");
	if (ptr) {
		nc = *ptr;
		*ptr++ = '\0';
	}

	if (arg[0] == '\0')
		tmp = NULL;
	else {
		tmp = strdup_esc(arg);
		if (tmp == NULL)
			return -ENOMEM;
	}

	if (file_spec)
		pp->file = tmp;
	else {
		pp->function = tmp;

		/*
		 * Keep pp->function even if this is absolute address,
		 * so it can mark whether abs_address is valid.
		 * Which make 'perf probe lib.bin 0x0' possible.
		 *
		 * Note that checking length of tmp is not needed
		 * because when we access tmp[1] we know tmp[0] is '0',
		 * so tmp[1] should always valid (but could be '\0').
		 */
		if (tmp && !strncmp(tmp, "0x", 2)) {
			pp->abs_address = strtoull(pp->function, &tmp, 0);
			if (*tmp != '\0') {
				semantic_error("Invalid absolute address.\n");
				return -EINVAL;
			}
		}
	}

	/* Parse other options */
	while (ptr) {
		arg = ptr;
		c = nc;
		if (c == ';') {	/* Lazy pattern must be the last part */
			pp->lazy_line = strdup(arg); /* let leave escapes */
			if (pp->lazy_line == NULL)
				return -ENOMEM;
			break;
		}
		ptr = strpbrk_esc(arg, ";:+@%");
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
			pp->file = strdup_esc(arg);
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

	tmp = strchr(str, '@');
	if (tmp && tmp != str && !strcmp(tmp + 1, "user")) { /* user attr */
		if (!user_access_is_supported()) {
			semantic_error("ftrace does not support user access\n");
			return -EINVAL;
		}
		*tmp = '\0';
		arg->user_access = true;
		pr_debug("user_access ");
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

	/* Generate event name if needed */
	if (!pev->event && pev->point.function && pev->point.line
			&& !pev->point.lazy_line && !pev->point.offset) {
		if (asprintf(&pev->event, "%s_L%d", pev->point.function,
			pev->point.line) < 0) {
			ret = -ENOMEM;
			goto out;
		}
	}

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

/* Returns true if *any* ARG is either C variable, $params or $vars. */
bool perf_probe_with_var(struct perf_probe_event *pev)
{
	int i = 0;

	for (i = 0; i < pev->nargs; i++)
		if (is_c_varname(pev->args[i].var)              ||
		    !strcmp(pev->args[i].var, PROBE_ARG_PARAMS) ||
		    !strcmp(pev->args[i].var, PROBE_ARG_VARS))
			return true;
	return false;
}

/* Return true if this perf_probe_event requires debuginfo */
bool perf_probe_event_need_dwarf(struct perf_probe_event *pev)
{
	if (pev->point.file || pev->point.line || pev->point.lazy_line)
		return true;

	if (perf_probe_with_var(pev))
		return true;

	return false;
}

/* Parse probe_events event into struct probe_point */
int parse_probe_trace_command(const char *cmd, struct probe_trace_event *tev)
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
	if (fmt1_str == NULL || fmt2_str == NULL || fmt3_str == NULL) {
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
		if (!tp->module) {
			ret = -ENOMEM;
			goto out;
		}
		tev->uprobes = (tp->module[0] == '/');
		p++;
	} else
		p = argv[1];
	fmt1_str = strtok_r(p, "+", &fmt);
	/* only the address started with 0x */
	if (fmt1_str[0] == '0')	{
		/*
		 * Fix a special case:
		 * if address == 0, kernel reports something like:
		 * p:probe_libc/abs_0 /lib/libc-2.18.so:0x          (null) arg1=%ax
		 * Newer kernel may fix that, but we want to
		 * support old kernel also.
		 */
		if (strcmp(fmt1_str, "0x") == 0) {
			if (!argv[2] || strcmp(argv[2], "(null)")) {
				ret = -EINVAL;
				goto out;
			}
			tp->address = 0;

			free(argv[2]);
			for (i = 2; argv[i + 1] != NULL; i++)
				argv[i] = argv[i + 1];

			argv[i] = NULL;
			argc -= 1;
		} else
			tp->address = strtoull(fmt1_str, NULL, 0);
	} else {
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

	if (tev->uprobes) {
		fmt2_str = strchr(p, '(');
		if (fmt2_str)
			tp->ref_ctr_offset = strtoul(fmt2_str + 1, NULL, 0);
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
char *synthesize_perf_probe_arg(struct perf_probe_arg *pa)
{
	struct perf_probe_arg_field *field = pa->field;
	struct strbuf buf;
	char *ret = NULL;
	int err;

	if (strbuf_init(&buf, 64) < 0)
		return NULL;

	if (pa->name && pa->var)
		err = strbuf_addf(&buf, "%s=%s", pa->name, pa->var);
	else
		err = strbuf_addstr(&buf, pa->name ?: pa->var);
	if (err)
		goto out;

	while (field) {
		if (field->name[0] == '[')
			err = strbuf_addstr(&buf, field->name);
		else
			err = strbuf_addf(&buf, "%s%s", field->ref ? "->" : ".",
					  field->name);
		field = field->next;
		if (err)
			goto out;
	}

	if (pa->type)
		if (strbuf_addf(&buf, ":%s", pa->type) < 0)
			goto out;

	ret = strbuf_detach(&buf, NULL);
out:
	strbuf_release(&buf);
	return ret;
}

/* Compose only probe point (not argument) */
char *synthesize_perf_probe_point(struct perf_probe_point *pp)
{
	struct strbuf buf;
	char *tmp, *ret = NULL;
	int len, err = 0;

	if (strbuf_init(&buf, 64) < 0)
		return NULL;

	if (pp->function) {
		if (strbuf_addstr(&buf, pp->function) < 0)
			goto out;
		if (pp->offset)
			err = strbuf_addf(&buf, "+%lu", pp->offset);
		else if (pp->line)
			err = strbuf_addf(&buf, ":%d", pp->line);
		else if (pp->retprobe)
			err = strbuf_addstr(&buf, "%return");
		if (err)
			goto out;
	}
	if (pp->file) {
		tmp = pp->file;
		len = strlen(tmp);
		if (len > 30) {
			tmp = strchr(pp->file + len - 30, '/');
			tmp = tmp ? tmp + 1 : pp->file + len - 30;
		}
		err = strbuf_addf(&buf, "@%s", tmp);
		if (!err && !pp->function && pp->line)
			err = strbuf_addf(&buf, ":%d", pp->line);
	}
	if (!err)
		ret = strbuf_detach(&buf, NULL);
out:
	strbuf_release(&buf);
	return ret;
}

char *synthesize_perf_probe_command(struct perf_probe_event *pev)
{
	struct strbuf buf;
	char *tmp, *ret = NULL;
	int i;

	if (strbuf_init(&buf, 64))
		return NULL;
	if (pev->event)
		if (strbuf_addf(&buf, "%s:%s=", pev->group ?: PERFPROBE_GROUP,
				pev->event) < 0)
			goto out;

	tmp = synthesize_perf_probe_point(&pev->point);
	if (!tmp || strbuf_addstr(&buf, tmp) < 0)
		goto out;
	free(tmp);

	for (i = 0; i < pev->nargs; i++) {
		tmp = synthesize_perf_probe_arg(pev->args + i);
		if (!tmp || strbuf_addf(&buf, " %s", tmp) < 0)
			goto out;
		free(tmp);
	}

	ret = strbuf_detach(&buf, NULL);
out:
	strbuf_release(&buf);
	return ret;
}

static int __synthesize_probe_trace_arg_ref(struct probe_trace_arg_ref *ref,
					    struct strbuf *buf, int depth)
{
	int err;
	if (ref->next) {
		depth = __synthesize_probe_trace_arg_ref(ref->next, buf,
							 depth + 1);
		if (depth < 0)
			return depth;
	}
	if (ref->user_access)
		err = strbuf_addf(buf, "%s%ld(", "+u", ref->offset);
	else
		err = strbuf_addf(buf, "%+ld(", ref->offset);
	return (err < 0) ? err : depth;
}

static int synthesize_probe_trace_arg(struct probe_trace_arg *arg,
				      struct strbuf *buf)
{
	struct probe_trace_arg_ref *ref = arg->ref;
	int depth = 0, err;

	/* Argument name or separator */
	if (arg->name)
		err = strbuf_addf(buf, " %s=", arg->name);
	else
		err = strbuf_addch(buf, ' ');
	if (err)
		return err;

	/* Special case: @XXX */
	if (arg->value[0] == '@' && arg->ref)
			ref = ref->next;

	/* Dereferencing arguments */
	if (ref) {
		depth = __synthesize_probe_trace_arg_ref(ref, buf, 1);
		if (depth < 0)
			return depth;
	}

	/* Print argument value */
	if (arg->value[0] == '@' && arg->ref)
		err = strbuf_addf(buf, "%s%+ld", arg->value, arg->ref->offset);
	else
		err = strbuf_addstr(buf, arg->value);

	/* Closing */
	while (!err && depth--)
		err = strbuf_addch(buf, ')');

	/* Print argument type */
	if (!err && arg->type)
		err = strbuf_addf(buf, ":%s", arg->type);

	return err;
}

static int
synthesize_probe_trace_args(struct probe_trace_event *tev, struct strbuf *buf)
{
	int i, ret = 0;

	for (i = 0; i < tev->nargs && ret >= 0; i++)
		ret = synthesize_probe_trace_arg(&tev->args[i], buf);

	return ret;
}

static int
synthesize_uprobe_trace_def(struct probe_trace_point *tp, struct strbuf *buf)
{
	int err;

	/* Uprobes must have tp->module */
	if (!tp->module)
		return -EINVAL;
	/*
	 * If tp->address == 0, then this point must be a
	 * absolute address uprobe.
	 * try_to_find_absolute_address() should have made
	 * tp->symbol to "0x0".
	 */
	if (!tp->address && (!tp->symbol || strcmp(tp->symbol, "0x0")))
		return -EINVAL;

	/* Use the tp->address for uprobes */
	err = strbuf_addf(buf, "%s:0x%" PRIx64, tp->module, tp->address);

	if (err >= 0 && tp->ref_ctr_offset) {
		if (!uprobe_ref_ctr_is_supported())
			return -EINVAL;
		err = strbuf_addf(buf, "(0x%lx)", tp->ref_ctr_offset);
	}
	return err >= 0 ? 0 : err;
}

static int
synthesize_kprobe_trace_def(struct probe_trace_point *tp, struct strbuf *buf)
{
	if (!strncmp(tp->symbol, "0x", 2)) {
		/* Absolute address. See try_to_find_absolute_address() */
		return strbuf_addf(buf, "%s%s0x%" PRIx64, tp->module ?: "",
				  tp->module ? ":" : "", tp->address);
	} else {
		return strbuf_addf(buf, "%s%s%s+%lu", tp->module ?: "",
				tp->module ? ":" : "", tp->symbol, tp->offset);
	}
}

char *synthesize_probe_trace_command(struct probe_trace_event *tev)
{
	struct probe_trace_point *tp = &tev->point;
	struct strbuf buf;
	char *ret = NULL;
	int err;

	if (strbuf_init(&buf, 32) < 0)
		return NULL;

	if (strbuf_addf(&buf, "%c:%s/%s ", tp->retprobe ? 'r' : 'p',
			tev->group, tev->event) < 0)
		goto error;

	if (tev->uprobes)
		err = synthesize_uprobe_trace_def(tp, &buf);
	else
		err = synthesize_kprobe_trace_def(tp, &buf);

	if (err >= 0)
		err = synthesize_probe_trace_args(tev, &buf);

	if (err >= 0)
		ret = strbuf_detach(&buf, NULL);
error:
	strbuf_release(&buf);
	return ret;
}

static int find_perf_probe_point_from_map(struct probe_trace_point *tp,
					  struct perf_probe_point *pp,
					  bool is_kprobe)
{
	struct symbol *sym = NULL;
	struct map *map = NULL;
	u64 addr = tp->address;
	int ret = -ENOENT;

	if (!is_kprobe) {
		map = dso__new_map(tp->module);
		if (!map)
			goto out;
		sym = map__find_symbol(map, addr);
	} else {
		if (tp->symbol && !addr) {
			if (kernel_get_symbol_address_by_name(tp->symbol,
						&addr, true, false) < 0)
				goto out;
		}
		if (addr) {
			addr += tp->offset;
			sym = machine__find_kernel_symbol(host_machine, addr, &map);
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
		map__put(map);
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
	} else {
		ret = e_snprintf(buf, 128, "0x%" PRIx64, tp->address);
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
	struct strbuf buf = STRBUF_INIT;
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
			if ((ret = strbuf_init(&buf, 32)) < 0)
				goto error;
			ret = synthesize_probe_trace_arg(&tev->args[i], &buf);
			pev->args[i].name = strbuf_detach(&buf, NULL);
		}
		if (pev->args[i].name == NULL && ret >= 0)
			ret = -ENOMEM;
	}
error:
	if (ret < 0)
		clear_perf_probe_event(pev);

	return ret;
}

void clear_perf_probe_event(struct perf_probe_event *pev)
{
	struct perf_probe_arg_field *field, *next;
	int i;

	zfree(&pev->event);
	zfree(&pev->group);
	zfree(&pev->target);
	clear_perf_probe_point(&pev->point);

	for (i = 0; i < pev->nargs; i++) {
		zfree(&pev->args[i].name);
		zfree(&pev->args[i].var);
		zfree(&pev->args[i].type);
		field = pev->args[i].field;
		while (field) {
			next = field->next;
			zfree(&field->name);
			free(field);
			field = next;
		}
	}
	pev->nargs = 0;
	zfree(&pev->args);
}

#define strdup_or_goto(str, label)	\
({ char *__p = NULL; if (str && !(__p = strdup(str))) goto label; __p; })

static int perf_probe_point__copy(struct perf_probe_point *dst,
				  struct perf_probe_point *src)
{
	dst->file = strdup_or_goto(src->file, out_err);
	dst->function = strdup_or_goto(src->function, out_err);
	dst->lazy_line = strdup_or_goto(src->lazy_line, out_err);
	dst->line = src->line;
	dst->retprobe = src->retprobe;
	dst->offset = src->offset;
	return 0;

out_err:
	clear_perf_probe_point(dst);
	return -ENOMEM;
}

static int perf_probe_arg__copy(struct perf_probe_arg *dst,
				struct perf_probe_arg *src)
{
	struct perf_probe_arg_field *field, **ppfield;

	dst->name = strdup_or_goto(src->name, out_err);
	dst->var = strdup_or_goto(src->var, out_err);
	dst->type = strdup_or_goto(src->type, out_err);

	field = src->field;
	ppfield = &(dst->field);
	while (field) {
		*ppfield = zalloc(sizeof(*field));
		if (!*ppfield)
			goto out_err;
		(*ppfield)->name = strdup_or_goto(field->name, out_err);
		(*ppfield)->index = field->index;
		(*ppfield)->ref = field->ref;
		field = field->next;
		ppfield = &((*ppfield)->next);
	}
	return 0;
out_err:
	return -ENOMEM;
}

int perf_probe_event__copy(struct perf_probe_event *dst,
			   struct perf_probe_event *src)
{
	int i;

	dst->event = strdup_or_goto(src->event, out_err);
	dst->group = strdup_or_goto(src->group, out_err);
	dst->target = strdup_or_goto(src->target, out_err);
	dst->uprobes = src->uprobes;

	if (perf_probe_point__copy(&dst->point, &src->point) < 0)
		goto out_err;

	dst->args = zalloc(sizeof(struct perf_probe_arg) * src->nargs);
	if (!dst->args)
		goto out_err;
	dst->nargs = src->nargs;

	for (i = 0; i < src->nargs; i++)
		if (perf_probe_arg__copy(&dst->args[i], &src->args[i]) < 0)
			goto out_err;
	return 0;

out_err:
	clear_perf_probe_event(dst);
	return -ENOMEM;
}

void clear_probe_trace_event(struct probe_trace_event *tev)
{
	struct probe_trace_arg_ref *ref, *next;
	int i;

	zfree(&tev->event);
	zfree(&tev->group);
	zfree(&tev->point.symbol);
	zfree(&tev->point.realname);
	zfree(&tev->point.module);
	for (i = 0; i < tev->nargs; i++) {
		zfree(&tev->args[i].name);
		zfree(&tev->args[i].value);
		zfree(&tev->args[i].type);
		ref = tev->args[i].ref;
		while (ref) {
			next = ref->next;
			free(ref);
			ref = next;
		}
	}
	zfree(&tev->args);
	tev->nargs = 0;
}

struct kprobe_blacklist_node {
	struct list_head list;
	u64 start;
	u64 end;
	char *symbol;
};

static void kprobe_blacklist__delete(struct list_head *blacklist)
{
	struct kprobe_blacklist_node *node;

	while (!list_empty(blacklist)) {
		node = list_first_entry(blacklist,
					struct kprobe_blacklist_node, list);
		list_del_init(&node->list);
		zfree(&node->symbol);
		free(node);
	}
}

static int kprobe_blacklist__load(struct list_head *blacklist)
{
	struct kprobe_blacklist_node *node;
	const char *__debugfs = debugfs__mountpoint();
	char buf[PATH_MAX], *p;
	FILE *fp;
	int ret;

	if (__debugfs == NULL)
		return -ENOTSUP;

	ret = e_snprintf(buf, PATH_MAX, "%s/kprobes/blacklist", __debugfs);
	if (ret < 0)
		return ret;

	fp = fopen(buf, "r");
	if (!fp)
		return -errno;

	ret = 0;
	while (fgets(buf, PATH_MAX, fp)) {
		node = zalloc(sizeof(*node));
		if (!node) {
			ret = -ENOMEM;
			break;
		}
		INIT_LIST_HEAD(&node->list);
		list_add_tail(&node->list, blacklist);
		if (sscanf(buf, "0x%" PRIx64 "-0x%" PRIx64, &node->start, &node->end) != 2) {
			ret = -EINVAL;
			break;
		}
		p = strchr(buf, '\t');
		if (p) {
			p++;
			if (p[strlen(p) - 1] == '\n')
				p[strlen(p) - 1] = '\0';
		} else
			p = (char *)"unknown";
		node->symbol = strdup(p);
		if (!node->symbol) {
			ret = -ENOMEM;
			break;
		}
		pr_debug2("Blacklist: 0x%" PRIx64 "-0x%" PRIx64 ", %s\n",
			  node->start, node->end, node->symbol);
		ret++;
	}
	if (ret < 0)
		kprobe_blacklist__delete(blacklist);
	fclose(fp);

	return ret;
}

static struct kprobe_blacklist_node *
kprobe_blacklist__find_by_address(struct list_head *blacklist, u64 address)
{
	struct kprobe_blacklist_node *node;

	list_for_each_entry(node, blacklist, list) {
		if (node->start <= address && address < node->end)
			return node;
	}

	return NULL;
}

static LIST_HEAD(kprobe_blacklist);

static void kprobe_blacklist__init(void)
{
	if (!list_empty(&kprobe_blacklist))
		return;

	if (kprobe_blacklist__load(&kprobe_blacklist) < 0)
		pr_debug("No kprobe blacklist support, ignored\n");
}

static void kprobe_blacklist__release(void)
{
	kprobe_blacklist__delete(&kprobe_blacklist);
}

static bool kprobe_blacklist__listed(u64 address)
{
	return !!kprobe_blacklist__find_by_address(&kprobe_blacklist, address);
}

static int perf_probe_event__sprintf(const char *group, const char *event,
				     struct perf_probe_event *pev,
				     const char *module,
				     struct strbuf *result)
{
	int i, ret;
	char *buf;

	if (asprintf(&buf, "%s:%s", group, event) < 0)
		return -errno;
	ret = strbuf_addf(result, "  %-20s (on ", buf);
	free(buf);
	if (ret)
		return ret;

	/* Synthesize only event probe point */
	buf = synthesize_perf_probe_point(&pev->point);
	if (!buf)
		return -ENOMEM;
	ret = strbuf_addstr(result, buf);
	free(buf);

	if (!ret && module)
		ret = strbuf_addf(result, " in %s", module);

	if (!ret && pev->nargs > 0) {
		ret = strbuf_add(result, " with", 5);
		for (i = 0; !ret && i < pev->nargs; i++) {
			buf = synthesize_perf_probe_arg(&pev->args[i]);
			if (!buf)
				return -ENOMEM;
			ret = strbuf_addf(result, " %s", buf);
			free(buf);
		}
	}
	if (!ret)
		ret = strbuf_addch(result, ')');

	return ret;
}

/* Show an event */
int show_perf_probe_event(const char *group, const char *event,
			  struct perf_probe_event *pev,
			  const char *module, bool use_stdout)
{
	struct strbuf buf = STRBUF_INIT;
	int ret;

	ret = perf_probe_event__sprintf(group, event, pev, module, &buf);
	if (ret >= 0) {
		if (use_stdout)
			printf("%s\n", buf.buf);
		else
			pr_info("%s\n", buf.buf);
	}
	strbuf_release(&buf);

	return ret;
}

static bool filter_probe_trace_event(struct probe_trace_event *tev,
				     struct strfilter *filter)
{
	char tmp[128];

	/* At first, check the event name itself */
	if (strfilter__compare(filter, tev->event))
		return true;

	/* Next, check the combination of name and group */
	if (e_snprintf(tmp, 128, "%s:%s", tev->group, tev->event) < 0)
		return false;
	return strfilter__compare(filter, tmp);
}

static int __show_perf_probe_events(int fd, bool is_kprobe,
				    struct strfilter *filter)
{
	int ret = 0;
	struct probe_trace_event tev;
	struct perf_probe_event pev;
	struct strlist *rawlist;
	struct str_node *ent;

	memset(&tev, 0, sizeof(tev));
	memset(&pev, 0, sizeof(pev));

	rawlist = probe_file__get_rawlist(fd);
	if (!rawlist)
		return -ENOMEM;

	strlist__for_each_entry(ent, rawlist) {
		ret = parse_probe_trace_command(ent->s, &tev);
		if (ret >= 0) {
			if (!filter_probe_trace_event(&tev, filter))
				goto next;
			ret = convert_to_perf_probe_event(&tev, &pev,
								is_kprobe);
			if (ret < 0)
				goto next;
			ret = show_perf_probe_event(pev.group, pev.event,
						    &pev, tev.point.module,
						    true);
		}
next:
		clear_perf_probe_event(&pev);
		clear_probe_trace_event(&tev);
		if (ret < 0)
			break;
	}
	strlist__delete(rawlist);
	/* Cleanup cached debuginfo if needed */
	debuginfo_cache__exit();

	return ret;
}

/* List up current perf-probe events */
int show_perf_probe_events(struct strfilter *filter)
{
	int kp_fd, up_fd, ret;

	setup_pager();

	if (probe_conf.cache)
		return probe_cache__show_all_caches(filter);

	ret = init_probe_symbol_maps(false);
	if (ret < 0)
		return ret;

	ret = probe_file__open_both(&kp_fd, &up_fd, 0);
	if (ret < 0)
		return ret;

	if (kp_fd >= 0)
		ret = __show_perf_probe_events(kp_fd, true, filter);
	if (up_fd >= 0 && ret >= 0)
		ret = __show_perf_probe_events(up_fd, false, filter);
	if (kp_fd > 0)
		close(kp_fd);
	if (up_fd > 0)
		close(up_fd);
	exit_probe_symbol_maps();

	return ret;
}

static int get_new_event_name(char *buf, size_t len, const char *base,
			      struct strlist *namelist, bool ret_event,
			      bool allow_suffix)
{
	int i, ret;
	char *p, *nbase;

	if (*base == '.')
		base++;
	nbase = strdup(base);
	if (!nbase)
		return -ENOMEM;

	/* Cut off the dot suffixes (e.g. .const, .isra) and version suffixes */
	p = strpbrk(nbase, ".@");
	if (p && p != nbase)
		*p = '\0';

	/* Try no suffix number */
	ret = e_snprintf(buf, len, "%s%s", nbase, ret_event ? "__return" : "");
	if (ret < 0) {
		pr_debug("snprintf() failed: %d\n", ret);
		goto out;
	}
	if (!strlist__has_entry(namelist, buf))
		goto out;

	if (!allow_suffix) {
		pr_warning("Error: event \"%s\" already exists.\n"
			   " Hint: Remove existing event by 'perf probe -d'\n"
			   "       or force duplicates by 'perf probe -f'\n"
			   "       or set 'force=yes' in BPF source.\n",
			   buf);
		ret = -EEXIST;
		goto out;
	}

	/* Try to add suffix */
	for (i = 1; i < MAX_EVENT_INDEX; i++) {
		ret = e_snprintf(buf, len, "%s_%d", nbase, i);
		if (ret < 0) {
			pr_debug("snprintf() failed: %d\n", ret);
			goto out;
		}
		if (!strlist__has_entry(namelist, buf))
			break;
	}
	if (i == MAX_EVENT_INDEX) {
		pr_warning("Too many events are on the same function.\n");
		ret = -ERANGE;
	}

out:
	free(nbase);

	/* Final validation */
	if (ret >= 0 && !is_c_func_name(buf)) {
		pr_warning("Internal error: \"%s\" is an invalid event name.\n",
			   buf);
		ret = -EINVAL;
	}

	return ret;
}

/* Warn if the current kernel's uprobe implementation is old */
static void warn_uprobe_event_compat(struct probe_trace_event *tev)
{
	int i;
	char *buf = synthesize_probe_trace_command(tev);
	struct probe_trace_point *tp = &tev->point;

	if (tp->ref_ctr_offset && !uprobe_ref_ctr_is_supported()) {
		pr_warning("A semaphore is associated with %s:%s and "
			   "seems your kernel doesn't support it.\n",
			   tev->group, tev->event);
	}

	/* Old uprobe event doesn't support memory dereference */
	if (!tev->uprobes || tev->nargs == 0 || !buf)
		goto out;

	for (i = 0; i < tev->nargs; i++)
		if (strglobmatch(tev->args[i].value, "[$@+-]*")) {
			pr_warning("Please upgrade your kernel to at least "
				   "3.14 to have access to feature %s\n",
				   tev->args[i].value);
			break;
		}
out:
	free(buf);
}

/* Set new name from original perf_probe_event and namelist */
static int probe_trace_event__set_name(struct probe_trace_event *tev,
				       struct perf_probe_event *pev,
				       struct strlist *namelist,
				       bool allow_suffix)
{
	const char *event, *group;
	char buf[64];
	int ret;

	/* If probe_event or trace_event already have the name, reuse it */
	if (pev->event && !pev->sdt)
		event = pev->event;
	else if (tev->event)
		event = tev->event;
	else {
		/* Or generate new one from probe point */
		if (pev->point.function &&
			(strncmp(pev->point.function, "0x", 2) != 0) &&
			!strisglob(pev->point.function))
			event = pev->point.function;
		else
			event = tev->point.realname;
	}
	if (pev->group && !pev->sdt)
		group = pev->group;
	else if (tev->group)
		group = tev->group;
	else
		group = PERFPROBE_GROUP;

	/* Get an unused new event name */
	ret = get_new_event_name(buf, 64, event, namelist,
				 tev->point.retprobe, allow_suffix);
	if (ret < 0)
		return ret;

	event = buf;

	tev->event = strdup(event);
	tev->group = strdup(group);
	if (tev->event == NULL || tev->group == NULL)
		return -ENOMEM;

	/*
	 * Add new event name to namelist if multiprobe event is NOT
	 * supported, since we have to use new event name for following
	 * probes in that case.
	 */
	if (!multiprobe_event_is_supported())
		strlist__add(namelist, event);
	return 0;
}

static int __open_probe_file_and_namelist(bool uprobe,
					  struct strlist **namelist)
{
	int fd;

	fd = probe_file__open(PF_FL_RW | (uprobe ? PF_FL_UPROBE : 0));
	if (fd < 0)
		return fd;

	/* Get current event names */
	*namelist = probe_file__get_namelist(fd);
	if (!(*namelist)) {
		pr_debug("Failed to get current event list.\n");
		close(fd);
		return -ENOMEM;
	}
	return fd;
}

static int __add_probe_trace_events(struct perf_probe_event *pev,
				     struct probe_trace_event *tevs,
				     int ntevs, bool allow_suffix)
{
	int i, fd[2] = {-1, -1}, up, ret;
	struct probe_trace_event *tev = NULL;
	struct probe_cache *cache = NULL;
	struct strlist *namelist[2] = {NULL, NULL};
	struct nscookie nsc;

	up = pev->uprobes ? 1 : 0;
	fd[up] = __open_probe_file_and_namelist(up, &namelist[up]);
	if (fd[up] < 0)
		return fd[up];

	ret = 0;
	for (i = 0; i < ntevs; i++) {
		tev = &tevs[i];
		up = tev->uprobes ? 1 : 0;
		if (fd[up] == -1) {	/* Open the kprobe/uprobe_events */
			fd[up] = __open_probe_file_and_namelist(up,
								&namelist[up]);
			if (fd[up] < 0)
				goto close_out;
		}
		/* Skip if the symbol is out of .text or blacklisted */
		if (!tev->point.symbol && !pev->uprobes)
			continue;

		/* Set new name for tev (and update namelist) */
		ret = probe_trace_event__set_name(tev, pev, namelist[up],
						  allow_suffix);
		if (ret < 0)
			break;

		nsinfo__mountns_enter(pev->nsi, &nsc);
		ret = probe_file__add_event(fd[up], tev);
		nsinfo__mountns_exit(&nsc);
		if (ret < 0)
			break;

		/*
		 * Probes after the first probe which comes from same
		 * user input are always allowed to add suffix, because
		 * there might be several addresses corresponding to
		 * one code line.
		 */
		allow_suffix = true;
	}
	if (ret == -EINVAL && pev->uprobes)
		warn_uprobe_event_compat(tev);
	if (ret == 0 && probe_conf.cache) {
		cache = probe_cache__new(pev->target, pev->nsi);
		if (!cache ||
		    probe_cache__add_entry(cache, pev, tevs, ntevs) < 0 ||
		    probe_cache__commit(cache) < 0)
			pr_warning("Failed to add event to probe cache\n");
		probe_cache__delete(cache);
	}

close_out:
	for (up = 0; up < 2; up++) {
		strlist__delete(namelist[up]);
		if (fd[up] >= 0)
			close(fd[up]);
	}
	return ret;
}

static int find_probe_functions(struct map *map, char *name,
				struct symbol **syms)
{
	int found = 0;
	struct symbol *sym;
	struct rb_node *tmp;
	const char *norm, *ver;
	char *buf = NULL;
	bool cut_version = true;

	if (map__load(map) < 0)
		return -EACCES;	/* Possible permission error to load symbols */

	/* If user gives a version, don't cut off the version from symbols */
	if (strchr(name, '@'))
		cut_version = false;

	map__for_each_symbol(map, sym, tmp) {
		norm = arch__normalize_symbol_name(sym->name);
		if (!norm)
			continue;

		if (cut_version) {
			/* We don't care about default symbol or not */
			ver = strchr(norm, '@');
			if (ver) {
				buf = strndup(norm, ver - norm);
				if (!buf)
					return -ENOMEM;
				norm = buf;
			}
		}

		if (strglobmatch(norm, name)) {
			found++;
			if (syms && found < probe_conf.max_probes)
				syms[found - 1] = sym;
		}
		if (buf)
			zfree(&buf);
	}

	return found;
}

void __weak arch__fix_tev_from_maps(struct perf_probe_event *pev __maybe_unused,
				struct probe_trace_event *tev __maybe_unused,
				struct map *map __maybe_unused,
				struct symbol *sym __maybe_unused) { }


static void pr_kallsyms_access_error(void)
{
	pr_err("Please ensure you can read the /proc/kallsyms symbol addresses.\n"
	       "If /proc/sys/kernel/kptr_restrict is '2', you can not read\n"
	       "kernel symbol addresses even if you are a superuser. Please change\n"
	       "it to '1'. If kptr_restrict is '1', the superuser can read the\n"
	       "symbol addresses.\n"
	       "In that case, please run this command again with sudo.\n");
}

/*
 * Find probe function addresses from map.
 * Return an error or the number of found probe_trace_event
 */
static int find_probe_trace_events_from_map(struct perf_probe_event *pev,
					    struct probe_trace_event **tevs)
{
	struct map *map = NULL;
	struct ref_reloc_sym *reloc_sym = NULL;
	struct symbol *sym;
	struct symbol **syms = NULL;
	struct probe_trace_event *tev;
	struct perf_probe_point *pp = &pev->point;
	struct probe_trace_point *tp;
	int num_matched_functions;
	int ret, i, j, skipped = 0;
	char *mod_name;

	map = get_target_map(pev->target, pev->nsi, pev->uprobes);
	if (!map) {
		ret = -EINVAL;
		goto out;
	}

	syms = malloc(sizeof(struct symbol *) * probe_conf.max_probes);
	if (!syms) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Load matched symbols: Since the different local symbols may have
	 * same name but different addresses, this lists all the symbols.
	 */
	num_matched_functions = find_probe_functions(map, pp->function, syms);
	if (num_matched_functions <= 0) {
		if (num_matched_functions == -EACCES) {
			pr_err("Failed to load symbols from %s\n",
			       pev->target ?: "/proc/kallsyms");
			if (pev->target)
				pr_err("Please ensure the file is not stripped.\n");
			else
				pr_kallsyms_access_error();
		} else
			pr_err("Failed to find symbol %s in %s\n", pp->function,
				pev->target ? : "kernel");
		ret = -ENOENT;
		goto out;
	} else if (num_matched_functions > probe_conf.max_probes) {
		pr_err("Too many functions matched in %s\n",
			pev->target ? : "kernel");
		ret = -E2BIG;
		goto out;
	}

	/* Note that the symbols in the kmodule are not relocated */
	if (!pev->uprobes && !pev->target &&
			(!pp->retprobe || kretprobe_offset_is_supported())) {
		reloc_sym = kernel_get_ref_reloc_sym(NULL);
		if (!reloc_sym) {
			pr_warning("Relocated base symbol is not found! "
				   "Check /proc/sys/kernel/kptr_restrict\n"
				   "and /proc/sys/kernel/perf_event_paranoid. "
				   "Or run as privileged perf user.\n\n");
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

	for (j = 0; j < num_matched_functions; j++) {
		sym = syms[j];

		if (sym->type != STT_FUNC)
			continue;

		/* There can be duplicated symbols in the map */
		for (i = 0; i < j; i++)
			if (sym->start == syms[i]->start) {
				pr_debug("Found duplicated symbol %s @ %" PRIx64 "\n",
					 sym->name, sym->start);
				break;
			}
		if (i != j)
			continue;

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

		/* Check the kprobe (not in module) is within .text  */
		if (!pev->uprobes && !pev->target &&
		    kprobe_warn_out_range(sym->name, tp->address)) {
			tp->symbol = NULL;	/* Skip it */
			skipped++;
		} else if (reloc_sym) {
			tp->symbol = strdup_or_goto(reloc_sym->name, nomem_out);
			tp->offset = tp->address - reloc_sym->addr;
		} else {
			tp->symbol = strdup_or_goto(sym->name, nomem_out);
			tp->offset = pp->offset;
		}
		tp->realname = strdup_or_goto(sym->name, nomem_out);

		tp->retprobe = pp->retprobe;
		if (pev->target) {
			if (pev->uprobes) {
				tev->point.module = strdup_or_goto(pev->target,
								   nomem_out);
			} else {
				mod_name = find_module_name(pev->target);
				tev->point.module =
					strdup(mod_name ? mod_name : pev->target);
				free(mod_name);
				if (!tev->point.module)
					goto nomem_out;
			}
		}
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
		arch__fix_tev_from_maps(pev, tev, map, sym);
	}
	if (ret == skipped) {
		ret = -ENOENT;
		goto err_out;
	}

out:
	map__put(map);
	free(syms);
	return ret;

nomem_out:
	ret = -ENOMEM;
err_out:
	clear_probe_trace_events(*tevs, num_matched_functions);
	zfree(tevs);
	goto out;
}

static int try_to_find_absolute_address(struct perf_probe_event *pev,
					struct probe_trace_event **tevs)
{
	struct perf_probe_point *pp = &pev->point;
	struct probe_trace_event *tev;
	struct probe_trace_point *tp;
	int i, err;

	if (!(pev->point.function && !strncmp(pev->point.function, "0x", 2)))
		return -EINVAL;
	if (perf_probe_event_need_dwarf(pev))
		return -EINVAL;

	/*
	 * This is 'perf probe /lib/libc.so 0xabcd'. Try to probe at
	 * absolute address.
	 *
	 * Only one tev can be generated by this.
	 */
	*tevs = zalloc(sizeof(*tev));
	if (!*tevs)
		return -ENOMEM;

	tev = *tevs;
	tp = &tev->point;

	/*
	 * Don't use tp->offset, use address directly, because
	 * in synthesize_probe_trace_command() address cannot be
	 * zero.
	 */
	tp->address = pev->point.abs_address;
	tp->retprobe = pp->retprobe;
	tev->uprobes = pev->uprobes;

	err = -ENOMEM;
	/*
	 * Give it a '0x' leading symbol name.
	 * In __add_probe_trace_events, a NULL symbol is interpreted as
	 * invalid.
	 */
	if (asprintf(&tp->symbol, "0x%" PRIx64, tp->address) < 0)
		goto errout;

	/* For kprobe, check range */
	if ((!tev->uprobes) &&
	    (kprobe_warn_out_range(tev->point.symbol,
				   tev->point.address))) {
		err = -EACCES;
		goto errout;
	}

	if (asprintf(&tp->realname, "abs_%" PRIx64, tp->address) < 0)
		goto errout;

	if (pev->target) {
		tp->module = strdup(pev->target);
		if (!tp->module)
			goto errout;
	}

	if (tev->group) {
		tev->group = strdup(pev->group);
		if (!tev->group)
			goto errout;
	}

	if (pev->event) {
		tev->event = strdup(pev->event);
		if (!tev->event)
			goto errout;
	}

	tev->nargs = pev->nargs;
	tev->args = zalloc(sizeof(struct probe_trace_arg) * tev->nargs);
	if (!tev->args)
		goto errout;

	for (i = 0; i < tev->nargs; i++)
		copy_to_probe_trace_arg(&tev->args[i], &pev->args[i]);

	return 1;

errout:
	clear_probe_trace_events(*tevs, 1);
	*tevs = NULL;
	return err;
}

/* Concatenate two arrays */
static void *memcat(void *a, size_t sz_a, void *b, size_t sz_b)
{
	void *ret;

	ret = malloc(sz_a + sz_b);
	if (ret) {
		memcpy(ret, a, sz_a);
		memcpy(ret + sz_a, b, sz_b);
	}
	return ret;
}

static int
concat_probe_trace_events(struct probe_trace_event **tevs, int *ntevs,
			  struct probe_trace_event **tevs2, int ntevs2)
{
	struct probe_trace_event *new_tevs;
	int ret = 0;

	if (*ntevs == 0) {
		*tevs = *tevs2;
		*ntevs = ntevs2;
		*tevs2 = NULL;
		return 0;
	}

	if (*ntevs + ntevs2 > probe_conf.max_probes)
		ret = -E2BIG;
	else {
		/* Concatenate the array of probe_trace_event */
		new_tevs = memcat(*tevs, (*ntevs) * sizeof(**tevs),
				  *tevs2, ntevs2 * sizeof(**tevs2));
		if (!new_tevs)
			ret = -ENOMEM;
		else {
			free(*tevs);
			*tevs = new_tevs;
			*ntevs += ntevs2;
		}
	}
	if (ret < 0)
		clear_probe_trace_events(*tevs2, ntevs2);
	zfree(tevs2);

	return ret;
}

/*
 * Try to find probe_trace_event from given probe caches. Return the number
 * of cached events found, if an error occurs return the error.
 */
static int find_cached_events(struct perf_probe_event *pev,
			      struct probe_trace_event **tevs,
			      const char *target)
{
	struct probe_cache *cache;
	struct probe_cache_entry *entry;
	struct probe_trace_event *tmp_tevs = NULL;
	int ntevs = 0;
	int ret = 0;

	cache = probe_cache__new(target, pev->nsi);
	/* Return 0 ("not found") if the target has no probe cache. */
	if (!cache)
		return 0;

	for_each_probe_cache_entry(entry, cache) {
		/* Skip the cache entry which has no name */
		if (!entry->pev.event || !entry->pev.group)
			continue;
		if ((!pev->group || strglobmatch(entry->pev.group, pev->group)) &&
		    strglobmatch(entry->pev.event, pev->event)) {
			ret = probe_cache_entry__get_event(entry, &tmp_tevs);
			if (ret > 0)
				ret = concat_probe_trace_events(tevs, &ntevs,
								&tmp_tevs, ret);
			if (ret < 0)
				break;
		}
	}
	probe_cache__delete(cache);
	if (ret < 0) {
		clear_probe_trace_events(*tevs, ntevs);
		zfree(tevs);
	} else {
		ret = ntevs;
		if (ntevs > 0 && target && target[0] == '/')
			pev->uprobes = true;
	}

	return ret;
}

/* Try to find probe_trace_event from all probe caches */
static int find_cached_events_all(struct perf_probe_event *pev,
				   struct probe_trace_event **tevs)
{
	struct probe_trace_event *tmp_tevs = NULL;
	struct strlist *bidlist;
	struct str_node *nd;
	char *pathname;
	int ntevs = 0;
	int ret;

	/* Get the buildid list of all valid caches */
	bidlist = build_id_cache__list_all(true);
	if (!bidlist) {
		ret = -errno;
		pr_debug("Failed to get buildids: %d\n", ret);
		return ret;
	}

	ret = 0;
	strlist__for_each_entry(nd, bidlist) {
		pathname = build_id_cache__origname(nd->s);
		ret = find_cached_events(pev, &tmp_tevs, pathname);
		/* In the case of cnt == 0, we just skip it */
		if (ret > 0)
			ret = concat_probe_trace_events(tevs, &ntevs,
							&tmp_tevs, ret);
		free(pathname);
		if (ret < 0)
			break;
	}
	strlist__delete(bidlist);

	if (ret < 0) {
		clear_probe_trace_events(*tevs, ntevs);
		zfree(tevs);
	} else
		ret = ntevs;

	return ret;
}

static int find_probe_trace_events_from_cache(struct perf_probe_event *pev,
					      struct probe_trace_event **tevs)
{
	struct probe_cache *cache;
	struct probe_cache_entry *entry;
	struct probe_trace_event *tev;
	struct str_node *node;
	int ret, i;

	if (pev->sdt) {
		/* For SDT/cached events, we use special search functions */
		if (!pev->target)
			return find_cached_events_all(pev, tevs);
		else
			return find_cached_events(pev, tevs, pev->target);
	}
	cache = probe_cache__new(pev->target, pev->nsi);
	if (!cache)
		return 0;

	entry = probe_cache__find(cache, pev);
	if (!entry) {
		/* SDT must be in the cache */
		ret = pev->sdt ? -ENOENT : 0;
		goto out;
	}

	ret = strlist__nr_entries(entry->tevlist);
	if (ret > probe_conf.max_probes) {
		pr_debug("Too many entries matched in the cache of %s\n",
			 pev->target ? : "kernel");
		ret = -E2BIG;
		goto out;
	}

	*tevs = zalloc(ret * sizeof(*tev));
	if (!*tevs) {
		ret = -ENOMEM;
		goto out;
	}

	i = 0;
	strlist__for_each_entry(node, entry->tevlist) {
		tev = &(*tevs)[i++];
		ret = parse_probe_trace_command(node->s, tev);
		if (ret < 0)
			goto out;
		/* Set the uprobes attribute as same as original */
		tev->uprobes = pev->uprobes;
	}
	ret = i;

out:
	probe_cache__delete(cache);
	return ret;
}

static int convert_to_probe_trace_events(struct perf_probe_event *pev,
					 struct probe_trace_event **tevs)
{
	int ret;

	if (!pev->group && !pev->sdt) {
		/* Set group name if not given */
		if (!pev->uprobes) {
			pev->group = strdup(PERFPROBE_GROUP);
			ret = pev->group ? 0 : -ENOMEM;
		} else
			ret = convert_exec_to_group(pev->target, &pev->group);
		if (ret != 0) {
			pr_warning("Failed to make a group name.\n");
			return ret;
		}
	}

	ret = try_to_find_absolute_address(pev, tevs);
	if (ret > 0)
		return ret;

	/* At first, we need to lookup cache entry */
	ret = find_probe_trace_events_from_cache(pev, tevs);
	if (ret > 0 || pev->sdt)	/* SDT can be found only in the cache */
		return ret == 0 ? -ENOENT : ret; /* Found in probe cache */

	/* Convert perf_probe_event with debuginfo */
	ret = try_to_find_probe_trace_events(pev, tevs);
	if (ret != 0)
		return ret;	/* Found in debuginfo or got an error */

	return find_probe_trace_events_from_map(pev, tevs);
}

int convert_perf_probe_events(struct perf_probe_event *pevs, int npevs)
{
	int i, ret;

	/* Loop 1: convert all events */
	for (i = 0; i < npevs; i++) {
		/* Init kprobe blacklist if needed */
		if (!pevs[i].uprobes)
			kprobe_blacklist__init();
		/* Convert with or without debuginfo */
		ret  = convert_to_probe_trace_events(&pevs[i], &pevs[i].tevs);
		if (ret < 0)
			return ret;
		pevs[i].ntevs = ret;
	}
	/* This just release blacklist only if allocated */
	kprobe_blacklist__release();

	return 0;
}

static int show_probe_trace_event(struct probe_trace_event *tev)
{
	char *buf = synthesize_probe_trace_command(tev);

	if (!buf) {
		pr_debug("Failed to synthesize probe trace event.\n");
		return -EINVAL;
	}

	/* Showing definition always go stdout */
	printf("%s\n", buf);
	free(buf);

	return 0;
}

int show_probe_trace_events(struct perf_probe_event *pevs, int npevs)
{
	struct strlist *namelist = strlist__new(NULL, NULL);
	struct probe_trace_event *tev;
	struct perf_probe_event *pev;
	int i, j, ret = 0;

	if (!namelist)
		return -ENOMEM;

	for (j = 0; j < npevs && !ret; j++) {
		pev = &pevs[j];
		for (i = 0; i < pev->ntevs && !ret; i++) {
			tev = &pev->tevs[i];
			/* Skip if the symbol is out of .text or blacklisted */
			if (!tev->point.symbol && !pev->uprobes)
				continue;

			/* Set new name for tev (and update namelist) */
			ret = probe_trace_event__set_name(tev, pev,
							  namelist, true);
			if (!ret)
				ret = show_probe_trace_event(tev);
		}
	}
	strlist__delete(namelist);

	return ret;
}

static int show_bootconfig_event(struct probe_trace_event *tev)
{
	struct probe_trace_point *tp = &tev->point;
	struct strbuf buf;
	char *ret = NULL;
	int err;

	if (strbuf_init(&buf, 32) < 0)
		return -ENOMEM;

	err = synthesize_kprobe_trace_def(tp, &buf);
	if (err >= 0)
		err = synthesize_probe_trace_args(tev, &buf);
	if (err >= 0)
		ret = strbuf_detach(&buf, NULL);
	strbuf_release(&buf);

	if (ret) {
		printf("'%s'", ret);
		free(ret);
	}

	return err;
}

int show_bootconfig_events(struct perf_probe_event *pevs, int npevs)
{
	struct strlist *namelist = strlist__new(NULL, NULL);
	struct probe_trace_event *tev;
	struct perf_probe_event *pev;
	char *cur_name = NULL;
	int i, j, ret = 0;

	if (!namelist)
		return -ENOMEM;

	for (j = 0; j < npevs && !ret; j++) {
		pev = &pevs[j];
		if (pev->group && strcmp(pev->group, "probe"))
			pr_warning("WARN: Group name %s is ignored\n", pev->group);
		if (pev->uprobes) {
			pr_warning("ERROR: Bootconfig doesn't support uprobes\n");
			ret = -EINVAL;
			break;
		}
		for (i = 0; i < pev->ntevs && !ret; i++) {
			tev = &pev->tevs[i];
			/* Skip if the symbol is out of .text or blacklisted */
			if (!tev->point.symbol && !pev->uprobes)
				continue;

			/* Set new name for tev (and update namelist) */
			ret = probe_trace_event__set_name(tev, pev,
							  namelist, true);
			if (ret)
				break;

			if (!cur_name || strcmp(cur_name, tev->event)) {
				printf("%sftrace.event.kprobes.%s.probe = ",
					cur_name ? "\n" : "", tev->event);
				cur_name = tev->event;
			} else
				printf(", ");
			ret = show_bootconfig_event(tev);
		}
	}
	printf("\n");
	strlist__delete(namelist);

	return ret;
}

int apply_perf_probe_events(struct perf_probe_event *pevs, int npevs)
{
	int i, ret = 0;

	/* Loop 2: add all events */
	for (i = 0; i < npevs; i++) {
		ret = __add_probe_trace_events(&pevs[i], pevs[i].tevs,
					       pevs[i].ntevs,
					       probe_conf.force_add);
		if (ret < 0)
			break;
	}
	return ret;
}

void cleanup_perf_probe_events(struct perf_probe_event *pevs, int npevs)
{
	int i, j;
	struct perf_probe_event *pev;

	/* Loop 3: cleanup and free trace events  */
	for (i = 0; i < npevs; i++) {
		pev = &pevs[i];
		for (j = 0; j < pevs[i].ntevs; j++)
			clear_probe_trace_event(&pevs[i].tevs[j]);
		zfree(&pevs[i].tevs);
		pevs[i].ntevs = 0;
		nsinfo__zput(pev->nsi);
		clear_perf_probe_event(&pevs[i]);
	}
}

int add_perf_probe_events(struct perf_probe_event *pevs, int npevs)
{
	int ret;

	ret = init_probe_symbol_maps(pevs->uprobes);
	if (ret < 0)
		return ret;

	ret = convert_perf_probe_events(pevs, npevs);
	if (ret == 0)
		ret = apply_perf_probe_events(pevs, npevs);

	cleanup_perf_probe_events(pevs, npevs);

	exit_probe_symbol_maps();
	return ret;
}

int del_perf_probe_events(struct strfilter *filter)
{
	int ret, ret2, ufd = -1, kfd = -1;
	char *str = strfilter__string(filter);

	if (!str)
		return -EINVAL;

	/* Get current event names */
	ret = probe_file__open_both(&kfd, &ufd, PF_FL_RW);
	if (ret < 0)
		goto out;

	ret = probe_file__del_events(kfd, filter);
	if (ret < 0 && ret != -ENOENT)
		goto error;

	ret2 = probe_file__del_events(ufd, filter);
	if (ret2 < 0 && ret2 != -ENOENT) {
		ret = ret2;
		goto error;
	}
	ret = 0;

error:
	if (kfd >= 0)
		close(kfd);
	if (ufd >= 0)
		close(ufd);
out:
	free(str);

	return ret;
}

int show_available_funcs(const char *target, struct nsinfo *nsi,
			 struct strfilter *_filter, bool user)
{
        struct rb_node *nd;
	struct map *map;
	int ret;

	ret = init_probe_symbol_maps(user);
	if (ret < 0)
		return ret;

	/* Get a symbol map */
	map = get_target_map(target, nsi, user);
	if (!map) {
		pr_err("Failed to get a map for %s\n", (target) ? : "kernel");
		return -EINVAL;
	}

	ret = map__load(map);
	if (ret) {
		if (ret == -2) {
			char *str = strfilter__string(_filter);
			pr_err("Failed to find symbols matched to \"%s\"\n",
			       str);
			free(str);
		} else
			pr_err("Failed to load symbols in %s\n",
			       (target) ? : "kernel");
		goto end;
	}
	if (!dso__sorted_by_name(map->dso))
		dso__sort_by_name(map->dso);

	/* Show all (filtered) symbols */
	setup_pager();

	for (nd = rb_first_cached(&map->dso->symbol_names); nd;
	     nd = rb_next(nd)) {
		struct symbol_name_rb_node *pos = rb_entry(nd, struct symbol_name_rb_node, rb_node);

		if (strfilter__compare(_filter, pos->sym.name))
			printf("%s\n", pos->sym.name);
	}
end:
	map__put(map);
	exit_probe_symbol_maps();

	return ret;
}

int copy_to_probe_trace_arg(struct probe_trace_arg *tvar,
			    struct perf_probe_arg *pvar)
{
	tvar->value = strdup(pvar->var);
	if (tvar->value == NULL)
		return -ENOMEM;
	if (pvar->type) {
		tvar->type = strdup(pvar->type);
		if (tvar->type == NULL)
			return -ENOMEM;
	}
	if (pvar->name) {
		tvar->name = strdup(pvar->name);
		if (tvar->name == NULL)
			return -ENOMEM;
	} else
		tvar->name = NULL;
	return 0;
}
