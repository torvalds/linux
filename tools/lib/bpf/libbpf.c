// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * Common eBPF ELF object loading operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 * Copyright (C) 2017 Nicira, Inc.
 * Copyright (C) 2019 Isovalent, Inc.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <libgen.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <endian.h>
#include <fcntl.h>
#include <errno.h>
#include <asm/unistd.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <linux/list.h>
#include <linux/limits.h>
#include <linux/perf_event.h>
#include <linux/ring_buffer.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <tools/libc_compat.h>
#include <libelf.h>
#include <gelf.h>

#include "libbpf.h"
#include "bpf.h"
#include "btf.h"
#include "str_error.h"
#include "libbpf_internal.h"

#ifndef EM_BPF
#define EM_BPF 247
#endif

#ifndef BPF_FS_MAGIC
#define BPF_FS_MAGIC		0xcafe4a11
#endif

/* vsprintf() in __base_pr() uses nonliteral format string. It may break
 * compilation if user enables corresponding warning. Disable it explicitly.
 */
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

#define __printf(a, b)	__attribute__((format(printf, a, b)))

static int __base_pr(enum libbpf_print_level level, const char *format,
		     va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;

	return vfprintf(stderr, format, args);
}

static libbpf_print_fn_t __libbpf_pr = __base_pr;

void libbpf_set_print(libbpf_print_fn_t fn)
{
	__libbpf_pr = fn;
}

__printf(2, 3)
void libbpf_print(enum libbpf_print_level level, const char *format, ...)
{
	va_list args;

	if (!__libbpf_pr)
		return;

	va_start(args, format);
	__libbpf_pr(level, format, args);
	va_end(args);
}

#define STRERR_BUFSIZE  128

#define CHECK_ERR(action, err, out) do {	\
	err = action;			\
	if (err)			\
		goto out;		\
} while(0)


/* Copied from tools/perf/util/util.h */
#ifndef zfree
# define zfree(ptr) ({ free(*ptr); *ptr = NULL; })
#endif

#ifndef zclose
# define zclose(fd) ({			\
	int ___err = 0;			\
	if ((fd) >= 0)			\
		___err = close((fd));	\
	fd = -1;			\
	___err; })
#endif

#ifdef HAVE_LIBELF_MMAP_SUPPORT
# define LIBBPF_ELF_C_READ_MMAP ELF_C_READ_MMAP
#else
# define LIBBPF_ELF_C_READ_MMAP ELF_C_READ
#endif

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

struct bpf_capabilities {
	/* v4.14: kernel support for program & map names. */
	__u32 name:1;
	/* v5.2: kernel support for global data sections. */
	__u32 global_data:1;
	/* BTF_KIND_FUNC and BTF_KIND_FUNC_PROTO support */
	__u32 btf_func:1;
	/* BTF_KIND_VAR and BTF_KIND_DATASEC support */
	__u32 btf_datasec:1;
};

/*
 * bpf_prog should be a better name but it has been used in
 * linux/filter.h.
 */
struct bpf_program {
	/* Index in elf obj file, for relocation use. */
	int idx;
	char *name;
	int prog_ifindex;
	char *section_name;
	/* section_name with / replaced by _; makes recursive pinning
	 * in bpf_object__pin_programs easier
	 */
	char *pin_name;
	struct bpf_insn *insns;
	size_t insns_cnt, main_prog_cnt;
	enum bpf_prog_type type;

	struct reloc_desc {
		enum {
			RELO_LD64,
			RELO_CALL,
			RELO_DATA,
		} type;
		int insn_idx;
		union {
			int map_idx;
			int text_off;
		};
	} *reloc_desc;
	int nr_reloc;
	int log_level;

	struct {
		int nr;
		int *fds;
	} instances;
	bpf_program_prep_t preprocessor;

	struct bpf_object *obj;
	void *priv;
	bpf_program_clear_priv_t clear_priv;

	enum bpf_attach_type expected_attach_type;
	void *func_info;
	__u32 func_info_rec_size;
	__u32 func_info_cnt;

	struct bpf_capabilities *caps;

	void *line_info;
	__u32 line_info_rec_size;
	__u32 line_info_cnt;
	__u32 prog_flags;
};

enum libbpf_map_type {
	LIBBPF_MAP_UNSPEC,
	LIBBPF_MAP_DATA,
	LIBBPF_MAP_BSS,
	LIBBPF_MAP_RODATA,
};

static const char * const libbpf_type_to_btf_name[] = {
	[LIBBPF_MAP_DATA]	= ".data",
	[LIBBPF_MAP_BSS]	= ".bss",
	[LIBBPF_MAP_RODATA]	= ".rodata",
};

struct bpf_map {
	int fd;
	char *name;
	int sec_idx;
	size_t sec_offset;
	int map_ifindex;
	int inner_map_fd;
	struct bpf_map_def def;
	__u32 btf_key_type_id;
	__u32 btf_value_type_id;
	void *priv;
	bpf_map_clear_priv_t clear_priv;
	enum libbpf_map_type libbpf_type;
};

struct bpf_secdata {
	void *rodata;
	void *data;
};

static LIST_HEAD(bpf_objects_list);

struct bpf_object {
	char name[BPF_OBJ_NAME_LEN];
	char license[64];
	__u32 kern_version;

	struct bpf_program *programs;
	size_t nr_programs;
	struct bpf_map *maps;
	size_t nr_maps;
	size_t maps_cap;
	struct bpf_secdata sections;

	bool loaded;
	bool has_pseudo_calls;

	/*
	 * Information when doing elf related work. Only valid if fd
	 * is valid.
	 */
	struct {
		int fd;
		void *obj_buf;
		size_t obj_buf_sz;
		Elf *elf;
		GElf_Ehdr ehdr;
		Elf_Data *symbols;
		Elf_Data *data;
		Elf_Data *rodata;
		Elf_Data *bss;
		size_t strtabidx;
		struct {
			GElf_Shdr shdr;
			Elf_Data *data;
		} *reloc;
		int nr_reloc;
		int maps_shndx;
		int btf_maps_shndx;
		int text_shndx;
		int data_shndx;
		int rodata_shndx;
		int bss_shndx;
	} efile;
	/*
	 * All loaded bpf_object is linked in a list, which is
	 * hidden to caller. bpf_objects__<func> handlers deal with
	 * all objects.
	 */
	struct list_head list;

	struct btf *btf;
	struct btf_ext *btf_ext;

	void *priv;
	bpf_object_clear_priv_t clear_priv;

	struct bpf_capabilities caps;

	char path[];
};
#define obj_elf_valid(o)	((o)->efile.elf)

void bpf_program__unload(struct bpf_program *prog)
{
	int i;

	if (!prog)
		return;

	/*
	 * If the object is opened but the program was never loaded,
	 * it is possible that prog->instances.nr == -1.
	 */
	if (prog->instances.nr > 0) {
		for (i = 0; i < prog->instances.nr; i++)
			zclose(prog->instances.fds[i]);
	} else if (prog->instances.nr != -1) {
		pr_warning("Internal error: instances.nr is %d\n",
			   prog->instances.nr);
	}

	prog->instances.nr = -1;
	zfree(&prog->instances.fds);

	zfree(&prog->func_info);
	zfree(&prog->line_info);
}

static void bpf_program__exit(struct bpf_program *prog)
{
	if (!prog)
		return;

	if (prog->clear_priv)
		prog->clear_priv(prog, prog->priv);

	prog->priv = NULL;
	prog->clear_priv = NULL;

	bpf_program__unload(prog);
	zfree(&prog->name);
	zfree(&prog->section_name);
	zfree(&prog->pin_name);
	zfree(&prog->insns);
	zfree(&prog->reloc_desc);

	prog->nr_reloc = 0;
	prog->insns_cnt = 0;
	prog->idx = -1;
}

static char *__bpf_program__pin_name(struct bpf_program *prog)
{
	char *name, *p;

	name = p = strdup(prog->section_name);
	while ((p = strchr(p, '/')))
		*p = '_';

	return name;
}

static int
bpf_program__init(void *data, size_t size, char *section_name, int idx,
		  struct bpf_program *prog)
{
	const size_t bpf_insn_sz = sizeof(struct bpf_insn);

	if (size == 0 || size % bpf_insn_sz) {
		pr_warning("corrupted section '%s', size: %zu\n",
			   section_name, size);
		return -EINVAL;
	}

	memset(prog, 0, sizeof(*prog));

	prog->section_name = strdup(section_name);
	if (!prog->section_name) {
		pr_warning("failed to alloc name for prog under section(%d) %s\n",
			   idx, section_name);
		goto errout;
	}

	prog->pin_name = __bpf_program__pin_name(prog);
	if (!prog->pin_name) {
		pr_warning("failed to alloc pin name for prog under section(%d) %s\n",
			   idx, section_name);
		goto errout;
	}

	prog->insns = malloc(size);
	if (!prog->insns) {
		pr_warning("failed to alloc insns for prog under section %s\n",
			   section_name);
		goto errout;
	}
	prog->insns_cnt = size / bpf_insn_sz;
	memcpy(prog->insns, data, size);
	prog->idx = idx;
	prog->instances.fds = NULL;
	prog->instances.nr = -1;
	prog->type = BPF_PROG_TYPE_UNSPEC;

	return 0;
errout:
	bpf_program__exit(prog);
	return -ENOMEM;
}

static int
bpf_object__add_program(struct bpf_object *obj, void *data, size_t size,
			char *section_name, int idx)
{
	struct bpf_program prog, *progs;
	int nr_progs, err;

	err = bpf_program__init(data, size, section_name, idx, &prog);
	if (err)
		return err;

	prog.caps = &obj->caps;
	progs = obj->programs;
	nr_progs = obj->nr_programs;

	progs = reallocarray(progs, nr_progs + 1, sizeof(progs[0]));
	if (!progs) {
		/*
		 * In this case the original obj->programs
		 * is still valid, so don't need special treat for
		 * bpf_close_object().
		 */
		pr_warning("failed to alloc a new program under section '%s'\n",
			   section_name);
		bpf_program__exit(&prog);
		return -ENOMEM;
	}

	pr_debug("found program %s\n", prog.section_name);
	obj->programs = progs;
	obj->nr_programs = nr_progs + 1;
	prog.obj = obj;
	progs[nr_progs] = prog;
	return 0;
}

static int
bpf_object__init_prog_names(struct bpf_object *obj)
{
	Elf_Data *symbols = obj->efile.symbols;
	struct bpf_program *prog;
	size_t pi, si;

	for (pi = 0; pi < obj->nr_programs; pi++) {
		const char *name = NULL;

		prog = &obj->programs[pi];

		for (si = 0; si < symbols->d_size / sizeof(GElf_Sym) && !name;
		     si++) {
			GElf_Sym sym;

			if (!gelf_getsym(symbols, si, &sym))
				continue;
			if (sym.st_shndx != prog->idx)
				continue;
			if (GELF_ST_BIND(sym.st_info) != STB_GLOBAL)
				continue;

			name = elf_strptr(obj->efile.elf,
					  obj->efile.strtabidx,
					  sym.st_name);
			if (!name) {
				pr_warning("failed to get sym name string for prog %s\n",
					   prog->section_name);
				return -LIBBPF_ERRNO__LIBELF;
			}
		}

		if (!name && prog->idx == obj->efile.text_shndx)
			name = ".text";

		if (!name) {
			pr_warning("failed to find sym for prog %s\n",
				   prog->section_name);
			return -EINVAL;
		}

		prog->name = strdup(name);
		if (!prog->name) {
			pr_warning("failed to allocate memory for prog sym %s\n",
				   name);
			return -ENOMEM;
		}
	}

	return 0;
}

static struct bpf_object *bpf_object__new(const char *path,
					  void *obj_buf,
					  size_t obj_buf_sz)
{
	struct bpf_object *obj;
	char *end;

	obj = calloc(1, sizeof(struct bpf_object) + strlen(path) + 1);
	if (!obj) {
		pr_warning("alloc memory failed for %s\n", path);
		return ERR_PTR(-ENOMEM);
	}

	strcpy(obj->path, path);
	/* Using basename() GNU version which doesn't modify arg. */
	strncpy(obj->name, basename((void *)path), sizeof(obj->name) - 1);
	end = strchr(obj->name, '.');
	if (end)
		*end = 0;

	obj->efile.fd = -1;
	/*
	 * Caller of this function should also call
	 * bpf_object__elf_finish() after data collection to return
	 * obj_buf to user. If not, we should duplicate the buffer to
	 * avoid user freeing them before elf finish.
	 */
	obj->efile.obj_buf = obj_buf;
	obj->efile.obj_buf_sz = obj_buf_sz;
	obj->efile.maps_shndx = -1;
	obj->efile.btf_maps_shndx = -1;
	obj->efile.data_shndx = -1;
	obj->efile.rodata_shndx = -1;
	obj->efile.bss_shndx = -1;

	obj->loaded = false;

	INIT_LIST_HEAD(&obj->list);
	list_add(&obj->list, &bpf_objects_list);
	return obj;
}

static void bpf_object__elf_finish(struct bpf_object *obj)
{
	if (!obj_elf_valid(obj))
		return;

	if (obj->efile.elf) {
		elf_end(obj->efile.elf);
		obj->efile.elf = NULL;
	}
	obj->efile.symbols = NULL;
	obj->efile.data = NULL;
	obj->efile.rodata = NULL;
	obj->efile.bss = NULL;

	zfree(&obj->efile.reloc);
	obj->efile.nr_reloc = 0;
	zclose(obj->efile.fd);
	obj->efile.obj_buf = NULL;
	obj->efile.obj_buf_sz = 0;
}

static int bpf_object__elf_init(struct bpf_object *obj)
{
	int err = 0;
	GElf_Ehdr *ep;

	if (obj_elf_valid(obj)) {
		pr_warning("elf init: internal error\n");
		return -LIBBPF_ERRNO__LIBELF;
	}

	if (obj->efile.obj_buf_sz > 0) {
		/*
		 * obj_buf should have been validated by
		 * bpf_object__open_buffer().
		 */
		obj->efile.elf = elf_memory(obj->efile.obj_buf,
					    obj->efile.obj_buf_sz);
	} else {
		obj->efile.fd = open(obj->path, O_RDONLY);
		if (obj->efile.fd < 0) {
			char errmsg[STRERR_BUFSIZE], *cp;

			err = -errno;
			cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
			pr_warning("failed to open %s: %s\n", obj->path, cp);
			return err;
		}

		obj->efile.elf = elf_begin(obj->efile.fd,
					   LIBBPF_ELF_C_READ_MMAP, NULL);
	}

	if (!obj->efile.elf) {
		pr_warning("failed to open %s as ELF file\n", obj->path);
		err = -LIBBPF_ERRNO__LIBELF;
		goto errout;
	}

	if (!gelf_getehdr(obj->efile.elf, &obj->efile.ehdr)) {
		pr_warning("failed to get EHDR from %s\n", obj->path);
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}
	ep = &obj->efile.ehdr;

	/* Old LLVM set e_machine to EM_NONE */
	if (ep->e_type != ET_REL ||
	    (ep->e_machine && ep->e_machine != EM_BPF)) {
		pr_warning("%s is not an eBPF object file\n", obj->path);
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}

	return 0;
errout:
	bpf_object__elf_finish(obj);
	return err;
}

static int bpf_object__check_endianness(struct bpf_object *obj)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	if (obj->efile.ehdr.e_ident[EI_DATA] == ELFDATA2LSB)
		return 0;
#elif __BYTE_ORDER == __BIG_ENDIAN
	if (obj->efile.ehdr.e_ident[EI_DATA] == ELFDATA2MSB)
		return 0;
#else
# error "Unrecognized __BYTE_ORDER__"
#endif
	pr_warning("endianness mismatch.\n");
	return -LIBBPF_ERRNO__ENDIAN;
}

static int
bpf_object__init_license(struct bpf_object *obj, void *data, size_t size)
{
	memcpy(obj->license, data, min(size, sizeof(obj->license) - 1));
	pr_debug("license of %s is %s\n", obj->path, obj->license);
	return 0;
}

static int
bpf_object__init_kversion(struct bpf_object *obj, void *data, size_t size)
{
	__u32 kver;

	if (size != sizeof(kver)) {
		pr_warning("invalid kver section in %s\n", obj->path);
		return -LIBBPF_ERRNO__FORMAT;
	}
	memcpy(&kver, data, sizeof(kver));
	obj->kern_version = kver;
	pr_debug("kernel version of %s is %x\n", obj->path, obj->kern_version);
	return 0;
}

static int compare_bpf_map(const void *_a, const void *_b)
{
	const struct bpf_map *a = _a;
	const struct bpf_map *b = _b;

	if (a->sec_idx != b->sec_idx)
		return a->sec_idx - b->sec_idx;
	return a->sec_offset - b->sec_offset;
}

static bool bpf_map_type__is_map_in_map(enum bpf_map_type type)
{
	if (type == BPF_MAP_TYPE_ARRAY_OF_MAPS ||
	    type == BPF_MAP_TYPE_HASH_OF_MAPS)
		return true;
	return false;
}

static int bpf_object_search_section_size(const struct bpf_object *obj,
					  const char *name, size_t *d_size)
{
	const GElf_Ehdr *ep = &obj->efile.ehdr;
	Elf *elf = obj->efile.elf;
	Elf_Scn *scn = NULL;
	int idx = 0;

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		const char *sec_name;
		Elf_Data *data;
		GElf_Shdr sh;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_warning("failed to get section(%d) header from %s\n",
				   idx, obj->path);
			return -EIO;
		}

		sec_name = elf_strptr(elf, ep->e_shstrndx, sh.sh_name);
		if (!sec_name) {
			pr_warning("failed to get section(%d) name from %s\n",
				   idx, obj->path);
			return -EIO;
		}

		if (strcmp(name, sec_name))
			continue;

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warning("failed to get section(%d) data from %s(%s)\n",
				   idx, name, obj->path);
			return -EIO;
		}

		*d_size = data->d_size;
		return 0;
	}

	return -ENOENT;
}

int bpf_object__section_size(const struct bpf_object *obj, const char *name,
			     __u32 *size)
{
	int ret = -ENOENT;
	size_t d_size;

	*size = 0;
	if (!name) {
		return -EINVAL;
	} else if (!strcmp(name, ".data")) {
		if (obj->efile.data)
			*size = obj->efile.data->d_size;
	} else if (!strcmp(name, ".bss")) {
		if (obj->efile.bss)
			*size = obj->efile.bss->d_size;
	} else if (!strcmp(name, ".rodata")) {
		if (obj->efile.rodata)
			*size = obj->efile.rodata->d_size;
	} else {
		ret = bpf_object_search_section_size(obj, name, &d_size);
		if (!ret)
			*size = d_size;
	}

	return *size ? 0 : ret;
}

int bpf_object__variable_offset(const struct bpf_object *obj, const char *name,
				__u32 *off)
{
	Elf_Data *symbols = obj->efile.symbols;
	const char *sname;
	size_t si;

	if (!name || !off)
		return -EINVAL;

	for (si = 0; si < symbols->d_size / sizeof(GElf_Sym); si++) {
		GElf_Sym sym;

		if (!gelf_getsym(symbols, si, &sym))
			continue;
		if (GELF_ST_BIND(sym.st_info) != STB_GLOBAL ||
		    GELF_ST_TYPE(sym.st_info) != STT_OBJECT)
			continue;

		sname = elf_strptr(obj->efile.elf, obj->efile.strtabidx,
				   sym.st_name);
		if (!sname) {
			pr_warning("failed to get sym name string for var %s\n",
				   name);
			return -EIO;
		}
		if (strcmp(name, sname) == 0) {
			*off = sym.st_value;
			return 0;
		}
	}

	return -ENOENT;
}

static struct bpf_map *bpf_object__add_map(struct bpf_object *obj)
{
	struct bpf_map *new_maps;
	size_t new_cap;
	int i;

	if (obj->nr_maps < obj->maps_cap)
		return &obj->maps[obj->nr_maps++];

	new_cap = max((size_t)4, obj->maps_cap * 3 / 2);
	new_maps = realloc(obj->maps, new_cap * sizeof(*obj->maps));
	if (!new_maps) {
		pr_warning("alloc maps for object failed\n");
		return ERR_PTR(-ENOMEM);
	}

	obj->maps_cap = new_cap;
	obj->maps = new_maps;

	/* zero out new maps */
	memset(obj->maps + obj->nr_maps, 0,
	       (obj->maps_cap - obj->nr_maps) * sizeof(*obj->maps));
	/*
	 * fill all fd with -1 so won't close incorrect fd (fd=0 is stdin)
	 * when failure (zclose won't close negative fd)).
	 */
	for (i = obj->nr_maps; i < obj->maps_cap; i++) {
		obj->maps[i].fd = -1;
		obj->maps[i].inner_map_fd = -1;
	}

	return &obj->maps[obj->nr_maps++];
}

static int
bpf_object__init_internal_map(struct bpf_object *obj, enum libbpf_map_type type,
			      int sec_idx, Elf_Data *data, void **data_buff)
{
	char map_name[BPF_OBJ_NAME_LEN];
	struct bpf_map_def *def;
	struct bpf_map *map;

	map = bpf_object__add_map(obj);
	if (IS_ERR(map))
		return PTR_ERR(map);

	map->libbpf_type = type;
	map->sec_idx = sec_idx;
	map->sec_offset = 0;
	snprintf(map_name, sizeof(map_name), "%.8s%.7s", obj->name,
		 libbpf_type_to_btf_name[type]);
	map->name = strdup(map_name);
	if (!map->name) {
		pr_warning("failed to alloc map name\n");
		return -ENOMEM;
	}
	pr_debug("map '%s' (global data): at sec_idx %d, offset %zu.\n",
		 map_name, map->sec_idx, map->sec_offset);

	def = &map->def;
	def->type = BPF_MAP_TYPE_ARRAY;
	def->key_size = sizeof(int);
	def->value_size = data->d_size;
	def->max_entries = 1;
	def->map_flags = type == LIBBPF_MAP_RODATA ? BPF_F_RDONLY_PROG : 0;
	if (data_buff) {
		*data_buff = malloc(data->d_size);
		if (!*data_buff) {
			zfree(&map->name);
			pr_warning("failed to alloc map content buffer\n");
			return -ENOMEM;
		}
		memcpy(*data_buff, data->d_buf, data->d_size);
	}

	pr_debug("map %td is \"%s\"\n", map - obj->maps, map->name);
	return 0;
}

static int bpf_object__init_global_data_maps(struct bpf_object *obj)
{
	int err;

	if (!obj->caps.global_data)
		return 0;
	/*
	 * Populate obj->maps with libbpf internal maps.
	 */
	if (obj->efile.data_shndx >= 0) {
		err = bpf_object__init_internal_map(obj, LIBBPF_MAP_DATA,
						    obj->efile.data_shndx,
						    obj->efile.data,
						    &obj->sections.data);
		if (err)
			return err;
	}
	if (obj->efile.rodata_shndx >= 0) {
		err = bpf_object__init_internal_map(obj, LIBBPF_MAP_RODATA,
						    obj->efile.rodata_shndx,
						    obj->efile.rodata,
						    &obj->sections.rodata);
		if (err)
			return err;
	}
	if (obj->efile.bss_shndx >= 0) {
		err = bpf_object__init_internal_map(obj, LIBBPF_MAP_BSS,
						    obj->efile.bss_shndx,
						    obj->efile.bss, NULL);
		if (err)
			return err;
	}
	return 0;
}

static int bpf_object__init_user_maps(struct bpf_object *obj, bool strict)
{
	Elf_Data *symbols = obj->efile.symbols;
	int i, map_def_sz = 0, nr_maps = 0, nr_syms;
	Elf_Data *data = NULL;
	Elf_Scn *scn;

	if (obj->efile.maps_shndx < 0)
		return 0;

	if (!symbols)
		return -EINVAL;

	scn = elf_getscn(obj->efile.elf, obj->efile.maps_shndx);
	if (scn)
		data = elf_getdata(scn, NULL);
	if (!scn || !data) {
		pr_warning("failed to get Elf_Data from map section %d\n",
			   obj->efile.maps_shndx);
		return -EINVAL;
	}

	/*
	 * Count number of maps. Each map has a name.
	 * Array of maps is not supported: only the first element is
	 * considered.
	 *
	 * TODO: Detect array of map and report error.
	 */
	nr_syms = symbols->d_size / sizeof(GElf_Sym);
	for (i = 0; i < nr_syms; i++) {
		GElf_Sym sym;

		if (!gelf_getsym(symbols, i, &sym))
			continue;
		if (sym.st_shndx != obj->efile.maps_shndx)
			continue;
		nr_maps++;
	}
	/* Assume equally sized map definitions */
	pr_debug("maps in %s: %d maps in %zd bytes\n",
		 obj->path, nr_maps, data->d_size);

	map_def_sz = data->d_size / nr_maps;
	if (!data->d_size || (data->d_size % nr_maps) != 0) {
		pr_warning("unable to determine map definition size "
			   "section %s, %d maps in %zd bytes\n",
			   obj->path, nr_maps, data->d_size);
		return -EINVAL;
	}

	/* Fill obj->maps using data in "maps" section.  */
	for (i = 0; i < nr_syms; i++) {
		GElf_Sym sym;
		const char *map_name;
		struct bpf_map_def *def;
		struct bpf_map *map;

		if (!gelf_getsym(symbols, i, &sym))
			continue;
		if (sym.st_shndx != obj->efile.maps_shndx)
			continue;

		map = bpf_object__add_map(obj);
		if (IS_ERR(map))
			return PTR_ERR(map);

		map_name = elf_strptr(obj->efile.elf, obj->efile.strtabidx,
				      sym.st_name);
		if (!map_name) {
			pr_warning("failed to get map #%d name sym string for obj %s\n",
				   i, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}

		map->libbpf_type = LIBBPF_MAP_UNSPEC;
		map->sec_idx = sym.st_shndx;
		map->sec_offset = sym.st_value;
		pr_debug("map '%s' (legacy): at sec_idx %d, offset %zu.\n",
			 map_name, map->sec_idx, map->sec_offset);
		if (sym.st_value + map_def_sz > data->d_size) {
			pr_warning("corrupted maps section in %s: last map \"%s\" too small\n",
				   obj->path, map_name);
			return -EINVAL;
		}

		map->name = strdup(map_name);
		if (!map->name) {
			pr_warning("failed to alloc map name\n");
			return -ENOMEM;
		}
		pr_debug("map %d is \"%s\"\n", i, map->name);
		def = (struct bpf_map_def *)(data->d_buf + sym.st_value);
		/*
		 * If the definition of the map in the object file fits in
		 * bpf_map_def, copy it.  Any extra fields in our version
		 * of bpf_map_def will default to zero as a result of the
		 * calloc above.
		 */
		if (map_def_sz <= sizeof(struct bpf_map_def)) {
			memcpy(&map->def, def, map_def_sz);
		} else {
			/*
			 * Here the map structure being read is bigger than what
			 * we expect, truncate if the excess bits are all zero.
			 * If they are not zero, reject this map as
			 * incompatible.
			 */
			char *b;
			for (b = ((char *)def) + sizeof(struct bpf_map_def);
			     b < ((char *)def) + map_def_sz; b++) {
				if (*b != 0) {
					pr_warning("maps section in %s: \"%s\" "
						   "has unrecognized, non-zero "
						   "options\n",
						   obj->path, map_name);
					if (strict)
						return -EINVAL;
				}
			}
			memcpy(&map->def, def, sizeof(struct bpf_map_def));
		}
	}
	return 0;
}

static const struct btf_type *skip_mods_and_typedefs(const struct btf *btf,
						     __u32 id)
{
	const struct btf_type *t = btf__type_by_id(btf, id);

	while (true) {
		switch (BTF_INFO_KIND(t->info)) {
		case BTF_KIND_VOLATILE:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_TYPEDEF:
			t = btf__type_by_id(btf, t->type);
			break;
		default:
			return t;
		}
	}
}

/*
 * Fetch integer attribute of BTF map definition. Such attributes are
 * represented using a pointer to an array, in which dimensionality of array
 * encodes specified integer value. E.g., int (*type)[BPF_MAP_TYPE_ARRAY];
 * encodes `type => BPF_MAP_TYPE_ARRAY` key/value pair completely using BTF
 * type definition, while using only sizeof(void *) space in ELF data section.
 */
static bool get_map_field_int(const char *map_name, const struct btf *btf,
			      const struct btf_type *def,
			      const struct btf_member *m, __u32 *res) {
	const struct btf_type *t = skip_mods_and_typedefs(btf, m->type);
	const char *name = btf__name_by_offset(btf, m->name_off);
	const struct btf_array *arr_info;
	const struct btf_type *arr_t;

	if (BTF_INFO_KIND(t->info) != BTF_KIND_PTR) {
		pr_warning("map '%s': attr '%s': expected PTR, got %u.\n",
			   map_name, name, BTF_INFO_KIND(t->info));
		return false;
	}

	arr_t = btf__type_by_id(btf, t->type);
	if (!arr_t) {
		pr_warning("map '%s': attr '%s': type [%u] not found.\n",
			   map_name, name, t->type);
		return false;
	}
	if (BTF_INFO_KIND(arr_t->info) != BTF_KIND_ARRAY) {
		pr_warning("map '%s': attr '%s': expected ARRAY, got %u.\n",
			   map_name, name, BTF_INFO_KIND(arr_t->info));
		return false;
	}
	arr_info = (const void *)(arr_t + 1);
	*res = arr_info->nelems;
	return true;
}

static int bpf_object__init_user_btf_map(struct bpf_object *obj,
					 const struct btf_type *sec,
					 int var_idx, int sec_idx,
					 const Elf_Data *data, bool strict)
{
	const struct btf_type *var, *def, *t;
	const struct btf_var_secinfo *vi;
	const struct btf_var *var_extra;
	const struct btf_member *m;
	const char *map_name;
	struct bpf_map *map;
	int vlen, i;

	vi = (const struct btf_var_secinfo *)(const void *)(sec + 1) + var_idx;
	var = btf__type_by_id(obj->btf, vi->type);
	var_extra = (const void *)(var + 1);
	map_name = btf__name_by_offset(obj->btf, var->name_off);
	vlen = BTF_INFO_VLEN(var->info);

	if (map_name == NULL || map_name[0] == '\0') {
		pr_warning("map #%d: empty name.\n", var_idx);
		return -EINVAL;
	}
	if ((__u64)vi->offset + vi->size > data->d_size) {
		pr_warning("map '%s' BTF data is corrupted.\n", map_name);
		return -EINVAL;
	}
	if (BTF_INFO_KIND(var->info) != BTF_KIND_VAR) {
		pr_warning("map '%s': unexpected var kind %u.\n",
			   map_name, BTF_INFO_KIND(var->info));
		return -EINVAL;
	}
	if (var_extra->linkage != BTF_VAR_GLOBAL_ALLOCATED &&
	    var_extra->linkage != BTF_VAR_STATIC) {
		pr_warning("map '%s': unsupported var linkage %u.\n",
			   map_name, var_extra->linkage);
		return -EOPNOTSUPP;
	}

	def = skip_mods_and_typedefs(obj->btf, var->type);
	if (BTF_INFO_KIND(def->info) != BTF_KIND_STRUCT) {
		pr_warning("map '%s': unexpected def kind %u.\n",
			   map_name, BTF_INFO_KIND(var->info));
		return -EINVAL;
	}
	if (def->size > vi->size) {
		pr_warning("map '%s': invalid def size.\n", map_name);
		return -EINVAL;
	}

	map = bpf_object__add_map(obj);
	if (IS_ERR(map))
		return PTR_ERR(map);
	map->name = strdup(map_name);
	if (!map->name) {
		pr_warning("map '%s': failed to alloc map name.\n", map_name);
		return -ENOMEM;
	}
	map->libbpf_type = LIBBPF_MAP_UNSPEC;
	map->def.type = BPF_MAP_TYPE_UNSPEC;
	map->sec_idx = sec_idx;
	map->sec_offset = vi->offset;
	pr_debug("map '%s': at sec_idx %d, offset %zu.\n",
		 map_name, map->sec_idx, map->sec_offset);

	vlen = BTF_INFO_VLEN(def->info);
	m = (const void *)(def + 1);
	for (i = 0; i < vlen; i++, m++) {
		const char *name = btf__name_by_offset(obj->btf, m->name_off);

		if (!name) {
			pr_warning("map '%s': invalid field #%d.\n",
				   map_name, i);
			return -EINVAL;
		}
		if (strcmp(name, "type") == 0) {
			if (!get_map_field_int(map_name, obj->btf, def, m,
					       &map->def.type))
				return -EINVAL;
			pr_debug("map '%s': found type = %u.\n",
				 map_name, map->def.type);
		} else if (strcmp(name, "max_entries") == 0) {
			if (!get_map_field_int(map_name, obj->btf, def, m,
					       &map->def.max_entries))
				return -EINVAL;
			pr_debug("map '%s': found max_entries = %u.\n",
				 map_name, map->def.max_entries);
		} else if (strcmp(name, "map_flags") == 0) {
			if (!get_map_field_int(map_name, obj->btf, def, m,
					       &map->def.map_flags))
				return -EINVAL;
			pr_debug("map '%s': found map_flags = %u.\n",
				 map_name, map->def.map_flags);
		} else if (strcmp(name, "key_size") == 0) {
			__u32 sz;

			if (!get_map_field_int(map_name, obj->btf, def, m,
					       &sz))
				return -EINVAL;
			pr_debug("map '%s': found key_size = %u.\n",
				 map_name, sz);
			if (map->def.key_size && map->def.key_size != sz) {
				pr_warning("map '%s': conflicting key size %u != %u.\n",
					   map_name, map->def.key_size, sz);
				return -EINVAL;
			}
			map->def.key_size = sz;
		} else if (strcmp(name, "key") == 0) {
			__s64 sz;

			t = btf__type_by_id(obj->btf, m->type);
			if (!t) {
				pr_warning("map '%s': key type [%d] not found.\n",
					   map_name, m->type);
				return -EINVAL;
			}
			if (BTF_INFO_KIND(t->info) != BTF_KIND_PTR) {
				pr_warning("map '%s': key spec is not PTR: %u.\n",
					   map_name, BTF_INFO_KIND(t->info));
				return -EINVAL;
			}
			sz = btf__resolve_size(obj->btf, t->type);
			if (sz < 0) {
				pr_warning("map '%s': can't determine key size for type [%u]: %lld.\n",
					   map_name, t->type, sz);
				return sz;
			}
			pr_debug("map '%s': found key [%u], sz = %lld.\n",
				 map_name, t->type, sz);
			if (map->def.key_size && map->def.key_size != sz) {
				pr_warning("map '%s': conflicting key size %u != %lld.\n",
					   map_name, map->def.key_size, sz);
				return -EINVAL;
			}
			map->def.key_size = sz;
			map->btf_key_type_id = t->type;
		} else if (strcmp(name, "value_size") == 0) {
			__u32 sz;

			if (!get_map_field_int(map_name, obj->btf, def, m,
					       &sz))
				return -EINVAL;
			pr_debug("map '%s': found value_size = %u.\n",
				 map_name, sz);
			if (map->def.value_size && map->def.value_size != sz) {
				pr_warning("map '%s': conflicting value size %u != %u.\n",
					   map_name, map->def.value_size, sz);
				return -EINVAL;
			}
			map->def.value_size = sz;
		} else if (strcmp(name, "value") == 0) {
			__s64 sz;

			t = btf__type_by_id(obj->btf, m->type);
			if (!t) {
				pr_warning("map '%s': value type [%d] not found.\n",
					   map_name, m->type);
				return -EINVAL;
			}
			if (BTF_INFO_KIND(t->info) != BTF_KIND_PTR) {
				pr_warning("map '%s': value spec is not PTR: %u.\n",
					   map_name, BTF_INFO_KIND(t->info));
				return -EINVAL;
			}
			sz = btf__resolve_size(obj->btf, t->type);
			if (sz < 0) {
				pr_warning("map '%s': can't determine value size for type [%u]: %lld.\n",
					   map_name, t->type, sz);
				return sz;
			}
			pr_debug("map '%s': found value [%u], sz = %lld.\n",
				 map_name, t->type, sz);
			if (map->def.value_size && map->def.value_size != sz) {
				pr_warning("map '%s': conflicting value size %u != %lld.\n",
					   map_name, map->def.value_size, sz);
				return -EINVAL;
			}
			map->def.value_size = sz;
			map->btf_value_type_id = t->type;
		} else {
			if (strict) {
				pr_warning("map '%s': unknown field '%s'.\n",
					   map_name, name);
				return -ENOTSUP;
			}
			pr_debug("map '%s': ignoring unknown field '%s'.\n",
				 map_name, name);
		}
	}

	if (map->def.type == BPF_MAP_TYPE_UNSPEC) {
		pr_warning("map '%s': map type isn't specified.\n", map_name);
		return -EINVAL;
	}

	return 0;
}

static int bpf_object__init_user_btf_maps(struct bpf_object *obj, bool strict)
{
	const struct btf_type *sec = NULL;
	int nr_types, i, vlen, err;
	const struct btf_type *t;
	const char *name;
	Elf_Data *data;
	Elf_Scn *scn;

	if (obj->efile.btf_maps_shndx < 0)
		return 0;

	scn = elf_getscn(obj->efile.elf, obj->efile.btf_maps_shndx);
	if (scn)
		data = elf_getdata(scn, NULL);
	if (!scn || !data) {
		pr_warning("failed to get Elf_Data from map section %d (%s)\n",
			   obj->efile.maps_shndx, MAPS_ELF_SEC);
		return -EINVAL;
	}

	nr_types = btf__get_nr_types(obj->btf);
	for (i = 1; i <= nr_types; i++) {
		t = btf__type_by_id(obj->btf, i);
		if (BTF_INFO_KIND(t->info) != BTF_KIND_DATASEC)
			continue;
		name = btf__name_by_offset(obj->btf, t->name_off);
		if (strcmp(name, MAPS_ELF_SEC) == 0) {
			sec = t;
			break;
		}
	}

	if (!sec) {
		pr_warning("DATASEC '%s' not found.\n", MAPS_ELF_SEC);
		return -ENOENT;
	}

	vlen = BTF_INFO_VLEN(sec->info);
	for (i = 0; i < vlen; i++) {
		err = bpf_object__init_user_btf_map(obj, sec, i,
						    obj->efile.btf_maps_shndx,
						    data, strict);
		if (err)
			return err;
	}

	return 0;
}

static int bpf_object__init_maps(struct bpf_object *obj, int flags)
{
	bool strict = !(flags & MAPS_RELAX_COMPAT);
	int err;

	err = bpf_object__init_user_maps(obj, strict);
	if (err)
		return err;

	err = bpf_object__init_user_btf_maps(obj, strict);
	if (err)
		return err;

	err = bpf_object__init_global_data_maps(obj);
	if (err)
		return err;

	if (obj->nr_maps) {
		qsort(obj->maps, obj->nr_maps, sizeof(obj->maps[0]),
		      compare_bpf_map);
	}
	return 0;
}

static bool section_have_execinstr(struct bpf_object *obj, int idx)
{
	Elf_Scn *scn;
	GElf_Shdr sh;

	scn = elf_getscn(obj->efile.elf, idx);
	if (!scn)
		return false;

	if (gelf_getshdr(scn, &sh) != &sh)
		return false;

	if (sh.sh_flags & SHF_EXECINSTR)
		return true;

	return false;
}

static void bpf_object__sanitize_btf(struct bpf_object *obj)
{
	bool has_datasec = obj->caps.btf_datasec;
	bool has_func = obj->caps.btf_func;
	struct btf *btf = obj->btf;
	struct btf_type *t;
	int i, j, vlen;
	__u16 kind;

	if (!obj->btf || (has_func && has_datasec))
		return;

	for (i = 1; i <= btf__get_nr_types(btf); i++) {
		t = (struct btf_type *)btf__type_by_id(btf, i);
		kind = BTF_INFO_KIND(t->info);

		if (!has_datasec && kind == BTF_KIND_VAR) {
			/* replace VAR with INT */
			t->info = BTF_INFO_ENC(BTF_KIND_INT, 0, 0);
			/*
			 * using size = 1 is the safest choice, 4 will be too
			 * big and cause kernel BTF validation failure if
			 * original variable took less than 4 bytes
			 */
			t->size = 1;
			*(int *)(t+1) = BTF_INT_ENC(0, 0, 8);
		} else if (!has_datasec && kind == BTF_KIND_DATASEC) {
			/* replace DATASEC with STRUCT */
			struct btf_var_secinfo *v = (void *)(t + 1);
			struct btf_member *m = (void *)(t + 1);
			struct btf_type *vt;
			char *name;

			name = (char *)btf__name_by_offset(btf, t->name_off);
			while (*name) {
				if (*name == '.')
					*name = '_';
				name++;
			}

			vlen = BTF_INFO_VLEN(t->info);
			t->info = BTF_INFO_ENC(BTF_KIND_STRUCT, 0, vlen);
			for (j = 0; j < vlen; j++, v++, m++) {
				/* order of field assignments is important */
				m->offset = v->offset * 8;
				m->type = v->type;
				/* preserve variable name as member name */
				vt = (void *)btf__type_by_id(btf, v->type);
				m->name_off = vt->name_off;
			}
		} else if (!has_func && kind == BTF_KIND_FUNC_PROTO) {
			/* replace FUNC_PROTO with ENUM */
			vlen = BTF_INFO_VLEN(t->info);
			t->info = BTF_INFO_ENC(BTF_KIND_ENUM, 0, vlen);
			t->size = sizeof(__u32); /* kernel enforced */
		} else if (!has_func && kind == BTF_KIND_FUNC) {
			/* replace FUNC with TYPEDEF */
			t->info = BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0);
		}
	}
}

static void bpf_object__sanitize_btf_ext(struct bpf_object *obj)
{
	if (!obj->btf_ext)
		return;

	if (!obj->caps.btf_func) {
		btf_ext__free(obj->btf_ext);
		obj->btf_ext = NULL;
	}
}

static bool bpf_object__is_btf_mandatory(const struct bpf_object *obj)
{
	return obj->efile.btf_maps_shndx >= 0;
}

static int bpf_object__init_btf(struct bpf_object *obj,
				Elf_Data *btf_data,
				Elf_Data *btf_ext_data)
{
	bool btf_required = bpf_object__is_btf_mandatory(obj);
	int err = 0;

	if (btf_data) {
		obj->btf = btf__new(btf_data->d_buf, btf_data->d_size);
		if (IS_ERR(obj->btf)) {
			pr_warning("Error loading ELF section %s: %d.\n",
				   BTF_ELF_SEC, err);
			goto out;
		}
		err = btf__finalize_data(obj, obj->btf);
		if (err) {
			pr_warning("Error finalizing %s: %d.\n",
				   BTF_ELF_SEC, err);
			goto out;
		}
	}
	if (btf_ext_data) {
		if (!obj->btf) {
			pr_debug("Ignore ELF section %s because its depending ELF section %s is not found.\n",
				 BTF_EXT_ELF_SEC, BTF_ELF_SEC);
			goto out;
		}
		obj->btf_ext = btf_ext__new(btf_ext_data->d_buf,
					    btf_ext_data->d_size);
		if (IS_ERR(obj->btf_ext)) {
			pr_warning("Error loading ELF section %s: %ld. Ignored and continue.\n",
				   BTF_EXT_ELF_SEC, PTR_ERR(obj->btf_ext));
			obj->btf_ext = NULL;
			goto out;
		}
	}
out:
	if (err || IS_ERR(obj->btf)) {
		if (btf_required)
			err = err ? : PTR_ERR(obj->btf);
		else
			err = 0;
		if (!IS_ERR_OR_NULL(obj->btf))
			btf__free(obj->btf);
		obj->btf = NULL;
	}
	if (btf_required && !obj->btf) {
		pr_warning("BTF is required, but is missing or corrupted.\n");
		return err == 0 ? -ENOENT : err;
	}
	return 0;
}

static int bpf_object__sanitize_and_load_btf(struct bpf_object *obj)
{
	int err = 0;

	if (!obj->btf)
		return 0;

	bpf_object__sanitize_btf(obj);
	bpf_object__sanitize_btf_ext(obj);

	err = btf__load(obj->btf);
	if (err) {
		pr_warning("Error loading %s into kernel: %d.\n",
			   BTF_ELF_SEC, err);
		btf__free(obj->btf);
		obj->btf = NULL;
		/* btf_ext can't exist without btf, so free it as well */
		if (obj->btf_ext) {
			btf_ext__free(obj->btf_ext);
			obj->btf_ext = NULL;
		}

		if (bpf_object__is_btf_mandatory(obj))
			return err;
	}
	return 0;
}

static int bpf_object__elf_collect(struct bpf_object *obj, int flags)
{
	Elf *elf = obj->efile.elf;
	GElf_Ehdr *ep = &obj->efile.ehdr;
	Elf_Data *btf_ext_data = NULL;
	Elf_Data *btf_data = NULL;
	Elf_Scn *scn = NULL;
	int idx = 0, err = 0;

	/* Elf is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, ep->e_shstrndx), NULL)) {
		pr_warning("failed to get e_shstrndx from %s\n", obj->path);
		return -LIBBPF_ERRNO__FORMAT;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		char *name;
		GElf_Shdr sh;
		Elf_Data *data;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_warning("failed to get section(%d) header from %s\n",
				   idx, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}

		name = elf_strptr(elf, ep->e_shstrndx, sh.sh_name);
		if (!name) {
			pr_warning("failed to get section(%d) name from %s\n",
				   idx, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warning("failed to get section(%d) data from %s(%s)\n",
				   idx, name, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}
		pr_debug("section(%d) %s, size %ld, link %d, flags %lx, type=%d\n",
			 idx, name, (unsigned long)data->d_size,
			 (int)sh.sh_link, (unsigned long)sh.sh_flags,
			 (int)sh.sh_type);

		if (strcmp(name, "license") == 0) {
			err = bpf_object__init_license(obj,
						       data->d_buf,
						       data->d_size);
			if (err)
				return err;
		} else if (strcmp(name, "version") == 0) {
			err = bpf_object__init_kversion(obj,
							data->d_buf,
							data->d_size);
			if (err)
				return err;
		} else if (strcmp(name, "maps") == 0) {
			obj->efile.maps_shndx = idx;
		} else if (strcmp(name, MAPS_ELF_SEC) == 0) {
			obj->efile.btf_maps_shndx = idx;
		} else if (strcmp(name, BTF_ELF_SEC) == 0) {
			btf_data = data;
		} else if (strcmp(name, BTF_EXT_ELF_SEC) == 0) {
			btf_ext_data = data;
		} else if (sh.sh_type == SHT_SYMTAB) {
			if (obj->efile.symbols) {
				pr_warning("bpf: multiple SYMTAB in %s\n",
					   obj->path);
				return -LIBBPF_ERRNO__FORMAT;
			}
			obj->efile.symbols = data;
			obj->efile.strtabidx = sh.sh_link;
		} else if (sh.sh_type == SHT_PROGBITS && data->d_size > 0) {
			if (sh.sh_flags & SHF_EXECINSTR) {
				if (strcmp(name, ".text") == 0)
					obj->efile.text_shndx = idx;
				err = bpf_object__add_program(obj, data->d_buf,
							      data->d_size, name, idx);
				if (err) {
					char errmsg[STRERR_BUFSIZE];
					char *cp = libbpf_strerror_r(-err, errmsg,
								     sizeof(errmsg));

					pr_warning("failed to alloc program %s (%s): %s",
						   name, obj->path, cp);
					return err;
				}
			} else if (strcmp(name, ".data") == 0) {
				obj->efile.data = data;
				obj->efile.data_shndx = idx;
			} else if (strcmp(name, ".rodata") == 0) {
				obj->efile.rodata = data;
				obj->efile.rodata_shndx = idx;
			} else {
				pr_debug("skip section(%d) %s\n", idx, name);
			}
		} else if (sh.sh_type == SHT_REL) {
			int nr_reloc = obj->efile.nr_reloc;
			void *reloc = obj->efile.reloc;
			int sec = sh.sh_info; /* points to other section */

			/* Only do relo for section with exec instructions */
			if (!section_have_execinstr(obj, sec)) {
				pr_debug("skip relo %s(%d) for section(%d)\n",
					 name, idx, sec);
				continue;
			}

			reloc = reallocarray(reloc, nr_reloc + 1,
					     sizeof(*obj->efile.reloc));
			if (!reloc) {
				pr_warning("realloc failed\n");
				return -ENOMEM;
			}

			obj->efile.reloc = reloc;
			obj->efile.nr_reloc++;

			obj->efile.reloc[nr_reloc].shdr = sh;
			obj->efile.reloc[nr_reloc].data = data;
		} else if (sh.sh_type == SHT_NOBITS && strcmp(name, ".bss") == 0) {
			obj->efile.bss = data;
			obj->efile.bss_shndx = idx;
		} else {
			pr_debug("skip section(%d) %s\n", idx, name);
		}
	}

	if (!obj->efile.strtabidx || obj->efile.strtabidx >= idx) {
		pr_warning("Corrupted ELF file: index of strtab invalid\n");
		return -LIBBPF_ERRNO__FORMAT;
	}
	err = bpf_object__init_btf(obj, btf_data, btf_ext_data);
	if (!err)
		err = bpf_object__init_maps(obj, flags);
	if (!err)
		err = bpf_object__sanitize_and_load_btf(obj);
	if (!err)
		err = bpf_object__init_prog_names(obj);
	return err;
}

static struct bpf_program *
bpf_object__find_prog_by_idx(struct bpf_object *obj, int idx)
{
	struct bpf_program *prog;
	size_t i;

	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		if (prog->idx == idx)
			return prog;
	}
	return NULL;
}

struct bpf_program *
bpf_object__find_program_by_title(const struct bpf_object *obj,
				  const char *title)
{
	struct bpf_program *pos;

	bpf_object__for_each_program(pos, obj) {
		if (pos->section_name && !strcmp(pos->section_name, title))
			return pos;
	}
	return NULL;
}

static bool bpf_object__shndx_is_data(const struct bpf_object *obj,
				      int shndx)
{
	return shndx == obj->efile.data_shndx ||
	       shndx == obj->efile.bss_shndx ||
	       shndx == obj->efile.rodata_shndx;
}

static bool bpf_object__shndx_is_maps(const struct bpf_object *obj,
				      int shndx)
{
	return shndx == obj->efile.maps_shndx ||
	       shndx == obj->efile.btf_maps_shndx;
}

static bool bpf_object__relo_in_known_section(const struct bpf_object *obj,
					      int shndx)
{
	return shndx == obj->efile.text_shndx ||
	       bpf_object__shndx_is_maps(obj, shndx) ||
	       bpf_object__shndx_is_data(obj, shndx);
}

static enum libbpf_map_type
bpf_object__section_to_libbpf_map_type(const struct bpf_object *obj, int shndx)
{
	if (shndx == obj->efile.data_shndx)
		return LIBBPF_MAP_DATA;
	else if (shndx == obj->efile.bss_shndx)
		return LIBBPF_MAP_BSS;
	else if (shndx == obj->efile.rodata_shndx)
		return LIBBPF_MAP_RODATA;
	else
		return LIBBPF_MAP_UNSPEC;
}

static int
bpf_program__collect_reloc(struct bpf_program *prog, GElf_Shdr *shdr,
			   Elf_Data *data, struct bpf_object *obj)
{
	Elf_Data *symbols = obj->efile.symbols;
	struct bpf_map *maps = obj->maps;
	size_t nr_maps = obj->nr_maps;
	int i, nrels;

	pr_debug("collecting relocating info for: '%s'\n", prog->section_name);
	nrels = shdr->sh_size / shdr->sh_entsize;

	prog->reloc_desc = malloc(sizeof(*prog->reloc_desc) * nrels);
	if (!prog->reloc_desc) {
		pr_warning("failed to alloc memory in relocation\n");
		return -ENOMEM;
	}
	prog->nr_reloc = nrels;

	for (i = 0; i < nrels; i++) {
		struct bpf_insn *insns = prog->insns;
		enum libbpf_map_type type;
		unsigned int insn_idx;
		unsigned int shdr_idx;
		const char *name;
		size_t map_idx;
		GElf_Sym sym;
		GElf_Rel rel;

		if (!gelf_getrel(data, i, &rel)) {
			pr_warning("relocation: failed to get %d reloc\n", i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (!gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &sym)) {
			pr_warning("relocation: symbol %"PRIx64" not found\n",
				   GELF_R_SYM(rel.r_info));
			return -LIBBPF_ERRNO__FORMAT;
		}

		name = elf_strptr(obj->efile.elf, obj->efile.strtabidx,
				  sym.st_name) ? : "<?>";

		pr_debug("relo for %lld value %lld name %d (\'%s\')\n",
			 (long long) (rel.r_info >> 32),
			 (long long) sym.st_value, sym.st_name, name);

		shdr_idx = sym.st_shndx;
		if (!bpf_object__relo_in_known_section(obj, shdr_idx)) {
			pr_warning("Program '%s' contains unrecognized relo data pointing to section %u\n",
				   prog->section_name, shdr_idx);
			return -LIBBPF_ERRNO__RELOC;
		}

		insn_idx = rel.r_offset / sizeof(struct bpf_insn);
		pr_debug("relocation: insn_idx=%u\n", insn_idx);

		if (insns[insn_idx].code == (BPF_JMP | BPF_CALL)) {
			if (insns[insn_idx].src_reg != BPF_PSEUDO_CALL) {
				pr_warning("incorrect bpf_call opcode\n");
				return -LIBBPF_ERRNO__RELOC;
			}
			prog->reloc_desc[i].type = RELO_CALL;
			prog->reloc_desc[i].insn_idx = insn_idx;
			prog->reloc_desc[i].text_off = sym.st_value;
			obj->has_pseudo_calls = true;
			continue;
		}

		if (insns[insn_idx].code != (BPF_LD | BPF_IMM | BPF_DW)) {
			pr_warning("bpf: relocation: invalid relo for insns[%d].code 0x%x\n",
				   insn_idx, insns[insn_idx].code);
			return -LIBBPF_ERRNO__RELOC;
		}

		if (bpf_object__shndx_is_maps(obj, shdr_idx) ||
		    bpf_object__shndx_is_data(obj, shdr_idx)) {
			type = bpf_object__section_to_libbpf_map_type(obj, shdr_idx);
			if (type != LIBBPF_MAP_UNSPEC) {
				if (GELF_ST_BIND(sym.st_info) == STB_GLOBAL) {
					pr_warning("bpf: relocation: not yet supported relo for non-static global \'%s\' variable found in insns[%d].code 0x%x\n",
						   name, insn_idx, insns[insn_idx].code);
					return -LIBBPF_ERRNO__RELOC;
				}
				if (!obj->caps.global_data) {
					pr_warning("bpf: relocation: kernel does not support global \'%s\' variable access in insns[%d]\n",
						   name, insn_idx);
					return -LIBBPF_ERRNO__RELOC;
				}
			}

			for (map_idx = 0; map_idx < nr_maps; map_idx++) {
				if (maps[map_idx].libbpf_type != type)
					continue;
				if (type != LIBBPF_MAP_UNSPEC ||
				    (maps[map_idx].sec_idx == sym.st_shndx &&
				     maps[map_idx].sec_offset == sym.st_value)) {
					pr_debug("relocation: found map %zd (%s, sec_idx %d, offset %zu) for insn %u\n",
						 map_idx, maps[map_idx].name,
						 maps[map_idx].sec_idx,
						 maps[map_idx].sec_offset,
						 insn_idx);
					break;
				}
			}

			if (map_idx >= nr_maps) {
				pr_warning("bpf relocation: map_idx %d larger than %d\n",
					   (int)map_idx, (int)nr_maps - 1);
				return -LIBBPF_ERRNO__RELOC;
			}

			prog->reloc_desc[i].type = type != LIBBPF_MAP_UNSPEC ?
						   RELO_DATA : RELO_LD64;
			prog->reloc_desc[i].insn_idx = insn_idx;
			prog->reloc_desc[i].map_idx = map_idx;
		}
	}
	return 0;
}

static int bpf_map_find_btf_info(struct bpf_object *obj, struct bpf_map *map)
{
	struct bpf_map_def *def = &map->def;
	__u32 key_type_id = 0, value_type_id = 0;
	int ret;

	/* if it's BTF-defined map, we don't need to search for type IDs */
	if (map->sec_idx == obj->efile.btf_maps_shndx)
		return 0;

	if (!bpf_map__is_internal(map)) {
		ret = btf__get_map_kv_tids(obj->btf, map->name, def->key_size,
					   def->value_size, &key_type_id,
					   &value_type_id);
	} else {
		/*
		 * LLVM annotates global data differently in BTF, that is,
		 * only as '.data', '.bss' or '.rodata'.
		 */
		ret = btf__find_by_name(obj->btf,
				libbpf_type_to_btf_name[map->libbpf_type]);
	}
	if (ret < 0)
		return ret;

	map->btf_key_type_id = key_type_id;
	map->btf_value_type_id = bpf_map__is_internal(map) ?
				 ret : value_type_id;
	return 0;
}

int bpf_map__reuse_fd(struct bpf_map *map, int fd)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	int new_fd, err;
	char *new_name;

	err = bpf_obj_get_info_by_fd(fd, &info, &len);
	if (err)
		return err;

	new_name = strdup(info.name);
	if (!new_name)
		return -errno;

	new_fd = open("/", O_RDONLY | O_CLOEXEC);
	if (new_fd < 0)
		goto err_free_new_name;

	new_fd = dup3(fd, new_fd, O_CLOEXEC);
	if (new_fd < 0)
		goto err_close_new_fd;

	err = zclose(map->fd);
	if (err)
		goto err_close_new_fd;
	free(map->name);

	map->fd = new_fd;
	map->name = new_name;
	map->def.type = info.type;
	map->def.key_size = info.key_size;
	map->def.value_size = info.value_size;
	map->def.max_entries = info.max_entries;
	map->def.map_flags = info.map_flags;
	map->btf_key_type_id = info.btf_key_type_id;
	map->btf_value_type_id = info.btf_value_type_id;

	return 0;

err_close_new_fd:
	close(new_fd);
err_free_new_name:
	free(new_name);
	return -errno;
}

int bpf_map__resize(struct bpf_map *map, __u32 max_entries)
{
	if (!map || !max_entries)
		return -EINVAL;

	/* If map already created, its attributes can't be changed. */
	if (map->fd >= 0)
		return -EBUSY;

	map->def.max_entries = max_entries;

	return 0;
}

static int
bpf_object__probe_name(struct bpf_object *obj)
{
	struct bpf_load_program_attr attr;
	char *cp, errmsg[STRERR_BUFSIZE];
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int ret;

	/* make sure basic loading works */

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insns = insns;
	attr.insns_cnt = ARRAY_SIZE(insns);
	attr.license = "GPL";

	ret = bpf_load_program_xattr(&attr, NULL, 0);
	if (ret < 0) {
		cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
		pr_warning("Error in %s():%s(%d). Couldn't load basic 'r0 = 0' BPF program.\n",
			   __func__, cp, errno);
		return -errno;
	}
	close(ret);

	/* now try the same program, but with the name */

	attr.name = "test";
	ret = bpf_load_program_xattr(&attr, NULL, 0);
	if (ret >= 0) {
		obj->caps.name = 1;
		close(ret);
	}

	return 0;
}

static int
bpf_object__probe_global_data(struct bpf_object *obj)
{
	struct bpf_load_program_attr prg_attr;
	struct bpf_create_map_attr map_attr;
	char *cp, errmsg[STRERR_BUFSIZE];
	struct bpf_insn insns[] = {
		BPF_LD_MAP_VALUE(BPF_REG_1, 0, 16),
		BPF_ST_MEM(BPF_DW, BPF_REG_1, 0, 42),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int ret, map;

	memset(&map_attr, 0, sizeof(map_attr));
	map_attr.map_type = BPF_MAP_TYPE_ARRAY;
	map_attr.key_size = sizeof(int);
	map_attr.value_size = 32;
	map_attr.max_entries = 1;

	map = bpf_create_map_xattr(&map_attr);
	if (map < 0) {
		cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
		pr_warning("Error in %s():%s(%d). Couldn't create simple array map.\n",
			   __func__, cp, errno);
		return -errno;
	}

	insns[0].imm = map;

	memset(&prg_attr, 0, sizeof(prg_attr));
	prg_attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	prg_attr.insns = insns;
	prg_attr.insns_cnt = ARRAY_SIZE(insns);
	prg_attr.license = "GPL";

	ret = bpf_load_program_xattr(&prg_attr, NULL, 0);
	if (ret >= 0) {
		obj->caps.global_data = 1;
		close(ret);
	}

	close(map);
	return 0;
}

static int bpf_object__probe_btf_func(struct bpf_object *obj)
{
	const char strs[] = "\0int\0x\0a";
	/* void x(int a) {} */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* FUNC_PROTO */                                /* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 1), 0),
		BTF_PARAM_ENC(7, 1),
		/* FUNC x */                                    /* [3] */
		BTF_TYPE_ENC(5, BTF_INFO_ENC(BTF_KIND_FUNC, 0, 0), 2),
	};
	int btf_fd;

	btf_fd = libbpf__load_raw_btf((char *)types, sizeof(types),
				      strs, sizeof(strs));
	if (btf_fd >= 0) {
		obj->caps.btf_func = 1;
		close(btf_fd);
		return 1;
	}

	return 0;
}

static int bpf_object__probe_btf_datasec(struct bpf_object *obj)
{
	const char strs[] = "\0x\0.data";
	/* static int a; */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* VAR x */                                     /* [2] */
		BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_VAR, 0, 0), 1),
		BTF_VAR_STATIC,
		/* DATASEC val */                               /* [3] */
		BTF_TYPE_ENC(3, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(2, 0, 4),
	};
	int btf_fd;

	btf_fd = libbpf__load_raw_btf((char *)types, sizeof(types),
				      strs, sizeof(strs));
	if (btf_fd >= 0) {
		obj->caps.btf_datasec = 1;
		close(btf_fd);
		return 1;
	}

	return 0;
}

static int
bpf_object__probe_caps(struct bpf_object *obj)
{
	int (*probe_fn[])(struct bpf_object *obj) = {
		bpf_object__probe_name,
		bpf_object__probe_global_data,
		bpf_object__probe_btf_func,
		bpf_object__probe_btf_datasec,
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(probe_fn); i++) {
		ret = probe_fn[i](obj);
		if (ret < 0)
			pr_debug("Probe #%d failed with %d.\n", i, ret);
	}

	return 0;
}

static int
bpf_object__populate_internal_map(struct bpf_object *obj, struct bpf_map *map)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err, zero = 0;
	__u8 *data;

	/* Nothing to do here since kernel already zero-initializes .bss map. */
	if (map->libbpf_type == LIBBPF_MAP_BSS)
		return 0;

	data = map->libbpf_type == LIBBPF_MAP_DATA ?
	       obj->sections.data : obj->sections.rodata;

	err = bpf_map_update_elem(map->fd, &zero, data, 0);
	/* Freeze .rodata map as read-only from syscall side. */
	if (!err && map->libbpf_type == LIBBPF_MAP_RODATA) {
		err = bpf_map_freeze(map->fd);
		if (err) {
			cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
			pr_warning("Error freezing map(%s) as read-only: %s\n",
				   map->name, cp);
			err = 0;
		}
	}
	return err;
}

static int
bpf_object__create_maps(struct bpf_object *obj)
{
	struct bpf_create_map_attr create_attr = {};
	int nr_cpus = 0;
	unsigned int i;
	int err;

	for (i = 0; i < obj->nr_maps; i++) {
		struct bpf_map *map = &obj->maps[i];
		struct bpf_map_def *def = &map->def;
		char *cp, errmsg[STRERR_BUFSIZE];
		int *pfd = &map->fd;

		if (map->fd >= 0) {
			pr_debug("skip map create (preset) %s: fd=%d\n",
				 map->name, map->fd);
			continue;
		}

		if (obj->caps.name)
			create_attr.name = map->name;
		create_attr.map_ifindex = map->map_ifindex;
		create_attr.map_type = def->type;
		create_attr.map_flags = def->map_flags;
		create_attr.key_size = def->key_size;
		create_attr.value_size = def->value_size;
		if (def->type == BPF_MAP_TYPE_PERF_EVENT_ARRAY &&
		    !def->max_entries) {
			if (!nr_cpus)
				nr_cpus = libbpf_num_possible_cpus();
			if (nr_cpus < 0) {
				pr_warning("failed to determine number of system CPUs: %d\n",
					   nr_cpus);
				err = nr_cpus;
				goto err_out;
			}
			pr_debug("map '%s': setting size to %d\n",
				 map->name, nr_cpus);
			create_attr.max_entries = nr_cpus;
		} else {
			create_attr.max_entries = def->max_entries;
		}
		create_attr.btf_fd = 0;
		create_attr.btf_key_type_id = 0;
		create_attr.btf_value_type_id = 0;
		if (bpf_map_type__is_map_in_map(def->type) &&
		    map->inner_map_fd >= 0)
			create_attr.inner_map_fd = map->inner_map_fd;

		if (obj->btf && !bpf_map_find_btf_info(obj, map)) {
			create_attr.btf_fd = btf__fd(obj->btf);
			create_attr.btf_key_type_id = map->btf_key_type_id;
			create_attr.btf_value_type_id = map->btf_value_type_id;
		}

		*pfd = bpf_create_map_xattr(&create_attr);
		if (*pfd < 0 && (create_attr.btf_key_type_id ||
				 create_attr.btf_value_type_id)) {
			err = -errno;
			cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
			pr_warning("Error in bpf_create_map_xattr(%s):%s(%d). Retrying without BTF.\n",
				   map->name, cp, err);
			create_attr.btf_fd = 0;
			create_attr.btf_key_type_id = 0;
			create_attr.btf_value_type_id = 0;
			map->btf_key_type_id = 0;
			map->btf_value_type_id = 0;
			*pfd = bpf_create_map_xattr(&create_attr);
		}

		if (*pfd < 0) {
			size_t j;

			err = -errno;
err_out:
			cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
			pr_warning("failed to create map (name: '%s'): %s(%d)\n",
				   map->name, cp, err);
			for (j = 0; j < i; j++)
				zclose(obj->maps[j].fd);
			return err;
		}

		if (bpf_map__is_internal(map)) {
			err = bpf_object__populate_internal_map(obj, map);
			if (err < 0) {
				zclose(*pfd);
				goto err_out;
			}
		}

		pr_debug("created map %s: fd=%d\n", map->name, *pfd);
	}

	return 0;
}

static int
check_btf_ext_reloc_err(struct bpf_program *prog, int err,
			void *btf_prog_info, const char *info_name)
{
	if (err != -ENOENT) {
		pr_warning("Error in loading %s for sec %s.\n",
			   info_name, prog->section_name);
		return err;
	}

	/* err == -ENOENT (i.e. prog->section_name not found in btf_ext) */

	if (btf_prog_info) {
		/*
		 * Some info has already been found but has problem
		 * in the last btf_ext reloc. Must have to error out.
		 */
		pr_warning("Error in relocating %s for sec %s.\n",
			   info_name, prog->section_name);
		return err;
	}

	/* Have problem loading the very first info. Ignore the rest. */
	pr_warning("Cannot find %s for main program sec %s. Ignore all %s.\n",
		   info_name, prog->section_name, info_name);
	return 0;
}

static int
bpf_program_reloc_btf_ext(struct bpf_program *prog, struct bpf_object *obj,
			  const char *section_name,  __u32 insn_offset)
{
	int err;

	if (!insn_offset || prog->func_info) {
		/*
		 * !insn_offset => main program
		 *
		 * For sub prog, the main program's func_info has to
		 * be loaded first (i.e. prog->func_info != NULL)
		 */
		err = btf_ext__reloc_func_info(obj->btf, obj->btf_ext,
					       section_name, insn_offset,
					       &prog->func_info,
					       &prog->func_info_cnt);
		if (err)
			return check_btf_ext_reloc_err(prog, err,
						       prog->func_info,
						       "bpf_func_info");

		prog->func_info_rec_size = btf_ext__func_info_rec_size(obj->btf_ext);
	}

	if (!insn_offset || prog->line_info) {
		err = btf_ext__reloc_line_info(obj->btf, obj->btf_ext,
					       section_name, insn_offset,
					       &prog->line_info,
					       &prog->line_info_cnt);
		if (err)
			return check_btf_ext_reloc_err(prog, err,
						       prog->line_info,
						       "bpf_line_info");

		prog->line_info_rec_size = btf_ext__line_info_rec_size(obj->btf_ext);
	}

	return 0;
}

static int
bpf_program__reloc_text(struct bpf_program *prog, struct bpf_object *obj,
			struct reloc_desc *relo)
{
	struct bpf_insn *insn, *new_insn;
	struct bpf_program *text;
	size_t new_cnt;
	int err;

	if (relo->type != RELO_CALL)
		return -LIBBPF_ERRNO__RELOC;

	if (prog->idx == obj->efile.text_shndx) {
		pr_warning("relo in .text insn %d into off %d\n",
			   relo->insn_idx, relo->text_off);
		return -LIBBPF_ERRNO__RELOC;
	}

	if (prog->main_prog_cnt == 0) {
		text = bpf_object__find_prog_by_idx(obj, obj->efile.text_shndx);
		if (!text) {
			pr_warning("no .text section found yet relo into text exist\n");
			return -LIBBPF_ERRNO__RELOC;
		}
		new_cnt = prog->insns_cnt + text->insns_cnt;
		new_insn = reallocarray(prog->insns, new_cnt, sizeof(*insn));
		if (!new_insn) {
			pr_warning("oom in prog realloc\n");
			return -ENOMEM;
		}

		if (obj->btf_ext) {
			err = bpf_program_reloc_btf_ext(prog, obj,
							text->section_name,
							prog->insns_cnt);
			if (err)
				return err;
		}

		memcpy(new_insn + prog->insns_cnt, text->insns,
		       text->insns_cnt * sizeof(*insn));
		prog->insns = new_insn;
		prog->main_prog_cnt = prog->insns_cnt;
		prog->insns_cnt = new_cnt;
		pr_debug("added %zd insn from %s to prog %s\n",
			 text->insns_cnt, text->section_name,
			 prog->section_name);
	}
	insn = &prog->insns[relo->insn_idx];
	insn->imm += prog->main_prog_cnt - relo->insn_idx;
	return 0;
}

static int
bpf_program__relocate(struct bpf_program *prog, struct bpf_object *obj)
{
	int i, err;

	if (!prog)
		return 0;

	if (obj->btf_ext) {
		err = bpf_program_reloc_btf_ext(prog, obj,
						prog->section_name, 0);
		if (err)
			return err;
	}

	if (!prog->reloc_desc)
		return 0;

	for (i = 0; i < prog->nr_reloc; i++) {
		if (prog->reloc_desc[i].type == RELO_LD64 ||
		    prog->reloc_desc[i].type == RELO_DATA) {
			bool relo_data = prog->reloc_desc[i].type == RELO_DATA;
			struct bpf_insn *insns = prog->insns;
			int insn_idx, map_idx;

			insn_idx = prog->reloc_desc[i].insn_idx;
			map_idx = prog->reloc_desc[i].map_idx;

			if (insn_idx + 1 >= (int)prog->insns_cnt) {
				pr_warning("relocation out of range: '%s'\n",
					   prog->section_name);
				return -LIBBPF_ERRNO__RELOC;
			}

			if (!relo_data) {
				insns[insn_idx].src_reg = BPF_PSEUDO_MAP_FD;
			} else {
				insns[insn_idx].src_reg = BPF_PSEUDO_MAP_VALUE;
				insns[insn_idx + 1].imm = insns[insn_idx].imm;
			}
			insns[insn_idx].imm = obj->maps[map_idx].fd;
		} else if (prog->reloc_desc[i].type == RELO_CALL) {
			err = bpf_program__reloc_text(prog, obj,
						      &prog->reloc_desc[i]);
			if (err)
				return err;
		}
	}

	zfree(&prog->reloc_desc);
	prog->nr_reloc = 0;
	return 0;
}


static int
bpf_object__relocate(struct bpf_object *obj)
{
	struct bpf_program *prog;
	size_t i;
	int err;

	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];

		err = bpf_program__relocate(prog, obj);
		if (err) {
			pr_warning("failed to relocate '%s'\n",
				   prog->section_name);
			return err;
		}
	}
	return 0;
}

static int bpf_object__collect_reloc(struct bpf_object *obj)
{
	int i, err;

	if (!obj_elf_valid(obj)) {
		pr_warning("Internal error: elf object is closed\n");
		return -LIBBPF_ERRNO__INTERNAL;
	}

	for (i = 0; i < obj->efile.nr_reloc; i++) {
		GElf_Shdr *shdr = &obj->efile.reloc[i].shdr;
		Elf_Data *data = obj->efile.reloc[i].data;
		int idx = shdr->sh_info;
		struct bpf_program *prog;

		if (shdr->sh_type != SHT_REL) {
			pr_warning("internal error at %d\n", __LINE__);
			return -LIBBPF_ERRNO__INTERNAL;
		}

		prog = bpf_object__find_prog_by_idx(obj, idx);
		if (!prog) {
			pr_warning("relocation failed: no section(%d)\n", idx);
			return -LIBBPF_ERRNO__RELOC;
		}

		err = bpf_program__collect_reloc(prog, shdr, data, obj);
		if (err)
			return err;
	}
	return 0;
}

static int
load_program(struct bpf_program *prog, struct bpf_insn *insns, int insns_cnt,
	     char *license, __u32 kern_version, int *pfd)
{
	struct bpf_load_program_attr load_attr;
	char *cp, errmsg[STRERR_BUFSIZE];
	int log_buf_size = BPF_LOG_BUF_SIZE;
	char *log_buf;
	int btf_fd, ret;

	if (!insns || !insns_cnt)
		return -EINVAL;

	memset(&load_attr, 0, sizeof(struct bpf_load_program_attr));
	load_attr.prog_type = prog->type;
	load_attr.expected_attach_type = prog->expected_attach_type;
	if (prog->caps->name)
		load_attr.name = prog->name;
	load_attr.insns = insns;
	load_attr.insns_cnt = insns_cnt;
	load_attr.license = license;
	load_attr.kern_version = kern_version;
	load_attr.prog_ifindex = prog->prog_ifindex;
	/* if .BTF.ext was loaded, kernel supports associated BTF for prog */
	if (prog->obj->btf_ext)
		btf_fd = bpf_object__btf_fd(prog->obj);
	else
		btf_fd = -1;
	load_attr.prog_btf_fd = btf_fd >= 0 ? btf_fd : 0;
	load_attr.func_info = prog->func_info;
	load_attr.func_info_rec_size = prog->func_info_rec_size;
	load_attr.func_info_cnt = prog->func_info_cnt;
	load_attr.line_info = prog->line_info;
	load_attr.line_info_rec_size = prog->line_info_rec_size;
	load_attr.line_info_cnt = prog->line_info_cnt;
	load_attr.log_level = prog->log_level;
	load_attr.prog_flags = prog->prog_flags;

retry_load:
	log_buf = malloc(log_buf_size);
	if (!log_buf)
		pr_warning("Alloc log buffer for bpf loader error, continue without log\n");

	ret = bpf_load_program_xattr(&load_attr, log_buf, log_buf_size);

	if (ret >= 0) {
		if (load_attr.log_level)
			pr_debug("verifier log:\n%s", log_buf);
		*pfd = ret;
		ret = 0;
		goto out;
	}

	if (errno == ENOSPC) {
		log_buf_size <<= 1;
		free(log_buf);
		goto retry_load;
	}
	ret = -LIBBPF_ERRNO__LOAD;
	cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
	pr_warning("load bpf program failed: %s\n", cp);

	if (log_buf && log_buf[0] != '\0') {
		ret = -LIBBPF_ERRNO__VERIFY;
		pr_warning("-- BEGIN DUMP LOG ---\n");
		pr_warning("\n%s\n", log_buf);
		pr_warning("-- END LOG --\n");
	} else if (load_attr.insns_cnt >= BPF_MAXINSNS) {
		pr_warning("Program too large (%zu insns), at most %d insns\n",
			   load_attr.insns_cnt, BPF_MAXINSNS);
		ret = -LIBBPF_ERRNO__PROG2BIG;
	} else {
		/* Wrong program type? */
		if (load_attr.prog_type != BPF_PROG_TYPE_KPROBE) {
			int fd;

			load_attr.prog_type = BPF_PROG_TYPE_KPROBE;
			load_attr.expected_attach_type = 0;
			fd = bpf_load_program_xattr(&load_attr, NULL, 0);
			if (fd >= 0) {
				close(fd);
				ret = -LIBBPF_ERRNO__PROGTYPE;
				goto out;
			}
		}

		if (log_buf)
			ret = -LIBBPF_ERRNO__KVER;
	}

out:
	free(log_buf);
	return ret;
}

int
bpf_program__load(struct bpf_program *prog,
		  char *license, __u32 kern_version)
{
	int err = 0, fd, i;

	if (prog->instances.nr < 0 || !prog->instances.fds) {
		if (prog->preprocessor) {
			pr_warning("Internal error: can't load program '%s'\n",
				   prog->section_name);
			return -LIBBPF_ERRNO__INTERNAL;
		}

		prog->instances.fds = malloc(sizeof(int));
		if (!prog->instances.fds) {
			pr_warning("Not enough memory for BPF fds\n");
			return -ENOMEM;
		}
		prog->instances.nr = 1;
		prog->instances.fds[0] = -1;
	}

	if (!prog->preprocessor) {
		if (prog->instances.nr != 1) {
			pr_warning("Program '%s' is inconsistent: nr(%d) != 1\n",
				   prog->section_name, prog->instances.nr);
		}
		err = load_program(prog, prog->insns, prog->insns_cnt,
				   license, kern_version, &fd);
		if (!err)
			prog->instances.fds[0] = fd;
		goto out;
	}

	for (i = 0; i < prog->instances.nr; i++) {
		struct bpf_prog_prep_result result;
		bpf_program_prep_t preprocessor = prog->preprocessor;

		memset(&result, 0, sizeof(result));
		err = preprocessor(prog, i, prog->insns,
				   prog->insns_cnt, &result);
		if (err) {
			pr_warning("Preprocessing the %dth instance of program '%s' failed\n",
				   i, prog->section_name);
			goto out;
		}

		if (!result.new_insn_ptr || !result.new_insn_cnt) {
			pr_debug("Skip loading the %dth instance of program '%s'\n",
				 i, prog->section_name);
			prog->instances.fds[i] = -1;
			if (result.pfd)
				*result.pfd = -1;
			continue;
		}

		err = load_program(prog, result.new_insn_ptr,
				   result.new_insn_cnt,
				   license, kern_version, &fd);

		if (err) {
			pr_warning("Loading the %dth instance of program '%s' failed\n",
					i, prog->section_name);
			goto out;
		}

		if (result.pfd)
			*result.pfd = fd;
		prog->instances.fds[i] = fd;
	}
out:
	if (err)
		pr_warning("failed to load program '%s'\n",
			   prog->section_name);
	zfree(&prog->insns);
	prog->insns_cnt = 0;
	return err;
}

static bool bpf_program__is_function_storage(const struct bpf_program *prog,
					     const struct bpf_object *obj)
{
	return prog->idx == obj->efile.text_shndx && obj->has_pseudo_calls;
}

static int
bpf_object__load_progs(struct bpf_object *obj, int log_level)
{
	size_t i;
	int err;

	for (i = 0; i < obj->nr_programs; i++) {
		if (bpf_program__is_function_storage(&obj->programs[i], obj))
			continue;
		obj->programs[i].log_level |= log_level;
		err = bpf_program__load(&obj->programs[i],
					obj->license,
					obj->kern_version);
		if (err)
			return err;
	}
	return 0;
}

static bool bpf_prog_type__needs_kver(enum bpf_prog_type type)
{
	switch (type) {
	case BPF_PROG_TYPE_SOCKET_FILTER:
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
	case BPF_PROG_TYPE_XDP:
	case BPF_PROG_TYPE_CGROUP_SKB:
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
	case BPF_PROG_TYPE_LWT_XMIT:
	case BPF_PROG_TYPE_LWT_SEG6LOCAL:
	case BPF_PROG_TYPE_SOCK_OPS:
	case BPF_PROG_TYPE_SK_SKB:
	case BPF_PROG_TYPE_CGROUP_DEVICE:
	case BPF_PROG_TYPE_SK_MSG:
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
	case BPF_PROG_TYPE_LIRC_MODE2:
	case BPF_PROG_TYPE_SK_REUSEPORT:
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
	case BPF_PROG_TYPE_UNSPEC:
	case BPF_PROG_TYPE_TRACEPOINT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE:
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
		return false;
	case BPF_PROG_TYPE_KPROBE:
	default:
		return true;
	}
}

static int bpf_object__validate(struct bpf_object *obj, bool needs_kver)
{
	if (needs_kver && obj->kern_version == 0) {
		pr_warning("%s doesn't provide kernel version\n",
			   obj->path);
		return -LIBBPF_ERRNO__KVERSION;
	}
	return 0;
}

static struct bpf_object *
__bpf_object__open(const char *path, void *obj_buf, size_t obj_buf_sz,
		   bool needs_kver, int flags)
{
	struct bpf_object *obj;
	int err;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warning("failed to init libelf for %s\n", path);
		return ERR_PTR(-LIBBPF_ERRNO__LIBELF);
	}

	obj = bpf_object__new(path, obj_buf, obj_buf_sz);
	if (IS_ERR(obj))
		return obj;

	CHECK_ERR(bpf_object__elf_init(obj), err, out);
	CHECK_ERR(bpf_object__check_endianness(obj), err, out);
	CHECK_ERR(bpf_object__probe_caps(obj), err, out);
	CHECK_ERR(bpf_object__elf_collect(obj, flags), err, out);
	CHECK_ERR(bpf_object__collect_reloc(obj), err, out);
	CHECK_ERR(bpf_object__validate(obj, needs_kver), err, out);

	bpf_object__elf_finish(obj);
	return obj;
out:
	bpf_object__close(obj);
	return ERR_PTR(err);
}

struct bpf_object *__bpf_object__open_xattr(struct bpf_object_open_attr *attr,
					    int flags)
{
	/* param validation */
	if (!attr->file)
		return NULL;

	pr_debug("loading %s\n", attr->file);

	return __bpf_object__open(attr->file, NULL, 0,
				  bpf_prog_type__needs_kver(attr->prog_type),
				  flags);
}

struct bpf_object *bpf_object__open_xattr(struct bpf_object_open_attr *attr)
{
	return __bpf_object__open_xattr(attr, 0);
}

struct bpf_object *bpf_object__open(const char *path)
{
	struct bpf_object_open_attr attr = {
		.file		= path,
		.prog_type	= BPF_PROG_TYPE_UNSPEC,
	};

	return bpf_object__open_xattr(&attr);
}

struct bpf_object *bpf_object__open_buffer(void *obj_buf,
					   size_t obj_buf_sz,
					   const char *name)
{
	char tmp_name[64];

	/* param validation */
	if (!obj_buf || obj_buf_sz <= 0)
		return NULL;

	if (!name) {
		snprintf(tmp_name, sizeof(tmp_name), "%lx-%lx",
			 (unsigned long)obj_buf,
			 (unsigned long)obj_buf_sz);
		name = tmp_name;
	}
	pr_debug("loading object '%s' from buffer\n", name);

	return __bpf_object__open(name, obj_buf, obj_buf_sz, true, true);
}

int bpf_object__unload(struct bpf_object *obj)
{
	size_t i;

	if (!obj)
		return -EINVAL;

	for (i = 0; i < obj->nr_maps; i++)
		zclose(obj->maps[i].fd);

	for (i = 0; i < obj->nr_programs; i++)
		bpf_program__unload(&obj->programs[i]);

	return 0;
}

int bpf_object__load_xattr(struct bpf_object_load_attr *attr)
{
	struct bpf_object *obj;
	int err;

	if (!attr)
		return -EINVAL;
	obj = attr->obj;
	if (!obj)
		return -EINVAL;

	if (obj->loaded) {
		pr_warning("object should not be loaded twice\n");
		return -EINVAL;
	}

	obj->loaded = true;

	CHECK_ERR(bpf_object__create_maps(obj), err, out);
	CHECK_ERR(bpf_object__relocate(obj), err, out);
	CHECK_ERR(bpf_object__load_progs(obj, attr->log_level), err, out);

	return 0;
out:
	bpf_object__unload(obj);
	pr_warning("failed to load object '%s'\n", obj->path);
	return err;
}

int bpf_object__load(struct bpf_object *obj)
{
	struct bpf_object_load_attr attr = {
		.obj = obj,
	};

	return bpf_object__load_xattr(&attr);
}

static int check_path(const char *path)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	struct statfs st_fs;
	char *dname, *dir;
	int err = 0;

	if (path == NULL)
		return -EINVAL;

	dname = strdup(path);
	if (dname == NULL)
		return -ENOMEM;

	dir = dirname(dname);
	if (statfs(dir, &st_fs)) {
		cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
		pr_warning("failed to statfs %s: %s\n", dir, cp);
		err = -errno;
	}
	free(dname);

	if (!err && st_fs.f_type != BPF_FS_MAGIC) {
		pr_warning("specified path %s is not on BPF FS\n", path);
		err = -EINVAL;
	}

	return err;
}

int bpf_program__pin_instance(struct bpf_program *prog, const char *path,
			      int instance)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err;

	err = check_path(path);
	if (err)
		return err;

	if (prog == NULL) {
		pr_warning("invalid program pointer\n");
		return -EINVAL;
	}

	if (instance < 0 || instance >= prog->instances.nr) {
		pr_warning("invalid prog instance %d of prog %s (max %d)\n",
			   instance, prog->section_name, prog->instances.nr);
		return -EINVAL;
	}

	if (bpf_obj_pin(prog->instances.fds[instance], path)) {
		cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
		pr_warning("failed to pin program: %s\n", cp);
		return -errno;
	}
	pr_debug("pinned program '%s'\n", path);

	return 0;
}

int bpf_program__unpin_instance(struct bpf_program *prog, const char *path,
				int instance)
{
	int err;

	err = check_path(path);
	if (err)
		return err;

	if (prog == NULL) {
		pr_warning("invalid program pointer\n");
		return -EINVAL;
	}

	if (instance < 0 || instance >= prog->instances.nr) {
		pr_warning("invalid prog instance %d of prog %s (max %d)\n",
			   instance, prog->section_name, prog->instances.nr);
		return -EINVAL;
	}

	err = unlink(path);
	if (err != 0)
		return -errno;
	pr_debug("unpinned program '%s'\n", path);

	return 0;
}

static int make_dir(const char *path)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err = 0;

	if (mkdir(path, 0700) && errno != EEXIST)
		err = -errno;

	if (err) {
		cp = libbpf_strerror_r(-err, errmsg, sizeof(errmsg));
		pr_warning("failed to mkdir %s: %s\n", path, cp);
	}
	return err;
}

int bpf_program__pin(struct bpf_program *prog, const char *path)
{
	int i, err;

	err = check_path(path);
	if (err)
		return err;

	if (prog == NULL) {
		pr_warning("invalid program pointer\n");
		return -EINVAL;
	}

	if (prog->instances.nr <= 0) {
		pr_warning("no instances of prog %s to pin\n",
			   prog->section_name);
		return -EINVAL;
	}

	if (prog->instances.nr == 1) {
		/* don't create subdirs when pinning single instance */
		return bpf_program__pin_instance(prog, path, 0);
	}

	err = make_dir(path);
	if (err)
		return err;

	for (i = 0; i < prog->instances.nr; i++) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%d", path, i);
		if (len < 0) {
			err = -EINVAL;
			goto err_unpin;
		} else if (len >= PATH_MAX) {
			err = -ENAMETOOLONG;
			goto err_unpin;
		}

		err = bpf_program__pin_instance(prog, buf, i);
		if (err)
			goto err_unpin;
	}

	return 0;

err_unpin:
	for (i = i - 1; i >= 0; i--) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%d", path, i);
		if (len < 0)
			continue;
		else if (len >= PATH_MAX)
			continue;

		bpf_program__unpin_instance(prog, buf, i);
	}

	rmdir(path);

	return err;
}

int bpf_program__unpin(struct bpf_program *prog, const char *path)
{
	int i, err;

	err = check_path(path);
	if (err)
		return err;

	if (prog == NULL) {
		pr_warning("invalid program pointer\n");
		return -EINVAL;
	}

	if (prog->instances.nr <= 0) {
		pr_warning("no instances of prog %s to pin\n",
			   prog->section_name);
		return -EINVAL;
	}

	if (prog->instances.nr == 1) {
		/* don't create subdirs when pinning single instance */
		return bpf_program__unpin_instance(prog, path, 0);
	}

	for (i = 0; i < prog->instances.nr; i++) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%d", path, i);
		if (len < 0)
			return -EINVAL;
		else if (len >= PATH_MAX)
			return -ENAMETOOLONG;

		err = bpf_program__unpin_instance(prog, buf, i);
		if (err)
			return err;
	}

	err = rmdir(path);
	if (err)
		return -errno;

	return 0;
}

int bpf_map__pin(struct bpf_map *map, const char *path)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err;

	err = check_path(path);
	if (err)
		return err;

	if (map == NULL) {
		pr_warning("invalid map pointer\n");
		return -EINVAL;
	}

	if (bpf_obj_pin(map->fd, path)) {
		cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
		pr_warning("failed to pin map: %s\n", cp);
		return -errno;
	}

	pr_debug("pinned map '%s'\n", path);

	return 0;
}

int bpf_map__unpin(struct bpf_map *map, const char *path)
{
	int err;

	err = check_path(path);
	if (err)
		return err;

	if (map == NULL) {
		pr_warning("invalid map pointer\n");
		return -EINVAL;
	}

	err = unlink(path);
	if (err != 0)
		return -errno;
	pr_debug("unpinned map '%s'\n", path);

	return 0;
}

int bpf_object__pin_maps(struct bpf_object *obj, const char *path)
{
	struct bpf_map *map;
	int err;

	if (!obj)
		return -ENOENT;

	if (!obj->loaded) {
		pr_warning("object not yet loaded; load it first\n");
		return -ENOENT;
	}

	err = make_dir(path);
	if (err)
		return err;

	bpf_object__for_each_map(map, obj) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%s", path,
			       bpf_map__name(map));
		if (len < 0) {
			err = -EINVAL;
			goto err_unpin_maps;
		} else if (len >= PATH_MAX) {
			err = -ENAMETOOLONG;
			goto err_unpin_maps;
		}

		err = bpf_map__pin(map, buf);
		if (err)
			goto err_unpin_maps;
	}

	return 0;

err_unpin_maps:
	while ((map = bpf_map__prev(map, obj))) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%s", path,
			       bpf_map__name(map));
		if (len < 0)
			continue;
		else if (len >= PATH_MAX)
			continue;

		bpf_map__unpin(map, buf);
	}

	return err;
}

int bpf_object__unpin_maps(struct bpf_object *obj, const char *path)
{
	struct bpf_map *map;
	int err;

	if (!obj)
		return -ENOENT;

	bpf_object__for_each_map(map, obj) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%s", path,
			       bpf_map__name(map));
		if (len < 0)
			return -EINVAL;
		else if (len >= PATH_MAX)
			return -ENAMETOOLONG;

		err = bpf_map__unpin(map, buf);
		if (err)
			return err;
	}

	return 0;
}

int bpf_object__pin_programs(struct bpf_object *obj, const char *path)
{
	struct bpf_program *prog;
	int err;

	if (!obj)
		return -ENOENT;

	if (!obj->loaded) {
		pr_warning("object not yet loaded; load it first\n");
		return -ENOENT;
	}

	err = make_dir(path);
	if (err)
		return err;

	bpf_object__for_each_program(prog, obj) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%s", path,
			       prog->pin_name);
		if (len < 0) {
			err = -EINVAL;
			goto err_unpin_programs;
		} else if (len >= PATH_MAX) {
			err = -ENAMETOOLONG;
			goto err_unpin_programs;
		}

		err = bpf_program__pin(prog, buf);
		if (err)
			goto err_unpin_programs;
	}

	return 0;

err_unpin_programs:
	while ((prog = bpf_program__prev(prog, obj))) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%s", path,
			       prog->pin_name);
		if (len < 0)
			continue;
		else if (len >= PATH_MAX)
			continue;

		bpf_program__unpin(prog, buf);
	}

	return err;
}

int bpf_object__unpin_programs(struct bpf_object *obj, const char *path)
{
	struct bpf_program *prog;
	int err;

	if (!obj)
		return -ENOENT;

	bpf_object__for_each_program(prog, obj) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%s", path,
			       prog->pin_name);
		if (len < 0)
			return -EINVAL;
		else if (len >= PATH_MAX)
			return -ENAMETOOLONG;

		err = bpf_program__unpin(prog, buf);
		if (err)
			return err;
	}

	return 0;
}

int bpf_object__pin(struct bpf_object *obj, const char *path)
{
	int err;

	err = bpf_object__pin_maps(obj, path);
	if (err)
		return err;

	err = bpf_object__pin_programs(obj, path);
	if (err) {
		bpf_object__unpin_maps(obj, path);
		return err;
	}

	return 0;
}

void bpf_object__close(struct bpf_object *obj)
{
	size_t i;

	if (!obj)
		return;

	if (obj->clear_priv)
		obj->clear_priv(obj, obj->priv);

	bpf_object__elf_finish(obj);
	bpf_object__unload(obj);
	btf__free(obj->btf);
	btf_ext__free(obj->btf_ext);

	for (i = 0; i < obj->nr_maps; i++) {
		zfree(&obj->maps[i].name);
		if (obj->maps[i].clear_priv)
			obj->maps[i].clear_priv(&obj->maps[i],
						obj->maps[i].priv);
		obj->maps[i].priv = NULL;
		obj->maps[i].clear_priv = NULL;
	}

	zfree(&obj->sections.rodata);
	zfree(&obj->sections.data);
	zfree(&obj->maps);
	obj->nr_maps = 0;

	if (obj->programs && obj->nr_programs) {
		for (i = 0; i < obj->nr_programs; i++)
			bpf_program__exit(&obj->programs[i]);
	}
	zfree(&obj->programs);

	list_del(&obj->list);
	free(obj);
}

struct bpf_object *
bpf_object__next(struct bpf_object *prev)
{
	struct bpf_object *next;

	if (!prev)
		next = list_first_entry(&bpf_objects_list,
					struct bpf_object,
					list);
	else
		next = list_next_entry(prev, list);

	/* Empty list is noticed here so don't need checking on entry. */
	if (&next->list == &bpf_objects_list)
		return NULL;

	return next;
}

const char *bpf_object__name(const struct bpf_object *obj)
{
	return obj ? obj->path : ERR_PTR(-EINVAL);
}

unsigned int bpf_object__kversion(const struct bpf_object *obj)
{
	return obj ? obj->kern_version : 0;
}

struct btf *bpf_object__btf(const struct bpf_object *obj)
{
	return obj ? obj->btf : NULL;
}

int bpf_object__btf_fd(const struct bpf_object *obj)
{
	return obj->btf ? btf__fd(obj->btf) : -1;
}

int bpf_object__set_priv(struct bpf_object *obj, void *priv,
			 bpf_object_clear_priv_t clear_priv)
{
	if (obj->priv && obj->clear_priv)
		obj->clear_priv(obj, obj->priv);

	obj->priv = priv;
	obj->clear_priv = clear_priv;
	return 0;
}

void *bpf_object__priv(const struct bpf_object *obj)
{
	return obj ? obj->priv : ERR_PTR(-EINVAL);
}

static struct bpf_program *
__bpf_program__iter(const struct bpf_program *p, const struct bpf_object *obj,
		    bool forward)
{
	size_t nr_programs = obj->nr_programs;
	ssize_t idx;

	if (!nr_programs)
		return NULL;

	if (!p)
		/* Iter from the beginning */
		return forward ? &obj->programs[0] :
			&obj->programs[nr_programs - 1];

	if (p->obj != obj) {
		pr_warning("error: program handler doesn't match object\n");
		return NULL;
	}

	idx = (p - obj->programs) + (forward ? 1 : -1);
	if (idx >= obj->nr_programs || idx < 0)
		return NULL;
	return &obj->programs[idx];
}

struct bpf_program *
bpf_program__next(struct bpf_program *prev, const struct bpf_object *obj)
{
	struct bpf_program *prog = prev;

	do {
		prog = __bpf_program__iter(prog, obj, true);
	} while (prog && bpf_program__is_function_storage(prog, obj));

	return prog;
}

struct bpf_program *
bpf_program__prev(struct bpf_program *next, const struct bpf_object *obj)
{
	struct bpf_program *prog = next;

	do {
		prog = __bpf_program__iter(prog, obj, false);
	} while (prog && bpf_program__is_function_storage(prog, obj));

	return prog;
}

int bpf_program__set_priv(struct bpf_program *prog, void *priv,
			  bpf_program_clear_priv_t clear_priv)
{
	if (prog->priv && prog->clear_priv)
		prog->clear_priv(prog, prog->priv);

	prog->priv = priv;
	prog->clear_priv = clear_priv;
	return 0;
}

void *bpf_program__priv(const struct bpf_program *prog)
{
	return prog ? prog->priv : ERR_PTR(-EINVAL);
}

void bpf_program__set_ifindex(struct bpf_program *prog, __u32 ifindex)
{
	prog->prog_ifindex = ifindex;
}

const char *bpf_program__title(const struct bpf_program *prog, bool needs_copy)
{
	const char *title;

	title = prog->section_name;
	if (needs_copy) {
		title = strdup(title);
		if (!title) {
			pr_warning("failed to strdup program title\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	return title;
}

int bpf_program__fd(const struct bpf_program *prog)
{
	return bpf_program__nth_fd(prog, 0);
}

int bpf_program__set_prep(struct bpf_program *prog, int nr_instances,
			  bpf_program_prep_t prep)
{
	int *instances_fds;

	if (nr_instances <= 0 || !prep)
		return -EINVAL;

	if (prog->instances.nr > 0 || prog->instances.fds) {
		pr_warning("Can't set pre-processor after loading\n");
		return -EINVAL;
	}

	instances_fds = malloc(sizeof(int) * nr_instances);
	if (!instances_fds) {
		pr_warning("alloc memory failed for fds\n");
		return -ENOMEM;
	}

	/* fill all fd with -1 */
	memset(instances_fds, -1, sizeof(int) * nr_instances);

	prog->instances.nr = nr_instances;
	prog->instances.fds = instances_fds;
	prog->preprocessor = prep;
	return 0;
}

int bpf_program__nth_fd(const struct bpf_program *prog, int n)
{
	int fd;

	if (!prog)
		return -EINVAL;

	if (n >= prog->instances.nr || n < 0) {
		pr_warning("Can't get the %dth fd from program %s: only %d instances\n",
			   n, prog->section_name, prog->instances.nr);
		return -EINVAL;
	}

	fd = prog->instances.fds[n];
	if (fd < 0) {
		pr_warning("%dth instance of program '%s' is invalid\n",
			   n, prog->section_name);
		return -ENOENT;
	}

	return fd;
}

void bpf_program__set_type(struct bpf_program *prog, enum bpf_prog_type type)
{
	prog->type = type;
}

static bool bpf_program__is_type(const struct bpf_program *prog,
				 enum bpf_prog_type type)
{
	return prog ? (prog->type == type) : false;
}

#define BPF_PROG_TYPE_FNS(NAME, TYPE)				\
int bpf_program__set_##NAME(struct bpf_program *prog)		\
{								\
	if (!prog)						\
		return -EINVAL;					\
	bpf_program__set_type(prog, TYPE);			\
	return 0;						\
}								\
								\
bool bpf_program__is_##NAME(const struct bpf_program *prog)	\
{								\
	return bpf_program__is_type(prog, TYPE);		\
}								\

BPF_PROG_TYPE_FNS(socket_filter, BPF_PROG_TYPE_SOCKET_FILTER);
BPF_PROG_TYPE_FNS(kprobe, BPF_PROG_TYPE_KPROBE);
BPF_PROG_TYPE_FNS(sched_cls, BPF_PROG_TYPE_SCHED_CLS);
BPF_PROG_TYPE_FNS(sched_act, BPF_PROG_TYPE_SCHED_ACT);
BPF_PROG_TYPE_FNS(tracepoint, BPF_PROG_TYPE_TRACEPOINT);
BPF_PROG_TYPE_FNS(raw_tracepoint, BPF_PROG_TYPE_RAW_TRACEPOINT);
BPF_PROG_TYPE_FNS(xdp, BPF_PROG_TYPE_XDP);
BPF_PROG_TYPE_FNS(perf_event, BPF_PROG_TYPE_PERF_EVENT);

void bpf_program__set_expected_attach_type(struct bpf_program *prog,
					   enum bpf_attach_type type)
{
	prog->expected_attach_type = type;
}

#define BPF_PROG_SEC_IMPL(string, ptype, eatype, is_attachable, atype) \
	{ string, sizeof(string) - 1, ptype, eatype, is_attachable, atype }

/* Programs that can NOT be attached. */
#define BPF_PROG_SEC(string, ptype) BPF_PROG_SEC_IMPL(string, ptype, 0, 0, 0)

/* Programs that can be attached. */
#define BPF_APROG_SEC(string, ptype, atype) \
	BPF_PROG_SEC_IMPL(string, ptype, 0, 1, atype)

/* Programs that must specify expected attach type at load time. */
#define BPF_EAPROG_SEC(string, ptype, eatype) \
	BPF_PROG_SEC_IMPL(string, ptype, eatype, 1, eatype)

/* Programs that can be attached but attach type can't be identified by section
 * name. Kept for backward compatibility.
 */
#define BPF_APROG_COMPAT(string, ptype) BPF_PROG_SEC(string, ptype)

static const struct {
	const char *sec;
	size_t len;
	enum bpf_prog_type prog_type;
	enum bpf_attach_type expected_attach_type;
	int is_attachable;
	enum bpf_attach_type attach_type;
} section_names[] = {
	BPF_PROG_SEC("socket",			BPF_PROG_TYPE_SOCKET_FILTER),
	BPF_PROG_SEC("kprobe/",			BPF_PROG_TYPE_KPROBE),
	BPF_PROG_SEC("kretprobe/",		BPF_PROG_TYPE_KPROBE),
	BPF_PROG_SEC("classifier",		BPF_PROG_TYPE_SCHED_CLS),
	BPF_PROG_SEC("action",			BPF_PROG_TYPE_SCHED_ACT),
	BPF_PROG_SEC("tracepoint/",		BPF_PROG_TYPE_TRACEPOINT),
	BPF_PROG_SEC("raw_tracepoint/",		BPF_PROG_TYPE_RAW_TRACEPOINT),
	BPF_PROG_SEC("xdp",			BPF_PROG_TYPE_XDP),
	BPF_PROG_SEC("perf_event",		BPF_PROG_TYPE_PERF_EVENT),
	BPF_PROG_SEC("lwt_in",			BPF_PROG_TYPE_LWT_IN),
	BPF_PROG_SEC("lwt_out",			BPF_PROG_TYPE_LWT_OUT),
	BPF_PROG_SEC("lwt_xmit",		BPF_PROG_TYPE_LWT_XMIT),
	BPF_PROG_SEC("lwt_seg6local",		BPF_PROG_TYPE_LWT_SEG6LOCAL),
	BPF_APROG_SEC("cgroup_skb/ingress",	BPF_PROG_TYPE_CGROUP_SKB,
						BPF_CGROUP_INET_INGRESS),
	BPF_APROG_SEC("cgroup_skb/egress",	BPF_PROG_TYPE_CGROUP_SKB,
						BPF_CGROUP_INET_EGRESS),
	BPF_APROG_COMPAT("cgroup/skb",		BPF_PROG_TYPE_CGROUP_SKB),
	BPF_APROG_SEC("cgroup/sock",		BPF_PROG_TYPE_CGROUP_SOCK,
						BPF_CGROUP_INET_SOCK_CREATE),
	BPF_EAPROG_SEC("cgroup/post_bind4",	BPF_PROG_TYPE_CGROUP_SOCK,
						BPF_CGROUP_INET4_POST_BIND),
	BPF_EAPROG_SEC("cgroup/post_bind6",	BPF_PROG_TYPE_CGROUP_SOCK,
						BPF_CGROUP_INET6_POST_BIND),
	BPF_APROG_SEC("cgroup/dev",		BPF_PROG_TYPE_CGROUP_DEVICE,
						BPF_CGROUP_DEVICE),
	BPF_APROG_SEC("sockops",		BPF_PROG_TYPE_SOCK_OPS,
						BPF_CGROUP_SOCK_OPS),
	BPF_APROG_SEC("sk_skb/stream_parser",	BPF_PROG_TYPE_SK_SKB,
						BPF_SK_SKB_STREAM_PARSER),
	BPF_APROG_SEC("sk_skb/stream_verdict",	BPF_PROG_TYPE_SK_SKB,
						BPF_SK_SKB_STREAM_VERDICT),
	BPF_APROG_COMPAT("sk_skb",		BPF_PROG_TYPE_SK_SKB),
	BPF_APROG_SEC("sk_msg",			BPF_PROG_TYPE_SK_MSG,
						BPF_SK_MSG_VERDICT),
	BPF_APROG_SEC("lirc_mode2",		BPF_PROG_TYPE_LIRC_MODE2,
						BPF_LIRC_MODE2),
	BPF_APROG_SEC("flow_dissector",		BPF_PROG_TYPE_FLOW_DISSECTOR,
						BPF_FLOW_DISSECTOR),
	BPF_EAPROG_SEC("cgroup/bind4",		BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET4_BIND),
	BPF_EAPROG_SEC("cgroup/bind6",		BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET6_BIND),
	BPF_EAPROG_SEC("cgroup/connect4",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET4_CONNECT),
	BPF_EAPROG_SEC("cgroup/connect6",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET6_CONNECT),
	BPF_EAPROG_SEC("cgroup/sendmsg4",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_UDP4_SENDMSG),
	BPF_EAPROG_SEC("cgroup/sendmsg6",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_UDP6_SENDMSG),
	BPF_EAPROG_SEC("cgroup/recvmsg4",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_UDP4_RECVMSG),
	BPF_EAPROG_SEC("cgroup/recvmsg6",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_UDP6_RECVMSG),
	BPF_EAPROG_SEC("cgroup/sysctl",		BPF_PROG_TYPE_CGROUP_SYSCTL,
						BPF_CGROUP_SYSCTL),
	BPF_EAPROG_SEC("cgroup/getsockopt",	BPF_PROG_TYPE_CGROUP_SOCKOPT,
						BPF_CGROUP_GETSOCKOPT),
	BPF_EAPROG_SEC("cgroup/setsockopt",	BPF_PROG_TYPE_CGROUP_SOCKOPT,
						BPF_CGROUP_SETSOCKOPT),
};

#undef BPF_PROG_SEC_IMPL
#undef BPF_PROG_SEC
#undef BPF_APROG_SEC
#undef BPF_EAPROG_SEC
#undef BPF_APROG_COMPAT

#define MAX_TYPE_NAME_SIZE 32

static char *libbpf_get_type_names(bool attach_type)
{
	int i, len = ARRAY_SIZE(section_names) * MAX_TYPE_NAME_SIZE;
	char *buf;

	buf = malloc(len);
	if (!buf)
		return NULL;

	buf[0] = '\0';
	/* Forge string buf with all available names */
	for (i = 0; i < ARRAY_SIZE(section_names); i++) {
		if (attach_type && !section_names[i].is_attachable)
			continue;

		if (strlen(buf) + strlen(section_names[i].sec) + 2 > len) {
			free(buf);
			return NULL;
		}
		strcat(buf, " ");
		strcat(buf, section_names[i].sec);
	}

	return buf;
}

int libbpf_prog_type_by_name(const char *name, enum bpf_prog_type *prog_type,
			     enum bpf_attach_type *expected_attach_type)
{
	char *type_names;
	int i;

	if (!name)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(section_names); i++) {
		if (strncmp(name, section_names[i].sec, section_names[i].len))
			continue;
		*prog_type = section_names[i].prog_type;
		*expected_attach_type = section_names[i].expected_attach_type;
		return 0;
	}
	pr_warning("failed to guess program type based on ELF section name '%s'\n", name);
	type_names = libbpf_get_type_names(false);
	if (type_names != NULL) {
		pr_info("supported section(type) names are:%s\n", type_names);
		free(type_names);
	}

	return -EINVAL;
}

int libbpf_attach_type_by_name(const char *name,
			       enum bpf_attach_type *attach_type)
{
	char *type_names;
	int i;

	if (!name)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(section_names); i++) {
		if (strncmp(name, section_names[i].sec, section_names[i].len))
			continue;
		if (!section_names[i].is_attachable)
			return -EINVAL;
		*attach_type = section_names[i].attach_type;
		return 0;
	}
	pr_warning("failed to guess attach type based on ELF section name '%s'\n", name);
	type_names = libbpf_get_type_names(true);
	if (type_names != NULL) {
		pr_info("attachable section(type) names are:%s\n", type_names);
		free(type_names);
	}

	return -EINVAL;
}

static int
bpf_program__identify_section(struct bpf_program *prog,
			      enum bpf_prog_type *prog_type,
			      enum bpf_attach_type *expected_attach_type)
{
	return libbpf_prog_type_by_name(prog->section_name, prog_type,
					expected_attach_type);
}

int bpf_map__fd(const struct bpf_map *map)
{
	return map ? map->fd : -EINVAL;
}

const struct bpf_map_def *bpf_map__def(const struct bpf_map *map)
{
	return map ? &map->def : ERR_PTR(-EINVAL);
}

const char *bpf_map__name(const struct bpf_map *map)
{
	return map ? map->name : NULL;
}

__u32 bpf_map__btf_key_type_id(const struct bpf_map *map)
{
	return map ? map->btf_key_type_id : 0;
}

__u32 bpf_map__btf_value_type_id(const struct bpf_map *map)
{
	return map ? map->btf_value_type_id : 0;
}

int bpf_map__set_priv(struct bpf_map *map, void *priv,
		     bpf_map_clear_priv_t clear_priv)
{
	if (!map)
		return -EINVAL;

	if (map->priv) {
		if (map->clear_priv)
			map->clear_priv(map, map->priv);
	}

	map->priv = priv;
	map->clear_priv = clear_priv;
	return 0;
}

void *bpf_map__priv(const struct bpf_map *map)
{
	return map ? map->priv : ERR_PTR(-EINVAL);
}

bool bpf_map__is_offload_neutral(const struct bpf_map *map)
{
	return map->def.type == BPF_MAP_TYPE_PERF_EVENT_ARRAY;
}

bool bpf_map__is_internal(const struct bpf_map *map)
{
	return map->libbpf_type != LIBBPF_MAP_UNSPEC;
}

void bpf_map__set_ifindex(struct bpf_map *map, __u32 ifindex)
{
	map->map_ifindex = ifindex;
}

int bpf_map__set_inner_map_fd(struct bpf_map *map, int fd)
{
	if (!bpf_map_type__is_map_in_map(map->def.type)) {
		pr_warning("error: unsupported map type\n");
		return -EINVAL;
	}
	if (map->inner_map_fd != -1) {
		pr_warning("error: inner_map_fd already specified\n");
		return -EINVAL;
	}
	map->inner_map_fd = fd;
	return 0;
}

static struct bpf_map *
__bpf_map__iter(const struct bpf_map *m, const struct bpf_object *obj, int i)
{
	ssize_t idx;
	struct bpf_map *s, *e;

	if (!obj || !obj->maps)
		return NULL;

	s = obj->maps;
	e = obj->maps + obj->nr_maps;

	if ((m < s) || (m >= e)) {
		pr_warning("error in %s: map handler doesn't belong to object\n",
			   __func__);
		return NULL;
	}

	idx = (m - obj->maps) + i;
	if (idx >= obj->nr_maps || idx < 0)
		return NULL;
	return &obj->maps[idx];
}

struct bpf_map *
bpf_map__next(const struct bpf_map *prev, const struct bpf_object *obj)
{
	if (prev == NULL)
		return obj->maps;

	return __bpf_map__iter(prev, obj, 1);
}

struct bpf_map *
bpf_map__prev(const struct bpf_map *next, const struct bpf_object *obj)
{
	if (next == NULL) {
		if (!obj->nr_maps)
			return NULL;
		return obj->maps + obj->nr_maps - 1;
	}

	return __bpf_map__iter(next, obj, -1);
}

struct bpf_map *
bpf_object__find_map_by_name(const struct bpf_object *obj, const char *name)
{
	struct bpf_map *pos;

	bpf_object__for_each_map(pos, obj) {
		if (pos->name && !strcmp(pos->name, name))
			return pos;
	}
	return NULL;
}

int
bpf_object__find_map_fd_by_name(const struct bpf_object *obj, const char *name)
{
	return bpf_map__fd(bpf_object__find_map_by_name(obj, name));
}

struct bpf_map *
bpf_object__find_map_by_offset(struct bpf_object *obj, size_t offset)
{
	return ERR_PTR(-ENOTSUP);
}

long libbpf_get_error(const void *ptr)
{
	return PTR_ERR_OR_ZERO(ptr);
}

int bpf_prog_load(const char *file, enum bpf_prog_type type,
		  struct bpf_object **pobj, int *prog_fd)
{
	struct bpf_prog_load_attr attr;

	memset(&attr, 0, sizeof(struct bpf_prog_load_attr));
	attr.file = file;
	attr.prog_type = type;
	attr.expected_attach_type = 0;

	return bpf_prog_load_xattr(&attr, pobj, prog_fd);
}

int bpf_prog_load_xattr(const struct bpf_prog_load_attr *attr,
			struct bpf_object **pobj, int *prog_fd)
{
	struct bpf_object_open_attr open_attr = {};
	struct bpf_program *prog, *first_prog = NULL;
	enum bpf_attach_type expected_attach_type;
	enum bpf_prog_type prog_type;
	struct bpf_object *obj;
	struct bpf_map *map;
	int err;

	if (!attr)
		return -EINVAL;
	if (!attr->file)
		return -EINVAL;

	open_attr.file = attr->file;
	open_attr.prog_type = attr->prog_type;

	obj = bpf_object__open_xattr(&open_attr);
	if (IS_ERR_OR_NULL(obj))
		return -ENOENT;

	bpf_object__for_each_program(prog, obj) {
		/*
		 * If type is not specified, try to guess it based on
		 * section name.
		 */
		prog_type = attr->prog_type;
		prog->prog_ifindex = attr->ifindex;
		expected_attach_type = attr->expected_attach_type;
		if (prog_type == BPF_PROG_TYPE_UNSPEC) {
			err = bpf_program__identify_section(prog, &prog_type,
							    &expected_attach_type);
			if (err < 0) {
				bpf_object__close(obj);
				return -EINVAL;
			}
		}

		bpf_program__set_type(prog, prog_type);
		bpf_program__set_expected_attach_type(prog,
						      expected_attach_type);

		prog->log_level = attr->log_level;
		prog->prog_flags = attr->prog_flags;
		if (!first_prog)
			first_prog = prog;
	}

	bpf_object__for_each_map(map, obj) {
		if (!bpf_map__is_offload_neutral(map))
			map->map_ifindex = attr->ifindex;
	}

	if (!first_prog) {
		pr_warning("object file doesn't contain bpf program\n");
		bpf_object__close(obj);
		return -ENOENT;
	}

	err = bpf_object__load(obj);
	if (err) {
		bpf_object__close(obj);
		return -EINVAL;
	}

	*pobj = obj;
	*prog_fd = bpf_program__fd(first_prog);
	return 0;
}

struct bpf_link {
	int (*destroy)(struct bpf_link *link);
};

int bpf_link__destroy(struct bpf_link *link)
{
	int err;

	if (!link)
		return 0;

	err = link->destroy(link);
	free(link);

	return err;
}

struct bpf_link_fd {
	struct bpf_link link; /* has to be at the top of struct */
	int fd; /* hook FD */
};

static int bpf_link__destroy_perf_event(struct bpf_link *link)
{
	struct bpf_link_fd *l = (void *)link;
	int err;

	err = ioctl(l->fd, PERF_EVENT_IOC_DISABLE, 0);
	if (err)
		err = -errno;

	close(l->fd);
	return err;
}

struct bpf_link *bpf_program__attach_perf_event(struct bpf_program *prog,
						int pfd)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link_fd *link;
	int prog_fd, err;

	if (pfd < 0) {
		pr_warning("program '%s': invalid perf event FD %d\n",
			   bpf_program__title(prog, false), pfd);
		return ERR_PTR(-EINVAL);
	}
	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warning("program '%s': can't attach BPF program w/o FD (did you load it?)\n",
			   bpf_program__title(prog, false));
		return ERR_PTR(-EINVAL);
	}

	link = malloc(sizeof(*link));
	if (!link)
		return ERR_PTR(-ENOMEM);
	link->link.destroy = &bpf_link__destroy_perf_event;
	link->fd = pfd;

	if (ioctl(pfd, PERF_EVENT_IOC_SET_BPF, prog_fd) < 0) {
		err = -errno;
		free(link);
		pr_warning("program '%s': failed to attach to pfd %d: %s\n",
			   bpf_program__title(prog, false), pfd,
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return ERR_PTR(err);
	}
	if (ioctl(pfd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
		err = -errno;
		free(link);
		pr_warning("program '%s': failed to enable pfd %d: %s\n",
			   bpf_program__title(prog, false), pfd,
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return ERR_PTR(err);
	}
	return (struct bpf_link *)link;
}

/*
 * this function is expected to parse integer in the range of [0, 2^31-1] from
 * given file using scanf format string fmt. If actual parsed value is
 * negative, the result might be indistinguishable from error
 */
static int parse_uint_from_file(const char *file, const char *fmt)
{
	char buf[STRERR_BUFSIZE];
	int err, ret;
	FILE *f;

	f = fopen(file, "r");
	if (!f) {
		err = -errno;
		pr_debug("failed to open '%s': %s\n", file,
			 libbpf_strerror_r(err, buf, sizeof(buf)));
		return err;
	}
	err = fscanf(f, fmt, &ret);
	if (err != 1) {
		err = err == EOF ? -EIO : -errno;
		pr_debug("failed to parse '%s': %s\n", file,
			libbpf_strerror_r(err, buf, sizeof(buf)));
		fclose(f);
		return err;
	}
	fclose(f);
	return ret;
}

static int determine_kprobe_perf_type(void)
{
	const char *file = "/sys/bus/event_source/devices/kprobe/type";

	return parse_uint_from_file(file, "%d\n");
}

static int determine_uprobe_perf_type(void)
{
	const char *file = "/sys/bus/event_source/devices/uprobe/type";

	return parse_uint_from_file(file, "%d\n");
}

static int determine_kprobe_retprobe_bit(void)
{
	const char *file = "/sys/bus/event_source/devices/kprobe/format/retprobe";

	return parse_uint_from_file(file, "config:%d\n");
}

static int determine_uprobe_retprobe_bit(void)
{
	const char *file = "/sys/bus/event_source/devices/uprobe/format/retprobe";

	return parse_uint_from_file(file, "config:%d\n");
}

static int perf_event_open_probe(bool uprobe, bool retprobe, const char *name,
				 uint64_t offset, int pid)
{
	struct perf_event_attr attr = {};
	char errmsg[STRERR_BUFSIZE];
	int type, pfd, err;

	type = uprobe ? determine_uprobe_perf_type()
		      : determine_kprobe_perf_type();
	if (type < 0) {
		pr_warning("failed to determine %s perf type: %s\n",
			   uprobe ? "uprobe" : "kprobe",
			   libbpf_strerror_r(type, errmsg, sizeof(errmsg)));
		return type;
	}
	if (retprobe) {
		int bit = uprobe ? determine_uprobe_retprobe_bit()
				 : determine_kprobe_retprobe_bit();

		if (bit < 0) {
			pr_warning("failed to determine %s retprobe bit: %s\n",
				   uprobe ? "uprobe" : "kprobe",
				   libbpf_strerror_r(bit, errmsg,
						     sizeof(errmsg)));
			return bit;
		}
		attr.config |= 1 << bit;
	}
	attr.size = sizeof(attr);
	attr.type = type;
	attr.config1 = ptr_to_u64(name); /* kprobe_func or uprobe_path */
	attr.config2 = offset;		 /* kprobe_addr or probe_offset */

	/* pid filter is meaningful only for uprobes */
	pfd = syscall(__NR_perf_event_open, &attr,
		      pid < 0 ? -1 : pid /* pid */,
		      pid == -1 ? 0 : -1 /* cpu */,
		      -1 /* group_fd */, PERF_FLAG_FD_CLOEXEC);
	if (pfd < 0) {
		err = -errno;
		pr_warning("%s perf_event_open() failed: %s\n",
			   uprobe ? "uprobe" : "kprobe",
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return err;
	}
	return pfd;
}

struct bpf_link *bpf_program__attach_kprobe(struct bpf_program *prog,
					    bool retprobe,
					    const char *func_name)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int pfd, err;

	pfd = perf_event_open_probe(false /* uprobe */, retprobe, func_name,
				    0 /* offset */, -1 /* pid */);
	if (pfd < 0) {
		pr_warning("program '%s': failed to create %s '%s' perf event: %s\n",
			   bpf_program__title(prog, false),
			   retprobe ? "kretprobe" : "kprobe", func_name,
			   libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link = bpf_program__attach_perf_event(prog, pfd);
	if (IS_ERR(link)) {
		close(pfd);
		err = PTR_ERR(link);
		pr_warning("program '%s': failed to attach to %s '%s': %s\n",
			   bpf_program__title(prog, false),
			   retprobe ? "kretprobe" : "kprobe", func_name,
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return link;
	}
	return link;
}

struct bpf_link *bpf_program__attach_uprobe(struct bpf_program *prog,
					    bool retprobe, pid_t pid,
					    const char *binary_path,
					    size_t func_offset)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int pfd, err;

	pfd = perf_event_open_probe(true /* uprobe */, retprobe,
				    binary_path, func_offset, pid);
	if (pfd < 0) {
		pr_warning("program '%s': failed to create %s '%s:0x%zx' perf event: %s\n",
			   bpf_program__title(prog, false),
			   retprobe ? "uretprobe" : "uprobe",
			   binary_path, func_offset,
			   libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link = bpf_program__attach_perf_event(prog, pfd);
	if (IS_ERR(link)) {
		close(pfd);
		err = PTR_ERR(link);
		pr_warning("program '%s': failed to attach to %s '%s:0x%zx': %s\n",
			   bpf_program__title(prog, false),
			   retprobe ? "uretprobe" : "uprobe",
			   binary_path, func_offset,
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return link;
	}
	return link;
}

static int determine_tracepoint_id(const char *tp_category,
				   const char *tp_name)
{
	char file[PATH_MAX];
	int ret;

	ret = snprintf(file, sizeof(file),
		       "/sys/kernel/debug/tracing/events/%s/%s/id",
		       tp_category, tp_name);
	if (ret < 0)
		return -errno;
	if (ret >= sizeof(file)) {
		pr_debug("tracepoint %s/%s path is too long\n",
			 tp_category, tp_name);
		return -E2BIG;
	}
	return parse_uint_from_file(file, "%d\n");
}

static int perf_event_open_tracepoint(const char *tp_category,
				      const char *tp_name)
{
	struct perf_event_attr attr = {};
	char errmsg[STRERR_BUFSIZE];
	int tp_id, pfd, err;

	tp_id = determine_tracepoint_id(tp_category, tp_name);
	if (tp_id < 0) {
		pr_warning("failed to determine tracepoint '%s/%s' perf event ID: %s\n",
			   tp_category, tp_name,
			   libbpf_strerror_r(tp_id, errmsg, sizeof(errmsg)));
		return tp_id;
	}

	attr.type = PERF_TYPE_TRACEPOINT;
	attr.size = sizeof(attr);
	attr.config = tp_id;

	pfd = syscall(__NR_perf_event_open, &attr, -1 /* pid */, 0 /* cpu */,
		      -1 /* group_fd */, PERF_FLAG_FD_CLOEXEC);
	if (pfd < 0) {
		err = -errno;
		pr_warning("tracepoint '%s/%s' perf_event_open() failed: %s\n",
			   tp_category, tp_name,
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return err;
	}
	return pfd;
}

struct bpf_link *bpf_program__attach_tracepoint(struct bpf_program *prog,
						const char *tp_category,
						const char *tp_name)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int pfd, err;

	pfd = perf_event_open_tracepoint(tp_category, tp_name);
	if (pfd < 0) {
		pr_warning("program '%s': failed to create tracepoint '%s/%s' perf event: %s\n",
			   bpf_program__title(prog, false),
			   tp_category, tp_name,
			   libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link = bpf_program__attach_perf_event(prog, pfd);
	if (IS_ERR(link)) {
		close(pfd);
		err = PTR_ERR(link);
		pr_warning("program '%s': failed to attach to tracepoint '%s/%s': %s\n",
			   bpf_program__title(prog, false),
			   tp_category, tp_name,
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return link;
	}
	return link;
}

static int bpf_link__destroy_fd(struct bpf_link *link)
{
	struct bpf_link_fd *l = (void *)link;

	return close(l->fd);
}

struct bpf_link *bpf_program__attach_raw_tracepoint(struct bpf_program *prog,
						    const char *tp_name)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link_fd *link;
	int prog_fd, pfd;

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warning("program '%s': can't attach before loaded\n",
			   bpf_program__title(prog, false));
		return ERR_PTR(-EINVAL);
	}

	link = malloc(sizeof(*link));
	if (!link)
		return ERR_PTR(-ENOMEM);
	link->link.destroy = &bpf_link__destroy_fd;

	pfd = bpf_raw_tracepoint_open(tp_name, prog_fd);
	if (pfd < 0) {
		pfd = -errno;
		free(link);
		pr_warning("program '%s': failed to attach to raw tracepoint '%s': %s\n",
			   bpf_program__title(prog, false), tp_name,
			   libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link->fd = pfd;
	return (struct bpf_link *)link;
}

enum bpf_perf_event_ret
bpf_perf_event_read_simple(void *mmap_mem, size_t mmap_size, size_t page_size,
			   void **copy_mem, size_t *copy_size,
			   bpf_perf_event_print_t fn, void *private_data)
{
	struct perf_event_mmap_page *header = mmap_mem;
	__u64 data_head = ring_buffer_read_head(header);
	__u64 data_tail = header->data_tail;
	void *base = ((__u8 *)header) + page_size;
	int ret = LIBBPF_PERF_EVENT_CONT;
	struct perf_event_header *ehdr;
	size_t ehdr_size;

	while (data_head != data_tail) {
		ehdr = base + (data_tail & (mmap_size - 1));
		ehdr_size = ehdr->size;

		if (((void *)ehdr) + ehdr_size > base + mmap_size) {
			void *copy_start = ehdr;
			size_t len_first = base + mmap_size - copy_start;
			size_t len_secnd = ehdr_size - len_first;

			if (*copy_size < ehdr_size) {
				free(*copy_mem);
				*copy_mem = malloc(ehdr_size);
				if (!*copy_mem) {
					*copy_size = 0;
					ret = LIBBPF_PERF_EVENT_ERROR;
					break;
				}
				*copy_size = ehdr_size;
			}

			memcpy(*copy_mem, copy_start, len_first);
			memcpy(*copy_mem + len_first, base, len_secnd);
			ehdr = *copy_mem;
		}

		ret = fn(ehdr, private_data);
		data_tail += ehdr_size;
		if (ret != LIBBPF_PERF_EVENT_CONT)
			break;
	}

	ring_buffer_write_tail(header, data_tail);
	return ret;
}

struct perf_buffer;

struct perf_buffer_params {
	struct perf_event_attr *attr;
	/* if event_cb is specified, it takes precendence */
	perf_buffer_event_fn event_cb;
	/* sample_cb and lost_cb are higher-level common-case callbacks */
	perf_buffer_sample_fn sample_cb;
	perf_buffer_lost_fn lost_cb;
	void *ctx;
	int cpu_cnt;
	int *cpus;
	int *map_keys;
};

struct perf_cpu_buf {
	struct perf_buffer *pb;
	void *base; /* mmap()'ed memory */
	void *buf; /* for reconstructing segmented data */
	size_t buf_size;
	int fd;
	int cpu;
	int map_key;
};

struct perf_buffer {
	perf_buffer_event_fn event_cb;
	perf_buffer_sample_fn sample_cb;
	perf_buffer_lost_fn lost_cb;
	void *ctx; /* passed into callbacks */

	size_t page_size;
	size_t mmap_size;
	struct perf_cpu_buf **cpu_bufs;
	struct epoll_event *events;
	int cpu_cnt;
	int epoll_fd; /* perf event FD */
	int map_fd; /* BPF_MAP_TYPE_PERF_EVENT_ARRAY BPF map FD */
};

static void perf_buffer__free_cpu_buf(struct perf_buffer *pb,
				      struct perf_cpu_buf *cpu_buf)
{
	if (!cpu_buf)
		return;
	if (cpu_buf->base &&
	    munmap(cpu_buf->base, pb->mmap_size + pb->page_size))
		pr_warning("failed to munmap cpu_buf #%d\n", cpu_buf->cpu);
	if (cpu_buf->fd >= 0) {
		ioctl(cpu_buf->fd, PERF_EVENT_IOC_DISABLE, 0);
		close(cpu_buf->fd);
	}
	free(cpu_buf->buf);
	free(cpu_buf);
}

void perf_buffer__free(struct perf_buffer *pb)
{
	int i;

	if (!pb)
		return;
	if (pb->cpu_bufs) {
		for (i = 0; i < pb->cpu_cnt && pb->cpu_bufs[i]; i++) {
			struct perf_cpu_buf *cpu_buf = pb->cpu_bufs[i];

			bpf_map_delete_elem(pb->map_fd, &cpu_buf->map_key);
			perf_buffer__free_cpu_buf(pb, cpu_buf);
		}
		free(pb->cpu_bufs);
	}
	if (pb->epoll_fd >= 0)
		close(pb->epoll_fd);
	free(pb->events);
	free(pb);
}

static struct perf_cpu_buf *
perf_buffer__open_cpu_buf(struct perf_buffer *pb, struct perf_event_attr *attr,
			  int cpu, int map_key)
{
	struct perf_cpu_buf *cpu_buf;
	char msg[STRERR_BUFSIZE];
	int err;

	cpu_buf = calloc(1, sizeof(*cpu_buf));
	if (!cpu_buf)
		return ERR_PTR(-ENOMEM);

	cpu_buf->pb = pb;
	cpu_buf->cpu = cpu;
	cpu_buf->map_key = map_key;

	cpu_buf->fd = syscall(__NR_perf_event_open, attr, -1 /* pid */, cpu,
			      -1, PERF_FLAG_FD_CLOEXEC);
	if (cpu_buf->fd < 0) {
		err = -errno;
		pr_warning("failed to open perf buffer event on cpu #%d: %s\n",
			   cpu, libbpf_strerror_r(err, msg, sizeof(msg)));
		goto error;
	}

	cpu_buf->base = mmap(NULL, pb->mmap_size + pb->page_size,
			     PROT_READ | PROT_WRITE, MAP_SHARED,
			     cpu_buf->fd, 0);
	if (cpu_buf->base == MAP_FAILED) {
		cpu_buf->base = NULL;
		err = -errno;
		pr_warning("failed to mmap perf buffer on cpu #%d: %s\n",
			   cpu, libbpf_strerror_r(err, msg, sizeof(msg)));
		goto error;
	}

	if (ioctl(cpu_buf->fd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
		err = -errno;
		pr_warning("failed to enable perf buffer event on cpu #%d: %s\n",
			   cpu, libbpf_strerror_r(err, msg, sizeof(msg)));
		goto error;
	}

	return cpu_buf;

error:
	perf_buffer__free_cpu_buf(pb, cpu_buf);
	return (struct perf_cpu_buf *)ERR_PTR(err);
}

static struct perf_buffer *__perf_buffer__new(int map_fd, size_t page_cnt,
					      struct perf_buffer_params *p);

struct perf_buffer *perf_buffer__new(int map_fd, size_t page_cnt,
				     const struct perf_buffer_opts *opts)
{
	struct perf_buffer_params p = {};
	struct perf_event_attr attr = { 0, };

	attr.config = PERF_COUNT_SW_BPF_OUTPUT,
	attr.type = PERF_TYPE_SOFTWARE;
	attr.sample_type = PERF_SAMPLE_RAW;
	attr.sample_period = 1;
	attr.wakeup_events = 1;

	p.attr = &attr;
	p.sample_cb = opts ? opts->sample_cb : NULL;
	p.lost_cb = opts ? opts->lost_cb : NULL;
	p.ctx = opts ? opts->ctx : NULL;

	return __perf_buffer__new(map_fd, page_cnt, &p);
}

struct perf_buffer *
perf_buffer__new_raw(int map_fd, size_t page_cnt,
		     const struct perf_buffer_raw_opts *opts)
{
	struct perf_buffer_params p = {};

	p.attr = opts->attr;
	p.event_cb = opts->event_cb;
	p.ctx = opts->ctx;
	p.cpu_cnt = opts->cpu_cnt;
	p.cpus = opts->cpus;
	p.map_keys = opts->map_keys;

	return __perf_buffer__new(map_fd, page_cnt, &p);
}

static struct perf_buffer *__perf_buffer__new(int map_fd, size_t page_cnt,
					      struct perf_buffer_params *p)
{
	struct bpf_map_info map = {};
	char msg[STRERR_BUFSIZE];
	struct perf_buffer *pb;
	__u32 map_info_len;
	int err, i;

	if (page_cnt & (page_cnt - 1)) {
		pr_warning("page count should be power of two, but is %zu\n",
			   page_cnt);
		return ERR_PTR(-EINVAL);
	}

	map_info_len = sizeof(map);
	err = bpf_obj_get_info_by_fd(map_fd, &map, &map_info_len);
	if (err) {
		err = -errno;
		pr_warning("failed to get map info for map FD %d: %s\n",
			   map_fd, libbpf_strerror_r(err, msg, sizeof(msg)));
		return ERR_PTR(err);
	}

	if (map.type != BPF_MAP_TYPE_PERF_EVENT_ARRAY) {
		pr_warning("map '%s' should be BPF_MAP_TYPE_PERF_EVENT_ARRAY\n",
			   map.name);
		return ERR_PTR(-EINVAL);
	}

	pb = calloc(1, sizeof(*pb));
	if (!pb)
		return ERR_PTR(-ENOMEM);

	pb->event_cb = p->event_cb;
	pb->sample_cb = p->sample_cb;
	pb->lost_cb = p->lost_cb;
	pb->ctx = p->ctx;

	pb->page_size = getpagesize();
	pb->mmap_size = pb->page_size * page_cnt;
	pb->map_fd = map_fd;

	pb->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (pb->epoll_fd < 0) {
		err = -errno;
		pr_warning("failed to create epoll instance: %s\n",
			   libbpf_strerror_r(err, msg, sizeof(msg)));
		goto error;
	}

	if (p->cpu_cnt > 0) {
		pb->cpu_cnt = p->cpu_cnt;
	} else {
		pb->cpu_cnt = libbpf_num_possible_cpus();
		if (pb->cpu_cnt < 0) {
			err = pb->cpu_cnt;
			goto error;
		}
		if (map.max_entries < pb->cpu_cnt)
			pb->cpu_cnt = map.max_entries;
	}

	pb->events = calloc(pb->cpu_cnt, sizeof(*pb->events));
	if (!pb->events) {
		err = -ENOMEM;
		pr_warning("failed to allocate events: out of memory\n");
		goto error;
	}
	pb->cpu_bufs = calloc(pb->cpu_cnt, sizeof(*pb->cpu_bufs));
	if (!pb->cpu_bufs) {
		err = -ENOMEM;
		pr_warning("failed to allocate buffers: out of memory\n");
		goto error;
	}

	for (i = 0; i < pb->cpu_cnt; i++) {
		struct perf_cpu_buf *cpu_buf;
		int cpu, map_key;

		cpu = p->cpu_cnt > 0 ? p->cpus[i] : i;
		map_key = p->cpu_cnt > 0 ? p->map_keys[i] : i;

		cpu_buf = perf_buffer__open_cpu_buf(pb, p->attr, cpu, map_key);
		if (IS_ERR(cpu_buf)) {
			err = PTR_ERR(cpu_buf);
			goto error;
		}

		pb->cpu_bufs[i] = cpu_buf;

		err = bpf_map_update_elem(pb->map_fd, &map_key,
					  &cpu_buf->fd, 0);
		if (err) {
			err = -errno;
			pr_warning("failed to set cpu #%d, key %d -> perf FD %d: %s\n",
				   cpu, map_key, cpu_buf->fd,
				   libbpf_strerror_r(err, msg, sizeof(msg)));
			goto error;
		}

		pb->events[i].events = EPOLLIN;
		pb->events[i].data.ptr = cpu_buf;
		if (epoll_ctl(pb->epoll_fd, EPOLL_CTL_ADD, cpu_buf->fd,
			      &pb->events[i]) < 0) {
			err = -errno;
			pr_warning("failed to epoll_ctl cpu #%d perf FD %d: %s\n",
				   cpu, cpu_buf->fd,
				   libbpf_strerror_r(err, msg, sizeof(msg)));
			goto error;
		}
	}

	return pb;

error:
	if (pb)
		perf_buffer__free(pb);
	return ERR_PTR(err);
}

struct perf_sample_raw {
	struct perf_event_header header;
	uint32_t size;
	char data[0];
};

struct perf_sample_lost {
	struct perf_event_header header;
	uint64_t id;
	uint64_t lost;
	uint64_t sample_id;
};

static enum bpf_perf_event_ret
perf_buffer__process_record(struct perf_event_header *e, void *ctx)
{
	struct perf_cpu_buf *cpu_buf = ctx;
	struct perf_buffer *pb = cpu_buf->pb;
	void *data = e;

	/* user wants full control over parsing perf event */
	if (pb->event_cb)
		return pb->event_cb(pb->ctx, cpu_buf->cpu, e);

	switch (e->type) {
	case PERF_RECORD_SAMPLE: {
		struct perf_sample_raw *s = data;

		if (pb->sample_cb)
			pb->sample_cb(pb->ctx, cpu_buf->cpu, s->data, s->size);
		break;
	}
	case PERF_RECORD_LOST: {
		struct perf_sample_lost *s = data;

		if (pb->lost_cb)
			pb->lost_cb(pb->ctx, cpu_buf->cpu, s->lost);
		break;
	}
	default:
		pr_warning("unknown perf sample type %d\n", e->type);
		return LIBBPF_PERF_EVENT_ERROR;
	}
	return LIBBPF_PERF_EVENT_CONT;
}

static int perf_buffer__process_records(struct perf_buffer *pb,
					struct perf_cpu_buf *cpu_buf)
{
	enum bpf_perf_event_ret ret;

	ret = bpf_perf_event_read_simple(cpu_buf->base, pb->mmap_size,
					 pb->page_size, &cpu_buf->buf,
					 &cpu_buf->buf_size,
					 perf_buffer__process_record, cpu_buf);
	if (ret != LIBBPF_PERF_EVENT_CONT)
		return ret;
	return 0;
}

int perf_buffer__poll(struct perf_buffer *pb, int timeout_ms)
{
	int i, cnt, err;

	cnt = epoll_wait(pb->epoll_fd, pb->events, pb->cpu_cnt, timeout_ms);
	for (i = 0; i < cnt; i++) {
		struct perf_cpu_buf *cpu_buf = pb->events[i].data.ptr;

		err = perf_buffer__process_records(pb, cpu_buf);
		if (err) {
			pr_warning("error while processing records: %d\n", err);
			return err;
		}
	}
	return cnt < 0 ? -errno : cnt;
}

struct bpf_prog_info_array_desc {
	int	array_offset;	/* e.g. offset of jited_prog_insns */
	int	count_offset;	/* e.g. offset of jited_prog_len */
	int	size_offset;	/* > 0: offset of rec size,
				 * < 0: fix size of -size_offset
				 */
};

static struct bpf_prog_info_array_desc bpf_prog_info_array_desc[] = {
	[BPF_PROG_INFO_JITED_INSNS] = {
		offsetof(struct bpf_prog_info, jited_prog_insns),
		offsetof(struct bpf_prog_info, jited_prog_len),
		-1,
	},
	[BPF_PROG_INFO_XLATED_INSNS] = {
		offsetof(struct bpf_prog_info, xlated_prog_insns),
		offsetof(struct bpf_prog_info, xlated_prog_len),
		-1,
	},
	[BPF_PROG_INFO_MAP_IDS] = {
		offsetof(struct bpf_prog_info, map_ids),
		offsetof(struct bpf_prog_info, nr_map_ids),
		-(int)sizeof(__u32),
	},
	[BPF_PROG_INFO_JITED_KSYMS] = {
		offsetof(struct bpf_prog_info, jited_ksyms),
		offsetof(struct bpf_prog_info, nr_jited_ksyms),
		-(int)sizeof(__u64),
	},
	[BPF_PROG_INFO_JITED_FUNC_LENS] = {
		offsetof(struct bpf_prog_info, jited_func_lens),
		offsetof(struct bpf_prog_info, nr_jited_func_lens),
		-(int)sizeof(__u32),
	},
	[BPF_PROG_INFO_FUNC_INFO] = {
		offsetof(struct bpf_prog_info, func_info),
		offsetof(struct bpf_prog_info, nr_func_info),
		offsetof(struct bpf_prog_info, func_info_rec_size),
	},
	[BPF_PROG_INFO_LINE_INFO] = {
		offsetof(struct bpf_prog_info, line_info),
		offsetof(struct bpf_prog_info, nr_line_info),
		offsetof(struct bpf_prog_info, line_info_rec_size),
	},
	[BPF_PROG_INFO_JITED_LINE_INFO] = {
		offsetof(struct bpf_prog_info, jited_line_info),
		offsetof(struct bpf_prog_info, nr_jited_line_info),
		offsetof(struct bpf_prog_info, jited_line_info_rec_size),
	},
	[BPF_PROG_INFO_PROG_TAGS] = {
		offsetof(struct bpf_prog_info, prog_tags),
		offsetof(struct bpf_prog_info, nr_prog_tags),
		-(int)sizeof(__u8) * BPF_TAG_SIZE,
	},

};

static __u32 bpf_prog_info_read_offset_u32(struct bpf_prog_info *info, int offset)
{
	__u32 *array = (__u32 *)info;

	if (offset >= 0)
		return array[offset / sizeof(__u32)];
	return -(int)offset;
}

static __u64 bpf_prog_info_read_offset_u64(struct bpf_prog_info *info, int offset)
{
	__u64 *array = (__u64 *)info;

	if (offset >= 0)
		return array[offset / sizeof(__u64)];
	return -(int)offset;
}

static void bpf_prog_info_set_offset_u32(struct bpf_prog_info *info, int offset,
					 __u32 val)
{
	__u32 *array = (__u32 *)info;

	if (offset >= 0)
		array[offset / sizeof(__u32)] = val;
}

static void bpf_prog_info_set_offset_u64(struct bpf_prog_info *info, int offset,
					 __u64 val)
{
	__u64 *array = (__u64 *)info;

	if (offset >= 0)
		array[offset / sizeof(__u64)] = val;
}

struct bpf_prog_info_linear *
bpf_program__get_prog_info_linear(int fd, __u64 arrays)
{
	struct bpf_prog_info_linear *info_linear;
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	__u32 data_len = 0;
	int i, err;
	void *ptr;

	if (arrays >> BPF_PROG_INFO_LAST_ARRAY)
		return ERR_PTR(-EINVAL);

	/* step 1: get array dimensions */
	err = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (err) {
		pr_debug("can't get prog info: %s", strerror(errno));
		return ERR_PTR(-EFAULT);
	}

	/* step 2: calculate total size of all arrays */
	for (i = BPF_PROG_INFO_FIRST_ARRAY; i < BPF_PROG_INFO_LAST_ARRAY; ++i) {
		bool include_array = (arrays & (1UL << i)) > 0;
		struct bpf_prog_info_array_desc *desc;
		__u32 count, size;

		desc = bpf_prog_info_array_desc + i;

		/* kernel is too old to support this field */
		if (info_len < desc->array_offset + sizeof(__u32) ||
		    info_len < desc->count_offset + sizeof(__u32) ||
		    (desc->size_offset > 0 && info_len < desc->size_offset))
			include_array = false;

		if (!include_array) {
			arrays &= ~(1UL << i);	/* clear the bit */
			continue;
		}

		count = bpf_prog_info_read_offset_u32(&info, desc->count_offset);
		size  = bpf_prog_info_read_offset_u32(&info, desc->size_offset);

		data_len += count * size;
	}

	/* step 3: allocate continuous memory */
	data_len = roundup(data_len, sizeof(__u64));
	info_linear = malloc(sizeof(struct bpf_prog_info_linear) + data_len);
	if (!info_linear)
		return ERR_PTR(-ENOMEM);

	/* step 4: fill data to info_linear->info */
	info_linear->arrays = arrays;
	memset(&info_linear->info, 0, sizeof(info));
	ptr = info_linear->data;

	for (i = BPF_PROG_INFO_FIRST_ARRAY; i < BPF_PROG_INFO_LAST_ARRAY; ++i) {
		struct bpf_prog_info_array_desc *desc;
		__u32 count, size;

		if ((arrays & (1UL << i)) == 0)
			continue;

		desc  = bpf_prog_info_array_desc + i;
		count = bpf_prog_info_read_offset_u32(&info, desc->count_offset);
		size  = bpf_prog_info_read_offset_u32(&info, desc->size_offset);
		bpf_prog_info_set_offset_u32(&info_linear->info,
					     desc->count_offset, count);
		bpf_prog_info_set_offset_u32(&info_linear->info,
					     desc->size_offset, size);
		bpf_prog_info_set_offset_u64(&info_linear->info,
					     desc->array_offset,
					     ptr_to_u64(ptr));
		ptr += count * size;
	}

	/* step 5: call syscall again to get required arrays */
	err = bpf_obj_get_info_by_fd(fd, &info_linear->info, &info_len);
	if (err) {
		pr_debug("can't get prog info: %s", strerror(errno));
		free(info_linear);
		return ERR_PTR(-EFAULT);
	}

	/* step 6: verify the data */
	for (i = BPF_PROG_INFO_FIRST_ARRAY; i < BPF_PROG_INFO_LAST_ARRAY; ++i) {
		struct bpf_prog_info_array_desc *desc;
		__u32 v1, v2;

		if ((arrays & (1UL << i)) == 0)
			continue;

		desc = bpf_prog_info_array_desc + i;
		v1 = bpf_prog_info_read_offset_u32(&info, desc->count_offset);
		v2 = bpf_prog_info_read_offset_u32(&info_linear->info,
						   desc->count_offset);
		if (v1 != v2)
			pr_warning("%s: mismatch in element count\n", __func__);

		v1 = bpf_prog_info_read_offset_u32(&info, desc->size_offset);
		v2 = bpf_prog_info_read_offset_u32(&info_linear->info,
						   desc->size_offset);
		if (v1 != v2)
			pr_warning("%s: mismatch in rec size\n", __func__);
	}

	/* step 7: update info_len and data_len */
	info_linear->info_len = sizeof(struct bpf_prog_info);
	info_linear->data_len = data_len;

	return info_linear;
}

void bpf_program__bpil_addr_to_offs(struct bpf_prog_info_linear *info_linear)
{
	int i;

	for (i = BPF_PROG_INFO_FIRST_ARRAY; i < BPF_PROG_INFO_LAST_ARRAY; ++i) {
		struct bpf_prog_info_array_desc *desc;
		__u64 addr, offs;

		if ((info_linear->arrays & (1UL << i)) == 0)
			continue;

		desc = bpf_prog_info_array_desc + i;
		addr = bpf_prog_info_read_offset_u64(&info_linear->info,
						     desc->array_offset);
		offs = addr - ptr_to_u64(info_linear->data);
		bpf_prog_info_set_offset_u64(&info_linear->info,
					     desc->array_offset, offs);
	}
}

void bpf_program__bpil_offs_to_addr(struct bpf_prog_info_linear *info_linear)
{
	int i;

	for (i = BPF_PROG_INFO_FIRST_ARRAY; i < BPF_PROG_INFO_LAST_ARRAY; ++i) {
		struct bpf_prog_info_array_desc *desc;
		__u64 addr, offs;

		if ((info_linear->arrays & (1UL << i)) == 0)
			continue;

		desc = bpf_prog_info_array_desc + i;
		offs = bpf_prog_info_read_offset_u64(&info_linear->info,
						     desc->array_offset);
		addr = offs + ptr_to_u64(info_linear->data);
		bpf_prog_info_set_offset_u64(&info_linear->info,
					     desc->array_offset, addr);
	}
}

int libbpf_num_possible_cpus(void)
{
	static const char *fcpu = "/sys/devices/system/cpu/possible";
	int len = 0, n = 0, il = 0, ir = 0;
	unsigned int start = 0, end = 0;
	int tmp_cpus = 0;
	static int cpus;
	char buf[128];
	int error = 0;
	int fd = -1;

	tmp_cpus = READ_ONCE(cpus);
	if (tmp_cpus > 0)
		return tmp_cpus;

	fd = open(fcpu, O_RDONLY);
	if (fd < 0) {
		error = errno;
		pr_warning("Failed to open file %s: %s\n",
			   fcpu, strerror(error));
		return -error;
	}
	len = read(fd, buf, sizeof(buf));
	close(fd);
	if (len <= 0) {
		error = len ? errno : EINVAL;
		pr_warning("Failed to read # of possible cpus from %s: %s\n",
			   fcpu, strerror(error));
		return -error;
	}
	if (len == sizeof(buf)) {
		pr_warning("File %s size overflow\n", fcpu);
		return -EOVERFLOW;
	}
	buf[len] = '\0';

	for (ir = 0, tmp_cpus = 0; ir <= len; ir++) {
		/* Each sub string separated by ',' has format \d+-\d+ or \d+ */
		if (buf[ir] == ',' || buf[ir] == '\0') {
			buf[ir] = '\0';
			n = sscanf(&buf[il], "%u-%u", &start, &end);
			if (n <= 0) {
				pr_warning("Failed to get # CPUs from %s\n",
					   &buf[il]);
				return -EINVAL;
			} else if (n == 1) {
				end = start;
			}
			tmp_cpus += end - start + 1;
			il = ir + 1;
		}
	}
	if (tmp_cpus <= 0) {
		pr_warning("Invalid #CPUs %d from %s\n", tmp_cpus, fcpu);
		return -EINVAL;
	}

	WRITE_ONCE(cpus, tmp_cpus);
	return tmp_cpus;
}
