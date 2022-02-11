// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <uapi/linux/mman.h> /* To get things like MAP_HUGETLB even on older libc headers */
#include "debug.h"
#include "dso.h"
#include "map.h"
#include "namespaces.h"
#include "srcline.h"
#include "symbol.h"
#include "thread.h"
#include "vdso.h"

static inline int is_android_lib(const char *filename)
{
	return strstarts(filename, "/data/app-lib/") ||
	       strstarts(filename, "/system/lib/");
}

static inline bool replace_android_lib(const char *filename, char *newfilename)
{
	const char *libname;
	char *app_abi;
	size_t app_abi_length, new_length;
	size_t lib_length = 0;

	libname  = strrchr(filename, '/');
	if (libname)
		lib_length = strlen(libname);

	app_abi = getenv("APP_ABI");
	if (!app_abi)
		return false;

	app_abi_length = strlen(app_abi);

	if (strstarts(filename, "/data/app-lib/")) {
		char *apk_path;

		if (!app_abi_length)
			return false;

		new_length = 7 + app_abi_length + lib_length;

		apk_path = getenv("APK_PATH");
		if (apk_path) {
			new_length += strlen(apk_path) + 1;
			if (new_length > PATH_MAX)
				return false;
			snprintf(newfilename, new_length,
				 "%s/libs/%s/%s", apk_path, app_abi, libname);
		} else {
			if (new_length > PATH_MAX)
				return false;
			snprintf(newfilename, new_length,
				 "libs/%s/%s", app_abi, libname);
		}
		return true;
	}

	if (strstarts(filename, "/system/lib/")) {
		char *ndk, *app;
		const char *arch;
		int ndk_length, app_length;

		ndk = getenv("NDK_ROOT");
		app = getenv("APP_PLATFORM");

		if (!(ndk && app))
			return false;

		ndk_length = strlen(ndk);
		app_length = strlen(app);

		if (!(ndk_length && app_length && app_abi_length))
			return false;

		arch = !strncmp(app_abi, "arm", 3) ? "arm" :
		       !strncmp(app_abi, "mips", 4) ? "mips" :
		       !strncmp(app_abi, "x86", 3) ? "x86" : NULL;

		if (!arch)
			return false;

		new_length = 27 + ndk_length +
			     app_length + lib_length
			   + strlen(arch);

		if (new_length > PATH_MAX)
			return false;
		snprintf(newfilename, new_length,
			"%.*s/platforms/%.*s/arch-%s/usr/lib/%s",
			ndk_length, ndk, app_length, app, arch, libname);

		return true;
	}
	return false;
}

void map__init(struct map *map, u64 start, u64 end, u64 pgoff, struct dso *dso)
{
	map->start    = start;
	map->end      = end;
	map->pgoff    = pgoff;
	map->reloc    = 0;
	map->dso      = dso__get(dso);
	map->map_ip   = map__map_ip;
	map->unmap_ip = map__unmap_ip;
	RB_CLEAR_NODE(&map->rb_node);
	map->erange_warned = false;
	refcount_set(&map->refcnt, 1);
}

struct map *map__new(struct machine *machine, u64 start, u64 len,
		     u64 pgoff, struct dso_id *id,
		     u32 prot, u32 flags, struct build_id *bid,
		     char *filename, struct thread *thread)
{
	struct map *map = malloc(sizeof(*map));
	struct nsinfo *nsi = NULL;
	struct nsinfo *nnsi;

	if (map != NULL) {
		char newfilename[PATH_MAX];
		struct dso *dso;
		int anon, no_dso, vdso, android;

		android = is_android_lib(filename);
		anon = is_anon_memory(filename) || flags & MAP_HUGETLB;
		vdso = is_vdso_map(filename);
		no_dso = is_no_dso_memory(filename);
		map->prot = prot;
		map->flags = flags;
		nsi = nsinfo__get(thread->nsinfo);

		if ((anon || no_dso) && nsi && (prot & PROT_EXEC)) {
			snprintf(newfilename, sizeof(newfilename),
				 "/tmp/perf-%d.map", nsinfo__pid(nsi));
			filename = newfilename;
		}

		if (android) {
			if (replace_android_lib(filename, newfilename))
				filename = newfilename;
		}

		if (vdso) {
			/* The vdso maps are always on the host and not the
			 * container.  Ensure that we don't use setns to look
			 * them up.
			 */
			nnsi = nsinfo__copy(nsi);
			if (nnsi) {
				nsinfo__put(nsi);
				nsinfo__clear_need_setns(nnsi);
				nsi = nnsi;
			}
			pgoff = 0;
			dso = machine__findnew_vdso(machine, thread);
		} else
			dso = machine__findnew_dso_id(machine, filename, id);

		if (dso == NULL)
			goto out_delete;

		map__init(map, start, start + len, pgoff, dso);

		if (anon || no_dso) {
			map->map_ip = map->unmap_ip = identity__map_ip;

			/*
			 * Set memory without DSO as loaded. All map__find_*
			 * functions still return NULL, and we avoid the
			 * unnecessary map__load warning.
			 */
			if (!(prot & PROT_EXEC))
				dso__set_loaded(dso);
		}
		dso->nsinfo = nsi;

		if (build_id__is_defined(bid))
			dso__set_build_id(dso, bid);

		dso__put(dso);
	}
	return map;
out_delete:
	nsinfo__put(nsi);
	free(map);
	return NULL;
}

/*
 * Constructor variant for modules (where we know from /proc/modules where
 * they are loaded) and for vmlinux, where only after we load all the
 * symbols we'll know where it starts and ends.
 */
struct map *map__new2(u64 start, struct dso *dso)
{
	struct map *map = calloc(1, (sizeof(*map) +
				     (dso->kernel ? sizeof(struct kmap) : 0)));
	if (map != NULL) {
		/*
		 * ->end will be filled after we load all the symbols
		 */
		map__init(map, start, 0, 0, dso);
	}

	return map;
}

bool __map__is_kernel(const struct map *map)
{
	if (!map->dso->kernel)
		return false;
	return machine__kernel_map(map__kmaps((struct map *)map)->machine) == map;
}

bool __map__is_extra_kernel_map(const struct map *map)
{
	struct kmap *kmap = __map__kmap((struct map *)map);

	return kmap && kmap->name[0];
}

bool __map__is_bpf_prog(const struct map *map)
{
	const char *name;

	if (map->dso->binary_type == DSO_BINARY_TYPE__BPF_PROG_INFO)
		return true;

	/*
	 * If PERF_RECORD_BPF_EVENT is not included, the dso will not have
	 * type of DSO_BINARY_TYPE__BPF_PROG_INFO. In such cases, we can
	 * guess the type based on name.
	 */
	name = map->dso->short_name;
	return name && (strstr(name, "bpf_prog_") == name);
}

bool __map__is_bpf_image(const struct map *map)
{
	const char *name;

	if (map->dso->binary_type == DSO_BINARY_TYPE__BPF_IMAGE)
		return true;

	/*
	 * If PERF_RECORD_KSYMBOL is not included, the dso will not have
	 * type of DSO_BINARY_TYPE__BPF_IMAGE. In such cases, we can
	 * guess the type based on name.
	 */
	name = map->dso->short_name;
	return name && is_bpf_image(name);
}

bool __map__is_ool(const struct map *map)
{
	return map->dso && map->dso->binary_type == DSO_BINARY_TYPE__OOL;
}

bool map__has_symbols(const struct map *map)
{
	return dso__has_symbols(map->dso);
}

static void map__exit(struct map *map)
{
	BUG_ON(refcount_read(&map->refcnt) != 0);
	dso__zput(map->dso);
}

void map__delete(struct map *map)
{
	map__exit(map);
	free(map);
}

void map__put(struct map *map)
{
	if (map && refcount_dec_and_test(&map->refcnt))
		map__delete(map);
}

void map__fixup_start(struct map *map)
{
	struct rb_root_cached *symbols = &map->dso->symbols;
	struct rb_node *nd = rb_first_cached(symbols);
	if (nd != NULL) {
		struct symbol *sym = rb_entry(nd, struct symbol, rb_node);
		map->start = sym->start;
	}
}

void map__fixup_end(struct map *map)
{
	struct rb_root_cached *symbols = &map->dso->symbols;
	struct rb_node *nd = rb_last(&symbols->rb_root);
	if (nd != NULL) {
		struct symbol *sym = rb_entry(nd, struct symbol, rb_node);
		map->end = sym->end;
	}
}

#define DSO__DELETED "(deleted)"

int map__load(struct map *map)
{
	const char *name = map->dso->long_name;
	int nr;

	if (dso__loaded(map->dso))
		return 0;

	nr = dso__load(map->dso, map);
	if (nr < 0) {
		if (map->dso->has_build_id) {
			char sbuild_id[SBUILD_ID_SIZE];

			build_id__sprintf(&map->dso->bid, sbuild_id);
			pr_debug("%s with build id %s not found", name, sbuild_id);
		} else
			pr_debug("Failed to open %s", name);

		pr_debug(", continuing without symbols\n");
		return -1;
	} else if (nr == 0) {
#ifdef HAVE_LIBELF_SUPPORT
		const size_t len = strlen(name);
		const size_t real_len = len - sizeof(DSO__DELETED);

		if (len > sizeof(DSO__DELETED) &&
		    strcmp(name + real_len + 1, DSO__DELETED) == 0) {
			pr_debug("%.*s was updated (is prelink enabled?). "
				"Restart the long running apps that use it!\n",
				   (int)real_len, name);
		} else {
			pr_debug("no symbols found in %s, maybe install a debug package?\n", name);
		}
#endif
		return -1;
	}

	return 0;
}

struct symbol *map__find_symbol(struct map *map, u64 addr)
{
	if (map__load(map) < 0)
		return NULL;

	return dso__find_symbol(map->dso, addr);
}

struct symbol *map__find_symbol_by_name(struct map *map, const char *name)
{
	if (map__load(map) < 0)
		return NULL;

	if (!dso__sorted_by_name(map->dso))
		dso__sort_by_name(map->dso);

	return dso__find_symbol_by_name(map->dso, name);
}

struct map *map__clone(struct map *from)
{
	size_t size = sizeof(struct map);
	struct map *map;

	if (from->dso && from->dso->kernel)
		size += sizeof(struct kmap);

	map = memdup(from, size);
	if (map != NULL) {
		refcount_set(&map->refcnt, 1);
		RB_CLEAR_NODE(&map->rb_node);
		dso__get(map->dso);
	}

	return map;
}

size_t map__fprintf(struct map *map, FILE *fp)
{
	return fprintf(fp, " %" PRIx64 "-%" PRIx64 " %" PRIx64 " %s\n",
		       map->start, map->end, map->pgoff, map->dso->name);
}

size_t map__fprintf_dsoname(struct map *map, FILE *fp)
{
	char buf[symbol_conf.pad_output_len_dso + 1];
	const char *dsoname = "[unknown]";

	if (map && map->dso) {
		if (symbol_conf.show_kernel_path && map->dso->long_name)
			dsoname = map->dso->long_name;
		else
			dsoname = map->dso->name;
	}

	if (symbol_conf.pad_output_len_dso) {
		scnprintf_pad(buf, symbol_conf.pad_output_len_dso, "%s", dsoname);
		dsoname = buf;
	}

	return fprintf(fp, "%s", dsoname);
}

char *map__srcline(struct map *map, u64 addr, struct symbol *sym)
{
	if (map == NULL)
		return SRCLINE_UNKNOWN;
	return get_srcline(map->dso, map__rip_2objdump(map, addr), sym, true, true, addr);
}

int map__fprintf_srcline(struct map *map, u64 addr, const char *prefix,
			 FILE *fp)
{
	int ret = 0;

	if (map && map->dso) {
		char *srcline = map__srcline(map, addr, NULL);
		if (strncmp(srcline, SRCLINE_UNKNOWN, strlen(SRCLINE_UNKNOWN)) != 0)
			ret = fprintf(fp, "%s%s", prefix, srcline);
		free_srcline(srcline);
	}
	return ret;
}

void srccode_state_free(struct srccode_state *state)
{
	zfree(&state->srcfile);
	state->line = 0;
}

/**
 * map__rip_2objdump - convert symbol start address to objdump address.
 * @map: memory map
 * @rip: symbol start address
 *
 * objdump wants/reports absolute IPs for ET_EXEC, and RIPs for ET_DYN.
 * map->dso->adjust_symbols==1 for ET_EXEC-like cases except ET_REL which is
 * relative to section start.
 *
 * Return: Address suitable for passing to "objdump --start-address="
 */
u64 map__rip_2objdump(struct map *map, u64 rip)
{
	struct kmap *kmap = __map__kmap(map);

	/*
	 * vmlinux does not have program headers for PTI entry trampolines and
	 * kcore may not either. However the trampoline object code is on the
	 * main kernel map, so just use that instead.
	 */
	if (kmap && is_entry_trampoline(kmap->name) && kmap->kmaps && kmap->kmaps->machine) {
		struct map *kernel_map = machine__kernel_map(kmap->kmaps->machine);

		if (kernel_map)
			map = kernel_map;
	}

	if (!map->dso->adjust_symbols)
		return rip;

	if (map->dso->rel)
		return rip - map->pgoff;

	/*
	 * kernel modules also have DSO_TYPE_USER in dso->kernel,
	 * but all kernel modules are ET_REL, so won't get here.
	 */
	if (map->dso->kernel == DSO_SPACE__USER)
		return rip + map->dso->text_offset;

	return map->unmap_ip(map, rip) - map->reloc;
}

/**
 * map__objdump_2mem - convert objdump address to a memory address.
 * @map: memory map
 * @ip: objdump address
 *
 * Closely related to map__rip_2objdump(), this function takes an address from
 * objdump and converts it to a memory address.  Note this assumes that @map
 * contains the address.  To be sure the result is valid, check it forwards
 * e.g. map__rip_2objdump(map->map_ip(map, map__objdump_2mem(map, ip))) == ip
 *
 * Return: Memory address.
 */
u64 map__objdump_2mem(struct map *map, u64 ip)
{
	if (!map->dso->adjust_symbols)
		return map->unmap_ip(map, ip);

	if (map->dso->rel)
		return map->unmap_ip(map, ip + map->pgoff);

	/*
	 * kernel modules also have DSO_TYPE_USER in dso->kernel,
	 * but all kernel modules are ET_REL, so won't get here.
	 */
	if (map->dso->kernel == DSO_SPACE__USER)
		return map->unmap_ip(map, ip - map->dso->text_offset);

	return ip + map->reloc;
}

bool map__contains_symbol(struct map *map, struct symbol *sym)
{
	u64 ip = map->unmap_ip(map, sym->start);

	return ip >= map->start && ip < map->end;
}

static struct map *__map__next(struct map *map)
{
	struct rb_node *next = rb_next(&map->rb_node);

	if (next)
		return rb_entry(next, struct map, rb_node);
	return NULL;
}

struct map *map__next(struct map *map)
{
	return map ? __map__next(map) : NULL;
}

struct kmap *__map__kmap(struct map *map)
{
	if (!map->dso || !map->dso->kernel)
		return NULL;
	return (struct kmap *)(map + 1);
}

struct kmap *map__kmap(struct map *map)
{
	struct kmap *kmap = __map__kmap(map);

	if (!kmap)
		pr_err("Internal error: map__kmap with a non-kernel map\n");
	return kmap;
}

struct maps *map__kmaps(struct map *map)
{
	struct kmap *kmap = map__kmap(map);

	if (!kmap || !kmap->kmaps) {
		pr_err("Internal error: map__kmaps with a non-kernel map\n");
		return NULL;
	}
	return kmap->kmaps;
}

u64 map__map_ip(const struct map *map, u64 ip)
{
	return ip - map->start + map->pgoff;
}

u64 map__unmap_ip(const struct map *map, u64 ip)
{
	return ip + map->start - map->pgoff;
}

u64 identity__map_ip(const struct map *map __maybe_unused, u64 ip)
{
	return ip;
}
