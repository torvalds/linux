#include "symbol.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "map.h"
#include "thread.h"
#include "strlist.h"
#include "vdso.h"
#include "build-id.h"
#include "util.h"
#include "debug.h"
#include "machine.h"
#include <linux/string.h>
#include "unwind.h"

static void __maps__insert(struct maps *maps, struct map *map);

const char *map_type__name[MAP__NR_TYPES] = {
	[MAP__FUNCTION] = "Functions",
	[MAP__VARIABLE] = "Variables",
};

static inline int is_anon_memory(const char *filename)
{
	return !strcmp(filename, "//anon") ||
	       !strncmp(filename, "/dev/zero", sizeof("/dev/zero") - 1) ||
	       !strncmp(filename, "/anon_hugepage", sizeof("/anon_hugepage") - 1);
}

static inline int is_no_dso_memory(const char *filename)
{
	return !strncmp(filename, "[stack", 6) ||
	       !strncmp(filename, "/SYSV",5)   ||
	       !strcmp(filename, "[heap]");
}

static inline int is_android_lib(const char *filename)
{
	return !strncmp(filename, "/data/app-lib", 13) ||
	       !strncmp(filename, "/system/lib", 11);
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

	if (!strncmp(filename, "/data/app-lib", 13)) {
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

	if (!strncmp(filename, "/system/lib/", 11)) {
		char *ndk, *app;
		const char *arch;
		size_t ndk_length;
		size_t app_length;

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
			"%s/platforms/%s/arch-%s/usr/lib/%s",
			ndk, app, arch, libname);

		return true;
	}
	return false;
}

void map__init(struct map *map, enum map_type type,
	       u64 start, u64 end, u64 pgoff, struct dso *dso)
{
	map->type     = type;
	map->start    = start;
	map->end      = end;
	map->pgoff    = pgoff;
	map->reloc    = 0;
	map->dso      = dso__get(dso);
	map->map_ip   = map__map_ip;
	map->unmap_ip = map__unmap_ip;
	RB_CLEAR_NODE(&map->rb_node);
	map->groups   = NULL;
	map->erange_warned = false;
	atomic_set(&map->refcnt, 1);
}

struct map *map__new(struct machine *machine, u64 start, u64 len,
		     u64 pgoff, u32 pid, u32 d_maj, u32 d_min, u64 ino,
		     u64 ino_gen, u32 prot, u32 flags, char *filename,
		     enum map_type type, struct thread *thread)
{
	struct map *map = malloc(sizeof(*map));

	if (map != NULL) {
		char newfilename[PATH_MAX];
		struct dso *dso;
		int anon, no_dso, vdso, android;

		android = is_android_lib(filename);
		anon = is_anon_memory(filename);
		vdso = is_vdso_map(filename);
		no_dso = is_no_dso_memory(filename);

		map->maj = d_maj;
		map->min = d_min;
		map->ino = ino;
		map->ino_generation = ino_gen;
		map->prot = prot;
		map->flags = flags;

		if ((anon || no_dso) && type == MAP__FUNCTION) {
			snprintf(newfilename, sizeof(newfilename), "/tmp/perf-%d.map", pid);
			filename = newfilename;
		}

		if (android) {
			if (replace_android_lib(filename, newfilename))
				filename = newfilename;
		}

		if (vdso) {
			pgoff = 0;
			dso = machine__findnew_vdso(machine, thread);
		} else
			dso = machine__findnew_dso(machine, filename);

		if (dso == NULL)
			goto out_delete;

		map__init(map, type, start, start + len, pgoff, dso);

		if (anon || no_dso) {
			map->map_ip = map->unmap_ip = identity__map_ip;

			/*
			 * Set memory without DSO as loaded. All map__find_*
			 * functions still return NULL, and we avoid the
			 * unnecessary map__load warning.
			 */
			if (type != MAP__FUNCTION)
				dso__set_loaded(dso, map->type);
		}
		dso__put(dso);
	}
	return map;
out_delete:
	free(map);
	return NULL;
}

/*
 * Constructor variant for modules (where we know from /proc/modules where
 * they are loaded) and for vmlinux, where only after we load all the
 * symbols we'll know where it starts and ends.
 */
struct map *map__new2(u64 start, struct dso *dso, enum map_type type)
{
	struct map *map = calloc(1, (sizeof(*map) +
				     (dso->kernel ? sizeof(struct kmap) : 0)));
	if (map != NULL) {
		/*
		 * ->end will be filled after we load all the symbols
		 */
		map__init(map, type, start, 0, 0, dso);
	}

	return map;
}

/*
 * Use this and __map__is_kmodule() for map instances that are in
 * machine->kmaps, and thus have map->groups->machine all properly set, to
 * disambiguate between the kernel and modules.
 *
 * When the need arises, introduce map__is_{kernel,kmodule)() that
 * checks (map->groups != NULL && map->groups->machine != NULL &&
 * map->dso->kernel) before calling __map__is_{kernel,kmodule}())
 */
bool __map__is_kernel(const struct map *map)
{
	return __machine__kernel_map(map->groups->machine, map->type) == map;
}

static void map__exit(struct map *map)
{
	BUG_ON(!RB_EMPTY_NODE(&map->rb_node));
	dso__zput(map->dso);
}

void map__delete(struct map *map)
{
	map__exit(map);
	free(map);
}

void map__put(struct map *map)
{
	if (map && atomic_dec_and_test(&map->refcnt))
		map__delete(map);
}

void map__fixup_start(struct map *map)
{
	struct rb_root *symbols = &map->dso->symbols[map->type];
	struct rb_node *nd = rb_first(symbols);
	if (nd != NULL) {
		struct symbol *sym = rb_entry(nd, struct symbol, rb_node);
		map->start = sym->start;
	}
}

void map__fixup_end(struct map *map)
{
	struct rb_root *symbols = &map->dso->symbols[map->type];
	struct rb_node *nd = rb_last(symbols);
	if (nd != NULL) {
		struct symbol *sym = rb_entry(nd, struct symbol, rb_node);
		map->end = sym->end;
	}
}

#define DSO__DELETED "(deleted)"

int map__load(struct map *map, symbol_filter_t filter)
{
	const char *name = map->dso->long_name;
	int nr;

	if (dso__loaded(map->dso, map->type))
		return 0;

	nr = dso__load(map->dso, map, filter);
	if (nr < 0) {
		if (map->dso->has_build_id) {
			char sbuild_id[SBUILD_ID_SIZE];

			build_id__sprintf(map->dso->build_id,
					  sizeof(map->dso->build_id),
					  sbuild_id);
			pr_warning("%s with build id %s not found",
				   name, sbuild_id);
		} else
			pr_warning("Failed to open %s", name);

		pr_warning(", continuing without symbols\n");
		return -1;
	} else if (nr == 0) {
#ifdef HAVE_LIBELF_SUPPORT
		const size_t len = strlen(name);
		const size_t real_len = len - sizeof(DSO__DELETED);

		if (len > sizeof(DSO__DELETED) &&
		    strcmp(name + real_len + 1, DSO__DELETED) == 0) {
			pr_warning("%.*s was updated (is prelink enabled?). "
				"Restart the long running apps that use it!\n",
				   (int)real_len, name);
		} else if (filter) {
			pr_warning("no symbols passed the given filter.\n");
			return -2;	/* Empty but maybe by the filter */
		} else {
			pr_warning("no symbols found in %s, maybe install "
				   "a debug package?\n", name);
		}
#endif
		return -1;
	}

	return 0;
}

int __weak arch__compare_symbol_names(const char *namea, const char *nameb)
{
	return strcmp(namea, nameb);
}

struct symbol *map__find_symbol(struct map *map, u64 addr,
				symbol_filter_t filter)
{
	if (map__load(map, filter) < 0)
		return NULL;

	return dso__find_symbol(map->dso, map->type, addr);
}

struct symbol *map__find_symbol_by_name(struct map *map, const char *name,
					symbol_filter_t filter)
{
	if (map__load(map, filter) < 0)
		return NULL;

	if (!dso__sorted_by_name(map->dso, map->type))
		dso__sort_by_name(map->dso, map->type);

	return dso__find_symbol_by_name(map->dso, map->type, name);
}

struct map *map__clone(struct map *from)
{
	struct map *map = memdup(from, sizeof(*map));

	if (map != NULL) {
		atomic_set(&map->refcnt, 1);
		RB_CLEAR_NODE(&map->rb_node);
		dso__get(map->dso);
		map->groups = NULL;
	}

	return map;
}

int map__overlap(struct map *l, struct map *r)
{
	if (l->start > r->start) {
		struct map *t = l;
		l = r;
		r = t;
	}

	if (l->end > r->start)
		return 1;

	return 0;
}

size_t map__fprintf(struct map *map, FILE *fp)
{
	return fprintf(fp, " %" PRIx64 "-%" PRIx64 " %" PRIx64 " %s\n",
		       map->start, map->end, map->pgoff, map->dso->name);
}

size_t map__fprintf_dsoname(struct map *map, FILE *fp)
{
	const char *dsoname = "[unknown]";

	if (map && map->dso && (map->dso->name || map->dso->long_name)) {
		if (symbol_conf.show_kernel_path && map->dso->long_name)
			dsoname = map->dso->long_name;
		else if (map->dso->name)
			dsoname = map->dso->name;
	}

	return fprintf(fp, "%s", dsoname);
}

int map__fprintf_srcline(struct map *map, u64 addr, const char *prefix,
			 FILE *fp)
{
	char *srcline;
	int ret = 0;

	if (map && map->dso) {
		srcline = get_srcline(map->dso,
				      map__rip_2objdump(map, addr), NULL, true);
		if (srcline != SRCLINE_UNKNOWN)
			ret = fprintf(fp, "%s%s", prefix, srcline);
		free_srcline(srcline);
	}
	return ret;
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
	if (!map->dso->adjust_symbols)
		return rip;

	if (map->dso->rel)
		return rip - map->pgoff;

	/*
	 * kernel modules also have DSO_TYPE_USER in dso->kernel,
	 * but all kernel modules are ET_REL, so won't get here.
	 */
	if (map->dso->kernel == DSO_TYPE_USER)
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
	if (map->dso->kernel == DSO_TYPE_USER)
		return map->unmap_ip(map, ip - map->dso->text_offset);

	return ip + map->reloc;
}

static void maps__init(struct maps *maps)
{
	maps->entries = RB_ROOT;
	pthread_rwlock_init(&maps->lock, NULL);
}

void map_groups__init(struct map_groups *mg, struct machine *machine)
{
	int i;
	for (i = 0; i < MAP__NR_TYPES; ++i) {
		maps__init(&mg->maps[i]);
	}
	mg->machine = machine;
	atomic_set(&mg->refcnt, 1);
}

static void __maps__purge(struct maps *maps)
{
	struct rb_root *root = &maps->entries;
	struct rb_node *next = rb_first(root);

	while (next) {
		struct map *pos = rb_entry(next, struct map, rb_node);

		next = rb_next(&pos->rb_node);
		rb_erase_init(&pos->rb_node, root);
		map__put(pos);
	}
}

static void maps__exit(struct maps *maps)
{
	pthread_rwlock_wrlock(&maps->lock);
	__maps__purge(maps);
	pthread_rwlock_unlock(&maps->lock);
}

void map_groups__exit(struct map_groups *mg)
{
	int i;

	for (i = 0; i < MAP__NR_TYPES; ++i)
		maps__exit(&mg->maps[i]);
}

bool map_groups__empty(struct map_groups *mg)
{
	int i;

	for (i = 0; i < MAP__NR_TYPES; ++i) {
		if (maps__first(&mg->maps[i]))
			return false;
	}

	return true;
}

struct map_groups *map_groups__new(struct machine *machine)
{
	struct map_groups *mg = malloc(sizeof(*mg));

	if (mg != NULL)
		map_groups__init(mg, machine);

	return mg;
}

void map_groups__delete(struct map_groups *mg)
{
	map_groups__exit(mg);
	free(mg);
}

void map_groups__put(struct map_groups *mg)
{
	if (mg && atomic_dec_and_test(&mg->refcnt))
		map_groups__delete(mg);
}

struct symbol *map_groups__find_symbol(struct map_groups *mg,
				       enum map_type type, u64 addr,
				       struct map **mapp,
				       symbol_filter_t filter)
{
	struct map *map = map_groups__find(mg, type, addr);

	/* Ensure map is loaded before using map->map_ip */
	if (map != NULL && map__load(map, filter) >= 0) {
		if (mapp != NULL)
			*mapp = map;
		return map__find_symbol(map, map->map_ip(map, addr), filter);
	}

	return NULL;
}

struct symbol *maps__find_symbol_by_name(struct maps *maps, const char *name,
					 struct map **mapp, symbol_filter_t filter)
{
	struct symbol *sym;
	struct rb_node *nd;

	pthread_rwlock_rdlock(&maps->lock);

	for (nd = rb_first(&maps->entries); nd; nd = rb_next(nd)) {
		struct map *pos = rb_entry(nd, struct map, rb_node);

		sym = map__find_symbol_by_name(pos, name, filter);

		if (sym == NULL)
			continue;
		if (mapp != NULL)
			*mapp = pos;
		goto out;
	}

	sym = NULL;
out:
	pthread_rwlock_unlock(&maps->lock);
	return sym;
}

struct symbol *map_groups__find_symbol_by_name(struct map_groups *mg,
					       enum map_type type,
					       const char *name,
					       struct map **mapp,
					       symbol_filter_t filter)
{
	struct symbol *sym = maps__find_symbol_by_name(&mg->maps[type], name, mapp, filter);

	return sym;
}

int map_groups__find_ams(struct addr_map_symbol *ams, symbol_filter_t filter)
{
	if (ams->addr < ams->map->start || ams->addr >= ams->map->end) {
		if (ams->map->groups == NULL)
			return -1;
		ams->map = map_groups__find(ams->map->groups, ams->map->type,
					    ams->addr);
		if (ams->map == NULL)
			return -1;
	}

	ams->al_addr = ams->map->map_ip(ams->map, ams->addr);
	ams->sym = map__find_symbol(ams->map, ams->al_addr, filter);

	return ams->sym ? 0 : -1;
}

static size_t maps__fprintf(struct maps *maps, FILE *fp)
{
	size_t printed = 0;
	struct rb_node *nd;

	pthread_rwlock_rdlock(&maps->lock);

	for (nd = rb_first(&maps->entries); nd; nd = rb_next(nd)) {
		struct map *pos = rb_entry(nd, struct map, rb_node);
		printed += fprintf(fp, "Map:");
		printed += map__fprintf(pos, fp);
		if (verbose > 2) {
			printed += dso__fprintf(pos->dso, pos->type, fp);
			printed += fprintf(fp, "--\n");
		}
	}

	pthread_rwlock_unlock(&maps->lock);

	return printed;
}

size_t __map_groups__fprintf_maps(struct map_groups *mg, enum map_type type,
				  FILE *fp)
{
	size_t printed = fprintf(fp, "%s:\n", map_type__name[type]);
	return printed += maps__fprintf(&mg->maps[type], fp);
}

size_t map_groups__fprintf(struct map_groups *mg, FILE *fp)
{
	size_t printed = 0, i;
	for (i = 0; i < MAP__NR_TYPES; ++i)
		printed += __map_groups__fprintf_maps(mg, i, fp);
	return printed;
}

static void __map_groups__insert(struct map_groups *mg, struct map *map)
{
	__maps__insert(&mg->maps[map->type], map);
	map->groups = mg;
}

static int maps__fixup_overlappings(struct maps *maps, struct map *map, FILE *fp)
{
	struct rb_root *root;
	struct rb_node *next;
	int err = 0;

	pthread_rwlock_wrlock(&maps->lock);

	root = &maps->entries;
	next = rb_first(root);

	while (next) {
		struct map *pos = rb_entry(next, struct map, rb_node);
		next = rb_next(&pos->rb_node);

		if (!map__overlap(pos, map))
			continue;

		if (verbose >= 2) {
			fputs("overlapping maps:\n", fp);
			map__fprintf(map, fp);
			map__fprintf(pos, fp);
		}

		rb_erase_init(&pos->rb_node, root);
		/*
		 * Now check if we need to create new maps for areas not
		 * overlapped by the new map:
		 */
		if (map->start > pos->start) {
			struct map *before = map__clone(pos);

			if (before == NULL) {
				err = -ENOMEM;
				goto put_map;
			}

			before->end = map->start;
			__map_groups__insert(pos->groups, before);
			if (verbose >= 2)
				map__fprintf(before, fp);
			map__put(before);
		}

		if (map->end < pos->end) {
			struct map *after = map__clone(pos);

			if (after == NULL) {
				err = -ENOMEM;
				goto put_map;
			}

			after->start = map->end;
			__map_groups__insert(pos->groups, after);
			if (verbose >= 2)
				map__fprintf(after, fp);
			map__put(after);
		}
put_map:
		map__put(pos);

		if (err)
			goto out;
	}

	err = 0;
out:
	pthread_rwlock_unlock(&maps->lock);
	return err;
}

int map_groups__fixup_overlappings(struct map_groups *mg, struct map *map,
				   FILE *fp)
{
	return maps__fixup_overlappings(&mg->maps[map->type], map, fp);
}

/*
 * XXX This should not really _copy_ te maps, but refcount them.
 */
int map_groups__clone(struct thread *thread,
		      struct map_groups *parent, enum map_type type)
{
	struct map_groups *mg = thread->mg;
	int err = -ENOMEM;
	struct map *map;
	struct maps *maps = &parent->maps[type];

	pthread_rwlock_rdlock(&maps->lock);

	for (map = maps__first(maps); map; map = map__next(map)) {
		struct map *new = map__clone(map);
		if (new == NULL)
			goto out_unlock;

		err = unwind__prepare_access(thread, new, NULL);
		if (err)
			goto out_unlock;

		map_groups__insert(mg, new);
		map__put(new);
	}

	err = 0;
out_unlock:
	pthread_rwlock_unlock(&maps->lock);
	return err;
}

static void __maps__insert(struct maps *maps, struct map *map)
{
	struct rb_node **p = &maps->entries.rb_node;
	struct rb_node *parent = NULL;
	const u64 ip = map->start;
	struct map *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map, rb_node);
		if (ip < m->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&map->rb_node, parent, p);
	rb_insert_color(&map->rb_node, &maps->entries);
	map__get(map);
}

void maps__insert(struct maps *maps, struct map *map)
{
	pthread_rwlock_wrlock(&maps->lock);
	__maps__insert(maps, map);
	pthread_rwlock_unlock(&maps->lock);
}

static void __maps__remove(struct maps *maps, struct map *map)
{
	rb_erase_init(&map->rb_node, &maps->entries);
	map__put(map);
}

void maps__remove(struct maps *maps, struct map *map)
{
	pthread_rwlock_wrlock(&maps->lock);
	__maps__remove(maps, map);
	pthread_rwlock_unlock(&maps->lock);
}

struct map *maps__find(struct maps *maps, u64 ip)
{
	struct rb_node **p, *parent = NULL;
	struct map *m;

	pthread_rwlock_rdlock(&maps->lock);

	p = &maps->entries.rb_node;
	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map, rb_node);
		if (ip < m->start)
			p = &(*p)->rb_left;
		else if (ip >= m->end)
			p = &(*p)->rb_right;
		else
			goto out;
	}

	m = NULL;
out:
	pthread_rwlock_unlock(&maps->lock);
	return m;
}

struct map *maps__first(struct maps *maps)
{
	struct rb_node *first = rb_first(&maps->entries);

	if (first)
		return rb_entry(first, struct map, rb_node);
	return NULL;
}

struct map *map__next(struct map *map)
{
	struct rb_node *next = rb_next(&map->rb_node);

	if (next)
		return rb_entry(next, struct map, rb_node);
	return NULL;
}

struct kmap *map__kmap(struct map *map)
{
	if (!map->dso || !map->dso->kernel) {
		pr_err("Internal error: map__kmap with a non-kernel map\n");
		return NULL;
	}
	return (struct kmap *)(map + 1);
}

struct map_groups *map__kmaps(struct map *map)
{
	struct kmap *kmap = map__kmap(map);

	if (!kmap || !kmap->kmaps) {
		pr_err("Internal error: map__kmaps with a non-kernel map\n");
		return NULL;
	}
	return kmap->kmaps;
}
