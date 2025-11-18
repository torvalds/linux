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

static void map__init(struct map *map, u64 start, u64 end, u64 pgoff,
		      struct dso *dso, u32 prot, u32 flags)
{
	map__set_start(map, start);
	map__set_end(map, end);
	map__set_pgoff(map, pgoff);
	assert(map__reloc(map) == 0);
	map__set_dso(map, dso__get(dso));
	refcount_set(map__refcnt(map), 1);
	RC_CHK_ACCESS(map)->prot = prot;
	RC_CHK_ACCESS(map)->flags = flags;
	map__set_mapping_type(map, MAPPING_TYPE__DSO);
	assert(map__erange_warned(map) == false);
	assert(map__priv(map) == false);
	assert(map__hit(map) == false);
}

struct map *map__new(struct machine *machine, u64 start, u64 len,
		     u64 pgoff, const struct dso_id *id,
		     u32 prot, u32 flags,
		     char *filename, struct thread *thread)
{
	struct map *result;
	RC_STRUCT(map) *map;
	struct nsinfo *nsi = NULL;
	struct nsinfo *nnsi;

	map = zalloc(sizeof(*map));
	if (ADD_RC_CHK(result, map)) {
		char newfilename[PATH_MAX];
		struct dso *dso;
		int anon, no_dso, vdso, android;

		android = is_android_lib(filename);
		anon = is_anon_memory(filename) || flags & MAP_HUGETLB;
		vdso = is_vdso_map(filename);
		no_dso = is_no_dso_memory(filename);
		nsi = nsinfo__get(thread__nsinfo(thread));

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

		assert(!dso__kernel(dso));
		map__init(result, start, start + len, pgoff, dso, prot, flags);

		if (anon || no_dso) {
			map->mapping_type = MAPPING_TYPE__IDENTITY;

			/*
			 * Set memory without DSO as loaded. All map__find_*
			 * functions still return NULL, and we avoid the
			 * unnecessary map__load warning.
			 */
			if (!(prot & PROT_EXEC))
				dso__set_loaded(dso);
		}
		mutex_lock(dso__lock(dso));
		dso__set_nsinfo(dso, nsi);
		mutex_unlock(dso__lock(dso));

		if (!build_id__is_defined(&id->build_id)) {
			/*
			 * If the mmap event had no build ID, search for an existing dso from the
			 * build ID header by name. Otherwise only the dso loaded at the time of
			 * reading the header will have the build ID set and all future mmaps will
			 * have it missing.
			 */
			struct dso *header_bid_dso = dsos__find(&machine->dsos, filename, false);

			if (header_bid_dso && dso__header_build_id(header_bid_dso)) {
				dso__set_build_id(dso, dso__bid(header_bid_dso));
				dso__set_header_build_id(dso, 1);
			}
			dso__put(header_bid_dso);
		}
		dso__put(dso);
	}
	return result;
out_delete:
	nsinfo__put(nsi);
	RC_CHK_FREE(result);
	return NULL;
}

/*
 * Constructor variant for modules (where we know from /proc/modules where
 * they are loaded) and for vmlinux, where only after we load all the
 * symbols we'll know where it starts and ends.
 */
struct map *map__new2(u64 start, struct dso *dso)
{
	struct map *result;
	RC_STRUCT(map) *map;

	map = calloc(1, sizeof(*map) + (dso__kernel(dso) ? sizeof(struct kmap) : 0));
	if (ADD_RC_CHK(result, map)) {
		/* ->end will be filled after we load all the symbols. */
		map__init(result, start, /*end=*/0, /*pgoff=*/0, dso, /*prot=*/0, /*flags=*/0);
	}

	return result;
}

bool __map__is_kernel(const struct map *map)
{
	if (!dso__kernel(map__dso(map)))
		return false;
	return machine__kernel_map(maps__machine(map__kmaps((struct map *)map))) == map;
}

bool __map__is_extra_kernel_map(const struct map *map)
{
	struct kmap *kmap = __map__kmap((struct map *)map);

	return kmap && kmap->name[0];
}

bool __map__is_bpf_prog(const struct map *map)
{
	const char *name;
	struct dso *dso = map__dso(map);

	if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_PROG_INFO)
		return true;

	/*
	 * If PERF_RECORD_BPF_EVENT is not included, the dso will not have
	 * type of DSO_BINARY_TYPE__BPF_PROG_INFO. In such cases, we can
	 * guess the type based on name.
	 */
	name = dso__short_name(dso);
	return name && (strstr(name, "bpf_prog_") == name);
}

bool __map__is_bpf_image(const struct map *map)
{
	const char *name;
	struct dso *dso = map__dso(map);

	if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_IMAGE)
		return true;

	/*
	 * If PERF_RECORD_KSYMBOL is not included, the dso will not have
	 * type of DSO_BINARY_TYPE__BPF_IMAGE. In such cases, we can
	 * guess the type based on name.
	 */
	name = dso__short_name(dso);
	return name && is_bpf_image(name);
}

bool __map__is_ool(const struct map *map)
{
	const struct dso *dso = map__dso(map);

	return dso && dso__binary_type(dso) == DSO_BINARY_TYPE__OOL;
}

bool map__has_symbols(const struct map *map)
{
	return dso__has_symbols(map__dso(map));
}

static void map__exit(struct map *map)
{
	BUG_ON(refcount_read(map__refcnt(map)) != 0);
	dso__zput(RC_CHK_ACCESS(map)->dso);
}

void map__delete(struct map *map)
{
	map__exit(map);
	RC_CHK_FREE(map);
}

void map__put(struct map *map)
{
	if (map && refcount_dec_and_test(map__refcnt(map)))
		map__delete(map);
	else
		RC_CHK_PUT(map);
}

void map__fixup_start(struct map *map)
{
	struct dso *dso = map__dso(map);
	struct rb_root_cached *symbols = dso__symbols(dso);
	struct rb_node *nd = rb_first_cached(symbols);

	if (nd != NULL) {
		struct symbol *sym = rb_entry(nd, struct symbol, rb_node);

		map__set_start(map, sym->start);
	}
}

void map__fixup_end(struct map *map)
{
	struct dso *dso = map__dso(map);
	struct rb_root_cached *symbols = dso__symbols(dso);
	struct rb_node *nd = rb_last(&symbols->rb_root);

	if (nd != NULL) {
		struct symbol *sym = rb_entry(nd, struct symbol, rb_node);
		map__set_end(map, sym->end);
	}
}

#define DSO__DELETED "(deleted)"

int map__load(struct map *map)
{
	struct dso *dso = map__dso(map);
	const char *name = dso__long_name(dso);
	int nr;

	if (dso__loaded(dso))
		return 0;

	nr = dso__load(dso, map);
	if (nr < 0) {
		if (dso__has_build_id(dso)) {
			char sbuild_id[SBUILD_ID_SIZE];

			build_id__snprintf(dso__bid(dso), sbuild_id, sizeof(sbuild_id));
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

	return dso__find_symbol(map__dso(map), addr);
}

struct symbol *map__find_symbol_by_name_idx(struct map *map, const char *name, size_t *idx)
{
	struct dso *dso;

	if (map__load(map) < 0)
		return NULL;

	dso = map__dso(map);
	dso__sort_by_name(dso);

	return dso__find_symbol_by_name(dso, name, idx);
}

struct symbol *map__find_symbol_by_name(struct map *map, const char *name)
{
	size_t idx;

	return map__find_symbol_by_name_idx(map, name, &idx);
}

struct map *map__clone(struct map *from)
{
	struct map *result;
	RC_STRUCT(map) *map;
	size_t size = sizeof(RC_STRUCT(map));
	struct dso *dso = map__dso(from);

	if (dso && dso__kernel(dso))
		size += sizeof(struct kmap);

	map = memdup(RC_CHK_ACCESS(from), size);
	if (ADD_RC_CHK(result, map)) {
		refcount_set(&map->refcnt, 1);
		map->dso = dso__get(dso);
	}

	return result;
}

size_t map__fprintf(struct map *map, FILE *fp)
{
	const struct dso *dso = map__dso(map);

	return fprintf(fp, " %" PRIx64 "-%" PRIx64 " %" PRIx64 " %s\n",
		       map__start(map), map__end(map), map__pgoff(map), dso__name(dso));
}

static bool prefer_dso_long_name(const struct dso *dso, bool print_off)
{
	return dso__long_name(dso) &&
	       (symbol_conf.show_kernel_path ||
		(print_off && (dso__name(dso)[0] == '[' || dso__is_kcore(dso))));
}

static size_t __map__fprintf_dsoname(struct map *map, bool print_off, FILE *fp)
{
	char buf[symbol_conf.pad_output_len_dso + 1];
	const char *dsoname = "[unknown]";
	const struct dso *dso = map ? map__dso(map) : NULL;

	if (dso) {
		if (prefer_dso_long_name(dso, print_off))
			dsoname = dso__long_name(dso);
		else
			dsoname = dso__name(dso);
	}

	if (symbol_conf.pad_output_len_dso) {
		scnprintf_pad(buf, symbol_conf.pad_output_len_dso, "%s", dsoname);
		dsoname = buf;
	}

	return fprintf(fp, "%s", dsoname);
}

size_t map__fprintf_dsoname(struct map *map, FILE *fp)
{
	return __map__fprintf_dsoname(map, false, fp);
}

size_t map__fprintf_dsoname_dsoff(struct map *map, bool print_off, u64 addr, FILE *fp)
{
	const struct dso *dso = map ? map__dso(map) : NULL;
	int printed = 0;

	if (print_off && (!dso || !dso__is_object_file(dso)))
		print_off = false;
	printed += fprintf(fp, " (");
	printed += __map__fprintf_dsoname(map, print_off, fp);
	if (print_off)
		printed += fprintf(fp, "+0x%" PRIx64, addr);
	printed += fprintf(fp, ")");

	return printed;
}

char *map__srcline(struct map *map, u64 addr, struct symbol *sym)
{
	if (map == NULL)
		return SRCLINE_UNKNOWN;

	return get_srcline(map__dso(map), map__rip_2objdump(map, addr), sym, true, true, addr);
}

int map__fprintf_srcline(struct map *map, u64 addr, const char *prefix,
			 FILE *fp)
{
	const struct dso *dso = map ? map__dso(map) : NULL;
	int ret = 0;

	if (dso) {
		char *srcline = map__srcline(map, addr, NULL);
		if (srcline != SRCLINE_UNKNOWN)
			ret = fprintf(fp, "%s%s", prefix, srcline);
		zfree_srcline(&srcline);
	}
	return ret;
}

void srccode_state_free(struct srccode_state *state)
{
	zfree(&state->srcfile);
	state->line = 0;
}

static const struct kmap *__map__const_kmap(const struct map *map);

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
u64 map__rip_2objdump(const struct map *map, u64 rip)
{
	const struct kmap *kmap = __map__const_kmap(map);
	const struct dso *dso = map__dso(map);

	/*
	 * vmlinux does not have program headers for PTI entry trampolines and
	 * kcore may not either. However the trampoline object code is on the
	 * main kernel map, so just use that instead.
	 */
	if (kmap && is_entry_trampoline(kmap->name) && kmap->kmaps) {
		struct machine *machine = maps__machine(kmap->kmaps);

		if (machine) {
			struct map *kernel_map = machine__kernel_map(machine);

			if (kernel_map)
				map = kernel_map;
		}
	}

	if (!dso__adjust_symbols(dso))
		return rip;

	if (dso__rel(dso))
		return rip - map__pgoff(map);

	if (dso__kernel(dso) == DSO_SPACE__USER)
		return rip + dso__text_offset(dso);

	return map__unmap_ip(map, rip) - map__reloc(map);
}

/**
 * map__objdump_2mem - convert objdump address to a memory address.
 * @map: memory map
 * @ip: objdump address
 *
 * Closely related to map__rip_2objdump(), this function takes an address from
 * objdump and converts it to a memory address.  Note this assumes that @map
 * contains the address.  To be sure the result is valid, check it forwards
 * e.g. map__rip_2objdump(map__map_ip(map, map__objdump_2mem(map, ip))) == ip
 *
 * Return: Memory address.
 */
u64 map__objdump_2mem(const struct map *map, u64 ip)
{
	const struct dso *dso = map__dso(map);

	if (!dso__adjust_symbols(dso))
		return map__unmap_ip(map, ip);

	if (dso__rel(dso))
		return map__unmap_ip(map, ip + map__pgoff(map));

	if (dso__kernel(dso) == DSO_SPACE__USER)
		return map__unmap_ip(map, ip - dso__text_offset(dso));

	return ip + map__reloc(map);
}

/* convert objdump address to relative address.  (To be removed) */
u64 map__objdump_2rip(const struct map *map, u64 ip)
{
	const struct dso *dso = map__dso(map);

	if (!dso__adjust_symbols(dso))
		return ip;

	if (dso__rel(dso))
		return ip + map__pgoff(map);

	if (dso__kernel(dso) == DSO_SPACE__USER)
		return ip - dso__text_offset(dso);

	return map__map_ip(map, ip + map__reloc(map));
}

bool map__contains_symbol(const struct map *map, const struct symbol *sym)
{
	u64 ip = map__unmap_ip(map, sym->start);

	return ip >= map__start(map) && ip < map__end(map);
}

struct kmap *__map__kmap(struct map *map)
{
	const struct dso *dso = map__dso(map);

	if (!dso || !dso__kernel(dso))
		return NULL;
	return (struct kmap *)(&RC_CHK_ACCESS(map)[1]);
}

static const struct kmap *__map__const_kmap(const struct map *map)
{
	const struct dso *dso = map__dso(map);

	if (!dso || !dso__kernel(dso))
		return NULL;
	return (struct kmap *)(&RC_CHK_ACCESS(map)[1]);
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
