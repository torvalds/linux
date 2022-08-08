// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * resolve_btfids scans Elf object for .BTF_ids section and resolves
 * its symbols with BTF ID values.
 *
 * Each symbol points to 4 bytes data and is expected to have
 * following name syntax:
 *
 * __BTF_ID__<type>__<symbol>[__<id>]
 *
 * type is:
 *
 *   func    - lookup BTF_KIND_FUNC symbol with <symbol> name
 *             and store its ID into the data:
 *
 *             __BTF_ID__func__vfs_close__1:
 *             .zero 4
 *
 *   struct  - lookup BTF_KIND_STRUCT symbol with <symbol> name
 *             and store its ID into the data:
 *
 *             __BTF_ID__struct__sk_buff__1:
 *             .zero 4
 *
 *   union   - lookup BTF_KIND_UNION symbol with <symbol> name
 *             and store its ID into the data:
 *
 *             __BTF_ID__union__thread_union__1:
 *             .zero 4
 *
 *   typedef - lookup BTF_KIND_TYPEDEF symbol with <symbol> name
 *             and store its ID into the data:
 *
 *             __BTF_ID__typedef__pid_t__1:
 *             .zero 4
 *
 *   set     - store symbol size into first 4 bytes and sort following
 *             ID list
 *
 *             __BTF_ID__set__list:
 *             .zero 4
 *             list:
 *             __BTF_ID__func__vfs_getattr__3:
 *             .zero 4
 *             __BTF_ID__func__vfs_fallocate__4:
 *             .zero 4
 */

#define  _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libelf.h>
#include <gelf.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/rbtree.h>
#include <linux/zalloc.h>
#include <linux/err.h>
#include <btf.h>
#include <libbpf.h>
#include <parse-options.h>

#define BTF_IDS_SECTION	".BTF_ids"
#define BTF_ID		"__BTF_ID__"

#define BTF_STRUCT	"struct"
#define BTF_UNION	"union"
#define BTF_TYPEDEF	"typedef"
#define BTF_FUNC	"func"
#define BTF_SET		"set"

#define ADDR_CNT	100

struct btf_id {
	struct rb_node	 rb_node;
	char		*name;
	union {
		int	 id;
		int	 cnt;
	};
	int		 addr_cnt;
	Elf64_Addr	 addr[ADDR_CNT];
};

struct object {
	const char *path;
	const char *btf;

	struct {
		int		 fd;
		Elf		*elf;
		Elf_Data	*symbols;
		Elf_Data	*idlist;
		int		 symbols_shndx;
		int		 idlist_shndx;
		size_t		 strtabidx;
		unsigned long	 idlist_addr;
	} efile;

	struct rb_root	sets;
	struct rb_root	structs;
	struct rb_root	unions;
	struct rb_root	typedefs;
	struct rb_root	funcs;

	int nr_funcs;
	int nr_structs;
	int nr_unions;
	int nr_typedefs;
};

static int verbose;

int eprintf(int level, int var, const char *fmt, ...)
{
	va_list args;
	int ret;

	if (var >= level) {
		va_start(args, fmt);
		ret = vfprintf(stderr, fmt, args);
		va_end(args);
	}
	return ret;
}

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_debug(fmt, ...) \
	eprintf(1, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debugN(n, fmt, ...) \
	eprintf(n, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug2(fmt, ...) pr_debugN(2, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)

static bool is_btf_id(const char *name)
{
	return name && !strncmp(name, BTF_ID, sizeof(BTF_ID) - 1);
}

static struct btf_id *btf_id__find(struct rb_root *root, const char *name)
{
	struct rb_node *p = root->rb_node;
	struct btf_id *id;
	int cmp;

	while (p) {
		id = rb_entry(p, struct btf_id, rb_node);
		cmp = strcmp(id->name, name);
		if (cmp < 0)
			p = p->rb_left;
		else if (cmp > 0)
			p = p->rb_right;
		else
			return id;
	}
	return NULL;
}

static struct btf_id*
btf_id__add(struct rb_root *root, char *name, bool unique)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct btf_id *id;
	int cmp;

	while (*p != NULL) {
		parent = *p;
		id = rb_entry(parent, struct btf_id, rb_node);
		cmp = strcmp(id->name, name);
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else
			return unique ? NULL : id;
	}

	id = zalloc(sizeof(*id));
	if (id) {
		pr_debug("adding symbol %s\n", name);
		id->name = name;
		rb_link_node(&id->rb_node, parent, p);
		rb_insert_color(&id->rb_node, root);
	}
	return id;
}

static char *get_id(const char *prefix_end)
{
	/*
	 * __BTF_ID__func__vfs_truncate__0
	 * prefix_end =  ^
	 * pos        =    ^
	 */
	int len = strlen(prefix_end);
	int pos = sizeof("__") - 1;
	char *p, *id;

	if (pos >= len)
		return NULL;

	id = strdup(prefix_end + pos);
	if (id) {
		/*
		 * __BTF_ID__func__vfs_truncate__0
		 * id =            ^
		 *
		 * cut the unique id part
		 */
		p = strrchr(id, '_');
		p--;
		if (*p != '_') {
			free(id);
			return NULL;
		}
		*p = '\0';
	}
	return id;
}

static struct btf_id *add_set(struct object *obj, char *name)
{
	/*
	 * __BTF_ID__set__name
	 * name =    ^
	 * id   =         ^
	 */
	char *id = name + sizeof(BTF_SET "__") - 1;
	int len = strlen(name);

	if (id >= name + len) {
		pr_err("FAILED to parse set name: %s\n", name);
		return NULL;
	}

	return btf_id__add(&obj->sets, id, true);
}

static struct btf_id *add_symbol(struct rb_root *root, char *name, size_t size)
{
	char *id;

	id = get_id(name + size);
	if (!id) {
		pr_err("FAILED to parse symbol name: %s\n", name);
		return NULL;
	}

	return btf_id__add(root, id, false);
}

/* Older libelf.h and glibc elf.h might not yet define the ELF compression types. */
#ifndef SHF_COMPRESSED
#define SHF_COMPRESSED (1 << 11) /* Section with compressed data. */
#endif

/*
 * The data of compressed section should be aligned to 4
 * (for 32bit) or 8 (for 64 bit) bytes. The binutils ld
 * sets sh_addralign to 1, which makes libelf fail with
 * misaligned section error during the update:
 *    FAILED elf_update(WRITE): invalid section alignment
 *
 * While waiting for ld fix, we fix the compressed sections
 * sh_addralign value manualy.
 */
static int compressed_section_fix(Elf *elf, Elf_Scn *scn, GElf_Shdr *sh)
{
	int expected = gelf_getclass(elf) == ELFCLASS32 ? 4 : 8;

	if (!(sh->sh_flags & SHF_COMPRESSED))
		return 0;

	if (sh->sh_addralign == expected)
		return 0;

	pr_debug2(" - fixing wrong alignment sh_addralign %u, expected %u\n",
		  sh->sh_addralign, expected);

	sh->sh_addralign = expected;

	if (gelf_update_shdr(scn, sh) == 0) {
		printf("FAILED cannot update section header: %s\n",
			elf_errmsg(-1));
		return -1;
	}
	return 0;
}

static int elf_collect(struct object *obj)
{
	Elf_Scn *scn = NULL;
	size_t shdrstrndx;
	int idx = 0;
	Elf *elf;
	int fd;

	fd = open(obj->path, O_RDWR, 0666);
	if (fd == -1) {
		pr_err("FAILED cannot open %s: %s\n",
			obj->path, strerror(errno));
		return -1;
	}

	elf_version(EV_CURRENT);

	elf = elf_begin(fd, ELF_C_RDWR_MMAP, NULL);
	if (!elf) {
		pr_err("FAILED cannot create ELF descriptor: %s\n",
			elf_errmsg(-1));
		return -1;
	}

	obj->efile.fd  = fd;
	obj->efile.elf = elf;

	elf_flagelf(elf, ELF_C_SET, ELF_F_LAYOUT);

	if (elf_getshdrstrndx(elf, &shdrstrndx) != 0) {
		pr_err("FAILED cannot get shdr str ndx\n");
		return -1;
	}

	/*
	 * Scan all the elf sections and look for save data
	 * from .BTF_ids section and symbols.
	 */
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		Elf_Data *data;
		GElf_Shdr sh;
		char *name;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_err("FAILED get section(%d) header\n", idx);
			return -1;
		}

		name = elf_strptr(elf, shdrstrndx, sh.sh_name);
		if (!name) {
			pr_err("FAILED get section(%d) name\n", idx);
			return -1;
		}

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_err("FAILED to get section(%d) data from %s\n",
				idx, name);
			return -1;
		}

		pr_debug2("section(%d) %s, size %ld, link %d, flags %lx, type=%d\n",
			  idx, name, (unsigned long) data->d_size,
			  (int) sh.sh_link, (unsigned long) sh.sh_flags,
			  (int) sh.sh_type);

		if (sh.sh_type == SHT_SYMTAB) {
			obj->efile.symbols       = data;
			obj->efile.symbols_shndx = idx;
			obj->efile.strtabidx     = sh.sh_link;
		} else if (!strcmp(name, BTF_IDS_SECTION)) {
			obj->efile.idlist       = data;
			obj->efile.idlist_shndx = idx;
			obj->efile.idlist_addr  = sh.sh_addr;
		}

		if (compressed_section_fix(elf, scn, &sh))
			return -1;
	}

	return 0;
}

static int symbols_collect(struct object *obj)
{
	Elf_Scn *scn = NULL;
	int n, i, err = 0;
	GElf_Shdr sh;
	char *name;

	scn = elf_getscn(obj->efile.elf, obj->efile.symbols_shndx);
	if (!scn)
		return -1;

	if (gelf_getshdr(scn, &sh) != &sh)
		return -1;

	n = sh.sh_size / sh.sh_entsize;

	/*
	 * Scan symbols and look for the ones starting with
	 * __BTF_ID__* over .BTF_ids section.
	 */
	for (i = 0; !err && i < n; i++) {
		char *tmp, *prefix;
		struct btf_id *id;
		GElf_Sym sym;
		int err = -1;

		if (!gelf_getsym(obj->efile.symbols, i, &sym))
			return -1;

		if (sym.st_shndx != obj->efile.idlist_shndx)
			continue;

		name = elf_strptr(obj->efile.elf, obj->efile.strtabidx,
				  sym.st_name);

		if (!is_btf_id(name))
			continue;

		/*
		 * __BTF_ID__TYPE__vfs_truncate__0
		 * prefix =  ^
		 */
		prefix = name + sizeof(BTF_ID) - 1;

		/* struct */
		if (!strncmp(prefix, BTF_STRUCT, sizeof(BTF_STRUCT) - 1)) {
			obj->nr_structs++;
			id = add_symbol(&obj->structs, prefix, sizeof(BTF_STRUCT) - 1);
		/* union  */
		} else if (!strncmp(prefix, BTF_UNION, sizeof(BTF_UNION) - 1)) {
			obj->nr_unions++;
			id = add_symbol(&obj->unions, prefix, sizeof(BTF_UNION) - 1);
		/* typedef */
		} else if (!strncmp(prefix, BTF_TYPEDEF, sizeof(BTF_TYPEDEF) - 1)) {
			obj->nr_typedefs++;
			id = add_symbol(&obj->typedefs, prefix, sizeof(BTF_TYPEDEF) - 1);
		/* func */
		} else if (!strncmp(prefix, BTF_FUNC, sizeof(BTF_FUNC) - 1)) {
			obj->nr_funcs++;
			id = add_symbol(&obj->funcs, prefix, sizeof(BTF_FUNC) - 1);
		/* set */
		} else if (!strncmp(prefix, BTF_SET, sizeof(BTF_SET) - 1)) {
			id = add_set(obj, prefix);
			/*
			 * SET objects store list's count, which is encoded
			 * in symbol's size, together with 'cnt' field hence
			 * that - 1.
			 */
			if (id)
				id->cnt = sym.st_size / sizeof(int) - 1;
		} else {
			pr_err("FAILED unsupported prefix %s\n", prefix);
			return -1;
		}

		if (!id)
			return -ENOMEM;

		if (id->addr_cnt >= ADDR_CNT) {
			pr_err("FAILED symbol %s crossed the number of allowed lists\n",
				id->name);
			return -1;
		}
		id->addr[id->addr_cnt++] = sym.st_value;
	}

	return 0;
}

static int symbols_resolve(struct object *obj)
{
	int nr_typedefs = obj->nr_typedefs;
	int nr_structs  = obj->nr_structs;
	int nr_unions   = obj->nr_unions;
	int nr_funcs    = obj->nr_funcs;
	int err, type_id;
	struct btf *btf;
	__u32 nr_types;

	btf = btf__parse(obj->btf ?: obj->path, NULL);
	err = libbpf_get_error(btf);
	if (err) {
		pr_err("FAILED: load BTF from %s: %s\n",
			obj->path, strerror(-err));
		return -1;
	}

	err = -1;
	nr_types = btf__get_nr_types(btf);

	/*
	 * Iterate all the BTF types and search for collected symbol IDs.
	 */
	for (type_id = 1; type_id <= nr_types; type_id++) {
		const struct btf_type *type;
		struct rb_root *root;
		struct btf_id *id;
		const char *str;
		int *nr;

		type = btf__type_by_id(btf, type_id);
		if (!type) {
			pr_err("FAILED: malformed BTF, can't resolve type for ID %d\n",
				type_id);
			goto out;
		}

		if (btf_is_func(type) && nr_funcs) {
			nr   = &nr_funcs;
			root = &obj->funcs;
		} else if (btf_is_struct(type) && nr_structs) {
			nr   = &nr_structs;
			root = &obj->structs;
		} else if (btf_is_union(type) && nr_unions) {
			nr   = &nr_unions;
			root = &obj->unions;
		} else if (btf_is_typedef(type) && nr_typedefs) {
			nr   = &nr_typedefs;
			root = &obj->typedefs;
		} else
			continue;

		str = btf__name_by_offset(btf, type->name_off);
		if (!str) {
			pr_err("FAILED: malformed BTF, can't resolve name for ID %d\n",
				type_id);
			goto out;
		}

		id = btf_id__find(root, str);
		if (id) {
			if (id->id) {
				pr_info("WARN: multiple IDs found for '%s': %d, %d - using %d\n",
					str, id->id, type_id, id->id);
			} else {
				id->id = type_id;
				(*nr)--;
			}
		}
	}

	err = 0;
out:
	btf__free(btf);
	return err;
}

static int id_patch(struct object *obj, struct btf_id *id)
{
	Elf_Data *data = obj->efile.idlist;
	int *ptr = data->d_buf;
	int i;

	if (!id->id) {
		pr_err("FAILED unresolved symbol %s\n", id->name);
		return -EINVAL;
	}

	for (i = 0; i < id->addr_cnt; i++) {
		unsigned long addr = id->addr[i];
		unsigned long idx = addr - obj->efile.idlist_addr;

		pr_debug("patching addr %5lu: ID %7d [%s]\n",
			 idx, id->id, id->name);

		if (idx >= data->d_size) {
			pr_err("FAILED patching index %lu out of bounds %lu\n",
				idx, data->d_size);
			return -1;
		}

		idx = idx / sizeof(int);
		ptr[idx] = id->id;
	}

	return 0;
}

static int __symbols_patch(struct object *obj, struct rb_root *root)
{
	struct rb_node *next;
	struct btf_id *id;

	next = rb_first(root);
	while (next) {
		id = rb_entry(next, struct btf_id, rb_node);

		if (id_patch(obj, id))
			return -1;

		next = rb_next(next);
	}
	return 0;
}

static int cmp_id(const void *pa, const void *pb)
{
	const int *a = pa, *b = pb;

	return *a - *b;
}

static int sets_patch(struct object *obj)
{
	Elf_Data *data = obj->efile.idlist;
	int *ptr = data->d_buf;
	struct rb_node *next;

	next = rb_first(&obj->sets);
	while (next) {
		unsigned long addr, idx;
		struct btf_id *id;
		int *base;
		int cnt;

		id   = rb_entry(next, struct btf_id, rb_node);
		addr = id->addr[0];
		idx  = addr - obj->efile.idlist_addr;

		/* sets are unique */
		if (id->addr_cnt != 1) {
			pr_err("FAILED malformed data for set '%s'\n",
				id->name);
			return -1;
		}

		idx = idx / sizeof(int);
		base = &ptr[idx] + 1;
		cnt = ptr[idx];

		pr_debug("sorting  addr %5lu: cnt %6d [%s]\n",
			 (idx + 1) * sizeof(int), cnt, id->name);

		qsort(base, cnt, sizeof(int), cmp_id);

		next = rb_next(next);
	}
	return 0;
}

static int symbols_patch(struct object *obj)
{
	int err;

	if (__symbols_patch(obj, &obj->structs)  ||
	    __symbols_patch(obj, &obj->unions)   ||
	    __symbols_patch(obj, &obj->typedefs) ||
	    __symbols_patch(obj, &obj->funcs)    ||
	    __symbols_patch(obj, &obj->sets))
		return -1;

	if (sets_patch(obj))
		return -1;

	elf_flagdata(obj->efile.idlist, ELF_C_SET, ELF_F_DIRTY);

	err = elf_update(obj->efile.elf, ELF_C_WRITE);
	if (err < 0) {
		pr_err("FAILED elf_update(WRITE): %s\n",
			elf_errmsg(-1));
	}

	pr_debug("update %s for %s\n",
		 err >= 0 ? "ok" : "failed", obj->path);
	return err < 0 ? -1 : 0;
}

static const char * const resolve_btfids_usage[] = {
	"resolve_btfids [<options>] <ELF object>",
	NULL
};

int main(int argc, const char **argv)
{
	bool no_fail = false;
	struct object obj = {
		.efile = {
			.idlist_shndx  = -1,
			.symbols_shndx = -1,
		},
		.structs  = RB_ROOT,
		.unions   = RB_ROOT,
		.typedefs = RB_ROOT,
		.funcs    = RB_ROOT,
		.sets     = RB_ROOT,
	};
	struct option btfid_options[] = {
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show errors, etc)"),
		OPT_STRING(0, "btf", &obj.btf, "BTF data",
			   "BTF data"),
		OPT_BOOLEAN(0, "no-fail", &no_fail,
			   "do not fail if " BTF_IDS_SECTION " section is not found"),
		OPT_END()
	};
	int err = -1;

	argc = parse_options(argc, argv, btfid_options, resolve_btfids_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc != 1)
		usage_with_options(resolve_btfids_usage, btfid_options);

	obj.path = argv[0];

	if (elf_collect(&obj))
		goto out;

	/*
	 * We did not find .BTF_ids section or symbols section,
	 * nothing to do..
	 */
	if (obj.efile.idlist_shndx == -1 ||
	    obj.efile.symbols_shndx == -1) {
		if (no_fail)
			return 0;
		pr_err("FAILED to find needed sections\n");
		return -1;
	}

	if (symbols_collect(&obj))
		goto out;

	if (symbols_resolve(&obj))
		goto out;

	if (symbols_patch(&obj))
		goto out;

	err = 0;
out:
	if (obj.efile.elf)
		elf_end(obj.efile.elf);
	close(obj.efile.fd);
	return err;
}
