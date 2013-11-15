#include "symbol.h"
#include "dso.h"
#include "machine.h"
#include "util.h"
#include "debug.h"

char dso__symtab_origin(const struct dso *dso)
{
	static const char origin[] = {
		[DSO_BINARY_TYPE__KALLSYMS]		= 'k',
		[DSO_BINARY_TYPE__VMLINUX]		= 'v',
		[DSO_BINARY_TYPE__JAVA_JIT]		= 'j',
		[DSO_BINARY_TYPE__DEBUGLINK]		= 'l',
		[DSO_BINARY_TYPE__BUILD_ID_CACHE]	= 'B',
		[DSO_BINARY_TYPE__FEDORA_DEBUGINFO]	= 'f',
		[DSO_BINARY_TYPE__UBUNTU_DEBUGINFO]	= 'u',
		[DSO_BINARY_TYPE__BUILDID_DEBUGINFO]	= 'b',
		[DSO_BINARY_TYPE__SYSTEM_PATH_DSO]	= 'd',
		[DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE]	= 'K',
		[DSO_BINARY_TYPE__GUEST_KALLSYMS]	= 'g',
		[DSO_BINARY_TYPE__GUEST_KMODULE]	= 'G',
		[DSO_BINARY_TYPE__GUEST_VMLINUX]	= 'V',
	};

	if (dso == NULL || dso->symtab_type == DSO_BINARY_TYPE__NOT_FOUND)
		return '!';
	return origin[dso->symtab_type];
}

int dso__binary_type_file(struct dso *dso, enum dso_binary_type type,
			  char *root_dir, char *file, size_t size)
{
	char build_id_hex[BUILD_ID_SIZE * 2 + 1];
	int ret = 0;

	switch (type) {
	case DSO_BINARY_TYPE__DEBUGLINK: {
		char *debuglink;

		strncpy(file, dso->long_name, size);
		debuglink = file + dso->long_name_len;
		while (debuglink != file && *debuglink != '/')
			debuglink--;
		if (*debuglink == '/')
			debuglink++;
		filename__read_debuglink(dso->long_name, debuglink,
					 size - (debuglink - file));
		}
		break;
	case DSO_BINARY_TYPE__BUILD_ID_CACHE:
		/* skip the locally configured cache if a symfs is given */
		if (symbol_conf.symfs[0] ||
		    (dso__build_id_filename(dso, file, size) == NULL))
			ret = -1;
		break;

	case DSO_BINARY_TYPE__FEDORA_DEBUGINFO:
		snprintf(file, size, "%s/usr/lib/debug%s.debug",
			 symbol_conf.symfs, dso->long_name);
		break;

	case DSO_BINARY_TYPE__UBUNTU_DEBUGINFO:
		snprintf(file, size, "%s/usr/lib/debug%s",
			 symbol_conf.symfs, dso->long_name);
		break;

	case DSO_BINARY_TYPE__BUILDID_DEBUGINFO:
		if (!dso->has_build_id) {
			ret = -1;
			break;
		}

		build_id__sprintf(dso->build_id,
				  sizeof(dso->build_id),
				  build_id_hex);
		snprintf(file, size,
			 "%s/usr/lib/debug/.build-id/%.2s/%s.debug",
			 symbol_conf.symfs, build_id_hex, build_id_hex + 2);
		break;

	case DSO_BINARY_TYPE__VMLINUX:
	case DSO_BINARY_TYPE__GUEST_VMLINUX:
	case DSO_BINARY_TYPE__SYSTEM_PATH_DSO:
		snprintf(file, size, "%s%s",
			 symbol_conf.symfs, dso->long_name);
		break;

	case DSO_BINARY_TYPE__GUEST_KMODULE:
		snprintf(file, size, "%s%s%s", symbol_conf.symfs,
			 root_dir, dso->long_name);
		break;

	case DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE:
		snprintf(file, size, "%s%s", symbol_conf.symfs,
			 dso->long_name);
		break;

	case DSO_BINARY_TYPE__KCORE:
	case DSO_BINARY_TYPE__GUEST_KCORE:
		snprintf(file, size, "%s", dso->long_name);
		break;

	default:
	case DSO_BINARY_TYPE__KALLSYMS:
	case DSO_BINARY_TYPE__GUEST_KALLSYMS:
	case DSO_BINARY_TYPE__JAVA_JIT:
	case DSO_BINARY_TYPE__NOT_FOUND:
		ret = -1;
		break;
	}

	return ret;
}

static int open_dso(struct dso *dso, struct machine *machine)
{
	char *root_dir = (char *) "";
	char *name;
	int fd;

	name = malloc(PATH_MAX);
	if (!name)
		return -ENOMEM;

	if (machine)
		root_dir = machine->root_dir;

	if (dso__binary_type_file(dso, dso->data_type,
				  root_dir, name, PATH_MAX)) {
		free(name);
		return -EINVAL;
	}

	fd = open(name, O_RDONLY);
	free(name);
	return fd;
}

int dso__data_fd(struct dso *dso, struct machine *machine)
{
	static enum dso_binary_type binary_type_data[] = {
		DSO_BINARY_TYPE__BUILD_ID_CACHE,
		DSO_BINARY_TYPE__SYSTEM_PATH_DSO,
		DSO_BINARY_TYPE__NOT_FOUND,
	};
	int i = 0;

	if (dso->data_type != DSO_BINARY_TYPE__NOT_FOUND)
		return open_dso(dso, machine);

	do {
		int fd;

		dso->data_type = binary_type_data[i++];

		fd = open_dso(dso, machine);
		if (fd >= 0)
			return fd;

	} while (dso->data_type != DSO_BINARY_TYPE__NOT_FOUND);

	return -EINVAL;
}

static void
dso_cache__free(struct rb_root *root)
{
	struct rb_node *next = rb_first(root);

	while (next) {
		struct dso_cache *cache;

		cache = rb_entry(next, struct dso_cache, rb_node);
		next = rb_next(&cache->rb_node);
		rb_erase(&cache->rb_node, root);
		free(cache);
	}
}

static struct dso_cache*
dso_cache__find(struct rb_root *root, u64 offset)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct dso_cache *cache;

	while (*p != NULL) {
		u64 end;

		parent = *p;
		cache = rb_entry(parent, struct dso_cache, rb_node);
		end = cache->offset + DSO__DATA_CACHE_SIZE;

		if (offset < cache->offset)
			p = &(*p)->rb_left;
		else if (offset >= end)
			p = &(*p)->rb_right;
		else
			return cache;
	}
	return NULL;
}

static void
dso_cache__insert(struct rb_root *root, struct dso_cache *new)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct dso_cache *cache;
	u64 offset = new->offset;

	while (*p != NULL) {
		u64 end;

		parent = *p;
		cache = rb_entry(parent, struct dso_cache, rb_node);
		end = cache->offset + DSO__DATA_CACHE_SIZE;

		if (offset < cache->offset)
			p = &(*p)->rb_left;
		else if (offset >= end)
			p = &(*p)->rb_right;
	}

	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, root);
}

static ssize_t
dso_cache__memcpy(struct dso_cache *cache, u64 offset,
		  u8 *data, u64 size)
{
	u64 cache_offset = offset - cache->offset;
	u64 cache_size   = min(cache->size - cache_offset, size);

	memcpy(data, cache->data + cache_offset, cache_size);
	return cache_size;
}

static ssize_t
dso_cache__read(struct dso *dso, struct machine *machine,
		 u64 offset, u8 *data, ssize_t size)
{
	struct dso_cache *cache;
	ssize_t ret;
	int fd;

	fd = dso__data_fd(dso, machine);
	if (fd < 0)
		return -1;

	do {
		u64 cache_offset;

		ret = -ENOMEM;

		cache = zalloc(sizeof(*cache) + DSO__DATA_CACHE_SIZE);
		if (!cache)
			break;

		cache_offset = offset & DSO__DATA_CACHE_MASK;
		ret = -EINVAL;

		if (-1 == lseek(fd, cache_offset, SEEK_SET))
			break;

		ret = read(fd, cache->data, DSO__DATA_CACHE_SIZE);
		if (ret <= 0)
			break;

		cache->offset = cache_offset;
		cache->size   = ret;
		dso_cache__insert(&dso->cache, cache);

		ret = dso_cache__memcpy(cache, offset, data, size);

	} while (0);

	if (ret <= 0)
		free(cache);

	close(fd);
	return ret;
}

static ssize_t dso_cache_read(struct dso *dso, struct machine *machine,
			      u64 offset, u8 *data, ssize_t size)
{
	struct dso_cache *cache;

	cache = dso_cache__find(&dso->cache, offset);
	if (cache)
		return dso_cache__memcpy(cache, offset, data, size);
	else
		return dso_cache__read(dso, machine, offset, data, size);
}

ssize_t dso__data_read_offset(struct dso *dso, struct machine *machine,
			      u64 offset, u8 *data, ssize_t size)
{
	ssize_t r = 0;
	u8 *p = data;

	do {
		ssize_t ret;

		ret = dso_cache_read(dso, machine, offset, p, size);
		if (ret < 0)
			return ret;

		/* Reached EOF, return what we have. */
		if (!ret)
			break;

		BUG_ON(ret > size);

		r      += ret;
		p      += ret;
		offset += ret;
		size   -= ret;

	} while (size);

	return r;
}

ssize_t dso__data_read_addr(struct dso *dso, struct map *map,
			    struct machine *machine, u64 addr,
			    u8 *data, ssize_t size)
{
	u64 offset = map->map_ip(map, addr);
	return dso__data_read_offset(dso, machine, offset, data, size);
}

struct map *dso__new_map(const char *name)
{
	struct map *map = NULL;
	struct dso *dso = dso__new(name);

	if (dso)
		map = map__new2(0, dso, MAP__FUNCTION);

	return map;
}

struct dso *dso__kernel_findnew(struct machine *machine, const char *name,
		    const char *short_name, int dso_type)
{
	/*
	 * The kernel dso could be created by build_id processing.
	 */
	struct dso *dso = __dsos__findnew(&machine->kernel_dsos, name);

	/*
	 * We need to run this in all cases, since during the build_id
	 * processing we had no idea this was the kernel dso.
	 */
	if (dso != NULL) {
		dso__set_short_name(dso, short_name);
		dso->kernel = dso_type;
	}

	return dso;
}

void dso__set_long_name(struct dso *dso, char *name)
{
	if (name == NULL)
		return;
	dso->long_name = name;
	dso->long_name_len = strlen(name);
}

void dso__set_short_name(struct dso *dso, const char *name)
{
	if (name == NULL)
		return;
	dso->short_name = name;
	dso->short_name_len = strlen(name);
}

static void dso__set_basename(struct dso *dso)
{
	dso__set_short_name(dso, basename(dso->long_name));
}

int dso__name_len(const struct dso *dso)
{
	if (!dso)
		return strlen("[unknown]");
	if (verbose)
		return dso->long_name_len;

	return dso->short_name_len;
}

bool dso__loaded(const struct dso *dso, enum map_type type)
{
	return dso->loaded & (1 << type);
}

bool dso__sorted_by_name(const struct dso *dso, enum map_type type)
{
	return dso->sorted_by_name & (1 << type);
}

void dso__set_sorted_by_name(struct dso *dso, enum map_type type)
{
	dso->sorted_by_name |= (1 << type);
}

struct dso *dso__new(const char *name)
{
	struct dso *dso = calloc(1, sizeof(*dso) + strlen(name) + 1);

	if (dso != NULL) {
		int i;
		strcpy(dso->name, name);
		dso__set_long_name(dso, dso->name);
		dso__set_short_name(dso, dso->name);
		for (i = 0; i < MAP__NR_TYPES; ++i)
			dso->symbols[i] = dso->symbol_names[i] = RB_ROOT;
		dso->cache = RB_ROOT;
		dso->symtab_type = DSO_BINARY_TYPE__NOT_FOUND;
		dso->data_type   = DSO_BINARY_TYPE__NOT_FOUND;
		dso->loaded = 0;
		dso->rel = 0;
		dso->sorted_by_name = 0;
		dso->has_build_id = 0;
		dso->kernel = DSO_TYPE_USER;
		dso->needs_swap = DSO_SWAP__UNSET;
		INIT_LIST_HEAD(&dso->node);
	}

	return dso;
}

void dso__delete(struct dso *dso)
{
	int i;
	for (i = 0; i < MAP__NR_TYPES; ++i)
		symbols__delete(&dso->symbols[i]);
	if (dso->sname_alloc)
		free((char *)dso->short_name);
	if (dso->lname_alloc)
		free(dso->long_name);
	dso_cache__free(&dso->cache);
	free(dso);
}

void dso__set_build_id(struct dso *dso, void *build_id)
{
	memcpy(dso->build_id, build_id, sizeof(dso->build_id));
	dso->has_build_id = 1;
}

bool dso__build_id_equal(const struct dso *dso, u8 *build_id)
{
	return memcmp(dso->build_id, build_id, sizeof(dso->build_id)) == 0;
}

void dso__read_running_kernel_build_id(struct dso *dso, struct machine *machine)
{
	char path[PATH_MAX];

	if (machine__is_default_guest(machine))
		return;
	sprintf(path, "%s/sys/kernel/notes", machine->root_dir);
	if (sysfs__read_build_id(path, dso->build_id,
				 sizeof(dso->build_id)) == 0)
		dso->has_build_id = true;
}

int dso__kernel_module_get_build_id(struct dso *dso,
				    const char *root_dir)
{
	char filename[PATH_MAX];
	/*
	 * kernel module short names are of the form "[module]" and
	 * we need just "module" here.
	 */
	const char *name = dso->short_name + 1;

	snprintf(filename, sizeof(filename),
		 "%s/sys/module/%.*s/notes/.note.gnu.build-id",
		 root_dir, (int)strlen(name) - 1, name);

	if (sysfs__read_build_id(filename, dso->build_id,
				 sizeof(dso->build_id)) == 0)
		dso->has_build_id = true;

	return 0;
}

bool __dsos__read_build_ids(struct list_head *head, bool with_hits)
{
	bool have_build_id = false;
	struct dso *pos;

	list_for_each_entry(pos, head, node) {
		if (with_hits && !pos->hit)
			continue;
		if (pos->has_build_id) {
			have_build_id = true;
			continue;
		}
		if (filename__read_build_id(pos->long_name, pos->build_id,
					    sizeof(pos->build_id)) > 0) {
			have_build_id	  = true;
			pos->has_build_id = true;
		}
	}

	return have_build_id;
}

void dsos__add(struct list_head *head, struct dso *dso)
{
	list_add_tail(&dso->node, head);
}

struct dso *dsos__find(struct list_head *head, const char *name, bool cmp_short)
{
	struct dso *pos;

	if (cmp_short) {
		list_for_each_entry(pos, head, node)
			if (strcmp(pos->short_name, name) == 0)
				return pos;
		return NULL;
	}
	list_for_each_entry(pos, head, node)
		if (strcmp(pos->long_name, name) == 0)
			return pos;
	return NULL;
}

struct dso *__dsos__findnew(struct list_head *head, const char *name)
{
	struct dso *dso = dsos__find(head, name, false);

	if (!dso) {
		dso = dso__new(name);
		if (dso != NULL) {
			dsos__add(head, dso);
			dso__set_basename(dso);
		}
	}

	return dso;
}

size_t __dsos__fprintf_buildid(struct list_head *head, FILE *fp,
			       bool (skip)(struct dso *dso, int parm), int parm)
{
	struct dso *pos;
	size_t ret = 0;

	list_for_each_entry(pos, head, node) {
		if (skip && skip(pos, parm))
			continue;
		ret += dso__fprintf_buildid(pos, fp);
		ret += fprintf(fp, " %s\n", pos->long_name);
	}
	return ret;
}

size_t __dsos__fprintf(struct list_head *head, FILE *fp)
{
	struct dso *pos;
	size_t ret = 0;

	list_for_each_entry(pos, head, node) {
		int i;
		for (i = 0; i < MAP__NR_TYPES; ++i)
			ret += dso__fprintf(pos, i, fp);
	}

	return ret;
}

size_t dso__fprintf_buildid(struct dso *dso, FILE *fp)
{
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	build_id__sprintf(dso->build_id, sizeof(dso->build_id), sbuild_id);
	return fprintf(fp, "%s", sbuild_id);
}

size_t dso__fprintf(struct dso *dso, enum map_type type, FILE *fp)
{
	struct rb_node *nd;
	size_t ret = fprintf(fp, "dso: %s (", dso->short_name);

	if (dso->short_name != dso->long_name)
		ret += fprintf(fp, "%s, ", dso->long_name);
	ret += fprintf(fp, "%s, %sloaded, ", map_type__name[type],
		       dso__loaded(dso, type) ? "" : "NOT ");
	ret += dso__fprintf_buildid(dso, fp);
	ret += fprintf(fp, ")\n");
	for (nd = rb_first(&dso->symbols[type]); nd; nd = rb_next(nd)) {
		struct symbol *pos = rb_entry(nd, struct symbol, rb_node);
		ret += symbol__fprintf(pos, fp);
	}

	return ret;
}
